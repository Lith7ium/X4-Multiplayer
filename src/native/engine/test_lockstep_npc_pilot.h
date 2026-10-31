static unsigned pilot_test_nested,pilot_test_change;
static void *pilot_test_exec;
static uintptr_t pilot_test_irq(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    assert(exec==pilot_test_exec && program==&test_calls && begin==7 && end==9 && mode==1);
    ++test_calls;test_rng+=5;*(uint64_t*)((unsigned char*)exec+0x18)+=2;return 0xfedcba9876ULL;
}
static void pilot_test_event(void *context) {
    ++test_calls;test_rng+=3;++*(uint64_t*)((unsigned char*)context+0x140);
    assert(interrupt_observe(pilot_test_irq,&test_rng,pilot_test_exec,&test_calls,7,9,1,0)==0xfedcba9876ULL);
}
static void pilot_test_original(void *exec) {
    ++test_calls;test_rng+=7;unsigned char *e=exec;uintptr_t value=*(uintptr_t*)(e+0x40),context=value-0xa8;
    uintptr_t previous=*(uintptr_t*)(image+NPC_PROVENANCE_CURRENT);*(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=(uintptr_t)exec;
    ++*(uint64_t*)(e+0x18);
    if(npc_pilot_current) assert(!npc_pilot_clear());
    if(pilot_test_nested) {pilot_test_nested=0;interrupt_event_observe(pilot_test_event,&test_rng,(void*)context,0);}
    if(pilot_test_change==2) ++*(uint64_t*)(context+8);
    if(pilot_test_change==3) *(uintptr_t*)(e+0x40)+=8;
    if(pilot_test_change==4) assert(VirtualFree((void*)context,0,MEM_RELEASE));
    if(pilot_test_change==5) *(uintptr_t*)(context+0x120)=0;
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=previous;
}
static DWORD WINAPI pilot_test_thread(LPVOID exec) {
    assert(!npc_pilot_current);npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);return 0;
}
static void pilot_test_reset(void) {
    assert(!npc_pilot_current && !npc_pilot_overflow_depth);npc_pilot_count=npc_pilot_context_count=0;
    npc_pilot_calls=npc_pilot_failures=npc_pilot_overflows=npc_pilot_foreign=npc_pilot_unmapped=npc_pilot_unreadable=0;
    npc_pilot_retired=npc_pilot_after_unresolved=npc_pilot_sequence=0;interrupt_failures=0;pilot_test_nested=pilot_test_change=0;
}
static void test_npc_pilot(void) {
    uint64_t options=boot_options,issued=stats.issued;int enabled=npc_provenance_enabled,phase=boot_phase,bmode=boot_mode;
    int ii=interrupt_installed,ei=interrupt_event_installed,wi=world_identity_installed,origin_ready=interrupt_origin_ready;
    boot_options=281474976710656ULL;npc_provenance_enabled=1;
    unsigned char *context=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(context);
    unsigned char exec[0x50]={0},script[0x40]={0},entity[0x80]={0};pilot_test_exec=exec;
    *(unsigned*)(exec+0x38)=0x66;*(uintptr_t*)(exec+0x40)=(uintptr_t)context+0xa8;
    *(uint64_t*)(context+8)=77;*(void**)(context+0xd8)=script;test_npc_string(script+0x10,"move.flee");
    *(void**)(context+0x120)=entity;*(uint64_t*)(entity+8)=123;
    *(unsigned*)(context+0x18)=0x555;*(unsigned*)(context+0x20)=0xa;*(unsigned*)(context+0x30)=0x58;
    *(uint64_t*)(context+0x28)=UINT64_MAX;*(uint64_t*)(context+0x38)=UINT64_MAX; /* Payloads are never read. */
    world_identities=VirtualAlloc(NULL,WORLD_IDENTITY_CAPACITY*sizeof(*world_identities),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,NPC_PILOT_SAVED) && world_identity_record(124,456));
    npc_pilot_enabled=0;unsigned calls=test_calls;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(test_calls==calls+1 && !npc_pilot_calls); /* Default off. */
    assert(!npc_pilot_configure(NPC_PILOT_SAVED));boot_phase=0;boot_mode=1;stats.issued=0;
    interrupt_installed=interrupt_event_installed=world_identity_installed=1;
    assert(!interrupt_origin_pinned()); /* Synthetic layouts cannot authorize native pins. */
    assert(!npc_pilot_configure(NPC_PILOT_SAVED));interrupt_origin_ready=1; /* Simulate an already verified activation. */
    assert(!npc_pilot_configure(NPC_PILOT_SAVED+1));assert(npc_pilot_configure(NPC_PILOT_SAVED));
    boot_phase=3;stats.issued=20;calls=test_calls;uint64_t rng=test_rng;pilot_test_nested=1;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(test_calls==calls+3 && test_rng==rng+15 && npc_pilot_count==3 && !npc_pilot_failures);
    assert(npc_pilot_configure(NPC_PILOT_SAVED) && npc_pilot_count==3 && npc_pilot_calls==3); /* UI reload is idempotent. */
    assert(npc_pilots[0].kind==1 && npc_pilots[1].kind==3 && npc_pilots[2].kind==2 && npc_pilots[2].nest==2 && npc_pilots[2].depth==3);
    assert(!npc_pilots[0].irq_origin.source && !npc_pilots[1].irq_origin.source &&
        npc_pilots[2].irq_origin.source==3 && !npc_pilots[2].irq_origin.raw && !npc_pilots[2].irq_origin.caller);
    assert(npc_pilots[0].enter==1 && npc_pilots[0].exit==6 && npc_pilots[1].enter==2 && npc_pilots[1].exit==5 &&
           npc_pilots[2].enter==3 && npc_pilots[2].exit==4);
    assert(npc_pilots[1].parent_kind==2 && npc_pilots[1].parent_status==1 && npc_pilots[1].parent_saved==NPC_PILOT_SAVED);
    for(unsigned i=0;i<3;i++) assert(npc_pilots[i].returned && npc_pilots[i].after_status==1 && !npc_pilots[i].errors &&
        npc_pilots[i].types[0]==0xa && npc_pilots[i].types[1]==0x58 && !strcmp(npc_pilots[i].script,"move.flee"));
    assert(!npc_pilot_current && !npc_provenance_current && npc_pilot_clear());
    uint64_t token=npc_pilot_contexts[0].token;pilot_test_change=2;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilots[0].after_status==2 && npc_pilot_retired==1);
    pilot_test_change=0;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilots[1].context!=token);
    pilot_test_change=3;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilots[2].after_status==3);
    *(uintptr_t*)(exec+0x40)=(uintptr_t)context+0xa8;pilot_test_change=5;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilots[3].after_status==5 && npc_pilot_after_unresolved==1);
    *(void**)(context+0x120)=entity;pilot_test_reset();npc_pilot_enabled=1;
    *(uint64_t*)(entity+8)=124;calls=test_calls;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(!npc_pilot_calls && test_calls==calls+1);
    *(uint64_t*)(entity+8)=125;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilot_unmapped==1 && !npc_pilot_calls);
    *(uint64_t*)(entity+8)=123;calls=test_calls;
    HANDLE thread=CreateThread(NULL,0,pilot_test_thread,exec,0,NULL);assert(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    assert(test_calls==calls+1 && npc_pilot_foreign==1 && !npc_pilot_calls);
    npc_pilot_count=NPC_PILOT_LIMIT;calls=test_calls;npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(test_calls==calls+1 && npc_pilot_overflows==1 && npc_pilot_failures==1);
    pilot_test_reset();npc_pilot_sample full_sample={0};full_sample.depth=NPC_PILOT_DEPTH;
    npc_pilot_scope full={0};full.sample=&full_sample;npc_pilot_current=&full;calls=test_calls;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(test_calls==calls+1 && npc_pilot_current==&full && !npc_pilot_overflow_depth && npc_pilot_overflows==1);
    npc_pilot_current=NULL;pilot_test_reset();npc_pilot_context_count=NPC_PILOT_CONTEXTS;*(uint64_t*)(context+8)=9999;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilot_overflows==1 && npc_pilot_failures==1);
    pilot_test_reset();boot_options=140737488355328ULL;npc_pilot_count=NPC_PILOT_LIMIT;calls=test_calls;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);
    assert(test_calls==calls+1 && !npc_pilot_calls && !npc_pilot_overflows && !npc_pilot_failures);
    boot_options=281474976710656ULL;pilot_test_reset();pilot_test_change=4;
    npc_exec_entry(pilot_test_original,exec,&test_rng,0x106e3fb);assert(npc_pilots[0].after_status==4 && npc_pilot_after_unresolved==1);
    pilot_test_reset();npc_pilot_enabled=0;npc_provenance_enabled=enabled;boot_options=options;boot_phase=phase;boot_mode=bmode;stats.issued=issued;
    interrupt_installed=ii;interrupt_event_installed=ei;world_identity_installed=wi;interrupt_origin_ready=origin_ready;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
}
