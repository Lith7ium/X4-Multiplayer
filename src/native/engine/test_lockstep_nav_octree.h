/* native_gate_nav_octree: builds wait for the gate's join in 'sync'. */
/* A cache with the two request counts (+0x338, +0x378) and a builder thread
 * that runs the native loop's shape: pop, build, Sleep(1) and retry on 0. */
static __attribute__((aligned(16))) unsigned char test_noct_cache[0x400];
static volatile LONG test_noct_stop,test_noct_loops;
static uint64_t test_noct_queue[64];static unsigned test_noct_head,test_noct_tail;
static CRITICAL_SECTION test_noct_queue_lock;
static volatile LONG test_noct_invalid; /* this ID's build is deferred */
static volatile LONG test_noct_slow;    /* a build that never ends while set */
static HANDLE test_noct_wake;           /* the request semaphore */
static uint64_t test_noct_built[64];static volatile LONG test_noct_built_count;
static void test_noct_counts(void) {
    uint64_t pending=test_noct_tail-test_noct_head,zero=0;
    memcpy(test_noct_cache+0x338,&pending,8);memcpy(test_noct_cache+0x378,&zero,8);
}
static void test_noct_request(uint64_t id) {
    EnterCriticalSection(&test_noct_queue_lock);test_noct_queue[test_noct_tail++%64]=id;test_noct_counts();LeaveCriticalSection(&test_noct_queue_lock);
    ReleaseSemaphore(test_noct_wake,1,NULL);
}
static uint64_t test_noct_fake_pop(void *cache,uint8_t *flag) {
    assert(cache==test_noct_cache);uint64_t id=0;
    EnterCriticalSection(&test_noct_queue_lock);
    if(test_noct_head!=test_noct_tail) {id=test_noct_queue[test_noct_head++%64];test_noct_counts();}
    LeaveCriticalSection(&test_noct_queue_lock);
    *flag=(uint8_t)(id&1);return id;
}
static uint8_t test_noct_fake_build(void *cache,uint64_t id,uint32_t flag) {
    assert(cache==test_noct_cache && flag==(id&1));
    if((LONG)id==InterlockedCompareExchange(&test_noct_invalid,0,0)) return 0;
    while(InterlockedCompareExchange(&test_noct_slow,0,0) && !InterlockedCompareExchange(&test_noct_stop,0,0)) Sleep(1);
    Sleep(2); /* a mesh load and the build */
    LONG at=InterlockedIncrement(&test_noct_built_count)-1;test_noct_built[at%64]=id;return 1;
}
static DWORD WINAPI test_noct_builder(LPVOID unused) {
    (void)unused;
    while(!InterlockedCompareExchange(&test_noct_stop,0,0)) {
        if(WaitForSingleObject(test_noct_wake,5)!=WAIT_OBJECT_0) continue;
        uint8_t flag=0;uint64_t id=noct_pop_with(test_noct_fake_pop,test_noct_cache,&flag);
        while(id && !InterlockedCompareExchange(&test_noct_stop,0,0)) {
            InterlockedIncrement(&test_noct_loops);
            if(noct_build_with(test_noct_fake_build,test_noct_cache,id,flag)) id=noct_pop_with(test_noct_fake_pop,test_noct_cache,&flag);
            else Sleep(1);
        }
    }
    return 0;
}
static void test_noct_settle(void) {for(int i=0;i<1000 && (test_noct_head!=test_noct_tail || noct_busy || noct_held);i++) Sleep(2);Sleep(10);}
static void test_nav_octree(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;DWORD saved_ui=ui_thread;
    stats.active=1;boot_phase=3;stats.issued=20;ui_thread=GetCurrentThreadId();
    InitializeCriticalSection(&test_noct_queue_lock);test_noct_wake=CreateSemaphoreW(NULL,0,64,NULL);assert(test_noct_wake);
    memset(test_noct_cache,0,sizeof(test_noct_cache));test_noct_head=test_noct_tail=0;test_noct_counts();
    if(!noct_lock_ready) {InitializeCriticalSection(&noct_lock);noct_lock_ready=1;}
    if(!noct_open) noct_open=CreateEventW(NULL,TRUE,TRUE,NULL);
    assert(noct_open);noct_installed=1;noct_failures=0;noct_rows=0;
    HANDLE builder=CreateThread(NULL,0,test_noct_builder,NULL,0,NULL);assert(builder);
    /* Observe: builds run as they come, rows carry the tick read on the builder thread. */
    noct_mode=NOCT_OBSERVE;noct_closed=0;
    test_noct_request(0x101);test_noct_request(0x102);test_noct_settle();
    assert(test_noct_built_count==2 && noct_builds==2 && noct_free_builds==2 && !noct_join_builds && noct_pops==2 && !noct_held);
    assert(noct_join_with(test_noct_cache,NULL,0,1000) && !noct_joins); /* observe never joins */
    char text[192];assert(noct_row_text(0,text,sizeof(text)) && strstr(text,"tick=20 key=0000000000000000 klass=2 flag=1 joined=0 us="));
    assert(!noct_row_text(2,text,sizeof(text)));
    /* Sync: the first join closes the builds; requests of a frame wait for the next join and are all built there. */
    noct_mode=NOCT_SYNC;assert(noct_join_with(test_noct_cache,NULL,0,1000) && noct_joins==1 && noct_closed);
    stats.issued=21;test_noct_request(0x201);test_noct_request(0x202);test_noct_request(0x203);
    Sleep(40);assert(test_noct_built_count==2 && noct_held && noct_waited==1); /* popped one, waiting in the build */
    stats.issued=22;assert(noct_join_with(test_noct_cache,NULL,0,2000));
    assert(test_noct_built_count==5 && noct_builds==5 && noct_join_builds==3 && noct_joins==2 && noct_closed && !noct_held && noct_last_pending==2);
    assert(noct_row_text(4,text,sizeof(text)) && strstr(text,"tick=22 ") && strstr(text," joined=1 "));
    Sleep(20);assert(test_noct_built_count==5);
    /* A deferred ID (invalidated sets) holds the requests behind it until it can be built; the join does not wait for it. */
    InterlockedExchange(&test_noct_invalid,0x301);test_noct_request(0x301);test_noct_request(0x302);
    assert(noct_join_with(test_noct_cache,NULL,0,2000) && noct_held && noct_deferred && noct_held_deferred==1 && test_noct_built_count==5);
    assert(noct_deferrals>=1 && noct_closed && test_noct_tail-test_noct_head==1);
    InterlockedExchange(&test_noct_invalid,0);
    Sleep(20);assert(test_noct_built_count==5); /* closed: nothing builds inside a frame */
    assert(noct_join_with(test_noct_cache,NULL,0,2000) && test_noct_built_count==7 && !noct_held && test_noct_head==test_noct_tail);
    /* A build that does not end within the join's time fails closed and leaves the builds open. */
    InterlockedExchange(&test_noct_slow,1);test_noct_request(0x401);Sleep(10);
    assert(!noct_join_with(test_noct_cache,NULL,0,30) && !strcmp(noct_stage,"join_timeout") && noct_failures==1 && !noct_closed);
    assert(!noct_join_with(test_noct_cache,NULL,0,30)); /* failed: no further joins */
    InterlockedExchange(&test_noct_slow,0);test_noct_settle();assert(test_noct_built_count==8);
    char status[768];assert(noct_status(status,sizeof(status)) && strstr(status,"mode=sync installed=1 ") && strstr(status," builds=8 ") &&
                            strstr(status," stage=join_timeout failures=1"));
    /* Restore opens a closed gate: a waiting build proceeds. */
    noct_failures=0;noct_stage="armed";assert(noct_join_with(test_noct_cache,NULL,0,2000) && noct_closed);
    test_noct_request(0x501);Sleep(20);assert(test_noct_built_count==8 && noct_waited>=2);
    noct_open_builds();test_noct_settle();assert(test_noct_built_count==9 && !noct_closed);
    InterlockedExchange(&test_noct_stop,1);WaitForSingleObject(builder,2000);CloseHandle(builder);
    CloseHandle(test_noct_wake);DeleteCriticalSection(&test_noct_queue_lock);
    /* Arming refusals on the zero image; sites are calls (and two tail jumps) to their targets. */
    noct_installed=0;noct_mode=NOCT_OFF;noct_rows=0;noct_failures=0;noct_stage="idle";
    noct_pops=noct_builds=noct_deferrals=noct_join_builds=noct_free_builds=noct_build_us=noct_max_build_us=noct_waited=0;
    noct_joins=noct_join_passes=noct_join_us=noct_max_join_us=noct_last_pending=noct_max_pending=noct_held_deferred=0;
    noct_held=noct_deferred=noct_busy=noct_closed=noct_joining=0;
    boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!noct_arm(NOCT_SYNC,0,0) && !strcmp(noct_stage,"site") && !noct_installed && !noct_arm(NOCT_OFF,0,0) && !noct_arm(NOCT_AGREED+1,30,0));
    assert(!noct_arm(NOCT_DEADLINE,0,0) && !strcmp(noct_stage,"delay") && !noct_arm(NOCT_DEADLINE,NOCT_DELAY_MAX+1,0));
    for(unsigned i=0;i<NOCT_SITES;i++) {
        unsigned char call[5]={noct_sites[i].op};int32_t d=noct_rel_to(i,image+noct_sites[i].target);memcpy(call+1,&d,4);memcpy(image+noct_sites[i].rva,call,5);
        assert(noct_site_state(i)==0);
    }
    image[noct_sites[5].rva]=0xe8;assert(noct_site_state(5)<0);image[noct_sites[5].rva]=0xe9; /* a tail jump stays a jump */
    assert(!noct_arm(NOCT_SYNC,0,0) && !strcmp(noct_stage,"pins")); /* the zero image's blocks do not match */
    assert(!noct_arm(NOCT_DEADLINE,30,0) && !strcmp(noct_stage,"pins") && noct_mode==NOCT_OFF);
    for(unsigned i=0;i<NOCT_SITES;i++) memset(image+noct_sites[i].rva,0,5);
    stats.issued=saved_issued;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;ui_thread=saved_ui;noct_stage="idle";
    printf("nav octree observe/sync join/deferral/timeout/restore tests PASS\n");
}
/* 'deadline': fake cache (build lock +0x30, request counts), octree maps, objects and busy sets. */
static __attribute__((aligned(16))) unsigned char test_nd_cache[0x400];
/* The octree's layout as NavOctreeBuilder 0x1094100 makes it: refcount +0, origin +0x10, cell size +0x20,
 * cells [+0x28, +0x30) of 12 bytes, depth +0x40 (id/flag are the test's own bookkeeping). */
typedef struct {LONG refs;uint32_t pad0;uint64_t id;float origin[4];float cell;uint32_t pad1;unsigned char *begin,*end,*cap;uint8_t level;
                unsigned char pad2[3];unsigned flag;unsigned char cells[3*24];} test_nd_octree;
static test_nd_octree test_nd_pool[64];static unsigned test_nd_pool_used;
static struct {uint64_t key;test_nd_octree *octree;} test_nd_map[64];static unsigned test_nd_map_count;
static uint64_t test_nd_busy[8];static unsigned test_nd_busy_count;
static int test_nd_ignore_busy,test_nd_moment;
static unsigned test_nd_requests;static uint64_t test_nd_last_request;
static unsigned test_nd_builds;
typedef struct {void *vtable;uint64_t id;} test_nd_object;
static test_nd_octree **test_nd_slot(uint64_t key) {
    for(unsigned i=0;i<test_nd_map_count;i++) if(test_nd_map[i].key==key) return &test_nd_map[i].octree;
    return NULL;
}
static int test_nd_is_busy(uint64_t id) {for(unsigned i=0;i<test_nd_busy_count;i++) if(test_nd_busy[i]==id) return 1;return 0;}
static void *test_nd_make(void *builder) {test_nd_octree *o=builder;o->refs=1;return o;}
static uint8_t test_nd_build(void *cache,uint64_t id,uint32_t flag) {
    assert(cache==test_nd_cache);EnterCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));
    if(!test_nd_ignore_busy && test_nd_is_busy(id)) {LeaveCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));return 0;}
    assert(test_nd_pool_used<64);test_nd_octree *o=&test_nd_pool[test_nd_pool_used++];o->id=id;o->flag=flag;
    /* Content from the object alone, unless test_nd_moment changes it (a build from another moment's state). */
    memset(o->cells,0,sizeof(o->cells));memcpy(o->cells,&id,8);o->cells[30]=(unsigned char)test_nd_moment;
    o->origin[0]=1.0f;o->cell=8.0f;o->level=2;o->begin=o->cells;o->end=o->cells+sizeof(o->cells);o->cap=o->end;
    void *made=noct_octree_with(test_nd_make,o);assert(made==o);
    uint64_t key=id*2+(flag&1);test_nd_octree **slot=test_nd_slot(key);
    if(slot) *slot=o;else {test_nd_map[test_nd_map_count].key=key;test_nd_map[test_nd_map_count++].octree=o;}
    ++test_nd_builds;LeaveCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));return 1;
}
static void test_nd_request(void *cache,uint64_t id,uint32_t flag) {assert(cache==test_nd_cache && flag<2);++test_nd_requests;test_nd_last_request=id*2+flag;}
static void test_nd_invalidate(void *object) {
    uint64_t id=((test_nd_object *)object)->id;
    for(unsigned i=test_nd_map_count;i-->0;) if(test_nd_map[i].key>>1==id) test_nd_map[i]=test_nd_map[--test_nd_map_count];
    test_nd_busy[test_nd_busy_count++]=id;
}
static void **test_nd_get(void *cache,void **out,void *object,uint32_t flag,uint32_t ask) {
    assert(cache==test_nd_cache);uint64_t key=((test_nd_object *)object)->id*2+(flag&1);test_nd_octree **slot=test_nd_slot(key);
    if(slot) {InterlockedIncrement(&(*slot)->refs);*out=*slot;}
    else {*out=NULL;if(ask&0xff) test_nd_request(cache,key>>1,flag&1);}
    return out;
}
static void *test_nd_lookup(test_nd_object *object,uint32_t flag,uint32_t ask) {
    void *out=(void*)1;assert(noct_get_with(test_nd_get,test_nd_request,0,test_nd_cache,&out,object,flag,ask)==&out);
    if(out) InterlockedDecrement(&((test_nd_octree *)out)->refs); /* the consumer lets go */
    return out;
}
/* The builder thread's view of one pop and build (single-threaded here). */
static uint8_t test_nd_builder(uint64_t id,uint32_t flag) {
    EnterCriticalSection(&noct_lock);noct_ctx.pop_serial=noct_serial;LeaveCriticalSection(&noct_lock);
    return noct_build_with(test_nd_build,test_nd_cache,id,flag);
}
static void test_nav_octree_deadline(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_phase=boot_phase;DWORD saved_ui=ui_thread;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();
    memset(test_nd_cache,0,sizeof(test_nd_cache));InitializeCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));
    if(!noct_lock_ready) {InitializeCriticalSection(&noct_lock);noct_lock_ready=1;}
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    noct_mode=NOCT_DEADLINE;noct_delay=30;noct_installed=1;noct_stage="armed";
    static test_nd_object a={NULL,0x500},b={NULL,0x600},c={NULL,0x700},d={NULL,0x800},e={NULL,0x900};
    /* A request at tick 10 is due at 40: built at 12, hidden until the join of 40. */
    stats.issued=10;noct_request_with(test_nd_request,test_nd_cache,0x500,1);
    assert(test_nd_requests==1 && noct_requested==1 && noct_live==1 && noct_next_due==40);
    noct_request_with(test_nd_request,test_nd_cache,0x500,1);assert(noct_requested==1 && test_nd_requests==2); /* one record */
    stats.issued=12;assert(test_nd_builder(0x500,1) && test_nd_builds==1 && noct_hidden_count==1);
    test_nd_octree *p1=*test_nd_slot(0x500*2+1);assert(p1->refs==1);
    stats.issued=20;assert(!test_nd_lookup(&a,1,1) && p1->refs==1 && noct_hides==1 && noct_rerequests==1 && test_nd_requests==3);
    assert(!test_nd_lookup(&a,1,0) && test_nd_requests==3); /* no build asked for: none made */
    assert(noct_join_with(test_nd_cache,test_nd_build,20,1000) && noct_drained && noct_joins==1); /* first join: nothing pending */
    assert(noct_join_with(test_nd_cache,test_nd_build,39,1000) && !noct_published && !test_nd_lookup(&a,1,0));
    assert(noct_join_with(test_nd_cache,test_nd_build,40,1000) && noct_published==1 && !noct_live && !noct_hidden_count && !noct_inline);
    stats.issued=41;assert(test_nd_lookup(&a,1,0)==p1);
    char text[256];assert(noct_row_text(0,text,sizeof(text)) && strstr(text,"tick=40 key=0000000000000000 klass=2 flag=1 joined=0 us=") &&
                          strstr(text," cells=6 due=40"));
    {uint64_t c1=0,c2=0,c3=0,n=0;test_nd_octree x,y;memcpy(&x,p1,sizeof(x));memcpy(&y,p1,sizeof(y));
     x.begin=x.cells;x.end=x.cells+sizeof(x.cells);y.begin=y.cells;y.end=y.cells+sizeof(y.cells);
     c1=noct_content((unsigned char*)&x,&n);c2=noct_content((unsigned char*)&y,&n);y.cells[30]^=1;c3=noct_content((unsigned char*)&y,&n);
     assert(c1 && c1==c2 && c3!=c1 && n==6 && strstr(text,"content=") && !strstr(text,"content=0000000000000000"));}
    /* Not built by its due tick: the join builds it on the main thread. */
    stats.issued=50;noct_request_with(test_nd_request,test_nd_cache,0x600,0);assert(noct_next_due==80);
    assert(noct_join_with(test_nd_cache,test_nd_build,79,1000) && !noct_inline && test_nd_builds==1);
    assert(noct_join_with(test_nd_cache,test_nd_build,80,1000) && noct_inline==1 && test_nd_builds==2 && noct_published==2);
    assert(noct_row_text(1,text,sizeof(text)) && strstr(text,"tick=80 ") && strstr(text," flag=0 joined=1 ") && strstr(text," due=80"));
    stats.issued=81;assert(test_nd_lookup(&b,0,0)==*test_nd_slot(0x600*2));
    /* Still in the invalidated sets at its due tick: one frame later, decided by the frames. */
    stats.issued=60;noct_request_with(test_nd_request,test_nd_cache,0x700,1);test_nd_busy[test_nd_busy_count++]=0x700;
    assert(!test_nd_builder(0x700,1) && noct_deferrals==1);
    assert(noct_join_with(test_nd_cache,test_nd_build,90,1000) && noct_postponed==1 && noct_published==2 && noct_next_due==91);
    test_nd_busy_count=0;
    assert(noct_join_with(test_nd_cache,test_nd_build,91,1000) && noct_published==3 && noct_inline==3);
    stats.issued=92;assert(test_nd_lookup(&c,1,0));
    /* Popped before an invalidation: its octree stays hidden until a new request's build replaces it. */
    stats.issued=100;noct_request_with(test_nd_request,test_nd_cache,0x800,0);
    EnterCriticalSection(&noct_lock);noct_ctx.pop_serial=noct_serial;LeaveCriticalSection(&noct_lock);
    noct_invalidate_with(test_nd_invalidate,4,&d);assert(noct_invalidations==1 && !noct_find(0x800*2) && !noct_live);
    test_nd_ignore_busy=1;assert(noct_build_with(test_nd_build,test_nd_cache,0x800,0));test_nd_ignore_busy=0; /* it passed the busy check first */
    assert(noct_stale==1 && noct_hidden_count==1);
    stats.issued=101;assert(!test_nd_lookup(&d,0,1) && test_nd_last_request==0x800*2 && noct_live==1); /* the miss asks again */
    assert(noct_join_with(test_nd_cache,test_nd_build,131,1000) && noct_postponed==2 && noct_hidden_count==1); /* still busy at 131 */
    test_nd_busy_count=0;
    assert(test_nd_builder(0x800,0) && noct_hidden_count==1 && noct_stale==1); /* the new build replaces the stale octree, hidden until due */
    assert(noct_join_with(test_nd_cache,test_nd_build,132,1000) && !noct_hidden_count && !noct_live);
    stats.issued=133;assert(test_nd_lookup(&d,0,0)==*test_nd_slot(0x800*2));
    /* A build nobody recorded (asked before arming) shows at once; a rebuild of a shown octree too. */
    assert(test_nd_builder(0x900,1) && noct_unrecorded==1 && test_nd_lookup(&e,1,0)==*test_nd_slot(0x900*2+1));
    noct_request_with(test_nd_request,test_nd_cache,0x500,1);assert(!noct_live); /* shown: the record stays */
    assert(test_nd_builder(0x500,1) && test_nd_lookup(&a,1,0)==*test_nd_slot(0x500*2+1) && test_nd_lookup(&a,1,0)!=p1);
    /* Stop shows everything; the status names the hitch. */
    stats.issued=140;noct_request_with(test_nd_request,test_nd_cache,0x600,1);assert(test_nd_builder(0x600,1) && noct_hidden_count==1);
    char status[1536];assert(noct_status(status,sizeof(status)) && strstr(status,"mode=deadline installed=1 ") && strstr(status," delay=30 ") &&
                             strstr(status," published=4 ") && strstr(status," live=1 ") && strstr(status," inline=4 ") && strstr(status," postponed=2 ") &&
                             strstr(status," stale=1 ") && strstr(status," max_join_us=") && strstr(status," hides="));
    noct_open_builds();assert(!noct_hidden_count && test_nd_lookup(&b,1,0));
    /* A full record table fails closed. */
    noct_record_count=NOCT_RECORDS*3/4;EnterCriticalSection(&noct_lock);noct_note_request(0xa00,0);LeaveCriticalSection(&noct_lock);
    assert(noct_failures==1 && !strcmp(noct_stage,"records_full") && !noct_join_with(test_nd_cache,test_nd_build,150,1000));
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    noct_mode=NOCT_OFF;noct_installed=0;noct_stage="idle";noct_delay=NOCT_DELAY_DEFAULT;
    DeleteCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));
    stats.issued=saved_issued;stats.active=saved_active;boot_phase=saved_phase;ui_thread=saved_ui;
    printf("nav octree deadline hide/publish/inline/postpone/stale/unrecorded/stop tests PASS\n");
}
/* 'agreed': three slots' reports through the wire decide when an octree shows; no join builds. */
static char test_na_report[64];
static test_nd_octree test_na_shared;
static void **test_na_existing(void *cache,void **out,void *root,uint32_t flag) {
    assert(cache==test_nd_cache && root==(void*)0x7007 && flag==0);InterlockedIncrement(&test_na_shared.refs);*out=&test_na_shared;return out;
}
/* A build that finds its root-keyed octree already made: no new octree, no octree hook. */
static uint8_t test_na_noop_build(void *cache,uint64_t id,uint32_t flag) {
    void *out=NULL;assert(noct_existing_with(test_na_existing,cache,&out,(void*)0x7007,flag)==&out && out==&test_na_shared);
    InterlockedDecrement(&test_na_shared.refs);(void)id;return 1;
}
static const char *test_na_report_now(void) {
    int n=noct_report_text(test_na_report,sizeof(test_na_report));test_na_report[n>0?n:0]=0;return test_na_report;
}
static void test_nav_octree_agreed(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_phase=boot_phase;DWORD saved_ui=ui_thread;
    unsigned saved_version=wire_version,saved_clients=wire_clients;
    world_identity_entry *prior_identities=world_identities;LONG64 prior_count=world_identity_count;LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0xa01,0x300) && world_identity_record(0xa02,0x100) && world_identity_record(0xa03,0x200) &&
           world_identity_record(0xa05,0x500) && world_identity_record(0xb01,0x700) && world_identity_record(0xb02,0x600) &&
           world_identity_record(0xa06,0x800));
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();wire_version=3;wire_clients=3;
    memset(test_nd_cache,0,sizeof(test_nd_cache));InitializeCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));
    InitializeCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x2e0));
    test_nd_pool_used=test_nd_map_count=test_nd_busy_count=0;test_nd_builds=0;
    if(!noct_lock_ready) {InitializeCriticalSection(&noct_lock);noct_lock_ready=1;}
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    noct_mode=NOCT_AGREED;noct_installed=1;noct_stage="armed";
    static test_nd_object a1={NULL,0xa01},a2={NULL,0xa02},a3={NULL,0xa03},a4={NULL,0xa04},a5={NULL,0xa05};
    /* Tick 22 (sector entry): four requests; the join numbers the keyed ones by (class, key, flag), the keyless one shows natively. */
    stats.issued=22;
    noct_request_with(test_nd_request,test_nd_cache,0xa01,0);noct_request_with(test_nd_request,test_nd_cache,0xa02,1);
    noct_request_with(test_nd_request,test_nd_cache,0xa03,0);noct_request_with(test_nd_request,test_nd_cache,0xa04,0);
    assert(!*test_na_report_now()); /* nothing numbered yet */
    char receipt[160];assert(!noct_agree_text(22,3,",,",receipt,sizeof(receipt)) || 1);
    assert(noct_join_with(test_nd_cache,test_nd_build,22,1000) && noct_enumerated && noct_next_index==3 && noct_local_records==1);
    assert(noct_entry(0)->key==0xa02*2+1 && noct_entry(1)->key==0xa03*2 && noct_entry(2)->key==0xa01*2 && test_nd_builds==0);
    assert(!strcmp(test_na_report_now(),"00000000" "00000000000000000000000000000000"));
    /* This client builds index 1 (and the keyless object, which shows at once). */
    assert(test_nd_builder(0xa03,0) && test_nd_builder(0xa04,0) && noct_hidden_count==1);
    assert(!strcmp(test_na_report_now(),"00000000" "01000000000000000000000000000000"));
    stats.issued=23;assert(!test_nd_lookup(&a3,0,0) && test_nd_lookup(&a4,0,0));
    /* Admission of tick 23: slot 2 has built everything, slots 1 and 3 only index 1: index 1 is agreed. */
    char payloads[200];snprintf(payloads,sizeof(payloads),"%s,%s,%s","0000000001000000000000000000000000000000",
                                "0300000000000000000000000000000000000000","0000000001000000000000000000000000000000");
    int n=noct_agree_text(23,3,payloads,receipt,sizeof(receipt));assert(n>0);
    assert(!strncmp(receipt,"OCT_1|23|0|1|3|",15) && strlen(receipt)==15+16 && noct_agreed_total==1);
    uint64_t keys=noct_keys_hash;
    assert(noct_join_with(test_nd_cache,test_nd_build,23,1000) && noct_published==1 && noct_shown_index==0 && test_nd_builds==2);
    stats.issued=24;assert(test_nd_lookup(&a3,0,0)==*test_nd_slot(0xa03*2) && !test_nd_lookup(&a2,1,0));
    char text[256];assert(noct_row_text(0,text,sizeof(text)) && strstr(text,"tick=23 key=0000000000000200 klass=0 flag=0 joined=0 us=") &&
                          strstr(text," index=1 assigned=22"));
    /* The rest builds here; slot 3 sends nothing at 24 (its last report stands), all of them at 25. */
    assert(test_nd_builder(0xa02,1) && test_nd_builder(0xa01,0) && !strcmp(test_na_report_now(),"03000000" "00000000000000000000000000000000"));
    assert(noct_agree_text(24,3,"0300000000000000000000000000000000000000,0300000000000000000000000000000000000000,",receipt,sizeof(receipt)));
    assert(noct_agreed_total==1 && noct_join_with(test_nd_cache,test_nd_build,24,1000) && noct_published==1);
    assert(noct_agree_text(25,3,",,0300000000000000000000000000000000000000",receipt,sizeof(receipt)) && !strncmp(receipt,"OCT_1|25|0|3|3|",15));
    assert(noct_join_with(test_nd_cache,test_nd_build,25,1000) && noct_published==3 && noct_shown_index==3 && !noct_live && !noct_hidden_count);
    stats.issued=26;assert(test_nd_lookup(&a1,0,0) && test_nd_lookup(&a2,1,0) && !*test_na_report_now() && noct_keys_hash==keys);
    /* An invalidated request needs no build: its index counts as done here and shows nothing. */
    noct_request_with(test_nd_request,test_nd_cache,0xa05,0);assert(noct_join_with(test_nd_cache,test_nd_build,26,1000) && noct_next_index==4);
    assert(noct_keys_hash!=keys && !strcmp(test_na_report_now(),"03000000" "00000000000000000000000000000000"));
    noct_invalidate_with(test_nd_invalidate,4,&a5);assert(!strcmp(test_na_report_now(),"04000000" "00000000000000000000000000000000"));
    assert(noct_agree_text(27,3,"0400000000000000000000000000000000000000,0400000000000000000000000000000000000000,"
                                "0400000000000000000000000000000000000000",receipt,sizeof(receipt)));
    assert(noct_join_with(test_nd_cache,test_nd_build,27,1000) && noct_shown_index==4 && noct_published==3);
    /* A no-op build (the root-keyed octree was already made, e.g. for a request an invalidation dropped, hidden
     * since) shows that octree when its own index is agreed. */
    test_na_shared.refs=1;EnterCriticalSection(&noct_lock);noct_hide(&test_na_shared,NOCT_NEVER,0xdead*2);LeaveCriticalSection(&noct_lock);
    noct_request_with(test_nd_request,test_nd_cache,0xa06,0);assert(noct_join_with(test_nd_cache,test_nd_build,27,1000) && noct_next_index==5);
    EnterCriticalSection(&noct_lock);noct_ctx.pop_serial=noct_serial;LeaveCriticalSection(&noct_lock);
    assert(noct_build_with(test_na_noop_build,test_nd_cache,0xa06,0) && noct_find(0xa06*2)->state==NOCT_R_NOOP && noct_find(0xa06*2)->octree==&test_na_shared);
    assert(noct_hidden_find(&test_na_shared)>=0 && !strcmp(test_na_report_now(),"05000000" "00000000000000000000000000000000"));
    assert(noct_agree_text(28,3,"0500000000000000000000000000000000000000,0500000000000000000000000000000000000000,"
                                "0500000000000000000000000000000000000000",receipt,sizeof(receipt)));
    assert(noct_join_with(test_nd_cache,test_nd_build,28,1000) && noct_hidden_find(&test_na_shared)<0 && noct_shown_index==5 && test_na_shared.refs==1);
    /* Refusals: a malformed payload, a wrong slot count, another mode. Joins never build. */
    assert(!noct_agree_text(28,3,"04000000000000000000000000000000000000zz,,",receipt,sizeof(receipt)));
    assert(!noct_agree_text(28,3,",",receipt,sizeof(receipt)) && !noct_agree_text(28,3,",,,",receipt,sizeof(receipt)));
    assert(!noct_agree_text(28,3,"0400,,",receipt,sizeof(receipt)) && test_nd_builds==4 && !noct_inline);
    char status[1536];assert(noct_status(status,sizeof(status)) && strstr(status,"mode=agreed installed=1 ") && strstr(status," assigned=5 shown=5 agreed=5 waiting=0 local=1 ") &&
                             strstr(status," reports=12 ") && strstr(status," max_wait=3 ") && strstr(status," published=4 "));
    /* Lookup and invalidation rows, per frame: the hidden lookup at 23, the pending miss at 24, the invalidation at 26. */
    char look[256];int hidden_row=0,miss_row=0,inval_row=0;
    for(uint64_t i=0;noct_look_text(i,look,sizeof(look));i++) {
        hidden_row|=strstr(look,"tick=23 site=446a8b key=0000000000000200 klass=0 flag=0 ask=0 result=hidden count=1")!=NULL;
        miss_row|=strstr(look,"tick=24 site=446a8b key=0000000000000100 klass=0 flag=1 ask=0 result=miss count=1")!=NULL;
        inval_row|=strstr(look,"site=7450d3 key=0000000000000500 klass=0 flag=0 ask=0 result=invalidated count=1")!=NULL;
    }
    assert(hidden_row && miss_row && inval_row && !noct_look_overflow);
    /* First join after arming: requests already in the native set (in-order walk of its tree) are numbered too. */
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    static __attribute__((aligned(16))) unsigned char nodes[2][0x30];unsigned char *set=test_nd_cache+0x310,*na=nodes[0],*nb=nodes[1],*zero=NULL;
    memset(nodes,0,sizeof(nodes));uint64_t two=2,ka=0xb01,kb=0xb02;
    memcpy(set+8,&na,8);memcpy(set+0x20,&na,8);memcpy(set+0x28,&two,8);
    memcpy(na,&set,8);memcpy(na+8,&zero,8);memcpy(na+0x10,&nb,8);memcpy(na+0x20,&ka,8);
    memcpy(nb,&na,8);memcpy(nb+0x20,&kb,8);
    stats.issued=0;assert(noct_join_with(test_nd_cache,test_nd_build,0,1000) && noct_next_index==2);
    assert(noct_entry(0)->key==0xb02*2 && noct_entry(1)->key==0xb01*2); /* saved 0x600 before 0x700 */
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    noct_mode=NOCT_OFF;noct_installed=0;noct_stage="idle";
    DeleteCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x30));DeleteCriticalSection((CRITICAL_SECTION *)(test_nd_cache+0x2e0));
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior_identities;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    wire_version=saved_version;wire_clients=saved_clients;
    stats.issued=saved_issued;stats.active=saved_active;boot_phase=saved_phase;ui_thread=saved_ui;
    printf("nav octree agreed numbering/report/agreement/publication/invalidation/enumeration tests PASS\n");
}
/* Input snapshot: a fake slot 1 whose output follows the object's live state. */
typedef struct {void *holder_vtable;uint64_t id;unsigned char pad[0x28];void *holder;LONG refs,alive;uint32_t live;uint32_t parts;} test_ns_object;
static test_ns_object test_ns_a,test_ns_b,test_ns_c;
static __attribute__((aligned(16))) unsigned char test_ns_view[0x400];
static volatile LONG test_ns_seen_view=-1,test_ns_slot1_calls,test_ns_destroys;
static void test_ns_destroy(void *holder) {(void)holder;InterlockedIncrement(&test_ns_destroys);}
static void *test_ns_holder_vtable[1]={(void*)test_ns_destroy};
static void test_ns_init(test_ns_object *o,uint64_t id,uint32_t live,uint32_t parts) {
    memset(o,0,sizeof(*o));o->id=id;o->holder=test_ns_holder_vtable;o->refs=1;o->alive=2;o->live=live;o->parts=parts;
}
static void test_ns_append(void *vector,void *where,void *part,void *transform) {
    unsigned char **v=vector,*begin=v[0],*end=v[1];assert(where==end);size_t n=(size_t)(end-begin)/NOCT_SNAP_ENTRY;
    unsigned char *grown=malloc((n+4)*NOCT_SNAP_ENTRY);assert(grown);if(n) memcpy(grown,begin,n*NOCT_SNAP_ENTRY);free(begin);
    memcpy(grown+n*NOCT_SNAP_ENTRY,part,8);memset(grown+n*NOCT_SNAP_ENTRY+8,0,8);memcpy(grown+n*NOCT_SNAP_ENTRY+0x10,transform,0x40);
    v[0]=grown;v[1]=grown+(n+1)*NOCT_SNAP_ENTRY;v[2]=grown+(n+4)*NOCT_SNAP_ENTRY;
}
/* Like slot 1: reads +0x10 (object) and +0x180 (flag), writes the bounds +0x20..+0x4f and appends parts. */
static void test_ns_slot1(void *builder_) {
    unsigned char *builder=builder_;test_ns_object *o=NULL;memcpy(&o,builder+0x10,8);InterlockedIncrement(&test_ns_slot1_calls);
    LONG view;memcpy(&view,test_ns_view+0x314,4);InterlockedExchange(&test_ns_seen_view,view);
    for(unsigned i=0;i<0x30;i++) builder[0x20+i]=(unsigned char)(o->live+i+builder[0x180]);
    for(uint32_t k=0;k<o->parts;k++) {
        unsigned char transform[0x40];memset(transform,(int)(o->live+k),sizeof(transform));void *part=(void*)(uintptr_t)(0x1000+k);
        unsigned char *end=NULL,*cap=NULL;memcpy(&end,builder+0x190,8);memcpy(&cap,builder+0x198,8);
        if(end!=cap) {memcpy(end,&part,8);memcpy(end+0x10,transform,0x40);end+=NOCT_SNAP_ENTRY;memcpy(builder+0x190,&end,8);}
        else test_ns_append(builder+0x188,end,&part,transform);
    }
}
static void **test_ns_lookup(void **out,uint64_t id) {
    test_ns_object *o=id==0x701?&test_ns_a:id==0x702?&test_ns_b:id==0x703?&test_ns_c:NULL;
    if(o && o->alive==2) {InterlockedIncrement(&o->refs);*out=o;} else *out=NULL;
    return out;
}
static void test_ns_request(void *cache,uint64_t id,uint32_t flag) {(void)cache;(void)id;(void)flag;}
/* A builder as 0x1093000 makes it: the object at +0x10, the flag at +0x180, the parts vector empty. */
typedef struct {unsigned char b[NOCT_BUILDER_SIZE];} test_ns_builder;
static void test_ns_make(test_ns_builder *x,test_ns_object *o,unsigned flag) {memset(x,0,sizeof(*x));memcpy(x->b+0x10,&o,8);x->b[0x180]=(unsigned char)flag;}
static void test_ns_free(test_ns_builder *x) {unsigned char *begin=NULL;memcpy(&begin,x->b+0x188,8);free(begin);}
static uint32_t test_ns_count(const test_ns_builder *x) {
    unsigned char *begin=NULL,*end=NULL;memcpy(&begin,x->b+0x188,8);memcpy(&end,x->b+0x190,8);return (uint32_t)((size_t)(end-begin)/NOCT_SNAP_ENTRY);
}
static test_ns_builder test_ns_waiting;static volatile LONG test_ns_waiting_done;
static DWORD WINAPI test_ns_builder_thread(LPVOID unused) {
    (void)unused;noct_slot1_with(test_ns_slot1,test_ns_append,test_ns_waiting.b);InterlockedExchange(&test_ns_waiting_done,1+noct_tls_snapshot);return 0;
}
static void test_nav_octree_snapshot(void) {
    uint64_t saved_issued=stats.issued;uint32_t saved_active=stats.active;int saved_phase=boot_phase;DWORD saved_ui=ui_thread,saved_main=main_thread;
    stats.active=1;boot_phase=3;ui_thread=main_thread=GetCurrentThreadId();
    if(!noct_lock_ready) {InitializeCriticalSection(&noct_lock);noct_lock_ready=1;}
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    if(!noct_snap_scratch) noct_snap_scratch=VirtualAlloc(NULL,(size_t)NOCT_SNAP_PARTS*NOCT_SNAP_ENTRY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!noct_snap_event) noct_snap_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(noct_snap_scratch && noct_snap_event);
    noct_view_override=test_ns_view;memset(test_ns_view,0,sizeof(test_ns_view));
    noct_mode=NOCT_OBSERVE;noct_snapshot=1;noct_installed=1;noct_stage="armed";
    noct_slot1_original=test_ns_slot1;noct_lookup_original=test_ns_lookup;noct_append_original=test_ns_append;
    test_ns_init(&test_ns_a,0x701,10,3);test_ns_init(&test_ns_b,0x702,20,2);test_ns_init(&test_ns_c,0x703,30,1);
    /* Tick 22: a request marks (A, 0); its build starts on the builder thread before the join and waits. */
    stats.issued=22;noct_request_with(test_ns_request,NULL,0x701,0);
    assert(noct_snap_count==1 && noct_snap_fresh_count==1 && noct_snap_find(0x701*2)->state==NOCT_S_PENDING);
    LONG view=1;memcpy(test_ns_view+0x314,&view,4);
    test_ns_make(&test_ns_waiting,&test_ns_a,0);test_ns_waiting_done=0;
    HANDLE builder=CreateThread(NULL,0,test_ns_builder_thread,NULL,0,NULL);assert(builder);
    Sleep(80);assert(!test_ns_waiting_done && noct_snap_waits>=1 && InterlockedCompareExchange(&noct_builder_view,0,0)==1);
    /* The join collects A at tick 22's state, with the builder thread's view; the main thread's view comes back after. */
    view=0;memcpy(test_ns_view+0x314,&view,4);test_ns_a.live=11;
    assert(noct_join_with(NULL,NULL,22,1000) && noct_snap_captured==1 && test_ns_seen_view==1 && noct_main_view==0);
    memcpy(&view,test_ns_view+0x314,4);assert(view==0 && test_ns_a.refs==1 && !test_ns_destroys);
    WaitForSingleObject(builder,2000);CloseHandle(builder);
    assert(test_ns_waiting_done==2 && test_ns_count(&test_ns_waiting)==3 && test_ns_waiting.b[0x20]==11 && noct_snap_used==1);
    /* Later builds of A use the same snapshot, whatever A's live state is by then. */
    test_ns_a.live=99;test_ns_a.parts=7;LONG calls=test_ns_slot1_calls;
    test_ns_builder again;test_ns_make(&again,&test_ns_a,0);noct_slot1_with(test_ns_slot1,test_ns_append,again.b);
    assert(noct_tls_snapshot==1 && test_ns_slot1_calls==calls && test_ns_count(&again)==3 && !memcmp(again.b+0x20,test_ns_waiting.b+0x20,0x30));
    unsigned char *p1=NULL,*p2=NULL;memcpy(&p1,again.b+0x188,8);memcpy(&p2,test_ns_waiting.b+0x188,8);
    for(unsigned k=0;k<3;k++) assert(!memcmp(p1+k*NOCT_SNAP_ENTRY,p2+k*NOCT_SNAP_ENTRY,8) && !memcmp(p1+k*NOCT_SNAP_ENTRY+0x10,p2+k*NOCT_SNAP_ENTRY+0x10,0x40));
    /* A builder whose vector has room gets the parts written in place (as slot 1 does). */
    test_ns_builder roomy;test_ns_make(&roomy,&test_ns_a,0);unsigned char *room=malloc(8*NOCT_SNAP_ENTRY),*room_cap=room+8*NOCT_SNAP_ENTRY;assert(room);
    memcpy(roomy.b+0x188,&room,8);memcpy(roomy.b+0x190,&room,8);memcpy(roomy.b+0x198,&room_cap,8);
    noct_slot1_with(test_ns_slot1,test_ns_append,roomy.b);assert(test_ns_count(&roomy)==3 && !memcmp(room,p2,8));
    /* Not asked for since arming (B), or the other flag of A: live. */
    test_ns_builder live;test_ns_make(&live,&test_ns_b,0);noct_slot1_with(test_ns_slot1,test_ns_append,live.b);
    assert(!noct_tls_snapshot && noct_snap_live==1 && test_ns_count(&live)==2 && live.b[0x20]==20);
    /* An invalidation drops A's snapshot; the next request takes a new one at its join. */
    test_ns_object *ao=&test_ns_a;noct_invalidate_with((noct_invalidate_fn)(void *)test_ns_destroy,4,ao);
    assert(noct_snap_dropped==1 && !noct_snap_find(0x701*2));
    stats.issued=40;noct_request_with(test_ns_request,NULL,0x701,0);assert(noct_join_with(NULL,NULL,40,1000) && noct_snap_captured==2);
    test_ns_builder fresh;test_ns_make(&fresh,&test_ns_a,0);noct_slot1_with(test_ns_slot1,test_ns_append,fresh.b);
    assert(noct_tls_snapshot && test_ns_count(&fresh)==7 && fresh.b[0x20]==99);
    /* An object gone by its join: no snapshot, its build runs live. */
    test_ns_c.alive=3;noct_request_with(test_ns_request,NULL,0x703,1);assert(noct_join_with(NULL,NULL,41,1000) && noct_snap_gone==1);
    assert(noct_snap_find(0x703*2+1)->state==NOCT_S_GONE);
    test_ns_builder gone;test_ns_make(&gone,&test_ns_c,1);noct_slot1_with(test_ns_slot1,test_ns_append,gone.b);assert(!noct_tls_snapshot && noct_snap_live==2);
    /* On the main thread a pending snapshot is never waited for; stop releases a waiting builder (live). */
    test_ns_c.alive=2;noct_request_with(test_ns_request,NULL,0x702,1);
    test_ns_builder mainb;test_ns_make(&mainb,&test_ns_b,1);noct_slot1_with(test_ns_slot1,test_ns_append,mainb.b);assert(noct_snap_live==3);
    test_ns_make(&test_ns_waiting,&test_ns_b,1);test_ns_waiting_done=0;view=1;memcpy(test_ns_view+0x314,&view,4);
    builder=CreateThread(NULL,0,test_ns_builder_thread,NULL,0,NULL);assert(builder);Sleep(60);assert(!test_ns_waiting_done);
    noct_installed=0;noct_open_builds();WaitForSingleObject(builder,2000);CloseHandle(builder);assert(test_ns_waiting_done==1 && noct_snap_live==4);
    noct_installed=1;
    char status[1536];assert(noct_status(status,sizeof(status)) && strstr(status," snapshot=1 snaps=") && strstr(status," snap_captured=2 snap_gone=1 snap_used=")
                             && strstr(status," builder_view=1 main_view=0 "));
    /* A full row carries snapshot=. */
    noct_ring[0]=(noct_row){41,0x701,5,0,0,0,0,0,0,1};noct_rows=1;char row[256];
    assert(noct_row_text(0,row,sizeof(row)) && strstr(row," snapshot=1"));
    test_ns_free(&test_ns_waiting);test_ns_free(&again);test_ns_free(&live);test_ns_free(&fresh);test_ns_free(&gone);test_ns_free(&mainb);free(room);
    EnterCriticalSection(&noct_lock);noct_reset_counters();LeaveCriticalSection(&noct_lock);
    noct_view_override=NULL;noct_mode=NOCT_OFF;noct_snapshot=0;noct_installed=0;noct_stage="idle";noct_rows=0;
    noct_slot1_original=NULL;noct_lookup_original=NULL;noct_append_original=NULL;
    stats.issued=saved_issued;stats.active=saved_active;boot_phase=saved_phase;ui_thread=saved_ui;main_thread=saved_main;
    printf("nav octree input snapshot capture/wait/inject/live/invalidate/gone/view/stop tests PASS\n");
}
static void test_nav_octree_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    for(unsigned i=0;i<NOCT_SITES;i++) assert(noct_site_state(i)==0);
    assert(noct_pins_ok());
    /* The thread loop: wait on the semaphore at this+0x50 (cache+0x58), pop and build with rcx=this-8 (the cache). */
    assert(!memcmp(image+0x1092210,"\x48\x8b\x4e\x50\xba\xff\xff\xff\xff",9) && !memcmp(image+0x109222f,"\x48\x8d\x4e\xf8",4));
    assert(!memcmp(image+0x1092249,"\x48\x8d\x4e\xf8\x48\x8b\xd3",7) && !memcmp(image+0x1092255,"\x84\xc0\x74\x16",4));
    assert(!memcmp(image+0x109226f,"\xb9\x01\x00\x00\x00\xff\x15",7)); /* Sleep(1) and retry the same ID */
    /* The pop: the two counts; the request: sets +0x310/+0x350 under +0x2e0, then the semaphore +0x58. */
    assert(!memcmp(image+0x1092041,"\x48\x39\xaf\x38\x03\x00\x00",7) && !memcmp(image+0x10920d3,"\x48\x39\xb7\x78\x03\x00\x00",7));
    assert(!memcmp(image+0x109288b,"\x48\x8d\x99\xe0\x02\x00\x00",7) && !memcmp(image+0x10928a4,"\x48\x8d\xbe\x50\x03\x00\x00",7));
    assert(!memcmp(image+0x10928ad,"\x48\x8d\xbe\x10\x03\x00\x00",7) && !memcmp(image+0x10928fc,"\x48\x8b\x4e\x58",4));
    /* Creation asks for both octrees of an object (flag 0, then 1) from the singleton. */
    assert(0x75b2cdu+*(int32_t*)(image+0x75b2c9)==NOCT_CACHE && image[0x75b2cd]==0xe8 && 0x75b2d2u+*(int32_t*)(image+0x75b2ce)==0x1092870u);
    assert(!memcmp(image+0x75b2d6,"\x41\xb0\x01",3) && image[0x75b2e5]==0xe9 && 0x75b2eau+*(int32_t*)(image+0x75b2e6)==0x1092870u);
    /* The build: invalidated sets +0x260/+0x2a0 under +0x238 return 0 (retry); the result is dil. */
    assert(!memcmp(image+0x109305f,"\x49\x8d\xbf\x38\x02\x00\x00",7) && !memcmp(image+0x109306f,"\x49\x8d\x97\x60\x02\x00\x00",7));
    assert(!memcmp(image+0x10930a5,"\x49\x8d\x97\xa0\x02\x00\x00",7) && !memcmp(image+0x10930f2,"\x40\x32\xff",3) && !memcmp(image+0x10935a5,"\x40\x0f\xb6\xc7",4));
    /* ObjectNavOctreeBuilder slot 2 asks GetAssetIntern(.., 0, 1) and sleeps 1 ms until the mesh is loaded. */
    assert(!memcmp(image+0x1096358,"\x41\xb9\x01\x00\x00\x00\x45\x33\xc0\x48\x8b\x0d",12) && 0x1096368u+*(int32_t*)(image+0x1096364)==0x391bd78u);
    assert(image[0x1096368]==0xe8 && 0x109636du+*(int32_t*)(image+0x1096369)==0x14bab90u && !memcmp(image+0x1096343,"\xb9\x01\x00\x00\x00\xff\x15",7));
    /* The octree the content digest reads: 0x50 bytes, origin +0x10, cell size +0x20, cells vector +0x28/+0x30/+0x38 of 12-byte
     * cells (count = bytes * 0x2aaaaaaaaaaaaaab >> 64 >> 1), depth +0x40. */
    assert(!memcmp(image+0x109420f,"\xb9\x50\x00\x00\x00",5) && !memcmp(image+0x109424c,"\x0f\x11\x40\x10",4));
    assert(!memcmp(image+0x1094250,"\xf3\x0f\x11\x48\x20",5) && !memcmp(image+0x1094255,"\x48\x89\x58\x28\x48\x89\x58\x30\x48\x89\x58\x38",12));
    assert(!memcmp(image+0x1094261,"\xc6\x40\x40\x00",4) && !memcmp(image+0x10942b6,"\x48\x8b\x4f\x38\x48\x2b\x4f\x28",8));
    assert(!memcmp(image+0x109426f,"\x48\xbd\xab\xaa\xaa\xaa\xaa\xaa\xaa\x2a",10) && !memcmp(image+0x10942c4,"\x48\xd1\xfa",3));
    /* The busy-set swap is the cache's slot 1. */
    assert(*(uint64_t*)(image+0x2c68fe8)==0x140000000ull+0x10922a0u);
    image[0x1092250+5]^=1;assert(!noct_pins_ok());image[0x1092250+5]^=1;assert(noct_pins_ok());
    /* 'deadline': its sites and blocks; the lookup takes a reference (+0 of the octree) and asks for a build on a miss
     * only when its fifth argument is set ([rdi+8] the object ID, r15b the flag); the octree builder's result is published
     * as is (ID maps: node +0x28; root maps: the slot); the invalidation reads the object ID at +8; builds hold +0x30. */
    int saved_mode=noct_mode;noct_mode=NOCT_DEADLINE;
    for(unsigned i=0;i<NOCT_SITES;i++) assert(noct_site_state(i)==0);
    assert(noct_pins_ok());
    /* Lookup/invalidation rows name their call site by its return address. */
    for(unsigned i=0;i<4;i++) assert(noct_look_returns[i]==noct_sites[12+i].rva+5 && noct_sites[12+i].kind==NOCT_K_GET);
    assert(noct_look_returns[4]==0x7450d3u+5 && noct_look_returns[5]==0x745255u+5 && noct_look_returns[6]==0x753510u+5 && noct_look_returns[7]==0x784691u+5);
    assert(noct_site_of(image+0x53fcff,0)==2 && noct_site_of(image+0x753515,1)==6 && noct_site_of(image+0x75b2f7,1)==8 && noct_site_of(image+1,0)==9);
    assert(!memcmp(image+0x1092450,"\x80\x7c\x24\x60\x00",5) && !memcmp(image+0x1092457,"\x45\x0f\xb6\xc7\x48\x8b\x57\x08",8));
    assert(!memcmp(image+0x109238f,"\xb8\x01\x00\x00\x00\xf0\x0f\xc1\x01",9));
    assert(!memcmp(image+0x1093304,"\x48\x8d\x4d\x80",4) && !memcmp(image+0x109330d,"\x4c\x8b\xe0",3));
    assert(!memcmp(image+0x10933cb,"\x4c\x89\x60\x28",4) && !memcmp(image+0x109341b,"\x4c\x89\x20",3));
    assert(!memcmp(image+0x75b146,"\x48\x8b\x71\x08",4) && !memcmp(image+0x1093045,"\x48\x8d\x59\x30",4));
    /* A root-keyed object whose octree exists builds nothing: 0x1093e60's result (a held reference) is tested and the build returns 1. */
    assert(!memcmp(image+0x10931c5,"\x41\xbc\x01\x00\x00\x00\x48\x83\x38\x00",10) && !memcmp(image+0x10931fc,"\x84\xd2\x0f\x85",4));
    noct_mode=NOCT_AGREED;assert(noct_pins_ok());noct_mode=NOCT_DEADLINE;
    /* Input snapshot: slot 1 of ObjectNavOctreeBuilder (vtable entry 0x2c68f70) reads the builder at +0x10, +0x40 and +0x180 and
     * writes +0x20..+0x4f and the 0x50-byte parts vector at +0x188 (grown by 0x85b760 with &part and &transform); 0x86fc70 takes an
     * object by ID with a reference (count +0x40, state +0x44 == 2 alive); banks follow the thread's TLS +0x314. */
    assert(*(uint64_t*)(image+NOCT_SLOT1_ENTRY)==0x140000000ull+NOCT_SLOT1);
    assert(!memcmp(image+0x109587a,"\x4c\x8b\xe9",3) && !memcmp(image+0x1095926,"\x49\x8b\x4d\x10",4) && !memcmp(image+0x10959f1,"\x49\x8d\x5d\x20",4));
    assert(!memcmp(image+0x1095c37,"\x41\x80\xbd\x80\x01\x00\x00\x00",8) && !memcmp(image+0x1095bb4,"\x49\x8d\x8d\x88\x01\x00\x00",7));
    assert(!memcmp(image+0x1095bf1,"\x48\x83\x41\x08\x50",5) && !memcmp(image+0x1096035,"\x41\x0f\x11\x55\x20",5) && !memcmp(image+0x109605b,"\xf3\x41\x0f\x11\x55\x40",6));
    assert(!memcmp(image+0x1095bf8,"\x4c\x8d\x4c\x24\x50\x4c\x8d\x85\xc8\x01\x00\x00\xe8",13) && 0x1095c09u+*(int32_t*)(image+0x1095c05)==NOCT_APPEND);
    assert(!memcmp(image+0x10c9950,"\x48\x8b\x08\xba\x14\x03\x00\x00",8) && !memcmp(image+0x10c995e,"\x83\x3c\x0a\x00",4));
    assert(!memcmp(image+0x86fcb8,"\x8b\x47\x44\x90",4) && !memcmp(image+0x86fcc6,"\xf0\x0f\xc1\x47\x40",5) && !memcmp(image+0x1093572,"\x48\x8d\x4b\x38",4));
    noct_snapshot=1;assert(noct_pins_ok());image[0x1095a00]^=1;assert(!noct_pins_ok());image[0x1095a00]^=1;assert(noct_pins_ok());noct_snapshot=0;
    image[0x446a8b+5]^=1;assert(!noct_pins_ok());image[0x446a8b+5]^=1;
    image[0x784681]^=1;assert(!noct_pins_ok());image[0x784681]^=1;assert(noct_pins_ok());
    noct_mode=saved_mode;
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("nav octree native pins, thread loop, request sets, creation requests and deferral verified\n");
}
