/* native_gate_nav_cadence: counters, arming refusals, aligned-block CAS of the
 * unaligned branch sites, rollback, tick checks and the pinned native blocks. */
static unsigned char test_nc_controller[0x100],test_nc_probe[0x400];
static void test_nc_set(double params,double probe) {
    memcpy(test_nc_controller+0xd0,&params,8);memcpy(test_nc_probe+0x3e0,&probe,8);
}
/* One vfunc33 call: deadlines before, then what the native code leaves behind. */
static void test_nc_call(double time,double params,double probe,double params_after,double probe_after,unsigned other) {
    test_nc_set(params,probe);nav_cadence_sample c;nav_cadence_enter(test_nc_controller,time,&c);
    test_nc_set(params_after,probe_after);nav_cadence_leave(test_nc_controller,&c,other,0);
}
static void test_nc_reset(void) {
    memset(nav_cadence_frame,0,sizeof(nav_cadence_frame));memset(nav_cadence_total,0,sizeof(nav_cadence_total));
    nav_cadence_tick=UINT64_MAX;nav_cadence_first_now=nav_cadence_first_mid=nav_cadence_unreadable=0;nav_cadence_failures=0;
    nav_cadence_mode=NAV_CADENCE_OFF;nav_cadence_installed=nav_cadence_restored=nav_cadence_patched=0;
    nav_cadence_active=0;nav_cadence_stage="idle";
}
static void test_nc_write_sites(int mode) {
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {uint16_t v=nav_cadence_value(i,mode);memcpy(image+nav_cadence_sites[i].rva,&v,2);}
}
static void test_nav_cadence(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    test_nc_reset();
    unsigned char *probe=test_nc_probe;memcpy(test_nc_controller+0x48,&probe,8);
    test_nc_controller[0xd8]=1;test_nc_probe[0x3e8]=1;
    /* Inactive: nothing is read or counted. */
    stats.issued=7;test_nc_call(5.0,4.9,4.9,5.0,5.0,0);
    assert(!nav_cadence_total[0].calls && nav_cadence_tick==UINT64_MAX && !nav_cadence_unreadable);
    InterlockedExchange(&nav_cadence_active,1);
    test_nc_call(5.0,4.9,4.9,5.0,5.7,0);   /* params now, probes 0.5-1 s */
    test_nc_call(5.0,4.9,4.9,6.5,6.9,0);   /* both 1-2 s */
    test_nc_call(5.0,5.5,5.5,5.5,5.5,0);   /* not due: unchanged */
    test_nc_call(5.0,4.9,4.9,5.8,5.0,1);   /* other controller */
    const nav_cadence_counts *f=nav_cadence_frame,*t=nav_cadence_total;
    assert(nav_cadence_tick==7 && f[0].calls==3 && f[0].refresh[0]==2 && f[0].now[0]==1 && !f[0].mid[0] && f[0].slow[0]==1);
    assert(f[0].refresh[1]==2 && !f[0].now[1] && f[0].mid[1]==1 && f[0].slow[1]==1);
    assert(f[1].calls==1 && f[1].refresh[0]==1 && f[1].mid[0]==1 && f[1].refresh[1]==1 && f[1].now[1]==1);
    assert(nav_cadence_first_now==7 && nav_cadence_first_mid==7);
    /* Disabled blocks and a missing probe object are not counted as refreshes. */
    test_nc_controller[0xd8]=0;test_nc_probe[0x3e8]=0;test_nc_call(5.0,4.9,4.9,6.0,6.0,0);
    assert(f[0].calls==4 && f[0].refresh[0]==2 && f[0].refresh[1]==2);
    test_nc_controller[0xd8]=1;test_nc_probe[0x3e8]=1;memset(test_nc_controller+0x48,0,8);test_nc_call(5.0,4.9,4.9,6.0,4.9,0);
    assert(f[0].calls==5 && f[0].refresh[0]==3 && f[0].slow[0]==2 && f[0].refresh[1]==2);
    memcpy(test_nc_controller+0x48,&probe,8);
    /* A new admitted frame resets the frame counters only. */
    stats.issued=8;test_nc_call(6.0,5.9,5.9,7.5,6.0,0);
    assert(nav_cadence_tick==8 && f[0].calls==1 && f[0].slow[0]==1 && f[0].now[1]==1 && !f[1].calls);
    assert(t[0].calls==6 && t[0].refresh[0]==4 && t[0].slow[0]==3 && t[0].now[1]==1 && t[1].calls==1 && nav_cadence_first_now==7);
    nav_cadence_sample c;nav_cadence_enter(NULL,1.0,&c);assert(!c.valid && nav_cadence_unreadable==1);
    nav_cadence_leave(NULL,&c,0,0);assert(t[0].calls==6);
    char status[1536];int n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && strstr(status,"frame=8 fixture=1/1/0/0/1/1/1/0/0 other=0/0/0/0/0/0/0/0/0 total_fixture=6/4/1/0/3/3/1/1/1 total_other=1/1/0/1/0/1/1/0/0 first_now=7 first_mid=7 unreadable=1"));
    /* Fixture rows: the first state and every change, nothing for an unchanged state or other controllers. */
    memset(nav_cadence_seen,0,sizeof(nav_cadence_seen));nav_cadence_rows=0;
    test_nc_set(5.9,6.1);nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,3);
    assert(nav_cadence_rows==1);
    nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,3);assert(nav_cadence_rows==1);
    nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,1,0);assert(nav_cadence_rows==1);
    test_nc_probe[0x3e8]=0;nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,3);assert(nav_cadence_rows==2);
    test_nc_probe[0x3e8]=1;nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,4);assert(nav_cadence_rows==3);
    nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,ACTOR_CAPACITY+1);assert(nav_cadence_rows==3);
    char row[256];assert(nav_cadence_row(0,row,sizeof(row))>0);
    assert(!strcmp(row,"index=0 tick=8 id=3 saved=0000000000000000 attention=-1 params_on=1 params_due=401799999999999a probe=1 probe_on=1 probe_due=4018666666666666 body=3 zone_scene=3 behaviour=ffffffff avoid=255"));
    /* The Agent's flight behaviour and avoidance flags (controller+0x40). */
    {
        static unsigned char agent[0x1000],behaviour[16];void *p=agent;unsigned char *b=behaviour;void *vt=image+0x2c51158;
        memset(agent,0,sizeof(agent));memcpy(test_nc_controller+0x40,&p,8);agent[0xf5d]=1;
        nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,5);
        assert(nav_cadence_row(3,row,sizeof(row))>0 && strstr(row," behaviour=00000000 avoid=2"));
        memcpy(behaviour,&vt,8);memcpy(agent+0xf10,&b,8);agent[0xf5c]=1;
        nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,5);
        assert(nav_cadence_row(4,row,sizeof(row))>0 && strstr(row," behaviour=02c51158 avoid=3"));
        void *outside=(void*)1;memcpy(behaviour,&outside,8);
        nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,0,5);
        assert(nav_cadence_row(5,row,sizeof(row))>0 && strstr(row," behaviour=ffffffff avoid=3"));
        memset(test_nc_controller+0x40,0,8);
    }
    assert(nav_cadence_row(1,row,sizeof(row))>0 && strstr(row,"id=3") && strstr(row," probe_on=0 "));
    assert(!nav_cadence_row(6,row,sizeof(row)));
    nav_cadence_rows=NAV_CADENCE_RING+5;assert(nav_cadence_row(2,row,sizeof(row))>0 && !strcmp(row,"index=2 evicted=1"));
    nav_cadence_rows=0;memset(nav_cadence_seen,0,sizeof(nav_cadence_seen));
    /* Watched non-fixture ships by saved ID; unmapped or unwatched ships give no row. */
    assert(!nav_cadence_watch_add(0) && nav_cadence_watch_add(0x74d83) && nav_cadence_watch_add(0x74d83) && nav_cadence_watch_count==1);
    nav_cadence_enter(test_nc_controller,6.0,&c);nav_cadence_leave(test_nc_controller,&c,1,0);assert(!nav_cadence_rows); /* no ship */
    nav_cadence_watch_count=0;memset(nav_cadence_watch_seen,0,sizeof(nav_cadence_watch_seen));nav_cadence_rows=0;
    /* Arming refusals. */
    test_nc_reset();stats.active=0;assert(!nav_cadence_arm(NAV_CADENCE_FAR));
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!nav_cadence_arm(NAV_CADENCE_FAR));
    ui_thread=GetCurrentThreadId();stats.issued=1;assert(!nav_cadence_arm(NAV_CADENCE_FAR));stats.issued=0;
    assert(!nav_cadence_arm(NAV_CADENCE_OFF) && !nav_cadence_arm(4));
    int saved_nav=nav_installed;void *saved_entry;memcpy(&saved_entry,image+NAV_ENTRY,8);
    nav_installed=0;assert(!nav_cadence_arm(NAV_CADENCE_EVERY) && !strcmp(nav_cadence_stage,"nav_module"));
    nav_installed=1;*(void**)(image+NAV_ENTRY)=(void*)nav_move;test_nc_write_sites(NAV_CADENCE_OBSERVE);
    assert(!nav_cadence_arm(NAV_CADENCE_OBSERVE) && !strcmp(nav_cadence_stage,"pins") && !nav_cadence_installed);
    /* Each unaligned site is rewritten through its aligned 16 bytes; neighbours keep their bytes. */
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {
        unsigned char *block=image+(nav_cadence_sites[i].rva&~15u),before[16],after[16];
        for(unsigned j=0;j<16;j++) if(block+j<image+nav_cadence_sites[i].rva || block+j>=image+nav_cadence_sites[i].rva+2) block[j]=(unsigned char)(0xa0+j);
        memcpy(before,block,16);
        uint16_t value=nav_cadence_sites[i].distant;
        assert(nav_cadence_swap(i,nav_cadence_sites[i].original,value) && nav_cadence_site(i)==value);
        memcpy(after,block,16);unsigned offset=nav_cadence_sites[i].rva&15u;
        assert(!memcmp(before,after,offset) && !memcmp(before+offset+2,after+offset+2,14-offset));
        assert(!nav_cadence_swap(i,nav_cadence_sites[i].original,value)); /* expected value no longer there */
        assert(nav_cadence_swap(i,value,nav_cadence_sites[i].original) && nav_cadence_site(i)==nav_cadence_sites[i].original);
    }
    /* A failed install rolls back the sites it already wrote. */
    uint16_t foreign=0xcccc;memcpy(image+nav_cadence_sites[2].rva,&foreign,2);
    assert(!nav_cadence_install_sites(NAV_CADENCE_FAR));
    assert(nav_cadence_site(0)==nav_cadence_sites[0].original && nav_cadence_site(1)==nav_cadence_sites[1].original && nav_cadence_site(2)==0xcccc);
    test_nc_write_sites(NAV_CADENCE_OBSERVE);
    assert(nav_cadence_install_sites(NAV_CADENCE_EVERY) && nav_cadence_sites_are(NAV_CADENCE_EVERY) &&
           nav_cadence_site(1)==nav_cadence_sites[1].original && nav_cadence_site(3)==nav_cadence_sites[3].original);
    /* Tick check, ownership and restore. */
    nav_cadence_installed=nav_cadence_patched=1;nav_cadence_mode=NAV_CADENCE_EVERY;nav_cadence_active=1;
    assert(nav_cadence_owned());
    memcpy(image+nav_cadence_sites[0].rva,&foreign,2);assert(!nav_cadence_owned());
    assert(!nav_cadence_check() && !strcmp(nav_cadence_stage,"site_changed") && nav_cadence_failures==1);
    test_nc_write_sites(NAV_CADENCE_EVERY);assert(!nav_cadence_check()); /* a counted failure stays */
    nav_cadence_failures=0;assert(!nav_cadence_check() && !strcmp(nav_cadence_stage,"block_changed")); /* zero image: unpinned */
    memcpy(image+nav_cadence_sites[0].rva,&foreign,2);nav_cadence_restore();
    assert(!nav_cadence_active && nav_cadence_installed && nav_cadence_patched && !nav_cadence_restored);
    assert(nav_cadence_site(2)==nav_cadence_sites[2].original && nav_cadence_site(0)==0xcccc); /* the rest went back */
    test_nc_write_sites(NAV_CADENCE_EVERY);nav_cadence_restore();
    assert(nav_cadence_restored && !nav_cadence_installed && !nav_cadence_patched && nav_cadence_sites_are(NAV_CADENCE_OBSERVE));
    /* finish() restores the sites; a foreign site keeps stats.restored at 0. */
    test_nc_write_sites(NAV_CADENCE_FAR);nav_cadence_mode=NAV_CADENCE_FAR;nav_cadence_installed=nav_cadence_patched=1;nav_cadence_active=1;nav_cadence_restored=0;
    uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(95);
    assert(stats.reason==95 && !stats.active && stats.restored==1 && !nav_cadence_installed && nav_cadence_restored && nav_cadence_sites_are(NAV_CADENCE_OBSERVE));
    test_nc_write_sites(NAV_CADENCE_FAR);memcpy(image+nav_cadence_sites[3].rva,&foreign,2);
    nav_cadence_installed=nav_cadence_patched=1;nav_cadence_active=1;nav_cadence_restored=0;stats.active=1;stats.restored=1;finish(95);
    assert(!stats.restored && nav_cadence_installed && !nav_cadence_restored && nav_cadence_site(3)==0xcccc);
    /* Observe never writes a site. */
    test_nc_write_sites(NAV_CADENCE_OBSERVE);nav_cadence_patched=0;nav_cadence_mode=NAV_CADENCE_OBSERVE;assert(nav_cadence_owned());
    nav_cadence_restore();assert(nav_cadence_restored && !nav_cadence_installed && nav_cadence_sites_are(NAV_CADENCE_OBSERVE));
    /* Status bounds at the widest values. */
    for(unsigned k=0;k<2;k++) {
        nav_cadence_counts *w=&nav_cadence_frame[k];w->calls=UINT64_MAX;
        for(unsigned b=0;b<2;b++) w->refresh[b]=w->now[b]=w->mid[b]=w->slow[b]=UINT64_MAX;
        nav_cadence_total[k]=*w;
    }
    nav_cadence_tick=stats.issued=UINT64_MAX-1;nav_cadence_first_now=nav_cadence_first_mid=nav_cadence_unreadable=UINT64_MAX;
    nav_cadence_failures=INT64_MAX;nav_cadence_stage="foreign_module";nav_cadence_mode=NAV_CADENCE_OBSERVE;
    n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE NAV_CADENCE tick=18446744073709551615 \n")<2048);
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) memset(image+(nav_cadence_sites[i].rva&~15u),0,16);
    memcpy(image+NAV_ENTRY,&saved_entry,8);nav_installed=saved_nav;test_nc_reset();
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;stats.reason=saved_reason;stats.restored=0;
    printf("nav cadence counters/arming/aligned CAS/rollback/check/finish/status tests PASS\n");
}
static void test_nav_cadence_native_image(const char *path) {
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
    assert(nav_cadence_pins() && nav_cadence_sites_are(NAV_CADENCE_OBSERVE));
    /* The scheduling the sites select. Block 1 (parameters, ctrl+0xd0 when ctrl+0xd8). */
    int32_t rel;float f;double d;
    assert(!memcmp(image+0xea78ac,"\x41\x38\xb6\xd8\x00\x00\x00",7) && !memcmp(image+0xea787d,"\x0f\x57\xf6",3)); /* enable byte; xmm6 = 0 */
    memcpy(&rel,image+0xea791b,4);assert(0xea791f+rel==0xea1e80);             /* parameter build first */
    memcpy(&rel,image+0xea7954,4);assert(0xea7958+rel==0x3e3a60);             /* camera distance */
    memcpy(&rel,image+0xea7879,4);memcpy(&f,image+0xea787d+rel,4);assert(f==1000000.0f);    /* xmm8: 1 km squared */
    memcpy(&rel,image+0xea7884,4);memcpy(&f,image+0xea7888+rel,4);assert(f==25000000.0f);   /* xmm7: 5 km squared */
    memcpy(&rel,image+0xea788d,4);memcpy(&f,image+0xea7891+rel,4);assert(f==0.5f);          /* xmm10 */
    memcpy(&rel,image+0xea7896,4);memcpy(&d,image+0xea789a+rel,8);assert(d==0.5);           /* xmm11 */
    memcpy(&rel,image+0xea789f,4);memcpy(&f,image+0xea78a3+rel,4);assert(f==1.0f);          /* xmm12 */
    memcpy(&rel,image+0xea78a8,4);memcpy(&d,image+0xea78ac+rel,8);assert(d==1.0);           /* xmm9 */
    assert(0xea795c+2+image[0xea795d]==0xea7963 && !memcmp(image+0xea795e,"\x0f\x57\xc0\xeb\x3c",5) && 0xea7963+0x3c==0xea799f);
    assert(0xea7977+2+image[0xea7978]==0xea798d && !memcmp(image+0xea7979,"\x41\x0f\x28\xca",4) && !memcmp(image+0xea798d,"\x41\x0f\x28\xcc",4));
    memcpy(&rel,image+0xea797e,4);assert(0xea7982+rel==0x14ddc90);memcpy(&rel,image+0xea7992,4);assert(0xea7996+rel==0x14ddc90);
    assert(!memcmp(image+0xea799a,"\xf2\x41\x0f\x58\xc1",5) && !memcmp(image+0xea79a4,"\xf2\x41\x0f\x11\x86\xd0\x00\x00\x00",9));
    /* Block 2 (obstacle probes, (ctrl+0x48)+0x3e0 when +0x3e8). */
    assert(!memcmp(image+0xea7be2,"\x80\xba\xe8\x03\x00\x00\x00",7));
    memcpy(&rel,image+0xea7c03,4);assert(0xea7c07+rel==0x3e40a0);
    assert(0xea7c0b+2+image[0xea7c0c]==0xea7c4f && 0xea7c21+2+image[0xea7c22]==0xea7c3a);
    assert(!memcmp(image+0xea7c23,"\x41\x0f\x28\xca",4) && !memcmp(image+0xea7c3a,"\x41\x0f\x28\xcc",4));
    memcpy(&rel,image+0xea7c28,4);assert(0xea7c2c+rel==0x14ddc90);memcpy(&rel,image+0xea7c3f,4);assert(0xea7c43+rel==0x14ddc90);
    assert(!memcmp(image+0xea7c4a,"\xf2\x41\x0f\x58\xf1",5) && !memcmp(image+0xea7c58,"\xf2\x0f\x11\xb0\xe0\x03\x00\x00",8));
    /* Tampering in either block, the entry or a site with an unknown value refuses; our values are accepted. */
    const unsigned tamper[]={NAV_CADENCE_PIN_A,0xea7900,0xea7960,NAV_CADENCE_PIN_A+NAV_CADENCE_PIN_A_SIZE-1,NAV_CADENCE_PIN_B,0xea7c30,NAV_CADENCE_PIN_B+NAV_CADENCE_PIN_B_SIZE-1,NAV_MOVE+31};
    for(unsigned i=0;i<sizeof(tamper)/sizeof(*tamper);i++) {image[tamper[i]]^=1;assert(!nav_cadence_pins());image[tamper[i]]^=1;}
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {image[nav_cadence_sites[i].rva+1]^=0x40;assert(!nav_cadence_pins());image[nav_cadence_sites[i].rva+1]^=0x40;}
    test_nc_write_sites(NAV_CADENCE_FAR);assert(nav_cadence_pins());test_nc_write_sites(NAV_CADENCE_EVERY);assert(nav_cadence_pins());
    test_nc_write_sites(NAV_CADENCE_OBSERVE);
    /* Arm/check/restore cycles on the mapped image. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_nav=nav_installed;
    void *saved_entry;memcpy(&saved_entry,image+NAV_ENTRY,8);*(void**)(image+NAV_ENTRY)=(void*)nav_move;nav_installed=1;
    const int modes[3]={NAV_CADENCE_FAR,NAV_CADENCE_EVERY,NAV_CADENCE_OBSERVE};
    for(unsigned m=0;m<3;m++) {
        test_nc_reset();stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();
        assert(nav_cadence_arm(modes[m]) && nav_cadence_installed && nav_cadence_patched==(modes[m]!=NAV_CADENCE_OBSERVE));
        assert(nav_cadence_sites_are(modes[m]==NAV_CADENCE_OBSERVE?NAV_CADENCE_OBSERVE:modes[m]) && nav_cadence_check() && nav_cadence_owned());
        assert(nav_cadence_arm(modes[m]) && !nav_cadence_arm(modes[(m+1)%3])); /* identical rerun only */
        image[0xea7c30]^=1;assert(!nav_cadence_check() && !strcmp(nav_cadence_stage,"block_changed"));image[0xea7c30]^=1;
        nav_cadence_failures=0;assert(nav_cadence_check());
        nav_cadence_restore();assert(nav_cadence_restored && !nav_cadence_installed && nav_cadence_sites_are(NAV_CADENCE_OBSERVE) && nav_cadence_pins());
    }
    test_nc_reset();stats.active=1;test_nc_write_sites(NAV_CADENCE_FAR);
    assert(!nav_cadence_arm(NAV_CADENCE_FAR) && !strcmp(nav_cadence_stage,"site")); /* never arms over a patched site */
    test_nc_write_sites(NAV_CADENCE_OBSERVE);test_nc_reset();
    memcpy(image+NAV_ENTRY,&saved_entry,8);nav_installed=saved_nav;stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("nav cadence native pins, scheduling semantics, tamper refusals and far/every/observe cycles verified");
}
/* native_gate_nav_probe_init: pinned while avoidance is off, reset once per portable ID when on. */
static void test_nav_probe_init(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5151,0x74d83) && world_identity_record(0x5252,0x74d84));
    static unsigned char ship[0x40];unsigned char *s=ship,*probe=test_nc_probe;
    memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    memcpy(test_nc_controller+0x28,&s,8);memcpy(test_nc_controller+0x48,&probe,8);*(uint64_t*)(ship+8)=0x5151;
    /* Arming: after the cadence, before the first admitted frame, on the MoveWorker. */
    nav_probe_armed=0;nav_cadence_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!nav_probe_arm());nav_cadence_installed=1;stats.issued=1;assert(!nav_probe_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!nav_probe_arm());ui_thread=GetCurrentThreadId();
    assert(nav_probe_arm() && nav_probe_armed && nav_probe_arm());
    double due,v;
    /* Avoidance off: the loading-time deadline is pinned to "due now", once per value. */
    stats.issued=32;v=7.96;memcpy(test_nc_probe+0x3e0,&v,8);test_nc_probe[0x3e8]=0;
    nav_probe_apply(test_nc_controller,5.5);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==5.5-1.0/60.0 && nav_probe_pins==1 && nav_probe_count==1 && !nav_probe_inits && nav_probe_frame_pins==1);
    nav_probe_apply(test_nc_controller,5.5);assert(nav_probe_pins==1);
    /* On later: the known ID keeps the pinned value (vfunc33 probes at once). */
    test_nc_probe[0x3e8]=1;nav_probe_apply(test_nc_controller,5.6);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==5.5-1.0/60.0 && !nav_probe_inits);
    /* On at the first call of another ID: reset once, then left to vfunc33. */
    *(uint64_t*)(ship+8)=0x5252;v=9.0;memcpy(test_nc_probe+0x3e0,&v,8);stats.issued=33;
    nav_probe_apply(test_nc_controller,6.0);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==6.0-1.0/60.0 && nav_probe_inits==1 && nav_probe_first==33 && nav_probe_frame_inits==1 && !nav_probe_frame_pins && nav_probe_count==2);
    v=7.5;memcpy(test_nc_probe+0x3e0,&v,8);nav_probe_apply(test_nc_controller,6.1);memcpy(&due,test_nc_probe+0x3e0,8);assert(due==7.5 && nav_probe_inits==1);
    /* Unmapped ship: pinned while off, untouched while on. */
    *(uint64_t*)(ship+8)=0x9999;test_nc_probe[0x3e8]=0;nav_probe_apply(test_nc_controller,6.2);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==6.2-1.0/60.0 && nav_probe_unmapped==1 && nav_probe_count==2);
    test_nc_probe[0x3e8]=1;v=8.0;memcpy(test_nc_probe+0x3e0,&v,8);nav_probe_apply(test_nc_controller,6.3);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==8.0 && nav_probe_unmapped==2);
    /* No probe block or a non-finite time: nothing. */
    nav_probe_apply(test_nc_controller,NAN);memcpy(&due,test_nc_probe+0x3e0,8);assert(due==8.0);
    memset(test_nc_controller+0x48,0,8);test_nc_probe[0x3e8]=0;nav_probe_apply(test_nc_controller,6.4);memcpy(&due,test_nc_probe+0x3e0,8);assert(due==8.0);
    memcpy(test_nc_controller+0x48,&probe,8);
    /* Through nav_cadence_enter while the cadence is active. */
    nav_cadence_active=1;nav_cadence_sample c;nav_cadence_enter(test_nc_controller,6.5,&c);memcpy(&due,test_nc_probe+0x3e0,8);
    assert(due==6.5-1.0/60.0 && c.valid && c.probe==due && !c.probe_on && nav_probe_unmapped==3);
    /* A full table fails closed (stop reason 95). */
    nav_cadence_failures=0;unsigned saved_count=nav_probe_count;nav_probe_count=NAV_PROBE_TABLE/4*3;*(uint64_t*)(ship+8)=0x5151;
    world_identity_record(0x5353,0x74d85);*(uint64_t*)(ship+8)=0x5353;nav_probe_apply(test_nc_controller,6.6);
    assert(nav_cadence_failures==1 && !strcmp(nav_cadence_stage,"probe_table") && !nav_cadence_check());
    nav_probe_count=saved_count;nav_cadence_failures=0;nav_cadence_stage="idle";
    /* Status carries the counters. */
    char status[1536];int n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && strstr(status," probe_init=1 probe_frame=") && strstr(status," probe_inits=1 probe_ids=2 probe_unmapped=3 probe_first=33"));
    nav_probe_armed=0;nav_cadence_installed=0;nav_cadence_active=0;memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;
    memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("nav probe init pin/reset/unmapped/table/status tests PASS\n");
}
