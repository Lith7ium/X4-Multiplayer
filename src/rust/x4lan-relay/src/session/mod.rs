//! Session authority: the Rust counterpart of host.py's `Session` and the layered
//! session classes built on it (bubble -> economy -> coop -> dock -> universe).
//!
//! Python models the layers as a subclass chain whose `receive`/`snapshots` call
//! `super()` and then post-process each other's packets as strings. Here one
//! [`Session`] owns one state struct per layer and the selected [`Level`] decides
//! which layers participate; a layer that rewrites another layer's packets in Python
//! (coop turning `ORDER_EXEC` into `COOP_EXEC`) instead emits the right variant at the
//! source. Output order, including Python dict iteration order, is preserved.

pub mod bubble;
pub mod cargo;
pub mod coop;
pub mod dock;
pub mod economy;
pub mod identity;
pub mod missions;
pub mod npc;
pub mod projectile;
pub mod quests;
pub mod universe;

use std::collections::VecDeque;
use std::net::SocketAddr;

use crate::ordmap::OrderedMap;
use crate::pynum::fmt_g;
use crate::wire::{float, seq, Atom, Hex32, Ship, Slot, MAX_PACKET, MAX_SLOTS};

pub const LEASE_SECONDS: f64 = 30.0;
pub const STATE_SECONDS: f64 = 0.75;
pub const TICK_HZ: u32 = 20;
/// Players of a legacy dedicated session (authority p1 plus guests p2/p3).
pub const LEGACY_DEDICATED_PLAYERS: u8 = 3;

/// One outgoing datagram.
pub type Packet = (SocketAddr, Vec<u8>);

/// Which Python session class this relay behaves as.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum Level {
    /// host.py `Session`: transform relay only.
    Base,
    /// bubble.py `BubbleSession`.
    Bubble,
    /// economy.py `EconomySession`.
    Economy,
    /// coop.py `CoopSession`.
    Coop,
    /// docking.py `DockSession`.
    Dock,
    /// universe.py `UniverseSession` (which includes docking.py `DockSession`).
    Universe,
}

/// Source of fresh 128-bit leases (`secrets.token_hex(16)` in Python).
pub enum LeaseSource {
    /// Operating-system CSPRNG; production.
    Os,
    /// `{n:032x}` for n = 1, 2, ...: deterministic, for differential replay only.
    Counter(u128),
}

impl LeaseSource {
    fn next(&mut self) -> Hex32 {
        match self {
            LeaseSource::Os => Hex32::from_bytes(crate::rng::random16()),
            LeaseSource::Counter(n) => {
                *n += 1;
                Hex32::parse(&format!("{:032x}", *n)).expect("hex counter")
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct Peer {
    pub address: SocketAddr,
    pub nonce: Hex32,
    pub lease: Hex32,
    pub last_seen: f64,
    /// Last accepted STATE/IDLE sequence; -1 before the first.
    pub seq: i64,
    pub state_at: f64,
    pub pose: Option<[f64; 6]>,
    pub joined_at: f64,
    pub state_messages: u64,
    /// The nonce a RESUME replaced, until the next accepted state (lost WELCOME retry).
    pub resume_from: Option<Hex32>,
    /// The client sent `XBCAP|1|<lease>|1`: it unpacks XB bundles (see [`bundle`]).
    pub bundle: bool,
}

/// The XB bundle format a client announces with `XBCAP|1|<lease>|<version>`.
pub const BUNDLE_VERSION: &str = "1";

/// host.py `bundle`: pack one tick's snapshot packets into XB datagrams for
/// bundle-capable peers.
///
/// `XB|1|<lease>|<len>:<item><len>:<item>...`: an item is a packet `KIND|1|<lease>|rest`
/// addressed to that peer, carried as `KIND|rest`; the client restores the envelope and
/// validates every message exactly as if sent alone. Datagrams stay within
/// [`MAX_PACKET`], the limit of the native client transport. Packets for other peers,
/// other forms and oversize items pass through unchanged and each destination's packet
/// order is preserved. A bundle of one item is sent as the original packet. Without
/// capable peers the input is returned unchanged.
pub fn bundle(packets: Vec<Packet>, peers: &OrderedMap<Slot, Peer>) -> Vec<Packet> {
    let capable: Vec<(SocketAddr, Hex32)> = peers.values().filter(|p| p.bundle).map(|p| (p.address, p.lease)).collect();
    if capable.is_empty() {
        return packets;
    }
    let lease_of = |address: &SocketAddr| capable.iter().find(|(a, _)| a == address).map(|(_, l)| *l);
    // address -> (lease, [(original packet, encoded item)], encoded size)
    type Open = (Hex32, Vec<(Vec<u8>, Vec<u8>)>, usize);
    let mut out = Vec::with_capacity(packets.len());
    let mut open: OrderedMap<SocketAddr, Open> = OrderedMap::new();
    let flush = |open: &mut OrderedMap<SocketAddr, Open>, out: &mut Vec<Packet>, address: SocketAddr| {
        let Some((lease, mut items, _)) = open.remove(&address) else { return };
        if items.len() == 1 {
            out.push((address, items.pop().expect("one item").0));
            return;
        }
        let mut packet = format!("XB|1|{lease}|").into_bytes();
        for (_, item) in &items {
            packet.extend_from_slice(item);
        }
        out.push((address, packet));
    };
    for (address, packet) in packets {
        let item = lease_of(&address).and_then(|lease| {
            let bar = packet.iter().position(|b| *b == b'|')?;
            let kind = &packet[..bar];
            if kind.is_empty()
                || kind.len() > 32
                || !kind.iter().all(|b| b.is_ascii_uppercase() || b.is_ascii_digit() || *b == b'_')
            {
                return None;
            }
            let envelope = format!("1|{lease}|");
            let rest = packet[bar + 1..].strip_prefix(envelope.as_bytes())?;
            let length = kind.len() + 1 + rest.len();
            let mut item = format!("{length}:").into_bytes();
            item.extend_from_slice(kind);
            item.push(b'|');
            item.extend_from_slice(rest);
            Some((lease, item))
        });
        let Some((lease, item)) = item else {
            flush(&mut open, &mut out, address);
            out.push((address, packet));
            continue;
        };
        let header = "XB|1|".len() + 32 + 1;
        if header + item.len() > MAX_PACKET {
            flush(&mut open, &mut out, address);
            out.push((address, packet));
            continue;
        }
        if open.get(&address).is_some_and(|(_, _, size)| header + size + item.len() > MAX_PACKET) {
            flush(&mut open, &mut out, address);
        }
        if !open.contains_key(&address) {
            open.insert(address, (lease, Vec::new(), 0));
        }
        let entry = open.get_mut(&address).expect("opened");
        entry.2 += item.len();
        entry.1.push((packet, item));
    }
    let remaining: Vec<SocketAddr> = open.keys().copied().collect();
    for address in remaining {
        flush(&mut open, &mut out, address);
    }
    out
}

/// host.py `Session` state.
pub struct Core {
    pub token: Hex32,
    pub build: Atom,
    pub bubble: Atom,
    pub dedicated: bool,
    /// Admitted slots are `p1..=max_players`; 2 outside dedicated mode.
    pub max_players: u8,
    /// Shared co-op ships `1..=ship_count`: 2 outside dedicated mode, otherwise one per
    /// human guest (the authority's observer never consumes one) and at least 2.
    pub ship_count: u8,
    pub sector: Option<Atom>,
    pub peers: OrderedMap<Slot, Peer>,
    pub tick: u64,
    pub accepted_states: u64,
    /// The last 64 expired (slot, nonce) identities; HELLO cannot reuse them.
    pub retired: VecDeque<(Slot, Hex32)>,
    /// The last expired identity per slot and the sector it held.
    pub resumable: OrderedMap<Slot, (Peer, Option<Atom>)>,
}

pub struct Session {
    pub level: Level,
    pub core: Core,
    pub bubble: bubble::Bubble,
    pub economy: economy::Economy,
    pub coop: coop::Coop,
    pub dock: dock::Docks,
    pub universe: universe::Universe,
    leases: LeaseSource,
}

/// A decoded version-1 envelope: ASCII text split on `|`.
pub struct Fields<'a> {
    pub raw: &'a [u8],
    pub f: Vec<&'a str>,
}

impl<'a> Fields<'a> {
    /// `raw.decode('ascii').split('|')`; `None` for non-ASCII bytes.
    pub fn decode(raw: &'a [u8]) -> Option<Self> {
        if !raw.is_ascii() {
            return None;
        }
        let text = std::str::from_utf8(raw).ok()?;
        Some(Fields { raw, f: text.split('|').collect() })
    }

    pub fn len(&self) -> usize {
        self.f.len()
    }

    pub fn is_empty(&self) -> bool {
        self.f.is_empty()
    }

    pub fn kind(&self) -> &'a str {
        self.f[0]
    }

    /// `len(f) >= 2 and f[1] == '1'`.
    pub fn v1(&self) -> bool {
        self.f.len() >= 2 && self.f[1] == "1"
    }

    pub fn get(&self, i: usize) -> Option<&'a str> {
        self.f.get(i).copied()
    }

    pub fn tail(&self, from: usize) -> Vec<String> {
        self.f[from..].iter().map(|s| (*s).to_owned()).collect()
    }
}

impl Session {
    pub fn new(level: Level, token: &str, build: &str, bubble: &str, leases: LeaseSource) -> Result<Self, String> {
        Self::with_mode(level, token, build, bubble, leases, false, 2)
    }

    /// A legacy dedicated session: authority p1 plus guests p2/p3 on ships 2/1.
    pub fn new_dedicated(
        level: Level,
        token: &str,
        build: &str,
        bubble: &str,
        leases: LeaseSource,
    ) -> Result<Self, String> {
        Self::new_dedicated_players(level, token, build, bubble, leases, LEGACY_DEDICATED_PLAYERS)
    }

    /// A dedicated session with `max_players` slots (authority p1 plus `max_players - 1`
    /// human guests, 2..=8) and `max(2, max_players - 1)` shared ships.
    pub fn new_dedicated_players(
        level: Level,
        token: &str,
        build: &str,
        bubble: &str,
        leases: LeaseSource,
        max_players: u8,
    ) -> Result<Self, String> {
        if !(2..=MAX_SLOTS).contains(&max_players) {
            return Err(format!("max_players must be from 2 to {MAX_SLOTS}"));
        }
        Self::with_mode(level, token, build, bubble, leases, true, max_players)
    }

    /// Ships of a session with `max_players` slots.
    pub fn ships_for(dedicated: bool, max_players: u8) -> u8 {
        if dedicated {
            max_players.saturating_sub(1).max(2)
        } else {
            2
        }
    }

    fn with_mode(
        level: Level,
        token: &str,
        build: &str,
        bubble: &str,
        leases: LeaseSource,
        dedicated: bool,
        max_players: u8,
    ) -> Result<Self, String> {
        let token = Hex32::parse(token).map_err(|_| "token must be 32 lowercase hexadecimal characters".to_string())?;
        let (Ok(build), Ok(bubble)) = (Atom::parse(build), Atom::parse(bubble)) else {
            return Err("build/bubble must be simple identifiers".into());
        };
        if level == Level::Universe && !build.as_str().starts_with("world1-") {
            return Err("world synchronization requires a world1- build label".into());
        }
        let ship_count = Self::ships_for(dedicated, max_players);
        Ok(Session {
            level,
            core: Core {
                token,
                build,
                bubble,
                dedicated,
                max_players,
                ship_count,
                sector: None,
                peers: OrderedMap::new(),
                tick: 0,
                accepted_states: 0,
                retired: VecDeque::new(),
                resumable: OrderedMap::new(),
            },
            bubble: bubble::Bubble::default(),
            economy: economy::Economy::default(),
            coop: coop::Coop::new(ship_count),
            dock: dock::Docks::default(),
            universe: universe::Universe::default(),
            leases,
        })
    }

    pub fn peer(&self, slot: Slot) -> Option<&Peer> {
        self.core.peers.get(&slot)
    }

    /// Whether this session admits `slot` (`p1..=max_players`).
    pub fn admits(&self, slot: Slot) -> bool {
        slot.number() <= self.core.max_players
    }

    /// The session's shared ships, `1..=ship_count`.
    pub fn ships(&self) -> impl Iterator<Item = Ship> {
        Ship::all(self.core.ship_count)
    }

    /// A ship field validated against this session's ship count (`integer(f, count)`).
    pub fn ship_field(&self, text: &str) -> crate::wire::Parsed<Option<Ship>> {
        crate::wire::ship_field(text, self.core.ship_count)
    }

    /// Every live peer except `slot`, in slot-insertion order.
    pub fn others(&self, slot: Slot) -> impl Iterator<Item = (&Slot, &Peer)> {
        self.core.peers.iter().filter(move |(s, _)| **s != slot)
    }

    /// bubble.py `_peer`: the slot whose lease and source address both match.
    pub fn authenticated(&self, lease: &str, address: SocketAddr) -> Option<(Slot, Hex32)> {
        self.core
            .peers
            .iter()
            .filter(|(slot, _)| self.admits(**slot))
            .find(|(_, p)| p.address == address && p.lease.matches(lease))
            .map(|(s, p)| (*s, p.lease))
    }

    pub fn live_leases(&self) -> Vec<Hex32> {
        self.core.peers.values().map(|p| p.lease).collect()
    }

    /// Live leases plus those of resumable (expired) identities.
    pub fn retained_leases(&self) -> Vec<Hex32> {
        let mut out = self.live_leases();
        out.extend(self.core.resumable.values().map(|(p, _)| p.lease));
        out
    }

    /// The full `expire` chain of the selected level. Idempotent for a fixed `now`,
    /// which is why Python's repeated calls along `super()` need not be repeated.
    pub fn expire(&mut self, now: f64) {
        let had_host = self.core.peers.contains_key(&Slot::P1);
        self.expire_core(now);
        if self.level >= Level::Bubble {
            self.expire_bubble(had_host);
        }
        if self.level >= Level::Economy {
            self.expire_economy(had_host);
        }
        if self.level >= Level::Coop {
            self.expire_coop(now);
        }
    }

    fn expire_core(&mut self, now: f64) {
        let core = &mut self.core;
        let expired: Vec<Slot> =
            core.peers.iter().filter(|(_, p)| now - p.last_seen >= LEASE_SECONDS).map(|(s, _)| *s).collect();
        for slot in expired {
            let peer = core.peers.remove(&slot).expect("listed");
            core.retired.push_back((slot, peer.nonce));
            while core.retired.len() > 64 {
                core.retired.pop_front();
            }
            core.resumable.insert(slot, (peer, core.sector.clone()));
        }
        if core.peers.is_empty() {
            core.sector = None;
        }
    }

    /// Handle one datagram; returns replies to its source address.
    pub fn receive(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        self.expire(now);
        match self.level {
            Level::Base => self.receive_core(raw, address, now),
            Level::Bubble => self.receive_bubble(raw, address, now),
            Level::Economy => self.receive_economy(raw, address, now),
            Level::Coop => self.receive_coop_entry(raw, address, now),
            Level::Dock if raw.starts_with(b"CDOCK|") => self.receive_dock(raw, address, now),
            Level::Dock => self.receive_coop_entry(raw, address, now),
            Level::Universe => self.receive_universe(raw, address, now),
        }
    }

    /// host.py `Session.receive` (after `expire`).
    pub(crate) fn receive_core(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > MAX_PACKET {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        if !fields.v1() {
            return vec![];
        }
        let f = &fields.f;
        match (f[0], f.len()) {
            ("PING", 4) => self.ping(f).into_iter().collect(),
            ("RESUME", 10) => self.resume(f, address, now).into_iter().collect(),
            ("HELLO", 8) => self.hello(f, address, now).into_iter().collect(),
            ("STATE", 10) | ("IDLE", 4) => {
                self.state(f, address, now);
                vec![]
            }
            ("XBCAP", 4) => {
                // Additive capability: relays without it ignore the message, and only a
                // client that announced it ever receives an XB bundle.
                if f[3] == BUNDLE_VERSION {
                    let slot = self.core.peers.iter().find(|(_, p)| p.address == address && p.lease.matches(f[2]));
                    if let Some(slot) = slot.map(|(s, _)| *s) {
                        self.core.peers.get_mut(&slot).expect("found").bundle = true;
                    }
                }
                vec![]
            }
            _ => vec![],
        }
    }

    fn ping(&self, f: &[&str]) -> Option<Vec<u8>> {
        let nonce = Hex32::parse(f[3]).ok().filter(|_| self.core.token.matches(f[2]))?;
        // Authenticated readiness check: never claims a slot or fixes a sector.
        Some(format!("PONG|1|{nonce}|{}|{}", self.core.build, self.core.bubble).into_bytes())
    }

    fn resume(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Option<Vec<u8>> {
        let core = &mut self.core;
        let (token, build, sector, bubble, slot) = (f[2], f[3], f[4], f[5], f[6]);
        if !core.token.matches(token) || build != core.build.as_str() || bubble != core.bubble.as_str() {
            return None;
        }
        let slot = Slot::parse(slot).ok().filter(|slot| slot.number() <= core.max_players)?;
        let (previous, lease, nonce) = (Hex32::parse(f[7]).ok()?, Hex32::parse(f[8]).ok()?, Hex32::parse(f[9]).ok()?);
        let sector = Atom::parse(sector).ok()?;
        let live = core.peers.get(&slot).is_some();
        let candidate = match core.peers.get(&slot) {
            Some(peer) => peer,
            None => match core.resumable.get(&slot) {
                Some((peer, _)) => peer,
                // A new relay cannot prove this is the same world. Do not silently
                // start a second economy over an existing game.
                None => return Some(format!("RESET|1|{nonce}").into_bytes()),
            },
        };
        let saved_sector_differs = !live && core.resumable.get(&slot).is_some_and(|(_, s)| s.as_ref() != Some(&sector));
        let nonce_ok =
            candidate.nonce == previous || (candidate.nonce == nonce && candidate.resume_from == Some(previous));
        if !candidate.lease.matches(lease.as_str())
            || !nonce_ok
            || core.sector.as_ref().is_some_and(|s| *s != sector)
            || saved_sector_differs
            || core.peers.iter().any(|(s, p)| *s != slot && p.address == address)
        {
            return None;
        }
        let mut peer = match core.peers.get(&slot) {
            Some(peer) => peer.clone(),
            None => core.resumable.get(&slot).expect("candidate").0.clone(),
        };
        peer.resume_from = Some(previous);
        peer.address = address;
        peer.nonce = nonce;
        peer.last_seen = now;
        // Keep sequence, world identity and command deduplication intact.
        peer.pose = None;
        core.peers.insert(slot, peer);
        core.resumable.remove(&slot);
        core.sector = Some(sector);
        Some(format!("WELCOME|1|{nonce}|{lease}|{TICK_HZ}").into_bytes())
    }

    fn hello(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Option<Vec<u8>> {
        let (token, build, sector, bubble, slot, nonce) = (f[2], f[3], f[4], f[5], f[6], f[7]);
        if !self.core.token.matches(token) || build != self.core.build.as_str() || bubble != self.core.bubble.as_str() {
            return None;
        }
        let slot = Slot::parse(slot).ok().filter(|slot| self.admits(*slot))?;
        let nonce = Hex32::parse(nonce).ok()?;
        let sector = Atom::parse(sector).ok()?;
        if self.core.sector.as_ref().is_some_and(|s| *s != sector) {
            return None;
        }
        if let Some(peer) = self.core.peers.get(&slot) {
            if peer.address != address || peer.nonce != nonce {
                return None;
            }
        } else {
            if self.core.retired.contains(&(slot, nonce)) {
                return None;
            }
            // A single endpoint must not own two slots.
            if self.core.peers.values().any(|p| p.address == address) {
                return None;
            }
            let lease = self.leases.next();
            let peer = Peer {
                address,
                nonce,
                lease,
                last_seen: now,
                seq: -1,
                state_at: f64::NEG_INFINITY,
                pose: None,
                joined_at: now,
                state_messages: 0,
                resume_from: None,
                bundle: false,
            };
            self.core.peers.insert(slot, peer);
            self.core.resumable.remove(&slot);
            self.core.sector = Some(sector);
        }
        let peer = self.core.peers.get_mut(&slot).expect("present");
        peer.last_seen = now;
        Some(format!("WELCOME|1|{nonce}|{}|{TICK_HZ}", peer.lease).into_bytes())
    }

    fn state(&mut self, f: &[&str], address: SocketAddr, now: f64) {
        let Some(slot) =
            self.core.peers.iter().find(|(_, p)| p.address == address && p.lease.matches(f[2])).map(|(s, _)| *s)
        else {
            return;
        };
        let Ok(sequence) = seq(f[3]) else { return };
        let peer = self.core.peers.get_mut(&slot).expect("found");
        if i64::from(sequence) <= peer.seq {
            return;
        }
        let pose = if f[0] == "STATE" {
            let mut values = [0.0; 6];
            for (i, text) in f[4..].iter().enumerate() {
                let Ok(v) = float(text) else { return };
                values[i] = v;
            }
            if !values.iter().all(|v| v.is_finite())
                || values[..3].iter().any(|v| v.abs() > 50000.0)
                || values[3..].iter().any(|v| v.abs() > std::f64::consts::TAU)
            {
                return;
            }
            Some(values)
        } else {
            None
        };
        peer.seq = i64::from(sequence);
        peer.pose = pose;
        peer.last_seen = now;
        peer.resume_from = None;
        peer.state_at = now;
        peer.state_messages += 1;
        self.core.accepted_states += u64::from(pose.is_some());
    }

    /// The periodic snapshot packets of the selected level (one 20 Hz tick).
    pub fn snapshots(&mut self, now: f64) -> Vec<Packet> {
        self.expire(now);
        let mut out = self.snapshots_core(now);
        if self.level >= Level::Bubble {
            self.snapshots_bubble(now, &mut out);
        }
        if self.level >= Level::Economy {
            self.snapshots_economy(now, &mut out);
        }
        if self.level >= Level::Coop {
            self.snapshots_coop(now, &mut out);
        }
        if self.level >= Level::Dock {
            self.snapshots_dock(now, &mut out);
        }
        if self.level >= Level::Universe {
            self.snapshots_universe(now, &mut out);
        }
        out
    }

    fn snapshots_core(&mut self, now: f64) -> Vec<Packet> {
        self.core.tick += 1;
        let tick = self.core.tick;
        let mut out = Vec::new();
        for (slot, peer) in self.core.peers.iter() {
            // Every other live peer with a fresh pose, in slot-insertion order. With two
            // slots this is Python's single `other()` lookup.
            let fresh: Vec<(&Peer, [f64; 6])> = self
                .others(*slot)
                .filter_map(|(_, r)| r.pose.filter(|_| now - r.state_at < STATE_SECONDS).map(|pose| (r, pose)))
                .collect();
            if fresh.is_empty() {
                out.push((peer.address, format!("EMPTY|1|{}|{tick}", peer.lease).into_bytes()));
                continue;
            }
            // Co-op never forwards legacy transform frames.
            if self.level >= Level::Coop {
                continue;
            }
            for (remote, pose) in fresh {
                let pose: Vec<String> = pose.iter().map(|x| fmt_g(*x, 9)).collect();
                let packet = format!("PEER|1|{}|{tick}|{}|{}|{}", peer.lease, remote.lease, remote.seq, pose.join("|"));
                out.push((peer.address, packet.into_bytes()));
            }
        }
        out
    }
}
