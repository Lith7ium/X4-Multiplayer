/* Exact-image hooks. Resident thunks and unwind metadata remain allocated even
 * after restoration; an already admitted original may still be executing. */
static int transition_pins(void) {
    return hash_ok(image+0xea4c00,654,"5094ca3adf5fe63c6a9f6cd6e98020e405c4ed5f461469ce935973fd7a9afaf1") &&
        hash_ok(image+0xd88710,759,"e9600674841db99135a2509e27eca8147552ad5b6b9b383508e43c4c21613890") &&
        hash_ok(image+0x6cd8b0,39,"9317be9e33cdf30695ab01f4dfb062d85b836a872a3019c103cf9279c4ff2a32") &&
        hash_ok(image+0x745e00,711,"acd257ef830505bca734583af1fbb2460f8bb09d2bb07d323cbe4596856343b2") &&
        hash_ok(image+0xd83ae0,761,"8dc02f6d8f93ead6ba5f019efcebfcc668721cfb63bf05de76f6c1b44616cd93") &&
        hash_ok(image+0x4500a0,730,"9c05b270f67d60de38a00713724c8e6cce9e2934692151be662e878d62b883d9") &&
        hash_ok(image+0x450380,1110,"8682c85c8ad6cc9040517c50831418a3ed43fb11b80c5d599fb8169baa5ac60e") &&
        hash_ok(image+0xea9230,364,"a4e5a29a47b9256cd0ca82b0426acef96db4341c8186ed913d9f70deb4553b4f") &&
        hash_ok(image+0xd9c590,902,"5986110543f3a5e70fb3fc671e121375b6f136cb633922e616b234afd8c67cd0") &&
        hash_ok(image+0xd9c250,831,"3b8bc7c183957b354e5b52bd2a47754504e24a396081aeb586fe4af1a2372e81") &&
        hash_ok(image+0xd8dd00,75,"a7edf1951f0e2c26938681dab588717ba15cad0c99d840c0ddc1c3a56b02d279") &&
        hash_ok(image+0x352b10,8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53") &&
        hash_ok(image+0xafdfd0,269,"fa051cf54d291f07522934fbe06d2d1b9d6275743fda9f85de430ba446232883") &&
        *(void**)(image+TRANSITION_PUBLICATION)==image+0x6cd8b0;
}
static int transition_encode(unsigned char *trampoline,unsigned char *dispatch,unsigned char *entry,void *target,unsigned site,int verify) {
    if(site>1) return 0;
    static const unsigned char transfer[5]={0x48,0x89,0x5c,0x24,0x10};
    static const unsigned char activation[7]={0x48,0x83,0xec,0x38,0x83,0xf9,0x08};
    unsigned length=site?7:5;
    if(verify && memcmp(entry,site?activation:transfer,length)) return 0;
    memcpy(trampoline,site?activation:transfer,length);void *next=entry+length;
    if(site) {
        /* A RIP-indirect JMP is recognized by Windows as an epilogue and
         * would incorrectly skip this trampoline's still-live stack frame.
         * ModRM.mod=3 register JMP is not a legal unwind epilogue. R11 is
         * volatile and not an argument; MOV/JMP preserve the original CMP. */
        trampoline[7]=0x49;trampoline[8]=0xbb;memcpy(trampoline+9,&next,8);
        trampoline[17]=0x41;trampoline[18]=0xff;trampoline[19]=0xe3;
    } else {
        trampoline[length]=0xff;trampoline[length+1]=0x25;memset(trampoline+length+2,0,4);
        memcpy(trampoline+length+6,&next,8);
    }
    dispatch[0]=0xff;dispatch[1]=0x25;memset(dispatch+2,0,4);memcpy(dispatch+6,&target,8);
    /* sub rsp,0x38 at offset4: UWOP_ALLOC_SMALL, opinfo6. CMP and indirect
     * jump preserve flags into original JNE at +7. */
    if(site) {const unsigned char unwind[8]={1,4,1,0,4,0x62,0,0};memcpy(trampoline+24,unwind,8);}
    return 1;
}
static int transition_emit(unsigned char *trampoline,unsigned char *dispatch,unsigned char *entry,void *target,unsigned site) {
    return transition_encode(trampoline,dispatch,entry,target,site,1);
}
static int transition_register_unwind(BOOLEAN (WINAPI *add)(PRUNTIME_FUNCTION,DWORD,DWORD64)) {
    if(transition_unwind_base) return transition_unwind_base==(DWORD64)(uintptr_t)thunk;
    transition_unwind=(RUNTIME_FUNCTION){2880,2900,2904};
    if(!add(&transition_unwind,1,(DWORD64)(uintptr_t)thunk)) return 0;
    transition_unwind_base=(DWORD64)(uintptr_t)thunk;return 1;
}
static int transition_swap(unsigned site,uint64_t expected,uint64_t value) {
    if(site>2) return 0;DWORD old,ignored;unsigned char *entry=image+transition_entries[site];
    if(!VirtualProtect(entry,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)entry,(LONG64)value,(LONG64)expected);
    VirtualProtect(entry,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),entry,8);return actual==expected;
}
/* Caller owns transition_lock for the entire publication/restoration operation.
 * In-flight originals never hold this lock. A STOP may publish stats.active=0
 * before waiting here; configuration rechecks it before enabling any observer. */
static void transition_restore_locked(void) {
    transition_closing=1;transition_enabled=0;
    if(transition_open>transition_restore_open) transition_restore_open=transition_open;
    for(unsigned i=0;i<3;i++) if((transition_sites&(1u<<i)) && transition_swap(i,transition_patched[i],transition_original[i])) transition_sites&=~(1u<<i);
}
static void transition_restore(void) {
    AcquireSRWLockExclusive(&transition_lock);transition_restore_locked();ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_patch(int (*swap)(unsigned,uint64_t,uint64_t)) {
    for(unsigned i=0;i<3;i++) {
        if(!swap(i,transition_original[i],transition_patched[i])) {
            for(unsigned j=0;j<i;j++) if(swap(j,transition_patched[j],transition_original[j])) transition_sites&=~(1u<<j);
            return 0;
        }
        transition_sites|=1u<<i;
    }
    return 1;
}
static int transition_emit_page(unsigned char *page) {
    memset(page+2816,0,128);
    /* Compile-time pinned prefixes only; disabled optional features introduce
     * no extra startup code pin. Full verification stays at installation. */
    return transition_encode(page+2816,page+2848,image+TRANSITION_TRANSFER,(void*)transition_transfer,0,0) &&
        transition_encode(page+2880,page+2912,image+TRANSITION_ACTIVATION,(void*)transition_activate,1,0);
}
static int transition_prepare(void) {
    intptr_t relative[2];for(unsigned i=0;i<2;i++) {
        relative[i]=(intptr_t)(thunk+2848+i*64)-(intptr_t)(image+transition_entries[i]+5);
        if(relative[i]<INT32_MIN || relative[i]>INT32_MAX) return 0;
    }
    unsigned char expected[128]={0};
    void *targets[2]={(void*)transition_transfer,(void*)transition_activate};int ok=1;
    for(unsigned i=0;i<2;i++) {
        unsigned char *entry=image+transition_entries[i],*stub=expected+i*64;
        if(!transition_emit(stub,stub+32,entry,targets[i],i)) {ok=0;break;}
        memcpy(&transition_original[i],entry,8);transition_patched[i]=transition_original[i];
        unsigned char *patch=(unsigned char*)&transition_patched[i];int32_t rel=(int32_t)relative[i];
        patch[0]=0xe9;memcpy(patch+1,&rel,4);if(i) patch[5]=patch[6]=0x90;
    }
    transition_original[2]=(uint64_t)(uintptr_t)(image+0x6cd8b0);transition_patched[2]=(uint64_t)(uintptr_t)transition_publish;
    return ok && thunk_bytes_match(2816,expected,sizeof(expected)) && transition_register_unwind(RtlAddFunctionTable);
}
static int transition_preflight(void) {
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_queue_enabled || world_queue_sites!=7 || !world_trace_watch_count || !transition_pins() || !world_queue_model_pins()) return 0;
    if(transition_tls==TLS_OUT_OF_INDEXES) transition_tls=TlsAlloc();
    return transition_tls!=TLS_OUT_OF_INDEXES && transition_prepare();
}
static int transition_configure_transaction(int (*prepare)(void),int (*swap)(unsigned,uint64_t,uint64_t)) {
    int ok=0;AcquireSRWLockExclusive(&transition_lock);
    if(transition_closing || !stats.active || (transition_prebind_enabled && (!transition_diag_enabled || transition_prebind.poison))) goto done;
    if(transition_started) {ok=transition_enabled && transition_sites==7;goto done;}
    if(!prepare() || transition_closing || !stats.active) goto done;
    transition_started=1;
    if(!transition_patch(swap)) {transition_restore_locked();goto done;}
    /* Global finish may mark inactive while waiting for our operation lock.
     * Never publish enabled after that cancellation; undo owned sites first. */
    if(transition_closing || !stats.active) {transition_restore_locked();goto done;}
    if(transition_saved_count) {
        world_queue_transition_enqueue_begin=transition_saved_enqueue_begin;world_queue_transition_enqueue_end=transition_saved_enqueue_end;
        world_queue_transition_dispatch_begin=transition_saved_dispatch_begin;world_queue_transition_dispatch_end=transition_saved_dispatch_end;
    } else {world_queue_model_begin=transition_queue_begin;world_queue_model_end=transition_queue_end;}
    transition_enabled=1;ok=1;
done:
    ReleaseSRWLockExclusive(&transition_lock);return ok;
}
static int transition_configure(void) {
    return transition_configure_transaction(transition_preflight,transition_swap);
}
