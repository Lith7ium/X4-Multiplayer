/* Offline tests for per-script RNG streams: key derivation and portability,
 * nesting (same owner, child, unknown), restore on exception, unkeyed fallback,
 * observe mode, MD cue names, masks, refusals, vtable CAS and bounded export.
 * Synthetic AI contexts and cues live in owned memory; no engine image. */
typedef struct {
    unsigned char context[0x200],entity[0x80],script[0x80],exec[0x80],callstack[0xd0];
    uintptr_t entries[3],program[2];
} test_sr_ai;
typedef struct {
    unsigned char cue[0x2a0],module[0x100],node[0x60],exec[0x80];
    uintptr_t entries[2],program[2];
} test_sr_md;
static test_sr_ai test_sr_a,test_sr_a2,test_sr_b,test_sr_c;
static test_sr_md test_sr_cue;
static unsigned char test_sr_module2[0x100];
static uint64_t *test_sr_word,test_sr_seen[8];
static unsigned test_sr_draws,test_sr_child_draws=5;
static uint64_t test_sr_step(uint64_t s) {
    s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (s>>30)|(s<<34);
}
static void test_sr_draw(unsigned n) {for(unsigned i=0;i<n;i++) *test_sr_word=test_sr_step(*test_sr_word);}
static uint64_t test_sr_fnv(const char *text) {
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;text[i];i++) hash=(hash^(unsigned char)text[i])*1099511628211ULL;
    return hash;
}
static void test_sr_ai_init(test_sr_ai *a,uint64_t runtime,const char *name,uint64_t pc) {
    memset(a,0,sizeof(*a));
    *(void**)a->context=image+SCRIPT_RNG_AI_VT;*(void**)(a->context+0x120)=a->entity;*(uint64_t*)(a->entity+8)=runtime;
    *(void**)(a->context+0xd8)=a->script;test_npc_string(a->script+0x10,name);
    a->program[0]=(uintptr_t)a->entries;a->program[1]=(uintptr_t)(a->entries+3);
    *(void**)(a->exec+0x10)=a->program;*(uint64_t*)(a->exec+0x18)=pc;
    *(uint32_t*)(a->exec+0x38)=0x66;*(void**)(a->exec+0x40)=a->context+0xa8;
    *(void**)a->callstack=image+SCRIPT_RNG_STACK_VT;*(void**)(a->callstack+0xc8)=a->context;
}
static void test_sr_md_init(test_sr_md *m) {
    memset(m,0,sizeof(*m));
    *(void**)m->cue=image+SCRIPT_RNG_CUE_VT;*(void**)(m->cue+0x160)=m->cue;*(void**)(m->cue+0x150)=m->module;
    test_npc_string(m->module+0x18,"Other");test_npc_string(m->node+0x20,"Cue1");
    *(void**)(m->module+0x80)=m->node;*(void**)m->node=m->module+0x60;*(void**)(m->node+0x40)=m->cue;
    m->program[0]=(uintptr_t)m->entries;m->program[1]=(uintptr_t)(m->entries+2);
    *(void**)(m->exec+0x10)=m->program;*(uint32_t*)(m->exec+0x38)=0x5d;*(void**)(m->exec+0x40)=m->cue+0xb0;
}
static void test_sr_reset(void) {
    memset(script_rng_ring,0,sizeof(script_rng_ring));memset(script_rng_names,0,sizeof(script_rng_names));
    memset(&script_rng_now,0,sizeof(script_rng_now));memset(&script_rng_last,0,sizeof(script_rng_last));
    memset(script_rng_sources,0,sizeof(script_rng_sources));memset(script_rng_why,0,sizeof(script_rng_why));
    script_rng_cursor=script_rng_failures=script_rng_threads=0;script_rng_name_count=script_rng_depth_max=0;
    script_rng_outer=script_rng_child=script_rng_same=script_rng_nested_unkeyed=script_rng_unkeyed=0;
    script_rng_table_full=script_rng_restores=script_rng_draws_unknown=0;script_rng_draws=0;script_rng_current=NULL;
    script_rng_rows=1;script_rng_cache_flush();script_rng_ai_hits=script_rng_ai_misses=0;
    script_rng_unmapped_keyed=script_rng_unkeyed_rows=0;
}
static void test_sr_exec(void *exec) {(void)exec;test_sr_seen[0]=*test_sr_word;test_sr_draw(test_sr_draws);}
static void test_sr_child(void *exec) {(void)exec;test_sr_seen[3]=*test_sr_word;test_sr_draw(test_sr_child_draws);}
static uintptr_t test_sr_range(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    (void)exec;(void)program;(void)begin;(void)end;(void)mode;test_sr_seen[4]=*test_sr_word;test_sr_draw(1);return 0x51;
}
static void test_sr_event(void *context) {(void)context;test_sr_seen[5]=*test_sr_word;test_sr_draw(3);}
static uintptr_t test_sr_handler(void *self,void *a,void *b,void *c) {
    (void)a;(void)b;(void)c;assert(self);test_sr_seen[6]=*test_sr_word;test_sr_draw(4);return 0x77;
}
/* A: 2 draws, child B (5, restored), own range (same: 1), unknown C event
 * (nested unkeyed: 3), own call-stack event (same: 4), then 1 more draw. */
static void test_sr_nested(void *exec) {
    (void)exec;test_sr_seen[0]=*test_sr_word;test_sr_draw(2);test_sr_seen[1]=*test_sr_word;
    npc_exec_entry(test_sr_child,test_sr_b.exec,test_sr_word,0);
    test_sr_seen[2]=*test_sr_word;assert(test_sr_seen[2]==test_sr_seen[1]);
    assert(interrupt_observe(test_sr_range,test_sr_word,test_sr_a.exec,test_sr_a.program,0,3,1,0)==0x51);
    interrupt_event_observe(test_sr_event,test_sr_word,test_sr_c.context,0);
    assert(script_rng_event_run(SR_STACK,test_sr_handler,test_sr_word,test_sr_a.callstack,NULL,NULL,NULL)==0x77);
    test_sr_draw(1);
}
static uint64_t test_sr_listener[2],test_sr_first[2];
static unsigned test_sr_order;
static void test_sr_listen(void *context) {test_sr_listener[context==test_sr_b.context]=*test_sr_word;test_sr_draw(2);}
static void test_sr_deliver(void *exec) {
    (void)exec;test_sr_draw(1);
    for(unsigned i=0;i<2;i++) interrupt_event_observe(test_sr_listen,test_sr_word,(i^test_sr_order)?test_sr_b.context:test_sr_a2.context,0);
}
static void test_sr_unknown(void *exec) {
    (void)exec;test_sr_seen[0]=*test_sr_word;test_sr_draw(2);
    npc_exec_entry(test_sr_child,test_sr_b.exec,test_sr_word,0);test_sr_draw(1);
}
static void test_sr_throw(void *exec) {(void)exec;test_sr_draw(3);RaiseException(0xe04d5044,0,0,NULL);}

static void test_script_rng(void) {
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);
    assert(world_identity_record(0x5001,0xa1) && world_identity_record(0x6001,0xa1) && world_identity_record(0x5002,0xb2) &&
           world_identity_record(0x7001,0x77));
    test_sr_ai_init(&test_sr_a,0x5001,"move.idle",4);test_sr_ai_init(&test_sr_a2,0x6001,"move.idle",4);
    test_sr_ai_init(&test_sr_b,0x5002,"fight.attack",9);test_sr_ai_init(&test_sr_c,0x5003,"move.patrol",0);
    test_sr_md_init(&test_sr_cue);
    uint64_t old_options=boot_options;int old_seeded=interrupt_seeded,old_ai=ai_seeded;
    boot_options=0;interrupt_seeded=0;ai_seeded=0;
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=7;ui_thread=GetCurrentThreadId();
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_mode=SCRIPT_RNG_KEY;InterlockedExchange(&script_rng_active,1);test_sr_reset();

    /* Key derivation: the shared schema, sensitive to every tuple field. */
    uint64_t k=script_rng_key(SR_EXEC,7,0xa1,5,1,4);
    assert(k==lockstep_seed(SCRIPT_RNG_DOMAIN+SR_EXEC,7,0xa1,5,1,4));
    assert(k!=script_rng_key(SR_RANGE,7,0xa1,5,1,4) && k!=script_rng_key(SR_EXEC,8,0xa1,5,1,4));
    assert(k!=script_rng_key(SR_EXEC,7,0xb2,5,1,4) && k!=script_rng_key(SR_EXEC,7,0xa1,6,1,4));
    assert(k!=script_rng_key(SR_EXEC,7,0xa1,5,2,4) && k!=script_rng_key(SR_EXEC,7,0xa1,5,1,5));
    assert(script_rng_steps(1,1)==0 && script_rng_steps(1,test_sr_step(test_sr_step(1)))==2 && script_rng_steps(1,2)==UINT32_MAX);

    /* Outermost AI run: keyed by saved owner, script, program length, PC and
     * the per-tick ordinal; the caller's word comes back unchanged. */
    uint64_t stream_a=script_rng_stream(test_sr_fnv("move.idle"),SR_EXEC,SCRIPT_RNG_AI,3);
    uint64_t ka=script_rng_key(SR_EXEC,7,0xa1,stream_a,1,4);
    uint64_t word=0x1111222233334444ULL;test_sr_word=&word;test_sr_draws=6;
    npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==ka && word==0x1111222233334444ULL && script_rng_outer==1 && script_rng_restores==1);
    assert(!script_rng_failures && !script_rng_current && script_rng_cursor==1);
    script_rng_row_t *r=&script_rng_ring[0];
    assert(r->key==ka && r->seed==ka && r->draws==6 && r->seeded==1 && r->state==SR_OUTER && r->source==SR_EXEC &&
           r->owner==0xa1 && r->ordinal==1 && r->detail==4 && !strcmp(r->name,"move.idle"));
    assert(r->caller==0x1111222233334444ULL && r->why==0); /* the natural word the scope replaced */
    word=0x5555666677778888ULL;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0); /* second run, same tick */
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,7,0xa1,stream_a,2,4) && word==0x5555666677778888ULL);
    /* Portability: other addresses and another runtime ID with the same saved
     * ID and script give the same key; the caller's word is not an input. */
    stats.issued=8;npc_exec_entry(test_sr_exec,test_sr_a2.exec,&word,0);
    uint64_t k8=script_rng_key(SR_EXEC,8,0xa1,stream_a,1,4);assert(test_sr_seen[0]==k8);
    script_rng_now.open=0;word=1;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==k8 && word==1 && script_rng_last.tick==7 && script_rng_last.outer==2);

    /* Nesting: child B gets its own context-free key and A is restored after
     * it; A's own range and call-stack event continue A; unknown C continues A. */
    test_sr_reset();stats.issued=9;word=0x0123456789abcdefULL;
    npc_exec_entry(test_sr_nested,test_sr_a.exec,&word,0);
    uint64_t k9=script_rng_key(SR_EXEC,9,0xa1,stream_a,1,4);
    uint64_t stream_b=script_rng_stream(test_sr_fnv("fight.attack"),SR_EXEC,SCRIPT_RNG_AI,3);
    uint64_t kb=script_rng_key(SR_EXEC,9,0xb2,stream_b,1,9);
    assert(test_sr_seen[0]==k9 && test_sr_seen[3]==kb);
    uint64_t expect=k9;for(unsigned i=0;i<2;i++) expect=test_sr_step(expect);
    assert(test_sr_seen[1]==expect && test_sr_seen[4]==expect);expect=test_sr_step(expect);
    assert(test_sr_seen[5]==expect);for(unsigned i=0;i<3;i++) expect=test_sr_step(expect);
    assert(test_sr_seen[6]==expect);
    assert(word==0x0123456789abcdefULL && !script_rng_current && !script_rng_failures);
    assert(script_rng_outer==1 && script_rng_child==1 && script_rng_same==2 && script_rng_nested_unkeyed==1 && script_rng_why[SR_UNMAPPED]==1);
    assert(script_rng_cursor==2 && script_rng_ring[0].state==SR_CHILD && script_rng_ring[0].draws==5 && script_rng_ring[0].parent==k9 &&
           script_rng_ring[0].key==kb && script_rng_ring[0].ordinal==1 &&
           script_rng_ring[0].depth==2 && script_rng_ring[1].state==SR_OUTER && script_rng_ring[1].draws==2+1+3+4+1);
    assert(script_rng_now.outer==1 && script_rng_now.child==1 && script_rng_now.draws==16 && script_rng_depth_max==2);
    /* Rerun with another caller word and a child making 9 draws instead of 5:
     * A's own stream and exit word do not depend on the child's count. */
    uint64_t row_a=script_rng_ring[1].exit;test_sr_reset();word=42;test_sr_child_draws=9;
    npc_exec_entry(test_sr_nested,test_sr_a.exec,&word,0);
    assert(script_rng_ring[1].exit==row_a && script_rng_ring[0].draws==9 && word==42);test_sr_child_draws=5;
    /* Context-free: B run outermost in the same tick gets the key it got nested. */
    test_sr_reset();npc_exec_entry(test_sr_child,test_sr_b.exec,&word,0);assert(test_sr_seen[3]==kb && script_rng_outer==1);
    /* Listener order inside one parent does not change any listener's key. */
    for(unsigned order=0;order<2;order++) {
        test_sr_reset();test_sr_order=order;npc_exec_entry(test_sr_deliver,test_sr_a.exec,&word,0);
        assert(script_rng_child==2 && !script_rng_failures);
        if(!order) {test_sr_first[0]=test_sr_listener[0];test_sr_first[1]=test_sr_listener[1];}
        else assert(test_sr_listener[0]==test_sr_first[0] && test_sr_listener[1]==test_sr_first[1] && test_sr_first[0]!=test_sr_first[1]);
    }

    /* Unknown outermost owner: no fabricated key, natural stream continues;
     * its known child B is keyed as outermost and leaves no trace on it. */
    test_sr_reset();stats.issued=10;word=0x77;
    npc_exec_entry(test_sr_unknown,test_sr_c.exec,&word,0);
    uint64_t natural=0x77;for(unsigned i=0;i<3;i++) natural=test_sr_step(natural);
    assert(test_sr_seen[0]==0x77 && word==natural && script_rng_unkeyed==1 && script_rng_outer==1 && !script_rng_child);
    assert(test_sr_seen[3]==script_rng_key(SR_EXEC,10,0xb2,stream_b,1,9) && script_rng_now.unkeyed==1);

    /* Option 1 (natural rows): the unknown owner's scope gets a census row
     * (state 5) with the natural word it ran on, its draws, the reason, the
     * unmapped marker as owner and a join key in its own domain; it stays out
     * of every tally. B's row records the natural word C left it. */
    uint64_t stream_c=script_rng_stream(test_sr_fnv("move.patrol"),SR_EXEC,SCRIPT_RNG_AI,3);
    uint64_t keyset_plain=script_rng_now.keyset;
    test_sr_reset();script_rng_options=SCRIPT_RNG_OPT_NATURAL;stats.issued=10;word=0x77;
    npc_exec_entry(test_sr_unknown,test_sr_c.exec,&word,0);
    assert(word==natural && test_sr_seen[0]==0x77 && script_rng_unkeyed==1 && script_rng_outer==1 && script_rng_now.outer==1);
    assert(script_rng_now.keyset==keyset_plain && script_rng_unkeyed_rows==1 && script_rng_cursor==2);
    uint64_t after_c=test_sr_step(test_sr_step(0x77));
    assert(script_rng_ring[0].state==SR_OUTER && script_rng_ring[0].caller==after_c && script_rng_ring[0].why==0);
    r=&script_rng_ring[1];
    assert(r->state==SR_UNKEYED && r->seeded==0 && r->seed==0x77 && r->caller==0x77 && r->exit==natural && r->draws==3 &&
           r->why==SR_UNMAPPED && r->owner==SCRIPT_RNG_UNMAPPED_OWNER && r->ordinal==1 && r->detail==0 && !strcmp(r->name,"move.patrol"));
    assert(r->key==lockstep_seed(SCRIPT_RNG_UNKEYED_DOMAIN+SR_EXEC,10,SCRIPT_RNG_UNMAPPED_OWNER,stream_c,1,0));
    {char text[640];assert(script_rng_row(1,text,sizeof(text))>0 && strstr(text," state=5 ") && strstr(text," name=move.patrol caller=0000000000000077 why=2"));}
    npc_exec_entry(test_sr_unknown,test_sr_c.exec,&word,0); /* second unmapped run, same tick: next rank */
    assert(script_rng_ring[3].ordinal==2 && script_rng_ring[3].key==lockstep_seed(SCRIPT_RNG_UNKEYED_DOMAIN+SR_EXEC,10,SCRIPT_RNG_UNMAPPED_OWNER,stream_c,2,0));
    script_rng_rows=0;test_sr_draws=1;uint64_t silent=word;npc_exec_entry(test_sr_exec,test_sr_c.exec,&word,0);
    assert(word==test_sr_step(silent) && script_rng_unkeyed_rows==2); /* no export, no row */
    script_rng_rows=1;

    /* Option 2 (unportable keying): the unmapped owner is keyed with the
     * marker and its rank; the caller's word survives; B nested in it becomes
     * a child with the same context-free key. */
    test_sr_reset();script_rng_options=SCRIPT_RNG_OPT_UNMAPPED;stats.issued=10;word=0x77;
    npc_exec_entry(test_sr_unknown,test_sr_c.exec,&word,0);
    uint64_t kc=script_rng_key(SR_EXEC,10,SCRIPT_RNG_UNMAPPED_OWNER,stream_c,1,0);
    assert(test_sr_seen[0]==kc && word==0x77 && test_sr_seen[3]==script_rng_key(SR_EXEC,10,0xb2,stream_b,1,9));
    assert(script_rng_outer==1 && script_rng_child==1 && !script_rng_unkeyed && !script_rng_why[SR_UNMAPPED] &&
           script_rng_unmapped_keyed==1 && script_rng_restores==2 && script_rng_now.outer==1);
    assert(script_rng_ring[0].state==SR_CHILD && script_rng_ring[0].parent==kc && script_rng_ring[1].state==SR_OUTER &&
           script_rng_ring[1].why==SR_UNMAPPED && script_rng_ring[1].owner==SCRIPT_RNG_UNMAPPED_OWNER && script_rng_ring[1].draws==3 &&
           script_rng_ring[1].caller==0x77);
    word=0x88;npc_exec_entry(test_sr_exec,test_sr_c.exec,&word,0); /* next unportable run: next rank */
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,10,SCRIPT_RNG_UNMAPPED_OWNER,stream_c,2,0) && word==0x88 && script_rng_unmapped_keyed==2);
    /* Observe mode with option 2: the same key is derived, nothing written. */
    test_sr_reset();script_rng_mode=SCRIPT_RNG_OBSERVE;word=0x99;test_sr_draws=2;
    npc_exec_entry(test_sr_exec,test_sr_c.exec,&word,0);
    assert(test_sr_seen[0]==0x99 && word==test_sr_step(test_sr_step(0x99)) && script_rng_ring[0].key==kc && !script_rng_ring[0].seeded);
    script_rng_mode=SCRIPT_RNG_KEY;script_rng_options=0;test_sr_draws=6;

    /* Exception through a keyed scope: word restored, scope popped, counted. */
    test_sr_reset();word=0x99;int caught=0;
    __try {npc_exec_entry(test_sr_throw,test_sr_a.exec,&word,0);}
    __except(GetExceptionCode()==0xe04d5044?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && word==0x99 && script_rng_failures==1 && !script_rng_current && script_rng_cursor==1 && script_rng_ring[0].draws==3);

    /* Observe: same identities and keys, no write. */
    test_sr_reset();script_rng_mode=SCRIPT_RNG_OBSERVE;stats.issued=11;word=0xabc;test_sr_draws=2;
    npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==0xabc && word==test_sr_step(test_sr_step(0xabc)) && !script_rng_restores);
    assert(script_rng_ring[0].seeded==0 && script_rng_ring[0].seed==0xabc && script_rng_ring[0].draws==2 &&
           script_rng_ring[0].key==script_rng_key(SR_EXEC,11,0xa1,stream_a,1,4));
    script_rng_mode=SCRIPT_RNG_KEY;

    /* MD: cue identity is the static module.cue name, cached per definition;
     * an event's first object parameter contributes only its saved ID. */
    test_sr_reset();stats.issued=12;word=5;test_sr_draws=1;
    uint64_t md_owner=test_sr_fnv("Other.Cue1"),md_stream=script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_EXEC,SCRIPT_RNG_MD,2);
    npc_exec_entry(test_sr_exec,test_sr_cue.exec,&word,0);
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,12,md_owner,md_stream,1,0) && word==5 && script_rng_name_count==1);
    assert(script_rng_ring[0].kind==SCRIPT_RNG_MD && script_rng_ring[0].instance==0 && !strcmp(script_rng_ring[0].name,"Other.Cue1"));
    *(void**)(test_sr_cue.node+0x40)=NULL; /* cached: no second tree walk */
    *(uint32_t*)(test_sr_cue.cue+0x18)=0x633;*(uint32_t*)(test_sr_cue.cue+0x20)=0xa;*(uint64_t*)(test_sr_cue.cue+0x28)=0x7001;
    assert(script_rng_event_run(SR_CUE,test_sr_handler,&word,test_sr_cue.cue,NULL,NULL,NULL)==0x77);
    uint64_t cue_detail=lockstep_mix((0x633ULL<<32)^lockstep_mix(0x77));
    assert(test_sr_seen[6]==script_rng_key(SR_CUE,12,md_owner,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),1,cue_detail));
    assert(word==5 && script_rng_outer==2);
    *(uint32_t*)(test_sr_cue.cue+0x20)=0;assert(script_rng_event_run(SR_CUE,test_sr_handler,&word,test_sr_cue.cue,NULL,NULL,NULL)==0x77);
    assert(test_sr_seen[6]==script_rng_key(SR_CUE,12,md_owner,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),1,
        lockstep_mix((0x633ULL<<32)^lockstep_mix(0)))); /* another parameter: own rank */
    assert(script_rng_event_run(SR_CUE,test_sr_handler,&word,test_sr_cue.cue,NULL,NULL,NULL)==0x77);
    assert(test_sr_seen[6]==script_rng_key(SR_CUE,12,md_owner,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),2,
        lockstep_mix((0x633ULL<<32)^lockstep_mix(0))));
    /* Call-stack event resolves its owning AI context. */
    *(uint32_t*)(test_sr_a.callstack+0x18)=0x506;
    script_rng_event_run(SR_STACK,test_sr_handler,&word,test_sr_a.callstack,NULL,NULL,NULL);
    assert(test_sr_seen[6]==script_rng_key(SR_STACK,12,0xa1,script_rng_stream(test_sr_fnv("move.idle"),SR_STACK,SCRIPT_RNG_AI,0),1,
        lockstep_mix((0x506ULL<<32)^lockstep_mix(0))) && word==5);
    /* A replaced definition (other module at the same address) is re-resolved. */
    *(void**)(test_sr_cue.cue+0x150)=test_sr_module2;test_npc_string(test_sr_module2+0x18,"Other");
    *(void**)(test_sr_module2+0x80)=test_sr_module2+0x60;
    npc_exec_entry(test_sr_exec,test_sr_cue.exec,&word,0);
    assert(test_sr_seen[0]==5 && script_rng_why[SR_UNNAMED]==1 && script_rng_unkeyed==1);
    *(void**)(test_sr_cue.cue+0x150)=test_sr_cue.module;*(void**)(test_sr_cue.node+0x40)=test_sr_cue.cue;
    /* Masked kind: transparent, not counted as missing identity. */
    script_rng_mask=SCRIPT_RNG_AI;memset(script_rng_names,0,sizeof(script_rng_names));
    uint64_t unmasked=word;npc_exec_entry(test_sr_exec,test_sr_cue.exec,&word,0);
    assert(test_sr_seen[0]==unmasked && script_rng_why[SR_MASKED]==1 && script_rng_unkeyed==1);
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;

    /* Pass-through: foreign thread, unsupported phase, inactive, depth limit. */
    test_sr_reset();word=3;ui_thread=GetCurrentThreadId()+1;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==3 && script_rng_threads==1 && !script_rng_cursor);ui_thread=GetCurrentThreadId();
    boot_phase=2;stats.issued=0;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);assert(!script_rng_cursor);
    script_rng_mask|=SCRIPT_RNG_BOOT;npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==script_rng_key(SR_EXEC,0,0xa1,stream_a,1,4) && script_rng_cursor==1);
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;boot_phase=3;stats.issued=13;
    uint64_t before=word;InterlockedExchange(&script_rng_active,0);npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(test_sr_seen[0]==before && script_rng_cursor==1);InterlockedExchange(&script_rng_active,1);
    script_rng_frame deep;memset(&deep,0,sizeof(deep));deep.depth=SCRIPT_RNG_DEPTH;script_rng_current=&deep;
    npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    assert(script_rng_failures==1 && script_rng_current==&deep);script_rng_current=NULL;

    /* Bounded export: worst-case status and rows stay far below 8191 bytes. */
    test_sr_reset();npc_exec_entry(test_sr_exec,test_sr_a.exec,&word,0);
    memset(script_rng_ring[0].name,'x',sizeof(script_rng_ring[0].name)-1);script_rng_ring[0].name[3]=' ';
    char row[640];int n=script_rng_row(0,row,sizeof(row));
    assert(n>0 && strstr(row,"state=1 kind=1") && strstr(row," name=xxx_x"));printf("NATIVE_GATE SCRIPT_RNG_ROW %s\n",row);
    assert(!script_rng_row(1,row,sizeof(row)));
    char sample[2048];assert(script_rng_status(sample,sizeof(sample))>0 && strstr(sample," cur_outer=1 "));
    printf("NATIVE_GATE SCRIPT_RNG tick=13 %s\n",sample);
    for(unsigned i=0;i<SR_SOURCES;i++) script_rng_sources[i]=INT64_MAX/8;
    for(unsigned i=0;i<SR_REASONS;i++) script_rng_why[i]=INT64_MAX;
    script_rng_outer=script_rng_child=script_rng_same=script_rng_nested_unkeyed=script_rng_unkeyed=INT64_MAX;
    script_rng_table_full=script_rng_threads=script_rng_restores=script_rng_failures=script_rng_draws_unknown=INT64_MAX;
    script_rng_draws=UINT64_MAX;script_rng_cursor=INT64_MAX;script_rng_depth_max=script_rng_name_count=UINT32_MAX;
    memset(&script_rng_now,0xff,sizeof(script_rng_now));memset(&script_rng_last,0xff,sizeof(script_rng_last));
    char status[2048];n=script_rng_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE SCRIPT_RNG tick=18446744073709551615 \n")<sizeof(status));
    assert(script_rng_row((uint64_t)0,row,sizeof(row))>0 && strstr(row,"evicted=1"));
    script_rng_row_t *worst=&script_rng_ring[(uint64_t)(INT64_MAX-1)%SCRIPT_RNG_RING];
    memset(worst,0xff,sizeof(*worst));memset(worst->name,'y',sizeof(worst->name));worst->draws=UINT32_MAX-1;
    int m=script_rng_row((uint64_t)(INT64_MAX-1),row,sizeof(row));assert(m>0 && (unsigned)m+64<sizeof(row));
    printf("script RNG worst-case status %d bytes, row %d bytes\n",n,m);
    test_sr_reset();

    /* Arming refusals precede any write; vtable CAS install/restore. */
    InterlockedExchange(&script_rng_active,0);boot_phase=0;stats.issued=0;
    assert(!script_rng_arm(SCRIPT_RNG_KEY,3,0,0)); /* interrupt/nested/identity hooks missing */
    interrupt_installed=interrupt_event_installed=nested_action_installed=world_identity_installed=1;
    ai_seeded=1;assert(!script_rng_arm(SCRIPT_RNG_KEY,3,0,0));ai_seeded=0;
    interrupt_seeded=1;assert(!script_rng_arm(SCRIPT_RNG_KEY,3,0,0));interrupt_seeded=0;
    boot_options=140737488355328ULL;assert(!script_rng_arm(SCRIPT_RNG_KEY,3,0,0));boot_options=0;
    assert(!script_rng_arm(SCRIPT_RNG_KEY,4,0,0) && !script_rng_arm(SCRIPT_RNG_KEY,8,0,0) && !script_rng_arm(SCRIPT_RNG_KEY,0,0,0));
    boot_rng_anchor_armed=1;assert(!script_rng_arm(SCRIPT_RNG_OBSERVE,7,0,0));boot_rng_anchor_armed=0;
    assert(!script_rng_arm(SCRIPT_RNG_KEY,3,1,4) && !script_rng_arm(SCRIPT_RNG_KEY,3,0,SCRIPT_RNG_OPT_NATURAL) &&
           !script_rng_arm(SCRIPT_RNG_KEY,2,1,SCRIPT_RNG_OPT_UNMAPPED)); /* unknown bit, natural rows without export, no AI kind */
    assert(!script_rng_arm(SCRIPT_RNG_KEY,3,0,0)); /* pins fail on the placeholder image */
    assert(!script_rng_armed && !npc_exec_installed && !script_rng_cue_installed && !script_rng_stack_installed && !script_rng_phase2_armed());
    interrupt_installed=interrupt_event_installed=nested_action_installed=world_identity_installed=0;
    *(void**)(image+SCRIPT_RNG_CUE_SLOT)=image+SCRIPT_RNG_CUE_HANDLER;*(void**)(image+SCRIPT_RNG_STACK_SLOT)=image+SCRIPT_RNG_STACK_HANDLER;
    assert(script_rng_slot_swap(SCRIPT_RNG_CUE_SLOT,image+SCRIPT_RNG_CUE_HANDLER,(void*)script_rng_cue_event));
    assert(!script_rng_slot_swap(SCRIPT_RNG_CUE_SLOT,image+SCRIPT_RNG_CUE_HANDLER,(void*)script_rng_cue_event));
    assert(script_rng_slot_swap(SCRIPT_RNG_STACK_SLOT,image+SCRIPT_RNG_STACK_HANDLER,(void*)script_rng_stack_event));
    script_rng_cue_installed=script_rng_stack_installed=1;script_rng_armed=1;assert(script_rng_owned());
    *(void**)(image+SCRIPT_RNG_STACK_SLOT)=image;assert(!script_rng_owned());
    script_rng_restore();assert(!script_rng_restored && script_rng_stack_installed && !script_rng_cue_installed);
    assert(*(void**)(image+SCRIPT_RNG_CUE_SLOT)==image+SCRIPT_RNG_CUE_HANDLER);
    *(void**)(image+SCRIPT_RNG_STACK_SLOT)=(void*)script_rng_stack_event;script_rng_restore();
    assert(script_rng_restored && !script_rng_stack_installed && *(void**)(image+SCRIPT_RNG_STACK_SLOT)==image+SCRIPT_RNG_STACK_HANDLER);
    assert(script_rng_owned() && !script_rng_active);
    script_rng_armed=script_rng_restored=0;script_rng_mask=0;

    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    boot_options=old_options;interrupt_seeded=old_seeded;ai_seeded=old_ai;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native script RNG key/nesting/restore/fallback tests pass");
}
