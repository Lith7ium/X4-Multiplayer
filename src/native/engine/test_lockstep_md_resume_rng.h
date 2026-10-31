/* Offline tests for load-time MD cue delays (native_gate_md_resume_rng):
 * portable keys independent of the natural word, restore (also on unwind),
 * ordinals, unnamed fallback, pass-through/foreign/failure paths, the rel32
 * CAS of the call site, arming refusals and bounded status/rows. Synthetic
 * cues live in owned memory and call bytes in the placeholder image. */
static test_sr_md test_mdr_cue2;
static unsigned char test_mdr_module[0x100];
static uint64_t test_mdr_seen,test_mdr_slot;
static unsigned test_mdr_draws=2,test_mdr_calls;
static int test_mdr_throw;
/* The evaluator: draws and maps its last word to a delay in [0, 15) s. */
static double test_mdr_original(void *cue,uint64_t slot) {
    assert(cue);test_mdr_seen=*test_sr_word;test_mdr_slot=slot;++test_mdr_calls;test_sr_draw(test_mdr_draws);
    if(test_mdr_throw) RaiseException(0xe04d5045,0,0,NULL);
    return (double)(*test_sr_word>>11)*(15.0/9007199254740992.0);
}
static double test_mdr_call(uint64_t *word,void *cue,uint64_t slot) {
    test_sr_word=word;return md_resume_run((uintptr_t)(image+MD_RESUME_SITE+5),test_mdr_original,word,cue,slot);
}
static void test_mdr_reset(void) {
    memset(md_resume_slots,0,sizeof(md_resume_slots));memset(md_resume_ring,0,sizeof(md_resume_ring));
    memset(script_rng_names,0,sizeof(script_rng_names));script_rng_name_count=0;md_resume_slots_tick=UINT64_MAX;
    md_resume_draws=md_resume_keyset=md_resume_drawset=md_resume_delayset=0;
    md_resume_calls=md_resume_keyed=md_resume_passed=md_resume_unnamed=md_resume_threads=md_resume_foreign=0;
    md_resume_table_full=md_resume_draws_unknown=md_resume_cursor=md_resume_failures=0;test_mdr_calls=0;test_mdr_throw=0;
}
static void test_md_resume(void) {
    test_sr_md_init(&test_sr_cue);test_sr_md_init(&test_mdr_cue2);
    double act1=7.0832,act2=3.5;uint64_t a1,a2;memcpy(&a1,&act1,8);memcpy(&a2,&act2,8);
    memcpy(test_sr_cue.cue+0x1f8,&act1,8);memcpy(test_mdr_cue2.cue+0x1f8,&act2,8);
    stats.active=1;boot_mode=1;boot_phase=2;stats.issued=0;ui_thread=GetCurrentThreadId();
    md_resume_armed=1;InterlockedExchange(&md_resume_active,1);test_mdr_reset();
    uint64_t owner=test_sr_fnv("Other.Cue1");

    /* Keyed by name, slot, activation and ordinal: the natural word never
     * reaches the evaluator and comes back unchanged. */
    uint64_t word=0x1111;double d1=test_mdr_call(&word,test_sr_cue.cue,0);
    uint64_t k1=lockstep_seed(MD_RESUME_DOMAIN,0,owner,0,1,a1);
    assert(test_mdr_seen==k1 && test_mdr_slot==0 && word==0x1111 && md_resume_keyed==1 && md_resume_calls==1 && !md_resume_failures);
    md_resume_row_t *r=&md_resume_ring[0];
    assert(md_resume_cursor==1 && r->owner==owner && r->ordinal==1 && r->key==k1 && r->natural==0x1111 && r->draws==2 &&
           r->activation==a1 && !r->unnamed && !r->thread && !strcmp(r->name,"Other.Cue1"));
    uint64_t bits;memcpy(&bits,&d1,8);assert(r->delay==bits && md_resume_draws==2);
    /* Another process: another natural word, the same delay. */
    test_mdr_reset();word=0xdeadbeef;double d1b=test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==k1 && d1b==d1 && word==0xdeadbeef);
    uint64_t keyset=md_resume_keyset,drawset=md_resume_drawset,delayset=md_resume_delayset;
    test_mdr_reset();word=0x1111;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(md_resume_keyset==keyset && md_resume_drawset==drawset && md_resume_delayset==delayset);
    /* The same (cue, slot, activation) again is the next rank; another
     * activation time or slot is its own rank 1. */
    double d2=test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,0,owner,0,2,a1) && d2!=d1 && md_resume_ring[1].ordinal==2);
    test_mdr_call(&word,test_sr_cue.cue,1);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,0,owner,1,1,a1) && test_mdr_slot==1);
    test_npc_string(test_mdr_cue2.node+0x20,"Cue2");test_mdr_call(&word,test_mdr_cue2.cue,0);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,0,test_sr_fnv("Other.Cue2"),0,1,a2) && word==0x1111);
    /* A new tick starts new ranks. */
    stats.issued=5;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,5,owner,0,1,a1) && md_resume_ring[4].tick==5);stats.issued=0;

    /* No name: owner 0, still keyed and counted. */
    test_mdr_reset();*(void**)(test_mdr_cue2.cue+0x150)=test_mdr_module;test_npc_string(test_mdr_module+0x18,"Other");
    *(void**)(test_mdr_module+0x80)=test_mdr_module+0x60;
    word=0x22;test_mdr_call(&word,test_mdr_cue2.cue,0);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,0,0,0,1,a2) && md_resume_unnamed==1 && !md_resume_failures &&
           md_resume_ring[0].unnamed==1 && !md_resume_ring[0].name[0] && word==0x22);
    *(void**)(test_mdr_cue2.cue+0x150)=test_mdr_cue2.module;
    /* Another thread is keyed the same way and counted. */
    ui_thread=GetCurrentThreadId()+1;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==lockstep_seed(MD_RESUME_DOMAIN,0,owner,0,1,a1) && md_resume_threads==1 && md_resume_ring[1].thread==1);
    ui_thread=GetCurrentThreadId();

    /* Unwinding restores the word and counts a failure without a row. */
    test_mdr_reset();word=0x99;test_mdr_throw=1;int caught=0;
    __try {test_mdr_call(&word,test_sr_cue.cue,0);}
    __except(GetExceptionCode()==0xe04d5045?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && word==0x99 && test_mdr_seen==k1 && md_resume_failures==1 && !md_resume_cursor);test_mdr_throw=0;

    /* Pass-through and failure paths run the evaluator on the natural word. */
    uint64_t natural=0x5;for(unsigned i=0;i<2;i++) natural=test_sr_step(natural);
    test_mdr_reset();InterlockedExchange(&md_resume_active,0);word=5;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==5 && word==natural && md_resume_passed==1 && !md_resume_failures);
    InterlockedExchange(&md_resume_active,1);stats.active=0;word=5;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==5 && md_resume_passed==2);stats.active=1;
    word=5;test_sr_word=&word;md_resume_run((uintptr_t)(image+MD_RESUME_SITE+6),test_mdr_original,&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==5 && word==natural && md_resume_foreign==1 && md_resume_failures==1);
    word=5;md_resume_run((uintptr_t)(image+MD_RESUME_SITE+5),test_mdr_original,NULL,test_sr_cue.cue,0);
    assert(test_mdr_seen==5 && md_resume_failures==2);
    *(void**)test_mdr_cue2.cue=image+SCRIPT_RNG_AI_VT;word=5;test_mdr_call(&word,test_mdr_cue2.cue,0);
    assert(test_mdr_seen==5 && md_resume_failures==3 && !md_resume_keyed);*(void**)test_mdr_cue2.cue=image+SCRIPT_RNG_CUE_VT;
    for(unsigned i=0;i<MD_RESUME_TABLE;i++) {md_resume_slots[i].used=1;md_resume_slots[i].owner=~(uint64_t)i;}
    md_resume_slots_tick=0;word=5;test_mdr_call(&word,test_sr_cue.cue,0);
    assert(test_mdr_seen==5 && md_resume_table_full==1 && md_resume_failures==4 && !md_resume_keyed);

    /* Rows: format, sanitized name, eviction marker, end. */
    test_mdr_reset();word=0x1111;test_mdr_call(&word,test_sr_cue.cue,0);
    char row[512];assert(md_resume_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=0 owner=") && strstr(row," ord=1 ") &&
        strstr(row," natural=0000000000001111 ") && strstr(row," draws=2 ") && strstr(row," name=Other.Cue1"));
    printf("NATIVE_GATE MD_RESUME_RNG_ROW %s\n",row);assert(!md_resume_row(1,row,sizeof(row)));
    md_resume_cursor=MD_RESUME_RING+3;assert(md_resume_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    memset(md_resume_ring[2].name,'x',sizeof(md_resume_ring[2].name));md_resume_ring[2].name[5]=' ';md_resume_ring[2].draws=UINT32_MAX;
    assert(md_resume_row(MD_RESUME_RING+2,row,sizeof(row))>0 && strstr(row," draws=-1 ") && strstr(row," name=xxxxx_xxx"));
    for(unsigned i=0;i<MD_RESUME_RING;i++) {
        md_resume_row_t *e=&md_resume_ring[i];e->tick=e->owner=e->slot=e->activation=e->ordinal=e->key=e->natural=e->exit=e->delay=UINT64_MAX;
        e->draws=UINT32_MAX-1;e->unnamed=e->thread=255;memset(e->name,'y',sizeof(e->name)-1);e->name[sizeof(e->name)-1]=0;
    }
    md_resume_cursor=INT64_MAX;
    int n=md_resume_row((uint64_t)INT64_MAX-1,row,sizeof(row));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE MD_RESUME_RNG_ROW \n")<sizeof(row));

    /* Call-site CAS: only the rel32 of E8 at 0x1115bad changes (it straddles
     * a 16-byte boundary); restore needs our exact value. */
    unsigned char *saved_thunk=thunk;thunk=image+0x6000000;
    unsigned char *site=image+MD_RESUME_SITE;memset(site-16,0x90,48);site[0]=0xe8;
    int32_t rel=(int32_t)((intptr_t)(image+MD_RESUME_DELAY)-(intptr_t)(site+5));memcpy(site+1,&rel,4);
    unsigned char before[48];memcpy(before,site-16,48);
    assert(md_resume_prepare() && md_resume_original_rel==rel);
    assert(md_resume_swap(md_resume_original_rel,md_resume_patched_rel));md_resume_installed=1;
    memcpy(&rel,site+1,4);assert(site[0]==0xe8 && site+5+rel==thunk+MD_RESUME_THUNK);
    for(unsigned j=0;j<48;j++) if(j<17 || j>20) assert((site-16)[j]==before[j]);
    assert(md_resume_owned() && !md_resume_swap(md_resume_original_rel,md_resume_patched_rel) && !md_resume_prepare());
    site[2]^=1;assert(!md_resume_owned());
    md_resume_restore();assert(!md_resume_restored && md_resume_installed && !md_resume_active);
    site[2]^=1;md_resume_restore();
    assert(md_resume_restored && !md_resume_installed && !memcmp(site-16,before,48) && md_resume_owned());
    /* A stopped hook passes through. */
    word=5;test_mdr_reset();test_mdr_call(&word,test_sr_cue.cue,0);assert(test_mdr_seen==5 && md_resume_passed==1);
    site[0]=0xe9;assert(!md_resume_prepare());site[0]=0xe8;
    memcpy(&rel,site+1,4);rel+=1;memcpy(site+1,&rel,4);assert(!md_resume_prepare());rel-=1;memcpy(site+1,&rel,4);
    thunk=saved_thunk;

    /* Arming refusals precede any write (pins fail on the placeholder). */
    md_resume_armed=md_resume_restored=0;boot_phase=0;stats.issued=0;thunk=image+0x6000000;
    assert(!md_resume_arm() && !md_resume_installed && !memcmp(site-16,before,48));
    stats.active=0;assert(!md_resume_arm());stats.active=1;boot_phase=2;assert(!md_resume_arm());
    assert(!md_resume_armed && !md_resume_active);thunk=saved_thunk;

    /* Bounded status. */
    char status[768];assert(md_resume_status(status,sizeof(status))>0 && strstr(status,"armed=0 installed=0 active=0 restored=0 calls=1 keyed=0 passed=1 "));
    printf("NATIVE_GATE MD_RESUME_RNG tick=1 %s\n",status);
    md_resume_armed=md_resume_installed=md_resume_restored=INT32_MIN;InterlockedExchange(&md_resume_active,(LONG)(-2147483647-1));
    md_resume_calls=md_resume_keyed=md_resume_passed=md_resume_unnamed=md_resume_threads=md_resume_foreign=INT64_MAX;
    md_resume_table_full=md_resume_draws_unknown=md_resume_failures=INT64_MAX;md_resume_cursor=INT64_MAX;
    md_resume_draws=md_resume_keyset=md_resume_drawset=md_resume_delayset=UINT64_MAX;
    n=md_resume_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE MD_RESUME_RNG tick=18446744073709551615 \n")<sizeof(status));
    md_resume_armed=md_resume_installed=md_resume_restored=0;InterlockedExchange(&md_resume_active,0);test_mdr_reset();
    memset(site-16,0,48);
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native MD load-time delay key/restore/ordinal/fallback/CAS tests PASS");
}
/* With the X4 9.00 executable: every pin, the E8 at 0x1115bad to the delay
 * evaluator after mov edx,[rbx+0x200]; mov rcx,rbx, and tamper refusals. */
static void test_mdr_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image,*prior_thunk=thunk;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(image);
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
    assert(md_resume_pinned() && md_resume_prepare());
    int32_t rel;memcpy(&rel,image+MD_RESUME_SITE+1,4);
    assert(image[MD_RESUME_SITE]==0xe8 && MD_RESUME_SITE+5+rel==MD_RESUME_DELAY && rel==md_resume_original_rel);
    assert(!memcmp(image+MD_RESUME_SITE-9,"\x8b\x93\x00\x02\x00\x00\x48\x8b\xcb",9)); /* mov edx,[rbx+0x200]; mov rcx,rbx */
    assert(((MD_RESUME_SITE+1)&15)>12 && ((MD_RESUME_SITE+1)&63)<=60); /* straddles 16 bytes, inside one line */
    const unsigned tampers[]={MD_RESUME_CALLER,MD_RESUME_SITE,MD_RESUME_SITE+4,0x1115bbc,MD_RESUME_DELAY,0x111f950,0x1111a40,0x1118b30};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!md_resume_pinned());image[tampers[i]]^=1;}
    assert(md_resume_pinned());md_resume_original_rel=md_resume_patched_rel=0;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;thunk=prior_thunk;
    puts("MD load-time delay call site native pins, call target and tamper refusals PASS");
}
