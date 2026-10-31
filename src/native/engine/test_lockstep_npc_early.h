/* The external predicate is mocked here; the world observer tests its native
 * flag/clock/configuration contract independently before root wires it. */
static int early_test_allowed,early_test_owned,early_test_case,early_test_recover;
static int early_test_eligible(void) {return early_test_allowed;}
static void early_test_original(void *exec) {
    ++test_calls;test_rng+=13;
    uintptr_t previous=*(uintptr_t*)(image+NPC_PROVENANCE_CURRENT);
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=(uintptr_t)exec;
    if(early_test_recover) {early_test_recover=0;early_test_allowed=1;}
    assert(npc_provenance_current_matches((uintptr_t)exec)==early_test_owned);
    if(early_test_case) {
        int which=early_test_case;early_test_case=0;npc_provenance_frame *outer=npc_provenance_current;
        if(which==2) {early_test_allowed=0;early_test_owned=0;early_test_recover=1;}
        if(which==3) {early_test_allowed=0;assert(!npc_provenance_current_matches((uintptr_t)exec));}
        if(which==4) {boot_phase=3;assert(!npc_provenance_current_matches((uintptr_t)exec));}
        if(which!=3) npc_exec_entry(early_test_original,exec,&test_rng,0x106e3fb);
        assert(npc_provenance_current==outer && !npc_provenance_denied_depth);
        if(which==2) {early_test_owned=1;assert(npc_provenance_current_matches((uintptr_t)exec));}
        if(which==4) assert(!npc_provenance_current_matches((uintptr_t)exec));
    }
    *(uintptr_t*)(image+NPC_PROVENANCE_CURRENT)=previous;
}
static DWORD WINAPI early_test_thread(LPVOID exec) {
    assert(!npc_provenance_current && !npc_provenance_denied_depth);
    npc_exec_entry(early_test_original,exec,&test_rng,0);return 0;
}
static void test_npc_provenance_early(void) {
    uint64_t options=boot_options,issued=stats.issued;int enabled=npc_provenance_enabled,phase=boot_phase,bmode=boot_mode,active=stats.active;
    unsigned char exec[0x50]={0},context[0x200]={0};
    *(unsigned*)(exec+0x38)=0x66;*(uintptr_t*)(exec+0x40)=(uintptr_t)context+0xa8;*(uint64_t*)(context+8)=81;
    boot_options=281474976710656ULL;npc_provenance_enabled=1;boot_mode=1;boot_phase=2;stats.issued=0;stats.active=1;
    assert(!npc_provenance_current && !npc_provenance_denied_depth && !npc_provenance_early_eligible);
    uint64_t triggers=npc_trigger_calls,provenance=npc_provenance_calls,execs=npc_exec_calls;LONG64 pilots=npc_pilot_calls;
    uint64_t failures=npc_provenance_failures,overflows=npc_provenance_overflows;
    unsigned calls=test_calls;uint64_t rng=test_rng;early_test_owned=0;
    npc_exec_entry(early_test_original,exec,&test_rng,0); /* Default NULL. */
    npc_provenance_early_eligible=early_test_eligible;early_test_allowed=0;
    npc_exec_entry(early_test_original,exec,&test_rng,0); /* Explicit refusal. */
    early_test_allowed=1;early_test_owned=1;
    npc_exec_entry(early_test_original,exec,&test_rng,0); /* Raw foreign caller stays unsupported. */
    assert(test_calls==calls+3 && test_rng==rng+39 && !npc_provenance_current);
    npc_provenance_frame frame;int entered=npc_provenance_enter(&frame,exec,0);
    assert(entered==1 && frame.scope_phase==2 && frame.caller_source==3 && !frame.caller && !ai_origin_dispatch_current);
    ++*(uint64_t*)(context+8);assert(!npc_provenance_current_matches((uintptr_t)exec));
    --*(uint64_t*)(context+8);npc_provenance_leave(&frame,entered);
    for(int which=1;which<=4;which++) {
        early_test_allowed=early_test_owned=1;early_test_case=which;calls=test_calls;rng=test_rng;
        npc_exec_entry(early_test_original,exec,&test_rng,0x106e3fb);
        unsigned originals=which==3?1:2;
        assert(test_calls==calls+originals && test_rng==rng+13*originals && !npc_provenance_current && !npc_provenance_denied_depth);
        boot_phase=2;
    }
    /* Unknown/unreadable early owners form no positive ownership assertion. */
    early_test_allowed=1;early_test_owned=0;*(unsigned*)(exec+0x38)=0x777;
    npc_exec_entry(early_test_original,exec,&test_rng,0);*(unsigned*)(exec+0x38)=0x66;
    npc_exec_entry(early_test_original,(void*)1,&test_rng,0);
    /* Independent native guards are mandatory even if the external callback errs. */
    stats.issued=1;npc_exec_entry(early_test_original,exec,&test_rng,0);stats.issued=0;
    boot_mode=0;npc_exec_entry(early_test_original,exec,&test_rng,0);boot_mode=1;
    boot_phase=1;npc_exec_entry(early_test_original,exec,&test_rng,0);boot_phase=2;
    stats.active=0;npc_exec_entry(early_test_original,exec,&test_rng,0);stats.active=1;
    boot_options=140737488355328ULL;npc_exec_entry(early_test_original,exec,&test_rng,0);boot_options=281474976710656ULL;
    HANDLE thread=CreateThread(NULL,0,early_test_thread,exec,0,NULL);assert(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    assert(!npc_provenance_current && !npc_provenance_denied_depth && npc_provenance_failures==failures && npc_provenance_overflows==overflows);
    assert(npc_trigger_calls==triggers && npc_provenance_calls==provenance && npc_exec_calls==execs && npc_pilot_calls==pilots);
    /* Ordinary phase3 does not depend on an early predicate returning true. */
    early_test_allowed=0;early_test_owned=1;boot_phase=3;
    npc_exec_entry(early_test_original,exec,&test_rng,0x106e3fb);assert(!npc_provenance_current);
    npc_provenance_early_eligible=NULL;npc_provenance_enabled=enabled;boot_options=options;stats.issued=issued;
    boot_phase=phase;boot_mode=bmode;stats.active=active;
}
