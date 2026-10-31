static unsigned char *saved_registry,*saved_page,*saved_macro;
static void saved_reset(void) {
    transition_prebind_enabled=0;memset(&transition_prebind,0,sizeof(transition_prebind));
    transition_diag_enabled=transition_diag_count=transition_diag_exported=0;
    transition_diag_calls=transition_diag_unknown=transition_diag_unreadable=transition_diag_ambiguous=transition_diag_dropped=0;
    memset(transition_diag_rows,0,sizeof(transition_diag_rows));
    transition_saved_count=transition_saved_bound=transition_saved_last=transition_saved_tickets_count=0;
    transition_saved_watch_rows=transition_saved_ticket_rows=0;
    transition_saved_failures=transition_saved_import_calls=transition_saved_prebind=0;
    transition_saved_queue_calls=transition_saved_queue_returns=transition_saved_queue_other=transition_saved_queue_unknown=transition_saved_queue_failures=0;
    memset(transition_saved_watches,0,sizeof(transition_saved_watches));memset(transition_saved_manifest,0,sizeof(transition_saved_manifest));
    memset(transition_saved_tickets,0,sizeof(transition_saved_tickets));
    world_queue_transition_enqueue_begin=NULL;world_queue_transition_enqueue_end=NULL;
    world_queue_transition_dispatch_begin=NULL;world_queue_transition_dispatch_end=NULL;
}
static void saved_fixture_stage(unsigned player,unsigned bind) {
    saved_reset();fixture();reset_events();transition_started=1;stats.issued=0;boot_mode=1;boot_phase=2;main_thread=ui_thread=GetCurrentThreadId();
    world_queue_enabled=0;world_queue_count=world_queue_order=world_queue_failures=0;world_queue_model_begin=NULL;world_queue_model_end=NULL;
    world_trace_watch_count=world_trace_bound=0;world_trace_last_tick=12;
    *(void**)old_controller=image+(player?0x2c39488:0x2c51158);agent[0xf79]=(unsigned char)player;
    memset(saved_registry,0,4096);memset(saved_macro,0,4096);
    uint64_t runtime=*(uint64_t*)(object+8);unsigned index=(unsigned)(runtime&0x1ffffff)-1,part=index&0xfffff,lane=part%3;
    unsigned char *block=saved_page+(part/3)*32;memset(block,0,32);
    *(void**)(saved_registry+0x58+(index>>20)*8)=saved_page;*(void**)(image+0x6cf1500)=saved_registry;
    *(void**)(block+lane*8)=object;identity_generation(block,lane,(unsigned)((runtime>>25)&0xffff));identity_xor32(block+0x1c,5u<<(16+lane*4));
    *(void**)(object+0x60)=saved_macro;*(void**)saved_macro=image+0x2ba5430;*(void**)(image+0x2ba5430+0x90)=image+0xefb10;
    memcpy(saved_macro+0x20,"ship_test_macro",16);*(uint64_t*)(saved_macro+0x30)=15;*(uint64_t*)(saved_macro+0x38)=15;
    char manifest[128];snprintf(manifest,sizeof(manifest),"2|0000000000176a13/%c/ship_test_macro/argon",player?'p':'n');unsigned count=0;
    assert(transition_saved_parse(manifest,strlen(manifest),120,transition_saved_watches,&count));transition_saved_count=count;transition_saved_last=120;
    strcpy(transition_saved_manifest,manifest);transition_import_capture(object,runtime,0x176a13);
    assert(transition_saved_watches[0].imports==1 && !transition_saved_bound);
    assert(transition_object_watch(object)==-1 && transition_saved_prebind==1);
    if(bind) {boot_phase=3;assert(transition_saved_bind() && transition_saved_bound==1 && !transition_saved_failures);}
    world_queue_transition_enqueue_begin=transition_saved_enqueue_begin;world_queue_transition_enqueue_end=transition_saved_enqueue_end;
    world_queue_transition_dispatch_begin=transition_saved_dispatch_begin;world_queue_transition_dispatch_end=transition_saved_dispatch_end;
    *(void**)(image+0x6cf21c8)=object;*(void**)command=image+WORLD_QUEUE_MODEL_VTABLE;memset(command+8,0,8);
}
static void saved_fixture(unsigned player) {saved_fixture_stage(player,1);}
static void saved_enqueue(void) {world_queue_enqueue_observe(native_enqueue,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xeac95a),native_tls);}
static void saved_dispatch(void) {world_queue_dispatch_observe(native_model,command,agent,(uintptr_t)(image+0xeacf66),native_tls);}
static int saved_changed_type_reader(const void *source,void *out,size_t length) {
    int ok=read_local(source,out,length);
    if(ok && source==command && length==8) *(void**)command=image+WORLD_QUEUE_ABORT_VTABLE;
    return ok;
}
static void saved_immediate(void *manager,uint64_t runtime,void *input) {
    native_enqueue(manager,runtime,input);world_queue_dispatch_observe(native_model,input,agent,(uintptr_t)(image+0xeacf66),native_tls);
}
static void saved_poison(void *manager,uint64_t runtime,void *input) {
    native_enqueue(manager,runtime,input);DWORD old;assert(VirtualProtect(input,4096,PAGE_NOACCESS,&old));
}
static void saved_blocked(void *manager,uint64_t runtime,void *input) {
    assert(SetEvent(entered));assert(WaitForSingleObject(resume,5000)==WAIT_OBJECT_0);native_enqueue(manager,runtime,input);
}
static DWORD WINAPI saved_queue_worker(void *unused) {
    (void)unused;world_queue_enqueue_observe(saved_blocked,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xeac95a),native_tls);return 0;
}
static void test_saved_manifest_pins(void) {
    transition_saved_watch distinct[2]={
        {.saved=1,.runtime=2,.object=3,.agent=4,.initial_controller=5},
        {.saved=6,.runtime=7,.object=8,.agent=9,.initial_controller=10}};
    assert(transition_saved_distinct(distinct,2));
    distinct[1].agent=distinct[0].agent;assert(!transition_saved_distinct(distinct,2));distinct[1].agent=9;
    distinct[1].initial_controller=distinct[0].initial_controller;assert(!transition_saved_distinct(distinct,2));
    saved_reset();ui_thread=GetCurrentThreadId();stats.active=boot_mode=world_identity_installed=1;boot_phase=0;stats.issued=0;
    transition_closing=transition_started=0;const char *manifest="2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player";
    assert(transition_saved_pins() && transition_saved_config(manifest,strlen(manifest),120));
    boot_phase=3;assert(transition_saved_config(manifest,strlen(manifest),120));
    assert(!transition_saved_config(manifest,strlen(manifest),119));transition_closing=1;
    assert(!transition_saved_config(manifest,strlen(manifest),120));
    saved_reset();transition_closing=0;stats.active=boot_mode=world_identity_installed=0;boot_phase=0;
}
static void saved_raw_overlap(void) {
    world_queue_event baseline[2];world_queue_lifetime tokens[2];
    for(unsigned replaced=0;replaced<=1;replaced++) for(unsigned separate=0;separate<=1;separate++) {
        saved_fixture(0);
        world_trace_watch_count=world_trace_bound=1;
        world_trace_watches[0]=(world_trace_watch){0x176a13,0x123456789abcULL,object,old_controller,agent};world_trace_imports[0]=1;
        memset(world_queue_events,0,sizeof(world_queue_events));memset(world_queue_lifetimes,0,sizeof(world_queue_lifetimes));
        world_queue_enabled=1;world_queue_restoring=0;world_queue_open=world_queue_peak=world_queue_restore_open=0;
        if(!separate) {
            world_queue_transition_enqueue_begin=NULL;world_queue_transition_enqueue_end=NULL;
            world_queue_transition_dispatch_begin=NULL;world_queue_transition_dispatch_end=NULL;
        }
        if(replaced) {
            /* Simulate a published replacement on owned fixture memory. The
             * raw watch retains its initial controller and must reject it. */
            memcpy(next_controller,old_controller,0xf0);old_controller[0x58]=0;
            *(void**)next_controller=image+0x2c39488;*(void**)(object+0x200)=next_controller;agent[0xf79]=1;
        }
        unsigned before=calls;stats.issued=1;saved_enqueue();stats.issued=2;saved_dispatch();
        assert(calls==before+2 && world_queue_count==2 && !world_queue_open && !transition_open);
        assert(world_queue_events[1].receipt_valid && world_queue_lifetimes[0].state==3);
        if(replaced) assert(world_queue_events[1].errors&256);
        if(!separate) {memcpy(baseline,world_queue_events,sizeof(baseline));memcpy(tokens,world_queue_lifetimes,sizeof(tokens));}
        else {
            assert(!memcmp(baseline,world_queue_events,sizeof(baseline)) && !memcmp(tokens,world_queue_lifetimes,sizeof(tokens)));
            assert(transition_saved_tickets_count==1 && !transition_saved_outstanding() && transition_events[1].queue_valid && !transition_failures);
        }
    }
    puts("separate model ledger preserves byte-identical overlapping raw queue rows/tokens and lifetime failure PASS");
}
#include "test_lockstep_transition_queue_diag.h"
#include "test_lockstep_transition_prebind.h"
static void test_saved_watch_and_queue(void) {
    saved_registry=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    saved_page=VirtualAlloc(NULL,0xb00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    saved_macro=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(saved_registry && saved_page && saved_macro);
    saved_raw_overlap();
    test_transition_queue_diag();
    test_transition_prebind();
    saved_fixture(1);unsigned char changed[16];unsigned mode=0;
    /* Initial type is valid, then both full copies agree on another type. A
     * stable wrong header must not become a positive model receipt. */
    assert(transition_model_copy_read(saved_changed_type_reader,command,changed,&mode)==-1);
    /* Both audited initial controller types bind independently of raw watches. */
    for(unsigned player=0;player<=1;player++) {
        saved_fixture(player);transition_state s;assert(transition_read(object,NULL,&s) && s.controller_type==(player?0x2c39488u:0x2c51158u));
        assert(!world_trace_watch_count && !world_queue_count);
        *(uint64_t*)(object+8)^=1ULL<<25;assert(!transition_read(object,NULL,&s));*(uint64_t*)(object+8)^=1ULL<<25;
        *(void**)(old_controller+0x40)=agent+16;assert(!transition_read(object,NULL,&s));*(void**)(old_controller+0x40)=agent;
        faction[0x10]='x';assert(!transition_read(object,NULL,&s));faction[0x10]='a';
        transition_import_capture(object,*(uint64_t*)(object+8),0x176a13);assert(transition_saved_failures==1 && !transition_read(object,NULL,&s));
    }
    saved_fixture(1);unsigned before=calls;stats.issued=30;saved_enqueue();
    assert(calls==before+1 && transition_saved_tickets_count==1 && transition_saved_outstanding()==1 && transition_ready()==1);
    stats.issued=90;saved_dispatch();assert(calls==before+2 && transition_saved_outstanding()==0 && transition_events[1].queue_valid);
    assert(!transition_failures && !transition_open && !world_queue_count && transition_saved_queue_calls==2 && transition_saved_queue_returns==2);
    /* A retired command address gets a new ticket, never the old receipt. */
    *(void**)(agent+0xf10)=model;saved_enqueue();saved_dispatch();assert(transition_saved_tickets_count==2 && transition_events[3].enqueue==2);
    /* The opposite transfer/publication direction also preserves the imported
     * Agent and owner after the old PC is unreadable. This does not invent a
     * release model-order/result predicate or claim a native release action. */
    saved_fixture(1);before=calls;unsigned published=publication_calls;
    transition_transfer_observe(native_transfer,next_controller,old_controller,0,1,(uintptr_t)(image+1));
    *(void**)next_controller=image+0x2c51158;
    transition_publish_observe(native_publish,object,next_controller,1,(uintptr_t)(image+1));
    assert(calls==before+1 && publication_calls==published+1 && transition_ready()==2 && !transition_failures);
    assert(!transition_events[0].old_owns_after && !transition_events[1].after.player && transition_events[1].after.owns_agent);
    assert(transition_events[1].after.controller_type==0x2c51158 && transition_events[1].after.agent==(uintptr_t)agent);
    DWORD ignored;assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&ignored));
    /* Real transfer/publication wrappers join their delayed, independent model
     * receipts. Old controller is unreadable after publication. */
    saved_fixture(0);before=calls;stats.issued=30;
    transition_transfer_observe(native_transfer,next_controller,old_controller,1,0,(uintptr_t)(image+0xd83b36));
    *(void**)next_controller=image+0x2c39488;
    transition_publish_observe(native_publish_activate,object,next_controller,1,(uintptr_t)(image+0x450291));
    assert(transition_ready()==4 && transition_saved_tickets_count==1 && transition_events[3].publication==2);
    stats.issued=90;saved_dispatch();assert(transition_events[4].queue_valid && transition_events[4].transfer==1 && transition_events[4].publication==2);
    assert(calls==before+3 && !transition_failures && !world_queue_count && !transition_open);
    export_evidence();
    assert(VirtualProtect(old_controller,4096,PAGE_READWRITE,&ignored));
    /* Delayed receipt across an unobserved/replaced controller must not borrow
     * the same object's/Agent's identity. Original still executes once. */
    for(unsigned fault=0;fault<5;fault++) {
        saved_fixture(1);receipt_fault=0;saved_enqueue();before=calls;
        if(fault==0) {memcpy(next_controller,old_controller,0xf0);*(void**)(object+0x200)=next_controller;}
        if(fault==1) *(uint64_t*)(object+8)^=1ULL<<25;
        if(fault==2) *(uint32_t*)(command+8)=1;
        if(fault==3) transition_saved_tickets[0].runtime^=1;
        if(fault==4) transition_saved_tickets[0].state=2;
        /* Preserve the original's bounded synthetic input for changed-mode
         * case while still checking production observer's mismatch. */
        world_queue_dispatch_observe(native_model_fault,command,agent,(uintptr_t)(image+0xeacf66),native_tls);
        assert(calls==before+1 && transition_failures && !transition_events[1].queue_valid && transition_saved_outstanding()==1 && !transition_open);
    }
    saved_fixture(1);saved_enqueue();before=calls;saved_enqueue();assert(calls==before+1 && transition_failures && transition_saved_tickets_count==1);
    saved_fixture(1);before=calls;
    world_queue_enqueue_observe(saved_immediate,object,*(uint64_t*)(object+8),command,(uintptr_t)(image+0xeac95a),native_tls);
    assert(calls==before+2 && !transition_saved_outstanding() && !transition_open && transition_peak==2 && !transition_failures);
    saved_fixture(1);unsigned char *poison=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(poison);memcpy(poison,command,16);before=calls;
    world_queue_enqueue_observe(saved_poison,object,*(uint64_t*)(object+8),poison,(uintptr_t)(image+0xeac95a),native_tls);
    assert(calls==before+1 && !transition_open && !transition_failures && transition_saved_outstanding()==1);
    assert(VirtualProtect(poison,4096,PAGE_READWRITE,&ignored));VirtualFree(poison,0,MEM_RELEASE);
    /* STOP observes the native original still open after its immutable enqueue
     * receipt, then retains restore_open after late return. */
    saved_fixture(1);entered=CreateEventW(NULL,TRUE,FALSE,NULL);resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(entered && resume);
    HANDLE worker=CreateThread(NULL,0,saved_queue_worker,NULL,0,NULL);assert(worker);
    assert(WaitForSingleObject(entered,5000)==WAIT_OBJECT_0 && transition_open==1 && transition_ready()==1);
    transition_restore();assert(transition_restore_open==1 && !transition_enabled);
    assert(SetEvent(resume));assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    assert(!transition_open && transition_restore_open==1 && transition_saved_queue_returns==1);
    before=calls;saved_enqueue();assert(calls==before+1 && transition_saved_queue_calls==1 && !transition_open);
    CloseHandle(worker);CloseHandle(entered);CloseHandle(resume);
    /* The imported registry slot replacing the object invalidates its watch. */
    saved_fixture(1);uint64_t runtime=*(uint64_t*)(object+8);unsigned index=(unsigned)(runtime&0x1ffffff)-1,part=index&0xfffff;
    *(void**)(saved_page+(part/3)*32+(part%3)*8)=next_controller;transition_state s;assert(!transition_read(object,NULL,&s));
    saved_reset();transition_started=0;boot_mode=boot_phase=0;stats.issued=0;
    VirtualFree(saved_registry,0,MEM_RELEASE);VirtualFree(saved_page,0,MEM_RELEASE);VirtualFree(saved_macro,0,MEM_RELEASE);
    puts("separate saved Nav/Player watches, fresh registry/Agent/owner guards, delayed tickets and unchanged raw ledger PASS");
}
