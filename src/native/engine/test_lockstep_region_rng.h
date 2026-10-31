/* Offline tests for region updates on per-region streams (native_gate_region_rng):
 * portable keys independent of the natural word, restore (also on unwind),
 * ordinals, observe mode, pass rows, unmapped/thread/nested pass-through,
 * failure paths, the vtable slot CAS, arming refusals and bounded status/rows.
 * Synthetic regions and DamageFields live in owned memory; the stand-in Move
 * runs a pass like DamageField slot 8: last = now, due = now + 1 s + a jitter
 * from the current TLS word. */
static unsigned char test_rgn_region[0x300],test_rgn_other[0x40];
static unsigned char test_rgn_fields[REGION_RNG_FIELD_CAP+2][0x1100];
static uintptr_t test_rgn_vector[REGION_RNG_FIELD_CAP+3];
static uint64_t test_rgn_seen;
static double test_rgn_now=10.0;
static unsigned test_rgn_calls,test_rgn_extra;
static int test_rgn_throw;
static void test_rgn_move(void *self) {
    unsigned char *region=self;test_rgn_seen=*test_sr_word;++test_rgn_calls;
    uintptr_t *v=*(uintptr_t**)(region+REGION_RNG_FIELDS),*e=*(uintptr_t**)(region+REGION_RNG_FIELDS+8);
    for(;v<e;v++) {
        unsigned char *field=(unsigned char*)*v;double due;
        if(*(void**)field!=image+REGION_RNG_FIELD_VTABLE) {test_sr_draw(1);continue;}
        memcpy(&due,field+REGION_RNG_DUE,8);if(!(test_rgn_now>due)) continue;
        test_sr_draw(1);double jitter=(double)(*test_sr_word>>40)*(0.1/16777216.0),next=test_rgn_now+1.0+jitter;
        memcpy(field+REGION_RNG_LAST,&test_rgn_now,8);memcpy(field+REGION_RNG_DUE,&next,8);
    }
    test_sr_draw(test_rgn_extra);
    if(test_rgn_throw) RaiseException(0xe0524547,0,0,NULL);
}
static void test_rgn_noop(void *self) {(void)self;++test_rgn_calls;}
static void test_rgn_call(uint64_t *word) {test_sr_word=word;region_rng_run(test_rgn_move,word,test_rgn_region);}
static void test_rgn_due(unsigned field,double due) {memcpy(test_rgn_fields[field]+REGION_RNG_DUE,&due,8);}
static double test_rgn_get(unsigned field,unsigned offset) {double v;memcpy(&v,test_rgn_fields[field]+offset,8);return v;}
/* fields: count of vector entries; entry 1 is a foreign field type. */
static void test_rgn_layout(unsigned fields) {
    memset(test_rgn_region,0,sizeof(test_rgn_region));memset(test_rgn_fields,0,sizeof(test_rgn_fields));
    *(uint64_t*)(test_rgn_region+8)=0x9001;*(void**)test_rgn_other=image+0x2c4b108; /* ResourceField */
    for(unsigned i=0;i<fields;i++) {
        test_rgn_vector[i]=(uintptr_t)(i==1?test_rgn_other:test_rgn_fields[i]);
        *(void**)test_rgn_fields[i]=image+REGION_RNG_FIELD_VTABLE;test_rgn_due(i,9.9);
    }
    *(uintptr_t**)(test_rgn_region+REGION_RNG_FIELDS)=test_rgn_vector;*(uintptr_t**)(test_rgn_region+REGION_RNG_FIELDS+8)=test_rgn_vector+fields;
}
static void test_rgn_reset(void) {
    memset(region_rng_slots,0,sizeof(region_rng_slots));memset(region_rng_ring,0,sizeof(region_rng_ring));
    memset(&region_rng_now,0,sizeof(region_rng_now));memset(&region_rng_last,0,sizeof(region_rng_last));
    region_rng_epoch=0;region_rng_epoch_tick=UINT64_MAX;region_rng_draws=0;region_rng_depth=0;
    region_rng_calls=region_rng_scoped=region_rng_keyed=region_rng_passed=region_rng_threads=region_rng_nested=0;
    region_rng_unmapped=region_rng_table_full=region_rng_fields_seen=region_rng_field_overflow=region_rng_passes=0;
    region_rng_draws_unknown=region_rng_cursor=region_rng_failures=0;test_rgn_calls=0;test_rgn_extra=0;test_rgn_throw=0;test_rgn_now=10.0;
    script_rng_cache_flush();
}
static void test_region_rng(void) {
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x9001,0x7ce));
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=7;ui_thread=GetCurrentThreadId();
    region_rng_armed=1;region_rng_mode=REGION_RNG_KEY;InterlockedExchange(&region_rng_active,1);test_rgn_reset();test_rgn_layout(3);
    uint64_t k1=lockstep_seed(REGION_RNG_DOMAIN,7,0x7ce,0,1,0);

    /* Keyed by tick and saved region ID: the natural word never reaches the
     * fields and comes back unchanged; one row per DamageField pass. */
    uint64_t word=0x1111;test_rgn_call(&word);
    assert(test_rgn_seen==k1 && word==0x1111 && region_rng_keyed==1 && region_rng_scoped==1 && region_rng_passes==2 && !region_rng_failures);
    assert(region_rng_cursor==2 && region_rng_fields_seen==2 && region_rng_draws==3 && region_rng_now.passes==2 && region_rng_now.keyed==1);
    region_rng_row_t *r=&region_rng_ring[0];double due0=test_rgn_get(0,REGION_RNG_DUE),due2=test_rgn_get(2,REGION_RNG_DUE);
    uint64_t bits;memcpy(&bits,&due0,8);
    assert(r->tick==7 && r->region==0x7ce && r->field==0 && r->ordinal==1 && r->key==k1 && r->word==k1 && r->seeded==1 && r->mapped==1 &&
           r->draws==3 && r->due==bits && region_rng_ring[1].field==2 && due0>=11.0 && due0<11.1 && test_rgn_get(0,REGION_RNG_LAST)==10.0);
    double prior=9.9;memcpy(&bits,&prior,8);assert(r->prior==bits);
    /* Another process: another natural word, the same pass times and sums. */
    uint64_t dueset=region_rng_now.dueset,keyset=region_rng_now.keyset;
    test_rgn_reset();test_rgn_layout(3);word=0xdeadbeef;test_rgn_call(&word);
    assert(test_rgn_seen==k1 && word==0xdeadbeef && test_rgn_get(0,REGION_RNG_DUE)==due0 && test_rgn_get(2,REGION_RNG_DUE)==due2);
    assert(region_rng_now.dueset==dueset && region_rng_now.keyset==keyset);
    /* No due field: no row, the scope still keys and restores. */
    test_rgn_call(&word);assert(test_rgn_seen==lockstep_seed(REGION_RNG_DOMAIN,7,0x7ce,0,2,0) && region_rng_cursor==2 && word==0xdeadbeef);
    /* A new tick starts new ranks and rolls the tally. */
    stats.issued=8;test_rgn_now=11.5;test_rgn_call(&word);
    assert(test_rgn_seen==lockstep_seed(REGION_RNG_DOMAIN,8,0x7ce,0,1,0) && region_rng_last.tick==7 && region_rng_last.calls==2 &&
           region_rng_now.tick==8 && region_rng_now.calls==1);
    stats.issued=7;

    /* Observe: same identities and rows, the natural word drives the pass. */
    test_rgn_reset();test_rgn_layout(3);region_rng_mode=REGION_RNG_OBSERVE;word=0x1111;test_rgn_call(&word);
    uint64_t natural=0x1111;for(unsigned i=0;i<3;i++) natural=test_sr_step(natural);
    assert(test_rgn_seen==0x1111 && word==natural && !region_rng_keyed && region_rng_scoped==1 && region_rng_cursor==2);
    assert(region_rng_ring[0].seeded==0 && region_rng_ring[0].key==k1 && region_rng_ring[0].word==0x1111 &&
           region_rng_ring[0].exit==natural && region_rng_ring[0].draws==3 && !region_rng_now.keyset);
    region_rng_mode=REGION_RNG_KEY;

    /* Unmapped region: natural stream, counted, never a fabricated key. */
    test_rgn_reset();test_rgn_layout(3);*(uint64_t*)(test_rgn_region+8)=0x9999;word=0x2222;test_rgn_call(&word);
    assert(test_rgn_seen==0x2222 && region_rng_unmapped==1 && !region_rng_scoped && !region_rng_failures &&
           region_rng_ring[0].mapped==0 && region_rng_ring[0].region==0 && region_rng_ring[0].ordinal==0);
    *(uint64_t*)(test_rgn_region+8)=0x9001;
    /* Pass-through: inactive, stopped gate, boot phase 2, nested; another thread is counted. */
    test_rgn_reset();test_rgn_layout(3);word=5;InterlockedExchange(&region_rng_active,0);test_rgn_call(&word);
    assert(test_rgn_seen==5 && region_rng_passed==1);InterlockedExchange(&region_rng_active,1);
    stats.active=0;word=5;test_rgn_call(&word);assert(test_rgn_seen==5 && region_rng_passed==2);stats.active=1;
    boot_phase=2;word=5;test_rgn_call(&word);assert(test_rgn_seen==5 && region_rng_passed==3);boot_phase=3;
    region_rng_depth=1;word=5;test_rgn_call(&word);assert(test_rgn_seen==5 && region_rng_nested==1);region_rng_depth=0;
    ui_thread=GetCurrentThreadId()+1;word=5;test_rgn_call(&word);assert(test_rgn_seen==5 && region_rng_threads==1 && !region_rng_failures);
    ui_thread=GetCurrentThreadId();assert(!region_rng_scoped && !region_rng_cursor);

    /* Failures run the original natively and are counted (reason 93). */
    test_rgn_reset();test_rgn_layout(3);word=5;test_sr_word=&word;region_rng_run(test_rgn_noop,NULL,test_rgn_region);
    assert(test_rgn_calls==1 && region_rng_failures==1);
    *(uint64_t*)(test_rgn_region+8)=0;region_rng_run(test_rgn_noop,&word,test_rgn_region);
    assert(test_rgn_calls==2 && region_rng_failures==2 && word==5);*(uint64_t*)(test_rgn_region+8)=0x9001;
    uintptr_t *end=(uintptr_t*)(test_rgn_region+REGION_RNG_FIELDS+8);
    *end=(uintptr_t)(test_rgn_vector-1);region_rng_run(test_rgn_noop,&word,test_rgn_region);assert(region_rng_failures==3);
    *end=(uintptr_t)(test_rgn_vector+REGION_RNG_FIELD_MAX+1);region_rng_run(test_rgn_noop,&word,test_rgn_region);assert(region_rng_failures==4);
    *end=(uintptr_t)test_rgn_vector+4;region_rng_run(test_rgn_noop,&word,test_rgn_region);assert(region_rng_failures==5);
    test_rgn_layout(3);test_rgn_vector[1]=0;region_rng_run(test_rgn_noop,&word,test_rgn_region);assert(region_rng_failures==6);
    test_rgn_layout(3);assert(test_rgn_calls==6 && word==5 && !region_rng_scoped && !region_rng_cursor);
    for(unsigned i=0;i<REGION_RNG_TABLE;i++) {region_rng_slots[i].epoch=1;region_rng_slots[i].saved=~(uint64_t)i;}
    region_rng_epoch=1;region_rng_epoch_tick=7;word=5;test_rgn_call(&word);
    assert(test_rgn_seen==5 && region_rng_table_full==1 && region_rng_failures==7 && !region_rng_scoped);
    /* Unwinding restores the word and the depth and counts a failure. */
    test_rgn_reset();test_rgn_layout(3);word=0x99;test_rgn_throw=1;int caught=0;
    __try {test_rgn_call(&word);}
    __except(GetExceptionCode()==0xe0524547?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && word==0x99 && test_rgn_seen==k1 && region_rng_failures==1 && !region_rng_depth && !region_rng_cursor);
    test_rgn_throw=0;
    /* Beyond the generator-step cap: rows say -1 and the call is counted once. */
    test_rgn_reset();test_rgn_layout(3);test_rgn_extra=SCRIPT_RNG_STEP_CAP+1;word=0x1111;test_rgn_call(&word);
    assert(word==0x1111 && region_rng_draws_unknown==1 && !region_rng_draws && region_rng_cursor==2 &&
           region_rng_ring[0].draws==UINT32_MAX && region_rng_ring[1].draws==UINT32_MAX && !region_rng_failures);
    /* More DamageFields than the cap: the first eight are followed, the rest counted. */
    test_rgn_reset();test_rgn_layout(REGION_RNG_FIELD_CAP+2);word=0x1111;test_rgn_call(&word);
    assert(region_rng_field_overflow==1 && region_rng_fields_seen==REGION_RNG_FIELD_CAP && region_rng_cursor==REGION_RNG_FIELD_CAP &&
           !region_rng_failures && word==0x1111);

    /* Rows: format, eviction marker, end, worst width. */
    test_rgn_reset();test_rgn_layout(3);word=0x1111;test_rgn_call(&word);
    char row[512];assert(region_rng_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=7 region=00000000000007ce field=0 ord=1 mapped=1 seeded=1 ") &&
        strstr(row," draws=3 ") && strstr(row," prior="));
    printf("NATIVE_GATE REGION_RNG_ROW %s\n",row);assert(!region_rng_row(2,row,sizeof(row)));
    region_rng_cursor=REGION_RNG_RING+3;assert(region_rng_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    region_rng_ring[2].draws=UINT32_MAX;assert(region_rng_row(REGION_RNG_RING+2,row,sizeof(row))>0 && strstr(row," draws=-1 "));
    for(unsigned i=0;i<REGION_RNG_RING;i++) {
        region_rng_row_t *e=&region_rng_ring[i];e->tick=e->region=e->ordinal=e->key=e->word=e->exit=e->last=e->due=e->prior=UINT64_MAX;
        e->draws=UINT32_MAX-1;e->field=UINT32_MAX;e->seeded=e->mapped=255;
    }
    region_rng_cursor=INT64_MAX;int n=region_rng_row((uint64_t)INT64_MAX-1,row,sizeof(row));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE REGION_RNG_ROW \n")<sizeof(row));

    /* Slot CAS: only our exact pointer is restored. Arming refuses before any
     * write while the pins fail (placeholder image) or a precondition is missing. */
    test_rgn_reset();region_rng_armed=0;region_rng_installed=0;InterlockedExchange(&region_rng_active,0);
    void **slot=(void**)(image+REGION_RNG_SLOT);*slot=image+REGION_RNG_MOVE;
    boot_phase=0;stats.issued=0;world_identity_installed=1;
    assert(!region_rng_arm(REGION_RNG_KEY) && *slot==image+REGION_RNG_MOVE && !region_rng_armed && !region_rng_active);
    world_identity_installed=0;assert(!region_rng_arm(REGION_RNG_KEY));world_identity_installed=1;
    stats.active=0;assert(!region_rng_arm(REGION_RNG_KEY));stats.active=1;
    boot_phase=2;assert(!region_rng_arm(REGION_RNG_KEY));boot_phase=0;
    stats.issued=1;assert(!region_rng_arm(REGION_RNG_KEY));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!region_rng_arm(REGION_RNG_KEY));ui_thread=GetCurrentThreadId();
    assert(*slot==image+REGION_RNG_MOVE);
    assert(script_rng_slot_swap(REGION_RNG_SLOT,image+REGION_RNG_MOVE,(void*)region_rng_move));
    region_rng_installed=region_rng_armed=1;region_rng_mode=REGION_RNG_KEY;InterlockedExchange(&region_rng_active,1);
    assert(region_rng_owned() && region_rng_arm(REGION_RNG_KEY) && !region_rng_arm(REGION_RNG_OBSERVE));
    *slot=image+1;assert(!region_rng_owned());
    region_rng_restore();assert(!region_rng_restored && region_rng_installed && !region_rng_active && *slot==image+1);
    *slot=(void*)region_rng_move;region_rng_restore();
    assert(region_rng_restored && !region_rng_installed && *slot==image+REGION_RNG_MOVE && region_rng_owned());
    /* A stopped hook passes through. */
    boot_phase=3;word=5;test_rgn_layout(3);test_rgn_call(&word);assert(test_rgn_seen==5 && region_rng_passed==1);
    *slot=NULL;world_identity_installed=0;

    /* Bounded status. */
    char status[1536];assert(region_rng_status(status,sizeof(status))>0 &&
        strstr(status,"armed=1 installed=0 active=0 mode=1 restored=1 calls=1 scoped=0 keyed=0 passed=1 "));
    printf("NATIVE_GATE REGION_RNG tick=1 %s\n",status);
    region_rng_armed=region_rng_installed=region_rng_restored=region_rng_mode=INT32_MIN;
    InterlockedExchange(&region_rng_active,(LONG)(-2147483647-1));
    region_rng_calls=region_rng_scoped=region_rng_keyed=region_rng_passed=region_rng_threads=region_rng_nested=INT64_MAX;
    region_rng_unmapped=region_rng_table_full=region_rng_fields_seen=region_rng_field_overflow=region_rng_passes=INT64_MAX;
    region_rng_draws_unknown=region_rng_cursor=region_rng_failures=INT64_MAX;region_rng_draws=UINT64_MAX;
    memset(&region_rng_now,0xff,sizeof(region_rng_now));memset(&region_rng_last,0xff,sizeof(region_rng_last));
    n=region_rng_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE REGION_RNG tick=18446744073709551615 \n")<sizeof(status));
    region_rng_armed=region_rng_installed=region_rng_restored=0;region_rng_mode=REGION_RNG_OBSERVE;
    InterlockedExchange(&region_rng_active,0);test_rgn_reset();
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;script_rng_cache_flush();
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native region update key/restore/ordinal/observe/pass rows/slot CAS tests PASS");
}
/* With the X4 9.00 executable: both pinned bodies, the two relocated vtable
 * slots, the field-vector walk of Region::Move, the DamageField time fields
 * and its TLS jitter draw, tamper refusals, and a full arm/restore on the copy. */
static void test_region_rng_native_image(const char *path) {
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
    /* The file holds preferred-base pointers; apply the two relocations the loader would. */
    assert(*(uint64_t*)(image+REGION_RNG_SLOT)==nt.OptionalHeader.ImageBase+REGION_RNG_MOVE);
    assert(*(uint64_t*)(image+REGION_RNG_FIELD_SLOT)==nt.OptionalHeader.ImageBase+REGION_RNG_FIELD_UPDATE);
    assert(*(uint64_t*)(image+REGION_RNG_FIELD_VTABLE)==nt.OptionalHeader.ImageBase+0xee15b0); /* DamageField slot 0 */
    assert(REGION_RNG_SLOT==0x2b2ad60u+674u*8u && REGION_RNG_FIELD_SLOT==REGION_RNG_FIELD_VTABLE+8u*8u);
    *(void**)(image+REGION_RNG_SLOT)=image+REGION_RNG_MOVE;*(void**)(image+REGION_RNG_FIELD_SLOT)=image+REGION_RNG_FIELD_UPDATE;
    assert(region_rng_pinned());
    /* Region::Move: field vector end/begin, then slot 8 (+0x40) of each field. */
    assert(!memcmp(image+0x767421,"\x49\x8b\xbe\x90\x02\x00\x00\x49\x8b\x9e\x88\x02\x00\x00",14));
    assert(!memcmp(image+0x767437,"\x48\x8b\x01\xff\x50\x40",6));
    /* DamageField slot 8: now > due (+0x10f8), dt from last (+0x10f0), last = now,
     * low attention: ecx=0x318 (TLS word) -> Random::FloatState 0x14ddc90, due = now + 1 + jitter. */
    assert(!memcmp(image+0xee16fe,"\x66\x0f\x2f\x87\xf8\x10\x00\x00",8) && !memcmp(image+0xee1727,"\xf2\x0f\x5c\x87\xf0\x10\x00\x00",8));
    assert(!memcmp(image+0xee1941,"\x48\x89\x87\xf0\x10\x00\x00",7) && !memcmp(image+0xee196e,"\xb9\x18\x03\x00\x00",5));
    int32_t rel;memcpy(&rel,image+0xee197f,4);assert(image[0xee197e]==0xe8 && 0xee1983+rel==0x14ddc90);
    assert(!memcmp(image+0xee1998,"\xf2\x0f\x11\x8f\xf8\x10\x00\x00",8));
    double one,tenth;float range;memcpy(&one,image+0x2cbe870,8);memcpy(&tenth,image+0x2cbe6f0,8);memcpy(&range,image+0x2cbe2a8,4);
    assert(one==1.0 && tenth==0.10000000149011612 && range==0.1f);
    const unsigned tampers[]={REGION_RNG_MOVE,0x767424,0x76742b,0x76743c,REGION_RNG_MOVE+REGION_RNG_MOVE_LENGTH-1,
                              REGION_RNG_FIELD_UPDATE,0xee1702,0xee197f,0xee199c,REGION_RNG_FIELD_UPDATE+REGION_RNG_FIELD_UPDATE_LENGTH-1};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!region_rng_pinned());image[tampers[i]]^=1;}
    *(void**)(image+REGION_RNG_SLOT)=image+REGION_RNG_MOVE+1;assert(!region_rng_pinned());
    *(void**)(image+REGION_RNG_SLOT)=image+REGION_RNG_MOVE;
    *(void**)(image+REGION_RNG_FIELD_SLOT)=image+REGION_RNG_FIELD_UPDATE+1;assert(!region_rng_pinned());
    *(void**)(image+REGION_RNG_FIELD_SLOT)=image+REGION_RNG_FIELD_UPDATE;assert(region_rng_pinned());
    /* Full arm and restore on the copy: only the slot pointer changes. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    region_rng_armed=region_rng_installed=region_rng_restored=0;
    assert(region_rng_arm(REGION_RNG_KEY) && *(void**)(image+REGION_RNG_SLOT)==(void*)region_rng_move && region_rng_owned());
    assert(region_rng_arm(REGION_RNG_KEY) && !region_rng_arm(REGION_RNG_OBSERVE));
    region_rng_restore();assert(region_rng_restored && !region_rng_installed && *(void**)(image+REGION_RNG_SLOT)==image+REGION_RNG_MOVE);
    region_rng_armed=region_rng_installed=region_rng_restored=0;InterlockedExchange(&region_rng_active,0);
    stats.active=0;boot_mode=0;world_identity_installed=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("Region::Move slot 674 / DamageField pass native pins, relocated slots, tamper refusals and arm/restore PASS");
}
