/* Determinism fix, DEFAULT OFF: ship sound starts draw from a private RNG word
 * (native_gate_ship_sound_rng = 'split' | 'observe').
 * Ship vtable slot 1143 (+0x23b8) is 0x7c8bc0 in all seven ship-family vtables
 * (Ship 0x2b84a98, ShipExtraSmall 0x2b7d738, ShipSmall 0x2b7feb8, ShipMedium
 * 0x2b82440, ShipLarge 0x2b9bff8, ShipExtraLarge 0x2b99a88, SpaceSuit
 * 0x2b789b0; no direct caller). It starts a set of the ship's sounds (ship+0xb68,
 * 0x3e7ee0 into the XSfx code) and draws from the calling thread's word
 * TLS+0x318 (FloatState at 0x14eefdf, Random::I32 through 0x14f2550); slot 1144
 * (the stop, 0x7c8c60) draws nothing. Two of its callers decide per client:
 * PlayerController::vfunc33's tail (0xd98622..0xd98669: local input axis
 * 0x2f4b78c with a 0.5 s hold) and Ship::OnCameraEntered 0x7cddc0 (camera
 * context changes, which follow the real-time camera smoothing). long-L14: the
 * player's own ship scope (actor_all, 0ae79c) drew once more on client 2 at
 * tick 669 and on client 1 at 786, and the run parted by the 1500 capture.
 * 'split' points the seven slots at a wrapper (data CAS, no thunk) that runs
 * the original with the thread's word set to a private per-call word (seeded
 * from a call counter, never the simulation's) and puts the caller's word back
 * afterwards, also when an exception unwinds: a sound start consumes no
 * simulation draw. 'observe' keeps the wrapper but not the swap and counts the
 * draws taken from the caller's word. Status: calls, calls that drew, draws
 * (bounded step count; unknown beyond it), calls off the MoveWorker, per
 * admitted frame and in total. Pins: the 0x98-byte body of 0x7c8bc0 and every
 * slot. The gate tick re-checks them (stop reason 140); stop restores by CAS
 * and leaves a foreign value in place. */
#define SS_TARGET 0x7c8bc0u
#define SS_BODY 0x98u
#define SS_BODY_SHA "2cba1e39c86ca249094efd80bbad0003454208e3f71ad9d95fbbe36061fd1ff3"
#define SS_SLOT 0x23b8u
#define SS_VTABLES 7u
#define SS_STOP_REASON 140u /* 132 is the weapon fire module's */
#define SS_DOMAIN 0x58344c5353444e44ULL /* "X4LSSDND" */
enum {SS_OFF,SS_SPLIT,SS_OBSERVE};
static const unsigned ss_vtables[SS_VTABLES]={0x2b84a98u,0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u,0x2b789b0u};
typedef uintptr_t (*ss_fn)(void *,void *,void *,uintptr_t);
static int ss_mode,ss_installed,ss_restored;
static unsigned char ss_swapped[SS_VTABLES];
static volatile LONG ss_active;
static volatile LONG64 ss_calls,ss_drew,ss_draws,ss_unknown,ss_foreign,ss_counter,ss_failures;
static uint64_t ss_tick=UINT64_MAX,ss_frame_calls,ss_frame_drew,ss_frame_draws;
static ss_fn ss_original_override; /* tests only; NULL runs 0x7c8bc0 */
static const char *ss_stage="idle";
static int ss_pins(void) {
    if(!hash_ok(image+SS_TARGET,SS_BODY,SS_BODY_SHA)) return 0;
    for(unsigned i=0;i<SS_VTABLES;i++) {
        void *slot=NULL;if(!read_local(image+ss_vtables[i]+SS_SLOT,&slot,8)) return 0;
        if(slot!=(void*)(image+SS_TARGET) && !(ss_installed && ss_swapped[i])) return 0;
    }
    return 1;
}
/* One sound start: the original with a private word ('split') or the caller's ('observe'). */
static uintptr_t ss_run(ss_fn original,uint64_t *rng,void *self,void *list,void *data,uintptr_t flag) {
    InterlockedIncrement64(&ss_calls);
    if(!rng || !InterlockedCompareExchange(&ss_active,0,0)) return original(self,list,data,flag);
    int main=GetCurrentThreadId()==ui_thread,split=ss_mode==SS_SPLIT;
    if(!main) InterlockedIncrement64(&ss_foreign);
    uint64_t saved=*rng,seed=saved;uintptr_t result=0;
    if(split) {seed=lockstep_seed(SS_DOMAIN,(uint64_t)InterlockedIncrement64(&ss_counter),0,0,0,0);*rng=seed;}
    __try {result=original(self,list,data,flag);}
    __finally {
        uint64_t exit=*rng;if(split) *rng=saved;
        uint32_t draws=actor_all_steps(seed,exit);
        if(draws==UINT32_MAX) InterlockedIncrement64(&ss_unknown);
        else if(draws) {InterlockedIncrement64(&ss_drew);InterlockedAdd64(&ss_draws,draws);}
        if(main) {
            if(ss_tick!=stats.issued) {ss_tick=stats.issued;ss_frame_calls=ss_frame_drew=ss_frame_draws=0;}
            ++ss_frame_calls;if(draws && draws!=UINT32_MAX) {++ss_frame_drew;ss_frame_draws+=draws;}
        }
        if(AbnormalTermination()) InterlockedIncrement64(&ss_failures);
    }
    return result;
}
static __attribute__((noinline)) uintptr_t ss_wrapper(void *self,void *list,void *data,uintptr_t flag) {
    return ss_run(ss_original_override?ss_original_override:(ss_fn)(image+SS_TARGET),actor_all_tls_rng(),self,list,data,flag);
}
static int ss_owned(void) {
    if(!ss_installed) return 1;
    for(unsigned i=0;i<SS_VTABLES;i++) if(ss_swapped[i] && *(void*volatile*)(image+ss_vtables[i]+SS_SLOT)!=(void*)ss_wrapper) return 0;
    return 1;
}
/* Give every slot we hold back; returns 1 when none is left. */
static int ss_unswap(void) {
    int all=1;
    for(unsigned i=0;i<SS_VTABLES;i++) if(ss_swapped[i]) {
        if(script_rng_slot_swap(ss_vtables[i]+SS_SLOT,(void*)ss_wrapper,image+SS_TARGET)) ss_swapped[i]=0;else all=0;
    }
    return all;
}
/* Before the first admitted frame, on the MoveWorker. */
static int ss_arm(int mode) {
    if(ss_installed) return stats.active && mode==ss_mode;
    if((mode!=SS_SPLIT && mode!=SS_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!ss_pins()) {ss_stage="pins";return 0;}
    ss_calls=ss_drew=ss_draws=ss_unknown=ss_foreign=ss_counter=ss_failures=0;ss_tick=UINT64_MAX;ss_frame_calls=ss_frame_drew=ss_frame_draws=0;
    ss_mode=mode;InterlockedExchange(&ss_active,1);ss_installed=1;
    for(unsigned i=0;i<SS_VTABLES;i++) {
        if(!script_rng_slot_swap(ss_vtables[i]+SS_SLOT,image+SS_TARGET,(void*)ss_wrapper)) {
            InterlockedExchange(&ss_active,0);ss_unswap();ss_installed=0;ss_stage="slot";return 0;
        }
        ss_swapped[i]=1;
    }
    ss_restored=0;ss_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 140. */
static int ss_check(void) {
    if(InterlockedCompareExchange64(&ss_failures,0,0)) {ss_stage="exception";return 0;}
    const char *why=!ss_owned()?"slot_changed":!ss_pins()?"block_changed":NULL;
    if(why) {ss_stage=why;InterlockedIncrement64(&ss_failures);return 0;}
    return 1;
}
static void ss_restore(void) {
    InterlockedExchange(&ss_active,0);
    if(!ss_installed) return;
    if(!ss_unswap()) return; /* a foreign value stays */
    ss_installed=0;ss_restored=1;
}
static int ss_status(char *out,size_t capacity) {
    int fresh=ss_tick==stats.issued;
    int n=snprintf(out,capacity,"mode=%s installed=%d frame=%llu calls=%llu drew=%llu draws=%llu total_calls=%lld total_drew=%lld "
        "total_draws=%lld unknown=%lld foreign=%lld restored=%d stage=%s failures=%lld",
        ss_mode==SS_SPLIT?"split":ss_mode==SS_OBSERVE?"observe":"off",ss_installed,(unsigned long long)(ss_tick==UINT64_MAX?0:ss_tick),
        (unsigned long long)(fresh?ss_frame_calls:0),(unsigned long long)(fresh?ss_frame_drew:0),(unsigned long long)(fresh?ss_frame_draws:0),
        (long long)ss_calls,(long long)ss_drew,(long long)ss_draws,(long long)ss_unknown,(long long)ss_foreign,ss_restored,ss_stage,(long long)ss_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
