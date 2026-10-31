/* Synthetic lifecycle and behaviour tests for framediet.c (no X4 involved).
 * A fake image holds the VBI table pointer, the six vtable entries, RenderFlags, the ID map
 * pointer and the deferred event list; fake engine functions stand in for the wrapped virtuals.
 * Like X4.exe, the pages with the vtable entries are read-only and the code pages execute/read
 * while the module runs, so the protection save/restore of every write is exercised. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "framediet.c"

static void *fake_table[40];
static unsigned char fake_tls[0x400];
static unsigned char fake_map[0x200];
static unsigned char *fake_block;
static unsigned char fake_components[6][COMPONENT_WINDOW];
static unsigned char fake_macro[0x50];
static unsigned char *fake_tls_fn(void) { return fake_tls; }
static int main_thread_role;   /* what the main-thread seam reports */
static int fake_on_main_thread(void) { return main_thread_role; }
static LONGLONG fake_now;
static BOOL WINAPI fake_qpc(LARGE_INTEGER *v) { v->QuadPart = fake_now; return TRUE; }

static void **entry_of(int k) { return (void **)(image + x4_framediet_vtables[k].entry); }
static void *fn_of(int k) { return image + x4_framediet_vtables[k].fn; }

/* Page protection like X4.exe: .rdata vtables read-only, code execute/read, .data read/write. */
static DWORD protection_at(const void *at) {
    MEMORY_BASIC_INFORMATION m;
    assert(VirtualQuery(at, &m, sizeof(m)));
    return m.Protect;
}
static void set_protection(void *at, size_t n, DWORD protection) {
    DWORD old;
    assert(VirtualProtect(at, n, protection, &old));
}
static void protect_like_x4(void) {
    for (int k = 0; k < VS_COUNT; ++k) set_protection(entry_of(k), sizeof(void *), PAGE_READONLY);
    set_protection(image + ELEMENT_CALL_RVA, 5, PAGE_EXECUTE_READ);
}
static void assert_protected_like_x4(void) {
    for (int k = 0; k < VS_COUNT; ++k) assert(protection_at(entry_of(k)) == PAGE_READONLY);
    assert(protection_at(image + ELEMENT_CALL_RVA) == PAGE_EXECUTE_READ);
}
/* A foreign writer (another hook) changes a protected entry or call bytes. */
static void poke(void *at, const void *data, size_t n) {
    DWORD old, ignored;
    assert(VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old));
    memcpy(at, data, n);
    assert(VirtualProtect(at, n, old, &ignored));
    FlushInstructionCache(GetCurrentProcess(), at, n);
}
static void poke_entry(int k, void *value) { poke(entry_of(k), &value, sizeof(value)); }

/* Side effects a fake engine function performs (to check the audit). */
typedef struct { int rng_steps; int ids; int events; int poke_offset; int poke_refcount; uint64_t reseed; } effects;
static effects walk_fx, camera_fx, render_fx, control_fx, sim_fx;
static int scene_walks, scene_manager_calls, ui_renders, client_renders, camera_renders, spins;

static void apply(const effects *fx) {
    uint64_t *rng = (uint64_t *)(fake_tls + TLS_RNG);
    for (int i = 0; i < fx->rng_steps; ++i) *rng = rng_step(*rng);
    if (fx->reseed) *rng = fx->reseed;
    *(uint64_t *)(fake_map + ID_MAP_INSERTS) += (uint64_t)fx->ids;
    *(uint64_t *)(image + EVENT_LIST_RVA + 8) += (uint64_t)fx->events;
    if (fx->poke_offset) fake_components[1][fx->poke_offset] ^= 0x5a;
    if (fx->poke_refcount) ++*(uint32_t *)(fake_components[2] + REFCOUNT_OFFSET);
}

/* Another thread calls the layer walk while the MoveWorker is inside the scene manager. */
static int walk_from_other_thread, other_thread_walks;
static DWORD WINAPI other_thread_walk(LPVOID unused) {
    (void)unused;
    ((fd_fn2)*entry_of(VS_SCENE_WALK))(NULL, (void *)9);
    return 0;
}

/* AnarkScene slot 5 shape: clear lists (not modelled), then tail-call slot 12 via the vtable. */
static uintptr_t fake_scene_manager(void *self) {
    ++scene_manager_calls;
    for (int scene = 0; scene < 3; ++scene)
        ((fd_fn2)*entry_of(VS_SCENE_WALK))(self, (void *)(uintptr_t)(scene + 1));
    if (walk_from_other_thread) {
        HANDLE t = CreateThread(NULL, 0, other_thread_walk, NULL, 0, NULL);
        assert(t && WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
        CloseHandle(t);
    }
    return 0;
}
static uintptr_t fake_scene_walk(void *self, void *root) {
    (void)self; assert(root);
    ++scene_walks; apply(&walk_fx);
    if (root == (void *)9) ++other_thread_walks;
    return 7;
}
static uintptr_t fake_ui_render(void *self) { (void)self; ++ui_renders; apply(&render_fx); return 0; }
static uintptr_t fake_client_render(void *self) {
    ++client_renders;
    for (int camera = 0; camera < 2; ++camera) ((fd_fn1)*entry_of(VS_CAMERA_RENDER))(self);
    return 0;
}
static uintptr_t fake_camera_render(void *self) { (void)self; ++camera_renders; apply(&camera_fx); return 0; }
static int game_renders, element_updates;
static effects element_fx;
static uintptr_t fake_element_update(void *tree, void *root, uintptr_t time) {
    assert(tree == (void *)0x298 && root == (void *)0x1000 && (uint32_t)time == 4242);
    ++element_updates; apply(&element_fx);
    return 0;
}
/* GameRender::Move shape: render banks UI and GameClient through their vtable entries. */
static uintptr_t fake_game_render(void *self) {
    ++game_renders;
    ((fd_fn1)*entry_of(VS_UI_RENDER))(self);
    ((fd_fn1)*entry_of(VS_CLIENT_RENDER))(self);
    return 0;
}
static double spun_us;
static void fake_spin(double us) { (void)us; ++spins; spun_us = us; apply(&control_fx); }

static void register_component(uint64_t slot, unsigned char *component, uint16_t gen) {
    unsigned char *b = fake_block + (slot / 3) * 32;
    unsigned j = (unsigned)(slot % 3);
    *(unsigned char **)(b + 8 * j) = component;
    *(uint16_t *)(b + 0x18 + 2 * j) = gen;
    *(uint32_t *)(b + 0x1c) |= 1u << (16 + 4 * j);
}

static void reset_counts(void) {
    memset(&walk_fx, 0, sizeof(walk_fx)); memset(&camera_fx, 0, sizeof(camera_fx));
    memset(&render_fx, 0, sizeof(render_fx)); memset(&control_fx, 0, sizeof(control_fx));
    memset(&element_fx, 0, sizeof(element_fx)); memset(&sim_fx, 0, sizeof(sim_fx)); element_updates = 0;
    scene_walks = scene_manager_calls = ui_renders = client_renders = camera_renders = spins = game_renders = 0;
    walk_from_other_thread = other_thread_walks = 0;
}

/* movabs rax, target; jmp rax: the fake image's "engine functions" jump to the fakes. */
static void stub(size_t rva, void *target) {
    unsigned char *at = image + rva;
    at[0] = 0x48; at[1] = 0xb8;
    memcpy(at + 2, &target, 8);
    at[10] = 0xff; at[11] = 0xe0;
}

static void setup_engine(void) {
    set_protection(image, X4_IMAGE_SIZE, PAGE_EXECUTE_READWRITE);
    memset(fake_table, 0, sizeof(fake_table));
    fake_table[3] = fake_table;  /* GameClient present */
    *(void ***)(image + VBI_RVA) = fake_table;
    void *fakes[VS_COUNT] = {(void *)fake_scene_manager, (void *)fake_scene_walk, (void *)fake_ui_render,
                             (void *)fake_client_render, (void *)fake_camera_render, (void *)fake_game_render};
    for (int k = 0; k < VS_COUNT; ++k) { stub(x4_framediet_vtables[k].fn, fakes[k]); *entry_of(k) = fn_of(k); }
    /* The presentation update's call of the element update: sub rsp,0x28; call rel32; add rsp,0x28; ret,
       with the E8 exactly at ELEMENT_CALL_RVA, and the "engine function" at ELEMENT_FN_RVA. */
    {
        unsigned char *c = image + ELEMENT_CALL_RVA - 4;
        const unsigned char pro[] = {0x48, 0x83, 0xec, 0x28}, epi[] = {0x48, 0x83, 0xc4, 0x28, 0xc3};
        memcpy(c, pro, 4);
        c[4] = 0xe8;
        int32_t rel = (int32_t)(ELEMENT_FN_RVA - (ELEMENT_CALL_RVA + 5));
        memcpy(c + 5, &rel, 4);
        memcpy(c + 9, epi, 5);
        stub(ELEMENT_FN_RVA, (void *)fake_element_update);
    }
    FlushInstructionCache(GetCurrentProcess(), image, X4_IMAGE_SIZE);
    *(uint64_t *)(image + RENDER_FLAGS_RVA) = 0x20;          /* noshadows */
    *(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) = 0x20;
    *(unsigned char **)(image + ID_MAP_PTR_RVA) = fake_map;
    *(unsigned char **)(fake_map + ID_MAP_BLOCKS) = fake_block;
    *(uint64_t *)(fake_map + ID_MAP_INSERTS) = 1000;
    *(uint64_t *)(image + EVENT_LIST_RVA + 8) = 3;
    *(uint64_t *)(fake_tls + TLS_RNG) = 0x1234567890abcdefull;
    *(double *)(fake_tls + TLS_GAME_TIME) = 42.0;
    memset(fake_components, 0, sizeof(fake_components));
    *(unsigned char **)(fake_macro) = NULL;
    *(uint32_t *)(fake_macro + 0x44) = 0x75;   /* ship */
    for (int c = 0; c < 6; ++c) *(unsigned char **)(fake_components[c] + 0x60) = fake_macro;
    fd_draw_ours = fd_draw_watch = 0;            /* a fresh process */
    main_thread_role = 0;
    reset_counts();
    protect_like_x4();
}

/* Arms like start() does after the executable checks (under the lock). */
static const char *arm(int mode, int sites, int period, int audit_every, unsigned digest_max) {
    static uint64_t generation = 100;
    AcquireSRWLockExclusive(&fd_lock);
    const char *why = fd_arm(++generation, mode, sites, period, audit_every, digest_max);
    ReleaseSRWLockExclusive(&fd_lock);
    return why;
}
static const char *set_mode(int mode, int sites) {
    AcquireSRWLockExclusive(&fd_lock);
    const char *why = fd_set(fd_current, mode, sites);
    ReleaseSRWLockExclusive(&fd_lock);
    return why;
}
static void disarm_all(void) {
    AcquireSRWLockExclusive(&fd_lock);
    fd_stop(fd_current);
    ReleaseSRWLockExclusive(&fd_lock);
    assert(!fd_current);
}
static int is_active(void) {
    AcquireSRWLockExclusive(&fd_lock);
    int on = fd_active(fd_owner_for(fd_state.generation));
    ReleaseSRWLockExclusive(&fd_lock);
    return on;
}
/* After a guard-detected disarm the owner still exists (stats readable) until Lua stops it. */
static void stop_disarmed(void) { disarm_all(); }

static int draw_bits(void) {
    return ((*(uint64_t *)(image + RENDER_FLAGS_RVA) & DISABLE_RENDERING) ? 1 : 0) |
           ((*(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) & DISABLE_RENDERING) ? 2 : 0);
}

static void presentation_update(void) {
    ((fd_fn3)(void *)(image + ELEMENT_CALL_RVA - 4))((void *)0x298, (void *)0x1000, 4242);
}

/* One MoveWorker frame: slot 2 guard, slot 3 simulation draws, slot 6 element update + scene update,
   slot 10 GameRender::Move. */
static void frame(void) {
    fd_slot_move(&fd_hook);
    apply(&sim_fx);
    presentation_update();
    ((fd_fn1)*entry_of(VS_SCENE_MANAGER))(NULL);
    ((fd_fn1)*entry_of(VS_GAME_RENDER))(NULL);
}

static int32_t call_rel_now(void) { int32_t r; memcpy(&r, image + ELEMENT_CALL_RVA + 1, 4); return r; }

/* Stress: a "MoveWorker" arms and runs guard frames while the main thread stops the owner (as Lua
   does on a loading screen). Each round must end with the engine restored. */
static volatile LONG worker_armed, worker_stop;
static DWORD WINAPI stress_worker(LPVOID unused) {
    (void)unused;
    if (arm(FD_PASS, FD_ALL | FD_UI_ELEMENTS | FD_RNG_SPLIT, 0, 0, 0)) return 1;
    InterlockedExchange(&worker_armed, 1);
    while (!worker_stop) {
        fd_slot_move(&fd_hook);
        YieldProcessor();
    }
    return 0;
}
static void stress_round(int loading) {
    worker_armed = worker_stop = 0;
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 0;
    HANDLE t = CreateThread(NULL, 0, stress_worker, NULL, 0, NULL);
    assert(t);
    while (!worker_armed) {
        DWORD code;
        assert(GetExitCodeThread(t, &code) && code == STILL_ACTIVE);
        YieldProcessor();
    }
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = loading;
    for (volatile int spin = 0; spin < 2000; ++spin) YieldProcessor();
    disarm_all();                                         /* main thread, concurrently with the guard */
    InterlockedExchange(&worker_stop, 1);
    assert(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    for (int k = 0; k < VS_COUNT; ++k) assert(*entry_of(k) == fn_of(k));
    assert(!fd_call_patched && call_rel_now() == (int32_t)(ELEMENT_FN_RVA - (ELEMENT_CALL_RVA + 5)));
    assert(!fake_table[VBI_SLOT] && draw_bits() == 0 && !fd_current);
    assert_protected_like_x4();
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 0;
}

/* Lua allocations can run finalisers. Test the actual active()/stats() entry points with a
   constructor that stops/replaces the owner, without needing an X4 process or a Lua allocation. */
static fd_handle api_handle;
static double api_number, api_frames, api_armed, api_draw;
static int api_bool, api_nil, api_replace_on_table;
static const char *api_text;
static char api_mode[16];
static void api_unlocked(void) {
    assert(TryAcquireSRWLockExclusive(&fd_lock));
    ReleaseSRWLockExclusive(&fd_lock);
}
static void *api_checkudata(lua_State *L, int index, const char *name) {
    (void)L; assert(index == 1 && !strcmp(name, META)); return &api_handle;
}
static void api_pushnumber(lua_State *L, double n) { (void)L; api_unlocked(); api_number = n; }
static void api_pushboolean(lua_State *L, int b) { (void)L; api_unlocked(); api_bool = b; }
static void api_pushnil(lua_State *L) { (void)L; api_unlocked(); ++api_nil; }
static void api_pushlstring(lua_State *L, const char *s, size_t n) {
    (void)L; (void)n; api_unlocked(); api_text = s;
}
static void api_setfield(lua_State *L, int index, const char *name) {
    (void)L; assert(index == -2); api_unlocked();
    if (!strcmp(name, "frames")) api_frames = api_number;
    if (!strcmp(name, "armed")) api_armed = api_number;
    if (!strcmp(name, "draw_owned")) api_draw = api_number;
    if (!strcmp(name, "mode")) snprintf(api_mode, sizeof(api_mode), "%s", api_text);
}
static void api_createtable(lua_State *L, int array, int fields) {
    (void)L; (void)array; (void)fields; api_unlocked();
    if (api_replace_on_table) {
        disarm_all();
        assert(!arm(FD_PASS, FD_UI_SCENES, 0, 0, 0));
    }
}

static volatile LONG other_done;
static DWORD WINAPI other_thread_frame(LPVOID unused) {
    (void)unused;
    ((fd_fn1)*entry_of(VS_SCENE_MANAGER))(NULL);
    ((fd_fn1)*entry_of(VS_UI_RENDER))(NULL);
    ((fd_fn1)*entry_of(VS_CLIENT_RENDER))(NULL);
    InterlockedExchange(&other_done, 1);
    return 0;
}

int main(void) {
    assert(!verify_image() && !verify_headers());   /* the test executable is not X4 */
    image = VirtualAlloc(NULL, X4_IMAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    fake_block = VirtualAlloc(NULL, (ID_BLOCK_SLOTS / 3 + 1) * 32, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(image && fake_block);
    fd_tls = fake_tls_fn;
    fd_spin = fake_spin;
    fd_on_main_thread = fake_on_main_thread;

    /* Site parsing and modes. */
    {
        int s, m;
        assert(parse_sites("server", &s) && s == (FD_UI_SCENES | FD_DRAW));
        assert(parse_sites("ui_scenes,camera", &s) && s == (FD_UI_SCENES | FD_CAMERA));
        assert(parse_sites("all", &s) && s == FD_ALL);
        assert(parse_sites("all,rng_split", &s) && s == (FD_ALL | FD_RNG_SPLIT));
        assert(parse_sites("ui_elements", &s) && s == FD_UI_ELEMENTS);
        assert(parse_sites("", &s) && s == 0);
        assert(!parse_sites("ui_scene", &s) && !parse_sites("draw,bogus", &s));
        assert(parse_mode("diet", &m) && m == FD_DIET && parse_mode("pass", &m) && m == FD_PASS);
        assert(parse_mode("audit", &m) && m == FD_AUDIT && !parse_mode("skip", &m) && !parse_mode(NULL, &m));
    }

    /* RNG distance on X4's thread LCG, including the bound. */
    {
        uint64_t a = 99, b = a;
        for (int i = 0; i < 17; ++i) b = rng_step(b);
        assert(rng_distance(a, a) == 0 && rng_distance(a, b) == 17);
        assert(rng_distance(b, a) == RNG_STEP_BOUND + 1);
    }

    /* Refusals leave everything untouched: occupied slot, loading, main thread, foreign vtable entry. */
    setup_engine();
    fake_table[VBI_SLOT] = fake_table;
    assert(!strcmp(arm(FD_DIET, FD_ALL, 0, 0, 0), "VBI slot occupied or unavailable"));
    fake_table[VBI_SLOT] = NULL; fake_table[17 + VBI_SLOT] = fake_table;
    assert(!strcmp(arm(FD_DIET, FD_ALL, 0, 0, 0), "VBI slot occupied or unavailable"));
    fake_table[17 + VBI_SLOT] = NULL;
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 1;
    assert(!strcmp(arm(FD_DIET, FD_ALL, 0, 0, 0), "loading"));
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 0;
    main_thread_role = 1;     /* loading just ended, Lua still runs on the main thread */
    assert(!strcmp(arm(FD_PASS, FD_ALL | FD_UI_ELEMENTS, 0, 0, 0), "loading (UI on the main thread)"));
    main_thread_role = -1;
    assert(!strcmp(arm(FD_DIET, FD_ALL, 0, 0, 0), "main thread unknown"));
    main_thread_role = 0;
    assert(!fd_current && !fake_table[VBI_SLOT] && call_rel_now() == (int32_t)(ELEMENT_FN_RVA - (ELEMENT_CALL_RVA + 5)));
    poke_entry(VS_CAMERA_RENDER, image + 5);   /* someone else wrapped Camera::Render */
    assert(!strcmp(arm(FD_DIET, FD_ALL, 0, 0, 0), "vtable entry is not the verified original"));
    assert(!fd_current && !fake_table[VBI_SLOT]);
    for (int k = 0; k < VS_CAMERA_RENDER; ++k) assert(*entry_of(k) == fn_of(k));
    assert(*entry_of(VS_CAMERA_RENDER) == image + 5);
    assert_protected_like_x4();
    /* The same foreign entry does not block a set that does not need it. */
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    assert(fd_current && fd_current->armed && fake_table[VBI_SLOT] == &fd_hook);
    assert(*entry_of(VS_SCENE_MANAGER) == (void *)fd_scene_manager && *entry_of(VS_SCENE_WALK) == (void *)fd_scene_walk);
    assert(*entry_of(VS_UI_RENDER) == fn_of(VS_UI_RENDER) && *entry_of(VS_CAMERA_RENDER) == image + 5);
    assert_protected_like_x4();
    assert(!strcmp(arm(FD_DIET, FD_UI_SCENES, 0, 0, 0), "an owner already exists; stop it first"));
    disarm_all();
    assert(*entry_of(VS_SCENE_MANAGER) == fn_of(VS_SCENE_MANAGER) && *entry_of(VS_SCENE_WALK) == fn_of(VS_SCENE_WALK));
    assert(*entry_of(VS_CAMERA_RENDER) == image + 5 && !fake_table[VBI_SLOT]);
    assert_protected_like_x4();
    puts("PASS: refusals (occupied slot, loading bank, loading, main thread, foreign entry, second owner) leave the engine untouched");

    /* Diet: the scene layer walk is skipped inside the scene manager only; draw bit held. */
    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    for (int f = 0; f < 10; ++f) frame();
    assert(scene_manager_calls == 10 && scene_walks == 0);
    assert(fd_state.st[ST_UI_SCENES].calls == 30 && fd_state.st[ST_UI_SCENES].skipped == 30);
    assert(ui_renders == 10 && camera_renders == 20);   /* not installed: untouched originals */
    assert(fd_state.frames == 10 && fd_state.draw_set == 1);
    assert(*(uint64_t *)(image + RENDER_FLAGS_RVA) == (0x20 | DISABLE_RENDERING));
    assert(*(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) == (0x20 | DISABLE_RENDERING));
    /* Outside the scene manager the walk passes through. */
    ((fd_fn2)*entry_of(VS_SCENE_WALK))(NULL, (void *)1);
    assert(scene_walks == 1);
    /* An options apply copies a staged value without the bit: re-applied at the next frame. */
    *(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) = 0x80;
    *(uint64_t *)(image + RENDER_FLAGS_RVA) = 0x80;
    frame();
    assert(fd_state.draw_reapplied == 1 && *(uint64_t *)(image + RENDER_FLAGS_RVA) == (0x80 | DISABLE_RENDERING));
    /* Other threads always pass through, also while the MoveWorker is inside the scene manager. */
    {
        int walks = scene_walks;
        HANDLE t = CreateThread(NULL, 0, other_thread_frame, NULL, 0, NULL);
        assert(t && WaitForSingleObject(t, 5000) == WAIT_OBJECT_0 && other_done);
        CloseHandle(t);
        assert(scene_walks == walks + 3);
        walk_from_other_thread = 1;
        uint64_t skipped = fd_state.st[ST_UI_SCENES].skipped;
        frame();
        assert(other_thread_walks == 1 && fd_state.st[ST_UI_SCENES].skipped == skipped + 3);
        walk_from_other_thread = 0;
    }
    /* Loading: everything passes through and the bit stays (the server shows no loading screen). */
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 1;
    {
        int walks = scene_walks;
        frame();
        assert(scene_walks == walks + 3 && fd_state.loading_frames == 1);
        assert(draw_bits() == 3 && fd_draw_owned);
    }
    *(int *)((unsigned char *)fake_table + LOADING_OFFSET) = 0;
    /* stop: entries restored, only our bit cleared, slot released, protections back. */
    disarm_all();
    assert(*(uint64_t *)(image + RENDER_FLAGS_RVA) == 0x80 && *(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) == 0x80);
    for (int k = 0; k < VS_COUNT; ++k) assert(*entry_of(k) == fn_of(k));
    assert(!fake_table[VBI_SLOT]);
    assert_protected_like_x4();
    puts("PASS: diet skips the Anark layer walk only inside the scene update (other threads pass), holds and releases disable_rendering");

    /* A bit that was already set by someone else is never cleared by us. */
    setup_engine();
    *(uint64_t *)(image + RENDER_FLAGS_RVA) |= DISABLE_RENDERING;
    *(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) |= DISABLE_RENDERING;
    assert(!arm(FD_DIET, FD_DRAW, 0, 0, 0));
    frame(); frame();
    assert(!fd_draw_owned && fd_state.draw_set == 0);
    assert(!set_mode(FD_PASS, -1));
    frame();
    assert(draw_bits() == 3);
    disarm_all();
    assert(draw_bits() == 3);
    puts("PASS: a disable_rendering bit set by someone else is left alone");

    /* Our bit coming back after a release (an engine copy racing it) is cleared again; after a stop, the
       next owner adopts it. */
    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    frame();
    assert(draw_bits() == 3 && fd_draw_owned);
    assert(!set_mode(FD_PASS, -1));
    assert(draw_bits() == 0 && !fd_draw_owned);         /* set() releases at once */
    *(uint64_t *)(image + RENDER_FLAGS_STAGED_RVA) |= DISABLE_RENDERING;   /* stale staged copy */
    frame();
    assert(draw_bits() == 0 && fd_state.draw_recleared == 1);
    for (int f = 0; f < DRAW_WATCH_FRAMES + 1; ++f) frame();
    assert(!fd_draw_ours && fd_draw_watch == 0);         /* watch over: a later bit would be foreign */
    assert(!set_mode(FD_DIET, -1));
    frame();
    assert(draw_bits() == 3 && fd_state.draw_set == 2);  /* the guard holds it again */
    assert(!set_mode(FD_DIET, FD_UI_SCENES));            /* sites without draw: released by set() */
    assert(draw_bits() == 0);
    assert(!strcmp(set_mode(FD_DIET, FD_UI_RENDER), "site not installed"));
    assert(!set_mode(FD_DIET, FD_UI_SCENES | FD_DRAW));
    frame();
    assert(draw_bits() == 3);
    disarm_all();
    assert(draw_bits() == 0);
    *(uint64_t *)(image + RENDER_FLAGS_RVA) |= DISABLE_RENDERING;          /* the race after stop */
    assert(!arm(FD_PASS, FD_UI_SCENES, 0, 0, 0));
    assert(fd_draw_owned);                               /* adopted: ours, not foreign */
    frame();
    assert(draw_bits() == 0 && !fd_draw_owned);
    disarm_all();
    assert(!strcmp(set_mode(FD_DIET, -1), "not armed"));
    puts("PASS: set() releases the bit; a racing copy is cleared again; a stale bit of ours is adopted after stop");

    /* All sites, period 4: every 4th frame presents (all originals run). */
    setup_engine();
    assert(!arm(FD_DIET, FD_ALL, 4, 0, 0));
    for (int f = 0; f < 8; ++f) frame();
    assert(scene_walks == 2 * 3 && ui_renders == 2 && camera_renders == 2 * 2);
    assert(fd_state.present_frames == 2 && client_renders == 8);
    assert(fd_state.st[ST_CAMERA].skipped == 12 && fd_state.st[ST_UI_RENDER].skipped == 6);
    /* pass mode: same wrappers, nothing skipped, draw bit released. */
    assert(!set_mode(FD_PASS, -1));
    reset_counts();
    for (int f = 0; f < 3; ++f) frame();
    assert(scene_walks == 9 && ui_renders == 3 && camera_renders == 6 && !fd_draw_owned);
    assert(!(*(uint64_t *)(image + RENDER_FLAGS_RVA) & DISABLE_RENDERING));
    disarm_all();
    puts("PASS: all sites skip, period frames present everything, pass mode is inert");

    /* Fail closed: a vtable entry changed under us, the slot replaced, the table replaced. */
    setup_engine();
    assert(!arm(FD_DIET, FD_ALL, 0, 0, 0));
    frame();
    poke_entry(VS_UI_RENDER, image + 9);
    fd_slot_move(&fd_hook);
    assert(!fd_state.armed && fd_state.reason && !strcmp(fd_state.reason, "vtable entry changed"));
    assert(!is_active());
    assert(*entry_of(VS_UI_RENDER) == image + 9);   /* foreign value kept */
    assert(*entry_of(VS_CAMERA_RENDER) == fn_of(VS_CAMERA_RENDER) && !fake_table[VBI_SLOT]);
    assert(!(*(uint64_t *)(image + RENDER_FLAGS_RVA) & DISABLE_RENDERING));
    assert_protected_like_x4();
    {
        /* Disarmed wrappers pass through even in their context (a call already in flight). */
        int walks = scene_walks;
        fd_ctx_scenes = 1;
        ((fd_fn2)(void *)fd_scene_walk)(NULL, (void *)1);
        fd_ctx_scenes = 0;
        assert(scene_walks == walks + 1);
    }
    stop_disarmed();
    setup_engine();
    assert(!arm(FD_DIET, FD_ALL, 0, 0, 0));
    fake_table[VBI_SLOT] = fake_table;   /* someone took slot 2 */
    fd_slot_move(&fd_hook);
    assert(!fd_state.armed && !strcmp(fd_state.reason, "callback ownership lost"));
    assert(fake_table[VBI_SLOT] == fake_table);
    for (int k = 0; k < VS_COUNT; ++k) assert(*entry_of(k) == fn_of(k));
    stop_disarmed();
    setup_engine();
    assert(!arm(FD_DIET, FD_ALL, 0, 0, 0));
    {
        static void *other_table[40];
        *(void ***)(image + VBI_RVA) = other_table;  /* engine replaced its table singleton */
        int walks = scene_walks;
        ((fd_fn1)*entry_of(VS_SCENE_MANAGER))(NULL);
        assert(scene_walks == walks + 3);       /* wrappers pass through at once */
        assert(!is_active());                   /* Lua's active() notices without the guard */
        assert(!fd_state.armed && !strcmp(fd_state.reason, "callback ownership lost"));
        assert(!other_table[VBI_SLOT] && fake_table[VBI_SLOT] == &fd_hook);  /* stale table never written */
        for (int k = 0; k < VS_COUNT; ++k) assert(*entry_of(k) == fn_of(k));
    }
    stop_disarmed();
    puts("PASS: fail closed on a changed entry, a taken slot or a replaced table; foreign values kept");

    /* The guard skips a frame while arm/stop/set hold the lock (main thread on a loading screen). */
    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    frame();
    {
        uint64_t frames = fd_state.frames;
        AcquireSRWLockExclusive(&fd_lock);
        fd_slot_move(&fd_hook);
        ReleaseSRWLockExclusive(&fd_lock);
        assert(fd_state.frames == frames && fd_state.guard_skipped == 1 && fd_state.armed);
        fd_slot_move(&fd_hook);
        assert(fd_state.frames == frames + 1);
    }
    disarm_all();
    /* Concurrent stop on the main thread while the arming thread runs the guard (both with and
       without the loading flag): never a torn restore, every round ends restored. */
    for (int round = 0; round < 300; ++round) stress_round(round & 1);
    puts("PASS: the guard skips frames while another thread holds the lock; 300 concurrent stop rounds restore everything");

    /* UI Lua stall watchdog: while sites are skipped, UI Lua must keep calling active(); after STALL_MS
       and STALL_MIN_FRAMES of silence the guard reverts to pass and records the sites. */
    setup_engine();
    fd_qpc = fake_qpc;
    fake_now = 1000000;
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    for (int f = 0; f < 50; ++f) { fake_now += fd_qpf / 10; assert(is_active()); frame(); }   /* 5 s with heartbeat */
    assert(fd_state.stalls == 0 && fd_state.mode == FD_DIET && draw_bits() == 3);
    assert(is_active());
    for (int f = 0; f < 100; ++f) { fake_now += fd_qpf / 50; frame(); }   /* 100 silent frames in 2 s */
    assert(fd_state.stalls == 0 && fd_state.mode == FD_DIET);
    fake_now += fd_qpf + fd_qpf / 10;                                       /* now 3.1 s silent */
    frame();
    assert(fd_state.stalls == 1 && fd_state.mode == FD_PASS && fd_state.stall_sites == (FD_UI_SCENES | FD_DRAW));
    assert(draw_bits() == 0 && fd_state.armed);
    {
        int walks = scene_walks;
        frame();
        assert(scene_walks == walks + 3);   /* everything runs again */
    }
    assert(!strcmp(set_mode(FD_DIET, -1), "diet disabled after UI Lua stall; stop owner first"));
    assert(!strcmp(set_mode(FD_DIET, FD_DRAW), "diet disabled after UI Lua stall; stop owner first"));
    assert(fd_state.mode == FD_PASS && draw_bits() == 0);
    assert(fd_state.reason && strstr(fd_state.reason, "UI Lua stalled"));
    assert(!set_mode(FD_AUDIT, -1));  /* observation can continue without skipping */
    for (int f = 0; f < 40; ++f) { fake_now += fd_qpf; frame(); }
    assert(fd_state.stalls == 1);
    disarm_all();
    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    for (int f = 0; f < STALL_MIN_FRAMES - 1; ++f) { fake_now += fd_qpf; frame(); }
    assert(fd_state.stalls == 0);   /* long silence but too few frames */
    fake_now += fd_qpf;
    frame();
    assert(fd_state.stalls == 1 && fd_state.mode == FD_PASS);
    disarm_all();
    fd_qpc = fd_qpc_default;
    puts("PASS: UI Lua watchdog restores pass-through and permanently refuses diet for that owner");

    /* A refused arm keeps the previous owner's statistics and generation. */
    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES, 0, 0, 0));
    for (int f = 0; f < 4; ++f) frame();
    {
        uint64_t generation = fd_state.generation;
        disarm_all();
        poke_entry(VS_SCENE_WALK, image + 7);
        assert(!strcmp(arm(FD_DIET, FD_UI_SCENES, 0, 0, 0), "vtable entry is not the verified original"));
        assert(fd_state.generation == generation && fd_state.frames == 4 && fd_state.st[ST_UI_SCENES].skipped == 12);
        assert(fd_owner_for(generation) == &fd_state && !fd_owner_for(generation + 1) && !fd_owner_for(0));
        poke_entry(VS_SCENE_WALK, fn_of(VS_SCENE_WALK));
    }
    puts("PASS: a refused arm keeps the previous owner's statistics; stale generations resolve to nothing");

    /* Audit: originals run; RNG steps, ID insertions, deferred events and component writes are
       measured per region; refcount writes are ignored; the control window sees background writes. */
    setup_engine();
    for (int c = 0; c < 6; ++c) register_component((uint64_t)c * 7, fake_components[c], (uint16_t)(c + 1));
    assert(!arm(FD_AUDIT, FD_UI_SCENES | FD_UI_RENDER | FD_CAMERA, 0, 1, 64));
    walk_fx.poke_refcount = 1;                       /* ignored word */
    camera_fx.rng_steps = 5; camera_fx.ids = 2; camera_fx.events = 1;
    camera_fx.poke_offset = 0x1ec;                   /* attention of component 1 */
    control_fx.poke_offset = 0x70;                   /* a "background thread" write */
    for (int f = 0; f < 8; ++f) frame();
    assert(scene_walks == 24 && camera_renders == 16 && ui_renders == 8);   /* nothing skipped */
    {
        const fd_stats *w = &fd_state.st[ST_UI_SCENES], *c = &fd_state.st[ST_CAMERA];
        const fd_stats *r = &fd_state.st[ST_UI_RENDER], *k = &fd_state.st[ST_CONTROL];
        assert(w->audited == 24 && w->rng_calls == 0 && w->ids == 0 && w->event_calls == 0);
        assert(w->digests == 2 && w->digest_components == 12 && w->digest_changed == 0);
        assert(c->audited == 16 && c->rng_calls == 16 && c->rng_steps == 80 && c->rng_unknown == 0);
        assert(c->ids == 32 && c->id_calls == 16 && c->events_up == 16 && c->time_changed == 0);
        assert(c->digests == 2 && c->digest_changed == 2 && c->histogram[0x1e8 / 8] == 2);
        assert(strstr(c->first, "class=0x75 off=0x1e8"));
        assert(r->audited == 8 && r->digests == 2 && r->digest_changed == 0 && r->rng_calls == 0);
        assert(k->digests == 2 && spins == 2 && k->digest_changed == 2 && k->histogram[0x70 / 8] == 2);
        assert(fd_state.st[ST_CAMERA].skipped == 0);
        /* Whole frames: 7 complete frames measured (the first slot 2 only primes); 10 steps and 4 IDs
           from the two camera renders per frame. */
        assert(fd_state.frame_audited == 7 && fd_state.frame_rng_steps == 70 && fd_state.frame_ids == 28);
        char words[128];
        top_words(c, words, sizeof(words));
        assert(!strcmp(words, "0x1e8:2"));
    }
    /* The control window lasts the slowest region's mean, at most CONTROL_MAX_US. */
    fd_state.st[ST_CAMERA].ms_total = 1e6;
    for (int f = 0; f < 5 && spins < 3; ++f) frame();
    assert(spins == 3 && spun_us == CONTROL_MAX_US);
    fd_state.st[ST_CAMERA].ms_total = 0;
    /* A reseed (or more than RNG_STEP_BOUND draws) is counted as unknown, not as steps. */
    memset(&camera_fx, 0, sizeof(camera_fx));
    camera_fx.reseed = 77;
    {
        uint64_t steps = fd_state.st[ST_CAMERA].rng_steps, unknown = fd_state.st[ST_CAMERA].rng_unknown;
        frame();
        assert(fd_state.st[ST_CAMERA].rng_steps == steps);
        assert(fd_state.st[ST_CAMERA].rng_unknown == unknown + 1);   /* first camera reseeds, second keeps 77 */
    }
    disarm_all();
    puts("PASS: audit measures RNG steps, ID insertions, events and component writes per region, control window (capped)");

    /* rng_split: slot 10 runs on a private stream; the simulation stream never sees its draws, while
       simulation draws between the guard and slot 10 (slot 3) stay on it. */
    setup_engine();
    assert(!arm(FD_PASS, FD_CAMERA | FD_RNG_SPLIT, 0, 0, 0));
    camera_fx.rng_steps = 5;
    sim_fx.rng_steps = 3;
    {
        uint64_t sim = *(uint64_t *)(fake_tls + TLS_RNG);
        for (int f = 0; f < 4; ++f) frame();
        assert(rng_distance(sim, *(uint64_t *)(fake_tls + TLS_RNG)) == 12);   /* 4 x 3 slot 3 draws only */
        assert(game_renders == 4 && camera_renders == 8 && fd_state.split_calls == 4);
        assert(fd_state.split_render_steps == 40);
        uint64_t expect = FD_RENDER_RNG_SEED;
        for (int i = 0; i < 40; ++i) expect = rng_step(expect);
        assert(fd_render_rng == expect);
        /* diet with camera skipped + split: no render draws at all, the same simulation stream. */
        assert(!set_mode(FD_DIET, -1));
        uint64_t render = fd_render_rng, before = *(uint64_t *)(fake_tls + TLS_RNG);
        frame();
        assert(fd_render_rng == render && rng_distance(before, *(uint64_t *)(fake_tls + TLS_RNG)) == 3);
        assert(fd_state.st[ST_CAMERA].skipped == 2);
    }
    disarm_all();
    /* Without the split the same frames move the simulation stream by the render draws too. */
    setup_engine();
    assert(!arm(FD_PASS, FD_CAMERA, 0, 0, 0));
    camera_fx.rng_steps = 5;
    sim_fx.rng_steps = 3;
    {
        uint64_t sim = *(uint64_t *)(fake_tls + TLS_RNG);
        frame();
        assert(rng_distance(sim, *(uint64_t *)(fake_tls + TLS_RNG)) == 13);
    }
    disarm_all();
    puts("PASS: rng_split keeps view-dependent slot 10 draws off the simulation stream");

    /* ui_elements: unsafe diet refused; pass/audit retain the call-site instrumentation. */
    setup_engine();
    {
        int32_t original = call_rel_now();
        for (int period = 0; period < 3; ++period) {
            assert(!strcmp(arm(FD_DIET, FD_UI_ELEMENTS, period, 0, 0), "ui_elements diet disabled: stops UI Lua"));
            assert(!fd_current && !fake_table[VBI_SLOT] && call_rel_now() == original);
        }
        assert(!arm(FD_PASS, FD_UI_ELEMENTS, 0, 0, 0));
        assert(!strcmp(set_mode(FD_DIET, -1), "ui_elements diet disabled: stops UI Lua"));
        assert(fd_state.mode == FD_PASS);
        assert(fd_call_patched && call_rel_now() != original && fd_stub_page);
        assert_protected_like_x4();
        assert(protection_at(fd_stub_page) == PAGE_EXECUTE_READ);
        unsigned char *target = image + ELEMENT_CALL_RVA + 5 + call_rel_now();
        assert(target == fd_stub_page);
        for (int f = 0; f < 5; ++f) frame();
        assert(element_updates == 5 && fd_state.st[ST_UI_ELEMENTS].skipped == 0);
        assert(!set_mode(FD_PASS, -1));
        frame();
        assert(element_updates == 6 && fd_state.st[ST_UI_ELEMENTS].passed == 6);   /* args preserved */
        assert(!set_mode(FD_AUDIT, -1));
        element_fx.rng_steps = 3;
        frame();
        assert(element_updates == 7 && fd_state.st[ST_UI_ELEMENTS].rng_steps == 3);
        disarm_all();
        assert(!fd_call_patched && call_rel_now() == original);
        assert_protected_like_x4();
        presentation_update();
        assert(element_updates == 8);   /* original call again */
        /* A foreign retarget is detected at the next frame and left alone. */
        assert(!arm(FD_PASS, FD_UI_ELEMENTS, 0, 0, 0));
        int32_t foreign = original + 16;
        poke(image + ELEMENT_CALL_RVA + 1, &foreign, 4);
        fd_slot_move(&fd_hook);
        assert(!fd_state.armed && !strcmp(fd_state.reason, "element update call changed"));
        assert(call_rel_now() == foreign);
        stop_disarmed();
        poke(image + ELEMENT_CALL_RVA + 1, &original, 4);
        /* A call site that no longer targets the element update refuses arming; nothing changes. */
        unsigned char jmp = 0xe9, call = 0xe8;
        poke(image + ELEMENT_CALL_RVA, &jmp, 1);
        assert(!strcmp(arm(FD_AUDIT, FD_UI_ELEMENTS | FD_UI_SCENES, 0, 0, 0), "element update call site changed"));
        assert(!fd_current && !fake_table[VBI_SLOT] && *entry_of(VS_SCENE_WALK) == fn_of(VS_SCENE_WALK));
        poke(image + ELEMENT_CALL_RVA, &call, 1);
    }
    puts("PASS: ui_elements diet refused; near stub, pass/audit, restore, foreign value kept, refusal");

    /* CPU accounting: the main thread is the process's earliest thread; times are monotonic. */
    {
        HANDLE main_thread = fd_find_main_thread();
        assert(main_thread && GetThreadId(main_thread) == GetCurrentThreadId());
        double before = thread_cpu_ms(main_thread);
        volatile double sink = 0;
        for (int i = 0; i < 20000000; ++i) sink += i * 0.5;
        assert(thread_cpu_ms(main_thread) > before && process_cpu_ms() >= thread_cpu_ms(main_thread));
        CloseHandle(main_thread);
        /* The default seam: this (earliest) thread is the main thread, a new one is not. */
        assert(fd_on_main_thread_default() == 1);
    }
    puts("PASS: main-thread lookup and CPU-time accounting");

    setup_engine();
    assert(!arm(FD_DIET, FD_UI_SCENES | FD_DRAW, 0, 0, 0));
    for (int f = 0; f < 7; ++f) frame();
    api_handle.generation = fd_state.generation;
    p_luaL_checkudata = api_checkudata;
    p_lua_pushnumber = api_pushnumber;
    p_lua_pushboolean = api_pushboolean;
    p_lua_pushnil = api_pushnil;
    p_lua_pushlstring = api_pushlstring;
    p_lua_setfield = api_setfield;
    p_lua_createtable = api_createtable;
    assert(active(NULL) == 2 && api_bool == 1 && api_number == 0);
    api_replace_on_table = 1;
    assert(stats(NULL) == 1);  /* the Lua allocation replaces the static owner */
    assert(api_frames == 7 && api_armed == 1 && api_draw == 1 && !strcmp(api_mode, "diet"));
    assert(fd_state.frames == 0 && fd_state.mode == FD_PASS);
    assert(active(NULL) == 2 && api_bool == 0 && api_number == 0);  /* stale generation */
    assert(stats(NULL) == 1 && api_nil == 1);
    disarm_all();
    puts("PASS: Lua active ABI and stats snapshot survive allocation-time owner replacement without holding the lock");

    /* Histogram summary orders by count and lists at most six words. */
    {
        fd_stats s0;
        char words[128];
        memset(&s0, 0, sizeof(s0));
        for (unsigned w = 0; w < 8; ++w) s0.histogram[w + 1] = w + 1;
        top_words(&s0, words, sizeof(words));
        assert(!strcmp(words, "0x40:8,0x38:7,0x30:6,0x28:5,0x20:4,0x18:3"));
    }

    VirtualFree(fake_block, 0, MEM_RELEASE);
    VirtualFree(image, 0, MEM_RELEASE);
    puts("PASS: framediet synthetic lifecycle");
    return 0;
}
