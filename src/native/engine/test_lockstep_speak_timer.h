/* native_gate_speak_timer: arming refusals, CAS patch/restore, tick checks and the pinned speech code. */
static void test_speak_timer_reset(void) {
    speak_timer_installed=speak_timer_restored=speak_timer_patched=0;speak_timer_failures=0;speak_timer_stage="idle";
}
static void test_speak_timer(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    test_speak_timer_reset();
    const uint16_t original=SPEAK_TIMER_ORIGINAL;memcpy(image+SPEAK_TIMER_SITE,&original,2);
    /* Arming refuses without an active gate, after tick 0, off the UI thread and on unpinned bytes. */
    stats.active=0;stats.issued=0;ui_thread=GetCurrentThreadId();assert(!speak_timer_arm());
    stats.active=1;stats.issued=1;assert(!speak_timer_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!speak_timer_arm());ui_thread=GetCurrentThreadId();
    assert(!speak_timer_arm() && !strcmp(speak_timer_stage,"pins") && !speak_timer_installed);
    assert(speak_timer_site()==SPEAK_TIMER_ORIGINAL);
    /* The site rewrite is a 16-bit CAS in both directions; a foreign value stays. */
    assert(speak_timer_swap(SPEAK_TIMER_ORIGINAL,SPEAK_TIMER_PATCHED) && speak_timer_site()==SPEAK_TIMER_PATCHED);
    assert(!speak_timer_swap(SPEAK_TIMER_ORIGINAL,SPEAK_TIMER_PATCHED) && speak_timer_site()==SPEAK_TIMER_PATCHED);
    speak_timer_installed=speak_timer_patched=1;
    assert(speak_timer_owned());
    assert(!speak_timer_check() && !strcmp(speak_timer_stage,"block_changed") && speak_timer_failures==1); /* the plain image has no pinned code */
    assert(!speak_timer_check()); /* a failure stays */
    speak_timer_failures=0;
    const uint16_t foreign=0xcccc;memcpy(image+SPEAK_TIMER_SITE,&foreign,2);
    assert(!speak_timer_owned() && !speak_timer_check() && !strcmp(speak_timer_stage,"site_changed"));
    speak_timer_restore();assert(speak_timer_installed && speak_timer_patched && !speak_timer_restored && speak_timer_site()==foreign); /* a foreign value stays */
    const uint16_t patched=SPEAK_TIMER_PATCHED;memcpy(image+SPEAK_TIMER_SITE,&patched,2);
    speak_timer_restore();assert(speak_timer_restored && !speak_timer_installed && !speak_timer_patched && speak_timer_site()==SPEAK_TIMER_ORIGINAL);
    speak_timer_restore();assert(speak_timer_restored);
    char status[256];assert(speak_timer_status(status,sizeof(status))>0 && strstr(status,"installed=0 site=7275 restored=1 stage=site_changed failures=1"));
    memset(image+SPEAK_TIMER_SITE,0,2);
    test_speak_timer_reset();stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    printf("speak timer arming, CAS, tick check, restore and status tests PASS\n");
}
static void test_speak_timer_native_image(const char *path) {
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
    assert(speak_timer_pins() && speak_timer_site()==SPEAK_TIMER_ORIGINAL);
    /* What the site does: the handle test, the jne over the state store, the store of state 4 for a line with no handle. */
    assert(!memcmp(image+0x5617c9,"\x83\x7e\x14\x00\x75\x72",6) && 0x5617cdu+2u+image[0x5617ce]==0x561841u);
    assert(!memcmp(image+0x5617cf,"\x83\x7e\x10\x00\x75\x65",6) && 0x5617d3u+2u+image[0x5617d4]==0x56183au);
    assert(!memcmp(image+0x56183a,"\xc7\x46\x10\x04\x00\x00\x00",7));
    /* The words the speech modules replace do not count; anything else in the pinned code refuses. */
    const unsigned others[]={0x561330,0x563e80,0x563fb0,0x562590};
    for(unsigned i=0;i<sizeof(others)/sizeof(*others);i++) {image[others[i]]^=1;assert(speak_timer_pins());image[others[i]]^=1;}
    const unsigned tamper[]={0x561340,0x5617d0,0x561a05,0x563d30,0x563e7f,0x563e90,0x563faf,0x563fc0,0x564342,0x5625a0,0x562ac2,0x1263650,0x1263712};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!speak_timer_pins());image[tamper[i]]^=1;}
    /* A full arm/check/restore cycle on the mapped image. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    test_speak_timer_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(speak_timer_arm() && speak_timer_installed && speak_timer_patched && speak_timer_site()==SPEAK_TIMER_PATCHED && speak_timer_check() && speak_timer_owned());
    assert(speak_timer_pins() && speak_timer_arm()); /* identical rerun */
    image[0x5617d0]^=1;assert(!speak_timer_check() && !strcmp(speak_timer_stage,"block_changed"));image[0x5617d0]^=1;
    speak_timer_failures=0;assert(speak_timer_check());
    speak_timer_restore();assert(speak_timer_restored && !speak_timer_installed && speak_timer_site()==SPEAK_TIMER_ORIGINAL && speak_timer_pins());
    test_speak_timer_reset();const uint16_t patched=SPEAK_TIMER_PATCHED,original=SPEAK_TIMER_ORIGINAL;
    memcpy(image+SPEAK_TIMER_SITE,&patched,2);assert(!speak_timer_arm() && !strcmp(speak_timer_stage,"site"));memcpy(image+SPEAK_TIMER_SITE,&original,2);
    test_speak_timer_reset();stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("speak timer native pins, the site's meaning, tamper refusals and arm/restore cycle verified");
}
