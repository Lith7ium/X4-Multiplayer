/* Included by the transition harness: reuse its owned ship/registry fixture. */
static unsigned char origin_test_word[16]={0x0f,0x58,0x15,0x09,0x2b,0xb1,0x02,0xe8,0xe4,0xe8,0x7e,0x00,0x48,0x8b,0x74,0x24};
static unsigned char origin_test_call[31],origin_test_adapter[24],origin_test_dispatch[19],origin_test_indirect[3];
static int origin_test_actual;
static unsigned char *origin_test_player,*origin_test_universe,*origin_test_teb,*origin_test_event,*origin_test_code;
static uintptr_t origin_test_tls_pointer;
static unsigned origin_test_posts,origin_test_deliveries,origin_test_immediate,origin_test_blocked;
static unsigned origin_test_block_delivery;
static unsigned origin_test_raise_post,origin_test_raise_delivery;
#define ORIGIN_TEST_EXCEPTION 0xe0424242u
static unsigned origin_test_post_alignment,origin_test_delivery_alignment;
static const uint64_t origin_test_post_result=0xfedcba98765432abULL,origin_test_delivery_result=0xabcdeffedcba9876ULL;
static double origin_test_due=30.01;
static HANDLE origin_test_entered,origin_test_resume;
typedef uint64_t (*origin_test_adapter_fn)(void *,void *,double,unsigned,void *);
static origin_test_adapter_fn origin_test_invoke;
static double origin_test_row;
static unsigned origin_test_nil;
static char origin_test_text[1536];
static FILE *origin_test_export;
static unsigned origin_test_export_transition_status;
static double origin_test_number(lua_State *L,int index) {(void)L;assert(index==1);return origin_test_row;}
static void origin_test_pushnil(lua_State *L) {(void)L;++origin_test_nil;}
static void origin_test_pushstring(lua_State *L,const char *text,size_t length) {
    (void)L;assert(length==strlen(text) && length<sizeof(origin_test_text));memcpy(origin_test_text,text,length+1);
    if(origin_test_export) {
        fputs(!strncmp(text,"schema=",7)?(origin_test_export_transition_status?"NATIVE_GATE PLAYER_TRANSITION_STOP ":"NATIVE_GATE PLAYER_ORIGIN_STOP "):"NATIVE_GATE ",origin_test_export);
        assert(fwrite(text,1,length,origin_test_export)==length);fputc('\n',origin_test_export);
    }
}
static void origin_test_api_setup(void) {
    p_lua_tonumber=origin_test_number;p_lua_pushnil=origin_test_pushnil;p_lua_pushlstring=origin_test_pushstring;
    origin_test_nil=0;origin_test_row=1;origin_test_text[0]=0;
}
static void origin_test_load(void) {
    const unsigned slots[]={0x2a85278,0x2a85280,0x2a85288,0x2b329c8,0x2b33a70,0x2abd728,0x2b45558};
    const unsigned targets[]={0xf04b0,0x152860,0x152870,0x998700,0x706cc0,0x3508e0,0x35b8a0};
    for(unsigned i=0;i<7;i++) {assert(*(uint64_t*)(image+slots[i])==0x140000000ULL+targets[i]);*(void**)(image+slots[i])=image+targets[i];}
    assert(origin_pins());assert(!memcmp(image+PORIGIN_WORD,origin_test_word,16));
    memcpy(origin_test_call,image+0x1abb07,sizeof(origin_test_call));memcpy(origin_test_adapter,image+0x998700,sizeof(origin_test_adapter));
    memcpy(origin_test_dispatch,image+0x152870,sizeof(origin_test_dispatch));memcpy(origin_test_indirect,image+0x99c5ad,sizeof(origin_test_indirect));
    assert(!memcmp(origin_test_indirect,(unsigned char[]){0xff,0x50,0x20},3));origin_test_actual=1;
    puts("origin19 full ranges/7 slots, actual aligned call and native argument adapters PASS");
}
static void origin_test_slot(void *object,uint64_t runtime) {
    unsigned index=(unsigned)(runtime&0x1ffffff)-1,part=index&0xfffff,lane=part%3;unsigned char *b=saved_page+(part/3)*32;
    *(void**)(saved_registry+0x58+(index>>20)*8)=saved_page;*(void**)(b+lane*8)=object;
    identity_generation(b,lane,(unsigned)((runtime>>25)&0xffff));unsigned flags=*(unsigned*)(b+0x1c);
    *(unsigned*)(b+0x1c)=(flags&~(15u<<(16+lane*4)))|(5u<<(16+lane*4));
}
static void origin_test_reset(void) {
    DWORD ignored;assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&ignored));
    saved_fixture(1);memset(&origin_ledger,0,sizeof(origin_ledger));assert(!porigin_current);
    origin_started=origin_closing=origin_sites=origin_imports=origin_first=origin_last=0;origin_bound=0;
    origin_player_saved=origin_ship_saved=origin_runtime=origin_player=0;
    origin_calls=origin_returns=origin_open=origin_peak=origin_restore_open=origin_prebind=origin_failures=0;
    origin_unwinds=0;
    origin_ticket_rows=origin_edge_rows=0;memset(origin_token,0,sizeof(origin_token));
    memcpy(faction+0x10,"player",7);*(uint64_t*)(faction+0x20)=6;strcpy(transition_saved_watches[0].owner,"player");
    strcpy(transition_saved_watches[0].initial.owner,"player");
    *(void**)origin_test_player=image+0x2b329a8;*(uint64_t*)(origin_test_player+8)=(77ULL<<25)|3;
    *(void**)(origin_test_player+0x70)=object;origin_test_slot(origin_test_player,*(uint64_t*)(origin_test_player+8));
    assert(world_identity_record(*(uint64_t*)(origin_test_player+8),0x11e12));
    *(void**)(origin_test_universe+0x230)=origin_test_player;*(void**)(image+0x3d198f8)=origin_test_universe;
    memset(origin_test_event,0,4096);*(void**)origin_test_event=image+0x2a85278;
    memset(native_tls,0,sizeof(native_tls));native_tls[0x10]=1;*(double*)(native_tls+0x300)=31.5;
    origin_test_tls_pointer=(uintptr_t)native_tls;*(void**)(origin_test_teb+0x58)=&origin_test_tls_pointer;
    memcpy(image+PORIGIN_WORD,origin_test_word,16);*(void**)(image+PORIGIN_SLOT)=image+0x152870;
    if(origin_test_actual) {memcpy(image+0x1abb07,origin_test_call,31);memcpy(image+0x998700,origin_test_adapter,24);memcpy(image+0x152870,origin_test_dispatch,19);}
    origin_test_posts=origin_test_deliveries=origin_test_immediate=origin_test_blocked=origin_test_block_delivery=0;
    origin_test_raise_post=origin_test_raise_delivery=0;
    boot_phase=0;stats.issued=0;transition_sites=7;world_identity_installed=1;ui_thread=main_thread=GetCurrentThreadId();
}
static int origin_test_prepare(void) {
    unsigned char stub[32];origin_emit(stub,(void*)origin_post);assert(thunk_bytes_match(PORIGIN_STUB,stub,32));
    origin_original_slot=(uint64_t)(uintptr_t)(image+0x152870);origin_patched_slot=(uint64_t)(uintptr_t)origin_delivery;
    return origin_encode(&origin_original_word,&origin_patched_word,image+PORIGIN_WORD,thunk+PORIGIN_STUB);
}
static const char *origin_test_token="1|0000000000011e12|0000000000176a13|1|120";
static void origin_test_install(void) {
    assert(origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_prepare,origin_swap));
    boot_phase=2;origin_import_capture(origin_test_player,*(uint64_t*)(origin_test_player+8),0x11e12);
    boot_phase=3;assert(origin_bind() && origin_bound && origin_ledger.configured);stats.issued=30;
}
static uint64_t origin_test_native_post(void *player,void *event,double due,unsigned char flags) {
    ++origin_test_posts;assert(player==origin_test_player && event==origin_test_event && flags==1 && porigin_bits(due)==porigin_bits(origin_test_due));
    if(origin_test_immediate) assert(origin_test_invoke(player,player,due,0xcf,event)==origin_test_delivery_result);
    if(origin_test_blocked) {assert(SetEvent(origin_test_entered));assert(WaitForSingleObject(origin_test_resume,5000)==WAIT_OBJECT_0);}
    if(origin_test_raise_post) {DWORD old;assert(VirtualProtect(event,4096,PAGE_NOACCESS,&old));RaiseException(ORIGIN_TEST_EXCEPTION,0,0,NULL);}
    return origin_test_post_result;
}
static uint64_t origin_test_native_delivery(void *receiver,void *source,double due) {
    ++origin_test_deliveries;assert(receiver==origin_test_player && source==receiver && porigin_bits(due)==porigin_bits(origin_test_due));
    if(origin_test_block_delivery) {assert(SetEvent(origin_test_entered));assert(WaitForSingleObject(origin_test_resume,5000)==WAIT_OBJECT_0);}
    if(origin_test_raise_delivery) {DWORD old;assert(VirtualProtect(origin_test_event,4096,PAGE_NOACCESS,&old));RaiseException(ORIGIN_TEST_EXCEPTION,0,0,NULL);}
    transition_transfer_observe(native_transfer,next_controller,old_controller,0,1,(uintptr_t)(image+0x4504a7));
    *(void**)next_controller=image+0x2c51158;
    transition_publish_observe(native_publish,object,next_controller,1,(uintptr_t)(image+0x45052a));
    return origin_test_delivery_result;
}
static uint64_t origin_test_post_wrapper(void *receiver,void *event,double due,unsigned char flags) {
    return origin_observe_post((porigin_post_fn)(image+0x99a3f0),receiver,event,due,flags,
        (uintptr_t)__builtin_return_address(0),origin_test_teb);
}
static uint64_t origin_test_delivery_wrapper(void *event,void *receiver,void *source,double due) {
    return origin_observe_delivery((porigin_delivery_fn)(image+0x152870),event,receiver,source,due,
        (uintptr_t)__builtin_return_address(0),origin_test_teb);
}
static void origin_test_native_jump(unsigned char *code,void *target,unsigned *alignment) {
    unsigned n=0;const unsigned char record[]={0x48,0x89,0xe0,0x83,0xe0,0x0f,0x49,0xba};memcpy(code,record,sizeof(record));n+=sizeof(record);
    emit_address(code,&n,(uintptr_t)alignment);code[n++]=0x41;code[n++]=0x89;code[n++]=0x02;
    code[n++]=0xff;code[n++]=0x25;memset(code+n,0,4);n+=4;emit_address(code,&n,(uintptr_t)target);
}
static void origin_test_executable(void) {
    /* A private page is emitted completely before RX. Production's shared page
     * stays untouched. Only the TLS source is replaced with owned fixture data. */
    origin_test_code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(origin_test_code);unsigned n=0;
    const unsigned char post_entry[]={0x57,0x48,0x83,0xec,0x30,0x48,0x89,0x5c,0x24,0x40,0x48,0x89,0x74,0x24,0x48,0x49,0xbb};
    memcpy(origin_test_code,post_entry,sizeof(post_entry));n=sizeof(post_entry);emit_address(origin_test_code,&n,(uintptr_t)(image+0x1abb07));
    memcpy(origin_test_code+n,(unsigned char[]){0x41,0xff,0xe3},3);
    unsigned char *d=origin_test_code+128;n=0;
    const unsigned char delivery_entry[]={0x4c,0x8b,0x54,0x24,0x28,0x48,0x83,0xec,0x28,0x4c,0x89,0x54,0x24,0x20,0x48,0x8b,0x01,0x49,0xbb};
    memcpy(d,delivery_entry,sizeof(delivery_entry));n=sizeof(delivery_entry);emit_address(d,&n,(uintptr_t)(image+0x99c5ad));memcpy(d+n,(unsigned char[]){0x41,0xff,0xe3},3);
    origin_test_invoke=(origin_test_adapter_fn)d;
    /* Test nearstub must itself lie within rel32 of the owned image. Use a
     * private image subpage, never modify the published production thunk. */
    origin_emit(image+0x5000000,(void*)origin_test_post_wrapper);
    memcpy(image+PORIGIN_WORD,origin_test_word,16); /* owned synthetic site only */
    assert(origin_encode(&origin_original_word,&origin_patched_word,image+PORIGIN_WORD,image+0x5000000));
    memcpy(image+PORIGIN_WORD,&origin_patched_word,16);origin_patched_slot=(uint64_t)(uintptr_t)origin_test_delivery_wrapper;
    *(uint64_t*)(image+PORIGIN_SLOT)=origin_patched_slot;
    memcpy(image+0x99c5ad,origin_test_indirect,3);memcpy(image+0x99c5b0,(unsigned char[]){0x48,0x83,0xc4,0x28,0xc3},5);
    *(void**)(image+0x2b329a8+0x20)=image+0x998700;*(void**)(image+0x2b329a8+0x10c8)=image+0x706cc0;
    origin_test_native_jump(image+0x99a3f0,(void*)origin_test_native_post,&origin_test_post_alignment);
    origin_test_native_jump(image+0x706cc0,(void*)origin_test_native_delivery,&origin_test_delivery_alignment);
    DWORD ignored;assert(VirtualProtect(origin_test_code,4096,PAGE_EXECUTE_READ,&ignored));
    const unsigned pages[]={0x1ab000,0x99a000,0x99c000,0x998000,0x152000,0x706000,0x5000000};
    for(unsigned i=0;i<sizeof(pages)/sizeof(*pages);i++) assert(VirtualProtect(image+pages[i],4096,PAGE_EXECUTE_READWRITE,&ignored));
    FlushInstructionCache(GetCurrentProcess(),image,0x6fa9000);FlushInstructionCache(GetCurrentProcess(),origin_test_code,4096);
}
static void test_origin_forwarding(void) {
    if(!origin_test_actual) {puts("origin actual-image executable ABI not requested");return;}
    for(unsigned immediate=0;immediate<2;immediate++) {
        origin_test_reset();origin_test_install();origin_test_executable();origin_test_immediate=immediate;
        register uintptr_t bx __asm__("rbx")=0x123456789abcdef0ULL;
        register uintptr_t r12 __asm__("r12")=0x8877665544332211ULL;
        typedef unsigned long long v128 __attribute__((vector_size(16)));
        register v128 xmm __asm__("xmm6")={0x0123456789abcdefULL,0xfedcba9876543210ULL};
        __asm__ volatile("" : "+r"(bx),"+r"(r12),"+x"(xmm));
        assert(((porigin_post_fn)origin_test_code)(origin_test_player,origin_test_event,origin_test_due,1)==((origin_test_post_result&~255ULL)|1));
        if(!immediate) assert(origin_test_invoke(origin_test_player,origin_test_player,origin_test_due,0xcf,origin_test_event)==origin_test_delivery_result);
        __asm__ volatile("" : "+r"(bx),"+r"(r12),"+x"(xmm));
        assert(bx==0x123456789abcdef0ULL && r12==0x8877665544332211ULL && xmm[0]==0x0123456789abcdefULL && xmm[1]==0xfedcba9876543210ULL);
        assert(origin_test_posts==1 && origin_test_deliveries==1 && origin_test_post_alignment==8 && origin_test_delivery_alignment==8);
        assert(!origin_ledger.failures && !origin_failures && origin_ledger.link_count==2 && !transition_failures);
        assert(origin_ledger.tickets[0].post_rax==origin_test_post_result && origin_ledger.tickets[0].delivery_rax==origin_test_delivery_result);
        assert(origin_ledger.tickets[0].due!=origin_ledger.tickets[0].delivery_clock && origin_ledger.tickets[0].delivery_clock==porigin_bits(31.5));
        assert(origin_ledger.tickets[0].controller_after==(uintptr_t)next_controller && !world_trace_watch_count && !world_queue_count);
        origin_test_api_setup();assert(lua_player_origin_ticket(NULL)==1 && origin_test_nil==1 && !origin_ticket_rows);
        origin_restore();assert(!origin_sites && porigin_complete(&origin_ledger) && !origin_restore_open && origin_calls==origin_returns);
        assert(!memcmp(image+PORIGIN_WORD,origin_test_word,16) && *(void**)(image+PORIGIN_SLOT)==image+0x152870);
        const char *export_path=getenv("X4LAN_ORIGIN_TEST_LOG");
        if(!immediate && export_path) {origin_test_export=fopen(export_path,"w");assert(origin_test_export);}
        assert(lua_player_origin_ticket(NULL)==1 && origin_ticket_rows==1 && strstr(origin_test_text,"due=403e028f5c28f5c3"));
        assert(lua_player_origin_ticket(NULL)==1 && origin_test_nil==2);origin_test_row=2;
        assert(lua_player_origin_edge(NULL)==1 && !origin_edge_rows && origin_test_nil==3);origin_test_row=1;
        assert(lua_player_origin_edge(NULL)==1 && origin_edge_rows==1);origin_test_row=2;
        assert(lua_player_origin_edge(NULL)==1 && origin_edge_rows==2);
        assert(lua_player_origin_status(NULL)==1 && strstr(origin_test_text,"capture_complete=1"));
        if(origin_test_export) {
            /* Emit the real context as-is: this fixture has not installed or
             * restored transition hooks. Its sites/enabled failure must remain. */
            origin_test_row=1;assert(lua_player_transition_watch(NULL)==1);
            for(origin_test_row=1;origin_test_row<=transition_ready();origin_test_row++) assert(lua_player_transition_row(NULL)==1);
            origin_test_export_transition_status=1;assert(lua_player_transition_status(NULL)==1);origin_test_export_transition_status=0;
            assert(!fclose(origin_test_export));origin_test_export=NULL;
        }
        origin_restore_open=1;assert(lua_player_origin_status(NULL)==1 && strstr(origin_test_text,"capture_complete=0"));origin_restore_open=0;
        VirtualFree(origin_test_code,0,MEM_RELEASE);
    }
    puts("origin actual call/epilogue, fifth-stack-arg adapter, XMM2/3 double, fullRAX, nonvolatiles and immediate/deferred links PASS");
}
static void test_origin_guards(void) {
    for(unsigned fault=0;fault<12;fault++) {
        origin_test_reset();origin_test_install();porigin_arguments a={origin_test_player,origin_test_event,origin_test_player,porigin_bits(origin_test_due),0x1abb0c,1};porigin_sample s;
        assert(origin_capture(origin_test_teb,PO_POST,&a,&s));
        if(fault==0) *(uint64_t*)(origin_test_player+8)^=1ULL<<25;
        if(fault==1) *(void**)(origin_test_player+0x70)=origin_test_player;
        if(fault==2) origin_test_slot(origin_test_player+16,origin_runtime);
        if(fault==3) *(void**)(origin_test_universe+0x230)=origin_test_player+16;
        if(fault==4) faction[0x10]='x';
        if(fault==5) *(void**)(old_controller+0x40)=agent+16;
        if(fault==6) native_tls[0x10]=0;
        if(fault==7) *(double*)(native_tls+0x300)=NAN;
        if(fault==8) *(void**)origin_test_event=image+0x2a85270;
        if(fault==9) *(uint64_t*)(origin_test_event+8)=1;
        if(fault==10) agent[0xf79]=0;
        if(fault==11) *(void**)old_controller=image+0x2c51158;
        assert(!origin_capture(origin_test_teb,PO_POST,&a,&s));origin_restore();assert(!origin_sites);
    }
    origin_test_reset();origin_test_install();stats.issued=121;assert(!origin_window());stats.issued=30;boot_phase=2;assert(!origin_window());
    origin_restore();stats.active=1;assert(!origin_configure_transaction(origin_test_token,strlen(origin_test_token),origin_test_prepare,origin_swap));
    origin_test_reset();origin_test_install();unsigned char foreign[16];memcpy(foreign,&origin_patched_word,16);foreign[0]^=1;
    memcpy(image+PORIGIN_WORD,foreign,16);origin_restore();assert(origin_sites==1 && origin_failures && !memcmp(image+PORIGIN_WORD,foreign,16));
    memcpy(image+PORIGIN_WORD,&origin_patched_word,16);origin_restore();assert(!origin_sites);
    origin_test_reset();origin_test_install();*(uint64_t*)(image+PORIGIN_SLOT)=0x12345678;
    origin_restore();assert(origin_sites==2 && origin_failures && *(uint64_t*)(image+PORIGIN_SLOT)==0x12345678);
    *(uint64_t*)(image+PORIGIN_SLOT)=origin_patched_slot;origin_restore();assert(!origin_sites);
    uint64_t player,ship;unsigned first,last;
    const char *invalid[]={"1|0000000000011e12|0000000000176a13|01|120","1|0000000000011e12|0000000000176a13|+1|120",
        "1|0000000000011e12|0000000000176a13|1|121","1|0000000000011e12|0000000000176a13|2|1",
        "1|0000000000011e12|0000000000011e12|1|120","1|0000000000011e12|0000000000176a13|1|4294967416"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) assert(!origin_parse(invalid[i],strlen(invalid[i]),&player,&ship,&first,&last));
    puts("origin fresh native identity/event/TLS/time/window guards and foreign-byte restoration refusal PASS");
}
#include "test_lockstep_player_origin_stop.h"
#include "test_lockstep_player_origin_seh.h"
#include "test_lockstep_player_getup_native.h"
static void test_player_origin_native(void) {
    saved_registry=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);saved_page=VirtualAlloc(NULL,0xb00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    saved_macro=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(saved_registry && saved_page && saved_macro);
    origin_test_player=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);origin_test_universe=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    origin_test_teb=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);origin_test_event=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(origin_test_player && origin_test_universe && origin_test_teb && origin_test_event);
    unsigned char *previous=thunk;thunk=make_thunk();assert(thunk);
    test_origin_forwarding();test_origin_guards();test_origin_stop();test_origin_seh();test_transition_seh();test_getup_native();
    porigin_ticket maximum;memset(&maximum,0xff,sizeof(maximum));char row[1024];int length=origin_ticket_format(&maximum,UINT_MAX,row,sizeof(row));assert(length>0 && length<1000 && strlen(row)==(size_t)length);
    char tiny[8];assert(origin_ticket_format(&maximum,1,tiny,sizeof(tiny))==-1 && !tiny[7]);
    origin_test_api_setup();origin_started=1;origin_closing=1;origin_bound=1;origin_imports=UINT_MAX;origin_first=origin_last=120;
    origin_calls=origin_returns=origin_open=origin_peak=origin_restore_open=origin_prebind=origin_failures=UINT64_MAX;
    origin_ledger.post_calls=origin_ledger.post_returns=origin_ledger.delivery_calls=origin_ledger.delivery_returns=UINT64_MAX;
    origin_ledger.open=origin_ledger.peak=origin_ledger.stop_open=origin_ledger.failures=origin_ledger.other_receivers=origin_ledger.unknown=origin_ledger.excluded=UINT64_MAX;
    origin_ledger.count=PORIGIN_MAX;origin_ledger.link_count=PORIGIN_LINK_MAX;
    origin_unwinds=origin_ledger.post_unwinds=origin_ledger.delivery_unwinds=UINT64_MAX;
    memset(&origin_ledger.binding,0xff,sizeof(origin_ledger.binding));ui_thread=GetCurrentThreadId();
    assert(lua_player_origin_status(NULL)==1 && !origin_test_nil && strlen(origin_test_text)<1536 && strstr(origin_test_text,"capture_complete=0"));
    VirtualFree(thunk,0,MEM_RELEASE);thunk=previous;
    unsigned char *all[]={saved_registry,saved_page,saved_macro,origin_test_player,origin_test_universe,origin_test_teb,origin_test_event};
    for(unsigned i=0;i<sizeof(all)/sizeof(*all);i++) VirtualFree(all[i],0,MEM_RELEASE);
    origin_started=0;memset(&origin_ledger,0,sizeof(origin_ledger));saved_reset();
}
