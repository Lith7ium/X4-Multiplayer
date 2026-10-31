/* native_gate_shape_order: a stand-in child replaced after its file loaded
 * ends at the stand-in's index, the same order a cached build gives; plain
 * appends, other threads and loading frames stay native; mismatches fail
 * closed; rows, call-site swaps and the pinned build. Owned memory only. */
typedef struct {__attribute__((aligned(16))) unsigned char compound[0x80];unsigned char subs[16*SORDER_SUBSHAPE];} test_so_compound;
static uint32_t test_so_bounds_start,test_so_bounds_count;
static unsigned test_so_bounds_calls;
static void test_so_init(test_so_compound *c,const uintptr_t *children,unsigned n) {
    memset(c,0,sizeof(*c));uint64_t count=n;unsigned char *subs=c->subs;memcpy(c->compound+0x50,&count,8);memcpy(c->compound+0x60,&subs,8);
    for(unsigned i=0;i<n;i++) {memcpy(c->subs+i*SORDER_SUBSHAPE,&children[i],8);float x=(float)children[i];memcpy(c->subs+i*SORDER_SUBSHAPE+8,&x,4);}
}
static uintptr_t test_so_child(const test_so_compound *c,unsigned i) {uintptr_t p;memcpy(&p,c->subs+i*SORDER_SUBSHAPE,8);return p;}
/* Jolt's RemoveShape and the engine's append-only AddShape on the synthetic compound. */
static void test_so_remove(void *compound,uint32_t index) {
    unsigned char *c=compound,*subs;uint64_t count;memcpy(&count,c+0x50,8);memcpy(&subs,c+0x60,8);
    memmove(subs+index*SORDER_SUBSHAPE,subs+(index+1)*SORDER_SUBSHAPE,(size_t)(count-1-index)*SORDER_SUBSHAPE);--count;memcpy(c+0x50,&count,8);
}
static uint64_t test_so_add(void *compound,void *position,void *rotation,void *shape,uint64_t a5,uint64_t a6) {
    unsigned char *c=compound,*subs;uint64_t count;memcpy(&count,c+0x50,8);memcpy(&subs,c+0x60,8);(void)rotation;(void)a5;(void)a6;
    memset(subs+count*SORDER_SUBSHAPE,0,SORDER_SUBSHAPE);memcpy(subs+count*SORDER_SUBSHAPE,&shape,8);
    float x=(float)(uintptr_t)shape;memcpy(subs+count*SORDER_SUBSHAPE+8,position?position:&x,4);
    ++count;memcpy(c+0x50,&count,8);return count-1;
}
static void test_so_bounds(void *compound,uint32_t start,uint32_t count) {(void)compound;test_so_bounds_start=start;test_so_bounds_count=count;++test_so_bounds_calls;}
/* A step's system with two request maps (root +8, count +0x28 of +0x6c0 and
 * +0x700): the synthetic pre-pass applies one file per call. */
static __attribute__((aligned(16))) unsigned char test_so_system[0x800],test_so_scene[0x40];
static unsigned test_so_pass_calls,test_so_files;
static void test_so_pass(void *scene) {
    assert(scene==test_so_scene);++test_so_pass_calls;
    if(test_so_files && test_so_files!=UINT32_MAX) --test_so_files;
    uint64_t count=test_so_files?test_so_files:0;void *root=count?(void*)test_so_system:NULL;
    memcpy(test_so_system+0x6c8,&root,8);memcpy(test_so_system+0x6e8,&count,8);
}
/* A file cache (lock at +0, map head at +0x30, one node) and the engine's
 * synchronous load: an asset manager with two entries (types 7 and 8), and a
 * loader thread that puts the file in after a short delay and releases the
 * semaphore (as XLib::AssetLoadThread does after a 0x20 file). */
static __attribute__((aligned(16))) unsigned char test_so_cache[0x100],test_so_node[0x40],test_so_desc[0x40],test_so_entry[2][SORDER_ENTRY_SIZE];
static unsigned char *test_so_manager;static uint64_t test_so_limits[16];
static CRITICAL_SECTION test_so_sync_lock;static HANDLE test_so_done;
static unsigned test_so_request_calls,test_so_queue_calls,test_so_loader_delay,test_so_loader_files=1;
static uint64_t test_so_queue_key=0x0000000700012345ULL;
static void test_so_insert(void) {
    EnterCriticalSection((CRITICAL_SECTION *)test_so_cache);
    unsigned char *node=test_so_node;memcpy(test_so_cache+0x30+8,&node,8);LeaveCriticalSection((CRITICAL_SECTION *)test_so_cache);
}
static DWORD WINAPI test_so_loader(LPVOID unused) {
    (void)unused;Sleep(test_so_loader_delay);if(test_so_loader_files) test_so_insert();
    ReleaseSemaphore(test_so_done,1,NULL);return 0;
}
static HANDLE test_so_thread;
static uint8_t test_so_queue(void *manager,const uint64_t *key,uint32_t flags) {
    assert(manager==test_so_manager && *key==test_so_queue_key && flags==SORDER_SYNC_FLAGS);++test_so_queue_calls;
    assert((DWORD)(uintptr_t)test_so_sync_lock.OwningThread==GetCurrentThreadId()); /* queued under the engine's lock */
    if(test_so_loader_delay==UINT32_MAX) return 1; /* never finishes */
    test_so_thread=CreateThread(NULL,0,test_so_loader,NULL,0,NULL);assert(test_so_thread);return 1;
}
static void test_so_join(void) {WaitForSingleObject(test_so_thread,1000);CloseHandle(test_so_thread);test_so_thread=NULL;}
static uint64_t test_so_request(void *cache,void *desc,void *r8,void *r9,uint64_t a5,uint64_t a6,uint64_t a7) {
    assert(cache==test_so_cache && desc==test_so_desc && r8==(void*)8 && r9==(void*)9 && a5==5 && a6==6 && a7==7);++test_so_request_calls;
    void *root=NULL;memcpy(&root,test_so_cache+0x38,8);return root?0x5ab:0; /* built in place when cached */
}
static uint64_t test_so_notify_update,test_so_notify_activation;
static void test_so_notify(void *iface,void *body_id,void *com,uint64_t update,uint64_t activation) {
    assert(iface==(void*)0x1d0 && body_id==(void*)0x8 && com==(void*)0x40);test_so_notify_update=update;test_so_notify_activation=activation;
}
static void test_shape_order(void) {
    uint64_t saved_issued=stats.issued;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;int saved_boot=boot_mode,saved_phase=boot_phase;
    stats.active=1;boot_phase=3;ui_thread=GetCurrentThreadId();stats.issued=15;
    sorder_installed=1;sorder_failures=0;sorder_rows=0;sorder_moves=sorder_removals=sorder_appends=sorder_native=0;memset(&sorder_cur,0,sizeof(sorder_cur));
    /* Built from cache: hull 0x10 first. Built with a stand-in 0x99 at the hull's place, replaced later. */
    static test_so_compound cached,late;const uintptr_t direct[4]={0x10,0x20,0x30,0x40},stand_in[4]={0x99,0x20,0x30,0x40};
    test_so_init(&cached,direct,4);test_so_init(&late,stand_in,4);
    sorder_remove_with(test_so_remove,late.compound,0);
    assert(sorder_cur.pending && sorder_removals==1);
    float position=16.0f;sorder_add_with(test_so_add,test_so_bounds,late.compound,&position,NULL,(void*)0x10,0,0);
    for(unsigned i=0;i<4;i++) assert(test_so_child(&late,i)==test_so_child(&cached,i));
    assert(!memcmp(late.subs,cached.subs,4*SORDER_SUBSHAPE)); /* poses moved with their shapes */
    assert(test_so_bounds_calls==1 && test_so_bounds_start==0 && test_so_bounds_count==4 && sorder_moves==1 && !sorder_cur.pending);
    char text[160];assert(sorder_row_text(0,text,sizeof(text)) && strstr(text,"tick=15 from=3 to=0 children=4"));
    /* A stand-in that was already last: nothing to move. A new child without a removal: native append. */
    const uintptr_t tail[3]={0x10,0x20,0x99};test_so_compound t;test_so_init(&t,tail,3);
    sorder_remove_with(test_so_remove,t.compound,2);sorder_add_with(test_so_add,test_so_bounds,t.compound,NULL,NULL,(void*)0x30,0,0);
    assert(test_so_child(&t,2)==0x30 && test_so_bounds_calls==1 && sorder_moves==1);
    sorder_add_with(test_so_add,test_so_bounds,t.compound,NULL,NULL,(void*)0x50,0,0);assert(test_so_child(&t,3)==0x50 && sorder_appends==1);
    /* Boot frames after arming move in place too (long-L17); other threads run natively (the original append order). */
    test_so_init(&late,stand_in,4);boot_phase=2;
    sorder_remove_with(test_so_remove,late.compound,0);sorder_add_with(test_so_add,test_so_bounds,late.compound,NULL,NULL,(void*)0x10,0,0);
    assert(test_so_child(&late,0)==0x10 && test_so_child(&late,3)==0x40 && sorder_moves==2 && sorder_boot_moves==1 && !sorder_native);boot_phase=3;
    test_so_init(&late,stand_in,4);ui_thread=GetCurrentThreadId()+1;
    sorder_remove_with(test_so_remove,late.compound,0);sorder_add_with(test_so_add,test_so_bounds,late.compound,NULL,NULL,(void*)0x10,0,0);
    assert(test_so_child(&late,0)==0x20 && test_so_child(&late,3)==0x10 && sorder_native==2 && !sorder_cur.pending);ui_thread=GetCurrentThreadId();
    /* An append for another compound than the removal's, or two removals in a row, fail closed. */
    test_so_init(&late,stand_in,4);sorder_remove_with(test_so_remove,late.compound,0);
    sorder_add_with(test_so_add,test_so_bounds,t.compound,NULL,NULL,(void*)0x60,0,0);
    assert(sorder_failures==1 && !strcmp(sorder_stage,"compound_mismatch") && !sorder_check());sorder_failures=0;
    sorder_remove_with(test_so_remove,late.compound,0);sorder_remove_with(test_so_remove,late.compound,0);
    assert(sorder_failures==1 && !strcmp(sorder_stage,"removal_unpaired"));sorder_failures=0;sorder_cur.pending=0;
    /* Wait mode: the pass repeats until no request waits; place mode never waits; a file that never finishes times out. */
    unsigned char *system=test_so_system;memcpy(test_so_scene+0x20,&system,8);memset(test_so_system,0,sizeof(test_so_system));
    sorder_mode=SORDER_PLACE;test_so_files=3;test_so_pass_calls=0;sorder_prepass_with(test_so_pass,test_so_scene,1000);
    assert(test_so_pass_calls==1 && !sorder_waits);
    sorder_mode=SORDER_WAIT;test_so_files=4;test_so_pass_calls=0;sorder_prepass_with(test_so_pass,test_so_scene,1000);
    assert(test_so_pass_calls==4 && !test_so_files && sorder_waits==1 && sorder_wait_passes==3 && sorder_last_pending==3 && !sorder_failures);
    test_so_pass_calls=0;sorder_prepass_with(test_so_pass,test_so_scene,1000);assert(test_so_pass_calls==1 && sorder_waits==1);
    void *busy=test_so_system;memcpy(test_so_system+0x708,&busy,8); /* the second map */
    test_so_files=UINT32_MAX;sorder_prepass_with(test_so_pass,test_so_scene,20);
    assert(sorder_failures==1 && !strcmp(sorder_stage,"load_timeout") && sorder_waits==2);
    sorder_failures=0;test_so_files=0;memset(test_so_system+0x708,0,8);
    boot_phase=2;test_so_files=5;test_so_pass_calls=0;sorder_prepass_with(test_so_pass,test_so_scene,1000);assert(test_so_pass_calls==1);boot_phase=3;
    char status[1024];assert(sorder_status(status,sizeof(status)) && strstr(status,"mode=wait installed=1 ") && strstr(status," total_moves=2 ") &&
                            strstr(status," waits=2 ") && strstr(status," boot_moves=1 "));
    /* Sync: a missing file goes through the engine's synchronous load (queued with 0x21 under its lock, the
     * loader's semaphore waited for), then the original finds it; cached files, no-load keys, resident
     * assets and keys outside the manager's table pass straight through; a load that never ends times out. */
    InitializeCriticalSection((CRITICAL_SECTION *)test_so_cache);memset(test_so_node,0,sizeof(test_so_node));
    InitializeCriticalSection(&test_so_sync_lock);test_so_done=CreateSemaphoreW(NULL,0,1,NULL);assert(test_so_done);
    test_so_manager=VirtualAlloc(NULL,(SORDER_ENTRY_TABLE+16u)*24u,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(test_so_manager);
    memset(test_so_entry,0,sizeof(test_so_entry));memset(test_so_limits,0,sizeof(test_so_limits));test_so_limits[7]=test_so_limits[8]=0x100000;
    uintptr_t table7=(uintptr_t)test_so_entry[0]-0x12345u*SORDER_ENTRY_SIZE,table8=(uintptr_t)test_so_entry[1]-0x54321u*SORDER_ENTRY_SIZE;
    memcpy(test_so_manager+(SORDER_ENTRY_TABLE+7u)*24u,&table7,8);memcpy(test_so_manager+(SORDER_ENTRY_TABLE+8u)*24u,&table8,8);
    sorder_loader loader={test_so_queue,test_so_manager,&test_so_sync_lock,test_so_done,(const unsigned char *)test_so_limits};
    assert(sorder_entry(&loader,0x0000000700012345ULL)==test_so_entry[0] && sorder_entry(&loader,0x0000000800054321ULL)==test_so_entry[1]);
    assert(!sorder_entry(&loader,0x0000000000012345ULL) && !sorder_entry(&loader,0x0000001000012345ULL) && !sorder_entry(&loader,0x0000000700100000ULL));
    uint32_t low=0x12345;memcpy(test_so_node+0x20,&low,4);test_so_node[0x24]=7;
    uint64_t key=0x0000000700012345ULL;memcpy(test_so_desc+0x28,&key,8);
    sorder_mode=SORDER_SYNC;test_so_loader_delay=15;test_so_request_calls=test_so_queue_calls=0;
    assert(sorder_cached(test_so_cache,0x30,key)==0);
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0x5ab);
    test_so_join();
    assert(test_so_queue_calls==1 && test_so_request_calls==1 && sorder_loads==1 && sorder_requests==1 && sorder_load_us>0 && !sorder_failures);
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0x5ab);
    assert(test_so_queue_calls==1 && sorder_cached_hits==1);
    uint64_t other=0x0000000700012346ULL;memcpy(test_so_desc+0x28,&other,8);assert(sorder_cached(test_so_cache,0x30,other)==0);
    uint64_t lower=0x0000000600012345ULL;assert(sorder_cached(test_so_cache,0x30,lower)==0);memcpy(test_so_desc+0x28,&key,8);
    /* The null key: the original's second key at desc+0x30 is loaded and waited for. */
    uint8_t null_gen=image[0x2f529dc];uint32_t null_low;memcpy(&null_low,image+0x2f529d8,4);
    image[0x2f529dc]=7;uint32_t mine=0x00012345;memcpy(image+0x2f529d8,&mine,4);memset(test_so_cache+0x38,0,8);
    uint64_t alt=0x0000000800054321ULL;memcpy(test_so_desc+0x30,&alt,8);low=0x54321;memcpy(test_so_node+0x20,&low,4);test_so_node[0x24]=8;
    test_so_queue_key=alt;test_so_loader_delay=5;
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0x5ab);
    test_so_join();
    assert(sorder_alt_keys==1 && sorder_loads==2 && test_so_queue_calls==2 && !sorder_failures);
    image[0x2f529dc]=null_gen;memcpy(image+0x2f529d8,&null_low,4);
    /* A no-load key passes through untouched. */
    uint8_t skip_gen=image[0x31beb04];uint32_t skip_low;memcpy(&skip_low,image+0x31beb00,4);
    image[0x31beb04]=7;memcpy(image+0x31beb00,&mine,4);memset(test_so_cache+0x38,0,8);
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0);
    assert(sorder_passed==1 && test_so_queue_calls==2);image[0x31beb04]=skip_gen;memcpy(image+0x31beb00,&skip_low,4);
    low=0x12345;memcpy(test_so_node+0x20,&low,4);test_so_node[0x24]=7;test_so_queue_key=key;
    /* A resident asset (not in the cache) and keys the manager rejects are not queued. */
    void *resident=(void*)0x77;memcpy(test_so_entry[0],&resident,8);
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0);
    assert(sorder_resident==1 && test_so_queue_calls==2);memset(test_so_entry[0],0,8);
    uint64_t typeless=0x0000000000012345ULL,beyond=0x0000000700100000ULL;
    memcpy(test_so_desc+0x28,&typeless,8);sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000);
    memcpy(test_so_desc+0x28,&beyond,8);sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000);
    sorder_loader unmanaged=loader;unmanaged.manager=NULL;memcpy(test_so_desc+0x28,&key,8);
    sorder_request_with(test_so_request,&unmanaged,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000);
    assert(sorder_invalid==3 && test_so_queue_calls==2 && !sorder_failures);
    /* A load that ends without the file in the cache: counted, the original asks as usual. */
    test_so_loader_files=0;test_so_loader_delay=5;
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,1000)==0);
    test_so_join();assert(sorder_unloaded==1 && sorder_loads==2 && test_so_queue_calls==3 && !sorder_failures);test_so_loader_files=1;
    test_so_loader_delay=UINT32_MAX;
    assert(sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,20)==0);
    assert(sorder_failures==1 && !strcmp(sorder_stage,"sync_timeout") && test_so_queue_calls==4);sorder_failures=0;
    sorder_mode=SORDER_WAIT;test_so_request_calls=0;
    sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,20);
    assert(test_so_request_calls==1 && test_so_queue_calls==4); /* not sync: straight to the original */
    sorder_mode=SORDER_SYNC;boot_phase=2;test_so_request_calls=0;
    sorder_request_with(test_so_request,&loader,test_so_cache,test_so_desc,0x28,0x30,(void*)8,(void*)9,5,6,7,20);
    assert(test_so_request_calls==1 && test_so_queue_calls==4);boot_phase=3; /* boot frames: no synchronous loads */
    CloseHandle(test_so_done);DeleteCriticalSection(&test_so_sync_lock);VirtualFree(test_so_manager,0,MEM_RELEASE);test_so_manager=NULL;
    /* Every mode: the apply's NotifyShapeChanged always asks for new mass properties (live frames only). */
    sorder_notify_with(test_so_notify,(void*)0x1d0,(void*)0x8,(void*)0x40,0xab00,1);
    assert(test_so_notify_update==0xab01 && test_so_notify_activation==1 && sorder_mass_forced==1);
    sorder_notify_with(test_so_notify,(void*)0x1d0,(void*)0x8,(void*)0x40,1,0);assert(test_so_notify_update==1 && sorder_mass_forced==1);
    boot_phase=2;sorder_notify_with(test_so_notify,(void*)0x1d0,(void*)0x8,(void*)0x40,0,1); /* boot frames too (long-L17) */
    assert(test_so_notify_update==1 && sorder_mass_forced==2 && sorder_boot_forced==1);boot_phase=3;
    ui_thread=GetCurrentThreadId()+1;sorder_notify_with(test_so_notify,(void*)0x1d0,(void*)0x8,(void*)0x40,0,1);
    assert(test_so_notify_update==0 && sorder_mass_forced==2);ui_thread=GetCurrentThreadId();
    DeleteCriticalSection((CRITICAL_SECTION *)test_so_cache);
    sorder_requests=sorder_cached_hits=sorder_loads=sorder_load_us=sorder_load_max_us=sorder_passed=sorder_alt_keys=sorder_mass_forced=0;
    sorder_resident=sorder_invalid=sorder_unloaded=sorder_boot_moves=sorder_boot_forced=0;
    sorder_mode=SORDER_OFF;sorder_waits=sorder_wait_passes=sorder_wait_us=sorder_wait_max_us=sorder_last_pending=0;
    sorder_installed=0;
    /* Arming refusals on the zero image. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    assert(!sorder_arm(SORDER_WAIT) && !strcmp(sorder_stage,"site") && !sorder_installed && sorder_mode==SORDER_OFF);
    assert(!sorder_arm(SORDER_OFF) && !sorder_arm(SORDER_SYNC+1));
    stats.issued=1;assert(!sorder_arm(SORDER_PLACE));stats.issued=0;boot_phase=2;assert(!sorder_arm(SORDER_PLACE));boot_phase=0;
    ui_thread=GetCurrentThreadId()+1;assert(!sorder_arm(SORDER_PLACE));ui_thread=GetCurrentThreadId();
    /* Call sites: swap, ownership, restore (the plain image's original calls). */
    for(unsigned i=0;i<SORDER_CALLS;i++) {
        unsigned char call[5]={0xe8};int32_t rel=(int32_t)(sorder_calls[i].target-(sorder_calls[i].rva+5));memcpy(call+1,&rel,4);
        memcpy(image+sorder_calls[i].rva,call,5);
    }
    if(!sorder_stubs) sorder_stubs=sorder_alloc_near(SORDER_STUB_SIZE);
    assert(sorder_stubs && sorder_write_stubs() && sorder_stubs_owned());
    for(unsigned i=0;i<SORDER_CALLS;i++) {
        assert(sorder_call_state(i)==0 && sorder_cas(i,sorder_rel_to(i,image+sorder_calls[i].target),sorder_rel_to(i,sorder_stub(i))));
        sorder_done|=1u<<i;assert(sorder_call_state(i)==1);
    }
    sorder_installed=1;assert(sorder_owned());
    image[sorder_calls[0].rva+3]^=1;assert(!sorder_owned());image[sorder_calls[0].rva+3]^=1;assert(sorder_owned());
    sorder_restore();assert(!sorder_installed && sorder_restored && !sorder_done);
    for(unsigned i=0;i<SORDER_CALLS;i++) {assert(sorder_call_state(i)==0);memset(image+sorder_calls[i].rva,0,5);}
    sorder_restored=0;sorder_stage="idle";sorder_rows=0;sorder_moves=sorder_removals=sorder_appends=sorder_native=0;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("shape order in-place replacement/native/fail-closed/swap tests PASS\n");
}
static void test_shape_order_native_image(const char *path) {
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
    assert(sorder_pins_ok());sorder_mode=SORDER_WAIT;assert(sorder_pins_ok());sorder_mode=SORDER_SYNC;assert(sorder_pins_ok());
    /* The builder's lookups: cache *0x6cf1518, shape map +0x30 with the key at desc+0x28, mesh map +0x70 with desc+0x20;
     * a miss asks GetAssetIntern(*0x391bd78, key, 0, 1). */
    assert(0xc0da1u+*(int32_t*)(image+0xc0d9d)==SORDER_CACHE);
    assert(!memcmp(image+0xb1da5,"\x4d\x8d\x4f\x30",4) && !memcmp(image+0xb20c6,"\x4d\x8d\x4f\x70",4));
    assert(!memcmp(image+0xb1e4c,"\x41\xb9\x01\x00\x00\x00\x45\x33\xc0\x48\x8b\x56\x28\x48\x8b\x0d",16) && 0xb1e60u+*(int32_t*)(image+0xb1e5c)==SORDER_ASSET_MANAGER);
    assert(!memcmp(image+0xb217c,"\x41\xb9\x01\x00\x00\x00\x45\x33\xc0\x48\x8b\x56\x20\x48\x8b\x0d",16) && 0xb2190u+*(int32_t*)(image+0xb218c)==SORDER_ASSET_MANAGER);
    assert(image[0xb1e60]==0xe8 && 0xb1e65u+*(int32_t*)(image+0xb1e61)==SORDER_ASSET);
    /* GetAssetIntern queues and returns NULL during play (r8b=0, loading flag clear); its r8b=1 branch is the
     * synchronous load sorder_request uses: lock 0x6b60ec0, queue 0x14bb1d0 with |0x20, wait on *0x6b60ee8. */
    assert(!memcmp(image+0x14bad02,"\x80\xbd\x60\x46\x10\x00\x00",7) && !memcmp(image+0x14bad0b,"\x45\x84\xff",3) && !memcmp(image+0x14bad10,"\x83\xcf\x40",3));
    assert(!memcmp(image+0x14babdb,"\x4c\x8d\x05",3) && 0x14babe2u+*(int32_t*)(image+0x14babde)==SORDER_ASSET_LIMITS);
    assert(!memcmp(image+0x14babf1,"\x48\x05\x68\xad\x00\x00",6) && !memcmp(image+0x14babfb,"\x48\x6b\xf2\x58",4));
    assert(!memcmp(image+0x14bad57,"\x48\x8d\x1d",3) && 0x14bad5eu+*(int32_t*)(image+0x14bad5a)==SORDER_SYNC_LOCK);
    assert(!memcmp(image+0x14bad6d,"\x83\xcf\x20",3) && image[0x14bad7b]==0xe8 && 0x14bad80u+*(int32_t*)(image+0x14bad7c)==SORDER_QUEUE);
    assert(!memcmp(image+0x14bad85,"\x48\x8b\x0d",3) && 0x14bad8cu+*(int32_t*)(image+0x14bad88)==SORDER_SYNC_DONE);
    /* The loader releases the semaphore after a 0x20 file; the semaphore is CreateSemaphoreW(0, 0, 1, 0), the lock set up at startup. */
    assert(!memcmp(image+0x14dc8c5,"\x40\xf6\xc6\x20",4) && 0x14dc8d9u+*(int32_t*)(image+0x14dc8d5)==SORDER_SYNC_DONE);
    assert(!memcmp(image+0x933c4,"\x45\x33\xc9\x33\xd2\x33\xc9\x45\x8d\x41\x01\xff\x15",13) && 0x933e3u+*(int32_t*)(image+0x933df)==SORDER_SYNC_DONE);
    assert(0x933a0u+*(int32_t*)(image+0x9339c)==SORDER_SYNC_LOCK);
    /* The per-type table sizes are read-only data (types 7 and 8: 4000 entries). */
    assert(*(uint64_t*)(image+SORDER_ASSET_LIMITS+7*8)==0xfa0 && *(uint64_t*)(image+SORDER_ASSET_LIMITS+8*8)==0xfa0);
    image[0xb1d70]^=1;assert(!sorder_pins_ok());image[0xb1d70]^=1;sorder_mode=SORDER_WAIT;
    /* The null key's second key at desc+0x30 (both lookups), loaded by 0xf522d0 through the same GetAssetIntern. */
    assert(!memcmp(image+0xb1d8e,"\x48\x8b\x5a\x30",4) && !memcmp(image+0xb20af,"\x48\x8b\x5a\x30",4));
    assert(image[0xf5233a]==0xe8 && 0xf5233fu+*(int32_t*)(image+0xf5233b)==SORDER_ASSET && !memcmp(image+0xf5231f,"\x48\x8b\x51\x30",4));
    /* The apply asks for new mass properties only when record +0x3a and +0x39 are 0; NotifyShapeChanged then calls
     * SetMassProperties(motion, allowed DOFs, shape mass properties) when the body has motion properties. */
    assert(!memcmp(image+0xc7ec4,"\x0f\xb6\x43\x3a",4) && !memcmp(image+0xc7ed9,"\x38\x43\x39",3) && !memcmp(image+0xc7ede,"\x41\xb1\x01",3));
    assert(!memcmp(image+0x140628b,"\x84\xdb",2) && !memcmp(image+0x140628f,"\x48\x8b\x5f\x48\x48\x85\xdb",7) && image[0x14062b4]==0xe8);
    for(unsigned i=0;i<SORDER_CALLS;i++) assert(sorder_call_state(i)==0);
    /* The ready-list producers: request map +0x6c0 -> ready list +0x740, +0x700 -> +0x1750, under system+0x698. */
    assert(!memcmp(image+0xbdaf4,"\x4c\x8b\x5f\x20\x4d\x8d\x93\xc0\x06\x00\x00",11) && !memcmp(image+0xbdb66,"\x49\x8d\x8b\x40\x07\x00\x00",7));
    assert(!memcmp(image+0xbdbd4,"\x4c\x8b\x5f\x20\x4d\x8d\x93\x00\x07\x00\x00",11) && *(uint32_t*)(image+0xbdade+3)==0x698);
    assert(*(uint32_t*)(image+0xbdbbe+3)==0x698 && *(uint32_t*)(image+0xbdb69)==0x740);
    image[0xbdc90]^=1;assert(!sorder_pins_ok());image[0xbdc90]^=1;sorder_mode=SORDER_OFF;assert(sorder_pins_ok());
    /* The apply removes child edi of the compound (r15), then appends the new child to the same compound. */
    assert(!memcmp(image+0xc803f,"\x8b\xd7\x49\x8b\xcf",5) && !memcmp(image+0xc8284,"\x49\x8b\xcf",3));
    /* Pre-pass 0xbdc80 hands ready requests to 0xc7e30 (two lists). */
    assert(0xbde8au+*(int32_t*)(image+0xbde86)==0xc7e30u && 0xbe2cau+*(int32_t*)(image+0xbe2c6)==0xc7e30u);
    /* RemoveShape and the append both finish with CalculateSubShapeBounds. */
    assert(image[0x140cf0e]==0xe9 && 0x140cf13u+*(int32_t*)(image+0x140cf0f)==SORDER_BOUNDS);
    assert(image[0x140ce1c]==0xe8 && 0x140ce21u+*(int32_t*)(image+0x140ce1d)==SORDER_BOUNDS);
    image[0x140cb20]^=1;assert(!sorder_pins_ok());image[0x140cb20]^=1;
    image[sorder_calls[1].rva+1]^=1;assert(sorder_call_state(1)<0 && !sorder_pins_ok());image[sorder_calls[1].rva+1]^=1;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("shape order native pins, apply sequence, pre-pass callers and bounds calls verified");
}
