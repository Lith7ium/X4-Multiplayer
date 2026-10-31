static int transition_diag_format(const transition_diag_row *r,unsigned ordinal,char *out,size_t capacity) {
    static const char hex[]="0123456789abcdef";char first[33],second[33];
    for(unsigned i=0;i<16;i++) {first[2*i]=hex[r->first[i]>>4];first[2*i+1]=hex[r->first[i]&15];second[2*i]=hex[r->second[i]>>4];second[2*i+1]=hex[r->second[i]&15];}
    first[32]=second[32]=0;
    int n=snprintf(out,capacity,
        "PLAYER_TRANSITION_QUEUE_DIAG schema=1 ordinal=%u kind=%u tick=%llu boot=%u thread=%u bound=%u selection=%u watch=%u imports=%u id_generation=%u"
        " runtime=%016llx saved=%016llx command=%llx agent=%llx object=%llx imported_object=%llx bound_agent=%llx manager=%llx"
        " recipient_reads=%u manager_reads=%u manager_match=%u caller=%llx reads=%u decode=%u type=%llx first=%s second=%s"
        " parent=%llu parent_kind=%u order=%llu candidates=%u source=%llu byte_match=%u",
        ordinal,r->kind,(unsigned long long)r->tick,r->boot,r->thread,r->bound,r->selection,r->watch,r->imports,r->id_generation,
        (unsigned long long)r->runtime,(unsigned long long)r->saved,(unsigned long long)r->command,(unsigned long long)r->agent,
        (unsigned long long)r->object,(unsigned long long)r->imported_object,(unsigned long long)r->bound_agent,(unsigned long long)r->manager,
        r->recipient_reads,r->manager_reads,r->manager_match,(unsigned long long)r->caller,r->reads,r->decode,(unsigned long long)r->type,first,second,
        (unsigned long long)r->parent,r->parent_kind,(unsigned long long)r->order,r->candidates,(unsigned long long)r->source,r->byte_match);
    return n<0 || (size_t)n>=capacity?-1:n;
}
static int lua_player_transition_queue_evidence_config(lua_State *L) {
    size_t n=0;const char *run=p_lua_tolstring(L,1,&n);
    p_lua_pushnumber(L,n==64 && run && strlen(run)==n && transition_diag_config(run));return 1;
}
static int lua_player_transition_queue_evidence_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !transition_diag_enabled || !transition_started) {p_lua_pushnil(L);return 1;}
    char out[768];AcquireSRWLockShared(&transition_lock);
    int n=snprintf(out,sizeof(out),
        "schema=1 capacity=%u count=%u rows=%u calls=%llu unknown=%llu unreadable=%llu ambiguous=%llu dropped=%llu open=%llu restore_open=%llu closing=%u"
        " run=%s image=%llx allocation_generation_known=0 ticket_authority=0 atomic_lease=0",
        TRANSITION_DIAG_MAX,transition_diag_count,transition_diag_exported,(unsigned long long)transition_diag_calls,
        (unsigned long long)transition_diag_unknown,(unsigned long long)transition_diag_unreadable,(unsigned long long)transition_diag_ambiguous,
        (unsigned long long)transition_diag_dropped,(unsigned long long)transition_open,(unsigned long long)transition_restore_open,
        transition_closing,transition_diag_run,(unsigned long long)(uintptr_t)image);
    ReleaseSRWLockShared(&transition_lock);
    if(n<0 || (size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_transition_queue_evidence_row(lua_State *L) {
    double number=p_lua_tonumber(L,1);transition_diag_row row;
    if(GetCurrentThreadId()!=ui_thread || !transition_diag_enabled || !transition_started || !isfinite(number) ||
       floor(number)!=number || number<1 || number>TRANSITION_DIAG_MAX) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&transition_lock);
    int ok=number==transition_diag_exported+1 && number<=transition_diag_count;
    if(ok) row=transition_diag_rows[(unsigned)number-1];ReleaseSRWLockShared(&transition_lock);
    if(!ok) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=transition_diag_format(&row,(unsigned)number,out,sizeof(out));
    if(n<0) p_lua_pushnil(L);else {++transition_diag_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
