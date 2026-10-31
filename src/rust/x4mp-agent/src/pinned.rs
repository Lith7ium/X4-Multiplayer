//! The one Linux X4 build this agent supports, and what must be found in it before the
//! agent may touch anything.
//!
//! Target: X4 Foundations 9.00 (611726), Steam build 23660954, Linux ELF64 x86-64
//! `ET_EXEC` (fixed image base 0x400000, no ASLR for the main image). Every address
//! below is an ELF virtual address in that file and is valid **only** for it. None of
//! the Windows RVAs apply, and none of these addresses may be reused on another build.
//!
//! A signature site is `(va, length, SHA-256 of those bytes)`. Raw engine bytes are not
//! stored. Evidence and confidence for each address are in
//! `reports/LINUX-AGENT-MAP-20261025.txt`.

use crate::sha256::{hex, Sha256};

pub const GAME_VERSION: &str = "9.00 (611726)";
pub const STEAM_BUILD: u32 = 23_660_954;
pub const FILE_SIZE: u64 = 60_459_280;
pub const FILE_SHA256: &str = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
/// `.note.gnu.build-id` descriptor (20 bytes) at [`BUILD_ID_NOTE_VA`].
pub const BUILD_ID: &str = "77cf7ac0ff32d88ffd3db7eb6ea2e025aecb711d";
pub const BUILD_ID_NOTE_VA: u64 = 0x4002fc;
pub const IMAGE_BASE: u64 = 0x400000;

// --- Engine data the agent reads or writes (all verified through the sites below) ---

/// Global `XLib::VbiTable*` (in `.bss`). Read by `MoveWorker::Run` at 0x1212653.
pub const VBI_TABLE_GLOBAL: u64 = 0x3dbc4d8;
/// Table: `[0..17)` per-frame handlers, `+0x88` loading bank, `+0x110` render bank.
pub const VBI_SLOTS: usize = 17;
pub const VBI_TABLE_SIZE: usize = 0x138;
pub const VBI_LOADING_BANK: usize = 0x88;
/// `int` loading flag, set to 1 by the game start/load routine (0xf9a050) and cleared
/// on completion (0xf4aa07). While set, `MoveWorker` still calls per-frame handlers
/// that have no loading-bank entry; the agent then counts a loading skip.
pub const VBI_LOADING_FLAG: usize = 0x130;
/// Per-frame slot claimed by the agent. Statically empty on Linux (writers exist only
/// for 1, 3, 4, 6, 8, 9, 10, 11, 14, 16). Windows work uses 5, 7 and 12; 13 runs after
/// slot 3 (GameClient/universe), 6 (UI) and 11 (JobEngine), before 14 (Steam).
pub const AGENT_SLOT: usize = 13;
/// Slot 3 holds the `X4::GameClient` object itself (primary vtable below).
pub const GAME_CLIENT_SLOT: usize = 3;
pub const GAME_CLIENT_VTABLE: u64 = 0x3ab12a0;

/// TimerBase global block. `+0x00` recursive pthread mutex (0x28 bytes); fields follow
/// the Windows layout shifted by +8 (CRITICAL_SECTION is 0x28 vs. 0x30 here).
pub const TIMER_BASE: u64 = 0x3db8280;
pub const TIMER_GAME_TIME: u64 = TIMER_BASE + 0x30; // f64, only FrameTick/SetGameTime write
pub const TIMER_REAL_TIME: u64 = TIMER_BASE + 0x38; // f64, unscaled, runs while paused
pub const TIMER_UNPAUSED_REAL_TIME: u64 = TIMER_BASE + 0x40; // f64
pub const TIMER_SPEED: u64 = TIMER_BASE + 0x58; // f64, 0 paused, 1 normal, <=6 SETA
pub const TIMER_PAUSE_COUNT: u64 = TIMER_BASE + 0x64; // i32
pub const TIMER_TEMP_PAUSE_COUNT: u64 = TIMER_BASE + 0x68; // i32

/// Executable static-TLS offsets (local-exec model, fixed for the ET_EXEC main image).
/// `CurTime` (game time seen by this thread, synced at the start of every MoveWorker
/// iteration) and its previous value.
pub const TLS_CUR_TIME: i32 = -0x2728;
pub const TLS_PREV_TIME: i32 = -0x2628;
/// Component view selector; MoveWorker and Pathing Manager set it to 1 at thread start.
pub const TLS_VIEW_SELECTOR: i32 = -0x2620;
/// `pthread_setname_np` name given by `MoveWorker::Run` (string at 0x2d4eb7e).
pub const MOVE_WORKER_THREAD_NAME: &[u8] = b"Movement worker";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Site {
    pub name: &'static str,
    pub va: u64,
    pub len: usize,
    pub sha256: &'static str,
}

pub const SITES: &[Site] = &[
    Site { name: "move_worker_run_entry", va: 0x1212490, len: 0x35, sha256: "b09c6bed2ccf279993e1f55142bfa88c501af14f776503a2cdba90023afd3c5d" },
    Site { name: "move_worker_frame_start", va: 0x12125a0, len: 0x80, sha256: "57db462588f8cf62bf9dbac327343157a2e2484b2e367a261bca245102f29d5c" },
    Site { name: "move_worker_vbi_dispatch", va: 0x1212653, len: 0x5d, sha256: "7ff3eedea62f3e1d21f05452ceea7f367f4edf5ab2b9655245ae69d32940680f" },
    Site { name: "vbi_unregister", va: 0x1a3b9c0, len: 0x79, sha256: "b4f1e93fba9acedc979030826b33fb01234dd9899a05a04b2b6d98e25ed584fc" },
    Site { name: "vbi_table_alloc", va: 0xfcecb1, len: 0x56, sha256: "bec46cb484d0dd404ab933fb5920bd03c6933a039a5d256fbfde16ab907cb433" },
    Site { name: "vbi_loading_enter", va: 0xf9a044, len: 0x23, sha256: "80752963d9ea194a82fe4b41d9b96656c9088f4f8b4b05628a067bf3e9a21ee2" },
    Site { name: "vbi_loading_leave", va: 0xf4a9f0, len: 0x2a, sha256: "211c950bdf4effc130c056c3660f5f8932a8d182c4ea94f4389642511482b03c" },
    Site { name: "frame_tick_time_advance", va: 0x2025260, len: 0x95, sha256: "77e65fe042991e20e6af69ae8304a7222b7330fd1eae4ca9e6d85c473c9ae149" },
    Site { name: "timer_is_paused", va: 0x1a3b800, len: 0x42, sha256: "571df2fed03b0652ab04b0c79858d612b220366fdf20192418a96ce862fce53e" },
    Site { name: "export_is_game_paused", va: 0xc1ea20, len: 0xc, sha256: "5a68f009952d0267dde65837f1fbdd56a3b77a483fa07ff6c6890697d5b89166" },
    Site { name: "export_get_current_game_time", va: 0xa41960, len: 0x22, sha256: "71cbbbd1de9b38ea8eb8de9352d579e1e383708fc9b946b745432ab96c7287cd" },
    Site { name: "game_client_vbi_register", va: 0xfcf94f, len: 0x19, sha256: "941bf1e9d4f20e6506b877ba12546a4c83aaf592a2e1457c417008abd9159542" },
    Site { name: "ui_vbi_register", va: 0xa847ff, len: 0x30, sha256: "d5cca94ca0fbadd00e9b30260f066fc63ccc348a116501899e361bf6583c7098" },
    Site { name: "game_client_vtable_head", va: 0x3ab12a0, len: 0x18, sha256: "a4f00599507bf8d9dc0c3cd3e8684dcd3c64541c61882585f21466f497c8f70f" },
    Site { name: "ui_vbi_vtable", va: 0x3ab4978, len: 0x20, sha256: "53a305469a33eddf4bbb44ef521359d55d672b7753953df90aa88a424787f64f" },
    Site { name: "move_worker_vtable_head", va: 0x3b4cd58, len: 0x18, sha256: "268f9c510fedb221e11681efbbad4167f5d1f7cf2a55d2383431553297a9e34b" },
    // --- Fixed-step scheduler (src/scheduler.rs); evidence in reports/SIM-DECOUPLE-SCHEDULER.txt ---
    // FrameTick: dt from the clock, movie-capture override, publish, graphics state,
    // semSimStart release, both `cmp byte [fs+0x200],0` render decisions.
    Site { name: "frame_tick_dt_render_decision", va: 0x2025110, len: 0x150, sha256: "0ce60b4f123c4904b3c34de83e7ac4aeb73edf3cd2c78d76446c7f98613b9e1e" },
    // FrameTick no-render branch: renderer frame advance (0x21e78d0) + view finalize (0x2022810).
    Site { name: "frame_tick_no_render_path", va: 0x2025420, len: 0x30, sha256: "d882197f42e7cff821a016dc1cc0a5028352e5a28f9e6309f85f31c3552219b7" },
    // Main loop: frame-in-flight test, 16 ms semSimDone wait + clear, FrameTick call + set.
    Site { name: "main_loop_in_flight_check", va: 0x72f638, len: 0x1a, sha256: "c525558c3a509c9f81e553154c26796d821c557107303ac26485f377f112d7f9" },
    Site { name: "main_loop_sim_done_wait", va: 0x72f745, len: 0x6c, sha256: "0db4aedf3402446323a03bfe2e8ad1ac494d73f78cbfd9cfbf2c11704ad2d74b" },
    Site { name: "main_loop_frame_tick_call", va: 0x72faf0, len: 0x32, sha256: "d2dc7882f6f30bd1b9272eae441199355f71ee8c92367d7e3df48f6d13e4b285" },
    // FrameState: 0x2f0-byte allocation, constructor call and store to the global.
    Site { name: "frame_state_alloc", va: 0xfd10bc, len: 0x23, sha256: "08db467161b2dcb7be44606f78e1913efe5c7522f1f7eba3dfacb697fb453065" },
    // FrameState constructor: render flag (+0x200) = 1, +0x210 = 0.
    Site { name: "frame_state_ctor_render_flag", va: 0x201d8e2, len: 0xe, sha256: "d15a01cf3ad3c5ae5aa3a691629d543ee431b1486d739639d0c63686f5c8f2e0" },
    // Read-only double 1/60 in .rodata (target of the patched dt load).
    Site { name: "rodata_one_sixtieth", va: 0x2e24578, len: 8, sha256: "c6c9a57771f2313713756c05b0b8a1413e5ef99d747cabf716952b4f6137f34b" },
    // Read-only double 1/20 in .rodata (dt for fixed20).
    Site { name: "rodata_one_twentieth", va: 0x2e270c0, len: 8, sha256: "7f925b2036e92fe2f1f0697c3aedfff74d719c1ee2cb6b739d7c30484e75972c" },
];

// --- Fixed-step scheduler data (all inside the sites above) ---

/// Global `FrameState*` (main-thread frame object, 0x2f0 bytes), set once by 0xfd10d8.
pub const FRAME_STATE_GLOBAL: u64 = 0x3daaaf0;
/// `bool` in FrameState, set to 1 by the constructor and never written again; FrameTick
/// reads it twice. Read #1 (0x2025162) gates the pre-render calls 0x224ceb0 and
/// **0x218e2c0, which flips the render-list double buffer** (index 0x72a0fa0, halves of
/// 0x16a9b00 bytes at 0x454d9a0) and resets its per-frame lists and counters. Read #2
/// (0x2025208) chooses the render branch or the engine's no-render branch (0x2025430).
/// The scheduler must never clear this flag: without the flip the producer half grows
/// every frame (native freeze 25 Oct 2026). It only reads it as a guard.
pub const FRAME_STATE_RENDER_FLAG: u64 = 0x200;
/// FrameState padding byte after the render flag (no access to +0x201..+0x203 anywhere;
/// +0x204 is a dword). With the decision patch, read #2 tests this byte, which the
/// scheduler owns; read #1 keeps testing the engine's flag, so the flip runs every frame.
pub const FRAME_STATE_DECISION_BYTE: u64 = 0x201;
/// Read #2: `cmp byte ptr [rbx+0x200],0` (7 bytes) + first byte of the following `je`,
/// one naturally aligned qword. Patched: the same compare on [rbx+0x201].
pub const DECISION_CMP_VA: u64 = 0x2025208;
pub const DECISION_CMP_ORIGINAL: [u8; 8] = [0x80, 0xbb, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0f];
pub const DECISION_CMP_PATCHED: [u8; 8] = [0x80, 0xbb, 0x01, 0x02, 0x00, 0x00, 0x00, 0x0f];
/// Main-loop `bool` (initial value 1 in .data, so the first wait consumes semSimDone's
/// initial count): 1 from FrameTick's return (0x72fb1b) until the main thread acquires
/// semSimDone (cleared at 0x72f7aa). Seen as 1 from VBI slot 13, the main thread is
/// parked outside FrameTick and cannot enter it before this simulation frame ends.
pub const MAIN_FRAME_IN_FLIGHT: u64 = 0x3d8d97b;
/// Main-loop appState (byte; bit 1 = synchronous loading, read at 0x72f756). While set the
/// MoveWorker runs no frames; the stall watchdog does not count that time.
pub const MAIN_APP_STATE: u64 = 0x3daab04;
/// Movie-capture flag and its frame time (FrameTick 0x2025126 / 0x202512d).
pub const MOVIE_CAPTURE_FLAG: u64 = 0x4533d38;
/// `double 1/60` and `double 1/20` in .rodata. The image has no 1/30; fixed30 points the
/// dt load at a read-only page of the agent's own (see `sched_live`).
pub const ONE_SIXTIETH_CONST: u64 = 0x2e24578;
pub const ONE_TWENTIETH_CONST: u64 = 0x2e270c0;
/// Candidate addresses for the agent's constant page: below the image (0x400000), far
/// from the brk heap after .bss, within rip-relative reach of [`DT_LOAD_VA`].
pub const CONST_PAGE_CANDIDATES: [u64; 3] = [0x300000, 0x200000, 0x380000];

/// FrameTick's non-capture dt path at 0x2025260 (inside `frame_tick_time_advance`):
/// `movsd xmm1,[rip+0xdff268]` (1.0) ; `minsd xmm1,xmm0` ; `pxor xmm0,xmm0`.
/// Two naturally aligned qwords, each replaced with one atomic store:
/// 1. the load's displacement is retargeted to a `double 1/N` (dt = min(1/N, dt));
/// 2. the `minsd` becomes a 4-byte NOP (dt = 1/N exactly).
///
/// Every intermediate state is a valid dt (never 1.0); restore runs in reverse order.
pub const DT_LOAD_VA: u64 = 0x2025260;
pub const DT_MIN_VA: u64 = 0x2025268;
pub const DT_PAGE: u64 = 0x2025000;
pub const DT_LOAD_ORIGINAL: [u8; 8] = [0xf2, 0x0f, 0x10, 0x0d, 0x68, 0xf2, 0xdf, 0x00];
pub const DT_MIN_ORIGINAL: [u8; 8] = [0xf2, 0x0f, 0x5d, 0xc8, 0x66, 0x0f, 0xef, 0xc0];
pub const DT_MIN_PATCHED: [u8; 8] = [0x0f, 0x1f, 0x40, 0x00, 0x66, 0x0f, 0xef, 0xc0];

/// The patched dt load: same instruction, displacement to `target` (a `double 1/N`).
/// `None` when `target` is out of rip-relative reach.
pub fn dt_load_patched(target: u64) -> Option<[u8; 8]> {
    let disp = i32::try_from(target as i64 - (DT_LOAD_VA as i64 + 8)).ok()?;
    let d = disp.to_le_bytes();
    Some([0xf2, 0x0f, 0x10, 0x0d, d[0], d[1], d[2], d[3]])
}

/// Read access to the executable by virtual address: the live process in the agent,
/// the ELF file on disk in the offline checker, or a fake in tests.
pub trait Image {
    fn read(&self, va: u64, len: usize) -> Option<Vec<u8>>;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Check {
    Ok,
    Unreadable,
    Mismatch(String),
}

pub fn digest(bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    hex(&hasher.finish())
}

pub fn check_site(image: &dyn Image, site: &Site) -> Check {
    match image.read(site.va, site.len) {
        Some(bytes) if bytes.len() == site.len => {
            let found = digest(&bytes);
            if found == site.sha256 {
                Check::Ok
            } else {
                Check::Mismatch(found)
            }
        }
        _ => Check::Unreadable,
    }
}

pub fn check_build_id(image: &dyn Image) -> Check {
    // Elf64_Nhdr {namesz=4, descsz=20, type=NT_GNU_BUILD_ID(3)} "GNU\0" + 20-byte id.
    match image.read(BUILD_ID_NOTE_VA, 36) {
        Some(note) if note.len() == 36 => {
            let header_ok = note[..16] == [4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, b'G', b'N', b'U', 0];
            let found = hex(&note[16..]);
            if header_ok && found == BUILD_ID {
                Check::Ok
            } else {
                Check::Mismatch(found)
            }
        }
        _ => Check::Unreadable,
    }
}

/// Number of verified sites when the build-id and every site match; otherwise the first
/// failing check as `name:reason`.
pub fn verify_sites(image: &dyn Image) -> Result<usize, String> {
    match check_build_id(image) {
        Check::Ok => {}
        Check::Unreadable => return Err("build_id:unreadable".into()),
        Check::Mismatch(found) => return Err(format!("build_id:mismatch:{found}")),
    }
    for site in SITES {
        match check_site(image, site) {
            Check::Ok => {}
            Check::Unreadable => return Err(format!("{}:unreadable", site.name)),
            Check::Mismatch(found) => return Err(format!("{}:mismatch:{found}", site.name)),
        }
    }
    Ok(SITES.len())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    /// Synthetic image holding bytes whose hashes we control.
    struct Fake(HashMap<u64, Vec<u8>>);
    impl Image for Fake {
        fn read(&self, va: u64, len: usize) -> Option<Vec<u8>> {
            self.0.get(&va).filter(|b| b.len() == len).cloned()
        }
    }

    #[test]
    fn site_table_is_well_formed() {
        let mut names = std::collections::HashSet::new();
        for site in SITES {
            assert!(names.insert(site.name), "duplicate {}", site.name);
            assert_eq!(site.sha256.len(), 64);
            assert!(site.sha256.bytes().all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()));
            assert!(site.len >= 8 && site.len <= 0x200, "{}", site.name);
            assert!(site.va >= IMAGE_BASE && site.va < 0x3e00000, "{}", site.name);
        }
        for (i, a) in SITES.iter().enumerate() {
            for b in &SITES[i + 1..] {
                let disjoint = a.va + a.len as u64 <= b.va || b.va + b.len as u64 <= a.va;
                assert!(disjoint, "{} overlaps {}", a.name, b.name);
            }
        }
        assert!(AGENT_SLOT < VBI_SLOTS && ![5, 7, 12, 1, 3, 4, 6, 8, 9, 10, 11, 14, 16].contains(&AGENT_SLOT));
        assert_eq!(VBI_LOADING_BANK, VBI_SLOTS * 8);
    }

    #[test]
    fn dt_patch_is_well_formed() {
        let site = SITES.iter().find(|s| s.name == "frame_tick_time_advance").unwrap();
        // Both qwords are naturally aligned, adjacent, on one page and inside the pinned site.
        assert_eq!((DT_LOAD_VA % 8, DT_MIN_VA % 8, DT_MIN_VA - DT_LOAD_VA), (0, 0, 8));
        assert_eq!((DT_LOAD_VA & !0xfff, DT_MIN_VA & !0xfff), (DT_PAGE, DT_PAGE));
        assert!(site.va <= DT_LOAD_VA && DT_MIN_VA + 8 <= site.va + site.len as u64);
        // The original load reads the 1.0 clamp at 0x2e244d0; the patch reads 1/60.
        let target = |q: [u8; 8]| DT_LOAD_VA as i64 + 8 + i32::from_le_bytes([q[4], q[5], q[6], q[7]]) as i64;
        assert_eq!(target(DT_LOAD_ORIGINAL), 0x2e244d0);
        for c in [ONE_SIXTIETH_CONST, ONE_TWENTIETH_CONST, CONST_PAGE_CANDIDATES[0], CONST_PAGE_CANDIDATES[1], CONST_PAGE_CANDIDATES[2]] {
            let q = dt_load_patched(c).unwrap();
            assert_eq!(target(q) as u64, c);
            assert_eq!(q[..4], DT_LOAD_ORIGINAL[..4]); // same movsd xmm1,[rip+d]
        }
        assert!(dt_load_patched(0x7fff_0000_0000).is_none());
        // Read #2 is one aligned qword; the patch only changes the displacement byte.
        let site = SITES.iter().find(|s| s.name == "frame_tick_dt_render_decision").unwrap();
        assert_eq!(DECISION_CMP_VA % 8, 0);
        assert!(site.va <= DECISION_CMP_VA && DECISION_CMP_VA + 8 <= site.va + site.len as u64);
        let diff: Vec<usize> = (0..8).filter(|&i| DECISION_CMP_ORIGINAL[i] != DECISION_CMP_PATCHED[i]).collect();
        assert_eq!(diff, vec![2]);
        assert_eq!(DECISION_CMP_PATCHED[2] as u64, FRAME_STATE_DECISION_BYTE & 0xff);
        let twentieth = SITES.iter().find(|s| s.name == "rodata_one_twentieth").unwrap();
        assert_eq!(digest(&(1.0f64 / 20.0).to_le_bytes()), twentieth.sha256);
        // minsd xmm1,xmm0 becomes a 4-byte NOP; the following pxor is unchanged.
        assert_eq!(DT_MIN_PATCHED[..4], [0x0f, 0x1f, 0x40, 0x00]);
        assert_eq!(DT_MIN_PATCHED[4..], DT_MIN_ORIGINAL[4..]);
        let rodata = SITES.iter().find(|s| s.name == "rodata_one_sixtieth").unwrap();
        assert_eq!(rodata.va, ONE_SIXTIETH_CONST);
        assert_eq!(digest(&(1.0f64 / 60.0).to_le_bytes()), rodata.sha256);
    }

    #[test]
    fn build_id_check_requires_exact_note() {
        let mut note = vec![4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, b'G', b'N', b'U', 0];
        let id: Vec<u8> = (0..20).map(|i| u8::from_str_radix(&BUILD_ID[i * 2..i * 2 + 2], 16).unwrap()).collect();
        note.extend_from_slice(&id);
        let ok = Fake([(BUILD_ID_NOTE_VA, note.clone())].into());
        assert_eq!(check_build_id(&ok), Check::Ok);
        note[35] ^= 1;
        assert!(matches!(check_build_id(&Fake([(BUILD_ID_NOTE_VA, note)].into())), Check::Mismatch(_)));
        assert_eq!(check_build_id(&Fake(HashMap::new())), Check::Unreadable);
    }

    #[test]
    fn verify_fails_closed_on_first_bad_or_missing_site() {
        // No real engine bytes here: every site is unreadable, so verification refuses.
        let mut note = vec![4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, b'G', b'N', b'U', 0];
        note.extend((0..20).map(|i| u8::from_str_radix(&BUILD_ID[i * 2..i * 2 + 2], 16).unwrap()));
        let image = Fake([(BUILD_ID_NOTE_VA, note)].into());
        assert_eq!(verify_sites(&image), Err(format!("{}:unreadable", SITES[0].name)));
        let site = Site { name: "t", va: 0x1000, len: 3, sha256: "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" };
        assert_eq!(check_site(&Fake([(0x1000, b"abc".to_vec())].into()), &site), Check::Ok);
        assert!(matches!(check_site(&Fake([(0x1000, b"abd".to_vec())].into()), &site), Check::Mismatch(_)));
    }
}
