//! Fixed-step simulation decoupled from rendering (platform-independent logic).
//!
//! The engine pairs exactly one `FrameTick` (main thread: advance time, flip the
//! render-list buffer, release semSimStart, then render) with one MoveWorker simulation
//! frame. With the dt patch every `FrameTick` advances game time by exactly `speed / hz`
//! (`hz` = 60, 30 or 20), so one simulation frame is one fixed step. The scheduler never
//! adds or removes semaphore traffic. Per step it chooses whether the **next**
//! `FrameTick` renders or takes the engine's no-render branch, and it paces steps
//! against the wall clock:
//!
//! - **Pacing.** Step `k` after the baseline may not be followed by the next `FrameTick`
//!   before `epoch + k/hz s`; the caller sleeps until then (VBI slot 13).
//! - **Catch-up.** When steps run late, renders are skipped so `FrameTick`s run back to
//!   back at simulation speed until the step clock is back on wall time.
//! - **Bounded catch-up.** A render is forced after `max_catchup_steps` consecutive
//!   skipped renders, or when `max_render_gap_ms` passed since the last render, even
//!   while behind and even with `Never` (gap `0` disables the gap rule). No-render
//!   frames can never run unboundedly long unless explicitly configured.
//! - **Spiral-of-death guard.** Lateness above `max_lag_ms` is dropped (the epoch moves
//!   forward) and reported; game time then runs slower than wall time instead of the
//!   loop trying to repay an unbounded debt.
//! - **Loading** frames reset the baseline (a load is not catch-up debt) and render.
//! - **Temporary pause** (save countdown) renders whenever the interval allows, so the
//!   engine's 3-frame countdown is shown, even while behind.
//! - **Pause / SETA** need no rule: speed only scales the game time per step; the step
//!   clock stays `hz` wall time and game time stays monotonic.

const NS_PER_S: u64 = 1_000_000_000;

/// Step rates with a known `double 1/hz` for the dt patch.
pub const SUPPORTED_HZ: [u32; 3] = [60, 30, 20];

/// Wall-clock offset of step `k` from the epoch at `hz` steps/s, exact (no drift).
pub fn step_offset_ns(k: u64, hz: u32) -> u64 {
    ((k as u128 * NS_PER_S as u128) / hz.max(1) as u128) as u64
}
pub fn step_ns(hz: u32) -> u64 {
    NS_PER_S / hz.max(1) as u64
}
pub fn fixed_dt(hz: u32) -> f64 {
    1.0 / hz.max(1) as f64
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderPolicy {
    /// Render when at least this many ms passed since the last render and the step clock
    /// is not behind. `0` renders every step that is on time (client behaviour).
    IntervalMs(u64),
    /// Do not render on schedule (headless server); only the bounds below force renders.
    Never,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SchedConfig {
    /// Fixed steps per second (one of [`SUPPORTED_HZ`]).
    pub hz: u32,
    pub render: RenderPolicy,
    /// Consecutive skipped renders after which one render is forced, behind or not.
    pub max_catchup_steps: u32,
    /// Longest wall time without a render before one is forced; 0 disables this bound.
    pub max_render_gap_ms: u64,
    /// Lateness kept as catch-up debt; anything beyond is dropped.
    pub max_lag_ms: u64,
}

impl Default for SchedConfig {
    fn default() -> Self {
        SchedConfig {
            hz: 60,
            render: RenderPolicy::IntervalMs(0),
            max_catchup_steps: 120,
            max_render_gap_ms: 5000,
            max_lag_ms: 1000,
        }
    }
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Inputs {
    /// Monotonic clock (ns), sampled after the main thread was seen parked.
    pub now_ns: u64,
    /// VBI loading flag set (or the time guard saw no advance after a load).
    pub loading: bool,
    /// TimerBase temporary pause count non-zero (save countdown).
    pub temp_paused: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Decision {
    /// Whether the next `FrameTick` renders.
    pub render: bool,
    /// Sleep on the simulation thread until this monotonic time (ns) before returning.
    pub sleep_until_ns: Option<u64>,
    /// Steps of lateness at this decision (0 when on time).
    pub lag_steps: u64,
}

/// Cumulative counters (monotonic) plus maxima the logger turns into window values.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Stats {
    pub steps: u64,
    pub renders: u64,
    pub skipped: u64,
    /// Renders forced by `max_catchup_steps`.
    pub forced_renders: u64,
    /// Renders forced by `max_render_gap_ms`.
    pub forced_gap_renders: u64,
    pub loading_steps: u64,
    pub dropped_ns: u64,
    pub rebaselines: u64,
    pub max_lag_steps: u64,
    pub max_skip_run: u64,
}

#[derive(Debug, Clone)]
pub struct Scheduler {
    cfg: SchedConfig,
    epoch_ns: Option<u64>,
    steps_since_epoch: u64,
    last_render_ns: Option<u64>,
    skip_run: u64,
    pub stats: Stats,
}

impl Scheduler {
    pub fn new(cfg: SchedConfig) -> Self {
        Scheduler {
            cfg,
            epoch_ns: None,
            steps_since_epoch: 0,
            last_render_ns: None,
            skip_run: 0,
            stats: Stats::default(),
        }
    }

    /// Consecutive skipped renders so far.
    pub fn current_skip_run(&self) -> u64 {
        self.skip_run
    }

    pub fn config(&self) -> SchedConfig {
        self.cfg
    }

    /// Forget the wall-clock baseline (next step starts a new one).
    pub fn rebaseline(&mut self) {
        if self.epoch_ns.is_some() {
            self.stats.rebaselines += 1;
        }
        self.epoch_ns = None;
        self.steps_since_epoch = 0;
    }

    /// Called once per simulation frame (VBI slot 13), i.e. once per `FrameTick`.
    pub fn step(&mut self, inp: Inputs) -> Decision {
        if inp.loading {
            self.stats.loading_steps += 1;
            self.rebaseline();
            self.skip_run = 0;
            self.last_render_ns = Some(inp.now_ns);
            return Decision { render: true, sleep_until_ns: None, lag_steps: 0 };
        }
        let hz = self.cfg.hz;
        let now = inp.now_ns;
        let epoch = *self.epoch_ns.get_or_insert(now);
        self.steps_since_epoch += 1;
        self.stats.steps += 1;
        let target = epoch.saturating_add(step_offset_ns(self.steps_since_epoch, hz));
        let mut lag_ns = now.saturating_sub(target);
        let max_lag_ns = self.cfg.max_lag_ms.saturating_mul(1_000_000);
        if lag_ns > max_lag_ns {
            let drop = lag_ns - max_lag_ns;
            self.epoch_ns = Some(epoch + drop);
            self.stats.dropped_ns += drop;
            lag_ns = max_lag_ns;
        }
        let target = self.epoch_ns.unwrap_or(epoch).saturating_add(step_offset_ns(self.steps_since_epoch, hz));
        let lag_steps = lag_ns / step_ns(hz);
        let behind = lag_steps >= 1;
        self.stats.max_lag_steps = self.stats.max_lag_steps.max(lag_steps);

        let since_render = self.last_render_ns.map(|t| now.saturating_sub(t));
        let gap_ns = self.cfg.max_render_gap_ms.saturating_mul(1_000_000);
        let gap_forced = gap_ns > 0 && since_render.is_some_and(|d| d >= gap_ns);
        let steps_forced = self.skip_run >= self.cfg.max_catchup_steps as u64;
        let scheduled = match self.cfg.render {
            RenderPolicy::Never => false,
            RenderPolicy::IntervalMs(ms) => {
                let due = since_render.is_none_or(|d| d >= ms.saturating_mul(1_000_000));
                due && (!behind || inp.temp_paused)
            }
        };
        let render = scheduled || steps_forced || gap_forced;
        if render && !scheduled {
            if steps_forced {
                self.stats.forced_renders += 1;
            } else {
                self.stats.forced_gap_renders += 1;
            }
        }
        if render {
            self.stats.renders += 1;
            self.last_render_ns = Some(now);
            self.skip_run = 0;
        } else {
            if self.last_render_ns.is_none() {
                self.last_render_ns = Some(now); // the gap counts from the first step
            }
            self.stats.skipped += 1;
            self.skip_run += 1;
            self.stats.max_skip_run = self.stats.max_skip_run.max(self.skip_run);
        }
        let sleep_until_ns = (now < target).then_some(target);
        Decision { render, sleep_until_ns, lag_steps }
    }
}

// ----------------------------------------------------------------------------------
// The slot-13 protocol, generic over the engine so the synthetic harness runs it too.

/// What the scheduler needs from the running engine (or a fake of it).
pub trait Engine {
    /// The main thread has returned from `FrameTick` and waits for semSimDone of the
    /// simulation frame that is calling us (Linux: main-loop frame-in-flight byte == 1).
    fn main_parked(&self) -> bool;
    fn now_ns(&self) -> u64;
    /// Short back-off while waiting for the main thread (render in progress).
    fn pause(&self);
    fn sleep_until(&self, ns: u64);
    /// Current value of the render decision the scheduler owns (Linux: FrameState+0x201,
    /// tested by FrameTick's second read once the decision patch is in). The guard
    /// compares it with the last write. It must never be the flag that also gates the
    /// render-list flip (FrameTick's first read): that one has to stay set every frame.
    fn render_decision(&self) -> bool;
    fn set_render_decision(&self, render: bool);
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Slot13 {
    /// Decision written (when it changed) and pacing done.
    Stepped { decision: Decision, waited_ns: u64 },
    /// The main thread stayed inside `FrameTick` for the whole timeout. Nothing was
    /// written: the next `FrameTick` keeps the previous decision.
    WaitTimeout { waited_ns: u64 },
    /// The decision byte no longer holds the value this scheduler wrote last: another
    /// writer exists. Fatal.
    ForeignFlagWrite,
}

/// One simulation frame of the scheduler, run in VBI slot 13 on the MoveWorker.
///
/// The decision is written only while the main thread is parked outside `FrameTick` (it
/// cannot enter `FrameTick` before this frame releases semSimDone), so it never changes
/// inside one `FrameTick` and the write happens-before the read through the semaphore.
/// `last_written` tracks the value this scheduler owns.
pub fn slot13_step(
    sched: &mut Scheduler,
    engine: &impl Engine,
    last_written: &mut bool,
    loading: bool,
    temp_paused: bool,
    wait_timeout_ns: u64,
) -> Slot13 {
    let start = engine.now_ns();
    while !engine.main_parked() {
        let waited = engine.now_ns().saturating_sub(start);
        if waited >= wait_timeout_ns {
            return Slot13::WaitTimeout { waited_ns: waited };
        }
        engine.pause();
    }
    let now = engine.now_ns();
    let waited_ns = now.saturating_sub(start);
    if engine.render_decision() != *last_written {
        return Slot13::ForeignFlagWrite;
    }
    let decision = sched.step(Inputs { now_ns: now, loading, temp_paused });
    if decision.render != *last_written {
        engine.set_render_decision(decision.render);
        *last_written = decision.render;
    }
    if let Some(t) = decision.sleep_until_ns {
        engine.sleep_until(t);
    }
    Slot13::Stepped { decision, waited_ns }
}

// ----------------------------------------------------------------------------------
// Time guard: proves that every simulation frame was one fixed step.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TimeSample {
    pub game: f64,
    pub real: f64,
    pub speed: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StepCheck {
    /// realTime +dt and game time + previous speed x dt.
    Exact,
    /// realTime +dt but game time moved differently (SetGameTime, speed written after
    /// slot 13). Counted, not fatal: the dt patch is still in effect.
    GameIrregular,
    /// realTime unchanged: a `FrameTick` without time advance (end of loading).
    NoAdvance,
    /// realTime moved by anything else: the fixed dt is not in effect. Fatal.
    RealTimeStep,
    /// A non-finite or out-of-range value. Fatal.
    Invalid,
}

const REAL_TOL: f64 = 1e-6;

/// Classify one simulation frame against the fixed step `dt` (1/hz).
pub fn check_step(prev: &TimeSample, cur: &TimeSample, dt: f64) -> StepCheck {
    for v in [prev.game, prev.real, prev.speed, cur.game, cur.real, cur.speed] {
        if !v.is_finite() {
            return StepCheck::Invalid;
        }
    }
    if !(0.0..=64.0).contains(&cur.speed) {
        return StepCheck::Invalid;
    }
    let dreal = cur.real - prev.real;
    if dreal == 0.0 {
        return StepCheck::NoAdvance;
    }
    if (dreal - dt).abs() > REAL_TOL {
        return StepCheck::RealTimeStep;
    }
    let dgame = cur.game - prev.game;
    if (dgame - prev.speed * dt).abs() <= REAL_TOL {
        StepCheck::Exact
    } else {
        StepCheck::GameIrregular
    }
}

// ----------------------------------------------------------------------------------
// Stall watchdog

/// Decides when the scheduler has stalled: armed, not loading, and no slot-13 callback
/// finished for `stall_ms`. Loading (sync or VBI) legitimately stops simulation frames
/// and restarts the window.
#[derive(Debug, Clone, Copy)]
pub struct StallWatch {
    stall_ns: u64,
    since_ns: Option<u64>,
    last_progress: u64,
}

impl StallWatch {
    pub fn new(stall_ms: u64) -> Self {
        StallWatch { stall_ns: stall_ms.saturating_mul(1_000_000), since_ns: None, last_progress: 0 }
    }

    /// `progress` is a counter of completed slot-13 callbacks. Returns the stalled time in
    /// ns once it reaches the limit.
    pub fn observe(&mut self, now_ns: u64, progress: u64, loading: bool) -> Option<u64> {
        if loading || progress != self.last_progress || self.since_ns.is_none() {
            self.last_progress = progress;
            self.since_ns = Some(now_ns);
            return None;
        }
        let stalled = now_ns.saturating_sub(self.since_ns.unwrap_or(now_ns));
        (stalled >= self.stall_ns).then_some(stalled)
    }
}

// ----------------------------------------------------------------------------------
// Log line

/// Values the frame callback publishes for the per-second `SCHED` line.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct SchedSnapshot {
    pub stats: Stats,
    pub hz: u32,
    pub exact_steps: u64,
    pub irregular_steps: u64,
    pub game_time: f64,
    pub speed: f64,
    /// Current run of consecutive no-render steps.
    pub skip_run: u64,
    /// Longest wait for the main thread to leave FrameTick (≈ render time), window.
    pub max_wait_ns: u64,
    pub wait_timeouts: u64,
}

pub fn sched_line(state: &str, prev: &SchedSnapshot, cur: &SchedSnapshot, wall_ms: u64) -> String {
    let wall_s = wall_ms.max(1) as f64 / 1000.0;
    let dsteps = cur.stats.steps.saturating_sub(prev.stats.steps);
    let drenders = cur.stats.renders.saturating_sub(prev.stats.renders);
    let dgame = cur.game_time - prev.game_time;
    let ratio = dgame / wall_s;
    let per_speed = if cur.speed > 0.0 { ratio / cur.speed } else { 0.0 };
    format!(
        "X4MP_AGENT SCHED state={state} hz={} sps={:.2} rps={:.2} game_wall={:.4} game_wall_per_speed={:.4} speed={:.3} max_lag_steps={} skip_run={} max_skip_run={} dropped_ms={:.1} ddropped_ms={:.1} forced_steps={} forced_gap={} rebaselines={} loading_steps={} exact={} irregular={} max_wait_ms={:.1} wait_timeouts={} steps={} renders={} wall_ms={}",
        cur.hz,
        dsteps as f64 / wall_s,
        drenders as f64 / wall_s,
        ratio,
        per_speed,
        cur.speed,
        cur.stats.max_lag_steps,
        cur.skip_run,
        cur.stats.max_skip_run,
        cur.stats.dropped_ns as f64 / 1e6,
        cur.stats.dropped_ns.saturating_sub(prev.stats.dropped_ns) as f64 / 1e6,
        cur.stats.forced_renders,
        cur.stats.forced_gap_renders,
        cur.stats.rebaselines,
        cur.stats.loading_steps,
        cur.exact_steps,
        cur.irregular_steps,
        cur.max_wait_ns as f64 / 1e6,
        cur.wait_timeouts,
        cur.stats.steps,
        cur.stats.renders,
        wall_ms,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    const MS: u64 = 1_000_000;

    fn cfg(render: RenderPolicy) -> SchedConfig {
        SchedConfig { render, max_render_gap_ms: 0, ..SchedConfig::default() }
    }
    fn at(now_ns: u64) -> Inputs {
        Inputs { now_ns, ..Default::default() }
    }
    fn off60(k: u64) -> u64 {
        step_offset_ns(k, 60)
    }

    #[test]
    fn step_offsets_are_exact_and_drift_free() {
        assert_eq!(step_offset_ns(0, 60), 0);
        assert_eq!(step_offset_ns(60, 60), NS_PER_S);
        assert_eq!(step_offset_ns(3, 60), 50 * MS);
        assert_eq!(step_offset_ns(216_000, 60), 3600 * NS_PER_S); // one hour, exact
        assert_eq!(step_offset_ns(30, 30), NS_PER_S);
        assert_eq!(step_offset_ns(108_000, 30), 3600 * NS_PER_S);
        assert_eq!(step_offset_ns(1, 20), 50 * MS);
        assert_eq!((fixed_dt(60), fixed_dt(30), fixed_dt(20)), (1.0 / 60.0, 1.0 / 30.0, 0.05));
    }

    #[test]
    fn on_time_steps_sleep_to_the_step_boundary_and_render() {
        let mut s = Scheduler::new(cfg(RenderPolicy::IntervalMs(0)));
        let d = s.step(at(1_000 * MS));
        assert!(d.render);
        assert_eq!(d.sleep_until_ns, Some(1_000 * MS + off60(1)));
        let d = s.step(at(1_000 * MS + off60(1)));
        assert_eq!((d.render, d.lag_steps), (true, 0));
        assert_eq!(d.sleep_until_ns, Some(1_000 * MS + off60(2)));
    }

    #[test]
    fn thirty_hertz_paces_at_thirty_steps_per_second() {
        let mut s = Scheduler::new(SchedConfig { hz: 30, ..cfg(RenderPolicy::IntervalMs(0)) });
        let d = s.step(at(0));
        assert_eq!(d.sleep_until_ns, Some(step_offset_ns(1, 30)));
        // A 33 ms step (the measured server cost) is on time at 30 Hz.
        let mut now = 0;
        for _ in 0..90 {
            now += 33 * MS;
            let d = s.step(at(now));
            assert_eq!(d.lag_steps, 0);
            assert!(d.render);
            now = now.max(d.sleep_until_ns.unwrap_or(now));
        }
        assert_eq!(s.stats.dropped_ns, 0);
    }

    #[test]
    fn late_steps_skip_renders_until_caught_up() {
        let mut s = Scheduler::new(cfg(RenderPolicy::IntervalMs(0)));
        s.step(at(0));
        let mut now = 500 * MS; // a 500 ms render
        let d = s.step(at(now));
        assert!(!d.render && d.sleep_until_ns.is_none() && d.lag_steps >= 28);
        let mut skipped = 1;
        loop {
            now += 2 * MS;
            let d = s.step(at(now));
            if d.render {
                break;
            }
            assert!(d.sleep_until_ns.is_none());
            skipped += 1;
            assert!(skipped < 100);
        }
        assert!((29..=33).contains(&skipped), "{skipped}");
        assert_eq!(s.stats.dropped_ns, 0);
    }

    #[test]
    fn catch_up_is_bounded_by_forced_renders() {
        let mut s = Scheduler::new(SchedConfig {
            max_catchup_steps: 5,
            max_lag_ms: 10_000,
            ..cfg(RenderPolicy::IntervalMs(0))
        });
        s.step(at(0));
        let mut now = 0;
        let mut renders = Vec::new();
        for i in 0..30 {
            now += 40 * MS; // each step slower than 60 Hz: permanently behind
            if s.step(at(now)).render {
                renders.push(i);
            }
        }
        // Step 0 is still within one step of its slot and renders; afterwards every run
        // of 5 skipped renders ends in a forced render.
        assert_eq!(renders, vec![0, 6, 12, 18, 24]);
        assert_eq!(s.stats.max_skip_run, 5);
        assert_eq!(s.stats.forced_renders, 4);
    }

    #[test]
    fn render_gap_forces_renders_while_behind_with_a_long_interval() {
        // The native failure mode: 33 ms steps at 60 Hz are permanently behind; with a
        // 1000 ms interval and 600 catch-up steps renders came only every ~20 s.
        let mut s = Scheduler::new(SchedConfig {
            render: RenderPolicy::IntervalMs(1000),
            max_catchup_steps: 600,
            max_render_gap_ms: 1500,
            max_lag_ms: 1000,
            hz: 60,
        });
        let mut now = 0;
        let mut last_render = 0u64;
        let mut max_gap = 0u64;
        for _ in 0..600 {
            now += 33 * MS;
            if s.step(at(now)).render {
                max_gap = max_gap.max(now - last_render);
                last_render = now;
            }
        }
        assert!(max_gap <= 1500 * MS + 33 * MS, "gap {} ms", max_gap / MS);
        assert!(s.stats.forced_gap_renders >= 10, "{}", s.stats.forced_gap_renders);
    }

    #[test]
    fn never_policy_still_renders_at_the_gap_bound_unless_disabled() {
        let mut s = Scheduler::new(SchedConfig { max_render_gap_ms: 1000, ..cfg(RenderPolicy::Never) });
        let mut renders = 0;
        for k in 0..600u64 {
            if s.step(at(off60(k))).render {
                renders += 1;
            }
        }
        assert_eq!(renders, 9, "10 s, first second has no prior render"); // 1 s .. 9 s
        let mut s = Scheduler::new(cfg(RenderPolicy::Never));
        for k in 0..120u64 {
            let d = s.step(at(off60(k)));
            assert!(!d.render);
            assert_eq!(d.sleep_until_ns, Some(off60(k + 1)));
        }
    }

    #[test]
    fn max_catchup_caps_consecutive_no_render_steps_in_every_policy() {
        for render in [RenderPolicy::Never, RenderPolicy::IntervalMs(60_000)] {
            let mut s = Scheduler::new(SchedConfig { max_catchup_steps: 50, ..cfg(render) });
            for k in 0..500u64 {
                s.step(at(off60(k)));
                assert!(s.current_skip_run() <= 50);
            }
            assert_eq!(s.stats.max_skip_run, 50, "{render:?}");
        }
    }

    #[test]
    fn spiral_of_death_guard_drops_excess_lag() {
        let mut s = Scheduler::new(SchedConfig { max_lag_ms: 250, ..cfg(RenderPolicy::IntervalMs(0)) });
        s.step(at(0));
        let d = s.step(at(5_000 * MS)); // a 5 s hitch (save)
        assert_eq!(d.lag_steps, 250 * MS / step_ns(60));
        let dropped = 5_000 * MS - off60(2) - 250 * MS;
        assert!(s.stats.dropped_ns.abs_diff(dropped) <= 1, "{} vs {dropped}", s.stats.dropped_ns);
        let mut now = 5_000 * MS;
        let mut n = 0;
        while !s.step(at(now)).render {
            now += MS;
            n += 1;
            assert!(n < 40);
        }
        assert!((14..=17).contains(&n), "{n}");
    }

    #[test]
    fn render_interval_limits_render_rate() {
        let mut s = Scheduler::new(cfg(RenderPolicy::IntervalMs(500)));
        let renders = (0..600u64).filter(|&k| s.step(at(off60(k))).render).count();
        assert_eq!(renders, 20);
    }

    #[test]
    fn loading_renders_and_rebaselines_without_debt() {
        let mut s = Scheduler::new(cfg(RenderPolicy::IntervalMs(0)));
        s.step(at(0));
        s.step(at(off60(1)));
        let d = s.step(Inputs { now_ns: 10 * MS, loading: true, temp_paused: false });
        assert_eq!(d, Decision { render: true, sleep_until_ns: None, lag_steps: 0 });
        let d = s.step(at(30_000 * MS));
        assert!(d.render && d.lag_steps == 0);
        assert_eq!((s.stats.dropped_ns, s.stats.rebaselines, s.stats.loading_steps), (0, 1, 1));
    }

    #[test]
    fn temp_pause_renders_even_while_behind() {
        let mut s = Scheduler::new(cfg(RenderPolicy::IntervalMs(0)));
        s.step(at(0));
        let d = s.step(Inputs { now_ns: 300 * MS, loading: false, temp_paused: true });
        assert!(d.render && d.lag_steps > 0);
        let d = s.step(at(301 * MS));
        assert!(!d.render);
    }

    #[test]
    fn step_check_classifies_time_deltas() {
        let dt = fixed_dt(60);
        let p = TimeSample { game: 100.0, real: 200.0, speed: 1.0 };
        let next = |dg: f64, dr: f64, speed: f64| TimeSample { game: 100.0 + dg, real: 200.0 + dr, speed };
        assert_eq!(check_step(&p, &next(dt, dt, 1.0), dt), StepCheck::Exact);
        assert_eq!(check_step(&p, &next(dt, dt, 6.0), dt), StepCheck::Exact); // speed changes after FrameTick
        let paused = TimeSample { speed: 0.0, ..p };
        assert_eq!(check_step(&paused, &next(0.0, dt, 0.0), dt), StepCheck::Exact);
        let seta = TimeSample { speed: 6.0, ..p };
        assert_eq!(check_step(&seta, &next(6.0 * dt, dt, 6.0), dt), StepCheck::Exact);
        assert_eq!(check_step(&p, &next(-50.0, dt, 1.0), dt), StepCheck::GameIrregular);
        assert_eq!(check_step(&p, &next(0.0, 0.0, 1.0), dt), StepCheck::NoAdvance);
        assert_eq!(check_step(&p, &next(0.02, 0.02, 1.0), dt), StepCheck::RealTimeStep);
        assert_eq!(check_step(&p, &next(dt, 2.0 * dt, 1.0), dt), StepCheck::RealTimeStep);
        assert_eq!(check_step(&p, &next(f64::NAN, dt, 1.0), dt), StepCheck::Invalid);
        assert_eq!(check_step(&p, &next(dt, dt, 99.0), dt), StepCheck::Invalid);
        // 30 Hz: a 1/60 step is a fault, 1/30 is exact.
        let dt30 = fixed_dt(30);
        assert_eq!(check_step(&p, &next(dt30, dt30, 1.0), dt30), StepCheck::Exact);
        assert_eq!(check_step(&p, &next(dt, dt, 1.0), dt30), StepCheck::RealTimeStep);
    }

    #[test]
    fn exact_steps_accumulate_to_n_steps() {
        for hz in SUPPORTED_HZ {
            let dt = fixed_dt(hz);
            let (mut game, mut real) = (1234.5_f64, 98765.0_f64);
            let mut prev = TimeSample { game, real, speed: 1.0 };
            for _ in 0..hz as u64 * 3600 {
                game += dt;
                real += dt;
                let cur = TimeSample { game, real, speed: 1.0 };
                assert_eq!(check_step(&prev, &cur, dt), StepCheck::Exact);
                prev = cur;
            }
            assert!((game - 1234.5 - 3600.0).abs() < 1e-6, "{hz}");
        }
    }

    #[test]
    fn stall_watch_fires_only_without_progress_outside_loading() {
        let mut w = StallWatch::new(10_000);
        assert_eq!(w.observe(0, 0, false), None);
        assert_eq!(w.observe(9_999 * MS, 0, false), None);
        assert_eq!(w.observe(10_000 * MS, 0, false), Some(10_000 * MS));
        // Progress restarts the window.
        let mut w = StallWatch::new(10_000);
        w.observe(0, 0, false);
        assert_eq!(w.observe(9_000 * MS, 5, false), None);
        assert_eq!(w.observe(18_000 * MS, 5, false), None);
        assert!(w.observe(19_000 * MS, 5, false).is_some());
        // Loading never counts as a stall.
        let mut w = StallWatch::new(10_000);
        w.observe(0, 7, false);
        assert_eq!(w.observe(60_000 * MS, 7, true), None);
        assert_eq!(w.observe(65_000 * MS, 7, false), None);
        assert!(w.observe(70_000 * MS, 7, false).is_some());
    }

    #[test]
    fn sched_line_reports_rates_ratio_and_skip_run() {
        let a = SchedSnapshot { game_time: 10.0, speed: 1.0, hz: 30, ..Default::default() };
        let mut b = a;
        b.stats.steps = 30;
        b.stats.renders = 2;
        b.stats.dropped_ns = 5 * MS;
        b.skip_run = 7;
        b.game_time = 11.0;
        let line = sched_line("armed", &a, &b, 1000);
        assert!(line.starts_with("X4MP_AGENT SCHED state=armed hz=30 sps=30.00 rps=2.00 game_wall=1.0000"), "{line}");
        assert!(line.contains(" skip_run=7 ") && line.contains(" ddropped_ms=5.0 "), "{line}");
    }
}
