/* native_gate_attention_all: arming, the per-key first-call due pin, the
 * (tick, key) seeded draw restored afterwards, unmapped objects, refusals. */
static uint64_t test_aa_seen_rng,test_aa_seen_due_bits;
static unsigned test_aa_calls;
static void test_aa_original(void *self) {
    unsigned char *object=self;++test_aa_calls;
    memcpy(&test_aa_seen_due_bits,object+0x2e0,8);
    test_aa_seen_rng=*(uint64_t*)(object+0x300); /* the test passes the object's own word as the TLS word */
    *(uint64_t*)(object+0x300)^=0x1234; /* the native draw advances the word */
}
static void test_attention_all(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    int saved_installed=attention_installed;uint64_t saved_failures=attention_failures;
    world_identity_entry *prior=world_identities;unsigned prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x6161,0x176a99) && world_identity_record(0x6262,0x29bfd));
    static unsigned char object[0x400];memset(object,0,sizeof(object));*(uint64_t*)(object+8)=0x6161;
    uint64_t *rng=(uint64_t*)(object+0x300);double due,v;
    /* Arming: the attention hook installed, before the first admitted frame, on the MoveWorker. */
    attention_all_armed=0;attention_installed=0;stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!attention_all_arm());attention_installed=1;stats.issued=1;assert(!attention_all_arm());stats.issued=0;
    ui_thread=GetCurrentThreadId()+1;assert(!attention_all_arm());ui_thread=GetCurrentThreadId();
    boot_phase=3;assert(!attention_all_arm());boot_phase=0;
    assert(attention_all_arm() && attention_all_armed && attention_all_arm() && !strcmp(attention_all_stage,"armed"));
    /* First admitted call of a key: due pinned to now - 1/60, the draw seeded from (tick, key), the word restored. */
    stats.issued=40;boot_phase=3;attention_failures=0;test_aa_calls=0;
    v=11.25;memcpy(object+0x2e0,&v,8);*rng=0xabcdef;
    attention_all_apply(test_aa_original,object,rng,5.5);
    memcpy(&due,&test_aa_seen_due_bits,8);
    assert(test_aa_calls==1 && due==5.5-1.0/60.0 && *rng==0xabcdef);
    assert(test_aa_seen_rng==lockstep_seed(ATTENTION_ALL_DOMAIN,40,0x176a99,WORLD_KEY_SAVED,0,0));
    assert(attention_all_inits==1 && attention_all_calls==1 && attention_all_count==1 && attention_all_first==40 && attention_all_frame_inits==1);
    /* Later calls of the key: the native due stays, the seed follows the tick only (not the word or earlier draws). */
    v=6.4;memcpy(object+0x2e0,&v,8);*rng=0x77;stats.issued=41;
    attention_all_apply(test_aa_original,object,rng,5.6);memcpy(&due,&test_aa_seen_due_bits,8);
    assert(due==6.4 && *rng==0x77 && attention_all_inits==1 && attention_all_calls==2 && attention_all_frame_calls==1 && !attention_all_frame_inits);
    assert(test_aa_seen_rng==lockstep_seed(ATTENTION_ALL_DOMAIN,41,0x176a99,WORLD_KEY_SAVED,0,0));
    *rng=0x99;attention_all_apply(test_aa_original,object,rng,5.6);
    assert(test_aa_seen_rng==lockstep_seed(ATTENTION_ALL_DOMAIN,41,0x176a99,WORLD_KEY_SAVED,0,0) && *rng==0x99);
    /* Another key: its own first call. */
    *(uint64_t*)(object+8)=0x6262;v=9.0;memcpy(object+0x2e0,&v,8);
    attention_all_apply(test_aa_original,object,rng,5.7);memcpy(&due,&test_aa_seen_due_bits,8);
    assert(due==5.7-1.0/60.0 && attention_all_inits==2 && attention_all_count==2 && attention_all_first==40);
    /* Unmapped: native path with the natural word, nothing pinned. */
    *(uint64_t*)(object+8)=0x9999;v=8.0;memcpy(object+0x2e0,&v,8);*rng=0x55;
    attention_all_apply(test_aa_original,object,rng,5.8);memcpy(&due,&test_aa_seen_due_bits,8);
    assert(due==8.0 && test_aa_seen_rng==0x55 && *rng==(0x55^0x1234) && attention_all_unmapped==1 && attention_all_count==2 && attention_all_unmapped_ids==1);
    attention_all_apply(test_aa_original,object,rng,5.81);assert(attention_all_unmapped==2 && attention_all_unmapped_ids==1);attention_all_unmapped=1; /* per call vs per object */
    /* No TLS word or a non-finite time: native call, a failure (stop reason 54). */
    *(uint64_t*)(object+8)=0x6161;unsigned before=test_aa_calls;
    attention_all_apply(test_aa_original,object,NULL,5.9);assert(test_aa_calls==before+1 && attention_failures==1 && !strcmp(attention_all_stage,"tls"));
    attention_all_apply(test_aa_original,object,rng,NAN);assert(attention_failures==2);
    /* A full table fails closed. */
    attention_failures=0;unsigned saved_count=attention_all_count;attention_all_count=ATTENTION_ALL_TABLE/4*3;
    assert(world_identity_record(0x6363,0x30000));*(uint64_t*)(object+8)=0x6363;
    attention_all_apply(test_aa_original,object,rng,6.0);assert(attention_failures==1 && !strcmp(attention_all_stage,"table"));
    attention_all_count=saved_count;
    /* Status. */
    char out[512];stats.issued=41;assert(attention_all_status(out,sizeof(out))>0);
    assert(strstr(out,"armed=1 installed=1 frame=41 ") && strstr(out," total_inits=2 ids=2 unmapped=1 unmapped_ids=1 first=40 stage=table failures=1"));
    attention_all_armed=0;memset(attention_all_table,0,sizeof(attention_all_table));attention_all_count=0;memset(attention_all_unmapped_table,0,sizeof(attention_all_unmapped_table));attention_all_unmapped_ids=0;attention_all_stage="idle";
    attention_all_calls=attention_all_inits=attention_all_unmapped=attention_all_first=0;attention_all_tick=UINT64_MAX;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    attention_installed=saved_installed;attention_failures=saved_failures;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
}
