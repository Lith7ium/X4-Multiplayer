//! coop.py: stable cooperative ships, independent of player slots and sectors.
//!
//! The relay arbitrates pilot ownership, not game physics. Host X4 remains the sole
//! authority for damage, cargo, treasury and mission outcomes.

use std::f64::consts::TAU;
use std::net::SocketAddr;

use super::bubble::{Body, BodyMode, CommandResult};
use super::economy::{parse_lines, OrderResult};
use super::missions::MissionJournal;
use super::projectile::ProjectileRelay;
use super::quests::QuestState;
use super::{Fields, Level, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::pynum::fmt_f;
use crate::wire::{bounded, float, is_shot_digest, pose, seq, uint, Atom, Hex32, Parsed, Reject, Ship, Slot};

pub const POSITION_LIMIT: f64 = 50_000_000.0;
/// coop.py `MOTION_MODES`: NMODE values (coop_game.lua), companion of NAV2.
pub const MOTION_MODES: [&str; 4] = ["normal", "boost", "travel", "highway"];
/// Seconds without a pilot frame before a live health generation ends.
pub const HEALTH_STALE: f64 = 0.75;

/// Who holds the controls of a ship: `None` is Python's `'0'`.
pub type Pilot = Option<Slot>;

fn pilot_str(pilot: Pilot) -> &'static str {
    pilot.map_or("0", Slot::as_str)
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum NavKind {
    Nav,
    Nav2,
}

impl NavKind {
    pub fn as_str(self) -> &'static str {
        match self {
            NavKind::Nav => "NAV",
            NavKind::Nav2 => "NAV2",
        }
    }
}

/// The latest accepted movement frame of a ship; `fields` are f[3:] verbatim.
#[derive(Clone, Debug)]
pub struct Navigation {
    pub lease: Hex32,
    pub seq: u32,
    pub at: f64,
    pub fields: Vec<String>,
    pub kind: NavKind,
}

impl Navigation {
    /// NAV2 source time (validated finite on receipt).
    pub fn source_time(&self) -> f64 {
        float(&self.fields[12]).expect("validated NAV2 time")
    }

    pub fn sector(&self) -> &str {
        &self.fields[3]
    }
}

/// Latest NMODE of a ship: `fields` are entity|epoch|navSeq|sourceTime|mode verbatim.
#[derive(Clone, Debug)]
struct MotionMode {
    lease: Hex32,
    epoch: u32,
    seq: u32,
    at: f64,
    fields: Vec<String>,
}

#[derive(Clone, Debug)]
struct Fire2 {
    lease: Hex32,
    epoch: u32,
    seq: u32,
    time: f64,
    shots: [u32; 2],
    at: f64,
    fields: Vec<String>,
}

#[derive(Clone, Debug)]
struct Health {
    lease: Hex32,
    seq: u32,
    at: f64,
    fields: Vec<String>,
}

/// Cumulative hits keyed by (shooter, shooter gen, victim 1-4, victim gen).
#[derive(Clone, Debug)]
struct Shot {
    lease: Hex32,
    seq: u32,
    hits: u32,
    at: f64,
    fields: Vec<String>,
}

type ShotKey = (Ship, u64, u32, u64);

#[derive(Clone, Debug)]
pub struct Metrics {
    pub seq: u32,
    pub at: f64,
    pub callbacks_hz: f64,
    pub interval_avg_ms: f64,
    pub interval_max_ms: f64,
    pub state: String,
    pub sector: String,
}

/// (sequence, granted ship or 0, aboard ship or 0) per lease.
type Crew = (u32, u32, u32);

/// Per-ship vectors are indexed by `Ship::index()` and sized to the session's ship
/// count. Pilot-reported health only runs for the fixture ships 1/2.
pub struct Coop {
    pub owners: Vec<Slot>,
    pub epochs: Vec<u64>,
    pub crew: OrderedMap<Hex32, Crew>,
    pub navigation: OrderedMap<Ship, Navigation>,
    motion_modes: OrderedMap<Ship, MotionMode>,
    fire2: OrderedMap<Ship, Fire2>,
    health: OrderedMap<Ship, Health>,
    pub health_gen: Vec<u64>,
    health_key: Vec<Option<(Slot, u64, Pilot)>>,
    pub health_live: Vec<u64>,
    health_seen: Vec<f64>,
    shots: OrderedMap<ShotKey, Shot>,
    pub projectiles: ProjectileRelay,
    body_sectors: OrderedMap<Hex32, String>,
    pub game_metrics: OrderedMap<Hex32, Metrics>,
    pub authority_revision: u64,
    pub order_ships: OrderedMap<Hex32, (Ship, u32)>,
    pub quest: QuestState,
    pub missions: MissionJournal,
}

impl Coop {
    /// Ship 2 starts with guest p2 (two-player semantics); every other ship starts with
    /// the authority p1 until a guest takes it through the normal CREW handoff.
    pub fn new(ships: u8) -> Self {
        let n = usize::from(ships.max(2));
        let owners = (1..=n).map(|i| if i == 2 { Slot::P2 } else { Slot::P1 }).collect();
        Coop {
            owners,
            epochs: vec![1; n],
            crew: OrderedMap::new(),
            navigation: OrderedMap::new(),
            motion_modes: OrderedMap::new(),
            fire2: OrderedMap::new(),
            health: OrderedMap::new(),
            health_gen: vec![0; n],
            health_key: vec![None; n],
            health_live: vec![0; n],
            health_seen: vec![0.0; n],
            shots: OrderedMap::new(),
            projectiles: ProjectileRelay::default(),
            body_sectors: OrderedMap::new(),
            game_metrics: OrderedMap::new(),
            authority_revision: 1,
            order_ships: OrderedMap::new(),
            quest: QuestState::default(),
            missions: MissionJournal::default(),
        }
    }
}

const COOP_KINDS: [&str; 15] = [
    "CREW",
    "NAV",
    "NAV2",
    "NMODE",
    "FIRE2",
    "HEALTH",
    "HACK",
    "HSTALE",
    "SHOT",
    "BODY2",
    "PERF",
    "COOPORDER",
    "QUEST",
    "BOLT",
    "BEND",
];

impl Session {
    pub fn owner(&self, ship: Ship) -> Slot {
        self.coop.owners[ship.index()]
    }

    pub fn epoch(&self, ship: Ship) -> u64 {
        self.coop.epochs[ship.index()]
    }

    /// The slot whose granted crew report controls `ship`.
    pub fn pilot(&self, ship: Ship) -> Pilot {
        self.core
            .peers
            .iter()
            .find(|(_, p)| self.coop.crew.get(&p.lease).map_or(0, |c| c.1) == ship.number())
            .map(|(s, _)| *s)
    }

    /// Pending trade or command bound to `ship` (universe adds cargo transactions).
    pub fn busy(&self, ship: Ship) -> bool {
        let orders = self.economy.orders.iter().any(|(k, v)| {
            v.result == OrderResult::Pending && self.coop.order_ships.get(k).is_some_and(|(s, _)| *s == ship)
        });
        let commands =
            self.bubble.commands.values().any(|c| c.result == CommandResult::Pending && c.ship == Some(ship.number()));
        orders || commands || (self.level >= Level::Universe && self.universe.cargo.busy(ship))
    }

    pub fn dead(&self, ship: Ship) -> bool {
        if ship.is_fixture() {
            self.bubble.world.is_some_and(|w| w[ship.index() * 3] <= 1.01)
        } else {
            self.bubble.extra.get(&ship).is_some_and(|row| row.hull <= 1.01)
        }
    }

    fn end_health(&mut self, ship: Ship) {
        self.coop.health_gen[ship.index()] += 1;
        self.coop.health_live[ship.index()] = 0;
        self.coop.health.remove(&ship);
    }

    /// One health generation per (owner, epoch, pilot); a change starts the next one.
    pub fn health_generation(&mut self, ship: Ship) -> u64 {
        let key = Some((self.owner(ship), self.epoch(ship), self.pilot(ship)));
        let i = ship.index();
        if key != self.coop.health_key[i] {
            self.coop.health_key[i] = key;
            self.coop.health_gen[i] += 1;
            self.coop.health.remove(&ship);
        }
        self.coop.health_gen[i]
    }

    /// A SHOT victim: 1/2 are the fixture co-op ships, 3/4 the LAN targets, in every
    /// session size. Ships 3.. are never shot victims (their health stays host-owned).
    fn victim_ship(victim: u32) -> Option<Ship> {
        Ship::from_number(i128::from(victim)).filter(|s| s.is_fixture())
    }

    /// Co-op ships carry their health generation; LAN targets (3, 4) are host objects.
    fn victim_generation(&mut self, victim: u32) -> u64 {
        match Self::victim_ship(victim) {
            Some(ship) => self.health_generation(ship),
            None => 0,
        }
    }

    /// The slot whose world decides the victim: a co-op ship's remote pilot while its
    /// health generation is live, otherwise the host.
    fn victim_authority(&mut self, victim: u32) -> Slot {
        if let Some(ship) = Self::victim_ship(victim) {
            let slot = self.owner(ship);
            let gen = self.health_generation(ship);
            if slot != Slot::P1 && self.pilot(ship) == Some(slot) && self.coop.health_live[ship.index()] == gen {
                return slot;
            }
        }
        Slot::P1
    }

    pub(super) fn expire_coop(&mut self, now: f64) {
        let live = self.live_leases();
        let retained = self.retained_leases();
        let c = &mut self.coop;
        if c.crew.keys().any(|k| !live.contains(k)) {
            c.authority_revision += 1;
        }
        c.crew.retain(|k, _| live.contains(k));
        c.body_sectors.retain(|k, _| retained.contains(k));
        c.game_metrics.retain(|k, _| live.contains(k));
        c.health.retain(|_, v| live.contains(&v.lease));
        // Each authority transition gets its own generation; a live generation whose
        // pilot stream stopped is an authority boundary too.
        for ship in [Ship::ONE, Ship::TWO] {
            let gen = self.health_generation(ship);
            if self.coop.health_live[ship.index()] == gen && now - self.coop.health_seen[ship.index()] > HEALTH_STALE {
                self.end_health(ship);
            }
        }
    }

    /// CoopSession.receive entry: mission packets bypass the co-op envelope checks.
    pub(crate) fn receive_coop_entry(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.starts_with(b"MSTATE|") || raw.starts_with(b"MREQ|") {
            self.receive_mission(raw, address, now);
            return vec![];
        }
        self.receive_coop(raw, address, now)
    }

    fn receive_coop(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > 768 {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        if fields.len() < 3 || fields.f[1] != "1" {
            return vec![];
        }
        let kind = fields.kind();
        if !COOP_KINDS.contains(&kind) {
            // Legacy movement/fire/dock frames must never override cooperative authority.
            if matches!(kind, "STATE" | "FIRE" | "DOCKSTATE" | "BODY" | "ORDER") {
                return vec![];
            }
            let old_leases: Vec<(Slot, Hex32)> = self.core.peers.iter().map(|(s, p)| (*s, p.lease)).collect();
            let result = self.receive_economy(raw, address, now);
            if kind == "HELLO" {
                self.after_hello(&old_leases);
            }
            if kind == "ACTION" && fields.len() == 7 {
                if let Some(reply) = self.bind_action(fields.f[4]) {
                    return vec![reply];
                }
            }
            return result;
        }
        let Some((slot, lease)) = self.authenticated(fields.f[2], address) else { return vec![] };
        self.coop_update(&fields, slot, lease, address, now).unwrap_or_default()
    }

    /// A fresh game in a slot ends the movement authority its previous lease held.
    fn after_hello(&mut self, old_leases: &[(Slot, Hex32)]) {
        let current: Vec<(Slot, Hex32)> = self.core.peers.iter().map(|(s, p)| (*s, p.lease)).collect();
        for (slot, lease) in current {
            if old_leases.iter().any(|(s, l)| *s == slot && *l == lease) {
                continue;
            }
            for ship in Ship::all(self.core.ship_count) {
                let stale = self.coop.navigation.get(&ship).is_some_and(|nav| nav.lease != lease);
                if self.owner(ship) == slot && stale {
                    self.coop.epochs[ship.index()] += 1;
                    self.coop.navigation.remove(&ship);
                    self.coop.motion_modes.remove(&ship);
                    self.coop.authority_revision += 1;
                }
            }
        }
    }

    /// Bind a newly accepted ACTION to the requester's ship, rejecting it otherwise.
    fn bind_action(&mut self, request: &str) -> Option<Vec<u8>> {
        let key = Hex32::parse(request).ok()?;
        let cmd = self.bubble.commands.get(&key)?;
        if cmd.ship.is_some() {
            return None;
        }
        let slot = cmd.slot;
        let entity = self.core.peers.get(&slot).map_or(0, |p| self.coop.crew.get(&p.lease).map_or(0, |c| c.1));
        let ship = Ship::from_number(i128::from(entity)).filter(|s| s.number() <= u32::from(self.core.ship_count));
        // Legacy fixture actions (energy-cell drops/station transfer) exist for ships 1/2 only.
        let rejected = match ship {
            Some(ship) => self.owner(ship) != slot || self.dead(ship) || !ship.is_fixture(),
            None => true,
        };
        let cmd = self.bubble.commands.get_mut(&key).expect("present");
        cmd.ship = Some(entity);
        if rejected {
            cmd.result = CommandResult::Rejected;
            return Some(format!("RESULT|1|{request}|REJECTED").into_bytes());
        }
        None
    }

    fn coop_update(
        &mut self,
        fields: &Fields,
        slot: Slot,
        lease: Hex32,
        address: SocketAddr,
        now: f64,
    ) -> Parsed<Vec<Vec<u8>>> {
        let f = &fields.f;
        match (f[0], f.len()) {
            ("QUEST", 8) if slot == Slot::P1 => self.coop.quest.receive(lease, &f[3..])?,
            ("CREW", 6) => return self.crew(f, slot, lease),
            ("NAV", 15) | ("NAV2", 22) => self.navigation(fields, slot, lease, now)?,
            ("NMODE", 8) => self.motion_mode(fields, slot, lease, now)?,
            ("FIRE2", 11) => self.fire2(fields, slot, lease, now)?,
            ("HEALTH", 8) => {
                let (ship, gen, sequence) = (self.ship_field(f[3])?, seq(f[4])?, seq(f[5])?);
                // Pilot-reported health is a fixture-ship subsystem (fail closed beyond 2).
                let ship = ship.filter(|s| s.is_fixture()).ok_or(Reject)?;
                if self.owner(ship) != slot || self.pilot(ship) != Some(slot) {
                    return Err(Reject);
                }
                let gen = u64::from(gen);
                if gen != self.health_generation(ship) || gen != self.coop.health_live[ship.index()] || self.dead(ship)
                {
                    return Err(Reject);
                }
                let values = [float(f[6])?, float(f[7])?];
                if !values.iter().all(|v| v.is_finite() && (0.0..=100.0).contains(v)) {
                    return Err(Reject);
                }
                if self.coop.health.get(&ship).is_some_and(|old| sequence <= old.seq) {
                    return Err(Reject);
                }
                self.coop.health.insert(ship, Health { lease, seq: sequence, at: now, fields: fields.tail(3) });
                self.coop.health_seen[ship.index()] = now;
            }
            ("SHOT", 10) => self.shot(fields, slot, lease, now)?,
            ("BOLT", _) | ("BEND", _) => {
                let raw = fields.raw;
                self.receive_projectile(slot, lease, raw, now);
            }
            ("HACK", 5) => {
                let (ship, gen) = (self.ship_field(f[3])?, u64::from(seq(f[4])?));
                if let Some(ship) = ship.filter(|s| s.is_fixture()) {
                    if self.owner(ship) == slot
                        && self.pilot(ship) == Some(slot)
                        && gen == self.health_generation(ship)
                        && self.coop.health_live[ship.index()] != gen
                    {
                        self.coop.health_live[ship.index()] = gen;
                        self.coop.health_seen[ship.index()] = now;
                    }
                }
            }
            ("HSTALE", 5) => {
                // The host's own freshness release ends the generation it names.
                let (ship, gen) = (self.ship_field(f[3])?, u64::from(seq(f[4])?));
                if let (Slot::P1, Some(ship)) = (slot, ship.filter(|s| s.is_fixture())) {
                    if gen == self.health_generation(ship) {
                        self.end_health(ship);
                    }
                }
            }
            ("BODY2", 14) => {
                let (sequence, mode, dead, generation) = (seq(f[3])?, f[4], uint(f[5], 1)?, seq(f[6])?);
                let mode = BodyMode::parse(mode)?;
                if mode == BodyMode::Suit && generation == 0 {
                    return Err(Reject);
                }
                Atom::parse_macro(f[7])?;
                pose(&f[8..], POSITION_LIMIT)?;
                if self.bubble.bodies.get(&lease).is_some_and(|old| !old.superseded_by(sequence, dead, generation)) {
                    return Err(Reject);
                }
                let body = Body { seq: sequence, mode, dead, generation, pose: fields.tail(8) };
                self.bubble.bodies.insert(lease, body);
                self.coop.body_sectors.insert(lease, f[7].to_owned());
            }
            ("PERF", 9) => {
                let sequence = seq(f[3])?;
                let values = [float(f[4])?, float(f[5])?, float(f[6])?];
                if !values.iter().all(|v| v.is_finite() && (0.0..=60000.0).contains(v)) {
                    return Err(Reject);
                }
                if !matches!(f[7], "ship" | "foot" | "suit" | "paused" | "waiting") {
                    return Err(Reject);
                }
                Atom::parse(f[8])?;
                if self.coop.game_metrics.get(&lease).is_some_and(|old| sequence <= old.seq) {
                    return Err(Reject);
                }
                let metrics = Metrics {
                    seq: sequence,
                    at: now,
                    callbacks_hz: values[0],
                    interval_avg_ms: values[1],
                    interval_max_ms: values[2],
                    state: f[7].to_owned(),
                    sector: f[8].to_owned(),
                };
                self.coop.game_metrics.insert(lease, metrics);
            }
            ("COOPORDER", 9) => return self.coop_order(f, slot, lease, address, now),
            _ => {}
        }
        Ok(vec![])
    }

    fn crew(&mut self, f: &[&str], slot: Slot, lease: Hex32) -> Parsed<Vec<Vec<u8>>> {
        let ships = u32::from(self.core.ship_count);
        let (sequence, controlled, aboard) = (seq(f[3])?, uint(f[4], ships)?, uint(f[5], ships)?);
        let old = self.coop.crew.get(&lease).copied();
        let old_seq = old.map_or(-1, |c| i64::from(c.0));
        if i64::from(sequence) <= old_seq || (controlled != 0 && controlled != aboard) {
            return Err(Reject);
        }
        let mut granted = controlled;
        if let Some(ship) = Ship::from_number(i128::from(controlled)) {
            let occupied = self.pilot(ship);
            if self.dead(ship) || occupied.is_some_and(|s| s != slot) || (self.owner(ship) != slot && self.busy(ship)) {
                granted = 0;
            } else if self.owner(ship) != slot {
                self.coop.owners[ship.index()] = slot;
                self.coop.epochs[ship.index()] += 1;
                self.coop.navigation.remove(&ship);
                self.coop.motion_modes.remove(&ship);
                self.coop.authority_revision += 1;
            }
        }
        self.coop.crew.insert(lease, (sequence, granted, aboard));
        if old.map_or((0, 0), |c| (c.1, c.2)) != (granted, aboard) {
            self.coop.authority_revision += 1;
        }
        if controlled != 0 && granted == 0 {
            return Ok(vec![format!("PILOT_DENIED|1|{lease}|{sequence}|{controlled}").into_bytes()]);
        }
        Ok(vec![])
    }

    fn navigation(&mut self, fields: &Fields, slot: Slot, lease: Hex32, now: f64) -> Parsed<()> {
        let f = &fields.f;
        let (ship, epoch, sequence) = (self.ship_field(f[3])?, seq(f[4])?, seq(f[5])?);
        let ship = ship.ok_or(Reject)?;
        if self.owner(ship) != slot || u64::from(epoch) != self.epoch(ship) {
            return Err(Reject);
        }
        Atom::parse_macro(f[6])?;
        pose(&f[7..13], POSITION_LIMIT)?;
        uint(f[13], 2)?;
        seq(f[14])?;
        let old = self.coop.navigation.get(&ship);
        if old.is_some_and(|o| o.lease == lease && sequence <= o.seq) {
            return Err(Reject);
        }
        if self.dead(ship) {
            return Err(Reject);
        }
        let kind = if f[0] == "NAV2" { NavKind::Nav2 } else { NavKind::Nav };
        if kind == NavKind::Nav2 {
            let source_time = float(f[15])?;
            let mut velocity = Vec::with_capacity(6);
            for text in &f[16..22] {
                velocity.push(float(text)?);
            }
            if !source_time.is_finite() || !(0.0..=1e12).contains(&source_time) {
                return Err(Reject);
            }
            if !bounded(&velocity, 100000.0, TAU) {
                return Err(Reject);
            }
            if old.is_some_and(|o| o.kind == NavKind::Nav2 && source_time <= o.source_time()) {
                return Err(Reject);
            }
        }
        let nav = Navigation { lease, seq: sequence, at: now, fields: fields.tail(3), kind };
        self.coop.navigation.insert(ship, nav);
        Ok(())
    }

    /// coop.py NMODE: entity|epoch|navSeq|sourceTime|mode from the ship's movement authority.
    fn motion_mode(&mut self, fields: &Fields, slot: Slot, lease: Hex32, now: f64) -> Parsed<()> {
        let f = &fields.f;
        let (ship, epoch, sequence) = (self.ship_field(f[3])?, seq(f[4])?, seq(f[5])?);
        let ship = ship.ok_or(Reject)?;
        if self.owner(ship) != slot || u64::from(epoch) != self.epoch(ship) {
            return Err(Reject);
        }
        let source_time = float(f[6])?;
        if !source_time.is_finite() || !(0.0..=1e12).contains(&source_time) || !MOTION_MODES.contains(&f[7]) {
            return Err(Reject);
        }
        if let Some(old) = self.coop.motion_modes.get(&ship) {
            if old.lease == lease && old.epoch == epoch && sequence <= old.seq {
                return Err(Reject);
            }
        }
        if self.dead(ship) {
            return Err(Reject);
        }
        let mode = MotionMode { lease, epoch, seq: sequence, at: now, fields: fields.tail(3) };
        self.coop.motion_modes.insert(ship, mode);
        Ok(())
    }

    fn fire2(&mut self, fields: &Fields, slot: Slot, lease: Hex32, now: f64) -> Parsed<()> {
        // entity|epoch|seq|sourceTime|primaryGroup|secondaryGroup|primaryShots|secondaryShots
        let f = &fields.f;
        let (ship, epoch, sequence) = (self.ship_field(f[3])?, seq(f[4])?, seq(f[5])?);
        let ship = ship.ok_or(Reject)?;
        if self.owner(ship) != slot || u64::from(epoch) != self.epoch(ship) {
            return Err(Reject);
        }
        let source_time = float(f[6])?;
        if !source_time.is_finite() || !(0.0..=1e12).contains(&source_time) {
            return Err(Reject);
        }
        let groups = [uint(f[7], 4)?, uint(f[8], 4)?];
        if groups.contains(&0) {
            return Err(Reject);
        }
        let shots = [seq(f[9])?, seq(f[10])?];
        if let Some(old) = self.coop.fire2.get(&ship) {
            if old.lease == lease
                && old.epoch == epoch
                && (sequence <= old.seq
                    || source_time <= old.time
                    || shots[0] < old.shots[0]
                    || shots[1] < old.shots[1])
            {
                return Err(Reject);
            }
        }
        if self.dead(ship) {
            return Err(Reject);
        }
        let fire = Fire2 { lease, epoch, seq: sequence, time: source_time, shots, at: now, fields: fields.tail(3) };
        self.coop.fire2.insert(ship, fire);
        Ok(())
    }

    fn shot(&mut self, fields: &Fields, slot: Slot, lease: Hex32, now: f64) -> Parsed<()> {
        // shooter|shooterGen|victim|victimGen|seq|hits|digest: cumulative hits the
        // sender's own piloted ship scored; the victim's authority applies them.
        let f = &fields.f;
        let (shooter, sgen, victim, vgen) =
            (self.ship_field(f[3])?, seq(f[4])?, uint(f[5], 4)?, uint(f[6], 2_147_483_647)?);
        let (sequence, hits) = (seq(f[7])?, uint(f[8], 1_000_000)?);
        // Shot claims are a fixture-ship subsystem: ships 3.. neither claim nor receive.
        let shooter = shooter.filter(|s| s.is_fixture()).ok_or(Reject)?;
        let (sgen, vgen) = (u64::from(sgen), u64::from(vgen));
        let accepted = self.owner(shooter) == slot
            && self.pilot(shooter) == Some(slot)
            && sgen == self.health_generation(shooter)
            && victim != 0
            && victim != shooter.number()
            && vgen == self.victim_generation(victim)
            && self.victim_authority(victim) != slot
            && (slot == Slot::P1 || self.coop.health_live[shooter.index()] == sgen)
            && is_shot_digest(f[9]);
        if !accepted {
            return Err(Reject);
        }
        let key = (shooter, sgen, victim, vgen);
        if let Some(old) = self.coop.shots.get(&key) {
            if old.lease != lease || sequence <= old.seq || hits < old.hits || f[9] != old.fields[6] {
                return Err(Reject);
            }
        }
        self.coop.shots.insert(key, Shot { lease, seq: sequence, hits, at: now, fields: fields.tail(3) });
        Ok(())
    }

    fn coop_order(
        &mut self,
        f: &[&str],
        slot: Slot,
        lease: Hex32,
        address: SocketAddr,
        now: f64,
    ) -> Parsed<Vec<Vec<u8>>> {
        let (request, host, entity, epoch, market, lines) = (f[3], f[4], f[5], f[6], f[7], f[8]);
        let (ship, epoch) = (self.ship_field(entity)?, seq(epoch)?);
        let ship = ship.ok_or(Reject)?;
        let key = Hex32::parse(request)?;
        parse_lines(lines)?;
        if self.economy.orders.contains_key(&key) {
            if self.coop.order_ships.get(&key) != Some(&(ship, epoch)) {
                return Err(Reject);
            }
        } else if !ship.is_fixture() {
            // Market cargo rows and native trade execution cover ships 1/2 only.
            return Ok(vec![Self::order_reply(lease, key, "REJECTED")]);
        } else if self.owner(ship) != slot
            || u64::from(epoch) != self.epoch(ship)
            || self.pilot(ship) != Some(slot)
            || self.dead(ship)
        {
            return Ok(vec![Self::order_reply(lease, key, "REJECTED")]);
        }
        // Python re-enters EconomySession.receive with a synthesized ORDER packet,
        // which re-runs expire and re-authenticates; both are no-ops at this point.
        let _ = (address, now);
        let reply = self.order(slot, lease, request, host, market, lines).unwrap_or_default();
        if self.economy.orders.contains_key(&key) {
            self.coop.order_ships.insert(key, (ship, epoch));
        }
        Ok(reply)
    }

    pub(super) fn snapshots_coop(&mut self, now: f64, out: &mut Vec<Packet>) {
        let slots: Vec<(Slot, Hex32, SocketAddr)> =
            self.core.peers.iter().map(|(s, p)| (*s, p.lease, p.address)).collect();
        for &(slot, lease, address) in &slots {
            let mut authority = format!("AUTH|1|{lease}|{}", self.coop.authority_revision);
            for ship in Ship::all(self.core.ship_count) {
                authority +=
                    &format!("|{}|{}|{}", self.owner(ship).as_str(), self.epoch(ship), pilot_str(self.pilot(ship)));
            }
            out.push((address, authority.into_bytes()));
            for (ship, mode) in self.coop.motion_modes.iter() {
                // Forwarded before NAV2 so a receiver knows the mode of the sample it renders.
                if self.owner(*ship) == slot
                    || u64::from(mode.epoch) != self.epoch(*ship)
                    || now - mode.at > 0.75
                    || self.dead(*ship)
                {
                    continue;
                }
                let packet = format!("NMODE|1|{lease}|{}|{}", mode.lease, mode.fields.join("|"));
                out.push((address, packet.into_bytes()));
            }
            for (ship, nav) in self.coop.navigation.iter() {
                if self.owner(*ship) == slot || now - nav.at > 0.75 || self.dead(*ship) {
                    continue;
                }
                let packet = format!("{}|1|{lease}|{}|{}", nav.kind.as_str(), nav.lease, nav.fields.join("|"));
                out.push((address, packet.into_bytes()));
            }
            for (ship, fire) in self.coop.fire2.iter() {
                // A frame from an old owner/epoch is never forwarded; stale fire must stop.
                if self.owner(*ship) == slot
                    || u64::from(fire.epoch) != self.epoch(*ship)
                    || now - fire.at > 0.75
                    || self.dead(*ship)
                {
                    continue;
                }
                let packet = format!("FIRE2|1|{lease}|{}|{}", fire.lease, fire.fields.join("|"));
                out.push((address, packet.into_bytes()));
            }
            // HGEN names the generations of THIS authority revision.
            // HGEN and pilot health stay two-ship in every session size.
            let (g1, g2) = (self.health_generation(Ship::ONE), self.health_generation(Ship::TWO));
            out.push((address, format!("HGEN|1|{lease}|{}|{g1}|{g2}", self.coop.authority_revision).into_bytes()));
            for ship in [Ship::ONE, Ship::TWO] {
                let gen = self.health_generation(ship);
                if let Some(hp) = self.coop.health.get(&ship) {
                    if self.owner(ship) != slot && now - hp.at <= 0.75 && !self.dead(ship) {
                        let packet = format!("HEALTH|1|{lease}|{}|{}", hp.lease, hp.fields.join("|"));
                        out.push((address, packet.into_bytes()));
                    }
                }
                if slot != Slot::P1
                    && self.owner(ship) == slot
                    && self.pilot(ship) == Some(slot)
                    && self.coop.health_live[ship.index()] != gen
                    && !self.dead(ship)
                {
                    if let Some(world) = self.bubble.world {
                        let (hull, shield) = (world[ship.index() * 3], world[ship.index() * 3 + 1]);
                        let packet = format!("HBASE|1|{lease}|{ship}|{gen}|{}|{}", fmt_f(hull, 3), fmt_f(shield, 3));
                        out.push((address, packet.into_bytes()));
                    }
                }
            }
            let keys: Vec<ShotKey> = self.coop.shots.keys().copied().collect();
            for key in keys {
                let (shooter, sgen, victim, vgen) = key;
                if sgen != self.health_generation(shooter) || vgen != self.victim_generation(victim) {
                    self.coop.shots.remove(&key); // an ended generation never forwards
                    continue;
                }
                // Never back to the shooter's own slot: where it is the authority its hits are native.
                let shot = self.coop.shots.get(&key).expect("present");
                let (shot_lease, at, fields) = (shot.lease, shot.at, shot.fields.join("|"));
                if self.victim_authority(victim) == slot
                    && self.owner(shooter) != slot
                    && now - at <= 0.75
                    && !self.dead(shooter)
                {
                    out.push((address, format!("SHOT|1|{lease}|{shot_lease}|{fields}").into_bytes()));
                }
            }
            for (_, other) in self.core.peers.iter().filter(|(other_slot, _)| **other_slot != slot) {
                if let (Some(body), Some(sector)) =
                    (self.bubble.bodies.get(&other.lease), self.coop.body_sectors.get(&other.lease))
                {
                    let packet =
                        format!("BODY2|1|{lease}|{}|{}|{sector}|{}", other.lease, body.head(), body.pose.join("|"));
                    out.push((address, packet.into_bytes()));
                }
            }
        }
        self.snapshots_projectiles(now, out);
        self.snapshots_quest(now, out);
        self.snapshots_missions(now, out);
    }
}
