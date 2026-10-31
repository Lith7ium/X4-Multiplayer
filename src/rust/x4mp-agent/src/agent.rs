//! In-process side of the agent (Linux only): the ELF constructor, the worker thread that
//! verifies the build and defers registration, and the per-frame callback in VBI slot 13.
//!
//! The observer's only engine write is one pointer into a verified-empty per-frame slot of
//! the VBI table (compare-exchange from null), and clearing it again on disarm. With
//! `X4MP_SCHED=fixedN` the fixed-step scheduler (`sched_live.rs`) additionally patches
//! three qwords of FrameTick (render decision, dt) and owns one FrameState padding byte;
//! all are restored on every disarm path, including its stall watchdog.

use std::cell::Cell;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicUsize, Ordering};
use std::time::{Duration, Instant};

use crate::config::{self, Config, Parsed};
use crate::export::{self, FrameSample};
use crate::logic::{cadence_line, ArmGate, Disarm, Gate, Observation, Snapshot};
use crate::pinned::{self, Image};
use crate::sched_live;
use crate::scheduler::sched_line;
use crate::sha256;

#[used]
#[link_section = ".init_array"]
static INIT: extern "C" fn() = constructor;

extern "C" fn constructor() {
    // Never unwind into the dynamic loader; never block X4's startup.
    let _ = catch_unwind(|| {
        let parsed = config::parse(|key| std::env::var(key).ok(), std::process::id());
        match parsed {
            Parsed::Disabled => {}
            Parsed::Invalid(reason) => eprintln!("X4MP_AGENT REFUSED config: {reason}"),
            Parsed::Enabled(config) => {
                let _ = std::thread::Builder::new().name("x4mp-agent".into()).spawn(move || worker(config));
            }
        }
    });
}

// ----------------------------------------------------------------------------------
// Live process memory

/// Readable `[start, end)` ranges from `/proc/self/maps`.
pub(crate) fn readable_ranges() -> Vec<(u64, u64)> {
    let Ok(maps) = fs::read_to_string("/proc/self/maps") else { return Vec::new() };
    maps.lines()
        .filter_map(|line| {
            let mut parts = line.split_whitespace();
            let range = parts.next()?;
            let perms = parts.next()?;
            if !perms.starts_with('r') {
                return None;
            }
            let (a, b) = range.split_once('-')?;
            Some((u64::from_str_radix(a, 16).ok()?, u64::from_str_radix(b, 16).ok()?))
        })
        .collect()
}

pub(crate) fn covered(ranges: &[(u64, u64)], va: u64, len: usize) -> bool {
    let Some(end) = va.checked_add(len as u64) else { return false };
    ranges.iter().any(|&(a, b)| a <= va && end <= b)
}

/// The running executable, read in place after checking `/proc/self/maps`.
pub(crate) struct LiveImage {
    pub(crate) ranges: Vec<(u64, u64)>,
}

impl Image for LiveImage {
    fn read(&self, va: u64, len: usize) -> Option<Vec<u8>> {
        if !covered(&self.ranges, va, len) {
            return None;
        }
        // SAFETY: the whole range is mapped readable in this process.
        Some(unsafe { std::slice::from_raw_parts(va as *const u8, len) }.to_vec())
    }
}

pub(crate) unsafe fn read_u64(va: u64) -> u64 {
    std::ptr::read_volatile(va as *const u64)
}
pub(crate) unsafe fn read_f64(va: u64) -> f64 {
    std::ptr::read_volatile(va as *const f64)
}
pub(crate) unsafe fn read_i32(va: u64) -> i32 {
    std::ptr::read_volatile(va as *const i32)
}

/// One per-frame slot of the VBI table, as an atomic word.
unsafe fn slot(table: u64, index: usize) -> &'static AtomicUsize {
    &*((table + 8 * index as u64) as *const AtomicUsize)
}

/// MoveWorker TLS game clock: current and previous `CurTime` (local-exec static TLS of
/// the ET_EXEC image, fixed fs-relative offsets).
#[inline]
unsafe fn tls_dt() -> f64 {
    let cur: u64;
    let prev: u64;
    std::arch::asm!(
        "mov {cur}, qword ptr fs:[{c}]",
        "mov {prev}, qword ptr fs:[{p}]",
        cur = out(reg) cur, prev = out(reg) prev,
        c = const pinned::TLS_CUR_TIME, p = const pinned::TLS_PREV_TIME,
        options(nostack, readonly, preserves_flags)
    );
    f64::from_bits(cur) - f64::from_bits(prev)
}

fn movement_worker_running() -> bool {
    let Ok(tasks) = fs::read_dir("/proc/self/task") else { return false };
    tasks.flatten().any(|task| {
        fs::read(task.path().join("comm"))
            .map(|name| name.strip_suffix(b"\n").unwrap_or(&name) == pinned::MOVE_WORKER_THREAD_NAME)
            .unwrap_or(false)
    })
}

// ----------------------------------------------------------------------------------
// The registered handler and its shared state

/// The object placed in the slot: the engine calls `(*vtable)[2](this)` (vtable +0x10).
#[repr(C)]
struct Handler {
    vtable: *const [usize; 4],
}
// SAFETY: immutable after construction; only its address is shared with the engine.
unsafe impl Sync for Handler {}

extern "C" fn no_op(_this: *mut Handler) {}
/// The callable vtable is filled at runtime (fn pointers are not usize constants).
static mut VTABLE_LIVE: [usize; 4] = [0; 4];
static mut HANDLER_LIVE: Handler = Handler { vtable: std::ptr::null() };

static TABLE: AtomicU64 = AtomicU64::new(0);
static ARMED: AtomicBool = AtomicBool::new(false);
static DISARM: AtomicU32 = AtomicU32::new(0);
static OWNER_CLAIMED: AtomicBool = AtomicBool::new(false);
static IN_CALLBACK: AtomicBool = AtomicBool::new(false);

static FRAMES: AtomicU64 = AtomicU64::new(0);
static LOADING_SKIPS: AtomicU64 = AtomicU64::new(0);
static PAUSED_FRAMES: AtomicU64 = AtomicU64::new(0);
static GAME_TIME: AtomicU64 = AtomicU64::new(0);
static REAL_TIME: AtomicU64 = AtomicU64::new(0);
static SPEED: AtomicU64 = AtomicU64::new(0);
static TLS_DT: AtomicU64 = AtomicU64::new(0);
static PAUSED: AtomicBool = AtomicBool::new(false);
static MAX_GAP_NS: AtomicU64 = AtomicU64::new(0);
static LAST_NS: AtomicU64 = AtomicU64::new(0);

thread_local! {
    static IS_OWNER: Cell<bool> = const { Cell::new(false) };
}

fn epoch() -> &'static Instant {
    static EPOCH: std::sync::OnceLock<Instant> = std::sync::OnceLock::new();
    EPOCH.get_or_init(Instant::now)
}

/// Monotonic ns since the agent epoch.
pub(crate) fn epoch_ns() -> u64 {
    epoch().elapsed().as_nanos() as u64
}

fn handler_address() -> usize {
    // The address of the static is stable; its contents are initialised by `arm`.
    std::ptr::addr_of!(HANDLER_LIVE) as usize
}

/// Fail closed: record the first reason, restore the scheduler's engine writes and
/// withdraw the slot if it is still ours.
fn disarm(reason: Disarm) {
    let _ = DISARM.compare_exchange(0, reason as u32, Ordering::SeqCst, Ordering::SeqCst);
    if IS_OWNER.with(Cell::get) {
        sched_live::restore_in_slot(); // MoveWorker, outside the scheduler lock
    } else {
        sched_live::restore_from_worker(); // takes the scheduler lock (at most 3 s)
    }
    if ARMED.swap(false, Ordering::SeqCst) {
        let table = TABLE.load(Ordering::SeqCst);
        // SAFETY: the table pointer was verified readable/writable at arm time and the
        // engine keeps the table alive for the process lifetime once created.
        if table != 0 && unsafe { read_u64(pinned::VBI_TABLE_GLOBAL) } == table {
            let _ = unsafe { slot(table, pinned::AGENT_SLOT) }.compare_exchange(
                handler_address(),
                0,
                Ordering::SeqCst,
                Ordering::SeqCst,
            );
        }
    }
}

extern "C" fn on_move(_this: *mut Handler) {
    if !ARMED.load(Ordering::Acquire) {
        return;
    }
    if IN_CALLBACK.swap(true, Ordering::Acquire) {
        disarm(Disarm::Reentrant);
        return;
    }
    let result = catch_unwind(AssertUnwindSafe(frame));
    IN_CALLBACK.store(false, Ordering::Release);
    match result {
        Ok(Ok(())) => {}
        Ok(Err(reason)) => disarm(reason),
        Err(_) => disarm(Disarm::Panic),
    }
}

fn frame() -> Result<(), Disarm> {
    // Thread ownership: the first callback must be on the `Movement worker` thread, and
    // every later callback on that same thread.
    if !IS_OWNER.with(Cell::get) {
        if OWNER_CLAIMED.swap(true, Ordering::SeqCst) {
            return Err(Disarm::ForeignThread);
        }
        let name = fs::read("/proc/thread-self/comm").unwrap_or_default();
        if name.strip_suffix(b"\n").unwrap_or(&name) != pinned::MOVE_WORKER_THREAD_NAME {
            return Err(Disarm::NotMovementWorker);
        }
        IS_OWNER.with(|owner| owner.set(true));
    }
    let table = TABLE.load(Ordering::Relaxed);
    // SAFETY: fixed .bss global of the pinned executable; table verified at arm time.
    unsafe {
        if read_u64(pinned::VBI_TABLE_GLOBAL) != table {
            return Err(Disarm::TableChanged);
        }
        if slot(table, pinned::AGENT_SLOT).load(Ordering::Relaxed) != handler_address() {
            return Err(Disarm::SlotChanged);
        }
        let client = slot(table, pinned::GAME_CLIENT_SLOT).load(Ordering::Relaxed) as u64;
        if client == 0 || read_u64(client) != pinned::GAME_CLIENT_VTABLE {
            return Err(Disarm::GameClientChanged);
        }
    }
    let now = epoch().elapsed().as_nanos() as u64;
    let last = LAST_NS.swap(now, Ordering::Relaxed);
    if last != 0 {
        MAX_GAP_NS.fetch_max(now.saturating_sub(last), Ordering::Relaxed);
    }
    // SAFETY: loading flag is an int inside the verified table.
    if unsafe { read_i32(table + pinned::VBI_LOADING_FLAG as u64) } != 0 {
        LOADING_SKIPS.fetch_add(1, Ordering::Relaxed);
        return sched_live::on_frame(true).map_err(|_| Disarm::SchedulerGuard);
    }
    // SAFETY: TimerBase fields are fixed .bss doubles/ints of the pinned executable. The
    // engine writes them under its mutex on the main thread before releasing this frame.
    let (game_time, real_time, speed, paused, dt) = unsafe {
        let paused = read_i32(pinned::TIMER_PAUSE_COUNT) != 0 || read_i32(pinned::TIMER_TEMP_PAUSE_COUNT) != 0;
        (
            read_f64(pinned::TIMER_GAME_TIME),
            read_f64(pinned::TIMER_REAL_TIME),
            read_f64(pinned::TIMER_SPEED),
            paused,
            tls_dt(),
        )
    };
    if !game_time.is_finite() || !real_time.is_finite() || !speed.is_finite() || !(0.0..=64.0).contains(&speed) {
        return Err(Disarm::InvalidTime);
    }
    let frame = FRAMES.fetch_add(1, Ordering::Relaxed) + 1;
    if paused {
        PAUSED_FRAMES.fetch_add(1, Ordering::Relaxed);
    }
    GAME_TIME.store(game_time.to_bits(), Ordering::Relaxed);
    REAL_TIME.store(real_time.to_bits(), Ordering::Relaxed);
    SPEED.store(speed.to_bits(), Ordering::Relaxed);
    TLS_DT.store(dt.to_bits(), Ordering::Relaxed);
    PAUSED.store(paused, Ordering::Relaxed);
    let sample = FrameSample { frame, game_time, real_time, speed, tls_dt: dt, paused, monotonic_ns: now };
    export::on_frame(&sample).map_err(|_| Disarm::ExportFailed)?;
    // Last in the callback: it may wait for the main thread and pace (sleep).
    sched_live::on_frame(false).map_err(|_| Disarm::SchedulerGuard)
}

fn snapshot() -> Snapshot {
    Snapshot {
        frames: FRAMES.load(Ordering::Relaxed),
        loading_skips: LOADING_SKIPS.load(Ordering::Relaxed),
        paused_frames: PAUSED_FRAMES.load(Ordering::Relaxed),
        game_time: f64::from_bits(GAME_TIME.load(Ordering::Relaxed)),
        real_time: f64::from_bits(REAL_TIME.load(Ordering::Relaxed)),
        speed: f64::from_bits(SPEED.load(Ordering::Relaxed)),
        tls_dt: f64::from_bits(TLS_DT.load(Ordering::Relaxed)),
        paused: PAUSED.load(Ordering::Relaxed),
        max_gap_ns: MAX_GAP_NS.swap(0, Ordering::Relaxed),
        monotonic_ns: epoch().elapsed().as_nanos() as u64,
    }
}

// ----------------------------------------------------------------------------------
// Worker thread

struct Log(Option<File>);

impl Log {
    fn open(path: &Path) -> Log {
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        Log(OpenOptions::new().create(true).append(true).open(path).ok())
    }
    fn line(&mut self, text: &str) {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs_f64())
            .unwrap_or(0.0);
        match &mut self.0 {
            Some(file) => {
                let _ = writeln!(file, "{stamp:.3} {text}");
                let _ = file.flush();
            }
            None => eprintln!("{text}"),
        }
    }
}

fn verify_build(log: &mut Log) -> bool {
    let exe = Path::new("/proc/self/exe");
    match fs::metadata(exe) {
        Ok(meta) if meta.len() == pinned::FILE_SIZE => {}
        Ok(meta) => {
            log.line(&format!("X4MP_AGENT REFUSED executable size {} (pinned {})", meta.len(), pinned::FILE_SIZE));
            return false;
        }
        Err(e) => {
            log.line(&format!("X4MP_AGENT REFUSED cannot stat executable: {e}"));
            return false;
        }
    }
    match sha256::file_hex(exe) {
        Ok(digest) if digest == pinned::FILE_SHA256 => {}
        Ok(digest) => {
            log.line(&format!("X4MP_AGENT REFUSED executable sha256 {digest}"));
            return false;
        }
        Err(e) => {
            log.line(&format!("X4MP_AGENT REFUSED cannot hash executable: {e}"));
            return false;
        }
    }
    match pinned::verify_sites(&LiveImage { ranges: readable_ranges() }) {
        Ok(count) => {
            log.line(&format!(
                "X4MP_AGENT VERIFIED build={} version={} sites={count} pid={}",
                pinned::STEAM_BUILD,
                pinned::GAME_VERSION,
                std::process::id()
            ));
            true
        }
        Err(reason) => {
            log.line(&format!("X4MP_AGENT REFUSED live signature {reason}"));
            false
        }
    }
}

fn observe(start: Instant) -> Observation {
    let ranges = readable_ranges();
    let now_ms = start.elapsed().as_millis() as u64;
    let mut o = Observation { now_ms, mover_thread: movement_worker_running(), ..Default::default() };
    if !covered(&ranges, pinned::VBI_TABLE_GLOBAL, 8) || !covered(&ranges, pinned::TIMER_BASE, 0x70) {
        return o;
    }
    // SAFETY: every address below is checked against /proc/self/maps first.
    unsafe {
        let table = read_u64(pinned::VBI_TABLE_GLOBAL);
        o.game_time = read_f64(pinned::TIMER_GAME_TIME);
        if table == 0 || !covered(&ranges, table, pinned::VBI_TABLE_SIZE) {
            return o;
        }
        o.table_ready = true;
        o.loading = read_i32(table + pinned::VBI_LOADING_FLAG as u64) != 0;
        o.slot_free = slot(table, pinned::AGENT_SLOT).load(Ordering::SeqCst) == 0
            && slot(table, pinned::VBI_SLOTS + pinned::AGENT_SLOT).load(Ordering::SeqCst) == 0;
        let client = slot(table, pinned::GAME_CLIENT_SLOT).load(Ordering::SeqCst) as u64;
        o.game_client_ok = client != 0 && covered(&ranges, client, 8) && read_u64(client) == pinned::GAME_CLIENT_VTABLE;
    }
    o
}

fn arm(table: u64) -> bool {
    // SAFETY: single-threaded initialisation before the pointer is published.
    unsafe {
        VTABLE_LIVE = [no_op as *const () as usize, no_op as *const () as usize, on_move as *const () as usize, no_op as *const () as usize];
        HANDLER_LIVE = Handler { vtable: std::ptr::addr_of!(VTABLE_LIVE) };
    }
    epoch();
    TABLE.store(table, Ordering::SeqCst);
    ARMED.store(true, Ordering::SeqCst);
    // SAFETY: table verified mapped; the slot was observed null. Compare-exchange so a
    // concurrent writer is never overwritten.
    let won = unsafe { slot(table, pinned::AGENT_SLOT) }
        .compare_exchange(0, handler_address(), Ordering::SeqCst, Ordering::SeqCst)
        .is_ok();
    if !won {
        ARMED.store(false, Ordering::SeqCst);
    }
    won
}

fn worker(config: Config) {
    let mut log = Log::open(&config.log);
    log.line(&format!("X4MP_AGENT START pid={} log={}", std::process::id(), config.log.display()));
    if !verify_build(&mut log) {
        return;
    }
    if let Some(sched) = config.sched {
        log.line(&format!(
            "X4MP_AGENT SCHED CONFIG mode=fixed{} render={:?} max_catchup_steps={} max_render_gap_ms={} max_lag_ms={} wait_ms={} stall_ms={}",
            sched.policy.hz,
            sched.policy.render,
            sched.policy.max_catchup_steps,
            sched.policy.max_render_gap_ms,
            sched.policy.max_lag_ms,
            sched.wait_ms,
            sched.stall_ms
        ));
        sched_live::init(sched);
    }
    let start = Instant::now();
    let mut gate = ArmGate::new(config.settle_ms);
    let mut last_wait = "";
    loop {
        if config.disarm_file.exists() {
            log.line("X4MP_AGENT NOT_ARMED reason=disarm_file_present");
            return;
        }
        let o = observe(start);
        match gate.observe(&o) {
            Gate::Wait(reason) => {
                if reason != last_wait {
                    log.line(&format!("X4MP_AGENT WAIT reason={reason}"));
                    last_wait = reason;
                }
            }
            Gate::Refuse(reason) => {
                log.line(&format!("X4MP_AGENT REFUSED reason={reason}"));
                return;
            }
            Gate::Arm => {
                // SAFETY: observe() just verified the table is mapped.
                let table = unsafe { read_u64(pinned::VBI_TABLE_GLOBAL) };
                if arm(table) {
                    log.line(&format!("X4MP_AGENT ARMED slot={} table=0x{table:x}", pinned::AGENT_SLOT));
                    break;
                }
                log.line("X4MP_AGENT REFUSED reason=agent_slot_taken_during_arm");
                return;
            }
        }
        std::thread::sleep(Duration::from_millis(250));
    }
    let mut prev = snapshot();
    let mut prev_sched = sched_live::snapshot();
    let mut last_phase = sched_live::phase();
    let mut watchdog = sched_live::Watchdog::new();
    let mut prev_at = Instant::now();
    loop {
        // Poll the stall watchdog several times per cadence period.
        let period = Duration::from_millis(config.cadence_ms);
        let slice = Duration::from_millis(250).min(period);
        let started = Instant::now();
        while started.elapsed() < period {
            std::thread::sleep(slice.min(period.saturating_sub(started.elapsed())).max(Duration::from_millis(1)));
            if let Some(line) = watchdog.poll() {
                log.line(&line);
                disarm(Disarm::SchedulerGuard);
                break;
            }
        }
        if config.disarm_file.exists() {
            disarm(Disarm::DisarmFile);
        }
        let cur = snapshot();
        let cur_sched = sched_live::snapshot();
        let wall_ms = prev_at.elapsed().as_millis() as u64;
        log.line(&cadence_line(&prev, &cur, wall_ms));
        let phase = sched_live::phase();
        if phase != last_phase {
            log_phase(&mut log, phase);
            last_phase = phase;
        }
        if config.sched.is_some() {
            log.line(&sched_line(phase.name(), &prev_sched, &cur_sched, wall_ms));
        }
        prev = cur;
        prev_sched = cur_sched;
        prev_at = Instant::now();
        let code = DISARM.load(Ordering::SeqCst);
        if code != 0 {
            let phase = sched_live::phase();
            if phase != last_phase {
                log_phase(&mut log, phase);
            }
            log.line(&format!(
                "X4MP_AGENT DISARMED reason={} sched={} sched_reason={} restore={}",
                Disarm::from_code(code).reason(),
                phase.name(),
                sched_live::fault().reason(),
                sched_live::restore_note()
            ));
            return;
        }
    }
}

fn log_phase(log: &mut Log, phase: sched_live::Phase) {
    use sched_live::Phase;
    match phase {
        Phase::Active => log.line(&format!(
            "X4MP_AGENT SCHED ARMED dt=1/{} step_const=0x{:x} patch=0x{:x},0x{:x},0x{:x} decision=FrameState+0x{:x} flip_flag=FrameState+0x{:x}(untouched)",
            sched_live::config().map(|c| c.policy.hz).unwrap_or(0),
            sched_live::step_constant(),
            pinned::DECISION_CMP_VA,
            pinned::DT_LOAD_VA,
            pinned::DT_MIN_VA,
            pinned::FRAME_STATE_DECISION_BYTE,
            pinned::FRAME_STATE_RENDER_FLAG
        )),
        Phase::Refused => {
            log.line(&format!("X4MP_AGENT SCHED REFUSED reason={} restore={}", sched_live::fault().reason(), sched_live::restore_note()))
        }
        Phase::Faulted => {
            log.line(&format!("X4MP_AGENT SCHED FAULT reason={} restore={}", sched_live::fault().reason(), sched_live::restore_note()))
        }
        Phase::Restored => log.line(&format!("X4MP_AGENT SCHED RESTORED restore={}", sched_live::restore_note())),
        Phase::Pending | Phase::Off => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn covered_requires_one_range_to_contain_the_whole_span() {
        let ranges = [(0x1000, 0x2000), (0x2000, 0x3000)];
        assert!(covered(&ranges, 0x1000, 0x1000));
        assert!(!covered(&ranges, 0x1ff8, 0x10)); // spans two mappings: refuse
        assert!(!covered(&ranges, 0x3000, 1));
        assert!(!covered(&ranges, u64::MAX - 2, 8));
    }

    #[test]
    fn own_maps_are_parsed() {
        let ranges = readable_ranges();
        let here = covered as *const () as usize as u64;
        assert!(covered(&ranges, here, 16));
    }
}
