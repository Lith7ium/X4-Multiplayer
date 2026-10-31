/* Determinism fix, DEFAULT OFF: Gravidar factor-pass and scan deadlines
 * (native_gate_gravidar_deadlines; needs native_gate_gravidar_time_sync and the
 * world identities).
 *
 * long-L9 (every current fix plus native_gate_gravidar_digest, 1500 ticks):
 * the radar factor digest parts at tick 325, the world state at the 900
 * capture. A unit's factor (+0x44) is written only by SetObjectFactor
 * 0x10b30c0, from Object::SetParent 0x6cdab0 and from each sector's periodic
 * factor pass: U::Sector::Move (slot 674 0x784230, vtable slot 0x2b1ff58,
 * called by the component walk on the main thread) runs Positional::Move
 * 0x748120 at attention (+0x1ec) > 4 (which moves the sector's children through
 * their slot 674), then, when the sector holds a Gravidar field (+0x442)
 * and its deadline +0x448 is not at or after the TLS game time (slot0+0x300),
 * sets the factor of every object of the sector (+0xb0) to the field
 * interference at its position and schedules the next pass:
 *   attention >= 5: now + 1 s;
 *   else:           now + 5 s + Random::FloatState(slot0+0x318, 1.0) 0x14ddc90,
 * a draw from the shared main-thread word in the middle of the walk. The
 * constructor 0x781e60 sets +0x448 = 0 and nothing saves it, so the first Move
 * after loading runs the pass: in the ungated boot frame's walk (L9: 461
 * Region::Moves passed natively before tick 1, region_rng's passed=), at the
 * load time 7.083 with the boot word, which nothing keys, or at tick 1 from a
 * walk word that may already differ (the digest rows show the deadlines from
 * tick 1 on). Either way each client starts with its own deadline, and the
 * first gated low-attention pass falls on tick 300-360, a different one per
 * client (L9: the factors part at 325). Later passes draw from the walk
 * stream, which earlier consumers shift (lockstep_region_rng.h), and the
 * 1 s / 5-6 s choice follows the sector's attention.
 * Fix, swapping the slot for gdl_move (main thread, boot phase 3, sectors
 * with a field; every other Move passes through untouched):
 * - the first admitted Move of each such sector (insert-only table of runtime
 *   IDs, as native_gate_nav_probe_init) sets +0x448 = 0 first: every client
 *   runs the pass in that frame with the gated time, as after a fresh load;
 * - a Move whose pass is due at attention < 5 (no child Moves, the one draw)
 *   runs on a word keyed lockstep_seed(GDL_DOMAIN, tick, sector key, 0, 0, 0)
 *   (saved or created ID; a pass moves the deadline at least 1 s ahead, so one
 *   per tick), the caller's word restored afterwards, also on unwind. A due
 *   pass at attention >= 5 draws nothing and runs natively, so the children's
 *   Moves keep their own streams; should the attention drop below 5 during
 *   that Move, its low pass draws natively and is counted (unscoped). A
 *   sector without a portable key keeps its natural draw (counted unmapped).
 * Unit scan times: only an AI::ActiveGravidarUnit has them (0xc8 bytes, built
 * by 0x10b2820; ctor 0x10ac0c0 writes -1.0 to all three). Its apply (slot 7
 * 0x10acd70, Gravidar thread) sets +0xa8 = TLS time, +0xb0 = time + 0.5 + a
 * draw from the thread word, and +0xb8 = time when it posts has_scanned (the
 * contact set changed, or 10 s after the last); the sector scan 0x10b34c0
 * (0x10b3850) applies a unit when +0xb0 is -1 or due. Nothing else in X4
 * writes them (slot 1 0x10ac3d0 only marks a released unit); the gate's own
 * native_gate_radar_initial_refresh sets the fixture units' times when they
 * register (lockstep_gravidar.h, frame 1's snapshot preparation). With the time
 * synced (native_gate_gravidar_time_sync), the draw keyed
 * (native_gate_gravidar_key_all) and the load pass held to gated frame 1
 * (native_gate_gravidar_load_pass) there is no other input. Without the hold,
 * Init's load pass applies units at load time with the Gravidar thread's
 * unkeyed word. So at boot-phase-3 entry (after the boot drain, Gravidar idle,
 * before the fixture registration) every active unit whose times are not the
 * constructor's is set back to -1/-1/-1 under its own lock: its first gated
 * visit applies it, keyed, and posts has_scanned like a new unit.
 * Failures (no TLS block, an unreadable sector, the table full, an unwind, a
 * busy or unreadable Gravidar at admission) stop the gate with reason 121;
 * a replaced slot is caught by the owned check; stop gives the slot back. */
#define GDL_DOMAIN 0x58344c4752444c4eULL /* "X4LGRDLN" */
/* The Linux gate (linux/gate_gravidar_deadlines.h) defines these for its executable. */
#ifndef GDL_OWN_PLATFORM
#define GDL_SLOT 0x2b1ff58u            /* U::Sector vtable 0x2b1ea48, slot 674 */
#define GDL_MOVE 0x784230u             /* U::Sector::Move */
#define GDL_MOVE_LENGTH 0x2e2u
#define GDL_UNIT_CTOR 0x10ac0c0u       /* AI::ActiveGravidarUnit ctor */
#define GDL_UNIT_CTOR_LENGTH 0x1ffu
#define GDL_FIELD 0x442u
#define GDL_DUE 0x448u
#define GDL_TLS_READY 0x10
#define GDL_TLS_TIME 0x300
#define GDL_TLS_WORD 0x318
#endif
#define GDL_ATTENTION 0x1ecu
#define GDL_TABLE 4096u
#define GDL_STOP_REASON 121u
typedef void (*gdl_move_fn)(void *);
typedef struct {uint64_t tick,calls,passes,keyed,dueset;uint32_t open;} gdl_tally;
static volatile LONG gdl_active;
static int gdl_armed,gdl_installed,gdl_restored,gdl_depth,gdl_admitted;
static uint64_t gdl_seen[GDL_TABLE];
static unsigned gdl_seen_count;
static gdl_tally gdl_now,gdl_last;
static uint64_t gdl_units_active,gdl_units_rebased;
static volatile LONG64 gdl_calls,gdl_passed,gdl_threads,gdl_nested,gdl_fields,gdl_first,gdl_rebased,gdl_due,
    gdl_keyed,gdl_unmapped,gdl_native,gdl_unscoped,gdl_passes,gdl_low,gdl_high,gdl_failures;
static const char *gdl_stage="idle";
/* 1 when the runtime ID was new (now recorded), 0 when known, -1 when the table is full. */
static int gdl_first_move(uint64_t runtime) {
    unsigned h=(unsigned)(lockstep_mix(runtime)&(GDL_TABLE-1));
    for(unsigned n=0;n<GDL_TABLE;n++,h=(h+1)&(GDL_TABLE-1)) {
        if(gdl_seen[h]==runtime) return 0;
        if(!gdl_seen[h]) {
            if(gdl_seen_count>=GDL_TABLE/4*3) return -1;
            gdl_seen[h]=runtime;++gdl_seen_count;return 1;
        }
    }
    return -1;
}
static void gdl_roll(uint64_t tick) {
    if(gdl_now.open && gdl_now.tick==tick) return;
    if(gdl_now.open) gdl_last=gdl_now;
    memset(&gdl_now,0,sizeof(gdl_now));gdl_now.tick=tick;gdl_now.open=1;
}
/* tls = the executing thread's TLS block (slot 0): game time +0x300, word +0x318. */
static void gdl_run(gdl_move_fn original,unsigned char *tls,void *self) {
    InterlockedIncrement64(&gdl_calls);
    if(!InterlockedCompareExchange(&gdl_active,0,0) || !stats.active || boot_phase!=3) {
        InterlockedIncrement64(&gdl_passed);original(self);return;
    }
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&gdl_threads);original(self);return;}
    if(gdl_depth) {InterlockedIncrement64(&gdl_nested);original(self);return;}
    unsigned char *sector=self,field=0,ready=0;uint64_t runtime=0;
    if(!tls || !script_rng_read(tls+GDL_TLS_READY,&ready,1) || !ready || !script_rng_read(sector+8,&runtime,8) || !runtime ||
       !script_rng_read(sector+GDL_FIELD,&field,1)) {
        gdl_stage="read";InterlockedIncrement64(&gdl_failures);original(self);return;
    }
    if(!field) {original(self);return;} /* no factor pass, no draw */
    InterlockedIncrement64(&gdl_fields);
    int first=gdl_first_move(runtime);
    if(first<0) {gdl_stage="table";InterlockedIncrement64(&gdl_failures);original(self);return;}
    double due,now,zero=0.0;int32_t attention=0;
    if(!script_rng_read(sector+GDL_DUE,&due,8) || !script_rng_read(tls+GDL_TLS_TIME,&now,8) ||
       !script_rng_read(sector+GDL_ATTENTION,&attention,4)) {
        gdl_stage="read";InterlockedIncrement64(&gdl_failures);original(self);return;
    }
    if(first) {
        InterlockedIncrement64(&gdl_first);
        if(memcmp(&due,&zero,8)) {memcpy(sector+GDL_DUE,&zero,8);due=0.0;InterlockedIncrement64(&gdl_rebased);}
    }
    uint64_t tick=stats.issued;gdl_roll(tick);++gdl_now.calls;
    if(due>=now) {original(self);return;} /* the native test: comisd due,now; jae -> no pass */
    InterlockedIncrement64(&gdl_due);
    unsigned klass;uint64_t key=script_rng_saved_with(runtime,&klass);
    uint64_t *rng=(uint64_t*)(tls+GDL_TLS_WORD),natural=*rng;int scoped=0;
    if(attention>=5) {InterlockedIncrement64(&gdl_native);original(self);}
    else if(!key) {InterlockedIncrement64(&gdl_unmapped);original(self);}
    else {
        DWORD error=GetLastError();*rng=lockstep_seed(GDL_DOMAIN,tick,key,0,0,0);scoped=1;
        InterlockedIncrement64(&gdl_keyed);++gdl_now.keyed;SetLastError(error);++gdl_depth;
        __try {original(self);}
        __finally {
            error=GetLastError();--gdl_depth;*rng=natural;
            if(AbnormalTermination()) {gdl_stage="unwind";InterlockedIncrement64(&gdl_failures);}
            SetLastError(error);
        }
    }
    double after;
    if(!script_rng_read(sector+GDL_DUE,&after,8)) {gdl_stage="read";InterlockedIncrement64(&gdl_failures);return;}
    if(!memcmp(&after,&due,8)) return;
    InterlockedIncrement64(&gdl_passes);++gdl_now.passes;
    InterlockedIncrement64(after-now>=5.0?&gdl_low:&gdl_high);
    if(after-now>=5.0 && !scoped && key) InterlockedIncrement64(&gdl_unscoped);
    uint64_t bits;memcpy(&bits,&after,8);gdl_now.dueset+=lockstep_mix(key^lockstep_mix(bits));
}
#ifndef GDL_OWN_PLATFORM
/* noinline keeps the TEB read out of the SEH funclet of the scope. */
static __attribute__((noinline)) unsigned char *gdl_tls(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots?(unsigned char*)slots[0]:NULL;
}
#endif
static __attribute__((noinline)) void gdl_move(void *self) {
    gdl_run((gdl_move_fn)(image+GDL_MOVE),gdl_tls(),self);
}
#ifdef GDL_OWN_PLATFORM
static int gdl_pinned(void);
#else
/* Sector::Move (field/deadline/attention layout, the FloatState draw), the
 * slot, the unit constructor's -1 times, the unit apply and the table lookup. */
static int gdl_pinned(void) {
    return hash_ok(image+GDL_MOVE,GDL_MOVE_LENGTH,"f09165f450b152e0f9d4aa10e0375599321552303a81e30106fa73172145c8d4") &&
        hash_ok(image+GDL_UNIT_CTOR,GDL_UNIT_CTOR_LENGTH,"121342d781b4f419cbb4dd624a273bca4c12dd65ba6965f113f4d77239ce83bd") &&
        hash_ok(image+GRAVIDAR_UNIT_APPLY,2262,"a1f8ae33a9b8c9494c231878651d7a146767b3a44b5fd94f651afcafc04ab6df") &&
        hash_ok(image+0x10b2dc0,181,"43ced4fa9f55c0b96d47b6f3252a10fe3fcf3c7d6b9d95042f242ff9d9671840") &&
        *(void**)(image+GDL_SLOT)==(void*)(image+GDL_MOVE);
}
#endif
static int gdl_owned(void) {return !gdl_installed || *(void**)(image+GDL_SLOT)==(void*)gdl_move;}
/* Before load, after native_gate_gravidar_time_sync (idempotent on a loading-UI reload). */
static int gdl_arm(void) {
    if(gdl_armed) return stats.active && gdl_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed ||
       !gravidar_time_sync) return 0;
    if(!gdl_pinned()) {gdl_stage="pins";return 0;}
    memset(gdl_seen,0,sizeof(gdl_seen));gdl_seen_count=0;gdl_admitted=0;
    if(!script_rng_slot_swap(GDL_SLOT,image+GDL_MOVE,(void*)gdl_move)) {gdl_stage="slot";return 0;}
    gdl_installed=1;gdl_armed=1;gdl_stage="armed";InterlockedExchange(&gdl_active,1);return 1;
}
#ifndef GDL_OWN_PLATFORM
/* One unit of the table walk: an active unit's times back to the constructor's. */
static int gdl_unit(unsigned char *unit) {
    static const double unset[3]={-1.0,-1.0,-1.0};void *vtable=NULL;double times[3];
    if(!read_local(unit,&vtable,8)) return 0;
    if(vtable!=(void*)(image+GDIG_ACTIVE)) return 1; /* passive: 0x68 bytes, no scan times */
    ++gdl_units_active;
    EnterCriticalSection((CRITICAL_SECTION*)(unit+0x18));
    int ok=read_local(unit+0xa8,times,24);
    if(ok && memcmp(times,unset,24)) {memcpy(unit+0xa8,unset,24);++gdl_units_rebased;}
    LeaveCriticalSection((CRITICAL_SECTION*)(unit+0x18));
    return ok;
}
/* Boot-phase-3 entry, after the Gravidar boot drain and before frame 1 runs. */
static int gdl_admit(void) {
    if(!gdl_armed || gdl_admitted) return 1;
    unsigned char *worker=*(unsigned char*volatile*)(image+GDIG_WORKER);void *vtable=NULL;
    if(!worker || !read_local(worker,&vtable,8) || vtable!=image+GDIG_VTABLE) {gdl_stage="worker";return 0;}
    EnterCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    unsigned char ready=*(volatile unsigned char*)(worker+0x6988);
    LeaveCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    if(ready!=1) {gdl_stage="busy";return 0;}
    gdl_units_active=gdl_units_rebased=0;
    for(unsigned b=0;b<GDIG_BUCKETS;b++) {
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(worker+0x88+b*0x28);int ok;
        EnterCriticalSection(lock);ok=gdig_tree(worker+0x2890+b*0x40,gdl_unit);LeaveCriticalSection(lock);
        if(!ok) {gdl_stage="units";return 0;}
    }
    gdl_admitted=1;gdl_stage="admitted";return 1;
}
#endif
/* Open scopes still restore their own words; this gives the slot back. */
static void gdl_restore(void) {
    if(!gdl_armed) return;
    InterlockedExchange(&gdl_active,0);
    if(gdl_installed && script_rng_slot_swap(GDL_SLOT,(void*)gdl_move,image+GDL_MOVE)) gdl_installed=0;
    gdl_restored=!gdl_installed;
}
static int gdl_status(char *out,size_t cap) {
    const gdl_tally *t=&gdl_now,*l=&gdl_last;
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld restored=%d calls=%lld passed=%lld threads=%lld nested=%lld fields=%lld"
        " first=%lld rebased=%lld due=%lld keyed=%lld unmapped=%lld native=%lld unscoped=%lld passes=%lld low=%lld high=%lld sectors=%u"
        " cur_tick=%llu cur_calls=%llu cur_passes=%llu cur_keyed=%llu cur_dueset=%016llx"
        " last_tick=%llu last_calls=%llu last_passes=%llu last_keyed=%llu last_dueset=%016llx"
        " admitted=%d units_active=%llu units_rebased=%llu failures=%lld stage=%s",
        gdl_armed,gdl_installed,gdl_active,gdl_restored,(long long)gdl_calls,(long long)gdl_passed,(long long)gdl_threads,
        (long long)gdl_nested,(long long)gdl_fields,(long long)gdl_first,(long long)gdl_rebased,(long long)gdl_due,
        (long long)gdl_keyed,(long long)gdl_unmapped,(long long)gdl_native,(long long)gdl_unscoped,(long long)gdl_passes,
        (long long)gdl_low,(long long)gdl_high,gdl_seen_count,
        (unsigned long long)t->tick,(unsigned long long)t->calls,(unsigned long long)t->passes,(unsigned long long)t->keyed,
        (unsigned long long)t->dueset,(unsigned long long)l->tick,(unsigned long long)l->calls,(unsigned long long)l->passes,
        (unsigned long long)l->keyed,(unsigned long long)l->dueset,gdl_admitted,(unsigned long long)gdl_units_active,
        (unsigned long long)gdl_units_rebased,(long long)gdl_failures,gdl_stage);
    return n>0 && (size_t)n<cap?n:0;
}
