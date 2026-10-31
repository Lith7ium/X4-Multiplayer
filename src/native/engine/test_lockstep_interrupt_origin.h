static void test_interrupt_origin(void) {
    unsigned char *prior_thunk=thunk;thunk=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(thunk);
    int ai_on=ai_installed,irq_on=interrupt_installed,ready=interrupt_origin_ready,active=stats.active;
    unsigned __int128 ai_patch=ai_patched_word,ai_bytes=0;uint64_t irq_patch=interrupt_patched_word,irq_bytes=0;
    memcpy(&ai_bytes,image+AI_WORD,16);memcpy(&irq_bytes,image+INTERRUPT_ENTRY,8);
    unsigned char site_bytes[9][5];
    assert(sizeof(interrupt_callsites)/sizeof(*interrupt_callsites)==9);
    for(unsigned i=0;i<9;i++) {
        memcpy(site_bytes[i],image+interrupt_callsites[i].caller-5,5);
        memcpy(image+interrupt_callsites[i].caller-5,interrupt_callsites[i].call,5);
    }
    ai_installed=interrupt_installed=1;interrupt_origin_ready=1;ai_patched_word=123;interrupt_patched_word=456;
    memcpy(image+AI_WORD,&ai_patched_word,16);memcpy(image+INTERRUPT_ENTRY,&interrupt_patched_word,8);
    assert(interrupt_origin_owned() && !interrupt_origin_pinned()); /* Call bytes alone are insufficient. */
    for(unsigned i=0;i<9;i++) {
        interrupt_origin x=interrupt_origin_read((uintptr_t)(image+interrupt_callsites[i].caller));
        assert(x.source==1 && x.raw==interrupt_callsites[i].caller && x.caller==x.raw);
    }
    interrupt_origin x=interrupt_origin_read((uintptr_t)(image+0x1234));assert(x.source==2 && x.raw==0x1234 && !x.caller);
    x=interrupt_origin_read(0);assert(x.source==3 && !x.raw && !x.caller);
    x=interrupt_origin_read((uintptr_t)thunk+832);assert(x.source==3 && !x.raw && !x.caller); /* No guessed owned-stub mapping. */
    uint32_t caller=interrupt_callsites[4].caller;
    image[caller-5]^=1;x=interrupt_origin_read((uintptr_t)(image+caller));assert(x.source==4 && x.raw==caller && !x.caller);image[caller-5]^=1;
    interrupt_origin_ready=0;x=interrupt_origin_read((uintptr_t)(image+caller));assert(x.source==4 && !x.caller);interrupt_origin_ready=1;
    image[AI_WORD]^=1;x=interrupt_origin_read((uintptr_t)(image+caller));assert(x.source==4 && !x.caller);image[AI_WORD]^=1;
    image[INTERRUPT_ENTRY]^=1;x=interrupt_origin_read((uintptr_t)(image+caller));assert(x.source==4 && !x.caller);image[INTERRUPT_ENTRY]^=1;
    /* Execute the actual five-argument entry with observation disabled. Its
     * original receives all native arguments once and retains the full result,
     * including AL. No synthetic native TLS block or RNG replacement is used. */
    unsigned char saved[14],stub[14]={0xff,0x25};memcpy(saved,thunk+800,14);
    void *original=(void*)test_interrupt_original;memcpy(stub+6,&original,8);
    DWORD old,ignored;assert(VirtualProtect(thunk+800,14,PAGE_EXECUTE_READWRITE,&old));memcpy(thunk+800,stub,14);
    FlushInstructionCache(GetCurrentProcess(),thunk+800,14);stats.active=0;
    unsigned calls=test_calls;uint64_t rng=test_rng;
    assert(interrupt_run(&calls,&test_calls,11,22,1)==0x1234567812345678ULL && test_calls==calls+1 && test_rng==rng+123);
    memcpy(thunk+800,saved,14);FlushInstructionCache(GetCurrentProcess(),thunk+800,14);assert(VirtualProtect(thunk+800,14,old,&ignored));
    for(unsigned i=0;i<9;i++) memcpy(image+interrupt_callsites[i].caller-5,site_bytes[i],5);
    memcpy(image+AI_WORD,&ai_bytes,16);memcpy(image+INTERRUPT_ENTRY,&irq_bytes,8);
    ai_patched_word=ai_patch;interrupt_patched_word=irq_patch;ai_installed=ai_on;interrupt_installed=irq_on;interrupt_origin_ready=ready;stats.active=active;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior_thunk;
}
