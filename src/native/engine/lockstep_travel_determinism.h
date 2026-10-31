/* Census experiment, DEFAULT OFF. Q01's first event difference is a one-pass
 * shift of StartTravelModeEvent (and StopTravelModeEvent) for ~160 attention-2
 * small ships: 93/95 and 63/66 of each client's one-sided ships post the same
 * event one pathing pass later on the other client. Agent_UpdatePathing
 * 0xe8f250 decides travel on the JobThread at 0xe9189d-0xe91a22: it queues
 * Start/Stop in the batch command list when its own waypoint flag (+0xf7b,
 * rewritten every update at 0xe8f6b4) or its formation leader's flags
 * (+0xf20 -> leader Agent +0x4d2/+0x4d3/+0xf79/+0xf7b) ask for it and the flight
 * model (vtable +0xc8) agrees; +0xf40 throttles to 1 s (travelling) or 5 s.
 * The leader's +0xf7b is written by the leader's own update in the same pass,
 * usually on another JobThread, so a wingman sees this pass's or the previous
 * pass's value depending on thread timing. RunPass 0xeae3c0 queues one job per
 * batch at 0xeae942 and 0xeae9a0. This experiment runs those pathing batch
 * jobs inline on the pathing thread, in RunPass's batch order, with a JobThread
 * TLS context (game time 0, view 0) and a per-batch RNG key, so every
 * cross-agent read observes one fixed order. Requires the serial pathing join
 * and the lifted pathing budget. Other JTM jobs are forwarded unchanged. */
#define TRAVEL_SITE_A 0xeae940u /* aligned word holding the rel32 at 0xeae943 */
#define TRAVEL_SITE_B 0xeae9a0u /* aligned word holding the rel32 at 0xeae9a1 */
#define TRAVEL_QUEUE 0x14bc2d0u
#define TRAVEL_JOB_THUNK 0xf29cd0u
#define TRAVEL_BATCH_JOB 0xeaeb60u
#define TRAVEL_SLOT 4048u
#define TRAVEL_DOMAIN 0x58344c5452415645ULL /* "X4LTRAVE" */
typedef int (*travel_queue_fn)(void *,void *,void *,unsigned,unsigned);
typedef void (*travel_job_fn)(void *,void *);
static uint64_t travel_original[2],travel_patched[2];
static int travel_site_patched[2],travel_installed,travel_restored;
static volatile LONG travel_active;
static uint64_t travel_tick=UINT64_MAX,travel_passes,travel_batches,travel_pass_agents,travel_unmapped;
static uint64_t travel_pass_hash,travel_last_us,travel_max_us;
static unsigned travel_ordinal;
static volatile LONG64 travel_forwarded,travel_failures;
static const char *travel_stage="idle";
/* The order UpdateBatch 0xeaecc0 visits: from the cursor (+0x18), skipping the
 * sentinel (+0x10), for the member count (+8). Saved IDs make it comparable. */
static int travel_hash_batch(unsigned char *batch,uint64_t *hash,uint64_t *agents,uint64_t *unmapped) {
    int ok=1;
    __try {
        uint64_t members=*(uint64_t*)(batch+8);
        unsigned char *sentinel=*(unsigned char**)(batch+0x10),*node=*(unsigned char**)(batch+0x18);
        if(!sentinel || members>PATH_DET_CAPACITY) ok=0;
        for(uint64_t i=0;ok && i<members;i++) {
            if(node==sentinel) node=*(unsigned char**)(sentinel+8);
            if(!node || node==sentinel) {ok=0;break;}
            unsigned char *agent=*(unsigned char**)(node+0x10),*object=agent?*(unsigned char**)(agent+0xef0):NULL;
            uint64_t runtime=object?*(uint64_t*)(object+8):0,saved=runtime?world_identity_lookup(runtime):0;
            if(!saved) ++*unmapped;
            *hash=path_det_fnv(*hash,&saved,8);++*agents;
            node=*(unsigned char**)(node+8);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    return ok;
}
static int travel_payload_ok(const void *payload,const void *body) {
    void *batch=NULL,*fn=NULL;uint64_t cleanup=1;
    return payload && read_local(payload,&batch,8) && read_local((const unsigned char*)payload+8,&fn,8) &&
        read_local((const unsigned char*)payload+0x20,&cleanup,8) && batch && fn==body && !cleanup;
}
static int travel_run_inline(travel_job_fn job,void *payload,unsigned char *tls) {
    if(!tls) {travel_stage="tls";return 0;}
    if(stats.issued!=travel_tick) {
        travel_tick=stats.issued;travel_ordinal=0;travel_pass_agents=0;travel_pass_hash=0xcbf29ce484222325ULL;++travel_passes;
    }
    uint64_t hash=travel_pass_hash,agents=0,unmapped=0;
    if(!travel_hash_batch(*(unsigned char**)payload,&hash,&agents,&unmapped)) {travel_stage="batch";return 0;}
    unsigned ordinal=++travel_ordinal;
    uint64_t saved_time,saved_previous,saved_rng,zero=0,key=lockstep_seed(TRAVEL_DOMAIN,stats.issued,ordinal,0,0,0);
    uint32_t saved_view,zero_view=0;
    memcpy(&saved_time,tls+0x300,8);memcpy(&saved_previous,tls+0x328,8);memcpy(&saved_rng,tls+0x318,8);memcpy(&saved_view,tls+0x314,4);
    LARGE_INTEGER start,end;QueryPerformanceCounter(&start);
    __try {
        /* A JobThread never syncs TLS game time and reads the published view. */
        memcpy(tls+0x300,&zero,8);memcpy(tls+0x328,&zero,8);memcpy(tls+0x314,&zero_view,4);memcpy(tls+0x318,&key,8);
        job(payload,NULL);
    } __finally {
        memcpy(tls+0x300,&saved_time,8);memcpy(tls+0x328,&saved_previous,8);memcpy(tls+0x314,&saved_view,4);memcpy(tls+0x318,&saved_rng,8);
    }
    QueryPerformanceCounter(&end);LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
    uint64_t us=frequency.QuadPart?(uint64_t)((end.QuadPart-start.QuadPart)*1000000/frequency.QuadPart):0;
    if(ordinal==1) travel_last_us=0;
    travel_last_us+=us;if(travel_last_us>travel_max_us) travel_max_us=travel_last_us;
    travel_pass_hash=hash;travel_pass_agents+=agents;travel_unmapped+=unmapped;++travel_batches;travel_stage="done";
    return 1;
}
static int travel_queue_impl(travel_queue_fn queue,const void *job_thunk,const void *body,unsigned char *tls,
                             void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    if(!InterlockedCompareExchange(&travel_active,0,0) || !stats.active || boot_phase!=3 ||
       cb!=job_thunk || !travel_payload_ok(payload,body)) {
        InterlockedIncrement64(&travel_forwarded);return queue(mgr,cb,payload,cat,prio);
    }
    /* Nothing has run if inlining refuses; the job is then queued natively. */
    if(!travel_run_inline((travel_job_fn)cb,payload,tls)) {InterlockedIncrement64(&travel_failures);return queue(mgr,cb,payload,cat,prio);}
    return 0;
}
static __attribute__((noinline)) int travel_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return travel_queue_impl((travel_queue_fn)(image+TRAVEL_QUEUE),image+TRAVEL_JOB_THUNK,image+TRAVEL_BATCH_JOB,
        slots?(unsigned char*)slots[0]:NULL,mgr,cb,payload,cat,prio);
}
static int travel_swap(unsigned site,uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+site,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+site),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+site,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,8);return actual==expected;
}
static int travel_pins(void) {
    return hash_ok(image+0xeae3c0,1939,"bf0257fcb5e544638c7b2ce0d85e1a6b3c5abeaf1aa6839ebbf2ee50ccddb13b") && /* RunPass */
        hash_ok(image+TRAVEL_BATCH_JOB,339,"160804451a484e2717cf4d09c259a570fbc7439af163764d020e530b08f9156e") &&
        hash_ok(image+TRAVEL_JOB_THUNK,132,"a16058aac9e81a35586aeadc02cd42ab45f9a194059099ba0af6a548f9b79022") &&
        /* Barrier semaphore created with initial 0, maximum 1: surplus releases fail harmlessly. */
        hash_ok(image+0xeabb3b,17,"e24a397fe29e0d13a58e985c34c9f270ea4df765f51431d7d0327adc7423a767");
}
static int travel_patch_word(unsigned site,unsigned offset,uint64_t original,uint64_t *patched) {
    intptr_t distance=(intptr_t)(thunk+TRAVEL_SLOT)-(intptr_t)(image+site+offset+4);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    int32_t relative=(int32_t)distance;*patched=original;memcpy((unsigned char*)patched+offset,&relative,4);return 1;
}
static void travel_restore(void) {
    if(!travel_installed) return;
    InterlockedExchange(&travel_active,0);
    const unsigned sites[2]={TRAVEL_SITE_A,TRAVEL_SITE_B};
    for(unsigned i=0;i<2;i++) if(travel_site_patched[i] && travel_swap(sites[i],travel_patched[i],travel_original[i])) travel_site_patched[i]=0;
    travel_restored=!travel_site_patched[0] && !travel_site_patched[1];
    if(travel_restored) travel_installed=0;
}
static int travel_arm(void) {
    if(travel_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !path_installed || !(path_options&1) || !(path_det_mask&PATH_DET_BUDGET_BIT) || !travel_pins() ||
       !thunk_slot_ready(TRAVEL_SLOT)) return 0;
    memcpy(&travel_original[0],image+TRAVEL_SITE_A,8);memcpy(&travel_original[1],image+TRAVEL_SITE_B,8);
    if(!travel_patch_word(TRAVEL_SITE_A,3,travel_original[0],&travel_patched[0]) ||
       !travel_patch_word(TRAVEL_SITE_B,1,travel_original[1],&travel_patched[1])) return 0;
    travel_installed=1;
    travel_site_patched[0]=travel_swap(TRAVEL_SITE_A,travel_original[0],travel_patched[0]);
    travel_site_patched[1]=travel_site_patched[0] && travel_swap(TRAVEL_SITE_B,travel_original[1],travel_patched[1]);
    if(!travel_site_patched[1]) {travel_restore();travel_installed=0;return 0;}
    InterlockedExchange(&travel_active,1);return 1;
}
static int travel_owned(void) {
    return !travel_installed || (*(volatile uint64_t*)(image+TRAVEL_SITE_A)==travel_patched[0] &&
        *(volatile uint64_t*)(image+TRAVEL_SITE_B)==travel_patched[1]);
}
static int travel_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld installed=%d tick=%llu passes=%llu batches=%u agents=%llu order=%016llx "
        "pass_us=%llu max_us=%llu total_batches=%llu unmapped=%llu forwarded=%lld restored=%d stage=%s failures=%lld",
        travel_active,travel_installed,(unsigned long long)travel_tick,(unsigned long long)travel_passes,travel_ordinal,
        (unsigned long long)travel_pass_agents,(unsigned long long)travel_pass_hash,(unsigned long long)travel_last_us,
        (unsigned long long)travel_max_us,(unsigned long long)travel_batches,(unsigned long long)travel_unmapped,
        (long long)travel_forwarded,travel_restored,travel_stage,(long long)travel_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
