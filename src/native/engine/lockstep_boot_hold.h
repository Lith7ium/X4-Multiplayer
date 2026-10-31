/* Boot hold, DEFAULT OFF (native_gate_boot_hold): the first AIDirector::Move
 * after a snapshot load runs in the first gated frame instead of boot phase 2.
 *
 * What boot phase 2 runs (static analysis of X4 9.00; census runs A01-T08):
 * the main thread's loading frames (FrameTick 0xfa2d70, loading branch) run
 * the loading bank and present; no script runs on them. The MoveWorker is in
 * one iteration throughout: module slot 3 (0xab1100) calls EnterLoadingMode
 * 0xac2a50, GameClass::Load 0x9ee7a0 (MD start-up 0x1120600 with
 * MDScheduler_Update, AIDirector_Import 0x1069000, then
 * EventSystem_ProcessDueEvents at 0x9f0a43: boot event pass 1) and
 * LeaveLoadingMode 0xac2c70. That parks on g_semSimStart until the next
 * loading frame clears the loading flag and releases it, then the same
 * iteration ends with one UniverseFrameUpdate 0x9fdef0: boot event pass 2 at
 * the frozen load time, MDScheduler_Update, then the first AIDirector::Move
 * 0x106d250, which resumes every imported context whose blocking action does
 * not reattach (0x10666e0: wake flag 0x80) and runs them. MainMessageLoop
 * then waits for g_semSimDone, so the next FrameTick sees no loading flag and
 * is admitted as tick 1. The number of loading frames never changes this
 * work (boot_passes=2 in all 26 census clients A01-T08; held Gravidar
 * publishes equal on both clients of every run). But that one Move runs
 * while ungated threads still work: the Gravidar load pass that
 * GameClass::Load kicks (drained only at admission) and the pathing pass
 * (joined serially only in gated frames). In A02 (12,266 vs 12,264 tick-0
 * entries) the retained tick-0 rows of both clients (the last ~1,900; the
 * ring evicted the rest) are identical up to the last segment of that Move,
 * whose resumed set and order differ (move.idle of one owner at tick 0 on
 * client 1 and at tick 1 on client 2; fighters resumed at one PC reordered).
 *
 * Hold: the call at 0x9fe093 (UniverseFrameUpdate -> AIDirector::Move, its
 * only caller) is redirected through THUNK_JUMP 2032 (rel32 inside one
 * aligned qword CAS). While the gate is active in boot phase 2 with no
 * admitted tick, the call returns without running Move: wake requests stay in
 * the current wake vector (dir+0x200/+0x218; the index +0x230 is not toggled),
 * timed wakes and pending resumes stay queued, exactly as before any Move.
 * Every other call runs Move unchanged, so the first gated frame's Move runs
 * them after the Gravidar boot drain and the serial pathing join. Events
 * delivered to contexts before their first Move already happen natively
 * (boot passes 1 and 2); the hold adds tick 1's pass. Admission requires at
 * least one held call. Failures (another thread or director in phase 2, no
 * hold at admission) stop the gate with reason 86; stop restores the call. */
/* The Linux gate (linux/gate_boot_hold.h) defines the director, and its own patch of Move. */
#ifndef BOOT_HOLD_OWN_PLATFORM
#define BOOT_HOLD_SITE 0x9fe093u
#define BOOT_HOLD_WORD 0x9fe090u
#define BOOT_HOLD_MOVE 0x106d250u
#define BOOT_HOLD_DIRECTOR 0x6cf15f0u
#define BOOT_HOLD_THUNK 2032u
#endif
typedef void (*boot_hold_fn)(void *);
static volatile LONG boot_hold_active;
static int boot_hold_armed,boot_hold_installed,boot_hold_restored,boot_hold_admitted;
static uint64_t boot_hold_original_word,boot_hold_patched_word,boot_hold_held,boot_hold_moves,boot_hold_released;
static uint64_t boot_hold_pending=UINT64_MAX,boot_hold_admit_pending=UINT64_MAX,boot_hold_release_pending=UINT64_MAX;
static volatile LONG64 boot_hold_failures;
static const char *boot_hold_stage="idle";

/* Queued wake requests in both wake vectors ({begin,end,capacity} at +0x200
 * and +0x218, 8-byte context pointers); UINT64_MAX when unreadable. Evidence
 * only: the hold never depends on it. */
static uint64_t boot_hold_wakes(const void *director) {
    uintptr_t v[6];uint64_t n=0;
    if(!director || !read_local((const unsigned char*)director+0x200,v,sizeof(v))) return UINT64_MAX;
    for(unsigned i=0;i<2;i++) {
        uintptr_t begin=v[i*3],end=v[i*3+1];
        if(end<begin || ((end-begin)&7) || (end-begin)/8>0x1000000) return UINT64_MAX;
        n+=(end-begin)/8;
    }
    return n;
}
static void boot_hold_execute(boot_hold_fn original,void *director) {
    ++boot_hold_moves;
    if(InterlockedCompareExchange(&boot_hold_active,0,0) && stats.active && boot_mode && boot_phase==2 && !stats.issued) {
        if(GetCurrentThreadId()==ui_thread && director==*(void**)(image+BOOT_HOLD_DIRECTOR)) {
            ++boot_hold_held;boot_hold_pending=boot_hold_wakes(director);boot_hold_stage="held";return;
        }
        InterlockedIncrement64(&boot_hold_failures);boot_hold_stage="hold_foreign";
    } else if(boot_hold_admitted && !boot_hold_released && stats.active && stats.issued) {
        boot_hold_released=stats.issued;boot_hold_release_pending=boot_hold_wakes(director);boot_hold_stage="released";
    }
    original(director);
}
#ifndef BOOT_HOLD_OWN_PLATFORM
/* noinline keeps the thunk's native return address out of any inlined frame. */
static __attribute__((noinline)) void boot_hold_move(void *director) {
    boot_hold_execute((boot_hold_fn)(image+BOOT_HOLD_MOVE),director);
}
#endif
/* Main thread, boot phase 3 entry, after the Gravidar boot drain; the worker
 * is parked on g_semSimStart. */
static int boot_hold_admit(void) {
    if(!boot_hold_armed) return 1;
    boot_hold_admit_pending=boot_hold_wakes(*(void**)(image+BOOT_HOLD_DIRECTOR));
    if(!boot_hold_held) {boot_hold_stage="admit_unheld";return 0;}
    boot_hold_admitted=1;boot_hold_stage="admitted";return 1;
}
#ifndef BOOT_HOLD_OWN_PLATFORM
/* The caller (three loads and the call) and Move's wake-vector prologue. */
static int boot_hold_pinned(void) {
    return hash_ok(image+0x9fe074,36,"499df8a5f99493b2b3199842ef4b9d513dc97e0f97ced81291dfd3c826f3e2b7") &&
        hash_ok(image+BOOT_HOLD_MOVE,150,"f00ec65bb7ea48bf27342be60901603dded6d715d074d47300c7780c8ddc378b");
}
static int boot_hold_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+BOOT_HOLD_WORD,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+BOOT_HOLD_WORD),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+BOOT_HOLD_WORD,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+BOOT_HOLD_WORD,8);
    return actual==expected;
}
static int boot_hold_owned(void) {return !boot_hold_installed || *(uint64_t*)(image+BOOT_HOLD_WORD)==boot_hold_patched_word;}
/* Armed once at boot, before load (idempotent on a loading-UI reload). */
static int boot_hold_arm(void) {
    if(boot_hold_armed) return stats.active && boot_hold_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !boot_hold_pinned() || !thunk_slot_ready(BOOT_HOLD_THUNK)) return 0;
    unsigned char call[5];int32_t rel;
    if(!read_local(image+BOOT_HOLD_SITE,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);if((uintptr_t)(image+BOOT_HOLD_SITE+5)+(intptr_t)rel!=(uintptr_t)(image+BOOT_HOLD_MOVE)) return 0;
    intptr_t distance=(intptr_t)(thunk+BOOT_HOLD_THUNK)-(intptr_t)(image+BOOT_HOLD_SITE+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&boot_hold_original_word,image+BOOT_HOLD_WORD,8);boot_hold_patched_word=boot_hold_original_word;
    rel=(int32_t)distance;memcpy((unsigned char*)&boot_hold_patched_word+(BOOT_HOLD_SITE+1-BOOT_HOLD_WORD),&rel,4);
    if(!boot_hold_swap(boot_hold_original_word,boot_hold_patched_word)) return 0;
    boot_hold_installed=1;boot_hold_armed=1;boot_hold_stage="armed";InterlockedExchange(&boot_hold_active,1);return 1;
}
static void boot_hold_restore(void) {
    if(!boot_hold_armed) return;
    InterlockedExchange(&boot_hold_active,0);
    if(boot_hold_installed && boot_hold_swap(boot_hold_patched_word,boot_hold_original_word)) boot_hold_installed=0;
    boot_hold_restored=!boot_hold_installed;
}
#endif
static int boot_hold_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld held=%llu moves=%llu pending=%lld admitted=%d admit_pending=%lld"
        " released=%llu release_pending=%lld restored=%d stage=%s failures=%lld",
        boot_hold_armed,boot_hold_installed,boot_hold_active,(unsigned long long)boot_hold_held,(unsigned long long)boot_hold_moves,
        (long long)boot_hold_pending,boot_hold_admitted,(long long)boot_hold_admit_pending,(unsigned long long)boot_hold_released,
        (long long)boot_hold_release_pending,boot_hold_restored,boot_hold_stage,(long long)boot_hold_failures);
    return n>0 && (size_t)n<cap?n:0;
}
