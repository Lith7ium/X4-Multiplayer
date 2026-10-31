/* native_gate_draw_watch: a real write breakpoint on this thread's word while a
 * watched ship scope is open in an admitted frame (vectored handler, self test
 * at the first such scope, rows with rip, chain and stored value), nothing
 * before the first admitted frame, for other ships or after the scope, the
 * fail-safe for stray breakpoints (an unwound scope, a set left behind) and
 * their classification and rows (also a trap-flag single step), the left-set
 * check, and the observer hook in actor_all's ship scope. */
static volatile uint64_t *test_dw_word;
static __attribute__((noinline)) void test_dw_draw(uint64_t value) {*test_dw_word=value;}
static __attribute__((noinline)) void test_dw_ship(void *self) {(void)self;test_dw_draw(0x1111);test_dw_draw(0x2222);}
static __attribute__((noinline)) void test_dw_ship_throw(void *self) {(void)self;test_dw_draw(0x3333);RaiseException(0xe0445754,0,0,NULL);}
static __attribute__((noinline)) void test_dw_step(void) {
    __builtin_ia32_writeeflags_u64(__builtin_ia32_readeflags_u64()|0x100);__asm__ volatile("nop");
}
static void test_dw_scope_throw(unsigned char *ship,uint64_t *natural) {
    __try {actor_all_ship_execute(test_dw_ship_throw,ship,natural);}
    __except(GetExceptionCode()==0xe0445754?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {}
}
/* Frame mode: seed, frame-level draws, a keyed scope in between. Real breakpoint. */
static uint64_t test_dw_pcg(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static void test_draw_watch_frame(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    dw_id_count=0;dw_armed=0;dw_frame_armed=0;ui_thread=GetCurrentThreadId();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    volatile uint64_t word=0x1234567890abcdefULL;test_dw_word=&word;
    /* Arming: before the first admitted frame, not beside a list. */
    dw_id_count=1;assert(!dw_frame_arm());dw_id_count=0;
    stats.issued=1;assert(!dw_frame_arm());stats.issued=0;
    assert(dw_frame_arm() && dw_frame_armed && dw_handle && !strcmp(dw_stage,"armed_frame") && !dw_arm() && dw_frame_arm());
    /* Inert before an admitted frame, off the MoveWorker. */
    dw_frame_begin((uint64_t*)&word);assert(!dw_active && !dw_proven);
    stats.issued=500;boot_phase=2;dw_frame_begin((uint64_t*)&word);assert(!dw_active);boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;dw_frame_begin((uint64_t*)&word);assert(!dw_active);ui_thread=GetCurrentThreadId();
    test_dw_draw(word);assert(!dw_rows && !dw_strays);
    /* A frame: two frame-level draws around a keyed scope; only those two are rows, with their ordinals. */
    const uint64_t seed=0x1111222233334444ULL;word=seed;
    dw_frame_begin((uint64_t*)&word);assert(dw_active && dw_proven && dw_selftest_hits==1 && dw_target==&word);
    const uint64_t first=test_dw_pcg(seed);test_dw_draw(first);
    const uint64_t caller=first,keyed=0xabcdef0123456789ULL;
    test_dw_draw(keyed);test_dw_draw(test_dw_pcg(keyed));test_dw_draw(test_dw_pcg(test_dw_pcg(keyed)));test_dw_draw(caller);
    const uint64_t second=test_dw_pcg(first);test_dw_draw(second);
    /* A write that is no step of the frame's word is no draw either, and what follows it is inside its scope. */
    dw_frame_end();assert(!dw_active && !dw_target);
    test_dw_draw(test_dw_pcg(second));
    assert(dw_stores==6 && dw_rows==2 && dw_frame_draws==2 && !dw_strays);
    assert(dw_ring[0].word==first && dw_ring[1].word==second && dw_ring[0].ordinal==1 && dw_ring[1].ordinal==2 && !dw_ring[0].id && dw_ring[0].tick==500);
    char row[256];assert(dw_row(1,row,sizeof(row))>0 && !strncmp(row,"index=1 tick=500 id=0000000000000000 store=2 rip=g",50));
    /* The next frame's seed restarts the ordinal; a frame the probe did not end closes at the next seed. */
    word=seed;dw_frame_begin((uint64_t*)&word);test_dw_draw(test_dw_pcg(seed));
    word=seed+1u; /* a seed that is no step of the last word: only a store */
    dw_frame_begin((uint64_t*)&word);assert(dw_active);dw_frame_end();
    assert(dw_ring[2].ordinal==1);
    /* The tick check lets a frame's breakpoint stand; a stop closes it. */
    word=seed;dw_frame_begin((uint64_t*)&word);assert(dw_check());
    dw_stop();assert(!dw_frame_armed && !dw_active && !strcmp(dw_stage,"stopped"));
    test_dw_draw(5);
    char status[600];assert(dw_status(status,sizeof(status))>0 && strstr(status," frame=0 frame_draws="));
    dw_rows=0;dw_stores=0;dw_scopes=0;dw_frame_draws=0;dw_proven=0;dw_selftest_hits=0;dw_thread=0;dw_stage="idle";dw_strays=0;
    dw_stray_b0=dw_stray_b123=dw_stray_bs=dw_stray_none=dw_stray_armed=dw_stray_other=dw_stray_admitted=dw_stray_rows=0;dw_foreign_dr=0;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("draw watch frame mode: only frame-level draws, keyed scopes skipped, verified\n");
}
static void test_draw_watch(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    LONG saved_all=actor_all_armed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x5c0001,0xae79c) && world_identity_record(0x5c0002,0x92d05));
    dw_id_count=0;dw_armed=0;ui_thread=GetCurrentThreadId();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;actor_all_armed=0;
    /* IDs: nonzero, up to four, before arming, on the MoveWorker. */
    assert(!dw_add(0) && dw_add(0xae79c) && dw_add(0xae79c) && dw_id_count==1);
    ui_thread=GetCurrentThreadId()+1;assert(!dw_add(0x1));ui_thread=GetCurrentThreadId();
    /* Arming needs the all-ship RNG, before the first admitted frame, and touches no debug register. */
    assert(!dw_arm());actor_all_armed=1;stats.issued=1;assert(!dw_arm());stats.issued=0;
    assert(dw_arm() && dw_armed && dw_handle && actor_all_ship_watch==dw_scope && !strcmp(dw_stage,"armed") && dw_module_base);
    assert(!dw_proven && !dw_thread && !dw_selftest_hits);
    /* The probe arms again after the loading UI reload: the same ID is accepted, a new one not. */
    assert(dw_arm() && dw_add(0xae79c) && !dw_add(0x92d05) && dw_id_count==1);
    /* Inert outside admitted MoveWorker frames: before the first tick, while loading, on another thread. */
    volatile uint64_t word=0x5555;test_dw_word=&word;
    dw_scope(1,0xae79c,(uint64_t*)&word);assert(!dw_active && !dw_proven);
    stats.issued=777;boot_phase=2;dw_scope(1,0xae79c,(uint64_t*)&word);assert(!dw_active);boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;dw_scope(1,0xae79c,(uint64_t*)&word);assert(!dw_active);ui_thread=GetCurrentThreadId();
    test_dw_draw(7);assert(!dw_rows && !dw_strays);
    /* The first admitted watched scope proves the mechanism, then three stores are caught. */
    dw_scope(1,0xae79c,(uint64_t*)&word);assert(dw_active && dw_target==&word && dw_proven && dw_selftest_hits==1);
    SetLastError(4321);test_dw_draw(1);assert(GetLastError()==4321); /* the handler keeps the last error */
    test_dw_draw(2);test_dw_draw(3);
    dw_scope(0,0xae79c,(uint64_t*)&word);assert(!dw_active && !dw_target);
    assert(dw_rows==3 && dw_stores==3 && dw_scopes==1 && word==3 && !dw_strays);
    test_dw_draw(4);assert(dw_rows==3 && !dw_strays); /* cleared with the scope */
    char row[256];
    for(unsigned i=0;i<3;i++) {
        assert(dw_row(i,row,sizeof(row))>0);char expect[64];
        snprintf(expect,sizeof(expect),"index=%u tick=777 id=00000000000ae79c store=%u rip=g",i,i+1);assert(!strncmp(row,expect,strlen(expect)));
        snprintf(expect,sizeof(expect)," word=%016llx",(unsigned long long)(i+1));assert(strstr(row,expect));
        assert(strstr(row," chain=g")); /* the caller of the storing function, in this module */
    }
    /* One storing instruction, three call sites. */
    assert(dw_ring[1].rip==dw_ring[0].rip && dw_ring[2].rip==dw_ring[0].rip && dw_ring[0].chain[0]!=dw_ring[1].chain[0] && dw_ring[1].chain[0]!=dw_ring[2].chain[0]);
    /* An unwatched ship: nothing. */
    dw_scope(1,0x92d05,(uint64_t*)&word);assert(!dw_active);test_dw_draw(5);assert(dw_rows==3);
    /* Through actor_all's ship scope: the hook opens on the scope's key and closes before the restore. */
    static unsigned char ship[0x40];*(uint64_t*)(ship+8)=0x5c0001;uint64_t natural=0x7777;test_dw_word=(volatile uint64_t*)&natural;
    actor_all_ship_execute(test_dw_ship,ship,&natural);
    assert(natural==0x7777 && dw_rows==5 && !dw_active && dw_ring[3].word==0x1111 && dw_ring[4].word==0x2222 && dw_ring[3].ordinal==1);
    *(uint64_t*)(ship+8)=0x5c0002;actor_all_ship_execute(test_dw_ship,ship,&natural);assert(dw_rows==5); /* not watched */
    /* An exception unwinding the scope: no exception is raised while unwinding;
     * the breakpoint left on the word is a stray at the next store (the scope's
     * own restore of the caller's word), cleared there once. */
    *(uint64_t*)(ship+8)=0x5c0001;test_dw_scope_throw(ship,&natural);
    assert(natural==0x7777 && dw_rows==6 && !dw_active && !dw_target && dw_strays==1);
    test_dw_draw(0x4444);test_dw_draw(0x5555);assert(dw_strays==1 && dw_rows==6);
    /* Classified as this thread's, our breakpoint hit (Dr6 B0) with our pattern, in an admitted frame; one row. */
    assert(dw_stray_b0==1 && !dw_stray_bs && !dw_stray_none && dw_stray_armed==1 && !dw_stray_other && dw_stray_admitted==1 && dw_stray_rows==1);
    assert(dw_stray_row(0,row,sizeof(row))>0 && !strncmp(row,"row=0 stray=1 tick=777 mine=1 active=0 armed=1 admitted=1 dr6=",62) &&
           strstr(row," rip=g") && strstr(row," chain=g"));
    assert(!dw_stray_row(1,row,sizeof(row)));
    /* A breakpoint set outside any scope (as a lost clear would leave it): cleared at its first hit. */
    volatile uint64_t other=0;test_dw_word=&other;dw_set(&other);test_dw_draw(1);assert(dw_strays==2 && other==1);test_dw_draw(2);assert(dw_strays==2);
    /* A trap-flag single step on this thread is a stray too; the thread holds no debug state any more,
     * so Windows reports Dr6 = 0: none=. */
    test_dw_step();assert(dw_strays==3 && !dw_stray_bs && dw_stray_none==1 && dw_stray_b0==2 && !dw_stray_b123 && dw_stray_armed==2 && dw_stray_rows==3);
    assert(dw_stray_row(2,row,sizeof(row))>0 && strstr(row," stray=3 tick=777 mine=1 active=0 armed=0 admitted=1 dr6=0 rip=g"));
    /* 2000 scopes, each with stores before and after its close: exactly one row per store inside and no
     * stray (when Dr1-Dr3 and Dr7 kept the raised record's garbage, copies of this test run at once saw
     * extra rows and strays from breakpoint 3). */
    test_dw_word=&word;LONG64 rows=dw_rows;uint64_t scopes=dw_scopes;
    for(unsigned k=0;k<2000;k++) {
        dw_scope(1,0xae79c,(uint64_t*)&word);test_dw_draw(k);dw_scope(0,0xae79c,(uint64_t*)&word);test_dw_draw(k+1);test_dw_draw(k+2);
        if(!(k&255)) SwitchToThread();
    }
    assert(dw_rows==rows+2000 && dw_strays==3 && dw_scopes==scopes+2000 && !dw_active);dw_rows=rows;
    /* A breakpoint left set fails the tick check; stop clears it and keeps the handler. */
    dw_scope(1,0xae79c,(uint64_t*)&word);assert(!dw_check() && !strcmp(dw_stage,"left_set") && dw_failures==1);
    dw_scope(1,0xae79c,(uint64_t*)&word); /* no new scope once a failure is counted */
    dw_stop();assert(!dw_armed && !dw_active && dw_handle && !actor_all_ship_watch);
    test_dw_word=&word;test_dw_draw(6);assert(dw_rows==6 && dw_strays==3);
    char status[512];assert(dw_status(status,sizeof(status))>0 && strstr(status,"armed=0 ids=1 proven=1 scopes=") &&
                            strstr(status," rows=6 active=0 strays=3 stage=stopped failures=1 stray_b0=2 stray_b123=0 stray_bs=0 stray_none=1 "
                                   "stray_armed=2 stray_other=0 stray_admitted=3 stray_rows=3"));
    /* Contexts carrying debug state this module never loads: counted, and every load writes all of it. */
    {
        CONTEXT c;memset(&c,0,sizeof(c));EXCEPTION_RECORD e;memset(&e,0,sizeof(e));EXCEPTION_POINTERS ep={&e,&c};
        c.Dr1=0x11;c.Dr2=0x22;c.Dr3=(DWORD64)(uintptr_t)&word;c.Dr6=0xffff0ff8;c.Dr7=0x400|(1u<<6)|(1u<<28)|(2u<<30);
        e.ExceptionCode=DW_SET_CODE;e.NumberParameters=1;e.ExceptionInformation[0]=(ULONG_PTR)&word;
        LONG64 foreign=dw_foreign_dr=0;memset(dw_foreign_first,0,sizeof(dw_foreign_first));DWORD64 dr7=c.Dr7;
        assert(dw_handler(&ep)==EXCEPTION_CONTINUE_EXECUTION && dw_foreign_dr==foreign+1);
        assert(dw_foreign_first[0]=='s' && dw_foreign_first[1]==0x11 && dw_foreign_first[2]==0x22 && dw_foreign_first[3]==(uint64_t)(uintptr_t)&word &&
               dw_foreign_first[4]==dr7);
        char text[512];assert(dw_status(text,sizeof(text))>0 && strstr(text," foreign_dr=1 foreign_first=s0:11:22:"));
        assert(c.Dr0==(DWORD64)(uintptr_t)&word && !c.Dr1 && !c.Dr2 && !c.Dr3 && !c.Dr6 && c.Dr7==(1u|(1u<<16)|(2u<<18)) &&
               (c.ContextFlags&CONTEXT_DEBUG_REGISTERS)==CONTEXT_DEBUG_REGISTERS);
        c.Dr3=0x33;c.Dr7|=1u<<6;e.ExceptionInformation[0]=0;assert(dw_handler(&ep)==EXCEPTION_CONTINUE_EXECUTION && dw_foreign_dr==foreign+2);
        assert(!c.Dr0 && !c.Dr3 && !c.Dr7);
        /* In an open scope a single step that is not breakpoint 0 (here B3) is a stray, not a store; the watch stays. */
        LONG64 rows=dw_rows,strays=dw_strays,b123=dw_stray_b123;volatile uint64_t *target=dw_target;DWORD thread=dw_thread;
        dw_thread=GetCurrentThreadId();dw_target=&word;dw_active=1;
        RtlCaptureContext(&c);c.Dr6=0xffff0ff8;c.Dr7=0x400|(1u<<6);c.Dr3=0x33;e.ExceptionCode=EXCEPTION_SINGLE_STEP;e.NumberParameters=0;
        assert(dw_handler(&ep)==EXCEPTION_CONTINUE_EXECUTION && dw_rows==rows && dw_strays==strays+1 && dw_stray_b123==b123+1 && dw_foreign_dr==foreign+3);
        assert(c.Dr0==(DWORD64)(uintptr_t)&word && !c.Dr3 && c.Dr7==(1u|(1u<<16)|(2u<<18)));
        c.Dr6=0xffff0ff1;assert(dw_handler(&ep)==EXCEPTION_CONTINUE_EXECUTION && dw_rows==rows+1 && dw_strays==strays+1); /* B0: a store */
        dw_active=0;dw_target=target;dw_thread=thread;dw_rows=rows;dw_strays=strays;dw_stray_b123=b123;dw_stray_rows=3;
    }
    /* Sampling: the first 16 strays, then every 1024th. */
    CONTEXT fake;memset(&fake,0,sizeof(fake));RtlCaptureContext(&fake);fake.Dr6=0;fake.Dr7=0;
    LONG64 none=dw_stray_none;dw_strays=dw_stray_rows=0;dw_stray_other=0;for(unsigned i=0;i<16+2048;i++) dw_stray(&fake,0);
    assert(dw_strays==16+2048 && dw_stray_rows==16+2 && dw_stray_none==none+16+2048 && dw_stray_other==2048+16);
    assert(dw_stray_row(17,row,sizeof(row))>0 && !strncmp(row,"row=17 stray=2064 ",18) && strstr(row," mine=0 "));
    dw_stray_rows=DW_STRAY_RING+2;assert(dw_stray_row(1,row,sizeof(row))>0 && !strcmp(row,"row=1 evicted=1"));
    dw_rows=DW_RING+2;assert(dw_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    dw_rows=dw_failures=dw_strays=0;dw_scopes=dw_stores=0;dw_id_count=0;dw_proven=0;memset(dw_ring,0,sizeof(dw_ring));dw_stage="idle";
    dw_stray_b0=dw_stray_b123=dw_stray_bs=dw_stray_none=dw_stray_armed=dw_stray_other=dw_stray_admitted=dw_stray_rows=0;memset(dw_stray_ring,0,sizeof(dw_stray_ring));
    actor_all_armed=saved_all;memset(actor_all_shards,0,sizeof(actor_all_shards));actor_all_ship_depth=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("draw watch inert arming/admitted-only scopes/self test/rows/chain/hook/unwind/stray fail-safe/left-set/stop tests PASS\n");
}
