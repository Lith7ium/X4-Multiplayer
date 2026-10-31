/* native_gate_build_rng: keyed UpdateBuildEvent reschedule draw (storage key and game time, thread word
 * untouched, loading frames keyed too), fallback/unmapped, trampoline, site CAS/check/restore, the pinned build. */
static uint64_t test_brng_seen;
static float test_brng_draw(uint64_t *word,float range) {test_brng_seen=*word;*word^=0x5555;return range*0.25f;}
static void test_build_rng(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7c001,0x43cd9) && world_identity_record(0x7c002,0x43cda));
    static unsigned char storage[0x40],tls[0x320];memset(storage,0,sizeof(storage));memset(tls,0,sizeof(tls));
    void *vt=image+0x2aaca18;memcpy(storage,&vt,8);*(uint64_t*)(storage+8)=0x7c001;
    uint64_t *word=(uint64_t*)(tls+0x318);double now=7.083;memcpy(tls+0x300,&now,8);uint64_t bits;memcpy(&bits,&now,8);
    brng_keyed=brng_loading=brng_native=brng_unmapped=brng_fallback=0;
    /* Loading (boot phase 2): the draw runs on (storage key, game time); the thread's word is neither read nor advanced. */
    stats.active=1;boot_phase=2;stats.issued=0;*word=0x1111;
    float r=brng_run(test_brng_draw,word,10.0f,storage);
    assert(r==2.5f && *word==0x1111 && test_brng_seen==lockstep_seed(BRNG_DOMAIN,0,0x43cd9,bits,0,WORLD_KEY_SAVED) && brng_keyed==1 && brng_loading==1);
    *word=0x9999;brng_run(test_brng_draw,word,10.0f,storage);
    assert(*word==0x9999 && test_brng_seen==lockstep_seed(BRNG_DOMAIN,0,0x43cd9,bits,0,WORLD_KEY_SAVED)); /* independent of the word */
    *(uint64_t*)(storage+8)=0x7c002;brng_run(test_brng_draw,word,10.0f,storage);
    assert(test_brng_seen==lockstep_seed(BRNG_DOMAIN,0,0x43cda,bits,0,WORLD_KEY_SAVED));
    /* Admitted frame, later game time: another stream for the same storage. */
    boot_phase=3;stats.issued=3601;now=67.1;memcpy(tls+0x300,&now,8);memcpy(&bits,&now,8);*(uint64_t*)(storage+8)=0x7c001;
    brng_run(test_brng_draw,word,10.0f,storage);
    assert(test_brng_seen==lockstep_seed(BRNG_DOMAIN,0,0x43cd9,bits,0,WORLD_KEY_SAVED) && brng_keyed==4 && brng_loading==3);
    /* No portable key: (class vtable, game time); no vtable in the image, no storage or no word: the thread's word. */
    *(uint64_t*)(storage+8)=0x9898;*word=0x4444;brng_run(test_brng_draw,word,10.0f,storage);
    assert(test_brng_seen==lockstep_seed(BRNG_DOMAIN,0,0,0x2aaca18,bits,WORLD_KEY_NONE) && *word==0x4444 && brng_fallback==1 && !brng_unmapped);
    memset(storage,0,8);*word=0x4444;brng_run(test_brng_draw,word,10.0f,storage);
    assert(test_brng_seen==0x4444 && *word==(0x4444^0x5555) && brng_unmapped==1);
    *word=0x4444;brng_run(test_brng_draw,word,10.0f,NULL);assert(test_brng_seen==0x4444 && brng_unmapped==2);
    /* The trampoline: mov r8, rdi; jmp [rip]; brng_draw. */
    unsigned char t[17];brng_write_trampoline(t);
    assert(t[0]==0x4c && t[1]==0x8b && t[2]==0xc7 && t[3]==0xff && t[4]==0x25 && !t[5] && !t[8] && *(void**)(t+9)==(void*)brng_draw && brng_trampoline_ok(t));
    t[2]^=1;assert(!brng_trampoline_ok(t));
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();brng_installed=0;
    world_identity_installed=0;assert(!brng_arm() && !strcmp(brng_stage,"needs"));world_identity_installed=1;
    assert(!brng_arm() && !strcmp(brng_stage,"pins") && !brng_installed);
    stats.issued=1;assert(!brng_arm());stats.issued=0;boot_phase=2;assert(!brng_arm());boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!brng_arm());ui_thread=GetCurrentThreadId();
    /* Site CAS, ownership, restore on the plain image. */
    unsigned char *saved_page=brng_page;brng_page=brng_alloc_page();assert(brng_page);
    image[BRNG_SITE]=0xe8;int32_t d=brng_rel(image+BRNG_DRAW);memcpy(image+BRNG_SITE+1,&d,4);
    assert(brng_state()==0);
    DWORD old,ignored;assert(VirtualProtect(brng_page,0x1000,PAGE_READWRITE,&old));brng_write_trampoline(brng_page);
    assert(VirtualProtect(brng_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    assert(brng_cas(brng_rel(image+BRNG_DRAW),brng_rel(brng_page)) && brng_state()==1);
    brng_done=1;brng_installed=1;assert(brng_owned());
    image[BRNG_SITE+2]^=1;assert(brng_state()<0 && !brng_owned());image[BRNG_SITE+2]^=1;assert(brng_owned());
    char out[256];assert(brng_status(out,sizeof(out))>0 && strstr(out,"installed=1 ") && strstr(out," keyed=4 loading=3 native=0 unmapped=2 fallback=1 "));
    brng_restore();assert(!brng_installed && brng_restored && !brng_done && brng_state()==0);
    memset(image+BRNG_SITE,0,5);VirtualFree(brng_page,0,MEM_RELEASE);brng_page=saved_page;
    brng_restored=0;brng_stage="idle";brng_keyed=brng_loading=brng_native=brng_unmapped=brng_fallback=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("build update keyed draw/trampoline/arm/site tests PASS\n");
}
static void test_build_rng_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=brng_page;brng_page=NULL;
    /* The draw site, its pins, and what the trampoline relies on: rdi holds the build storage (mov rdi, rcx at
     * 0x428816), rcx the thread word (TLS+0x318), the post adds the game time (TLS+0x300) and 60.0, and posts
     * an UpdateBuildEvent (vtable 0x2ac1710, slot 1 returns 0x20d) to rdi. */
    assert(brng_state()==0 && brng_pins());
    assert(!memcmp(image+0x428816,"\x48\x8b\xf9",3) && !memcmp(image+0x428926,"\xb9\x18\x03\x00\x00\x48\x03\xce",8));
    assert(!memcmp(image+0x428933,"\xb8\x00\x03\x00\x00",5) && image[0x428956]==0xe8 && 0x42895bu+*(int32_t*)(image+0x428957)==0x99a3f0u);
    assert(!memcmp(image+0x428942,"\x48\x8b\xcf",3) && 0x4288eeu+*(int32_t*)(image+0x4288ea)==0x2ac1710u);
    assert(*(uint64_t*)(image+0x2ac1718)==0x140000000ULL+0x34e040u && !memcmp(image+0x34e040,"\xb8\x0d\x02\x00\x00\xc3",6));
    assert(*(double*)(image+0x2cbeac8)==60.0 && *(float*)(image+0x2cbec4c)==10.0f);
    image[BRNG_SCHEDULER+0x10]^=1;assert(!brng_pins());image[BRNG_SCHEDULER+0x10]^=1;
    image[BRNG_SCHEDULER+0x150]^=1;assert(!brng_pins());image[BRNG_SCHEDULER+0x150]^=1;
    image[BRNG_DRAW+0x40]^=1;assert(!brng_pins());image[BRNG_DRAW+0x40]^=1;
    assert(brng_pins());
    brng_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("build update draw site, pins, storage register and event verified");
}
