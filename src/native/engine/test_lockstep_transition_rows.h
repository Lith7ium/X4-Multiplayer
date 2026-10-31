/* native_gate_transition_rows: one row per TransitionEvent delivery (gate and
 * ship keys, gate class, flag, event and delivery times, ordinal, thread),
 * passing every call through unchanged; gating, eviction, arming refusals,
 * entry ownership, restore, finish, status bounds, and the native pins. */
static void *test_trr_args[3];
static double test_trr_time;
static unsigned test_trr_calls;
static void test_trr_original(void *event,void *listener,void *arg,double time) {
    test_trr_args[0]=event;test_trr_args[1]=listener;test_trr_args[2]=arg;test_trr_time=time;++test_trr_calls;
}
static void test_trr_reset(void) {
    trr_installed=trr_restored=0;trr_active=0;trr_calls=trr_rows=trr_failures=0;trr_tick=UINT64_MAX;trr_ordinal=0;
    trr_original_override=NULL;trr_stage="idle";test_trr_calls=0;
}
static void test_transition_rows(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7b0001,0x8bddc) && world_identity_record(0x7b0002,0xac854) && world_identity_record(0x7b0010,0x1fd20));
    test_trr_reset();
    static unsigned char gate[0x40],event[0x40],event2[0x40],tls[0x320];
    *(void**)gate=image+0xd9f000;*(uint64_t*)(gate+8)=0x7b0010;
    *(uint64_t*)(event+0x18)=0x7b0001;event[0x20]=1;*(uint64_t*)(event2+0x18)=0x7b0002;
    double now=33.25;memcpy(tls+0x300,&now,8);
    /* Inactive or not admitted: straight through, no row. */
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1990;ui_thread=GetCurrentThreadId();
    trr_run(test_trr_original,tls,event,gate,(void*)7,32.75);
    assert(test_trr_calls==1 && test_trr_args[0]==event && test_trr_args[1]==gate && test_trr_args[2]==(void*)7 && test_trr_time==32.75 && !trr_rows);
    trr_active=1;stats.issued=0;trr_run(test_trr_original,tls,event,gate,NULL,1.0);boot_phase=2;stats.issued=1990;
    trr_run(test_trr_original,tls,event,gate,NULL,1.0);boot_phase=3;assert(test_trr_calls==3 && !trr_rows && trr_calls==3);
    /* Admitted: a row per delivery, ordinals per tick, then the original. */
    trr_run(test_trr_original,tls,event,gate,NULL,32.75);trr_run(test_trr_original,tls,event2,gate,NULL,33.25);
    assert(test_trr_calls==5 && trr_rows==2);
    char row[384];
    assert(trr_row(0,row,sizeof(row))>0 && !strcmp(row,"index=0 tick=1990 ordinal=1 gate=s:000000000001fd20 class=00d9f000 object=s:000000000008bddc "
                                                   "flag=1 time=4040600000000000 now=4040a00000000000 ui=1"));
    assert(trr_row(1,row,sizeof(row))>0 && strstr(row," ordinal=2 ") && strstr(row," object=s:00000000000ac854 flag=0 time=4040a00000000000 "));
    stats.issued=1991;trr_run(test_trr_original,tls,event2,gate,NULL,33.25);assert(trr_row(2,row,sizeof(row))>0 && strstr(row," tick=1991 ordinal=1 "));
    /* Unmapped objects, no TLS block, another thread. */
    *(uint64_t*)(event+0x18)=0x7b0999;ui_thread=GetCurrentThreadId()+1;trr_run(test_trr_original,NULL,event,NULL,NULL,1.0);
    assert(trr_row(3,row,sizeof(row))>0 && strstr(row," gate=-:0000000000000000 class=00000000 object=-:0000000000000000 ") && strstr(row," now=0000000000000000 ui=0"));
    ui_thread=GetCurrentThreadId();
    char status[256];int n=trr_status(status,sizeof(status));
    assert(n>0 && !strcmp(status,"installed=0 calls=7 rows=4 restored=0 stage=idle failures=0"));
    trr_rows=TRR_RING+2;assert(trr_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    /* Arming: before the first admitted frame, on the MoveWorker, with the world identities; pins (zero image refuses). */
    test_trr_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;world_identity_installed=1;
    stats.issued=1;assert(!trr_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!trr_arm());ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!trr_arm());boot_phase=0;
    world_identity_installed=0;assert(!trr_arm() && !strcmp(trr_stage,"needs"));world_identity_installed=1;
    assert(!trr_arm() && !strcmp(trr_stage,"pins") && !trr_installed && !trr_active);
    /* Ownership and restore on a hand-set entry. */
    void *saved_entry;memcpy(&saved_entry,image+TRR_ENTRY,8);
    *(void**)(image+TRR_ENTRY)=(void*)trr_dispatch;trr_installed=1;trr_active=1;assert(trr_owned() && trr_arm());
    *(void**)(image+TRR_ENTRY)=(void*)test_trr_original;assert(!trr_owned() && !trr_check() && !strcmp(trr_stage,"entry_changed"));
    trr_failures=0;trr_restore();assert(trr_installed && !trr_restored && !trr_active && *(void**)(image+TRR_ENTRY)==(void*)test_trr_original);
    *(void**)(image+TRR_ENTRY)=(void*)trr_dispatch;trr_restore();assert(!trr_installed && trr_restored && *(void**)(image+TRR_ENTRY)==image+TRR_DISPATCH);
    /* finish() gives the entry back. */
    *(void**)(image+TRR_ENTRY)=(void*)trr_dispatch;trr_installed=1;trr_active=1;trr_restored=0;uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;
    finish(TRR_STOP_REASON);assert(stats.reason==TRR_STOP_REASON && !trr_installed && trr_restored && *(void**)(image+TRR_ENTRY)==image+TRR_DISPATCH);
    /* Status and rows at the widest values. */
    trr_installed=1;trr_calls=trr_rows=trr_failures=INT64_MAX;trr_stage="block_changed";
    n=trr_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TRANSITION_ROWS_STOP \n")<256);
    trr_rows=1;trr_ring[0]=(trr_row_t){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,WORLD_KEY_CREATED,WORLD_KEY_SAVED,255,1};
    n=trr_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE TRANSITION_ROW \n")<384);
    memcpy(image+TRR_ENTRY,&saved_entry,8);test_trr_reset();memset(trr_ring,0,sizeof(trr_ring));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_installed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("transition rows pass-through/rows/ordinals/unmapped/gating/eviction/arming/ownership/restore/finish/status tests PASS\n");
}
/* With the X4 9.00 executable: the entry in TransitionEvent's vtable, the
 * dispatch it pins and what it calls, tamper refusals and arm/restore on the copy. */
static void test_transition_rows_native_image(const char *path) {
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
    uint64_t base=nt.OptionalHeader.ImageBase;
    /* The file holds preferred-base pointers: slot 2 of the vtable whose locator names U::TransitionEvent. */
    assert(*(uint64_t*)(image+TRR_ENTRY)==base+TRR_DISPATCH && TRR_ENTRY-0x2ba9f90u==2u*8u);
    uint64_t locator=*(uint64_t*)(image+0x2ba9f90u-8u)-base;uint32_t type=*(uint32_t*)(image+locator+12);
    assert(!strcmp((const char*)image+type+16,".?AVTransitionEvent@U@@"));
    /* The listener's slot 29 (+0xe8) gets the object found by +0x18 and the flag +0x20; slot 29 of the highway entry gate posts the warp. */
    assert(!memcmp(image+0x9abe50,"\x44\x0f\xb6\x71\x20\x49\x8b\xf0\x48\x8b\x51\x18",12) && !memcmp(image+0x9abe69,"\x48\x8b\xa8\xe8\x00\x00\x00",7) &&
           !memcmp(image+0x9abeb5,"\xff\xd5",2));
    locator=*(uint64_t*)(image+0x2c34050u-8u)-base;type=*(uint32_t*)(image+locator+12);
    assert(!strcmp((const char*)image+type+16,".?AVHighwayEntryGate@U@@") && *(uint64_t*)(image+0x2c34050u+29u*8u)==base+0xd9fb50u);
    *(void**)(image+TRR_ENTRY)=image+TRR_DISPATCH;assert(trr_pins());
    image[TRR_DISPATCH+0x30]^=1;assert(!trr_pins());image[TRR_DISPATCH+0x30]^=1;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=world_identity_installed;
    test_trr_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    assert(trr_arm() && trr_installed && trr_owned() && trr_check() && trr_arm() && *(void**)(image+TRR_ENTRY)==(void*)trr_dispatch);
    image[TRR_DISPATCH+0x10]^=1;assert(!trr_check() && !strcmp(trr_stage,"block_changed"));image[TRR_DISPATCH+0x10]^=1;trr_failures=0;
    trr_restore();assert(trr_restored && !trr_installed && *(void**)(image+TRR_ENTRY)==image+TRR_DISPATCH);
    /* A foreign entry refuses arming and stays. */
    *(void**)(image+TRR_ENTRY)=image+TRR_DISPATCH+0x10;test_trr_reset();
    assert(!trr_arm() && !strcmp(trr_stage,"entry") && *(void**)(image+TRR_ENTRY)==image+TRR_DISPATCH+0x10 && !trr_active);
    test_trr_reset();world_identity_installed=saved_installed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("transition rows native entry (TransitionEvent slot 2), dispatch pin and slot-29 call, tamper refusals and arm/restore verified");
}
