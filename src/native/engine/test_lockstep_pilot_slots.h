/* Stock piloting of slots 2..N (lockstep_pilot_slots.h) on the fake image region:
 * arming, the factory player, the seat inside slot 2's scope, the vtable copy
 * (scoped upkeep and environment entries, the 6/7/10 routes, counting thunks),
 * the re-pointed d95180 call, client 2's capture and staging of slot 2, slot 2's
 * replay inside its scope, the authority answers, the release and the
 * fail-closed paths. The image test checks every byte the module relies on. */
static __attribute__((aligned(16))) unsigned char test_pslot_p2[0x7000],test_pslot_ship2[0xc00],test_pslot_room[0x400],test_pslot_c2[0x400],
    test_pslot_agent2[0x40],test_pslot_macro[0x80],test_pslot_render[0x10],test_pslot_post[0x200];
static void *test_pslot_ship_vt[1100],*test_pslot_room_vt[600],*test_pslot_agent_vt[32];
static uint64_t test_pslot_rng;
static int test_pslot_inner_route;
static unsigned char *test_pslot_parent=test_pslot_room;
static unsigned char test_pslot_local_at_take[2];
typedef struct {char what;void *object,*singleton;unsigned char flight;} test_pslot_call;
static test_pslot_call test_pslot_calls[64];
static unsigned test_pslot_call_count;
static unsigned char *test_pslot_singleton(void) {unsigned char *s=NULL;memcpy(&s,test_pnat_universe+VPS_SINGLETON,8);return s;}
static void test_pslot_log(char what,void *object) {
    assert(test_pslot_call_count<64);
    test_pslot_calls[test_pslot_call_count++]=(test_pslot_call){what,object,test_pslot_singleton(),*PNAT_U8(PNAT_FLIGHT)};
}
static unsigned test_pslot_count(char what) {unsigned n=0;for(unsigned i=0;i<test_pslot_call_count;i++) n+=test_pslot_calls[i].what==what;return n;}
static uint64_t *test_pslot_rng_word(void) {return &test_pslot_rng;}
/* The factory as X4's: a new player, the singleton repointed, the old player's Universe reference dropped. */
static void **test_pslot_factory(void *universe,void **out,void *parent,void *conn,void *a5,void *a6,void *macro,void *a8,uint64_t activate,
    void *a10,uint64_t a11,const void *s12,const void *s13,const void *s14,void *a15,uint64_t mode,uint64_t post) {
    assert(universe==test_pnat_universe && !parent && !conn && !a5 && !a6 && macro==test_pslot_macro && !a8 && activate==1 && !a10 && !a11 &&
           s12==pnat_base+VPS_EMPTY_STRING && s13==s12 && s14==s12 && !a15 && !mode && post==1);
    unsigned char *p1=test_pslot_singleton(),*p2=test_pslot_p2;uintptr_t vt=(uintptr_t)(pnat_base+VPS_PLAYER_VTABLE);void *m=test_pslot_macro;
    memset(test_pslot_p2,0,sizeof(test_pslot_p2));memcpy(p2,&vt,8);memcpy(p2+0x60,&m,8);*(int*)(p2+0x40)=2;
    memcpy(test_pnat_universe+VPS_SINGLETON,&p2,8);--*(int*)(p1+0x40);test_pslot_rng+=2;
    *out=p2;return out;
}
static void *test_pslot_controller_of(void *key) {
    assert(key==pnat_base+PNAT_CONTROLLER_KEY);
    if(test_pslot_singleton()!=test_pslot_p2) return test_pnat_controller;
    void *c=NULL;memcpy(&c,test_pslot_ship2+0x200,8);return c==(void*)test_pslot_c2?c:NULL;
}
static unsigned char test_pslot_room_is(void *self,int cls) {assert(self==test_pslot_room);return cls==PSLOT_ROOM_CLASS;}
static unsigned char test_pslot_agent_is(void *self,int cls) {assert(self==test_pslot_agent2);return cls==0x2e;}
static void *test_pslot_agent_next(void *self) {assert(self==test_pslot_agent2);return NULL;}
static unsigned char test_pslot_c_is(void *self,int cls) {assert(self==test_pslot_c2);return cls==PSLOT_CONTROLLER_CLASS;}
static void *test_pslot_c_next(void *self) {assert(self==test_pslot_c2);return test_pslot_agent2;}
static unsigned char test_pslot_assist_value=1,test_pslot_mode_value;
static unsigned char test_pslot_c_assist(void *self,unsigned char argument) {assert(self==test_pslot_c2);return argument?(unsigned char)!test_pslot_assist_value:test_pslot_assist_value;}
static unsigned char test_pslot_c_mode(void *self) {assert(self==test_pslot_c2);return test_pslot_mode_value;}
static int test_pslot_c_getter(void *self) {assert(self==test_pslot_c2);return 13;}
static void test_pslot_c_other(void) {assert(0);}
/* Take control as Controllable::vfunc1074: inside slot 2's scope, a PlayerController on top, the bank reset. */
static void test_pslot_take(void *ship) {
    unsigned char *p2=test_pslot_p2,*parent=NULL;void *begin=NULL,*end=NULL,*first=NULL;
    assert(ship==test_pslot_ship2 && test_pslot_singleton()==p2 && pstk_scope_slot==2);
    memcpy(&parent,p2+0x70,8);memcpy(&begin,p2+0x308,8);memcpy(&end,p2+0x310,8);memcpy(&first,(void**)begin+2,8);
    assert(parent==test_pslot_parent && (unsigned char*)end-(unsigned char*)begin==3*8 && first==ship); /* the pilot post's index 2 */
    test_pslot_local_at_take[0]=*PNAT_U8(0x2f4b7e0);test_pslot_local_at_take[1]=*PNAT_U8(PNAT_CONTEXTS);
    void *vt=pnat_base+PSLOT_VTABLE,*s=ship,*c=test_pslot_c2;memset(test_pslot_c2,0,sizeof(test_pslot_c2));
    memcpy(test_pslot_c2,&vt,8);memcpy(test_pslot_c2+0x28,&s,8);memcpy(test_pslot_ship2+0x200,&c,8);
    *PNAT_U8(0x2f4b750)=0x5a;*PNAT_U8(PNAT_FLIGHT)=0;   /* d84c20 in slot 2's scope */
    test_pslot_log('T',ship);
}
static void test_pslot_release(void *ship) {
    assert(ship==test_pslot_ship2 && test_pslot_singleton()==test_pslot_p2 && *(void**)test_pslot_c2==(void*)(pnat_base+PSLOT_VTABLE));
    void *a=test_pslot_agent2;memcpy(test_pslot_ship2+0x200,&a,8);test_pslot_log('X',ship);
}
static void test_pslot_update(void *controller,double now) {assert(now==100.0);test_pslot_log('U',controller);}
static void *test_pslot_orig(void *self,void *out,double t,unsigned char flag) {(void)t;(void)flag;test_pslot_log('o',self);return out;}
static void *test_pslot_agent(void *self,void *out,double t,unsigned char flag) {(void)t;(void)flag;test_pslot_log('a',self);return out;}
static void *test_pslot_player(void *self,void *out,double t,unsigned char flag) {(void)t;(void)flag;test_pslot_log('p',self);return out;}
static void *test_pslot_route(void *controller,unsigned slot) {
    void **vt=*(void***)controller;return ((void*(*)(void*,void*,double,unsigned char))vt[slot])(controller,controller,1.0,0);
}
static void test_pslot_upkeep(void *controller,double now) {
    assert(now==100.0);test_pslot_log('K',controller);
    if(test_pslot_inner_route) test_pslot_route(controller,6);   /* inside the scope: the original */
}
static void test_pslot_environment(void *controller,void *a,void *b) {assert(a==(void*)1 && b==(void*)2);test_pslot_log('E',controller);}
static void test_pslot_owner(void *controller,void *a,void *b,void *c) {assert(a==(void*)1 && b==(void*)2 && c==(void*)3);test_pslot_log('O',controller);}
static void test_pslot_change(void *controller,void *a,void *b,char c) {assert(a==(void*)1 && b==(void*)2 && c==4);test_pslot_log('G',controller);}
static DWORD WINAPI test_pslot_foreign_thread(LPVOID controller) {
    void **vt=*(void***)controller;((void(*)(void*,double))vt[33])(controller,100.0);return 0;
}
static unsigned test_pslot_store_offset(unsigned rva) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(rva>=r->offset && rva<r->offset+r->size) return n+(rva-r->offset);
        n+=r->size;
    }
    assert(0);return 0;
}
/* gate.actor on a stock slot's ship (stock-J1b: refused with stage=controller_type at the seat). */
static double test_pslot_lua_args[3];
static char test_pslot_lua_text[8192];
static int test_pslot_lua_nil_seen;
static double test_pslot_lua_number(lua_State *L,int index) {(void)L;assert(index>=1 && index<=3);return test_pslot_lua_args[index-1];}
static void test_pslot_lua_nil(lua_State *L) {(void)L;test_pslot_lua_nil_seen=1;}
static void test_pslot_lua_string(lua_State *L,const char *text,size_t length) {
    (void)L;assert(length<sizeof(test_pslot_lua_text));memcpy(test_pslot_lua_text,text,length);test_pslot_lua_text[length]=0;
}
static void *test_pslot_actor_find(void *map,uint64_t id,unsigned kind) {(void)map;assert(id==0x176ad5 && kind==4);return test_pslot_ship2;}
static void test_pslot_actor_call(void) {
    test_pslot_lua_args[0]=(double)0x176ad5;test_pslot_lua_args[1]=3;test_pslot_lua_args[2]=0;
    test_pslot_lua_nil_seen=0;test_pslot_lua_text[0]=0;lua_actor(NULL);
}
static void test_pslot_actor(void) {
    static __attribute__((aligned(16))) unsigned char agent[0x1000],model[0x400];
    double (*old_number)(lua_State *,int)=p_lua_tonumber;void (*old_nil)(lua_State *)=p_lua_pushnil;void (*old_string)(lua_State *,const char *,size_t)=p_lua_pushlstring;
    int saved_agent=agent_installed,saved_flight=flight_installed,saved_initial=flight_initial,saved_spline=spline_installed,saved_params=params_enabled;
    int saved_configured=snapshot_configured,saved_gravidar=gravidar_initial;void *saved_component=actor_components[2];uint64_t saved_id=actor_ids[2];
    p_lua_tonumber=test_pslot_lua_number;p_lua_pushnil=test_pslot_lua_nil;p_lua_pushlstring=test_pslot_lua_string;
    agent_installed=0;flight_installed=0;flight_initial=1;spline_installed=0;params_enabled=0;snapshot_configured=0;gravidar_initial=0;
    actor_find_override=test_pslot_actor_find;
    void *map=test_pslot_ship2;memcpy(image+0x6cf1500,&map,8);
    uint64_t id=0x176ad5;void *c=test_pslot_c2,*slots=agent+0xad0,*zero=NULL;
    memset(test_pslot_ship2,0,sizeof(test_pslot_ship2));memcpy(test_pslot_ship2+8,&id,8);memcpy(test_pslot_ship2+0x200,&c,8);
    memset(test_pslot_c2,0,sizeof(test_pslot_c2));memcpy(test_pslot_c2+0x30,&slots,8);memset(agent,0,sizeof(agent));
    {void *copy=&pslots[2].vt.slots[0];memcpy(test_pslot_c2,&copy,8);}
    memcpy(test_pslot_c2+0x40,(void*[]){agent},8);{void *owner=test_pslot_ship2;memcpy(agent+0xef0,&owner,8);}
    /* The slot's PlayerController: the row, with no Steering model, with a model of another class, with the spline. */
    test_pslot_actor_call();
    assert(!test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,";current=") && strstr(test_pslot_lua_text,";previous=") &&
           strstr(test_pslot_lua_text,";steering=none:0") && strstr(test_pslot_lua_text,";nav_refresh="));
    {void *m=model,*vt=image+0x123;memcpy(agent+0xf10,&m,8);memcpy(model,&vt,8);}
    test_pslot_actor_call();assert(!test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,";steering=slot:0:123"));
    {void *vt=image+0x2c4f480;memcpy(model,&vt,8);}
    test_pslot_actor_call();assert(!test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,";steering=spline:0"));
    memcpy(agent+0xf10,&zero,8);
    /* The Agent keeps its identity under the slot's controller; a stock PlayerController is not a slot's. */
    actor_components[2]=test_pslot_ship2;actor_ids[2]=id;
    assert(agent_identity(2,agent));
    {void *vt=image+0x2c39488;memcpy(test_pslot_c2,&vt,8);}
    assert(!agent_identity(2,agent));
    test_pslot_actor_call();assert(test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,"stage=controller_type"));
    /* A NavigationController is as before, any other class refused. */
    {void *vt=image+0x2c51158;memcpy(test_pslot_c2,&vt,8);}
    test_pslot_actor_call();assert(!test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,";steering=none:0"));assert(agent_identity(2,agent));
    {void *vt=image+0x1000;memcpy(test_pslot_c2,&vt,8);}
    test_pslot_actor_call();assert(test_pslot_lua_nil_seen && strstr(test_pslot_lua_text,"stage=controller_type detail_rva=1000"));
    actor_find_override=NULL;memset(image+0x6cf1500,0,8);
    p_lua_tonumber=old_number;p_lua_pushnil=old_nil;p_lua_pushlstring=old_string;
    agent_installed=saved_agent;flight_installed=saved_flight;flight_initial=saved_initial;spline_installed=saved_spline;params_enabled=saved_params;
    snapshot_configured=saved_configured;gravidar_initial=saved_gravidar;actor_components[2]=saved_component;actor_ids[2]=saved_id;
}
/* Presentation fixtures: X4's calls as seen from the scope. */
static __attribute__((aligned(16))) unsigned char test_pres_self[0x200],test_pres_other_camera[16];
static unsigned test_pres_ui_calls,test_pres_refills,test_pres_renders,test_pres_local_calls;
static int test_pres_fault,test_pres_write,test_pres_off,test_pres_pick_target;
static uint64_t test_pres_target_seen;
static unsigned char test_pres_bank_slot,test_pres_bank_canonical;
/* In the scope the canonical player stays the singleton and finds slot 2's room, ship and bank. */
static void *test_pres_room_now(void) {void *r=NULL;memcpy(&r,test_pnat_player+PRES_PARENT,8);return r;}
static int test_pres_in_scope(void) {
    void **vector=NULL,*ship=NULL,*view=NULL;
    memcpy(&vector,test_pnat_player+PRES_VECTOR,8);ship=vector?vector[2]:NULL;
    memcpy(&view,test_pnat_player+PRES_VIEW_CACHE+PRES_VIEW_STRIDE,8);
    return test_pslot_singleton()==test_pnat_player && test_pres_room_now()==(void*)test_pslot_room && ship==(void*)test_pslot_ship2 && view==(void*)test_pslot_room &&
        *(int*)(test_pslot_room+PRES_ATTENTION)==PRES_ROOM_ATTENTION && *(int*)(test_pslot_room+PRES_VIEW_ATTENTION+PRES_VIEW_STRIDE)==PRES_ROOM_ATTENTION;
}
static void test_pres_ui(void *self) {
    ++test_pres_ui_calls;
    if(test_pres_off) {assert(self==test_pres_self && !test_pres_in_scope() && !pres_open);return;}
    assert(self==test_pres_self && test_pres_in_scope() && pres_open);
    assert(*PNAT_U8(0x2f4b750)==test_pres_bank_slot && *PNAT_U8(PNAT_CONTEXTS)==0x31);   /* slot 2's SIM copy, this client's LOCAL */
    assert(pres_pause() && !test_pres_in_scope() && *PNAT_U8(0x2f4b750)==test_pres_bank_canonical && !pres_open);  /* the probe's frame */
    pres_resume();assert(test_pres_in_scope() && pres_open && *PNAT_U8(0x2f4b750)==test_pres_bank_slot);
    if(test_pres_write) {*PNAT_U8(0x2f4b750)=0x77;test_pnat_player[0x6e20]=1;}
    test_pres_target_seen=*(uint64_t*)(test_pnat_player+PRES_TARGET);
    if(test_pres_pick_target) *(uint64_t*)(test_pnat_player+PRES_TARGET)=0x66;
    if(test_pres_fault) {
        EnterCriticalSection((CRITICAL_SECTION*)(test_pres_self+PRES_UI_LOCK));   /* as X4's entry holds it */
        volatile int *nothing=NULL;*nothing=1;
    }
}
static void test_pres_refill(void *camera) {
    assert(camera==(void*)test_pview_camera || camera==(void*)test_pres_other_camera);
    assert(test_pres_in_scope()==(camera==(void*)test_pview_camera));
    ++test_pres_refills;
}
static void test_pres_render(void *self) {
    assert(self==test_pres_self && test_pres_in_scope());++test_pres_renders;
    if(test_pres_fault) {volatile int *nothing=NULL;*nothing=1;}
}
/* X4's 0x8e8450 for the player it finds: its room becomes the anchor's parent. */
static unsigned char test_pres_universe_anchor[0x100],*test_pres_room1;
static unsigned test_pres_anchor_calls;
static void test_pres_ui_anchors(void *universe) {
    (void)universe;++test_pres_anchor_calls;++test_pslot_rng;
    unsigned char *room=test_pres_room_now();
    memcpy(test_pres_universe_anchor+PRES_PARENT,&room,8);
}
static void test_pres_local_action(int id) {assert(id==0xa9 && test_pres_in_scope());++test_pres_local_calls;}
/* What X4 answers about the player in the scope: its own walks over the overlaid identity. */
static uint64_t test_pres_answer_ship;
static unsigned test_pres_picks;
static void test_pres_pick(void *player) {assert(player==test_pnat_player && test_pres_in_scope());++test_pres_picks;}
static uint64_t test_pres_occupied(void) {return test_pres_answer_ship?test_pres_answer_ship:*(uint64_t*)(test_pslot_ship2+8);}
static uint64_t test_pres_controlled(void) {return *(uint64_t*)(test_pslot_ship2+8);}
static uint64_t test_pres_zone(void) {return 0x5ca00;}
static void **test_pres_ancestor(void *component,void **out,int cls,char from_self) {
    assert(component==test_pnat_player && cls==PSLOT_ROOM_CLASS && !from_self);
    memcpy(out,test_pnat_player+PRES_VIEW_CACHE+PRES_VIEW_STRIDE,8);
    if(*out) ++*(int*)((unsigned char*)*out+0x40);
    return out;
}
/* Three stock slots on client 3: two players from the factory, two seats, two scopes in one frame. */
static __attribute__((aligned(16))) unsigned char test_pslot3_p[2][0x7000],test_pslot3_ship[2][0x500],test_pslot3_room[2][0x400],test_pslot3_c[2][0x400];
static void *test_pslot3_ship_vt[1100],*test_pslot3_room_vt[600];
static unsigned test_pslot3_made;
typedef struct {void *controller,*singleton;unsigned char bank;} test_pslot3_row;
static test_pslot3_row test_pslot3_rows[8];
static unsigned test_pslot3_row_count;
static void **test_pslot3_factory(void *universe,void **out,void *parent,void *conn,void *a5,void *a6,void *macro,void *a8,uint64_t activate,
    void *a10,uint64_t a11,const void *s12,const void *s13,const void *s14,void *a15,uint64_t mode,uint64_t post) {
    (void)universe;(void)parent;(void)conn;(void)a5;(void)a6;(void)macro;(void)a8;(void)activate;(void)a10;(void)a11;(void)s12;(void)s13;(void)s14;(void)a15;(void)mode;(void)post;
    assert(test_pslot3_made<2);
    unsigned char *p1=test_pslot_singleton(),*made=test_pslot3_p[test_pslot3_made++];uintptr_t vt=(uintptr_t)(pnat_base+VPS_PLAYER_VTABLE);void *m=test_pslot_macro;
    memset(made,0,0x7000);memcpy(made,&vt,8);memcpy(made+0x60,&m,8);*(int*)(made+0x40)=2;
    memcpy(test_pnat_universe+VPS_SINGLETON,&made,8);--*(int*)(p1+0x40);test_pslot_rng+=2;
    *out=made;return out;
}
static int test_pslot3_of(const unsigned char *player) {
    for(int i=0;i<2;i++) if(player==test_pslot3_p[i]) return i;
    return -1;
}
static void *test_pslot3_controller_of(void *key) {
    assert(key==pnat_base+PNAT_CONTROLLER_KEY);
    int i=test_pslot3_of(test_pslot_singleton());void *c=NULL;
    if(i<0) return test_pnat_controller;
    memcpy(&c,test_pslot3_ship[i]+0x200,8);return c;
}
static void test_pslot3_take(void *ship) {
    int i=test_pslot3_of(test_pslot_singleton());
    assert(i>=0 && ship==test_pslot3_ship[i] && pstk_scope_slot==(unsigned)i+2);
    void *vt=pnat_base+PSLOT_VTABLE,*s=ship,*c=test_pslot3_c[i];memset(c,0,0x400);
    memcpy(test_pslot3_c[i],&vt,8);memcpy(test_pslot3_c[i]+0x28,&s,8);memcpy(test_pslot3_ship[i]+0x200,&c,8);
    *PNAT_U8(0x2f4b750)=(unsigned char)(0x60+i);*PNAT_U8(PNAT_FLIGHT)=0;
}
static void test_pslot3_release(void *ship) {
    int i=test_pslot3_of(test_pslot_singleton());
    assert(i>=0 && ship==test_pslot3_ship[i] && *(void**)test_pslot3_c[i]==(void*)(pnat_base+PSLOT_VTABLE));
    memset(test_pslot3_ship[i]+0x200,0,8);
}
static void test_pslot3_update(void *controller,double now) {
    assert(now==100.0 && test_pslot3_row_count<8);
    test_pslot3_rows[test_pslot3_row_count++]=(test_pslot3_row){controller,test_pslot_singleton(),*PNAT_U8(0x2f4b750)};
}
static unsigned char test_pslot3_assist(void *self,unsigned char argument) {(void)self;(void)argument;return 1;}
static unsigned char test_pslot3_mode(void *self) {(void)self;return 0;}
static unsigned char test_pslot3_is_room(void *self,int cls) {(void)self;return cls==PSLOT_ROOM_CLASS;}
/* A slot ship's death: the registry's answer per runtime ID. */
static uint64_t test_pdeath_ids[3];
static unsigned char *test_pdeath_objects[3];
static void *test_pdeath_find(void *map,uint64_t runtime,unsigned flags) {
    assert(map==test_pdeath_ids && flags==4);
    for(unsigned i=0;i<3;i++) if(test_pdeath_ids[i]==runtime) return test_pdeath_objects[i];
    return NULL;
}
/* Kill through the component vtable: the suit a seat left. */
static unsigned test_pdeath_kills;
static void test_pdeath_kill(void *component,unsigned reason,void *killer,double now) {
    assert(component==test_pslot_ship2 && reason==13 && !killer && now==100.0);
    ++test_pdeath_kills;
}
/* Both slots' records of tick t, one state event, as the wire staged them at t-3. */
static void test_pdeath_stage(uint64_t t) {
    pstk_record r;unsigned char b[PSTK_BYTES_MAX];uint64_t saved=stats.issued;
    memset(&r,0,sizeof(r));r.capture16=(uint16_t)(t-3);r.count=1;r.events[0]=(pstk_event){PSTK_STATE,0xd,1};
    unsigned n=pstk_encode(&r,b);
    stats.issued=t-3;memset(pstk_locals,0,sizeof(pstk_locals));
    assert(pstk_stage(1,b,n,t-3) && pstk_stage(2,b,n,t-3));
    stats.issued=saved;
}
/* Crash note: a real access violation's information, the line, the file, X4's filter after it. */
static EXCEPTION_RECORD test_crash_record;
static CONTEXT test_crash_context;
static unsigned test_crash_previous_calls;
static LONG WINAPI test_crash_previous(EXCEPTION_POINTERS *info) {(void)info;++test_crash_previous_calls;return EXCEPTION_EXECUTE_HANDLER;}
static __attribute__((noinline)) void test_crash_fault(volatile int *where) {*where=7;}
static int test_crash_capture(EXCEPTION_POINTERS *info) {
    test_crash_record=*info->ExceptionRecord;test_crash_context=*info->ContextRecord;return EXCEPTION_EXECUTE_HANDLER;
}
static const char *test_crash_strings[3];
static double test_crash_pushed;
static const char *test_crash_tolstring(lua_State *L,int index,size_t *length) {
    (void)L;assert(index>=1 && index<=2);if(length && test_crash_strings[index]) *length=strlen(test_crash_strings[index]);return test_crash_strings[index];
}
static void test_crash_pushnumber(lua_State *L,double value) {(void)L;test_crash_pushed=value;}
static void test_crash_note(void) {
    char dir[MAX_PATH],note[MAX_PATH],text[4096];DWORD n=GetTempPathA(MAX_PATH,dir);assert(n && n<MAX_PATH-40);
    snprintf(note,sizeof(note),"%sx4lan-crash-test-%lu.txt",dir,(unsigned long)GetCurrentProcessId());DeleteFileA(note);
    __try {test_crash_fault((volatile int*)(uintptr_t)0x10);} __except(test_crash_capture(GetExceptionInformation())) {}
    EXCEPTION_POINTERS info={&test_crash_record,&test_crash_context};
    uint64_t saved_issued=stats.issued;stats.issued=1599;
    assert(crash_note_text(&info,text,sizeof(text)) && strstr(text,"[X4LAN] NATIVE_GATE CRASH tick=1599 code=c0000005 at=") &&
           strstr(text," access=1 data=10 thread=") && strstr(text," chain=") && strstr(text,"\r\n"));
    /* Armed: the filter writes the line once and hands on to the filter that was there. */
    LPTOP_LEVEL_EXCEPTION_FILTER kept=SetUnhandledExceptionFilter(test_crash_previous);
    crash_note_armed=0;crash_note_entered=0;crash_note_previous=NULL;
    assert(!crash_note_arm("",NULL) && crash_note_arm(note,NULL) && crash_note_armed && crash_note_previous==test_crash_previous);
    assert(crash_note_arm(note,NULL) && crash_note_previous==test_crash_previous);   /* a UI reload keeps X4's */
    assert(crash_note_filter(&info)==EXCEPTION_EXECUTE_HANDLER && test_crash_previous_calls==1);
    assert(crash_note_filter(&info)==EXCEPTION_EXECUTE_HANDLER && test_crash_previous_calls==2);   /* once only */
    {   FILE *f=fopen(note,"rb");char back[4096];size_t got=f?fread(back,1,sizeof(back)-1,f):0;if(f) fclose(f);back[got]=0;
        assert(got && strstr(back,"NATIVE_GATE CRASH tick=1599 code=c0000005") && !strstr(strstr(back,"\n")+1,"CRASH"));}
    SetUnhandledExceptionFilter(kept);crash_note_armed=0;crash_note_entered=0;crash_note_previous=NULL;crash_note_path[0][0]=0;
    /* Through Lua, where the probe arms it: right after loading the gate, before gate.start names the UI thread
     * (build120 refused here: accept_ui). */
    {   DWORD saved_ui=ui_thread;ui_thread=0;
        const char *(*saved_tolstring)(lua_State*,int,size_t*)=p_lua_tolstring;void (*saved_push)(lua_State*,double)=p_lua_pushnumber;
        p_lua_tolstring=test_crash_tolstring;p_lua_pushnumber=test_crash_pushnumber;
        kept=SetUnhandledExceptionFilter(test_crash_previous);
        test_crash_strings[1]=note;test_crash_strings[2]=NULL;test_crash_pushed=-1;
        assert(lua_crash_note(NULL)==1 && test_crash_pushed==1 && crash_note_armed && crash_note_previous==test_crash_previous);
        test_crash_strings[1]=NULL;assert(lua_crash_note(NULL)==1 && test_crash_pushed==0);
        SetUnhandledExceptionFilter(kept);crash_note_armed=0;crash_note_entered=0;crash_note_previous=NULL;crash_note_path[0][0]=0;
        p_lua_tolstring=saved_tolstring;p_lua_pushnumber=saved_push;ui_thread=saved_ui;}
    stats.issued=saved_issued;DeleteFileA(note);
}
static void test_pslot_reset(void) {
    memset(pslots,0,sizeof(pslots));pslot_armed=0;pslot_failed=0;pslot_mask=0;pslot_stage="off";pslot_why="-";pslot_p1=NULL;
    pslot_update_tick=0;pslot_hook_installed=0;pslot_in_scope=0;pstk_scope_slot=0;memset(pslot_getter_orig,0,sizeof(pslot_getter_orig));
    memset((void*)pslot_getter_out,0,sizeof(pslot_getter_out));memset((void*)pslot_getter_in,0,sizeof(pslot_getter_in));
    memset((void*)pslot_route_hits,0,sizeof(pslot_route_hits));pslot_updates=pslot_hook_calls=0;
    memset((void*)pslot_getter_render,0,sizeof(pslot_getter_render));pslot_callers=0;pslot_caller_tail=(unsigned)pslot_caller_head;pslot_caller_dropped=0;
    test_pslot_assist_value=1;test_pslot_mode_value=0;render_rng_in_slot=0;
}
static void test_pilot_slots(void) {
    pstk_focus_forced=1;
    unsigned char *saved_image_base=pnat_base;pnat_fns saved_fns=pnat_fn;pstk_fns saved_pstk=pstk_fn;pslot_fns saved_slots=pslot_fn;
    uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;DWORD saved_ui_thread=ui_thread;
    const void *saved_ptrs[PSTK_HANDLERS];memcpy(saved_ptrs,pstk_handler_ptrs,sizeof(saved_ptrs));
    vps_create_fn saved_create=vps_create;uint64_t *(*saved_rng)(void)=vps_rng_word;
    unsigned char *region=VirtualAlloc(NULL,TEST_PNAT_REGION,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region);pnat_base=region;ui_thread=GetCurrentThreadId();test_pslot_reset();
    /* The stock environment of test_pilot_stock: input object, multimaps, patches, aim slot, view sites. */
    pnat_fn=(pnat_fns){test_pnat_refresh,test_pslot_controller_of,test_pnat_option,test_pnat_steer_mode,test_pnat_steer_direct,
        test_pnat_steer_reset,test_pnat_throttle,test_pnat_match_reset,test_pnat_state,test_pnat_range,test_pnat_space_original,
        test_pstk_cursor_original,test_pnat_time};
    pstk_fn=(pstk_fns){test_pstk_handoff,test_pstk_release,test_pstk_started,test_pstk_stopped,test_pstk_curve,test_pstk_authority,
        test_pstk_aim_original,test_pstk_transform,test_pstk_ship,test_pstk_ray,test_pstk_zone_of};
    {void *aim=(void*)test_pstk_aim_original;memcpy(region+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT,&aim,8);}
    pview_fns saved_view=pview_fn;pview_fn=(pview_fns){test_pview_fov,test_pview_floating,test_pview_portable};
    for(unsigned i=0;i<PVIEW_SITES;i++) memcpy(region+pview_sites[i].rva,pview_sites[i].original,pview_sites[i].length);
    pstk_handler_ptrs[0]=(void*)test_pstk_action;pstk_handler_ptrs[1]=(void*)test_pstk_state;pstk_handler_ptrs[2]=(void*)test_pstk_range;
    pstk_handler_ptrs[3]=(void*)test_pstk_action;pstk_handler_ptrs[4]=(void*)test_pstk_state;
    unsigned char *input=test_pnat_input,*controller=test_pnat_controller,*map=input+0x63e0,*p1=test_pnat_player,*p2=test_pslot_p2;
    memset(test_pnat_input,0,sizeof(test_pnat_input));memset(test_pnat_universe,0,sizeof(test_pnat_universe));
    void *p=input;memcpy(region+PNAT_INPUT,&p,8);p=test_pnat_universe;memcpy(region+PNAT_UNIVERSE,&p,8);memcpy(region+PNAT_OPTIONS,&p,8);
    memcpy(test_pnat_universe+VPS_SINGLETON,&p1,8);
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
    test_pnat_insert(map,0x12f,(void*)test_pstk_action);test_pnat_insert(map+0x40,0xd,(void*)test_pstk_state);
    {void *camera=test_pview_camera;memset(test_pview_camera,0,sizeof(test_pview_camera));memcpy(test_pnat_universe+0x3d0,&camera,8);}
    test_pview_floating_value=0;memset(p1+0x6c60,0,0x40);*(uint64_t*)(p1+0x6dd0)=0;
    /* Slot fixtures: the canonical player (seated pilot, assist on), the factory, slot 2's ship with its
     * Agent's controller and cockpit room, the PlayerController vtable, the pilot post, the call site. */
    {   uintptr_t vt=(uintptr_t)(region+VPS_PLAYER_VTABLE);void *m=test_pslot_macro,*r=test_pslot_render,*post=test_pslot_post;
        memset(test_pslot_macro,0,sizeof(test_pslot_macro));*(int*)(test_pslot_macro+0x44)=VPS_PLAYER_CLASS;
        memcpy(p1,&vt,8);memcpy(p1+0x60,&m,8);*(int*)(p1+0x40)=3;memset(p1+0x6aac,0,12);p1[0x6aac]=1;*(int*)(p1+0x6ab8)=0x77;
        memset(region+VPS_EMPTY_STRING,0,32);memcpy(region+VPS_RENDER_FLAG,&r,8);region[VPS_CONTEXT_FLAG]=1;region[VPL_REENTRY]=0;
        *(int64_t*)(test_pslot_post+0x1b0)=2;memcpy(region+PSLOT_POST,&post,8);*(int*)(region+PSLOT_PILOT_POSITION)=0x77;
        memcpy(region+PSLOT_CALL_SITE,pslot_call_bytes,5);
        test_pslot_rng=0x99;vps_create=test_pslot_factory;vps_rng_word=test_pslot_rng_word;}
    {   void *svt=test_pslot_ship_vt,*rvt=test_pslot_room_vt,*avt=test_pslot_agent_vt,*room=test_pslot_room,*agent=test_pslot_agent2;
        memset(test_pslot_ship2,0,sizeof(test_pslot_ship2));memcpy(test_pslot_ship2,&svt,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);
        memcpy(test_pslot_ship2+0x200,&agent,8);test_pslot_ship2[0xd1]=1;
        test_pslot_ship_vt[PSLOT_TAKE]=(void*)test_pslot_take;test_pslot_ship_vt[PSLOT_RELEASE]=(void*)test_pslot_release;
        memcpy(test_pslot_room,&rvt,8);test_pslot_room_vt[PSLOT_IS_CLASS]=(void*)test_pslot_room_is;
        memcpy(test_pslot_agent2,&avt,8);test_pslot_agent_vt[3]=(void*)test_pslot_agent_is;test_pslot_agent_vt[20]=(void*)test_pslot_agent_next;
        void **cvt=(void**)(region+PSLOT_VTABLE);cvt[-1]=(void*)0xc01;
        for(unsigned i=0;i<PSLOT_VT_SLOTS;i++) cvt[i]=(void*)test_pslot_c_other;
        cvt[3]=(void*)test_pslot_c_is;cvt[20]=(void*)test_pslot_c_next;cvt[49]=(void*)test_pslot_c_assist;cvt[50]=(void*)test_pslot_c_mode;
        cvt[13]=(void*)test_pslot_c_getter;}
    pslot_fn=(pslot_fns){test_pslot_update,test_pslot_upkeep,test_pslot_environment,test_pslot_owner,test_pslot_change,test_pslot_take,
        test_pslot_release,{(void*)test_pslot_orig,(void*)test_pslot_orig,(void*)test_pslot_orig},
        {(void*)test_pslot_agent,(void*)test_pslot_agent,(void*)test_pslot_agent},{(void*)test_pslot_player,(void*)test_pslot_player,(void*)test_pslot_player}};
    /* Arming on client 2 (slot 2 captures its own input): slots 1 and 2. */
    pilot_armed=1;pilot_suppress=1;pilot_source=PILOT_SOURCE_WIRE;pilot_slot_count=2;pilot_delay=3;pilot_device_armed=0;
    pilot_start=10;pilot_stop=40;pilot_controls_owned=0;pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pnat_installed=0;pnat_restored=1;
    memset(pilot_slots,0,sizeof(pilot_slots));
    pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;
    pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].player=0;
    pnat_armed=0;pnat_mode=0;stats.slot=2;
    /* Client 2 captures exactly when slot 2 is a stock slot; every refusal names itself. */
    assert(!pnat_arm_stock_using(0,3) && !strcmp(pstk_arm_why,"stock_slot_not_capturing"));
    assert(!pnat_arm_stock_using(1,1) && !strcmp(pstk_arm_why,"capture_not_a_stock_slot"));
    vps_create=NULL;assert(!pnat_arm_stock_using(1,3) && !strcmp(pstk_arm_why,"slots_factory"));vps_create=test_pslot_factory;
    pslot_fn.update=NULL;assert(!pnat_arm_stock_using(1,3) && !strcmp(pstk_arm_why,"slots_fns"));pslot_fn.update=test_pslot_update;
    pilot_start=5;assert(!pnat_arm_stock_using(1,3) && !strcmp(pstk_arm_why,"slots_window"));pilot_start=10; /* the player needs take-2 >= 1 */
    assert(pnat_arm_stock_using(1,3) && pslot_armed && pslot_mask==2 && pstk_slot_mask==3 && pstk_capture_slot==2 && pilot_stock_slots==3 &&
           pstk_capturing && !strcmp(pslot_stage,"armed"));
    assert(pnat_arm_stock_using(1,3) && !pnat_arm_stock_using(1,1));
    /* Presentation on client 2: needs the slots, the render split and X4's functions; every refusal named. */
    pres_fns saved_pres=pres_fn;pres_armed=0;pres_active=0;pres_ui_installed=0;pres_refill_installed=0;pres_failed=0;pres_open=0;pres_paused=0;
    pres_scopes=pres_ui=pres_render=pres_local=pres_refills=pres_refused=pres_refused_player=pres_faults=pres_skipped=0;
    pres_camera=NULL;pres_rng_moved=0;pres_restored=1;pres_anchor_moved=0;pres_fault_detail[0]=0;
    render_rng_installed=0;pres_fn=(pres_fns){test_pres_ui,test_pres_refill,test_pres_ui_anchors,test_pres_occupied,test_pres_controlled,test_pres_zone,test_pres_ancestor,test_pres_pick};
    assert(!pres_arm_using(1) && !strcmp(pstk_arm_why,"present_without_render_split"));
    render_rng_installed=1;pres_fn.zone=NULL;assert(!pres_arm_using(1) && !strcmp(pstk_arm_why,"present_functions"));pres_fn.zone=test_pres_zone;
    assert(pres_arm_using(1) && pres_slot==2 && !strcmp(pres_stage,"armed") && !pres_active && pres_hud==1);
    assert(!pres_arm_using(0) && pres_arm_using(1));   /* armed: the same mode only */
    {   unsigned char *anchor=test_pres_universe_anchor;memcpy(&test_pres_room1,p1+PRES_PARENT,8);test_pres_anchor_calls=0;pres_anchor_moves=0;
        memcpy(test_pres_universe_anchor+PRES_PARENT,&test_pres_room1,8);
        unsigned char *universe=NULL;assert(read_local(region+VPL_UNIVERSE,&universe,8) && universe);
        memcpy(universe+PRES_UI_ANCHOR,&anchor,8);}
    {   void *entry=(void*)test_pres_ui;
        memcpy(region+PRES_UI_ENTRY,&entry,8);memcpy(region+PRES_REFILL_CALL,pres_refill_bytes,5);memcpy(region+PRES_PICK_CALL,pres_pick_bytes,5);
        *(int*)(test_pslot_room+PRES_ATTENTION)=9;*(int*)(test_pslot_room+PRES_VIEW_ATTENTION+PRES_VIEW_STRIDE)=8;test_pres_picks=0;
        /* The canonical player's fixture stays as it is, its parent is what comes back. This thread reads bank 1. */
        *(int*)(region+PRES_VIEW_INDEX)=1;*(uint64_t*)(test_pslot_ship2+8)=0x176ad5;*(int*)(test_pslot_room+0x40)=5;
        test_pres_answer_ship=0;
        test_pres_ui_calls=test_pres_refills=test_pres_renders=test_pres_local_calls=0;
        test_pres_fault=test_pres_write=0;memset(test_pres_self,0,sizeof(test_pres_self));
        InitializeCriticalSection((CRITICAL_SECTION*)(test_pres_self+PRES_UI_LOCK));}
    test_pnat_insert(map,0xa9,(void*)test_pres_local_action);
    /* Slot 2's pilot record is the stock section: no pilot capture on client 2, no pilot staging of slot 2. */
    {   unsigned char rec[PILOT_RECORD_BYTES];const char *why="";assert(!pilot_capture(1,rec,&why) && !strcmp(why,"stock_slot"));}
    uint32_t *counter=(uint32_t*)(region+PNAT_COUNTER);unsigned char *contexts=region+PNAT_CONTEXTS,*flight=region+PNAT_FLIGHT;
    *counter=0;*contexts=1;*flight=0;
    /* take-2 (5): slot 2's player from the factory, the singleton back to slot 1's. */
    stats.issued=5;assert(pnat_pass(5,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    assert(!strcmp(pslot_stage,"constructed") && pslots[2].player==p2 && pslot_p1==p1 && test_pslot_singleton()==p1 &&
           *(int*)(p1+0x40)==3 && *(int*)(p2+0x40)==1 && test_pslot_rng==0x99);
    /* take-1 (6): seated in slot 2's scope; the canonical block untouched, slot 2's store holds the bank reset. */
    region[0x2f4b750]=0x11;region[0x2f4b7e0]=0;*counter=2;uint64_t canonical=pslot_hash();test_pslot_call_count=0;
    stats.issued=6;assert(pnat_pass(6,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    pslot_slot *s2=&pslots[2];
    assert(!strcmp(pslot_stage,"seated") && s2->seated && s2->controller==test_pslot_c2 && s2->ship==test_pslot_ship2 && s2->room==test_pslot_room);
    assert(test_pslot_calls[0].what=='T' && test_pslot_calls[0].singleton==p2 && test_pslot_local_at_take[0]==1 && test_pslot_local_at_take[1]==1);
    /* P_k's seat holds from here to the release: upward pointers only. */
    assert(test_pslot_singleton()==p1 && *(void**)(p2+0x70)==(void*)test_pslot_room && *(void**)(p2+0x308)==(void*)&s2->vector[0] &&
           *(void**)(p2+0x310)==(void*)&s2->vector[3] && s2->vector[2]==(void*)test_pslot_ship2);
    assert(region[0x2f4b750]==0x11 && region[0x2f4b7e0]==0 && *counter==2 && pslot_hash()==canonical && !pstk_scope_slot && !pslot_in_scope);
    assert(s2->store[test_pslot_store_offset(0x2f4b750)]==0x5a && s2->store[test_pslot_store_offset(0x2f4b7e0)]==1 &&
           s2->store[test_pslot_store_offset(PNAT_CONTEXTS)]==1 && !s2->store[test_pslot_store_offset(0x2f4b720)]);
    assert(*(int*)(p2+0x6ab8)==0x77 && p2[0x6aac]==1 && s2->post==2);
    *counter=0;canonical=pslot_hash();
    /* C_k's own vtable: thunks, routes, scoped entries; the stock column object kept. */
    void **vt=*(void***)test_pslot_c2;
    assert(vt==&s2->vt.slots[0] && vt[-1]==(void*)0xc01 && vt[33]==(void*)pslot_vf33 && vt[34]==(void*)pslot_vf34 && vt[35]==(void*)pslot_vf35 &&
           vt[36]==(void*)pslot_vf36 && vt[6]==(void*)pslot_route6 && vt[7]==(void*)pslot_route7 && vt[10]==(void*)pslot_route10 &&
           vt[13]==pslot_thunks[13] && pslot_getter_orig[13]==(void*)test_pslot_c_getter);
    /* Readers that identify a controller by its vtable see a PlayerController. */
    assert(pslot_canonical_vtable((uintptr_t)vt)==(uintptr_t)(region+PSLOT_VTABLE) && pslot_canonical_vtable(0x1234)==0x1234);
    assert(pslot_vtable_is_copy((uintptr_t)vt) && !pslot_vtable_is_copy((uintptr_t)(region+PSLOT_VTABLE)));
    /* Its predicate at the seat: no flight control, assist, no highway -> the Agent moves the ship. */
    assert(s2->authority==1 && s2->vt.route[0]==(void*)test_pslot_agent);
    /* The call site: re-pointed to the near page, which jumps to pslot_update. */
    int32_t rel;memcpy(&rel,region+PSLOT_CALL_SITE+1,4);
    unsigned char *page=region+PSLOT_CALL_SITE+5+rel;void *jump=NULL;memcpy(&jump,page+6,8);
    assert(region[PSLOT_CALL_SITE]==0xe8 && page==pslot_page && page[0]==0xff && page[1]==0x25 && jump==(void*)pslot_update && pslot_hook_intact());
    /* Frame 6 through the page: the canonical update, then slot 2's in its scope. */
    void (*hooked)(void*,double)=(void(*)(void*,double))page;
    test_pslot_call_count=0;hooked(controller,100.0);
    assert(test_pslot_call_count==2 && test_pslot_calls[0].what=='U' && test_pslot_calls[0].object==controller && test_pslot_calls[0].singleton==p1 &&
           test_pslot_calls[1].what=='U' && test_pslot_calls[1].object==test_pslot_c2 && test_pslot_calls[1].singleton==p2 &&
           pslot_update_tick==6 && s2->updates==1 && test_pslot_singleton()==p1 && pslot_hash()==canonical);
    /* The tree phase: upkeep and environment entries of C_2 run in its scope; 6/7/10 outside answer from
     * the snapshot (the Agent), inside from the original; other entries counted, then stock. */
    test_pslot_call_count=0;test_pslot_inner_route=1;
    ((void(*)(void*,double))vt[33])(test_pslot_c2,100.0);test_pslot_inner_route=0;
    ((void(*)(void*,void*,void*))vt[34])(test_pslot_c2,(void*)1,(void*)2);
    ((void(*)(void*,void*,void*,void*))vt[35])(test_pslot_c2,(void*)1,(void*)2,(void*)3);
    ((void(*)(void*,void*,void*,char))vt[36])(test_pslot_c2,(void*)1,(void*)2,4);
    assert(test_pslot_calls[0].what=='K' && test_pslot_calls[0].singleton==p2 && test_pslot_calls[1].what=='o' && test_pslot_calls[1].singleton==p2);
    assert(test_pslot_calls[2].what=='E' && test_pslot_calls[2].singleton==p2 && test_pslot_calls[3].what=='O' && test_pslot_calls[4].what=='G' &&
           test_pslot_calls[4].singleton==p2 && s2->upkeep==1 && s2->environment==2 && s2->owner==1 && test_pslot_singleton()==p1);
    test_pslot_call_count=0;
    test_pslot_route(test_pslot_c2,6);test_pslot_route(test_pslot_c2,7);test_pslot_route(test_pslot_c2,10);
    assert(test_pslot_call_count==3 && test_pslot_count('a')==3 && pslot_route_hits[0]==2 && pslot_route_hits[1]==1 && pslot_route_hits[2]==1);
    assert(((int(*)(void*))vt[13])(test_pslot_c2)==13 && pslot_getter_out[13]==1 && pslot_getter_in[49]>=1 && !pslot_getter_out[49]);
    /* Rendering is counted apart from simulating; only simulating callers enter the table. */
    render_rng_in_slot=1;assert(((int(*)(void*))vt[13])(test_pslot_c2)==13 && pslot_getter_render[13]==1 && pslot_getter_out[13]==1);render_rng_in_slot=0;
    pslot_callers_drain();
    {   char callers[256];assert(pslot_callers==1 && pslot_caller_table[0].slot==13 && pslot_caller_table[0].rva==0xffffffffu && pslot_caller_table[0].count==1);
        assert(pslot_callers_text(callers,sizeof(callers)) && !strcmp(callers,"callers=1 dropped=0 13:ffffffff:1"));}
    /* Slots 49, 50, 14, 23 outside the scope: slot 2's answers at its last scope exit, not the live globals or a later change. */
    region[0x2f4b828]=1;region[0x2f4b7d8]=1;region[PNAT_HIGHWAY]=1;
    {float slot1_throttle=0.75f;memcpy(region+0x2f4b764,&slot1_throttle,4);}
    test_pslot_assist_value=0;test_pslot_mode_value=1;
    {   unsigned char a0=((unsigned char(*)(void*,unsigned char))vt[49])(test_pslot_c2,0),a1=((unsigned char(*)(void*,unsigned char))vt[49])(test_pslot_c2,1);
        assert(a0==1 && a1==0 && !((unsigned char(*)(void*))vt[50])(test_pslot_c2) && ((unsigned char(*)(void*))vt[23])(test_pslot_c2)==1 &&
               ((float(*)(void*))vt[14])(test_pslot_c2)==0.0f && pslot_getter_out[49]==2 && pslot_getter_out[50]==1 && pslot_getter_out[23]==1 && pslot_getter_out[14]==1);}
    region[0x2f4b828]=0;region[0x2f4b7d8]=0;region[PNAT_HIGHWAY]=0;memset(region+0x2f4b764,0,4);
    /* The next scope exit takes the new answers. */
    test_pslot_call_count=0;((void(*)(void*,double))vt[33])(test_pslot_c2,100.0);
    assert(s2->assist[0]==0 && s2->assist[1]==1 && s2->agent_mode==1 && !s2->authority);
    test_pslot_assist_value=1;test_pslot_mode_value=0;((void(*)(void*,double))vt[33])(test_pslot_c2,100.0);
    assert(s2->assist[0]==1 && s2->authority==1);
    /* d8dd50 for C_2 outside its scope: the snapshot; the canonical controller untouched. */
    {unsigned char a=7;assert(pslot_authority(test_pslot_c2,&a) && a==1 && !pslot_authority(controller,&a) && !pslot_authority(test_pnat_ship,&a));}
    /* A call from another thread never opens a scope: refused, the session stops with 161. */
    {   HANDLE t=CreateThread(NULL,0,test_pslot_foreign_thread,test_pslot_c2,0,NULL);assert(t);WaitForSingleObject(t,INFINITE);CloseHandle(t);
        assert(pslot_failed && pnat_failed && pilot_stop_reason()==PSLOT_STOP_REASON && s2->refused==1 && !strcmp(pslot_why,"upkeep_scope"));
        pslot_failed=0;pnat_failed=0;pnat_fail_stage=NULL;pslot_why="-";pilot_failures=0;}
    /* take (7) and take+1 (8): the last frame's update seen; stock pieces installed, d8dd50 patched too. */
    stats.issued=7;assert(pnat_pass(7,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pilot_controls_owned &&
                          !strcmp(pslot_stage,"live"));
    hooked(controller,100.0);
    stats.issued=8;assert(pnat_pass(8,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && pnat_installed &&
                          pstk_capture_start==8 && pstk_first_apply==11);
    assert(pnat_patches[0].installed && region[PNAT_AUTHORITY_RVA]==0x48 && region[PNAT_AUTHORITY_RVA+1]==0xb8);
    {unsigned char a=pnat_authority(test_pslot_c2);assert(a==1);}
    /* Client 2 records its own input as slot 2: SPACE, accelerate; travel (MD activity) refused for slot 2. */
    stats.issued=8;pnat_refused=0;test_pstk_call_count=0;
    pnat_space(8,1);pnat_dispatch_state(map,0xd,1);pnat_dispatch_action(map,0x12f);
    assert(pstk_build.count==2 && pnat_refused==1 && !test_pstk_call_count);
    /* With pilot fire X4's fire input is not refused, it rides the record's flag (with the focus). */
    pfire_armed=1;pnat_refused=0;pnat_dispatch_state(map,0x18,1);assert(!pnat_refused && pstk_build.count==2 && (pnat_fire_buttons()&1));pnat_refused=1;
    /* Without the focus a control that engages is not recorded, a release is; the id is still refused or local as ever. */
    {   unsigned count=pstk_build.count;LONG64 unfocused=pstk_unfocused;pstk_focus_forced=-1;
        pnat_dispatch_state(map,0xd,1);assert(pstk_build.count==count && pstk_unfocused==unfocused+1);
        pnat_dispatch_state(map,0xd,0);assert(pstk_build.count==count+1 && pstk_unfocused==unfocused+1);
        pstk_focus_forced=1;pstk_build.count=count;}
    unsigned char rec[PSTK_BYTES_MAX],one[PSTK_BYTES_MAX];unsigned length=0,one_length;const char *why="";
    memset(test_pnat_player+0x6c60,0,0x40);LONG64 samples=pstk_aim_samples,failures=pstk_aim_sample_failures;
    assert(pstk_capture(8,rec,&length,&why) && !(rec[1]&PSTK_FLAG_AIM) && (rec[1]&PSTK_FLAG_VIEW) && pstk_aim_samples==samples &&
           pstk_aim_sample_failures==failures);  /* aim is slot 1's only */
    assert(rec[1]&PSTK_FLAG_FIRE);
    pnat_dispatch_state(map,0x18,0);assert(!(pnat_fire_buttons()&1));pfire_armed=0;
    /* Staging: slot 2's record from client 2 (echo checked), slot 1's from client 1; a slot-2 record
     * with travel and a slot outside the stock set are refused. */
    pstk_record r1;memset(&r1,0,sizeof(r1));r1.capture16=8;r1.count=1;r1.events[0]=(pstk_event){PSTK_STATE,0xd,1};
    one_length=pstk_encode(&r1,one);
    assert(pstk_stage(2,rec,length,8) && pstk_stage(1,one,one_length,8) && !pstk_stage(3,one,one_length,8));
    {   pstk_record bad=r1;bad.capture16=9;bad.events[0]=(pstk_event){PSTK_ACTION,0x12f,0};unsigned char b[PSTK_BYTES_MAX];unsigned n=pstk_encode(&bad,b);
        assert(!pstk_stage(2,b,n,8) && pstk_stage(1,b,n,9));pstk_ring[0][12%PSTK_RING].present=0;}
    {unsigned char changed[PSTK_BYTES_MAX];memcpy(changed,rec,length);changed[PSTK_HEAD+PSTK_EVENT+2]^=1;assert(!pstk_stage(2,changed,length,8));} /* echo mismatch */
    memcpy(&test_pres_room1,p1+PRES_PARENT,8);memcpy(test_pres_universe_anchor+PRES_PARENT,&test_pres_room1,8);
    for(uint64_t t=9;t<=10;t++) {
        stats.issued=t-1;hooked(controller,100.0);stats.issued=t;
        assert(pnat_pass(t,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    }
    /* 10, the window start: the camera on slot 2's player through X4's own call, inside the scope, the eye at the ship's
     * pilot connection; the UI entry and the refill call are the presentation's now. */
    assert(pres_active && !pres_open && !strcmp(pres_stage,"presenting") && test_pslot_singleton()==p1 && !test_pres_in_scope() &&
           pres_asked_ship==0x176ad5 && pres_asked_controlled==0x176ad5 && pres_asked_zone==0x5ca00 && !pres_restored);
    /* The room's attention is the simulation's again outside the scope. */
    assert(*(int*)(test_pslot_room+PRES_ATTENTION)==9 && *(int*)(test_pslot_room+PRES_VIEW_ATTENTION+PRES_VIEW_STRIDE)==8);
    /* The self-test's reference is given back; the player's identity is the canonical one again. */
    assert(*(int*)(test_pslot_room+0x40)==5 && test_pres_room_now()==(void*)test_pres_room1);
    /* This client's own calls leave the thread's simulation stream where it was. */
    assert(test_pslot_rng==0x99 && pres_rng_moved==1);
    /* What a player's presence set on the ship: once, then nothing. */
    {const char *d=pres_diff_take();assert(d && !strncmp(d,"slot=2",6) && !pres_diff_take());}
    /* An anchor under the player (a save's camera_anchor_scene) follows the scope: no connection needed. */
    {   unsigned char *universe=test_pnat_universe,chain[0x100],*saved=NULL,*player=p1,*link=chain;
        memset(chain,0,sizeof(chain));memcpy(&saved,test_pres_universe_anchor+PRES_PARENT,8);
        memcpy(chain+PRES_PARENT,&player,8);memcpy(test_pres_universe_anchor+PRES_PARENT,&link,8);
        assert(pres_anchor_placed(universe,&pslots[2]));
        memcpy(test_pres_universe_anchor+PRES_PARENT,&saved,8);}
    /* The HUD's place: X4's own call in the scope put the anchor in ship 2's room. */
    assert(test_pres_anchor_calls==1 && pres_anchor_moves==1 && !memcmp(test_pres_universe_anchor+PRES_PARENT,&(unsigned char*){test_pslot_room},8));
    void (*ui_entry)(void*)=*(void(**)(void*))(region+PRES_UI_ENTRY);
    assert(ui_entry==pres_ui_move && region[PRES_REFILL_CALL]==0xe8 && memcmp(region+PRES_REFILL_CALL,pres_refill_bytes,5));
    /* Slot 6: X4's UI in the scope with a copy of slot 2's block and this client's LOCAL; the probe's frame paused out of it. */
    uint64_t canonical_before=pslot_hash();unsigned char store_750=s2->store[test_pslot_store_offset(0x2f4b750)];
    *contexts=0x31;canonical_before=pslot_hash();
    region[0x2f4b750]=0x11;canonical_before=pslot_hash();test_pres_bank_canonical=0x11;test_pres_bank_slot=store_750=0x5a;
    s2->store[test_pslot_store_offset(0x2f4b750)]=0x5a;
    ui_entry(test_pres_self);
    assert(test_pres_ui_calls==1 && !pres_open && test_pslot_singleton()==p1 && pslot_hash()==canonical_before && !pres_refused && !pres_refused_player);
    /* A simulation write refuses itself: discarded, counted, named; the store, the player's field and the canonical block as before. */
    test_pres_write=1;ui_entry(test_pres_self);test_pres_write=0;
    assert(pres_refused==1 && pres_refused_player==1 && s2->store[test_pslot_store_offset(0x2f4b750)]==store_750 &&
           !p1[0x6e20] && !p2[0x6e20] && pslot_hash()==canonical_before);
    /* A target selected in the scope: not a refusal, visible in later scopes and in the record's field, on nobody's player. */
    test_pres_pick_target=1;ui_entry(test_pres_self);test_pres_pick_target=0;
    assert(pres_refused==1 && pres_refused_player==1 && pres_targets==1 && pres_local_target==0x66 && pview_slot_target==0x66 &&
           !*(uint64_t*)(p1+PRES_TARGET) && !*(uint64_t*)(p2+PRES_TARGET));
    ui_entry(test_pres_self);assert(test_pres_target_seen==0x66 && pres_targets==1 && !*(uint64_t*)(p1+PRES_TARGET));
    pres_target_set=0;pres_local_target=0;pview_slot_target=0;pres_targets=0;
    *contexts=1;
    /* Slot 10, the local ids of slot 4 and the refill of this camera run in it, another camera's refill not. */
    pres_render_call(test_pres_render,test_pres_self);assert(test_pres_renders==1 && !pres_open && pres_render==1);
    pnat_dispatch_action(map,0xa9);assert(test_pres_local_calls==1 && pres_local==1 && !pres_open);
    {   int32_t site;memcpy(&site,region+PRES_REFILL_CALL+1,4);void (*refill)(void*)=(void(*)(void*))(region+PRES_REFILL_CALL+5+site);
        assert((unsigned char*)refill==pslot_page+PRES_STUB_OFFSET);
        refill(test_pview_camera);refill(test_pres_other_camera);assert(test_pres_refills==2 && pres_refills==1 && !pres_open);
        /* The camera pick in the scope, through its own call. */
        memcpy(&site,region+PRES_PICK_CALL+1,4);void (*pick)(void*)=(void(*)(void*))(region+PRES_PICK_CALL+5+site);
        assert((unsigned char*)pick==pslot_page+PRES_PICK_STUB);
        pick(test_pnat_player);assert(test_pres_picks==1 && pres_picks==1 && !pres_open);}
    /* No scope in the middle of a slot's: the slot's update refuses a presentation scope and vice versa. */
    assert(pslot_enter(2) && !pres_enter() && pslot_leave(2));
    assert(pres_enter() && !pslot_enter(2));pres_leave();
    /* A fault in a presentation entry point: the scope closed, presentation ended, the session stops naming it (162). */
    *contexts=0x31;test_pres_fault=1;ui_entry(test_pres_self);test_pres_fault=0;
    assert(pres_faults==1 && !pres_open && !pres_active && test_pslot_singleton()==p1 && pslot_hash()==canonical_before && pres_failed && pnat_failed &&
           pilot_stop_reason()==PRES_STOP_REASON && !strncmp(pres_why,"ui_fault:",9) && !test_pres_in_scope());
    /* Everything is back at once, on this thread: UI entry, refill call, the HUD's place, X4's lock. */
    assert(pres_restored && !pres_ui_installed && !pres_refill_installed && *(void**)(region+PRES_UI_ENTRY)==(void*)test_pres_ui &&
           !memcmp(region+PRES_REFILL_CALL,pres_refill_bytes,5) && !memcmp(region+PRES_PICK_CALL,pres_pick_bytes,5) && !strcmp(pres_stage,"failed") &&
           !memcmp(test_pres_universe_anchor+PRES_PARENT,&test_pres_room1,8) && test_pslot_rng==0x99);
    assert(((CRITICAL_SECTION*)(test_pres_self+PRES_UI_LOCK))->OwningThread==NULL);
    /* The fault names itself: address, access, registers, the chain from the faulting code up. */
    assert(strstr(pres_fault_detail,"addr=0 write=1 ") && strstr(pres_fault_detail," rcx=") && strstr(pres_fault_detail," chain=@"));
    {   const char *chain=strstr(pres_fault_detail,"chain=");unsigned hops=0;for(const char *c=chain;*c;c++) hops+=*c=='>';assert(hops>=2);}
    {   char row[4096];assert(pres_status(row,sizeof(row)) && strstr(row," present=failed present_why=ui_fault:") && strstr(row," present_restored=1 present_fault=addr=0 "));}
    /* A later UI frame runs X4's entry alone. */
    {void (*entry)(void*)=*(void(**)(void*))(region+PRES_UI_ENTRY);unsigned calls=test_pres_ui_calls;test_pres_off=1;entry(test_pres_self);test_pres_off=0;assert(test_pres_ui_calls==calls+1);}
    /* A failure found outside the pass: the tick gate leaves the stop to the frame thread for a while, then takes it. */
    {   DWORD saved_thread=ui_thread;ui_thread=GetCurrentThreadId();assert(!pslot_stop_deferred());
        ui_thread=GetCurrentThreadId()+1;pslot_stop_waits=0;assert(pslot_stop_deferred());pslot_stop_waits=PSLOT_STOP_WAIT;assert(!pslot_stop_deferred());
        ui_thread=saved_thread;pslot_stop_waits=0;}
    /* The next pass stops with the original failure, on the frame thread, before anything else can name another one. */
    stats.issued=11;assert(!pnat_pass(11,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
                           !strncmp(pnat_stage,"ui_fault:",9) && pilot_stop_reason()==PRES_STOP_REASON && !pslot_failed);
    /* An identity the self-test refuses: named, nothing left behind. */
    *contexts=1;pres_failed=0;pnat_failed=0;pnat_fail_stage=NULL;pres_why="-";pilot_failures=0;pres_stage="armed";pnat_stage="live";
    test_pres_answer_ship=0x999;assert(!pres_start(10) && !strcmp(pres_why,"identity_occupied") && !pres_active && !pres_open);
    pres_unhook();assert(pres_restored);test_pres_answer_ship=0;
    /* Presenting again for the rest of the flow. */
    pres_failed=0;pres_why="-";pres_stage="armed";pres_faults=0;pres_fault_detail[0]=0;
    assert(pres_start(10) && pres_active);test_pres_anchor_calls=1;pres_anchor_moves=1;test_pslot_rng=0x99;
    /* X4 moved the anchor back for slot 1's player (slot 1 changed seat): the next pass puts it in ship 2's room again. */
    memcpy(test_pres_universe_anchor+PRES_PARENT,&test_pres_room1,8);
    /* 11: slot 1's record on the canonical player, then slot 2's in its scope (Flight Started on P_2, d9c590 on C_2). */
    stats.issued=10;hooked(controller,100.0);
    stats.issued=11;test_pstk_call_count=0;test_pslot_call_count=0;
    assert(pnat_pass(11,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
    assert(test_pres_anchor_calls==2 && pres_anchor_moves==2 && !memcmp(test_pres_universe_anchor+PRES_PARENT,&(unsigned char*){test_pslot_room},8));
    /* The trigger of each slot's record of this tick: slot 2 held fire at capture 8, slot 1 did not. */
    {   int saved_mode=pfire_mode;pfire_mode=PFIRE_RECORD;
        assert(pfire_wanted(&pilot_slots[1],2,11) && !pfire_wanted(&pilot_slots[0],1,11) && !pfire_wanted(&pilot_slots[1],2,12));
        /* X4's own shoot controller coming back on a stock slot's gun is taken back, not a stop: replayed everywhere. */
        pfire_gun gun;memset(&gun,0,sizeof(gun));unsigned char shoot[16];void *vt=region+PFIRE_PLAYER_VTABLE;memcpy(shoot,&vt,8);
        const char *stage=pfire_stage;
        assert(pfire_replaced(&pilot_slots[1],&gun,2,0,shoot,0)==2 && gun.foreign);pfire_stage=stage;
        pfire_replaced_by_sim=0;pfire_last_tick=0;pfire_last_slot=0;pfire_last_gun=0;pfire_last_class="none";pfire_last_local=0;
        pfire_mode=saved_mode;}
    assert(test_pstk_calls[0].what=='s' && test_pstk_calls[0].id==0xd && !test_pstk_calls[0].flight);         /* slot 1: no flight control */
    assert(test_pstk_calls[1].what=='A' && test_pstk_calls[1].object==test_pslot_c2 && test_pstk_calls[2].what=='H' &&
           test_pstk_calls[2].object==test_pslot_c2 && test_pstk_calls[3].what=='+' && test_pstk_calls[3].object==p2 &&
           test_pstk_calls[4].what=='s' && test_pstk_calls[4].id==0xd && test_pstk_calls[4].flight==1 && test_pstk_call_count==5);
    assert(!*flight && !pstk_flight && pstk_slot_frames[1].flight==1 && s2->store[test_pslot_store_offset(PNAT_FLIGHT)]==1 && s2->replays==1);
    /* Slot 2 now has flight control: its routes answer with the PlayerController branch. */
    assert(s2->authority==0 && s2->vt.route[0]==(void*)test_pslot_player);
    test_pslot_call_count=0;test_pslot_route(test_pslot_c2,6);assert(test_pslot_calls[0].what=='p');
    {unsigned char a=pnat_authority(test_pslot_c2);assert(a==0);test_pslot_ship2[0xd1]=0;a=pnat_authority(test_pslot_c2);assert(a==1);test_pslot_ship2[0xd1]=1;}
    char text[4096];assert(pstk_frame_text(text,sizeof(text)) && !strncmp(text,"PSTOCK_1:11:1:",14) && strstr(text,"|S2:2:") && !strcmp(text+strlen(text)-2,":1"));
    {   unsigned char c=0,f=0;float fov=0;uint64_t target=0;assert(pview_of(2,&c,&f,&fov,&target) && c==1 && fov==0.5f);}
    assert(pnat_status(text,sizeof(text)) && strstr(text," present=presenting present_why=- present_slot=2 ") && strstr(text," camera_mode=0 asks=176ad5/176ad5/5ca00 hud=1/2 "));
    assert(pnat_status(text,sizeof(text)) && strstr(text," stock=capture slot=2 slots=3 ") && strstr(text," slots=live slots_why=- slots_mask=2 hook=1 ") &&
           strstr(text," s2=1/0/1/") && strstr(text," getters_out=13:1"));
    /* 12: slot 1's record is there, slot 2's is not: stop (155, a stock record). */
    stats.issued=11;hooked(controller,100.0);
    {   pstk_record r=r1;r.capture16=9;unsigned char b[PSTK_BYTES_MAX];unsigned n=pstk_encode(&r,b);
        pstk_ring[0][12%PSTK_RING].present=0;stats.issued=9;assert(pstk_stage(1,b,n,9));}
    stats.issued=12;assert(!pnat_pass(12,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
                           !strcmp(pnat_stage,"stock_missing_slot") && pilot_stop_reason()==PSTK_STOP_REASON);
    pnat_failed=0;pnat_fail_stage=NULL;
    /* A frame without the re-pointed call: stop (161). */
    stats.issued=13;assert(!pnat_pass(13,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) &&
                           !strcmp(pslot_why,"update_missing") && pilot_stop_reason()==PSLOT_STOP_REASON);
    pslot_failed=0;pnat_failed=0;pnat_fail_stage=NULL;pslot_why="-";pslot_stage="live";
    /* stop+1 (41): C_2 released in its scope with the stock vtable back, the call site restored. */
    test_pslot_call_count=0;stats.issued=41;
    assert(pnat_pass(41,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock) && !strcmp(pslot_stage,"released"));
    /* The presentation back first, as the canonical player; the UI entry and the refill call restored. */
    assert(!pres_active && *(void**)(region+PRES_UI_ENTRY)==(void*)test_pres_ui && !pres_ui_installed && !pres_refill_installed && pres_restored &&
           !memcmp(region+PRES_REFILL_CALL,pres_refill_bytes,5) && !memcmp(region+PRES_PICK_CALL,pres_pick_bytes,5) && !strcmp(pres_stage,"stopped"));
    assert(test_pres_anchor_calls==3 && !memcmp(test_pres_universe_anchor+PRES_PARENT,&test_pres_room1,8) && test_pslot_rng==0x99);
    assert(test_pres_room_now()==(void*)test_pres_room1);
    DeleteCriticalSection((CRITICAL_SECTION*)(test_pres_self+PRES_UI_LOCK));
    pres_armed=0;pres_slot=0;render_rng_installed=0;pres_fn=saved_pres;
    assert(!*(void**)(p2+0x70) && !*(void**)(p2+0x308));
    assert(test_pslot_calls[0].what=='X' && test_pslot_calls[0].singleton==p2 && !s2->seated && s2->released && !s2->controller &&
           *(void**)test_pslot_c2==(void*)(pnat_base+PSLOT_VTABLE) && !memcmp(region+PSLOT_CALL_SITE,pslot_call_bytes,5) && !pslot_hook_installed &&
           test_pslot_singleton()==p1 && !pnat_installed && pnat_restored);
    /* Seat refusals (fresh arming each): ship not a slot ship, unknown take entry, already player-controlled. */
    {   static const char *const reasons[3]={"ship_unresolved","ship_vtable","ship_controlled"};
        for(unsigned v=0;v<3;v++) {
            test_pslot_reset();pslot_fn.take=test_pslot_take;assert(pslot_arm_using(3));
            pslots[2].player=p2;pslot_p1=p1;memset(p2+0x70,0,8);
            void *agent=test_pslot_agent2,*room=test_pslot_room;memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);
            if(v==0) pilot_slots[1].player=1;
            if(v==1) pslot_fn.take=test_pslot_release;
            if(v==2) {void *c=test_pslot_c2;memcpy(test_pslot_ship2+0x200,&c,8);}
            assert(!pslot_seat(2) && !strcmp(pslot_why,reasons[v]) && !pslots[2].seated && test_pslot_singleton()==p1);
            pilot_slots[1].player=0;pslot_fn.take=test_pslot_take;
        }
        void *agent=test_pslot_agent2,*room=test_pslot_room;memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);}
    /* A ship without a cockpit room: the ship itself is the parent inside the scope. */
    {   void *agent=test_pslot_agent2,*room=test_pslot_room;
        test_pslot_reset();assert(pslot_arm_using(3));pslots[2].player=p2;pslot_p1=p1;
        memset(test_pslot_ship2+PSLOT_ROOM,0,8);test_pslot_parent=test_pslot_ship2;stats.issued=6;
        assert(pslot_seat(2) && pslots[2].seated && pslots[2].roomless && pslots[2].room==test_pslot_ship2 && test_pslot_singleton()==p1);
        assert(pslot_release(2) && !pslots[2].seated && *(void**)test_pslot_c2==(void*)(pnat_base+PSLOT_VTABLE));
        test_pslot_parent=test_pslot_room;memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);}
    /* A slot ship's death (lockstep_pilot_death.h): the session goes on, the slot's records consumed and ignored; a
     * controller change with another cause still stops; the ship MD built becomes the slot's at a fixed tick. */
    {   void *agent=test_pslot_agent2,*room=test_pslot_room;void *null=NULL;
        test_pslot_reset();memcpy(test_pslot_ship2+0x200,&agent,8);memcpy(test_pslot_ship2+PSLOT_ROOM,&room,8);test_pslot_parent=test_pslot_room;
        assert(pslot_arm_using(3));pslots[2].player=p2;pslot_p1=p1;memcpy(p2+0x70,&null,8);
        memset(pilot_slots,0,sizeof(pilot_slots));
        pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;pilot_slots[0].saved=0xa1;pilot_slots[0].runtime=0x1001;
        pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].saved=0xa2;pilot_slots[1].runtime=0x2001;
        pilot_slots[0].seen=pilot_slots[1].seen=1;pilot_slots[0].found=test_pnat_ship;pilot_slots[1].found=test_pslot_ship2;
        *(double*)(test_pnat_ship+PDEATH_HULL)=100.0;*(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;
        *(uint64_t*)(test_pnat_ship+8)=0x1001;*(uint64_t*)(test_pslot_ship2+8)=0x2001;
        test_pdeath_ids[0]=0x1001;test_pdeath_ids[1]=0x2001;test_pdeath_ids[2]=0;test_pdeath_objects[0]=test_pnat_ship;test_pdeath_objects[1]=test_pslot_ship2;test_pdeath_objects[2]=test_pslot_ship2;
        pdeath_find=(pilot_find_fn)test_pdeath_find;pdeath_map=test_pdeath_ids;pdeath_player=test_pnat_ship;
        {   int32_t call=(int32_t)((intptr_t)(region+PDEATH_GAME_OVER)-(intptr_t)(region+PDEATH_GAME_OVER_CALL+5));
            region[PDEATH_GAME_OVER_CALL]=0xe8;memcpy(region+PDEATH_GAME_OVER_CALL+1,&call,4);}
        /* Arming: stock piloting and the eject option, every refusal named. */
        pdeath_armed=0;pnat_mode=0;assert(!pdeath_arm_using(5,0) && !strcmp(pstk_arm_why,"death_without_stock"));pnat_mode=PNAT_MODE_STOCK;
        assert(!pdeath_arm_using(PILOT_TICK_MAX+1u,0) && !strcmp(pstk_arm_why,"death_delay"));
        test_pnat_eject_option=0;assert(!pdeath_arm_using(5,0) && !strcmp(pstk_arm_why,"death_eject_off"));test_pnat_eject_option=1;
        assert(pdeath_arm_using(5,0) && pdeath_armed && !strcmp(pdeath_stage,"armed"));
        assert(!pdeath_arm_using(6,0) && !strcmp(pstk_arm_why,"death_rerun") && !pdeath_arm_using(5,1) && pdeath_arm_using(5,0));
        /* Off: the same text as ever, armed: the dead mask in the declared state. */
        pilot_start=10;pilot_stop=60;pstk_capture_start=8;pstk_first_apply=11;pstk_flight=0;pstk_flight_known=1;*flight=0;*counter=0;*contexts=1;
        memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));pstk_local_active=1;pstk_capturing=1;pstk_capture_slot=2;
        memset(&pstk_build,0,sizeof(pstk_build));
        pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pslot_failed=0;pslot_why="-";pslot_stage="armed";stats.slot=2;pnat_input=input;
        stats.issued=6;assert(pslot_seat(2) && pslots[2].seated && pslots[2].first_length==pslots[2].length && pslot_hook_install());
        pslot_slot *s=&pslots[2];void **cvtable=*(void***)test_pslot_c2;
        assert(pnat_install(controller));
        int32_t site;memcpy(&site,region+PSLOT_CALL_SITE+1,4);void (*frame)(void*,double)=(void(*)(void*,double))(region+PSLOT_CALL_SITE+5+site);
#define TEST_PDEATH_PASS(t,c) (stats.issued=(t)-1,frame(controller,100.0),stats.issued=(t),test_pdeath_stage(t),pnat_pass(t,c,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock))
        /* 12: alive. The guard in the game over's place from the first pass, X4's call re-pointed. */
        assert(TEST_PDEATH_PASS(12,controller) && !pdeath_dead && pdeath_guard_installed && !strcmp(pdeath_stage,"live") && pdeath_held[0][0xd]);
        {   int32_t at;memcpy(&at,region+PDEATH_GAME_OVER_CALL+1,4);
            assert(region[PDEATH_GAME_OVER_CALL]==0xe8 && region+PDEATH_GAME_OVER_CALL+5+at==pslot_page+PDEATH_STUB);}
        /* A controller change without a dying ship: stop, as ever. */
        {   static unsigned char other[0x100];
            assert(!TEST_PDEATH_PASS(13,other) && !strcmp(pnat_stage,"stock_controller_changed") && pilot_stop_reason()==PSTK_STOP_REASON && !pdeath_dead);
            pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;
            /* Slot 1's controller went with its ship before the pass saw it: a scope must not read it. */
            unsigned char *kept=pnat_controller;pnat_controller=other;assert(pslot_enter(2) && pslot_leave(2));pnat_controller=kept;}
        /* Slot 2's hull at zero in this frame: its update is skipped (simulation state: on every client). */
        *(double*)(test_pslot_ship2+PDEATH_HULL)=0.0;
        {LONG64 updates=s->updates;stats.issued=13;test_pslot_call_count=0;frame(controller,100.0);assert(s->updates==updates && test_pslot_call_count==1 && test_pslot_calls[0].object==controller);}
        /* 14: the ship is whole, the kill still on its way: released in its scope, as the stock eject's order. */
        pfire_armed=1;test_pslot_call_count=0;test_pstk_call_count=0;
        LONG64 replays=s->replays;
        stats.issued=14;test_pdeath_stage(14);
        assert(pnat_pass(14,controller,test_pnat_refresh,counter,contexts,flight,pnat_authority_stock));
        pfire_armed=0;
        assert(pdeath_dead==2 && pilot_slots[1].dead && !pilot_slots[1].resolved && !pilot_slots[1].object && !s->seated && !s->controller &&
               test_pslot_count('X')==1 && *(void**)test_pslot_c2==(void*)(region+PSLOT_VTABLE) && !*(void**)(p2+0x70) && !pview_slot_player);
        /* Its record: consumed, its hash stays, nothing replayed, no fire, no view. */
        assert(pstk_slot_frames[1].tick==14 && s->replays==replays && !pstk_fire_on[1] && pdeath_slots[1].dead_tick==14 && pdeath_slots[1].deaths==1);
        {   char text[256];assert(pstk_frame_text(text,sizeof(text)) && strstr(text,"|S2:1:") && !strcmp(text+strlen(text)-3,"|D2"));}
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"die:2") && !pdeath_take_events());}
        /* Slot 1's ship goes: the eject took the controller. Flying: flight control off once, as the stock callback would, on every client. */
        pstk_flight=1;*flight=1;*(double*)(test_pnat_ship+PDEATH_HULL)=0.0;
        {   static unsigned char suit_controller[0x100];
            test_pstk_call_count=0;
            assert(TEST_PDEATH_PASS(15,suit_controller));
            assert(pdeath_dead==3 && pilot_slots[0].dead && !pnat_controller && !pnat_ship && !pstk_flight && !*flight && test_pstk_count('L')==1 && test_pstk_count('-')==1);
            assert(!test_pstk_count('s') && !pstk_frame_events && pstk_frame_hash && !pstk_fire_on[0]);
            {const char *events=pdeath_take_events();assert(events && !strcmp(events,"die:1"));}
            /* Dead: no flight or controller check, no replay; the aim of X4's player between ships is not a stop. */
            test_pstk_call_count=0;assert(TEST_PDEATH_PASS(16,suit_controller) && !test_pstk_count('s') && !strcmp(pdeath_stage,"dead"));
            assert(TEST_PDEATH_PASS(17,NULL));}
        /* The capture goes on without aim and view; a dead slot's record is valid and ignored. */
        {   unsigned char rec2[PSTK_BYTES_MAX];unsigned len2=0;const char *why2="";
            pstk_capture_slot=1;assert(pstk_capture(17,rec2,&len2,&why2) && !(rec2[1]&(PSTK_FLAG_AIM|PSTK_FLAG_VIEW)));pstk_capture_slot=2;memset(pstk_locals,0,sizeof(pstk_locals));}
        /* The MD answer before its time is refused. */
        assert(!pdeath_ship(2,0x2002));
        /* 19 and 20: dead+5, the probe raises the MD event; the answer for a slot waiting for one. */
        assert(TEST_PDEATH_PASS(18,NULL));
        assert(TEST_PDEATH_PASS(19,NULL) && pdeath_slots[1].state==PDEATH_CREATING);
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"create:2"));}
        assert(pdeath_ship(2,0x2002) && !pdeath_ship(2,0x2003) && !pdeath_ship(1,0x1002));
        assert(TEST_PDEATH_PASS(20,NULL) && pdeath_slots[0].state==PDEATH_CREATING && pdeath_ship(1,0x1002));
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"create:1"));}
        for(uint64_t t=21;t<34;t++) assert(TEST_PDEATH_PASS(t,NULL));
        /* 34: slot 2's new ship is the slot's: the first store again, seated in its scope; the local controls get their edge. */
        *(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;*(uint64_t*)(test_pslot_ship2+8)=0x2002;test_pdeath_ids[1]=0x2002;
        {   void *a=test_pslot_agent2;memcpy(test_pslot_ship2+0x200,&a,8);}
        s->store[test_pslot_store_offset(0x2f4b750)]=0x7f;memset(&pstk_build,0,sizeof(pstk_build));
        {   unsigned char first=s->first[test_pslot_store_offset(0x2f4b720)];
            assert(TEST_PDEATH_PASS(34,NULL));
            assert(pdeath_dead==1 && !pilot_slots[1].dead && pilot_slots[1].renewed && pilot_slots[1].runtime==0x2002 && pilot_slots[1].resolved && pilot_slots[1].object==test_pslot_ship2 &&
                   s->seated && s->controller==test_pslot_c2 && *(void**)(p2+0x70)==(void*)test_pslot_room && pview_slot_player==p2 && pdeath_slots[1].respawns==1 &&
                   s->store[test_pslot_store_offset(0x2f4b720)]==first && s->store[test_pslot_store_offset(0x2f4b750)]==0x5a &&
                   pstk_build.count==1 && pstk_build.events[0].kind==PSTK_SPACE && pstk_build.events[0].value==1);}
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"live:2"));}
        /* X4 skips the frame call when its player has no PlayerController: with slot 1 dead slot 2 still updates, from the pass. */
        {   LONG64 updates=s->updates;stats.issued=35;pslot_update_tick=33;test_pslot_call_count=0;
            assert(pslot_check(35) && s->updates==updates+1 && test_pslot_call_count==1 && test_pslot_calls[0].object==test_pslot_c2);
            pilot_slots[0].dead=0;assert(!pslot_check(35) && !strcmp(pslot_why,"update_missing"));pilot_slots[0].dead=1;pslot_failed=0;pslot_why="-";pslot_stage="live";}
        /* A presenting client whose slot is dead has no player to act as: local ids are dropped. */
        {   LONG64 dropped=pstk_dropped;unsigned saved_present=pres_slot;pres_slot=1;pnat_dispatch_action(map,0xa9);
            assert(pstk_dropped==dropped+1);pres_slot=saved_present;}
        memset(&pstk_build,0,sizeof(pstk_build));
        /* A claim on the new ship answers to the slot's saved ID. */
        assert(hitn_runtime_of(0xa2)==0x2002);
        /* 35: slot 1 without a ship: named. With it: controller and ship latched, what its records left held released. */
        {   pdeath_slot *d=&pdeath_slots[0];uint64_t answer=d->ship;d->ship=0;
            assert(!pdeath_seat(1,controller) && !strcmp(pdeath_why,"respawn_no_ship") && pdeath_failed);pdeath_failed=0;pdeath_why="-";pilot_slots[0].dead=1;d->ship=answer;
            *(double*)(test_pnat_ship+PDEATH_HULL)=100.0;*(uint64_t*)(test_pnat_ship+8)=0x1002;test_pdeath_ids[0]=0x1002;
            static unsigned char wrong[0x100];
            assert(!pdeath_seat(1,wrong) && !strcmp(pdeath_why,"respawn_not_in_control") && pilot_slots[0].dead && !pnat_controller);pdeath_failed=0;pdeath_why="-";
            test_pstk_call_count=0;
            assert(TEST_PDEATH_PASS(35,controller));
            assert(!pdeath_dead && pnat_controller==controller && pnat_ship==test_pnat_ship && pilot_slots[0].renewed && pilot_slots[0].player && pilot_slots[0].runtime==0x1002 && d->respawns==1);
            assert(test_pstk_calls[0].what=='s' && test_pstk_calls[0].id==0xd && !test_pstk_calls[0].value);  /* released, then this tick's record presses again */
            {const char *events=pdeath_take_events();assert(events && !strcmp(events,"live:1"));}
            char text[256];assert(pstk_frame_text(text,sizeof(text)) && !strcmp(text+strlen(text)-3,"|D0"));}
        /* 36: slot 2 again, the kill already through: the controller stays with the ship under its copy; its entries do nothing. */
        pilot_slots[1].found=NULL;
        {   test_pslot_call_count=0;assert(TEST_PDEATH_PASS(36,controller));
            assert(pdeath_dead==2 && !s->seated && !s->controller && !test_pslot_count('X') && *(void***)test_pslot_c2==cvtable && pslot_is_retired(test_pslot_c2) && !*(void**)(p2+0x70));
            ((void(*)(void*,double))cvtable[33])(test_pslot_c2,100.0);((void(*)(void*,void*,void*))cvtable[34])(test_pslot_c2,(void*)1,(void*)2);
            ((void(*)(void*,void*,void*,void*))cvtable[35])(test_pslot_c2,(void*)1,(void*)2,(void*)3);((void(*)(void*,void*,void*,char))cvtable[36])(test_pslot_c2,(void*)1,(void*)2,4);
            assert(!pnat_failed && !pslot_failed);
            unsigned char a=7;assert(pslot_authority(test_pslot_c2,&a) && a==1);}
        /* The resolve: a dead slot has no ship, a renewed one needs no saved identity or controller. */
        {   pilot_slot probe;memset(&probe,0,sizeof(probe));probe.reversed=1;probe.runtime=0x2002;probe.dead=1;probe.saved=0xa2;
            assert(!pilot_resolve(&probe,(pilot_find_fn)test_pdeath_find,test_pdeath_ids,NULL) && !probe.found);
            probe.dead=0;probe.renewed=1;
            assert(pilot_resolve(&probe,(pilot_find_fn)test_pdeath_find,test_pdeath_ids,NULL) && probe.seen && probe.found==test_pslot_ship2 && probe.object==test_pslot_ship2);
            probe.runtime=0x9999;assert(!pilot_resolve(&probe,(pilot_find_fn)test_pdeath_find,test_pdeath_ids,NULL) && !probe.found && pdeath_dying(&probe));}
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"die:2"));}
        /* 37: X4's game over stays out of reach in the same way below; first the suit. Slot 2 dead again, now with a suit:
         * the seat on what MD built, no ship of the slot's, its records still ignored. */
        {   void *a=test_pslot_agent2;memcpy(test_pslot_ship2+0x200,&a,8);memset(pslots[2].retired,0,sizeof(pslots[2].retired));}
        pdeath_suit=1;pdeath_slots[1].state=PDEATH_ALIVE;pilot_slots[1].dead=0;pdeath_dead=0;pilot_slots[1].seen=1;pilot_slots[1].found=NULL;
        pdeath_slots[1].ship=0;test_pslot_ship_vt[PDEATH_KILL_SLOT]=(void*)test_pdeath_kill;test_pdeath_kills=0;
        assert(TEST_PDEATH_PASS(37,controller) && pdeath_dead==2 && pdeath_slots[1].suit_tick==42);
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"die:2,suit:2"));}
        assert(!pdeath_suit_ship(1,0x2003) && pdeath_suit_ship(2,0x2003) && !pdeath_suit_ship(2,0x2009));
        *(uint64_t*)(test_pslot_ship2+8)=0x2003;test_pdeath_ids[2]=0x2003;*(double*)(test_pslot_ship2+PDEATH_HULL)=100.0;
        for(uint64_t t=38;t<42;t++) assert(TEST_PDEATH_PASS(t,controller) && !s->seated);
        assert(TEST_PDEATH_PASS(42,controller) && s->seated && s->ship==test_pslot_ship2 && s->runtime==0x2003 && pdeath_slots[1].in_suit && pilot_slots[1].dead &&
               pdeath_dead==2 && pdeath_slots[1].state==PDEATH_CREATING && *(void**)(p2+0x70)==(void*)test_pslot_room && !pdeath_slots[1].suit_tick);
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"insuit:2,create:2"));}
        /* In the suit: the update in its scope, no record replayed, the local ids run in the scope again. */
        {   LONG64 updates=s->updates,replays2=s->replays;
            assert(TEST_PDEATH_PASS(43,controller) && s->updates==updates+1 && s->replays==replays2);
            unsigned saved_present=pres_slot;pres_slot=2;assert(pres_has_seat());pres_slot=saved_present;
            char row[1024];assert(pdeath_status(row,sizeof(row)) && strstr(row," dead=2 delay=5 suit=1 ") && strstr(row," d2=2/3/1/37/suit"));}
        /* An answer that never came on a client: named, not a silent difference. */
        {   pdeath_slot *d=&pdeath_slots[1];pdeath_slot kept=*d;d->in_suit=0;d->suit_tick=44;d->suit=0;
            assert(!pdeath_suit_pass(2,44) && !strcmp(pdeath_why,"suit_no_ship") && pdeath_failed);
            *d=kept;pdeath_failed=0;pdeath_why="-";pdeath_stage="dead";}
        /* The suit gone by another way than a hit: the seat ends, the slot stays dead. */
        {   pdeath_slot *d=&pdeath_slots[1];uint64_t id=test_pdeath_ids[2];unsigned char *seated_ship=s->ship;void *c=s->controller;test_pdeath_ids[2]=0;
            assert(pdeath_suit_pass(2,44) && !d->in_suit && !s->seated && pslot_is_retired(c));
            {const char *events=pdeath_take_events();assert(events && !strcmp(events,"nosuit:2"));}
            /* Back in it for the release below. */
            test_pdeath_ids[2]=id;void *a=test_pslot_agent2;memcpy(seated_ship+0x200,&a,8);memset(s->retired,0,sizeof(s->retired));
            assert(pslot_seat_ship(2,seated_ship) && s->seated);d->in_suit=1;}
        /* 57: the new ship: out of the suit in its scope, the suit killed as X4 kills a player's old one, seated in the new. */
        assert(pdeath_ship(2,0x2004));
        for(uint64_t t=44;t<57;t++) assert(TEST_PDEATH_PASS(t,controller));
        *(uint64_t*)(test_pslot_ship2+8)=0x2004;test_pdeath_ids[1]=0x2004;test_pslot_call_count=0;
        assert(!test_pdeath_kills && TEST_PDEATH_PASS(57,controller));
        assert(test_pdeath_kills==1 && test_pslot_count('X')==1 && test_pslot_count('T')==1 && !pdeath_slots[1].in_suit &&
               !pdeath_dead && !pilot_slots[1].dead && pilot_slots[1].runtime==0x2004 && s->seated && s->runtime==0x2004 && pdeath_slots[1].respawns==2);
        {const char *events=pdeath_take_events();assert(events && !strcmp(events,"live:2"));}
        pdeath_suit=0;
        /* The player killed some other way: X4's game over never runs; the session stops, named (163). */
        {   void (*guard)(void)=(void(*)(void))(pslot_page+PDEATH_STUB);guard();
            assert(pdeath_game_overs==1 && pdeath_failed && pnat_failed && pilot_stop_reason()==PDEATH_STOP_REASON);
            assert(!TEST_PDEATH_PASS(58,controller) && !strcmp(pnat_stage,"player_killed"));
            char row[1024];assert(pdeath_status(row,sizeof(row)) && strstr(row," death=failed death_why=player_killed dead=0 delay=5 suit=0 guard=1 game_overs=1 seats=1/1 d1=0/1/1/15 d2=0/3/2/37"));}
        /* At the end the call is X4's again. */
        pdeath_restore();
        {   int32_t call=(int32_t)((intptr_t)(region+PDEATH_GAME_OVER)-(intptr_t)(region+PDEATH_GAME_OVER_CALL+5));int32_t now;memcpy(&now,region+PDEATH_GAME_OVER_CALL+1,4);
            assert(!pdeath_guard_installed && region[PDEATH_GAME_OVER_CALL]==0xe8 && now==call);}
#undef TEST_PDEATH_PASS
        /* Leave nothing behind for the blocks below. */
        pnat_uninstall();pslot_hook_uninstall();pdeath_armed=0;pdeath_failed=0;pdeath_dead=0;pdeath_why="-";pdeath_find=NULL;pdeath_map=NULL;pdeath_player=NULL;
        pnat_failed=0;pnat_fail_stage=NULL;pilot_failures=0;pslot_failed=0;pslot_why="-";pstk_capture_slot=2;
        memset(pilot_slots,0,sizeof(pilot_slots));
        pilot_slots[0].resolved=1;pilot_slots[0].object=test_pnat_ship;pilot_slots[0].player=1;
        pilot_slots[1].resolved=1;pilot_slots[1].object=test_pslot_ship2;pilot_slots[1].player=0;
        *(uint64_t*)(test_pslot_ship2+8)=0;*(uint64_t*)(test_pnat_ship+8)=0;
        memcpy(test_pslot_ship2+0x200,&agent,8);memset(test_pslot_c2,0,sizeof(test_pslot_c2));
        pilot_start=10;pilot_stop=40;pstk_flight=0;*flight=0;memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(pstk_ring,0,sizeof(pstk_ring));pnat_controller=controller;pnat_ship=test_pnat_ship;}
    /* The PlayerController branches are the stock prologue, then an indirect jump to the continuation. */
    {   static void (*const branches[3])(void)={pslot_player6,pslot_player7,pslot_player10};
        for(unsigned i=0;i<3;i++) {
            const unsigned char *t=(const unsigned char*)branches[i];unsigned n=pslot_branches[i].a_len+pslot_branches[i].b_len;int32_t rel;
            assert(!memcmp(t,pslot_branches[i].pa,pslot_branches[i].a_len) && !memcmp(t+pslot_branches[i].a_len,pslot_branches[i].pb,pslot_branches[i].b_len));
            assert(t[n]==0xff && t[n+1]==0x25);memcpy(&rel,t+n+2,4);assert(t+n+6+rel==(const unsigned char*)&pslot_player_resume[i]);
        }}
    /* Three stock slots on client 3: both players from the factory, both seated, two scopes in one frame with their own
     * stores and routes, the presentation of slot 3 with slot 2 remote, both released. */
    {   test_pslot_reset();test_pslot3_made=0;test_pslot3_row_count=0;pview_slot_player=NULL;
        void **cvt=(void**)(region+PSLOT_VTABLE);cvt[49]=(void*)test_pslot3_assist;cvt[50]=(void*)test_pslot3_mode;
        vps_create=test_pslot3_factory;pnat_fn.controller=test_pslot3_controller_of;
        pslot_fn.take=test_pslot3_take;pslot_fn.release=test_pslot3_release;pslot_fn.update=test_pslot3_update;
        test_pslot3_ship_vt[PSLOT_TAKE]=(void*)test_pslot3_take;test_pslot3_ship_vt[PSLOT_RELEASE]=(void*)test_pslot3_release;
        test_pslot3_room_vt[PSLOT_IS_CLASS]=(void*)test_pslot3_is_room;
        for(unsigned i=0;i<2;i++) {
            void *svt=test_pslot3_ship_vt,*rvt=test_pslot3_room_vt,*room=test_pslot3_room[i];
            memset(test_pslot3_ship[i],0,0x500);memcpy(test_pslot3_ship[i],&svt,8);memcpy(test_pslot3_ship[i]+PSLOT_ROOM,&room,8);test_pslot3_ship[i][0xd1]=1;
            memcpy(test_pslot3_room[i],&rvt,8);
            pilot_slots[i+1].resolved=1;pilot_slots[i+1].object=test_pslot3_ship[i];pilot_slots[i+1].player=0;
        }
        pilot_slot_count=3;stats.slot=3;region[0x2f4b750]=0x11;
        assert(pslot_arm_using(7) && pslot_mask==6);
        assert(pslot_construct_all() && test_pslot3_made==2 && pslots[2].player==test_pslot3_p[0] && pslots[3].player==test_pslot3_p[1] &&
               pslot_p1==p1 && test_pslot_singleton()==p1);
        uint64_t canonical3=pslot_hash();
        assert(pslot_seat_all() && pslots[2].seated && pslots[3].seated && pslot_hash()==canonical3 && test_pslot_singleton()==p1);
        assert(pslots[2].store[test_pslot_store_offset(0x2f4b750)]==0x60 && pslots[3].store[test_pslot_store_offset(0x2f4b750)]==0x61 && region[0x2f4b750]==0x11);
        assert(pslots[2].vt.slots!=pslots[3].vt.slots && pslot_of(test_pslot3_c[0])==2 && pslot_of(test_pslot3_c[1])==3 &&
               *(void**)test_pslot3_c[0]==(void*)&pslots[2].vt.slots[0] && *(void**)test_pslot3_c[1]==(void*)&pslots[3].vt.slots[0]);
        /* Only the capturing slot's player answers its view sample. */
        assert(pview_slot_player==test_pslot3_p[1]);
        /* One frame: the canonical update, then slot 2's and slot 3's, each with its own player and control block. */
        pslot_update(controller,100.0);
        assert(test_pslot3_row_count==3 && test_pslot3_rows[0].controller==controller && test_pslot3_rows[0].singleton==p1 && test_pslot3_rows[0].bank==0x11 &&
               test_pslot3_rows[1].controller==test_pslot3_c[0] && test_pslot3_rows[1].singleton==test_pslot3_p[0] && test_pslot3_rows[1].bank==0x60 &&
               test_pslot3_rows[2].controller==test_pslot3_c[1] && test_pslot3_rows[2].singleton==test_pslot3_p[1] && test_pslot3_rows[2].bank==0x61 &&
               test_pslot_singleton()==p1 && pslot_hash()==canonical3);
        /* A write in slot 2's scope stays in slot 2's store. */
        assert(pslot_enter(2));*PNAT_U8(0x2f4b750)=0x70;assert(pslot_leave(2));
        assert(pslots[2].store[test_pslot_store_offset(0x2f4b750)]==0x70 && pslots[3].store[test_pslot_store_offset(0x2f4b750)]==0x61 && region[0x2f4b750]==0x11);
        /* Each authority answer is its own slot's. */
        pslots[2].authority=1;pslots[3].authority=0;
        {unsigned char a=7;assert(pslot_authority(test_pslot3_c[0],&a) && a==1 && pslot_authority(test_pslot3_c[1],&a) && a==0);}
        /* Client 3 presents slot 3, slot 2 is remote. */
        pres_slot=3;pres_active=1;pres_open=0;*(int*)(region+PRES_VIEW_INDEX)=0;
        {   void *room3=test_pslot3_room[1],*parent=NULL,*view=NULL,*parent1=NULL,*view1=NULL;
            memcpy(&parent1,p1+PRES_PARENT,8);memcpy(&view1,p1+PRES_VIEW_CACHE,8);
            assert(pres_enter() && test_pslot_singleton()==p1 && region[0x2f4b750]==0x61);
            memcpy(&parent,p1+PRES_PARENT,8);memcpy(&view,p1+PRES_VIEW_CACHE,8);assert(parent==room3 && view==room3);
            pres_leave();memcpy(&parent,p1+PRES_PARENT,8);memcpy(&view,p1+PRES_VIEW_CACHE,8);
            assert(parent==parent1 && view==view1 && region[0x2f4b750]==0x11 && pslot_hash()==canonical3);}
        pres_slot=0;pres_active=0;
        assert(pslot_release_all() && !pslots[2].seated && !pslots[3].seated && !strcmp(pslot_stage,"released") && !pview_slot_player);
        assert(!*(void**)(test_pslot3_p[0]+0x70) && !*(void**)(test_pslot3_p[1]+0x70) && !memcmp(region+PSLOT_CALL_SITE,pslot_call_bytes,5));
        memset(&pilot_slots[2],0,sizeof(pilot_slots[2]));
        vps_create=test_pslot_factory;pnat_fn.controller=test_pslot_controller_of;}
    printf("pilot stock slots 2..N arming/construct/seat/vtable copy/routes/authority/update hook/capture/staging/replay/release/fail-closed tests PASS\n");
    test_pslot_reset();
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_slot=0;pilot_stock_slots=0;pstk_slot_mask=0;pstk_capture_slot=0;pnat_stage="off";
    pnat_failed=0;pnat_fail_stage=NULL;pilot_armed=0;pilot_suppress=0;pilot_source=0;pilot_slot_count=0;pilot_delay=0;pilot_controls_owned=0;
    pilot_failures=0;pilot_start=pilot_stop=0;pilot_stage="off";memset(pilot_slots,0,sizeof(pilot_slots));stats.issued=saved_issued;stats.slot=saved_slot;
    memcpy(pstk_handler_ptrs,saved_ptrs,sizeof(saved_ptrs));pstk_fn=saved_pstk;pslot_fn=saved_slots;ui_thread=saved_ui_thread;
    vps_create=saved_create;vps_rng_word=saved_rng;
    pstk_aim_tick=0;pstk_aim_valid_replay=0;pstk_aim_installed=0;InterlockedExchange(&pstk_aim_on,0);
    pview_fn=saved_view;if(pview_page) {VirtualFree(pview_page,0,MEM_RELEASE);pview_page=NULL;}pview_installed=0;
    for(unsigned i=0;i<PILOT_SLOTS;i++) InterlockedExchange64(&pview_state[i],0);
    if(pslot_page) {VirtualFree(pslot_page,0,MEM_RELEASE);pslot_page=NULL;}
    memset(test_pnat_universe,0,sizeof(test_pnat_universe));memset(test_pnat_player,0,sizeof(test_pnat_player));
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));memset(pstk_ring,0,sizeof(pstk_ring));
    pnat_base=saved_image_base;pnat_fn=saved_fns;VirtualFree(region,0,MEM_RELEASE);
}
/* stock-J1: gate.pilot_stock refused on both clients. The real order on the real bytes
 * (a relocated copy of the image, no X4 code runs): the pins seen with the boot hold
 * already patched into UniverseFrameUpdate at arming, then the whole arming path. */
static void test_pilot_slots_arming_order(const char *path) {
    unsigned char *prior=image,*prior_thunk=thunk,*prior_base=pnat_base;
    uint64_t saved_issued=stats.issued;unsigned saved_slot=stats.slot;int saved_active=stats.active;DWORD saved_ui_thread=ui_thread;
    pnat_fns saved_fns=pnat_fn;pstk_fns saved_pstk=pstk_fn;pslot_fns saved_slots=pslot_fn;pview_fns saved_view=pview_fn;
    vps_create_fn saved_create=vps_create;uint64_t *(*saved_rng)(void)=vps_rng_word;int saved_boot=boot_mode;int saved_phase=boot_phase;
    const void *saved_ptrs[PSTK_HANDLERS];memcpy(saved_ptrs,pstk_handler_ptrs,sizeof(saved_ptrs));
    image=test_pmath_map(path);thunk=make_thunk();assert(thunk);
    pnat_pins_checked=pstk_pins_checked=pslot_pins_checked=vps_pins_checked=vpl_pins_checked=0;
    stats.active=1;stats.issued=0;stats.slot=2;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    /* Pristine: every table of the slots path passes on the bytes as loaded. */
    assert(pnat_stock_pins(3u));
    /* The real boot hold at its real point (armed before gate.pilot_stock): the whole-function hash the
     * first build pinned now fails, which is what stock-J1 hit; the call-site pin does not. */
    const char *whole="ceea80a071d72ff5c47433884c1e64df2a570913077d631b2775adf989a93e93";
    assert(hash_ok(image+0x9fdef0,3323,whole));
    assert(boot_hold_arm() && boot_hold_installed);
    assert(!hash_ok(image+0x9fdef0,3323,whole));
    /* Every table from scratch with it installed (no cached answer), as an arming without the early check. */
    pnat_pins_checked=pstk_pins_checked=pslot_pins_checked=vps_pins_checked=vpl_pins_checked=0;
    assert(pnat_stock_pins(3u));
    /* The calls the other modules re-point in UniverseFrameUpdate do not touch the pin either. */
    static const unsigned others[]={0x9fe2e0u,0x9fe337u,0x9fe427u,0x9fe573u};
    unsigned char kept[sizeof(others)/sizeof(*others)];
    for(unsigned i=0;i<sizeof(others)/sizeof(*others);i++) {assert(image[others[i]]==0xe8);kept[i]=image[others[i]+1];image[others[i]+1]^=0x55;}
    pslot_pins_checked=0;assert(pslot_pins());
    for(unsigned i=0;i<sizeof(others)/sizeof(*others);i++) image[others[i]+1]=kept[i];
    /* Cutscene ready: the native post, the manager's update, X4's post and string copy; the site's bytes. */
    cr_pins_checked=0;assert(cr_pins() && image[CR_SITE]==0x0f && image[CR_SITE+1]==0x85 && image[CR_SITE-7]==0x80);
    /* Ship death: the stock path it relies on, the call the guard re-points. */
    pdeath_pins_checked=0;assert(pdeath_pins());
    {   int32_t call;memcpy(&call,image+PDEATH_GAME_OVER_CALL+1,4);
        assert(image[PDEATH_GAME_OVER_CALL]==0xe8 && image+PDEATH_GAME_OVER_CALL+5+call==image+PDEATH_GAME_OVER);
        assert(((PDEATH_GAME_OVER_CALL+4)&~15u)==(PDEATH_GAME_OVER_CALL&~15u));}
    /* The whole arming path, client 2 with slots 1 and 2 as stock-J1's config asks. */
    pilot_armed=1;pilot_suppress=1;pilot_source=PILOT_SOURCE_WIRE;pilot_slot_count=2;pilot_delay=3;pilot_device_armed=0;pilot_start=30;pilot_stop=600;
    pnat_armed=0;pnat_mode=0;test_pslot_reset();
    assert(pnat_arm_stock(1,3u) && pnat_armed && pnat_mode==PNAT_MODE_STOCK && pslot_armed && pslot_mask==2 && pstk_slot_mask==3 && pstk_capture_slot==2 &&
           vps_create==(vps_create_fn)(image+VPS_FACTORY) && pslot_fn.update==(void(*)(void*,double))(image+PSLOT_UPDATE_RVA) && pnat_base==image);
    assert(pnat_arm_stock(1,3u) && !pnat_arm_stock(0,3u)); /* the reload's identical rerun */
    /* The call site on the real bytes: installed and restored with the boot hold's word next to it. */
    assert(pslot_hook_install() && pslot_hook_intact() && image[PSLOT_CALL_SITE]==0xe8 && memcmp(image+PSLOT_CALL_SITE,pslot_call_bytes,5));
    assert(pslot_hook_uninstall() && !memcmp(image+PSLOT_CALL_SITE,pslot_call_bytes,5));
    /* Presentation on the real bytes: its pins with the boot hold installed, the refill call re-pointed and restored. */
    pres_armed=0;pres_pins_checked=0;render_rng_installed=1;pres_fns saved_pres=pres_fn;
    assert(pres_arm(1) && pres_armed && pres_slot==2 && pres_fn.ui_move==(void(*)(void*))(image+PRES_UI_MOVE));
    assert(*(void**)(image+PRES_UI_ENTRY)==(void*)(image+PRES_UI_MOVE));
    assert(pslot_hook_install() && pres_refill_install() && image[PRES_REFILL_CALL]==0xe8 && memcmp(image+PRES_REFILL_CALL,pres_refill_bytes,5));
    assert(pres_pick_install() && image[PRES_PICK_CALL]==0xe8 && memcmp(image+PRES_PICK_CALL,pres_pick_bytes,5));
    pres_pick_uninstall();assert(!memcmp(image+PRES_PICK_CALL,pres_pick_bytes,5));
    pres_refill_uninstall();assert(!memcmp(image+PRES_REFILL_CALL,pres_refill_bytes,5));assert(pslot_hook_uninstall());
    image[PRES_UI_ANCHORS+3]^=1;pres_armed=0;pres_pins_checked=0;
    assert(!pres_arm(1) && !strcmp(pstk_arm_why,"pins_present:8e8450"));image[PRES_UI_ANCHORS+3]^=1;
    pres_armed=0;pres_slot=0;pres_pins_checked=0;render_rng_installed=0;pres_fn=saved_pres;
    /* gate.actor on a stock slot's ship, on the real image (its guard hashes X4's lookup). */
    test_pslot_actor();
    /* A changed function names the table and the RVA, from the early check and from the arming. */
    pnat_armed=0;pnat_mode=0;test_pslot_reset();pslot_pins_checked=0;
    image[PSLOT_TAKE_RVA+3]^=1;
    assert(!pnat_stock_pins(3u) && !strcmp(pstk_arm_why,"pins_slots:4ed040"));
    assert(!pnat_arm_stock(1,3u) && !strcmp(pstk_arm_why,"pins_slots:4ed040"));
    image[PSLOT_TAKE_RVA+3]^=1;pslot_pins_checked=0;
    image[0x8e0250+3]^=1;vps_pins_checked=0;assert(!pnat_stock_pins(3u) && !strcmp(pstk_arm_why,"pins_construct:8e0250"));image[0x8e0250+3]^=1;vps_pins_checked=0;
    image[0xd898c0+3]^=1;vpl_pins_checked=0;assert(!pnat_stock_pins(3u) && !strcmp(pstk_arm_why,"pins_players:d898c0"));image[0xd898c0+3]^=1;vpl_pins_checked=0;
    stats.issued=1;assert(!pnat_arm_stock(1,3u) && !strcmp(pstk_arm_why,"after_first_tick"));stats.issued=0;
    boot_hold_restore();assert(!boot_hold_installed);boot_hold_armed=0;boot_hold_restored=0;boot_hold_stage="off";InterlockedExchange(&boot_hold_active,0);
    printf("pilot stock slots arming order on the pinned image verified (boot hold installed first, named refusals)\n");
    test_pslot_reset();
    pnat_armed=0;pnat_mode=0;pstk_capturing=0;pstk_slot=0;pilot_stock_slots=0;pstk_slot_mask=0;pstk_capture_slot=0;pnat_stage="off";pstk_arm_refuse("-");
    pilot_armed=0;pilot_suppress=0;pilot_source=0;pilot_slot_count=0;pilot_delay=0;pilot_start=pilot_stop=0;
    pnat_pins_checked=pstk_pins_checked=pslot_pins_checked=vps_pins_checked=vpl_pins_checked=0;
    if(pslot_page) {VirtualFree(pslot_page,0,MEM_RELEASE);pslot_page=NULL;}
    VirtualFree(thunk,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    image=prior;thunk=prior_thunk;pnat_base=prior_base;pnat_fn=saved_fns;pstk_fn=saved_pstk;pslot_fn=saved_slots;pview_fn=saved_view;
    vps_create=saved_create;vps_rng_word=saved_rng;boot_mode=saved_boot;boot_phase=saved_phase;ui_thread=saved_ui_thread;
    stats.issued=saved_issued;stats.slot=saved_slot;stats.active=saved_active;memcpy(pstk_handler_ptrs,saved_ptrs,sizeof(saved_ptrs));
}
static void test_pilot_slots_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_SECTION_HEADER sections[32];
    assert(fread(&dos,1,sizeof(dos),f)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && !fseek(f,dos.e_lfanew,SEEK_SET));
    assert(fread(&nt,1,sizeof(nt),f)==sizeof(nt) && nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.FileHeader.NumberOfSections<=32);
    unsigned count=nt.FileHeader.NumberOfSections;
    assert(fread(sections,sizeof(*sections),count,f)==count);
    static unsigned char bytes[8192];
    /* Static facts of the frame the module relies on: all of UniverseFrameUpdate as analysed (tree, then the
     * only d95180 call, then physics), U::ShipSmall's take and release control. */
    assert(test_pnat_file_bytes(f,sections,count,0x9fdef0,3323,bytes) && hash_ok(bytes,3323,"ceea80a071d72ff5c47433884c1e64df2a570913077d631b2775adf989a93e93"));
    {   uint64_t take,release;assert(test_pnat_file_bytes(f,sections,count,0x2b7feb8+8*PSLOT_TAKE,8,bytes));memcpy(&take,bytes,8);
        assert(test_pnat_file_bytes(f,sections,count,0x2b7feb8+8*PSLOT_RELEASE,8,bytes));memcpy(&release,bytes,8);
        assert(take==0x140000000ULL+PSLOT_TAKE_RVA && release==0x140000000ULL+PSLOT_RELEASE_RVA);}
    for(unsigned i=0;i<PSLOT_PIN_COUNT;i++) {
        assert(pslot_pin_table[i].size<=sizeof(bytes) && test_pnat_file_bytes(f,sections,count,pslot_pin_table[i].rva,pslot_pin_table[i].size,bytes));
        assert(hash_ok(bytes,pslot_pin_table[i].size,pslot_pin_table[i].sha));
    }
    /* The call site in one 16-byte block, the only d95180 call; slot 1's controller in rdi, game time in xmm1. */
    assert(((PSLOT_CALL_SITE+4)&~15u)==(PSLOT_CALL_SITE&~15u));
    assert(test_pnat_file_bytes(f,sections,count,PSLOT_CALL_SITE,5,bytes) && !memcmp(bytes,pslot_call_bytes,5));
    {int32_t rel;memcpy(&rel,bytes+1,4);assert(PSLOT_CALL_SITE+5u+(unsigned)rel==PSLOT_UPDATE_RVA);}
    /* 6/7/10: the prologue copies, the skipped global test (cmp [0x2f4b7d8],0; jne) and the continuation's
     * first instruction (the controller's component +0x28, then its +0xd1). */
    for(unsigned i=0;i<3;i++) {
        const unsigned b=pslot_branches[i].a+pslot_branches[i].a_len;int32_t rel;
        assert(test_pnat_file_bytes(f,sections,count,pslot_branches[i].a,pslot_branches[i].a_len,bytes) &&
               !memcmp(bytes,pslot_branches[i].pa,pslot_branches[i].a_len));
        assert(test_pnat_file_bytes(f,sections,count,pslot_branches[i].b,pslot_branches[i].b_len,bytes) &&
               !memcmp(bytes,pslot_branches[i].pb,pslot_branches[i].b_len));
        assert(test_pnat_file_bytes(f,sections,count,b,8,bytes) && bytes[0]==0x48 && bytes[1]==0x83 && bytes[2]==0x3d && !bytes[7]);
        memcpy(&rel,bytes+3,4);assert(b+8u+(unsigned)rel==0x2f4b7d8u && pslot_branches[i].b==b+8u);
        unsigned jne=pslot_branches[i].b+pslot_branches[i].b_len;
        assert(test_pnat_file_bytes(f,sections,count,jne,2,bytes) && (bytes[0]==0x75 || (bytes[0]==0x0f && bytes[1]==0x85)));
        assert(test_pnat_file_bytes(f,sections,count,pslot_branches[i].resume,4,bytes) && bytes[0]==0x48 && bytes[1]==0x8b && bytes[3]==0x28);
    }
    /* The PlayerController vtable: 71 entries, the wrapped and routed ones where the module expects them. */
    static const unsigned slots[][2]={{6,0xd8a680},{7,0xd8b3e0},{10,0xd8b790},{33,0xd97e80},{34,0xd98e50},{35,0xd9cdb0},{36,0xd98de0},{49,0xd9bf70}};
    assert(test_pnat_file_bytes(f,sections,count,PSLOT_VTABLE,8*(PSLOT_VT_SLOTS+1),bytes));
    for(unsigned i=0;i<sizeof(slots)/sizeof(*slots);i++) {uint64_t v;memcpy(&v,bytes+8*slots[i][0],8);assert(v==0x140000000ULL+slots[i][1]);}
    for(unsigned i=0;i<PSLOT_VT_SLOTS;i++) {uint64_t v;memcpy(&v,bytes+8*i,8);assert(v>0x140001000ULL && v<0x141c00000ULL);}
    {uint64_t v;memcpy(&v,bytes+8*PSLOT_VT_SLOTS,8);assert(v<0x140001000ULL || v>=0x141c00000ULL);}
    fclose(f);
    printf("pinned native image pilot stock slots verified (%u functions, call site, 6/7/10 branches, PlayerController vtable)\n",
           (unsigned)PSLOT_PIN_COUNT);
    test_pilot_slots_arming_order(path);
}
