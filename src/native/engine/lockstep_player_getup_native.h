/* No hooks or direct controller writes. Calls the pinned stock public GetUp. */
#include "lockstep_player_getup.h"
#include "lockstep_getup_diagnostic_wire.h"
static unsigned gd_enabled;
static pgetup_ledger getup_ledger;
typedef struct {
    uintptr_t image,thunk;uint64_t options,started;
    unsigned slot,port,timeout,local_mode;
    char digest[65],origin[80],watch[320];
} getup_session_binding;
static getup_session_binding getup_session;
static uint64_t getup_request_id(void) {return pgetup_request(&getup_ledger);}
/* Local diagnostic requires this observer, including current slot ownership.
 * Keep this additional contract out of ordinary paired modes. */
static int getup_diagnostic_spline_owned(void) {
    void *entry=NULL;
    return spline_installed && read_local(image+SPLINE_ENTRY,&entry,sizeof(entry)) && entry==(void*)spline_update;
}
static int getup_hooks_owned(void) {
    if(!origin_integrity() || !transition_enabled || transition_closing || transition_sites!=7) return 0;
    for(unsigned i=0;i<3;i++) {uint64_t word;if(!read_local(image+transition_entries[i],&word,8) || word!=transition_patched[i]) return 0;}
    return 1;
}
static int getup_configure_mode(const char *run,const char *contract) {
    unsigned local_mode=contract && !strcmp(contract,GD_CONTRACT);
    if((transition_diag_enabled || transition_prebind_enabled) && (!local_mode || !transition_diag_enabled)) return 0;
    if(contract && strcmp(contract,"agreed-getup-1") && !local_mode) return 0;
    if(local_mode && (stats.slot!=1 || boot_options!=GD_OPTIONS || !path_installed || !(path_options&1) ||
       !world_identity_installed || !ai_installed || !ai_full_layout_verified || !ship_installed ||
       !nav_installed || !agent_installed || !flight_installed || !getup_diagnostic_spline_owned() ||
       !world_trace_early || !world_trace_watch_count || !world_queue_enabled || world_queue_sites!=7)) return 0;
    if(!stats.active || !used || !boot_mode || boot_phase<0 || boot_phase>3 || GetCurrentThreadId()!=ui_thread ||
       !wire_pair() || portable_wire_enabled || !origin_started || origin_closing || origin_sites!=3 || !getup_hooks_owned() ||
       origin_first!=1 || origin_last!=120 || origin_player_saved!=0x11e12 || origin_ship_saved!=0xae79c ||
       transition_saved_count!=1 || strcmp(transition_saved_watches[0].macro,"ship_arg_s_scout_01_a_macro") ||
       !pgetup_run(run) || !hash_ok(secret,32,run) || origin_failed() || transition_failures || transition_saved_failures) return 0;
    getup_session_binding current;memset(&current,0,sizeof(current));current.image=(uintptr_t)image;current.thunk=(uintptr_t)thunk;
    current.options=boot_options;current.started=boot_started;current.slot=stats.slot;current.port=remote_port;current.timeout=timeout_ms;current.local_mode=local_mode;
    memcpy(current.digest,initial_state_digest,sizeof(current.digest));memcpy(current.origin,origin_token,sizeof(current.origin));
    memcpy(current.watch,transition_saved_manifest,sizeof(current.watch));
    int ok=0;AcquireSRWLockExclusive(&getup_ledger.lock);pgetup_ledger *l=&getup_ledger;
    if(l->closing || !stats.active || l->failures || l->open || l->unwinds) goto done;
    if(l->configured) {
        /* A new Lua state may reattach during Load. Nothing is reset, including
         * the first publication, processed inputs and the already used claim. */
        ok=l->epoch==1 && gd_enabled==local_mode && !strcmp(l->run,run) && !memcmp(&current,&getup_session,sizeof(current)) &&
            l->calls==l->returns && l->calls==l->claimed && l->accepted==l->claimed &&
            l->claimed<=1 && l->processed<=120 && stats.issued>=l->processed && stats.issued<=l->processed+1 &&
            stats.report_tick<=l->processed && (boot_phase==3 || (!stats.issued && !l->processed && !l->claimed));
        AcquireSRWLockShared(&origin_ledger.lock);ok=ok && origin_ledger.request==getup_request_id;ReleaseSRWLockShared(&origin_ledger.lock);
        goto done;
    }
    if(boot_phase || stats.issued || l->processed || gate_tick_hz!=GATE_TICK_HZ_DEFAULT) goto done; /* its evidence is fixed at 1/60 s */
    memcpy(l->run,run,65);l->epoch=1;l->configured=1;memcpy(&getup_session,&current,sizeof(current));gd_enabled=local_mode;
    AcquireSRWLockExclusive(&origin_ledger.lock);origin_ledger.request=getup_request_id;ReleaseSRWLockExclusive(&origin_ledger.lock);ok=1;
done:ReleaseSRWLockExclusive(&getup_ledger.lock);
    return ok;
}
static inline int getup_configure(const char *run) {return getup_configure_mode(run,NULL);}
/* Load can acknowledge only a complete already configured extension chain.
 * The existing healthy reattachment checks preserve all counters and receipts. */
static int getup_extensions_load_ready(void) {
    if(!transition_diag_enabled && !transition_prebind_enabled) return 1;
    if(!transition_diag_enabled || !gd_enabled) return 0;
    char run[65];AcquireSRWLockShared(&getup_ledger.lock);
    int ok=getup_ledger.configured && !getup_ledger.closing && !getup_ledger.failures && getup_session.local_mode;
    memcpy(run,getup_ledger.run,sizeof(run));ReleaseSRWLockShared(&getup_ledger.lock);
    if(!ok || !pgetup_run(run)) return 0;
    return (!transition_prebind_enabled || transition_prebind_config(run)) &&
        transition_diag_config(run) && getup_configure_mode(run,GD_CONTRACT);
}
static int getup_reject_view(void) {
    AcquireSRWLockExclusive(&getup_ledger.lock);if(getup_ledger.configured) ++getup_ledger.failures;
    ReleaseSRWLockExclusive(&getup_ledger.lock);return 0;
}
static int getup_retain_observation(unsigned phase,uint64_t publication,uintptr_t controller) {
    AcquireSRWLockExclusive(&getup_ledger.lock);pgetup_ledger *l=&getup_ledger;
    int ok=l->configured && !l->closing && !l->failures && phase>=l->phase &&
        (!l->publication || (publication==l->publication && controller==l->published_controller));
    if(ok) {l->phase=phase;if(publication) {l->publication=publication;l->published_controller=controller;}}
    else if(l->configured) ++l->failures;
    ReleaseSRWLockExclusive(&getup_ledger.lock);return ok;
}
static int getup_retain_publication(uint64_t publication,uintptr_t controller) {
    AcquireSRWLockExclusive(&getup_ledger.lock);pgetup_ledger *l=&getup_ledger;
    int ok=l->configured && !l->closing && !l->failures && publication && controller &&
        (!l->publication || (l->publication==publication && l->published_controller==controller));
    if(ok) {l->publication=publication;l->published_controller=controller;}
    else if(l->configured) ++l->failures;
    ReleaseSRWLockExclusive(&l->lock);return ok;
}
/* A raw publication proves only that selected controller change was observed.
 * It does not prove that the deferred flight-model change completed. */
static int getup_view(porigin_sample *sample,transition_state *state,unsigned *phase) {
    transition_saved_watch watch;
    if(!transition_saved_copy(0,&watch) || !origin_identity(sample) || !porigin_same(&sample->binding,&origin_ledger.binding) ||
       !transition_read((void*)sample->binding.ship,NULL,state) || state->saved!=0xae79c ||
       !state->owns_agent || state->agent!=sample->binding.agent || strcmp(state->owner,"player")) return getup_reject_view();
    unsigned claimed,accepted;AcquireSRWLockShared(&getup_ledger.lock);
    claimed=getup_ledger.claimed;accepted=getup_ledger.accepted;ReleaseSRWLockShared(&getup_ledger.lock);
    *phase=accepted?2:claimed?1:0;
    porigin_ticket ticket={0};porigin_link links[PORIGIN_LINK_MAX];unsigned count=0;
    AcquireSRWLockShared(&origin_ledger.lock);
    int valid_count=origin_ledger.count<=1 && origin_ledger.link_count<=PORIGIN_LINK_MAX && (!accepted || origin_ledger.count==1) &&
        (origin_ledger.count==0 || (claimed && origin_ledger.tickets[0].request==1));
    if(valid_count && origin_ledger.count==1 && origin_ledger.tickets[0].request==1) {
        ticket=origin_ledger.tickets[0];count=origin_ledger.link_count;memcpy(links,origin_ledger.links,count*sizeof(*links));
    }
    ReleaseSRWLockShared(&origin_ledger.lock);
    if(!valid_count) return getup_reject_view();
    if(ticket.errors) return getup_reject_view();if(ticket.delivery_state==2) *phase=3;
    uint64_t publication=0;uintptr_t published=0;
    for(unsigned i=0;i<count;i++) {
        uint64_t at=links[i].transition;
        if(links[i].ticket!=1 || !at || at>TRANSITION_MAX) return getup_reject_view();
        transition_event *e=&transition_events[at-1];
        if(!InterlockedCompareExchange(&e->complete,0,0)) continue;
        if(e->errors || e->queue_errors) return getup_reject_view();
        if(e->kind==TR_PUBLICATION && e->before.saved==state->saved && e->after.saved==state->saved &&
           e->before.runtime==state->runtime && e->after.runtime==state->runtime &&
           e->before.object==state->object && e->after.object==state->object &&
           e->before.agent==state->agent && e->after.agent==state->agent &&
           e->before.faction==state->faction && e->after.faction==state->faction &&
           e->before.controller_type==0x2c39488 && e->before.controller==watch.initial_controller && e->old_controller==watch.initial_controller &&
           e->after.controller_type==0x2c51158 && e->after.controller && e->new_controller==e->after.controller && e->after.owns_agent && !e->after.player &&
           e->thread==ticket.delivery_thread && e->tick>=ticket.delivery_tick && e->tick<=ticket.after_tick &&
           ticket.transition_before<e->enter && e->leave<=ticket.transition_after) {
            if(publication) return getup_reject_view();publication=at;published=e->after.controller;
        } else if(e->kind==TR_PUBLICATION) return getup_reject_view();
    }
    /* Discover publication from immutable receipts before checking current
     * controller: a replacement before the first UI view must not hide it. */
    if(publication && !getup_retain_publication(publication,published)) return 0;
    LONG64 total=InterlockedCompareExchange64(&transition_count,0,0);if(total<0 || total>TRANSITION_MAX) return getup_reject_view();
    for(LONG64 i=0;i<total;i++) {
        transition_event *e=&transition_events[i];
        if(InterlockedCompareExchange(&e->complete,0,0) && e->kind==TR_PUBLICATION && (uint64_t)i+1!=publication) return getup_reject_view();
    }
    if(!publication) {
        if(state->controller_type!=0x2c39488 || state->controller!=watch.initial_controller || state->player!=1) return getup_reject_view();
        return getup_retain_observation(*phase,0,0);
    }
    if(state->controller_type!=0x2c51158 || state->controller!=published || state->player || ticket.delivery_state!=2) return getup_reject_view();
    *phase=4;
    for(LONG64 i=0;i<total;i++) {
        transition_event *e=&transition_events[i];
        if(!InterlockedCompareExchange(&e->complete,0,0) || e->kind!=TR_MODEL_DISPATCH || e->publication!=publication) continue;
        if(e->errors || e->queue_errors || !e->queue_valid || !e->queue || !e->enqueue ||
           e->after.saved!=state->saved || e->after.runtime!=state->runtime || e->after.object!=state->object ||
           e->after.agent!=state->agent || e->after.faction!=state->faction || e->after.controller!=state->controller) return getup_reject_view();
        *phase=5;
    }
    return getup_retain_observation(*phase,publication,published);
}
static int getup_guard(void *unused,uint64_t tick,int action) {
    (void)unused;porigin_sample sample;transition_state state;unsigned phase;
    return stats.active && boot_mode && boot_phase==3 && GetCurrentThreadId()==ui_thread &&
        tick==stats.issued && stats.report_tick+1==tick && pgetup_words(tick,inputs[0],inputs[1]) &&
        !origin_failed() && origin_integrity() && transition_enabled && transition_sites==7 &&
        !transition_failures && !transition_saved_failures && getup_view(&sample,&state,&phase) &&
        (!action || (phase==0 && state.controller_type==0x2c39488 && state.player==1 && transition_prebind_action_check(tick,1))) && (tick<120 || phase==5);
}
static int getup_failed(void) {
    AcquireSRWLockShared(&getup_ledger.lock);int bad=getup_ledger.configured && getup_ledger.failures;
    ReleaseSRWLockShared(&getup_ledger.lock);return bad;
}
