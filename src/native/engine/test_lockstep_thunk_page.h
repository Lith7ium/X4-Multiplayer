/* The caller maps an independent exact native image. No game is started. */
static unsigned test_thunk_protect_calls,test_thunk_flush_calls,test_thunk_free_calls,test_thunk_failure;
static BOOL WINAPI test_thunk_protect(LPVOID page,SIZE_T size,DWORD protection,PDWORD prior) {
    ++test_thunk_protect_calls;assert(size==4096 && protection==PAGE_EXECUTE_READ);
    if(test_thunk_failure==1) return FALSE;
    return VirtualProtect(page,size,test_thunk_failure==3?PAGE_EXECUTE_READWRITE:protection,prior);
}
static BOOL WINAPI test_thunk_flush(HANDLE process,LPCVOID page,SIZE_T size) {
    ++test_thunk_flush_calls;assert(size==4096);
    return test_thunk_failure==2?FALSE:FlushInstructionCache(process,page,size);
}
static BOOL WINAPI test_thunk_free(LPVOID page,SIZE_T size,DWORD mode) {
    ++test_thunk_free_calls;assert(!size && mode==MEM_RELEASE && !world_queue_forward_base);
    DWORD64 base=0;assert(!RtlLookupFunctionEntry((DWORD64)(uintptr_t)page+2689,&base,NULL));
    return VirtualFree(page,size,mode);
}
static volatile LONG test_thunk_stop,test_thunk_calls;
static HANDLE test_thunk_started;
static void test_thunk_original(void *frame,unsigned char loading) {
    assert(frame==(void*)0x1234 && loading==7);InterlockedIncrement(&test_thunk_calls);
}
static DWORD WINAPI test_thunk_worker(LPVOID unused) {
    (void)unused;assert(SetEvent(test_thunk_started));
    do {((tick_fn)thunk)((void*)0x1234,7);} while(!InterlockedCompareExchange(&test_thunk_stop,0,0));
    return 0;
}
static void test_thunk_native_image(void) {
    unsigned char *previous_thunk=thunk;uint64_t main_word=*(uint64_t*)(image+WORD_RVA);
    assert(!stats.active && !ai_installed && !world_queue_forward_base && !portable_wire_enabled);
    for(unsigned failure=1;failure<=3;failure++) {
        test_thunk_failure=failure;test_thunk_protect_calls=test_thunk_flush_calls=test_thunk_free_calls=0;
        assert(!make_thunk_using(VirtualAlloc,test_thunk_protect,test_thunk_flush,test_thunk_free));
        assert(test_thunk_protect_calls==1 && test_thunk_flush_calls==(failure!=1) && test_thunk_free_calls==1);
        assert(thunk==previous_thunk && *(uint64_t*)(image+WORD_RVA)==main_word && !installed);
    }
    test_thunk_failure=0;test_thunk_protect_calls=test_thunk_flush_calls=test_thunk_free_calls=0;
    thunk=make_thunk_using(VirtualAlloc,test_thunk_protect,test_thunk_flush,test_thunk_free);assert(thunk);
    assert(test_thunk_protect_calls==1 && test_thunk_flush_calls==1 && !test_thunk_free_calls && thunk_page_rx(thunk));
    unsigned char frozen[4096];memcpy(frozen,thunk,sizeof(frozen));unsigned end=0;
    unsigned char bounded[320],untouched[320];memset(bounded,0xa5,sizeof(bounded));memcpy(untouched,bounded,sizeof(bounded));
    thunk_recipe invalid=*thunk_recipe_at(800);invalid.size=13;assert(!thunk_emit_slot(bounded,&invalid));
    invalid=*thunk_recipe_at(800);invalid.prefix_size=16;assert(!thunk_emit_slot(bounded,&invalid));
    invalid=*thunk_recipe_at(800);invalid.dispatch=47;assert(!thunk_emit_slot(bounded,&invalid));
    invalid=*thunk_recipe_at(800);invalid.dispatch=7;assert(!thunk_emit_slot(bounded,&invalid));
    invalid=*thunk_recipe_at(800);invalid.kind=999;assert(!thunk_emit_slot(bounded,&invalid));
    assert(!memcmp(bounded,untouched,sizeof(bounded)));
    assert(sizeof(thunk_recipes)/sizeof(*thunk_recipes)==59);
    for(unsigned i=0;i<sizeof(thunk_recipes)/sizeof(*thunk_recipes);i++) {
        const thunk_recipe *r=&thunk_recipes[i];assert(r->offset>=end && r->offset+r->size<=4096 && thunk_slot_ready(r->offset));
        end=r->offset+r->size;
        if(r->prefix_size) {
            assert(r->prefix_size<=15 && !memcmp(image+r->entry,r->prefix,r->prefix_size));
            unsigned immediate=(r->kind==THUNK_UNWIND_TRAMPOLINE || r->kind==THUNK_UNWIND_PAIR)?2:6;
            void *continuation=NULL;memcpy(&continuation,thunk+r->offset+r->prefix_size+immediate,8);
            assert(continuation==image+r->entry+r->prefix_size);
        }
        /* Both instructions and reserved padding are immutable. */
        for(unsigned j=0;j<2;j++) {
            DWORD old;unsigned byte=r->offset+(j?r->size-1:0);
            assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));thunk[byte]^=1;
            assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(!thunk_slot_ready(r->offset));
            assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));thunk[byte]^=1;
            assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(thunk_slot_ready(r->offset));
        }
    }
    assert(!thunk_slot_ready(1) && !thunk_bytes_match(4096,frozen,1) && !thunk_bytes_match(0,frozen,4097) &&
           !thunk_bytes_match(0,NULL,1) && !thunk_bytes_match(0,frozen,0));
    DWORD protections[]={PAGE_READWRITE,PAGE_EXECUTE_READWRITE,PAGE_READONLY,PAGE_NOACCESS,PAGE_EXECUTE_READ|PAGE_GUARD};
    for(unsigned i=0;i<sizeof(protections)/sizeof(*protections);i++) {
        DWORD old;assert(VirtualProtect(thunk,4096,protections[i],&old));assert(!thunk_slot_ready(0));
        assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));
    }
    unsigned char *base=thunk;thunk=base+16;assert(!thunk_page_rx(thunk));thunk=base;
    assert(!memcmp(frozen,thunk,sizeof(frozen)) && *(uint64_t*)(image+WORD_RVA)==main_word);
    /* Independently retained, actual-image emitters agree with the production
     * recipes for the mixed ABI / AL / registered-unwind stubs. */
    unsigned char expected[320]={0};
    assert(solver_emit_thunks(expected,expected+64,image+SOLVER_RVA,(void*)solver_dispatch));
    assert(!memcmp(expected,thunk+2304,128));memset(expected,0,sizeof(expected));
    assert(request_emit_thunks(expected,expected+32,image+REQUEST_RVA,(void*)request_dispatch));
    assert(!memcmp(expected,thunk+2496,64));memset(expected,0,sizeof(expected));
    assert(registry_md_emit(expected,expected+32,image+REGISTRY_MD_RVA,(void*)registry_md_record));
    assert(!memcmp(expected,thunk+2432,64));
    void *targets[]={(void*)world_queue_enqueue,(void*)world_queue_append,(void*)world_queue_dispatch};
    for(unsigned i=0;i<3;i++) {
        memset(expected,0,sizeof(expected));assert(world_queue_emit(expected,expected+32,image+world_queue_entries[i],targets[i],i));
        assert(!memcmp(expected,thunk+2560+i*64,64));
    }
    /* Native-entry CAS failure cannot revoke or free the already sealed page. */
    assert(!swap_word(main_word^1,main_word^2) && *(uint64_t*)(image+WORD_RVA)==main_word && thunk_page_rx(thunk));
    /* Concurrent calls through the real main stub during actual AI install and
     * restore. Only this private image's native entry is changed by the test. */
    unsigned char tick_bytes[14];memcpy(tick_bytes,image+TICK_RVA,14);DWORD old,ignored;
    assert(VirtualProtect(image+TICK_RVA,14,PAGE_EXECUTE_READWRITE,&old));
    thunk_absolute_jump(image+TICK_RVA,(void*)test_thunk_original);FlushInstructionCache(GetCurrentProcess(),image+TICK_RVA,14);
    int old_boot=boot_mode;boot_mode=1;test_thunk_stop=test_thunk_calls=0;
    test_thunk_started=CreateEventW(NULL,TRUE,FALSE,NULL);assert(test_thunk_started);
    HANDLE worker=CreateThread(NULL,0,test_thunk_worker,NULL,0,NULL);assert(worker);
    assert(WaitForSingleObject(test_thunk_started,2000)==WAIT_OBJECT_0);
    for(unsigned iteration=0;iteration<100;iteration++) {
        assert(ai_install(0));ai_restore();assert(!ai_installed && ai_restored);
        for(unsigned i=0;i<sizeof(thunk_recipes)/sizeof(*thunk_recipes);i++) assert(thunk_slot_ready(thunk_recipes[i].offset));
    }
    InterlockedExchange(&test_thunk_stop,1);assert(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0 && test_thunk_calls>0);
    CloseHandle(worker);CloseHandle(test_thunk_started);boot_mode=old_boot;
    memcpy(image+TICK_RVA,tick_bytes,14);FlushInstructionCache(GetCurrentProcess(),image+TICK_RVA,14);
    assert(VirtualProtect(image+TICK_RVA,14,old,&ignored));
    assert(!memcmp(frozen,thunk,sizeof(frozen)) && thunk_page_rx(thunk) && test_thunk_protect_calls==1 && test_thunk_flush_calls==1);
    ai_original_word=ai_patched_word=0;ai_restored=ai_full_layout_verified=0;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=previous_thunk;
    puts("immutable shared thunk: 42 regions/native prefixes, RX and flush failures, bounds/tamper, mixed ABI emitters and concurrent main execution verified");
}
