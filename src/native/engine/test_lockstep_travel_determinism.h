/* Inline pathing batch execution: JobThread TLS emulation, per-batch keys,
 * restoration, saved-ID order hashing and forwarding. No native code runs. */
static unsigned char *test_tr_tls;
static uint64_t test_tr_seen_time,test_tr_seen_previous,test_tr_seen_rng;
static uint32_t test_tr_seen_view;
static void *test_tr_seen_payload;
static unsigned test_tr_jobs,test_tr_queued;
static void test_tr_job(void *payload,void *scratch) {
    assert(!scratch);++test_tr_jobs;test_tr_seen_payload=payload;
    memcpy(&test_tr_seen_time,test_tr_tls+0x300,8);memcpy(&test_tr_seen_previous,test_tr_tls+0x328,8);
    memcpy(&test_tr_seen_view,test_tr_tls+0x314,4);memcpy(&test_tr_seen_rng,test_tr_tls+0x318,8);
}
static int test_tr_queue(void *mgr,void *cb,void *payload,unsigned cat,unsigned prio) {
    (void)mgr;(void)cb;(void)payload;assert(cat==1 && prio==0);++test_tr_queued;return 7;
}
static void test_travel_determinism(void) {
    static test_pd_ship ships[3];static unsigned char batch[0x20],sentinel[0x18],payload[0x98],tls[0x400];
    int owned_identities=!world_identities;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);
    for(unsigned i=0;i<3;i++) {
        test_pd_link(&ships[i],0x9000+i,sentinel,i+1<3?ships[i+1].node:sentinel);
        assert(world_identity_record(0x9000+i,0x70000+i));
    }
    memset(batch,0,sizeof(batch));*(uint64_t*)(batch+8)=3;*(unsigned char**)(batch+0x10)=sentinel;
    *(unsigned char**)(sentinel+8)=ships[0].node;*(unsigned char**)(batch+0x18)=ships[0].node;
    memset(payload,0,sizeof(payload));*(unsigned char**)payload=batch;*(void**)(payload+8)=image+TRAVEL_BATCH_JOB;
    test_tr_tls=tls;memset(tls,0,sizeof(tls));
    const uint64_t time=0x401c772015d867c4ULL,previous=0x401c660f04c756b3ULL,rng=0x1234567890abcdefULL;const uint32_t view=1;
    memcpy(tls+0x300,&time,8);memcpy(tls+0x328,&previous,8);memcpy(tls+0x314,&view,4);memcpy(tls+0x318,&rng,8);
    int saved_active=stats.active,saved_phase=boot_phase;uint64_t saved_issued=stats.issued;
    stats.active=1;boot_phase=3;stats.issued=5;InterlockedExchange(&travel_active,1);
    travel_tick=UINT64_MAX;travel_passes=travel_batches=0;test_tr_jobs=test_tr_queued=0;
    #define TR_CALL(cb) travel_queue_impl(test_tr_queue,(void*)test_tr_job,image+TRAVEL_BATCH_JOB,tls,NULL,(void*)(cb),payload,1,0)
    assert(TR_CALL(test_tr_job)==0 && test_tr_jobs==1 && !test_tr_queued && test_tr_seen_payload==payload);
    /* A JobThread context: no synced game time, published view, per-batch key. */
    assert(!test_tr_seen_time && !test_tr_seen_previous && !test_tr_seen_view &&
           test_tr_seen_rng==lockstep_seed(TRAVEL_DOMAIN,5,1,0,0,0));
    assert(!memcmp(tls+0x300,&time,8) && !memcmp(tls+0x328,&previous,8) && !memcmp(tls+0x314,&view,4) && !memcmp(tls+0x318,&rng,8));
    assert(travel_passes==1 && travel_ordinal==1 && travel_pass_agents==3 && travel_unmapped==0 && !strcmp(travel_stage,"done"));
    uint64_t first=travel_pass_hash;
    assert(TR_CALL(test_tr_job)==0 && travel_ordinal==2 && test_tr_seen_rng==lockstep_seed(TRAVEL_DOMAIN,5,2,0,0,0));
    /* A new pass restarts the ordinal and reproduces the same order hash. */
    stats.issued=6;assert(TR_CALL(test_tr_job)==0 && travel_passes==2 && travel_ordinal==1 && travel_pass_hash==first);
    /* The visit order starts at the cursor; a rotated cursor is a different order. */
    stats.issued=7;*(unsigned char**)(batch+0x18)=ships[1].node;assert(TR_CALL(test_tr_job)==0 && travel_pass_hash!=first);
    *(unsigned char**)(batch+0x18)=ships[0].node;
    /* Everything else is forwarded unchanged to the native queue. */
    unsigned jobs=test_tr_jobs;
    assert(TR_CALL(test_tr_queue)==7 && test_tr_queued==1);
    *(uint64_t*)(payload+0x20)=1;assert(TR_CALL(test_tr_job)==7 && test_tr_queued==2);*(uint64_t*)(payload+0x20)=0;
    *(void**)(payload+8)=NULL;assert(TR_CALL(test_tr_job)==7 && test_tr_queued==3);*(void**)(payload+8)=image+TRAVEL_BATCH_JOB;
    boot_phase=2;assert(TR_CALL(test_tr_job)==7 && test_tr_queued==4);boot_phase=3;
    InterlockedExchange(&travel_active,0);assert(TR_CALL(test_tr_job)==7 && test_tr_queued==5);InterlockedExchange(&travel_active,1);
    assert(test_tr_jobs==jobs && travel_forwarded==5 && !travel_failures);
    /* An unreadable batch refuses before the job runs; it is queued natively and counted. */
    *(unsigned char**)(batch+0x10)=NULL;assert(TR_CALL(test_tr_job)==7 && test_tr_jobs==jobs && travel_failures==1 && !strcmp(travel_stage,"batch"));
    *(unsigned char**)(batch+0x10)=sentinel;
    assert(travel_queue_impl(test_tr_queue,(void*)test_tr_job,image+TRAVEL_BATCH_JOB,NULL,NULL,(void*)test_tr_job,payload,1,0)==7 &&
           travel_failures==2 && !strcmp(travel_stage,"tls"));
    #undef TR_CALL
    /* Restoration only replaces words that still hold this patch. */
    travel_original[0]=0x1111;travel_original[1]=0x2222;travel_patched[0]=0x3333;travel_patched[1]=0x4444;
    *(uint64_t*)(image+TRAVEL_SITE_A)=0x3333;*(uint64_t*)(image+TRAVEL_SITE_B)=0x5555;
    travel_installed=1;travel_site_patched[0]=travel_site_patched[1]=1;travel_restore();
    assert(travel_installed && !travel_restored && *(uint64_t*)(image+TRAVEL_SITE_A)==0x1111 && *(uint64_t*)(image+TRAVEL_SITE_B)==0x5555 && !travel_active);
    *(uint64_t*)(image+TRAVEL_SITE_B)=0x4444;travel_restore();
    assert(!travel_installed && travel_restored && *(uint64_t*)(image+TRAVEL_SITE_B)==0x2222 && travel_owned());
    assert(!travel_arm()); /* No active boot gate in this harness. */
    char status[512];assert(travel_status(status,sizeof(status)) && strstr(status,"failures=2"));
    travel_failures=travel_forwarded=0;travel_restored=0;
    stats.active=saved_active;boot_phase=saved_phase;stats.issued=saved_issued;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    printf("travel determinism inline order/TLS/key/forward/restore tests PASS\n");
}
