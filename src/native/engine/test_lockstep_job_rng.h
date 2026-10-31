/* Offline tests for the per-job RNG hook: key derivation, submission ordinal
 * stability, the save/seed/restore (and observe) execution path, census rows,
 * and the entry CAS install/restore. No engine image is required; a fake
 * JobThreadManager and QueueJob are built inside the test's placeholder image. */
static uint64_t test_job_step(uint64_t s) {
    s=s*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;
    return (s>>30)|(s<<34);
}
static uint64_t *test_job_rng_ptr;
static unsigned test_job_draws,test_job_body_calls;
static void test_job_body(void *slot,void *scratch) {
    (void)slot;(void)scratch;++test_job_body_calls;
    for(unsigned i=0;i<test_job_draws;i++) *test_job_rng_ptr=test_job_step(*test_job_rng_ptr);
}
static void test_job_body_throw(void *slot,void *scratch) {
    (void)slot;(void)scratch;++test_job_body_calls;
    *test_job_rng_ptr=test_job_step(*test_job_rng_ptr);
    RaiseException(0xe04d5044,0,0,NULL);
}
static int test_job_next_id;
static int test_job_fake_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    EnterCriticalSection((CRITICAL_SECTION*)((unsigned char*)mgr+JOB_CS_OFF)); /* recursive, as QueueJob does */
    int id=test_job_next_id++;
    unsigned char *slot=(unsigned char*)mgr+JOB_SLOTBASE_OFF+(unsigned)id*JOB_STRIDE;
    memcpy(slot,payload,0x98);
    *(void**)(slot+JOB_CB_OFF)=cb;*(uint32_t*)(slot+0xac)=cat;*(uint32_t*)(slot+0xb0)=prio;*(uint32_t*)(slot+0xa8)=1;
    --*(volatile LONG64*)((unsigned char*)mgr+JOB_FREECOUNT_OFF);
    LeaveCriticalSection((CRITICAL_SECTION*)((unsigned char*)mgr+JOB_CS_OFF));
    return id;
}
static void test_job_reset(void) {
    memset(job_rng_table,0,sizeof(job_rng_table));memset(job_rng_buckets,0,sizeof(job_rng_buckets));
    job_rng_ring_cursor=0;job_rng_submissions=job_rng_substituted=job_rng_seeded=job_rng_restored_runs=0;
    job_rng_passthrough=job_rng_misses=job_rng_multi=job_rng_lowslots=job_rng_foreign_cb=job_rng_queue_fail=job_rng_failures=0;
}
static void test_job_rng(void) {
    /* Key derivation: byte-identical to the shared schema, and sensitive to
     * every tuple field so distinct jobs never alias one stream. */
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)==lockstep_seed(JOB_RNG_DOMAIN,7,0xeaeb60,1,3,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)==job_rng_key(7,0xeaeb60,1,3,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)!=job_rng_key(8,0xeaeb60,1,3,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)!=job_rng_key(7,0x77e200,1,3,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)!=job_rng_key(7,0xeaeb60,2,3,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)!=job_rng_key(7,0xeaeb60,1,4,0xe8f250));
    assert(job_rng_key(7,0xeaeb60,1,3,0xe8f250)!=job_rng_key(7,0xeaeb60,1,3,0));

    /* Submission ordinal: per-callback counter, resets each tick, flags a second
     * submitter of the same callback within a tick. */
    if(!job_rng_lock_ready) {InitializeCriticalSection(&job_rng_ord_lock);job_rng_lock_ready=1;}
    memset(job_rng_buckets,0,sizeof(job_rng_buckets));
    int multi=0;
    assert(job_rng_ordinal(0xeaeb60,5,100,&multi)==1 && !multi);
    assert(job_rng_ordinal(0xeaeb60,5,100,&multi)==2 && !multi);
    assert(job_rng_ordinal(0x77e200,5,100,&multi)==1 && !multi); /* independent callback */
    assert(job_rng_ordinal(0xeaeb60,6,100,&multi)==1 && !multi); /* new tick resets */
    assert(job_rng_ordinal(0xeaeb60,6,101,&multi)==0 && multi);  /* second submitter */
    assert(job_rng_ordinal(0xeaeb60,6,100,&multi)==0 && multi);  /* stays poisoned this tick */

    /* Fake manager and QueueJob inside the placeholder image. The callback a
     * submitter passes is a game thunk, so it is an in-image RVA; the body it
     * runs is modelled separately for the execution tests. */
    unsigned char *mgr=image+0x3000000;memset(mgr,0,0x5b8+8*JOB_STRIDE);
    *(void**)(image+JOB_MGR_RVA)=mgr;
    InitializeCriticalSection((CRITICAL_SECTION*)(mgr+JOB_CS_OFF));
    *(volatile LONG64*)(mgr+JOB_FREECOUNT_OFF)=0x7000;
    stats.active=1;boot_phase=3;stats.issued=11;job_rng_seed_mode=JOB_RNG_KEY;
    InterlockedExchange(&job_rng_active,1);
    void *cb=image+0x12340; /* in-image callback RVA 0x12340 */
    unsigned char payload[0x98];memset(payload,0,sizeof(payload));
    *(void**)(payload+8)=image+0x40000; /* inner body pointer -> body_rva 0x40000 */

    /* key-mode submission substitutes our wrapper, stashes the real callback at
     * slot+0x98, and records the key keyed on (tick,cb,cat,ordinal,body). */
    test_job_reset();test_job_next_id=0;
    int id=job_rng_submit(test_job_fake_queue,mgr,cb,payload,1,2);
    assert(id==0 && job_rng_substituted==1 && !job_rng_multi && !job_rng_lowslots);
    unsigned char *slot=mgr+JOB_SLOTBASE_OFF+(unsigned)id*JOB_STRIDE;
    assert(*(void**)(slot+JOB_CB_OFF)==(void*)job_rng_exec);   /* worker will call wrapper */
    assert(*(void**)(slot+JOB_CB_SPARE_OFF)==cb);             /* real callback preserved */
    assert(job_rng_table[id].valid && job_rng_table[id].real_cb==cb && job_rng_table[id].seed==1);
    uint64_t expected_key=job_rng_key(11,0x12340,1,1,0x40000);
    assert(job_rng_table[id].key==expected_key && job_rng_table[id].body_rva==0x40000);

    /* Execution (key mode): reseed the executing word to the key, run the body,
     * restore afterwards, and record one census row seed->exit. */
    memset(slot,0,JOB_STRIDE);*(void**)(slot+JOB_CB_SPARE_OFF)=(void*)test_job_body;
    job_rng_table[0].real_cb=(void*)test_job_body;job_rng_table[0].seed=1;
    uint64_t word=0xdeadbeefcafef00dULL;test_job_rng_ptr=&word;test_job_draws=5;test_job_body_calls=0;
    job_rng_run(&word,slot,NULL);
    assert(test_job_body_calls==1 && word==0xdeadbeefcafef00dULL); /* restored to the pre-seed value */
    assert(job_rng_seeded==1 && job_rng_restored_runs==1 && !job_rng_failures && job_rng_ring_cursor==1);
    job_rng_row_t *r=&job_rng_ring[0];
    uint64_t stepped=expected_key;for(unsigned i=0;i<5;i++) stepped=test_job_step(stepped);
    assert(r->seed_word==expected_key && r->exit_word==stepped && r->seeded==1 && r->key==expected_key && r->body_rva==0x40000);

    /* Execution (observe mode): record the natural entry/exit words, no reseed. */
    test_job_reset();
    job_rng_table[0]=(job_rng_record){0};job_rng_table[0].real_cb=(void*)test_job_body;
    job_rng_table[0].key=0x1111111111111111ULL;job_rng_table[0].seed=0;job_rng_table[0].valid=1;
    *(void**)(slot+JOB_CB_SPARE_OFF)=(void*)test_job_body;
    word=0x0123456789abcdefULL;uint64_t natural=word;test_job_rng_ptr=&word;test_job_draws=3;test_job_body_calls=0;
    job_rng_run(&word,slot,NULL);
    uint64_t natural_exit=natural;for(unsigned i=0;i<3;i++) natural_exit=test_job_step(natural_exit);
    assert(test_job_body_calls==1 && word==natural_exit); /* unchanged, not reseeded */
    assert(!job_rng_seeded && job_rng_passthrough==1 && job_rng_restored_runs==0);
    assert(job_rng_ring[0].seed_word==natural && job_rng_ring[0].exit_word==natural_exit && job_rng_ring[0].seeded==0);

    /* Execution exception still restores the word and is counted. */
    test_job_reset();
    job_rng_table[0]=(job_rng_record){0};job_rng_table[0].real_cb=(void*)test_job_body_throw;
    job_rng_table[0].key=0x2222222222222222ULL;job_rng_table[0].seed=1;job_rng_table[0].valid=1;
    *(void**)(slot+JOB_CB_SPARE_OFF)=(void*)test_job_body_throw;
    word=0x7777777777777777ULL;test_job_rng_ptr=&word;test_job_draws=0;test_job_body_calls=0;
    int caught=0;
    __try {job_rng_run(&word,slot,NULL);}
    __except(GetExceptionCode()==0xe04d5044?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && test_job_body_calls==1 && word==0x7777777777777777ULL && job_rng_failures==1 && job_rng_ring_cursor==1);

    /* observe-mode submission keeps the natural stream (table seed flag 0). */
    test_job_reset();test_job_next_id=0;job_rng_seed_mode=JOB_RNG_OBSERVE;
    memset(mgr+JOB_SLOTBASE_OFF,0,JOB_STRIDE);*(volatile LONG64*)(mgr+JOB_FREECOUNT_OFF)=0x7000;
    id=job_rng_submit(test_job_fake_queue,mgr,cb,payload,1,2);
    assert(id==0 && job_rng_substituted==1 && job_rng_table[id].seed==0);

    /* Low free-slot count fails closed: the real callback is queued unmodified. */
    test_job_reset();test_job_next_id=0;job_rng_seed_mode=JOB_RNG_KEY;
    memset(mgr+JOB_SLOTBASE_OFF,0,JOB_STRIDE);*(volatile LONG64*)(mgr+JOB_FREECOUNT_OFF)=1;
    id=job_rng_submit(test_job_fake_queue,mgr,cb,payload,1,2);
    assert(id==0 && job_rng_lowslots==1 && !job_rng_substituted);
    assert(*(void**)(mgr+JOB_SLOTBASE_OFF+JOB_CB_OFF)==cb); /* not substituted */

    /* A foreign (out-of-image) callback is passed through untouched. */
    test_job_reset();test_job_next_id=0;*(volatile LONG64*)(mgr+JOB_FREECOUNT_OFF)=0x7000;
    id=job_rng_submit(test_job_fake_queue,mgr,(void*)&test_job_rng,payload,1,2);
    assert(job_rng_foreign_cb==1 && !job_rng_substituted);

    /* Disabled hook is a straight pass-through. */
    test_job_reset();test_job_next_id=0;InterlockedExchange(&job_rng_active,0);
    id=job_rng_submit(test_job_fake_queue,mgr,cb,payload,1,2);
    assert(!job_rng_submissions && !job_rng_substituted);
    InterlockedExchange(&job_rng_active,1);

    DeleteCriticalSection((CRITICAL_SECTION*)(mgr+JOB_CS_OFF));
    *(void**)(image+JOB_MGR_RVA)=NULL;

    /* Entry CAS install/restore, mirroring the other hooks' swap tests. */
    uint64_t original=0x5048524e474f4221ULL,patched=0xaabbccddeeff0011ULL;
    memcpy(image+JOB_QUEUE_ENTRY,&original,8);
    assert(job_rng_swap(original,patched) && !job_rng_swap(original,0));
    assert(*(uint64_t*)(image+JOB_QUEUE_ENTRY)==patched);
    job_rng_original_word=original;job_rng_patched_word=patched;job_rng_installed=1;
    job_rng_restore();
    assert(job_rng_restored && !job_rng_installed && !job_rng_active && *(uint64_t*)(image+JOB_QUEUE_ENTRY)==original);

    stats.active=0;boot_phase=0;stats.issued=0;
    puts("native job RNG key/seed/restore/census tests pass");
}
