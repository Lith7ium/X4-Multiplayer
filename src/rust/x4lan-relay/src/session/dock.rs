//! docking.py: co-op saved-dock observations and two-player reservation arbitration.
//!
//! Native handles never cross the wire. Unknown/stale observations do not release a
//! previously claimed bay; an explicit FREE or a new movement epoch is required.

use std::net::SocketAddr;

use super::{Fields, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{seq, uint, Hex32, Parsed, Reject, Ship, Slot};

const MAX_DOCK: u32 = 250_000;
const FRESH: f64 = 0.75;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DockMode {
    Approach,
    External,
    Stored,
    Free,
    Unknown,
}

impl DockMode {
    fn parse(text: &str) -> Option<Self> {
        Some(match text {
            "APPROACH" => Self::Approach,
            "EXTERNAL" => Self::External,
            "STORED" => Self::Stored,
            "FREE" => Self::Free,
            "UNKNOWN" => Self::Unknown,
            _ => return None,
        })
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::Approach => "APPROACH",
            Self::External => "EXTERNAL",
            Self::Stored => "STORED",
            Self::Free => "FREE",
            Self::Unknown => "UNKNOWN",
        }
    }

    /// Attached modes name a bay (> 0); FREE/UNKNOWN carry none.
    fn attached(self) -> bool {
        matches!(self, Self::Approach | Self::External | Self::Stored)
    }
}

#[derive(Clone, Debug)]
struct DockRow {
    owner: Slot,
    lease: Hex32,
    epoch: u32,
    seq: u32,
    mode: DockMode,
    dock: u32,
    claim: u32,
    at: f64,
}

#[derive(Default)]
pub struct Docks {
    rows: OrderedMap<Ship, DockRow>,
    seen: OrderedMap<Ship, ((Slot, Hex32, u32), u32)>,
}

impl Session {
    fn current_dock(&self, ship: Ship, row: &DockRow) -> bool {
        row.owner == self.owner(ship) && u64::from(row.epoch) == self.epoch(ship) && !self.dead(ship)
    }

    pub(super) fn receive_dock(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > 768 {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        self.dock_update(&fields, address, now).unwrap_or_default()
    }

    fn dock_update(&mut self, fields: &Fields, address: SocketAddr, now: f64) -> Parsed<Vec<Vec<u8>>> {
        let f = &fields.f;
        if f.len() != 8 || f[1] != "1" {
            return Err(Reject);
        }
        let (slot, lease) = self.authenticated(f[2], address).ok_or(Reject)?;
        let (ship, epoch, sequence) = (self.ship_field(f[3])?, seq(f[4])?, seq(f[5])?);
        let (mode, dock) = (f[6], uint(f[7], MAX_DOCK)?);
        // Native dock attachment arbitration covers the fixture ships 1/2 only.
        let ship = ship.filter(|s| s.is_fixture()).ok_or(Reject)?;
        if self.owner(ship) != slot || self.epoch(ship) != u64::from(epoch) || self.dead(ship) {
            return Err(Reject);
        }
        let mode = DockMode::parse(mode).ok_or(Reject)?;
        if !((mode.attached() && dock > 0) || (!mode.attached() && dock == 0)) {
            return Err(Reject);
        }
        let key = (slot, lease, epoch);
        if self.dock.seen.get(&ship).is_some_and(|(k, s)| *k == key && sequence <= *s) {
            return Err(Reject);
        }
        // A denied newer request also fences older FREE/attachment packets; the last
        // accepted claim stays until an explicit newer state releases it.
        self.dock.seen.insert(ship, (key, sequence));
        if dock != 0 {
            for (other, row) in self.dock.rows.iter() {
                if *other != ship && self.current_dock(*other, row) && row.claim == dock {
                    return Ok(vec![format!("CDOCK_DENIED|1|{lease}|{ship}|{epoch}|{sequence}|{dock}").into_bytes()]);
                }
            }
        }
        let mut claim = dock;
        if mode == DockMode::Unknown {
            if let Some(old) = self.dock.rows.get(&ship) {
                if self.current_dock(ship, old) {
                    claim = old.claim;
                }
            }
        }
        let row = DockRow { owner: slot, lease, epoch, seq: sequence, mode, dock, claim, at: now };
        self.dock.rows.insert(ship, row);
        Ok(vec![])
    }

    pub(super) fn snapshots_dock(&mut self, now: f64, out: &mut Vec<Packet>) {
        for (slot, peer) in self.core.peers.iter() {
            for (ship, row) in self.dock.rows.iter() {
                let owner = self.core.peers.get(&row.owner);
                if row.owner == *slot
                    || !self.current_dock(*ship, row)
                    || owner.is_none_or(|o| o.lease != row.lease)
                    || now - row.at > FRESH
                {
                    continue;
                }
                let packet = format!(
                    "CDOCK|1|{}|{}|{ship}|{}|{}|{}|{}",
                    peer.lease,
                    row.lease,
                    row.epoch,
                    row.seq,
                    row.mode.as_str(),
                    row.dock
                );
                out.push((peer.address, packet.into_bytes()));
            }
        }
    }
}
