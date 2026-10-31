/* native_gate_mass_traffic: the counted TryEnter and frame-start wait, the
 * join and RNG point (sync and observe), patch CAS/restore, refusals, the
 * status bounds and the pinned build. */
static __declspec(align(16)) unsigned char test_mt_task[0x100];
static uint64_t test_mt_slot[0x400/8];
static volatile LONG test_mt_held,test_mt_go;
static DWORD WINAPI test_mt_holder(LPVOID unused) {
    (void)unused;CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+MT_LOCK);
    EnterCriticalSection(lock);InterlockedExchange(&test_mt_held,1);
    while(!InterlockedCompareExchange(&test_mt_go,0,0)) Sleep(1);
    LeaveCriticalSection(lock);return 0;
}
static DWORD WINAPI test_mt_finisher(LPVOID unused) {
    (void)unused;Sleep(30);CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(test_mt_task+MT_TASK_LOCK);
    EnterCriticalSection(lock);test_mt_task[MT_READY]=1;LeaveCriticalSection(lock);return 0;
}
static DWORD WINAPI test_mt_spin(LPVOID stop) {
    while(!InterlockedCompareExchange((volatile LONG*)stop,0,0)) YieldProcessor();
    return 0;
}
static DWORD WINAPI test_mt_region_late(LPVOID task) {
    Sleep(1300);CRITICAL_SECTION *lock=(CRITICAL_SECTION*)((unsigned char*)task+MT_REGION_LOCK);
    EnterCriticalSection(lock);((unsigned char*)task)[MT_REGION_READY]=1;LeaveCriticalSection(lock);return 0;
}
static void test_mt_reset(void) {
    mt_installed=mt_restored=mt_patched=0;mt_active=0;mt_failures=0;mt_stage="idle";
    mt_worker_waits=mt_worker_wakes=mt_try_total=mt_try_failed=mt_frame_try=mt_frame_failed=0;
    mt_stride=1;mt_stride_skips=0;mt_frame=UINT64_MAX;mt_first_skip=0;mt_pass_reset(&mt_traffic);mt_pass_reset(&mt_region);memset(mt_site_done,0,sizeof(mt_site_done));
}
static void test_mt_write_sites(void) {
    for(unsigned i=0;i<MT_SITES;i++) {
        unsigned char *p=image+mt_sites[i].rva;p[0]=0xff;p[1]=0x15;
        int32_t d=mt_disp_to(mt_sites[i].rva,image+mt_sites[i].iat);memcpy(p+2,&d,4);
    }
}
static void test_mass_traffic(void) {
    uint64_t saved_issued=stats.issued,saved_completed=stats.completed;DWORD saved_ui=ui_thread;uint32_t saved_active=stats.active;
    int saved_boot=boot_mode,saved_phase=boot_phase;unsigned char *saved_slot=mt_traffic.slot;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+MT_LOCK);InitializeCriticalSection(lock);
    test_mt_reset();ui_thread=GetCurrentThreadId();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;
    /* Arm refusals: inactive, wrong thread, after the boot, bad mode; a zero image fails the pins. */
    stats.active=0;assert(!mt_arm(MT_SYNC));stats.active=1;
    ui_thread=GetCurrentThreadId()+1;assert(!mt_arm(MT_SYNC));ui_thread=GetCurrentThreadId();
    boot_phase=1;assert(!mt_arm(MT_SYNC));boot_phase=0;stats.issued=1;assert(!mt_arm(MT_OBSERVE));stats.issued=0;
    assert(!mt_arm(MT_OFF) && !mt_arm(7));
    assert(!mt_arm(MT_SYNC) && !strcmp(mt_stage,"pins") && !mt_installed && !mt_active);
    /* TryEnter: counted only while active and admitted, on g_MassTrafficLock only. */
    boot_phase=3;stats.issued=7;
    assert(mt_try_enter(lock) && !mt_try_total);LeaveCriticalSection(lock);
    mt_active=1;mt_traffic.mode=MT_OBSERVE;
    assert(mt_try_enter(lock) && mt_try_total==1 && mt_frame_try==1 && mt_frame==7 && !mt_try_failed);LeaveCriticalSection(lock);
    CRITICAL_SECTION other;InitializeCriticalSection(&other);assert(mt_try_enter(&other) && mt_try_total==1);LeaveCriticalSection(&other);
    boot_phase=2;assert(mt_try_enter(lock) && mt_try_total==1);LeaveCriticalSection(lock);boot_phase=3;
    /* Held by another thread (the pass): skipped, counted; observe never fails. */
    test_mt_held=test_mt_go=0;HANDLE holder=CreateThread(NULL,0,test_mt_holder,NULL,0,NULL);assert(holder);
    while(!InterlockedCompareExchange(&test_mt_held,0,0)) Sleep(1);
    assert(!mt_try_enter(lock) && mt_try_failed==1 && mt_frame_failed==1 && mt_first_skip==7 && !mt_failures);
    /* Sync: before the first join a skip is legitimate (load-time pass), after it fails closed. */
    mt_traffic.mode=MT_SYNC;assert(!mt_try_enter(lock) && !mt_failures);
    mt_traffic.joins=1;assert(!mt_try_enter(lock) && mt_failures==1 && !strcmp(mt_stage,"skipped") && !mt_check());
    InterlockedExchange(&test_mt_go,1);WaitForSingleObject(holder,5000);CloseHandle(holder);
    stats.issued=8;assert(mt_try_enter(lock) && mt_frame==8 && mt_frame_try==1 && !mt_frame_failed && mt_try_total==5);LeaveCriticalSection(lock);
    /* The frame-start wait: only g_semSimStart is counted. */
    HANDLE start=CreateEventW(NULL,TRUE,TRUE,NULL),unrelated=CreateEventW(NULL,TRUE,TRUE,NULL);assert(start && unrelated);
    *(HANDLE*)(image+MT_SEM_START)=start;
    assert(mt_wait(start,0)==WAIT_OBJECT_0 && mt_worker_waits==1 && mt_worker_wakes==1);
    assert(mt_wait(unrelated,0)==WAIT_OBJECT_0 && mt_worker_waits==1);
    /* Sync join. No task: nothing to join. */
    test_mt_reset();mt_active=1;mt_traffic.mode=MT_SYNC;stats.issued=10;
    *(void**)(image+MT_TASK)=NULL;assert(mt_join(10,50) && mt_traffic.absent==1);
    memset(test_mt_task,0,sizeof(test_mt_task));InitializeCriticalSection((CRITICAL_SECTION*)(test_mt_task+MT_TASK_LOCK));
    *(void**)(image+MT_TASK)=test_mt_task;mt_traffic.slot=(unsigned char*)test_mt_slot;memset(test_mt_slot,0,sizeof(test_mt_slot));
    /* The MoveWorker has not reached its frame-start wait: fail closed. */
    assert(!mt_join(10,30) && !strcmp(mt_stage,"worker") && mt_failures==1);mt_failures=0;
    /* Worker parked, pass done: joined, the next pass seeded with the completed tick's key. */
    mt_worker_waits=3;mt_worker_wakes=2;test_mt_task[MT_READY]=1;
    assert(mt_join(10,1000) && mt_traffic.joins==1 && !mt_traffic.busy && mt_traffic.seeds==1 && mt_traffic.seeded && !mt_traffic.draws);
    assert(test_mt_slot[0x318/8]==lockstep_seed(MT_DOMAIN,10,0,0,0,0) && mt_traffic.seed==test_mt_slot[0x318/8]);
    /* A running pass is waited for; its draws are counted from our seed. */
    uint64_t w=mt_traffic.seed;for(unsigned i=0;i<5;i++) w=render_rng_step(w);test_mt_slot[0x318/8]=w;
    test_mt_task[MT_READY]=0;HANDLE finisher=CreateThread(NULL,0,test_mt_finisher,NULL,0,NULL);assert(finisher);
    assert(mt_join(11,5000) && mt_traffic.joins==2 && mt_traffic.busy==1 && mt_traffic.draws==5 && mt_traffic.total_draws==5 && mt_traffic.word==w);
    WaitForSingleObject(finisher,5000);CloseHandle(finisher);
    assert(test_mt_slot[0x318/8]==lockstep_seed(MT_DOMAIN,11,0,0,0,0) && mt_traffic.seeds==2 && mt_traffic.total_wait_us>=mt_traffic.wait_us);
    /* A word the generator does not reach from the seed: unknown draws, still joined. */
    test_mt_slot[0x318/8]=0x1234;assert(mt_join(12,1000) && mt_traffic.draws_unknown==1 && mt_traffic.total_draws==5);
    /* The pass never completes: fail closed. */
    test_mt_task[MT_READY]=0;assert(!mt_join(13,30) && !strcmp(mt_stage,"pass"));mt_failures=0;test_mt_task[MT_READY]=1;
    /* No MassTraffic thread: sync fails closed, observe only reports. */
    mt_traffic.slot=NULL;
    {
        unsigned char *found=mt_find_slot(L"MassTrafficUpdateTask",NULL);assert(!found); /* the test process has no such thread */
        assert(!mt_join(14,1000) && !strcmp(mt_stage,"thread"));mt_failures=0;
    }
    /* Observe: never waits; busy or unknown are counted, words sampled when idle. */
    test_mt_reset();mt_active=1;mt_traffic.mode=MT_OBSERVE;mt_traffic.slot=(unsigned char*)test_mt_slot;
    mt_worker_waits=mt_worker_wakes=4;assert(mt_join(20,5000) && mt_traffic.unknown==1 && !mt_traffic.joins);
    mt_worker_waits=5;test_mt_task[MT_READY]=0;assert(mt_join(21,5000) && mt_traffic.busy==1);
    test_mt_task[MT_READY]=1;test_mt_slot[0x318/8]=0x77;assert(mt_join(22,5000) && mt_traffic.sampled && mt_traffic.word==0x77 && !mt_traffic.draws && !mt_traffic.seeds);
    w=0x77;for(unsigned i=0;i<3;i++) w=render_rng_step(w);test_mt_slot[0x318/8]=w;
    assert(mt_join(23,5000) && mt_traffic.draws==3 && test_mt_slot[0x318/8]==w); /* observe never writes the word */
    mt_active=0;assert(mt_join(24,5000) && mt_traffic.draws==3);
    /* Region pass: its own task, lock +0x118 and ready byte +0x140, its own thread word and key. */
    test_mt_reset();mt_active=1;mt_region.mode=MT_SYNC;mt_traffic.mode=MT_OFF;
    static __declspec(align(16)) unsigned char region_task[0x200];static uint64_t region_slot[0x400/8];
    memset(region_task,0,sizeof(region_task));memset(region_slot,0,sizeof(region_slot));
    InitializeCriticalSection((CRITICAL_SECTION*)(region_task+MT_REGION_LOCK));
    *(void**)(image+MT_REGION_TASK)=region_task;mt_region.slot=(unsigned char*)region_slot;
    mt_worker_waits=mt_worker_wakes=7;assert(!mt_join(30,30) && !strcmp(mt_stage,"worker"));mt_failures=0;
    mt_worker_waits=8;region_task[MT_REGION_READY]=1;
    assert(mt_join(30,1000) && mt_region.joins==1 && mt_region.seeds==1 && region_slot[0x318/8]==lockstep_seed(MT_REGION_DOMAIN,30,0,0,0,0));
    assert(!mt_traffic.joins && !mt_traffic.absent); /* traffic off */
    w=mt_region.seed;for(unsigned i=0;i<9;i++) w=render_rng_step(w);region_slot[0x318/8]=w;
    region_task[MT_REGION_READY]=0;mt_region.timeout_ms=30;assert(!mt_join(31,30) && !strcmp(mt_stage,"region_pass"));
    mt_region.timeout_ms=MT_REGION_TIMEOUT_MS;mt_failures=0;
    region_task[MT_REGION_READY]=1;assert(mt_join(31,1000) && mt_region.draws==9 && mt_region.joins==2);
    /* Both passes: traffic first, then region; a missing region task is only counted. */
    mt_traffic.mode=MT_SYNC;mt_traffic.slot=(unsigned char*)test_mt_slot;test_mt_task[MT_READY]=1;
    assert(mt_join(32,1000) && mt_traffic.joins==1 && mt_region.joins==3);
    *(void**)(image+MT_REGION_TASK)=NULL;assert(mt_join(33,1000) && mt_region.absent==1 && mt_traffic.joins==2);
    /* Observe region: busy counted without waiting, no seed. */
    *(void**)(image+MT_REGION_TASK)=region_task;mt_region.mode=MT_OBSERVE;region_task[MT_REGION_READY]=0;
    assert(mt_join(34,5000) && mt_region.busy==1 && mt_region.seeds==3);
    mt_region.slot=NULL;
    {DWORD id=0;unsigned char *found=mt_find_slot(L"RegionManager",&id);assert(!found && !id);}
    mt_region.mode=MT_SYNC;region_task[MT_REGION_READY]=1;assert(!mt_join(35,1000) && !strcmp(mt_stage,"region_thread"));mt_failures=0;
    DeleteCriticalSection((CRITICAL_SECTION*)(region_task+MT_REGION_LOCK));*(void**)(image+MT_REGION_TASK)=NULL;
    /* A slow pass is sampled once after 1 s: its thread's RIP and image code addresses on its stack. */
    {
        static volatile LONG spin_stop;spin_stop=0;
        HANDLE spinner=CreateThread(NULL,0,test_mt_spin,(LPVOID)&spin_stop,0,NULL);assert(spinner);
        mt_pass_reset(&mt_region);mt_region.mode=MT_SYNC;mt_region.slot=(unsigned char*)region_slot;mt_region.thread_id=GetThreadId(spinner);
        *(void**)(image+MT_REGION_TASK)=region_task;InitializeCriticalSection((CRITICAL_SECTION*)(region_task+MT_REGION_LOCK));
        region_task[MT_REGION_READY]=0;mt_traffic.mode=MT_OFF;
        HANDLE late=CreateThread(NULL,0,test_mt_region_late,region_task,0,NULL);assert(late);
        assert(mt_join(36,5000) && mt_region.stalls==1 && mt_region.stall_ms>=MT_STALL_MS && mt_region.stall_tick==stats.issued &&
               mt_region.max_wait_us>=1000000);
        WaitForSingleObject(late,5000);CloseHandle(late);
        char text[512];assert(mt_pass_text(&mt_region,text,sizeof(text))>0 && strstr(text," stalls=1 stall="));
        /* A pass that never completes is sampled again and fails closed after the (shortened) limit. */
        region_task[MT_REGION_READY]=0;mt_region.timeout_ms=1500;
        assert(!mt_join(37,1500) && !strcmp(mt_stage,"region_pass") && mt_region.stalls==3);
        mt_region.timeout_ms=MT_REGION_TIMEOUT_MS;mt_failures=0;
        InterlockedExchange(&spin_stop,1);WaitForSingleObject(spinner,5000);CloseHandle(spinner);
        DeleteCriticalSection((CRITICAL_SECTION*)(region_task+MT_REGION_LOCK));*(void**)(image+MT_REGION_TASK)=NULL;
    }
    /* Stride 3: the kick of the frame after tick T passes unless T+1 is on the stride; the byte
     * is held at 0 only between two joins and comes back at the join and at the restore. */
    {
        test_mt_reset();mt_active=1;mt_traffic.mode=MT_SYNC;mt_traffic.slot=(unsigned char*)test_mt_slot;
        *(void**)(image+MT_TASK)=test_mt_task;test_mt_task[MT_READY]=1;mt_worker_waits=1;mt_worker_wakes=0;
        DWORD ui=ui_thread;uint64_t saved_stride_issued=stats.issued;stats.issued=0;
        stats.active=0;assert(!mt_stride_arm(3));stats.active=1;
        ui_thread=GetCurrentThreadId()+1;assert(!mt_stride_arm(3));ui_thread=ui;
        assert(!mt_stride_arm(0) && !mt_stride_arm(MT_STRIDE_MAX+1));
        mt_traffic.mode=MT_OBSERVE;assert(!mt_stride_arm(3));mt_traffic.mode=MT_SYNC;
        assert(mt_stride_arm(3) && mt_stride==3 && mt_stride_arm(3) && !mt_stride_arm(2));
        stats.issued=saved_stride_issued;
        unsigned char expect[]={0,0,1,0,0,1,0}; /* ready after the join at tick 30+i: kick only before 33, 36 */
        for(unsigned i=0;i<7;i++) {
            assert(mt_join(30+i,1000));
            assert(test_mt_task[MT_READY]==expect[i] && mt_traffic.held==!expect[i]);
        }
        assert(mt_traffic.joins==7 && mt_stride_skips==4);
        {char text[1024];assert(mt_status(text,sizeof(text))>0 && strstr(text," stride=3 stride_skips=4"));}
        /* A real kick at the end of a frame: the join then waits for the pass. */
        mt_join(37,1000);assert(test_mt_task[MT_READY]==0 && mt_traffic.held);
        /* stopped while held: the byte comes back */
        mt_restore();assert(test_mt_task[MT_READY]==1 && !mt_traffic.held);
        /* Both passes in phase, or the join would still cost the longer one on every tick. */
        {
            static __declspec(align(16)) unsigned char stride_region[0x200];static uint64_t stride_slot[0x80];
            memset(stride_region,0,sizeof(stride_region));InitializeCriticalSection((CRITICAL_SECTION*)(stride_region+MT_REGION_LOCK));
            *(void**)(image+MT_REGION_TASK)=stride_region;stride_region[MT_REGION_READY]=1;
            mt_active=1;mt_region.mode=MT_SYNC;mt_region.slot=(unsigned char*)stride_slot;mt_stride_skips=0;
            for(unsigned i=0;i<6;i++) {
                assert(mt_join(40+i,1000));
                int on=(41+i)%3==0;
                assert(test_mt_task[MT_READY]==on && stride_region[MT_REGION_READY]==on && mt_traffic.held==!on && mt_region.held==!on);
            }
            assert(mt_stride_skips==3); /* each held frame once, not per pass */
            mt_restore();assert(test_mt_task[MT_READY]==1 && stride_region[MT_REGION_READY]==1 && !mt_region.held);
            DeleteCriticalSection((CRITICAL_SECTION*)(stride_region+MT_REGION_LOCK));*(void**)(image+MT_REGION_TASK)=NULL;
            /* Region alone is enough to arm it; traffic need not be synced. */
            test_mt_reset();stats.issued=0;mt_region.mode=MT_SYNC;assert(mt_stride_arm(2));
        }
        stats.issued=saved_stride_issued;
        *(void**)(image+MT_TASK)=NULL;test_mt_reset();
    }
    /* Arming: a pass reruns identically; the shared patches need the pins (zero image). */
    test_mt_reset();stats.active=1;boot_phase=0;stats.issued=0;
    assert(!mt_region_arm(MT_SYNC) && !strcmp(mt_stage,"pins") && mt_region.mode==MT_OFF && !mt_installed);
    mt_installed=1;assert(mt_region_arm(MT_OBSERVE) && mt_region.mode==MT_OBSERVE && mt_region_arm(MT_OBSERVE) && !mt_region_arm(MT_SYNC));
    assert(mt_arm(MT_SYNC) && mt_traffic.mode==MT_SYNC);mt_installed=0;boot_phase=3;stats.issued=8;
    /* Patches: both displacements to our page slots by CAS, ownership, foreign value, restore. */
    test_mt_reset();test_mt_write_sites();
    if(!mt_page) mt_page=mt_page_near();assert(mt_page);
    void *entries[MT_SITES]={(void*)mt_try_enter,(void*)mt_wait};memcpy(mt_page,entries,sizeof(entries));
    for(unsigned i=0;i<MT_SITES;i++) assert(mt_site_state(i)==0 && ((mt_sites[i].rva+2)&15u)<=12u);
    assert(mt_install_patches() && mt_site_state(0)==1 && mt_site_state(1)==1);
    mt_patched=mt_installed=1;assert(mt_owned());
    *(void**)mt_page=NULL;assert(!mt_owned());*(void**)mt_page=(void*)mt_try_enter;
    int32_t foreign=0x01020304;memcpy(image+MT_WAIT_SITE+2,&foreign,4);assert(mt_site_state(1)==-1 && !mt_owned());
    mt_restore();assert(!mt_restored && mt_installed && mt_site_state(0)==0); /* the foreign site stays marked */
    int32_t ours=mt_disp_to(MT_WAIT_SITE,mt_page+8);memcpy(image+MT_WAIT_SITE+2,&ours,4);
    mt_restore();assert(mt_restored && !mt_installed && mt_site_state(1)==0 && !mt_active);
    /* finish() restores through mt_restore. */
    test_mt_reset();assert(mt_install_patches());mt_patched=mt_installed=1;mt_active=1;
    uint32_t saved_reason=stats.reason;stats.restored=0;finish(MT_STOP_REASON);
    assert(stats.reason==MT_STOP_REASON && mt_restored && mt_site_state(0)==0 && mt_site_state(1)==0 && !mt_active);
    /* Status bounds at extreme values. */
    mt_traffic.mode=MT_OBSERVE;mt_region.mode=MT_SYNC;mt_frame=mt_first_skip=UINT64_MAX;
    for(unsigned k=0;k<2;k++) {
        mt_pass *q=k?&mt_region:&mt_traffic;q->joins=q->busy=q->unknown=q->absent=UINT64_MAX;
        q->wait_us=q->max_wait_us=q->total_wait_us=q->seeds=q->draws=q->total_draws=q->draws_unknown=q->word=UINT64_MAX-1;
    }
    mt_frame_try=mt_frame_failed=mt_try_total=mt_try_failed=mt_worker_waits=mt_worker_wakes=mt_failures=INT64_MAX;mt_stage="block_changed";
    char status[1024];int n=mt_region_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE REGION_PASS tick=18446744073709551615 \n")<2048 && !strncmp(status,"mode=sync ",10) && strstr(status," camera=- "));
    n=mt_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE MASS_TRAFFIC tick=18446744073709551615 \n")<2048 && strstr(status," camera=- "));
    /* The camera sample: mode and the hash of the local PosRot. */
    static __declspec(align(16)) unsigned char universe[0x400],camera[0x900];memset(universe,0,sizeof(universe));memset(camera,0,sizeof(camera));
    *(void**)(image+0x3d198f8)=universe;*(void**)(universe+0x3d0)=camera;*(uint32_t*)(camera+0x868)=4294967295u;
    uint32_t mode;uint64_t h0=mt_camera_hash(&mode);camera[0x5cf]=1;uint64_t h1=mt_camera_hash(&mode);camera[0x5d0]=1;
    assert(h0 && h1 && h0!=h1 && mt_camera_hash(&mode)==h1 && mode==4294967295u);
    n=mt_status(status,sizeof(status));
    assert(n>0 && n+strlen("[X4LAN] NATIVE_GATE MASS_TRAFFIC tick=18446744073709551615 \n")<2048 && strstr(status," camera=4294967295:"));
    *(void**)(image+0x3d198f8)=NULL;
    test_mt_reset();*(void**)(image+MT_TASK)=NULL;*(HANDLE*)(image+MT_SEM_START)=NULL;CloseHandle(start);CloseHandle(unrelated);
    for(unsigned i=0;i<MT_SITES;i++) memset(image+mt_sites[i].rva,0,6);
    DeleteCriticalSection(&other);DeleteCriticalSection((CRITICAL_SECTION*)(test_mt_task+MT_TASK_LOCK));DeleteCriticalSection(lock);
    memset(image+MT_LOCK,0,sizeof(CRITICAL_SECTION));mt_traffic.slot=saved_slot;
    stats.issued=saved_issued;stats.completed=saved_completed;ui_thread=saved_ui;stats.active=saved_active;stats.reason=saved_reason;stats.restored=0;
    boot_mode=saved_boot;boot_phase=saved_phase;
    printf("mass traffic try/wait counting, sync/observe join, RNG point, patch/restore/finish and status tests PASS\n");
}
static void test_mass_traffic_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image,*prior_page=mt_page;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        assert(section.VirtualAddress<=nt.OptionalHeader.SizeOfImage && section.SizeOfRawData<=nt.OptionalHeader.SizeOfImage-section.VirtualAddress);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    /* Both sites are the original IAT calls; the pins hold. */
    mt_page=NULL;assert(mt_site_state(0)==0 && mt_site_state(1)==0 && mt_pins_ok());
    /* Zone::Move TryEnters g_MassTrafficLock; the MoveWorker waits on g_semSimStart; the kick reads the task. */
    int32_t d;memcpy(&d,image+0x834c6a,4);assert(!memcmp(image+0x834c67,"\x48\x8d\x0d",3) && 0x834c6eu+(uint32_t)d==MT_LOCK);
    memcpy(&d,image+0xef45e6,4);assert(!memcmp(image+0xef45e3,"\x48\x8b\x0d",3) && 0xef45eau+(uint32_t)d==MT_SEM_START);
    memcpy(&d,image+0xef477a,4);assert(!memcmp(image+0xef4777,"\x48\x8b\x3d",3) && 0xef477eu+(uint32_t)d==MT_TASK);
    /* The kick clears the ready byte and signals +0xa8; the pass sets the ready byte and parks on +0xa0. */
    assert(!memcmp(image+0xef47a0,"\x80\xbf\x88\x00\x00\x00\x00",7) && !memcmp(image+0xef47a9,"\xc6\x87\x88\x00\x00\x00\x00",7) &&
           !memcmp(image+0xef47bb,"\x48\x8b\x8f\xa8\x00\x00\x00",7));
    assert(!memcmp(image+0x8c07a7,"\x41\xc6\x85\x88\x00\x00\x00\x01",8) && !memcmp(image+0x8c07da,"\x41\x87\x85\xa0\x00\x00\x00",7));
    assert(!memcmp(image+0x2b8d160,"MassTrafficUpdateTask",22) && !memcmp(image+0x2c53ab0,"RegionManager",14));
    /* The region kick reads its task, clears +0x140 and signals +0x160; Run (runnable at task+0x20) sets +0x140 and parks on +0x158. */
    memcpy(&d,image+0xef47d7,4);assert(!memcmp(image+0xef47d4,"\x48\x8b\x3d",3) && 0xef47dbu+(uint32_t)d==MT_REGION_TASK);
    assert(!memcmp(image+0xef4807,"\x80\xbf\x40\x01\x00\x00\x00",7) && !memcmp(image+0xef4810,"\xc6\x87\x40\x01\x00\x00\x00",7) &&
           !memcmp(image+0xef4822,"\x48\x8b\x8f\x60\x01\x00\x00",7));
    assert(!memcmp(image+0xef315a,"\x48\x8d\x8e\xf8\x00\x00\x00",7) && !memcmp(image+0xef317f,"\xc6\x86\x20\x01\x00\x00\x01",7) &&
           !memcmp(image+0xef31b0,"\x87\x86\x38\x01\x00\x00",6));
    image[0xef3200]^=1;assert(!mt_pins_ok());image[0xef3200]^=1;
    image[0xef4800]^=1;assert(!mt_pins_ok());image[0xef4800]^=1;
    /* Tampering anywhere in a pinned block refuses. */
    image[0x8c0b00]^=1;assert(!mt_pins_ok());image[0x8c0b00]^=1;
    image[0x834c6e]^=1;assert(mt_site_state(0)==-1 && !mt_pins_ok());image[0x834c6e]^=1;assert(mt_pins_ok());
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;mt_page=prior_page;
    puts("mass traffic native sites, task protocol, thread name, pins and tamper refusal verified");
}
