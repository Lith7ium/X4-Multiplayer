/* Serial pathing and path determinism on the Linux X4 9.00 executable: the Linux twin of
 * the gate's path_release (lockstep_gate.c) and of lockstep_path_determinism.h, whose walk,
 * order, stamping and budget are shared. Included by gate_linux.c only.
 *
 * Boot option 1. MoveWorker::Run releases the pathing pass at 0x12125e3 (call 0x1a23be0
 * with manager+0x70; Windows ReleaseSemaphore on manager+0x18). The call is sent here: after
 * the release this thread waits for the pass on the completion semaphore manager+0x10,
 * puts that token back for the engine's own late join (WaitAndSwapBuffers 0x13e3560) and
 * goes on. So the universe and AI updates of the frame no longer race the pass.
 *
 * native_gate_path_determinism (mask, as on Windows): bit 1 stamps every batched agent's
 * NavigationController once per frame after the join, in saved-ID order, at the frame's game
 * time. Bit 2 lifts the 1 ms budget of UpdateBatch (manager+0x3a8, read at 0x13e3416), which
 * otherwise switches agents to cheap mode or rotates a batch cursor on wall-clock time.
 *
 * Linux layouts, from the pinned functions:
 *   manager  done semaphore +0x10, release semaphore +0x70, state +0x194, joined +0x198,
 *            batch vectors +0x2b0 and +0x2c8 of 0x20-byte batches (RunPass 0x13e3d9a..),
 *            budget +0x3a8. The semaphores are inline (mutex +0, condition +0x28, count
 *            +0x58, maximum +0x5c), not handles.
 *   agent    owner +0xf10, banks +0xaf0; nav controller owner +0x20, slots +0x28, agent +0x38.
 *   thread   CurTime fs:-0x2728, previous time fs:-0x2628 (SyncThreadGameTime 0x1a3b900).
 * Windows reads the walk under a fault guard. Here it reads directly: the same thread walks
 * the same lists UpdateBatch has just walked, the pass is joined and the lists stay unchanged
 * until the next release, and each link is checked before use (a null or a broken ring fails
 * the frame). A bad pointer would stop the process, which fails closed for a session. */
#define PATH_DET_OWN_PLATFORM
#define PATH_DET_MANAGER (LX_PATH_MANAGER_VA-LX_IMAGE_BASE)
#define PATH_DET_REFRESH (LX_PATH_REFRESH_VA-LX_IMAGE_BASE)
#define PATH_DET_SYNC (LX_PATH_SYNC_VA-LX_IMAGE_BASE)
#define PATH_DET_NAV_VTABLE (0x3b697e8u-LX_IMAGE_BASE) /* U::NavigationController (gate_world_classes.h) */
#define PATH_DET_READ_BANK (LX_BANK_READ_VA-LX_IMAGE_BASE)
#define PATH_DET_BUDGET LX_PATH_BUDGET
#define PATH_DET_BATCH_BEGIN 0x2b0
#define PATH_DET_BATCH_END 0x2b8
#define PATH_DET_AGENT_OWNER LXW_AGENT_OWNER
#define PATH_DET_AGENT_SLOTS LXW_AGENT_SLOTS
#define PATH_DET_NAV_OWNER LXW_NAV_OWNER
#define PATH_DET_NAV_SLOTS LXW_NAV_SLOTS
#define PATH_DET_NAV_AGENT LXW_NAV_AGENT
#define PATH_DET_TLS_TIME GATE_TLS_LINUX_TIME
#define PATH_DET_TLS_PREVIOUS GATE_TLS_LINUX_PREVIOUS_TIME
/* lockstep_world_identity.h's keys; created IDs only with native_gate_portable_created. */
enum {WORLD_KEY_SAVED=0,WORLD_KEY_CREATED=1,WORLD_KEY_NONE=2};
static uint64_t world_identity_key(uint64_t runtime,unsigned *klass) {
    uint64_t id=world_identity_loading(runtime); /* also before the seal: the event passes of the load */
    if(id) {*klass=WORLD_KEY_SAVED;return id;}
    if(runtime && world_key_portable() && (id=created_lookup(runtime))) {*klass=WORLD_KEY_CREATED;return id;}
    *klass=WORLD_KEY_NONE;return 0;
}
/* 0 while the portable keys are off (the saved table never changes after the seal). */
static uint64_t world_identity_generation(void) {
    return world_key_portable()?(uint64_t)InterlockedCompareExchange64(&world_created_generation,0,0):0;
}
/* No structured exceptions here (see above); the shared code's blocks run plainly. */
#define __try if(1)
#define __except(filter) else
#define __finally
#define read_local gate_read_local
#define LONG long /* the status prints the armed word with %ld */
static int path_det_pins(void) {return 1;} /* lx_sites, verified before anything is patched */
static int path_installed;
static unsigned path_options;
#include "lockstep_path_determinism.h"
#undef __try
#undef __except
#undef __finally
#undef read_local
#undef LONG
static int path_det_frame(unsigned char *manager) {
    return path_det_refresh_frame((path_det_refresh_fn)(void*)(image+PATH_DET_REFRESH),(path_det_sync_fn)(void*)(image+PATH_DET_SYNC),
        manager,gate_tls_self(),image+PATH_DET_NAV_VTABLE,(const volatile uint32_t*)(image+PATH_DET_READ_BANK));
}

/* ---- the release site ---- */
#define LXP_THUNK_HOOK 0x140u /* jmp [rip] -> lxp_path_release */
#define LXP_SEM_COUNT 0x58u
#define LXP_RELEASE_SEMAPHORE 0x70u
#define LXP_DONE_SEMAPHORE 0x10u
#define LXP_STATE 0x194u
#define LXP_JOINED 0x198u
#define LXP_JOIN_TIMEOUT_MS 5000u
typedef void (*lxp_release_fn)(void *,unsigned);
typedef int (*lxp_timed_wait_fn)(void *,unsigned);
static int path_restored;
static uint64_t path_joins,path_patched_word;
static volatile LONG path_pass_open;
static int lxg_before_release(void); /* the gated Gravidar pass (gate_gravidar.h) */
/* The travel snapshot (gate_travel_snapshot.h). */
static volatile long tsnap_active;
static volatile int64_t tsnap_failures;
static int tsnap_before_release(unsigned char *manager);
static int tsnap_after_join(unsigned char *manager);
static void lxp_path_release(void *semaphore,unsigned count) {
    const lxp_release_fn release=(lxp_release_fn)(void*)LX(LX_SEM_RELEASE_VA);
    if(boot_mode && boot_phase<3) {release(semaphore,count);return;}
    if(boot_mode) accept_ui();
    unsigned char *manager=*(unsigned char**)LX(LX_PATH_MANAGER_VA);
    int state=0;
    if(manager) memcpy(&state,manager+LXP_STATE,4);
    int valid=stats.active && GetCurrentThreadId()==ui_thread && manager && count==1 &&
        semaphore==(void*)(manager+LXP_RELEASE_SEMAPHORE) && state==2 && manager[LXP_JOINED]==0;
    if(valid && !lxg_before_release()) {release(semaphore,count);return;}
    if(valid && (path_det_mask&PATH_DET_BUDGET_BIT) && !path_det_budget_frame(manager)) {
        InterlockedIncrement64(&path_det_failures);finish(79);
        release(semaphore,count);return;
    }
    if(valid && InterlockedCompareExchange(&tsnap_active,0,0) && !tsnap_before_release(manager)) {
        InterlockedIncrement64(&tsnap_failures);finish(89);
        release(semaphore,count);return;
    }
    if(valid) InterlockedExchange(&path_pass_open,1);
    release(semaphore,count);
    if(!valid) {InterlockedExchange(&path_pass_open,0);finish(22);return;}
    if(path_options&2) { /* The frame seed, after the release as on Windows: same domain and key. */
        unsigned char *base=gate_tls_self();
        if(!base || !gate_tls_worker(base)) {finish(25);return;}
        *gate_tls_rng(base)=lockstep_seed(LX_SEED_DOMAIN,stats.issued,0,0,0,0);++rng_seeds;
    }
    /* The pass is done when its token arrives; replace exactly that token for the engine's join. */
    unsigned char *done=manager+LXP_DONE_SEMAPHORE;
    if(!((lxp_timed_wait_fn)(void*)LX(LX_SEM_TIMED_WAIT_VA))(done,LXP_JOIN_TIMEOUT_MS)) {finish(23);return;}
    uint32_t prior;memcpy(&prior,done+LXP_SEM_COUNT,4);
    if(prior) {finish(24);return;}
    release(done,1);
    ++path_joins;InterlockedExchange(&path_pass_open,0);
    /* Before anything flushes events: wait for late batch command-list splices. */
    if(InterlockedCompareExchange(&tsnap_active,0,0) && stats.active && !tsnap_after_join(manager)) {
        InterlockedIncrement64(&tsnap_failures);finish(89);
    }
    if((path_det_mask&PATH_DET_REFRESH_BIT) && stats.active && !path_det_frame(manager)) {
        InterlockedIncrement64(&path_det_failures);finish(79);
    }
}
/* The word holding the call: one aligned compare-exchange, the first 3 bytes kept. */
static uint64_t lxp_patched_word(void) {
    const uintptr_t site=(uintptr_t)LX(LX_PATH_RELEASE_SITE);
    intptr_t distance=(intptr_t)(thunk+LXP_THUNK_HOOK)-(intptr_t)(site+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    uint64_t word=LX_PATH_RELEASE_ORIGINAL;int32_t rel=(int32_t)distance;
    memcpy((unsigned char*)&word+(site&7)+1,&rel,4);
    return word;
}
static int path_install(unsigned options) {
    unsigned char *site=LX(LX_PATH_RELEASE_SITE),*word=(unsigned char*)((uintptr_t)site&~(uintptr_t)7);
    if(path_installed || !thunk || rd64(word)!=LX_PATH_RELEASE_ORIGINAL || !(path_patched_word=lxp_patched_word())) return 0;
    if(!gate_code_swap64(word,LX_PATH_RELEASE_ORIGINAL,path_patched_word)) return 0;
    path_options=options;path_installed=1;return 1;
}
static void path_restore(void) {
    if(!path_installed) return;
    unsigned char *word=(unsigned char*)((uintptr_t)LX(LX_PATH_RELEASE_SITE)&~(uintptr_t)7);
    path_restored=gate_code_swap64(word,path_patched_word,LX_PATH_RELEASE_ORIGINAL);
    if(path_restored) path_installed=0;
}
static int path_owned(void) {
    return !path_installed || __atomic_load_n((uint64_t*)((uintptr_t)LX(LX_PATH_RELEASE_SITE)&~(uintptr_t)7),__ATOMIC_SEQ_CST)==path_patched_word;
}
static void lxp_thunk_write(unsigned char *page) {lxw_jump_write(page+LXP_THUNK_HOOK,(void*)lxp_path_release);}
static int lua_path_determinism(lua_State *L) {
    double mask=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mask) && mask>=1 && mask<=3 && floor(mask)==mask && path_det_arm((unsigned)mask));return 1;
}
static int lua_path_determinism_status(lua_State *L) {
    char out[640];int n=accept_ui()?path_det_status(out,sizeof(out)):0;
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
