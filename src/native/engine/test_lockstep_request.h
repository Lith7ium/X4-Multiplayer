static unsigned char test_request_prefix[10]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
static void *test_request_self;
static double test_request_value,test_request_epsilon;
static int test_request_axis;
static unsigned test_request_calls,test_request_alignment;
static uintptr_t test_request_caller;
static request_fn test_request_trampoline;
static void test_request_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE);
    assert(dos.e_lfanew>0 && dos.e_lfanew<4096 && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.Signature==IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.FileHeader.TimeDateStamp==0x6a1eaded &&
        nt.OptionalHeader.SizeOfImage==0x6fa9000 && nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64));
    unsigned offset=0;
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),f)==sizeof(section));
        if(REQUEST_RVA>=section.VirtualAddress && REQUEST_RVA+214<=section.VirtualAddress+section.SizeOfRawData)
            offset=section.PointerToRawData+REQUEST_RVA-section.VirtualAddress;
    }
    unsigned char actual[214];assert(offset && !fseek(f,(long)offset,SEEK_SET) && fread(actual,1,214,f)==214);fclose(f);
    assert(hash_ok(actual,214,"ef39acf755eaecacd63bb4b33fe48ad93094952929135412787ef0efe0b3b4b4"));
    unsigned char trampoline[32],dispatch[32];void *continuation=NULL;
    assert(request_emit_thunks(trampoline,dispatch,actual,(void*)test_request_native_image));
    memcpy(&continuation,trampoline+11,8);assert(continuation==actual+5 && !memcmp(trampoline,actual,5));
    assert(!memcmp(actual,test_request_prefix,10));memcpy(test_request_prefix,actual,10);
    puts("pinned native image request prefix/emitter/continuation verified (5 bytes)");
}
static uintptr_t test_request_original(void *self,double value,int axis,double epsilon) {
    assert(self==test_request_self && !memcmp(&value,&test_request_value,8) && axis==test_request_axis &&
        !memcmp(&epsilon,&test_request_epsilon,8));
    ++test_request_calls;double result=value+9;
    memcpy((unsigned char*)self+0x36d8,&result,8);return 0x12345678abcdef01ULL;
}
static uintptr_t test_request_hook(void *self,double value,int axis,double epsilon) {
    return request_observe(test_request_trampoline,self,value,axis,epsilon,test_request_caller);
}
static void test_request_abi(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);
    memcpy(code,test_request_prefix,10);unsigned n=10;
    /* Execute both actual native shadow-space stores. The relocated first store
     * runs once, continuation executes the second; preserve mixed register args. */
    unsigned char alignment[]={0x48,0x83,0xec,0x28,0x48,0x89,0xe0,0x83,0xe0,0x0f,0x48,0xbb};
    memcpy(code+n,alignment,sizeof(alignment));n+=sizeof(alignment);
    void *address=&test_request_alignment;memcpy(code+n,&address,8);n+=8;
    unsigned char call[]={0x89,0x03,0x48,0xb8};memcpy(code+n,call,sizeof(call));n+=sizeof(call);
    address=(void*)test_request_original;memcpy(code+n,&address,8);n+=8;
    unsigned char tail[]={0xff,0xd0,0x48,0x83,0xc4,0x28,0x48,0x8b,0x5c,0x24,0x08,0xc3};memcpy(code+n,tail,sizeof(tail));
    thunk_recipe recipe=*thunk_recipe_at(2496);recipe.target=(void*)test_request_hook;
    assert(thunk_emit_slot_at(code+256,&recipe,code));
    test_request_trampoline=(request_fn)(code+256);uint64_t original;memcpy(&original,code,8);
    code[0]=0xe9;int32_t distance=283;memcpy(code+1,&distance,4);FlushInstructionCache(GetCurrentProcess(),code,4096);
    unsigned before=test_request_calls;test_request_alignment=99;
    assert(((request_fn)code)(test_request_self,test_request_value,test_request_axis,test_request_epsilon)==0x12345678abcdef01ULL);
    assert(test_request_calls==before+1 && test_request_alignment==0 && !memcmp(code+5,(unsigned char*)&original+5,3));
    code[0]=0;assert(!request_emit_thunks(code+256,code+288,code,(void*)test_request_hook));
    VirtualFree(code,0,MEM_RELEASE);
}
static DWORD WINAPI test_request_thread(LPVOID unused) {(void)unused;assert(!request_current_agent && !solver_current_scope);return 0;}
static HANDLE test_request_entered,test_request_continue;
static uintptr_t test_request_blocked_original(void *self,double value,int axis,double epsilon) {
    uintptr_t result=test_request_original(self,value,axis,epsilon);
    assert(SetEvent(test_request_entered));
    assert(WaitForSingleObject(test_request_continue,10000)==WAIT_OBJECT_0);return result;
}
static DWORD WINAPI test_request_stop_worker(LPVOID mode) {
    if(mode) {
        /* Controlled preemption after the production admission lock but before
         * caller selection or record reservation. */
        assert(request_admit());assert(SetEvent(test_request_entered));
        assert(WaitForSingleObject(test_request_continue,10000)==WAIT_OBJECT_0);
        assert(request_observe_admitted(test_request_original,test_request_self,test_request_value,
            test_request_axis,test_request_epsilon,0)==0x12345678abcdef01ULL);
        request_leave();
    } else assert(request_observe(test_request_blocked_original,test_request_self,test_request_value,
        test_request_axis,test_request_epsilon,0)==0x12345678abcdef01ULL);
    return 0;
}
static void test_request_concurrent_stop(void) {
    for(unsigned mode=0;mode<2;mode++) {
        request_installed=stats.active=1;boot_phase=3;request_closing=0;
        request_original_word=123;request_patched_word=456;*(uint64_t*)(image+REQUEST_RVA)=456;
        test_request_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_request_continue=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(test_request_entered && test_request_continue);
        unsigned calls=test_request_calls;
        HANDLE worker=CreateThread(NULL,0,test_request_stop_worker,(void*)(uintptr_t)mode,0,NULL);assert(worker);
        assert(WaitForSingleObject(test_request_entered,10000)==WAIT_OBJECT_0);
        assert(request_open==1 && request_peak==1 && !request_count && !request_scope_count);
        request_restore();
        assert(!request_installed && request_restored && request_restore_open==1 && request_failures==1);
        assert(!request_admit());
        assert(request_observe(test_request_original,test_request_self,test_request_value,
            test_request_axis,test_request_epsilon,0)==0x12345678abcdef01ULL);
        assert(!request_count && request_open==1);
        assert(SetEvent(test_request_continue));assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
        assert(!request_open && request_restore_open==1 && request_excluded==1 && test_request_calls==calls+2);
        request_restore();assert(request_restore_open==1 && request_failures==1);
        CloseHandle(worker);CloseHandle(test_request_entered);CloseHandle(test_request_continue);
        request_closing=request_restored=request_peak=request_restore_open=request_failures=request_excluded=0;
        stats.active=0;boot_phase=0;
    }
}
static size_t test_request_export_length;
static double test_request_export_number(lua_State *L,int index) {(void)L;return index==1?1:0;}
static void test_request_export_string(lua_State *L,const char *value,size_t size) {
    (void)L;assert(value && size<16384);test_request_export_length=size;
    assert(strstr(value," main=16 ") && strstr(value," pending=16 ") && strstr(value," pending_data="));
    assert(size==strlen(value));
}
static void test_request_export_bounds(void) {
    char hex_out[8];const unsigned char byte=0xab;
    memset(hex_out,0xa5,sizeof(hex_out));
    assert(request_hex(hex_out,6,0,"x",&byte,1)==5 && !strcmp(hex_out," x=ab") && (unsigned char)hex_out[6]==0xa5);
    assert(request_hex(hex_out,5,0,"x",&byte,1)==-1);
    assert(request_hex(hex_out,sizeof(hex_out),-1,"x",&byte,1)==-1);
    assert(request_hex(hex_out,sizeof(hex_out),sizeof(hex_out),"x",&byte,1)==-1);
    assert(request_hex(hex_out,sizeof(hex_out),0,"x",&byte,UINT32_MAX)==-1);
    double (*old_number)(lua_State*,int)=p_lua_tonumber;
    void (*old_string)(lua_State*,const char*,size_t)=p_lua_pushlstring;
    p_lua_tonumber=test_request_export_number;p_lua_pushlstring=test_request_export_string;
    request_scope_event *e=&request_scopes[0];memset(e,0xff,sizeof(*e));
    e->state[0].queue_count[0]=e->state[0].queue_count[1]=16;e->ready=1;
    request_scope_count=1;request_enabled_tick=1;ui_thread=GetCurrentThreadId();
    assert(lua_player_request_scope(NULL)==1 && test_request_export_length>11000 && test_request_export_length<13000);
    p_lua_tonumber=old_number;p_lua_pushlstring=old_string;request_enabled_tick=0;
}
static request_agent_scope *test_request_outer_agent;
static solver_scope *test_request_outer_flight;
static void *test_request_unknown_self;
static unsigned test_request_fallback_calls;
static void test_request_agent_fallback_original(void *self,void *commands,float dt,double time,unsigned char cheap) {
    assert(self==test_request_unknown_self && commands==(void*)0x123456 && dt==0.25f && time==10.5 && cheap==1);
    assert(request_current_agent && request_current_agent!=test_request_outer_agent && !request_current_agent->actor &&
        !request_current_agent->record && request_current_agent->previous==test_request_outer_agent);
    ++test_request_fallback_calls;
    request_observe(test_request_original,test_request_self,test_request_value,test_request_axis,test_request_epsilon,0xec9274);
}
static void test_request_flight_fallback_original(void *self,void *slot,float dt,double time,unsigned char cheap,unsigned char player) {
    assert(self==test_request_unknown_self && slot==(void*)0x123456 && dt==0.25f && time==10.5 && cheap==1 && player==1);
    assert(solver_current_scope && solver_current_scope!=test_request_outer_flight && !solver_current_scope->kind &&
        solver_current_scope->previous==test_request_outer_flight && request_current_agent==test_request_outer_agent);
    ++test_request_fallback_calls;
    request_observe(test_request_original,test_request_self,test_request_value,test_request_axis,test_request_epsilon,0xec9274);
}
static void test_request_actual_fallbacks(void *agent,void *model) {
    request_agent_scope outer;solver_scope flight;unsigned char unknown[0x1000]={0};
    request_agent_begin(&outer,1,agent);solver_scope_begin(&flight,2,model,agent,(unsigned char*)agent+0xad0,1);
    assert(outer.record);test_request_outer_agent=&outer;test_request_outer_flight=&flight;test_request_unknown_self=unknown;
    for(unsigned which=0;which<2;which++) {
        unsigned char *entry=image+(which?FLIGHT_UPDATE:AGENT_ORIGINAL),saved[14];DWORD old,ignored;
        memcpy(saved,entry,14);assert(VirtualProtect(entry,14,PAGE_EXECUTE_READWRITE,&old));
        entry[0]=0xff;entry[1]=0x25;memset(entry+2,0,4);
        void *target=which?(void*)test_request_flight_fallback_original:(void*)test_request_agent_fallback_original;
        memcpy(entry+6,&target,8);FlushInstructionCache(GetCurrentProcess(),entry,14);
        LONG count=request_count,scopes=request_scope_count,failures=request_failures,excluded=request_excluded;
        unsigned calls=test_request_calls,before=test_request_fallback_calls;
        if(which) flight_update(unknown,(void*)0x123456,0.25f,10.5,1,1);
        else agent_update(unknown,(void*)0x123456,0.25f,10.5,1);
        assert(test_request_fallback_calls==before+1 && test_request_calls==calls+1 && request_count==count && request_scope_count==scopes);
        assert(request_current_agent==&outer && solver_current_scope==&flight && request_open==1);
        assert(request_excluded==excluded+1 && request_failures==failures+(which?0:1));
        memcpy(entry,saved,14);FlushInstructionCache(GetCurrentProcess(),entry,14);assert(VirtualProtect(entry,14,old,&ignored));
    }
    solver_scope_end(&flight);request_agent_end(&outer);
    assert(!solver_current_scope && !request_current_agent && !request_open && !solver_open_scopes);
}
static double test_request_boot_option,test_request_boot_result;
static double test_request_lua_number(lua_State *L,int index) {
    (void)L;return index==1?1:index==2?12345:index==6?test_request_boot_option:1000;
}
static const char *test_request_lua_string(lua_State *L,int index,size_t *length) {
    (void)L;const char *value=index==3?"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    if(length)*length=strlen(value);return value;
}
static void test_request_lua_result(lua_State *L,double value) {(void)L;test_request_boot_result=value;}
static void test_request_boot_mask(void) {
    /* Exercise actual lua_boot's dependency/mask branch through a valid existing
     * session reload. No game/network/patch path is entered by this test. */
    gate_stats old_stats=stats;int old_boot=boot_mode,old_used=used;unsigned old_port=remote_port;
    uint64_t old_options=boot_options;char old_secret[sizeof(secret)],old_digest[sizeof(initial_state_digest)];
    memcpy(old_secret,secret,sizeof(secret));memcpy(old_digest,initial_state_digest,sizeof(initial_state_digest));
    double (*old_number)(lua_State*,int)=p_lua_tonumber;
    const char *(*old_string)(lua_State*,int,size_t*)=p_lua_tolstring;
    void (*old_push)(lua_State*,double)=p_lua_pushnumber;
    p_lua_tonumber=test_request_lua_number;p_lua_tolstring=test_request_lua_string;p_lua_pushnumber=test_request_lua_result;
    boot_mode=used=stats.active=1;stats.slot=1;remote_port=12345;
    strcpy(secret,test_request_lua_string(NULL,3,NULL));strcpy(initial_state_digest,test_request_lua_string(NULL,5,NULL));
    boot_options=140737488355327ULL|SOLVER_OPTION|REQUEST_OPTION;test_request_boot_option=(double)boot_options;
    assert(lua_boot(NULL)==1 && test_request_boot_result==0);
    const double refused[]={(double)(1ULL<<47),(double)(1ULL<<48),(double)(1ULL<<49),(double)(1ULL<<51),
        (double)(1ULL<<53),0.5,NAN,(double)REQUEST_OPTION,(double)(REQUEST_OPTION|SOLVER_OPTION|70368744177664ULL|5)};
    for(unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);i++) {
        test_request_boot_option=refused[i];test_request_boot_result=-1;
        assert(lua_boot(NULL)==1 && test_request_boot_result==10);
    }
    stats=old_stats;boot_mode=old_boot;used=old_used;remote_port=old_port;boot_options=old_options;
    memcpy(secret,old_secret,sizeof(secret));memcpy(initial_state_digest,old_digest,sizeof(initial_state_digest));
    p_lua_tonumber=old_number;p_lua_tolstring=old_string;p_lua_pushnumber=old_push;
}
static void test_request_observation(void) {
    test_request_boot_mask();
    assert(boot_option_mask_valid((double)(140737488355327ULL|SOLVER_OPTION|REQUEST_OPTION)));
    /* Root also supports the NPC and MD observer bits; their dependencies are
     * checked by lua_boot, independently of exact-double mask admission. */
    assert(boot_option_mask_valid((double)(1ULL<<47)) && boot_option_mask_valid((double)(1ULL<<51)));
    assert(!boot_option_mask_valid((double)(1ULL<<53)) && !boot_option_mask_valid(NAN) && !boot_option_mask_valid(0.5));
    unsigned char *state=VirtualAlloc(NULL,0x18f60,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    unsigned char *configuration=VirtualAlloc(NULL,0x50a0,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    unsigned char object[0x208]={0},controller[0x50]={0},agent[0x1000]={0},model[0x3a0]={0},other_model[0x3a0]={0};
    assert(state && configuration);
    test_request_self=state+0x6a00;test_request_value=-38.5;test_request_axis=2;test_request_epsilon=0.0001;
    test_request_caller=0xec9274;test_request_abi();assert(test_request_calls==1 && !request_count);
    test_request_concurrent_stop();
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(777,123));
    *(uint64_t*)(object+8)=777;*(void**)(object+0x200)=controller;*(void**)controller=image+0x2c51158;
    *(void**)(controller+0x28)=object;*(void**)(controller+0x30)=agent+0xad0;*(void**)(controller+0x40)=agent;
    *(void**)(agent+0xef0)=object;*(void**)(agent+0xf80)=state;*(void**)(agent+0xf10)=model;
    *(void**)model=image+FLIGHT_ENTRY-0x70;*(void**)(model+0x10)=agent;memcpy(other_model,model,sizeof(model));
    *(void**)(image+0x6cf1538)=configuration;*(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    actor_components[0]=object;actor_ids[0]=777;flight_samples[0].agent=agent;flight_samples[0].model=model;
    stats.active=1;stats.issued=7;boot_phase=3;ui_thread=GetCurrentThreadId();solver_installed=request_installed=1;
    request_agent_scope outer,inner;solver_scope flight;
    request_agent_begin(&outer,1,agent);assert(outer.active && outer.record==1 && request_scopes[0].valid);
    solver_scope_begin(&flight,2,model,agent,agent+0xad0,1);
    HANDLE thread=CreateThread(NULL,0,test_request_thread,NULL,0,NULL);assert(thread);
    assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
    memset(state+0xa0d8,0,48);test_request_abi();
    assert(request_count==1 && request_events[0].ready && request_events[0].valid && request_events[0].owner==123 &&
        request_events[0].group==1 && request_events[0].axis==2 && request_events[0].ordinal==1 && !request_failures);
    double result;memcpy(&result,request_events[0].targets[1],8);assert(result==-29.5);
    /* Unknown nested Agent cannot inherit the known parent's request identity. */
    request_agent_begin(&inner,13,agent);assert(request_current_agent==&inner && !inner.record);
    unsigned calls=test_request_calls;request_observe(test_request_original,test_request_self,test_request_value,2,test_request_epsilon,0xec9274);
    assert(test_request_calls==calls+1 && request_count==1 && request_failures==1);
    request_agent_end(&inner);assert(request_current_agent==&outer && request_open==1);
    request_agent_begin(&inner,1,agent);assert(!inner.record && request_failures==2);request_agent_end(&inner);
    /* Unknown caller and wrong native axis both still preserve original args. */
    request_observe(test_request_original,test_request_self,test_request_value,2,test_request_epsilon,0);
    assert(request_excluded==2 && request_count==1 && request_scopes[0].other_requests==1);
    test_request_axis=99;calls=test_request_calls;
    request_observe(test_request_original,test_request_self,test_request_value,99,test_request_epsilon,0xec9274);
    assert(test_request_calls==calls+1 && request_failures==3);test_request_axis=2;
    *(void**)(image+0x6cf1538)=NULL;calls=test_request_calls;
    request_observe(test_request_original,test_request_self,test_request_value,2,test_request_epsilon,0xec9274);
    assert(test_request_calls==calls+1 && request_count==2 && !request_events[1].valid && request_failures==4);
    *(void**)(image+0x6cf1538)=configuration;
    request_count=REQUEST_CAPACITY;calls=test_request_calls;
    request_observe(test_request_original,test_request_self,test_request_value,2,test_request_epsilon,0xec9274);
    assert(test_request_calls==calls+1 && request_failures==5);request_count=2;
    solver_scope_end(&flight);*(void**)(agent+0xf10)=other_model;request_agent_end(&outer);
    assert(!request_current_agent && !request_open && request_scopes[0].ready && request_scopes[0].valid &&
        !request_scopes[0].model_same && request_scopes[0].solver_same && request_scopes[0].requests==2);
    request_scope_count=REQUEST_SCOPE_CAPACITY;request_agent_begin(&outer,1,agent);
    assert(!outer.record && request_failures==6);request_agent_end(&outer);assert(!request_open);
    request_scope_count=1;
    /* Duplicate saved actor/solver ownership cannot inherit the first claim. */
    *(void**)(agent+0xf10)=model;request_agent_begin(&outer,1,agent);
    solver_scope_begin(&flight,2,model,agent,agent+0xad0,1);
    flight_samples[1].agent=agent;actor_components[1]=object;actor_ids[1]=777;
    LONG failures=request_failures;calls=test_request_calls;
    request_observe(test_request_original,test_request_self,test_request_value,2,test_request_epsilon,0xec9274);
    assert(request_failures==failures+1 && test_request_calls==calls+1);
    memset(&flight_samples[1],0,sizeof(flight_samples[1]));actor_components[1]=NULL;actor_ids[1]=0;
    solver_scope_end(&flight);request_agent_end(&outer);
    test_request_actual_fallbacks(agent,model);
    _Alignas(16) unsigned char queue[0x90]={0};uintptr_t bounds[]={(uintptr_t)queue,(uintptr_t)queue+0x90,(uintptr_t)queue+0x90};
    *(uintptr_t*)queue=(uintptr_t)image+0x1234;*(uint64_t*)(queue+0x10)=777;*(uintptr_t*)(queue+0x18)=0xdeadbeef;
    request_snapshot snapshot={0};assert(request_queue_copy(read_local,bounds,&snapshot,0));
    assert(snapshot.queue_count[0]==1 && !snapshot.unknown_refs && snapshot.resource_refs==1 &&
        *(uint64_t*)snapshot.queue[0]==0x1234 && *(uint64_t*)(snapshot.queue[0]+0x10)==123 &&
        *(uint64_t*)(snapshot.queue[0]+0x18)==1 && *(uintptr_t*)(queue+0x18)==0xdeadbeef);
    bounds[1]++;assert(!request_queue_copy(read_local,bounds,&snapshot,0));bounds[1]--;
    bounds[1]=bounds[2]=bounds[0]+0x90*(REQUEST_QUEUE_CAPACITY+1);assert(!request_queue_copy(read_local,bounds,&snapshot,0));
    bounds[1]=bounds[2]=bounds[0]+0x90;*(uintptr_t*)queue=1;assert(!request_queue_copy(read_local,bounds,&snapshot,0));
    request_original_word=123;request_patched_word=456;*(uint64_t*)(image+REQUEST_RVA)=789;request_restore();
    assert(request_installed && !request_restored && *(uint64_t*)(image+REQUEST_RVA)==789);
    *(uint64_t*)(image+REQUEST_RVA)=456;request_restore();assert(!request_installed && request_restored && *(uint64_t*)(image+REQUEST_RVA)==123);
    assert(!request_pins());test_request_export_bounds();
    memset(request_events,0,sizeof(request_events));memset(request_scopes,0,sizeof(request_scopes));memset(request_owners,0,sizeof(request_owners));
    request_count=request_scope_count=request_failures=request_excluded=request_restored=0;request_enabled_tick=0;
    request_closing=request_peak=request_restore_open=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    VirtualFree(state,0,MEM_RELEASE);VirtualFree(configuration,0,MEM_RELEASE);
    memset(flight_samples,0,sizeof(flight_samples));memset(actor_components,0,sizeof(actor_components));memset(actor_ids,0,sizeof(actor_ids));
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;solver_installed=0;test_request_calls=0;
}
