static uintptr_t *test_queue_bounds;
static unsigned char *test_queue_bytes;
static unsigned test_queue_mutation,test_queue_reads;
static unsigned test_world_ai_calls;
static double test_world_config_result;
static const char *test_world_config_text="0000000000000063";
static double test_world_config_number(lua_State *L,int index) {(void)L;assert(index==2);return 12;}
static const char *test_world_config_string(lua_State *L,int index,size_t *length) {
    (void)L;assert(index==1);*length=strlen(test_world_config_text);return test_world_config_text;
}
static void test_world_config_push(lua_State *L,double number) {(void)L;test_world_config_result=number;}
static void test_world_config_retry(void) {
    double (*old_number)(lua_State *,int)=p_lua_tonumber;
    const char *(*old_string)(lua_State *,int,size_t *)=p_lua_tolstring;
    void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    p_lua_tonumber=test_world_config_number;p_lua_tolstring=test_world_config_string;p_lua_pushnumber=test_world_config_push;
    DWORD old_ui=ui_thread;int old_phase=boot_phase;ui_thread=0;boot_phase=2;
    assert(!accept_ui());
    assert(lua_world_trace_config(NULL)==1 && test_world_config_result==1);
    test_world_config_text="0000000000000064";
    assert(lua_world_trace_config(NULL)==1 && test_world_config_result==0);
    test_world_config_text="0000000000000063";world_trace_early=0;
    assert(lua_world_trace_config(NULL)==1 && test_world_config_result==0);
    world_trace_early=1;ui_thread=old_ui;boot_phase=old_phase;
    p_lua_tonumber=old_number;p_lua_tolstring=old_string;p_lua_pushnumber=old_push;
}
static void test_world_ai_original(void *exec) {++test_world_ai_calls;++*(uint64_t*)((unsigned char*)exec+0x18);}
static int test_world_trace_queue_read(const void *source,void *dest,size_t bytes) {
    int result=read_local(source,dest,bytes);
    if(result && source==test_queue_bytes && ++test_queue_reads==1) {
        if(test_queue_mutation==1) ++test_queue_bounds[1];
        if(test_queue_mutation==2) test_queue_bytes[0x20]^=1;
    }
    return result;
}
static void test_world_trace_queues(void) {
    __attribute__((aligned(16))) unsigned char data[0x120]={0};
    uintptr_t bounds[3]={(uintptr_t)data,(uintptr_t)data+0x90,(uintptr_t)data+sizeof(data)};
    world_trace_snapshot sample={0};*(void**)data=image+0x1234;
    assert(world_trace_queue_copy(read_local,bounds,&sample,0));
    assert(sample.queue_count[0]==1 && sample.queue_capacity[0]==2);
    assert(*(unsigned*)sample.queue_refs[0]==0x1234 && !memcmp(sample.queue[0],data,0x90));
    uintptr_t empty[3]={0};assert(world_trace_queue_copy(read_local,empty,&sample,1) && !sample.queue_count[1]);
    test_queue_bounds=bounds;test_queue_bytes=data;
    for(unsigned mode=1;mode<=2;mode++) {
        test_queue_mutation=mode;test_queue_reads=0;memset(&sample,0,sizeof(sample));
        assert(!world_trace_queue_copy(test_world_trace_queue_read,bounds,&sample,0));
        bounds[1]=(uintptr_t)data+0x90;
    }
    uintptr_t invalid[][3]={{1,0,0},{0,0,0x90},{(uintptr_t)data,(uintptr_t)data+1,(uintptr_t)data+0x90},
        {(uintptr_t)data,(uintptr_t)data+17*0x90,(uintptr_t)data+17*0x90},
        {(uintptr_t)data,(uintptr_t)data+0x90,(uintptr_t)data+65537*0x90}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) assert(!world_trace_queue_copy(read_local,invalid[i],&sample,0));
    *(uint64_t*)(data+0x10)=0xfedcba9876543210ULL;memset(&sample,0,sizeof(sample));
    assert(!world_trace_queue_copy(read_local,bounds,&sample,0) && sample.queue_unknown_refs==1);
    assert(!world_identities);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0xfedcba9876543210ULL,77));
    memset(&sample,0,sizeof(sample));assert(world_trace_queue_copy(read_local,bounds,&sample,0));
    uint64_t saved;memcpy(&saved,sample.queue_refs[0]+4,8);assert(saved==77 && !sample.queue_unknown_refs);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
}
static DWORD WINAPI test_world_trace_worker(void *context) {
    unsigned char tls[0x320]={0};world_trace_watch *w=context;
    world_trace_event *event=world_trace_begin_context(WT_AGENT,w->agent,NULL,0.25f,12.0,1,0,tls);
    assert(event);world_trace_end(event);return 0;
}
static void test_world_trace(void) {
    test_world_trace_queues();
    unsigned char object[0x208]={0},controller[0xe0]={0},agent[0xf20]={0};
    unsigned char model[0x3a0]={0},tls[0x320]={0},manager[0x220]={0};
    *(uint64_t*)(object+8)=700;*(void**)(object+0x200)=controller;
    *(void**)controller=image+0x2c51158;*(void**)(controller+0x28)=object;
    *(void**)(controller+0x30)=agent+0xad0;*(void**)(controller+0x40)=agent;
    *(void**)(agent+0xef0)=object;*(void**)(agent+0xf10)=model;
    *(void**)model=image+0x2c4e9c8;*(void**)(model+0x10)=agent;
    *(void**)(image+0x6cf21c8)=manager;*(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    *(uint64_t*)(tls+0x318)=123;*(double*)(agent+0xac0)=6.25;
    world_trace_watches[0]=(world_trace_watch){.saved=99,.runtime=700,.object=object,.controller=controller,.agent=agent};
    world_trace_watch_count=1;
    assert(!world_trace_prepare(1));assert(world_trace_prepare(12));assert(!world_trace_prepare(12));
    stats.active=1;stats.issued=2;boot_phase=3;ui_thread=GetCurrentThreadId();
    unsigned char unchanged[sizeof(agent)];memcpy(unchanged,agent,sizeof(agent));
    world_trace_event *outer=world_trace_begin_context(WT_SHIP,object,NULL,0,0,0,0,tls);
    assert(outer && !outer->complete && !outer->errors && !outer->parent);
    world_trace_event *inner=world_trace_begin_context(WT_STEERING,model,agent+0xad0,0.5f,3.0,1,0,tls);
    assert(inner && inner->parent==1 && !inner->errors);
    assert(!memcmp(unchanged,agent,sizeof(agent)) && *(uint64_t*)(tls+0x318)==123);
    /* The sole simulated engine effect occurs between observer calls. */
    *(float*)(agent+0xad0)=4.5f;*(uint64_t*)(tls+0x318)=456;
    world_trace_end(inner);assert(inner->complete && !world_trace_ready());
    world_trace_end(outer);assert(world_trace_ready()==2 && !world_trace_failures && !TlsGetValue(world_trace_tls));
    assert(inner->before.rng==123 && inner->after.rng==456 && outer->after.rng==456);
    assert(*(float*)inner->before.banks==0 && *(float*)inner->after.banks==4.5f);
    assert(outer->enter<inner->enter && inner->leave<outer->leave);
    assert(inner->before.read==0 && inner->before.write==1 && inner->before.model_length==0x3a0);
    assert(*(double*)inner->before.agent_timer==6.25 && inner->cheap==1 && inner->dt==0x3f000000);
    /* Two workers reserve disjoint records and retain independent parent TLS. */
    HANDLE workers[2];for(unsigned i=0;i<2;i++) {workers[i]=CreateThread(NULL,0,test_world_trace_worker,&world_trace_watches[0],0,NULL);assert(workers[i]);}
    assert(WaitForMultipleObjects(2,workers,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<2;i++) CloseHandle(workers[i]);
    assert(world_trace_ready()==4 && !world_trace_failures);
    for(unsigned i=2;i<4;i++) assert(!world_trace_events[i].parent && !world_trace_events[i].ui);
    /* Linear's allocation is only 0x270. A guard page makes a larger model read
     * fail rather than accidentally passing on neighboring readable memory. */
    unsigned char *linear_pages=VirtualAlloc(NULL,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(linear_pages);DWORD old;assert(VirtualProtect(linear_pages+4096,4096,PAGE_NOACCESS,&old));
    unsigned char *linear=linear_pages+4096-0x270;
    *(void**)linear=image+0x2c4f610;*(void**)(linear+0x10)=agent;*(void**)(agent+0xf10)=linear;
    outer=world_trace_begin_context(WT_AGENT,agent,NULL,0,0,0,0,tls);assert(outer);
    world_trace_end(outer);assert(!outer->errors && outer->before.model_length==0x270 && world_trace_ready()==5);
    assert(!world_trace_layout_pins()); /* fake image cannot authorize live layout reads */
    *(void**)(agent+0xf10)=model;VirtualFree(linear_pages,0,MEM_RELEASE);
    outer=world_trace_begin_context(WT_SHIP,object,NULL,0,0,0,0,tls);assert(outer);
    *(uint64_t*)(object+8)=701;world_trace_end(outer);assert(outer->errors==4 && world_trace_failures==1);
    *(uint64_t*)(object+8)=700;
    outer=world_trace_begin_context(WT_AGENT,agent,NULL,0,0,0,0,NULL);assert(outer);
    world_trace_end(outer);assert(outer->errors==6 && world_trace_failures==2);
    unsigned char context[0x128]={0},script[0x30]={0},exec[0x50]={0};
    memcpy(script+0x10,"patrol.test",11);*(uint64_t*)(script+0x20)=11;*(uint64_t*)(script+0x28)=15;
    *(void**)(context+0xd8)=script;*(void**)(context+0x120)=object;*(uint64_t*)(exec+0x18)=7;
    world_trace_early=1;
    outer=world_trace_begin_context(WT_AI,object,NULL,0,0,0,0,tls);assert(outer);
    world_trace_ai_execute(test_world_ai_original,outer,object,exec,context,(uintptr_t)(image+0x106e3fb));
    assert(!outer->errors && outer->complete && test_world_ai_calls==1 && outer->pc_before==7 && outer->pc_after==8);
    assert(!strcmp(outer->script,"patrol.test") && outer->caller==0x106e3fb && outer->before.rng==outer->after.rng);
    world_trace_ai_execute(test_world_ai_original,NULL,NULL,exec,context,0);assert(test_world_ai_calls==2);
    assert(world_trace_manifest("0000000000000063",16,12));
    assert(!world_trace_manifest("0000000000000064",16,12));
    assert(!world_trace_manifest("0000000000000063,0000000000000063",33,12));
    assert(!world_trace_manifest("0000000000000000",16,12));
    assert(!world_trace_manifest("0000000000000063",16,2));
    test_world_config_retry();
    world_trace_watch retained=world_trace_watches[0];
    world_trace_watches[0].object=NULL;world_trace_watches[0].runtime=0;boot_phase=2;
    world_trace_import_capture((void*)1,700,99); /* incomplete object is never dereferenced */
    assert(world_trace_watches[0].object==(void*)1 && world_trace_imports[0]==1 && !world_trace_early_failures);
    world_trace_import_capture((void*)2,700,99);assert(world_trace_early_failures==1 && world_trace_watches[0].object==(void*)1);
    world_trace_watches[0]=retained;world_trace_imports[0]=0;world_trace_early_failures=0;boot_phase=3;
    assert(!world_trace_bind()); /* post-admission late binding is refused */
    world_trace_early=0;world_trace_early_stage="off";
    test_world_queue(object,agent,tls);
    unsigned count=(unsigned)world_trace_count;stats.issued=13;
    assert(!world_trace_begin_context(WT_SHIP,object,NULL,0,0,0,0,tls) && world_trace_count==count);
    stats.issued=2;InterlockedExchange64(&world_trace_count,WORLD_TRACE_MAX);
    assert(!world_trace_begin_context(WT_SHIP,object,NULL,0,0,0,0,tls) && world_trace_dropped==1);
    InterlockedExchange(&world_trace_enabled,0);
    assert(!world_trace_begin_context(WT_SHIP,object,NULL,0,0,0,0,tls));
    /* Test owns this allocation; production intentionally retains it to exit. */
    VirtualFree(world_trace_events,0,MEM_RELEASE);world_trace_events=NULL;TlsFree(world_trace_tls);world_trace_tls=TLS_OUT_OF_INDEXES;
    world_trace_watch_count=world_trace_last_tick=0;world_trace_count=world_trace_order=world_trace_failures=world_trace_dropped=0;
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;
}
