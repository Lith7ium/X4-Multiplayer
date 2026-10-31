//! identity.py: host display identity and shared sensor observations, scoped away from
//! native IDs.

use std::net::SocketAddr;

use super::{Fields, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{hex_utf8, is_code, is_lower_hex_pairs, seq, Hex32, Parsed, Reject, Slot};

/// P: co-op ships 1..=ship_count; F: targets 1/2 and markets 3/4; N: committed universe IDs.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Scope {
    Player,
    Fixture,
    Npc,
}

#[derive(Default)]
pub struct Identities {
    rows: OrderedMap<(Scope, u32), (u32, f64, Vec<String>)>,
    /// guest lease -> ((scope, entity) -> seq last forwarded). Only new host sequences are
    /// forwarded; the host repeats each identity with a new sequence at least every
    /// keepalive (identity_game.lua), so a repeat here cannot refresh a guest.
    forwarded: OrderedMap<Hex32, OrderedMap<(Scope, u32), u32>>,
    next_send: f64,
}

impl Identities {
    /// New host sequences for one guest, oldest arrival first, within the per-tick budget
    /// (identity.py `snapshots`).
    fn fresh_for(&mut self, guest: Hex32, host: Hex32, now: f64) -> Vec<String> {
        if !self.forwarded.contains_key(&guest) {
            self.forwarded.insert(guest, OrderedMap::new());
        }
        let rows = &self.rows;
        let forwarded = self.forwarded.get_mut(&guest).expect("inserted");
        forwarded.retain(|key, _| rows.contains_key(key));
        let live: Vec<(&(Scope, u32), &(u32, f64, Vec<String>))> =
            rows.iter().filter(|(_, v)| v.2[0] == host.as_str() && now - v.1 < 2.0).collect();
        let mut fresh: Vec<_> = live.iter().copied().filter(|(k, v)| forwarded.get(k) != Some(&v.0)).collect();
        // Stable, like Python's sorted(): equal arrival times keep insertion order.
        fresh.sort_by(|a, b| a.1 .1.partial_cmp(&b.1 .1).unwrap_or(std::cmp::Ordering::Equal));
        let budget = live.len().div_ceil(10).clamp(32, 256);
        let mut out = Vec::new();
        for (key, value) in fresh.into_iter().take(budget) {
            forwarded.insert(*key, value.0);
            out.push(value.2.join("|"));
        }
        out
    }
}

impl Session {
    pub(super) fn receive_identity(&mut self, raw: &[u8], address: SocketAddr, now: f64) {
        if raw.len() > 768 {
            return;
        }
        let Some(fields) = Fields::decode(raw) else { return };
        let _ = self.identity_update(&fields.f, address, now);
    }

    fn identity_update(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Parsed<()> {
        if f.len() != 11 || f[0] != "IDENT" || f[1] != "1" {
            return Err(Reject);
        }
        let (slot, lease) = self.authenticated(f[2], address).ok_or(Reject)?;
        if slot != Slot::P1 || f[3] != lease.as_str() {
            return Err(Reject);
        }
        if self.universe.epoch.is_some_and(|e| e != lease) {
            return Err(Reject);
        }
        let (sequence, scope, entity, code, name, known, contact) =
            (seq(f[4])?, f[5], seq(f[6])?, f[7], f[8], f[9], f[10]);
        let scope = match scope {
            "P" if (1..=u32::from(self.core.ship_count)).contains(&entity) => Scope::Player,
            "F" if matches!(entity, 1..=4) => Scope::Fixture,
            "N" if self.universe.entity_ids.values().any(|ids| ids.contains(&entity)) => Scope::Npc,
            _ => return Err(Reject),
        };
        let name_ok = name.len() <= 320 && is_lower_hex_pairs(name);
        if !is_code(code) || !name_ok || !matches!(known, "0" | "1") || !matches!(contact, "0" | "1") {
            return Err(Reject);
        }
        let decoded = hex_utf8(name)?;
        if decoded.chars().any(|c| c < ' ' || c == '\x7f') {
            return Err(Reject);
        }
        let rows = &mut self.universe.identities.rows;
        if rows.get(&(scope, entity)).is_some_and(|old| sequence <= old.0) {
            return Err(Reject);
        }
        let fields = f[3..].iter().map(|s| (*s).to_owned()).collect();
        rows.insert((scope, entity), (sequence, now, fields));
        Ok(())
    }

    pub(super) fn snapshots_identity(&mut self, now: f64, out: &mut Vec<Packet>) {
        let ids = &mut self.universe.identities;
        if now < ids.next_send {
            return;
        }
        ids.next_send = now + 0.1;
        if !self.core.dedicated {
            let (Some(host), Some(guest)) = (self.core.peers.get(&Slot::P1), self.core.peers.get(&Slot::P2)) else {
                return;
            };
            let entity_ids = &self.universe.entity_ids;
            ids.rows.retain(|(scope, entity), _| {
                *scope != Scope::Npc || entity_ids.values().any(|set| set.contains(entity))
            });
            let (guest_lease, guest_address, host_lease) = (guest.lease, guest.address, host.lease);
            ids.forwarded.retain(|lease, _| *lease == guest_lease);
            for fields in ids.fresh_for(guest_lease, host_lease, now) {
                out.push((guest_address, format!("IDENT|1|{guest_lease}|{fields}").into_bytes()));
            }
            return;
        }
        let Some(host) = self.core.peers.get(&Slot::P1).map(|p| p.lease) else { return };
        let guests: Vec<(Hex32, SocketAddr)> =
            self.core.peers.iter().filter(|(slot, _)| **slot != Slot::P1).map(|(_, p)| (p.lease, p.address)).collect();
        let entity_ids = &self.universe.entity_ids;
        ids.rows
            .retain(|(scope, entity), _| *scope != Scope::Npc || entity_ids.values().any(|set| set.contains(entity)));
        // Only forward fresh observations; each guest expires forced visibility.
        ids.forwarded.retain(|lease, _| guests.iter().any(|(guest, _)| guest == lease));
        for (guest_lease, guest_address) in guests {
            for fields in ids.fresh_for(guest_lease, host, now) {
                out.push((guest_address, format!("IDENT|1|{guest_lease}|{fields}").into_bytes()));
            }
        }
    }
}
