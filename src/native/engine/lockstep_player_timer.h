/* Determinism fix, DEFAULT OFF: the player's fleet rebuild timer
 * (native_gate_player_timer). U::Player::vfunc674 0x7113e0 (the player
 * entity's Move: vtable 0x2b329a8 slot 674, entry 0x2b33eb8; called by
 * Positional::Move 0x748650 inside the occupied ship's actor_all scope) checks
 * the deadline Player+0x8a538 against the TLS game time (slot0+0x300) at
 * 0x71335b; once due it walks the fleet rebuild records (tree 0x6cf1870,
 * 0x56f800 per record: 'reconstitute_fleet' for player-faction fleets) and
 * re-arms it as time + 60 + FloatState(TLS+0x318, 10) at 0x7133e0..0x71340b.
 * The first deadline comes from the Player constructor 0x6fa8e0 (time + 10 +
 * FloatState(word, 10) at 0x6fae62..0x6fae91, drawn unkeyed while loading) and
 * the save import (vfunc568) does not restore it, so each client starts with
 * its own. long-L16 (native_gate_draw_watch on 0ae79c): the first check ran at
 * tick 707 on client 2 and 1135 on client 1 (both 10..20 s after loading), each
 * one FloatState draw (rip 0x14ddcf0, chain 0x7133f5, 0x748656) in the ship
 * scope on that client only (ACTOR_RNG_ROW draws=1), so the ship's word parted.
 * This module points entry 0x2b33eb8 at a wrapper (data only, no thunk; a CAS
 * from the original) that, at the first call per Player object on the
 * MoveWorker in an admitted frame, sets the deadline to that frame's time +
 * 15 s (the constructor's 10 s plus half its jitter) before running the
 * original: the first check and its re-arm draw then come on the same tick on
 * every client, and the re-arm draw is keyed by the scope it runs in. Other
 * calls pass straight through; the status counts checks (fires=, the tick of
 * the last one, the deadline it drew) for the census to compare. The autopilot
 * failure deadline (+0x8a540, 'autopilot_epically_failed' 0x72c720) also starts
 * from a constructor draw, but the import restores it (0x6fd6be); it is left
 * alone and reported (autopilot_due=). Pins: the prologue through mov r15,rcx,
 * the check/re-arm block and the constructor's first deadline. Every gate tick
 * re-checks entry and pins (stop reason 142); stop restores by CAS and leaves a
 * foreign value in place. */
#define PT_ENTRY 0x2b33eb8u   /* U::Player vtable 0x2b329a8 slot 674 */
#define PT_MOVE 0x7113e0u     /* U::Player::vfunc674 */
#define PT_HEAD 0x65u         /* prologue through mov r15,rcx (0x711442) */
#define PT_HEAD_SHA "424a6962b251af56b9ae7a581ba4541a79ae6566c3dd66e70ca137c7fdb6da0b"
#define PT_CHECK 0x71335bu    /* deadline test, record walk and re-arm, through 0x713414 */
#define PT_CHECK_SIZE 0xb9u
#define PT_CHECK_SHA "5d029c11031597bf677aac34c109f9a855f497fb42a129b5fcd710b27890da97"
#define PT_INIT 0x6fae62u     /* constructor: the first deadline, through 0x6fae99 */
#define PT_INIT_SIZE 0x37u
#define PT_INIT_SHA "918a40513ded81b214601672393bfde048983e77cae1bd967e19d98ae5b89250"
#define PT_DUE 0x8a538u       /* Player: fleet rebuild deadline (double, game time) */
#define PT_AUTOPILOT_DUE 0x8a540u
#define PT_FIRST 15.0
#define PT_OBJECTS 4u
#define PT_STOP_REASON 142u
typedef void (*pt_fn)(void *);
static int pt_installed,pt_restored;
static volatile LONG64 pt_calls,pt_failures;
static uint64_t pt_admitted,pt_rebased,pt_first,pt_fires,pt_last_fire,pt_unreadable,pt_full;
static uint64_t pt_before,pt_after,pt_autopilot,pt_next; /* double bits */
static const void *pt_objects[PT_OBJECTS];
static unsigned pt_object_count;
static pt_fn pt_original_override; /* tests only; NULL runs U::Player::vfunc674 */
static const char *pt_stage="idle";
static int pt_pins(void) {
    return hash_ok(image+PT_MOVE,PT_HEAD,PT_HEAD_SHA) && hash_ok(image+PT_CHECK,PT_CHECK_SIZE,PT_CHECK_SHA) &&
        hash_ok(image+PT_INIT,PT_INIT_SIZE,PT_INIT_SHA);
}
static void pt_move(void *self);
static int pt_owned(void) {return !pt_installed || *(void*volatile*)(image+PT_ENTRY)==(void*)pt_move;}
/* 1 when the object is new (and now recorded), 0 when known, -1 when the table is full. */
static int pt_first_call(const void *self) {
    for(unsigned i=0;i<pt_object_count;i++) if(pt_objects[i]==self) return 0;
    if(pt_object_count>=PT_OBJECTS) return -1;
    pt_objects[pt_object_count++]=self;return 1;
}
/* tls: the calling thread's TLS block (slot 0), NULL when unset. */
static void pt_run(pt_fn original,void *self,const unsigned char *tls) {
    InterlockedIncrement64(&pt_calls);
    if(!stats.active || boot_phase!=3 || !stats.issued || GetCurrentThreadId()!=ui_thread) {original(self);return;}
    unsigned char ready=0;double now=0,due=0;
    if(!tls || !read_local(tls+0x10,&ready,1) || !ready || !read_local(tls+0x300,&now,8) || !isfinite(now) ||
       !read_local((unsigned char*)self+PT_DUE,&due,8)) {++pt_unreadable;original(self);return;}
    ++pt_admitted;
    int first=pt_first_call(self);
    if(first<0) ++pt_full;
    else if(first) {
        double value=now+PT_FIRST;
        memcpy(&pt_before,&due,8);memcpy((unsigned char*)self+PT_DUE,&value,8);memcpy(&pt_after,&value,8);due=value;
        if(!read_local((unsigned char*)self+PT_AUTOPILOT_DUE,&pt_autopilot,8)) pt_autopilot=0;
        ++pt_rebased;if(!pt_first) pt_first=stats.issued;
    }
    original(self);
    double after=0;
    if(read_local((unsigned char*)self+PT_DUE,&after,8) && memcmp(&after,&due,8)) {++pt_fires;pt_last_fire=stats.issued;memcpy(&pt_next,&after,8);}
}
/* noinline keeps the TEB read out of the caller. */
static __attribute__((noinline)) void pt_move(void *self) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    pt_run(pt_original_override?pt_original_override:(pt_fn)(image+PT_MOVE),self,tls?(const unsigned char*)tls[0]:NULL);
}
/* Before the first admitted frame, on the MoveWorker. */
static int pt_arm(void) {
    if(pt_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!pt_pins()) {pt_stage="pins";return 0;}
    if(!script_rng_slot_swap(PT_ENTRY,image+PT_MOVE,(void*)pt_move)) {pt_stage="entry";return 0;}
    pt_calls=pt_failures=0;pt_admitted=pt_rebased=pt_first=pt_fires=pt_last_fire=pt_unreadable=pt_full=0;
    pt_before=pt_after=pt_autopilot=pt_next=0;memset(pt_objects,0,sizeof(pt_objects));pt_object_count=0;
    pt_installed=1;pt_restored=0;pt_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 142. */
static int pt_check(void) {
    if(InterlockedCompareExchange64(&pt_failures,0,0)) return 0;
    const char *why=!pt_owned()?"entry_changed":!pt_pins()?"block_changed":NULL;
    if(why) {pt_stage=why;InterlockedIncrement64(&pt_failures);return 0;}
    return 1;
}
static void pt_restore(void) {
    if(!pt_installed) return;
    if(!script_rng_slot_swap(PT_ENTRY,(void*)pt_move,image+PT_MOVE)) return; /* a foreign value stays */
    pt_installed=0;pt_restored=1;
}
static int pt_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d calls=%lld admitted=%llu rebased=%llu first=%llu before=%016llx due=%016llx autopilot_due=%016llx "
        "fires=%llu last_fire=%llu next=%016llx objects=%u unreadable=%llu full=%llu restored=%d stage=%s failures=%lld",
        pt_installed,(long long)pt_calls,(unsigned long long)pt_admitted,(unsigned long long)pt_rebased,(unsigned long long)pt_first,
        (unsigned long long)pt_before,(unsigned long long)pt_after,(unsigned long long)pt_autopilot,(unsigned long long)pt_fires,
        (unsigned long long)pt_last_fire,(unsigned long long)pt_next,pt_object_count,(unsigned long long)pt_unreadable,(unsigned long long)pt_full,
        pt_restored,pt_stage,(long long)pt_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
