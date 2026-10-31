/* native_gate_tempzone_order: the FUN_140785db0 wrapper (request sites,
 * thread classes, misses noted, found zones and creates counted, other code's
 * creates, last error), the parked slot around a joined pass (RunPass's clear
 * on the published view, park, unpark, a slot changed meanwhile, an unjoined
 * park), the grant (smallest request by sector key and position whatever the
 * job order, the set digest, TLS game time 0 and a keyed word during the call
 * and both restored, the slot store with its reference), a pending zone and an
 * overflowing or vanished request deferring the grant, a grant finding an
 * active zone, 'observe' rows, arming refusals, ownership, restore, finish,
 * status and row bounds, the thunk ABI, and the native pins and structure. */
typedef struct {unsigned char bytes[0x300];} test_tz_zone_t;
static test_tz_zone_t test_tz_made,test_tz_existing;
static int64_t test_tz_result_create,test_tz_result_find;
static unsigned test_tz_calls,test_tz_releases,test_tz_draws;
static void *test_tz_sector_seen;static uint32_t test_tz_position_seen[4];static unsigned char test_tz_create_seen;
static unsigned char *test_tz_tls;static double test_tz_time_seen;static uint64_t test_tz_word_seen;
static uint64_t test_tz_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static int64_t *test_tz_original(void *sector,int64_t *out,const void *position,unsigned char create) {
    ++test_tz_calls;test_tz_sector_seen=sector;memcpy(test_tz_position_seen,position,16);test_tz_create_seen=create;
    if(test_tz_tls && create) { /* only the grant runs in the MoveWorker context */
        memcpy(&test_tz_time_seen,test_tz_tls+0x300,8);memcpy(&test_tz_word_seen,test_tz_tls+0x318,8);uint64_t w=test_tz_word_seen;
        for(unsigned i=0;i<test_tz_draws;i++) w=test_tz_step(w);
        memcpy(test_tz_tls+0x318,&w,8);
    }
    *out=create?test_tz_result_create:test_tz_result_find;
    if(*out) InterlockedIncrement((volatile LONG*)((unsigned char*)*out+0x40));
    SetLastError(0x4d2);return out;
}
static void test_tz_release(int64_t *ref) {++test_tz_releases;if(*ref) InterlockedDecrement((volatile LONG*)((unsigned char*)*ref+0x40));}
static LONG test_tz_count(const test_tz_zone_t *z) {return *(const LONG*)(z->bytes+0x40);}
static void test_tz_zone(test_tz_zone_t *z,uint64_t runtime,int active) {
    memset(z,0,sizeof(*z));memcpy(z->bytes+8,&runtime,8);*(LONG*)(z->bytes+0x44)=1;
    for(unsigned i=0;i<4;i++) *(uint32_t*)(z->bytes+0x11c+i*0x80)=active?0x10000:0;
}
static void test_tz_reset(void) {
    tz_mode=TZ_OFF;tz_installed=tz_restored=tz_blocked=tz_open=0;tz_active=0;tz_original_word=tz_patched_word=tz_saved_zone=0;tz_saved_flag=0;
    tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=0;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=0;
    memset(tz_candidates,0,sizeof(tz_candidates));tz_last_ret=0;tz_original_override=NULL;tz_release_override=NULL;tz_stage="idle";
}
static volatile LONG *test_tz_flag(void) {return (volatile LONG*)(image+TZ_FLAG);}
static volatile int64_t *test_tz_slot(void) {return (volatile int64_t*)(image+TZ_ZONE);}
/* One request call of a job (the thread classes come from ui_thread/main_thread/path_pass_open). */
static void test_tz_request(uint32_t site,void *sector,const float position[4],unsigned char create) {
    int64_t out=0;assert(tz_find_run(test_tz_original,(uintptr_t)image+site,sector,&out,position,create)==&out);
}
static void test_tz_pass(unsigned char *sectors[3],const float positions[3][4],const unsigned order[3]) {
    assert(tz_before_release());
    for(unsigned i=0;i<3;i++) test_tz_request(order[i]&1?TZ_SWITCH_FIND:TZ_PATH_FIND,sectors[order[i]],positions[order[i]],0);
}
/* Through an emitted 3024 slot: a native-like FUN_140785db0 entered by a stub whose call returns to code+521. */
static int64_t *test_tz_abi_target(void *sector,int64_t *out,const void *position,unsigned char create) {
    (void)sector;(void)position;*out=create?0x1234:0x5678;return out;
}
static void test_tz_abi(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);
    const unsigned char prologue[5]={0x48,0x89,0x5c,0x24,0x08};memcpy(code,prologue,5);
    code[5]=0x48;code[6]=0xb8;void *target=(void*)test_tz_abi_target;memcpy(code+7,&target,8);code[15]=0xff;code[16]=0xe0;
    thunk_recipe recipe=*thunk_recipe_at(TZ_THUNK);assert(recipe.entry==TZ_FIND && !memcmp(recipe.prefix,prologue,5));
    assert(thunk_emit_slot_at(code+256,&recipe,code));
    void *dispatch;memcpy(&dispatch,code+256+32+6,8);assert(dispatch==(void*)tz_find);
    code[0]=0xe9;int32_t rel=(int32_t)((code+256+32)-(code+5));memcpy(code+1,&rel,4);
    const unsigned char stub[]={0x48,0x83,0xec,0x28,0xe8,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
    memcpy(code+512,stub,sizeof(stub));rel=(int32_t)(code-(code+521));memcpy(code+512+5,&rel,4);
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    tz_original_override=(tz_find_fn)(code+256);tz_active=1;int64_t out=0;float position[4]={1,2,3,0};
    typedef int64_t *(*stub_fn)(void *,int64_t *,const void *,unsigned char);
    assert(((stub_fn)(code+512))(NULL,&out,position,1)==&out && out==0x1234 && tz_last_ret==(uintptr_t)(code+521));
    assert(((stub_fn)(code+512))(NULL,&out,position,0)==&out && out==0x5678);
    tz_original_override=NULL;tz_active=0;VirtualFree(code,0,MEM_RELEASE);
}
static void test_tempzone_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread,saved_main=main_thread;uint32_t saved_active=stats.active;
    int saved_boot=boot_mode,saved_phase=boot_phase,saved_installed=world_identity_installed;LONG saved_open=path_pass_open;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    /* Sector keys run opposite to their runtime IDs: the order is the portable one. */
    assert(world_identity_record(0x5c0001,0x30) && world_identity_record(0x5c0002,0x20) && world_identity_record(0x5c0003,0x10));
    InitializeCriticalSection((CRITICAL_SECTION*)(image+TZ_LOCK));*test_tz_flag()=0;*test_tz_slot()=0;*(uint32_t*)(image+TZ_VIEW)=1;
    test_tz_reset();tz_release_override=test_tz_release;tz_original_override=test_tz_original;
    static unsigned char sector_a[0x40],sector_b[0x40],sector_c[0x40],stray[0x40],tls[0x320];
    *(uint64_t*)(sector_a+8)=0x5c0001;*(uint64_t*)(sector_b+8)=0x5c0002;*(uint64_t*)(sector_c+8)=0x5c0003;*(uint64_t*)(stray+8)=0x5c0999;
    unsigned char *sectors[3]={sector_a,sector_b,sector_c};
    const float positions[3][4]={{100,0,60000,0},{-5,7,1,0},{3,-90000,2,0}};
    float nearby[4]={1,1,1,0};
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1989;
    /* Inactive: the original only. */
    ui_thread=GetCurrentThreadId()+1;main_thread=GetCurrentThreadId()+2;path_pass_open=1;test_tz_result_find=0;
    test_tz_request(TZ_PATH_FIND,sector_a,nearby,0);assert(test_tz_calls==1 && !tz_finds && !tz_noted && !tz_last_ret);
    /* Order: a job's create=0 miss is noted, a found zone is not; create=1 in a parked pass is counted. */
    tz_mode=TZ_ORDER;tz_installed=1;tz_active=1;
    SetLastError(77);test_tz_request(TZ_PATH_FIND,sector_a,nearby,0);assert(GetLastError()==0x4d2); /* the original's error, kept */
    assert(tz_finds==1 && tz_misses==1 && tz_noted==1 && tz_candidates[0].sector==sector_a && tz_candidates[0].runtime==0x5c0001 &&
           tz_candidates[0].site==TZ_PATH_FIND && !memcmp(tz_candidates[0].position,nearby,16) && tz_last_ret==(uintptr_t)image+TZ_PATH_FIND);
    test_tz_zone(&test_tz_existing,0x77001,1);test_tz_result_find=(int64_t)&test_tz_existing;
    test_tz_request(TZ_SWITCH_FIND,sector_b,nearby,0);assert(tz_finds==2 && tz_misses==1 && tz_noted==1);
    test_tz_result_find=0;test_tz_request(TZ_PATH_CREATE,sector_b,nearby,1);assert(tz_creates==1 && tz_unblocked==1 && tz_noted==1);
    test_tz_request(0xee7c7f,sector_b,nearby,1);test_tz_request(0xee7c7f,sector_b,nearby,0);assert(tz_other_creates==1 && tz_finds==2);
    /* The region cell (0xee7290) on its own thread: a found (active) zone is counted, a made one also logged; in a pass apart. */
    LONG saved_pass=path_pass_open;path_pass_open=0;test_tz_zone(&test_tz_made,0x99001,0);test_tz_result_create=(int64_t)&test_tz_existing;
    LONG64 other_rows=tz_rows;test_tz_request(0xee7c7f,sector_c,nearby,1);
    assert(tz_other_creates==2 && !tz_other_made && !tz_other_in_pass && tz_rows==other_rows);
    test_tz_result_create=(int64_t)&test_tz_made;test_tz_request(0xee7c7f,sector_c,nearby,1);
    assert(tz_other_creates==3 && tz_other_made==1 && !tz_other_in_pass && tz_rows==other_rows+1);
    {char other[384];assert(tz_row((uint64_t)other_rows,other,sizeof(other))>0 &&
        !strcmp(strstr(other,"kind="),"kind=other site=ee7c7f thread=other sector=s:0000000000000010 pos=3f800000,3f800000,3f800000 "
        "zone=0000000000099001 active=0 n=0 set=0000000000000000 draws=0"));}
    path_pass_open=saved_pass;test_tz_request(0xee7c7f,sector_c,nearby,1);assert(tz_other_made==2 && tz_other_in_pass==1 && tz_rows==other_rows+2);
    test_tz_result_create=0;tz_rows=other_rows;tz_other_creates=1;tz_other_made=tz_other_in_pass=0;
    path_pass_open=0;test_tz_request(TZ_PATH_FIND,sector_b,nearby,0);assert(tz_outside==1 && tz_noted==1); /* a job outside a joined pass */
    path_pass_open=1;ui_thread=GetCurrentThreadId();test_tz_request(TZ_PATH_FIND,sector_b,nearby,0);assert(tz_outside==2); /* the MoveWorker */
    test_tz_request(0xee7c7f,sector_b,nearby,1);assert(tz_other_creates==1);
    ui_thread=GetCurrentThreadId()+1;
    /* A pass: free slot -> parked; three misses in some job order -> the grant of the smallest (sector key 0x10 = sector_c). */
    test_tz_zone(&test_tz_made,0x88001,0);test_tz_result_create=(int64_t)&test_tz_made;test_tz_tls=tls;test_tz_draws=3;
    double now=1989.5;memcpy(tls+0x300,&now,8);uint64_t word=0xabcdef;memcpy(tls+0x318,&word,8);
    const unsigned order1[3]={0,1,2},order2[3]={2,0,1};
    test_tz_pass(sectors,positions,order1);
    assert(tz_blocked && tz_open && *test_tz_flag()==1 && !*test_tz_slot() && tz_noted==3 && tz_passes==1 && tz_blocks==1 && tz_opens==1);
    unsigned calls=test_tz_calls;
    assert(tz_after_join_with(tls) && !tz_blocked && test_tz_calls==calls+1 && test_tz_create_seen==1 && test_tz_sector_seen==sector_c &&
           !memcmp(test_tz_position_seen,positions[2],16));
    uint64_t seed=lockstep_seed(TZ_DOMAIN,1989,0x10,((uint64_t)0x40400000u<<32)|0xc7afc800u,0x40000000u,WORLD_KEY_SAVED);
    assert(test_tz_time_seen==0.0 && test_tz_word_seen==seed);
    {double t;uint64_t w;memcpy(&t,tls+0x300,8);memcpy(&w,tls+0x318,8);assert(t==1989.5 && w==0xabcdef);}
    assert(*test_tz_slot()==(int64_t)&test_tz_made && *test_tz_flag()==1 && test_tz_count(&test_tz_made)==1 && *(LONG*)(test_tz_made.bytes+0x44)==2);
    assert(tz_granted==1 && tz_stored==1 && tz_drew==1 && tz_rows==1 && tz_requests==3 && tz_max==3);
    char row[384];assert(tz_row(0,row,sizeof(row))>0);
    uint64_t set=0;
    for(unsigned i=0;i<3;i++) {
        uint32_t bits[3];memcpy(bits,positions[i],12);uint64_t key=0x30-0x10*i;
        set+=lockstep_mix(lockstep_seed(TZ_DOMAIN,key,((uint64_t)bits[0]<<32)|bits[1],bits[2],i&1?TZ_SWITCH_FIND:TZ_PATH_FIND,0));
    }
    {char expect[384];snprintf(expect,sizeof(expect),"index=0 tick=1989 kind=grant site=e91241 thread=moveworker sector=s:0000000000000010 "
        "pos=40400000,c7afc800,40000000 zone=0000000000088001 active=0 n=3 set=%016llx draws=3",(unsigned long long)set);assert(!strcmp(row,expect));}
    /* Pending (not active): the next pass is parked and not open; its requests wait; the slot comes back. */
    stats.issued=1990;test_tz_pass(sectors,positions,order2);
    assert(tz_blocked && !tz_open && *test_tz_flag()==1 && !*test_tz_slot() && tz_saved_zone==(uint64_t)&test_tz_made);
    calls=test_tz_calls;assert(tz_after_join_with(tls) && test_tz_calls==calls && *test_tz_slot()==(int64_t)&test_tz_made && *test_tz_flag()==1);
    assert(tz_waits==1 && tz_row(1,row,sizeof(row))>0 && strstr(row," tick=1990 kind=wait site=e91241 thread=moveworker sector=s:0000000000000010 ") &&
           strstr(row," zone=0000000000000000 active=0 n=3 set=") && strstr(row,"draws=0"));
    {char expect[32];snprintf(expect,sizeof(expect)," set=%016llx ",(unsigned long long)set);assert(strstr(row,expect));} /* the job order is not in it */
    /* Active in the published view: the next pass clears it as RunPass would (reference released), then it is open. */
    stats.issued=1991;for(unsigned i=0;i<4;i++) *(uint32_t*)(test_tz_made.bytes+0x11c+i*0x80)=i==1?0x10000:0;
    unsigned releases=test_tz_releases;assert(tz_before_release() && tz_cleared==1 && tz_open && test_tz_releases==releases+1 && !test_tz_count(&test_tz_made));
    assert(*test_tz_flag()==1 && !*test_tz_slot() && !tz_saved_flag && !tz_saved_zone);
    /* Another view index: the zone is not active there. */
    assert(tz_after_join_with(tls) && !*test_tz_flag() && !*test_tz_slot());
    *(uint32_t*)(image+TZ_VIEW)=2;assert(!tz_active_view(test_tz_made.bytes));*(uint32_t*)(image+TZ_VIEW)=1;assert(tz_active_view(test_tz_made.bytes));
    *(uint32_t*)(test_tz_made.bytes+0x1e4)=0x20000;assert(!tz_active_view(test_tz_made.bytes));*(uint32_t*)(test_tz_made.bytes+0x1e4)=0;
    *(uint32_t*)(test_tz_made.bytes+0x11c+0x80)|=0x40000;assert(!tz_active_view(test_tz_made.bytes));
    *(uint32_t*)(image+TZ_VIEW)=7;assert(!tz_active_view(test_tz_existing.bytes));*(uint32_t*)(image+TZ_VIEW)=1;
    /* A different job order gives the same grant and set. */
    test_tz_zone(&test_tz_made,0x88002,0);stats.issued=1992;test_tz_pass(sectors,positions,order2);LONG64 rows=tz_rows;
    assert(tz_after_join_with(tls) && test_tz_sector_seen==sector_c && tz_rows==rows+1 && tz_row((uint64_t)rows,row,sizeof(row))>0);
    {char expect[64];snprintf(expect,sizeof(expect)," n=3 set=%016llx draws=3",(unsigned long long)set);assert(strstr(row,expect) && strstr(row," kind=grant "));}
    /* The grant finds an active zone: counted, released, nothing stored. */
    *test_tz_slot()=0;*test_tz_flag()=0;test_tz_zone(&test_tz_existing,0x77002,1);test_tz_result_create=(int64_t)&test_tz_existing;
    stats.issued=1993;test_tz_pass(sectors,positions,order1);assert(tz_after_join_with(tls) && tz_found==1 && !*test_tz_slot() && !*test_tz_flag());
    assert(!test_tz_count(&test_tz_existing) && tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," zone=0000000000077002 active=1 "));
    /* No request: no row; a vanished sector (its ID changed) and an overflowing pass wait. */
    test_tz_result_create=(int64_t)&test_tz_made;rows=tz_rows;assert(tz_before_release() && tz_after_join_with(tls) && tz_rows==rows);
    assert(tz_before_release());test_tz_request(TZ_PATH_FIND,sector_a,nearby,0);*(uint64_t*)(sector_a+8)=0x5c0042;
    calls=test_tz_calls;assert(tz_after_join_with(tls) && test_tz_calls==calls && tz_gone==1);*(uint64_t*)(sector_a+8)=0x5c0001;
    assert(tz_before_release());for(unsigned i=0;i<TZ_CANDIDATES+1;i++) test_tz_request(TZ_PATH_FIND,sector_b,nearby,0);
    calls=test_tz_calls;assert(tz_noted==TZ_CANDIDATES+1 && tz_after_join_with(tls) && test_tz_calls==calls && tz_overflow==1 && !*test_tz_flag());
    assert(tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," kind=wait ") && strstr(row," n=257 "));
    /* An unmapped sector keys by its runtime ID (counted). */
    assert(tz_before_release());test_tz_request(TZ_PATH_FIND,stray,nearby,0);unsigned long long unmapped=tz_unmapped;
    assert(tz_after_join_with(NULL) && tz_unmapped==unmapped+1 && test_tz_sector_seen==stray && *test_tz_slot()==(int64_t)&test_tz_made);
    assert(tz_row((uint64_t)(tz_rows-1),row,sizeof(row))>0 && strstr(row," sector=-:00000000005c0999 ") && strstr(row," draws=0"));
    *test_tz_slot()=0;*test_tz_flag()=0;test_tz_zone(&test_tz_made,0x88003,0);
    /* A slot changed during the parked pass: put back nothing, fail; an unjoined park refuses the next release. */
    assert(tz_before_release());*test_tz_flag()=0;assert(!tz_after_join_with(tls) && !strcmp(tz_stage,"slot_changed") && !tz_blocked && !*test_tz_flag());
    assert(tz_before_release() && !tz_before_release() && !strcmp(tz_stage,"unjoined"));
    tz_restore();assert(!tz_blocked && !*test_tz_flag() && !*test_tz_slot() && !tz_active);
    /* Observe: nothing parked; a job's create=1 request logs a row keyed when printed. */
    tz_mode=TZ_OBSERVE;tz_active=1;tz_installed=1;*test_tz_flag()=0;assert(tz_before_release() && !tz_blocked && !*test_tz_flag());
    test_tz_result_create=(int64_t)&test_tz_made;rows=tz_rows;test_tz_request(TZ_SWITCH_CREATE,sector_b,positions[1],1);
    test_tz_request(TZ_PATH_FIND,sector_b,nearby,0);assert(tz_rows==rows+1 && !tz_noted);
    assert(tz_row((uint64_t)rows,row,sizeof(row))>0 && !strcmp(strstr(row,"kind="),"kind=create site=e890a6 thread=pass sector=s:0000000000000020 "
        "pos=c0a00000,40e00000,3f800000 zone=0000000000088003 active=0 n=0 set=0000000000000000 draws=0"));
    assert(tz_after_join_with(tls));
    /* Status and rows at the widest values; eviction. */
    char status[896];int n=tz_status(status,sizeof(status));assert(n>0 && strstr(status,"mode=observe installed=1 "));
    assert(strstr(status," other_creates=1 other_made=0 other_in_pass=0 outside=2 "));
    tz_mode=TZ_ORDER;tz_noted=tz_rows=tz_failures=tz_finds=tz_misses=tz_creates=tz_unblocked=tz_other_creates=tz_other_made=tz_other_in_pass=tz_outside=INT64_MAX;
    tz_passes=tz_blocks=tz_opens=tz_cleared=tz_granted=tz_stored=tz_found=tz_waits=tz_overflow=tz_requests=tz_max=tz_drew=tz_unmapped=tz_gone=UINT64_MAX;
    tz_restored=1;tz_stage="grant_exception";n=tz_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TEMPZONE_ORDER_STOP \n")<896);
    tz_rows=1;tz_ring[0]=(tz_row_t){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,{UINT32_MAX,UINT32_MAX,UINT32_MAX},UINT32_MAX,UINT32_MAX,UINT32_MAX,
        TZ_ROW_CREATE,PATH_THREAD_MOVEWORKER,WORLD_KEY_CREATED,255};
    n=tz_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TEMPZONE_ROW \n")<384);
    tz_rows=TZ_RING+2;assert(tz_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1") && !tz_row(TZ_RING+2,row,sizeof(row)));
    /* Arming: a mode, before the first admitted frame, on the MoveWorker, the pathing join, the identities; pins (zero image refuses). */
    test_tz_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    assert(!tz_arm(TZ_OFF,1) && !tz_arm(9,1) && !tz_arm(TZ_ORDER,2));stats.issued=1;assert(!tz_arm(TZ_ORDER,1));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!tz_arm(TZ_ORDER,1));ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!tz_arm(TZ_ORDER,1));boot_phase=0;
    world_identity_installed=0;assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"needs"));world_identity_installed=1;
    assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"pins") && !tz_installed && !tz_active);
    /* Ownership and restore on a hand-set entry word; finish restores it and puts a parked slot back. */
    uint64_t saved_word;memcpy(&saved_word,image+TZ_FIND,8);
    tz_original_word=0x1111;tz_patched_word=0x2222;*(uint64_t*)(image+TZ_FIND)=0x2222;tz_installed=1;tz_mode=TZ_ORDER;tz_active=1;
    assert(tz_owned() && tz_arm(TZ_ORDER,1) && !tz_arm(TZ_OBSERVE,1));
    *(uint64_t*)(image+TZ_FIND)=0x3333;assert(!tz_owned() && !tz_check() && !strcmp(tz_stage,"entry_changed"));
    tz_failures=0;tz_restore();assert(tz_installed && !tz_restored && !tz_active && *(uint64_t*)(image+TZ_FIND)==0x3333);
    *(uint64_t*)(image+TZ_FIND)=0x2222;tz_active=1;*test_tz_flag()=0;*test_tz_slot()=0;assert(tz_before_release() && tz_blocked);
    uint32_t saved_reason=stats.reason;stats.restored=1;finish(TZ_STOP_REASON);
    assert(stats.reason==TZ_STOP_REASON && !tz_installed && tz_restored && *(uint64_t*)(image+TZ_FIND)==0x1111 && !tz_blocked && !*test_tz_flag());
    memcpy(image+TZ_FIND,&saved_word,8);
    test_tz_abi();
    test_tz_reset();test_tz_tls=NULL;memset(tz_ring,0,sizeof(tz_ring));
    DeleteCriticalSection((CRITICAL_SECTION*)(image+TZ_LOCK));memset(image+TZ_LOCK,0,sizeof(CRITICAL_SECTION));
    *test_tz_flag()=0;*test_tz_slot()=0;*(uint32_t*)(image+TZ_VIEW)=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_installed;path_pass_open=saved_open;
    stats.issued=saved_issued;ui_thread=saved_ui;main_thread=saved_main;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("tempzone order wrapper/park/grant/order-independence/pending/clear/overflow/observe/arming/ownership/finish/status/ABI tests PASS\n");
}
/* With the X4 9.00 executable: the request blocks and their four calls, the slot
 * globals, RunPass's clear, the published view index, the creator's post time,
 * tamper refusals and a full arm/restore through a sealed page on the copy. */
static void test_tempzone_order_native_image(const char *path) {
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
    assert(tz_pins(1) && tz_pins(0) && !memcmp(image+TZ_FIND,"\x48\x89\x5c\x24\x08",5));
    /* The four request calls: r9b 1 (create) then the call, returning to the recorded sites. */
    const unsigned calls[4]={TZ_PATH_CREATE-5,TZ_PATH_FIND-5,TZ_SWITCH_CREATE-5,TZ_SWITCH_FIND-5},flags[4]={0xe9108d,0xe91229,0xe89091,0xe89121};
    for(unsigned i=0;i<4;i++) {
        assert(image[calls[i]]==0xe8 && calls[i]+5+*(int32_t*)(image+calls[i]+1)==TZ_FIND);
        assert(!memcmp(image+flags[i],i&1?"\x45\x33\xc9":"\x41\xb1\x01",3));
    }
    /* Slot globals: the flag test, the lock, the store and the flag set in Agent_UpdatePathing; RunPass's clear. */
    const unsigned refs[][2]={{0xe91054,TZ_FLAG},{0xe91063,TZ_LOCK},{0xe911c6,TZ_ZONE},{0xe911d7,TZ_FLAG},{0xe8905f,TZ_FLAG},{0xe890ec,TZ_ZONE},
        {0xeae860,TZ_FLAG},{0xeae878,TZ_ZONE},{0x69ed7c,TZ_VIEW},{0x785bc1,TZ_VIEW}};
    for(unsigned i=0;i<sizeof(refs)/sizeof(*refs);i++) {
        unsigned at=refs[i][0],length=image[at]==0x48?7:6;assert(at+length+*(int32_t*)(image+at+length-4)==refs[i][1]);
    }
    assert(image[0xeae884]==0xe8 && 0xeae889+*(int32_t*)(image+0xeae885)==TZ_ACTIVE);
    /* The lookup and the creator: create=1 calls 0x7877d0, which posts deferred with the TLS game time (TLS+0x300). */
    assert(image[0x785e2f]==0xe8 && 0x785e34+*(int32_t*)(image+0x785e30)==TZ_CREATE && image[0x785dde]==0xe8 && 0x785de3+*(int32_t*)(image+0x785ddf)==TZ_LOOKUP);
    assert(!memcmp(image+0x78783e,"\xb8\x00\x03\x00\x00",5) && !memcmp(image+0x787843,"\xf2\x0f\x10\x34\x30",5) &&
           image[0x787881]==0xe8 && 0x787886+*(int32_t*)(image+0x787882)==0x99a720);
    /* The one other caller: the region cell 0xee7290 (create=1 at 0xee7c7a), from 0xeea8a0, from RegionManager::Run. */
    assert(!memcmp(image+0xee7c66,"\x41\xb1\x01",3) && image[0xee7c7a]==0xe8 && 0xee7c7f+*(int32_t*)(image+0xee7c7b)==TZ_FIND);
    assert(image[0xeeb350]==0xe8 && 0xeeb355+*(int32_t*)(image+0xeeb351)==0xee7290 && image[0xef325e]==0xe8 && 0xef3263+*(int32_t*)(image+0xef325f)==0xeea8a0);
    image[TZ_FIND+0x40]^=1;assert(!tz_pins(0));image[TZ_FIND+0x40]^=1;
    image[TZ_PATH_BLOCK+0x80]^=1;assert(!tz_pins(0));image[TZ_PATH_BLOCK+0x80]^=1;
    image[TZ_CLEAR_BLOCK+0x10]^=1;assert(!tz_pins(0));image[TZ_CLEAR_BLOCK+0x10]^=1;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=world_identity_installed;unsigned char *prior_thunk=thunk;
    test_tz_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    thunk=NULL;assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"thunk"));
    thunk=make_thunk();assert(thunk && thunk_slot_ready(TZ_THUNK));unsigned char native[8];memcpy(native,image+TZ_FIND,8);
    assert(!tz_arm(TZ_ORDER,0) && tz_arm(TZ_ORDER,1) && tz_installed && tz_owned() && tz_check() && tz_arm(TZ_ORDER,1) && !tz_arm(TZ_OBSERVE,1));
    {int32_t jump;memcpy(&jump,image+TZ_FIND+1,4);assert(image[TZ_FIND]==0xe9 && image+TZ_FIND+5+jump==thunk+TZ_THUNK+32 && !memcmp(image+TZ_FIND+5,native+5,3));
     void *continuation,*dispatch;memcpy(&continuation,thunk+TZ_THUNK+5+6,8);memcpy(&dispatch,thunk+TZ_THUNK+32+6,8);
     assert(!memcmp(thunk+TZ_THUNK,native,5) && continuation==image+TZ_FIND+5 && dispatch==(void*)tz_find);}
    image[TZ_SWITCH_BLOCK+0x20]^=1;assert(!tz_check() && !strcmp(tz_stage,"block_changed"));image[TZ_SWITCH_BLOCK+0x20]^=1;tz_failures=0;
    tz_restore();assert(tz_restored && !tz_installed && !memcmp(image+TZ_FIND,native,8));
    /* A foreign entry word refuses arming and stays. */
    image[TZ_FIND]=0xcc;test_tz_reset();assert(!tz_arm(TZ_ORDER,1) && !strcmp(tz_stage,"pins") && image[TZ_FIND]==0xcc);image[TZ_FIND]=native[0];
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior_thunk;
    test_tz_reset();world_identity_installed=saved_installed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("tempzone order native request calls, slot globals, RunPass clear, published view index, creator post time, pins, "
         "tamper refusals and sealed-page arm/restore verified");
}
