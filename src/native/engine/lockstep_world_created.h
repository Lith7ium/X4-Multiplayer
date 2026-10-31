/* Portable identities for components created after the snapshot load.
 * DEFAULT OFF: native_gate_created_identities (needs native_gate_world_identities).
 *
 * Hook. AutoIDMap::AssignNewID 0xcc220 (268-byte body pinned, entry word CAS,
 * five-byte home-store prefix relocated to thunk 448..511). Every UniverseID of
 * the process takes its runtime ID there: both U::Component constructors
 * (0x3d1da0, 0x3d2400), UI elements and effect creators, 96 direct callers.
 * The wrapper runs the original once and, while recording, appends the new
 * runtime ID and the calling thread's class to a pending list. It reads nothing
 * else and calls no engine function.
 *
 * Assignment. At the gate boundary before the next tick is admitted (main
 * thread; the MoveWorker has joined) the pending list of frame T is resolved:
 *  - native lookup ced70 must still find the object with object+8 == runtime,
 *    else the creation was transient (created and removed within the frame);
 *  - the verified IsRealClass bitmap must contain Component (22); bullets (11,
 *    the bullet module's domain) and view-local presentation classes
 *    (cutsceneanchor 32, effectobject 42, fogvolume 46, holomap 58, lensflare
 *    61, stardust 97, uielement 105) are excluded and counted;
 *  - creations off the MoveWorker (TLS slot+0x314 view 1, not the main thread)
 *    are counted as foreign and stay unmapped: their interleaving with the
 *    MoveWorker is not ordered, so no ordinal may depend on it;
 *  - key = (parent portable ID, class vtable RVA, macro name hash). The parent
 *    is a saved ID, an earlier created ID, a created ID assigned earlier in
 *    this pass (parents are resolved first), 0 without a parent, or 1 when
 *    the parent has no portable identity;
 *  - ordinal = rank among this frame's MoveWorker creations of the same key,
 *    in resolution order (parents before children, otherwise creation order).
 *    MD, AI scripts, job spawns, weapons and kills all run on the MoveWorker in
 *    VBI slot order, so the rank is deterministic whenever the simulation is;
 *  - ID = 1<<63 | T<<40 | 40-bit hash(T, key, ordinal, salt). A collision with
 *    a live ID takes the next salt (deterministic, counted).
 * No runtime ID, pointer, thread ID or wall time enters an ID. Modules,
 * weapons, shields and pilots of a created ship are keyed by the ship's
 * created ID plus their own class, macro and ordinal.
 *
 * Lifetime. Each boundary looks every live identity up again and retires the
 * destroyed ones from the runtime<->created table (CREATED_LIVE entries). World
 * captures add a row for every live identity present at the capture and keep
 * it, absent after destruction like a saved row (WORLD_HASH_CREATED_MAX rows).
 * Pending overflow, a full table, salt exhaustion or a tick beyond 2^23-1
 * stop the gate with reason 96 (fail closed; 91 is the pilot module). The saved lookup
 * world_identity_lookup is unchanged; world_identity_portable adds created IDs.
 * With native_gate_portable_created the ordering/keying modules use them too
 * (world_identity_key); every table change bumps world_created_generation.
 *
 * Load-created objects, DEFAULT OFF: native_gate_created_load. long-L4 (all
 * fixes) first differs at tick 592 on AI owners keyed by the unportable
 * marker of native_gate_script_rng_unmapped: about 20 ships (20 unmapped
 * pathing agents every frame) with their pilots and engineers, running
 * scripts from boot phase 2 on. The load creates them after the snapshot
 * import and before the seal, so they have no saved ID, and recording only
 * starts at the seal, so they never get a created ID; their per-tick rank
 * keys follow the native execution order. With the option the hook also
 * notes every AssignNewID between arming and the seal (runtime ID and thread
 * ID, CREATED_LOAD_NOTES entries; snapshot 005 makes about 1.37 million).
 * At the seal (main thread, before frame 1) the still-present Components
 * without a saved ID, not excluded, of the thread that made most of them get
 * created IDs exactly like later creations, with tick 0: key = (parent
 * portable ID, class, macro), ordinal = rank in that thread's creation order,
 * parents first. Objects of other threads stay unportable (load_foreign).
 * Note overflow or more than CREATED_LOAD_MAX candidates assign none of them
 * (load_skipped) rather than a partial, order-dependent set. */
/* The Linux gate (linux/gate_world_created.h) has its own hook, reads, macro and class lookups. */
#ifndef CREATED_OWN_PLATFORM
#define CREATED_ENTRY 0xcc220u
#define CREATED_THUNK 448u
#define CREATED_TYPE_OF(vtable) vtable>=(uintptr_t)image && vtable-(uintptr_t)image<0x6fa9000u?(uint32_t)(vtable-(uintptr_t)image):0xffffffffu
#endif
#define CREATED_BIT 0x8000000000000000ULL
#define CREATED_ORPHAN 1ULL
#define CREATED_TICK_SHIFT 40u
#define CREATED_TICK_LIMIT 0x7fffffULL
#define CREATED_HASH_MASK 0xffffffffffULL
#define CREATED_PENDING 131072u
#define CREATED_LIVE 524288u /* play26-long: 13108 live at tick 15548, 0.34 more a tick; 131072 lasted 3.2 h at 30 Hz */
#define CREATED_INDEX (CREATED_LIVE*2u)
#define CREATED_BATCH_MAP (CREATED_PENDING*2u)
#define CREATED_LOG 65536u
#define CREATED_SALTS 16u
#define CREATED_DOMAIN 0x58344c4352454154ULL /* "X4LCREAT" */
#define CREATED_CLASS_CACHE 1024u
#define CREATED_STOP_REASON 96u
#define CREATED_LOAD_NOTES (1u<<21)
#define CREATED_LOAD_MAX 32768u
#define CREATED_LOAD_THREADS 16u
enum {CREATED_MOVEWORKER,CREATED_MAIN,CREATED_OTHER};
/* native_gate_runtime_ids (lockstep_runtime_ids.h): who allocates, and from which list. */
#ifndef CREATED_OWN_PLATFORM
static int rid_mode;
static uint64_t rid_assign(void *object,void *caller);
#endif
enum {CREATED_KIND_COMPONENT=1,CREATED_KIND_EXCLUDED,CREATED_KIND_OTHER};
enum {CREATED_ROW_ASSIGN=1,CREATED_ROW_RETIRE,CREATED_ROW_FOREIGN};
#ifndef CREATED_OWN_PLATFORM
typedef uint64_t (*created_assign_fn)(void *);
#endif
typedef struct {uint64_t runtime;uint32_t thread,reserved;} created_note_t;
typedef struct {uint64_t runtime,created;uint32_t tick,captured;} created_live_t;
typedef struct {uint64_t runtime,parent_runtime,created,macro;uintptr_t object;uint32_t type,assigned;} created_candidate_t;
typedef struct {uint64_t key;uint32_t value,stamp;} created_slot_t;
typedef struct {uint64_t id,parent;uint32_t kind,tick,type,ordinal,thread;char macro[96];} created_row_t;
typedef struct {uintptr_t vtable;unsigned kind;} created_class_t;
static const unsigned created_excluded_classes[]={11,32,42,46,58,61,97,105};

static created_note_t *created_pending,*created_batch;
static unsigned created_pending_count;
static SRWLOCK created_pending_lock=SRWLOCK_INIT;
static created_live_t *created_live;
static unsigned created_live_count;
static uint32_t *created_by_runtime,*created_by_id; /* live position + 1; 0 empty */
static SRWLOCK created_lock=SRWLOCK_INIT;
static created_candidate_t *created_candidates;
static created_slot_t *created_parents,*created_ordinals;
static uint32_t created_stamp;
static created_class_t created_classes[CREATED_CLASS_CACHE];
static created_row_t *created_ring;
static volatile LONG64 created_row_cursor;
static volatile LONG created_armed,created_recording,created_rows_armed;
static int created_installed,created_restored;
static uint64_t created_original_word,created_patched_word;
static volatile LONG64 created_calls,created_noted,created_unreadable,created_pending_overflow,created_failures;
static uint64_t created_frames,created_assigned,created_retired,created_transient,created_other,created_excluded;
static uint64_t created_foreign_main,created_foreign_other,created_orphans,created_macro_unknown,created_rehash;
static uint64_t created_imported,created_duplicate,created_last_tick,created_digest=0xcbf29ce484222325ULL;
static unsigned created_last_assigned,created_peak;
static const char *created_stage="idle";
/* native_gate_created_load: notes made during the load (thread = thread ID). */
static created_note_t *created_load;
static volatile LONG created_load_armed,created_load_recording;
static volatile LONG64 created_load_noted,created_load_overflow;
static uint64_t created_load_candidates,created_load_foreign,created_load_assigned;
static unsigned created_load_threads;static int created_load_done,created_load_skipped;

#ifndef CREATED_OWN_PLATFORM
/* Guarded copy (clang's __try covers faults only inside calls, so the load is
 * reached through an opaque pointer, as in lockstep_script_rng.h). */
static __attribute__((noinline)) void created_copy(void *dest,const void *source,size_t bytes) {memcpy(dest,source,bytes);}
static void (*volatile created_copy_fn)(void *,const void *,size_t)=created_copy;
static int created_read(const void *source,void *dest,size_t bytes) {
    if(!source) return 0;
    __try {created_copy_fn(dest,source,bytes);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
    return 1;
}
#endif
static int created_fail(const char *stage) {
    created_stage=stage;InterlockedIncrement64(&created_failures);return 0;
}
static int created_enabled(void) {return InterlockedCompareExchange(&created_armed,0,0)!=0;}

/* The identity scheme. key: what was created where; id: which one of them. */
static uint64_t created_key(uint64_t parent,uint32_t type,uint64_t macro) {
    return lockstep_seed(CREATED_DOMAIN,0,parent,type,macro,0);
}
static uint64_t created_make_id(uint64_t tick,uint64_t key,uint32_t ordinal,uint32_t salt) {
    return CREATED_BIT|(tick<<CREATED_TICK_SHIFT)|(lockstep_seed(CREATED_DOMAIN^1,tick,key,ordinal,salt,0)&CREATED_HASH_MASK);
}
static uint64_t created_text_hash(const char *text) {
    uint64_t hash=0xcbf29ce484222325ULL;
    for(;*text;text++) {hash^=(unsigned char)*text;hash*=0x100000001b3ULL;}
    return hash<2?hash+2:hash; /* 0: no macro, 1: unreadable macro */
}
static void created_fold(uint64_t a,uint64_t b,uint64_t c) {
    const uint64_t words[3]={a,b,c};const unsigned char *p=(const unsigned char*)words;
    for(unsigned i=0;i<sizeof(words);i++) {created_digest^=p[i];created_digest*=0x100000001b3ULL;}
}

/* Live table: dense array plus two linear-probe indexes (load <= 1/2). */
static unsigned created_home(uint64_t key) {return (unsigned)lockstep_mix(key)&(CREATED_INDEX-1u);}
static uint64_t created_index_key(unsigned position,int by_id) {
    return by_id?created_live[position].created:created_live[position].runtime;
}
static unsigned created_index_slot(const uint32_t *index,int by_id,uint64_t key) {
    unsigned slot=created_home(key);
    while(index[slot] && created_index_key(index[slot]-1u,by_id)!=key) slot=(slot+1u)&(CREATED_INDEX-1u);
    return slot;
}
static void created_index_erase(uint32_t *index,int by_id,unsigned hole) {
    for(unsigned next=(hole+1u)&(CREATED_INDEX-1u);index[next];next=(next+1u)&(CREATED_INDEX-1u)) {
        unsigned home=created_home(created_index_key(index[next]-1u,by_id));
        /* Shift back unless the entry's home lies cyclically in (hole,next]. */
        if(((next-home)&(CREATED_INDEX-1u))>=((next-hole)&(CREATED_INDEX-1u))) {index[hole]=index[next];hole=next;}
    }
    index[hole]=0;
}
static uint64_t created_find_runtime(uint64_t runtime) {
    if(!created_by_runtime || !runtime) return 0;
    unsigned slot=created_index_slot(created_by_runtime,0,runtime);
    return created_by_runtime[slot]?created_live[created_by_runtime[slot]-1u].created:0;
}
static uint64_t created_find_id(uint64_t id) {
    if(!created_by_id || !id) return 0;
    unsigned slot=created_index_slot(created_by_id,1,id);
    return created_by_id[slot]?created_live[created_by_id[slot]-1u].runtime:0;
}
static int created_live_add(uint64_t runtime,uint64_t id,uint32_t tick) {
    if(created_live_count>=CREATED_LIVE) return 0;
    unsigned a=created_index_slot(created_by_runtime,0,runtime),b=created_index_slot(created_by_id,1,id);
    if(created_by_runtime[a] || created_by_id[b]) return 0;
    created_live[created_live_count]=(created_live_t){runtime,id,tick,0};
    created_by_runtime[a]=created_by_id[b]=++created_live_count;
    if(created_live_count>created_peak) created_peak=created_live_count;
    return 1;
}
static void created_live_remove(unsigned position) {
    created_index_erase(created_by_runtime,0,created_index_slot(created_by_runtime,0,created_live[position].runtime));
    created_index_erase(created_by_id,1,created_index_slot(created_by_id,1,created_live[position].created));
    unsigned last=--created_live_count;
    if(position==last) return;
    created_live[position]=created_live[last];
    created_by_runtime[created_index_slot(created_by_runtime,0,created_live[position].runtime)]=position+1u;
    created_by_id[created_index_slot(created_by_id,1,created_live[position].created)]=position+1u;
}
/* Portable lookups for captures and Lua. */
static uint64_t created_lookup(uint64_t runtime) {
    if(!created_enabled() || !runtime) return 0;
    AcquireSRWLockShared(&created_lock);uint64_t id=created_find_runtime(runtime);ReleaseSRWLockShared(&created_lock);
    return id;
}
static uint64_t created_runtime_of(uint64_t id) {
    if(!created_enabled() || !(id&CREATED_BIT)) return 0;
    AcquireSRWLockShared(&created_lock);uint64_t runtime=created_find_id(id);ReleaseSRWLockShared(&created_lock);
    return runtime;
}
#ifndef CREATED_OWN_PLATFORM
static uint64_t world_identity_portable(uint64_t runtime) {
    uint64_t saved=world_identity_lookup(runtime);
    return saved?saved:created_lookup(runtime);
}
#endif

/* Per-frame maps, valid for the current stamp only (no clearing). */
static created_slot_t *created_map_slot(created_slot_t *map,uint64_t key) {
    unsigned slot=(unsigned)lockstep_mix(key^0x9e3779b97f4a7c15ULL)&(CREATED_BATCH_MAP-1u);
    while(map[slot].stamp==created_stamp && map[slot].key!=key) slot=(slot+1u)&(CREATED_BATCH_MAP-1u);
    return &map[slot];
}

/* Hook side. */
#ifdef CREATED_OWN_PLATFORM
static unsigned created_thread_class(void);
#else
static unsigned created_thread_class(void) {
    void **slots=NULL,*slot=NULL;unsigned char view=0;
    if(GetCurrentThreadId()==main_thread) return CREATED_MAIN;
    if(created_read((unsigned char*)NtCurrentTeb()+0x58,&slots,8) && slots && created_read(slots,&slot,8) && slot &&
       created_read((unsigned char*)slot+0x314,&view,1) && view==1) return CREATED_MOVEWORKER;
    return CREATED_OTHER;
}
#endif
static void created_note_with(void *object,unsigned thread) {
    uint64_t runtime=0;
    if(!created_read((unsigned char*)object+8,&runtime,8) || !runtime) {InterlockedIncrement64(&created_unreadable);return;}
    AcquireSRWLockExclusive(&created_pending_lock);
    if(created_pending && created_pending_count<CREATED_PENDING) {
        created_pending[created_pending_count].runtime=runtime;created_pending[created_pending_count].thread=thread;
        ++created_pending_count;InterlockedIncrement64(&created_noted);
    } else InterlockedIncrement64(&created_pending_overflow);
    ReleaseSRWLockExclusive(&created_pending_lock);
}
/* Load notes: lock-free append; a slot whose writer has not finished reads
 * runtime 0 at the seal and is skipped. */
static void created_load_note(void *object) {
    uint64_t runtime=0;
    if(!created_read((unsigned char*)object+8,&runtime,8) || !runtime) {InterlockedIncrement64(&created_unreadable);return;}
    LONG64 index=InterlockedIncrement64(&created_load_noted)-1;
    if(!created_load || index>=(LONG64)CREATED_LOAD_NOTES) {InterlockedIncrement64(&created_load_overflow);return;}
    created_load[index].thread=(uint32_t)GetCurrentThreadId();created_load[index].reserved=0;
    MemoryBarrier();created_load[index].runtime=runtime;
}
static uint64_t created_assign_execute(created_assign_fn original,void *object) {
    uint64_t result=original(object); /* exactly once, same argument and result */
    InterlockedIncrement64(&created_calls);
    if(InterlockedCompareExchange(&created_recording,0,0)) {
        DWORD error=GetLastError();created_note_with(object,created_thread_class());SetLastError(error);
    } else if(InterlockedCompareExchange(&created_load_recording,0,0)) {
        DWORD error=GetLastError();created_load_note(object);SetLastError(error);
    }
    return result;
}
#ifndef CREATED_OWN_PLATFORM
static __attribute__((noinline)) uint64_t created_assign(void *object) {
    if(rid_mode) return rid_assign(object,__builtin_return_address(0));
    return created_assign_execute((created_assign_fn)(thunk+CREATED_THUNK),object);
}
#endif

/* Boundary side: classification, macro and parent of a still-present object. */
static unsigned created_classify(uintptr_t vtable) {
    unsigned slot=(unsigned)lockstep_mix(vtable)&(CREATED_CLASS_CACHE-1u);
    for(unsigned probe=0;probe<CREATED_CLASS_CACHE;probe++,slot=(slot+1u)&(CREATED_CLASS_CACHE-1u)) {
        if(created_classes[slot].vtable==vtable) return created_classes[slot].kind;
        if(!created_classes[slot].vtable) break;
    }
    uint32_t words[8];unsigned kind=CREATED_KIND_OTHER;
    if(world_class_words(vtable,words) && world_class_has(words,22)) {
        kind=CREATED_KIND_COMPONENT;
        for(unsigned i=0;i<sizeof(created_excluded_classes)/sizeof(*created_excluded_classes);i++)
            if(world_class_has(words,created_excluded_classes[i])) kind=CREATED_KIND_EXCLUDED;
    }
    if(!created_classes[slot].vtable) created_classes[slot]=(created_class_t){vtable,kind};
    return kind;
}
#ifdef CREATED_OWN_PLATFORM
static int created_macro(const unsigned char *object,char name[96],uint64_t *hash);
#else
/* 1 named (hash of the name), 0 no macro (hash 0), -1 unreadable (hash 1).
 * U::Macro vtable 0x2ba5430, slot 18 0xefb10 returns the MSVC string +0x20. */
static int created_macro(const unsigned char *object,char name[96],uint64_t *hash) {
    unsigned char *macro=NULL;void *vtable=NULL,*getter=NULL;uint64_t length=0,capacity=0;const unsigned char *data;
    name[0]=0;*hash=1;
    if(!created_read(object+0x60,&macro,8)) return -1;
    if(!macro) {*hash=0;return 0;}
    if(!created_read(macro,&vtable,8) || vtable!=image+0x2ba5430 || !created_read((unsigned char*)vtable+18*8,&getter,8) ||
       getter!=image+0xefb10 || !created_read(macro+0x30,&length,8) || !length || length>=96 ||
       !created_read(macro+0x38,&capacity,8) || capacity<length) return -1;
    data=macro+0x20;
    if(capacity>=16 && (!created_read(data,&data,8) || !data)) return -1;
    if(!created_read(data,name,(size_t)length)) {name[0]=0;return -1;}
    name[length]=0;
    for(uint64_t i=0;i<length;i++) {char c=name[i];if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_')) {name[0]=0;return -1;}}
    *hash=created_text_hash(name);return 1;
}
#endif
static int created_present(world_hash_find_fn find,void *map,uint64_t runtime,unsigned char **out) {
    unsigned char *object=find(map,runtime,4);uint64_t id=0;
    if(out) *out=object;
    return object && created_read(object+8,&id,8) && id==runtime;
}
static void created_row(unsigned kind,uint32_t tick,uint64_t id,uint64_t parent,uint32_t type,uint32_t ordinal,uint32_t thread,const unsigned char *object) {
    if(!created_ring || !InterlockedCompareExchange(&created_rows_armed,0,0)) return;
    LONG64 index=InterlockedCompareExchange64(&created_row_cursor,0,0);
    created_row_t *r=&created_ring[(uint64_t)index%CREATED_LOG];uint64_t ignored;
    *r=(created_row_t){id,parent,kind,tick,type,ordinal,thread,{0}};
    if(object && created_macro(object,r->macro,&ignored)<0) strcpy(r->macro,"?");
    InterlockedExchange64(&created_row_cursor,index+1);
}
/* Destroyed identities leave the table; capture rows keep them as absent. */
static void created_sweep(uint64_t tick,world_hash_find_fn find,void *map) {
    for(unsigned i=0;i<created_live_count;) {
        if(created_present(find,map,created_live[i].runtime,NULL)) {++i;continue;}
        created_fold(tick,created_live[i].created,2);created_row(CREATED_ROW_RETIRE,(uint32_t)tick,created_live[i].created,0,0,0,0,NULL);
        ++created_retired;created_live_remove(i);
    }
}
/* 1 resolved into *parent, 0 waits for a parent pending in this frame. */
static int created_parent(const created_candidate_t *c,uint64_t *parent) {
    if(!c->parent_runtime) {*parent=0;return 1;}
    if(c->parent_runtime!=UINT64_MAX) {
        uint64_t id=world_identity_lookup(c->parent_runtime);
        if(!id) id=created_find_runtime(c->parent_runtime);
        if(id) {*parent=id;return 1;}
        created_slot_t *s=created_map_slot(created_parents,c->parent_runtime);
        if(s->stamp==created_stamp && !created_candidates[s->value].assigned) return 0;
    }
    *parent=CREATED_ORPHAN;++created_orphans;return 1;
}
static int created_give(uint64_t tick,created_candidate_t *c,uint64_t parent) {
    uint64_t key=created_key(parent,c->type,c->macro);
    created_slot_t *s=created_map_slot(created_ordinals,key);
    if(s->stamp!=created_stamp) {s->key=key;s->value=0;s->stamp=created_stamp;}
    uint32_t ordinal=s->value++;
    for(uint32_t salt=0;salt<CREATED_SALTS;salt++) {
        uint64_t id=created_make_id(tick,key,ordinal,salt);
        if(created_find_id(id)) {++created_rehash;continue;}
        if(!created_live_add(c->runtime,id,(uint32_t)tick)) return created_fail("live_full");
        c->created=id;c->assigned=1;++created_assigned;++created_last_assigned;
        created_fold(tick,id,parent^((uint64_t)c->type<<32)^ordinal);
        created_row(CREATED_ROW_ASSIGN,(uint32_t)tick,id,parent,c->type,ordinal,CREATED_MOVEWORKER,(const unsigned char*)c->object);
        return 1;
    }
    return created_fail("collision");
}
static int created_resolve(uint64_t tick,const created_note_t *batch,unsigned count,world_hash_find_fn find,void *map) {
    if(!++created_stamp) {
        memset(created_parents,0,sizeof(*created_parents)*CREATED_BATCH_MAP);
        memset(created_ordinals,0,sizeof(*created_ordinals)*CREATED_BATCH_MAP);created_stamp=1;
    }
    unsigned n=0;created_last_assigned=0;
    for(unsigned i=0;i<count;i++) {
        unsigned char *object=NULL,*parent=NULL;uintptr_t vtable=0;uint64_t runtime=batch[i].runtime;
        if(!created_present(find,map,runtime,&object) || !created_read(object,&vtable,8)) {++created_transient;continue;}
        unsigned kind=created_classify(vtable);
        if(kind==CREATED_KIND_OTHER) {++created_other;continue;}
        if(kind==CREATED_KIND_EXCLUDED) {++created_excluded;continue;}
        if(world_identity_lookup(runtime)) {++created_imported;continue;}
        if(created_find_runtime(runtime)) {++created_duplicate;continue;}
        uint32_t type=CREATED_TYPE_OF(vtable);
        if(batch[i].thread!=CREATED_MOVEWORKER) {
            if(batch[i].thread==CREATED_MAIN) ++created_foreign_main;else ++created_foreign_other;
            created_row(CREATED_ROW_FOREIGN,(uint32_t)tick,0,0,type,0,batch[i].thread,object);continue;
        }
        created_candidate_t *c=&created_candidates[n];char name[96];
        *c=(created_candidate_t){.runtime=runtime,.object=(uintptr_t)object,.type=type};
        if(created_macro(object,name,&c->macro)<0) ++created_macro_unknown;
        if(!created_read(object+0x70,&parent,8)) c->parent_runtime=UINT64_MAX;
        else if(parent && (!created_read(parent+8,&c->parent_runtime,8) || !c->parent_runtime)) c->parent_runtime=UINT64_MAX;
        created_slot_t *s=created_map_slot(created_parents,runtime);
        if(s->stamp==created_stamp) {++created_duplicate;continue;}
        s->key=runtime;s->value=n++;s->stamp=created_stamp;
    }
    /* Parents first; within a pass, creation order. The resulting order (and
     * so every ordinal) depends only on the MoveWorker creation order and the
     * parent links, never on runtime IDs or addresses. */
    for(int progress=1;progress;) {
        progress=0;
        for(unsigned i=0;i<n;i++) {
            created_candidate_t *c=&created_candidates[i];uint64_t parent;
            if(c->assigned || !created_parent(c,&parent)) continue;
            if(!created_give(tick,c,parent)) return 0;
            progress=1;
        }
    }
    for(unsigned i=0;i<n;i++) if(!created_candidates[i].assigned) { /* parent cycle: not a tree */
        ++created_orphans;if(!created_give(tick,&created_candidates[i],CREATED_ORPHAN)) return 0;
    }
    return 1;
}
/* Gate boundary, main thread, MoveWorker joined. tick = the frame just run. */
static int created_frame_with(uint64_t tick,world_hash_find_fn find,void *map) {
    if(!InterlockedCompareExchange(&created_recording,0,0)) return 1;
    if(InterlockedCompareExchange64(&created_failures,0,0)) return 0;
    if(tick>CREATED_TICK_LIMIT) return created_fail("tick_range");
    if(!map) return created_fail("map");
    AcquireSRWLockExclusive(&created_pending_lock);
    created_note_t *batch=created_pending;unsigned count=created_pending_count;
    created_pending=created_batch;created_batch=batch;created_pending_count=0;
    ReleaseSRWLockExclusive(&created_pending_lock);
    if(InterlockedCompareExchange64(&created_pending_overflow,0,0)) return created_fail("pending_overflow");
    AcquireSRWLockExclusive(&created_lock);
    uint64_t changes=created_assigned+created_retired;
    created_sweep(tick,find,map);
    int ok=created_resolve(tick,batch,count,find,map);
    /* Consumers that cache lookups (world_identity_generation) revalidate. */
    if(created_assigned+created_retired!=changes) InterlockedIncrement64(&world_created_generation);
    ReleaseSRWLockExclusive(&created_lock);
    ++created_frames;created_last_tick=tick;
    if(ok) created_stage="frame";
    return ok;
}
#ifndef CREATED_OWN_PLATFORM
static int created_frame(uint64_t tick) {
    return created_frame_with(tick,(world_hash_find_fn)(image+0xced70),*(void**)(image+0x6cf1500));
}
#endif
/* A load note that would become a created ID: present, a Component, not
 * excluded, neither imported nor already created. */
static int created_load_candidate(world_hash_find_fn find,void *map,uint64_t runtime) {
    unsigned char *object=NULL;uintptr_t vtable=0;
    return runtime && created_present(find,map,runtime,&object) && created_read(object,&vtable,8) &&
        created_classify(vtable)==CREATED_KIND_COMPONENT && !world_identity_lookup(runtime) && !created_find_runtime(runtime);
}
/* The seal: give load-created objects created IDs (tick 0). 1 also when the
 * set is skipped; 0 only for a failure of the shared assignment (stop 96). */
static int created_load_resolve_with(world_hash_find_fn find,void *map) {
    InterlockedExchange(&created_load_recording,0);MemoryBarrier();
    if(created_load_done) return 1;
    created_load_done=1;
    LONG64 noted=InterlockedCompareExchange64(&created_load_noted,0,0);
    if(!created_load || !map || InterlockedCompareExchange64(&created_load_overflow,0,0) || noted>(LONG64)CREATED_LOAD_NOTES) {
        created_load_skipped=1;return 1;
    }
    unsigned count=(unsigned)noted;uint32_t ids[CREATED_LOAD_THREADS];uint64_t per[CREATED_LOAD_THREADS];
    memset(ids,0,sizeof(ids));memset(per,0,sizeof(per));created_load_threads=0;created_load_candidates=0;
    for(unsigned i=0;i<count;i++) {
        created_note_t *note=&created_load[i];note->reserved=0;
        if(!created_load_candidate(find,map,note->runtime)) continue;
        unsigned t=0;while(t<created_load_threads && ids[t]!=note->thread) ++t;
        if(t==created_load_threads) {
            if(created_load_threads==CREATED_LOAD_THREADS) {++created_load_candidates;continue;}
            ids[created_load_threads++]=note->thread;
        }
        ++per[t];++created_load_candidates;note->reserved=t+1u;
    }
    unsigned dominant=0;for(unsigned t=1;t<created_load_threads;t++) if(per[t]>per[dominant]) dominant=t;
    uint64_t chosen=created_load_threads?per[dominant]:0;created_load_foreign=created_load_candidates-chosen;
    if(chosen>CREATED_LOAD_MAX) {created_load_skipped=1;return 1;}
    /* Compact the chosen thread's notes in place, in creation order. */
    unsigned n=0;
    for(unsigned i=0;i<count;i++) if(created_load[i].reserved==dominant+1u) {
        created_load[n]=(created_note_t){created_load[i].runtime,CREATED_MOVEWORKER,0};++n;
    }
    AcquireSRWLockExclusive(&created_lock);
    uint64_t before=created_assigned;
    int ok=created_resolve(0,created_load,n,find,map);
    created_load_assigned=created_assigned-before;
    if(created_load_assigned) InterlockedIncrement64(&world_created_generation);
    ReleaseSRWLockExclusive(&created_lock);
    if(ok) created_stage="load";
    return ok;
}
/* World capture: every live identity present now and never captured becomes
 * a permanent row; rows are kept sorted by ID (all above the saved IDs). */
static int created_capture_rows(world_hash_find_fn find,void *map,world_hash_item *items,unsigned *count,unsigned capacity) {
    unsigned before=*count;int ok=1;
    AcquireSRWLockExclusive(&created_lock);
    for(unsigned i=0;i<created_live_count;i++) {
        created_live_t *e=&created_live[i];
        if(e->captured || !created_present(find,map,e->runtime,NULL)) continue;
        if(*count>=capacity) {ok=0;break;}
        memset(&items[*count],0,sizeof(*items));items[*count].runtime=e->runtime;items[*count].saved=e->created;
        ++*count;e->captured=1;
    }
    ReleaseSRWLockExclusive(&created_lock);
    if(!ok) return created_fail("capture_rows_full");
    if(*count>before) qsort(items,*count,sizeof(*items),world_hash_compare);
    return 1;
}

/* Installation, owned by the gate session. */
#ifdef CREATED_OWN_PLATFORM
static void *created_alloc(size_t bytes);
#else
static int created_pins(void) {
    return hash_ok(image+CREATED_ENTRY,268,"5ead430da168fdfe1f4dfb0eb328b7dd6b04cf1dc6a5ee56ea3bcfb133916bd2") &&
           hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36");
}
static int created_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+CREATED_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+CREATED_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+CREATED_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+CREATED_ENTRY,8);return actual==expected;
}
static void *created_alloc(size_t bytes) {return VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
#endif
static int created_allocate(int rows) {
    if(!created_pending) created_pending=created_alloc(sizeof(*created_pending)*CREATED_PENDING);
    if(!created_batch) created_batch=created_alloc(sizeof(*created_batch)*CREATED_PENDING);
    if(!created_live) created_live=created_alloc(sizeof(*created_live)*CREATED_LIVE);
    if(!created_by_runtime) created_by_runtime=created_alloc(sizeof(*created_by_runtime)*CREATED_INDEX);
    if(!created_by_id) created_by_id=created_alloc(sizeof(*created_by_id)*CREATED_INDEX);
    if(!created_candidates) created_candidates=created_alloc(sizeof(*created_candidates)*CREATED_PENDING);
    if(!created_parents) created_parents=created_alloc(sizeof(*created_parents)*CREATED_BATCH_MAP);
    if(!created_ordinals) created_ordinals=created_alloc(sizeof(*created_ordinals)*CREATED_BATCH_MAP);
    if(rows && !created_ring) created_ring=created_alloc(sizeof(*created_ring)*CREATED_LOG);
    return created_pending && created_batch && created_live && created_by_runtime && created_by_id && created_candidates &&
           created_parents && created_ordinals && (!rows || created_ring);
}
#ifndef CREATED_OWN_PLATFORM
static int created_arm(int rows) {
    /* A loading-UI reload reruns configuration: accept the installed owner. */
    if(created_armed) return stats.active && created_installed && (!rows || created_rows_armed);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !world_identities || !thunk || !created_pins()) return 0;
    if(!thunk_slot_ready(448)) return 0;
    intptr_t distance=(intptr_t)(thunk+CREATED_THUNK+32)-(intptr_t)(image+CREATED_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX || !created_allocate(rows)) return 0;
    static const unsigned char native[8]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74};
    memcpy(&created_original_word,image+CREATED_ENTRY,8);
    if(memcmp(&created_original_word,native,8)) return 0;
    created_patched_word=created_original_word;
    unsigned char *patch=(unsigned char*)&created_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!created_swap(created_original_word,created_patched_word)) return 0;
    created_installed=1;created_stage="armed";
    if(rows) InterlockedExchange(&created_rows_armed,1);
    InterlockedExchange(&created_armed,1);return 1;
}
#endif
/* native_gate_created_load: after the created identities, before loading. */
static int created_load_arm(void) {
    if(created_load_armed) return stats.active && created_armed && created_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !created_armed || !created_installed) return 0;
    if(!created_load && !(created_load=created_alloc(sizeof(*created_load)*CREATED_LOAD_NOTES))) return 0;
    InterlockedExchange(&created_load_armed,1);InterlockedExchange(&created_load_recording,1);return 1;
}
/* At the seal (first live frame): the load's imports are over. Load-created
 * objects get their IDs first; 0 stops the gate (shared assignment failed). */
static int created_begin_with(world_hash_find_fn find,void *map) {
    int ok=1;
    if(created_armed && created_installed && created_load_armed) ok=created_load_resolve_with(find,map);
    if(created_armed && created_installed) {InterlockedExchange(&created_recording,1);if(ok) created_stage="recording";}
    return ok;
}
#ifndef CREATED_OWN_PLATFORM
static int created_begin(void) {
    return created_begin_with((world_hash_find_fn)(image+0xced70),created_load_armed?*(void**)(image+0x6cf1500):NULL);
}
#endif
static void created_stop(void) {InterlockedExchange(&created_recording,0);InterlockedExchange(&created_load_recording,0);}
/* native_gate_portable_created: the ordering/keying modules use created IDs
 * (world_identity_key in lockstep_world_identity.h). Armed after the created
 * identities and before loading; it stays set for the session (a module's
 * own switch and restore still decide whether that module runs at all). */
static int portable_created_arm(void) {
    if(world_portable_created) return stats.active && created_armed && created_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !created_armed || !created_installed) return 0;
    InterlockedExchange(&world_portable_created,1);return 1;
}
#ifndef CREATED_OWN_PLATFORM
static void created_restore(void) {
    created_stop();
    if(!created_installed) return;
    created_restored=created_swap(created_patched_word,created_original_word);
    if(created_restored) created_installed=0;
    /* Tables stay allocated until process exit: Lua may query after STOP and
     * an in-flight hook may still append to the pending list. */
}
#endif
static int created_status(char *out,size_t capacity) {
    LONG64 cursor=InterlockedCompareExchange64(&created_row_cursor,0,0);
    int n=snprintf(out,capacity,"armed=%ld installed=%d recording=%ld restored=%d resolved_tick=%llu frames=%llu calls=%lld noted=%lld "
        "assigned=%llu last=%u live=%u peak=%u retired=%llu transient=%llu other=%llu excluded=%llu foreign_main=%llu foreign_other=%llu "
        "orphans=%llu macro_unknown=%llu rehash=%llu imported=%llu duplicate=%llu unreadable=%lld overflow=%lld digest=%016llx "
        "rows_armed=%ld rows=%lld row_overwritten=%lld stage=%s failures=%lld portable=%ld generation=%lld"
        " load=%ld load_noted=%lld load_overflow=%lld load_candidates=%llu load_threads=%u load_foreign=%llu load_assigned=%llu"
        " load_skipped=%d",
        created_armed,created_installed,created_recording,created_restored,(unsigned long long)created_last_tick,
        (unsigned long long)created_frames,(long long)created_calls,(long long)created_noted,(unsigned long long)created_assigned,
        created_last_assigned,created_live_count,created_peak,(unsigned long long)created_retired,(unsigned long long)created_transient,
        (unsigned long long)created_other,(unsigned long long)created_excluded,(unsigned long long)created_foreign_main,
        (unsigned long long)created_foreign_other,(unsigned long long)created_orphans,(unsigned long long)created_macro_unknown,
        (unsigned long long)created_rehash,(unsigned long long)created_imported,(unsigned long long)created_duplicate,
        (long long)created_unreadable,(long long)created_pending_overflow,(unsigned long long)created_digest,created_rows_armed,
        (long long)cursor,(long long)(cursor>(LONG64)CREATED_LOG?cursor-(LONG64)CREATED_LOG:0),created_stage,(long long)created_failures,
        world_portable_created,(long long)world_created_generation,created_load_armed,(long long)created_load_noted,
        (long long)created_load_overflow,(unsigned long long)created_load_candidates,created_load_threads,
        (unsigned long long)created_load_foreign,(unsigned long long)created_load_assigned,created_load_skipped);
    return n>0 && (size_t)n<capacity?n:0;
}
/* One row per call; the probe walks absolute indices until nil. */
static int created_row_text(uint64_t index,char *out,size_t capacity) {
    static const char *const threads[3]={"moveworker","main","other"};
    LONG64 cursor=InterlockedCompareExchange64(&created_row_cursor,0,0);
    if(!created_ring || (LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)CREATED_LOG && index<(uint64_t)(cursor-(LONG64)CREATED_LOG))
        n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const created_row_t *r=&created_ring[index%CREATED_LOG];
        if(r->kind==CREATED_ROW_ASSIGN)
            n=snprintf(out,capacity,"index=%llu kind=assign tick=%u id=%016llx parent=%016llx type=%x ordinal=%u macro=%s",
                (unsigned long long)index,r->tick,(unsigned long long)r->id,(unsigned long long)r->parent,r->type,r->ordinal,r->macro[0]?r->macro:"-");
        else if(r->kind==CREATED_ROW_RETIRE)
            n=snprintf(out,capacity,"index=%llu kind=retire tick=%u id=%016llx",(unsigned long long)index,r->tick,(unsigned long long)r->id);
        else n=snprintf(out,capacity,"index=%llu kind=foreign tick=%u thread=%s type=%x macro=%s",
                (unsigned long long)index,r->tick,threads[r->thread<3?r->thread:2],r->type,r->macro[0]?r->macro:"-");
    }
    return n>0 && (size_t)n<capacity?n:0;
}
