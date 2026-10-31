/* Determinism fix, DEFAULT OFF: native_gate_nav_octree = 'observe' | 'sync' | 'deadline' | 'agreed'.
 *
 * big-BS6 with every shape/mass class closed (shape_order 'sync', s3a/s3b):
 * the first difference is two spawned frigates (80000f09105ebb8a,
 * 80000ff25cdfd254) that keep their spawn velocity and start to accelerate
 * and turn at a tick that differs between processes - 20..24 over the 20
 * traced clients of bo..s3b, always both in the same tick - while the other
 * two frigates (80000f0ff3922735, 80000fa5d6f1a777) start at 17 on every
 * client. Positions, shapes, masses, contacts and the engine parameters'
 * scheduling agree (UpdateEngineParametersEvent: 8 s + FloatState 2 s after
 * creation; the camera cadence is fixed by native_gate_nav_cadence 'far'),
 * so the start waits for something that finishes on wall-clock time.
 *
 * AI::NavOctreeCache (*0x6cf19f8) builds the navigation octrees of objects
 * on its own thread (loop 0x1092190): object creation asks for both octrees
 * of an object (0x75b280 -> 0x1092870, flag 0 and 1; also a lookup miss in
 * 0x1092300 at 0x1092462): the ID goes into a request set (+0x310/+0x350,
 * counts +0x338/+0x378, under +0x2e0) and the semaphore +0x58 is released.
 * The thread pops the lowest ID (0x1092010 at 0x1092233/0x1092265) and builds
 * it (0x1093000 at 0x1092250, under +0x30): ObjectNavOctreeBuilder slot 2
 * (0x1096260) loads the object's mesh with GetAssetIntern(.., 0, 1) and
 * Sleep(1) until the loader has it, NavOctreeBuilder 0x1094100 (call
 * 0x1093308) makes the octree, and the build publishes it in the cache's maps
 * under +0x78 at once (per object ID +0xe0/+0x160, per root +0xa0/+0x120). A
 * build returns 0 for an object in the invalidated sets (+0x260/+0x2a0,
 * filled by the invalidation 0x75b130 - which also drops the object's
 * octrees and pending requests - and swapped each frame by slot 1
 * 0x10922a0); the thread then retries it after Sleep(1). Consumers (DockingBay
 * slot 693, GetDockingApproachPos, Container slot 1104, the pathing in
 * 0x109f480/0x10a2d00 ...) read the maps in their frame through 0x1092300
 * (call sites 0x446a8b, 0x53f129, 0x53fcfa, 0x75b0db): an octree is there in
 * the first frame that starts after its build finished, which is wall-clock
 * time.
 *
 * 'sync': the two pops and the build call of the thread loop go through
 * stubs. During admitted frames a build waits (manual-reset event) until the
 * gate's main-thread join (MoveWorker joined, before FrameTick, the same
 * point as mt_join): the join opens the builds, waits until the thread holds
 * nothing it can build (no build running; no popped ID left, or the popped ID
 * deferred by the invalidated sets) and no request is waiting, then closes
 * them again. So every octree requested in frame N is published between N
 * and N+1 on every client, and none appears inside a frame. big-BS6ns a/b/c
 * passed with it, but the join blocks up to 0.34 s in the spawn's frame.
 *
 * 'deadline' (native_gate_nav_octree_delay = k, default 30): the thread
 * builds as natively, but a new octree is visible only from the frame after
 * the join of its due tick (the request's frame + k), on every client:
 * - requests (0x75b2cd, the tail jump 0x75b2e5, the lookup miss 0x1092462)
 *   open a record (object ID, flag) due at the request's tick + k; a request
 *   for an ID that has a record keeps it;
 * - the new octree (0x1093308) of a build with a record is hidden until the
 *   record's due tick; a build without a record (asked before arming) shows
 *   at once; a build whose object was invalidated (0x7450d3, 0x745255,
 *   0x753510, the tail jump 0x75b2f2, 0x784691) after the builder popped it
 *   is hidden for good (it raced the invalidation that drops it natively; a
 *   new request rebuilds it);
 * - the lookups (0x1092300 at its four call sites) report a hidden octree as
 *   absent: the reference the lookup took is released and, when the caller
 *   asks for a build on a miss, the request is made as the native miss
 *   makes it;
 * - a build that finds its object's root-keyed octree already made (0x1093e60
 *   at 0x10931c0; nothing new is built) shows that octree with its own
 *   record, also when it was hidden for an earlier, invalidated request;
 * - the join of tick D builds every record due by D that the thread has not
 *   built, on the main thread (0x1093000 under +0x30, serialized with the
 *   thread's builds); an object still in the invalidated sets then (fixed by
 *   the frames run so far) moves its due tick to D+1. Then every octree due
 *   by D becomes visible. The pop order (lowest runtime ID) only decides
 *   when the thread builds, never when an octree shows. The join waits only
 *   for a build that is late (inline builds, counted) and, once, at the
 *   first join, for everything asked before it.
 * NAV_OCTREE_ROW per published record (tick of the join, key, flag, inline,
 * build time). max_join_us is the hitch. st3-F49 (three players, start warps
 * into two sectors at tick 22) asked for 75 octrees at once, ~0.33 s each on
 * one builder thread: a deadline join would build most of them inline.
 *
 * 'agreed' (protocol 3 input wire, native_gate_clients): the same records,
 * hooks and lookups as 'deadline', but an octree shows once every client has
 * built it, agreed through the wire, so no join ever builds or waits:
 * - at each join the records opened in the frame just run (and, at the first
 *   join, every request still in the native request sets or held by the
 *   thread) get the next indices in (class, portable key, flag) order;
 *   records without a portable key show natively (counted local);
 * - every tick this client reports in its 'octree' input section (container
 *   kind 0x4f, 20 bytes): W, the first index not built here (built: the
 *   octree made or the record dropped by an invalidation), u32 little endian,
 *   then 128 bits for W+1..W+128 (bit b of byte j = W+1+8j+b). Reports are
 *   cumulative: a tick dropped from a full blob loses nothing;
 * - on admission (every client, identical blobs) an index is agreed when
 *   every slot's latest report covers it; the join of that tick shows it.
 *   The receipt OCT_1|tick|published|agreed|assigned|keys joins the declared
 *   state (keys: a hash of every assigned (class, key, flag) in index order),
 *   so diverging requests stop the session instead of drifting.
 * The pop order still only decides when the thread builds.
 *
 * NAV_OCTREE_LOOKUP rows ('deadline', 'agreed'), per frame and aggregated:
 * every lookup of an object whose own record is pending (miss), every
 * lookup that met a hidden octree (hidden) and every invalidation that
 * dropped a record (invalidated), with the call site (446a8b, 53f129,
 * 53fcfa, 75b0db; invalidations 7450d3, 745255, 753510, 784691, or the tail
 * jump from 0x75b280), key, flag, the caller's build-on-miss argument and a
 * count. A hidden lookup does exactly what the native miss does: null, the
 * reference given back, and the miss's request when asked for. Clients must
 * log identical rows; the first difference names the consumer. Invalidations
 * of objects without a record are not kept for the stale check (st3-F54a ran
 * ~225 per tick, which would wrap its ring within a long build).
 *
 * Content digest ('deadline', 'agreed'): st3-F54a/F55a ('agreed') each lost
 * one container transporter by tick 600 in a sector a player entered, while
 * every lookup row and every published tick agreed; no consumer looked an
 * octree up while it was hidden. The thread builds from the object's live
 * state (ObjectNavOctreeBuilder slot 1 0x1095830: children 0x10c9910 from
 * the bank of the thread's TLS view, their transforms 0x3dde10), so an octree
 * built at another moment can differ. NAV_OCTREE_ROW carries content= (FNV-1a
 * over the octree NavOctreeBuilder 0x1094100 returns: origin +0x10, cell
 * size +0x20, depth +0x40 and the 12-byte cells [+0x28, +0x30)) and cells=.
 *
 * Input snapshot (native_gate_nav_octree_snapshot = true, with any mode;
 * alone it runs 'observe'): st3-F59a confirmed different content for the
 * same octree on different clients (stations 2578f and 27818, both flags,
 * other cell counts on every client; 38 other octrees equal). The builder's
 * slot 1 (ObjectNavOctreeBuilder 0x1095830, vtable entry 0x2c68f70) reads
 * live, frame-banked state through the thread's TLS view (+0x314): the
 * object's children 0x10c9910, their transforms 0x3dde10, part transforms
 * 0x74ccb0. It reads the builder only at +0x10 (the object), +0x40 and
 * +0x180 (the flag) and writes only +0x20..+0x4f (the bounds) and the parts
 * vector +0x188/+0x190/+0x198 (0x50-byte entries: part data, 8 unused,
 * transform 0x40; grown by 0x85b760 when full). So:
 * - every request (the 'deadline' request sites, installed for the snapshot
 *   in every mode) marks its (object, flag) pending; the next gate join
 *   (main thread, MoveWorker parked, the same state on every client) runs
 *   slot 1 on a scratch builder for it - the object taken by ID through
 *   0x86fc70 and given back, the TLS view set to the builder thread's - and
 *   keeps the bounds and the parts;
 * - the builder's slot 1 goes through the vtable entry: a captured object is
 *   filled from its snapshot (bounds copied, parts appended as slot 1 does),
 *   a pending one waits for the join (builder thread only), anything else
 *   (asked for before arming, gone) runs live, counted;
 * - an invalidation drops the object's snapshots; the next request takes a
 *   new one. NAV_OCTREE_ROW carries snapshot=1 when the octree was built
 *   from one. The expensive build (slot 2, the levels) stays in the
 *   background; a join only adds the collections of the frame's requests.
 *
 * 'observe' installs the thread-loop stubs without waiting: NAV_OCTREE_ROW
 * per finished build (tick read on the builder thread, so the frame in which
 * the octree became visible; object key, flag, build time). Status per tick.
 * Pins: the thread loop (with the three calls), the pop, the request, the
 * build and the invalidated-set swap; with 'deadline' also the lookup, the
 * invalidation, the creation request, the octree builder and the blocks of
 * the other call sites. Stop reason 137: a site no longer ours, a pin
 * change, a join timeout, a full record or index table. Stop opens the builds
 * and shows every hidden octree before the sites are restored. */
#define NOCT_CACHE 0x6cf19f8u
#define NOCT_POP 0x1092010u
#define NOCT_BUILD 0x1093000u
#define NOCT_OCTREE 0x1094100u
#define NOCT_REQUEST 0x1092870u
#define NOCT_INVALIDATE 0x75b130u
#define NOCT_GET 0x1092300u
#define NOCT_EXISTING 0x1093e60u /* root-keyed octree already there (maps +0xa0/+0x120) */
#define NOCT_SITES 17u
#define NOCT_BASE_SITES 3u
#define NOCT_JOIN_MS 10000u
#define NOCT_STOP_REASON 137u /* 136 is the engine refresh module's */
#define NOCT_RING 8192u
#define NOCT_STUB_SIZE 0x1000u
#define NOCT_RECORDS 65536u   /* power of two */
#define NOCT_HIDDEN 4096u
#define NOCT_INVALS 4096u
#define NOCT_DUE_MAX 1024u    /* records published per join pass */
#define NOCT_DELAY_DEFAULT 30u
#define NOCT_DELAY_MAX 3600u
#define NOCT_NEVER UINT64_MAX
#define NOCT_ORDER 65536u     /* indices assigned and not yet shown everywhere; power of two */
#define NOCT_FRESH 8192u      /* records opened between two joins */
#define NOCT_REPORT_BYTES 20u
#define NOCT_REPORT_BITS 128u
#define NOCT_LOOK_FRAME 512u   /* distinct lookup rows per frame */
#define NOCT_SLOT1 0x1095830u          /* ObjectNavOctreeBuilder slot 1: collects children, transforms, parts */
#define NOCT_SLOT1_ENTRY 0x2c68f70u    /* its vtable entry */
#define NOCT_LOOKUP 0x86fc70u          /* object by ID, with a reference */
#define NOCT_APPEND 0x85b760u          /* the parts vector's grow-and-append */
#define NOCT_SNAPS 16384u              /* power of two */
#define NOCT_SNAP_ENTRY 0x50u
#define NOCT_SNAP_PARTS 65536u         /* parts one scratch collection may hold (never grown) */
#define NOCT_BUILDER_SIZE 0x1a0u
#define NOCT_LOOK_RING 16384u
enum {NOCT_K_POP,NOCT_K_BUILD,NOCT_K_OCTREE,NOCT_K_REQUEST,NOCT_K_INVALIDATE,NOCT_K_GET,NOCT_K_EXISTING};
static const struct {unsigned rva,target;unsigned char op,kind;} noct_sites[NOCT_SITES]={
    {0x1092233u,NOCT_POP,0xe8,NOCT_K_POP},{0x1092265u,NOCT_POP,0xe8,NOCT_K_POP},{0x1092250u,NOCT_BUILD,0xe8,NOCT_K_BUILD},
    {0x1093308u,NOCT_OCTREE,0xe8,NOCT_K_OCTREE},
    {0x75b2cdu,NOCT_REQUEST,0xe8,NOCT_K_REQUEST},{0x75b2e5u,NOCT_REQUEST,0xe9,NOCT_K_REQUEST},{0x1092462u,NOCT_REQUEST,0xe8,NOCT_K_REQUEST},
    {0x7450d3u,NOCT_INVALIDATE,0xe8,NOCT_K_INVALIDATE},{0x745255u,NOCT_INVALIDATE,0xe8,NOCT_K_INVALIDATE},
    {0x753510u,NOCT_INVALIDATE,0xe8,NOCT_K_INVALIDATE},{0x75b2f2u,NOCT_INVALIDATE,0xe9,NOCT_K_INVALIDATE},
    {0x784691u,NOCT_INVALIDATE,0xe8,NOCT_K_INVALIDATE},
    {0x446a8bu,NOCT_GET,0xe8,NOCT_K_GET},{0x53f129u,NOCT_GET,0xe8,NOCT_K_GET},{0x53fcfau,NOCT_GET,0xe8,NOCT_K_GET},{0x75b0dbu,NOCT_GET,0xe8,NOCT_K_GET},
    {0x10931c0u,NOCT_EXISTING,0xe8,NOCT_K_EXISTING}};
typedef struct {unsigned rva,size;const char *sha;} noct_pin;
static const noct_pin noct_pins[]={
    {0x1092190u,0x10cu,"9bd4e758e141bbb73b5d6e5dfdf67226d0bf8f80b0045f8eebdc8c5c54b52183"},  /* the builder thread's loop */
    {0x1092010u,0x17bu,"cd8718cb641be3f6eb18554fbd28865694d05e383e84695dc2eeaf2795f70951"},  /* pop */
    {0x1092870u,0xaeu,"7d10ea937968d75b77512f627aaa1d3707e6440c8d1b956a632f642ba16b3cbd"},   /* request */
    {0x1093000u,0x5cau,"3d5817331c409a6568c69db73b96bf19aef70d79d844a20aa48908f9a4c16f9c"},  /* build */
    {0x10922a0u,0x5du,"ae205e6e042310c92045fdb331143f3b82528f61c49cc663c51a57882999b6e4"}};  /* invalidated-set swap */
static const noct_pin noct_deadline_pins[]={
    {0x1092300u,0x18au,"21f3a5bbc6d6735fb281b8aee969285b8d8d245f4c5350f922dc3f33ada30d48"},  /* lookup */
    {0x75b130u,0x144u,"978c046cec382b2f6728b1b288fea58a2dd84fa96b043a2a1c0419cfa6fbd96b"},   /* invalidation */
    {0x75b280u,0x77u,"6b767bfe0c4003fa1902a9f8798858e542d872dd34bceeb4be140d6054504b5d"},    /* creation request */
    {0x446a70u,0x29u,"96c521c25c1b3f28cf1f6c464eff7749cfe2bacf64f8c8266787da0b7c462875"},    /* lookup caller */
    {0x75b090u,0x7du,"4f22c3862459f99918ab5220e8c49ba339f508745efd6702350494a9473c9159"},    /* lookup caller (Positional slot 891) */
    {0x1094100u,0x23fu,"0e6ad78fc8a3159dc99605c278c20e986b04c98b1ae35a5bad1f2f5338244769"},  /* NavOctreeBuilder: the new octree */
    {0x1093e60u,0xc5u,"c97122725335701cc81e8cd337dedb74579196df88c03a3b7ea75f01a05c9cca"},   /* root-keyed octree lookup */
    {0x7450c3u,0x15u,"319e20feccdc1a56fe75ff1b431e674e595f60f608ba439b391237901515a7de"},    /* invalidation call blocks */
    {0x745245u,0x15u,"4d430e5265e716ba160b8d1b05d3246c626afdb3c855cad2cd3b65048ae3b22a"},
    {0x753500u,0x15u,"f02cf2c9067105b708a3d4cc149c3c1d6b9c7e9d042cbc78a9f3191dbb17e483"},
    {0x784681u,0x15u,"d213929185816c35b694d54780fee56eae32f7b2a7c8a8dddcaa6dacd7ab5de4"},
    {0x53f119u,0x15u,"3c2fd150ef069c0d66218cb265a66c6bc2197fdbaa2cb5a4d44900bb1f8224a1"},    /* lookup call blocks */
    {0x53fceau,0x15u,"059ef721a8d74f34492b9925ff0c226d3d94625fa630c4f4146f349bf448b7a4"}};
static const noct_pin noct_snapshot_pins[]={
    {0x1095830u,0xa30u,"4c166c7482ddfa39b21739ea01f2aaed242a5e0798824e8a29a224837d48a00e"},  /* builder slot 1 */
    {0x86fc70u,0x90u,"38569f64a87057d7373b5a33becb7e7446bbcf021b192a22fc296fd4ee5a8576"},    /* object by ID */
    {0x85b760u,0x295u,"6ff0046c237d6e37836a83475715bacd648c52e94f6862e869d5c03965703d90"},   /* parts grow-and-append */
    {0x1092300u,0x18au,"21f3a5bbc6d6735fb281b8aee969285b8d8d245f4c5350f922dc3f33ada30d48"},  /* the request sites' blocks */
    {0x75b130u,0x144u,"978c046cec382b2f6728b1b288fea58a2dd84fa96b043a2a1c0419cfa6fbd96b"},
    {0x75b280u,0x77u,"6b767bfe0c4003fa1902a9f8798858e542d872dd34bceeb4be140d6054504b5d"},
    {0x7450c3u,0x15u,"319e20feccdc1a56fe75ff1b431e674e595f60f608ba439b391237901515a7de"},
    {0x745245u,0x15u,"4d430e5265e716ba160b8d1b05d3246c626afdb3c855cad2cd3b65048ae3b22a"},
    {0x753500u,0x15u,"f02cf2c9067105b708a3d4cc149c3c1d6b9c7e9d042cbc78a9f3191dbb17e483"},
    {0x784681u,0x15u,"d213929185816c35b694d54780fee56eae32f7b2a7c8a8dddcaa6dacd7ab5de4"}};
enum {NOCT_OFF,NOCT_OBSERVE,NOCT_SYNC,NOCT_DEADLINE,NOCT_AGREED};
enum {NOCT_R_FREE,NOCT_R_REQUESTED,NOCT_R_BUILT,NOCT_R_NOOP,NOCT_R_VISIBLE};
typedef uint64_t (*noct_pop_fn)(void *,uint8_t *);
typedef uint8_t (*noct_build_fn)(void *,uint64_t,uint32_t);
typedef void *(*noct_octree_fn)(void *);
typedef void (*noct_request_fn)(void *,uint64_t,uint32_t);
typedef void (*noct_invalidate_fn)(void *);
typedef void **(*noct_get_fn)(void *,void **,void *,uint32_t,uint32_t);
typedef void **(*noct_existing_fn)(void *,void **,void *,uint32_t);
typedef struct {uint64_t tick,id,us,due,index,content,cells;uint8_t flag,joined,snapshot;} noct_row;
/* A request: key = ID*2+flag. */
typedef struct {uint64_t key,due,gen,us,index,opened,content,cells;void *octree;uint8_t state,inlined,snapshot;} noct_record; /* index: assigned index + 1 */
typedef struct {void *octree;uint64_t due,key;} noct_hidden_entry;
typedef struct {uint64_t id,serial;} noct_inval;
/* Per thread: the build in progress (builder thread or the join's inline build). */
typedef struct {uint64_t key,gen,serial,pop_serial;int has,valid,built;void *existing;} noct_context; /* existing: a build's no-op octree */
static _Thread_local noct_context noct_ctx;
static int noct_installed,noct_restored,noct_mode;
static unsigned noct_done,noct_delay=NOCT_DELAY_DEFAULT;
static unsigned char *noct_stubs;
static CRITICAL_SECTION noct_lock;static int noct_lock_ready;
static HANDLE noct_open;                  /* manual reset; signaled = builds may run */
static int noct_closed,noct_busy,noct_held,noct_deferred,noct_joining,noct_drained,noct_drain_join;
static noct_row noct_ring[NOCT_RING];
static noct_record noct_records[NOCT_RECORDS];
static noct_hidden_entry noct_hidden[NOCT_HIDDEN];
static noct_inval noct_invals[NOCT_INVALS];
static unsigned noct_hidden_count,noct_record_count,noct_live;   /* live: requested, built or no-op, not yet shown */
static uint64_t noct_gen,noct_serial,noct_inval_count,noct_visible_tick,noct_next_due=NOCT_NEVER;
static volatile LONG64 noct_rows,noct_failures;
static uint64_t noct_pops,noct_builds,noct_deferrals,noct_join_builds,noct_free_builds,noct_build_us,noct_max_build_us,noct_waited;
static uint64_t noct_joins,noct_join_passes,noct_join_us,noct_max_join_us,noct_last_pending,noct_max_pending,noct_held_deferred;
/* 'agreed': index -> record (key, generation), agreement and publication. */
typedef struct {uint64_t key,gen,assigned;uint8_t agreed,shown;} noct_order_entry;
static noct_order_entry noct_order[NOCT_ORDER];
static uint64_t noct_fresh[NOCT_FRESH];static unsigned noct_fresh_count;
static uint64_t noct_next_index,noct_shown_index,noct_local_w,noct_agreed_total,noct_keys_hash=0xcbf29ce484222325ull,noct_local_records,
                noct_reports,noct_max_wait,noct_held_id;
static uint32_t noct_held_flag;
static int noct_enumerated;
typedef struct {uint64_t w;unsigned char bits[NOCT_REPORT_BITS/8];int seen;} noct_slot_report;
/* Lookup and invalidation rows: site (0..3 lookups, 4..8 invalidations, 9 other), result. */
enum {NOCT_L_MISS,NOCT_L_HIDDEN,NOCT_L_VISIBLE,NOCT_L_INVALIDATED};
typedef struct {uint64_t tick,id;uint32_t count;uint8_t site,flag,ask,result;} noct_look;
static noct_look noct_look_frame[NOCT_LOOK_FRAME],noct_look_ring[NOCT_LOOK_RING];
static unsigned noct_look_frame_count;
static volatile LONG64 noct_looks;
static uint64_t noct_look_overflow;
static const unsigned noct_look_returns[9]={0x446a90u,0x53f12eu,0x53fcffu,0x75b0e0u,0x7450d8u,0x74525au,0x753515u,0x784696u,0};
static const char *const noct_look_sites[10]={"446a8b","53f129","53fcfa","75b0db","7450d3","745255","753510","784691","tail","other"};
static const char *const noct_look_results[4]={"miss","hidden","visible","invalidated"};
static unsigned noct_site_of(const void *ret,int invalidation) {
    uintptr_t at=(uintptr_t)((const unsigned char *)ret-image);
    for(unsigned i=invalidation?4:0;i<(invalidation?8u:4u);i++) if(at==noct_look_returns[i]) return i;
    return invalidation?8u:9u;
}
/* Under the lock: one row per (site, object, flag, ask, result) and frame, counted. */
static void noct_look_note(uint64_t id,unsigned site,unsigned flag,unsigned ask,unsigned result) {
    for(unsigned i=0;i<noct_look_frame_count;i++) {
        noct_look *l=&noct_look_frame[i];
        if(l->id==id && l->site==site && l->flag==flag && l->ask==ask && l->result==result) {++l->count;return;}
    }
    if(noct_look_frame_count>=NOCT_LOOK_FRAME) {++noct_look_overflow;return;}
    noct_look_frame[noct_look_frame_count++]=(noct_look){0,id,1,(uint8_t)site,(uint8_t)flag,(uint8_t)ask,(uint8_t)result};
}
/* Join: the frame's rows into the ring with the frame's tick. Under the lock. */
static void noct_look_flush(uint64_t tick) {
    for(unsigned i=0;i<noct_look_frame_count;i++) {
        noct_look l=noct_look_frame[i];l.tick=tick;
        LONG64 at=InterlockedIncrement64(&noct_looks)-1;noct_look_ring[(uint64_t)at%NOCT_LOOK_RING]=l;
    }
    noct_look_frame_count=0;
}
static noct_slot_report noct_slot_reports[GATE_CLIENTS_MAX];
static uint64_t noct_requested,noct_published,noct_hides,noct_rerequests,noct_invalidations,noct_stale,noct_unrecorded,noct_inline,
                noct_inline_us,noct_max_inline_us,noct_postponed,noct_max_records,noct_max_hidden,noct_drain_us;
static const char *noct_stage="idle";
static int noct_fail(const char *why) {noct_stage=why;InterlockedIncrement64(&noct_failures);return 0;}
static uint64_t noct_us(LARGE_INTEGER from,LARGE_INTEGER to,LARGE_INTEGER frequency) {
    return frequency.QuadPart && to.QuadPart>=from.QuadPart?(uint64_t)((to.QuadPart-from.QuadPart)*1000000/frequency.QuadPart):0;
}
/* Requests waiting in the two sets (counts read as words; -1 unreadable). */
static int64_t noct_pending(const unsigned char *cache) {
    uint64_t a=0,b=0;
    if(!cache || !created_read(cache+0x338,&a,8) || !created_read(cache+0x378,&b,8)) return -1;
    return (int64_t)(a+b);
}
/* ---- deadline tables (all under noct_lock) ---- */
static unsigned noct_slot(uint64_t key) {uint64_t h=key*0x9e3779b97f4a7c15ull;return (unsigned)(h>>48)&(NOCT_RECORDS-1);}
static noct_record *noct_find(uint64_t key) {
    for(unsigned i=noct_slot(key),n=0;n<NOCT_RECORDS;i=(i+1)&(NOCT_RECORDS-1),n++) {
        if(noct_records[i].state==NOCT_R_FREE) return NULL;
        if(noct_records[i].key==key) return &noct_records[i];
    }
    return NULL;
}
static noct_record *noct_insert(uint64_t key) {
    if(noct_record_count>=NOCT_RECORDS*3/4) {noct_fail("records_full");return NULL;}
    unsigned i=noct_slot(key);
    while(noct_records[i].state!=NOCT_R_FREE) i=(i+1)&(NOCT_RECORDS-1);
    memset(&noct_records[i],0,sizeof(noct_records[i]));noct_records[i].key=key;
    if(++noct_record_count>noct_max_records) noct_max_records=noct_record_count;
    return &noct_records[i];
}
/* Linear probing: backward shift keeps every chain whole. */
static int noct_is_live(const noct_record *r) {return r->state==NOCT_R_REQUESTED || r->state==NOCT_R_BUILT || r->state==NOCT_R_NOOP;}
static void noct_erase(noct_record *r) {
    if(noct_is_live(r)) --noct_live;
    unsigned i=(unsigned)(r-noct_records),j=i;
    memset(&noct_records[i],0,sizeof(noct_records[i]));--noct_record_count;
    for(;;) {
        j=(j+1)&(NOCT_RECORDS-1);
        if(noct_records[j].state==NOCT_R_FREE) return;
        unsigned home=noct_slot(noct_records[j].key);
        if((j>i && (home<=i || home>j)) || (j<i && home<=i && home>j)) {noct_records[i]=noct_records[j];memset(&noct_records[j],0,sizeof(noct_records[j]));i=j;}
    }
}
static int noct_hidden_find(const void *octree) {
    for(unsigned i=0;i<noct_hidden_count;i++) if(noct_hidden[i].octree==octree) return (int)i;
    return -1;
}
static void noct_hidden_drop(unsigned i) {noct_hidden[i]=noct_hidden[--noct_hidden_count];}
/* The octree shows: no entry for it or for its key (an older octree the map no longer holds). */
static void noct_show(const void *octree,uint64_t key) {
    for(unsigned i=noct_hidden_count;i-->0;) if(noct_hidden[i].octree==octree || noct_hidden[i].key==key) noct_hidden_drop(i);
}
static int noct_hide(void *octree,uint64_t due,uint64_t key) {
    noct_show(octree,key);
    if(noct_hidden_count>=NOCT_HIDDEN) return noct_fail("hidden_full");
    noct_hidden[noct_hidden_count++]=(noct_hidden_entry){octree,due,key};
    if(noct_hidden_count>noct_max_hidden) noct_max_hidden=noct_hidden_count;
    return 1;
}
static int noct_invalidated_since(uint64_t id,uint64_t serial) {
    uint64_t n=noct_inval_count<NOCT_INVALS?noct_inval_count:NOCT_INVALS;
    for(uint64_t k=0;k<n;k++) {
        const noct_inval *v=&noct_invals[(noct_inval_count-1-k)%NOCT_INVALS];
        if(v->serial<=serial) return 0;
        if(v->id==id) return 1;
    }
    return noct_inval_count>NOCT_INVALS; /* the ring wrapped past the build's start: assume yes */
}
/* ---- input snapshot ---- */
typedef void (*noct_slot1_fn)(void *);
typedef void **(*noct_lookup_fn)(void **,uint64_t);
typedef void (*noct_append_fn)(void *,void *,void *,void *);
enum {NOCT_S_FREE,NOCT_S_PENDING,NOCT_S_CAPTURED,NOCT_S_GONE};
typedef struct {uint64_t key,tick;unsigned char *parts;uint32_t count;uint8_t state;unsigned char box[0x30];} noct_snap;
static noct_snap noct_snaps[NOCT_SNAPS];
static unsigned noct_snap_count;
static uint64_t noct_snap_fresh[NOCT_FRESH];static unsigned noct_snap_fresh_count;
static int noct_snapshot,noct_slot1_swapped;
static noct_slot1_fn noct_slot1_original;static noct_lookup_fn noct_lookup_original;static noct_append_fn noct_append_original;
static HANDLE noct_snap_event;           /* manual reset; set when a join has taken its snapshots */
static unsigned char *noct_snap_scratch; /* NOCT_SNAP_PARTS entries */
static __attribute__((aligned(16))) unsigned char noct_scratch_builder[NOCT_BUILDER_SIZE];
static volatile LONG noct_builder_view=-1;static LONG noct_main_view=-1;
static uint64_t noct_snap_captured,noct_snap_gone,noct_snap_used,noct_snap_live,noct_snap_waits,noct_snap_max_parts,noct_snap_us,
                noct_snap_max_us,noct_snap_dropped;
static _Thread_local int noct_tls_snapshot; /* the last slot 1 on this thread was filled from a snapshot */
static unsigned noct_snap_slot(uint64_t key) {uint64_t h=key*0xc2b2ae3d27d4eb4full;return (unsigned)(h>>48)&(NOCT_SNAPS-1);}
static noct_snap *noct_snap_find(uint64_t key) {
    for(unsigned i=noct_snap_slot(key),n=0;n<NOCT_SNAPS;i=(i+1)&(NOCT_SNAPS-1),n++) {
        if(noct_snaps[i].state==NOCT_S_FREE) return NULL;
        if(noct_snaps[i].key==key) return &noct_snaps[i];
    }
    return NULL;
}
static void noct_snap_erase(noct_snap *e) {
    unsigned i=(unsigned)(e-noct_snaps),j=i;
    free(e->parts);memset(&noct_snaps[i],0,sizeof(noct_snaps[i]));--noct_snap_count;
    for(;;) {
        j=(j+1)&(NOCT_SNAPS-1);
        if(noct_snaps[j].state==NOCT_S_FREE) return;
        unsigned home=noct_snap_slot(noct_snaps[j].key);
        if((j>i && (home<=i || home>j)) || (j<i && home<=i && home>j)) {noct_snaps[i]=noct_snaps[j];memset(&noct_snaps[j],0,sizeof(noct_snaps[j]));i=j;}
    }
}
/* Under the lock: a request marks (object, flag) for the next join's snapshot. */
static void noct_snap_note(uint64_t id,uint32_t flag) {
    uint64_t key=id*2+(flag&1u);
    if(!noct_snapshot || !id || noct_snap_find(key)) return;
    if(noct_snap_count>=NOCT_SNAPS*3/4 || noct_snap_fresh_count>=NOCT_FRESH) {noct_fail("snapshots_full");return;}
    unsigned i=noct_snap_slot(key);while(noct_snaps[i].state!=NOCT_S_FREE) i=(i+1)&(NOCT_SNAPS-1);
    memset(&noct_snaps[i],0,sizeof(noct_snaps[i]));noct_snaps[i].key=key;noct_snaps[i].state=NOCT_S_PENDING;++noct_snap_count;
    noct_snap_fresh[noct_snap_fresh_count++]=key;
}
/* Under the lock: an invalidation drops the object's snapshots. */
static void noct_snap_drop(uint64_t id) {
    for(unsigned flag=0;flag<2;flag++) {noct_snap *e=noct_snap_find(id*2+flag);if(e) {noct_snap_erase(e);++noct_snap_dropped;}}
}
/* The engine's per-thread block (TLS slot 0, as gs:[0x58] in X4); its +0x314 is the thread's bank view.
 * Unit tests point noct_view_override at a buffer of their own. */
static unsigned char *noct_view_override;
static unsigned char *noct_tls_block(void) {
    if(noct_view_override) return noct_view_override;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots?(unsigned char*)slots[0]:NULL;
}
/* The engine's release of a reference taken by 0x86fc70 (count +0x40, state +0x44, holder +0x38). */
static void noct_object_release(unsigned char *object) {
    if(InterlockedDecrement((volatile LONG*)(object+0x40))) return;
    if(InterlockedCompareExchange((volatile LONG*)(object+0x44),3,2)==2) {void **holder=(void**)(object+0x38);((void (*)(void*))(*(void***)holder)[0])(holder);}
}
/* Join, main thread (MoveWorker parked): slot 1 on a scratch builder for every pending (object, flag). */
static void noct_snap_capture(uint64_t tick) {
    if(!noct_snapshot || !noct_lock_ready) return;
    static uint64_t keys[NOCT_FRESH];unsigned n;
    EnterCriticalSection(&noct_lock);
    n=noct_snap_fresh_count;memcpy(keys,noct_snap_fresh,n*sizeof(*keys));noct_snap_fresh_count=0;
    LeaveCriticalSection(&noct_lock);
    if(!n) return;
    if(noct_snap_event) ResetEvent(noct_snap_event);
    unsigned char *tls=noct_tls_block();
    for(unsigned k=0;k<n;k++) {
        EnterCriticalSection(&noct_lock);
        noct_snap *e=noct_snap_find(keys[k]);int pending=e && e->state==NOCT_S_PENDING;
        LeaveCriticalSection(&noct_lock);
        if(!pending) continue;
        void *object=NULL;noct_lookup_original(&object,keys[k]>>1);
        if(!object) {EnterCriticalSection(&noct_lock);if((e=noct_snap_find(keys[k]))) e->state=NOCT_S_GONE;LeaveCriticalSection(&noct_lock);++noct_snap_gone;continue;}
        unsigned char *b=noct_scratch_builder,*begin=noct_snap_scratch,*cap=noct_snap_scratch+(size_t)NOCT_SNAP_PARTS*NOCT_SNAP_ENTRY;
        memset(b,0,NOCT_BUILDER_SIZE);memcpy(b+0x10,&object,8);b[0x180]=(unsigned char)(keys[k]&1u);
        memcpy(b+0x188,&begin,8);memcpy(b+0x190,&begin,8);memcpy(b+0x198,&cap,8);
        LONG view=InterlockedCompareExchange(&noct_builder_view,0,0),saved=0;
        if(tls) {memcpy(&saved,tls+0x314,4);noct_main_view=saved;LONG want=view>=0?view:0;memcpy(tls+0x314,&want,4);}
        LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);
        noct_slot1_original(b);
        QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=noct_us(t0,t1,frequency);
        if(tls) memcpy(tls+0x314,&saved,4);
        unsigned char *got=NULL,*end=NULL;memcpy(&got,b+0x188,8);memcpy(&end,b+0x190,8);
        noct_object_release(object);
        if(got!=begin || end<begin || (size_t)(end-begin)%NOCT_SNAP_ENTRY) {noct_fail("snapshot_parts");return;} /* never reached: the scratch never fills */
        uint32_t count=(uint32_t)((size_t)(end-begin)/NOCT_SNAP_ENTRY);
        unsigned char *parts=count?malloc((size_t)count*NOCT_SNAP_ENTRY):NULL;
        if(count && !parts) {noct_fail("snapshot_memory");return;}
        if(count) memcpy(parts,begin,(size_t)count*NOCT_SNAP_ENTRY);
        EnterCriticalSection(&noct_lock);
        if((e=noct_snap_find(keys[k])) && e->state==NOCT_S_PENDING) {
            e->parts=parts;e->count=count;memcpy(e->box,b+0x20,sizeof(e->box));e->tick=tick;e->state=NOCT_S_CAPTURED;parts=NULL;
            ++noct_snap_captured;noct_snap_us+=us;if(us>noct_snap_max_us) noct_snap_max_us=us;if(count>noct_snap_max_parts) noct_snap_max_parts=count;
        }
        LeaveCriticalSection(&noct_lock);
        free(parts);
    }
    if(noct_snap_event) SetEvent(noct_snap_event);
}
/* Builder slot 1 (vtable entry): from the snapshot when the object has one. */
static void noct_slot1_with(noct_slot1_fn original,noct_append_fn append,unsigned char *builder) {
    noct_tls_snapshot=0;
    if(!noct_snapshot || !noct_lock_ready || !noct_installed) {original(builder);return;}
    int main=GetCurrentThreadId()==main_thread;unsigned char *tls=noct_tls_block();
    if(!main && tls) {LONG view;memcpy(&view,tls+0x314,4);InterlockedExchange(&noct_builder_view,view);}
    unsigned char *object=NULL;uint64_t id=0;
    if(!created_read(builder+0x10,&object,8) || !object || !created_read(object+8,&id,8) || !id) {++noct_snap_live;original(builder);return;}
    uint64_t key=id*2+(builder[0x180]&1u);
    for(;;) {
        EnterCriticalSection(&noct_lock);
        noct_snap *e=noct_snap_find(key);int state=e?e->state:NOCT_S_FREE;
        if(state==NOCT_S_CAPTURED) {
            memcpy(builder+0x20,e->box,sizeof(e->box));
            for(uint32_t i=0;i<e->count;i++) {
                unsigned char *entry=e->parts+(size_t)i*NOCT_SNAP_ENTRY,*end=NULL,*cap=NULL;
                memcpy(&end,builder+0x190,8);memcpy(&cap,builder+0x198,8);
                if(end!=cap) {memcpy(end,entry,NOCT_SNAP_ENTRY);end+=NOCT_SNAP_ENTRY;memcpy(builder+0x190,&end,8);}
                else append(builder+0x188,end,entry,entry+0x10);
            }
            ++noct_snap_used;noct_tls_snapshot=1;
            LeaveCriticalSection(&noct_lock);
            return;
        }
        int wait=state==NOCT_S_PENDING && !main && noct_installed && !InterlockedCompareExchange64(&noct_failures,0,0);
        if(wait) ++noct_snap_waits;
        LeaveCriticalSection(&noct_lock);
        if(!wait) break;
        WaitForSingleObject(noct_snap_event,50);
    }
    ++noct_snap_live;original(builder);
}
static void noct_slot1(void *builder) {noct_slot1_with(noct_slot1_original,noct_append_original,builder);}
static int noct_deadline(void) {return noct_mode>=NOCT_DEADLINE && noct_installed && noct_lock_ready;} /* deadline or agreed: records and hiding */
/* The MoveWorker's frame: a request's due tick. */
static uint64_t noct_frame(void) {return *(volatile uint64_t*)&stats.issued;}
/* A request: open a record unless the object already has one. Under the lock. */
static void noct_note_request(uint64_t id,uint32_t flag) {
    uint64_t key=id*2+(flag&1);
    if(!id || noct_find(key)) return;
    noct_record *r=noct_insert(key);if(!r) return;
    r->state=NOCT_R_REQUESTED;r->gen=++noct_gen;r->opened=noct_frame();++noct_requested;++noct_live;
    if(noct_mode==NOCT_AGREED) {
        r->due=NOCT_NEVER; /* shown when agreed */
        if(noct_fresh_count>=NOCT_FRESH) {noct_fail("fresh_full");return;}
        noct_fresh[noct_fresh_count++]=key;
    } else {
        r->due=noct_frame()+noct_delay;
        if(r->due<noct_next_due) noct_next_due=r->due;
    }
}
/* Builder thread: pops under the module's lock (see the join). */
static uint64_t noct_pop_with(noct_pop_fn original,void *cache,uint8_t *flag) {
    if(!noct_lock_ready) return original(cache,flag);
    EnterCriticalSection(&noct_lock);
    uint64_t id=original(cache,flag);
    if(id) {noct_held=1;noct_deferred=0;++noct_pops;noct_ctx.pop_serial=noct_serial;noct_held_id=id;noct_held_flag=*flag&1u;} else noct_held=0;
    LeaveCriticalSection(&noct_lock);
    return id;
}
/* Builder thread: in 'sync' a build waits while the builds are closed; in
 * 'deadline' it carries its record into the octree hook. */
static uint8_t noct_build_with(noct_build_fn original,void *cache,uint64_t id,uint32_t flag) {
    if(!noct_lock_ready) return original(cache,id,flag);
    int joined=0;uint64_t key=id*2+(flag&1);
    EnterCriticalSection(&noct_lock);
    if(noct_mode==NOCT_SYNC && noct_closed) {
        ++noct_waited;
        while(noct_closed) {LeaveCriticalSection(&noct_lock);WaitForSingleObject(noct_open,INFINITE);EnterCriticalSection(&noct_lock);}
    }
    noct_busy=1;joined=noct_joining;
    if(noct_mode>=NOCT_DEADLINE) {
        noct_record *r=noct_find(key);
        noct_ctx=(noct_context){key,r?r->gen:0,noct_ctx.pop_serial,noct_ctx.pop_serial,r!=NULL,1,0,NULL};
    }
    LeaveCriticalSection(&noct_lock);
    LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);
    uint8_t result=original(cache,id,flag);
    QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=noct_us(t0,t1,frequency);
    EnterCriticalSection(&noct_lock);
    noct_busy=0;
    if(result) {
        noct_held=0;noct_deferred=0;++noct_builds;noct_build_us+=us;if(us>noct_max_build_us) noct_max_build_us=us;
        if(joined) ++noct_join_builds;else ++noct_free_builds;
        if(noct_mode>=NOCT_DEADLINE) {
            noct_record *r=noct_find(key);
            if(r && noct_ctx.has && r->gen==noct_ctx.gen) {
                r->us=us;
                if(!noct_ctx.built && r->state==NOCT_R_REQUESTED) {r->state=NOCT_R_NOOP;r->octree=noct_ctx.existing;}
            }
        } else {
            LONG64 at=InterlockedIncrement64(&noct_rows)-1;
            noct_ring[(uint64_t)at%NOCT_RING]=(noct_row){noct_frame(),id,us,0,0,0,0,(uint8_t)flag,(uint8_t)joined,(uint8_t)noct_tls_snapshot};
        }
    } else {noct_deferred=1;++noct_deferrals;}
    noct_ctx.valid=0;
    LeaveCriticalSection(&noct_lock);
    return result;
}
/* FNV-1a over an octree's content (header fields and its cell array). */
static uint64_t noct_content(const unsigned char *octree,uint64_t *cells) {
    uint64_t h=0xcbf29ce484222325ull;unsigned char head[0x18],level=0;const unsigned char *begin=NULL,*end=NULL;
    *cells=0;
    if(!created_read(octree+0x10,head,0x14) || !created_read(octree+0x40,&level,1) ||
       !created_read(octree+0x28,&begin,8) || !created_read(octree+0x30,&end,8) || end<begin) return 0;
    head[0x14]=level;
    for(unsigned i=0;i<0x15;i++) {h^=head[i];h*=0x100000001b3ull;}
    uint64_t bytes=(uint64_t)(end-begin);if(bytes>(64ull<<20)) return 0;
    *cells=bytes/12;
    static _Thread_local unsigned char chunk[4096];
    for(uint64_t at=0;at<bytes;) {
        size_t n=bytes-at>sizeof(chunk)?sizeof(chunk):(size_t)(bytes-at);
        if(!created_read(begin+at,chunk,n)) return 0;
        for(size_t i=0;i<n;i++) {h^=chunk[i];h*=0x100000001b3ull;}
        at+=n;
    }
    return h|1u;
}
/* Inside a build ('deadline'): the octree just made, hidden until its record is due. */
static void *noct_octree_with(noct_octree_fn original,void *builder) {
    void *octree=original(builder);
    if(!octree || !noct_ctx.valid || !noct_lock_ready) return octree;
    uint64_t cells=0,content=noct_content(octree,&cells); /* outside the lock: the cells may be many */
    EnterCriticalSection(&noct_lock);
    uint64_t key=noct_ctx.key,id=key>>1;noct_ctx.built=1;
    noct_record *r=noct_find(key);
    if(r) {r->content=content;r->cells=cells;r->snapshot=(uint8_t)noct_tls_snapshot;}
    if(noct_invalidated_since(id,noct_ctx.serial) || (noct_ctx.has && (!r || r->gen!=noct_ctx.gen))) {
        /* Popped before an invalidation that drops it natively. Without a newer
         * request it stays hidden (a client that lost the race has none); a
         * newer request's build replaces it, or it stands for that build. */
        ++noct_stale;
        if(r && r->state==NOCT_R_VISIBLE) noct_show(octree,key);
        else if(r && (r->state==NOCT_R_BUILT || r->state==NOCT_R_NOOP)) {r->octree=octree;noct_hide(octree,r->due,key);}
        else noct_hide(octree,NOCT_NEVER,key);
    } else if(r && r->state!=NOCT_R_VISIBLE) { /* its record (also one opened while a build begun before it ran) */
        r->state=NOCT_R_BUILT;r->octree=octree;noct_hide(octree,r->due,key);
    } else { /* shown already, or asked before arming */
        noct_show(octree,key);if(!r) ++noct_unrecorded;
    }
    LeaveCriticalSection(&noct_lock);
    return octree;
}
/* MoveWorker (and lookups): a request opens its record before the native request. */
static void noct_request_with(noct_request_fn original,void *cache,uint64_t id,uint32_t flag) {
    if(noct_installed && noct_lock_ready && (noct_deadline() || noct_snapshot)) {
        EnterCriticalSection(&noct_lock);
        if(noct_deadline()) noct_note_request(id,flag);
        noct_snap_note(id,flag);
        LeaveCriticalSection(&noct_lock);
    }
    original(cache,id,flag);
}
/* The invalidation drops the object's octrees and requests natively; its
 * records go after it, and builds popped before it become stale. */
static void noct_invalidate_with(noct_invalidate_fn original,unsigned site,void *object) {
    original(object);
    uint64_t id=0;
    if(!noct_installed || !noct_lock_ready || !(noct_deadline() || noct_snapshot) || !object || !created_read((unsigned char*)object+8,&id,8) || !id) return;
    EnterCriticalSection(&noct_lock);
    if(noct_snapshot) noct_snap_drop(id);
    if(!noct_deadline()) {++noct_invalidations;LeaveCriticalSection(&noct_lock);return;}
    int kept=noct_held && noct_held_id==id; /* a build popped before this invalidation becomes stale */
    for(unsigned flag=0;flag<2;flag++) {
        noct_record *r=noct_find(id*2+flag);
        if(r) {noct_look_note(id,site,flag,0,NOCT_L_INVALIDATED);noct_erase(r);kept=1;}
    }
    ++noct_invalidations;
    if(kept) {++noct_serial;noct_invals[noct_inval_count%NOCT_INVALS]=(noct_inval){id,noct_serial};++noct_inval_count;}
    LeaveCriticalSection(&noct_lock);
}
/* A lookup: a hidden octree reads as absent (the reference it took is given
 * back; the request a miss would make is made). */
static void **noct_get_with(noct_get_fn original,noct_request_fn request,unsigned site,void *cache,void **out,void *object,uint32_t flag,
                            uint32_t ask) {
    void **result=original(cache,out,object,flag,ask);
    if(!noct_deadline() || !out) return result;
    void *octree=*out;int hidden=0;uint64_t id=0;
    if(object) created_read((unsigned char*)object+8,&id,8);
    EnterCriticalSection(&noct_lock);
    if(octree) {hidden=noct_hidden_find(octree)>=0;if(hidden) ++noct_hides;}
    const noct_record *r=id?noct_find(id*2+(flag&1u)):NULL;
    if(hidden || (r && noct_is_live(r))) noct_look_note(id,site,flag&1u,ask&0xffu?1u:0u,hidden?NOCT_L_HIDDEN:octree?NOCT_L_VISIBLE:NOCT_L_MISS);
    LeaveCriticalSection(&noct_lock);
    if(!hidden) return result;
    InterlockedDecrement((volatile LONG*)octree);*out=NULL;
    if((ask&0xffu) && id) {
        EnterCriticalSection(&noct_lock);++noct_rerequests;LeaveCriticalSection(&noct_lock);
        noct_request_with(request,cache,id,flag&0xffu);
    }
    return result;
}
/* Inside a build (deadline, agreed): the root-keyed octree the build found already made (a no-op
 * build). Its record shows that octree when published, so a copy hidden for an earlier request
 * (e.g. one that raced an invalidation) shows with it on every client. */
static void **noct_existing_with(noct_existing_fn original,void *cache,void **out,void *root,uint32_t flag) {
    void **result=original(cache,out,root,flag);
    if(noct_ctx.valid && result && *result) noct_ctx.existing=*result;
    return result;
}
static void **noct_existing(void *cache,void **out,void *root,uint32_t flag) {
    return noct_existing_with((noct_existing_fn)(void *)(image+NOCT_EXISTING),cache,out,root,flag);
}
static uint64_t noct_pop(void *cache,uint8_t *flag) {return noct_pop_with((noct_pop_fn)(void *)(image+NOCT_POP),cache,flag);}
static uint8_t noct_build(void *cache,uint64_t id,uint32_t flag) {return noct_build_with((noct_build_fn)(void *)(image+NOCT_BUILD),cache,id,flag);}
static void *noct_octree(void *builder) {return noct_octree_with((noct_octree_fn)(void *)(image+NOCT_OCTREE),builder);}
static void noct_request(void *cache,uint64_t id,uint32_t flag) {noct_request_with((noct_request_fn)(void *)(image+NOCT_REQUEST),cache,id,flag);}
static void noct_invalidate(void *object) {
    nos_note(noct_site_of(__builtin_return_address(0),1),__builtin_return_address(0),object); /* census-zone counts, lockstep_nav_octree_sites.h */
    noct_invalidate_with((noct_invalidate_fn)(void *)(image+NOCT_INVALIDATE),noct_site_of(__builtin_return_address(0),1),object);
}
static void **noct_get(void *cache,void **out,void *object,uint32_t flag,uint32_t ask) {
    return noct_get_with((noct_get_fn)(void *)(image+NOCT_GET),(noct_request_fn)(void *)(image+NOCT_REQUEST),noct_site_of(__builtin_return_address(0),0),
                         cache,out,object,flag,ask);
}
/* Wait until nothing buildable is held or requested; with gate, the builds
 * are opened for the wait and closed after it ('sync'). Main thread. */
static int noct_drain(unsigned char *cache,unsigned timeout_ms,int gate,uint64_t *took) {
    LARGE_INTEGER t0,now,frequency;QueryPerformanceCounter(&t0);QueryPerformanceFrequency(&frequency);
    /* A held ID deferred in an earlier join is retried in this one (its invalidated set may have been swapped since). */
    EnterCriticalSection(&noct_lock);noct_joining=1;noct_deferred=0;if(gate) {noct_closed=0;SetEvent(noct_open);}LeaveCriticalSection(&noct_lock);
    uint64_t passes=0,us=0;int idle=0;
    for(;;) {
        EnterCriticalSection(&noct_lock);
        int64_t pending=noct_pending(cache);
        if(pending<0) {noct_joining=0;LeaveCriticalSection(&noct_lock);return noct_fail("request_sets");}
        if(passes==0) {noct_last_pending=(uint64_t)pending;if((uint64_t)pending>noct_max_pending) noct_max_pending=(uint64_t)pending;}
        idle=!noct_busy && (noct_held?noct_deferred:pending==0);
        if(idle) {
            if(noct_held) ++noct_held_deferred;
            noct_joining=0;if(gate) {noct_closed=1;ResetEvent(noct_open);}
        }
        LeaveCriticalSection(&noct_lock);
        QueryPerformanceCounter(&now);us=noct_us(t0,now,frequency);
        if(idle) break;
        if(us>(uint64_t)timeout_ms*1000) {
            EnterCriticalSection(&noct_lock);noct_joining=0;LeaveCriticalSection(&noct_lock); /* left open */
            return noct_fail("join_timeout");
        }
        Sleep(passes++<64?0:1);
    }
    noct_join_passes+=passes;if(took) *took=us;
    return 1;
}
/* 'deadline' join of tick D: build the due records the thread has not built
 * (main thread, under the cache's build lock +0x30), then show every octree
 * due by D. Nothing to do (no live record due by D) costs one comparison. */
static int noct_deadline_join(unsigned char *cache,noct_build_fn build,uint64_t tick,unsigned timeout_ms) {
    if(!noct_drained) { /* once: everything asked for before the first admitted frame */
        uint64_t us=0;if(!noct_drain(cache,timeout_ms,0,&us)) return 0;
        noct_drain_us=us;noct_drained=1;noct_drain_join=1;
    }
    EnterCriticalSection(&noct_lock);
    int idle=!noct_live || tick<noct_next_due;
    if(idle) noct_visible_tick=tick;
    LeaveCriticalSection(&noct_lock);
    if(idle) return 1;
    static uint64_t keys[NOCT_DUE_MAX],gens[NOCT_DUE_MAX];
    for(;;) {
        unsigned n=0;
        EnterCriticalSection(&noct_lock);
        for(unsigned i=0;i<NOCT_RECORDS && n<NOCT_DUE_MAX;i++)
            if(noct_records[i].state==NOCT_R_REQUESTED && noct_records[i].due<=tick) {keys[n]=noct_records[i].key;gens[n]=noct_records[i].gen;n++;}
        LeaveCriticalSection(&noct_lock);
        for(unsigned k=0;k<n;k++) {
            CRITICAL_SECTION *serial=(CRITICAL_SECTION *)(cache+0x30);
            EnterCriticalSection(serial);
            EnterCriticalSection(&noct_lock);
            noct_record *r=noct_find(keys[k]);int due=r && r->gen==gens[k] && r->state==NOCT_R_REQUESTED;
            if(due) noct_ctx=(noct_context){keys[k],gens[k],noct_serial,noct_serial,1,1,0,NULL};
            LeaveCriticalSection(&noct_lock);
            if(due) {
                LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);
                uint8_t built=build(cache,keys[k]>>1,(uint32_t)(keys[k]&1));
                QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=noct_us(t0,t1,frequency);
                EnterCriticalSection(&noct_lock);
                r=noct_find(keys[k]);
                if(r && r->gen==gens[k]) {
                    if(!built) {r->due=tick+1;++noct_postponed;} /* still in the invalidated sets: the frames decide */
                    else {if(r->state==NOCT_R_REQUESTED) {r->state=NOCT_R_NOOP;r->octree=noct_ctx.existing;}r->us=us;r->inlined=1;}
                }
                ++noct_inline;noct_inline_us+=us;if(us>noct_max_inline_us) noct_max_inline_us=us;
                noct_ctx.valid=0;
                LeaveCriticalSection(&noct_lock);
            }
            LeaveCriticalSection(serial);
        }
        if(n<NOCT_DUE_MAX) break;
    }
    /* Show every octree due by D; the next due tick of what stays. */
    EnterCriticalSection(&noct_lock);
    for(unsigned i=noct_hidden_count;i-->0;) if(noct_hidden[i].due<=tick) noct_hidden_drop(i);
    uint64_t next=NOCT_NEVER;
    for(unsigned i=0;i<NOCT_RECORDS;i++) {
        noct_record *r=&noct_records[i];
        if(!noct_is_live(r)) continue;
        if(r->state!=NOCT_R_REQUESTED && r->due<=tick) {
            if(r->octree) for(unsigned h=noct_hidden_count;h-->0;) if(noct_hidden[h].octree==r->octree) noct_hidden_drop(h);
            r->state=NOCT_R_VISIBLE;r->octree=NULL;++noct_published;--noct_live;
            LONG64 at=InterlockedIncrement64(&noct_rows)-1;
            noct_ring[(uint64_t)at%NOCT_RING]=(noct_row){tick,r->key>>1,r->us,r->due,0,r->content,r->cells,(uint8_t)(r->key&1),r->inlined,r->snapshot};
        } else if(r->due<next) next=r->due;
    }
    noct_next_due=next;noct_visible_tick=tick;
    LeaveCriticalSection(&noct_lock);
    return !InterlockedCompareExchange64(&noct_failures,0,0);
}
/* ---- 'agreed' ---- */
static noct_order_entry *noct_entry(uint64_t index) {return &noct_order[index&(NOCT_ORDER-1)];}
/* Built here, or no longer asked for (the record of that generation is gone). Under the lock. */
static int noct_index_done(uint64_t index) {
    const noct_order_entry *e=noct_entry(index);const noct_record *r=noct_find(e->key);
    if(!r || r->gen!=e->gen) return 1;
    return r->state==NOCT_R_BUILT || r->state==NOCT_R_NOOP || r->state==NOCT_R_VISIBLE;
}
/* First join: the requests still in the native sets (+0x310 flag 0, +0x350 flag 1: count +0x28,
 * first node +0x20; node parent +0, left +8, right +0x10, ID +0x20) and the one the thread holds,
 * under the sets' lock +0x2e0 (taken after the module's lock, as the pops do). */
static int noct_enumerate(unsigned char *cache) {
    CRITICAL_SECTION *sets=(CRITICAL_SECTION *)(cache+0x2e0);int ok=1;
    EnterCriticalSection(sets);
    for(unsigned flag=0;flag<2 && ok;flag++) {
        unsigned char *set=cache+(flag?0x350u:0x310u),*node=NULL;uint64_t count=0;
        if(!created_read(set+0x28,&count,8) || !created_read(set+0x20,&node,8) || count>NOCT_RECORDS) {ok=0;break;}
        for(uint64_t k=0;k<count;k++) {
            uint64_t id=0;unsigned char *next=NULL;
            if(!node || node==set || !created_read(node+0x20,&id,8)) {ok=0;break;}
            noct_note_request(id,flag);noct_snap_note(id,flag); /* the snapshot of an earlier request: this join's state, taken at the next */
            if(!created_read(node+0x10,&next,8)) {ok=0;break;}
            if(next) {unsigned char *left=NULL;while(created_read(next+8,&left,8) && left) next=left;node=next;continue;}
            unsigned char *parent=NULL,*child=node;
            for(unsigned depth=0;;depth++) {
                unsigned char *pl=NULL;
                if(depth>128 || !created_read(child,&parent,8) || !parent || !created_read(parent+8,&pl,8)) {ok=0;break;}
                if(pl==child) break;
                child=parent;
            }
            if(!ok) break;
            node=parent;
        }
    }
    LeaveCriticalSection(sets);
    if(ok && noct_held && !noct_find(noct_held_id*2+noct_held_flag)) {noct_note_request(noct_held_id,noct_held_flag);noct_snap_note(noct_held_id,noct_held_flag);}
    return ok?1:noct_fail("request_sets");
}
typedef struct {uint64_t key,portable,gen;unsigned klass;} noct_fresh_entry;
static int noct_fresh_order(const void *a,const void *b) {
    const noct_fresh_entry *x=a,*y=b;
    if(x->klass!=y->klass) return x->klass<y->klass?-1:1;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    return (x->key&1)<(y->key&1)?-1:(x->key&1)>(y->key&1);
}
static uint64_t noct_fnv(uint64_t hash,uint64_t value) {for(unsigned i=0;i<8;i++) {hash^=(value>>(8*i))&0xffu;hash*=0x100000001b3ull;}return hash;}
/* The records opened since the last join get the next indices; keyless ones show natively. Under the lock. */
static int noct_assign(uint64_t tick) {
    static noct_fresh_entry list[NOCT_FRESH];unsigned n=0;
    for(unsigned i=0;i<noct_fresh_count;i++) {
        noct_record *r=noct_find(noct_fresh[i]);
        if(!r || r->index || !noct_is_live(r)) continue;
        int seen=0;for(unsigned j=0;j<n && !seen;j++) seen=list[j].key==r->key;
        if(seen) continue;
        unsigned klass=WORLD_KEY_NONE;uint64_t portable=world_identity_key(r->key>>1,&klass);
        if(klass==WORLD_KEY_NONE) { /* no portable identity: as native */
            r->state=NOCT_R_VISIBLE;--noct_live;++noct_local_records;
            for(unsigned h=noct_hidden_count;h-->0;) if(noct_hidden[h].key==r->key) noct_hidden_drop(h);
            continue;
        }
        list[n++]=(noct_fresh_entry){r->key,portable,r->gen,klass};
    }
    noct_fresh_count=0;
    qsort(list,n,sizeof(*list),noct_fresh_order);
    for(unsigned i=0;i<n;i++) {
        if(noct_next_index-noct_shown_index>=NOCT_ORDER) return noct_fail("order_full");
        noct_record *r=noct_find(list[i].key);
        r->index=noct_next_index+1;
        *noct_entry(noct_next_index)=(noct_order_entry){list[i].key,list[i].gen,tick,0,0};
        noct_keys_hash=noct_fnv(noct_fnv(noct_fnv(noct_keys_hash,list[i].klass),list[i].portable),list[i].key&1);
        ++noct_next_index;
    }
    return 1;
}
/* This client's report: 20 bytes as 40 hex characters, or 0 when every index is shown. MoveWorker. */
static int noct_report_text(char *out,size_t cap) {
    if(noct_mode!=NOCT_AGREED || !noct_installed || !noct_lock_ready || cap<2*NOCT_REPORT_BYTES+1) return 0;
    unsigned char bytes[NOCT_REPORT_BYTES];memset(bytes,0,sizeof(bytes));int any;
    EnterCriticalSection(&noct_lock);
    any=noct_shown_index<noct_next_index;
    if(any) {
        if(noct_local_w<noct_shown_index) noct_local_w=noct_shown_index;
        while(noct_local_w<noct_next_index && noct_index_done(noct_local_w)) ++noct_local_w;
        uint32_t w=(uint32_t)noct_local_w;memcpy(bytes,&w,4);
        for(unsigned b=0;b<NOCT_REPORT_BITS && noct_local_w+1+b<noct_next_index;b++)
            if(noct_index_done(noct_local_w+1+b)) bytes[4+b/8]|=(unsigned char)(1u<<(b%8));
    }
    LeaveCriticalSection(&noct_lock);
    if(!any) return 0;
    for(unsigned i=0;i<NOCT_REPORT_BYTES;i++) snprintf(out+2*i,3,"%02x",bytes[i]);
    return 2*NOCT_REPORT_BYTES;
}
static int noct_slot_covers(const noct_slot_report *rep,uint64_t index) {
    if(!rep->seen) return 0;
    if(index<rep->w) return 1;
    if(index==rep->w) return 0;
    uint64_t b=index-rep->w-1;
    return b<NOCT_REPORT_BITS && (rep->bits[b/8]>>(b%8)&1u);
}
static int noct_hex_byte(const char *h,unsigned char *v) {
    unsigned x=0;for(unsigned i=0;i<2;i++) {char c=h[i];unsigned d=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16;if(d>15) return 0;x=x<<4|d;}
    *v=(unsigned char)x;return 1;
}
/* Admission of tick A, every client with the identical blobs: payloads are the slots' octree
 * sections, comma separated ('' for none). An index every slot covers is agreed and shows at the
 * join of A. The receipt joins the declared state. MoveWorker. */
static int noct_agree_text(uint64_t tick,unsigned clients,const char *payloads,char *out,size_t cap) {
    if(noct_mode!=NOCT_AGREED || !noct_installed || !noct_lock_ready || !payloads || clients<1 || clients>GATE_CLIENTS_MAX) return 0;
    noct_slot_report parsed[GATE_CLIENTS_MAX];int has[GATE_CLIENTS_MAX];memset(has,0,sizeof(has));
    const char *at=payloads;
    for(unsigned k=0;k<clients;k++) {
        size_t n=strcspn(at,",");
        if(n==2*NOCT_REPORT_BYTES) {
            unsigned char bytes[NOCT_REPORT_BYTES];int ok=1;
            for(unsigned i=0;i<NOCT_REPORT_BYTES && ok;i++) ok=noct_hex_byte(at+2*i,&bytes[i]);
            if(!ok) return 0;
            uint32_t w;memcpy(&w,bytes,4);parsed[k].w=w;memcpy(parsed[k].bits,bytes+4,sizeof(parsed[k].bits));parsed[k].seen=1;has[k]=1;
        } else if(n) return 0;
        at+=n;
        if(k+1<clients) {if(*at!=',') return 0;++at;}
    }
    if(*at) return 0;
    EnterCriticalSection(&noct_lock);
    for(unsigned k=0;k<clients;k++) if(has[k] && (!noct_slot_reports[k].seen || parsed[k].w>=noct_slot_reports[k].w)) {
        noct_slot_reports[k]=parsed[k];++noct_reports;
    }
    for(uint64_t i=noct_shown_index;i<noct_next_index;i++) {
        noct_order_entry *e=noct_entry(i);if(e->agreed) continue;
        int all=1;for(unsigned k=0;k<clients && all;k++) all=noct_slot_covers(&noct_slot_reports[k],i);
        if(all) {e->agreed=1;++noct_agreed_total;}
    }
    int n=snprintf(out,cap,"OCT_1|%llu|%llu|%llu|%llu|%016llx",(unsigned long long)tick,(unsigned long long)noct_shown_index,
                   (unsigned long long)noct_agreed_total,(unsigned long long)noct_next_index,(unsigned long long)noct_keys_hash);
    LeaveCriticalSection(&noct_lock);
    return n>0 && (size_t)n<cap?n:0;
}
/* 'agreed' join of tick D: indices for the frame's new records, then every agreed index shows. */
static int noct_agreed_join(unsigned char *cache,uint64_t tick) {
    EnterCriticalSection(&noct_lock);
    int ok=1;
    if(!noct_enumerated) {ok=noct_enumerate(cache);noct_enumerated=1;}
    if(ok) ok=noct_assign(tick);
    for(uint64_t i=noct_shown_index;ok && i<noct_next_index;i++) {
        noct_order_entry *e=noct_entry(i);
        if(!e->agreed || e->shown) continue;
        e->shown=1;
        noct_record *r=noct_find(e->key);
        if(r && r->gen==e->gen && (r->state==NOCT_R_BUILT || r->state==NOCT_R_NOOP)) {
            for(unsigned h=noct_hidden_count;h-->0;) if(noct_hidden[h].key==r->key || (r->octree && noct_hidden[h].octree==r->octree)) noct_hidden_drop(h);
            r->state=NOCT_R_VISIBLE;r->octree=NULL;++noct_published;--noct_live;
            uint64_t wait=tick>=e->assigned?tick-e->assigned:0;if(wait>noct_max_wait) noct_max_wait=wait;
            LONG64 at=InterlockedIncrement64(&noct_rows)-1;
            noct_ring[(uint64_t)at%NOCT_RING]=(noct_row){tick,r->key>>1,r->us,e->assigned,i,r->content,r->cells,(uint8_t)(r->key&1),0,r->snapshot};
        }
    }
    while(noct_shown_index<noct_next_index && noct_entry(noct_shown_index)->shown) ++noct_shown_index;
    noct_visible_tick=tick;
    LeaveCriticalSection(&noct_lock);
    return ok && !InterlockedCompareExchange64(&noct_failures,0,0);
}
/* Gate tick, main thread (MoveWorker joined, before FrameTick). 0 fails closed. */
static int noct_join_with(unsigned char *cache,noct_build_fn build,uint64_t tick,unsigned timeout_ms) {
    if(!noct_installed || !noct_lock_ready || (noct_mode<NOCT_SYNC && !noct_snapshot)) return 1;
    if(InterlockedCompareExchange64(&noct_failures,0,0)) return 0;
    LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);
    noct_snap_capture(tick); /* first: this frame's requests are collected before any build of them */
    if(noct_mode<NOCT_SYNC) {
        if(InterlockedCompareExchange64(&noct_failures,0,0)) return 0;
        QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=noct_us(t0,t1,frequency);
        ++noct_joins;noct_join_us+=us;if(us>noct_max_join_us) noct_max_join_us=us;
        return 1;
    }
    if(noct_mode>=NOCT_DEADLINE) {EnterCriticalSection(&noct_lock);noct_look_flush(tick);LeaveCriticalSection(&noct_lock);}
    int ok=noct_mode==NOCT_SYNC?noct_drain(cache,timeout_ms,1,NULL):noct_mode==NOCT_DEADLINE?noct_deadline_join(cache,build,tick,timeout_ms):
           noct_agreed_join(cache,tick);
    QueryPerformanceCounter(&t1);QueryPerformanceFrequency(&frequency);uint64_t us=noct_us(t0,t1,frequency);
    if(!ok) return 0;
    /* max_join_us is the hitch of the frames: the one-time first drain is drain_us. */
    ++noct_joins;noct_join_us+=us;if(!noct_drain_join && us>noct_max_join_us) noct_max_join_us=us;
    noct_drain_join=0;
    return 1;
}
static int noct_join(uint64_t tick,unsigned timeout_ms) {
    unsigned char *cache=NULL;
    if(!noct_installed || (noct_mode<NOCT_SYNC && !noct_snapshot)) return 1;
    if(!read_local(image+NOCT_CACHE,&cache,8) || !cache) return noct_fail("cache");
    return noct_join_with(cache,(noct_build_fn)(void *)(image+NOCT_BUILD),tick,timeout_ms);
}
/* Builds run freely and nothing stays hidden (stop, failure, restore). */
static void noct_open_builds(void) {
    if(!noct_lock_ready) return;
    EnterCriticalSection(&noct_lock);
    noct_closed=0;noct_joining=0;if(noct_open) SetEvent(noct_open);noct_hidden_count=0;
    if(noct_snap_event) SetEvent(noct_snap_event);
    LeaveCriticalSection(&noct_lock);
}
static int noct_site_used(unsigned i) {
    if(i<NOCT_BASE_SITES || noct_mode>=NOCT_DEADLINE) return 1;
    return noct_snapshot && (noct_sites[i].kind==NOCT_K_REQUEST || noct_sites[i].kind==NOCT_K_INVALIDATE);
}
static unsigned char *noct_stub(unsigned i) {return noct_stubs+0x20u*i;}
static int32_t noct_rel_to(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+noct_sites[i].rva+5));}
/* 0 original, 1 ours (its stub), -1 anything else. */
static int noct_site_state(unsigned i) {
    unsigned char now[5];int32_t d;if(!read_local(image+noct_sites[i].rva,now,5) || now[0]!=noct_sites[i].op) return -1;
    memcpy(&d,now+1,4);
    if(d==noct_rel_to(i,image+noct_sites[i].target)) return 0;
    return noct_stubs && d==noct_rel_to(i,noct_stub(i))?1:-1;
}
static int noct_pin_ok(const noct_pin *pin) {
    static unsigned char copy[0xc00];
    if(pin->size>sizeof(copy) || !read_local(image+pin->rva,copy,pin->size)) return 0;
    for(unsigned i=0;i<NOCT_SITES;i++) {
        unsigned at=noct_sites[i].rva;if(at<pin->rva || at+5>pin->rva+pin->size) continue;
        if(noct_site_state(i)<0) return 0;
        int32_t d=noct_rel_to(i,image+noct_sites[i].target);memcpy(copy+(at-pin->rva)+1,&d,4);
    }
    return hash_ok(copy,pin->size,pin->sha);
}
static int noct_pins_ok(void) {
    for(unsigned p=0;p<sizeof(noct_pins)/sizeof(*noct_pins);p++) if(!noct_pin_ok(&noct_pins[p])) return 0;
    if(noct_snapshot)
        for(unsigned p=0;p<sizeof(noct_snapshot_pins)/sizeof(*noct_snapshot_pins);p++) if(!noct_pin_ok(&noct_snapshot_pins[p])) return 0;
    if(noct_mode>=NOCT_DEADLINE)
        for(unsigned p=0;p<sizeof(noct_deadline_pins)/sizeof(*noct_deadline_pins);p++) if(!noct_pin_ok(&noct_deadline_pins[p])) return 0;
    return 1;
}
static unsigned char *noct_alloc_near(unsigned size) {
    uintptr_t at=(uintptr_t)(image+noct_sites[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<NOCT_SITES;i++) {
                intptr_t lo=(intptr_t)p-(intptr_t)(image+noct_sites[i].rva+5),hi=(intptr_t)(p+size)-(intptr_t)(image+noct_sites[i].rva+5);
                fits=lo>=INT32_MIN && hi<=INT32_MAX;
            }
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void *noct_target(unsigned i) {
    switch(noct_sites[i].kind) {
        case NOCT_K_POP: return (void*)noct_pop;
        case NOCT_K_BUILD: return (void*)noct_build;
        case NOCT_K_OCTREE: return (void*)noct_octree;
        case NOCT_K_REQUEST: return (void*)noct_request;
        case NOCT_K_INVALIDATE: return (void*)noct_invalidate;
        case NOCT_K_EXISTING: return (void*)noct_existing;
        default: return (void*)noct_get;
    }
}
static int noct_write_stubs(void) {
    DWORD old,ignored;if(!VirtualProtect(noct_stubs,NOCT_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<NOCT_SITES;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=noct_target(i);
        memcpy(noct_stub(i),jump,6);memcpy(noct_stub(i)+6,&target,8);
    }
    int ok=VirtualProtect(noct_stubs,NOCT_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),noct_stubs,NOCT_STUB_SIZE);return ok;
}
static int noct_stubs_owned(void) {
    for(unsigned i=0;i<NOCT_SITES;i++) {
        void *t=NULL;if(!read_local(noct_stub(i)+6,&t,8) || t!=noct_target(i) || noct_stub(i)[0]!=0xff || noct_stub(i)[1]!=0x25) return 0;
    }
    return 1;
}
static int noct_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(noct_sites[i].rva+1,e,v,4);
}
static int noct_unpatch(void) {
    for(unsigned i=0;i<NOCT_SITES;i++) if(noct_done&(1u<<i)) {
        int state=noct_site_state(i);
        if(state==1 && noct_cas(i,noct_rel_to(i,noct_stub(i)),noct_rel_to(i,image+noct_sites[i].target))) noct_done&=~(1u<<i);
        else if(state==0) noct_done&=~(1u<<i);
    }
    return !noct_done;
}
static int noct_slot1_swap(void *expected,void *value) {
    unsigned char *p=image+NOCT_SLOT1_ENTRY;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static void noct_snap_clear(void) {
    for(unsigned i=0;i<NOCT_SNAPS;i++) free(noct_snaps[i].parts);
    memset(noct_snaps,0,sizeof(noct_snaps));noct_snap_count=0;noct_snap_fresh_count=0;
}
static void noct_reset_counters(void) {
    noct_closed=noct_busy=noct_held=noct_deferred=noct_joining=noct_drained=0;
    noct_rows=noct_failures=0;noct_pops=noct_builds=noct_deferrals=noct_join_builds=noct_free_builds=noct_build_us=noct_max_build_us=noct_waited=0;
    noct_joins=noct_join_passes=noct_join_us=noct_max_join_us=noct_last_pending=noct_max_pending=noct_held_deferred=0;
    noct_requested=noct_published=noct_hides=noct_rerequests=noct_invalidations=noct_stale=noct_unrecorded=noct_inline=0;
    noct_inline_us=noct_max_inline_us=noct_postponed=noct_max_records=noct_max_hidden=noct_drain_us=0;
    memset(noct_records,0,sizeof(noct_records));noct_record_count=noct_live=0;noct_hidden_count=0;noct_next_due=NOCT_NEVER;
    noct_gen=noct_serial=noct_inval_count=noct_visible_tick=0;noct_restored=0;
    memset(noct_order,0,sizeof(noct_order));memset(noct_slot_reports,0,sizeof(noct_slot_reports));noct_fresh_count=0;
    noct_next_index=noct_shown_index=noct_local_w=noct_agreed_total=noct_local_records=noct_reports=noct_max_wait=noct_held_id=0;
    noct_keys_hash=0xcbf29ce484222325ull;noct_enumerated=0;noct_held_flag=0;
    noct_look_frame_count=0;noct_looks=0;noct_look_overflow=0;
    noct_snap_clear();noct_snap_captured=noct_snap_gone=noct_snap_used=noct_snap_live=noct_snap_waits=noct_snap_max_parts=noct_snap_us=0;
    noct_snap_max_us=noct_snap_dropped=0;noct_builder_view=-1;noct_main_view=-1;
}
/* Before the first admitted frame, on the MoveWorker. */
static int noct_arm(int mode,unsigned delay,int snapshot) {
    if(noct_installed) return stats.active && noct_mode==mode && noct_snapshot==!!snapshot;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || mode<NOCT_OBSERVE || mode>NOCT_AGREED) return 0;
    if(mode==NOCT_DEADLINE && (!delay || delay>NOCT_DELAY_MAX)) {noct_stage="delay";return 0;}
    if(mode==NOCT_AGREED && wire_version!=3) {noct_stage="wire";return 0;} /* reports ride the protocol 3 input wire */
    int prior=noct_mode,prior_snapshot=noct_snapshot;noct_mode=mode;noct_snapshot=!!snapshot;
    for(unsigned i=0;i<NOCT_SITES;i++) if(noct_site_used(i) && noct_site_state(i)!=0) {noct_mode=prior;noct_snapshot=prior_snapshot;noct_stage="site";return 0;}
    if(noct_snapshot && *(void**)(image+NOCT_SLOT1_ENTRY)!=(void*)(image+NOCT_SLOT1)) {noct_mode=prior;noct_snapshot=prior_snapshot;noct_stage="slot1";return 0;}
    if(!noct_pins_ok()) {noct_mode=prior;noct_snapshot=prior_snapshot;noct_stage="pins";return 0;}
    noct_mode=prior;noct_snapshot=prior_snapshot;
    if(snapshot && !noct_snap_scratch) noct_snap_scratch=VirtualAlloc(NULL,(size_t)NOCT_SNAP_PARTS*NOCT_SNAP_ENTRY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(snapshot && !noct_snap_event) noct_snap_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(snapshot && (!noct_snap_scratch || !noct_snap_event)) {noct_stage="snapshot_memory";return 0;}
    unsigned char *cache=NULL;if(!read_local(image+NOCT_CACHE,&cache,8) || !cache || noct_pending(cache)<0) {noct_stage="cache";return 0;}
    if(!noct_stubs) noct_stubs=noct_alloc_near(NOCT_STUB_SIZE);
    if(!noct_stubs) {noct_stage="memory";return 0;}
    if(!noct_write_stubs()) {noct_stage="stubs";return 0;}
    if(!noct_open) noct_open=CreateEventW(NULL,TRUE,TRUE,NULL);
    if(!noct_open) {noct_stage="event";return 0;}
    if(!noct_lock_ready) {InitializeCriticalSection(&noct_lock);noct_lock_ready=1;}
    EnterCriticalSection(&noct_lock);
    noct_reset_counters();noct_mode=mode;noct_delay=mode==NOCT_DEADLINE?delay:NOCT_DELAY_DEFAULT;SetEvent(noct_open);
    noct_snapshot=!!snapshot;noct_slot1_original=(noct_slot1_fn)(void *)(image+NOCT_SLOT1);
    noct_lookup_original=(noct_lookup_fn)(void *)(image+NOCT_LOOKUP);noct_append_original=(noct_append_fn)(void *)(image+NOCT_APPEND);
    LeaveCriticalSection(&noct_lock);
    /* The octree hook and the build first: a pop or a request through our stub always meets them. */
    static const unsigned order[NOCT_SITES]={16,3,2,0,1,4,5,6,7,8,9,10,11,12,13,14,15};
    for(unsigned k=0;k<NOCT_SITES;k++) {
        unsigned i=order[k];if(!noct_site_used(i)) continue;
        if(!noct_cas(i,noct_rel_to(i,image+noct_sites[i].target),noct_rel_to(i,noct_stub(i)))) {
            noct_unpatch();noct_mode=NOCT_OFF;noct_snapshot=0;noct_stage="cas";return 0;
        }
        noct_done|=1u<<i;
    }
    /* Slot 1 last: a request through our stub has always marked its object before a build can look for it. */
    if(snapshot) {
        if(!noct_slot1_swap(image+NOCT_SLOT1,(void*)noct_slot1)) {noct_unpatch();noct_mode=NOCT_OFF;noct_snapshot=0;noct_stage="slot1_swap";return 0;}
        noct_slot1_swapped=1;
    }
    noct_installed=1;noct_stage="armed";return 1;
}
static int noct_owned(void) {
    if(!noct_installed) return 1;
    if(noct_slot1_swapped && *(void**)(image+NOCT_SLOT1_ENTRY)!=(void*)noct_slot1) return 0;
    for(unsigned i=0;i<NOCT_SITES;i++) if((noct_done&(1u<<i)) && noct_site_state(i)!=1) return 0;
    return noct_stubs_owned();
}
/* Main-thread tick check; any failure stops the gate with reason 137. */
static int noct_check(void) {
    if(InterlockedCompareExchange64(&noct_failures,0,0)) {noct_open_builds();return 0;}
    const char *why=!noct_owned()?"site_changed":!noct_pins_ok()?"block_changed":NULL;
    if(why) {noct_open_builds();return noct_fail(why);}
    return 1;
}
static void noct_restore(void) {
    if(!noct_installed) return;
    noct_open_builds();
    if(noct_slot1_swapped && noct_slot1_swap((void*)noct_slot1,image+NOCT_SLOT1)) noct_slot1_swapped=0;
    if(noct_unpatch() && !noct_slot1_swapped) {noct_installed=0;noct_restored=1;}
}
static const char *noct_mode_name(void) {
    return noct_mode==NOCT_AGREED?"agreed":noct_mode==NOCT_DEADLINE?"deadline":noct_mode==NOCT_SYNC?"sync":noct_mode==NOCT_OBSERVE?"observe":"off";
}
/* On the MoveWorker: the object's portable key is resolved here. */
static int noct_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&noct_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)NOCT_RING && index<(uint64_t)(count-(LONG64)NOCT_RING)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    noct_row r;
    if(noct_lock_ready) EnterCriticalSection(&noct_lock);
    r=noct_ring[index%NOCT_RING];
    if(noct_lock_ready) LeaveCriticalSection(&noct_lock);
    unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(r.id,&klass);
    n=snprintf(out,cap,"index=%llu tick=%llu key=%016llx klass=%u flag=%u joined=%u us=%llu",(unsigned long long)index,(unsigned long long)r.tick,
               (unsigned long long)key,klass,r.flag,r.joined,(unsigned long long)r.us);
    if(n>0 && (size_t)n<cap && noct_snapshot) {
        int m=snprintf(out+n,cap-(size_t)n," snapshot=%u",r.snapshot);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    }
    if(n>0 && (size_t)n<cap && noct_mode>=NOCT_DEADLINE) { /* content of the octree shown (0: none built here, e.g. a no-op build) */
        int m=snprintf(out+n,cap-(size_t)n," content=%016llx cells=%llu",(unsigned long long)r.content,(unsigned long long)r.cells);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    }
    if(n>0 && (size_t)n<cap && noct_mode==NOCT_DEADLINE) {
        int m=snprintf(out+n,cap-(size_t)n," due=%llu",(unsigned long long)r.due);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    } else if(n>0 && (size_t)n<cap && noct_mode==NOCT_AGREED) { /* due holds the assignment tick */
        int m=snprintf(out+n,cap-(size_t)n," index=%llu assigned=%llu",(unsigned long long)r.index,(unsigned long long)r.due);
        n=m>0 && (size_t)m<cap-(size_t)n?n+m:-1;
    }
    return n>0 && (size_t)n<cap?n:0;
}
/* On the MoveWorker: the object's portable key is resolved here. */
static int noct_look_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&noct_looks,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)NOCT_LOOK_RING && index<(uint64_t)(count-(LONG64)NOCT_LOOK_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    noct_look l;
    if(noct_lock_ready) EnterCriticalSection(&noct_lock);
    l=noct_look_ring[index%NOCT_LOOK_RING];
    if(noct_lock_ready) LeaveCriticalSection(&noct_lock);
    unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(l.id,&klass);
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s key=%016llx klass=%u flag=%u ask=%u result=%s count=%u",(unsigned long long)index,
               (unsigned long long)l.tick,noct_look_sites[l.site<10?l.site:9],(unsigned long long)key,klass,l.flag,l.ask,
               noct_look_results[l.result<4?l.result:0],l.count);
    return n>0 && (size_t)n<cap?n:0;
}
static int noct_status(char *out,size_t cap) {
    if(noct_lock_ready) EnterCriticalSection(&noct_lock);
    int n=snprintf(out,cap,"mode=%s installed=%d frame=%llu delay=%u pops=%llu builds=%llu join_builds=%llu free_builds=%llu deferrals=%llu waited=%llu "
        "build_us=%llu max_build_us=%llu joins=%llu join_passes=%llu join_us=%llu max_join_us=%llu last_pending=%llu max_pending=%llu "
        "held_deferred=%llu held=%d closed=%d requested=%llu published=%llu records=%u live=%u max_records=%llu hidden=%u max_hidden=%llu hides=%llu "
        "rerequests=%llu invalidations=%llu stale=%llu unrecorded=%llu inline=%llu inline_us=%llu max_inline_us=%llu postponed=%llu "
        "drain_us=%llu assigned=%llu shown=%llu agreed=%llu waiting=%llu local=%llu reports=%llu max_wait=%llu "
        "looks=%lld look_overflow=%llu snapshot=%d snaps=%u snap_captured=%llu snap_gone=%llu snap_used=%llu snap_live=%llu snap_waits=%llu "
        "snap_dropped=%llu snap_parts=%llu snap_us=%llu max_snap_us=%llu builder_view=%ld main_view=%ld "
        "rows=%lld restored=%d stage=%s failures=%lld",noct_mode_name(),noct_installed,
        (unsigned long long)stats.issued,noct_mode==NOCT_DEADLINE?noct_delay:0u,(unsigned long long)noct_pops,(unsigned long long)noct_builds,
        (unsigned long long)noct_join_builds,(unsigned long long)noct_free_builds,(unsigned long long)noct_deferrals,(unsigned long long)noct_waited,
        (unsigned long long)noct_build_us,(unsigned long long)noct_max_build_us,(unsigned long long)noct_joins,(unsigned long long)noct_join_passes,
        (unsigned long long)noct_join_us,(unsigned long long)noct_max_join_us,(unsigned long long)noct_last_pending,(unsigned long long)noct_max_pending,
        (unsigned long long)noct_held_deferred,noct_held,noct_closed,(unsigned long long)noct_requested,(unsigned long long)noct_published,
        noct_record_count,noct_live,(unsigned long long)noct_max_records,noct_hidden_count,(unsigned long long)noct_max_hidden,(unsigned long long)noct_hides,
        (unsigned long long)noct_rerequests,(unsigned long long)noct_invalidations,(unsigned long long)noct_stale,(unsigned long long)noct_unrecorded,
        (unsigned long long)noct_inline,(unsigned long long)noct_inline_us,(unsigned long long)noct_max_inline_us,(unsigned long long)noct_postponed,
        (unsigned long long)noct_drain_us,(unsigned long long)noct_next_index,(unsigned long long)noct_shown_index,
        (unsigned long long)noct_agreed_total,(unsigned long long)(noct_next_index-noct_shown_index),(unsigned long long)noct_local_records,
        (unsigned long long)noct_reports,(unsigned long long)noct_max_wait,(long long)noct_looks,(unsigned long long)noct_look_overflow,
        noct_snapshot,noct_snap_count,(unsigned long long)noct_snap_captured,(unsigned long long)noct_snap_gone,(unsigned long long)noct_snap_used,
        (unsigned long long)noct_snap_live,(unsigned long long)noct_snap_waits,(unsigned long long)noct_snap_dropped,
        (unsigned long long)noct_snap_max_parts,(unsigned long long)noct_snap_us,(unsigned long long)noct_snap_max_us,
        (long)InterlockedCompareExchange(&noct_builder_view,0,0),(long)noct_main_view,
        (long long)noct_rows,noct_restored,noct_stage,(long long)noct_failures);
    if(noct_lock_ready) LeaveCriticalSection(&noct_lock);
    return n>0 && (size_t)n<cap?n:0;
}
