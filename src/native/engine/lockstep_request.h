/* Read-only velocity request and enclosing Agent observation. No authority,
 * control values, queues, native flags, timers or transforms are changed. */
#define REQUEST_RVA 0xe5d380u
#define REQUEST_TRAMPOLINE 2496u
#define REQUEST_DISPATCH 2528u
#define REQUEST_OPTION (1ULL<<52)
#define REQUEST_CAPACITY 16384u
#define REQUEST_SCOPE_CAPACITY 2048u
#define REQUEST_QUEUE_CAPACITY 16u
typedef uintptr_t (*request_fn)(void *,double,int,double);
/* lockstep_pilot_inputs.h may own this same entry and thunk slot instead of the
 * observer. The two are exclusive; neither installs while the other owns it. */
static volatile LONG pilot_entry_owner;
static uintptr_t pilot_request(request_fn,void *,double,int,double,uintptr_t);
typedef struct {
    unsigned queue_count[2],queue_capacity[2],unknown_refs,resource_refs;
    unsigned read_bank,write_bank,model_rva,model_present,solver_present;
    /* Queue address-bearing fields become vtable RVA, saved ref, and resource
     * presence. Resource contents remain UNOBSERVED, never lease evidence.
     * All other captured bytes remain strict diagnostic data. */
    unsigned char queue[2][REQUEST_QUEUE_CAPACITY*0x90];
    /* Raw banks retain local references (including +b0/+b8) and padding. They
     * are strict private diagnostics, not portable decoded input equality. */
    unsigned char banks[0x340],params[0x140],model_state[16];
    void *model,*solver; /* Private identity comparisons only; never exported. */
    uintptr_t queue_bounds[2][3]; /* Private race detection, never exported. */
} request_snapshot;
typedef struct {
    uint64_t tick,owner,ordinal;
    unsigned actor,role,valid,requests,model_same,solver_same,other_requests,other_first_caller;
    request_snapshot state[2];
    volatile LONG ready;
} request_scope_event;
typedef struct {
    uint64_t tick,owner,agent_ordinal,ordinal,caller,value,epsilon;
    unsigned actor,role,group,axis,valid;
    unsigned char targets[2][48],dirty[2][2],gains[48],solver_state[20],model_state[16],params[0x140],configuration[16];
    volatile LONG ready;
} request_event;
static request_event request_events[REQUEST_CAPACITY];
static request_scope_event request_scopes[REQUEST_SCOPE_CAPACITY];
static volatile LONG request_count,request_scope_count,request_failures,request_excluded,request_open;
static SRWLOCK request_admission_lock=SRWLOCK_INIT;
static LONG request_peak,request_restore_open;
static int request_closing;
static int request_installed,request_restored;
static uint64_t request_original_word,request_patched_word,request_enabled_tick;
static __thread request_agent_scope *request_current_agent;
typedef struct { uint64_t tick,ordinal;volatile LONG busy; } request_owner;
static request_owner request_owners[12];

static int request_admit(void) {
    int admitted=0;
    AcquireSRWLockExclusive(&request_admission_lock);
    if(request_installed && !request_closing && stats.active && boot_phase==3) {
        ++request_open;if(request_open>request_peak) request_peak=request_open;admitted=1;
    }
    ReleaseSRWLockExclusive(&request_admission_lock);
    return admitted;
}
static void request_leave(void) {
    AcquireSRWLockExclusive(&request_admission_lock);
    if(request_open>0) --request_open;else InterlockedIncrement(&request_failures);
    ReleaseSRWLockExclusive(&request_admission_lock);
}
static unsigned request_role(void) {
    DWORD thread=GetCurrentThreadId();return thread==ui_thread?1:thread==main_thread?2:3;
}
static int request_model_state(void *model,unsigned char out[16]) {
    memset(out,0,16);
    return !model || (read_local((unsigned char*)model+0x68,out,4) &&
        read_local((unsigned char*)model+0xa8,out+4,3) && read_local((unsigned char*)model+0xac,out+8,8));
}
static int request_queue_copy(int (*reader)(const void *,void *,size_t),void *header,request_snapshot *s,unsigned which) {
    uintptr_t bounds[3],again[3];unsigned char verify[REQUEST_QUEUE_CAPACITY*0x90];
    if(which>1 || !reader(header,bounds,24) || bounds[1]<bounds[0] || bounds[2]<bounds[1] ||
       (bounds[0] && (bounds[0]<0x10000 || bounds[0]%16)) || (!bounds[0] && bounds[2]) ||
       (bounds[1]-bounds[0])%0x90 || (bounds[2]-bounds[0])%0x90) return 0;
    uintptr_t count=(bounds[1]-bounds[0])/0x90,capacity=(bounds[2]-bounds[0])/0x90;
    if(count>REQUEST_QUEUE_CAPACITY || capacity>65536) return 0;
    size_t bytes=count*0x90;
    if(bytes && (!reader((void*)bounds[0],s->queue[which],bytes) || !reader((void*)bounds[0],verify,bytes) ||
                 memcmp(s->queue[which],verify,bytes))) return 0;
    if(!reader(header,again,24) || memcmp(bounds,again,24)) return 0;
    s->queue_count[which]=(unsigned)count;s->queue_capacity[which]=(unsigned)capacity;
    memcpy(s->queue_bounds[which],bounds,sizeof(bounds));
    for(unsigned i=0;i<count;i++) {
        unsigned char *row=s->queue[which]+i*0x90;uintptr_t vtable,resource;uint64_t runtime,saved;
        memcpy(&vtable,row,8);memcpy(&runtime,row+0x10,8);memcpy(&resource,row+0x18,8);
        if(vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return 0;
        uint64_t rva=vtable-(uintptr_t)image,present=resource!=0;saved=runtime?world_identity_lookup(runtime):0;
        if(runtime && !saved) ++s->unknown_refs;
        s->resource_refs+=resource!=0;
        memcpy(row,&rva,8);memcpy(row+0x10,&saved,8);memcpy(row+0x18,&present,8);
    }
    return 1;
}
static int request_agent_identity(unsigned actor,void *agent,uint64_t *saved) {
    void *controller=NULL,*owner=NULL,*back=NULL;
    if(actor<1 || actor>12 || !agent_identity(actor-1,agent) || !solver_identity(actor,saved) ||
       !read_local((unsigned char*)actor_components[actor-1]+0x200,&controller,8) ||
       !read_local((unsigned char*)controller+0x28,&owner,8) || owner!=actor_components[actor-1] ||
       !read_local((unsigned char*)controller+0x40,&back,8) || back!=agent ||
       !read_local((unsigned char*)agent+0xef0,&owner,8) || owner!=actor_components[actor-1]) return 0;
    for(unsigned i=0;i<12;i++) if(i+1!=actor && actor_components[i] && actor_components[i]==owner) return 0;
    return owner!=player_observer_object;
}
static int request_snapshot_read(unsigned actor,void *agent,request_snapshot *s) {
    unsigned char *a=agent;void *component=NULL,*model_owner=NULL;uintptr_t vtable=0;uint64_t saved=0;
    if(!request_agent_identity(actor,agent,&saved) ||
       !read_local(a+0xef0,&component,8) || component!=actor_components[actor-1] ||
       !read_local(image+0x38f4ee4,&s->read_bank,4) || !read_local(image+0x2f4b6f4,&s->write_bank,4) ||
       s->read_bank>1 || s->write_bank>1 || s->read_bank+s->write_bank!=1 ||
       !read_local(a+0xf10,&s->model,8) || !read_local(a+0xf80,&s->solver,8) ||
       !read_local(a+0xad0,s->banks,sizeof(s->banks)) || !read_local(a+0x340,s->params,sizeof(s->params))) return 0;
    s->model_present=s->model!=NULL;s->solver_present=s->solver!=NULL;
    if(s->model) {
        if(!read_local(s->model,&vtable,8) || !read_local((unsigned char*)s->model+0x10,&model_owner,8) || model_owner!=agent ||
           vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return 0;
        s->model_rva=(unsigned)(vtable-(uintptr_t)image);
        /* Do not interpret a different native model's fields as Steering. */
        if(s->model_rva==FLIGHT_ENTRY-0x70 && !request_model_state(s->model,s->model_state)) return 0;
    }
    if(!request_queue_copy(read_local,a+0x6a8,s,0) || !request_queue_copy(read_local,a+0x6c0,s,1)) return 0;
    for(unsigned i=0;i<2;i++) {
        uintptr_t recheck[3];
        if(!read_local(a+(i?0x6c0:0x6a8),recheck,sizeof(recheck)) || memcmp(recheck,s->queue_bounds[i],sizeof(recheck))) return 0;
    }
    unsigned read,write;
    return read_local(image+0x38f4ee4,&read,4) && read_local(image+0x2f4b6f4,&write,4) &&
        read==s->read_bank && write==s->write_bank && request_agent_identity(actor,agent,&saved);
}
static void request_agent_begin(request_agent_scope *s,unsigned actor,void *agent) {
    memset(s,0,sizeof(*s));
    if(!request_admit()) return;
    /* Even excluded/refused nested scopes shadow the parent. Their requests
     * must never be attributed to the outer saved owner. */
    s->previous=request_current_agent;s->agent=agent;s->actor=actor;
    s->tick=stats.issued;s->thread=GetCurrentThreadId();s->active=1;request_current_agent=s;
    if(actor<1 || actor>12) {InterlockedIncrement(&request_excluded);return;}
    request_owner *owner=&request_owners[actor-1];uint64_t saved=0;
    if(InterlockedCompareExchange(&owner->busy,1,0)) {InterlockedIncrement(&request_failures);return;}
    LONG n=InterlockedIncrement(&request_scope_count);
    if(n<1 || n>(LONG)REQUEST_SCOPE_CAPACITY || !request_agent_identity(actor,agent,&saved)) {
        InterlockedIncrement(&request_failures);InterlockedExchange(&owner->busy,0);return;
    }
    if(owner->tick!=stats.issued) {owner->tick=stats.issued;owner->ordinal=0;}
    request_scope_event *e=&request_scopes[n-1];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->owner=saved;e->ordinal=++owner->ordinal;e->actor=actor;e->role=request_role();
    e->valid=request_snapshot_read(actor,agent,&e->state[0]);
    if(!e->valid) InterlockedIncrement(&request_failures);
    s->record=(unsigned)n;
}
static void request_agent_end(request_agent_scope *s) {
    if(!s->active) return;
    int balanced=request_current_agent==s && s->thread==GetCurrentThreadId() && s->tick==stats.issued;
    if(!balanced) {InterlockedIncrement(&request_failures);request_current_agent=NULL;}
    else request_current_agent=s->previous;
    if(!s->record) {s->active=0;request_leave();return;}
    request_scope_event *e=&request_scopes[s->record-1];e->valid&=balanced;
    uint64_t saved=0;
    e->valid &= solver_identity(s->actor,&saved) && saved==e->owner && request_snapshot_read(s->actor,s->agent,&e->state[1]);
    e->requests=s->requests;e->model_same=e->state[0].model==e->state[1].model;e->solver_same=e->state[0].solver==e->state[1].solver;
    if(!e->valid) InterlockedIncrement(&request_failures);
    InterlockedExchange(&e->ready,1);InterlockedExchange(&request_owners[s->actor-1].busy,0);
    s->active=0;request_leave();
}
static int request_site(uintptr_t caller,unsigned *group,unsigned *axis) {
    static const uintptr_t sites[]={0xec91cc,0xec91ee,0xec9210,0xec9230,0xec9252,0xec9274};
    for(unsigned i=0;i<6;i++) if(caller==sites[i]) {*group=i/3;*axis=i%3;return 1;}
    return 0;
}
static int request_targets(void *self,unsigned char target[48],unsigned char dirty[2]) {
    return read_local((unsigned char*)self+0x36d8,target,48) &&
        read_local((unsigned char*)self+0x3840,dirty,1) && read_local((unsigned char*)self+0x3848,dirty+1,1);
}
static int request_context(request_event *e,void *solver,void *model,void *agent) {
    static const unsigned offsets[]={0x1c,0x1d,0x22,0x23,0x24,0x31,0x55b0,0x118a8,0xa0d0,0x163a0};
    unsigned char *p=solver;void *config=NULL;
    for(unsigned i=0;i<10;i++) if(!read_local(p+offsets[i],e->solver_state+i,1)) return 0;
    return read_local(p+0x48,e->solver_state+12,4) && read_local(p+0x50,e->solver_state+16,4) &&
        read_local(p+0xa258,e->gains,24) && read_local(p+0x16528,e->gains+24,24) &&
        request_model_state(model,e->model_state) && read_local((unsigned char*)agent+0x340,e->params,sizeof(e->params)) &&
        read_local(image+0x6cf1538,&config,8) && config && read_local((unsigned char*)config+0x5054,e->configuration,8) &&
        read_local((unsigned char*)config+0x5094,e->configuration+8,4) &&
        read_local(image+0x3d19b15,e->configuration+12,1) && read_local(image+0x3d19b24,e->configuration+13,1);
}
static uintptr_t request_observe_admitted(request_fn original,void *self,double value,int axis,double epsilon,uintptr_t caller) {
    unsigned group=0,expected_axis=0;
    if(!request_site(caller,&group,&expected_axis)) {
        request_agent_scope *scope=request_current_agent;
        if(scope && scope->active && scope->record && scope->tick==stats.issued && scope->thread==GetCurrentThreadId()) {
            request_scope_event *e=&request_scopes[scope->record-1];
            if(!e->other_requests) e->other_first_caller=caller<0x6fa9000?(unsigned)caller:UINT32_MAX;
            ++e->other_requests;
        }
        InterlockedIncrement(&request_excluded);return original(self,value,axis,epsilon);
    }
    solver_scope *flight=solver_current_scope;request_agent_scope *agent=request_current_agent;
    if(!flight || flight->kind!=2 || flight->actor<1 || flight->actor>12) {
        InterlockedIncrement(&request_excluded);return original(self,value,axis,epsilon);
    }
    unsigned char *solver=NULL;void *actual_model=NULL,*model_vtable=NULL;unsigned owner=0;uint64_t saved=0;
    int valid=flight->active && flight->thread==GetCurrentThreadId() && flight->tick==stats.issued &&
        agent && agent->active && agent->record && agent->actor==flight->actor && agent->agent==flight->agent &&
        agent->tick==stats.issued && agent->thread==GetCurrentThreadId() &&
        read_local((unsigned char*)flight->agent+0xf80,&solver,8) && solver &&
        solver_claim(solver,&owner,&saved)==1 && owner==flight->actor &&
        request_agent_identity(owner,flight->agent,&saved) &&
        read_local((unsigned char*)flight->agent+0xf10,&actual_model,8) && actual_model==flight->self &&
        read_local(actual_model,&model_vtable,8) && model_vtable==image+FLIGHT_ENTRY-0x70 &&
        flight->self==flight_samples[owner-1].model && flight->agent==flight_samples[owner-1].agent &&
        self==solver+(group?0x6a00:0x12cd0) && axis==(int)expected_axis && isfinite(value) && isfinite(epsilon);
    if(!valid) {InterlockedIncrement(&request_failures);return original(self,value,axis,epsilon);}
    LONG n=InterlockedIncrement(&request_count);
    if(n<1 || n>(LONG)REQUEST_CAPACITY) {InterlockedIncrement(&request_failures);return original(self,value,axis,epsilon);}
    request_event *e=&request_events[n-1];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->owner=saved;e->actor=owner;e->role=request_role();e->caller=caller;
    e->group=group;e->axis=(unsigned)axis;e->agent_ordinal=request_scopes[agent->record-1].ordinal;e->ordinal=++agent->requests;
    memcpy(&e->value,&value,8);memcpy(&e->epsilon,&epsilon,8);
    e->valid=request_targets(self,e->targets[0],e->dirty[0]) && request_context(e,solver,flight->self,flight->agent);
    uintptr_t result=original(self,value,axis,epsilon); /* Exactly once, all four args unchanged. */
    unsigned checked_owner=0;uint64_t checked_saved=0;
    e->valid &= request_targets(self,e->targets[1],e->dirty[1]) && solver_claim(solver,&checked_owner,&checked_saved)==1 &&
        checked_owner==owner && checked_saved==saved && request_agent_identity(owner,flight->agent,&checked_saved) &&
        checked_saved==saved && read_local((unsigned char*)flight->agent+0xf10,&actual_model,8) && actual_model==flight->self &&
        request_current_agent==agent && solver_current_scope==flight;
    if(!e->valid) InterlockedIncrement(&request_failures);
    InterlockedExchange(&e->ready,1);return result;
}
static uintptr_t request_observe(request_fn original,void *self,double value,int axis,double epsilon,uintptr_t caller) {
    /* Admission precedes caller/owner selection, including unobserved originals.
     * Restoration closes this same lock before checking the open count. It
     * never waits for a native call, and trampoline storage stays resident. */
    if(!request_admit()) return original(self,value,axis,epsilon);
    uintptr_t result=request_observe_admitted(original,self,value,axis,epsilon,caller);
    request_leave();return result;
}
static uintptr_t request_dispatch(void *self,double value,int axis,double epsilon) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    if(InterlockedCompareExchange(&pilot_entry_owner,0,0))
        return pilot_request((request_fn)(thunk+REQUEST_TRAMPOLINE),self,value,axis,epsilon,caller);
    return request_observe((request_fn)(thunk+REQUEST_TRAMPOLINE),self,value,axis,epsilon,caller);
}
/* Retained as the independent actual-image ABI emitter used by the test harness. */
static __attribute__((unused)) int request_emit_thunks(unsigned char *trampoline,unsigned char *dispatch,const unsigned char *entry,void *target) {
    static const unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08};
    if(memcmp(entry,prefix,5)) return 0;
    memcpy(trampoline,entry,5);trampoline[5]=0xff;trampoline[6]=0x25;memset(trampoline+7,0,4);
    const void *continuation=entry+5;memcpy(trampoline+11,&continuation,8);
    dispatch[0]=0xff;dispatch[1]=0x25;memset(dispatch+2,0,4);memcpy(dispatch+6,&target,8);return 1;
}
static int request_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+REQUEST_RVA,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t found=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+REQUEST_RVA),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+REQUEST_RVA,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+REQUEST_RVA,8);return found==expected;
}
static void request_restore(void) {
    AcquireSRWLockExclusive(&request_admission_lock);
    request_closing=1;
    if(request_open>request_restore_open) {
        request_restore_open=request_open;InterlockedIncrement(&request_failures);
    }
    if(request_installed) {
        request_restored=request_swap(request_patched_word,request_original_word);
        if(request_restored) request_installed=0;
    }
    ReleaseSRWLockExclusive(&request_admission_lock);
}
static int request_pins(void);
static int request_install(void) {
    if(!(boot_options&REQUEST_OPTION) || !boot_mode || !stats.active || stats.issued!=1 || boot_phase!=3 ||
       !solver_installed || !agent_installed || !flight_installed || !path_installed || !(path_options&1) ||
       request_enabled_tick || GetCurrentThreadId()!=ui_thread || InterlockedCompareExchange(&pilot_entry_owner,0,0) ||
       !request_pins()) return 0;
    intptr_t distance=(intptr_t)(thunk+REQUEST_DISPATCH)-(intptr_t)(image+REQUEST_RVA+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(2496)) return 0;
    memcpy(&request_original_word,image+REQUEST_RVA,8);request_patched_word=request_original_word;
    unsigned char *p=(unsigned char*)&request_patched_word;p[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(p+1,&rel,4);
    if(!request_swap(request_original_word,request_patched_word)) return 0;
    AcquireSRWLockExclusive(&request_admission_lock);
    request_installed=1;request_closing=0;request_enabled_tick=stats.issued;
    ReleaseSRWLockExclusive(&request_admission_lock);return 1;
}

static int tsnap_unpatched_hash(unsigned rva,ULONG length,const char *expected);
static int request_pins(void) {
    /* Agent update holds the travel snapshot's one-byte read patch; pin its pre-patch bytes. */
    return hash_ok(image+0x00e5d380,214,"ef39acf755eaecacd63bb4b33fe48ad93094952929135412787ef0efe0b3b4b4") &&
        hash_ok(image+0x01a051ac,104,"be7f7a85a2ed85a1e43da3c16eae7855355364090586e265546c95748cae83b0") &&
        hash_ok(image+0x00ec7200,10109,"bff420d07fb655ff195560b395af5decc0ab5b2ecf9691481e578eb3282eb796") &&
        hash_ok(image+0x00e95fb0,2048,"c72b49324fed4c46ad78a1b40c100ec7c710e9ae636e6f5cfe6ccd19d258e20d") &&
        hash_ok(image+0x00ecce50,189,"29a4f53de85b2366df267d731d0aa30e47e8d2b97972b627161628a3d23ed4eb") &&
        hash_ok(image+0x00eccf10,614,"b200de0820044acdca15139e69ef7b73c49665c3763173fdd5ca6e1c727333a4") &&
        hash_ok(image+0x00e88160,4760,"0e48033bdb74b55f24f2a490504340d445cc70b07c8606105c97778b99bf84ab") &&
        tsnap_unpatched_hash(0x00e8f250,10613,"ae0d8b729a39a75f83b1c3d783585f587de1cc2e5eff846620f2f0c482504f52") &&
        hash_ok(image+0x00e8dfc0,4180,"3ceb3030f466a8d7346de9359a97b67e0fc1098593d0ed14188b434527b2306c") &&
        hash_ok(image+0x00e8d850,1892,"e32a6ed0c9172b1d4c28fdc3880a0b1aff3b7678e96e9d47fa0c0b765f6efa79") &&
        hash_ok(image+0x00ec91c7,5,"63d718a279939ef0f627d716dc194730c66e61ce05bb42bb382696d27046fe2e") &&
        hash_ok(image+0x00ec91e9,5,"c01b130a5c7534d91ce911815a4170b2e65f81a382cc79afff72100623a729c3") &&
        hash_ok(image+0x00ec920b,5,"a758f665596b9690079ab7f214579f51486195e394cfac6917b974c60f497cf8") &&
        hash_ok(image+0x00ec922b,5,"e147e23010699a356a075bdf80b5ce746b8cb7a83b9b9b64e05fe4d6a46aff32") &&
        hash_ok(image+0x00ec924d,5,"e9e7351993e36ff6bd05550b036968b03c6d572a1c35e352958ddd2ff391e973") &&
        hash_ok(image+0x00ec926f,5,"a8ce9a44abb9c4d5dd86e745bba614dcf005ac1755f07bf185c80f593a27f903") &&
        hash_ok(image+0x02cbe470,8,"244daf676b076b34d5be3f723196f6a9a2fa7e81a05c781394a14fa645e1d6d8");
}
