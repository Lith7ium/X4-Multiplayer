/* native_gate_ui_cargo: UI lazy-update calls blocked/observed in admitted frames (counts, rows, keys),
 * native elsewhere, stubs, arming refusals, site CAS/check/restore, the pinned build. */
static unsigned test_uic_updates;
static void *test_uic_container;
static double test_uic_now;
static void test_uic_update(void *container,double now,uint64_t force) {(void)force;++test_uic_updates;test_uic_container=container;test_uic_now=now;}
static void test_ui_cargo(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x8d001,0x43cc2));
    static unsigned char station[0x40];memset(station,0,sizeof(station));*(uint64_t*)(station+8)=0x8d001;
    uic_row_t *saved_ring=uic_ring;uic_ring=VirtualAlloc(NULL,sizeof(uic_row_t)*UIC_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(uic_ring);
    uic_update=test_uic_update;memset(uic_calls,0,sizeof(uic_calls));uic_blocked=uic_passed=uic_native=uic_unmapped=uic_keyset=0;uic_rows=0;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=3021;ui_thread=GetCurrentThreadId();
    /* Off: native. */
    uic_mode=UIC_OFF;test_uic_updates=0;uic_call(station,57.4,0,7);assert(test_uic_updates==1 && uic_native==1 && !uic_calls[7]);
    /* block: an admitted frame's UI query does not update; counted, keyed, one row. */
    uic_mode=UIC_BLOCK;uic_call(station,57.4,0,7);
    assert(test_uic_updates==1 && uic_blocked==1 && uic_calls[7]==1 && uic_rows==1);
    char out[512];assert(uic_row(0,out,sizeof(out))>0 && strstr(out,"tick=3021 site=GetStorageData class=saved key=0000000000043cc2 blocked=1"));
    /* observe: updates natively, still counted. */
    uic_mode=UIC_OBSERVE;uic_call(station,57.5,0,1);
    assert(test_uic_updates==2 && test_uic_container==station && test_uic_now==57.5 && uic_passed==1 && uic_calls[1]==1 && uic_rows==2);
    assert(uic_row(1,out,sizeof(out))>0 && strstr(out,"site=GetCargo class=saved key=0000000000043cc2 blocked=0"));
    /* No portable key: counted unmapped, key 0. */
    static unsigned char ship[0x40];memset(ship,0,sizeof(ship));*(uint64_t*)(ship+8)=0x9999;
    uic_mode=UIC_BLOCK;uic_call(ship,57.5,0,3);assert(uic_unmapped==1 && uic_blocked==2 && test_uic_updates==2);
    /* Loading frames, other threads and unknown sites stay native. */
    boot_phase=2;uic_call(station,7.0,0,7);assert(test_uic_updates==3 && uic_blocked==2);boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;uic_call(station,57.5,0,7);assert(test_uic_updates==4);ui_thread=GetCurrentThreadId();
    uic_call(station,57.5,0,UIC_SITES);assert(test_uic_updates==5 && uic_native==4);
    uint64_t keyset=uic_keyset;uic_keyset=0;uic_call(ship,57.5,0,3);uic_call(station,57.4,0,7);uint64_t k1=uic_keyset;
    uic_keyset=0;uic_call(station,57.4,0,7);uic_call(ship,57.5,0,3);assert(uic_keyset==k1 && keyset);
    assert(uic_status(out,sizeof(out))>0 && strstr(out,"mode=block frame=3021 ") && strstr(out," calls=0/1/0/3/0/0/0/3/0/0 "));
    /* Stubs: mov r9d, site; jmp [rip]; uic_call. */
    unsigned char t[20];uic_write_stub(t,7);
    assert(t[0]==0x41 && t[1]==0xb9 && *(uint32_t*)(t+2)==7 && t[6]==0xff && t[7]==0x25 && !*(uint32_t*)(t+8) && *(void**)(t+12)==(void*)uic_call);
    assert(uic_stub_ok(t,7) && !uic_stub_ok(t,6));
    /* Arming refusals on the zero image. */
    uic_mode=UIC_OFF;uic_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!uic_arm(0) && !strcmp(uic_stage,"mode"));
    world_identity_installed=0;assert(!uic_arm(UIC_BLOCK) && !strcmp(uic_stage,"needs"));world_identity_installed=1;
    assert(!uic_arm(UIC_BLOCK) && !strcmp(uic_stage,"pins") && !uic_installed);
    stats.issued=1;assert(!uic_arm(UIC_BLOCK));stats.issued=0;boot_phase=2;assert(!uic_arm(UIC_BLOCK));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!uic_arm(UIC_BLOCK));ui_thread=GetCurrentThreadId();
    /* Site CAS, ownership and restore on the plain image. */
    unsigned char *saved_page=uic_page;uic_page=uic_alloc_page();assert(uic_page);
    for(unsigned i=0;i<UIC_SITES;i++) {image[uic_sites[i].rva]=0xe8;int32_t d=uic_rel(i,image+UIC_TARGET);memcpy(image+uic_sites[i].rva+1,&d,4);assert(uic_state(i)==0);}
    DWORD old,ignored;assert(VirtualProtect(uic_page,0x1000,PAGE_READWRITE,&old));
    for(unsigned i=0;i<UIC_SITES;i++) uic_write_stub(uic_page+UIC_STUB*i,i);
    assert(VirtualProtect(uic_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    for(unsigned i=0;i<UIC_SITES;i++) {assert(uic_cas(i,uic_rel(i,image+UIC_TARGET),uic_rel(i,uic_page+UIC_STUB*i)) && uic_state(i)==1);uic_done|=1u<<i;}
    uic_installed=1;uic_mode=UIC_BLOCK;assert(uic_owned());
    image[uic_sites[4].rva+3]^=1;assert(uic_state(4)<0 && !uic_owned());image[uic_sites[4].rva+3]^=1;assert(uic_owned());
    uic_restore();assert(!uic_installed && uic_restored && !uic_done && uic_mode==UIC_OFF);
    for(unsigned i=0;i<UIC_SITES;i++) {assert(uic_state(i)==0);memset(image+uic_sites[i].rva,0,5);}
    VirtualFree(uic_page,0,MEM_RELEASE);uic_page=saved_page;
    uic_restored=0;uic_stage="idle";uic_update=NULL;
    VirtualFree(uic_ring,0,MEM_RELEASE);uic_ring=saved_ring;uic_rows=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("UI cargo block/observe/rows/stubs/arm/site tests PASS\n");
}
static void test_ui_cargo_native_image(const char *path) {
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
    unsigned char *saved_page=uic_page;uic_page=NULL;
    assert(uic_pins());
    /* Every site: E8 to 0x41a910 with force 0 (xor r8d, r8d before it) and the game time from TLS+0x300 in xmm1. */
    for(unsigned i=0;i<UIC_SITES;i++) {
        const unsigned char *s=image+uic_sites[i].rva;assert(uic_state(i)==0);int found=0;
        for(unsigned k=1;k<=0x10;k++) found|=!memcmp(s-k,"\x45\x33\xc0",3);
        assert(found);
    }
    /* 0x41a910: rcx container, xmm1 time; +0xa18 the time it was last brought to (returns when not newer). */
    assert(!memcmp(image+0x41a95c,"\xf2\x0f\x10\x81\x18\x0a\x00\x00",8) && !memcmp(image+0x41a975,"\xf2\x44\x0f\x11\x91\x18\x0a\x00\x00",9));
    image[UIC_TARGET+0x20]^=1;assert(!uic_pins());image[UIC_TARGET+0x20]^=1;
    image[uic_sites[7].rva-4]^=1;assert(!uic_pins());image[uic_sites[7].rva-4]^=1;
    assert(uic_pins());
    uic_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("UI cargo sites, pins and the lazy update's time check verified");
}
