/* Serial pathing and path determinism of the Linux gate (gate_path.h), included by
 * test_gate_linux.c after test_gate_world.h. Owned memory and our own stand-ins; no X4 code
 * runs. The release itself runs in the engine scenario protocol2_serial_pathing. */
enum {WP_SHIPS=6};
static __attribute__((aligned(16))) unsigned char wp_manager[0x400],wp_batch[2][0x20],wp_sentinel[2][0x20],wp_node[WP_SHIPS][0x20],
    wp_agent[WP_SHIPS][0x1000],wp_object[WP_SHIPS][0x400],wp_controller[WP_SHIPS][0x100],wp_tls_block[0x3000];
static unsigned wp_calls;static unsigned char *wp_order[WP_SHIPS+2];
static double wp_now;static uint32_t wp_bank;
#define WP_TLS (wp_tls_block+0x2800)
static void wp_sync(void) {
    memcpy(WP_TLS+GATE_TLS_LINUX_PREVIOUS_TIME,WP_TLS+GATE_TLS_LINUX_TIME,8);memcpy(WP_TLS+GATE_TLS_LINUX_TIME,&wp_now,8);
}
static void wp_refresh(void *controller) {
    unsigned char *c=controller;
    assert(wp_calls<WP_SHIPS+2);wp_order[wp_calls++]=c;
    uint32_t cursor;memcpy(&cursor,c+0xc8,4);
    if(cursor!=wp_bank) {memcpy(c+0xc0,WP_TLS+GATE_TLS_LINUX_TIME,8);memcpy(c+0xc8,&wp_bank,4);}
}
/* Batches in the given order, the first three members in the first vector. */
static void wp_build(const unsigned order[WP_SHIPS],const void *nav_vtable) {
    memset(wp_manager,0,sizeof(wp_manager));memset(wp_batch,0,sizeof(wp_batch));
    for(unsigned v=0;v<2;v++) {
        unsigned char *begin=wp_batch[v],*end=begin+0x20;
        memcpy(wp_manager+PATH_DET_BATCH_BEGIN+v*0x18,&begin,8);memcpy(wp_manager+PATH_DET_BATCH_END+v*0x18,&end,8);
        uint64_t members=3;memcpy(wp_batch[v]+8,&members,8);
        unsigned char *sentinel=wp_sentinel[v];memcpy(wp_batch[v]+0x10,&sentinel,8);
        unsigned char *previous=sentinel;
        for(unsigned i=0;i<3;i++) {
            unsigned char *node=wp_node[order[v*3+i]];
            memcpy(previous+8,&node,8);previous=node;
            unsigned char *agent=wp_agent[order[v*3+i]];memcpy(node+0x10,&agent,8);
        }
        memcpy(previous+8,&sentinel,8);
    }
    for(unsigned i=0;i<WP_SHIPS;i++) {
        unsigned char *a=wp_agent[i],*o=wp_object[i],*c=wp_controller[i],*slots=a+LXW_AGENT_SLOTS;
        memset(c,0,sizeof(wp_controller[i]));
        memcpy(a+LXW_AGENT_OWNER,&o,8);memcpy(o+LXW_OBJ_CONTROLLER,&c,8);
        uint64_t runtime=9000+i;memcpy(o+LXW_OBJ_RUNTIME,&runtime,8);
        memcpy(c,&nav_vtable,8);memcpy(c+LXW_NAV_OWNER,&o,8);memcpy(c+LXW_NAV_AGENT,&a,8);memcpy(c+LXW_NAV_SLOTS,&slots,8);
        uint32_t never=2;memcpy(c+0xc8,&never,4);
    }
}
static int wp_frame(const void *nav_vtable) {
    wp_calls=0;
    return path_det_refresh_frame(wp_refresh,wp_sync,wp_manager,WP_TLS,nav_vtable,&wp_bank);
}
static void test_path_determinism(void) {
    reset_session();wt_reset_identities();
    if(!world_identities) world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));
    assert(world_identities);
    /* Saved IDs run against the runtime IDs and the list order; ship 5 has no identity. */
    const uint64_t saved[WP_SHIPS-1]={50,40,30,20,10};
    for(unsigned i=0;i<WP_SHIPS-1;i++) assert(world_identity_record(9000+i,saved[i]));
    world_identity_sealed=1;
    static const unsigned char nav_vtable[8]={0};
    const unsigned listed[WP_SHIPS]={2,0,5,4,1,3},reversed[WP_SHIPS]={3,1,4,5,0,2};
    /* Previous thread time, restored afterwards. */
    double before=100.0,previous=99.0;
    memcpy(WP_TLS+GATE_TLS_LINUX_TIME,&before,8);memcpy(WP_TLS+GATE_TLS_LINUX_PREVIOUS_TIME,&previous,8);
    wp_build(listed,nav_vtable);wp_now=100.5;wp_bank=1;
    path_det_seq_valid=path_det_items_valid=0;path_det_stamped=path_det_current=path_det_stale=0;path_det_unmapped=0;
    assert(wp_frame(nav_vtable));
    /* Saved-ID order, the unmapped ship last, each exactly once. */
    assert(wp_calls==WP_SHIPS);
    const unsigned expected[WP_SHIPS]={4,3,2,1,0,5};
    for(unsigned i=0;i<WP_SHIPS;i++) assert(wp_order[i]==wp_controller[expected[i]]);
    assert(path_det_stamped==WP_SHIPS && !path_det_stale && !path_det_current && path_det_unmapped==1 && path_det_last_count==WP_SHIPS);
    double t;memcpy(&t,WP_TLS+GATE_TLS_LINUX_TIME,8);assert(t==before); /* the thread's own time is back */
    memcpy(&t,WP_TLS+GATE_TLS_LINUX_PREVIOUS_TIME,8);assert(t==previous);
    memcpy(&t,wp_controller[3]+0xc0,8);assert(t==wp_now);
    const uint64_t order=path_det_order_hash,stamps=path_det_stamp_hash;
    /* Another list order, the same frame: the same order and stamps. The result does not depend
     * on heap addresses or batch membership, which differ between clients. */
    wp_build(reversed,nav_vtable);wp_bank=1;
    assert(wp_frame(nav_vtable) && path_det_order_hash==order && path_det_stamp_hash==stamps);
    for(unsigned i=0;i<WP_SHIPS;i++) assert(wp_order[i]==wp_controller[expected[i]]);
    /* An unchanged walk reuses the sorted list; the next frame's bank stamps everyone again. */
    uint64_t reuses=path_det_reuses,stale=path_det_stale;
    wp_now=100.75;wp_bank=0;
    assert(wp_frame(nav_vtable) && path_det_reuses==reuses+1 && path_det_stale==stale && path_det_order_hash==order && path_det_stamp_hash!=stamps);
    /* A later query finds the stamp current. */
    wp_now=101.0;assert(wp_frame(nav_vtable) && path_det_current>=WP_SHIPS);
    /* A controller that is not a nav controller, or whose links do not hold, is skipped. */
    path_det_skipped=0;
    static const unsigned char other_vtable[8]={0};
    memcpy(wp_controller[0],&(const void*){other_vtable},8);
    wp_now=102.0;wp_bank=1;assert(wp_frame(nav_vtable) && wp_calls==WP_SHIPS-1 && path_det_skipped==1);
    memcpy(wp_controller[0],&(const void*){nav_vtable},8);
    memcpy(wp_controller[1]+LXW_NAV_OWNER,&(const void*){wp_object[0]},8);
    path_det_skipped=0;wp_now=103.0;wp_bank=0;assert(wp_frame(nav_vtable) && wp_calls==WP_SHIPS-1 && path_det_skipped==1);
    memcpy(wp_controller[1]+LXW_NAV_OWNER,&(const void*){wp_object[1]},8);
    /* A ring the member count does not close fails the frame; the time is restored. */
    uint64_t wrong=2;memcpy(wp_batch[0]+8,&wrong,8);
    wp_now=104.0;assert(!wp_frame(nav_vtable) && !strcmp(path_det_stage,"collect"));
    wrong=3;memcpy(wp_batch[0]+8,&wrong,8);
    /* Time that does not advance fails too. */
    wp_build(listed,nav_vtable);wp_now=before;assert(!wp_frame(nav_vtable) && !strcmp(path_det_stage,"time"));
    memcpy(&t,WP_TLS+GATE_TLS_LINUX_TIME,8);assert(t==before);
    /* Two ships with one saved ID: refused rather than ordered arbitrarily. */
    wt_reset_identities();
    for(unsigned i=0;i<WP_SHIPS;i++) assert(world_identity_record(9000+i,i==1?10:10+i));
    world_identity_sealed=1;
    wp_build(listed,nav_vtable);path_det_items_valid=0;wp_now=105.0;assert(!wp_frame(nav_vtable) && !strcmp(path_det_stage,"duplicate"));
    /* The budget: lifted once from the native value, kept, given back; anything else fails closed. */
    const uint64_t native=PATH_DET_BUDGET_NATIVE;
    memcpy(wp_manager+LX_PATH_BUDGET,&native,8);
    path_det_budget_manager=NULL;path_det_budget_applied=path_det_budget_restored=0;path_det_budget_frames=0;
    assert(path_det_budget_frame(wp_manager) && path_det_budget_applied && rd64(wp_manager+LX_PATH_BUDGET)==PATH_DET_BUDGET_LIFTED);
    assert(path_det_budget_frame(wp_manager) && path_det_budget_frames==2);
    assert(!path_det_budget_frame(wp_batch[0]) && !strcmp(path_det_stage,"budget_owner"));
    /* Restored only while the manager is still the published one. */
    void *published=NULL;memcpy(LX(LX_PATH_MANAGER_VA),&published,8);
    path_det_restore();assert(path_det_budget_applied && rd64(wp_manager+LX_PATH_BUDGET)==PATH_DET_BUDGET_LIFTED);
    published=wp_manager;memcpy(LX(LX_PATH_MANAGER_VA),&published,8);
    path_det_restore();assert(!path_det_budget_applied && path_det_budget_restored && rd64(wp_manager+LX_PATH_BUDGET)==native);
    published=NULL;memcpy(LX(LX_PATH_MANAGER_VA),&published,8);
    /* A foreign value is not adopted. */
    uint64_t foreign=native+1;memcpy(wp_manager+LX_PATH_BUDGET,&foreign,8);path_det_budget_manager=NULL;
    assert(!path_det_budget_frame(wp_manager) && !strcmp(path_det_stage,"budget_native") && rd64(wp_manager+LX_PATH_BUDGET)==foreign);
    path_det_budget_applied=path_det_budget_restored=0;path_det_budget_manager=NULL;
    wt_reset_identities();
    printf("path determinism PASS\n");
}
/* The count budget: the block's 16 bytes on the real or stand-in image, then the emitted code run. */
static void test_path_budget(void) {
    reset_session();
    unsigned char *site=LX(LX_PBUD_SITE);uintptr_t page=(uintptr_t)site&~(uintptr_t)(GATE_PAGE-1);
    assert(((uintptr_t)site&15)==0);
    if(memcmp(site,lx_pbud_native,16)) { /* the synthetic image: the pinned bytes */
        assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(site,lx_pbud_native,16);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    }
    pbud_installed=pbud_restored=0;pbud_n=0;pbud_stage="idle";
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();path_installed=1;path_options=1;
    /* Refused, nothing written. */
    assert(!pbud_arm(0) && !pbud_arm(LX_PBUD_MAX+1));
    path_options=0;assert(!pbud_arm(3));path_options=1;
    path_installed=0;assert(!pbud_arm(3));path_installed=1;
    stats.issued=1;assert(!pbud_arm(3));stats.issued=0;
    boot_phase=1;assert(!pbud_arm(3));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!pbud_arm(3));ui_thread=GetCurrentThreadId();
    assert(!memcmp(site,lx_pbud_native,16) && !strcmp(pbud_stage,"state"));
    /* Armed: the emitted bytes; a reload passes only with the same N. */
    assert(pbud_arm(3) && pbud_installed && pbud_owned());
    unsigned char code[16];pbud_emit(code,3);assert(!memcmp(site,code,16));
    assert(pbud_arm(3) && !pbud_arm(4));
    /* A foreign value: the tick check fails, the restore keeps it. */
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));site[7]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!pbud_owned());pbud_restore();assert(pbud_installed && !strcmp(pbud_stage,"restore_foreign"));
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));site[7]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    /* Never under an open pass; afterwards exactly the native bytes. */
    InterlockedExchange(&path_pass_open,1);pbud_restore();assert(pbud_installed && !strcmp(pbud_stage,"pass_open"));InterlockedExchange(&path_pass_open,0);
    char text[160];assert(pbud_status(text,sizeof(text))>0 && strstr(text,"installed=1 n=3 "));
    pbud_restore();assert(!pbud_installed && pbud_restored && !memcmp(site,lx_pbud_native,16) && pbud_owned());
    /* Another block is refused. */
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));site[1]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!pbud_arm(3) && !strcmp(pbud_stage,"pins"));
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));site[1]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    path_installed=0;path_options=0;reset_session();
    /* The code itself: a wrapper puts the index in r13 and dl=1, the exits return 1 and 2, and the
     * byte the cheap exit reads comes back above them. */
    unsigned char *exec=mmap(NULL,0x1000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(exec!=MAP_FAILED);
    static const unsigned char wrap[]={0x41,0x55,0x48,0x83,0xec,0x40,0x49,0x89,0xfd,0xba,1,0,0,0,0xc6,0x44,0x24,0x17,0,
        0xe8,0xe8,0,0,0, /* call +0x100 */
        0x0f,0xb6,0x54,0x24,0x17,0xc1,0xe2,0x08,0x09,0xd0,0x48,0x83,0xc4,0x40,0x41,0x5d,0xc3};
    static const unsigned char full[]={0xb8,1,0,0,0,0xc3},cheap[]={0xb8,2,0,0,0,0xc3};
    memcpy(exec,wrap,sizeof(wrap));
    memcpy(exec+0x100+((intptr_t)LX_PBUD_NEXT-(intptr_t)LX_PBUD_SITE),full,sizeof(full));memcpy(exec+0x100+((intptr_t)LX_PBUD_CHEAP-(intptr_t)LX_PBUD_SITE),cheap,sizeof(cheap));
    pbud_emit(code,3);memcpy(exec+0x100,code,16);
    uint64_t (*run)(uint64_t)=(uint64_t(*)(uint64_t))(void*)exec;
    assert(run(0)==0x101 && run(1)==0x101 && run(2)==0x102 && run(3)==0x102 && run(UINT64_MAX)==0x102);
    pbud_emit(code,LX_PBUD_MAX);memcpy(exec+0x100,code,16);
    assert(run(LX_PBUD_MAX-2)==0x101 && run(LX_PBUD_MAX-1)==0x102);
    assert(!munmap(exec,0x1000));
    pbud_stage="idle";pbud_restored=0;pbud_n=0;
    printf("path budget PASS\n");
}
/* The real or stand-in word at the release site: the patch and its exact restoration. */
static void test_path_install(void) {
    reset_session();
    unsigned char *site=LX(LX_PATH_RELEASE_SITE),*word=(unsigned char*)((uintptr_t)site&~(uintptr_t)7);
    uintptr_t page=(uintptr_t)word&~(uintptr_t)(GATE_PAGE-1);
    if(rd64(word)!=LX_PATH_RELEASE_ORIGINAL) { /* the synthetic image: the pinned word */
        const uint64_t original=LX_PATH_RELEASE_ORIGINAL;
        assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(word,&original,8);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    }
    thunk=make_thunk();assert(thunk);
    path_installed=0;path_restored=0;
    unsigned char *saved_thunk=thunk;thunk=NULL;assert(!path_install(3) && rd64(word)==LX_PATH_RELEASE_ORIGINAL);thunk=saved_thunk;
    assert(path_install(3) && path_installed && path_options==3 && path_owned());
    int32_t rel;memcpy(&rel,site+1,4);
    assert(site[0]==0xe8 && site+5+rel==thunk+LXP_THUNK_HOOK && !memcmp(word,"\x83\xc7\x70",3));
    assert(!path_install(3)); /* once */
    /* The hook's jump really leads to the release hook. */
    assert(thunk[LXP_THUNK_HOOK]==0xff && thunk[LXP_THUNK_HOOK+1]==0x25);
    void *target;memcpy(&target,thunk+LXP_THUNK_HOOK+6,8);assert(target==(void*)lxp_path_release);
    /* A foreign owner is kept. */
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));word[0]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!path_owned());path_restore();assert(!path_restored && path_installed);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));word[0]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    path_restore();assert(path_restored && !path_installed && rd64(word)==LX_PATH_RELEASE_ORIGINAL);
    /* Refused, nothing patched, on another word. */
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));word[0]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(!path_install(3) && !path_installed);
    assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));word[0]^=1;assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_EXEC));
    printf("serial pathing patch PASS\n");
}
