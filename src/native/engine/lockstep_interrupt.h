/* Native AI event/interrupt trace, with separately opt-in fixture RNG scope.
 * Native events, arguments and interpreter decisions are not replaced. */
#define INTERRUPT_ENTRY 0xb58ce0u
#define INTERRUPT_EVENT_ENTRY 0x2c6b418u
#define INTERRUPT_EVENT_HANDLER 0x1066750u
#define INTERRUPT_LIMIT 8192u
typedef uintptr_t (*interrupt_fn)(void *,void *,uint64_t,uint64_t,unsigned char);
typedef void (*interrupt_event_fn)(void *);
typedef struct {
    uint64_t tick,ordinal,begin,end,pc,rng_before,rng_after,values[2],result;
    unsigned actor,kind,event,flags_before,flags_after,types[2],mode;
    uint64_t trigger_cause,trigger_occurrence,trigger_steps;unsigned trigger_status;
    char script[96];
} interrupt_sample;
static interrupt_sample interrupt_samples[INTERRUPT_LIMIT];
static unsigned interrupt_count;
static uint64_t interrupt_calls,interrupt_original_word,interrupt_patched_word;
static volatile LONG64 interrupt_failures;
static int interrupt_installed,interrupt_event_installed,interrupt_restored,interrupt_seeded;
static uint64_t interrupt_actor_tick[ACTOR_CAPACITY],interrupt_actor_calls[ACTOR_CAPACITY],interrupt_seeds;
#include "lockstep_interrupt_faults.h"
static uint64_t *interrupt_rng(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
#include "lockstep_boot_rng_anchor.h"
#include "lockstep_interrupt_origin.h"
#include "lockstep_interrupt_step.h"
#include "lockstep_nested_action.h"
static interrupt_sample *interrupt_begin(void *context,unsigned kind,uint64_t *rng) {
    if(gate_play_mode && !interrupt_seeded) return NULL;
    unsigned actor=stats.active && boot_phase==3?ai_actor(context):0;
    if(!actor) return NULL;
    if(GetCurrentThreadId()!=ui_thread) {interrupt_fault(IFAULT_THREAD,actor,0,0,NULL);return NULL;}
    if(interrupt_count==INTERRUPT_LIMIT) {
        ++interrupt_dropped;if(interrupt_seeded) InterlockedIncrement64(&interrupt_failures);
        return NULL;
    }
    interrupt_sample *s=&interrupt_samples[interrupt_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++interrupt_calls;s->actor=actor;s->kind=kind;
    unsigned char *c=context;
    if(!rng || !ai_script(context,s->script) ||
       !read_local(c+0x18,&s->event,4) || !read_local(c+0x188,&s->flags_before,4) ||
       !read_local(c+0x140,&s->pc,8) || !read_local(c+0x20,&s->types[0],4) ||
       !read_local(c+0x30,&s->types[1],4) || !read_local(c+0x28,&s->values[0],8) ||
       !read_local(c+0x38,&s->values[1],8)) interrupt_fault(IFAULT_READ,actor,s->pc,0,s->script);
    s->rng_before=rng?*rng:0;return s;
}
static void interrupt_end(interrupt_sample *s,void *context,uint64_t *rng) {
    if(!s) return;
    s->rng_after=rng?*rng:0;
    if(!read_local((unsigned char*)context+0x188,&s->flags_after,4)) interrupt_fault(IFAULT_END,s->actor,s->pc,0,s->script);
}
static uintptr_t interrupt_observe_native(interrupt_fn original,uint64_t *rng,void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode,uintptr_t caller) {
    npc_pilot_scope pilot;int pilot_entered=npc_pilot_begin(&pilot,exec,NULL,2,rng,begin,end,mode);
    if(pilot_entered==1) pilot.sample->irq_origin=interrupt_origin_read(caller);
    uint32_t tag=0;uintptr_t value=0;void *context=NULL;
    if(stats.active && boot_phase==3 && read_local((unsigned char*)exec+0x38,&tag,4) && tag==0x66 &&
       read_local((unsigned char*)exec+0x40,&value,8) && value>=0xa8) context=(void*)(value-0xa8);
    registry_origin_scope origin;
    registry_origin_enter(&origin,REGISTRY_ORIGIN_RANGE,caller,exec,context,program,begin,end,mode,registry_origin_tls(),rng);
    interrupt_sample *s=context?interrupt_begin(context,1,rng):NULL;
    uint64_t saved=rng?*rng:0;
    int restore_rng=0;
    interrupt_sample *previous=interrupt_current;
    uintptr_t result=0;
    const int keyed=interrupt_seeded; /* a stop during the call clears the flag; this scope was keyed all the same */
    __try {
        __try {
            if(s) {
                s->begin=begin;s->end=end;s->mode=mode;
                npc_trigger_irq_begin(s);
                if(interrupt_seeded && rng) {
                    unsigned i=s->actor-1;char name[96];uint64_t script_hash=ai_script(context,name);
                    if(interrupt_actor_tick[i]!=stats.issued) {interrupt_actor_tick[i]=stats.issued;interrupt_actor_calls[i]=0;}
                    uint64_t sequence=++interrupt_actor_calls[i];
                    restore_rng=1;
                    *rng=actor_seed(0x41495f494e544552ULL,stats.issued,s->actor,script_hash,sequence,begin*0x9e3779b97f4a7c15ULL ^ end ^ ((uint64_t)mode<<63));
                    s->rng_before=*rng;++interrupt_seeds;
                }
            }
            interrupt_current=s;
            result=original(exec,program,begin,end,mode);
            interrupt_current=previous;
            if(s) {s->result=result;interrupt_end(s,context,rng);}
        } __finally {
            DWORD error=GetLastError();
            if(restore_rng) *rng=saved;
            SetLastError(error);
        }
        registry_origin_leave(&origin,result);
        npc_pilot_end(&pilot,pilot_entered,rng);
    } __finally {
        interrupt_current=previous;
        if(AbnormalTermination()) {
            interrupt_fault(IFAULT_UNWIND,s?s->actor:0,s?s->pc:0,0,s?s->script:NULL);
            if(keyed && !interrupt_seeded) InterlockedIncrement64(&interrupt_failures);
            registry_origin_abort(&origin);
        }
    }
    return result;
}
/* The optional per-script RNG scope encloses every range observer. */
static uintptr_t interrupt_observe(interrupt_fn original,uint64_t *rng,void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode,uintptr_t caller) {
    script_rng_frame scope;int scoped=script_rng_enter(&scope,SR_RANGE,exec,program,begin,end,mode,rng);uintptr_t result=0;
    __try {result=interrupt_observe_native(original,rng,exec,program,begin,end,mode,caller);}
    __finally {script_rng_leave(&scope,scoped,rng,AbnormalTermination());}
    return result;
}
static __attribute__((noinline)) uintptr_t interrupt_run(void *exec,void *program,uint64_t begin,uint64_t end,unsigned char mode) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    boot_rng_anchor_apply(BOOT_ANCHOR_RUN);
    return interrupt_observe((interrupt_fn)(thunk+800),interrupt_rng(),exec,program,begin,end,mode,caller);
}
static void interrupt_event_observe_native(interrupt_event_fn original,uint64_t *rng,void *context,uintptr_t caller) {
    npc_pilot_scope pilot;int pilot_entered=npc_pilot_begin(&pilot,NULL,context,3,rng,0,0,0);
    registry_origin_scope origin;
    registry_origin_enter(&origin,REGISTRY_ORIGIN_EVENT,caller,NULL,context,NULL,0,0,0,registry_origin_tls(),rng);
    interrupt_sample *s=interrupt_begin(context,0,rng);
    __try {
        original(context);interrupt_end(s,context,rng);
        registry_origin_leave(&origin,0);
        npc_pilot_end(&pilot,pilot_entered,rng);
    } __finally {
        if(AbnormalTermination()) {interrupt_fault(IFAULT_EVENT_UNWIND,s?s->actor:0,s?s->pc:0,0,s?s->script:NULL);registry_origin_abort(&origin);}
    }
}
static void interrupt_event_observe(interrupt_event_fn original,uint64_t *rng,void *context,uintptr_t caller) {
    script_rng_frame scope;int scoped=script_rng_enter(&scope,SR_EVENT,context,NULL,0,0,0,rng);
    __try {interrupt_event_observe_native(original,rng,context,caller);}
    __finally {script_rng_leave(&scope,scoped,rng,AbnormalTermination());}
}
static __attribute__((noinline)) void interrupt_event(void *context) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    boot_rng_anchor_apply(BOOT_ANCHOR_EVENT);
    interrupt_event_observe((interrupt_event_fn)(image+INTERRUPT_EVENT_HANDLER),interrupt_rng(),context,caller);
}
static int interrupt_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+INTERRUPT_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+INTERRUPT_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+INTERRUPT_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+INTERRUPT_ENTRY,8);return actual==expected;
}
static int interrupt_event_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+INTERRUPT_EVENT_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)(image+INTERRUPT_EVENT_ENTRY),value,expected);
    VirtualProtect(image+INTERRUPT_EVENT_ENTRY,8,old,&ignored);return actual==expected;
}
static void interrupt_restore(void) {
    int i=npc_deadline_restore();
    int j=npc_ready_restore();
    int h=npc_finish_restore();
    int g=npc_enqueue_restore();
    int f=npc_speak_restore();
    int e=npc_exec_restore();
    int d=nested_action_restore();
    int c=interrupt_step_restore();
    int a=!interrupt_installed || interrupt_swap(interrupt_patched_word,interrupt_original_word);
    int b=!interrupt_event_installed || interrupt_event_swap((void*)interrupt_event,image+INTERRUPT_EVENT_HANDLER);
    if(a) interrupt_installed=0;if(b) interrupt_event_installed=0;interrupt_restored=a && b && c && d && e && f && g && h && i && j;
}
static int interrupt_install(int seed) {
    if(!boot_mode || !ai_installed || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4lan_hooks_64.dll") ||
       !hash_ok(image+INTERRUPT_ENTRY,741,"748506325f4a887950dcd5993f0464dc5383e51862e1795af859ed6a73c25848") ||
       !hash_ok(image+INTERRUPT_EVENT_HANDLER,343,"0dd52bfd8ecef79bdc5743c2532d96f815451265c26e26ff209deac51eb5291a") ||
       *(void**)(image+INTERRUPT_EVENT_ENTRY)!=image+INTERRUPT_EVENT_HANDLER) return 0;
    intptr_t distance=(intptr_t)(thunk+832)-(intptr_t)(image+INTERRUPT_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(800)) return 0;
    memcpy(&interrupt_original_word,image+INTERRUPT_ENTRY,8);interrupt_patched_word=interrupt_original_word;
    unsigned char *patch=(unsigned char*)&interrupt_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);patch[5]=patch[6]=0x90;
    if(!interrupt_swap(interrupt_original_word,interrupt_patched_word)) return 0;
    interrupt_installed=1;
    if(!interrupt_event_swap(image+INTERRUPT_EVENT_HANDLER,(void*)interrupt_event)) {interrupt_restore();return 0;}
    interrupt_event_installed=1;
    if(!interrupt_step_install()) {interrupt_restore();return 0;}
    if(!nested_action_install()) {interrupt_restore();return 0;}
    if((boot_options&422212465065984ULL) && !npc_exec_install()) {interrupt_restore();return 0;}
    if((boot_options&281474976710656ULL) && !npc_speak_install()) {interrupt_restore();return 0;}
    if((boot_options&281474976710656ULL) && !npc_enqueue_install()) {interrupt_restore();return 0;}
    if((boot_options&281474976710656ULL) && !npc_finish_install()) {interrupt_restore();return 0;}
    if((boot_options&562949953421312ULL) && !npc_ready_install()) {interrupt_restore();return 0;}
    if((boot_options&562949953421312ULL) && !npc_deadline_install()) {interrupt_restore();return 0;}
    interrupt_seeded=seed;return 1;
}
