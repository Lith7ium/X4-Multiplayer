/* Execute the production forwarding wrappers over owned synthetic originals.
 * This verifies ABI forwarding, not an engine constructor or a live game. */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned char *image,*thunk;
/* native_gate_spawn_rng stays off here too (test_lockstep_spawn_rng.h runs it). */
typedef struct {int active;} spawn_rng_scope;
static int spawn_rng_begin(uintptr_t state,spawn_rng_scope *s) {(void)state;s->active=0;return 0;}
static void spawn_rng_end(spawn_rng_scope *s,int normal) {(void)normal;assert(!s->active);}
#include "lockstep_creation_trace_contract.h"
#include "lockstep_creation_trace_forward.h"
static uint64_t expected[10],observed_result;
static unsigned expected_count,expected_kind,begins,ends,originals,mode,deactivate,result_kind,last_index,aborts,throw_original;
static DWORD native_exception_flags;static unsigned native_exception_baseline;
/* This ABI-only executable keeps the separate opt-in intervention disabled;
 * test_lockstep_creation_rng.c exercises its production implementation. */
static void creation_rng_begin(unsigned index,unsigned kind,uintptr_t caller,const uint64_t *args,unsigned *scope) {
    (void)index;(void)kind;(void)caller;(void)args;assert(*scope==0);
}
static void creation_rng_end(unsigned scope,int normal) {(void)normal;assert(!scope);}
static void creation_rng_original(unsigned scope) {assert(!scope);}
static void registry_creation_trace(unsigned index,unsigned edge) {(void)index;(void)edge;}
static __attribute__((noinline)) void native_throw(void) {
    const ULONG_PTR data[]={0x123456789abcdef0ULL,0x778899aabbccddeeULL,0x5678};RaiseException(0xe04d5041,0,3,data);
}
static unsigned creation_enter(unsigned kind,uintptr_t caller,const uint64_t *args,unsigned count) {
    assert(caller && kind==expected_kind && count==expected_count && !memcmp(args,expected,count*8));
    assert(GetLastError()==0x12345678);++begins;SetLastError(0xdeadbeef);return mode?17:0;
}
static void creation_leave(unsigned index,uint64_t result,unsigned kind) {
    assert(GetLastError()==0x55667788);++ends;last_index=index;observed_result=result;result_kind=kind;
    SetLastError(0xfeedbeef);
}
static void creation_abort(unsigned index) {
    assert(index==17 && GetLastError()==0x55667788);++aborts;SetLastError(0xfadedbad);
}
static void original_check(const uint64_t *args,unsigned count) {
    assert(GetLastError()==0x12345678 && count==expected_count && !memcmp(args,expected,count*8));
    ++originals;if(deactivate)mode=0;SetLastError(0x55667788);
    if(throw_original)native_throw();
}
#define ORIGINAL __attribute__((noinline))
static ORIGINAL uintptr_t original_generate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        unsigned char e,uintptr_t f,uintptr_t g,unsigned h,unsigned char i,unsigned char j) {
    uint64_t args[]={a,b,c,d,e,f,g,h,i,j};original_check(args,10);*(uint64_t*)b=0xabcdef1234567890ULL;return b;
}
static ORIGINAL void original_initialize(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        uintptr_t e,unsigned char f,uintptr_t g,unsigned h,unsigned char i) {
    uint64_t args[]={a,b,c,d,e,f,g,h,i};original_check(args,9);*(uint64_t*)b=0xfedcba9876543210ULL;
}
static ORIGINAL uintptr_t original_pilot(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,
        uintptr_t e,uintptr_t f,uintptr_t g) {
    uint64_t args[]={a,b,c,d,e,f,g};original_check(args,7);*(uint64_t*)b=0x123456789abcdef0ULL;return b;
}
static ORIGINAL void original_navigation(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,unsigned char e,unsigned char f) {
    uint64_t args[]={a,b,c,d,e,f};original_check(args,6);
}
static ORIGINAL uintptr_t original_agent(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d) {
    uint64_t args[]={a,b,c,d};original_check(args,4);return a;
}
static ORIGINAL void original_setter(uintptr_t a,uintptr_t b,unsigned char c) {
    uint64_t args[]={a,b,c};original_check(args,3);
}
static ORIGINAL float original_random(uint64_t *state,float bound) {
    uint32_t bits;memcpy(&bits,&bound,4);uint64_t args[]={(uintptr_t)state,bits};original_check(args,2);
    *state+=19;bits=0x3f123456;float result;memcpy(&result,&bits,4);return result;
}
static ORIGINAL unsigned char original_query(uintptr_t owner,unsigned type,double minimum,double maximum,unsigned char flag) {
    uint64_t low,high;memcpy(&low,&minimum,8);memcpy(&high,&maximum,8);uint64_t args[]={owner,type,low,high,flag};
    original_check(args,5);return 0xa5;
}
static ORIGINAL void original_post(uintptr_t owner,uintptr_t event,double due,unsigned char flag) {
    uint64_t bits;memcpy(&bits,&due,8);uint64_t args[]={owner,event,bits,flag};original_check(args,4);
}
static void jump(unsigned char *out,void *target) {
    const unsigned char op[]={0xff,0x25,0,0,0,0};memcpy(out,op,6);memcpy(out+6,&target,8);
}
static void prepare(unsigned kind,unsigned count) {
    expected_kind=kind;expected_count=count;begins=ends=originals=result_kind=last_index=aborts=0;observed_result=0;
    for(unsigned i=0;i<10;i++)expected[i]=0xa123456700000000ULL+i;
}
static int exception_matches(EXCEPTION_POINTERS *p) {
    const EXCEPTION_RECORD *r=p->ExceptionRecord;
    assert(r->ExceptionCode==0xe04d5041 && r->NumberParameters==3);
    /* Windows may add software-origin flags. Compare with the direct native
     * throw instead of assuming RaiseException exposes its input flags. */
    if(!native_exception_baseline) {native_exception_baseline=1;native_exception_flags=r->ExceptionFlags;}
    else assert(r->ExceptionFlags==native_exception_flags);
    assert(r->ExceptionInformation[0]==0x123456789abcdef0ULL && r->ExceptionInformation[1]==0x778899aabbccddeeULL && r->ExceptionInformation[2]==0x5678);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void finish_case(unsigned admitted) {
    assert(GetLastError()==0x55667788 && begins==1 && originals==1 && ends==1 && last_index==(admitted?17u:0u));
}
static void execute(unsigned kind,unsigned admitted) {
    uint64_t reference=0,rng=0x1020304050607080ULL;mode=admitted;
    switch(kind) {
    case CREATION_GENERATE:
        prepare(kind,10);expected[1]=(uintptr_t)&reference;expected[4]=0xfe;expected[7]=0xe3456789;expected[8]=0x91;expected[9]=0x82;
        SetLastError(0x12345678);
        assert(creation_generate(expected[0],expected[1],expected[2],expected[3],(unsigned char)expected[4],expected[5],expected[6],(unsigned)expected[7],(unsigned char)expected[8],(unsigned char)expected[9])==(uintptr_t)&reference);
        assert(reference==0xabcdef1234567890ULL && observed_result==(uintptr_t)&reference && result_kind==1);break;
    case CREATION_INITIALIZE:
        prepare(kind,9);expected[1]=(uintptr_t)&reference;expected[5]=0xe3;expected[7]=0xd1234567;expected[8]=0x94;SetLastError(0x12345678);
        creation_initialize(expected[0],expected[1],expected[2],expected[3],expected[4],(unsigned char)expected[5],expected[6],(unsigned)expected[7],(unsigned char)expected[8]);
        assert(reference==0xfedcba9876543210ULL && !result_kind);break;
    case CREATION_PILOT:
        prepare(kind,7);expected[1]=(uintptr_t)&reference;SetLastError(0x12345678);
        assert(creation_pilot(expected[0],expected[1],expected[2],expected[3],expected[4],expected[5],expected[6])==(uintptr_t)&reference);
        assert(reference==0x123456789abcdef0ULL && observed_result==(uintptr_t)&reference && result_kind==1);break;
    case CREATION_NAVIGATION:
        prepare(kind,6);expected[4]=0xb4;expected[5]=0xc5;SetLastError(0x12345678);
        creation_navigation(expected[0],expected[1],expected[2],expected[3],(unsigned char)expected[4],(unsigned char)expected[5]);assert(!result_kind);break;
    case CREATION_AGENT:
        prepare(kind,4);SetLastError(0x12345678);assert(creation_agent(expected[0],expected[1],expected[2],expected[3])==expected[0]);assert(observed_result==expected[0] && result_kind==1);break;
    case CREATION_SETTER:
        prepare(kind,3);expected[2]=0xe7;SetLastError(0x12345678);creation_setter(expected[0],expected[1],(unsigned char)expected[2]);assert(!result_kind);break;
    case CREATION_RANDOM: {
        prepare(kind,2);expected[0]=(uintptr_t)&rng;expected[1]=0x3f000000;SetLastError(0x12345678);
        float result=creation_random(&rng,0.5f);uint32_t bits;memcpy(&bits,&result,4);
        assert(bits==0x3f123456 && observed_result==bits && result_kind==2 && rng==0x1020304050607093ULL);break;
    }
    case CREATION_EVENT_QUERY: {
        prepare(kind,5);expected[1]=0xf1234567;double minimum=-31.25,maximum=1024.125;
        memcpy(expected+2,&minimum,8);memcpy(expected+3,&maximum,8);expected[4]=0xd6;SetLastError(0x12345678);
        assert(creation_event_query(expected[0],(unsigned)expected[1],minimum,maximum,(unsigned char)expected[4])==0xa5);
        assert(observed_result==0xa5 && result_kind==3);break;
    }
    default: {
        assert(kind==CREATION_POST);prepare(kind,4);double due=1234.5;memcpy(expected+2,&due,8);expected[3]=0xc8;SetLastError(0x12345678);
        creation_post(expected[0],expected[1],due,(unsigned char)expected[3]);assert(!result_kind);break;
    }
    }
    finish_case(admitted);
}
int main(void) {
    assert(sizeof(creation_pins)/sizeof(*creation_pins)==24 && sizeof(creation_patch_contracts)/sizeof(*creation_patch_contracts)==9);
    thunk=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    image=VirtualAlloc(NULL,0xa00000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(thunk && image);
    void *targets[]={(void*)original_generate,(void*)original_initialize,(void*)original_pilot,(void*)original_navigation,
        (void*)original_agent,(void*)original_setter,(void*)original_random};
    for(unsigned i=0;i<7;i++)jump(thunk+3072+i*64,targets[i]);
    jump(image+0x99a210,(void*)original_query);jump(image+0x99a3f0,(void*)original_post);
    DWORD old;assert(VirtualProtect(thunk,4096,PAGE_EXECUTE_READ,&old) && VirtualProtect(image,0xa00000,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),thunk,4096) && FlushInstructionCache(GetCurrentProcess(),image,0xa00000));
    for(unsigned admitted=0;admitted<2;admitted++)for(unsigned kind=1;kind<=9;kind++)execute(kind,admitted);
    deactivate=1;for(unsigned kind=1;kind<=9;kind++)execute(kind,1);
    unsigned baseline_caught=0;
    __try {native_throw();} __except(exception_matches(GetExceptionInformation())) {baseline_caught=1;}
    assert(baseline_caught);
    throw_original=1;
    for(unsigned stop=0;stop<2;stop++)for(unsigned admitted=0;admitted<2;admitted++)for(unsigned kind=1;kind<=9;kind++) {
        deactivate=stop;unsigned caught=0;
        __try {execute(kind,admitted);assert(!"native exception must propagate");}
        __except(exception_matches(GetExceptionInformation())) {caught=1;}
        assert(caught && begins==1 && originals==1 && !ends && aborts==admitted && GetLastError()==0x55667788);
    }
    assert(VirtualFree(thunk,0,MEM_RELEASE) && VirtualFree(image,0,MEM_RELEASE));
    puts("creation production forwarders: nine GP/stack/XMM/outref/result/LastError ABIs, original-once; 36 real foreign-exception admission/STOP cases retain payload and unwind once; synthetic originals only");
    return 0;
}
