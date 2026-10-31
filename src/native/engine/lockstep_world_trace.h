/* Bounded, read-only imported-world motion observer. All layouts belong to the
 * complete EXE_SHA build verified before gate installation. No native getter,
 * extra patch site, actor registration, or simulation write is introduced.
 * Raw blocks include local pointers/padding: diagnostics, never portable hashes.
 * Each native wrapper calls its original exactly once, even if tracing fails. */
#define WORLD_TRACE_MAX 2048u
#define WORLD_TRACE_WATCH_MAX 64u
#define WORLD_TRACE_QUEUE_MAX 16u
#define WORLD_TRACE_COMMAND_BYTES 0x90u
enum { WT_BASELINE,WT_SHIP,WT_NAV,WT_AGENT,WT_STEERING,WT_SPLINE,WT_LOADED,WT_PATH_BEFORE,WT_PATH_AFTER,WT_AI,WT_PENDING,WT_ORDER };
typedef struct {
    uint64_t saved,runtime;
    unsigned char *object,*controller,*agent;
} world_trace_watch;
typedef struct {
    uint64_t rng,tls_time;
    uint32_t tls_view,read,write,read_end,write_end,model_rva,model_length,stage;
    uint32_t queue_count[2],queue_capacity[2],queue_unknown_refs;
    uintptr_t queue_bounds[2][3];
    unsigned char banks[0x340],params[0x4e0],agent_state[0x5f0],model[0x4b0],slot[0x1a0];
    unsigned char nav[21],agent_timer[9],manager[48];
    unsigned char queue[2][WORLD_TRACE_QUEUE_MAX*WORLD_TRACE_COMMAND_BYTES],queue_refs[2][WORLD_TRACE_QUEUE_MAX*12];
} world_trace_snapshot;
typedef struct {
    volatile LONG complete;
    unsigned kind,watch,errors,thread,ui,cheap,player,dt;
    uint64_t tick,parent,enter,leave,time;
    uint64_t pc_before,pc_after;
    unsigned caller,boot;
    char script[96];
    void *self,*slot,*tls;
    world_trace_snapshot before,after;
} world_trace_event;
static world_trace_watch world_trace_watches[WORLD_TRACE_WATCH_MAX];
static unsigned world_trace_watch_count,world_trace_last_tick,world_trace_exported;
static world_trace_event *world_trace_events;
static volatile LONG64 world_trace_count,world_trace_order,world_trace_failures,world_trace_dropped;
static volatile LONG world_trace_enabled;
static volatile LONG world_trace_early;
static DWORD world_trace_tls=TLS_OUT_OF_INDEXES;

static int tsnap_unpatched_hash(unsigned rva,ULONG length,const char *expected);
static int world_trace_layout_pins(void) {
    /* Complete contiguous functions: model selection/allocation, Linear ctor,
     * Linear Update, and enclosing Agent Update. No new entry is patched. */
    return hash_ok(image+0xe87cf0,1124,"c9d5c8e4b36b4814dc9bf479faa0c8921aeaf4904465cb827f4f4a0634779f5b") &&
        hash_ok(image+0xeb1a10,735,"6692d3e753b9a9d481c7321f77aae910a1213c4826d562c21e0fdffdae64078d") &&
        hash_ok(image+0xeb27c0,5212,"7920e6e4ff99bf0d0c348eeab202b0a21f5e9d69c4005ba75d302eac7ea0c8a4") &&
        /* Agent Update, seen without the travel-snapshot one-byte read patch. */
        tsnap_unpatched_hash(0xe8f250,10613,"ae0d8b729a39a75f83b1c3d783585f587de1cc2e5eff846620f2f0c482504f52") &&
        hash_ok(image+0xe8dfc0,4180,"3ceb3030f466a8d7346de9359a97b67e0fc1098593d0ed14188b434527b2306c") &&
        hash_ok(image+0xe8d850,1892,"e32a6ed0c9172b1d4c28fdc3880a0b1aff3b7678e96e9d47fa0c0b765f6efa79");
}

static int world_trace_queue_copy(int (*reader)(const void *,void *,size_t),const void *header,
                                  world_trace_snapshot *s,unsigned queue) {
    uintptr_t bounds[3],recheck[3];unsigned char verify[WORLD_TRACE_QUEUE_MAX*WORLD_TRACE_COMMAND_BYTES];
    if(queue>1 || !reader(header,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || bounds[2]<bounds[1] ||
       (bounds[0] && (bounds[0]<0x10000 || bounds[0]%16)) || (!bounds[0] && bounds[2]) ||
       (bounds[1]-bounds[0])%WORLD_TRACE_COMMAND_BYTES || (bounds[2]-bounds[0])%WORLD_TRACE_COMMAND_BYTES) return 0;
    uintptr_t count=(bounds[1]-bounds[0])/WORLD_TRACE_COMMAND_BYTES,capacity=(bounds[2]-bounds[0])/WORLD_TRACE_COMMAND_BYTES;
    if(count>WORLD_TRACE_QUEUE_MAX || capacity>65536) return 0;
    memcpy(s->queue_bounds[queue],bounds,sizeof(bounds));
    s->queue_count[queue]=(unsigned)count;s->queue_capacity[queue]=(unsigned)capacity;
    size_t bytes=count*WORLD_TRACE_COMMAND_BYTES;
    if(bytes && (!reader((void*)bounds[0],s->queue[queue],bytes) ||
                 !reader((void*)bounds[0],verify,bytes) || memcmp(verify,s->queue[queue],bytes))) return 0;
    if(!reader(header,recheck,sizeof(recheck)) || memcmp(bounds,recheck,sizeof(bounds))) return 0;
    for(unsigned i=0;i<count;i++) {
        unsigned char *command=s->queue[queue]+i*WORLD_TRACE_COMMAND_BYTES;uintptr_t vtable;uint64_t runtime,saved;
        memcpy(&vtable,command,8);memcpy(&runtime,command+0x10,8);
        if(vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return 0;
        unsigned rva=(unsigned)(vtable-(uintptr_t)image);saved=runtime?world_identity_lookup(runtime):0;
        memcpy(s->queue_refs[queue]+i*12,&rva,4);memcpy(s->queue_refs[queue]+i*12+4,&saved,8);
        if(runtime && !saved) ++s->queue_unknown_refs;
    }
    return s->queue_unknown_refs==0;
}

static int world_trace_banks(uint32_t *read,uint32_t *write) {
    return read_local(image+0x38f4ee4,read,4) && read_local(image+0x2f4b6f4,write,4) &&
        *read<=1 && *write<=1 && *read+*write==1;
}
static int world_trace_identity(world_trace_watch *w) {
    uint64_t id;unsigned char *controller,*slots,*owner,*agent;void *vtable;
    return read_local(w->object+8,&id,8) && id==w->runtime &&
        read_local(w->object+0x200,&controller,8) && controller==w->controller &&
        read_local(controller,&vtable,8) && vtable==image+0x2c51158 &&
        read_local(controller+0x28,&owner,8) && owner==w->object &&
        read_local(controller+0x30,&slots,8) && slots==w->agent+0xad0 &&
        read_local(controller+0x40,&agent,8) && agent==w->agent &&
        read_local(w->agent+0xef0,&owner,8) && owner==w->object;
}
static int world_trace_capture(world_trace_event *e,world_trace_snapshot *s) {
    world_trace_watch *w=&world_trace_watches[e->watch];
    unsigned char *model,*owner,*manager;uintptr_t vtable;
    s->stage=1;if(!world_trace_identity(w)) return 0;
    s->stage=2;if(!world_trace_banks(&s->read,&s->write)) return 0;
    s->stage=3;if(!read_local(w->agent+0xad0,s->banks,sizeof(s->banks)) ||
       !read_local(w->agent,s->params,sizeof(s->params)) ||
       !read_local(w->agent+0x4e0,s->agent_state,sizeof(s->agent_state)) ||
       !read_local(w->agent+0xac0,s->agent_timer,sizeof(s->agent_timer)) ||
       !read_local(w->controller+0xc0,s->nav,12) ||
       !read_local(w->controller+0xd0,s->nav+12,9) ||
       !read_local(w->agent+0xf10,&model,8)) return 0;
    s->stage=4;if(model) {
        if(!read_local(model,&vtable,8) || !read_local(model+0x10,&owner,8) || owner!=w->agent) return 0;
        s->model_rva=(unsigned)(vtable-(uintptr_t)image);
        if(vtable==(uintptr_t)(image+0x2c4e9c8)) s->model_length=0x3a0;
        else if(vtable==(uintptr_t)(image+0x2c4f480)) s->model_length=0x4b0;
        else if(vtable==(uintptr_t)(image+0x2c4f610)) s->model_length=0x270;
        else return 0; /* Unsupported model cannot silently count as coverage. */
        if((e->kind==WT_STEERING || e->kind==WT_SPLINE) && model!=e->self) return 0;
        if(!read_local(model,s->model,s->model_length)) return 0;
    } else if(e->kind==WT_STEERING || e->kind==WT_SPLINE) return 0;
    /* Agent consumes these native 0x90-byte vectors before updating its model.
     * Copy twice and recheck all vector bounds; never call queue accessors. */
    s->stage=10;if(!world_trace_queue_copy(read_local,w->agent+0x6a8,s,0) ||
                   !world_trace_queue_copy(read_local,w->agent+0x6c0,s,1)) return 0;
    for(unsigned i=0;i<2;i++) {
        uintptr_t bounds[3];
        if(!read_local(w->agent+(i?0x6c0:0x6a8),bounds,sizeof(bounds)) ||
           memcmp(bounds,s->queue_bounds[i],sizeof(bounds))) return 0;
    }
    s->stage=5;if(e->slot && !read_local(e->slot,s->slot,sizeof(s->slot))) return 0;
    s->stage=6;if(!e->tls || !read_local((unsigned char*)e->tls+0x300,&s->tls_time,8) ||
       !read_local((unsigned char*)e->tls+0x314,&s->tls_view,4) ||
       !read_local((unsigned char*)e->tls+0x318,&s->rng,8)) return 0;
    /* Local wall-clock budget evidence is not a deterministic comparison key. */
    s->stage=7;if(!read_local(image+0x6cf21c8,&manager,8) || !manager ||
       !read_local(manager+0x1f8,s->manager,40) || !read_local(manager+0x84,s->manager+40,8)) return 0;
    s->stage=8;if(!world_trace_banks(&s->read_end,&s->write_end) ||
        s->read!=s->read_end || s->write!=s->write_end || !world_trace_identity(w)) return 0;
    s->stage=9;return 1;
}
static world_trace_event *world_trace_begin_context(unsigned kind,void *self,void *slot,float dt,double time,
                                                   unsigned cheap,unsigned player,void *tls) {
    if(!world_trace_enabled || !stats.active || boot_phase!=3 || (!stats.issued && kind!=WT_LOADED) || stats.issued>world_trace_last_tick) return NULL;
    unsigned index=world_trace_watch_count;void *model_agent=NULL;
    if((kind==WT_STEERING || kind==WT_SPLINE) && !read_local((unsigned char*)self+0x10,&model_agent,8)) return NULL;
    for(unsigned i=0;i<world_trace_watch_count;i++) {
        world_trace_watch *w=&world_trace_watches[i];
        if(((kind==WT_BASELINE || kind==WT_SHIP || kind>=WT_LOADED) && self==w->object) ||
           (kind==WT_NAV && self==w->controller) || (kind==WT_AGENT && self==w->agent) ||
           ((kind==WT_STEERING || kind==WT_SPLINE) && model_agent==w->agent)) {index=i;break;}
    }
    if(index==world_trace_watch_count) return NULL;
    LONG64 n=InterlockedIncrement64(&world_trace_count);
    if(n>WORLD_TRACE_MAX) {InterlockedIncrement64(&world_trace_dropped);return NULL;}
    world_trace_event *e=&world_trace_events[n-1];
    e->kind=kind;e->watch=index;e->tick=stats.issued;e->self=self;e->slot=slot;e->tls=tls;
    e->boot=(unsigned)boot_phase;
    e->cheap=cheap;e->player=player;e->thread=GetCurrentThreadId();e->ui=e->thread==ui_thread;
    memcpy(&e->dt,&dt,4);memcpy(&e->time,&time,8);
    e->parent=(uint64_t)(uintptr_t)TlsGetValue(world_trace_tls);
    e->enter=(uint64_t)InterlockedIncrement64(&world_trace_order);
    if(!TlsSetValue(world_trace_tls,(void*)(uintptr_t)n)) e->errors|=1;
    if(!world_trace_capture(e,&e->before)) e->errors|=2;
    return e;
}
static world_trace_event *world_trace_begin(unsigned kind,void *self,void *slot,float dt,double time,unsigned cheap,unsigned player) {
    if(!world_trace_enabled || !stats.active || boot_phase!=3 || (!stats.issued && kind!=WT_LOADED) || stats.issued>world_trace_last_tick) return NULL;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return world_trace_begin_context(kind,self,slot,dt,time,cheap,player,tls?tls[0]:NULL);
}
static void world_trace_end(world_trace_event *e) {
    if(!e) return;
    if(!world_trace_capture(e,&e->after)) e->errors|=4;
    uint64_t n=(uint64_t)(e-world_trace_events)+1;
    if(e->thread!=GetCurrentThreadId() || (uint64_t)(uintptr_t)TlsGetValue(world_trace_tls)!=n) e->errors|=8;
    if(!TlsSetValue(world_trace_tls,(void*)(uintptr_t)e->parent)) e->errors|=16;
    e->leave=(uint64_t)InterlockedIncrement64(&world_trace_order);
    if(e->errors) InterlockedIncrement64(&world_trace_failures);
    InterlockedExchange(&e->complete,1); /* publish the complete immutable record */
}
static int world_trace_prepare(unsigned last) {
    if(world_trace_events || last<2 || last>12 || !world_trace_watch_count || world_trace_watch_count>WORLD_TRACE_WATCH_MAX) return 0;
    for(unsigned i=0;i<world_trace_watch_count;i++) if(!world_trace_identity(&world_trace_watches[i])) return 0;
    world_trace_events=VirtualAlloc(NULL,sizeof(*world_trace_events)*WORLD_TRACE_MAX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!world_trace_events) return 0;
    world_trace_tls=TlsAlloc();if(world_trace_tls==TLS_OUT_OF_INDEXES) return 0;
    world_trace_last_tick=last;InterlockedExchange(&world_trace_enabled,1);return 1;
}
/* Called only at the joined UI boundary. Never reset/reuse event storage while
 * callbacks can retain it, including after stop or an incomplete observation. */
static unsigned world_trace_ready(void) {
    LONG64 total=InterlockedCompareExchange64(&world_trace_count,0,0);
    unsigned count=total>WORLD_TRACE_MAX?WORLD_TRACE_MAX:(unsigned)total;
    for(unsigned i=0;i<count;i++) if(!InterlockedCompareExchange(&world_trace_events[i].complete,0,0)) return i;
    return count;
}
