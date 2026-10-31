/* native_gate_decimal: stubs, entry switch with frozen threads, check, restore and arm
 * refusals on the synthetic image, the reader adapters; with X4.exe: the pins, and both
 * readers executed in the relocated image before (same outcome, values within the
 * originals' error), during (bits of portable_decimal.c) and after (original bits). */
static void test_decimal(void) {
    uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;int saved_boot=boot_mode,saved_phase=boot_phase;
    unsigned char *saved_page=pdec_page;
    static const unsigned char prologue[5]={0x48,0x8b,0xc4,0x48,0x89};
    for(unsigned i=0;i<PDEC_COUNT;i++) {memcpy(image+pdec_sites[i].rva,prologue,5);memcpy(pdec_original[i],prologue,5);}
    pdec_page=pdec_alloc_page();assert(pdec_page);
    DWORD old,ignored;assert(VirtualProtect(pdec_page,0x1000,PAGE_READWRITE,&old));
    for(unsigned i=0;i<PDEC_COUNT;i++) pdec_stub_bytes(i,pdec_page+16*i);
    assert(VirtualProtect(pdec_page,0x1000,PAGE_EXECUTE_READ,&ignored) && pdec_page_ok());
    assert(pdec_state(0)==0 && pdec_state(1)==0 && pdec_switch(1)==1 && pdec_state(0)==1 && pdec_state(1)==1);
    pdec_installed=1;pdec_failures=0;assert(pdec_owned() && pdec_check());
    image[pdec_sites[1].rva+3]^=1;assert(!pdec_owned() && !pdec_check() && !strcmp(pdec_stage,"site_changed"));
    image[pdec_sites[1].rva+3]^=1;pdec_failures=0;
    char out[256];assert(pdec_status(out,sizeof(out))>0 && strstr(out,"installed=1 readers=2 "));
    pdec_restore();assert(!pdec_installed && pdec_restored && pdec_state(0)==0 && pdec_state(1)==0);
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    assert(!pdec_arm() && !strcmp(pdec_stage,"pins"));
    stats.issued=1;assert(!pdec_arm() && !strcmp(pdec_stage,"state"));stats.issued=0;
    /* A selected-Wait manifest (stock reader bits) and this module refuse each other. */
    unsigned saved_started=world_wait_started;world_wait_started=1;
    assert(!pdec_arm() && !strcmp(pdec_stage,"world_wait_manifest") && !pdec_active());world_wait_started=saved_started;
    InterlockedExchange(&pdec_any,1);assert(pdec_active());InterlockedExchange(&pdec_any,0);
    /* The adapters: separator ';', fallback from the fifth argument, correctly rounded. */
    assert(pdec_read64("0.1;2",NULL,NULL,NULL,7.0)==0.1 && pdec_read64("x",NULL,NULL,NULL,7.0)==7.0 && pdec_read64("#",NULL,NULL,NULL,7.0)==0.0);
    assert(pdec_read32("0.1",NULL,NULL,NULL,7.0f)==0.1f && pdec_read32("1e#",NULL,NULL,NULL,7.0f)==1.0f && pdec_read32("1.5e1",NULL,NULL,NULL,0.0f)==15.0f);
    for(unsigned i=0;i<PDEC_COUNT;i++) memset(image+pdec_sites[i].rva,0,5);
    VirtualFree(pdec_page,0,MEM_RELEASE);pdec_page=saved_page;pdec_restored=0;pdec_stage="idle";pdec_busy=0;
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("decimal readers stubs/switch/check/restore/arm refusals (incl. a selected-Wait manifest)/adapters PASS\n");
}
typedef double (*test_pdec_r64)(const char *,void *,void *,void *,double);
typedef float (*test_pdec_r32)(const char *,void *,void *,void *,float);
/* The live failure (fp-dc, Play17L): the probe arms native_gate_decimal, then
 * native_gate_ai_wait_rng, whose ai_wait_arm checks the world Wait pins (0x14c3090 hashed
 * whole) before its entry CAS. On the relocated X4.exe: both arming orders succeed, both
 * stay owned, and both restore. */
static void test_decimal_ai_wait_order(const char *path) {
    unsigned char *prior=image,*saved_page=pdec_page,*saved_thunk=thunk;image=test_pmath_map(path);pdec_page=NULL;
    uint32_t saved_active=stats.active;uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;
    int saved_boot=boot_mode,saved_phase=boot_phase,saved_identity=world_identity_installed;
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    thunk=make_thunk();assert(thunk && world_wait_thunk_matches() && world_wait_pins() && pdec_pins());
    for(unsigned order=0;order<2;order++) {
        ai_wait_armed=0;ai_wait_shared=0;ai_wait_restored=0;InterlockedExchange(&ai_wait_active,0);
        world_wait_installed=0;world_wait_restoring=0;world_wait_enabled=0;
        if(!order) {assert(pdec_arm() && pdec_installed);assert(world_wait_pins() && ai_wait_arm());}
        else {assert(ai_wait_arm() && world_wait_installed);assert(pdec_arm() && pdec_installed);}
        /* (world_wait_pins covers the Wait entry 0x10a50d0 itself: it holds only before that entry is installed) */
        assert(ai_wait_armed && ai_wait_owned() && ai_wait_entry_owned() && pdec_owned() && pdec_check() && pdec_pins());
        assert(image[WORLD_WAIT_ENTRY]==0xe9 && image[pdec_sites[0].rva]==0xe9);
        world_wait_restore();ai_wait_restore();pdec_restore();
        assert(!world_wait_installed && ai_wait_restored && !pdec_installed && pdec_restored && world_wait_pins() && pdec_pins());
        pdec_restored=0;pdec_stage="idle";
    }
    ai_wait_armed=ai_wait_restored=ai_wait_shared=0;InterlockedExchange(&ai_wait_active,0);world_wait_installed=0;world_wait_restoring=0;
    VirtualFree(thunk,0,MEM_RELEASE);thunk=saved_thunk;
    VirtualFree(pdec_page,0,MEM_RELEASE);pdec_page=saved_page;
    stats.active=saved_active;stats.issued=saved_issued;ui_thread=saved_ui;boot_mode=saved_boot;boot_phase=saved_phase;
    world_identity_installed=saved_identity;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("decimal + AI Wait RNG on X4.exe: decimal then ai_wait_rng and ai_wait_rng then decimal both arm, stay owned and restore");
}
static void test_decimal_native_image(const char *path) {
    unsigned char *prior=image,*saved_page=pdec_page;image=test_pmath_map(path);pdec_page=NULL;
    assert(pdec_pins());
    for(unsigned i=0;i<PDEC_COUNT;i++) memcpy(pdec_original[i],image+pdec_sites[i].rva,5);
    static const char *texts[]={"0.1","123.456","-7.25","1e5","1.5E-3","3.4028235e38","0.30000000000000004","1 2.5","-.5;9",
        "1/note\n5","#","5#","1e%101","2.5e-300","","x","1..2","1e;","9007199254740993","0.000001234567890123456789"};
    const unsigned n=sizeof(texts)/sizeof(*texts);
    test_pdec_r64 r64=(test_pdec_r64)(void*)(image+pdec_sites[0].rva);test_pdec_r32 r32=(test_pdec_r32)(void*)(image+pdec_sites[1].rva);
    double native64[32],ours64[32];float native32[32],ours32[32];
    for(unsigned i=0;i<n;i++) {native64[i]=r64(texts[i],NULL,NULL,NULL,-99.0);native32[i]=r32(texts[i],NULL,NULL,NULL,-99.0f);}
    for(unsigned i=0;i<n;i++) {
        double want=x4pd_parse64(texts[i],';',NULL,NULL,-99.0);float want32=x4pd_parse32(texts[i],';',NULL,NULL,-99.0f);
        assert((native64[i]==-99.0)==(want==-99.0) && (native32[i]==-99.0f)==(want32==-99.0f));
        assert(fabs(native64[i]-want)<=1e-9*fabs(want) && fabs((double)native32[i]-(double)want32)<=1e-6*fabs((double)want32));
    }
    pdec_page=pdec_alloc_page();assert(pdec_page);
    DWORD old,ignored;assert(VirtualProtect(pdec_page,0x1000,PAGE_READWRITE,&old));
    for(unsigned i=0;i<PDEC_COUNT;i++) pdec_stub_bytes(i,pdec_page+16*i);
    assert(VirtualProtect(pdec_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    /* Probe order: the decimal module arms before native_gate_world_wait / ai_wait_rng,
     * whose pins hash 0x14c3090 whole. They must pass before and while it is installed. */
    assert(world_wait_pin_list[3].rva==pdec_sites[0].rva && hash_ok(image+0x14c3090,1204,world_wait_pin_list[3].sha));
    assert(pdec_switch(1)==1);pdec_installed=1;assert(pdec_owned() && pdec_pins());
    assert(image[pdec_sites[0].rva]==0xe9 && image[pdec_sites[1].rva]==0xe9);              /* raw: our jumps */
    assert(hash_ok(image+0x14c3090,1204,world_wait_pin_list[3].sha));                       /* viewed: original */
    {   unsigned char viewed[1204];assert(read_local(image+0x14c3090,viewed,sizeof(viewed)) && !memcmp(viewed,pdec_original[0],5));
        unsigned char part[3];assert(read_local(image+pdec_sites[1].rva+2,part,3) && !memcmp(part,pdec_original[1]+2,3));
        unsigned char around[8];assert(sample_read(image+pdec_sites[1].rva-1,around,8) && !memcmp(around+1,pdec_original[1],5));
        unsigned char before=image[pdec_sites[1].rva-1];assert(around[0]==before);
        /* a foreign change is not hidden */
        unsigned char *e=image+pdec_sites[0].rva+1;DWORD o,i2;assert(VirtualProtect(e,1,PAGE_EXECUTE_READWRITE,&o));
        *e^=0x40;assert(!hash_ok(image+0x14c3090,1204,world_wait_pin_list[3].sha) && !pdec_owned());*e^=0x40;
        assert(VirtualProtect(e,1,o,&i2) && pdec_owned());
    }
    for(unsigned i=0;i<n;i++) {ours64[i]=r64(texts[i],NULL,NULL,NULL,-99.0);ours32[i]=r32(texts[i],NULL,NULL,NULL,-99.0f);}
    unsigned differ=0;
    for(unsigned i=0;i<n;i++) {
        double want=x4pd_parse64(texts[i],';',NULL,NULL,-99.0);float want32=x4pd_parse32(texts[i],';',NULL,NULL,-99.0f);
        assert(!memcmp(&ours64[i],&want,8) && !memcmp(&ours32[i],&want32,4));
        differ+=memcmp(&ours64[i],&native64[i],8)!=0 || memcmp(&ours32[i],&native32[i],4)!=0;
    }
    pdec_restore();assert(!pdec_installed && pdec_pins() && !InterlockedCompareExchange(&pdec_any,0,0));
    assert(hash_ok(image+0x14c3090,1204,world_wait_pin_list[3].sha));
    for(unsigned i=0;i<n;i++) {double a=r64(texts[i],NULL,NULL,NULL,-99.0);float b=r32(texts[i],NULL,NULL,NULL,-99.0f);
        assert(!memcmp(&a,&native64[i],8) && !memcmp(&b,&native32[i],4));}
    VirtualFree(pdec_page,0,MEM_RELEASE);pdec_page=saved_page;pdec_restored=0;pdec_stage="idle";
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("decimal readers: %u texts read natively, redirected (correctly rounded; %u differ from the native bits) and restored; "
           "world Wait's pin of 0x14c3090 passes through the original-byte view while installed\n",n,differ);
    test_decimal_ai_wait_order(path);
}
