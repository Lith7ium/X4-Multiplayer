//! missions.py: host-native faction mission journal. Never awards money or invents
//! progress. Complete, checksummed records survive packet loss and late joins; IDs
//! are scoped to the host lease; terminal records are retained, never evicted.

use std::collections::BTreeMap;
use std::net::SocketAddr;

use super::{Fields, Packet, Session};
use crate::pynum::py_int;
use crate::wire::{adler32, hex_utf8, is_lower_hex_pairs, seq, Hex32, Parsed, Reject, Slot, MAX_PACKET};

const LIMIT: u32 = 256;
const PART_SIZE: usize = 500;
const MAX_PARTS: i128 = 24;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum MissionState {
    Offered,
    Accepted,
    Completed,
    Failed,
    Aborted,
    Withdrawn,
}

impl MissionState {
    fn parse(text: &str) -> Parsed<Self> {
        Ok(match text {
            "O" => Self::Offered,
            "A" => Self::Accepted,
            "C" => Self::Completed,
            "F" => Self::Failed,
            "X" => Self::Aborted,
            "W" => Self::Withdrawn,
            _ => return Err(Reject),
        })
    }

    /// missions.py `TRANSITIONS`; terminal states have none.
    fn may_become(self, next: Self) -> bool {
        use MissionState::*;
        match self {
            Offered => true,
            Accepted => matches!(next, Accepted | Completed | Failed | Aborted),
            _ => false,
        }
    }
}

#[derive(Clone, PartialEq, Eq, Debug)]
struct Row {
    state: MissionState,
    done: u32,
    total: u32,
    reward: u32,
    faction: String,
    title: String,
    description: String,
    objective: String,
}

fn text_field(value: &str, maximum: usize) -> Parsed<String> {
    if value.len() > maximum * 2 || !is_lower_hex_pairs(value) {
        return Err(Reject);
    }
    let text = hex_utf8(value)?;
    if text.chars().any(|c| (c < ' ' && c != '\n' && c != '\t') || c == '\x7f') {
        return Err(Reject);
    }
    Ok(text)
}

fn is_faction(text: &str) -> bool {
    crate::wire::is_ware(text) // same `[a-z][a-z0-9_]{0,63}` shape
}

fn parse_record(payload: &str) -> Parsed<Row> {
    let f: Vec<&str> = payload.split(',').collect();
    if f.len() != 9 || f[0] != "quota" {
        return Err(Reject);
    }
    let state = MissionState::parse(f[1])?;
    let (done, total, reward) = (seq(f[2])?, seq(f[3])?, seq(f[4])?);
    if !(done <= total && total <= 1_000_000) || total == 0 || !is_faction(f[5]) {
        return Err(Reject);
    }
    if (state == MissionState::Offered && done != 0) || (state == MissionState::Completed && done != total) {
        return Err(Reject);
    }
    Ok(Row {
        state,
        done,
        total,
        reward,
        faction: f[5].to_owned(),
        title: text_field(f[6], 256)?,
        description: text_field(f[7], 4096)?,
        objective: text_field(f[8], 512)?,
    })
}

fn advances(old: Option<&Row>, new: &Row) -> bool {
    // A terminal fact may be the first retained record after joining.
    let Some(old) = old else { return true };
    old.state.may_become(new.state)
        && new.done >= old.done
        && (&old.total, &old.reward, &old.faction, &old.title, &old.description, &old.objective)
            == (&new.total, &new.reward, &new.faction, &new.title, &new.description, &new.objective)
}

struct Record {
    rev: u32,
    row: Row,
    payload: String,
    digest: u32,
}

struct Staged {
    rev: u32,
    total: u32,
    digest: u32,
    parts: BTreeMap<u32, String>,
    at: f64,
}

#[derive(Default)]
pub struct MissionJournal {
    epoch: Option<Hex32>,
    records: BTreeMap<u32, Record>,
    pending: BTreeMap<u32, Staged>,
    requests: crate::ordmap::OrderedMap<u32, (Hex32, u32, String)>,
    cursor: usize,
    next_send: f64,
    changed: Vec<u32>,
}

impl Session {
    /// The journal is scoped to the current host lease; a new host starts empty.
    fn mission_host(&mut self) -> Option<Hex32> {
        let host = self.core.peers.get(&Slot::P1)?.lease;
        let m = &mut self.coop.missions;
        if m.epoch != Some(host) {
            m.epoch = Some(host);
            m.records.clear();
            m.pending.clear();
            m.requests.clear();
            m.cursor = 0;
            m.changed.clear();
        }
        Some(host)
    }

    pub(super) fn receive_mission(&mut self, raw: &[u8], address: SocketAddr, now: f64) {
        if raw.len() > MAX_PACKET {
            return;
        }
        let Some(fields) = Fields::decode(raw) else { return };
        let f = &fields.f;
        if f.len() < 7 || f[1] != "1" {
            return;
        }
        let host = self.mission_host();
        let peer = self.authenticated(f[2], address);
        let (Some(host), Some((slot, lease))) = (host, peer) else { return };
        if f[3] != host.as_str() {
            return;
        }
        let _ = self.mission_update(f, slot, lease, now);
    }

    fn mission_update(&mut self, f: &[&str], slot: Slot, lease: Hex32, now: f64) -> Parsed<()> {
        let (ident, rev) = (seq(f[4])?, seq(f[5])?);
        if !(1..=LIMIT).contains(&ident) || rev == 0 {
            return Err(Reject);
        }
        let m = &mut self.coop.missions;
        match (f[0], f.len(), slot) {
            ("MREQ", 7, _) if slot != Slot::P1 => {
                if let Some(old) = m.records.get(&ident) {
                    let allowed = matches!(
                        (old.row.state, f[6]),
                        (MissionState::Offered, "ACCEPT") | (MissionState::Accepted, "ABORT")
                    );
                    if old.rev == rev && allowed {
                        m.requests.insert(ident, (lease, rev, f[6].to_owned()));
                    }
                }
            }
            ("MSTATE", 10, Slot::P1) => {
                if m.records.get(&ident).is_some_and(|old| rev <= old.rev) {
                    return Err(Reject);
                }
                let (part, total, digest) =
                    (py_int(f[6]).ok_or(Reject)?, py_int(f[7]).ok_or(Reject)?, py_int(f[8]).ok_or(Reject)?);
                let bounded = 1 <= part && part <= total && total <= MAX_PARTS && (0..=0xffff_ffff).contains(&digest);
                if !bounded || !(1..=PART_SIZE).contains(&f[9].len()) {
                    return Err(Reject);
                }
                if !f[9].bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_' || c == b',') {
                    return Err(Reject);
                }
                let (part, total, digest) = (part as u32, total as u32, digest as u32);
                if m.pending.get(&ident).is_some_and(|p| rev < p.rev) {
                    return Err(Reject);
                }
                let restart = match m.pending.get(&ident) {
                    None => true,
                    Some(p) => rev > p.rev || now - p.at >= 10.0,
                };
                if restart {
                    m.pending.insert(ident, Staged { rev, total, digest, parts: BTreeMap::new(), at: now });
                }
                let p = m.pending.get_mut(&ident).expect("staged");
                if (total, digest) != (p.total, p.digest) {
                    return Err(Reject);
                }
                if p.parts.get(&part).is_some_and(|old| old != f[9]) {
                    return Err(Reject);
                }
                p.parts.insert(part, f[9].to_owned());
                if p.parts.len() as u32 != total {
                    return Err(Reject);
                }
                let payload: String = p.parts.values().map(String::as_str).collect();
                if adler32(payload.as_bytes()) != digest {
                    return Err(Reject);
                }
                let row = parse_record(&payload)?;
                if !advances(m.records.get(&ident).map(|r| &r.row), &row) {
                    return Err(Reject);
                }
                m.records.insert(ident, Record { rev, row, payload, digest });
                m.pending.remove(&ident);
                if !m.changed.contains(&ident) {
                    m.changed.push(ident);
                }
                m.requests.remove(&ident);
            }
            _ => {}
        }
        Ok(())
    }

    pub(super) fn snapshots_missions(&mut self, now: f64, out: &mut Vec<Packet>) {
        let Some(host) = self.mission_host() else { return };
        let m = &mut self.coop.missions;
        if now < m.next_send {
            return;
        }
        m.next_send = now + 0.25;
        if !self.core.dedicated {
            let Some(guest) = self.core.peers.get(&Slot::P2) else { return };
            let host_address = self.core.peers.get(&Slot::P1).expect("host").address;
            if !m.records.is_empty() {
                let ids: Vec<u32> = m.records.keys().copied().collect();
                m.cursor %= ids.len();
                let ident = if m.changed.is_empty() { ids[m.cursor] } else { m.changed.remove(0) };
                m.cursor += 1;
                let r = &m.records[&ident];
                let parts: Vec<&str> =
                    r.payload.as_bytes().chunks(PART_SIZE).map(|c| std::str::from_utf8(c).expect("ascii")).collect();
                for (i, part) in parts.iter().enumerate() {
                    let packet = format!(
                        "MSTATE|1|{}|{host}|{ident}|{}|{}|{}|{}|{part}",
                        guest.lease,
                        r.rev,
                        i + 1,
                        parts.len(),
                        r.digest
                    );
                    out.push((guest.address, packet.into_bytes()));
                }
            }
            let guest_lease = guest.lease;
            m.requests.retain(|_, (lease, _, _)| *lease == guest_lease);
            for (ident, (_, rev, action)) in m.requests.iter() {
                out.push((host_address, format!("MREQ|1|{host}|{host}|{ident}|{rev}|{action}").into_bytes()));
            }
            return;
        }
        let guests: Vec<Hex32> =
            self.core.peers.iter().filter(|(slot, _)| **slot != Slot::P1).map(|(_, peer)| peer.lease).collect();
        let host_address = self.core.peers.get(&Slot::P1).expect("host").address;
        // One record per tick, independent of either player's sector or attention.
        if !m.records.is_empty() {
            let ids: Vec<u32> = m.records.keys().copied().collect();
            m.cursor %= ids.len();
            let ident = if m.changed.is_empty() { ids[m.cursor] } else { m.changed.remove(0) };
            m.cursor += 1;
            let r = &m.records[&ident];
            let parts: Vec<&str> =
                r.payload.as_bytes().chunks(PART_SIZE).map(|c| std::str::from_utf8(c).expect("ascii")).collect();
            for guest_lease in &guests {
                let guest = self.core.peers.values().find(|peer| peer.lease == *guest_lease).expect("listed");
                for (i, part) in parts.iter().enumerate() {
                    let packet = format!(
                        "MSTATE|1|{guest_lease}|{host}|{ident}|{}|{}|{}|{}|{part}",
                        r.rev,
                        i + 1,
                        parts.len(),
                        r.digest
                    );
                    out.push((guest.address, packet.into_bytes()));
                }
            }
        }
        m.requests.retain(|_, (lease, _, _)| guests.contains(lease));
        for (ident, (_, rev, action)) in m.requests.iter() {
            out.push((host_address, format!("MREQ|1|{host}|{host}|{ident}|{rev}|{action}").into_bytes()));
        }
    }
}
