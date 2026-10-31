/* native_gate_region_camera / _region_fields / _mass_traffic_camera: stand-in
 * camera layout, slots and stubs, group arming, lazy install, per-frame update,
 * replacements, checks, stop, the pinned build. */
static __declspec(align(16)) unsigned char test_rc_object[0x100],test_rc_zone[0x100],test_rc_sector[0x100];
static void *test_rc_vtable[0x11e8/8+1];
static char test_rc_is_class(void *object,int klass) {
    return (klass==0x6d && object==test_rc_zone) || ((klass==0xf || klass==0x58) && object==test_rc_sector);
}
static unsigned char *test_rc_ship,*test_rc_carrier,*test_rc_holder;static int test_rc_carrier_is_ship=1;
/* Policy world: ship_a in zone_a in sector_a, ship_b in zone_b in sector_b (both in one cluster). */
static __declspec(align(16)) unsigned char test_rcp_ship_a[0x200],test_rcp_ship_b[0x200],test_rcp_zone_a[0x200],test_rcp_zone_b[0x200],
    test_rcp_sector_a[0x200],test_rcp_sector_b[0x200],test_rcp_cluster[0x200],test_rcp_part_a[0x200],test_rcp_part_b[0x200],test_rcp_traffic[0x300];
static void *test_rcp_vtable[0x11e8/8+1];
static void *test_rcp_extra_sector;
static char test_rcp_class(void *o,int klass) {
    if(klass==0x58) return o==test_rcp_sector_a || o==test_rcp_sector_b || (o && o==test_rcp_extra_sector);
    if(klass==0x6d) return o==test_rcp_zone_a || o==test_rcp_zone_b;
    if(klass==0xf) return o==test_rcp_cluster;
    return 0;
}
static void test_rcp_world(void) {
    unsigned char *all[]={test_rcp_ship_a,test_rcp_ship_b,test_rcp_zone_a,test_rcp_zone_b,test_rcp_sector_a,test_rcp_sector_b,test_rcp_cluster,test_rcp_part_a,test_rcp_part_b};
    test_rcp_vtable[0x11e8/8]=(void*)test_rcp_class;void *vt=test_rcp_vtable;
    for(unsigned i=0;i<sizeof(all)/sizeof(*all);i++) {memset(all[i],0,0x200);memcpy(all[i],&vt,8);}
    struct {unsigned char *child,*parent;} links[]={{test_rcp_ship_a,test_rcp_zone_a},{test_rcp_ship_b,test_rcp_zone_b},{test_rcp_part_a,test_rcp_zone_a},
        {test_rcp_part_b,test_rcp_zone_b},{test_rcp_zone_a,test_rcp_sector_a},{test_rcp_zone_b,test_rcp_sector_b},{test_rcp_sector_a,test_rcp_cluster},
        {test_rcp_sector_b,test_rcp_cluster}};
    for(unsigned i=0;i<sizeof(links)/sizeof(*links);i++) memcpy(links[i].child+0x70,&links[i].parent,8);
    *(uint64_t*)(test_rcp_ship_a+8)=0x5ca81;*(uint64_t*)(test_rcp_ship_b+8)=0x5ca82;
    memset(test_rcp_traffic,0,sizeof(test_rcp_traffic));void *zb=test_rcp_zone_b;memcpy(test_rcp_traffic+0x100,&zb,8); /* +0x108 empty: +0x100 */
}
/* Calls a policy thunk as its site does: r12 and rdi hold the object, the volatile registers and xmm0..xmm5
 * hold sentinels; stores rax, rcx, rdx, r8..r11 and xmm0..xmm5 after the thunk into out[0..6] and out+0x40. */
static unsigned char *test_rcp_harness_code;
static uint64_t test_rcp_out[20];
static void test_rcp_harness(void *thunk) {
    unsigned char *p=test_rcp_harness_code;unsigned n=0;uint64_t v;void *out=test_rcp_out;
#define EMIT(...) do {static const unsigned char b_[]={__VA_ARGS__};memcpy(p+n,b_,sizeof(b_));n+=sizeof(b_);} while(0)
#define IMM(x) do {v=(uint64_t)(x);memcpy(p+n,&v,8);n+=8;} while(0)
    EMIT(0x41,0x54,0x57,0x48,0x83,0xec,0x08,0x49,0x89,0xcc,0x48,0x89,0xcf);             /* push r12; push rdi; sub rsp,8; mov r12,rcx; mov rdi,rcx */
    EMIT(0x48,0xba);IMM(0x1111111111111111ULL);EMIT(0x49,0xb8);IMM(0x2222222222222222ULL);  /* mov rdx/r8, sentinel */
    EMIT(0x49,0xb9);IMM(0x3333333333333333ULL);EMIT(0x49,0xba);IMM(0x4444444444444444ULL);  /* r9, r10 */
    EMIT(0x49,0xbb);IMM(0x5555555555555555ULL);                                                /* r11 */
    static const unsigned char movq[6]={0xc0,0xc8,0xd0,0xd8,0xe0,0xe8};
    for(unsigned x=0;x<6;x++) {EMIT(0x48,0xb8);IMM(0xa0a0a0a000000000ULL+x);p[n++]=0x66;p[n++]=0x48;p[n++]=0x0f;p[n++]=0x6e;p[n++]=movq[x];}
    EMIT(0x48,0xb8);IMM(thunk);EMIT(0xff,0xd0);                                                /* mov rax, thunk; call rax */
    EMIT(0x50,0x48,0xb8);IMM(out);                                                             /* push rax; mov rax, out */
    EMIT(0x48,0x89,0x48,0x08,0x48,0x89,0x50,0x10,0x4c,0x89,0x40,0x18,0x4c,0x89,0x48,0x20,0x4c,0x89,0x50,0x28,0x4c,0x89,0x58,0x30);
    EMIT(0xf3,0x0f,0x7f,0x40,0x40,0xf3,0x0f,0x7f,0x48,0x50,0xf3,0x0f,0x7f,0x50,0x60,0xf3,0x0f,0x7f,0x58,0x70);
    EMIT(0xf3,0x0f,0x7f,0xa0,0x80,0x00,0x00,0x00,0xf3,0x0f,0x7f,0xa8,0x90,0x00,0x00,0x00);
    EMIT(0x59,0x48,0x89,0x08,0x48,0x83,0xc4,0x08,0x5f,0x41,0x5c,0xc3);                        /* pop rcx; mov [rax],rcx; add rsp,8; pop rdi; pop r12; ret */
#undef EMIT
#undef IMM
    assert(n<0x200);FlushInstructionCache(GetCurrentProcess(),p,n);
}
/* Stand-ins for GetTransformRelativeTo 0x3db8a0 and the two view-cache helpers behind the policy's position stubs. */
static void *test_rcp_gtr_self[8],*test_rcp_gtr_ancestor[8];static double test_rcp_gtr_time[8];static unsigned test_rcp_gtr_calls,test_rcp_native_calls;
/* As the engine's: composes each object's local transform into the matrix it is given (here
 * translations only: ship_b (5,6,7) in zone_b, zone_b (60,0,0) in sector_b, sector_b (40,20,3) in the
 * cluster) up the +0x70 chain to the ancestor; asserts the matrix it is given is the identity PosRot. */
static float test_rcp_local(const void *o,unsigned axis) {
    static const float ship[3]={5,6,7},zone[3]={60,0,0},sector[3]={40,20,3};
    return o==test_rcp_ship_b?ship[axis]:o==test_rcp_zone_b?zone[axis]:o==test_rcp_sector_b?sector[axis]:0;
}
static void test_rcp_gtr(void *self,float *out,double time,void *ancestor,unsigned char flag) {
    (void)flag;if(test_rcp_gtr_calls<8) {test_rcp_gtr_self[test_rcp_gtr_calls]=self;test_rcp_gtr_ancestor[test_rcp_gtr_calls]=ancestor;test_rcp_gtr_time[test_rcp_gtr_calls]=time;}
    ++test_rcp_gtr_calls;
    static const float identity[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};assert(!memcmp(out,identity,sizeof(identity)));
    for(unsigned char *p=self;p && p!=ancestor;p=*(unsigned char**)(p+0x70)) for(unsigned a=0;a<3;a++) out[a]+=test_rcp_local(p,a);
}
static void test_rcp_zone_native(void *component,float *out,void *camera) {(void)component;(void)camera;++test_rcp_native_calls;out[0]=-1;out[1]=-2;out[2]=-3;}
static void *test_rcp_lane_native(void *camera,float *out,void *zone) {(void)camera;(void)zone;++test_rcp_native_calls;out[0]=-4;return out;}
/* Stand-ins for the traffic switch-on 0x8aacb0 and the build's collection 0x8afaf0. */
static void test_rcp_activate(void *traffic) {((unsigned char*)traffic)[0x1d0]=1;((unsigned char*)traffic)[0x228]=0;}
static void *test_rcp_collect_items[3];
static void *test_rcp_collect(void *traffic,void **vector) {(void)traffic;vector[0]=test_rcp_collect_items;vector[1]=test_rcp_collect_items+3;return vector;}
/* Shape refresh stand-ins: the macro getter 0x7ad890, Random::FloatState, the native interval and refresh, a compound
 * shape whose slot 3 (0x18) returns the bounds test_shape_box (min xyz, pad, max xyz, pad). */
static unsigned char test_shape_macro_data[0x300];
static unsigned char *test_shape_macro(void *macro) {(void)macro;return test_shape_macro_data;}
static void *test_shape_rand_state;static float test_shape_rand_range;static unsigned test_shape_rand_calls;
static uint64_t test_shape_rand_word;
static float test_shape_rand(void *state,float range) {test_shape_rand_state=state;test_shape_rand_word=*(uint64_t*)state;test_shape_rand_range=range;++test_shape_rand_calls;return 0.25f;}
/* sound_rng stand-ins: FloatState, and the choice drawing from the thread's word. */
static uint64_t test_sound_float_word;static void *test_sound_float_state;static float test_sound_float_range;
static float test_sound_float(void *state,float range) {test_sound_float_state=state;test_sound_float_word=*(uint64_t*)state;test_sound_float_range=range;return 0.5f;}
static uint64_t test_sound_seen,test_sound_args[4];static int test_sound_throw;
static uint64_t test_sound_choice(void *a,void *b,void *c,void *d) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);uint64_t *word=(uint64_t*)((unsigned char*)tls[0]+0x318);
    test_sound_seen=*word;*word=*word*3u+1u;
    test_sound_args[0]=(uint64_t)(uintptr_t)a;test_sound_args[1]=(uint64_t)(uintptr_t)b;test_sound_args[2]=(uint64_t)(uintptr_t)c;test_sound_args[3]=(uint64_t)(uintptr_t)d;
    if(test_sound_throw) RaiseException(0xe0534e44,0,0,NULL);
    return 0x1234;
}
static void test_sound_choice_throw(uint64_t (*stub)(void *,void *,void *,void *)) {
    __try {stub(NULL,NULL,NULL,NULL);}
    __except(GetExceptionCode()==0xe0534e44?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
static unsigned test_shape_native_calls;
static float test_shape_interval_native(void *holder,void *owner) {(void)holder;(void)owner;++test_shape_native_calls;return 9.0f;}
static void *test_shape_update_args[3];static unsigned test_shape_update_calls;
static char test_shape_update_native(void *holder,void *owner,unsigned char force) {
    test_shape_update_args[0]=holder;test_shape_update_args[1]=owner;test_shape_update_args[2]=(void*)(uintptr_t)force;++test_shape_update_calls;return 1;
}
static float test_shape_box[8];
static float *test_shape_bounds(void *shape,float *out) {(void)shape;memcpy(out,test_shape_box,sizeof(test_shape_box));return out;}
static void *test_shape_vtable[4];
static __declspec(align(16)) unsigned char test_shape_holder[0x40],test_shape_compound[0x10],test_shape_shape[0x20];
static void test_shape_world(void) {
    test_shape_vtable[3]=(void*)test_shape_bounds;void *vt=test_shape_vtable;memset(test_shape_shape,0,sizeof(test_shape_shape));memcpy(test_shape_shape,&vt,8);
    void *s=test_shape_shape;memset(test_shape_compound,0,sizeof(test_shape_compound));memcpy(test_shape_compound+8,&s,8);
    void *c=test_shape_compound;memset(test_shape_holder,0,sizeof(test_shape_holder));memcpy(test_shape_holder+0x10,&c,8);
    const float box[8]={0,0,0,0, 1,2,2,0};memcpy(test_shape_box,box,sizeof(box));memset(test_shape_macro_data,0,sizeof(test_shape_macro_data));
}
/* Stand-ins behind the XS detail and camera probe stubs. */
static char test_xs_external(void) {return 0;}
static unsigned test_vpl_slots;static uint64_t test_vpl_targets[3];static int test_vpl_external_flags[3];static void *test_vpl_ships[3];
static unsigned test_vpl_count(void) {return test_vpl_slots;}
static uint64_t test_vpl_target(unsigned k) {return k>=1 && k<=2?test_vpl_targets[k]:0;}
static int test_vpl_external(unsigned k) {return k>=1 && k<=2?test_vpl_external_flags[k]:0;}
static void *test_vpl_ship(unsigned k) {return k>=1 && k<=2?test_vpl_ships[k]:NULL;}
static void **test_xs_target(void *ship,void **out) {(void)ship;*out=(void*)0x77;return out;}
static float test_xs_distance(void *ship) {(void)ship;return 7.0f;}
static float test_probe_value;static double test_probe_time;
static float test_probe_distance(void *object,void *camera) {(void)object;(void)camera;return test_probe_value;}
static void **test_probe_walk(void *unused,void **out) {(void)unused;return out;}
static void *test_probe_vector(void *a,float *out,void *camera) {(void)a;(void)camera;return out;}
static void *test_probe_offset(void *camera,float *out,double time,void *frame) {(void)camera;(void)frame;test_probe_time=time;return out;}
/* Stand-ins behind the highway rows stubs: GetOffset writes a translation; 0x5f7a50 stores the parameter and a spline position. */
static double test_hw_time;static float test_hw_t;
static void *test_hw_offset(void *camera,float *out,double time,void *highway) {
    (void)camera;(void)highway;test_hw_time=time;out[0]=100.0f;out[1]=-2.0f;out[2]=3.0f;out[3]=0.0f;return out;
}
static void test_hw_set(void *highway,float t) {
    test_hw_t=t;unsigned char *p=highway;const float pos[3]={1.0f,2.0f,3.0f};memcpy(p+0x560,&t,4);memcpy(p+0x570,pos,12);
}
/* Stands in for Camera 0x97dff0 behind SetParent's context call. */
static void *test_rc_context_args[3];static unsigned test_rc_context_hits;
static void test_rc_context_native(void *camera,void *old_parent,void *new_parent) {
    test_rc_context_args[0]=camera;test_rc_context_args[1]=old_parent;test_rc_context_args[2]=new_parent;++test_rc_context_hits;
}
/* ship (0x75) -> holder (neither) -> carrier (0x6f, and 0x75 unless switched off). */
static char test_rc_controllable_class(void *object,int klass) {
    if(klass==0x75) return object==test_rc_ship || (object==test_rc_carrier && test_rc_carrier_is_ship);
    if(klass==0x6f) return object==test_rc_carrier;
    return 0;
}
static void test_rc_write_original_sites(void) {
    *(void**)(image+VSC_ZONE_SLOT)=image+VSC_ZONE;*(void**)(image+VSC_HIGHWAY_SLOT)=image+VSC_HIGHWAY;
    for(unsigned i=0;i<RC_SITES;i++) {
        unsigned char *p=image+rc_sites[i].rva;memcpy(p,rc_sites[i].prefix,3);
        int32_t d=rc_disp_to(rc_sites[i].rva,image+rc_site_target(i));memcpy(p+3,&d,4);
    }
    for(unsigned i=0;i<RC_CALLS;i++) {
        unsigned char *p=image+rc_calls[i].rva;p[0]=0xe8;int32_t d=rc_rel_to(rc_calls[i].rva,image+rc_calls[i].target);memcpy(p+1,&d,4);
    }
    for(unsigned i=0;i<RC_JUMPS;i++) memcpy(image+rc_jumps[i].rva,rc_jumps[i].original,7);
}
static void test_rc_clear_sites(void) {
    for(unsigned i=0;i<RC_SITES;i++) memset(image+rc_sites[i].rva,0,7);
    for(unsigned i=0;i<RC_CALLS;i++) memset(image+rc_calls[i].rva,0,5);
    for(unsigned i=0;i<RC_JUMPS;i++) memset(image+rc_jumps[i].rva,0,7);
}
static void test_rc_reset(void) {
    rc_mode=RC_OFF;rc_installed=rc_restored=rc_patched=0;rc_armed_groups=rc_installed_groups=0;
    memset(rc_site_done,0,sizeof(rc_site_done));memset(rc_call_done,0,sizeof(rc_call_done));
    rc_next=0;rc_updates=rc_native_frames=rc_viewpoint_id=rc_viewpoint_key=rc_install_tick=rc_install_waits=0;rc_tick=UINT64_MAX;rc_failures=0;rc_stage="idle";
    rc_view_zone=rc_view_sector=NULL;for(unsigned i=0;i<RC_CALLS;i++) rc_stub_calls[i]=0;
    rc_context_skipped=0;rc_context_native=NULL;
}
/* A fresh page and stub page as rc_arm prepares them (the plain image has no pinned code). */
static void test_rc_pages(void) {
    if(!rc_page) rc_page=rc_alloc_near(RC_PAGE_SIZE,PAGE_READWRITE);
    if(!rc_stubs) rc_stubs=rc_alloc_near(RC_STUB_SIZE,PAGE_EXECUTE_READ);
    assert(rc_page && rc_stubs);
    memset(rc_page,0,RC_PAGE_SIZE);void *manager=rc_page+RC_MANAGER,*shadow=rc_page+RC_SHADOW,*manager2=rc_page+RC_MANAGER2;
    memcpy(rc_page,&manager,8);memcpy(rc_page+8,&shadow,8);memcpy(rc_page+16,&manager2,8);assert(rc_write_stubs() && rc_stubs_owned());
}
static void test_region_camera(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_view=view_installed,saved_region=mt_region.mode,saved_traffic=mt_traffic.mode;unsigned saved_latched=view_latched,saved_count=view_count;
    int saved_mt=mt_installed;LONG64 saved_waits=mt_worker_waits,saved_wakes=mt_worker_wakes;
    void *saved_point=view_points[0];uint64_t saved_id=view_ids[0];
    void *saved_universe=*(void**)(image+RC_UNIVERSE);
    /* Layout as 0x979ef0 and Camera::GetOffset 0x97a300 read it. */
    static __declspec(align(16)) unsigned char camera[RC_CAMERA_SIZE];void *object=test_rc_object;
    memset(camera,0xcc,sizeof(camera));memset(camera,0,sizeof(camera));rc_write_camera(camera,object);
    for(unsigned b=0;b<2;b++) {
        unsigned char *bank=camera+b*RC_BANK;void *parent,*key;uint64_t capacity,count;float rows[12],translation[4];
        memcpy(&parent,bank+0x30,8);memcpy(&key,bank+0x40,8);memcpy(&capacity,bank+0x2c0,8);memcpy(&count,bank+0x2d0,8);
        memcpy(translation,bank+0x50,16);memcpy(rows,bank+0x60,48);
        assert(parent==object && key==object && capacity==8 && count==1);
        assert(!translation[0] && !translation[1] && !translation[2] && !translation[3]);
        assert(rows[0]==1 && rows[5]==1 && rows[10]==1 && !rows[1] && !rows[2] && !rows[4] && !rows[6] && !rows[8] && !rows[9] && !rows[3] && !rows[7] && !rows[11]);
        assert(((uintptr_t)(bank+0x60)&15u)==0 && ((uintptr_t)(bank+0x50)&15u)==0); /* mulps/addps with memory operands */
    }
    void *anchor,*second;int32_t mode;float local[16];
    memcpy(&anchor,camera+0x770,8);memcpy(&second,camera+0x778,8);memcpy(&mode,camera+0x868,4);memcpy(local,camera+0x590,64);
    assert(anchor==object && second==object && mode==1);
    assert(!local[0] && !local[1] && !local[2] && !local[3] && local[4]==1 && local[9]==1 && local[14]==1 && !local[5] && !local[8] && !local[13]);
    assert(1*RC_BANK+0x2d0+8<=0x590 && 0x868+4<=RC_CAMERA_SIZE);
    /* Ancestors through the class test, the object itself first. */
    void *vt=test_rc_vtable;test_rc_vtable[0x11e8/8]=(void*)test_rc_is_class;
    memset(test_rc_object,0,sizeof(test_rc_object));memset(test_rc_zone,0,sizeof(test_rc_zone));memset(test_rc_sector,0,sizeof(test_rc_sector));
    memcpy(test_rc_object,&vt,8);memcpy(test_rc_zone,&vt,8);memcpy(test_rc_sector,&vt,8);
    void *z=test_rc_zone,*s=test_rc_sector;memcpy(test_rc_object+0x70,&z,8);memcpy(test_rc_zone+0x70,&s,8);
    assert(rc_ancestor(test_rc_object,0x6d)==test_rc_zone && rc_ancestor(test_rc_object,0xf)==test_rc_sector && !rc_ancestor(test_rc_object,0x33));
    assert(rc_ancestor(test_rc_zone,0x6d)==test_rc_zone && !rc_ancestor(NULL,0x6d));
    /* Sites and calls: original -> ours -> original by CAS; anything else is foreign. */
    test_rc_reset();test_rc_write_original_sites();unsigned char *saved_page=rc_page,*saved_stubs=rc_stubs;test_rc_pages();
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==0);
    for(unsigned g=0;g<RC_GROUPS;g++) assert(rc_install_group(g));
    assert(rc_installed_groups==(1u<<RC_GROUPS)-1 && rc_patched);
    for(unsigned i=0;i<RC_SITES;i++) {
        assert(rc_site_state(i)==1);int32_t d;memcpy(&d,image+rc_sites[i].rva+3,4);
        assert(image+rc_sites[i].rva+7+d==rc_page+8u*rc_sites[i].slot);
    }
    for(unsigned i=0;i<RC_CALLS;i++) {assert(rc_call_state(i)==1);int32_t d;memcpy(&d,image+rc_calls[i].rva+1,4);assert(image+rc_calls[i].rva+5+d==rc_stubs+0x20u*i);}
    /* The mining jump: jmp rel32 to its thunk, 66 90; the thunk calls rc_mining_near and resumes at 0x788720. */
    for(unsigned i=0;i<RC_JUMPS;i++) {
        assert(rc_jump_state(i)==1 && rc_jump_done[i]);unsigned char *at=image+rc_jumps[i].rva;int32_t d;memcpy(&d,at+1,4);
        assert(at[0]==0xe9 && at+5+d==rc_jump_stub(i) && at[5]==0x66 && at[6]==0x90);
    }
    {
        unsigned char *stub=rc_jump_stub(0);void *fn,*resume;
        assert(!memcmp(stub,"\x48\x83\xec\x20\x48\x89\xf9\x4c\x89\xea\x49\x89\xf0\x48\xb8",15));
        memcpy(&fn,stub+15,8);assert(fn==(void*)rc_mining_near);
        assert(!memcmp(stub+23,"\xff\xd0\x48\x83\xc4\x20\x0f\xb6\xd8\x41\xbc\x02\x00\x00\x00\x41\xbe\x17\x00\x00\x00"
                               "\x44\x89\xb5\xe8\x01\x00\x00\x48\xc7\x44\x24\x60\x00\x00\x00\x00\xff\x25\x00\x00\x00\x00",43));
        memcpy(&resume,stub+66,8);assert(resume==image+0x788720u && RC_JUMP_STUBS+0x80u*RC_JUMPS<=RC_STUB_SIZE);
        assert(RC_CALLS*0x20u<=RC_THUNKS && RC_THUNKS+RC_THUNK_SITES*RC_THUNK_SIZE<=RC_JUMP_STUBS);
    }
    assert(rc_stubs[0]==0xff && rc_stubs[1]==0x25 && *(void**)(rc_stubs+6)==(void*)rc_camera_zone && *(void**)(rc_stubs+0x29)==(void*)rc_camera_sector &&
           *(void**)(rc_stubs+0x49)==(void*)rc_active_camera && *(void**)(rc_stubs+0x66)==(void*)rc_effect_creator);
    /* Stubs 1 and 2 first move the policy's object (r12) into a free volatile register. */
    assert(!memcmp(rc_stubs+0x20,"\x4d\x89\xe0\xff\x25\x00\x00\x00\x00",9) && !memcmp(rc_stubs+0x40,"\x4c\x89\xe1\xff\x25\x00\x00\x00\x00",9));
    assert(rc_owned());
    *rc_manager_camera()=rc_page+0x10;assert(!rc_owned());*rc_manager_camera()=NULL;assert(rc_owned());
    void *wrong=rc_page;memcpy(rc_page+8,&wrong,8);assert(!rc_owned());void *shadow=rc_page+RC_SHADOW;memcpy(rc_page+8,&shadow,8);
    image[rc_sites[5].rva+1]^=0x40;assert(rc_site_state(5)<0 && !rc_owned());
    assert(!rc_remove_patches() && rc_site_done[5] && !rc_site_done[0] && !rc_call_done[0]);
    image[rc_sites[5].rva+1]^=0x40;
    image[rc_calls[1].rva]=0xe9;assert(rc_call_state(1)<0);image[rc_calls[1].rva]=0xe8; /* call already restored */
    assert(rc_remove_patches() && !rc_installed_groups);
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==0);
    for(unsigned i=0;i<RC_JUMPS;i++) assert(rc_jump_state(i)==0 && !rc_jump_done[i]);
    rc_patched=0;
    /* Under the policy the ten object sites take the thunk form (call rel32 to their thunk, 66 90). */
    {
        unsigned thunks=0;rc_policy=1;
        for(unsigned g=0;g<RC_GROUPS;g++) assert(rc_install_group(g));
        for(unsigned i=0;i<RC_SITES;i++) {
            if(!rc_sites[i].object) {assert(rc_site_state(i)==1 && rc_site_ours(i)==1);continue;}
            ++thunks;assert(rc_site_state(i)==2 && rc_site_ours(i)==2);unsigned char *at=image+rc_sites[i].rva;int32_t d;memcpy(&d,at+1,4);
            assert(at[0]==0xe8 && at+5+d==rc_thunk(i) && at[5]==0x66 && at[6]==0x90);
        }
        assert(thunks==RC_THUNK_SITES && rc_thunk(RC_SITES-1)==NULL && rc_owned());
        unsigned site=0;while(!rc_sites[site].object) ++site;
        image[rc_sites[site].rva+6]=0x91;assert(rc_site_state(site)<0 && !rc_owned());image[rc_sites[site].rva+6]=0x90;assert(rc_owned());
        DWORD prot;assert(VirtualProtect(rc_stubs,RC_STUB_SIZE,PAGE_EXECUTE_READWRITE,&prot));
        rc_thunk(site)[0x10]^=1;assert(!rc_stubs_owned());rc_thunk(site)[0x10]^=1;assert(rc_stubs_owned());
        assert(VirtualProtect(rc_stubs,RC_STUB_SIZE,prot,&prot));
        assert(rc_remove_patches() && !rc_installed_groups);
        for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
        rc_policy=0;rc_patched=0;
    }
    /* Arming refusals (the plain image has no pinned code). */
    test_rc_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    view_installed=1;mt_region.mode=MT_SYNC;mt_traffic.mode=MT_SYNC;mt_installed=1;
    assert(!rc_arm(-1) && !rc_arm(RC_GROUPS));
    mt_installed=0;assert(!rc_arm(RC_FIELDS) && !strcmp(rc_stage,"needs"));mt_installed=1; /* the install needs the pass join */
    stats.active=0;assert(!rc_arm(RC_REGION));stats.active=1;
    stats.issued=1;assert(!rc_arm(RC_REGION));stats.issued=0;
    boot_phase=1;assert(!rc_arm(RC_FIELDS));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!rc_arm(RC_TRAFFIC));ui_thread=GetCurrentThreadId();
    view_installed=0;assert(!rc_arm(RC_FIELDS) && !strcmp(rc_stage,"needs"));view_installed=1;
    mt_region.mode=MT_OBSERVE;assert(!rc_arm(RC_REGION) && !strcmp(rc_stage,"needs"));mt_region.mode=MT_SYNC;
    mt_traffic.mode=MT_OFF;assert(!rc_arm(RC_TRAFFIC) && !strcmp(rc_stage,"needs"));mt_traffic.mode=MT_SYNC;
    for(unsigned g=0;g<RC_GROUPS;g++) assert(!rc_arm((int)g) && !strcmp(rc_stage,g==RC_POLICY || g==RC_VIEW_SCENES || g==RC_FIELD_FILL || g==RC_TRAFFIC_PASS?"needs":"pins") && !rc_installed);
    /* The policy needs the traffic or the highway group armed first. */
    rc_armed_groups=1u<<RC_HIGHWAY;assert(!rc_arm(RC_POLICY) && !strcmp(rc_stage,"pins"));rc_armed_groups=0;
    for(unsigned i=0;i<RC_SITES;i++) {
        rc_armed_groups=rc_sites[i].group==RC_FIELD_FILL?1u<<RC_FIELDS:0; /* the fill arms after the fields group */
        image[rc_sites[i].rva+3]^=1;assert(!rc_arm(rc_sites[i].group) && !strcmp(rc_stage,"site"));image[rc_sites[i].rva+3]^=1;
    }
    for(unsigned i=0;i<RC_CALLS;i++) {
        rc_armed_groups=rc_calls[i].group==RC_POLICY?1u<<RC_HIGHWAY:rc_calls[i].group==RC_VIEW_SCENES?1u<<RC_SCENES:
                        rc_calls[i].group==RC_FIELD_FILL?1u<<RC_FIELDS:rc_calls[i].group==RC_TRAFFIC_PASS?1u<<RC_TRAFFIC_ROWS:0; /* the policy arms after the highway or traffic group, view scenes after the scenes, the fill after the fields, the pass after the rows */
        image[rc_calls[i].rva+2]^=1;assert(!rc_arm(rc_calls[i].group) && !strcmp(rc_stage,"site"));image[rc_calls[i].rva+2]^=1;
    }
    for(unsigned i=0;i<RC_JUMPS;i++) {
        rc_armed_groups=rc_jumps[i].group==RC_TRAFFIC_PASS?1u<<RC_TRAFFIC_ROWS:0;image[rc_jumps[i].rva+3]^=1;assert(!rc_arm(rc_jumps[i].group) && !strcmp(rc_stage,"site"));image[rc_jumps[i].rva+3]^=1;
    }
    rc_armed_groups=0;
    /* Frames: values first, then the lazy install of every armed group (fields and traffic here). */
    world_identity_entry *prior_identities=world_identities;unsigned prior_identity_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0x5ca73,0x176a99));
    static __declspec(align(16)) unsigned char universe[0x400];void *live_player=(void*)0x1111,*live_camera=(void*)0x2222;
    memset(universe,0,sizeof(universe));memcpy(universe+0x230,&live_player,8);memcpy(universe+0x3d0,&live_camera,8);
    *(void**)(image+RC_UNIVERSE)=universe;
    test_rc_reset();test_rc_pages();rc_installed=1;rc_mode=RC_VIEW;rc_armed_groups=(1u<<RC_FIELDS)|(1u<<RC_TRAFFIC);
    *(uint64_t*)(test_rc_object+8)=0x5ca73;
    view_latched=1;view_count=1;view_points[0]=object;view_ids[0]=0x5ca73;
    mt_worker_waits=1;mt_worker_wakes=0; /* the MoveWorker is parked at its frame start */
    assert(rc_join_point(6) && !rc_installed_groups && !rc_install_waits); /* no stand-in yet: nothing to install */
    stats.issued=7;assert(rc_frame(7) && !rc_installed_groups && !rc_patched); /* slot 6 only sets the stand-in */
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    assert(rc_join_point(7) && !strcmp(rc_stage,"installed") && rc_install_tick==7 && !rc_install_waits);
    assert(rc_installed_groups==((1u<<RC_FIELDS)|(1u<<RC_TRAFFIC)) && rc_patched);
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==(rc_sites[i].group==RC_FIELDS || rc_sites[i].group==RC_TRAFFIC?1:0));
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==(rc_calls[i].group==RC_FIELDS || rc_calls[i].group==RC_TRAFFIC?1:0));
    unsigned char *a=rc_page+RC_CAMERAS,*b=a+RC_CAMERA_SIZE;void *parent;
    assert(*rc_manager_camera()==a && *rc_shadow_camera()==a && *rc_shadow_player()==object && rc_next==1 && rc_updates==1);
    assert(rc_viewpoint_id==0x5ca73 && rc_viewpoint_key==0x176a99 && rc_tick==7 && rc_view_zone==test_rc_zone && rc_view_sector==test_rc_sector);
    memcpy(&parent,a+0x30,8);assert(parent==object);memcpy(&parent,a+RC_BANK+0x30,8);assert(parent==object);memcpy(&parent,a+0x770,8);assert(parent==object);
    /* The replacements answer for the stand-in. */
    void *out=(void*)1;assert(rc_camera_zone(NULL,&out)==&out && out==test_rc_zone);
    assert(rc_camera_sector(NULL,&out,NULL)==&out && out==test_rc_sector);
    assert(rc_active_camera(NULL)==a && rc_stub_calls[0]==1 && rc_stub_calls[1]==1 && rc_stub_calls[2]==1);
    /* The controllable for UniverseFrameUpdate's highway parameter: the viewpoint's class-0x75 ancestor, then
     * class-0x75 parents of class 0x6f (a ship carried by a carrier) until a class-0x6f parent is not one. */
    {
        static __declspec(align(16)) unsigned char ship[0x100],carrier[0x100],holder[0x100];
        static void *cvt[0x11e8/8+1];cvt[0x11e8/8]=(void*)test_rc_controllable_class;void *cv=cvt;
        memset(ship,0,sizeof(ship));memset(carrier,0,sizeof(carrier));memset(holder,0,sizeof(holder));
        memcpy(ship,&cv,8);memcpy(carrier,&cv,8);memcpy(holder,&cv,8);test_rc_ship=ship;test_rc_carrier=carrier;test_rc_holder=holder;
        void *q=holder;memcpy(ship+0x70,&q,8);q=carrier;memcpy(holder+0x70,&q,8);
        unsigned char *anchor_now=NULL;memcpy(&anchor_now,a+0x770,8);void *saved_anchor=anchor_now,*sp=ship;memcpy(a+0x770,&sp,8);
        void *found=(void*)1;assert(rc_controllable_a(NULL,&found)==&found && found==carrier && rc_stub_calls[4]==1);
        test_rc_carrier_is_ship=0;found=(void*)1;assert(rc_controllable_b(NULL,&found)==&found && found==ship && rc_stub_calls[5]==1);
        test_rc_carrier_is_ship=1;memcpy(a+0x770,&saved_anchor,8);
        /* The viewpoint itself is no controllable and has none above: none. */
        found=(void*)1;assert(rc_controllable_a(NULL,&found)==&found && !found);
    }
    /* Through the executable stubs, as the patched calls reach them. */
    assert(((void *(*)(void))(void *)rc_stub(2))()==a && rc_stub_calls[2]==2);
    out=NULL;assert(((void **(*)(void *,void **))(void *)rc_stub(1))(NULL,&out)==&out && out==test_rc_sector && rc_stub_calls[1]==2);
    stats.issued=8;assert(rc_frame(8) && *rc_manager_camera()==b && *rc_shadow_camera()==b && rc_next==0);
    memcpy(&parent,b+0x30,8);assert(parent==object);
    /* The viewpoint's ID no longer matches (reused memory): no stand-in; the shadow universe holds the live player and camera. */
    *(uint64_t*)(test_rc_object+8)=0x999;stats.issued=9;
    assert(rc_frame(9) && !*rc_manager_camera() && *rc_shadow_player()==live_player && *rc_shadow_camera()==live_camera && rc_native_frames==1 && !rc_viewpoint_id && !rc_viewpoint_key);
    *(uint64_t*)(test_rc_object+8)=0x5ca73;view_count=0;stats.issued=10;assert(rc_frame(10) && !*rc_manager_camera() && rc_native_frames==2);
    view_count=1;stats.issued=11;assert(rc_frame(11) && *rc_manager_camera()==a);
    /* Guards: the admitted tick on the MoveWorker only; a failure stops at the next check. */
    assert(rc_owned());
    assert(!rc_frame(10) && rc_failures==1 && !strcmp(rc_stage,"frame_guard") && !rc_check());
    char text[3072];assert(rc_status(text,sizeof(text))>0);
    assert(strstr(text,"mode=view installed=1 frame=11 viewpoint=5ca73 key=0000000000176a99 updates=5 native_frames=2 restored=0 stage=frame_guard failures=1 "
                       "groups=fields,traffic patched=fields,traffic stub_calls=1/2/2/0/2/1 install_tick=7 install_waits=0 effects=0/0 effect_rows=0"));
    /* Viewpoint 2 (the second latched view point): the second manager's stand-in camera, its zone (0x6d) and
     * class-0x58 ancestor for the scene choice's active-camera calls; gone when its ID no longer matches. */
    {
        static __declspec(align(16)) unsigned char second[0x100];memset(second,0,sizeof(second));
        void *sv=test_rc_vtable,*zp=test_rc_zone;memcpy(second,&sv,8);memcpy(second+0x70,&zp,8);*(uint64_t*)(second+8)=0x5ca74;
        assert(!*rc_manager2_camera() && !rc_vp2_zone && !rc_vp2_sector);
        LONG64 before[RC_CALLS];for(unsigned i=0;i<RC_CALLS;i++) before[i]=rc_stub_calls[i];
        view_count=2;view_points[1]=second;view_ids[1]=0x5ca74;stats.issued=12;assert(rc_frame(12));
        unsigned char *c2=rc_page+RC_CAMERAS2;void *anchor2;memcpy(&anchor2,c2+0x770,8);
        assert(*rc_manager2_camera()==c2 && anchor2==second && rc_vp2_zone==test_rc_zone && rc_vp2_sector==test_rc_sector && rc_vp2_id==0x5ca74);
        assert(*rc_manager_camera()!=c2 && rc_owned());
        void *out=NULL;assert(rc_scene_sector6(NULL,&out)==&out && out==test_rc_sector);
        out=NULL;assert(rc_scene_zone7(NULL,&out)==&out && out==test_rc_zone);
        assert(rc_scene_active8()==c2);
        out=NULL;assert(rc_scene_sector9(NULL,&out)==&out && out==test_rc_sector);
        out=NULL;assert(rc_scene_sector10(NULL,&out)==&out && out==test_rc_sector);
        for(unsigned i=6;i<=10;i++) assert(rc_stub_calls[i]==before[i]+1);
        /* Through the executable stub, as the patched call in Zone::vfunc943 reaches it. */
        out=NULL;assert(((void **(*)(void *,void **))(void *)rc_stub(6))(NULL,&out)==&out && out==test_rc_sector);
        stats.issued=13;assert(rc_frame(13) && *rc_manager2_camera()==c2+RC_CAMERA_SIZE && rc_vp2_updates==2); /* alternates */
        *rc_manager2_camera()=rc_page+0x10;assert(!rc_owned());*rc_manager2_camera()=c2;assert(rc_owned());
        void *wrong=rc_page;memcpy(rc_page+16,&wrong,8);assert(!rc_owned());void *m2=rc_page+RC_MANAGER2;memcpy(rc_page+16,&m2,8);
        *(uint64_t*)(second+8)=0x999;stats.issued=14;assert(rc_frame(14) && !*rc_manager2_camera() && !rc_vp2_zone && !rc_vp2_id);
        view_count=1;view_points[1]=NULL;view_ids[1]=0;
        char status[1024];assert(rc_status(status,sizeof(status))>0 && strstr(status," key2=0000000000000000 scene_calls="));
    }
    /* SetParent's camera context call: the anchor test read a stand-in (viewpoint 1), the events go out for
     * the live player camera; any other camera passes unchanged; a stand-in without a live camera: no call. */
    {
        LONG64 before=rc_stub_calls[RC_CONTEXT_CALL];void *old_parent=(void*)0x3333,*new_parent=(void*)0x4444;
        rc_context_native=test_rc_context_native;test_rc_context_hits=0;memset(test_rc_context_args,0,sizeof(test_rc_context_args));
        assert(*rc_shadow_camera()==a || *rc_shadow_camera()==b);
        rc_context_events(a,old_parent,new_parent);
        assert(test_rc_context_hits==1 && test_rc_context_args[0]==live_camera && test_rc_context_args[1]==old_parent && test_rc_context_args[2]==new_parent);
        rc_context_events(b,new_parent,old_parent);
        assert(test_rc_context_hits==2 && test_rc_context_args[0]==live_camera && test_rc_context_args[1]==new_parent);
        void *other=(void*)0x5555;rc_context_events(other,old_parent,new_parent);
        assert(test_rc_context_hits==3 && test_rc_context_args[0]==other); /* the live camera itself (no stand-in latched) */
        /* Through the executable stub, as the patched call in SetParent reaches it. */
        ((void (*)(void *,void *,void *))(void *)rc_stub(RC_CONTEXT_CALL))(a,old_parent,new_parent);
        assert(test_rc_context_hits==4 && test_rc_context_args[0]==live_camera);
        void *none=NULL;memcpy(universe+0x3d0,&none,8);rc_context_events(a,old_parent,new_parent);
        assert(test_rc_context_hits==4 && rc_context_skipped==1);memcpy(universe+0x3d0,&live_camera,8);
        assert(rc_stub_calls[RC_CONTEXT_CALL]==before+5);
        char status[1024];assert(rc_status(status,sizeof(status))>0 && strstr(status," context_calls=") && strstr(status," context_skipped=1"));
        rc_context_native=NULL;rc_context_skipped=0;
    }
    /* Viewpoint policy: the object's sector picks the view point (2 only where view point 1 is not). */
    {
        test_rcp_world();
        void *saved_points[2]={view_points[0],view_points[1]};uint64_t saved_ids[2]={view_ids[0],view_ids[1]};unsigned saved_vcount=view_count;
        view_points[0]=test_rcp_ship_a;view_ids[0]=0x5ca81;view_points[1]=test_rcp_ship_b;view_ids[1]=0x5ca82;view_count=2;
        stats.issued=20;assert(rc_frame(20));
        void *camera2=*rc_manager2_camera();void *shadow2_player,*shadow2_camera;
        memcpy(&shadow2_player,rc_page+RC_SHADOW2+0x230,8);memcpy(&shadow2_camera,rc_page+RC_SHADOW2+0x3d0,8);
        assert(camera2 && shadow2_player==test_rcp_ship_b && shadow2_camera==camera2 && rc_view_sector58==test_rcp_sector_a &&
               rc_vp2_sector==test_rcp_sector_b && rc_vp2_cluster==test_rcp_cluster && *rc_shadow_player()==test_rcp_ship_a);
        unsigned manager_site=RC_SITES,universe_site=RC_SITES,traffic_site=RC_SITES;
        for(unsigned i=0;i<RC_SITES;i++) {
            if(rc_sites[i].object==RC_OBJ_R12 && manager_site==RC_SITES) manager_site=i;
            if(rc_sites[i].object==RC_OBJ_RDI && rc_sites[i].slot==RC_SLOT_UNIVERSE && universe_site==RC_SITES) universe_site=i;
            if(rc_sites[i].object==RC_OBJ_R12_TRAFFIC && rc_sites[i].slot==RC_SLOT_UNIVERSE) traffic_site=i;
        }
        assert(manager_site<RC_SITES && universe_site<RC_SITES && traffic_site<RC_SITES);
        /* Off: always view point 1. */
        assert(rc_pick(test_rcp_part_b,manager_site)==rc_page+RC_MANAGER && rc_pick(test_rcp_part_b,universe_site)==rc_page+RC_SHADOW);
        rc_policy=1;rc_picks=rc_picks_two=0;
        assert(rc_pick(test_rcp_part_b,manager_site)==rc_page+RC_MANAGER2 && rc_pick(test_rcp_part_a,manager_site)==rc_page+RC_MANAGER);
        assert(rc_pick(test_rcp_part_b,universe_site)==rc_page+RC_SHADOW2 && rc_pick(test_rcp_part_a,universe_site)==rc_page+RC_SHADOW);
        assert(rc_pick(test_rcp_traffic,traffic_site)==rc_page+RC_SHADOW2 && rc_pick(NULL,universe_site)==rc_page+RC_SHADOW);
        void *za=test_rcp_zone_a;memcpy(test_rcp_traffic+0x108,&za,8); /* +0x108 wins over +0x100 */
        assert(rc_pick(test_rcp_traffic,traffic_site)==rc_page+RC_SHADOW);memset(test_rcp_traffic+0x108,0,8);
        assert(rc_picks==7 && rc_picks_two==3 && rc_site_picks[manager_site]==3 && rc_site_picks[universe_site]==4 && rc_site_picks[traffic_site]==2 /* the two off picks too */);
        /* Policy rows: tick, site, choice, the object's runtime ID (a traffic object: its zone's), MoveWorker flag. */
        assert(!rc_pick_rows_arm(5)); /* not before the first frame here: refused */
        *(uint64_t*)(test_rcp_part_b+8)=0x777;*(uint64_t*)(test_rcp_zone_b+8)=0x888;rc_pick_rows_ticks=stats.issued;rc_pick_rows=0;
        assert(rc_pick(test_rcp_part_b,manager_site)==rc_page+RC_MANAGER2 && rc_pick(test_rcp_traffic,traffic_site)==rc_page+RC_SHADOW2);
        out=NULL;rc_camera_sector(NULL,&out,test_rcp_part_b);assert(rc_pick_rows==3);
        char prow[192];assert(rc_pick_row(0,prow,sizeof(prow))>0 && strstr(prow," two=1 key=0000000000000000 runtime=777 mover="));
        assert(rc_pick_row(1,prow,sizeof(prow))>0 && strstr(prow," runtime=888 ") && rc_pick_row(2,prow,sizeof(prow))>0 && strstr(prow," site=101 two=1 "));
        assert(!rc_pick_row(3,prow,sizeof(prow)));
        rc_pick_rows_ticks=stats.issued-1;assert(rc_pick(test_rcp_part_b,manager_site) && rc_pick_rows==3); /* past the limit: none */
        rc_pick_rows=RC_PICK_ROWS+4;assert(rc_pick_row(1,prow,sizeof(prow))>0 && !strcmp(prow,"index=1 evicted=1"));
        /* View point 2's zone test and lane camera from the simulation's transforms at the thread's game time;
         * view point 1's through the original helpers; both logged with their vector (sites 0x103, 0x104). */
        {
            rc_gtr_native=test_rcp_gtr;rc_zone_test_native=test_rcp_zone_native;rc_lane_camera_native=test_rcp_lane_native;
            test_rcp_gtr_calls=test_rcp_native_calls=0;rc_pick_rows=0;rc_pick_rows_ticks=stats.issued;
            void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0;memcpy(&now,(unsigned char*)tls[0]+0x300,8);
            void *cam2=*rc_manager2_camera(),*cam1=*rc_manager_camera();__declspec(align(16)) float v[16];
            memset(v,0x7f,sizeof(v));rc_zone_test(test_rcp_zone_b,v,cam2);
            assert(test_rcp_gtr_calls==2 && test_rcp_gtr_self[0]==test_rcp_zone_b && test_rcp_gtr_ancestor[0]==test_rcp_cluster &&
                   test_rcp_gtr_self[1]==test_rcp_ship_b && test_rcp_gtr_ancestor[1]==test_rcp_cluster && test_rcp_gtr_time[0]==now);
            assert(v[0]==-5 && v[1]==-6 && v[2]==-7 && v[3]==0 && v[4]==1 && v[9]==1 && v[14]==1 && !test_rcp_native_calls); /* zone minus ship */
            rc_zone_test(test_rcp_zone_b,v,cam1);assert(test_rcp_native_calls==1 && v[0]==-1 && test_rcp_gtr_calls==2);
            memset(v,0x7f,sizeof(v));assert(rc_lane_camera(cam2,v,test_rcp_zone_b)==v && v[0]==5 && v[1]==6 && v[2]==7 && v[4]==1 && v[9]==1 && v[14]==1 &&
                                         test_rcp_gtr_calls==3 &&
                                         test_rcp_gtr_self[2]==test_rcp_ship_b && test_rcp_gtr_ancestor[2]==test_rcp_zone_b);
            assert(rc_lane_camera(cam1,v,test_rcp_zone_b)==v && v[0]==-4 && test_rcp_native_calls==2);
            /* Through the executable stubs, as the patched calls reach them. */
            memset(v,0x7f,sizeof(v));((void (*)(void *,float *,void *))(void *)rc_stub(RC_ZONE_TEST_CALL))(test_rcp_zone_b,v,cam2);assert(v[0]==-5 && test_rcp_gtr_calls==5);
            ((void *(*)(void *,float *,void *))(void *)rc_stub(RC_LANE_CAMERA_CALL))(cam2,v,test_rcp_zone_b);assert(v[0]==5 && test_rcp_gtr_calls==6);
            /* Policy off: the originals for every camera. */
            rc_policy=0;rc_zone_test(test_rcp_zone_b,v,cam2);assert(test_rcp_native_calls==3 && test_rcp_gtr_calls==6);rc_policy=1;
            char prow[192];assert(rc_pick_rows==7 && rc_pick_row(0,prow,sizeof(prow))>0 && strstr(prow," site=103 two=1 ") && strstr(prow," pos=-5.000,-6.000,-7.000"));
            assert(rc_pick_row(3,prow,sizeof(prow))>0 && strstr(prow," site=104 two=0 ") && strstr(prow," pos=-4.000,"));
            rc_gtr_native=NULL;rc_zone_test_native=NULL;rc_lane_camera_native=NULL;
            /* Switch-on and collection rows (diagnostic): +0x1d0 before/after, +0x228; the cached-waypoint count. */
            rc_activate_native=test_rcp_activate;rc_collect_native=test_rcp_collect;rc_pick_rows=0;
            test_rcp_traffic[0x1d0]=0;test_rcp_traffic[0x228]=1;*(uint64_t*)(test_rcp_zone_b+8)=0x888;
            ((void (*)(void *))(void *)rc_stub(RC_ACTIVATE_CALL))(test_rcp_traffic);
            assert(test_rcp_traffic[0x1d0]==1 && rc_pick_rows==1 && rc_pick_row(0,prow,sizeof(prow))>0 &&
                   strstr(prow," site=105 two=1 key=0000000000000000 runtime=888 ") && strstr(prow," a=0 b=1 pend=0"));
            rc_activate_update_a(test_rcp_traffic);rc_activate_update_b(test_rcp_traffic);
            assert(rc_pick_row(1,prow,sizeof(prow))>0 && strstr(prow," site=106 ") && strstr(prow," a=1 b=1 ") &&
                   rc_pick_row(2,prow,sizeof(prow))>0 && strstr(prow," site=107 "));
            void *vec[3]={0};assert(((void *(*)(void *,void **))(void *)rc_stub(RC_COLLECT_CALL))(test_rcp_traffic,vec)==vec);
            assert(rc_pick_row(3,prow,sizeof(prow))>0 && strstr(prow," site=108 ") && strstr(prow," a=3 b=0 pend=0"));
            rc_activate_native=NULL;rc_collect_native=NULL;test_rcp_traffic[0x1d0]=0;test_rcp_traffic[0x228]=0;
        }
        rc_pick_rows=0;rc_pick_rows_ticks=0;*(uint64_t*)(test_rcp_part_b+8)=0;*(uint64_t*)(test_rcp_zone_b+8)=0;
        rc_picks=rc_picks_two=0;for(unsigned i=0;i<RC_SITES;i++) rc_site_picks[i]=0;
        /* The camera-cluster and active-camera calls with their objects. */
        void *out=NULL;assert(rc_camera_sector(NULL,&out,test_rcp_part_b)==&out && out==test_rcp_cluster);
        out=NULL;assert(rc_camera_sector(NULL,&out,test_rcp_part_a)==&out && out==rc_view_sector);
        assert(rc_active_camera(test_rcp_traffic)==camera2 && rc_active_camera(test_rcp_part_a)==*rc_manager_camera());
        /* UniverseFrameUpdate's controllable: view point 2 on odd ticks. */
        rc_alternations=0;void *found;
        rc_tick=21;found=(void*)1;rc_controllable_a(NULL,&found);assert(rc_alternations==1);
        rc_tick=22;found=(void*)1;rc_controllable_a(NULL,&found);assert(rc_alternations==1);
        /* Through every thunk: only the load's register changes; it holds the pick for the object. */
        test_rcp_harness_code=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(test_rcp_harness_code);
        for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object) {
            void *object=rc_sites[i].object==RC_OBJ_R12_TRAFFIC?(void*)test_rcp_traffic:(void*)test_rcp_part_b;
            unsigned reg=((rc_sites[i].prefix[2]>>3)&7u)|(rc_sites[i].prefix[0]&4u?8u:0u);
            unsigned slot=reg==0?0:reg==1?1:reg==2?2:reg==8?3:99;assert(slot<99);
            void *want=rc_sites[i].slot==RC_SLOT_UNIVERSE?rc_page+RC_SHADOW2:rc_page+RC_MANAGER2;
            memset(test_rcp_out,0,sizeof(test_rcp_out));test_rcp_harness(rc_thunk(i));((void (*)(void *))(void *)test_rcp_harness_code)(object);
            const uint64_t sentinel[4]={(uint64_t)(uintptr_t)rc_thunk(i),(uint64_t)(uintptr_t)object,0x1111111111111111ULL,0x2222222222222222ULL};
            for(unsigned r=0;r<4;r++) assert(test_rcp_out[r==3?3:r]==(r==slot?(uint64_t)(uintptr_t)want:sentinel[r]));
            assert(test_rcp_out[4]==0x3333333333333333ULL && test_rcp_out[5]==0x4444444444444444ULL && test_rcp_out[6]==0x5555555555555555ULL);
            for(unsigned x=0;x<6;x++) assert(test_rcp_out[8+2*x]==0xa0a0a0a000000000ULL+x);
            memset(test_rcp_out,0,sizeof(test_rcp_out));((void (*)(void *))(void *)test_rcp_harness_code)(test_rcp_part_a);
            assert(test_rcp_out[slot==3?3:slot]==(uint64_t)(uintptr_t)(rc_sites[i].slot==RC_SLOT_UNIVERSE?rc_page+RC_SHADOW:rc_page+RC_MANAGER));
        }
        VirtualFree(test_rcp_harness_code,0,MEM_RELEASE);test_rcp_harness_code=NULL;
        /* One sector for both view points: view point 1 everywhere, no alternation. */
        void *sa=test_rcp_sector_a;memcpy(test_rcp_zone_b+0x70,&sa,8);stats.issued=21;assert(rc_frame(21));
        assert(rc_vp2_sector==test_rcp_sector_a && rc_pick(test_rcp_part_b,manager_site)==rc_page+RC_MANAGER);
        rc_alternations=0;found=(void*)1;rc_controllable_a(NULL,&found);assert(!rc_alternations);
        /* More view points than stand-ins (a 4 client session): the second stand-in goes to the first view point in another sector. */
        {
            void *sb=test_rcp_sector_b;memcpy(test_rcp_zone_b+0x70,&sb,8); /* ship_b back in sector_b */
            *(uint64_t*)(test_rcp_part_a+8)=0x5ca83;*(uint64_t*)(test_rcp_part_b+8)=0x5ca84;
            void *const held_points[VIEW_MAX]={view_points[0],view_points[1],view_points[2],view_points[3]};
            uint64_t held_ids[VIEW_MAX];memcpy(held_ids,view_ids,sizeof(held_ids));
            view_points[0]=test_rcp_ship_a;view_ids[0]=0x5ca81;view_points[1]=test_rcp_part_a;view_ids[1]=0x5ca83; /* slot 2 beside slot 1 */
            view_points[2]=test_rcp_ship_b;view_ids[2]=0x5ca82;view_points[3]=test_rcp_part_b;view_ids[3]=0x5ca84;
            view_count=4;stats.issued=211;assert(rc_frame(211));
            assert(rc_vp2_id==0x5ca82 && rc_vp2_sector==test_rcp_sector_b && !rc_uncovered_now && !rc_uncovered_ticks);
            void *second=NULL;memcpy(&second,rc_page+RC_SHADOW2+0x230,8);assert(second==test_rcp_ship_b);
            /* All in one sector: the second view point, as before. */
            void *sa2=test_rcp_sector_a;memcpy(test_rcp_zone_b+0x70,&sa2,8);stats.issued=212;assert(rc_frame(212));
            assert(rc_vp2_id==0x5ca83 && !rc_uncovered_now);
            /* A third sector: the first other one is chosen, the third counted, on every client alike. */
            memcpy(test_rcp_zone_b+0x70,&sb,8);
            static unsigned char sector_c[0x200],zone_c[0x200];void *vt=test_rcp_vtable,*cluster=test_rcp_cluster,*sc=sector_c,*zc=zone_c;
            memset(sector_c,0,sizeof(sector_c));memset(zone_c,0,sizeof(zone_c));memcpy(sector_c,&vt,8);memcpy(zone_c,&vt,8);
            memcpy(sector_c+0x70,&cluster,8);memcpy(zone_c+0x70,&sc,8);
            test_rcp_extra_sector=sector_c;
            void *held_parent;memcpy(&held_parent,test_rcp_part_b+0x70,8);memcpy(test_rcp_part_b+0x70,&zc,8);
            stats.issued=213;assert(rc_frame(213));
            assert(rc_vp2_id==0x5ca82 && rc_uncovered_now==1 && rc_uncovered_ticks==1);
            char text[3072];assert(rc_status(text,sizeof(text))>0 && strstr(text," uncovered=1/1 "));
            test_rcp_extra_sector=NULL;memcpy(test_rcp_part_b+0x70,&held_parent,8);rc_uncovered_ticks=0;rc_uncovered_now=0;
            for(unsigned i=0;i<VIEW_MAX;i++) {view_points[i]=held_points[i];view_ids[i]=held_ids[i];}
            *(uint64_t*)(test_rcp_part_a+8)=0;*(uint64_t*)(test_rcp_part_b+8)=0;
            memcpy(test_rcp_zone_b+0x70,&sa,8);view_count=2;stats.issued=214;assert(rc_frame(214));
        }
        /* No view point 2: the second shadow universe holds view point 1's values. */
        view_count=1;stats.issued=22;assert(rc_frame(22) && !*rc_manager2_camera());
        memcpy(&shadow2_player,rc_page+RC_SHADOW2+0x230,8);assert(shadow2_player==test_rcp_ship_a && rc_pick(test_rcp_part_b,manager_site)==rc_page+RC_MANAGER);
        char status[1024];assert(rc_status(status,sizeof(status))>0 && strstr(status," policy=sector picks="));
        rc_policy=0;view_points[0]=saved_points[0];view_points[1]=saved_points[1];view_ids[0]=saved_ids[0];view_ids[1]=saved_ids[1];view_count=saved_vcount;
    }
    /* Shape refresh: the interval from the simulation and the nearest view point latched in the body's sector (the
     * native rule's view-cache reads replaced), the native flag, bounds and RNG draw; the first refresh per object key
     * after the install aligned to the thread's game time. */
    {
        test_rcp_world();test_shape_world();
        void *saved_points[2]={view_points[0],view_points[1]};uint64_t saved_ids[2]={view_ids[0],view_ids[1]};unsigned saved_vcount=view_count;
        view_points[0]=test_rcp_ship_a;view_ids[0]=0x5ca81;view_points[1]=test_rcp_ship_b;view_ids[1]=0x5ca82;view_count=2;view_latched=1;
        stats.issued=30;assert(rc_frame(30) && rc_shape_count==2 && rc_shape_points[0]==test_rcp_ship_a && rc_shape_sectors[0]==test_rcp_sector_a &&
                                rc_shape_points[1]==test_rcp_ship_b && rc_shape_sectors[1]==test_rcp_sector_b);
        rc_gtr_native=test_rcp_gtr;rc_shape_macro_native=test_shape_macro;rc_shape_rand_native=test_shape_rand;
        rc_shape_interval_native=test_shape_interval_native;rc_shape_update_native=test_shape_update_native;
        rc_shape_near=rc_shape_far=rc_shape_native=rc_shape_aligned=rc_shape_unmapped=rc_shape_unkeyed=0;test_shape_rand_calls=test_shape_native_calls=0;
        void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0;memcpy(&now,(unsigned char*)tls[0]+0x300,8);
        /* part_b at (60,0,0) and ship_b at (65,6,7) in sector_b: sqrt(110) from view point 2; bounds diagonal 3. */
        test_rcp_gtr_calls=0;float r=rc_shape_interval(test_shape_holder,test_rcp_part_b);
        assert(fabsf(r-((sqrtf(110.0f)-3.0f)/3.0f-0.25f))<1e-5f && test_shape_rand_calls==1 && test_shape_rand_range==0.1f &&
               test_shape_rand_state!=(unsigned char*)tls[0]+0x318 && test_shape_rand_word==lockstep_seed(RC_SHAPE_RNG_DOMAIN,30,0,0,0,0) &&
               rc_shape_unkeyed==1 && rc_shape_near==1 && test_rcp_gtr_calls==2 &&
               test_rcp_gtr_ancestor[0]==test_rcp_sector_b && test_rcp_gtr_time[0]==now);
        /* part_a at the origin of sector_a with ship_a: (0 - 3) / 3 clamps to 0.5. */
        r=rc_shape_interval(test_shape_holder,test_rcp_part_a);assert(r==0.5f-0.25f && test_shape_rand_calls==2);
        /* A small compound far away: (d - 0.01) / 0.01 clamps to 5. */
        test_shape_box[4]=0.01f;test_shape_box[5]=test_shape_box[6]=0;r=rc_shape_interval(test_shape_holder,test_rcp_part_b);assert(r==5.0f-0.25f);
        /* No bounds (diagonal below 1e-4): 0.5 without a draw. */
        test_shape_box[4]=0;r=rc_shape_interval(test_shape_holder,test_rcp_part_b);assert(r==0.5f && test_shape_rand_calls==3);
        test_shape_world();
        /* The macro flag (+0x278): 0 s; no owner: 0.5. */
        test_shape_macro_data[0x278]=1;assert(rc_shape_interval(test_shape_holder,test_rcp_part_b)==0.0f);test_shape_macro_data[0x278]=0;
        assert(rc_shape_interval(test_shape_holder,NULL)==0.5f && test_shape_rand_calls==3);
        /* No view point in the body's sector: 5 s without a draw (the native other-sector answer). */
        view_count=1;stats.issued=31;assert(rc_frame(31) && rc_shape_count==1);
        r=rc_shape_interval(test_shape_holder,test_rcp_part_b);assert(r==5.0f && test_shape_rand_calls==3 && rc_shape_far==1);
        /* Two view points in one sector: the nearer counts (ship_a at the origin of sector_b now, 60 away; ship_b sqrt(110)). */
        void *sb=test_rcp_sector_b;memcpy(test_rcp_zone_a+0x70,&sb,8);view_count=2;stats.issued=32;
        assert(rc_frame(32) && rc_shape_count==2 && rc_shape_sectors[0]==test_rcp_sector_b);
        r=rc_shape_interval(test_shape_holder,test_rcp_part_b);assert(fabsf(r-((sqrtf(110.0f)-3.0f)/3.0f-0.25f))<1e-5f);
        void *sa=test_rcp_sector_a;memcpy(test_rcp_zone_a+0x70,&sa,8);
        /* Not latched yet (or after stop): the native interval. */
        LONG latched=rc_shape_count;rc_shape_count=-1;assert(rc_shape_interval(test_shape_holder,test_rcp_part_b)==9.0f && test_shape_native_calls==1 && rc_shape_native==1);
        rc_shape_count=latched;
        /* Through the executable stub, as the patched call in 0xc2c00 reaches it. */
        r=((float (*)(void *,void *))(void *)rc_stub(RC_SHAPE_INTERVAL_CALL))(test_shape_holder,test_rcp_part_b);
        assert(fabsf(r-((sqrtf(110.0f)-3.0f)/3.0f-0.25f))<1e-5f);
        /* Alignment: the first refresh per object key sets the due time (+0x28) to the thread's game time, then the native
         * refresh with the same arguments; later calls and objects without a key keep their due times. */
        memset((void *)rc_shape_keys,0,sizeof(rc_shape_keys));assert(rc_shape_key_first(0x123)==1 && rc_shape_key_first(0x123)==0);
        memset((void *)rc_shape_keys,0,sizeof(rc_shape_keys));
        *(uint64_t*)(test_rcp_part_b+8)=0x5ca73; /* key 0x176a99 */
        double due=1e9;memcpy(test_shape_holder+0x28,&due,8);test_shape_update_calls=0;
        assert(rc_shape_update(test_shape_holder,test_rcp_part_b,0)==1 && test_shape_update_calls==1 && test_shape_update_args[0]==test_shape_holder &&
               test_shape_update_args[1]==test_rcp_part_b && !test_shape_update_args[2] && rc_shape_aligned==1);
        memcpy(&due,test_shape_holder+0x28,8);assert(due==now);
        due=1e9;memcpy(test_shape_holder+0x28,&due,8);
        assert(((char (*)(void *,void *,unsigned char))(void *)rc_stub(RC_SHAPE_UPDATE_CALL))(test_shape_holder,test_rcp_part_b,1)==1 &&
               test_shape_update_calls==2 && test_shape_update_args[2]==(void*)1 && rc_shape_aligned==1);
        memcpy(&due,test_shape_holder+0x28,8);assert(due==1e9);
        *(uint64_t*)(test_rcp_part_b+8)=0;rc_shape_update(test_shape_holder,test_rcp_part_b,0);assert(rc_shape_unmapped==1 && rc_shape_aligned==1);
        *(uint64_t*)(test_rcp_part_b+8)=0x5ca73;memset((void *)rc_shape_keys,0,sizeof(rc_shape_keys));rc_shape_count=-1;
        rc_shape_update(test_shape_holder,test_rcp_part_b,0);memcpy(&due,test_shape_holder+0x28,8);assert(due==1e9 && rc_shape_aligned==1); /* not latched */
        rc_shape_count=latched;*(uint64_t*)(test_rcp_part_b+8)=0;
        char status[2048];assert(rc_status(status,sizeof(status))>0 && strstr(status," shape_refresh=") && strstr(status," aligned=1 unmapped=1 near=") &&
                                strstr(status," far=1 shape_native=1"));
        rc_gtr_native=NULL;rc_shape_macro_native=NULL;rc_shape_rand_native=NULL;rc_shape_interval_native=NULL;rc_shape_update_native=NULL;
        rc_shape_near=rc_shape_far=rc_shape_native=rc_shape_aligned=rc_shape_unmapped=0;memset((void *)rc_shape_keys,0,sizeof(rc_shape_keys));
        view_points[0]=saved_points[0];view_points[1]=saved_points[1];view_ids[0]=saved_ids[0];view_ids[1]=saved_ids[1];view_count=saved_vcount;
    }
    /* Mining (0x7884a0's camera test), XS detail (ShipExtraSmall::vfunc876's camera terms) and the camera probe. */
    {
        test_rcp_world();
        void *saved_points[2]={view_points[0],view_points[1]};uint64_t saved_ids[2]={view_ids[0],view_ids[1]};unsigned saved_vcount=view_count;
        view_points[0]=test_rcp_ship_a;view_ids[0]=0x5ca81;view_points[1]=test_rcp_ship_b;view_ids[1]=0x5ca82;view_count=2;view_latched=1;
        stats.issued=40;assert(rc_frame(40) && rc_shape_count==2 && rc_shape_clusters[0]==test_rcp_cluster && rc_shape_clusters[1]==test_rcp_cluster);
        rc_gtr_native=test_rcp_gtr;rc_mining_calls=rc_mining_near_count=rc_mining_foreign=0;
        /* In zone_b's frame ship_b sits at (5,6,7) and ship_a at (-100,-20,-3): near (within 50 km) at (5,6,7). */
        const float here[4]={5,6,7,0},far_away[4]={60000,0,0,0};
        assert(rc_mining_near(test_rcp_cluster,here,test_rcp_zone_b)==1 && rc_mining_near_count==1);
        assert(rc_mining_near(test_rcp_cluster,far_away,test_rcp_zone_b)==0); /* 59995 km from ship_b, 60100 from ship_a */
        const float off_a[4]={-100,-20+49999.0f,-3,0};assert(rc_mining_near(test_rcp_cluster,off_a,test_rcp_zone_b)==1); /* 49999 from ship_a */
        /* Another cluster than the view points': not near; a frame outside the cluster: not near, counted. */
        assert(rc_mining_near(test_rcp_sector_a,here,test_rcp_zone_a)==0 && !rc_mining_foreign);
        static __declspec(align(16)) unsigned char loose[0x100];memset(loose,0,sizeof(loose));
        assert(rc_mining_near(test_rcp_cluster,here,loose)==0 && rc_mining_foreign==1 && rc_mining_calls==5);
        /* Rows (site 0x109): the cluster's key, near flag, best squared distance and candidates. */
        rc_pick_rows=0;rc_pick_rows_ticks=(unsigned)stats.issued+10;*(uint64_t*)(test_rcp_cluster+8)=0x999;
        assert(rc_mining_near(test_rcp_cluster,here,test_rcp_zone_b)==1 && rc_pick_rows==1);
        char row[256];assert(rc_pick_row(0,row,sizeof(row))>0 && strstr(row," site=109 two=1 key=0000000000000000 runtime=999 ") && strstr(row," pos=0.000,2.000,1.000"));
        /* XS: the external view answers yes, the camera distance is the squared distance to the nearest view point in the ship's sector. */
        rc_pick_rows=0;rc_xs_near=rc_xs_far=0;
        assert(rc_xs_external()==1);
        float d=rc_xs_distance(test_rcp_part_b);assert(d==110.0f && rc_xs_near==1); /* part_b (60,0,0), ship_b (65,6,7) in sector_b */
        view_count=1;stats.issued=41;assert(rc_frame(41) && rc_shape_count==1);
        d=rc_xs_distance(test_rcp_part_b);assert(d==3.0e38f && rc_xs_far==1); /* no view point in sector_b */
        assert(rc_pick_rows==2 && rc_pick_row(0,row,sizeof(row))>0 && strstr(row," site=10a two=1 ") && rc_pick_row(1,row,sizeof(row))>0 && strstr(row," two=0 "));
        rc_pick_rows=0;rc_pick_rows_ticks=0;*(uint64_t*)(test_rcp_cluster+8)=0;
        /* Before the first latch: the natives. */
        LONG latched=rc_shape_count;rc_shape_count=-1;rc_xs_external_native=test_xs_external;rc_xs_distance_native=test_xs_distance;
        assert(rc_xs_external()==0 && rc_xs_distance(test_rcp_part_b)==7.0f);rc_shape_count=latched;
        rc_xs_external_native=NULL;rc_xs_distance_native=NULL;rc_gtr_native=NULL;
        /* Through the executable stubs. */
        assert(((char (*)(void))(void *)rc_stub(RC_XS_EXTERNAL_CALL))()==1);
        /* A stock session: the slots' replayed soft targets, external views and ships (slot 1 ship_b in external view targeting part_b,
         * slot 2 ship_a in the cockpit). */
        {
            unsigned (*saved_count)(void)=rc_vpl_count;uint64_t (*saved_target)(unsigned)=rc_vpl_target;int (*saved_external)(unsigned)=rc_vpl_external;
            void *(*saved_ship)(unsigned)=rc_vpl_ship;
            rc_vpl_count=test_vpl_count;rc_vpl_target=test_vpl_target;rc_vpl_external=test_vpl_external;rc_vpl_ship=test_vpl_ship;rc_gtr_native=test_rcp_gtr;
            test_vpl_slots=2;test_vpl_ships[1]=test_rcp_ship_b;test_vpl_ships[2]=test_rcp_ship_a;test_vpl_external_flags[1]=1;test_vpl_external_flags[2]=0;
            *(uint64_t*)(test_rcp_part_b+8)=0x5ca73;test_vpl_targets[1]=0x176a99;test_vpl_targets[2]=1; /* slot 2: a target without a portable ID */
            void *found=(void*)0x55;rc_xs_targets=0;
            assert(rc_xs_target(test_rcp_part_b,&found)==&found && found==test_rcp_part_b && rc_xs_targets==1);
            test_vpl_targets[1]=0;found=(void*)0x55;assert(rc_xs_target(test_rcp_part_b,&found)==&found && !found);
            assert(((void **(*)(void *,void **))(void *)rc_stub(RC_XS_TARGET_CALL))(test_rcp_part_b,&found)==&found && !found);
            assert(rc_xs_external()==1);test_vpl_external_flags[1]=0;assert(rc_xs_external()==0);test_vpl_external_flags[1]=1;
            assert(rc_xs_distance(test_rcp_part_b)==110.0f); /* slot 1's ship_b, in sector_b */
            test_vpl_external_flags[1]=0;assert(rc_xs_distance(test_rcp_part_b)==3.0e38f);test_vpl_external_flags[1]=1;
            test_vpl_ships[1]=test_rcp_ship_a;assert(rc_xs_distance(test_rcp_part_b)==3.0e38f); /* external, but in sector_a */
            /* No stock session: the native getter. */
            test_vpl_slots=0;rc_xs_target_native=test_xs_target;found=NULL;assert(rc_xs_target(test_rcp_part_b,&found)==&found && found==(void*)0x77);
            rc_xs_target_native=NULL;*(uint64_t*)(test_rcp_part_b+8)=0;rc_gtr_native=NULL;rc_xs_targets=0;
            rc_vpl_count=saved_count;rc_vpl_target=saved_target;rc_vpl_external=saved_external;rc_vpl_ship=saved_ship;
        }
        /* Camera probe: every call passes through and is counted by thread class; the crystal pass also counts distances above 3 km. */
        memset((void *)rc_probe_counts,0,sizeof(rc_probe_counts));rc_probe_crystal_far=0;const unsigned cls=rc_thread_class();
        rc_probe_distance_native=test_probe_distance;rc_probe_walk_native=test_probe_walk;rc_probe_vector_native=test_probe_vector;rc_probe_offset_native=test_probe_offset;
        /* The Crystal pass (group mining): before the first latch the native distance; then the nearest view point in the object's sector. */
        LONG latched2=rc_shape_count;rc_shape_count=-1;
        test_probe_value=1.0e7f;assert(rc_probe_crystal((void*)1,(void*)2)==1.0e7f);test_probe_value=4.0e6f;assert(rc_probe_crystal((void*)1,(void*)2)==4.0e6f);
        assert(rc_probe_counts[0][cls]==2 && rc_probe_crystal_far==1);
        rc_shape_count=latched2;view_count=2;stats.issued=42;assert(rc_frame(42) && rc_shape_count==2);rc_gtr_native=test_rcp_gtr;
        assert(rc_probe_crystal(test_rcp_part_b,NULL)==110.0f && rc_probe_crystal_far==1); /* ship_b sqrt(110) away: no respawn */
        view_count=1;stats.issued=43;assert(rc_frame(43));
        assert(rc_probe_crystal(test_rcp_part_b,NULL)==3.0e38f && rc_probe_crystal_far==2 && rc_probe_counts[0][cls]==4); /* nobody in sector_b */
        rc_gtr_native=NULL;
        assert(((float (*)(void *,void *))(void *)rc_stub(RC_PROBE_CALL))(test_rcp_part_b,NULL)==3.0e38f && rc_probe_counts[0][cls]==5);
        void *out=NULL;assert(rc_probe_walk1(NULL,&out)==&out && rc_probe_walk9(NULL,&out)==&out && rc_probe_counts[1][cls]==1 && rc_probe_counts[9][cls]==1);
        float v[4];assert(rc_probe_booster_a(NULL,v,NULL)==v && rc_probe_booster_b(NULL,v,NULL)==v && rc_probe_counts[10][cls]==1 && rc_probe_counts[11][cls]==1);
        assert(rc_probe_zone921(NULL,v,1.5,NULL)==v && test_probe_time==1.5 && rc_probe_counts[12][cls]==1);
        assert(((void **(*)(void *,void **))(void *)rc_stub(RC_PROBE_CALL+5))(NULL,&out)==&out && rc_probe_counts[5][cls]==1);
        char status[3072];assert(rc_status(status,sizeof(status))>0 && strstr(status," mining=6/") && strstr(status," crystal=") && strstr(status,",far:3 xs=") &&
                                strstr(status," probe=walk:"));
        rc_probe_distance_native=NULL;rc_probe_walk_native=NULL;rc_probe_vector_native=NULL;rc_probe_offset_native=NULL;
        memset((void *)rc_probe_counts,0,sizeof(rc_probe_counts));rc_probe_crystal_far=0;rc_mining_calls=rc_mining_near_count=rc_mining_foreign=0;rc_xs_near=rc_xs_far=0;
        view_points[0]=saved_points[0];view_points[1]=saved_points[1];view_ids[0]=saved_ids[0];view_ids[1]=saved_ids[1];view_count=saved_vcount;
    }
    /* Highway rows: the three calls pass through; rows only inside the armed tick window (key, attention, parameter before, values). */
    {
        static __declspec(align(16)) unsigned char highway[0x600];memset(highway,0,sizeof(highway));
        *(uint64_t*)(highway+8)=0x5ca73;*(uint32_t*)(highway+0x1ec)=7;const float t0=0.25f;memcpy(highway+0x560,&t0,4); /* key 0x176a99 */
        rc_hw_offset_native=test_hw_offset;rc_hw_set_native=test_hw_set;rc_hw_rows=0;
        for(unsigned k=0;k<3;k++) rc_stub_calls[RC_HWROW_CALL+k]=0;
        const uint64_t tick=stats.issued;rc_hw_from=tick;rc_hw_to=tick+1;
        float out[16];memset(out,0,sizeof(out));char row[320];
        assert(rc_hw_offset((void*)0x11,out,2.5,highway)==out && test_hw_time==2.5 && out[0]==100.0f && rc_hw_rows==1);
        rc_hw_set_move(highway,0.5f);assert(test_hw_t==0.5f && rc_hw_rows==2);
        rc_hw_set_frame(highway,0.75f);assert(test_hw_t==0.75f && rc_hw_rows==3);
        assert(rc_hw_row(0,row,sizeof(row))>0 && strstr(row," site=offset key=0000000000176a99 attention=7 mover=") &&
               strstr(row," before=3e800000 v=42c80000,c0000000,40400000,00000000 f=100,-2,3,0 b=0.25"));
        assert(rc_hw_row(1,row,sizeof(row))>0 && strstr(row," site=move ") && strstr(row," before=3e800000 v=3f000000,3f800000,40000000,40400000 "));
        assert(rc_hw_row(2,row,sizeof(row))>0 && strstr(row," site=frame ") && strstr(row," before=3f000000 v=3f400000,3f800000,"));
        assert(!rc_hw_row(3,row,sizeof(row)));
        assert(rc_stub_calls[RC_HWROW_CALL]==1 && rc_stub_calls[RC_HWROW_CALL+1]==1 && rc_stub_calls[RC_HWROW_CALL+2]==1);
        /* Outside the window: passed through, not recorded; no highway: no row. */
        rc_hw_from=tick+1;rc_hw_to=tick+2;rc_hw_set_move(highway,0.125f);assert(test_hw_t==0.125f && rc_hw_rows==3);
        rc_hw_from=tick;rc_hw_to=tick;assert(rc_hw_offset(NULL,out,1.0,NULL)==out && rc_hw_rows==3);
        /* Through the executable stubs. */
        ((void (*)(void *,float))(void *)rc_stub(RC_HWROW_CALL+2))(highway,0.375f);assert(rc_hw_rows==4 && test_hw_t==0.375f);
        assert(((void *(*)(void *,float *,double,void *))(void *)rc_stub(RC_HWROW_CALL))(NULL,out,3.5,highway)==out && test_hw_time==3.5 && rc_hw_rows==5);
        ((void (*)(void *,float))(void *)rc_stub(RC_HWROW_CALL+1))(highway,0.0625f);assert(rc_hw_rows==6 && test_hw_t==0.0625f);
        LONG64 saved_rows=rc_hw_rows;rc_hw_rows=RC_HWROW_RING+5;assert(rc_hw_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));rc_hw_rows=saved_rows;
        char status[3072];assert(rc_status(status,sizeof(status))>0 && strstr(status," highway_rows=3/3/2,rows:6 highway_speed="));
        /* sound_rng, through the executable stubs: the thread's word as it was, whatever the sound draws; a different private word each call. */
        {
            void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);uint64_t *word=(uint64_t*)((unsigned char*)tls[0]+0x318);const uint64_t held=*word;
            rc_sound_float_native=test_sound_float;rc_sound_choice_native=test_sound_choice;rc_sound_seeds=0;
            LONG64 floats=rc_stub_calls[RC_SOUND_CALL],choices=rc_stub_calls[RC_SOUND_CALL+1u];
            float (*fstub)(void *,float)=(float (*)(void *,float))(void *)rc_stub(RC_SOUND_CALL);
            uint64_t (*cstub)(void *,void *,void *,void *)=(uint64_t (*)(void *,void *,void *,void *))(void *)rc_stub(RC_SOUND_CALL+1u);
            assert(fstub(word,0.05f)==0.5f && test_sound_float_range==0.05f && test_sound_float_state!=word && *word==held);
            const uint64_t first=test_sound_float_word;
            assert(fstub(word,0.05f)==0.5f && test_sound_float_word!=first && *word==held);
            assert(cstub((void*)1,(void*)2,(void*)3,(void*)4)==0x1234 && *word==held && test_sound_seen!=held && test_sound_seen!=first);
            assert(test_sound_args[0]==1 && test_sound_args[1]==2 && test_sound_args[2]==3 && test_sound_args[3]==4);
            test_sound_throw=1;test_sound_choice_throw(cstub);test_sound_throw=0;assert(*word==held); /* unwinding puts it back */
            assert(rc_stub_calls[RC_SOUND_CALL]==floats+2 && rc_stub_calls[RC_SOUND_CALL+1u]==choices+2);
            assert(rc_status(status,sizeof(status))>0 && strstr(status," sound="));
            rc_sound_float_native=NULL;rc_sound_choice_native=NULL;
        }
        /* Arming: before the first admitted frame with the group armed, 1..3600 ticks from a tick >= 1; identical reruns only. */
        uint64_t saved_issued2=stats.issued;int saved_boot2=boot_mode,saved_phase2=boot_phase;unsigned saved_armed2=rc_armed_groups;
        rc_hw_from=rc_hw_to=0;stats.issued=0;boot_mode=1;boot_phase=0;rc_armed_groups&=~(1u<<RC_HIGHWAY_ROWS);
        assert(!rc_hw_rows_arm(10,1100));rc_armed_groups|=1u<<RC_HIGHWAY_ROWS;
        assert(!rc_hw_rows_arm(0,1100) && !rc_hw_rows_arm(3601,1100) && !rc_hw_rows_arm(10,0) && !rc_hw_from);
        assert(rc_hw_rows_arm(10,1100) && rc_hw_from==1100 && rc_hw_to==1109 && !rc_hw_rows);
        assert(rc_hw_rows_arm(10,1100) && !rc_hw_rows_arm(11,1100) && !rc_hw_rows_arm(10,1));
        stats.issued=saved_issued2;boot_mode=saved_boot2;boot_phase=saved_phase2;rc_armed_groups=saved_armed2;
        rc_hw_offset_native=NULL;rc_hw_set_native=NULL;rc_hw_rows=0;rc_hw_from=rc_hw_to=0;
        for(unsigned k=0;k<3;k++) rc_stub_calls[RC_HWROW_CALL+k]=0;
    }
    /* Highway speed: the seed is the portable key (no key: 0), and the thunk leaves rbp = seed, rax = the multiplier, xmm0 kept, at the resume. */
    {
        rc_hw_speed_keyed=rc_hw_speed_unmapped=0;
        assert(rc_hw_speed_seed(0x5ca73)==0x176a99 && rc_hw_speed_keyed==1 && !rc_hw_speed_unmapped);
        assert(!rc_hw_speed_seed(0x999) && !rc_hw_speed_seed(0) && rc_hw_speed_keyed==1 && rc_hw_speed_unmapped==2);
        static uint64_t seen[3];
        unsigned char *code=VirtualAlloc(NULL,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);memset(code,0xcc,0x1000);
        unsigned char *thunk=code+0x200,*resume=code+0x100;unsigned n=0;void *where=seen;
        /* entry(runtime rcx, factor xmm1): push rbp; mov rbp, rcx; movss xmm0, xmm1; jmp thunk (rsp 16-aligned as in vfunc8's body) */
        static const unsigned char entry[]={0x55,0x48,0x89,0xcd,0xf3,0x0f,0x10,0xc1,0xe9};memcpy(code,entry,sizeof(entry));
        int32_t rel=(int32_t)(thunk-(code+sizeof(entry)+4));memcpy(code+sizeof(entry),&rel,4);
        /* resume: mov r11, seen; mov [r11], rbp; mov [r11+8], rax; movss [r11+0x10], xmm0; pop rbp; ret */
        resume[n++]=0x49;resume[n++]=0xbb;memcpy(resume+n,&where,8);n+=8;
        static const unsigned char store[]={0x49,0x89,0x2b,0x49,0x89,0x43,0x08,0xf3,0x41,0x0f,0x11,0x43,0x10,0x5d,0xc3};memcpy(resume+n,store,sizeof(store));
        assert(rc_hw_speed_stub_bytes(thunk,resume)<=0x80u);FlushInstructionCache(GetCurrentProcess(),code,0x1000);
        void (*run)(uint64_t,float)=(void (*)(uint64_t,float))(void *)code;float factor;
        run(0x5ca73,0.05f);memcpy(&factor,seen+2,4);assert(seen[0]==0x176a99 && seen[1]==0x5851f42d4c957f2dULL && factor==0.05f);
        run(0x999,0.25f);memcpy(&factor,seen+2,4);assert(!seen[0] && seen[1]==0x5851f42d4c957f2dULL && factor==0.25f);
        assert(rc_hw_speed_keyed==2 && rc_hw_speed_unmapped==3);
        /* The wobble's thunk: [rsp+0x38] of the caller's frame = the seed of [rax+8]; xmm1 kept, rsp as it was.
         * entry(object rcx, range xmm1): sub rsp,0x48; mov rax,rcx; jmp thunk. resume: mov r11,seen; mov rax,[rsp+0x38]; mov [r11],rax;
         * movss [r11+0x10],xmm1; add rsp,0x48; ret. */
        static __declspec(align(16)) unsigned char ship[0x20];memset(ship,0,sizeof(ship));
        memset(code,0xcc,0x1000);
        static const unsigned char wobble_entry[]={0x48,0x83,0xec,0x48,0x48,0x89,0xc8,0xe9};memcpy(code,wobble_entry,sizeof(wobble_entry));
        rel=(int32_t)(thunk-(code+sizeof(wobble_entry)+4));memcpy(code+sizeof(wobble_entry),&rel,4);
        n=0;resume[n++]=0x49;resume[n++]=0xbb;memcpy(resume+n,&where,8);n+=8;
        static const unsigned char wobble_store[]={0x48,0x8b,0x44,0x24,0x38,0x49,0x89,0x03,0xf3,0x41,0x0f,0x11,0x4b,0x10,0x48,0x83,0xc4,0x48,0xc3};memcpy(resume+n,wobble_store,sizeof(wobble_store));
        assert(rc_hw_wobble_stub_bytes(thunk,resume)<=0x80u);FlushInstructionCache(GetCurrentProcess(),code,0x1000);
        void (*wobble)(void *,float)=(void (*)(void *,float))(void *)code;
        *(uint64_t*)(ship+8)=0x5ca73;wobble(ship,4.0f);memcpy(&factor,seen+2,4);assert(seen[0]==0x176a99 && factor==4.0f);
        *(uint64_t*)(ship+8)=0x999;wobble(ship,6.0f);memcpy(&factor,seen+2,4);assert(!seen[0] && factor==6.0f);
        /* The bump's thunk: rdx = the seed of [rcx+8], flags of test rdx,rdx, xmm4 kept, xmm1 zero.
         * entry(object rcx, xmm1): sub rsp,0x48; movaps xmm4,xmm1; jmp thunk. resume: mov r11,seen; mov [r11],rdx; setz al; movzx eax,al; mov [r11+8],rax;
         * movss [r11+0x10],xmm4; movd eax,xmm1; mov [r11+0x14],eax; add rsp,0x48; ret. */
        memset(code,0xcc,0x1000);
        static const unsigned char bump_entry[]={0x48,0x83,0xec,0x48,0x0f,0x28,0xe1,0xe9};memcpy(code,bump_entry,sizeof(bump_entry));
        rel=(int32_t)(thunk-(code+sizeof(bump_entry)+4));memcpy(code+sizeof(bump_entry),&rel,4);
        n=0;resume[n++]=0x49;resume[n++]=0xbb;memcpy(resume+n,&where,8);n+=8;
        static const unsigned char bump_store[]={0x49,0x89,0x13,0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0x49,0x89,0x43,0x08,0xf3,0x41,0x0f,0x11,0x63,0x10,0x66,0x0f,0x7e,0xc8,0x41,0x89,0x43,0x14,0x48,0x83,0xc4,0x48,0xc3};
        memcpy(resume+n,bump_store,sizeof(bump_store));
        assert(rc_hw_bump_stub_bytes(thunk,resume)<=0x80u);FlushInstructionCache(GetCurrentProcess(),code,0x1000);
        void (*bump)(void *,float)=(void (*)(void *,float))(void *)code;
        *(uint64_t*)(ship+8)=0x5ca73;bump(ship,0.5f);memcpy(&factor,seen+2,4);uint32_t zero;memcpy(&zero,(unsigned char*)(seen+2)+4,4);
        assert(seen[0]==0x176a99 && !seen[1] && factor==0.5f && !zero);
        *(uint64_t*)(ship+8)=0x999;bump(ship,0.75f);memcpy(&factor,seen+2,4);memcpy(&zero,(unsigned char*)(seen+2)+4,4);
        assert(!seen[0] && seen[1]==1 && factor==0.75f && !zero); /* no key: seed 0, and ZF from the test */
        /* Every highway jump's thunk through the emitter, within its 0x80 stub. */
        for(unsigned j=1;j<RC_JUMPS;j++) {unsigned char sized[0x100];assert(rc_jump_stub_bytes(j,sized,resume)<=0x80u);}
        assert(VirtualFree(code,0,MEM_RELEASE));rc_hw_speed_keyed=rc_hw_speed_unmapped=0;
    }
    /* Stop reasons: a changed site of mining, xs_detail, camera_probe or highway_rows stops with 166, 167, 168, 169; anything else with 130. */
    {
        unsigned saved_armed=rc_armed_groups,saved_installed=rc_installed_groups;
        static const unsigned groups[4]={RC_MINING,RC_XS,RC_PROBE,RC_HIGHWAY_ROWS},reasons[4]={166,167,168,169};
        for(unsigned k=0;k<4;k++) {
            rc_failures=0;rc_stop=RC_STOP_REASON;rc_armed_groups=1u<<groups[k];
            const unsigned i=groups[k]==RC_XS?RC_XS_DISTANCE_CALL:groups[k]==RC_PROBE?RC_PROBE_CALL+12:RC_HWROW_CALL+1;
            if(groups[k]==RC_MINING) {rc_jump_done[0]=1;image[rc_jumps[0].rva]^=1;}
            else {rc_call_done[i]=1;image[rc_calls[i].rva]^=1;}
            assert(!rc_check() && rc_stop_reason()==reasons[k] && !strcmp(rc_stage,"site_changed"));
            if(groups[k]==RC_MINING) {rc_jump_done[0]=0;image[rc_jumps[0].rva]^=1;}
            else {rc_call_done[i]=0;image[rc_calls[i].rva]^=1;}
        }
        rc_failures=0;rc_stop=RC_STOP_REASON;rc_armed_groups=saved_armed;rc_installed_groups=saved_installed;
    }
    /* Effect deferral rows: owner key from the spawn context +0x38, delay +0x68, shot time, the thread's game time. */
    {
        static __declspec(align(16)) unsigned char element[0x80],context[0x40];float delay=0.25f;memset(element,0,sizeof(element));
        memcpy(element+0x68,&delay,4);memset(context,0,sizeof(context));void *owner=test_rc_object;memcpy(context+0x38,&owner,8);
        void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0,shot;
        assert(tls && tls[0] && read_local((unsigned char*)tls[0]+0x300,&now,8));
        uint64_t before=(uint64_t)rc_effect_rows,bits;
        shot=now-0.5;memcpy(&bits,&shot,8);rc_effect_note((uint64_t)(uintptr_t)element,(uint64_t)(uintptr_t)context,bits);
        shot=now-0.1;memcpy(&bits,&shot,8);rc_effect_note((uint64_t)(uintptr_t)element,0,bits);
        assert((uint64_t)rc_effect_rows==before+2);
        const rc_effect_row_t *r0=&rc_effect_ring[before%RC_EFFECT_ROW_RING],*r1=&rc_effect_ring[(before+1)%RC_EFFECT_ROW_RING];
        assert(r0->key==0x176a99 && r0->klass==WORLD_KEY_SAVED && !r0->deferred && !r0->unreadable && r0->tick==stats.issued);
        assert(!r1->key && r1->klass==WORLD_KEY_NONE && r1->deferred); /* 0.25 s delay still ahead of the thread's time */
        char row[256];assert(rc_effect_row(before,row,sizeof(row))>0 && strstr(row," key=0000000000176a99 klass=0 delay=3e800000 time=") && strstr(row," deferred=0 unreadable=0"));
        assert(rc_effect_row(before+1,row,sizeof(row))>0 && strstr(row," deferred=1 "));
        assert(!rc_effect_row(before+2,row,sizeof(row)));
        LONG64 saved_rows=rc_effect_rows;rc_effect_rows=RC_EFFECT_ROW_RING+5;assert(rc_effect_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
        rc_effect_rows=saved_rows;
    }
    /* Status bounds at the widest values. */
    rc_tick=UINT64_MAX-1;rc_viewpoint_id=rc_viewpoint_key=rc_updates=rc_native_frames=rc_install_tick=rc_install_waits=UINT64_MAX;rc_failures=INT64_MAX;rc_stage="block_changed";
    rc_armed_groups=rc_installed_groups=(1u<<RC_GROUPS)-1;for(unsigned i=0;i<RC_CALLS;i++) rc_stub_calls[i]=INT64_MAX;rc_effect_rows=INT64_MAX;
    rc_context_skipped=INT64_MAX;for(unsigned i=0;i<RC_SITES;i++) rc_site_picks[i]=INT64_MAX;rc_picks=rc_picks_two=rc_alternations=rc_pick_rows=INT64_MAX;
    rc_shape_aligned=rc_shape_unmapped=rc_shape_near=rc_shape_far=rc_shape_native=INT64_MAX;
    rc_mining_calls=rc_mining_near_count=rc_mining_foreign=rc_xs_near=rc_xs_far=rc_xs_targets=rc_probe_crystal_far=INT64_MAX;
    for(unsigned k=0;k<RC_PROBE_SITES;k++) for(unsigned t=0;t<3;t++) rc_probe_counts[k][t]=INT64_MAX/16;
    rc_hw_rows=rc_hw_speed_keyed=rc_hw_speed_unmapped=INT64_MAX;
    assert(rc_status(text,sizeof(text))>0 && strlen(text)<2900);
    rc_hw_rows=rc_hw_speed_keyed=rc_hw_speed_unmapped=0;
    rc_mining_calls=rc_mining_near_count=rc_mining_foreign=rc_xs_near=rc_xs_far=rc_xs_targets=rc_probe_crystal_far=0;memset((void *)rc_probe_counts,0,sizeof(rc_probe_counts));rc_context_skipped=0;for(unsigned i=0;i<RC_SITES;i++) rc_site_picks[i]=0;
    rc_shape_aligned=rc_shape_unmapped=rc_shape_near=rc_shape_far=rc_shape_native=0;
    rc_picks=rc_picks_two=rc_alternations=rc_pick_rows=0;rc_armed_groups=rc_installed_groups=(1u<<RC_FIELDS)|(1u<<RC_TRAFFIC);rc_effect_rows=0;
    /* Stop: the stand-in is withdrawn at once; the sites go back only while no traffic pass runs. */
    static __declspec(align(16)) unsigned char task[0x200];memset(task,0,sizeof(task));
    InitializeCriticalSection((CRITICAL_SECTION*)(task+MT_TASK_LOCK));
    void *saved_task=*(void**)(image+MT_TASK);*(void**)(image+MT_TASK)=task;
    assert(!rc_passes_idle());
    rc_failures=0;rc_restore();
    assert(rc_installed && rc_patched && !rc_restored && !*rc_manager_camera() && *rc_shadow_player()==live_player && !strcmp(rc_stage,"restore_busy"));
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==(rc_calls[i].group==RC_FIELDS || rc_calls[i].group==RC_TRAFFIC?1:0));
    task[MT_READY]=1;assert(rc_passes_idle());
    rc_restore();assert(!rc_installed && !rc_patched && rc_restored && !rc_armed_groups);
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==0);
    /* A pass still running at the first join (cam-B8: the pass kicked by the last loading
     * frame runs into the first admitted frame): the install waits, counted, for a later join. */
    test_rc_reset();test_rc_pages();rc_installed=1;rc_mode=RC_VIEW;rc_armed_groups=1u<<RC_TRAFFIC;task[MT_READY]=0;
    view_count=1;stats.issued=12;assert(rc_frame(12) && !rc_installed_groups);
    assert(rc_join_point(12) && rc_install_waits==1 && !rc_installed_groups && !rc_failures);
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    task[MT_READY]=1;mt_worker_waits=mt_worker_wakes; /* pass done, but the MoveWorker is not parked */
    assert(rc_join_point(12) && rc_install_waits==2 && !rc_installed_groups);
    mt_worker_waits=mt_worker_wakes+1;stats.issued=13;
    assert(rc_frame(13) && rc_join_point(13) && rc_installed_groups==1u<<RC_TRAFFIC && rc_install_tick==13 && !strcmp(rc_stage,"installed"));
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==(rc_sites[i].group==RC_TRAFFIC?1:0));
    assert(rc_join_point(14) && rc_install_waits==2); /* installed: nothing more to do */
    assert(rc_remove_patches());rc_patched=0;
    /* Bounded: a pass that never finishes stops the gate (reason 130) without writing a site. */
    test_rc_reset();test_rc_pages();rc_installed=1;rc_mode=RC_VIEW;rc_armed_groups=1u<<RC_TRAFFIC;task[MT_READY]=0;
    stats.issued=15;assert(rc_frame(15));
    for(unsigned i=1;i<RC_INSTALL_WAITS;i++) assert(rc_join_point(15));
    assert(!rc_join_point(15) && !strcmp(rc_stage,"busy") && rc_failures==1 && !rc_installed_groups && !rc_check());
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    *(void**)(image+MT_TASK)=saved_task;DeleteCriticalSection((CRITICAL_SECTION*)(task+MT_TASK_LOCK));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_identity_count;world_identity_sealed=prior_sealed;
    *(void**)(image+RC_UNIVERSE)=saved_universe;
    test_rc_reset();test_rc_clear_sites();
    if(!saved_page) {VirtualFree(rc_page,0,MEM_RELEASE);rc_page=NULL;} else rc_page=saved_page;
    if(!saved_stubs) {VirtualFree(rc_stubs,0,MEM_RELEASE);rc_stubs=NULL;} else rc_stubs=saved_stubs;
    view_installed=saved_view;mt_region.mode=saved_region;mt_traffic.mode=saved_traffic;
    mt_installed=saved_mt;mt_worker_waits=saved_waits;mt_worker_wakes=saved_wakes;
    view_latched=saved_latched;view_count=saved_count;view_points[0]=saved_point;view_ids[0]=saved_id;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("region camera layout/slots/stubs/arm/frame/install/stop tests PASS\n");
}
/* field_fill: the fill of a store of two cells against the render pass's rules. */
static __declspec(align(16)) unsigned char test_ff_manager[0x200],test_ff_entry[2][0x940],test_ff_cell[2][0x420],test_ff_zone[0x200],test_ff_view[0x100],test_ff_tls[0x400];
static __declspec(align(16)) unsigned char test_ff_fields[2][0x100],test_ff_tree[3][0x40],test_ff_lists[3][0x10],test_ff_new_node[0x120];
static void *test_ff_zone_vt[0x11e8/8+1],*test_ff_cell_vt[0x1580/8+1],*test_ff_field_vt[0xe8/8+1];
static int test_ff_zone_33,test_ff_cell_active,test_ff_field_on[2],test_ff_refreshes,test_ff_released,test_ff_rows,test_ff_nodes_made,test_ff_emplaced;
static void *test_ff_variants[2];
static float test_ff_at[3];
static uint64_t test_ff_word,test_ff_area_word;
static struct {void *tree,*key;float x;} test_ff_row[16];
static char test_ff_zone_class(void *o,int k) {(void)o;return k==0x33 && test_ff_zone_33;}
static char test_ff_cell_on(void *o,int32_t attention,int zero) {assert((o==test_ff_cell[0] || o==test_ff_cell[1]) && attention==5 && !zero);return (char)test_ff_cell_active;}
static float test_ff_value(void *f) {return f==test_ff_fields[0]?6000.0f:10.0f;}
static char test_ff_active(void *f) {return (char)test_ff_field_on[f==test_ff_fields[0]?0:1];}
static void **test_ff_field_variants(void *f) {(void)f;return test_ff_variants;}
static void test_ff_note(void *tree,void *key,void *record) {
    assert(test_ff_rows<16);test_ff_row[test_ff_rows].tree=tree;test_ff_row[test_ff_rows].key=key;memcpy(&test_ff_row[test_ff_rows].x,record,4);++test_ff_rows;
}
static void test_ff_position(void *record,float *out,void *zone,void *cell,unsigned char late) {
    assert(zone==test_ff_zone && (cell==test_ff_cell[0] || cell==test_ff_cell[1]) && !late);memcpy(out,record,12);memcpy(&test_ff_word,test_ff_tls+0x318,8);
}
static void *test_ff_emplace(void *tree,void *out,void *hint,void **key,void *record) {
    assert(out && !hint);++test_ff_emplaced;test_ff_note(tree,*key,record);return out;
}
static void *test_ff_node(void *tree) {(void)tree;++test_ff_nodes_made;return test_ff_new_node;}
static void test_ff_construct(void *at,void **key,void *record) {assert(at==test_ff_new_node+0x20);test_ff_note(*(void **)test_ff_new_node,*key,record);}
static void test_ff_release(void *node,size_t size) {assert(size==0xa0);++test_ff_released;free(node);}
static void test_ff_refresh(void *entry) {
    assert((entry==test_ff_entry[0] || entry==test_ff_entry[1]) && ((CRITICAL_SECTION *)((unsigned char *)entry+0x8f0))->RecursionCount>0);++test_ff_refreshes;
}
static void *test_ff_area(void *sector,float *pos,void *wares,char a,char b,char c,char d,unsigned e,float *factor) {
    assert(sector==test_ff_zone && wares==test_ff_view && a==1 && b==2 && c==3 && d==4 && e==5 && factor==test_ff_at);(void)pos;
    memcpy(&test_ff_area_word,test_ff_tls+0x318,8);return test_ff_cell[0];
}
static void test_ff_gtr(void *self,float *out,double time,void *ancestor,unsigned char flag) {
    double now;memcpy(&now,test_ff_tls+0x300,8);assert(self==test_ff_view && ancestor==test_ff_zone && time==now && !flag);memcpy(out,test_ff_at,12);
}
static unsigned char *test_ff_add(unsigned k,float x,unsigned char flag) {
    unsigned char *n=calloc(1,0xa0),*head=test_ff_lists[k],*prev=*(unsigned char **)(head+8);assert(n);
    const float v[3]={x,0,0};memcpy(n+0x10,v,12);n[0x90]=flag;
    *(unsigned char **)n=head;*(unsigned char **)(n+8)=prev;*(unsigned char **)prev=n;*(unsigned char **)(head+8)=n;
    ++*(uint64_t *)(test_ff_tree[k]+0x30);return n;
}
static unsigned test_ff_left(unsigned k,float out[8]) {
    unsigned n=0;unsigned char *head=test_ff_lists[k];
    for(unsigned char *p=*(unsigned char **)head;p!=head;p=*(unsigned char **)p) {assert(n<8);memcpy(&out[n++],p+0x10,4);}
    assert(n==*(uint64_t *)(test_ff_tree[k]+0x30));return n;
}
/* What Field::vfunc10 does with the pending tree in the next Region::Move. */
static void test_ff_consume(void) {
    for(unsigned k=0;k<2;k++) {*(void **)(test_ff_fields[k]+0x58)=NULL;*(void **)(test_ff_fields[k]+0x70)=NULL;*(uint64_t *)(test_ff_fields[k]+0x78)=0;}
}
static void test_rc_field_fill(void) {
    static const rc_fill_calls_t calls={test_ff_position,test_ff_emplace,test_ff_node,test_ff_construct,test_ff_release,test_ff_refresh,test_ff_area};
    static float radii[10]={5000,0,0,1000,0, 0,0,0,3000,0};static void *pair[2];pair[0]=radii;pair[1]=radii+10;
    void *const saved_manager=*(void **)(image+RC_FILL_MANAGER),*const saved_radii=*(void **)(image+RC_FILL_RADII);
    *(void **)(image+RC_FILL_MANAGER)=test_ff_manager;*(void **)(image+RC_FILL_RADII)=pair;
    rc_fill_test=&calls;rc_gtr_native=test_ff_gtr;
    test_ff_zone_vt[0x11e8/8]=(void *)test_ff_zone_class;*(void **)test_ff_zone=test_ff_zone_vt;
    test_ff_cell_vt[0x1580/8]=(void *)test_ff_cell_on;
    test_ff_field_vt[0x20/8]=(void *)test_ff_value;test_ff_field_vt[0xd8/8]=(void *)test_ff_active;test_ff_field_vt[0xe8/8]=(void *)test_ff_field_variants;
    static void *entries[2];entries[0]=test_ff_entry[0];entries[1]=test_ff_entry[1];
    unsigned char *const store=test_ff_manager+0x60;
    *(void ***)(store+0x40)=entries;*(void ***)(store+0x48)=entries+2;InitializeCriticalSection((CRITICAL_SECTION *)(store+0x68));
    for(unsigned k=0;k<2;k++) {
        *(void **)test_ff_cell[k]=test_ff_cell_vt;*(int32_t *)(test_ff_cell[k]+0x1ec)=5;
        InitializeCriticalSection((CRITICAL_SECTION *)(test_ff_entry[k]+0x8f0));*(void **)(test_ff_entry[k]+0x8e0)=test_ff_cell[k];
        *(void **)test_ff_fields[k]=test_ff_field_vt;InitializeCriticalSection((CRITICAL_SECTION *)(test_ff_fields[k]+0x20));test_ff_field_on[k]=1;
    }
    /* Cell 0's tree: fields[1] the root, fields[0] its left child. Cell 1's: fields[0] alone. */
    unsigned char *const header0=test_ff_entry[0]+0x10,*const header1=test_ff_entry[1]+0x10;
    *(void **)(header0+8)=test_ff_tree[1];*(void **)(test_ff_entry[0]+0x30)=test_ff_tree[0];
    *(void **)test_ff_tree[1]=header0;*(void **)(test_ff_tree[1]+8)=test_ff_tree[0];*(void **)test_ff_tree[0]=test_ff_tree[1];
    *(void **)(test_ff_tree[0]+0x20)=test_ff_fields[0];*(void **)(test_ff_tree[1]+0x20)=test_ff_fields[1];
    *(void **)(header1+8)=test_ff_tree[2];*(void **)(test_ff_entry[1]+0x30)=test_ff_tree[2];
    *(void **)test_ff_tree[2]=header1;*(void **)(test_ff_tree[2]+0x20)=test_ff_fields[0];
    for(unsigned k=0;k<3;k++) {
        *(void **)(test_ff_tree[k]+0x28)=test_ff_lists[k];*(void **)test_ff_lists[k]=test_ff_lists[k];*(void **)(test_ff_lists[k]+8)=test_ff_lists[k];
    }
    assert(rc_fill_next(test_ff_tree[0])==test_ff_tree[1] && rc_fill_next(test_ff_tree[1])==header0 && rc_fill_next(test_ff_tree[2])==header1);
    /* Cell 0, field 0 (radius 1000): in range, out of range, in range but consumed. Field 1 (radius 3000). Cell 1, field 0: in range. */
    test_ff_add(0,500,0);test_ff_add(0,1500,0);test_ff_add(0,900,1);
    test_ff_add(1,2000,0);test_ff_add(1,2500,0);test_ff_add(1,3500,0);
    test_ff_add(2,700,0);
    test_ff_cell_active=1;test_ff_variants[0]=NULL;test_ff_variants[1]=(void *)8;
    *(int32_t *)(test_ff_zone+0x1ec)=8;void *const saved_zone=rc_view_zone,*const saved_sector=rc_view_sector;rc_view_zone=test_ff_zone;rc_view_sector=test_ff_zone;
    const double now=12.5;memcpy(test_ff_tls+0x300,&now,8);const uint64_t before=0x1234;memcpy(test_ff_tls+0x318,&before,8);
    rc_fill_frames=rc_fill_cells=rc_fill_nodes=rc_fill_spawned=0;
    /* A class-0x33 zone or a zone below attention 8: the refreshes only. No zone, no thread block, no manager: nothing. */
    test_ff_zone_33=1;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==2);test_ff_zone_33=0;
    *(int32_t *)(test_ff_zone+0x1ec)=7;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==4);*(int32_t *)(test_ff_zone+0x1ec)=8;
    rc_view_zone=NULL;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==4);rc_view_zone=test_ff_zone;
    assert(rc_fill_frame_tls(test_ff_view,7,NULL) && test_ff_refreshes==4);
    *(void **)(image+RC_FILL_MANAGER)=NULL;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==4);*(void **)(image+RC_FILL_MANAGER)=test_ff_manager;
    assert(rc_fill_frames==2 && !test_ff_rows);
    /* A flagged or missing zone is left alone; an inactive one gets its refresh only; so does a field off or without variants. */
    *(uint32_t *)(test_ff_cell[0]+0x1e4)=0x20000;*(void **)(test_ff_entry[1]+0x8e0)=NULL;
    assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==4 && !test_ff_rows);
    *(uint32_t *)(test_ff_cell[0]+0x1e4)=0;*(void **)(test_ff_entry[1]+0x8e0)=test_ff_cell[1];
    test_ff_cell_active=0;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && test_ff_refreshes==6 && !test_ff_rows);test_ff_cell_active=1;
    test_ff_field_on[0]=test_ff_field_on[1]=0;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && !test_ff_rows);test_ff_field_on[0]=test_ff_field_on[1]=1;
    test_ff_variants[1]=NULL;assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls) && !test_ff_rows);test_ff_variants[1]=(void *)8;
    assert(!rc_fill_nodes && !rc_fill_spawned);
    /* The fill: each list's order, the key the cell's zone; the first insert makes the root, the next emplaces. Cell 1 waits: its field's tree holds cell 0's. */
    test_ff_refreshes=0;rc_fill_frames=rc_fill_cells=0;test_ff_word=0;
    assert(rc_fill_frame_tls(test_ff_view,7,test_ff_tls));
    assert(test_ff_refreshes==2 && test_ff_rows==3 && test_ff_nodes_made==2 && test_ff_emplaced==1 && test_ff_released==3);
    assert(test_ff_row[0].x==500 && test_ff_row[1].x==2000 && test_ff_row[2].x==2500);
    assert(test_ff_row[0].tree==test_ff_fields[0]+0x50 && test_ff_row[1].tree==test_ff_fields[1]+0x50 && test_ff_row[2].tree==test_ff_fields[1]+0x50);
    assert(test_ff_row[0].key==test_ff_cell[0] && test_ff_row[1].key==test_ff_cell[0] && test_ff_row[2].key==test_ff_cell[0]);
    for(unsigned k=0;k<2;k++) {
        assert(*(void **)(test_ff_fields[k]+0x58)==test_ff_new_node && *(void **)(test_ff_fields[k]+0x70)==test_ff_new_node && *(uint64_t *)(test_ff_fields[k]+0x78)==1);
    }
    assert(*(void **)test_ff_new_node==test_ff_fields[1]+0x50 && *(uint32_t *)(test_ff_new_node+0x18)==1);
    float left[8];assert(test_ff_left(0,left)==2 && left[0]==1500 && left[1]==900);assert(test_ff_left(1,left)==1 && left[0]==3500);assert(test_ff_left(2,left)==1 && left[0]==700);
    /* The word is the tick's while the fill runs and the caller's after. */
    uint64_t after;memcpy(&after,test_ff_tls+0x318,8);assert(after==before && test_ff_word==lockstep_seed(RC_FILL_DOMAIN,7,0,0,0,0));
    assert(rc_fill_frames==1 && rc_fill_cells==2 && rc_fill_nodes==5 && rc_fill_spawned==3);
    /* Both trees still pending: the next frame touches no list. Consumed: cell 1's node goes, keyed by its own zone. */
    assert(rc_fill_frame_tls(test_ff_view,8,test_ff_tls) && test_ff_rows==3 && rc_fill_nodes==5);
    test_ff_consume();
    assert(rc_fill_frame_tls(test_ff_view,9,test_ff_tls) && test_ff_rows==4 && test_ff_row[3].x==700 && test_ff_row[3].key==test_ff_cell[1] && rc_fill_nodes==8);
    assert(!test_ff_left(2,left));
    /* The view moves: the nodes now in range go, the consumed one stays. */
    test_ff_consume();test_ff_at[0]=1500;
    assert(rc_fill_frame_tls(test_ff_view,10,test_ff_tls) && test_ff_rows==6 && test_ff_row[4].x==1500 && test_ff_row[5].x==3500 && test_ff_released==6 && rc_fill_nodes==10);
    assert(test_ff_left(0,left)==1 && left[0]==900 && !test_ff_left(1,left));
    test_ff_at[0]=0;
    /* More entries than the copy holds: no pass at all, the caller stops the gate. */
    static void *many[RC_FILL_ENTRIES+1];*(void ***)(store+0x40)=many;*(void ***)(store+0x48)=many+RC_FILL_ENTRIES+1;
    const LONG64 frames=rc_fill_frames;assert(!rc_fill_frame_tls(test_ff_view,11,test_ff_tls) && rc_fill_frames==frames);
    *(void ***)(store+0x40)=entries;*(void ***)(store+0x48)=entries+2;
    /* A node's resource area: the word from its position alone, the caller's after, no thread block: the native call. */
    float pos[3]={1.0f,2.0f,3.0f},other[3]={1.0f,2.0f,4.0f};uint32_t bits[3];memcpy(bits,pos,12);
    uint64_t other_word=0x77;memcpy(test_ff_tls+0x318,&other_word,8);
    assert(rc_fill_area_tls(test_ff_tls,test_ff_zone,pos,test_ff_view,1,2,3,4,5,test_ff_at)==test_ff_cell[0]);
    const uint64_t first_word=test_ff_area_word;assert(first_word==lockstep_seed(RC_FILL_AREA_DOMAIN,((uint64_t)bits[0]<<32)|bits[1],bits[2],0,0,0));
    memcpy(&after,test_ff_tls+0x318,8);assert(after==other_word);
    other_word=0x88;memcpy(test_ff_tls+0x318,&other_word,8);
    rc_fill_area_tls(test_ff_tls,test_ff_zone,pos,test_ff_view,1,2,3,4,5,test_ff_at);assert(test_ff_area_word==first_word);
    rc_fill_area_tls(test_ff_tls,test_ff_zone,other,test_ff_view,1,2,3,4,5,test_ff_at);assert(test_ff_area_word!=first_word);
    memcpy(&after,test_ff_tls+0x318,8);assert(after==other_word);
    test_ff_area_word=0;assert(rc_fill_area_tls(NULL,test_ff_zone,pos,test_ff_view,1,2,3,4,5,test_ff_at)==test_ff_cell[0] && test_ff_area_word==other_word);
    /* The render pass's refresh: nothing while the fill runs, the native one in 'block'. */
    test_ff_refreshes=0;rc_fill_view=1;rc_fill_render_refresh(test_ff_entry[0]);assert(!test_ff_refreshes);
    rc_fill_view=0;EnterCriticalSection((CRITICAL_SECTION *)(test_ff_entry[0]+0x8f0));rc_fill_render_refresh(test_ff_entry[0]);LeaveCriticalSection((CRITICAL_SECTION *)(test_ff_entry[0]+0x8f0));assert(test_ff_refreshes==1);
    /* Status: the mode and the counters. */
    char status[3072];const unsigned saved_armed=rc_armed_groups;
    rc_armed_groups=1u<<RC_FIELD_FILL;rc_fill_view=1;assert(rc_status(status,sizeof(status))>0 && strstr(status," field_fill=view,frames:4,cells:8,nodes:10,spawned:6 "));
    rc_fill_view=0;assert(rc_status(status,sizeof(status))>0 && strstr(status," field_fill=block,frames:4,"));
    rc_armed_groups=0;assert(rc_status(status,sizeof(status))>0 && strstr(status," field_fill=off "));
    rc_armed_groups=saved_armed;
    /* The native functions behind the seam when no test replaces them. */
    rc_fill_test=NULL;const rc_fill_calls_t native=rc_fill_calls();
    assert((void *)native.position==image+0xee5b30u && (void *)native.emplace==image+0xf33dd0u && (void *)native.node==image+0x6976e0u &&
           (void *)native.construct==image+0xf35460u && (void *)native.release==image+0x174f310u && (void *)native.refresh==image+0xee6a80u &&
           (void *)native.area==image+0x78a690u);
    /* Cleanup. */
    for(unsigned k=0;k<3;k++) {
        unsigned char *head=test_ff_lists[k];
        for(unsigned char *p=*(unsigned char **)head,*next;p!=head;p=next) {next=*(unsigned char **)p;free(p);}
    }
    for(unsigned k=0;k<2;k++) {DeleteCriticalSection((CRITICAL_SECTION *)(test_ff_fields[k]+0x20));DeleteCriticalSection((CRITICAL_SECTION *)(test_ff_entry[k]+0x8f0));}
    DeleteCriticalSection((CRITICAL_SECTION *)(store+0x68));
    rc_gtr_native=NULL;rc_view_zone=saved_zone;rc_view_sector=saved_sector;
    *(void **)(image+RC_FILL_MANAGER)=saved_manager;*(void **)(image+RC_FILL_RADII)=saved_radii;
    rc_fill_frames=rc_fill_cells=rc_fill_nodes=rc_fill_spawned=0;
    puts("field fill zone guards, cell and field guards, one cell per field, tree walk and insert, node release, keyed words, render refresh, overflow and status verified");
}
/* traffic_rows: rows for queue growth inside and outside Zone::Move, the render pass's late and early queue, the thunk. */
static int test_tq_sims,test_tq_checks,test_tq_lates;static unsigned char *test_tq_grow;static char test_tq_member_says=1;
static void test_tq_sim(void *network,void *zone) {(void)network;(void)zone;++test_tq_sims;if(test_tq_grow) ++*(uint64_t *)(test_tq_grow+0x2b8);}
static char test_tq_check(void *network) {(void)network;++test_tq_checks;return 1;}
static void **test_tq_late(void *tree,void **out,void *record) {(void)tree;++test_tq_lates;*out=record;return out;}
static char test_tq_member(void *member) {(void)member;return test_tq_member_says;}
static void test_rc_traffic_rows(void) {
    static __declspec(align(16)) unsigned char network[0x400],first[0x400],second[0x400],zone[0x200];
    void *const vt=test_rc_vtable;memcpy(zone,&vt,8);*(uint64_t *)(zone+8)=0x777;
    memset(network,0,sizeof(network));memset(first,0,sizeof(first));memset(second,0,sizeof(second));
    *(void **)(first+0x100)=zone;*(void **)(second+0x100)=zone;
    *(void **)(network+0x40)=first;*(uint64_t *)(network+0x368)=100;*(void **)(network+0x48)=second;*(uint64_t *)(network+0x370)=1;
    unsigned char *objects[4];assert(tq_objects(network,objects,4)==2 && objects[0]==first && objects[1]==second);
    static unsigned char *outside[2];outside[0]=second;outside[1]=first; /* a separate array when the capacity is not 100 */
    *(uint64_t *)(network+0x368)=4;*(void **)(network+0x48)=outside;*(uint64_t *)(network+0x370)=2;
    assert(tq_objects(network,objects,4)==3 && objects[1]==second && objects[2]==first);
    *(uint64_t *)(network+0x368)=100;*(void **)(network+0x48)=second;*(uint64_t *)(network+0x370)=1;
    tq_reset();tq_sim_native=test_tq_sim;tq_check_native=test_tq_check;tq_late_native=test_tq_late;
    const LONG64 rows=rc_pick_rows;const unsigned ticks=rc_pick_rows_ticks;const uint64_t issued=stats.issued;
    rc_pick_rows_ticks=10;stats.issued=4;
    /* The first count is growth from nothing; none since then; Zone::Move's own queue; then growth outside it. */
    *(uint64_t *)(first+0x2b8)=2;assert(tq_check(network,zone)==1 && test_tq_checks==1 && tq_outside_count==1 && rc_pick_rows==rows+1);
    assert(tq_check(network,zone)==1 && tq_outside_count==1 && rc_pick_rows==rows+1);
    test_tq_grow=second;tq_sim(network,zone);assert(test_tq_sims==1 && tq_sim_count==1 && tq_outside_count==1 && rc_pick_rows==rows+2);
    const rc_pick_row_t *r=&rc_pick_ring[(uint64_t)(rows+1)%RC_PICK_ROWS];
    assert(r->site==0x10cu && r->a==0 && r->b==1 && r->pend==1 && r->tick==4 && r->runtime==0x777);
    ++*(uint64_t *)(first+0x2b8);test_tq_grow=NULL;tq_sim(network,zone);assert(tq_outside_count==2 && tq_sim_count==1);
    r=&rc_pick_ring[(uint64_t)(rows+2)%RC_PICK_ROWS];assert(r->site==0x10du && r->a==2 && r->b==3 && r->pend==0);
    --*(uint64_t *)(first+0x2b8);assert(tq_check(network,zone)==1 && tq_outside_count==2); /* shrinking is no row */
    /* The render pass: the late queue with the record's ID; the early queue only for a tracked member not yet queued. */
    static uint64_t record[2]={0x123456789aULL,0};void *slot=NULL;
    assert(tq_late(first+0x290,&slot,record)==&slot && slot==record && test_tq_lates==1 && tq_late_count==1);
    r=&rc_pick_ring[(uint64_t)(rows+3)%RC_PICK_ROWS];assert(r->site==0x10bu && r->a==0x3456789au && r->runtime==0x777);
    static void *member_vt[10];member_vt[9]=(void*)test_tq_member;static unsigned char member[0x20];void *mv=member_vt;memcpy(member,&mv,8);
    *(uint64_t *)(member+8)=0x55;
    assert(tq_tracked(member,first+0x290,first+0x290)==1 && tq_early_count==1);
    r=&rc_pick_ring[(uint64_t)(rows+4)%RC_PICK_ROWS];assert(r->site==0x10au && r->a==0x55 && r->b==1);
    assert(tq_tracked(member,&slot,first+0x290)==1 && tq_early_count==1); /* already queued */
    test_tq_member_says=0;assert(!tq_tracked(member,first+0x290,first+0x290) && tq_early_count==1);test_tq_member_says=1;
    stats.issued=11;assert(tq_late(first+0x290,&slot,record)==&slot && rc_pick_rows==rows+5); /* rows only within the ticks */
    /* The thunk. */
    unsigned char code[0x80];void *word;
    assert(tq_early_stub_bytes(code)==50 && !memcmp(code,"\x48\x89\xda\x4d\x89\xf0\x48\xb8",8));
    memcpy(&word,code+8,8);assert(word==(void*)tq_tracked);assert(!memcmp(code+16,"\xff\xd0\x84\xc0\x74\x0e\xff\x25\0\0\0\0",12));
    memcpy(&word,code+28,8);assert(word==image+TQ_EARLY_TRACKED);memcpy(&word,code+42,8);assert(word==image+TQ_EARLY_UNTRACKED);
    /* Status. */
    char status[3072];const unsigned saved_armed=rc_armed_groups;rc_armed_groups=1u<<RC_TRAFFIC_ROWS;
    assert(rc_status(status,sizeof(status))>0 && strstr(status," traffic_rows=early:1,late:2,sim:1,outside:2 "));
    rc_armed_groups=0;assert(rc_status(status,sizeof(status))>0 && strstr(status," traffic_rows=off "));rc_armed_groups=saved_armed;
    tq_sim_native=NULL;tq_check_native=NULL;tq_late_native=NULL;tq_reset();rc_pick_rows=rows;rc_pick_rows_ticks=ticks;stats.issued=issued;
    puts("traffic rows: network objects, growth inside and outside Zone::Move, render late and early queue, thunk and status verified");
}
/* traffic_pass: the pass per view point, the calls per mode and thread, the relative transform, the hook's thunk, status. */
static int test_tp_calls,test_tp_places,test_tp_filters,test_tp_draws,test_tp_natives;static DWORD test_tp_inside;static void *test_tp_view;
static DWORD test_tp_hook_inside;
static void test_tp_traffic(void *network,void *zone,void *view) {
    (void)zone;++test_tp_calls;test_tp_inside=tp_thread;test_tp_hook_inside=tp_hook_thread;test_tp_view=view;
    unsigned char *first=*(unsigned char **)((unsigned char *)network+0x40);if(first) {const double now=99.0;memcpy(first+0xe8,&now,8);}
}
static void test_tp_place(void *a,void *b,void *c,void *d) {(void)a;(void)b;(void)c;(void)d;++test_tp_places;}
static void *test_tp_filter(void *a,void *b,double c,double d,double e,void *f) {(void)b;(void)c;(void)d;(void)e;(void)f;++test_tp_filters;return a;}
static void test_tp_draw(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++test_tp_draws;}
static void *test_tp_relative(void *a,float *out,void *b,unsigned char flag) {(void)a;(void)b;(void)flag;++test_tp_natives;out[0]=-1;return out;}
static __declspec(align(16)) unsigned char test_tp_a[0x100],test_tp_b[0x100];
static void test_tp_gtr(void *self,float *out,double time,void *ancestor,unsigned char flag) {
    (void)time;(void)ancestor;(void)flag;
    static const float a[16]={10,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0},b[16]={4,0,0,0, 0,1,0,0, -1,0,0,0, 0,0,1,0};
    memcpy(out,self==test_tp_a?a:b,64);
}
static void test_rc_traffic_pass(void) {
    const unsigned saved_installed=rc_installed_groups;void *const saved_zone_parent=*(void **)(test_rc_zone+0x70);
    void *const vt=test_rc_vtable;test_rc_vtable[0x11e8/8]=(void*)test_rc_is_class;
    memcpy(test_rc_zone,&vt,8);memcpy(test_rc_sector,&vt,8);*(void **)(test_rc_zone+0x70)=test_rc_sector;*(void **)(test_rc_sector+0x70)=NULL;
    memcpy(test_tp_a,&vt,8);memcpy(test_tp_b,&vt,8);*(void **)(test_tp_a+0x70)=test_rc_sector;*(void **)(test_tp_b+0x70)=test_rc_sector;
    tq_reset();tp_reset();tq_check_native=test_tq_check;tp_traffic_native=test_tp_traffic;tp_place_native=test_tp_place;tp_filter_native=test_tp_filter;
    tp_draw_native=test_tp_draw;tq_late_native=test_tq_late;tp_relative_native=test_tp_relative;rc_gtr_native=test_tp_gtr;
    /* The camera 0x82f1e0 compares with: the cutscene manager's, else the universe's; the stand-ins once traffic_views is in. */
    static __declspec(align(16)) unsigned char manager[0x100],universe[0x400];static unsigned char camera_a,camera_b;
    void *const saved_cut=*(void **)(image+RC_CUTSCENE),*const saved_uni=*(void **)(image+RC_UNIVERSE);
    *(void **)(image+RC_CUTSCENE)=manager;*(void **)(image+RC_UNIVERSE)=universe;rc_installed_groups=0;
    *(void **)(manager+0xb0)=NULL;*(void **)(universe+0x3d0)=&camera_b;assert(tp_camera()==&camera_b);
    *(void **)(manager+0xb0)=&camera_a;assert(tp_camera()==&camera_a);
    test_rc_pages();rc_installed_groups=1u<<RC_TRAFFIC_VIEWS;*rc_manager_camera()=NULL;*rc_shadow_camera()=&camera_b;assert(tp_camera()==&camera_b);
    *rc_manager_camera()=&camera_a;assert(tp_camera()==&camera_a);*rc_manager_camera()=NULL;*rc_shadow_camera()=NULL;rc_installed_groups=0;
    /* The pass runs at the check, after it passes, with the pass group in: one call per view point in the zone's sector. */
    static __declspec(align(16)) unsigned char network[0x400];static unsigned char point_a,point_b;memset(network,0,sizeof(network));
    *(int32_t *)(test_rc_zone+0x1ec)=8;const LONG saved_count=rc_shape_count;
    rc_shape_points[0]=&point_a;rc_shape_sectors[0]=test_rc_sector;rc_shape_points[1]=&point_b;rc_shape_sectors[1]=test_rc_zone;
    InterlockedExchange(&rc_shape_count,2);
    tp_mode=TP_OBSERVE;assert(tq_check(network,test_rc_zone)==1 && !test_tp_calls); /* the group is not in */
    rc_installed_groups=1u<<RC_TRAFFIC_PASS;assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==1 && tp_zones==1 && tp_calls==1 && !tp_thread);
    assert(test_tp_inside==GetCurrentThreadId() && test_tp_view==&tp_views[0] && tp_views[0].slot[0]==tp_eyes[0] && tp_views[0].slot[1]==&point_a);
    assert(tp_views[0].slot[0x12]==&camera_a);for(unsigned k=0;k<0x90;k++) assert(!tp_eyes[0][k]);
    rc_shape_sectors[1]=test_rc_sector;assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==3 && tp_zones==2 && test_tp_view==&tp_views[1]);
    *(int32_t *)(test_rc_zone+0x1ec)=7;assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==5 && tp_zones==3);*(int32_t *)(test_rc_zone+0x1ec)=8; /* any attention */
    tp_mode=TP_OFF;assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==5);
    tp_mode=TP_OBSERVE;tp_thread=GetCurrentThreadId();assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==5);tp_thread=0; /* never nested */
    /* 'observe' puts the objects' last update times back and skips the hook; 'view' keeps the pass's time and runs it. */
    static __declspec(align(16)) unsigned char object[0x400];memset(object,0,sizeof(object));*(void **)(network+0x40)=object;
    const double before=5.0;double after;memcpy(object+0xe8,&before,8);rc_shape_sectors[1]=test_rc_zone;
    assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==6 && test_tp_hook_inside==GetCurrentThreadId() && !tp_hook_thread);
    memcpy(&after,object+0xe8,8);assert(after==5.0);
    tp_mode=TP_VIEW;assert(tq_check(network,test_rc_zone)==1 && test_tp_calls==7 && !test_tp_hook_inside);
    memcpy(&after,object+0xe8,8);assert(after==99.0);*(void **)(network+0x40)=NULL;rc_shape_sectors[1]=test_rc_sector;
    /* The writing calls per mode, ours (this thread in the pass) or the render pass's. */
    static void *tree_header;static uint64_t record[2]={0x77,0};void *slot=NULL;
    static void *member_vt[10];member_vt[9]=(void*)test_tq_member;static unsigned char member[0x20];void *mv=member_vt;memcpy(member,&mv,8);
    for(int mode=TP_OBSERVE;mode<=TP_VIEW;mode++) for(int ours=0;ours<2;ours++) {
        tp_mode=mode;tp_thread=ours?GetCurrentThreadId():0;const int skip=ours?mode==TP_OBSERVE:mode==TP_VIEW;
        const int places=test_tp_places,filters=test_tp_filters,draws=test_tp_draws,lates=test_tq_lates;
        tp_place_a(NULL,NULL,NULL,NULL);tp_place_b(NULL,NULL,NULL,NULL);assert(test_tp_places==places+(skip?0:2));
        assert(tp_filter_call(&slot,NULL,0,0,0,NULL)==(skip?NULL:(void*)&slot) && test_tp_filters==filters+(skip?0:1));
        tp_draw(0,0,0,0,0,0,0,0);assert(test_tp_draws==draws+(ours?0:1));
        slot=NULL;assert(tq_late(&tree_header,&slot,record)==&slot);
        if(ours && mode==TP_OBSERVE) assert(slot==&tree_header && test_tq_lates==lates);else assert(slot==record && test_tq_lates==lates+1);
        test_tq_member_says=0;assert(!tq_tracked(member,&tree_header,&tree_header));test_tq_member_says=1;
        assert(tq_tracked(member,&slot,&tree_header)==1);
        assert(tq_tracked(member,&tree_header,&tree_header)==(skip?0:1));
    }
    assert(tp_late[1]==2 && tp_late[0]==2 && tq_late_count==2 && tp_early[1]==2 && tp_early[0]==2 && tq_early_count==2);
    assert(tp_place[1]==2 && tp_place[0]==2 && tp_place_skipped==4 && tp_filter[1]==1 && tp_filter[0]==1 && tp_filter_skipped==2 && tp_draw_skipped==2);
    /* The relative transform: ours from the simulation in the common cluster, a's frame in b's; else native. */
    float out[16];tp_thread=0;assert(tp_relative_a(test_tp_a,out,test_tp_b,0)==out && out[0]==-1 && test_tp_natives==1);
    tp_thread=GetCurrentThreadId();assert(tp_relative_b(test_tp_a,out,test_tp_b,0)==out && test_tp_natives==1 && tp_relative==1);
    static const float expected[16]={0,-6,0,0, 0,-1,0,0, 1,0,0,0, 0,0,1,0};assert(!memcmp(out,expected,sizeof(expected)));
    static void *none_vt[0x11e8/8+1];void *nv=none_vt;memcpy(test_tp_b,&nv,8); /* b outside a's cluster: native */
    assert(tp_relative_c(test_tp_a,out,test_tp_b,0)==out && test_tp_natives==2);tp_thread=0;
    /* The hook's thunk: the two replaced moves, this thread's test, the call or past it. */
    unsigned char code[0x80];void *word;
    assert(tp_hook_stub_bytes(code)==59 && !memcmp(code,"\x41\x0f\x28\xd4\x48\x8b\xd6\x49\xbb",9));
    memcpy(&word,code+9,8);assert(word==(void*)&tp_hook_thread);
    assert(!memcmp(code+17,"\x45\x8b\x1b\x65\x44\x3b\x1c\x25\x48\0\0\0\x74\x0e\xff\x25\0\0\0\0",20));
    memcpy(&word,code+37,8);assert(word==image+TP_HOOK_CALL);memcpy(&word,code+51,8);assert(word==image+TP_HOOK_AFTER);
    /* Status. */
    char status[400];tp_mode=TP_VIEW;tp_status(status,sizeof(status));
    assert(strstr(status,"view,zones:5,calls:7,late:2/2,early:2/2,place:2/2/4,filter:1/1/2,draw_skip:2,relative:1,grown:0"));
    tp_reset();tp_status(status,sizeof(status));assert(!strcmp(status,"off"));
    tq_check_native=NULL;tq_late_native=NULL;tp_traffic_native=NULL;tp_place_native=NULL;tp_filter_native=NULL;tp_draw_native=NULL;tp_relative_native=NULL;
    rc_gtr_native=NULL;InterlockedExchange(&rc_shape_count,saved_count);rc_shape_points[0]=rc_shape_points[1]=NULL;tq_reset();
    *(void **)(image+RC_CUTSCENE)=saved_cut;*(void **)(image+RC_UNIVERSE)=saved_uni;*(void **)(test_rc_zone+0x70)=saved_zone_parent;
    rc_installed_groups=saved_installed;
    puts("traffic pass camera, per view point calls, writes per mode and thread, relative transform, hook thunk and status verified");
}
/* Stand-ins 2..3: a third and a fourth sector's first view point get a stand-in of their own when allowed. */
static __declspec(align(16)) unsigned char test_rcs_ship_c[0x200],test_rcs_sector_c[0x200],test_rcs_ship_d[0x200],test_rcs_sector_d[0x200];
static void *test_rcs_vtable[0x11e8/8+1];
static char test_rcs_class(void *o,int klass) {
    if(klass==0x58 && (o==test_rcs_sector_c || o==test_rcs_sector_d)) return 1;
    return test_rcp_class(o,klass);
}
static void test_rc_standins(void) {
    test_rcp_world();test_rcs_vtable[0x11e8/8]=(void*)test_rcs_class;void *vt=test_rcs_vtable;
    unsigned char *all[]={test_rcs_ship_c,test_rcs_sector_c,test_rcs_ship_d,test_rcs_sector_d};
    for(unsigned i=0;i<4;i++) {memset(all[i],0,0x200);memcpy(all[i],&vt,8);}
    void *sc=test_rcs_sector_c,*sd=test_rcs_sector_d,*cl=test_rcp_cluster;
    memcpy(test_rcs_ship_c+0x70,&sc,8);memcpy(test_rcs_ship_d+0x70,&sd,8);memcpy(test_rcs_sector_c+0x70,&cl,8);memcpy(test_rcs_sector_d+0x70,&cl,8);
    *(uint64_t*)(test_rcs_ship_c+8)=0x5ca83;*(uint64_t*)(test_rcs_ship_d+8)=0x5ca84;
    void *saved_points[VIEW_MAX];uint64_t saved_ids[VIEW_MAX];memcpy(saved_points,view_points,sizeof(saved_points));memcpy(saved_ids,view_ids,sizeof(saved_ids));
    const unsigned saved_count=view_count,saved_latched=view_latched;const uint64_t saved_issued=stats.issued;const DWORD saved_ui=ui_thread;
    const uint32_t saved_active=stats.active;const int saved_installed=rc_installed,saved_policy=rc_policy;
    test_rc_pages();rc_installed=1;stats.active=1;ui_thread=GetCurrentThreadId();view_latched=1;rc_policy=1;rc_more=0;rc_standins_max=2;
    view_points[0]=test_rcp_ship_a;view_ids[0]=0x5ca81;view_points[1]=test_rcp_ship_b;view_ids[1]=0x5ca82;
    view_points[2]=test_rcs_ship_c;view_ids[2]=0x5ca83;view_points[3]=test_rcs_ship_d;view_ids[3]=0x5ca84;view_count=4;
    /* Two (the default): sectors c and d stay uncovered and answer as view point 1. */
    stats.issued=30;assert(rc_frame(30) && rc_more==0 && rc_uncovered_now==2 && !*rc_k_manager_camera(2) && !*rc_k_manager_camera(3));
    assert(rc_object_k(test_rcp_ship_b,RC_OBJ_R12)==1 && rc_object_k(test_rcs_ship_c,RC_OBJ_R12)==0);
    /* Arming: only before the first admitted frame, 2..4, with the policy. */
    assert(!rc_standins_arm(3));stats.issued=0;assert(!rc_standins_arm(1) && !rc_standins_arm(5));
    rc_policy=0;assert(!rc_standins_arm(4));rc_policy=1;assert(rc_standins_arm(4) && rc_standins_max==4);
    /* Four: each further sector gets the next stand-in, its camera at its view point. */
    stats.issued=31;assert(rc_frame(31) && rc_more==2 && !rc_uncovered_now);
    void *c2=*rc_k_manager_camera(2),*c3=*rc_k_manager_camera(3),*p2,*s2;
    assert(c2==rc_page+rc_k_cameras[2] && c3==rc_page+rc_k_cameras[3]);
    memcpy(&p2,rc_page+rc_k_shadow[2]+0x230,8);memcpy(&s2,rc_page+rc_k_shadow[2]+0x3d0,8);assert(p2==test_rcs_ship_c && s2==c2);
    memcpy(&p2,(unsigned char*)c3+0x770,8);assert(p2==test_rcs_ship_d && rc_k_sector[3]==test_rcs_sector_d && rc_k_cluster[3]==test_rcp_cluster);
    assert(rc_object_k(test_rcs_ship_c,RC_OBJ_R12)==2 && rc_object_k(test_rcs_ship_d,RC_OBJ_R12)==3 && rc_object_k(test_rcp_ship_b,RC_OBJ_R12)==1);
    assert(rc_object_k(test_rcp_ship_a,RC_OBJ_R12)==0 && rc_object_two(test_rcs_ship_d,RC_OBJ_R12));
    assert(rc_camera_k(c2)==2 && rc_camera_k(c3)==3 && rc_camera_k(*rc_manager2_camera())==1 && !rc_camera_k(*rc_manager_camera()) && rc_is_camera2(c3));
    unsigned site=RC_SITES;for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object==RC_OBJ_R12 && site==RC_SITES) site=i;
    assert(site<RC_SITES && rc_pick(test_rcs_ship_d,site)==rc_page+rc_k_manager[3] && rc_pick(test_rcs_ship_c,site)==rc_page+rc_k_manager[2]);
    void *answer=NULL;rc_camera_sector(NULL,&answer,test_rcs_ship_c);assert(answer==test_rcp_cluster);
    assert(rc_owned());
    /* The next frame writes the other buffer; three view points: one further stand-in, the fourth unused. */
    stats.issued=32;assert(rc_frame(32) && *rc_k_manager_camera(2)==rc_page+rc_k_cameras[2]+RC_CAMERA_SIZE);
    view_count=3;stats.issued=33;assert(rc_frame(33) && rc_more==1 && !*rc_k_manager_camera(3) && rc_object_k(test_rcs_ship_d,RC_OBJ_R12)==0);
    memcpy(&p2,rc_page+rc_k_shadow[3]+0x230,8);assert(p2==*rc_shadow_player());
    /* Two view points in one further sector: the first in slot order holds it. */
    view_points[3]=test_rcs_ship_c;view_ids[3]=0x5ca83;view_count=4;stats.issued=34;assert(rc_frame(34) && rc_more==1);
    char status[3072];assert(rc_status(status,sizeof(status))>0 && strstr(status," standins=3/4 "));
    /* View point 2 in view point 1's sector: no further stand-ins either. */
    view_points[1]=test_rcp_ship_a;view_points[2]=test_rcp_ship_a;view_count=3;stats.issued=35;assert(rc_frame(35) && rc_more==0);
    memcpy(view_points,saved_points,sizeof(saved_points));memcpy(view_ids,saved_ids,sizeof(saved_ids));view_count=saved_count;view_latched=saved_latched;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;rc_installed=saved_installed;rc_policy=saved_policy;rc_more=0;rc_standins_max=2;
    puts("viewpoint stand-ins: arming, further sectors in slot order, cameras and shadows, object and camera mapping, picks, buffers, status verified");
}
static void test_region_camera_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=rc_page,*saved_stubs=rc_stubs;rc_page=rc_stubs=NULL;test_rc_reset();
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0); /* every load reads 0x3d19900 or g_Universe */
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==0); /* e8 calls of the camera zone, sector and active camera */
    assert(rc_pins_ok((1u<<RC_GROUPS)-1));
    /* field_fill: what the fill reproduces. The camera compare and the consumed flag, the insert, the node's free, the calls. */
    assert(!memcmp(image+0xeec70f,"\x48\x39\x88\x90\x00\x00\x00",7) && !memcmp(image+0xeec71c,"\x80\xbb\x90\x00\x00\x00\x00",7));
    assert(!memcmp(image+0xeec9aa,"\x49\x8d\x4f\x50\xe8",5) && !memcmp(image+0xeec9c5,"\x4c\x89\x58\x08\x4c\x89\x58\x20\x49\x89\x03",11));
    assert(!memcmp(image+0xeec9e2,"\x49\xc7\x47\x78\x01\x00\x00\x00\x41\xc7\x43\x18\x01\x00\x00\x00",16));
    assert(!memcmp(image+0xeebed8,"\x49\xff\x4c\x24\x30\xba\xa0\x00\x00\x00",10));
    /* traffic_rows: Zone::Move's two calls (rcx the network, r14 the zone), the tracked test and its branch, the late queue's record. */
    assert(!memcmp(image+0x834c8d,"\x48\x85\xc9\x74\x17\x49\x8b\xd6\xe8",9) && !memcmp(image+0x834ca2,"\x49\x8b\x8e\xf0\x04\x00\x00\xe8",8));
    assert(!memcmp(image+0x8abf21,"\x48\x8b\x4e\x20\x48\x85\xc9\x74\x0a\x48\x8b\x01\xff\x50\x48\x84\xc0\x75\x12",19));
    assert(!memcmp(image+0x8abf34,"\x40\x84\xff",3) && !memcmp(image+0x8abf46,"\x49\x3b\xde",3));
    assert(!memcmp(image+0x8ac59e,"\x4c\x8d\x85\x60\x01\x00\x00",7)); /* r8 = the record, its first word the member's ID */
    /* traffic_pass: the hook's moves and call, the view's two words read, 0x82f1e0's compare with the view's camera. */
    assert(!memcmp(image+0x8abd6f,"\x41\x0f\x28\xd4\x48\x8b\xd6\xff\x90\x80\x00\x00\x00",13));
    assert(!memcmp(image+0x82f24f,"\x49\x39\x85\x90\x00\x00\x00",7));
    assert(!memcmp(image+0x8acc97,"\xf2\x45\x0f\x11\xa7\xe8\x00\x00\x00",9)); /* the object's last update time */
    /* traffic_views: the two loads feed the compare with the view's camera (+0x90), whose result is the player-view flag. */
    assert(!memcmp(image+0xeec6f0,"\x48\x8b\x05",3)); /* (field fill's load, unchanged) */
    assert(!memcmp(image+0x82f22b,"\x48\x85\xc0\x74\x0c\x48\x8b\x80\xb0\x00\x00\x00",12));
    assert(!memcmp(image+0x82f243,"\x48\x85\xc0\x74\x07\x48\x8b\x80\xd0\x03\x00\x00\x49\x39\x85\x90\x00\x00\x00\x40\x0f\x94\xc5",23));
    {int32_t rel;assert(image[0x82f207]==0xe8);memcpy(&rel,image+0x82f208,4);assert(0x82f20c+rel==0x83ef20);
     assert(image[0x842a58]==0xe8);memcpy(&rel,image+0x842a59,4);assert(0x842a5d+rel==0x82f1e0);}
    /* An entry's +0x8e0 is the zone 0x785db0 gives at the cell's centre; the node is tested against the zone's box and the zone's activity. */
    assert(!memcmp(image+0xee7cea,"\x49\x8d\x8d\xe0\x08\x00\x00",7) && !memcmp(image+0xee78d0,"\x48\x8b\x8b\xe0\x08\x00\x00",7));
    assert(!memcmp(image+0xeec95a,"\x8b\x97\xec\x01\x00\x00\x48\x8b\xcf\xff\x90\x80\x15\x00\x00",15));
    static const struct {unsigned at,target;} fill_calls[]={{0xeec9ae,0xf33dd0},{0xeec9b9,0x6976e0},{0xeec9dd,0xf35460},{0xeebee5,0x174f310},{0xeeb98a,0xee6a80},{0xeebf37,0xee5b30},{0xee6d32,0x78a690},{0xee7c7a,0x785db0}};
    for(unsigned i=0;i<sizeof(fill_calls)/sizeof(*fill_calls);i++) {
        int32_t rel;assert(image[fill_calls[i].at]==0xe8);memcpy(&rel,image+fill_calls[i].at+1,4);assert(fill_calls[i].at+5+rel==fill_calls[i].target);
    }
    /* What each load feeds: the manager's camera (+0xb0) or the player (+0x230) / player camera (+0x3d0). */
    assert(!memcmp(image+0xeea970,"\x4d\x8b\x81\xb0\x00\x00\x00",7) && !memcmp(image+0xeeaa6f,"\x49\x8b\x89\xb0\x00\x00\x00",7) &&
           !memcmp(image+0x9fcc51,"\x48\x8b\x89\xb0\x00\x00\x00",7));
    assert(!memcmp(image+0xedc5f7,"\x48\x8b\x89\xb0\x00\x00\x00",7) && !memcmp(image+0x83f367,"\x4d\x8b\x80\xb0\x00\x00\x00",7) &&
           !memcmp(image+0x8be867,"\x48\x8b\x92\xb0\x00\x00\x00",7) && !memcmp(image+0x8ab365,"\x48\x83\xb8\xb0\x00\x00\x00\x00",8));
    assert(!memcmp(image+0xee087d,"\x48\x05\x30\x02\x00\x00",6) && !memcmp(image+0xee0945,"\x4c\x8d\xb0\x30\x02\x00\x00",7) &&
           !memcmp(image+0xee28f1,"\x48\x05\x30\x02\x00\x00",6) && !memcmp(image+0x8ab2b3,"\x48\x05\x30\x02\x00\x00",6));
    assert(!memcmp(image+0x8be7ba,"\x48\x8b\x88\xd0\x03\x00\x00",7) && !memcmp(image+0x8be878,"\x48\x8b\x90\xd0\x03\x00\x00",7));
    /* Scene choice: Zone::vfunc943 returns scene A (0x38f0748) for the player's class-0x58 ancestor and B (0x38f0740)
     * for the cutscene camera's; Highway::vfunc943 the same; the three manager loads and five active-camera calls. */
    assert(image[0x83ff21]==0x48 && 0x83ff28u+*(int32_t*)(image+0x83ff24)==0x38f0748u &&
           image[0x83ff92]==0x48 && 0x83ff99u+*(int32_t*)(image+0x83ff95)==0x38f0740u);
    assert(0x5f97ddu+*(int32_t*)(image+0x5f97d9)==0x38f0748u && 0x5f981au+*(int32_t*)(image+0x5f9816)==0x38f0740u &&
           0x5f9934u+*(int32_t*)(image+0x5f9930)==0x38f0740u);
    assert(!memcmp(image+0x83ff4d,"\x48\x8b\x89\xb0\x00\x00\x00",7) && !memcmp(image+0x5f971d,"\x48\x8b\x89\xb0\x00\x00\x00",7) &&
           !memcmp(image+0x5f98eb,"\x48\x8b\x89\xb0\x00\x00\x00",7));
    /* Highway::vfunc674: the player object (+0x230) for the sector guard, the player camera (+0x3d0) whose
     * GetOffset relative to the highway (0x5f4d7a) gives the spline parameter set by 0x5f7a50. */
    assert(!memcmp(image+0x5f4a91,"\x48\x05\x30\x02\x00\x00",6) && !memcmp(image+0x5f4d61,"\x48\x8b\x89\xd0\x03\x00\x00",7) &&
           image[0x5f4d7a]==0xe8 && 0x5f4d7fu+*(int32_t*)(image+0x5f4d7b)==0x97a300u && !memcmp(image+0x5f4d77,"\x4c\x8b\xcf",3) &&
           image[0x5f4fee]==0xe8 && 0x5f4ff3u+*(int32_t*)(image+0x5f4fef)==0x5f7a50u);
    /* highway_rows: 0x5f7a50(highway rcx -> r14, parameter xmm1 -> xmm6) stores +0x560 and the spline position +0x570; its callers
     * pass the highway in rcx and the parameter in xmm1 (movaps xmm1, xmm9; mov rcx, rdi / movss xmm1, [rax]; mov rcx, rbx). */
    assert(!memcmp(image+0x5f7a6c,"\x0f\x28\xf1\x4c\x8b\xf1",6) && !memcmp(image+0x5f7aaa,"\xf3\x41\x0f\x11\xb6\x60\x05\x00\x00",9) &&
           !memcmp(image+0x5f4fe7,"\x41\x0f\x28\xc9\x48\x8b\xcf",7) && !memcmp(image+0x9fe3f6,"\xf3\x0f\x10\x08\x48\x8b\xcb",7));
    /* highway_speed: vfunc8 takes the ship (model+0x10 -> +0xef0) and its runtime ID (+8) into rbp, the factor comes back in xmm0, then
     * movabs rax, multiplier (0xeb0a2c, only reached by the jae at 0xeb0a10); imul rbp, rax; movabs rax, increment; add rax, rbp; ror rax, 30;
     * the low word to a float times 2^-32 times xmm0, stored at +0x80. The local tube's factor call precedes it. */
    assert(!memcmp(image+0xeb09e1,"\x48\x8b\x46\x10\x48\x8b\x90\xf0\x0e\x00\x00\x48\x8b\x6a\x08",15) && !memcmp(image+0xeb0a10,"\x73\x1a",2) &&
           !memcmp(image+0xeb0a2c,"\x48\xb8\x2d\x7f\x95\x4c\x2d\xf4\x51\x58\x48\x0f\xaf\xe8\x48\xb8\x4f\x81\x67\xf7\x7e\x7b\x05\x14\x48\x03\xc5\x48\xc1\xc8\x1e\x8b\xc0",33) &&
           !memcmp(image+0xeb0a5d,"\xf3\x0f\x59\xf0\xf3\x0f\x11\xb6\x80\x00\x00\x00",12) && image[0xeb0a05]==0xe8 && 0xeb0a0au+*(int32_t*)(image+0xeb0a06)==0x605250u);
    assert(!memcmp(image+0xeb0a2c,rc_jumps[1].original,7) && rc_jumps[1].resume==0xeb0a36u);
    /* The flight step: the ship (model+0x10 -> +0xef0) in rax, xmm1 the range, the ID into the local state [rsp+0x38], then FloatState(&state, range)
     * twice; the bump: the ship in rcx, the ID into rdx with its sign tested, xmm4 the sine, xmm1 zero, the only values live past the sine's call. */
    assert(!memcmp(image+0xeb0ef7,"\x48\x8b\x80\xf0\x0e\x00\x00",7) && !memcmp(image+0xeb0f10,"\x41\x0f\x28\xcf",4) &&
           !memcmp(image+0xeb0f19,rc_jumps[2].original,7) && !memcmp(image+0xeb0f20,"\x24\x38\x48\x8d\x4c\x24\x38",7) &&
           image[0xeb0f27]==0xe8 && 0xeb0f2cu+*(int32_t*)(image+0xeb0f28)==0x14ddc90u && image[0xeb0f44]==0xe8 && 0xeb0f49u+*(int32_t*)(image+0xeb0f45)==0x14ddc90u);
    assert(image[0xeb107c]==0xe8 && !memcmp(image+0xeb1085,"\x0f\x28\xe0\x0f\x57\xc9\x48\x8b\x88\xf0\x0e\x00\x00",13) &&
           !memcmp(image+0xeb1092,rc_jumps[3].original,7) && !memcmp(image+0xeb1099,"\x78\x07",2) && rc_jumps[3].resume==0xeb1099u && rc_jumps[2].resume==0xeb0f22u);
    /* The highway frame carries the parameter: GetLocalTransform 0x5f79f0 adds +0x570, which 0x5f7a50 sets. */
    assert(*(uint64_t*)(image+0x2b091e0+0x1440)==0x140000000ULL+0x5f79f0u && !memcmp(image+0x5f7a37,"\x0f\x58\x81\x70\x05\x00\x00",7) &&
           !memcmp(image+0x5f7ac7,"\x41\x0f\x11\x86\x70\x05\x00\x00",8));
    /* UniverseFrameUpdate: the controllable (0x9fe2e0), its class-0x33 (Highway) parent with attention >= 8, the
     * controllable again (0x9fe337), its controller transform and 0x5f7a50 (0x9fe3fd). */
    assert(!memcmp(image+0x9fe2dc,"\x48\x8d\x55\xd0",4) && !memcmp(image+0x9fe306,"\xba\x33\x00\x00\x00",5) &&
           !memcmp(image+0x9fe326,"\x83\xbb\xec\x01\x00\x00\x08",7) && image[0x9fe3fd]==0xe8 && 0x9fe402u+*(int32_t*)(image+0x9fe3fe)==0x5f7a50u &&
           !memcmp(image+0x9f9dcf,"\x48\x8b\x8b\x80\x07\x00\x00",7) && !memcmp(image+0x9f9e11,"\x48\x8b\x9b\x70\x07\x00\x00",7));
    /* SetParent's camera context test: g_Universe (0x6ce5cc), its player camera (+0x3d0), the camera's anchor (+0x770)
     * walked up +0x70 against the re-parented object ([rbp+0x510]), then 0x97dff0(camera, old rdi, new rbx). */
    assert(!memcmp(image+0x6ce5d8,"\x48\x8b\x88\xd0\x03\x00\x00",7) && !memcmp(image+0x6ce5e4,"\x48\x8b\x81\x70\x07\x00\x00",7) &&
           !memcmp(image+0x6ce5f0,"\x48\x8b\x95\x10\x05\x00\x00",7) && !memcmp(image+0x6ce5fc,"\x48\x8b\x40\x70",4) &&
           !memcmp(image+0x6ce607,"\x4c\x8b\xc3\x48\x8b\xd7",6));
    /* 0x97dff0 keeps camera / old / new (rcx, rdx, r8) and checks the camera against the manager's +0xb0/+0xb8 and the player camera. */
    assert(!memcmp(image+0x97dff0,"\x48\x89\x54\x24\x10\x48\x89\x4c\x24\x08",10) && !memcmp(image+0x97e015,"\x4d\x8b\xe0\x48\x8b\xfa",6) &&
           !memcmp(image+0x97e400,"\x48\x3b\x90\xb0\x00\x00\x00",7) && !memcmp(image+0x97e41e,"\x48\x8b\x88\xd0\x03\x00\x00",7));
    /* Policy objects: 0x83ef20's component (r12 = rcx), 0x8ab1e0's traffic object (r12 = rcx; its zone +0x108, else +0x100),
     * 0x8be450's component (rdi, set per loop), Highway::vfunc674's highway (rdi = rcx), then never written before the sites;
     * vfunc674 compares sectors (class 0x58). The two calls' moves read r12; 0x8ab40a's r8 is set after it. */
    assert(!memcmp(image+0x83ef37,"\x4c\x8b\xe1",3) && !memcmp(image+0x8ab217,"\x4c\x8b\xe1",3) && !memcmp(image+0x8be55d,"\x48\x8b\xf8",3) &&
           !memcmp(image+0x5f4a51,"\x48\x8b\xf9",3) && !memcmp(image+0x5f4ab4,"\xba\x58\x00\x00\x00",5));
    assert(!memcmp(image+0x8ab41e,"\x4d\x8b\x84\x24\x08\x01\x00\x00",8) && !memcmp(image+0x8ab42b,"\x4d\x8b\x84\x24\x00\x01\x00\x00",8) &&
           !memcmp(image+0x8ab40f,"\x4c\x8b\xc3",3) && !memcmp(image+0x83f2fb,"\x48\x8d\x55\xc7",4) && !memcmp(image+0x8be884,"\x48\x8b\xcf",3));
    /* The policy's position calls: 0x3e35d0(component r12, out, camera) at 0x83f26f; 0x979ef0(camera, out [rbp-0x70], frame zone rbx)
     * at 0x8ab419; GetTransformRelativeTo 0x3db8a0 takes the ancestor in r9 (cmp r9, rcx). */
    assert(!memcmp(image+0x83f268,"\x48\x8d\x55\xd7\x49\x8b\xcc",7) && !memcmp(image+0x8ab40f,"\x4c\x8b\xc3\x48\x8d\x55\x90\x48\x8b\xc8",10) &&
           !memcmp(image+0x3db8a0,"\x4c\x3b\xc9\x0f\x84",5));
    /* GetTransformRelativeTo composes into the caller's matrix: each object's slot 0x1440 gets the same out (r12)
     * (mov rdx, r12; mov rcx, rbx; call [rax+0x1440]); self == ancestor returns at once. The identity PosRot its
     * callers set: translation 0, then the rows at 0x22c01d0..0x22c01f0 (rc_gtr starts from the same). */
    assert(!memcmp(image+0x3db99e,"\x49\x8b\xd4\x48\x8b\xcb\xff\x90\x40\x14\x00\x00",12));
    {
        const float rows[12]={1,0,0,0,0,1,0,0,0,0,1,0};assert(!memcmp(image+0x22c01d0,rows,sizeof(rows)));
    }
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object) { /* each policy site loads rax, rcx, rdx or r8 */
        unsigned reg=((rc_sites[i].prefix[2]>>3)&7u)|(rc_sites[i].prefix[0]&4u?8u:0u);assert(reg==0 || reg==1 || reg==2 || reg==8);
    }
    /* Effect creation: the camera offset helper (only called from 0xd717a0) reads the manager's camera; the cull counts. */
    assert(!memcmp(image+0x9fcbd1,"\x48\x8b\x89\xb0\x00\x00\x00",7) && image[0xd723e8]==0xe8 && 0xd723ed+*(int32_t*)(image+0xd723e9)==0x9fcb90 &&
           !memcmp(image+0xd7241f,"\xff\x05\x03\xd9\xd6\x05\xff\x05\x01\xd9\xd6\x05",12) &&
           0xd72425u+*(int32_t*)(image+0xd72421)==RC_EFFECT_ATTEMPTS && 0xd7242bu+*(int32_t*)(image+0xd72427)==RC_EFFECT_CULLS);
    /* 0x979ef0's bank layout and GetOffset's anchor, +0x778, mode table and local PosRot. */
    assert(!memcmp(image+0x979f20,"\x48\x69\xd8\xb0\x02\x00\x00",7) && !memcmp(image+0x979f2a,"\x4c\x8d\x4b\x40",4) &&
           !memcmp(image+0x979f2e,"\x49\x83\xb9\x80\x02\x00\x00\x08",8) && !memcmp(image+0x979f4c,"\x49\x8b\x89\x90\x02\x00\x00",7) &&
           !memcmp(image+0x979f71,"\x48\x8b\x4b\x30",4) && RC_BANK==0x2b0);
    assert(!memcmp(image+0x97a313,"\x4c\x8b\x81\x70\x07\x00\x00",7) && !memcmp(image+0x97a337,"\x48\x8b\x99\x78\x07\x00\x00",7) &&
           !memcmp(image+0x97a34b,"\x48\x63\x81\x68\x08\x00\x00",7) && *(uint32_t*)(image+0x97a6c4)==0x97a36eu &&
           !memcmp(image+0x97a5b9,"\x0f\x10\xae\x90\x05\x00\x00",7));
    /* Shape refresh: Body_UpdateShape calls 0xc2c00(holder [rbx+0x20], owner [rbx], force dil) at 0xbffb7; 0xc2c00 refreshes
     * when the due time [rsi+0x28] <= the thread's game time [r15+0x300], calls the interval 0xc2810(holder rsi, owner rdi) at
     * 0xc2dac and stores now + interval at +0x28; 0xc2810: the macro flag (0x7ad890 of owner+0x60, +0x278), the compound's bounds
     * (holder+0x10 -> +8, slot 0x18), Random::FloatState 0x14ddc90(TLS+0x318, 0.1) and the constants 1e-4, 5, 0.5. */
    assert(!memcmp(image+0xbffac,"\x48\x8b\x13\x44\x0f\xb6\xc7\x48\x8b\x4b\x20\xe8",12));
    assert(!memcmp(image+0xc2c77,"\xf2\x0f\x10\x46\x28\x33\xed\x66\x43\x0f\x2f\x04\x3e\x0f\x86",15));
    assert(!memcmp(image+0xc2da6,"\x48\x8b\xd7\x48\x8b\xce\xe8",7) &&
           !memcmp(image+0xc2db1,"\x0f\x57\xc9\xf3\x0f\x5a\xc8\xf2\x43\x0f\x58\x0c\x3e\xf2\x0f\x11\x4e\x28",18));
    assert(!memcmp(image+0xc2846,"\x48\x8b\x4a\x60\xe8",5) && 0xc284fu+*(int32_t*)(image+0xc284b)==0x7ad890u &&
           !memcmp(image+0xc284f,"\x38\x98\x78\x02\x00\x00",6));
    assert(!memcmp(image+0xc2b30,"\x48\x8b\x45\x67\x48\x8b\x40\x10\x48\x8b\x48\x08\x48\x8b\x01\x48\x8d\x55\xe7\xff\x50\x18",22));
    assert(!memcmp(image+0xc2bb2,"\xb9\x18\x03\x00\x00\x49\x03\xcf",8) && image[0xc2bc2]==0xe8 && 0xc2bc7u+*(int32_t*)(image+0xc2bc3)==0x14ddc90u);
    {
        float c;memcpy(&c,image+0x2cbe2a8,4);assert(c==0.1f);memcpy(&c,image+0x2cbeb7c,4);assert(c==5.0f);
        memcpy(&c,image+0x2cbe430,4);assert(c==0.5f);memcpy(&c,image+0x2cbe0ac,4);assert(c==0.0001f);
    }
    /* Mining: 0x7884a0 walks the region (rcx -> r15) up to its class-0xf ancestor into rdi, keeps the position (r8 -> r13) and the
     * frame (r9 -> rsi); the jump site loads g_Universe; the camera block (cutscene manager +0xb0, else the player camera) ends in
     * 0x979ef0(camera, &pos, frame) and the 2.5e9 compare; the no-reference path stores 0x18 then 0x17 at [rbp+0x1e8] with r12d 2
     * and [rsp+0x60] 0; 0x78871e xor bl,bl then 0x788720 test r14b,1 (release when [rsp+0x60] is set). */
    assert(!memcmp(image+0x7884d2,"\x49\x8b\xf1\x4d\x8b\xe8\x4c\x8b\xf9",9) && !memcmp(image+0x7884e5,"\x48\x8b\x79\x70",4) &&
           !memcmp(image+0x7884f3,"\xba\x0f\x00\x00\x00",5) && image[0x788503]==0x75 && 0x788505u+image[0x788504]==0x78854au);
    assert(!memcmp(image+0x78854a,rc_jumps[0].original,7) && 0x788551u+*(int32_t*)(image+0x78854d)==RC_UNIVERSE);
    assert(!memcmp(image+0x7885a0,"\x41\xbc\x02\x00\x00\x00",6) && !memcmp(image+0x7885ed,"\x33\xc0\x48\x89\x44\x24\x60\x33\xdb",9) &&
           !memcmp(image+0x78862d,"\x41\xbe\x16\x00\x00\x00",6) && !memcmp(image+0x788674,"\x41\x83\xce\x01\x44\x89\xb5\xe8\x01\x00\x00",11));
    assert(image[0x7886ea]==0xe8 && 0x7886efu+*(int32_t*)(image+0x7886eb)==0x979ef0u && !memcmp(image+0x7886e3,"\x4c\x8b\xc6",3) &&
           !memcmp(image+0x7886f3,"\x41\x0f\x5c\x55\x00",5) && 0x788718u+*(int32_t*)(image+0x788714)==0x2cbef14u);
    { float c;memcpy(&c,image+0x2cbef14,4);assert(c==2.5e9f);memcpy(&c,image+0x2cbeee8,4);assert(c==4.0e6f); }
    assert(!memcmp(image+0x78871e,"\x32\xdb\x41\xf6\xc6\x01\x74\x32\x48\x8b\x44\x24\x60",13) && !memcmp(image+0x788758,"\x84\xdb",2));
    /* XS: vfunc876 keeps the ship in rsi, asks the soft target (0x7e5346: lea rdx,[rsp+0x50]; the target compared with rsi), then
     * IsExternalViewActive (0x7e5412) and the ship's (rsi -> rcx) camera distance (0x7e541e) against 4e6. */
    assert(!memcmp(image+0x7e5321,"\x48\x8b\xf1",3) && !memcmp(image+0x7e5341,"\x48\x8d\x54\x24\x50",5) && !memcmp(image+0x7e534b,"\x48\x39\x30",3));
    assert(!memcmp(image+0x7e541b,"\x48\x8b\xce",3) && !memcmp(image+0x7e5423,"\x0f\x2f\x05",3) && 0x7e542au+*(int32_t*)(image+0x7e5426)==0x2cbeee8u &&
           !memcmp(image+0x7e5417,"\x84\xc0\x74\x23",4));
    /* Crystal pass: the distance (object rdi, player camera rdx) against xmm7, then the respawn call 0x9082c0 when farther. */
    assert(!memcmp(image+0x73f6a0,"\x48\x8b\xcf",3) && !memcmp(image+0x73f6a8,"\x0f\x2f\xc7\x76\x0f",5) && image[0x73f6b7]==0xe8 &&
           0x73f6bcu+*(int32_t*)(image+0x73f6b8)==0x9082c0u);
    /* Tampering in a pinned block or a site's opcode refuses. */
    for(unsigned p=0;p<RC_PINS;p++) {unsigned at=rc_pins[p].rva+rc_pins[p].size/2;image[at]^=1;assert(!rc_pins_ok(1u<<rc_pins[p].group));image[at]^=1;}
    for(unsigned i=0;i<RC_SITES;i++) {image[rc_sites[i].rva+1]^=0x40;assert(rc_site_state(i)<0 && !rc_pins_ok(1u<<rc_sites[i].group));image[rc_sites[i].rva+1]^=0x40;}
    assert(rc_pins_ok((1u<<RC_GROUPS)-1));
    test_view_scenes_native_image(image);
    /* The vtable entries as the loader would have them: this copy is not relocated. */
    *(void**)(image+VSC_ZONE_SLOT)=image+VSC_ZONE;*(void**)(image+VSC_HIGHWAY_SLOT)=image+VSC_HIGHWAY;
    /* Arm all three groups, install at the first frame, check, restore on the mapped image. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_view=view_installed,saved_region=mt_region.mode,saved_traffic=mt_traffic.mode;unsigned saved_latched=view_latched,saved_count=view_count;
    int saved_mt=mt_installed;LONG64 saved_waits=mt_worker_waits,saved_wakes=mt_worker_wakes;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=1;boot_phase=0;view_installed=1;mt_region.mode=MT_SYNC;mt_traffic.mode=MT_SYNC;mt_installed=1;
    assert(rc_arm(RC_REGION) && rc_arm(RC_FIELDS) && rc_arm(RC_TRAFFIC) && rc_arm(RC_EFFECTS) && rc_arm(RC_EFFECT_ROWS) && rc_arm(RC_HIGHWAY) && rc_arm(RC_SCENES) &&
           rc_arm(RC_CONTEXT) && rc_arm(RC_POLICY) && rc_policy && rc_arm(RC_SHAPE_REFRESH) && rc_arm(RC_MINING) && rc_arm(RC_XS) && rc_arm(RC_PROBE) &&
           rc_arm(RC_HIGHWAY_ROWS) && rc_arm(RC_HIGHWAY_SPEED) && rc_arm(RC_SOUND) && rc_arm(RC_VIEW_SCENES) && rc_arm(RC_FIELD_FILL) && rc_arm(RC_TRAFFIC_VIEWS) && rc_arm(RC_TRAFFIC_ROWS) && rc_arm(RC_TRAFFIC_PASS) && vsc_armed && rc_installed && !rc_patched && rc_page && rc_stubs &&
           rc_armed_groups==(1u<<RC_GROUPS)-1);
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0); /* nothing written before the first admitted frame */
    void *slots[2];memcpy(slots,rc_page,16);assert(slots[0]==rc_page+RC_MANAGER && slots[1]==rc_page+RC_SHADOW && !*rc_manager_camera() && rc_stubs_owned());
    assert(rc_arm(RC_FIELDS)); /* identical rerun */
    view_latched=0;view_count=0;stats.issued=1;assert(rc_frame(1) && !rc_patched && rc_native_frames==1);
    mt_worker_waits=mt_worker_wakes+1;assert(rc_join_point(1) && rc_installed_groups==(1u<<RC_GROUPS)-1 && rc_patched && rc_install_tick==1); /* no task: idle */
    assert(mt_pins_ok()); /* the mass traffic pin over Zone::Move reads traffic_rows's calls as the originals */
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==rc_site_ours(i) && (rc_sites[i].object?2:1)==rc_site_ours(i));
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==1);
    for(unsigned i=0;i<RC_JUMPS;i++) assert(rc_jump_state(i)==1);
    assert(rc_pins_ok((1u<<RC_GROUPS)-1) && rc_owned() && rc_check()); /* the thunk forms read as the original loads */
    image[rc_pins[4].rva+0x40]^=1;assert(!rc_check() && !strcmp(rc_stage,"block_changed"));image[rc_pins[4].rva+0x40]^=1;
    rc_failures=0;assert(rc_check());
    assert(vsc_installed && vsc_owned() && *(void**)(image+VSC_ZONE_SLOT)==(void*)vsc_zone_scene);
    rc_restore();assert(rc_restored && !rc_installed && !rc_patched && rc_pins_ok((1u<<RC_GROUPS)-1));rc_policy=0;
    assert(!vsc_installed && !vsc_armed && !vsc_active && vsc_slots_native());
    for(unsigned i=0;i<RC_SITES;i++) assert(rc_site_state(i)==0);
    for(unsigned i=0;i<RC_CALLS;i++) assert(rc_call_state(i)==0);
    for(unsigned i=0;i<RC_JUMPS;i++) assert(rc_jump_state(i)==0);
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    view_installed=saved_view;mt_region.mode=saved_region;mt_traffic.mode=saved_traffic;view_latched=saved_latched;view_count=saved_count;
    mt_installed=saved_mt;mt_worker_waits=saved_waits;mt_worker_wakes=saved_wakes;
    VirtualFree(rc_page,0,MEM_RELEASE);VirtualFree(rc_stubs,0,MEM_RELEASE);rc_page=saved_page;rc_stubs=saved_stubs;test_rc_reset();
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("region camera native pins, loads, calls, bank and GetOffset layout, tamper refusals and arm/install/restore verified");
}
