//! x4lan/players.py `PlayerRoster`: relay side of player factions (client flag player_factions).
//!
//! Every human slot pK owns pool faction x4lan_playerK. The relay only routes and caches; slot p1
//! (the authority: a 7.x client 1, or the dedicated server) authors inter-player relations and
//! sector owners. Wire format and validation follow players.py exactly:
//!
//!   client -> relay  PFID|1|lease|seq|nameHex|icon
//!   relay -> client  PFROSTER|1|lease|slot|nameHex|icon|seq          every connected human, 1 Hz
//!   p1     -> relay  PFREL|1|lease|rev|a|b|value                      authoritative relation (a < b)
//!   relay -> others  PFREL|1|lease|rev|a|b|value                      cached, 1 Hz and on change
//!   other  -> relay  PFREQ|1|lease|seq|target|value                   relation intent
//!   relay -> p1      PFREQ|1|hostLease|from|authoritySeq|target|value
//!   p1     -> relay  PFSEC|1|lease|seq|sectorMacro|owner               cached, every 5 s and on change
//!   other  -> relay  PFSEC|1|lease|seq|sectorMacro|S<own index>       claim intent, to p1
//! A server (dedicated p1) sends no PFID, so it never appears in the roster.

use std::collections::BTreeMap;
use std::net::SocketAddr;

use crate::ordmap::OrderedMap;
use crate::session::{Packet, Peer};
use crate::wire::{Hex32, Slot};

pub const POOL: u32 = 16;
const MAX_SEQ: u32 = 2_147_483_647;
const MAX_SECTORS: usize = 1024;
const ROSTER_SECONDS: f64 = 1.0;
const SECTOR_SECONDS: f64 = 5.0;

fn number(text: &str, limit: u32) -> Option<u32> {
    if text.is_empty() || text.len() > 10 || !text.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let value: u64 = text.parse().ok()?;
    (value <= u64::from(limit)).then_some(value as u32)
}

fn relation(text: &str) -> Option<f64> {
    if text.len() > 12 {
        return None;
    }
    let value: f64 = text.trim().parse().ok()?;
    (value.is_finite() && (-1.0..=1.0).contains(&value)).then_some(value)
}

fn name_hex(text: &str) -> bool {
    !text.is_empty() && text.len() <= 96 && text.len() % 2 == 0 && text.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f'))
}

fn atom(text: &str) -> bool {
    !text.is_empty() && text.len() <= 64 && text.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
}

fn macro_name(text: &str) -> bool {
    text.len() > 6
        && text.len() <= 96
        && text.ends_with("_macro")
        && text.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_')
}

/// `S<1..99>` without a leading zero.
fn sector_token(text: &str) -> bool {
    let Some(digits) = text.strip_prefix('S') else { return false };
    (1..=2).contains(&digits.len()) && !digits.starts_with('0') && digits.bytes().all(|b| b.is_ascii_digit())
}

fn index(slot: Slot) -> u32 {
    u32::from(slot.number())
}

#[derive(Default)]
pub struct PlayerRoster {
    identities: BTreeMap<Slot, (Hex32, u32, String, String)>,
    relations: BTreeMap<(u32, u32), (u32, String)>,
    sectors: BTreeMap<String, (u32, String)>,
    host_lease: Option<Hex32>,
    request_sequences: BTreeMap<(Slot, u32), (Hex32, u32)>,
    authority_request_sequence: u32,
    next_roster: f64,
    next_sectors: f64,
}

impl PlayerRoster {
    pub fn new() -> Self {
        Self::default()
    }

    fn peer<'a>(peers: &'a OrderedMap<Slot, Peer>, address: SocketAddr, lease: &str) -> Option<(Slot, &'a Peer)> {
        peers.iter().find(|(_, p)| p.address == address && p.lease.matches(lease)).map(|(s, p)| (*s, p))
    }

    fn host<'a>(&mut self, peers: &'a OrderedMap<Slot, Peer>) -> Option<&'a Peer> {
        let host = peers.get(&Slot::P1);
        if let Some(lease) = host.map(|h| h.lease) {
            if self.host_lease != Some(lease) {
                // A new host lease is a new authority epoch: forget its predecessor's state.
                self.host_lease = Some(lease);
                self.relations.clear();
                self.sectors.clear();
                self.authority_request_sequence = 0;
            }
        }
        host
    }

    /// Returns (address, packet) pairs to send immediately.
    pub fn handle(&mut self, raw: &[u8], address: SocketAddr, peers: &OrderedMap<Slot, Peer>, _now: f64) -> Vec<Packet> {
        if raw.len() > 768 || !raw.is_ascii() {
            return Vec::new();
        }
        let text = std::str::from_utf8(raw).unwrap_or("");
        let f: Vec<&str> = text.split('|').collect();
        if f.len() < 4 || f[1] != "1" {
            return Vec::new();
        }
        let Some((slot, peer)) = Self::peer(peers, address, f[2]) else { return Vec::new() };
        let own = index(slot);
        let host = self.host(peers);
        self.handle_fields(&f, slot, own, peer, host, peers).unwrap_or_default()
    }

    fn handle_fields(
        &mut self,
        f: &[&str],
        slot: Slot,
        own: u32,
        peer: &Peer,
        host: Option<&Peer>,
        peers: &OrderedMap<Slot, Peer>,
    ) -> Option<Vec<Packet>> {
        let mut out = Vec::new();
        match (f[0], f.len()) {
            ("PFID", 6) => {
                let seq = number(f[3], MAX_SEQ)?;
                if !name_hex(f[4]) || !atom(f[5]) {
                    return None;
                }
                if let Some(old) = self.identities.get(&slot) {
                    if old.0 == peer.lease && seq <= old.1 {
                        return None;
                    }
                }
                self.identities.insert(slot, (peer.lease, seq, f[4].to_string(), f[5].to_string()));
            }
            ("PFREL", 7) if slot == Slot::P1 => {
                let (rev, a, b) = (number(f[3], MAX_SEQ)?, number(f[4], POOL)?, number(f[5], POOL)?);
                let value = relation(f[6])?;
                if !(1 <= a && a < b) {
                    return None;
                }
                if let Some(old) = self.relations.get(&(a, b)) {
                    if rev <= old.0 {
                        return None;
                    }
                }
                let text = format!("{value:.4}");
                self.relations.insert((a, b), (rev, text.clone()));
                for (other, target) in peers.iter() {
                    if *other != Slot::P1 {
                        out.push((target.address, format!("PFREL|1|{}|{rev}|{a}|{b}|{text}", target.lease).into_bytes()));
                    }
                }
            }
            ("PFREQ", 6) if slot != Slot::P1 => {
                let host = host?;
                let (seq, target) = (number(f[3], MAX_SEQ)?, number(f[4], POOL)?);
                let value = relation(f[5])?;
                if target < 1 || target == own {
                    return None;
                }
                if let Some(previous) = self.request_sequences.get(&(slot, target)) {
                    if previous.0 == peer.lease && seq <= previous.1 {
                        return None;
                    }
                }
                if self.authority_request_sequence >= MAX_SEQ {
                    return None;
                }
                // Fence per client lease and target; the relay stamps a monotonic order
                // within the host lease so the second UDP hop cannot reorder intents.
                self.request_sequences.insert((slot, target), (peer.lease, seq));
                self.authority_request_sequence += 1;
                let order = self.authority_request_sequence;
                out.push((host.address, format!("PFREQ|1|{}|{own}|{order}|{target}|{value:.4}", host.lease).into_bytes()));
            }
            ("PFSEC", 6) => {
                let seq = number(f[3], MAX_SEQ)?;
                if !macro_name(f[4]) || !atom(f[5]) {
                    return None;
                }
                if slot == Slot::P1 {
                    if f[5].starts_with('S') && !sector_token(f[5]) {
                        return None;
                    }
                    if let Some(old) = self.sectors.get_mut(f[4]) {
                        if old.1 == f[5] {
                            old.0 = seq;
                            return None;
                        }
                    } else if self.sectors.len() >= MAX_SECTORS {
                        return None;
                    }
                    self.sectors.insert(f[4].to_string(), (seq, f[5].to_string()));
                    for (other, target) in peers.iter() {
                        if *other != Slot::P1 {
                            out.push((target.address, format!("PFSEC|1|{}|{seq}|{}|{}", target.lease, f[4], f[5]).into_bytes()));
                        }
                    }
                } else if let Some(host) = host {
                    // A client may only claim for its own human; the authority decides.
                    if f[5] == format!("S{own}") {
                        out.push((host.address, format!("PFSEC|1|{}|{seq}|{}|{}", host.lease, f[4], f[5]).into_bytes()));
                    }
                }
            }
            _ => {}
        }
        Some(out)
    }

    pub fn snapshots(&mut self, peers: &OrderedMap<Slot, Peer>, now: f64) -> Vec<Packet> {
        let host_present = self.host(peers).is_some();
        let mut out = Vec::new();
        let live: Vec<(Slot, &(Hex32, u32, String, String))> = self
            .identities
            .iter()
            .filter(|(slot, ident)| peers.get(slot).is_some_and(|p| p.lease == ident.0))
            .map(|(s, i)| (*s, i))
            .collect();
        if now >= self.next_roster {
            self.next_roster = now + ROSTER_SECONDS;
            for (slot, peer) in peers.iter() {
                for (other, (_, seq, name, icon)) in &live {
                    out.push((peer.address, format!("PFROSTER|1|{}|{}|{name}|{icon}|{seq}", peer.lease, index(*other)).into_bytes()));
                }
                if *slot != Slot::P1 && host_present {
                    for ((a, b), (rev, value)) in &self.relations {
                        out.push((peer.address, format!("PFREL|1|{}|{rev}|{a}|{b}|{value}", peer.lease).into_bytes()));
                    }
                }
            }
        }
        if now >= self.next_sectors && host_present {
            self.next_sectors = now + SECTOR_SECONDS;
            for (slot, peer) in peers.iter() {
                if *slot != Slot::P1 {
                    for (macro_id, (seq, token)) in &self.sectors {
                        out.push((peer.address, format!("PFSEC|1|{}|{seq}|{macro_id}|{token}", peer.lease).into_bytes()));
                    }
                }
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn peer(port: u16, lease: char) -> Peer {
        let lease = Hex32::parse(&lease.to_string().repeat(32)).unwrap();
        Peer {
            address: SocketAddr::from(([127, 0, 0, 1], port)),
            nonce: lease,
            lease,
            last_seen: 0.0,
            seq: -1,
            state_at: 0.0,
            pose: None,
            joined_at: 0.0,
            state_messages: 0,
            resume_from: None,
            bundle: false,
        }
    }

    fn texts(out: Vec<Packet>) -> Vec<(u16, String)> {
        out.into_iter().map(|(a, p)| (a.port(), String::from_utf8(p).unwrap())).collect()
    }

    #[test]
    fn server_routes_two_clients() {
        // Dedicated layout: p1 = server (no PFID), p2 and p3 = human clients.
        let mut peers = OrderedMap::new();
        peers.insert(Slot::P1, peer(1, 'a'));
        peers.insert(Slot::P2, peer(2, 'b'));
        peers.insert(Slot::P3, peer(3, 'c'));
        let (a, b, c) = ("a".repeat(32), "b".repeat(32), "c".repeat(32));
        let at = |p| SocketAddr::from(([127, 0, 0, 1], p));
        let mut r = PlayerRoster::new();
        assert!(r.handle(format!("PFID|1|{b}|1|416c696365|faction_player").as_bytes(), at(2), &peers, 0.0).is_empty());
        r.handle(format!("PFID|1|{c}|1|426f62|faction_player").as_bytes(), at(3), &peers, 0.0);
        // Wrong address for the lease: ignored.
        r.handle(format!("PFID|1|{b}|2|5a|x").as_bytes(), at(3), &peers, 0.0);
        let rows = texts(r.snapshots(&peers, 1.0));
        assert!(rows.contains(&(1, format!("PFROSTER|1|{a}|2|416c696365|faction_player|1"))));
        assert!(rows.contains(&(3, format!("PFROSTER|1|{c}|2|416c696365|faction_player|1"))));
        assert!(rows.contains(&(2, format!("PFROSTER|1|{b}|3|426f62|faction_player|1"))));
        assert!(!rows.iter().any(|(_, t)| t.contains("|1|") && t.starts_with("PFROSTER") && t.split('|').nth(3) == Some("1")));
        // Server authors 2-3; both clients get it; a client cannot author.
        assert!(r.handle(format!("PFREL|1|{b}|1|2|3|-0.5").as_bytes(), at(2), &peers, 1.0).is_empty());
        let out = texts(r.handle(format!("PFREL|1|{a}|1|2|3|0.75").as_bytes(), at(1), &peers, 1.0));
        assert_eq!(out, vec![(2, format!("PFREL|1|{b}|1|2|3|0.7500")), (3, format!("PFREL|1|{c}|1|2|3|0.7500"))]);
        assert!(r.handle(format!("PFREL|1|{a}|1|2|3|-1").as_bytes(), at(1), &peers, 1.0).is_empty());
        // Client intent to the server, stamped with the sender index and a relay order.
        let out = texts(r.handle(format!("PFREQ|1|{b}|5|3|-0.5").as_bytes(), at(2), &peers, 1.0));
        assert_eq!(out, vec![(1, format!("PFREQ|1|{a}|2|1|3|-0.5000"))]);
        assert!(r.handle(format!("PFREQ|1|{b}|5|3|0").as_bytes(), at(2), &peers, 1.0).is_empty()); // replay
        assert!(r.handle(format!("PFREQ|1|{b}|6|2|0").as_bytes(), at(2), &peers, 1.0).is_empty()); // self
        let out = texts(r.handle(format!("PFREQ|1|{c}|1|2|0.0").as_bytes(), at(3), &peers, 1.0));
        assert_eq!(out, vec![(1, format!("PFREQ|1|{a}|3|2|2|0.0000"))]);
        // Sectors: server rows cached and forwarded; a client claims only for itself.
        let out = texts(r.handle(format!("PFSEC|1|{a}|1|cluster_14_sector001_macro|S2").as_bytes(), at(1), &peers, 1.0));
        assert_eq!(out.len(), 2);
        assert!(r.handle(format!("PFSEC|1|{b}|1|cluster_01_sector001_macro|S3").as_bytes(), at(2), &peers, 1.0).is_empty());
        let out = texts(r.handle(format!("PFSEC|1|{b}|2|cluster_01_sector001_macro|S2").as_bytes(), at(2), &peers, 1.0));
        assert_eq!(out, vec![(1, format!("PFSEC|1|{a}|2|cluster_01_sector001_macro|S2"))]);
        let rows = texts(r.snapshots(&peers, 10.0));
        assert!(rows.contains(&(3, format!("PFREL|1|{c}|1|2|3|0.7500"))));
        assert!(rows.contains(&(2, format!("PFSEC|1|{b}|1|cluster_14_sector001_macro|S2"))));
    }

    #[test]
    fn new_host_lease_forgets_authority_state() {
        let mut peers = OrderedMap::new();
        peers.insert(Slot::P1, peer(1, 'a'));
        peers.insert(Slot::P2, peer(2, 'b'));
        let mut r = PlayerRoster::new();
        r.handle(format!("PFREL|1|{}|1|1|2|0.75", "a".repeat(32)).as_bytes(), SocketAddr::from(([127, 0, 0, 1], 1)), &peers, 0.0);
        assert_eq!(r.relations.len(), 1);
        peers.get_mut(&Slot::P1).unwrap().lease = Hex32::parse(&"f".repeat(32)).unwrap();
        r.snapshots(&peers, 1.0);
        assert!(r.relations.is_empty());
    }

    #[test]
    fn validation_matches_python() {
        assert_eq!(number("0012", 100), Some(12));
        assert_eq!(number("-1", 100), None);
        assert_eq!(number("101", 100), None);
        assert_eq!(relation("-0.5000"), Some(-0.5));
        assert_eq!(relation("2"), None);
        assert!(sector_token("S2") && sector_token("S16") && !sector_token("S0") && !sector_token("S02") && !sector_token("S123"));
        assert!(macro_name("cluster_14_sector001_macro") && !macro_name("Cluster_macro") && !macro_name("_macro"));
        assert!(name_hex("41") && !name_hex("4") && !name_hex("4G"));
    }
}
