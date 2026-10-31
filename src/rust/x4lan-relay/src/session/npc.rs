//! npc_combat.py: host-owned NPC health and cumulative, acknowledged guest hit claims.
//!
//! No HP deltas are inferred from replica motion or disappearance. Native X4 owns
//! damage, regeneration and death; the relay only fences identity and retries.

use std::net::SocketAddr;

use super::universe::Baseline;
use super::{Fields, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{float, is_code, is_shot_digest, seq, Atom, Hex32, Parsed, Reject, Ship, Slot};

const FRESH: f64 = 5.0;
/// Refuse further admission; never evict a deduplication ledger.
const LIMIT: usize = 32768;

struct Descriptor {
    sector: String,
    macro_name: String,
    owner: String,
    active: bool,
}

/// (sector, macro, owner)
type DescriptorKey = (String, String, String);

struct NpcState {
    gen: u32,
    seq: u32,
    /// (sector, code, macro, owner)
    identity: (String, String, String, String),
    descriptor: DescriptorKey,
    alive: bool,
    at: f64,
    fields: Vec<String>,
}

struct Claim {
    seq: u32,
    hits: u32,
    digest: String,
    fields: Vec<String>,
    ack: u32,
}

/// (claimant lease, npc, npc gen, shooter, shooter gen)
type ClaimKey = (Hex32, u32, u32, u32, u32);

#[derive(Default)]
pub struct NpcCombat {
    descriptors: OrderedMap<u32, Descriptor>,
    states: OrderedMap<u32, NpcState>,
    claims: OrderedMap<ClaimKey, Claim>,
    /// guest lease -> (npc -> NHP seq last forwarded). Only a new host sequence is
    /// forwarded: the host repeats every NPC with a new sequence at least every keepalive
    /// (npc_combat_game.lua), which recovers a lost datagram and feeds a late joiner.
    forwarded: OrderedMap<Hex32, OrderedMap<u32, u32>>,
    next_send: f64,
}

impl NpcCombat {
    /// New NHP sequences for one guest, oldest arrival first, within the per-tick budget
    /// (npc_combat.py `snapshots`).
    fn fresh_for(&mut self, guest: Hex32, sector: Option<&str>, now: f64) -> Vec<String> {
        if !self.forwarded.contains_key(&guest) {
            self.forwarded.insert(guest, OrderedMap::new());
        }
        let states = &self.states;
        let forwarded = self.forwarded.get_mut(&guest).expect("inserted");
        forwarded.retain(|entity, _| states.contains_key(entity));
        let live: Vec<(u32, &NpcState)> = states
            .iter()
            .filter(|(_, r)| now - r.at < FRESH && sector == Some(r.descriptor.0.as_str()))
            .map(|(k, r)| (*k, r))
            .collect();
        let mut fresh: Vec<(u32, &NpcState)> =
            live.iter().copied().filter(|(k, r)| forwarded.get(k) != Some(&r.seq)).collect();
        // Stable, like Python's sorted(): equal arrival times keep insertion order.
        fresh.sort_by(|a, b| a.1.at.partial_cmp(&b.1.at).unwrap_or(std::cmp::Ordering::Equal));
        let budget = fresh.len().min(live.len().div_ceil(10).clamp(32, 256));
        let mut out = Vec::with_capacity(budget);
        for (entity, r) in fresh.into_iter().take(budget) {
            forwarded.insert(entity, r.seq);
            out.push(r.fields.join("|"));
        }
        out
    }
}

impl NpcCombat {
    /// A committed baseline (re)describes its sector's ships. Absence suspends
    /// admission; it is not a destruction event.
    pub fn committed(&mut self, baseline: &Baseline) {
        for value in self.descriptors.values_mut() {
            if value.sector == baseline.sector.as_str() {
                value.active = false;
            }
        }
        for row in &baseline.rows {
            let f: Vec<&str> = row.split(',').collect();
            if f[1] != "ship" {
                continue;
            }
            let entity: u32 = f[0].parse().expect("validated record id");
            if !self.descriptors.contains_key(&entity) && self.descriptors.len() >= LIMIT {
                continue;
            }
            let descriptor = Descriptor {
                sector: baseline.sector.to_string(),
                macro_name: f[2].to_owned(),
                owner: f[3].to_owned(),
                active: true,
            };
            self.descriptors.insert(entity, descriptor);
        }
    }

    fn current(&self, entity: u32, gen: u32, now: f64) -> bool {
        let (Some(r), Some(d)) = (self.states.get(&entity), self.descriptors.get(&entity)) else { return false };
        r.gen == gen
            && now - r.at < FRESH
            && r.descriptor == (d.sector.clone(), d.macro_name.clone(), d.owner.clone())
            && d.active
            && r.alive
    }
}

impl Session {
    /// This guest pilots `ship` under live health generation `gen`.
    fn npc_pilot(&mut self, ship: u32, gen: u32, slot: Slot) -> bool {
        // NPC hit claims need live pilot health, which only the fixture ships 1/2 have.
        let Some(ship) = Ship::from_number(i128::from(ship)).filter(|s| s.is_fixture()) else { return false };
        if slot == Slot::P1 || self.owner(ship) != slot || self.pilot(ship) != Some(slot) {
            return false;
        }
        let generation = self.health_generation(ship);
        u64::from(gen) == generation && generation == self.coop.health_live[ship.index()] && !self.dead(ship)
    }

    pub(super) fn receive_npc(&mut self, raw: &[u8], address: SocketAddr, now: f64) {
        if raw.len() > 768 {
            return;
        }
        let Some(fields) = Fields::decode(raw) else { return };
        let _ = self.npc_update(&fields.f, address, now);
    }

    fn npc_update(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Parsed<()> {
        if f.len() < 4 || f[1] != "1" {
            return Err(Reject);
        }
        let peer = self.authenticated(f[2], address);
        let host = self.core.peers.get(&Slot::P1).map(|p| p.lease);
        let (Some((slot, lease)), Some(host)) = (peer, host) else { return Err(Reject) };
        if f[3] != host.as_str() || self.universe.epoch != Some(host) {
            return Err(Reject);
        }
        match (f[0], f.len(), slot) {
            ("NHP", 14, Slot::P1) => {
                let (entity, gen, sequence) = (seq(f[4])?, seq(f[5])?, seq(f[6])?);
                let (sector, code, macro_name, owner) = (f[7], f[8], f[9], f[10]);
                let (hull, shield) = (float(f[11])?, float(f[12])?);
                let npc = &mut self.universe.npc;
                let d = npc.descriptors.get(&entity);
                let alive = f[13];
                let valid = entity != 0
                    && gen != 0
                    && sequence != 0
                    && d.is_some()
                    && owner != "player"
                    && is_code(code)
                    && Atom::parse(macro_name).is_ok()
                    && d.is_some_and(|d| {
                        (sector, macro_name, owner) == (d.sector.as_str(), d.macro_name.as_str(), d.owner.as_str())
                    })
                    && [hull, shield].iter().all(|v| v.is_finite() && (0.0..=100.0).contains(v))
                    && matches!(alive, "0" | "1")
                    && !(alive == "0" && hull != 0.0)
                    && !(alive == "1" && (hull <= 0.0 || !d.is_some_and(|d| d.active)));
                if !valid {
                    return Err(Reject);
                }
                let identity = (sector.to_owned(), code.to_owned(), macro_name.to_owned(), owner.to_owned());
                if let Some(old) = npc.states.get(&entity) {
                    if sequence <= old.seq || gen != old.gen || identity != old.identity || (!old.alive && alive == "1")
                    {
                        return Err(Reject);
                    }
                }
                let state = NpcState {
                    gen,
                    seq: sequence,
                    descriptor: (sector.to_owned(), macro_name.to_owned(), owner.to_owned()),
                    identity,
                    alive: alive == "1",
                    at: now,
                    fields: f[3..].iter().map(|s| (*s).to_owned()).collect(),
                };
                npc.states.insert(entity, state);
            }
            ("NCLAIM", 11, _) if slot != Slot::P1 => {
                let (entity, gen, shooter, sg, sequence, hits) =
                    (seq(f[4])?, seq(f[5])?, seq(f[6])?, seq(f[7])?, seq(f[8])?, seq(f[9])?);
                let ok = sequence != 0
                    && (1..=1_000_000).contains(&hits)
                    && self.universe.npc.current(entity, gen, now)
                    && self.npc_pilot(shooter, sg, slot)
                    && is_shot_digest(f[10])
                    && self.universe.interest(lease).is_some_and(|i| {
                        self.universe.npc.states.get(&entity).is_some_and(|s| s.descriptor.0 == i.sector.as_str())
                    });
                if !ok {
                    return Err(Reject);
                }
                let npc = &mut self.universe.npc;
                let key = (lease, entity, gen, shooter, sg);
                let old = npc.claims.get(&key);
                if old.is_some_and(|o| sequence <= o.seq || hits < o.hits || f[10] != o.digest) {
                    return Err(Reject);
                }
                if old.is_none() && npc.claims.len() >= LIMIT {
                    return Err(Reject);
                }
                let ack = old.map_or(0, |o| o.ack);
                let claim = Claim {
                    seq: sequence,
                    hits,
                    digest: f[10].to_owned(),
                    fields: f[3..].iter().map(|s| (*s).to_owned()).collect(),
                    ack,
                };
                npc.claims.insert(key, claim);
            }
            ("NACK", 10, Slot::P1) => {
                let (entity, gen, shooter, sg, count) = (seq(f[5])?, seq(f[6])?, seq(f[7])?, seq(f[8])?, seq(f[9])?);
                let claimant = Hex32::parse(f[4]).map_err(|_| Reject)?;
                if let Some(claim) = self.universe.npc.claims.get_mut(&(claimant, entity, gen, shooter, sg)) {
                    if claim.ack <= count && count <= claim.hits {
                        claim.ack = count;
                    }
                }
            }
            _ => {}
        }
        Ok(())
    }

    pub(super) fn snapshots_npc(&mut self, now: f64, out: &mut Vec<Packet>) {
        if now < self.universe.npc.next_send {
            return;
        }
        self.universe.npc.next_send = now + 0.1;
        if !self.core.dedicated {
            let host = self.core.peers.get(&Slot::P1).map(|p| (p.lease, p.address));
            let guest = self.core.peers.get(&Slot::P2).map(|p| (p.lease, p.address));
            let (Some((host_lease, host_address)), Some((guest_lease, guest_address))) = (host, guest) else { return };
            if self.universe.epoch != Some(host_lease) {
                return;
            }
            let interest_sector = self.universe.interest(guest_lease).map(|i| i.sector.to_string());
            let npc = &mut self.universe.npc;
            npc.forwarded.retain(|lease, _| *lease == guest_lease);
            for fields in npc.fresh_for(guest_lease, interest_sector.as_deref(), now) {
                out.push((guest_address, format!("NHP|1|{guest_lease}|{fields}").into_bytes()));
            }
            let keys: Vec<ClaimKey> = self.universe.npc.claims.keys().copied().collect();
            for key in keys {
                let (lease, entity, gen, shooter, sg) = key;
                if lease != guest_lease || !self.npc_pilot(shooter, sg, Slot::P2) {
                    self.universe.npc.claims.remove(&key);
                    continue;
                }
                if !self.universe.npc.current(entity, gen, now) {
                    continue;
                }
                let claim = self.universe.npc.claims.get(&key).expect("present");
                let nack =
                    format!("NACK|1|{guest_lease}|{host_lease}|{lease}|{entity}|{gen}|{shooter}|{sg}|{}", claim.ack);
                out.push((guest_address, nack.into_bytes()));
                if claim.ack < claim.hits {
                    let packet = format!("NCLAIM|1|{host_lease}|{lease}|{}", claim.fields.join("|"));
                    out.push((host_address, packet.into_bytes()));
                }
            }
            return;
        }
        let Some((host_lease, host_address)) = self.core.peers.get(&Slot::P1).map(|p| (p.lease, p.address)) else {
            return;
        };
        if self.universe.epoch != Some(host_lease) {
            return;
        }
        let guests: Vec<(Slot, Hex32, SocketAddr)> = self
            .core
            .peers
            .iter()
            .filter(|(slot, _)| **slot != Slot::P1)
            .map(|(slot, peer)| (*slot, peer.lease, peer.address))
            .collect();
        let interests: Vec<Option<String>> =
            guests.iter().map(|(_, lease, _)| self.universe.interest(*lease).map(|i| i.sector.to_string())).collect();
        let npc = &mut self.universe.npc;
        npc.forwarded.retain(|lease, _| guests.iter().any(|(_, guest, _)| guest == lease));
        for (index, (_, guest_lease, guest_address)) in guests.iter().enumerate() {
            for fields in npc.fresh_for(*guest_lease, interests[index].as_deref(), now) {
                out.push((*guest_address, format!("NHP|1|{guest_lease}|{fields}").into_bytes()));
            }
        }
        let keys: Vec<ClaimKey> = self.universe.npc.claims.keys().copied().collect();
        for key in keys {
            let (lease, entity, gen, shooter, sg) = key;
            let claimant = guests.iter().find(|(_, guest_lease, _)| *guest_lease == lease);
            let valid = claimant.is_some_and(|(slot, _, _)| self.npc_pilot(shooter, sg, *slot));
            if !valid {
                self.universe.npc.claims.remove(&key);
                continue;
            }
            if !self.universe.npc.current(entity, gen, now) {
                continue;
            }
            let (_, _, guest_address) = claimant.expect("checked");
            let claim = self.universe.npc.claims.get(&key).expect("present");
            let nack = format!("NACK|1|{lease}|{host_lease}|{lease}|{entity}|{gen}|{shooter}|{sg}|{}", claim.ack);
            out.push((*guest_address, nack.into_bytes()));
            if claim.ack < claim.hits {
                let packet = format!("NCLAIM|1|{host_lease}|{lease}|{}", claim.fields.join("|"));
                out.push((host_address, packet.into_bytes()));
            }
        }
    }
}
