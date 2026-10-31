/* Standalone production provenance core. Owned memory, no native image hooks. */
#include "lockstep_player_origin.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
typedef struct {
    uint64_t rng,controller,work;
    unsigned char untouched[72];
} engine_state;
static porigin_ledger ledger;
static porigin_binding bound={0x11e12,0xae79c,0x2000001,0x4000002,0x10000,0x20000,0x30000,0x60000};
static engine_state engine;
static unsigned capture_calls[3],posts,deliveries,immediate,link_enabled,change_identity,fail_stage;
static unsigned nested_other,poison_after_post,blocked_capture,stop_in_delivery,post_inside_delivery;
static unsigned nested_duplicate,blocked_delivery,bulk_links,identity_field;
static unsigned poison_after_delivery;
static unsigned throw_post,throw_delivery,throw_caught,catch_nested;
#define PURE_ORIGIN_EXCEPTION 0xe0424242u
static int pure_origin_filter(DWORD code) {
    if(code!=PURE_ORIGIN_EXCEPTION) return EXCEPTION_CONTINUE_SEARCH;
    ++throw_caught;return EXCEPTION_EXECUTE_HANDLER;
}
static void *expected_source;
static unsigned char event_memory[PORIGIN_MAX+2][24];
static unsigned char *allocated_event;
static HANDLE reached,resume;
static uint64_t post_result=0xfedcba9876543210ULL,delivery_result=0x12345678fefdfcfbULL;
static double due=12.010000000000002;
static int capture(void *user,unsigned stage,const porigin_arguments *a,porigin_sample *out) {
    assert(user==&engine && stage<=PO_AFTER);++capture_calls[stage];
    unsigned char bytes[24];SIZE_T n=0;
    if(!ReadProcessMemory(GetCurrentProcess(),a->event,bytes,sizeof(bytes),&n) || n!=sizeof(bytes) || bytes[0]!=0x7a) return 0;
    if(stage==PO_POST && blocked_capture) {
        assert(SetEvent(reached));assert(WaitForSingleObject(resume,5000)==WAIT_OBJECT_0);
    }
    out->binding=bound;out->controller=engine.controller;out->tick=stage==PO_POST?30:31;
    if(change_identity==stage+1) {
        if(identity_field==1) out->binding.ship_owner+=8;
        else if(identity_field==2) out->binding.agent+=8;
        else if(identity_field==3) out->binding.player+=8;
        else out->binding.player_runtime^=0x2000000;
    }
    return fail_stage!=stage+1;
}
static uint64_t native_delivery(void *event,void *receiver,void *source,double time);
static uint64_t deliver(void *event,void *receiver,void *source,double time,unsigned caller) {
    return porigin_observe_delivery(&ledger,capture,&engine,native_delivery,event,receiver,source,time,caller);
}
static uint64_t native_post(void *receiver,void *event,double time,unsigned char flags) {
    ++posts;assert(receiver==(void*)bound.player && flags==1 && porigin_bits(time)==porigin_bits(due));
    assert(!porigin_link_transition(&ledger,9999));
    engine.rng=engine.rng*6364136223846793005ULL+1;++engine.work;
    if(immediate) {
        assert(!ledger.tickets[ledger.count-1].posted_returned);
        assert(deliver(event,receiver,receiver,time,0x99c5b0)==delivery_result);
    }
    if(poison_after_post) {DWORD old;assert(VirtualProtect(event,4096,PAGE_NOACCESS,&old));}
    if(throw_post) RaiseException(PURE_ORIGIN_EXCEPTION,0,0,NULL);
    return post_result;
}
static uint64_t native_delivery(void *event,void *receiver,void *source,double time) {
    ++deliveries;assert(event && source==expected_source && porigin_bits(time)==porigin_bits(due));
    if(blocked_delivery) {assert(SetEvent(reached));assert(WaitForSingleObject(resume,5000)==WAIT_OBJECT_0);}
    engine.rng=engine.rng*2862933555777941757ULL+3037000493ULL;++engine.work;
    if(throw_delivery && (throw_delivery==1 || receiver!=(void*)bound.player)) RaiseException(PURE_ORIGIN_EXCEPTION,0,0,NULL);
    if(receiver!=(void*)bound.player) {assert(!porigin_link_transition(&ledger,999));return delivery_result;}
    if(nested_other) {
        nested_other=0;
        if(catch_nested) {
            porigin_scope *parent=porigin_current;
            __try {deliver(event,(void*)0x77700,source,time,0x99c614);assert(0);}
            __except(pure_origin_filter(GetExceptionCode())) {}
            assert(porigin_current==parent && parent->selected);
        } else assert(deliver(event,(void*)0x77700,source,time,0x99c614)==delivery_result);
    }
    if(post_inside_delivery) {
        post_inside_delivery=0;
        assert(porigin_observe_post(&ledger,capture,&engine,native_post,receiver,event_memory[1],due,1,0x1abb0c)==post_result);
    }
    if(nested_duplicate) {nested_duplicate=0;assert(deliver(event,receiver,source,time,0x99c5b0)==delivery_result);}
    if(stop_in_delivery) porigin_stop(&ledger);
    if(link_enabled) assert(porigin_link_transition(&ledger,42));
    for(unsigned i=0;i<bulk_links;i++) assert(porigin_link_transition(&ledger,i+1)==(i<PORIGIN_LINK_MAX));
    engine.controller=0x50000;
    if(poison_after_delivery) {DWORD old;assert(VirtualProtect(event,4096,PAGE_NOACCESS,&old));}
    return delivery_result;
}
static void reset(void) {
    assert(!porigin_current);memset(&ledger,0,sizeof(ledger));memset(&engine,0xa5,sizeof(engine));
    engine.rng=0xf0e1d2c3b4a59687ULL;engine.controller=0x40000;engine.work=0;
    throw_post=throw_delivery=throw_caught=catch_nested=0;
    memset(capture_calls,0,sizeof(capture_calls));posts=deliveries=immediate=link_enabled=change_identity=fail_stage=0;
    nested_other=poison_after_post=blocked_capture=stop_in_delivery=post_inside_delivery=0;
    nested_duplicate=blocked_delivery=bulk_links=identity_field=poison_after_delivery=0;expected_source=(void*)bound.player;
    memset(event_memory,0x7a,sizeof(event_memory));assert(porigin_configure(&ledger,&bound));
}
static void post(void *event) {
    assert(porigin_observe_post(&ledger,capture,&engine,native_post,(void*)bound.player,event,due,1,0x1abb0c)==post_result);
}
static void primary(void *event) {assert(deliver(event,(void*)bound.player,(void*)bound.player,due,0x99c5b0)==delivery_result);}
static void test_deferred_and_immediate(void) {
    reset();post(event_memory[0]);assert(ledger.count==1 && !ledger.tickets[0].delivery_state);
    link_enabled=1;primary(event_memory[0]);assert(ledger.link_count==1 && ledger.links[0].ticket==1 && ledger.links[0].transition==42);
    porigin_stop(&ledger);assert(porigin_complete(&ledger));assert(posts==1 && deliveries==1);
    assert(ledger.tickets[0].post_rax==post_result && ledger.tickets[0].delivery_rax==delivery_result);
    reset();immediate=link_enabled=1;post(event_memory[0]);porigin_stop(&ledger);
    assert(porigin_complete(&ledger) && ledger.peak==2 && ledger.open==0 && !porigin_current);
    assert(capture_calls[PO_POST]==1 && capture_calls[PO_DELIVERY]==1 && capture_calls[PO_AFTER]==1);
}
static void test_no_post_transfer_read(void) {
    reset();allocated_event=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(allocated_event);
    memset(allocated_event,0x7a,4096);immediate=1;poison_after_post=1;post(allocated_event);
    porigin_stop(&ledger);assert(porigin_complete(&ledger) && capture_calls[PO_POST]==1);
    assert(VirtualFree(allocated_event,0,MEM_RELEASE));allocated_event=NULL;
    reset();allocated_event=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(allocated_event);
    memset(allocated_event,0x7a,4096);post(allocated_event);poison_after_delivery=1;primary(allocated_event);
    porigin_stop(&ledger);assert(!porigin_complete(&ledger) && posts==1 && deliveries==1 && (ledger.tickets[0].errors&PO_CAPTURE));
    assert(VirtualFree(allocated_event,0,MEM_RELEASE));allocated_event=NULL;
}
static void test_natural_engine_equality(void) {
    reset();native_post((void*)bound.player,event_memory[0],due,1);
    native_delivery(event_memory[0],(void*)bound.player,(void*)bound.player,due);
    engine_state direct=engine;
    reset();post(event_memory[0]);primary(event_memory[0]);
    assert(!memcmp(&direct,&engine,sizeof(engine)));porigin_stop(&ledger);assert(porigin_complete(&ledger));
}
static void test_failures_original_once(void) {
    for(unsigned stage=1;stage<=3;stage++) {
        reset();fail_stage=stage;post(event_memory[0]);primary(event_memory[0]);porigin_stop(&ledger);
        assert(posts==1 && deliveries==1 && !porigin_complete(&ledger) && ledger.failures);
    }
    for(unsigned stage=1;stage<=3;stage++) {
        reset();change_identity=stage;post(event_memory[0]);primary(event_memory[0]);porigin_stop(&ledger);
        assert(posts==1 && deliveries==1 && !porigin_complete(&ledger));
    }
    reset();post(event_memory[0]);primary(event_memory[0]);primary(event_memory[0]);porigin_stop(&ledger);
    assert(posts==1 && deliveries==2 && !porigin_complete(&ledger) && (ledger.tickets[0].errors&PO_DUPLICATE));
    reset();post(event_memory[0]);primary(event_memory[0]);post(event_memory[0]);porigin_stop(&ledger);
    assert(posts==2 && deliveries==1 && ledger.count==1 && !porigin_complete(&ledger));
    reset();post(event_memory[0]);assert(deliver(event_memory[0],(void*)bound.player,(void*)bound.player,due,1)==delivery_result);
    porigin_stop(&ledger);assert(!porigin_complete(&ledger) && (ledger.tickets[0].errors&PO_CALLER));
    reset();post(event_memory[0]);porigin_stop(&ledger);assert(!porigin_complete(&ledger));
    reset();post(event_memory[0]);nested_duplicate=1;primary(event_memory[0]);porigin_stop(&ledger);
    assert(deliveries==2 && ledger.peak==2 && !porigin_complete(&ledger));
    for(unsigned field=1;field<=3;field++) {
        reset();post(event_memory[0]);identity_field=field;change_identity=2;primary(event_memory[0]);porigin_stop(&ledger);
        assert(posts==1 && deliveries==1 && !porigin_complete(&ledger));
    }
    reset();post(event_memory[0]);expected_source=(void*)0x88800;
    assert(deliver(event_memory[0],(void*)bound.player,expected_source,due,0x99c5b0)==delivery_result);
    porigin_stop(&ledger);assert(deliveries==1 && !porigin_complete(&ledger));
    reset();post(event_memory[0]);double prior=due;due+=0.5;primary(event_memory[0]);due=prior;
    porigin_stop(&ledger);assert(!porigin_complete(&ledger) && (ledger.tickets[0].errors&PO_TIME));
    reset();assert(porigin_observe_post(&ledger,capture,&engine,native_post,(void*)bound.player,event_memory[0],due,1,1)==post_result);
    porigin_stop(&ledger);assert(posts==1 && ledger.count==0 && !porigin_complete(&ledger));
    reset();prior=due;uint64_t nan_bits=0x7ff80000deadbeefULL;memcpy(&due,&nan_bits,8);post(event_memory[0]);due=prior;
    porigin_stop(&ledger);assert(posts==1 && ledger.count==0 && !porigin_complete(&ledger));
}
static void test_scope_barriers_and_bounds(void) {
    reset();post(event_memory[0]);nested_other=link_enabled=1;primary(event_memory[0]);porigin_stop(&ledger);
    assert(porigin_complete(&ledger) && ledger.other_receivers==1 && ledger.link_count==1 && ledger.peak==2);
    reset();post(event_memory[0]);post_inside_delivery=link_enabled=1;primary(event_memory[0]);
    assert(ledger.count==2 && ledger.link_count==1 && ledger.links[0].ticket==1);
    link_enabled=0;primary(event_memory[1]);porigin_stop(&ledger);
    assert(!porigin_complete(&ledger) && (ledger.tickets[1].errors&PO_IDENTITY));
    reset();for(unsigned i=0;i<PORIGIN_MAX+1;i++) post(event_memory[i]);
    porigin_stop(&ledger);assert(posts==PORIGIN_MAX+1 && ledger.count==PORIGIN_MAX && ledger.failures==1);
    assert(!porigin_configure(&ledger,&bound));post(event_memory[PORIGIN_MAX+1]);
    assert(posts==PORIGIN_MAX+2 && ledger.post_calls==PORIGIN_MAX+1 && !porigin_current);
    reset();post(event_memory[0]);stop_in_delivery=1;primary(event_memory[0]);
    assert(ledger.stop_open==1 && !ledger.open && ledger.tickets[0].delivery_state==2 && !porigin_complete(&ledger));
    reset();post(event_memory[0]);bulk_links=PORIGIN_LINK_MAX+1;primary(event_memory[0]);porigin_stop(&ledger);
    assert(ledger.link_count==PORIGIN_LINK_MAX && !porigin_complete(&ledger));
    reset();porigin_binding changed=bound;changed.ship_runtime^=0x2000000;
    assert(!porigin_configure(&ledger,&changed) && porigin_configure(&ledger,&bound));porigin_stop(&ledger);
}
static DWORD WINAPI worker(void *unused) {(void)unused;post(event_memory[0]);return 0;}
static DWORD WINAPI delivery_worker(void *unused) {(void)unused;primary(event_memory[0]);return 0;}
static void test_concurrent_stop_before_reservation(void) {
    reset();reached=CreateEvent(NULL,TRUE,FALSE,NULL);resume=CreateEvent(NULL,TRUE,FALSE,NULL);assert(reached && resume);
    blocked_capture=1;HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);assert(thread);
    assert(WaitForSingleObject(reached,5000)==WAIT_OBJECT_0);
    porigin_stop(&ledger);assert(ledger.stop_open==1 && ledger.open==1 && ledger.count==0 && ledger.post_calls==1);
    assert(!porigin_configure(&ledger,&bound));assert(SetEvent(resume));assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    assert(ledger.count==1 && !ledger.open && ledger.post_returns==1 && ledger.stop_open==1 && posts==1 && !porigin_complete(&ledger));
    CloseHandle(thread);CloseHandle(reached);CloseHandle(resume);
    reset();post(event_memory[0]);reached=CreateEvent(NULL,TRUE,FALSE,NULL);resume=CreateEvent(NULL,TRUE,FALSE,NULL);assert(reached && resume);
    blocked_delivery=1;thread=CreateThread(NULL,0,delivery_worker,NULL,0,NULL);assert(thread);
    assert(WaitForSingleObject(reached,5000)==WAIT_OBJECT_0);porigin_stop(&ledger);
    assert(ledger.stop_open==1 && ledger.open==1 && ledger.tickets[0].delivery_state==1 && ledger.delivery_returns==0);
    assert(SetEvent(resume));assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    assert(!ledger.open && ledger.delivery_returns==1 && ledger.tickets[0].delivery_state==2 && ledger.stop_open==1 && !porigin_complete(&ledger));
    CloseHandle(thread);CloseHandle(reached);CloseHandle(resume);
}
static void test_exception_accounting_and_no_after_capture(void) {
    for(unsigned mode=0;mode<5;mode++) {
        reset();
        if(mode==0) throw_post=1;
        else {post(event_memory[0]);throw_delivery=mode<3?1:2;}
        if(mode>=3) nested_other=1;
        if(mode==4) catch_nested=1;
        __try {
            if(!mode) post(event_memory[0]);else primary(event_memory[0]);
            assert(mode==4);
        } __except(pure_origin_filter(GetExceptionCode())) {}
        assert(throw_caught==1 && !porigin_current && !ledger.open && ledger.failures);
        assert(posts==1 && deliveries==(!mode?0:mode>=3?2:1));
        assert(capture_calls[PO_AFTER]==(mode==4));
        assert(ledger.post_calls==ledger.post_returns+ledger.post_unwinds);
        assert(ledger.delivery_calls==ledger.delivery_returns+ledger.delivery_unwinds);
        assert(ledger.post_unwinds==!mode && ledger.delivery_unwinds==(!mode?0:mode==3?2:1));
        assert(ledger.tickets[0].posted_returned==(mode!=0));
        assert(ledger.tickets[0].delivery_state==(!mode?0:mode==4?2:1));
        porigin_stop(&ledger);assert(!porigin_complete(&ledger));
    }
    puts("origin exception scope: exact propagation, nested other receiver and native-caught inner restoration PASS");
}
int main(void) {
    test_deferred_and_immediate();test_no_post_transfer_read();test_natural_engine_equality();
    test_failures_original_once();test_scope_barriers_and_bounds();test_concurrent_stop_before_reservation();
    test_exception_accounting_and_no_after_capture();
    puts("player origin production ledger: PASS (synthetic, no native hooks/game)");return 0;
}
