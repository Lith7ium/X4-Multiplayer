/* Census experiment, DEFAULT OFF. In c003 every timing-only divergence was the
 * NavigationController refresh stamp (+0xc0 game time, +0xc8 pathing bank).
 * RefreshFromPathingState 0xea2a20 is its only writer: on the first Move
 * (0xea77f0) or transform query (GetTransformImpl 0xea5a90) after a bank flip,
 * it stores the CALLING thread's TLS game time (TLS+0x300) and the read bank
 * (0x38f4ee4), then posts transition (0xea35e0) and waypoint events. Low
 * attention ships are never Moved, so they are stamped only when MoveWorker
 * code happens to query them; c003 shows whole zones stamped at unrelated
 * frames on the two clients.
 * Bit 1: after the serial pathing join, stamp every batched agent's controller
 * once per frame, in runtime-ID order, at the frame's own game time. Later
 * queries then find the stamp current and change nothing. The MoveWorker TLS
 * time words are bracketed around the engine's own SyncThreadGameTime and are
 * restored before the native frame continues.
 * Bit 2: lift the 1 ms QPC budget (manager+0x218, ctor 0xeabb8d) so UpdateBatch
 * 0xeaecc0 never switches agents to cheap mode or rotates a batch cursor on
 * wall-clock time. Both are restored on stop; failures stop the gate.
 * Order key (class, key): class 0 saved ID, class 2 the flagged runtime key
 * 1<<63|runtime of an object without a portable identity (process local), the
 * class taken from the key's top bit so (class, key) is exactly the former key
 * order. With native_gate_portable_created a created object is class 1 with
 * its created ID: saved agents keep their order, created ones follow them in
 * created-ID order, unportable ones come last; the order and stamp hashes
 * cover classes 0 and 1 (lockstep_world_identity.h). */
#include <stdlib.h>
/* The Linux gate (linux/gate_path.h) defines these for its executable and its own
 * frame and pins; the walk, the order, the stamping and the budget are shared. */
#ifndef PATH_DET_MANAGER
#define PATH_DET_MANAGER 0x6cf21c8u
#define PATH_DET_REFRESH 0xea2a20u
#define PATH_DET_SYNC 0x14b8e20u
#define PATH_DET_NAV_VTABLE 0x2c51158u
#define PATH_DET_READ_BANK 0x38f4ee4u
#define PATH_DET_BUDGET 0x218u
#define PATH_DET_BATCH_BEGIN 0x180
#define PATH_DET_BATCH_END 0x188
#define PATH_DET_AGENT_OWNER 0xef0
#define PATH_DET_AGENT_SLOTS 0xad0
#define PATH_DET_NAV_OWNER 0x28
#define PATH_DET_NAV_SLOTS 0x30
#define PATH_DET_NAV_AGENT 0x40
#define PATH_DET_TLS_TIME 0x300
#define PATH_DET_TLS_PREVIOUS 0x328
#endif
#define PATH_DET_BUDGET_NATIVE 0x3f50624dd2f1a9fcULL /* 0.001 s */
#define PATH_DET_BUDGET_LIFTED 0x41cdcd6500000000ULL /* 1e9 s */
#define PATH_DET_CAPACITY 131072u
enum {PATH_DET_REFRESH_BIT=1,PATH_DET_BUDGET_BIT=2};
typedef void (*path_det_refresh_fn)(void *);
typedef void (*path_det_sync_fn)(void);
typedef struct {uint64_t id;unsigned char *controller;uint32_t klass;} path_det_item;
enum {PATH_DET_SAVED=0,PATH_DET_CREATED=1,PATH_DET_LOCAL=2}; /* item classes */
static path_det_item path_det_items[PATH_DET_CAPACITY];
static volatile LONG path_det_armed;
static unsigned path_det_mask,path_det_last_count,path_det_last_bank=2;
static unsigned char *path_det_budget_manager;
static int path_det_budget_applied,path_det_budget_restored;
static uint64_t path_det_frames,path_det_stamped,path_det_current,path_det_stale,path_det_skipped,path_det_budget_frames;
static uint64_t path_det_order_hash,path_det_stamp_hash,path_det_last_time,path_det_unmapped,path_det_created;
/* The batches as walked: list order, batch sizes and cursor positions by saved ID. It must
 * agree between clients for native_gate_path_budget, whose decision follows exactly these. */
static uint64_t path_det_walk_hash;
static volatile LONG64 path_det_failures;
static const char *path_det_stage="idle";
static uint64_t path_det_fnv(uint64_t hash,const void *data,size_t size) {
    const unsigned char *p=data;
    for(size_t i=0;i<size;i++) {hash^=p[i];hash*=0x100000001b3ULL;}
    return hash;
}
static int path_det_item_order(const void *a,const void *b) {
    const path_det_item *p=a,*q=b;
    if(p->klass!=q->klass) return p->klass<q->klass?-1:1;
    return p->id<q->id?-1:p->id>q->id;
}
/* Per-member memory of the previous walk, in traversal order. It serves as
 * the saved-ID cache and as proof that the sorted item list is unchanged.
 * A cached saved ID is reused only for the same Agent, Object and runtime ID
 * and the same identity table generation: recorded mappings never change and
 * a miss can only become a hit when the table grows, which bumps the
 * generation. Entries are rewritten on every walk; nothing is trusted blindly.
 * saved/klass hold the portable key and its WORLD_KEY_* class; the generation
 * also changes with the created table (world_identity_generation). */
typedef struct {unsigned char *node,*agent,*object,*controller;uint64_t runtime,saved;uint16_t admitted,klass;uint32_t gen;} path_det_member;
static path_det_member path_det_seq[PATH_DET_CAPACITY];
static unsigned path_det_seq_count,path_det_cache_count,path_det_gen=1;
static int path_det_seq_valid,path_det_items_valid;
static unsigned char *path_det_seq_manager;
static const void *path_det_id_table;static LONG64 path_det_id_count=-1;static uint64_t path_det_id_created;
static uint64_t path_det_reuses,path_det_rebuilds,path_det_lookups,path_det_id_hits;
static uint64_t path_det_collect_us,path_det_sort_us,path_det_refresh_us,path_det_total_us,path_det_max_total_us;
static uint64_t path_det_elapsed_us(LARGE_INTEGER from,LARGE_INTEGER to) {
    LARGE_INTEGER f;QueryPerformanceFrequency(&f);
    return f.QuadPart && to.QuadPart>=from.QuadPart?(uint64_t)((to.QuadPart-from.QuadPart)*1000000/f.QuadPart):0;
}
static void path_det_id_epoch(const void **table,LONG64 *count,uint64_t *created,unsigned *gen) {
    LONG64 now=InterlockedCompareExchange64(&world_identity_count,0,0);uint64_t made=world_identity_generation();
    if(*table!=(const void*)world_identities || *count!=now || *created!=made) {
        *table=world_identities;*count=now;*created=made;if(!++*gen) *gen=1;
    }
}
static uint64_t path_det_saved(const path_det_member *e,unsigned gen,const unsigned char *agent,const unsigned char *object,uint64_t runtime,
                               unsigned *klass,uint64_t *lookups,uint64_t *hits) {
    if(e->gen==gen && e->agent==agent && e->object==object && e->runtime==runtime) {++*hits;*klass=e->klass;return e->saved;}
    ++*lookups;return world_identity_key(runtime,klass);
}
/* Pinned layout (RunPass 0xeae3c0, UpdateBatch, AssignAgentToBatch 0xeaf2b0):
 * vectors +0x180 and +0x198 of 0x20-byte batches, member count at +8 and a
 * circular sentinel list at +0x10 (node+8 next, node+0x10 agent). Only exact
 * NavigationController owners with the world_nav link shape are admitted.
 * The admission reads are the same every frame; *same reports that every
 * member, link and runtime ID matches the previous walk exactly. */
static int path_det_walk(unsigned char *manager,const unsigned char *nav_vtable,uint64_t *skipped,uint64_t *unmapped,uint64_t *created,int *same) {
    unsigned k=0;uint64_t skip=0,missing=0,made=0;int ok=1,unchanged=path_det_seq_valid && manager==path_det_seq_manager;
    unsigned previous=path_det_seq_valid?path_det_seq_count:0;uint64_t walk=0x57414c4b4f524445ULL;
    path_det_id_epoch(&path_det_id_table,&path_det_id_count,&path_det_id_created,&path_det_gen);
    __try {
        for(unsigned v=0;v<2 && ok;v++) {
            unsigned char *begin=*(unsigned char**)(manager+PATH_DET_BATCH_BEGIN+v*0x18),*end=*(unsigned char**)(manager+PATH_DET_BATCH_END+v*0x18);
            if(end<begin || (uintptr_t)(end-begin)%0x20) {ok=0;break;}
            for(unsigned char *batch=begin;batch<end && ok;batch+=0x20) {
                uint64_t members=*(uint64_t*)(batch+8);unsigned char *sentinel=*(unsigned char**)(batch+0x10),*node;
                const unsigned char *cursor=*(unsigned char**)(batch+0x18);
                if(!sentinel || members>PATH_DET_CAPACITY) {ok=0;break;}
                node=*(unsigned char**)(sentinel+8);
                walk=(walk+members)*0x9e3779b97f4a7c15ULL;walk^=walk>>29;
                for(uint64_t i=0;i<members;i++,node=*(unsigned char**)(node+8)) {
                    if(node==sentinel || k>=PATH_DET_CAPACITY) {ok=0;break;}
                    if(k+8<previous) { /* the previous walk's addresses are a good guess */
                        const path_det_member *ahead=&path_det_seq[k+8];
                        __builtin_prefetch(ahead->node);if(ahead->agent) __builtin_prefetch(ahead->agent+PATH_DET_AGENT_OWNER);
                        if(ahead->object) {__builtin_prefetch(ahead->object);__builtin_prefetch(ahead->object+0x200);}
                        if(ahead->controller) {__builtin_prefetch(ahead->controller);__builtin_prefetch(ahead->controller+PATH_DET_NAV_AGENT);}
                    }
                    unsigned char *agent=*(unsigned char**)(node+0x10),*object=NULL,*controller=NULL;uint64_t id=0;uint32_t admitted=0;
                    if(agent && (object=*(unsigned char**)(agent+PATH_DET_AGENT_OWNER)) && (controller=*(unsigned char**)(object+0x200)) &&
                       *(const unsigned char**)controller==nav_vtable && *(unsigned char**)(controller+PATH_DET_NAV_OWNER)==object &&
                       *(unsigned char**)(controller+PATH_DET_NAV_AGENT)==agent && *(unsigned char**)(controller+PATH_DET_NAV_SLOTS)==agent+PATH_DET_AGENT_SLOTS &&
                       (id=*(uint64_t*)(object+8))) admitted=1;
                    path_det_member *e=&path_det_seq[k];
                    if(unchanged && (k>=previous || e->node!=node || e->agent!=agent || e->object!=object || e->controller!=controller ||
                       e->runtime!=id || e->admitted!=admitted || e->gen!=path_det_gen)) unchanged=0;
                    uint64_t saved=0;unsigned klass=WORLD_KEY_NONE;
                    if(admitted) {
                        saved=path_det_saved(e,path_det_gen,agent,object,id,&klass,&path_det_lookups,&path_det_id_hits);
                        if(!saved) ++missing;else if(klass==WORLD_KEY_CREATED) ++made;
                    } else ++skip;
                    e->node=node;e->agent=agent;e->object=object;e->controller=controller;e->runtime=id;e->saved=saved;
                    e->admitted=(uint16_t)admitted;e->klass=(uint16_t)klass;e->gen=path_det_gen;
                    walk=(walk^saved^(uint64_t)(node==cursor))*0x9e3779b97f4a7c15ULL;walk^=walk>>29;
                    ++k;
                }
                if(ok && node!=sentinel) ok=0; /* The member count must close the ring. */
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    if(k!=previous) unchanged=0;
    path_det_seq_count=ok?k:0;path_det_seq_valid=ok;path_det_seq_manager=manager;path_det_walk_hash=walk;
    *skipped=skip;*unmapped=missing;*created=made;*same=ok && unchanged;return ok;
}
/* Same items in the same order as collecting from scratch: rebuilt and sorted
 * only when the walk differs from the previous one in any member or link.
 * Returns 2 for a duplicate key, which fails the frame like before. */
static int path_det_collect(unsigned char *manager,const unsigned char *nav_vtable,unsigned *count,uint64_t *skipped) {
    uint64_t unmapped=0,created=0;int same=0;*count=0;path_det_sort_us=0;
    if(!path_det_walk(manager,nav_vtable,skipped,&unmapped,&created,&same)) {path_det_items_valid=0;return 0;}
    /* Runtime IDs are process-local; order by the portable saved ID.
     * Objects absent from the imported save keep a flagged runtime key;
     * created objects (portable keys on) their created ID in class 1. */
    path_det_unmapped+=unmapped;path_det_created+=created;
    if(same && path_det_items_valid) {*count=path_det_cache_count;++path_det_reuses;return 1;}
    LARGE_INTEGER sort_start,sort_end;QueryPerformanceCounter(&sort_start);unsigned n=0;
    for(unsigned k=0;k<path_det_seq_count;k++) {
        const path_det_member *e=&path_det_seq[k];if(!e->admitted) continue;
        path_det_item *item=&path_det_items[n++];item->controller=e->controller;
        if(e->klass==WORLD_KEY_CREATED) {item->id=e->saved;item->klass=PATH_DET_CREATED;}
        else {item->id=e->saved?e->saved:(1ULL<<63)|e->runtime;item->klass=item->id>>63?PATH_DET_LOCAL:PATH_DET_SAVED;}
    }
    qsort(path_det_items,n,sizeof(path_det_items[0]),path_det_item_order);
    path_det_items_valid=0;path_det_cache_count=n;*count=n;++path_det_rebuilds;
    for(unsigned i=1;i<n;i++) if(path_det_items[i].id==path_det_items[i-1].id && path_det_items[i].klass==path_det_items[i-1].klass) {
        path_det_seq_valid=0;return 2;
    }
    QueryPerformanceCounter(&sort_end);path_det_sort_us=path_det_elapsed_us(sort_start,sort_end);
    path_det_items_valid=1;return 1;
}
static int path_det_refresh_frame(path_det_refresh_fn refresh,path_det_sync_fn sync,unsigned char *manager,
                                  unsigned char *tls,const unsigned char *nav_vtable,const volatile uint32_t *read_bank) {
    unsigned count=0;uint64_t skipped=0;LARGE_INTEGER t0,t1,t2;QueryPerformanceCounter(&t0);
    path_det_stage="collect";if(!manager || !tls) return 0;
    int collected=path_det_collect(manager,nav_vtable,&count,&skipped);
    QueryPerformanceCounter(&t1);path_det_collect_us=path_det_elapsed_us(t0,t1);
    path_det_collect_us=path_det_collect_us>path_det_sort_us?path_det_collect_us-path_det_sort_us:0; /* walk only */
    if(!collected) return 0;
    path_det_stage="duplicate";if(collected==2) return 0;
    uint64_t saved_time,saved_previous;memcpy(&saved_time,tls+PATH_DET_TLS_TIME,8);memcpy(&saved_previous,tls+PATH_DET_TLS_PREVIOUS,8);
    int ok=0;
    __try {
        double before,now;memcpy(&before,&saved_time,8);
        sync();memcpy(&now,tls+PATH_DET_TLS_TIME,8);
        uint32_t bank=*read_bank;path_det_stage="time";
        if(isfinite(now) && now>before && bank<=1) {
            uint64_t order=0xcbf29ce484222325ULL,stamp=order,stamped=0,current=0,stale=0;path_det_stage="refresh";
            for(unsigned i=0;i<count;i++) {
                if(i+8<count) {unsigned char *ahead=path_det_items[i+8].controller;__builtin_prefetch(ahead);__builtin_prefetch(ahead+0xc0);}
                unsigned char *c=path_det_items[i].controller;
                uint32_t cursor=*(volatile uint32_t*)(c+0xc8);
                refresh(c);
                uint64_t when;memcpy(&when,c+0xc0,8);uint32_t after=*(volatile uint32_t*)(c+0xc8);double t;memcpy(&t,&when,8);
                if(cursor==bank) ++current;else if(after==bank && t==now) ++stamped;else ++stale;
                if(path_det_items[i].klass!=PATH_DET_LOCAL) { /* Only portable keys are comparable across clients. */
                    order=path_det_fnv(order,&path_det_items[i].id,8);
                    /* Hash the stamp relative to the live bank: the physical phase may differ
                     * between clients and the engine treats the two banks symmetrically. */
                    uint32_t relation=after==bank?0:after==2?2:1;
                    stamp=path_det_fnv(path_det_fnv(path_det_fnv(stamp,&path_det_items[i].id,8),&when,8),&relation,4);
                }
            }
            path_det_stamped+=stamped;path_det_current+=current;path_det_stale+=stale;path_det_skipped+=skipped;
            path_det_order_hash=order;path_det_stamp_hash=stamp;memcpy(&path_det_last_time,&now,8);
            path_det_last_count=count;path_det_last_bank=bank;++path_det_frames;path_det_stage="done";ok=1;
        }
    } __finally {
        memcpy(tls+PATH_DET_TLS_TIME,&saved_time,8);memcpy(tls+PATH_DET_TLS_PREVIOUS,&saved_previous,8);
    }
    QueryPerformanceCounter(&t2);path_det_refresh_us=path_det_elapsed_us(t1,t2);path_det_total_us=path_det_elapsed_us(t0,t2);
    if(path_det_total_us>path_det_max_total_us) path_det_max_total_us=path_det_total_us;
    return ok;
}
#ifndef PATH_DET_OWN_PLATFORM
static int path_det_frame(unsigned char *manager) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!slots || !slots[0]) {path_det_stage="tls";return 0;}
    return path_det_refresh_frame((path_det_refresh_fn)(image+PATH_DET_REFRESH),(path_det_sync_fn)(image+PATH_DET_SYNC),
        manager,(unsigned char*)slots[0],image+PATH_DET_NAV_VTABLE,(const volatile uint32_t*)(image+PATH_DET_READ_BANK));
}
#endif
/* Runs on MoveWorker before the pass is released. One manager is tracked; a
 * replaced or foreign-owned budget word fails closed instead of being adopted. */
static int path_det_budget_frame(unsigned char *manager) {
    if(!manager || ((uintptr_t)(manager+PATH_DET_BUDGET)&7)) {path_det_stage="budget_manager";return 0;}
    volatile LONG64 *word=(volatile LONG64*)(manager+PATH_DET_BUDGET);
    if(path_det_budget_manager) {
        int ok=manager==path_det_budget_manager && (uint64_t)InterlockedCompareExchange64(word,0,0)==PATH_DET_BUDGET_LIFTED;
        if(!ok) path_det_stage="budget_owner";else ++path_det_budget_frames;
        return ok;
    }
    if((uint64_t)InterlockedCompareExchange64(word,(LONG64)PATH_DET_BUDGET_LIFTED,(LONG64)PATH_DET_BUDGET_NATIVE)!=PATH_DET_BUDGET_NATIVE) {
        path_det_stage="budget_native";return 0;
    }
    path_det_budget_manager=manager;path_det_budget_applied=1;++path_det_budget_frames;return 1;
}
static void path_det_restore(void) {
    if(!path_det_budget_applied) return;
    unsigned char *published=NULL;
    /* Only a still-published manager is written; otherwise restoration stays false. */
    if(!read_local(image+PATH_DET_MANAGER,&published,8) || published!=path_det_budget_manager) return;
    if((uint64_t)InterlockedCompareExchange64((volatile LONG64*)(published+PATH_DET_BUDGET),
        (LONG64)PATH_DET_BUDGET_NATIVE,(LONG64)PATH_DET_BUDGET_LIFTED)==PATH_DET_BUDGET_LIFTED) {
        path_det_budget_applied=0;path_det_budget_restored=1;
    }
}
#ifndef PATH_DET_OWN_PLATFORM
static int path_det_pins(void) {
    return hash_ok(image+PATH_DET_REFRESH,537,"d301bea94b279ddce92ced403bafdd17a90ed2eb83c5cc5944cf2ae600fc4a4f") &&
        hash_ok(image+PATH_DET_SYNC,101,"e416f11fb3a8d1f3d6c4e40995a2f57582064bd6e68539f636f96c2f3b6c3020") &&
        /* UpdateBatch around (not including) the agent-hook rel32 at 0xeaed70. */
        hash_ok(image+0xeaecc0,176,"b83d2ecd5752c4ae0de3a7e250064a720a983b3bd427bc530baeceaba558922f") &&
        hash_ok(image+0xeaed74,353,"0d710205daf528902a2b95da8418d7173ea4e0fea8676d1b297c3885b6b140cf") &&
        hash_ok(image+0xeaf2b0,216,"c44bc6f94a518ef5613a518587617c01250558926488dac7a80f14a970039e11") &&
        hash_ok(image+0xeabb8d,17,"13ac1795954cab2bc61fac315ec5635c94ac92bf19c51d5c079da5ec9a4d05f7");
}
#endif
static int path_det_arm(unsigned mask) {
    /* A loading-UI reload reruns configuration: accept only the same mask. */
    if(path_det_armed) return stats.active && path_det_mask==mask;
    if(!mask || mask>3 || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !path_installed || ((mask&PATH_DET_REFRESH_BIT) && !(path_options&1)) || !path_det_pins()) return 0;
    path_det_mask=mask;InterlockedExchange(&path_det_armed,1);return 1;
}
static int path_det_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld mask=%u frames=%llu count=%u bank=%u time=%016llx stamped=%llu current=%llu stale=%llu skipped=%llu "
        "unmapped=%llu order=%016llx stamps=%016llx budget_frames=%llu budget_applied=%d budget_restored=%d stage=%s failures=%lld "
        "collect_us=%llu sort_us=%llu refresh_us=%llu total_us=%llu max_total_us=%llu reuses=%llu rebuilds=%llu lookups=%llu id_hits=%llu created=%llu walk=%016llx",
        path_det_armed,path_det_mask,(unsigned long long)path_det_frames,path_det_last_count,path_det_last_bank,(unsigned long long)path_det_last_time,
        (unsigned long long)path_det_stamped,(unsigned long long)path_det_current,(unsigned long long)path_det_stale,
        (unsigned long long)path_det_skipped,(unsigned long long)path_det_unmapped,(unsigned long long)path_det_order_hash,(unsigned long long)path_det_stamp_hash,
        (unsigned long long)path_det_budget_frames,path_det_budget_applied,path_det_budget_restored,path_det_stage,(long long)path_det_failures,
        (unsigned long long)path_det_collect_us,(unsigned long long)path_det_sort_us,(unsigned long long)path_det_refresh_us,
        (unsigned long long)path_det_total_us,(unsigned long long)path_det_max_total_us,(unsigned long long)path_det_reuses,
        (unsigned long long)path_det_rebuilds,(unsigned long long)path_det_lookups,(unsigned long long)path_det_id_hits,
        (unsigned long long)path_det_created,(unsigned long long)path_det_walk_hash);
    return n>0 && (size_t)n<capacity?n:0;
}
