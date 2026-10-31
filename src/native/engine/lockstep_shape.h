/* Fixture body shape refresh has its own randomized deadline. Keep the native
 * update, isolate its random stream, and optionally align its first refresh. */
#define SHAPE_ENTRY 0xbff80u
#define SHAPE_EXTRA_ROLES 15u /* Three known scenes, five native object layers. */
#define SHAPE_RECORDS (2u+SHAPE_EXTRA_ROLES)
typedef void (*shape_fn)(void *,unsigned char);
typedef struct {uint64_t tick,calls,total_calls,time,due_before,due_after,native_due_after,rng_before,rng_after,init_total,last_init;unsigned flag,initialized,forced;} shape_sample;
static shape_sample shape_samples[ACTOR_CAPACITY][SHAPE_RECORDS];
typedef struct {void *holder;uint64_t id;} shape_holder_key;
static shape_holder_key shape_initialized[ACTOR_CAPACITY][SHAPE_RECORDS];
typedef struct {void *record;uint32_t body_id;unsigned replaced;} shape_extra_identity;
static shape_extra_identity shape_extra[2][SHAPE_EXTRA_ROLES];
static unsigned shape_extra_registered,shape_extra_replaced;
static unsigned char *shape_dock_objects[2],*shape_dock_anchors[2];
static uint64_t shape_dock_ids[2],shape_dock_anchor_ids[2];
static int shape_docking,shape_dock_full;
static unsigned shape_dock_registered;
static int shape_installed,shape_restored,shape_seeded,shape_initial;
static unsigned shape_inits;
static uint64_t shape_original_word,shape_patched_word,shape_calls,shape_failures;
static uint64_t shape_forced_refreshes;
#define SHAPE_LIFETIMES 256u
typedef struct {void *record,*holder;uint32_t body_id;unsigned present;uint64_t holder_id;} shape_identity;
typedef struct {
    uint64_t tick,ordinal,due_before;unsigned actor,which,seen,initialized;
    shape_identity before,after;
} shape_lifetime;
static shape_identity shape_identities[ACTOR_CAPACITY][SHAPE_RECORDS];
static shape_lifetime shape_lifetimes[SHAPE_LIFETIMES];
static unsigned shape_lifetime_count;
static uint64_t shape_lifetime_dropped;
static uint64_t shape_lifetime_calls;
/* Slot state of a BodyID in a Jolt body manager (BodyManager::mBodies, index in
 * the low 23 bits, generation above): 1 = the slot holds exactly this body,
 * 0 = the body is gone (free slot, low bit set, or a later generation in the
 * slot), -1 = unreadable or out of range. */
static int shape_body_slot(unsigned char *manager,uint32_t body_id) {
    uint64_t count;unsigned char *bodies,*body;uint32_t actual;
    if(!read_local(manager,&count,8) || count>0x800000u || (body_id&0x7fffffu)>=count ||
       !read_local(manager+0x10,&bodies,8) || !bodies || !read_local(bodies+(body_id&0x7fffffu)*8,&body,8)) return -1;
    if(!body || ((uintptr_t)body&1)) return 0;
    if(!read_local(body+0x70,&actual,4)) return -1;
    return actual==body_id;
}
/* Additional DockArea records are not stored in Component's two standard body
 * slots. Resolve their live Jolt identity; a scene/layer role must be unique for
 * this bounded fixture. Never seed from an address or a process-local BodyID.
 * Attention changes destroy and rebuild these bodies (the dock leaves and
 * re-enters the physics scene), so a role may be re-registered by a new record
 * or body, but only when the registered body is provably gone from the same
 * scene's body manager: its slot is free or holds a later generation. The
 * role (scene x layer) keys the stream, so re-registration keeps the seeds.
 * A still-live registered body (true duplicate), the same BodyID under another
 * record, or an unreadable slot fails closed. */
static int shape_extra_role(unsigned dock,unsigned char *record,unsigned *which) {
    static const unsigned scene_rvas[3]={0x38f0748u,0x38f0740u,0x38f0738u};
    unsigned char *object,*xbody,*scene,*system,*iface,*manager,*bodies,*body;
    void *userdata,*known;uint64_t count,id;uint32_t body_id,actual_id;uint16_t layer;unsigned scene_role=3;
    if(dock>=2 || !record || !read_local(record,&object,8) || object!=shape_dock_objects[dock] ||
       !read_local(object+8,&id,8) || id!=shape_dock_ids[dock] ||
       !read_local(record+0x10,&xbody,8) || !xbody || !read_local(xbody+8,&body_id,4) || body_id==0xffffffffu ||
       !read_local(record+0x18,&scene,8) || !scene) return 0;
    for(unsigned i=0;i<3;i++) if(read_local(image+scene_rvas[i],&known,8) && scene==known) {
        if(scene_role!=3) return 0;
        scene_role=i;
    }
    if(scene_role==3 || !read_local(scene+0x20,&system,8) || !system ||
       !read_local(system+0x1b8,&iface,8) || !iface || !read_local(iface+8,&manager,8) || !manager ||
       !read_local(manager,&count,8) || count>0x800000u || (body_id&0x7fffffu)>=count ||
       !read_local(manager+0x10,&bodies,8) || !bodies ||
       !read_local(bodies+(body_id&0x7fffffu)*8,&body,8) || !body || ((uintptr_t)body&1) ||
       !read_local(body+0x70,&actual_id,4) || actual_id!=body_id ||
       !read_local(body+0x50,&userdata,8) || userdata!=record ||
       !read_local(body+0x74,&layer,2) || layer>=5) return 0;
    unsigned role=scene_role*5+layer;shape_extra_identity *entry=&shape_extra[dock][role];
    if(entry->record && (entry->record!=record || entry->body_id!=body_id)) {
        if(entry->body_id==body_id || shape_body_slot(manager,entry->body_id)!=0) return 0;
        entry->record=record;entry->body_id=body_id;++entry->replaced;++shape_extra_replaced;
    }
    if(!entry->record) {entry->record=record;entry->body_id=body_id;++shape_extra_registered;}
    *which=2+role;return 1;
}
static int shape_holder_id(void *holder,uint64_t *id) {
    *id=0;return !holder || (read_local((unsigned char*)holder+8,id,8) && *id!=0);
}
static int shape_holder_seen(unsigned actor,void *holder,uint64_t id) {
    for(unsigned i=0;i<(actor_is_dock(actor)?SHAPE_RECORDS:2u);i++)
        if(shape_initialized[actor-1][i].holder==holder && shape_initialized[actor-1][i].id==id) return 1;
    return 0;
}
static shape_lifetime *shape_lifetime_observe(unsigned actor,unsigned which,void *record,void *holder) {
    void *xbody;uint32_t body_id=UINT32_MAX;uint64_t holder_id;
    if(gate_play_mode) return NULL;
    if(!read_local((unsigned char*)record+0x10,&xbody,8) ||
       (xbody && !read_local((unsigned char*)xbody+8,&body_id,4)) ||
       !shape_holder_id(holder,&holder_id)) {++shape_failures;return NULL;}
    shape_identity *old=&shape_identities[actor-1][which];
    if(old->present && old->record==record && old->holder==holder && old->body_id==body_id && old->holder_id==holder_id) return NULL;
    /* Drained only with native_gate_shape_lifetimes: rows, so a full table loses rows and keeps the session. */
    if(shape_lifetime_count==SHAPE_LIFETIMES) {++shape_lifetime_dropped;*old=(shape_identity){record,holder,body_id,1,holder_id};return NULL;}
    shape_lifetime *e=&shape_lifetimes[shape_lifetime_count++];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->ordinal=++shape_lifetime_calls;e->actor=actor;e->which=which;e->before=*old;
    *old=(shape_identity){record,holder,body_id,1,holder_id};e->after=*old;
    e->seen=holder && shape_holder_seen(actor,holder,holder_id);
    if(holder && !read_local((unsigned char*)holder+0x28,&e->due_before,8)) ++shape_failures;
    return e;
}
static int shape_bind_dock(unsigned slot,unsigned char *anchor,unsigned char *object) {
    void *type;uint64_t id,anchor_id;unsigned char *parent=object;
    if(slot<1 || slot>2 || !anchor || !object || anchor==object ||
       !read_local(object,&type,8) || type!=image+0x2adbdc0 ||
       !read_local(object+8,&id,8) || !id || !read_local(anchor+8,&anchor_id,8) || !anchor_id) return 0;
    unsigned i=slot-1;
    for(unsigned depth=0;depth<16 && parent!=anchor;depth++)
        if(!read_local(parent+0x70,&parent,8) || !parent) return 0;
    if(parent!=anchor || shape_dock_objects[1-i]==object || shape_dock_anchors[1-i]==anchor) return 0;
    if(shape_dock_objects[i]) return shape_dock_objects[i]==object && shape_dock_ids[i]==id &&
        shape_dock_anchors[i]==anchor && shape_dock_anchor_ids[i]==anchor_id;
    shape_dock_objects[i]=object;shape_dock_ids[i]=id;
    shape_dock_anchors[i]=anchor;shape_dock_anchor_ids[i]=anchor_id;++shape_dock_registered;return 1;
}
static void shape_execute(shape_fn original,shape_sample *s,unsigned actor,unsigned which,uint64_t *rng,
                          void *record,unsigned char *holder,double time,unsigned char flag) {
    uint64_t saved=rng?*rng:0;s->initialized=s->forced=0;s->due_before=s->due_after=s->native_due_after=0;
    if(holder) {
        uint64_t holder_id;
        if(!shape_holder_id(holder,&holder_id)) {++shape_failures;original(record,flag);return;}
        memcpy(&s->due_before,holder+0x28,8);
        /* Holder constructors assign a new native ID even when the allocator
         * recycles an address. This key is only local lifetime bookkeeping;
         * native IDs never enter the random stream or portable state hash. */
        if(shape_initial && !shape_holder_seen(actor,holder,holder_id)) {
            *(double*)(holder+0x28)=time-gate_tick_dt();
            s->initialized=1;++shape_inits;
            ++s->init_total;s->last_init=stats.issued;
        }
        /* Remember every referencing role, including a shared holder that was
         * initialized through another role. Replacing that other role must not
         * make this still-live holder look new. */
        if(shape_initial) shape_initialized[actor-1][which]=(shape_holder_key){holder,holder_id};
        /* Native refresh intervals also depend on view distance and shape
         * bounds. Optional fixture cadence removes those scheduling inputs;
         * native child transforms/shape updates still run normally once. */
        if(shape_dock_full && actor_is_dock(actor)) {
            *(double*)(holder+0x28)=time-gate_tick_dt();s->forced=1;++shape_forced_refreshes;
        }
    }
    const int restore_rng=shape_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x53484150455f5550ULL,stats.issued,actor,which,s->calls,0);
        s->rng_before=rng?*rng:0;original(record,flag);s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&shape_failures);
        SetLastError(error);
    }
    if(holder) {
        memcpy(&s->native_due_after,holder+0x28,8);
        if(s->forced) *(double*)(holder+0x28)=time;
        memcpy(&s->due_after,holder+0x28,8);
    }
}
static void shape_move(void *record,unsigned char flag) {
    registry_observe_owner(record,0,REGISTRY_SHAPE);
    shape_fn original=(shape_fn)(thunk+384);unsigned actor=0,which=0;unsigned char *object,*holder;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && read_local(record,&object,8)) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
            uint64_t id;void *main_record,*second_record;
            if(object!=actor_components[i] || !object || !read_local(object+8,&id,8) || id!=actor_ids[i]) continue;
            if(!read_local(object+0x260,&main_record,8) || !read_local(object+0x268,&second_record,8)) break;
            if(record==main_record) {actor=i+1;which=0;break;}
            if(record==second_record) {actor=i+1;which=1;break;}
        }
        if(!actor && shape_docking) for(unsigned i=0;i<2;i++) {
            uint64_t id;void *main_record,*second_record;
            if(!object || object!=shape_dock_objects[i]) continue;
            if(!read_local(object+8,&id,8) || id!=shape_dock_ids[i] ||
               !read_local(object+0x260,&main_record,8) || !read_local(object+0x268,&second_record,8)) {++shape_failures;break;}
            if(record==main_record) {actor=i+13;which=0;break;}
            if(record==second_record) {actor=i+13;which=1;break;}
            if(shape_extra_role(i,record,&which)) actor=i+13;
            else ++shape_failures;
            break;
        }
    }
    if(!actor) {original(record,flag);return;}
    shape_sample *s=&shape_samples[actor-1][which];
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++s->total_calls;++shape_calls;s->flag=flag;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;double time;
    if(!rng || !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time) ||
       !read_local((unsigned char*)record+0x20,&holder,8)) {++shape_failures;original(record,flag);return;}
    memcpy(&s->time,&time,8);
    shape_lifetime *lifetime=shape_lifetime_observe(actor,which,record,holder);
    shape_execute(original,s,actor,which,rng,record,holder,time,flag);
    if(lifetime) lifetime->initialized=s->initialized;
}
static int shape_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+SHAPE_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+SHAPE_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+SHAPE_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+SHAPE_ENTRY,8);return actual==expected;
}
static int shape_install(int seed,int initial,int docking,int full) {
    if(full && !docking) return 0;
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+SHAPE_ENTRY,32,"95c3e58aa23320b393d50359ccc7f572d401e805e81855895fb46480cb988232")) return 0;
    if(!hash_ok(image+0xc0010,227,"6ef0773000f2b5a6c8b881e0906902b68212df148ba99bb8ba6f6708062b4eb4") ||
       !hash_ok(image+0xcc220,268,"5ead430da168fdfe1f4dfb0eb328b7dd6b04cf1dc6a5ee56ea3bcfb133916bd2")) return 0;
    if(docking && (!seed || !initial ||
       !hash_ok(image+SHAPE_ENTRY,138,"8278ac79100ba088a0d26da942aefbfc9254e98b84c8c44d3807726250460321") ||
       !hash_ok(image+0xc2c00,904,"ce34f4bbe700f06364ec545aaa2fe9e7be198c751ad0ed0e06239ecf0b56cdf4") ||
       !hash_ok(image+0xc2810,996,"af541073310dc1a7ac2e0d773dd87c041c76021abbda46e9b6309e1ef4d4aee9"))) return 0;
    intptr_t distance=(intptr_t)(thunk+416)-(intptr_t)(image+SHAPE_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(384)) return 0;
    memcpy(&shape_original_word,image+SHAPE_ENTRY,8);shape_patched_word=shape_original_word;
    unsigned char *patch=(unsigned char*)&shape_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!shape_swap(shape_original_word,shape_patched_word)) return 0;
    shape_seeded=seed;shape_initial=initial;shape_docking=docking;shape_dock_full=full;shape_installed=1;return 1;
}
static void shape_restore(void) {
    if(!shape_installed) return;
    shape_restored=shape_swap(shape_patched_word,shape_original_word);
    if(shape_restored) shape_installed=0;
}
