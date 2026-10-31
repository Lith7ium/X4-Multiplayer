/* Census experiment, DEFAULT OFF; the parallel alternative to the inline
 * travel determinism mode (mutually exclusive). Static audit of every
 * formation-leader dereference reached from the pathing pass (leader =
 * *(*(agent+0xf20)+0x48)): reads of the leader's bank go through the read
 * selector (0xe86469, 0xe8acd4 via 0xeb2faf), +0xef8 (0xe93b39 via 0xe8cbdd and
 * 0xe90d57, 0xe8d0f2, 0xeb6359) and +0xef0 are written only by constructors,
 * loads and MoveWorker orders (0xe856f8, 0xe93605 via 0xe981ff/0xeac432,
 * 0xeaf1e4 via 0xea92ff); +0x4d2/+0x4d3/+0xf79 have no pass-time writer; the
 * formation object is changed only by orders. The one leader field rewritten
 * during the pass is +0xf7b (each agent's own update: 0xe8f6b4, 0xe8fced,
 * 0xe88160, 0xe89f10), read by the wingman travel decision at 0xe918d5.
 * Before each pass is released, this mode copies every managed agent's +0xf7b
 * into its allocation padding +0xf88 (0xf90-byte object, last field +0xf80)
 * and that one read's displacement is changed to +0xf88, so a wingman always
 * sees its leader's pass-start value. Jobs stay parallel. Copying needs an
 * idle pathing thread, which only the serial join proves: the read moves to
 * the padding after the first joined live pass (that pass is unchanged) and
 * every later pass is preceded by a snapshot. Batch jobs report completion
 * through a callback wrapper so the join also waits for the command-list
 * splices that PathingBatchJob performs after releasing the barrier. Pathing
 * jobs make no RNG draws (J01), so no TLS context is changed. */
/* The Linux gate (linux/gate_travel_snapshot.h) defines these for its executable, and its own arming. */
#ifndef TSNAP_OWN_PLATFORM
#define TSNAP_READ_WORD 0xe918d0u /* aligned word; byte 7 is the disp32 low byte at 0xe918d7 */
#define TSNAP_LEA_A 0xeae930u /* 16-byte window: lea rdx,[rip+d32] at +4, d32 at +7 */
#define TSNAP_LEA_B 0xeae990u /* lea rdx,[rip+d32] at +2, d32 at +5 */
#define TSNAP_JOB_THUNK 0xf29cd0u
#define TSNAP_FLAG 0xf7bu
#define TSNAP_PAD 0xf88u
#define TSNAP_DISP_FLAG 0x7bu /* low byte of disp32 0x00000f7b; 0x0f stays */
#define TSNAP_DISP_PAD 0x88u /* low byte of disp32 0x00000f88 */
#define TSNAP_SLOT 4064u
#define TSNAP_AGENT_OBJECT 0xef0
#define TSNAP_FRESH 0x178
#define TSNAP_BATCHES 0x180
#define TSNAP_BATCHES_END 0x188
#endif
#define TSNAP_WAIT_MS 2000u
typedef void (*tsnap_job_fn)(void *,void *);
static uint64_t tsnap_read_original,tsnap_read_patched;
static unsigned __int128 tsnap_lea_original[2],tsnap_lea_patched[2];
static int tsnap_read_installed,tsnap_lea_installed[2],tsnap_installed,tsnap_restored;
static volatile LONG tsnap_active;
static volatile LONG64 tsnap_started,tsnap_done,tsnap_failures;
static uint64_t tsnap_tick=UINT64_MAX,tsnap_frames,tsnap_agents,tsnap_flagged,tsnap_new_agents,tsnap_hash,tsnap_unmapped,tsnap_unmapped_flagged;
static uint64_t tsnap_created,tsnap_walk_created;
static uint64_t tsnap_jobs,tsnap_snap_us,tsnap_max_snap_us,tsnap_pass_us,tsnap_max_pass_us,tsnap_wait_us,tsnap_max_wait_us;
static LARGE_INTEGER tsnap_release_qpc;
static int tsnap_idle,tsnap_released;
static const char *tsnap_stage="idle";
static uint64_t tsnap_us(LARGE_INTEGER from,LARGE_INTEGER to) {
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    return f.QuadPart && to.QuadPart>=from.QuadPart?(uint64_t)((to.QuadPart-from.QuadPart)*1000000/f.QuadPart):0;
}
#ifndef TSNAP_OWN_PLATFORM
static int tsnap_swap8(unsigned site,uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+site,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+site),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+site,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,8);return actual==expected;
}
static int tsnap_swap16(unsigned site,unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+site,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+site),expected,value);
    VirtualProtect(image+site,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,16);return actual==expected;
}
#endif
/* Worker callback wrapper: counts entry before and completion after the
 * native job thunk, which includes the command-list splice. */
static void tsnap_job_run(tsnap_job_fn job,void *slot,void *scratch) {
    InterlockedIncrement64(&tsnap_started);
    __try {job(slot,scratch);} __finally {InterlockedIncrement64(&tsnap_done);}
}
static void tsnap_job(void *slot,void *scratch) {tsnap_job_run((tsnap_job_fn)(image+TSNAP_JOB_THUNK),slot,scratch);}
/* Positional memory of the previous snapshot walk: the saved-ID cache (same
 * reuse rule as path determinism: same Agent, Object, runtime ID and identity
 * generation) and prefetch addresses. Every Agent is still read and copied. */
static path_det_member tsnap_seq[PATH_DET_CAPACITY];
static unsigned tsnap_seq_count,tsnap_gen=1;
static const void *tsnap_id_table;static LONG64 tsnap_id_count=-1;static uint64_t tsnap_id_created;
static uint64_t tsnap_lookups,tsnap_id_hits;
/* The receipt is portable: keyed by saved ID and summed (order-independent).
 * Agents without a saved ID (created after load; runtime IDs are process
 * local) are copied like all others but only counted, never hashed. With
 * native_gate_portable_created a created agent is hashed by its created ID
 * (counted as created, not unmapped); copying never depends on identity. */
static int tsnap_copy_agent(unsigned char *agent,unsigned k,uint64_t *agents,uint64_t *flagged,uint64_t *hash,uint64_t *unmapped,uint64_t *unmapped_flagged) {
    if(k+8<tsnap_seq_count) {
        const path_det_member *ahead=&tsnap_seq[k+8];
        __builtin_prefetch(ahead->node);__builtin_prefetch(ahead->agent+TSNAP_AGENT_OBJECT);__builtin_prefetch(ahead->agent+TSNAP_FLAG);
        __builtin_prefetch(ahead->agent+TSNAP_PAD,1);if(ahead->object) __builtin_prefetch(ahead->object);
    }
    unsigned char *object=*(unsigned char**)(agent+TSNAP_AGENT_OBJECT),flag=agent[TSNAP_FLAG];
    agent[TSNAP_PAD]=flag;
    uint64_t runtime=object?*(uint64_t*)(object+8):0,saved=0;unsigned klass=WORLD_KEY_NONE;
    if(runtime) {
        if(k<PATH_DET_CAPACITY) {
            path_det_member *e=&tsnap_seq[k];
            saved=path_det_saved(e,tsnap_gen,agent,object,runtime,&klass,&tsnap_lookups,&tsnap_id_hits);
            e->agent=agent;e->object=object;e->runtime=runtime;e->saved=saved;e->klass=(uint16_t)klass;e->gen=tsnap_gen;
        } else {++tsnap_lookups;saved=world_identity_key(runtime,&klass);}
    } else if(k<PATH_DET_CAPACITY) {tsnap_seq[k].agent=agent;tsnap_seq[k].object=object;tsnap_seq[k].runtime=0;tsnap_seq[k].gen=0;}
    if(klass==WORLD_KEY_CREATED) {*hash+=lockstep_mix(lockstep_mix(saved)+(flag!=0));++tsnap_walk_created;}
    else if(saved) *hash+=lockstep_mix(saved*2+(flag!=0));
    else {++*unmapped;*unmapped_flagged+=flag!=0;}
    ++*agents;*flagged+=flag!=0;return 1;
}
/* Every agent a pass can update or read as a leader: both batch vectors
 * (+0x180, +0x198) and the not-yet-assigned list whose sentinel is at +0x178. */
static int tsnap_snapshot(unsigned char *manager,uint64_t *agents,uint64_t *flagged,uint64_t *fresh,uint64_t *hash,uint64_t *unmapped,uint64_t *unmapped_flagged) {
    int ok=1;unsigned k=0;*agents=*flagged=*fresh=*hash=*unmapped=*unmapped_flagged=0;tsnap_walk_created=0;
    path_det_id_epoch(&tsnap_id_table,&tsnap_id_count,&tsnap_id_created,&tsnap_gen);
    __try {
        for(unsigned v=0;v<2 && ok;v++) {
            unsigned char *begin=*(unsigned char**)(manager+TSNAP_BATCHES+v*0x18),*end=*(unsigned char**)(manager+TSNAP_BATCHES_END+v*0x18);
            if(end<begin || (uintptr_t)(end-begin)%0x20) {ok=0;break;}
            for(unsigned char *batch=begin;batch<end && ok;batch+=0x20) {
                uint64_t members=*(uint64_t*)(batch+8);unsigned char *sentinel=*(unsigned char**)(batch+0x10),*node;
                if(!sentinel || members>PATH_DET_CAPACITY) {ok=0;break;}
                node=*(unsigned char**)(sentinel+8);
                for(uint64_t i=0;i<members;i++,node=*(unsigned char**)(node+8)) {
                    unsigned char *agent;
                    if(node==sentinel || !(agent=*(unsigned char**)(node+0x10))) {ok=0;break;}
                    if(k<PATH_DET_CAPACITY) tsnap_seq[k].node=node;
                    tsnap_copy_agent(agent,k++,agents,flagged,hash,unmapped,unmapped_flagged);
                }
                if(ok && node!=sentinel) ok=0;
            }
        }
        unsigned char *sentinel=*(unsigned char**)(manager+TSNAP_FRESH),*node;
        if(ok && !sentinel) ok=0;
        node=ok?*(unsigned char**)(sentinel+8):NULL;
        for(uint64_t i=0;ok && node!=sentinel;i++,node=*(unsigned char**)(node+8)) {
            unsigned char *agent;
            if(i>=PATH_DET_CAPACITY || !node || !(agent=*(unsigned char**)(node+0x10))) {ok=0;break;}
            if(k<PATH_DET_CAPACITY) tsnap_seq[k].node=node;
            tsnap_copy_agent(agent,k++,agents,flagged,hash,unmapped,unmapped_flagged);++*fresh;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    tsnap_seq_count=ok?(k<PATH_DET_CAPACITY?k:PATH_DET_CAPACITY):0;
    return ok;
}
static int tsnap_drain(unsigned timeout_ms,uint64_t *waited_us) {
    LARGE_INTEGER start,now;QueryPerformanceCounter(&start);ULONGLONG begin=GetTickCount64();
    while(InterlockedCompareExchange64(&tsnap_done,0,0)!=InterlockedCompareExchange64(&tsnap_started,0,0)) {
        if(GetTickCount64()-begin>timeout_ms) {tsnap_stage="drain_timeout";return 0;}
        SwitchToThread();
    }
    QueryPerformanceCounter(&now);*waited_us=tsnap_us(start,now);return 1;
}
static int tsnap_take(unsigned char *manager) {
    uint64_t agents,flagged,fresh,hash,unmapped,unmapped_flagged;LARGE_INTEGER start,end;QueryPerformanceCounter(&start);
    if(!manager || !tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged)) {tsnap_stage="snapshot";return 0;}
    QueryPerformanceCounter(&end);
    tsnap_snap_us=tsnap_us(start,end);if(tsnap_snap_us>tsnap_max_snap_us) tsnap_max_snap_us=tsnap_snap_us;
    tsnap_agents=agents;tsnap_flagged=flagged;tsnap_new_agents=fresh;tsnap_hash=hash;tsnap_unmapped=unmapped;
    tsnap_unmapped_flagged=unmapped_flagged;tsnap_created=tsnap_walk_created;tsnap_tick=stats.issued;++tsnap_frames;return 1;
}
/* MoveWorker, live frame, before releasing its pass. The previous pass was
 * joined and drained by tsnap_after_join, so no job reads +0xf88 now. */
static int tsnap_before_release(unsigned char *manager) {
    if(tsnap_read_installed && !tsnap_idle) {tsnap_stage="not_idle";return 0;}
    if(tsnap_idle && !tsnap_take(manager)) return 0;
    tsnap_idle=0;tsnap_released=1;tsnap_jobs=(uint64_t)InterlockedCompareExchange64(&tsnap_started,0,0);
    QueryPerformanceCounter(&tsnap_release_qpc);tsnap_stage="released";return 1;
}
/* MoveWorker, after the serial join: RunPass returned, so every batch job has
 * entered the wrapper; wait for their splices before the frame flushes events.
 * The pathing thread is now provably idle: install the read on first use. */
static int tsnap_after_join(unsigned char *manager) {
    uint64_t waited=0;LARGE_INTEGER joined;QueryPerformanceCounter(&joined);
    if(!tsnap_released) {tsnap_stage="not_released";return 0;}
    if(!tsnap_drain(TSNAP_WAIT_MS,&waited)) return 0;
    tsnap_pass_us=tsnap_us(tsnap_release_qpc,joined);if(tsnap_pass_us>tsnap_max_pass_us) tsnap_max_pass_us=tsnap_pass_us;
    tsnap_wait_us=waited;if(waited>tsnap_max_wait_us) tsnap_max_wait_us=waited;
    tsnap_jobs=(uint64_t)InterlockedCompareExchange64(&tsnap_started,0,0)-tsnap_jobs;tsnap_released=0;
    if(!tsnap_read_installed) {
        if(!tsnap_take(manager)) return 0;
        if(!tsnap_swap8(TSNAP_READ_WORD,tsnap_read_original,tsnap_read_patched)) {tsnap_stage="read_patch";return 0;}
        tsnap_read_installed=1;
    }
    tsnap_idle=1;tsnap_stage="joined";return 1;
}
#ifndef TSNAP_OWN_PLATFORM
/* Pins of other features cover whole functions; they hash the pre-patch bytes. */
static int tsnap_unpatched_hash(unsigned rva,ULONG length,const char *expected) {
    static unsigned char copy[16384];const unsigned byte=TSNAP_READ_WORD+7;
    if(length>sizeof(copy) || !read_local(image+rva,copy,length)) return 0;
    if(tsnap_read_installed && byte>=rva && byte<rva+length) copy[byte-rva]=(unsigned char)(tsnap_read_original>>56);
    return hash_ok(copy,length,expected);
}
static int tsnap_pins(void) {
    return hash_ok(image+0xe9189d,89,"16bd114b404ed99f27ca3953c15fde2ca6bb44881a6645b437cfec9e5711db8a") && /* travel block */
        hash_ok(image+0xeae3c0,1939,"bf0257fcb5e544638c7b2ce0d85e1a6b3c5abeaf1aa6839ebbf2ee50ccddb13b") && /* RunPass */
        hash_ok(image+TSNAP_JOB_THUNK,132,"a16058aac9e81a35586aeadc02cd42ab45f9a194059099ba0af6a548f9b79022") &&
        hash_ok(image+0xeac5d0,18,"faed0276da9c4138faeb84258066f98c05d2a1e62fff0f76900a3f9d452e4c18") && /* 0xf90-byte Agent */
        hash_ok(image+0xe85772,34,"dc342dd5ff59ee45fa9d04ff57ba6208063c751841577ec92b55ee2add3a75dc"); /* tail ends at +0xf88 */
}
#endif
static void tsnap_restore(void) {
    if(!tsnap_installed) return;
    InterlockedExchange(&tsnap_active,0);
    /* The read goes first: no wingman reads padding once the pass stops refreshing it. */
    if(tsnap_read_installed && tsnap_swap8(TSNAP_READ_WORD,tsnap_read_patched,tsnap_read_original)) tsnap_read_installed=0;
    const unsigned sites[2]={TSNAP_LEA_A,TSNAP_LEA_B};
    for(unsigned i=0;i<2;i++) if(tsnap_lea_installed[i] && tsnap_swap16(sites[i],tsnap_lea_patched[i],tsnap_lea_original[i])) tsnap_lea_installed[i]=0;
    tsnap_restored=!tsnap_read_installed && !tsnap_lea_installed[0] && !tsnap_lea_installed[1];
    if(tsnap_restored) tsnap_installed=0;
}
static int tsnap_lea_word(unsigned site,unsigned offset,unsigned __int128 original,unsigned __int128 *patched) {
    intptr_t distance=(intptr_t)(thunk+TSNAP_SLOT)-(intptr_t)(image+site+offset+4);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    int32_t relative=(int32_t)distance;*patched=original;memcpy((unsigned char*)patched+offset,&relative,4);return 1;
}
#ifndef TSNAP_OWN_PLATFORM
static int tsnap_arm(void) {
    if(tsnap_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) || travel_installed || !path_installed || !(path_options&1) ||
       !(path_det_mask&PATH_DET_BUDGET_BIT) || !tsnap_pins() || !thunk_slot_ready(TSNAP_SLOT)) return 0;
    memcpy(&tsnap_lea_original[0],image+TSNAP_LEA_A,16);memcpy(&tsnap_lea_original[1],image+TSNAP_LEA_B,16);
    memcpy(&tsnap_read_original,image+TSNAP_READ_WORD,8);
    if((tsnap_read_original>>56)!=TSNAP_DISP_FLAG || image[TSNAP_READ_WORD+8]!=0x0f || !tsnap_lea_word(TSNAP_LEA_A,7,tsnap_lea_original[0],&tsnap_lea_patched[0]) ||
       !tsnap_lea_word(TSNAP_LEA_B,5,tsnap_lea_original[1],&tsnap_lea_patched[1])) return 0;
    tsnap_read_patched=(tsnap_read_original&0x00ffffffffffffffULL)|((uint64_t)TSNAP_DISP_PAD<<56);
    tsnap_installed=1;tsnap_idle=tsnap_released=0;
    /* Completion wrappers only; the read moves after the first joined live pass. */
    tsnap_lea_installed[0]=tsnap_swap16(TSNAP_LEA_A,tsnap_lea_original[0],tsnap_lea_patched[0]);
    tsnap_lea_installed[1]=tsnap_lea_installed[0] && tsnap_swap16(TSNAP_LEA_B,tsnap_lea_original[1],tsnap_lea_patched[1]);
    if(!tsnap_lea_installed[1]) {tsnap_restore();tsnap_installed=0;return 0;}
    InterlockedExchange(&tsnap_active,1);return 1;
}
#endif
static int tsnap_owned(void) {
    return !tsnap_installed || (*(volatile uint64_t*)(image+TSNAP_READ_WORD)==(tsnap_read_installed?tsnap_read_patched:tsnap_read_original) &&
        *(volatile unsigned __int128*)(image+TSNAP_LEA_A)==tsnap_lea_patched[0] &&
        *(volatile unsigned __int128*)(image+TSNAP_LEA_B)==tsnap_lea_patched[1]);
}
static int tsnap_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld installed=%d read=%d tick=%llu frames=%llu agents=%llu new=%llu flagged=%llu snapshot=%016llx "
        "unmapped=%llu unmapped_flagged=%llu jobs=%llu pass_us=%llu max_pass_us=%llu snap_us=%llu max_snap_us=%llu wait_us=%llu max_wait_us=%llu "
        "restored=%d stage=%s failures=%lld lookups=%llu id_hits=%llu created=%llu",
        tsnap_active,tsnap_installed,tsnap_read_installed,(unsigned long long)tsnap_tick,(unsigned long long)tsnap_frames,(unsigned long long)tsnap_agents,
        (unsigned long long)tsnap_new_agents,(unsigned long long)tsnap_flagged,(unsigned long long)tsnap_hash,(unsigned long long)tsnap_unmapped,(unsigned long long)tsnap_unmapped_flagged,
        (unsigned long long)tsnap_jobs,(unsigned long long)tsnap_pass_us,(unsigned long long)tsnap_max_pass_us,(unsigned long long)tsnap_snap_us,
        (unsigned long long)tsnap_max_snap_us,(unsigned long long)tsnap_wait_us,(unsigned long long)tsnap_max_wait_us,
        tsnap_restored,tsnap_stage,(long long)tsnap_failures,(unsigned long long)tsnap_lookups,(unsigned long long)tsnap_id_hits,
        (unsigned long long)tsnap_created);
    return n>0 && (size_t)n<capacity?n:0;
}
