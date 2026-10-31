/* KillEvent is deferred beyond the bullet update. Its native listener creates
 * the wreck controller and draws random linear/angular velocity for it. */
#define DEATH_ENTRY 0x2b7fef8u
#define DEATH_XL_ENTRY 0x2b99ac8u
#define DEATH_LISTENER 0x7cd610u
#define DEATH_EVENTS 64u
typedef void (*death_fn)(void *,void *,double,unsigned,void *);
typedef struct {uint64_t tick,call,time,before,after;unsigned actor,reason;} death_event;
static death_event death_events[DEATH_EVENTS];
static uint64_t death_actor_calls[ACTOR_CAPACITY],death_calls,death_failures;
static unsigned death_event_count;
static uint64_t death_dropped;
static int death_installed,death_restored,death_seeded;
static unsigned death_owned_slots;
static void death_execute(death_fn original,death_event *e,uint64_t *rng,
                          void *self,void *source,double time,unsigned reason,void *killer) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=death_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x44454154485f3031ULL,e->tick,e->actor,0,e->call,0);
        e->before=rng?*rng:0;original(self,source,time,reason,killer);e->after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&death_failures);
        SetLastError(error);
    }
}
static void death_listener(void *self,void *source,double time,unsigned reason,void *killer) {
    registry_observe_object(self,REGISTRY_DEATH);
    death_fn original=(death_fn)(image+DEATH_LISTENER);unsigned actor=0;
    if(stats.active && boot_phase==3 && source==self && GetCurrentThreadId()==ui_thread) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
            uint64_t id;if(self==actor_components[i] && read_local((unsigned char*)self+8,&id,8) && id==actor_ids[i]) {actor=i+1;break;}
        }
    }
    if(!actor) {original(self,source,time,reason,killer);return;}
    /* The key needs the event's tick, actor and call; the table is rows, drained only with native_gate_death_diag. */
    static death_event scratch;death_event *e=&scratch;
    if(!gate_play_mode) {
        if(death_event_count==DEATH_EVENTS) ++death_dropped;
        else e=&death_events[death_event_count++];
    }
    memset(e,0,sizeof(*e));
    e->actor=actor;e->tick=stats.issued;e->call=++death_actor_calls[actor-1];e->reason=reason;memcpy(&e->time,&time,8);++death_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng || !isfinite(time)) ++death_failures;
    death_execute(original,e,rng,self,source,time,reason,killer);
}
static int death_swap_at(unsigned entry,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+entry,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+entry),value,expected);
    VirtualProtect(image+entry,8,old,&ignored);return actual==expected;
}
static int death_swap(void *expected,void *value) {return death_swap_at(DEATH_ENTRY,expected,value);}
static void death_restore(void) {
    if(!death_installed) return;
    if((death_owned_slots&1) && death_swap((void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~1u;
    if((death_owned_slots&2) && death_swap_at(DEATH_XL_ENTRY,(void*)death_listener,image+DEATH_LISTENER)) death_owned_slots&=~2u;
    death_restored=death_owned_slots==0;death_installed=!death_restored;
}
static int death_install_slots(void) {
    if(death_owned_slots || !death_swap(image+DEATH_LISTENER,(void*)death_listener)) return 0;
    death_owned_slots=1;death_installed=1;death_restored=0;
    if(!death_swap_at(DEATH_XL_ENTRY,image+DEATH_LISTENER,(void*)death_listener)) {death_restore();return 0;}
    death_owned_slots|=2;return 1;
}
static int death_install(int seed) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_combat.dll") ||
       !hash_ok(image+DEATH_LISTENER,180,"acf5deebe3cd68de95084938ccf71d601f4c5f1d1f18b2ecf4826ee85d43a99e") ||
       !death_install_slots()) return 0;
    death_seeded=seed;return 1;
}
