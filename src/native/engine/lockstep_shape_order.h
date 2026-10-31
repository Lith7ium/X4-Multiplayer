/* Determinism fix, DEFAULT OFF: native_gate_shape_order = true.
 *
 * big-BS6 physics trace (native_gate_physics_trace): in the diverging runs
 * bsa and bsc the first difference is the tick-16 step (the first after the
 * tick-15 spawn). BodyIDs, flags, inputs and every contact's bodies, manifold
 * and settings agree; only the SubShapeID on destroyer 80000ff8a4da86ef's
 * hull differs (low bits ...c0 on client 1, ...c8 on client 2, child 0 versus
 * child 8 of its compound), and with it Jolt's contact sort key, so the two
 * colliding destroyers are solved in another order (post parts at 16, the
 * controllers' inputs at 26). The passing runs agree: bo c0/c0, bsb c8/c8.
 * Destroyer 80000fdf2e5ba84e has its hull at child 8 on every client.
 *
 * Child shapes of a part whose collision file is not loaded when the body is
 * built start as stand-ins and wait in the system's request maps
 * (+0x6c0/+0x700, keyed by file). The loader thread appends each finished
 * file to a ready list (+0x740/+0x1750); the next Scene::Step pre-pass
 * 0xbdc80 hands every request of a ready file to 0xc7e30 -> 0xc7f30, which
 * removes the stand-in (RemoveShape 0x140ce60 at 0xc8044) and appends the
 * real child (AddShape 0x140cb10 at 0xc8290, always at the end), then
 * NotifyShapeChanged. A part whose file is already cached is built in place.
 * So whether the load finished before the ship was built decides the child
 * index, and the loader thread's timing differs between processes.
 *
 * Fix (true or 'wait'): on the MoveWorker after arming the two calls go
 * through stubs; the removal's index is kept and the appended child moved
 * back to it (the sub-shapes in between shift up by one 0x28-byte entry,
 * references moved as they are) and CalculateSubShapeBounds 0x140c5c0
 * recomputes the bounds from that index, so the child order is the build
 * order whether or not the file was cached.
 *
 * 'wait' also closes the window in which a stand-in is still in place on one
 * client only. big-BS6soc: 60 children replaced at tick 16 on client 1 and at
 * 17 on client 2 (their files finished a frame apart). The files finish on
 * XLib::AssetLoadThread: CollisionShapeFile/CollisionMeshFile slot 1 (0xc85f0,
 * 0xc8a30) -> 0xb2380/0xb25f0 -> 0xbdac0/0xbdba0, which under system+0x698
 * append the file to the ready list (+0x740/+0x1750) when the request map
 * (+0x6c0/+0x700, root +8, count +0x28) holds requests for it; the pre-pass
 * erases a file's requests when it applies them. Scene::Step's call of the
 * pre-pass (0xba65d) goes through sorder_prepass: after the original pass it
 * repeats the pass until both request maps of the step's system are empty, so
 * every request made before a step is applied in that step on every client
 * (a cached file's child is in place from the build; the stand-in's request
 * carries the build-time pose). Stop reason 135 after SORDER_WAIT_MS without
 * progress (a file that never finishes).
 *
 * 'sync' also takes the stand-ins away in admitted frames: big-BS6soa shows
 * children appended without a stand-in (appends 118 at tick 7 on client 1,
 * at 8 on client 2) whose index is the order their files finished, and a
 * cached file depends on the cache's own collection (Scene::Step collects
 * unreferenced files under TryEnterCriticalSection). The shape builder
 * 0xc0240 asks the file cache (*0x6cf1518: lock at +0, shape files +0x30,
 * mesh files +0x70, keyed by the asset key's 24-bit index and byte 4) through
 * 0xb1d40 (call 0xc0dca, key at desc+0x28) or 0xb2060 (0xc0dc3, desc+0x20);
 * a miss asks AssetManager::GetAssetIntern 0x14bab90 (manager *0x391bd78,
 * key, 0, 1) to load it and leaves a request. A null key (0x2f529d8/dc)
 * makes the original use the key at desc+0x30 and load it through 0xf522d0
 * (the same GetAssetIntern call); a no-load key (0x31beb00/04) loads nothing.
 *
 * GetAssetIntern(.., 0, 1) never loads during play: with the manager's
 * loading flag (+0x104660) clear it queues the file (0x14bb1d0, flags |0x40,
 * the asset's own priority queue, skipped if already queued) and returns
 * NULL. big-BS6s2a (first 'sync'): requests=938 cached=362 and every one of
 * the 576 misses returned NULL, so nothing waited (loads=0, alt_keys=0) and
 * the children were still appended late (moves 69 vs 68). sorder_request now
 * uses the engine's own synchronous load (GetAssetIntern's r8b=1 branch,
 * 0x14bad57..0x14bad9d) with a bounded wait: under its lock 0x6b60ec0 it
 * queues the key with flags 0x21 (0x20: queue 3, which the loader
 * 0x14dc750 takes first, never skipped as already queued) and waits on the
 * loader's semaphore *0x6b60ee8 (CreateSemaphoreW(0, 0, 1) at 0x933c0),
 * released after each 0x20 file (0x14dc8c5). The file is then in the cache
 * (0xc85f0 inserts it before the asset is published) and the original builds
 * the child in place. A key outside the manager's table (type byte 1..15,
 * index below 0x297b7e0[type], as GetAssetIntern checks), or an asset that is
 * already resident, is not queued (invalid/resident); a load that ends without
 * the file in the cache counts as unloaded. No-load keys pass (passed). A wait
 * past SORDER_WAIT_MS stops the gate (sync_timeout; the semaphore's late
 * release then wakes the next native synchronous load early).
 *
 * Mass (every mode): a body keeps the mass properties of the shape it was
 * built with. The apply's NotifyShapeChanged (0x14061e0 at 0xc7efe) asks for
 * new mass properties only for feedback bodies (record +0x39 and +0x3a both
 * 0); big-BS6sya/syb part at tick 16 on the second (layer-3, dynamic) bodies
 * of three frigates whose children were stand-ins on one client: same final
 * children, different inverse mass and inertia. The call goes through
 * sorder_notify, which always asks for them: the body creation's
 * CalculateMassAndInertia (multiplier 1.0) of the final shape, which is what
 * a body built without stand-ins has; NotifyShapeChanged itself skips a body
 * without motion properties.
 *
 * The in-place move and the mass request also run in the boot frames after
 * arming (MoveWorker only): long-L17 client 1 kept, from tick 2, a different
 * inverse inertia (rounding-level: 39704e01 vs 39704e20) on the second body of
 * saved fixtures 5 and 6 while body 0 agreed - the sum over children in
 * another order. Waiting and synchronous loads stay in admitted frames.
 * Without this module that race stays live, at about 2 in 100 client boots and
 * with no diagnostics on: the same bits on fixture 6 in full-X90c and play22-soak;
 * stock-J2b client 1 five values on fixtures 2-6, each later fixture further from
 * the usual one, as the files finished while the fixtures were built (the same on
 * body 0 would part the simulation: contacts sort by sub-shape ID).
 *
 * SHAPE_ORDER status per tick, SHAPE_ORDER_ROW per moved child. Pins: the
 * apply 0xc7f30 and its caller 0xc7e30, AddShape, RemoveShape,
 * CalculateSubShapeBounds; with 'wait' the pre-pass, both ready-list
 * producers and the call block; with 'sync' also both cache lookups, their
 * call block and GetAssetIntern. Stop reason 135: a site no longer ours, a
 * pin change, an append for another compound than the removal's, a wait
 * timeout or a fault. */
#define SORDER_REMOVE 0x140ce60u
#define SORDER_ADD 0x140cb10u
#define SORDER_BOUNDS 0x140c5c0u
#define SORDER_PREPASS 0xbdc80u
#define SORDER_SHAPE_REQUEST 0xb1d40u
#define SORDER_MESH_REQUEST 0xb2060u
#define SORDER_ASSET 0x14bab90u
#define SORDER_ASSET_MANAGER 0x391bd78u
#define SORDER_QUEUE 0x14bb1d0u
#define SORDER_ASSET_LIMITS 0x297b7e0u
#define SORDER_SYNC_LOCK 0x6b60ec0u
#define SORDER_SYNC_DONE 0x6b60ee8u
#define SORDER_SYNC_FLAGS 0x21u
#define SORDER_ENTRY_TABLE 0xad68u
#define SORDER_ENTRY_SIZE 0x58u
#define SORDER_CACHE 0x6cf1518u
#define SORDER_NOTIFY 0x14061e0u
#define SORDER_CALLS 6u
#define SORDER_WAIT_MS 5000u
#define SORDER_STOP_REASON 135u
#define SORDER_SUBSHAPE 0x28u
#define SORDER_MAX_CHILDREN 65536u
#define SORDER_RING 4096u
#define SORDER_STUB_SIZE 0x1000u
static const struct {unsigned rva,target;} sorder_calls[SORDER_CALLS]={{0xc8044u,SORDER_REMOVE},{0xc8290u,SORDER_ADD},{0xba65du,SORDER_PREPASS},
    {0xc0dcau,SORDER_SHAPE_REQUEST},{0xc0dc3u,SORDER_MESH_REQUEST},{0xc7efeu,SORDER_NOTIFY}};
enum {SORDER_OFF,SORDER_PLACE,SORDER_WAIT,SORDER_SYNC};
static const struct {unsigned rva,size;const char *sha;} sorder_pins[]={
    {0xc7f30u,0x3ceu,"b09b333ed0b3334cc80952263028bcf289468dd49a2d3939b8483643448cd36d"},   /* the apply */
    {0xc7e30u,0xf1u,"b0edab2f0a887d2c18b19abeeebabf79f16d102ccc8e8178d97f3e2dd247ba61"},    /* its caller */
    {0x140cb10u,0x34eu,"7c56f905c1d78e60d31686d852fdf33bfe739d9b75556ec3785ab18925b2b2c6"}, /* AddShape (append) */
    {0x140ce60u,0xb3u,"2b84cc564f6897e9f817c14598d16de87d2d8cbe5d2faa61caf10e2fa140cb37"},  /* RemoveShape */
    {0x140c5c0u,0x4au,"958ea9112821ff3db9f24aa43db949341f2e2591d7f18d93312d9994b8bcd2d8"},  /* CalculateSubShapeBounds */
    {0x14061e0u,0x274u,"10a072513db8d1e35618df6e84d2c9fa875a6325355748788f61c8b0defe22b5"}}; /* NotifyShapeChanged */
static const struct {unsigned rva,size;const char *sha;} sorder_wait_pins[]={
    {0xbdc80u,0x8ceu,"c4133b41f4e9cb76c1da194ed74afb47da251a8cde0ea4050ebbbb262c641f42"},  /* the pre-pass */
    {0xbdac0u,0xd5u,"d6bd613ed0f3cc4f7ea2681c1cb36a82b70a6927253b7fc6deb3cf06259ffdc4"},   /* ready list +0x740 */
    {0xbdba0u,0xd5u,"21dc99edeed3e587ca0771485a93c9f5bacf1fb5d2743cafd82cb8f5c2edbc85"},   /* ready list +0x1750 */
    {0xba640u,0x28u,"d21fe222fce5a3c59cd5ecab8a6d1a828dc6ec75c4cc94d429dce72cb89a4381"}};  /* Scene::Step's pre-pass call */
static const struct {unsigned rva,size;const char *sha;} sorder_sync_pins[]={
    {0xb1d40u,0x311u,"e8a4d750c189f2f79e28f8ace27e45b2c8f3ecf49fd4e78350dd89a46f32e87c"},   /* shape file lookup/request */
    {0xb2060u,0x319u,"057b39a5bdccb2e0957d9e7de4dfe56e4c8fe8e208cdf5eed2cf29fac76f6f11"},   /* mesh file lookup/request */
    {0x14bab90u,0x246u,"252b92d6ae56a121c5f2ff3c64296a212541a4b8e647bbbd41450e557a4e4e7d"}, /* GetAssetIntern */
    {0xc0d8bu,0x44u,"ece608be0bdba263b8e67ad9ea1be38f3a005f28c5dad6126e900a0ac1e27b2f"},    /* the builder's two calls */
    {0x14bb1d0u,0x158u,"3d8ea0dfd151fa903d557af2356a2483deb04dd2d075de83f51c067733d201c4"}, /* the load queue */
    {0x14dc750u,0x1d0u,"c955b702b116e56312397c65147b13ed9420c56507e809875b0ff523a643b479"}}; /* XLib::AssetLoadThread's loop */
typedef void (*sorder_remove_fn)(void *,uint32_t);
typedef uint64_t (*sorder_add_fn)(void *,void *,void *,void *,uint64_t,uint64_t);
typedef void (*sorder_bounds_fn)(void *,uint32_t,uint32_t);
typedef void (*sorder_pass_fn)(void *);
typedef uint64_t (*sorder_request_fn)(void *,void *,void *,void *,uint64_t,uint64_t,uint64_t);
typedef uint8_t (*sorder_queue_fn)(void *,const uint64_t *,uint32_t);
/* The engine's synchronous load: queue, its lock and the loader's semaphore. */
typedef struct {sorder_queue_fn queue;unsigned char *manager;CRITICAL_SECTION *lock;HANDLE done;const unsigned char *limits;} sorder_loader;
typedef void (*sorder_notify_fn)(void *,void *,void *,uint64_t,uint64_t);
typedef struct {uint64_t tick;uint32_t from,to,count;} sorder_row;
static int sorder_installed,sorder_restored,sorder_mode;
static unsigned sorder_done;
static unsigned char *sorder_stubs;
static struct {unsigned char *compound;uint32_t index;int pending;} sorder_cur;
static sorder_row sorder_ring[SORDER_RING];
static volatile LONG64 sorder_rows,sorder_failures;
static uint64_t sorder_removals,sorder_moves,sorder_appends,sorder_native,sorder_tick=UINT64_MAX,sorder_frame_moves;
static uint64_t sorder_waits,sorder_wait_passes,sorder_wait_us,sorder_wait_max_us,sorder_last_pending;
static uint64_t sorder_requests,sorder_cached_hits,sorder_loads,sorder_load_us,sorder_load_max_us,sorder_passed,sorder_alt_keys,sorder_mass_forced;
static uint64_t sorder_resident,sorder_invalid,sorder_unloaded,sorder_boot_moves,sorder_boot_forced;
static const char *sorder_stage="idle";
static int sorder_fail(const char *why) {sorder_stage=why;InterlockedIncrement64(&sorder_failures);return 0;}
/* In-place moves and mass requests: every frame after arming, MoveWorker only. */
static int sorder_ready(void) {
    return sorder_installed && stats.active && GetCurrentThreadId()==ui_thread && !InterlockedCompareExchange64(&sorder_failures,0,0);
}
/* Waiting and synchronous loads: admitted frames only. */
static int sorder_live(void) {return sorder_ready() && boot_phase==3;}
/* The last of count sub-shapes back to index (the entries from index up by one). */
static int sorder_move(unsigned char *compound,uint32_t index,sorder_bounds_fn bounds) {
    uint64_t count=0;unsigned char *subs=NULL,last[SORDER_SUBSHAPE];
    if(!created_read(compound+0x50,&count,8) || !created_read(compound+0x60,&subs,8) || !subs || !count || count>SORDER_MAX_CHILDREN)
        return sorder_fail("compound");
    if((uint64_t)index+1>=count) return 1; /* already the last */
    memcpy(last,subs+(count-1)*SORDER_SUBSHAPE,SORDER_SUBSHAPE);
    memmove(subs+((size_t)index+1)*SORDER_SUBSHAPE,subs+(size_t)index*SORDER_SUBSHAPE,(size_t)(count-1-index)*SORDER_SUBSHAPE);
    memcpy(subs+(size_t)index*SORDER_SUBSHAPE,last,SORDER_SUBSHAPE);
    bounds(compound,index,(uint32_t)(count-index));
    if(sorder_tick!=stats.issued) {sorder_tick=stats.issued;sorder_frame_moves=0;}
    ++sorder_moves;++sorder_frame_moves;if(boot_phase!=3) ++sorder_boot_moves;
    LONG64 at=InterlockedIncrement64(&sorder_rows)-1;sorder_ring[(uint64_t)at%SORDER_RING]=(sorder_row){stats.issued,(uint32_t)(count-1),index,(uint32_t)count};
    return 1;
}
/* Testable cores: the native callees as arguments. */
static void sorder_remove_with(sorder_remove_fn original,unsigned char *compound,uint32_t index) {
    original(compound,index);
    if(!sorder_ready()) {++sorder_native;return;}
    if(sorder_cur.pending) sorder_fail("removal_unpaired");
    sorder_cur.compound=compound;sorder_cur.index=index;sorder_cur.pending=1;++sorder_removals;
}
static uint64_t sorder_add_with(sorder_add_fn original,sorder_bounds_fn bounds,unsigned char *compound,void *position,void *rotation,void *shape,
                                uint64_t a5,uint64_t a6) {
    uint64_t result=original(compound,position,rotation,shape,a5,a6);
    if(!sorder_ready()) {++sorder_native;sorder_cur.pending=0;return result;}
    if(!sorder_cur.pending) {++sorder_appends;return result;} /* a new child, no stand-in removed: native append */
    sorder_cur.pending=0;
    if(sorder_cur.compound!=compound) {sorder_fail("compound_mismatch");return result;}
    sorder_move(compound,sorder_cur.index,bounds);
    return result;
}
/* Requests still waiting for a file in the system's two maps (root, count). */
static int sorder_pending(const unsigned char *system,uint64_t *count) {
    void *roots[2]={NULL,NULL};uint64_t counts[2]={0,0};
    if(!created_read(system+0x6c8,&roots[0],8) || !created_read(system+0x708,&roots[1],8) ||
       !created_read(system+0x6e8,&counts[0],8) || !created_read(system+0x728,&counts[1],8)) return -1;
    *count=counts[0]+counts[1];return roots[0] || roots[1];
}
/* The pre-pass of a step, repeated until no request waits (wait mode). */
static void sorder_prepass_with(sorder_pass_fn original,unsigned char *scene,unsigned timeout_ms) {
    original(scene);
    if(sorder_mode<SORDER_WAIT || !sorder_live()) return;
    unsigned char *system=NULL;uint64_t count=0;
    if(!created_read(scene+0x20,&system,8) || !system) {sorder_fail("scene");return;}
    int pending=sorder_pending(system,&count);
    if(pending<0) {sorder_fail("request_maps");return;}
    if(!pending) return;
    LARGE_INTEGER start,now,frequency;QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    uint64_t passes=0,us=0;sorder_last_pending=count;
    while(pending>0) {
        QueryPerformanceCounter(&now);us=frequency.QuadPart?(uint64_t)((now.QuadPart-start.QuadPart)*1000000/frequency.QuadPart):0;
        if(us>(uint64_t)timeout_ms*1000) {sorder_fail("load_timeout");break;}
        Sleep(passes<64?0:1);original(scene);++passes;
        pending=sorder_pending(system,&count);
    }
    if(pending<0) sorder_fail("request_maps");
    ++sorder_waits;sorder_wait_passes+=passes;sorder_wait_us+=us;if(us>sorder_wait_max_us) sorder_wait_max_us=us;
}
/* Is the file of key in the cache map (lower bound by 24-bit index and byte 4,
 * as 0xb1d40 searches it)? Under the cache's own lock. -1: unreadable. */
static int sorder_cached(unsigned char *cache,unsigned map,uint64_t key) {
    uint32_t want=(uint32_t)(((key>>32)&0xffu)<<24)+(uint32_t)(key&0xffffffu);int found=0;
    EnterCriticalSection((CRITICAL_SECTION *)cache);
    unsigned char *head=cache+map,*node=NULL,*best=head;
    if(!created_read(head+8,&node,8)) found=-1;
    for(unsigned depth=0;found>=0 && node && depth<128;depth++) {
        uint32_t low=0;uint8_t gen=0;unsigned char *next=NULL;
        if(!created_read(node+0x20,&low,4) || !created_read(node+0x24,&gen,1)) {found=-1;break;}
        if(((uint32_t)gen<<24)+(low&0xffffffu)<want) {if(!created_read(node+0x10,&next,8)) found=-1;}
        else {best=node;if(!created_read(node+8,&next,8)) found=-1;}
        node=next;
    }
    if(found>=0 && best!=head) {
        uint32_t low=0;uint8_t gen=0;
        if(!created_read(best+0x20,&low,4) || !created_read(best+0x24,&gen,1)) found=-1;
        else found=!(want<((uint32_t)gen<<24)+(low&0xffffffu));
    }
    LeaveCriticalSection((CRITICAL_SECTION *)cache);
    return found;
}
/* The asset manager's entry of key (0x58 bytes: asset +0, flags +0x20), or
 * NULL for a key GetAssetIntern rejects (type byte 1..15, index below the
 * type's table size). */
static unsigned char *sorder_entry(const sorder_loader *loader,uint64_t key) {
    uint8_t type=(uint8_t)(key>>32);uint64_t index=key&0xffffffu,limit=0;unsigned char *entries=NULL;
    if((uint8_t)(type-1u)>0x0eu || !created_read(loader->limits+8u*type,&limit,8) || index>=limit) return NULL;
    if(!created_read(loader->manager+((size_t)type+SORDER_ENTRY_TABLE)*24u,&entries,8) || !entries) return NULL;
    return entries+index*SORDER_ENTRY_SIZE;
}
/* The builder's file lookup ('sync'): a missing file is loaded through the
 * engine's synchronous load before the original runs, so the original finds
 * it and builds in place. */
static uint64_t sorder_request_with(sorder_request_fn original,const sorder_loader *loader,unsigned char *cache,unsigned char *desc,
                                    unsigned key_at,unsigned map,void *r8,void *r9,uint64_t a5,uint64_t a6,uint64_t a7,unsigned timeout_ms) {
    if(sorder_mode!=SORDER_SYNC || !sorder_live() || !cache || !desc) return original(cache,desc,r8,r9,a5,a6,a7);
    uint64_t key=0;uint8_t null_gen=0,skip_gen=0,mark=0;uint32_t null_low=0,skip_low=0,low=0;
    if(!created_read(desc+key_at,&key,8) || !created_read(image+0x2f529dc,&null_gen,1) || !created_read(image+0x2f529d8,&null_low,4) ||
       !created_read(image+0x31beb04,&skip_gen,1) || !created_read(image+0x31beb00,&skip_low,4)) {sorder_fail("request");return original(cache,desc,r8,r9,a5,a6,a7);}
    ++sorder_requests;
    mark=(uint8_t)(key>>32);low=(uint32_t)key;
    if(mark==null_gen && !((low^null_low)&0xffffffu)) { /* the original's second key (0xf522d0 loads it) */
        if(!created_read(desc+0x30,&key,8)) {sorder_fail("request");return original(cache,desc,r8,r9,a5,a6,a7);}
        mark=(uint8_t)(key>>32);low=(uint32_t)key;++sorder_alt_keys;
    }
    if(mark==skip_gen && !((low^skip_low)&0xffffffu)) {++sorder_passed;return original(cache,desc,r8,r9,a5,a6,a7);}
    int cached=sorder_cached(cache,map,key);
    if(cached<0) {sorder_fail("cache");return original(cache,desc,r8,r9,a5,a6,a7);}
    if(cached) {++sorder_cached_hits;return original(cache,desc,r8,r9,a5,a6,a7);}
    unsigned char *entry=loader->manager?sorder_entry(loader,key):NULL;void *asset=NULL;
    if(!entry) {++sorder_invalid;return original(cache,desc,r8,r9,a5,a6,a7);}  /* the original's own path (it logs the key) */
    if(!created_read(entry,&asset,8)) {sorder_fail("asset_entry");return original(cache,desc,r8,r9,a5,a6,a7);}
    if(asset) {++sorder_resident;return original(cache,desc,r8,r9,a5,a6,a7);} /* loaded, not in the cache: nothing to wait for */
    if(!loader->lock || !loader->done) {sorder_fail("loader");return original(cache,desc,r8,r9,a5,a6,a7);}
    LARGE_INTEGER start,now,frequency;QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    EnterCriticalSection(loader->lock);
    uint64_t at=key;DWORD waited=WAIT_FAILED;
    if(loader->queue(loader->manager,&at,SORDER_SYNC_FLAGS)&0xffu) waited=WaitForSingleObject(loader->done,timeout_ms);
    LeaveCriticalSection(loader->lock);
    QueryPerformanceCounter(&now);uint64_t us=frequency.QuadPart?(uint64_t)((now.QuadPart-start.QuadPart)*1000000/frequency.QuadPart):0;
    if(waited!=WAIT_OBJECT_0) {sorder_fail(waited==WAIT_TIMEOUT?"sync_timeout":"sync_queue");return original(cache,desc,r8,r9,a5,a6,a7);}
    sorder_load_us+=us;if(us>sorder_load_max_us) sorder_load_max_us=us;
    cached=sorder_cached(cache,map,key);
    if(cached<0) sorder_fail("cache");
    else if(cached) ++sorder_loads;
    else ++sorder_unloaded;
    return original(cache,desc,r8,r9,a5,a6,a7);
}
static sorder_loader sorder_native_loader(void) {
    sorder_loader loader={(sorder_queue_fn)(void *)(image+SORDER_QUEUE),NULL,(CRITICAL_SECTION *)(image+SORDER_SYNC_LOCK),NULL,image+SORDER_ASSET_LIMITS};
    read_local(image+SORDER_ASSET_MANAGER,&loader.manager,8);read_local(image+SORDER_SYNC_DONE,&loader.done,8);
    return loader;
}
static uint64_t sorder_shape_request(void *cache,void *desc,void *r8,void *r9,uint64_t a5,uint64_t a6,uint64_t a7) {
    sorder_loader loader=sorder_native_loader();
    return sorder_request_with((sorder_request_fn)(void *)(image+SORDER_SHAPE_REQUEST),&loader,cache,desc,0x28u,0x30u,r8,r9,a5,a6,a7,SORDER_WAIT_MS);
}
static uint64_t sorder_mesh_request(void *cache,void *desc,void *r8,void *r9,uint64_t a5,uint64_t a6,uint64_t a7) {
    sorder_loader loader=sorder_native_loader();
    return sorder_request_with((sorder_request_fn)(void *)(image+SORDER_MESH_REQUEST),&loader,cache,desc,0x20u,0x70u,r8,r9,a5,a6,a7,SORDER_WAIT_MS);
}
/* The apply's NotifyShapeChanged with new mass properties for every body. */
static void sorder_notify_with(sorder_notify_fn original,void *iface,void *body_id,void *previous_com,uint64_t update,uint64_t activation) {
    if(sorder_ready() && !(update&0xffu)) {update=(update&~0xffull)|1u;++sorder_mass_forced;if(boot_phase!=3) ++sorder_boot_forced;}
    original(iface,body_id,previous_com,update,activation);
}
static void sorder_notify(void *iface,void *body_id,void *previous_com,uint64_t update,uint64_t activation) {
    sorder_notify_with((sorder_notify_fn)(void *)(image+SORDER_NOTIFY),iface,body_id,previous_com,update,activation);
}
static void sorder_prepass(void *scene) {
    sorder_prepass_with((sorder_pass_fn)(void *)(image+SORDER_PREPASS),scene,SORDER_WAIT_MS);
}
static void sorder_remove(void *compound,uint32_t index) {
    sorder_remove_with((sorder_remove_fn)(void *)(image+SORDER_REMOVE),compound,index);
}
static uint64_t sorder_add(void *compound,void *position,void *rotation,void *shape,uint64_t a5,uint64_t a6) {
    return sorder_add_with((sorder_add_fn)(void *)(image+SORDER_ADD),(sorder_bounds_fn)(void *)(image+SORDER_BOUNDS),compound,position,rotation,shape,a5,a6);
}
static unsigned char *sorder_stub(unsigned i) {return sorder_stubs+0x20u*i;}
static int32_t sorder_rel_to(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+sorder_calls[i].rva+5));}
/* 0 original, 1 ours (its stub), -1 anything else. */
static int sorder_call_state(unsigned i) {
    unsigned char now[5];int32_t d;if(!read_local(image+sorder_calls[i].rva,now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==sorder_rel_to(i,image+sorder_calls[i].target)) return 0;
    return sorder_stubs && d==sorder_rel_to(i,sorder_stub(i))?1:-1;
}
static int sorder_pins_ok(void) {
    static unsigned char copy[0x900];
    const unsigned base=sizeof(sorder_pins)/sizeof(*sorder_pins),waits=sizeof(sorder_wait_pins)/sizeof(*sorder_wait_pins),
                   syncs=sizeof(sorder_sync_pins)/sizeof(*sorder_sync_pins);
    unsigned total=base+(sorder_mode>=SORDER_WAIT?waits:0)+(sorder_mode==SORDER_SYNC?syncs:0);
    for(unsigned p=0;p<total;p++) {
        unsigned rva,size;const char *sha;
        if(p<base) {rva=sorder_pins[p].rva;size=sorder_pins[p].size;sha=sorder_pins[p].sha;}
        else if(p<base+waits) {rva=sorder_wait_pins[p-base].rva;size=sorder_wait_pins[p-base].size;sha=sorder_wait_pins[p-base].sha;}
        else {rva=sorder_sync_pins[p-base-waits].rva;size=sorder_sync_pins[p-base-waits].size;sha=sorder_sync_pins[p-base-waits].sha;}
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        for(unsigned i=0;i<SORDER_CALLS;i++) {
            unsigned at=sorder_calls[i].rva;if(at<rva || at+5>rva+size) continue;
            if(sorder_call_state(i)<0) return 0;
            int32_t d=sorder_rel_to(i,image+sorder_calls[i].target);memcpy(copy+(at-rva)+1,&d,4);
        }
        if(!hash_ok(copy,size,sha)) return 0;
    }
    return 1;
}
static unsigned char *sorder_alloc_near(unsigned size) {
    uintptr_t at=(uintptr_t)(image+sorder_calls[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<SORDER_CALLS;i++) {
                intptr_t lo=(intptr_t)p-(intptr_t)(image+sorder_calls[i].rva+5),hi=(intptr_t)(p+size)-(intptr_t)(image+sorder_calls[i].rva+5);
                fits=lo>=INT32_MIN && hi<=INT32_MAX;
            }
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void *sorder_target(unsigned i) {
    return i==0?(void*)sorder_remove:i==1?(void*)sorder_add:i==2?(void*)sorder_prepass:i==3?(void*)sorder_shape_request:
           i==4?(void*)sorder_mesh_request:(void*)sorder_notify;
}
static int sorder_write_stubs(void) {
    DWORD old,ignored;if(!VirtualProtect(sorder_stubs,SORDER_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<SORDER_CALLS;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=sorder_target(i);
        memcpy(sorder_stub(i),jump,6);memcpy(sorder_stub(i)+6,&target,8);
    }
    int ok=VirtualProtect(sorder_stubs,SORDER_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),sorder_stubs,SORDER_STUB_SIZE);return ok;
}
static int sorder_stubs_owned(void) {
    for(unsigned i=0;i<SORDER_CALLS;i++) {
        void *t=NULL;if(!read_local(sorder_stub(i)+6,&t,8) || t!=sorder_target(i) || sorder_stub(i)[0]!=0xff || sorder_stub(i)[1]!=0x25) return 0;
    }
    return 1;
}
static int sorder_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(sorder_calls[i].rva+1,e,v,4);
}
static int sorder_unpatch(void) {
    for(unsigned i=0;i<SORDER_CALLS;i++) if(sorder_done&(1u<<i)) {
        int state=sorder_call_state(i);
        if(state==1 && sorder_cas(i,sorder_rel_to(i,sorder_stub(i)),sorder_rel_to(i,image+sorder_calls[i].target))) sorder_done&=~(1u<<i);
        else if(state==0) sorder_done&=~(1u<<i);
    }
    return !sorder_done;
}
/* Before the first admitted frame, on the MoveWorker (which runs the apply
 * and the steps). The pre-pass call is patched in wait mode only. */
/* Calls patched per mode: remove, add and notify always; the pre-pass from
 * 'wait'; the two lookups in 'sync'. */
static int sorder_call_used(unsigned i) {return i<2 || i==5 || (i==2 && sorder_mode>=SORDER_WAIT) || ((i==3 || i==4) && sorder_mode==SORDER_SYNC);}
static int sorder_arm(int mode) {
    if(sorder_installed) return stats.active && sorder_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || mode<SORDER_PLACE || mode>SORDER_SYNC) return 0;
    sorder_mode=mode;
    for(unsigned i=0;i<SORDER_CALLS;i++) if(sorder_call_used(i) && sorder_call_state(i)!=0) {sorder_stage="site";sorder_mode=SORDER_OFF;return 0;}
    if(!sorder_pins_ok()) {sorder_stage="pins";sorder_mode=SORDER_OFF;return 0;}
    if(!sorder_stubs) sorder_stubs=sorder_alloc_near(SORDER_STUB_SIZE);
    if(!sorder_stubs) {sorder_stage="memory";sorder_mode=SORDER_OFF;return 0;}
    if(!sorder_write_stubs()) {sorder_stage="stubs";sorder_mode=SORDER_OFF;return 0;}
    sorder_rows=sorder_failures=0;sorder_removals=sorder_moves=sorder_appends=sorder_native=sorder_frame_moves=0;sorder_tick=UINT64_MAX;
    sorder_waits=sorder_wait_passes=sorder_wait_us=sorder_wait_max_us=sorder_last_pending=0;
    sorder_requests=sorder_cached_hits=sorder_loads=sorder_load_us=sorder_load_max_us=sorder_passed=sorder_alt_keys=sorder_mass_forced=0;
    sorder_resident=sorder_invalid=sorder_unloaded=sorder_boot_moves=sorder_boot_forced=0;
    memset(&sorder_cur,0,sizeof(sorder_cur));sorder_restored=0;
    for(unsigned i=SORDER_CALLS;i-->0;) { /* the removal last: every other call passes through until a removal is recorded */
        if(!sorder_call_used(i)) continue;
        if(!sorder_cas(i,sorder_rel_to(i,image+sorder_calls[i].target),sorder_rel_to(i,sorder_stub(i)))) {sorder_unpatch();sorder_stage="cas";return 0;}
        sorder_done|=1u<<i;
    }
    sorder_installed=1;sorder_stage="armed";return 1;
}
static int sorder_owned(void) {
    if(!sorder_installed) return 1;
    for(unsigned i=0;i<SORDER_CALLS;i++) if((sorder_done&(1u<<i)) && sorder_call_state(i)!=1) return 0;
    return sorder_stubs_owned();
}
/* Main-thread tick check; any failure stops the gate with reason 135. */
static int sorder_check(void) {
    if(InterlockedCompareExchange64(&sorder_failures,0,0)) return 0;
    const char *why=!sorder_owned()?"site_changed":!sorder_pins_ok()?"block_changed":sorder_cur.pending?"removal_unpaired":NULL;
    return why?sorder_fail(why):1;
}
static void sorder_restore(void) {
    if(!sorder_installed) return;
    if(sorder_unpatch()) {sorder_installed=0;sorder_restored=1;}
}
static const char *sorder_mode_name(void) {return sorder_mode==SORDER_SYNC?"sync":sorder_mode==SORDER_WAIT?"wait":sorder_mode==SORDER_PLACE?"place":"off";}
static int sorder_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&sorder_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)SORDER_RING && index<(uint64_t)(count-(LONG64)SORDER_RING)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const sorder_row *r=&sorder_ring[index%SORDER_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu from=%u to=%u children=%u",(unsigned long long)index,(unsigned long long)r->tick,r->from,r->to,r->count);
    return n>0 && (size_t)n<cap?n:0;
}
static int sorder_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"mode=%s installed=%d frame=%llu last_tick=%llu moves=%llu total_moves=%llu removals=%llu appends=%llu native=%llu rows=%lld "
        "waits=%llu wait_passes=%llu wait_us=%llu max_wait_us=%llu last_pending=%llu requests=%llu cached=%llu loads=%llu load_us=%llu "
        "max_load_us=%llu passed=%llu alt_keys=%llu resident=%llu invalid=%llu unloaded=%llu mass_forced=%llu boot_moves=%llu boot_forced=%llu "
        "restored=%d stage=%s failures=%lld",sorder_mode_name(),sorder_installed,
        (unsigned long long)stats.issued,(unsigned long long)(sorder_tick==UINT64_MAX?0:sorder_tick),
        (unsigned long long)sorder_frame_moves,(unsigned long long)sorder_moves,(unsigned long long)sorder_removals,(unsigned long long)sorder_appends,
        (unsigned long long)sorder_native,(long long)sorder_rows,(unsigned long long)sorder_waits,(unsigned long long)sorder_wait_passes,
        (unsigned long long)sorder_wait_us,(unsigned long long)sorder_wait_max_us,(unsigned long long)sorder_last_pending,
        (unsigned long long)sorder_requests,(unsigned long long)sorder_cached_hits,(unsigned long long)sorder_loads,(unsigned long long)sorder_load_us,
        (unsigned long long)sorder_load_max_us,(unsigned long long)sorder_passed,(unsigned long long)sorder_alt_keys,
        (unsigned long long)sorder_resident,(unsigned long long)sorder_invalid,(unsigned long long)sorder_unloaded,
        (unsigned long long)sorder_mass_forced,(unsigned long long)sorder_boot_moves,(unsigned long long)sorder_boot_forced,sorder_restored,sorder_stage,
        (long long)sorder_failures);
    return n>0 && (size_t)n<cap?n:0;
}
