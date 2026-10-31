/* Offline tests for the per-script RNG hot path: golden keys computed with the
 * implementation before the caches (92151d9), identity cache hits and every
 * invalidation input, the sealed identity table, rows off, ordinal epochs,
 * guarded reads and self-timing. Synthetic objects in owned memory only. */
static test_sr_ai test_srp_d;
static unsigned char test_srp_heap_script[0x80];
static char test_srp_heap_name[]="fight.attack.object.fighter";
static unsigned test_srp_resolve(void *context,script_rng_identity *id) {
    memset(id,0,sizeof(*id));id->kind=SCRIPT_RNG_AI;id->local=(uintptr_t)context;return script_rng_ai(id);
}
static void test_script_rng_perf(void) {
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && !world_identity_sealed);
    assert(world_identity_record(0x5001,0xa1) && world_identity_record(0x6001,0xa1) && world_identity_record(0x5002,0xb2) &&
           world_identity_record(0x7001,0x77));
    test_sr_ai_init(&test_sr_a,0x5001,"move.idle",4);test_sr_ai_init(&test_sr_a2,0x6001,"move.idle",4);
    test_sr_ai_init(&test_sr_b,0x5002,"fight.attack",9);test_sr_ai_init(&test_sr_c,0x5003,"move.patrol",0);
    test_sr_ai_init(&test_srp_d,0x5004,"move.idle",4);test_sr_md_init(&test_sr_cue);
    stats.active=1;boot_mode=1;boot_phase=3;ui_thread=GetCurrentThreadId();boot_options=0;
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_mode=SCRIPT_RNG_KEY;InterlockedExchange(&script_rng_active,1);test_sr_reset();
    uint64_t word=1;test_sr_word=&word;test_sr_draws=1;

    /* Golden vectors of the pre-cache implementation, cold and warm cache. */
    stats.issued=7;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==0xaaa2ded9dbdd8d26ULL && script_rng_ai_misses==1 && !script_rng_ai_hits);
    script_rng_now.open=0;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==0xaaa2ded9dbdd8d26ULL && script_rng_ai_hits==1 && word==1);
    stats.issued=9;npc_exec_entry(test_sr_child,test_sr_b.exec,&word,0);assert(test_sr_seen[3]==0x0dbac9890746ac58ULL);
    stats.issued=12;*(uint32_t*)(test_sr_cue.cue+0x18)=0x633;*(uint32_t*)(test_sr_cue.cue+0x20)=0xa;*(uint64_t*)(test_sr_cue.cue+0x28)=0x7001;
    script_rng_event_run(SR_CUE,test_sr_handler,&word,test_sr_cue.cue,NULL,NULL,NULL);assert(test_sr_seen[6]==0xf68ece429b363ce2ULL);
    script_rng_event_run(SR_CUE,test_sr_handler,&word,test_sr_cue.cue,NULL,NULL,NULL); /* saved-ID cache hit: next rank */
    assert(test_sr_seen[6]==script_rng_key(SR_CUE,12,test_sr_fnv("Other.Cue1"),script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),2,
        lockstep_mix((0x633ULL<<32)^lockstep_mix(0x77))));
    *(uint32_t*)(test_sr_a.callstack+0x18)=0x506;
    script_rng_event_run(SR_STACK,test_sr_handler,&word,test_sr_a.callstack,NULL,NULL,NULL);assert(test_sr_seen[6]==0x1fc0640c5987bb62ULL);
    assert(word==1 && !script_rng_failures);

    /* The cache returns exactly the uncached identity; each validated input
     * (entity, runtime ID, script pointer, name header) invalidates it. */
    script_rng_identity cold,warm,id;test_sr_reset();
    assert(test_srp_resolve(test_sr_a.context,&cold)==SR_OK && script_rng_ai_misses==1);
    assert(test_srp_resolve(test_sr_a.context,&warm)==SR_OK && script_rng_ai_hits==1 && !memcmp(&cold,&warm,sizeof(cold)));
    assert(cold.owner==0xa1 && cold.script==test_sr_fnv("move.idle") && !strcmp(cold.name,"move.idle"));
    ui_thread=GetCurrentThreadId()+1; /* another thread never uses the cache */
    assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && script_rng_ai_hits==1 && !memcmp(&id,&cold,sizeof(id)));
    ui_thread=GetCurrentThreadId();
    *(uint64_t*)(test_sr_a.entity+8)=0x5002;assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && id.owner==0xb2);
    *(uint64_t*)(test_sr_a.entity+8)=0x5001;assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && id.owner==0xa1);
    *(void**)(test_sr_a.context+0x120)=test_sr_b.entity;assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && id.owner==0xb2);
    *(void**)(test_sr_a.context+0x120)=test_sr_a.entity;
    *(void**)(test_sr_a.context+0xd8)=test_sr_b.script;
    assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && id.script==test_sr_fnv("fight.attack") && !strcmp(id.name,"fight.attack"));
    *(void**)(test_sr_a.context+0xd8)=test_sr_a.script;test_sr_a.script[0x10+8]='f'; /* same pointer, renamed in place */
    assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && id.script==test_sr_fnv("move.idlf"));
    test_sr_a.script[0x10+8]='e';assert(test_srp_resolve(test_sr_a.context,&id)==SR_OK && !memcmp(&id,&cold,sizeof(id)));
    *(uint64_t*)(test_sr_a.context)=0; /* foreign vtable is re-checked on every entry */
    assert(test_srp_resolve(test_sr_a.context,&id)==SR_FOREIGN);*(void**)test_sr_a.context=image+SCRIPT_RNG_AI_VT;
    /* Heap-allocated script names decode identically through either reader. */
    char text[96],again[96];memset(test_srp_heap_script,0,sizeof(test_srp_heap_script));
    *(char**)(test_srp_heap_script+0x10)=test_srp_heap_name;*(uint64_t*)(test_srp_heap_script+0x20)=strlen(test_srp_heap_name);
    *(uint64_t*)(test_srp_heap_script+0x28)=31;*(void**)(test_srp_d.context+0xd8)=test_srp_heap_script;
    assert(ai_script_with(read_local,test_srp_d.context,text)==ai_script_with(script_rng_read,test_srp_d.context,again) && !strcmp(text,again));
    assert(ai_script(test_srp_d.context,text)==test_sr_fnv(test_srp_heap_name));
    test_srp_heap_name[5]=' ';assert(!ai_script_with(read_local,test_srp_d.context,text) && !ai_script_with(script_rng_read,test_srp_d.context,again));
    test_srp_heap_name[5]='.';*(void**)(test_srp_d.context+0xd8)=test_srp_d.script;
    /* Unmapped is never cached before the seal: a later import maps it. */
    assert(test_srp_resolve(test_sr_c.context,&id)==SR_UNMAPPED && test_srp_resolve(test_sr_c.context,&id)==SR_UNMAPPED);
    LONG64 hits=script_rng_ai_hits;assert(world_identity_record(0x5003,0xc3));
    assert(test_srp_resolve(test_sr_c.context,&id)==SR_OK && id.owner==0xc3 && script_rng_ai_hits==hits);

    /* Sealed table: lock-free lookups, writes refused, unmapped then cached. */
    world_identity_seal();assert(world_identity_sealed && world_identity_lookup(0x5001)==0xa1 && world_identity_lookup(0x6001)==0xa1);
    assert(!world_identity_record(0x5004,0xd4) && world_identity_sealed_writes==1 && !world_identity_lookup(0x5004));
    assert(test_srp_resolve(test_srp_d.context,&id)==SR_UNMAPPED && script_rng_ai_hits==hits);
    assert(test_srp_resolve(test_srp_d.context,&id)==SR_UNMAPPED && script_rng_ai_hits==hits+1);
    world_identity_sealed=0;world_identity_sealed_writes=0;

    /* Rows off: no ring writes, identical tallies and keys. */
    script_rng_tally with,without;
    test_sr_reset();script_rng_rows=0;stats.issued=30;word=5;npc_exec_entry(test_sr_nested,test_sr_a.exec,&word,0);
    without=script_rng_now;assert(!script_rng_cursor && without.outer==1 && without.child==2 && word==5); /* C is mapped now */
    test_sr_reset();stats.issued=30;word=5;npc_exec_entry(test_sr_nested,test_sr_a.exec,&word,0);with=script_rng_now;
    assert(script_rng_cursor==3 && with.keyset==without.keyset && with.drawset==without.drawset && with.draws==without.draws);

    /* Ordinal epochs: a new tick empties the table; the counter wraps safely. */
    uint64_t stream_a=script_rng_stream(test_sr_fnv("move.idle"),SR_EXEC,SCRIPT_RNG_AI,3);
    test_sr_reset();script_rng_epoch=UINT32_MAX;script_rng_slots[5].epoch=1;stats.issued=31;
    npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);assert(script_rng_epoch==1 && !script_rng_slots[5].epoch);
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,31,0xa1,stream_a,1,4));
    npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);assert(test_sr_seen[0]==script_rng_key(SR_EXEC,31,0xa1,stream_a,2,4));
    stats.issued=32;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(script_rng_epoch==2 && test_sr_seen[0]==script_rng_key(SR_EXEC,32,0xa1,stream_a,1,4));

    /* Guarded reads fail closed exactly where read_local does. */
    unsigned char *page=VirtualAlloc(NULL,8192,MEM_RESERVE,PAGE_NOACCESS);uint64_t value=0;
    assert(page && VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE)==page);
    assert(!script_rng_read(NULL,&value,8) && !script_rng_read(page+4096,&value,8) && !script_rng_read(page+4092,&value,8));
    assert(script_rng_read(page+4088,&value,8) && !read_local(page+4092,&value,8) && read_local(page+4088,&value,8));
    assert(test_srp_resolve(page+4096,&id)==SR_UNREADABLE);
    script_rng_frame f;uintptr_t record;memset(&id,0,sizeof(id));
    assert(script_rng_locate(SR_EXEC,page+4096-0x40,&id,&record)==SR_UNREADABLE);(void)f;
    VirtualFree(page,0,MEM_RELEASE);

    /* Self-timing counts scope work into the tick and converts through QPC. */
    test_sr_reset();script_rng_clock_start();uint64_t spent=script_rng_self;stats.issued=33;
    for(unsigned i=0;i<64;i++) npc_exec_entry(test_sr_nested,test_sr_a.exec,&word,0);
    assert(script_rng_self>spent && script_rng_now.self>0 && script_rng_now.self<=script_rng_self-spent);
    Sleep(2);uint64_t us=script_rng_us(script_rng_now.self);assert(us<1000000);
    char status[2048];int n=script_rng_status(status,sizeof(status));
    assert(n>0 && strstr(status," rows=1 self_us=") && strstr(status," ai_hits=") && strstr(status," identity_sealed=0"));
    printf("script RNG hot path: 64 nested runs, %llu us self, %lld cache hits\n",(unsigned long long)us,(long long)script_rng_ai_hits);

    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    InterlockedExchange(&script_rng_active,0);test_sr_reset();script_rng_mask=0;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native script RNG golden keys/cache/seal/rows/epoch/timing tests pass");
}
