/* Default-off camera follow for the piloting prototype: a client whose pilot
 * slot ship is not the occupied player ship watches that ship through the
 * stock external target view, the call the stock UI makes for "target view"
 * (menu_interactmenu.lua buttonExternal, menu_map.lua F3):
 *   SetPlayerCameraTargetView 0x1b9230(UniverseID, force) -> Find 0xced70 ->
 *   0x9faad0(target, force, check_only=0); IsPlayerCameraTargetViewPossible
 *   0x1b3750 is the same worker with check_only=1, which returns at 0x9fac24
 *   before any change; GetExternalTargetViewComponent 0xafd280 reads the
 *   player camera (g_Universe 0x3d198f8 +0x3d0) target +0x770 -> +8 only.
 * The switch is NOT presentation-only by static reading: the worker posts the
 * player event event_player_activated_external_target_view (MD listener only
 * in scenario_tutorials.xml), calls the player/GameClient view hooks, inserts
 * into the zone recent-trees (0x8419b0, pruned by Zone::Move), requests the
 * external camera input contexts 0xd/0xe and draws thread RNG (0x97f5b0).
 * This wrapper therefore keeps the calling thread's RNG word (TLS+0x318) as it
 * was before the call (the draws land on a discarded copy, like the render
 * split) and counts AutoIDMap insertions (*0x6cf1500 +0x38) during the call,
 * so ids=0 / steps=N in the status show what the switch did on this client.
 * The event, the view hooks and the zone tree insert are not undone; they are
 * reported here and in docs/LOCKSTEP-SANDBOX.txt as unverified.
 * Rate: one attempt at most every PILOT_CAMERA_RETRY ticks and at most
 * PILOT_CAMERA_ATTEMPTS in total; a held view costs one getter call per tick.
 * Never called for the occupied ship (its cockpit view is already correct). */
#define PILOT_CAMERA_SET 0x1b9230u
#define PILOT_CAMERA_POSSIBLE 0x1b3750u
#define PILOT_CAMERA_CURRENT 0xafd280u
#define PILOT_CAMERA_WORKER 0x9faad0u
#define PILOT_CAMERA_ID_MAP 0x6cf1500u
#define PILOT_CAMERA_RETRY 60u
#define PILOT_CAMERA_ATTEMPTS 64u
typedef void (*pilot_camera_set_fn)(uint64_t,unsigned char);
typedef unsigned char (*pilot_camera_possible_fn)(uint64_t,unsigned char);
typedef uint64_t (*pilot_camera_current_fn)(void);
typedef struct {pilot_camera_set_fn set;pilot_camera_possible_fn possible;pilot_camera_current_fn current;} pilot_camera_api;
static int pilot_camera_enabled,pilot_camera_checked,pilot_camera_held;
static uint64_t pilot_camera_tick,pilot_camera_last_attempt,pilot_camera_attempts,pilot_camera_applied,pilot_camera_hold_ticks,
    pilot_camera_lost,pilot_camera_refused,pilot_camera_steps,pilot_camera_unknown,pilot_camera_ids,pilot_camera_target;
static const char *pilot_camera_stage="off";
static int pilot_camera_pins(void) {
    if(!pilot_camera_checked) pilot_camera_checked=
        hash_ok(image+PILOT_CAMERA_SET,56,"0e2bf084c10d06627552f4e24838c733974245484eaa3aea6ed6d313fee3cb39") &&
        hash_ok(image+PILOT_CAMERA_POSSIBLE,56,"33ed5545283ccc7429d7af1752b1daf356a7ab5e73d74edec87b16e3b3e3fc73") &&
        hash_ok(image+PILOT_CAMERA_CURRENT,41,"1676383ebd57066fefa93ca02e48f3167bbebf4e6cf2c9fb3b1259ef9bb4d4ef") &&
        hash_ok(image+PILOT_CAMERA_WORKER,1605,"c6137d093484d23a77c5b22ef4d9345588869a435ac4eece1f14bc2c9c652daf")?1:-1;
    return pilot_camera_checked==1;
}
/* Before the window: requires the armed prototype; identical reruns succeed. */
static int pilot_camera_arm(void) {
    if(pilot_camera_enabled) return pilot_armed;
    if(!pilot_armed) return 0;
    pilot_camera_enabled=1;pilot_camera_stage="armed";return 1;
}
/* One admitted tick on the UI thread. word/inserts are the calling thread's RNG
 * word and the AutoIDMap insert counter (NULL when unavailable: no counting). */
static const char *pilot_camera_step_using(const pilot_camera_api *api,uint64_t tick,volatile uint64_t *word,const volatile uint64_t *inserts) {
    if(!pilot_camera_enabled) return pilot_camera_stage="off";
    pilot_camera_tick=tick;
    if(!word) return pilot_camera_stage="thread";
    if(stats.slot<1 || stats.slot>pilot_slot_count) return pilot_camera_stage="no_slot";
    pilot_slot *s=&pilot_slots[stats.slot-1];
    if(!s->resolved || !s->runtime) return pilot_camera_stage="unresolved";
    if(s->player) return pilot_camera_stage="own_ship";
    pilot_camera_target=s->saved;
    const uint64_t caller=*word,ids=inserts?*inserts:0;volatile uint64_t after=caller;const char *volatile stage="held";
    __try {
        if(api->current()==s->runtime) {++pilot_camera_hold_ticks;pilot_camera_held=1;}
        else {
            if(pilot_camera_held) {++pilot_camera_lost;pilot_camera_held=0;}
            if(pilot_camera_attempts>=PILOT_CAMERA_ATTEMPTS) stage="attempts_exhausted";
            else if(pilot_camera_attempts && tick<pilot_camera_last_attempt+PILOT_CAMERA_RETRY) stage="waiting";
            else {
                pilot_camera_last_attempt=tick;++pilot_camera_attempts;
                if(!api->possible(s->runtime,1)) {++pilot_camera_refused;stage="not_possible";}
                else {
                    rid_scope(1);api->set(s->runtime,1);rid_scope(0); /* its IDs are this client's alone */
                    if(api->current()==s->runtime) {++pilot_camera_applied;pilot_camera_held=1;stage="applied";}
                    else stage="not_applied";
                }
            }
        }
    } __finally {
        DWORD error=GetLastError();
        after=*word;*word=caller; /* draws made by the switch never reach the simulation stream */
        rid_scope(0);
        if(AbnormalTermination()) stage="exception";
        SetLastError(error);
    }
    uint64_t steps=render_rng_distance(caller,after);
    if(steps==UINT64_MAX) ++pilot_camera_unknown;else pilot_camera_steps+=steps;
    if(inserts) pilot_camera_ids+=*inserts-ids;
    return pilot_camera_stage=stage;
}
static const char *pilot_camera_step(uint64_t tick) {
    pilot_camera_api api;memset(&api,0,sizeof(api));
    if(!pilot_camera_enabled) return pilot_camera_stage="off";
    if(!stats.active || !accept_ui()) return pilot_camera_stage="guard";
    if(!pilot_camera_pins()) return pilot_camera_stage="pins";
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    unsigned char *tls=slots?(unsigned char*)slots[0]:NULL,*map=NULL;uint32_t role=0;
    if(!tls || !read_local(tls+0x314,&role,4) || role!=1) return pilot_camera_step_using(&api,tick,NULL,NULL);
    api.set=(pilot_camera_set_fn)(image+PILOT_CAMERA_SET);
    api.possible=(pilot_camera_possible_fn)(image+PILOT_CAMERA_POSSIBLE);
    api.current=(pilot_camera_current_fn)(image+PILOT_CAMERA_CURRENT);
    if(!read_local(image+PILOT_CAMERA_ID_MAP,&map,8)) map=NULL;
    return pilot_camera_step_using(&api,tick,(volatile uint64_t*)(tls+0x318),map?(const volatile uint64_t*)(map+0x38):NULL);
}
static int pilot_camera_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"stage=%s tick=%llu target=%016llx held=%d hold_ticks=%llu attempts=%llu applied=%llu lost=%llu "
        "refused=%llu steps=%llu unknown=%llu ids=%llu",pilot_camera_stage,(unsigned long long)pilot_camera_tick,
        (unsigned long long)pilot_camera_target,pilot_camera_held,(unsigned long long)pilot_camera_hold_ticks,
        (unsigned long long)pilot_camera_attempts,(unsigned long long)pilot_camera_applied,(unsigned long long)pilot_camera_lost,
        (unsigned long long)pilot_camera_refused,(unsigned long long)pilot_camera_steps,(unsigned long long)pilot_camera_unknown,
        (unsigned long long)pilot_camera_ids);
    return n>0 && (size_t)n<capacity?n:0;
}
