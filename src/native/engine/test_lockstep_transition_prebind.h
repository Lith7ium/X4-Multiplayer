/* Owned memory + actual callbacks; no native game, fake completion or getter. */
static void prebind_fixture(void) {
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==0x123456789abcULL) world_identities[i].saved=0x176a13;
    saved_fixture_stage(1,0);uint64_t runtime=*(uint64_t*)(object+8);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0xae79c;
    const char *manifest="2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player";unsigned count=0;
    memset(transition_saved_watches,0,sizeof(transition_saved_watches));
    assert(transition_saved_parse(manifest,strlen(manifest),120,transition_saved_watches,&count) && count==1);
    strcpy(transition_saved_manifest,manifest);transition_saved_import_calls=transition_saved_prebind=0;
    strcpy((char*)saved_macro+0x100,"ship_arg_s_scout_01_a_macro");*(void**)(saved_macro+0x20)=saved_macro+0x100;
    *(uint64_t*)(saved_macro+0x30)=strlen((char*)saved_macro+0x100);*(uint64_t*)(saved_macro+0x38)=63;
    memcpy(faction+0x10,"player",7);*(uint64_t*)(faction+0x20)=6;transition_import_capture(object,runtime,0xae79c);
    *(void**)(image+0x2c39488+0x198)=image+0xd8dd00;image[0x2f4b828]=0;
    transition_prebind_enabled=1;transition_prebind.image=(uintptr_t)image;transition_prebind.thunk=(uintptr_t)thunk;
    transition_prebind.started=boot_started;transition_prebind.port=remote_port;transition_prebind.timeout=timeout_ms;
    strcpy(transition_prebind.digest,initial_state_digest);
    strcpy(transition_prebind.run,"84e0c0eafaa95a34c293f278ac52e45ce537bab5e752a00e6959a13ae103b65a");
    transition_diag_enabled=1;strcpy(transition_diag_run,transition_prebind.run);
}
static void prebind_enqueue(void) {
    world_queue_enqueue_observe(native_enqueue,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26),native_tls);
}
static void prebind_activate(int context,unsigned char active) {
    assert(context==8 && active==1);prebind_enqueue();image[0x2f4b828]=1;
}
static void prebind_start(void) {transition_activate_observe(prebind_activate,8,1,(uintptr_t)(image+0x1252026));}
static void prebind_export(const char *suffix) {
    const char *base=getenv("X4LAN_TRANSITION_PREBIND_TEST_LOG");if(!base) return;
    char path[1024];int length=snprintf(path,sizeof(path),"%s%s",base,suffix);assert(length>0 && (size_t)length<sizeof(path));
    FILE *out=fopen(path,"w");assert(out);p_lua_pushlstring=diag_lua_string;p_lua_pushnil=diag_lua_nil;p_lua_tonumber=evidence_number;
    fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_QUEUE_DIAG_ENABLED schema=1 resumed=0 run=%s allocation_generation_known=0 ticket_authority=0\n",transition_diag_run);
    fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_PREBIND_ENABLED schema=1 resumed=0 run=%s atomic_lease=0\n",transition_prebind.run);
    for(evidence_row=1;evidence_row<=3;evidence_row++) {assert(lua_player_transition_prebind_row(NULL)==1 && diag_lua_text[0]);fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);}
    evidence_row=3;assert(lua_player_transition_prebind_row(NULL)==1 && !diag_lua_text[0]);
    for(evidence_row=1;evidence_row<=transition_diag_count;evidence_row++) {assert(lua_player_transition_queue_evidence_row(NULL)==1 && diag_lua_text[0]);fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);}
    transition_restore();assert(lua_player_transition_prebind_status(NULL)==1);fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_PREBIND_STOP %s\n",diag_lua_text);
    assert(lua_player_transition_queue_evidence_status(NULL)==1);fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_QUEUE_DIAG_STOP %s\n",diag_lua_text);
    evidence_row=1;assert(lua_player_transition_watch(NULL)==1 && diag_lua_text[0]);fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);
    for(evidence_row=1;evidence_row<=transition_ready();evidence_row++) {assert(lua_player_transition_row(NULL)==1 && diag_lua_text[0]);fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);}
    evidence_row=1;assert(lua_player_transition_ticket(NULL)==1 && diag_lua_text[0]);fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);
    assert(lua_player_transition_status(NULL)==1);fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_STOP %s\n",diag_lua_text);assert(!fclose(out));
}
static unsigned prebind_fault;
static int prebind_fault_reader(const void *source,void *out,size_t length) {
    if(source==command && ++diag_reads==prebind_fault) {memset(out,0xa5,length);return 0;}
    return read_local(source,out,length);
}
static void prebind_read_activate(int context,unsigned char active) {
    assert(context==8 && active==1 && transition_saved_queue_admit());
    void *scope=transition_prebind_enqueue(prebind_fault_reader,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26));
    native_enqueue(object,*(uint64_t*)(object+8),command);transition_saved_enqueue_end(scope);image[0x2f4b828]=1;
}
static void prebind_immediate_activate(int context,unsigned char active) {
    assert(context==8 && active==1);
    world_queue_enqueue_observe(saved_immediate,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26),native_tls);image[0x2f4b828]=1;
}
static HANDLE prebind_entered,prebind_resume;
static int prebind_paused_reader(const void *source,void *out,size_t length) {
    if(source==command) {assert(SetEvent(prebind_entered));assert(WaitForSingleObject(prebind_resume,5000)==WAIT_OBJECT_0);}
    return read_local(source,out,length);
}
static void prebind_paused_activate(int context,unsigned char active) {
    assert(context==8 && active==1 && transition_saved_queue_admit());
    void *scope=transition_prebind_enqueue(prebind_paused_reader,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26));
    native_enqueue(object,*(uint64_t*)(object+8),command);transition_saved_enqueue_end(scope);image[0x2f4b828]=1;
}
static DWORD WINAPI prebind_worker(void *unused) {
    (void)unused;transition_activate_observe(prebind_paused_activate,8,1,(uintptr_t)(image+0x1252026));return 0;
}
static void prebind_blocked_activate(int context,unsigned char active) {
    assert(context==8 && active==1);world_queue_enqueue_observe(saved_blocked,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26),native_tls);image[0x2f4b828]=1;
}
static DWORD WINAPI prebind_original_worker(void *unused) {
    (void)unused;transition_activate_observe(prebind_blocked_activate,8,1,(uintptr_t)(image+0x1252026));return 0;
}
static unsigned prebind_bind_result;
static DWORD WINAPI prebind_bind_worker(void *unused) {
    (void)unused;main_thread=GetCurrentThreadId();assert(SetEvent(entered));prebind_bind_result=(unsigned)transition_saved_bind();return 0;
}
static void prebind_throw(void *manager,uint64_t runtime,void *input) {
    native_enqueue(manager,runtime,input);RaiseException(0xe0424242,0,0,NULL);
}
static void prebind_throw_activate(int context,unsigned char active) {
    assert(context==8 && active==1);world_queue_enqueue_observe(prebind_throw,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xd8dd26),native_tls);
}
static void prebind_empty_activate(int context,unsigned char active) {assert(context==8 && active==1);++activation_calls;image[0x2f4b828]=1;}
static void prebind_deactivate(int context,unsigned char active) {assert(context==8 && active==0);++activation_calls;image[0x2f4b828]=0;}
static void prebind_other_model(void *input,void *other) {assert(input==command && other!=agent);++calls;}
static unsigned char *prebind_protected;
static void prebind_protected_activate(int context,unsigned char active) {
    assert(context==8 && active==1);
    world_queue_enqueue_observe(saved_poison,object,*(uint64_t*)(object+8),prebind_protected,(uintptr_t)(image+0xd8dd26),native_tls);image[0x2f4b828]=1;
}
static void test_prebind_config(void) {
    prebind_fixture();transition_prebind_enabled=transition_diag_enabled=0;memset(&transition_prebind,0,sizeof(transition_prebind));
    transition_started=0;boot_phase=0;stats.slot=1;boot_options=70377334178981ULL;
    int previous_used=used;uint64_t previous_patched=patched_word;used=1;memcpy(&patched_word,image+WORD_RVA,8);
    unsigned char *previous_thunk=thunk;thunk=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(thunk);
    strcpy(secret,"00000000000000000000000000000000");
    strcpy(initial_state_digest,"22701acf4492101258d3ec8b0872b69550a75bb63be70b96b0b2f9136b397df1");
    const char *run="84e0c0eafaa95a34c293f278ac52e45ce537bab5e752a00e6959a13ae103b65a";
    assert(!transition_diag_config(run)); /* extended digest cannot silently use old mode */
    for(unsigned fault=0;fault<7;fault++) {
        if(fault==0) initial_state_digest[0]='0';if(fault==1) secret[0]='1';if(fault==2) boot_phase=1;
        if(fault==3) transition_started=1;if(fault==4) stats.slot=2;if(fault==5) ++boot_options;
        if(fault==6) *(void**)(image+0x2c39488+0x198)=image+0xd8dd01;
        assert(!transition_prebind_config(run) && !transition_prebind_enabled);
        if(fault==0) initial_state_digest[0]='2';if(fault==1) secret[0]='0';if(fault==2) boot_phase=0;
        if(fault==3) transition_started=0;if(fault==4) stats.slot=1;if(fault==5) --boot_options;
        if(fault==6) *(void**)(image+0x2c39488+0x198)=image+0xd8dd00;
    }
    assert(transition_prebind_config(run));
    pgetup_ledger previous_getup=getup_ledger;unsigned previous_gd=gd_enabled;
    void (*previous_push)(lua_State*,double)=p_lua_pushnumber;p_lua_pushnumber=diag_lua_number;
    memset(&getup_ledger,0,sizeof(getup_ledger));gd_enabled=0;
    assert(lua_load_requested(NULL)==1 && diag_lua_result==0 && boot_phase==0);
    assert(!getup_configure_mode(run,GD_CONTRACT) && !getup_configure(run));
    assert(transition_diag_config(run));
    assert(lua_load_requested(NULL)==1 && diag_lua_result==0 && boot_phase==0);
    getup_ledger=previous_getup;gd_enabled=previous_gd;p_lua_pushnumber=previous_push;
    transition_started=1;transition_sites=7;
    for(unsigned i=0;i<3;i++) memcpy(&transition_patched[i],image+transition_entries[i],8);
    transition_prebind.count=2;transition_prebind.exported=1;transition_prebind.returned=1;
    transition_prebind_ledger previous=transition_prebind;
    for(unsigned phase=0;phase<4;phase++) {boot_phase=(int)phase;assert(transition_prebind_config(run) && transition_diag_config(run));assert(!memcmp(&previous,&transition_prebind,sizeof(previous)));}
    for(unsigned fault=0;fault<7;fault++) {
        if(fault==0) ++boot_started;if(fault==1) ++remote_port;if(fault==2) ++timeout_ms;
        if(fault==3) transition_closing=1;if(fault==4) ++transition_open;if(fault==5) transition_prebind.poison=1;if(fault==6) ++thunk;
        assert(!transition_prebind_config(run));
        if(fault==0) --boot_started;if(fault==1) --remote_port;if(fault==2) --timeout_ms;
        if(fault==3) transition_closing=0;if(fault==4) --transition_open;if(fault==5) transition_prebind.poison=0;if(fault==6) --thunk;
    }
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=previous_thunk;used=previous_used;patched_word=previous_patched;
}
static void test_transition_prebind(void) {
    test_prebind_config();
    prebind_fixture();unsigned before=calls;prebind_start();
    assert(!transition_prebind_action_check(30,0));
    assert(calls==before+1 && !transition_failures && transition_prebind.count==2 && !transition_prebind.pending &&
        transition_prebind.returned==1 && transition_saved_tickets_count==1 && !transition_saved_tickets[0].state && !transition_open);
    boot_phase=3;assert(transition_saved_bind() && transition_prebind.count==3 && transition_saved_tickets[0].state==1);
    assert(!transition_prebind_action_check(30,0));
    transition_prebind_ledger stable=transition_prebind;assert(transition_saved_bind() && !memcmp(&stable,&transition_prebind,sizeof(stable)));
    stats.issued=1;saved_dispatch();assert(calls==before+2 && !transition_failures && transition_saved_tickets[0].state==3);
    assert(transition_prebind_action_check(30,0));
    prebind_export("");
    /* A second dispatch of the tracked address must not disappear as an
     * unselected recipient after the genuine ticket has retired. */
    prebind_fixture();prebind_start();boot_phase=3;assert(transition_saved_bind());stats.issued=1;saved_dispatch();
    assert(transition_prebind_action_check(30,0));
    unsigned char *other_agent=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(other_agent);
    uint64_t other_object[2]={0,0x1234};*(void**)(other_agent+0xef0)=other_object;before=calls;
    world_queue_dispatch_observe(prebind_other_model,command,other_agent,(uintptr_t)(image+0xeacf66),native_tls);
    assert(calls==before+1 && transition_prebind.poison && transition_failures && transition_saved_queue_unknown==1 &&
        !transition_prebind_action_check(30,0) && transition_saved_tickets[0].state==3);
    prebind_export(".reused");
    assert(VirtualFree(other_agent,0,MEM_RELEASE));
    prebind_fixture();prebind_protected=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(prebind_protected);memcpy(prebind_protected,command,16);
    before=calls;transition_activate_observe(prebind_protected_activate,8,1,(uintptr_t)(image+0x1252026));
    assert(calls==before+1 && transition_prebind.returned==1 && !transition_prebind.poison);
    boot_phase=3;assert(transition_saved_bind()); /* command stays unreadable through ACK and bind */
    DWORD protection;assert(VirtualProtect(prebind_protected,4096,PAGE_READWRITE,&protection));stats.issued=1;
    world_queue_dispatch_observe(native_model,prebind_protected,agent,(uintptr_t)(image+0xeacf66),native_tls);
    assert(!transition_failures && transition_saved_tickets[0].state==3 && VirtualFree(prebind_protected,0,MEM_RELEASE));
    /* Later legitimate activation is unrelated to the one boot2 parent. */
    prebind_fixture();prebind_start();boot_phase=3;assert(transition_saved_bind());stats.issued=30;
    transition_activate_observe(prebind_empty_activate,8,1,(uintptr_t)(image+0x1252026));assert(!transition_prebind.poison);
    saved_dispatch();assert(!transition_failures);
    before=calls;prebind_enqueue();assert(calls==before+1 && transition_prebind.poison && transition_failures); /* retired address reuse stays denied */
    /* Exact command read failures: original still once, no ACK/bind proof. */
    for(prebind_fault=1;prebind_fault<=3;prebind_fault++) {
        prebind_fixture();diag_reads=0;before=calls;
        transition_activate_observe(prebind_read_activate,8,1,(uintptr_t)(image+0x1252026));
        assert(calls==before+1 && transition_prebind.poison && !transition_prebind.count && !transition_open);
        boot_phase=3;assert(!transition_saved_bind());
    }
    /* Mutating each native relationship at producer/bind is a refusal. */
    for(unsigned phase=0;phase<2;phase++) for(unsigned fault=0;fault<9;fault++) {
        prebind_fixture();before=calls;if(phase) prebind_start();
        if(fault==0) *(uint64_t*)(object+8)^=1ULL<<25;
        if(fault==1) *(void**)(object+0x200)=next_controller;
        if(fault==2) *(void**)(old_controller+0x40)=agent+16;
        if(fault==3) *(void**)(old_controller+0x30)=agent;
        if(fault==4) faction[0x10]='x';
        if(fault==5) *(void**)(agent+0xef0)=next_controller;
        if(fault==6) agent[0xf79]=0;
        if(fault==7) old_controller[0x58]=0;
        if(fault==8) *(void**)(agent+0xf10)=NULL;
        /* changed model can be a valid producer; only its later change is forbidden */
        if(!phase && fault!=0) prebind_start();
        boot_phase=3;int bound=transition_saved_bind();
        assert(bound==(!phase && fault==8));
        if(fault==0) *(uint64_t*)(object+8)^=1ULL<<25;
        if(!phase && fault!=0) assert(calls==before+1);
    }
    prebind_fixture();before=calls;transition_activate_observe(prebind_immediate_activate,8,1,(uintptr_t)(image+0x1252026));
    assert(calls==before+2 && transition_prebind.poison && !transition_prebind.finalized);boot_phase=3;assert(!transition_saved_bind());
    prebind_fixture();prebind_start();before=calls;saved_dispatch();assert(calls==before+1 && transition_prebind.poison);
    boot_phase=3;assert(!transition_saved_bind());
    prebind_fixture();prebind_start();transition_prebind_ack();assert(transition_prebind.poison && transition_prebind.returned==2);
    prebind_fixture();prebind_start();transition_activate_observe(prebind_empty_activate,8,1,(uintptr_t)(image+0x1252026));assert(transition_prebind.poison);
    prebind_fixture();prebind_start();transition_activate_observe(prebind_deactivate,8,0,(uintptr_t)(image+0x1252026));assert(transition_prebind.poison);
    /* Reservation and active formatter while borrowed reads are blocked. */
    FILE *inflight=NULL;const char *export_path=getenv("X4LAN_TRANSITION_PREBIND_TEST_LOG");char inflight_path[1024];
    if(export_path) {int n=snprintf(inflight_path,sizeof(inflight_path),"%s.inflight",export_path);assert(n>0 && (size_t)n<sizeof(inflight_path));inflight=fopen(inflight_path,"w");assert(inflight);}
    for(unsigned fault=0;fault<4;fault++) {
        prebind_fixture();before=calls;prebind_entered=CreateEventW(NULL,TRUE,FALSE,NULL);prebind_resume=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(prebind_entered && prebind_resume);HANDLE worker=CreateThread(NULL,0,prebind_worker,NULL,0,NULL);assert(worker);
        assert(WaitForSingleObject(prebind_entered,5000)==WAIT_OBJECT_0);
        assert(transition_prebind.reserved && transition_prebind.pending && transition_prebind.count==0 && transition_open==2);
        assert(lua_player_transition_prebind_status(NULL)==1 && strstr(diag_lua_text,"count=0 rows=0 source=1 returns=0 pending=1"));
        if(inflight && !fault) fprintf(inflight,"ACTIVE %s\n",diag_lua_text);
        if(fault==1) {transition_restore();assert(lua_player_transition_prebind_status(NULL)==1);if(inflight) fprintf(inflight,"STOP_OPEN %s\n",diag_lua_text);}
        if(fault==2) prebind_enqueue(); /* concurrent same-address attempt poisons reserved source */
        if(fault==3) saved_dispatch(); /* dispatch races the reserved, unread source */
        assert(SetEvent(prebind_resume) && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
        assert(CloseHandle(worker) && CloseHandle(prebind_entered) && CloseHandle(prebind_resume));
        assert(calls==before+1+(fault>=2) && !transition_open);
        if(fault) {assert(transition_prebind.poison);if(fault==1) {assert(transition_restore_open==2);assert(lua_player_transition_prebind_status(NULL)==1);if(inflight) fprintf(inflight,"STOP_RETURNED %s\n",diag_lua_text);}}
        else {assert(!transition_prebind.poison && transition_prebind.count==2);boot_phase=3;assert(transition_saved_bind());}
    }
    if(inflight) assert(!fclose(inflight));
    prebind_fixture();before=calls;entered=CreateEventW(NULL,TRUE,FALSE,NULL);resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(entered && resume);
    HANDLE original_worker=CreateThread(NULL,0,prebind_original_worker,NULL,0,NULL);assert(original_worker);
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0 && transition_prebind.count==1 && transition_prebind.pending && transition_open==2);
    transition_restore();assert(SetEvent(resume) && WaitForSingleObject(original_worker,5000)==WAIT_OBJECT_0);
    assert(calls==before+1 && transition_prebind.count==1 && transition_prebind.returned==1 && transition_prebind.poison && transition_restore_open==2 && !transition_open);
    assert(CloseHandle(original_worker) && CloseHandle(entered) && CloseHandle(resume));
    prebind_fixture();prebind_start();boot_phase=3;entered=CreateEventW(NULL,TRUE,FALSE,NULL);assert(entered);
    AcquireSRWLockExclusive(&transition_lock);prebind_bind_result=1;
    HANDLE binder=CreateThread(NULL,0,prebind_bind_worker,NULL,0,NULL);assert(binder && WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    transition_restore_locked();ReleaseSRWLockExclusive(&transition_lock);
    assert(WaitForSingleObject(binder,5000)==WAIT_OBJECT_0 && !prebind_bind_result && !transition_saved_bound && !transition_prebind.finalized && transition_prebind.count==2);
    assert(CloseHandle(binder) && CloseHandle(entered));main_thread=GetCurrentThreadId();
    prebind_fixture();before=calls;int caught=0;
    __try {transition_activate_observe(prebind_throw_activate,8,1,(uintptr_t)(image+0x1252026));}
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && calls==before+1 && transition_prebind.pending && !transition_prebind.returned &&
        transition_open==1 && transition_saved_queue_calls==1 && !transition_saved_queue_returns && transition_direct_unwinds==1);
    transition_restore();assert(transition_restore_open==1); /* no queue exception-cleanup claim */
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==0x123456789abcULL) world_identities[i].saved=0x176a13;
    saved_reset();reset_events();puts("prebind source/ACK/atomic bind, premature dispatch, reuse, guarded identity, concurrent STOP and honest exception debt PASS");
}
