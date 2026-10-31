/* Fixture trace, native obstacle refresh and bounded result join for spline flight.
 * Raw model bytes contain pointers and are diagnostic, never portable hashes. */
#define SPLINE_ENTRY 0x2c4f4f0u
#define SPLINE_UPDATE 0xeb7230u
typedef struct {
    uint64_t tick,calls,time,cheap,applied_cheap,player,waited;
    uint32_t dt,model[2][0x4b0/4],slot[2][0x1a0/4],params[0x1a0/4];
    uint64_t pending_count[2];
    uint32_t pending_status[2],pending_flags[2];
} spline_sample;
static spline_sample spline_samples[ACTOR_CAPACITY];
static int spline_installed,spline_restored,spline_refresh,spline_join;
static uint64_t spline_calls,spline_failures,spline_refreshes,spline_joins,spline_waits,spline_timeouts;

static int spline_pending(spline_sample *s,void *model,unsigned phase) {
    uint64_t queue[5];void *block,*request;
    s->pending_count[phase]=0;s->pending_status[phase]=0;s->pending_flags[phase]=0;
    if(!read_local((unsigned char*)model+0x428,queue,sizeof(queue))) return 0;
    s->pending_count[phase]=queue[4];
    if(!queue[4]) return 1;
    /* Native deque: two request pointers per block, power-of-two block map.
     * The pathing owner controls the deque. The result worker publishes its
     * status/flags under the same critical section used by native Update. */
    if(!queue[1] || !queue[2] || queue[2]>4096 || (queue[2]&(queue[2]-1)) || queue[4]>queue[2]*2 ||
       !read_local((void*)(uintptr_t)(queue[1]+8*((queue[3]>>1)&(queue[2]-1))),&block,8) || !block ||
       !read_local((unsigned char*)block+8*(queue[3]&1),&request,8) || !request) return 0;
    EnterCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    int ok=read_local((unsigned char*)request+0xa8,&s->pending_status[phase],4) &&
           read_local((unsigned char*)request+0xac,&s->pending_flags[phase],4);
    LeaveCriticalSection((CRITICAL_SECTION*)(image+0x3d74938));
    return ok;
}

static int spline_join_pending(spline_sample *s,void *model,unsigned timeout) {
    s->waited=0;
    if(!spline_pending(s,model,0)) return 0;
    if(!s->pending_count[0]) return 1;
    InterlockedIncrement64((volatile LONG64*)&spline_joins);
    if(s->pending_status[0]>1) return 0;
    if(s->pending_status[0]) return 1;
    void *worker,*vtable;unsigned char running;
    if(!read_local(image+0x6cf29c0,&worker,8) || !worker ||
       !read_local(worker,&vtable,8) || vtable!=image+0x2c68f98 ||
       !read_local((unsigned char*)worker+0x58,&running,1) || running!=1) return 0;
    s->waited=1;InterlockedIncrement64((volatile LONG64*)&spline_waits);
    ULONGLONG started=GetTickCount64();
    /* The native calculator owns result production. Release its publication
     * lock between reads and allow it to finish; never run or cancel its job.
     * The fixture pathing owner cannot consume this request until we return. */
    while(!s->pending_status[0]) {
        if(GetTickCount64()-started>=timeout) {
            InterlockedIncrement64((volatile LONG64*)&spline_timeouts);return 0;
        }
        Sleep(1);
        if(!spline_pending(s,model,0) || !s->pending_count[0] || s->pending_status[0]>1) return 0;
    }
    return 1;
}

static int spline_state(void *model,void *agent,uint32_t values[4]) {
    void *vtable,*owner;
    if(!read_local(model,&vtable,8) || vtable!=image+SPLINE_ENTRY-0x70 ||
       !read_local((unsigned char*)model+0x10,&owner,8) || owner!=agent ||
       !read_local((unsigned char*)model+0xd0,values,16)) return 0;
    for(unsigned i=0;i<4;i++) {float f;memcpy(&f,values+i,4);if(!isfinite(f)) return 0;}
    return 1;
}

static void spline_observe(flight_fn original,spline_sample *s,void *self,void *agent,
                           void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;s->cheap=cheap;s->applied_cheap=spline_refresh?0:cheap;s->player=player;
    s->waited=0;
    memcpy(&s->dt,&dt,4);memcpy(&s->time,&time,8);
    if(!read_local(self,s->model[0],sizeof(s->model[0])) ||
       !read_local(slot,s->slot[0],sizeof(s->slot[0])) ||
       !read_local((unsigned char*)agent+0x340,s->params,sizeof(s->params)) || !spline_pending(s,self,0))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    if(spline_join && !spline_join_pending(s,self,1000))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    if(spline_refresh) {
        /* Same native eaf660 helper as steering, but spline stores its obstacle
         * publication time at +108 and vector at +f0. Force fresh geometry;
         * native contact membership and all avoidance calculations stay native. */
        *(double*)((unsigned char*)self+0x108)=-1.0;
        InterlockedIncrement64((volatile LONG64*)&spline_refreshes);
    }
    original(self,slot,dt,time,(unsigned char)s->applied_cheap,player);
    if(!read_local(self,s->model[1],sizeof(s->model[1])) ||
       !read_local(slot,s->slot[1],sizeof(s->slot[1])) || !spline_pending(s,self,1))
        InterlockedIncrement64((volatile LONG64*)&spline_failures);
    InterlockedIncrement64((volatile LONG64*)&spline_calls);
}
static void spline_update(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    void *registry_agent;
    if(registry_is_observing() && read_local((unsigned char*)self+0x10,&registry_agent,8)) registry_observe_owner(registry_agent,0xef0,REGISTRY_SPLINE);
    flight_fn original=(flight_fn)(image+SPLINE_UPDATE);
    void *agent=NULL,*model=NULL;
    if(stats.active && boot_phase==3 && read_local((unsigned char*)self+0x10,&agent,8)) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(agent_samples[i].agent==agent && agent_identity(i,agent)) {
            if(!read_local((unsigned char*)agent+0xf10,&model,8) || model!=self) {
                InterlockedIncrement64((volatile LONG64*)&spline_failures);break;
            }
            spline_observe(original,&spline_samples[i],self,agent,slot,dt,time,cheap,player);return;
        }
    }
    world_trace_event *event=world_trace_begin(WT_SPLINE,self,slot,dt,time,cheap,player);
    original(self,slot,dt,time,cheap,player);world_trace_end(event);
}
static int spline_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+SPLINE_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+SPLINE_ENTRY),value,expected);
    VirtualProtect(image+SPLINE_ENTRY,8,old,&ignored);return actual==expected;
}
static int spline_install(int refresh,int join) {
    if(!boot_mode || !agent_installed || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+SPLINE_UPDATE,16379,"bf700855ab64122b1142dcf3fceda6f41ea67318f70186e0b2944d9dd5bdfcac") ||
       (refresh && (!agent_full || !hash_ok(image+0xeaf660,3671,"36298ef739653940d9d234e26cd1250fac0fc0a700b5c85ca955ba685a3ba54b"))) ||
       (join && (!agent_full || !(path_options&1) ||
        !hash_ok(image+0x1099650,629,"8a7a84c0e938f6077cf072946cb6050e594a3a0341c937dd58546e7179ce1ab9") ||
        !hash_ok(image+0xec0cb0,11611,"7e3e1114c607d8ea4717ae26dc7516471ea9aaf3dd0130b761151bda1faa7ffb") ||
        !hash_ok(image+0xeb6760,90,"06cb3e7172633ce43392cdb8921e50a8cf394c9616711b5504b91dce30ee8f73") ||
        !hash_ok(image+0xeb55a0,3420,"4966db7097c45d251774f62b51dc2896cb57157860c3b6a4c0bc21b4e88c4f21"))) ||
       !spline_swap(image+SPLINE_UPDATE,(void*)spline_update)) return 0;
    spline_refresh=refresh;spline_join=join;spline_installed=1;return 1;
}
static void spline_restore(void) {
    if(!spline_installed) return;
    spline_restored=spline_swap((void*)spline_update,image+SPLINE_UPDATE);
    if(spline_restored) spline_installed=0;
}
