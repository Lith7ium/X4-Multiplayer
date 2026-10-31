/* Stock piloting (lockstep_pilot_stock.h) on the fake image region of the native
 * controls tests: record codec and verdicts, capture of the dispatch stream and the
 * SPACE edge, wire staging, the replay through the multimap and the simulation half
 * of d88710, the cursor substitution, the schedule and every fail-closed path. */
typedef struct {char what;int id;float value;unsigned char flight;void *object;} test_pstk_call;
static test_pstk_call test_pstk_calls[64];
static unsigned test_pstk_call_count;
static unsigned char test_pstk_authority_value=1;
static float test_pstk_curve_scale=1.0f;
static void test_pstk_log(char what,int id,float value,void *object) {
    assert(test_pstk_call_count<64);
    test_pstk_calls[test_pstk_call_count++]=(test_pstk_call){what,id,value,pnat_base?*PNAT_U8(PNAT_FLIGHT):0,object};
}
static unsigned test_pstk_count(char what) {unsigned n=0;for(unsigned i=0;i<test_pstk_call_count;i++) n+=test_pstk_calls[i].what==what;return n;}
static void test_pstk_handoff(void *controller,char authority) {test_pstk_log('H',authority,0,controller);}
static void test_pstk_release(void *controller) {test_pstk_log('L',0,0,controller);}
static void test_pstk_started(void *player) {test_pstk_log('+',0,0,player);}
static void test_pstk_stopped(void *player) {test_pstk_log('-',0,0,player);}
static float test_pstk_curve(void *curve,float x) {(void)curve;return x*test_pstk_curve_scale;}
static unsigned char test_pstk_authority(void *controller) {test_pstk_log('A',0,0,controller);return test_pstk_authority_value;}
static void test_pstk_action(int id) {test_pstk_log('a',id,0,NULL);}
static void test_pstk_state(int id,unsigned char active) {test_pstk_log('s',id,active,NULL);}
static void test_pstk_range(int id,float value) {test_pstk_log('g',id,value,NULL);}
static void test_pstk_foreign(int id,unsigned char active) {(void)id;(void)active;assert(0);}
static void test_pstk_local_handler(int id) {test_pstk_log('l',id,0,NULL);}
static void test_pstk_cursor_original(void *controller) {
    assert(controller==test_pnat_controller);
    float *out=(float*)(pnat_base+0x2f4b738);out[0]=0.25f;out[1]=-0.5f;out[2]=0.0f;test_pstk_log('C',0,0,controller);
}
static uint32_t test_pstk_bits(float f) {uint32_t v;memcpy(&v,&f,4);return v;}
/* Aim fixtures: the ship sits at +10/+20/+30 in its zone, axes aligned; the cursor
 * ray point is 1/2/3 in the ship's frame; the stock aim reports what it read. */
static unsigned char test_pstk_zone[64];
static float test_pstk_seen_point[4],test_pstk_seen_distance;
static void *test_pstk_seen_picked;
static unsigned char test_pstk_seen_steer;
static unsigned test_pstk_aim_calls;
static void test_pstk_transform(void *from,float *t,void *to,uint64_t flag) {
    assert(!flag && t[4]==1.0f && t[9]==1.0f && t[14]==1.0f);
    float d=from==(void*)test_pnat_ship && to==(void*)test_pstk_zone?1.0f:from==(void*)test_pstk_zone && to==(void*)test_pnat_ship?-1.0f:0.0f;
    assert(d!=0.0f);t[0]=10.0f*d;t[1]=20.0f*d;t[2]=30.0f*d;
}
static void **test_pstk_ship(void *player,void **out) {assert(player==test_pnat_player);*out=test_pnat_ship;return out;}
static void test_pstk_ray(void *player,float *out,void *relative,uint64_t flag) {
    assert(player==test_pnat_player && relative==test_pnat_ship && !flag);out[0]=1.0f;out[1]=2.0f;out[2]=3.0f;out[3]=0.0f;
}
static unsigned char *test_pstk_zone_of(unsigned char *player) {assert(player==test_pnat_player);return test_pstk_zone;}
static unsigned char test_pstk_aim_original(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                                            unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    assert(controller==(void*)test_pnat_controller && space==(void*)test_pstk_zone && weapon==(void*)test_pnat_ship && !transform &&
           a7==1.5f && a8==-2.5f && a9==1 && !a10 && a11==1 && !a12 && a13==1);
    unsigned char *player=test_pnat_player,*input=test_pnat_input;
    memcpy(test_pstk_seen_point,player+0x6c60,16);memcpy(&test_pstk_seen_distance,player+0x6c80,4);memcpy(&test_pstk_seen_picked,player+0x6c88,8);
    test_pstk_seen_steer=input[0x9053];++test_pstk_aim_calls;
    memcpy(first,test_pstk_seen_point,16);memcpy(second,test_pstk_seen_point,16);return 1;
}
/* View fixtures: a cockpit camera with FOV factor 0.5, floating off, soft target runtime 0x55. */
static unsigned char test_pview_camera[0x900];
static unsigned char test_pview_floating_value;
static float test_pview_fov(void *camera) {assert(camera==(void*)test_pview_camera);return 0.5f;}
static unsigned char test_pview_floating(void) {return test_pview_floating_value;}
static uint64_t test_pview_portable(uint64_t runtime) {return runtime==0x55?0x1055:0;}
/* A call through the PlayerShootController vtable slot, as Weapon::Move makes it (8141c2). */
static unsigned char test_pstk_weapon_aim(unsigned char *region,float *out) {
    pstk_aim_fn fn=*(pstk_aim_fn*)(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT);float second[4];
    return fn(test_pnat_controller,out,second,test_pstk_zone,test_pnat_ship,NULL,1.5f,-2.5f,1,0,1,0,1);
}
static void test_pstk_codec(void) {
    pstk_record r,d;unsigned char b[PSTK_BYTES_MAX];
    memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_ACTIVE|PSTK_FLAG_CURSOR;r.capture16=0x0102;r.cursor[0]=0.5f;r.cursor[2]=-1.0f;r.count=3;
    r.events[0]=(pstk_event){PSTK_SPACE,8,1};r.events[1]=(pstk_event){PSTK_STATE,0x11,1};r.events[2]=(pstk_event){PSTK_RANGE,4,test_pstk_bits(0.75f)};
    unsigned n=pstk_encode(&r,b);
    assert(n==PSTK_HEAD+3*PSTK_EVENT && b[0]==3 && b[1]==3 && b[2]==2 && b[3]==1 && b[42]==3 && b[43]==8 && b[44]==0xc0 && b[45]==1);
    assert(pstk_decode(b,n,&d) && d.count==3 && d.capture16==0x0102 && d.cursor[0]==0.5f && d.cursor[2]==-1.0f &&
           d.events[1].kind==PSTK_STATE && d.events[1].id==0x11 && d.events[1].value==1 && d.events[2].value==test_pstk_bits(0.75f));
    /* The wire test vector of tests/test_native_gate_inputs.py (STOCK). */
    memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_ACTIVE;r.capture16=0x0102;r.count=1;r.events[0]=(pstk_event){PSTK_STATE,0x11,1};
    n=pstk_encode(&r,b);char hex[2*PSTK_BYTES_MAX+1];pilot_hex(b,n,hex);
    assert(!strcmp(hex,"03010201" "000000000000000000000000" "000000000000000000000000" "00" "00000000000000000000000000" "01" "1140" "01000000"));
    /* Refusals: version, flags, count/length, id, values, SPACE id, non-replay id, cursor and aim bits without their flag. */
    unsigned char c[PSTK_BYTES_MAX];
    memcpy(c,b,n);c[0]=2;assert(!pstk_decode(c,n,&d));
    memcpy(c,b,n);c[1]=32;assert(!pstk_decode(c,n,&d));
    memcpy(c,b,n);assert(!pstk_decode(c,n-1,&d));c[42]=32;assert(!pstk_decode(c,n,&d));
    memcpy(c,b,n);c[43]=0x00;c[44]=0x42;assert(!pstk_decode(c,n,&d));                 /* state id 512 */
    memcpy(c,b,n);c[45]=2;assert(!pstk_decode(c,n,&d));                                 /* state value 2 */
    memcpy(c,b,n);c[43]=0x7b;c[44]=0x00;assert(!pstk_decode(c,n,&d));                  /* action 0x7b with value 1 */
    memcpy(c,b,n);c[43]=0x7b;c[44]=0x00;c[45]=0;assert(pstk_decode(c,n,&d));            /* action 0x7b, value 0 */
    memcpy(c,b,n);c[43]=0x09;c[44]=0xc0;assert(!pstk_decode(c,n,&d));                  /* SPACE context 9 */
    memcpy(c,b,n);c[43]=0x30;c[44]=0x01;assert(!pstk_decode(c,n,&d));                  /* action 0x130 scan: refused id */
    memcpy(c,b,n);c[43]=0x18;c[44]=0x40;assert(!pstk_decode(c,n,&d));                  /* fire: refused */
    memcpy(c,b,n);c[43]=0x04;c[44]=0x80;c[45]=0;c[46]=0;c[47]=0xc0;c[48]=0x7f;assert(!pstk_decode(c,n,&d)); /* range NaN */
    memcpy(c,b,n);c[5]=1;assert(!pstk_decode(c,n,&d));                                  /* cursor bits without the flag */
    memcpy(c,b,n);c[17]=1;assert(!pstk_decode(c,n,&d));                                 /* aim bits without the flag */
    memcpy(c,b,n);c[28]=1;assert(!pstk_decode(c,n,&d));                                 /* steering flag without the aim flag */
    memcpy(c,b,n);c[29]=1;assert(!pstk_decode(c,n,&d));                                 /* view bits without the view flag */
    memcpy(c,b,n);c[31]=1;assert(!pstk_decode(c,n,&d));                                 /* FOV bits without the view flag */
    memcpy(c,b,n);c[34]=1;assert(!pstk_decode(c,n,&d));                                 /* soft target without the view flag */
    /* View: cockpit and floating bits, the FOV factor, the soft target's portable ID. */
    memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_VIEW;r.view=3;r.fov=0.75f;r.target=0x0102030405060708ULL;
    n=pstk_encode(&r,b);assert(n==PSTK_HEAD && b[29]==3 && b[34]==8 && b[41]==1);
    assert(pstk_decode(b,n,&d) && d.view==3 && d.fov==0.75f && d.target==0x0102030405060708ULL);
    memcpy(c,b,n);c[29]=4;assert(!pstk_decode(c,n,&d));
    memcpy(c,b,n);c[30]=0;c[31]=0;c[32]=0xc0;c[33]=0x7f;assert(!pstk_decode(c,n,&d));  /* FOV NaN */
    /* Aim: a point in the ship's frame and the mouse steering flag. */
    memset(&r,0,sizeof(r));r.flags=PSTK_FLAG_AIM;r.aim[0]=1.5f;r.aim[1]=-2.0f;r.aim[2]=3.0f;r.steer=1;
    n=pstk_encode(&r,b);assert(n==PSTK_HEAD && b[1]==PSTK_FLAG_AIM && b[28]==1);
    assert(pstk_decode(b,n,&d) && d.aim[0]==1.5f && d.aim[1]==-2.0f && d.aim[2]==3.0f && d.steer==1 && !d.count);
    memcpy(c,b,n);c[28]=2;assert(!pstk_decode(c,n,&d));
    memcpy(c,b,n);c[16]=0;c[17]=0;c[18]=0xc0;c[19]=0x7f;assert(!pstk_decode(c,n,&d));  /* aim NaN */
    /* Verdicts: the replay, refuse and local sets are disjoint per kind. */
    for(unsigned k=0;k<3;k++) for(int id=0;id<512;id++) {
        unsigned v=pstk_verdict(k,id);int listed=0;
        if(k==0) listed=pstk_listed(pstk_replay_actions,PSTK_COUNT(pstk_replay_actions),id)+pstk_listed(pstk_refuse_actions,PSTK_COUNT(pstk_refuse_actions),id);
        if(k==1) listed=pstk_listed(pstk_replay_states,PSTK_COUNT(pstk_replay_states),id)+pstk_listed(pstk_refuse_states,PSTK_COUNT(pstk_refuse_states),id);
        if(k==2) listed=pstk_listed(pstk_replay_ranges,PSTK_COUNT(pstk_replay_ranges),id)+pstk_listed(pstk_refuse_ranges,PSTK_COUNT(pstk_refuse_ranges),id);
        assert(listed<=1 && (listed==1)==(v!=PSTK_LOCAL));
    }
    assert(pstk_verdict(PSTK_STATE,0x11)==PSTK_REPLAY && pstk_verdict(PSTK_ACTION,0x12f)==PSTK_REPLAY && pstk_verdict(PSTK_RANGE,4)==PSTK_REPLAY);
    assert(pstk_verdict(PSTK_STATE,0x5b)==PSTK_REFUSE && pstk_verdict(PSTK_ACTION,0xb3)==PSTK_REFUSE && pstk_verdict(PSTK_RANGE,0x11)==PSTK_REFUSE);
    assert(pstk_verdict(PSTK_ACTION,0xa9)==PSTK_LOCAL && pstk_verdict(PSTK_RANGE,0x1d)==PSTK_LOCAL && pstk_verdict(PSTK_STATE,0x82)==PSTK_LOCAL);
    /* Every pnat refused id stays out of the replay set. */
    for(unsigned i=0;i<PNAT_BLOCKED_COUNT;i++) {
        const pnat_id *b2=&pnat_blocked_ids[i];
        int allowed=(b2->kind==PNAT_ACTION && (b2->id==0x12f || b2->id==0xe1 || b2->id==0x131 || b2->id==0xdd)) ||
            (b2->kind==PNAT_STATE && (b2->id==0x11 || b2->id==0x50 || b2->id==0x85)) || (b2->kind==PNAT_ACTION && b2->id==0x148);
        assert(allowed?pstk_verdict(b2->kind,b2->id)==PSTK_REPLAY:pstk_verdict(b2->kind,b2->id)==PSTK_REFUSE);
    }
}
static void test_pilot_stock(void) {
    pstk_focus_forced=1;
    test_pstk_codec();
    unsigned char *saved_image_base=pnat_base;pnat_fns saved_fns=pnat_fn;pstk_fns saved_pstk=pstk_fn;
    uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    const void *saved_ptrs[PSTK_HANDLERS];memcpy(saved_ptrs,pstk_handler_ptrs,sizeof(saved_ptrs));
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region);pnat_base=region;
    pnat_fn=(pnat_fns){test_pnat_refresh,test_pnat_controller_of,test_pnat_option,test_pnat_steer_mode,test_pnat_steer_direct,
        test_pnat_steer_reset,test_pnat_throttle,test_pnat_match_reset,test_pnat_state,test_pnat_range,test_pnat_space_original,
        test_pstk_cursor_original,test_pnat_time};
    pstk_fn=(pstk_fns){test_pstk_handoff,test_pstk_release,test_pstk_started,test_pstk_stopped,test_pstk_curve,test_pstk_authority,
        test_pstk_aim_original,test_pstk_transform,test_pstk_ship,test_pstk_ray,test_pstk_zone_of};
    {void *aim=(void*)test_pstk_aim_original;memcpy(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT,&aim,8);}
    pview_fns saved_view=pview_fn;pview_fn=(pview_fns){test_pview_fov,test_pview_floating,test_pview_portable};
    for(unsigned i=0;i<PVIEW_SITES;i++) memcpy(region+pview_sites[i].rva,pview_sites[i].original,pview_sites[i].length);
    test_pview_floating_value=0;*(uint64_t*)(test_pnat_player+0x6dd0)=0x55;
    DWORD saved_ui_thread=ui_thread;ui_thread=GetCurrentThreadId();
    memset(test_pnat_player+0x6c60,0,0x40);
    pstk_handler_ptrs[0]=(void*)test_pstk_action;pstk_handler_ptrs[1]=(void*)test_pstk_state;pstk_handler_ptrs[2]=(void*)test_pstk_range;
    pstk_handler_ptrs[3]=(void*)test_pstk_action;pstk_handler_ptrs[4]=(void*)test_pstk_state;
    unsigned char *input=test_pnat_input,*controller=test_pnat_controller,*map=input+0x63e0;
    memset(test_pnat_input,0,sizeof(test_pnat_input));memset(test_pnat_universe,0,sizeof(test_pnat_universe));
    void *p=input;memcpy(region+PNAT_INPUT,&p,8);p=test_pnat_universe;memcpy(region+PNAT_UNIVERSE,&p,8);memcpy(region+PNAT_OPTIONS,&p,8);
    p=test_pnat_player;memcpy(test_pnat_universe+0x230,&p,8);
    static unsigned char config[0x300];memset(config,0,sizeof(config));p=config;memcpy(region+PNAT_CONFIG,&p,8);
    test_pnat_vtable[49]=(void*)test_pnat_assist;p=test_pnat_vtable;memcpy(controller,&p,8);
    p=test_pnat_ship;memcpy(controller+0x28,&p,8);test_pnat_ship[0xd1]=1;
    test_pnat_cursor_vtable[1]=(void*)test_pnat_cursor_slot1;test_pnat_cursor_vtable[2]=(void*)test_pnat_cursor_slot2;
    p=test_pnat_cursor_vtable;memcpy(test_pnat_cursor_object,&p,8);p=test_pnat_cursor_object;memcpy(input+0x8fe0,&p,8);
    test_pnat_vector[0]=(void*)test_pnat_handler_action;test_pnat_vector[1]=region+PNAT_SPACE_RVA;test_pnat_vector[2]=(void*)test_pnat_handler_state;
    p=test_pnat_vector;memcpy(input+0x10,&p,8);p=test_pnat_vector+3;memcpy(input+0x18,&p,8);
    for(unsigned i=0;i<3;i++) {p=region+pnat_vf_originals[i];memcpy(region+PNAT_VTABLE+8*pnat_vf_slots[i],&p,8);}
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) test_pnat_bytes(pnat_patches[i].rva,pnat_patches[i].expected);
    for(unsigned kind=0;kind<3;kind++) {pnat_node *h=(pnat_node*)(map+0x40*kind);memset(h,0,0x40);h->parent=h;}
    test_pnat_node_count=0;
    unsigned char *states=map+0x40,*ranges=map+0x80;
    test_pnat_insert(map,0x12f,(void*)test_pstk_action);test_pnat_insert(map,0xa9,(void*)test_pstk_local_handler);
    test_pnat_insert(map,0x130,(void*)test_pstk_action);
    test_pnat_insert(states,0x11,(void*)test_pstk_state);test_pnat_insert(states,0xd,(void*)test_pstk_state);
    test_pnat_insert(ranges,4,(void*)test_pstk_range);
    {void *camera=test_pview_camera;memset(test_pview_camera,0,sizeof(test_pview_camera));memcpy(test_pnat_universe+0x3d0,&camera,8);}
    /* Arming: slot 1's client captures (no device), every other client replays. */
    pilot_armed=1;pilot_suppress=1;pilot_source=PILOT_SOURCE_WIRE;pilot_slot_count=2;pilot_delay=3;pilot_device_armed=0;
    /* Every refusal names itself (gate.pilot_stock returns it to the probe). */
    pnat_armed=0;pnat_mode=0;stats.slot=2;assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"capture_not_a_stock_slot"));
    stats.slot=1;pilot_device_armed=1;assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"capture_with_device"));pilot_device_armed=0;
    pilot_source=PILOT_SOURCE_SCRIPT;assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"not_wire"));pilot_source=PILOT_SOURCE_WIRE;
    pilot_suppress=0;assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"no_suppress"));pilot_suppress=1;
    pilot_armed=0;assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"pilot_not_armed"));pilot_armed=1;
    assert(!pnat_arm_stock_using(1,2) && !strcmp(pstk_arm_why,"slots_without_1"));              /* slot 1 always */
    assert(!pnat_arm_stock_using(1,5) && !strcmp(pstk_arm_why,"slots_range"));                  /* slots in range */
    assert(!pnat_arm_stock_using(0,1) && !strcmp(pstk_arm_why,"stock_slot_not_capturing"));     /* capture on its client */
    assert(pnat_arm_stock_using(1,1) && pnat_mode==PNAT_MODE_STOCK && pstk_capturing && pstk_slot==1 && pilot_stock_slots==1 &&
           pstk_slot_mask==1 && pstk_capture_slot==1 && !pslot_armed);
    /* Identical rerun only. */
    assert(pnat_arm_stock_using(1,1) && !pnat_arm_stock_using(0,1) && !strcmp(pstk_arm_why,"rerun_capture") &&
           !pnat_arm_stock_using(1,3) && !strcmp(pstk_arm_why,"rerun_slots") && !pnat_arm_using(1));
    /* Schedule: start 10 (take 7, live 8), stop 40, delay 3: records from 8, replay from 11. */
    pilot_start=10;pilot_stop=40;pilot_controls_owned=0;pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pnat_installed=0;pnat_restored=1;
    memset(pilot_slots,0,sizeof(pilot_slots));pilot_slots[0].player=0;
    uint32_t *counter=(uint32_t*)(region+PNAT_COUNTER);unsigned char *contexts=region+PNAT_CONTEXTS,*flight=region+PNAT_FLIGHT;
    *counter=0;*contexts=1;*flight=0;
    /* The aim slot is taken from the first pass of the session; until the first replay it aims straight ahead. */
    assert(pnat_pass(7,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pilot_controls_owned);
    assert(pstk_aim_installed && pstk_aim_intact() && *(void**)(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT)==(void*)pstk_aim);
    /* The view sites: a jump (or the re-pointed call) to the trampolines near the image; neutral until a record. */
    assert(pview_installed && pview_intact() && pview_page);
    for(unsigned i=0;i<PVIEW_SITES;i++) {
        const pview_site *s=&pview_sites[i];int32_t rel;memcpy(&rel,region+s->rva+1,4);
        assert(region[s->rva]==(s->kind==PVIEW_CALL?0xe8:0xe9) && region+s->rva+5+rel==pview_tramp(i));
        assert(!memcmp(region+s->rva+PVIEW_PATCH,s->original+PVIEW_PATCH,s->length-PVIEW_PATCH)); /* the tail is left */
    }
    {   const unsigned char *t=pview_tramp(PVIEW_FOV);void *target=NULL;
        static const unsigned char tail[12]={0xff,0xd0,0xf3,0x44,0x0f,0x59,0xd8,0xf3,0x44,0x0f,0x59,0xc8};
        memcpy(&target,t+2,8);assert(t[0]==0x48 && t[1]==0xb8 && target==(void*)pview_fov_stub && !memcmp(t+10,tail,12));
        memcpy(&target,t+28,8);assert(t[22]==0xff && t[23]==0x25 && target==region+0xd93087);
        t=pview_tramp(PVIEW_SHIP);assert(t[0]==0x48 && t[1]==0x8b && t[2]==0xd8);memcpy(&target,t+5,8);assert(target==(void*)pview_external_stub);
        memcpy(&target,t+28,8);assert(target==region+0x7d4eb5);memcpy(&target,t+42,8);assert(target==region+0x7d4d93);
        t=pview_tramp(PVIEW_WEAPON);memcpy(&target,t+6,8);assert(t[0]==0xff && t[1]==0x25 && target==(void*)pview_floating_stub);}
    stats.issued=7;assert(pview_fov_stub()==1.0f && pview_cockpit_stub()==1 && !pview_external_stub() && !pview_floating_stub());
    {   float out[4];stats.issued=7;test_pnat_player[0x6c80]=0x11;input[0x9053]=1;
        assert(test_pstk_weapon_aim(region,out)==1 && test_pstk_aim_calls==1 && test_pstk_seen_point[0]==10.0f && test_pstk_seen_point[1]==20.0f &&
               test_pstk_seen_point[2]==1030.0f && test_pstk_seen_distance==1.0f && test_pstk_seen_picked==(void*)test_pnat_player &&
               !test_pstk_seen_steer && out[2]==1030.0f && pstk_aim_neutral==1 && pstk_aim_replaced==1);
        assert(test_pnat_player[0x6c80]==0x11 && input[0x9053]==1 && !*(uint64_t*)(test_pnat_player+0x6c88)); /* put back */
        input[0x9053]=0;test_pnat_player[0x6c80]=0;}
    assert(!pnat_pass(8,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pnat_stage,"stock_slot_not_occupied") &&
           pilot_stop_reason()==PSTK_STOP_REASON);
    pnat_failed=0;pilot_slots[0].player=1;
    assert(pnat_pass(8,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pnat_installed && pnat_is_live() &&
           !pilot_controls_owned && pstk_capture_start==8 && pstk_first_apply==11 && !pstk_flight && pstk_flight_known);
    /* Stock subset: no transform routers, no authority patch, no routed controller. */
    assert(*(void**)(region+PNAT_VTABLE+48)==region+0xd8a680 && !memcmp(region+PNAT_AUTHORITY_RVA,pnat_patches[0].expected,16) &&
           !pnat_route_controller && test_pnat_vector[1]==(void*)pnat_space);
    for(unsigned i=1;i<PNAT_PATCH_COUNT;i++) assert(pnat_patches[i].installed && region[pnat_patches[i].rva]==0x48);
    /* Capture on slot 1's client: SPACE edge, replay ids recorded (not run), refused logged, local run. */
    stats.issued=8;test_pstk_call_count=0;test_pnat_refreshes=0;input[0x9053]=0;
    pnat_space(8,1);
    assert(input[0x9062]==1 && input[0x9064]==1 && !*flight && test_pnat_refreshes==1 && !test_pstk_count('H') && pstk_local_active==1);
    pnat_dispatch_state(map,0x11,1);pnat_dispatch_range(map,4,0.75f);pnat_dispatch_action(map,0x12f);
    assert(!test_pstk_call_count && pstk_build.count==4);
    pnat_refused=0;pnat_dispatch_action(map,0x130);pnat_dispatch_state(map,0x18,1);assert(!test_pstk_call_count && pnat_refused==2);
    pnat_dispatch_action(map,0xa9);assert(test_pstk_count('l')==1 && pstk_build.count==4);
    /* Cursor: the stock body inside a flight window (local active, replayed off), outputs kept, zero to d95180. */
    test_pstk_call_count=0;pnat_cursor(controller);
    assert(test_pstk_calls[0].what=='C' && test_pstk_calls[0].flight==1 && !*flight && pstk_cursor_live[0]==0.25f && pstk_cursor_live_set &&
           *PNAT_F32(0x2f4b738)==0.0f && *PNAT_F32(0x2f4b73c)==0.0f);
    /* Scripted input joins the same record; foreign or refused ids are not injected. */
    assert(pstk_inject(PSTK_STATE,0xd,1) && !pstk_inject(PSTK_STATE,0x18,1) && !pstk_inject(PSTK_ACTION,0x7b,1) && !pstk_inject(PSTK_STATE,0xd,2));
    unsigned char rec[PSTK_BYTES_MAX],again[PSTK_BYTES_MAX];unsigned length=0,length2=0;const char *why="";
    stats.issued=7;assert(!pstk_capture(7,rec,&length,&why) && !strcmp(why,"not_live"));
    /* Slot 1's aim of this frame: the camera pick (zone frame) as a point in the ship's frame, and +0x9053. */
    {   float pick[4]={100.0f,0.0f,50.0f,0.0f},distance=5.0f;void *picked=test_pstk_zone;
        memcpy(test_pnat_player+0x6c60,pick,16);memcpy(test_pnat_player+0x6c80,&distance,4);memcpy(test_pnat_player+0x6c88,&picked,8);}
    input[0x9053]=1;
    test_pview_floating_value=1;
    stats.issued=8;assert(pstk_capture(8,rec,&length,&why) && length==PSTK_HEAD+5*PSTK_EVENT &&
                          rec[1]==(PSTK_FLAG_ACTIVE|PSTK_FLAG_CURSOR|PSTK_FLAG_AIM|PSTK_FLAG_VIEW) && rec[28]==1 && rec[29]==3);
    test_pview_floating_value=0;
    input[0x9053]=0;
    {   float aim[3];unsigned char steer=0;float none=-1.0f;memcpy(test_pnat_player+0x6c80,&none,4);   /* no pick: the cursor ray */
        assert(pstk_aim_sample(aim,&steer) && aim[0]==1.0f && aim[1]==2.0f && aim[2]==3.0f && !steer);
        void *ship=test_pnat_ship;float some=5.0f;memcpy(test_pnat_player+0x6c80,&some,4);memcpy(test_pnat_player+0x6c88,&ship,8); /* the own ship */
        assert(pstk_aim_sample(aim,&steer) && aim[2]==3.0f);
        memset(test_pnat_player+0x6c60,0,0x40);}
    assert(pstk_capture(8,again,&length2,&why) && length2==length && !memcmp(again,rec,length) && !pstk_build.count); /* idempotent per tick */
    pstk_record r;int decoded=pstk_decode(rec,length,&r);
    assert(decoded && r.events[0].kind==PSTK_SPACE && r.events[1].id==0x11 && r.events[2].kind==PSTK_RANGE &&
        r.events[3].id==0x12f && r.events[4].id==0xd && r.cursor[0]==0.25f && r.aim[0]==90.0f && r.aim[1]==-20.0f && r.aim[2]==20.0f &&
        r.steer==1 && r.view==3 && r.fov==0.5f && r.target==0x1055);
    /* An external camera: not cockpit, factor 1; a soft target without a portable ID is 1. */
    {   unsigned char bits=0;float fov=0;uint64_t target=0;int mode=1;memcpy(test_pview_camera+0x868,&mode,4);
        *(uint64_t*)(test_pnat_player+0x6dd0)=0x77;
        assert(pview_sample(&bits,&fov,&target) && bits==0 && fov==1.0f && target==1);
        mode=0;memcpy(test_pview_camera+0x868,&mode,4);*(uint64_t*)(test_pnat_player+0x6dd0)=0x55;}
    /* Staging: only slot 1, apply = capture + delay, the capturing client's echo must match. */
    assert(!pstk_stage(2,rec,length,8) && pstk_stage(1,rec,length,8) && pstk_stage(1,rec,length,9));
    memcpy(again,rec,length);again[43]^=1;assert(!pstk_stage(1,again,length,8)); /* echo mismatch (and not a valid id) */
    /* Before the first apply tick: no replay, flight stays 0. */
    assert(pnat_pass(9,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
           pnat_pass(10,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !*flight);
    /* Tick 11: replay on this client as on every other one. */
    stats.issued=11;test_pstk_call_count=0;test_pnat_call_count=0;input[0x9062]=1;input[0x9064]=1;
    assert(pnat_pass(11,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    assert(test_pstk_calls[0].what=='A' && test_pstk_calls[1].what=='H' && test_pstk_calls[1].id==1 && test_pstk_calls[1].object==controller &&
           test_pstk_calls[2].what=='+' && test_pstk_calls[2].object==test_pnat_player && *flight==1 && pstk_flight==1 &&
           ((double*)(region+PNAT_TIMERS))[0]==100.5 && input[0x9062]==1 && input[0x9064]==1);
    assert(test_pstk_calls[3].what=='s' && test_pstk_calls[3].id==0x11 && test_pstk_calls[3].value==1.0f && test_pstk_calls[3].flight==1);
    assert(test_pstk_calls[4].what=='g' && test_pstk_calls[4].id==4 && test_pstk_calls[4].value==0.75f && test_pstk_calls[5].what=='a' &&
           test_pstk_calls[5].id==0x12f && test_pstk_calls[6].what=='s' && test_pstk_calls[6].id==0xd && test_pstk_call_count==7);
    char text[1024];assert(pstk_frame_text(text,sizeof(text)) && !strncmp(text,"PSTOCK_1:11:5:",14) && strstr(text,":1:"));
    /* This frame's d95180 reads the replayed cursor values. */
    test_pstk_call_count=0;pnat_cursor(controller);assert(test_pstk_calls[0].flight==1 && *PNAT_F32(0x2f4b738)==0.25f &&
        *PNAT_F32(0x2f4b73c)==-0.5f);
    /* ...and every weapon of slot 1 aims at the replayed point on every client (back into the zone's frame, steering 1). */
    {   float out[4];LONG64 neutral=pstk_aim_neutral;
        assert(test_pstk_weapon_aim(region,out)==1 && test_pstk_seen_point[0]==100.0f && test_pstk_seen_point[1]==0.0f &&
               test_pstk_seen_point[2]==50.0f && test_pstk_seen_steer==1 && pstk_aim_neutral==neutral && !input[0x9053]);
        InterlockedExchange(&pstk_aim_on,0);unsigned calls=test_pstk_aim_calls;float marker=7.0f;memcpy(test_pnat_player+0x6c60,&marker,4);
        assert(test_pstk_weapon_aim(region,out)==1 && test_pstk_aim_calls==calls+1 && test_pstk_seen_point[0]==7.0f); /* off: untouched */
        InterlockedExchange(&pstk_aim_on,1);memset(test_pnat_player+0x6c60,0,0x40);
        stats.issued=12;assert(test_pstk_weapon_aim(region,out)==1 && test_pstk_seen_point[2]==1030.0f && pstk_aim_neutral==neutral+1);
        stats.issued=11;}
    /* ...and slot 1's view answers the four camera reads on every client (neutral the tick after). */
    {   unsigned char c=0,f=0;float fov=0;uint64_t target=0;
        assert(pview_fov_stub()==0.5f && pview_cockpit_stub()==1 && !pview_external_stub() && pview_floating_stub()==1);
        assert(pview_of(1,&c,&f,&fov,&target) && c==1 && f==1 && fov==0.5f && target==0x1055);
        assert(pview_of(2,&c,&f,&fov,&target) && c==1 && !f && fov==1.0f && !target && !pview_of(PILOT_SLOTS+1,&c,&f,&fov,&target)); /* no record: neutral */
        assert(vpl_slot_count()==2 && vpl_soft_target(1)==0x1055 && !vpl_soft_target(2) && !vpl_soft_target(3) && !vpl_external_view(1) &&
               !vpl_external_view(2));
        stats.issued=12;assert(pview_fov_stub()==1.0f && !pview_floating_stub() && pview_of(1,&c,&f,&fov,&target) && !target);
        stats.issued=11;}
    /* Fail closed: a missing record, flight control moved locally, a foreign handler under a replay id. */
    assert(!pnat_pass(12,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pnat_stage,"stock_missing") &&
           pilot_stop_reason()==PSTK_STOP_REASON);
    pnat_failed=0;*flight=0;
    assert(!pnat_pass(12,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pnat_stage,"stock_flight_drift"));
    *flight=1;pnat_failed=0;
    /* SPACE left on the capturing client (a menu): recorded, replayed through d9c250 and Flight Stopped. */
    stats.issued=12;test_pstk_call_count=0;pnat_space(8,0);assert(!input[0x9062] && *flight==1 && pstk_build.count==1);
    assert(pstk_capture(12,rec,&length,&why) && pstk_stage(1,rec,length,12));
    test_pnat_insert(states,0x11,(void*)test_pstk_foreign); /* a second, foreign handler under BOOST */
    pstk_record_event(PSTK_STATE,0x11,0);
    stats.issued=13;assert(pstk_capture(13,again,&length2,&why) && pstk_stage(1,again,length2,13));
    *(uint64_t*)(region+0x2f4b750)=~0ULL;*(uint64_t*)(region+0x2f4b794)=~0ULL;
    pstk_ring[0][14%PSTK_RING].present=0; /* tick 14 missing, checked below */
    stats.issued=15;test_pstk_call_count=0;
    assert(pnat_pass(15,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !*flight && pstk_flight==0 &&
           test_pstk_calls[0].what=='L' && test_pstk_calls[1].what=='-' && !*(uint64_t*)(region+0x2f4b750) && !*(uint64_t*)(region+0x2f4b794) &&
           !input[0x9062]);
    assert(!pnat_pass(16,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pnat_stage,"stock_foreign_handler"));
    pnat_failed=0;
    /* Overflow fails closed; the settings fingerprint follows options and the curve. */
    memset(&pstk_build,0,sizeof(pstk_build));
    for(unsigned i=0;i<PSTK_EVENTS_MAX;i++) assert(pstk_record_event(PSTK_STATE,0xd,i&1));
    assert(!pstk_record_event(PSTK_STATE,0xd,1) && pnat_fail_stage && !strcmp(pnat_fail_stage,"stock_overflow"));
    pnat_fail_stage=NULL;pnat_failed=0;memset(&pstk_build,0,sizeof(pstk_build));
    uint64_t fp=pstk_settings_using(region);assert(fp && fp==pstk_settings_using(region));
    region[0x2f4b9bf]^=1;assert(pstk_settings_using(region)!=fp);region[0x2f4b9bf]^=1;
    test_pstk_curve_scale=2.0f;assert(pstk_settings_using(region)!=fp);test_pstk_curve_scale=1.0f;
    input[0x912c]=1;assert(pstk_settings_using(region)!=fp);input[0x912c]=0;assert(pstk_settings_using(region)==fp);
    /* A client that does not capture: replay ids dropped, SPACE local only, no record. */
    pstk_capturing=0;LONG64 dropped=pstk_dropped;test_pstk_call_count=0;
    pnat_dispatch_state(map,0x11,1);pnat_dispatch_action(map,0xa9);pnat_space(8,1);
    assert(pstk_dropped==dropped+1 && test_pstk_count('l')==1 && !pstk_build.count && !pstk_capture(16,rec,&length,&why));
    test_pstk_call_count=0;*PNAT_F32(0x2f4b738)=0;pnat_cursor(controller);assert(!pstk_cursor_live_set || pstk_cursor_live[0]==0.25f);
    /* stop+1 restores the subset; stop+2 releases the returning share. */
    assert(pnat_pass(41,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !pnat_installed && pnat_restored &&
           test_pnat_vector[1]==region+PNAT_SPACE_RVA && !pstk_aim_installed && !pstk_aim_on &&
           *(void**)(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT)==(void*)test_pstk_aim_original && !pview_installed);
    for(unsigned i=0;i<PVIEW_SITES;i++) assert(!memcmp(region+pview_sites[i].rva,pview_sites[i].original,pview_sites[i].length));
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) assert(!memcmp(region+pnat_patches[i].rva,pnat_patches[i].expected,16));
    assert(pnat_pass(42,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !pilot_controls_owned);
    assert(pnat_status(text,sizeof(text)) && strstr(text," mode=stock ") && strstr(text," stock=replay slot=1 "));
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_slot=0;pilot_stock_slots=0;pstk_slot_mask=0;pstk_capture_slot=0;pnat_stage="off";
    pnat_failed=0;pnat_fail_stage=NULL;
    pilot_armed=0;pilot_suppress=0;pilot_source=0;pilot_slot_count=0;pilot_delay=0;pilot_controls_owned=0;pilot_failures=0;
    pilot_start=pilot_stop=0;pilot_stage="off";memset(pilot_slots,0,sizeof(pilot_slots));stats.issued=saved_issued;stats.slot=saved_slot;
    memcpy(pstk_handler_ptrs,saved_ptrs,sizeof(saved_ptrs));pstk_fn=saved_pstk;ui_thread=saved_ui_thread;
    pstk_aim_tick=0;pstk_aim_valid_replay=0;pstk_aim_installed=0;InterlockedExchange(&pstk_aim_on,0);
    pview_fn=saved_view;if(pview_page) {VirtualFree(pview_page,0,MEM_RELEASE);pview_page=NULL;}pview_installed=0;
    for(unsigned i=0;i<PILOT_SLOTS;i++) InterlockedExchange64(&pview_state[i],0);
    memset(test_pnat_universe+0x3d0,0,8);*(uint64_t*)(test_pnat_player+0x6dd0)=0;
    pnat_base=saved_image_base;pnat_fn=saved_fns;VirtualFree(region,0,MEM_RELEASE);
    printf("pilot stock codec/verdicts/capture/staging/replay/space/cursor/aim/view/settings/schedule/fail-closed tests PASS\n");
}
static void test_pilot_stock_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[4096];
    unsigned pins=sizeof(pstk_pin_table)/sizeof(*pstk_pin_table);
    for(unsigned i=0;i<pins;i++) {
        assert(pstk_pin_table[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,pstk_pin_table[i].rva,pstk_pin_table[i].size,bytes));
        assert(hash_ok(bytes,pstk_pin_table[i].size,pstk_pin_table[i].sha));
    }
    /* d88710 calls d9c590(controller, d8dd50(controller)), d9c250(controller), 70eb10/70ec00(player) in that order. */
    assert(test_pnat_file_bytes(f,sections,count,0xd88710,759,bytes));
    static const unsigned calls[][2]={{0xd888a7,0xd9c590},{0xd888da,0x70eb10},{0xd889b9,0xd9c250},{0xd889e1,0x70ec00},{0xd88892,0xd8dd50}};
    for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);i++) {
        const unsigned char *c=bytes+(calls[i][0]-0xd88710);int32_t rel;memcpy(&rel,c+1,4);
        assert(c[0]==0xe8 && calls[i][0]+5u+(unsigned)rel==calls[i][1]);
    }
    /* Weapon::Move asks the controller for the aim through vtable slot 21 (mov r13,[rax+0xa8] ... call r13), and
     * the player's controller class has 7e8650 there. */
    static const unsigned char load_slot[7]={0x4c,0x8b,0xa8,0xa8,0x00,0x00,0x00},call_r13[3]={0x41,0xff,0xd5};
    assert(test_pnat_file_bytes(f,sections,count,0x8140f1,7,bytes) && !memcmp(bytes,load_slot,7));
    assert(test_pnat_file_bytes(f,sections,count,0x8141c2,3,bytes) && !memcmp(bytes,call_r13,3));
    uint64_t slot=0;assert(test_pnat_file_bytes(f,sections,count,PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT,8,bytes));memcpy(&slot,bytes,8);
    assert(slot==0x140000000ULL+PSTK_AIM_RVA);
    /* View sites: the original bytes, one 16-byte block each, the calls' target, and the continuations the
     * trampolines jump to (mulss xmm11/xmm9 by xmm0 at d9307d is what the FOV trampoline repeats). */
    for(unsigned i=0;i<PVIEW_SITES;i++) {
        const pview_site *s=&pview_sites[i];
        assert(test_pnat_file_bytes(f,sections,count,s->rva,s->length,bytes) && !memcmp(bytes,s->original,s->length));
        assert(((s->rva+PVIEW_PATCH-1)&~15u)==(s->rva&~15u));
        if(s->kind==PVIEW_CALL) {int32_t rel;memcpy(&rel,bytes+1,4);assert(s->rva+5u+(unsigned)rel==0x9fbf50u);}
    }
    static const struct {unsigned rva,n;unsigned char b[10];} cont[]={
        {0xd9307d,10,{0xf3,0x44,0x0f,0x59,0xd8,0xf3,0x44,0x0f,0x59,0xc8}},{0xd93087,4,{0x41,0x0f,0x28,0xc3}},
        {0x54efd6,9,{0xf3,0x44,0x0f,0x59,0x85,0x70,0x0b,0x00,0x00}},{0x54efdf,8,{0x48,0x8b,0xac,0x24,0x88,0x00,0x00,0x00}},
        {0x7d4eb5,2,{0x32,0xc0}},{0x7d4d93,7,{0x48,0x8d,0x98,0x30,0x02,0x00,0x00}}};
    for(unsigned i=0;i<sizeof(cont)/sizeof(*cont);i++)
        assert(test_pnat_file_bytes(f,sections,count,cont[i].rva,cont[i].n,bytes) && !memcmp(bytes,cont[i].b,cont[i].n));
    fclose(f);
    printf("pinned native image pilot stock verified (%u functions, d88710 call order, aim slot and call site, view sites)\n",pins);
}
