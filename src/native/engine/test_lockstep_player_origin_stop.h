static unsigned origin_test_pause,origin_test_swaps,origin_test_prepares,origin_test_locked,origin_test_config_result;
static HANDLE origin_test_stop_requested,origin_test_stop_done;
static int origin_test_pause_prepare(void) {
    ++origin_test_prepares;int ok=origin_test_prepare();
    if(!origin_test_pause) {assert(SetEvent(origin_test_entered));assert(WaitForSingleObject(origin_test_resume,5000)==WAIT_OBJECT_0);}
    return ok;
}
static int origin_test_pause_swap(unsigned site,int install,int *changed) {
    int ok=origin_swap(site,install,changed);
    if(install && *changed && ++origin_test_swaps==origin_test_pause) {
        assert(SetEvent(origin_test_entered));assert(WaitForSingleObject(origin_test_resume,5000)==WAIT_OBJECT_0);
    }
    return ok;
}
static DWORD WINAPI origin_test_config_worker(void *unused) {
    (void)unused;ui_thread=GetCurrentThreadId();origin_test_config_result=(unsigned)origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_pause_prepare,origin_test_pause_swap);return 0;
}
static DWORD WINAPI origin_test_stop_worker(void *unused) {
    (void)unused;InterlockedExchange((volatile LONG*)&stats.active,0);origin_test_locked=!TryAcquireSRWLockExclusive(&origin_lock);
    if(!origin_test_locked) ReleaseSRWLockExclusive(&origin_lock);assert(SetEvent(origin_test_stop_requested));
    origin_restore();assert(SetEvent(origin_test_stop_done));return 0;
}
static int origin_test_fail_second(unsigned site,int install,int *changed) {
    if(site==1 && install) {*changed=0;return 0;}return origin_swap(site,install,changed);
}
static DWORD WINAPI origin_test_open_worker(void *mode) {
    main_thread=GetCurrentThreadId();
    if((uintptr_t)mode==2) assert(origin_test_invoke(origin_test_player,origin_test_player,origin_test_due,0xcf,origin_test_event)==origin_test_delivery_result);
    else assert(origin_observe_post(origin_test_native_post,origin_test_player,origin_test_event,origin_test_due,1,(uintptr_t)(image+0x1abb0c),origin_test_teb)==origin_test_post_result);
    return 0;
}
static void test_origin_stop(void) {
    origin_test_reset();assert(!origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_prepare,origin_test_fail_second));
    assert(origin_started && origin_closing && !origin_sites && !memcmp(image+PORIGIN_WORD,origin_test_word,16));
    for(origin_test_pause=0;origin_test_pause<=2;origin_test_pause++) {
        origin_test_reset();origin_test_swaps=origin_test_prepares=origin_test_locked=0;
        origin_test_entered=CreateEvent(NULL,TRUE,FALSE,NULL);origin_test_resume=CreateEvent(NULL,TRUE,FALSE,NULL);
        origin_test_stop_requested=CreateEvent(NULL,TRUE,FALSE,NULL);origin_test_stop_done=CreateEvent(NULL,TRUE,FALSE,NULL);
        assert(origin_test_entered && origin_test_resume && origin_test_stop_requested && origin_test_stop_done);
        HANDLE installer=CreateThread(NULL,0,origin_test_config_worker,NULL,0,NULL);assert(installer);
        assert(WaitForSingleObject(origin_test_entered,5000)==WAIT_OBJECT_0);
        HANDLE stopper=CreateThread(NULL,0,origin_test_stop_worker,NULL,0,NULL);assert(stopper);
        assert(WaitForSingleObject(origin_test_stop_requested,5000)==WAIT_OBJECT_0 && origin_test_locked);
        assert(WaitForSingleObject(origin_test_stop_done,0)==WAIT_TIMEOUT && origin_test_swaps==origin_test_pause);
        assert(SetEvent(origin_test_resume));assert(WaitForSingleObject(installer,5000)==WAIT_OBJECT_0 && WaitForSingleObject(stopper,5000)==WAIT_OBJECT_0);
        assert(!origin_test_config_result && !origin_sites && origin_closing && origin_test_prepares==1);
        assert(!memcmp(image+PORIGIN_WORD,origin_test_word,16) && *(void**)(image+PORIGIN_SLOT)==image+0x152870);
        stats.active=1;ui_thread=GetCurrentThreadId();assert(!origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_pause_prepare,origin_test_pause_swap));
        assert(origin_test_prepares==1);CloseHandle(installer);CloseHandle(stopper);CloseHandle(origin_test_entered);CloseHandle(origin_test_resume);
        CloseHandle(origin_test_stop_requested);CloseHandle(origin_test_stop_done);
    }
    for(unsigned mode=0;mode<(origin_test_actual?3u:2u);mode++) {
        origin_test_reset();
        if(mode) origin_test_install();
        else assert(origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_prepare,origin_swap));
        if(mode==2) {
            origin_test_executable();assert(((porigin_post_fn)origin_test_code)(origin_test_player,origin_test_event,origin_test_due,1)==((origin_test_post_result&~255ULL)|1));
            origin_test_block_delivery=1;
        } else origin_test_blocked=1;
        origin_test_entered=CreateEvent(NULL,TRUE,FALSE,NULL);origin_test_resume=CreateEvent(NULL,TRUE,FALSE,NULL);assert(origin_test_entered && origin_test_resume);
        HANDLE worker=CreateThread(NULL,0,origin_test_open_worker,(void*)(uintptr_t)mode,0,NULL);assert(worker);
        assert(WaitForSingleObject(origin_test_entered,5000)==WAIT_OBJECT_0 && origin_open==1);
        origin_restore();assert(!origin_sites && origin_restore_open==1 && origin_open==1);
        assert(origin_ledger.stop_open==(mode?1u:0u));
        assert(SetEvent(origin_test_resume));assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
        assert(!origin_open && origin_restore_open==1 && origin_calls==origin_returns && !porigin_complete(&origin_ledger));
        if(!mode) assert(origin_prebind==1 && !origin_ledger.count);
        if(mode==2) {assert(origin_test_deliveries==1);VirtualFree(origin_test_code,0,MEM_RELEASE);}
        else assert(origin_test_posts==1);
        CloseHandle(worker);CloseHandle(origin_test_entered);CloseHandle(origin_test_resume);
    }
    puts("origin STOP before publish/aftereachCAS and prebind/post/delivery original-open retention PASS");
}
