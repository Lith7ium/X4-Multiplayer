static void *test_origin_exec,*test_origin_context,*test_origin_program;
static uint64_t test_origin_rng_word;
static unsigned test_origin_calls,test_origin_throw,test_origin_stop;
static void test_origin_native_maybe_throw(void) {
    if(!test_origin_throw)return;
    if(test_origin_stop)registry_origin_close();
    RaiseException(0xe04d5043,0,0,NULL);
}
static uintptr_t test_origin_range_original(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    assert(exec==test_origin_exec && program==test_origin_program && begin==11 && end==22 && mode==1);
    ++test_origin_calls;test_origin_native_maybe_throw();test_origin_rng_word+=7;return 0xfedcba98765432a5ULL;
}
static void test_origin_event_original(void *context) {assert(context==test_origin_context);++test_origin_calls;test_origin_native_maybe_throw();}
static void test_origin_ai_original(void *exec) {assert(exec==test_origin_exec);++test_origin_calls;test_origin_native_maybe_throw();}
static void test_origin_radar_original(void *context,void *source,unsigned flags) {
    assert(context==test_origin_context && source==test_origin_program && flags==1234);++test_origin_calls;test_origin_native_maybe_throw();
}
static uintptr_t test_origin_catching_parent(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    assert(exec==test_origin_exec && program==test_origin_program && begin==11 && end==22 && mode==1);
    ++test_origin_calls;registry_origin_scope *parent=registry_origin_current;
    unsigned caught=0;test_origin_throw=1;
    __try {interrupt_event_observe(test_origin_event_original,&test_origin_rng_word,test_origin_context,0);}
    __except(GetExceptionCode()==0xe04d5043?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    assert(caught && registry_origin_current==parent && registry_origin_open==1);
    test_origin_throw=0;
    interrupt_event_observe(test_origin_event_original,&test_origin_rng_word,test_origin_context,0);
    assert(registry_origin_current==parent && registry_origin_open==1);
    return 0x123456789abcdef0ULL;
}
static void test_origin_reset(void) {
    memset(registry_pending,0,sizeof(registry_pending));registry_pending_count=0;
    registry_origin_enabled=1;registry_origin_closing=0;registry_origin_open=registry_origin_restore_open=registry_origin_peak=registry_origin_failures=0;
    assert(!registry_origin_current);registry_md_order=0;
}
static HANDLE test_origin_entered,test_origin_release;
static DWORD WINAPI test_origin_worker(LPVOID object) {
    assert(!registry_origin_current);registry_origin_scope scope;
    registry_origin_enter(&scope,REGISTRY_ORIGIN_EVENT,0,NULL,NULL,NULL,0,0,0,NULL,NULL);
    registry_record_update(object,999,REGISTRY_AI);assert(SetEvent(test_origin_entered));
    assert(WaitForSingleObject(test_origin_release,2000)==WAIT_OBJECT_0);
    registry_origin_leave(&scope,0);assert(!registry_origin_current);return 0;
}
static void test_registry_origin(void *object) {
    unsigned char context[0x190]={0},exec[0x48]={0},script[0x30]={0},tls[0x320]={0};
    *(void**)(context+0x120)=object;*(void**)(context+0x130)=exec;*(void**)(context+0xd8)=script;
    memcpy(script+0x10,"orders.base",12);*(uint64_t*)(script+0x20)=11;*(uint64_t*)(script+0x28)=15;
    *(uint32_t*)(exec+0x38)=0x66;*(uintptr_t*)(exec+0x40)=(uintptr_t)context+0xa8;*(uint64_t*)(exec+0x18)=42;
    *(uint64_t*)(context+0x140)=17;tls[0x314]=1;*(double*)(tls+0x300)=7.0;*(uint64_t*)(tls+0x318)=123;
    test_origin_exec=exec;test_origin_context=context;test_origin_program=script;test_origin_calls=0;test_origin_rng_word=100;
    unsigned char ai_save[16],irq_save[8],call_save[5];memcpy(ai_save,image+AI_WORD,16);memcpy(irq_save,image+INTERRUPT_ENTRY,8);
    memcpy(call_save,image+0x1066dc1,5);memcpy(image+0x1066dc1,interrupt_callsites[2].call,5);
    int ai_on=ai_installed,irq_on=interrupt_installed;unsigned __int128 ai_patch=ai_patched_word;uint64_t irq_patch=interrupt_patched_word;
    ai_installed=interrupt_installed=1;ai_patched_word=123;interrupt_patched_word=456;interrupt_origin_ready=1;
    memcpy(image+AI_WORD,&ai_patched_word,16);memcpy(image+INTERRUPT_ENTRY,&interrupt_patched_word,8);
    registry_md_installed=1;test_origin_reset();registry_origin_scope first,nested;
    registry_origin_enter(&first,REGISTRY_ORIGIN_RANGE,(uintptr_t)(image+0x1066dc6),exec,context,script,0,3,1,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,999,REGISTRY_AI);
    registry_pending_update immutable=registry_pending[0];assert(immutable.origin.recorded && immutable.origin.reads==767 && !immutable.origin.error);
    assert(immutable.origin.identity.recorded && immutable.origin.identity.first.actual==999 && immutable.origin.identity.first.agent_state==5 && immutable.origin.identity.stable==1);
    assert(immutable.origin.caller_source==1 && immutable.origin.verified_caller==0x1066dc6 && immutable.origin.pc==42 && immutable.origin.context_pc==17);
    assert(immutable.origin.depth==0 && immutable.origin.entity==999 && immutable.origin.type==0x2b99a88 && !strcmp(immutable.origin.script,"orders.base"));
    registry_origin_enter(&nested,REGISTRY_ORIGIN_EVENT,0,NULL,NULL,NULL,0,0,0,NULL,NULL);
    registry_record_update(object,999,REGISTRY_AI);registry_origin_leave(&nested,0);
    assert(!memcmp(&immutable,&registry_pending[0],sizeof(immutable)) && registry_origin_current==&first);
    assert(registry_origin_peak==2);
    *(uint64_t*)(tls+0x318)=130;registry_origin_leave(&first,0xa5);
    registry_pending_origin completed=registry_pending[0].origin;
    assert(completed.reads==1023 && completed.complete && completed.result==0xa5 && completed.rng_before==123 && completed.rng_after==130);
    assert(registry_pending[0].order==immutable.order && registry_pending[0].tick==immutable.tick && registry_pending[0].thread==immutable.thread && registry_md_order==1);
    char output[1536];assert(registry_origin_format(0,output,sizeof(output)) && strstr(output,"source=2 caller=1066dc6 verified=1066dc6") && strstr(output,"reads=3ff error=0 complete=1"));
    assert(!registry_origin_format(0,output,10) && !registry_origin_format(1,output,sizeof(output)));
    registry_pending_origin maximum;memset(&maximum,255,sizeof(maximum));memset(maximum.script,'x',95);maximum.script[95]=0;
    registry_pending[0].origin=maximum;
    assert(registry_origin_format(0,output,sizeof(output))>0 && strlen(output)<900); /* One bounded engine log row, including maximal script. */
    registry_pending[0].origin=completed;
    test_origin_reset();registry_record_update(object,999,REGISTRY_AI);immutable=registry_pending[0];
    assert(immutable.origin.recorded && immutable.origin.error==ORIGIN_UNKNOWN && !immutable.origin.complete);
    assert(immutable.origin.identity.recorded && immutable.origin.identity.first.actual==999);
    registry_origin_enter(&first,REGISTRY_ORIGIN_AI,(uintptr_t)(image+0x106e3fb),exec,context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,999,REGISTRY_AI);registry_origin_leave(&first,0);assert(!memcmp(&immutable,&registry_pending[0],sizeof(immutable)));
    test_origin_reset();registry_origin_enter(&first,REGISTRY_ORIGIN_RANGE,0,NULL,NULL,NULL,0,0,0,NULL,NULL);
    registry_record_update(object,999,REGISTRY_AI);registry_origin_leave(&first,0);completed=registry_pending[0].origin;
    assert(completed.recorded && completed.complete && completed.reads==1 && completed.caller_source==3 && (completed.error&(ORIGIN_READ|ORIGIN_CALLER))==(ORIGIN_READ|ORIGIN_CALLER));
    registry_origin_enter(&first,REGISTRY_ORIGIN_RANGE,(uintptr_t)(image+0x1066dc6),exec,context,script,0,3,1,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,999,REGISTRY_AI);registry_origin_leave(&first,0);assert(!memcmp(&completed,&registry_pending[0].origin,sizeof(completed)));
    /* Real production wrappers: each calls ai_actor before the original and
     * captures its own origin. No synthetic engine TLS or RNG replacement. */
    test_origin_reset();unsigned calls=test_origin_calls;
    assert(interrupt_observe(test_origin_range_original,&test_origin_rng_word,exec,script,11,22,1,(uintptr_t)(image+0x1066dc6))==0xfedcba98765432a5ULL);
    completed=registry_pending[0].origin;
    assert(test_origin_calls==calls+1 && registry_pending_count==1 && completed.source==REGISTRY_ORIGIN_RANGE && completed.complete &&
        completed.begin==11 && completed.end==22 && completed.mode==1 && completed.rng_before==100 && completed.rng_after==107 && completed.result==0xfedcba98765432a5ULL);
    immutable=registry_pending[0];assert(!registry_bind(15,999,test_dynamic_find,object) && !strcmp(registry_stage,"late_update"));
    assert(!memcmp(&immutable,&registry_pending[0],sizeof(immutable)) && !dynamic_ships[0].bound_tick && !params_done[14]);
    dynamic_ships[0].late_updates=0;dynamic_ships[0].first_update_tick=0;dynamic_ships[0].first_update_kind=0;
    test_origin_reset();calls=test_origin_calls;interrupt_event_observe(test_origin_event_original,&test_origin_rng_word,context,(uintptr_t)(image+0x1234));
    assert(test_origin_calls==calls+1 && registry_pending[0].origin.source==REGISTRY_ORIGIN_EVENT && registry_pending[0].origin.complete);
    test_origin_reset();calls=test_origin_calls;radar_execute(test_origin_radar_original,context,script,1234,(uintptr_t)(image+0x1234));
    assert(test_origin_calls==calls+1 && registry_pending[0].origin.source==REGISTRY_ORIGIN_RADAR && registry_pending[0].origin.mode==1234);
    unsigned char original_bytes[14],jump[14]={0xff,0x25};void *target=(void*)test_origin_ai_original;memcpy(jump+6,&target,8);
    memcpy(original_bytes,image+AI_ORIGINAL,14);DWORD protection,ignored;assert(VirtualProtect(image+AI_ORIGINAL,14,PAGE_EXECUTE_READWRITE,&protection));
    memcpy(image+AI_ORIGINAL,jump,14);FlushInstructionCache(GetCurrentProcess(),image+AI_ORIGINAL,14);
    test_origin_reset();calls=test_origin_calls;ai_run(exec);
    assert(test_origin_calls==calls+1 && registry_pending[0].origin.source==REGISTRY_ORIGIN_AI && registry_pending[0].origin.complete);
    uint64_t old_ai_failures=ai_failures;LONG64 old_irq_failures=interrupt_failures,old_radar_failures=radar_failures;
    test_origin_throw=1;
    for(unsigned stop=0;stop<2;stop++)for(unsigned kind=0;kind<4;kind++) {
        test_origin_stop=stop;test_origin_reset();calls=test_origin_calls;
        /* An unadmitted parent still owns a stack link. A child abort must
         * restore it even though the parent contributes no native receipt. */
        registry_origin_enabled=0;registry_origin_scope parent;
        registry_origin_enter(&parent,REGISTRY_ORIGIN_EVENT,0,NULL,NULL,NULL,0,0,0,NULL,NULL);
        assert(!parent.admitted);registry_origin_enabled=1;
        interrupt_sample parent_interrupt={0};interrupt_sample *old_interrupt=interrupt_current;interrupt_current=&parent_interrupt;
        unsigned caught=0;
        __try {
            if(kind==0)(void)interrupt_observe(test_origin_range_original,&test_origin_rng_word,exec,script,11,22,1,(uintptr_t)(image+0x1066dc6));
            else if(kind==1)interrupt_event_observe(test_origin_event_original,&test_origin_rng_word,context,(uintptr_t)(image+0x1234));
            else if(kind==2)radar_execute(test_origin_radar_original,context,script,1234,(uintptr_t)(image+0x1234));
            else ai_run(exec);
        } __except(GetExceptionCode()==0xe04d5043?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
        assert(caught && test_origin_calls==calls+1 && registry_origin_current==&parent && !registry_origin_open);
        assert(interrupt_current==&parent_interrupt);interrupt_current=old_interrupt;
        assert(registry_origin_restore_open==(stop?1:0) && registry_pending_count==1);
        completed=registry_pending[0].origin;
        assert((completed.error&ORIGIN_UNWIND) && !completed.complete && !completed.result && !completed.rng_after && !(completed.reads&256));
        registry_origin_abort(&parent);registry_origin_abort(&parent);
        assert(!registry_origin_current && !registry_origin_open);
    }
    test_origin_throw=test_origin_stop=0;
    test_origin_reset();calls=test_origin_calls;
    assert(interrupt_observe(test_origin_catching_parent,&test_origin_rng_word,exec,script,11,22,1,(uintptr_t)(image+0x1066dc6))==0x123456789abcdef0ULL);
    assert(test_origin_calls==calls+3 && !registry_origin_current && !registry_origin_open && registry_origin_failures);
    assert(registry_pending[0].origin.complete && registry_pending[0].origin.result==0x123456789abcdef0ULL);
    ai_failures=old_ai_failures;interrupt_failures=old_irq_failures;radar_failures=old_radar_failures;
    memcpy(image+AI_ORIGINAL,original_bytes,14);FlushInstructionCache(GetCurrentProcess(),image+AI_ORIGINAL,14);assert(VirtualProtect(image+AI_ORIGINAL,14,protection,&ignored));
    /* A foreign thread's in-flight original cannot be declared complete by STOP. */
    test_origin_reset();test_origin_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_origin_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(test_origin_entered && test_origin_release);HANDLE worker=CreateThread(NULL,0,test_origin_worker,object,0,NULL);assert(worker);
    assert(WaitForSingleObject(test_origin_entered,2000)==WAIT_OBJECT_0);assert(!registry_origin_current);
    registry_origin_close();assert(registry_origin_open==1 && registry_origin_restore_open==1 && !registry_pending[0].origin.complete);
    assert(SetEvent(test_origin_release) && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
    assert(registry_origin_open==0 && registry_origin_restore_open==1 && registry_pending[0].origin.complete);
    CloseHandle(worker);CloseHandle(test_origin_entered);CloseHandle(test_origin_release);
    registry_origin_enter(&first,REGISTRY_ORIGIN_AI,0,exec,context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));assert(!first.admitted);registry_origin_leave(&first,0);
    test_origin_reset();unsigned char many[17][0x80]={0};
    registry_origin_enter(&first,REGISTRY_ORIGIN_RANGE,(uintptr_t)(image+0x1066dc6),exec,context,script,0,3,1,tls,(uint64_t*)(tls+0x318));
    for(unsigned i=0;i<17;i++) {
        *(void**)many[i]=image+0x2b99a88;*(uint64_t*)(many[i]+8)=2000+i;*(void**)(context+0x120)=many[i];
        registry_record_update(many[i],2000+i,REGISTRY_AI);
    }
    registry_origin_leave(&first,0);assert(registry_pending_count==17 && registry_origin_failures==1);
    assert(registry_pending[15].origin.complete && !registry_pending[16].origin.complete && registry_pending[16].origin.error==ORIGIN_OVERFLOW);
    *(void**)(context+0x120)=object;
    test_origin_reset();registry_origin_enabled=0;registry_md_installed=0;
    ai_installed=ai_on;interrupt_installed=irq_on;ai_patched_word=ai_patch;interrupt_patched_word=irq_patch;interrupt_origin_ready=0;
    memcpy(image+AI_WORD,ai_save,16);memcpy(image+INTERRUPT_ENTRY,irq_save,8);memcpy(image+0x1066dc1,call_save,5);
}
