/* native_gate_nav_player: the player's controller through the nav cadence
 * bracket (probe init, counts, its own rows), arming refusals, the entry CAS,
 * tick checks, restore, and the pinned PlayerController::vfunc33 sites. */
static double test_np_seen_due;
static unsigned test_np_original_calls;
static uint64_t *test_np_word;
/* Stands in for PlayerController::vfunc33: records the probe deadline it found,
 * then refreshes it to now + 1.5 and draws once from the word. */
static void test_np_original(void *self,double time) {
    unsigned char *probe=NULL;memcpy(&probe,(unsigned char*)self+0x48,8);
    ++test_np_original_calls;
    if(probe) {memcpy(&test_np_seen_due,probe+0x3e0,8);double next=time+1.5;memcpy(probe+0x3e0,&next,8);}
    if(test_np_word) *test_np_word=*test_np_word*6364136223846793005ULL+1442695040888963407ULL;
}
static void test_np_reset(void) {
    nav_player_installed=nav_player_restored=nav_player_seen=0;nav_player_calls=nav_player_failures=0;
    nav_player_admitted=nav_player_manual=nav_player_scoped=nav_player_unscoped=nav_player_drawn=nav_player_rows=nav_player_unmapped=0;
    nav_player_tick=UINT64_MAX;nav_player_frame_admitted=0;nav_player_stage="idle";nav_player_original_override=NULL;
}
static void test_nav_player(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x6161,0xae79c));
    static unsigned char ship[0x300];unsigned char *s=ship,*probe=test_nc_probe;
    memset(ship,0,sizeof(ship));memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    memcpy(test_nc_controller+0x28,&s,8);memcpy(test_nc_controller+0x48,&probe,8);*(uint64_t*)(ship+8)=0x6161;*(int*)(ship+0x1ec)=9;
    test_nc_controller[0xd8]=1;
    test_np_reset();test_nc_reset();nav_probe_armed=0;nav_cadence_rows=0;nav_cadence_watch_count=0;
    memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;nav_probe_pins=nav_probe_inits=nav_probe_unmapped=nav_probe_first=0;
    /* Outside an admitted MoveWorker frame: the original runs alone. */
    uint64_t word=0x1234;test_np_word=&word;test_np_original_calls=0;
    stats.active=0;boot_mode=1;boot_phase=3;stats.issued=40;ui_thread=GetCurrentThreadId();
    nav_player_run(test_np_original,test_nc_controller,5.0,&word);
    assert(test_np_original_calls==1 && nav_player_calls==1 && !nav_player_admitted && !nav_player_rows && !nav_cadence_rows);
    stats.active=1;boot_phase=2;nav_player_run(test_np_original,test_nc_controller,5.0,&word);assert(!nav_player_admitted);
    boot_phase=3;ui_thread=GetCurrentThreadId()+1;nav_player_run(test_np_original,test_nc_controller,5.0,&word);assert(!nav_player_admitted);
    ui_thread=GetCurrentThreadId();
    /* Manual flight (autopilot flag 0): counted, passed through, nothing bracketed. */
    nav_cadence_active=1;nav_probe_armed=1;double v0=7.96;memcpy(test_nc_probe+0x3e0,&v0,8);
    nav_player_run(test_np_original,test_nc_controller,5.0,&word);
    assert(nav_player_admitted==1 && nav_player_manual==1 && !nav_player_unscoped && !nav_player_drawn && !nav_probe_pins &&
           !nav_cadence_total[1].calls && test_np_seen_due==7.96);
    nav_cadence_active=0;nav_probe_armed=0;nav_player_admitted=nav_player_manual=0;nav_player_tick=UINT64_MAX;--nav_player_calls;--test_np_original_calls;
    uint64_t autopilot=1;memcpy(image+NAV_PLAYER_AUTOPILOT,&autopilot,8);
    /* Admitted with the cadence inactive: counted, but nothing read and no row. */
    nav_player_run(test_np_original,test_nc_controller,5.0,&word);
    assert(test_np_original_calls==4 && nav_player_admitted==1 && nav_player_unscoped==1 && nav_player_drawn==1 && !nav_cadence_rows);
    /* Cadence active with the probe init armed: avoidance off pins the loading
     * deadline to "due now" before the original runs; the row carries the
     * deadline the original left (id=0, saved = the player ship's key). */
    nav_cadence_active=1;nav_cadence_installed=1;nav_probe_armed=1;stats.issued=41;
    double v=7.96;memcpy(test_nc_probe+0x3e0,&v,8);test_nc_probe[0x3e8]=0;
    nav_player_run(test_np_original,test_nc_controller,6.0,&word);
    assert(test_np_seen_due==6.0-1.0/60.0 && nav_probe_pins==1 && nav_cadence_total[1].calls==1 && nav_cadence_total[0].calls==0);
    assert(nav_player_rows==1 && nav_cadence_rows==1 && nav_player_frame_admitted==1 && nav_player_tick==41);
    char row[256];assert(nav_cadence_row(0,row,sizeof(row))>0);
    assert(!strcmp(row,"index=0 tick=41 id=0 saved=00000000000ae79c attention=9 params_on=1 params_due=0000000000000000 probe=1 probe_on=0 probe_due=401e000000000000 body=0 zone_scene=3 behaviour=ffffffff avoid=255"));
    /* Avoidance on at the first call of the ID: reset once, then left to the original. */
    test_nc_probe[0x3e8]=1;v=9.25;memcpy(test_nc_probe+0x3e0,&v,8);nav_probe_count=0;memset(nav_probe_table,0,sizeof(nav_probe_table));
    nav_player_run(test_np_original,test_nc_controller,6.5,&word);
    assert(test_np_seen_due==6.5-1.0/60.0 && nav_probe_inits==1 && nav_player_rows==2);
    nav_player_run(test_np_original,test_nc_controller,6.6,&word);assert(test_np_seen_due==8.0 && nav_probe_inits==1 && nav_player_rows==3);
    /* An unchanged state gives no row: the stand-in leaves time + 1.5 again. */
    nav_player_run(test_np_original,test_nc_controller,6.6,&word);assert(nav_player_rows==3 && nav_cadence_rows==3);
    /* Inside a ship's actor_all umbrella: counted as scoped; a call without a draw is not drawn. */
    ++actor_all_ship_depth;test_np_word=NULL;nav_player_run(test_np_original,test_nc_controller,6.6,&word);--actor_all_ship_depth;
    assert(nav_player_scoped==1 && nav_player_drawn==5 && nav_player_admitted==6);
    /* No readable word: a draw is not counted. */
    test_np_word=&word;nav_player_run(test_np_original,test_nc_controller,6.6,NULL);assert(nav_player_drawn==5 && nav_player_unscoped==6);
    /* A watched ship gets the watch row only (one per change, not two). */
    assert(nav_cadence_watch_add(0xae79c));unsigned before=nav_cadence_rows;
    nav_player_run(test_np_original,test_nc_controller,7.0,&word);
    assert(nav_cadence_rows==before+1 && nav_player_rows==3 && nav_cadence_ring[before%NAV_CADENCE_RING].saved==0xae79c);
    nav_cadence_watch_count=0;memset(nav_cadence_watch_seen,0,sizeof(nav_cadence_watch_seen));
    /* An unmapped ship: counted, no row. */
    *(uint64_t*)(ship+8)=0x9999;before=nav_cadence_rows;nav_player_run(test_np_original,test_nc_controller,7.5,&word);
    assert(nav_player_unmapped==1 && nav_cadence_rows==before);*(uint64_t*)(ship+8)=0x6161;
    char status[512];int n=nav_player_status(status,sizeof(status));
    assert(n>0 && strstr(status,"installed=0 frame=41 frame_calls=8 calls=12 admitted=9 manual=0 scoped=1 unscoped=8 drawn=7 rows=3 unmapped=1 restored=0 stage=idle failures=0"));
    /* Arming: after the cadence, before the first admitted frame, on the MoveWorker; pins and the entry. */
    test_np_reset();nav_cadence_installed=0;stats.issued=0;boot_phase=0;
    assert(!nav_player_arm());nav_cadence_installed=1;stats.active=0;assert(!nav_player_arm());stats.active=1;
    stats.issued=1;assert(!nav_player_arm());stats.issued=0;ui_thread=GetCurrentThreadId()+1;assert(!nav_player_arm());
    ui_thread=GetCurrentThreadId();boot_phase=1;assert(!nav_player_arm());boot_phase=0;
    assert(!nav_player_arm() && !strcmp(nav_player_stage,"pins") && !nav_player_installed); /* zero image: unpinned */
    /* Ownership, tick check and restore on a hand-set entry. */
    void *saved_entry;memcpy(&saved_entry,image+NAV_PLAYER_ENTRY,8);
    *(void**)(image+NAV_PLAYER_ENTRY)=(void*)nav_player_move;nav_player_installed=1;assert(nav_player_owned());
    assert(!nav_player_check() && !strcmp(nav_player_stage,"block_changed") && nav_player_failures==1);
    nav_player_failures=0;*(void**)(image+NAV_PLAYER_ENTRY)=(void*)test_np_original;assert(!nav_player_owned());
    assert(!nav_player_check() && !strcmp(nav_player_stage,"entry_changed"));
    nav_player_restore();assert(nav_player_installed && !nav_player_restored && *(void**)(image+NAV_PLAYER_ENTRY)==(void*)test_np_original);
    *(void**)(image+NAV_PLAYER_ENTRY)=(void*)nav_player_move;nav_player_restore();
    assert(!nav_player_installed && nav_player_restored && *(void**)(image+NAV_PLAYER_ENTRY)==image+NAV_PLAYER_MOVE);
    /* finish() gives the entry back and counts it in stats.restored. */
    *(void**)(image+NAV_PLAYER_ENTRY)=(void*)nav_player_move;nav_player_installed=1;nav_player_restored=0;nav_cadence_installed=0;
    uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;finish(95);
    assert(stats.reason==95 && !nav_player_installed && nav_player_restored && *(void**)(image+NAV_PLAYER_ENTRY)==image+NAV_PLAYER_MOVE);
    *(void**)(image+NAV_PLAYER_ENTRY)=(void*)test_np_original;nav_player_installed=1;nav_player_restored=0;stats.active=1;stats.restored=1;finish(95);
    assert(!stats.restored && nav_player_installed && !nav_player_restored);
    /* Status bounds at the widest values. */
    nav_player_calls=nav_player_failures=INT64_MAX;nav_player_tick=stats.issued=UINT64_MAX-1;
    nav_player_admitted=nav_player_manual=nav_player_scoped=nav_player_unscoped=nav_player_drawn=nav_player_rows=nav_player_unmapped=nav_player_frame_admitted=UINT64_MAX;
    nav_player_stage="foreign_module";n=nav_player_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE NAV_PLAYER tick=18446744073709551615 \n")<512);
    memcpy(image+NAV_PLAYER_ENTRY,&saved_entry,8);memset(image+NAV_PLAYER_AUTOPILOT,0,8);test_np_reset();test_nc_reset();nav_probe_armed=0;nav_cadence_rows=0;
    memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;nav_probe_pins=nav_probe_inits=nav_probe_unmapped=nav_probe_first=0;
    memset(nav_cadence_seen,0,sizeof(nav_cadence_seen));memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("nav player bracket/probe init/rows/watch/scoped/arming/check/restore/status tests PASS\n");
}
/* With the X4 9.00 executable: the relocated slot-33 entry, the pinned
 * prologue (time kept in xmm7), the autopilot branch and the direct call of
 * NavigationController::vfunc33, tamper refusals and a full arm/restore. */
static void test_nav_player_native_image(const char *path) {
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
    /* The file holds preferred-base pointers: slot 33 of both controller vtables. */
    assert(NAV_PLAYER_ENTRY==0x2c39488u+33u*8u && NAV_ENTRY==0x2c51158u+33u*8u);
    assert(*(uint64_t*)(image+NAV_PLAYER_ENTRY)==nt.OptionalHeader.ImageBase+NAV_PLAYER_MOVE);
    assert(*(uint64_t*)(image+NAV_ENTRY)==nt.OptionalHeader.ImageBase+NAV_MOVE);
    *(void**)(image+NAV_PLAYER_ENTRY)=image+NAV_PLAYER_MOVE;
    assert(nav_player_pins());
    /* movaps xmm7,xmm1 ends the pinned prologue; the autopilot test reads 0x2f4b7d8
     * and jumps to the Agent branch, whose call passes xmm7 and this to 0xea77f0. */
    assert(!memcmp(image+NAV_PLAYER_MOVE+NAV_PLAYER_HEAD-3,"\x0f\x28\xf9",3));
    int32_t rel;memcpy(&rel,image+NAV_PLAYER_BRANCH+3,4);assert(NAV_PLAYER_BRANCH+7+rel==NAV_PLAYER_AUTOPILOT);
    assert(!memcmp(image+0xd97f39,"\x45\x33\xff",3)); /* xor r15d,r15d (no later write before 0xd981bb): the flag is compared with 0 */
    memcpy(&rel,image+NAV_PLAYER_BRANCH+9,4);assert(NAV_PLAYER_BRANCH+13+rel==NAV_PLAYER_CALL);
    memcpy(&rel,image+NAV_PLAYER_CALL+7,4);assert(image[NAV_PLAYER_CALL+6]==0xe8 && NAV_PLAYER_CALL+11+rel==NAV_MOVE);
    const unsigned tampers[]={NAV_PLAYER_MOVE,NAV_PLAYER_MOVE+NAV_PLAYER_HEAD-1,NAV_PLAYER_BRANCH,NAV_PLAYER_BRANCH+12,NAV_PLAYER_CALL,NAV_PLAYER_CALL+10};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!nav_player_pins());image[tampers[i]]^=1;}
    assert(nav_player_pins());
    /* Full arm and restore on the copy: only the entry changes; a foreign owner refuses. */
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    test_np_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();nav_cadence_installed=1;
    *(void**)(image+NAV_PLAYER_ENTRY)=(void*)test_np_original;assert(!nav_player_arm() && !strcmp(nav_player_stage,"entry"));
    *(void**)(image+NAV_PLAYER_ENTRY)=image+NAV_PLAYER_MOVE;
    assert(nav_player_arm() && nav_player_installed && *(void**)(image+NAV_PLAYER_ENTRY)==(void*)nav_player_move && nav_player_owned());
    assert(nav_player_arm() && nav_player_check());
    image[NAV_PLAYER_CALL+7]^=1;assert(!nav_player_check() && !strcmp(nav_player_stage,"block_changed"));image[NAV_PLAYER_CALL+7]^=1;
    nav_player_failures=0;assert(nav_player_check());
    nav_player_restore();assert(nav_player_restored && !nav_player_installed && *(void**)(image+NAV_PLAYER_ENTRY)==image+NAV_PLAYER_MOVE);
    test_np_reset();nav_cadence_installed=0;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("nav player native entry, pinned prologue/autopilot branch/direct call, tamper refusals and arm/restore verified");
}
/* native_gate_nav_params_init: the parameter refresh deadline (ctrl+0xd0),
 * pinned while the block is off and reset once per portable ID when on, for
 * every bracketed controller; the fly7a player case through the player bracket. */
static double test_np_seen_params;
static void test_np_params_original(void *self,double time) {
    memcpy(&test_np_seen_params,(unsigned char*)self+0xd0,8);
    if(((unsigned char*)self)[0xd8] && test_np_seen_params<=time) {double next=time+1.25;memcpy((unsigned char*)self+0xd0,&next,8);}
}
static void test_nav_params_init(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x6161,0xae79c) && world_identity_record(0x5151,0x74d83));
    static unsigned char ship[0x300];unsigned char *s=ship;
    memset(ship,0,sizeof(ship));memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    memcpy(test_nc_controller+0x28,&s,8);*(uint64_t*)(ship+8)=0x5151;
    /* Arming: after the cadence, before the first admitted frame, on the MoveWorker. */
    nav_params_armed=0;nav_cadence_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!nav_params_arm());nav_cadence_installed=1;stats.issued=1;assert(!nav_params_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!nav_params_arm());ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!nav_params_arm());boot_phase=0;
    assert(nav_params_arm() && nav_params_armed && nav_params_arm() && !nav_probe_armed);
    double due,v;
    /* Parameters off: the loading deadline is pinned to "due now", once per value. */
    stats.issued=32;v=8.66;memcpy(test_nc_controller+0xd0,&v,8);test_nc_controller[0xd8]=0;
    nav_params_apply(test_nc_controller,5.5);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==5.5-1.0/60.0 && nav_params_pins==1 && nav_params_count==1 && !nav_params_inits && nav_params_frame_pins==1);
    nav_params_apply(test_nc_controller,5.5);assert(nav_params_pins==1);
    /* On later: the known ID keeps the pinned value (vfunc33 builds at once). */
    test_nc_controller[0xd8]=1;nav_params_apply(test_nc_controller,5.6);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==5.5-1.0/60.0 && !nav_params_inits);
    /* On at the first call of another ID: reset once, then left to vfunc33. */
    *(uint64_t*)(ship+8)=0x6161;v=8.2762;memcpy(test_nc_controller+0xd0,&v,8);stats.issued=33;
    nav_params_apply(test_nc_controller,6.0);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==6.0-1.0/60.0 && nav_params_inits==1 && nav_params_first==33 && nav_params_frame_inits==1 && !nav_params_frame_pins && nav_params_count==2);
    v=7.5;memcpy(test_nc_controller+0xd0,&v,8);nav_params_apply(test_nc_controller,6.1);memcpy(&due,test_nc_controller+0xd0,8);assert(due==7.5 && nav_params_inits==1);
    /* The probe init keeps its own table: the same ID is new there. */
    assert(nav_probe_first_call(0xae79c)==1 && nav_probe_first_call(0xae79c)==0 && nav_params_count==2);
    memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;
    /* Unmapped ship: pinned while off, untouched while on. */
    *(uint64_t*)(ship+8)=0x9999;test_nc_controller[0xd8]=0;nav_params_apply(test_nc_controller,6.2);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==6.2-1.0/60.0 && nav_params_unmapped==1 && nav_params_count==2);
    test_nc_controller[0xd8]=1;v=8.0;memcpy(test_nc_controller+0xd0,&v,8);nav_params_apply(test_nc_controller,6.3);memcpy(&due,test_nc_controller+0xd0,8);
    assert(due==8.0 && nav_params_unmapped==2);
    nav_params_apply(test_nc_controller,NAN);memcpy(&due,test_nc_controller+0xd0,8);assert(due==8.0);
    /* fly7a: the player's controller through native_gate_nav_player. Two clients
     * load 8.2762 and 8.6583; both build their parameters in the first admitted
     * frame and agree on every later deadline. */
    double after[2];
    for(unsigned client=0;client<2;client++) {
        memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;nav_params_inits=0;
        test_np_reset();nav_cadence_active=1;nav_cadence_rows=0;
        *(uint64_t*)(ship+8)=0x6161;test_nc_controller[0xd8]=1;v=client?8.6583:8.2762;memcpy(test_nc_controller+0xd0,&v,8);
        stats.active=1;boot_phase=3;stats.issued=1;uint64_t autopilot=1;memcpy(image+NAV_PLAYER_AUTOPILOT,&autopilot,8);
        nav_player_run(test_np_params_original,test_nc_controller,7.1,NULL);
        assert(test_np_seen_params==7.1-1.0/60.0 && nav_params_inits==1);
        stats.issued=2;nav_player_run(test_np_params_original,test_nc_controller,7.1+1.0/60.0,NULL);
        memcpy(&after[client],test_nc_controller+0xd0,8);
    }
    assert(after[0]==after[1] && after[0]==7.1+1.25);memset(image+NAV_PLAYER_AUTOPILOT,0,8);
    /* A fixture re-based by native_gate_nav_initial_refresh gets the same value. */
    {
        nav_sample sample;memset(&sample,0,sizeof(sample));int saved_initial=nav_initial_refresh,saved_seeded=nav_seeded;
        unsigned saved_init=nav_initialized[0],saved_inits=nav_inits;nav_initial_refresh=1;nav_seeded=0;nav_initialized[0]=0;
        memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;*(uint64_t*)(ship+8)=0x5151;
        v=9.0;memcpy(test_nc_controller+0xd0,&v,8);nav_params_apply(test_nc_controller,7.2);
        double ours;memcpy(&ours,test_nc_controller+0xd0,8);
        nav_execute(test_np_params_original,&sample,1,NULL,test_nc_controller,7.2);
        assert(sample.initialized==1 && ours==7.2-1.0/60.0 && test_np_seen_params==ours);
        nav_initial_refresh=saved_initial;nav_seeded=saved_seeded;nav_initialized[0]=saved_init;nav_inits=saved_inits;
    }
    /* A full table fails closed (stop reason 95). */
    nav_cadence_failures=0;nav_params_count=NAV_PROBE_TABLE/4*3;world_identity_record(0x5353,0x74d85);*(uint64_t*)(ship+8)=0x5353;
    nav_params_apply(test_nc_controller,7.3);
    assert(nav_cadence_failures==1 && !strcmp(nav_cadence_stage,"params_table") && !nav_cadence_check());
    nav_cadence_failures=0;nav_cadence_stage="idle";
    /* Status carries the counters, also at the widest values within the Lua buffer. */
    char status[1536];int n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && strstr(status," params_init=1 params_frame=") && strstr(status," params_ids="));
    nav_probe_armed=nav_params_armed=INT_MAX;nav_probe_tick=nav_params_tick=stats.issued=UINT64_MAX-1;nav_probe_count=nav_params_count=UINT32_MAX;
    nav_probe_pins=nav_probe_inits=nav_probe_unmapped=nav_probe_first=nav_probe_frame_pins=nav_probe_frame_inits=UINT64_MAX;
    nav_params_pins=nav_params_inits=nav_params_unmapped=nav_params_first=nav_params_frame_pins=nav_params_frame_inits=UINT64_MAX;
    for(unsigned k=0;k<2;k++) {
        nav_cadence_counts *w=&nav_cadence_frame[k];w->calls=UINT64_MAX;
        for(unsigned b=0;b<2;b++) w->refresh[b]=w->now[b]=w->mid[b]=w->slow[b]=UINT64_MAX;
        nav_cadence_total[k]=*w;
    }
    nav_cadence_tick=UINT64_MAX-1;nav_cadence_first_now=nav_cadence_first_mid=nav_cadence_unreadable=UINT64_MAX;
    nav_cadence_failures=INT64_MAX;nav_cadence_stage="foreign_module";nav_cadence_mode=NAV_CADENCE_OBSERVE;
    n=nav_cadence_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE NAV_CADENCE tick=18446744073709551615 \n")<2048);
    test_nc_reset();test_np_reset();nav_probe_armed=nav_params_armed=0;nav_probe_tick=nav_params_tick=UINT64_MAX;
    memset(nav_probe_table,0,sizeof(nav_probe_table));memset(nav_params_table,0,sizeof(nav_params_table));nav_probe_count=nav_params_count=0;
    nav_probe_pins=nav_probe_inits=nav_probe_unmapped=nav_probe_first=nav_probe_frame_pins=nav_probe_frame_inits=0;
    nav_params_pins=nav_params_inits=nav_params_unmapped=nav_params_first=nav_params_frame_pins=nav_params_frame_inits=0;
    memset(nav_cadence_frame,0,sizeof(nav_cadence_frame));memset(nav_cadence_total,0,sizeof(nav_cadence_total));nav_cadence_rows=0;
    memset(nav_cadence_seen,0,sizeof(nav_cadence_seen));memset(test_nc_controller,0,sizeof(test_nc_controller));memset(test_nc_probe,0,sizeof(test_nc_probe));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("nav params init pin/reset/unmapped/table/player bracket/fixture refresh/status tests PASS\n");
}
