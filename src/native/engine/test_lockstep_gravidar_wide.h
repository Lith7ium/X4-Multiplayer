/* native_gate_gravidar_wide: a scan's wide-list applies are kept and run in
 * vector order at the call of the sector's last group with entries; empty
 * lists pass through in place; the group's sector entry and index; failures
 * (foreign return address, group outside the sector entries, kept lists of
 * another scan, overflow, an exception in an apply) count, drop or flush as
 * documented; a stop inside a scan flushes its kept lists; the call site
 * prepare/swap/owned/restore; arming refusals; status bound. */
typedef struct {unsigned char bytes[GWIDE_ENTRY];} test_gwide_entry_t;
static unsigned char test_gwide_worker[0x7000],test_gwide_sectors[2][0x80];
static unsigned char test_gwide_records[2*GWIDE_RECORD],test_gwide_groups[5*GWIDE_GROUP];
static test_gwide_entry_t test_gwide_entries[8];
static void *test_gwide_log[16];
static unsigned test_gwide_calls;
static int test_gwide_throw;
static void test_gwide_native(void *worker,void *list) {
    assert(worker==test_gwide_worker);if(test_gwide_calls<16) test_gwide_log[test_gwide_calls]=list;
    ++test_gwide_calls;
    if(test_gwide_throw) RaiseException(0xe0475744u,0,0,NULL);
}
static void test_gwide_list(unsigned group,unsigned wide,unsigned first,unsigned count) {
    uintptr_t bounds[3]={(uintptr_t)test_gwide_entries[first].bytes,(uintptr_t)test_gwide_entries[first+count].bytes,(uintptr_t)test_gwide_entries[first+count].bytes};
    memcpy(test_gwide_groups+group*GWIDE_GROUP+(wide?GWIDE_WIDE:GWIDE_NEAR),bounds,24);
}
static void *test_gwide_wide(unsigned group) {return test_gwide_groups+group*GWIDE_GROUP+GWIDE_WIDE;}
/* Two sectors: A = groups 0..3 (0 near, 1 near + wide station, 2 near, 3 empty), B = group 4 (wide). */
static void test_gwide_world(void) {
    memset(test_gwide_worker,0,sizeof(test_gwide_worker));memset(test_gwide_groups,0,sizeof(test_gwide_groups));
    memset(test_gwide_entries,0,sizeof(test_gwide_entries));memset(test_gwide_sectors,0,sizeof(test_gwide_sectors));
    for(unsigned i=0;i<2;i++) {
        uint64_t id=0x74+i;memcpy(test_gwide_sectors[i]+8,&id,8);unsigned char *r=test_gwide_records+i*GWIDE_RECORD;
        uintptr_t record[3]={(uintptr_t)test_gwide_sectors[i],(uintptr_t)(test_gwide_groups+(i?4:0)*GWIDE_GROUP),(uintptr_t)(test_gwide_groups+(i?5:4)*GWIDE_GROUP)};
        memcpy(r,record,24);
    }
    uintptr_t records[2]={(uintptr_t)test_gwide_records,(uintptr_t)(test_gwide_records+2*GWIDE_RECORD)};memcpy(test_gwide_worker+GWIDE_SECTORS,records,16);
    for(unsigned g=0;g<5;g++) {
        uintptr_t empty[2]={(uintptr_t)test_gwide_entries[7].bytes,(uintptr_t)test_gwide_entries[7].bytes};
        memcpy(test_gwide_groups+g*GWIDE_GROUP+GWIDE_NEAR,empty,16);memcpy(test_gwide_groups+g*GWIDE_GROUP+GWIDE_WIDE,empty,16);
    }
    test_gwide_list(0,0,0,1);test_gwide_list(1,0,1,1);test_gwide_list(1,1,2,1);test_gwide_list(2,0,3,2);test_gwide_list(4,1,5,1);
    for(unsigned i=0;i<7;i++) {uint64_t sector=i==5?0x75:0x74;memcpy(test_gwide_entries[i].bytes+8,&sector,8);}
    test_gwide_calls=0;gwide_pending=gwide_last=0;gwide_record=NULL;gwide_hint=0;gwide_failures=0;gwide_stage="armed";
    gwide_calls=gwide_kept=gwide_entries=gwide_flushes=gwide_direct=gwide_max=gwide_late=0;
}
static void test_gwide_scan(unsigned first,unsigned last) {
    for(unsigned g=first;g<=last;g++) gwide_run((uintptr_t)(image+GWIDE_SITE+5),test_gwide_worker,test_gwide_wide(g));
}
static void test_gravidar_wide(void) {
    gwide_apply_override=test_gwide_native;test_gwide_world();InterlockedExchange(&gwide_active,1);
    /* Sector entry and group of a group, of an entry. */
    uintptr_t groups[2];unsigned index=9,group=9,wide=9;
    assert(gwide_record_of(test_gwide_worker,test_gwide_groups+2*GWIDE_GROUP,groups,&index)==test_gwide_records && index==2 &&
           groups[0]==(uintptr_t)test_gwide_groups && groups[1]==(uintptr_t)(test_gwide_groups+4*GWIDE_GROUP));
    assert(gwide_hint==(uintptr_t)test_gwide_records && gwide_record_of(test_gwide_worker,test_gwide_groups,groups,&index)==test_gwide_records && !index);
    assert(gwide_record_of(test_gwide_worker,test_gwide_groups+4*GWIDE_GROUP,groups,&index)==test_gwide_records+GWIDE_RECORD && index==0 &&
           gwide_hint==(uintptr_t)(test_gwide_records+GWIDE_RECORD));
    gwide_hint=(uintptr_t)test_gwide_records+8; /* not an entry: ignored */
    assert(gwide_record_of(test_gwide_worker,test_gwide_groups+GWIDE_GROUP,groups,&index)==test_gwide_records && index==1 && gwide_hint==(uintptr_t)test_gwide_records);
    assert(!gwide_record_of(test_gwide_worker,test_gwide_groups+2*GWIDE_GROUP+8,groups,&index) && !gwide_record_of(NULL,test_gwide_groups,groups,&index));
    assert(gwide_entry_home(test_gwide_worker,test_gwide_entries[2].bytes,groups,&group,&wide)==test_gwide_records && group==1 && wide==1);
    assert(gwide_entry_home(test_gwide_worker,test_gwide_entries[4].bytes,groups,&group,&wide)==test_gwide_records && group==2 && !wide);
    assert(gwide_entry_home(test_gwide_worker,test_gwide_entries[5].bytes,groups,&group,&wide)==test_gwide_records+GWIDE_RECORD && !group && wide);
    assert(!gwide_entry_home(test_gwide_worker,test_gwide_entries[6].bytes,groups,&group,&wide)); /* sector 0x74, outside every list */
    /* Scan of A: group 0's empty wide list in place, group 1's kept, group 2 (the last with entries) flushes it after its own. */
    SetLastError(0x66);test_gwide_scan(0,2);assert(GetLastError()==0x66);
    assert(test_gwide_calls==3 && test_gwide_log[0]==test_gwide_wide(0) && test_gwide_log[1]==test_gwide_wide(2) && test_gwide_log[2]==test_gwide_wide(1));
    assert(gwide_calls==3 && gwide_kept==1 && gwide_entries==1 && gwide_direct==2 && gwide_flushes==1 && gwide_late==1 && gwide_max==1 && !gwide_pending && !gwide_failures);
    /* A wide list in the last group: kept and flushed at once, after the earlier kept lists. */
    test_gwide_world();test_gwide_list(2,1,4,1);test_gwide_scan(0,2);
    assert(test_gwide_calls==3 && test_gwide_log[0]==test_gwide_wide(0) && test_gwide_log[1]==test_gwide_wide(1) && test_gwide_log[2]==test_gwide_wide(2));
    assert(gwide_kept==2 && gwide_flushes==1 && gwide_late==1 && gwide_max==2 && !gwide_failures);
    /* Scan of B: one group, flushed in its own call. */
    test_gwide_world();test_gwide_scan(4,4);assert(test_gwide_calls==1 && test_gwide_log[0]==test_gwide_wide(4) && gwide_flushes==1 && !gwide_late);
    /* Kept lists of another scan (B after an unfinished A; A again from group 1) are dropped: failure. */
    test_gwide_world();test_gwide_scan(0,1);assert(gwide_pending==1 && test_gwide_calls==1);
    test_gwide_scan(4,4);assert(gwide_failures==1 && !strcmp(gwide_stage,"stale") && !gwide_pending && test_gwide_calls==2 && test_gwide_log[1]==test_gwide_wide(4));
    test_gwide_world();test_gwide_scan(1,1);test_gwide_scan(1,1);assert(gwide_failures==1 && !strcmp(gwide_stage,"stale") && test_gwide_calls==1);
    /* A foreign return address or group: applied in place, failure. */
    test_gwide_world();gwide_run((uintptr_t)(image+GWIDE_SITE+6),test_gwide_worker,test_gwide_wide(1));
    assert(gwide_failures==1 && !strcmp(gwide_stage,"return") && test_gwide_calls==1 && !gwide_pending);
    static unsigned char stray[0x100];memset(stray,0,sizeof(stray));
    gwide_run((uintptr_t)(image+GWIDE_SITE+5),test_gwide_worker,stray+GWIDE_WIDE);assert(gwide_failures==2 && !strcmp(gwide_stage,"group") && test_gwide_calls==2);
    /* Overflow: the kept lists are flushed, then this one in place. */
    test_gwide_world();gwide_pending=GWIDE_PENDING;gwide_record=test_gwide_records;gwide_last=0;
    for(unsigned i=0;i<GWIDE_PENDING;i++) {gwide_lists[i]=test_gwide_wide(0);gwide_indices[i]=0;}
    test_gwide_scan(1,1);
    assert(test_gwide_calls==GWIDE_PENDING+1 && !gwide_pending && gwide_late==GWIDE_PENDING && gwide_failures==1 && !strcmp(gwide_stage,"overflow"));
    /* An exception in an apply: the flush ends (kept lists gone), failure. */
    test_gwide_world();test_gwide_list(2,1,4,1);test_gwide_scan(0,1);test_gwide_throw=1;int caught=0;
    __try {test_gwide_scan(2,2);} __except(GetExceptionCode()==0xe0475744u?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    test_gwide_throw=0;assert(caught && !gwide_pending && gwide_failures==1 && !strcmp(gwide_stage,"apply_exception") && test_gwide_calls==2);
    /* A stop inside a scan: the next call of the same scan flushes the kept lists first; another scan's are dropped. */
    test_gwide_world();test_gwide_scan(0,1);InterlockedExchange(&gwide_active,0);test_gwide_scan(2,2);
    assert(test_gwide_calls==3 && test_gwide_log[1]==test_gwide_wide(1) && test_gwide_log[2]==test_gwide_wide(2) && !gwide_pending);
    InterlockedExchange(&gwide_active,1);test_gwide_world();test_gwide_scan(0,1);InterlockedExchange(&gwide_active,0);test_gwide_scan(4,4);
    assert(test_gwide_calls==2 && test_gwide_log[1]==test_gwide_wide(4) && !gwide_pending);
    /* The call site: prepare, swap, owned, restore; arming refusals. */
    unsigned char *saved_thunk=thunk;thunk=image+0x6000000;
    unsigned char *site=image+GWIDE_SITE,before[48];memcpy(before,site-16,48);memset(site-16,0x90,48);site[0]=0xe8;
    int32_t rel=(int32_t)((intptr_t)(image+GWIDE_APPLY)-(intptr_t)(site+5));memcpy(site+1,&rel,4);
    gwide_armed=gwide_installed=gwide_restored=0;
    assert(gwide_prepare() && gwide_original_rel==rel && gwide_swap(gwide_original_rel,gwide_patched_rel));gwide_installed=1;gwide_armed=1;
    memcpy(&rel,site+1,4);assert(site[0]==0xe8 && site+5+rel==thunk+GWIDE_THUNK && gwide_owned() && !gwide_prepare());
    gwide_patched_rel^=1;assert(!gwide_owned() && !gwide_check() && !strcmp(gwide_stage,"call_changed"));gwide_patched_rel^=1;gwide_failures=0;
    gwide_restore();assert(gwide_restored && !gwide_installed && !gwide_active && gwide_prepare());
    site[0]=0xe9;assert(!gwide_prepare());site[0]=0xe8;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    gwide_armed=gwide_installed=gwide_restored=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!gwide_arm(GWIDE_MODE_WIDE,1) && !strcmp(gwide_stage,"thunk")); /* the slot is not emitted on the placeholder page */
    assert(!gwide_arm(GWIDE_MODE_SORT,0) && !strcmp(gwide_stage,"needs") && !gwide_arm(GWIDE_MODE_OFF,1) && !gwide_arm(3,1));
    int saved_identity=world_identity_installed;world_identity_installed=0;assert(!gwide_arm(GWIDE_MODE_SORT,1) && !strcmp(gwide_stage,"needs"));
    world_identity_installed=1;assert(!gwide_arm(GWIDE_MODE_SORT,1) && !strcmp(gwide_stage,"pins"));world_identity_installed=saved_identity;
    stats.issued=1;assert(!gwide_arm(GWIDE_MODE_WIDE,1));stats.issued=0;boot_phase=2;assert(!gwide_arm(GWIDE_MODE_WIDE,1));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!gwide_arm(GWIDE_MODE_WIDE,1) && !gwide_arm(GWIDE_MODE_SORT,1));
    assert(!gwide_armed && !gwide_installed && !gwide_active && gwide_mode==GWIDE_MODE_OFF);
    memcpy(site-16,before,48);thunk=saved_thunk;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    /* Status bound. */
    gwide_armed=gwide_installed=gwide_restored=INT32_MIN;gwide_calls=gwide_kept=gwide_entries=gwide_flushes=gwide_direct=gwide_max=gwide_late=UINT64_MAX;
    gwide_pending=UINT_MAX;gwide_failures=INT64_MIN;gwide_stage="sort_neighbours";InterlockedExchange(&gwide_active,(LONG)(-2147483647-1));
    gwide_sorts=gwide_settles=gwide_sectors=gwide_moved=gwide_unmapped=gwide_order_hash=UINT64_MAX;gwide_permuted=INT32_MIN;gwide_mode=GWIDE_MODE_WIDE;
    char status[640];int n=gwide_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_WIDE_STOP \n")<640);
    gwide_sorts=gwide_settles=gwide_sectors=gwide_moved=gwide_unmapped=gwide_order_hash=0;gwide_permuted=0;gwide_mode=GWIDE_MODE_OFF;
    gwide_armed=gwide_installed=gwide_restored=0;InterlockedExchange(&gwide_active,0);test_gwide_world();gwide_stage="idle";gwide_apply_override=NULL;
    printf("Gravidar wide-list applies: kept/flushed in order/empty in place/sector entries/stale/foreign/exception/stop/call site/arming/status tests PASS\n");
}
/* 'sort': the gated publish runs its inner publish with the ready lock held and
 * address-ordered groups, then permutes every sector's groups into (class, key)
 * order with remapped neighbour indices (identity orders and single groups
 * untouched); the settle puts groups, indices and the pass's list changes back;
 * busy, changed, unsettled, collision, bad neighbour and exception failures;
 * pass-through when off or on a reset; restore settles. */
static unsigned char test_gsort_zones[6][0x40],test_gsort_records[3*GWIDE_RECORD],test_gsort_groups[7*GWIDE_GROUP];
static uint64_t test_gsort_neighbours[7][2];
static unsigned test_gsort_inner_calls;
static int test_gsort_throw;
static void test_gsort_inner(void *worker,unsigned char reset) {
    (void)reset;CRITICAL_SECTION *ready=(CRITICAL_SECTION*)((unsigned char*)worker+GWIDE_READY_LOCK);++test_gsort_inner_calls;
    if(gwide_permuted) return; /* an unsettled publish: native, without the lock */
    if(gwide_mode==GWIDE_MODE_SORT && InterlockedCompareExchange(&gwide_active,0,0) && !reset)
        assert((DWORD)(uintptr_t)ready->OwningThread==GetCurrentThreadId() && ready->RecursionCount>=1);
    for(unsigned i=1;i<4;i++) assert(*(uintptr_t*)(test_gsort_groups+i*GWIDE_GROUP)>*(uintptr_t*)(test_gsort_groups+(i-1)*GWIDE_GROUP));
    if(test_gsort_throw) RaiseException(0xe0475745u,0,0,NULL);
}
static void test_gsort_neighbour_list(unsigned group,const uint64_t *values,unsigned count) {
    memcpy(test_gsort_neighbours[group],values,count*8);
    uintptr_t list[3]={(uintptr_t)test_gsort_neighbours[group],(uintptr_t)(test_gsort_neighbours[group]+count),(uintptr_t)(test_gsort_neighbours[group]+2)};
    memcpy(test_gsort_groups+group*GWIDE_GROUP+0x38,list,24);
}
/* A: groups 0..3 (zones 0..3 ascending, keys saved 77, saved 60, none 902, saved 7b); B: group 4 alone; C: groups 5..6 already in order. */
static void test_gsort_world(void) {
    memset(test_gsort_groups,0,sizeof(test_gsort_groups));memset(test_gsort_zones,0,sizeof(test_gsort_zones));
    const uint64_t runtimes[6]={0x903,0x901,0x902,0x904,0x905,0x906};
    for(unsigned i=0;i<6;i++) memcpy(test_gsort_zones[i]+8,&runtimes[i],8);
    const unsigned zone_of[7]={0,1,2,3,4,4,5};
    for(unsigned g=0;g<7;g++) {
        void *zone=test_gsort_zones[zone_of[g]];memcpy(test_gsort_groups+g*GWIDE_GROUP,&zone,8);
        uintptr_t marker[2]={0x1000+g,0x1000+g};memcpy(test_gsort_groups+g*GWIDE_GROUP+GWIDE_NEAR,marker,16);
    }
    const uint64_t n0[2]={1,2},n1[1]={0},n2[2]={0,3},n3[1]={2},n5[1]={1},n6[1]={0};
    test_gsort_neighbour_list(0,n0,2);test_gsort_neighbour_list(1,n1,1);test_gsort_neighbour_list(2,n2,2);test_gsort_neighbour_list(3,n3,1);
    test_gsort_neighbour_list(5,n5,1);test_gsort_neighbour_list(6,n6,1);
    const unsigned first[3]={0,4,5},end[3]={4,5,7};
    for(unsigned r=0;r<3;r++) {
        uintptr_t record[3]={0,(uintptr_t)(test_gsort_groups+first[r]*GWIDE_GROUP),(uintptr_t)(test_gsort_groups+end[r]*GWIDE_GROUP)};
        memcpy(test_gsort_records+r*GWIDE_RECORD,record,24);
    }
    uintptr_t records[2]={(uintptr_t)test_gsort_records,(uintptr_t)(test_gsort_records+3*GWIDE_RECORD)};memcpy(test_gwide_worker+GWIDE_SECTORS,records,16);
    test_gwide_worker[GWIDE_READY]=1;
}
static void *test_gsort_zone(unsigned group) {return *(void**)(test_gsort_groups+group*GWIDE_GROUP);}
static uint64_t test_gsort_neighbour(unsigned group,unsigned i) {uint64_t *list=*(uint64_t**)(test_gsort_groups+group*GWIDE_GROUP+0x38);return list[i];}
static void test_gravidar_wide_sort(void) {
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x903,0x77) && world_identity_record(0x901,0x60) && world_identity_record(0x904,0x7b) &&
           world_identity_record(0x905,0x50) && world_identity_record(0x906,0x51));
    memset(test_gwide_worker,0,sizeof(test_gwide_worker));
    InitializeCriticalSection((CRITICAL_SECTION*)(test_gwide_worker+GWIDE_LIST_LOCK));InitializeCriticalSection((CRITICAL_SECTION*)(test_gwide_worker+GWIDE_READY_LOCK));
    void *saved_worker;memcpy(&saved_worker,image+GWIDE_WORKER,8);void *worker=test_gwide_worker;memcpy(image+GWIDE_WORKER,&worker,8);
    test_gsort_world();gwide_inner=test_gsort_inner;gwide_failures=0;gwide_stage="armed";
    gwide_sorts=gwide_settles=gwide_sectors=gwide_moved=gwide_unmapped=gwide_order_hash=0;
    /* Off (or wide) and resets pass through untouched. */
    gwide_mode=GWIDE_MODE_WIDE;InterlockedExchange(&gwide_active,1);gwide_publish(worker,0);
    gwide_mode=GWIDE_MODE_SORT;InterlockedExchange(&gwide_active,0);gwide_publish(worker,0);
    InterlockedExchange(&gwide_active,1);gwide_publish(worker,1);
    assert(test_gsort_inner_calls==3 && !gwide_permuted && !gwide_sorts && test_gsort_zone(0)==test_gsort_zones[0]);
    /* The publish: A becomes 1,0,3,2 (saved 60, 77, 7b, then the unmapped 902) with indices remapped; B and C stay. */
    gwide_publish(worker,0);
    assert(test_gsort_inner_calls==4 && gwide_permuted && gwide_sorts==1 && gwide_sectors==1 && gwide_moved==4 && gwide_unmapped==1 && !gwide_failures);
    assert(test_gsort_zone(0)==test_gsort_zones[1] && test_gsort_zone(1)==test_gsort_zones[0] && test_gsort_zone(2)==test_gsort_zones[3] &&
           test_gsort_zone(3)==test_gsort_zones[2] && test_gsort_zone(5)==test_gsort_zones[4] && test_gsort_zone(6)==test_gsort_zones[5]);
    assert(*(uintptr_t*)(test_gsort_groups+GWIDE_NEAR)==0x1001 && *(uintptr_t*)(test_gsort_groups+3*GWIDE_GROUP+GWIDE_NEAR)==0x1002);
    assert(test_gsort_neighbour(0,0)==1 && test_gsort_neighbour(1,0)==0 && test_gsort_neighbour(1,1)==3 && test_gsort_neighbour(2,0)==3 &&
           test_gsort_neighbour(3,0)==1 && test_gsort_neighbour(3,1)==2 && test_gsort_neighbour(5,0)==1 && test_gsort_neighbour(6,0)==0);
    uint64_t order=gwide_order_hash;assert(order);
    /* A publish before the settle: native, and a failure. */
    gwide_publish(worker,0);assert(test_gsort_inner_calls==5 && gwide_failures==1 && !strcmp(gwide_stage,"sort_unsettled"));gwide_failures=0;
    /* The pass changed a list of portable group 0 (old group 1); a busy worker cannot settle; an idle one can. */
    uintptr_t changed[2]={0x2001,0x2002};memcpy(test_gsort_groups+GWIDE_NEAR,changed,16);
    test_gwide_worker[GWIDE_READY]=0;assert(!gwide_settle() && gwide_failures==1 && !strcmp(gwide_stage,"sort_busy") && gwide_permuted);
    gwide_failures=0;test_gwide_worker[GWIDE_READY]=1;assert(gwide_settle() && !gwide_permuted && gwide_settles==1 && !gwide_failures);
    for(unsigned g=0;g<7;g++) assert(test_gsort_zone(g)==test_gsort_zones[g<4?g:g==4?4:g==5?4:5]);
    assert(*(uintptr_t*)(test_gsort_groups+GWIDE_GROUP+GWIDE_NEAR)==0x2001 && *(uintptr_t*)(test_gsort_groups+GWIDE_NEAR)==0x1000);
    assert(test_gsort_neighbour(0,0)==1 && test_gsort_neighbour(0,1)==2 && test_gsort_neighbour(1,0)==0 && test_gsort_neighbour(2,0)==0 &&
           test_gsort_neighbour(2,1)==3 && test_gsort_neighbour(3,0)==2);
    assert(gwide_settle()); /* nothing to do */
    /* The same world sorts to the same order digest part. */
    gwide_order_hash=0;gwide_publish(worker,0);assert(gwide_order_hash==order);
    /* A vector changed while permuted (Update grew A): the settle fails, the rest is still put back. */
    uintptr_t end=(uintptr_t)(test_gsort_groups+3*GWIDE_GROUP);memcpy(test_gsort_records+16,&end,8);
    assert(!gwide_settle() && gwide_failures==1 && !strcmp(gwide_stage,"sort_changed") && !gwide_permuted);
    test_gsort_world();gwide_failures=0;
    /* Two zones with one key; a neighbour index out of range; an exception in the inner publish. */
    memcpy(test_gsort_zones[2]+8,test_gsort_zones[0]+8,8);gwide_publish(worker,0);
    assert(gwide_failures==1 && !strcmp(gwide_stage,"sort_collision") && gwide_permuted && gwide_settle());
    test_gsort_world();gwide_failures=0;test_gsort_neighbours[0][1]=4;gwide_publish(worker,0);
    assert(gwide_failures==1 && !strcmp(gwide_stage,"sort_neighbours") && gwide_settle());
    test_gsort_world();gwide_failures=0;test_gsort_throw=1;int caught=0;
    __try {gwide_publish(worker,0);} __except(GetExceptionCode()==0xe0475745u?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    test_gsort_throw=0;assert(caught && gwide_failures==1 && !strcmp(gwide_stage,"sort_exception") && !gwide_permuted);
    CRITICAL_SECTION *ready=(CRITICAL_SECTION*)(test_gwide_worker+GWIDE_READY_LOCK);assert(!ready->RecursionCount);
    /* Restore (stop) settles a permuted worker. */
    test_gsort_world();gwide_failures=0;gwide_publish(worker,0);assert(gwide_permuted);
    gwide_armed=1;gwide_installed=0;gwide_restore();assert(!gwide_permuted && gwide_restored && !gwide_active && test_gsort_zone(0)==test_gsort_zones[0]);
    gwide_armed=gwide_restored=0;gwide_mode=GWIDE_MODE_OFF;gwide_inner=NULL;gwide_failures=0;gwide_stage="idle";
    gwide_sorts=gwide_settles=gwide_sectors=gwide_moved=gwide_unmapped=gwide_order_hash=0;
    DeleteCriticalSection((CRITICAL_SECTION*)(test_gwide_worker+GWIDE_LIST_LOCK));DeleteCriticalSection((CRITICAL_SECTION*)(test_gwide_worker+GWIDE_READY_LOCK));
    memset(test_gwide_worker,0,sizeof(test_gwide_worker));memcpy(image+GWIDE_WORKER,&saved_worker,8);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    printf("Gravidar sorted zone groups: pass-through/lock held/portable order/neighbours/settle/busy/changed/unsettled/collision/exception/restore tests PASS\n");
}
/* With the X4 9.00 executable: the scan's two list applies (near rdx=group+0x08 at
 * 0x10b3ce4, wide rdx=group+0x20 at 0x10b3cf1, both to 0x10b5410), the group stride
 * and done flag, 0x10b5410's slot 7 call with the entry as result, the pins, the
 * cache-line bound of the rel32, the address-ordered group lookup 0x10cae90. */
static void test_gravidar_wide_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image,*prior_thunk=thunk;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);thunk=image+0x6f00000;
    assert(gwide_pinned() && gwide_prepare() && ((GWIDE_SITE+1)&63)<=60);
    int32_t rel;memcpy(&rel,image+GWIDE_SITE+1,4);assert(image[GWIDE_SITE]==0xe8 && GWIDE_SITE+5+rel==GWIDE_APPLY);
    /* lea r15,[r12-0x20] (r12 = group+0x28); mov rdx,r15 ... call near; lea rdx,[r12-8]; mov rcx,r13; call wide. */
    assert(!memcmp(image+0x10b3bba,"\x4d\x8d\x7c\x24\xe0",5));
    assert(!memcmp(image+0x10b3cd7,"\x49\x8b\xd7\x4c\x8b\xad\x90\x00\x00\x00\x49\x8b\xcd\xe8",14));
    memcpy(&rel,image+0x10b3ce5,4);assert(0x10b3ce9+rel==GWIDE_APPLY);
    assert(!memcmp(image+0x10b3ce9,"\x49\x8d\x54\x24\xf8\x49\x8b\xcd",8));
    /* The group's done flag (+0x50) after its applies; 0x10b5410 calls slot 7 with r8 = the entry. */
    assert(!memcmp(image+0x10b3d14,"\x41\xc6\x44\x24\x28\x01",6) && !memcmp(image+0x10b545c,"\x4c\x8b\xc7\x48\x8b\xce\xff\x50\x38",9));
    assert(hash_ok(image+0x10cae90,0x5f,"45fbf6485a43a66d4a6db2c749662b3da0ef279a7d63d43e86f58459e4f55d0f"));
    /* 'sort': Run, PublishResults and the lookup pinned; the pass's barrier enters the ready lock (rbx) after its wake-up wait. */
    assert(gwide_sort_pinned() && !memcmp(image+0x10ae249,"\x48\x8b\xcb\xff\x15",5) && !memcmp(image+0x10ae230,"\x49\x8b\x8c\x24\xa8\x69\x00\x00",8));
    image[GWIDE_PUBLISH+7]^=1;assert(!gwide_sort_pinned());image[GWIDE_PUBLISH+7]^=1;
    const unsigned tampers[]={GWIDE_SCAN,GWIDE_SITE-1,GWIDE_SITE+5,GWIDE_SCAN_END-1,GWIDE_APPLY,GWIDE_APPLY+GWIDE_APPLY_SIZE-1};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!gwide_pinned());image[tampers[i]]^=1;}
    image[GWIDE_SITE+1]^=1;assert(gwide_pinned() && !gwide_prepare());image[GWIDE_SITE+1]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;thunk=prior_thunk;
    puts("Gravidar wide-list apply site, list arguments, group done flag, slot 7 result, pins and group lookup verified");
}
