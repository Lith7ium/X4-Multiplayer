//! In-process side of the fixed-step scheduler (Linux only). Runs inside the agent's
//! VBI slot-13 callback on the MoveWorker thread; the worker thread logs, runs the stall
//! watchdog and can restore.
//!
//! Engine writes, all verified before and restored on every disarm path:
//! 1. Code, three naturally aligned qwords of FrameTick, each replaced and restored with
//!    one 8-byte compare-exchange (every intermediate state is a valid FrameTick):
//!    - [`pinned::DECISION_CMP_VA`]: FrameTick's second read (render or no-render branch)
//!      tests FrameState+0x201 (padding the scheduler owns) instead of the engine's
//!      render flag +0x200. The first read, which gates the render-list flip
//!      (0x218e2c0) and 0x224ceb0, keeps testing +0x200 = 1, so the flip runs on every
//!      frame, rendered or not. (Clearing +0x200 itself starved the flip and froze the
//!      native server after ~1200 unrendered frames, 25 Oct 2026.)
//!    - [`pinned::DT_LOAD_VA`] / [`pinned::DT_MIN_VA`]: dt = 1/hz exactly.
//! 2. Data: the decision byte FrameState+0x201 (written in slot 13 while the main thread
//!    is parked outside FrameTick; unread once the compare is restored).
//! 3. fixed30 only: one anonymous read-only page of the agent's own holding `1/30`
//!    (the image has no such constant), mapped below the image within rip reach.
//!
//! No semaphore is touched and no engine function is called.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicU8, Ordering};
use std::sync::{Mutex, OnceLock, TryLockError};
use std::time::{Duration, Instant};

use crate::agent::{covered, epoch_ns, read_f64, read_i32, read_u64, readable_ranges, LiveImage};
use crate::config::Sched;
use crate::pinned;
use crate::scheduler::{
    check_step, fixed_dt, slot13_step, Engine, SchedSnapshot, Scheduler, Slot13, StallWatch, StepCheck, TimeSample,
};

// ----------------------------------------------------------------------------------
// Phases, faults and published statistics

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Phase {
    Off = 0,
    Pending = 1,
    Active = 2,
    Refused = 3,
    Faulted = 4,
    Restored = 5,
}

impl Phase {
    fn from_code(code: u32) -> Phase {
        [Phase::Off, Phase::Pending, Phase::Active, Phase::Refused, Phase::Faulted, Phase::Restored]
            .get(code as usize)
            .copied()
            .unwrap_or(Phase::Faulted)
    }
    pub fn name(self) -> &'static str {
        match self {
            Phase::Off => "off",
            Phase::Pending => "pending",
            Phase::Active => "armed",
            Phase::Refused => "refused",
            Phase::Faulted => "faulted",
            Phase::Restored => "restored",
        }
    }
}

/// Why admission was refused or the active scheduler faulted (first reason wins).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum Fault {
    None = 0,
    FrameStateUnavailable,
    RenderFlagNotInitial,
    MovieCaptureActive,
    CodeBytesNotOriginal,
    ConstantUnavailable,
    LiveSiteMismatch,
    CodePageNotRx,
    MprotectFailed,
    PatchRace,
    FrameStateChanged,
    PatchBytesChanged,
    RealTimeStep,
    TimeInvalid,
    ForeignDecisionWrite,
    StatePoisoned,
    Stall,
    RenderFlagChanged,
}

impl Fault {
    fn from_code(code: u32) -> Fault {
        use Fault::*;
        [
            None,
            FrameStateUnavailable,
            RenderFlagNotInitial,
            MovieCaptureActive,
            CodeBytesNotOriginal,
            ConstantUnavailable,
            LiveSiteMismatch,
            CodePageNotRx,
            MprotectFailed,
            PatchRace,
            FrameStateChanged,
            PatchBytesChanged,
            RealTimeStep,
            TimeInvalid,
            ForeignDecisionWrite,
            StatePoisoned,
            Stall,
            RenderFlagChanged,
        ]
        .get(code as usize)
        .copied()
        .unwrap_or(StatePoisoned)
    }
    pub fn reason(self) -> &'static str {
        match self {
            Fault::None => "none",
            Fault::FrameStateUnavailable => "frame_state_unavailable",
            Fault::RenderFlagNotInitial => "render_flag_not_initial",
            Fault::MovieCaptureActive => "movie_capture_active",
            Fault::CodeBytesNotOriginal => "code_bytes_not_original",
            Fault::ConstantUnavailable => "step_constant_unavailable",
            Fault::LiveSiteMismatch => "live_site_mismatch",
            Fault::CodePageNotRx => "code_page_not_r-x",
            Fault::MprotectFailed => "mprotect_failed",
            Fault::PatchRace => "code_bytes_changed_during_patch",
            Fault::FrameStateChanged => "frame_state_pointer_changed",
            Fault::PatchBytesChanged => "patch_bytes_changed",
            Fault::RealTimeStep => "real_time_step_not_fixed",
            Fault::TimeInvalid => "timer_values_invalid",
            Fault::ForeignDecisionWrite => "decision_byte_foreign_write",
            Fault::StatePoisoned => "scheduler_state_poisoned",
            Fault::Stall => "simulation_stalled",
            Fault::RenderFlagChanged => "engine_render_flag_changed",
        }
    }
}

static CONFIG: OnceLock<Sched> = OnceLock::new();
static PHASE: AtomicU32 = AtomicU32::new(Phase::Off as u32);
static FAULT: AtomicU32 = AtomicU32::new(0);
static RESTORE_REQUEST: AtomicBool = AtomicBool::new(false);
/// Which code qwords currently hold our patch.
static CMP_PATCHED: AtomicBool = AtomicBool::new(false);
static DT_PATCHED: AtomicBool = AtomicBool::new(false);
/// Patched dt load (depends on the step constant), and the constant's address.
static DT_LOAD_PATCH: AtomicU64 = AtomicU64::new(0);
static STEP_CONST: AtomicU64 = AtomicU64::new(0);
/// The FrameState whose decision byte we own, its original value, our last write.
static FRAME_STATE: AtomicU64 = AtomicU64::new(0);
static DECISION_ORIGINAL: AtomicU8 = AtomicU8::new(0);
static DECISION_OWNED: AtomicBool = AtomicBool::new(false);
static DECISION_VALUE: AtomicBool = AtomicBool::new(true);
static RESTORE_NOTE: AtomicU32 = AtomicU32::new(0);
/// Completed slot-13 callbacks while armed (stall watchdog progress).
static PROGRESS: AtomicU64 = AtomicU64::new(0);

static ST_STEPS: AtomicU64 = AtomicU64::new(0);
static ST_RENDERS: AtomicU64 = AtomicU64::new(0);
static ST_SKIPPED: AtomicU64 = AtomicU64::new(0);
static ST_FORCED: AtomicU64 = AtomicU64::new(0);
static ST_FORCED_GAP: AtomicU64 = AtomicU64::new(0);
static ST_LOADING: AtomicU64 = AtomicU64::new(0);
static ST_DROPPED_NS: AtomicU64 = AtomicU64::new(0);
static ST_REBASELINES: AtomicU64 = AtomicU64::new(0);
static ST_MAX_LAG: AtomicU64 = AtomicU64::new(0);
static ST_MAX_SKIP: AtomicU64 = AtomicU64::new(0);
static ST_SKIP_RUN: AtomicU64 = AtomicU64::new(0);
static ST_EXACT: AtomicU64 = AtomicU64::new(0);
static ST_IRREGULAR: AtomicU64 = AtomicU64::new(0);
static ST_NO_ADVANCE: AtomicU64 = AtomicU64::new(0);
static ST_MAX_WAIT_NS: AtomicU64 = AtomicU64::new(0);
static ST_TIMEOUTS: AtomicU64 = AtomicU64::new(0);
static ST_GAME: AtomicU64 = AtomicU64::new(0);
static ST_SPEED: AtomicU64 = AtomicU64::new(0);

struct Live {
    sched: Scheduler,
    prev: Option<TimeSample>,
}

static STATE: Mutex<Option<Live>> = Mutex::new(None);

pub fn phase() -> Phase {
    Phase::from_code(PHASE.load(Ordering::SeqCst))
}
pub fn fault() -> Fault {
    Fault::from_code(FAULT.load(Ordering::SeqCst))
}
pub fn config() -> Option<Sched> {
    CONFIG.get().copied()
}
pub fn step_constant() -> u64 {
    STEP_CONST.load(Ordering::SeqCst)
}
/// How the last restore ran: in the slot-13 callback or from the worker thread.
pub fn restore_note() -> &'static str {
    match RESTORE_NOTE.load(Ordering::SeqCst) {
        1 => "slot",
        2 => "worker",
        3 => "worker_unlocked",
        _ => "none",
    }
}

fn set_fault(f: Fault, phase: Phase) {
    let _ = FAULT.compare_exchange(0, f as u32, Ordering::SeqCst, Ordering::SeqCst);
    PHASE.store(phase as u32, Ordering::SeqCst);
}

/// Called once by the worker before the slot is registered.
pub fn init(cfg: Sched) {
    if CONFIG.set(cfg).is_ok() {
        *STATE.lock().unwrap_or_else(|e| e.into_inner()) = Some(Live { sched: Scheduler::new(cfg.policy), prev: None });
        PHASE.store(Phase::Pending as u32, Ordering::SeqCst);
    }
}

pub fn snapshot() -> SchedSnapshot {
    let mut s = SchedSnapshot { hz: config().map(|c| c.policy.hz).unwrap_or(0), ..Default::default() };
    s.stats.steps = ST_STEPS.load(Ordering::Relaxed);
    s.stats.renders = ST_RENDERS.load(Ordering::Relaxed);
    s.stats.skipped = ST_SKIPPED.load(Ordering::Relaxed);
    s.stats.forced_renders = ST_FORCED.load(Ordering::Relaxed);
    s.stats.forced_gap_renders = ST_FORCED_GAP.load(Ordering::Relaxed);
    s.stats.loading_steps = ST_LOADING.load(Ordering::Relaxed);
    s.stats.dropped_ns = ST_DROPPED_NS.load(Ordering::Relaxed);
    s.stats.rebaselines = ST_REBASELINES.load(Ordering::Relaxed);
    s.stats.max_lag_steps = ST_MAX_LAG.swap(0, Ordering::Relaxed);
    // Window maximum of the no-render run; seeded with the current run so a long run is
    // visible in every line it spans.
    let run = ST_SKIP_RUN.load(Ordering::Relaxed);
    s.stats.max_skip_run = ST_MAX_SKIP.swap(run, Ordering::Relaxed).max(run);
    s.skip_run = run;
    s.exact_steps = ST_EXACT.load(Ordering::Relaxed);
    s.irregular_steps = ST_IRREGULAR.load(Ordering::Relaxed) + ST_NO_ADVANCE.load(Ordering::Relaxed);
    s.max_wait_ns = ST_MAX_WAIT_NS.swap(0, Ordering::Relaxed);
    s.wait_timeouts = ST_TIMEOUTS.load(Ordering::Relaxed);
    s.game_time = f64::from_bits(ST_GAME.load(Ordering::Relaxed));
    s.speed = f64::from_bits(ST_SPEED.load(Ordering::Relaxed));
    s
}

// ----------------------------------------------------------------------------------
// Raw memory, code patching

unsafe fn read_u8(va: u64) -> u8 {
    std::ptr::read_volatile(va as *const u8)
}

fn qword(va: u64) -> &'static AtomicU64 {
    // SAFETY: callers pass the 8-byte-aligned patch addresses of the pinned image,
    // verified mapped before the first use.
    unsafe { &*(va as *const AtomicU64) }
}

extern "C" {
    fn mprotect(addr: *mut std::ffi::c_void, len: usize, prot: i32) -> i32;
    fn mmap(addr: *mut std::ffi::c_void, len: usize, prot: i32, flags: i32, fd: i32, off: i64)
        -> *mut std::ffi::c_void;
    fn munmap(addr: *mut std::ffi::c_void, len: usize) -> i32;
    fn syscall(number: i64, ...) -> i64;
}
const PROT_READ: i32 = 1;
const PROT_WRITE: i32 = 2;
const PROT_EXEC: i32 = 4;
const MAP_PRIVATE: i32 = 0x02;
const MAP_ANONYMOUS: i32 = 0x20;
const MAP_FIXED_NOREPLACE: i32 = 0x100000;
const SYS_MEMBARRIER: i64 = 324;
const MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE: i64 = 32;
const MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE: i64 = 64;

/// Best effort: make every thread of the process serialize its instruction stream.
fn sync_cores() {
    // SAFETY: plain syscalls with integer arguments; failure (old kernel) is ignored.
    unsafe {
        static REGISTERED: OnceLock<bool> = OnceLock::new();
        let ok = *REGISTERED.get_or_init(|| {
            syscall(SYS_MEMBARRIER, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE, 0i64, 0i64) == 0
        });
        if ok {
            syscall(SYS_MEMBARRIER, MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE, 0i64, 0i64);
        }
    }
}

/// Run `f` with the FrameTick code page writable (and still executable: other threads
/// may run code on that page meanwhile), then make it `r-x` again.
fn with_code_writable<T>(f: impl FnOnce() -> T) -> Result<T, Fault> {
    let page = pinned::DT_PAGE as *mut std::ffi::c_void;
    // SAFETY: the page belongs to the pinned executable's text mapping (checked r-x).
    unsafe {
        if mprotect(page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 {
            return Err(Fault::MprotectFailed);
        }
        let out = f();
        if mprotect(page, 4096, PROT_READ | PROT_EXEC) != 0 {
            return Err(Fault::MprotectFailed);
        }
        Ok(out)
    }
}

fn maps_perms(start: u64, len: u64) -> Option<String> {
    let maps = std::fs::read_to_string("/proc/self/maps").ok()?;
    maps.lines().find_map(|line| {
        let mut parts = line.split_whitespace();
        let (range, perms) = (parts.next()?, parts.next()?);
        let (a, b) = range.split_once('-')?;
        let (a, b) = (u64::from_str_radix(a, 16).ok()?, u64::from_str_radix(b, 16).ok()?);
        (a <= start && start + len <= b).then(|| perms.to_owned())
    })
}

fn q(bytes: [u8; 8]) -> u64 {
    u64::from_le_bytes(bytes)
}

/// The `double 1/hz` the dt load will read: .rodata for 60/20 Hz, an own page for 30 Hz.
fn step_constant_for(hz: u32) -> Result<u64, Fault> {
    let want = fixed_dt(hz).to_bits();
    let rodata = match hz {
        60 => Some(pinned::ONE_SIXTIETH_CONST),
        20 => Some(pinned::ONE_TWENTIETH_CONST),
        _ => None,
    };
    if let Some(va) = rodata {
        // SAFETY: .rodata of the pinned image (also covered by a pinned site).
        return if unsafe { read_u64(va) } == want { Ok(va) } else { Err(Fault::ConstantUnavailable) };
    }
    for &addr in &pinned::CONST_PAGE_CANDIDATES {
        // SAFETY: anonymous private mapping; MAP_FIXED_NOREPLACE never replaces an
        // existing mapping (older kernels treat it as a hint, checked below).
        unsafe {
            let p = mmap(
                addr as *mut _,
                4096,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                -1,
                0,
            );
            if p as isize == -1 {
                continue;
            }
            if p as u64 != addr {
                munmap(p, 4096);
                continue;
            }
            std::ptr::write_volatile(p as *mut u64, want);
            if mprotect(p, 4096, PROT_READ) != 0 {
                munmap(p, 4096);
                continue;
            }
            // Kept for the process lifetime: a thread may still be executing the patched
            // load while it is being restored.
            return Ok(addr);
        }
    }
    Err(Fault::ConstantUnavailable)
}

fn apply_patches(load_patched: u64) -> Result<(), Fault> {
    let cas =
        |va: u64, from: u64, to: u64| qword(va).compare_exchange(from, to, Ordering::SeqCst, Ordering::SeqCst).is_ok();
    let result = with_code_writable(|| {
        // 1. Decision: read #2 tests FrameState+0x201 (already set to 1 = render).
        if !cas(pinned::DECISION_CMP_VA, q(pinned::DECISION_CMP_ORIGINAL), q(pinned::DECISION_CMP_PATCHED)) {
            return false;
        }
        CMP_PATCHED.store(true, Ordering::SeqCst);
        // 2. dt load first (dt = min(1/hz, dt)), then drop the min (dt = 1/hz).
        if !cas(pinned::DT_LOAD_VA, q(pinned::DT_LOAD_ORIGINAL), load_patched) {
            return false;
        }
        if !cas(pinned::DT_MIN_VA, q(pinned::DT_MIN_ORIGINAL), q(pinned::DT_MIN_PATCHED)) {
            let _ = cas(pinned::DT_LOAD_VA, load_patched, q(pinned::DT_LOAD_ORIGINAL));
            return false;
        }
        DT_PATCHED.store(true, Ordering::SeqCst);
        true
    })?;
    sync_cores();
    if result {
        Ok(())
    } else {
        Err(Fault::PatchRace)
    }
}

/// Restore code (decision compare first, then dt in reverse order) and the decision
/// byte. Safe from any thread at any time: each store is atomic, every intermediate
/// state is a valid FrameTick, and nothing reads the decision byte once the compare is
/// back to the engine's render flag.
fn restore_all(note: u32) {
    let cas = |va: u64, from: u64, to: u64| {
        let _ = qword(va).compare_exchange(from, to, Ordering::SeqCst, Ordering::SeqCst);
    };
    let cmp = CMP_PATCHED.swap(false, Ordering::SeqCst);
    let dt = DT_PATCHED.swap(false, Ordering::SeqCst);
    if cmp || dt {
        let load_patched = DT_LOAD_PATCH.load(Ordering::SeqCst);
        let _ = with_code_writable(|| {
            if cmp {
                cas(pinned::DECISION_CMP_VA, q(pinned::DECISION_CMP_PATCHED), q(pinned::DECISION_CMP_ORIGINAL));
            }
            if dt {
                cas(pinned::DT_MIN_VA, q(pinned::DT_MIN_PATCHED), q(pinned::DT_MIN_ORIGINAL));
                cas(pinned::DT_LOAD_VA, load_patched, q(pinned::DT_LOAD_ORIGINAL));
            }
        });
        sync_cores();
    }
    if DECISION_OWNED.swap(false, Ordering::SeqCst) {
        let fs = FRAME_STATE.load(Ordering::SeqCst);
        // SAFETY: fs was verified mapped at admission; FrameState lives for the process.
        unsafe {
            if fs != 0 && read_u64(pinned::FRAME_STATE_GLOBAL) == fs {
                let byte = (fs + pinned::FRAME_STATE_DECISION_BYTE) as *mut u8;
                std::ptr::write_volatile(byte, DECISION_ORIGINAL.load(Ordering::SeqCst));
            }
        }
    }
    if cmp || dt {
        RESTORE_NOTE.store(note, Ordering::SeqCst);
    }
}

/// Full restore from the slot-13 thread.
pub fn restore_in_slot() {
    restore_all(1);
}

/// Restore from the worker thread (disarm file, stall watchdog). Takes the scheduler
/// lock when it can, so no slot-13 decision is written concurrently; after 3 s it
/// restores anyway (a late decision write would then only touch unread padding).
pub fn restore_from_worker() {
    if !CMP_PATCHED.load(Ordering::SeqCst)
        && !DT_PATCHED.load(Ordering::SeqCst)
        && !DECISION_OWNED.load(Ordering::SeqCst)
    {
        if phase() == Phase::Pending {
            PHASE.store(Phase::Restored as u32, Ordering::SeqCst);
        }
        return;
    }
    RESTORE_REQUEST.store(true, Ordering::SeqCst);
    let start = Instant::now();
    loop {
        match STATE.try_lock() {
            Ok(_guard) => {
                restore_all(2);
                break;
            }
            Err(TryLockError::Poisoned(_)) => {
                restore_all(3);
                break;
            }
            Err(TryLockError::WouldBlock) if start.elapsed() >= Duration::from_secs(3) => {
                restore_all(3);
                break;
            }
            Err(TryLockError::WouldBlock) => std::thread::sleep(Duration::from_millis(2)),
        }
    }
    if matches!(phase(), Phase::Active | Phase::Pending) {
        PHASE.store(Phase::Restored as u32, Ordering::SeqCst);
    }
}

// ----------------------------------------------------------------------------------
// Stall watchdog (worker thread)

/// True while X4 legitimately runs no simulation frames: sync loading (main-loop
/// appState bit 1) or the VBI loading flag.
fn engine_loading() -> bool {
    // SAFETY: fixed .bss/.data of the pinned image; table pointer checked for null.
    unsafe {
        if read_u8(pinned::MAIN_APP_STATE) & 2 != 0 {
            return true;
        }
        let table = read_u64(pinned::VBI_TABLE_GLOBAL);
        table != 0 && read_i32(table + pinned::VBI_LOADING_FLAG as u64) != 0
    }
}

pub struct Watchdog(Option<StallWatch>);

impl Watchdog {
    pub fn new() -> Self {
        Watchdog(config().map(|c| StallWatch::new(c.stall_ms)))
    }
    /// Polled by the worker. Returns a log line when the watchdog fired; it has then
    /// restored everything and the caller disarms the agent.
    pub fn poll(&mut self) -> Option<String> {
        let watch = self.0.as_mut()?;
        if phase() != Phase::Active {
            return None;
        }
        let stalled = watch.observe(epoch_ns(), PROGRESS.load(Ordering::SeqCst), engine_loading())?;
        // SAFETY: fixed .bss byte of the pinned image.
        let in_flight = unsafe { read_u8(pinned::MAIN_FRAME_IN_FLIGHT) };
        set_fault(Fault::Stall, Phase::Faulted);
        restore_from_worker();
        PHASE.store(Phase::Faulted as u32, Ordering::SeqCst);
        Some(format!(
            "X4MP_AGENT SCHED STALL no_step_ms={} main_frame_in_flight={} steps={} renders={} skip_run={} restore={}",
            stalled / 1_000_000,
            in_flight,
            ST_STEPS.load(Ordering::Relaxed),
            ST_RENDERS.load(Ordering::Relaxed),
            ST_SKIP_RUN.load(Ordering::Relaxed),
            restore_note()
        ))
    }
}

// ----------------------------------------------------------------------------------
// The live engine for the slot-13 protocol

fn main_parked() -> bool {
    // SAFETY: fixed .bss byte of the pinned image (inside main_loop_* sites).
    unsafe { read_u8(pinned::MAIN_FRAME_IN_FLIGHT) == 1 }
}

fn wait_parked(timeout: Duration) -> bool {
    let start = Instant::now();
    while !main_parked() {
        if start.elapsed() >= timeout {
            return false;
        }
        std::thread::sleep(Duration::from_micros(500));
    }
    true
}

fn wait_budget() -> Duration {
    Duration::from_millis(config().map(|c| c.wait_ms).unwrap_or(5000))
}

struct LiveEngine {
    decision: *mut u8,
}

impl Engine for LiveEngine {
    fn main_parked(&self) -> bool {
        main_parked()
    }
    fn now_ns(&self) -> u64 {
        epoch_ns()
    }
    fn pause(&self) {
        std::thread::sleep(Duration::from_micros(500));
    }
    fn sleep_until(&self, ns: u64) {
        let now = epoch_ns();
        if ns > now {
            std::thread::sleep(Duration::from_nanos((ns - now).min(1_000_000_000)));
        }
    }
    fn render_decision(&self) -> bool {
        // SAFETY: verified FrameState padding byte.
        unsafe { std::ptr::read_volatile(self.decision) != 0 }
    }
    fn set_render_decision(&self, render: bool) {
        // SAFETY: verified FrameState padding byte; main thread parked (slot13_step).
        unsafe { std::ptr::write_volatile(self.decision, u8::from(render)) };
        DECISION_VALUE.store(render, Ordering::SeqCst);
    }
}

fn admit(live: &mut Live) -> Result<bool, Fault> {
    let cfg = config().ok_or(Fault::StatePoisoned)?;
    let ranges = readable_ranges();
    // SAFETY: every address is checked against /proc/self/maps first.
    unsafe {
        let fs = read_u64(pinned::FRAME_STATE_GLOBAL);
        if fs == 0 || !covered(&ranges, fs, 0x2f0) {
            return Err(Fault::FrameStateUnavailable);
        }
        if read_u8(fs + pinned::FRAME_STATE_RENDER_FLAG) != 1 {
            return Err(Fault::RenderFlagNotInitial);
        }
        if read_u8(pinned::MOVIE_CAPTURE_FLAG) != 0 {
            return Err(Fault::MovieCaptureActive);
        }
        let image = LiveImage { ranges };
        for name in [
            "frame_tick_time_advance",
            "frame_tick_dt_render_decision",
            "main_loop_frame_tick_call",
            "main_loop_sim_done_wait",
        ] {
            let site = pinned::SITES.iter().find(|s| s.name == name).ok_or(Fault::LiveSiteMismatch)?;
            if pinned::check_site(&image, site) != pinned::Check::Ok {
                return Err(Fault::LiveSiteMismatch);
            }
        }
        if qword(pinned::DECISION_CMP_VA).load(Ordering::SeqCst) != q(pinned::DECISION_CMP_ORIGINAL)
            || qword(pinned::DT_LOAD_VA).load(Ordering::SeqCst) != q(pinned::DT_LOAD_ORIGINAL)
            || qword(pinned::DT_MIN_VA).load(Ordering::SeqCst) != q(pinned::DT_MIN_ORIGINAL)
        {
            return Err(Fault::CodeBytesNotOriginal);
        }
        if !maps_perms(pinned::DT_PAGE, 4096).is_some_and(|p| p.starts_with("r-x")) {
            return Err(Fault::CodePageNotRx);
        }
        // Patch only while the main thread is parked outside FrameTick.
        if !wait_parked(wait_budget()) {
            return Ok(false); // try again next frame
        }
        let constant = match STEP_CONST.load(Ordering::SeqCst) {
            0 => step_constant_for(cfg.policy.hz)?,
            c => c,
        };
        STEP_CONST.store(constant, Ordering::SeqCst);
        let load_patched = pinned::dt_load_patched(constant).ok_or(Fault::ConstantUnavailable)?;
        DT_LOAD_PATCH.store(q(load_patched), Ordering::SeqCst);
        // Own the decision byte (render = 1) before read #2 is pointed at it.
        let byte = (fs + pinned::FRAME_STATE_DECISION_BYTE) as *mut u8;
        FRAME_STATE.store(fs, Ordering::SeqCst);
        DECISION_ORIGINAL.store(std::ptr::read_volatile(byte), Ordering::SeqCst);
        DECISION_OWNED.store(true, Ordering::SeqCst);
        std::ptr::write_volatile(byte, 1);
        DECISION_VALUE.store(true, Ordering::SeqCst);
        apply_patches(q(load_patched))?;
        live.prev = None;
        Ok(true)
    }
}

fn publish(live: &Live) {
    let s = &live.sched.stats;
    ST_STEPS.store(s.steps, Ordering::Relaxed);
    ST_RENDERS.store(s.renders, Ordering::Relaxed);
    ST_SKIPPED.store(s.skipped, Ordering::Relaxed);
    ST_FORCED.store(s.forced_renders, Ordering::Relaxed);
    ST_FORCED_GAP.store(s.forced_gap_renders, Ordering::Relaxed);
    ST_LOADING.store(s.loading_steps, Ordering::Relaxed);
    ST_DROPPED_NS.store(s.dropped_ns, Ordering::Relaxed);
    ST_REBASELINES.store(s.rebaselines, Ordering::Relaxed);
    let run = live.sched.current_skip_run();
    ST_SKIP_RUN.store(run, Ordering::Relaxed);
    ST_MAX_SKIP.fetch_max(run, Ordering::Relaxed);
}

/// The scheduler's part of one slot-13 callback. `Err` means the scheduler faulted and
/// restored; the caller disarms the whole agent.
pub fn on_frame(loading: bool) -> Result<(), Fault> {
    let p = phase();
    if !matches!(p, Phase::Pending | Phase::Active) {
        return Ok(());
    }
    let mut guard = match STATE.lock() {
        Ok(g) => g,
        Err(_) => {
            restore_in_slot();
            set_fault(Fault::StatePoisoned, Phase::Faulted);
            return Err(Fault::StatePoisoned);
        }
    };
    let Some(live) = guard.as_mut() else { return Ok(()) };
    if RESTORE_REQUEST.load(Ordering::SeqCst) {
        restore_in_slot();
        PHASE.store(Phase::Restored as u32, Ordering::SeqCst);
        return Ok(());
    }
    let result = match frame_inner(live, p, loading) {
        Ok(()) => Ok(()),
        Err(f) => {
            restore_in_slot();
            if phase() == Phase::Pending {
                // Admission refused: nothing of ours is in the engine; the observer stays.
                set_fault(f, Phase::Refused);
                Ok(())
            } else {
                set_fault(f, Phase::Faulted);
                Err(f)
            }
        }
    };
    PROGRESS.fetch_add(1, Ordering::SeqCst);
    result
}

fn frame_inner(live: &mut Live, p: Phase, loading: bool) -> Result<(), Fault> {
    if p == Phase::Pending {
        if loading || !admit(live)? {
            return Ok(());
        }
        PHASE.store(Phase::Active as u32, Ordering::SeqCst);
    }
    let hz = config().map(|c| c.policy.hz).unwrap_or(60);
    let fs = FRAME_STATE.load(Ordering::SeqCst);
    let load_patched = DT_LOAD_PATCH.load(Ordering::SeqCst);
    // SAFETY: fixed addresses of the pinned image and the FrameState verified at admit.
    let (sample, temp_paused) = unsafe {
        if read_u64(pinned::FRAME_STATE_GLOBAL) != fs {
            return Err(Fault::FrameStateChanged);
        }
        if read_u8(fs + pinned::FRAME_STATE_RENDER_FLAG) != 1 {
            return Err(Fault::RenderFlagChanged);
        }
        if read_u8(pinned::MOVIE_CAPTURE_FLAG) != 0 {
            return Err(Fault::MovieCaptureActive);
        }
        if qword(pinned::DECISION_CMP_VA).load(Ordering::SeqCst) != q(pinned::DECISION_CMP_PATCHED)
            || qword(pinned::DT_LOAD_VA).load(Ordering::SeqCst) != load_patched
            || qword(pinned::DT_MIN_VA).load(Ordering::SeqCst) != q(pinned::DT_MIN_PATCHED)
        {
            return Err(Fault::PatchBytesChanged);
        }
        (
            TimeSample {
                game: read_f64(pinned::TIMER_GAME_TIME),
                real: read_f64(pinned::TIMER_REAL_TIME),
                speed: read_f64(pinned::TIMER_SPEED),
            },
            read_i32(pinned::TIMER_TEMP_PAUSE_COUNT) != 0,
        )
    };
    let mut no_advance = false;
    if loading {
        live.prev = None;
    } else {
        if let Some(prev) = live.prev {
            match check_step(&prev, &sample, fixed_dt(hz)) {
                StepCheck::Exact => {
                    ST_EXACT.fetch_add(1, Ordering::Relaxed);
                }
                StepCheck::GameIrregular => {
                    ST_IRREGULAR.fetch_add(1, Ordering::Relaxed);
                }
                StepCheck::NoAdvance => {
                    ST_NO_ADVANCE.fetch_add(1, Ordering::Relaxed);
                    no_advance = true;
                }
                StepCheck::RealTimeStep => return Err(Fault::RealTimeStep),
                StepCheck::Invalid => return Err(Fault::TimeInvalid),
            }
        }
        live.prev = Some(sample);
    }
    ST_GAME.store(sample.game.to_bits(), Ordering::Relaxed);
    ST_SPEED.store(sample.speed.to_bits(), Ordering::Relaxed);

    let engine = LiveEngine { decision: (fs + pinned::FRAME_STATE_DECISION_BYTE) as *mut u8 };
    let mut last = DECISION_VALUE.load(Ordering::SeqCst);
    let wait_ns = wait_budget().as_nanos() as u64;
    let outcome = slot13_step(&mut live.sched, &engine, &mut last, loading || no_advance, temp_paused, wait_ns);
    match outcome {
        Slot13::Stepped { decision, waited_ns } => {
            ST_MAX_WAIT_NS.fetch_max(waited_ns, Ordering::Relaxed);
            ST_MAX_LAG.fetch_max(decision.lag_steps, Ordering::Relaxed);
        }
        Slot13::WaitTimeout { waited_ns } => {
            ST_MAX_WAIT_NS.fetch_max(waited_ns, Ordering::Relaxed);
            ST_TIMEOUTS.fetch_add(1, Ordering::Relaxed);
        }
        Slot13::ForeignFlagWrite => return Err(Fault::ForeignDecisionWrite),
    }
    publish(live);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn thirty_hertz_constant_page_is_mapped_read_only_within_reach() {
        // Maps the agent's own constant page in the test process (the image constants
        // for 60/20 Hz exist only inside X4 and are covered by sigcheck instead).
        let va = step_constant_for(30).expect("constant page");
        assert!(pinned::CONST_PAGE_CANDIDATES.contains(&va));
        // SAFETY: just mapped and filled by step_constant_for.
        assert_eq!(unsafe { std::ptr::read_volatile(va as *const f64) }, 1.0 / 30.0);
        assert_eq!(maps_perms(va, 4096).as_deref().map(|p| &p[..3]), Some("r--"));
        assert!(pinned::dt_load_patched(va).is_some());
        // A second request takes the next candidate (never replaces a mapping).
        let other = step_constant_for(30).expect("second page");
        assert_ne!(other, va);
    }
}
