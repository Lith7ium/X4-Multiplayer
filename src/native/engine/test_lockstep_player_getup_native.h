/* Reuses owned registry/event pages and the actual pinned post/dispatch adapters. */
static const char getup_test_run[]="3ba3f5f43b92602683c19aee62a20342b084dd5971ddd33808d81a328879a547";
static unsigned getup_test_calls,getup_test_drop,getup_test_local,getup_test_load_setup;
static double getup_test_result;
static double getup_test_number(lua_State *L,int index) {(void)L;assert(index==1 || index==3);return index==1?(double)stats.issued:1;}
static const char *getup_test_string(lua_State *L,int index,size_t *length) {
    (void)L;assert(index==2 || index==4 || index==5);const char *s=index==2?getup_test_run:inputs[index-4];*length=strlen(s);return s;
}
static void getup_test_pushnumber(lua_State *L,double value) {(void)L;getup_test_result=value;}
static bool getup_test_public(void) {
    ++getup_test_calls;assert(getup_request_id()==1);
    if(getup_test_drop) return true;
    ((porigin_post_fn)origin_test_code)(origin_test_player,origin_test_event,origin_test_due,1);return true;
}
static void getup_test_reset(void) {
    memset(&getup_ledger,0,sizeof(getup_ledger));gd_enabled=0;origin_test_reset();getup_test_calls=getup_test_drop=0;
    uint64_t runtime=*(uint64_t*)(object+8);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0xae79c;
    transition_saved_watch *w=&transition_saved_watches[0];w->saved=w->initial.saved=0xae79c;
    strcpy(w->macro,"ship_arg_s_scout_01_a_macro");strcpy((char*)saved_macro+0x100,w->macro);
    *(void**)(saved_macro+0x20)=saved_macro+0x100;*(uint64_t*)(saved_macro+0x30)=strlen(w->macro);*(uint64_t*)(saved_macro+0x38)=63;
    const char *token="1|0000000000011e12|00000000000ae79c|1|120";
    assert(origin_configure_transaction(token,strlen(token),origin_test_prepare,origin_swap));
    used=1;stats.slot=1;stats.report_tick=0;remote_port=62000;timeout_ms=60000;boot_started=100;boot_options=getup_test_local?GD_OPTIONS:70368744177665ULL;
    memset(initial_state_digest,'b',64);initial_state_digest[64]=0;
    /* Existing transition tests separately exercise emitter/CAS/unwind. This
     * owned fixture publishes its expected words for the action ownership guard. */
    for(unsigned i=0;i<3;i++) {
        DWORD old,ignored;assert(VirtualProtect(image+transition_entries[i],8,PAGE_EXECUTE_READWRITE,&old));
        transition_patched[i]=0x123400+i;memcpy(image+transition_entries[i],&transition_patched[i],8);
        assert(VirtualProtect(image+transition_entries[i],8,old,&ignored));
    }
    if(getup_test_local) {
        path_installed=1;path_options=1;world_identity_installed=1;ai_installed=1;ai_full_layout_verified=1;
        ship_installed=nav_installed=agent_installed=flight_installed=spline_installed=1;
        world_trace_early=1;world_trace_watch_count=1;world_queue_enabled=1;world_queue_sites=7;
        void *entry=*(void**)(image+SPLINE_ENTRY);assert(spline_swap(entry,(void*)spline_update));
    }
    memset(secret,'a',32);secret[32]=0;
    if(getup_test_load_setup) return;
    const char *contract=getup_test_local?GD_CONTRACT:NULL;
    assert(getup_configure_mode(getup_test_run,contract));assert(getup_configure_mode(getup_test_run,contract));
    pgetup_ledger pristine=getup_ledger;
    for(int phase=1;phase<=2;phase++) {boot_phase=phase;assert(getup_configure_mode(getup_test_run,contract));assert(!memcmp(&pristine,&getup_ledger,sizeof(pristine)));}
    boot_phase=2;origin_import_capture(origin_test_player,*(uint64_t*)(origin_test_player+8),0x11e12);
    boot_phase=3;assert(origin_bind());origin_test_executable();origin_test_register_unwind();
    origin_emit(image+0x1aba30,(void*)getup_test_public);FlushInstructionCache(GetCurrentProcess(),image+0x1aba30,32);
    strcpy(inputs[0],"00000000");strcpy(inputs[1],"00000000");
    for(unsigned i=1;i<30;i++) {stats.issued=i;stats.report_tick=i-1;assert(pgetup_batch(&getup_ledger,getup_test_run,1,i,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));}
    stats.issued=30;stats.report_tick=29;strcpy(inputs[0],"87010000");
}
static void getup_test_clean(void);
static void getup_test_attachment(void) {
    getup_test_reset();pgetup_ledger saved=getup_ledger;
    assert(getup_configure(getup_test_run) && !memcmp(&saved,&getup_ledger,sizeof(saved)));
    transition_diag_enabled=1;
    assert(!getup_configure(getup_test_run) && !memcmp(&saved,&getup_ledger,sizeof(saved)));
    transition_diag_enabled=0;
    for(unsigned fault=0;fault<9;fault++) {
        if(fault==0) ++boot_options;if(fault==1) ++boot_started;if(fault==2) ++stats.slot;if(fault==3) ++remote_port;
        if(fault==4) ++timeout_ms;if(fault==5) initial_state_digest[0]='c';if(fault==6) ++thunk;if(fault==7) ++origin_sites;
        if(fault==8) {wire_version=3;wire_clients=3;} /* GetUp binds the two-player protocol 2 wire */
        assert(!getup_configure(getup_test_run) && !memcmp(&saved,&getup_ledger,sizeof(saved)));
        if(fault==0) --boot_options;if(fault==1) --boot_started;if(fault==2) --stats.slot;if(fault==3) --remote_port;
        if(fault==4) --timeout_ms;if(fault==5) initial_state_digest[0]='b';if(fault==6) --thunk;if(fault==7) --origin_sites;
        if(fault==8) {wire_version=2;wire_clients=2;}
    }
    assert(!getup_configure("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    getup_test_clean();assert(!getup_configure(getup_test_run));
    getup_test_reset();origin_test_immediate=1;
    assert(pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
    saved=getup_ledger;assert(getup_configure(getup_test_run) && !memcmp(&saved,&getup_ledger,sizeof(saved)));
    assert(!pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
    assert(getup_test_calls==1 && getup_ledger.calls==1 && getup_ledger.claimed==1 && getup_ledger.failures);
    assert(!getup_configure(getup_test_run));getup_test_clean();
}
static void getup_test_clean(void) {
    pgetup_stop(&getup_ledger);origin_restore();assert(RtlDeleteFunctionTable(origin_test_unwind));
    VirtualFree(origin_test_code,0,MEM_RELEASE);
    uint64_t runtime=*(uint64_t*)(object+8);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0x176a13;
}
static void getup_test_extension_load(void) {
    getup_test_load_setup=1;getup_test_reset();getup_test_load_setup=0;
    transition_started=transition_saved_bound=0;
    strcpy(transition_saved_manifest,"2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player");
    strcpy(initial_state_digest,"5577ce9dae4dc4e016c7bf5d9048fdcc83a06fac7b388bc2bd08afe47b908d75");
    *(void**)(image+0x2c39488+0x198)=image+0xd8dd00;
    uint64_t previous_patched=patched_word;memcpy(&patched_word,image+WORD_RVA,8);
    void (*previous_push)(lua_State*,double)=p_lua_pushnumber;p_lua_pushnumber=diag_lua_number;
    assert(transition_prebind_config(getup_test_run));
    assert(lua_load_requested(NULL)==1 && !diag_lua_result && !boot_phase);
    assert(transition_diag_config(getup_test_run));
    assert(lua_load_requested(NULL)==1 && !diag_lua_result && !boot_phase);
    transition_started=1;
    assert(getup_configure_mode(getup_test_run,GD_CONTRACT));
    pgetup_ledger saved=getup_ledger;transition_prebind_ledger receipt=transition_prebind;
    for(unsigned fault=0;fault<14;fault++) {
        if(fault==0) transition_diag_enabled=0;if(fault==1) gd_enabled=0;if(fault==2) getup_ledger.configured=0;
        if(fault==3) getup_ledger.closing=1;if(fault==4) getup_ledger.failures=1;if(fault==5) getup_ledger.run[0]^=1;
        if(fault==6) transition_prebind.run[0]^=1;if(fault==7) transition_diag_run[0]^=1;if(fault==8) ++boot_started;
        if(fault==9) ++origin_sites;if(fault==10) transition_closing=1;if(fault==11) transition_prebind.poison=1;
        if(fault==12) ++origin_ledger.failures;if(fault==13) ++getup_session.options;
        assert(lua_load_requested(NULL)==1 && !diag_lua_result && !boot_phase);
        if(fault==0) transition_diag_enabled=1;if(fault==1) gd_enabled=1;if(fault==2) getup_ledger.configured=1;
        if(fault==3) getup_ledger.closing=0;if(fault==4) getup_ledger.failures=0;if(fault==5) getup_ledger.run[0]^=1;
        if(fault==6) transition_prebind.run[0]^=1;if(fault==7) transition_diag_run[0]^=1;if(fault==8) --boot_started;
        if(fault==9) --origin_sites;if(fault==10) transition_closing=0;if(fault==11) transition_prebind.poison=0;
        if(fault==12) --origin_ledger.failures;if(fault==13) --getup_session.options;
    }
    assert(lua_load_requested(NULL)==1 && diag_lua_result==1 && boot_phase==1);
    for(unsigned phase=1;phase<=2;phase++) {
        boot_phase=(int)phase;assert(transition_prebind_config(getup_test_run) && transition_diag_config(getup_test_run) && getup_configure_mode(getup_test_run,GD_CONTRACT));
        assert(!memcmp(&saved,&getup_ledger,sizeof(saved)) && !memcmp(&receipt,&transition_prebind,sizeof(receipt)));
        assert(lua_load_requested(NULL)==1 && !diag_lua_result && boot_phase==(int)phase);
    }
    pgetup_stop(&getup_ledger);boot_phase=0;
    assert(lua_load_requested(NULL)==1 && !diag_lua_result && !boot_phase);
    origin_restore();assert(!getup_extensions_load_ready());p_lua_pushnumber=previous_push;patched_word=previous_patched;
    uint64_t runtime=*(uint64_t*)(object+8);
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) if(world_identities[i].runtime==runtime) world_identities[i].saved=0x176a13;
}
static void getup_test_diagnostic_attachment(void) {
    /* Owned synthetic observer installation state; actual emitter and source
     * pins are independently tested by this exact-image executable. */
    int *flags[]={&path_installed,&world_identity_installed,&ai_installed,&ship_installed,&nav_installed,
        &agent_installed,&flight_installed,&spline_installed};
    int previous[sizeof(flags)/sizeof(flags[0])];for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) previous[i]=*flags[i];
    unsigned old_early=world_trace_early,old_count=world_trace_watch_count,old_enabled=world_queue_enabled,old_sites=world_queue_sites;
    unsigned old_verified=ai_full_layout_verified;uint64_t old_options=path_options;
    void *old_spline_entry=*(void**)(image+SPLINE_ENTRY);
    getup_test_local=1;getup_test_reset();pgetup_ledger saved=getup_ledger;
    assert(gd_enabled && getup_session.local_mode && getup_session.options==GD_OPTIONS);
    assert(!getup_configure(getup_test_run) && !getup_configure_mode(getup_test_run,"unknown"));
    assert(spline_swap((void*)spline_update,image+SPLINE_UPDATE));
    assert(!getup_configure_mode(getup_test_run,GD_CONTRACT) && !memcmp(&saved,&getup_ledger,sizeof(saved)));
    assert(spline_swap(image+SPLINE_UPDATE,(void*)spline_update));
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        int value=*flags[i];*flags[i]=0;assert(!getup_configure_mode(getup_test_run,GD_CONTRACT));*flags[i]=value;
        assert(!memcmp(&saved,&getup_ledger,sizeof(saved)));
    }
    assert(getup_configure_mode(getup_test_run,GD_CONTRACT));origin_test_immediate=1;
    assert(pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
    saved=getup_ledger;assert(getup_configure_mode(getup_test_run,GD_CONTRACT));
    assert(!getup_configure(getup_test_run) && !memcmp(&saved,&getup_ledger,sizeof(saved)) && getup_test_calls==1);
    origin_test_api_setup();assert(lua_player_getup_status(NULL)==1 && strstr(origin_test_text,"schema=2 contract=diagnostic-getup-1 native_clients=1 parity_checked=0"));
    assert(lua_player_getup_diagnostic_status(NULL)==1 && strstr(origin_test_text,"options=70377334178981") && strstr(origin_test_text,"session_pass=0 parity_checked=0 independent_native_players=0 completed_release=0"));
    FILE *status=fopen(".local/native-lockstep/getup-diagnostic-c-status.txt","wb");assert(status);
    assert(fputs(origin_test_text,status)>=0 && !fclose(status));
    getup_test_clean();assert(!getup_configure_mode(getup_test_run,GD_CONTRACT));
    getup_test_extension_load();getup_test_local=gd_enabled=0;
    assert(spline_swap((void*)spline_update,old_spline_entry));
    for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) *flags[i]=previous[i];
    world_trace_early=old_early;world_trace_watch_count=old_count;world_queue_enabled=old_enabled;world_queue_sites=old_sites;
    ai_full_layout_verified=old_verified;path_options=old_options;
}
static void getup_test_prebind_action(void) {
    /* The prospective producer/bind harness proves these local ledger states.
     * Here the real action guard and pinned public forwarder are exercised. */
    for(unsigned fault=0;fault<10;fault++) {
        getup_test_reset();origin_test_immediate=1;transition_prebind_enabled=1;
        transition_prebind=(transition_prebind_ledger){.reserved=1,.returned=1,.finalized=1,.count=3,.ticket=1,.event=2,.command=0x70000};
        transition_saved_tickets_count=1;
        transition_saved_tickets[0]=(transition_saved_ticket){.state=3,.event=2,.dispatch=1,.command=0x70000};
        transition_count=1;transition_order=2;
        transition_events[0]=(transition_event){.kind=TR_MODEL_DISPATCH,.boot=3,.tick=1,.enter=1,.leave=2,
            .complete=1,.queue_valid=1,.queue=1,.enqueue=1};
        if(fault==1) {transition_prebind.count=1;transition_prebind.pending=1;transition_prebind.returned=transition_prebind.finalized=0;}
        if(fault==2) {transition_prebind.count=2;transition_prebind.finalized=0;}
        if(fault==3) transition_saved_tickets[0].state=1;
        if(fault==4) {transition_saved_tickets[0].state=2;transition_open=1;transition_events[0].complete=0;}
        if(fault==5) transition_prebind.poison=1;
        if(fault==6) transition_closing=1;
        if(fault==7) transition_events[0].errors=32;
        if(fault==8) transition_events[0].tick=30;
        if(fault==9) transition_events[0].complete=0;
        int accepted=pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30));
        assert(accepted==(fault==0) && getup_test_calls==(fault==0) && getup_ledger.calls==(fault==0));
        if(!fault) {
            assert(transition_prebind.action_tick==30 && transition_prebind.action_clock==2 &&
                transition_prebind.action_thread==GetCurrentThreadId() && transition_prebind.action_ticket==1 && transition_prebind.action_dispatch==1);
            transition_prebind_ledger receipt=transition_prebind;
            assert(!transition_prebind_action_check(30,1) && !memcmp(&receipt,&transition_prebind,sizeof(receipt)));
            origin_test_api_setup();assert(lua_player_transition_prebind_status(NULL)==1 && strstr(origin_test_text,"action_tick=30 action_clock=2"));
        } else assert(!transition_prebind.action_tick);
        if(fault) {
            /* A later retirement cannot repair the action's permanent refusal. */
            transition_prebind.count=3;transition_prebind.pending=transition_prebind.poison=0;
            transition_prebind.returned=transition_prebind.finalized=1;transition_open=transition_closing=0;
            transition_saved_tickets[0].state=3;transition_events[0].complete=1;transition_events[0].errors=0;transition_events[0].tick=1;
            assert(transition_prebind_action_check(30,0));
            assert(!pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
            assert(!getup_test_calls && !getup_ledger.calls && !transition_prebind.action_tick);
        }
        getup_test_clean();
    }
    transition_prebind_enabled=0;assert(transition_prebind_action_check(30,1));
}
static void test_getup_native(void) {
    getup_test_prebind_action();
    if(!origin_test_actual) return;
    getup_test_diagnostic_attachment();getup_test_attachment();
    for(unsigned immediate=0;immediate<2;immediate++) {
        getup_test_reset();origin_test_immediate=immediate;
        p_lua_tonumber=getup_test_number;p_lua_tolstring=getup_test_string;p_lua_pushnumber=getup_test_pushnumber;
        assert(lua_player_getup_batch(NULL)==1 && getup_test_result==1);
        assert(getup_test_calls==1 && getup_ledger.accepted && origin_ledger.count==1 && origin_ledger.tickets[0].request==1);
        if(!immediate) {stats.issued=31;origin_test_invoke(origin_test_player,origin_test_player,origin_test_due,0xcf,origin_test_event);}
        porigin_ticket *t=&origin_ledger.tickets[0];assert(t->post_enter<t->post_leave && t->delivery_enter<t->delivery_leave && t->transition_after>t->transition_before);
        assert(immediate?t->delivery_leave<t->post_leave:t->post_leave<t->delivery_enter);
        porigin_sample sample;transition_state state;unsigned phase;assert(getup_view(&sample,&state,&phase) && phase==4);
        pgetup_ledger before_attach=getup_ledger;
        assert(getup_configure(getup_test_run) && !memcmp(&before_attach,&getup_ledger,sizeof(before_attach)) && getup_test_calls==1);
        origin_test_api_setup();assert(lua_player_origin_ticket(NULL)==1 && origin_test_nil);pgetup_stop(&getup_ledger);origin_restore();
        const char *export_path=getenv("X4LAN_GETUP_TEST_LOG");
        if(!immediate && export_path) {origin_test_export=fopen(export_path,"w");assert(origin_test_export);}
        assert(lua_player_origin_ticket(NULL)==1 && strstr(origin_test_text,"schema=3") && strstr(origin_test_text,"request=1"));
        if(origin_test_export) {
            for(origin_test_row=1;origin_test_row<=origin_ledger.link_count;origin_test_row++) assert(lua_player_origin_edge(NULL)==1);
            assert(lua_player_origin_status(NULL)==1);
            origin_test_row=1;assert(lua_player_transition_watch(NULL)==1);
            for(origin_test_row=1;origin_test_row<=transition_ready();origin_test_row++) assert(lua_player_transition_row(NULL)==1);
            origin_test_export_transition_status=1;assert(lua_player_transition_status(NULL)==1);origin_test_export_transition_status=0;
            FILE *out=origin_test_export;origin_test_export=NULL;assert(lua_player_getup_status(NULL)==1);
            fprintf(out,"NATIVE_GATE PLAYER_GETUP_STOP %s\n",origin_test_text);assert(!fclose(out));
        }
        getup_test_clean();
    }
    for(unsigned mode=0;mode<4;mode++) {
        getup_test_reset();origin_test_immediate=1;
        assert(pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
        porigin_sample sample;transition_state state;unsigned phase;
        if(mode&1) assert(getup_view(&sample,&state,&phase) && phase==4);
        unsigned char *replacement=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(replacement);
        memcpy(replacement,next_controller,4096);*(void**)replacement=image+((mode&2)?0x2c51158:0x2c39488);
        *(void**)(object+0x200)=replacement;agent[0xf79]=(mode&2)?0:1;
        stats.issued=32;stats.report_tick=31;strcpy(inputs[0],"00000000");
        assert(!getup_view(&sample,&state,&phase));assert(!getup_guard(NULL,32,0));
        assert(getup_ledger.publication==2 && getup_ledger.published_controller==(uintptr_t)next_controller && getup_ledger.failures);
        *(void**)(object+0x200)=next_controller;agent[0xf79]=0;
        assert(!getup_view(&sample,&state,&phase)); /* Failure is sticky; no inferred repair. */
        VirtualFree(replacement,0,MEM_RELEASE);getup_test_clean();
    }
    for(unsigned fault=0;fault<9;fault++) {
        getup_test_reset();
        if(fault==0) stats.active=0;
        if(fault==1) strcpy(inputs[1],"87010000");
        if(fault==2) ++stats.report_tick;
        if(fault==3) *(uint64_t*)(origin_test_player+8)^=1ULL<<25;
        if(fault==4) *(void**)(old_controller+0x40)=agent+16;
        if(fault==5) faction[0x10]='x';
        if(fault==6) pgetup_stop(&getup_ledger);
        if(fault==7) origin_ledger.count=1;
        if(fault==8) origin_ledger.count=2;
        assert(!pgetup_batch(&getup_ledger,getup_test_run,1,30,"87010000","00000000",getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
        assert(!getup_test_calls && !getup_ledger.calls);getup_test_clean();
    }
    getup_test_reset();getup_test_drop=1;
    assert(pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30)));
    porigin_sample sample;transition_state state;unsigned phase;
    assert(getup_ledger.accepted && getup_test_calls==1 && !getup_view(&sample,&state,&phase));getup_test_clean();
    getup_test_reset();origin_test_raise_post=1;unsigned caught=0;
    __try {pgetup_batch(&getup_ledger,getup_test_run,1,30,inputs[0],inputs[1],getup_guard,NULL,(pgetup_original)(image+0x1aba30));}
    __except(GetExceptionCode()==ORIGIN_TEST_EXCEPTION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && getup_test_calls==1 && getup_ledger.unwinds==1 && !getup_ledger.open && !getup_request_id() && !porigin_current);
    DWORD ignored;assert(VirtualProtect(origin_test_event,4096,PAGE_READWRITE,&ignored));getup_test_clean();
    puts("GetUp native guard, retained publication, same-session reattachment/replay, public bool ABI, request/origin/direct links, deferred/immediate and unwind PASS");
}
