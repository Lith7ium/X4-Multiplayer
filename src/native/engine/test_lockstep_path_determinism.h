/* Synthetic pathing batches: order, link admission, TLS bracketing, stamp
 * accounting and the budget word's owner checks. No native code is executed. */
#define TEST_PD_AGENTS 6
static unsigned char *test_pd_tls;
static double test_pd_now;
static uint32_t test_pd_bank;
static uint64_t test_pd_order[16];
static unsigned test_pd_calls;
static void test_pd_sync(void) {
    memcpy(test_pd_tls+0x328,test_pd_tls+0x300,8);memcpy(test_pd_tls+0x300,&test_pd_now,8);
}
/* Mirrors only the observable stamp rule of 0xea2a20. */
static void test_pd_refresh(void *controller) {
    unsigned char *c=controller,*object=*(unsigned char**)(c+0x28);double now,last;
    memcpy(&now,test_pd_tls+0x300,8);memcpy(&last,c+0xc0,8);
    assert(test_pd_calls<16);test_pd_order[test_pd_calls++]=*(uint64_t*)(object+8);
    if(now>last && *(uint32_t*)(c+0xc8)!=test_pd_bank) {memcpy(c+0xc0,&now,8);*(uint32_t*)(c+0xc8)=test_pd_bank;}
}
typedef struct {unsigned char node[0x18],agent[0xf90],object[0x210],controller[0xd8];} test_pd_ship; /* native Agent is 0xf90 bytes */
static void test_pd_link(test_pd_ship *s,uint64_t id,unsigned char *sentinel_prev,unsigned char *next) {
    memset(s,0,sizeof(*s));
    *(unsigned char**)(s->node)=sentinel_prev;*(unsigned char**)(s->node+8)=next;*(unsigned char**)(s->node+0x10)=s->agent;
    *(unsigned char**)(s->agent+0xef0)=s->object;*(uint64_t*)(s->object+8)=id;*(unsigned char**)(s->object+0x200)=s->controller;
    *(unsigned char**)(s->controller)=image+PATH_DET_NAV_VTABLE;*(unsigned char**)(s->controller+0x28)=s->object;
    *(unsigned char**)(s->controller+0x30)=s->agent+0xad0;*(unsigned char**)(s->controller+0x40)=s->agent;
    double loaded=1.0;memcpy(s->controller+0xc0,&loaded,8);*(uint32_t*)(s->controller+0xc8)=0;
}
static void test_path_determinism(void) {
    static test_pd_ship ships[TEST_PD_AGENTS];
    static unsigned char manager[0x220],batches[3][0x20],sentinels[3][0x18],tls[0x400];
    /* Batch 0 (vector +0x180): ids 50,10; batch 1 (+0x180): 40 and a
     * PlayerController-like owner; batch 2 (+0x198): 30,20. */
    const uint64_t ids[TEST_PD_AGENTS]={50,10,40,99,30,20};
    const unsigned members[3]={2,2,2};unsigned k=0;
    memset(manager,0,sizeof(manager));
    for(unsigned b=0;b<3;b++) {
        unsigned char *sentinel=sentinels[b];memset(batches[b],0,0x20);
        *(uint64_t*)(batches[b]+8)=members[b];*(unsigned char**)(batches[b]+0x10)=sentinel;
        *(unsigned char**)(sentinel+8)=ships[k].node;
        for(unsigned i=0;i<members[b];i++,k++)
            test_pd_link(&ships[k],ids[k],sentinel,i+1<members[b]?ships[k+1].node:sentinel);
    }
    *(unsigned char**)(ships[3].controller)=image+0x2c39500; /* not an exact NavigationController */
    *(unsigned char**)(manager+0x180)=batches[0];*(unsigned char**)(manager+0x188)=batches[0]+0x40;
    *(unsigned char**)(manager+0x198)=batches[2];*(unsigned char**)(manager+0x1a0)=batches[2]+0x20;
    *(uint32_t*)(ships[2].controller+0xc8)=1; /* already current for bank 1 */
    test_pd_tls=tls;memset(tls,0,sizeof(tls));double before=5.0,previous=4.5;memcpy(tls+0x300,&before,8);memcpy(tls+0x328,&previous,8);
    test_pd_now=6.0;test_pd_bank=1;test_pd_calls=0;
    path_det_frames=path_det_stamped=path_det_current=path_det_stale=path_det_skipped=path_det_unmapped=0;
    /* Saved IDs reverse the runtime order; runtime 20 is absent from the save. */
    static world_identity_entry identities[WORLD_IDENTITY_CAPACITY];world_identity_entry *prior=world_identities;LONG64 prior_count=world_identity_count;
    memset(identities,0,sizeof(identities));world_identities=identities;
    const uint64_t mapped[4]={10,30,40,50};for(unsigned i=0;i<4;i++) assert(world_identity_record(mapped[i],1000-mapped[i]));
    assert(path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank));
    const uint64_t sorted[5]={50,40,30,10,20};
    assert(test_pd_calls==5 && !memcmp(test_pd_order,sorted,sizeof(sorted)) && path_det_unmapped==1);
    assert(!memcmp(tls+0x300,&before,8) && !memcmp(tls+0x328,&previous,8)); /* TLS bracket restored */
    assert(path_det_frames==1 && path_det_last_count==5 && path_det_stamped==4 && path_det_current==1 &&
           !path_det_stale && path_det_skipped==1 && !strcmp(path_det_stage,"done"));
    double t;memcpy(&t,ships[0].controller+0xc0,8);assert(t==6.0 && *(uint32_t*)(ships[0].controller+0xc8)==1);
    memcpy(&t,ships[2].controller+0xc0,8);assert(t==1.0); /* observer-current stamps are left alone */
    uint64_t order=path_det_order_hash,stamps=path_det_stamp_hash;
    /* Next frame: bank flips, every admitted controller is stamped in the same order. */
    double later=5.5;memcpy(tls+0x300,&later,8);test_pd_now=6.5;test_pd_bank=0;test_pd_calls=0;
    assert(path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank));
    assert(path_det_order_hash==order && path_det_stamp_hash!=stamps && path_det_stamped==9 && path_det_current==1);
    /* The stamp hash is relative to the live bank: a client whose physical
     * bank phase is flipped, with consistently imported cursors, hashes equal. */
    uint64_t phase_hash[2];
    for(unsigned run=0;run<2;run++) {
        for(unsigned i=0;i<TEST_PD_AGENTS;i++) {double loaded=1.0;memcpy(ships[i].controller+0xc0,&loaded,8);*(uint32_t*)(ships[i].controller+0xc8)=run;}
        *(uint32_t*)(ships[2].controller+0xc8)=!run; /* current in both phases */
        double start=5.5;memcpy(tls+0x300,&start,8);test_pd_now=6.5;test_pd_bank=!run;test_pd_calls=0;
        assert(path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank));
        assert(path_det_last_bank==!run);phase_hash[run]=path_det_stamp_hash;
    }
    assert(phase_hash[0]==phase_hash[1] && path_det_order_hash==order);
    /* Time that does not advance refuses before any refresh; TLS still restored. */
    test_pd_now=5.5;test_pd_calls=0;
    assert(!path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank));
    assert(!test_pd_calls && !strcmp(path_det_stage,"time") && !memcmp(tls+0x300,&later,8));
    /* A member count that does not close the ring, or a duplicate ID, refuses. */
    *(uint64_t*)(batches[2]+8)=3;test_pd_now=7.0;
    assert(!path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank) && !test_pd_calls);
    *(uint64_t*)(batches[2]+8)=2;*(uint64_t*)(ships[5].object+8)=10; /* same saved ID as ship 1 */
    assert(!path_det_refresh_frame(test_pd_refresh,test_pd_sync,manager,tls,image+PATH_DET_NAV_VTABLE,&test_pd_bank) &&
           !strcmp(path_det_stage,"duplicate") && !test_pd_calls);
    *(uint64_t*)(ships[5].object+8)=20;world_identities=prior;world_identity_count=prior_count;
    /* Budget word: native value only, one tracked manager, restore only while published. */
    static unsigned char other[0x220];uint64_t native=PATH_DET_BUDGET_NATIVE,lifted=PATH_DET_BUDGET_LIFTED;
    path_det_budget_manager=NULL;path_det_budget_applied=path_det_budget_restored=0;path_det_budget_frames=0;
    *(uint64_t*)(manager+0x218)=native+1;assert(!path_det_budget_frame(manager) && !path_det_budget_applied);
    *(uint64_t*)(manager+0x218)=native;assert(path_det_budget_frame(manager) && *(uint64_t*)(manager+0x218)==lifted);
    assert(path_det_budget_frame(manager) && path_det_budget_frames==2);
    memcpy(other+0x218,&native,8);assert(!path_det_budget_frame(other) && *(uint64_t*)(other+0x218)==native);
    *(unsigned char**)(image+PATH_DET_MANAGER)=other;path_det_restore();
    assert(path_det_budget_applied && !path_det_budget_restored && *(uint64_t*)(manager+0x218)==lifted);
    *(unsigned char**)(image+PATH_DET_MANAGER)=manager;path_det_restore();
    assert(!path_det_budget_applied && path_det_budget_restored && *(uint64_t*)(manager+0x218)==native);
    *(unsigned char**)(image+PATH_DET_MANAGER)=NULL;path_det_budget_manager=NULL;path_det_budget_restored=0;
    /* Arming needs an active boot gate with the serial pathing join. */
    assert(!path_det_arm(1) && !path_det_armed);
    char status[512];assert(path_det_status(status,sizeof(status)) && strstr(status,"armed=0 mask=0 "));
    printf("path determinism order/admission/TLS bracket/stamp/budget owner tests PASS\n");
}
