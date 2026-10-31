static void *provenance_test_child;
static unsigned provenance_test_replace;
static void provenance_test_original(void *exec) {
    ++test_calls;test_rng+=11;
    uintptr_t saved=*(uintptr_t*)(image+NPC_PROVENANCE_CURRENT);
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=(uintptr_t)exec;
    if(npc_provenance_current && npc_provenance_current->readable && !npc_provenance_overflow_depth) {
        assert(npc_provenance_current_matches((uintptr_t)exec));
        assert(!npc_provenance_current_matches((uintptr_t)exec+1));
    }
    if(provenance_test_replace) {
        npc_provenance_guard g;assert(npc_provenance_guard_read(exec,&g));
        ++*(uint64_t*)(g.context+8);provenance_test_replace=0;
        assert(!npc_provenance_current_matches((uintptr_t)exec));
    }
    if(provenance_test_child) {
        void *child=provenance_test_child;provenance_test_child=NULL;
        uint64_t pc=*(uint64_t*)((unsigned char*)exec+0x18);if(!pc) *(uint64_t*)((unsigned char*)exec+0x18)=1;
        npc_exec_entry(provenance_test_original,child,&test_rng,0x111c821);
        *(uint64_t*)((unsigned char*)exec+0x18)=pc;
    }
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=saved;
}
static DWORD WINAPI provenance_test_thread(LPVOID exec) {
    assert(!ai_origin_current && !ai_origin_dispatch_current && !npc_provenance_current_matches((uintptr_t)exec));
    assert(!npc_provenance_current);npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(!npc_provenance_current);return 0;
}
static void provenance_test_reset(void) {
    npc_provenance_count=npc_provenance_cue_count=0;
    npc_provenance_calls=npc_provenance_unresolved=npc_provenance_unowned=npc_provenance_failures=npc_provenance_overflows=0;
    npc_trigger_count=0;npc_trigger_calls=npc_trigger_seen=npc_trigger_unsupported=0;interrupt_failures=0;
    assert(!npc_provenance_current && !npc_provenance_overflow_depth);
}
static unsigned provenance_origin_replace,provenance_origin_reenter;
static void provenance_origin_original(void *exec) {
    if(provenance_origin_reenter) {
        provenance_origin_reenter=0;
        /* Reenter the same ExecState through an unknown caller after consumption. */
        npc_exec_entry(provenance_test_original,exec,&test_rng,0);
    } else provenance_test_original(exec);
}
static void provenance_origin_entry(void *exec) {
    uintptr_t ret=(uintptr_t)__builtin_return_address(0),base=(uintptr_t)image;
    assert(ret<base || ret>=base+0x6fa9000); /* Actual owned C call loses the native RVA. */
    npc_provenance_guard guard;assert(npc_provenance_guard_read(exec,&guard));
    if(provenance_origin_replace) ++*(uint64_t*)(guard.context+8);
    npc_exec_entry(provenance_origin_original,exec,&test_rng,0);
    if(provenance_origin_replace) {--*(uint64_t*)(guard.context+8);provenance_origin_replace=0;}
}
static void test_provenance_origin(void *outer,void *child) {
    unsigned char original[14];memcpy(original,image+AI_ORIGINAL,14);DWORD old,ignored;
    assert(VirtualProtect(image+AI_ORIGINAL,14,PAGE_EXECUTE_READWRITE,&old));
    image[AI_ORIGINAL]=0xff;image[AI_ORIGINAL+1]=0x25;memset(image+AI_ORIGINAL+2,0,4);
    void *target=(void*)provenance_origin_entry;memcpy(image+AI_ORIGINAL+6,&target,8);
    FlushInstructionCache(GetCurrentProcess(),image+AI_ORIGINAL,14);
    int installed=ai_installed;unsigned __int128 patch=ai_patched_word;uint64_t failures=ai_failures;
    /* Execute the real C ai_run through a synthetic native CALL whose return
     * address is exactly106e3fb. The call is ABI-aligned with shadow space. */
    unsigned char native[32],stub[14];memcpy(native,image+0x106e3e0,32);memcpy(stub,image+0x106e500,14);
    DWORD native_old;assert(VirtualProtect(image+0x106e3e0,0x200,PAGE_EXECUTE_READWRITE,&native_old));
    unsigned char *call=image+0x106e3e0;memset(call,0x90,32);
    memcpy(call,"\x48\x83\xec\x28",4);call[0x16]=0xe8;int32_t rel=0x105;memcpy(call+0x17,&rel,4);
    memcpy(call+0x1b,"\x48\x83\xc4\x28\xc3",5);
    unsigned char *jump=image+0x106e500;jump[0]=0xff;jump[1]=0x25;memset(jump+2,0,4);
    target=(void*)ai_run;memcpy(jump+6,&target,8);FlushInstructionCache(GetCurrentProcess(),call,0x200);
    ai_installed=1;memcpy(&ai_patched_word,image+AI_WORD,16);
    unsigned initial=npc_provenance_count,initial_calls=test_calls;uint64_t initial_rng=test_rng;
    provenance_test_child=child;((ai_run_fn)call)(outer);
    assert(npc_provenance_samples[initial].owner_caller_source==2 && npc_provenance_samples[initial].owner_caller==0x106e3fb &&
           test_calls==initial_calls+2 && test_rng==initial_rng+22 && !ai_origin_current && !ai_origin_dispatch_current && ai_failures==failures);
    ai_origin_frame frame;int entered=ai_origin_begin(&frame,outer,(uintptr_t)(image+0x106e3fb));
    assert(entered && frame.valid && ai_origin_current==&frame);
    provenance_test_child=child;unsigned start=npc_provenance_count,calls=test_calls;uint64_t rng=test_rng;
    ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);
    npc_provenance_sample *s=&npc_provenance_samples[start];
    assert(test_calls==calls+2 && test_rng==rng+22 && !s->flags && s->owner_caller==0x106e3fb &&
           !s->owner_caller_raw && s->owner_caller_source==2 && !ai_origin_dispatch_current && ai_failures==failures);
    assert(!ai_origin_consume(outer)); /* Scope alone cannot claim a dispatch. */
    ai_origin_dispatch guarded={NULL,&frame,0};ai_origin_dispatch_current=&guarded;
    assert(!ai_origin_consume(child) && !guarded.consumed);
    assert(ai_origin_consume(outer)==0x106e3fb && guarded.consumed && !ai_origin_consume(outer));
    ai_origin_dispatch_current=NULL;
    provenance_origin_reenter=1;provenance_test_child=child;start=npc_provenance_count;
    ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);s=&npc_provenance_samples[start];
    assert(!s->owner_caller && !s->owner_caller_raw && s->owner_caller_source==3 && ai_failures==failures);
    /* Nested wrapper scopes restore the previous scope and each call consumes once. */
    ai_origin_frame inner;int inner_entered=ai_origin_begin(&inner,outer,(uintptr_t)(image+0x106e3fb));
    assert(inner_entered && inner.previous==&frame);provenance_test_child=child;
    ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);ai_origin_end(&inner,inner_entered);
    assert(ai_origin_current==&frame && !ai_origin_dispatch_current && ai_failures==failures);
    provenance_origin_replace=1;provenance_test_child=child;start=npc_provenance_count;
    ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);s=&npc_provenance_samples[start];
    assert(!s->owner_caller && s->owner_caller_source==3 && ai_failures==failures+1);
    ai_origin_end(&frame,entered);assert(!ai_origin_current && !ai_origin_dispatch_current);
    /* Wrong incoming callsite or changed patch ownership never obtains origin. */
    for(unsigned foreign=0;foreign<2;foreign++) {
        if(foreign) ai_patched_word^=1;
        entered=ai_origin_begin(&frame,outer,(uintptr_t)(image+(foreign?0x106e3fb:0x106e3fc)));
        assert(entered && !frame.valid);provenance_test_child=child;start=npc_provenance_count;
        ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);s=&npc_provenance_samples[start];
        assert(!s->owner_caller && s->owner_caller_source==3);ai_origin_end(&frame,entered);
        if(foreign) ai_patched_word^=1;
    }
    /* Bit47-only dispatch leaves both diagnostic TLS stacks and counters alone. */
    uint64_t options=boot_options;boot_options=140737488355328ULL;start=npc_provenance_count;calls=test_calls;
    assert(!ai_origin_begin(&frame,outer,(uintptr_t)(image+0x106e3fb)));
    ai_call_original((ai_run_fn)(image+AI_ORIGINAL),outer);
    assert(npc_provenance_count==start && test_calls==calls+1 && !ai_origin_current && !ai_origin_dispatch_current);
    boot_options=options;ai_installed=installed;ai_patched_word=patch;ai_failures=failures;
    memcpy(image+0x106e3e0,native,32);memcpy(image+0x106e500,stub,14);FlushInstructionCache(GetCurrentProcess(),call,0x200);
    assert(VirtualProtect(image+0x106e3e0,0x200,native_old,&ignored));
    memcpy(image+AI_ORIGINAL,original,14);FlushInstructionCache(GetCurrentProcess(),image+AI_ORIGINAL,14);
    assert(VirtualProtect(image+AI_ORIGINAL,14,old,&ignored));
}
static void test_npc_provenance(void *source_exec,void *source_cue,void *node) {
    uint64_t options=boot_options;boot_options=281474976710656ULL;npc_provenance_enabled=1;
    unsigned char cue[0x2a0]={0},parent[0x2a0]={0},definition[0x2a0]={0},exec[0x50];
    memcpy(cue,source_cue,0x180);memcpy(parent,source_cue,0x180);memcpy(definition,source_cue,0x180);memcpy(exec,source_exec,sizeof(exec));
    *(uint64_t*)(cue+8)=1;*(uint64_t*)(parent+8)=2;*(uint64_t*)(definition+8)=3;
    *(void**)(exec+0x40)=cue+0xb0;*(void**)(cue+0x158)=parent;*(void**)(cue+0x168)=definition;
    *(void**)(cue+0x170)=cue;*(void**)(cue+0x178)=cue;
    *(unsigned*)(cue+0x18)=0x580;*(unsigned*)(cue+0x20)=0xa;*(uint64_t*)(cue+0x28)=123;
    *(unsigned*)(cue+0x30)=0xa;*(uint64_t*)(cue+0x38)=123; /* Same tag outside object slot stays unresolved. */
    *(unsigned*)(cue+0x40)=0x54;*(uint64_t*)(cue+0x48)=(uintptr_t)node;
    *(unsigned*)(cue+0x180)=2;*(unsigned*)(cue+0x184)=2;*(unsigned*)(cue+0x200)=3;*(double*)(cue+0x1f8)=12.0;
    world_identities=VirtualAlloc(NULL,WORLD_IDENTITY_CAPACITY*sizeof(*world_identities),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(123,456));
    *(void**)(image+0x1018)=image+0x2000;
    test_calls=0;test_rng=50;unsigned char before[sizeof(cue)];memcpy(before,cue,sizeof(cue));
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    npc_provenance_sample *s=&npc_provenance_samples[0];
    assert(test_calls==1 && test_rng==61 && !memcmp(before,cue,sizeof(cue)) && !s->flags && !interrupt_failures);
    assert(npc_provenance_calls==1 && npc_provenance_count==1 && s->trigger==npc_triggers[0].ordinal && s->depth==1);
    assert(s->cue.token && s->links[0].token!=s->cue.token && s->links[1].token!=s->cue.token && s->links[2].token==s->cue.token &&
           s->links[3].token==s->cue.token && s->ancestry_count==1 && s->ancestry[0].token==s->links[0].token);
    assert(s->values[0].kind==1 && s->values[0].value==456 && s->values[1].kind==0 && !s->values[1].value &&
           s->values[2].kind==0 && !s->values[2].value && s->values[3].kind==0 && npc_provenance_unresolved==3);
    assert(s->state==2 && s->pending==2 && s->block==3 && !s->owner_status);
    /* Same name, same address reused for a new runtime lifetime: distinct LOCAL token. */
    uint64_t old=s->cue.token;*(uint64_t*)(cue+8)=4;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(npc_provenance_samples[1].cue.token!=old && !strcmp(npc_provenance_samples[1].cue.name,s->cue.name) && !npc_provenance_cue_token(1));
    assert(npc_provenance_decode(0x580,0,0xa,999).kind==2 && npc_provenance_decode(0x580,0,0,UINT64_MAX).kind==3);
    assert(npc_provenance_decode(0x637,0,0x12,4).kind==4 && npc_provenance_decode(0x637,0,0x12,999).kind==5);
    assert(npc_provenance_decode(0x637,1,0x12,4).kind==0 && npc_provenance_decode(0x581,0,0xa,123).kind==0);
    /* A non-NPC AI native parent remains the direct owner under a foreign barrier. */
    unsigned char ai[0x200]={0},entity[0x80]={0},script[0x40]={0},outer[0x50];memcpy(outer,exec,sizeof(outer));
    *(unsigned*)(outer+0x38)=0x66;*(void**)(outer+0x40)=ai+0xa8;*(uint64_t*)(outer+0x18)=1;
    *(uint64_t*)(ai+8)=7;*(void**)(ai+0x120)=entity;*(uint64_t*)(entity+8)=123;*(void**)(ai+0xd8)=script;
    test_npc_string(script+0x10,"ambient.script");
    provenance_test_child=exec;unsigned start=npc_provenance_count;
    npc_exec_entry(provenance_test_original,outer,&test_rng,0x106e3fb);s=&npc_provenance_samples[start];
    assert(!s->flags && s->owner_kind==2 && s->owner_status==1 && s->owner_saved==456 && !strcmp(s->owner.name,"ambient.script") &&
           s->owner_pc==0 && s->owner_type==0x1000 && s->owner_action==0x2000 && s->owner_caller==0x106e3fb && s->depth==2);
    assert(!npc_provenance_current && !npc_trigger_foreign_depth && !interrupt_failures);
    test_provenance_origin(outer,exec);
    /* A foreign MD module is named without expanding the NPC action scope. */
    unsigned char other_cue[0x2a0]={0},other_definition[0x180]={0},module[0x100]={0},name_node[0x60]={0},md_exec[0x50];
    memcpy(md_exec,outer,sizeof(md_exec));*(unsigned*)(md_exec+0x38)=0x5d;*(void**)(md_exec+0x40)=other_cue+0xb0;
    *(uint64_t*)(other_cue+8)=8;*(void**)(other_cue+0x160)=other_definition;*(void**)(other_definition+0x150)=module;
    test_npc_string(module+0x18,"Ambient");test_npc_string(name_node+0x20,"Ticker");
    *(void**)(module+0x80)=name_node;*(void**)(name_node+0x40)=other_definition;
    provenance_test_child=exec;start=npc_provenance_count;
    npc_exec_entry(provenance_test_original,md_exec,&test_rng,0x111c821);s=&npc_provenance_samples[start];
    assert(npc_provenance_count==start+1 && !s->flags && s->owner_kind==1 && s->owner_status==1 && s->owner.token && !s->owner_saved && !strcmp(s->owner.name,"Ambient.Ticker"));
    /* Reentrant named execution preserves both ownership stacks. */
    unsigned char child_exec[0x50];memcpy(child_exec,exec,sizeof(child_exec));
    provenance_test_child=child_exec;start=npc_provenance_count;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(npc_provenance_count==start+2 && !npc_provenance_samples[start+1].flags && npc_provenance_samples[start+1].owner_status==1 &&
           npc_provenance_samples[start+1].owner.token==npc_provenance_samples[start].cue.token && !npc_provenance_current && !npc_trigger_current);
    npc_provenance_frame thread_guard={0};npc_provenance_current=&thread_guard;start=npc_provenance_count;
    HANDLE thread=CreateThread(NULL,0,provenance_test_thread,exec,0,NULL);assert(thread);
    assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    assert(npc_provenance_current==&thread_guard && npc_provenance_count==start && npc_trigger_foreign_thread==1);
    npc_provenance_current=NULL;npc_trigger_foreign_thread=0;
    /* Native parent present without an observed entry is explicit, not inherited. */
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=(uintptr_t)outer;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(npc_provenance_samples[npc_provenance_count-1].owner_status==2 && npc_provenance_unowned==1);
    *(uint64_t*)(entity+8)=999;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(!npc_provenance_samples[npc_provenance_count-1].owner_saved && npc_provenance_unowned==2);
    *(uint64_t*)(entity+8)=123;
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=0;
    /* Changed owner lifetime while its TLS bracket is active must fail ownership. */
    provenance_test_replace=1;provenance_test_child=exec;
    npc_exec_entry(provenance_test_original,outer,&test_rng,0x106e3fb);
    assert(npc_provenance_samples[npc_provenance_count-1].flags&16 && npc_provenance_failures && !npc_provenance_current);
    provenance_test_reset();
    /* Ancestry cycle and unreadable cue are retained as failed observations. */
    *(void**)(cue+0x158)=cue;npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(npc_provenance_samples[0].flags&4 && npc_provenance_failures==1);*(void**)(cue+0x158)=parent;
    provenance_test_reset();unsigned char chain[9][0x2a0]={{0}};
    for(unsigned i=0;i<9;i++) {
        memcpy(chain[i],source_cue,0x180);*(uint64_t*)(chain[i]+8)=100+i;
        if(i<8) *(void**)(chain[i]+0x158)=chain[i+1];
    }
    *(void**)(cue+0x158)=chain[0];npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(npc_provenance_samples[0].flags&4 && npc_provenance_samples[0].ancestry_count==8);*(void**)(cue+0x158)=parent;
    provenance_test_reset();npc_provenance_cue_count=NPC_PROVENANCE_CUES;*(uint64_t*)(cue+8)=99999;
    npc_provenance_link full_link;assert(!npc_provenance_cue_read((uintptr_t)cue,&full_link) && npc_provenance_overflows==1);
    *(uint64_t*)(cue+8)=4;
    provenance_test_reset();*(uint64_t*)(cue+8)=0;unsigned calls=test_calls;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(test_calls==calls+1 && npc_provenance_samples[0].flags && npc_provenance_failures);*(uint64_t*)(cue+8)=4;
    provenance_test_reset();npc_provenance_count=NPC_PROVENANCE_LIMIT;calls=test_calls;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(test_calls==calls+1 && npc_provenance_calls==1 && npc_provenance_overflows==1 && npc_provenance_failures==1);
    provenance_test_reset();npc_provenance_frame full={0};full.depth=NPC_PROVENANCE_DEPTH;npc_provenance_current=&full;calls=test_calls;
    npc_exec_entry(provenance_test_original,exec,&test_rng,0x111c821);
    assert(test_calls==calls+1 && npc_provenance_current==&full && !npc_provenance_overflow_depth && npc_provenance_overflows==1 && npc_provenance_failures);
    npc_provenance_current=NULL;provenance_test_reset();
    /* Native API legacy-only mode never enters even a full enabled observer. */
    npc_provenance_count=NPC_PROVENANCE_LIMIT;boot_options=140737488355328ULL;calls=test_calls;
    npc_exec_entry(trigger_test_legacy_selected,exec,&test_rng,0x111c821);
    assert(test_calls==calls+1 && !npc_provenance_calls && !npc_provenance_overflows && !npc_provenance_failures && !interrupt_failures);
    npc_exec_count=0;npc_exec_calls=0;memset(npc_exec_keys,0,sizeof(npc_exec_keys));provenance_test_reset();
    assert(!npc_provenance_pinned()); /* A nonmatching image can never activate these layouts. */
    npc_provenance_enabled=0;boot_options=options;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
}
