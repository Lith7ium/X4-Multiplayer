/* Production observer, owned synthetic memory and executable ABI probes only.
 * Pinned EXE ranges are read into owned fixture memory; its registry resolver
 * executes only on synthetic pages. No game/module launch or file writes. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "test_lockstep_transition_identity.h"
static unsigned char transfer_prefix[5]={0x48,0x89,0x5c,0x24,0x10};
static unsigned char activation_prefix[7]={0x48,0x83,0xec,0x38,0x83,0xf9,0x08};
static unsigned char activation_eighth=0x0f;
static unsigned calls,activation_calls,publication_calls;
static unsigned char *object,*old_controller,*next_controller,*agent,*model,*faction;
static unsigned char native_tls[0x320];
static void reset_events(void) {
    memset(transition_events,0,sizeof(transition_events));transition_count=transition_order=transition_failures=0;
    transition_excluded=transition_dropped=transition_open=transition_peak=transition_restore_open=0;
    transition_direct_calls=transition_direct_returns=transition_direct_unwinds=0;
    transition_closing=transition_sites=transition_rows=0;transition_enabled=stats.active=1;
    assert(TlsSetValue(transition_tls,NULL));
}
static void fixture(void) {
    memset(old_controller,0,4096);memset(next_controller,0,4096);memset(agent,0,4096);
    *(void**)object=image+0x2b7feb8;*(uint64_t*)(object+8)=0x123456789abcULL;
    *(void**)(object+0x348)=faction;*(void**)(object+0x200)=old_controller;
    *(void**)(image+0x2b7feb8+0x1620)=image+0x352b10;
    memcpy(faction+0x10,"argon",6);*(uint64_t*)(faction+0x20)=5;*(uint64_t*)(faction+0x28)=15;
    *(void**)old_controller=image+0x2c51158;*(void**)(old_controller+0x28)=object;
    *(void**)(old_controller+0x30)=agent+0xad0;*(void**)(old_controller+0x40)=agent;old_controller[0x58]=1;
    *(void**)(agent+0xef0)=object;*(void**)(agent+0xf10)=model;
    *(void**)model=image+0x2c4e9c8;*(void**)(model+0x10)=agent;
    world_trace_watch_count=1;world_trace_watches[0]=(world_trace_watch){0x176a13,0x123456789abcULL,object,old_controller,agent};
    world_trace_imports[0]=1;world_trace_bound=1;assert(world_identity_record(0x123456789abcULL,0x176a13));
}
static uintptr_t native_transfer(void *next,void *old,unsigned char flag,int extra) {
    ++calls;assert(next==next_controller && old==old_controller && flag<=1 && extra>=0 && extra<=1);
    memcpy(next,old,0xf0);*((unsigned char*)old+0x58)=0;*((unsigned char*)next+0x58)=1;agent[0xf79]=flag;
    return (uintptr_t)next;
}
static void native_publish(void *o,void *next,unsigned char detach) {
    ++publication_calls;assert(o==object && next==next_controller && detach==1);
    *(void**)(object+0x200)=next;DWORD ignored;assert(VirtualProtect(old_controller,4096,PAGE_NOACCESS,&ignored));
}
static void native_enqueue(void *manager,uint64_t runtime,void *command) {
    assert(manager==object && runtime==0x123456789abcULL && command);++calls;
}
static void native_model(void *command,void *a) {
    assert(a==agent && *(uint32_t*)((unsigned char*)command+8)==0);++calls;*(void**)(agent+0xf10)=NULL;
}
static unsigned char command[16];
static FILE *evidence_file;
static unsigned evidence_row;
static double evidence_number(lua_State *L,int index) {(void)L;assert(index==1);return evidence_row;}
static void evidence_string(lua_State *L,const char *value,size_t length) {
    (void)L;assert(length==strlen(value) && length<TRANSITION_ROW_CAP);
    fputs(evidence_row?"[X4LAN] NATIVE_GATE ":"[X4LAN] NATIVE_GATE PLAYER_TRANSITION_STOP ",evidence_file);
    assert(fwrite(value,1,length,evidence_file)==length);fputc('\n',evidence_file);
}
static void export_evidence(void) {
    const char *path=getenv("X4LAN_TRANSITION_TEST_LOG");if(!path) return;
    evidence_file=fopen(path,"w");assert(evidence_file);transition_restore();transition_started=1;ui_thread=GetCurrentThreadId();
    p_lua_tonumber=evidence_number;p_lua_pushlstring=evidence_string;
    for(evidence_row=1;evidence_row<=transition_saved_bound;evidence_row++) assert(lua_player_transition_watch(NULL)==1);
    for(evidence_row=1;evidence_row<=transition_ready();evidence_row++) assert(lua_player_transition_row(NULL)==1);
    for(evidence_row=1;evidence_row<=transition_saved_tickets_count;evidence_row++) assert(lua_player_transition_ticket(NULL)==1);
    evidence_row=0;assert(lua_player_transition_status(NULL)==1);assert(!fclose(evidence_file));transition_started=0;
}
static void native_activate(int context,unsigned char active) {
    ++activation_calls;assert(context==8 && active==1);
    world_queue_enqueue_observe(native_enqueue,object,0x123456789abcULL,command,(uintptr_t)(image+0xeac95a),native_tls);
    image[0x2f4b828]=1;
}
static void test_publication_abi(void) {
    fixture();reset_events();native_transfer(next_controller,old_controller,1,0);
    *(void**)next_controller=image+0x2c39488;
    /* Execute the production vtable wrapper through its actual CAS slot. The
     * owned replacement for the original forwards the three native arguments. */
    unsigned char *native=image+0x6cd8b0;native[0]=0x49;native[1]=0xbb;void *callee=(void*)native_publish;memcpy(native+2,&callee,8);
    native[10]=0x41;native[11]=0xff;native[12]=0xe3;DWORD page_old;
    assert(VirtualProtect(native,16,PAGE_EXECUTE_READWRITE,&page_old));FlushInstructionCache(GetCurrentProcess(),native,16);
    transition_original[2]=(uintptr_t)native;transition_patched[2]=(uintptr_t)transition_publish;
    *(uintptr_t*)(image+TRANSITION_PUBLICATION)=transition_original[2];assert(transition_swap(2,transition_original[2],transition_patched[2]));
    ((transition_publish_fn)*(void**)(image+TRANSITION_PUBLICATION))(object,next_controller,1);
    assert(transition_swap(2,transition_patched[2],transition_original[2]));DWORD page_ignored;
    assert(VirtualProtect(native,16,page_old,&page_ignored));
    /* The synthetic caller is outside X4, so preserve that explicit error. */
    assert(transition_count==1 && transition_events[0].errors==1 && transition_failures==1);
    assert(transition_events[0].after.controller==(uintptr_t)next_controller);
    DWORD ignored;assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&ignored));
}
static void native_publish_activate(void *o,void *next,unsigned char detach) {
    native_publish(o,next,detach);transition_activate_observe(native_activate,8,1,(uintptr_t)(image+0x12556f0));
}
static void test_transitions(unsigned nested) {
    fixture();reset_events();transition_state s;assert(transition_read(object,NULL,&s) && s.saved==0x176a13 && !strcmp(s.owner,"argon"));
    assert(transition_transfer_observe(native_transfer,next_controller,old_controller,1,0,(uintptr_t)(image+0xd83b36))==(uintptr_t)next_controller);
    assert(transition_ready()==1 && !transition_failures && transition_events[0].after.player==1 && !transition_events[0].old_owns_after);
    *(void**)next_controller=image+0x2c39488;
    memset(world_queue_events,0,sizeof(world_queue_events));memset(world_queue_lifetimes,0,sizeof(world_queue_lifetimes));
    world_queue_count=world_queue_order=0;world_queue_enabled=1;world_queue_restoring=0;world_trace_last_tick=120;
    world_queue_model_begin=transition_queue_begin;world_queue_model_end=transition_queue_end;
    *(void**)(image+0x6cf21c8)=object;*(void**)command=image+WORLD_QUEUE_MODEL_VTABLE;*(uint32_t*)(command+8)=0;
    transition_publish_observe(nested?native_publish_activate:native_publish,object,next_controller,1,(uintptr_t)(image+0x450291));
    assert(transition_ready()==(nested?4u:2u) && !transition_failures && transition_events[1].after.controller==(uintptr_t)next_controller);
    assert(!world_trace_identity(&world_trace_watches[0])); /* strict failure retained */
    DWORD ignored;assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&ignored));
    if(!nested) transition_activate_observe(native_activate,8,1,(uintptr_t)(image+0x12556f0));
    assert(transition_ready()==4 && transition_events[3].kind==TR_MODEL_ENQUEUE && transition_events[3].parent==3);
    assert(transition_events[3].transfer==1 && transition_events[3].publication==2 && transition_events[3].enqueue==1);
    /* Request and callback completed, model remains. No early completion. */
    assert(*(void**)(agent+0xf10)==model && transition_events[2].active_after==1);
    world_queue_dispatch_observe(native_model,command,agent,(uintptr_t)(image+0xeacf66),native_tls);
    assert(transition_ready()==5 && transition_events[4].kind==TR_MODEL_DISPATCH && !transition_events[4].errors);
    assert(transition_events[4].before.model==(uintptr_t)model && !transition_events[4].after.model && transition_events[4].after.player==1);
    assert(transition_events[4].queue_errors && world_queue_events[1].errors && transition_events[4].enqueue==1);
    assert(!transition_open && !world_queue_open && world_queue_lifetimes[0].state==3);
    export_evidence();
    /* Full uint32 mode decode: byte-only truncation must be refused. */
    world_queue_event q={0};*(uint32_t*)(command+8)=0x100;assert(!world_queue_command(&q,command));
    for(unsigned mode=0;mode<=6;mode++) {memset(&q,0,sizeof(q));*(uint32_t*)(command+8)=mode;assert(world_queue_command(&q,command) && q.order_value==mode);}
    world_queue_enabled=0;world_queue_model_begin=NULL;world_queue_model_end=NULL;
    puts("transition transfer/publication/activation/deferred dispatch and preserved raw lifetime failure PASS");
}
static unsigned receipt_fault;
static void native_model_fault(void *input,void *a) {
    assert(a==agent);++calls;*(void**)(agent+0xf10)=NULL;
    if(receipt_fault==2) *(uint32_t*)((unsigned char*)input+8)=1; /* changed after initial typed copy */
    if(receipt_fault==3) world_queue_lifetimes[0].runtime^=1; /* wrong recipient before retirement */
}
static void test_receipt_failures(void) {
    unsigned char *storage=VirtualAlloc(NULL,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(storage);
    unsigned char *input=storage+4096-8;DWORD old;
    for(receipt_fault=0;receipt_fault<5;receipt_fault++) {
        fixture();reset_events();*(void**)old_controller=image+0x2c39488;
        memset(world_queue_events,0,sizeof(world_queue_events));memset(world_queue_lifetimes,0,sizeof(world_queue_lifetimes));
        world_queue_count=world_queue_order=world_queue_failures=0;world_queue_enabled=1;world_queue_restoring=0;world_trace_last_tick=120;
        world_queue_model_begin=transition_queue_begin;world_queue_model_end=transition_queue_end;
        *(void**)(image+0x6cf21c8)=object;*(void**)input=image+WORLD_QUEUE_MODEL_VTABLE;memset(input+8,0,8);
        if(receipt_fault==4) assert(VirtualProtect(storage+4096,4096,PAGE_NOACCESS,&old));
        unsigned before=calls;
        world_queue_enqueue_observe(native_enqueue,object,0x123456789abcULL,input,(uintptr_t)(image+0xeac95a),native_tls);
        assert(world_queue_events[0].receipt_valid==(receipt_fault!=4));
        if(receipt_fault==4) assert(VirtualProtect(storage+4096,4096,PAGE_READWRITE,&old));
        if(receipt_fault==0) world_queue_lifetimes[0].state=0; /* absent live command token */
        if(receipt_fault==1) assert(VirtualProtect(storage+4096,4096,PAGE_NOACCESS,&old));
        world_queue_dispatch_observe(native_model_fault,input,agent,(uintptr_t)(image+0xeacf66),native_tls);
        assert(calls==before+2 && !world_queue_open && !transition_open && !*(void**)(agent+0xf10));
        assert(world_queue_events[1].order_type==WORLD_QUEUE_MODEL_VTABLE && !world_queue_events[1].receipt_valid);
        assert(!transition_events[1].queue_valid && (transition_events[1].errors&32));
        if(receipt_fault==1) {
            /* The vtable was readable but payload was not: mode defaults0.
             * This must never look like a verified native mode0 completion. */
            assert(world_queue_events[1].order_value==0 && (world_queue_events[1].errors&8));
            assert(VirtualProtect(storage+4096,4096,PAGE_READWRITE,&old));
        }
        if(receipt_fault==3) assert(world_queue_lifetimes[0].state==2); /* failed exact retirement stays outstanding */
    }
    world_queue_enabled=0;world_queue_model_begin=NULL;world_queue_model_end=NULL;VirtualFree(storage,0,MEM_RELEASE);
    puts("transition typed receipt: partial read/default0, missing token, changed input/recipient all refuse PASS");
}
static void nested_leaf(int context,unsigned char active) {
    assert(context==7 && active==1);transition_scope *scope=TlsGetValue(transition_tls);assert(scope && !scope->event);++activation_calls;
}
static void nested_activation(int context,unsigned char active) {
    assert(context==8 && active==1);transition_scope *parent=TlsGetValue(transition_tls);assert(parent && parent->event);
    transition_activate_observe(nested_leaf,7,1,(uintptr_t)(image+1));assert(TlsGetValue(transition_tls)==parent);++activation_calls;
}
static void capacity_activate(int context,unsigned char active) {assert(context==8 && active==1);++activation_calls;}
static void test_failures(void) {
    fixture();reset_events();transition_activate_observe(nested_activation,8,1,(uintptr_t)(image+1));
    assert(transition_count==1 && transition_excluded==1 && !TlsGetValue(transition_tls));
    transition_state s;*(uint64_t*)(object+8)^=0x100000000ULL;assert(!transition_read(object,NULL,&s));*(uint64_t*)(object+8)^=0x100000000ULL;
    world_trace_imports[0]=2;assert(!transition_read(object,NULL,&s));world_trace_imports[0]=1;
    world_trace_watches[1]=world_trace_watches[0];world_trace_imports[1]=1;world_trace_watch_count=2;
    assert(!transition_read(object,NULL,&s));world_trace_watch_count=1;
    *(void**)(old_controller+0x40)=NULL;assert(!transition_read(object,NULL,&s));*(void**)(old_controller+0x40)=agent;
    faction[0x10]='!';assert(!transition_read(object,NULL,&s));faction[0x10]='a';
    /* Full owner mismatch must poison a claimed event, with original once. */
    faction[0x10]='!';unsigned transfer_calls=calls;
    assert(transition_transfer_observe(native_transfer,next_controller,old_controller,0,1,(uintptr_t)(image+1))==(uintptr_t)next_controller);
    assert(calls==transfer_calls+1 && transition_events[1].errors && transition_failures==1);faction[0x10]='a';
    transition_count=TRANSITION_MAX;unsigned before=activation_calls;
    transition_activate_observe(capacity_activate,8,1,(uintptr_t)(image+1));
    assert(activation_calls==before+1 && transition_count==TRANSITION_MAX+1 && transition_dropped==1 && !transition_open);
}
static HANDLE entered,resume;
static uintptr_t blocked_transfer(void *next,void *old,unsigned char flag,int extra) {
    assert(SetEvent(entered));assert(WaitForSingleObject(resume,5000)==WAIT_OBJECT_0);return native_transfer(next,old,flag,extra);
}
static DWORD WINAPI stop_worker(void *unused) {
    (void)unused;assert(transition_transfer_observe(blocked_transfer,next_controller,old_controller,1,0,(uintptr_t)(image+1))==(uintptr_t)next_controller);return 0;
}
static void test_stop(void) {
    fixture();reset_events();entered=CreateEventW(NULL,TRUE,FALSE,NULL);resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(entered && resume);
    for(unsigned i=0;i<3;i++) {transition_original[i]=123;transition_patched[i]=456;*(uint64_t*)(image+transition_entries[i])=456;}
    transition_sites=7;HANDLE worker=CreateThread(NULL,0,stop_worker,NULL,0,NULL);assert(worker && WaitForSingleObject(entered,5000)==WAIT_OBJECT_0);
    assert(transition_open==1 && !transition_events[0].complete);transition_restore();
    assert(!transition_sites && !transition_enabled && transition_restore_open==1 && !transition_admit());
    unsigned before=activation_calls;transition_activate_observe(capacity_activate,8,1,(uintptr_t)(image+1));
    assert(activation_calls==before+1 && transition_count==1 && transition_open==1);
    assert(SetEvent(resume) && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    assert(transition_open==0 && transition_restore_open==1 && transition_events[0].complete && !transition_events[0].errors);
    CloseHandle(worker);CloseHandle(entered);CloseHandle(resume);
    /* Foreign entry is retained and restoration failure remains visible. */
    transition_sites=1;*(uint64_t*)(image+TRANSITION_TRANSFER)=789;transition_restore();assert(transition_sites==1 && *(uint64_t*)(image+TRANSITION_TRANSFER)==789);
    transition_sites=0;puts("transition concurrent stop/original open/owned restoration PASS");
}
static uintptr_t abi_next,abi_old;static unsigned abi_flag;static int abi_extra;
static unsigned alignment;static uint64_t resume_flags;
static transition_transfer_fn abi_trampoline;
static transition_activate_fn abi_activation;
static uintptr_t abi_native(void *next,void *old,unsigned char flag,int extra) {
    assert((uintptr_t)next==abi_next && (uintptr_t)old==abi_old && flag==abi_flag && extra==abi_extra);++calls;return 0xfedcba9876543210ULL;
}
static uintptr_t abi_hook(void *next,void *old,unsigned char flag,int extra) {return transition_transfer_observe(abi_trampoline,next,old,flag,extra,0);}
static void abi_activate(int context,unsigned char active) {assert((context==7 || context==8) && active==0xfe);++activation_calls;}
static void abi_activation_hook(int context,unsigned char active) {transition_activate_observe(abi_activation,context,active,0);}
static void emit_address(unsigned char *code,unsigned *n,uintptr_t value) {memcpy(code+*n,&value,8);*n+=8;}
static void test_abi(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);transition_enabled=0;
    memcpy(code,transfer_prefix,5);unsigned n=5;
    const unsigned char prefix[]={0x48,0x83,0xec,0x28,0x48,0x89,0xe0,0x83,0xe0,0x0f,0x49,0xba};
    memcpy(code+n,prefix,sizeof(prefix));n+=sizeof(prefix);emit_address(code,&n,(uintptr_t)&alignment);
    const unsigned char call[]={0x41,0x89,0x02,0x48,0xb8};memcpy(code+n,call,sizeof(call));n+=sizeof(call);emit_address(code,&n,(uintptr_t)abi_native);
    const unsigned char tail[]={0xff,0xd0,0x48,0x83,0xc4,0x28,0x48,0x8b,0x5c,0x24,0x10,0xc3};memcpy(code+n,tail,sizeof(tail));
    assert(transition_emit(code+256,code+288,code,(void*)abi_hook,0));abi_trampoline=(transition_transfer_fn)(code+256);
    uint64_t original;memcpy(&original,code,8);code[0]=0xe9;int32_t rel=283;memcpy(code+1,&rel,4);
    abi_next=0x123456789abcULL;abi_old=0xabcdef012345ULL;abi_flag=0xfe;abi_extra=INT32_MIN;unsigned count=calls;
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    register uintptr_t keep_bx __asm__("rbx")=0x123456789abcdef0ULL;
    register uintptr_t keep_r12 __asm__("r12")=0x8877665544332211ULL;
    typedef unsigned long long vector128 __attribute__((vector_size(16)));
    register vector128 keep_xmm __asm__("xmm6")={0x0123456789abcdefULL,0xfedcba9876543210ULL};
    __asm__ volatile("" : "+r"(keep_bx),"+r"(keep_r12),"+x"(keep_xmm));
    assert(((transition_transfer_fn)code)((void*)abi_next,(void*)abi_old,(unsigned char)abi_flag,abi_extra)==0xfedcba9876543210ULL && calls==count+1 && alignment==0);
    __asm__ volatile("" : "+r"(keep_bx),"+r"(keep_r12),"+x"(keep_xmm));
    assert(keep_bx==0x123456789abcdef0ULL && keep_r12==0x8877665544332211ULL &&
        keep_xmm[0]==0x0123456789abcdefULL && keep_xmm[1]==0xfedcba9876543210ULL);
    assert(!memcmp(code+5,(unsigned char*)&original+5,3));
    unsigned char *a=code+512;memcpy(a,activation_prefix,7);n=7;
    const unsigned char flags[]={0x9c,0x58,0x49,0xba};memcpy(a+n,flags,sizeof(flags));n+=sizeof(flags);emit_address(a,&n,(uintptr_t)&resume_flags);
    const unsigned char flag_store[]={0x49,0x89,0x02,0x48,0xb8};memcpy(a+n,flag_store,sizeof(flag_store));n+=sizeof(flag_store);emit_address(a,&n,(uintptr_t)abi_activate);
    const unsigned char finish[]={0xff,0xd0,0x48,0x83,0xc4,0x38,0xc3};memcpy(a+n,finish,sizeof(finish));
    assert(transition_emit(code+2880,code+2912,a,(void*)abi_activation_hook,1));abi_activation=(transition_activate_fn)(code+2880);
    a[0]=0xe9;rel=(int32_t)((code+2912)-(a+5));memcpy(a+1,&rel,4);a[5]=a[6]=0x90;
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    ((transition_activate_fn)a)(8,0xfe);assert(resume_flags&0x40);((transition_activate_fn)a)(7,0xfe);assert(!(resume_flags&0x40));
    unsigned char *saved_thunk=thunk;thunk=code;assert(transition_register_unwind(RtlAddFunctionTable));
    DWORD64 base=0;PRUNTIME_FUNCTION entry=RtlLookupFunctionEntry((DWORD64)(uintptr_t)(code+2887),&base,NULL);assert(entry && base==(DWORD64)(uintptr_t)code);
    const unsigned pcs[]={4,7,17};
    for(unsigned i=0;i<3;i++) {
        uint64_t stack[16]={0};stack[7]=0x1234567890abcdefULL;CONTEXT context={0};context.Rip=(DWORD64)(uintptr_t)(code+2880+pcs[i]);context.Rsp=(DWORD64)(uintptr_t)stack;
        void *handler=NULL;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,entry,&context,&handler,&frame,NULL);
        assert(context.Rip==stack[7] && context.Rsp==(DWORD64)(uintptr_t)stack+0x40);
    }
    assert(RtlDeleteFunctionTable(&transition_unwind));transition_unwind_base=0;thunk=saved_thunk;VirtualFree(code,0,MEM_RELEASE);
    puts("transition actual-prefix ABI/full RAX/argument/alignment/CMP flags/stack unwind PASS");
}
static unsigned patch_failure;
static uint64_t patch_words[3];
static int patch_swap(unsigned site,uint64_t expected,uint64_t value) {
    if(site==patch_failure && value==transition_patched[site]) return 0;
    if(patch_words[site]!=expected) return 0;patch_words[site]=value;return 1;
}
static void test_frozen_and_patch(void) {
    unsigned char *saved_thunk=thunk;
    memcpy(image+TRANSITION_TRANSFER,transfer_prefix,5);memcpy(image+TRANSITION_ACTIVATION,activation_prefix,7);image[TRANSITION_ACTIVATION+7]=activation_eighth;
    /* Use the production allocation/seal path. A subpage inside the fake image
     * has a different AllocationBase and correctly fails the shared RX guard. */
    thunk=make_thunk();assert(thunk);unsigned char expected[128];memcpy(expected,thunk+2816,128);
    DWORD old;assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));
    assert(!transition_prepare()); /* writable page never accepted */
    assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(transition_prepare());
    assert(((unsigned char*)&transition_patched[1])[7]==activation_eighth && ((unsigned char*)&transition_patched[1])[5]==0x90 && ((unsigned char*)&transition_patched[1])[6]==0x90);
    assert(!memcmp(expected,thunk+2816,128));MEMORY_BASIC_INFORMATION region;
    assert(VirtualQuery(thunk,&region,sizeof(region)) && region.Protect==PAGE_EXECUTE_READ);
    assert(!thunk_bytes_match(4090,expected,128) && !thunk_bytes_match(0,expected,0));
    assert(VirtualProtect(thunk,4096,PAGE_READWRITE,&old));thunk[2848]^=1;
    assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old));assert(!transition_prepare());
    assert(RtlDeleteFunctionTable(&transition_unwind));transition_unwind_base=0;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=saved_thunk;
    for(patch_failure=0;patch_failure<3;patch_failure++) {
        transition_sites=0;for(unsigned i=0;i<3;i++) {transition_original[i]=patch_words[i]=100+i;transition_patched[i]=200+i;}
        assert(!transition_patch(patch_swap) && !transition_sites);
        for(unsigned i=0;i<3;i++) assert(patch_words[i]==transition_original[i]);
    }
    assert(transition_patch(patch_swap) && transition_sites==7);transition_sites=0;
    puts("transition immutable RX page/tamper refusal/partial install rollback PASS");
}
static HANDLE configure_entered,configure_resume,configure_stop_requested,configure_stop_done;
static unsigned configure_pause,configure_swaps,configure_prepares,configure_locked;
static int configure_result;
static int configure_prepare_paused(void) {
    ++configure_prepares;int ok=transition_prepare();assert(ok);
    if(!configure_pause) {assert(SetEvent(configure_entered));assert(WaitForSingleObject(configure_resume,5000)==WAIT_OBJECT_0);}
    return ok;
}
static int configure_swap_paused(unsigned site,uint64_t expected,uint64_t value) {
    int ok=transition_swap(site,expected,value);assert(ok);
    if(value==transition_patched[site] && ++configure_swaps==configure_pause) {
        assert(SetEvent(configure_entered));assert(WaitForSingleObject(configure_resume,5000)==WAIT_OBJECT_0);
    }
    return ok;
}
static DWORD WINAPI configure_worker(void *unused) {
    (void)unused;configure_result=transition_configure_transaction(configure_prepare_paused,configure_swap_paused);return 0;
}
static DWORD WINAPI configure_stopper(void *unused) {
    (void)unused;InterlockedExchange((volatile LONG*)&stats.active,0);
    /* Probe the actual production lock while installer is deliberately paused.
     * This distinguishes a blocked STOP from a scheduler-delayed STOP without
     * relying on an arbitrary sleep. */
    configure_locked=!TryAcquireSRWLockExclusive(&transition_lock);
    if(!configure_locked) ReleaseSRWLockExclusive(&transition_lock);
    assert(SetEvent(configure_stop_requested));transition_restore();assert(SetEvent(configure_stop_done));return 0;
}
static void test_configure_stop(void) {
    unsigned char *saved_thunk=thunk;
    memcpy(image+TRANSITION_TRANSFER,transfer_prefix,5);memcpy(image+TRANSITION_ACTIVATION,activation_prefix,7);
    image[TRANSITION_ACTIVATION+7]=activation_eighth;thunk=make_thunk();assert(thunk);
    for(configure_pause=0;configure_pause<=3;configure_pause++) {
        reset_events();transition_enabled=transition_started=0;configure_swaps=configure_prepares=configure_locked=0;
        world_queue_model_begin=NULL;world_queue_model_end=NULL;
        memcpy(image+TRANSITION_TRANSFER,transfer_prefix,5);memcpy(image+TRANSITION_ACTIVATION,activation_prefix,7);
        image[TRANSITION_ACTIVATION+7]=activation_eighth;*(void**)(image+TRANSITION_PUBLICATION)=image+0x6cd8b0;
        uint64_t originals[3];for(unsigned i=0;i<3;i++) memcpy(&originals[i],image+transition_entries[i],8);
        configure_entered=CreateEventW(NULL,TRUE,FALSE,NULL);configure_resume=CreateEventW(NULL,TRUE,FALSE,NULL);
        configure_stop_requested=CreateEventW(NULL,TRUE,FALSE,NULL);configure_stop_done=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(configure_entered && configure_resume && configure_stop_requested && configure_stop_done);
        HANDLE installer=CreateThread(NULL,0,configure_worker,NULL,0,NULL);assert(installer);
        assert(WaitForSingleObject(configure_entered,5000)==WAIT_OBJECT_0);
        HANDLE stopper=CreateThread(NULL,0,configure_stopper,NULL,0,NULL);assert(stopper);
        assert(WaitForSingleObject(configure_stop_requested,5000)==WAIT_OBJECT_0);
        assert(configure_locked && WaitForSingleObject(configure_stop_done,0)==WAIT_TIMEOUT);
        assert(!transition_enabled && configure_swaps==configure_pause && !transition_open);
        assert(SetEvent(configure_resume));assert(WaitForSingleObject(installer,5000)==WAIT_OBJECT_0);
        assert(WaitForSingleObject(stopper,5000)==WAIT_OBJECT_0 && WaitForSingleObject(configure_stop_done,0)==WAIT_OBJECT_0);
        assert(configure_result==0 && configure_prepares==1 && !transition_enabled && transition_closing && !transition_sites);
        assert(!transition_admit() && !world_queue_model_begin && !world_queue_model_end);
        for(unsigned i=0;i<3;i++) assert(*(uint64_t*)(image+transition_entries[i])==originals[i]);
        /* Closed remains closed even if an unrelated caller rewrites active. */
        stats.active=1;assert(!transition_configure_transaction(configure_prepare_paused,configure_swap_paused) && configure_prepares==1);
        CloseHandle(installer);CloseHandle(stopper);CloseHandle(configure_entered);CloseHandle(configure_resume);
        CloseHandle(configure_stop_requested);CloseHandle(configure_stop_done);
    }
    assert(RtlDeleteFunctionTable(&transition_unwind));transition_unwind_base=0;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=saved_thunk;transition_started=0;
    puts("transition STOP after prepare/each of3 CAS installs: no reopen or stranded hooks PASS");
}
#include "test_lockstep_transition_watch.h"
#include "test_lockstep_player_origin_native.h"
static void load_image(const char *path) {
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0 && length<512*1024*1024 && !fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);assert(bytes && fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);assert(hash_ok(bytes,(ULONG)length,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(void*)bytes;IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE && nt->OptionalHeader.SizeOfImage==0x6fa9000);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=&sections[i];assert((size_t)s->PointerToRawData+s->SizeOfRawData<=(size_t)length && (uint64_t)s->VirtualAddress+s->SizeOfRawData<=0x6fa9000);
        memcpy(image+s->VirtualAddress,bytes+s->PointerToRawData,s->SizeOfRawData);
    }
    assert(*(uint64_t*)(image+TRANSITION_PUBLICATION)==nt->OptionalHeader.ImageBase+0x6cd8b0);
    *(void**)(image+TRANSITION_PUBLICATION)=image+0x6cd8b0;assert(transition_pins());
    assert(*(uint64_t*)(image+0x2c51c40)==nt->OptionalHeader.ImageBase+0xe979d0);
    *(void**)(image+0x2c51c40)=image+0xe979d0;assert(world_queue_model_pins());
    assert(!memcmp(image+TRANSITION_TRANSFER,transfer_prefix,5) && !memcmp(image+TRANSITION_ACTIVATION,activation_prefix,7));
    memcpy(transfer_prefix,image+TRANSITION_TRANSFER,5);memcpy(activation_prefix,image+TRANSITION_ACTIVATION,7);
    activation_eighth=image[TRANSITION_ACTIVATION+7];assert(activation_eighth==0x0f);
    test_transition_registry_identity(1);
    test_saved_manifest_pins();
    origin_test_load();
    free(bytes);memset(image,0,0x6fa9000);puts("transition complete actual-image hash and full function pins PASS");
}
int main(int argc,char **argv) {
    assert(argc<=2);image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    if(argc==2) load_image(argv[1]);
    else test_transition_registry_identity(0);
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);
    unsigned char **allocations[]={&object,&old_controller,&next_controller,&agent,&model,&faction};
    for(unsigned i=0;i<6;i++) {*allocations[i]=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(*allocations[i]);}
    transition_tls=TlsAlloc();world_queue_dispatch_tls=TlsAlloc();assert(transition_tls!=TLS_OUT_OF_INDEXES && world_queue_dispatch_tls!=TLS_OUT_OF_INDEXES);
    assert(!transition_configure());test_abi();test_frozen_and_patch();test_configure_stop();test_publication_abi();test_transitions(0);test_transitions(1);test_receipt_failures();test_stop();
    /* Source bounds and rejection tests are independent of raw native success. */
    test_failures();
    test_saved_watch_and_queue();
    test_player_origin_native();
    transition_event maximum;memset(&maximum,0xff,sizeof(maximum));memset(maximum.before.owner,'a',63);maximum.before.owner[63]=0;
    memcpy(maximum.after.owner,maximum.before.owner,64);char row[TRANSITION_ROW_CAP];int n=transition_format(&maximum,UINT32_MAX,row,sizeof(row));
    assert(n>0 && n<1900 && strlen(row)==(size_t)n);char tiny[8];assert(transition_format(&maximum,1,tiny,sizeof(tiny))==-1 && tiny[7]==0);
    for(unsigned i=0;i<6;i++) VirtualFree(*allocations[i],0,MEM_RELEASE);
    TlsFree(transition_tls);TlsFree(world_queue_dispatch_tls);free(world_identities);VirtualFree(image,0,MEM_RELEASE);
    puts("production transition observer offline tests PASS; no native game or independent player control");return 0;
}
