/* native_gate_gravidar_posts: a post (+0xb8 == +0xa8) merges the unit's new
 * set (+0x68) with the old one left in its scan entry (+0x10), unsorted input
 * sorted, added and removed contacts as portable keys or unmapped with their
 * class; the scan inputs from the scan's entries (the scanner's own entry and
 * group, each listed contact's entry: position, distance, bound, visibility,
 * size, flags, group and list, or z=- when not gathered); kept contacts near
 * their bound as edge items; a scanner outside its sector's lists (homeless);
 * timer posts counted only, no post no work, unreadable and oversize sets, the
 * per-tick row cap, the digest's portable-only and order-free parts, the last
 * error kept, arming, restore, and row and status bounds. */
typedef struct {unsigned char bytes[0x40];} test_gpost_object_t;
typedef struct {unsigned char bytes[0xc8];} test_gpost_unit_t;
enum {TEST_GPOST_ENTRIES=8,TEST_GPOST_GROUPS=3};
static unsigned char test_gpost_worker[0x6900],test_gpost_sector[0x40],test_gpost_record[GWIDE_RECORD],test_gpost_groups[TEST_GPOST_GROUPS*GWIDE_GROUP];
static unsigned char test_gpost_entries[TEST_GPOST_ENTRIES][GWIDE_ENTRY];
static test_gpost_object_t test_gpost_unmapped;
static test_gpost_unit_t test_gpost_unit;
static void *test_gpost_find(void *map,uint64_t runtime,uint32_t mode) {
    (void)map;assert(mode==4);return runtime==0x9999?test_gpost_unmapped.bytes:NULL;
}
static void test_gpost_vector(unsigned char *at,uint64_t *items,unsigned count) {
    uintptr_t bounds[3]={(uintptr_t)items,(uintptr_t)(items+count),(uintptr_t)(items+count)};memcpy(at,bounds,24);
}
static void test_gpost_times(test_gpost_unit_t *u,double scan,double event) {memcpy(u->bytes+0xa8,&scan,8);memcpy(u->bytes+0xb8,&event,8);}
/* Entry i: object runtime, sector 0x74, position x on the axis, size, range, factor, visibility. */
static void test_gpost_entry(unsigned i,uint64_t runtime,float x,float size,float range,float vis) {
    unsigned char *e=test_gpost_entries[i];memset(e,0,GWIDE_ENTRY);uint64_t sector=0x74;const float one=1.0f;
    memcpy(e,&runtime,8);memcpy(e+8,&sector,8);memcpy(e+0x70,&x,4);memcpy(e+0x7c,&one,4);memcpy(e+0xa8,&size,4);memcpy(e+0xac,&range,4);
    memcpy(e+0xb0,&one,4);memcpy(e+0xb4,&vis,4);
}
static void test_gpost_list(unsigned group,unsigned wide,unsigned first,unsigned count) {
    uintptr_t bounds[3]={(uintptr_t)test_gpost_entries[first],(uintptr_t)test_gpost_entries[first+count],(uintptr_t)test_gpost_entries[first+count]};
    memcpy(test_gpost_groups+group*GWIDE_GROUP+(wide?GWIDE_WIDE:GWIDE_NEAR),bounds,24);
}
/* Sector 0x74: group 0 near = 7003 (added) and the unmapped 9999 (added); group 1 near = 7002 (kept, edge) and 7004
 * (kept), wide = the station 7001 (the scanner); group 2 near = 7005 (removed, out of bound) and 7006 (removed
 * within its bound: a later group than the station's). 7007 (removed) is not gathered. */
static void test_gpost_world(void) {
    memset(test_gpost_worker,0,sizeof(test_gpost_worker));memset(test_gpost_groups,0,sizeof(test_gpost_groups));
    uint64_t sector=0x74;memcpy(test_gpost_sector+8,&sector,8);
    uintptr_t record[3]={(uintptr_t)test_gpost_sector,(uintptr_t)test_gpost_groups,(uintptr_t)(test_gpost_groups+TEST_GPOST_GROUPS*GWIDE_GROUP)};
    memcpy(test_gpost_record,record,24);
    uintptr_t records[2]={(uintptr_t)test_gpost_record,(uintptr_t)(test_gpost_record+GWIDE_RECORD)};memcpy(test_gpost_worker+GWIDE_SECTORS,records,16);
    test_gpost_entry(0,0x7003,10000,50,0,0);test_gpost_entry(1,0x9999,20000,5,0,0.5f);
    test_gpost_entry(2,0x7002,39500,50,0,0);test_gpost_entry(3,0x7004,1000,50,0,0);
    test_gpost_entry(4,0x7001,0,100,40000,0);test_gpost_entries[4][0xbb]=1; /* due */
    test_gpost_entry(5,0x7005,41000,50,0,0);test_gpost_entry(6,0x7006,30000,50,0,0);
    test_gpost_list(0,0,0,2);test_gpost_list(0,1,2,0);test_gpost_list(1,0,2,2);test_gpost_list(1,1,4,1);test_gpost_list(2,0,5,2);test_gpost_list(2,1,7,0);
}
static void test_gravidar_posts(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7001,0x23a8c) && world_identity_record(0x7002,0x6dd4b) && world_identity_record(0x7003,0x6dd60) &&
           world_identity_record(0x7005,0x9fb3b) && world_identity_record(0x7004,0x6dd70) && world_identity_record(0x7006,0x9fb3c) &&
           world_identity_record(0x7007,0x9fb3d));
    void *saved_map,*saved_worker;memcpy(&saved_map,image+GPOST_MAP,8);memcpy(&saved_worker,image+GPOST_WORKER,8);
    void *fake=(void*)0x1234,*worker=test_gpost_worker;memcpy(image+GPOST_MAP,&fake,8);memcpy(image+GPOST_WORKER,&worker,8);
    gpost_find_override=test_gpost_find;memset(&test_gpost_unmapped,0,sizeof(test_gpost_unmapped));*(void**)test_gpost_unmapped.bytes=image+0x2b7d738;
    test_gpost_world();
    test_gpost_unit_t *unit=&test_gpost_unit;memset(unit,0,sizeof(*unit));unsigned char *result=test_gpost_entries[4];
    /* Arming: the keyed apply armed, before the first admitted frame, on the MoveWorker. */
    gpost_armed=0;gpost_active=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!gpost_arm(0));stats.issued=1;assert(!gpost_arm(1));stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!gpost_arm(1));
    ui_thread=GetCurrentThreadId();
    static uint64_t now_set[8],old_set[8];
    test_gpost_times(unit,25.5,25.5);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(!gpost_posts); /* not armed */
    assert(gpost_arm(1) && gpost_armed && gpost_active && gpost_arm(1));
    /* A change post at tick 1474: 7002/7004 kept, 7003 and the unmapped 9999 added (new set unsorted), 7005/7006/7007 removed. */
    stats.issued=1474;boot_phase=3;
    now_set[0]=0x9999;now_set[1]=0x7002;now_set[2]=0x7003;now_set[3]=0x7004;test_gpost_vector(unit->bytes+0x68,now_set,4);
    old_set[0]=0x7002;old_set[1]=0x7004;old_set[2]=0x7005;old_set[3]=0x7006;old_set[4]=0x7007;test_gpost_vector(result+0x10,old_set,5);
    SetLastError(0x55);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(GetLastError()==0x55);
    assert(gpost_posts==1 && gpost_changes==1 && !gpost_timers && gpost_rows==1 && gpost_unmapped==1 && !gpost_homeless && gpost_absent==1 && !gpost_failures);
    static char row[4608];assert(gpost_row(0,row,sizeof(row))>0);
    char expect[320];
    snprintf(expect,sizeof(expect),"index=0 tick=1474 unit=s:0000000000023a8c sizes=5/4 added=2 removed=3 time=4039800000000000 "
        "a={p=00000000,00000000,00000000 r=%08x f=%08x s=%08x v=00000000 fl=8 z=1/3w} plus=",gpost_bits(40000),gpost_bits(1),gpost_bits(100));
    assert(!strncmp(row,expect,strlen(expect)));
    snprintf(expect,sizeof(expect),"plus=s:000000000006dd60{p=%08x,00000000,00000000 d=%08x b=%08x v=00000000 s=%08x fl=0 z=0n}",
        gpost_bits(10000),gpost_bits(10000),gpost_bits(40150),gpost_bits(50));assert(strstr(row,expect));
    snprintf(expect,sizeof(expect),",u:9999/02b7d738{p=%08x,00000000,00000000 d=%08x b=%08x v=%08x s=%08x fl=0 z=0n} minus=",
        gpost_bits(20000),gpost_bits(20000),gpost_bits(40000*1.5f+105),gpost_bits(0.5f),gpost_bits(5));assert(strstr(row,expect));
    snprintf(expect,sizeof(expect)," minus=s:000000000009fb3b{p=%08x,00000000,00000000 d=%08x b=%08x v=00000000 s=%08x fl=0 z=2n},"
        "s:000000000009fb3c{p=%08x,00000000,00000000 d=%08x b=%08x v=00000000 s=%08x fl=0 z=2n},s:000000000009fb3d{z=-} edge=",
        gpost_bits(41000),gpost_bits(41000),gpost_bits(40150),gpost_bits(50),gpost_bits(30000),gpost_bits(30000),gpost_bits(40150),gpost_bits(50));
    assert(strstr(row,expect));
    snprintf(expect,sizeof(expect)," edge=s:000000000006dd4b{p=%08x,00000000,00000000 d=%08x b=%08x v=00000000 s=%08x fl=0 z=1n}",
        gpost_bits(39500),gpost_bits(39500),gpost_bits(40150),gpost_bits(50));
    assert(strstr(row,expect) && !strstr(row,"6dd70"));
    uint64_t first=gpost_digest;
    /* The digest: the unmapped contact does not enter it; the same change gives the same part. */
    now_set[0]=0x7002;now_set[1]=0x7003;now_set[2]=0x7004;now_set[3]=0x8888;test_gpost_vector(unit->bytes+0x68,now_set,4);gpost_digest=0;
    gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(gpost_digest==first && gpost_rows==2);
    /* A timer post (equal sets) is counted only; no post (event older than the scan) is ignored. */
    now_set[0]=0x7002;now_set[1]=0x7004;now_set[2]=0x7005;now_set[3]=0x7006;now_set[4]=0x7007;test_gpost_vector(unit->bytes+0x68,now_set,5);
    gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(gpost_timers==1 && gpost_rows==2 && gpost_posts==3);
    test_gpost_times(unit,26.0,25.5);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(gpost_posts==3);
    /* Removal only, an empty new set (no edges); an empty old set. */
    test_gpost_times(unit,25.5,25.5);test_gpost_vector(unit->bytes+0x68,now_set,0);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);
    assert(gpost_row((uint64_t)(gpost_rows-1),row,sizeof(row))>0 && strstr(row," sizes=5/0 added=0 removed=5 ") && strstr(row," plus= minus=s:000000000006dd4b{") &&
           strstr(row,"z=-} edge=") && row[strlen(row)-1]=='=');
    test_gpost_vector(unit->bytes+0x68,now_set,2);test_gpost_vector(result+0x10,old_set,0);gpost_note(unit,result,0x6dd4b,WORLD_KEY_CREATED);
    assert(gpost_row((uint64_t)(gpost_rows-1),row,sizeof(row))>0 && strstr(row," unit=c:000000000006dd4b sizes=0/2 added=2 removed=0 ") &&
           strstr(row," plus=s:000000000006dd4b{") && strstr(row,",s:000000000006dd70{"));
    /* A scanner whose entry is in no list of its sector: its own inputs, z=-, contacts z=- (homeless). */
    uint64_t other=0x75;memcpy(result+8,&other,8);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);other=0x74;memcpy(result+8,&other,8);
    assert(gpost_homeless==1 && gpost_row((uint64_t)(gpost_rows-1),row,sizeof(row))>0 && strstr(row," fl=8 z=-} plus=s:000000000006dd4b{z=-},s:000000000006dd70{z=-}"));
    test_gpost_vector(result+0x10,old_set,5);
    /* Unreadable or oversize sets fail (counted); a busy note fails. */
    uint64_t failures=gpost_failures;uintptr_t bad[2]={16,8};memcpy(result+0x10,bad,16);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);
    assert(gpost_failures==failures+1);
    test_gpost_vector(result+0x10,old_set,GPOST_SET_MAX+1);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(gpost_oversize==1 && gpost_failures==failures+2);
    test_gpost_vector(result+0x10,old_set,5);gpost_busy=1;gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);gpost_busy=0;assert(gpost_failures==failures+3);
    /* The per-tick row cap: further change rows of the tick are dropped (counted). */
    stats.issued=1500;LONG64 rows=gpost_rows;test_gpost_vector(unit->bytes+0x68,now_set,0);
    for(unsigned i=0;i<GPOST_TICK_ROWS+3;i++) gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);
    assert(gpost_rows==rows+GPOST_TICK_ROWS && gpost_dropped==3);
    /* Widest row and status; eviction; restore stops it. */
    gpost_row_t *w=&gpost_ring[0];memset(w,0xff,sizeof(*w));w->unit_klass=WORLD_KEY_CREATED;w->added=w->removed=w->edges=UINT32_MAX;
    for(unsigned i=0;i<GPOST_LIST;i++) {
        gpost_item_t *items[3]={&w->plus[i],&w->minus[i],&w->edge[i]};
        for(unsigned k=0;k<3;k++) {items[k]->klass=WORLD_KEY_NONE;items[k]->state=GPOST_FOUND;}
    }
    gpost_rows=1;int n=gpost_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_POST_ROW \n")<4608);
    gpost_rows=GPOST_RING+2;assert(gpost_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    char status[448];gpost_posts=gpost_changes=gpost_timers=gpost_dropped=gpost_unmapped=gpost_oversize=gpost_failures=gpost_digest=UINT64_MAX;
    gpost_homeless=gpost_absent=UINT64_MAX;gpost_rows=INT64_MAX;
    n=gpost_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE GRAVIDAR_POSTS_STOP \n")<448);
    gpost_restore();gpost_posts=0;test_gpost_times(unit,30.0,30.0);gpost_note(unit,result,0x23a8c,WORLD_KEY_SAVED);assert(!gpost_active && !gpost_posts);
    gpost_armed=0;gpost_rows=0;gpost_posts=gpost_changes=gpost_timers=gpost_dropped=gpost_unmapped=gpost_oversize=gpost_failures=gpost_digest=0;
    gpost_homeless=gpost_absent=0;gpost_find_override=NULL;
    memcpy(image+GPOST_MAP,&saved_map,8);memcpy(image+GPOST_WORKER,&saved_worker,8);
    memset(gpost_ring,0,sizeof(gpost_ring));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("Gravidar posts merge/added/removed/unmapped class/scan entry inputs/groups/not gathered/edges/homeless/timer/no-post/digest/failures/row cap/arming/restore/status tests PASS\n");
}
/* With the X4 9.00 executable: the apply's set swap (+0x68 <-> entry+0x10), the
 * post's +0xb8 = TLS time and +0xa8 = TLS time at entry, the unit slots that
 * fill the entry, the entry builder 0x10caef0 and the pair check 0x10b4100
 * (layout and bound pinned), the component lookup. */
static void test_gravidar_posts_native_image(const char *path) {
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
    assert(hash_ok(image+0x10acd70,0x8d6,"a1f8ae33a9b8c9494c231878651d7a146767b3a44b5fd94f651afcafc04ab6df"));
    /* Swap of the unit's primary set (+0x68..+0x78) with the scan entry's (+0x10..+0x20). */
    assert(!memcmp(image+0x10acf8e,"\x4d\x3b\xfe\x74\x2c\x49\x8b\x0f\x49\x8b\x06\x49\x89\x07\x49\x89\x0e\x49\x8b\x4f\x08\x49\x8b\x46\x08\x49\x89\x47\x08\x49\x89\x4e\x08\x49\x8b\x4f\x10\x49\x8b\x46\x10\x49\x89\x47\x10\x49\x89\x4e\x10",0x31));
    /* +0xa8 = TLS time before the post decision; the post writes +0xb8 = TLS time last. */
    assert(!memcmp(image+0x10ad31d,"\xb8\x00\x03\x00\x00\x48\x8b\x04\x38\x48\x89\x85\xa8\x00\x00\x00",0x10) && !memcmp(image+0x10ad540,"\x49\x8b\x04\x3e\x48\x8b\x8c\x24\xc0\x00\x00\x00\x48\x89\x81\xb8\x00\x00\x00",0x13));
    assert(hash_ok(image+GPOST_FIND,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"));
    /* ActiveGravidarUnit slots 3/4/5: +0x5c == 0, +0xc4 != 0, range float +0xc0 (the entry's +0xbc.., +0xac). */
    uint64_t base=nt.OptionalHeader.ImageBase,slots[6];memcpy(slots,image+0x2c67610,48);
    assert(slots[3]-base==0x10abae0 && slots[4]-base==0x10abaf0 && slots[5]-base==0x10abb00);
    assert(!memcmp(image+0x10abae0,"\x8b\x41\x5c",3) && !memcmp(image+0x10abaf0,"\x8b\x81\xc4\x00\x00\x00",6) &&
           !memcmp(image+0x10abb00,"\xf3\x0f\x10\x81\xc0\x00\x00\x00",8));
    /* The entry builder (object, sector, position +0x70, size/range/factor/visibility +0xa8.., flags) and the pair check. */
    assert(hash_ok(image+0x10caef0,432,"8657e0b3c52e73a90e4f756465d5a6fc9e956a7ae55debe3f77d8ac7dbec36f2"));
    assert(hash_ok(image+0x10b4100,4863,"8b1702ac67fba19ece72ed8541a5d91e374f8c5873d8712e73004714c4a6c5dc"));
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("Gravidar posts native apply (set swap, scan and event times), unit slots, entry builder and pair check verified");
}
