//! cargo.py: reliable native cargo intentions; only host X4 mutates inventory.
//!
//! Each request is bound to the authenticated pilot and ship generation. Outcomes are
//! retained through reconnect; a timeout is never inferred as success.

use std::net::SocketAddr;

use super::{Fields, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{is_ware, seq, Hex32, Parsed, Reject, Ship, Slot};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CargoResult {
    Pending,
    Ok,
    Rejected,
    Capacity,
    Busy,
    Stale,
    Uncertain,
}

impl CargoResult {
    fn parse_done(text: &str) -> Option<Self> {
        Some(match text {
            "OK" => Self::Ok,
            "REJECTED" => Self::Rejected,
            "CAPACITY" => Self::Capacity,
            "BUSY" => Self::Busy,
            "STALE" => Self::Stale,
            "UNCERTAIN" => Self::Uncertain,
            _ => return None,
        })
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "PENDING",
            Self::Ok => "OK",
            Self::Rejected => "REJECTED",
            Self::Capacity => "CAPACITY",
            Self::Busy => "BUSY",
            Self::Stale => "STALE",
            Self::Uncertain => "UNCERTAIN",
        }
    }
}

/// A drop carries no object; a pickup names the container. The type makes the
/// Python check `(DROP and obj != 0) or (PICKUP and obj == 0)` structural.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CargoVerb {
    Drop,
    Pickup { object: u32 },
}

impl CargoVerb {
    fn parse(verb: &str, object: u32) -> Parsed<Self> {
        match (verb, object) {
            ("DROP", 0) => Ok(Self::Drop),
            ("PICKUP", 1..) => Ok(Self::Pickup { object }),
            _ => Err(Reject),
        }
    }

    fn name(self) -> &'static str {
        match self {
            Self::Drop => "DROP",
            Self::Pickup { .. } => "PICKUP",
        }
    }

    fn object(self) -> u32 {
        match self {
            Self::Drop => 0,
            Self::Pickup { object } => object,
        }
    }
}

/// Everything a retry must repeat exactly.
#[derive(Clone, PartialEq, Eq, Debug)]
struct Signature {
    slot: Slot,
    host: Hex32,
    ship: Ship,
    epoch: u32,
    verb: CargoVerb,
    ware: String,
    amount: u32,
}

struct Request {
    signature: Signature,
    result: CargoResult,
    fence: u32,
    next_send: f64,
}

#[derive(Default)]
pub struct CargoTransactions {
    requests: OrderedMap<Hex32, Request>,
    inventory: OrderedMap<String, (u32, f64, Vec<String>)>,
    next_inventory: f64,
    inventory_cursor: usize,
}

impl CargoTransactions {
    pub fn busy(&self, ship: Ship) -> bool {
        self.requests.values().any(|r| r.signature.ship == ship && r.result == CargoResult::Pending)
    }
}

fn reply(lease: Hex32, request: Hex32, status: CargoResult, fence: u32) -> Vec<Vec<u8>> {
    vec![format!("CARGOREPLY|1|{lease}|{request}|{}|{fence}", status.as_str()).into_bytes()]
}

impl Session {
    pub(super) fn receive_cargo(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > 768 {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        self.cargo_update(&fields.f, address, now).unwrap_or_default()
    }

    fn cargo_update(&mut self, f: &[&str], address: SocketAddr, now: f64) -> Parsed<Vec<Vec<u8>>> {
        if f.len() < 4 || f[1] != "1" {
            return Err(Reject);
        }
        let (slot, lease) = self.authenticated(f[2], address).ok_or(Reject)?;
        if f[0] == "CINV" && f.len() == 8 && slot == Slot::P1 && f[3] == lease.as_str() {
            let (sequence, ware, first, second) = (seq(f[4])?, f[5], seq(f[6])?, seq(f[7])?);
            if !is_ware(ware) || first.max(second) > 1_000_000 {
                return Err(Reject);
            }
            let inventory = &mut self.universe.cargo.inventory;
            match inventory.get(&ware.to_owned()) {
                Some(old) if old.2[0] == lease.as_str() && sequence <= old.0 => return Err(Reject),
                None if inventory.len() >= 256 => return Err(Reject),
                _ => {}
            }
            let fields: Vec<String> = f[3..].iter().map(|s| (*s).to_owned()).collect();
            inventory.insert(ware.to_owned(), (sequence, now, fields));
            return Ok(vec![]);
        }
        let request = Hex32::parse(f[3])?;
        if f[0] == "CARGODONE" && f.len() == 6 && slot == Slot::P1 {
            if let Some(result) = CargoResult::parse_done(f[4]) {
                let fence = seq(f[5])?;
                if let Some(old) = self.universe.cargo.requests.get_mut(&request) {
                    if old.signature.host == lease && old.result == CargoResult::Pending {
                        old.result = result;
                        old.fence = fence;
                    }
                }
                return Ok(vec![]);
            }
        }
        if f[0] != "CARGOREQ" || f.len() != 11 {
            return Err(Reject);
        }
        let (host, ship, epoch, verb, object, ware, amount) = (f[4], f[5], f[6], f[7], f[8], f[9], f[10]);
        let (ship, epoch, object, amount) = (seq(ship)?, seq(epoch)?, seq(object)?, seq(amount)?);
        let host = Hex32::parse(host)?;
        // Native cargo drop/pickup covers the fixture ships 1/2 only (fail closed beyond).
        let ship = Ship::from_number(i128::from(ship)).filter(|s| s.is_fixture()).ok_or(Reject)?;
        let verb = CargoVerb::parse(verb, object)?;
        if epoch < 1 || !is_ware(ware) || !(1..=1_000_000).contains(&amount) {
            return Err(Reject);
        }
        let signature = Signature { slot, host, ship, epoch, verb, ware: ware.to_owned(), amount };
        if let Some(old) = self.universe.cargo.requests.get(&request) {
            if old.signature != signature {
                return Ok(reply(lease, request, CargoResult::Rejected, 0));
            }
            return Ok(reply(lease, request, old.result, old.fence));
        }
        if self.core.peers.get(&Slot::P1).is_none_or(|p| p.lease != host) {
            return Ok(reply(lease, request, CargoResult::Stale, 0));
        }
        if self.owner(ship) != slot
            || self.epoch(ship) != u64::from(epoch)
            || self.pilot(ship) != Some(slot)
            || self.dead(ship)
        {
            return Ok(reply(lease, request, CargoResult::Rejected, 0));
        }
        if self.busy(ship) {
            return Ok(reply(lease, request, CargoResult::Busy, 0));
        }
        // Reject new requests at the cap; never evict a completed ID into reuse.
        if self.universe.cargo.requests.len() >= 8192 {
            return Ok(reply(lease, request, CargoResult::Busy, 0));
        }
        let entry = Request { signature, result: CargoResult::Pending, fence: 0, next_send: now };
        self.universe.cargo.requests.insert(request, entry);
        Ok(reply(lease, request, CargoResult::Pending, 0))
    }

    pub(super) fn snapshots_cargo(&mut self, now: f64, out: &mut Vec<Packet>) {
        if !self.core.dedicated {
            let host = self.core.peers.get(&Slot::P1).map(|p| (p.lease, p.address));
            let guest = self.core.peers.get(&Slot::P2).map(|p| (p.lease, p.address));
            let cargo = &mut self.universe.cargo;
            if let (Some((host_lease, _)), Some((guest_lease, guest_address))) = (host, guest) {
                if now >= cargo.next_inventory {
                    cargo.next_inventory = now + 0.2;
                    let values: Vec<&(u32, f64, Vec<String>)> = cargo.inventory.values().collect();
                    let count = values.len().min(32);
                    for i in 0..count {
                        let (_, at, fields) = values[(cargo.inventory_cursor + i) % values.len()];
                        if fields[0] == host_lease.as_str() && now - at < 2.0 {
                            out.push((
                                guest_address,
                                format!("CINV|1|{guest_lease}|{}", fields.join("|")).into_bytes(),
                            ));
                        }
                    }
                    cargo.inventory_cursor += count;
                }
            }
            for (request, item) in cargo.requests.iter_mut() {
                if item.result != CargoResult::Pending {
                    continue;
                }
                let Some((host_lease, host_address)) = host.filter(|(l, _)| *l == item.signature.host) else {
                    item.result = CargoResult::Uncertain;
                    continue;
                };
                if now < item.next_send {
                    continue;
                }
                item.next_send = now + 0.25;
                let s = &item.signature;
                let packet = format!(
                    "CARGOEXEC|1|{host_lease}|{request}|{}|{}|{}|{}|{}|{}|{}",
                    s.ship,
                    s.epoch,
                    s.verb.name(),
                    s.verb.object(),
                    s.ware,
                    s.amount,
                    s.slot.as_str()
                );
                out.push((host_address, packet.into_bytes()));
            }
            return;
        }
        let host = self.core.peers.get(&Slot::P1).map(|p| (p.lease, p.address));
        let guests: Vec<(Hex32, SocketAddr)> = self
            .core
            .peers
            .iter()
            .filter(|(slot, _)| **slot != Slot::P1)
            .map(|(_, peer)| (peer.lease, peer.address))
            .collect();
        let cargo = &mut self.universe.cargo;
        if let Some((host_lease, _)) = host {
            if now >= cargo.next_inventory {
                cargo.next_inventory = now + 0.2;
                let values: Vec<&(u32, f64, Vec<String>)> = cargo.inventory.values().collect();
                let count = values.len().min(32);
                for i in 0..count {
                    let (_, at, fields) = values[(cargo.inventory_cursor + i) % values.len()];
                    if fields[0] == host_lease.as_str() && now - at < 2.0 {
                        for (guest_lease, guest_address) in &guests {
                            let packet = format!("CINV|1|{guest_lease}|{}", fields.join("|"));
                            out.push((*guest_address, packet.into_bytes()));
                        }
                    }
                }
                cargo.inventory_cursor += count;
            }
        }
        for (request, item) in cargo.requests.iter_mut() {
            if item.result != CargoResult::Pending {
                continue;
            }
            // A timeout is uncertainty, never permission to replay into a new host.
            let Some((host_lease, host_address)) = host.filter(|(l, _)| *l == item.signature.host) else {
                item.result = CargoResult::Uncertain;
                continue;
            };
            if now < item.next_send {
                continue;
            }
            item.next_send = now + 0.25;
            let s = &item.signature;
            let packet = format!(
                "CARGOEXEC|1|{host_lease}|{request}|{}|{}|{}|{}|{}|{}|{}",
                s.ship,
                s.epoch,
                s.verb.name(),
                s.verb.object(),
                s.ware,
                s.amount,
                s.slot.as_str()
            );
            out.push((host_address, packet.into_bytes()));
        }
    }
}
