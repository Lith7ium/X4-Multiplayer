/* Piloting prototype: record codec, script, scheme mapping, wire staging,
 * frame table, request substitution, suppression ownership, restoration, pins. */
static unsigned char test_pilot_objects[2][0x300],test_pilot_controllers[2][0x60],test_pilot_agents[2][0x1000];
static unsigned char test_pilot_solvers[2][0x13000];
static uint64_t test_pilot_runtimes[2]={0xa100,0xa200};
static double test_pilot_seen;
static unsigned test_pilot_calls;
static uint32_t test_pilot_counter;
static unsigned char test_pilot_contexts=1,test_pilot_flight=1,test_pilot_enabled=1;
static unsigned test_pilot_refresh_calls,test_pilot_authority_value=2;
static void *test_pilot_find(void *map,uint64_t runtime,unsigned kind) {
    assert(map==(void*)test_pilot_runtimes && kind==4);
    for(unsigned i=0;i<2;i++) if(runtime==test_pilot_runtimes[i]) return test_pilot_objects[i];
    return NULL;
}
static uintptr_t test_pilot_original(void *self,double value,int axis,double epsilon) {
    assert(self && axis>=0 && axis<3 && epsilon==0.0001);test_pilot_seen=value;++test_pilot_calls;return 0x5150;
}
/* d9cc30 as decompiled: the PlayerController's contexts are requested only with
 * a controller, flight enabled (2f4b7e0) and no deactivation share; the SPACE
 * callback d88710 then sets or clears flight control (immediate in this fake). */
static void test_pilot_refresh(char present) {
    ++test_pilot_refresh_calls;
    if(present && test_pilot_enabled && !test_pilot_counter) {if(!test_pilot_contexts) {test_pilot_contexts=1;test_pilot_flight=1;}}
    else if(test_pilot_contexts) {test_pilot_contexts=0;test_pilot_flight=0;}
}
static unsigned char test_pilot_authority(void *controller) {
    assert(controller==(void*)test_pilot_controllers[0]);
    return (unsigned char)(test_pilot_authority_value<2?test_pilot_authority_value:!test_pilot_flight);
}
static void test_pilot_bytes(unsigned slot,uint64_t capture,int16_t z,unsigned char out[PILOT_RECORD_BYTES]) {
    pilot_record r;memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST;r.capture16=(uint16_t)capture;
    r.q[2]=z;r.q[3]=(int16_t)(slot==1?32767:-32767);r.q[6]=z;pilot_encode(&r,out);
}
static int test_pilot_frame(uint64_t tick) {
    stats.issued=tick;
    return pilot_before_release_using(test_pilot_find,test_pilot_runtimes,test_pilot_objects[0],test_pilot_controllers[0],
        test_pilot_refresh,test_pilot_authority,&test_pilot_counter,&test_pilot_contexts,&test_pilot_flight);
}
static uintptr_t test_pilot_call(unsigned slot,unsigned group,unsigned axis,double value) {
    static const uintptr_t sites[]={0xec91cc,0xec91ee,0xec9210,0xec9230,0xec9252,0xec9274};
    return pilot_request(test_pilot_original,test_pilot_solvers[slot]+(group?0x6a00:0x12cd0),value,(int)axis,0.0001,sites[group*3+axis]);
}
static void test_pilot_inputs(void) {
    pilot_record r,d;unsigned char b[PILOT_RECORD_BYTES],c[PILOT_RECORD_BYTES];char hex[2*PILOT_RECORD_BYTES+1];
    /* Codec: exact bytes, round trip, strict refusal. */
    memset(&r,0,sizeof(r));r.mode=PILOT_MODE_ASSIST|PILOT_MODE_BOOST;r.buttons=5;r.capture16=0xbeef;
    r.q[0]=32767;r.q[1]=-32767;r.q[2]=pilot_quantize(0.5f);r.q[3]=pilot_quantize(-2.0f);r.q[6]=1;r.target=0x0123456789abcdefULL;
    assert(r.q[2]==16384 && r.q[3]==-32767 && pilot_quantize(3.0f)==32767 && pilot_axis(32767)==1.0f && pilot_axis(-32767)==-1.0f);
    pilot_encode(&r,b);
    assert(b[0]==1 && b[1]==3 && b[2]==0xef && b[3]==0xbe && b[4]==0xff && b[5]==0x7f && b[6]==0x01 && b[7]==0x80 &&
           b[18]==5 && !b[19] && b[20]==0xef && b[27]==0x01);
    assert(pilot_decode(b,&d) && d.mode==r.mode && d.buttons==5 && d.capture16==0xbeef && d.target==r.target &&
           !memcmp(d.q,r.q,sizeof(r.q)));
    pilot_hex(b,PILOT_RECORD_BYTES,hex);assert(strlen(hex)==56 && !strncmp(hex,"0103efbeff7f0180",16));
    memset(c,0,sizeof(c));assert(pilot_unhex(hex,56,c) && !memcmp(b,c,sizeof(b)));
    assert(!pilot_unhex(hex,55,c) && !pilot_unhex(NULL,56,c));
    hex[0]='A';assert(!pilot_unhex(hex,56,c));
    memcpy(c,b,sizeof(c));c[0]=2;assert(!pilot_decode(c,&d));
    memcpy(c,b,sizeof(c));c[1]|=0x20;assert(!pilot_decode(c,&d));
    memcpy(c,b,sizeof(c));c[1]|=PILOT_MODE_REQUESTS;assert(pilot_decode(c,&d) && d.mode==(r.mode|PILOT_MODE_REQUESTS));
    memcpy(c,b,sizeof(c));c[18]=0x10;assert(!pilot_decode(c,&d));
    memcpy(c,b,sizeof(c));c[19]=1;assert(!pilot_decode(c,&d));
    memcpy(c,b,sizeof(c));c[8]=0;c[9]=0x80;assert(!pilot_decode(c,&d)); /* -32768 */
    memcpy(c,b,sizeof(c));c[1]|=PILOT_MODE_TARGET_UNMAPPED;assert(!pilot_decode(c,&d)); /* unmapped with an ID */
    /* Script: from (slot, tick) only. */
    pilot_script(1,30,30,&r);assert(r.mode==PILOT_MODE_ASSIST && r.capture16==30 && r.q[2]==32767 && !r.q[0] && !r.q[4] && r.q[6]==32767);
    pilot_script(2,150,30,&r);assert(r.q[2]==32767 && r.q[4]==-16384);
    pilot_script(1,270,30,&r);assert(r.q[2]==16384 && r.q[3]==16384);
    pilot_script(2,390,30,&r);assert(r.q[0]==-32767 && r.q[5]==-16384 && !r.q[2]);
    pilot_script(1,510,30,&r);for(unsigned i=0;i<7;i++) assert(!r.q[i]);
    pilot_script(1,630,30,&r);assert(r.q[2]==32767);
    pilot_script(1,29,30,&r);for(unsigned i=0;i<7;i++) assert(!r.q[i]);
    /* Mapping onto the driven ship's own limits. */
    float l[8]={300,0,-100,50,60,0.5f,0.75f,0.875f};int forced=0;
    memset(&r,0,sizeof(r));r.q[0]=32767;r.q[1]=-32767;r.q[2]=-32767;r.q[3]=32767;r.q[4]=-32767;r.q[5]=32767;
    assert(pilot_target(&r,l,1,0,&forced)==50.0f && pilot_target(&r,l,1,1,&forced)==-60.0f && pilot_target(&r,l,1,2,&forced)==-100.0f);
    assert(pilot_target(&r,l,0,0,&forced)==0.75f && pilot_target(&r,l,0,1,&forced)==-0.5f && pilot_target(&r,l,0,2,&forced)==0.875f && !forced);
    r.q[2]=32767;assert(pilot_target(&r,l,1,2,&forced)==300.0f && !forced);
    l[2]=100;r.q[2]=-32767;assert(pilot_target(&r,l,1,2,&forced)==-100.0f);
    r.mode=PILOT_MODE_TRAVEL;assert(pilot_target(&r,l,1,2,&forced)==300.0f && forced);
    assert(pilot_limits_valid(l));l[1]=NAN;assert(pilot_limits_valid(l));
    l[0]=-1;assert(!pilot_limits_valid(l));l[0]=300;l[5]=INFINITY;assert(!pilot_limits_valid(l));l[5]=0.5f;
    /* Manifest and configuration. */
    uint64_t saved[PILOT_SLOTS];unsigned count=0;
    assert(pilot_parse_manifest("0000000000176a13,0000000000176a74",saved,&count) && count==2 && saved[1]==0x176a74);
    assert(!pilot_parse_manifest("0000000000176a13,0000000000176a13",saved,&count));
    assert(!pilot_parse_manifest("0000000000000000",saved,&count) && !pilot_parse_manifest("0000000000176A13",saved,&count));
    assert(!pilot_parse_manifest("0000000000176a13,",saved,&count) && !pilot_parse_manifest("000000000176a13",saved,&count));
    assert(!pilot_parse_manifest("0000000000000001,0000000000000002,0000000000000003,0000000000000004,0000000000000005",saved,&count));
    assert(pilot_config_valid(2,PILOT_SOURCE_SCRIPT,0,30,600,0) && !pilot_config_valid(2,PILOT_SOURCE_SCRIPT,2,30,600,0));
    assert(pilot_config_valid(2,PILOT_SOURCE_WIRE,3,30,600,1) && !pilot_config_valid(2,PILOT_SOURCE_WIRE,1,30,600,0));
    assert(!pilot_config_valid(2,PILOT_SOURCE_WIRE,31,40,600,0) && !pilot_config_valid(2,PILOT_SOURCE_WIRE,3,3,600,0));
    assert(!pilot_config_valid(2,PILOT_SOURCE_SCRIPT,0,30,29,0) && !pilot_config_valid(2,PILOT_SOURCE_SCRIPT,0,30,PILOT_TICK_MAX+1,0));
    assert(!pilot_config_valid(0,PILOT_SOURCE_SCRIPT,0,30,600,0) && !pilot_config_valid(2,3,0,30,600,0));
    assert(!pilot_arm("0000000000176a13",PILOT_SOURCE_SCRIPT,0,30,600,0) && !pilot_armed); /* No active boot gate here. */
    const char *why="";assert(!pilot_capture(5,b,&why) && !strcmp(why,"not_armed"));
    /* Armed wire session: two fake ships, staged records, suppression. */
    int owned_identities=!world_identities;uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0xa100,0x176a13) && world_identity_record(0xa200,0x176a74));
    for(unsigned i=0;i<2;i++) {
        memset(test_pilot_objects[i],0,0x300);memset(test_pilot_controllers[i],0,0x60);memset(test_pilot_agents[i],0,0x1000);
        unsigned char *o=test_pilot_objects[i],*k=test_pilot_controllers[i],*a=test_pilot_agents[i],*s=test_pilot_solvers[i];
        memcpy(o+8,&test_pilot_runtimes[i],8);memcpy(o+0x200,&k,8);memcpy(k+0x28,&o,8);memcpy(k+0x40,&a,8);
        memcpy(a+0xef0,&o,8);memcpy(a+0xf80,&s,8);
        const float limits[8]={200.0f+i,0,-50,40,45,0.5f,0.25f,1.0f};memcpy(a+PILOT_LIMITS,limits,sizeof(limits));
    }
    memset(pilot_slots,0,sizeof(pilot_slots));memset(pilot_ring,0,sizeof(pilot_ring));memset(pilot_locals,0,sizeof(pilot_locals));
    pilot_slots[0].saved=0x176a13;pilot_slots[1].saved=0x176a74;pilot_slot_count=2;pilot_source=PILOT_SOURCE_WIRE;pilot_delay=3;
    pilot_start=10;pilot_stop=20;pilot_suppress=1;pilot_armed=1;pilot_installed=1;pilot_frame_tick=0;
    test_pilot_counter=0;test_pilot_contexts=1;test_pilot_flight=1;test_pilot_authority_value=2;stats.slot=1;
    assert(pilot_reverse(0x176a13)==0xa100 && !pilot_reverse(0x999));
    /* Staging: apply = capture + delay, strictly after the current tick. */
    stats.issued=5;
    test_pilot_bytes(1,5,32767,b);assert(pilot_stage_record(1,b,5) && pilot_ring[8%PILOT_RING].tick==8);
    assert(pilot_stage_record(1,b,5)); /* identical duplicate */
    test_pilot_bytes(1,5,0,c);assert(!pilot_stage_record(1,c,5)); /* conflicting duplicate */
    test_pilot_bytes(2,2,0,c);assert(!pilot_stage_record(2,c,5) && pilot_late==1); /* apply 5 is not ahead */
    assert(!pilot_stage_record(3,b,5) && !pilot_stage_record(0,b,5));
    pilot_locals[6%PILOT_LOCAL_RING]=(pilot_local){6,9,1,{0}};test_pilot_bytes(1,6,100,c);
    assert(!pilot_stage_record(1,c,6) && !strcmp(pilot_stage,"echo_mismatch"));
    memcpy(pilot_locals[6%PILOT_LOCAL_RING].bytes,c,PILOT_RECORD_BYTES);assert(pilot_stage_record(1,c,6));
    /* Frames: take one deactivation share at start-3 (contexts removed, flight
     * control off), require it held, no driving before start. */
    assert(test_pilot_frame(7) && test_pilot_counter==1 && !test_pilot_contexts && !test_pilot_flight && pilot_controls_owned &&
           pilot_controls_base==0 && pilot_controls_present);
    assert(pilot_slots[0].resolved && pilot_slots[0].player && pilot_slots[1].resolved && !pilot_slots[1].player && !pilot_slots[0].driving);
    assert(pilot_locals_pending(7)==1 && test_pilot_frame(8) && test_pilot_frame(9) && pilot_locals_pending(9)==0);
    test_pilot_bytes(1,7,32767,b);assert(pilot_stage_record(1,b,9));
    assert(!test_pilot_frame(10) && !strcmp(pilot_stage,"missing_record") && pilot_missing==1);
    test_pilot_bytes(2,7,-32767,c);assert(pilot_stage_record(2,c,9));
    assert(test_pilot_frame(10) && pilot_live && pilot_frame_tick==10 && pilot_slots[0].driving && pilot_slots[1].driving);
    /* Substitution: only XMM1 of an admitted Steering call, exactly once. */
    test_pilot_calls=0;
    assert(test_pilot_call(0,1,2,7.0)==0x5150 && test_pilot_seen==200.0 && test_pilot_calls==1);
    assert(test_pilot_call(1,1,2,7.0)==0x5150 && test_pilot_seen==-50.0); /* reverse limit magnitude */
    assert(test_pilot_call(0,0,0,7.0)==0x5150 && test_pilot_seen==0.25 && test_pilot_call(1,0,0,7.0)==0x5150 && test_pilot_seen==-0.25);
    assert(test_pilot_call(0,1,0,7.0)==0x5150 && test_pilot_seen==0.0);
    assert(pilot_slots[0].applied[0]==1 && pilot_slots[0].applied[1]==2 && pilot_frame_applied==5 && test_pilot_calls==5);
    uint64_t expected=0;
    const struct {uint64_t saved;unsigned stream;double value;} receipts[]={{0x176a13,5,200.0},{0x176a74,5,-50.0},{0x176a13,0,0.25},{0x176a74,0,-0.25},{0x176a13,3,0.0}};
    for(unsigned i=0;i<5;i++) {uint64_t bits;memcpy(&bits,&receipts[i].value,8);expected+=lockstep_seed(PILOT_DOMAIN,10,receipts[i].saved,receipts[i].stream,bits,0);}
    assert((uint64_t)pilot_frame_receipt==expected);
    char text[1024];assert(pilot_frame_text(text,sizeof(text)) && !strncmp(text,"PILOT_1:10:5:",13));
    assert(pilot_request(test_pilot_original,test_pilot_solvers[0]+0x6a00,7.0,2,0.0001,0x123456)==0x5150 && test_pilot_seen==7.0); /* unknown caller */
    assert(pilot_request(test_pilot_original,test_pilot_solvers[0]+0x6a00,7.0,1,0.0001,0xec9274)==0x5150 && test_pilot_seen==7.0); /* wrong axis */
    static unsigned char other[0x13000];
    assert(pilot_request(test_pilot_original,other+0x6a00,7.0,2,0.0001,0xec9274)==0x5150 && test_pilot_seen==7.0); /* undriven solver */
    unsigned char *replaced=other;memcpy(test_pilot_agents[0]+0xf80,&replaced,8);
    assert(test_pilot_call(0,1,2,7.0)==0x5150 && test_pilot_seen==7.0); /* solver changed under the agent */
    unsigned char *own=test_pilot_solvers[0];memcpy(test_pilot_agents[0]+0xf80,&own,8);
    float bad=-1.0f;memcpy(test_pilot_agents[0]+PILOT_LIMITS,&bad,4);
    assert(test_pilot_call(0,1,2,7.0)==0x5150 && test_pilot_seen==7.0 && pilot_bad_limits==1);
    float good=200.0f;memcpy(test_pilot_agents[0]+PILOT_LIMITS,&good,4);
    stats.issued=11;assert(test_pilot_call(0,1,2,7.0)==0x5150 && test_pilot_seen==7.0); /* stale table */
    stats.issued=10;InterlockedExchange(&pilot_live,0);assert(test_pilot_call(0,1,2,7.0)==0x5150 && test_pilot_seen==7.0);
    assert(pilot_frame_applied==5 && !pilot_open && test_pilot_calls==12);
    /* Delay mismatch and lost controls fail the frame. */
    test_pilot_bytes(1,8,0,b);assert(pilot_stage_record(1,b,10));test_pilot_bytes(2,8,0,b);assert(pilot_stage_record(2,b,10));
    pilot_ring[11%PILOT_RING].bytes[0][2]=0x09;assert(!test_pilot_frame(11) && !strcmp(pilot_stage,"delay_mismatch"));
    pilot_ring[11%PILOT_RING].bytes[0][2]=0x08;assert(test_pilot_frame(11));
    /* A menu or the map adds its own share: tolerated, counted as a UI hold;
     * the capture side then records neutral input. */
    test_pilot_bytes(1,9,0,b);assert(pilot_stage_record(1,b,11));test_pilot_bytes(2,9,0,b);assert(pilot_stage_record(2,b,11));
    test_pilot_counter=2;
    LONG64 ui=pilot_controls_ui_passes;assert(test_pilot_frame(12) && pilot_controls_ui_passes==ui+1 && pilot_controls_ui_hold());
    test_pilot_counter=1;assert(!pilot_controls_ui_hold());
    /* A foreign path removed our share before its requests took effect: taken again. */
    test_pilot_counter=0;test_pilot_contexts=1;test_pilot_flight=0;LONG64 retakes=pilot_controls_retakes;
    test_pilot_bytes(1,9,0,b);assert(pilot_stage_record(1,b,11));test_pilot_bytes(2,9,0,b);assert(pilot_stage_record(2,b,11));
    stats.issued=11;assert(test_pilot_frame(12) && test_pilot_counter==1 && !test_pilot_contexts && pilot_controls_retakes==retakes+1);
    /* Requests back while our share is held: removed again. */
    test_pilot_contexts=1;LONG64 refreshes=pilot_controls_refreshes;
    test_pilot_bytes(1,9,0,b);assert(pilot_stage_record(1,b,11));
    assert(test_pilot_frame(12) && !test_pilot_contexts && pilot_controls_refreshes==refreshes+1);
    /* Flight control back (the controller would move the ship), or the
     * authority predicate says the controller owns motion: fail closed. */
    test_pilot_flight=1;assert(!test_pilot_frame(12) && !strcmp(pilot_stage,"authority_lost"));test_pilot_flight=0;
    test_pilot_authority_value=0;assert(!test_pilot_frame(12) && !strcmp(pilot_stage,"authority_lost") && !pilot_controls_authority);
    test_pilot_authority_value=2;assert(test_pilot_frame(12) && pilot_controls_authority==1);
    /* After the window: not driving, controls returned once at stop+1. */
    assert(test_pilot_frame(21) && !pilot_slots[0].driving && !test_pilot_counter && test_pilot_contexts && test_pilot_flight && !pilot_controls_owned);
    assert(test_pilot_frame(22) && test_pilot_flight && !test_pilot_counter);
    /* Unresolved ship is counted, never driven. */
    LONG64 unresolved=pilot_unresolved;test_pilot_runtimes[1]=0xdead;
    for(uint64_t t=13;t<=14;t++) {test_pilot_bytes(1,t-3,0,b);test_pilot_bytes(2,t-3,0,c);assert(pilot_stage_record(1,b,t-1) && pilot_stage_record(2,c,t-1));}
    pilot_controls_owned=1;pilot_controls_base=0;pilot_controls_counter_ptr=&test_pilot_counter;test_pilot_counter=1;
    test_pilot_contexts=0;test_pilot_flight=0;
    assert(test_pilot_frame(13) && !pilot_slots[1].resolved && !pilot_slots[1].driving && pilot_unresolved==unresolved+1);
    test_pilot_runtimes[1]=0xa200;
    /* Status and pending ring for presentation prediction. */
    pilot_locals[14%PILOT_LOCAL_RING]=(pilot_local){14,17,1,{1,1}};
    assert(pilot_pending(text,sizeof(text)) && strstr(text,"frame_tick=13 14:17:0101"));
    assert(pilot_status(text,sizeof(text)) && strstr(text,"source=wire delay=3 window=10-20 slots=2") &&
           strstr(text," s1=0000000000176a13/1/1/1/") && strstr(text,"suppress=1 controls_owned=1 park=0/1/0/0/1 retakes=1 refreshes=1"));
    /* Ownership and restoration of the shared request entry and the controls flag. */
    uint64_t original=0x0824245c8948ULL,patched=0x0824245c89e9ULL,foreign=0x1111ULL;
    memcpy(image+REQUEST_RVA,&patched,8);pilot_original_word=original;pilot_patched_word=patched;pilot_installed=1;
    InterlockedExchange(&pilot_entry_owner,1);assert(pilot_owned());
    memcpy(image+REQUEST_RVA,&foreign,8);assert(!pilot_owned());
    pilot_restore_using(test_pilot_refresh,&test_pilot_counter,1);
    assert(pilot_installed && !pilot_restored && pilot_entry_owner && !strcmp(pilot_stage,"restore_refused") &&
           !pilot_controls_owned && !test_pilot_counter && test_pilot_flight && *(uint64_t*)(image+REQUEST_RVA)==foreign);
    memcpy(image+REQUEST_RVA,&patched,8);pilot_restore_using(test_pilot_refresh,&test_pilot_counter,1);
    assert(!pilot_installed && pilot_restored && !pilot_entry_owner && *(uint64_t*)(image+REQUEST_RVA)==original && !pilot_live);
    /* Only our share is released: a UI still holding its own share keeps
     * controls parked, and a lost share is not taken from anyone else. */
    pilot_controls_owned=1;test_pilot_counter=2;test_pilot_contexts=0;test_pilot_flight=0;
    pilot_restore_using(test_pilot_refresh,&test_pilot_counter,1);
    assert(!pilot_controls_owned && test_pilot_counter==1 && !test_pilot_contexts && !test_pilot_flight && pilot_restored);
    pilot_controls_owned=1;test_pilot_counter=0;pilot_restore_using(test_pilot_refresh,&test_pilot_counter,1);
    assert(!pilot_controls_owned && !test_pilot_counter && pilot_restored);
    /* Taking needs the requests actually removed; otherwise the share is undone. */
    test_pilot_counter=0;test_pilot_enabled=1;test_pilot_contexts=0;test_pilot_flight=0;
    assert(pilot_controls_take(test_pilot_refresh,&test_pilot_counter,&test_pilot_contexts,1) && test_pilot_counter==1);
    pilot_controls_release(test_pilot_refresh,&test_pilot_counter,1);assert(!test_pilot_counter && test_pilot_contexts && test_pilot_flight);
    static unsigned char stuck=1;
    assert(!pilot_controls_take(test_pilot_refresh,&test_pilot_counter,&stuck,1) && !test_pilot_counter && !pilot_controls_owned);
    pilot_controls_retakes=pilot_controls_refreshes=pilot_controls_ui_passes=pilot_neutral=0;pilot_controls_authority=2;
    memset(image+REQUEST_RVA,0,8);
    /* The shared dispatch routes to the pilot only while it owns the entry. A
     * stub whose call returns to the real linear-z Steering site exercises the
     * actual dispatch ABI and return-address filter; mixed register arguments. */
    unsigned char *saved_thunk=thunk,*page=VirtualAlloc(NULL,THUNK_PAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(page);thunk_absolute_jump(page+REQUEST_TRAMPOLINE,(void*)test_pilot_original);thunk=page;
    unsigned char *site=image+0xec9264;DWORD protection;void *dispatch=(void*)request_dispatch;
    assert(VirtualProtect(site,32,PAGE_EXECUTE_READWRITE,&protection));
    const unsigned char head[]={0x48,0x83,0xec,0x28,0x48,0xb8},tail[]={0x48,0x83,0xc4,0x28,0xc3};
    memcpy(site,head,6);memcpy(site+6,&dispatch,8);site[14]=0xff;site[15]=0xd0;memcpy(site+16,tail,5);
    FlushInstructionCache(GetCurrentProcess(),site,32);
    request_fn stub=(request_fn)site;stats.issued=pilot_frame_tick;InterlockedExchange(&pilot_live,1);
    LONG64 routed=pilot_calls;test_pilot_calls=0;InterlockedExchange(&pilot_entry_owner,1);
    assert(stub(test_pilot_solvers[0]+0x6a00,7.0,2,0.0001)==0x5150 && test_pilot_seen==0.0 && pilot_calls==routed+1 && !pilot_open);
    InterlockedExchange(&pilot_entry_owner,0);
    assert(stub(test_pilot_solvers[0]+0x6a00,7.0,2,0.0001)==0x5150 && test_pilot_seen==7.0 && pilot_calls==routed+1 && test_pilot_calls==2);
    InterlockedExchange(&pilot_live,0);memset(site,0,32);assert(VirtualProtect(site,32,protection,&protection));
    thunk=saved_thunk;VirtualFree(page,0,MEM_RELEASE);
    /* Reset: the prototype stays default off for later tests. */
    memset(pilot_slots,0,sizeof(pilot_slots));memset(pilot_ring,0,sizeof(pilot_ring));memset(pilot_locals,0,sizeof(pilot_locals));
    pilot_armed=pilot_installed=pilot_restored=pilot_controls_owned=0;pilot_slot_count=pilot_source=pilot_delay=pilot_suppress=0;
    pilot_start=pilot_stop=pilot_frames=pilot_frame_tick=0;pilot_stage="off";InterlockedExchange(&pilot_live,0);
    pilot_failures=pilot_calls=pilot_frame_applied=pilot_frame_receipt=pilot_applied=pilot_receipt=pilot_missing=pilot_late=0;
    pilot_unresolved=pilot_no_solver=pilot_bad_limits=pilot_forced=pilot_staged_count=pilot_captures=pilot_restore_open=0;
    stats.issued=saved_issued;stats.slot=saved_slot;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    printf("pilot inputs codec/script/mapping/staging/frame/substitution/suppression/restore tests PASS\n");
}
static void test_pilot_native_image(const char *path) {
    static const struct {unsigned rva,size;const char *sha;} pins[]={
        {0xe5d380,214,"ef39acf755eaecacd63bb4b33fe48ad93094952929135412787ef0efe0b3b4b4"},
        {0xec7200,10109,"bff420d07fb655ff195560b395af5decc0ab5b2ecf9691481e578eb3282eb796"},
        {0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"},
        {0x9fc550,277,"fca3a29a2c0ca40fafa27dac44da67e9b8bd0aa52b3d812c72b72f4ed212ca67"},
        {0xd8e5b0,4712,"2a2145135a34b685e82fdc774c6e1b255f4e99c3b72ad26eac3a8961bbfaf487"},
        {0xb09880,91,"46f11b7670b45cff7e4f4e96e0087afa3afb93d491622160803a99bddc7469d3"},
        {0xea7600,77,"929dedb5c752070cacf8e090a08a8c963e34ed124129184e1951e86d20960fa3"},
        {0xea7650,56,"d19a760d39407a1e7831ddffddd40702bb2e6ce23ab5d6ba7b6b8173a03f9c3e"},
        {0x179760,54,"ec618b3d0aa0db0021012ccc43509c3f6c7d0cbaa234f506d7a7b0c08bd474c7"},
        {0x175a00,66,"712e88aa8e43cf4271c06f866dd64b6a0c2fbf938852efa14138ecc78fc9455e"},
        {0xd9cc30,53,"71e1d4718f219b147fe6dc46d86dd4e34956b0305aa07babd05f2a6a5c59a91c"},
        {0xd8d9d0,493,"01165eddbdd9bfe5cb36d6fc1ce4c84f5f59012df7f92dfd00fe813ff26482f1"},
        /* Parking by a deactivation share (refresh with its tail, request removal, authority predicate). */
        {0xd9cc30,137,"2f09f72542b0e0526df12d2df393b4c5fb2a2a909ac4539162a8125417f6ee27"},
        {0x1252850,241,"5fe3f1305271b133209201783f6adffe36e8ab1e64ccdd5eff7ecbbd9302d7f4"},
        {0xd8dd50,81,"d819afff6cb97c4b41d44bfb9a52a6290cf93cc53659d34ac994e1428cbc0600"},
        /* Camera follow (lockstep_pilot_camera.h). */
        {0x1b9230,56,"0e2bf084c10d06627552f4e24838c733974245484eaa3aea6ed6d313fee3cb39"},
        {0x1b3750,56,"33ed5545283ccc7429d7af1752b1daf356a7ab5e73d74edec87b16e3b3e3fc73"},
        {0xafd280,41,"1676383ebd57066fefa93ca02e48f3167bbebf4e6cf2c9fb3b1259ef9bb4d4ef"},
        {0x9faad0,1605,"c6137d093484d23a77c5b22ef4d9345588869a435ac4eece1f14bc2c9c652daf"}};
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    assert(fread(sections,sizeof(*sections),nt.FileHeader.NumberOfSections,f)==nt.FileHeader.NumberOfSections);
    static unsigned char bytes[10109];
    for(unsigned i=0;i<sizeof(pins)/sizeof(*pins);i++) {
        unsigned offset=0;
        for(unsigned j=0;j<nt.FileHeader.NumberOfSections;j++)
            if(pins[i].rva>=sections[j].VirtualAddress && pins[i].rva+pins[i].size<=sections[j].VirtualAddress+sections[j].SizeOfRawData)
                offset=sections[j].PointerToRawData+pins[i].rva-sections[j].VirtualAddress;
        assert(offset && !fseek(f,(long)offset,SEEK_SET) && fread(bytes,1,pins[i].size,f)==pins[i].size && hash_ok(bytes,pins[i].size,pins[i].sha));
    }
    fclose(f);
    puts("pinned native image pilot capture/apply/parking/camera pins verified (19 functions)");
}
