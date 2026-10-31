/* native_gate_gravidar_digest: tree walk over the 256 buckets, per-unit factor/time/class
 * digests keyed by portable ID and independent of order, passive units without scan
 * times, active-source lists, field-sector deadlines, change rows, refusals. */
typedef struct {void *parent,*left,*right;uint64_t pad,key;void *unit;} test_gdig_node;
static __declspec(align(16)) unsigned char test_gdig_units[5][0x100];
static __declspec(align(16)) unsigned char test_gdig_sectors[4][0x500];
static test_gdig_node test_gdig_nodes[5];
static void test_gdig_reset_digests(void) {
    gdig_units=gdig_unmapped=gdig_factors=gdig_times=gdig_classes=gdig_passive=0;
    gdig_sectors=gdig_sector_unmapped=gdig_deadlines=gdig_attention=0;
    memset(gdig_active,0,sizeof(gdig_active));memset(gdig_active_digest,0,sizeof(gdig_active_digest));memset(gdig_active_unmapped,0,sizeof(gdig_active_unmapped));
}
static void test_gdig_sector(unsigned i,uint64_t runtime,unsigned char field,double due,int32_t attention) {
    memset(test_gdig_sectors[i],0,0x500);void *vt=image+GDIG_SECTOR;memcpy(test_gdig_sectors[i],&vt,8);
    memcpy(test_gdig_sectors[i]+8,&runtime,8);test_gdig_sectors[i][0x442]=field;
    memcpy(test_gdig_sectors[i]+0x448,&due,8);memcpy(test_gdig_sectors[i]+0x1ec,&attention,4);
}
static uint64_t test_gdig_row_count(void) {return (uint64_t)InterlockedCompareExchange64(&gdig_rows,0,0);}
static void test_gravidar_digest(void) {
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x701,0x29bfd) && world_identity_record(0x702,0x9fb3b) && world_identity_record(0x703,0x10) &&
           world_identity_record(0x704,0x11) && world_identity_record(0x801,0x7a5) && world_identity_record(0x802,0x7c8));
    unsigned char *worker=VirtualAlloc(NULL,0x7000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(worker);
    void *vt=image+GDIG_VTABLE;memcpy(worker,&vt,8);
    for(unsigned b=0;b<GDIG_BUCKETS;b++) InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x88+b*0x28));
    *(void**)(image+GDIG_WORKER)=worker;
    /* Units: three mapped (two in one bucket's tree, one in another), one unmapped. */
    uint64_t runtimes[5]={0x701,0x702,0x703,0x799,0x704};
    for(unsigned i=0;i<5;i++) {
        memset(test_gdig_units[i],0,0x100);memcpy(test_gdig_units[i]+8,&runtimes[i],8);
        float f=0.5f+(float)i;memcpy(test_gdig_units[i]+0x44,&f,4);double t[3]={1.0+i,2.0+i,3.0+i};memcpy(test_gdig_units[i]+0xa8,t,24);
        void *uv=image+0x2c67610;memcpy(test_gdig_units[i],&uv,8);
        memset(&test_gdig_nodes[i],0,sizeof(test_gdig_nodes[i]));test_gdig_nodes[i].unit=test_gdig_units[i];test_gdig_nodes[i].key=runtimes[i];
    }
    test_gdig_nodes[0].left=&test_gdig_nodes[1];                                   /* bucket 1: root 0, left child 1 */
    *(void**)(worker+0x2890+1*0x40+8)=&test_gdig_nodes[0];
    test_gdig_nodes[2].right=&test_gdig_nodes[3];                                  /* bucket 3: root 2, right child 3 */
    *(void**)(worker+0x2890+3*0x40+8)=&test_gdig_nodes[2];
    /* Active sources: pending {unit 0}, current {unit 1, unmapped 3}. */
    static void *pending[1],*current[2];pending[0]=test_gdig_units[0];current[0]=test_gdig_units[1];current[1]=test_gdig_units[3];
    uintptr_t v[2]={(uintptr_t)pending,(uintptr_t)(pending+1)};memcpy(worker+0x48,v,16);
    v[0]=(uintptr_t)current;v[1]=(uintptr_t)(current+2);memcpy(worker+0x60,v,16);
    uint64_t saved_issued=stats.issued;stats.issued=2;
    assert(gdig_frame() && !strcmp(gdig_stage,"ok") && gdig_units==3 && gdig_unmapped==1 && gdig_tick==2 && !gdig_passive && !gdig_sectors);
    assert(gdig_active[0]==1 && gdig_active[1]==1 && gdig_active_unmapped[1]==1 && gdig_active_digest[0]==gdig_mix(0x29bfd));
    uint64_t factors=gdig_factors,times=gdig_times,classes=gdig_classes;
    /* The same units in other trees and order give the same digests; one factor changes only the factor digest. */
    memset(worker+0x2890+1*0x40+8,0,8);memset(worker+0x2890+3*0x40+8,0,8);
    for(unsigned i=0;i<4;i++) {test_gdig_nodes[i].left=test_gdig_nodes[i].right=NULL;}
    test_gdig_nodes[3].left=&test_gdig_nodes[0];test_gdig_nodes[0].right=&test_gdig_nodes[2];test_gdig_nodes[2].left=&test_gdig_nodes[1];
    *(void**)(worker+0x2890+200*0x40+8)=&test_gdig_nodes[3];
    assert(gdig_frame() && gdig_factors==factors && gdig_times==times && gdig_classes==classes && gdig_units==3);
    float f=7.0f;memcpy(test_gdig_units[2]+0x44,&f,4);
    assert(gdig_frame() && gdig_factors!=factors && gdig_times==times && gdig_classes==classes);
    double t=9.0;memcpy(test_gdig_units[1]+0xb0,&t,8);assert(gdig_frame() && gdig_times!=times);
    char status[1024];int n=gdig_status(status,sizeof(status));
    assert(n>0 && strstr(status,"tick=2 units=3 unmapped=1 ") && strstr(status," active=1/1 ") && strstr(status," stage=ok passive=0 sectors=0 "));
    printf("NATIVE_GATE GRAVIDAR_DIGEST tick=2 %s\n",status);
    /* A passive unit (0x68 bytes) has no scan times: whatever lies at +0xa8 (the
     * next heap block) never reaches the times digest; factor and class do. */
    void *pv=image+0x2c67660;memcpy(test_gdig_units[4],&pv,8);test_gdig_nodes[2].right=&test_gdig_nodes[4];
    assert(gdig_frame() && gdig_units==4 && gdig_passive==1);
    times=gdig_times;factors=gdig_factors;classes=gdig_classes;
    t=123.0;memcpy(test_gdig_units[4]+0xb8,&t,8);memset(test_gdig_units[4]+0xa8,0x5a,8);
    assert(gdig_frame() && gdig_times==times && gdig_factors==factors && gdig_classes==classes);
    f=3.25f;memcpy(test_gdig_units[4]+0x44,&f,4);assert(gdig_frame() && gdig_factors!=factors && gdig_times==times);
    /* Field sectors of the sector list: one mapped field sector, a field-less
     * one, an unmapped field sector, a null entry and a foreign object. */
    test_gdig_sector(0,0x801,1,12.5,2);test_gdig_sector(1,0x802,0,0.0,2);test_gdig_sector(2,0x8ff,1,8.0,7);
    test_gdig_sector(3,0x802,1,9.0,7);memcpy(test_gdig_sectors[3],&pv,8);
    static unsigned char entries[5][32];memset(entries,0,sizeof(entries));
    for(unsigned i=0;i<4;i++) *(void**)entries[i]=test_gdig_sectors[i];
    v[0]=(uintptr_t)entries[0];v[1]=(uintptr_t)entries[5];memcpy(worker+0x68b8,v,16);
    assert(gdig_frame() && gdig_sectors==1 && gdig_sector_unmapped==1 && !strcmp(gdig_stage,"ok"));
    uint64_t deadlines=gdig_deadlines,attention=gdig_attention;
    assert(deadlines==gdig_mix(0x7a5^gdig_mix(0x4029000000000000ULL)) && attention==gdig_mix(0x7a5^gdig_mix(2)));
    double due=13.0;memcpy(test_gdig_sectors[0]+0x448,&due,8);assert(gdig_frame() && gdig_deadlines!=deadlines && gdig_attention==attention);
    int32_t level=5;memcpy(test_gdig_sectors[0]+0x1ec,&level,4);assert(gdig_frame() && gdig_attention!=attention);
    test_gdig_sectors[1][0x442]=1;assert(gdig_frame() && gdig_sectors==2);test_gdig_sectors[1][0x442]=0;
    /* A misaligned or oversized list is refused. */
    v[1]=(uintptr_t)entries[0]+33;memcpy(worker+0x68b8,v,16);assert(!gdig_frame() && !strcmp(gdig_stage,"sectors"));
    v[1]=(uintptr_t)entries[0]+32*(GDIG_SECTOR_CAP+1);memcpy(worker+0x68b8,v,16);assert(!gdig_frame() && !strcmp(gdig_stage,"sectors"));
    v[1]=(uintptr_t)entries[5];memcpy(worker+0x68b8,v,16);assert(gdig_frame());

    /* Rows: arming takes the tick count once, before the first tick. */
    __typeof__(stats.active) saved_active=stats.active;DWORD saved_ui=ui_thread;
    assert(!gdig_armed && !gdig_row_ticks);stats.active=1;ui_thread=GetCurrentThreadId();stats.issued=0;
    assert(!gdig_arm(-1) && !gdig_arm(1.5) && !gdig_arm(NAN) && !gdig_arm(GDIG_ROW_TICKS_MAX+1.0) && !gdig_armed);
    stats.active=0;assert(!gdig_arm(2));stats.active=1;
    ui_thread=GetCurrentThreadId()+1;assert(!gdig_arm(2));ui_thread=GetCurrentThreadId();
    stats.issued=1;assert(!gdig_arm(2));stats.issued=0;
    assert(gdig_arm(2) && gdig_armed && gdig_row_ticks==2 && gdig_table && gdig_ring && gdig_arm(2) && !gdig_arm(3) && !gdig_arm(0));
    /* Tick 1: first sightings. Unit 4 (passive) and unit 0 in constructor state give no row. */
    f=1.0f;memcpy(test_gdig_units[4]+0x44,&f,4);memcpy(test_gdig_units[0]+0x44,&f,4);
    double unset[3]={-1.0,-1.0,-1.0};memcpy(test_gdig_units[0]+0xa8,unset,24);
    stats.issued=1;assert(gdig_frame());
    /* units 1 and 2 (active, applied), sector 0x7a5 */
    assert(test_gdig_row_count()==3 && gdig_table_count==5 && !gdig_row_drop);
    char row[384];int seen_sector=0,seen_unit=0;
    for(uint64_t i=0;i<3;i++) {
        assert(gdig_row(i,row,sizeof(row))>0 && strstr(row," tick=1 ") && strstr(row," first=1 "));
        if(strstr(row,"kind=s key=s:00000000000007a5 first=1 attention=5 due=402a000000000000")) seen_sector=1;
        if(strstr(row,"kind=a key=s:0000000000029bfd")) assert(0);
        if(strstr(row,"kind=a key=s:0000000000000010 first=1 factor=40e00000 class=02c67610 scan=4008000000000000 due=4010000000000000 event=4014000000000000")) seen_unit=1;
    }
    assert(seen_sector && seen_unit && !gdig_row(3,row,sizeof(row)));
    printf("NATIVE_GATE GRAVIDAR_DIGEST_ROW %s\n",row);
    /* Tick 2 (within the row ticks): a time change gives a row; no change, no row. */
    stats.issued=2;assert(gdig_frame() && test_gdig_row_count()==3);
    t=11.0;memcpy(test_gdig_units[0]+0xb0,&t,8);assert(gdig_frame() && test_gdig_row_count()==4);
    assert(gdig_row(3,row,sizeof(row))>0 && strstr(row,"tick=2 kind=a key=s:0000000000029bfd first=0 factor=3f800000 ") && strstr(row," due=4026000000000000 "));
    /* Tick 3: times alone no longer give a row, a factor or a sector change does. */
    stats.issued=3;t=12.0;memcpy(test_gdig_units[0]+0xb0,&t,8);assert(gdig_frame() && test_gdig_row_count()==4);
    f=0.25f;memcpy(test_gdig_units[4]+0x44,&f,4);assert(gdig_frame() && test_gdig_row_count()==5);
    assert(gdig_row(4,row,sizeof(row))>0 && strstr(row,"tick=3 kind=p key=s:0000000000000011 first=0 factor=3e800000 class=02c67660"));
    due=14.0;memcpy(test_gdig_sectors[0]+0x448,&due,8);assert(gdig_frame() && test_gdig_row_count()==6);
    assert(gdig_row(5,row,sizeof(row))>0 && strstr(row,"tick=3 kind=s key=s:00000000000007a5 first=0 attention=5 due=402c000000000000"));
    /* A full table drops new keys (counted), known keys keep their rows. */
    uint64_t count=gdig_table_count;gdig_table_count=GDIG_TABLE/4*3;
    assert(world_identity_record(0x705,0x12));uint64_t r5=0x705;memcpy(test_gdig_units[4]+8,&r5,8);
    assert(gdig_frame() && gdig_table_full==1 && gdig_row_drop==1 && test_gdig_row_count()==6);
    gdig_table_count=count;r5=0x704;memcpy(test_gdig_units[4]+8,&r5,8);
    /* Eviction marker and worst-case widths. */
    LONG64 cursor=gdig_rows;gdig_rows=GDIG_RING+4;
    assert(gdig_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1") && gdig_row(GDIG_RING+3,row,sizeof(row))>0);
    gdig_rows=INT64_MAX;
    for(unsigned k=0;k<3;k++) {
        uint64_t index=(uint64_t)INT64_MAX-1-k;gdig_row_t *e=&gdig_ring[index%GDIG_RING];
        memset(e,0xff,sizeof(*e));e->kind=(uint8_t)k;e->klass=WORLD_KEY_CREATED;if(k==GDIG_ROW_SECTOR) e->factor=0x80000000u;
        n=gdig_row(index,row,sizeof(row));
        assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_DIGEST_ROW \n")<sizeof(row));
    }
    gdig_rows=cursor;
    /* A cycle or a broken worker is refused, not followed. */
    test_gdig_nodes[1].left=&test_gdig_nodes[3];assert(!gdig_frame() && !strcmp(gdig_stage,"tree"));test_gdig_nodes[1].left=NULL;
    void *bad=NULL;memcpy(worker,&bad,8);assert(!gdig_frame() && !strcmp(gdig_stage,"worker"));
    *(void**)(image+GDIG_WORKER)=NULL;assert(!gdig_frame() && !strcmp(gdig_stage,"worker"));
    /* Status bounds. */
    gdig_tick=gdig_units=gdig_unmapped=gdig_factors=gdig_times=gdig_classes=gdig_passive=UINT64_MAX;
    gdig_sectors=gdig_sector_unmapped=gdig_deadlines=gdig_attention=gdig_table_count=gdig_table_full=gdig_row_drop=UINT64_MAX;
    for(unsigned k=0;k<2;k++) gdig_active[k]=gdig_active_digest[k]=gdig_active_unmapped[k]=UINT64_MAX;
    gdig_tick=UINT64_MAX-1;gdig_rows=INT64_MAX;gdig_row_ticks=UINT32_MAX;gdig_stage="sectors";n=gdig_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_DIGEST tick=18446744073709551615 \n")<sizeof(status));
    for(unsigned b=0;b<GDIG_BUCKETS;b++) DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x88+b*0x28));
    VirtualFree(worker,0,MEM_RELEASE);test_gdig_reset_digests();gdig_tick=UINT64_MAX;gdig_stage="idle";
    gdig_armed=0;gdig_row_ticks=0;gdig_rows=0;gdig_table_count=gdig_table_full=gdig_row_drop=0;stats.active=saved_active;ui_thread=saved_ui;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;
    printf("Gravidar digest tree walk, order independence, factor/time digests, passive units, field sectors, rows, refusals and status tests PASS\n");
}
