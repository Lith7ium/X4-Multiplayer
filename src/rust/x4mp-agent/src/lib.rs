//! Opt-in, build-pinned Linux X4 engine agent, loaded with `LD_PRELOAD` by the dedicated
//! server supervisor. It does nothing unless `X4MP_AGENT=1` and the process is the pinned
//! X4 build; then it registers a per-simulation-frame observer in VBI slot 13. With
//! `X4MP_SCHED=fixed60` it also runs the fixed 60 Hz step scheduler (see `scheduler`).

pub mod config;
pub mod elf;
pub mod export;
pub mod logic;
pub mod pinned;
pub mod scheduler;
pub mod sha256;

#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
mod agent;
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
mod sched_live;
