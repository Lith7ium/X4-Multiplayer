/* native_gate_weapon_fire_rng: keyed shot scope (seed, restore, unmapped,
 * ordinals), arming refusals, entry swap/check/restore, the pinned build. */
static uint64_t test_wf_seen,test_wf_calls,test_wf_args[12];
static uint64_t *test_wf_word;
static double test_wf_original(void *weapon,void *a2,void *a3,uint64_t shot,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,
                               uint64_t a10,uint64_t a11,uint64_t a12) {
    ++test_wf_calls;test_wf_seen=*test_wf_word;*test_wf_word^=0x5555; /* the shot draws */
    uint64_t args[12]={(uintptr_t)weapon,(uintptr_t)a2,(uintptr_t)a3,shot,a5,a6,a7,a8,a9,a10,a11,a12};memcpy(test_wf_args,args,sizeof(args));
    return 7.25;
}
static void test_weapon_fire(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7171,0x176c5f) && world_identity_record(0x7272,0x176c5e));
    static unsigned char weapon[0x40];memset(weapon,0,sizeof(weapon));*(uint64_t*)(weapon+8)=0x7171;
    uint64_t word=0x1111;test_wf_word=&word;
    memset(wfire_ordinals,0,sizeof(wfire_ordinals));wfire_keyed=wfire_unmapped=wfire_frame_keyed=wfire_table_full=0;wfire_tick=UINT64_MAX;
    /* A keyed shot: seeded from (tick, key, shot, ordinal, klass), all twelve arguments forwarded, the word restored. */
    stats.issued=20;
    double r=wfire_run(test_wf_original,&word,weapon,(void*)2,(void*)3,0,0x4010000000000000ULL,6,7,8,9,10,11,0x3f800000);
    assert(r==7.25 && test_wf_calls==1 && word==0x1111 && test_wf_seen==lockstep_seed(WFIRE_DOMAIN,20,0x176c5f,0,1,WORLD_KEY_SAVED));
    assert(test_wf_args[0]==(uintptr_t)weapon && test_wf_args[4]==0x4010000000000000ULL && test_wf_args[11]==0x3f800000 && test_wf_args[7]==8);
    assert(wfire_keyed==1 && wfire_frame_keyed==1);
    /* The next shot of the same weapon: its index; the same index again: the next ordinal. Independent of the word. */
    word=0x9999;wfire_run(test_wf_original,&word,weapon,0,0,1,0,0,0,0,0,0,0,0);
    assert(test_wf_seen==lockstep_seed(WFIRE_DOMAIN,20,0x176c5f,1,2,WORLD_KEY_SAVED) && word==0x9999);
    wfire_run(test_wf_original,&word,weapon,0,0,1,0,0,0,0,0,0,0,0);
    assert(test_wf_seen==lockstep_seed(WFIRE_DOMAIN,20,0x176c5f,1,3,WORLD_KEY_SAVED));
    /* Another weapon has its own ordinals; a new tick starts them again. */
    *(uint64_t*)(weapon+8)=0x7272;wfire_run(test_wf_original,&word,weapon,0,0,0,0,0,0,0,0,0,0,0);
    assert(test_wf_seen==lockstep_seed(WFIRE_DOMAIN,20,0x176c5e,0,1,WORLD_KEY_SAVED));
    stats.issued=21;*(uint64_t*)(weapon+8)=0x7171;wfire_run(test_wf_original,&word,weapon,0,0,0,0,0,0,0,0,0,0,0);
    assert(test_wf_seen==lockstep_seed(WFIRE_DOMAIN,21,0x176c5f,0,1,WORLD_KEY_SAVED) && wfire_frame_keyed==1 && wfire_keyed==5);
    /* Unmapped weapon or no word: native, counted, the word untouched by us. */
    *(uint64_t*)(weapon+8)=0x9898;word=0x4444;wfire_run(test_wf_original,&word,weapon,0,0,0,0,0,0,0,0,0,0,0);
    assert(test_wf_seen==0x4444 && word==(0x4444^0x5555) && wfire_unmapped==1);
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();wfire_installed=0;
    world_identity_installed=0;assert(!wfire_arm() && !strcmp(wfire_stage,"needs"));world_identity_installed=1;
    assert(!wfire_arm() && !strcmp(wfire_stage,"pins") && !wfire_installed);
    stats.issued=1;assert(!wfire_arm());stats.issued=0;boot_phase=2;assert(!wfire_arm());boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!wfire_arm());ui_thread=GetCurrentThreadId();
    /* Entry swap by CAS, ownership, restore (the plain image's entries). */
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) *(void**)(image+wfire_entries[i])=(void*)(image+WFIRE_FN);
    wfire_swapped=0;for(unsigned i=0;i<WFIRE_ENTRIES;i++) {assert(wfire_swap(i,image+WFIRE_FN,(void*)wfire_shot));wfire_swapped|=1u<<i;}
    wfire_installed=1;assert(wfire_owned());
    *(void**)(image+wfire_entries[1])=(void*)0x1234;assert(!wfire_owned());*(void**)(image+wfire_entries[1])=(void*)wfire_shot;
    char out[512];assert(wfire_status(out,sizeof(out))>0 && strstr(out,"installed=1 ") && strstr(out," unmapped=1 "));
    wfire_restore();assert(!wfire_installed && wfire_restored && !wfire_swapped);
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) {assert(*(void**)(image+wfire_entries[i])==(void*)(image+WFIRE_FN));*(void**)(image+wfire_entries[i])=NULL;}
    wfire_restored=0;wfire_stage="idle";wfire_keyed=wfire_unmapped=wfire_frame_keyed=0;wfire_tick=UINT64_MAX;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("weapon fire keyed shot scope/arm/swap tests PASS\n");
}
static void test_weapon_fire_native_image(const char *path) {
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
    /* The pinned fire virtual and the fire loop's slot-998 call (mov r10,[rax+0x1f30]; call r10). */
    assert(wfire_pins());
    assert(!memcmp(image+0x818141,"\x4c\x8b\x90\x30\x1f\x00\x00",7) && !memcmp(image+0x818190,"\x41\xff\xd2",3) && 998*8==0x1f30);
    /* The slot-998 entries in .rdata are relocated absolute addresses of 0x8185b0 (preferred base 0x140000000). */
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) assert(*(uint64_t*)(image+wfire_entries[i])==0x140000000ULL+WFIRE_FN);
    /* The jitter is the shot's first draw: FloatState on TLS+0x318. */
    assert(!memcmp(image+0x8186aa,"\xb9\x18\x03\x00\x00",5) && image[0x8186b2]==0xe8 && 0x8186b7u+*(int32_t*)(image+0x8186b3)==0x14ddc90u);
    image[WFIRE_FN+0x40]^=1;assert(!wfire_pins());image[WFIRE_FN+0x40]^=1;
    image[0x818150]^=1;assert(!wfire_pins());image[0x818150]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("weapon fire native pins, slot-998 dispatch, entries and jitter draw verified");
}
