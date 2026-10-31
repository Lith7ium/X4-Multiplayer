/* Offline tests for the direct MD cue checks (native_gate_md_timed_rng) and
 * the boot hold (native_gate_boot_hold): call-site classification, keys of
 * the three new sources, listener nesting, foreign/pass-through paths, the
 * 16-byte and 8-byte call-site CAS, admission and bounded status. Synthetic
 * cues, director and call bytes live in the placeholder image; no engine. */
static test_sr_md test_mdt_cue2;
static uint64_t test_mdt_seen[4];
static unsigned test_mdt_draws=3,test_mdt_original_calls;
static uintptr_t test_mdt_original(void *cue,void *a,void *b,void *c) {
    (void)a;(void)b;(void)c;assert(cue);test_mdt_seen[0]=*test_sr_word;test_sr_draw(test_mdt_draws);++test_mdt_original_calls;return 0;
}
/* Cue1's scope: 2 draws, a listener cue2 checked through the raise site (own
 * key, restored), cue1 itself through the raise site (same: continues), 1 draw. */
static uintptr_t test_mdt_parent(void *cue,void *a,void *b,void *c) {
    (void)a;(void)b;(void)c;test_mdt_seen[1]=*test_sr_word;test_sr_draw(2);test_mdt_seen[2]=*test_sr_word;
    md_timed_run((uintptr_t)(image+0x111e50a),test_mdt_original,test_sr_word,test_mdt_cue2.cue);
    assert(*test_sr_word==test_mdt_seen[2]);
    md_timed_run((uintptr_t)(image+0x111e50a),test_mdt_original,test_sr_word,cue);
    test_sr_draw(1);return 0x55;
}
static unsigned test_bh_calls;
static void *test_bh_seen;
static void test_bh_move(void *director) {test_bh_seen=director;++test_bh_calls;}
static uint64_t test_mdt_detail(uint32_t keyword) {return lockstep_mix(((uint64_t)keyword<<32)^lockstep_mix(0));}
static void test_mdt_record(test_sr_md *m,uint32_t keyword,uint32_t tag,uint64_t payload) {
    *(uint32_t*)(m->cue+0x18)=keyword;*(uint32_t*)(m->cue+0x20)=tag;*(uint64_t*)(m->cue+0x28)=payload;
}
static void test_mdt_timed(void) {
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(0x7001,0x77));
    test_sr_md_init(&test_sr_cue);test_sr_md_init(&test_mdt_cue2);test_npc_string(test_mdt_cue2.node+0x20,"Cue2");
    stats.active=1;boot_mode=1;boot_phase=3;stats.issued=21;ui_thread=GetCurrentThreadId();
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;script_rng_mode=SCRIPT_RNG_KEY;InterlockedExchange(&script_rng_active,1);test_sr_reset();
    memset(script_rng_slots,0,sizeof(script_rng_slots));

    /* The return address names the site; anything else is foreign. */
    assert(md_timed_site_of((uintptr_t)(image+0x1122090))==0 && md_timed_site_of((uintptr_t)(image+0x111d29b))==1);
    assert(md_timed_site_of((uintptr_t)(image+0x111e50a))==2 && md_timed_site_of((uintptr_t)(image+0x112208b))==MD_TIMED_SITES);
    assert(md_timed_site_of((uintptr_t)(image+0x111f990))==MD_TIMED_SITES && md_timed_source[0]==SR_TIMED &&
           md_timed_source[1]==SR_WAIT && md_timed_source[2]==SR_RAISE);

    /* Timed check of a non-event cue (empty record): outermost, keyed by the
     * cue name, SR_TIMED and the per-tick ordinal; the word comes back. */
    uint64_t owner1=test_sr_fnv("Other.Cue1"),owner2=test_sr_fnv("Other.Cue2");
    uint64_t timed=script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_TIMED,SCRIPT_RNG_MD,0);
    uint64_t word=0x1234;test_sr_word=&word;test_mdt_record(&test_sr_cue,0,0,0);
    md_timed_run((uintptr_t)(image+0x1122090),test_mdt_original,&word,test_sr_cue.cue);
    uint64_t k1=script_rng_key(SR_TIMED,21,owner1,timed,1,test_mdt_detail(0));
    assert(test_mdt_seen[0]==k1 && word==0x1234 && md_timed_calls[0]==1 && script_rng_sources[SR_TIMED]==1 && script_rng_outer==1);
    script_rng_row_t *r=&script_rng_ring[0];
    assert(script_rng_cursor==1 && r->source==SR_TIMED && r->kind==SCRIPT_RNG_MD && r->state==SR_OUTER && r->draws==3 &&
           r->owner==owner1 && r->ordinal==1 && r->seeded && !strcmp(r->name,"Other.Cue1"));
    md_timed_run((uintptr_t)(image+0x1122090),test_mdt_original,&word,test_sr_cue.cue);
    assert(test_mdt_seen[0]==script_rng_key(SR_TIMED,21,owner1,timed,2,test_mdt_detail(0)) && word==0x1234);
    /* Another source is another key family, not another rank of this one. */
    assert(k1!=script_rng_key(SR_CUE,21,owner1,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),1,test_mdt_detail(0)));
    char row[640];assert(script_rng_row(0,row,sizeof(row))>0 && strstr(row," src=6 state=1 kind=2 "));

    /* Enter-waiting self check: record keyword 0x636 with the cue's own
     * runtime ID (value type 0x12), which never enters the key. */
    test_mdt_record(&test_sr_cue,0x636,0x12,0xdeadbeef);
    md_timed_run((uintptr_t)(image+0x111d29b),test_mdt_original,&word,test_sr_cue.cue);
    uint64_t kw=script_rng_key(SR_WAIT,21,owner1,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_WAIT,SCRIPT_RNG_MD,0),1,test_mdt_detail(0x636));
    assert(test_mdt_seen[0]==kw && word==0x1234 && md_timed_calls[1]==1 && script_rng_sources[SR_WAIT]==1);
    test_mdt_record(&test_sr_cue,0x636,0x12,0x1111);
    md_timed_run((uintptr_t)(image+0x111d29b),test_mdt_original,&word,test_sr_cue.cue);
    assert(test_mdt_seen[0]==script_rng_key(SR_WAIT,21,owner1,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_WAIT,SCRIPT_RNG_MD,0),2,test_mdt_detail(0x636)));

    /* Listener inside a keyed cue event: cue2 gets the context-free SR_RAISE
     * key and cue1's word is restored after it; cue1 raising onto itself
     * continues its own stream. */
    test_sr_reset();memset(script_rng_slots,0,sizeof(script_rng_slots));script_rng_now.open=0;stats.issued=22;word=0x99;
    test_mdt_record(&test_sr_cue,0x633,0xa,0x7001);test_mdt_record(&test_mdt_cue2,0x637,0x12,0x4242);test_mdt_draws=4;
    assert(script_rng_event_run(SR_CUE,test_mdt_parent,&word,test_sr_cue.cue,NULL,NULL,NULL)==0x55);
    uint64_t parent=script_rng_key(SR_CUE,22,owner1,script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_CUE,SCRIPT_RNG_MD,0),1,
        lockstep_mix((0x633ULL<<32)^lockstep_mix(0x77)));
    uint64_t raise=script_rng_stream(SCRIPT_RNG_MD_SCRIPT,SR_RAISE,SCRIPT_RNG_MD,0);
    uint64_t child=script_rng_key(SR_RAISE,22,owner2,raise,1,test_mdt_detail(0x637));
    uint64_t expect=parent;for(unsigned i=0;i<2;i++) expect=test_sr_step(expect);
    assert(test_mdt_seen[1]==parent && test_mdt_seen[2]==expect && word==0x99);
    assert(script_rng_child==1 && script_rng_same==1 && script_rng_outer==1 && md_timed_calls[2]==2 && !script_rng_failures);
    assert(script_rng_cursor==2 && script_rng_ring[0].state==SR_CHILD && script_rng_ring[0].key==child && script_rng_ring[0].parent==parent &&
           script_rng_ring[0].source==SR_RAISE && script_rng_ring[0].draws==4 && !strcmp(script_rng_ring[0].name,"Other.Cue2"));
    assert(script_rng_ring[1].state==SR_OUTER && script_rng_ring[1].draws==2+4+1 && script_rng_ring[1].key==parent);
    /* Context-free: the same listener checked outermost gets the same key family. */
    test_sr_reset();memset(script_rng_slots,0,sizeof(script_rng_slots));script_rng_now.open=0;word=7;
    md_timed_run((uintptr_t)(image+0x111e50a),test_mdt_original,&word,test_mdt_cue2.cue);
    assert(test_mdt_seen[0]==child && word==7 && script_rng_outer==1);
    test_mdt_draws=3;

    /* Foreign return address: runs unscoped and counts a failure. */
    test_sr_reset();word=0x10;test_mdt_original_calls=0;
    md_timed_run((uintptr_t)(image+1),test_mdt_original,&word,test_sr_cue.cue);
    uint64_t natural=0x10;for(unsigned i=0;i<3;i++) natural=test_sr_step(natural);
    assert(test_mdt_original_calls==1 && word==natural && md_timed_foreign==1 && md_timed_failures==1 && !script_rng_cursor);
    /* Boot phase 2 without the boot bit: counted call, plain pass-through. */
    boot_phase=2;stats.issued=0;word=0x20;
    md_timed_run((uintptr_t)(image+0x1122090),test_mdt_original,&word,test_sr_cue.cue);
    assert(test_mdt_seen[0]==0x20 && !script_rng_cursor && md_timed_calls[0]==3);
    script_rng_mask|=SCRIPT_RNG_BOOT;md_timed_run((uintptr_t)(image+0x1122090),test_mdt_original,&word,test_sr_cue.cue);
    assert(script_rng_cursor==1 && script_rng_ring[0].tick==0 && script_rng_ring[0].source==SR_TIMED);
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;boot_phase=3;stats.issued=23;
    /* MD masked off: transparent. */
    script_rng_mask=SCRIPT_RNG_AI;word=0x30;md_timed_run((uintptr_t)(image+0x1122090),test_mdt_original,&word,test_sr_cue.cue);
    assert(test_mdt_seen[0]==0x30 && script_rng_why[SR_MASKED]==1);script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;

    /* Call-site CAS: E8 to Cue_CheckConditions becomes E8 to the thunk slot,
     * only inside the aligned 16-byte block; restore needs our exact bytes. */
    unsigned char *saved_thunk=thunk;thunk=image+0x6000000;
    for(unsigned i=0;i<MD_TIMED_SITES;i++) {
        unsigned char *site=image+md_timed_site[i];int32_t rel=(int32_t)((intptr_t)(image+MD_TIMED_CHECK)-(intptr_t)(site+5));
        memset(image+(md_timed_site[i]&~15u),0x90,16);site[0]=0xe8;memcpy(site+1,&rel,4);
    }
    unsigned char before[3][16];for(unsigned i=0;i<MD_TIMED_SITES;i++) memcpy(before[i],image+(md_timed_site[i]&~15u),16);
    image[md_timed_site[1]]=0xe9;assert(!md_timed_prepare(1));image[md_timed_site[1]]=0xe8;
    *(int32_t*)(image+md_timed_site[2]+1)+=1;assert(!md_timed_prepare(2));*(int32_t*)(image+md_timed_site[2]+1)-=1;
    for(unsigned i=0;i<MD_TIMED_SITES;i++) {
        assert(md_timed_prepare(i) && md_timed_swap(i,md_timed_original[i],md_timed_patched[i]));md_timed_installed[i]=1;
        int32_t rel;memcpy(&rel,image+md_timed_site[i]+1,4);
        assert(image+md_timed_site[i]+5+rel==thunk+MD_TIMED_THUNK && image[md_timed_site[i]]==0xe8);
        unsigned at=md_timed_site[i]&15u;
        for(unsigned j=0;j<16;j++) if(j<at+1 || j>at+4) assert(image[(md_timed_site[i]&~15u)+j]==before[i][j]);
    }
    md_timed_armed=1;assert(md_timed_owned() && md_timed_sites_installed()==3 && !md_timed_swap(0,md_timed_original[0],md_timed_patched[0]));
    image[md_timed_site[0]+1]^=1;assert(!md_timed_owned());
    md_timed_restore();assert(!md_timed_restored && md_timed_installed[0] && !md_timed_installed[1] && !md_timed_installed[2]);
    assert(!memcmp(image+(md_timed_site[1]&~15u),before[1],16) && !memcmp(image+(md_timed_site[2]&~15u),before[2],16));
    image[md_timed_site[0]+1]^=1;md_timed_restore();
    assert(md_timed_restored && !md_timed_sites_installed() && !memcmp(image+(md_timed_site[0]&~15u),before[0],16) && md_timed_owned());
    md_timed_armed=md_timed_restored=0;thunk=saved_thunk;

    /* Arming refusals precede any write. */
    boot_phase=0;stats.issued=0;script_rng_armed=0;assert(!md_timed_arm());
    script_rng_armed=1;script_rng_mask=SCRIPT_RNG_AI;assert(!md_timed_arm());
    script_rng_mask=SCRIPT_RNG_AI|SCRIPT_RNG_MD;assert(!md_timed_arm()); /* pins fail on the placeholder image */
    assert(!md_timed_armed && !md_timed_sites_installed());
    for(unsigned i=0;i<MD_TIMED_SITES;i++) assert(!memcmp(image+(md_timed_site[i]&~15u),before[i],16));
    script_rng_armed=0;

    /* Bounded status. */
    char status[384];assert(md_timed_status(status,sizeof(status))>0 && strstr(status,"armed=0 sites=0 timed_calls=5 "));
    printf("NATIVE_GATE MD_TIMED_RNG tick=23 %s\n",status);
    for(unsigned i=0;i<MD_TIMED_SITES;i++) md_timed_calls[i]=INT64_MAX;
    md_timed_foreign=md_timed_failures=INT64_MAX;md_timed_armed=md_timed_restored=INT32_MIN;
    int n=md_timed_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE MD_TIMED_RNG tick=18446744073709551615 \n")<sizeof(status));
    md_timed_armed=md_timed_restored=0;md_timed_foreign=md_timed_failures=0;memset((void*)md_timed_calls,0,sizeof(md_timed_calls));

    test_sr_reset();InterlockedExchange(&script_rng_active,0);script_rng_mask=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native MD timed/wait/raise cue check site/key/nesting/CAS tests PASS");
}
static void test_boot_hold(void) {
    static unsigned char director[0x240],other[0x240];static void *wakes[8];
    memset(director,0,sizeof(director));
    uintptr_t *v=(uintptr_t*)(director+0x200);
    v[0]=(uintptr_t)wakes;v[1]=(uintptr_t)(wakes+3);v[2]=(uintptr_t)(wakes+8);v[3]=(uintptr_t)(wakes+4);v[4]=(uintptr_t)(wakes+5);
    assert(boot_hold_wakes(director)==4 && boot_hold_wakes(NULL)==UINT64_MAX);
    v[1]=(uintptr_t)wakes+3;assert(boot_hold_wakes(director)==UINT64_MAX);v[1]=(uintptr_t)(wakes+3);
    void *saved_director=*(void**)(image+BOOT_HOLD_DIRECTOR);*(void**)(image+BOOT_HOLD_DIRECTOR)=director;
    stats.active=1;boot_mode=1;boot_phase=2;stats.issued=0;ui_thread=GetCurrentThreadId();
    boot_hold_armed=1;InterlockedExchange(&boot_hold_active,1);

    /* Phase 2, nothing admitted: the post-load Move is held. */
    boot_hold_execute(test_bh_move,director);
    assert(!test_bh_calls && boot_hold_held==1 && boot_hold_pending==4 && boot_hold_moves==1 && !strcmp(boot_hold_stage,"held"));
    /* Another director or thread in phase 2 is never held: it runs and fails. */
    boot_hold_execute(test_bh_move,other);
    assert(test_bh_calls==1 && test_bh_seen==other && boot_hold_failures==1 && boot_hold_held==1);
    ui_thread=GetCurrentThreadId()+1;boot_hold_execute(test_bh_move,director);
    assert(test_bh_calls==2 && boot_hold_failures==2 && boot_hold_held==1);ui_thread=GetCurrentThreadId();boot_hold_failures=0;
    /* Phase 1 (menu universe) and an inactive hold pass through. */
    boot_phase=1;boot_hold_execute(test_bh_move,director);assert(test_bh_calls==3 && boot_hold_held==1);
    boot_phase=2;InterlockedExchange(&boot_hold_active,0);boot_hold_execute(test_bh_move,director);
    assert(test_bh_calls==4 && boot_hold_held==1);InterlockedExchange(&boot_hold_active,1);

    /* Admission requires a held call; the first gated Move releases it. */
    boot_hold_held=0;boot_phase=3;assert(!boot_hold_admit() && !boot_hold_admitted && !strcmp(boot_hold_stage,"admit_unheld"));
    boot_hold_held=1;assert(boot_hold_admit() && boot_hold_admitted && boot_hold_admit_pending==4);
    stats.issued=1;boot_hold_execute(test_bh_move,director);
    assert(test_bh_calls==5 && test_bh_seen==director && boot_hold_released==1 && boot_hold_release_pending==4 && !strcmp(boot_hold_stage,"released"));
    stats.issued=2;boot_hold_execute(test_bh_move,director);assert(test_bh_calls==6 && boot_hold_released==1 && !boot_hold_failures);
    boot_hold_armed=0;assert(boot_hold_admit());boot_hold_armed=1;

    /* Call-site CAS: only the rel32 of E8 at 0x9fe093 inside the aligned
     * qword changes; restore needs our exact word. */
    unsigned char *saved_thunk=thunk;thunk=image+0x6000000;
    memset(image+BOOT_HOLD_WORD,0x90,16);image[BOOT_HOLD_SITE]=0xe8;
    int32_t rel=(int32_t)((intptr_t)(image+BOOT_HOLD_MOVE)-(intptr_t)(image+BOOT_HOLD_SITE+5));memcpy(image+BOOT_HOLD_SITE+1,&rel,4);
    memcpy(&boot_hold_original_word,image+BOOT_HOLD_WORD,8);boot_hold_patched_word=boot_hold_original_word;
    rel=(int32_t)((intptr_t)(thunk+BOOT_HOLD_THUNK)-(intptr_t)(image+BOOT_HOLD_SITE+5));
    memcpy((unsigned char*)&boot_hold_patched_word+(BOOT_HOLD_SITE+1-BOOT_HOLD_WORD),&rel,4);
    assert(((unsigned char*)&boot_hold_patched_word)[3]==0xe8 && !memcmp(&boot_hold_patched_word,&boot_hold_original_word,3));
    assert(boot_hold_swap(boot_hold_original_word,boot_hold_patched_word));boot_hold_installed=1;
    assert(boot_hold_owned() && !boot_hold_swap(boot_hold_original_word,boot_hold_patched_word));
    image[BOOT_HOLD_WORD]^=1;assert(!boot_hold_owned());
    boot_hold_restore();assert(!boot_hold_restored && boot_hold_installed && !boot_hold_active);
    image[BOOT_HOLD_WORD]^=1;boot_hold_restore();
    assert(boot_hold_restored && !boot_hold_installed && *(uint64_t*)(image+BOOT_HOLD_WORD)==boot_hold_original_word && boot_hold_owned());
    /* A stopped hold passes through even in phase 2. */
    boot_phase=2;stats.issued=0;boot_hold_execute(test_bh_move,director);assert(test_bh_calls==7);
    thunk=saved_thunk;

    /* Arming refusals precede any write (pins fail on the placeholder). */
    boot_hold_armed=0;boot_phase=0;assert(!boot_hold_arm() && !boot_hold_installed);
    assert(*(uint64_t*)(image+BOOT_HOLD_WORD)==boot_hold_original_word);
    stats.active=0;assert(!boot_hold_arm());stats.active=1;

    /* Bounded status. */
    char status[512];assert(boot_hold_status(status,sizeof(status))>0 && strstr(status," held=1 ") && strstr(status," released=1 "));
    printf("NATIVE_GATE BOOT_HOLD tick=2 %s\n",status);
    boot_hold_armed=boot_hold_installed=boot_hold_restored=boot_hold_admitted=INT32_MIN;InterlockedExchange(&boot_hold_active,(LONG)(-2147483647-1));
    boot_hold_held=boot_hold_moves=boot_hold_released=UINT64_MAX;boot_hold_pending=boot_hold_admit_pending=boot_hold_release_pending=(uint64_t)INT64_MIN;
    boot_hold_failures=INT64_MAX;boot_hold_stage="admit_unheld";
    int n=boot_hold_status(status,sizeof(status));
    assert(n>0 && (unsigned)n+strlen("[X4LAN] NATIVE_GATE BOOT_HOLD tick=18446744073709551615 \n")<sizeof(status));
    boot_hold_armed=boot_hold_installed=boot_hold_restored=boot_hold_admitted=0;InterlockedExchange(&boot_hold_active,0);
    boot_hold_held=boot_hold_moves=boot_hold_released=0;boot_hold_pending=boot_hold_admit_pending=boot_hold_release_pending=UINT64_MAX;
    boot_hold_failures=0;boot_hold_stage="idle";
    *(void**)(image+BOOT_HOLD_DIRECTOR)=saved_director;stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;
    puts("native boot hold phase-2 AIDirector hold/admission/release/CAS tests PASS");
}
/* With the X4 9.00 executable: every pin, the three E8 sites to
 * Cue_CheckConditions, the E8 to AIDirector::Move after the g_AIDirector
 * load, and one-byte tamper refusals. */
static void test_mdt_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image,*prior_thunk=thunk;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);thunk=image+0x6f00000;
    assert(md_timed_pinned() && boot_hold_pinned());
    for(unsigned i=0;i<MD_TIMED_SITES;i++) {
        assert(md_timed_prepare(i));int32_t rel;memcpy(&rel,image+md_timed_site[i]+1,4);
        assert(image[md_timed_site[i]]==0xe8 && md_timed_site[i]+5+rel==MD_TIMED_CHECK);
    }
    int32_t rel;memcpy(&rel,image+BOOT_HOLD_SITE+1,4);assert(image[BOOT_HOLD_SITE]==0xe8 && BOOT_HOLD_SITE+5+rel==BOOT_HOLD_MOVE);
    memcpy(&rel,image+BOOT_HOLD_SITE-4,4);assert(!memcmp(image+BOOT_HOLD_SITE-7,"\x48\x8b\x0d",3) && BOOT_HOLD_SITE+rel==BOOT_HOLD_DIRECTOR);
    const unsigned tampers[]={0x1121f80,0x112208b,0x111d090,0x111d296,0x111e370,0x111e505,MD_TIMED_CHECK,0x111c38d};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {image[tampers[i]]^=1;assert(!md_timed_pinned());image[tampers[i]]^=1;}
    const unsigned holds[]={0x9fe074,BOOT_HOLD_SITE,BOOT_HOLD_SITE+4,BOOT_HOLD_MOVE,0x106d2a9};
    for(unsigned i=0;i<sizeof(holds)/sizeof(*holds);i++) {image[holds[i]]^=1;assert(!boot_hold_pinned());image[holds[i]]^=1;}
    assert(md_timed_pinned() && boot_hold_pinned());
    memset(md_timed_original,0,sizeof(md_timed_original));memset(md_timed_patched,0,sizeof(md_timed_patched));
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;thunk=prior_thunk;
    puts("MD timed check sites and boot hold native pins, call targets and tamper refusals PASS");
}
