//! Classification rules for the X4 savegame format.
//!
//! These tables were derived from a full scan of a 9.00 (build 611726) save; see
//! `README.md` for the measured counts behind each rule. Everything that is not listed
//! here falls back to the conservative default: a `[0x..]` attribute value is a strong
//! reference to a component and pins its target.

/// Component classes that form a *dynamic* unit: the subtree rooted at such a component
/// can be dropped as a whole from a distant sector and re-streamed by the server later.
/// Everything else (stations, modules, build storages, gates, highways, zones, sectors,
/// regions, lockboxes, data vaults, anomalies, ...) is persistent and always kept.
pub const DYNAMIC_CLASSES: &[&str] = &[
    // ships of every size, including drones and the player's spacesuit
    "ship_xs",
    "ship_s",
    "ship_m",
    "ship_l",
    "ship_xl",
    "spacesuit",
    // projectiles and deployables
    "missile",
    "bullet",
    "mine",
    "bomb",
    "countermeasure",
    "satellite",
    "navbeacon",
    "resourceprobe",
    // drops, containers and wreck pieces
    "collectablewares",
    "collectableblueprints",
    "collectableammo",
    "collectableshieldrestore",
    "recyclable",
    // minable rocks spawned by resource regions (regions themselves are persistent)
    "asteroid",
];

/// Classes counted as "objects" in the analysis (things placed in space or docked).
/// Sub-parts (turrets, shields, cargo bays, NPCs, rooms, ...) appear only in totals.
pub const OBJECT_CLASSES: &[&str] = &[
    "station",
    "buildstorage",
    "ship_xs",
    "ship_s",
    "ship_m",
    "ship_l",
    "ship_xl",
    "spacesuit",
    "gate",
    "highway",
    "highwayentrygate",
    "highwayexitgate",
    "asteroid",
    "recyclable",
    "lockbox",
    "collectablewares",
    "collectableblueprints",
    "collectableammo",
    "datavault",
    "signalleak",
    "anomaly",
    "object",
    "adsignobject",
    "satellite",
    "navbeacon",
    "resourceprobe",
    "mine",
    "missile",
    "bullet",
    "countermeasure",
    "checkpoint",
];

/// Component classes whose `connected` links define travel between sectors (neighbours).
pub const TRAVEL_CLASSES: &[&str] = &["gate", "highway", "highwayentrygate", "highwayexitgate"];

/// Elements whose `id` attribute *defines* an id in the local (non-component) namespace:
/// orders, trade offers, construction-sequence entries, resource areas, mass-traffic
/// networks, quotas, filters, build tasks, accounts and the weapon data mirror. These
/// ids collide numerically with component and connection ids, so they live apart.
pub const LOCAL_DEF_ELEMENTS: &[&str] = &[
    "entry",
    "weapon",
    "order",
    "trade",
    "area",
    "network",
    "quota",
    "quotalist",
    "filter",
    "build",
    "reservation",
    "account",
];

/// Local sub-namespaces. Ids of different local element kinds collide (a ship's order
/// `[0x2470]` and a station's construction entry `[0x2470]` are unrelated), so each
/// referenced kind is resolved separately; 0 groups the kinds nothing refers to.
pub fn local_ns(elem: &[u8]) -> u8 {
    match elem {
        b"entry" => 1,
        b"order" => 2,
        b"build" => 3,
        b"network" => 4,
        _ => 0,
    }
}

/// `(element, attribute, local namespace)` triples that reference a local id.
pub const LOCAL_REF_ATTRS: &[(&str, &str, u8)] = &[
    ("component", "construction", 1),
    ("script", "order", 2),
    ("buildtasks", "build", 3),
    ("build", "order", 3),
    ("event", "network", 4),
];

/// Typed script values (`<value type=.. value=..>` in MD cues, AI-script vars, the
/// script value pool and order parameters). The engine itself saves such values
/// pointing at components that no longer exist (destroyed objects), so a dangling
/// component id here is a state X4 already loads; see README "tolerated dangling".
pub const SCRIPT_VALUE_ELEMENTS: &[&str] = &["value", "key", "param", "param2", "param3"];

/// Script value types that are plain scalars; their decimal `value` is never a pool id.
pub const SCALAR_TYPES: &[&str] = &[
    "integer",
    "largeinteger",
    "float",
    "time",
    "money",
    "length",
    "angle",
    "hitpoints",
    "boolean",
    "position",
    "rotation_scalar",
    "linear",
    "glow",
    "cue",
];

pub fn is_dynamic_class(class: &str) -> bool {
    DYNAMIC_CLASSES.contains(&class)
}

pub fn is_object_class(class: &str) -> bool {
    OBJECT_CLASSES.contains(&class)
}

pub fn is_travel_class(class: &str) -> bool {
    TRAVEL_CLASSES.contains(&class)
}

pub fn is_local_def(elem: &[u8]) -> bool {
    LOCAL_DEF_ELEMENTS.iter().any(|e| e.as_bytes() == elem)
}

pub fn local_ref_ns(elem: &[u8], attr: &[u8]) -> Option<u8> {
    LOCAL_REF_ATTRS.iter().find(|(e, a, _)| e.as_bytes() == elem && a.as_bytes() == attr).map(|t| t.2)
}

/// Whether a component reference from `elem@attr` is a script value (see
/// [`SCRIPT_VALUE_ELEMENTS`]); `filtered@id` (mission/station filter lists) behaves alike.
pub fn is_script_value(elem: &[u8], attr: &[u8], has_type: bool) -> bool {
    (has_type && attr == b"value" && SCRIPT_VALUE_ELEMENTS.iter().any(|e| e.as_bytes() == elem))
        || (elem == b"filtered" && attr == b"id")
}

pub fn is_scalar_type(t: &[u8]) -> bool {
    SCALAR_TYPES.iter().any(|s| s.as_bytes() == t)
}

/// Ship size label used in reports (`ship_m` -> `m`).
pub fn ship_size(class: &str) -> Option<&str> {
    class.strip_prefix("ship_")
}
