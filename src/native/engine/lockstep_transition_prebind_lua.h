static int lua_player_transition_prebind_config(lua_State *L) {
    size_t length=0;const char *run=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,length==64 && run && !memchr(run,0,length) && transition_prebind_config(run));return 1;
}
static int lua_player_transition_prebind_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !transition_prebind_enabled) {p_lua_pushnil(L);return 1;}
    char out[768];AcquireSRWLockShared(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    int n=snprintf(out,sizeof(out),
        "schema=1 capacity=3 count=%u rows=%u source=%u returns=%u pending=%u finalized=%u poison=%u open=%llu restore_open=%llu closing=%u run=%s image=%llx atomic_lease=0 allocation_generation_known=0 queue_exception_cleanup=0 action_tick=%llu action_clock=%llu action_thread=%u action_ticket=%llu action_dispatch=%llu",
        p->count,p->exported,p->reserved,p->returned,p->pending,p->finalized,p->poison,(unsigned long long)transition_open,
        (unsigned long long)transition_restore_open,transition_closing,p->run,(unsigned long long)(uintptr_t)image,
        (unsigned long long)p->action_tick,(unsigned long long)p->action_clock,p->action_thread,
        (unsigned long long)p->action_ticket,(unsigned long long)p->action_dispatch);
    ReleaseSRWLockShared(&transition_lock);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_transition_prebind_row(lua_State *L) {
    double number=p_lua_tonumber(L,1);transition_prebind_row row;unsigned index=0;
    if(GetCurrentThreadId()!=ui_thread || !transition_prebind_enabled || !isfinite(number) || floor(number)!=number || number<1 || number>3) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockExclusive(&transition_lock);
    if(number==transition_prebind.exported+1 && number<=transition_prebind.count) {index=(unsigned)number;row=transition_prebind.rows[index-1];}
    ReleaseSRWLockExclusive(&transition_lock);if(!index) {p_lua_pushnil(L);return 1;}
    char out[1536],bytes[33];static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++) {bytes[i*2]=hex[row.bytes[i]>>4];bytes[i*2+1]=hex[row.bytes[i]&15];}bytes[32]=0;
    int n=snprintf(out,sizeof(out),
        "PLAYER_TRANSITION_PREBIND schema=1 ordinal=%u phase=%u thread=%u boot=%u tick=%llu clock=%llu event=%llu ticket=%llu parent=%llu caller=%x command=%llx bytes=%s registry=%llx page=%llx block=%llx index=%u lane=%u generation=%u status=%u",
        index,row.phase,row.thread,row.boot,(unsigned long long)row.tick,(unsigned long long)row.clock,
        (unsigned long long)row.event,(unsigned long long)row.ticket,(unsigned long long)row.parent,row.caller,
        (unsigned long long)row.command,bytes,(unsigned long long)row.registry.registry,(unsigned long long)row.registry.page,
        (unsigned long long)row.registry.block,row.registry.index,row.registry.lane,row.registry.generation,row.registry.status);
    if(n>0 && (size_t)n<sizeof(out)) {int added=transition_state_format(&row.state,0,out+n,sizeof(out)-(size_t)n);n=added<0?-1:n+added;}
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);
    else {AcquireSRWLockExclusive(&transition_lock);++transition_prebind.exported;ReleaseSRWLockExclusive(&transition_lock);p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
