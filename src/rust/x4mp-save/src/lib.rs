//! Offline X4 savegame tooling for hot join (design section 7b, items 7 and 8):
//! streaming analysis, per-client trimming by reference closure, and verification.
//!
//! Saves are streamed twice (index, then filtered copy); no DOM is built.

pub mod analyze;
pub mod index;
pub mod io;
pub mod schema;
pub mod trim;
pub mod verify;

/// Peak resident set size in bytes, when the OS exposes it (Linux `VmHWM`).
pub fn peak_rss_bytes() -> Option<u64> {
    let s = std::fs::read_to_string("/proc/self/status").ok()?;
    let line = s.lines().find(|l| l.starts_with("VmHWM:"))?;
    let kb: u64 = line.split_whitespace().nth(1)?.parse().ok()?;
    Some(kb * 1024)
}
