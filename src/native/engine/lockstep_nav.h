/* Native navigation upkeep randomizes parameter refresh deadlines by distance.
 * Scope that stream separately from the enclosing ship/child traversal. */
#define NAV_ENTRY 0x2c51260u
#define NAV_MOVE 0xea77f0u
typedef void (*nav_fn)(void *,double);
/* Refresh cadence counters (lockstep_nav_cadence.h); inert unless armed. */
typedef struct {double time,params,probe;unsigned char params_on,probe_on;unsigned valid;} nav_cadence_sample;
static void nav_cadence_enter(const void *controller,double time,nav_cadence_sample *c);
static void nav_cadence_leave(const void *controller,const nav_cadence_sample *c,unsigned other,unsigned actor);
typedef struct {uint64_t tick,calls,time,deadline_before,deadline_after,rng_before,rng_after;unsigned initialized;} nav_sample;
static nav_sample nav_samples[ACTOR_CAPACITY];
static int nav_installed,nav_restored,nav_seeded,nav_initial_refresh;
static unsigned nav_initialized[ACTOR_CAPACITY],nav_inits;
static uint64_t nav_calls,nav_failures;
static void nav_execute(nav_fn original,nav_sample *s,unsigned actor,uint64_t *rng,void *self,double time) {
    uint64_t saved=rng?*rng:0;memcpy(&s->deadline_before,(unsigned char*)self+0xd0,8);
    s->initialized=0;
    if(nav_initial_refresh && !nav_initialized[actor-1]) {
        *(double*)((unsigned char*)self+0xd0)=time-gate_tick_dt();
        nav_initialized[actor-1]=1;++nav_inits;s->initialized=1;
    }
    const int restore_rng=nav_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x4e41565f55504454ULL,stats.issued,actor,0,s->calls,0);
        s->rng_before=rng?*rng:0;original(self,time);s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&nav_failures);
        SetLastError(error);
    }
    memcpy(&s->deadline_after,(unsigned char*)self+0xd0,8);
}
static void nav_move(void *self,double time) {
    registry_observe_owner(self,0x28,REGISTRY_NAV);
    nav_fn original=(nav_fn)(image+NAV_MOVE);nav_sample *s=NULL;unsigned actor=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        unsigned char *object=actor_components[i];uint64_t id;void *controller;
        if(object && read_local(object+8,&id,8) && id==actor_ids[i] && read_local(object+0x200,&controller,8) && controller==self) {actor=i+1;s=&nav_samples[i];break;}
    }
    nav_cadence_sample cadence;cadence.valid=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) nav_cadence_enter(self,time,&cadence);
    if(!s) {
        world_trace_event *event=world_trace_begin(WT_NAV,self,NULL,0,time,0,0);
        if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && actor_all_on()) actor_all_nav_execute(original,self,time,actor_all_tls_rng());
        else original(self,time);
        world_trace_end(event);nav_cadence_leave(self,&cadence,1,0);return;
    }
    if(!isfinite(time)) {++nav_failures;original(self,time);nav_cadence_leave(self,&cadence,0,actor);return;}
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++nav_calls;memcpy(&s->time,&time,8);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng) ++nav_failures;
    nav_execute(original,s,actor,rng,self,time);
    nav_cadence_leave(self,&cadence,0,actor);
    snapshot_nav_observe(actor,s);
    registry_nav_observe(actor,s);
}
static int nav_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+NAV_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+NAV_ENTRY),value,expected);
    VirtualProtect(image+NAV_ENTRY,8,old,&ignored);return actual==expected;
}
static int nav_install(int seed,int initial_refresh) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+NAV_MOVE,32,"acc248e006b7b6889930478f2b42aa2d7a011fc1107095b78229142f08336964")) return 0;
    if(!nav_swap(image+NAV_MOVE,(void*)nav_move)) return 0;
    nav_seeded=seed;nav_initial_refresh=initial_refresh;nav_installed=1;return 1;
}
static void nav_restore(void) {
    if(!nav_installed) return;
    nav_restored=nav_swap((void*)nav_move,image+NAV_MOVE);
    if(nav_restored) nav_installed=0;
}
