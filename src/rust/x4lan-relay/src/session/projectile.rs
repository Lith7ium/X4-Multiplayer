//! projectile_protocol.py + projectile_relay.py: visual-only BOLT/BEND forwarding.
//!
//! The relay forwards a pilot's observed projectile births and ends to the other
//! player and derives no hit, damage or authority from them. Every accepted packet and
//! every forward rechecks the pilot's authority (owner, pilot, epoch, health
//! generation, live ship, fresh same-pilot NAV2). Source times are compared only with
//! the pilot's estimated source clock.

use std::collections::VecDeque;

use super::coop::NavKind;
use super::{Packet, Session};
use crate::ordmap::OrderedMap;
use crate::pynum::fmt_g;
use crate::wire::{Atom, Hex32, Parsed, Reject, Ship, Slot, MAX_PACKET, MAX_SEQ};

const NAV_FRESH: f64 = 0.75;
const LATE: f64 = 1.0;
const AHEAD: f64 = 0.1;
const WINDOW: u32 = 512;
const LIVE: usize = 192;
const RATE: usize = 96;
const REPEATS: u32 = 3;
const GAP: f64 = 0.04;
const PER_TICK: usize = 32;

const MAX_TIME: f64 = 1e12;
const MAX_POSITION: f64 = 50_000_000.0;
const MAX_VELOCITY: f64 = 100_000.0;
const MAX_ANGLE: f64 = 2.0 * std::f64::consts::PI;
const MAX_TTL: f64 = 3.0;

/// Weapon kinds with a known visual (projectile_protocol.py `KINDS`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum WeaponKind {
    TerranPlasma = 1,
    LaserMk1 = 2,
    LaserMk2 = 3,
}

impl WeaponKind {
    fn from_number(n: u32) -> Parsed<Self> {
        match n {
            1 => Ok(Self::TerranPlasma),
            2 => Ok(Self::LaserMk1),
            3 => Ok(Self::LaserMk2),
            _ => Err(Reject),
        }
    }
}

/// The identity both message kinds share.
#[derive(Clone, Debug, PartialEq)]
pub struct Head {
    pub entity: Ship,
    pub epoch: u32,
    pub gen: u32,
    pub id: u32,
    pub time: f64,
}

/// A projectile birth. Numeric bounds are enforced by [`parse`].
#[derive(Clone, Debug, PartialEq)]
pub struct Bolt {
    pub head: Head,
    pub sector: Atom,
    pub kind: WeaponKind,
    /// x, y, z, vx, vy, vz, yaw, pitch, roll.
    pub numbers: [f64; 9],
    pub ttl: f64,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Message {
    Bolt(Bolt),
    End(Head),
}

impl Message {
    fn head(&self) -> &Head {
        match self {
            Message::Bolt(b) => &b.head,
            Message::End(h) => h,
        }
    }
}

/// `[1-9][0-9]{0,9}` within 1..=MAX_SEQ.
fn positive(text: &str) -> Parsed<u32> {
    let b = text.as_bytes();
    if b.is_empty() || b.len() > 10 || b[0] == b'0' || !b.iter().all(u8::is_ascii_digit) {
        return Err(Reject);
    }
    let value: u64 = text.parse().map_err(|_| Reject)?;
    if value > u64::from(MAX_SEQ) {
        return Err(Reject);
    }
    Ok(value as u32)
}

/// `-?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?` then finite and |value| <= limit.
fn number(text: &str, limit: f64) -> Parsed<f64> {
    let b = text.as_bytes();
    let mut i = usize::from(b.first() == Some(&b'-'));
    let digits = |i: &mut usize| {
        let start = *i;
        while *i < b.len() && b[*i].is_ascii_digit() {
            *i += 1;
        }
        *i > start
    };
    if !digits(&mut i) {
        return Err(Reject);
    }
    if b.get(i) == Some(&b'.') {
        i += 1;
        if !digits(&mut i) {
            return Err(Reject);
        }
    }
    if matches!(b.get(i), Some(b'e' | b'E')) {
        i += 1;
        if matches!(b.get(i), Some(b'+' | b'-')) {
            i += 1;
        }
        if !digits(&mut i) {
            return Err(Reject);
        }
    }
    if i != b.len() {
        return Err(Reject);
    }
    let value: f64 = text.parse().map_err(|_| Reject)?;
    if !value.is_finite() || value.abs() > limit {
        return Err(Reject);
    }
    Ok(value)
}

/// Decode a client-direction BOLT/BEND (`parse(raw, 'client')`): the sender lease and
/// the message.
pub fn parse(raw: &[u8]) -> Parsed<(Hex32, Message)> {
    if raw.len() > MAX_PACKET || !raw.iter().all(|c| (0x21..=0x7e).contains(c)) {
        return Err(Reject);
    }
    let text = std::str::from_utf8(raw).map_err(|_| Reject)?;
    let f: Vec<&str> = text.split('|').collect();
    const HEAD: usize = 3;
    if f.len() < 2 || f[1] != "1" || !matches!(f[0], "BOLT" | "BEND") {
        return Err(Reject);
    }
    let bolt = f[0] == "BOLT";
    if f.len() != if bolt { HEAD + 17 } else { HEAD + 5 } {
        return Err(Reject);
    }
    let lease = Hex32::parse(f[2])?;
    let entity = positive(f[HEAD])?;
    let head = Head {
        // Projectile visuals are a fixture-ship subsystem (ships 1/2 in every session size).
        entity: Ship::from_number(i128::from(entity)).filter(|s| s.is_fixture()).ok_or(Reject)?,
        epoch: positive(f[HEAD + 1])?,
        gen: positive(f[HEAD + 2])?,
        id: positive(f[HEAD + 3])?,
        time: number(f[HEAD + 4], MAX_TIME)?,
    };
    if head.time < 0.0 {
        return Err(Reject);
    }
    if !bolt {
        return Ok((lease, Message::End(head)));
    }
    let limits = [
        MAX_POSITION,
        MAX_POSITION,
        MAX_POSITION,
        MAX_VELOCITY,
        MAX_VELOCITY,
        MAX_VELOCITY,
        MAX_ANGLE,
        MAX_ANGLE,
        MAX_ANGLE,
    ];
    let mut numbers = [0.0; 9];
    for (i, limit) in limits.iter().enumerate() {
        numbers[i] = number(f[HEAD + 7 + i], *limit)?;
    }
    let message = Bolt {
        head,
        sector: Atom::parse(f[HEAD + 5])?,
        kind: WeaponKind::from_number(positive(f[HEAD + 6])?)?,
        numbers,
        ttl: number(f[HEAD + 16], MAX_TTL)?,
    };
    if message.ttl <= 0.0 {
        return Err(Reject);
    }
    Ok((lease, Message::Bolt(message)))
}

/// Encode the forwarded form: recipient lease, then the source lease.
pub fn serialize_forwarded(message: &Message, lease: Hex32, source: Hex32) -> Vec<u8> {
    let h = message.head();
    let mut f = vec![
        if matches!(message, Message::Bolt(_)) { "BOLT" } else { "BEND" }.to_string(),
        "1".into(),
        lease.to_string(),
        source.to_string(),
        h.entity.to_string(),
        h.epoch.to_string(),
        h.gen.to_string(),
        h.id.to_string(),
        fmt_g(h.time, 17),
    ];
    if let Message::Bolt(b) = message {
        f.push(b.sector.to_string());
        f.push((b.kind as u32).to_string());
        f.extend(b.numbers.iter().map(|v| fmt_g(*v, 17)));
        f.push(fmt_g(b.ttl, 17));
    }
    // Bounded inputs keep a forward under ~480 bytes; it can never exceed MAX_PACKET.
    f.join("|").into_bytes()
}

struct Record {
    live: bool,
    bolt: Option<Bolt>,
    end: Option<Head>,
    /// Forwarding state per recipient lease. With two players there is one recipient and
    /// this is projectile_relay.py's single sent/end_sent/at; with more players every
    /// other client keeps its own repeats and gap (one shared counter starved all but the
    /// first recipient, which was the server).
    delivery: OrderedMap<Hex32, Delivery>,
}

#[derive(Clone, Copy, Debug)]
struct Delivery {
    sent: u32,
    end_sent: u32,
    at: f64,
}

impl Delivery {
    const NEW: Delivery = Delivery { sent: 0, end_sent: 0, at: -1e9 };
}

impl Record {
    fn to(&self, peer: &Hex32) -> Delivery {
        self.delivery.get(peer).copied().unwrap_or(Delivery::NEW)
    }

    /// Every recipient that received this record has had all end repeats.
    fn ends_delivered(&self) -> bool {
        !self.delivery.is_empty() && self.delivery.values().all(|d| d.end_sent >= REPEATS)
    }
}

/// One pilot's records under one (lease, epoch, gen) key.
struct Stream {
    key: (Hex32, u32, u64),
    latest: u32,
    seen: OrderedMap<u32, Record>,
    births: VecDeque<f64>,
}

impl Stream {
    fn new(key: (Hex32, u32, u64)) -> Self {
        Stream { key, latest: 0, seen: OrderedMap::new(), births: VecDeque::new() }
    }

    fn admit(&self, id: u32) -> bool {
        i64::from(id) > i64::from(self.latest) - i64::from(WINDOW) && (self.latest == 0 || id <= self.latest + WINDOW)
    }

    fn mark(&mut self, id: u32, record: Record) {
        self.seen.insert(id, record);
        if id > self.latest {
            self.latest = id;
            let floor = i64::from(id) - i64::from(WINDOW);
            self.seen.retain(|old, _| i64::from(*old) > floor);
        }
    }

    fn live_count(&self) -> usize {
        self.seen.values().filter(|r| r.live).count()
    }

    /// A record retires at birth time + ttl, on a sector change, or after its end's repeats.
    fn retire(&mut self, source: f64, sector: &str) {
        for record in self.seen.values_mut().filter(|r| r.live) {
            let bolt = record.bolt.as_ref().expect("live records are births");
            if source >= bolt.head.time + bolt.ttl
                || bolt.sector.as_str() != sector
                || (record.end.is_some() && record.ends_delivered())
            {
                record.live = false;
            }
        }
    }
}

#[derive(Default)]
pub struct ProjectileRelay {
    streams: OrderedMap<Ship, Stream>,
}

/// (epoch, gen, sector, estimated pilot source time) while the slot may publish visuals.
struct Authority {
    epoch: u64,
    gen: u64,
    sector: String,
    source: f64,
}

impl Session {
    fn projectile_authority(&mut self, ship: Ship, slot: Slot, lease: Hex32, now: f64) -> Option<Authority> {
        if self.owner(ship) != slot || self.pilot(ship) != Some(slot) {
            return None;
        }
        if !self.has_world() || self.dead(ship) {
            return None;
        }
        let gen = self.health_generation(ship);
        if slot != Slot::P1 && self.coop.health_live[ship.index()] != gen {
            return None;
        }
        let nav = self.coop.navigation.get(&ship)?;
        if nav.kind != NavKind::Nav2 || nav.lease != lease || now - nav.at > NAV_FRESH {
            return None;
        }
        Some(Authority {
            epoch: self.epoch(ship),
            gen,
            sector: nav.sector().to_owned(),
            source: nav.source_time() + (now - nav.at),
        })
    }

    pub(super) fn receive_projectile(&mut self, slot: Slot, lease: Hex32, raw: &[u8], now: f64) {
        let Ok((sender, message)) = parse(raw) else { return };
        if sender != lease {
            return;
        }
        let head = message.head().clone();
        let Some(auth) = self.projectile_authority(head.entity, slot, lease, now) else { return };
        if (u64::from(head.epoch), u64::from(head.gen)) != (auth.epoch, auth.gen) {
            return;
        }
        let key = (lease, head.epoch, u64::from(head.gen));
        let streams = &mut self.coop.projectiles.streams;
        if streams.get(&head.entity).is_none_or(|s| s.key != key) {
            streams.insert(head.entity, Stream::new(key));
        }
        let stream = streams.get_mut(&head.entity).expect("present");
        stream.retire(auth.source, &auth.sector);
        let in_window = auth.source - LATE <= head.time && head.time <= auth.source + AHEAD;
        if !in_window || !stream.admit(head.id) {
            return;
        }
        let old = stream.seen.get(&head.id);
        match message {
            Message::Bolt(bolt) => {
                if old.is_some() || bolt.sector.as_str() != auth.sector || bolt.head.time + bolt.ttl <= auth.source {
                    return;
                }
                while stream.births.front().is_some_and(|t| now - t >= 1.0) {
                    stream.births.pop_front();
                }
                if stream.births.len() >= RATE || stream.live_count() >= LIVE {
                    return;
                }
                stream.births.push_back(now);
                let record = Record { live: true, bolt: Some(bolt), end: None, delivery: OrderedMap::new() };
                stream.mark(head.id, record);
            }
            Message::End(end) => match old {
                // End before birth: a bounded tombstone refuses the late birth.
                None => {
                    let record = Record { live: false, bolt: None, end: Some(end), delivery: OrderedMap::new() };
                    stream.mark(head.id, record);
                }
                Some(record) => {
                    let first_end = record.live
                        && record.end.is_none()
                        && record.bolt.as_ref().is_some_and(|b| end.time >= b.head.time);
                    if first_end {
                        stream.seen.get_mut(&head.id).expect("present").end = Some(end);
                        // first end wins
                    }
                }
            },
        }
    }

    pub(super) fn snapshots_projectiles(&mut self, now: f64, out: &mut Vec<Packet>) {
        let entities: Vec<Ship> = self.coop.projectiles.streams.keys().copied().collect();
        for ship in entities {
            let (lease, epoch, gen) = self.coop.projectiles.streams.get(&ship).expect("present").key;
            let slot = self.core.peers.iter().find(|(_, p)| p.lease == lease).map(|(s, _)| *s);
            let auth = slot.and_then(|slot| self.projectile_authority(ship, slot, lease, now));
            let stream = self.coop.projectiles.streams.get_mut(&ship).expect("present");
            let auth = match auth {
                Some(a) if (a.epoch, a.gen) == (u64::from(epoch), gen) => a,
                _ => {
                    // Retire everything but keep the ids, so a late repeat under this
                    // key can never resurrect a record.
                    for record in stream.seen.values_mut() {
                        record.live = false;
                    }
                    continue;
                }
            };
            stream.retire(auth.source, &auth.sector);
            let slot = slot.expect("authority implies a slot");
            for (other, peer) in self.core.peers.iter() {
                if *other == slot {
                    continue;
                }
                let mut budget = PER_TICK;
                // Unsent births first, so repeats never delay a new projectile's first forward.
                let mut order: Vec<u32> = stream.seen.iter().filter(|(_, r)| r.live).map(|(id, _)| *id).collect();
                order.sort_by_key(|id| stream.seen.get(id).expect("listed").to(&peer.lease).sent > 0);
                for id in order {
                    if budget == 0 {
                        break;
                    }
                    let record = stream.seen.get_mut(&id).expect("listed");
                    let mut d = record.to(&peer.lease);
                    if now - d.at < GAP {
                        continue;
                    }
                    let message = if d.sent == 0 || (record.end.is_none() && d.sent < REPEATS) {
                        d.sent += 1;
                        Message::Bolt(record.bolt.clone().expect("live birth"))
                    } else if record.end.is_some() && d.end_sent < REPEATS {
                        d.end_sent += 1;
                        Message::End(record.end.clone().expect("checked"))
                    } else {
                        continue;
                    };
                    d.at = now;
                    record.delivery.insert(peer.lease, d);
                    budget -= 1;
                    out.push((peer.address, serialize_forwarded(&message, peer.lease, lease)));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lease() -> String {
        "c".repeat(32)
    }

    #[test]
    fn strict_client_codec() {
        let bend = format!("BEND|1|{}|1|2|3|4|5.5", lease());
        let (sender, message) = parse(bend.as_bytes()).unwrap();
        assert_eq!(sender.as_str(), lease());
        assert!(matches!(message, Message::End(Head { entity: Ship::ONE, id: 4, .. })));
        for bad in [
            format!("BEND|1|{}|3|2|3|4|5.5", lease()),  // entity
            format!("BEND|1|{}|1|02|3|4|5.5", lease()), // leading zero
            format!("BEND|1|{}|1|2|3|4|-5", lease()),   // negative time
            format!("BEND|1|{}|1|2|3|4|5.", lease()),   // number grammar
            format!("BEND|1|{}|1|2|3|4| 5", lease()),   // space is not printable
            format!("BEND|1|{}|1|2|3|4|1e13", lease()), // bound
        ] {
            assert!(parse(bad.as_bytes()).is_err(), "{bad}");
        }
        let bolt = format!("BOLT|1|{}|2|1|1|9|10|sector_macro|2|1|2|3|4|5|6|0.1|0.2|0.3|1.5", lease());
        let (_, message) = parse(bolt.as_bytes()).unwrap();
        let forwarded = String::from_utf8(serialize_forwarded(
            &message,
            Hex32::parse(&"d".repeat(32)).unwrap(),
            Hex32::parse(&lease()).unwrap(),
        ))
        .unwrap();
        assert!(forwarded.starts_with(&format!(
            "BOLT|1|{}|{}|2|1|1|9|10|sector_macro|2|1|2|3|4|5|6|0.10000000000000001|",
            "d".repeat(32),
            lease()
        )));
        assert!(parse(bolt.replace("|1.5", "|0").as_bytes()).is_err()); // ttl must be > 0
    }
}
