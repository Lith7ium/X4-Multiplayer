//! Platform-independent decisions: when registration is allowed, disarm reasons and the
//! cadence log format. Kept separate from the unsafe engine code so it is unit-tested.

/// One poll of the engine state made by the agent's worker thread before registration.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Observation {
    pub now_ms: u64,
    /// `*(VBI_TABLE_GLOBAL)` is non-null and the whole table is readable.
    pub table_ready: bool,
    pub loading: bool,
    /// Agent slot and its loading-bank entry are both null.
    pub slot_free: bool,
    /// Slot 3 holds an object whose vtable is the pinned `X4::GameClient` vtable.
    pub game_client_ok: bool,
    /// A thread named `Movement worker` exists in this process.
    pub mover_thread: bool,
    pub game_time: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Gate {
    Wait(&'static str),
    Arm,
    /// Something is in the slot this agent wants: never overwrite it.
    Refuse(&'static str),
}

/// Registration is deferred until the game is out of loading and has been simulating
/// (advancing game time) with every precondition true for `settle_ms`.
#[derive(Debug, Clone)]
pub struct ArmGate {
    settle_ms: u64,
    since: Option<(u64, f64)>,
}

impl ArmGate {
    pub fn new(settle_ms: u64) -> Self {
        ArmGate { settle_ms, since: None }
    }

    pub fn observe(&mut self, o: &Observation) -> Gate {
        let blocker = if !o.table_ready {
            Some("vbi_table_unavailable")
        } else if !o.mover_thread {
            Some("movement_worker_absent")
        } else if !o.game_client_ok {
            Some("game_client_unavailable")
        } else if o.loading {
            Some("loading")
        } else if !o.game_time.is_finite() || o.game_time < 0.0 {
            Some("game_time_invalid")
        } else {
            None
        };
        if let Some(reason) = blocker {
            self.since = None;
            return Gate::Wait(reason);
        }
        if !o.slot_free {
            self.since = None;
            return Gate::Refuse("agent_slot_occupied");
        }
        match self.since {
            None => {
                self.since = Some((o.now_ms, o.game_time));
                Gate::Wait("settling")
            }
            Some((t0, g0)) => {
                if o.now_ms.saturating_sub(t0) < self.settle_ms {
                    Gate::Wait("settling")
                } else if o.game_time <= g0 {
                    // Out of loading but not simulating (or paused): keep waiting, but
                    // restart the window so the advance is observed within settle_ms.
                    self.since = Some((o.now_ms, o.game_time));
                    Gate::Wait("game_time_not_advancing")
                } else {
                    Gate::Arm
                }
            }
        }
    }
}

/// Why the frame callback disarmed itself (stored as an atomic code on the hot path).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Disarm {
    None = 0,
    ForeignThread = 1,
    NotMovementWorker = 2,
    Reentrant = 3,
    TableChanged = 4,
    SlotChanged = 5,
    GameClientChanged = 6,
    Panic = 7,
    ExportFailed = 8,
    DisarmFile = 9,
    InvalidTime = 10,
    SchedulerGuard = 11,
}

impl Disarm {
    pub fn from_code(code: u32) -> Disarm {
        use Disarm::*;
        [
            None,
            ForeignThread,
            NotMovementWorker,
            Reentrant,
            TableChanged,
            SlotChanged,
            GameClientChanged,
            Panic,
            ExportFailed,
            DisarmFile,
            InvalidTime,
            SchedulerGuard,
        ]
        .get(code as usize)
        .copied()
        .unwrap_or(Panic)
    }

    pub fn reason(self) -> &'static str {
        match self {
            Disarm::None => "none",
            Disarm::ForeignThread => "callback_on_foreign_thread",
            Disarm::NotMovementWorker => "first_callback_not_on_movement_worker",
            Disarm::Reentrant => "reentrant_callback",
            Disarm::TableChanged => "vbi_table_pointer_changed",
            Disarm::SlotChanged => "agent_slot_no_longer_owned",
            Disarm::GameClientChanged => "game_client_slot_changed",
            Disarm::Panic => "callback_panicked",
            Disarm::ExportFailed => "export_extension_failed",
            Disarm::DisarmFile => "disarm_file_present",
            Disarm::InvalidTime => "timer_values_invalid",
            Disarm::SchedulerGuard => "scheduler_guard",
        }
    }
}

/// Counters published by the frame callback and read by the worker thread.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Snapshot {
    pub frames: u64,
    pub loading_skips: u64,
    pub paused_frames: u64,
    pub game_time: f64,
    pub real_time: f64,
    pub speed: f64,
    pub tls_dt: f64,
    pub paused: bool,
    /// Largest wall-clock gap between consecutive callbacks since the previous line (ns).
    pub max_gap_ns: u64,
    pub monotonic_ns: u64,
}

/// `CADENCE` line from two snapshots taken `wall_ms` apart (wall = worker clock).
pub fn cadence_line(prev: &Snapshot, cur: &Snapshot, wall_ms: u64) -> String {
    let frames = cur.frames.saturating_sub(prev.frames);
    let fps = if wall_ms == 0 { 0.0 } else { frames as f64 * 1000.0 / wall_ms as f64 };
    format!(
        "X4MP_AGENT CADENCE frames={} dframes={} fps={:.2} game={:.6} real={:.6} dgame={:.6} dreal={:.6} speed={:.3} paused={} tls_dt={:.6} loading_skips={} dloading={} paused_frames={} max_gap_ms={:.3} wall_ms={}",
        cur.frames,
        frames,
        fps,
        cur.game_time,
        cur.real_time,
        cur.game_time - prev.game_time,
        cur.real_time - prev.real_time,
        cur.speed,
        u8::from(cur.paused),
        cur.tls_dt,
        cur.loading_skips,
        cur.loading_skips.saturating_sub(prev.loading_skips),
        cur.paused_frames,
        cur.max_gap_ns as f64 / 1e6,
        wall_ms,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ready(now_ms: u64, game_time: f64) -> Observation {
        Observation {
            now_ms,
            table_ready: true,
            loading: false,
            slot_free: true,
            game_client_ok: true,
            mover_thread: true,
            game_time,
        }
    }

    #[test]
    fn arms_only_after_settled_simulation() {
        let mut gate = ArmGate::new(2000);
        assert_eq!(gate.observe(&Observation::default()), Gate::Wait("vbi_table_unavailable"));
        assert_eq!(gate.observe(&ready(0, 1.0)), Gate::Wait("settling"));
        assert_eq!(gate.observe(&ready(1999, 2.0)), Gate::Wait("settling"));
        assert_eq!(gate.observe(&ready(2000, 3.0)), Gate::Arm);
    }

    #[test]
    fn loading_restarts_the_window() {
        let mut gate = ArmGate::new(1000);
        gate.observe(&ready(0, 1.0));
        let mut loading = ready(900, 1.5);
        loading.loading = true;
        assert_eq!(gate.observe(&loading), Gate::Wait("loading"));
        assert_eq!(gate.observe(&ready(1100, 2.0)), Gate::Wait("settling"));
        assert_eq!(gate.observe(&ready(2000, 2.5)), Gate::Wait("settling"));
        assert_eq!(gate.observe(&ready(2100, 3.0)), Gate::Arm);
    }

    #[test]
    fn frozen_game_time_never_arms() {
        let mut gate = ArmGate::new(500);
        gate.observe(&ready(0, 7.0));
        assert_eq!(gate.observe(&ready(600, 7.0)), Gate::Wait("game_time_not_advancing"));
        assert_eq!(gate.observe(&ready(1200, 7.0)), Gate::Wait("game_time_not_advancing"));
        assert_eq!(gate.observe(&ready(1800, 7.1)), Gate::Arm);
    }

    #[test]
    fn occupied_slot_refuses_and_missing_prerequisites_wait() {
        let mut gate = ArmGate::new(500);
        let mut o = ready(0, 1.0);
        o.slot_free = false;
        assert_eq!(gate.observe(&o), Gate::Refuse("agent_slot_occupied"));
        let mut o = ready(0, 1.0);
        o.mover_thread = false;
        assert_eq!(gate.observe(&o), Gate::Wait("movement_worker_absent"));
        let mut o = ready(0, 1.0);
        o.game_client_ok = false;
        assert_eq!(gate.observe(&o), Gate::Wait("game_client_unavailable"));
        assert_eq!(gate.observe(&ready(0, f64::NAN)), Gate::Wait("game_time_invalid"));
    }

    #[test]
    fn disarm_codes_round_trip() {
        for code in 0..=11 {
            assert_eq!(Disarm::from_code(code) as u32, code);
        }
        assert_eq!(Disarm::from_code(99), Disarm::Panic);
    }

    #[test]
    fn cadence_line_reports_deltas() {
        let a = Snapshot { frames: 100, loading_skips: 3, game_time: 10.0, real_time: 20.0, ..Default::default() };
        let b = Snapshot {
            frames: 130,
            loading_skips: 3,
            game_time: 11.0,
            real_time: 21.0,
            speed: 1.0,
            max_gap_ns: 40_000_000,
            ..Default::default()
        };
        let line = cadence_line(&a, &b, 1000);
        assert!(line.starts_with("X4MP_AGENT CADENCE frames=130 dframes=30 fps=30.00 game=11.000000"), "{line}");
        assert!(line.contains(" dgame=1.000000 dreal=1.000000 speed=1.000 paused=0 "), "{line}");
        assert!(line.contains(" dloading=0 ") && line.contains(" max_gap_ms=40.000 wall_ms=1000"), "{line}");
    }
}
