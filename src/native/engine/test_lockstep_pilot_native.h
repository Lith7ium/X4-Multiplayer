/* Native controls (lockstep_pilot_native.h): routers, trampoline bytes, entry
 * patches, the re-stated dispatchers and handlers, the SPACE callback, the
 * authority predicate, request recording, the schedule and restoration, all on
 * a fake image region. The native-image test checks pins and bytes in X4.exe. */
#define TEST_PNAT_REGION 0x7000000u
typedef struct {char what;int id;int active;float value;unsigned char flight;} test_pnat_call;
static test_pnat_call test_pnat_calls[64];
static unsigned test_pnat_call_count,test_pnat_refreshes,test_pnat_matches,test_pnat_option_value=1;
static unsigned char test_pnat_assist_value=1;
static float test_pnat_throttle_args[8][4];
static unsigned test_pnat_throttles;
static __attribute__((aligned(16))) unsigned char test_pnat_input[0xa000],test_pnat_controller[0x14000],test_pnat_ship[0x400],
    test_pnat_universe[0x400],test_pnat_player[0x7000],test_pnat_cursor_object[16];
static void *test_pnat_vtable[64],*test_pnat_cursor_vtable[4],*test_pnat_vector[4],*test_pnat_vector_moved[8];
static pnat_node test_pnat_nodes[64];
static unsigned test_pnat_node_count;
static unsigned char test_pnat_flight_now(void) {return pnat_base?*PNAT_U8(PNAT_FLIGHT):0;}
static void test_pnat_log(char what,int id,int active,float value) {
    assert(test_pnat_call_count<64);
    test_pnat_calls[test_pnat_call_count++]=(test_pnat_call){what,id,active,value,test_pnat_flight_now()};
}
static void test_pnat_refresh(char present) {
    ++test_pnat_refreshes;
    uint32_t count=*(volatile uint32_t*)(pnat_base+PNAT_COUNTER);
    *PNAT_U8(PNAT_CONTEXTS)=(unsigned char)(present && !count);
}
static void *test_pnat_controller_of(void *key) {assert(key==pnat_base+PNAT_CONTROLLER_KEY);return test_pnat_controller;}
static unsigned test_pnat_eject_option=1;
static unsigned char test_pnat_option(void *options,const char *name,unsigned char fallback) {
    assert(options==test_pnat_universe+0x118);
    if(name!=(const char*)(pnat_base+PNAT_PERSISTENT_NAME)) {assert(!strcmp(name,"emergencyeject") && fallback==1);return (unsigned char)test_pnat_eject_option;}
    assert(!fallback);
    return (unsigned char)test_pnat_option_value;
}
static void test_pnat_steer_mode(void *input,char value) {assert(input==test_pnat_input);test_pnat_log('m',0,value,0);}
static void test_pnat_steer_direct(void *input,char value) {assert(input==test_pnat_input);test_pnat_log('d',0,value,0);}
static void test_pnat_steer_reset(void *input,char value) {assert(input==test_pnat_input && !value);test_pnat_log('r',0,0,0);}
static void test_pnat_throttle(float delta,char a,char b,char c) {
    assert(test_pnat_throttles<8);float *t=test_pnat_throttle_args[test_pnat_throttles++];
    t[0]=delta;t[1]=a;t[2]=b;t[3]=c;test_pnat_log('t',0,0,delta);
}
static void test_pnat_match_reset(void) {++test_pnat_matches;}
static void test_pnat_state(int id,unsigned char active) {test_pnat_log('S',id,active,0);}
static void test_pnat_range(int id,float value) {test_pnat_log('R',id,0,value);}
static void test_pnat_space_original(int context,unsigned char active) {test_pnat_log('O',context,active,0);}
static void test_pnat_cursor_original(void *controller) {assert(controller==test_pnat_controller);test_pnat_log('C',0,0,0);}
static int test_pnat_time(double *now) {*now=100.0;return 1;}
static unsigned char test_pnat_assist(void *self,unsigned char argument) {
    assert(self==test_pnat_controller && !argument);return test_pnat_assist_value;
}
static void test_pnat_cursor_slot1(void *self,char value) {assert(self==test_pnat_cursor_object && !value);test_pnat_log('1',0,0,0);}
static void test_pnat_cursor_slot2(void *self,char value) {assert(self==test_pnat_cursor_object && !value);test_pnat_log('2',0,0,0);}
static void test_pnat_handler_action(int id) {test_pnat_log('a',id,0,0);}
static void test_pnat_handler_state(int id,unsigned char active) {test_pnat_log('s',id,active,0);}
static void test_pnat_handler_range(int id,float value) {test_pnat_log('g',id,0,value);}
static void *test_pnat_vf_target(void *self,void *out,double t,unsigned char flag) {(void)t;(void)flag;test_pnat_log('b',0,0,0);return (unsigned char*)out+(self==test_pnat_controller);}
static void *test_pnat_vf_original(void *self,void *out,double t,unsigned char flag) {(void)self;(void)t;(void)flag;test_pnat_log('o',0,0,0);return out;}
/* The InputMapping insertion (1255610/12556f0/12557e0) without rebalancing:
 * equal keys go right, the header's +8 is the root, the root's parent the header. */
static void test_pnat_insert(unsigned char *header,int key,void *handler) {
    pnat_node *h=(pnat_node*)header,*n=&test_pnat_nodes[test_pnat_node_count++];
    assert(test_pnat_node_count<=64);memset(n,0,sizeof(*n));n->key=key;n->handler=handler;
    if(!h->left) {h->left=n;n->parent=h;return;}
    for(pnat_node *x=h->left;;) {
        if(x->key<=key) {if(!x->right) {x->right=n;n->parent=x;return;} x=x->right;}
        else {if(!x->left) {x->left=n;n->parent=x;return;} x=x->left;}
    }
}
static unsigned test_pnat_count(char what) {unsigned n=0;for(unsigned i=0;i<test_pnat_call_count;i++) n+=test_pnat_calls[i].what==what;return n;}
static void test_pnat_bytes(unsigned rva,const unsigned char *bytes) {memcpy(pnat_base+rva,bytes,16);}
static void test_pnat_routers(void) {
    typedef void *(*vf_fn)(void*,void*,double,unsigned char);
    unsigned char out[4];void *saved_orig[3],*saved_base[3];LONG64 hits=pnat_vf_hits[1];
    memcpy(saved_orig,pnat_vf_orig,sizeof(saved_orig));memcpy(saved_base,pnat_vf_base,sizeof(saved_base));
    for(unsigned i=0;i<3;i++) {pnat_vf_orig[i]=(void*)test_pnat_vf_original;pnat_vf_base[i]=(void*)test_pnat_vf_target;}
    test_pnat_call_count=0;pnat_route_controller=test_pnat_controller;
    assert(((vf_fn)pnat_vf7)(test_pnat_controller,out,1.0,1)==out+1 && test_pnat_calls[0].what=='b' && pnat_vf_hits[1]==hits+1);
    assert(((vf_fn)pnat_vf6)(test_pnat_ship,out,1.0,1)==out && test_pnat_calls[1].what=='o');
    assert(((vf_fn)pnat_vf10)(test_pnat_controller,out,1.0,0)==out+1 && test_pnat_calls[2].what=='b');
    pnat_route_controller=NULL;
    assert(((vf_fn)pnat_vf10)(test_pnat_controller,out,1.0,0)==out && test_pnat_calls[3].what=='o');
    memcpy(pnat_vf_orig,saved_orig,sizeof(saved_orig));memcpy(pnat_vf_base,saved_base,sizeof(saved_base));
    /* The trampoline holds the stock 18-byte prologue, then jmp [rip+x]. */
    const unsigned char *t=(const unsigned char*)pnat_cursor_trampoline;
    assert(!memcmp(t,pnat_cursor_prologue,18) && t[18]==0xff && t[19]==0x25);
}
static void test_pilot_native_controls(void) {
    unsigned char *saved_image_base=pnat_base;pnat_fns saved_fns=pnat_fn;uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region);pnat_base=region;
    pnat_fn=(pnat_fns){test_pnat_refresh,test_pnat_controller_of,test_pnat_option,test_pnat_steer_mode,test_pnat_steer_direct,
        test_pnat_steer_reset,test_pnat_throttle,test_pnat_match_reset,test_pnat_state,test_pnat_range,test_pnat_space_original,
        test_pnat_cursor_original,test_pnat_time};
    test_pnat_routers();
    /* Fake world: the input object and its maps, the controller, ship and player. */
    unsigned char *input=test_pnat_input,*controller=test_pnat_controller,*map=input+0x63e0;
    void *p=input;memcpy(region+PNAT_INPUT,&p,8);p=test_pnat_universe;memcpy(region+PNAT_UNIVERSE,&p,8);memcpy(region+PNAT_OPTIONS,&p,8);
    p=test_pnat_player;memcpy(test_pnat_universe+0x230,&p,8);
    unsigned char config[0x80]={0};float step=5.0f;memcpy(config+0x6c,&step,4);p=config;memcpy(region+PNAT_CONFIG,&p,8);
    test_pnat_vtable[49]=(void*)test_pnat_assist;p=test_pnat_vtable;memcpy(controller,&p,8);
    p=test_pnat_ship;memcpy(controller+0x28,&p,8);test_pnat_ship[0xd1]=1;
    test_pnat_cursor_vtable[1]=(void*)test_pnat_cursor_slot1;test_pnat_cursor_vtable[2]=(void*)test_pnat_cursor_slot2;
    p=test_pnat_cursor_vtable;memcpy(test_pnat_cursor_object,&p,8);p=test_pnat_cursor_object;memcpy(input+0x8fe0,&p,8);
    test_pnat_vector[0]=(void*)test_pnat_handler_action;test_pnat_vector[1]=region+PNAT_SPACE_RVA;test_pnat_vector[2]=(void*)test_pnat_handler_state;
    p=test_pnat_vector;memcpy(input+0x10,&p,8);p=test_pnat_vector+3;memcpy(input+0x18,&p,8);
    for(unsigned i=0;i<3;i++) {p=region+pnat_vf_originals[i];memcpy(region+PNAT_VTABLE+8*pnat_vf_slots[i],&p,8);}
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) test_pnat_bytes(pnat_patches[i].rva,pnat_patches[i].expected);
    *(double*)(region+PNAT_LEAD)=0.5;
    for(unsigned kind=0;kind<3;kind++) {pnat_node *h=(pnat_node*)(map+0x40*kind);memset(h,0,0x40);h->parent=h;}
    test_pnat_node_count=0;
    /* Actions: blocked travel (two handlers), stop (the stock handler is
     * re-stated, a second handler runs), an unblocked id with two handlers. */
    test_pnat_insert(map,0x200,(void*)test_pnat_handler_action);test_pnat_insert(map,0x12f,(void*)test_pnat_handler_action);
    test_pnat_insert(map,0x7b,region+PNAT_ACTION_HANDLER);test_pnat_insert(map,0x300,(void*)test_pnat_handler_action);
    test_pnat_insert(map,0x12f,(void*)test_pnat_handler_action);test_pnat_insert(map,0x7b,(void*)test_pnat_handler_action);
    test_pnat_insert(map,0x200,(void*)test_pnat_handler_action);test_pnat_insert(map,0x1,(void*)test_pnat_handler_action);
    /* States and ranges. */
    unsigned char *states=map+0x40,*ranges=map+0x80;
    test_pnat_insert(states,0x18,(void*)test_pnat_handler_state);test_pnat_insert(states,0xd,region+PNAT_STATE_HANDLER);
    test_pnat_insert(states,0xf,region+PNAT_STATE_HANDLER);test_pnat_insert(states,0x10,region+PNAT_STATE_HANDLER);
    test_pnat_insert(states,0x2,(void*)test_pnat_handler_state);test_pnat_insert(states,0x18,(void*)test_pnat_handler_state);
    test_pnat_insert(ranges,4,region+PNAT_RANGE_HANDLER);test_pnat_insert(ranges,10,region+PNAT_RANGE_HANDLER);
    test_pnat_insert(ranges,0x11,(void*)test_pnat_handler_range);test_pnat_insert(ranges,1,(void*)test_pnat_handler_range);
    /* Equal range walk against a brute-force in-order count. */
    for(int key=0;key<0x310;key++) {
        pnat_node *upper=NULL,*n=pnat_first((pnat_node*)map,key,&upper);unsigned walked=0,expected=0;
        for(unsigned i=0;i<test_pnat_node_count;i++) {
            pnat_node *x=&test_pnat_nodes[i];int in_actions=0;
            for(pnat_node *y=x;y;y=y->parent==y?NULL:y->parent) if(y==(pnat_node*)map) {in_actions=1;break;}
            expected+=in_actions && x->key==key;
        }
        for(pnat_node *x=n;n && x!=upper;x=pnat_next(x)) {assert(x->key==key);++walked;}
        assert(walked==expected);
    }
    /* Not live: every handler runs as in 1255cd0/1255dd0/1255ee0. */
    pnat_tables();pnat_armed=1;pnat_capture_requests=1;InterlockedExchange(&pnat_live,0);
    test_pnat_call_count=0;pnat_dispatch_action(map,0x12f);assert(test_pnat_count('a')==2);
    test_pnat_call_count=0;pnat_dispatch_state(map,0x18,1);assert(test_pnat_count('s')==2 && test_pnat_calls[0].active==1);
    test_pnat_call_count=0;pnat_dispatch_action(map,0x999);assert(!test_pnat_call_count);
    /* Schedule: start 10, stop 20; take at 7, install at 8. */
    pilot_armed=1;pilot_suppress=1;pilot_start=10;pilot_stop=20;pilot_controls_owned=0;pnat_failed=0;pnat_fail_stage=NULL;
    pilot_failures=0;pnat_installed=0;pnat_restored=1;pnat_stage="armed";
    uint32_t *counter=(uint32_t*)(region+PNAT_COUNTER);unsigned char *contexts=region+PNAT_CONTEXTS,*flight=region+PNAT_FLIGHT;
    *counter=0;*contexts=1;*flight=0;test_pnat_refreshes=0;
    assert(pnat_pass(7,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pilot_controls_owned && *counter==1 &&
           !*contexts && !strcmp(pnat_stage,"parked"));
    assert(pnat_pass(8,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pnat_installed && pnat_is_live() &&
           !pilot_controls_owned && !*counter && *contexts && !strcmp(pnat_stage,"live"));
    /* Installed: vtable routers, five entry jumps, the callback entry. */
    assert(*(void**)(region+PNAT_VTABLE+48)==(void*)pnat_vf6 && *(void**)(region+PNAT_VTABLE+56)==(void*)pnat_vf7 &&
           *(void**)(region+PNAT_VTABLE+80)==(void*)pnat_vf10 && pnat_vf_base[0]==region+0xea5a70 && pnat_vf_orig[2]==region+0xd8b790);
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) {
        unsigned char *e=region+pnat_patches[i].rva;void *target=NULL;memcpy(&target,e+2,8);
        assert(e[0]==0x48 && e[1]==0xb8 && target==pnat_patches[i].target && e[10]==0xff && e[11]==0xe0 && e[12]==0xcc && e[15]==0xcc);
    }
    assert(test_pnat_vector[1]==(void*)pnat_space && pnat_route_controller==controller && pnat_cursor_resume==region+PNAT_CURSOR_RESUME);
    /* (1) Authority: routed 1; other controllers get the stock predicate. */
    pnat_auth_override=0;assert(pnat_authority(controller)==1);
    *flight=1;assert(pnat_authority(controller)==1 && pnat_auth_override==1);*flight=0;
    *flight=1;assert(pnat_authority_stock(controller)==0);test_pnat_ship[0xd1]=0;assert(pnat_authority_stock(controller)==1);
    test_pnat_ship[0xd1]=1;*(uint64_t*)(region+PNAT_AUTOPILOT)=7;assert(pnat_authority_stock(controller)==1);
    *(uint64_t*)(region+PNAT_AUTOPILOT)=0;*flight=0;assert(pnat_authority_stock(controller)==1);
    *PNAT_U8(PNAT_HIGHWAY)=1;assert(pnat_authority_stock(controller)==0);*PNAT_U8(PNAT_HIGHWAY)=0;
    test_pnat_assist_value=0;assert(pnat_authority_stock(controller)==0);test_pnat_assist_value=1;
    /* Live pass: invariants hold, no UI hold. */
    pnat_ui_passes=0;assert(pnat_pass(9,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !pnat_ui_passes &&
        pnat_natural==1 && pnat_assist==1 && !pnat_flight_seen);
    *counter=1;assert(pnat_ui_hold());assert(pnat_pass(10,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pnat_ui_passes==1);
    *counter=0;assert(!pnat_ui_hold());
    /* (4) Dispatch while live. */
    stats.issued=11;pnat_refusal_head=pnat_refusal_tail=0;pnat_refused=0;
    test_pnat_call_count=0;pnat_dispatch_action(map,0x12f);assert(!test_pnat_call_count && pnat_refused==1);
    test_pnat_call_count=0;pnat_dispatch_action(map,0x200);assert(test_pnat_count('a')==2);
    *PNAT_F32(PNAT_SETPOINT)=0.5f;test_pnat_throttles=0;test_pnat_matches=0;
    test_pnat_call_count=0;pnat_dispatch_action(map,0x7b);
    assert(test_pnat_count('a')==1 && test_pnat_throttles==1 && test_pnat_throttle_args[0][0]==-0.5f && test_pnat_throttle_args[0][2]==1 &&
           !test_pnat_throttle_args[0][1] && !test_pnat_throttle_args[0][3] && *PNAT_U8(PNAT_FULL_STOP)==1 && test_pnat_matches==1);
    test_pnat_call_count=0;pnat_dispatch_state(map,0x18,1);assert(pnat_fire_buttons()==1); /* refused, still carried */
    pnat_dispatch_state(map,0x18,0);assert(!test_pnat_call_count && pnat_refused==3 && !pnat_fire_buttons());
    test_pnat_call_count=0;pnat_dispatch_state(map,0xd,1);
    assert(!test_pnat_call_count && *PNAT_U8(PNAT_KEY_ACCEL)==1 && *PNAT_F32(PNAT_BASE_SETPOINT)==0.5f);
    *PNAT_F32(PNAT_AXIS_LAST)=0;pnat_dispatch_state(map,0xd,0);assert(!*PNAT_U8(PNAT_KEY_ACCEL) && *(double*)(region+PNAT_HOLD)==-1.0);
    pnat_dispatch_state(map,0xe,1);assert(!*PNAT_U8(PNAT_KEY_DECEL) && !test_pnat_call_count); /* 0xe has no node: nothing runs */
    test_pnat_call_count=0;pnat_dispatch_state(map,0xf,1);
    assert(test_pnat_count('S')==1 && test_pnat_calls[0].flight==1 && !*flight); /* full forward inside a flight-control window */
    test_pnat_throttles=0;test_pnat_call_count=0;pnat_dispatch_state(map,0x10,1);pnat_dispatch_state(map,0x10,1);
    assert(test_pnat_throttles==1 && test_pnat_throttle_args[0][0]==-1.5f && test_pnat_throttle_args[0][3]==1 && *PNAT_U8(PNAT_KEY_REVERSE)==1);
    pnat_dispatch_state(map,0x10,0);assert(test_pnat_throttles==2 && test_pnat_throttle_args[1][0]==-0.5f && !*PNAT_U8(PNAT_KEY_REVERSE));
    test_pnat_call_count=0;pnat_dispatch_state(map,0x2,1);assert(test_pnat_count('s')==1 && test_pnat_calls[0].id==2 && !test_pnat_calls[0].flight);
    test_pnat_call_count=0;pnat_dispatch_range(map,4,0.25f);
    assert(test_pnat_count('R')==1 && test_pnat_calls[0].flight==1 && test_pnat_calls[0].value==0.25f && !*flight);
    test_pnat_throttles=0;*PNAT_F32(PNAT_BASE_SETPOINT)=0;pnat_dispatch_range(map,10,2.0f);
    assert(test_pnat_throttles==1 && fabsf(test_pnat_throttle_args[0][0]-2.0f*5.0f*0.008333334f)<1e-7f && test_pnat_throttle_args[0][1]==1 &&
           test_pnat_throttle_args[0][2]==1 && !test_pnat_throttle_args[0][3] && *PNAT_F32(PNAT_BASE_SETPOINT)==0.5f);
    pnat_dispatch_range(map,10,0.0f);assert(test_pnat_throttles==1);
    LONG64 refused=pnat_refused;unsigned head=pnat_refusal_head;
    test_pnat_call_count=0;pnat_dispatch_range(map,0x11,0.5f);pnat_dispatch_range(map,0x11,0.6f);pnat_dispatch_range(map,0x11,0.0f);
    pnat_dispatch_range(map,0x11,-0.7f);
    assert(!test_pnat_call_count && pnat_refused==refused+4 && pnat_refusal_head==head+2); /* two deflection edges logged */
    assert(pnat_fire_buttons()==1);
    test_pnat_call_count=0;pnat_dispatch_range(map,1,0.3f);assert(test_pnat_count('g')==1 && !test_pnat_calls[0].flight);
    char text[2048];int n=pnat_refusals_text(text,sizeof(text));
    assert(n>0 && !strcmp(text,"11:action:0x12f:TOGGLE_TRAVEL_MODE:1,11:state:0x18:FIRE_PRIMARY_WEAPON:1,"
        "11:range:0x11:FIRE_PRIMARY_WEAPON:1,11:range:0x11:FIRE_PRIMARY_WEAPON:-1"));
    assert(!pnat_refusals_text(text,sizeof(text)));
    /* (3) SPACE: deactivation then activation, input side only. */
    test_pnat_call_count=0;test_pnat_refreshes=0;input[0x9056]=1;input[0x9058]=1;input[0x9062]=1;input[0x9064]=1;
    *(uint64_t*)(region+0x2f4b750)=~0ULL;*(uint32_t*)(region+0x2f4b790)=~0u;*(uint64_t*)(region+0x2f4b794)=~0ULL;
    pnat_space(8,0);
    assert(!pnat_fire_buttons()); /* SPACE left: no trigger stays held */
    assert(!input[0x9056] && !input[0x9058] && *PNAT_U8(PNAT_STEER_SAVED)==1 && *PNAT_U8(PNAT_DIRECT_SAVED)==1 && !input[0x9062] && !input[0x9064] &&
           !*(uint64_t*)(region+0x2f4b750) && !*(uint32_t*)(region+0x2f4b790) && !*(uint64_t*)(region+0x2f4b794) &&
           ((double*)(region+PNAT_TIMERS))[4]==100.5 && !*flight && test_pnat_refreshes==1 && test_pnat_count('1')==1 && test_pnat_count('r')==1 &&
           test_pnat_count('2')==1 && !test_pnat_count('O'));
    input[0x9053]=1;test_pnat_call_count=0;pnat_space(8,1);
    assert(input[0x9062]==1 && input[0x9064]==1 && !*flight && test_pnat_calls[0].what=='m' && test_pnat_calls[0].active==1 &&
           test_pnat_calls[1].what=='d' && !*PNAT_U8(PNAT_STEER_SAVED) && !*PNAT_U8(PNAT_DIRECT_SAVED) && ((double*)(region+PNAT_TIMERS))[0]==100.5);
    test_pnat_call_count=0;pnat_space(9,1);assert(!test_pnat_call_count);
    /* (5) Cursor steering inside a window; outside the preconditions with flight control 0. */
    test_pnat_call_count=0;pnat_cursor(controller);assert(test_pnat_calls[0].what=='C' && test_pnat_calls[0].flight==1 && !*flight);
    *(uint64_t*)(region+PNAT_AUTOPILOT)=5;test_pnat_call_count=0;pnat_cursor(controller);
    assert(test_pnat_calls[0].what=='C' && !test_pnat_calls[0].flight);*(uint64_t*)(region+PNAT_AUTOPILOT)=0;
    /* (6) The controller's own requests: recorded, replaced by zero. */
    assert(pnat_request_owner(controller+PNAT_LINEAR)==controller && pnat_request_owner(controller+PNAT_ANGULAR)==controller &&
           !pnat_request_owner(controller+0x100));
    stats.issued=12;test_pilot_seen=-1;
    static const double values[6]={0.25,-0.5,0.125,10,-20,150};
    for(unsigned g=0;g<2;g++) for(unsigned a=0;a<3;a++) {
        assert(pnat_request(test_pilot_original,controller+(g?PNAT_LINEAR:PNAT_ANGULAR),values[g*3+a],(int)a,0.0001,controller)==0x5150);
        assert(test_pilot_seen==0.0);
    }
    assert(pnat_req_mask==63 && pnat_req_tick==12 && pnat_req_values[2]==0.125f && pnat_req_values[5]==150.0f);
    pilot_record r;memset(&r,0,sizeof(r));const float limits[8]={300,0,-100,40,50,0.5f,1.0f,0.25f};
    assert(pnat_request_record(12,limits,&r) && (r.mode&PILOT_MODE_REQUESTS));
    assert(r.q[3]==pilot_quantize(0.25f) && r.q[4]==pilot_quantize(-1.0f) && r.q[5]==pilot_quantize(0.5f) && r.q[0]==pilot_quantize(0.25f) &&
           r.q[1]==pilot_quantize(-0.4f) && r.q[2]==pilot_quantize(0.5f));
    assert(fabsf(pilot_target(&r,limits,0,0,NULL)-0.25f)<1e-4f && fabsf(pilot_target(&r,limits,1,2,NULL)-150.0f)<0.01f);
    pnat_req_values[5]=-50.0f;assert(pnat_request_record(12,limits,&r) && r.q[2]==pilot_quantize(-0.5f));
    assert(!pnat_request_record(13,limits,&r));
    unsigned char b[PILOT_RECORD_BYTES];pilot_record d;pilot_encode(&r,b);assert(pilot_decode(b,&d) && d.mode==r.mode);
    /* Fail closed: flight control, assist, activity, a patch, the callback. */
    *flight=1;assert(!pnat_pass(13,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
        !strcmp(pnat_stage,"native_flight_control") && pilot_stop_reason()==PNAT_STOP_REASON);
    *flight=0;pnat_failed=0;assert(pilot_stop_reason()==91u);
    test_pnat_assist_value=0;assert(!pnat_pass(13,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    test_pnat_assist_value=1;pnat_failed=0;
    *(int*)(test_pnat_player+0x6ab4)=1;assert(!pnat_pass(13,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
        !strcmp(pnat_stage,"native_player_activity"));
    test_pnat_call_count=0;pnat_dispatch_state(map,0xd,1); /* a restatement refuses outside its preconditions */
    assert(pnat_fail_stage && !strcmp(pnat_fail_stage,"input_precondition") && pilot_failures==1);
    *(int*)(test_pnat_player+0x6ab4)=0;pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;
    unsigned char saved_byte=region[0xd8dd50+15];region[0xd8dd50+15]=0x90;
    assert(!pnat_pass(14,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pnat_stage,"native_patch_lost"));
    region[0xd8dd50+15]=saved_byte;pnat_failed=0;
    /* A grown callback vector: found again by value. */
    memcpy(test_pnat_vector_moved,test_pnat_vector,sizeof(test_pnat_vector));test_pnat_vector[1]=NULL;
    p=test_pnat_vector_moved;memcpy(input+0x10,&p,8);p=test_pnat_vector_moved+4;memcpy(input+0x18,&p,8);
    assert(pnat_pass(15,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    /* stop+1: everything restored, then the stock path gets a share; stop+2 releases it. */
    assert(pnat_pass(21,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !pnat_installed && pnat_restored &&
           !pnat_is_live() && !pnat_route_controller && pilot_controls_owned && *counter==1 && !strcmp(pnat_stage,"returning"));
    assert(test_pnat_vector_moved[1]==region+PNAT_SPACE_RVA && *(void**)(region+PNAT_VTABLE+48)==region+0xd8a680 &&
           *(void**)(region+PNAT_VTABLE+80)==region+0xd8b790);
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) assert(!memcmp(region+pnat_patches[i].rva,pnat_patches[i].expected,16));
    assert(pnat_pass(22,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !pilot_controls_owned && !*counter &&
           !strcmp(pnat_stage,"returned"));
    /* Installation refuses a foreign entry byte and leaves nothing behind. */
    p=test_pnat_vector;memcpy(input+0x10,&p,8);p=test_pnat_vector+3;memcpy(input+0x18,&p,8);test_pnat_vector[1]=region+PNAT_SPACE_RVA;
    region[0x1255dd0]=0xe9;assert(!pnat_install(controller) && !pnat_installed && pnat_restored && test_pnat_vector[1]==region+PNAT_SPACE_RVA);
    assert(*(void**)(region+PNAT_VTABLE+56)==region+0xd8b3e0 && !memcmp(region+0xd8dd50,pnat_patches[0].expected,16));
    region[0x1255dd0]=0x48;
    test_pnat_vector[0]=region+PNAT_SPACE_RVA;assert(!pnat_install(controller) && !pnat_installed); /* two stock entries */
    test_pnat_vector[0]=(void*)test_pnat_handler_action;
    assert(pnat_install(controller) && pnat_installed);pnat_restore();assert(!pnat_installed && pnat_restored);
    /* Not live: SPACE goes to the stock callback. */
    test_pnat_call_count=0;pnat_space(8,1);assert(test_pnat_calls[0].what=='O' && test_pnat_calls[0].active==1);
    pnat_armed=0;pnat_stage="off";pnat_failed=0;pnat_fail_stage=NULL;pilot_armed=0;pilot_suppress=0;pilot_controls_owned=0;pilot_failures=0;
    pilot_start=pilot_stop=0;pilot_stage="off";stats.issued=saved_issued;stats.slot=saved_slot;
    pnat_base=saved_image_base;pnat_fn=saved_fns;VirtualFree(region,0,MEM_RELEASE);
    printf("pilot native controls routers/trampoline/patches/dispatch/handlers/space/cursor/requests/schedule/restore tests PASS\n");
}
static int test_pnat_file_bytes(FILE *f,const IMAGE_SECTION_HEADER *sections,unsigned count,unsigned rva,unsigned size,unsigned char *out) {
    for(unsigned j=0;j<count;j++)
        if(rva>=sections[j].VirtualAddress && rva+size<=sections[j].VirtualAddress+sections[j].SizeOfRawData)
            return !fseek(f,(long)(sections[j].PointerToRawData+rva-sections[j].VirtualAddress),SEEK_SET) && fread(out,1,size,f)==size;
    return 0;
}
static void test_pilot_native_controls_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[7300];
    unsigned pins=sizeof(pnat_pin_table)/sizeof(*pnat_pin_table);
    for(unsigned i=0;i<pins;i++) {
        assert(pnat_pin_table[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,pnat_pin_table[i].rva,pnat_pin_table[i].size,bytes));
        assert(hash_ok(bytes,pnat_pin_table[i].size,pnat_pin_table[i].sha));
    }
    /* The bytes each patch expects, the trampoline's copy, the routed slots. */
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++)
        assert(test_pnat_file_bytes(f,sections,count,pnat_patches[i].rva,16,bytes) && !memcmp(bytes,pnat_patches[i].expected,16));
    assert(test_pnat_file_bytes(f,sections,count,PNAT_CURSOR_RVA,18,bytes) && !memcmp(bytes,pnat_cursor_prologue,18) &&
           !memcmp((const void*)pnat_cursor_trampoline,bytes,18));
    uint64_t slot=0;
    for(unsigned i=0;i<PNAT_SLOTS_ROUTED;i++)
        assert(test_pnat_file_bytes(f,sections,count,PNAT_VTABLE+8*pnat_vf_slots[i],8,(unsigned char*)&slot) &&
               slot==nt.OptionalHeader.ImageBase+pnat_vf_originals[i]);
    assert(test_pnat_file_bytes(f,sections,count,PNAT_VTABLE+8*49,8,(unsigned char*)&slot) && slot==nt.OptionalHeader.ImageBase+0xd9bf70);
    assert(test_pnat_file_bytes(f,sections,count,PNAT_PERSISTENT_NAME,24,bytes) && !memcmp(bytes,"persistentmousesteering",24));
    double lead=0;assert(test_pnat_file_bytes(f,sections,count,PNAT_LEAD,8,(unsigned char*)&lead) && lead==0.5);
    fclose(f);
    printf("pinned native image pilot native controls verified (%u functions, %u entries, 3 vtable slots, trampoline)\n",pins,(unsigned)PNAT_PATCH_COUNT);
}
