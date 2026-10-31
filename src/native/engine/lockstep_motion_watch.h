/* Census diagnostic, DEFAULT OFF, read-only: per-tick pathing state of watched
 * ships (native_gate_motion_watch = {saved IDs}).
 *
 * cam-B13a/b (camera held 70 km off on client 2, no spawn scenario): the only
 * difference through 540 is ship 0xa214c (zone 0x732, low attention), whose
 * motion bytes part at the 240 capture in the last mantissa bits only, after a
 * turn that starts between 180 and 240; 14 more low-attention ships follow by
 * 600. Its NavigationController upkeep (vfunc33, where NAV_CADENCE_ROWs come
 * from) never runs, so the nav watch shows nothing. This watch reads, each
 * admitted frame in slot 6 (after the pathing join of the frame):
 * attention (+0x1ec), the NavigationController at object+0x200 (vtable,
 * back pointer +0x28 checked), both pathing state slots at [ctrl+0x30] +
 * i*0x1a0 (pose and linear/angular velocity, 0x60 bytes each) and the current
 * index 0x38f4ee4, the controller's cached transform (+0x60, 0x40 bytes) and
 * the agent's flight control model (agent = ctrl+0x40, back pointer +0xef0
 * checked; model agent+0xf10: vtable, target transform +0x20..+0x60, target
 * reference ID +0x60, path point count/flags +0x68..+0x70) and the agent's
 * path (FlightPoints of 0x90 bytes in the vector at agent+0x6a8/+0x6b0: count
 * and the first point's reference, connection and transform, +0x10..+0x60).
 * The two pathing state slots print as cur= (slot[index], the state the frame
 * produced) and prev=: the double-buffer index parity differs between clients
 * (cam-B16, from tick 1) while slot[index] agrees. cam-B16 also showed the
 * remaining roots (78855 at 574, 6decf 587, 6dd87 590) changing their Linear
 * model acceleration on client 1 only (78855: client 2 at 598) with the same
 * target, path and prior state: the rows add what the Linear step 0xeb27c0
 * reads besides them, raw: the model's plan +0x70..+0xe0 (start position and
 * time, last step time, factors, axes; model_ext=), the agent's speed fields
 * +0x364, +0x4d0, +0x4d2, +0x4d8, +0x518..+0x520 and flag bytes +0xf33, +0xf5c,
 * +0xf5d, +0xf74, +0xf75, +0xf77, +0xf7a (agent_ext=), and the formation
 * (agent+0xf20; formation=1) leader **(formation+0x48), whose speed the step
 * matches (leader= portable reference, leader_ext= the same agent fields).
 * cam-B17: no formations; 78855 enters travel mode (agent +0x4d2 = 1, +0x364 =
 * the travel speed +0x4d8) at 574 on client 1 and 598 on client 2, 896ff
 * leaves it at 576 on client 1 only. Agent_UpdatePathing 0xe8f250 decides
 * boost and travel mode: after the last decision's cooldown (+0xf38 boost,
 * +0xf40 travel; -1 at creation) it asks the model (Linear slot 25: the
 * heading error +0x98 against a config threshold) and posts StartBoost /
 * StopBoost / StartTravelMode / StopTravelMode events to the ship; the ship
 * (StartTravelMode 0x6d8bf0) either starts at once or posts a charge-started
 * and a travel-started event at now + charge time (from its forward speed
 * against +0x3b0). agent_ext= appends +0xf38, +0xf40 (doubles), +0x4d1
 * (boost) and +0x4d3; ship_ext= the ship's +0x3b0 and +0x3c8. The event
 * dispatches are traced with the order rows (MOTION_EVENT, below).
 * cam-B18: no traced event near 574/598. The agent's +0x364/+0x4d0.. fields
 * are not agent fields but the third copy (agent+0x340) of the flight
 * parameter slots at agent + i*0x1a0 (i = g_PathingStateBufferIndex for the
 * read slot, g_PathingStateBufferIndex2 0x2f4b6f4 for the written one): +0x24
 * max speed, +0x191 boost, +0x192 travel (the engine's +0x340), +0x193 a
 * pending ship event 0x1fa, +0x194, +0x198 travel speed. 0xea1e80 refreshes a
 * slot from the ship and its engine; it is called (e8) from
 * OnUpdateEngineParameters, NavigationController slots 33/35/36, 0xea4e90 and
 * 0xd8ddb0 (three of them traced, see the refresh rows). fp= prints those fields for the read slot, the written slot and
 * the third copy; with the order rows each refresh of a watched ship is a
 * MOTION_REFRESH row (call site, chain, slot, fields before and after). The
 * Linear
 * step also slows a formation leader for its wingmen (agent+0xf18: the
 * formation it leads, wingman agents at +0x48..+0x50): wingmen= their count,
 * wingman0= the first one's ship as a portable reference.
 * Rows are raw bytes without local pointers or runtime IDs: references print
 * as the portable key of the referenced component or of its nearest keyed
 * ancestor, the depth walked and the component's type (cam-B14: the target's
 * reference ID differed by the creation offset of the two processes, a
 * false difference from tick 2). Two clients' rows compare field by field and
 * the first differing tick and field name the input that rounds differently.
 * Objects resolve once per run through the saved -> runtime map and the
 * engine's component lookup 0xced70 (pinned by the pilot module). */
#define MOTION_WATCH_MAX 16u
#define MOTION_WATCH_RING 8192u
#define MOTION_WATCH_NAV_VTABLE 0x2c51158u /* U::NavigationController */
#define MOTION_WATCH_INDEX 0x38f4ee4u      /* g_PathingStateBufferIndex */
#define MOTION_PATH_STRIDE 0x90u           /* sizeof(U::Path::FlightPoint) */
#define MOTION_PATH_MAX 4096u
#define MOTION_REF_DEPTH 8u
#define MOTION_REF_UNRESOLVED 0xffu
typedef struct {uint64_t key;unsigned klass,depth;uint32_t type;} motion_ref_t;
#define MOTION_AGENT_EXT 47u
typedef struct {uint64_t tick,saved;int attention;uint32_t ctrl_vt,model_vt,index,flags,path_count,path_conn,formation;
    unsigned char slot[2][0x60],cache[0x40],target[0x50],path0[0x50],model_ext[0x70],agent_ext[MOTION_AGENT_EXT],leader_ext[MOTION_AGENT_EXT],ship_ext[5];
    uint32_t wingmen;unsigned char fp[48];motion_ref_t target_ref,path_ref,leader,wingman0;} motion_watch_row_t;
#define MOTION_PATHING_INDEX2 0x2f4b6f4u /* g_PathingStateBufferIndex2 */
/* Flight parameter fields of one 0x1a0 slot: +0x24, +0x190..+0x193, +0x194, +0x198. */
static int motion_fp_fields(const unsigned char *slot,unsigned char out[16]) {
    return read_local(slot+0x24,out,4) && read_local(slot+0x190,out+4,4) && read_local(slot+0x194,out+8,4) && read_local(slot+0x198,out+12,4);
}
/* Agent fields the Linear step reads (offset, size), packed in this order. */
static const struct {unsigned short at,size;} motion_agent_fields[]={
    {0x364,4},{0x4d0,1},{0x4d2,1},{0x4d8,4},{0x518,12},{0xf33,1},{0xf5c,1},{0xf5d,1},{0xf74,1},{0xf75,1},{0xf77,1},{0xf7a,1},
    {0xf38,8},{0xf40,8},{0x4d1,1},{0x4d3,1}};
static int motion_watch_agent_ext(const unsigned char *agent,unsigned char out[MOTION_AGENT_EXT]) {
    unsigned at=0;
    for(unsigned i=0;i<sizeof(motion_agent_fields)/sizeof(motion_agent_fields[0]);i++) {
        if(!read_local(agent+motion_agent_fields[i].at,out+at,motion_agent_fields[i].size)) {memset(out,0,MOTION_AGENT_EXT);return 0;}
        at+=motion_agent_fields[i].size;
    }
    return at==MOTION_AGENT_EXT;
}
static uint64_t motion_watch_saved[MOTION_WATCH_MAX],motion_watch_runtime[MOTION_WATCH_MAX];
static void *volatile motion_watch_agent[MOTION_WATCH_MAX]; /* last agent read per watched ship (pointer compare only) */
static unsigned motion_watch_count,motion_watch_armed;
static uint64_t motion_watch_misses;
static motion_watch_row_t *motion_watch_ring;
static volatile LONG64 motion_watch_rows;
static uint32_t motion_watch_rva(const void *p) {
    const unsigned char *q=(const unsigned char*)pslot_canonical_vtable((uintptr_t)p);
    return q>=image && q<image+0x6fa9000?(uint32_t)(q-image):0xffffffffu;
}
typedef void *(*motion_watch_find_fn)(void *,uint64_t,unsigned);
typedef void *(*motion_watch_lookup_fn)(uint64_t);
static void *motion_watch_engine_lookup(uint64_t runtime) {
    void *map=NULL;if(!read_local(image+0x6cf1500,&map,8) || !map) return NULL;
    return ((motion_watch_find_fn)(void *)(image+0xced70))(map,runtime,4);
}
static motion_watch_lookup_fn motion_watch_lookup=motion_watch_engine_lookup;
/* A runtime reference as the portable key of itself or its nearest keyed
 * ancestor (parent +0x70), the depth walked (MOTION_REF_UNRESOLVED: none within
 * MOTION_REF_DEPTH) and its own type; 0 is no reference. */
static motion_ref_t motion_watch_ref(uint64_t runtime) {
    motion_ref_t r={0,WORLD_KEY_NONE,0,0};
    if(!runtime) return r;
    unsigned char *object=motion_watch_lookup(runtime);void *vt=NULL;uint64_t id=runtime;
    r.type=object && read_local(object,&vt,8)?motion_watch_rva(vt):0xffffffffu;
    for(unsigned depth=0;depth<=MOTION_REF_DEPTH;depth++) {
        unsigned klass;uint64_t key=world_identity_key(id,&klass);
        if(key) {r.key=key;r.klass=klass;r.depth=depth;return r;}
        unsigned char *parent=NULL;
        if(!object || !read_local(object+0x70,&parent,8) || !parent || !read_local(parent+8,&id,8) || !id) break;
        object=parent;
    }
    r.depth=MOTION_REF_UNRESOLVED;return r;
}
static void motion_ref_text(char out[48],motion_ref_t r) {
    snprintf(out,48,"%016llx/%u/%u/%x",(unsigned long long)r.key,r.klass,r.depth,r.type);
}
static int motion_watch_add(uint64_t saved) {
    if(!saved || motion_watch_count>=MOTION_WATCH_MAX || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    /* The engine's component lookup, as the pilot module pins it. */
    if(!hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36")) return 0;
    for(unsigned i=0;i<motion_watch_count;i++) if(motion_watch_saved[i]==saved) return 1;
    if(!motion_watch_ring) {
        motion_watch_ring=VirtualAlloc(NULL,sizeof(motion_watch_row_t)*MOTION_WATCH_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!motion_watch_ring) return 0;
    }
    motion_watch_saved[motion_watch_count]=saved;motion_watch_runtime[motion_watch_count]=0;motion_watch_agent[motion_watch_count]=NULL;
    ++motion_watch_count;motion_watch_armed=1;return 1;
}
/* The agent's path: count and the first FlightPoint (flags 16 and 32). */
static void motion_watch_path(const unsigned char *agent,motion_watch_row_t *r) {
    unsigned char *begin=NULL,*end=NULL;
    if(!read_local(agent+0x6a8,&begin,8) || !read_local(agent+0x6b0,&end,8) || end<begin ||
       (size_t)(end-begin)%MOTION_PATH_STRIDE || (size_t)(end-begin)/MOTION_PATH_STRIDE>MOTION_PATH_MAX) return;
    r->path_count=(uint32_t)((size_t)(end-begin)/MOTION_PATH_STRIDE);r->flags|=16;
    if(r->path_count && read_local(begin+0x10,r->path0,0x50)) {
        uint64_t ref,connection;memcpy(&ref,r->path0,8);memcpy(&connection,r->path0+8,8);
        r->path_ref=motion_watch_ref(ref);r->path_conn=connection!=0;memset(r->path0,0,16);r->flags|=32;
    }
}
/* One row from a resolved object; 0 when the layout does not match (counted). */
static int motion_watch_read(const unsigned char *object,uint64_t saved,uint64_t tick,motion_watch_row_t *r,void **agent_out) {
    memset(r,0,sizeof(*r));r->tick=tick;r->saved=saved;r->ctrl_vt=r->model_vt=0xffffffffu;r->target_ref=r->path_ref=r->leader=r->wingman0=motion_watch_ref(0);
    if(agent_out) *agent_out=NULL;
    unsigned char *ctrl=NULL,*back=NULL,*slots=NULL,*agent=NULL,*model=NULL;void *vt=NULL;
    if(!read_local(object+0x1ec,&r->attention,4)) return 0;
    if(read_local(object+0x3b0,r->ship_ext,4) && read_local(object+0x3c8,r->ship_ext+4,1)) r->flags|=512;
    if(!read_local(object+0x200,&ctrl,8) || !ctrl || !read_local(ctrl,&vt,8)) return 1; /* no controller: attention only */
    r->ctrl_vt=motion_watch_rva(vt);
    if(vt!=(void*)(image+MOTION_WATCH_NAV_VTABLE) || !read_local(ctrl+0x28,&back,8) || back!=object) return 1;
    r->flags|=1;
    if(read_local(image+MOTION_WATCH_INDEX,&r->index,4) && read_local(ctrl+0x30,&slots,8) && slots &&
       read_local(slots,r->slot[0],0x60) && read_local(slots+0x1a0,r->slot[1],0x60)) r->flags|=2;
    if(read_local(ctrl+0x60,r->cache,0x40)) r->flags|=4;
    if(!read_local(ctrl+0x40,&agent,8) || !agent || !read_local(agent+0xef0,&back,8) || back!=object) return 1;
    if(agent_out) *agent_out=agent;
    motion_watch_path(agent,r);
    if(motion_watch_agent_ext(agent,r->agent_ext)) r->flags|=64;
    uint32_t index2=0;
    if(read_local(image+MOTION_PATHING_INDEX2,&index2,4) && (r->index&~1u)==0 && (index2&~1u)==0 &&
       motion_fp_fields(agent+r->index*0x1a0u,r->fp) && motion_fp_fields(agent+index2*0x1a0u,r->fp+16) && motion_fp_fields(agent+0x340,r->fp+32))
        r->flags|=1024;
    unsigned char *formation=NULL,*slot=NULL,*leader=NULL,*leader_object=NULL;uint64_t leader_id=0;
    if(read_local(agent+0xf20,&formation,8) && formation) {
        r->formation=1;
        if(read_local(formation+0x48,&slot,8) && slot && read_local(slot,&leader,8) && leader) {
            if(motion_watch_agent_ext(leader,r->leader_ext)) r->flags|=128;
            if(read_local(leader+0xef0,&leader_object,8) && leader_object && read_local(leader_object+8,&leader_id,8)) r->leader=motion_watch_ref(leader_id);
        }
    }
    unsigned char *led=NULL,*begin=NULL,*end=NULL,*wingman=NULL,*wingman_object=NULL;uint64_t wingman_id=0;
    if(read_local(agent+0xf18,&led,8) && led && read_local(led+0x48,&begin,8) && read_local(led+0x50,&end,8) && end>=begin && (size_t)(end-begin)<=0x1000) {
        r->wingmen=(uint32_t)((size_t)(end-begin)/8);
        if(r->wingmen && read_local(begin,&wingman,8) && wingman && read_local(wingman+0xef0,&wingman_object,8) && wingman_object &&
           read_local(wingman_object+8,&wingman_id,8)) r->wingman0=motion_watch_ref(wingman_id);
    }
    if(read_local(agent+0xf10,&model,8) && model && read_local(model,&vt,8)) {
        r->model_vt=motion_watch_rva(vt);
        if(read_local(model+0x20,r->target,0x50)) {
            uint64_t ref;memcpy(&ref,r->target+0x40,8);r->target_ref=motion_watch_ref(ref);memset(r->target+0x40,0,8);r->flags|=8;
        }
        /* Only the model's own bytes (sizes as 0xe87cf0 allocates them): beyond them lies neighbouring heap, which differs per client. */
        const uint32_t own=r->model_vt==0x2c4f7d0u?0u:r->model_vt==0x2c4f6f0u?0x30u:r->model_vt==0x2c4f8d0u?0x50u:0x70u; /* Forced, Highway, MassTraffic */
        if(!own || read_local(model+0x70,r->model_ext,own)) r->flags|=256;
        if(r->model_vt==0x2c4f6f0u) {r->model_ext[0x17]=0;memset(r->model_ext+0x28,0,8);} /* Highway: padding at +0x87 and +0x98..+0x9f, never written */
    }
    return 1;
}
/* Slot 6 of every admitted frame. */
static int motion_watch_frame(uint64_t tick) {
    if(!motion_watch_armed) return 1;
    if(!stats.active || GetCurrentThreadId()!=ui_thread || tick!=stats.issued) return 0;
    void *map=NULL;motion_watch_find_fn find=(motion_watch_find_fn)(void *)(image+0xced70);
    if(!read_local(image+0x6cf1500,&map,8) || !map) {++motion_watch_misses;return 1;}
    for(unsigned i=0;i<motion_watch_count;i++) {
        if(!motion_watch_runtime[i]) motion_watch_runtime[i]=pilot_reverse(motion_watch_saved[i]);
        uint64_t id=0;unsigned char *object=motion_watch_runtime[i]?find(map,motion_watch_runtime[i],4):NULL;
        if(!object || !read_local(object+8,&id,8) || id!=motion_watch_runtime[i]) {++motion_watch_misses;continue;}
        LONG64 index=InterlockedIncrement64(&motion_watch_rows)-1;void *agent=NULL;
        if(!motion_watch_read(object,motion_watch_saved[i],tick,&motion_watch_ring[(uint64_t)index%MOTION_WATCH_RING],&agent)) ++motion_watch_misses;
        motion_watch_agent[i]=agent;
    }
    return 1;
}
static void motion_watch_hex(char *out,const unsigned char *bytes,unsigned n) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<n;i++) {out[2*i]=digits[bytes[i]>>4];out[2*i+1]=digits[bytes[i]&15];}
    out[2*n]=0;
}
static int motion_watch_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&motion_watch_rows,0,0);int n;
    if(!motion_watch_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MOTION_WATCH_RING && index<(uint64_t)(count-(LONG64)MOTION_WATCH_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const motion_watch_row_t *r=&motion_watch_ring[index%MOTION_WATCH_RING];
    char cur[0xc1],prev[0xc1],cache[0x81],target[0xa1],path0[0xa1],model_ext[0xe1],agent_ext[2*MOTION_AGENT_EXT+1],leader_ext[2*MOTION_AGENT_EXT+1];
    char target_ref[48],path_ref[48],leader[48],wingman0[48],ship_ext[11],fp[97];motion_watch_hex(fp,r->fp,48);motion_ref_text(wingman0,r->wingman0);unsigned now=r->index&1u;motion_watch_hex(ship_ext,r->ship_ext,5);
    motion_watch_hex(cur,r->slot[now],0x60);motion_watch_hex(prev,r->slot[now^1u],0x60);motion_watch_hex(cache,r->cache,0x40);motion_watch_hex(target,r->target,0x50);
    motion_watch_hex(path0,r->path0,0x50);motion_watch_hex(model_ext,r->model_ext,0x70);motion_watch_hex(agent_ext,r->agent_ext,MOTION_AGENT_EXT);
    motion_watch_hex(leader_ext,r->leader_ext,MOTION_AGENT_EXT);
    motion_ref_text(target_ref,r->target_ref);motion_ref_text(path_ref,r->path_ref);motion_ref_text(leader,r->leader);
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx attention=%d ctrl=%08x model=%08x flags=%u cur=%s prev=%s cache=%s target=%s "
        "target_ref=%s path=%u path0=%s path0_ref=%s/%u model_ext=%s agent_ext=%s formation=%u leader=%s leader_ext=%s ship_ext=%s wingmen=%u wingman0=%s fp=%s",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->saved,r->attention,r->ctrl_vt,r->model_vt,r->flags,
        cur,prev,cache,target,target_ref,r->path_count,path0,path_ref,r->path_conn,model_ext,agent_ext,r->formation,leader,leader_ext,ship_ext,r->wingmen,wingman0,fp);
    return n>0 && (size_t)n<cap?n:0;
}
/* Order rows, DEFAULT OFF (native_gate_motion_watch_orders, needs the watch):
 * cam-B14 showed a214c's new LinearFlightControlModel target at tick 194
 * differing in the last bits of its position (z 0x4609e998 vs 0x4609e9a0)
 * while every earlier field agrees. Targets of AI move actions reach the agent
 * as AddTargetPointToPathOrders: AI::MoveToAction / MoveGateAction /
 * MoveTargetPointsAction build a FlightPoint (0x6e0810, 0x6e09a0, 0x6e0b20,
 * 0x6e0cb0) and queue it through 0xeadf30(manager, ship runtime ID, point, 0).
 * Those four e8 calls go through jmp stubs to a logger that records, for a
 * watched ship, the point's bytes +0x10..+0x60 (reference, connection,
 * transform; reference mapped, connection pointer as a flag) and the first two
 * image return addresses on the stack (in the creator, then its caller), then
 * queues it unchanged. A point that
 * already differs names the AI-side computation; one that agrees moves the
 * search into the pathing application. Sites written by CAS at arm (on the
 * MoveWorker, before any admitted frame); pins: the four creators and
 * 0xeadf30; restore at stop. Rows written off the MoveWorker publish through
 * a per-row ready word (a row not yet complete reads as absent). */
#define MOTION_ORDER_SITES 4u
#define MOTION_ORDER_QUEUE 0xeadf30u
#define MOTION_ORDER_RING 4096u
static const unsigned motion_order_sites[MOTION_ORDER_SITES]={0x6e0962u,0x6e0ae9u,0x6e0c72u,0x6e0df9u};
typedef struct {volatile LONG64 ready;uint64_t tick,saved;uint32_t site,chain[2],connection;motion_ref_t ref;unsigned char point[0x50];} motion_order_row_t;
static motion_order_row_t *motion_order_ring;
static volatile LONG64 motion_order_rows;
static unsigned char *motion_order_stubs;
static unsigned char motion_order_done[MOTION_ORDER_SITES];
static int motion_order_installed;
typedef void *(*motion_order_fn)(void *,uint64_t,void *,uint64_t);
static int32_t motion_order_rel(unsigned site,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+site+5));}
/* The first `want` image return addresses from `skip` frames up (our own frames are not in the image). */
static void motion_chain(unsigned skip,uint32_t *chain,unsigned want) {
    for(unsigned k=0;k<want;k++) chain[k]=0xffffffffu;
    void *frames[16];USHORT n=RtlCaptureStackBackTrace(skip,16,frames,NULL);unsigned k=0;
    for(USHORT j=0;j<n && k<want;j++) {uint32_t rva=motion_watch_rva(frames[j]);if(rva!=0xffffffffu) chain[k++]=rva;}
}
static void motion_order_note(unsigned site,uint64_t runtime,const unsigned char *point) {
    unsigned klass;uint64_t key=runtime?world_identity_key(runtime,&klass):0;
    unsigned i=0;for(;i<motion_watch_count;i++) if(motion_watch_saved[i]==key) break;
    if(!key || i==motion_watch_count || !motion_order_ring) return;
    LONG64 index=InterlockedIncrement64(&motion_order_rows)-1;motion_order_row_t *r=&motion_order_ring[(uint64_t)index%MOTION_ORDER_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->saved=key;r->site=motion_order_sites[site];r->connection=0;memset(&r->ref,0,sizeof(r->ref));
    motion_chain(0,r->chain,2);
    if(!point || !read_local(point+0x10,r->point,0x50)) {memset(r->point,0,sizeof(r->point));r->site|=0x80000000u;}
    else {
        uint64_t ref,connection;memcpy(&ref,r->point,8);memcpy(&connection,r->point+8,8);
        r->ref=motion_watch_ref(ref);r->connection=connection!=0;memset(r->point,0,16);
    }
    InterlockedExchange64(&r->ready,index+1);
}
#define MOTION_ORDER_STUB(i) static void *motion_order_stub##i(void *m,uint64_t id,void *point,uint64_t a4) { \
    motion_order_note(i,id,point);return ((motion_order_fn)(void *)(image+MOTION_ORDER_QUEUE))(m,id,point,a4);}
MOTION_ORDER_STUB(0) MOTION_ORDER_STUB(1) MOTION_ORDER_STUB(2) MOTION_ORDER_STUB(3)
static void *motion_order_targets(unsigned i) {
    void *t[MOTION_ORDER_SITES]={(void*)motion_order_stub0,(void*)motion_order_stub1,(void*)motion_order_stub2,(void*)motion_order_stub3};
    return t[i];
}
/* 0 original, 1 ours, -1 anything else. */
static int motion_order_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+motion_order_sites[i],now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE)) return 0;
    if(motion_order_stubs && d==motion_order_rel(motion_order_sites[i],motion_order_stubs+0x20u*i)) return 1;
    return -1;
}
static int motion_order_pins(void) {
    static unsigned char copy[0x620];
    if(!read_local(image+0x6e0810,copy,0x620)) return 0;
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) {
        if(motion_order_state(i)<0) return 0;
        int32_t d=motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE);memcpy(copy+(motion_order_sites[i]-0x6e0810u)+1,&d,4);
    }
    return hash_ok(copy,0x620,"15ef878ad087578283c4beabb46ff411756f350beb9864d6097f72f9c6575997") &&
           hash_ok(image+MOTION_ORDER_QUEUE,0x8e,"5b340dfed542b9b2deed1aee352bfe20627adebd7d33ab4a8d1bd45c684d30c7");
}
/* Refresh rows (same key): the e8 calls of 0xea1e80(controller, slot, flag)
 * that no other module pins: 0xd8ddde (0xd8ddb0), 0xea1d9a
 * (OnUpdateEngineParameters, the periodic one) and 0xea94ac (slot 36). Not
 * routed, because other modules hash the code around them every tick or at a
 * capture (cam-B19: with all eight, the nav cadence's tick check of its pin
 * 0xea7866..0xea79ad stopped the gate at boot, REFUSED code=10 after the
 * reload): 0xea791a (slot 33, nav cadence pin), 0xea50ec/0xea50fc (0xea4e90,
 * world hash nav pins), 0xea8f82/0xea8f91 (0xea8cf0, creation trace contract
 * and registry origin pins). */
#define MOTION_REFRESH_SITES 3u
#define MOTION_REFRESH_FN 0xea1e80u
#define MOTION_REFRESH_RING 4096u
#define MOTION_REFRESH_STUBS 0x200u /* offset of the refresh stubs on the stub page */
static const unsigned motion_refresh_sites[MOTION_REFRESH_SITES]={0xd8dddeu,0xea1d9au,0xea94acu};
typedef struct {volatile LONG64 ready;uint64_t tick,saved;uint32_t site,flag,slot,ui,chain[4];unsigned char before[16],after[16];} motion_refresh_row_t;
static motion_refresh_row_t *motion_refresh_ring;
static volatile LONG64 motion_refresh_rows;
static unsigned char motion_refresh_done[MOTION_REFRESH_SITES];
typedef uint64_t (*motion_refresh_fn)(void *,void *,uint64_t);
/* The watched index of a controller's ship, or -1 (no reads beyond the controller's ship and its ID). */
static int motion_refresh_watched(const unsigned char *nav) {
    unsigned char *ship=NULL;uint64_t id=0;
    if(!nav || !read_local(nav+0x28,&ship,8) || !ship || !read_local(ship+8,&id,8) || !id) return -1;
    for(unsigned i=0;i<motion_watch_count;i++) if(motion_watch_runtime[i]==id) return (int)i;
    return -1;
}
static uint64_t motion_refresh_call(unsigned site,void *nav,void *slot,uint64_t flag) {
    motion_refresh_fn original=(motion_refresh_fn)(void *)(image+MOTION_REFRESH_FN);
    int w=motion_refresh_ring?motion_refresh_watched(nav):-1;
    if(w<0) return original(nav,slot,flag);
    unsigned char before[16]={0};motion_fp_fields(slot,before);
    uint64_t result=original(nav,slot,flag);
    LONG64 index=InterlockedIncrement64(&motion_refresh_rows)-1;motion_refresh_row_t *r=&motion_refresh_ring[(uint64_t)index%MOTION_REFRESH_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->saved=motion_watch_saved[w];r->site=motion_refresh_sites[site];r->flag=(uint32_t)(flag&0xff);
    r->ui=GetCurrentThreadId()==ui_thread;memcpy(r->before,before,16);memset(r->after,0,16);motion_fp_fields(slot,r->after);
    /* Which slot: 0 the read slot (index), 1 the written slot (index2), 2 the third copy, 3 other. */
    unsigned char *agent=NULL;uint32_t i1=0,i2=0;r->slot=3;
    if(read_local((unsigned char*)nav+0x40,&agent,8) && agent && read_local(image+MOTION_WATCH_INDEX,&i1,4) && read_local(image+MOTION_PATHING_INDEX2,&i2,4))
        r->slot=(unsigned char*)slot==agent+(size_t)i1*0x1a0u?0u:(unsigned char*)slot==agent+(size_t)i2*0x1a0u?1u:(unsigned char*)slot==agent+0x340?2u:3u;
    motion_chain(0,r->chain,4);
    InterlockedExchange64(&r->ready,index+1);
    return result;
}
#define MOTION_REFRESH_STUB(i) static uint64_t motion_refresh_stub##i(void *nav,void *slot,uint64_t flag) {return motion_refresh_call(i,nav,slot,flag);}
MOTION_REFRESH_STUB(0) MOTION_REFRESH_STUB(1) MOTION_REFRESH_STUB(2)
static void *motion_refresh_target(unsigned i) {
    void *t[MOTION_REFRESH_SITES]={(void*)motion_refresh_stub0,(void*)motion_refresh_stub1,(void*)motion_refresh_stub2};
    return t[i];
}
static unsigned char *motion_refresh_stub(unsigned i);
/* 0 original, 1 ours, -1 anything else. */
static int motion_refresh_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+motion_refresh_sites[i],now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==motion_order_rel(motion_refresh_sites[i],image+MOTION_REFRESH_FN)) return 0;
    if(motion_order_stubs && d==motion_order_rel(motion_refresh_sites[i],motion_refresh_stub(i))) return 1;
    return -1;
}
static int motion_refresh_pins(void) {
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) if(motion_refresh_state(i)<0) return 0;
    return hash_ok(image+MOTION_REFRESH_FN,0x180,"f6f2e15839d1ef5ae54f0b72970bf8b07d656dc984a4dcbaf8529edcf66eb3ca");
}
static int motion_refresh_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&motion_refresh_rows,0,0);int n;
    if(!motion_refresh_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MOTION_REFRESH_RING && index<(uint64_t)(count-(LONG64)MOTION_REFRESH_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const motion_refresh_row_t *r=&motion_refresh_ring[index%MOTION_REFRESH_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0; /* still being written */
    static const char *const slots[4]={"read","written","third","other"};char before[33],after[33];
    motion_watch_hex(before,r->before,16);motion_watch_hex(after,r->after,16);
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx site=%08x flag=%u slot=%s ui=%u chain=%08x,%08x,%08x,%08x before=%s after=%s",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->saved,r->site,r->flag,slots[r->slot&3u],r->ui,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],before,after);
    return n>0 && (size_t)n<cap?n:0;
}
static unsigned char *motion_order_page(void) {
    uintptr_t at=(uintptr_t)(image+motion_order_sites[0])&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<MOTION_ORDER_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+motion_order_sites[i]+5);fits=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;
            }
            for(unsigned i=0;fits && i<MOTION_REFRESH_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+motion_refresh_sites[i]+5);fits=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;
            }
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int motion_order_cas(unsigned site,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(site+1,e,v,4);
}
static unsigned char *motion_refresh_stub(unsigned i) {return motion_order_stubs+MOTION_REFRESH_STUBS+0x20u*i;}
/* Target rows (same key): who sets a watched ship's flight model target.
 * Spline, Linear, Forced and MassTraffic keep the base
 * FlightControlModel::SetTarget 0xeaf620(model, transform, reference ID, flag)
 * in vtable slot 3; the four entries swap (CAS) to a forwarder that, when the
 * model's agent (+0x10) is one the watch read last frame (pointer compare, no
 * reads for other ships), records the transform and reference as passed, the
 * flag and the first four image return addresses on the stack (SetTarget's
 * caller first; cam-B15's rows skipped that one), then calls SetTarget
 * unchanged. Pinned: SetTarget's bytes; restore at stop. */
#define MOTION_TARGET_SET 0xeaf620u
#define MOTION_TARGET_ENTRIES 4u
#define MOTION_TARGET_RING 4096u
static const unsigned motion_target_entries[MOTION_TARGET_ENTRIES]={0x2c4f480u+0x18u,0x2c4f610u+0x18u,0x2c4f7d0u+0x18u,0x2c4f8d0u+0x18u};
typedef struct {volatile LONG64 ready;uint64_t tick,saved;uint32_t model_vt,flag,chain[4];motion_ref_t ref;unsigned char transform[0x40];} motion_target_row_t;
static motion_target_row_t *motion_target_ring;
static volatile LONG64 motion_target_rows;
static unsigned motion_target_swapped;
typedef void (*motion_target_fn)(void *,const void *,uint64_t,void *);
static void motion_target_note(void *model,const void *transform,uint64_t ref,void *flag) {
    if(!motion_target_ring || !model) return;
    void *agent=*(void *const*)((const unsigned char*)model+0x10);unsigned i=0;
    if(!agent) return;
    for(;i<motion_watch_count;i++) if(motion_watch_agent[i]==agent) break;
    if(i==motion_watch_count) return;
    LONG64 index=InterlockedIncrement64(&motion_target_rows)-1;motion_target_row_t *r=&motion_target_ring[(uint64_t)index%MOTION_TARGET_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->saved=motion_watch_saved[i];r->model_vt=motion_watch_rva(*(void *const*)model);r->flag=flag!=NULL;
    motion_chain(0,r->chain,4);
    if(!transform || !read_local(transform,r->transform,0x40)) {memset(r->transform,0,sizeof(r->transform));r->flag|=0x80000000u;}
    r->ref=motion_watch_ref(ref);
    InterlockedExchange64(&r->ready,index+1);
}
static void motion_target_set(void *model,const void *transform,uint64_t ref,void *flag) {
    motion_target_note(model,transform,ref,flag);
    ((motion_target_fn)(void *)(image+MOTION_TARGET_SET))(model,transform,ref,flag);
}
/* Event rows (same key): the boost and travel mode events of watched ships.
 * Each event class's Dispatch (vtable slot 2: event, target, sender, time)
 * swaps (CAS) to a forwarder that, when the target is a watched ship (its
 * runtime ID compared with the watch's), records the event, its time, the
 * thread's game time (TLS+0x300), the event's +0x18 word, whether it runs on
 * the MoveWorker and four image return addresses, then dispatches unchanged.
 * Pinned: each Dispatch body. */
#define MOTION_EVENT_KINDS 8u
#define MOTION_EVENT_RING 4096u
static const struct {unsigned vtable,dispatch,size;const char *name,*sha;} motion_event_kinds[MOTION_EVENT_KINDS]={
    {0x2bafc38u,0x973f20u,0x13u,"start_travel","ec6d50b804743fd42684341bd026323727faa71fffced56085d40cde581879d1"},
    {0x2bab068u,0x9a4aa0u,0x1bu,"stop_travel","63f8e387378562ddcd9439d42e22eeca12e4e7388ea5bfcec3a2451c337c1d83"},
    {0x2baaf50u,0x9a4ae0u,0x1bu,"charge_started","a2f3aca8d0e34b5e5f7a66bccf461e6cbf243b37ca0a1d8a437c43c506ff47e5"},
    {0x2b48e40u,0x69e650u,0x13u,"travel_started","91c946c11b2afb372fae2b54b54e7b8aac4e0885c9a39bdc7389dcf2a2f70365"},
    {0x2b48e08u,0x69e680u,0x13u,"travel_stopped","5e292f105aac6f476f52fe055643914915a8cc5e81535bbe4039027ee80ac611"},
    {0x2b48dd0u,0x69e620u,0x13u,"charge_aborted","52df18567a5d75d8fea7d00120ef353ade30582371807e2be31ae6e8c9f7fc3c"},
    {0x2bafd88u,0x973ee0u,0x13u,"start_boost","93809eb2738428d7911d738227a6b9cac3d8e7f67b233d53ff1ee6866a8e7ab9"},
    {0x2ae9930u,0x4b6a60u,0x13u,"stop_boost","2968378aa525c874e9554221bdba597e5b786ee647f8d5789c43729869ac4ba8"}};
typedef struct {volatile LONG64 ready;uint64_t tick,saved,time,now,data;uint32_t kind,ui,chain[4];} motion_event_row_t;
static motion_event_row_t *motion_event_ring;
static volatile LONG64 motion_event_rows;
static unsigned motion_event_swapped;
typedef uint64_t (*motion_event_fn)(void *,void *,void *,double);
static void motion_event_note(unsigned kind,const unsigned char *event,const unsigned char *target,double time) {
    if(!motion_event_ring || !target) return;
    uint64_t id=*(const uint64_t*)(target+8);unsigned i=0;
    for(;i<motion_watch_count;i++) if(motion_watch_runtime[i] && motion_watch_runtime[i]==id) break;
    if(i==motion_watch_count) return;
    LONG64 index=InterlockedIncrement64(&motion_event_rows)-1;motion_event_row_t *r=&motion_event_ring[(uint64_t)index%MOTION_EVENT_RING];
    InterlockedExchange64(&r->ready,0);
    r->tick=stats.issued;r->saved=motion_watch_saved[i];r->kind=kind;memcpy(&r->time,&time,8);r->now=0;r->data=0;
    r->ui=GetCurrentThreadId()==ui_thread;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(tls && tls[0]) read_local((unsigned char*)tls[0]+0x300,&r->now,8);
    /* +0x18 exists only in the charge-started (charge end time) and stop-travel (a byte) events. */
    if(event && kind==2) read_local(event+0x18,&r->data,8);
    else if(event && kind==1) read_local(event+0x18,&r->data,1);
    motion_chain(0,r->chain,4);
    InterlockedExchange64(&r->ready,index+1);
}
#define MOTION_EVENT_FORWARD(i) static uint64_t motion_event_forward##i(void *event,void *target,void *sender,double time) { \
    motion_event_note(i,event,target,time);return ((motion_event_fn)(void *)(image+motion_event_kinds[i].dispatch))(event,target,sender,time);}
MOTION_EVENT_FORWARD(0) MOTION_EVENT_FORWARD(1) MOTION_EVENT_FORWARD(2) MOTION_EVENT_FORWARD(3)
MOTION_EVENT_FORWARD(4) MOTION_EVENT_FORWARD(5) MOTION_EVENT_FORWARD(6) MOTION_EVENT_FORWARD(7)
static void *motion_event_forwarder(unsigned i) {
    void *f[MOTION_EVENT_KINDS]={(void*)motion_event_forward0,(void*)motion_event_forward1,(void*)motion_event_forward2,(void*)motion_event_forward3,
                                 (void*)motion_event_forward4,(void*)motion_event_forward5,(void*)motion_event_forward6,(void*)motion_event_forward7};
    return f[i];
}
static int motion_event_swap(unsigned i,void *expected,void *value) {
    unsigned char *p=image+motion_event_kinds[i].vtable+0x10;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int motion_event_pins(void) {
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++) if(!hash_ok(image+motion_event_kinds[i].dispatch,motion_event_kinds[i].size,motion_event_kinds[i].sha)) return 0;
    return 1;
}
static int motion_event_owned(void) {
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++)
        if((motion_event_swapped&(1u<<i)) && *(void**)(image+motion_event_kinds[i].vtable+0x10)!=motion_event_forwarder(i)) return 0;
    return 1;
}
static int motion_event_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&motion_event_rows,0,0);int n;
    if(!motion_event_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MOTION_EVENT_RING && index<(uint64_t)(count-(LONG64)MOTION_EVENT_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const motion_event_row_t *r=&motion_event_ring[index%MOTION_EVENT_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0; /* still being written */
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx event=%s time=%016llx now=%016llx data=%016llx ui=%u chain=%08x,%08x,%08x,%08x",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->saved,r->kind<MOTION_EVENT_KINDS?motion_event_kinds[r->kind].name:"?",
        (unsigned long long)r->time,(unsigned long long)r->now,(unsigned long long)r->data,r->ui,r->chain[0],r->chain[1],r->chain[2],r->chain[3]);
    return n>0 && (size_t)n<cap?n:0;
}
static int motion_target_swap(unsigned i,void *expected,void *value) {
    unsigned char *p=image+motion_target_entries[i];DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int motion_target_pins(void) {
    return hash_ok(image+MOTION_TARGET_SET,0x31,"0d2c642e7809f6d293ce4b5bcaee47a8443e49fb4ab7c644df2b8f0a0f475e73");
}
static int motion_target_owned(void) {
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++)
        if((motion_target_swapped&(1u<<i)) && *(void**)(image+motion_target_entries[i])!=(void*)motion_target_set) return 0;
    return 1;
}
static void motion_order_restore(void);
/* After the watch list, before the first admitted frame, on the MoveWorker. */
static int motion_order_arm(void) {
    if(motion_order_installed) return 1;
    if(!motion_watch_armed || stats.issued || GetCurrentThreadId()!=ui_thread || !motion_order_pins() || !motion_target_pins() || !motion_event_pins() ||
       !motion_refresh_pins()) return 0;
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) if(motion_refresh_state(i)!=0) return 0;
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++) if(*(void**)(image+motion_event_kinds[i].vtable+0x10)!=(void*)(image+motion_event_kinds[i].dispatch)) return 0;
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) if(motion_order_state(i)!=0) return 0;
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++) if(*(void**)(image+motion_target_entries[i])!=(void*)(image+MOTION_TARGET_SET)) return 0;
    if(!motion_order_ring) motion_order_ring=VirtualAlloc(NULL,sizeof(motion_order_row_t)*MOTION_ORDER_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!motion_target_ring) motion_target_ring=VirtualAlloc(NULL,sizeof(motion_target_row_t)*MOTION_TARGET_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!motion_event_ring) motion_event_ring=VirtualAlloc(NULL,sizeof(motion_event_row_t)*MOTION_EVENT_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!motion_refresh_ring) motion_refresh_ring=VirtualAlloc(NULL,sizeof(motion_refresh_row_t)*MOTION_REFRESH_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!motion_order_stubs) motion_order_stubs=motion_order_page();
    if(!motion_order_ring || !motion_target_ring || !motion_event_ring || !motion_refresh_ring || !motion_order_stubs) return 0;
    DWORD old,ignored;if(!VirtualProtect(motion_order_stubs,0x1000,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *t=motion_order_targets(i);
        memcpy(motion_order_stubs+0x20u*i,jump,6);memcpy(motion_order_stubs+0x20u*i+6,&t,8);
    }
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *t=motion_refresh_target(i);
        memcpy(motion_refresh_stub(i),jump,6);memcpy(motion_refresh_stub(i)+6,&t,8);
    }
    if(!VirtualProtect(motion_order_stubs,0x1000,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),motion_order_stubs,0x1000);
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) {
        if(!motion_order_cas(motion_order_sites[i],motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE),
                             motion_order_rel(motion_order_sites[i],motion_order_stubs+0x20u*i))) {
            for(unsigned j=0;j<i;j++) if(motion_order_done[j] && motion_order_cas(motion_order_sites[j],motion_order_rel(motion_order_sites[j],motion_order_stubs+0x20u*j),
                motion_order_rel(motion_order_sites[j],image+MOTION_ORDER_QUEUE))) motion_order_done[j]=0;
            return 0;
        }
        motion_order_done[i]=1;
    }
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++) {
        if(!motion_target_swap(i,image+MOTION_TARGET_SET,(void*)motion_target_set)) {
            for(unsigned j=0;j<i;j++) if(motion_target_swap(j,(void*)motion_target_set,image+MOTION_TARGET_SET)) motion_target_swapped&=~(1u<<j);
            for(unsigned j=0;j<MOTION_ORDER_SITES;j++) if(motion_order_done[j] && motion_order_cas(motion_order_sites[j],motion_order_rel(motion_order_sites[j],motion_order_stubs+0x20u*j),
                motion_order_rel(motion_order_sites[j],image+MOTION_ORDER_QUEUE))) motion_order_done[j]=0;
            return 0;
        }
        motion_target_swapped|=1u<<i;
    }
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++) {
        if(!motion_event_swap(i,image+motion_event_kinds[i].dispatch,motion_event_forwarder(i))) {
            for(unsigned j=0;j<i;j++) if(motion_event_swap(j,motion_event_forwarder(j),image+motion_event_kinds[j].dispatch)) motion_event_swapped&=~(1u<<j);
            for(unsigned j=0;j<MOTION_TARGET_ENTRIES;j++) if((motion_target_swapped&(1u<<j)) && motion_target_swap(j,(void*)motion_target_set,image+MOTION_TARGET_SET)) motion_target_swapped&=~(1u<<j);
            for(unsigned j=0;j<MOTION_ORDER_SITES;j++) if(motion_order_done[j] && motion_order_cas(motion_order_sites[j],motion_order_rel(motion_order_sites[j],motion_order_stubs+0x20u*j),
                motion_order_rel(motion_order_sites[j],image+MOTION_ORDER_QUEUE))) motion_order_done[j]=0;
            return 0;
        }
        motion_event_swapped|=1u<<i;
    }
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) {
        if(!motion_order_cas(motion_refresh_sites[i],motion_order_rel(motion_refresh_sites[i],image+MOTION_REFRESH_FN),
                             motion_order_rel(motion_refresh_sites[i],motion_refresh_stub(i)))) {motion_order_restore();return 0;}
        motion_refresh_done[i]=1;
    }
    motion_order_installed=1;return 1;
}
static int motion_order_owned(void) {
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) if(motion_order_done[i] && motion_order_state(i)!=1) return 0;
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) if(motion_refresh_done[i] && motion_refresh_state(i)!=1) return 0;
    return motion_target_owned() && motion_event_owned();
}
static void motion_order_restore(void) {
    for(unsigned i=0;i<MOTION_ORDER_SITES;i++) if(motion_order_done[i] && motion_order_state(i)==1 &&
        motion_order_cas(motion_order_sites[i],motion_order_rel(motion_order_sites[i],motion_order_stubs+0x20u*i),
                         motion_order_rel(motion_order_sites[i],image+MOTION_ORDER_QUEUE))) motion_order_done[i]=0;
    for(unsigned i=0;i<MOTION_TARGET_ENTRIES;i++)
        if((motion_target_swapped&(1u<<i)) && motion_target_swap(i,(void*)motion_target_set,image+MOTION_TARGET_SET)) motion_target_swapped&=~(1u<<i);
    for(unsigned i=0;i<MOTION_EVENT_KINDS;i++)
        if((motion_event_swapped&(1u<<i)) && motion_event_swap(i,motion_event_forwarder(i),image+motion_event_kinds[i].dispatch)) motion_event_swapped&=~(1u<<i);
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) if(motion_refresh_done[i] && motion_refresh_state(i)==1 &&
        motion_order_cas(motion_refresh_sites[i],motion_order_rel(motion_refresh_sites[i],motion_refresh_stub(i)),
                         motion_order_rel(motion_refresh_sites[i],image+MOTION_REFRESH_FN))) motion_refresh_done[i]=0;
    int any=motion_target_swapped!=0 || motion_event_swapped!=0;for(unsigned i=0;i<MOTION_ORDER_SITES;i++) any|=motion_order_done[i];
    for(unsigned i=0;i<MOTION_REFRESH_SITES;i++) any|=motion_refresh_done[i];
    if(!any) motion_order_installed=0;
}
static int motion_order_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&motion_order_rows,0,0);int n;
    if(!motion_order_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MOTION_ORDER_RING && index<(uint64_t)(count-(LONG64)MOTION_ORDER_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const motion_order_row_t *r=&motion_order_ring[index%MOTION_ORDER_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0; /* still being written */
    char point[0xa1],ref[48];motion_watch_hex(point,r->point,0x50);motion_ref_text(ref,r->ref);
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx site=%08x chain=%08x,%08x ref=%s/%u point=%s",(unsigned long long)index,
        (unsigned long long)r->tick,(unsigned long long)r->saved,r->site,r->chain[0],r->chain[1],ref,r->connection,point);
    return n>0 && (size_t)n<cap?n:0;
}
static int motion_target_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&motion_target_rows,0,0);int n;
    if(!motion_target_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MOTION_TARGET_RING && index<(uint64_t)(count-(LONG64)MOTION_TARGET_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const motion_target_row_t *r=&motion_target_ring[index%MOTION_TARGET_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0; /* still being written */
    char transform[0x81],ref[48];motion_watch_hex(transform,r->transform,0x40);motion_ref_text(ref,r->ref);
    n=snprintf(out,cap,"index=%llu tick=%llu saved=%016llx model=%08x flag=%x chain=%08x,%08x,%08x,%08x ref=%s transform=%s",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->saved,r->model_vt,r->flag,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],ref,transform);
    return n>0 && (size_t)n<cap?n:0;
}
static int motion_watch_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"watched=%u rows=%lld misses=%llu orders=%d order_rows=%lld targets=%x target_rows=%lld events=%x event_rows=%lld refresh_rows=%lld",
        motion_watch_count,(long long)motion_watch_rows,(unsigned long long)motion_watch_misses,motion_order_installed,(long long)motion_order_rows,
        motion_target_swapped,(long long)motion_target_rows,motion_event_swapped,(long long)motion_event_rows,(long long)motion_refresh_rows);
    return n>0 && (size_t)n<capacity?n:0;
}
