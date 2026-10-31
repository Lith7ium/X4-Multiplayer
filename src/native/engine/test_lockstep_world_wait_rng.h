/* Owned-memory tests of the production wrapper; only the TLS-slot accessor is
 * injected, because the test executable does not own X4's static TLS slot0. */
static DWORD test_wwr_tls=TLS_OUT_OF_INDEXES;
static unsigned test_wwr_calls,test_wwr_mode;
static uint64_t test_wwr_seen;
static void *test_wwr_current(void) {
    if(test_wwr_mode==9) {test_wwr_mode=0;world_wait_restore();} /* STOP after admission, before seed */
    return TlsGetValue(test_wwr_tls);
}
static uint64_t test_wwr_step(uint64_t value) {
    value=value*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (value>>30)|(value<<34);
}
static unsigned char test_wwr_original(void *self,void *exec,void *value) {
    assert(self==test_ww_self&&exec==test_ww_exec&&value==test_ww_value);++test_wwr_calls;
    uint64_t *rng=(uint64_t*)((unsigned char*)test_ww_tls+0x318);test_wwr_seen=*rng;*rng=test_wwr_step(*rng);
    uintptr_t context=*(uintptr_t*)((unsigned char*)value+8)-0xa8;
    *(double*)(context+0x190)=7.1;*(unsigned*)(context+0x18c)=2;
    if(test_wwr_mode==1) ++*(uint64_t*)(context+8);
    if(test_wwr_mode==2) world_wait_restore();
    if(test_wwr_mode==3) {assert(SetEvent(test_ww_entered));assert(WaitForSingleObject(test_ww_release,2000)==WAIT_OBJECT_0);}
    if(test_wwr_mode==5||test_wwr_mode==6) {
        unsigned mode=test_wwr_mode;test_wwr_mode=0;
        uintptr_t caller=mode==5?(uintptr_t)(image+WORLD_WAIT_CALLER):0;
        assert(world_wait_observe_with_tls(test_wwr_original,self,exec,value,caller,test_ww_tls,test_wwr_current)==1);
        test_wwr_mode=mode;
    }
    if(test_wwr_mode==7) assert(TlsSetValue(test_wwr_tls,NULL));
    if(test_wwr_mode==8) ++*(unsigned*)(image+0x6adf9fc);
    return test_wwr_mode==4?0xa5:1;
}
static void test_wwr_reset(void) {
    assert(!world_wait_open&&!world_wait_rng_open);
    memset(world_wait_events,0,sizeof(world_wait_events));world_wait_count=world_wait_calls=world_wait_import_calls=world_wait_foreign_calls=0;
    world_wait_unknown=world_wait_failures=world_wait_dropped=world_wait_sequence=world_wait_restore_open=world_wait_peak=0;
    for(unsigned i=0;i<WORLD_WAIT_SELECTED;i++) world_wait_rng_selected[i].used=0;world_wait_rng_applied=world_wait_rng_refused=world_wait_rng_observed=0;world_wait_rng_restore_open=0;
    world_wait_restoring=0;world_wait_enabled=1;world_wait_installed=0;stats.active=1;stats.issued=0;
    boot_mode=1;boot_phase=2;boot_started=GetTickCount64();ui_thread=GetCurrentThreadId();main_thread=0;
    *(unsigned*)(image+0x6adf9fc)=0x26;*(unsigned*)(*(uintptr_t*)(image+TABLE_RVA)+0x130)=1;
    *(double*)(image+GAME_RVA)=*(double*)(image+0x3d19790)=*(double*)((unsigned char*)test_ww_tls+0x300)=7.083;
    *(unsigned*)((unsigned char*)test_ww_tls+0x314)=1;*(uint64_t*)((unsigned char*)test_ww_tls+0x318)=123;
    assert(TlsSetValue(test_wwr_tls,test_ww_tls)&&TlsSetValue(world_wait_rng_tls,NULL));test_wwr_calls=test_wwr_mode=0;
}
static unsigned char test_wwr_call(void) {
    return world_wait_observe_with_tls(test_wwr_original,test_ww_self,test_ww_exec,test_ww_value,
        (uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls,test_wwr_current);
}
static DWORD WINAPI test_wwr_worker(void *unused) {
    (void)unused;ui_thread=GetCurrentThreadId();assert(TlsSetValue(test_wwr_tls,test_ww_tls));assert(test_wwr_call()==1);return 0;
}
#include "test_lockstep_world_wait_policy.h"
static void test_world_wait_rng(void) {
    uint64_t old_time=world_wait_selected[0].time;unsigned old_main=main_thread;
    assert(world_wait_rng_time("6.91667",&world_wait_selected[0].time));
    *(uint64_t*)((unsigned char*)test_ww_exec+0x28)=world_wait_selected[0].time;
    const char *wire="7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8;1,0,17,0,6.91667";
    world_wait_rng_selection parsed[WORLD_WAIT_SELECTED];uint64_t snapshot[4],bits;unsigned schema;
    assert(world_wait_rng_parse(wire,strlen(wire),parsed,snapshot,&schema));
    assert(snapshot[0]==0x7816351056e6040dULL&&snapshot[3]==0x1e20746317e61aa8ULL);
    const char *bad[]={"","0","01","-1","+1","1e2",".1","1.","1..2"," 1","1 ","1000000000000000","0.0000000000000001"};
    for(unsigned i=1;i<sizeof(bad)/sizeof(bad[0]);i++) if(i!=1) assert(!world_wait_rng_time(bad[i],&bits));
    assert(!world_wait_rng_time(bad[0],&bits));assert(world_wait_rng_time("0.001",&bits)&&bits==0x3f50624dd2f1a9fdULL);
    assert(!world_wait_rng_parse(wire,strlen(wire)+1,parsed,snapshot,&schema));
    assert(!world_wait_rng_parse(wire,10,parsed,snapshot,&schema));
    assert(world_wait_rng_parse(wire,strlen(wire),parsed,snapshot,&schema));
    /* Independent Python literal golden for this synthetic saved99 owner. */
    uint64_t golden=world_wait_rng_seed(&world_wait_selected[0],&parsed[0],snapshot);
    assert(golden==0x6afa3d95d687908fULL);
    world_wait_rng_selection changed=parsed[0];changed.slot++;changed.pc++;changed.since++;
    assert(world_wait_rng_seed(&world_wait_selected[0],&changed,snapshot)==golden);
    world_wait_selection selector=world_wait_selected[0];selector.version^=1;selector.index^=1;
    assert(world_wait_rng_seed(&selector,&parsed[0],snapshot)!=golden); /* no XOR-field alias */
    /* Configure under the same restoration lock; a closed gate never reopens. */
    world_wait_started=world_wait_installed=1;world_wait_enabled=1;world_wait_restoring=1;boot_phase=0;
    assert(!world_wait_rng_configure(wire,strlen(wire))&&!world_wait_rng_started);
    world_wait_restoring=0;assert(world_wait_rng_configure(wire,strlen(wire)));
    assert(world_wait_rng_configure(wire,strlen(wire))&&!world_wait_rng_configure(wire,strlen(wire)-1));
    test_wwr_tls=TlsAlloc();assert(test_wwr_tls!=TLS_OUT_OF_INDEXES);
    test_wwr_reset();assert(test_wwr_call()==1&&test_wwr_calls==1&&test_wwr_seen==golden);
    world_wait_event *e=&world_wait_events[0];assert(!e->errors&&e->rng_applied&&e->rng_restored&&e->rng_after==123);
    assert(e->rng_scoped_after==test_wwr_step(golden)&&!world_wait_rng_open&&world_wait_rng_applied==1);
    /* Serialize real C receipt alongside unchanged raw rows for Python audit. */
    const char *export_path=getenv("X4_TEST_WAIT_RNG_OUT");
    if(export_path) {
        FILE *out=fopen(export_path,"wb");assert(out);char row[WORLD_WAIT_ROW_CAP];
        for(unsigned part=0;part<4;part++) {assert(world_wait_format(e,1,part,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);}
        assert(world_wait_rng_format(e,1,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);
        fputs("NATIVE_GATE WORLD_WAIT_STOP schema=1 capacity=128 selected=1 reserved=1 complete=1 rows=4 failures=0 dropped=0 calls=1 import_calls=1 foreign_calls=0 unknown=0 installed=0 restored=1 open=0 peak=1 restore_open=0 index_coverage=0\n",out);
        fputs("NATIVE_GATE WORLD_WAIT_RNG_STOP schema=1 selected=1 rows=1 applied=1 refused=0 open=0 restore_open=0 closed=1 global_sync=0 exception_cleanup=0\n",out);fclose(out);
    }
    unsigned calls=test_wwr_calls;assert(test_wwr_call()==1&&test_wwr_calls==calls+1);
    assert(world_wait_events[1].rng_refusal&WWR_DUPLICATE);
    assert(!world_wait_events[1].rng_applied&&*(uint64_t*)((unsigned char*)test_ww_tls+0x318)==test_wwr_step(123));
    /* Every pre-mutation refusal retains the original's natural RNG advance. */
    for(unsigned kind=0;kind<11;kind++) {
        test_wwr_reset();unsigned char *self=test_ww_self;
        if(kind==0) world_wait_rng_selected[0].slot++;
        if(kind==1) world_wait_rng_selected[0].pc++;
        if(kind==2) world_wait_rng_selected[0].since++;
        if(kind==3) assert(TlsSetValue(test_wwr_tls,NULL));
        if(kind==4) *(unsigned*)(image+0x6adf9fc)=0x24;
        if(kind==5) *(unsigned*)(self+0xb0)=8; /* explicit seed */
        if(kind==6) ++world_wait_selected[0].version;
        if(kind==7) boot_started=GetTickCount64()-120001;
        if(kind==8) world_wait_restoring=1;
        if(kind==9) world_wait_count=WORLD_WAIT_MAX;
        if(kind==10) test_wwr_mode=9;
        assert(test_wwr_call()==1&&test_wwr_calls==1&&test_wwr_seen==123);
        assert(*(uint64_t*)((unsigned char*)test_ww_tls+0x318)==test_wwr_step(123)&&!world_wait_rng_applied&&!world_wait_rng_open);
        if(kind<8) assert(world_wait_events[0].errors&&world_wait_events[0].rng_refusal&&!world_wait_events[0].rng_applied);
        if(kind==10) assert((world_wait_events[0].rng_refusal&WWR_CLOSED)&&world_wait_restore_open==1);
        if(kind==0) world_wait_rng_selected[0].slot--;
        if(kind==1) world_wait_rng_selected[0].pc--;
        if(kind==2) world_wait_rng_selected[0].since--;
        if(kind==5) *(unsigned*)(self+0xb0)=0;
        if(kind==6) --world_wait_selected[0].version;
    }
    for(unsigned mode=1;mode<=8;mode++) {
        test_wwr_reset();test_wwr_mode=mode;
        if(mode==3) continue; /* controlled worker schedule below */
        assert(test_wwr_call()==(mode==4?0xa5:1));e=&world_wait_events[0];
        assert(e->errors&&e->rng_applied&&!world_wait_rng_open);
        if(mode!=7) assert(e->rng_restored&&e->rng_after==123);else assert(!e->rng_restored&&(e->rng_refusal&WWR_RESTORE));
        if(mode==2) assert(world_wait_rng_restore_open==1&&world_wait_restore_open==1);
        if(mode==5||mode==6) assert(test_wwr_calls==2&&(e->rng_refusal&WWR_NESTED));else assert(test_wwr_calls==1);
    }
    test_wwr_reset();test_wwr_mode=3;test_ww_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_ww_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE worker=CreateThread(NULL,0,test_wwr_worker,NULL,0,NULL);assert(worker&&WaitForSingleObject(test_ww_entered,2000)==WAIT_OBJECT_0);
    world_wait_restore();assert(world_wait_rng_open==1&&world_wait_rng_restore_open==1&&!world_wait_ready());
    assert(SetEvent(test_ww_release)&&WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);e=&world_wait_events[0];
    assert((e->errors&128)&&e->rng_restored&&!world_wait_rng_open&&!world_wait_open);
    CloseHandle(worker);CloseHandle(test_ww_entered);CloseHandle(test_ww_release);ui_thread=GetCurrentThreadId();
    assert(!world_wait_rng_configure(wire,strlen(wire))&&world_wait_restoring&&!world_wait_enabled);
    world_wait_event maximum;memset(&maximum,0xff,sizeof(maximum));char row[WORLD_WAIT_ROW_CAP];
    int length=world_wait_rng_format(&maximum,UINT32_MAX,row,sizeof(row));assert(length>0&&length+32<2048);
    assert(world_wait_rng_format(&maximum,1,row,32)<0);
    unsigned char *read_only=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);DWORD old;
    assert(read_only&&world_wait_rng_writable((uint64_t*)(read_only+8))&&!world_wait_rng_writable((uint64_t*)(read_only+1)));
    assert(VirtualProtect(read_only,4096,PAGE_READONLY,&old)&&!world_wait_rng_writable((uint64_t*)(read_only+8)));VirtualFree(read_only,0,MEM_RELEASE);
    test_world_wait_policy();
    TlsFree(test_wwr_tls);test_wwr_tls=TLS_OUT_OF_INDEXES;assert(TlsSetValue(world_wait_rng_tls,NULL));
    /* Production TLS allocation remains resident; only test configuration resets. */
    world_wait_rng_started=0;world_wait_rng_applied=world_wait_rng_refused=world_wait_rng_exported=world_wait_rng_observed=world_wait_rng_schema=0;world_wait_rng_restore_open=0;
    memset(world_wait_rng_selected,0,sizeof(world_wait_rng_selected));world_wait_rng_manifest[0]=0;
    world_wait_selected[0].time=old_time;*(uint64_t*)((unsigned char*)test_ww_exec+0x28)=old_time;main_thread=old_main;
    puts("saved Wait scoped RNG: original-once, key, refusals, nested, STOP overlap and normal-return restoration PASS");
}
