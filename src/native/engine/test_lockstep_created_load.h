/* native_gate_created_load: load-created objects get created IDs (tick 0) at
 * the seal from the notes the hook made during the load. Two synthetic
 * clients with other runtime IDs, addresses, UI noise and a helper thread
 * give the same IDs; imported, excluded, non-Component, transient and
 * other-thread objects stay out; overflow assigns nothing. Owned memory. */
typedef struct {test_created_object sector,zone,a,a1,b,effect,ui,foreign;} test_cl_world;
static created_note_t test_cl_notes[64];
static void test_cl_note(unsigned *n,uint64_t runtime,uint32_t thread) {test_cl_notes[(*n)++]=(created_note_t){runtime,thread,0};}
/* One client's load: notes in creation order from the load thread (thread),
 * UI noise between them, a helper thread's ship, then the seal. */
static void test_cl_load(test_cl_world *w,uint64_t base,uint32_t thread,unsigned noise,uint64_t ids[5]) {
    test_created_reset();memset(test_cl_notes,0,sizeof(test_cl_notes));
    created_load=test_cl_notes;created_load_noted=created_load_overflow=0;created_load_done=created_load_skipped=0;
    created_load_candidates=created_load_foreign=created_load_assigned=0;created_load_threads=0;
    test_created_object_init(&w->sector,TEST_CREATED_VT_ZONE,base,"cluster_01_sector001_macro",NULL);
    test_created_object_init(&w->zone,TEST_CREATED_VT_ZONE,base+1,"zone001_cluster_01_sector001_macro",&w->sector);
    assert(world_identity_record(base,0x3fd) && world_identity_record(base+1,0x3fe)); /* imported by the snapshot */
    test_created_object_init(&w->a,TEST_CREATED_VT_SHIP,base+2,"ship_arg_l_miner_solid_01_a_macro",&w->zone);
    test_created_object_init(&w->a1,TEST_CREATED_VT_WEAPON,base+3,"weapon_gen_l_mining_01_mk1_macro",&w->a);
    test_created_object_init(&w->b,TEST_CREATED_VT_SHIP,base+4,"ship_arg_l_miner_solid_01_a_macro",&w->zone);
    test_created_object_init(&w->effect,TEST_CREATED_VT_EFFECT,base+5,"effect_explosion_macro",&w->zone);
    test_created_object_init(&w->ui,TEST_CREATED_VT_UI,base+6,NULL,NULL);
    test_created_object_init(&w->foreign,TEST_CREATED_VT_SHIP,base+8,"ship_arg_l_miner_solid_01_a_macro",&w->zone);
    unsigned n=0;
    const uint64_t order[]={base,base+1,base+2,base+3,base+4,base+5,base+7 /* transient */,base+8};
    for(unsigned i=0;i<sizeof(order)/sizeof(*order);i++) {
        for(unsigned j=0;j<noise;j++) test_cl_note(&n,base+6,thread);
        test_cl_note(&n,order[i],order[i]==base+8?thread+1:thread);
    }
    created_load_noted=n;
    assert(created_load_resolve_with(test_created_find,test_created_map) && !created_failures);
    const uint64_t listed[]={base+2,base+3,base+4,base+5,base+8};
    for(unsigned i=0;i<5;i++) ids[i]=created_find_runtime(listed[i]);
}
static void test_created_load(void) {
    const unsigned zone[]={22,77,109},ship[]={22,35,36,73,77,91,112,117},weapon[]={22,36,77,107},effect[]={22,42,77};
    test_created_class(TEST_CREATED_VT_ZONE,0,zone,3);test_created_class(TEST_CREATED_VT_SHIP,1,ship,8);
    test_created_class(TEST_CREATED_VT_WEAPON,2,weapon,4);test_created_class(TEST_CREATED_VT_EFFECT,3,effect,3);
    *(void**)(image+TEST_CREATED_VT_UI+0x11e0)=NULL;*(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    memset(created_classes,0,sizeof(created_classes));created_allocate(0);
    world_identity_entry *prior=world_identities;LONG64 prior_count=world_identity_count;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    created_armed=1;created_recording=0;created_load_armed=1;world_created_generation=0;
    static test_cl_world first,second;uint64_t a[5],b[5];
    /* Client 1: load thread 100, helper thread 101. */
    test_cl_load(&first,0x1000,100,0,a);
    assert(created_load_candidates==4 && created_load_threads==2 && created_load_foreign==1 && created_load_assigned==3 &&
           !created_load_skipped && created_assigned==3 && world_created_generation==1);
    assert(a[0] && a[1] && a[2] && !a[3] && !a[4]); /* effect excluded, helper-thread ship unportable */
    uint64_t miner=created_key(0x3fe,TEST_CREATED_VT_SHIP,created_text_hash("ship_arg_l_miner_solid_01_a_macro"));
    assert(a[0]==created_make_id(0,miner,0,0) && a[2]==created_make_id(0,miner,1,0) && !((a[0]>>CREATED_TICK_SHIFT)&CREATED_TICK_LIMIT));
    assert(a[1]==created_make_id(0,created_key(a[0],TEST_CREATED_VT_WEAPON,created_text_hash("weapon_gen_l_mining_01_mk1_macro")),0,0));
    assert(world_identity_portable(0x1002)==a[0] && world_identity_portable(0x1001)==0x3fe && !world_identity_portable(0x1008));
    uint64_t digest=created_digest;
    /* Only once per session. */
    assert(created_load_resolve_with(test_created_find,test_created_map) && created_assigned==3);
    /* Client 2: other runtime IDs and thread IDs, UI noise: the same IDs. */
    test_cl_load(&second,0x7000,900,3,b);
    assert(!memcmp(a,b,sizeof(a)) && created_digest==digest && created_load_foreign==1 && created_load_assigned==3);
    /* Overflow (or too many candidates) assigns none of them. */
    test_created_reset();created_load=test_cl_notes;created_load_done=0;created_load_overflow=1;created_load_noted=3;
    assert(created_load_resolve_with(test_created_find,test_created_map) && created_load_skipped && !created_assigned);
    created_load_overflow=0;
    /* The seal: load assignment first, then recording; the hook stops noting. */
    test_created_reset();created_load_done=0;created_load_skipped=0;created_load_recording=1;created_installed=1;
    memset(test_cl_notes,0,sizeof(test_cl_notes));created_load_noted=0;
    assert(created_begin_with(test_created_find,test_created_map) && created_recording && !created_load_recording && created_load_done);
    char status[1024];assert(created_status(status,sizeof(status))>0 && strstr(status," load=1 ") && strstr(status," load_skipped=0"));
    /* The note itself: runtime and the calling thread, lock-free. */
    created_recording=0;created_load_recording=1;created_load_noted=0;
    unsigned char object[16]={0};*(uint64_t*)(object+8)=0xabc;created_load_note(object);
    assert(created_load_noted==1 && test_cl_notes[0].runtime==0xabc && test_cl_notes[0].thread==(uint32_t)GetCurrentThreadId());
    /* Arming: after the created identities, before loading, on the UI thread. */
    created_load_armed=0;created_load_recording=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    created_installed=0;assert(!created_load_arm());created_installed=1;
    created_armed=0;assert(!created_load_arm());created_armed=1;
    boot_phase=2;assert(!created_load_arm());boot_phase=0;
    stats.issued=1;assert(!created_load_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!created_load_arm());ui_thread=GetCurrentThreadId();
    created_load=test_cl_notes;assert(created_load_arm() && created_load_armed && created_load_recording);
    assert(created_load_arm()); /* loading-UI reload accepts the armed owner */
    created_stop();assert(!created_load_recording);
    created_load_armed=0;created_load=NULL;created_load_noted=0;created_load_done=0;created_installed=0;created_armed=0;
    stats.active=0;boot_mode=0;world_created_generation=0;test_created_reset();
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;
    puts("created load identities: tick-0 IDs from the load thread's creation order on two clients, exclusions, helper thread, overflow, seal order, note and arm refusals PASS");
}
