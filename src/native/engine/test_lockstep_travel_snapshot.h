/* Travel snapshot: padding copy, order-independent receipt, join/release
 * sequencing, completion drain, pre-patch pins and restoration. */
static unsigned test_ts_jobs;
static void test_ts_job(void *slot,void *scratch) {assert(slot==(void*)&test_ts_jobs && !scratch);++test_ts_jobs;}
static void test_ts_sha(const void *data,ULONG length,char out[65]) {
    BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;unsigned char raw[32];
    assert(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 && BCryptCreateHash(a,&h,NULL,0,NULL,0,0)>=0);
    assert(BCryptHashData(h,(PUCHAR)data,length,0)>=0 && BCryptFinishHash(h,raw,32,0)>=0);
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);hex32(raw,out);
}
static void test_ts_layout(unsigned char *manager,unsigned char batches[2][0x20],unsigned char sentinels[3][0x18],test_pd_ship *ships,int swapped) {
    /* Batch A holds ships 0,1 and batch B ship 2; the fresh list holds ship 3. */
    const unsigned order[2]={swapped?1u:0u,swapped?0u:1u};
    memset(manager,0,0x220);
    for(unsigned b=0;b<2;b++) {memset(batches[b],0,0x20);*(unsigned char**)(batches[b]+0x10)=sentinels[b];}
    *(uint64_t*)(batches[order[0]]+8)=2;*(unsigned char**)(sentinels[order[0]]+8)=ships[0].node;
    *(unsigned char**)(ships[0].node+8)=ships[1].node;*(unsigned char**)(ships[1].node+8)=sentinels[order[0]];
    *(uint64_t*)(batches[order[1]]+8)=1;*(unsigned char**)(sentinels[order[1]]+8)=ships[2].node;*(unsigned char**)(ships[2].node+8)=sentinels[order[1]];
    *(unsigned char**)(sentinels[2]+8)=ships[3].node;*(unsigned char**)(ships[3].node+8)=sentinels[2];
    *(unsigned char**)(manager+0x180)=batches[0];*(unsigned char**)(manager+0x188)=batches[0]+0x40;
    *(unsigned char**)(manager+0x198)=batches[0]+0x40;*(unsigned char**)(manager+0x1a0)=batches[0]+0x40;
    *(unsigned char**)(manager+0x178)=sentinels[2];
}
static void test_travel_snapshot(void) {
    static test_pd_ship ships[4];static unsigned char manager[0x220],batches[2][0x20],sentinels[3][0x18];
    int owned_identities=!world_identities;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);
    /* Ships 0..2 carry saved IDs; ship 3 was created after load (runtime ID only). */
    for(unsigned i=0;i<4;i++) {
        test_pd_link(&ships[i],0xa000+i,NULL,NULL);ships[i].agent[TSNAP_FLAG]=(unsigned char)(i&1);ships[i].agent[TSNAP_PAD]=0xcc;
        if(i<3) assert(world_identity_record(0xa000+i,0x60000+i));
    }
    uint64_t agents,flagged,fresh,hash,unmapped,unmapped_flagged,first;
    test_ts_layout(manager,batches,sentinels,ships,0);
    assert(tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged) && agents==4 && flagged==2 && fresh==1 &&
           unmapped==1 && unmapped_flagged==1);
    /* The receipt is exactly the saved-ID sum over the three mapped agents. */
    assert(hash==lockstep_mix(0x60000ULL*2)+lockstep_mix(0x60001ULL*2+1)+lockstep_mix(0x60002ULL*2));
    for(unsigned i=0;i<4;i++) assert(ships[i].agent[TSNAP_PAD]==ships[i].agent[TSNAP_FLAG]);
    first=hash;test_ts_layout(manager,batches,sentinels,ships,1);
    assert(tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged) && hash==first); /* batch order is irrelevant */
    ships[2].agent[TSNAP_FLAG]=1;assert(tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged) && hash!=first && flagged==3);
    ships[2].agent[TSNAP_FLAG]=0;
    /* An unmapped agent's process-local runtime ID and its flag never enter the receipt. */
    *(uint64_t*)(ships[3].object+8)=0xbeef;ships[3].agent[TSNAP_FLAG]=0;
    assert(tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged) && hash==first &&
           unmapped==1 && !unmapped_flagged && flagged==1 && ships[3].agent[TSNAP_PAD]==0);
    *(uint64_t*)(ships[3].object+8)=0xa003;ships[3].agent[TSNAP_FLAG]=1;
    *(uint64_t*)(batches[0]+8)=3;assert(!tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged)); /* count must close the ring */
    test_ts_layout(manager,batches,sentinels,ships,0);*(unsigned char**)(manager+0x178)=NULL;
    assert(!tsnap_snapshot(manager,&agents,&flagged,&fresh,&hash,&unmapped,&unmapped_flagged));
    test_ts_layout(manager,batches,sentinels,ships,0);
    /* Completion wrapper and drain. */
    tsnap_started=tsnap_done=0;tsnap_job_run(test_ts_job,&test_ts_jobs,NULL);
    assert(test_ts_jobs==1 && tsnap_started==1 && tsnap_done==1);
    uint64_t waited=0;assert(tsnap_drain(0,&waited));
    InterlockedIncrement64(&tsnap_started);assert(!tsnap_drain(0,&waited) && !strcmp(tsnap_stage,"drain_timeout"));
    InterlockedIncrement64(&tsnap_done);
    /* Sequencing: the first joined live pass installs the read; later passes snapshot first. */
    uint64_t word=0x7bb9800975000000ULL;memcpy(image+TSNAP_READ_WORD,&word,8); /* live bytes 00 00 00 75 09 80 b9 7b */
    tsnap_read_original=word;tsnap_read_patched=(word&0x00ffffffffffffffULL)|((uint64_t)TSNAP_DISP_PAD<<56);
    assert((tsnap_read_original>>56)==TSNAP_DISP_FLAG);
    uint64_t saved_issued=stats.issued;stats.issued=1;
    tsnap_installed=1;tsnap_read_installed=tsnap_idle=tsnap_released=0;tsnap_frames=0;
    assert(!tsnap_after_join(manager) && !strcmp(tsnap_stage,"not_released"));
    for(unsigned i=0;i<4;i++) ships[i].agent[TSNAP_PAD]=0xcc;
    assert(tsnap_before_release(manager) && !tsnap_frames && ships[0].agent[TSNAP_PAD]==0xcc); /* not provably idle yet */
    assert(tsnap_after_join(manager) && tsnap_read_installed && tsnap_idle && tsnap_frames==1);
    assert(*(uint64_t*)(image+TSNAP_READ_WORD)==tsnap_read_patched && ships[1].agent[TSNAP_PAD]==1);
    ships[1].agent[TSNAP_FLAG]=0;stats.issued=2;
    assert(tsnap_before_release(manager) && tsnap_frames==2 && ships[1].agent[TSNAP_PAD]==0 && tsnap_tick==2 && !tsnap_idle);
    assert(!tsnap_before_release(manager) && !strcmp(tsnap_stage,"not_idle"));
    tsnap_released=1;assert(tsnap_after_join(manager) && tsnap_idle);
    /* Other features' whole-function pins still see the original byte. */
    static unsigned char region[64];memcpy(region,image+TSNAP_READ_WORD-16,64);region[16+7]=TSNAP_DISP_FLAG;
    char expected[65];test_ts_sha(region,64,expected);
    assert(tsnap_unpatched_hash(TSNAP_READ_WORD-16,64,expected));
    tsnap_read_installed=0;assert(!tsnap_unpatched_hash(TSNAP_READ_WORD-16,64,expected));tsnap_read_installed=1;
    /* Restoration: read first, then both callback windows; foreign bytes stay. */
    unsigned __int128 la=((unsigned __int128)0x1111<<64)|0x2222,lb=((unsigned __int128)0x3333<<64)|0x4444;
    tsnap_lea_original[0]=la;tsnap_lea_original[1]=lb;tsnap_lea_patched[0]=la+1;tsnap_lea_patched[1]=lb+1;
    memcpy(image+TSNAP_LEA_A,&tsnap_lea_patched[0],16);unsigned __int128 foreign=lb+7;memcpy(image+TSNAP_LEA_B,&foreign,16);
    tsnap_lea_installed[0]=tsnap_lea_installed[1]=1;InterlockedExchange(&tsnap_active,1);
    assert(tsnap_owned()==0);tsnap_restore();
    assert(tsnap_installed && !tsnap_restored && !tsnap_read_installed && *(uint64_t*)(image+TSNAP_READ_WORD)==tsnap_read_original &&
           !memcmp(image+TSNAP_LEA_A,&la,16) && tsnap_lea_installed[1] && !tsnap_active);
    memcpy(image+TSNAP_LEA_B,&tsnap_lea_patched[1],16);tsnap_restore();
    assert(!tsnap_installed && tsnap_restored && !memcmp(image+TSNAP_LEA_B,&lb,16) && tsnap_owned());
    assert(!tsnap_arm()); /* No active boot gate in this harness. */
    char status[640];assert(tsnap_status(status,sizeof(status)) && strstr(status,"installed=0 read=0 tick=2 frames=2"));
    tsnap_restored=0;tsnap_started=tsnap_done=0;tsnap_frames=0;tsnap_tick=UINT64_MAX;stats.issued=saved_issued;
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    printf("travel snapshot copy/portable receipt/sequencing/drain/pin/restore tests PASS\n");
}
