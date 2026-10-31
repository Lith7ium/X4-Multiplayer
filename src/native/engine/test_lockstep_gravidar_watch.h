/* native_gate_gravidar_watch: arming refusals, a keyed apply of a watched unit
 * through gravidar_key_unit (sets before/after, saved/unmapped contacts, sorted,
 * RNG restored), unwatched units untouched, row/set text and ring eviction.
 * Synthetic unit layout as read by gravidar_snapshot_contacts; no native code. */
static unsigned char test_gw_unit[0x100],test_gw_result[0x40],test_gw_other[0x100];
static uint64_t test_gw_primary[4],test_gw_scan[4],test_gw_added[4];
static unsigned test_gw_calls;
static void test_gw_vector(unsigned char *at,uint64_t *items,unsigned count) {
    uintptr_t bounds[3]={(uintptr_t)items,(uintptr_t)(items+count),(uintptr_t)(items+4)};
    if(!count) memset(bounds,0,sizeof(bounds));
    memcpy(at,bounds,sizeof(bounds));
}
/* The apply copies the scan result into the primary set and reports one added contact. */
static void test_gw_original(void *unit,void *queue,void *result) {
    (void)queue;(void)result;++test_gw_calls;
    uint64_t *rng=(uint64_t*)((unsigned char*)((void**)*(void***)((unsigned char*)NtCurrentTeb()+0x58))[0]+0x318);
    *rng=*rng*6364136223846793005ULL+1;
    if(unit==test_gw_unit) {
        memcpy(test_gw_primary,test_gw_scan,sizeof(test_gw_scan));test_gw_vector(test_gw_unit+0x68,test_gw_primary,3);
        test_gw_added[0]=test_gw_scan[2];test_gw_vector(test_gw_unit+0x80,test_gw_added,1);
    }
}
static void test_gravidar_watch(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    if(!world_identities) {
        world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        assert(world_identities);
    }
    LONG saved_sealed=world_identity_sealed;world_identity_sealed=0;
    /* Arming: UI thread, before the first gated frame, 16 lowercase hex digits, nonzero, slot 1..8 in order. */
    ui_thread=GetCurrentThreadId();stats.issued=0;gravidar_watch_count=0;
    assert(!gravidar_watch_add("9fb3b",5,1) && !gravidar_watch_add("000000000009FB3B",16,1) && !gravidar_watch_add("0000000000000000",16,1));
    stats.issued=1;assert(!gravidar_watch_add("000000000009fb3b",16,1));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!gravidar_watch_add("000000000009fb3b",16,1));ui_thread=GetCurrentThreadId();
    assert(!gravidar_watch_add("000000000009fb3b",16,0) && !gravidar_watch_add("000000000009fb3b",16,2) && !gravidar_watch_count);
    assert(gravidar_watch_add("000000000009fb3b",16,1) && gravidar_watch_count==1);
    /* The loading UI reloads the probe, which repeats the list (sc-missions2): the same ID
     * in the same slot re-arms, another ID in an armed slot or a duplicate does not. */
    assert(gravidar_watch_add("000000000009fb3b",16,1) && gravidar_watch_count==1);
    assert(!gravidar_watch_add("000000000009f121",16,1) && !gravidar_watch_add("000000000009fb3b",16,2));
    char id[17];
    for(unsigned i=1;i<GRAVIDAR_WATCH_MAX;i++) {snprintf(id,sizeof(id),"%016x",0x100u+i);assert(gravidar_watch_add(id,16,i+1));}
    assert(!gravidar_watch_add("0000000000000fff",16,GRAVIDAR_WATCH_MAX+1) && gravidar_watch_count==(LONG)GRAVIDAR_WATCH_MAX);
    assert(gravidar_watch_add("000000000009fb3b",16,1));
    for(unsigned i=1;i<GRAVIDAR_WATCH_MAX;i++) {snprintf(id,sizeof(id),"%016x",0x100u+i);assert(gravidar_watch_add(id,16,i+1));}
    assert(gravidar_watch_count==(LONG)GRAVIDAR_WATCH_MAX);
    stats.issued=1;assert(!gravidar_watch_add("000000000009fb3b",16,1));stats.issued=0; /* no re-arm once frames run */
    assert(gravidar_watched(0x9fb3b) && !gravidar_watched(0x9fb3c));
    /* World: the watched unit and two contacts have saved IDs; one contact has none. */
    assert(world_identity_record(0xaa01,0x9fb3b) && world_identity_record(0xaa02,0x20) && world_identity_record(0xaa03,0x10));
    assert(world_identity_record(0xbb01,0x9fb3c));
    memset(test_gw_unit,0,sizeof(test_gw_unit));memset(test_gw_other,0,sizeof(test_gw_other));memset(test_gw_result,0,sizeof(test_gw_result));
    uint64_t runtime=0xaa01;memcpy(test_gw_unit+8,&runtime,8);runtime=0xbb01;memcpy(test_gw_other+8,&runtime,8);
    InitializeCriticalSection((CRITICAL_SECTION*)(test_gw_unit+0x18));InitializeCriticalSection((CRITICAL_SECTION*)(test_gw_other+0x18));
    test_gw_primary[0]=0xaa02;test_gw_vector(test_gw_unit+0x68,test_gw_primary,1);
    test_gw_scan[0]=0xaa02;test_gw_scan[1]=0xcc77;test_gw_scan[2]=0xaa03;test_gw_vector(test_gw_result+0x10,test_gw_scan,3);
    /* Fake TLS slot 0 with the thread's RNG word at +0x318, restored afterwards. */
    static unsigned char block[0x400];static void *slots[1];slots[0]=block;
    void **teb_slot=(void**)((unsigned char*)NtCurrentTeb()+0x58),*prior_slots=*teb_slot;
    uint64_t word=0x1234;memcpy(block+0x318,&word,8);
    LONG64 cursor=gravidar_watch_cursor,keyed=gravidar_keyed;test_gw_calls=0;stats.issued=644;
    *teb_slot=slots;
    gravidar_key_unit(test_gw_original,test_gw_unit,NULL,test_gw_result);
    gravidar_key_unit(test_gw_original,test_gw_other,NULL,test_gw_result); /* saved but not watched: no row */
    gravidar_key_unit(test_gw_original,test_gw_unit,NULL,test_gw_result);
    *teb_slot=prior_slots;
    memcpy(&word,block+0x318,8);
    assert(test_gw_calls==3 && word==0x1234 && gravidar_keyed==keyed+3 && !gravidar_watch_busy);
    assert(gravidar_watch_cursor==cursor+2 && !gravidar_watch_failures);
    char out[6144];
    assert(gravidar_watch_row_text((uint64_t)cursor,out,sizeof(out)));
    char expect[256];
    snprintf(expect,sizeof(expect),"index=%lld tick=644 unit=000000000009fb3b ord=1 ok=1 counts=1/0/3/0/3/0/1/0",(long long)cursor);
    assert(!strcmp(out,expect));
    snprintf(expect,sizeof(expect),"index=%lld tick=644 unit=000000000009fb3b ord=2 ok=1 counts=3/0/3/0/3/0/1/0",(long long)cursor+1);
    assert(gravidar_watch_row_text((uint64_t)cursor+1,out,sizeof(out)) && !strcmp(out,expect));
    /* Scan result: saved contacts sorted by saved ID, the unmapped one last with its runtime ID. */
    assert(gravidar_watch_set_text((uint64_t)cursor,2,out,sizeof(out)));
    snprintf(expect,sizeof(expect),"index=%lld set=2 present=1 count=3 keys=s:0000000000000010,s:0000000000000020,u:000000000000cc77",(long long)cursor);
    assert(!strcmp(out,expect));
    assert(gravidar_watch_set_text((uint64_t)cursor,6,out,sizeof(out)));
    snprintf(expect,sizeof(expect),"index=%lld set=6 present=1 count=1 keys=s:0000000000000010",(long long)cursor);
    assert(!strcmp(out,expect));
    assert(gravidar_watch_set_text((uint64_t)cursor,1,out,sizeof(out)) && strstr(out,"present=0 count=0 keys="));
    assert(!gravidar_watch_set_text((uint64_t)cursor,8,out,sizeof(out)) && !gravidar_watch_row_text((uint64_t)cursor+2,out,sizeof(out)));
    assert(!gravidar_watch_set_text((uint64_t)cursor,2,out,40)); /* never a truncated set */
    /* Unmapped contacts counted per mapped set: row 1 scan+applied, row 2 old+scan+applied. */
    /* Ring: rows older than the ring report evicted. */
    for(unsigned i=0;i<GRAVIDAR_WATCH_RING;i++) gravidar_watch_record(0x9fb3b,0);
    assert(gravidar_watch_row_text((uint64_t)cursor,out,sizeof(out)) && strstr(out,"evicted=1"));
    assert(!gravidar_watch_set_text((uint64_t)cursor,2,out,sizeof(out)));
    assert(gravidar_watch_failures==(LONG64)GRAVIDAR_WATCH_RING);
    assert(gravidar_watch_status(out,sizeof(out)) && strstr(out,"watched=8 ") && strstr(out," unmapped=5"));
    DeleteCriticalSection((CRITICAL_SECTION*)(test_gw_unit+0x18));DeleteCriticalSection((CRITICAL_SECTION*)(test_gw_other+0x18));
    gravidar_watch_count=0;gravidar_watch_failures=0;gravidar_watch_unmapped=0;
    world_identity_sealed=saved_sealed;stats.issued=saved_issued;ui_thread=saved_ui;
}
