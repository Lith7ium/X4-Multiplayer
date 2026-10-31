static unsigned diag_reads,diag_fault;
static int diag_reader(const void *source,void *out,size_t size) {
    if(source==command) {
        unsigned step=++diag_reads;
        if(diag_fault==step) {memset(out,0xa5,size);return 0;}
        int ok=read_local(source,out,size);
        if(diag_fault==4 && step==2) command[15]^=1;
        if(diag_fault==5 && step==1) *(void**)command=image+WORLD_QUEUE_ABORT_VTABLE;
        return ok;
    }
    return read_local(source,out,size);
}
static void diag_prebind_activation(int context,unsigned char active) {
    assert(context==8 && active==1);saved_enqueue();image[0x2f4b828]=1;
}
static double diag_lua_result;
static char diag_lua_text[TRANSITION_ROW_CAP];
static void diag_lua_number(lua_State *L,double value) {(void)L;diag_lua_result=value;}
static void diag_lua_string(lua_State *L,const char *value,size_t length) {
    (void)L;assert(length==strlen(value) && length<sizeof(diag_lua_text));memcpy(diag_lua_text,value,length+1);
}
static void diag_lua_nil(lua_State *L) {(void)L;diag_lua_text[0]=0;}
static void test_diag_load_binding(void) {
    saved_fixture_stage(1,0);boot_phase=0;transition_diag_enabled=1;
    pgetup_ledger previous=getup_ledger;unsigned previous_gd=gd_enabled;
    void (*previous_push)(lua_State*,double)=p_lua_pushnumber;p_lua_pushnumber=diag_lua_number;
    memset(&getup_ledger,0,sizeof(getup_ledger));getup_ledger.configured=1;
    strcpy(getup_ledger.run,transition_diag_run);gd_enabled=1;
    for(unsigned fault=0;fault<5;fault++) {
        if(fault==0) gd_enabled=0;if(fault==1) getup_ledger.configured=0;if(fault==2) getup_ledger.closing=1;
        if(fault==3) getup_ledger.failures=1;if(fault==4) getup_ledger.run[0]^=1;
        assert(lua_load_requested(NULL)==1 && diag_lua_result==0 && boot_phase==0);
        if(fault==0) gd_enabled=1;if(fault==1) getup_ledger.configured=1;if(fault==2) getup_ledger.closing=0;
        if(fault==3) getup_ledger.failures=0;if(fault==4) getup_ledger.run[0]^=1;
    }
    /* Marker/ledger bits alone are not a complete native dependency chain. */
    assert(lua_load_requested(NULL)==1 && diag_lua_result==0 && boot_phase==0);
    boot_phase=0;transition_diag_enabled=0;gd_enabled=0;getup_ledger.configured=0;
    assert(lua_load_requested(NULL)==1 && diag_lua_result==1 && boot_phase==1);
    getup_ledger=previous;gd_enabled=previous_gd;p_lua_pushnumber=previous_push;
}
static void test_diag_export(void) {
    /* Construct the selected saved fixture before any observation. No captured
     * event is rewritten to manufacture the expected identity or receipt. */
    saved_fixture_stage(1,0);uint64_t runtime=*(uint64_t*)(object+8);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0xae79c;
    const char *manifest="2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player";unsigned count=0;
    memset(transition_saved_watches,0,sizeof(transition_saved_watches));
    assert(transition_saved_parse(manifest,strlen(manifest),120,transition_saved_watches,&count) && count==1);
    strcpy(transition_saved_manifest,manifest);transition_saved_import_calls=0;transition_saved_prebind=0;
    strcpy((char*)saved_macro+0x100,"ship_arg_s_scout_01_a_macro");*(void**)(saved_macro+0x20)=saved_macro+0x100;
    *(uint64_t*)(saved_macro+0x30)=strlen((char*)saved_macro+0x100);*(uint64_t*)(saved_macro+0x38)=63;
    memcpy(faction+0x10,"player",7);*(uint64_t*)(faction+0x20)=6;
    transition_import_capture(object,runtime,0xae79c);transition_diag_enabled=1;
    transition_activate_observe(diag_prebind_activation,8,1,(uintptr_t)(image+0x1252026));
    boot_phase=3;assert(transition_saved_bind());stats.issued=1;saved_dispatch();
    assert(transition_diag_count==2 && transition_count==2 && transition_events[1].errors==48 && !transition_saved_tickets_count);
    const char *path=getenv("X4LAN_TRANSITION_QUEUE_DIAG_TEST_LOG");FILE *out=path?fopen(path,"w"):NULL;assert(!path || out);
    p_lua_pushlstring=diag_lua_string;p_lua_pushnil=diag_lua_nil;p_lua_tonumber=evidence_number;
    assert(lua_player_transition_queue_evidence_status(NULL)==1 && strstr(diag_lua_text,"count=2 rows=0 calls=2"));
    if(out) fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_QUEUE_DIAG_ENABLED schema=1 resumed=0 run=%s allocation_generation_known=0 ticket_authority=0\n",transition_diag_run);
    for(evidence_row=1;evidence_row<=2;evidence_row++) {
        assert(lua_player_transition_queue_evidence_row(NULL)==1 && strstr(diag_lua_text,"PLAYER_TRANSITION_QUEUE_DIAG schema=1"));
        if(out) fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);
    }
    evidence_row=2;assert(lua_player_transition_queue_evidence_row(NULL)==1 && !diag_lua_text[0]);
    assert(transition_diag_exported==2);transition_restore();
    assert(lua_player_transition_queue_evidence_status(NULL)==1 && strstr(diag_lua_text,"count=2 rows=2 calls=2"));
    if(out) fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_QUEUE_DIAG_STOP %s\n",diag_lua_text);
    evidence_row=1;assert(lua_player_transition_watch(NULL)==1 && strstr(diag_lua_text,"PLAYER_TRANSITION_WATCH schema=2"));
    if(out) fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);
    for(evidence_row=1;evidence_row<=transition_ready();evidence_row++) {
        assert(lua_player_transition_row(NULL)==1 && strstr(diag_lua_text,"PLAYER_TRANSITION event="));
        if(out) fprintf(out,"NATIVE_GATE %s\n",diag_lua_text);
    }
    assert(lua_player_transition_status(NULL)==1 && strstr(diag_lua_text,"failures=1"));
    if(out) {fprintf(out,"NATIVE_GATE PLAYER_TRANSITION_STOP %s\n",diag_lua_text);assert(!fclose(out));}
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0x176a13;
}
static void test_transition_diag_config(void) {
    const char *run="84e0c0eafaa95a34c293f278ac52e45ce537bab5e752a00e6959a13ae103b65a";
    saved_fixture(1);transition_started=0;transition_saved_bound=0;boot_phase=0;stats.issued=0;stats.slot=1;
    int previous_used=used;uint64_t previous_patched=patched_word;used=1;memcpy(&patched_word,image+WORD_RVA,8);
    unsigned char *previous_thunk=thunk;thunk=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(thunk);
    boot_options=70377334178981ULL;boot_started=123;remote_port=45678;timeout_ms=60000;
    strcpy(secret,"00000000000000000000000000000000");
    strcpy(initial_state_digest,"bd14169fada13d6d918437df27fa6c36281736525138300fbdc674a3024b581d");
    strcpy(transition_saved_manifest,"2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player");
    for(unsigned fault=0;fault<9;fault++) {
        if(fault==0) initial_state_digest[0]='0';if(fault==1) secret[0]='1';if(fault==2) boot_phase=1;
        if(fault==3) stats.issued=1;if(fault==4) stats.slot=2;if(fault==5) ++boot_options;if(fault==6) transition_started=1;
        if(fault==7) used=0;if(fault==8) ++patched_word;
        assert(!transition_diag_config(run) && !transition_diag_enabled);
        if(fault==0) initial_state_digest[0]='b';if(fault==1) secret[0]='0';if(fault==2) boot_phase=0;
        if(fault==3) stats.issued=0;if(fault==4) stats.slot=1;if(fault==5) --boot_options;if(fault==6) transition_started=0;
        if(fault==7) used=1;if(fault==8) --patched_word;
    }
    assert(transition_diag_config(run));transition_started=1;transition_sites=7;
    for(unsigned i=0;i<3;i++) memcpy(&transition_patched[i],image+transition_entries[i],8);
    transition_diag_count=2;transition_diag_exported=1;transition_diag_calls=7;
    for(unsigned phase=0;phase<=3;phase++) {boot_phase=(int)phase;assert(transition_diag_config(run));}
    assert(transition_diag_count==2 && transition_diag_exported==1 && transition_diag_calls==7);
    for(unsigned fault=0;fault<9;fault++) {
        if(fault==0) ++boot_started;if(fault==1) ++remote_port;if(fault==2) ++timeout_ms;if(fault==3) ++thunk;
        if(fault==4) ++transition_open;if(fault==5) transition_closing=1;if(fault==6) stats.active=0;
        if(fault==7) image[transition_entries[0]]^=1;if(fault==8) transition_saved_queue_failures=1;
        assert(!transition_diag_config(run));
        if(fault==0) --boot_started;if(fault==1) --remote_port;if(fault==2) --timeout_ms;if(fault==3) --thunk;
        if(fault==4) --transition_open;if(fault==5) transition_closing=0;if(fault==6) stats.active=1;
        if(fault==7) image[transition_entries[0]]^=1;if(fault==8) transition_saved_queue_failures=0;
    }
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=previous_thunk;used=previous_used;patched_word=previous_patched;saved_reset();
}
static HANDLE diag_capture_entered,diag_capture_resume;
static int diag_paused_reader(const void *source,void *out,size_t size) {
    if(source==command) {
        assert(SetEvent(diag_capture_entered));
        assert(WaitForSingleObject(diag_capture_resume,5000)==WAIT_OBJECT_0);
    }
    return read_local(source,out,size);
}
static DWORD WINAPI diag_capture_worker(void *unused) {
    (void)unused;assert(transition_saved_queue_admit());
    transition_diag_capture(diag_paused_reader,1,command,object,*(uint64_t*)(object+8),(uintptr_t)(image+0xeac95a));
    native_enqueue(object,*(uint64_t*)(object+8),command);transition_saved_queue_leave();return 0;
}
static void test_diag_capture_publication(void) {
    char path[1024];const char *base=getenv("X4LAN_TRANSITION_QUEUE_DIAG_TEST_LOG");FILE *out=NULL;
    if(base) {int n=snprintf(path,sizeof(path),"%s.inflight",base);assert(n>0 && (size_t)n<sizeof(path));out=fopen(path,"w");assert(out);}
    for(unsigned stop=0;stop<2;stop++) {
        saved_fixture(1);transition_diag_enabled=1;unsigned before=calls;
        p_lua_pushlstring=diag_lua_string;p_lua_pushnil=diag_lua_nil;
        assert(lua_player_transition_queue_evidence_status(NULL)==1);
        if(out && !stop) fprintf(out,"IDLE %s\n",diag_lua_text);
        diag_capture_entered=CreateEventW(NULL,TRUE,FALSE,NULL);diag_capture_resume=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(diag_capture_entered && diag_capture_resume);
        HANDLE worker=CreateThread(NULL,0,diag_capture_worker,NULL,0,NULL);assert(worker);
        assert(WaitForSingleObject(diag_capture_entered,5000)==WAIT_OBJECT_0);
        /* Real admission remains open, while all published outcome counters
         * still describe the empty completed capture set. No native lock is
         * held during the deliberately blocked borrowed-memory read. */
        assert(transition_saved_queue_calls==1 && !transition_diag_calls && !transition_diag_count && transition_open==1 && !transition_failures);
        assert(lua_player_transition_queue_evidence_status(NULL)==1 && strstr(diag_lua_text,"count=0 rows=0 calls=0"));
        if(out) fprintf(out,"%s %s\n",stop?"BEFORE_STOP":"ACTIVE",diag_lua_text);
        if(stop) {
            transition_restore();assert(transition_restore_open==1 && transition_closing);
            assert(lua_player_transition_queue_evidence_status(NULL)==1);
            if(out) fprintf(out,"STOP_OPEN %s\n",diag_lua_text);
        }
        assert(SetEvent(diag_capture_resume) && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
        assert(calls==before+1 && transition_saved_queue_returns==1 && transition_diag_calls==1 && transition_diag_count==1 && !transition_open);
        assert(!transition_failures && transition_restore_open==stop);
        assert(lua_player_transition_queue_evidence_status(NULL)==1 && strstr(diag_lua_text,"count=1 rows=0 calls=1"));
        if(out) fprintf(out,"%s %s\n",stop?"STOP_RETURNED":"RETURNED",diag_lua_text);
        CloseHandle(worker);CloseHandle(diag_capture_entered);CloseHandle(diag_capture_resume);
    }
    if(out) assert(!fclose(out));
    puts("copied queue atomic outcome publication during borrowed read, original-once and retained STOP-open debt PASS");
}
static void test_transition_queue_diag(void) {
    test_transition_diag_config();test_diag_load_binding();test_diag_export();test_diag_capture_publication();
    /* Exactly the existing missing-ticket refusal survives observation. The
     * early typed copy is separate and cannot manufacture a valid receipt. */
    transition_event old[2];uint64_t prebind=0,unknown=0;
    for(unsigned enabled=0;enabled<=1;enabled++) {
        saved_fixture_stage(1,0);transition_diag_enabled=enabled;
        unsigned before=calls;
        transition_activate_observe(diag_prebind_activation,8,1,(uintptr_t)(image+0x1252026));
        assert(calls==before+1 && !transition_saved_tickets_count);
        boot_phase=3;assert(transition_saved_bind());stats.issued=1;saved_dispatch();
        assert(calls==before+2 && transition_count==2 && transition_events[1].errors==48 && !transition_saved_tickets_count);
        assert(!transition_open && transition_saved_queue_calls==2 && transition_saved_queue_returns==2);
        if(!enabled) {memcpy(old,transition_events,sizeof(old));prebind=transition_saved_prebind;unknown=transition_saved_queue_unknown;}
        else {
            assert(!memcmp(old,transition_events,sizeof(old)) && prebind==(uint64_t)transition_saved_prebind && unknown==(uint64_t)transition_saved_queue_unknown);
            assert(transition_diag_count==2 && transition_diag_calls==2 && !transition_diag_unknown);
            transition_diag_row *q=&transition_diag_rows[0],*d=&transition_diag_rows[1];
            assert(q->kind==1 && !q->bound && q->decode==1 && q->reads==7 && q->parent==1 && q->parent_kind==3);
            assert(q->manager_match && q->manager_reads && q->order==1 && q->selection==1);
            assert(d->kind==2 && d->bound && d->source==1 && d->candidates==1 && d->byte_match && d->recipient_reads==2);
            assert(d->selection==1 && d->runtime==q->runtime && !d->parent && d->decode==1);
        }
    }
    /* These controlled readers exercise successful, failed/partial and changed
     * native reads; failed output bytes cannot appear as observed zero/data. */
    for(unsigned fault=0;fault<=5;fault++) {
        saved_fixture(1);transition_diag_row r={0};diag_reads=0;diag_fault=fault;
        transition_diag_decode(diag_reader,command,&r);
        const unsigned outcomes[]={1,3,5,6,7,8},masks[]={7,0,1,3,7,7};
        assert(r.decode==outcomes[fault] && r.reads==masks[fault]);
        unsigned char zero[16]={0};if(!(r.reads&2)) assert(!memcmp(r.first,zero,16));
        if(!(r.reads&4)) assert(!memcmp(r.second,zero,16));
    }
    saved_fixture(1);transition_diag_row r={0};*(void**)command=image-1;transition_diag_decode(read_local,command,&r);assert(r.decode==4 && r.reads==1);
    memset(&r,0,sizeof(r));*(void**)command=image+WORLD_QUEUE_ABORT_VTABLE;transition_diag_decode(read_local,command,&r);assert(r.decode==2 && r.reads==1);
    memset(&r,0,sizeof(r));*(void**)command=image+WORLD_QUEUE_MODEL_VTABLE;*(unsigned*)(command+8)=7;transition_diag_decode(read_local,command,&r);assert(r.decode==9 && r.reads==7);
    /* Unknown or aliased imported identity cannot authorize command reads. */
    saved_fixture(1);transition_diag_enabled=1;diag_reads=0;diag_fault=0;
    transition_diag_capture(diag_reader,1,command,object,0x999,(uintptr_t)(image+1));
    assert(!diag_reads && transition_diag_unknown==1 && !transition_diag_count);
    transition_saved_watches[1]=transition_saved_watches[0];transition_saved_count=2;
    transition_diag_capture(diag_reader,1,command,object,*(uint64_t*)(object+8),(uintptr_t)(image+1));
    assert(!diag_reads && transition_diag_count==1 && transition_diag_rows[0].selection==2 && !transition_diag_rows[0].reads);
    saved_fixture(1);transition_diag_enabled=1;
    transition_diag_capture(read_local,2,command,NULL,0,(uintptr_t)(image+0xeacf66));
    assert(transition_diag_unreadable==1 && transition_diag_unknown==1 && !transition_diag_count);
    *(uint64_t*)(object+8)^=1ULL<<25;
    transition_diag_capture(read_local,2,command,agent,0,(uintptr_t)(image+0xeacf66));
    assert(transition_diag_unknown==2 && !transition_diag_count);
    /* Address reuse or duplicate producers remain explicitly ambiguous. */
    saved_fixture(1);transition_diag_enabled=1;saved_enqueue();saved_dispatch();
    saved_enqueue();saved_dispatch();assert(transition_diag_rows[3].candidates==2 && !transition_diag_rows[3].source && !transition_diag_rows[3].byte_match);
    /* Consumer can run before enqueue returns. No later transferred pointer
     * reads are needed by the copied records. */
    saved_fixture(1);transition_diag_enabled=1;unsigned before=calls;
    world_queue_enqueue_observe(saved_immediate,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xeac95a),native_tls);
    assert(calls==before+2 && transition_diag_count==2 && transition_diag_rows[1].source==1 && !transition_open);
    saved_fixture(1);transition_diag_enabled=1;unsigned char *poison=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(poison);memcpy(poison,command,16);before=calls;
    world_queue_enqueue_observe(saved_poison,object,*(uint64_t*)(object+8),poison,(uintptr_t)(image+0xeac95a),native_tls);
    assert(calls==before+1 && transition_diag_count==1 && transition_diag_rows[0].decode==1 && !transition_open);
    DWORD ignored;assert(VirtualProtect(poison,4096,PAGE_READWRITE,&ignored));VirtualFree(poison,0,MEM_RELEASE);
    /* Bounded storage refuses completeness without wrapping or erasing rows. */
    saved_fixture(1);transition_diag_enabled=1;
    for(unsigned i=0;i<TRANSITION_DIAG_MAX+1;i++) transition_diag_capture(read_local,1,command,object,*(uint64_t*)(object+8),(uintptr_t)(image+0xeac95a));
    assert(transition_diag_count==TRANSITION_DIAG_MAX && transition_diag_dropped==1 && transition_diag_calls==TRANSITION_DIAG_MAX+1);
    transition_diag_row maximal;memset(&maximal,0xff,sizeof(maximal));char line[1024];
    int length=transition_diag_format(&maximal,UINT32_MAX,line,sizeof(line));assert(length>0 && length<1024 && strlen(line)==(size_t)length);
    char tiny[8];assert(transition_diag_format(&maximal,1,tiny,sizeof(tiny))==-1);
    /* STOP during original keeps both copied diagnostics and the existing
     * open-at-restoration failure. No later call can reopen the observer. */
    saved_fixture(1);transition_diag_enabled=1;entered=CreateEventW(NULL,TRUE,FALSE,NULL);resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(entered && resume);
    HANDLE worker=CreateThread(NULL,0,saved_queue_worker,NULL,0,NULL);assert(worker && WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    assert(transition_diag_count==1 && transition_open==1);transition_restore();assert(transition_restore_open==1);
    assert(SetEvent(resume) && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);before=calls;saved_enqueue();
    assert(calls==before+1 && transition_diag_count==1 && !transition_open && transition_restore_open==1);
    CloseHandle(worker);CloseHandle(entered);CloseHandle(resume);
    puts("copied prebind/missing-ticket diagnostics preserve strict legacy rows, bounded reads, original-once and STOP PASS");
}
