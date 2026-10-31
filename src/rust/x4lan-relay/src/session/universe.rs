//! universe.py: W01 sector snapshot transport (opt-in, `world1-` builds only).
//!
//! Bounded, acknowledged, atomic object baselines on top of co-op. It does not
//! simulate NPCs, validate pickups, or synchronize vanilla missions. Absence means
//! removal from a sector view, not proof of global destruction.

use std::collections::{BTreeMap, BTreeSet};
use std::net::SocketAddr;

use super::cargo::CargoTransactions;
use super::identity::Identities;
use super::npc::NpcCombat;
use super::{Fields, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{adler32, bounded, float, pose, seq, Atom, Hex32, Parsed, Reject, Slot, MAX_PACKET, MAX_SEQ};

const MAX_OBJECTS: u32 = 4096;
const MAX_RECORD: usize = 512;
const WINDOW: usize = 12;
const MOTION_BUDGET: usize = 32;
/// Unchanged samples repeated per tick for datagram loss (universe.py `REPEAT_BUDGET`).
const REPEAT_BUDGET: usize = 2;
const RETRY: f64 = 0.25;
const STAGE_TIMEOUT: f64 = 15.0;
const POSITION_LIMIT: f64 = 50_000_000.0;

/// `str.isspace()` for ASCII, which (unlike `float()`) includes \x1c-\x1f.
fn is_str_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c | 0x1c..=0x1f)
}

/// One object record `id,kind,macro,owner,x,y,z,yaw,pitch,roll,ware,amount`;
/// returns its ID. Ships carry no cargo; drops carry a ware and a positive amount.
pub fn record(value: &str) -> Parsed<u32> {
    if value.len() > MAX_RECORD || !value.is_ascii() || value.bytes().any(is_str_space) {
        return Err(Reject);
    }
    let f: Vec<&str> = value.split(',').collect();
    if f.len() != 12 {
        return Err(Reject);
    }
    let id = seq(f[0])?;
    if id == 0 || !matches!(f[1], "ship" | "drop") {
        return Err(Reject);
    }
    Atom::parse_macro(f[2])?;
    Atom::parse(f[3])?;
    Atom::parse(f[10])?;
    let mut values = [0.0; 6];
    for (i, text) in f[4..10].iter().enumerate() {
        values[i] = float(text)?;
    }
    if !bounded(&values, POSITION_LIMIT, std::f64::consts::TAU) {
        return Err(Reject);
    }
    let amount = seq(f[11])?;
    let ship = f[1] == "ship";
    if amount > 1_000_000 || (ship && (f[10] != "none" || amount != 0)) || (!ship && (f[10] == "none" || amount == 0)) {
        return Err(Reject);
    }
    Ok(id)
}

/// A complete, validated sector view. Only [`Baseline::create`] builds one.
#[derive(Clone, Debug, PartialEq)]
pub struct Baseline {
    pub epoch: Hex32,
    pub revision: u32,
    pub sector: Atom,
    pub rows: Vec<String>,
    pub digest: u32,
}

impl Baseline {
    pub fn create(epoch: Hex32, revision: u32, sector: Atom, rows: Vec<String>) -> Parsed<Self> {
        if revision == 0
            || revision > MAX_SEQ
            || !sector.as_str().ends_with("_macro")
            || rows.len() > MAX_OBJECTS as usize
        {
            return Err(Reject);
        }
        let mut previous = 0;
        for row in &rows {
            let id = record(row)?;
            if id <= previous {
                return Err(Reject); // unique, increasing IDs
            }
            previous = id;
        }
        let digest = adler32(rows.join("\n").as_bytes());
        Ok(Baseline { epoch, revision, sector, rows, digest })
    }

    fn packet(&self, lease: Hex32, index: usize) -> Vec<u8> {
        let head = format!("|1|{lease}|{}|{}", self.epoch, self.revision);
        let text = if index == 0 {
            format!("UBEGIN{head}|{}|{}|{}", self.sector, self.rows.len(), self.digest)
        } else if index == self.rows.len() + 1 {
            format!("UCOMMIT{head}")
        } else {
            format!("UOBJECT{head}|{index}|{}", self.rows[index - 1])
        };
        // Rows arrived inside packets at least this long, so this cannot overflow.
        debug_assert!(text.len() <= MAX_PACKET);
        text.into_bytes()
    }
}

/// An in-flight baseline: (epoch, revision, sector, count, digest) plus staged rows.
struct Staged {
    identity: (String, u32, Atom, u32, u64),
    rows: BTreeMap<u32, String>,
    at: f64,
}

/// What the receiver answers: an acknowledgement index (or -1: restart) and, on a
/// verified commit, the new baseline.
type Ack = Option<(i64, Option<Baseline>)>;

/// Only a verified commit replaces visible state. At most one staged sector.
#[derive(Default)]
pub struct Receiver {
    pub latest: Option<Baseline>,
    pending: Option<Staged>,
}

impl Receiver {
    fn receive(&mut self, f: &[&str], now: f64) -> Parsed<Ack> {
        if f.len() < 5 || Hex32::parse(f[3]).is_err() {
            return Ok(None);
        }
        let (epoch, rev) = (f[3], seq(f[4])?);
        if rev == 0 {
            return Ok(None);
        }
        if let Some(old) = &self.latest {
            if epoch != old.epoch.as_str() || rev < old.revision {
                return Ok(None);
            }
            if rev == old.revision {
                // Lost final acknowledgement: retransmit completion, not effects.
                return Ok(Some((old.rows.len() as i64 + 1, None)));
            }
        }
        if self.pending.as_ref().is_some_and(|p| now - p.at > STAGE_TIMEOUT) {
            self.pending = None;
        }
        if f[0] == "UBEGIN" && f.len() == 8 {
            let sector = Atom::parse_macro(f[5])?;
            let count = seq(f[6])?;
            if count > MAX_OBJECTS || f[7].is_empty() || !f[7].bytes().all(|c| c.is_ascii_digit()) {
                return Ok(None);
            }
            let digest = f[7].bytes().fold(0u64, |acc, d| acc.saturating_mul(10).saturating_add(u64::from(d - b'0')));
            if digest > 0xffff_ffff {
                return Ok(None);
            }
            let identity = (epoch.to_owned(), rev, sector, count, digest);
            match &self.pending {
                // A newer begin cannot evict in-flight work.
                Some(p) if p.identity != identity => return Ok(None),
                Some(_) => {}
                None => self.pending = Some(Staged { identity, rows: BTreeMap::new(), at: now }),
            }
            return Ok(Some((0, None)));
        }
        let Some(p) = self.pending.as_mut().filter(|p| p.identity.0 == epoch && p.identity.1 == rev) else {
            return Ok(Some((-1, None))); // the sender must restart from its begin
        };
        let (count, digest) = (p.identity.3, p.identity.4);
        if f[0] == "UOBJECT" && f.len() == 7 {
            let index = seq(f[5])?;
            record(f[6])?;
            if !(1..=count).contains(&index) {
                return Ok(None);
            }
            match p.rows.get(&index) {
                Some(previous) if previous != f[6] => {
                    self.pending = None;
                    return Ok(Some((-1, None)));
                }
                Some(_) => {}
                None => {
                    p.rows.insert(index, f[6].to_owned());
                    p.at = now;
                }
            }
            return Ok(Some((i64::from(index), None)));
        }
        if f[0] == "UCOMMIT" && f.len() == 5 {
            if p.rows.len() as u32 != count {
                return Ok(None);
            }
            let rows: Vec<String> = p.rows.values().cloned().collect();
            let sector = p.identity.2.clone();
            let epoch = Hex32::parse(epoch).expect("checked");
            match Baseline::create(epoch, rev, sector, rows) {
                Ok(baseline) if u64::from(baseline.digest) == digest => {
                    self.latest = Some(baseline.clone());
                    self.pending = None;
                    return Ok(Some((i64::from(count) + 1, Some(baseline))));
                }
                _ => {
                    self.pending = None;
                    return Ok(Some((-1, None)));
                }
            }
        }
        Ok(None)
    }
}

/// Selective acknowledgement with a bounded sliding window and retries.
pub struct Sender {
    pub baseline: Baseline,
    acked: BTreeSet<usize>,
    sent_at: BTreeMap<usize, f64>,
    pub done: bool,
}

impl Sender {
    fn new(baseline: Baseline) -> Self {
        Sender { baseline, acked: BTreeSet::new(), sent_at: BTreeMap::new(), done: false }
    }

    fn acknowledge(&mut self, epoch: &str, revision: u32, index: i64) {
        let b = &self.baseline;
        if epoch != b.epoch.as_str() || revision != b.revision {
            return;
        }
        let end = b.rows.len() as i64 + 1;
        if index == -1 {
            self.acked.clear();
            self.sent_at.clear();
        } else if index == end && self.sent_at.contains_key(&(end as usize)) {
            self.done = true;
        } else if (0..end).contains(&index) && self.sent_at.contains_key(&(index as usize)) {
            self.acked.insert(index as usize);
        }
    }

    fn packets(&mut self, lease: Hex32, now: f64) -> Vec<Vec<u8>> {
        if self.done {
            return vec![];
        }
        let count = self.baseline.rows.len();
        let indices: Vec<usize> = if !self.acked.contains(&0) {
            vec![0]
        } else {
            let outstanding: Vec<usize> = (1..=count).filter(|i| !self.acked.contains(i)).take(WINDOW).collect();
            if outstanding.is_empty() {
                vec![count + 1]
            } else {
                outstanding
            }
        };
        let mut out = Vec::new();
        for i in indices {
            if self.sent_at.get(&i).is_none_or(|at| now - at >= RETRY) {
                self.sent_at.insert(i, now);
                out.push(self.baseline.packet(lease, i));
            }
        }
        out
    }
}

pub struct Interest {
    pub lease: Hex32,
    pub seq: u32,
    pub sector: Atom,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum MotionKind {
    Upos,
    Umove,
}

struct Position {
    seq: u32,
    at: f64,
    sector: Atom,
    fields: Vec<String>,
    kind: MotionKind,
}

#[derive(Default)]
pub struct GuestView {
    pub interest: Option<Interest>,
    interest_after_revision: u32,
    delivery: Option<Sender>,
    delivered: Option<u32>,
    position_cursor: usize,
    /// entity -> motion seq last forwarded to this client (universe.py `forwarded`).
    forwarded: OrderedMap<u32, u32>,
}

#[derive(Default)]
pub struct Universe {
    pub receiver: Receiver,
    /// The host lease that owns the world epoch for the whole relay life.
    pub epoch: Option<Hex32>,
    sectors: OrderedMap<Atom, Baseline>,
    pub entity_ids: OrderedMap<Atom, BTreeSet<u32>>,
    pub views: OrderedMap<Hex32, GuestView>,
    positions: OrderedMap<u32, Position>,
    pub cargo: CargoTransactions,
    pub identities: Identities,
    pub npc: NpcCombat,
}

impl Universe {
    pub fn interest(&self, lease: Hex32) -> Option<&Interest> {
        self.views.get(&lease).and_then(|view| view.interest.as_ref())
    }
}

const UNIVERSE_PREFIXES: [&[u8]; 7] =
    [b"UBEGIN|", b"UOBJECT|", b"UCOMMIT|", b"UACK|", b"UINTEREST|", b"UPOS|", b"UMOVE|"];

impl Session {
    /// UniverseSession.receive -> DockSession.receive -> CoopSession.receive.
    pub(crate) fn receive_universe(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        let starts = |prefixes: &[&[u8]]| prefixes.iter().any(|p| raw.starts_with(p));
        if starts(&[b"NHP|", b"NCLAIM|", b"NACK|"]) {
            self.receive_npc(raw, address, now);
            return vec![];
        }
        if raw.starts_with(b"IDENT|") {
            self.receive_identity(raw, address, now);
            return vec![];
        }
        if starts(&[b"CARGOREQ|", b"CARGODONE|", b"CINV|"]) {
            return self.receive_cargo(raw, address, now);
        }
        if !starts(&UNIVERSE_PREFIXES) {
            if raw.starts_with(b"CDOCK|") {
                return self.receive_dock(raw, address, now);
            }
            return self.receive_coop_entry(raw, address, now);
        }
        if raw.len() > MAX_PACKET {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        self.universe_update(&fields.f, address, now).unwrap_or_default()
    }

    fn universe_update(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Parsed<Vec<Vec<u8>>> {
        if f.len() < 3 || f[1] != "1" {
            return Err(Reject);
        }
        let (slot, lease) = self.authenticated(f[2], address).ok_or(Reject)?;
        let u = &mut self.universe;
        match (f[0], slot, f.len()) {
            ("UINTEREST", _, 5) if slot != Slot::P1 => {
                let (sequence, sector) = (seq(f[3])?, Atom::parse_macro(f[4])?);
                if !u.views.contains_key(&lease) {
                    u.views.insert(lease, GuestView::default());
                }
                let view = u.views.get_mut(&lease).expect("inserted");
                let same_guest = view.interest.is_some();
                if view.interest.as_ref().is_some_and(|i| sequence <= i.seq) {
                    return Err(Reject);
                }
                let changed = view.interest.as_ref().is_none_or(|i| i.sector != sector);
                view.interest = Some(Interest { lease, seq: sequence, sector });
                if changed {
                    view.delivery = None;
                    view.delivered = None;
                    // On sector return, require a capture newer than the last one sent.
                    view.interest_after_revision = match (&u.receiver.latest, same_guest) {
                        (Some(latest), true) => latest.revision,
                        _ => 0,
                    };
                }
                return Ok(vec![]);
            }
            ("UACK", _, 6) if slot != Slot::P1 => {
                let rev = seq(f[4])?;
                let index = if f[5] == "-1" { -1 } else { i64::from(seq(f[5])?) };
                if let Some(view) = u.views.get_mut(&lease) {
                    if let Some(delivery) = view.delivery.as_mut() {
                        delivery.acknowledge(f[3], rev, index);
                        if delivery.done {
                            view.delivered = Some(delivery.baseline.revision);
                        }
                    }
                }
                return Ok(vec![]);
            }
            _ => {}
        }
        if slot != Slot::P1 || f.len() < 5 || f[3] != lease.as_str() {
            return Err(Reject);
        }
        if u.epoch.is_some_and(|e| e != lease) {
            return Err(Reject); // a fresh host game needs a new relay and fresh guest
        }
        if (f[0] == "UPOS" && f.len() == 13) || (f[0] == "UMOVE" && f.len() == 20) {
            let (entity, sequence, sector) = (seq(f[4])?, seq(f[5])?, Atom::parse_macro(f[6])?);
            let kind = if f[0] == "UPOS" { MotionKind::Upos } else { MotionKind::Umove };
            let mut sample_time = 0.0;
            if kind == MotionKind::Upos {
                pose(&f[7..], POSITION_LIMIT)?;
            } else {
                sample_time = float(f[7])?;
                if !sample_time.is_finite() || !(0.0..=1e12).contains(&sample_time) {
                    return Err(Reject);
                }
                pose(&f[8..14], POSITION_LIMIT)?;
                let mut velocity = [0.0; 6];
                for (i, text) in f[14..20].iter().enumerate() {
                    velocity[i] = float(text)?;
                }
                if !bounded(&velocity, 100000.0, std::f64::consts::TAU) {
                    return Err(Reject);
                }
            }
            if !u.sectors.contains_key(&sector) || !u.entity_ids.get(&sector).is_some_and(|ids| ids.contains(&entity)) {
                return Err(Reject);
            }
            if let Some(old) = u.positions.get(&entity) {
                if sequence <= old.seq {
                    return Err(Reject);
                }
                if kind == MotionKind::Umove
                    && old.kind == MotionKind::Umove
                    && sample_time <= float(&old.fields[4]).expect("validated")
                {
                    return Err(Reject);
                }
            }
            let fields = f[3..].iter().map(|s| (*s).to_owned()).collect();
            u.positions.insert(entity, Position { seq: sequence, at: now, sector, fields, kind });
            return Ok(vec![]);
        }
        let Some((ack, committed)) = u.receiver.receive(f, now)? else { return Ok(vec![]) };
        u.epoch = Some(lease);
        if let Some(baseline) = committed {
            u.npc.committed(&baseline);
            u.sectors.remove(&baseline.sector);
            u.sectors.insert(baseline.sector.clone(), baseline);
            while u.sectors.len() > 8 {
                let first = u.sectors.keys().next().expect("non-empty").clone();
                u.sectors.remove(&first);
            }
            u.entity_ids.clear();
            for (sector, b) in u.sectors.iter() {
                let ids = b.rows.iter().map(|row| record(row).expect("committed rows are valid")).collect();
                u.entity_ids.insert(sector.clone(), ids);
            }
            let ids = &u.entity_ids;
            u.positions.retain(|entity, _| ids.values().any(|set| set.contains(entity)));
            for view in u.views.values_mut() {
                view.forwarded.retain(|entity, _| ids.values().any(|set| set.contains(entity)));
            }
        }
        Ok(vec![format!("UACK|1|{lease}|{}|{}|{ack}", f[3], f[4]).into_bytes()])
    }

    pub(super) fn snapshots_universe(&mut self, now: f64, out: &mut Vec<Packet>) {
        self.snapshots_cargo(now, out);
        self.snapshots_identity(now, out);
        self.snapshots_npc(now, out);
        let Some((host_lease, host_address)) = self.core.peers.get(&Slot::P1).map(|p| (p.lease, p.address)) else {
            return;
        };
        let guests: Vec<(Hex32, SocketAddr)> = self
            .core
            .peers
            .iter()
            .filter(|(slot, _)| **slot != Slot::P1)
            .map(|(_, peer)| (peer.lease, peer.address))
            .collect();
        let retained = self.retained_leases();
        let u = &mut self.universe;
        u.views.retain(|lease, _| retained.contains(lease));
        for (guest_lease, guest_address) in guests {
            let Some(view) = u.views.get_mut(&guest_lease) else { continue };
            let Some(interest) = view.interest.as_ref() else { continue };
            let sector = interest.sector.clone();
            let claimant = if self.core.dedicated { format!("|{guest_lease}") } else { String::new() };
            out.push((
                host_address,
                format!("UINTEREST|1|{host_lease}|{}|{sector}{claimant}", interest.seq).into_bytes(),
            ));
            if let Some(latest) = u.sectors.get(&sector) {
                if latest.revision > view.interest_after_revision
                    && view.delivery.as_ref().is_none_or(|d| d.done)
                    && Some(latest.revision) != view.delivered
                {
                    view.delivery = Some(Sender::new(latest.clone()));
                }
            }
            if let Some(delivery) = view.delivery.as_mut() {
                out.extend(delivery.packets(guest_lease, now).into_iter().map(|p| (guest_address, p)));
            }
            // Independent, lossy motion cannot create entities or commit a baseline.
            // Samples not yet forwarded go first (oldest arrival first); up to REPEAT_BUDGET
            // latest samples are then repeated round robin to cover datagram loss (universe.py).
            let live: Vec<(u32, &Position)> = u
                .positions
                .iter()
                .filter(|(_, p)| p.sector == sector && now - p.at < 0.75)
                .map(|(k, p)| (*k, p))
                .collect();
            if !live.is_empty() {
                let mut fresh: Vec<(u32, &Position)> =
                    live.iter().copied().filter(|(k, p)| view.forwarded.get(k) != Some(&p.seq)).collect();
                // Stable, like Python's sorted(): equal arrival times keep insertion order.
                fresh.sort_by(|a, b| a.1.at.partial_cmp(&b.1.at).unwrap_or(std::cmp::Ordering::Equal));
                let mut chosen: Vec<(u32, &Position)> = fresh.into_iter().take(MOTION_BUDGET).collect();
                if chosen.len() < MOTION_BUDGET {
                    let rest: Vec<(u32, &Position)> =
                        live.iter().copied().filter(|(k, _)| !chosen.iter().any(|(c, _)| c == k)).collect();
                    if !rest.is_empty() {
                        let take = REPEAT_BUDGET.min(MOTION_BUDGET - chosen.len()).min(rest.len());
                        for i in 0..take {
                            chosen.push(rest[(view.position_cursor + i) % rest.len()]);
                        }
                        view.position_cursor = (view.position_cursor + take) % rest.len();
                    }
                }
                for (entity, entry) in chosen {
                    view.forwarded.insert(entity, entry.seq);
                    let kind = if entry.kind == MotionKind::Upos { "UPOS" } else { "UMOVE" };
                    out.push((
                        guest_address,
                        format!("{kind}|1|{guest_lease}|{}", entry.fields.join("|")).into_bytes(),
                    ));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ship(id: u32) -> String {
        format!("{id},ship,ship_arg_s_fighter_01_a_macro,argon,1,2,3,0,0,0,none,0")
    }

    #[test]
    fn records_enforce_kind_invariants() {
        assert_eq!(record(&ship(7)), Ok(7));
        assert!(record("7,drop,crate_macro,argon,1,2,3,0,0,0,energycells,5").is_ok());
        assert!(record("7,drop,crate_macro,argon,1,2,3,0,0,0,none,5").is_err());
        assert!(record("7,ship,ship_macro,argon,1,2,3,0,0,0,none,1").is_err());
        assert!(record("0,ship,ship_macro,argon,1,2,3,0,0,0,none,0").is_err());
        assert!(record("7,ship,ship_macro,argon,1,2,3,0,0,7,none,0").is_err());
        assert!(record(&ship(7).replace(",1,", ",\x1c1,")).is_err());
    }

    #[test]
    fn baseline_transfer_commits_atomically() {
        let epoch = Hex32::parse(&"e".repeat(32)).unwrap();
        let rows = vec![ship(1), ship(5)];
        let baseline = Baseline::create(epoch, 3, Atom::parse("cluster_macro").unwrap(), rows.clone()).unwrap();
        assert!(Baseline::create(epoch, 3, Atom::parse("cluster_macro").unwrap(), vec![ship(5), ship(1)]).is_err());
        let mut sender = Sender::new(baseline.clone());
        let mut receiver = Receiver::default();
        let lease = Hex32::parse(&"a".repeat(32)).unwrap();
        let mut now = 0.0;
        while !sender.done {
            for packet in sender.packets(lease, now) {
                let text = String::from_utf8(packet).unwrap();
                let f: Vec<&str> = text.split('|').collect();
                if let Some((ack, committed)) = receiver.receive(&f, now).unwrap() {
                    if committed.is_some() {
                        assert_eq!(receiver.latest.as_ref(), Some(&baseline));
                    }
                    sender.acknowledge(f[3], 3, ack);
                }
            }
            now += 0.3;
            assert!(now < 10.0);
        }
        assert_eq!(receiver.latest.unwrap().rows, rows);
    }
}
