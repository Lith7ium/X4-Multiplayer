/* Determinism fix, DEFAULT OFF: native_gate_gravidar_wide = true | 'wide' (a
 * wide-range Gravidar unit takes its scan result after every pair check of its
 * sector; changes play) or 'sort' (native semantics, the zone groups in a
 * portable order for every gated pass).
 * sc-missions59..70: fight.attack.object.station enters on one client because
 * the station's event_gravidar_has_scanned posts on one client only. The post
 * rows of 70a/70b (lockstep_gravidar_posts.h) show one pattern: each one-sided
 * change is a station dropping ships at its first scan after they warped into a
 * tempzone of its own sector, and all ships of one tempzone behave alike on a
 * client. 70a: station 29bfd (zone 77, range 215 km) drops 9ef8e (warp 961,
 * dropped 980), 8f0dc (1559/1562), 8f131 (1667/1704), 90ae8 and 90b0f
 * (1731, 1741/1745), all into tempzone c:8003c05560f41635, on client 1 only;
 * client 2 drops 9ef8e when it moves on into zone 7b (1379/1439). 70b: station
 * 28a77 drops 90ddb/90dc6 (tempzone c:8006cf64fecedb40) on client 2 only and
 * 90aac/90abf (tempzone c:8006b89af1314793) on both. Census positions and
 * parents agree on both clients.
 * Mechanism (X4 9.00, static):
 * - Each Gravidar sector entry (worker +0x68b8, 32 bytes: [0] sector, [1..2]
 *   group vector) holds one 0x58-byte group per zone: [0] the zone, +0x08 the
 *   near list (units with range < 100 km), +0x20 the wide list (range >= 100
 *   km: stations), +0x38 neighbour group indices, +0x50 done. Update 0x10b1810
 *   keeps the vector sorted by the zone's ADDRESS (lower bound 0x10cae90 on the
 *   pointer), so a tempzone made at run time lands wherever the client's
 *   allocator put it.
 * - The scan 0x10b34c0 gathers every group's entries (0xc0 bytes: [0] object,
 *   +0x10 contact set, +0x28 second set, +0x70 position, +0xa0 unit, +0xa8
 *   size, +0xac range, +0xb0 factor, +0xb4 visibility, +0xb8.. flags), then for
 *   each group G in vector order runs the pair checks 0x10b4100 (both sides
 *   fill): G.near x G.near, G.near x each later neighbour's near list, and for
 *   every group H: G.near x H.wide and, while H is not done, G.wide x H.wide.
 *   Right after them it applies G's lists, near then wide (0x10b5410 called at
 *   0x10b3ce4 and 0x10b3cf1): slot 7 of each due unit (the contact sets and
 *   the has_scanned post), 0x10abc70 (live and player view flags), then the
 *   entry's sets are cleared.
 * - So a wide unit of H is applied before the near entries of every later
 *   group G are checked against it; those detections land in the cleared sets
 *   and are thrown away with the next gather. A station sees the ships of its
 *   own zone and of zones at a lower address, never the others. Near units are
 *   complete: every check that fills them runs before their group's apply.
 * 'wide': the wide-list apply call (0x10b3cf1, E8 rel32, one locked CAS inside a
 * cache line) goes through THUNK_JUMP 992 to gwide_apply, which keeps the list
 * and, at the call of the last group with entries (every later group empty, so
 * every pair check of the scan has run), applies the kept lists in vector order
 * with the native 0x10b5410. A wide unit then gets every contact its pair checks
 * found, whatever the address order. Near-list applies are unchanged; an empty
 * wide list is passed through in place (the native call does nothing). This
 * changes play: stations see the ships in range in every zone of their sector.
 * Failures (a foreign return address, a group outside the worker's sector
 * entries, kept lists of another scan, overflow, an unreadable list or group,
 * an exception in an apply) count and stop the gate with reason 150; the call
 * word and the pins (the scan around the call and 0x10b5410) are checked every
 * gate tick. Stop restores the call; lists still kept are applied first when
 * the next call of their scan arrives.
 * 'sort' (no code patch; needs the gated Gravidar frame, native_gate_radar_serial,
 * and the world identities): the gated publish runs with the worker's ready
 * lock held (recursive, inside the AI schedule's sector order when that is
 * armed), so the pass it kicks waits at its lock barrier (0x10ae24c) while,
 * under the list lock, every sector entry's zone groups are permuted into
 * portable order: saved ID < created ID (tempzones with a created identity) <
 * runtime ID (no portable identity: counted unmapped; the clients create in
 * the same order, so runtime IDs keep that order). Neighbour group indices are
 * remapped with them. The scan then processes and applies the groups in that
 * order, so a station sees exactly the zones that sort before its own (and its
 * own), the same on every client, and its applies and posts follow that order.
 * After the pass has joined, before anything else can use the vectors (Update
 * looks groups up by address), the address order and indices come back, before
 * the AI schedule puts the sector list back. The settle checks each vector's
 * bounds and its zone sequence; a busy worker, a changed vector, two zones with
 * one key, an unreadable group or a capacity overflow stop the gate with
 * reason 150; stop waits (bounded) for an idle worker and restores. Pins: Run,
 * PublishResults and Update's group lookup besides the scan.
 * The helpers that find a group's sector entry also serve the post rows. */
/* The Linux gate (linux/gate_gravidar_wide.h) ports 'sort' and defines these for its executable. */
#ifndef GWIDE_OWN_PLATFORM
#define GWIDE_SCAN 0x10b34c0u
#define GWIDE_SCAN_END 0x10b40fdu
#define GWIDE_SITE 0x10b3cf1u      /* E8 -> GWIDE_APPLY with rcx = worker, rdx = group+0x20 */
#define GWIDE_APPLY 0x10b5410u     /* one list: slot 7 when due, 0x10abc70, clear the sets */
#define GWIDE_APPLY_SIZE 0xa2u
#define GWIDE_HEAD_SHA "fe10e31b44ccd9f952273e360682eb255b0700e3a5ef5b93f51d5a35d8e1533c" /* scan up to the call */
#define GWIDE_TAIL_SHA "9623e02e7227ac7908320e6bc55ee69f5ea106dc6631f92153d5e2604165418f" /* after the call */
#define GWIDE_APPLY_SHA "116607b743a7a7ed2c9e4cd7259ebda8b6b4754328bd699df484e786448e5d81"
#define GWIDE_WORKER 0x6cf1650u
#define GWIDE_SECTORS 0x68b8u      /* worker: sector entries {begin,end}, GWIDE_RECORD bytes each */
#endif
#define GWIDE_RECORD 0x20u
#define GWIDE_GROUP 0x58u
#define GWIDE_ENTRY 0xc0u
#define GWIDE_NEAR 0x08u
#define GWIDE_WIDE 0x20u
#define GWIDE_RECORDS_MAX 65536u
#define GWIDE_GROUPS_MAX 65536u
#define GWIDE_THUNK 992u
#define GWIDE_PENDING 1024u
#define GWIDE_STOP_REASON 150u
#ifndef GWIDE_OWN_PLATFORM
#define GWIDE_PUBLISH 0x10b12c0u   /* PublishResults: Update, buffer swap, kick the pass */
#define GWIDE_RUN 0x10ae0e0u
#define GWIDE_LOWER_BOUND 0x10cae90u /* Update's group lookup: lower bound on the zone pointer */
#define GWIDE_LIST_LOCK 0x6890u    /* worker: sector list lock (scan, Update) */
#define GWIDE_READY_LOCK 0x6960u   /* worker: ready lock; the kicked pass enters it at 0x10ae24c */
#define GWIDE_READY 0x6988u
#endif
#define GWIDE_SORT_SECTORS 4096u
#define GWIDE_SORT_GROUPS 65536u
#define GWIDE_SORT_SCRATCH 4096u   /* groups of one sector */
enum {GWIDE_MODE_OFF,GWIDE_MODE_WIDE,GWIDE_MODE_SORT};
typedef void (*gwide_apply_fn)(void *,void *);
typedef void (*gwide_publish_fn)(void *,unsigned char);
static int gwide_mode,gwide_armed,gwide_installed,gwide_restored;
static volatile LONG gwide_active;
static int32_t gwide_original_rel,gwide_patched_rel;
/* Gravidar thread only (inside one sector scan); the status reads counters. */
static void *gwide_lists[GWIDE_PENDING];
static unsigned gwide_indices[GWIDE_PENDING],gwide_pending,gwide_last;
static const unsigned char *gwide_record;
static uint64_t gwide_calls,gwide_kept,gwide_entries,gwide_flushes,gwide_direct,gwide_max,gwide_late;
static volatile LONG64 gwide_failures;
static const char *gwide_stage="idle";
static gwide_apply_fn gwide_apply_override; /* tests only */
static int gwide_vector(const void *at,size_t stride,size_t max,uintptr_t bounds[2]) {
    return read_local(at,bounds,16) && bounds[1]>=bounds[0] && !((bounds[1]-bounds[0])%stride) && (bounds[1]-bounds[0])/stride<=max;
}
#ifndef GWIDE_OWN_PLATFORM
static int gwide_record_holds(uintptr_t r,const unsigned char *group,uintptr_t groups[2],unsigned *index) {
    uintptr_t g[2];
    if(!gwide_vector((const void*)(r+8),GWIDE_GROUP,GWIDE_GROUPS_MAX,g) || (uintptr_t)group<g[0] || (uintptr_t)group>=g[1] ||
       ((uintptr_t)group-g[0])%GWIDE_GROUP) return 0;
    groups[0]=g[0];groups[1]=g[1];if(index) *index=(unsigned)(((uintptr_t)group-g[0])/GWIDE_GROUP);
    return 1;
}
static uintptr_t gwide_hint; /* the last sector entry found: one scan asks for the same entry per group */
/* The worker's sector entry whose group vector holds group; groups gets that vector, *index the group's. */
static const unsigned char *gwide_record_of(const unsigned char *worker,const unsigned char *group,uintptr_t groups[2],unsigned *index) {
    uintptr_t records[2];
    if(!worker || !gwide_vector(worker+GWIDE_SECTORS,GWIDE_RECORD,GWIDE_RECORDS_MAX,records)) return NULL;
    if(gwide_hint>=records[0] && gwide_hint<records[1] && !((gwide_hint-records[0])%GWIDE_RECORD) &&
       gwide_record_holds(gwide_hint,group,groups,index)) return (const unsigned char*)gwide_hint;
    for(uintptr_t r=records[0];r<records[1];r+=GWIDE_RECORD) if(gwide_record_holds(r,group,groups,index)) {gwide_hint=r;return (const unsigned char*)r;}
    return NULL;
}
/* The sector entry, group index and list (0 near, 1 wide) of a scan entry, found by its sector's runtime ID (+0x08). */
static const unsigned char *gwide_entry_home(const unsigned char *worker,const unsigned char *entry,uintptr_t groups[2],unsigned *group,unsigned *wide) {
    uintptr_t records[2];uint64_t sector=0;
    if(!worker || !entry || !read_local(entry+8,&sector,8) || !sector || !gwide_vector(worker+GWIDE_SECTORS,GWIDE_RECORD,GWIDE_RECORDS_MAX,records)) return NULL;
    for(uintptr_t r=records[0];r<records[1];r+=GWIDE_RECORD) {
        const unsigned char *object=NULL;uint64_t id=0;uintptr_t g[2];
        if(!read_local((const void*)r,&object,8) || !object || !read_local(object+8,&id,8) || id!=sector ||
           !gwide_vector((const void*)(r+8),GWIDE_GROUP,GWIDE_GROUPS_MAX,g)) continue;
        for(uintptr_t at=g[0];at<g[1];at+=GWIDE_GROUP) for(unsigned k=0;k<2;k++) {
            uintptr_t list[2];
            if(!gwide_vector((const void*)(at+(k?GWIDE_WIDE:GWIDE_NEAR)),GWIDE_ENTRY,GWIDE_GROUPS_MAX*16u,list) ||
               (uintptr_t)entry<list[0] || (uintptr_t)entry>=list[1] || ((uintptr_t)entry-list[0])%GWIDE_ENTRY) continue;
            groups[0]=g[0];groups[1]=g[1];*group=(unsigned)((at-g[0])/GWIDE_GROUP);*wide=k;return (const unsigned char*)r;
        }
    }
    return NULL;
}
static gwide_apply_fn gwide_native(void) {return gwide_apply_override?gwide_apply_override:(gwide_apply_fn)(void *)(image+GWIDE_APPLY);}
#endif
static void gwide_fail(const char *stage) {gwide_stage=stage;InterlockedIncrement64(&gwide_failures);}
#ifndef GWIDE_OWN_PLATFORM
/* The kept lists, in vector order, with the native apply; late counts those kept past a later group's call. */
static void gwide_flush(void *worker,unsigned index) {
    unsigned n=gwide_pending;gwide_pending=0;gwide_record=NULL;
    if(!n) return;
    for(unsigned i=0;i<n;i++) if(gwide_indices[i]<index) ++gwide_late;
    int done=0;
    __try {for(unsigned i=0;i<n;i++) gwide_native()(worker,gwide_lists[i]);done=1;}
    __finally {if(!done) gwide_fail("apply_exception");}
    ++gwide_flushes;
}
/* Gravidar thread, the scan's wide-list apply of one group (ret: the native return address). */
static void gwide_run(uintptr_t ret,void *worker,void *list) {
    if(!InterlockedCompareExchange(&gwide_active,0,0)) {
        if(gwide_pending) { /* stopped inside a scan: its kept lists first, other lists are dropped */
            uintptr_t groups[2];unsigned index=0;
            if(gwide_record_of((const unsigned char*)worker,(unsigned char*)list-GWIDE_WIDE,groups,&index)==gwide_record && index>gwide_last)
                gwide_flush(worker,index);
            else {gwide_pending=0;gwide_record=NULL;}
        }
        gwide_native()(worker,list);return;
    }
    DWORD error=GetLastError();++gwide_calls;
    unsigned char *group=(unsigned char*)list-GWIDE_WIDE;uintptr_t groups[2]={0,0},entries[2]={0,0};unsigned index=0;
    int native=ret==(uintptr_t)(image+GWIDE_SITE+5);
    const unsigned char *record=native?gwide_record_of((const unsigned char*)worker,group,groups,&index):NULL;
    const char *why=!native?"return":!record?"group":!gwide_vector(list,GWIDE_ENTRY,GWIDE_GROUPS_MAX*16u,entries)?"list":
        gwide_pending && (record!=gwide_record || index<=gwide_last)?"stale":NULL;
    if(why) {
        gwide_fail(why);gwide_pending=0;gwide_record=NULL; /* kept lists of another scan are never applied */
        SetLastError(error);gwide_native()(worker,list);return;
    }
    if(entries[1]!=entries[0]) {
        if(gwide_pending==GWIDE_PENDING) {gwide_fail("overflow");gwide_flush(worker,index);SetLastError(error);gwide_native()(worker,list);return;}
        gwide_indices[gwide_pending]=index;gwide_lists[gwide_pending++]=list;gwide_record=record;gwide_last=index;++gwide_kept;gwide_entries+=(entries[1]-entries[0])/GWIDE_ENTRY;
        if(gwide_pending>gwide_max) gwide_max=gwide_pending;
    } else {
        if(gwide_pending) gwide_last=index;
        ++gwide_direct;SetLastError(error);gwide_native()(worker,list);error=GetLastError();
    }
    /* The last group with entries: every pair check of this scan has run. */
    int last=1;
    for(uintptr_t at=(uintptr_t)group+GWIDE_GROUP;at<groups[1];at+=GWIDE_GROUP) {
        uintptr_t lists[6];
        if(!read_local((const void*)(at+GWIDE_NEAR),lists,sizeof(lists))) {gwide_fail("groups");break;}
        if(lists[0]!=lists[1] || lists[3]!=lists[4]) {last=0;break;}
    }
    if(last && gwide_pending) {SetLastError(error);gwide_flush(worker,index);error=GetLastError();}
    SetLastError(error);
}
/* noinline: the return address read is the native caller's (entered by jmp from the thunk). */
static __attribute__((noinline)) void gwide_apply(void *worker,void *list) {
    gwide_run((uintptr_t)__builtin_return_address(0),worker,list);
}
/* 'sort': Run (the barrier), PublishResults and the address lookup that the restored order serves. */
static int gwide_sort_pinned(void) {
    return hash_ok(image+GWIDE_RUN,4686,"7fdbcc2998badaec41dea9b363938be23b24be94c881eb74c203cfd899be342c") &&
        hash_ok(image+GWIDE_PUBLISH,317,"89362a2325ff0149e2e41e08628ad92eb2405e592c3dfc6f3e3e148667399daa") &&
        hash_ok(image+GWIDE_LOWER_BOUND,0x5f,"45fbf6485a43a66d4a6db2c749662b3da0ef279a7d63d43e86f58459e4f55d0f");
}
static int gwide_pinned(void) {
    return hash_ok(image+GWIDE_SCAN,GWIDE_SITE-GWIDE_SCAN,GWIDE_HEAD_SHA) &&
        hash_ok(image+GWIDE_SITE+5,GWIDE_SCAN_END-GWIDE_SITE-5,GWIDE_TAIL_SHA) && hash_ok(image+GWIDE_APPLY,GWIDE_APPLY_SIZE,GWIDE_APPLY_SHA);
}
/* The rel32 at site+1 sits inside one 64-byte line: one locked CAS. */
static int gwide_swap(int32_t expected,int32_t value) {
    unsigned char *rel=image+GWIDE_SITE+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+GWIDE_SITE,5);
    return actual==expected;
}
static int gwide_prepare(void) {
    unsigned char call[5];int32_t rel;
    if(!read_local(image+GWIDE_SITE,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);if((uintptr_t)(image+GWIDE_SITE+5)+(intptr_t)rel!=(uintptr_t)(image+GWIDE_APPLY)) return 0;
    intptr_t distance=(intptr_t)(thunk+GWIDE_THUNK)-(intptr_t)(image+GWIDE_SITE+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    gwide_original_rel=rel;gwide_patched_rel=(int32_t)distance;return 1;
}
static int gwide_owned(void) {
    int32_t rel=0;if(!gwide_installed) return 1;
    memcpy(&rel,image+GWIDE_SITE+1,4);return rel==gwide_patched_rel;
}
#endif
/* 'sort': the zone groups of every sector entry in portable order for the gated
 * pass, the address order back after it. Keys (lockstep_world_identity.h order):
 * saved ID < created ID < runtime ID of a zone with neither (counted unmapped;
 * runtime IDs keep the clients' creation order). */
typedef struct {uintptr_t record,begin,end;uint64_t zones;uint32_t first,count;} gwide_sorted_t;
static gwide_sorted_t gwide_sorted[GWIDE_SORT_SECTORS];
static uint32_t gwide_perm[GWIDE_SORT_GROUPS],gwide_rank[GWIDE_SORT_SCRATCH];
static uint64_t gwide_keys[GWIDE_SORT_SCRATCH];
static uint8_t gwide_classes[GWIDE_SORT_SCRATCH];
static unsigned char gwide_scratch[GWIDE_SORT_SCRATCH*GWIDE_GROUP];
static unsigned gwide_sorted_count,gwide_perm_used;
static int gwide_permuted;
static uint64_t gwide_sorts,gwide_settles,gwide_moved,gwide_sectors,gwide_unmapped,gwide_order_hash;
static gwide_publish_fn gwide_inner; /* the publish gwide_publish wraps (native or the AI schedule's) */
static uint64_t gwide_zones_hash(const unsigned char *groups,unsigned n) {
    uint64_t hash=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<n;i++) {uintptr_t zone;memcpy(&zone,groups+(size_t)i*GWIDE_GROUP,8);hash=(hash^zone)*0x100000001b3ULL;}
    return hash;
}
static int gwide_before(unsigned a,unsigned b) {
    return gwide_classes[a]!=gwide_classes[b]?gwide_classes[a]<gwide_classes[b]:gwide_keys[a]<gwide_keys[b];
}
/* Neighbour index lists (+0x38, u64 group indices): every index through map. 0 on a bad list. */
static int gwide_remap(unsigned char *groups,unsigned n,const uint32_t *map) {
    for(unsigned i=0;i<n;i++) {
        uintptr_t list[2];
        if(!gwide_vector(groups+(size_t)i*GWIDE_GROUP+0x38,8,GWIDE_SORT_SCRATCH,list)) return 0;
        for(uint64_t *at=(uint64_t*)list[0];at<(uint64_t*)list[1];at++) {if(*at>=n) return 0;*at=map[*at];}
    }
    return 1;
}
/* One sector entry: plan the order, permute in place, keep what the settle needs. 1 done, 0 failure. */
static int gwide_sort_record(uintptr_t record) {
    uintptr_t g[2];
    if(!gwide_vector((const void*)(record+8),GWIDE_GROUP,GWIDE_GROUPS_MAX,g)) return 0;
    unsigned n=(unsigned)((g[1]-g[0])/GWIDE_GROUP);unsigned char *groups=(unsigned char*)g[0];
    if(n<2) return 1;
    if(n>GWIDE_SORT_SCRATCH || gwide_sorted_count==GWIDE_SORT_SECTORS || gwide_perm_used+n>GWIDE_SORT_GROUPS) {gwide_stage="sort_capacity";return 0;}
    uint32_t *perm=gwide_perm+gwide_perm_used;
    for(unsigned i=0;i<n;i++) {
        const unsigned char *zone=NULL;uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;
        if(!read_local(groups+(size_t)i*GWIDE_GROUP,&zone,8) || !zone || !read_local(zone+8,&runtime,8) || !runtime) {gwide_stage="sort_zone";return 0;}
        uint64_t key=world_identity_key(runtime,&klass);
        if(!key) {key=runtime;klass=WORLD_KEY_NONE;++gwide_unmapped;}
        gwide_keys[i]=key;gwide_classes[i]=(uint8_t)klass;perm[i]=i;
    }
    for(unsigned i=1;i<n;i++) { /* stable insertion sort: perm[new]=old */
        uint32_t v=perm[i];unsigned j=i;
        while(j && gwide_before(v,perm[j-1])) {perm[j]=perm[j-1];--j;}
        perm[j]=v;
    }
    unsigned moved=0;
    for(unsigned i=0;i<n;i++) {
        if(i && !gwide_before(perm[i-1],perm[i])) {gwide_stage="sort_collision";return 0;}
        if(gwide_classes[perm[i]]!=WORLD_KEY_NONE) gwide_order_hash=(gwide_order_hash^((uint64_t)gwide_classes[perm[i]]<<62^gwide_keys[perm[i]]))*0x100000001b3ULL;
        if(perm[i]!=i) ++moved;
    }
    if(!moved) return 1;
    for(unsigned i=0;i<n;i++) gwide_rank[perm[i]]=i;
    uint64_t zones=gwide_zones_hash(groups,n);
    if(!gwide_remap(groups,n,gwide_rank)) {gwide_stage="sort_neighbours";return 0;}
    for(unsigned i=0;i<n;i++) memcpy(gwide_scratch+(size_t)i*GWIDE_GROUP,groups+(size_t)perm[i]*GWIDE_GROUP,GWIDE_GROUP);
    memcpy(groups,gwide_scratch,(size_t)n*GWIDE_GROUP);
    gwide_sorted_t *t=&gwide_sorted[gwide_sorted_count++];t->record=record;t->begin=g[0];t->end=g[1];t->zones=zones;t->first=gwide_perm_used;t->count=n;
    gwide_perm_used+=n;gwide_moved+=moved;++gwide_sectors;return 1;
}
/* MoveWorker, inside the gated publish with the ready lock held (the kicked pass waits at 0x10ae24c). */
static int gwide_sort_apply(unsigned char *w) {
    uintptr_t records[2];int ok=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+GWIDE_LIST_LOCK));
    gwide_sorted_count=gwide_perm_used=0;gwide_permuted=1; /* partial work is settled too */
    if(gwide_vector(w+GWIDE_SECTORS,GWIDE_RECORD,GWIDE_RECORDS_MAX,records)) {
        ok=1;for(uintptr_t r=records[0];r<records[1] && ok;r+=GWIDE_RECORD) ok=gwide_sort_record(r);
    } else gwide_stage="sort_read";
    if(ok) ++gwide_sorts;
    LeaveCriticalSection((CRITICAL_SECTION*)(w+GWIDE_LIST_LOCK));
    return ok;
}
static void gwide_publish(void *worker,unsigned char reset) {
    gwide_publish_fn inner=gwide_inner?gwide_inner:(gwide_publish_fn)(void *)(image+GWIDE_PUBLISH);
    if(gwide_mode!=GWIDE_MODE_SORT || !InterlockedCompareExchange(&gwide_active,0,0) || gwide_permuted || reset) {
        if(gwide_permuted) gwide_fail("sort_unsettled");
        inner(worker,reset);return;
    }
    CRITICAL_SECTION *ready=(CRITICAL_SECTION*)((unsigned char*)worker+GWIDE_READY_LOCK);int ok=0;
    EnterCriticalSection(ready);
    __try {inner(worker,reset);ok=gwide_sort_apply((unsigned char*)worker);}
    __finally {LeaveCriticalSection(ready);if(!ok) gwide_fail(AbnormalTermination()?"sort_exception":gwide_stage);}
}
/* Idle worker only: every permuted vector back into address order. 1 settled (or nothing to do), 0 busy, -1 changed. */
static int gwide_settle_worker(unsigned char *w) {
    if(!gwide_permuted) return 1;
    int result=0;
    EnterCriticalSection((CRITICAL_SECTION*)(w+GWIDE_READY_LOCK));
    if(*(volatile unsigned char*)(w+GWIDE_READY)==1) {
        result=1;
        EnterCriticalSection((CRITICAL_SECTION*)(w+GWIDE_LIST_LOCK));
        for(unsigned k=gwide_sorted_count;k--;) {
            const gwide_sorted_t *t=&gwide_sorted[k];uintptr_t g[2];const uint32_t *perm=gwide_perm+t->first;unsigned n=t->count;
            unsigned char *groups=(unsigned char*)t->begin;
            if(!gwide_vector((const void*)(t->record+8),GWIDE_GROUP,GWIDE_GROUPS_MAX,g) || g[0]!=t->begin || g[1]!=t->end) {result=-1;continue;}
            for(unsigned i=0;i<n;i++) memcpy(gwide_scratch+(size_t)perm[i]*GWIDE_GROUP,groups+(size_t)i*GWIDE_GROUP,GWIDE_GROUP);
            memcpy(groups,gwide_scratch,(size_t)n*GWIDE_GROUP);
            if(!gwide_remap(groups,n,perm) || gwide_zones_hash(groups,n)!=t->zones) result=-1;
        }
        gwide_sorted_count=gwide_perm_used=0;gwide_permuted=0;++gwide_settles;
        LeaveCriticalSection((CRITICAL_SECTION*)(w+GWIDE_LIST_LOCK));
    }
    LeaveCriticalSection((CRITICAL_SECTION*)(w+GWIDE_READY_LOCK));
    if(result<0) gwide_stage="sort_changed";
    return result;
}
/* MoveWorker, after the gated Gravidar frame joined its pass (before the AI schedule's settle). */
static int gwide_settle(void) {
    if(!gwide_permuted) return 1;
    unsigned char *w=NULL;read_local(image+GWIDE_WORKER,&w,8);
    int state=w?gwide_settle_worker(w):-1;
    if(state==1) return 1;
    if(!state) gwide_stage="sort_busy";
    gwide_fail(gwide_stage);return 0;
}
/* Before the first admitted frame, on the MoveWorker (idempotent on a loading-UI reload).
 * 'wide' patches the wide-list apply call; 'sort' needs the gated Gravidar publish. */
static int gwide_arm(int mode,int gated) {
    if(gwide_armed) return stats.active && gwide_mode==mode && (mode!=GWIDE_MODE_WIDE || gwide_installed);
    if((mode!=GWIDE_MODE_WIDE && mode!=GWIDE_MODE_SORT) || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(mode==GWIDE_MODE_WIDE && (!thunk || !thunk_slot_ready(GWIDE_THUNK))) {gwide_stage="thunk";return 0;}
    if(mode==GWIDE_MODE_SORT && (!gated || !world_identity_installed)) {gwide_stage="needs";return 0;}
    if(!gwide_pinned() || (mode==GWIDE_MODE_SORT && !gwide_sort_pinned())) {gwide_stage="pins";return 0;}
    if(mode==GWIDE_MODE_WIDE && !gwide_prepare()) {gwide_stage="site";return 0;}
    gwide_pending=gwide_last=0;gwide_record=NULL;gwide_hint=0;gwide_calls=gwide_kept=gwide_entries=gwide_flushes=gwide_direct=gwide_max=gwide_late=0;gwide_failures=0;
    gwide_sorted_count=gwide_perm_used=0;gwide_permuted=0;gwide_sorts=gwide_settles=gwide_moved=gwide_sectors=gwide_unmapped=0;gwide_order_hash=0;
    gwide_mode=mode;InterlockedExchange(&gwide_active,1);
    if(mode==GWIDE_MODE_WIDE && !gwide_swap(gwide_original_rel,gwide_patched_rel)) {InterlockedExchange(&gwide_active,0);gwide_mode=GWIDE_MODE_OFF;gwide_stage="swap";return 0;}
    gwide_installed=mode==GWIDE_MODE_WIDE;gwide_armed=1;gwide_restored=0;gwide_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 150. */
static int gwide_check(void) {
    if(InterlockedCompareExchange64(&gwide_failures,0,0)) return 0;
    const char *why=!gwide_owned()?"call_changed":!gwide_pinned() || (gwide_mode==GWIDE_MODE_SORT && !gwide_sort_pinned())?"pins_changed":NULL;
    if(why) {gwide_fail(why);return 0;}
    return 1;
}
static void gwide_restore(void) {
    if(!gwide_armed) return;
    InterlockedExchange(&gwide_active,0);
    if(gwide_installed && gwide_swap(gwide_patched_rel,gwide_original_rel)) gwide_installed=0;
    /* A pass may still run on the permuted groups: wait for it (bounded). */
    unsigned char *w=NULL;read_local(image+GWIDE_WORKER,&w,8);
    for(ULONGLONG started=GetTickCount64();w && gwide_permuted;Sleep(1)) {
        if(gwide_settle_worker(w)<0 || GetTickCount64()-started>=5000) break;
    }
    gwide_restored=!gwide_installed && !gwide_permuted;
}
static int gwide_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"mode=%s armed=%d installed=%d active=%ld restored=%d calls=%llu kept=%llu entries=%llu direct=%llu flushes=%llu late=%llu "
        "max=%llu pending=%u sorts=%llu settles=%llu sectors=%llu moved=%llu unmapped=%llu order=%016llx permuted=%d stage=%s failures=%lld",
        gwide_mode==GWIDE_MODE_WIDE?"wide":gwide_mode==GWIDE_MODE_SORT?"sort":"off",gwide_armed,gwide_installed,gwide_active,gwide_restored,
        (unsigned long long)gwide_calls,(unsigned long long)gwide_kept,(unsigned long long)gwide_entries,(unsigned long long)gwide_direct,
        (unsigned long long)gwide_flushes,(unsigned long long)gwide_late,(unsigned long long)gwide_max,gwide_pending,(unsigned long long)gwide_sorts,
        (unsigned long long)gwide_settles,(unsigned long long)gwide_sectors,(unsigned long long)gwide_moved,(unsigned long long)gwide_unmapped,
        (unsigned long long)gwide_order_hash,gwide_permuted,gwide_stage,(long long)gwide_failures);
    return n>0 && (size_t)n<cap?n:0;
}
