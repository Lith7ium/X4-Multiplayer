/* Per-script deterministic RNG streams on the simulation thread, DEFAULT OFF.
 *
 * Census: the sim thread's TLS RNG word (slot0+0x318) is reseeded identically
 * every frame, yet within a frame two clients make different numbers of draws
 * on it. The draws come from AI scripts and MD cues sharing that one stream, so
 * any difference in which scripts run, or in their order, shifts every later
 * draw of every other script.
 *
 * Every script execution entry on the sim thread opens a scope. A keyed scope
 * saves the caller's word, replaces it by
 *   lockstep_seed(domain+source, tick, owner, stream, ordinal, detail)
 * and restored afterwards (__finally, also on exception). Key inputs are
 * portable only:
 *   AI  owner  = saved UniverseID of the context entity (world identity map);
 *                with native_gate_portable_created the created ID of an entity
 *                created after the load (lockstep_world_identity.h), from the
 *                frame after its creation on
 *   MD  owner  = FNV-1a of the static cue's encoded "module.cue" name
 *   stream     = script name hash (AI) or MD tag, entry source, program length
 *   detail     = entry PC (ExecState_Run), run range (begin,end,mode), or the
 *                event keyword plus the saved ID of its first object parameter
 *                (its created ID with portable keys on; 0 without either)
 *   ordinal    = count of (owner, stream, detail) entries this tick, so entries
 *                of one owner with different events or PCs never share a rank
 * Runtime IDs and pointers never enter a key; a pointer only decides nesting.
 *
 * Nesting: an entry inside a keyed scope for the same native owner (the same
 * AI context or cue: interrupt actions of the running script, its own event)
 * continues that stream; it is the same execution. A different known owner
 * nested in a keyed scope (a child) gets its own key with exactly the outermost
 * formula and the shared per-tick ordinal, and the parent's word is restored
 * after it. Keys are therefore context-free: the parent's draws never depend on
 * the draws of scripts it triggered, and a child's stream depends neither on
 * which script triggered it nor on its rank among the parent's other nested
 * entries. That rank follows pointer-ordered listener delivery and whichever
 * script happens to flush deferred events, so a parent-derived child key would
 * reintroduce exactly the order dependence this removes. The parent key is
 * kept in the receipt only. Unknown owners never receive a fabricated key:
 * they continue the current stream (natural outermost, counted unkeyed; the
 * enclosing one nested, counted nested_unkeyed) and their known children are
 * keyed normally.
 *
 * Modes: "key" reseeds and restores; "observe" derives the same identities,
 * keys and scope structure without writing, recording the natural words.
 * Keyed scopes keep order-independent per-tick key/draw sums and, when the
 * probe exports rows, bounded census rows (seed word, exit word before restore
 * and the generator steps between them). Arming, hooks and Lua live in
 * lockstep_script_rng_hooks.h; failures stop the gate with reason 82.
 *
 * Every census row carries caller= (the caller's word at entry: for an
 * outermost scope the natural sim-thread word the scope replaced, for a child
 * its parent's stream) and why= (0 portable owner, 2 unportable owner keyed by
 * option 2, else the reason a natural row is unkeyed).
 * Options (gate.script_rng 4th argument, default 0; long-L1/L3 census):
 *  1 natural rows: outermost UNKEYED entries, which run on and advance the
 *    natural stream, get rows too (state 5, seed = natural word). They never
 *    enter the per-tick key/draw sums; their key field is a census join key
 *    only (own domain and ordinal namespace, never written): the same formula
 *    with owner = the unportable marker. Needs rows.
 *  2 unportable keying: an AI context whose entity has neither a saved ID nor
 *    (with native_gate_portable_created) a created ID is keyed with owner =
 *    the marker SCRIPT_RNG_UNMAPPED_OWNER, so its rank among this tick's
 *    unportable entries of the same script and detail decides the key: the
 *    (deterministic) execution order instead of the natural word. That is
 *    every created object while portable keys are off, and with them on the
 *    creation frame of each created object, where its script starts: a
 *    distress drone's move.escapepod draws its first <wait min="1s"
 *    max="3s"/> there. The default stays: unknown owners continue the
 *    natural stream.
 *
 * Cost (2-4k entries per frame): fields are read directly under a guarded
 * call instead of ReadProcessMemory, an AI context's resolved identity and
 * runtime -> saved IDs are cached with validation, the ordinal table is
 * cleared by epoch, rows are written only when exported. None of this changes
 * an identity or a key. Entry/exit self-time is measured with the TSC and
 * reported per tick (cur_self_us/last_self_us). */
#define SCRIPT_RNG_DOMAIN 0x58344c5343525054ULL       /* "X4LSCRPT" */
#define SCRIPT_RNG_MD_SCRIPT 0x4d445f4355455331ULL    /* "MD_CUES1" */
/* The Linux gate (linux/gate_script_rng.h) defines these for its executable, and its own guarded read. */
#ifndef SCRIPT_RNG_OWN_PLATFORM
#define SCRIPT_RNG_AI_VT 0x2c6a270u    /* AI::AIScriptContext::vftable */
#define SCRIPT_RNG_STACK_VT 0x2c690b8u /* AI::AICallStackContext::vftable */
#define SCRIPT_RNG_CUE_VT 0x2c75170u   /* MD::Cue::vftable */
#define SCRIPT_RNG_AI_VALUE 0xa8       /* a context Value's payload is the context plus this */
#define SCRIPT_RNG_MD_VALUE 0xb0       /* a cue Value's payload is the cue plus this */
#define SCRIPT_RNG_CONTEXT_SCRIPT 0xd8 /* the context's script descriptor */
#endif
#define SCRIPT_RNG_DEPTH 256u
#define SCRIPT_RNG_TABLE 16384u
#define SCRIPT_RNG_PROBES 128u
#define SCRIPT_RNG_NAMES 4096u
#define SCRIPT_RNG_RING 32768u /* boot phase 2 alone keys about 12,300 scopes (long-L8) */
#define SCRIPT_RNG_STEP_CAP 4096u
#define SCRIPT_RNG_AI_CACHE 16384u
#define SCRIPT_RNG_SAVED_CACHE 4096u
enum {SCRIPT_RNG_OBSERVE=0,SCRIPT_RNG_KEY=1};
enum {SCRIPT_RNG_AI=1,SCRIPT_RNG_MD=2,SCRIPT_RNG_BOOT=4}; /* arm mask; BOOT also keys boot phase 2 */
/* Entry sources: ExecState_Run 0xb58bb0, run range 0xb58ce0, AIScriptContext
 * event 0x1066750, AICallStackContext event 0x1062f70, MD::Cue event 0x111f990;
 * the direct MD::Cue_CheckConditions 0x111c290 calls of lockstep_script_rng_md.h:
 * MDScheduler_Update timed check, Cue_EnterWaiting, Cue_RaiseCueEvent listener.
 * All MD sources from SR_CUE on read the cue's own event record at cue+0x18. */
enum {SR_EXEC=1,SR_RANGE=2,SR_EVENT=3,SR_STACK=4,SR_CUE=5,SR_TIMED=6,SR_WAIT=7,SR_RAISE=8,SR_SOURCES=9};
enum {SR_PASS=0,SR_OUTER=1,SR_CHILD=2,SR_SAME=3,SR_NESTED_UNKEYED=4,SR_UNKEYED=5};
enum {SR_OK=0,SR_UNREADABLE=1,SR_UNMAPPED=2,SR_UNNAMED=3,SR_FOREIGN=4,SR_MASKED=5,SR_REASONS=6};
enum {SCRIPT_RNG_OPT_NATURAL=1,SCRIPT_RNG_OPT_UNMAPPED=2,SCRIPT_RNG_OPTIONS=3};
#define SCRIPT_RNG_UNMAPPED_OWNER 0x7f554e4d41505044ULL /* "\x7fUNMAPPD": above every saved ID, bit 63 clear (created IDs set it) */
#define SCRIPT_RNG_UNKEYED_DOMAIN 0x58344c53554e4b59ULL /* "X4LSUNKY": census join keys of unkeyed rows */
typedef struct {
    uint64_t owner,script,program,detail,stream;uintptr_t local;
    unsigned kind,instance,created;char name[64]; /* created: owner is a created ID */
} script_rng_identity;
typedef struct script_rng_frame {
    struct script_rng_frame *parent,*owner;
    uint64_t tick,key,parent_key,saved,seed,ordinal;
    script_rng_identity id;unsigned source,state,depth,seeded,why;
} script_rng_frame;
typedef struct {uint64_t owner,stream;uint32_t count,epoch;} script_rng_slot;
typedef struct {uintptr_t definition,module;uint64_t hash;char name[64];} script_rng_name;
typedef struct {
    uint64_t tick,owner,stream,detail,ordinal,key,parent,seed,exit,caller;
    uint32_t draws,depth;uint8_t source,state,kind,instance,seeded,why;char name[64];
} script_rng_row_t;
typedef struct {uint64_t tick,draws,keyset,drawset,self;uint32_t phase,open,entries,outer,child,unkeyed,unknown;} script_rng_tally;
/* Performance caches (UI thread only, never a key input of their own): an AI
 * context's resolved identity, valid while the context's vtable, entity,
 * entity runtime ID, script pointer and the script's 32-byte name string
 * header are unchanged; and runtime -> saved ID. A mapped ID never changes
 * (the import refuses a conflicting saved ID, the table is sealed after load);
 * an unmapped result is cached only once the table is sealed. gen 0 marks a
 * saved ID (immutable); a created ID or a miss carries the created table
 * generation + 1 it was read in and expires with it (portable keys on; off,
 * the generation stays 0 and misses stay cached as before). */
typedef struct {uintptr_t context,entity,script;uint64_t runtime,owner,hash,gen;unsigned char header[32];char name[64];} script_rng_ai_entry;
typedef struct {uint64_t runtime,saved,gen;} script_rng_saved_entry;
static volatile LONG script_rng_active;
static int script_rng_armed,script_rng_mode,script_rng_restored,script_rng_cue_installed,script_rng_stack_installed,script_rng_exec_owned;
static int script_rng_rows; /* ring rows only when the probe exports them */
static unsigned script_rng_mask,script_rng_depth_max,script_rng_name_count,script_rng_options;
static LONG64 script_rng_unmapped_keyed,script_rng_unkeyed_rows;
static uint32_t script_rng_epoch;
static script_rng_ai_entry script_rng_ai_cache[SCRIPT_RNG_AI_CACHE];
static script_rng_saved_entry script_rng_saved_cache[SCRIPT_RNG_SAVED_CACHE];
static const world_identity_entry *script_rng_cache_table;
static LONG64 script_rng_ai_hits,script_rng_ai_misses;
/* Self-timing: TSC ticks spent in scope entry/exit (never the original). */
static uint64_t script_rng_self,script_rng_tsc0;
static LARGE_INTEGER script_rng_qpc0;
static script_rng_slot script_rng_slots[SCRIPT_RNG_TABLE];
static script_rng_name script_rng_names[SCRIPT_RNG_NAMES];
static script_rng_row_t script_rng_ring[SCRIPT_RNG_RING];
static script_rng_tally script_rng_now,script_rng_last;
static volatile LONG64 script_rng_cursor,script_rng_failures,script_rng_threads;
static LONG64 script_rng_created; /* keyed scopes whose owner is a created ID */
static LONG64 script_rng_sources[SR_SOURCES],script_rng_why[SR_REASONS],script_rng_outer,script_rng_child,script_rng_same,
    script_rng_nested_unkeyed,script_rng_unkeyed,script_rng_table_full,script_rng_restores,script_rng_draws_unknown;
static uint64_t script_rng_draws;
static _Thread_local script_rng_frame *script_rng_current;

static uint64_t script_rng_key(unsigned source,uint64_t tick,uint64_t owner,uint64_t stream,uint64_t ordinal,uint64_t detail) {
    return lockstep_seed(SCRIPT_RNG_DOMAIN+source,tick,owner,stream,ordinal,detail);
}
static uint64_t script_rng_stream(uint64_t script,unsigned source,unsigned kind,uint64_t program) {
    return lockstep_mix(script^lockstep_mix(((uint64_t)source<<56)^((uint64_t)kind<<48)^program));
}
#include "lockstep_rng_steps.h"
static uint64_t script_rng_tsc(void) {return __builtin_ia32_rdtsc();}
static void script_rng_clock_start(void) {QueryPerformanceCounter(&script_rng_qpc0);script_rng_tsc0=script_rng_tsc();}
/* TSC ticks to microseconds, calibrated against QPC since the clock start. */
static uint64_t script_rng_us(uint64_t ticks) {
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    uint64_t tsc=script_rng_tsc();
    if(!script_rng_tsc0 || tsc<=script_rng_tsc0 || now.QuadPart<=script_rng_qpc0.QuadPart || frequency.QuadPart<=0) return 0;
    double us=(double)ticks*((double)(now.QuadPart-script_rng_qpc0.QuadPart)*1e6/(double)frequency.QuadPart)/(double)(tsc-script_rng_tsc0);
    return us>=1.8e19?UINT64_MAX:(uint64_t)us;
}
#ifndef SCRIPT_RNG_OWN_PLATFORM
/* Guarded direct read for the per-entry hot path: read_local costs a system
 * call per field (about 250 ns), this about 4 ns. Same contract: 0 for NULL
 * or an access violation, after which every caller discards the possibly
 * partial copy. Clang's __try covers faults only inside calls (no
 * -fasync-exceptions), so the load happens in a separate function reached
 * through an opaque pointer: never inlined, never assumed nounwind. */
static __attribute__((noinline)) void script_rng_copy(void *dest,const void *source,size_t bytes) {memcpy(dest,source,bytes);}
static void (*volatile script_rng_copy_fn)(void *,const void *,size_t)=script_rng_copy;
static int script_rng_read(const void *source,void *dest,size_t bytes) {
    if(!source) return 0;
    __try {script_rng_copy_fn(dest,source,bytes);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
    return 1;
}
#endif
static void script_rng_cache_flush(void) {
    memset(script_rng_ai_cache,0,sizeof(script_rng_ai_cache));memset(script_rng_saved_cache,0,sizeof(script_rng_saved_cache));
    script_rng_cache_table=world_identities;
}
/* The caches belong to one identity table and to the UI thread. */
static int script_rng_cache_usable(void) {
    if(GetCurrentThreadId()!=ui_thread) return 0;
    if(script_rng_cache_table!=world_identities) script_rng_cache_flush();
    return 1;
}
/* runtime -> portable key (world_identity_key) and its class. */
static uint64_t script_rng_saved_with(uint64_t runtime,unsigned *klass) {
    if(!runtime || !script_rng_cache_usable()) return world_identity_key(runtime,klass);
    script_rng_saved_entry *e=&script_rng_saved_cache[lockstep_mix(runtime)&(SCRIPT_RNG_SAVED_CACHE-1)];
    uint64_t gen=world_identity_generation()+1;
    if(e->runtime==runtime && (!e->gen || e->gen==gen)) {
        *klass=!e->gen?WORLD_KEY_SAVED:e->saved?WORLD_KEY_CREATED:WORLD_KEY_NONE;return e->saved;
    }
    uint64_t saved=world_identity_key(runtime,klass);
    if(*klass==WORLD_KEY_SAVED) {e->runtime=runtime;e->saved=saved;e->gen=0;}
    else if(world_identity_sealed) {e->runtime=runtime;e->saved=saved;e->gen=gen;}
    return saved;
}
static uint64_t script_rng_saved(uint64_t runtime) {unsigned klass;return script_rng_saved_with(runtime,&klass);}
/* Cheap native locator: entity kind and the process-local owner object. The
 * owner pointer only decides nesting; it never enters a key. */
static unsigned script_rng_locate(unsigned source,void *object,script_rng_identity *id,uintptr_t *record) {
    uintptr_t self=(uintptr_t)object,vtable=0;*record=0;
    if(!self) return SR_UNREADABLE;
    if(source==SR_EXEC || source==SR_RANGE) {
        unsigned char owner[16];uint32_t tag;uintptr_t value;
        if(!script_rng_read((unsigned char*)object+0x38,owner,sizeof(owner))) return SR_UNREADABLE;
        memcpy(&tag,owner,4);memcpy(&value,owner+8,8);
        if(tag==0x66 && value>=SCRIPT_RNG_AI_VALUE) {id->kind=SCRIPT_RNG_AI;id->local=value-SCRIPT_RNG_AI_VALUE;return SR_OK;}
        if(tag==0x5d && value>=SCRIPT_RNG_MD_VALUE) {id->kind=SCRIPT_RNG_MD;id->local=value-SCRIPT_RNG_MD_VALUE;return SR_OK;}
        return SR_FOREIGN;
    }
    *record=self+0x18; /* ScriptEventHandler event record: keyword, then typed values. */
    if(source==SR_STACK) {
        if(!script_rng_read(object,&vtable,8)) return SR_UNREADABLE;
        if(vtable!=(uintptr_t)(image+SCRIPT_RNG_STACK_VT)) return SR_FOREIGN;
        if(!script_rng_read((unsigned char*)object+0xc8,&id->local,8) || !id->local) return SR_UNREADABLE;
        id->kind=SCRIPT_RNG_AI;return SR_OK;
    }
    id->kind=source>=SR_CUE?SCRIPT_RNG_MD:SCRIPT_RNG_AI;id->local=self;return SR_OK;
}
static unsigned script_rng_ai(script_rng_identity *id) {
    uintptr_t context=id->local,vtable=0,entity=0,script=0;uint64_t runtime=0;unsigned char header[32];char text[96];
    if(!script_rng_read((void*)context,&vtable,8)) return SR_UNREADABLE;
    if(vtable!=(uintptr_t)(image+SCRIPT_RNG_AI_VT)) return SR_FOREIGN;
    if(!script_rng_read((void*)(context+0x120),&entity,8) || !entity || !script_rng_read((void*)(entity+8),&runtime,8) || !runtime) return SR_UNREADABLE;
    int cache=script_rng_cache_usable() && script_rng_read((void*)(context+SCRIPT_RNG_CONTEXT_SCRIPT),&script,8) && script &&
        script_rng_read((void*)(script+0x10),header,sizeof(header));
    script_rng_ai_entry *e=&script_rng_ai_cache[lockstep_mix(context)&(SCRIPT_RNG_AI_CACHE-1)];uint64_t gen=world_identity_generation()+1;
    if(cache && e->context==context && e->entity==entity && e->runtime==runtime && e->script==script && (!e->gen || e->gen==gen) &&
       !memcmp(e->header,header,sizeof(header))) {
        ++script_rng_ai_hits;id->script=e->hash;memcpy(id->name,e->name,sizeof(id->name));id->owner=e->owner;id->created=e->gen && e->owner;
        return id->owner?SR_OK:SR_UNMAPPED;
    }
    ++script_rng_ai_misses;
    if(!(id->script=ai_script_with(script_rng_read,(void*)context,text))) return SR_UNNAMED;
    size_t n=strlen(text);if(n>=sizeof(id->name)) n=sizeof(id->name)-1;memcpy(id->name,text,n);id->name[n]=0;
    unsigned klass;id->owner=script_rng_saved_with(runtime,&klass); /* Missing map is unkeyed, never a runtime-ID fallback. */
    id->created=klass==WORLD_KEY_CREATED;
    if(cache && (id->owner || world_identity_sealed)) {
        e->context=context;e->entity=entity;e->runtime=runtime;e->script=script;e->owner=id->owner;e->hash=id->script;
        e->gen=klass==WORLD_KEY_SAVED?0:gen;
        memcpy(e->header,header,sizeof(header));memcpy(e->name,id->name,sizeof(e->name));
    }
    return id->owner?SR_OK:SR_UNMAPPED;
}
#include "lockstep_cue_identity.h"
static unsigned script_rng_resolve(unsigned source,void *object,void *program,uint64_t begin,uint64_t end,unsigned mode,uintptr_t record,script_rng_identity *id) {
    if(!(script_rng_mask&id->kind)) return SR_MASKED;
    unsigned why=id->kind==SCRIPT_RNG_AI?script_rng_ai(id):script_rng_md(id);
    /* An unmapped AI owner is still named: with an option set, finish its
     * stream and detail (natural rows name it, option 2 keys it). */
    if(why && !(why==SR_UNMAPPED && script_rng_options)) return why;
    uint64_t length=0;
    if(source==SR_EXEC || source==SR_RANGE) {
        uintptr_t head[2],bounds[2];
        if(!script_rng_read((unsigned char*)object+0x10,head,16)) return SR_UNREADABLE;
        uintptr_t vector=source==SR_RANGE?(uintptr_t)program:head[0];
        if(!vector || !script_rng_read((void*)vector,bounds,16) || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&7) ||
           (bounds[1]-bounds[0])/8>1048576) return SR_UNREADABLE;
        length=(bounds[1]-bounds[0])/8;
        if(source==SR_EXEC) {if(head[1]>1048576) return SR_UNREADABLE;id->detail=head[1];}
        else id->detail=lockstep_mix(begin^lockstep_mix(end^lockstep_mix(mode)));
    } else {
        unsigned char event[24];uint32_t keyword,tag;uint64_t payload;
        if(!script_rng_read((void*)record,event,sizeof(event))) return SR_UNREADABLE;
        memcpy(&keyword,event,4);memcpy(&tag,event+8,4);memcpy(&payload,event+16,8);
        /* A component parameter contributes only its saved ID; unmapped stays 0. */
        uint64_t parameter=tag==0xa?script_rng_saved(payload):0;
        id->detail=lockstep_mix(((uint64_t)keyword<<32)^lockstep_mix(parameter));
    }
    id->program=length;id->stream=script_rng_stream(id->script,source,id->kind,length);return why;
}
/* Per-tick (owner, stream, detail) ordinals; 0 when the bounded table is
 * exhausted. A slot of an older epoch is empty, so a new tick clears the
 * table with one increment instead of a 384 KB memset. */
static uint64_t script_rng_ordinal(uint64_t owner,uint64_t stream,uint64_t detail) {
    stream=lockstep_mix(stream^lockstep_mix(detail));
    unsigned first=(unsigned)(lockstep_mix(owner^lockstep_mix(stream))&(SCRIPT_RNG_TABLE-1));
    for(unsigned n=0;n<SCRIPT_RNG_PROBES;n++) {
        script_rng_slot *s=&script_rng_slots[(first+n)&(SCRIPT_RNG_TABLE-1)];
        if(s->epoch!=script_rng_epoch) {s->epoch=script_rng_epoch;s->owner=owner;s->stream=stream;s->count=1;return 1;}
        if(s->owner==owner && s->stream==stream) return ++s->count;
    }
    return 0;
}
static void script_rng_roll(uint64_t tick) {
    if(script_rng_now.open && script_rng_now.tick==tick && script_rng_now.phase==(unsigned)boot_phase) return;
    if(script_rng_now.open) script_rng_last=script_rng_now;
    memset(&script_rng_now,0,sizeof(script_rng_now));script_rng_now.tick=tick;script_rng_now.phase=(unsigned)boot_phase;script_rng_now.open=1;
    if(!++script_rng_epoch) {memset(script_rng_slots,0,sizeof(script_rng_slots));script_rng_epoch=1;}
}
static void script_rng_push(const script_rng_frame *f,uint64_t exit,uint32_t draws) {
    LONG64 index=InterlockedIncrement64(&script_rng_cursor)-1;
    script_rng_row_t *r=&script_rng_ring[(uint64_t)index%SCRIPT_RNG_RING];
    r->tick=f->tick;r->owner=f->id.owner;r->stream=f->id.stream;r->detail=f->id.detail;r->ordinal=f->ordinal;
    r->key=f->key;r->parent=f->parent_key;r->seed=f->seed;r->exit=exit;r->caller=f->saved;r->draws=draws;r->depth=f->depth;
    r->source=(uint8_t)f->source;r->state=(uint8_t)f->state;r->kind=(uint8_t)f->id.kind;r->instance=(uint8_t)f->id.instance;
    r->seeded=(uint8_t)f->seeded;r->why=(uint8_t)f->why;memcpy(r->name,f->id.name,sizeof(r->name));
}
static void script_rng_record(const script_rng_frame *f,uint64_t exit) {
    uint32_t draws=script_rng_steps(f->seed,exit);
    if(f->state==SR_OUTER) ++script_rng_now.outer;else ++script_rng_now.child;
    if(draws==UINT32_MAX) {++script_rng_now.unknown;++script_rng_draws_unknown;}
    else {script_rng_now.draws+=draws;script_rng_draws+=draws;}
    script_rng_now.keyset+=lockstep_mix(f->key);
    script_rng_now.drawset+=lockstep_mix(f->key^((uint64_t)draws*0x9e3779b97f4a7c15ULL));
    if(script_rng_rows) script_rng_push(f,exit,draws);
}
/* Option 1: an outermost unkeyed scope ran on the natural word (seed); its
 * row is census evidence only, outside every tally. */
static void script_rng_record_unkeyed(const script_rng_frame *f,uint64_t exit) {
    if(!script_rng_rows) return;
    ++script_rng_unkeyed_rows;script_rng_push(f,exit,script_rng_steps(f->seed,exit));
}
static void script_rng_spent(uint64_t since) {uint64_t spent=script_rng_tsc()-since;script_rng_now.self+=spent;script_rng_self+=spent;}
/* Returns 1 when a frame was pushed (script_rng_leave must run), 0 for an
 * untouched pass-through. rng is the executing thread's TLS word. */
static int script_rng_enter(script_rng_frame *f,unsigned source,void *object,void *program,uint64_t begin,uint64_t end,unsigned mode,uint64_t *rng) {
    if(!InterlockedCompareExchange(&script_rng_active,0,0) || !stats.active || source<SR_EXEC || source>=SR_SOURCES) return 0;
    if(boot_phase!=3 && !(boot_phase==2 && boot_mode && !stats.issued && (script_rng_mask&SCRIPT_RNG_BOOT))) return 0;
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&script_rng_threads);return 0;}
    uint64_t start=script_rng_tsc();
    script_rng_frame *parent=script_rng_current;
    if(!rng || (parent && parent->depth>=SCRIPT_RNG_DEPTH)) {InterlockedIncrement64(&script_rng_failures);script_rng_spent(start);return 0;}
    DWORD error=GetLastError();
    memset(f,0,sizeof(*f));f->parent=parent;f->depth=parent?parent->depth+1:1;f->source=source;f->tick=stats.issued;
    script_rng_roll(f->tick);++script_rng_sources[source];++script_rng_now.entries;
    if(f->depth>script_rng_depth_max) script_rng_depth_max=f->depth;
    script_rng_frame *owner=parent?parent->owner:NULL;uintptr_t record=0;
    unsigned why=script_rng_locate(source,object,&f->id,&record);
    if(owner && !why && f->id.local==owner->id.local) {f->state=SR_SAME;++script_rng_same;}
    else {
        if(!why) why=script_rng_resolve(source,object,program,begin,end,mode,record,&f->id);
        if(why==SR_UNMAPPED && (script_rng_options&SCRIPT_RNG_OPT_UNMAPPED)) {
            f->id.owner=SCRIPT_RNG_UNMAPPED_OWNER;f->why=SR_UNMAPPED;why=SR_OK;++script_rng_unmapped_keyed;
        }
        if(!why && !(f->ordinal=script_rng_ordinal(f->id.owner,f->id.stream,f->id.detail))) {++script_rng_table_full;why=SR_REASONS;}
        if(why) {
            /* Masked kinds are transparent by configuration, not missing identity. */
            if(why<SR_REASONS) ++script_rng_why[why];
            f->state=owner?SR_NESTED_UNKEYED:SR_UNKEYED;f->why=why;
            if(why!=SR_MASKED) {if(owner) ++script_rng_nested_unkeyed;else {++script_rng_unkeyed;++script_rng_now.unkeyed;}}
            /* Option 1: a census join key for the natural row, in its own
             * domain and ordinal namespace; it is never written. */
            if(f->state==SR_UNKEYED && why!=SR_MASKED && (script_rng_options&SCRIPT_RNG_OPT_NATURAL)) {
                f->id.owner=SCRIPT_RNG_UNMAPPED_OWNER;
                f->ordinal=script_rng_ordinal(f->id.owner^SCRIPT_RNG_UNKEYED_DOMAIN,f->id.stream,f->id.detail);
                f->key=lockstep_seed(SCRIPT_RNG_UNKEYED_DOMAIN+source,f->tick,f->id.owner,f->id.stream,f->ordinal,f->id.detail);
            }
        } else {
            if(owner) {f->state=SR_CHILD;f->parent_key=owner->key;++script_rng_child;}
            else {f->state=SR_OUTER;++script_rng_outer;}
            if(f->id.created) ++script_rng_created;
            f->key=script_rng_key(source,f->tick,f->id.owner,f->id.stream,f->ordinal,f->id.detail);
        }
    }
    f->owner=f->state==SR_OUTER || f->state==SR_CHILD?f:owner;
    f->saved=*rng;
    if(f->owner==f && script_rng_mode==SCRIPT_RNG_KEY) {*rng=f->key;f->seeded=1;}
    f->seed=*rng;script_rng_current=f;SetLastError(error);script_rng_spent(start);return 1;
}
/* Always restores the caller's word for a seeded frame, including unwinding. */
static void script_rng_leave(script_rng_frame *f,int entered,uint64_t *rng,int abnormal) {
    if(!entered) return;
    uint64_t start=script_rng_tsc(),exit=*rng;
    if(script_rng_current!=f) InterlockedIncrement64(&script_rng_failures);
    if(f->seeded) {*rng=f->saved;++script_rng_restores;}
    script_rng_current=f->parent;
    if(abnormal) InterlockedIncrement64(&script_rng_failures);
    if(f->owner==f) script_rng_record(f,exit);
    else if(f->state==SR_UNKEYED && f->why!=SR_MASKED && (script_rng_options&SCRIPT_RNG_OPT_NATURAL)) script_rng_record_unkeyed(f,exit);
    script_rng_spent(start);
}
/* One census row per call (never one huge line per tick): the probe walks
 * absolute indices until nil. Evicted rows report evicted=1 so it keeps going. */
static int script_rng_row(uint64_t index,char *out,size_t cap) {
    LONG64 cursor=InterlockedCompareExchange64(&script_rng_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)SCRIPT_RNG_RING && index<(uint64_t)(cursor-(LONG64)SCRIPT_RNG_RING))
        n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const script_rng_row_t *r=&script_rng_ring[index%SCRIPT_RNG_RING];char name[sizeof(r->name)];
        memcpy(name,r->name,sizeof(name));name[sizeof(name)-1]=0;
        for(size_t i=0;name[i];i++) if((unsigned char)name[i]<=32 || (unsigned char)name[i]>126 || name[i]=='|') name[i]='_';
        n=snprintf(out,cap,"index=%llu tick=%llu src=%u state=%u kind=%u inst=%u depth=%u owner=%016llx stream=%016llx detail=%016llx"
            " ord=%llu key=%016llx parent=%016llx seeded=%u seed=%016llx exit=%016llx draws=%lld name=%s caller=%016llx why=%u",
            (unsigned long long)index,(unsigned long long)r->tick,r->source,r->state,r->kind,r->instance,r->depth,
            (unsigned long long)r->owner,(unsigned long long)r->stream,(unsigned long long)r->detail,(unsigned long long)r->ordinal,
            (unsigned long long)r->key,(unsigned long long)r->parent,r->seeded,(unsigned long long)r->seed,(unsigned long long)r->exit,
            r->draws==UINT32_MAX?-1LL:(long long)r->draws,name[0]?name:"-",(unsigned long long)r->caller,r->why);
    }
    return n>0 && (size_t)n<cap?n:0;
}
