/* Offline tests for load-time AI Wait durations (native_gate_ai_wait_rng):
 * portable keys independent of the natural word, restore (also on unwind),
 * ordinals, gaps, unmapped/foreign/pass-through/failure paths, the composition
 * with the Wait observer, entry install/adopt/restore, arming refusals and
 * bounded status/rows. Synthetic Wait/Exec/context live in owned memory; the
 * TLS word is injected (the test does not own X4's TLS slot0). */
static unsigned char test_aw_self[0xe0],test_aw_exec[0xc0],test_aw_context[0x210],test_aw_entity[0x10],test_aw_script[0x60],test_aw_tls[0x320];
static uintptr_t test_aw_program[2];
static uint64_t *test_aw_word,test_aw_seen;
static unsigned test_aw_draws=1,test_aw_calls;
static int test_aw_throw;
static uint64_t test_aw_step(uint64_t value) {
    value=value*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (value>>30)|(value<<34);
}
/* Reattach stand-in: draws, then installs saved start + a duration in [0.1, 0.5) s. */
static unsigned char test_aw_original(void *self,void *exec,void *value) {
    assert(self==test_aw_self && exec==test_aw_exec && value==test_aw_exec+0x38);
    test_aw_seen=*test_aw_word;++test_aw_calls;for(unsigned i=0;i<test_aw_draws;i++) *test_aw_word=test_aw_step(*test_aw_word);
    if(test_aw_throw) RaiseException(0xe04d5046,0,0,NULL);
    double start;memcpy(&start,test_aw_exec+0x28,8);
    *(double*)(test_aw_context+0x190)=start+0.1+0.4*(double)(*test_aw_word>>11)/9007199254740992.0;*(unsigned*)(test_aw_context+0x18c)=2;
    return 1;
}
static uint64_t *test_aw_word_source(void) {return test_aw_word;}
static unsigned char test_aw_call(uint64_t *word,uintptr_t caller) {
    test_aw_word=word;return ai_wait_run(caller,test_aw_original,word,test_aw_self,test_aw_exec,test_aw_exec+0x38);
}
static void test_aw_reset(void) {
    memset(ai_wait_slots,0,sizeof(ai_wait_slots));memset(ai_wait_ring,0,sizeof(ai_wait_ring));memset(ai_wait_names,0,sizeof(ai_wait_names));
    ai_wait_slots_tick=ai_wait_last_tick=UINT64_MAX;ai_wait_last=0;ai_wait_last_thread=0;
    ai_wait_draws=ai_wait_keyset=ai_wait_drawset=ai_wait_dueset=ai_wait_gap_draws=0;
    ai_wait_calls=ai_wait_keyed=ai_wait_passed=ai_wait_foreign=ai_wait_unmapped=ai_wait_threads=ai_wait_table_full=0;
    ai_wait_nodraw=ai_wait_draws_unknown=ai_wait_gaps=ai_wait_gap_unknown=ai_wait_cursor=ai_wait_failures=0;
    test_aw_calls=0;test_aw_throw=0;test_aw_draws=1;*(double*)(test_aw_context+0x190)=-1.0;
}
static uint64_t test_aw_fnv(const char *text) {
    uint64_t h=14695981039346656037ULL;for(;*text;text++) h=(h^(unsigned char)*text)*1099511628211ULL;return h;
}
/* The key formula written out independently of ai_wait_stream. */
static uint64_t test_aw_key(uint64_t tick,uint64_t owner,uint64_t ordinal,uint64_t start) {
    uint64_t stream=lockstep_mix(test_aw_fnv("move.idle")^lockstep_mix(4^lockstep_mix(70^lockstep_mix(71))));
    return lockstep_seed(AI_WAIT_DOMAIN,tick,owner,stream,ordinal,start);
}
static void test_aw_build(void) {
    memset(test_aw_self,0,sizeof(test_aw_self));memset(test_aw_exec,0,sizeof(test_aw_exec));memset(test_aw_context,0,sizeof(test_aw_context));
    memset(test_aw_entity,0,sizeof(test_aw_entity));memset(test_aw_script,0,sizeof(test_aw_script));memset(test_aw_tls,0,sizeof(test_aw_tls));
    /* Wait node at program slot 70 of 72; constant 0.1..0.5 s range as in move.idle. */
    static uintptr_t nodes[72];memset(nodes,0,sizeof(nodes));nodes[70]=(uintptr_t)test_aw_self;
    test_aw_program[0]=(uintptr_t)nodes;test_aw_program[1]=(uintptr_t)(nodes+72);
    *(void**)test_aw_self=image+WORLD_WAIT_VTABLE;*(unsigned*)(test_aw_self+0x80)=2;*(unsigned*)(test_aw_self+0x84)=1;*(unsigned*)(test_aw_self+0x88)=1;
    *(unsigned*)(test_aw_self+0x90)=8;*(double*)(test_aw_self+0x98)=0.1;*(unsigned*)(test_aw_self+0xa0)=8;*(double*)(test_aw_self+0xa8)=0.5;
    double start=6.916670000000001;
    *(void**)(test_aw_exec+0x10)=test_aw_program;*(uint64_t*)(test_aw_exec+0x18)=71;*(uint64_t*)(test_aw_exec+0x20)=70;
    memcpy(test_aw_exec+0x28,&start,8);test_aw_exec[0x30]=1;
    *(unsigned*)(test_aw_exec+0x38)=0x66;*(void**)(test_aw_exec+0x40)=test_aw_context+0xa8;
    *(void**)test_aw_context=image+0x2c6a270;*(uint64_t*)(test_aw_context+8)=7;*(void**)(test_aw_context+0x120)=test_aw_entity;
    *(void**)(test_aw_context+0xd8)=test_aw_script;*(uint64_t*)(test_aw_entity+8)=0x9999;
    memcpy(test_aw_script+0x10,"move.idle",9);*(uint64_t*)(test_aw_script+0x20)=9;*(uint64_t*)(test_aw_script+0x28)=15;*(unsigned*)(test_aw_script+0x58)=4;
}
/* With the X4 9.00 executable: the importer's virtual Reattach call and the
 * Wait Reattach body that re-evaluates the duration from the saved start. */
static void test_aw_native_image(void) {
    if(!test_wq_image_path) return;
    FILE *file=fopen(test_wq_image_path,"rb");assert(file && !fseek(file,0,SEEK_END));long size=ftell(file);
    assert(size>0 && size<512*1024*1024 && !fseek(file,0,SEEK_SET));unsigned char *bytes=malloc((size_t)size);
    assert(bytes && fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);
    unsigned char site[16],reattach[0x5d],deadline[13];uint64_t vtable[6];int32_t rel;
    test_ww_copy_rva(bytes,(size_t)size,0x1069bec,site,sizeof(site));
    /* mov rax,[rsi]; lea r8,[r15+0x38]; mov rdx,r15; mov rcx,rsi; call [rax+0x28] -> returns to 0x1069bfc */
    assert(!memcmp(site,"\x48\x8b\x06\x4d\x8d\x47\x38\x49\x8b\xd7\x48\x8b\xce\xff\x50\x28",16) && 0x1069bec+16==WORLD_WAIT_CALLER);
    test_ww_copy_rva(bytes,(size_t)size,WORLD_WAIT_ENTRY,reattach,sizeof(reattach));
    assert(!memcmp(reattach+0x0f,"\x83\xb9\x80\x00\x00\x00\x00",7)); /* cmp dword [rcx+0x80],0: duration expression present */
    memcpy(&rel,reattach+0x31,4);assert(reattach[0x30]==0xe8 && WORLD_WAIT_ENTRY+0x35+rel==0xc59be0); /* evaluate self+0x78 */
    assert(!memcmp(reattach+0x35,"\xf2\x0f\x58\x46\x28",5)); /* addsd xmm0,[rsi+0x28]: saved start exec+0x28 */
    memcpy(&rel,reattach+0x47,4);assert(reattach[0x46]==0xe8 && WORLD_WAIT_ENTRY+0x4b+rel==0x109a490);
    test_ww_copy_rva(bytes,(size_t)size,0x109a4ee,deadline,sizeof(deadline));
    memcpy(&rel,deadline+9,4);assert(!memcmp(deadline,"\x0f\x28\xd6\xba\x02\x00\x00\x00\xe8",9) && 0x109a4ee+13+rel==0x10668b0); /* reason 2 */
    test_ww_copy_rva(bytes,(size_t)size,0xc59c08,site,10);memcpy(&rel,site+6,4);
    assert(site[5]==0xe8 && 0xc59c08+10+rel==0xc58da0); /* EvaluateRandomRange */
    test_ww_copy_rva(bytes,(size_t)size,WORLD_WAIT_VTABLE,vtable,sizeof(vtable));
    assert(vtable[1]==0x140000000ULL+0x1062a50 && vtable[5]==0x140000000ULL+WORLD_WAIT_ENTRY);
    free(bytes);puts("AI Wait load-time call site and Reattach duration path in the X4 image PASS");
}
static void test_ai_wait(void) {
    test_aw_native_image();
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x9999,0x80d54));
    test_aw_build();stats.active=1;boot_mode=1;boot_phase=2;stats.issued=0;ui_thread=GetCurrentThreadId();
    uintptr_t importer=(uintptr_t)(image+WORLD_WAIT_CALLER);uint64_t start;memcpy(&start,test_aw_exec+0x28,8);
    ai_wait_armed=1;InterlockedExchange(&ai_wait_active,1);test_aw_reset();

    /* Keyed by owner, script/version/slot/PC, saved start and ordinal; the
     * natural word never reaches the evaluator and comes back unchanged. */
    uint64_t word=0x1111;assert(test_aw_call(&word,importer)==1);
    uint64_t k1=test_aw_key(0,0x80d54,1,start);
    assert(test_aw_seen==k1 && word==0x1111 && ai_wait_keyed==1 && ai_wait_calls==1 && !ai_wait_failures && test_aw_calls==1);
    ai_wait_row_t *r=&ai_wait_ring[0];uint64_t due1;memcpy(&due1,test_aw_context+0x190,8);
    assert(ai_wait_cursor==1 && r->owner==0x80d54 && r->ordinal==1 && r->key==k1 && r->natural==0x1111 && r->draws==1 &&
           r->exit==test_aw_step(k1) && r->due==due1 && r->slot==70 && r->pc==71 && r->version==4 && r->start==start && !r->pre);
    assert(r->script==test_aw_fnv("move.idle") && !strcmp(ai_wait_name_get(r->script),"move.idle") && ai_wait_draws==1);
    printf("AI Wait golden key %016llx due %016llx\n",(unsigned long long)k1,(unsigned long long)due1);
    assert(k1==0x0ab2b5736ae4638cULL); /* independent Python recomputation */

    /* Another process (other natural word) draws the same key and deadline. */
    uint64_t keyset=ai_wait_keyset,drawset=ai_wait_drawset,dueset=ai_wait_dueset;
    test_aw_reset();word=0xabcdef;test_aw_call(&word,importer);
    assert(test_aw_seen==k1 && word==0xabcdef && ai_wait_ring[0].due==due1 && ai_wait_ring[0].natural==0xabcdef);
    assert(ai_wait_keyset==keyset && ai_wait_drawset==drawset && ai_wait_dueset==dueset);

    /* Same identity again this tick: next ordinal; the unchanged word is no gap. */
    test_aw_call(&word,importer);assert(test_aw_seen==test_aw_key(0,0x80d54,2,start) && ai_wait_ring[1].ordinal==2 && !ai_wait_gaps);
    /* Three foreign draws on the loading thread between restores: pre=3. */
    for(unsigned i=0;i<3;i++) word=test_aw_step(word);
    test_aw_call(&word,importer);assert(ai_wait_ring[2].pre==3 && ai_wait_gaps==1 && ai_wait_gap_draws==3 && ai_wait_ring[2].ordinal==3);
    /* A gap beyond the step cap is counted unknown. */
    word=0x77;test_aw_call(&word,importer);assert(ai_wait_ring[3].pre==UINT32_MAX && ai_wait_gap_unknown==1 && ai_wait_gaps==2);
    /* A new tick restarts ordinals and the gap trace. */
    stats.issued=5;word=0x88;test_aw_call(&word,importer);
    assert(test_aw_seen==test_aw_key(5,0x80d54,1,start) && ai_wait_ring[4].tick==5 && !ai_wait_ring[4].pre);stats.issued=0;
    /* Exact durations (no draw) are keyed and counted nodraw. */
    test_aw_draws=0;test_aw_call(&word,importer);assert(ai_wait_ring[5].draws==0 && ai_wait_nodraw==1);test_aw_draws=1;
    /* Another thread is keyed and counted. */
    ui_thread=GetCurrentThreadId()+1;test_aw_call(&word,importer);assert(ai_wait_threads==1 && ai_wait_ring[6].thread==1);
    ui_thread=GetCurrentThreadId();assert(!ai_wait_failures);

    /* Unwinding restores the word and counts a failure without a row. */
    test_aw_reset();word=0x99;test_aw_throw=1;int caught=0;
    __try {test_aw_call(&word,importer);}
    __except(GetExceptionCode()==0xe04d5046?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && word==0x99 && test_aw_seen==k1 && ai_wait_failures==1 && !ai_wait_cursor);test_aw_throw=0;

    /* Unmapped owners keep the natural word (never fabricated), not a failure;
     * their natural draws do not show up as a gap of the next restore. */
    test_aw_reset();*(uint64_t*)(test_aw_entity+8)=0x9998;word=5;test_aw_call(&word,importer);
    assert(test_aw_seen==5 && word==test_aw_step(5) && ai_wait_unmapped==1 && !ai_wait_failures && !ai_wait_keyed && !ai_wait_cursor);
    *(uint64_t*)(test_aw_entity+8)=0x9999;test_aw_call(&word,importer);assert(!ai_wait_gaps && ai_wait_keyed==1 && !ai_wait_ring[0].pre);
    /* Foreign callers and inactive/stopped scopes pass on the natural word. */
    test_aw_reset();word=5;test_aw_call(&word,importer+1);assert(test_aw_seen==5 && ai_wait_foreign==1 && !ai_wait_failures);
    InterlockedExchange(&ai_wait_active,0);word=5;test_aw_call(&word,importer);assert(test_aw_seen==5 && ai_wait_passed==1);
    InterlockedExchange(&ai_wait_active,1);stats.active=0;word=5;test_aw_call(&word,importer);assert(test_aw_seen==5 && ai_wait_passed==2);
    stats.active=1;assert(!ai_wait_keyed && !ai_wait_failures);
    /* Failures: no TLS word, an unreadable importer argument, an exhausted table. */
    word=5;test_aw_word=&word;ai_wait_run(importer,test_aw_original,NULL,test_aw_self,test_aw_exec,test_aw_exec+0x38);
    assert(test_aw_seen==5 && ai_wait_failures==1);
    *(unsigned*)(test_aw_exec+0x38)=0x5d;word=5;test_aw_call(&word,importer);assert(test_aw_seen==5 && ai_wait_failures==2);
    *(unsigned*)(test_aw_exec+0x38)=0x66;*(uint64_t*)(test_aw_exec+0x20)=69;word=5;test_aw_call(&word,importer);
    assert(test_aw_seen==5 && ai_wait_failures==3);*(uint64_t*)(test_aw_exec+0x20)=70; /* node at slot is not this Wait */
    for(unsigned i=0;i<AI_WAIT_TABLE;i++) {ai_wait_slots[i].used=1;ai_wait_slots[i].owner=~(uint64_t)i;}
    ai_wait_slots_tick=0;word=5;test_aw_call(&word,importer);
    assert(test_aw_seen==5 && ai_wait_table_full==1 && ai_wait_failures==4 && !ai_wait_keyed);

    /* Inside the Wait observer (disabled or selected): one original call, keyed,
     * and the observer sees the caller's word unchanged across the call. */
    test_aw_reset();uint64_t *(*saved_word)(void)=ai_wait_word;ai_wait_word=test_aw_word_source;
    test_aw_word=(uint64_t*)(test_aw_tls+0x318);*test_aw_word=0x4242;world_wait_enabled=0;
    assert(world_wait_observe(test_aw_original,test_aw_self,test_aw_exec,test_aw_exec+0x38,importer,test_aw_tls)==1);
    assert(test_aw_calls==1 && test_aw_seen==k1 && *test_aw_word==0x4242 && ai_wait_keyed==1);
    const char *manifest="0000000000080d54,4,5,401baaab8a5ce5b5,3fb999999999999a,3fe0000000000000,move.idle";
    unsigned count=0;assert(world_wait_parse(manifest,strlen(manifest),world_wait_selected,&count) && count==1);world_wait_selected_count=1;
    unsigned char table[0x134]={0};*(void**)(image+TABLE_RVA)=table;*(unsigned*)(test_aw_tls+0x314)=1;boot_started=GetTickCount64();
    world_wait_enabled=1;world_wait_restoring=0;test_aw_reset();*test_aw_word=0x4343;
    assert(world_wait_observe(test_aw_original,test_aw_self,test_aw_exec,test_aw_exec+0x38,importer,test_aw_tls)==1);
    assert(world_wait_ready()==1 && !world_wait_events[0].errors && world_wait_events[0].rng_before==0x4343 && world_wait_events[0].rng_after==0x4343);
    assert(world_wait_events[0].due_after==due1 && test_aw_seen==k1 && ai_wait_keyed==1);
    world_wait_enabled=0;world_wait_selected_count=0;memset(world_wait_selected,0,sizeof(world_wait_selected));
    memset(world_wait_events,0,sizeof(world_wait_events));world_wait_count=world_wait_calls=world_wait_import_calls=world_wait_foreign_calls=0;
    world_wait_unknown=world_wait_failures=world_wait_sequence=world_wait_peak=0;*(void**)(image+TABLE_RVA)=NULL;ai_wait_word=saved_word;

    /* Rows: format, name, eviction marker, end, worst width. */
    test_aw_reset();word=0x1111;test_aw_call(&word,importer);
    char row[512];assert(ai_wait_row(0,row,sizeof(row))>0 && strstr(row,"index=0 tick=0 owner=0000000000080d54 ") &&
        strstr(row," version=4 slot=70 pc=71 ") && strstr(row," ord=1 ") && strstr(row," natural=0000000000001111 ") &&
        strstr(row," draws=1 ") && strstr(row," pre=0 thread=0 name=move.idle"));
    printf("NATIVE_GATE AI_WAIT_RNG_ROW %s\n",row);assert(!ai_wait_row(1,row,sizeof(row)));
    ai_wait_cursor=AI_WAIT_RING+4;assert(ai_wait_row(1,row,sizeof(row))>0 && !strcmp(row,"index=1 evicted=1"));
    ai_wait_ring[2].script=0x1234;ai_wait_ring[2].draws=UINT32_MAX;ai_wait_ring[2].pre=UINT32_MAX;
    assert(ai_wait_row(AI_WAIT_RING+2,row,sizeof(row))>0 && strstr(row," draws=-1 ") && strstr(row," pre=-1 ") && strstr(row," name=-"));
    ai_wait_name_put(0x5678,"bad name|x");ai_wait_ring[3].script=0x5678;
    assert(ai_wait_row(AI_WAIT_RING+3,row,sizeof(row))>0 && strstr(row," name=bad_name_x"));
    char longest[128];memset(longest,'y',sizeof(longest)-1);longest[sizeof(longest)-1]=0;ai_wait_name_put(0x9abc,longest);
    for(unsigned i=0;i<AI_WAIT_RING;i++) {
        ai_wait_row_t *e=&ai_wait_ring[i];
        e->tick=e->owner=e->slot=e->pc=e->start=e->ordinal=e->key=e->natural=e->exit=e->due=UINT64_MAX;e->script=0x9abc;
        e->version=UINT32_MAX;e->draws=e->pre=UINT32_MAX-1;e->thread=255;
    }
    ai_wait_cursor=INT64_MAX;int n=ai_wait_row((uint64_t)INT64_MAX-1,row,sizeof(row));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE AI_WAIT_RNG_ROW \n")<sizeof(row) && strstr(row," name=yyyyyyyy"));

    /* Entry install (observer's CAS, thunk+2784), refusals, restore and adopt. */
    unsigned char *saved_thunk=thunk;thunk=make_thunk();assert(thunk && world_wait_thunk_matches());
    unsigned char *entry=image+WORLD_WAIT_ENTRY,prefix[8]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74};memcpy(entry,prefix,8);
    int saved_identity=world_identity_installed;world_identity_installed=1;
    ai_wait_armed=0;ai_wait_shared=0;ai_wait_restored=0;InterlockedExchange(&ai_wait_active,0);
    world_wait_installed=0;world_wait_restoring=0;boot_phase=0;stats.issued=0;
    assert(!ai_wait_arm() && !memcmp(entry,prefix,8) && !world_wait_installed); /* full pins fail on the placeholder */
    boot_phase=2;assert(!ai_wait_arm());boot_phase=0;stats.issued=1;assert(!ai_wait_arm());stats.issued=0;
    InterlockedExchange(&world_wait_rng_started,1);assert(!ai_wait_arm());InterlockedExchange(&world_wait_rng_started,0);
    world_identity_installed=0;assert(!ai_wait_arm());world_identity_installed=1;
    assert(!ai_wait_armed && !ai_wait_active && !memcmp(entry,prefix,8));
    assert(ai_wait_install() && world_wait_installed && entry[0]==0xe9 && !memcmp(entry+5,prefix+5,3));
    int32_t rel;memcpy(&rel,entry+1,4);assert(entry+5+rel==thunk+2784 && ai_wait_entry_owned());
    assert(!ai_wait_install()); /* never twice */
    /* The observer cannot re-read a patched entry as original; the selected experiment stays exclusive. */
    assert(!world_wait_configure(manifest,strlen(manifest)) && !world_wait_started);
    /* Adopt: arming over an installed entry (observer first) keeps it. */
    assert(ai_wait_arm() && ai_wait_armed && ai_wait_shared && ai_wait_active && ai_wait_owned());
    assert(ai_wait_arm()); /* idempotent on a loading-UI reload */
    entry[2]^=1;assert(!ai_wait_owned() && !ai_wait_arm());entry[2]^=1;assert(ai_wait_owned());
    char status[1024];assert(ai_wait_status(status,sizeof(status))>0 && strstr(status,"armed=1 installed=1 shared=1 active=1 restored=0 "));
    /* Stop: world_wait_restore gives the entry back, then the scope closes. */
    world_wait_restore();ai_wait_restore();
    assert(!world_wait_installed && !memcmp(entry,prefix,8) && ai_wait_restored && !ai_wait_active && ai_wait_owned());
    word=5;test_aw_reset();test_aw_word=&word;ai_wait_call(test_aw_original,test_aw_self,test_aw_exec,test_aw_exec+0x38,importer);
    assert(test_aw_seen==5 && !ai_wait_calls);
    /* A foreign entry is never adopted. */
    ai_wait_armed=0;ai_wait_shared=0;world_wait_restoring=0;world_wait_installed=1;world_wait_patched=0x1234;
    assert(!ai_wait_arm() && !ai_wait_armed);world_wait_installed=0;
    assert(!world_wait_restoring);ai_wait_restore();assert(!ai_wait_armed);
    VirtualFree(thunk,0,MEM_RELEASE);thunk=saved_thunk;world_identity_installed=saved_identity;memset(entry,0,8);

    /* Bounded status. */
    assert(ai_wait_status(status,sizeof(status))>0 && strstr(status," calls=0 keyed=0 passed=0 "));
    printf("NATIVE_GATE AI_WAIT_RNG tick=1 %s\n",status);
    ai_wait_armed=ai_wait_restored=ai_wait_shared=INT32_MIN;world_wait_installed=UINT32_MAX;InterlockedExchange(&ai_wait_active,(LONG)(-2147483647-1));
    ai_wait_calls=ai_wait_keyed=ai_wait_passed=ai_wait_foreign=ai_wait_unmapped=ai_wait_threads=ai_wait_table_full=INT64_MAX;
    ai_wait_nodraw=ai_wait_draws_unknown=ai_wait_gaps=ai_wait_gap_unknown=ai_wait_failures=INT64_MAX;ai_wait_cursor=INT64_MAX;
    ai_wait_draws=ai_wait_keyset=ai_wait_drawset=ai_wait_dueset=ai_wait_gap_draws=UINT64_MAX;
    n=ai_wait_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE AI_WAIT_RNG tick=18446744073709551615 \n")<sizeof(status));
    ai_wait_armed=ai_wait_restored=ai_wait_shared=0;world_wait_installed=0;world_wait_restoring=0;InterlockedExchange(&ai_wait_active,0);test_aw_reset();
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;ui_thread=0;
    puts("native AI Wait load-time key/restore/ordinal/gap/fallback/observer/install tests PASS");
}
