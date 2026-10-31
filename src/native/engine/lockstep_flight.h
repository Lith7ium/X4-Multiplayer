/* Disposable fixture diagnostic. The twelve MD actors register their steering
 * models at frame end, or through validated snapshot preparation before the
 * first pathing release. Worker callbacks observe only
 * these models. Optional RNG isolation changes only the owning thread's stream
 * for this one call, restoring it before returning to the engine. */
#define FLIGHT_ENTRY 0x2c4ea38u
#define FLIGHT_UPDATE 0xec7200u
#define FLIGHT_OBSTACLES 64u
#define FLIGHT_CTOR 0xec40c0u
typedef void (*flight_fn)(void *,void *,float,double,unsigned char,unsigned char);
typedef struct {
    void *model, *agent;
    uint64_t tick, calls, cheap, applied_cheap, rng_before, rng_after;
    uint32_t dt, before[24], after[24];
    uint32_t model_before[0x3a0/4], agent_limits[0x60/4];
    /* Complete raw diagnostic blocks, including forces and control targets that
     * the original first-six-vectors trace omitted. Unknown/padding fields are
     * not portable hashes. Both blocks have the pinned native 0x1a0 stride. */
    uint32_t slot_before[0x1a0/4], params_before[0x1a0/4];
    uint64_t avoidance_time[2];
    unsigned avoidance_count[2];
    uint32_t avoidance[2][FLIGHT_OBSTACLES][0x80/4];
    uint64_t behaviour_rva;
    void *initialized_model;
    uint64_t init_generation;
} flight_sample;
static flight_sample flight_samples[ACTOR_CAPACITY];
static int flight_installed, flight_restored, flight_seeded, flight_full, flight_avoidance_refresh, flight_initial;
static uint64_t flight_calls, flight_failures, flight_refreshes, flight_inits, flight_avoidance_truncated;
static uint64_t flight_ctor_original_word,flight_ctor_patched_word,flight_ctor_calls,flight_registration_inits;
static int flight_ctor_installed,flight_ctor_restored;
static int read_local(const void *,void *,size_t);

/* What the module could not do, named: reason 55 had no row. reg-big2 and reg-big3 stopped on client 2
 * only, at ticks 642 and 1194, after a fixture ship had died.
 *
 * flight_samples holds the Agent and model of each fixture by address, and nothing clears them when the
 * ship goes. A new ship's Agent can get a dead fixture's address, on one client only: the heap's state is
 * per process. Its model constructor then matched the old entry and failed the identity check that
 * follows (the Agent constructor 0xe85360 sets +0xef0 before any model exists, so it reads the new ship).
 * That is not a fault of the game: the entry is stale. It is retired and the ship goes on as what it is,
 * a ship that is no fixture, exactly as on the client whose heap did not reuse the address. The same check
 * guards the update, because a keyed fixture path on one client would be a real desync. agent_identity
 * (lockstep_agent.h) has always done this for the Agent's own update.
 *
 * The avoidance copies are diagnostics: a failed read loses a row. Still reason 55, as they decide a
 * result: a constructor that does not leave what the keyed initial values need on a live fixture, and a
 * native exception through a keyed scope. */
enum {FLIGHT_FAULT_STALE_AGENT,FLIGHT_FAULT_STALE_MODEL,FLIGHT_FAULT_AVOIDANCE_BEFORE,FLIGHT_FAULT_AVOIDANCE_AFTER,FLIGHT_FAULT_CONSTRUCT,FLIGHT_FAULT_UNWIND,FLIGHT_FAULT_SITES};
static const char *const flight_fault_names[FLIGHT_FAULT_SITES]={"stale_agent","stale_model","avoidance_before","avoidance_after","construct","unwind"};
#define FLIGHT_FAULT_ROWS_PER_SITE 4u
#define FLIGHT_FAULT_RING (FLIGHT_FAULT_SITES*FLIGHT_FAULT_ROWS_PER_SITE)
typedef struct {volatile LONG64 ready;uint64_t tick,other;unsigned site,actor,fatal;} flight_fault_row_t;
static flight_fault_row_t flight_fault_ring[FLIGHT_FAULT_RING];
static volatile LONG64 flight_fault_counts[FLIGHT_FAULT_SITES],flight_fault_rows;
static void flight_fault(unsigned site,unsigned actor,uint64_t other,int fatal) {
    LONG64 seen=InterlockedIncrement64(&flight_fault_counts[site]);
    if(fatal) InterlockedIncrement64((volatile LONG64*)&flight_failures);
    if(seen>(LONG64)FLIGHT_FAULT_ROWS_PER_SITE) return;
    LONG64 index=InterlockedIncrement64(&flight_fault_rows)-1;
    if(index>=(LONG64)FLIGHT_FAULT_RING) return;
    flight_fault_row_t *r=&flight_fault_ring[index];
    r->tick=stats.issued;r->other=other;r->site=site;r->actor=actor;r->fatal=(unsigned)fatal;
    InterlockedExchange64(&r->ready,index+1);
}
static __attribute__((unused)) void flight_fault_reset(void) { /* tests */
    memset((void*)flight_fault_counts,0,sizeof(flight_fault_counts));memset(flight_fault_ring,0,sizeof(flight_fault_ring));flight_fault_rows=0;
}
static int flight_fault_row(uint64_t index,char *out,size_t cap) {
    if(index>=FLIGHT_FAULT_RING) return 0;
    const flight_fault_row_t *r=&flight_fault_ring[index];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    int n=snprintf(out,cap,"index=%llu tick=%llu site=%s actor=%u other=%016llx fatal=%u",(unsigned long long)index,(unsigned long long)r->tick,
        flight_fault_names[r->site<FLIGHT_FAULT_SITES?r->site:0],r->actor,(unsigned long long)r->other,r->fatal);
    return n>0 && (size_t)n<cap?n:0;
}
static int flight_fault_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"failures=%llu truncated=%llu rows=%lld sites=",(unsigned long long)flight_failures,(unsigned long long)flight_avoidance_truncated,(long long)flight_fault_rows);
    if(n<0 || (size_t)n>=cap) return 0;
    int any=0;
    for(unsigned i=0;i<FLIGHT_FAULT_SITES;i++) {
        LONG64 count=InterlockedCompareExchange64(&flight_fault_counts[i],0,0);if(!count) continue;
        int m=snprintf(out+n,cap-(size_t)n,"%s%s:%lld",any?",":"",flight_fault_names[i],(long long)count);
        if(m<0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;any=1;
    }
    if(!any) {if(cap-(size_t)n<2) return 0;out[n++]='-';out[n]=0;}
    return n;
}
/* Still this actor's Agent? other: the runtime ID of the ship it belongs to now. */
static int flight_identity(unsigned i,const unsigned char *agent,uint64_t *other) {
    unsigned char *component=NULL;uint64_t id=0;*other=0;
    if(!read_local(agent+0xef0,&component,8) || !component || !read_local(component+8,&id,8)) return 0;
    *other=id;
    return component==actor_components[i] && id==actor_ids[i];
}
static void flight_retire(unsigned i,unsigned site,uint64_t other) {
    flight_samples[i].agent=NULL;flight_samples[i].model=NULL;
    flight_fault(site,i+1,other,0);
}

typedef float (*flight_random_fn)(uint64_t *,float);
static void flight_init_model(flight_random_fn random,flight_sample *s,unsigned actor,unsigned char *model) {
    if(s->initialized_model==model) return;
    uint64_t rng=actor_seed(0x5354454552494e49ULL,0,actor,s->init_generation+1,0,0);
    /* Exactly the two native constructor draws, using its float32 limits.
     * Reinitialize only constructor randomness, not the model or its orders. */
    uint32_t bits[2]={0x40c90fdbu,0x40490fdbu};float limits[2];memcpy(limits,bits,8);
    float azimuth=random(&rng,limits[0]),polar=random(&rng,limits[1]);
    memcpy(model+0x33c,&azimuth,4);memcpy(model+0x340,&polar,4);
    s->initialized_model=model;++s->init_generation;
    InterlockedIncrement64((volatile LONG64*)&flight_inits);
}

static void flight_init_generation(flight_random_fn random,flight_sample *s,unsigned actor,unsigned char *model) {
    /* Placement construction is a new lifetime even at the same address. */
    s->initialized_model=NULL;s->model=model;
    flight_init_model(random,s,actor,model);
}
typedef void *(*flight_ctor_fn)(void *,void *);
static void flight_construct_observe(flight_random_fn random,void *result,void *self,void *agent) {
    if(!stats.active || boot_phase!=3) return;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(flight_samples[i].agent==agent) {
        void *owner,*vtable;uint64_t other;
        if(!flight_identity(i,agent,&other)) {flight_retire(i,FLIGHT_FAULT_STALE_AGENT,other);continue;}
        if(result!=self || !read_local((unsigned char*)self+0x10,&owner,8) || owner!=agent ||
           !read_local(self,&vtable,8) || vtable!=image+FLIGHT_ENTRY-0x70) {
            flight_fault(FLIGHT_FAULT_CONSTRUCT,i+1,other,1);return;
        }
        flight_init_generation(random,&flight_samples[i],i+1,self);
        InterlockedIncrement64((volatile LONG64*)&flight_ctor_calls);break;
    }
}
static void *flight_construct(void *self,void *agent) {
    void *result=((flight_ctor_fn)(thunk+640))(self,agent);
    flight_construct_observe((flight_random_fn)(image+0x14ddc90),result,self,agent);
    return result;
}
static int flight_ctor_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+FLIGHT_CTOR,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+FLIGHT_CTOR),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+FLIGHT_CTOR,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+FLIGHT_CTOR,8);return actual==expected;
}
static int flight_ctor_install(void) {
    /* Full function already pinned by flight_install before any mutation. */
    intptr_t distance=(intptr_t)(thunk+672)-(intptr_t)(image+FLIGHT_CTOR+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(640)) return 0;
    memcpy(&flight_ctor_original_word,image+FLIGHT_CTOR,8);flight_ctor_patched_word=flight_ctor_original_word;
    unsigned char *patch=(unsigned char*)&flight_ctor_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!flight_ctor_swap(flight_ctor_original_word,flight_ctor_patched_word)) return 0;
    flight_ctor_installed=1;return 1;
}

static int flight_read_avoidance(flight_sample *s,const unsigned char *model,unsigned phase) {
    uintptr_t begin,end,capacity;
    if(phase>1 || !read_local(model+0x88,&s->avoidance_time[phase],8) ||
       !read_local(model+0x90,&begin,8) || !read_local(model+0x98,&end,8) ||
       !read_local(model+0xa0,&capacity,8) || end<begin || capacity<end ||
       (end-begin)%0x80 || (end-begin)/0x80>65536) return 0;
    /* Diagnostic copy only: a crowded battle may list more obstacles than the
     * buffer holds; keep the first FLIGHT_OBSTACLES and count the truncation. */
    uintptr_t count=(end-begin)/0x80;
    if(count>FLIGHT_OBSTACLES) {count=FLIGHT_OBSTACLES;InterlockedIncrement64((volatile LONG64*)&flight_avoidance_truncated);}
    s->avoidance_count[phase]=(unsigned)count;
    return !count || read_local((void*)begin,s->avoidance[phase],count*0x80);
}

static uint64_t flight_seed(uint64_t tick,unsigned actor,uint64_t ordinal) {
    return actor_seed(0x464c49474854524eULL,tick,actor,0,ordinal,0);
}
static void flight_execute(flight_fn original,flight_sample *sample,unsigned actor,uint64_t *rng,
                           void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    uint64_t saved=rng?*rng:0;
    /* The write obligation belongs to this invocation, even if STOP changes
     * installation/options before a native exception reaches this finally. */
    const int restore_rng=flight_seeded && rng,keyed=flight_seeded;
    __try {
        if(restore_rng) *rng=flight_seed(stats.issued,actor,sample->calls);
        sample->rng_before=rng?*rng:0;
        /* Rebuild native contact geometry on each admitted full steering update.
         * In particular, wrecks have no live pathing cache: otherwise their stale
         * geometry depends on asynchronous Gravidar publication. Contact membership
         * remains native and is not claimed to be deterministic by this experiment. */
        if(flight_avoidance_refresh) {
            *(double*)((unsigned char*)self+0x88)=-1.0;
            InterlockedIncrement64((volatile LONG64*)&flight_refreshes);
        }
        solver_scope observed;
        solver_scope_begin(&observed,2,self,sample->agent,slot,actor);
        original(self,slot,dt,time,flight_full?0:cheap,player);
        solver_scope_end(&observed);
        sample->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) flight_fault(FLIGHT_FAULT_UNWIND,actor,0,keyed);
        SetLastError(error);
    }
}
/* The fixture this model is, if it still is one. */
static flight_sample *flight_sample_for(void *self,unsigned *actor) {
    void *owner=*(void**)((unsigned char*)self+0x10);
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(flight_samples[i].model==self && flight_samples[i].agent==owner) {
        uint64_t other;
        if(!flight_identity(i,owner,&other)) {flight_retire(i,FLIGHT_FAULT_STALE_MODEL,other);return NULL;}
        *actor=i+1;return &flight_samples[i];
    }
    return NULL;
}
static void flight_update(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    void *registry_agent;
    if(registry_is_observing() && read_local((unsigned char*)self+0x10,&registry_agent,8)) registry_observe_owner(registry_agent,0xef0,REGISTRY_FLIGHT);
    flight_fn original=(flight_fn)(image+FLIGHT_UPDATE);
    flight_sample *sample=NULL;unsigned actor=0;
    if(stats.active && boot_phase==3) sample=flight_sample_for(self,&actor);
    if(!sample) {
        /* Kind0 is a nonpublishing excluded marker, so nested unregistered
         * models cannot inherit a saved owner's solver/request bracket. */
        solver_scope excluded;solver_scope_begin(&excluded,0,self,NULL,slot,0);
        world_trace_event *event=world_trace_begin(WT_STEERING,self,slot,dt,time,cheap,player);
        if(stats.active && boot_phase==3 && actor_all_on()) actor_all_flight_execute(original,self,slot,dt,time,cheap,player,actor_all_tls_rng());
        else original(self,slot,dt,time,cheap,player);
        world_trace_end(event);solver_scope_end(&excluded);return;
    }
    if(sample->tick!=stats.issued) {sample->tick=stats.issued;sample->calls=sample->cheap=sample->applied_cheap=0;}
    ++sample->calls;sample->cheap+=cheap!=0;
    sample->applied_cheap+=!flight_full && cheap!=0;
    memcpy(&sample->dt,&dt,4);memcpy(sample->before,slot,sizeof(sample->before));
    memcpy(sample->model_before,self,sizeof(sample->model_before));
    memcpy(sample->agent_limits,(unsigned char*)sample->agent+0x340,sizeof(sample->agent_limits));
    memcpy(sample->slot_before,slot,sizeof(sample->slot_before));
    memcpy(sample->params_before,(unsigned char*)sample->agent+0x340,sizeof(sample->params_before));
    void *behaviour=*(void**)((unsigned char*)self+0x70);
    if(!behaviour) behaviour=*(void**)((unsigned char*)self+0x78);
    sample->behaviour_rva=behaviour?(uint64_t)((unsigned char*)*(void**)behaviour-image):0;
    /* Remove known pointer fields. Remaining raw fields are local diagnostics,
     * not a portable state digest or a declaration that all are scalar values. */
    memset(sample->model_before,0,0x20);
    memset((unsigned char*)sample->model_before+0x70,0,0x10);
    if(!flight_read_avoidance(sample,self,0)) flight_fault(FLIGHT_FAULT_AVOIDANCE_BEFORE,actor,0,0);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    flight_execute(original,sample,actor,rng,self,slot,dt,time,cheap,player);
    if(!flight_read_avoidance(sample,self,1)) flight_fault(FLIGHT_FAULT_AVOIDANCE_AFTER,actor,0,0);
    memcpy(sample->after,slot,sizeof(sample->after));
    InterlockedIncrement64((volatile LONG64*)&flight_calls);
}
static int flight_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+FLIGHT_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+FLIGHT_ENTRY),value,expected);
    VirtualProtect(image+FLIGHT_ENTRY,8,old,&ignored);return actual==expected;
}
static int flight_install(int seed,int full,int refresh,int initial) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       (refresh && !full) ||
       !hash_ok(image+FLIGHT_UPDATE,32,"20c822202b85f4ecfcc9892f075241b6fddced6cc71ea149c42c224145ff066b")) return 0;
    if(initial && (!seed ||
       !hash_ok(image+0xec40c0,1532,"799fa233b280c0cb8f818b124e06ba31e7e63204cc386726721a2f5be9397dcf") ||
       !hash_ok(image+0x14ddc90,32,"b979e629881a2dacae664e2e1849bc808bf1f2e12be10ab9deeb875f076fc027"))) return 0;
    if(!flight_swap(image+FLIGHT_UPDATE,(void*)flight_update)) return 0;
    flight_seeded=seed;flight_full=full;flight_avoidance_refresh=refresh;flight_initial=initial;flight_installed=1;
    if(initial && !flight_ctor_install()) return 0; /* finish() restores the owned vtable on failure. */
    return 1;
}
static void flight_restore(void) {
    if(flight_ctor_installed) {
        flight_ctor_restored=flight_ctor_swap(flight_ctor_patched_word,flight_ctor_original_word);
        if(flight_ctor_restored) flight_ctor_installed=0;
    }
    if(!flight_installed) return;
    flight_restored=flight_swap((void*)flight_update,image+FLIGHT_UPDATE);
    if(flight_restored) flight_installed=0;
}
static int flight_register(unsigned actor,unsigned char *slots) {
    if(!flight_installed || !actor_is_ship(actor) || !slots) return 0;
    unsigned char *agent=slots-0xad0,*model;void *vtable,*owner;
    if(!read_local(agent+0xf10,&model,8) || !read_local(model,&vtable,8) ||
       !read_local(model+0x10,&owner,8) || owner!=agent) return 0;
    flight_samples[actor-1].agent=agent;
    if(vtable!=image+FLIGHT_ENTRY-0x70) {flight_samples[actor-1].model=NULL;return 0;}
    flight_samples[actor-1].model=model;
    if(flight_initial) {
        uint64_t before=flight_samples[actor-1].init_generation;
        flight_init_model((flight_random_fn)(image+0x14ddc90),&flight_samples[actor-1],actor,model);
        if(flight_samples[actor-1].init_generation!=before) ++flight_registration_inits;
    }
    return 1;
}
