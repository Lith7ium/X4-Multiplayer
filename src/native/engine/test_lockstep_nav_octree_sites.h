/* native_gate_nav_octree_sites: counts by (phase menu/load/run, site, return address, class,
 * thread), objects without an ID, the last object keyed when printed, the
 * first and last tick, per-site totals, a full table, the last error kept,
 * arming refusals, restore, and status and row bounds. */
static void test_nav_octree_sites(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread,saved_main=main_thread;uint32_t saved_active=stats.active;
    int saved_boot=boot_mode,saved_phase=boot_phase;LONG saved_open=path_pass_open;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0x6a0001,0xa7351));
    static unsigned char ship[0x40],zone[0x40],anonymous[0x40];
    *(void**)ship=image+0x2b7feb8;*(uint64_t*)(ship+8)=0x6a0001;*(void**)zone=image+0x2b66080;*(uint64_t*)(zone+8)=0x6a0002;
    *(void**)anonymous=image+0x2b66080;
    /* Arming: the octree stubs installed, before the load, on the MoveWorker. */
    nos_armed=0;nos_active=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();main_thread=GetCurrentThreadId()+1;
    assert(!nos_arm(0));stats.issued=1;assert(!nos_arm(1));stats.issued=0;boot_phase=2;assert(!nos_arm(1));boot_phase=0;
    ui_thread=GetCurrentThreadId()+2;assert(!nos_arm(1));ui_thread=GetCurrentThreadId();
    nos_note(8,image+0x75356e,ship);assert(!nos_used); /* not armed: nothing */
    assert(nos_arm(1) && nos_armed && nos_active && nos_arm(1));
    /* Menu phase (boot phases 0 and 1: the main menu's universe): counted apart. */
    nos_note(8,image+0x519696,ship);boot_phase=1;nos_note(8,image+0x519696,ship);
    assert(nos_used==1 && nos_totals[NOS_MENU][8]==2 && !nos_totals[NOS_LOAD][8]);
    /* Load phase (loading observed, no admitted tick): the tail jump twice on the MoveWorker, once on a job thread, an anonymous zone. */
    boot_phase=2;SetLastError(0x77);nos_note(8,image+0x75356e,ship);assert(GetLastError()==0x77);
    nos_note(8,image+0x75356e,ship);ui_thread=GetCurrentThreadId()+2;path_pass_open=0;nos_note(8,image+0x75356e,ship);
    ui_thread=GetCurrentThreadId();nos_note(4,image+0x7450d8,anonymous);nos_note(8,image+0x51c25f,ship);
    assert(nos_used==5 && nos_totals[NOS_LOAD][8]==4 && nos_totals[NOS_LOAD][4]==1 && !nos_totals[NOS_RUN][8]);
    /* Run phase: admitted tick 7, then 9; a sector destructor; an unknown site goes to 'other'. */
    boot_phase=3;stats.issued=7;nos_note(5,image+0x74525a,zone);stats.issued=9;nos_note(5,image+0x74525a,zone);nos_note(7,image+0x784696,zone);
    nos_note(42,(void*)16,NULL);
    assert(nos_used==8 && nos_totals[NOS_RUN][5]==2 && nos_totals[NOS_RUN][7]==1 && nos_totals[NOS_RUN][9]==1);
    char row[320];unsigned found=0;
    for(unsigned i=0;i<NOS_ENTRIES;i++) {
        assert(nos_row(i,row,sizeof(row))>0);
        if(strstr(row," free")) continue;
        ++found;
        if(strstr(row,"phase=load site=tail from=75356e class=02b7feb8 thread=moveworker "))
            assert(strstr(row," count=2 noid=0 last=s:00000000000a7351 runtime=00000000006a0001 first=0 last_tick=0"));
        if(strstr(row,"phase=load site=tail from=75356e class=02b7feb8 thread=other ")) assert(strstr(row," count=1 noid=0 "));
        if(strstr(row,"phase=load site=7450d3 from=7450d8 class=02b66080 thread=moveworker "))
            assert(strstr(row," count=1 noid=1 last=-:0000000000000000 runtime=0000000000000000 "));
        if(strstr(row,"phase=run site=745255 from=74525a class=02b66080 "))
            assert(strstr(row," count=2 noid=0 last=-:0000000000000000 runtime=00000000006a0002 first=7 last_tick=9"));
        if(strstr(row," site=other ")) assert(strstr(row,"from=ffffffff class=ffffffff ") && strstr(row," noid=1 "));
        if(strstr(row,"phase=menu ")) assert(strstr(row," site=tail from=519696 class=02b7feb8 thread=moveworker count=2 "));
    }
    assert(found==8 && !nos_row(NOS_ENTRIES,row,sizeof(row)));
    char status[768];int n=nos_status(status,sizeof(status));
    assert(n>0 && !strcmp(status,"armed=1 active=1 entries=8 overflow=0 menu=7450d3:0,745255:0,753510:0,784691:0,tail:2,other:0 "
        "load=7450d3:1,745255:0,753510:0,784691:0,tail:4,other:0 run=7450d3:0,745255:2,753510:0,784691:1,tail:0,other:1"));
    /* A full table: further keys are only counted in the totals. */
    for(unsigned i=0;i<NOS_ENTRIES;i++) {*(void**)zone=image+0x100000+i*8;nos_note(6,image+0x753515,zone);}
    assert(nos_used==NOS_ENTRIES && nos_overflow==8 && nos_totals[NOS_RUN][6]==NOS_ENTRIES);
    /* Widest row and status; restore stops counting. */
    nos_entries[0]=(nos_entry_t){UINT64_MAX,UINT64_MAX,0x6a0001,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,NOS_MENU,4,PATH_THREAD_MOVEWORKER,1};
    n=nos_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE NAV_OCTREE_SITE at=first \n")<320);
    for(unsigned p=0;p<NOS_PHASES;p++) for(unsigned s=0;s<NOS_SITES;s++) nos_totals[p][s]=UINT64_MAX;
    nos_used=UINT_MAX;nos_overflow=UINT64_MAX;n=nos_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE NAV_OCTREE_SITES_STOP \n")<768);
    nos_restore();uint64_t total=nos_totals[NOS_RUN][6];nos_note(6,image+0x753515,zone);assert(!nos_active && nos_totals[NOS_RUN][6]==total);
    nos_armed=0;memset(nos_entries,0,sizeof(nos_entries));memset(nos_totals,0,sizeof(nos_totals));nos_used=0;nos_overflow=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;main_thread=saved_main;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    path_pass_open=saved_open;
    printf("nav octree sites menu/load/run phases, site/caller/class/thread/noid/keyed/totals/overflow/last-error/arming/restore/status tests PASS\n");
}
