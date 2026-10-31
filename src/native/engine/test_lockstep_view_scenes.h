/* native_gate_view_scenes: the sector table (slot order, sticky, grace, full), the anchors with the native rule,
 * slot 943's answers, the extra steps, creation with B's arguments, and what a stop leaves. */
enum {TEST_VSC_SECTORS=5,TEST_VSC_SHIPS=6};
static __declspec(align(16)) unsigned char test_vsc_sectors[TEST_VSC_SECTORS][0x200],test_vsc_zones[TEST_VSC_SECTORS][0x200],test_vsc_ships[TEST_VSC_SHIPS][0x200],test_vsc_clusters[2][0x200];
static __declspec(align(16)) unsigned char test_vsc_scene_store[3][0x80];
static void *test_vsc_vtable[0x11e8/8+1];
static char test_vsc_class(void *o,int klass) {
    const unsigned char *p=o;
    if(klass==0x58) return p>=test_vsc_sectors[0] && p<test_vsc_sectors[0]+sizeof(test_vsc_sectors);
    if(klass==0x6d) return p>=test_vsc_zones[0] && p<test_vsc_zones[0]+sizeof(test_vsc_zones);
    if(klass==0xf) return p>=test_vsc_clusters[0] && p<test_vsc_clusters[0]+sizeof(test_vsc_clusters);
    return 0;
}
static void test_vsc_link(unsigned char *child,void *parent) {void *vt=test_vsc_vtable;memset(child,0,0x200);memcpy(child,&vt,8);memcpy(child+0x70,&parent,8);}
/* Sectors 0..2 in cluster 0, 3..4 in cluster 1; zone k in sector k; ship k in zone k unless placed. */
static void test_vsc_world(void) {
    test_vsc_vtable[0x11e8/8]=(void*)test_vsc_class;
    test_vsc_link(test_vsc_clusters[0],NULL);test_vsc_link(test_vsc_clusters[1],NULL);
    for(unsigned k=0;k<TEST_VSC_SECTORS;k++) {test_vsc_link(test_vsc_sectors[k],test_vsc_clusters[k<3?0:1]);test_vsc_link(test_vsc_zones[k],test_vsc_sectors[k]);}
    for(unsigned k=0;k<TEST_VSC_SHIPS;k++) {test_vsc_link(test_vsc_ships[k],test_vsc_zones[k%TEST_VSC_SECTORS]);*(uint64_t*)(test_vsc_ships[k]+8)=0x5ca90u+k;}
}
static void test_vsc_place(unsigned ship,unsigned zone) {void *z=test_vsc_zones[zone];memcpy(test_vsc_ships[ship]+0x70,&z,8);}
static void test_vsc_views(unsigned a,unsigned b,unsigned c,unsigned d) {
    const unsigned s[4]={a,b,c,d};
    for(unsigned i=0;i<4;i++) {view_points[i]=test_vsc_ships[s[i]];view_ids[i]=0x5ca90u+s[i];}
    view_count=4;rc_view_sector58=rc_ancestor(view_points[0],0x58);
}
/* Stand-ins for the constructor, the anchor call, Scene::Step and slot 943's natives. */
static unsigned test_vsc_ctors;static char test_vsc_name[16];static uint64_t test_vsc_name_size,test_vsc_name_cap,test_vsc_ctor_unused;static uint32_t test_vsc_ctor_counts[3];static int test_vsc_ctor_fail;
static void *test_vsc_ctor(void *self,void *name,uint64_t unused,uint32_t a,uint32_t b,uint32_t c) {
    const vsc_name_t *n=name;memcpy(test_vsc_name,n->text,16);test_vsc_name_size=n->size;test_vsc_name_cap=n->capacity;test_vsc_ctor_unused=unused;
    test_vsc_ctor_counts[0]=a;test_vsc_ctor_counts[1]=b;test_vsc_ctor_counts[2]=c;++test_vsc_ctors;
    if(test_vsc_ctor_fail) return NULL;
    memset(self,0,0x50);return self;
}
static void *test_vsc_allocate(size_t size) {
    static unsigned next;assert(size==0x50);return test_vsc_scene_store[(next++)%2u+1u];
}
static unsigned test_vsc_anchor_calls;static unsigned char test_vsc_anchor_rebase;static void *test_vsc_anchor_scene;static LONG test_vsc_anchor_count;
static void test_vsc_anchor(void *scene,void **zone,unsigned char rebase) {
    /* The engine's: the zone held across the call, its own reference on the anchor. */
    test_vsc_anchor_count=*(LONG*)((unsigned char*)*zone+0x40);
    memcpy((unsigned char*)scene+0x40,zone,8);InterlockedIncrement((volatile LONG*)((unsigned char*)*zone+0x40));
    test_vsc_anchor_scene=scene;test_vsc_anchor_rebase=rebase;++test_vsc_anchor_calls;
}
static void test_vsc_anchor_noref(void *scene,void **zone,unsigned char rebase) {(void)rebase;memcpy((unsigned char*)scene+0x40,zone,8);}
static void *test_vsc_steps[8];static double test_vsc_step_time;static unsigned test_vsc_step_count;
static void test_vsc_step(void *scene,double time) {test_vsc_steps[test_vsc_step_count++%8u]=scene;test_vsc_step_time=time;}
static void *test_vsc_native_answer;static unsigned test_vsc_native_calls;
static void *test_vsc_native(void *zone) {(void)zone;++test_vsc_native_calls;return test_vsc_native_answer;}
static void test_view_scenes(void) {
    void *saved_points[VIEW_MAX];uint64_t saved_ids[VIEW_MAX];unsigned saved_count=view_count;void *saved_home=rc_view_sector58;void *saved_b;
    memcpy(saved_points,view_points,sizeof(saved_points));memcpy(saved_ids,view_ids,sizeof(saved_ids));memcpy(&saved_b,image+VSC_SCENE_B,8);
    test_vsc_world();memset(test_vsc_scene_store,0,sizeof(test_vsc_scene_store));
    void *b=test_vsc_scene_store[0];memcpy(image+VSC_SCENE_B,&b,8);
    vsc_ctor_native=test_vsc_ctor;vsc_anchor_native=test_vsc_anchor;vsc_step_native=test_vsc_step;vsc_zone_native=test_vsc_native;vsc_highway_native=test_vsc_native;rid_allocate_native=test_vsc_allocate;
    vsc_created=0;vsc_active=0;vsc_steps=vsc_moves=vsc_rebases=vsc_assigns=vsc_releases=vsc_early=vsc_blocked_ticks=0;vsc_blocked_now=0;
    /* Creation: C and D with B's arguments, once; no scene B or a failed constructor refuses. */
    {
        void *none=NULL;memcpy(image+VSC_SCENE_B,&none,8);assert(!vsc_create() && !test_vsc_ctors);memcpy(image+VSC_SCENE_B,&b,8);
        test_vsc_ctor_fail=1;assert(!vsc_create() && test_vsc_ctors==1);test_vsc_ctor_fail=0;test_vsc_ctors=0;
        assert(vsc_create() && test_vsc_ctors==2 && vsc_created);
        assert(!memcmp(test_vsc_name,"Cutscene\0",9) && test_vsc_name_size==8 && test_vsc_name_cap==15 && !test_vsc_ctor_unused);
        assert(test_vsc_ctor_counts[0]==0x10000 && test_vsc_ctor_counts[1]==0x10000 && test_vsc_ctor_counts[2]==0x10000);
        assert(vsc_entries[0].scene==b && vsc_entries[1].scene && vsc_entries[2].scene && vsc_entries[1].scene!=vsc_entries[2].scene && vsc_entries[1].scene!=b);
        assert(vsc_extra[0]==vsc_entries[1].scene && vsc_extra[1]==vsc_entries[2].scene);
    }
    void *const sb=vsc_entries[0].scene,*const sc=vsc_entries[1].scene,*const sd=vsc_entries[2].scene;
    /* Slot order: slot 1's sector is A's and not in the table; each other sector takes the next scene. */
    test_vsc_views(0,1,2,3);vsc_latch(1);
    assert(vsc_entries[0].sector==test_vsc_sectors[1] && vsc_entries[1].sector==test_vsc_sectors[2] && vsc_entries[2].sector==test_vsc_sectors[3]);
    assert(vsc_entries[0].view==1 && vsc_entries[1].view==2 && vsc_entries[2].view==3 && vsc_entries[0].point==test_vsc_ships[1] && vsc_assigns==3 && !vsc_blocked_now);
    /* Two slots in one sector: one scene, the first in slot order. In slot 1's sector: none. */
    {
        vsc_entry_t held[VSC_ENTRIES];memcpy(held,vsc_entries,sizeof(held));
        for(unsigned k=0;k<VSC_ENTRIES;k++) {vsc_entries[k].sector=NULL;vsc_entries[k].released=0;}
        test_vsc_place(5,1);test_vsc_views(0,1,5,0);vsc_latch(2);
        assert(vsc_entries[0].sector==test_vsc_sectors[1] && vsc_entries[0].view==1 && !vsc_entries[1].sector && !vsc_entries[2].sector);
        memcpy(vsc_entries,held,sizeof(held));test_vsc_place(5,0);vsc_assigns=3;
    }
    /* Sticky: order and positions change, a sector keeps its scene. */
    test_vsc_views(0,3,2,1);vsc_latch(3);
    assert(vsc_entries[0].sector==test_vsc_sectors[1] && vsc_entries[1].sector==test_vsc_sectors[2] && vsc_entries[2].sector==test_vsc_sectors[3]);
    assert(vsc_entries[0].view==3 && vsc_entries[1].view==2 && vsc_entries[2].view==1 && vsc_assigns==3 && !vsc_releases);
    /* A sector left gives its scene back, which waits: a new sector finds none free and is counted; then it moves in. */
    test_vsc_place(1,4);test_vsc_views(0,1,2,3);vsc_latch(100);
    assert(!vsc_entries[0].sector && vsc_entries[0].released==100 && vsc_releases==1);
    assert(vsc_blocked_now==1 && vsc_blocked_ticks==1 && vsc_entries[1].sector==test_vsc_sectors[2] && vsc_entries[2].sector==test_vsc_sectors[3]);
    vsc_latch(100+VSC_GRACE-1u);assert(!vsc_entries[0].sector && vsc_blocked_now==1 && vsc_blocked_ticks==2);
    vsc_latch(100+VSC_GRACE);assert(vsc_entries[0].sector==test_vsc_sectors[4] && vsc_entries[0].view==1 && !vsc_entries[0].released && !vsc_blocked_now && vsc_assigns==4);
    /* A view point gone (ID no longer its own) holds nothing. */
    {
        const uint64_t id=view_ids[3];view_ids[3]=id+1u;vsc_latch(100+VSC_GRACE+1u);assert(!vsc_entries[2].sector && vsc_releases==2);view_ids[3]=id;
        vsc_latch(100+2u*VSC_GRACE+1u);assert(vsc_entries[2].sector==test_vsc_sectors[3]);
    }
    /* Early reuse: a released scene whose system still counts a body, or still has one active, waits; empty, it is
     * free before the grace. The scenes above had no readable system (+0x20 zero) and waited the whole grace. */
    {
        static __declspec(align(16)) unsigned char system[0x700];memset(system,0,sizeof(system));
        unsigned char *const scene=vsc_entries[2].scene;void *const sys=system,*saved=NULL;memcpy(&saved,scene+0x20,8);memcpy(scene+0x20,&sys,8);
        *(uint32_t*)(system+0x28)=1u;
        const uint64_t t=100+2u*VSC_GRACE+10u,early=vsc_early,assigns=vsc_assigns,blocked=vsc_blocked_ticks;const uint64_t id=view_ids[3];
        view_ids[3]=id+1u;vsc_latch(t);view_ids[3]=id;assert(!vsc_entries[2].sector && vsc_entries[2].released==t);
        vsc_latch(t+1u);assert(!vsc_entries[2].sector && vsc_blocked_now==1 && vsc_early==early);
        *(uint32_t*)(system+0x28)=0u;*(uint64_t*)(system+0x680)=1u;
        vsc_latch(t+2u);assert(!vsc_entries[2].sector && vsc_blocked_now==1 && vsc_blocked_ticks==blocked+2u);
        *(uint64_t*)(system+0x680)=0u;
        vsc_latch(t+3u);assert(vsc_entries[2].sector==test_vsc_sectors[3] && vsc_entries[2].view==3 && !vsc_entries[2].released);
        assert(!vsc_blocked_now && vsc_early==early+1u && vsc_assigns==assigns+1u && vsc_entries[0].sector==test_vsc_sectors[4] && vsc_entries[1].sector==test_vsc_sectors[2]);
        /* Released empty: free at once, in the same frame a new sector asks. */
        test_vsc_place(3,0);vsc_latch(t+4u);assert(!vsc_entries[2].sector && vsc_entries[2].released==t+4u);
        test_vsc_place(3,3);vsc_latch(t+5u);assert(vsc_entries[2].sector==test_vsc_sectors[3] && vsc_early==early+2u);
        memcpy(scene+0x20,&saved,8);
    }
    /* Anchors: first set without a rebase; in the same cluster with one; across clusters without; the same zone not at all. */
    {
        LONG before=*(LONG*)(test_vsc_zones[4]+0x40);
        vsc_entry_t *e=&vsc_entries[0];e->point=test_vsc_ships[1];test_vsc_place(1,4); /* sector 4, cluster 1 */
        vsc_anchor(e);
        assert(test_vsc_anchor_calls==1 && test_vsc_anchor_scene==sb && !test_vsc_anchor_rebase && *(void**)((unsigned char*)sb+0x40)==test_vsc_zones[4]);
        assert(test_vsc_anchor_count==before+1 && *(LONG*)(test_vsc_zones[4]+0x40)==before+1 && vsc_moves==1 && !vsc_rebases); /* the scene's reference stays, ours went */
        vsc_anchor(e);assert(test_vsc_anchor_calls==1);
        test_vsc_place(1,3);vsc_anchor(e);assert(test_vsc_anchor_calls==2 && test_vsc_anchor_rebase==1 && vsc_rebases==1); /* sector 3: cluster 1 */
        test_vsc_place(1,2);vsc_anchor(e);assert(test_vsc_anchor_calls==3 && !test_vsc_anchor_rebase && vsc_rebases==1);   /* sector 2: cluster 0 */
        /* A zone in state 1 goes to 2 while held, as the native call site does. */
        *(LONG*)(test_vsc_zones[1]+0x44)=1;test_vsc_place(1,1);vsc_anchor(e);assert(*(LONG*)(test_vsc_zones[1]+0x44)==2);
        /* The call must leave its own reference: none is a failure, not a release. */
        vsc_anchor_native=test_vsc_anchor_noref;rc_failures=0;test_vsc_place(1,0);vsc_anchor(e); /* zone 0: nobody else holds it */
        assert(rc_failures==1 && !strcmp(rc_stage,"scene_anchor_ref"));
        vsc_anchor_native=test_vsc_anchor;rc_failures=0;rc_stage="idle";
        test_vsc_place(1,4);
    }
    /* A whole frame: creation once, the table, an anchor per held scene; a failed creation fails the frame. */
    {
        vsc_created=0;test_vsc_ctor_fail=1;InterlockedExchange(&vsc_active,0);assert(!vsc_frame(200) && !vsc_active);test_vsc_ctor_fail=0;
        test_vsc_ctors=0;memset(test_vsc_scene_store,0,sizeof(test_vsc_scene_store));vsc_releases=0;
        test_vsc_views(0,1,2,3);test_vsc_place(1,1);test_vsc_anchor_calls=0;
        assert(vsc_frame(201) && vsc_active==1 && test_vsc_ctors==2 && test_vsc_anchor_calls==3);
        assert(*(void**)((unsigned char*)vsc_entries[0].scene+0x40)==test_vsc_zones[1] && *(void**)((unsigned char*)vsc_entries[1].scene+0x40)==test_vsc_zones[2] &&
               *(void**)((unsigned char*)vsc_entries[2].scene+0x40)==test_vsc_zones[3]);
        assert(vsc_frame(202) && test_vsc_ctors==2 && test_vsc_anchor_calls==3); /* nothing moved */
    }
    /* Slot 943: the table first; else native; a native B the table does not hold is no scene; a highway the same. */
    {
        void *const s_b=vsc_entries[0].scene,*const s_c=vsc_entries[1].scene,*const main=(void*)0x1234;
        test_vsc_native_calls=0;test_vsc_native_answer=main;
        assert(vsc_zone_scene(test_vsc_zones[1])==s_b && vsc_zone_scene(test_vsc_zones[2])==s_c && vsc_highway_scene(test_vsc_zones[1])==s_b && !test_vsc_native_calls);
        assert(vsc_zone_scene(test_vsc_zones[0])==main && test_vsc_native_calls==1);
        test_vsc_native_answer=s_b;assert(!vsc_zone_scene(test_vsc_zones[4]) && !vsc_highway_scene(test_vsc_zones[4]));
        test_vsc_native_answer=NULL;assert(!vsc_zone_scene(test_vsc_zones[4]));
    }
    /* The step: B, then C and D, at B's time. */
    {
        test_vsc_step_count=0;vsc_steps=0;LONG64 stubbed=rc_stub_calls[RC_VIEW_STEP_CALL];vsc_step(sb,12.5);
        assert(test_vsc_step_count==3 && test_vsc_steps[0]==sb && test_vsc_steps[1]==vsc_extra[0] && test_vsc_steps[2]==vsc_extra[1] && test_vsc_step_time==12.5 && vsc_steps==2);
        assert(rc_stub_calls[RC_VIEW_STEP_CALL]==stubbed+1);
    }
    /* Status names the slots, the anchors' IDs and the counters. */
    {
        *(uint64_t*)(test_vsc_zones[1]+8)=0xabc1;char text[320];vsc_status(text,sizeof(text));
        assert(strstr(text,"1,slots:2/3/4,anchors:abc1/") && strstr(text,",steps:2,") && strstr(text,",early:2,blocked:"));
    }
    /* Other modules can tell C and D from an unknown scene. */
    assert(vsc_extra[0] && vsc_extra[1]);
    /* The vtable entries: installed by compare and swap, a foreign value refused and reported, back at the end. */
    {
        *(void**)(image+VSC_ZONE_SLOT)=image+VSC_ZONE;*(void**)(image+VSC_HIGHWAY_SLOT)=image+VSC_HIGHWAY;
        assert(vsc_slots_native() && vsc_install() && vsc_installed && vsc_owned() && !vsc_slots_native());
        assert(*(void**)(image+VSC_ZONE_SLOT)==(void*)vsc_zone_scene && *(void**)(image+VSC_HIGHWAY_SLOT)==(void*)vsc_highway_scene);
        *(void**)(image+VSC_ZONE_SLOT)=(void*)1;assert(!vsc_owned() && !vsc_remove() && vsc_installed);
        *(void**)(image+VSC_ZONE_SLOT)=(void*)vsc_zone_scene;*(void**)(image+VSC_HIGHWAY_SLOT)=(void*)vsc_highway_scene;assert(vsc_owned());
        assert(vsc_remove() && !vsc_installed && vsc_slots_native() && vsc_remove());
        *(void**)(image+VSC_HIGHWAY_SLOT)=(void*)1;assert(!vsc_install() && !vsc_installed && *(void**)(image+VSC_ZONE_SLOT)==(void*)(image+VSC_ZONE));
        *(void**)(image+VSC_HIGHWAY_SLOT)=image+VSC_HIGHWAY;
    }
    /* A stop: nothing steps C or D any more, slot 943 answers natively even with the table still there. The scenes stay alive for teardown. */
    {
        void *const scene_c=vsc_extra[0];InterlockedExchange(&vsc_active,0);
        test_vsc_step_count=0;vsc_step(sb,13.0);assert(test_vsc_step_count==1 && test_vsc_steps[0]==sb);
        test_vsc_native_answer=sd;test_vsc_native_calls=0;assert(vsc_zone_scene(test_vsc_zones[1])==sd && test_vsc_native_calls==1);
        assert(vsc_extra[0]==scene_c && vsc_entries[0].sector);
        (void)sc;
    }
    vsc_ctor_native=NULL;vsc_anchor_native=NULL;vsc_step_native=NULL;vsc_zone_native=NULL;vsc_highway_native=NULL;rid_allocate_native=NULL;
    memset(vsc_entries,0,sizeof(vsc_entries));vsc_extra[0]=vsc_extra[1]=NULL;vsc_created=vsc_armed=vsc_installed=0;vsc_active=0;rc_failures=0;
    vsc_steps=vsc_moves=vsc_rebases=vsc_assigns=vsc_releases=vsc_early=vsc_blocked_ticks=0;vsc_blocked_now=0;
    memcpy(view_points,saved_points,sizeof(saved_points));memcpy(view_ids,saved_ids,sizeof(saved_ids));view_count=saved_count;rc_view_sector58=saved_home;memcpy(image+VSC_SCENE_B,&saved_b,8);
    printf("view scenes table/anchors/slot 943/steps/create/stop tests PASS\n");
}
/* The code the group calls and answers for, as read from the build: B's constructor call, the step, the vtable entries. */
static void test_view_scenes_native_image(unsigned char *mapped) {
    assert(*(uint64_t*)(mapped+VSC_ZONE_SLOT)==0x140000000ULL+VSC_ZONE && *(uint64_t*)(mapped+VSC_HIGHWAY_SLOT)==0x140000000ULL+VSC_HIGHWAY);
    /* 0x83f91e: mov dword [rsp+0x28], 0x10000; mov dword [rsp+0x20], 0x10000; mov r9d, 0x10000; lea rdx, name; mov rcx, rax; call 0xba090. */
    assert(!memcmp(mapped+0x83f901,"\xc7\x44\x24\x28\x00\x00\x01\x00\xc7\x44\x24\x20\x00\x00\x01\x00\x41\xb9\x00\x00\x01\x00",22));
    assert(mapped[0x83f91e]==0xe8 && 0x83f923u+*(int32_t*)(mapped+0x83f91f)==VSC_CTOR);
    /* "Cutscene", its size and capacity in the std::string the call gets. */
    assert(*(uint64_t*)(mapped+0x83f8eb)==0x656e656373747543ULL && *(uint32_t*)(mapped+0x83f8dd)==15u && *(uint32_t*)(mapped+0x83f8e5)==8u);
    /* A's and B's steps in UniverseFrameUpdate, both through 0xba600; B is the second. */
    assert(mapped[0x9fe459]==0xe8 && 0x9fe45eu+*(int32_t*)(mapped+0x9fe45a)==VSC_STEP && mapped[0x9fe48b]==0xe8 && 0x9fe490u+*(int32_t*)(mapped+0x9fe48c)==VSC_STEP);
    assert(0x9fe465u+*(int32_t*)(mapped+0x9fe461)==VSC_SCENE_B);
    /* The anchor call site: 0x97dff0 hands 0xbd410 the scene, a pointer to the zone and the rebase flag. */
    assert(mapped[0x97e4f9]==0xe8 && 0x97e4feu+*(int32_t*)(mapped+0x97e4fa)==VSC_ANCHOR);
    puts("view scenes constructor call, steps, vtable entries and anchor call verified");
}
