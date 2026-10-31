/* Read-only NPC_UseCases lifecycle trace, including execution outside fixture
 * interrupts. Names are decoded from the pinned native cue-name tree; no engine
 * formatter, expression evaluator or variable getter is invoked. */
#define NPC_ACTION_LIMIT 16384u
#define NPC_NAME_LIMIT 512u
typedef struct {nested_action_sample state;unsigned named;char cue[256];} npc_action_sample;
typedef struct {uintptr_t definition;char cue[256];} npc_name_entry;
static npc_action_sample npc_actions[NPC_ACTION_LIMIT],*npc_current,*npc_speech_owner;
static npc_name_entry npc_names[NPC_NAME_LIMIT];
static unsigned npc_action_count;
static uint64_t npc_action_calls;
/* The rows have a reader: native_gate_npc_lifecycle drains them, native_gate_npc_diag's speech
 * ownership points into them. Without either every action of NPC_UseCases filled a table nobody
 * emptied, at about 32 actions a second of game time: 16384 after 518 s, and then a stop. */
static int npc_action_rows;
static int npc_action_wanted(void) {return !gate_play_mode && (npc_action_rows || (boot_options&281474976710656ULL));}

#include "lockstep_cue_name.h"
static int npc_cue_name_ex(void *exec,char out[256],int npc_only) {return npc_cue_name_with(read_local,exec,out,npc_only);}
static int npc_cue_name(void *exec,char out[256]) {return npc_cue_name_ex(exec,out,1);}
static int npc_layout_pinned(void) {
    return hash_ok(image+0x1118b30,575,"848956991261f520dc498d466994425ccaee4e413188a70a1244db31b5b7c5bf") &&
        hash_ok(image+0x1110bf0,113,"8c2d5d728f4c7d2d68f2d95b0aa54b2c1f1089f10452cedfa80154256f2f84c1") &&
        hash_ok(image+0x110ef50,472,"67e8b01827bdcb48266391cab6c13218fd49477c2df46e8c1f0b8eae1dba0046") &&
        hash_ok(image+0xccc890,523,"96aefc74eb0a44e1e87407d86e80dab0648cf94197f8b06c7e88dd3cf56185d6");
}
static void npc_action_observe(nested_action_fn original,void *node,void *exec,void *frame,uint64_t *rng) {
    if(!npc_action_wanted() || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) {nested_action_observe(original,node,exec,frame,rng);return;}
    npc_action_sample *previous_speech_owner=npc_speech_owner,*previous=npc_current;
    npc_speech_owner=NULL; /* A nested foreign action cannot inherit an ancestor's speech ownership. */
    __try {
    char cue[256]="unreadable";int named=npc_cue_name(exec,cue);
    if(named==2) {nested_action_observe(original,node,exec,frame,rng);goto done;}
    if(npc_action_count==NPC_ACTION_LIMIT) {++npc_action_dropped;nested_action_observe(original,node,exec,frame,rng);goto done;}
    if(!rng) {interrupt_fault(IFAULT_NPC_RNG,0,0,0,cue);nested_action_observe(original,node,exec,frame,rng);goto done;}
    npc_action_sample *record=&npc_actions[npc_action_count++];memset(record,0,sizeof(*record));
    nested_action_sample *s=&record->state;record->named=(unsigned)named;strcpy(record->cue,cue);
    s->tick=stats.issued;s->ordinal=++npc_action_calls;s->parent=npc_current?npc_current->state.ordinal:0;
    s->depth=npc_current?npc_current->state.depth+1:1;
    if(interrupt_step_active) {s->interrupt=interrupt_step_active->parent;s->step=interrupt_step_active->ordinal;s->actor=interrupt_step_active->actor;}
    if(!named) interrupt_fault(IFAULT_NPC_NAME,s->actor,0,s->depth,cue);
    else if(s->depth>64) interrupt_fault(IFAULT_NPC_DEPTH,s->actor,0,s->depth,cue);
    else if(frame!=(unsigned char*)exec+0x38) interrupt_fault(IFAULT_NPC_FRAME,s->actor,0,s->depth,cue);
    else if(!nested_action_read(node,exec,s)) interrupt_fault(IFAULT_NPC_READ,s->actor,s->pc,s->depth,cue);
    s->before=*rng;npc_current=record;
    if(named && s->action==0xbf1e80) npc_speech_owner=record;
    nested_action_observe(original,node,exec,frame,rng);
    npc_current=previous;npc_speech_owner=previous_speech_owner;s->after=*rng;uint16_t flags;
    if(!read_local((unsigned char*)exec+0x18,&s->next,8) || !read_local((unsigned char*)exec+0x30,&flags,2))
        interrupt_fault(IFAULT_NPC_AFTER,s->actor,s->pc,s->depth,record->cue);
    else s->flags_after=flags;
done:;
    } __finally {
        npc_current=previous;npc_speech_owner=previous_speech_owner;
        if(AbnormalTermination()) interrupt_fault(IFAULT_NPC_UNWIND,0,0,0,NULL);
    }
}
