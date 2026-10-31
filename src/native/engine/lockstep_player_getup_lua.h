static int lua_player_getup_config(lua_State *L) {
    size_t n=0,cn=0;const char *run=p_lua_tolstring(L,1,&n),*contract=p_lua_tolstring(L,2,&cn);
    p_lua_pushnumber(L,n==64 && (!contract || (cn && cn<24 && strlen(contract)==cn)) && getup_configure_mode(run,contract));return 1;
}
static int lua_player_getup_batch(lua_State *L) {
    double tick=p_lua_tonumber(L,1),epoch=p_lua_tonumber(L,3);size_t rn=0,an=0,bn=0;
    const char *run=p_lua_tolstring(L,2,&rn),*a=p_lua_tolstring(L,4,&an),*b=p_lua_tolstring(L,5,&bn);
    int ok=0;
    if(rn==64 && an==8 && bn==8 && epoch==1 && isfinite(tick) && floor(tick)==tick && tick>=1 && tick<=120)
        ok=pgetup_batch(&getup_ledger,run,1,(uint64_t)tick,a,b,getup_guard,NULL,(pgetup_original)(image+0x1aba30));
    else {AcquireSRWLockExclusive(&getup_ledger.lock);++getup_ledger.failures;ReleaseSRWLockExclusive(&getup_ledger.lock);}
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_getup_state(lua_State *L) {
    porigin_sample sample;transition_state state;unsigned phase;double tick=p_lua_tonumber(L,1);
    if(!getup_ledger.configured || !stats.active || GetCurrentThreadId()!=ui_thread || tick!=stats.issued ||
       getup_ledger.processed!=stats.issued || !getup_view(&sample,&state,&phase)) {p_lua_pushnil(L);return 1;}
    char out[256];int n=snprintf(out,sizeof(out),
        "%s:%s:1:%llu:%u:%u:%u:0000000000011e12:00000000000ae79c:%x:%x:%u:%u",
        gd_enabled?"PLAYER_GETUP_LOCAL_1":"PLAYER_GETUP_STATE_1",getup_ledger.run,(unsigned long long)getup_ledger.processed,getup_ledger.claimed,getup_ledger.accepted,
        phase,state.controller_type,state.model_type,state.player,state.owns_agent);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_getup_status(lua_State *L) {
    if(!getup_ledger.configured || GetCurrentThreadId()!=ui_thread) {p_lua_pushnil(L);return 1;}
    char out[768];AcquireSRWLockShared(&getup_ledger.lock);pgetup_ledger *l=&getup_ledger;
    int n=snprintf(out,sizeof(out),
        "schema=%u%s run=%s epoch=1 tick=30 last=120 processed=%llu claimed=%u accepted=%u calls=%llu returns=%llu unwinds=%llu open=%llu stop_open=%llu failures=%llu closing=%u player=0000000000011e12 ship=00000000000ae79c command1=87010000 command2=00000000 completed_release=0 independent_player=0 atomic_lease=0 queue_exception_cleanup=0 boundary=admitted_ui",
        gd_enabled?2:1,gd_enabled?" contract=" GD_CONTRACT " native_clients=1 parity_checked=0":"",l->run,(unsigned long long)l->processed,l->claimed,l->accepted,(unsigned long long)l->calls,
        (unsigned long long)l->returns,(unsigned long long)l->unwinds,(unsigned long long)l->open,
        (unsigned long long)l->stop_open,(unsigned long long)l->failures,l->closing);
    ReleaseSRWLockShared(&l->lock);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_getup_diagnostic_status(lua_State *L) {
    if(!gd_enabled || !getup_ledger.configured || GetCurrentThreadId()!=ui_thread) {p_lua_pushnil(L);return 1;}
    char out[640];int n=snprintf(out,sizeof(out),
        "schema=1 contract=" GD_CONTRACT " run=%s epoch=1 native_clients=1 physical_slot=1 options=%llu tick=%llu issued=%llu calls=%llu returns=%llu unwinds=%llu failures=%llu terminal_started=%u terminal_finished=%u closed=%u restored=%u reason=%u stage=%s session_pass=0 parity_checked=0 independent_native_players=0 completed_release=0",
        getup_ledger.run,(unsigned long long)getup_session.options,(unsigned long long)stats.completed,(unsigned long long)stats.issued,(unsigned long long)gd_calls,
        (unsigned long long)gd_returns,(unsigned long long)gd_unwinds,(unsigned long long)gd_failures,gd_terminal_started,
        gd_terminal_finished,gd_closed,gd_terminal_restored,stats.reason,gd_stage);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
