//! ===================== EXTENSION POINT: state export to the relay =====================
//!
//! Not implemented. This crate deliberately contains no IPC or networking yet.
//!
//! [`on_frame`] runs on the X4 `Movement worker` thread inside VBI slot 13, once per
//! simulation frame that is not a loading frame, after every engine guard has passed.
//! A later state exporter for the relay plugs in here. Rules for that future code:
//!
//! - Never block, allocate unboundedly, take engine locks or do I/O on this thread.
//!   Copy what is needed into a bounded, preallocated single-producer queue and let a
//!   separate agent thread own sockets/pipes (see `agent.rs` worker thread).
//! - Only read engine state that has its own pinned signature in `pinned.rs`, and only
//!   on this thread (the universe is mutated by the MoveWorker without locks).
//! - On any inconsistency return `Err`; the caller disarms the agent (fail closed).
//! - Keep this separate from the hook-free transport (`x4lan-net`), per
//!   docs/ENGINE-INSTRUMENTATION.txt rule 2.

/// What the frame callback observed this frame.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct FrameSample {
    /// Callbacks since registration (not save-persistent; includes paused frames).
    pub frame: u64,
    /// TimerBase game time (s).
    pub game_time: f64,
    /// TimerBase real time (s), advances while paused.
    pub real_time: f64,
    /// TimerBase speed factor (0 paused, 1 normal, up to 6 SETA).
    pub speed: f64,
    /// Game-time step this frame from the MoveWorker TLS clock (CurTime - previous).
    pub tls_dt: f64,
    pub paused: bool,
    /// CLOCK_MONOTONIC at callback entry (ns).
    pub monotonic_ns: u64,
}

/// Called once per simulation frame on the MoveWorker thread. Currently a no-op.
#[inline]
pub fn on_frame(_sample: &FrameSample) -> Result<(), &'static str> {
    Ok(())
}
