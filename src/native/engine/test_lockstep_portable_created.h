/* native_gate_portable_created: every changed consumer with mixed saved,
 * created and unportable inputs on two synthetic clients whose runtime IDs,
 * addresses and native orders differ. Off reproduces the former keys and
 * orders; on, created objects take portable keys and a portable place behind
 * the saved ones, and saved objects keep their keys and relative order.
 * Owned memory only; no X4 code runs. */
#define TEST_PC_CREATED_A (CREATED_BIT|(15ULL<<CREATED_TICK_SHIFT)|0x2222222222ULL)
#define TEST_PC_CREATED_B (CREATED_BIT|(15ULL<<CREATED_TICK_SHIFT)|0x1111111111ULL) /* sorts before A */
static world_identity_entry *test_pc_prior_identities;
static LONG64 test_pc_prior_count;
static void test_pc_begin(void) {
    test_pc_prior_identities=world_identities;test_pc_prior_count=world_identity_count;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    test_created_reset();created_armed=1;created_recording=0;world_portable_created=0;world_created_generation=0;
}
static void test_pc_end(void) {
    test_created_reset();created_armed=0;created_recording=0;world_portable_created=0;world_created_generation=0;world_identity_sealed=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=test_pc_prior_identities;world_identity_count=test_pc_prior_count;
}
/* A created identity as the gate boundary assigns it: table entry, new generation. */
static void test_pc_create(uint64_t runtime,uint64_t id) {
    assert(created_live_add(runtime,id,15));InterlockedIncrement64(&world_created_generation);
}

/* Shared lookup, generation, boundary bump, status and arming. */
static void test_pc_keys(void) {
    test_pc_begin();unsigned k=99;
    assert(world_identity_record(0x100,0x30));test_pc_create(0x200,TEST_PC_CREATED_A);
    /* Off: exactly the saved lookup; the generation reads 0. */
    assert(world_identity_key(0x100,&k)==0x30 && k==WORLD_KEY_SAVED);
    assert(!world_identity_key(0x200,&k) && k==WORLD_KEY_NONE && !world_identity_key(0x300,&k) && k==WORLD_KEY_NONE);
    assert(!world_identity_key(0,&k) && k==WORLD_KEY_NONE && !world_identity_generation());
    assert(world_identity_portable(0x200)==TEST_PC_CREATED_A); /* captures see created IDs regardless */
    /* On: the created ID with its own class; saved stays saved. */
    world_portable_created=1;
    assert(world_identity_key(0x100,&k)==0x30 && k==WORLD_KEY_SAVED);
    assert(world_identity_key(0x200,&k)==TEST_PC_CREATED_A && k==WORLD_KEY_CREATED);
    assert(!world_identity_key(0x300,&k) && k==WORLD_KEY_NONE && world_identity_generation()==1);
    created_armed=0;assert(!world_identity_key(0x200,&k) && k==WORLD_KEY_NONE);created_armed=1;
    /* The boundary bumps the generation only when the table changes. */
    created_recording=1;
    assert(created_frame_with(16,test_created_find,test_created_map) && created_retired==1 && world_created_generation==2);
    assert(!world_identity_key(0x200,&k) && k==WORLD_KEY_NONE);
    assert(created_frame_with(17,test_created_find,test_created_map) && world_created_generation==2);
    created_recording=0;
    char status[1024];assert(created_status(status,sizeof(status)) && strstr(status," failures=0 portable=1 generation=2"));
    /* Arming: after the created identities, before loading, on the UI thread. */
    world_portable_created=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    created_installed=0;assert(!portable_created_arm() && !world_portable_created);
    created_installed=1;boot_phase=2;assert(!portable_created_arm());boot_phase=0;
    stats.issued=1;assert(!portable_created_arm());stats.issued=0;
    created_armed=0;assert(!portable_created_arm());created_armed=1;
    ui_thread=GetCurrentThreadId()+1;assert(!portable_created_arm());ui_thread=GetCurrentThreadId();
    assert(!world_portable_created && portable_created_arm() && world_portable_created);
    assert(portable_created_arm()); /* loading-UI reload accepts the installed owner */
    created_installed=0;assert(!portable_created_arm());
    world_portable_created=0;stats.active=0;boot_mode=0;
    test_pc_end();
    puts("portable created keys: saved-only lookup when off, created class/ID when on, generation bump on table change, status and arm refusals PASS");
}

/* Pathing batches. Labels 0,1 saved (0x30, 0x10), 2,3 created (A, B), 4,5
 * unportable; client 1 reverses the runtime order and permutes the list. */
#define TEST_PC_SHIPS 6
typedef struct {test_pd_ship ships[TEST_PC_SHIPS];unsigned char manager[0x220],batch[0x20],sentinel[0x18],pending[0x18],tls[0x400];} test_pc_world;
static test_pc_world test_pc_worlds[2];
static const uint64_t test_pc_runtime[2][TEST_PC_SHIPS]={{0x100,0x101,0x102,0x103,0x104,0x105},{0x905,0x904,0x903,0x902,0x901,0x900}};
static const unsigned test_pc_list[2][TEST_PC_SHIPS]={{0,1,2,3,4,5},{5,3,1,4,2,0}};
static void test_pc_build(unsigned client) {
    test_pc_world *w=&test_pc_worlds[client];
    memset(w->manager,0,sizeof(w->manager));memset(w->batch,0,sizeof(w->batch));memset(w->sentinel,0,sizeof(w->sentinel));
    for(unsigned label=0;label<TEST_PC_SHIPS;label++) {
        test_pd_link(&w->ships[label],test_pc_runtime[client][label],w->sentinel,NULL);
        w->ships[label].agent[TSNAP_FLAG]=(unsigned char)(label==2 || label==4);
    }
    unsigned char *prev=w->sentinel;
    for(unsigned i=0;i<TEST_PC_SHIPS;i++) {unsigned char *node=w->ships[test_pc_list[client][i]].node;*(unsigned char**)(prev+8)=node;prev=node;}
    *(unsigned char**)(prev+8)=w->sentinel;
    *(uint64_t*)(w->batch+8)=TEST_PC_SHIPS;*(unsigned char**)(w->batch+0x10)=w->sentinel;
    *(unsigned char**)(w->manager+0x180)=w->batch;*(unsigned char**)(w->manager+0x188)=w->batch+0x20;
    memset(w->pending,0,sizeof(w->pending));*(unsigned char**)(w->pending+8)=w->pending;*(unsigned char**)(w->manager+0x178)=w->pending;
}
static void test_pc_register(unsigned client,int created_b) {
    test_created_reset();
    test_pc_create(test_pc_runtime[client][2],TEST_PC_CREATED_A);
    if(created_b) test_pc_create(test_pc_runtime[client][3],TEST_PC_CREATED_B);
}
static unsigned test_pc_label(unsigned client,uint64_t runtime) {
    for(unsigned label=0;label<TEST_PC_SHIPS;label++) if(test_pc_runtime[client][label]==runtime) return label;
    assert(0);return 0;
}
/* One refresh frame; the visit order as labels; returns the order hash. */
static uint64_t test_pc_path(unsigned client,unsigned labels[TEST_PC_SHIPS]) {
    test_pc_world *w=&test_pc_worlds[client];double before=5.0;memcpy(w->tls+0x300,&before,8);
    test_pd_tls=w->tls;test_pd_now+=1.0;test_pd_bank^=1;test_pd_calls=0;
    assert(path_det_refresh_frame(test_pd_refresh,test_pd_sync,w->manager,w->tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank));
    assert(test_pd_calls==TEST_PC_SHIPS && path_det_last_count==TEST_PC_SHIPS && !memcmp(w->tls+0x300,&before,8));
    for(unsigned i=0;i<TEST_PC_SHIPS;i++) labels[i]=test_pc_label(client,test_pd_order[i]);
    return path_det_order_hash;
}
static void test_pc_path_determinism(void) {
    test_pc_begin();
    for(unsigned c=0;c<2;c++) {
        assert(world_identity_record(test_pc_runtime[c][0],0x30) && world_identity_record(test_pc_runtime[c][1],0x10));
        test_pc_build(c);
    }
    path_det_seq_valid=path_det_items_valid=0;path_det_unmapped=path_det_created=0;test_pd_now=6.0;test_pd_bank=0;
    unsigned got[2][TEST_PC_SHIPS];uint64_t off_hash[2],on_hash[2];
    /* Off: saved by saved ID, then every other agent by runtime ID: the
     * former key order, which differs between the clients after the saved part. */
    for(unsigned c=0;c<2;c++) {test_pc_register(c,1);off_hash[c]=test_pc_path(c,got[c]);}
    const unsigned off0[TEST_PC_SHIPS]={1,0,2,3,4,5},off1[TEST_PC_SHIPS]={1,0,5,4,3,2};
    assert(!memcmp(got[0],off0,sizeof(off0)) && !memcmp(got[1],off1,sizeof(off1)) && off_hash[0]==off_hash[1]);
    assert(path_det_unmapped==8 && !path_det_created);
    /* The former implementation's keys give the same order (reference sort). */
    for(unsigned c=0;c<2;c++) {
        path_det_item legacy[TEST_PC_SHIPS];
        for(unsigned label=0;label<TEST_PC_SHIPS;label++) {
            uint64_t runtime=test_pc_runtime[c][label],saved=world_identity_lookup(runtime);
            legacy[label].id=saved?saved:(1ULL<<63)|runtime;legacy[label].klass=0;legacy[label].controller=NULL;
        }
        qsort(legacy,TEST_PC_SHIPS,sizeof(*legacy),path_det_item_order);
        for(unsigned i=0;i<TEST_PC_SHIPS;i++) {
            uint64_t saved=world_identity_lookup(test_pc_runtime[c][got[c][i]]);
            assert(legacy[i].id==(saved?saved:(1ULL<<63)|test_pc_runtime[c][got[c][i]]));
        }
    }
    /* On: saved unchanged, created by created ID (B before A), unportable last. */
    world_portable_created=1;path_det_unmapped=path_det_created=0;
    for(unsigned c=0;c<2;c++) {test_pc_register(c,1);on_hash[c]=test_pc_path(c,got[c]);}
    const unsigned on0[TEST_PC_SHIPS]={1,0,3,2,4,5},on1[TEST_PC_SHIPS]={1,0,3,2,5,4};
    assert(!memcmp(got[0],on0,sizeof(on0)) && !memcmp(got[1],on1,sizeof(on1)));
    assert(on_hash[0]==on_hash[1] && on_hash[0]!=off_hash[0] && path_det_unmapped==4 && path_det_created==4);
    /* Creation frame: B not assigned yet is unportable; the boundary's
     * generation change rebuilds the cached list with B in the created class. */
    test_pc_register(0,0);uint64_t rebuilds=path_det_rebuilds;test_pc_path(0,got[0]);
    const unsigned early[TEST_PC_SHIPS]={1,0,2,3,4,5};assert(!memcmp(got[0],early,sizeof(early)));
    test_pc_path(0,got[0]);uint64_t reuses=path_det_reuses;assert(path_det_rebuilds==rebuilds+1);
    test_pc_create(test_pc_runtime[0][3],TEST_PC_CREATED_B);
    assert(test_pc_path(0,got[0])==on_hash[0] && !memcmp(got[0],on0,sizeof(on0)) && path_det_rebuilds==rebuilds+2 && path_det_reuses==reuses);
    char status[640];assert(path_det_status(status,sizeof(status)) && strstr(status," id_hits=") && strstr(status," created="));
    /* Travel snapshot receipt on the same batches (labels 2 and 4 flagged). */
    uint64_t base=lockstep_mix(0x30*2)+lockstep_mix(0x10*2),receipt[2];
    for(unsigned mode=0;mode<2;mode++) {
        world_portable_created=mode;
        for(unsigned c=0;c<2;c++) {
            test_pc_register(c,1);uint64_t agents,flagged,fresh,hash,unmapped,unmapped_flagged;
            assert(tsnap_snapshot(test_pc_worlds[c].manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged));
            assert(agents==TEST_PC_SHIPS && flagged==2 && !fresh);
            if(!mode) assert(hash==base && unmapped==4 && unmapped_flagged==2 && !tsnap_walk_created);
            else assert(hash==base+lockstep_mix(lockstep_mix(TEST_PC_CREATED_A)+1)+lockstep_mix(lockstep_mix(TEST_PC_CREATED_B)) &&
                        unmapped==2 && unmapped_flagged==1 && tsnap_walk_created==2);
            receipt[c]=hash;
            for(unsigned label=0;label<TEST_PC_SHIPS;label++)
                assert(test_pc_worlds[c].ships[label].agent[TSNAP_PAD]==test_pc_worlds[c].ships[label].agent[TSNAP_FLAG]);
        }
        assert(receipt[0]==receipt[1]);
    }
    path_det_seq_valid=path_det_items_valid=0;tsnap_seq_count=0;path_det_unmapped=path_det_created=0;
    test_pc_end();
    puts("portable created path determinism/travel snapshot: legacy order when off, saved<created<local on two clients, creation-frame rebuild, receipt PASS");
}

/* Due events at one time: sources 0,1 saved (0x50, 0x40), 2,3 created (A, B),
 * 4 unportable; layout 1 has other runtime IDs, addresses and slot order. */
static void test_pc_event_run(unsigned layout,unsigned seen[5]) {
    static const unsigned order[2][5]={{0,1,2,3,4},{4,2,3,1,0}};
    test_created_reset();
    for(unsigned k=0;k<5;k++) {
        uint64_t runtime=layout?0xe0e0000900ULL-k:0xe0e0000100ULL+k;memcpy(test_eo_sources[layout][k]+8,&runtime,8);
        if(k<2) assert(world_identity_record(runtime,k?0x40:0x50));
        if(k==2) test_pc_create(runtime,TEST_PC_CREATED_A);
        if(k==3) test_pc_create(runtime,TEST_PC_CREATED_B);
    }
    test_eo_reset_counters();event_order_created=0;test_eo_layout=layout;
    for(unsigned k=0;k<5;k++) test_eo_put(k,order[layout][k],order[layout][k],1.0);
    test_eo_count=5;test_eo_seen_count=0;test_eo_loop(0);
    assert(test_eo_seen_count==5 && !event_order_failures && event_order_passes==1 && event_order_ties==4);
    memcpy(seen,test_eo_seen,5*sizeof(*seen));
}
static void test_pc_event_order(void) {
    test_pc_begin();
    int saved_active=stats.active,saved_boot=boot_mode,saved_phase=boot_phase;uint64_t saved_issued=stats.issued;
    stats.active=1;boot_mode=0;event_order_armed=1;
    unsigned seen[2][5];uint64_t off_hash[2],on_hash[2];
    for(unsigned layout=0;layout<2;layout++) {
        test_pc_event_run(layout,seen[layout]);off_hash[layout]=event_order_hash;
        assert(event_order_local==3 && event_order_local_ties==3 && !event_order_created);
    }
    const unsigned off0[5]={1,0,2,3,4},off1[5]={1,0,4,3,2};
    assert(!memcmp(seen[0],off0,sizeof(off0)) && !memcmp(seen[1],off1,sizeof(off1)) && off_hash[0]==off_hash[1]);
    world_portable_created=1;
    /* Rows name created sources with their portable ID. */
    event_order_row_cursor=0;event_order_rows_armed=1;event_order_rows_ticks=1;boot_mode=1;boot_phase=3;stats.issued=1;
    for(unsigned layout=0;layout<2;layout++) {
        test_pc_event_run(layout,seen[layout]);on_hash[layout]=event_order_hash;
        assert(event_order_local==1 && event_order_created==2 && event_order_local_ties==1);
    }
    const unsigned on[5]={1,0,3,2,4};
    assert(!memcmp(seen[0],on,sizeof(on)) && !memcmp(seen[1],on,sizeof(on)) && on_hash[0]==on_hash[1] && on_hash[0]!=off_hash[0]);
    char row[384],expect[64];snprintf(expect,sizeof(expect)," slot=2 source=created:%016llx ",(unsigned long long)TEST_PC_CREATED_B);
    assert(event_order_row_cursor==10 && event_order_row(2,row,sizeof(row)) && strstr(row,expect));
    assert(event_order_row(4,row,sizeof(row)) && strstr(row," slot=4 source=runtime:000000e0e0000104 "));
    assert(event_order_row(7,row,sizeof(row)) && strstr(row,expect));
    char status[768];assert(event_order_status(status,sizeof(status)) && strstr(status," created=2"));
    event_order_row_cursor=0;event_order_rows_armed=0;event_order_rows_ticks=0;
    test_eo_reset_counters();event_order_created=0;event_order_armed=0;
    stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;stats.issued=saved_issued;
    for(unsigned layout=0;layout<2;layout++) for(unsigned k=0;k<5;k++) memset(test_eo_sources[layout][k]+8,0,8);
    test_pc_end();
    puts("portable created event order: saved prefix unchanged, created by created ID before local runs, equal order/hash on two layouts, created rows PASS");
}

/* find_* lists. Labels 0,1 saved (0x10, 0x30), 2,3 created (A, B), 4,5
 * unportable; the clients differ in runtime IDs and native order. */
static void test_pc_find_run(unsigned client,unsigned labels[6],uint64_t *set) {
    static const unsigned native[2][6]={{2,1,4,3,0,5},{0,3,4,2,5,1}};void *items[6],*by_label[6];
    test_created_reset();
    for(unsigned label=0;label<6;label++) {
        uint64_t runtime=client?0x2900-label:0x2100+label;
        by_label[label]=test_fo_component(100+client*6+label,runtime);
        if(label<2) assert(world_identity_record(runtime,label?0x30:0x10));
        if(label==2) test_pc_create(runtime,TEST_PC_CREATED_A);
        if(label==3) test_pc_create(runtime,TEST_PC_CREATED_B);
    }
    for(unsigned i=0;i<6;i++) items[i]=by_label[native[client][i]];
    test_fo_set(items,6);uint64_t cursor=(uint64_t)find_order_cursor;
    find_order_process((uintptr_t)(image+FIND_ORDER_SITE+5),test_fo_vector,test_fo_reader,world_identity_key);
    assert(!find_order_failures && (uint64_t)find_order_cursor==cursor+1 && test_fo_count()==6);
    for(unsigned i=0;i<6;i++) {
        labels[i]=6;for(unsigned label=0;label<6;label++) if(test_fo_buffer[i]==(uintptr_t)by_label[label]) labels[i]=label;
        assert(labels[i]<6);
    }
    *set=find_order_ring[cursor%FIND_ORDER_RING].set;
}
static void test_pc_find_order(void) {
    test_pc_begin();
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=23;ui_thread=GetCurrentThreadId();
    find_order_armed=1;find_order_mode=FIND_ORDER_SORT;InterlockedExchange(&find_order_active,1);test_fo_reset();find_order_created=0;
    script_rng_frame *saved_current=script_rng_current;script_rng_current=NULL;
    unsigned got[2][6];uint64_t set[2];
    for(unsigned c=0;c<2;c++) test_pc_find_run(c,got[c],&set[c]);
    const unsigned off0[6]={0,1,2,4,3,5},off1[6]={0,1,3,4,2,5};
    assert(!memcmp(got[0],off0,sizeof(off0)) && !memcmp(got[1],off1,sizeof(off1)) && set[0]==set[1]);
    assert(find_order_unmapped==8 && !find_order_created && find_order_ring[0].unmapped==4 && find_order_ring[0].ids[2]==0);
    world_portable_created=1;test_fo_reset();
    for(unsigned c=0;c<2;c++) test_pc_find_run(c,got[c],&set[c]);
    const unsigned on[6]={0,1,3,2,4,5};
    assert(!memcmp(got[0],on,sizeof(on)) && !memcmp(got[1],on,sizeof(on)) && set[0]==set[1]);
    assert(find_order_unmapped==4 && find_order_created==4 && find_order_ring[1].unmapped==2 && find_order_ring[1].count==6);
    assert(find_order_ring[1].ids[0]==0x10 && find_order_ring[1].ids[1]==0x30 && find_order_ring[1].ids[2]==TEST_PC_CREATED_B &&
           find_order_ring[1].ids[3]==TEST_PC_CREATED_A && !find_order_ring[1].ids[4]);
    char status[768];assert(find_order_status(status,sizeof(status)) && strstr(status," created=4"));
    /* Observe measures the same without writing. */
    find_order_mode=FIND_ORDER_OBSERVE;test_fo_reset();test_pc_find_run(1,got[1],&set[1]);
    assert(got[1][0]==0 && got[1][1]==3 && got[1][2]==4 && set[1]==set[0] && find_order_observed==1);
    find_order_armed=0;find_order_mode=FIND_ORDER_OFF;InterlockedExchange(&find_order_active,0);test_fo_reset();find_order_created=0;
    script_rng_current=saved_current;stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    test_pc_end();
    puts("portable created find order: saved first unchanged, created by created ID, unportable native order, equal lists/sets on two clients PASS");
}

/* Gravidar unit keys: saved always, created only with portable keys. */
static void test_pc_gravidar(void) {
    test_pc_begin();unsigned k=99;
    assert(world_identity_record(0x700,0x31));test_pc_create(0x701,TEST_PC_CREATED_A);
    assert(gravidar_unit_key(0x700,&k)==0x31 && k==WORLD_KEY_SAVED && !gravidar_unit_key(0x701,&k) && k==WORLD_KEY_NONE);
    world_portable_created=1;
    assert(gravidar_unit_key(0x700,&k)==0x31 && k==WORLD_KEY_SAVED);
    assert(gravidar_unit_key(0x701,&k)==TEST_PC_CREATED_A && k==WORLD_KEY_CREATED && !gravidar_unit_key(0x702,&k));
    assert(lockstep_seed(0x4752415649444152ULL,16,TEST_PC_CREATED_A,0,0,0)!=lockstep_seed(0x4752415649444152ULL,16,0x31,0,0,0));
    char status[384];assert(gravidar_key_status(status,sizeof(status)) && strstr(status," created="));
    test_pc_end();
    puts("portable created Gravidar unit keys: saved unchanged, created keyed only when on PASS");
}

/* Script RNG: created AI owners and component event parameters. */
static void test_pc_script_rng(void) {
    test_pc_begin();
    assert(world_identity_record(0x5001,0xa1));
    static test_sr_ai saved_ai,created1,created2;
    test_sr_ai_init(&saved_ai,0x5001,"move.idle",4);
    test_sr_ai_init(&created1,0x8001,"fight.attack",9);test_sr_ai_init(&created2,0x9001,"fight.attack",9); /* two clients */
    uint64_t old_options=boot_options;int old_seeded=interrupt_seeded,old_ai=ai_seeded;
    boot_options=0;interrupt_seeded=0;ai_seeded=0;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=20;ui_thread=GetCurrentThreadId();
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_mode=SCRIPT_RNG_KEY;InterlockedExchange(&script_rng_active,1);
    test_sr_reset();script_rng_created=0;world_identity_sealed=1; /* misses are cached from here on */
    uint64_t sa=script_rng_stream(test_sr_fnv("move.idle"),SR_EXEC,SCRIPT_RNG_AI,3),sc=script_rng_stream(test_sr_fnv("fight.attack"),SR_EXEC,SCRIPT_RNG_AI,3);
    uint64_t word;test_sr_word=&word;test_sr_draws=2;
    /* Off: the saved owner is keyed; the created owner is unmapped and runs on the natural word. */
    test_pc_create(0x8001,TEST_PC_CREATED_A);
    word=0x1234;npc_exec_entry(test_sr_exec,saved_ai.exec,&word,0);
    uint64_t saved_key=script_rng_key(SR_EXEC,20,0xa1,sa,1,4);assert(test_sr_seen[0]==saved_key && word==0x1234);
    word=0x1234;npc_exec_entry(test_sr_exec,created1.exec,&word,0);
    assert(test_sr_seen[0]==0x1234 && word==test_sr_step(test_sr_step(0x1234)) && script_rng_why[SR_UNMAPPED]==1 && !script_rng_created);
    /* On: the same saved key; the created owner keyed by its created ID. */
    world_portable_created=1;test_sr_reset();
    word=0x1234;npc_exec_entry(test_sr_exec,saved_ai.exec,&word,0);assert(test_sr_seen[0]==saved_key);
    uint64_t created_key=script_rng_key(SR_EXEC,20,TEST_PC_CREATED_A,sc,1,9);
    word=0x1234;npc_exec_entry(test_sr_exec,created1.exec,&word,0);
    assert(test_sr_seen[0]==created_key && word==0x1234 && script_rng_created==1 && !script_rng_why[SR_UNMAPPED]);
    assert(script_rng_cursor==2 && script_rng_ring[1].owner==TEST_PC_CREATED_A && script_rng_ring[1].key==created_key);
    /* Client 2: another runtime ID and context address, the same created ID and key. */
    test_created_reset();test_pc_create(0x9001,TEST_PC_CREATED_A);test_sr_reset();
    word=0x1234;npc_exec_entry(test_sr_exec,created2.exec,&word,0);assert(test_sr_seen[0]==created_key);
    /* Creation frame: unassigned is unkeyed (the miss is cached); the boundary's
     * generation change expires the miss and the next frame is keyed. */
    test_created_reset();test_sr_reset();stats.issued=21;
    word=0x55;npc_exec_entry(test_sr_exec,created1.exec,&word,0);assert(test_sr_seen[0]==0x55 && script_rng_why[SR_UNMAPPED]==1);
    word=0x55;npc_exec_entry(test_sr_exec,created1.exec,&word,0);assert(test_sr_seen[0]==0x55 && script_rng_ai_hits==1);
    test_pc_create(0x8001,TEST_PC_CREATED_A);stats.issued=22;
    word=0x55;npc_exec_entry(test_sr_exec,created1.exec,&word,0);
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,22,TEST_PC_CREATED_A,sc,1,9) && word==0x55);
    /* A saved owner's event naming a component: a created one is 0 off and its
     * created ID on; a saved one is its saved ID in both modes. */
    uint64_t st=script_rng_stream(test_sr_fnv("move.idle"),SR_STACK,SCRIPT_RNG_AI,0);
    *(uint32_t*)(saved_ai.callstack+0x18)=0x506;*(uint32_t*)(saved_ai.callstack+0x20)=0xa;
    const uint64_t parameters[2]={0x8001,0x5001},portable[2][2]={{0,0xa1},{TEST_PC_CREATED_A,0xa1}};
    for(unsigned mode=0;mode<2;mode++) for(unsigned p=0;p<2;p++) {
        world_portable_created=mode;test_sr_reset();stats.issued=23;*(uint64_t*)(saved_ai.callstack+0x28)=parameters[p];word=7;
        assert(script_rng_event_run(SR_STACK,test_sr_handler,&word,saved_ai.callstack,NULL,NULL,NULL)==0x77 && word==7);
        assert(test_sr_seen[6]==script_rng_key(SR_STACK,23,0xa1,st,1,lockstep_mix((0x506ULL<<32)^lockstep_mix(portable[mode][p]))));
    }
    char status[2048];assert(script_rng_status(status,sizeof(status)) && strstr(status," identity_sealed=1 created="));
    InterlockedExchange(&script_rng_active,0);script_rng_mask=0;script_rng_mode=0;test_sr_reset();script_rng_created=0;
    boot_options=old_options;interrupt_seeded=old_seeded;ai_seeded=old_ai;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    test_pc_end();
    puts("portable created script RNG: saved keys unchanged, created owners keyed by created ID on two clients, creation-frame miss expiry, event parameters PASS");
}
static void test_portable_created(void) {
    test_pc_keys();
    test_pc_path_determinism();
    test_pc_event_order();
    test_pc_find_order();
    test_pc_gravidar();
    test_pc_script_rng();
}
