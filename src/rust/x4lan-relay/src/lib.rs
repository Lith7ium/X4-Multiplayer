//! Rust port of the x4lan Python UDP relay: host.py's two-slot session authority and
//! the layered bubble/economy/coop/dock/universe sessions built on it.
//!
//! The wire protocol (bounded ASCII version-1 envelope, 768 bytes) is unchanged; the
//! differential harness in `src/rust/parity/` replays identical traffic into this crate
//! and the Python relay and requires byte-identical replies and snapshots.

pub mod dedicated;
pub mod json;
pub mod ordmap;
pub mod players;
pub mod pynum;
pub mod relay;
pub mod rng;
pub mod session;
pub mod wire;
