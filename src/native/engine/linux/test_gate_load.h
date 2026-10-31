/* The MD resume call and the AIDirector::Move entry on the synthetic or the executable's bytes:
 * the patches, a foreign owner, the exact restoration; the boot hold's decisions; the cue name
 * from libstdc++ strings. Included by test_gate_linux.c. */
static void tl_write(unsigned char *at,const void *bytes,size_t size) {
    uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    size_t span=((uintptr_t)at+size-page+GATE_PAGE-1)&~(size_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,span,PROT_READ|PROT_WRITE));memcpy(at,bytes,size);assert(!mprotect((void*)page,span,PROT_READ|PROT_EXEC));
}
static unsigned tl_native_moves;
static void tl_native_move(void *director) {(void)director;++tl_native_moves;}
static void test_boot_hold_decisions(void) {
    unsigned char director[0x300];memset(director,0,sizeof(director));
    void *slots[4]={NULL,NULL,NULL,NULL};uintptr_t begin=(uintptr_t)slots,end=begin+3*8;
    memcpy(director+0x218,&begin,8);memcpy(director+0x220,&end,8);
    void *global=director;memcpy(LX(LX_BOOT_HOLD_DIRECTOR_VA),&global,8);
    boot_hold_active=1;stats.active=1;boot_mode=1;boot_phase=2;stats.issued=0;ui_thread=GetCurrentThreadId();
    boot_hold_armed=1;boot_hold_held=boot_hold_moves=boot_hold_released=0;boot_hold_admitted=0;boot_hold_failures=0;tl_native_moves=0;
    assert(!boot_hold_admit() && !strcmp(boot_hold_stage,"admit_unheld"));
    boot_hold_execute(tl_native_move,director);
    assert(boot_hold_held==1 && !tl_native_moves && boot_hold_pending==3 && !boot_hold_failures);
    /* Another director in phase 2 runs, and is a failure. */
    unsigned char other[0x300];boot_hold_execute(tl_native_move,other);
    assert(boot_hold_held==1 && tl_native_moves==1 && boot_hold_failures==1);
    boot_hold_failures=0;tl_native_moves=0;
    assert(boot_hold_admit() && boot_hold_admitted && boot_hold_admit_pending==3);
    /* The first gated Move runs what was held. */
    boot_phase=3;stats.issued=1;
    boot_hold_execute(tl_native_move,director);
    assert(tl_native_moves==1 && boot_hold_released==1 && boot_hold_release_pending==3 && !strcmp(boot_hold_stage,"released"));
    stats.issued=2;boot_hold_execute(tl_native_move,director);
    assert(tl_native_moves==2 && boot_hold_released==1);
    boot_hold_active=0;boot_hold_armed=0;boot_hold_admitted=0;boot_hold_held=boot_hold_moves=boot_hold_released=0;
    stats.active=0;boot_mode=0;boot_phase=0;stats.issued=0;ui_thread=0;memset(LX(LX_BOOT_HOLD_DIRECTOR_VA),0,8);
    printf("boot hold decisions PASS\n");
}
static void test_cue_identity(void) {
    static __attribute__((aligned(16))) unsigned char cue[0x240],instance[0x240],lost[0x240],module[0x100],node[0x60];
    static const char module_name[]="FactionLogic",cue_name[]="Manager Delay"; /* the space is encoded */
    const void *vtable=LX(LX_CUE_VTABLE);uintptr_t data;uint64_t size;
    data=(uintptr_t)module_name;size=strlen(module_name);memcpy(module+0x18,&data,8);memcpy(module+0x20,&size,8);
    data=(uintptr_t)cue_name;size=strlen(cue_name);memcpy(node+0x20,&data,8);memcpy(node+0x28,&size,8);
    uintptr_t sentinel=(uintptr_t)module+0x60,first=(uintptr_t)node;
    memcpy(module+0x80,&first,8);memcpy(module+0x68,&first,8);memcpy(node,&sentinel,8);
    void *definition=cue,*own=lost,*owner=module;
    memcpy(node+0x40,&definition,8);
    memcpy(cue,&vtable,8);memcpy(cue+0x160,&definition,8);memcpy(cue+0x150,&owner,8);
    memcpy(instance,&vtable,8);memcpy(instance+0x160,&definition,8);
    memcpy(lost,&vtable,8);memcpy(lost+0x160,&own,8);memcpy(lost+0x150,&owner,8);
    script_rng_identity id;memset(&id,0,sizeof(id));id.local=(uintptr_t)cue;
    assert(script_rng_md(&id)==SR_OK && !strcmp(id.name,"FactionLogic.Manager%20Delay") && !id.instance && id.script==SCRIPT_RNG_MD_SCRIPT);
    uint64_t hash=14695981039346656037ULL;
    for(const char *p="FactionLogic.Manager%20Delay";*p;p++) hash=(hash^(unsigned char)*p)*1099511628211ULL;
    assert(id.owner==hash);
    memset(&id,0,sizeof(id));id.local=(uintptr_t)instance;
    assert(script_rng_md(&id)==SR_OK && id.owner==hash && id.instance==1); /* from the name cache */
    memset(&id,0,sizeof(id));id.local=(uintptr_t)lost;
    assert(script_rng_md(&id)==SR_UNNAMED); /* not in the module's tree */
    const void *other=LX(LX_CUE_VTABLE+8u);memcpy(lost,&other,8);
    memset(&id,0,sizeof(id));id.local=(uintptr_t)lost;
    assert(script_rng_md(&id)==SR_FOREIGN);
    memset(script_rng_names,0,sizeof(script_rng_names));script_rng_name_count=0;memset(npc_names,0,sizeof(npc_names));
    printf("cue identity PASS\n");
}
/* The sort itself is the shared code's (test_lockstep_gate.c); here the Linux pieces: the word, the
 * identities through the guarded read, and a delivery through the batch in portable order. */
static void *te_order[8];
static unsigned te_delivered;
static void te_native(void *source,void *event,double time) {(void)event;(void)time;te_order[te_delivered++%8]=source;}
static void test_event_order_linux(void) {
    reset_session();
    unsigned char *word=LX(LX_EVENT_CALL_WORD_VA);
    if(rd64(word)!=LX_EVENT_CALL_WORD_ORIGINAL) {const uint64_t original=LX_EVENT_CALL_WORD_ORIGINAL;tl_write(word,&original,8);}
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    assert(!event_order_arm() && !event_order_armed); /* no world identities */
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    world_identity_installed=1;
    assert(event_order_arm() && event_order_armed && event_order_installed && event_order_owned());
    int32_t rel;memcpy(&rel,word+4,4);
    assert(word+8+rel==thunk+LXE_THUNK_HOOK && word[3]==0xe8 && !memcmp(word,"\x8b\x75\x28",3));
    void *target;memcpy(&target,thunk+LXE_THUNK_HOOK+6,8);assert(target==(void*)event_order_deliver);
    /* Two equal-time sources, the higher address saved with the lower ID, one source unmapped. */
    static __attribute__((aligned(16))) unsigned char sources[3][32],events[3][16];
    uint64_t runtime[3]={7003,7004,9000};
    for(unsigned i=0;i<3;i++) memcpy(sources[i]+8,&runtime[i],8);
    assert(world_identity_record(7004,0x101) && world_identity_record(7003,0x102)); /* not sealed: as the load's event passes */
    static unsigned char batch[3*EVENT_ORDER_STRIDE];uint64_t count=3;double when=7.5;
    for(unsigned i=0;i<3;i++) {
        void *source=sources[i],*event=events[i];
        memcpy(batch+i*EVENT_ORDER_STRIDE+0x18,&source,8);memcpy(batch+i*EVENT_ORDER_STRIDE+0x28,&event,8);memcpy(batch+i*EVENT_ORDER_STRIDE+0x30,&when,8);
    }
    te_delivered=0;event_order_next=0;event_order_marked=0;
    for(unsigned i=0;i<3;i++) { /* the native loop: reads the slot, calls, releases */
        void *source,*event;double due;
        memcpy(&source,batch+i*EVENT_ORDER_STRIDE+0x18,8);memcpy(&event,batch+i*EVENT_ORDER_STRIDE+0x28,8);memcpy(&due,batch+i*EVENT_ORDER_STRIDE+0x30,8);
        event_order_deliver_with(te_native,batch,&count,source,event,due);
    }
    assert(te_delivered==3 && te_order[0]==sources[1] && te_order[1]==sources[0] && te_order[2]==sources[2]);
    assert(!event_order_failures && event_order_passes==1 && event_order_local==1 && event_order_ties==2 && event_order_moved>=2);
    event_order_restore();assert(event_order_restored && !event_order_installed && rd64(word)==LX_EVENT_CALL_WORD_ORIGINAL);
    event_order_armed=0;event_order_restored=0;event_order_passes=event_order_local=event_order_ties=event_order_moved=event_order_total=event_order_run_total=0;
    event_order_calls=0;event_order_hash=0xcbf29ce484222325ULL;event_order_next=event_order_marked=0;memset(event_order_marks,0,sizeof(event_order_marks));
    free(world_identities);world_identities=NULL;world_identity_installed=0;world_identity_sealed=0;world_identity_count=0;
    stats.active=0;boot_mode=0;ui_thread=0;
    printf("event order PASS\n");
}
/* The Gravidar's patches on these bytes, and finding its thread by name. */
static pthread_barrier_t tg_ready;
static volatile int tg_release;
static unsigned char *volatile tg_base;
static volatile uintptr_t tg_stack;
static void *tg_thread(void *name) {
    pthread_setname_np(pthread_self(),name);
    int local=0;tg_stack=(uintptr_t)&local;tg_base=gate_tls_self();
    pthread_barrier_wait(&tg_ready);
    while(!tg_release) usleep(1000);
    return NULL;
}
static void test_gravidar_linux(void) {
    reset_session();
    pthread_t first,second;
    pthread_barrier_init(&tg_ready,NULL,2);tg_release=0;
    assert(!lxg_thread_find() && !gravidar_thread_slot); /* no such thread */
    assert(!pthread_create(&first,NULL,tg_thread,(void*)"Gravidar"));pthread_barrier_wait(&tg_ready);
    assert(lxg_thread_find() && gravidar_thread_slot==tg_base && lxg_thread_sp<tg_stack && tg_stack-lxg_thread_sp<0x10000);
    struct sigaction now;assert(!sigaction(SIGRTMAX-3,NULL,&now) && now.sa_handler==SIG_DFL); /* the moment is over */
    /* Two threads of that name: refused, nothing taken. */
    gravidar_thread_slot=NULL;
    assert(!pthread_create(&second,NULL,tg_thread,(void*)"Gravidar"));pthread_barrier_wait(&tg_ready);
    assert(!lxg_thread_find() && !gravidar_thread_slot);
    tg_release=1;pthread_join(first,NULL);pthread_join(second,NULL);
    pthread_barrier_destroy(&tg_ready);
    gravidar_thread_slot=NULL;gravidar_thread_rng=NULL;lxg_thread_sp=0;
    /* The patches. */
    unsigned char *word=LX(LX_GRAVIDAR_CALL_WORD_VA);
    int32_t native=(int32_t)(LX_GRAVIDAR_PUBLISH_VA-(LX_GRAVIDAR_CALL_WORD_VA+6));
    unsigned char bytes[8]={0xf6,0xe8,0,0,0,0,0x48,0x8d};memcpy(bytes+2,&native,4);
    if(memcmp(word,bytes,8)) tl_write(word,bytes,8);
    void *apply=LX(LX_GRAVIDAR_UNIT_APPLY_VA);memcpy(LX(LX_GRAVIDAR_UNIT_SLOT_VA),&apply,8);
    stats.active=1;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();path_options=1;
    thunk=make_thunk();assert(thunk);
    assert(gravidar_install() && gravidar_installed && gravidar_unit_installed && gravidar_owned());
    int32_t rel;memcpy(&rel,word+2,4);
    assert(word+6+rel==thunk+LXG_THUNK_HOOK && word[0]==0xf6 && word[1]==0xe8 && word[6]==0x48 && word[7]==0x8d);
    void *target;memcpy(&target,thunk+LXG_THUNK_HOOK+6,8);assert(target==(void*)gravidar_defer);
    memcpy(&target,LX(LX_GRAVIDAR_UNIT_SLOT_VA),8);assert(target==(void*)gravidar_unit);
    gravidar_restore();
    assert(gravidar_restored && !gravidar_installed && !gravidar_unit_installed && !memcmp(word,bytes,8));
    memcpy(&target,LX(LX_GRAVIDAR_UNIT_SLOT_VA),8);assert(target==apply);
    /* The serial pathing option is required. */
    path_options=0;assert(!gravidar_install() && !gravidar_installed);
    gravidar_restored=0;stats.active=0;boot_mode=0;ui_thread=0;
    printf("gravidar patches and thread PASS\n");
}
static void test_load_patches(void) {
    reset_session();
    unsigned char *entry=LX(LX_BOOT_HOLD_MOVE_VA),*site=LX(LX_MD_RESUME_SITE_VA);
    if(rd64(entry)!=LX_BOOT_HOLD_ENTRY_ORIGINAL) { /* the synthetic image: the pinned bytes */
        const uint64_t original=LX_BOOT_HOLD_ENTRY_ORIGINAL;tl_write(entry,&original,8);
    }
    int32_t native=(int32_t)(LX_MD_RESUME_DELAY_VA-(LX_MD_RESUME_SITE_VA+5));
    if(site[0]!=0xe8) {
        unsigned char call[5]={0xe8,0,0,0,0};memcpy(call+1,&native,4);tl_write(site,call,5);
    }
    unsigned char window[16];memcpy(window,lxm_window(),16);
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    /* No thunk, nothing patched. */
    thunk=NULL;assert(!md_resume_arm() && !boot_hold_arm() && !md_resume_installed && !boot_hold_installed);
    thunk=make_thunk();assert(thunk);
    boot_phase=1;assert(!md_resume_arm() && !boot_hold_arm());boot_phase=0;
    assert(md_resume_arm() && boot_hold_arm() && md_resume_owned() && boot_hold_owned());
    assert(md_resume_arm() && boot_hold_arm()); /* a loading-UI reload */
    int32_t rel;memcpy(&rel,site+1,4);
    assert(site[0]==0xe8 && site+5+rel==thunk+LXM_THUNK_HOOK && !memcmp(window,lxm_window(),(size_t)(site+1-lxm_window())));
    void *target;
    assert(thunk[LXM_THUNK_HOOK]==0xff && thunk[LXM_THUNK_HOOK+1]==0x25);
    memcpy(&target,thunk+LXM_THUNK_HOOK+6,8);assert(target==(void*)md_resume_delay);
    memcpy(&rel,entry+1,4);
    assert(entry[0]==0xe9 && entry+5+rel==thunk+LXB_THUNK_HOOK && entry[5]==0x57 && entry[6]==0x41 && entry[7]==0x56);
    memcpy(&target,thunk+LXB_THUNK_HOOK+6,8);assert(target==(void*)boot_hold_move);
    /* The trampoline: the relocated prologue, then on into Move. */
    assert(!memcmp(thunk+LXB_THUNK_TRAMPOLINE,"\x55\x48\x89\xe5\x41\x57\xff\x25\x00\x00\x00\x00",12));
    memcpy(&target,thunk+LXB_THUNK_TRAMPOLINE+12,8);assert(target==(void*)(entry+6));
    /* A foreign owner is kept. */
    unsigned char flip=entry[7]^1;tl_write(entry+7,&flip,1);
    assert(!boot_hold_owned());boot_hold_restore();assert(!boot_hold_restored && boot_hold_installed);
    flip^=1;tl_write(entry+7,&flip,1);
    boot_hold_restore();assert(boot_hold_restored && !boot_hold_installed && rd64(entry)==LX_BOOT_HOLD_ENTRY_ORIGINAL);
    unsigned char saved=site[1],changed=saved^1;tl_write(site+1,&changed,1);
    assert(!md_resume_owned());md_resume_restore();assert(!md_resume_restored && md_resume_installed);
    tl_write(site+1,&saved,1);
    md_resume_restore();assert(md_resume_restored && !md_resume_installed && !memcmp(window,lxm_window(),16));
    /* Refused, nothing patched, on other bytes. */
    md_resume_armed=boot_hold_armed=0;md_resume_active=boot_hold_active=0;md_resume_restored=boot_hold_restored=0;
    unsigned char other=0xe9;tl_write(site,&other,1);assert(!md_resume_arm() && !md_resume_armed);
    other=0xe8;tl_write(site,&other,1);
    other=entry[0]^1;tl_write(entry,&other,1);assert(!boot_hold_arm() && !boot_hold_armed);
    other^=1;tl_write(entry,&other,1);
    stats.active=0;boot_mode=0;ui_thread=0;
    printf("load patches PASS\n");
}
