static void *test_solver_arguments[5];
static unsigned test_solver_alignment;
static solver_fn test_solver_trampoline;
static uintptr_t test_solver_caller;
static unsigned char test_solver_prefix[6]={0x40,0x53,0x48,0x83,0xec,0x60};
static void test_solver_native_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);
    IMAGE_DOS_HEADER dos;assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE);
    assert(dos.e_lfanew>0 && dos.e_lfanew<4096 && !fseek(f,dos.e_lfanew,SEEK_SET));
    IMAGE_NT_HEADERS64 nt;assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt));
    assert(nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
           nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.OptionalHeader.SizeOfImage==0x6fa9000 &&
           nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64));
    unsigned offset=0;
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),f)==sizeof(section));
        if(SOLVER_RVA>=section.VirtualAddress && SOLVER_RVA+224<=section.VirtualAddress+section.SizeOfRawData)
            offset=section.PointerToRawData+SOLVER_RVA-section.VirtualAddress;
    }
    assert(offset && !fseek(f,(long)offset,SEEK_SET));unsigned char actual[224];
    assert(fread(actual,1,sizeof(actual),f)==sizeof(actual));fclose(f);
    assert(hash_ok(actual,sizeof(actual),"51bfe36495c5f64db4c3c10acd96d19dd5248d0d60f271e12aa3d4829a9e1f28"));
    unsigned char trampoline[64]={0},dispatch[64]={0};void *continuation=NULL;
    assert(solver_emit_thunks(trampoline,dispatch,actual,(void*)test_solver_native_image));
    memcpy(&continuation,trampoline+8,8);assert(continuation==actual+6 && !memcmp(trampoline,actual,6));
    memcpy(test_solver_prefix,actual,6);
    puts("pinned native image solver prefix/emitter/continuation verified (6 bytes)");
}
static uintptr_t test_solver_original(void *a,void *b,void *c,void *d,void *e) {
    void *actual[]={a,b,c,d,e};assert(!memcmp(actual,test_solver_arguments,sizeof(actual)));
    ++test_calls;((unsigned char*)b)[0]^=1;((unsigned char*)d)[0]^=2;
    return 0x1234567800000001ULL;
}
static uintptr_t test_solver_hook(void *a,void *b,void *c,void *d,void *e) {
    return solver_observe(test_solver_trampoline,a,b,c,d,e,test_solver_caller);
}
static void test_solver_abi(void **supplied) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(code);memcpy(code,test_solver_prefix,6);
    unsigned n=6;
    /* A native stand-in executes the relocated real prologue, observes stack
     * alignment, forwards the actual fifth argument and returns a wide marker.
     * Both native code and callback must run once through the real entry jump. */
    unsigned char alignment[]={0x48,0x89,0xe0,0x83,0xe0,0x0f,0x48,0xbb};
    memcpy(code+n,alignment,sizeof(alignment));n+=sizeof(alignment);
    void *address=&test_solver_alignment;memcpy(code+n,&address,8);n+=8;code[n++]=0x89;code[n++]=0x03;
    unsigned char fifth[]={0x48,0x8b,0x84,0x24,0x90,0,0,0,0x48,0x89,0x44,0x24,0x20,0x48,0xb8};
    memcpy(code+n,fifth,sizeof(fifth));n+=sizeof(fifth);address=(void*)test_solver_original;memcpy(code+n,&address,8);n+=8;
    unsigned char tail[]={0xff,0xd0,0x48,0x83,0xc4,0x60,0x5b,0xc3};memcpy(code+n,tail,sizeof(tail));
    thunk_recipe recipe=*thunk_recipe_at(2304);recipe.target=(void*)test_solver_hook;
    assert(thunk_emit_slot_at(code+256,&recipe,code));
    test_solver_trampoline=(solver_fn)(code+256);
    uint64_t original;memcpy(&original,code,8);code[0]=0xe9;int32_t distance=315;memcpy(code+1,&distance,4);code[5]=0x90;
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    unsigned char values[5][32]={{0}};for(unsigned i=0;i<5;i++)test_solver_arguments[i]=supplied?supplied[i]:values[i];
    unsigned before_calls=test_calls;test_solver_alignment=99;test_solver_caller=supplied?0xd93559:0;
    unsigned char before_b=*(unsigned char*)test_solver_arguments[1],before_d=*(unsigned char*)test_solver_arguments[3];
    uintptr_t result=((solver_fn)code)(test_solver_arguments[0],test_solver_arguments[1],test_solver_arguments[2],test_solver_arguments[3],test_solver_arguments[4]);
    assert(result==0x1234567800000001ULL && test_calls==before_calls+1 &&
           *(unsigned char*)test_solver_arguments[1]==(before_b^1) && *(unsigned char*)test_solver_arguments[3]==(before_d^2) && test_solver_alignment==0);
    assert(!memcmp(code+6,(unsigned char*)&original+6,2));
    code[0]=0;assert(!solver_emit_thunks(code+256,code+320,code,(void*)test_solver_hook));
    VirtualFree(code,0,MEM_RELEASE);if(!supplied)test_calls=0;
}
static DWORD WINAPI test_solver_other_thread(LPVOID ignored) {
    (void)ignored;assert(!solver_current_scope);return 0;
}
static void test_solver_observation(void) {
    unsigned char *controller=VirtualAlloc(NULL,0x19600,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    unsigned char *state=VirtualAlloc(NULL,0x18f60,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    unsigned char *configuration=VirtualAlloc(NULL,0x5060,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    unsigned char object[0x80]={0},other[0x80]={0},agent[0x1000]={0},model[0x20]={0},slots[0x340]={0};
    assert(controller && state && configuration);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(777,123) && world_identity_record(888,456));
    *(uint64_t*)(object+8)=777;*(uint64_t*)(other+8)=888;
    player_observer_controller=controller;player_observer_object=object;player_observer_actor=123;
    *(void**)(controller+0x28)=object;*(void**)(controller+0x30)=slots;
    *(void**)(image+0x6cf1538)=configuration;*(uint32_t*)(image+0x2f4b6f4)=0;
    *(void**)(agent+0xf80)=state;*(void**)(agent+0xef0)=other;
    flight_samples[0].agent=agent;flight_samples[0].model=model;actor_components[0]=other;actor_ids[0]=888;
    stats.active=1;stats.issued=7;boot_phase=3;ui_thread=GetCurrentThreadId();solver_installed=1;
    void *arguments[]={controller+0x3b0,controller+0x1a0,controller+0x1c0,controller+0x100,controller+0x160};
    memcpy(test_solver_arguments,arguments,sizeof(arguments));test_calls=0;
    test_solver_abi(arguments); /* Exercise the complete active recording path through native machine-code entry/trampoline. */
    uintptr_t result;
    assert(test_calls==1 && solver_count==1 && solver_events[0].valid &&
           solver_events[0].owner==123 && solver_events[0].ordinal==1 && solver_events[0].motion[1][0]==1 &&
           solver_events[0].motion[1][64]==2 && !solver_failures);
    solver_scope outer,inner;solver_scope_begin(&outer,1,controller,NULL,NULL,0);
    solver_scope outside_fixture;
    solver_scope_begin(&outside_fixture,2,model,agent,slots,ACTOR_DYNAMIC_FIRST);
    solver_scope_end(&outside_fixture);
    assert(!outside_fixture.active && solver_current_scope==&outer && solver_open_scopes==1 && !solver_failures);
    HANDLE t=CreateThread(NULL,0,test_solver_other_thread,NULL,0,NULL);assert(t);
    assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
    solver_scope_begin(&inner,2,model,agent,slots,1);
    assert(solver_current_scope==&inner && inner.previous==&outer && solver_open_scopes==2);
    arguments[0]=state;memcpy(test_solver_arguments,arguments,sizeof(arguments));
    result=solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0xec92c5);
    assert(result==0x1234567800000001ULL && solver_count==2 && solver_events[1].owner==456 && solver_events[1].valid);
    memcpy(slots,(unsigned char*)arguments[3],64);memcpy(slots+64,(unsigned char*)arguments[4],32);
    solver_scope_end(&inner);assert(solver_current_scope==&outer && solver_open_scopes==1 && solver_count==3);
    assert(solver_events[2].kind==2 && solver_events[2].valid && solver_events[2].solve_tick==7 && solver_events[2].solve_ordinal==1);
    solver_scope_end(&outer);assert(!solver_current_scope && !solver_open_scopes && solver_count==4 && solver_events[3].valid);
    /* Unknown coverage is counted and forwarded without claiming an owner. */
    arguments[0]=other;memcpy(test_solver_arguments,arguments,sizeof(arguments));
    solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0);
    assert(solver_excluded==1 && test_calls==3 && !solver_failures);
    /* Ambiguous alias, wrong caller, invalid reads and capacity each forward once. */
    arguments[0]=controller+0x3b0;memcpy(test_solver_arguments,arguments,sizeof(arguments));
    *(void**)(agent+0xf80)=arguments[0];
    solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0xd93559);
    assert(solver_failures==1 && test_calls==4);*(void**)(agent+0xf80)=state;
    solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0);
    assert(solver_failures==2 && test_calls==5);
    *(void**)(image+0x6cf1538)=NULL;
    solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0xd93559);
    assert(solver_failures==3 && test_calls==6 && !solver_events[4].valid);
    solver_count=SOLVER_CAPACITY;
    solver_observe(test_solver_original,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],0xd93559);
    assert(solver_failures==4 && test_calls==7);
    solver_scope_begin(&outer,1,controller,NULL,NULL,0);solver_scope_begin(&inner,1,controller,NULL,NULL,0);
    solver_scope_end(&outer);solver_scope_end(&inner);assert(!solver_current_scope && !solver_open_scopes && solver_failures==6);
    solver_original_word=123;solver_patched_word=456;*(uint64_t*)(image+SOLVER_RVA)=789;solver_restore();
    assert(solver_installed && !solver_restored && *(uint64_t*)(image+SOLVER_RVA)==789);
    *(uint64_t*)(image+SOLVER_RVA)=456;solver_restore();assert(!solver_installed && solver_restored && *(uint64_t*)(image+SOLVER_RVA)==123);
    assert(!solver_pins()); /* A wrong image cannot install even when its site looks right. */
    memset(solver_events,0,sizeof(solver_events));memset(solver_owners,0,sizeof(solver_owners));
    solver_count=solver_failures=solver_excluded=solver_restored=0;solver_enabled_tick=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    VirtualFree(controller,0,MEM_RELEASE);VirtualFree(state,0,MEM_RELEASE);VirtualFree(configuration,0,MEM_RELEASE);
    player_observer_controller=player_observer_object=NULL;player_observer_actor=0;
    memset(flight_samples,0,sizeof(flight_samples));memset(actor_components,0,sizeof(actor_components));memset(actor_ids,0,sizeof(actor_ids));
    stats.active=0;stats.issued=0;boot_phase=0;ui_thread=0;test_calls=0;
}
