/* Explicit opt-in only; these methods never initiate a player action. */
static int origin_ticket_format(const porigin_ticket *t,unsigned index,char *out,size_t capacity) {
    int n=snprintf(out,capacity,
        "PLAYER_ORIGIN_TICKET schema=%u ticket=%u event=%llx due=%016llx post_tick=%llu delivery_tick=%llu post_clock=%016llx delivery_clock=%016llx after_clock=%016llx controller0=%llx controller1=%llx post_returned=%u delivery_state=%u errors=%u post_thread=%u delivery_thread=%u post_rax=%016llx delivery_rax=%016llx",
        origin_ledger.request?3:2,index,(unsigned long long)t->event,(unsigned long long)t->due,(unsigned long long)t->post_tick,
        (unsigned long long)t->delivery_tick,(unsigned long long)t->post_clock,(unsigned long long)t->delivery_clock,
        (unsigned long long)t->after_clock,(unsigned long long)t->controller_before,(unsigned long long)t->controller_after,
        t->posted_returned,t->delivery_state,t->errors,t->thread,t->delivery_thread,
        (unsigned long long)t->post_rax,(unsigned long long)t->delivery_rax);
    if(n>0 && (size_t)n<capacity && origin_ledger.request) n+=snprintf(out+n,capacity-(size_t)n,
        " request=%llu post_enter=%llu post_leave=%llu delivery_enter=%llu delivery_leave=%llu after_tick=%llu transition_before=%llu transition_after=%llu",
        (unsigned long long)t->request,(unsigned long long)t->post_enter,(unsigned long long)t->post_leave,
        (unsigned long long)t->delivery_enter,(unsigned long long)t->delivery_leave,(unsigned long long)t->after_tick,
        (unsigned long long)t->transition_before,(unsigned long long)t->transition_after);
    return n<0 || (size_t)n>=capacity?-1:n;
}
static int lua_player_origin_config(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);p_lua_pushnumber(L,origin_configure(text,length));return 1;
}
static int lua_player_origin_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !origin_started) {p_lua_pushnil(L);return 1;}
    char out[1536];AcquireSRWLockShared(&origin_lock);AcquireSRWLockShared(&origin_ledger.lock);
    porigin_ledger *l=&origin_ledger;porigin_binding *b=&l->binding;
    int native_complete=origin_bound && origin_imports==1 && origin_closing && !origin_sites && !origin_open &&
        !origin_restore_open && !origin_failures && !origin_unwinds && origin_calls==origin_returns;
    int n=snprintf(out,sizeof(out),
        "schema=%u first=%u last=%u imports=%u bound=%ld sites=%u closing=%u calls=%llu returns=%llu open=%llu peak=%llu restore_open=%llu prebind=%llu native_failures=%llu tickets=%u edges=%u post_calls=%llu post_returns=%llu delivery_calls=%llu delivery_returns=%llu core_open=%llu core_peak=%llu core_stop_open=%llu failures=%llu other=%llu unknown=%llu excluded=%llu ticket_rows=%u edge_rows=%u player_saved=%016llx ship_saved=%016llx player_runtime=%016llx ship_runtime=%016llx player=%llx ship=%llx agent=%llx owner=%llx raw_world_coverage=0 action_authority=0 atomic_lease=0 early_coverage=0 raw_pointers=1 due_is_current=0 native_unwinds=%llu post_unwinds=%llu delivery_unwinds=%llu",
        origin_ledger.request?3:2,origin_first,origin_last,origin_imports,(long)origin_bound,origin_sites,origin_closing,
        (unsigned long long)origin_calls,(unsigned long long)origin_returns,(unsigned long long)origin_open,(unsigned long long)origin_peak,
        (unsigned long long)origin_restore_open,(unsigned long long)origin_prebind,(unsigned long long)origin_failures,l->count,l->link_count,
        (unsigned long long)l->post_calls,(unsigned long long)l->post_returns,(unsigned long long)l->delivery_calls,(unsigned long long)l->delivery_returns,
        (unsigned long long)l->open,(unsigned long long)l->peak,(unsigned long long)l->stop_open,(unsigned long long)l->failures,
        (unsigned long long)l->other_receivers,(unsigned long long)l->unknown,(unsigned long long)l->excluded,origin_ticket_rows,origin_edge_rows,
        (unsigned long long)origin_player_saved,(unsigned long long)origin_ship_saved,(unsigned long long)b->player_runtime,
        (unsigned long long)b->ship_runtime,(unsigned long long)b->player,(unsigned long long)b->ship,(unsigned long long)b->agent,(unsigned long long)b->ship_owner,
        (unsigned long long)origin_unwinds,(unsigned long long)l->post_unwinds,(unsigned long long)l->delivery_unwinds);
    ReleaseSRWLockShared(&origin_ledger.lock);ReleaseSRWLockShared(&origin_lock);
    /* Completeness is diagnostic only; consumers must independently validate
     * every exported record and native restoration/transition receipt. */
    int complete=native_complete && porigin_complete(&origin_ledger);
    if(n>0 && (size_t)n<sizeof(out)) n+=snprintf(out+n,sizeof(out)-(size_t)n," capture_complete=%d",complete);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int origin_export_ready(void) {
    return origin_started && origin_closing && !origin_open && origin_ledger.closing && !origin_ledger.open;
}
static int lua_player_origin_ticket(lua_State *L) {
    double number=p_lua_tonumber(L,1);porigin_ticket ticket;
    if(GetCurrentThreadId()!=ui_thread || !isfinite(number) || floor(number)!=number || number<1 || number>PORIGIN_MAX) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&origin_lock);AcquireSRWLockShared(&origin_ledger.lock);
    int ok=origin_export_ready() && number==origin_ticket_rows+1 && number<=origin_ledger.count;
    if(ok) ticket=origin_ledger.tickets[(unsigned)number-1];
    ReleaseSRWLockShared(&origin_ledger.lock);ReleaseSRWLockShared(&origin_lock);
    if(!ok) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=origin_ticket_format(&ticket,(unsigned)number,out,sizeof(out));
    if(n<0) p_lua_pushnil(L);else {++origin_ticket_rows;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
static int lua_player_origin_edge(lua_State *L) {
    double number=p_lua_tonumber(L,1);porigin_link edge;
    if(GetCurrentThreadId()!=ui_thread || !isfinite(number) || floor(number)!=number || number<1 || number>PORIGIN_LINK_MAX) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&origin_lock);AcquireSRWLockShared(&origin_ledger.lock);
    int ok=origin_export_ready() && number==origin_edge_rows+1 && number<=origin_ledger.link_count;
    if(ok) edge=origin_ledger.links[(unsigned)number-1];
    ReleaseSRWLockShared(&origin_ledger.lock);ReleaseSRWLockShared(&origin_lock);
    if(!ok) {p_lua_pushnil(L);return 1;}
    char out[192];int n=snprintf(out,sizeof(out),"PLAYER_ORIGIN_EDGE schema=%u edge=%u ticket=%u transition=%llu",origin_ledger.request?3:2,(unsigned)number,edge.ticket,(unsigned long long)edge.transition);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else {++origin_edge_rows;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
