/* Server frame diet: dedicated server mode for X4 9.00 / Steam build 23660954 (Windows).
 *
 * A dedicated X4 server presents nothing to anyone, but its MoveWorker still spends a large
 * share of every frame on presentation (8-sector attention run, 25 Oct 2026, simprof folds:
 * slot 6 Anark UI 5.3 ms/frame at baseline of which Lua is 0.6 ms; slot 10 camera render
 * preparation 2.1 ms). This module skips selected presentation work while the simulation, MD,
 * AI, input and every UI Lua script (the mod's networking runs there) keep running unchanged.
 * Analysis and evidence: reports/REWRITE-FRAME-DIET.md.
 *
 * Sites (data-only except ui_elements: vtable entries compare-exchanged from the verified original,
 * one empty VBI slot, one engine-native render flag bit; ui_elements retargets one call's rel32):
 *   ui_scenes  AnarkScene vtable slot 12 (0xaf8760), the per-frame layer walk that the scene
 *              update (slot 5, 0x13aa510) tail-calls after clearing the scene's per-frame lists.
 *              Skipped only inside AnarkSceneManager slot 3 (0xaf8e50) on the MoveWorker, so the
 *              lists stay empty, which is the engine's own state for a scene whose root is not
 *              visible. The lists are consumed only by the UI render in slot 10.
 *   ui_render  CUIController::Render (0xb3bba0, render bank 1, slot 10). Optional (see report:
 *              per-frame UI queues must be checked for growth natively before it is a default).
 *   camera     U::Camera::Render (vtable slot 3, 0x983160) inside GameClient's render bank
 *              (0x122bfc0). Optional and NOT behaviour-identical: the render walk draws from the
 *              MoveWorker's TLS RNG (TLS+0x318) and spawns mass-traffic welding effects with
 *              UniverseIDs, both as a function of the camera view. `audit` mode measures it.
 *   draw       RenderFlags bit disable_rendering (0x8000000 of qword 0x6adfa30 and its staged
 *              copy 0x6adfa38), an engine debug flag tested only by the main thread's
 *              RenderPreviousFrame (0xfa007a). Main thread / GPU only, no MoveWorker effect.
 *   ui_elements  the Anark element tree update 0x1399c40 (~1.9 ms/frame): a direct call at 0x138f477 inside
 *              0x138f2c0, whose caller also runs the per-frame Lua dispatch, so the call itself is
 *              retargeted (CODE PATCH: the 4-byte rel32 of that E8, verified against the original
 *              target, compare-exchanged, re-verified every frame, restored on disarm) to a near stub
 *              that jumps to our wrapper. Audit/pass only: native bisect-001 proved skipping this
 *              update stops UI Lua (including networking). Diet mode refuses this site.
 *   rng_split  GameRender::Move (VBI slot 10, 0x121c1f0) runs with its own thread-RNG state: the
 *              MoveWorker's TLS+0x318 is swapped with a private render stream around the call, so
 *              view-dependent draws of the render walk never reach the simulation stream. Applies
 *              in every mode (it is a determinism fix, not a skip); effect objects the render walk
 *              creates still take UniverseIDs.
 * The per-frame guard runs in empty VBI slot 2 (before GameClient::Move): it re-verifies the
 * table and every installed entry and disarms everything on any change.
 *
 * Threads: the module arms only on the MoveWorker (Lua runs there outside loading screens). On
 * loading screens the UI, and so Lua (stop, __gc, set), runs on the main thread while the MoveWorker
 * keeps calling the slot 2 guard. Every change of the installed state (arm, disarm, set, the guard's
 * checks) therefore runs under one lock; the guard only tries it and skips a frame when it is held.
 *
 * Modes: `diet` skips the active sites, `pass` keeps every wrapper installed but inert (A/B
 * timing within one session), `audit` runs the originals and measures their side effects on the
 * simulation: MoveWorker TLS RNG steps (exact, the LCG is a bijection), UniverseID insertions
 * (AutoIDMap +0x38), the deferred event list size, TLS game time, and a snapshot/compare of the
 * first 0x200 bytes (U::Component) of a rotating subset of all registered components. A
 * `control` window of equal length at frame start measures what other threads change meanwhile.
 *
 * Lua: start(mode, sites[, period[, audit_every[, digest_max]]]) -> handle | nil, reason;
 * handle:set(mode[, sites]), handle:stats(), handle:active() -> armed, stalls, handle:stop() (also __gc).
 * active() is the UI Lua heartbeat: if UI Lua stops calling it for STALL_MS (and STALL_MIN_FRAMES
 * frames) while sites are skipped, the guard reverts the owner to pass mode and counts a stall.
 * Further diet requests for that owner are refused; a recovered watchdog is not a successful diet.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fixedtick_signatures.h"

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *);
#define API(ret, name, args) static ret (*p_##name) args
API(int, lua_gettop, (lua_State *));
API(int, lua_type, (lua_State *, int));
API(const char *, lua_tolstring, (lua_State *, int, size_t *));
API(double, lua_tonumber, (lua_State *, int));
API(void, lua_settop, (lua_State *, int));
API(int, lua_pushthread, (lua_State *));
API(void, lua_pushvalue, (lua_State *, int));
API(void, lua_pushnil, (lua_State *));
API(void, lua_pushboolean, (lua_State *, int));
API(void, lua_pushnumber, (lua_State *, double));
API(void, lua_pushlstring, (lua_State *, const char *, size_t));
API(void, lua_pushcclosure, (lua_State *, lua_CFunction, int));
API(void, lua_getfield, (lua_State *, int, const char *));
API(void, lua_setfield, (lua_State *, int, const char *));
API(void, lua_createtable, (lua_State *, int, int));
API(void *, lua_newuserdata, (lua_State *, size_t));
API(int, lua_setmetatable, (lua_State *, int));
API(int, luaL_newmetatable, (lua_State *, const char *));
API(void *, luaL_checkudata, (lua_State *, int, const char *));

#define REGISTRY (-10000)
#define LUA_TNIL 0
#define LUA_TNUMBER 3
#define LUA_TSTRING 4
#define META "x4lan.framediet.owner.v1"
#define VBI_RVA 0x6cf1570u
#define VBI_SLOT 2
#define LOADING_OFFSET 0x130
#define RENDER_FLAGS_RVA 0x6adfa30u          /* RenderFlags in use (main-thread render reads it) */
#define RENDER_FLAGS_STAGED_RVA 0x6adfa38u   /* copied over 0x6adfa30 by ProcessGraphicsStateChanges */
#define DISABLE_RENDERING 0x8000000ull
#define ID_MAP_PTR_RVA 0x6cf1500u           /* g_UniverseIDMap (XLib::AutoIDMap<U::UniverseID>*) */
#define ID_MAP_INSERTS 0x38                 /* u64, incremented by InsertAtSlot (0xcc64d) */
#define ID_MAP_BLOCKS 0x58                  /* 32 block pointers, 2^20 slots each */
#define EVENT_LIST_RVA 0x39943a8u           /* U::g_EventCommandQueue (std::list, size at +8) */
#define TLS_GAME_TIME 0x300
#define TLS_RNG 0x318
#define COMPONENT_WINDOW 0x200              /* sizeof(U::Component) */
#define REFCOUNT_OFFSET 0x40                /* +0x40 refcount / +0x44 lifecycle: other threads */
#define RNG_STEP_BOUND 65536u
#define HISTOGRAM_WORDS (COMPONENT_WINDOW / 8)

enum { FD_UI_SCENES = 1, FD_UI_RENDER = 2, FD_CAMERA = 4, FD_DRAW = 8, FD_RNG_SPLIT = 16, FD_UI_ELEMENTS = 32,
       FD_ALL = 15 };
enum { FD_PASS = 0, FD_DIET = 1, FD_AUDIT = 2 };
/* Statistics rows: the three MoveWorker regions plus the control window. */
enum { ST_UI_SCENES, ST_UI_RENDER, ST_CAMERA, ST_UI_ELEMENTS, ST_CONTROL, ST_COUNT };
static const char *const st_names[ST_COUNT] = {"ui_scenes", "ui_render", "camera", "ui_elements", "control"};
/* Wrapped virtuals, in x4_framediet_vtables order (signatures.py FRAMEDIET_VTABLE_ENTRIES). */
enum { VS_SCENE_MANAGER, VS_SCENE_WALK, VS_UI_RENDER, VS_CLIENT_RENDER, VS_CAMERA_RENDER, VS_GAME_RENDER, VS_COUNT };
static const int vs_site[VS_COUNT] = {FD_UI_SCENES, FD_UI_SCENES, FD_UI_RENDER, FD_CAMERA, FD_CAMERA, FD_RNG_SPLIT};

typedef struct {
    uint64_t calls, skipped, passed;
    uint64_t audited, rng_calls, rng_steps, rng_unknown, id_calls, ids, event_calls, events_up;
    uint64_t time_changed;
    double ms_total, ms_max;
    uint64_t digests, digest_components, digest_changed, digest_unreadable;
    uint32_t histogram[HISTOGRAM_WORDS];
    char first[160];
} fd_stats;

typedef struct {
    uint64_t generation;
    uint64_t frames, loading_frames, present_frames, draw_set, draw_reapplied, draw_recleared, guard_skipped;
    /* UI Lua heartbeat: the loader calls active() every UI update. If UI Lua stops calling while sites
       are skipped, the guard reverts the owner to pass mode (the mod's networking runs in UI Lua). */
    uint64_t lua_frames, stalls, stall_frame;
    int64_t lua_qpc;
    int stall_sites;
    uint64_t split_calls, split_render_steps;   /* rng_split: slot 10 calls, render-stream steps */
    /* audit: whole MoveWorker frames (slot 2 to slot 2) on the simulation stream and ID map */
    uint64_t frame_audited, frame_rng_steps, frame_rng_unknown, frame_ids;
    int armed, mode, sites, installed, period, audit_every;
    unsigned digest_max;
    const char *reason;
    fd_stats st[ST_COUNT];
} fd_owner;
/* The Lua userdata holds only the generation; the state itself is static, so a wrapper that is
   still running on another thread never touches memory the Lua GC has freed. */
typedef struct { uint64_t generation; } fd_handle;

typedef struct { void **vtable; } fd_handler;
typedef uintptr_t (*fd_fn1)(void *);
typedef uintptr_t (*fd_fn2)(void *, void *);
typedef uintptr_t (*fd_fn3)(void *, void *, uintptr_t);

/* ui_elements call-site patch (0x138f477: E8 rel32 -> 0x1399c40). */
#define ELEMENT_CALL_RVA 0x138f477u
#define ELEMENT_FN_RVA 0x1399c40u
#define STUB_PAGE 4096
static unsigned char *fd_stub_page;      /* RX page within rel32 reach of the call site; never freed */
static int32_t fd_call_original_rel, fd_call_hook_rel;
static int fd_call_patched;

static unsigned char *image;
static fd_owner fd_state;          /* last owner's state, kept for stats() after stop */
static fd_owner *fd_current;       /* &fd_state while an owner exists, else NULL */
static uint64_t fd_generation;
static void **fd_table;
static DWORD fd_thread;
static int64_t fd_qpf;
static void **fd_entry[VS_COUNT];
static void *fd_orig[VS_COUNT];
static void *fd_wrapper[VS_COUNT];
/* Serialises arm, disarm, set and the guard's checks (see "Threads" above). Not recursive: nothing
   that holds it calls back into Lua or into another locking function. */
static SRWLOCK fd_lock = SRWLOCK_INIT;
static int fd_file_verified;     /* the 55 MB executable hash is computed once per process */
static int fd_draw_owned;        /* the current owner holds disable_rendering */
/* The engine copies RenderFlags with plain loads and stores (options apply 0x121ef1f, graphics state
   changes 0xfa3811), so a copy racing our release can bring the bit back. Nothing in the engine sets
   disable_rendering itself: a bit that reappears within DRAW_WATCH_FRAMES of our release, or that a
   previous owner of this module set, is ours and is cleared again. */
#define DRAW_WATCH_FRAMES 120
static int fd_draw_ours;         /* we set the bit and have not seen it stay clear since */
static int fd_draw_watch;        /* guard frames left in which a reappearing bit is cleared */
#define CONTROL_MAX_US 5000.0    /* control window cap (the mean of the slowest audited region) */
#define STALL_MS 3000.0          /* no UI Lua call for this long ... */
#define STALL_MIN_FRAMES 30      /* ... and this many MoveWorker frames while skipping: revert to pass */
#define FD_SKIP_SITES (FD_UI_SCENES | FD_UI_RENDER | FD_CAMERA | FD_DRAW | FD_UI_ELEMENTS)
static int fd_ctx_scenes, fd_ctx_camera;
static int fd_present;           /* this frame runs every presentation site (period) */
#define FD_RENDER_RNG_SEED 0x52454e4445525631ull   /* "RENDERV1": private slot 10 stream */
static uint64_t fd_render_rng = FD_RENDER_RNG_SEED;
static uint64_t fd_frame_rng, fd_frame_ids;   /* audit: values at the previous slot 2 entry */
static int fd_frame_valid;
static int fd_digest_row = -1;   /* statistics row whose region gets the digest this frame */
static HANDLE fd_main_thread, fd_worker_thread;   /* CPU-time accounting (stats only) */
static unsigned fd_rotation;
static uint64_t fd_digest_cursor;

/* Test seams. */
static BOOL WINAPI fd_qpc_default(LARGE_INTEGER *v) { return QueryPerformanceCounter(v); }
static BOOL (WINAPI *fd_qpc)(LARGE_INTEGER *) = fd_qpc_default;
static unsigned char *fd_tls_default(void) {
    void **slots = *(void ***)((unsigned char *)NtCurrentTeb() + 0x58); /* ThreadLocalStoragePointer */
    return slots ? (unsigned char *)slots[0] : NULL;                       /* X4.exe's _tls_index is 0 */
}
static unsigned char *(*fd_tls)(void) = fd_tls_default;
static void fd_spin_default(double us) {
    LARGE_INTEGER a, b;
    if (!fd_qpc(&a)) return;
    do { YieldProcessor(); if (!fd_qpc(&b)) return; }
    while ((double)(b.QuadPart - a.QuadPart) * 1e6 / (double)fd_qpf < us);
}
static void (*fd_spin)(double us) = fd_spin_default;
/* 1 on X4's main thread (the process's earliest thread), 0 elsewhere, -1 when it cannot be found. */
static HANDLE fd_find_main_thread(void);
static int fd_on_main_thread_default(void) {
    if (!fd_main_thread) fd_main_thread = fd_find_main_thread();
    if (!fd_main_thread) return -1;
    return GetThreadId(fd_main_thread) == GetCurrentThreadId();
}
static int (*fd_on_main_thread)(void) = fd_on_main_thread_default;

static int readable(const void *ptr, size_t length, int write) {
    MEMORY_BASIC_INFORMATION m;
    uintptr_t start = (uintptr_t)ptr, end = start + length;
    if (!ptr || end < start || !VirtualQuery(ptr, &m, sizeof(m))) return 0;
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    if (end > (uintptr_t)m.BaseAddress + m.RegionSize) return 0;
    DWORD p = m.Protect & 0xff;
    if (write) return p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
                      p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

/* Heap reads for the digest go through ReadProcessMemory on our own process: a component freed
   (and its span decommitted) by another thread meanwhile yields a failed copy, never a fault. */
static BOOL fd_copy_default(void *dst, const void *src, size_t n) {
    SIZE_T got = 0;
    return src && ReadProcessMemory(GetCurrentProcess(), src, dst, n, &got) && got == n;
}
static BOOL (*fd_copy)(void *dst, const void *src, size_t n) = fd_copy_default;

static int digest_matches(const unsigned char digest[32], const char *hex) {
    static const char alphabet[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (hex[2*i] != alphabet[digest[i] >> 4] || hex[2*i+1] != alphabet[digest[i] & 15]) return 0;
    return 1;
}

static int hash_memory(const void *data, ULONG length, const char *expected) {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32];
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0) goto done;
    if (BCryptHashData(hash, (PUCHAR)data, length, 0) < 0) goto done;
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto done;
    ok = digest_matches(digest, expected);
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static int verify_file(void) {
    WCHAR path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    if (!n || n >= 32768) return 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    static unsigned char buffer[65536];
    unsigned char digest[32];
    DWORD got;
    int ok = 0;
    if (!GetFileSizeEx(file, &size) || size.QuadPart != X4_FILE_SIZE) goto done;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) < 0) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) goto done;
        if (!got) break;
        if (BCryptHashData(hash, buffer, got, 0) < 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto done;
    ok = digest_matches(digest, X4_FILE_SHA256);
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    return ok;
}

static int verify_checks(const x4_signature_check *checks, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (!readable(image + checks[i].rva, checks[i].length, 0) ||
            !hash_memory(image + checks[i].rva, checks[i].length, checks[i].sha)) return 0;
    return 1;
}

/* Cheap part: this process's image has the pinned PE header (sets `image`). */
static int verify_headers(void) {
    image = (unsigned char *)GetModuleHandleW(NULL);
    if (!readable(image, sizeof(IMAGE_DOS_HEADER), 0)) return 0;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return 0;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    return readable(nt, sizeof(*nt), 0) && nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt->FileHeader.TimeDateStamp == X4_PE_TIMESTAMP &&
           nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt->OptionalHeader.SizeOfImage == X4_IMAGE_SIZE;
}

/* Full check: headers, the file hash (once per process) and every code signature (each time). */
static int verify_image(void) {
    if (!verify_headers()) return 0;
    if (!fd_file_verified && !(fd_file_verified = verify_file())) return 0;
    return verify_checks(x4_common_checks, sizeof(x4_common_checks) / sizeof(x4_common_checks[0])) &&
           verify_checks(x4_framediet_checks, sizeof(x4_framediet_checks) / sizeof(x4_framediet_checks[0])) &&
           readable(image + VBI_RVA, sizeof(void *), 0);
}

/* ---------------------------------------------------------------------------------------- */
/* CPU time: the draw site saves main-thread time, which the MoveWorker probes cannot see. */

static uint64_t filetime(const FILETIME *f) { return ((uint64_t)f->dwHighDateTime << 32) | f->dwLowDateTime; }

/* The earliest-created thread of this process (X4's main thread runs WinMain and the render). */
static HANDLE fd_find_main_thread(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0), best = NULL;
    uint64_t best_created = UINT64_MAX;
    THREADENTRY32 te;
    if (snap == INVALID_HANDLE_VALUE) return NULL;
    te.dwSize = sizeof(te);
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        FILETIME c, e, k, u;
        if (t && GetThreadTimes(t, &c, &e, &k, &u) && filetime(&c) < best_created) {
            if (best) CloseHandle(best);
            best = t; best_created = filetime(&c);
        } else if (t) {
            CloseHandle(t);
        }
        te.dwSize = sizeof(te);
    }
    CloseHandle(snap);
    return best;
}

static double thread_cpu_ms(HANDLE t) {
    FILETIME c, e, k, u;
    return t && GetThreadTimes(t, &c, &e, &k, &u) ? (double)(filetime(&k) + filetime(&u)) / 1e4 : 0.0;
}

static double process_cpu_ms(void) {
    FILETIME c, e, k, u;
    return GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? (double)(filetime(&k) + filetime(&u)) / 1e4 : 0.0;
}

/* ---------------------------------------------------------------------------------------- */
/* Lifetime and per-frame guard */

static void **table_now(void) { return image ? *(void ***)(image + VBI_RVA) : NULL; }
static void fd_slot_move(fd_handler *self);
static void fd_no_op(fd_handler *self) { (void)self; }
static void *fd_slot_vtable[] = {(void *)fd_no_op, (void *)fd_slot_move, (void *)fd_no_op};
static fd_handler fd_hook = {fd_slot_vtable};

static int fd_table_owned(void) {
    void **table = table_now();
    return table && table == fd_table && readable(table, 0x138, 1) && table[VBI_SLOT] == &fd_hook;
}

static int fd_loading(void) {
    void **table = table_now();
    return !table || table != fd_table || *(volatile int *)((unsigned char *)table + LOADING_OFFSET);
}

static int write_entry(void **entry, void *expected, void *value) {
    DWORD protection, ignored;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &protection)) return 0;
    int ok = InterlockedCompareExchangePointer((void *volatile *)entry, value, expected) == expected;
    VirtualProtect(entry, sizeof(void *), protection, &ignored);
    return ok;
}

static volatile LONG *call_rel(void) { return (volatile LONG *)(image + ELEMENT_CALL_RVA + 1); }

static int write_rel(LONG expected, LONG value) {
    DWORD protection, ignored;
    void *at = (void *)call_rel();
    if (!VirtualProtect(at, 4, PAGE_EXECUTE_READWRITE, &protection)) return 0;
    int ok = InterlockedCompareExchange(call_rel(), value, expected) == expected;
    VirtualProtect(at, 4, protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, 4);
    return ok;
}

static void fd_call_restore(void) {
    if (!fd_call_patched) return;
    if (*call_rel() == fd_call_hook_rel) write_rel(fd_call_hook_rel, fd_call_original_rel);
    fd_call_patched = 0;   /* a foreign value is left alone (fail closed: never overwrite it) */
}

static void fd_uninstall(void) {
    fd_call_restore();
    for (int k = 0; k < VS_COUNT; ++k)
        if (fd_entry[k] && fd_orig[k] && *fd_entry[k] == fd_wrapper[k])
            write_entry(fd_entry[k], fd_wrapper[k], fd_orig[k]);
    memset(fd_entry, 0, sizeof(fd_entry));
    if (fd_current) fd_current->installed = 0;
}

static volatile uint64_t *render_flags(void) { return (volatile uint64_t *)(image + RENDER_FLAGS_RVA); }
static volatile uint64_t *render_flags_staged(void) { return (volatile uint64_t *)(image + RENDER_FLAGS_STAGED_RVA); }

static int fd_draw_bit_set(void) {
    return ((*render_flags() | *render_flags_staged()) & DISABLE_RENDERING) != 0;
}

/* Clears only the bit; any other flag changes made meanwhile are kept. */
static void fd_draw_clear(void) {
    InterlockedAnd64((volatile LONG64 *)render_flags_staged(), ~(LONG64)DISABLE_RENDERING);
    InterlockedAnd64((volatile LONG64 *)render_flags(), ~(LONG64)DISABLE_RENDERING);
}

/* Lock held. */
static void fd_draw_release(void) {
    if (!fd_draw_owned) return;
    fd_draw_clear();
    fd_draw_owned = 0;
    fd_draw_watch = DRAW_WATCH_FRAMES;   /* the guard clears a copy that raced this release */
}

/* Lock held; guard frames in which the owner does not hold the bit. */
static void fd_draw_watch_frame(fd_owner *o) {
    if (fd_draw_watch <= 0) return;
    if (fd_draw_bit_set()) { fd_draw_clear(); ++o->draw_recleared; fd_draw_watch = DRAW_WATCH_FRAMES; return; }
    if (--fd_draw_watch == 0) fd_draw_ours = 0;   /* stayed clear for the whole watch */
}

/* Lock held. */
static void fd_draw_hold(fd_owner *o) {
    /* Staged copy first: a concurrent options apply on the main thread copies it over the live value. */
    if (!(*render_flags_staged() & DISABLE_RENDERING) || !(*render_flags() & DISABLE_RENDERING)) {
        if (fd_draw_owned) ++o->draw_reapplied; else ++o->draw_set;
        InterlockedOr64((volatile LONG64 *)render_flags_staged(), (LONG64)DISABLE_RENDERING);
        InterlockedOr64((volatile LONG64 *)render_flags(), (LONG64)DISABLE_RENDERING);
        fd_draw_owned = 1;
        fd_draw_ours = 1;
    }
    fd_draw_watch = 0;
}

/* A UI Lua call reached the module (heartbeat). Lock held. */
static void fd_lua_seen(fd_owner *o) {
    LARGE_INTEGER now;
    if (o && fd_qpc(&now)) { o->lua_qpc = now.QuadPart; o->lua_frames = o->frames; }
}

/* Guard frames while skipping: UI Lua silent for STALL_MS and STALL_MIN_FRAMES means a skipped site
   stopped the UI update; run everything again (pass) and record which sites were active. Lock held. */
static void fd_stall_check(fd_owner *o) {
    LARGE_INTEGER now;
    if (o->mode != FD_DIET || !(o->sites & FD_SKIP_SITES) || !fd_qpc(&now)) return;
    if (o->frames - o->lua_frames < STALL_MIN_FRAMES) return;
    if ((double)(now.QuadPart - o->lua_qpc) * 1000.0 / (double)fd_qpf <= STALL_MS) return;
    ++o->stalls;
    o->stall_sites = o->sites;
    o->stall_frame = o->frames;
    o->mode = FD_PASS;
    o->reason = "UI Lua stalled while skipping; diet disabled for this owner";
    fd_draw_release();
    fd_lua_seen(o);
}

/* Lock held. */
static void fd_disarm(const char *reason) {
    fd_owner *o = fd_current;
    if (o) { o->armed = 0; if (reason && !o->reason) o->reason = reason; }
    fd_uninstall();
    fd_draw_release();
    if (fd_table_owned())
        InterlockedCompareExchangePointer((void *volatile *)&fd_table[VBI_SLOT], NULL, &fd_hook);
    fd_ctx_scenes = fd_ctx_camera = 0;
    fd_digest_row = -1;
}

/* ---------------------------------------------------------------------------------------- */
/* Audit: side-effect probes and the component digest */

typedef struct { uint64_t rng, ids, events; double game_time; int64_t qpc; int ok; } fd_probe;

static void probe_take(fd_probe *p) {
    memset(p, 0, sizeof(*p));
    unsigned char *tls = fd_tls();
    unsigned char *map = *(unsigned char **)(image + ID_MAP_PTR_RVA);
    LARGE_INTEGER now;
    if (!tls || !map || !fd_qpc(&now)) return;
    p->rng = *(volatile uint64_t *)(tls + TLS_RNG);
    p->game_time = *(volatile double *)(tls + TLS_GAME_TIME);
    p->ids = *(volatile uint64_t *)(map + ID_MAP_INSERTS);
    p->events = *(volatile uint64_t *)(image + EVENT_LIST_RVA + 8);
    p->qpc = now.QuadPart;
    p->ok = 1;
}

/* X4's thread RNG step (Random::*State, 0x14ddb30..): rotr64(s * 0x5851F42D4C957F2D + 0x14057B7EF767814F, 30). */
static uint64_t rng_step(uint64_t s) {
    s = s * 0x5851F42D4C957F2Dull + 0x14057B7EF767814Full;
    return (s >> 30) | (s << 34);
}

/* Steps from a to b; RNG_STEP_BOUND + 1 when not reached (reseeded or very many draws). */
static uint64_t rng_distance(uint64_t a, uint64_t b) {
    for (uint64_t n = 0; n <= RNG_STEP_BOUND; ++n) {
        if (a == b) return n;
        a = rng_step(a);
    }
    return RNG_STEP_BOUND + 1;
}

static void probe_account(fd_stats *s, const fd_probe *a, const fd_probe *b) {
    if (!a->ok || !b->ok) return;
    ++s->audited;
    if (a->rng != b->rng) {
        uint64_t n = rng_distance(a->rng, b->rng);
        ++s->rng_calls;
        if (n > RNG_STEP_BOUND) ++s->rng_unknown; else s->rng_steps += n;
    }
    if (b->ids != a->ids) { ++s->id_calls; s->ids += b->ids - a->ids; }
    if (b->events != a->events) { ++s->event_calls; if (b->events > a->events) s->events_up += b->events - a->events; }
    if (b->game_time != a->game_time) ++s->time_changed;
    double ms = fd_qpf > 0 ? (double)(b->qpc - a->qpc) * 1000.0 / (double)fd_qpf : 0.0;
    s->ms_total += ms;
    if (ms > s->ms_max) s->ms_max = ms;
}

typedef struct { const unsigned char *at; uint64_t uid; } fd_snap_ref;
static unsigned char *fd_snap;       /* digest_max * COMPONENT_WINDOW bytes */
static fd_snap_ref *fd_snap_refs;
static unsigned fd_snap_capacity, fd_snap_count;

static int fd_digest_alloc(unsigned count) {
    if (fd_snap && fd_snap_capacity >= count) return 1;
    unsigned char *bytes = VirtualAlloc(NULL, (SIZE_T)count * COMPONENT_WINDOW, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    fd_snap_ref *refs = VirtualAlloc(NULL, (SIZE_T)count * sizeof(fd_snap_ref), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!bytes || !refs) {
        if (bytes) VirtualFree(bytes, 0, MEM_RELEASE);
        if (refs) VirtualFree(refs, 0, MEM_RELEASE);
        return 0;
    }
    if (fd_snap) VirtualFree(fd_snap, 0, MEM_RELEASE);
    if (fd_snap_refs) VirtualFree(fd_snap_refs, 0, MEM_RELEASE);
    fd_snap = bytes; fd_snap_refs = refs; fd_snap_capacity = count;
    return 1;
}

/* Registered components in AutoIDMap slot order (layout: entity.md, ComponentRegistry::Find
   0xced70): 32 blocks of 2^20 slots, 3 slots per 32-byte bucket {ptr[3], u16 gen[3], u32 flags},
   flag nibble of slot j at bits 16+4j, bit 0 = present. A slot index is block << 20 | 3*bucket + j. */
#define ID_BLOCK_SLOTS (1u << 20)
#define ID_SLOT_COUNT (32ull * ID_BLOCK_SLOTS)
static int id_slot(const unsigned char *map, uint64_t slot, const unsigned char **component, uint64_t *uid) {
    const unsigned char *block = *(unsigned char *const *)(map + ID_MAP_BLOCKS + 8 * (slot >> 20));
    if (!block) return -1;   /* whole block absent */
    unsigned index = (unsigned)(slot & (ID_BLOCK_SLOTS - 1)), bucket = index / 3, j = index % 3;
    const unsigned char *b = block + (size_t)bucket * 32;
    uint32_t flags = *(const uint32_t *)(b + 0x1c);
    if (!((flags >> (16 + 4 * j)) & 1)) return 0;
    *component = *(unsigned char *const *)(b + 8 * j);
    uint16_t gen = *(const uint16_t *)(b + 0x18 + 2 * j);
    *uid = ((uint64_t)gen << 25) | (slot + 1);
    return *component != NULL;
}

static void fd_digest_begin(fd_owner *o, int row) {
    fd_snap_count = 0;
    unsigned char *map = *(unsigned char **)(image + ID_MAP_PTR_RVA);
    if (!map || !fd_snap) return;
    uint64_t slot = fd_digest_cursor, visited = 0;
    while (fd_snap_count < o->digest_max && visited < ID_SLOT_COUNT) {
        if (slot >= ID_SLOT_COUNT) slot = 0;
        const unsigned char *component;
        uint64_t uid;
        int r = id_slot(map, slot, &component, &uid);
        if (r < 0) { uint64_t skip = ID_BLOCK_SLOTS - (slot & (ID_BLOCK_SLOTS - 1)); slot += skip; visited += skip; continue; }
        ++slot; ++visited;
        if (!r) continue;
        if (!fd_copy(fd_snap + (size_t)fd_snap_count * COMPONENT_WINDOW, component, COMPONENT_WINDOW)) {
            ++o->st[row].digest_unreadable;
            continue;
        }
        fd_snap_refs[fd_snap_count].at = component;
        fd_snap_refs[fd_snap_count].uid = uid;
        ++fd_snap_count;
    }
    fd_digest_cursor = slot;
}

static void fd_digest_end(fd_owner *o, int row) {
    fd_stats *s = &o->st[row];
    unsigned char now[COMPONENT_WINDOW];
    ++s->digests;
    for (unsigned i = 0; i < fd_snap_count; ++i) {
        const unsigned char *then = fd_snap + (size_t)i * COMPONENT_WINDOW;
        if (!fd_copy(now, fd_snap_refs[i].at, COMPONENT_WINDOW)) { ++s->digest_unreadable; continue; }
        ++s->digest_components;
        int changed = 0;
        for (unsigned w = 0; w < HISTOGRAM_WORDS; ++w) {
            if (w * 8 == REFCOUNT_OFFSET) continue;   /* refcount/lifecycle: interlocked by any thread */
            uint64_t before, after;
            memcpy(&before, then + w * 8, 8);
            memcpy(&after, now + w * 8, 8);
            if (before == after) continue;
            ++s->histogram[w];
            if (!changed && !s->first[0]) {
                const unsigned char *macro;
                uint32_t cls = 0xffffffffu;
                memcpy(&macro, now + 0x60, sizeof(macro));
                if (!fd_copy(&cls, macro + 0x44, sizeof(cls))) cls = 0xffffffffu;
                snprintf(s->first, sizeof(s->first), "uid=%llu class=0x%x off=0x%x before=%016llx after=%016llx",
                         (unsigned long long)fd_snap_refs[i].uid, (unsigned)cls, w * 8,
                         (unsigned long long)before, (unsigned long long)after);
            }
            changed = 1;
        }
        if (changed) ++s->digest_changed;
    }
    fd_snap_count = 0;
}

/* Runs `call` with side-effect probes; the digest covers the call when this row is scheduled
   for the frame (first such call only). */
#define AUDITED(o, row, call) do {                                                                  fd_probe a_, b_;                                                                             int d_ = fd_digest_row == (row);                                                             if (d_) { fd_digest_row = -1; fd_digest_begin((o), (row)); }                                 probe_take(&a_);                                                                             call;                                                                                        probe_take(&b_);                                                                             if ((o)->armed) {                                                                                probe_account(&(o)->st[row], &a_, &b_);                                                      if (d_) fd_digest_end((o), (row));                                                       }                                                                                        } while (0)

/* ---------------------------------------------------------------------------------------- */
/* Wrappers. Each loads the owner once; the state is static, so a disarm on another thread
   only makes the remaining work of this call pass through. */

static fd_owner *fd_engaged_owner(void) {
    fd_owner *o = fd_current;
    return o && o->armed && GetCurrentThreadId() == fd_thread && !fd_loading() ? o : NULL;
}

static int fd_skipping(const fd_owner *o, int site) {
    /* Defence in depth: never skip the update required to drive UI Lua. */
    return site != FD_UI_ELEMENTS && o->mode == FD_DIET && (o->sites & site) && !fd_present;
}
static int fd_auditing(const fd_owner *o, int site) {
    return o->mode == FD_AUDIT && (o->sites & site);
}

/* AnarkSceneManager slot 3: for each scene, the scene update (slot 5) clears the scene's per-frame
   lists and tail-calls the layer walk (slot 12). Only provides the context for the walk. */
static uintptr_t fd_scene_manager(void *self) {
    fd_fn1 original = (fd_fn1)fd_orig[VS_SCENE_MANAGER];
    if (!fd_engaged_owner()) return original(self);
    ++fd_ctx_scenes;
    uintptr_t r = original(self);
    --fd_ctx_scenes;
    return r;
}

static uintptr_t fd_scene_walk(void *self, void *root) {
    fd_fn2 original = (fd_fn2)fd_orig[VS_SCENE_WALK];
    fd_owner *o = fd_ctx_scenes > 0 ? fd_engaged_owner() : NULL;
    uintptr_t r;
    if (!o) return original(self, root);
    fd_stats *s = &o->st[ST_UI_SCENES];
    ++s->calls;
    if (fd_skipping(o, FD_UI_SCENES)) { ++s->skipped; return 0; }
    ++s->passed;
    if (fd_auditing(o, FD_UI_SCENES)) { AUDITED(o, ST_UI_SCENES, r = original(self, root)); return r; }
    return original(self, root);
}

/* CUIController::Render (render bank 1, slot 10): builds the UI draw lists. */
static uintptr_t fd_ui_render(void *self) {
    fd_fn1 original = (fd_fn1)fd_orig[VS_UI_RENDER];
    fd_owner *o = fd_engaged_owner();
    uintptr_t r;
    if (!o) return original(self);
    fd_stats *s = &o->st[ST_UI_RENDER];
    ++s->calls;
    if (fd_skipping(o, FD_UI_RENDER)) { ++s->skipped; return 0; }
    ++s->passed;
    if (fd_auditing(o, FD_UI_RENDER)) { AUDITED(o, ST_UI_RENDER, r = original(self)); return r; }
    return original(self);
}

/* GameClient render bank (slot 10): calls Render of every camera in the universe camera list
   (Universe+0x2c0). Only provides the context for the camera wrapper. */
static uintptr_t fd_client_render(void *self) {
    fd_fn1 original = (fd_fn1)fd_orig[VS_CLIENT_RENDER];
    if (!fd_engaged_owner()) return original(self);
    ++fd_ctx_camera;
    uintptr_t r = original(self);
    --fd_ctx_camera;
    return r;
}

static uintptr_t fd_camera_render(void *self) {
    fd_fn1 original = (fd_fn1)fd_orig[VS_CAMERA_RENDER];
    fd_owner *o = fd_ctx_camera > 0 ? fd_engaged_owner() : NULL;
    uintptr_t r;
    if (!o) return original(self);
    fd_stats *s = &o->st[ST_CAMERA];
    ++s->calls;
    if (fd_skipping(o, FD_CAMERA)) { ++s->skipped; return 0; }
    ++s->passed;
    if (fd_auditing(o, FD_CAMERA)) { AUDITED(o, ST_CAMERA, r = original(self)); return r; }
    return original(self);
}

/* Target of the near stub at the element update call site (0x138f477): (tree, root, time). */
static uintptr_t fd_element_update(void *tree, void *root, uintptr_t time) {
    fd_fn3 original = (fd_fn3)(void *)(image + ELEMENT_FN_RVA);
    fd_owner *o = fd_engaged_owner();
    uintptr_t r;
    if (!o) return original(tree, root, time);
    fd_stats *s = &o->st[ST_UI_ELEMENTS];
    ++s->calls;
    if (fd_skipping(o, FD_UI_ELEMENTS)) { ++s->skipped; return 0; }
    ++s->passed;
    if (fd_auditing(o, FD_UI_ELEMENTS)) { AUDITED(o, ST_UI_ELEMENTS, r = original(tree, root, time)); return r; }
    return original(tree, root, time);
}

/* A free 64 KiB-aligned page within rel32 reach of `site`, scanning outwards from it. */
static unsigned char *fd_alloc_near(const unsigned char *site) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t gran = info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const uintptr_t reach = 0x7fff0000u;
    uintptr_t at = (uintptr_t)site;
    for (uintptr_t d = gran; d < reach; d += gran) {
        for (int side = 0; side < 2; ++side) {
            uintptr_t c = side ? ((at + d) & ~(gran - 1)) : (((at > d) ? at - d : 0) & ~(gran - 1));
            if (!c) continue;
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQuery((void *)c, &m, sizeof(m)) || m.State != MEM_FREE) continue;
            void *p = VirtualAlloc((void *)c, STUB_PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!p) continue;
            intptr_t lo = (intptr_t)p - (intptr_t)(site + 5), hi = lo + 16;
            if (lo == (int32_t)lo && hi == (int32_t)hi) return p;
            VirtualFree(p, 0, MEM_RELEASE);
        }
    }
    return NULL;
}

/* Verify the E8 rel32 still targets the element update, build the stub, retarget the call. */
static const char *fd_call_install(void) {
    unsigned char *site = image + ELEMENT_CALL_RVA;
    if (!readable(site, 5, 0) || site[0] != 0xe8) return "element update call site changed";
    int32_t rel;
    memcpy(&rel, site + 1, 4);
    if (site + 5 + rel != image + ELEMENT_FN_RVA) return "element update call site changed";
    if (!fd_stub_page) {
        unsigned char *page = fd_alloc_near(site);
        if (!page) return "no stub page within rel32 reach";
        void *target = (void *)fd_element_update;
        memset(page, 0xcc, STUB_PAGE);
        page[0] = 0x48; page[1] = 0xb8;           /* movabs rax, fd_element_update */
        memcpy(page + 2, &target, 8);
        page[10] = 0xff; page[11] = 0xe0;         /* jmp rax (rax is volatile, not an argument) */
        DWORD old;
        if (!VirtualProtect(page, STUB_PAGE, PAGE_EXECUTE_READ, &old)) return "stub page protection failed";
        FlushInstructionCache(GetCurrentProcess(), page, STUB_PAGE);
        fd_stub_page = page;
    }
    fd_call_original_rel = rel;
    fd_call_hook_rel = (int32_t)((intptr_t)fd_stub_page - (intptr_t)(site + 5));
    if (!write_rel(fd_call_original_rel, fd_call_hook_rel)) return "element update call patch failed";
    fd_call_patched = 1;
    return NULL;
}

/* GameRender::Move (VBI slot 10): with rng_split, the whole slot runs on the private render stream. */
static uintptr_t fd_game_render(void *self) {
    fd_fn1 original = (fd_fn1)fd_orig[VS_GAME_RENDER];
    fd_owner *o = fd_engaged_owner();
    unsigned char *tls;
    if (!o || !(o->sites & FD_RNG_SPLIT) || !(tls = fd_tls())) return original(self);
    volatile uint64_t *state = (volatile uint64_t *)(tls + TLS_RNG);
    uint64_t simulation = *state, render = fd_render_rng;
    *state = render;
    uintptr_t r = original(self);
    uint64_t after = *state;
    *state = simulation;
    fd_render_rng = after;
    ++o->split_calls;
    if (after != render) {
        uint64_t n = rng_distance(render, after);
        if (n <= RNG_STEP_BOUND) o->split_render_steps += n;
    }
    return r;
}

/* The guard body (lock held). */
static void fd_guard(fd_owner *o) {
    if (GetCurrentThreadId() != fd_thread || !fd_table_owned()) { fd_disarm("callback ownership lost"); return; }
    fd_ctx_scenes = fd_ctx_camera = 0;
    /* Loading screens: the wrappers pass through (UI on the main thread); checks resume afterwards. */
    if (fd_loading()) { ++o->loading_frames; fd_digest_row = -1; fd_frame_valid = 0; return; }
    for (int k = 0; k < VS_COUNT; ++k)
        if (fd_entry[k] && *fd_entry[k] != fd_wrapper[k]) { fd_disarm("vtable entry changed"); return; }
    if (fd_call_patched && *call_rel() != fd_call_hook_rel) { fd_disarm("element update call changed"); return; }
    ++o->frames;
    fd_stall_check(o);
    if (o->mode == FD_AUDIT) {
        /* Whole previous frame: every simulation-stream draw and ID insertion, for scale. */
        unsigned char *tls = fd_tls();
        unsigned char *map = *(unsigned char **)(image + ID_MAP_PTR_RVA);
        if (tls && map) {
            uint64_t rng = *(volatile uint64_t *)(tls + TLS_RNG), ids = *(volatile uint64_t *)(map + ID_MAP_INSERTS);
            if (fd_frame_valid) {
                uint64_t n = rng_distance(fd_frame_rng, rng);
                ++o->frame_audited;
                if (n > RNG_STEP_BOUND) ++o->frame_rng_unknown; else o->frame_rng_steps += n;
                o->frame_ids += ids - fd_frame_ids;
            }
            fd_frame_rng = rng; fd_frame_ids = ids; fd_frame_valid = 1;
        }
    } else {
        fd_frame_valid = 0;
    }
    fd_present = o->period > 0 && o->frames % (uint64_t)o->period == 0;
    if (fd_present) ++o->present_frames;
    if (o->mode == FD_DIET && (o->sites & FD_DRAW)) fd_draw_hold(o);
    else { fd_draw_release(); fd_draw_watch_frame(o); }
    fd_digest_row = -1;
    if (o->mode != FD_AUDIT || o->audit_every <= 0 || o->frames % (uint64_t)o->audit_every) return;
    /* Rotate the digest over the audited regions and the control window. */
    int rows[ST_COUNT], n = 0;
    if (o->sites & FD_UI_SCENES) rows[n++] = ST_UI_SCENES;
    if (o->sites & FD_UI_RENDER) rows[n++] = ST_UI_RENDER;
    if (o->sites & FD_CAMERA) rows[n++] = ST_CAMERA;
    if (o->sites & FD_UI_ELEMENTS) rows[n++] = ST_UI_ELEMENTS;
    rows[n++] = ST_CONTROL;
    int row = rows[fd_rotation++ % (unsigned)n];
    if (row != ST_CONTROL) { fd_digest_row = row; return; }
    /* Control: nothing of ours runs; other threads may still write. Same length as the
       slowest audited region on average (at least 0.5 ms, at most CONTROL_MAX_US). */
    double us = 500.0;
    for (int r = 0; r < ST_CONTROL; ++r)
        if (o->st[r].audited && o->st[r].ms_total * 1000.0 / (double)o->st[r].audited > us)
            us = o->st[r].ms_total * 1000.0 / (double)o->st[r].audited;
    if (us > CONTROL_MAX_US) us = CONTROL_MAX_US;
    fd_digest_row = ST_CONTROL;
    AUDITED(o, ST_CONTROL, fd_spin(us));
    fd_digest_row = -1;
}

/* VBI slot 2, every MoveWorker frame before GameClient::Move (also on loading screens). */
static void fd_slot_move(fd_handler *self) {
    (void)self;
    fd_owner *o = fd_current;
    if (!o || !o->armed) return;
    /* Held while arm/stop/set run (on loading screens on the main thread): skip this frame. */
    if (!TryAcquireSRWLockExclusive(&fd_lock)) { ++o->guard_skipped; return; }
    o = fd_current;
    if (o && o->armed) fd_guard(o);
    ReleaseSRWLockExclusive(&fd_lock);
}

/* ---------------------------------------------------------------------------------------- */
/* Installation */

static int fd_install(int sites) {
    void *wrappers[VS_COUNT] = {(void *)fd_scene_manager, (void *)fd_scene_walk, (void *)fd_ui_render,
                                (void *)fd_client_render, (void *)fd_camera_render, (void *)fd_game_render};
    void **entries[VS_COUNT] = {0};
    for (int k = 0; k < VS_COUNT; ++k) {
        if (!(sites & vs_site[k])) continue;
        const x4_framediet_vtable *v = &x4_framediet_vtables[k];
        void **entry = (void **)(image + v->entry);
        if (!readable(entry, sizeof(void *), 0) || *entry != (void *)(image + v->fn)) return 0;
        entries[k] = entry;
    }
    for (int k = 0; k < VS_COUNT; ++k) {
        if (!entries[k]) continue;
        fd_orig[k] = (void *)(image + x4_framediet_vtables[k].fn);
        fd_wrapper[k] = wrappers[k];
        if (!write_entry(entries[k], fd_orig[k], fd_wrapper[k])) {
            for (int j = 0; j < k; ++j)
                if (entries[j]) write_entry(entries[j], fd_wrapper[j], fd_orig[j]);
            memset(fd_entry, 0, sizeof(fd_entry));
            return 0;
        }
        fd_entry[k] = entries[k];
    }
    return 1;
}

static int parse_sites(const char *text, int *out) {
    int sites = 0;
    const char *p = text;
    while (p && *p) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 9 && !strncmp(p, "ui_scenes", n)) sites |= FD_UI_SCENES;
        else if (n == 9 && !strncmp(p, "ui_render", n)) sites |= FD_UI_RENDER;
        else if (n == 6 && !strncmp(p, "camera", n)) sites |= FD_CAMERA;
        else if (n == 4 && !strncmp(p, "draw", n)) sites |= FD_DRAW;
        else if (n == 9 && !strncmp(p, "rng_split", n)) sites |= FD_RNG_SPLIT;
        else if (n == 11 && !strncmp(p, "ui_elements", n)) sites |= FD_UI_ELEMENTS;
        else if (n == 6 && !strncmp(p, "server", n)) sites |= FD_UI_SCENES | FD_DRAW;   /* behaviour-identical set */
        else if (n == 3 && !strncmp(p, "all", n)) sites |= FD_ALL;
        else if (n) return 0;
        p = end ? end + 1 : NULL;
    }
    *out = sites;
    return 1;
}

static int parse_mode(const char *text, int *out) {
    if (!text) return 0;
    if (!strcmp(text, "diet")) *out = FD_DIET;
    else if (!strcmp(text, "pass")) *out = FD_PASS;
    else if (!strcmp(text, "audit")) *out = FD_AUDIT;
    else return 0;
    return 1;
}

/* ---------------------------------------------------------------------------------------- */
/* Lua API */

static int failure(lua_State *L, const char *reason) {
    p_lua_pushnil(L);
    p_lua_pushlstring(L, reason, strlen(reason));
    return 2;
}

static int number_arg(lua_State *L, int index, double fallback, double lo, double hi, double *out) {
    if (p_lua_gettop(L) < index || p_lua_type(L, index) == LUA_TNIL) { *out = fallback; return 1; }
    if (p_lua_type(L, index) != LUA_TNUMBER) return 0;
    double v = p_lua_tonumber(L, index);
    if (!(v >= lo && v <= hi) || v != (double)(int64_t)v) return 0;
    *out = v;
    return 1;
}

/* Arms an owner on the verified process (start() runs the executable checks first; the synthetic
   tests call this directly). Lock held. Returns NULL or the refusal reason; nothing stays changed on
   refusal (a previous owner's statistics included). */
static const char *fd_arm(uint64_t generation, int mode, int sites, int period, int audit_every, unsigned digest_max) {
    if (fd_current) return "an owner already exists; stop it first";
    if (mode == FD_DIET && (sites & FD_UI_ELEMENTS)) return "ui_elements diet disabled: stops UI Lua";
    void **table = table_now();
    if (!readable(table, 0x138, 1) || table[VBI_SLOT] || table[17 + VBI_SLOT] || !table[3])
        return "VBI slot occupied or unavailable";
    if (*(int *)((unsigned char *)table + LOADING_OFFSET)) return "loading";
    /* Right after a loading screen Lua can still run on the main thread: only the MoveWorker arms
       (the owner thread, and the rel32 write happens on the thread that executes that call). */
    int on_main = fd_on_main_thread();
    if (on_main < 0) return "main thread unknown";
    if (on_main) return "loading (UI on the main thread)";
    int flags_ok = readable(image + RENDER_FLAGS_RVA, 16, 1);
    if ((sites & FD_DRAW) && !flags_ok) return "render flags unavailable";
    unsigned char *tls = fd_tls();
    unsigned char *map = *(unsigned char **)(image + ID_MAP_PTR_RVA);
    if (!tls || !readable(tls + TLS_GAME_TIME, 0x20, 0) || !readable(map, ID_MAP_BLOCKS + 32 * 8, 0) ||
        !readable(image + EVENT_LIST_RVA, 16, 0))
        return "audit state unavailable";
    if (mode == FD_AUDIT && digest_max > 0 && !fd_digest_alloc(digest_max)) return "digest buffer unavailable";
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return "QPC frequency unavailable";
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)&fd_hook, &pinned))
        return "cannot pin callback module";
    fd_owner *o = &fd_state;
    fd_owner fresh, previous = *o;
    memset(&fresh, 0, sizeof(fresh));
    fresh.generation = generation;
    fresh.mode = mode; fresh.sites = sites; fresh.period = period; fresh.audit_every = audit_every;
    fresh.digest_max = fd_snap ? (digest_max < fd_snap_capacity ? digest_max : fd_snap_capacity) : 0;
    fd_table = table;
    fd_thread = GetCurrentThreadId();
    fd_qpf = frequency.QuadPart;
    fd_ctx_scenes = fd_ctx_camera = 0;
    fd_digest_row = -1;
    fd_present = 0;
    fd_render_rng = FD_RENDER_RNG_SEED;
    if (fd_worker_thread) CloseHandle(fd_worker_thread);
    fd_worker_thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    fd_frame_valid = 0;
    if (InterlockedCompareExchangePointer((void *volatile *)&table[VBI_SLOT], &fd_hook, NULL))
        return "VBI slot changed during registration";
    *o = fresh;
    fd_current = o;   /* the slot handler sees an unarmed owner until the entries are installed */
    const char *why = NULL;
    if (!fd_install(sites)) why = "vtable entry is not the verified original";
    else if ((sites & FD_UI_ELEMENTS) && (why = fd_call_install()) != NULL) fd_uninstall();
    if (why) {
        InterlockedCompareExchangePointer((void *volatile *)&table[VBI_SLOT], NULL, &fd_hook);
        fd_current = NULL;
        *o = previous;   /* stats() of the previous owner keep working */
        return why;
    }
    /* A bit a previous owner set and whose release raced an engine copy is ours: adopt it, so this
       owner holds or releases it. A bit set by anyone else stays foreign. */
    fd_draw_owned = flags_ok && fd_draw_ours && fd_draw_bit_set();
    fd_lua_seen(o);
    o->installed = 1;
    o->armed = 1;
    return NULL;
}

/* set(mode[, sites]) core; sites < 0 keeps them. Lock held. NULL or the refusal reason. */
static const char *fd_set(fd_owner *o, int mode, int sites) {
    if (!o || o != fd_current || !o->armed) return "not armed";
    if (sites < 0) sites = o->sites;
    if (mode == FD_DIET && o->stalls) return "diet disabled after UI Lua stall; stop owner first";
    if (mode == FD_DIET && (sites & FD_UI_ELEMENTS)) return "ui_elements diet disabled: stops UI Lua";
    int installed = FD_DRAW | (fd_call_patched ? FD_UI_ELEMENTS : 0);
    for (int k = 0; k < VS_COUNT; ++k) if (fd_entry[k]) installed |= vs_site[k];
    if (sites & ~installed) return "site not installed";
    o->mode = mode;
    o->sites = sites;
    if (mode != FD_DIET || !(sites & FD_DRAW)) fd_draw_release();
    fd_lua_seen(o);
    return NULL;
}

/* stop() core. Lock held. */
static void fd_stop(fd_owner *o) {
    if (o && o == fd_current) { fd_disarm(NULL); fd_current = NULL; }
}

/* active() core: the UI Lua heartbeat; also notices a lost slot (the guard no longer runs then).
   Lock held. */
static int fd_active(fd_owner *o) {
    if (o && o == fd_current && o->armed && !fd_table_owned()) fd_disarm("callback ownership lost");
    int on = o && o == fd_current && o->armed;
    if (on) fd_lua_seen(o);
    return on;
}

/* The owner state behind a handle's generation, or NULL for a stale handle. */
static fd_owner *fd_owner_for(uint64_t generation) {
    return generation && generation == fd_state.generation ? &fd_state : NULL;
}

static int start(lua_State *L) {
    int mode, sites, top = p_lua_gettop(L);
    double period, audit_every, digest_max;
    if (top < 2 || top > 5 || p_lua_type(L, 1) != LUA_TSTRING || p_lua_type(L, 2) != LUA_TSTRING)
        return failure(L, "usage: start(mode, sites[, period[, audit_every[, digest_max]]])");
    if (!parse_mode(p_lua_tolstring(L, 1, NULL), &mode)) return failure(L, "unsupported mode");
    if (!parse_sites(p_lua_tolstring(L, 2, NULL), &sites) || !sites) return failure(L, "unsupported sites");
    if (!number_arg(L, 3, 0, 0, 100000, &period)) return failure(L, "invalid period");
    if (!number_arg(L, 4, 30, 0, 100000, &audit_every)) return failure(L, "invalid audit interval");
    if (!number_arg(L, 5, 16384, 0, 1 << 20, &digest_max)) return failure(L, "invalid digest size");
    if (fd_current) return failure(L, "an owner already exists; stop it first");
    int main_thread = p_lua_pushthread(L);
    p_lua_settop(L, -2);
    if (!main_thread) return failure(L, "main Lua thread required");
    /* Cheap checks first: the probe retries every second on loading screens. */
    if (!verify_headers()) return failure(L, "unsupported executable or modified engine signatures");
    {
        void **table = readable(image + VBI_RVA, sizeof(void *), 0) ? table_now() : NULL;
        if (table && readable((unsigned char *)table + LOADING_OFFSET, 4, 0) &&
            *(volatile int *)((unsigned char *)table + LOADING_OFFSET))
            return failure(L, "loading");
    }
    if (!verify_image()) return failure(L, "unsupported executable or modified engine signatures");
    /* The handle exists before anything is armed, so a Lua allocation error cannot orphan an owner. */
    fd_handle *h = p_lua_newuserdata(L, sizeof(*h));
    h->generation = ++fd_generation;
    p_lua_getfield(L, REGISTRY, META);
    p_lua_setmetatable(L, -2);
    AcquireSRWLockExclusive(&fd_lock);
    const char *why = fd_arm(h->generation, mode, sites, (int)period, (int)audit_every, (unsigned)digest_max);
    ReleaseSRWLockExclusive(&fd_lock);
    if (why) return failure(L, why);
    return 1;
}

static fd_owner *owner_of(lua_State *L) {
    fd_handle *h = p_luaL_checkudata(L, 1, META);
    return fd_owner_for(h->generation);
}

static int stop(lua_State *L) {
    fd_owner *o = owner_of(L);
    AcquireSRWLockExclusive(&fd_lock);
    fd_stop(o);
    ReleaseSRWLockExclusive(&fd_lock);
    return 0;
}

/* active() -> armed, stalls (UI Lua stalls the guard reverted to pass mode). Call it every UI update:
   it is the heartbeat the stall watchdog waits for. */
static int active(lua_State *L) {
    fd_owner *o = owner_of(L);
    AcquireSRWLockExclusive(&fd_lock);
    int on = fd_active(o);
    double stalls = o ? (double)o->stalls : 0.0;
    ReleaseSRWLockExclusive(&fd_lock);
    p_lua_pushboolean(L, on);
    p_lua_pushnumber(L, stalls);
    return 2;
}

/* set(mode[, sites]): switch the mode, or change the active sites within the installed set
   (A/B timing in one session: `pass` and `diet` keep the same wrappers installed). */
static int set(lua_State *L) {
    fd_owner *o = owner_of(L);
    int mode, sites = -1, top = p_lua_gettop(L);
    if (top < 2 || p_lua_type(L, 2) != LUA_TSTRING || !parse_mode(p_lua_tolstring(L, 2, NULL), &mode))
        return failure(L, "unsupported mode");
    if (top >= 3 && p_lua_type(L, 3) != LUA_TNIL &&
        (p_lua_type(L, 3) != LUA_TSTRING || !parse_sites(p_lua_tolstring(L, 3, NULL), &sites)))
        return failure(L, "unsupported sites");
    AcquireSRWLockExclusive(&fd_lock);
    const char *why = fd_set(o, mode, sites);
    ReleaseSRWLockExclusive(&fd_lock);
    if (why) return failure(L, why);
    p_lua_pushboolean(L, 1);
    return 1;
}

static void number_field(lua_State *L, const char *name, double value) {
    p_lua_pushnumber(L, value);
    p_lua_setfield(L, -2, name);
}

static void string_field(lua_State *L, const char *name, const char *value) {
    p_lua_pushlstring(L, value, strlen(value));
    p_lua_setfield(L, -2, name);
}

/* The most often changed 8-byte words of U::Component as "0xOFF:count,..." (at most 6). */
static void top_words(const fd_stats *s, char *text, size_t size) {
    uint32_t h[HISTOGRAM_WORDS];
    size_t used = 0;
    memcpy(h, s->histogram, sizeof(h));
    text[0] = 0;
    for (int pick = 0; pick < 6; ++pick) {
        unsigned best = 0;
        for (unsigned w = 1; w < HISTOGRAM_WORDS; ++w) if (h[w] > h[best]) best = w;
        if (!h[best]) break;
        int n = snprintf(text + used, size - used, "%s0x%x:%u", used ? "," : "", best * 8, h[best]);
        if (n < 0 || (size_t)n >= size - used) break;
        used += (size_t)n;
        h[best] = 0;
    }
}

static int stats(lua_State *L) {
    fd_handle *h = p_luaL_checkudata(L, 1, META);
    fd_owner snapshot;
    int armed = 0, draw_owned = 0, found = 0;
    double main_cpu_ms = 0, worker_cpu_ms = 0;
    /* Lua allocations may invoke __gc (and stop()). Snapshot first, then release the lock before
       constructing the result table. On loading screens this serialises with the guard. */
    AcquireSRWLockExclusive(&fd_lock);
    fd_owner *live = fd_owner_for(h->generation);
    if (live) {
        snapshot = *live;
        armed = live == fd_current && live->armed;
        draw_owned = live == fd_current && fd_draw_owned;
        main_cpu_ms = thread_cpu_ms(fd_main_thread);
        worker_cpu_ms = thread_cpu_ms(fd_worker_thread);
        found = 1;
    }
    ReleaseSRWLockExclusive(&fd_lock);
    fd_owner *o = found ? &snapshot : NULL;
    static const char *const modes[] = {"pass", "diet", "audit"};
    char key[64], text[128];
    if (!o) { p_lua_pushnil(L); return 1; }
    p_lua_createtable(L, 0, 100);
    number_field(L, "frames", (double)o->frames);
    number_field(L, "loading_frames", (double)o->loading_frames);
    number_field(L, "present_frames", (double)o->present_frames);
    number_field(L, "armed", armed);
    number_field(L, "sites", o->sites);
    number_field(L, "period", o->period);
    number_field(L, "draw_owned", draw_owned);
    number_field(L, "draw_set", (double)o->draw_set);
    number_field(L, "draw_reapplied", (double)o->draw_reapplied);
    number_field(L, "draw_recleared", (double)o->draw_recleared);
    number_field(L, "guard_skipped", (double)o->guard_skipped);
    number_field(L, "stalls", (double)o->stalls);
    number_field(L, "stall_sites", o->stall_sites);
    number_field(L, "stall_frame", (double)o->stall_frame);
    number_field(L, "split_calls", (double)o->split_calls);
    number_field(L, "split_render_steps", (double)o->split_render_steps);
    number_field(L, "frame_audited", (double)o->frame_audited);
    number_field(L, "frame_rng_steps", (double)o->frame_rng_steps);
    number_field(L, "frame_rng_unknown", (double)o->frame_rng_unknown);
    number_field(L, "frame_ids", (double)o->frame_ids);
    number_field(L, "digest_max", o->digest_max);
    number_field(L, "main_cpu_ms", main_cpu_ms);
    number_field(L, "worker_cpu_ms", worker_cpu_ms);
    number_field(L, "process_cpu_ms", process_cpu_ms());
    {
        /* Process commit (private bytes), to watch per-frame UI queues for growth while a site is skipped. */
        PROCESS_MEMORY_COUNTERS_EX m;
        memset(&m, 0, sizeof(m));
        m.cb = sizeof(m);
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof(m)))
            number_field(L, "commit_mb", (double)m.PrivateUsage / (1024.0 * 1024.0));
    }
    string_field(L, "mode", modes[o->mode]);
    if (o->reason) string_field(L, "reason", o->reason);
    for (int r = 0; r < ST_COUNT; ++r) {
        const fd_stats *s = &o->st[r];
#define F(field, value) snprintf(key, sizeof(key), "%s_%s", st_names[r], field); number_field(L, key, (double)(value))
        F("calls", s->calls); F("skipped", s->skipped); F("passed", s->passed);
        F("audited", s->audited); F("rng_calls", s->rng_calls); F("rng_steps", s->rng_steps);
        F("rng_unknown", s->rng_unknown); F("id_calls", s->id_calls); F("ids", s->ids);
        F("event_calls", s->event_calls); F("events_up", s->events_up); F("time_changed", s->time_changed);
        F("ms_mean", s->audited ? s->ms_total / (double)s->audited : 0.0); F("ms_max", s->ms_max);
        F("digests", s->digests); F("digest_components", s->digest_components);
        F("digest_changed", s->digest_changed); F("digest_unreadable", s->digest_unreadable);
#undef F
        top_words(s, text, sizeof(text));
        snprintf(key, sizeof(key), "%s_words", st_names[r]);
        string_field(L, key, text);
        snprintf(key, sizeof(key), "%s_first", st_names[r]);
        string_field(L, key, s->first);
    }
    return 1;
}

static int resolve_lua(void) {
    HMODULE runtime = GetModuleHandleW(L"lua51_64.dll");
    if (!runtime) return 0;
#define RESOLVE(name) p_##name = (void *)GetProcAddress(runtime, #name); if (!p_##name) return 0
    RESOLVE(lua_gettop); RESOLVE(lua_type); RESOLVE(lua_tolstring); RESOLVE(lua_tonumber);
    RESOLVE(lua_settop); RESOLVE(lua_pushthread); RESOLVE(lua_pushvalue); RESOLVE(lua_pushnil);
    RESOLVE(lua_pushboolean); RESOLVE(lua_pushnumber); RESOLVE(lua_pushlstring);
    RESOLVE(lua_pushcclosure); RESOLVE(lua_getfield); RESOLVE(lua_setfield); RESOLVE(lua_createtable);
    RESOLVE(lua_newuserdata); RESOLVE(lua_setmetatable); RESOLVE(luaL_newmetatable); RESOLVE(luaL_checkudata);
#undef RESOLVE
    return 1;
}

static void method(lua_State *L, const char *name, lua_CFunction function) {
    p_lua_pushcclosure(L, function, 0);
    p_lua_setfield(L, -2, name);
}

__declspec(dllexport) int luaopen_x4lan_framediet(lua_State *L) {
    if (!resolve_lua()) return 0;
    p_luaL_newmetatable(L, META);
    p_lua_pushvalue(L, -1); p_lua_setfield(L, -2, "__index");
    method(L, "__gc", stop); method(L, "stop", stop); method(L, "active", active);
    method(L, "set", set); method(L, "stats", stats);
    p_lua_settop(L, -2);
    p_lua_createtable(L, 0, 1);
    method(L, "start", start);
    return 1;
}
