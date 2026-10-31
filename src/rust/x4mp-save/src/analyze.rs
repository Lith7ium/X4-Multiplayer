//! `analyze`: object counts by class and sector, dynamic share, reference statistics.

use std::collections::{BTreeMap, HashMap};
use std::fmt::Write as _;

use crate::index::{SaveIndex, Target, UnitKind};
use crate::schema;

#[derive(Default, Clone)]
pub struct SectorRow {
    pub macro_: String,
    pub owner: String,
    pub stations: u64,
    pub npc_ships: [u64; 5],
    pub keep_owner_ships: u64,
    pub other_objects: BTreeMap<String, u64>,
    pub dynamic_units: u64,
    pub dynamic_components: u64,
    pub components: u64,
}

/// One `element@attribute`: (name, count, weak, unresolved, target kind/class histogram).
pub type RefStat = (String, u64, bool, u64, Vec<(String, u64)>);

pub struct Analysis {
    pub class_totals: BTreeMap<String, u64>,
    pub object_totals: BTreeMap<String, u64>,
    pub sectors: Vec<SectorRow>,
    pub components: u64,
    pub dynamic_components: u64,
    pub dynamic_units: u64,
    pub dynamic_bytes: u64,
    pub pool_units: u64,
    pub entity_units: u64,
    pub refs: Vec<RefStat>,
    pub incoming_by_kind: Vec<(String, u64)>,
    /// Sector macro of the unit holding the player entity (`class="player"`).
    pub player_sector: Option<String>,
}

const SIZES: [&str; 5] = ["xs", "s", "m", "l", "xl"];

fn target_label(idx: &SaveIndex, t: Target) -> String {
    match t {
        Target::Component(id) => match idx.comps.get(&id) {
            Some(c) => format!("component:{}", idx.syms.name(c.class)),
            None if idx.entities.contains_key(&id) => "aidirector entity".into(),
            None => "unresolved component".into(),
        },
        Target::Connection(id) => {
            if idx.conns.contains_key(&id) {
                "connection".into()
            } else {
                "unresolved connection".into()
            }
        }
        Target::Local(ns, id) => {
            if idx.local_defs.contains_key(&(ns, id)) {
                "local (order/trade/entry/build)".into()
            } else {
                "unresolved local".into()
            }
        }
        Target::Pool(t, n) => {
            if idx.pool.contains_key(&(t, n)) {
                format!("pool:{}", idx.syms.name(t))
            } else if idx.pool_types.contains(&t) {
                format!("unresolved pool:{}", idx.syms.name(t))
            } else {
                format!("non-pool value type {}", idx.syms.name(t))
            }
        }
    }
}

pub fn analyze(idx: &SaveIndex, keep_owner: &str) -> Analysis {
    let keep_owner = idx.syms.get(keep_owner);
    let mut class_totals: BTreeMap<String, u64> = BTreeMap::new();
    let mut object_totals: BTreeMap<String, u64> = BTreeMap::new();
    let mut rows: HashMap<u64, SectorRow> = HashMap::new();
    let mut dynamic_components = 0u64;
    for c in idx.comps.values() {
        let class = idx.syms.name(c.class);
        *class_totals.entry(class.to_string()).or_default() += 1;
        let unit = &idx.units[c.unit as usize];
        let dynamic = unit.kind == UnitKind::Dynamic;
        if dynamic {
            dynamic_components += 1;
        }
        let row = rows.entry(c.sector).or_insert_with(|| SectorRow {
            macro_: if c.sector == 0 { "(outside sectors)".into() } else { idx.sector_macro(c.sector).to_string() },
            owner: idx.sectors.get(&c.sector).map(|s| idx.syms.name(s.owner).to_string()).unwrap_or_default(),
            ..Default::default()
        });
        row.components += 1;
        if dynamic {
            row.dynamic_components += 1;
        }
        if !schema::is_object_class(class) {
            continue;
        }
        let label = match schema::ship_size(class) {
            Some(_) if keep_owner.is_some_and(|o| o == c.owner) => format!("{class} (keep-owner)"),
            Some(_) => format!("{class} (npc)"),
            None => class.to_string(),
        };
        *object_totals.entry(label).or_default() += 1;
        match (class, schema::ship_size(class)) {
            ("station", _) => row.stations += 1,
            (_, Some(sz)) => {
                if keep_owner.is_some_and(|o| o == c.owner) {
                    row.keep_owner_ships += 1;
                } else if let Some(i) = SIZES.iter().position(|s| *s == sz) {
                    row.npc_ships[i] += 1;
                }
            }
            _ => *row.other_objects.entry(class.to_string()).or_default() += 1,
        }
    }
    let mut dynamic_units = 0;
    let mut dynamic_bytes = 0;
    let mut pool_units = 0;
    let mut entity_units = 0;
    for u in &idx.units {
        match u.kind {
            UnitKind::Dynamic => {
                dynamic_units += 1;
                if let Some(s) = u.sector {
                    if let Some(r) = rows.get_mut(&s) {
                        r.dynamic_units += 1;
                    }
                } else if let Some(r) = rows.get_mut(&0) {
                    r.dynamic_units += 1;
                }
                // Only count top-level dynamic units' bytes (nested ones are inside).
                if idx.units[u.parent as usize].kind != UnitKind::Dynamic {
                    dynamic_bytes += u.bytes;
                }
            }
            UnitKind::Pool => pool_units += 1,
            UnitKind::Entity => entity_units += 1,
            UnitKind::Persistent => {}
        }
    }

    let mut by_attr: HashMap<u32, (u64, bool, u64, HashMap<String, u64>)> = HashMap::new();
    let mut incoming: HashMap<String, u64> = HashMap::new();
    for r in &idx.refs {
        let label = target_label(idx, r.target);
        let e = by_attr.entry(r.attr).or_default();
        e.0 += 1;
        e.1 |= r.weak;
        if label.starts_with("unresolved") {
            e.2 += 1;
        }
        *e.3.entry(label.clone()).or_default() += 1;
        if !label.starts_with("non-pool") {
            *incoming.entry(label).or_default() += 1;
        }
    }
    let mut refs: Vec<_> = by_attr
        .into_iter()
        .map(|(a, (n, weak, unres, h))| {
            let mut h: Vec<_> = h.into_iter().collect();
            h.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));
            // Top four targets, plus every unresolved bucket.
            let mut i = 0;
            h.retain(|(l, _)| {
                i += 1;
                i <= 4 || l.starts_with("unresolved")
            });
            (idx.syms.name(a).to_string(), n, weak, unres, h)
        })
        .filter(|r| r.4.iter().any(|(l, _)| !l.starts_with("non-pool")))
        .collect();
    refs.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));
    let mut incoming_by_kind: Vec<_> = incoming.into_iter().collect();
    incoming_by_kind.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));

    let player_sector = idx.comps.values().find(|c| idx.syms.name(c.class) == "player").map(|c| {
        if c.sector == 0 {
            "(outside sectors)".to_string()
        } else {
            idx.sector_macro(c.sector).to_string()
        }
    });
    let mut sectors: Vec<SectorRow> = rows.into_values().collect();
    sectors.sort_by(|a, b| b.dynamic_components.cmp(&a.dynamic_components).then(a.macro_.cmp(&b.macro_)));
    Analysis {
        class_totals,
        object_totals,
        sectors,
        components: idx.comps.len() as u64,
        dynamic_components,
        dynamic_units,
        dynamic_bytes,
        pool_units,
        entity_units,
        refs,
        incoming_by_kind,
        player_sector,
    }
}

fn pct(a: u64, b: u64) -> f64 {
    if b == 0 {
        0.0
    } else {
        100.0 * a as f64 / b as f64
    }
}

impl Analysis {
    pub fn render(&self, idx: &SaveIndex, top: usize) -> String {
        let mut s = String::new();
        let _ = writeln!(s, "== totals");
        let _ = writeln!(
            s,
            "components: {}   sectors: {}   units: dynamic {}, script pool {}, ai entities {}",
            self.components,
            idx.sectors.len(),
            self.dynamic_units,
            self.pool_units,
            self.entity_units
        );
        let _ = writeln!(
            s,
            "dynamic share: {} of {} components ({:.1}%), {:.1} MB of {:.1} MB uncompressed XML ({:.1}%)",
            self.dynamic_components,
            self.components,
            pct(self.dynamic_components, self.components),
            self.dynamic_bytes as f64 / 1e6,
            idx.stats.bytes as f64 / 1e6,
            pct(self.dynamic_bytes, idx.stats.bytes)
        );
        let _ = writeln!(s, "player entity in sector: {}", self.player_sector.as_deref().unwrap_or("not found"));
        let j = &idx.jobs;
        let _ = writeln!(
            s,
            "jobs: enabled={} masstraffic={}; {} jobs ({} active=1, {} active=0, {} firsttime, {} spawn timers); \
             queue: {} ships ({} components), {} ship mentions, {} requests",
            j.enabled.as_deref().unwrap_or("-"),
            j.masstraffic.as_deref().unwrap_or("-"),
            j.jobs,
            j.active,
            j.inactive,
            j.firsttime,
            j.timers,
            j.queued_ships,
            j.queued_components,
            j.queued_ship_refs,
            j.requests
        );
        let mut sec: Vec<_> = idx.section_components.iter().map(|(k, v)| (idx.syms.name(*k), *v)).collect();
        sec.sort_by_key(|x| std::cmp::Reverse(x.1));
        let _ = writeln!(s, "components per section: {sec:?}");
        let _ = writeln!(s, "\n== objects by class");
        for (k, v) in &self.object_totals {
            let _ = writeln!(s, "  {k:<28} {v:>8}");
        }
        let _ = writeln!(s, "\n== all component classes");
        let mut ct: Vec<_> = self.class_totals.iter().collect();
        ct.sort_by(|a, b| b.1.cmp(a.1));
        for (k, v) in ct {
            let _ = writeln!(s, "  {k:<28} {v:>8}");
        }
        let _ = writeln!(s, "\n== per sector (sorted by dynamic components; top {top})");
        let _ = writeln!(
            s,
            "  {:<36} {:<12} {:>4} {:>5} {:>5} {:>5} {:>5} {:>5} {:>4} {:>6} {:>8} {:>8}  other",
            "sector", "owner", "stn", "xs", "s", "m", "l", "xl", "own", "dynU", "dynComp", "comps"
        );
        for r in self.sectors.iter().take(top) {
            let other: Vec<String> = r.other_objects.iter().map(|(k, v)| format!("{k}={v}")).collect();
            let _ = writeln!(
                s,
                "  {:<36} {:<12} {:>4} {:>5} {:>5} {:>5} {:>5} {:>5} {:>4} {:>6} {:>8} {:>8}  {}",
                r.macro_,
                r.owner,
                r.stations,
                r.npc_ships[0],
                r.npc_ships[1],
                r.npc_ships[2],
                r.npc_ships[3],
                r.npc_ships[4],
                r.keep_owner_ships,
                r.dynamic_units,
                r.dynamic_components,
                r.components,
                other.join(" ")
            );
        }
        if self.sectors.len() > top {
            let _ = writeln!(s, "  ... {} more sectors", self.sectors.len() - top);
        }
        let _ = writeln!(s, "\n== reference attributes (element@attribute: count, unresolved, targets)");
        for (a, n, weak, unres, h) in &self.refs {
            let hs: Vec<String> = h.iter().map(|(l, c)| format!("{l}={c}")).collect();
            let _ = writeln!(
                s,
                "  {a:<32} {n:>8} {}{}  {}",
                if *weak { "weak " } else { "" },
                if *unres > 0 { format!("unresolved={unres} ") } else { String::new() },
                hs.join(", ")
            );
        }
        let _ = writeln!(s, "\n== most referenced kinds (incoming references)");
        for (k, v) in self.incoming_by_kind.iter().take(top) {
            let _ = writeln!(s, "  {k:<36} {v:>8}");
        }
        s
    }
}
