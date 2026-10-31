//! bubble.py: bounded host-authoritative action routing for the test bubble.
//!
//! The relay never invents cargo or damage. Only p1's X4 may publish world state and
//! finish commands. Disconnecting the host cancels commands, never retries an
//! ambiguous transaction in a new world.

use std::f64::consts::TAU;
use std::net::{IpAddr, SocketAddr};

use super::{Fields, Level, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::pynum::fmt_g;
use crate::wire::{bounded, float, seq, uint, Atom, Hex32, Parsed, Reject, Ship, Slot};

pub const WORLD_FIELDS: usize = 17;
/// bubble.py `WALK2_FIELDS`: `WALK2|1|lease|seq|state|kind|ship|sector|code|macro|room|
/// x|y|z|yaw|speed|stime|transfers` (walk_game.lua), an on-foot pose relative to a
/// context (shared ship index or station identity) every client resolves identically.
pub const WALK2_FIELDS: usize = 18;
/// A WALK2 pose is forwarded while younger than this (seconds).
pub const WALK2_FRESH: f64 = 1.0;
/// An unchanged WALK2 pose is repeated to a receiver at most this often (seconds).
pub const WALK2_REPEAT: f64 = 0.2;
/// Seconds between forwarded `XSHIP` rows per ship and peer (5 Hz; health is slow).
pub const XSHIP_INTERVAL: f64 = 0.2;

/// Host-authoritative state of a co-op ship beyond the two WORLD rows:
/// `XSHIP|1|lease|revision|ship|hull|shield|cargo|dock`. `fields` keep the validated
/// sender text from the revision on.
#[derive(Clone, Debug)]
pub struct ExtraShip {
    pub revision: u32,
    pub hull: f64,
    pub fields: Vec<String>,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Verb {
    Pickup,
    Trade,
}

impl Verb {
    fn parse(text: &str) -> Option<Self> {
        match text {
            "PICKUP" => Some(Verb::Pickup),
            "TRADE" => Some(Verb::Trade),
            _ => None,
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Verb::Pickup => "PICKUP",
            Verb::Trade => "TRADE",
        }
    }
}

/// A command's lifecycle: `Pending` until the host reports one terminal result.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CommandResult {
    Pending,
    Ok,
    Rejected,
    Busy,
    Cancelled,
}

impl CommandResult {
    /// The host's `DONE` results (`RESULTS` in bubble.py).
    fn parse_done(text: &str) -> Option<Self> {
        match text {
            "OK" => Some(Self::Ok),
            "REJECTED" => Some(Self::Rejected),
            "BUSY" => Some(Self::Busy),
            "CANCELLED" => Some(Self::Cancelled),
            _ => None,
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "PENDING",
            Self::Ok => "OK",
            Self::Rejected => "REJECTED",
            Self::Busy => "BUSY",
            Self::Cancelled => "CANCELLED",
        }
    }
}

#[derive(Clone, Debug)]
pub struct Command {
    pub slot: Slot,
    pub lease: Hex32,
    pub ip: IpAddr,
    pub verb: Verb,
    pub result: CommandResult,
    pub created: f64,
    /// Co-op: the ship (0 = none) bound when the command was accepted.
    pub ship: Option<u32>,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BodyMode {
    Ship,
    Foot,
    Suit,
}

impl BodyMode {
    pub fn parse(text: &str) -> Parsed<Self> {
        match text {
            "ship" => Ok(Self::Ship),
            "foot" => Ok(Self::Foot),
            "suit" => Ok(Self::Suit),
            _ => Err(Reject),
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Ship => "ship",
            Self::Foot => "foot",
            Self::Suit => "suit",
        }
    }
}

/// A player's body report; the pose keeps the sender's validated text.
#[derive(Clone, Debug)]
pub struct Body {
    pub seq: u32,
    pub mode: BodyMode,
    pub dead: u32,
    pub generation: u32,
    pub pose: Vec<String>,
}

impl Body {
    /// `seq|mode|dead|gen` as Python's `map(str, body[:4])`.
    pub fn head(&self) -> String {
        format!("{}|{}|{}|{}", self.seq, self.mode.as_str(), self.dead, self.generation)
    }

    /// Ordering/death fence shared by BODY and BODY2.
    pub fn superseded_by(&self, seq: u32, dead: u32, generation: u32) -> bool {
        seq > self.seq && dead >= self.dead && generation >= self.generation
    }
}

#[derive(Clone, Debug)]
pub struct Walker {
    seq: u32,
    at: f64,
    context: u32,
    pose: Vec<String>,
}

/// The latest WALK2 of a lease: `fields` keep the validated sender text from `seq` on.
#[derive(Clone, Debug)]
pub struct Walker2 {
    seq: u32,
    at: f64,
    fields: Vec<String>,
}

/// bubble.py `walk2`: validate WALK2 fields 3.. against the session's ship count.
fn walk2(f: &[&str], ship_count: u8) -> Parsed<u32> {
    let (sequence, state, ship, _transfers) = (seq(f[3])?, seq(f[4])?, seq(f[6])?, seq(f[17])?);
    let kind = f[5];
    if state > 1 || !matches!(kind, "none" | "ship" | "station") || ship > u32::from(ship_count) {
        return Err(Reject);
    }
    for text in &f[7..11] {
        Atom::parse(text)?;
    }
    if (state == 1) == (kind == "none") || (kind == "ship") != (ship > 0) {
        return Err(Reject);
    }
    if kind == "station" && f[7..10].contains(&"-") {
        return Err(Reject);
    }
    let mut v = [0.0; 6];
    for (i, text) in f[11..17].iter().enumerate() {
        v[i] = float(text)?;
    }
    let [x, y, z, yaw, speed, stime] = v;
    if !v.iter().all(|n| n.is_finite())
        || x.abs().max(y.abs()).max(z.abs()) > 100_000.0
        || yaw.abs() > 360.0
        || !(0.0..=100.0).contains(&speed)
        || !(0.0..=1e9).contains(&stime)
    {
        return Err(Reject);
    }
    Ok(sequence)
}

#[derive(Default)]
pub struct Bubble {
    /// The host's 17-value world snapshot.
    pub world: Option<[f64; WORLD_FIELDS]>,
    pub world_owner: Option<Hex32>,
    pub world_revision: i64,
    pub commands: OrderedMap<Hex32, Command>,
    pub fire: OrderedMap<Hex32, (u32, f64)>,
    pub bodies: OrderedMap<Hex32, Body>,
    pub suit_kills: OrderedMap<Hex32, (Hex32, u32)>,
    walkers: OrderedMap<Hex32, Walker>,
    walkers2: OrderedMap<Hex32, Walker2>,
    /// Last WALK2 (seq, time) forwarded per (receiver, sender) lease pair.
    walk2_sent: OrderedMap<(Hex32, Hex32), (u32, f64)>,
    /// Ships 3.. (dedicated N-player sessions only); cleared with the host's world.
    pub extra: OrderedMap<Ship, ExtraShip>,
    pub extra_owner: Option<Hex32>,
    pub next_extra: f64,
}

impl Session {
    pub(super) fn expire_bubble(&mut self, had_host: bool) {
        let b = &mut self.bubble;
        if had_host && !self.core.peers.contains_key(&Slot::P1) {
            b.world = None;
            b.world_owner = None;
            b.world_revision = -1;
            b.extra.clear();
            b.extra_owner = None;
            for cmd in b.commands.values_mut() {
                if cmd.result == CommandResult::Pending {
                    cmd.result = CommandResult::Cancelled;
                }
            }
        }
        let live: Vec<Hex32> = self.core.peers.values().map(|p| p.lease).collect();
        let mut retained = live.clone();
        retained.extend(self.core.resumable.values().map(|(p, _)| p.lease));
        b.bodies.retain(|lease, _| retained.contains(lease));
        b.suit_kills.retain(|lease, _| retained.contains(lease));
        b.fire.retain(|lease, _| live.contains(lease));
        b.walkers.retain(|lease, _| live.contains(lease));
        b.walkers2.retain(|lease, _| live.contains(lease));
        b.walk2_sent.retain(|(to, from), _| live.contains(to) && live.contains(from));
        for cmd in b.commands.values_mut() {
            if !live.contains(&cmd.lease) && cmd.result == CommandResult::Pending {
                cmd.result = CommandResult::Cancelled;
            }
        }
    }

    /// The world snapshot exists (Python: `self.world` is a non-empty tuple).
    pub fn has_world(&self) -> bool {
        self.bubble.world.is_some()
    }

    pub(crate) fn receive_bubble(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > 768 {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        if !fields.v1() {
            return vec![];
        }
        let f = &fields.f;
        if f[0] == "ACTION" && f.len() == 7 {
            return self.action(f, address, now).into_iter().collect();
        }
        if !matches!(f[0], "WORLD" | "FIRE" | "DONE" | "BODY" | "SUITKILL" | "WALK" | "WALK2" | "XSHIP") {
            return self.receive_core(raw, address, now);
        }
        let _ = self.bubble_update(&fields, address, now);
        vec![]
    }

    fn action(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Option<Vec<u8>> {
        let (token, slot, request, verb, target) = (f[2], f[3], f[4], f[5], f[6]);
        if !self.core.token.matches(token) {
            return None;
        }
        let request = Hex32::parse(request).ok()?;
        let slot = Slot::parse(slot).ok().filter(|slot| self.admits(*slot))?;
        let verb = Verb::parse(verb)?;
        if target != "energycells" {
            return None;
        }
        let reply = |status: &str| Some(format!("RESULT|1|{request}|{status}").into_bytes());
        if let Some(old) = self.bubble.commands.get(&request) {
            if (old.slot, old.verb, old.ip) != (slot, verb, address.ip()) {
                return None;
            }
            return reply(old.result.as_str());
        }
        let peer = match self.core.peers.get(&slot) {
            Some(p) if p.address.ip() == address.ip() => p,
            _ => return reply("NO_PLAYER"),
        };
        if !self.has_world() || !self.core.peers.contains_key(&Slot::P1) {
            return reply("NO_WORLD");
        }
        let commands = &mut self.bubble.commands;
        if commands.values().filter(|c| c.result == CommandResult::Pending).count() >= 8 {
            return reply("BUSY");
        }
        if commands.len() >= 512 {
            let (_, oldest) = commands.first().expect("non-empty");
            // Never forget an ID in this host world: reject further work at the bound.
            return reply(if oldest.result == CommandResult::Pending { "BUSY" } else { "SESSION_FULL" });
        }
        let command = Command {
            slot,
            lease: peer.lease,
            ip: address.ip(),
            verb,
            result: CommandResult::Pending,
            created: now,
            ship: None,
        };
        commands.insert(request, command);
        reply("PENDING")
    }

    fn bubble_update(&mut self, fields: &Fields, address: SocketAddr, now: f64) -> Parsed<()> {
        let f = &fields.f;
        let (slot, lease) = self.authenticated(fields.get(2).ok_or(Reject)?, address).ok_or(Reject)?;
        let ship_count = self.core.ship_count;
        let b = &mut self.bubble;
        match (f[0], f.len()) {
            ("WALK2", WALK2_FIELDS) => {
                let sequence = walk2(f, ship_count)?;
                if b.walkers2.get(&lease).is_some_and(|old| sequence <= old.seq) {
                    return Err(Reject);
                }
                b.walkers2.insert(lease, Walker2 { seq: sequence, at: now, fields: fields.tail(3) });
            }
            ("WALK", 11) => {
                let sequence = seq(f[3])?;
                let context = seq(f[4])?;
                let mut values = Vec::with_capacity(6);
                for text in &f[5..] {
                    values.push(float(text)?);
                }
                if context > 4 || !bounded(&values, 10000.0, 360.0) {
                    return Err(Reject);
                }
                if b.walkers.get(&lease).is_some_and(|old| sequence <= old.seq) {
                    return Err(Reject);
                }
                b.walkers.insert(lease, Walker { seq: sequence, at: now, context, pose: fields.tail(5) });
            }
            ("BODY", 13) => {
                let (sequence, mode, dead, generation) = (seq(f[3])?, f[4], seq(f[5])?, seq(f[6])?);
                let mut values = Vec::with_capacity(6);
                for text in &f[7..] {
                    values.push(float(text)?);
                }
                let mode = BodyMode::parse(mode)?;
                if dead > 1 || (mode == BodyMode::Suit && generation == 0) || !bounded(&values, 50000.0, TAU) {
                    return Err(Reject);
                }
                if b.bodies.get(&lease).is_some_and(|old| !old.superseded_by(sequence, dead, generation)) {
                    return Err(Reject);
                }
                b.bodies.insert(lease, Body { seq: sequence, mode, dead, generation, pose: fields.tail(7) });
            }
            ("SUITKILL", 4) if slot == Slot::P1 => {
                // The packet names only a suit generation. Apply it to the one guest whose
                // live suit has it; an ambiguous generation (several guests) is refused.
                let generation = seq(f[3])?;
                let suited: Vec<Hex32> = self
                    .core
                    .peers
                    .iter()
                    .filter(|(s, _)| **s != Slot::P1)
                    .map(|(_, p)| p.lease)
                    .filter(|guest| {
                        b.bodies
                            .get(guest)
                            .is_some_and(|body| body.mode == BodyMode::Suit && body.generation == generation)
                    })
                    .collect();
                if let [guest] = suited[..] {
                    b.suit_kills.insert(guest, (lease, generation));
                }
            }
            ("XSHIP", 9) if slot == Slot::P1 => {
                let (revision, ship) = (seq(f[3])?, uint(f[4], u32::from(ship_count))?);
                let ship = Ship::from_number(i128::from(ship)).filter(|s| !s.is_fixture()).ok_or(Reject)?;
                let (hull, shield) = (float(f[5])?, float(f[6])?);
                let (_cargo, dock) = (uint(f[7], 1_000_000)?, uint(f[8], 1)?);
                let _ = dock;
                if ![hull, shield].iter().all(|v| v.is_finite() && (0.0..=100.0).contains(v)) {
                    return Err(Reject);
                }
                if b.extra_owner != Some(lease) {
                    b.extra.clear();
                    b.extra_owner = Some(lease);
                }
                if b.extra.get(&ship).is_some_and(|old| revision <= old.revision) {
                    return Err(Reject);
                }
                b.extra.insert(ship, ExtraShip { revision, hull, fields: fields.tail(3) });
            }
            ("WORLD", n) if slot == Slot::P1 && n == 4 + WORLD_FIELDS => {
                let revision = seq(f[3])?;
                let mut values = [0.0; WORLD_FIELDS];
                for (i, text) in f[4..].iter().enumerate() {
                    values[i] = float(text)?;
                }
                let valid = values.iter().all(|v| v.is_finite() && (0.0..=1_000_000.0).contains(v))
                    && [0, 1, 3, 4, 6, 7, 8, 9].iter().all(|&i| values[i] <= 100.0)
                    && [11, 12, 13, 14].iter().all(|&i| values[i] == 0.0 || values[i] == 1.0)
                    && [2, 5, 10, 15, 16].iter().all(|&i| values[i] == values[i].trunc());
                if !valid {
                    return Err(Reject);
                }
                if b.world_owner != Some(lease) {
                    b.world_revision = -1;
                    b.world_owner = Some(lease);
                }
                if i64::from(revision) <= b.world_revision {
                    return Err(Reject);
                }
                b.world_revision = i64::from(revision);
                b.world = Some(values);
            }
            ("FIRE", 4) => {
                let sequence = seq(f[3])?;
                let old = b.fire.get(&lease).map_or(-1, |(s, _)| i64::from(*s));
                if i64::from(sequence) > old {
                    b.fire.insert(lease, (sequence, now));
                }
            }
            ("DONE", 5) if slot == Slot::P1 => {
                if let (Some(result), Ok(request)) = (CommandResult::parse_done(f[4]), Hex32::parse(f[3])) {
                    if let Some(cmd) = b.commands.get_mut(&request) {
                        if cmd.result == CommandResult::Pending {
                            cmd.result = result;
                        }
                    }
                }
            }
            _ => {}
        }
        Ok(())
    }

    pub(super) fn snapshots_bubble(&mut self, now: f64, out: &mut Vec<Packet>) {
        let coop = self.level >= Level::Coop;
        let b = &self.bubble;
        let host = self.core.peers.get(&Slot::P1);
        let publish_extra = now >= b.next_extra && !b.extra.is_empty();
        let mut walk2_sent = Vec::new();
        for (slot, peer) in self.core.peers.iter() {
            if let (Some(world), Some(host)) = (&b.world, host) {
                let payload: Vec<String> = world.iter().map(|v| fmt_g(*v, 9)).collect();
                let packet =
                    format!("WORLD|1|{}|{}|{}|{}", peer.lease, host.lease, b.world_revision, payload.join("|"));
                out.push((peer.address, packet.into_bytes()));
            }
            if let (true, Some(host)) = (publish_extra, host) {
                for row in b.extra.values() {
                    let packet = format!("XSHIP|1|{}|{}|{}", peer.lease, host.lease, row.fields.join("|"));
                    out.push((peer.address, packet.into_bytes()));
                }
            }
            for (other_slot, other) in self.others(*slot) {
                if let Some(w) = b.walkers.get(&other.lease).filter(|w| now - w.at < 1.0) {
                    let packet =
                        format!("WALK|1|{}|{}|{}|{}|{}", peer.lease, other.lease, w.seq, w.context, w.pose.join("|"));
                    out.push((peer.address, packet.into_bytes()));
                }
                if let Some(w) = b.walkers2.get(&other.lease).filter(|w| now - w.at < WALK2_FRESH) {
                    let key = (peer.lease, other.lease);
                    let repeat = b.walk2_sent.get(&key).is_some_and(|(s, at)| *s == w.seq && now - at < WALK2_REPEAT);
                    if !repeat {
                        walk2_sent.push((key, (w.seq, now)));
                        let packet = format!(
                            "WALK2|1|{}|{}|{}|{}",
                            peer.lease,
                            other.lease,
                            other_slot.as_str(),
                            w.fields.join("|")
                        );
                        out.push((peer.address, packet.into_bytes()));
                    }
                }
                // Co-op replaces legacy BODY/FIRE with BODY2/FIRE2.
                if let Some(body) = b.bodies.get(&other.lease).filter(|_| !coop) {
                    let packet =
                        format!("BODY|1|{}|{}|{}|{}", peer.lease, other.lease, body.head(), body.pose.join("|"));
                    out.push((peer.address, packet.into_bytes()));
                }
                if let Some((seq, _)) = b.fire.get(&other.lease).filter(|(_, at)| now - at < 0.5 && !coop) {
                    out.push((peer.address, format!("FIRE|1|{}|{}|{seq}", peer.lease, other.lease).into_bytes()));
                }
            }
            if let (Some((killer, generation)), Some(host), Some(body)) =
                (b.suit_kills.get(&peer.lease), host, b.bodies.get(&peer.lease))
            {
                if *killer == host.lease && body.mode == BodyMode::Suit && body.generation == *generation {
                    let packet = format!("SUITKILL|1|{}|{}|{generation}", peer.lease, host.lease);
                    out.push((peer.address, packet.into_bytes()));
                }
            }
        }
        if publish_extra {
            self.bubble.next_extra = now + XSHIP_INTERVAL;
        }
        for (key, sent) in walk2_sent {
            self.bubble.walk2_sent.insert(key, sent);
        }
        let b = &self.bubble;
        if let Some(host) = host {
            // One transaction at a time. The receiver deduplicates before dispatch to MD.
            if let Some((request, cmd)) = b.commands.iter().find(|(_, c)| c.result == CommandResult::Pending) {
                let target = match (coop, cmd.ship) {
                    (false, _) => Some(cmd.slot.as_str().to_string()),
                    // Co-op addresses the bound ship, and holds a command without one.
                    (true, Some(ship)) if ship != 0 => Some(format!("p{ship}")),
                    (true, _) => None,
                };
                if let Some(target) = target {
                    let packet = format!("COMMAND|1|{}|{request}|{target}|{}", host.lease, cmd.verb.as_str());
                    out.push((host.address, packet.into_bytes()));
                }
            }
        }
    }
}
