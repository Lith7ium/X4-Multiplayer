/* Load-time MD cue delays on per-cue streams, DEFAULT OFF
 * (native_gate_md_resume_rng).
 *
 * Census F01/F02 (full fix stack): tick 0 is identical (12,376 keyed entries,
 * 178 draws, equal keysets) and every row key is portable, yet from tick 1 the
 * clients run different FactionLogic manager cues: the listeners of
 * Manager_Delay (EvaluateGoals_Wait, CheckEquipmentDocks_Wait) and the delayed
 * EvaluateGoals / CheckEquipmentDocks actions fire at different ticks (F02:
 * Loanshark 18 vs 1, FallenFamilies 1 vs 19, Terran EvaluateGoals 1 vs 23 with
 * 115 draws on both). Every FactionGoal_* difference is downstream of that:
 * the same goal batches run at other ticks or shift the per-tick ordinals.
 *
 * Mechanism (static analysis of X4 9.00): GameClass::Load 0x9ee7a0 -> MD
 * start-up 0x1120600 -> 0x111fd20 -> 0x111ffa0 -> 0x110e830 -> 0x110eb80 ->
 * the recursive cue walk 0x1114030 (likewise 0x1120380 -> 0x1114d60) ->
 * 0x1115840 -> Cue_ResumeAfterLoad 0x1115960 puts every loaded cue back into
 * the MD heap. A cue saved while its <delay> was pending (state 2) does not
 * keep its due time: the call at 0x1115bad evaluates the cue delay again
 * (0x111f8c0 -> 0xc59be0 -> EvaluateRandomRange 0xc58da0: a min/max delay
 * draws the TLS PCG slot0+0x318) and schedules kind 2 at
 * max(now + 1 ms, saved activation time cue+0x1f8 + new delay). No script
 * entry encloses this walk, so the draw takes the natural word, which differs
 * between processes during the load. The snapshot is ~7.08 s into a new game,
 * so the faction managers' Manager_Delay (0..15 s) and EvaluateGoals /
 * CheckEquipmentDocks instance delays are pending: each client re-draws them
 * differently, some become overdue (tick 1), others fall into the first
 * seconds at different ticks.
 *
 * Fix: that one E8 rel32 is redirected (4-byte CAS inside one cache line;
 * the call straddles a 16-byte boundary) through THUNK_JUMP 4032 to
 * md_resume_delay, which evaluates the delay under
 *   lockstep_seed(MD_RESUME_DOMAIN, tick, owner, slot, ordinal, activation)
 *   owner      FNV-1a of the static "module.cue" name (script RNG name cache);
 *              0 when the cue has no name (counted unnamed, still portable)
 *   slot       the delay slot argument (cue+0x200), saved state
 *   activation the saved activation time bits cue+0x1f8
 *   ordinal    count of (owner, slot, activation) in this tick; the cue walk
 *              follows the static tree and the saved instance order
 * and restores the caller's word afterwards (also when unwinding). Pointers
 * and runtime IDs never enter the key. Delays without a range make no draw.
 * Rows keep the natural word the native draw would have used (evidence only).
 * Failures (no TLS word, a non-cue argument, a foreign return address, an
 * unreadable activation time, an exhausted ordinal table, abnormal unwind)
 * stop the gate with reason 87; stop restores the call. */
#define MD_RESUME_DOMAIN 0x58344c4d44524553ULL /* "X4LMDRES" */
/* The Linux gate (linux/gate_md_resume.h) defines these for its executable, and its own patch. */
#ifndef MD_RESUME_OWN_PLATFORM
#define MD_RESUME_CALLER 0x1115960u /* Cue_ResumeAfterLoad */
#define MD_RESUME_SITE 0x1115badu   /* E8 -> MD_RESUME_DELAY */
#define MD_RESUME_DELAY 0x111f8c0u  /* cue delay evaluator: (cue, slot) -> double seconds */
#define MD_RESUME_THUNK 4032u
#endif
#define MD_RESUME_TABLE 4096u
#define MD_RESUME_PROBES 128u
#define MD_RESUME_RING 1024u
typedef double (*md_resume_fn)(void *,uint64_t);
typedef struct {uint64_t tick,owner,slot,activation;uint32_t count,used;} md_resume_slot_t;
typedef struct {
    uint64_t tick,owner,slot,activation,ordinal,key,natural,exit,delay;
    uint32_t draws;uint8_t unnamed,thread;char name[64];
} md_resume_row_t;
static volatile LONG md_resume_active;
static int md_resume_armed,md_resume_installed,md_resume_restored;
static int32_t md_resume_original_rel,md_resume_patched_rel;
static md_resume_slot_t md_resume_slots[MD_RESUME_TABLE];
static md_resume_row_t md_resume_ring[MD_RESUME_RING];
static uint64_t md_resume_slots_tick=UINT64_MAX,md_resume_draws,md_resume_keyset,md_resume_drawset,md_resume_delayset;
static volatile LONG64 md_resume_calls,md_resume_keyed,md_resume_passed,md_resume_unnamed,md_resume_threads,md_resume_foreign,
    md_resume_table_full,md_resume_draws_unknown,md_resume_cursor,md_resume_failures;

/* Per-tick (owner, slot, activation) ordinal; 0 when the bounded table is exhausted. */
static uint64_t md_resume_ordinal(uint64_t tick,uint64_t owner,uint64_t slot,uint64_t activation) {
    if(md_resume_slots_tick!=tick) {memset(md_resume_slots,0,sizeof(md_resume_slots));md_resume_slots_tick=tick;}
    unsigned first=(unsigned)(lockstep_mix(owner^lockstep_mix(slot^lockstep_mix(activation)))&(MD_RESUME_TABLE-1));
    for(unsigned n=0;n<MD_RESUME_PROBES;n++) {
        md_resume_slot_t *s=&md_resume_slots[(first+n)&(MD_RESUME_TABLE-1)];
        if(!s->used) {s->used=1;s->tick=tick;s->owner=owner;s->slot=slot;s->activation=activation;s->count=1;return 1;}
        if(s->owner==owner && s->slot==slot && s->activation==activation) return ++s->count;
    }
    return 0;
}
static void md_resume_record(const script_rng_identity *id,uint64_t tick,uint64_t slot,uint64_t activation,uint64_t ordinal,
                             uint64_t key,uint64_t natural,uint64_t exit,double delay,int unnamed,int thread) {
    uint32_t draws=script_rng_steps(key,exit);uint64_t bits;memcpy(&bits,&delay,8);
    if(draws==UINT32_MAX) InterlockedIncrement64(&md_resume_draws_unknown);else md_resume_draws+=draws;
    md_resume_keyset+=lockstep_mix(key);md_resume_drawset+=lockstep_mix(key^((uint64_t)draws*0x9e3779b97f4a7c15ULL));
    md_resume_delayset+=lockstep_mix(key^lockstep_mix(bits));
    LONG64 index=InterlockedIncrement64(&md_resume_cursor)-1;
    md_resume_row_t *r=&md_resume_ring[(uint64_t)index%MD_RESUME_RING];
    r->tick=tick;r->owner=id->owner;r->slot=slot;r->activation=activation;r->ordinal=ordinal;r->key=key;r->natural=natural;
    r->exit=exit;r->delay=bits;r->draws=draws;r->unnamed=(uint8_t)unnamed;r->thread=(uint8_t)thread;memcpy(r->name,id->name,sizeof(r->name));
}
/* back = the native return address; rng = the executing thread's TLS word. */
static double md_resume_run(uintptr_t back,md_resume_fn original,uint64_t *rng,void *cue,uint64_t slot) {
    InterlockedIncrement64(&md_resume_calls);
    if(back!=(uintptr_t)(image+MD_RESUME_SITE+5)) {
        InterlockedIncrement64(&md_resume_foreign);InterlockedIncrement64(&md_resume_failures);return original(cue,slot);
    }
    if(!InterlockedCompareExchange(&md_resume_active,0,0) || !stats.active) {InterlockedIncrement64(&md_resume_passed);return original(cue,slot);}
    script_rng_identity id;memset(&id,0,sizeof(id));id.kind=SCRIPT_RNG_MD;id.local=(uintptr_t)cue;
    unsigned why=rng?script_rng_md(&id):SR_UNREADABLE;uint64_t activation=0;
    if((why!=SR_OK && why!=SR_UNNAMED) || !read_local((unsigned char*)cue+0x1f8,&activation,8)) {
        InterlockedIncrement64(&md_resume_failures);return original(cue,slot);
    }
    int unnamed=why==SR_UNNAMED,thread=GetCurrentThreadId()!=ui_thread;
    if(unnamed) {id.owner=0;id.name[0]=0;InterlockedIncrement64(&md_resume_unnamed);}
    if(thread) InterlockedIncrement64(&md_resume_threads);
    uint64_t tick=stats.issued,ordinal=md_resume_ordinal(tick,id.owner,slot,activation);
    if(!ordinal) {InterlockedIncrement64(&md_resume_table_full);InterlockedIncrement64(&md_resume_failures);return original(cue,slot);}
    uint64_t key=lockstep_seed(MD_RESUME_DOMAIN,tick,id.owner,slot,ordinal,activation),natural=*rng,exit=natural;double delay=0;
    DWORD error=GetLastError();*rng=key;InterlockedIncrement64(&md_resume_keyed);SetLastError(error);
    __try {delay=original(cue,slot);}
    __finally {
        error=GetLastError();exit=*rng;*rng=natural;
        if(AbnormalTermination()) InterlockedIncrement64(&md_resume_failures);
        else md_resume_record(&id,tick,slot,activation,ordinal,key,natural,exit,delay,unnamed,thread);
        SetLastError(error);
    }
    return delay;
}
/* noinline: the return address is the native caller's (the thunk jumps). */
static __attribute__((noinline)) double md_resume_delay(void *cue,uint64_t slot) {
    return md_resume_run((uintptr_t)__builtin_return_address(0),(md_resume_fn)(image+MD_RESUME_DELAY),interrupt_rng(),cue,slot);
}
#ifndef MD_RESUME_OWN_PLATFORM
/* The caller with the call site, the evaluator, and the cue-name layout. */
static int md_resume_pinned(void) {
    return npc_layout_pinned() &&
        hash_ok(image+0x1111a40,905,"bde488a4463be31fa0a9d096c61c1b5922d2b1e3191a939bf11302905e688ee4") &&
        hash_ok(image+MD_RESUME_CALLER,716,"13f50df874238bf75f8b6ed321847b42ef380e507775f419843ede0aefc93439") &&
        hash_ok(image+MD_RESUME_DELAY,204,"4ef0d708fd22c34127043a5c56f56315650175e786359ee323448e75414afd56");
}
/* The rel32 at site+1 is unaligned but inside one 64-byte line: one locked CAS. */
static int md_resume_swap(int32_t expected,int32_t value) {
    unsigned char *rel=image+MD_RESUME_SITE+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+MD_RESUME_SITE,5);
    return actual==expected;
}
/* Original and patched rel32: E8 to the evaluator becomes E8 to the thunk. */
static int md_resume_prepare(void) {
    unsigned char call[5];int32_t rel;
    if(!read_local(image+MD_RESUME_SITE,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);if((uintptr_t)(image+MD_RESUME_SITE+5)+(intptr_t)rel!=(uintptr_t)(image+MD_RESUME_DELAY)) return 0;
    intptr_t distance=(intptr_t)(thunk+MD_RESUME_THUNK)-(intptr_t)(image+MD_RESUME_SITE+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    md_resume_original_rel=rel;md_resume_patched_rel=(int32_t)distance;return 1;
}
static int md_resume_owned(void) {
    int32_t rel=0;if(!md_resume_installed) return 1;
    memcpy(&rel,image+MD_RESUME_SITE+1,4);return rel==md_resume_patched_rel;
}
/* Armed once at boot, before load (idempotent on a loading-UI reload). */
static int md_resume_arm(void) {
    if(md_resume_armed) return stats.active && md_resume_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !md_resume_pinned() || !thunk_slot_ready(MD_RESUME_THUNK) || !md_resume_prepare() ||
       !md_resume_swap(md_resume_original_rel,md_resume_patched_rel)) return 0;
    md_resume_installed=1;md_resume_armed=1;InterlockedExchange(&md_resume_active,1);return 1;
}
/* Open scopes still restore their own words; this gives the call back. */
static void md_resume_restore(void) {
    if(!md_resume_armed) return;
    InterlockedExchange(&md_resume_active,0);
    if(md_resume_installed && md_resume_swap(md_resume_patched_rel,md_resume_original_rel)) md_resume_installed=0;
    md_resume_restored=!md_resume_installed;
}
#endif
static int md_resume_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&md_resume_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)MD_RESUME_RING?cursor-(LONG64)MD_RESUME_RING:0;
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld restored=%d calls=%lld keyed=%lld passed=%lld unnamed=%lld threads=%lld"
        " foreign=%lld table_full=%lld draws=%llu draws_unknown=%lld keyset=%016llx drawset=%016llx delayset=%016llx"
        " records=%lld dropped=%lld ring=%u failures=%lld",
        md_resume_armed,md_resume_installed,md_resume_active,md_resume_restored,(long long)md_resume_calls,(long long)md_resume_keyed,
        (long long)md_resume_passed,(long long)md_resume_unnamed,(long long)md_resume_threads,(long long)md_resume_foreign,
        (long long)md_resume_table_full,(unsigned long long)md_resume_draws,(long long)md_resume_draws_unknown,
        (unsigned long long)md_resume_keyset,(unsigned long long)md_resume_drawset,(unsigned long long)md_resume_delayset,
        (long long)cursor,(long long)dropped,MD_RESUME_RING,(long long)md_resume_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per call; evicted rows report evicted=1 so the probe keeps going. */
static int md_resume_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&md_resume_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)MD_RESUME_RING && index<(uint64_t)(cursor-(LONG64)MD_RESUME_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const md_resume_row_t *r=&md_resume_ring[index%MD_RESUME_RING];char name[sizeof(r->name)];
        memcpy(name,r->name,sizeof(name));name[sizeof(name)-1]=0;
        for(size_t i=0;name[i];i++) if((unsigned char)name[i]<=32 || (unsigned char)name[i]>126 || name[i]=='|') name[i]='_';
        n=snprintf(out,cap,"index=%llu tick=%llu owner=%016llx slot=%llu activation=%016llx ord=%llu key=%016llx natural=%016llx"
            " exit=%016llx draws=%lld delay=%016llx unnamed=%u thread=%u name=%s",
            (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->owner,(unsigned long long)r->slot,
            (unsigned long long)r->activation,(unsigned long long)r->ordinal,(unsigned long long)r->key,(unsigned long long)r->natural,
            (unsigned long long)r->exit,r->draws==UINT32_MAX?-1LL:(long long)r->draws,(unsigned long long)r->delay,r->unnamed,r->thread,
            name[0]?name:"-");
    }
    return n>0 && (size_t)n<cap?n:0;
}
