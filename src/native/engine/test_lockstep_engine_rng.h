/* native_gate_engine_refresh_rng: keyed reschedule draw (seed, thread word untouched,
 * unmapped and loading frames native), trampoline, site CAS/check/restore, the pinned build. */
static uint64_t test_erng_seen;
static float test_erng_draw(uint64_t *word,float range) {test_erng_seen=*word;*word^=0x5555;return range*0.5f;}
static void test_engine_rng(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5b065,0x78855) && world_identity_record(0x5b066,0x896ff));
    static unsigned char ship[0x40];memset(ship,0,sizeof(ship));*(uint64_t*)(ship+8)=0x5b065;
    erng_keyed=erng_native=erng_unmapped=erng_fallback=0;
    /* Admitted frame: the draw runs on (tick, ship key); the thread's word is neither read nor advanced. */
    stats.active=1;boot_phase=3;stats.issued=574;uint64_t word=0x1111;
    float r=erng_run(test_erng_draw,&word,2.0f,ship);
    assert(r==1.0f && word==0x1111 && test_erng_seen==lockstep_seed(ERNG_DOMAIN,574,0x78855,0,0,WORLD_KEY_SAVED) && erng_keyed==1);
    word=0x9999;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(word==0x9999 && test_erng_seen==lockstep_seed(ERNG_DOMAIN,574,0x78855,0,0,WORLD_KEY_SAVED)); /* independent of the word */
    *(uint64_t*)(ship+8)=0x5b066;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(test_erng_seen==lockstep_seed(ERNG_DOMAIN,574,0x896ff,0,0,WORLD_KEY_SAVED));
    stats.issued=598;*(uint64_t*)(ship+8)=0x5b065;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(test_erng_seen==lockstep_seed(ERNG_DOMAIN,598,0x78855,0,0,WORLD_KEY_SAVED) && erng_keyed==4);
    /* Unmapped ship: (tick, its class vtable); without a vtable in the image, no ship, or a loading frame: the thread's word. */
    *(uint64_t*)(ship+8)=0x9898;void *vt=image+0x2b82440;memcpy(ship,&vt,8);word=0x4444;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(test_erng_seen==lockstep_seed(ERNG_DOMAIN,598,0,0x2b82440,1,WORLD_KEY_NONE) && word==0x4444 && erng_fallback==1 && !erng_unmapped);
    memset(ship,0,8);word=0x4444;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(test_erng_seen==0x4444 && word==(0x4444^0x5555) && erng_unmapped==1);
    word=0x4444;erng_run(test_erng_draw,&word,2.0f,NULL);assert(test_erng_seen==0x4444 && erng_unmapped==2);
    boot_phase=2;*(uint64_t*)(ship+8)=0x5b065;word=0x4444;erng_run(test_erng_draw,&word,2.0f,ship);
    assert(test_erng_seen==0x4444 && erng_native==1);
    /* The trampoline: mov r8, rsi; jmp [rip]; erng_draw. */
    unsigned char t[17];erng_write_trampoline(t);
    assert(t[0]==0x4c && t[1]==0x8b && t[2]==0xc6 && t[3]==0xff && t[4]==0x25 && !t[5] && !t[8] && *(void**)(t+9)==(void*)erng_draw && erng_trampoline_ok(t));
    t[2]^=1;assert(!erng_trampoline_ok(t));
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();erng_installed=0;
    world_identity_installed=0;assert(!erng_arm() && !strcmp(erng_stage,"needs"));world_identity_installed=1;
    assert(!erng_arm() && !strcmp(erng_stage,"pins") && !erng_installed);
    stats.issued=1;assert(!erng_arm());stats.issued=0;boot_phase=2;assert(!erng_arm());boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!erng_arm());ui_thread=GetCurrentThreadId();
    /* Site CAS, ownership, restore on the plain image. */
    unsigned char *saved_page=erng_page;erng_page=erng_alloc_page();assert(erng_page);
    image[ERNG_SITE]=0xe8;int32_t d=erng_rel(image+ERNG_DRAW);memcpy(image+ERNG_SITE+1,&d,4);
    assert(erng_state()==0);
    DWORD old,ignored;assert(VirtualProtect(erng_page,0x1000,PAGE_READWRITE,&old));erng_write_trampoline(erng_page);
    assert(VirtualProtect(erng_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    assert(erng_cas(erng_rel(image+ERNG_DRAW),erng_rel(erng_page)) && erng_state()==1);
    erng_done=1;erng_installed=1;assert(erng_owned());
    image[ERNG_SITE+2]^=1;assert(erng_state()<0 && !erng_owned());image[ERNG_SITE+2]^=1;assert(erng_owned());
    char out[256];assert(erng_status(out,sizeof(out))>0 && strstr(out,"installed=1 ") && strstr(out," keyed=4 native=1 unmapped=2 fallback=1 "));
    erng_restore();assert(!erng_installed && erng_restored && !erng_done && erng_state()==0);
    memset(image+ERNG_SITE,0,5);VirtualFree(erng_page,0,MEM_RELEASE);erng_page=saved_page;
    erng_restored=0;erng_stage="idle";erng_keyed=erng_native=erng_unmapped=erng_fallback=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("engine refresh keyed draw/trampoline/arm/site tests PASS\n");
}
static void test_engine_rng_native_image(const char *path) {
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
    unsigned char *saved_page=erng_page;erng_page=NULL;
    /* The draw site, its pins, and what the trampoline relies on: rsi holds the ship ([this+0x18], 0xea1e14) and
     * rcx the thread word (TLS+0x318), the reschedule adds 8.0 and the draw to the game time and posts the event. */
    assert(erng_state()==0 && erng_pins());
    assert(!memcmp(image+0xea1e14,"\x48\x8b\x77\x18",4) && !memcmp(image+0xea1e33,"\xb9\x18\x03\x00\x00\x48\x03\xcf",8));
    assert(!memcmp(image+0xea1e5f,"\xf2\x0f\x58\xd0",4) && image[0xea1e63]==0xe8 && 0xea1e68u+*(int32_t*)(image+0xea1e64)==0x99a3f0u);
    /* The listener is OnUpdateEngineParameters in both vtables that carry it, and its refresh call is 0xea1e80. */
    assert(*(uint64_t*)(image+0x2c50bd0)==0x140000000ULL+0xea1d70u && *(uint64_t*)(image+0x2c38f00)==0x140000000ULL+0xea1d70u);
    assert(image[0xea1d9a]==0xe8 && 0xea1d9fu+*(int32_t*)(image+0xea1d9b)==0xea1e80u);
    image[0xea1d80]^=1;assert(!erng_pins());image[0xea1d80]^=1;
    image[0xea1e50]^=1;assert(!erng_pins());image[0xea1e50]^=1;
    image[ERNG_DRAW+0x40]^=1;assert(!erng_pins());image[ERNG_DRAW+0x40]^=1;
    /* The refresh call routed by the motion watch does not change the pins. */
    int32_t d;memcpy(&d,image+0xea1d9b,4);int32_t other=d+0x1000;memcpy(image+0xea1d9b,&other,4);assert(erng_pins());memcpy(image+0xea1d9b,&d,4);
    assert(erng_pins());
    erng_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("engine refresh draw site, pins, ship register and listener verified");
}
