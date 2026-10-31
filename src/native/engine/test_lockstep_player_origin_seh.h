/* Exceptions are caught only by this owned harness. Production propagates them.
 * Synthetic non-leaf callers need valid pdata: leaf fallback is insufficient. */
static RUNTIME_FUNCTION origin_test_unwind[2];
static unsigned origin_test_caught;
static int origin_test_filter(DWORD code) {
    if(code!=ORIGIN_TEST_EXCEPTION) return EXCEPTION_CONTINUE_SEARCH;
    ++origin_test_caught;return EXCEPTION_EXECUTE_HANDLER;
}
static void origin_test_register_unwind(void) {
    /* Post caller: PUSH RDI; SUB RSP,30; SAVE RBX @40, RSI @48.
     * Region starts after its synthetic prologue, so every code offset is0. */
    const unsigned char post[]={1,0,6,0,0,0x64,9,0,0,0x34,8,0,0,0x52,0,0x70};
    const unsigned char delivery[]={1,0,1,0,0,0x42,0,0}; /* SUB RSP,28 */
    memcpy(image+0x5000300,post,sizeof(post));memcpy(image+0x5000320,delivery,sizeof(delivery));
    origin_test_unwind[0]=(RUNTIME_FUNCTION){0x1abb07,0x1abb26,0x5000300};
    origin_test_unwind[1]=(RUNTIME_FUNCTION){0x99c5ad,0x99c5b5,0x5000320};
    assert(RtlAddFunctionTable(origin_test_unwind,2,(DWORD64)(uintptr_t)image));
    DWORD64 base=0;assert(RtlLookupFunctionEntry((DWORD64)(uintptr_t)(image+0x1abb0c),&base,NULL)==&origin_test_unwind[0]);
    assert(base==(DWORD64)(uintptr_t)image);
}
static void origin_test_throwing_call(unsigned mode) {
    __try {
        if(mode==1) origin_test_invoke(origin_test_player,origin_test_player,origin_test_due,0xcf,origin_test_event);
        else ((porigin_post_fn)origin_test_code)(origin_test_player,origin_test_event,origin_test_due,1);
        assert(0);
    } __except(origin_test_filter(GetExceptionCode())) {}
}
static DWORD WINAPI origin_test_unwind_worker(void *mode) {
    main_thread=GetCurrentThreadId();origin_test_throwing_call((unsigned)(uintptr_t)mode);assert(!porigin_current);return 0;
}
static void test_origin_seh(void) {
    if(!origin_test_actual) return;
    for(unsigned mode=0;mode<4;mode++) {
        origin_test_reset();origin_test_install();origin_test_executable();origin_test_register_unwind();origin_test_caught=0;
        if(mode==1) assert(((porigin_post_fn)origin_test_code)(origin_test_player,origin_test_event,origin_test_due,1)==((origin_test_post_result&~255ULL)|1));
        if(mode==0 || mode==3) origin_test_raise_post=1;
        else origin_test_raise_delivery=1;
        if(mode==2) origin_test_immediate=1;
        if(mode==3) {
            origin_test_blocked=1;origin_test_entered=CreateEvent(NULL,TRUE,FALSE,NULL);origin_test_resume=CreateEvent(NULL,TRUE,FALSE,NULL);
            assert(origin_test_entered && origin_test_resume);HANDLE worker=CreateThread(NULL,0,origin_test_unwind_worker,NULL,0,NULL);assert(worker);
            assert(WaitForSingleObject(origin_test_entered,5000)==WAIT_OBJECT_0);
            origin_restore();assert(origin_restore_open==1 && origin_ledger.stop_open==1 && origin_open==1 && !origin_returns);
            assert(SetEvent(origin_test_resume) && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
            CloseHandle(worker);CloseHandle(origin_test_entered);CloseHandle(origin_test_resume);
        } else origin_test_throwing_call(mode);
        assert(origin_test_caught==1 && !porigin_current && !origin_open && !origin_ledger.open);
        assert(origin_test_posts==1 && origin_test_deliveries==(mode==1 || mode==2));
        assert(origin_unwinds==(mode==2?2u:1u) && origin_calls==origin_returns+origin_unwinds);
        assert(origin_ledger.post_unwinds==(mode!=1) && origin_ledger.delivery_unwinds==(mode==1 || mode==2));
        assert(origin_ledger.post_calls==origin_ledger.post_returns+origin_ledger.post_unwinds);
        assert(origin_ledger.delivery_calls==origin_ledger.delivery_returns+origin_ledger.delivery_unwinds);
        assert(origin_ledger.tickets[0].errors&PO_UNWIND);
        assert(origin_ledger.tickets[0].posted_returned==(mode==1) && origin_ledger.tickets[0].delivery_state==((mode==1 || mode==2)?1u:0u));
        assert(!origin_ledger.tickets[0].controller_after && !origin_ledger.tickets[0].after_clock);
        origin_restore();assert(!origin_sites && !porigin_complete(&origin_ledger));
        DWORD old;assert(VirtualProtect(origin_test_event,4096,PAGE_READWRITE,&old));
        assert(RtlDeleteFunctionTable(origin_test_unwind));VirtualFree(origin_test_code,0,MEM_RELEASE);
    }
    puts("origin actual adapters: post/deferred/reentrant exception propagation, TLS/open cleanup and STOP retention PASS");
}
static unsigned transition_test_raise_calls;
static uintptr_t transition_test_throw_transfer(void *next,void *old,unsigned char flag,int extra) {
    (void)next;(void)flag;(void)extra;++transition_test_raise_calls;
    DWORD protection;assert(VirtualProtect(old,4096,PAGE_NOACCESS,&protection));
    RaiseException(ORIGIN_TEST_EXCEPTION,0,0,NULL);return 0;
}
static void transition_test_throw_publish(void *o,void *next,unsigned char flag) {
    (void)o;(void)next;(void)flag;++transition_test_raise_calls;
    DWORD protection;assert(VirtualProtect(old_controller,4096,PAGE_NOACCESS,&protection));
    RaiseException(ORIGIN_TEST_EXCEPTION,0,0,NULL);
}
static void transition_test_throw_activation(int context,unsigned char active) {
    (void)context;(void)active;++transition_test_raise_calls;RaiseException(ORIGIN_TEST_EXCEPTION,0,0,NULL);
}
static void transition_test_nested_activation(int context,unsigned char active) {
    assert(context==8 && active==1);
    transition_activate_observe(transition_test_throw_activation,7,1,(uintptr_t)(image+1));
}
static void test_transition_seh(void) {
    for(unsigned mode=0;mode<4;mode++) {
        origin_test_reset();origin_test_install();origin_test_caught=transition_test_raise_calls=0;
        transition_scope parent={0};transition_event *sentinel=&transition_events[TRANSITION_MAX-1];parent.event=sentinel;
        assert(TlsSetValue(transition_tls,&parent));
        __try {
            if(mode==0) transition_transfer_observe(transition_test_throw_transfer,next_controller,old_controller,0,1,(uintptr_t)(image+1));
            else if(mode==1) transition_publish_observe(transition_test_throw_publish,object,next_controller,1,(uintptr_t)(image+1));
            else transition_activate_observe(mode==2?transition_test_throw_activation:transition_test_nested_activation,8,1,(uintptr_t)(image+1));
            assert(0);
        } __except(origin_test_filter(GetExceptionCode())) {}
        assert(origin_test_caught==1 && transition_test_raise_calls==1 && !transition_open);
        assert(transition_direct_calls==(mode==3?2:1) && transition_direct_unwinds==transition_direct_calls && !transition_direct_returns);
        assert(TlsGetValue(transition_tls)==&parent && parent.event==sentinel);
        assert(transition_count==1 && transition_events[0].complete && (transition_events[0].errors&64));
        assert(!transition_events[0].after.object && !transition_events[0].result && transition_failures);
        assert(TlsSetValue(transition_tls,NULL));DWORD old;assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&old));origin_restore();
    }
    puts("direct transitions: exception propagation, native-read exclusion and nested excluded TLS restoration PASS");
}
