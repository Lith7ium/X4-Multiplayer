/* Read-only native action dispatch under a registered fixture interrupt step.
 * This observes the separate MD/general ExecState loop, not the AI interrupt
 * instruction helper. Context tags and explicit parents keep the scopes apart. */
#define NESTED_ACTION_WORD 0xb58ca0u
#define NESTED_ACTION_LIMIT 16384u
typedef void (*nested_action_fn)(void *,void *,void *);
typedef struct {
    uint64_t tick,ordinal,interrupt,step,parent,program,pc,next,type,action,before,after;
    unsigned actor,depth,owner,chance,flags_before,flags_after;
} nested_action_sample;
static nested_action_sample nested_actions[NESTED_ACTION_LIMIT],*nested_current;
static unsigned nested_action_count;
static uint64_t nested_action_calls;
static int nested_action_installed,nested_action_restored;
static unsigned __int128 nested_action_original,nested_action_patched;

static int nested_program_hash(void *exec,uint64_t *hash_out) {
    uintptr_t program,bounds[2];
    if(!read_local((unsigned char*)exec+0x10,&program,8) || !read_local((void*)program,bounds,16) ||
       !bounds[0] || bounds[1]<bounds[0] || ((bounds[1]-bounds[0])&7) ||
       (bounds[1]-bounds[0])/8>1048576) return 0;
    uint64_t hash=14695981039346656037ULL;
    for(uintptr_t p=bounds[0];p<bounds[1];p+=8) {
        uintptr_t n,t;if(!read_local((void*)p,&n,8) || !read_local((void*)n,&t,8) ||
            t<(uintptr_t)image || t>=(uintptr_t)(image+0x6fa9000)) return 0;
        hash=(hash^(t-(uintptr_t)image))*1099511628211ULL;
    }
    *hash_out=hash;return 1;
}

static int nested_action_read(void *node,void *exec,nested_action_sample *s) {
    uintptr_t type,action,program,bounds[2];uint16_t flags;
    if(!read_local(node,&type,8) || type<(uintptr_t)image || type>(uintptr_t)(image+0x6fa9000-32) ||
       !read_local((void*)(type+0x18),&action,8) || action<(uintptr_t)image || action>=(uintptr_t)(image+0x6fa9000) ||
       !read_local((unsigned char*)node+0x10,&s->chance,4) ||
       !read_local((unsigned char*)exec+0x18,&s->pc,8) || !s->pc ||
       !read_local((unsigned char*)exec+0x30,&flags,2) ||
       !read_local((unsigned char*)exec+0x38,&s->owner,4) ||
       !read_local((unsigned char*)exec+0x10,&program,8) ||
       !read_local((void*)program,bounds,16) || !bounds[0] || bounds[1]<bounds[0] ||
       ((bounds[1]-bounds[0])&7) || (bounds[1]-bounds[0])/8>1048576 ||
       s->pc>(bounds[1]-bounds[0])/8) return 0;
    uintptr_t current;if(!read_local((void*)(bounds[0]+(s->pc-1)*8),&current,8) || current!=(uintptr_t)node) return 0;
    --s->pc;s->flags_before=flags;s->type=type-(uintptr_t)image;s->action=action-(uintptr_t)image;
    /* Stable program shape, never an address. The native program is immutable
     * during execution; read every action type in the bounded vector. */
    return nested_program_hash(exec,&s->program);
}
static void nested_action_observe(nested_action_fn original,void *node,void *exec,void *frame,uint64_t *rng) {
    if(GetCurrentThreadId()!=ui_thread || !interrupt_current || !interrupt_step_active) {original(node,exec,frame);return;}
    if(nested_action_count==NESTED_ACTION_LIMIT) {++nested_action_dropped;original(node,exec,frame);return;}
    if(!rng) {interrupt_fault(IFAULT_ACTION_RNG,interrupt_current->actor,0,0,interrupt_current->script);original(node,exec,frame);return;}
    nested_action_sample *s=&nested_actions[nested_action_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++nested_action_calls;s->interrupt=interrupt_current->ordinal;
    s->step=interrupt_step_active->ordinal;s->actor=interrupt_current->actor;
    s->parent=nested_current?nested_current->ordinal:0;s->depth=nested_current?nested_current->depth+1:1;
    const char *script=interrupt_current->script;
    if(s->depth>64) interrupt_fault(IFAULT_ACTION_DEPTH,s->actor,0,s->depth,script);
    else if(frame!=(unsigned char*)exec+0x38) interrupt_fault(IFAULT_ACTION_FRAME,s->actor,0,s->depth,script);
    else if(!nested_action_read(node,exec,s)) interrupt_fault(IFAULT_ACTION_READ,s->actor,s->pc,s->depth,script);
    s->before=*rng;nested_action_sample *previous=nested_current;nested_current=s;
    __try {
        original(node,exec,frame);
        nested_current=previous;s->after=*rng;uint16_t flags;
        if(!read_local((unsigned char*)exec+0x18,&s->next,8) || !read_local((unsigned char*)exec+0x30,&flags,2))
            interrupt_fault(IFAULT_ACTION_AFTER,s->actor,s->pc,s->depth,interrupt_current?interrupt_current->script:NULL);
        else s->flags_after=flags;
    } __finally {
        nested_current=previous;
        if(AbnormalTermination()) interrupt_fault(IFAULT_ACTION_UNWIND,s->actor,s->pc,s->depth,interrupt_current?interrupt_current->script:NULL);
    }
}
#include "lockstep_npc_action.h"
#include "lockstep_npc_provenance.h"
#include "lockstep_npc_pilot.h"
#include "lockstep_npc_trigger.h"
#include "lockstep_script_rng.h"
#include "lockstep_npc_rng.h"
#include "lockstep_npc_speak.h"
#include "lockstep_npc_enqueue.h"
#include "lockstep_npc_finish.h"
#include "lockstep_npc_ready.h"
#include "lockstep_npc_deadline.h"
/* Optional resident read-only observer, published atomically only by its own
 * explicit configuration. It receives the existing original-once chain. */
typedef void (*world_action_callback_fn)(nested_action_fn,void *,void *,void *);
static PVOID volatile world_action_callback;
static void nested_action_chain(void *node,void *exec,void *frame) {
    nested_action_fn original=*(nested_action_fn*)(*(unsigned char**)node+0x18);
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && (boot_options&281474976710656ULL)) {
        npc_trigger_action_context context;npc_trigger_action_begin(&context,node,exec);
        npc_action_observe(original,node,exec,frame,interrupt_rng());npc_trigger_action_end(&context);
    } else npc_action_observe(original,node,exec,frame,interrupt_rng());
}
static void nested_action_run(void *node,void *exec,void *frame) {
    boot_rng_anchor_apply(BOOT_ANCHOR_ACTION);
    world_action_callback_fn observer=(world_action_callback_fn)InterlockedCompareExchangePointer(&world_action_callback,NULL,NULL);
    if(observer) observer(nested_action_chain,node,exec,frame);else nested_action_chain(node,exec,frame);
}
static int nested_action_swap(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+NESTED_ACTION_WORD,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+NESTED_ACTION_WORD),expected,value);
    VirtualProtect(image+NESTED_ACTION_WORD,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NESTED_ACTION_WORD,16);
    return actual==expected;
}
static int nested_action_restore(void) {
    if(!nested_action_installed) return 1;
    nested_action_restored=nested_action_swap(nested_action_patched,nested_action_original);
    if(nested_action_restored) nested_action_installed=0;return nested_action_restored;
}
static int nested_action_install(void) {
    if(!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !npc_layout_pinned() ||
       !hash_ok(image+0xb58bb0,303,"7509510d2582f326fb87c58a23b715d1f7499577bf681c67377347314e3aa759")) return 0;
    intptr_t distance=(intptr_t)(thunk+1024)-(intptr_t)(image+0xb58ca9);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1024)) return 0;
    memcpy(&nested_action_original,image+NESTED_ACTION_WORD,16);nested_action_patched=nested_action_original;
    unsigned char *patch=(unsigned char*)&nested_action_patched;patch[4]=0xe8;
    int32_t relative=(int32_t)distance;memcpy(patch+5,&relative,4);patch[9]=0x90;
    if(!nested_action_swap(nested_action_original,nested_action_patched)) return 0;
    nested_action_installed=1;return 1;
}
