/* native_gate_bullet_cadence: counters, arming refusals, CAS patch/restore,
 * tick checks and the pinned native scheduling block. */
static unsigned char test_bc_bullet[0x300],test_bc_tls[0x400];
static void test_bc_set(unsigned offset,double v) {memcpy(test_bc_bullet+offset,&v,8);}
/* One Move: next/last before, then what UpdateFlight leaves behind. */
static void test_bc_move(double now,double last,double next,double last_after,double next_after,unsigned other) {
    memcpy(test_bc_tls+0x300,&now,8);test_bc_set(0x268,last);test_bc_set(0x270,next);
    bullet_cadence_sample c;bullet_cadence_enter_tls(test_bc_bullet,&c,test_bc_tls);
    test_bc_set(0x268,last_after);test_bc_set(0x270,next_after);
    bullet_cadence_leave(test_bc_bullet,&c,other);
}
static void test_bc_reset(void) {
    memset(bullet_cadence_frame,0,sizeof(bullet_cadence_frame));memset(bullet_cadence_total,0,sizeof(bullet_cadence_total));
    bullet_cadence_tick=UINT64_MAX;bullet_cadence_first_later=bullet_cadence_unreadable=0;bullet_cadence_failures=0;
    bullet_cadence_mode=BULLET_CADENCE_OFF;bullet_cadence_installed=bullet_cadence_restored=bullet_cadence_patched=0;
    bullet_cadence_active=0;bullet_cadence_stage="idle";
}
static void test_bullet_cadence(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    test_bc_reset();
    /* Inactive: the wrapper hook costs one flag read and counts nothing. */
    stats.issued=5;test_bc_move(7.0,6.9,7.0,7.0,7.0,0);
    assert(!bullet_cadence_total[0].moves && !bullet_cadence_unreadable && bullet_cadence_tick==UINT64_MAX);
    InterlockedExchange(&bullet_cadence_active,1);
    test_bc_move(7.0,6.9,7.0,7.0,7.0,0);         /* due, tested, next test this frame */
    test_bc_move(7.0,6.9,6.95,7.0,7.15,0);       /* due, tested, next test 0.15 s later */
    test_bc_move(7.0,6.9,7.2,6.9,7.2,0);         /* not due: untouched */
    test_bc_move(7.0,7.0,7.0,7.0,7.0,1);         /* already tested this frame: not a new test */
    test_bc_move(7.0,6.9,7.0,6.9,7.0,1);         /* due but no test stamped (e.g. removed early) */
    test_bc_move(7.0,6.9,7.0,7.0,7.1,1);         /* other bullet, later */
    const bullet_cadence_counts *f=bullet_cadence_frame,*t=bullet_cadence_total;
    assert(bullet_cadence_tick==5 && f[0].moves==3 && f[0].tested==2 && f[0].same_frame==1 && f[0].later==1);
    assert(f[1].moves==3 && f[1].tested==1 && f[1].same_frame==0 && f[1].later==1 && bullet_cadence_first_later==5);
    /* A new admitted frame resets the frame counters only. */
    stats.issued=6;test_bc_move(8.0,7.9,8.0,8.0,8.0,0);
    assert(bullet_cadence_tick==6 && f[0].moves==1 && f[0].same_frame==1 && !f[0].later && !f[1].moves);
    assert(t[0].moves==4 && t[0].tested==3 && t[0].same_frame==2 && t[0].later==1 && t[1].moves==3 && bullet_cadence_first_later==5);
    /* Unreadable thread block or bullet: counted, never classified. */
    bullet_cadence_sample c;bullet_cadence_enter_tls(test_bc_bullet,&c,NULL);assert(!c.valid && bullet_cadence_unreadable==1);
    bullet_cadence_leave(test_bc_bullet,&c,0);assert(t[0].moves==4);
    bullet_cadence_enter_tls(NULL,&c,test_bc_tls);assert(!c.valid && bullet_cadence_unreadable==2);
    char status[768];int n=bullet_cadence_status(status,sizeof(status));
    assert(n>0 && strstr(status,"frame=6 fixture=1/1/1/0 other=0/0/0/0 total_fixture=4/3/2/1 total_other=3/1/0/1 first_later=5 unreadable=2 camera="));
    /* Arming refuses without an active gate, off the UI thread, after tick 0, for
     * unknown modes, without the owned bullet Move wrapper, and on unpinned bytes. */
    test_bc_reset();stats.active=0;assert(!bullet_cadence_arm(BULLET_CADENCE_EVERY));
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!bullet_cadence_arm(BULLET_CADENCE_EVERY));
    ui_thread=GetCurrentThreadId();stats.issued=1;assert(!bullet_cadence_arm(BULLET_CADENCE_EVERY));stats.issued=0;
    assert(!bullet_cadence_arm(BULLET_CADENCE_OFF) && !bullet_cadence_arm(3));
    int saved_bullet=bullet_installed;bullet_installed=0;
    assert(!bullet_cadence_arm(BULLET_CADENCE_EVERY) && !strcmp(bullet_cadence_stage,"bullet_module"));
    bullet_installed=3;for(unsigned i=0;i<2;i++) *(void**)(image+bullet_entries[i])=bullet_wrapper(i);
    uint16_t original=BULLET_CADENCE_ORIGINAL,patched=BULLET_CADENCE_PATCHED,foreign=0x9090;
    memcpy(image+BULLET_CADENCE_SITE,&original,2);
    assert(!bullet_cadence_arm(BULLET_CADENCE_OBSERVE) && !strcmp(bullet_cadence_stage,"pins") && !bullet_cadence_installed);
    assert(bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    /* The site rewrite is a 16-bit CAS in both directions; a foreign value stays. */
    assert(bullet_cadence_swap(BULLET_CADENCE_ORIGINAL,BULLET_CADENCE_PATCHED) && bullet_cadence_site()==BULLET_CADENCE_PATCHED);
    assert(!bullet_cadence_swap(BULLET_CADENCE_ORIGINAL,BULLET_CADENCE_PATCHED) && bullet_cadence_site()==BULLET_CADENCE_PATCHED);
    bullet_cadence_installed=bullet_cadence_patched=1;bullet_cadence_mode=BULLET_CADENCE_EVERY;bullet_cadence_active=1;
    assert(bullet_cadence_owned());
    memcpy(image+BULLET_CADENCE_SITE,&foreign,2);assert(!bullet_cadence_owned());
    assert(!bullet_cadence_check() && !strcmp(bullet_cadence_stage,"site_changed") && bullet_cadence_failures==1);
    memcpy(image+BULLET_CADENCE_SITE,&patched,2);assert(!bullet_cadence_check()); /* a counted failure stays */
    bullet_cadence_failures=0;assert(!bullet_cadence_check() && !strcmp(bullet_cadence_stage,"block_changed")); /* zero image: unpinned */
    memcpy(image+BULLET_CADENCE_SITE,&foreign,2);bullet_cadence_restore();
    assert(!bullet_cadence_active && bullet_cadence_installed && !bullet_cadence_restored && bullet_cadence_site()==0x9090);
    memcpy(image+BULLET_CADENCE_SITE,&patched,2);bullet_cadence_restore();
    assert(bullet_cadence_restored && !bullet_cadence_installed && !bullet_cadence_patched && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    /* finish() restores the site; a foreign site keeps stats.restored at 0. */
    memcpy(image+BULLET_CADENCE_SITE,&patched,2);bullet_cadence_installed=bullet_cadence_patched=1;bullet_cadence_active=1;bullet_cadence_restored=0;
    uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(94);
    assert(stats.reason==94 && !stats.active && stats.restored==1 && !bullet_cadence_installed && bullet_cadence_restored &&
           bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    memcpy(image+BULLET_CADENCE_SITE,&foreign,2);bullet_cadence_installed=bullet_cadence_patched=1;bullet_cadence_active=1;bullet_cadence_restored=0;
    stats.active=1;stats.restored=1;finish(94);
    assert(!stats.restored && bullet_cadence_installed && !bullet_cadence_restored && bullet_cadence_site()==0x9090);
    /* Observe never writes the site; restore just disarms. */
    memcpy(image+BULLET_CADENCE_SITE,&original,2);bullet_cadence_patched=0;bullet_cadence_mode=BULLET_CADENCE_OBSERVE;
    assert(bullet_cadence_owned());bullet_cadence_restore();
    assert(bullet_cadence_restored && !bullet_cadence_installed && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    /* Status stays bounded at the widest values. */
    for(unsigned k=0;k<2;k++) {
        bullet_cadence_frame[k]=(bullet_cadence_counts){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
        bullet_cadence_total[k]=bullet_cadence_frame[k];
    }
    bullet_cadence_tick=stats.issued=UINT64_MAX-1;bullet_cadence_first_later=bullet_cadence_unreadable=UINT64_MAX;
    bullet_cadence_failures=INT64_MAX;bullet_cadence_stage="foreign_module";bullet_cadence_mode=BULLET_CADENCE_OBSERVE;
    n=bullet_cadence_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE BULLET_CADENCE tick=18446744073709551615 \n")<2048);
    memset(image+BULLET_CADENCE_SITE,0,2);for(unsigned i=0;i<2;i++) memset(image+bullet_entries[i],0,8);
    bullet_installed=saved_bullet;test_bc_reset();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.reason=saved_reason;stats.restored=0;
    printf("bullet cadence counters/arming/CAS/check/finish/status tests PASS\n");
}
static void test_bullet_cadence_native_image(const char *path) {
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
    assert(bullet_cadence_pins() && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    /* What the pinned block does: the flag test, the jne over the camera branch to
     * the store, the camera distance call and its 5 km threshold, the thread-RNG
     * interval (0.1 s + FloatState(TLS word, 0.1)) and the last/next test stores. */
    int32_t rel;
    assert(image[BULLET_CADENCE_SITE]==0x75 && BULLET_CADENCE_SITE+2+image[BULLET_CADENCE_SITE+1]==0xd50449u);
    assert(!memcmp(image+0xd50408,"\x42\xf7\x84\x30\x1c\x01\x00\x00\x00\x00\x04\x00",12));
    assert(image[0xd50419]==0xe8);memcpy(&rel,image+0xd5041a,4);assert(0xd5041e+rel==0x3e40a0);
    memcpy(&rel,image+0xd50421,4);float limit;memcpy(&limit,image+0xd50425+rel,4);assert(limit==25000000.0f);
    assert(image[0xd50425]==0x72 && 0xd50427+image[0xd50426]==0xd50449u);
    memcpy(&rel,image+0xd50431,4);float spread;memcpy(&spread,image+0xd50435+rel,4);assert(spread==0.1f);
    assert(image[0xd50435]==0xe8);memcpy(&rel,image+0xd50436,4);assert(0xd5043a+rel==0x14ddc90);
    memcpy(&rel,image+0xd50445,4);double base;memcpy(&base,image+0xd50449+rel,8);assert(base==0.1);
    assert(!memcmp(image+0xd50460,"\x49\x89\x86\x68\x02\x00\x00",7) && !memcmp(image+0xd50476,"\xf2\x41\x0f\x11\xb6\x70\x02\x00\x00",9));
    /* GetCachedDistanceSqToCamera: the cutscene camera (+0xb0) when set, else g_Universe+0x3d0. */
    memcpy(&rel,image+0x3e40a7,4);assert(0x3e40ab+rel==BULLET_CADENCE_CUTSCENE && !memcmp(image+0x3e40b0,"\x48\x8b\x92\xb0\x00\x00\x00",7));
    memcpy(&rel,image+0x3e40bf,4);assert(0x3e40c3+rel==0x3d198f8 && !memcmp(image+0x3e40c8,"\x48\x8b\x92\xd0\x03\x00\x00",7));
    /* Tampering anywhere in the block or the entry refuses; the patched jmp is accepted. */
    const unsigned tamper[]={BULLET_CADENCE_BLOCK,0xd50410,BULLET_CADENCE_SITE,BULLET_CADENCE_SITE+1,0xd50430,BULLET_CADENCE_BLOCK+BULLET_CADENCE_BLOCK_SIZE-1,0xd4f25f};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!bullet_cadence_pins());image[tamper[i]]^=1;}
    image[BULLET_CADENCE_SITE]=0xeb;assert(bullet_cadence_block_ok() && bullet_cadence_pins());image[BULLET_CADENCE_SITE]=0x75;
    /* A full arm/check/restore cycle on the mapped image. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_bullet=bullet_installed;
    void *saved_entries[2];for(unsigned i=0;i<2;i++) {memcpy(&saved_entries[i],image+bullet_entries[i],8);*(void**)(image+bullet_entries[i])=bullet_wrapper(i);}
    test_bc_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();bullet_installed=3;
    assert(bullet_cadence_arm(BULLET_CADENCE_EVERY) && bullet_cadence_installed && bullet_cadence_patched && bullet_cadence_active);
    assert(bullet_cadence_site()==BULLET_CADENCE_PATCHED && image[BULLET_CADENCE_SITE+1]==0x33 && bullet_cadence_check());
    assert(bullet_cadence_arm(BULLET_CADENCE_EVERY) && !bullet_cadence_arm(BULLET_CADENCE_OBSERVE)); /* identical rerun only */
    image[0xd50430]^=1;assert(!bullet_cadence_check() && !strcmp(bullet_cadence_stage,"block_changed"));image[0xd50430]^=1;
    bullet_cadence_failures=0;assert(bullet_cadence_check());
    bullet_cadence_restore();assert(bullet_cadence_restored && !bullet_cadence_installed && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL && bullet_cadence_pins());
    test_bc_reset();assert(bullet_cadence_arm(BULLET_CADENCE_OBSERVE) && !bullet_cadence_patched && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL && bullet_cadence_check());
    bullet_cadence_restore();assert(bullet_cadence_restored && bullet_cadence_site()==BULLET_CADENCE_ORIGINAL);
    test_bc_reset();image[BULLET_CADENCE_SITE]=0xeb;assert(!bullet_cadence_arm(BULLET_CADENCE_EVERY) && !strcmp(bullet_cadence_stage,"site"));
    image[BULLET_CADENCE_SITE]=0x75;test_bc_reset();
    for(unsigned i=0;i<2;i++) memcpy(image+bullet_entries[i],&saved_entries[i],8);
    bullet_installed=saved_bullet;stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("bullet cadence native pins, scheduling semantics, tamper refusals and arm/restore cycle verified");
}
