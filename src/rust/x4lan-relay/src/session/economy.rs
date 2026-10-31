//! economy.py: bounded authoritative market snapshots and idempotent trading requests.
//!
//! Only the X4 host can publish inventory/balances or complete a transaction. The relay
//! validates and routes; it never fabricates a trade's result.

use std::collections::BTreeMap;
use std::net::SocketAddr;

use super::{Fields, Level, Packet, Session};
use crate::ordmap::OrderedMap;
use crate::wire::{integer, is_ware, seq, uint, Hex32, Parsed, Reject, Slot};

pub const MAX_AMOUNT: i128 = 1_000_000;
pub const MAX_MONEY: i128 = 1_000_000_000_000;

/// An order's lifecycle. Every terminal value is a host-reported outcome, except
/// `Uncertain`, which the relay sets when the host vanished mid-transaction.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum OrderResult {
    Pending,
    Ok,
    Rejected,
    Stock,
    Funds,
    Capacity,
    Price,
    Dock,
    Busy,
    Partial,
    Uncertain,
    Unsupported,
}

impl OrderResult {
    fn parse_done(text: &str) -> Option<Self> {
        Some(match text {
            "OK" => Self::Ok,
            "REJECTED" => Self::Rejected,
            "STOCK" => Self::Stock,
            "FUNDS" => Self::Funds,
            "CAPACITY" => Self::Capacity,
            "PRICE" => Self::Price,
            "DOCK" => Self::Dock,
            "BUSY" => Self::Busy,
            "PARTIAL" => Self::Partial,
            "UNCERTAIN" => Self::Uncertain,
            "UNSUPPORTED" => Self::Unsupported,
            _ => return None,
        })
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "PENDING",
            Self::Ok => "OK",
            Self::Rejected => "REJECTED",
            Self::Stock => "STOCK",
            Self::Funds => "FUNDS",
            Self::Capacity => "CAPACITY",
            Self::Price => "PRICE",
            Self::Dock => "DOCK",
            Self::Busy => "BUSY",
            Self::Partial => "PARTIAL",
            Self::Uncertain => "UNCERTAIN",
            Self::Unsupported => "UNSUPPORTED",
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Market {
    M1,
    M2,
}

impl Market {
    fn parse(text: &str) -> Parsed<Self> {
        match text {
            "m1" => Ok(Market::M1),
            "m2" => Ok(Market::M2),
            _ => Err(Reject),
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Market::M1 => "m1",
            Market::M2 => "m2",
        }
    }
}

#[derive(Clone, Debug)]
pub struct Order {
    pub slot: Slot,
    pub lease: Hex32,
    pub owner: Hex32,
    pub market: Market,
    /// The validated basket text, forwarded verbatim to the host.
    pub lines: String,
    pub result: OrderResult,
}

/// `parse_snapshot`: validates an assembled `M,..;W,..` economy snapshot.
pub fn parse_snapshot(payload: &str) -> Parsed<()> {
    let rows: Vec<&str> = payload.split(';').collect();
    let funds: Vec<&str> = rows[0].split(',').collect();
    if funds.len() != 5 || funds[0] != "M" {
        return Err(Reject);
    }
    for value in &funds[1..] {
        integer(value, MAX_MONEY, false)?;
    }
    if !(2..=17).contains(&rows.len()) {
        return Err(Reject);
    }
    let mut seen = Vec::new();
    for row in &rows[1..] {
        let f: Vec<&str> = row.split(',').collect();
        if f.len() != 13 || f[0] != "W" || !is_ware(f[1]) || seen.contains(&f[1]) {
            return Err(Reject);
        }
        // volume, p1, p2, market1, market2, bid1, ask1, limit1, bid2, ask2, limit2
        let mut values = [0i128; 11];
        for (i, text) in f[2..].iter().enumerate() {
            values[i] = integer(text, MAX_MONEY, false)?;
        }
        if !(1..=100_000).contains(&values[0])
            || [1, 2, 3, 4, 7, 10].iter().any(|&i| values[i] > MAX_AMOUNT)
            || [5, 6, 8, 9].iter().any(|&i| values[i] < 1)
        {
            return Err(Reject);
        }
        seen.push(f[1]);
    }
    Ok(())
}

/// `parse_lines`: a basket of up to 16 `ware,signedQuantity,price` lines.
pub fn parse_lines(payload: &str) -> Parsed<()> {
    let rows: Vec<&str> = payload.split(';').collect();
    if !(1..=16).contains(&rows.len()) {
        return Err(Reject);
    }
    let mut seen = Vec::new();
    for row in rows {
        let f: Vec<&str> = row.split(',').collect();
        if f.len() != 3 || !is_ware(f[0]) || seen.contains(&f[0]) {
            return Err(Reject);
        }
        let quantity = integer(f[1], MAX_AMOUNT, true)?;
        let price = integer(f[2], MAX_MONEY, false)?;
        if quantity == 0 || price == 0 || quantity.abs() * price > MAX_MONEY {
            return Err(Reject);
        }
        seen.push(f[0]);
    }
    Ok(())
}

/// Multi-part snapshot reassembly (economy.py `Assembly`).
pub struct Assembly {
    pending: OrderedMap<i64, (f64, u32, BTreeMap<u32, String>)>,
    pub revision: i64,
}

impl Default for Assembly {
    fn default() -> Self {
        Assembly { pending: OrderedMap::new(), revision: -1 }
    }
}

impl Assembly {
    /// Add one part. `Ok(Some(text))` when a validated snapshot completes; `Err` when a
    /// complete snapshot fails validation (its parts stay staged, as in Python).
    pub fn add(&mut self, rev: i64, part: u32, total: u32, payload: &str, now: f64) -> Parsed<Option<String>> {
        if rev <= self.revision || !(1..=8).contains(&total) || !(1..=total).contains(&part) || payload.len() > 500 {
            return Ok(None);
        }
        let revision = self.revision;
        self.pending.retain(|r, v| now - v.0 < 3.0 && *r > revision);
        if !self.pending.contains_key(&rev) {
            if self.pending.len() >= 4 {
                let oldest = *self.pending.keys().min().expect("non-empty");
                self.pending.remove(&oldest);
            }
            self.pending.insert(rev, (now, total, BTreeMap::new()));
        }
        let (_, expected, parts) = self.pending.get_mut(&rev).expect("staged");
        if total != *expected {
            return Ok(None);
        }
        if parts.get(&part).is_some_and(|old| old != payload) {
            return Ok(None);
        }
        parts.insert(part, payload.to_owned());
        if parts.len() as u32 != total {
            return Ok(None);
        }
        let value: String = parts.values().map(String::as_str).collect();
        parse_snapshot(&value)?;
        self.revision = rev;
        self.pending.retain(|r, _| *r > rev);
        Ok(Some(value))
    }
}

#[derive(Default)]
pub struct Economy {
    pub economy: Option<String>,
    pub economy_owner: Option<Hex32>,
    pub assembly: Assembly,
    pub orders: OrderedMap<Hex32, Order>,
    pub docks: OrderedMap<Hex32, (u32, String)>,
    pub next_economy: f64,
    pub next_docks: f64,
}

impl Session {
    pub(super) fn expire_economy(&mut self, had_host: bool) {
        let e = &mut self.economy;
        if had_host && !self.core.peers.contains_key(&Slot::P1) {
            e.economy = None;
            e.economy_owner = None;
            e.assembly = Assembly::default();
            for order in e.orders.values_mut() {
                if order.result == OrderResult::Pending {
                    order.result = OrderResult::Uncertain;
                }
            }
        }
        let live: Vec<Hex32> = self.core.peers.values().map(|p| p.lease).collect();
        e.docks.retain(|lease, _| live.contains(lease));
        for order in e.orders.values_mut() {
            if !live.contains(&order.lease) && order.result == OrderResult::Pending {
                order.result = OrderResult::Uncertain;
            }
        }
    }

    pub(super) fn order_reply(lease: Hex32, request: Hex32, status: &str) -> Vec<u8> {
        format!("ORDER_RESULT|1|{lease}|{request}|{status}").into_bytes()
    }

    pub(crate) fn receive_economy(&mut self, raw: &[u8], address: SocketAddr, now: f64) -> Vec<Vec<u8>> {
        if raw.len() > 768 {
            return vec![];
        }
        let Some(fields) = Fields::decode(raw) else { return vec![] };
        if !fields.v1() {
            return vec![];
        }
        if !matches!(fields.kind(), "ECON" | "ORDER" | "ORDER_DONE" | "DOCKSTATE") {
            return self.receive_bubble(raw, address, now);
        }
        self.economy_update(&fields, address, now).unwrap_or_default()
    }

    fn economy_update(&mut self, fields: &Fields, address: SocketAddr, now: f64) -> Parsed<Vec<Vec<u8>>> {
        let f = &fields.f;
        let (slot, lease) = self.authenticated(fields.get(2).ok_or(Reject)?, address).ok_or(Reject)?;
        match (f[0], f.len()) {
            ("ECON", 7) if slot == Slot::P1 => {
                let e = &mut self.economy;
                if e.economy_owner != Some(lease) {
                    e.economy_owner = Some(lease);
                    e.assembly = Assembly::default();
                }
                let (rev, part, total) = (seq(f[3])?, uint(f[4], 8)?, uint(f[5], 8)?);
                if let Some(payload) = e.assembly.add(i64::from(rev), part, total, f[6], now)? {
                    e.economy = Some(payload);
                }
            }
            ("ORDER", 7) => return self.order(slot, lease, f[3], f[4], f[5], f[6]),
            ("ORDER_DONE", 5) if slot == Slot::P1 => {
                if let (Some(result), Ok(request)) = (OrderResult::parse_done(f[4]), Hex32::parse(f[3])) {
                    if let Some(order) = self.economy.orders.get_mut(&request) {
                        if order.result == OrderResult::Pending && order.owner == lease {
                            order.result = result;
                        }
                    }
                }
            }
            ("DOCKSTATE", 5) if matches!(f[4], "none" | "m1" | "m2") => {
                let sequence = seq(f[3])?;
                let old = self.economy.docks.get(&lease).map_or(-1, |(s, _)| i64::from(*s));
                if i64::from(sequence) > old {
                    self.economy.docks.insert(lease, (sequence, f[4].to_owned()));
                }
            }
            _ => {}
        }
        Ok(vec![])
    }

    /// `ORDER|1|lease|request|owner|market|lines` (also reached from co-op COOPORDER).
    pub(super) fn order(
        &mut self,
        slot: Slot,
        lease: Hex32,
        request: &str,
        owner: &str,
        market: &str,
        lines: &str,
    ) -> Parsed<Vec<Vec<u8>>> {
        let (request, owner, market) = (Hex32::parse(request)?, Hex32::parse(owner)?, Market::parse(market)?);
        parse_lines(lines)?;
        let reply = |status: &str| Ok(vec![Self::order_reply(lease, request, status)]);
        if let Some(old) = self.economy.orders.get(&request) {
            if (old.slot, old.owner, old.market, old.lines.as_str()) != (slot, owner, market, lines) {
                return Err(Reject);
            }
            return reply(old.result.as_str());
        }
        let host_ok = self.core.peers.get(&Slot::P1).is_some_and(|h| h.lease == owner);
        if !host_ok || self.economy.economy.is_none() {
            return reply("UNCERTAIN");
        }
        let orders = &mut self.economy.orders;
        if orders.len() >= 1024 || orders.values().filter(|o| o.result == OrderResult::Pending).count() >= 8 {
            return reply("BUSY");
        }
        let order = Order { slot, lease, owner, market, lines: lines.to_owned(), result: OrderResult::Pending };
        orders.insert(request, order);
        reply("PENDING")
    }

    pub(super) fn snapshots_economy(&mut self, now: f64, out: &mut Vec<Packet>) {
        let coop = self.level >= Level::Coop;
        let Some(host) = self.core.peers.get(&Slot::P1) else { return };
        let e = &mut self.economy;
        let publish_economy = now >= e.next_economy;
        let publish_docks = now >= e.next_docks;
        if publish_economy {
            e.next_economy = now + 0.5;
        }
        if publish_docks {
            e.next_docks = now + 0.2;
        }
        for (slot, peer) in self.core.peers.iter() {
            if let (true, Some(economy)) = (publish_economy, &e.economy) {
                let chunks: Vec<&str> =
                    economy.as_bytes().chunks(500).map(|c| std::str::from_utf8(c).expect("ascii")).collect();
                let revision = e.assembly.revision;
                for (i, payload) in chunks.iter().enumerate() {
                    let packet =
                        format!("ECON|1|{}|{}|{revision}|{}|{}|{payload}", peer.lease, host.lease, i + 1, chunks.len());
                    out.push((peer.address, packet.into_bytes()));
                }
            }
            for (_, other) in self.core.peers.iter().filter(|(other, _)| *other != slot) {
                if let (true, false, Some((seq, market))) = (publish_docks, coop, e.docks.get(&other.lease)) {
                    let packet = format!("DOCKSTATE|1|{}|{}|{seq}|{market}", peer.lease, other.lease);
                    out.push((peer.address, packet.into_bytes()));
                }
            }
        }
        if let Some((request, order)) = e.orders.iter().find(|(_, o)| o.result == OrderResult::Pending) {
            // Co-op names the bound ship instead of the slot, and holds orders without one.
            let target = if coop {
                self.coop.order_ships.get(request).map(|(ship, _)| ship.to_string())
            } else {
                Some(order.slot.as_str().to_string())
            };
            if let Some(target) = target {
                let kind = if coop { "COOP_EXEC" } else { "ORDER_EXEC" };
                let packet =
                    format!("{kind}|1|{}|{request}|{target}|{}|{}", host.lease, order.market.as_str(), order.lines);
                out.push((host.address, packet.into_bytes()));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SNAPSHOT: &str = "M,1,2,3,4;W,energycells,10,1,2,3,4,5,6,7,8,9,10";

    #[test]
    fn snapshots_and_baskets() {
        assert!(parse_snapshot(SNAPSHOT).is_ok());
        assert!(parse_snapshot("M,1,2,3,4").is_err());
        assert!(parse_snapshot("M,1,2,3,4;W,energycells,0,1,2,3,4,5,6,7,8,9,10").is_err());
        assert!(parse_snapshot(&format!("{SNAPSHOT};W,energycells,10,1,2,3,4,5,6,7,8,9,10")).is_err());
        assert!(parse_lines("energycells,-5,10;water,3,1").is_ok());
        assert!(parse_lines("energycells,0,10").is_err());
        assert!(parse_lines("energycells,1000000,1000001").is_err());
    }

    #[test]
    fn assembly_requires_every_part_and_increasing_revisions() {
        let mut a = Assembly::default();
        let (first, second) = SNAPSHOT.split_at(10);
        assert_eq!(a.add(1, 1, 2, first, 0.0), Ok(None));
        assert_eq!(a.add(1, 1, 2, "conflict", 0.1), Ok(None));
        assert_eq!(a.add(1, 2, 2, second, 0.2), Ok(Some(SNAPSHOT.to_string())));
        assert_eq!(a.add(1, 1, 1, SNAPSHOT, 0.3), Ok(None));
        assert_eq!(a.add(2, 1, 1, "M,1", 0.4), Err(Reject));
    }
}
