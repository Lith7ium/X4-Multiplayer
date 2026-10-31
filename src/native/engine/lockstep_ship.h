/* Scope the RNG around each fixture ship's original native Move, including the
 * original recursive child walk. This neither replaces weapons nor replicates
 * movement/damage. It does not cover event delivery outside this call. */
#define SHIP_ENTRY 0x2b813c8u
#define SHIP_XL_ENTRY 0x2b9af98u
#define SHIP_MOVE 0x7cc810u
typedef void (*ship_fn)(void *);
typedef struct {uint64_t tick,calls,rng_before,rng_after;} ship_sample;
static ship_sample ship_samples[ACTOR_CAPACITY];
static int ship_installed,ship_restored,ship_seeded;
static unsigned ship_owned_slots;
static uint64_t ship_calls,ship_failures;
static void ship_execute(ship_fn original,ship_sample *s,unsigned actor,uint64_t *rng,void *self) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=ship_seeded && rng;
    ++actor_all_ship_depth; /* nested ships continue this stream (native_gate_actor_rng_all) */
    __try {
        if(restore_rng) *rng=actor_seed(0x534849505f4d4f56ULL,stats.issued,actor,0,s->calls,0);
        s->rng_before=rng?*rng:0;original(self);s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        --actor_all_ship_depth;
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&ship_failures);
        SetLastError(error);
    }
}
static void ship_move(void *self) {
    registry_observe_object(self,REGISTRY_SHIP);
    ship_fn original=(ship_fn)(image+SHIP_MOVE);ship_sample *s=NULL;unsigned actor=0;
    const int gated=stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread;
    if(gated) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        if(actor_components[i]==self && *(uint64_t*)((unsigned char*)self+8)==actor_ids[i]) {s=&ship_samples[i];actor=i+1;break;}
    }
    if(!s) {
        world_trace_event *event=world_trace_begin(WT_SHIP,self,NULL,0,0,0,0);
        if(gated && actor_all_on()) actor_all_ship_execute(original,self,actor_all_tls_rng());else original(self);
        world_trace_end(event);return;
    }
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++ship_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng) ++ship_failures;
    ship_execute(original,s,actor,rng,self);
}
static int ship_swap_at(unsigned entry,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+entry,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+entry),value,expected);
    VirtualProtect(image+entry,8,old,&ignored);return actual==expected;
}
static int ship_swap(void *expected,void *value) {return ship_swap_at(SHIP_ENTRY,expected,value);}
static void ship_restore(void) {
    if(!ship_installed) return;
    if((ship_owned_slots&1) && ship_swap((void*)ship_move,image+SHIP_MOVE)) ship_owned_slots&=~1u;
    if((ship_owned_slots&2) && ship_swap_at(SHIP_XL_ENTRY,(void*)ship_move,image+SHIP_MOVE)) ship_owned_slots&=~2u;
    ship_restored=ship_owned_slots==0;ship_installed=!ship_restored;
}
static int ship_install_slots(void) {
    if(ship_owned_slots || !ship_swap(image+SHIP_MOVE,(void*)ship_move)) return 0;
    ship_owned_slots=1;ship_installed=1;ship_restored=0;
    if(!ship_swap_at(SHIP_XL_ENTRY,image+SHIP_MOVE,(void*)ship_move)) {ship_restore();return 0;}
    ship_owned_slots|=2;return 1;
}
static int ship_install(int seed) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+SHIP_MOVE,1503,"a0346a6203409b640354b5bef3df9521b06d53da40fd23a1c701e562a863ec8b") ||
       !ship_install_slots()) return 0;
    ship_seeded=seed;return 1;
}
