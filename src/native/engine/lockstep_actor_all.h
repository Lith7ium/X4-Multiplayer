/* Per-ship RNG keys for every ship with a portable identity, DEFAULT OFF:
 * native_gate_actor_rng_all (needs native_gate_agent_rng and native_gate_ship_rng
 * seeding and the world identities; created ships also need
 * native_gate_portable_created).
 *
 * The per-actor modules key only the registered fixture actors; every other
 * ship draws from the calling thread's TLS word (slot0+0x318). On the
 * simulation thread that word is reseeded every frame, so non-fixture draws
 * are deterministic only while every earlier consumer of the frame drew
 * identically (one extra draw shifts all later ones). On the pathing
 * JobThreads it is never reseeded and the batch-to-thread assignment changes
 * every pass, so any draw there is not reproducible at all. big-BS4: the sim
 * word is equal on both clients to tick 60, events and scripts too, yet 12
 * spawned ships differ in motion at the tick-60 capture.
 *
 * Two umbrella scopes, each the outermost RNG scope of one ship's update:
 *  - ship   Ship Move (sim thread, ship module slot): the ship and its
 *           recursive child walk (weapons, navigation upkeep, attention and
 *           shape refresh of its parts) draw from
 *           lockstep_seed(ACTOR_ALL_SHIP_DOMAIN, tick, portable ID, 0, ordinal, 0),
 *           ordinal = count of that ship's Moves in this tick (sim thread only);
 *  - agent  the complete pathing Agent update (JobThreads, agent module call),
 *           steering and steering-model construction included:
 *           lockstep_seed(ACTOR_ALL_AGENT_DOMAIN, tick, portable ID, dt bits, 0, time bits);
 *           no shared counter on the job threads: (tick, ID, dt, time) is the
 *           call's identity, so a second update with equal arguments repeats
 *           the same stream (deterministic, merely correlated).
 * The portable ID is world_identity_key of the ship (agent+0xef0): its saved
 * ID or, with portable created keys, its created ID. A ship without one keeps
 * the natural word (counted unmapped), as in its creation frame.
 * Fallbacks with the same rule where the umbrella is not open on the thread
 * (counted 'outside'; expected to be zero): steering update (model+0x10 agent),
 * navigation upkeep (controller+0x28 object) and weapon Move (the weapon's own
 * portable ID), with their own domains.
 *
 * Fixtures are untouched: a registered fixture actor takes its module path as
 * before, and any ship nested inside an open fixture or umbrella scope (a
 * docked ship moved by its carrier's walk) continues the enclosing stream
 * instead of opening its own, exactly as it did before. Off, nothing here
 * runs; on, fixture keys and fixture streams are unchanged.
 *
 * Cost per keyed call: two guarded 8-byte copies (no system call), one saved
 * lookup (plus one created lookup under a shared SRW lock for created ships),
 * one lockstep_seed, the step count of the scope's draws and a few plain adds
 * to the thread's own tally shard; 1 call in 16 per thread is timed (TSC).
 *
 * Census: at every gate boundary (MoveWorker and pathing joined) the frame's
 * per-path tallies are folded: keyed/nested/unmapped/created scopes, scopes
 * with draws, draws, and order-independent keyset/drawset sums, which two
 * clients compare per tick. */
#define ACTOR_ALL_SHARDS 64u
#define ACTOR_ALL_ORDINALS 16384u
#define ACTOR_ALL_PROBES 64u
#define ACTOR_ALL_STEP_CAP 1024u
#define ACTOR_ALL_TIMED_MASK 15u
enum {ACTOR_ALL_SHIP,ACTOR_ALL_AGENT,ACTOR_ALL_FLIGHT,ACTOR_ALL_NAV,ACTOR_ALL_WEAPON,ACTOR_ALL_PATHS};
enum {AA_KEYED,AA_NESTED,AA_UNMAPPED,AA_CREATED,AA_DRAWN,AA_DRAWS,AA_UNKNOWN,AA_KEYSET,AA_DRAWSET,AA_TIMED,AA_TICKS,AA_FIELDS};
static const char *const actor_all_names[ACTOR_ALL_PATHS]={"ship","agent","flight","nav","weapon"};
static const uint64_t actor_all_domains[ACTOR_ALL_PATHS]={
    0x58344c4153484950ULL, /* "X4LASHIP" */
    0x58344c4141474e54ULL, /* "X4LAAGNT" */
    0x58344c41464c4954ULL, /* "X4LAFLIT" */
    0x58344c414e415655ULL, /* "X4LANAVU" */
    0x58344c4157454150ULL  /* "X4LAWEAP" */
};
/* Tallies: each thread owns one shard (plain adds, single writer) until the
 * shards run out; later threads share the last shard with atomic adds. The
 * boundary reads them while no scope runs (MoveWorker and pathing joined). */
typedef struct {uint64_t v[ACTOR_ALL_PATHS][AA_FIELDS];} __attribute__((aligned(64))) actor_all_shard;
static actor_all_shard actor_all_shards[ACTOR_ALL_SHARDS];
static volatile LONG actor_all_shard_next;
static _Thread_local int actor_all_shard_slot=-1;
static _Thread_local unsigned actor_all_timer;
static uint64_t actor_all_total[ACTOR_ALL_PATHS][AA_FIELDS],actor_all_frame_delta[ACTOR_ALL_PATHS][AA_FIELDS];
static uint64_t actor_all_frame_tick,actor_all_frames,actor_all_table_full;
static volatile LONG actor_all_armed;
static volatile LONG64 actor_all_failures;
/* Open fixture or umbrella scopes: ship scopes on the sim thread only, agent
 * scopes per thread (the pathing JobThreads run agents in parallel). */
static unsigned actor_all_ship_depth;
static _Thread_local unsigned actor_all_agent_depth;
typedef struct {uint64_t id;uint32_t path,count,epoch;} actor_all_slot;
static actor_all_slot actor_all_slots[ACTOR_ALL_ORDINALS];
static uint32_t actor_all_epoch;
static uint64_t actor_all_epoch_tick=UINT64_MAX;
static uint64_t actor_all_tsc0;
static LARGE_INTEGER actor_all_qpc0;

static int actor_all_on(void) {return *(volatile LONG*)&actor_all_armed!=0;}
/* Guarded copy (clang's __try covers faults only inside calls). */
static __attribute__((noinline)) void actor_all_copy(void *dest,const void *source,size_t bytes) {memcpy(dest,source,bytes);}
static void (*volatile actor_all_copy_fn)(void *,const void *,size_t)=actor_all_copy;
static int actor_all_read(const void *source,void *dest,size_t bytes) {
    if(!source) return 0;
    __try {actor_all_copy_fn(dest,source,bytes);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
    return 1;
}
static actor_all_shard *actor_all_shard_here(void) {
    if(actor_all_shard_slot<0) {
        LONG next=InterlockedIncrement(&actor_all_shard_next)-1;
        actor_all_shard_slot=next<(LONG)ACTOR_ALL_SHARDS-1?(int)next:(int)ACTOR_ALL_SHARDS-1;
    }
    return &actor_all_shards[actor_all_shard_slot];
}
static void actor_all_add(actor_all_shard *s,unsigned path,unsigned field,uint64_t value) {
    if(s==&actor_all_shards[ACTOR_ALL_SHARDS-1]) InterlockedExchangeAdd64((volatile LONG64*)&s->v[path][field],(LONG64)value);
    else s->v[path][field]+=value;
}
/* Portable ID of the object at object (+8 runtime ID); 0 when it has none. */
static uint64_t actor_all_identity(const unsigned char *object,unsigned *klass) {
    uint64_t runtime=0;*klass=WORLD_KEY_NONE;
    if(!object || !actor_all_read(object+8,&runtime,8) || !runtime) return 0;
    return world_identity_key(runtime,klass);
}
/* Generator steps of the engine PCG from seed to exit (UINT32_MAX: not reached). */
static uint32_t actor_all_steps(uint64_t from,uint64_t to) {
    for(uint32_t n=0;n<=ACTOR_ALL_STEP_CAP;n++) {
        if(from==to) return n;
        from=from*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;from=(from>>30)|(from<<34);
    }
    return UINT32_MAX;
}
/* Per-tick (path, ID) ordinal on the sim thread; 0 when the table is full. */
static uint64_t actor_all_ordinal(unsigned path,uint64_t id) {
    if(actor_all_epoch_tick!=stats.issued) {
        actor_all_epoch_tick=stats.issued;
        if(!++actor_all_epoch) {memset(actor_all_slots,0,sizeof(actor_all_slots));actor_all_epoch=1;}
    }
    unsigned first=(unsigned)lockstep_mix(id^((uint64_t)path<<59))&(ACTOR_ALL_ORDINALS-1u);
    for(unsigned n=0;n<ACTOR_ALL_PROBES;n++) {
        actor_all_slot *s=&actor_all_slots[(first+n)&(ACTOR_ALL_ORDINALS-1u)];
        if(s->epoch!=actor_all_epoch) {s->epoch=actor_all_epoch;s->id=id;s->path=path;s->count=1;return 1;}
        if(s->id==id && s->path==path) return ++s->count;
    }
    ++actor_all_table_full;return 0;
}
static uint64_t actor_all_key(unsigned path,uint64_t tick,uint64_t id,uint64_t stream,uint64_t ordinal,uint64_t detail) {
    return lockstep_seed(actor_all_domains[path],tick,id,stream,ordinal,detail);
}
/* One keyed scope: seed, original (by the caller, under __try/__finally),
 * then tally and restore. */
typedef struct {actor_all_shard *shard;uint64_t saved,seed,t0,t1,id;unsigned path,timed,created;} actor_all_scope;
/* Scope rows, DEFAULT OFF (native_gate_actor_rng_rows): one row per keyed scope
 * that drew (or whose draws could not be counted): tick, path, portable ID and
 * draws. long-L13: per-frame draw counts of the ship path part at frames 664
 * (client 1 one more) and 902 (client 2 one more), with one ship scope per
 * tick; the rows name that scope. Any thread (agent and flight scopes run on
 * the pathing workers): an interlocked ring, read after the join. */
#define ACTOR_ALL_ROW_RING 16384u
typedef struct {uint64_t tick,id;uint32_t path,draws;} actor_all_row_t;
static actor_all_row_t actor_all_ring[ACTOR_ALL_ROW_RING];
static volatile LONG64 actor_all_rows;
static volatile LONG actor_all_rows_armed;
static void actor_all_open(actor_all_scope *c,unsigned path,uint64_t *rng,uint64_t key,uint64_t id,unsigned klass,uint64_t t0,int timed) {
    c->shard=actor_all_shard_here();c->path=path;c->id=id;c->created=klass==WORLD_KEY_CREATED;c->timed=timed;c->t0=t0;
    c->saved=*rng;c->seed=key;*rng=key;
    if(timed) c->t1=__builtin_ia32_rdtsc();
}
static void actor_all_close(actor_all_scope *c,uint64_t *rng) {
    uint64_t t2=c->timed?__builtin_ia32_rdtsc():0,exit=*rng;*rng=c->saved;
    uint32_t draws=actor_all_steps(c->seed,exit);actor_all_shard *s=c->shard;unsigned p=c->path;
    actor_all_add(s,p,AA_KEYED,1);
    if(c->created) actor_all_add(s,p,AA_CREATED,1);
    if(draws==UINT32_MAX) actor_all_add(s,p,AA_UNKNOWN,1);
    else if(draws) {actor_all_add(s,p,AA_DRAWN,1);actor_all_add(s,p,AA_DRAWS,draws);}
    if(draws && InterlockedCompareExchange(&actor_all_rows_armed,0,0)) {
        LONG64 index=InterlockedIncrement64(&actor_all_rows)-1;
        actor_all_ring[(uint64_t)index%ACTOR_ALL_ROW_RING]=(actor_all_row_t){stats.issued,c->id,p,draws};
    }
    actor_all_add(s,p,AA_KEYSET,lockstep_mix(c->seed));
    actor_all_add(s,p,AA_DRAWSET,lockstep_mix(c->seed^((uint64_t)draws*0x9e3779b97f4a7c15ULL)));
    if(c->timed) {uint64_t t3=__builtin_ia32_rdtsc();actor_all_add(s,p,AA_TIMED,1);actor_all_add(s,p,AA_TICKS,(c->t1-c->t0)+(t3-t2));}
}
static int actor_all_timed(void) {return (++actor_all_timer&ACTOR_ALL_TIMED_MASK)==0;}
static void actor_all_count(unsigned path,unsigned field) {actor_all_add(actor_all_shard_here(),path,field,1);}

/* Optional observer of ship scopes (lockstep_draw_watch.h): open=1 once the
 * scope word is set, open=0 before the caller's word is restored (-1 when an
 * exception unwinds the scope); sim thread. */
static void (*actor_all_ship_watch)(int open,uint64_t id,uint64_t *rng);
/* Ship Move, sim thread, no fixture match. */
typedef void (*actor_all_ship_fn)(void *);
static void actor_all_ship_execute(actor_all_ship_fn original,void *self,uint64_t *rng) {
    int timed=actor_all_timed();uint64_t t0=timed?__builtin_ia32_rdtsc():0;unsigned klass;
    if(actor_all_ship_depth) {actor_all_count(ACTOR_ALL_SHIP,AA_NESTED);original(self);return;}
    uint64_t id=rng?actor_all_identity(self,&klass):0;
    if(!id) {actor_all_count(ACTOR_ALL_SHIP,AA_UNMAPPED);original(self);return;}
    actor_all_scope c;
    actor_all_open(&c,ACTOR_ALL_SHIP,rng,actor_all_key(ACTOR_ALL_SHIP,stats.issued,id,0,actor_all_ordinal(ACTOR_ALL_SHIP,id),0),id,klass,t0,timed);
    ++actor_all_ship_depth;void (*watch)(int,uint64_t,uint64_t *)=actor_all_ship_watch;
    if(watch) watch(1,id,rng);
    __try {original(self);}
    __finally {
        if(watch) watch(AbnormalTermination()?-1:0,id,rng);
        --actor_all_ship_depth;actor_all_close(&c,rng);if(AbnormalTermination()) InterlockedIncrement64(&actor_all_failures);
    }
}
/* Pathing Agent update, any thread, no fixture match. */
typedef void (*actor_all_agent_fn)(void *,void *,float,double,unsigned char);
static void actor_all_agent_execute(actor_all_agent_fn original,void *self,void *commands,float dt,double time,unsigned char cheap,uint64_t *rng) {
    int timed=actor_all_timed();uint64_t t0=timed?__builtin_ia32_rdtsc():0;unsigned klass;unsigned char *object=NULL;
    if(actor_all_agent_depth) {actor_all_count(ACTOR_ALL_AGENT,AA_NESTED);original(self,commands,dt,time,cheap);return;}
    uint64_t id=rng && actor_all_read((unsigned char*)self+0xef0,&object,8)?actor_all_identity(object,&klass):0;
    if(!id) {actor_all_count(ACTOR_ALL_AGENT,AA_UNMAPPED);original(self,commands,dt,time,cheap);return;}
    uint32_t dt_bits;uint64_t time_bits;memcpy(&dt_bits,&dt,4);memcpy(&time_bits,&time,8);
    actor_all_scope c;actor_all_open(&c,ACTOR_ALL_AGENT,rng,actor_all_key(ACTOR_ALL_AGENT,stats.issued,id,dt_bits,0,time_bits),id,klass,t0,timed);
    ++actor_all_agent_depth;
    __try {original(self,commands,dt,time,cheap);}
    __finally {--actor_all_agent_depth;actor_all_close(&c,rng);if(AbnormalTermination()) InterlockedIncrement64(&actor_all_failures);}
}
/* Steering update outside an agent scope (fallback). */
typedef void (*actor_all_flight_fn)(void *,void *,float,double,unsigned char,unsigned char);
static void actor_all_flight_execute(actor_all_flight_fn original,void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player,uint64_t *rng) {
    int timed=actor_all_timed();uint64_t t0=timed?__builtin_ia32_rdtsc():0;unsigned klass;unsigned char *agent=NULL,*object=NULL;
    if(actor_all_agent_depth) {actor_all_count(ACTOR_ALL_FLIGHT,AA_NESTED);original(self,slot,dt,time,cheap,player);return;}
    uint64_t id=rng && actor_all_read((unsigned char*)self+0x10,&agent,8) && actor_all_read(agent+0xef0,&object,8)?actor_all_identity(object,&klass):0;
    if(!id) {actor_all_count(ACTOR_ALL_FLIGHT,AA_UNMAPPED);original(self,slot,dt,time,cheap,player);return;}
    uint32_t dt_bits;uint64_t time_bits;memcpy(&dt_bits,&dt,4);memcpy(&time_bits,&time,8);
    actor_all_scope c;actor_all_open(&c,ACTOR_ALL_FLIGHT,rng,actor_all_key(ACTOR_ALL_FLIGHT,stats.issued,id,dt_bits,0,time_bits),id,klass,t0,timed);
    ++actor_all_agent_depth;
    __try {original(self,slot,dt,time,cheap,player);}
    __finally {--actor_all_agent_depth;actor_all_close(&c,rng);if(AbnormalTermination()) InterlockedIncrement64(&actor_all_failures);}
}
/* Navigation upkeep outside a ship scope (fallback, sim thread). */
typedef void (*actor_all_nav_fn)(void *,double);
static void actor_all_nav_execute(actor_all_nav_fn original,void *self,double time,uint64_t *rng) {
    int timed=actor_all_timed();uint64_t t0=timed?__builtin_ia32_rdtsc():0;unsigned klass;unsigned char *object=NULL;
    if(actor_all_ship_depth) {actor_all_count(ACTOR_ALL_NAV,AA_NESTED);original(self,time);return;}
    uint64_t id=rng && actor_all_read((unsigned char*)self+0x28,&object,8)?actor_all_identity(object,&klass):0;
    if(!id) {actor_all_count(ACTOR_ALL_NAV,AA_UNMAPPED);original(self,time);return;}
    uint64_t time_bits;memcpy(&time_bits,&time,8);
    actor_all_scope c;
    actor_all_open(&c,ACTOR_ALL_NAV,rng,actor_all_key(ACTOR_ALL_NAV,stats.issued,id,0,actor_all_ordinal(ACTOR_ALL_NAV,id),time_bits),id,klass,t0,timed);
    ++actor_all_ship_depth;
    __try {original(self,time);}
    __finally {--actor_all_ship_depth;actor_all_close(&c,rng);if(AbnormalTermination()) InterlockedIncrement64(&actor_all_failures);}
}
/* Weapon Move outside a ship scope (fallback, sim thread): the weapon's own ID. */
static void actor_all_weapon_execute(actor_all_ship_fn original,void *self,uint64_t *rng) {
    int timed=actor_all_timed();uint64_t t0=timed?__builtin_ia32_rdtsc():0;unsigned klass;
    if(actor_all_ship_depth) {actor_all_count(ACTOR_ALL_WEAPON,AA_NESTED);original(self);return;}
    uint64_t id=rng?actor_all_identity(self,&klass):0;
    if(!id) {actor_all_count(ACTOR_ALL_WEAPON,AA_UNMAPPED);original(self);return;}
    actor_all_scope c;
    actor_all_open(&c,ACTOR_ALL_WEAPON,rng,actor_all_key(ACTOR_ALL_WEAPON,stats.issued,id,0,actor_all_ordinal(ACTOR_ALL_WEAPON,id),0),id,klass,t0,timed);
    ++actor_all_ship_depth;
    __try {original(self);}
    __finally {--actor_all_ship_depth;actor_all_close(&c,rng);if(AbnormalTermination()) InterlockedIncrement64(&actor_all_failures);}
}
/* The calling thread's TLS RNG word, as the per-actor modules read it. */
static uint64_t *actor_all_tls_rng(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
/* Gate boundary (main thread; MoveWorker and pathing joined): fold frame T. */
static void actor_all_frame(uint64_t tick) {
    if(!actor_all_on()) return;
    for(unsigned p=0;p<ACTOR_ALL_PATHS;p++) for(unsigned f=0;f<AA_FIELDS;f++) {
        uint64_t sum=0;
        for(unsigned i=0;i<ACTOR_ALL_SHARDS;i++) sum+=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)&actor_all_shards[i].v[p][f],0,0);
        actor_all_frame_delta[p][f]=sum-actor_all_total[p][f];actor_all_total[p][f]=sum;
    }
    actor_all_frame_tick=tick;++actor_all_frames;
}
static uint64_t actor_all_ns(uint64_t ticks,uint64_t calls) {
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);uint64_t tsc=__builtin_ia32_rdtsc();
    if(!calls || !actor_all_tsc0 || tsc<=actor_all_tsc0 || now.QuadPart<=actor_all_qpc0.QuadPart || frequency.QuadPart<=0) return 0;
    double ns=(double)ticks/(double)calls*((double)(now.QuadPart-actor_all_qpc0.QuadPart)*1e9/(double)frequency.QuadPart)/(double)(tsc-actor_all_tsc0);
    return ns>=1.8e19?UINT64_MAX:(uint64_t)ns;
}
/* Per path: cumulative keyed/nested/unmapped/created/drawn/draws/unknown,
 * the sampled self cost per keyed call, then for the last folded frame
 * <path>_last=keyed/unmapped/drawn/draws and its keyset/drawset sums. */
static int actor_all_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&actor_all_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)ACTOR_ALL_ROW_RING && index<(uint64_t)(count-(LONG64)ACTOR_ALL_ROW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const actor_all_row_t *r=&actor_all_ring[index%ACTOR_ALL_ROW_RING];
    char draws[16];if(r->draws==UINT32_MAX) strcpy(draws,"unknown");else snprintf(draws,sizeof(draws),"%u",r->draws);
    n=snprintf(out,cap,"index=%llu tick=%llu path=%s id=%016llx draws=%s",(unsigned long long)index,(unsigned long long)r->tick,
        actor_all_names[r->path<ACTOR_ALL_PATHS?r->path:0],(unsigned long long)r->id,draws);
    return n>0 && (size_t)n<cap?n:0;
}
static int actor_all_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld frames=%llu last_tick=%llu table_full=%llu failures=%lld",actor_all_armed,
        (unsigned long long)actor_all_frames,(unsigned long long)actor_all_frame_tick,(unsigned long long)actor_all_table_full,
        (long long)actor_all_failures);
    for(unsigned p=0;n>0 && (size_t)n<capacity && p<ACTOR_ALL_PATHS;p++) {
        const uint64_t *t=actor_all_total[p],*d=actor_all_frame_delta[p];const char *name=actor_all_names[p];
        n+=snprintf(out+n,capacity-(size_t)n," %s_keyed=%llu %s_nested=%llu %s_unmapped=%llu %s_created=%llu %s_drawn=%llu %s_draws=%llu"
            " %s_unknown=%llu %s_ns=%llu %s_last=%llu/%llu/%llu/%llu %s_keyset=%016llx %s_drawset=%016llx",
            name,(unsigned long long)t[AA_KEYED],name,(unsigned long long)t[AA_NESTED],name,(unsigned long long)t[AA_UNMAPPED],
            name,(unsigned long long)t[AA_CREATED],name,(unsigned long long)t[AA_DRAWN],name,(unsigned long long)t[AA_DRAWS],
            name,(unsigned long long)t[AA_UNKNOWN],name,(unsigned long long)actor_all_ns(t[AA_TICKS],t[AA_TIMED]),
            name,(unsigned long long)d[AA_KEYED],(unsigned long long)d[AA_UNMAPPED],(unsigned long long)d[AA_DRAWN],(unsigned long long)d[AA_DRAWS],
            name,(unsigned long long)d[AA_KEYSET],name,(unsigned long long)d[AA_DRAWSET]);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
