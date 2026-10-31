//! `verify`: well-formedness (enforced while indexing), unique ids and reference
//! resolution.
//!
//! X4 itself saves references to ids that are not in the file: script values that still
//! hold destroyed objects (3,500+ in a fresh 9.00 start), UI listeners and temporary
//! actors. A trimmed save is therefore judged against its original (`--baseline`):
//! - a *structural* reference (anything but a typed script value) that resolved in the
//!   baseline and no longer resolves is always a failure;
//! - a *script value* that newly dangles is reported separately; it is the same state the
//!   engine writes for a destroyed object, and fails only with `--strict`.

use std::collections::{BTreeMap, HashSet};
use std::fmt::Write as _;

use crate::index::{SaveIndex, Target};
use crate::io::fmt_id;
use crate::trim::target_kind;

#[derive(Default, Debug)]
pub struct VerifyReport {
    pub root: String,
    pub components: usize,
    pub refs: usize,
    pub dup_components: usize,
    pub dup_connections: usize,
    pub dup_pool: usize,
    pub dup_entities: usize,
    /// attr -> (dangling refs, new structural, new script value)
    pub dangling_by_attr: BTreeMap<String, (u64, u64, u64)>,
    pub dangling_targets: usize,
    pub dangling_refs: u64,
    pub new_structural: u64,
    pub new_script_values: u64,
    pub examples: Vec<String>,
    pub has_baseline: bool,
    pub strict: bool,
}

impl VerifyReport {
    pub fn ok(&self) -> bool {
        let dup = self.dup_components + self.dup_connections + self.dup_pool + self.dup_entities;
        let dangling = if !self.has_baseline {
            self.dangling_refs
        } else if self.strict {
            self.new_structural + self.new_script_values
        } else {
            self.new_structural
        };
        self.root == "savegame" && dup == 0 && dangling == 0
    }

    pub fn render(&self) -> String {
        let mut s = String::new();
        let _ = writeln!(s, "root element:        <{}>", self.root);
        let _ = writeln!(s, "components:          {}", self.components);
        let _ = writeln!(s, "references:          {}", self.refs);
        let _ = writeln!(
            s,
            "duplicate ids:       components {}, connections {}, pool {}, ai entities {}",
            self.dup_components, self.dup_connections, self.dup_pool, self.dup_entities
        );
        let _ = writeln!(
            s,
            "dangling references: {} refs to {} distinct targets",
            self.dangling_refs, self.dangling_targets
        );
        if self.has_baseline {
            let _ = writeln!(
                s,
                "  vs baseline:       new structural {}, new script values {} ({}), pre-existing {}",
                self.new_structural,
                self.new_script_values,
                if self.strict { "counted: --strict" } else { "tolerated" },
                self.dangling_refs - self.new_structural - self.new_script_values
            );
        } else {
            let _ = writeln!(s, "  (no --baseline: every dangling reference counts)");
        }
        let _ =
            writeln!(s, "  {:<40} {:>8} {:>10} {:>10}", "element@attribute", "dangling", "new struct", "new script");
        for (attr, (n, ns, nv)) in &self.dangling_by_attr {
            let _ = writeln!(s, "  {attr:<40} {n:>8} {ns:>10} {nv:>10}");
        }
        for e in &self.examples {
            let _ = writeln!(s, "  e.g. {e}");
        }
        let _ = writeln!(s, "result: {}", if self.ok() { "OK" } else { "FAILED" });
        s
    }
}

/// Targets that do not resolve in `idx` (used as the baseline set).
pub fn dangling_targets(idx: &SaveIndex) -> HashSet<Target> {
    idx.refs.iter().filter(|r| !idx.is_defined(r.target)).map(|r| r.target).collect()
}

pub fn verify(idx: &SaveIndex, baseline: Option<&HashSet<Target>>, strict: bool) -> VerifyReport {
    let mut rep = VerifyReport {
        root: idx.root.clone(),
        components: idx.comps.len(),
        refs: idx.refs.len(),
        dup_components: idx.dup_components.len(),
        dup_connections: idx.dup_connections.len(),
        dup_pool: idx.dup_pool.len(),
        dup_entities: idx.dup_entities.len(),
        has_baseline: baseline.is_some(),
        strict,
        ..Default::default()
    };
    let mut targets = HashSet::new();
    for r in &idx.refs {
        if idx.is_defined(r.target) {
            continue;
        }
        targets.insert(r.target);
        let is_new = baseline.is_some_and(|b| !b.contains(&r.target));
        rep.dangling_refs += 1;
        let e = rep.dangling_by_attr.entry(idx.syms.name(r.attr).to_string()).or_default();
        e.0 += 1;
        if !is_new {
            continue;
        }
        if r.script_value {
            rep.new_script_values += 1;
            e.2 += 1;
        } else {
            rep.new_structural += 1;
            e.1 += 1;
        }
        if rep.examples.len() < 10 && (!r.script_value || strict) {
            let id = match r.target {
                Target::Component(i) | Target::Connection(i) | Target::Local(_, i) => fmt_id(i),
                Target::Pool(t, n) => format!("{}:{n}", idx.syms.name(t)),
            };
            rep.examples.push(format!(
                "new dangling {} -> {} {id} (section {}, unit {})",
                idx.syms.name(r.attr),
                target_kind(&r.target),
                idx.syms.name(r.ctx),
                r.from
            ));
        }
    }
    rep.dangling_targets = targets.len();
    rep
}
