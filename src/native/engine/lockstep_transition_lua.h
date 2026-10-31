#define TRANSITION_ROW_CAP 2048u
#include "lockstep_transition_queue_diag_lua.h"
static unsigned transition_ready(void) {
    unsigned count=(unsigned)(transition_count>TRANSITION_MAX?TRANSITION_MAX:transition_count),ready=0;
    while(ready<count && InterlockedCompareExchange(&transition_events[ready].complete,0,0)) ++ready;return ready;
}
static int transition_state_format(const transition_state *s,unsigned phase,char *out,size_t capacity) {
    int n=snprintf(out,capacity,
        " s%u=%016llx,%016llx,%llx,%llx,%llx,%llx,%llx,%llx,%x,%x,%u,%u,%s",
        phase,(unsigned long long)s->saved,(unsigned long long)s->runtime,(unsigned long long)s->object,
        (unsigned long long)s->controller,(unsigned long long)s->agent,(unsigned long long)s->slots,
        (unsigned long long)s->faction,(unsigned long long)s->model,s->controller_type,s->model_type,
        s->owns_agent,s->player,s->owner[0]?s->owner:"-");
    return n<0 || (size_t)n>=capacity?-1:n;
}
#include "lockstep_transition_prebind_lua.h"
static int transition_format(const transition_event *e,unsigned index,char *out,size_t capacity) {
    int n=snprintf(out,capacity,
        "PLAYER_TRANSITION event=%u kind=%u tick=%llu enter=%llu leave=%llu parent=%llu thread=%u boot=%u caller=%x errors=%u arg=%u extra=%u queue=%llu enqueue=%llu queue_errors=%u queue_valid=%u transfer=%llu publication=%llu old=%llx new=%llx result=%llx old_owns=%u active0=%u active1=%u",
        index,e->kind,(unsigned long long)e->tick,(unsigned long long)e->enter,(unsigned long long)e->leave,
        (unsigned long long)e->parent,e->thread,e->boot,e->caller,e->errors,e->arg,e->extra,
        (unsigned long long)e->queue,(unsigned long long)e->enqueue,e->queue_errors,e->queue_valid,
        (unsigned long long)e->transfer,(unsigned long long)e->publication,(unsigned long long)e->old_controller,
        (unsigned long long)e->new_controller,(unsigned long long)e->result,e->old_owns_after,e->active_before,e->active_after);
    if(n<0 || (size_t)n>=capacity) return -1;
    const transition_state *states[2]={&e->before,&e->after};
    for(unsigned i=0;i<2;i++) {int add=transition_state_format(states[i],i,out+n,capacity-(size_t)n);if(add<0) return -1;n+=add;}
    return n;
}
static int lua_player_transition_config(lua_State *L) {p_lua_pushnumber(L,transition_configure());return 1;}
static int lua_player_transition_watch_config(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);double last=p_lua_tonumber(L,2);
    int ok=isfinite(last) && last>=1 && last<=120 && floor(last)==last && transition_saved_config(text,length,(unsigned)last);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_transition_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !transition_started) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&transition_lock);LONG64 open=transition_open,peak=transition_peak,restore_open=transition_restore_open;
    unsigned sites=transition_sites,enabled=transition_enabled;ReleaseSRWLockShared(&transition_lock);
    char out[1536];int n=snprintf(out,sizeof(out),
        "schema=%u capacity=%u count=%llu complete=%u rows=%u failures=%llu excluded=%llu dropped=%llu open=%llu peak=%llu restore_open=%llu sites=%u enabled=%u atomic_lease=0 generation_exclusion=0 faction_exclusion=0 early_import_coverage=0 raw_pointers=1",
        transition_saved_count?2:1,TRANSITION_MAX,(unsigned long long)transition_count,transition_ready(),transition_rows,(unsigned long long)transition_failures,
        (unsigned long long)transition_excluded,(unsigned long long)transition_dropped,(unsigned long long)open,
        (unsigned long long)peak,(unsigned long long)restore_open,sites,enabled);
    if(transition_saved_count && n>0 && (size_t)n<sizeof(out)) n+=snprintf(out+n,sizeof(out)-(size_t)n,
        " watches=%u bound=%u watch_failures=%llu import_calls=%llu prebind=%llu last=%u queue_calls=%llu queue_returns=%llu queue_other=%llu queue_unknown=%llu queue_failures=%llu tickets=%u outstanding=%u raw_world_coverage=0 watch_rows=%u ticket_rows=%u",
        transition_saved_count,transition_saved_bound,(unsigned long long)transition_saved_failures,(unsigned long long)transition_saved_import_calls,
        (unsigned long long)transition_saved_prebind,transition_saved_last,(unsigned long long)transition_saved_queue_calls,
        (unsigned long long)transition_saved_queue_returns,(unsigned long long)transition_saved_queue_other,
        (unsigned long long)transition_saved_queue_unknown,(unsigned long long)transition_saved_queue_failures,
        transition_saved_tickets_count,transition_saved_outstanding(),transition_saved_watch_rows,transition_saved_ticket_rows);
    if(n>0 && (size_t)n<sizeof(out)) n+=snprintf(out+n,sizeof(out)-(size_t)n,
        " exception_schema=1 direct_calls=%llu direct_returns=%llu direct_unwinds=%llu queue_exception_cleanup=0",
        (unsigned long long)transition_direct_calls,(unsigned long long)transition_direct_returns,(unsigned long long)transition_direct_unwinds);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_transition_ticket(lua_State *L) {
    double number=p_lua_tonumber(L,1);transition_saved_ticket ticket;transition_saved_watch watch;
    if(GetCurrentThreadId()!=ui_thread || !transition_saved_count || !isfinite(number) || floor(number)!=number || number<1 || number>TRANSITION_QUEUE_MAX) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&transition_lock);
    int ok=transition_closing && !transition_open && (unsigned)number<=transition_saved_tickets_count && number==transition_saved_ticket_rows+1;
    if(ok) {ticket=transition_saved_tickets[(unsigned)number-1];ok=ticket.watch<transition_saved_count;if(ok) watch=transition_saved_watches[ticket.watch];}
    ReleaseSRWLockShared(&transition_lock);
    if(!ok) {p_lua_pushnil(L);return 1;}
    char out[768],before[33],after[33];static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++) {before[i*2]=digits[ticket.bytes[i]>>4];before[i*2+1]=digits[ticket.bytes[i]&15];after[i*2]=digits[ticket.after[i]>>4];after[i*2+1]=digits[ticket.after[i]&15];}
    before[32]=after[32]=0;
    int n=snprintf(out,sizeof(out),"PLAYER_TRANSITION_TICKET schema=2 ticket=%u state=%u saved=%016llx runtime=%016llx command=%llx agent=%llx enqueue=%llu dispatch=%llu mode=%u before=%s after=%s image=%llx raw_world_coverage=0",
        (unsigned)number,ticket.state,(unsigned long long)watch.saved,(unsigned long long)ticket.runtime,
        (unsigned long long)ticket.command,(unsigned long long)ticket.agent,(unsigned long long)ticket.event,
        (unsigned long long)ticket.dispatch,ticket.mode,before,after,(unsigned long long)(uintptr_t)image);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else {++transition_saved_ticket_rows;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
static int lua_player_transition_watch(lua_State *L) {
    double number=p_lua_tonumber(L,1);transition_saved_watch watch;
    if(GetCurrentThreadId()!=ui_thread || !isfinite(number) || floor(number)!=number || number<1 || number>TRANSITION_WATCH_MAX) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&transition_lock);
    int ok=number<=transition_saved_bound && number==transition_saved_watch_rows+1;
    if(ok) watch=transition_saved_watches[(unsigned)number-1];ReleaseSRWLockShared(&transition_lock);
    if(!ok) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=snprintf(out,sizeof(out),"PLAYER_TRANSITION_WATCH schema=2 index=%u type=%x macro=%s owner=%s generation=%u raw_world_coverage=0",
        (unsigned)number,watch.initial_type,watch.macro,watch.owner,(unsigned)((watch.runtime>>25)&0xffff));
    if(n>0 && (size_t)n<sizeof(out)) {int add=transition_state_format(&watch.initial,0,out+n,sizeof(out)-(size_t)n);n=add<0?-1:n+add;}
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else {++transition_saved_watch_rows;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
static int lua_player_transition_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !transition_started || !isfinite(index) || index<0 || index>TRANSITION_MAX || floor(index)!=index) {p_lua_pushnil(L);return 1;}
    unsigned ready=transition_ready();if(index==0) {p_lua_pushnumber(L,ready);return 1;}
    if(index>ready || index!=transition_rows+1) {p_lua_pushnil(L);return 1;}
    char out[TRANSITION_ROW_CAP];int n=transition_format(&transition_events[(unsigned)index-1],(unsigned)index,out,sizeof(out));
    if(n<0) {p_lua_pushnil(L);return 1;}++transition_rows;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
