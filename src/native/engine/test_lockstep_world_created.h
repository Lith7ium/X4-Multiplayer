/* Created identities: owned memory, synthetic objects and the pinned image
 * read-only. No X4 code is executed. */
#define TEST_CREATED_VT_ZONE 0x6e00000u
#define TEST_CREATED_VT_SHIP 0x6e02000u
#define TEST_CREATED_VT_WEAPON 0x6e04000u
#define TEST_CREATED_VT_EFFECT 0x6e06000u
#define TEST_CREATED_VT_UI 0x6e08000u
#define TEST_CREATED_STUBS 0x6e10000u
#define TEST_CREATED_BITMAPS 0x6e11000u
typedef struct {uint64_t runtime;unsigned char *object;int present;} test_created_slot;
static test_created_slot test_created_map[64];
static unsigned test_created_map_count,test_created_original_calls;
static void *test_created_find(void *map,uint64_t runtime,unsigned mode) {
    assert(map==(void*)test_created_map && mode==4);
    for(unsigned i=0;i<test_created_map_count;i++)
        if(test_created_map[i].present && test_created_map[i].runtime==runtime) return test_created_map[i].object;
    return NULL;
}
static void test_created_present(uint64_t runtime,int present) {
    for(unsigned i=0;i<test_created_map_count;i++) if(test_created_map[i].runtime==runtime) test_created_map[i].present=present;
}
/* A verified IsRealClass stub (the pinned 36-byte body) with its own bitmap. */
static void test_created_class(unsigned vtable,unsigned index,const unsigned *classes,unsigned count) {
    static const unsigned char stub[36]={0x48,0x63,0xc2,0x48,0x8d,0x15,0,0,0,0,0x48,0x8b,0xc8,0x24,0x0f,0x48,0xc1,0xe9,0x04,0x02,0xc0,
        0x8b,0x14,0x8a,0x0f,0xb6,0xc8,0xd3,0xea,0xf6,0xc2,0x03,0x0f,0x95,0xc0,0xc3};
    unsigned code=TEST_CREATED_STUBS+index*64,bitmap=TEST_CREATED_BITMAPS+index*32;
    memcpy(image+code,stub,sizeof(stub));int32_t relative=(int32_t)bitmap-(int32_t)(code+10);memcpy(image+code+6,&relative,4);
    uint32_t words[8]={0};for(unsigned i=0;i<count;i++) words[classes[i]/16]|=1u<<(2*(classes[i]%16));
    memcpy(image+bitmap,words,sizeof(words));*(void**)(image+vtable+0x11e0)=image+code;
}
typedef struct {unsigned char object[0x100],macro[0x40];char name[96];} test_created_object;
static void test_created_object_init(test_created_object *o,unsigned vtable,uint64_t runtime,const char *macro,const test_created_object *parent) {
    memset(o,0,sizeof(*o));*(void**)o->object=image+vtable;*(uint64_t*)(o->object+8)=runtime;
    if(macro) {
        strcpy(o->name,macro);*(void**)o->macro=image+0x2ba5430;*(uint64_t*)(o->macro+0x30)=strlen(macro);*(uint64_t*)(o->macro+0x38)=95;
        *(char**)(o->macro+0x20)=o->name;*(void**)(o->object+0x60)=o->macro;
    }
    if(parent) *(const void**)(o->object+0x70)=parent->object;
    test_created_map[test_created_map_count++]=(test_created_slot){runtime,o->object,1};
}
static void test_created_reset(void) {
    if(created_by_runtime) memset(created_by_runtime,0,sizeof(*created_by_runtime)*CREATED_INDEX);
    if(created_by_id) memset(created_by_id,0,sizeof(*created_by_id)*CREATED_INDEX);
    created_live_count=created_pending_count=created_peak=created_last_assigned=0;
    created_frames=created_assigned=created_retired=created_transient=created_other=created_excluded=0;
    created_foreign_main=created_foreign_other=created_orphans=created_macro_unknown=created_rehash=created_imported=created_duplicate=0;
    created_calls=created_noted=created_unreadable=created_pending_overflow=created_failures=0;
    created_row_cursor=0;created_digest=0xcbf29ce484222325ULL;created_stage="idle";test_created_map_count=0;
}
static uint64_t test_created_original(void *object) {
    ++test_created_original_calls;*(uint64_t*)((unsigned char*)object+8)=0x2000000000000000ULL+test_created_original_calls;
    SetLastError(4321);return 0x1234;
}
/* One client's tick-14 spawn: zone in a saved sector, ships, weapons, an
 * effect, a UI element, a transient, a foreign ship and a child noted before
 * its parent. Runtime IDs, addresses and UI noise vary by client. */
typedef struct {test_created_object sector,zone,a,a1,a2,b,b1,effect,ui,foreign,c1,c,late;} test_created_world;
static uint64_t test_created_spawn(test_created_world *w,uint64_t base,unsigned noise,int skip_b,uint64_t ids[10]) {
    test_created_object_init(&w->sector,TEST_CREATED_VT_ZONE,base,"cluster_01_sector001_macro",NULL);
    assert(world_identity_record(base,0x3fd));
    test_created_object_init(&w->zone,TEST_CREATED_VT_ZONE,base+1,"tempzone",&w->sector);
    test_created_object_init(&w->a,TEST_CREATED_VT_SHIP,base+2,"ship_arg_s_fighter_01_a_macro",&w->zone);
    test_created_object_init(&w->a1,TEST_CREATED_VT_WEAPON,base+3,"weapon_arg_s_laser_01_mk1_macro",&w->a);
    test_created_object_init(&w->a2,TEST_CREATED_VT_WEAPON,base+4,"weapon_arg_s_laser_01_mk1_macro",&w->a);
    test_created_object_init(&w->b,TEST_CREATED_VT_SHIP,base+5,"ship_arg_s_fighter_01_a_macro",&w->zone);
    test_created_object_init(&w->b1,TEST_CREATED_VT_WEAPON,base+6,"weapon_arg_s_laser_01_mk1_macro",&w->b);
    test_created_object_init(&w->effect,TEST_CREATED_VT_EFFECT,base+7,"effect_explosion_macro",&w->zone);
    test_created_object_init(&w->ui,TEST_CREATED_VT_UI,base+8,NULL,NULL);
    test_created_object_init(&w->foreign,TEST_CREATED_VT_SHIP,base+10,"ship_arg_s_fighter_01_a_macro",&w->zone);
    test_created_object_init(&w->c1,TEST_CREATED_VT_WEAPON,base+11,"weapon_arg_s_laser_01_mk1_macro",&w->c);
    test_created_object_init(&w->c,TEST_CREATED_VT_SHIP,base+12,"ship_arg_s_fighter_01_a_macro",&w->zone);
    test_created_object *order[]={&w->zone,&w->a,&w->a1,&w->a2,&w->b,&w->b1,&w->effect,&w->ui,NULL,&w->foreign,&w->c1,&w->c};
    for(unsigned i=0;i<sizeof(order)/sizeof(*order);i++) {
        for(unsigned j=0;j<noise;j++) created_note_with(w->ui.object,CREATED_MOVEWORKER);
        if(skip_b && (order[i]==&w->b || order[i]==&w->b1)) continue;
        if(!order[i]) { /* transient: noted, gone before the boundary */
            unsigned char gone[16]={0};*(uint64_t*)(gone+8)=base+9;created_note_with(gone,CREATED_MOVEWORKER);continue;
        }
        created_note_with(order[i]->object,order[i]==&w->foreign?CREATED_OTHER:CREATED_MOVEWORKER);
    }
    assert(created_frame_with(14,test_created_find,test_created_map));
    const test_created_object *listed[]={&w->zone,&w->a,&w->a1,&w->a2,&w->b,&w->b1,&w->c,&w->c1,&w->effect,&w->foreign};
    for(unsigned i=0;i<10;i++) ids[i]=created_find_runtime(*(const uint64_t*)(listed[i]->object+8));
    return created_find_runtime(base+1);
}
static void test_created_table(void) {
    test_created_reset();
    uint64_t key=created_key(0x3fd,0x1234,created_text_hash("ship_arg_s_fighter_01_a_macro"));
    uint64_t id=created_make_id(14,key,0,0);
    assert(id>>63 && ((id>>CREATED_TICK_SHIFT)&CREATED_TICK_LIMIT)==14 && id==created_make_id(14,key,0,0));
    assert(id!=created_make_id(14,key,1,0) && id!=created_make_id(15,key,0,0) && id!=created_make_id(14,key,0,1) &&
           id!=created_make_id(14,key^1,0,0) && key!=created_key(0x3fe,0x1234,created_text_hash("ship_arg_s_fighter_01_a_macro")) &&
           key!=created_key(0x3fd,0x1235,created_text_hash("ship_arg_s_fighter_01_a_macro")));
    assert(created_text_hash("")>=2 && created_text_hash("a")!=created_text_hash("b"));
    /* Insert/remove against a reference, including forced home collisions. */
    static uint64_t runtimes[4096],ids[4096];static unsigned char alive[4096];unsigned live=0;
    uint64_t colliding=1;unsigned home=created_home(1);
    for(unsigned i=0;i<4096;i++) {
        if(i<64) {do ++colliding;while(created_home(colliding)!=home);runtimes[i]=colliding;}
        else runtimes[i]=lockstep_mix(i)|1;
        ids[i]=created_make_id(1+i%7,runtimes[i],i,0);
        assert(created_live_add(runtimes[i],ids[i],1));alive[i]=1;++live;
    }
    assert(!created_live_add(runtimes[5],0x8000000000000001ULL,1) && !created_live_add(77,ids[5],1));
    uint64_t state=12345;
    for(unsigned step=0;step<20000;step++) {
        state=lockstep_mix(state+step);unsigned i=(unsigned)(state%4096);
        if(alive[i]) {
            unsigned slot=created_index_slot(created_by_runtime,0,runtimes[i]);assert(created_by_runtime[slot]);
            created_live_remove(created_by_runtime[slot]-1u);alive[i]=0;--live;
        } else {assert(created_live_add(runtimes[i],ids[i],2));alive[i]=1;++live;}
        if(step%997==0 || step==19999)
            for(unsigned j=0;j<4096;j++) assert(created_find_runtime(runtimes[j])==(alive[j]?ids[j]:0) && created_find_id(ids[j])==(alive[j]?runtimes[j]:0));
    }
    assert(created_live_count==live);
    /* Capacity: the table refuses beyond CREATED_LIVE, never overwrites. */
    test_created_reset();
    for(unsigned i=0;i<CREATED_LIVE;i++) assert(created_live_add(0x100000000ULL+i,CREATED_BIT|i,1));
    assert(!created_live_add(7,CREATED_BIT|0x7fffffffULL,1) && created_live_count==CREATED_LIVE);
    assert(created_find_runtime(0x100000000ULL+CREATED_LIVE-1)==(CREATED_BIT|(CREATED_LIVE-1)));
    test_created_reset();
    puts("created identity: ID scheme, two-way table with collision chains, 20000 insert/remove steps and capacity refusal verified");
}
static void test_created_hook(void) {
    test_created_reset();unsigned char object[16]={0};
    created_recording=0;test_created_original_calls=0;SetLastError(1);
    assert(created_assign_execute(test_created_original,object)==0x1234 && test_created_original_calls==1 && GetLastError()==4321);
    assert(created_calls==1 && !created_noted && !created_pending_count);
    created_recording=1;
    assert(created_assign_execute(test_created_original,object)==0x1234 && test_created_original_calls==2 && GetLastError()==4321);
    assert(created_noted==1 && created_pending_count==1 && created_pending[0].runtime==0x2000000000000002ULL);
    DWORD prior=main_thread;main_thread=GetCurrentThreadId();assert(created_thread_class()==CREATED_MAIN);main_thread=prior;
    memset(object,0,sizeof(object));created_note_with(object,CREATED_MOVEWORKER);assert(created_unreadable==1 && created_pending_count==1);
    /* Pending overflow is a named failure at the next boundary. */
    *(uint64_t*)(object+8)=99;created_pending_count=CREATED_PENDING;created_note_with(object,CREATED_MOVEWORKER);
    assert(created_pending_overflow==1);created_pending_count=0;
    assert(!created_frame_with(3,test_created_find,test_created_map) && !strcmp(created_stage,"pending_overflow") && created_failures==1);
    assert(!created_frame_with(4,test_created_find,test_created_map));
    test_created_reset();
    assert(!created_frame_with(CREATED_TICK_LIMIT+1,test_created_find,test_created_map) && !strcmp(created_stage,"tick_range"));
    test_created_reset();created_recording=0;assert(created_frame_with(5,test_created_find,test_created_map) && !created_frames);
    created_recording=1;test_created_reset();
    puts("created identity hook: original once with argument/result/LastError, recording gate, thread class, overflow and tick-range refusals verified");
}
static void test_created_frames(void) {
    const unsigned zone[]={22,77,109},ship[]={22,35,36,73,77,91,112,117},weapon[]={22,36,77,107},effect[]={22,42,77};
    test_created_class(TEST_CREATED_VT_ZONE,0,zone,3);test_created_class(TEST_CREATED_VT_SHIP,1,ship,8);
    test_created_class(TEST_CREATED_VT_WEAPON,2,weapon,4);test_created_class(TEST_CREATED_VT_EFFECT,3,effect,3);
    *(void**)(image+TEST_CREATED_VT_UI+0x11e0)=NULL;*(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    memset(created_classes,0,sizeof(created_classes));
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    static test_created_world first,second,third;uint64_t a[10],b[10],c[10];
    /* Client 1. */
    test_created_reset();created_rows_armed=1;created_recording=1;created_armed=1;
    test_created_spawn(&first,0x1000,0,0,a);
    assert(created_assigned==8 && created_live_count==8 && created_excluded==1 && created_other==1 && created_transient==1 &&
           created_foreign_other==1 && !created_orphans && !created_macro_unknown && !created_failures);
    assert(!a[8] && !a[9]); /* effect excluded, foreign ship unmapped */
    for(unsigned i=0;i<8;i++) {
        assert(a[i]>>63 && ((a[i]>>CREATED_TICK_SHIFT)&CREATED_TICK_LIMIT)==14);
        for(unsigned j=0;j<i;j++) assert(a[i]!=a[j]);
    }
    /* Keys: parent portable ID, class, macro; ordinals follow creation order. */
    uint64_t ship_key=created_key(a[0],TEST_CREATED_VT_SHIP,created_text_hash("ship_arg_s_fighter_01_a_macro"));
    uint64_t gun=created_text_hash("weapon_arg_s_laser_01_mk1_macro");
    assert(a[0]==created_make_id(14,created_key(0x3fd,TEST_CREATED_VT_ZONE,created_text_hash("tempzone")),0,0));
    assert(a[1]==created_make_id(14,ship_key,0,0) && a[4]==created_make_id(14,ship_key,1,0) && a[6]==created_make_id(14,ship_key,2,0));
    assert(a[2]==created_make_id(14,created_key(a[1],TEST_CREATED_VT_WEAPON,gun),0,0) && a[3]==created_make_id(14,created_key(a[1],TEST_CREATED_VT_WEAPON,gun),1,0));
    assert(a[5]==created_make_id(14,created_key(a[4],TEST_CREATED_VT_WEAPON,gun),0,0)); /* child of B, not of A */
    assert(a[7]==created_make_id(14,created_key(a[6],TEST_CREATED_VT_WEAPON,gun),0,0)); /* noted before its parent */
    assert(world_identity_portable(0x1002)==a[1] && world_identity_portable(0x1000)==0x3fd && !world_identity_portable(0x1008));
    assert(created_runtime_of(a[1])==0x1002 && !created_runtime_of(0x3fd));
    /* Rows: assignments in resolution order, the foreign ship, then a retirement. */
    char text[256];unsigned assigns=0,foreign=0;
    for(uint64_t i=0;created_row_text(i,text,sizeof(text));i++) {
        if(strstr(text,"kind=assign")) {++assigns;assert(strstr(text," tick=14 ") && strstr(text," macro="));}
        if(strstr(text,"kind=foreign")) {++foreign;assert(strstr(text,"thread=other") && strstr(text,"macro=ship_arg_s_fighter_01_a_macro"));}
    }
    assert(assigns==8 && foreign==1);
    snprintf(text,sizeof(text),"kind=assign tick=14 id=%016llx parent=%016llx type=%x ordinal=1 macro=ship_arg_s_fighter_01_a_macro",
        (unsigned long long)a[4],(unsigned long long)a[0],TEST_CREATED_VT_SHIP);
    char row[256];int found=0;for(uint64_t i=0;created_row_text(i,row,sizeof(row));i++) if(strstr(row,text)) found=1;
    assert(found);
    uint64_t digest=created_digest;
    /* Capture rows: present, never captured; sorted; kept once captured. */
    static world_hash_item items[16];unsigned count=0;
    assert(created_capture_rows(test_created_find,test_created_map,items,&count,16) && count==8);
    for(unsigned i=1;i<count;i++) assert(items[i-1].saved<items[i].saved);
    assert(created_capture_rows(test_created_find,test_created_map,items,&count,16) && count==8);
    /* Destruction: retired at the next boundary, row kept by the capture list. */
    test_created_present(0x1003,0);
    assert(created_frame_with(15,test_created_find,test_created_map) && created_retired==1 && created_live_count==7 &&
           !created_find_runtime(0x1003) && !created_find_id(a[2]) && created_find_runtime(0x1002)==a[1]);
    assert(created_capture_rows(test_created_find,test_created_map,items,&count,16) && count==8);
    found=0;for(uint64_t i=0;created_row_text(i,row,sizeof(row));i++) if(strstr(row,"kind=retire tick=15")) found=1;
    assert(found);
    /* A later creation of the same key starts again at ordinal 0 of its tick. */
    test_created_object_init(&first.late,TEST_CREATED_VT_SHIP,0x1020,"ship_arg_s_fighter_01_a_macro",&first.zone);
    created_note_with(first.late.object,CREATED_MOVEWORKER);
    assert(created_frame_with(20,test_created_find,test_created_map) && created_find_runtime(0x1020)==created_make_id(20,ship_key,0,0));
    /* Capacity of the capture rows fails closed with a named stage. */
    assert(!created_capture_rows(test_created_find,test_created_map,items,&count,8) && !strcmp(created_stage,"capture_rows_full"));
    /* Client 2: other runtime IDs, addresses and UI noise give the same IDs. */
    test_created_reset();test_created_spawn(&second,0x7000,3,0,b);
    assert(!memcmp(a,b,sizeof(a)) && created_digest==digest && created_other==1+3*12);
    /* Client 3 never creates ship B: its creation mismatch is an inventory
     * difference (B's child and C's tick-14 ordinal-2 ID are missing). */
    test_created_reset();test_created_spawn(&third,0x9000,0,1,c);
    assert(created_assigned==6 && c[6]==a[4] && !c[4] && !c[5]);
    unsigned missing=0;
    for(unsigned i=0;i<8;i++) {int seen=0;for(unsigned j=0;j<8;j++) if(c[j]==a[i]) seen=1;missing+=!seen;}
    assert(missing==2);
    /* A parent without a portable identity is the orphan key, not a runtime ID. */
    test_created_reset();test_created_object orphan_parent,orphan;
    test_created_object_init(&orphan_parent,TEST_CREATED_VT_ZONE,0x5000,"tempzone",NULL);
    test_created_object_init(&orphan,TEST_CREATED_VT_SHIP,0x5001,"ship_arg_s_fighter_01_a_macro",&orphan_parent);
    created_note_with(orphan.object,CREATED_MOVEWORKER);
    assert(created_frame_with(30,test_created_find,test_created_map) && created_orphans==1 &&
           created_find_runtime(0x5001)==created_make_id(30,created_key(CREATED_ORPHAN,TEST_CREATED_VT_SHIP,created_text_hash("ship_arg_s_fighter_01_a_macro")),0,0));
    /* Disabled: the portable lookup is exactly the saved lookup. */
    created_armed=0;assert(!world_identity_portable(0x5001) && !created_runtime_of(created_find_runtime(0x5001)));
    char status[1024];assert(created_status(status,sizeof(status)) && strstr(status,"assigned=1 ") && strstr(status,"orphans=1 "));
    created_recording=0;created_rows_armed=0;test_created_reset();
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    puts("created identity frames: parents first, per-key ordinals, client-independent IDs, exclusions, foreign/transient/orphan, retirement, capture rows and mismatch verified");
}
/* Arm refusals in owned memory: no world identities, no pins, wrong phase. */
static void test_created_arm_refusals(void) {
    unsigned char *prior_thunk=thunk;
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identity_installed=0;assert(!created_arm(0) && !created_installed && !created_armed);
    world_identity_installed=1;world_identities=NULL;assert(!created_arm(0));
    world_identities=(void*)1;thunk=NULL;assert(!created_arm(0));
    thunk=prior_thunk;if(!thunk) thunk=(unsigned char*)1;assert(!created_arm(0)); /* owned image fails the pins */
    thunk=prior_thunk;world_identities=NULL;world_identity_installed=0;stats.active=0;boot_mode=0;
    assert(!created_installed && !created_armed && !created_recording);
}
/* World capture on the pinned image copy: saved rows, then created rows; a
 * created parent reference is portable; a destroyed created row stays absent. */
static void test_created_capture_image(void) {
    uint64_t getter;memcpy(&getter,image+0x2ba5430+18*8,8);
    if(getter>=0x140000000ULL) {uintptr_t rebased=(uintptr_t)image+(uintptr_t)(getter-0x140000000ULL);memcpy(image+0x2ba5430+18*8,&rebased,8);}
    *(uint32_t*)(image+0x38f4ee4)=0;*(uint32_t*)(image+0x2f4b6f4)=1;*(void**)(image+0x6cf1500)=test_created_map;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(100,0x77));
    test_created_reset();memset(created_classes,0,sizeof(created_classes));
    static test_created_object saved,zone,ship;
    test_created_object_init(&saved,0x2a91750,100,"cluster_01_sector001_macro",NULL);
    test_created_object_init(&zone,0x2a91750,101,"tempzone",&saved);
    test_created_object_init(&ship,0x2a91750,102,"ship_arg_s_fighter_01_a_macro",&zone);
    created_armed=1;created_recording=1;
    created_note_with(ship.object,CREATED_MOVEWORKER);created_note_with(zone.object,CREATED_MOVEWORKER);
    assert(created_frame_with(1,test_created_find,test_created_map) && created_assigned==2 && !created_orphans);
    uint64_t z=created_find_runtime(101),a=created_find_runtime(102);
    assert(z==created_make_id(1,created_key(0x77,0x2a91750,created_text_hash("tempzone")),0,0));
    assert(a==created_make_id(1,created_key(z,0x2a91750,created_text_hash("ship_arg_s_fighter_01_a_macro")),0,0));
    world_hash_items=NULL;world_hash_created=NULL;world_hash_count=world_hash_created_count=0;world_hash_scans=0;
    world_hash_tick=0;world_hash_failures=0;world_hash_watch_count=0;world_class_count=0;
    assert(world_hash_capture_with(1,1,test_created_find));
    assert(world_hash_count==1 && world_hash_created_count==2 && world_hash_changes==3 && world_hash_alive==3);
    assert(strstr(world_hash_summary,"objects=3 present=3 changed=3 ") && strstr(world_hash_summary," created=2 state=WORLD_IMPORTED_1:1:3:"));
    const world_hash_item *first=world_hash_item_at(0),*second=world_hash_item_at(1),*third=world_hash_item_at(2);
    assert(first->saved==0x77 && second->saved==(z<a?z:a) && third->saved==(z<a?a:z));
    const world_hash_item *ship_row=second->saved==a?second:third,*zone_row=second->saved==z?second:third;
    assert(ship_row->parent==z && zone_row->parent==0x77 && !(ship_row->flags&16));
    test_created_present(102,0);
    assert(created_frame_with(2,test_created_find,test_created_map) && created_retired==1);
    assert(world_hash_capture_with(2,1,test_created_find) && world_hash_created_count==2 && world_hash_changes==1 && world_hash_alive==2);
    ship_row=world_hash_item_at(world_hash_changed[0]);assert(ship_row->saved==a && !ship_row->flags && !ship_row->type);
    assert(strstr(world_hash_summary,"objects=3 present=2 changed=1 ") && strstr(world_hash_summary," created=2 "));
    /* Disabled: no created rows and the summary keeps its original form. */
    VirtualFree(world_hash_created,0,MEM_RELEASE);VirtualFree(world_hash_items,0,MEM_RELEASE);VirtualFree(world_hash_changed,0,MEM_RELEASE);
    world_hash_items=NULL;world_hash_created=NULL;world_hash_changed=NULL;world_hash_count=world_hash_created_count=0;world_hash_scans=0;world_hash_tick=0;
    created_armed=0;created_recording=0;
    assert(world_hash_capture_with(3,1,test_created_find) && world_hash_count==1 && !world_hash_created && !strstr(world_hash_summary,"created="));
    assert(strstr(world_hash_summary,"objects=1 present=1 changed=1 unknown_class=0 unknown_motion=0 unknown_references=0 state=WORLD_IMPORTED_1:3:1:"));
    VirtualFree(world_hash_items,0,MEM_RELEASE);VirtualFree(world_hash_changed,0,MEM_RELEASE);
    world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;world_hash_scans=0;world_hash_tick=0;world_hash_failures=0;world_class_count=0;
    world_hash_schema=1;world_hash_stage="not_started";
    test_created_reset();VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
}
/* Pinned image: AssignNewID body and lookup, native entry word, class names
 * of the exclusion list, then actual install/restore on a private copy. */
static void test_created_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000 &&
           nt.FileHeader.TimeDateStamp==0x6a1eaded);
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
    assert(created_pins());
    const unsigned tails[]={CREATED_ENTRY,CREATED_ENTRY+0xbf,CREATED_ENTRY+267,0xced70,0xced70+183};
    for(unsigned i=0;i<sizeof(tails)/sizeof(*tails);i++) {image[tails[i]]^=1;assert(!created_pins());image[tails[i]]^=1;}
    /* AssignNewID writes the returned ID to object+8 (mov [rsi+8],rdi) and
     * InsertAtSlot cc500 is its only call into the map. */
    static const unsigned char store[4]={0x48,0x89,0x7e,0x08};assert(!memcmp(image+0xcc2df,store,4));
    int32_t rel;memcpy(&rel,image+0xcc2ce,4);assert(0xcc2d2+rel==0xcc500);
    /* Native class name table (16-byte {name, id} entries at 0x22c6790). */
    const struct {unsigned id;const char *name;} names[]={{22,"component"},{11,"bullet"},{32,"cutsceneanchor"},{42,"effectobject"},
        {46,"fogvolume"},{58,"holomap"},{61,"lensflare"},{97,"stardust"},{105,"uielement"},{36,"destructible"},{73,"object"},{77,"positional"}};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++) {
        uint64_t pointer,id;memcpy(&pointer,image+0x22c6790+16*names[i].id,8);memcpy(&id,image+0x22c6790+16*names[i].id+8,8);
        assert(id==names[i].id && pointer>=0x140000000ULL && pointer-0x140000000ULL<0x6fa9000ULL &&
               !strcmp((const char*)image+(pointer-0x140000000ULL),names[i].name));
    }
    for(unsigned i=0;i<sizeof(created_excluded_classes)/sizeof(*created_excluded_classes);i++) {
        int listed=0;for(unsigned j=0;j<sizeof(names)/sizeof(*names);j++) listed|=names[j].id==created_excluded_classes[i];
        assert(listed);
    }
    /* Component (0x2a91750) and a ship class decode through the verified stub.
     * The file copy is not relocated: rebase the three vtable slots read. */
    const unsigned vtables[]={0x2a91750,0x2b99a88,0x2a2a8e8};
    for(unsigned i=0;i<3;i++) {
        uint64_t slot;memcpy(&slot,image+vtables[i]+0x11e0,8);assert(slot>=0x140000000ULL && slot-0x140000000ULL<0x6fa9000ULL);
        uintptr_t rebased=(uintptr_t)image+(uintptr_t)(slot-0x140000000ULL);memcpy(image+vtables[i]+0x11e0,&rebased,8);
    }
    uint32_t words[8];
    assert(world_class_words((uintptr_t)(image+0x2a91750),words) && world_class_has(words,22) && !world_class_has(words,36));
    assert(world_class_words((uintptr_t)(image+0x2b99a88),words) && world_class_has(words,22) && world_class_has(words,94) && !world_class_has(words,42));
    memset(created_classes,0,sizeof(created_classes));
    assert(created_classify((uintptr_t)(image+0x2b99a88))==CREATED_KIND_COMPONENT && created_classify((uintptr_t)(image+0x2a2a8e8))==CREATED_KIND_OTHER);
    memset(created_classes,0,sizeof(created_classes));
    /* Install and restore on the private copy through a real sealed page. */
    unsigned char *prior_thunk=thunk;thunk=make_thunk();assert(thunk && thunk_slot_ready(CREATED_THUNK));
    static const unsigned char native[8]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74};
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(world_identities);
    boot_phase=2;assert(!created_arm(1));boot_phase=0;
    assert(created_arm(1) && created_installed && created_armed && created_rows_armed && !created_recording);
    int32_t jump;assert(image[CREATED_ENTRY]==0xe9 && !memcmp(image+CREATED_ENTRY+5,native+5,3));
    memcpy(&jump,image+CREATED_ENTRY+1,4);assert(image+CREATED_ENTRY+5+jump==thunk+CREATED_THUNK+32);
    void *continuation;memcpy(&continuation,thunk+CREATED_THUNK+5+6,8);assert(!memcmp(thunk+CREATED_THUNK,native,5) && continuation==image+CREATED_ENTRY+5);
    void *target;memcpy(&target,thunk+CREATED_THUNK+32+6,8);assert(target==(void*)created_assign);
    assert(created_arm(1)); /* loading-UI reload accepts the installed owner */
    boot_phase=3;created_begin();assert(created_recording);
    *(uint64_t*)(image+CREATED_ENTRY)^=1ULL<<56;created_restore();assert(!created_restored && created_installed && !created_recording);
    *(uint64_t*)(image+CREATED_ENTRY)^=1ULL<<56;created_restore();
    assert(created_restored && !created_installed && !memcmp(image+CREATED_ENTRY,native,8) && created_pins());
    created_armed=created_rows_armed=0;created_restored=0;created_original_word=created_patched_word=0;
    stats.active=0;boot_mode=0;boot_phase=0;world_identity_installed=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior_thunk;
    test_created_capture_image();
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("created identity native pins: AssignNewID/lookup bodies, object+8 store, class names, verified class decode, tamper refusals, sealed-page install/restore and saved+created world capture verified");
}
static void test_world_created(void) {
    test_created_arm_refusals();
    created_allocate(1);
    test_created_table();
    test_created_hook();
    test_created_frames();
    created_recording=0;created_armed=0;created_rows_armed=0;test_created_reset();
}
