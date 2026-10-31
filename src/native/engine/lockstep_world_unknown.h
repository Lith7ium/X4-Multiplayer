/* Bounded examples explain unresolved ownership; they never classify it as
 * foreign or remove the parent Action observer's strict unknown count. */
#define WORLD_VALUES_UNKNOWN_MAX 28u
#include "lockstep_world_unknown_identity.h"
typedef struct {
    volatile LONG complete;unsigned reason,reads,owned,errors,thread,ui,boot,raw_tag;
    uint64_t tick,raw_value;uintptr_t exec,current;world_action_guard owner;world_queue_phase_sample phase;
    wui_result identity;
} world_values_unknown_event;
static world_values_unknown_event world_values_unknown_events[WORLD_VALUES_UNKNOWN_MAX];
static LONG64 world_values_unknown_counts[8],world_values_unknown_total,world_values_unknown_omitted;
static unsigned world_values_unknown_reserved,world_values_unknown_exported;
static unsigned world_unknown_identity_exported;
static int world_unknown_identity_read(void *unused,uint64_t address,void *out,size_t size) {
    (void)unused;return address<=UINTPTR_MAX && read_local((void*)(uintptr_t)address,out,size);
}
static uint64_t world_unknown_identity_saved(void *unused,uint64_t runtime) {(void)unused;return world_identity_lookup(runtime);}
static int world_unknown_identity_stopped(void *unused) {(void)unused;return world_values_closing || !stats.active;}
static int world_unknown_identity_stub(void *unused,const unsigned char code[36]) {
    (void)unused;return hash_ok(code,36,"95552e650460a140f624748f17306613061330a17be9794f9e1e1d21abfb2fa0");
}
static void world_unknown_identity_phase(wui_result *r,unsigned which,const world_queue_phase_sample *p) {
    r->phase_flags[which][0]=p->app_flags;r->phase_flags[which][1]=p->vbi_loading;
    r->phase_flags[which][2]=p->view;r->phase_flags[which][3]=p->main;
    r->phase_clocks[which][0]=p->tls_time;r->phase_clocks[which][1]=p->game;
    r->phase_clocks[which][2]=p->real;r->phase_clocks[which][3]=p->saved;
}
static void world_unknown_identity_capture(world_values_unknown_event *e,void *node,void *exec,void *frame) {
    wui_result *r=&e->identity;
    if(e->reason!=6 || e->reads!=31 || !e->owned || !e->phase.eligible) {r->error=WUI_BOUNDARY;return;}
    const wui_api api={NULL,world_unknown_identity_read,world_unknown_identity_saved,world_unknown_identity_stopped,world_unknown_identity_stub};
    wui_observe(&api,(uintptr_t)image,(uintptr_t)node,(uintptr_t)exec,(uintptr_t)frame,r);
    world_unknown_identity_phase(r,0,&e->phase);r->boundary=1;
    world_action_guard again={0};unsigned reason=0;world_queue_phase_sample phase={0};
    int same=world_action_owner_ex(exec,&again,&reason)==0 && reason==6 &&
        again.tag==e->owner.tag && again.context==e->owner.context && again.lifetime==e->owner.lifetime &&
        again.entity==e->owner.entity && again.runtime==e->owner.runtime && again.saved==e->owner.saved &&
        npc_provenance_current_matches((uintptr_t)exec) && world_action_phase(&phase,world_queue_tls());
    world_unknown_identity_phase(r,1,&phase);
    if(same && !memcmp(r->phase_flags[0],r->phase_flags[1],sizeof(r->phase_flags[0])) &&
       !memcmp(r->phase_clocks[0],r->phase_clocks[1],sizeof(r->phase_clocks[0]))) r->boundary|=2;
    else r->error|=WUI_BOUNDARY;
    for(unsigned i=0;i<2;i++) {
        const wui_script *s=i?&r->after.script:&r->before.script;
        if(s->q[6]!=e->owner.context || s->q[7]!=e->owner.lifetime || s->q[8]!=e->owner.entity || s->q[9]!=e->owner.runtime) r->error|=WUI_BOUNDARY;
    }
}
static unsigned world_values_unknown_ready(void) {
    unsigned count=0;while(count<world_values_unknown_reserved&&InterlockedCompareExchange(&world_values_unknown_events[count].complete,0,0)) ++count;return count;
}
static void world_values_unknown_capture(void *node,void *exec,void *frame,unsigned reason,const world_action_guard *owner) {
    if(!world_values_admit()) return;
    world_values_unknown_event *e=NULL;
    AcquireSRWLockExclusive(&world_values_lock);
    ++world_values_unknown_total;
    if(!reason||reason>7) ++world_values_failures;
    else if(++world_values_unknown_counts[reason]<=4&&world_values_unknown_reserved<WORLD_VALUES_UNKNOWN_MAX) {
        e=&world_values_unknown_events[world_values_unknown_reserved++];e->reason=reason;e->exec=(uintptr_t)exec;e->owner=*owner;
        e->thread=GetCurrentThreadId();e->ui=e->thread==ui_thread;e->boot=boot_phase;e->tick=stats.issued;
    } else ++world_values_unknown_omitted;
    ReleaseSRWLockExclusive(&world_values_lock);
    if(e) {
        if(read_local(image+NPC_PROVENANCE_CURRENT,&e->current,8)) e->reads|=1;
        if(read_local((unsigned char*)exec+0x38,&e->raw_tag,4)) e->reads|=8;
        if(read_local((unsigned char*)exec+0x40,&e->raw_value,8)) e->reads|=16;
        e->owned=e->current==(uintptr_t)exec&&npc_provenance_current_matches(e->current);
        world_action_guard again={0};unsigned why=0;int status=world_action_owner_ex(exec,&again,&why);
        if((reason==7?status==1:status==0&&why==reason)&&again.tag==owner->tag&&again.context==owner->context&&again.lifetime==owner->lifetime&&
           again.entity==owner->entity&&again.runtime==owner->runtime&&again.saved==owner->saved) e->reads|=2;
        if(world_action_phase(&e->phase,world_queue_tls())) e->reads|=4;
        world_unknown_identity_capture(e,node,exec,frame);
    }
    AcquireSRWLockExclusive(&world_values_lock);
    if(e) {if(world_values_closing) e->errors|=WV_STOP;
        if(world_values_closing || !stats.active) {e->identity.error|=WUI_STOP;e->identity.stopped=1;}
        if(e->errors) ++world_values_failures;InterlockedExchange(&e->complete,1);}
    if(world_values_open<=0) ++world_values_failures;else --world_values_open;
    ReleaseSRWLockExclusive(&world_values_lock);
}
