/* Per-job deterministic RNG for the JobThreadManager worker pool, DEFAULT OFF.
 *
 * Census c003 showed that the simulation thread, adopted from the ~40-strong
 * JobThread pool (start RVA 0x1a1b9a4, Runnable XLib::JobThread::Run 0x14bbff0),
 * makes a different number of RNG draws per frame between two clients even with
 * an identical per-frame sim seed: jobs land on different workers run to run, so
 * the TLS RNG stream (TLS slot0+0x318) a job body draws from depends on which
 * worker ran it and in which order. The whole JTM job layer is one choke point.
 *
 * Every JTM job is submitted through JobThreadManager::QueueJob 0x14bc2d0 as
 *   int QueueJob(mgr, void *callback, void *payload, unsigned cat, unsigned prio)
 * which pops a free job id, copies the 0x98-byte payload to slot+0 (slot =
 * mgr+0x5b8+id*0xb8), stores the callback at slot+0xa0, the category at +0xac,
 * the priority at +0xb0, marks slot+0xa8 ready and releases the pool semaphore.
 * The sole execution dispatch is XLib::JobThread::Run's `call [slot+0xa0]`
 * (rcx=slot, rdx=per-worker scratch); the barrier/flush drain reuses the same
 * field. (Jolt's own ready jobs run inline on Jolt threads and never reach
 * QueueJob, so they are out of scope.)
 *
 * We hook QueueJob. For each submission on the live frame we derive a stable
 * key H(submit tick, callback RVA, category, per-callback submission ordinal,
 * inner body RVA), substitute the callback with job_rng_exec, and stash the real
 * callback in the slot's unused gap +0x98 (the payload copy is [0,0x98)). The
 * worker then calls job_rng_exec, which recovers the real callback from +0x98,
 * optionally writes the key into the executing thread's TLS word, runs the body,
 * restores the word (also on exception), and records a census row.
 *
 * Race: QueueJob releases the semaphore before returning, so a worker could run
 * the job the instant it is queued. We therefore hold the manager critical
 * section (mgr+0x540, the same one a worker takes to pop) across QueueJob and the
 * side-table / slot+0x98 write, so no worker can pop the job until its record is
 * in place. We only do this when at least JOB_FREE_FLOOR free slots exist, so
 * QueueJob cannot take its full-table SleepConditionVariableCS path while we hold
 * the lock. The ordinal is stable only when a single thread submits a given
 * callback within a frame; if a second submitter thread appears for that callback
 * in the same tick we fail closed (run the job unmodified, counted).
 *
 * Modes: "key" reseeds and restores; "observe" substitutes and records the
 * natural entry/exit words without touching RNG, so the coordinator can count
 * draws per job offline (rng_steps from seed word to exit word) and diff by key
 * across clients with no behaviour change. Default off; restore unpatches the
 * entry (in-flight jobs still drain safely through +0x98) and clears seeding. */
#define JOB_QUEUE_ENTRY 0x14bc2d0u
#define JOB_QUEUE_RUN 0x14bbff0u
#define JOB_MGR_RVA 0x6cf1540u
#define JOB_CS_OFF 0x540u
#define JOB_SLOTBASE_OFF 0x5b8u
#define JOB_STRIDE 0xb8u
#define JOB_CB_OFF 0xa0u
#define JOB_CB_SPARE_OFF 0x98u
#define JOB_FREECOUNT_OFF 0x5b0u
#define JOB_RNG_SLOT 3968u
#define JOB_FREE_FLOOR 2
#define JOB_RNG_BUCKETS 128u
#define JOB_RNG_RING 8192u
#define JOB_RNG_DOMAIN 0x58344c4a4f42524eULL /* "X4LJOBRN" */

enum {JOB_RNG_OBSERVE=0,JOB_RNG_KEY=1};
typedef int (*job_queue_fn)(void *,void *,void *,unsigned,unsigned);
typedef void (*job_cb_fn)(void *,void *);

typedef struct {void *real_cb;uint64_t key;uint32_t cb_rva,body_rva,ordinal,submit_tick;uint16_t category;uint8_t seed,valid;} job_rng_record;
typedef struct {uint64_t key,seed_word,exit_word;uint32_t tick,cb_rva,body_rva,ordinal;DWORD tid;uint16_t category;uint8_t seeded;} job_rng_row_t;
typedef struct {uint64_t cb_rva,tick;LONG64 count;DWORD tid;int multi,used;} job_rng_bucket;

static job_rng_record job_rng_table[0x8000];
static job_rng_row_t job_rng_ring[JOB_RNG_RING];
static volatile LONG64 job_rng_ring_cursor;
static job_rng_bucket job_rng_buckets[JOB_RNG_BUCKETS];
static CRITICAL_SECTION job_rng_ord_lock;
static uint64_t job_rng_original_word,job_rng_patched_word;
static int job_rng_installed,job_rng_restored,job_rng_seed_mode,job_rng_lock_ready;
static volatile LONG job_rng_active;
static volatile LONG64 job_rng_submissions,job_rng_substituted,job_rng_seeded,job_rng_restored_runs,
    job_rng_passthrough,job_rng_misses,job_rng_multi,job_rng_lowslots,job_rng_foreign_cb,job_rng_queue_fail,job_rng_failures;

static uint64_t job_rng_key(uint64_t tick,uint64_t cb_rva,uint64_t category,uint64_t ordinal,uint64_t body_rva) {
    return lockstep_seed(JOB_RNG_DOMAIN,tick,cb_rva,category,ordinal,body_rva);
}
static uint64_t *job_rng_tls(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
/* Per-callback submission ordinal, reset each tick; 0 means "do not key"
 * (overflow or a second submitting thread this tick for the same callback). */
static uint64_t job_rng_ordinal(uint64_t cb_rva,uint64_t tick,DWORD tid,int *multi) {
    uint64_t ord=0;*multi=0;
    if(!job_rng_lock_ready) {*multi=1;return 0;}
    EnterCriticalSection(&job_rng_ord_lock);
    job_rng_bucket *b=NULL;
    for(unsigned i=0;i<JOB_RNG_BUCKETS;i++) if(job_rng_buckets[i].used && job_rng_buckets[i].cb_rva==cb_rva) {b=&job_rng_buckets[i];break;}
    if(!b) for(unsigned i=0;i<JOB_RNG_BUCKETS;i++) if(!job_rng_buckets[i].used) {
        b=&job_rng_buckets[i];b->used=1;b->cb_rva=cb_rva;b->tick=tick;b->count=0;b->tid=tid;b->multi=0;break;
    }
    if(b) {
        if(b->tick!=tick) {b->tick=tick;b->count=0;b->tid=tid;b->multi=0;}
        if(b->tid!=tid) b->multi=1;
        if(b->multi) *multi=1;else ord=(uint64_t)(++b->count);
    } else *multi=1;
    LeaveCriticalSection(&job_rng_ord_lock);
    return ord;
}
static void job_rng_record_row(const job_rng_record *rec,uint32_t tick,DWORD tid,uint64_t seed_word,uint64_t exit_word,int seeded) {
    LONG64 idx=InterlockedIncrement64(&job_rng_ring_cursor)-1;
    job_rng_row_t *r=&job_rng_ring[(uint64_t)idx%JOB_RNG_RING];
    r->key=rec->key;r->seed_word=seed_word;r->exit_word=exit_word;
    r->tick=tick;r->cb_rva=rec->cb_rva;r->body_rva=rec->body_rva;r->ordinal=rec->ordinal;
    r->tid=tid;r->category=rec->category;r->seeded=(uint8_t)seeded;
}
/* Worker-thread execution wrapper; rng is the executing thread's TLS word. */
static void job_rng_run(uint64_t *rng,void *jobslot,void *scratch) {
    unsigned char *slot=jobslot,*mgr=*(unsigned char**)(image+JOB_MGR_RVA);
    void *real=*(void**)(slot+JOB_CB_SPARE_OFF);
    job_rng_record rec;memset(&rec,0,sizeof(rec));int have=0;
    if(mgr) {
        uintptr_t base=(uintptr_t)(mgr+JOB_SLOTBASE_OFF),s=(uintptr_t)slot;
        if(s>=base && (s-base)%JOB_STRIDE==0) {uint64_t id=(s-base)/JOB_STRIDE;if(id<0x8000) {rec=job_rng_table[id];have=1;}}
    }
    if(!real) {InterlockedIncrement64(&job_rng_misses);InterlockedIncrement64(&job_rng_failures);return;}
    job_cb_fn cb=(job_cb_fn)real;
    int do_seed=have && rec.valid && rec.seed && rng && InterlockedCompareExchange(&job_rng_active,0,0);
    uint64_t entry_word=rng?*rng:0,seed_word=do_seed?rec.key:entry_word,saved=entry_word,exit_word=0;DWORD tid=GetCurrentThreadId();
    if(do_seed) {*rng=rec.key;InterlockedIncrement64(&job_rng_seeded);} else InterlockedIncrement64(&job_rng_passthrough);
    __try {
        cb(jobslot,scratch);
    } __finally {
        exit_word=rng?*rng:0;
        if(do_seed) {*rng=saved;InterlockedIncrement64(&job_rng_restored_runs);}
        if(AbnormalTermination()) InterlockedIncrement64(&job_rng_failures);
        if(have) job_rng_record_row(&rec,(uint32_t)stats.issued,tid,seed_word,exit_word,do_seed);
    }
}
static void job_rng_exec(void *jobslot,void *scratch) {job_rng_run(job_rng_tls(),jobslot,scratch);}
/* Submission wrapper; original is the thunk trampoline re-entering QueueJob. */
static int job_rng_submit(job_queue_fn original,void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    if(!InterlockedCompareExchange(&job_rng_active,0,0) || !stats.active || boot_phase!=3 || !mgr || !cb)
        return original(mgr,cb,payload,cat,prio);
    uintptr_t cbp=(uintptr_t)cb,base=(uintptr_t)image;
    if(cbp<base || cbp>=base+0x6fa9000) {InterlockedIncrement64(&job_rng_foreign_cb);return original(mgr,cb,payload,cat,prio);}
    InterlockedIncrement64(&job_rng_submissions);
    uint64_t cb_rva=cbp-base,body_rva=0;void *body=NULL;
    if(read_local((unsigned char*)payload+8,&body,8) && (uintptr_t)body>=base && (uintptr_t)body<base+0x6fa9000)
        body_rva=(uintptr_t)body-base;
    DWORD tid=GetCurrentThreadId();uint64_t tick=stats.issued;int multi=0;
    uint64_t ordinal=job_rng_ordinal(cb_rva,tick,tid,&multi);
    if(multi || !ordinal) {InterlockedIncrement64(&job_rng_multi);return original(mgr,cb,payload,cat,prio);}
    uint64_t key=job_rng_key(tick,cb_rva,cat,ordinal,body_rva);
    CRITICAL_SECTION *cs=(CRITICAL_SECTION*)((unsigned char*)mgr+JOB_CS_OFF);
    EnterCriticalSection(cs);
    LONG64 freecount=*(volatile LONG64*)((unsigned char*)mgr+JOB_FREECOUNT_OFF);
    if(freecount<JOB_FREE_FLOOR) {LeaveCriticalSection(cs);InterlockedIncrement64(&job_rng_lowslots);return original(mgr,cb,payload,cat,prio);}
    int id=original(mgr,(void*)job_rng_exec,payload,cat,prio);
    if(id>=0 && id<0x8000) {
        unsigned char *slot=(unsigned char*)mgr+JOB_SLOTBASE_OFF+(unsigned)id*JOB_STRIDE;
        *(void**)(slot+JOB_CB_SPARE_OFF)=cb;
        job_rng_record *rec=&job_rng_table[id];
        rec->real_cb=cb;rec->key=key;rec->cb_rva=(uint32_t)cb_rva;rec->body_rva=(uint32_t)body_rva;
        rec->ordinal=(uint32_t)ordinal;rec->submit_tick=(uint32_t)tick;rec->category=(uint16_t)cat;
        rec->seed=(uint8_t)(job_rng_seed_mode==JOB_RNG_KEY);rec->valid=1;
        InterlockedIncrement64(&job_rng_substituted);
    } else InterlockedIncrement64(&job_rng_queue_fail);
    LeaveCriticalSection(cs);
    return id;
}
static __attribute__((noinline)) int job_rng_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    return job_rng_submit((job_queue_fn)(thunk+JOB_RNG_SLOT),mgr,cb,payload,cat,prio);
}
static int job_rng_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+JOB_QUEUE_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+JOB_QUEUE_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+JOB_QUEUE_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+JOB_QUEUE_ENTRY,8);
    return actual==expected;
}
static int job_rng_install(int mode) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4lan_hooks_64.dll") ||
       !hash_ok(image+JOB_QUEUE_ENTRY,0x212,"62afc848eb85da6b875653430983653086fc27a31e1a16f51043dca2e0a1fefd") ||
       !hash_ok(image+JOB_QUEUE_RUN,0x2dc,"9cc3480eeb9f14466c7dff9390a7e7d55676419ea65f98c9191b74138f0e2263")) return 0;
    if(!*(void**)(image+JOB_MGR_RVA)) return 0;
    intptr_t distance=(intptr_t)(thunk+JOB_RNG_SLOT+32)-(intptr_t)(image+JOB_QUEUE_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(JOB_RNG_SLOT)) return 0;
    if(!job_rng_lock_ready) {InitializeCriticalSection(&job_rng_ord_lock);job_rng_lock_ready=1;}
    memcpy(&job_rng_original_word,image+JOB_QUEUE_ENTRY,8);job_rng_patched_word=job_rng_original_word;
    unsigned char *patch=(unsigned char*)&job_rng_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!job_rng_swap(job_rng_original_word,job_rng_patched_word)) return 0;
    job_rng_seed_mode=mode==JOB_RNG_KEY?JOB_RNG_KEY:JOB_RNG_OBSERVE;job_rng_installed=1;InterlockedExchange(&job_rng_active,1);return 1;
}
/* Armed once at boot, before load, like the boot RNG anchor; idempotent on a
 * loading-UI reload that reruns the probe configuration with the same mode. */
static int job_rng_arm(int mode) {
    if(job_rng_installed) return stats.active && job_rng_seed_mode==(mode==JOB_RNG_KEY?JOB_RNG_KEY:JOB_RNG_OBSERVE);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    return job_rng_install(mode);
}
static void job_rng_restore(void) {
    if(!job_rng_installed) return;
    InterlockedExchange(&job_rng_active,0);
    job_rng_restored=job_rng_swap(job_rng_patched_word,job_rng_original_word);
    if(job_rng_restored) job_rng_installed=0;
}
static int job_rng_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&job_rng_ring_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)JOB_RNG_RING?cursor-(LONG64)JOB_RNG_RING:0;
    int n=snprintf(out,cap,"installed=%d active=%ld mode=%d restored=%d records=%lld dropped=%lld ring=%u"
        " submissions=%lld substituted=%lld seeded=%lld restored_runs=%lld passthrough=%lld misses=%lld"
        " multi=%lld lowslots=%lld foreign=%lld queuefail=%lld failures=%lld",
        job_rng_installed,job_rng_active,job_rng_seed_mode,job_rng_restored,(long long)cursor,(long long)dropped,JOB_RNG_RING,
        (long long)job_rng_submissions,(long long)job_rng_substituted,(long long)job_rng_seeded,(long long)job_rng_restored_runs,
        (long long)job_rng_passthrough,(long long)job_rng_misses,(long long)job_rng_multi,(long long)job_rng_lowslots,
        (long long)job_rng_foreign_cb,(long long)job_rng_queue_fail,(long long)job_rng_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per call (never one huge line per tick): the probe walks
 * absolute indices until nil. Evicted rows report evicted=1 so it keeps going. */
static int job_rng_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&job_rng_ring_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)JOB_RNG_RING && index<(uint64_t)(cursor-(LONG64)JOB_RNG_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        job_rng_row_t *r=&job_rng_ring[index%JOB_RNG_RING];
        n=snprintf(out,cap,"index=%llu tick=%u key=%016llx cb=%x body=%x ord=%u cat=%u tid=%lu seeded=%u seed=%016llx exit=%016llx",
            (unsigned long long)index,r->tick,(unsigned long long)r->key,r->cb_rva,r->body_rva,r->ordinal,r->category,
            (unsigned long)r->tid,r->seeded,(unsigned long long)r->seed_word,(unsigned long long)r->exit_word);
    }
    return n>0 && (size_t)n<cap?n:0;
}
