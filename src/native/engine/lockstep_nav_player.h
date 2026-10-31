/* Determinism fix, DEFAULT OFF: the player's own navigation upkeep in the nav
 * cadence module (native_gate_nav_player; needs native_gate_nav_cadence).
 * PlayerController::vfunc33 0xd97e80 (vtable 0x2c39488 slot 33, entry
 * 0x2c39590) takes its Agent branch while the autopilot runs (0x2f4b7d8 tested
 * at 0xd981bb) and does the occupied ship's navigation upkeep by a direct call
 * of NavigationController::vfunc33 0xea77f0 at 0xd98476 (the image's only
 * direct call of it; time = its own argument, kept in xmm7). The nav module
 * wraps the NavigationController entry 0x2c51260 only, so for the player's
 * controller: native_gate_nav_probe_init neither pins nor resets the obstacle-
 * probe deadline (Agent+0xac0, which the Agent constructor drew unkeyed while
 * loading, so per client), the cadence counters skip it and
 * native_gate_nav_cadence_watch logs no NAV_CADENCE_ROW for its ship (fly6a: 0
 * rows for the player ship 0ae79c). Highway fly runs part on one client only,
 * as discrete alternatives at ticks 212, 342 and 382, while the autopiloted
 * player brakes for the battle ships: a periodic player refresh whose phase is
 * per client and matters only when an obstacle is near.
 * This module points entry 0x2c39590 at a wrapper (data only, no thunk; a CAS
 * from the original, so it and the player observer exclude each other) that
 * brackets the original as the nav wrapper brackets NavigationController
 * upkeep, on the MoveWorker in admitted frames only: nav_cadence_enter before
 * (the probe and parameter deadline inits when armed; fly7a, with the probe
 * init only: the player's parameter deadline kept its loading value, 8.28 vs
 * 8.66 s) and nav_cadence_leave after (counted as 'other',
 * a row when the ship is watched), plus one NAV_CADENCE_ROW per state change of
 * the player controller itself (id=0, saved = its ship's portable key) when the
 * ship is not watched. Only calls that take the Agent branch are bracketed
 * (the autopilot flag 0x2f4b7d8 set, as 0xd981bb tests it): under manual flight
 * vfunc33 never reaches the nav upkeep, nothing reads the probe deadline
 * (its only reader is 0xea7bef, while +0x3e8 is set) and the call passes
 * through, counted as manual= (big-BS6q: the player sat in manual flight).
 * Every other call passes straight through. The status
 * says which RNG word the upkeep's 'far' draw used: scoped = inside a ship's
 * actor_all umbrella (keyed per tick and ship), unscoped = the frame's shared
 * word; drawn = calls after which the current word had moved. Pins: the
 * prologue up to movaps xmm7,xmm1, the autopilot branch and the direct call.
 * Every gate tick re-checks entry and pins (stop reason 95, the nav cadence
 * reason); stop restores by CAS and leaves a foreign value in place. */
#define NAV_PLAYER_ENTRY 0x2c39590u  /* PlayerController vtable 0x2c39488 slot 33 */
#define NAV_PLAYER_MOVE 0xd97e80u    /* PlayerController::vfunc33 */
#define NAV_PLAYER_HEAD 0x52u        /* prologue through movaps xmm7,xmm1 (0xd97ecf) */
#define NAV_PLAYER_HEAD_SHA "d64ba241e787c25c58da91288c68312ae3104604244d7d454bae839172e0f4e6"
#define NAV_PLAYER_BRANCH 0xd981bbu  /* cmp [0x2f4b7d8],r15; jne 0xd98470 */
#define NAV_PLAYER_CALL 0xd98470u    /* movaps xmm1,xmm7; mov rcx,rsi; call 0xea77f0 */
#define NAV_PLAYER_AUTOPILOT 0x2f4b7d8u /* qword, nonzero: vfunc33 takes the Agent branch */
typedef void (*nav_player_fn)(void *,double);
static const unsigned char nav_player_branch[13]={0x4c,0x39,0x3d,0x16,0x36,0x1b,0x02,0x0f,0x85,0xa8,0x02,0x00,0x00};
static const unsigned char nav_player_call[11]={0x0f,0x28,0xcf,0x48,0x8b,0xce,0xe8,0x75,0xf3,0x10,0x00};
static int nav_player_installed,nav_player_restored,nav_player_seen;
static volatile LONG64 nav_player_calls,nav_player_failures;
static uint64_t nav_player_admitted,nav_player_manual,nav_player_scoped,nav_player_unscoped,nav_player_drawn,nav_player_rows,nav_player_unmapped;
static uint64_t nav_player_tick=UINT64_MAX,nav_player_frame_admitted;
static nav_cadence_state nav_player_last;
static nav_player_fn nav_player_original_override; /* tests only; NULL runs PlayerController::vfunc33 */
static const char *nav_player_stage="idle";
static int nav_player_pins(void) {
    unsigned char branch[sizeof(nav_player_branch)],call[sizeof(nav_player_call)];
    return hash_ok(image+NAV_PLAYER_MOVE,NAV_PLAYER_HEAD,NAV_PLAYER_HEAD_SHA) &&
        read_local(image+NAV_PLAYER_BRANCH,branch,sizeof(branch)) && !memcmp(branch,nav_player_branch,sizeof(branch)) &&
        read_local(image+NAV_PLAYER_CALL,call,sizeof(call)) && !memcmp(call,nav_player_call,sizeof(call));
}
static void nav_player_move(void *self,double time);
static int nav_player_owned(void) {return !nav_player_installed || *(void*volatile*)(image+NAV_PLAYER_ENTRY)==(void*)nav_player_move;}
/* The player controller's own state row (skipped when its ship is watched: the watch row has it). */
static void nav_player_row(const void *controller) {
    nav_cadence_sample a;unsigned char *ship=NULL;uint64_t runtime=0,key=0,saved;unsigned klass=0;
    if(!nav_cadence_read(controller,&a)) return;
    if(!read_local((const unsigned char*)controller+0x28,&ship,8) || !ship || !read_local(ship+8,&runtime,8) || !runtime ||
       !(key=world_identity_key(runtime,&klass))) {++nav_player_unmapped;return;}
    if((saved=world_identity_lookup(runtime))) for(unsigned i=0;i<nav_cadence_watch_count;i++) if(nav_cadence_watch_ids[i]==saved) return;
    nav_cadence_state now;nav_cadence_fixture_state(controller,&a,&now);
    if(nav_player_seen && nav_cadence_state_same(&now,&nav_player_last)) return;
    nav_player_seen=1;nav_player_last=now;++nav_player_rows;nav_cadence_push(0,key,&now);
}
/* rng: the calling thread's current RNG word (NULL when unreadable). */
static void nav_player_run(nav_player_fn original,void *self,double time,const uint64_t *rng) {
    InterlockedIncrement64(&nav_player_calls);
    if(!stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) {original(self,time);return;}
    if(nav_player_tick!=stats.issued) {nav_player_tick=stats.issued;nav_player_frame_admitted=0;}
    ++nav_player_admitted;++nav_player_frame_admitted;
    uint64_t autopilot=0;
    if(!read_local(image+NAV_PLAYER_AUTOPILOT,&autopilot,8) || !autopilot) {++nav_player_manual;original(self,time);return;}
    if(actor_all_ship_depth) ++nav_player_scoped;else ++nav_player_unscoped;
    uint64_t before=rng?*rng:0;
    nav_cadence_sample cadence;nav_cadence_enter(self,time,&cadence);
    original(self,time);
    if(rng && *rng!=before) ++nav_player_drawn;
    nav_cadence_leave(self,&cadence,1,0);
    if(cadence.valid) nav_player_row(self);
}
/* noinline keeps the TEB read out of the caller. */
static __attribute__((noinline)) void nav_player_move(void *self,double time) {
    nav_player_run(nav_player_original_override?nav_player_original_override:(nav_player_fn)(image+NAV_PLAYER_MOVE),self,time,actor_all_tls_rng());
}
/* After native_gate_nav_cadence (and its probe init), before the first admitted frame. */
static int nav_player_arm(void) {
    if(nav_player_installed) return stats.active && nav_cadence_installed; /* Identical rerun after a loading-UI reload. */
    if(!nav_cadence_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll")) {nav_player_stage="foreign_module";return 0;}
    if(!nav_player_pins()) {nav_player_stage="pins";return 0;}
    if(!script_rng_slot_swap(NAV_PLAYER_ENTRY,image+NAV_PLAYER_MOVE,(void*)nav_player_move)) {nav_player_stage="entry";return 0;}
    nav_player_calls=nav_player_failures=0;nav_player_admitted=nav_player_manual=nav_player_scoped=nav_player_unscoped=nav_player_drawn=0;
    nav_player_rows=nav_player_unmapped=nav_player_frame_admitted=0;nav_player_tick=UINT64_MAX;nav_player_seen=0;
    nav_player_installed=1;nav_player_restored=0;nav_player_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 95. */
static int nav_player_check(void) {
    if(InterlockedCompareExchange64(&nav_player_failures,0,0)) return 0;
    const char *why=!nav_player_owned()?"entry_changed":!nav_player_pins()?"block_changed":NULL;
    if(why) {nav_player_stage=why;InterlockedIncrement64(&nav_player_failures);return 0;}
    return 1;
}
static void nav_player_restore(void) {
    if(!nav_player_installed) return;
    if(!script_rng_slot_swap(NAV_PLAYER_ENTRY,(void*)nav_player_move,image+NAV_PLAYER_MOVE)) return; /* a foreign value stays */
    nav_player_installed=0;nav_player_restored=1;
}
static int nav_player_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d frame=%llu frame_calls=%llu calls=%lld admitted=%llu manual=%llu scoped=%llu unscoped=%llu drawn=%llu "
        "rows=%llu unmapped=%llu restored=%d stage=%s failures=%lld",
        nav_player_installed,(unsigned long long)(nav_player_tick==UINT64_MAX?0:nav_player_tick),
        (unsigned long long)(nav_player_tick==stats.issued?nav_player_frame_admitted:0),(long long)nav_player_calls,
        (unsigned long long)nav_player_admitted,(unsigned long long)nav_player_manual,(unsigned long long)nav_player_scoped,(unsigned long long)nav_player_unscoped,
        (unsigned long long)nav_player_drawn,(unsigned long long)nav_player_rows,(unsigned long long)nav_player_unmapped,
        nav_player_restored,nav_player_stage,(long long)nav_player_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
