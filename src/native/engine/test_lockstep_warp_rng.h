/* native_gate_warp_rng: a warp dispatch on a word keyed by (tick, object key,
 * ordinal) with the caller's word restored ('split'), native draws counted
 * ('observe'), gating (not admitted, another thread, no portable key, a full
 * ordinal table), exception unwinding, rows, arming refusals, entry ownership,
 * restore, finish, status bounds, and the native pins; the poster attribution
 * through the constructor (site, thread class, tick, taken once by the
 * dispatch), its thunk ABI and the constructor patch on the native image. */
static unsigned test_wr_draws;
static volatile uint64_t *test_wr_word;
static uint64_t test_wr_seen;
static int test_wr_throw;
static void *test_wr_args[3];
static double test_wr_time;
static uint64_t test_wr_step(uint64_t s) {s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);}
static void test_wr_original(void *event,void *listener,void *arg,double time) {
    test_wr_args[0]=event;test_wr_args[1]=listener;test_wr_args[2]=arg;test_wr_time=time;test_wr_seen=*test_wr_word;
    for(unsigned i=0;i<test_wr_draws;i++) *test_wr_word=test_wr_step(*test_wr_word);
    if(test_wr_throw) RaiseException(0xe0575752,0,0,NULL);
}
static unsigned char test_wr_tls[0x320];
static void test_wr_guarded(uint64_t *word,void *event,void *listener,int *thrown) {
    *thrown=0;test_wr_word=word;double now=33.25;memcpy(test_wr_tls+0x300,&now,8);
    __try {wr_run(test_wr_original,word,test_wr_tls,event,listener,listener,2.5);}
    __except(GetExceptionCode()==0xe0575752?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {*thrown=1;}
}
static void test_wr_reset(void) {
    wr_mode=WR_OFF;wr_installed=wr_restored=wr_ctor_installed=0;wr_active=0;wr_calls=wr_failures=wr_rows=wr_constructed=0;
    wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=wr_attributed=0;
    wr_tick=UINT64_MAX;memset(wr_slots,0,sizeof(wr_slots));memset(wr_posters,0,sizeof(wr_posters));
    wr_original_override=NULL;wr_ctor_override=NULL;wr_stage="idle";test_wr_throw=0;
}
static void *test_wr_ctor_seen[4];static unsigned char test_wr_ctor_flags[2];static unsigned test_wr_ctor_calls;
static void *test_wr_ctor(void *event,void *space,void *transform,void *record,unsigned char a,unsigned char b) {
    test_wr_ctor_seen[0]=event;test_wr_ctor_seen[1]=space;test_wr_ctor_seen[2]=transform;test_wr_ctor_seen[3]=record;
    test_wr_ctor_flags[0]=a;test_wr_ctor_flags[1]=b;++test_wr_ctor_calls;
    uint64_t id=0;if(space) memcpy(&id,(unsigned char*)space+8,8);memcpy((unsigned char*)event+0x18,&id,8);return event;
}
/* Through an emitted 2976 slot: a native-like constructor (the relocated prologue, then a
 * jump to test_wr_ctor) entered by a stub whose call returns to code+531. */
static void test_wr_ctor_abi(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);
    const unsigned char prologue[5]={0x48,0x89,0x5c,0x24,0x18};memcpy(code,prologue,5);
    code[5]=0x48;code[6]=0xb8;void *target=(void*)test_wr_ctor;memcpy(code+7,&target,8);code[15]=0xff;code[16]=0xe0;
    thunk_recipe recipe=*thunk_recipe_at(WR_CTOR_THUNK);assert(recipe.entry==WR_CTOR && !memcmp(recipe.prefix,prologue,5));
    assert(thunk_emit_slot_at(code+256,&recipe,code));
    void *dispatch;memcpy(&dispatch,code+256+32+6,8);assert(dispatch==(void*)wr_construct);
    code[0]=0xe9;int32_t rel=(int32_t)((code+256+32)-(code+5));memcpy(code+1,&rel,4);
    const unsigned char stub[]={0x48,0x83,0xec,0x38,0xc6,0x44,0x24,0x20,0x01,0xc6,0x44,0x24,0x28,0x00,0xe8,0,0,0,0,0x48,0x83,0xc4,0x38,0xc3};
    memcpy(code+512,stub,sizeof(stub));rel=(int32_t)(code-(code+531));memcpy(code+512+15,&rel,4);
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    unsigned char *prior=image;image=code;wr_ctor_override=(wr_ctor_fn)(code+256);wr_active=1;
    static unsigned char event[0x40],space[0x40];*(uint64_t*)(space+8)=0x7a0100;memset(event,0,sizeof(event));
    unsigned before=test_wr_ctor_calls;
    assert(((void *(*)(void *,void *,void *,void *))(code+512))(event,space,NULL,space)==event);
    assert(test_wr_ctor_calls==before+1 && test_wr_ctor_seen[0]==event && test_wr_ctor_seen[1]==space && !test_wr_ctor_seen[2] &&
           test_wr_ctor_seen[3]==space && test_wr_ctor_flags[0]==1 && test_wr_ctor_flags[1]==0 && *(uint64_t*)(event+0x18)==0x7a0100);
    image=prior;wr_ctor_override=NULL;wr_active=0;
    wr_poster_t p=wr_take_poster(event);assert(p.event==(uintptr_t)event && p.site==531 && p.thread==PATH_THREAD_MOVEWORKER);
    VirtualFree(code,0,MEM_RELEASE);
}
static void test_warp_rng(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=world_identity_installed;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x7a0001,0xadd39) && world_identity_record(0x7a0002,0xadd62) && world_identity_record(0x7a0100,0x13e75f));
    test_wr_reset();
    static unsigned char ship[0x40],other[0x40],stray[0x40],event[0x40],record[0x40];
    *(uint64_t*)(ship+8)=0x7a0001;*(uint64_t*)(other+8)=0x7a0002;*(uint64_t*)(stray+8)=0x7a0999;
    *(uint64_t*)(event+0x18)=0x7a0100;*(unsigned char**)(event+0x28)=record;record[0]=1;
    uint64_t word=0x1234;int thrown;test_wr_draws=2;
    /* Inactive: native, counted as outside. */
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=1780;ui_thread=GetCurrentThreadId();
    test_wr_guarded(&word,event,ship,&thrown);
    assert(!thrown && test_wr_seen==0x1234 && word==test_wr_step(test_wr_step(0x1234)) && wr_calls==1 && wr_outside==1 && !wr_rows);
    assert(test_wr_args[0]==event && test_wr_args[1]==ship && test_wr_args[2]==ship && test_wr_time==2.5);
    /* Split: the dispatch sees (tick, key, ordinal); the caller's word is restored. */
    wr_mode=WR_SPLIT;wr_active=1;word=0x1234;
    test_wr_guarded(&word,event,ship,&thrown);
    uint64_t first=lockstep_seed(WR_DOMAIN,1780,0xadd39,1,0,WORLD_KEY_SAVED);
    assert(!thrown && word==0x1234 && test_wr_seen==first && wr_admitted==1 && wr_keyed==1 && wr_drew==1 && wr_draws==2 && wr_rows==1);
    char row[384];
    assert(wr_row(0,row,sizeof(row))>0);
    {char expect[320];snprintf(expect,sizeof(expect),"index=0 tick=1780 key=s:00000000000add39 ordinal=1 space=000000000013e75f flag=1 direction=0 "
        "keyed=1 natural=0000000000001234 word=%016llx draws=2 time=4004000000000000 now=4040a00000000000 site=0 thread=none posted=0 "
        "zone=00000000007a0100",(unsigned long long)first);assert(!strcmp(row,expect));}
    /* The same object again in the tick: ordinal 2; another object: its own ordinal 1; the next tick starts over. */
    test_wr_guarded(&word,event,ship,&thrown);assert(test_wr_seen==lockstep_seed(WR_DOMAIN,1780,0xadd39,2,0,WORLD_KEY_SAVED) && word==0x1234);
    test_wr_guarded(&word,event,other,&thrown);assert(test_wr_seen==lockstep_seed(WR_DOMAIN,1780,0xadd62,1,0,WORLD_KEY_SAVED));
    stats.issued=1781;test_wr_guarded(&word,event,ship,&thrown);assert(test_wr_seen==lockstep_seed(WR_DOMAIN,1781,0xadd39,1,0,WORLD_KEY_SAVED));
    /* The caller's word does not change the keyed one. */
    stats.issued=1782;word=0x9999;test_wr_guarded(&word,event,ship,&thrown);
    assert(test_wr_seen==lockstep_seed(WR_DOMAIN,1782,0xadd39,1,0,WORLD_KEY_SAVED) && word==0x9999);
    /* A direction in the record and an unmapped target space are reported. */
    void *given=record;memcpy(record+0x30,&given,8);*(uint64_t*)(event+0x18)=0x7a0555;
    test_wr_guarded(&word,event,other,&thrown);LONG64 last=wr_rows-1;
    assert(wr_row((uint64_t)last,row,sizeof(row))>0 && strstr(row," space=0000000000000000 flag=1 direction=1 keyed=1 "));
    memset(record+0x30,0,8);*(uint64_t*)(event+0x18)=0x7a0100;
    /* Without a portable key the dispatch draws natively (counted). */
    word=0x4242;test_wr_guarded(&word,event,stray,&thrown);
    assert(test_wr_seen==0x4242 && word==test_wr_step(test_wr_step(0x4242)) && wr_unmapped==1);
    assert(wr_row((uint64_t)(wr_rows-1),row,sizeof(row))>0 && strstr(row," key=-:0000000000000000 ordinal=0 ") && strstr(row," keyed=0 natural=0000000000004242 word=0000000000004242 draws=2 time="));
    /* Not admitted (no tick yet, loading) or another thread: native. */
    uint64_t outside=wr_outside;stats.issued=0;word=0x55;test_wr_guarded(&word,event,ship,&thrown);assert(test_wr_seen==0x55 && wr_outside==outside+1);
    stats.issued=1783;boot_phase=2;test_wr_guarded(&word,event,ship,&thrown);assert(wr_outside==outside+2);boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;word=0x66;test_wr_guarded(&word,event,ship,&thrown);assert(test_wr_seen==0x66 && wr_foreign==1);
    ui_thread=GetCurrentThreadId();
    /* A full ordinal table: native, counted. */
    stats.issued=1784;wr_tick=1784;for(unsigned i=0;i<WR_ORDINALS;i++) {wr_slots[i].key=0x900000+i;wr_slots[i].count=1;}
    assert(!wr_ordinal(0xadd39));word=0x77;test_wr_guarded(&word,event,ship,&thrown);assert(test_wr_seen==0x77 && wr_full==1);
    /* Observe: native word, counted and logged with the word it saw. */
    stats.issued=1785;wr_mode=WR_OBSERVE;word=0x88;uint64_t keyed=wr_keyed;test_wr_guarded(&word,event,ship,&thrown);
    assert(test_wr_seen==0x88 && word==test_wr_step(test_wr_step(0x88)) && wr_keyed==keyed);
    assert(wr_row((uint64_t)(wr_rows-1),row,sizeof(row))>0 && strstr(row," tick=1785 key=s:00000000000add39 ordinal=1 ") && strstr(row," keyed=0 natural=0000000000000088 word=0000000000000088 draws=2 time="));
    /* An exception unwinding the dispatch restores the caller's word and stops the gate at the next check. */
    wr_mode=WR_SPLIT;stats.issued=1786;test_wr_throw=1;word=0x1234;test_wr_guarded(&word,event,ship,&thrown);test_wr_throw=0;
    assert(thrown && word==0x1234 && wr_failures==1 && !wr_check() && !strcmp(wr_stage,"exception"));wr_failures=0;
    /* Beyond the step bound: unknown. */
    test_wr_draws=ACTOR_ALL_STEP_CAP+2;test_wr_guarded(&word,event,ship,&thrown);assert(wr_unknown==1 && word==0x1234);test_wr_draws=2;
    char status[512];int n=wr_status(status,sizeof(status));
    assert(n>0 && strstr(status,"mode=split installed=0 calls=15 admitted=11 keyed=8 ") && strstr(status," unmapped=1 foreign=1 outside=3 full=1 "));
    /* Attribution: the constructor notes (site, thread class, tick); the dispatch takes it once. */
    DWORD saved_main=main_thread;LONG saved_open=path_pass_open;
    wr_mode=WR_OBSERVE;stats.issued=1789;ui_thread=GetCurrentThreadId()+1;main_thread=GetCurrentThreadId()+2;path_pass_open=1;
    static unsigned char posted[0x40],zone[0x40];*(uint64_t*)(zone+8)=0x7a0777;
    assert(wr_construct_run(test_wr_ctor,(uintptr_t)image+0xe912ee,posted,zone,NULL,NULL,1,0)==posted && wr_constructed==1);
    assert(test_wr_ctor_seen[0]==posted && test_wr_ctor_seen[1]==zone && test_wr_ctor_flags[0]==1 && !test_wr_ctor_flags[1]);
    ui_thread=GetCurrentThreadId();stats.issued=1790;LONG64 before=wr_rows;
    test_wr_guarded(&word,posted,ship,&thrown);assert(wr_rows==before+1 && wr_attributed==1);
    assert(wr_row((uint64_t)before,row,sizeof(row))>0 && strstr(row," tick=1790 key=s:00000000000add39 ") && strstr(row," space=0000000000000000 ") &&
           strstr(row," site=e912ee thread=pass posted=1789 zone=00000000007a0777"));
    test_wr_guarded(&word,posted,ship,&thrown);assert(wr_attributed==1 && wr_row((uint64_t)(wr_rows-1),row,sizeof(row))>0 && strstr(row," site=0 thread=none posted=0 "));
    /* Thread classes; outside the image; another event in the same slot; not armed. */
    wr_construct_run(test_wr_ctor,(uintptr_t)image+0x36ad22,posted,zone,NULL,NULL,0,0);
    wr_poster_t p=wr_take_poster(posted);assert(p.site==0x36ad22 && p.thread==PATH_THREAD_MOVEWORKER && p.tick==1790);
    ui_thread=GetCurrentThreadId()+1;main_thread=GetCurrentThreadId();wr_construct_run(test_wr_ctor,0x10,posted,zone,NULL,NULL,0,0);
    p=wr_take_poster(posted);assert(p.site==0xffffffffu && p.thread==PATH_THREAD_MAIN);
    main_thread=GetCurrentThreadId()+2;path_pass_open=0;wr_construct_run(test_wr_ctor,(uintptr_t)image+0x5cf1cb,posted,zone,NULL,NULL,0,0);
    p=wr_take_poster(posted);assert(p.site==0x5cf1cb && p.thread==PATH_THREAD_OTHER);
    assert(!wr_take_poster(posted).event && !wr_take_poster(NULL).event);
    wr_construct_run(test_wr_ctor,(uintptr_t)image+0xe9116b,posted,zone,NULL,NULL,0,0);
    unsigned slot=wr_poster_slot(posted);wr_posters[slot].event^=8;assert(!wr_take_poster(posted).event);memset(&wr_posters[slot],0,sizeof(wr_posters[slot]));
    wr_active=0;LONG64 constructed=wr_constructed;wr_construct_run(test_wr_ctor,(uintptr_t)image+0xe9116b,posted,zone,NULL,NULL,0,0);
    assert(wr_constructed==constructed && !wr_take_poster(posted).event);wr_active=1;
    ui_thread=GetCurrentThreadId();main_thread=saved_main;path_pass_open=saved_open;wr_mode=WR_SPLIT;
    test_wr_ctor_abi();wr_active=1;
    wr_rows=WR_RING+2;assert(wr_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    /* Arming: a mode, before the first admitted frame, on the MoveWorker, with the world identities; pins (zero image refuses). */
    test_wr_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;world_identity_installed=1;
    assert(!wr_arm(WR_OFF) && !wr_arm(7));stats.issued=1;assert(!wr_arm(WR_SPLIT));stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!wr_arm(WR_SPLIT));ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!wr_arm(WR_SPLIT));boot_phase=0;
    world_identity_installed=0;assert(!wr_arm(WR_SPLIT) && !strcmp(wr_stage,"needs"));world_identity_installed=1;
    assert(!wr_arm(WR_SPLIT) && !strcmp(wr_stage,"pins") && !wr_installed && !wr_active);
    /* Ownership and restore on a hand-set entry. */
    void *saved_entry;memcpy(&saved_entry,image+WR_ENTRY,8);
    *(void**)(image+WR_ENTRY)=(void*)wr_dispatch;wr_installed=1;wr_mode=WR_SPLIT;wr_active=1;assert(wr_owned() && wr_arm(WR_SPLIT) && !wr_arm(WR_OBSERVE));
    *(void**)(image+WR_ENTRY)=(void*)test_wr_original;assert(!wr_owned() && !wr_check() && !strcmp(wr_stage,"entry_changed"));
    wr_failures=0;wr_restore();assert(wr_installed && !wr_restored && !wr_active && *(void**)(image+WR_ENTRY)==(void*)test_wr_original);
    *(void**)(image+WR_ENTRY)=(void*)wr_dispatch;wr_restore();assert(!wr_installed && wr_restored && *(void**)(image+WR_ENTRY)==image+WR_DISPATCH);
    /* finish() gives the entry back. */
    *(void**)(image+WR_ENTRY)=(void*)wr_dispatch;wr_installed=1;wr_active=1;wr_restored=0;uint32_t saved_reason=stats.reason;stats.active=1;stats.restored=1;
    finish(WR_STOP_REASON);assert(stats.reason==WR_STOP_REASON && !wr_installed && wr_restored && *(void**)(image+WR_ENTRY)==image+WR_DISPATCH);
    /* Status and rows at the widest values. */
    wr_mode=WR_OBSERVE;wr_installed=1;wr_calls=wr_failures=wr_rows=INT64_MAX;
    wr_admitted=wr_keyed=wr_unmapped=wr_foreign=wr_outside=wr_full=wr_drew=wr_draws=wr_unknown=UINT64_MAX;wr_stage="block_changed";
    n=wr_status(status,sizeof(status));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE WARP_RNG_STOP \n")<512);
    wr_rows=1;wr_ring[0]=(wr_row_t){UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,WORLD_KEY_CREATED,255,1,1,UINT64_MAX,UINT64_MAX,
        UINT64_MAX,UINT64_MAX,UINT32_MAX,PATH_THREAD_MOVEWORKER};
    n=wr_row(0,row,sizeof(row));assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE WARP_RNG_ROW \n")<384);
    memcpy(image+WR_ENTRY,&saved_entry,8);test_wr_reset();memset(wr_ring,0,sizeof(wr_ring));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_installed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    stats.reason=saved_reason;stats.restored=0;
    printf("warp RNG split/ordinals/observe/unmapped/gating/unwind/rows/arming/ownership/restore/finish/status tests PASS\n");
}
/* With the X4 9.00 executable: the entry in WarpEvent's vtable, the pinned
 * blocks and what they do, tamper refusals and a full arm/restore on the copy. */
static void test_warp_rng_native_image(const char *path) {
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
    /* The file holds preferred-base pointers: slot 2 of the vtable whose locator names U::WarpEvent. */
    assert(*(uint64_t*)(image+WR_ENTRY)==base+WR_DISPATCH && WR_ENTRY-0x2baa8c0u==2u*8u);
    uint64_t locator=*(uint64_t*)(image+0x2baa8c0u-8u)-base;uint32_t type=*(uint32_t*)(image+locator+12);
    assert(!strcmp((const char*)image+type+16,".?AVWarpEvent@U@@"));
    *(void**)(image+WR_ENTRY)=image+WR_DISPATCH;assert(wr_pins());
    /* Dispatch: target space ID +0x18, record +0x28, then the listener's slot 23 (+0xb8) by call rbp. */
    assert(!memcmp(image+0x9aa2c9,"\x4c\x8b\x69\x20\x48\x8b\x51\x18",8) && !memcmp(image+0x9aa2d8,"\x48\x8b\xa8\xb8\x00\x00\x00",7) &&
           !memcmp(image+0x9aa333,"\xff\xd5",2));
    /* OnWarpEvent: the safe-position search with the TLS game time (TLS+0x300), then Space::AddChild. */
    assert(!memcmp(image+0x6cf9c3,"\xb8\x00\x03\x00\x00",5) && !memcmp(image+0x6cf9d6,"\xf2\x0f\x10\x04\x30",5));
    assert(image[0x6cfa46]==0xe8 && 0x6cfa4b+*(int32_t*)(image+0x6cfa47)==0x835b80 && image[0x6cfb06]==0xe8 && 0x6cfb0b+*(int32_t*)(image+0x6cfb07)==0x83a8e0);
    /* The search: no direction given (+0x140 null) -> two FloatState draws on TLS+0x318 (mov ebx,0x318; add rbx,r12). */
    assert(!memcmp(image+0x837003,"\x48\x8b\x85\x40\x01\x00\x00\x48\x85\xc0",10) && !memcmp(image+0x8370b1,"\xbb\x18\x03\x00\x00\x49\x03\xdc\x48\x8b\xcb",11));
    assert(image[0x8370bc]==0xe8 && 0x8370c1+*(int32_t*)(image+0x8370bd)==0x14ddc90 && image[0x8370d0]==0xe8 && 0x8370d5+*(int32_t*)(image+0x8370d1)==0x14ddc90);
    /* The constructor: WarpEvent's vtable, +0x18 = the target's ID (or 0); the out-of-line callers. */
    assert(hash_ok(image+WR_CTOR,WR_CTOR_SIZE,WR_CTOR_SHA) && !memcmp(image+WR_CTOR,"\x48\x89\x5c\x24\x18",5));
    assert(!memcmp(image+0x9a9d2c,"\x48\x8d\x05",3) && 0x9a9d33+*(int32_t*)(image+0x9a9d2f)==0x2baa8c0 && !memcmp(image+0x9a9d44,"\x48\x89\x41\x18",4));
    {const unsigned calls[6]={0xe90baa,0xe91166,0xe912e9,0xe914f8,0x36ad1d,0x5cf1c6};
     for(unsigned i=0;i<6;i++) assert(image[calls[i]]==0xe8 && calls[i]+5+*(int32_t*)(image+calls[i]+1)==WR_CTOR);}
    image[WR_CTOR+0x40]^=1;assert(!wr_pins());image[WR_CTOR+0x40]^=1;
    image[WR_DISPATCH+0x40]^=1;assert(!wr_pins());image[WR_DISPATCH+0x40]^=1;
    image[WR_PLACE+0x80]^=1;assert(!wr_pins());image[WR_PLACE+0x80]^=1;
    image[WR_DIRECTION+0xd0]^=1;assert(!wr_pins());image[WR_DIRECTION+0xd0]^=1;
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=world_identity_installed;
    test_wr_reset();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();world_identity_installed=1;
    unsigned char *prior_thunk=thunk;thunk=NULL;assert(!wr_arm(WR_SPLIT) && !strcmp(wr_stage,"ctor") && !wr_ctor_installed);
    thunk=make_thunk();assert(thunk && thunk_slot_ready(WR_CTOR_THUNK));unsigned char native_ctor[8];memcpy(native_ctor,image+WR_CTOR,8);
    assert(wr_arm(WR_SPLIT) && wr_installed && wr_ctor_installed && wr_owned() && wr_check() && wr_arm(WR_SPLIT) && *(void**)(image+WR_ENTRY)==(void*)wr_dispatch);
    {int32_t jump;memcpy(&jump,image+WR_CTOR+1,4);assert(image[WR_CTOR]==0xe9 && image+WR_CTOR+5+jump==thunk+WR_CTOR_THUNK+32 && !memcmp(image+WR_CTOR+5,native_ctor+5,3));
     void *continuation,*dispatch;memcpy(&continuation,thunk+WR_CTOR_THUNK+5+6,8);memcpy(&dispatch,thunk+WR_CTOR_THUNK+32+6,8);
     assert(!memcmp(thunk+WR_CTOR_THUNK,native_ctor,5) && continuation==image+WR_CTOR+5 && dispatch==(void*)wr_construct);}
    image[WR_CTOR+1]^=1;assert(!wr_owned() && !wr_check() && !strcmp(wr_stage,"entry_changed"));image[WR_CTOR+1]^=1;wr_failures=0;
    image[WR_PLACE+0x10]^=1;assert(!wr_check() && !strcmp(wr_stage,"block_changed"));image[WR_PLACE+0x10]^=1;wr_failures=0;
    wr_restore();assert(wr_restored && !wr_installed && !wr_ctor_installed && *(void**)(image+WR_ENTRY)==image+WR_DISPATCH && !memcmp(image+WR_CTOR,native_ctor,8));
    /* A foreign entry refuses arming and stays; the constructor goes back. */
    *(void**)(image+WR_ENTRY)=image+WR_DISPATCH+0x10;test_wr_reset();
    assert(!wr_arm(WR_SPLIT) && !strcmp(wr_stage,"entry") && *(void**)(image+WR_ENTRY)==image+WR_DISPATCH+0x10 && !wr_active);
    assert(!wr_ctor_installed && !memcmp(image+WR_CTOR,native_ctor,8));
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior_thunk;
    test_wr_reset();world_identity_installed=saved_installed;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("warp RNG native entry (WarpEvent slot 2), dispatch/OnWarpEvent/safe-position direction and constructor pins, tamper refusals, "
         "constructor thunk patch and arm/restore verified");
}
