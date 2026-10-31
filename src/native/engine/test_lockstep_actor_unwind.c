/* Actual production writer helpers over owned memory and typed synthetic
 * originals. Real Windows SEH, no X4 instruction or hook installation. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

#define WRITERS 12u
#define FOREIGN_EXCEPTION 0xe0427777u
#define CALLER_WORD 0x1122334455667788ULL
#define MUTATION 0xfedcba9876543210ULL
#define AFTER_SENTINEL 0xdeadbeef12345678ULL
static uint64_t word,alternate_word,*selected_rng=&word;
static unsigned writer,depth,originals,caught,throw_at[2],stop_at[2],nested;
static unsigned char object[0x1000],context[0x200],script[0x80],exec_state[0x80];
static unsigned char native_tls[0x1000],replacement_tls[0x1000];
static void **test_tls;static int replace_tls;
static flight_sample fs[2];static ai_sample ais[2];static agent_sample as[2];
static ship_sample ss[2];static weapon_sample ws[2],outer_weapon;
static nav_sample ns[2];static shape_sample shs[2];static bullet_identity bs[2];
static bullet_event be[2];static death_event de[2];static attention_sample ats[2];
static gravidar_sample gs[2];static interrupt_sample outer_interrupt;
static int *seed_flags[WRITERS]={&flight_seeded,&ai_seeded,&agent_seeded,&ship_seeded,
    &weapon_seeded,&nav_seeded,&shape_seeded,&bullet_seeded,&death_seeded,
    &attention_seeded,&gravidar_seeded,&interrupt_seeded};
static volatile LONG64 *failures[WRITERS]={(volatile LONG64*)&flight_failures,
    (volatile LONG64*)&ai_failures,(volatile LONG64*)&agent_failures,
    (volatile LONG64*)&ship_failures,(volatile LONG64*)&weapon_failures,
    (volatile LONG64*)&nav_failures,(volatile LONG64*)&shape_failures,
    (volatile LONG64*)&bullet_failures,(volatile LONG64*)&death_failures,
    (volatile LONG64*)&attention_failures,&gravidar_failures,&interrupt_failures};
static const char *names[WRITERS]={"flight","ai","agent","ship","weapon","nav",
    "shape","bullet","death","attention","gravidar","range"};
/* Captured independently from the unchanged 74ced4a production helpers using
 * this exact synthetic input tuple; checked byte-for-byte before freezing. */
static const uint64_t normal_before[WRITERS]={0x99f17f2a2f63a54fULL,0x8fd87dc57243b446ULL,
    0x2897389dedd6e1efULL,0x2d7b94f11bc05f59ULL,0x35efd954fabea16dULL,0xf7b4ed57a96d43b6ULL,
    0xe773774ea3cd5b15ULL,0x437a799e30f4f35cULL,0xea137881f40c01b0ULL,0xd498803ac7345d74ULL,
    0xbd5ec879e09a7cf8ULL,0xf37e616a842fe0a3ULL};
static uintptr_t range_result;
static unsigned exception_flags;

static void invoke(void);
static int exception_filter(EXCEPTION_POINTERS *p) {
    if(p->ExceptionRecord->ExceptionCode!=FOREIGN_EXCEPTION) return EXCEPTION_CONTINUE_SEARCH;
    assert(p->ExceptionRecord->NumberParameters==2);
    assert(p->ExceptionRecord->ExceptionInformation[0]==0x123456789abcdef0ULL);
    assert(p->ExceptionRecord->ExceptionInformation[1]==0xfedcba9876543210ULL);
    assert(p->ExceptionRecord->ExceptionFlags==exception_flags);
    assert(GetLastError()==0x4a70+depth);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void native_throw(void) {
    ULONG_PTR args[]={0x123456789abcdef0ULL,0xfedcba9876543210ULL};
    SetLastError(0x4a70+depth);RaiseException(FOREIGN_EXCEPTION,0,2,args);
}
static void check_parent_context(void) {
    if(writer==4) assert(weapon_context==&ws[0] && weapon_context_actor==1 && weapon_context_slot==3);
    if(writer==11) assert(interrupt_current && interrupt_current!=&outer_interrupt);
}
static void effect(void) {
    ++originals;
    if(selected_rng) *selected_rng^=MUTATION;
    if(nested && !depth) {
        const uint64_t parent=*selected_rng;
        depth=1;
        __try {invoke();}
        __except(exception_filter(GetExceptionInformation())) {++caught;}
        depth=0;
        assert(*selected_rng==parent);check_parent_context();
        /* The native parent continues with another sibling before returning. */
        unsigned was_throwing=throw_at[1];throw_at[1]=0;depth=1;
        invoke();depth=0;throw_at[1]=was_throwing;
        assert(*selected_rng==parent);check_parent_context();
    }
    if(stop_at[depth]) {stats.active=0;*seed_flags[writer]=0;}
    if(replace_tls) {test_tls[0]=replacement_tls;alternate_word=*(uint64_t*)(replacement_tls+0x318);}
    SetLastError(0x4a70+depth);
    if(throw_at[depth]) native_throw();
}
static void original1(void *a) {assert(a==(writer==1?(void*)exec_state:(void*)object));effect();}
static void original2(void *a,double b) {assert(a==object && b==7.125);effect();}
static void originalshape(void *a,unsigned char b) {assert(a==object && b==0xa7);effect();}
static void original3(void *a,void *b,void *c) {assert(a==object && b==object+8 && c==object+16);effect();}
static void original5(void *a,void *b,double c,unsigned d,void *e) {
    assert(a==object && b==object+8 && c==7.125 && d==0xabcdef01 && e==object+16);effect();
}
static void originalagent(void *a,void *b,float c,double d,unsigned char e) {
    assert(a==object && b==object+8 && c==1.25f && d==7.125 && e==(agent_full?0:0xa7));effect();
}
static void originalflight(void *a,void *b,float c,double d,unsigned char e,unsigned char f) {
    assert(a==object && b==object+8 && c==1.25f && d==7.125 && e==(flight_full?0:0xa7) && f==0xb8);effect();
}
static uintptr_t originalrange(void *a,void *b,uint64_t c,uint64_t d,unsigned char e) {
    assert(a==exec_state && b==script && c==0x123456789abcdef0ULL && d==0x9876543210abcdefULL && e==0xa7);
    effect();return 0xf123456789abcdefULL;
}
static void invoke(void) {
    switch(writer) {
    case 0:flight_execute(originalflight,&fs[depth],1,selected_rng,object,object+8,1.25f,7.125,0xa7,0xb8);break;
    case 1:ai_execute(original1,&ais[depth],1,selected_rng,exec_state);break;
    case 2:agent_execute(originalagent,&as[depth],1,selected_rng,object,object+8,1.25f,7.125,0xa7);break;
    case 3:ship_execute(original1,&ss[depth],1,selected_rng,object);break;
    case 4:weapon_execute(original1,&ws[depth],1,3,selected_rng,object);break;
    case 5:nav_execute(original2,&ns[depth],1,selected_rng,object,7.125);break;
    case 6:shape_execute(originalshape,&shs[depth],1,0,selected_rng,object,NULL,7.125,0xa7);break;
    case 7:bullet_execute(original1,&bs[depth],&be[depth],selected_rng,object);break;
    case 8:death_execute(original5,&de[depth],selected_rng,object,object+8,7.125,0xabcdef01,object+16);break;
    case 9:attention_execute(original1,&ats[depth],1,selected_rng,object,7.125);break;
    case 10:gravidar_execute(original3,&gs[depth],1,selected_rng,object,object+8,object+16);break;
    case 11:range_result=interrupt_observe(originalrange,selected_rng,exec_state,script,
        0x123456789abcdef0ULL,0x9876543210abcdefULL,0xa7,0);break;
    default:assert(0);
    }
}
static uint64_t *after_word(void) {
    switch(writer) {
    case 0:return &fs[0].rng_after;case 1:return &ais[0].rng_after;case 2:return &as[0].rng_after;
    case 3:return &ss[0].rng_after;case 4:return &ws[0].rng_after;case 5:return &ns[0].rng_after;
    case 6:return &shs[0].rng_after;case 7:return &be[0].after;case 8:return &de[0].after;
    case 9:return &ats[0].after;case 10:return &gs[0].rng_after;
    default:return NULL;
    }
}
static uint64_t before_word(void) {
    switch(writer) {
    case 0:return fs[0].rng_before;case 1:return ais[0].rng_before;case 2:return as[0].rng_before;
    case 3:return ss[0].rng_before;case 4:return ws[0].rng_before;case 5:return ns[0].rng_before;
    case 6:return shs[0].rng_before;case 7:return be[0].before;case 8:return de[0].before;
    case 9:return ats[0].before;case 10:return gs[0].rng_before;default:return interrupt_samples[0].rng_before;
    }
}
static void reset(void) {
    stats.active=1;stats.issued=5;boot_phase=3;ui_thread=GetCurrentThreadId();
    *seed_flags[writer]=1;depth=originals=caught=nested=replace_tls=0;
    agent_full=flight_full=0;
    throw_at[0]=throw_at[1]=stop_at[0]=stop_at[1]=0;word=CALLER_WORD;selected_rng=&word;
    weapon_context=&outer_weapon;weapon_context_actor=17;weapon_context_slot=29;
    interrupt_current=&outer_interrupt;interrupt_count=0;interrupt_actor_calls[0]=0;
    if(after_word()) *after_word()=AFTER_SENTINEL;
    range_result=AFTER_SENTINEL;
}
static void check_outer_context(void) {
    assert(weapon_context==&outer_weapon && weapon_context_actor==17 && weapon_context_slot==29);
    assert(interrupt_current==&outer_interrupt && !registry_origin_current);
}
static void catch_in_parent(void) {
    __try {invoke();}
    __except(exception_filter(GetExceptionInformation())) {++caught;}
}
static void test_writer(void) {
    for(unsigned throwing=0;throwing<2;throwing++) for(unsigned stop=0;stop<2;stop++) {
        reset();throw_at[0]=throwing;stop_at[0]=stop;
        LONG64 before=*failures[writer];catch_in_parent();
        assert(word==CALLER_WORD && originals==1 && caught==throwing);
        assert(before_word()==normal_before[writer]);
        assert(GetLastError()==0x4a70 && *failures[writer]==before+throwing);
        if(throwing) {
            if(after_word()) assert(*after_word()==AFTER_SENTINEL);
            else assert(interrupt_samples[0].rng_after==0 && interrupt_samples[0].result==0);
            assert(range_result==AFTER_SENTINEL);
        } else if(writer==11) assert(range_result==0xf123456789abcdefULL);
        check_outer_context();
    }
    if(writer==0 || writer==2) {
        reset();agent_full=flight_full=1;invoke();assert(word==CALLER_WORD && originals==1);check_outer_context();
    }
    for(unsigned throwing=0;throwing<2;throwing++) {
        reset();*seed_flags[writer]=0;throw_at[0]=throwing;catch_in_parent();
        assert(word==(CALLER_WORD^MUTATION) && originals==1 && caught==throwing);check_outer_context();
        if(writer!=10) { /* Gravidar's selected helper requires its guarded nonnull TLS. */
            reset();selected_rng=NULL;throw_at[0]=throwing;catch_in_parent();
            assert(word==CALLER_WORD && originals==1 && caught==throwing);check_outer_context();
        }
    }
    for(unsigned outer_throw=0;outer_throw<2;outer_throw++) for(unsigned inner_throw=0;inner_throw<2;inner_throw++) {
        reset();nested=1;throw_at[0]=outer_throw;throw_at[1]=inner_throw;LONG64 before=*failures[writer];
        catch_in_parent();assert(word==CALLER_WORD && originals==3 && caught==outer_throw+inner_throw);
        assert(*failures[writer]==before+outer_throw+inner_throw);check_outer_context();
    }
    /* Both owned allocations stay live. Cleanup must restore the selected address,
     * not follow a replacement TLS[0] or overwrite its independent native state. */
    for(unsigned throwing=0;throwing<2;throwing++) {
        reset();throw_at[0]=throwing;replace_tls=1;
        void *old=test_tls[0];test_tls[0]=native_tls;
        selected_rng=(uint64_t*)(native_tls+0x318);*selected_rng=CALLER_WORD;
        *(uint64_t*)(replacement_tls+0x318)=0x76543210fedcba98ULL;
        __try {catch_in_parent();assert(*selected_rng==CALLER_WORD && test_tls[0]==replacement_tls);
            assert(*(uint64_t*)(replacement_tls+0x318)==alternate_word && originals==1 && caught==throwing);}
        __finally {test_tls[0]=old;}
        check_outer_context();
    }
    printf("%s: original ABI/once, normal/foreign exception, STOP/options, disabled/null, caught child+sibling, TLS replacement PASS\n",names[writer]);
}
static void emit_original(unsigned char *at,void *fn) {
    at[0]=0x48;at[1]=0xb8;memcpy(at+2,&fn,8);at[10]=0xff;at[11]=0xe0;
}
static void route_excluded(void) {
    switch(writer) {
    case 0:flight_update(object,object+8,1.25f,7.125,0xa7,0xb8);break;
    case 1:ai_run(exec_state);break;case 2:agent_update(object,object+8,1.25f,7.125,0xa7);break;
    case 3:ship_move(object);break;case 4:weapon_move(object);break;case 5:nav_move(object,7.125);break;
    case 6:shape_move(object,0xa7);break;case 7:bullet_move(object);break;
    case 8:death_listener(object,object+8,7.125,0xabcdef01,object+16);break;
    case 9:attention_refresh(object);break;case 10:gravidar_unit(object,object+8,object+16);break;
    case 11:range_result=interrupt_run(exec_state,script,0x123456789abcdef0ULL,0x9876543210abcdefULL,0xa7);break;
    }
}
static void test_excluded_routes(void) {
    image=VirtualAlloc(NULL,0x4000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    thunk=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(image && thunk);
    emit_original(image+FLIGHT_UPDATE,(void*)originalflight);emit_original(image+AI_ORIGINAL,(void*)original1);
    emit_original(image+AGENT_ORIGINAL,(void*)originalagent);emit_original(image+SHIP_MOVE,(void*)original1);
    emit_original(image+WEAPON_MOVE,(void*)original1);emit_original(image+NAV_MOVE,(void*)original2);
    emit_original(thunk+384,(void*)originalshape);emit_original(image+BULLET_MOVE,(void*)original1);
    emit_original(image+DEATH_LISTENER,(void*)original5);emit_original(thunk+576,(void*)original1);
    emit_original(image+GRAVIDAR_UNIT_APPLY,(void*)original3);emit_original(thunk+800,(void*)originalrange);
    assert(FlushInstructionCache(GetCurrentProcess(),image,0x4000000) && FlushInstructionCache(GetCurrentProcess(),thunk,4096));
    for(writer=0;writer<WRITERS;writer++) for(unsigned throwing=0;throwing<2;throwing++) {
        reset();stats.active=0;throw_at[0]=throwing;
        __try {route_excluded();}
        __except(exception_filter(GetExceptionInformation())) {++caught;}
        assert(word==(CALLER_WORD^MUTATION) && originals==1 && caught==throwing);check_outer_context();
    }
    VirtualFree(image,0,MEM_RELEASE);VirtualFree(thunk,0,MEM_RELEASE);image=thunk=NULL;
    puts("all twelve inactive production routes: no seed, original once, exact foreign exception PASS");
}
static unsigned char publication_worker[0x7000];
static void publication_original(void *worker,unsigned char flag) {
    assert(worker==publication_worker && !flag && gravidar_in_frame);++originals;
    if(stop_at[0]) stats.active=0;
    SetLastError(0x4a70);if(throw_at[0]) native_throw();
}
static void test_publication(void) {
    unsigned char *worker=publication_worker;
    image=(unsigned char*)0x10000000;*(void**)worker=image+0x2c68860;
    *(uint32_t*)(worker+0x40)=2;*(uint32_t*)(worker+0x44)=1;worker[0x6988]=1;
    InitializeCriticalSection((CRITICAL_SECTION*)(worker+0x6960));
    writer=10;
    for(unsigned previous=0;previous<2;previous++) for(unsigned throwing=0;throwing<2;throwing++) for(unsigned stop=0;stop<2;stop++) {
        reset();gravidar_in_frame=(LONG)previous;throw_at[0]=throwing;stop_at[0]=stop;
        LONG64 before=gravidar_failures;uint64_t frames=gravidar_frames;
        __try {assert(gravidar_frame(publication_original,worker,0));}
        __except(exception_filter(GetExceptionInformation())) {++caught;}
        assert(originals==1 && caught==throwing && gravidar_in_frame==(LONG)previous);
        assert(gravidar_failures==before+throwing && gravidar_frames==frames+!throwing && GetLastError()==0x4a70);
    }
    reset();gravidar_in_frame=0;worker[0x6988]=0;
    assert(!gravidar_frame(publication_original,worker,0) && !originals && !gravidar_in_frame);
    DeleteCriticalSection((CRITICAL_SECTION*)(worker+0x6960));image=NULL;
    puts("Gravidar publication: normal/throw/STOP, parent guard and prepublication refusal PASS");
}
int main(int argc,char **argv) {
    test_tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);assert(test_tls);
    __try {native_throw();}
    __except((exception_flags=GetExceptionInformation()->ExceptionRecord->ExceptionFlags),EXCEPTION_EXECUTE_HANDLER) {}
    *(uint64_t*)(object+8)=100;actor_components[0]=object;actor_ids[0]=100;
    *(void**)(context+0x120)=object;*(void**)(context+0xd8)=script;
    memcpy(script+0x10,"test",5);*(uint64_t*)(script+0x20)=4;*(uint64_t*)(script+0x28)=15;
    *(uint32_t*)(exec_state+0x38)=0x66;*(uintptr_t*)(exec_state+0x40)=(uintptr_t)context+0xa8;
    for(unsigned i=0;i<2;i++) {bs[i].actor=1;bs[i].slot=3;de[i].actor=1;de[i].tick=5;}
    if(argc==2 && !strcmp(argv[1],"--normal")) {
        for(writer=0;writer<WRITERS;writer++) {
            reset();invoke();check_outer_context();
            printf("%s before=%016llx after=%016llx caller=%016llx originals=%u result=%016llx last_error=%lu\n",
                names[writer],(unsigned long long)before_word(),(unsigned long long)(after_word()?*after_word():interrupt_samples[0].rng_after),
                (unsigned long long)word,originals,(unsigned long long)range_result,(unsigned long)GetLastError());
        }
        return 0;
    }
    unsigned first=0,last=WRITERS;if(argc==2) {first=(unsigned)strtoul(argv[1],NULL,10);last=first+1;assert(first<WRITERS);}
    for(writer=first;writer<last;writer++) test_writer();
    if(argc==1) {test_excluded_routes();test_publication();}
    puts("production actor RNG unwind: PASS; synthetic originals/owned memory, no native game");return 0;
}
