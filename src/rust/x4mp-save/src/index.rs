//! Pass 1: stream a save once and build a compact index of components, units and
//! cross-references. Nothing of the DOM is retained; only ids, classes and edges.
//!
//! A *unit* is the granularity of keep/drop decisions:
//! - unit 0 is the persistent world (everything outside a dynamic subtree);
//! - every component of a dynamic class (see [`crate::schema::DYNAMIC_CLASSES`]) starts a
//!   `Dynamic` unit containing its whole subtree except nested dynamic subtrees;
//! - every script value-pool entry (`savegame/script/refs/ref`) is a `Pool` unit;
//! - every AI director entity (`savegame/aidirector/entity`) is an `Entity` unit that is
//!   an alias of the component with the same id (the NPC or computer it drives).

use std::collections::{HashMap, HashSet};
use std::io::BufRead;
use std::time::{Duration, Instant};

use quick_xml::events::{BytesStart, Event};
use quick_xml::Reader;

use crate::io::{parse_dec, parse_hex_id, CountingReader};
use crate::schema;

pub const PERSISTENT: u32 = 0;

#[derive(Default)]
pub struct Interner {
    map: HashMap<Box<[u8]>, u32>,
    names: Vec<String>,
}

impl Interner {
    pub fn new() -> Self {
        let mut i = Interner::default();
        i.intern(b""); // 0 = none
        i
    }
    pub fn intern(&mut self, s: &[u8]) -> u32 {
        if let Some(&v) = self.map.get(s) {
            return v;
        }
        let v = self.names.len() as u32;
        self.names.push(String::from_utf8_lossy(s).into_owned());
        self.map.insert(s.into(), v);
        v
    }
    pub fn get(&self, s: &str) -> Option<u32> {
        self.map.get(s.as_bytes()).copied()
    }
    pub fn name(&self, v: u32) -> &str {
        &self.names[v as usize]
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum UnitKind {
    Persistent,
    Dynamic,
    Pool,
    Entity,
}

#[derive(Clone, Debug)]
pub struct Unit {
    pub kind: UnitKind,
    /// Component class (Dynamic) or pool type (Pool); 0 otherwise.
    pub class: u32,
    /// Component id (Dynamic, Entity) or pool id (Pool).
    pub id: u64,
    /// Enclosing unit (for nested dynamic units, e.g. a fighter docked in a carrier).
    pub parent: u32,
    /// Enclosing sector component id, if any.
    pub sector: Option<u64>,
    /// Distinct `owner` values of components inside the unit.
    pub owners: Vec<u32>,
    /// Contains the player entity (`class="player"`).
    pub has_player: bool,
    /// Lies inside the job spawn queue (`universe/jobs/job/waiting`).
    pub job_queue: bool,
    /// Number of components inside the unit (excluding nested units).
    pub components: u32,
    /// Uncompressed bytes spanned by the unit's element (approximate for Persistent).
    pub bytes: u64,
}

#[derive(Clone, Copy, Debug)]
pub struct CompInfo {
    pub class: u32,
    pub unit: u32,
    /// Enclosing sector id (or own id for sectors); 0 = none.
    pub sector: u64,
    pub owner: u32,
    pub macro_: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Target {
    Component(u64),
    Connection(u64),
    /// (local namespace, id); see [`schema::local_ns`].
    Local(u8, u64),
    Pool(u32, u64),
}

#[derive(Clone, Copy, Debug)]
pub struct Ref {
    pub from: u32,
    /// Interned `element@attribute`.
    pub attr: u32,
    /// Weak references do not pin their target; the referencing element is pruned
    /// instead when the target is dropped (listeners, script group members).
    pub weak: bool,
    /// A typed script value (MD/AI variable, pool entry, order parameter).
    pub script_value: bool,
    /// Lies inside the job queue (`universe/jobs/job/waiting|requested`).
    pub job_queue: bool,
    /// Interned top-level section of the referencing element (`universe`, `md`, ...).
    pub ctx: u32,
    pub target: Target,
}

#[derive(Clone, Copy, Debug)]
pub struct ConnInfo {
    pub unit: u32,
    pub owner_comp: u64,
    pub owner_class: u32,
    /// Enclosing sector id; 0 when the owner is not inside a sector (cluster highways).
    pub sector: u64,
}

#[derive(Clone, Debug)]
pub struct SectorInfo {
    pub macro_: u32,
    pub owner: u32,
    pub cluster: u64,
}

#[derive(Default, Debug, Clone)]
pub struct ParseStats {
    pub bytes: u64,
    pub elements: u64,
    pub attributes: u64,
    pub elapsed: Duration,
}

pub struct SaveIndex {
    pub syms: Interner,
    pub units: Vec<Unit>,
    pub comps: HashMap<u64, CompInfo>,
    pub conns: HashMap<u64, ConnInfo>,
    /// `connected` links: (connection id of the enclosing `connection`, target connection id).
    pub links: Vec<(u64, u64)>,
    pub local_defs: HashMap<(u8, u64), Vec<u32>>,
    pub pool: HashMap<(u32, u64), u32>,
    pub pool_types: HashSet<u32>,
    pub entities: HashMap<u64, u32>,
    pub sectors: HashMap<u64, SectorInfo>,
    pub refs: Vec<Ref>,
    pub dup_components: Vec<u64>,
    pub dup_connections: Vec<u64>,
    pub dup_pool: Vec<(u32, u64)>,
    pub dup_entities: Vec<u64>,
    /// Component count per top-level section (`universe`, `md`, ...).
    pub section_components: HashMap<u32, u64>,
    pub root: String,
    pub stats: ParseStats,
    pub jobs: JobStats,
    /// For every `universe/jobs/job/waiting/ship` element in document order: the unit of
    /// the ship component it wraps, or `None` for a bare `<ship id=..>` mention.
    pub queue_ships: Vec<Option<u32>>,
    /// Components with a direct `<overrides>` child (per-space god/job/faction logic flags
    /// written by MD `set_space_*_allowed`).
    pub has_overrides: HashSet<u64>,
}

/// Runtime job state found in `universe/jobs` (job definitions live in the game's
/// `libraries/jobs.xml`; the save only holds per-job overrides and queues).
#[derive(Default, Debug, Clone)]
pub struct JobStats {
    /// `<jobs enabled=..>` (global job switch), if present.
    pub enabled: Option<String>,
    pub masstraffic: Option<String>,
    pub jobs: u64,
    pub active: u64,
    pub inactive: u64,
    pub firsttime: u64,
    pub timers: u64,
    /// Ships waiting in `job/waiting/ship` (dynamic units) and their components.
    pub queued_ships: u64,
    pub queued_components: u64,
    /// `job/waiting/ship@id` mentions of ships already in space.
    pub queued_ship_refs: u64,
    pub requests: u64,
}

#[derive(Debug)]
pub struct IndexError(pub String);

impl std::fmt::Display for IndexError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for IndexError {}

const F_UNIT: u8 = 1;
const F_SECTOR: u8 = 2;
const F_CONN: u8 = 4;
const F_COMP: u8 = 8;
const F_QUEUE: u8 = 16;
const F_JOBS: u8 = 32;
const F_QSHIP: u8 = 64;

struct Walker {
    idx: SaveIndex,
    frames: Vec<u8>,
    unit_stack: Vec<u32>,
    unit_start: Vec<u64>,
    sector_stack: Vec<u64>,
    cluster_stack: Vec<u64>,
    conn_stack: Vec<u64>,
    comp_stack: Vec<(u64, u32)>,
    section: Vec<u8>,
    section_sym: u32,
    pool_type: u32,
    in_jobs: bool,
    queue_depth: u32,
    cur_queue_ship: Option<usize>,
    attr_key: Vec<u8>,
    s_player: u32,
    s_sector: u32,
    s_cluster: u32,
    /// Interned classes treated as dynamic in addition to [`schema::DYNAMIC_CLASSES`]
    /// (`trim --drop-class`, e.g. distant stations for a sector-only client save).
    extra_dynamic: HashSet<u32>,
}

impl SaveIndex {
    pub fn unit_rep(&self, u: u32) -> u32 {
        let unit = &self.units[u as usize];
        if unit.kind == UnitKind::Entity {
            if let Some(c) = self.comps.get(&unit.id) {
                return c.unit;
            }
        }
        u
    }

    pub fn class_name(&self, sym: u32) -> &str {
        self.syms.name(sym)
    }

    /// Units a reference target resolves to (empty when unresolved).
    pub fn resolve(&self, t: Target, out: &mut Vec<u32>) {
        out.clear();
        match t {
            Target::Component(id) => {
                if let Some(c) = self.comps.get(&id) {
                    out.push(c.unit);
                } else if let Some(&e) = self.entities.get(&id) {
                    out.push(self.unit_rep(e));
                }
            }
            Target::Connection(id) => {
                if let Some(c) = self.conns.get(&id) {
                    out.push(c.unit);
                }
            }
            Target::Local(ns, id) => {
                if let Some(v) = self.local_defs.get(&(ns, id)) {
                    out.extend_from_slice(v);
                }
            }
            Target::Pool(t, n) => {
                if let Some(&u) = self.pool.get(&(t, n)) {
                    out.push(u);
                }
            }
        }
    }

    /// Whether a target is defined in this file. Pool references to types that have no
    /// pool section (e.g. `cue`) and component references to non-component ids are
    /// judged by the namespace they belong to.
    pub fn is_defined(&self, t: Target) -> bool {
        match t {
            Target::Component(id) => self.comps.contains_key(&id) || self.entities.contains_key(&id),
            Target::Connection(id) => self.conns.contains_key(&id),
            Target::Local(ns, id) => self.local_defs.contains_key(&(ns, id)),
            Target::Pool(t, n) => !self.pool_types.contains(&t) || self.pool.contains_key(&(t, n)),
        }
    }

    pub fn sector_macro(&self, id: u64) -> &str {
        self.sectors.get(&id).map(|s| self.syms.name(s.macro_)).unwrap_or("?")
    }
}

/// Builds the index from an uncompressed XML stream.
pub fn build_index<R: BufRead>(input: R) -> Result<SaveIndex, IndexError> {
    build_index_with(input, &[])
}

/// Like [`build_index`], with extra component classes that start dynamic units.
pub fn build_index_with<R: BufRead>(input: R, extra_dynamic: &[String]) -> Result<SaveIndex, IndexError> {
    let start = Instant::now();
    let mut syms = Interner::new();
    let extra_dynamic: HashSet<u32> = extra_dynamic.iter().map(|c| syms.intern(c.as_bytes())).collect();
    let s_player = syms.intern(b"player");
    let s_sector = syms.intern(b"sector");
    let s_cluster = syms.intern(b"cluster");
    let persistent = Unit {
        kind: UnitKind::Persistent,
        class: 0,
        id: 0,
        parent: 0,
        sector: None,
        owners: Vec::new(),
        has_player: false,
        job_queue: false,
        components: 0,
        bytes: 0,
    };
    let mut w = Walker {
        idx: SaveIndex {
            syms,
            units: vec![persistent],
            comps: HashMap::with_capacity(1 << 19),
            conns: HashMap::with_capacity(1 << 17),
            links: Vec::new(),
            local_defs: HashMap::new(),
            pool: HashMap::new(),
            pool_types: HashSet::new(),
            entities: HashMap::new(),
            sectors: HashMap::new(),
            refs: Vec::with_capacity(1 << 20),
            dup_components: Vec::new(),
            dup_connections: Vec::new(),
            dup_pool: Vec::new(),
            dup_entities: Vec::new(),
            section_components: HashMap::new(),
            root: String::new(),
            stats: ParseStats::default(),
            jobs: JobStats::default(),
            queue_ships: Vec::new(),
            has_overrides: HashSet::new(),
        },
        frames: Vec::with_capacity(64),
        unit_stack: vec![PERSISTENT],
        unit_start: vec![0],
        sector_stack: Vec::new(),
        cluster_stack: Vec::new(),
        conn_stack: Vec::new(),
        comp_stack: Vec::new(),
        section: Vec::new(),
        section_sym: 0,
        pool_type: 0,
        in_jobs: false,
        queue_depth: 0,
        cur_queue_ship: None,
        attr_key: Vec::with_capacity(64),
        s_player,
        s_sector,
        s_cluster,
        extra_dynamic,
    };
    let mut reader = Reader::from_reader(CountingReader { inner: input, count: 0 });
    reader.config_mut().trim_text(false);
    reader.config_mut().check_end_names = true;
    let mut buf = Vec::with_capacity(4096);
    let mut saw_root = false;
    loop {
        let pos = reader.get_ref().count;
        match reader.read_event_into(&mut buf) {
            Ok(Event::Start(e)) => {
                if w.frames.is_empty() {
                    if saw_root {
                        return Err(IndexError("more than one root element".into()));
                    }
                    saw_root = true;
                    w.idx.root = String::from_utf8_lossy(e.name().as_ref()).into_owned();
                }
                w.start(&e, pos)?;
            }
            Ok(Event::Empty(e)) => {
                if w.frames.is_empty() {
                    return Err(IndexError("empty root element".into()));
                }
                w.start(&e, pos)?;
                w.end(reader.get_ref().count);
            }
            Ok(Event::End(_)) => w.end(reader.get_ref().count),
            Ok(Event::Eof) => break,
            Ok(Event::Text(t)) => {
                if w.frames.is_empty() && t.iter().any(|c| !c.is_ascii_whitespace()) {
                    return Err(IndexError("text outside the root element".into()));
                }
            }
            Ok(_) => {}
            Err(e) => {
                return Err(IndexError(format!("XML error at byte {}: {e}", reader.buffer_position())));
            }
        }
        buf.clear();
    }
    if !w.frames.is_empty() {
        return Err(IndexError(format!("unexpected end of file: {} unclosed elements", w.frames.len())));
    }
    if !saw_root {
        return Err(IndexError("no root element".into()));
    }
    let total = reader.get_ref().count;
    w.idx.units[0].bytes = total;
    w.idx.stats.bytes = total;
    w.idx.stats.elapsed = start.elapsed();
    Ok(w.idx)
}

impl Walker {
    fn cur_unit(&self) -> u32 {
        *self.unit_stack.last().unwrap()
    }

    fn push_unit(&mut self, unit: Unit, pos: u64) -> u32 {
        let u = self.idx.units.len() as u32;
        self.idx.units.push(unit);
        self.unit_stack.push(u);
        self.unit_start.push(pos);
        u
    }

    fn add_ref(&mut self, elem: &[u8], attr: &[u8], weak: bool, script_value: bool, target: Target) {
        self.attr_key.clear();
        self.attr_key.extend_from_slice(elem);
        self.attr_key.push(b'@');
        self.attr_key.extend_from_slice(attr);
        let attr = self.idx.syms.intern(&self.attr_key);
        let from = self.cur_unit();
        let ctx = self.section_sym;
        let job_queue = self.queue_depth > 0;
        self.idx.refs.push(Ref { from, attr, weak, script_value, ctx, job_queue, target });
    }

    fn start(&mut self, e: &BytesStart, pos: u64) -> Result<(), IndexError> {
        self.idx.stats.elements += 1;
        let depth = self.frames.len();
        let name = e.name();
        let name = name.as_ref();
        if depth == 1 {
            self.section.clear();
            self.section.extend_from_slice(name);
            self.section_sym = self.idx.syms.intern(name);
        }
        let mut flags = 0u8;

        // Collect attributes (borrowed when possible).
        let mut attrs: Vec<(&[u8], std::borrow::Cow<[u8]>)> = Vec::with_capacity(8);
        for a in e.attributes() {
            let a =
                a.map_err(|err| IndexError(format!("bad attribute in <{}>: {err}", String::from_utf8_lossy(name))))?;
            attrs.push((a.key.into_inner(), a.value));
        }
        self.idx.stats.attributes += attrs.len() as u64;
        let get = |k: &[u8]| attrs.iter().find(|(key, _)| *key == k).map(|(_, v)| v.as_ref());

        let in_script = self.section == b"script";
        let in_aidirector = self.section == b"aidirector";
        if depth == 2 && name == b"jobs" && self.section == b"universe" {
            self.in_jobs = true;
            flags |= F_JOBS;
            self.idx.jobs.enabled = get(b"enabled").map(|v| String::from_utf8_lossy(v).into_owned());
            self.idx.jobs.masstraffic = get(b"masstraffic").map(|v| String::from_utf8_lossy(v).into_owned());
        } else if self.in_jobs && depth == 3 && name == b"job" {
            let j = &mut self.idx.jobs;
            j.jobs += 1;
            match get(b"active") {
                Some(b"1") => j.active += 1,
                Some(_) => j.inactive += 1,
                None => {}
            }
            if get(b"firsttime").is_some() {
                j.firsttime += 1;
            }
            if get(b"time").is_some() {
                j.timers += 1;
            }
        } else if self.in_jobs && depth == 4 && (name == b"waiting" || name == b"requested") {
            self.queue_depth += 1;
            flags |= F_QUEUE;
        } else if self.queue_depth > 0 && name == b"request" {
            self.idx.jobs.requests += 1;
        } else if self.queue_depth > 0 && depth == 5 && name == b"ship" {
            if get(b"id").is_some() {
                self.idx.jobs.queued_ship_refs += 1;
            }
            self.idx.queue_ships.push(None);
            self.cur_queue_ship = Some(self.idx.queue_ships.len() - 1);
            flags |= F_QSHIP;
        }

        if name == b"component" {
            let class_b = get(b"class").unwrap_or(b"");
            let class = self.idx.syms.intern(class_b);
            let class_str = std::str::from_utf8(class_b).unwrap_or("");
            let id = get(b"id").and_then(parse_hex_id).unwrap_or(0);
            if schema::is_dynamic_class(class_str) || self.extra_dynamic.contains(&class) {
                let unit = Unit {
                    kind: UnitKind::Dynamic,
                    class,
                    id,
                    parent: self.cur_unit(),
                    sector: self.sector_stack.last().copied(),
                    owners: Vec::new(),
                    has_player: false,
                    job_queue: false,
                    components: 0,
                    bytes: 0,
                };
                self.push_unit(unit, pos);
                flags |= F_UNIT;
            }
            let unit = self.cur_unit();
            if self.queue_depth > 0 {
                self.idx.jobs.queued_components += 1;
                self.idx.units[unit as usize].job_queue = true;
                if flags & F_UNIT != 0 {
                    self.idx.jobs.queued_ships += 1;
                    if let Some(i) = self.cur_queue_ship {
                        if self.idx.queue_ships[i].is_none() {
                            self.idx.queue_ships[i] = Some(unit);
                        }
                    }
                }
            }
            let owner = get(b"owner").map(|o| self.idx.syms.intern(o)).unwrap_or(0);
            let macro_ = get(b"macro").map(|m| self.idx.syms.intern(m)).unwrap_or(0);
            {
                let u = &mut self.idx.units[unit as usize];
                u.components += 1;
                if owner != 0 && !u.owners.contains(&owner) {
                    u.owners.push(owner);
                }
                if class == self.s_player {
                    u.has_player = true;
                }
            }
            if class == self.s_cluster {
                self.cluster_stack.push(id);
            }
            if class == self.s_sector {
                self.sector_stack.push(id);
                flags |= F_SECTOR;
                let cluster = self.cluster_stack.last().copied().unwrap_or(0);
                self.idx.sectors.insert(id, SectorInfo { macro_, owner, cluster });
            }
            let sector = self.sector_stack.last().copied().unwrap_or(0);
            let sect = self.idx.syms.intern(&self.section);
            *self.idx.section_components.entry(sect).or_default() += 1;
            if id != 0 {
                let info = CompInfo { class, unit, sector, owner, macro_ };
                if self.idx.comps.insert(id, info).is_some() {
                    self.idx.dup_components.push(id);
                }
            }
            self.comp_stack.push((id, class));
            flags |= F_COMP;
        } else if name == b"overrides" {
            // direct child of the enclosing component (frames: component -> overrides)
            if let (Some(&(id, _)), Some(&f)) = (self.comp_stack.last(), self.frames.last()) {
                if f & F_COMP != 0 {
                    self.idx.has_overrides.insert(id);
                }
            }
        } else if name == b"connection" {
            if let Some(id) = get(b"id").and_then(parse_hex_id) {
                let (owner_comp, owner_class) = self.comp_stack.last().copied().unwrap_or((0, 0));
                let info = ConnInfo {
                    unit: self.cur_unit(),
                    owner_comp,
                    owner_class,
                    sector: self.sector_stack.last().copied().unwrap_or(0),
                };
                if self.idx.conns.insert(id, info).is_some() {
                    self.idx.dup_connections.push(id);
                }
                self.conn_stack.push(id);
                flags |= F_CONN;
            }
        } else if in_aidirector && depth == 2 && name == b"entity" {
            if let Some(id) = get(b"id").and_then(parse_hex_id) {
                let unit = Unit {
                    kind: UnitKind::Entity,
                    class: 0,
                    id,
                    parent: PERSISTENT,
                    sector: None,
                    owners: Vec::new(),
                    has_player: false,
                    job_queue: false,
                    components: 0,
                    bytes: 0,
                };
                let u = self.push_unit(unit, pos);
                flags |= F_UNIT;
                if self.idx.entities.insert(id, u).is_some() {
                    self.idx.dup_entities.push(id);
                }
            }
        } else if in_script && depth == 2 && name == b"refs" {
            self.pool_type = get(b"type").map(|t| self.idx.syms.intern(t)).unwrap_or(0);
            self.idx.pool_types.insert(self.pool_type);
        } else if in_script && depth == 3 && name == b"ref" {
            if let Some(id) = get(b"id").and_then(parse_dec) {
                let unit = Unit {
                    kind: UnitKind::Pool,
                    class: self.pool_type,
                    id,
                    parent: PERSISTENT,
                    sector: None,
                    owners: Vec::new(),
                    has_player: false,
                    job_queue: false,
                    components: 0,
                    bytes: 0,
                };
                let u = self.push_unit(unit, pos);
                flags |= F_UNIT;
                if self.idx.pool.insert((self.pool_type, id), u).is_some() {
                    self.idx.dup_pool.push((self.pool_type, id));
                }
            }
        }

        // Generic reference extraction.
        let has_type = get(b"type").is_some();
        let mut ty: Option<&[u8]> = None;
        let mut val: Option<&[u8]> = None;
        for (k, v) in &attrs {
            let k: &[u8] = k;
            let v: &[u8] = v.as_ref();
            if k == b"type" {
                ty = Some(v);
            } else if k == b"value" {
                val = Some(v);
            }
            let Some(id) = parse_hex_id(v) else { continue };
            if k == b"id" {
                if name == b"component" || name == b"connection" || (in_aidirector && name == b"entity") {
                    continue;
                }
                if schema::is_local_def(name) {
                    let unit = self.cur_unit();
                    let v = self.idx.local_defs.entry((schema::local_ns(name), id)).or_default();
                    if v.last() != Some(&unit) {
                        v.push(unit);
                    }
                    continue;
                }
                // `<member id>`, `<ship id>`, `<sector id>`, `<object id>`, `<target id>`,
                // `<filtered id>`, `<actor id>`: mentions of a component.
                let weak = in_script && name == b"member";
                let sv = schema::is_script_value(name, k, has_type);
                self.add_ref(name, k, weak, sv, Target::Component(id));
            } else if name == b"connected" && k == b"connection" {
                if let Some(&from) = self.conn_stack.last() {
                    self.idx.links.push((from, id));
                }
                self.add_ref(name, k, false, false, Target::Connection(id));
            } else if let Some(ns) = schema::local_ref_ns(name, k) {
                self.add_ref(name, k, false, false, Target::Local(ns, id));
            } else if k == b"listener" {
                self.add_ref(name, k, true, false, Target::Component(id));
            } else {
                let sv = schema::is_script_value(name, k, has_type);
                self.add_ref(name, k, false, sv, Target::Component(id));
            }
        }
        if let (Some(t), Some(v)) = (ty, val) {
            if !schema::is_scalar_type(t) {
                if let Some(n) = parse_dec(v) {
                    let t = self.idx.syms.intern(t);
                    self.add_ref(name, b"value", false, false, Target::Pool(t, n));
                }
            }
        }
        self.frames.push(flags);
        Ok(())
    }

    fn end(&mut self, pos: u64) {
        let flags = self.frames.pop().unwrap_or(0);
        if flags & F_COMP != 0 {
            if let Some((id, class)) = self.comp_stack.pop() {
                if class == self.s_cluster && self.cluster_stack.last() == Some(&id) {
                    self.cluster_stack.pop();
                }
            }
        }
        if flags & F_SECTOR != 0 {
            self.sector_stack.pop();
        }
        if flags & F_CONN != 0 {
            self.conn_stack.pop();
        }
        if flags & F_QUEUE != 0 {
            self.queue_depth -= 1;
        }
        if flags & F_JOBS != 0 {
            self.in_jobs = false;
        }
        if flags & F_QSHIP != 0 {
            self.cur_queue_ship = None;
        }
        if flags & F_UNIT != 0 {
            let u = self.unit_stack.pop().unwrap();
            let s = self.unit_start.pop().unwrap();
            self.idx.units[u as usize].bytes = pos.saturating_sub(s);
        }
        if self.frames.len() <= 1 {
            self.section.clear();
        }
    }
}
