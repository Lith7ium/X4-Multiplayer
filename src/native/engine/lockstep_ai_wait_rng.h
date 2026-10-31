/* Load-time AI Wait durations on per-wait streams, DEFAULT OFF
 * (native_gate_ai_wait_rng).
 *
 * Census V01 (full fix stack including the MD resume keys): loading and tick
 * 1 are identical, yet about 18 saved pilots run move.idle PC71,
 * move.generic PC345 or fight.attack.object.fighter PC201 in ticks 1-4 on one
 * client only, and the same script position many ticks later on the other
 * (80d54 move.idle PC71: tick 4 vs 1; 11fd90 fighter PC201: tick 1 vs 24).
 * The selected-Wait observer rows of the same run show the cause for 80d54:
 * both clients restore its saved Wait (slot 70, saved start 6.91667) during
 * the load, from different natural words, and get deadlines 7.1448 and
 * 7.0616 on either side of the load clock 7.083.
 *
 * Mechanism (static analysis of X4 9.00): AIDirector_Import 0x1069000 puts
 * every saved script context back. For an Exec saved while blocked
 * (exec+0x30) it takes the action node at the saved program slot (exec+0x20,
 * from the saved blocking index via 0xb5a060), checks its blocking predicate
 * (vtable+8) and calls its Reattach (vtable+0x28) at 0x1069bf9, return
 * 0x1069bfc. WaitAction::Reattach 0x10a50d0 has no saved deadline: when the
 * wait has a duration expression (self+0x80) it evaluates it again
 * (self+0x78 -> 0xc59be0 -> EvaluateRandomRange 0xc58da0; min/max without a
 * seed attribute draws the TLS PCG slot0+0x318) and adds the saved action
 * start time exec+0x28 (WaitAction::Execute 0x10a5040 adds the current TLS
 * game time +0x300 instead); 0x109a490 -> AIScriptContext_SetTimedWake
 * 0x10668b0 installs it with reason 2 unless an earlier deadline is pending.
 * A wait without a duration installs no timed wake (-1). No script entry
 * encloses the importer, so the draw takes the loading thread's natural word,
 * which differs between processes. The other Reattach methods (MoveTo,
 * Docking, Gate, TargetPoints, WayPoints, ApproachPath, DockMasstrafficDrone,
 * DetachFromMassTraffic, WaitForPrevScript, ExecuteCustomTrade/Trade) reach
 * no RNG function through direct calls (depth 6); they re-evaluate target
 * expressions through virtual calls and are not keyed here ("pre" below
 * measures any draw made between two Wait restores).
 *
 * Fix: rides on the Wait observer's entry hook (0x10a50d0, thunk 2752..2815;
 * installed here with the same CAS when the observer is not configured).
 * Every importer call (return address 0x1069bfc) runs under
 *   lockstep_seed(AI_WAIT_DOMAIN, tick, owner, stream, ordinal, start)
 *   owner    saved UniverseID of the context entity (world identity map)
 *   stream   script name hash, saved script version, program slot and PC
 *   start    the saved action start time bits exec+0x28
 *   ordinal  count of (owner, stream, start) in this tick (save order)
 * and restores the caller's word afterwards (also when unwinding). Pointers
 * and runtime IDs never enter the key. Unmapped owners are counted and keep
 * the natural word; foreign callers pass. Rows keep the natural word
 * (evidence only), the draws, the resulting deadline context+0x190 and "pre",
 * the generator steps the loading thread made since the previous Wait restore
 * of this tick. Failures (no TLS word, unreadable importer arguments, an
 * exhausted ordinal table, abnormal unwind) stop the gate with reason 88;
 * stop gives the entry back through world_wait_restore. Exclusive with the
 * selected-Wait RNG experiment (world_wait_rng_config). */
#define AI_WAIT_DOMAIN 0x58344c4149574149ULL /* "X4LAIWAI" */
#define AI_WAIT_TABLE 32768u
#define AI_WAIT_PROBES 256u
#define AI_WAIT_RING 32768u
#define AI_WAIT_NAMES 1024u
typedef struct {uint64_t tick,owner,stream,start;uint32_t count,used;} ai_wait_slot_t;
typedef struct {uint64_t hash;char name[64];} ai_wait_name_t;
typedef struct {
    uint64_t tick,owner,script,slot,pc,start,ordinal,key,natural,exit,due;
    uint32_t version,draws,pre;uint8_t thread;
} ai_wait_row_t;
static volatile LONG ai_wait_active;
static int ai_wait_armed,ai_wait_restored,ai_wait_shared;
static ai_wait_slot_t ai_wait_slots[AI_WAIT_TABLE];
static ai_wait_name_t ai_wait_names[AI_WAIT_NAMES];
static ai_wait_row_t ai_wait_ring[AI_WAIT_RING];
static uint64_t ai_wait_slots_tick=UINT64_MAX,ai_wait_last_tick=UINT64_MAX,ai_wait_last,ai_wait_draws,ai_wait_keyset,ai_wait_drawset,
    ai_wait_dueset,ai_wait_gap_draws;
static DWORD ai_wait_last_thread;
static volatile LONG64 ai_wait_calls,ai_wait_keyed,ai_wait_passed,ai_wait_foreign,ai_wait_unmapped,ai_wait_threads,ai_wait_table_full,
    ai_wait_nodraw,ai_wait_draws_unknown,ai_wait_gaps,ai_wait_gap_unknown,ai_wait_cursor,ai_wait_failures;
/* The loading thread's TLS word; a seam for the offline tests. */
static uint64_t *(*ai_wait_word)(void)=interrupt_rng;

/* Per-tick (owner, stream, start) ordinal; 0 when the bounded table is exhausted. */
static uint64_t ai_wait_ordinal(uint64_t tick,uint64_t owner,uint64_t stream,uint64_t start) {
    if(ai_wait_slots_tick!=tick) {memset(ai_wait_slots,0,sizeof(ai_wait_slots));ai_wait_slots_tick=tick;}
    unsigned first=(unsigned)(lockstep_mix(owner^lockstep_mix(stream^lockstep_mix(start)))&(AI_WAIT_TABLE-1));
    for(unsigned n=0;n<AI_WAIT_PROBES;n++) {
        ai_wait_slot_t *s=&ai_wait_slots[(first+n)&(AI_WAIT_TABLE-1)];
        if(!s->used) {s->used=1;s->tick=tick;s->owner=owner;s->stream=stream;s->start=start;s->count=1;return 1;}
        if(s->owner==owner && s->stream==stream && s->start==start) return ++s->count;
    }
    return 0;
}
static uint64_t ai_wait_stream(uint64_t script,unsigned version,uint64_t slot,uint64_t pc) {
    return lockstep_mix(script^lockstep_mix((uint64_t)version^lockstep_mix(slot^lockstep_mix(pc))));
}
/* Generator steps the thread made since its previous Wait restore in this tick
 * (0 for the first); every restore leaves the caller's word as it found it. */
static uint32_t ai_wait_gap(uint64_t tick,uint64_t natural) {
    DWORD thread=GetCurrentThreadId();uint32_t steps=0;
    if(ai_wait_last_tick==tick && ai_wait_last_thread==thread && ai_wait_last!=natural) {
        steps=script_rng_steps(ai_wait_last,natural);InterlockedIncrement64(&ai_wait_gaps);
        if(steps==UINT32_MAX) InterlockedIncrement64(&ai_wait_gap_unknown);else ai_wait_gap_draws+=steps;
    }
    ai_wait_last_tick=tick;ai_wait_last_thread=thread;ai_wait_last=natural;return steps;
}
/* Script names by hash for the rows (first writer wins; rows keep the hash). */
static void ai_wait_name_put(uint64_t hash,const char *name) {
    unsigned first=(unsigned)(lockstep_mix(hash)&(AI_WAIT_NAMES-1));
    for(unsigned n=0;n<AI_WAIT_NAMES;n++) {
        ai_wait_name_t *e=&ai_wait_names[(first+n)&(AI_WAIT_NAMES-1)];
        if(e->hash==hash) return;
        if(!e->hash) {size_t len=strlen(name);if(len>=sizeof(e->name)) len=sizeof(e->name)-1;memcpy(e->name,name,len);e->name[len]=0;e->hash=hash;return;}
    }
}
static const char *ai_wait_name_get(uint64_t hash) {
    unsigned first=(unsigned)(lockstep_mix(hash)&(AI_WAIT_NAMES-1));
    for(unsigned n=0;n<AI_WAIT_NAMES && hash;n++) {
        const ai_wait_name_t *e=&ai_wait_names[(first+n)&(AI_WAIT_NAMES-1)];
        if(e->hash==hash) return e->name;
        if(!e->hash) break;
    }
    return NULL;
}
static void ai_wait_record(const world_wait_guard *g,uint64_t script,uint64_t tick,uint64_t ordinal,uint64_t key,
                           uint64_t natural,uint64_t exit,uint32_t pre,int thread) {
    uint32_t draws=script_rng_steps(key,exit);uint64_t due=0;
    if(!read_local((void*)(g->context+0x190),&due,8)) InterlockedIncrement64(&ai_wait_failures);
    if(draws==UINT32_MAX) InterlockedIncrement64(&ai_wait_draws_unknown);
    else {ai_wait_draws+=draws;if(!draws) InterlockedIncrement64(&ai_wait_nodraw);}
    ai_wait_keyset+=lockstep_mix(key);ai_wait_drawset+=lockstep_mix(key^((uint64_t)draws*0x9e3779b97f4a7c15ULL));
    ai_wait_dueset+=lockstep_mix(key^lockstep_mix(due));ai_wait_name_put(script,g->name);
    LONG64 index=InterlockedIncrement64(&ai_wait_cursor)-1;
    ai_wait_row_t *r=&ai_wait_ring[(uint64_t)index%AI_WAIT_RING];
    r->tick=tick;r->owner=g->saved;r->script=script;r->slot=g->slot;r->pc=g->pc;r->start=g->time;r->ordinal=ordinal;r->key=key;
    r->natural=natural;r->exit=exit;r->due=due;r->version=g->version;r->draws=draws;r->pre=pre;r->thread=(uint8_t)thread;
}
/* caller = the native return address; rng = the executing thread's TLS word. */
static unsigned char ai_wait_run(uintptr_t caller,world_wait_fn original,uint64_t *rng,void *self,void *exec,void *value) {
    InterlockedIncrement64(&ai_wait_calls);
    if(!InterlockedCompareExchange(&ai_wait_active,0,0) || !stats.active) {InterlockedIncrement64(&ai_wait_passed);return original(self,exec,value);}
    if(caller!=(uintptr_t)(image+WORLD_WAIT_CALLER)) {InterlockedIncrement64(&ai_wait_foreign);return original(self,exec,value);}
    world_wait_guard g;
    if(!rng || !world_wait_guard_read(self,exec,value,&g)) {InterlockedIncrement64(&ai_wait_failures);return original(self,exec,value);}
    uint64_t tick=stats.issued;
    if(!g.saved) {
        /* Never a fabricated key; the gap trace continues behind its natural draws. */
        InterlockedIncrement64(&ai_wait_unmapped);ai_wait_gap(tick,*rng);unsigned char result=original(self,exec,value);
        ai_wait_last=*rng;return result;
    }
    int thread=GetCurrentThreadId()!=ui_thread;if(thread) InterlockedIncrement64(&ai_wait_threads);
    uint64_t script=world_wait_rng_hash(g.name),stream=ai_wait_stream(script,g.version,g.slot,g.pc),ordinal=ai_wait_ordinal(tick,g.saved,stream,g.time);
    if(!ordinal) {InterlockedIncrement64(&ai_wait_table_full);InterlockedIncrement64(&ai_wait_failures);return original(self,exec,value);}
    uint64_t natural=*rng,exit=natural,key=lockstep_seed(AI_WAIT_DOMAIN,tick,g.saved,stream,ordinal,g.time);
    uint32_t pre=ai_wait_gap(tick,natural);unsigned char result=0;
    DWORD error=GetLastError();*rng=key;InterlockedIncrement64(&ai_wait_keyed);SetLastError(error);
    __try {result=original(self,exec,value);}
    __finally {
        error=GetLastError();exit=*rng;*rng=natural;
        if(AbnormalTermination()) InterlockedIncrement64(&ai_wait_failures);
        else ai_wait_record(&g,script,tick,ordinal,key,natural,exit,pre,thread);
        SetLastError(error);
    }
    return result;
}
#ifndef AI_WAIT_OWN_PLATFORM /* Linux swaps the vtable slot instead (linux/gate_ai_wait.h). */
/* Inside the Wait observer, around its one original call. */
static unsigned char ai_wait_call(world_wait_fn original,void *self,void *exec,void *value,uintptr_t caller) {
    if(!InterlockedCompareExchange(&ai_wait_active,0,0)) return original(self,exec,value);
    return ai_wait_run(caller,original,ai_wait_word(),self,exec,value);
}
/* The entry patch helpers follow in lockstep_world_wait.h. */
static int world_wait_swap(uint64_t expected,uint64_t value);
static int world_wait_thunk_matches(void);
static int world_wait_pins(void);
static int ai_wait_entry_owned(void) {
    uint64_t entry=0;return world_wait_installed && read_local(image+WORLD_WAIT_ENTRY,&entry,8) && entry==world_wait_patched;
}
static int ai_wait_owned(void) {return !ai_wait_armed || !world_wait_installed || ai_wait_entry_owned();}
/* The observer's own entry patch (aligned 8-byte CAS, E9 to thunk+2784), under its lock. */
static int ai_wait_install(void) {
    intptr_t distance=(intptr_t)(thunk+2784)-(intptr_t)(image+WORLD_WAIT_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    uint64_t original,patched;memcpy(&original,image+WORLD_WAIT_ENTRY,8);patched=original;
    int32_t relative=(int32_t)distance;((unsigned char*)&patched)[0]=0xe9;memcpy((unsigned char*)&patched+1,&relative,4);
    AcquireSRWLockExclusive(&world_wait_lock);
    int ok=stats.active && !world_wait_restoring && !world_wait_installed && world_wait_swap(original,patched);
    if(ok) {world_wait_original=original;world_wait_patched=patched;world_wait_installed=1;}
    ReleaseSRWLockExclusive(&world_wait_lock);return ok;
}
/* Armed once at boot, before load (idempotent on a loading-UI reload). A Wait
 * observer configured first keeps its pinned installation; otherwise the full
 * importer/Wait/range/RNG pins are checked here before installing. */
static int ai_wait_arm(void) {
    if(ai_wait_armed) return stats.active && InterlockedCompareExchange(&ai_wait_active,0,0) && ai_wait_entry_owned();
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !world_identity_installed || InterlockedCompareExchange(&world_wait_rng_started,0,0) || world_wait_restoring ||
       !world_wait_thunk_matches()) return 0;
    if(world_wait_installed) {if(!ai_wait_entry_owned()) return 0;ai_wait_shared=1;}
    else if(!world_wait_pins() || !ai_wait_install()) return 0;
    ai_wait_armed=1;InterlockedExchange(&ai_wait_active,1);return 1;
}
/* After world_wait_restore: open scopes restore their own words. */
static void ai_wait_restore(void) {
    if(!ai_wait_armed) return;
    InterlockedExchange(&ai_wait_active,0);ai_wait_restored=!world_wait_installed;
}
#endif
static int ai_wait_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&ai_wait_cursor,0,0);
    LONG64 dropped=cursor>(LONG64)AI_WAIT_RING?cursor-(LONG64)AI_WAIT_RING:0;
    int n=snprintf(out,cap,"armed=%d installed=%d shared=%d active=%ld restored=%d calls=%lld keyed=%lld passed=%lld foreign=%lld"
        " unmapped=%lld threads=%lld table_full=%lld nodraw=%lld draws=%llu draws_unknown=%lld keyset=%016llx drawset=%016llx"
        " dueset=%016llx gaps=%lld gap_draws=%llu gap_unknown=%lld records=%lld dropped=%lld ring=%u failures=%lld",
        ai_wait_armed,ai_wait_armed && world_wait_installed,ai_wait_shared,ai_wait_active,ai_wait_restored,(long long)ai_wait_calls,
        (long long)ai_wait_keyed,(long long)ai_wait_passed,(long long)ai_wait_foreign,(long long)ai_wait_unmapped,
        (long long)ai_wait_threads,(long long)ai_wait_table_full,(long long)ai_wait_nodraw,(unsigned long long)ai_wait_draws,
        (long long)ai_wait_draws_unknown,(unsigned long long)ai_wait_keyset,(unsigned long long)ai_wait_drawset,
        (unsigned long long)ai_wait_dueset,(long long)ai_wait_gaps,(unsigned long long)ai_wait_gap_draws,(long long)ai_wait_gap_unknown,
        (long long)cursor,(long long)dropped,AI_WAIT_RING,(long long)ai_wait_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* One census row per keyed restore; evicted rows report evicted=1 so the probe keeps going. */
static int ai_wait_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&ai_wait_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)AI_WAIT_RING && index<(uint64_t)(cursor-(LONG64)AI_WAIT_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const ai_wait_row_t *r=&ai_wait_ring[index%AI_WAIT_RING];const char *known=ai_wait_name_get(r->script);char name[64];
        snprintf(name,sizeof(name),"%s",known?known:"");
        for(size_t i=0;name[i];i++) if((unsigned char)name[i]<=32 || (unsigned char)name[i]>126 || name[i]=='|') name[i]='_';
        n=snprintf(out,cap,"index=%llu tick=%llu owner=%016llx script=%016llx version=%u slot=%llu pc=%llu start=%016llx ord=%llu"
            " key=%016llx natural=%016llx exit=%016llx draws=%lld due=%016llx pre=%lld thread=%u name=%s",
            (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->owner,(unsigned long long)r->script,r->version,
            (unsigned long long)r->slot,(unsigned long long)r->pc,(unsigned long long)r->start,(unsigned long long)r->ordinal,
            (unsigned long long)r->key,(unsigned long long)r->natural,(unsigned long long)r->exit,
            r->draws==UINT32_MAX?-1LL:(long long)r->draws,(unsigned long long)r->due,r->pre==UINT32_MAX?-1LL:(long long)r->pre,
            r->thread,name[0]?name:"-");
    }
    return n>0 && (size_t)n<cap?n:0;
}
