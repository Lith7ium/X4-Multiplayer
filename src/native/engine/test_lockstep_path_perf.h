/* Equivalence of the cached path determinism and travel snapshot walks with
 * the original implementation; see test_path_perf_equivalence. */
/* Reference: the pre-optimization path determinism collect/refresh and
 * travel snapshot walk (92151d9), with private state, used only to prove the
 * cached implementation produces identical calls, order, stamps and receipts. */
static path_det_item ref_pd_items[PATH_DET_CAPACITY];
static unsigned ref_last_count,ref_last_bank;
static uint64_t ref_frames,ref_stamped,ref_current,ref_stale,ref_skipped,ref_order_hash,ref_stamp_hash,ref_last_time,ref_unmapped;
static const char *ref_stage="idle";
/* Pinned layout (RunPass 0xeae3c0, UpdateBatch, AssignAgentToBatch 0xeaf2b0):
 * vectors +0x180 and +0x198 of 0x20-byte batches, member count at +8 and a
 * circular sentinel list at +0x10 (node+8 next, node+0x10 agent). Only exact
 * NavigationController owners with the world_nav link shape are admitted. */
static int ref_collect(unsigned char *manager,const unsigned char *nav_vtable,unsigned *count,uint64_t *skipped) {
    unsigned n=0;uint64_t skip=0;int ok=1;
    __try {
        for(unsigned v=0;v<2 && ok;v++) {
            unsigned char *begin=*(unsigned char**)(manager+0x180+v*0x18),*end=*(unsigned char**)(manager+0x188+v*0x18);
            if(end<begin || (uintptr_t)(end-begin)%0x20) {ok=0;break;}
            for(unsigned char *batch=begin;batch<end && ok;batch+=0x20) {
                uint64_t members=*(uint64_t*)(batch+8);unsigned char *sentinel=*(unsigned char**)(batch+0x10),*node;
                if(!sentinel || members>PATH_DET_CAPACITY) {ok=0;break;}
                node=*(unsigned char**)(sentinel+8);
                for(uint64_t i=0;i<members;i++,node=*(unsigned char**)(node+8)) {
                    if(node==sentinel) {ok=0;break;}
                    unsigned char *agent=*(unsigned char**)(node+0x10),*object,*controller;uint64_t id;
                    if(!agent || !(object=*(unsigned char**)(agent+0xef0)) || !(controller=*(unsigned char**)(object+0x200)) ||
                       *(const unsigned char**)controller!=nav_vtable || *(unsigned char**)(controller+0x28)!=object ||
                       *(unsigned char**)(controller+0x40)!=agent || *(unsigned char**)(controller+0x30)!=agent+0xad0 ||
                       !(id=*(uint64_t*)(object+8))) {++skip;continue;}
                    if(n>=PATH_DET_CAPACITY) {ok=0;break;}
                    /* Runtime IDs are process-local; order by the portable saved ID.
                     * Objects absent from the imported save keep a flagged runtime key. */
                    uint64_t saved=world_identity_lookup(id);if(!saved) ++ref_unmapped;
                    ref_pd_items[n].id=saved?saved:(1ULL<<63)|id;ref_pd_items[n].controller=controller;++n;
                }
                if(ok && node!=sentinel) ok=0; /* The member count must close the ring. */
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    *count=n;*skipped=skip;return ok;
}
static int ref_refresh_frame(path_det_refresh_fn refresh,path_det_sync_fn sync,unsigned char *manager,
                                  unsigned char *tls,const unsigned char *nav_vtable,const volatile uint32_t *read_bank) {
    unsigned count=0;uint64_t skipped=0;
    ref_stage="collect";if(!manager || !tls || !ref_collect(manager,nav_vtable,&count,&skipped)) return 0;
    qsort(ref_pd_items,count,sizeof(ref_pd_items[0]),path_det_item_order);
    ref_stage="duplicate";
    for(unsigned i=1;i<count;i++) if(ref_pd_items[i].id==ref_pd_items[i-1].id) return 0;
    uint64_t saved_time,saved_previous;memcpy(&saved_time,tls+0x300,8);memcpy(&saved_previous,tls+0x328,8);
    int ok=0;
    __try {
        double before,now;memcpy(&before,&saved_time,8);
        sync();memcpy(&now,tls+0x300,8);
        uint32_t bank=*read_bank;ref_stage="time";
        if(isfinite(now) && now>before && bank<=1) {
            uint64_t order=0xcbf29ce484222325ULL,stamp=order,stamped=0,current=0,stale=0;ref_stage="refresh";
            for(unsigned i=0;i<count;i++) {
                unsigned char *c=ref_pd_items[i].controller;
                uint32_t cursor=*(volatile uint32_t*)(c+0xc8);
                refresh(c);
                uint64_t when;memcpy(&when,c+0xc0,8);uint32_t after=*(volatile uint32_t*)(c+0xc8);double t;memcpy(&t,&when,8);
                if(cursor==bank) ++current;else if(after==bank && t==now) ++stamped;else ++stale;
                if(!(ref_pd_items[i].id>>63)) { /* Only portable keys are comparable across clients. */
                    order=path_det_fnv(order,&ref_pd_items[i].id,8);
                    /* Hash the stamp relative to the live bank: the physical phase may differ
                     * between clients and the engine treats the two banks symmetrically. */
                    uint32_t relation=after==bank?0:after==2?2:1;
                    stamp=path_det_fnv(path_det_fnv(path_det_fnv(stamp,&ref_pd_items[i].id,8),&when,8),&relation,4);
                }
            }
            ref_stamped+=stamped;ref_current+=current;ref_stale+=stale;ref_skipped+=skipped;
            ref_order_hash=order;ref_stamp_hash=stamp;memcpy(&ref_last_time,&now,8);
            ref_last_count=count;ref_last_bank=bank;++ref_frames;ref_stage="done";ok=1;
        }
    } __finally {
        memcpy(tls+0x300,&saved_time,8);memcpy(tls+0x328,&saved_previous,8);
    }
    return ok;
}
/* The receipt is portable: keyed by saved ID and summed (order-independent).
 * Agents without a saved ID (created after load; runtime IDs are process
 * local) are copied like all others but only counted, never hashed. */
static int ref_tsnap_copy_agent(unsigned char *agent,uint64_t *agents,uint64_t *flagged,uint64_t *hash,uint64_t *unmapped,uint64_t *unmapped_flagged) {
    unsigned char *object=*(unsigned char**)(agent+0xef0),flag=agent[TSNAP_FLAG];
    agent[TSNAP_PAD]=flag;
    uint64_t runtime=object?*(uint64_t*)(object+8):0,saved=runtime?world_identity_lookup(runtime):0;
    if(saved) *hash+=lockstep_mix(saved*2+(flag!=0));
    else {++*unmapped;*unmapped_flagged+=flag!=0;}
    ++*agents;*flagged+=flag!=0;return 1;
}
/* Every agent a pass can update or read as a leader: both batch vectors
 * (+0x180, +0x198) and the not-yet-assigned list whose sentinel is at +0x178. */
static int ref_tsnap_snapshot(unsigned char *manager,uint64_t *agents,uint64_t *flagged,uint64_t *fresh,uint64_t *hash,uint64_t *unmapped,uint64_t *unmapped_flagged) {
    int ok=1;*agents=*flagged=*fresh=*hash=*unmapped=*unmapped_flagged=0;
    __try {
        for(unsigned v=0;v<2 && ok;v++) {
            unsigned char *begin=*(unsigned char**)(manager+0x180+v*0x18),*end=*(unsigned char**)(manager+0x188+v*0x18);
            if(end<begin || (uintptr_t)(end-begin)%0x20) {ok=0;break;}
            for(unsigned char *batch=begin;batch<end && ok;batch+=0x20) {
                uint64_t members=*(uint64_t*)(batch+8);unsigned char *sentinel=*(unsigned char**)(batch+0x10),*node;
                if(!sentinel || members>PATH_DET_CAPACITY) {ok=0;break;}
                node=*(unsigned char**)(sentinel+8);
                for(uint64_t i=0;i<members;i++,node=*(unsigned char**)(node+8)) {
                    unsigned char *agent;
                    if(node==sentinel || !(agent=*(unsigned char**)(node+0x10))) {ok=0;break;}
                    ref_tsnap_copy_agent(agent,agents,flagged,hash,unmapped,unmapped_flagged);
                }
                if(ok && node!=sentinel) ok=0;
            }
        }
        unsigned char *sentinel=*(unsigned char**)(manager+0x178),*node;
        if(ok && !sentinel) ok=0;
        node=ok?*(unsigned char**)(sentinel+8):NULL;
        for(uint64_t i=0;ok && node!=sentinel;i++,node=*(unsigned char**)(node+8)) {
            unsigned char *agent;
            if(i>=PATH_DET_CAPACITY || !node || !(agent=*(unsigned char**)(node+0x10))) {ok=0;break;}
            ref_tsnap_copy_agent(agent,agents,flagged,hash,unmapped,unmapped_flagged);++*fresh;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
    return ok;
}
/* Two identical synthetic worlds: world 0 runs the reference, world 1 the
 * cached implementation. Every frame applies the same mutation to both and
 * compares refresh calls, counters, hashes, stamps, snapshot receipts and
 * padding copies. */
#define TPP_SHIPS 12
typedef struct {test_pd_ship ships[TPP_SHIPS];unsigned char manager[0x220],batches[3][0x20],sentinels[4][0x18],tls[0x400];
    int list[TPP_SHIPS];unsigned calls[64],ncalls;} tpp_world;
static tpp_world tpp_worlds[2];
static tpp_world *tpp_current;
static double tpp_now;static uint32_t tpp_bank;
static void tpp_sync(void) {memcpy(tpp_current->tls+0x328,tpp_current->tls+0x300,8);memcpy(tpp_current->tls+0x300,&tpp_now,8);}
static void tpp_refresh(void *controller) {
    unsigned char *c=controller;double now,last;memcpy(&now,tpp_current->tls+0x300,8);memcpy(&last,c+0xc0,8);
    assert(tpp_current->ncalls<64);tpp_current->calls[tpp_current->ncalls++]=*(uint32_t*)(c+0xd0);
    if(now>last && *(uint32_t*)(c+0xc8)!=tpp_bank) {memcpy(c+0xc0,&now,8);*(uint32_t*)(c+0xc8)=tpp_bank;}
}
static void tpp_link(tpp_world *w) {
    /* list[i]: 0..2 batch, 3 pending, -1 detached; insertion order is ship index order. */
    unsigned char *prev[4];uint64_t count[4]={0};
    for(unsigned l=0;l<4;l++) prev[l]=w->sentinels[l];
    for(unsigned i=0;i<TPP_SHIPS;i++) {int l=w->list[i];if(l<0) continue;*(unsigned char**)(prev[l]+8)=w->ships[i].node;prev[l]=w->ships[i].node;++count[l];}
    for(unsigned l=0;l<4;l++) *(unsigned char**)(prev[l]+8)=w->sentinels[l];
    for(unsigned b=0;b<3;b++) {memset(w->batches[b],0,0x20);*(uint64_t*)(w->batches[b]+8)=count[b];*(unsigned char**)(w->batches[b]+0x10)=w->sentinels[b];}
    *(unsigned char**)(w->manager+0x180)=w->batches[0];*(unsigned char**)(w->manager+0x188)=w->batches[0]+0x40;
    *(unsigned char**)(w->manager+0x198)=w->batches[2];*(unsigned char**)(w->manager+0x1a0)=w->batches[2]+0x20;
    *(unsigned char**)(w->manager+0x178)=w->sentinels[3];
}
static void tpp_build(tpp_world *w) {
    memset(w,0,sizeof(*w));
    for(unsigned i=0;i<TPP_SHIPS;i++) {
        test_pd_link(&w->ships[i],0xc000+i*3,NULL,NULL);*(uint32_t*)(w->ships[i].controller+0xd0)=i;
        w->ships[i].agent[TSNAP_FLAG]=(unsigned char)(i%3==1);w->list[i]=i<10?(int)(i%3):3;
    }
    tpp_link(w);
}
static void tpp_mutate(tpp_world *w,unsigned step) {
    switch(step) {
    case 3:w->list[4]=0;break;                                                   /* batch move */
    case 4:*(unsigned char**)w->ships[5].controller=image+0x2c39500;break;      /* admission lost */
    case 5:*(unsigned char**)w->ships[5].controller=image+PATH_DET_NAV_VTABLE;break;
    case 6:*(uint64_t*)(w->ships[2].object+8)=0xc0ffee;break;                   /* runtime ID change */
    case 7:w->list[7]=-1;w->list[10]=2;w->ships[1].agent[TSNAP_FLAG]=0;break;   /* removal, assignment */
    case 8:*(uint64_t*)(w->ships[6].object+8)=*(uint64_t*)(w->ships[1].object+8);break; /* duplicate saved key */
    case 9:*(uint64_t*)(w->ships[6].object+8)=0xc000+6*3;break;
    case 11:*(unsigned char**)(w->ships[3].agent+0xef0)=NULL;break;              /* no Object */
    default:break;
    }
    tpp_link(w);
}
static void test_path_perf_equivalence(void) {
    int owned_identities=!world_identities;
    if(owned_identities) world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);
    for(unsigned i=0;i<TPP_SHIPS;i++) if(i!=9 && i!=10) assert(world_identity_record(0xc000+i*3,0x50000+(i*7)%TPP_SHIPS));
    tpp_build(&tpp_worlds[0]);tpp_build(&tpp_worlds[1]);
    path_det_seq_valid=path_det_items_valid=0;path_det_reuses=path_det_rebuilds=path_det_lookups=path_det_id_hits=0;tsnap_seq_count=0;
    path_det_frames=path_det_stamped=path_det_current=path_det_stale=path_det_skipped=path_det_unmapped=0;
    ref_frames=ref_stamped=ref_current=ref_stale=ref_skipped=ref_unmapped=0;
    double before=1.5;
    for(unsigned step=1;step<=12;step++) {
        if(step==5) assert(world_identity_record(0xc000+9*3,0x5ffff)); /* table growth: an unmapped runtime becomes mapped */
        tpp_mutate(&tpp_worlds[0],step);tpp_mutate(&tpp_worlds[1],step);
        tpp_now=2.0+step/60.0;tpp_bank=step&1;
        int result[2];
        for(unsigned w=0;w<2;w++) {
            tpp_current=&tpp_worlds[w];tpp_current->ncalls=0;memcpy(tpp_current->tls+0x300,&before,8);
            result[w]=w?path_det_refresh_frame(tpp_refresh,tpp_sync,tpp_current->manager,tpp_current->tls,image+PATH_DET_NAV_VTABLE,&tpp_bank):
                        ref_refresh_frame(tpp_refresh,tpp_sync,tpp_current->manager,tpp_current->tls,image+PATH_DET_NAV_VTABLE,&tpp_bank);
        }
        assert(result[0]==result[1] && !strcmp(ref_stage,path_det_stage));
        assert(tpp_worlds[0].ncalls==tpp_worlds[1].ncalls && !memcmp(tpp_worlds[0].calls,tpp_worlds[1].calls,tpp_worlds[0].ncalls*sizeof(unsigned)));
        assert(ref_frames==path_det_frames && ref_stamped==path_det_stamped && ref_current==path_det_current && ref_stale==path_det_stale &&
               ref_skipped==path_det_skipped && ref_unmapped==path_det_unmapped);
        if(result[0]) assert(ref_order_hash==path_det_order_hash && ref_stamp_hash==path_det_stamp_hash && ref_last_count==path_det_last_count);
        for(unsigned i=0;i<TPP_SHIPS;i++) assert(!memcmp(tpp_worlds[0].ships[i].controller+0xc0,tpp_worlds[1].ships[i].controller+0xc0,12));
        /* Travel snapshot walk: same receipt, counts and padding copies. */
        uint64_t a[6],b[6];
        int ra=ref_tsnap_snapshot(tpp_worlds[0].manager,&a[0],&a[1],&a[2],&a[3],&a[4],&a[5]);
        int rb=tsnap_snapshot(tpp_worlds[1].manager,&b[0],&b[1],&b[2],&b[3],&b[4],&b[5]);
        assert(ra==rb && !memcmp(a,b,sizeof(a)));
        for(unsigned i=0;i<TPP_SHIPS;i++) assert(tpp_worlds[0].ships[i].agent[TSNAP_PAD]==tpp_worlds[1].ships[i].agent[TSNAP_PAD]);
        if(result[0]) before=tpp_now;
    }
    /* Unchanged frames reused the sorted list; membership/link/identity changes rebuilt it. */
    assert(path_det_reuses>=2 && path_det_rebuilds>=6 && path_det_id_hits>0 && tsnap_id_hits>0);
    if(owned_identities) {VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;}
    path_det_seq_valid=path_det_items_valid=0;tsnap_seq_count=0;
    printf("path determinism/travel snapshot caches equal the reference over 12 membership/link/identity steps PASS (reuses=%llu rebuilds=%llu)\n",
        (unsigned long long)path_det_reuses,(unsigned long long)path_det_rebuilds);
}
