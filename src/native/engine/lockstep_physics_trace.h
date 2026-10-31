/* Read-only census diagnostic, DEFAULT OFF: native_gate_physics_trace = N
 * (rows for ticks 1..N; needs native_gate_world_identities, created bodies
 * are keyed with native_gate_portable_created).
 *
 * big-BS6: in 6 of 7 runs the ships native_gate_big_spawn creates at tick 15
 * part in motion by the tick-60 digest while every RNG, event and per-tick
 * row of ticks 1..60 agrees. Their motion goes through Jolt every frame:
 * XPhys::Scene::Step 0xba600 sets each body's velocity from its controller
 * (pre-sync loop), calls JPH::PhysicsSystem::Update 0x13d3460 at 0xba8f0 and
 * writes the body's velocity and pose back into its controller (0xbfb00).
 *
 * Threads. XPhys::XJobSystem (vtable 0x2a2aba0) never hands a ready job to a
 * worker: QueueJob 0xc5a80 forwards a job to JobThreadManager::QueueJob only
 * when its dependency count (+0x54) is non-zero, which it never is when Jolt
 * queues a ready job (CreateJob 0xc5960 queues at count 0; a forwarded job
 * already taken is a no-op for the worker's 0xb4100, CAS 0 -> executing).
 * Barrier::Wait 0xc5dc0 runs the first ready job of its queue on the waiting
 * thread, in insertion order. So every physics job, and every contact
 * callback (XContactListener 0x2a2ab48 slots 2/3 -> 0xb7230/0xb7250 ->
 * RecordContact 0xb75a0), runs on the MoveWorker inside Scene::Step in one
 * order; RecordContact's unlocked map sums and its GetTransformRelativeTo at
 * TLS+0x300 see the MoveWorker's game time. (The fixture contact observer
 * never saw another thread either.)
 *
 * What can still differ between two processes is the BodyIDs: with
 * mDeterministicSimulation (system+0x2ac, the constructor default that
 * Scene::Init 0xc6890 keeps) each island's contact constraints are sorted by
 * a hash of (BodyID, SubShapeID) pairs, and a pair of equal motion types is
 * oriented by BodyID (ProcessBodyPair), so the same bodies under other IDs
 * are solved in another order. IDs come from the body manager's free list in
 * creation order (lockstep_body_order.h).
 *
 * Record. The call at 0xba8f0 goes through a stub to ptrace_update, which
 * hashes every body of X4's step list (system+0x680/+0x690, filled by
 * GetBodies 0xb4540 this step) before and after the original Update: per
 * body with a portable key (saved or created) its BodyID and layer (ids),
 * active-list index (active), flags, compound child order (shape), mass
 * properties (mass), motion type, pose and velocities (pre, post), each an
 * order-independent sum; created bodies also on their own (cids, cshape,
 * cmass, cpre, cpost). The manager's slot count, body count and free-list
 * head (manager) say whether the next IDs agree. Listener slots 2/3 go
 * through ptrace_added/ptrace_persisted, which hash each callback with a
 * portable side (identities in Jolt's order, sub-shape IDs, manifold,
 * settings after the original) into a set and an ordered digest, and count
 * callbacks off the stepping thread (foreign, 0 expected) or outside a step
 * (stray). One PHYSICS_STEP row per Update call; PHYSICS_BODY rows (created
 * bodies) and PHYSICS_CONTACT rows (callbacks with a created side) for ticks
 * <= N, resolved to created IDs when printed (completed ticks only). Nothing
 * is written to the engine.
 * PHYSICS_CHILD rows (native_gate_physics_trace_children = M, ticks 1..M,
 * diagnostic): every child of each keyed body's top-level compound as the step
 * reads it: index, class, type/sub-type, the part it stands for (the child
 * shape's +0x28 ID: the component 0xc2c00 poses the child from, a sub-type
 * 0x10 child), the SubShape's position and rotation words (+8, 24 bytes) and
 * user data (+0x20). st3-P15S: the step digests part on shape at tick 2 step 1
 * with identical shape refresh schedules; these rows name the child, its part
 * and the pose that differ (st3-P15C: the player scout's two guns, aimed by
 * the local crosshair). native_gate_physics_trace_children_from = A starts the
 * child rows at tick A instead of 1.
 * Saved body rows (native_gate_physics_trace_saved = true, with
 * native_gate_physics_trace = N): PHYSICS_BODY rows for saved bodies too (not
 * only created ones) for ticks 1..N, so a body whose shape, mass or state parts
 * is named even while another body (the player's ship) parts the step
 * digests. Pins: the call block, GetBodies, the body lock
 * read 0xb6950, the two listener wrappers, QueueJob, Barrier::Wait and
 * Scene::Init's settings block. Stop reason 133: a site no longer ours, a pin
 * changed, a read fault or a step list over capacity. */
#define PTRACE_CALL 0xba8f0u
#define PTRACE_UPDATE 0x13d3460u
#define PTRACE_ADDED 0xb7230u
#define PTRACE_PERSISTED 0xb7250u
#define PTRACE_SLOTS 2u
#define PTRACE_STOP_REASON 133u
#define PTRACE_BODIES 65536u
#define PTRACE_STEP_RING 8192u
#define PTRACE_BODY_RING 65536u
#define PTRACE_CONTACT_RING 65536u
#define PTRACE_ROW_TICKS_MAX 1000000u
#define PTRACE_CHILD_RING 131072u
#define PTRACE_CHILD_TICKS_MAX 3600u
#define PTRACE_WORDS 13u
#define PTRACE_POINTS 64u
#define PTRACE_STUB_SIZE 0x1000u
static const unsigned ptrace_slots[PTRACE_SLOTS]={0x2a2ab58u,0x2a2ab60u}; /* XContactListener slots 2, 3 */
static const unsigned ptrace_targets[PTRACE_SLOTS]={PTRACE_ADDED,PTRACE_PERSISTED};
static const unsigned ptrace_scenes[3]={0x38f0748u,0x38f0740u,0x38f0738u};
static const struct {unsigned rva,size;const char *sha;} ptrace_pins[]={
    {0xba864u,0x96u,"bac954d4eb804e0307c58e0eab8d461fd8d64b16cd4f32f496d461fdd0af7c5b"},  /* Scene::Step: Update call */
    {0xb4540u,0x11du,"62b4061061c083446c817b3e986821a9a1d5f432688913dc6d58b56b772515bb"}, /* GetBodies */
    {0xb6950u,0x92u,"e44c0a5c4e371ac3fd0d3a11aef0c2ce91278b085732a7c1ddef9c15888198e5"},  /* body lock read */
    {0xb7230u,0x1du,"7a528abe5d453d86f4755a82afec551e75b7fab3e4791e870cb00ed486610d75"},  /* OnContactAdded */
    {0xb7250u,0x1du,"7058941a117d0b6ef7e0cb35bca789d317f1759696a2af94577cd068cb7db720"},  /* OnContactPersisted */
    {0xc5a80u,0x53u,"1419f15b57536e53bcc51c5ab71ac1c515311192b06d8766f4bfb65dcede3882"},  /* XJobSystem::QueueJob */
    {0xc5dc0u,0x1c2u,"614a6031d8ce6f0f9f5fe2dadb158c3c1b5b1fca803a0a82b8d2d8308fb06894"}, /* XBarrier::Wait */
    {0xc6c00u,0x123u,"f3b64e33f727a76d426cb380845b1454d53296388c1ce5662fa29bc8f5ed81e2"}}; /* Scene::Init settings */
enum {PTRACE_ROW_STEP,PTRACE_ROW_BODY,PTRACE_ROW_CONTACT,PTRACE_ROW_CHILD};
typedef uint32_t (*ptrace_update_fn)(void *,float,int,void *,void *);
typedef void (*ptrace_listener_fn)(void *,void *,void *,void *,void *);
typedef struct {uint64_t runtime,key;uint8_t klass,which;} ptrace_id;
typedef struct {
    unsigned char *body;ptrace_id id;uint64_t shape,mass;uint32_t body_id,active,children,words[PTRACE_WORDS];
    uint16_t layer;uint8_t motion,flags;
} ptrace_body;
typedef struct {
    uint64_t tick,manager,ids,active,flags,shape,mass,pre,post,cids,cshape,cmass,cpre,cpost,cset,corder,ccset,size;
    uint32_t step,result,bodies,keyed,created,local,missing,contacts,ckeyed,ccreated,clocal,cforeign,flips,sensors,num;
    uint8_t scene,det;
} ptrace_step_row;
typedef struct {
    uint64_t tick,shape,mass;ptrace_id id;uint32_t step,body_id,active,children,pre[PTRACE_WORDS],post[PTRACE_WORDS];
    uint16_t layer;uint8_t scene,motion,flags;
} ptrace_body_row;
typedef struct {
    uint64_t tick,points;ptrace_id id[2];uint32_t step,ord,body_id[2],sub[2],base[3],normal[3],depth,count,settings[7];
    uint8_t motion[2],added,flip,sensor;
} ptrace_contact_row;
typedef struct {
    uint64_t tick,part;ptrace_id id;uint32_t step,index,count,klass,pose[6],user;uint8_t scene,type,subtype,readable;
} ptrace_child_row;
static ptrace_child_row *ptrace_child_ring;
static volatile LONG64 ptrace_child_rows;
static unsigned ptrace_child_ticks,ptrace_child_from=1;
static int ptrace_saved_rows; /* PHYSICS_BODY rows for saved bodies too */
static int ptrace_installed,ptrace_restored;
static unsigned ptrace_row_ticks,ptrace_swapped;
static int ptrace_call_done;
static unsigned char *ptrace_stub;
static ptrace_body *ptrace_list;
static uint32_t *ptrace_ids;
static ptrace_step_row *ptrace_step_ring;
static ptrace_body_row *ptrace_body_ring;
static ptrace_contact_row *ptrace_contact_ring;
static volatile LONG64 ptrace_step_rows,ptrace_body_rows,ptrace_contact_rows;
static volatile LONG64 ptrace_failures,ptrace_foreign,ptrace_foreign_steps;
static uint64_t ptrace_steps,ptrace_native,ptrace_contacts,ptrace_stray,ptrace_tick=UINT64_MAX;
static unsigned ptrace_tick_steps;
static const char *ptrace_stage="idle";
static struct {int in_step;unsigned count;LONG64 foreign;ptrace_step_row row;} ptrace_cur;
static int ptrace_fail(const char *why) {ptrace_stage=why;InterlockedIncrement64(&ptrace_failures);return 0;}
static uint64_t ptrace_fold(uint64_t h,const uint64_t *words,unsigned n) {
    for(unsigned i=0;i<n;i++) h=lockstep_mix(h^words[i]^(uint64_t)i*0x9e3779b97f4a7c15ULL);
    return h;
}
static uint64_t ptrace_pack(const uint32_t *words,unsigned n,uint64_t *out) {
    for(unsigned i=0;i<n;i+=2) out[i/2]=(uint64_t)words[i]|(i+1<n?(uint64_t)words[i+1]<<32:0);
    return (n+1)/2;
}
/* The component owning a Jolt body: record = body+0x50 (user data), component
 * = record+0; which = its main (+0x260) or second (+0x268) record, 2 other, 3
 * unreadable. The key is portable (saved or created) or 0. */
static void ptrace_identity(const unsigned char *body,ptrace_id *out) {
    unsigned char *record=NULL,*component=NULL,*records[2]={NULL,NULL};unsigned klass=WORLD_KEY_NONE;
    out->runtime=out->key=0;out->klass=WORLD_KEY_NONE;out->which=3;
    if(!created_read(body+0x50,&record,8) || !record || !created_read(record,&component,8) || !component ||
       !created_read(component+8,&out->runtime,8)) return;
    out->which=created_read(component+0x260,records,16)?(record==records[0]?0:record==records[1]?1:2):3;
    out->key=out->runtime?world_identity_key(out->runtime,&klass):0;out->klass=(uint8_t)klass;
}
/* Body+0x00 position, +0x10 rotation, +0x48 motion properties (+0x00 linear,
 * +0x10 angular velocity, +0x70 active index), +0x70 BodyID, +0x74 object
 * layer, +0x78 motion type, +0x79 flags. */
static int ptrace_state(const unsigned char *body,ptrace_body *b) {
    unsigned char head[0x80],motion[0x80],*mp=NULL;
    if(!created_read(body,head,sizeof(head))) return 0;
    memcpy(&b->body_id,head+0x70,4);memcpy(&b->layer,head+0x74,2);b->motion=head[0x78];b->flags=head[0x79];
    memcpy(b->words,head,12);memcpy(b->words+3,head+0x10,16);memcpy(&mp,head+0x48,8);
    b->active=UINT32_MAX;memset(b->words+7,0,24);
    if(mp) {
        if(!created_read(mp,motion,sizeof(motion))) return 0;
        memcpy(b->words+7,motion,12);memcpy(b->words+10,motion+0x10,12);memcpy(&b->active,motion+0x70,4);
        /* Mass properties: inverse inertia diagonal +0x20, inertia rotation +0x30, inverse mass +0x58. */
        uint64_t m[5];memcpy(m,motion+0x20,32);m[4]=0;memcpy(&m[4],motion+0x58,4);b->mass=ptrace_fold(0x50545241434d4153ULL,m,5);
    } else b->mass=0;
    return 1;
}
/* Jolt body flag 0x08 (InvalidateContactCache) is set by a shape change and
 * cleared by the step: in flags, not in ids (big-BS6bo parted only on it, at
 * ticks 1 and 7, one shape refresh a frame apart on two static bodies). */
static uint64_t ptrace_id_hash(const ptrace_body *b,unsigned scene) {
    uint64_t w[3]={(uint64_t)scene|(uint64_t)b->id.klass<<8|(uint64_t)b->id.which<<16|(uint64_t)(b->flags&~8u)<<24|(uint64_t)b->layer<<32,
                   b->id.key,b->body_id};
    return lockstep_mix(ptrace_fold(0x5054524143454944ULL,w,3));
}
static uint64_t ptrace_flags_hash(const ptrace_body *b,unsigned scene) {
    uint64_t w[3]={(uint64_t)scene|(uint64_t)b->id.klass<<8|(uint64_t)b->id.which<<16,b->id.key,b->flags};
    return lockstep_mix(ptrace_fold(0x5054524143464c47ULL,w,3));
}
/* The body's top-level compound (+0x40 shape; Jolt shape +0x18 type: 2
 * decorated, inner at +0x20; 1 compound, sub-shapes +0x50 count, +0x60 array
 * of 0x28: shape, position, rotation, user data). The child order is the
 * SubShapeID: big-BS6bsa/bsc part on destroyer 80000ff8a4da86ef's hull,
 * child 0 on one client and 8 on the other (lockstep_shape_order.h). Digest:
 * per child in index order its class (vtable RVA), sub-type, position and
 * rotation; a leaf gives its class and 0 children. */
#define PTRACE_CHILDREN 1024u
static unsigned char ptrace_children_buffer[PTRACE_CHILDREN*0x28u];
static int ptrace_shape(const unsigned char *body,ptrace_body *b) {
    unsigned char *shape=NULL,head[0x28];b->children=0;
    if(!created_read(body+0x40,&shape,8) || !shape) {b->shape=0;return 1;}
    for(unsigned depth=0;;depth++) {
        if(depth>8 || !created_read(shape,head,0x28)) return 0;
        if(head[0x18]!=2) break;
        memcpy(&shape,head+0x20,8);if(!shape) return 0;
    }
    uintptr_t vt;memcpy(&vt,head,8);uint64_t h=0x5054524143534850ULL,w[3];
    w[0]=vt-(uintptr_t)image;w[1]=(uint64_t)head[0x18]|(uint64_t)head[0x19]<<8;h=ptrace_fold(h,w,2);
    if(head[0x18]==1) {
        uint32_t count=0;unsigned char *subs=NULL;
        if(!created_read(shape+0x50,&count,4) || !created_read(shape+0x60,&subs,8) || count>PTRACE_CHILDREN ||
           (count && (!subs || !created_read(subs,ptrace_children_buffer,(size_t)count*0x28)))) return 0;
        for(unsigned i=0;i<count;i++) {
            const unsigned char *e=ptrace_children_buffer+i*0x28;unsigned char *child=NULL,ch[0x1a];uint64_t pose[3];
            memcpy(&child,e,8);memcpy(pose,e+8,24);
            if(!child || !created_read(child,ch,sizeof(ch))) return 0;
            memcpy(&vt,ch,8);w[0]=(uint64_t)i|(uint64_t)ch[0x18]<<32|(uint64_t)ch[0x19]<<40;w[1]=vt-(uintptr_t)image;
            h=ptrace_fold(h,w,2);h=ptrace_fold(h,pose,3);
        }
        b->children=count;
    }
    b->shape=h;return 1;
}
/* PHYSICS_CHILD rows: the children ptrace_shape just read for this body (still in ptrace_children_buffer). */
static void ptrace_children_note(const ptrace_step_row *r,const ptrace_body *b) {
    for(unsigned i=0;i<b->children && i<PTRACE_CHILDREN;i++) {
        const unsigned char *e=ptrace_children_buffer+i*0x28;unsigned char *child=NULL,ch[0x30];ptrace_child_row o;memset(&o,0,sizeof(o));
        memcpy(&child,e,8);
        if(child && created_read(child,ch,sizeof(ch))) {
            uintptr_t vt;memcpy(&vt,ch,8);o.klass=(uint32_t)(vt-(uintptr_t)image);o.type=ch[0x18];o.subtype=ch[0x19];memcpy(&o.part,ch+0x28,8);o.readable=1;
        }
        o.tick=r->tick;o.step=r->step;o.scene=r->scene;o.id=b->id;o.index=i;o.count=b->children;memcpy(o.pose,e+8,24);memcpy(&o.user,e+0x20,4);
        LONG64 index=InterlockedIncrement64(&ptrace_child_rows)-1;ptrace_child_ring[(uint64_t)index%PTRACE_CHILD_RING]=o;
    }
}
static uint64_t ptrace_active_hash(const ptrace_body *b,unsigned scene) {
    uint64_t w[3]={(uint64_t)scene|(uint64_t)b->id.klass<<8|(uint64_t)b->id.which<<16,b->id.key,b->active};
    return lockstep_mix(ptrace_fold(0x5054524143454143ULL,w,3));
}
static uint64_t ptrace_state_hash(const ptrace_id *id,uint8_t motion,const uint32_t *words,unsigned scene) {
    uint64_t w[2+(PTRACE_WORDS+1)/2];w[0]=(uint64_t)scene|(uint64_t)id->klass<<8|(uint64_t)id->which<<16|(uint64_t)motion<<24;w[1]=id->key;
    return lockstep_mix(ptrace_fold(0x5054524143455354ULL,w,2+(unsigned)ptrace_pack(words,PTRACE_WORDS,w+2)));
}
static unsigned ptrace_scene_of(const unsigned char *system) {
    for(unsigned i=0;i<3;i++) {
        unsigned char *scene=NULL,*owned=NULL;
        if(created_read(image+ptrace_scenes[i],&scene,8) && scene && created_read(scene+0x20,&owned,8) && owned==system) return i;
    }
    /* native_gate_view_scenes: C and D; 3 stays "none of the known". */
    for(unsigned i=0;i<2u;i++) {
        unsigned char *owned=NULL;
        if(vsc_extra[i] && created_read((unsigned char*)vsc_extra[i]+0x20,&owned,8) && owned==system) return 4u+i;
    }
    return 3;
}
static void ptrace_push_step(const ptrace_step_row *r) {
    LONG64 index=InterlockedIncrement64(&ptrace_step_rows)-1;ptrace_step_ring[(uint64_t)index%PTRACE_STEP_RING]=*r;
}
/* Before Update: identities and inputs of every body of the step list. */
static int ptrace_begin(unsigned char *system) {
    ptrace_step_row *r=&ptrace_cur.row;uint64_t count=0,slots=0;uint32_t *list=NULL;unsigned char **bodies=NULL;
    memset(r,0,sizeof(*r));r->tick=stats.issued;r->step=++ptrace_tick_steps;r->scene=(uint8_t)ptrace_scene_of(system);
    if(!created_read(system+0x680,&count,8) || !created_read(system+0x690,&list,8) || !created_read(system+0x10,&slots,8) ||
       !created_read(system+0x20,&bodies,8) || !created_read(system+0x28,&r->num,4) || !created_read(system+0x30,&r->manager,8) ||
       !created_read(system+0x2ac,&r->det,1)) return ptrace_fail("system");
    if(count>PTRACE_BODIES) return ptrace_fail("list_capacity");
    if(count && (!list || !bodies || !created_read(list,ptrace_ids,(size_t)count*4))) return ptrace_fail("list");
    r->size=slots;
    uint64_t w[4]={r->scene,slots,r->num,r->manager};r->manager=ptrace_fold(0x50545241434d474eULL,w,4);
    for(unsigned i=0;i<(unsigned)count;i++) {
        ptrace_body *b=&ptrace_list[i];unsigned char *body=NULL;uint32_t index=ptrace_ids[i]&0x7fffffu,actual=0;memset(b,0,sizeof(*b));
        if(index>=slots || !created_read(bodies+index,&body,8) || !body || ((uintptr_t)body&1) ||
           !created_read(body+0x70,&actual,4) || actual!=ptrace_ids[i]) {++r->missing;continue;}
        ptrace_identity(body,&b->id);
        if(!ptrace_state(body,b)) return ptrace_fail("body");
        b->body=body;++r->bodies;
        if(!b->id.key) {++r->local;continue;}
        if(!ptrace_shape(body,b)) return ptrace_fail("shape");
        if(ptrace_child_ring && r->tick<=ptrace_child_ticks && r->tick>=ptrace_child_from) ptrace_children_note(r,b);
        uint64_t w2[3]={(uint64_t)r->scene|(uint64_t)b->id.klass<<8|(uint64_t)b->id.which<<16|(uint64_t)b->children<<24,b->id.key,b->shape};
        uint64_t ids=ptrace_id_hash(b,r->scene),pre=ptrace_state_hash(&b->id,b->motion,b->words,r->scene);
        uint64_t shape=lockstep_mix(ptrace_fold(0x5054524143534844ULL,w2,3));w2[2]=b->mass;
        uint64_t mass=lockstep_mix(ptrace_fold(0x50545241434d4153ULL,w2,3));
        r->ids+=ids;r->active+=ptrace_active_hash(b,r->scene);r->flags+=ptrace_flags_hash(b,r->scene);r->shape+=shape;r->mass+=mass;r->pre+=pre;++r->keyed;
        if(b->id.klass==WORLD_KEY_CREATED) {r->cids+=ids;r->cshape+=shape;r->cmass+=mass;r->cpre+=pre;++r->created;}
    }
    r->corder=0xcbf29ce484222325ULL;ptrace_cur.count=(unsigned)count;
    ptrace_cur.foreign=InterlockedCompareExchange64(&ptrace_foreign,0,0);ptrace_cur.in_step=1;return 1;
}
/* After Update: outputs, rows, the step row. */
static void ptrace_end(uint32_t result) {
    ptrace_step_row *r=&ptrace_cur.row;r->result=result;ptrace_cur.in_step=0;
    int rows=r->tick<=ptrace_row_ticks;
    for(unsigned i=0;i<ptrace_cur.count;i++) {
        ptrace_body *b=&ptrace_list[i];ptrace_body after=*b;
        if(!b->body || !b->id.key) continue;
        if(!ptrace_state(b->body,&after)) {ptrace_fail("body_after");return;}
        uint64_t post=ptrace_state_hash(&b->id,after.motion,after.words,r->scene);r->post+=post;
        const int created=b->id.klass==WORLD_KEY_CREATED;
        if(created) r->cpost+=post;
        if(!rows || (!created && !ptrace_saved_rows)) continue;
        LONG64 index=InterlockedIncrement64(&ptrace_body_rows)-1;ptrace_body_row *o=&ptrace_body_ring[(uint64_t)index%PTRACE_BODY_RING];
        o->tick=r->tick;o->id=b->id;o->step=r->step;o->body_id=b->body_id;o->active=b->active;o->layer=b->layer;o->scene=r->scene;
        o->children=b->children;o->shape=b->shape;o->mass=b->mass;
        o->motion=b->motion;o->flags=b->flags;memcpy(o->pre,b->words,sizeof(o->pre));memcpy(o->post,after.words,sizeof(o->post));
    }
    r->cforeign=(uint32_t)(InterlockedCompareExchange64(&ptrace_foreign,0,0)-ptrace_cur.foreign);
    ptrace_push_step(r);++ptrace_steps;
}
static uint32_t ptrace_update(void *system,float dt,int steps,void *allocator,void *jobs) {
    ptrace_update_fn original=(ptrace_update_fn)(void *)(image+PTRACE_UPDATE);
    if(!ptrace_installed || !stats.active || boot_phase!=3) {++ptrace_native;return original(system,dt,steps,allocator,jobs);}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&ptrace_foreign_steps);return original(system,dt,steps,allocator,jobs);}
    if(ptrace_tick!=stats.issued) {ptrace_tick=stats.issued;ptrace_tick_steps=0;}
    if(InterlockedCompareExchange64(&ptrace_failures,0,0) || !ptrace_begin(system)) {
        ptrace_cur.in_step=0;++ptrace_native;return original(system,dt,steps,allocator,jobs);
    }
    uint32_t result=original(system,dt,steps,allocator,jobs);
    ptrace_end(result);return result;
}
/* One contact callback (Jolt's body1, body2, manifold, settings). */
static int ptrace_contact_read(const unsigned char *a,const unsigned char *b,const unsigned char *manifold,ptrace_contact_row *c) {
    unsigned char head[0x34];uint32_t count2=0,points[2][PTRACE_POINTS][4];
    const unsigned char *bodies[2]={a,b};
    for(unsigned s=0;s<2;s++) {
        ptrace_identity(bodies[s],&c->id[s]);
        if(!created_read(bodies[s]+0x70,&c->body_id[s],4) || !created_read(bodies[s]+0x78,&c->motion[s],1)) return 0;
    }
    if(!created_read(manifold,head,sizeof(head))) return 0;
    memcpy(c->base,head,12);memcpy(c->normal,head+0x10,12);memcpy(&c->depth,head+0x20,4);memcpy(c->sub,head+0x24,8);memcpy(&c->count,head+0x30,4);
    if(c->count>PTRACE_POINTS || !created_read(manifold+0x440,&count2,4) || count2!=c->count ||
       (c->count && (!created_read(manifold+0x40,points[0],(size_t)c->count*16) || !created_read(manifold+0x450,points[1],(size_t)c->count*16)))) return 0;
    uint64_t h=0x50545241434f4e54ULL;
    for(unsigned s=0;s<2;s++) for(unsigned i=0;i<c->count;i++) {uint64_t w[2]={(uint64_t)points[s][i][0]|(uint64_t)points[s][i][1]<<32,points[s][i][2]};h=ptrace_fold(h,w,2);}
    c->points=h;return 1;
}
static int ptrace_order(const ptrace_id *x,const ptrace_id *y) {
    if(x->klass!=y->klass) return x->klass<y->klass?-1:1;
    if(x->key!=y->key) return x->key<y->key?-1:1;
    return x->which<y->which?-1:x->which>y->which;
}
static void ptrace_contact(ptrace_listener_fn original,unsigned added,void *self,void *a,void *b,void *manifold,void *settings) {
    if(!ptrace_installed || !stats.active || boot_phase!=3) {original(self,a,b,manifold,settings);return;}
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&ptrace_foreign);original(self,a,b,manifold,settings);return;}
    if(!ptrace_cur.in_step) {++ptrace_stray;original(self,a,b,manifold,settings);return;}
    ptrace_step_row *r=&ptrace_cur.row;ptrace_contact_row c;memset(&c,0,sizeof(c));
    int ok=ptrace_contact_read(a,b,manifold,&c);
    original(self,a,b,manifold,settings);
    ++ptrace_contacts;++r->contacts;
    if(!ok || !created_read(settings,c.settings,sizeof(c.settings))) {ptrace_fail("contact");return;}
    if(!c.id[0].key && !c.id[1].key) {++r->clocal;return;}
    c.tick=r->tick;c.step=r->step;c.ord=r->contacts;c.added=(uint8_t)added;c.flip=ptrace_order(&c.id[0],&c.id[1])>0;
    c.settings[6]&=0xffu;c.sensor=(uint8_t)c.settings[6]; /* mIsSensor; the three bytes after it are padding */
    uint64_t w[16]={(uint64_t)c.id[0].klass|(uint64_t)c.id[0].which<<8|(uint64_t)c.id[1].klass<<16|(uint64_t)c.id[1].which<<24|(uint64_t)added<<32,
        c.id[0].key,c.id[1].key,(uint64_t)c.sub[0]|(uint64_t)c.sub[1]<<32,(uint64_t)c.base[0]|(uint64_t)c.base[1]<<32,
        (uint64_t)c.base[2]|(uint64_t)c.normal[0]<<32,(uint64_t)c.normal[1]|(uint64_t)c.normal[2]<<32,(uint64_t)c.depth|(uint64_t)c.count<<32,c.points};
    unsigned n=9+(unsigned)ptrace_pack(c.settings,7,w+9);uint64_t h=ptrace_fold(0x5054524143524543ULL,w,n);
    r->cset+=lockstep_mix(h);r->corder=lockstep_mix(r->corder^h);++r->ckeyed;r->flips+=c.flip;r->sensors+=c.sensor!=0;
    if(c.id[0].klass!=WORLD_KEY_CREATED && c.id[1].klass!=WORLD_KEY_CREATED) return;
    r->ccset+=lockstep_mix(h);++r->ccreated;
    if(r->tick>ptrace_row_ticks) return;
    LONG64 index=InterlockedIncrement64(&ptrace_contact_rows)-1;ptrace_contact_ring[(uint64_t)index%PTRACE_CONTACT_RING]=c;
}
static void ptrace_added(void *self,void *a,void *b,void *manifold,void *settings) {
    ptrace_contact((ptrace_listener_fn)(void *)(image+PTRACE_ADDED),1,self,a,b,manifold,settings);
}
static void ptrace_persisted(void *self,void *a,void *b,void *manifold,void *settings) {
    ptrace_contact((ptrace_listener_fn)(void *)(image+PTRACE_PERSISTED),0,self,a,b,manifold,settings);
}
static int32_t ptrace_rel_to(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+PTRACE_CALL+5));}
/* 0 original, 1 ours (the stub), -1 anything else. */
static int ptrace_call_state(void) {
    unsigned char now[5];int32_t d;if(!read_local(image+PTRACE_CALL,now,5) || now[0]!=0xe8) return -1;
    memcpy(&d,now+1,4);
    if(d==ptrace_rel_to(image+PTRACE_UPDATE)) return 0;
    return ptrace_stub && d==ptrace_rel_to(ptrace_stub)?1:-1;
}
static int ptrace_pins_ok(void) {
    unsigned char copy[0x200];
    for(unsigned p=0;p<sizeof(ptrace_pins)/sizeof(*ptrace_pins);p++) {
        unsigned rva=ptrace_pins[p].rva,size=ptrace_pins[p].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        if(PTRACE_CALL>=rva && PTRACE_CALL+5<=rva+size) {
            if(ptrace_call_state()<0) return 0;
            int32_t d=ptrace_rel_to(image+PTRACE_UPDATE);memcpy(copy+(PTRACE_CALL-rva)+1,&d,4);
        }
        if(!hash_ok(copy,size,ptrace_pins[p].sha)) return 0;
    }
    return 1;
}
/* A 64 KB-granular executable page within rel32 reach of the call site. */
static unsigned char *ptrace_alloc_near(unsigned site,unsigned size) {
    uintptr_t at=(uintptr_t)(image+site)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t lo=(intptr_t)p-(intptr_t)(image+site+5),hi=(intptr_t)(p+size)-(intptr_t)(image+site+5);
            if(lo>=INT32_MIN && hi<=INT32_MAX) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int ptrace_write_stub(void) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=(void*)ptrace_update;DWORD old,ignored;
    if(!VirtualProtect(ptrace_stub,PTRACE_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    memcpy(ptrace_stub,jump,6);memcpy(ptrace_stub+6,&target,8);
    int ok=VirtualProtect(ptrace_stub,PTRACE_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),ptrace_stub,PTRACE_STUB_SIZE);return ok;
}
static int ptrace_stub_owned(void) {
    void *target=NULL;
    return ptrace_stub && ptrace_stub[0]==0xff && ptrace_stub[1]==0x25 && read_local(ptrace_stub+6,&target,8) && target==(void*)ptrace_update;
}
static int ptrace_swap(unsigned i,void *expected,void *value) {
    unsigned char *p=image+ptrace_slots[i];DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static void *ptrace_hook(unsigned i) {return i?(void*)ptrace_persisted:(void*)ptrace_added;}
static int ptrace_cas_call(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(PTRACE_CALL+1,e,v,4);
}
static void ptrace_unhook(void) {
    for(unsigned i=0;i<PTRACE_SLOTS;i++) if((ptrace_swapped&(1u<<i)) && ptrace_swap(i,ptrace_hook(i),image+ptrace_targets[i])) ptrace_swapped&=~(1u<<i);
    if(ptrace_call_done && ptrace_call_state()==1 && ptrace_cas_call(ptrace_rel_to(ptrace_stub),ptrace_rel_to(image+PTRACE_UPDATE))) ptrace_call_done=0;
    else if(ptrace_call_done && ptrace_call_state()==0) ptrace_call_done=0;
}
/* Before the first admitted frame, on the MoveWorker (the only thread that
 * runs Scene::Step); rows = 0..PTRACE_ROW_TICKS_MAX. */
static int ptrace_arm(unsigned rows) {
    if(ptrace_installed) return stats.active && ptrace_row_ticks==rows;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || rows>PTRACE_ROW_TICKS_MAX) return 0;
    if(!world_identity_installed) {ptrace_stage="needs";return 0;}
    if(!ptrace_pins_ok()) {ptrace_stage="pins";return 0;}
    if(ptrace_call_state()!=0) {ptrace_stage="site";return 0;}
    for(unsigned i=0;i<PTRACE_SLOTS;i++) if(*(void**)(image+ptrace_slots[i])!=(void*)(image+ptrace_targets[i])) {ptrace_stage="entry";return 0;}
    if(!ptrace_list) ptrace_list=VirtualAlloc(NULL,sizeof(*ptrace_list)*PTRACE_BODIES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_ids) ptrace_ids=VirtualAlloc(NULL,sizeof(*ptrace_ids)*PTRACE_BODIES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_step_ring) ptrace_step_ring=VirtualAlloc(NULL,sizeof(*ptrace_step_ring)*PTRACE_STEP_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_body_ring) ptrace_body_ring=VirtualAlloc(NULL,sizeof(*ptrace_body_ring)*PTRACE_BODY_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_contact_ring) ptrace_contact_ring=VirtualAlloc(NULL,sizeof(*ptrace_contact_ring)*PTRACE_CONTACT_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_stub) ptrace_stub=ptrace_alloc_near(PTRACE_CALL,PTRACE_STUB_SIZE);
    if(!ptrace_list || !ptrace_ids || !ptrace_step_ring || !ptrace_body_ring || !ptrace_contact_ring || !ptrace_stub) {ptrace_stage="memory";return 0;}
    if(!ptrace_write_stub()) {ptrace_stage="stub";return 0;}
    ptrace_step_rows=ptrace_body_rows=ptrace_contact_rows=0;ptrace_failures=ptrace_foreign=ptrace_foreign_steps=0;
    ptrace_steps=ptrace_native=ptrace_contacts=ptrace_stray=0;ptrace_tick=UINT64_MAX;ptrace_tick_steps=0;ptrace_restored=0;
    memset(&ptrace_cur,0,sizeof(ptrace_cur));ptrace_row_ticks=rows;
    for(unsigned i=0;i<PTRACE_SLOTS;i++) {
        if(!ptrace_swap(i,image+ptrace_targets[i],ptrace_hook(i))) {ptrace_unhook();ptrace_stage="cas";return 0;}
        ptrace_swapped|=1u<<i;
    }
    if(!ptrace_cas_call(ptrace_rel_to(image+PTRACE_UPDATE),ptrace_rel_to(ptrace_stub))) {ptrace_unhook();ptrace_stage="cas";return 0;}
    ptrace_call_done=1;ptrace_installed=1;ptrace_stage="armed";return 1;
}
/* After ptrace_arm, before the first admitted frame: PHYSICS_CHILD rows for ticks from..ticks. */
static int ptrace_children_arm(unsigned ticks,unsigned from) {
    if(ptrace_child_ticks) return ptrace_installed && ptrace_child_ticks==ticks && ptrace_child_from==from;
    if(!ptrace_installed || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread || !ticks || ticks>PTRACE_CHILD_TICKS_MAX ||
       !from || from>ticks) return 0;
    if(!ptrace_child_ring) ptrace_child_ring=VirtualAlloc(NULL,sizeof(*ptrace_child_ring)*PTRACE_CHILD_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ptrace_child_ring) {ptrace_stage="memory";return 0;}
    ptrace_child_rows=0;ptrace_child_ticks=ticks;ptrace_child_from=from;return 1;
}
/* After ptrace_arm, before the first admitted frame: PHYSICS_BODY rows for saved bodies too. */
static int ptrace_saved_arm(void) {
    if(ptrace_saved_rows) return ptrace_installed;
    if(!ptrace_installed || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread || !ptrace_row_ticks) return 0;
    ptrace_saved_rows=1;return 1;
}
static int ptrace_owned(void) {
    if(!ptrace_installed) return 1;
    for(unsigned i=0;i<PTRACE_SLOTS;i++) if((ptrace_swapped&(1u<<i)) && *(void**)(image+ptrace_slots[i])!=ptrace_hook(i)) return 0;
    return (!ptrace_call_done || ptrace_call_state()==1) && ptrace_stub_owned();
}
/* Main-thread tick check; any failure stops the gate with reason 133. */
static int ptrace_check(void) {
    if(InterlockedCompareExchange64(&ptrace_failures,0,0)) return 0;
    const char *why=!ptrace_owned()?"site_changed":!ptrace_pins_ok()?"block_changed":NULL;
    return why?ptrace_fail(why):1;
}
static void ptrace_restore(void) {
    if(!ptrace_installed) return;
    ptrace_unhook();
    if(!ptrace_swapped && !ptrace_call_done) {ptrace_installed=0;ptrace_restored=1;}
}
/* Rows of completed ticks (or any with final, at stop); created keys of
 * bodies made in a row's own tick are assigned at that tick's boundary. */
static void ptrace_resolve(ptrace_id *id) {
    if(id->key || !id->runtime) return;
    uint64_t created=created_lookup(id->runtime);if(created) {id->key=created;id->klass=WORLD_KEY_CREATED;}
}
static int ptrace_hex(char *out,size_t cap,const uint32_t *words,unsigned n) {
    int k=0;for(unsigned i=0;i<n && (size_t)k+9<cap;i++) k+=snprintf(out+k,cap-(size_t)k,"%08x",words[i]);
    return k;
}
static int ptrace_row(unsigned kind,uint64_t index,int final,char *out,size_t cap) {
    volatile LONG64 *rows=kind==PTRACE_ROW_STEP?&ptrace_step_rows:kind==PTRACE_ROW_BODY?&ptrace_body_rows:
                          kind==PTRACE_ROW_CONTACT?&ptrace_contact_rows:&ptrace_child_rows;
    uint64_t ring=kind==PTRACE_ROW_STEP?PTRACE_STEP_RING:kind==PTRACE_ROW_BODY?PTRACE_BODY_RING:kind==PTRACE_ROW_CONTACT?PTRACE_CONTACT_RING:PTRACE_CHILD_RING;
    LONG64 count=InterlockedCompareExchange64(rows,0,0);int n;
    if(kind>PTRACE_ROW_CHILD || !ptrace_step_ring || (kind==PTRACE_ROW_CHILD && !ptrace_child_ring) || (LONG64)index>=count) return 0;
    if(count>(LONG64)ring && index<(uint64_t)(count-(LONG64)ring)) return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    if(kind==PTRACE_ROW_STEP) {
        const ptrace_step_row *r=&ptrace_step_ring[index%ring];
        n=snprintf(out,cap,"index=%llu tick=%llu step=%u scene=%u result=%x det=%u bodies=%u keyed=%u created=%u local=%u missing=%u "
            "size=%llu num=%u manager=%016llx ids=%016llx active=%016llx flags=%016llx shape=%016llx mass=%016llx pre=%016llx post=%016llx "
            "cids=%016llx cshape=%016llx cmass=%016llx cpre=%016llx cpost=%016llx "
            "contacts=%u ckeyed=%u ccreated=%u clocal=%u cforeign=%u flips=%u sensors=%u cset=%016llx corder=%016llx ccset=%016llx",
            (unsigned long long)index,(unsigned long long)r->tick,r->step,r->scene,r->result,r->det,r->bodies,r->keyed,r->created,r->local,r->missing,
            (unsigned long long)r->size,r->num,(unsigned long long)r->manager,(unsigned long long)r->ids,(unsigned long long)r->active,
            (unsigned long long)r->flags,(unsigned long long)r->shape,(unsigned long long)r->mass,(unsigned long long)r->pre,(unsigned long long)r->post,
            (unsigned long long)r->cids,(unsigned long long)r->cshape,(unsigned long long)r->cmass,(unsigned long long)r->cpre,(unsigned long long)r->cpost,
            r->contacts,r->ckeyed,r->ccreated,r->clocal,r->cforeign,r->flips,r->sensors,(unsigned long long)r->cset,(unsigned long long)r->corder,
            (unsigned long long)r->ccset);
        return n>0 && (size_t)n<cap?n:0;
    }
    if(kind==PTRACE_ROW_BODY) {
        ptrace_body_row r=ptrace_body_ring[index%ring];char pre[PTRACE_WORDS*8+1],post[PTRACE_WORDS*8+1];
        if(!final && r.tick>=stats.issued) return 0;
        ptrace_resolve(&r.id);ptrace_hex(pre,sizeof(pre),r.pre,PTRACE_WORDS);ptrace_hex(post,sizeof(post),r.post,PTRACE_WORDS);
        n=snprintf(out,cap,"index=%llu tick=%llu step=%u scene=%u key=%016llx klass=%u which=%u body=%08x active=%08x motion=%u flags=%02x layer=%04x "
            "children=%u shape=%016llx mass=%016llx pre=%s post=%s",
            (unsigned long long)index,(unsigned long long)r.tick,r.step,r.scene,(unsigned long long)r.id.key,r.id.klass,r.id.which,r.body_id,r.active,
            r.motion,r.flags,r.layer,r.children,(unsigned long long)r.shape,(unsigned long long)r.mass,pre,post);
        return n>0 && (size_t)n<cap?n:0;
    }
    if(kind==PTRACE_ROW_CHILD) {
        ptrace_child_row r=ptrace_child_ring[index%ring];char pose[6*8+1];
        if(!final && r.tick>=stats.issued) return 0;
        ptrace_resolve(&r.id);ptrace_hex(pose,sizeof(pose),r.pose,6);
        ptrace_id part={r.part,0,WORLD_KEY_NONE,0};unsigned klass=WORLD_KEY_NONE;
        if(r.part) {part.key=world_identity_key(r.part,&klass);part.klass=(uint8_t)klass;ptrace_resolve(&part);}
        n=snprintf(out,cap,"index=%llu tick=%llu step=%u scene=%u key=%016llx klass=%u which=%u child=%u/%u class=%08x type=%02x/%02x readable=%u "
            "part=%u:%016llx part_runtime=%llx pose=%s user=%08x",
            (unsigned long long)index,(unsigned long long)r.tick,r.step,r.scene,(unsigned long long)r.id.key,r.id.klass,r.id.which,r.index,r.count,
            r.klass,r.type,r.subtype,r.readable,part.klass,(unsigned long long)part.key,(unsigned long long)r.part,pose,r.user);
        return n>0 && (size_t)n<cap?n:0;
    }
    ptrace_contact_row r=ptrace_contact_ring[index%ring];char settings[7*8+1];
    if(!final && r.tick>=stats.issued) return 0;
    ptrace_resolve(&r.id[0]);ptrace_resolve(&r.id[1]);ptrace_hex(settings,sizeof(settings),r.settings,7);
    n=snprintf(out,cap,"index=%llu tick=%llu step=%u ord=%u added=%u a=%u:%016llx/%u/%08x/%u b=%u:%016llx/%u/%08x/%u sub=%08x/%08x flip=%u "
        "base=%08x%08x%08x normal=%08x%08x%08x depth=%08x count=%u points=%016llx settings=%s sensor=%u",
        (unsigned long long)index,(unsigned long long)r.tick,r.step,r.ord,r.added,r.id[0].klass,(unsigned long long)r.id[0].key,r.id[0].which,r.body_id[0],r.motion[0],
        r.id[1].klass,(unsigned long long)r.id[1].key,r.id[1].which,r.body_id[1],r.motion[1],r.sub[0],r.sub[1],r.flip,
        r.base[0],r.base[1],r.base[2],r.normal[0],r.normal[1],r.normal[2],r.depth,r.count,(unsigned long long)r.points,settings,r.sensor);
    return n>0 && (size_t)n<cap?n:0;
}
static int ptrace_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d rows=%u frame=%llu steps=%llu native=%llu foreign_steps=%lld contacts=%llu foreign=%lld stray=%llu "
        "step_rows=%lld body_rows=%lld contact_rows=%lld restored=%d stage=%s failures=%lld child_ticks=%u child_from=%u child_rows=%lld saved_rows=%d",
        ptrace_installed,ptrace_row_ticks,(unsigned long long)stats.issued,(unsigned long long)ptrace_steps,(unsigned long long)ptrace_native,
        (long long)ptrace_foreign_steps,(unsigned long long)ptrace_contacts,(long long)ptrace_foreign,(unsigned long long)ptrace_stray,
        (long long)ptrace_step_rows,(long long)ptrace_body_rows,(long long)ptrace_contact_rows,ptrace_restored,ptrace_stage,(long long)ptrace_failures,
        ptrace_child_ticks,ptrace_child_from,(long long)ptrace_child_rows,ptrace_saved_rows);
    return n>0 && (size_t)n<cap?n:0;
}
