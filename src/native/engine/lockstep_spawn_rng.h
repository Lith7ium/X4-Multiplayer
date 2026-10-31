/* Ship loadouts from the caller's explicit generator state, DEFAULT OFF:
 * native_gate_spawn_rng ('key' / 'observe').
 *
 * long-L6 (native_gate_created_load): the about 20 ships the load creates
 * (366 vs 367 tick-0 created IDs, digests differ) have the same macros on both
 * clients but different equipment (engines, shields, turret types). They are
 * job ships: the job spawn 0xf06650 (U::JobSpawnSource) calls
 * U::ShipGenerator::Generate 0xc841b0 with the job's own generator state as
 * its 7th argument (job+0x528 -> state; JobDB seeds it at import as FNV-1a of
 * the job ID xor the session seed, 0xef8fe7), and so do god spawns
 * (GodShipEntry::SpawnShip, entry+0x170+8) and MD/AI create_ship (the
 * EvaluateSeedAttribute result). Generate hands that pointer to the macro
 * choice and to InitializeShip 0xc82060, but InitializeShip's loadout
 * generator 0x50e4a0 (both call sites and the tail of 0xc7e740) draws from the
 * calling thread's TLS word (gs:[0x58] slot+0x318: Random::FloatState and
 * inline PCG steps). During the load that word comes from
 * Random::SeedThreadState (_time64 + thread ID), so every load-spawned ship
 * gets its own equipment per client; after the seal it is the natural or the
 * script-keyed sim word.
 *
 * With 'key' each Generate call whose explicit state pointer is readable runs
 * with the calling thread's TLS word set to lockstep_seed(domain, *state):
 * the loadout then follows the same deterministic input as the macro choice.
 * The caller's word is restored afterwards (also on unwinding), so draws
 * outside Generate are untouched. A null or unreadable state keeps the
 * native word (counted). 'observe' derives the same key without writing.
 * Every scope leaves a SPAWN_RNG_ROW (tick, boot phase, UI thread, explicit
 * state, key, the caller's word, the word Generate started on and ended
 * with, steps). Hook: the Generate entry word (5-byte prefix, thunk slot
 * 3072, shared with the creation trace, which refuses while this is
 * installed and vice versa). Pins: Generate, InitializeShip and the loadout
 * generator bodies. Failures stop the gate with reason 110 (98 is the camera offset harness). */
/* The Linux gate (linux/gate_spawn_rng.h) has its own entry, hook and arming. */
#ifndef SPAWN_RNG_OWN_PLATFORM
#define SPAWN_RNG_ENTRY 0xc841b0u
#define SPAWN_RNG_THUNK 3072u
#endif
#define SPAWN_RNG_DOMAIN 0x58344c5350574e31ULL /* "X4LSPWN1" */
#define SPAWN_RNG_RING 1024u
#define SPAWN_RNG_STOP_REASON 110u
enum {SPAWN_RNG_OBSERVE=0,SPAWN_RNG_KEY=1};
typedef struct {uint64_t *word;uint64_t saved,state,key,entry,tick;unsigned phase,ui,active;} spawn_rng_scope;
typedef struct {uint64_t tick,state,key,caller,entry,exit;uint32_t draws,phase,ui,normal,mode;} spawn_rng_row_t;
static volatile LONG spawn_rng_active;
static int spawn_rng_armed,spawn_rng_installed,spawn_rng_restored,spawn_rng_mode;
static uint64_t spawn_rng_original_word,spawn_rng_patched_word;
static volatile LONG64 spawn_rng_calls,spawn_rng_keyed,spawn_rng_null,spawn_rng_unreadable,spawn_rng_failures,spawn_rng_cursor;
static volatile LONG64 spawn_rng_boot_calls;
static spawn_rng_row_t spawn_rng_ring[SPAWN_RNG_RING];
static uint64_t *(*spawn_rng_word)(void)=interrupt_rng; /* the calling thread's TLS word; replaceable by the offline tests */

static int spawn_rng_begin(uintptr_t state_pointer,spawn_rng_scope *s) {
    memset(s,0,sizeof(*s));
    if(!InterlockedCompareExchange(&spawn_rng_active,0,0)) return 0;
    InterlockedIncrement64(&spawn_rng_calls);if(boot_phase<3) InterlockedIncrement64(&spawn_rng_boot_calls);
    if(!state_pointer) {InterlockedIncrement64(&spawn_rng_null);return 0;}
    uint64_t state=0,*word=spawn_rng_word();
    if(!script_rng_read((const void*)state_pointer,&state,8)) {InterlockedIncrement64(&spawn_rng_unreadable);return 0;}
    if(!word) {InterlockedIncrement64(&spawn_rng_failures);return 0;}
    s->word=word;s->saved=*word;s->state=state;s->key=lockstep_seed(SPAWN_RNG_DOMAIN,state,0,0,0,0);
    s->tick=stats.issued;s->phase=(unsigned)boot_phase;s->ui=GetCurrentThreadId()==ui_thread;
    if(spawn_rng_mode==SPAWN_RNG_KEY) {*word=s->key;InterlockedIncrement64(&spawn_rng_keyed);}
    s->entry=*word;s->active=1;return 1;
}
/* Always restores a keyed scope's caller word, including unwinding. */
static void spawn_rng_end(spawn_rng_scope *s,int normal) {
    if(!s->active) return;
    uint64_t exit=*s->word;
    if(spawn_rng_mode==SPAWN_RNG_KEY) *s->word=s->saved;
    if(!normal) InterlockedIncrement64(&spawn_rng_failures);
    LONG64 index=InterlockedIncrement64(&spawn_rng_cursor)-1;
    spawn_rng_row_t *r=&spawn_rng_ring[(uint64_t)index%SPAWN_RNG_RING];
    *r=(spawn_rng_row_t){s->tick,s->state,s->key,s->saved,s->entry,exit,script_rng_steps(s->entry,exit),s->phase,s->ui,
        (uint32_t)(normal!=0),(uint32_t)spawn_rng_mode};
    s->active=0;
}

#ifndef SPAWN_RNG_OWN_PLATFORM
static int spawn_rng_pins(void) {
    static const unsigned char entry[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74};
    return hash_ok(image+SPAWN_RNG_ENTRY,1090,"ed86616b763698739cd1635d6de0e430f804971eb10f63e9c1ae78e856b7cb5e") &&
        hash_ok(image+0xc82060,3073,"fbfd2881e3e95f5225c74c2646046cf2ca3b7f27f9c9f3f77fb3e37becdf78ba") &&
        hash_ok(image+0x50e4a0,4119,"bb77fd0b832098a6478588b40f756ee84ab6c6a9af321826c742900ae428761c") &&
        !memcmp(image+SPAWN_RNG_ENTRY,entry,sizeof(entry));
}
static int spawn_rng_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+SPAWN_RNG_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+SPAWN_RNG_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+SPAWN_RNG_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+SPAWN_RNG_ENTRY,8);
    return actual==expected;
}
static int spawn_rng_owned(void) {
    return !spawn_rng_installed || *(volatile uint64_t*)(image+SPAWN_RNG_ENTRY)==spawn_rng_patched_word;
}
static volatile LONG creation_started; /* lockstep_creation_trace.h: the other owner of this entry */
/* Armed before loading, so the load's job spawns are covered; a loading-UI
 * reload that reruns the configuration with the same mode is accepted. */
static int spawn_rng_arm(int mode) {
    mode=mode==SPAWN_RNG_KEY?SPAWN_RNG_KEY:SPAWN_RNG_OBSERVE;
    if(spawn_rng_armed) return stats.active && spawn_rng_installed && spawn_rng_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       InterlockedCompareExchange(&creation_started,0,0) || !spawn_rng_pins() || !thunk_slot_ready(SPAWN_RNG_THUNK)) return 0;
    intptr_t distance=(intptr_t)(thunk+SPAWN_RNG_THUNK+32)-(intptr_t)(image+SPAWN_RNG_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&spawn_rng_original_word,image+SPAWN_RNG_ENTRY,8);spawn_rng_patched_word=spawn_rng_original_word;
    unsigned char *patch=(unsigned char*)&spawn_rng_patched_word;int32_t rel=(int32_t)distance;patch[0]=0xe9;memcpy(patch+1,&rel,4);
    spawn_rng_mode=mode;
    if(!spawn_rng_swap(spawn_rng_original_word,spawn_rng_patched_word)) return 0;
    spawn_rng_installed=1;spawn_rng_armed=1;InterlockedExchange(&spawn_rng_active,1);return 1;
}
#else
static int spawn_rng_swap(uint64_t expected,uint64_t value);
#endif
static void spawn_rng_restore(void) {
    if(!spawn_rng_armed) return;
    InterlockedExchange(&spawn_rng_active,0);
    if(spawn_rng_installed && spawn_rng_swap(spawn_rng_patched_word,spawn_rng_original_word)) spawn_rng_installed=0;
    spawn_rng_restored=!spawn_rng_installed;
}
static int spawn_rng_status(char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&spawn_rng_cursor,0,0);
    int n=snprintf(out,cap,"armed=%d installed=%d active=%ld mode=%d restored=%d calls=%lld boot_calls=%lld keyed=%lld null=%lld"
        " unreadable=%lld records=%lld dropped=%lld ring=%u failures=%lld",
        spawn_rng_armed,spawn_rng_installed,spawn_rng_active,spawn_rng_mode,spawn_rng_restored,(long long)spawn_rng_calls,
        (long long)spawn_rng_boot_calls,(long long)spawn_rng_keyed,(long long)spawn_rng_null,(long long)spawn_rng_unreadable,
        (long long)cursor,(long long)(cursor>(LONG64)SPAWN_RNG_RING?cursor-(LONG64)SPAWN_RNG_RING:0),SPAWN_RNG_RING,
        (long long)spawn_rng_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* One row per call; the probe walks absolute indices until nil. */
static int spawn_rng_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&spawn_rng_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)SPAWN_RNG_RING && index<(uint64_t)(cursor-(LONG64)SPAWN_RNG_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const spawn_rng_row_t *r=&spawn_rng_ring[index%SPAWN_RNG_RING];
        n=snprintf(out,cap,"index=%llu tick=%llu phase=%u ui=%u mode=%u normal=%u state=%016llx key=%016llx caller=%016llx"
            " entry=%016llx exit=%016llx draws=%lld",(unsigned long long)index,(unsigned long long)r->tick,r->phase,r->ui,r->mode,
            r->normal,(unsigned long long)r->state,(unsigned long long)r->key,(unsigned long long)r->caller,(unsigned long long)r->entry,
            (unsigned long long)r->exit,r->draws==UINT32_MAX?-1LL:(long long)r->draws);
    }
    return n>0 && (size_t)n<cap?n:0;
}
