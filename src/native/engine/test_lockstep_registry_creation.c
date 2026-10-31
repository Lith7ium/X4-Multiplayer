/* The production gate and exact mapped image are inherited from the constructor
 * harness. Objects, registry tables and originals below are owned test memory. */
#include "test_lockstep_creation_rng.c"
static const char rc_preimage[]=CREATION_RNG_BOOTSTRAP CREATION_RNG_PREFIX "1" CREATION_RNG_SUFFIX CREATION_RNG_ACK_SUFFIX REGISTRY_CREATION_SUFFIX;
static unsigned char rc_map[0x200],rc_context[0x190],rc_exec[0x48],rc_script[0x30];
static unsigned rc_lookup_calls,rc_lookup_mode,rc_generate_index,rc_init_index,rc_pilot_index;
static void rc_clear(void) {
    registry_creation_configured=0;registry_creation_closing=registry_creation_identity_count=registry_creation_operation_count=0;
    registry_creation_sequence=registry_creation_failures=registry_creation_dropped=0;
    registry_creation_open=registry_creation_peak=registry_creation_stop_open=registry_creation_identity_open=registry_creation_identity_stop_open=0;
    registry_creation_unpaired=registry_creation_workers=registry_creation_kinds=registry_creation_identity_admitted=0;
    registry_creation_handle=registry_creation_runtime=registry_creation_object=registry_creation_owner=registry_creation_tick=0;
    registry_creation_thread=0;registry_creation_digest[0]=registry_creation_rng_manifest[0]=0;
    registry_creation_ack_mode=registry_creation_acks=registry_creation_ack_phase=0;
    memset(registry_creation_identities,0,sizeof(registry_creation_identities));memset(registry_creation_operations,0,sizeof(registry_creation_operations));
}
static int rc_config(void) {
    return registry_creation_configure(REGISTRY_CREATION_MANIFEST,sizeof(REGISTRY_CREATION_MANIFEST)-1,
        manifest,sizeof(manifest)-1,rc_preimage,sizeof(rc_preimage)-1);
}
static void rc_reset(void) {
    rc_clear();reset_before_activity();creation_rng_started=0;registry_origin_closing=0;
    registry_origin_current=NULL;registry_origin_open=registry_origin_restore_open=registry_origin_peak=0;
    memset(registry_pending,0,sizeof(registry_pending));registry_pending_count=0;
    strcpy(initial_state_digest,"91b30261372f883f52286bd23a19ccbabd2c03b60b7d6627de662c731af6bf85");
    CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,rc_preimage,sizeof(rc_preimage)-1));
    CHECK(rc_config());CHECK(rc_config());CHECK(!registry_creation_sequence);
    CHECK(creation_rng_configure(manifest,sizeof(manifest)-1,rc_preimage,sizeof(rc_preimage)-1));
    CHECK(creation_rng_keys[0]==0x8b34a17d4ae84a2fULL&&creation_rng_keys[1]==0x5311c417920e6e36ULL);
    CHECK(registry_reserve(0x500000003ULL,CREATION_RNG_MACRO,strlen(CREATION_RNG_MACRO),"player",6,5)==15);
    boot_phase=3;stats.issued=6;creation_md_note(1,1,1,registry_md_next_order(),6,0x500000003ULL,1,1,0,ui_thread,31);
    *(void**)(image+0x6cf1500)=rc_map;rc_lookup_calls=rc_lookup_mode=0;
    memset(rc_context,0,sizeof(rc_context));memset(rc_exec,0,sizeof(rc_exec));memset(rc_script,0,sizeof(rc_script));
    *(void**)(rc_context+0x120)=object;*(void**)(rc_context+0x130)=rc_exec;*(void**)(rc_context+0xd8)=rc_script;
    memcpy(rc_script+0x10,"orders.base",12);*(uint64_t*)(rc_script+0x20)=11;*(uint64_t*)(rc_script+0x28)=15;
    *(uint32_t*)(rc_exec+0x38)=0x66;*(uintptr_t*)(rc_exec+0x40)=(uintptr_t)rc_context+0xa8;
}
static void *rc_find(void *map,uint64_t runtime,unsigned flags) {
    CHECK(map==rc_map&&runtime==registry_creation_runtime&&flags==4);++rc_lookup_calls;
    CHECK(TryAcquireSRWLockExclusive(&creation_lock));ReleaseSRWLockExclusive(&creation_lock);
    CHECK(TryAcquireSRWLockExclusive(&registry_lock));ReleaseSRWLockExclusive(&registry_lock);
    CHECK(TryAcquireSRWLockExclusive(&registry_creation_lock));ReleaseSRWLockExclusive(&registry_creation_lock);
    if(rc_lookup_mode==1)return NULL;
    if(rc_lookup_mode==2)return owner;
    if(rc_lookup_mode==3&&rc_lookup_calls==2)*(void**)(object+0x348)=macro;
    if(rc_lookup_mode==4&&rc_lookup_calls==2)*(void**)(image+0x6cf1500)=owner;
    if(rc_lookup_mode==5)registry_creation_stop();
    if(rc_lookup_mode==6)RaiseException(0xe0427777,0,0,NULL);
    if(rc_lookup_mode==7 && rc_lookup_calls==1)*(uint64_t*)(object+8)^=1ULL<<63;
    return object;
}
static void rc_begin(void) {
    uint64_t args[]={0xabc,(uintptr_t)&object_ref,0xdef,0,1,2,3,4,1,0};
    rc_generate_index=enter(CREATION_GENERATE,args,10);CHECK(rc_generate_index);
    registry_creation_trace_using(rc_generate_index,0,rc_find,rc_map);
    uint64_t init[]={0,(uintptr_t)&local_ref,0,0,0,0,0,0,1};
    rc_init_index=enter(CREATION_INITIALIZE,init,9);CHECK(rc_init_index);
    registry_creation_trace_using(rc_init_index,0,rc_find,rc_map);
    *(void**)(object+0x348)=owner;
    uint64_t pilot[]={0,0,(uintptr_t)&local_ref,(uintptr_t)owner,0,0,0};
    rc_pilot_index=enter(CREATION_PILOT,pilot,7);CHECK(rc_pilot_index);
}
static void rc_assert_unpublished(void) {
    CHECK(!actor_components[14]&&!actor_ids[14]&&!dynamic_ships[0].bound_tick&&!dynamic_ships[0].prepared_tick);
    CHECK(!agent_samples[14].agent&&!flight_samples[14].agent&&!params_done[14]);
}
static void rc_identity_tests(void) {
    for(unsigned mode=0;mode<=7;mode++) {if(mode==6)continue;
        rc_reset();rc_begin();rc_lookup_mode=mode;SetLastError(0x1234abcd);
        uint64_t md=(uint64_t)registry_md_order;
        registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
        CHECK(GetLastError()==0x1234abcd&&registry_md_order==(LONG64)md&&registry_creation_identity_count==1);
        registry_creation_identity_record r=registry_creation_identities[0];
        CHECK(r.closed&&r.complete&&r.object==(uintptr_t)object&&r.runtime==registry_creation_runtime);
        CHECK(r.admitted==(mode==0));CHECK(r.error==0 || mode!=0);
        CHECK(!registry_creation_identity_open&&rc_lookup_calls==2);rc_assert_unpublished();
        char out[1536];for(unsigned part=0;part<3;part++)CHECK(registry_creation_identity_format(1,part,out,sizeof(out))>0);
        CHECK(!registry_creation_identity_format(1,0,out,10));
    }
    rc_reset();rc_begin();rc_lookup_mode=6;unsigned caught=0;
    __try {
        __try {registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);CHECK(0);}
        __finally {if(AbnormalTermination())creation_abort(rc_pilot_index);}
    } __except(GetExceptionCode()==0xe0427777?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    CHECK(caught&&!registry_creation_identity_open&&registry_creation_identities[0].closed&&!registry_creation_identities[0].complete);
    CHECK(registry_creation_identities[0].error&RC_UNWIND);rc_assert_unpublished();
    registry_creation_trace_using(rc_pilot_index,2,NULL,NULL);CHECK(!registry_creation_identity_open);
    rc_reset();rc_begin();registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_PARAMS);
    registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    CHECK(!registry_creation_identities[0].admitted&&registry_creation_identities[0].pending_kind==REGISTRY_PARAMS);
}
static void rc_operations(void) {
    rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    registry_origin_scope parent,child;registry_origin_enter(&parent,REGISTRY_ORIGIN_AI,(uintptr_t)(image+0x106e3fb),rc_exec,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);
    registry_pending_update old=registry_pending[0];unsigned before=registry_creation_operation_count;
    for(unsigned kind=1;kind<=17;kind++)registry_record_update(object,*(uint64_t*)(object+8),kind);
    CHECK(registry_pending_count==1&&!memcmp(&old,&registry_pending[0],sizeof(old)));
    CHECK(registry_creation_operation_count==before+17&&registry_creation_kinds==0x1ffff&&registry_creation_unpaired==16);
    CHECK(registry_creation_open==2&&parent.creation_note_count==2);
    registry_origin_enter(&child,REGISTRY_ORIGIN_EVENT,0,NULL,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);CHECK(registry_creation_open==3);
    registry_origin_abort(&child);CHECK(registry_origin_current==&parent&&registry_creation_open==2);
    registry_creation_stop();CHECK(registry_creation_stop_open==2);
    registry_origin_leave(&parent,0x123456789abcdef0ULL);CHECK(!registry_creation_open&&!registry_origin_current);
    CHECK(registry_creation_operations[0].normal&&registry_creation_operations[0].result==0x123456789abcdef0ULL&&registry_creation_operations[0].stop_overlap);
    CHECK(registry_creation_operations[18].unwound&&!registry_creation_operations[18].normal);
    unsigned count=registry_creation_operation_count;registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AGENT);
    CHECK(registry_creation_operation_count==count);rc_assert_unpublished();
    char out[1536];CHECK(registry_creation_status(out,sizeof(out))&&strstr(out,"publication=0 ready=0"));
    for(unsigned i=1;i<=count;i++)CHECK(registry_creation_operation_format(i,out,sizeof(out))>0);
    rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    for(unsigned i=0;i<REGISTRY_CREATION_OPERATIONS+1;i++)registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_PARAMS);
    CHECK(registry_creation_operation_count==REGISTRY_CREATION_OPERATIONS&&registry_creation_dropped==1&&!registry_creation_identity_admitted);
}
static DWORD WINAPI rc_worker(LPVOID unused) {
    (void)unused;registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AGENT);return 0;
}
static void rc_scope_schedules(void) {
    for(unsigned source=1;source<=4;source++)for(unsigned abnormal=0;abnormal<=1;abnormal++) {
        rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
        registry_origin_scope parent,child,sibling;unsigned original_calls=0,caught=0;
        registry_origin_enter(&parent,REGISTRY_ORIGIN_AI,(uintptr_t)(image+0x106e3fb),rc_exec,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
        registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);
        __try {
            registry_origin_enter(&child,source,(uintptr_t)(image+(source==2?0x1066dc6:0x106e3fb)),rc_exec,rc_context,rc_script,0,4,1,tls,(uint64_t*)(tls+0x318));
            __try {
                registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);
                registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);++original_calls;
                if(abnormal)RaiseException(0xe0429999,0,0,NULL);
                registry_origin_leave(&child,0xfedcba9876543210ULL);
            } __finally {if(AbnormalTermination())registry_origin_abort(&child);}
        } __except(GetExceptionCode()==0xe0429999?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
        CHECK(original_calls==1&&caught==abnormal&&registry_origin_current==&parent&&registry_creation_open==1);
        CHECK(registry_creation_operations[1].closed&&registry_creation_operations[2].closed);
        CHECK(registry_creation_operations[1].normal==!abnormal&&registry_creation_operations[1].unwound==abnormal);
        registry_origin_enter(&sibling,source,0,rc_exec,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
        registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);registry_origin_leave(&sibling,9);
        CHECK(registry_origin_current==&parent&&registry_creation_open==1);
        registry_origin_leave(&parent,7);CHECK(!registry_creation_open&&!registry_origin_current);rc_assert_unpublished();
    }
    rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    HANDLE thread=CreateThread(NULL,0,rc_worker,NULL,0,NULL);CHECK(thread&&WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    CHECK(registry_creation_workers==1&&registry_creation_unpaired==1&&(registry_creation_operations[0].error&RC_THREAD));
    rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    registry_origin_scope foreign;*(void**)(rc_context+0x120)=NULL;
    registry_origin_enter(&foreign,REGISTRY_ORIGIN_AI,0,rc_exec,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);CHECK(!registry_creation_open&&registry_creation_unpaired==1);
    CHECK(registry_creation_operations[0].error&RC_SCOPE);registry_origin_leave(&foreign,0);
    rc_reset();rc_begin();registry_creation_trace_using(rc_pilot_index,0,rc_find,rc_map);
    *(uint64_t*)(object+8)^=1;registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_PARAMS);
    CHECK(registry_creation_operations[0].error&RC_LIFETIME);rc_assert_unpublished();
    /* Zero-activity configuration remains closed if STOP won before commit. */
    rc_clear();reset_before_activity();creation_rng_started=0;
    strcpy(initial_state_digest,"91b30261372f883f52286bd23a19ccbabd2c03b60b7d6627de662c731af6bf85");
    registry_creation_stop();CHECK(!rc_config()&&!registry_creation_configured);
}
static void rc_filter_tests(void) {
    EXCEPTION_RECORD record={0};CONTEXT context={0};EXCEPTION_POINTERS p={&record,&context};
    const unsigned sites[]={0xceda1,0xcedcb,0xcedd8,0xcedfc};
    for(unsigned i=0;i<4;i++) {
        record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.NumberParameters=2;record.ExceptionInformation[0]=0;
        record.ExceptionAddress=image+sites[i];context.Rip=(uintptr_t)record.ExceptionAddress;
        CHECK(registry_creation_query_filter(&p)==EXCEPTION_EXECUTE_HANDLER);
        for(unsigned mode=1;mode<=8;mode++){record.ExceptionInformation[0]=mode;CHECK(registry_creation_query_filter(&p)==EXCEPTION_CONTINUE_SEARCH);}
        record.ExceptionInformation[0]=0;context.Rip++;CHECK(registry_creation_query_filter(&p)==EXCEPTION_CONTINUE_SEARCH);context.Rip--;
        const DWORD codes[]={EXCEPTION_STACK_OVERFLOW,0xe06d7363,0xe0427777,EXCEPTION_ILLEGAL_INSTRUCTION};
        for(unsigned n=0;n<4;n++){record.ExceptionCode=codes[n];CHECK(registry_creation_query_filter(&p)==EXCEPTION_CONTINUE_SEARCH);}
    }
    record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.ExceptionAddress=image+0xcee1a;context.Rip=(uintptr_t)record.ExceptionAddress;
    CHECK(registry_creation_query_filter(&p)==EXCEPTION_CONTINUE_SEARCH);
}
static unsigned rc_lua_mode,rc_lua_pushes;static double rc_lua_value;
static const char *rc_lua_input(lua_State *state,int index,size_t *length) {
    (void)state;const char *s=rc_lua_mode?(index==1?manifest:rc_preimage):
        index==1?REGISTRY_CREATION_MANIFEST:index==2?manifest:rc_preimage;
    *length=strlen(s);return s;
}
static void rc_lua_number(lua_State *state,double value){(void)state;++rc_lua_pushes;rc_lua_value=value;}
static void rc_configuration(void) {
    rc_clear();reset_before_activity();creation_rng_started=0;
    strcpy(initial_state_digest,"91b30261372f883f52286bd23a19ccbabd2c03b60b7d6627de662c731af6bf85");
    image[0xcedfc]^=1;CHECK(!rc_config()&&!registry_creation_configured);image[0xcedfc]^=1;
    p_lua_tolstring=rc_lua_input;p_lua_pushnumber=rc_lua_number;p_lua_pushlstring=ack_lua_push;
    rc_lua_mode=0;rc_lua_pushes=0;CHECK(lua_registry_creation_config(NULL)==1&&rc_lua_pushes==1&&rc_lua_value==1);
    rc_lua_mode=1;ack_push_count=0;CHECK(lua_creation_rng_config(NULL)==2&&ack_push_count==2);
    char first[384],second[384],footer[384];strcpy(first,ack_pushes[1]);
    char boot[33];strcpy(boot,creation_rng_boot);uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));
    rc_lua_mode=0;CHECK(lua_registry_creation_config(NULL)==1&&rc_lua_value==1);
    rc_lua_mode=1;ack_push_count=0;CHECK(lua_creation_rng_config(NULL)==2&&ack_push_count==2);strcpy(second,ack_pushes[1]);
    CHECK(!strcmp(boot,creation_rng_boot)&&!memcmp(keys,creation_rng_keys,sizeof(keys))&&!registry_creation_sequence);
    CHECK(creation_rng_ack_status(footer,sizeof(footer)));
    printf("RC_ACK_CONFIG %s\nRC_ACK_FIRST %s\nRC_ACK_SECOND %s\nRC_ACK_STOP %s\nRC_PREIMAGE %s\n",ack_pushes[0],first,second,footer,rc_preimage);
    char bad[1024];strcpy(bad,rc_preimage);strcat(bad,REGISTRY_CREATION_SUFFIX);
    CHECK(!registry_creation_configure(REGISTRY_CREATION_MANIFEST,sizeof(REGISTRY_CREATION_MANIFEST)-1,manifest,sizeof(manifest)-1,bad,strlen(bad)));
    CHECK(!registry_creation_configure(REGISTRY_CREATION_MANIFEST,sizeof(REGISTRY_CREATION_MANIFEST)-1,manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
    CHECK(!registry_creation_configure(REGISTRY_CREATION_MANIFEST,sizeof(REGISTRY_CREATION_MANIFEST),manifest,sizeof(manifest)-1,rc_preimage,sizeof(rc_preimage)-1));
    registry_creation_sequence=1;CHECK(!rc_config());registry_creation_sequence=0;
    registry_creation_stop();CHECK(!rc_config()&&registry_creation_configured);
}
/* Execute the exact native lookup in owned memory. The breakpoint injects an
 * invalid address immediately before one audited load, then restores that
 * original byte. This proves the real SEH/unwind boundary; it is not evidence
 * that an engine registry naturally races or faults at that instruction. */
static uintptr_t rc_fault_site;static unsigned char rc_fault_byte;static unsigned rc_fault_injected;
static LONG CALLBACK rc_fault_inject(EXCEPTION_POINTERS *p) {
    if(p->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT ||
       (uintptr_t)p->ExceptionRecord->ExceptionAddress!=rc_fault_site)return EXCEPTION_CONTINUE_SEARCH;
    *(unsigned char*)rc_fault_site=rc_fault_byte;
    CHECK(FlushInstructionCache(GetCurrentProcess(),(void*)rc_fault_site,1));
    p->ContextRecord->Rip=rc_fault_site;
    if(rc_fault_site==(uintptr_t)(image+0xceda1))p->ContextRecord->Rcx=0;
    else p->ContextRecord->Rdx=0;
    ++rc_fault_injected;return EXCEPTION_CONTINUE_EXECUTION;
}
static void rc_native_faults(void) {
    const unsigned char unwind[]={1,5,2,0,5,0x34,1,0};
    CHECK(hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"));
    CHECK(!memcmp(image+0x2d86450,unwind,sizeof(unwind)));
    RUNTIME_FUNCTION entry={0xced70,0xcee28,0x2d86450};
    CHECK(RtlAddFunctionTable(&entry,1,(DWORD64)(uintptr_t)image));
    DWORD protection;CHECK(VirtualProtect(image+0xce000,4096,PAGE_EXECUTE_READWRITE,&protection));
    void *handler=AddVectoredExceptionHandler(1,rc_fault_inject);CHECK(handler);
    unsigned char table[32]={0};memset(rc_map,0,sizeof(rc_map));*(void**)(rc_map+0x58)=table;
    *(void**)table=object;*(uint32_t*)(table+0x1c)=5u<<16;*(uint16_t*)(table+0x18)=0;
    const unsigned sites[]={0xceda1,0xcedcb,0xcedd8,0xcedfc};
    for(unsigned i=0;i<4;i++) {
        rc_fault_site=(uintptr_t)(image+sites[i]);rc_fault_byte=*(unsigned char*)rc_fault_site;
        *(unsigned char*)rc_fault_site=0xcc;CHECK(FlushInstructionCache(GetCurrentProcess(),(void*)rc_fault_site,1));
        register uintptr_t guard __asm__("rbx")=0xa123b456c789d012ULL;
        __asm__ volatile("" : "+r"(guard));unsigned fault=0;rc_fault_injected=0;
        void *result=registry_creation_query((snapshot_find_fn)(image+0xced70),rc_map,1,&fault);
        __asm__ volatile("" : "+r"(guard));
        CHECK(!result&&fault==1&&rc_fault_injected==1&&guard==0xa123b456c789d012ULL);
        CHECK(*(unsigned char*)rc_fault_site==rc_fault_byte);
        *(unsigned char*)rc_fault_site=0xcc;CHECK(FlushInstructionCache(GetCurrentProcess(),(void*)rc_fault_site,1));
        *(void**)(image+0x6cf1500)=rc_map;creation_token token={0};token.object=1;token.runtime=1;token.true_owner=1;
        registry_creation_identity_record row={0};
        registry_creation_identity_read(&row,&token,(snapshot_find_fn)(image+0xced70),rc_map);
        CHECK(row.lookup==5&&(row.error&RC_LOOKUP)&&!row.presence.recorded&&!row.map_after&&!row.lookup_second);
    }
    unsigned fault=0;CHECK(registry_creation_query((snapshot_find_fn)(image+0xced70),rc_map,1,&fault)==object&&!fault);
    CHECK(RemoveVectoredExceptionHandler(handler));CHECK(RtlDeleteFunctionTable(&entry));
    CHECK(VirtualProtect(image+0xce000,4096,protection,&protection));
}
static void rc_lifecycle(void) {
    rc_reset();*(uint64_t*)(object+8)=0x2000001;
    unsigned char table[32]={0};*(void**)(rc_map+0x58)=table;*(void**)table=object;
    *(uint32_t*)(table+0x1c)=5u<<16;*(uint16_t*)(table+0x18)=1;
    DWORD protection;CHECK(VirtualProtect(image+0xce000,4096,PAGE_EXECUTE_READ,&protection));
    rc_begin();registry_creation_trace(rc_pilot_index,0);
    CHECK(registry_creation_identity_admitted&&registry_creation_identities[0].lookup==2);
    creation_leave_context(rc_pilot_index,0,1,tls);creation_leave_context(rc_init_index,0,0,tls);
    creation_leave_context(rc_generate_index,(uintptr_t)&object_ref,1,tls);
    CHECK(registry_creation_identity_count==3&&!registry_creation_failures);
    prepare_b();uint64_t nav[]={(uintptr_t)controller,(uintptr_t)object,(uintptr_t)fixed,0,1,0};
    unsigned ni=enter(CREATION_NAVIGATION,nav,6);CHECK(ni);
    *(void**)controller=image+0x2c51158;*(void**)(controller+0x28)=object;*(unsigned*)(controller+0xc8)=2;
    uint64_t aa[]={(uintptr_t)agent,(uintptr_t)object,0,0};unsigned ai=enter(CREATION_AGENT,aa,4);CHECK(ai);
    *(void**)(agent+0xef0)=object;*(double*)(agent+0xac0)=100.75;creation_leave_context(ai,(uintptr_t)agent,1,tls);
    *(void**)(controller+0x40)=agent;*(void**)(controller+0x30)=agent+0xad0;creation_leave_context(ni,0,0,tls);
    registry_origin_scope parent;registry_origin_enter(&parent,REGISTRY_ORIGIN_AI,(uintptr_t)(image+0x106e3fb),rc_exec,rc_context,NULL,0,0,0,tls,(uint64_t*)(tls+0x318));
    registry_record_update(object,*(uint64_t*)(object+8),REGISTRY_AI);
    uint64_t setter[]={(uintptr_t)object,(uintptr_t)controller,1};unsigned si=enter(CREATION_SETTER,setter,3);CHECK(si);
    *(void**)(object+0x200)=controller;creation_leave_context(si,0,0,tls);
    CHECK(registry_creation_identity_count==4&&registry_creation_identities[3].relationship==1);
    CHECK(registry_creation_identities[3].open_scopes==1&&!registry_creation_identities[3].admitted&&!creation_selected.b_closed);
    CHECK(!registry_creation_identities[3].error&&!creation_failures);rc_assert_unpublished();
    registry_origin_leave(&parent,0x1234);registry_creation_stop();
    char out[1536];puts("RC_TEST_BEGIN");
    printf("NATIVE_GATE REGISTRY_CREATION_CONFIG %s\n",REGISTRY_CREATION_MANIFEST);
    for(unsigned i=1;i<=4;i++)for(unsigned p=0;p<3;p++){CHECK(registry_creation_identity_format(i,p,out,sizeof(out)));printf("NATIVE_GATE REGISTRY_CREATION_IDENTITY %s\n",out);}
    CHECK(registry_creation_operation_format(1,out,sizeof(out)));printf("NATIVE_GATE REGISTRY_CREATION_OPERATION %s\n",out);
    CHECK(registry_creation_status(out,sizeof(out)));printf("NATIVE_GATE REGISTRY_CREATION_STOP %s\n",out);puts("RC_TEST_END");
    CHECK(VirtualProtect(image+0xce000,4096,protection,&protection));
}
__declspec(dllexport) int registry_creation_test(const char *path) {
    CHECK(creation_rng_test(path)==0);rc_identity_tests();rc_operations();rc_scope_schedules();rc_filter_tests();rc_native_faults();rc_configuration();rc_lifecycle();
    rc_clear();tls_slots[0]=old_tls;
    printf("Provisional registry identity/operation evidence: %u cumulative active checks PASS; publication unavailable\n",checks);return 0;
}
__declspec(dllexport) int registry_creation_force_fail(void) {CHECK(0);return 0;}
