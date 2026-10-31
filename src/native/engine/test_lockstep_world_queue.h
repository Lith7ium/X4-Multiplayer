static unsigned test_wq_calls,test_wq_protect;
static unsigned test_wq_dispatch_calls;
static void *test_wq_tls;
static uintptr_t test_wq_virtual_return;
static unsigned test_wq_phase_reads;
static int test_wq_phase_changing(const void *from,void *to,size_t bytes) {
    int ok=read_local(from,to,bytes);
    if(ok && from==image+GAME_RVA && ++test_wq_phase_reads==2) *(uint64_t*)to^=1;
    return ok;
}
static void test_world_queue_phase(unsigned char *tls) {
    unsigned char table[0x134]={0};uintptr_t saved_table=*(uintptr_t*)(image+TABLE_RVA);
    *(void**)(image+TABLE_RVA)=table;world_queue_phase_sample phase;
    assert(!world_queue_early_eligible());
    assert(world_queue_phase_read(read_local,tls,&phase) && phase.status==1 && phase.table==(uintptr_t)table);
    test_wq_phase_reads=0;assert(!world_queue_phase_read(test_wq_phase_changing,tls,&phase) && phase.status==3);
    *(unsigned*)(table+0x130)=2;assert(!world_queue_phase_read(read_local,tls,&phase) && phase.status==4);
    *(unsigned*)(table+0x130)=0;double saved_game=*(double*)(image+GAME_RVA);*(double*)(image+GAME_RVA)=NAN;
    assert(!world_queue_phase_read(read_local,tls,&phase) && phase.status==4);*(double*)(image+GAME_RVA)=saved_game;
    *(uintptr_t*)(image+TABLE_RVA)=1;assert(!world_queue_phase_read(read_local,tls,&phase) && phase.status==2);
    *(void**)(image+TABLE_RVA)=table;assert(world_queue_phase_read(read_local,tls,&phase));
    int saved_boot=boot_phase,saved_mode=boot_mode,saved_early=world_trace_early;uint64_t saved_tick=stats.issued;
    DWORD saved_ui=ui_thread;ULONGLONG saved_started=boot_started;
    world_queue_phase_verified=1;world_queue_enabled=1;world_trace_early=1;boot_mode=1;boot_phase=2;stats.issued=0;
    ui_thread=GetCurrentThreadId();boot_started=100;phase.view=1;
    assert(world_queue_early_context(&phase,100) && world_queue_early_context(&phase,120100));
    assert(!world_queue_early_context(&phase,99) && !world_queue_early_context(&phase,120101));
    phase.vbi_loading=1;phase.app_flags=2;assert(world_queue_early_context(&phase,100));
    phase.vbi_loading=0;phase.app_flags=0;assert(world_queue_early_context(&phase,100)); /* either native interval */
    phase.view=0;assert(!world_queue_early_context(&phase,100));phase.view=1;
    phase.status=3;assert(!world_queue_early_context(&phase,100));phase.status=1;
    boot_phase=3;assert(!world_queue_early_context(&phase,100));boot_phase=2;
    stats.issued=1;assert(!world_queue_early_context(&phase,100));stats.issued=0;
    ++ui_thread;assert(!world_queue_early_context(&phase,100));--ui_thread;
    world_queue_restoring=1;assert(!world_queue_early_context(&phase,100));world_queue_restoring=0;
    world_queue_enabled=0;assert(!world_queue_early_context(&phase,100));world_queue_phase_verified=0;
    world_trace_early=saved_early;boot_mode=saved_mode;boot_phase=saved_boot;stats.issued=saved_tick;ui_thread=saved_ui;boot_started=saved_started;
    *(uintptr_t*)(image+TABLE_RVA)=saved_table;
}
static const char *test_wq_image_path;
static HANDLE test_wq_entered,test_wq_release;
static void *test_wq_manager,*test_wq_agent,*test_wq_record;
static uint64_t test_wq_runtime;
static __attribute__((aligned(16))) unsigned char test_wq_pending[0x90];
static void test_wq_enqueue(void *manager,uint64_t runtime,void *command) {
    assert(manager==test_wq_manager && runtime==test_wq_runtime && command==test_wq_record);++test_wq_calls;
    if(test_wq_protect) {DWORD old;assert(VirtualProtect(command,4096,PAGE_NOACCESS,&old));}
}
static void test_wq_append(void *agent,void *record) {
    assert(agent==test_wq_agent && record==test_wq_record);++test_wq_calls;
    memcpy(test_wq_pending,record,0x90);uintptr_t bounds[3]={(uintptr_t)test_wq_pending,(uintptr_t)test_wq_pending+0x90,(uintptr_t)test_wq_pending+0x90};
    memcpy((unsigned char*)agent+0x6c0,bounds,sizeof(bounds));
}
static void test_wq_dispatch_target(void *command,void *agent) {
    ++test_wq_dispatch_calls;assert(agent==test_wq_agent && (unsigned char*)command+0x10==test_wq_record);
    /* Observer unit fixture. The executable forwarder below independently
     * proves the real virtual-callee return is this exact resident address. */
    world_queue_append_observe(test_wq_append,agent,(unsigned char*)command+0x10,(uintptr_t)(thunk+2698),test_wq_tls);
}
static void test_wq_dispatch_unknown(void *command,void *agent) {
    assert(command==test_wq_record && agent==test_wq_agent);++test_wq_dispatch_calls;
    test_wq_virtual_return=(uintptr_t)__builtin_return_address(0);
}
static void test_wq_dispatch_typed(void *command,void *agent) {
    ++test_wq_dispatch_calls;uintptr_t type=*(uintptr_t*)command-(uintptr_t)image;
    if(type==WORLD_QUEUE_ABORT_VTABLE) {memset((unsigned char*)agent+0x6c0,0,24);*((unsigned char*)agent+0xf33)=1;}
    else {
        unsigned offset=type==WORLD_QUEUE_ROLL_VTABLE?0xf76:type==WORLD_QUEUE_ROTATION_VTABLE?0xf77:0xf78;
        *((unsigned char*)agent+offset)=*((unsigned char*)command+8);
    }
}
static void test_wq_dispatch_restore(void *command,void *agent) {
    test_wq_dispatch_unknown(command,agent);world_queue_restore();
    test_wq_virtual_return=(uintptr_t)__builtin_return_address(0);
    assert(!world_queue_sites && world_queue_open==1 && world_queue_restore_open>=1);
}
static void test_wq_dispatch_duplicate(void *command,void *agent) {
    test_wq_dispatch_target(command,agent);test_wq_dispatch_target(command,agent);
}
static void test_wq_child_append(void *command,void *agent) {
    (void)agent;test_wq_dispatch_target(command,test_wq_agent);
}
static void test_wq_dispatch_foreign_scope(void *command,void *agent) {
    (void)agent;unsigned char foreign[0x1000]={0};
    world_queue_dispatch_observe(test_wq_child_append,command,foreign,(uintptr_t)(image+0xeacf66),test_wq_tls);
}
static void test_wq_dispatch_bad_origin(void *command,void *agent) {
    ++test_wq_dispatch_calls;
    world_queue_append_observe(test_wq_append,agent,(unsigned char*)command+0x10,(uintptr_t)test_wq_dispatch_bad_origin,test_wq_tls);
}
static void test_wq_dispatch_lifetime(void *command,void *agent) {
    test_wq_dispatch_unknown(command,agent);++*(uint64_t*)(world_trace_watches[0].object+8);
}
static void test_world_queue_typed(unsigned char *agent,unsigned char *tls) {
    unsigned char *allocation=VirtualAlloc(NULL,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(allocation);
    DWORD old;assert(VirtualProtect(allocation+4096,4096,PAGE_NOACCESS,&old));unsigned char *small=allocation+4096-16;
    const unsigned types[4]={WORLD_QUEUE_ABORT_VTABLE,WORLD_QUEUE_ROLL_VTABLE,WORLD_QUEUE_ROTATION_VTABLE,WORLD_QUEUE_POSITION_VTABLE};
    for(unsigned i=0;i<4;i++) {
        memset(small,0,16);*(void**)small=image+types[i];if(i) small[8]=1;
        test_wq_record=small;LONG64 before=world_queue_count;
        world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,small,(uintptr_t)(image+0x6e19a5),tls);
        assert(!world_queue_events[before].errors && world_queue_lifetimes[before].state==1);
        /* A native consumer detaches its list before dispatch. Neither a zero
         * live manager head nor a skipped recipient retires this generation. */
        *(uintptr_t*)((unsigned char*)test_wq_manager+0xb8)=0;
        assert(world_queue_lifetimes[before].state==1 && world_queue_events[before].order_size==16);
        world_queue_dispatch_observe(test_wq_dispatch_typed,small,agent,(uintptr_t)(image+0xeacf66),tls);
        world_queue_event *dispatched=&world_queue_events[before+1];
        assert(!dispatched->errors && world_queue_lifetimes[before].state==3 && dispatched->enqueue==(uint64_t)before+1);
        assert(dispatched->flags_after[i?6+i:0]==1);
    }
    /* Exact 0x10 allocation extent; the following page was inaccessible above. */
    memset(small,0,16);*(void**)small=image+WORLD_QUEUE_ROLL_VTABLE;small[8]=2;world_queue_event invalid={0};
    assert(!world_queue_command(&invalid,small));
    *(void**)small=image+WORLD_QUEUE_ABORT_VTABLE;*(uint64_t*)(small+8)=0xfeedbeef;memset(&invalid,0,sizeof(invalid));
    assert(!world_queue_command(&invalid,small)); /* unknown related owner */
    *(uint64_t*)(small+8)=0;test_wq_record=small;LONG64 skipped=world_queue_count;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,small,(uintptr_t)(image+0xea1864),tls);
    assert(world_queue_lifetimes[skipped].state==1); /* no dispatch, still incomplete */
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,small,(uintptr_t)(image+0xea1864),tls);
    assert(world_queue_events[skipped+1].errors&16); /* may not reuse an unretired input */
    world_queue_dispatch_observe(test_wq_dispatch_typed,small,agent,(uintptr_t)(image+0xeacf66),tls);
    assert(world_queue_lifetimes[skipped].state==3);VirtualFree(allocation,0,MEM_RELEASE);
    unsigned char command[0xb0]={0};*(void**)command=image+WORLD_QUEUE_ORDER_VTABLE;*(void**)(command+0x10)=image+0x2c4f578;
    for(unsigned i=0;i<3;i++) {
        test_wq_record=command;LONG64 start=world_queue_count;
        world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);test_wq_record=command+0x10;
        world_queue_dispatch_observe(i==2?test_wq_dispatch_bad_origin:i?test_wq_dispatch_foreign_scope:test_wq_dispatch_duplicate,command,agent,(uintptr_t)(image+0xeacf66),tls);
        assert(world_queue_events[start+(i?2:3)].errors&32); /* foreign barrier or duplicate append */
        if(i==2) assert(world_queue_events[start+2].raw_caller==(uintptr_t)test_wq_dispatch_bad_origin && world_queue_events[start+2].caller_source==2);
    }
    test_wq_record=command;LONG64 lifetime=world_queue_count;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);
    world_queue_dispatch_observe(test_wq_dispatch_lifetime,command,agent,(uintptr_t)(image+0xeacf66),tls);
    assert(world_queue_events[lifetime+1].errors&256);--*(uint64_t*)(world_trace_watches[0].object+8);
    LONG64 retained=world_queue_count,dropped=world_queue_dropped;unsigned calls=test_wq_dispatch_calls;
    world_queue_count=WORLD_QUEUE_MAX;
    world_queue_dispatch_observe(test_wq_dispatch_unknown,command,agent,(uintptr_t)(image+0xeacf66),tls);
    assert(test_wq_dispatch_calls==calls+1 && world_queue_dropped==dropped+1);world_queue_count=retained;
}
static int test_wq_owned(uintptr_t current) {return current==*(uintptr_t*)(image+0x38f4c60);}
static int test_wq_changed(uintptr_t current) {++*(uint64_t*)(current+0x18);return 1;}
static int test_wq_fail_second(unsigned site,uint64_t expected,uint64_t value) {return site==1?0:world_queue_swap(site,expected,value);}
static int test_wq_fail_third(unsigned site,uint64_t expected,uint64_t value) {return site==2?0:world_queue_swap(site,expected,value);}
static unsigned test_wq_unwind_attempts;
static BOOLEAN WINAPI test_wq_no_unwind(PRUNTIME_FUNCTION function,DWORD count,DWORD64 base) {
    assert(function==&world_queue_forward_unwind && count==1 && base==(DWORD64)(uintptr_t)thunk);++test_wq_unwind_attempts;return FALSE;
}
static void test_wq_virtual_unwind(void) {
    DWORD64 base=0;PRUNTIME_FUNCTION entry=RtlLookupFunctionEntry((DWORD64)(uintptr_t)(thunk+2698),&base,NULL);
    assert(entry && base==(DWORD64)(uintptr_t)thunk && entry->BeginAddress==2688 && entry->EndAddress==2703 && entry->UnwindData==2704);
    for(unsigned offset=2695;offset<=2698;offset+=3) {
        DWORD64 stack[16]={0},frame=0;PVOID handler=NULL;stack[5]=0x1234567887654321ULL;
        CONTEXT context={0};context.ContextFlags=CONTEXT_FULL;context.Rip=base+offset;context.Rsp=(DWORD64)(uintptr_t)stack;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,entry,&context,&handler,&frame,NULL);
        assert(context.Rip==stack[5] && context.Rsp==(DWORD64)(uintptr_t)stack+0x30);
    }
}
static void test_wq_abi_enqueue(void *manager,uint64_t runtime,void *command) {
    assert(manager==test_wq_manager && runtime==test_wq_runtime && command==test_wq_record);++test_wq_calls;
}
static void test_wq_abi_append(void *agent,void *record) {assert(agent==test_wq_agent && record==test_wq_record);++test_wq_calls;}
static void test_wq_image_prefixes(unsigned char prefixes[3][6]) {
    if(!test_wq_image_path) return;
    FILE *file=fopen(test_wq_image_path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long length=ftell(file);
    assert(length>0 && length<512*1024*1024 && !fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);assert(bytes && fread(bytes,1,(size_t)length,file)==(size_t)length);fclose(file);
    assert(hash_ok(bytes,(ULONG)length,EXE_SHA));IMAGE_DOS_HEADER *dos=(void*)bytes;
    assert(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0 && (size_t)dos->e_lfanew+sizeof(IMAGE_NT_HEADERS64)<(size_t)length);
    IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);assert((unsigned char*)(sections+nt->FileHeader.NumberOfSections)<=bytes+length);
    const unsigned phase_rvas[6]={TICK_RVA,0xac2a50,0xac2c70,0x9ee7a0,0x14b8dc0,0x14b8e20};
    const unsigned phase_sizes[6]={2127,522,446,9772,89,101};
    unsigned char *phase_image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(phase_image);
    for(unsigned i=0;i<6;i++) {
        int found=0;
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];unsigned rva=phase_rvas[i];
            if(rva<s->VirtualAddress || rva-s->VirtualAddress+phase_sizes[i]>s->SizeOfRawData) continue;
            size_t offset=s->PointerToRawData+rva-s->VirtualAddress;assert(offset+phase_sizes[i]<=(size_t)length);
            memcpy(phase_image+rva,bytes+offset,phase_sizes[i]);found=1;break;
        }
        assert(found);
    }
    unsigned char *prior_image=image;int prior_dt=dt_installed;image=phase_image;dt_installed=0;
    assert(world_queue_phase_pins());*(unsigned short*)(image+0xfa3302)=0x9066;
    assert(!world_queue_phase_pins());dt_installed=1;assert(world_queue_phase_pins());
    image[TICK_RVA+80]^=1;assert(!world_queue_phase_pins());image[TICK_RVA+80]^=1;
    *(unsigned short*)(image+0xfa3302)=0x9090;assert(!world_queue_phase_pins());
    image=prior_image;dt_installed=prior_dt;VirtualFree(phase_image,0,MEM_RELEASE);
    const unsigned sizes[2]={245,141};const char *hashes[2]={
        "2b270bde3dbf64151f2a3d69077f5dc90efd758101981317fcdc05decfcbe9dd",
        "4acf143b3200d1632cd984d9881f2a43a07d3d6786059c4dd0d8659821dbbc9e"};
    for(unsigned i=0;i<2;i++) {
        unsigned char *entry=NULL;
        for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];unsigned rva=world_queue_entries[i];
            if(rva<s->VirtualAddress || rva-s->VirtualAddress+sizes[i]>s->SizeOfRawData) continue;
            size_t offset=s->PointerToRawData+rva-s->VirtualAddress;assert(offset+sizes[i]<=(size_t)length);entry=bytes+offset;break;
        }
        assert(entry && hash_ok(entry,sizes[i],hashes[i]));
        unsigned char trampoline[32]={0},dispatch[32]={0};void *target=(void*)test_wq_abi_enqueue;
        assert(world_queue_emit(trampoline,dispatch,entry,target,i));memcpy(prefixes[i],entry,5);
        void *next=NULL,*actual=NULL;memcpy(&next,trampoline+11,8);memcpy(&actual,dispatch+6,8);
        assert(!memcmp(trampoline,entry,5) && next==entry+5 && actual==target);
        unsigned char corrupted[8];memcpy(corrupted,entry,8);corrupted[4]^=1;
        assert(!world_queue_emit(trampoline,dispatch,corrupted,target,i));
    }
    unsigned char *consumer=NULL;
    for(unsigned j=0;j<nt->FileHeader.NumberOfSections;j++) {
        IMAGE_SECTION_HEADER *s=&sections[j];
        if(0xeace40<s->VirtualAddress || 0xeace40-s->VirtualAddress+785>s->SizeOfRawData) continue;
        size_t offset=s->PointerToRawData+0xeace40-s->VirtualAddress;assert(offset+785<=(size_t)length);consumer=bytes+offset;break;
    }
    assert(consumer && hash_ok(consumer,785,"475eca6da99a7a143f3f4ceb4ece14b45c36bbbbee2cb624a1c268d8cc062818"));
    const unsigned char actual[8]={0x48,0x8b,0x01,0xff,0x50,0x08,0x48,0x8b};assert(!memcmp(consumer+0x120,actual,8));
    memcpy(prefixes[2],consumer+0x120,6);unsigned char forward[32]={0},dispatch[32]={0};
    assert(world_queue_emit(forward,dispatch,consumer+0x120,(void*)test_wq_dispatch_unknown,2));
    assert(!memcmp(forward,"\x48\x83\xec\x28\x48\x8b\x01\xff\x50\x08\x48\x83\xc4\x28\xc3",15));
    assert(!memcmp(forward+16,"\x01\x04\x01\x00\x04\x42\x00\x00",8));
    free(bytes);puts("world queue actual-image phase pins/owned-dt reconstruction and both ABI prefix variants verified");
}
static DWORD WINAPI test_wq_restore_thread(void *unused) {
    (void)unused;unsigned char tls[0x320]={0};assert(world_queue_admit());
    assert(SetEvent(test_wq_entered) && WaitForSingleObject(test_wq_release,2000)==WAIT_OBJECT_0);
    world_queue_event *e=world_queue_begin(0,0,(uintptr_t)(image+0xeadfbe),tls);assert(e);
    test_wq_abi_enqueue(test_wq_manager,test_wq_runtime,test_wq_record);
    world_queue_end(e);world_queue_leave();return 1;
}
static void test_world_queue(unsigned char *object,unsigned char *agent,unsigned char *tls) {
    assert(!world_queue_enabled && !world_queue_started && !world_queue_pins());
    world_queue_event maximum;memset(&maximum,0xff,sizeof(maximum));memset(maximum.name,'a',sizeof(maximum.name)-1);maximum.name[sizeof(maximum.name)-1]=0;
    char row[WORLD_QUEUE_ROW_CAP];int row_length=world_queue_format(&maximum,UINT32_MAX,row,sizeof(row));
    assert(row_length==1645 && strlen(row)==(size_t)row_length && row_length+sizeof("[X4LAN] NATIVE_GATE ")+1<2048);
    char short_row[64];assert(world_queue_format(&maximum,UINT32_MAX,short_row,sizeof(short_row))==-1 && short_row[63]==0);
    assert(world_queue_format(&maximum,UINT32_MAX,NULL,0)==-1);
    unsigned char *old_thunk=thunk;thunk=make_thunk();assert(thunk && thunk_page_rx(thunk));
    test_world_queue_phase(tls);
    world_queue_dispatch_tls=TlsAlloc();assert(world_queue_dispatch_tls!=TLS_OUT_OF_INDEXES);test_wq_tls=tls;
    test_wq_calls=0;test_wq_manager=*(void**)(image+0x6cf21c8);test_wq_runtime=700;test_wq_agent=agent;
    unsigned char command[0xb0]={0};*(void**)command=image+WORLD_QUEUE_ORDER_VTABLE;
    *(void**)(command+0x10)=image+0x2c4f578;test_wq_record=command;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,0,tls);
    assert(test_wq_calls==1 && !world_queue_count); /* default-off original once */
    world_trace_early=1;world_trace_bound=1;world_trace_imports[0]=1;world_queue_restoring=0;world_queue_enabled=1;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==2 && world_queue_ready()==1 && !world_queue_events[0].errors && world_queue_events[0].enqueue==1);
    test_wq_record=command+0x10;
    world_queue_dispatch_observe(test_wq_dispatch_target,command,agent,(uintptr_t)(image+0xeacf66),tls);
    assert(test_wq_calls==3 && world_queue_ready()==3 && !world_queue_events[1].errors && !world_queue_events[2].errors && world_queue_events[2].enqueue==1);
    assert(world_queue_lifetimes[0].state==3 && world_queue_events[1].trace_event && world_queue_events[2].trace_event && world_queue_events[2].dispatch_parent==2);
    world_queue_append_observe(test_wq_append,agent,command+0x10,(uintptr_t)(image+0xeacf66),tls);
    assert(test_wq_calls==4 && (world_queue_events[3].errors&32)); /* no unbracketed append */
    test_wq_record=command;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==5 && !world_queue_events[4].errors && world_queue_events[4].enqueue==5);
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==6 && (world_queue_events[5].errors&16)); /* active reused address */
    unsigned char *owned=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(owned);memcpy(owned,command,sizeof(command));
    test_wq_record=owned;test_wq_protect=1;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,owned,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==7 && !world_queue_events[6].errors);test_wq_protect=0;VirtualFree(owned,0,MEM_RELEASE);
    /* Native CURRENT alone never proves the current thread owns an execution. */
    unsigned char exec[0x50]={0},context[0x128]={0},script[0x30]={0},entity[16]={0};
    *(unsigned*)(exec+0x38)=0x66;*(uintptr_t*)(exec+0x40)=(uintptr_t)context+0xa8;
    *(uint64_t*)(context+8)=123;*(void**)(context+0xd8)=script;*(void**)(context+0x120)=entity;
    memcpy(script+0x10,"test.script",12);*(uint64_t*)(script+0x20)=11;*(uint64_t*)(script+0x28)=15;*(uint64_t*)(entity+8)=1234;
    *(void**)(image+0x38f4c60)=exec;world_queue_event source={0};world_queue_source(&source);
    assert(source.source_status==1 && source.name_status==2 && !strcmp(source.name,"test.script"));
    world_queue_owner_matches=test_wq_owned;memset(&source,0,sizeof(source));world_queue_source(&source);assert(source.source_status==2);
    world_queue_owner_matches=test_wq_changed;memset(&source,0,sizeof(source));world_queue_source(&source);assert(source.source_status==5);
    *(unsigned*)(exec+0x38)=0x99;memset(&source,0,sizeof(source));world_queue_source(&source);assert(source.source_status==3);
    world_queue_owner_matches=NULL;*(void**)(image+0x38f4c60)=NULL;
    LONG64 count=world_queue_count;world_queue_count=WORLD_QUEUE_MAX;
    test_wq_record=command;world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,700,command,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==8 && world_queue_dropped==1);world_queue_count=count;
    test_wq_runtime=701;unsigned unchanged=test_wq_calls;
    world_queue_enqueue_observe(test_wq_enqueue,test_wq_manager,701,command,(uintptr_t)(image+0xeadfbe),tls);
    assert(test_wq_calls==unchanged+1 && world_queue_count==count);test_wq_runtime=700;
    test_world_queue_typed(agent,tls);test_wq_record=command;
    world_queue_enabled=0;
    /* Execute both real-image five-byte prologues through near dispatch and
     * replay trampolines, with a test-only continuation checking Windows ABI. */
    DWORD protect;
    assert(VirtualProtect(image+0xe89000,0x2000,PAGE_EXECUTE_READWRITE,&protect));
    assert(VirtualProtect(image+0xeac000,0x2000,PAGE_EXECUTE_READWRITE,&protect));
    void *continuations[2]={(void*)test_wq_abi_enqueue,(void*)test_wq_abi_append};
    unsigned char prefixes[3][6]={{0x48,0x89,0x5c,0x24,0x10},{0x48,0x89,0x5c,0x24,0x08},{0x48,0x8b,0x01,0xff,0x50,0x08}};
    test_wq_image_prefixes(prefixes);
    for(unsigned i=0;i<2;i++) {
        unsigned char *entry=image+world_queue_entries[i];
        memcpy(entry,prefixes[i],5);entry[5]=0xff;entry[6]=0x25;memset(entry+7,0,4);memcpy(entry+11,&continuations[i],8);
    }
    /* Reproduce the call site's native alignment, shadow space and RBX node.
     * The preserved bytes after the call are the actual mov rcx,[rbx+18]. */
    unsigned char prologue[]={0x53,0x48,0x83,0xec,0x20,0x4c,0x89,0xc3,0xeb,0x06};
    memcpy(image+WORLD_QUEUE_DISPATCH-16,prologue,sizeof(prologue));
    memcpy(image+WORLD_QUEUE_DISPATCH,prefixes[2],6);
    const unsigned char continuation[]={0x48,0x8b,0x4b,0x18,0x48,0x83,0xc4,0x20,0x5b,0xc3};
    memcpy(image+WORLD_QUEUE_DISPATCH+6,continuation,sizeof(continuation));
    assert(!world_queue_register_unwind(test_wq_no_unwind) && !world_queue_forward_base && !world_queue_sites && test_wq_unwind_attempts==1);
    assert(world_queue_prepare_hooks());test_wq_virtual_unwind();
    assert(world_queue_register_unwind(test_wq_no_unwind) && test_wq_unwind_attempts==1); /* no duplicate registration */
    for(unsigned i=0;i<2;i++) assert((world_queue_original[i]>>40)==(world_queue_patched[i]>>40));
    assert((world_queue_original[2]>>48)==(world_queue_patched[2]>>48));
    assert(!world_queue_patch(test_wq_fail_second) && !world_queue_sites); /* rollback */
    assert(!world_queue_patch(test_wq_fail_third) && !world_queue_sites);
    assert(world_queue_patch(world_queue_swap));
    test_wq_record=command;unsigned before=test_wq_calls;
    ((world_queue_enqueue_fn)(image+WORLD_QUEUE_ENQUEUE))(test_wq_manager,700,command);
    ((world_queue_append_fn)(image+WORLD_QUEUE_APPEND))(agent,command);
    assert(test_wq_calls==before+2);
    uintptr_t vtable[2]={0,(uintptr_t)test_wq_dispatch_unknown};unsigned char opaque[16]={0},node[0x20]={0};
    *(void**)opaque=vtable;*(void**)(node+0x18)=opaque;test_wq_record=opaque;unsigned dispatch_before=test_wq_dispatch_calls;
    ((void (*)(void*,void*,void*))(image+WORLD_QUEUE_DISPATCH-16))(opaque,agent,node);
    assert(test_wq_dispatch_calls==dispatch_before+1 && test_wq_virtual_return==(uintptr_t)(thunk+2698));test_wq_record=command;
    uint64_t foreign=world_queue_patched[0]^0x100;memcpy(image+WORLD_QUEUE_ENQUEUE,&foreign,8);world_queue_restore();
    assert(world_queue_sites==1 && *(uint64_t*)(image+WORLD_QUEUE_ENQUEUE)==foreign);
    memcpy(image+WORLD_QUEUE_ENQUEUE,&world_queue_patched[0],8);world_queue_restore();assert(!world_queue_sites);
    for(unsigned i=0;i<3;i++) assert(*(uint64_t*)(image+world_queue_entries[i])==world_queue_original[i]);
    assert(!world_queue_open && !world_queue_restore_open && world_queue_peak>=2);
    world_queue_restoring=0;world_queue_enabled=1;assert(world_queue_patch(world_queue_swap));
    test_wq_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_wq_release=CreateEventW(NULL,TRUE,FALSE,NULL);assert(test_wq_entered && test_wq_release);
    LONG64 before_restore_count=world_queue_count;HANDLE worker=CreateThread(NULL,0,test_wq_restore_thread,NULL,0,NULL);assert(worker);
    assert(WaitForSingleObject(test_wq_entered,2000)==WAIT_OBJECT_0 && world_queue_open==1 && world_queue_count==before_restore_count);
    world_queue_restore();assert(!world_queue_sites && world_queue_open==1 && world_queue_restore_open==1 && !world_queue_admit());
    assert(SetEvent(test_wq_release) && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
    assert(!world_queue_open && world_queue_restore_open==1 && world_queue_count==before_restore_count+1);
    CloseHandle(worker);CloseHandle(test_wq_entered);CloseHandle(test_wq_release);
    /* Restore while a real six-byte indirect CALL is still in its original.
     * Its unmodified return address must remain eacf66, outside restored bytes. */
    world_queue_restoring=0;world_queue_enabled=1;assert(world_queue_patch(world_queue_swap));
    vtable[1]=(uintptr_t)test_wq_dispatch_restore;test_wq_record=opaque;dispatch_before=test_wq_dispatch_calls;
    ((void (*)(void*,void*,void*))(image+WORLD_QUEUE_DISPATCH-16))(opaque,agent,node);
    assert(test_wq_dispatch_calls==dispatch_before+1 && test_wq_virtual_return==(uintptr_t)(thunk+2698) && !world_queue_open && world_queue_restore_open>=1 && !world_queue_sites);
    TlsFree(world_queue_dispatch_tls);world_queue_dispatch_tls=TLS_OUT_OF_INDEXES;
    assert(RtlDeleteFunctionTable(&world_queue_forward_unwind));world_queue_forward_base=0; /* test-only allocation is about to be freed */
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=old_thunk;world_trace_early=0;world_trace_bound=0;world_trace_imports[0]=0;
    memset(agent+0x6c0,0,24);memset(world_queue_events,0,sizeof(world_queue_events));memset(world_queue_lifetimes,0,sizeof(world_queue_lifetimes));
    world_queue_count=world_queue_order=world_queue_failures=world_queue_dropped=world_queue_preimport_calls=0;
    world_queue_open=world_queue_peak=world_queue_restore_open=0;world_queue_restoring=0;
    (void)object;
}
