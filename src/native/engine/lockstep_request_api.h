/* Private diagnostic export. Identity pointers are not exported; complete raw
 * banks still contain native runtime references and padding, explicitly labeled. */
static int request_hex(char *out,size_t capacity,int n,const char *name,const unsigned char *data,unsigned size) {
    if(n<0 || (size_t)n>=capacity) return -1;
    int used=snprintf(out+n,capacity-(size_t)n," %s=",name);
    if(used<0 || (size_t)used>=capacity-(size_t)n ||
       (size_t)size>(capacity-(size_t)n-(size_t)used-1)/2) return -1;
    n+=used;static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<size;i++) {out[n++]=hex[data[i]>>4];out[n++]=hex[data[i]&15];}
    out[n]=0;
    return n;
}
static int lua_player_request_enable(lua_State *L) {p_lua_pushnumber(L,request_install());return 1;}
static int lua_player_request_event(lua_State *L) {
    double requested=p_lua_tonumber(L,1);char out[4096];int n;
    if(GetCurrentThreadId()!=ui_thread || !request_enabled_tick || !isfinite(requested) || requested<0 ||
       requested>REQUEST_CAPACITY || floor(requested)!=requested) {p_lua_pushnil(L);return 1;}
    if(!requested) {
        AcquireSRWLockExclusive(&request_admission_lock);
        LONG open=request_open,peak=request_peak,restore_open=request_restore_open;
        ReleaseSRWLockExclusive(&request_admission_lock);
        n=snprintf(out,sizeof(out),"schema=2 count=%ld scopes=%ld failures=%ld excluded=%ld open=%ld peak=%ld restore_open=%ld restored=%d active=%d early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1",
            request_count,request_scope_count,request_failures,request_excluded,open,peak,restore_open,request_restored,request_installed);
    } else {
        unsigned index=(unsigned)requested;request_event *e=&request_events[index-1];
        if(index>(unsigned)request_count || !InterlockedCompareExchange(&e->ready,1,1)) {p_lua_pushnil(L);return 1;}
        n=snprintf(out,sizeof(out),"index=%u tick=%llu actor=%u owner=%016llx agent_ordinal=%llu ordinal=%llu role=%u group=%u axis=%u caller=%08llx value=%016llx epsilon=%016llx valid=%u",
            index,(unsigned long long)e->tick,e->actor,(unsigned long long)e->owner,(unsigned long long)e->agent_ordinal,
            (unsigned long long)e->ordinal,e->role,e->group,e->axis,(unsigned long long)e->caller,
            (unsigned long long)e->value,(unsigned long long)e->epsilon,e->valid);
        const unsigned char *blocks[]={e->targets[0],e->targets[1],e->dirty[0],e->dirty[1],e->gains,e->solver_state,e->model_state,e->params,e->configuration};
        const unsigned sizes[]={48,48,2,2,48,20,16,0x140,16};
        const char *names[]={"before","after","dirty0","dirty1","gains","solver","model","params","configuration"};
        for(unsigned i=0;i<9;i++) {n=request_hex(out,sizeof(out),n,names[i],blocks[i],sizes[i]);if(n<0) {p_lua_pushnil(L);return 1;}}
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_request_scope(lua_State *L) {
    double requested=p_lua_tonumber(L,1),phase=p_lua_tonumber(L,2);char out[16384];int n;
    if(GetCurrentThreadId()!=ui_thread || !request_enabled_tick || !isfinite(requested) || requested<1 ||
       requested>REQUEST_SCOPE_CAPACITY || floor(requested)!=requested || (phase!=0 && phase!=1)) {p_lua_pushnil(L);return 1;}
    unsigned index=(unsigned)requested;request_scope_event *e=&request_scopes[index-1];
    if(index>(unsigned)request_scope_count || !InterlockedCompareExchange(&e->ready,1,1)) {p_lua_pushnil(L);return 1;}
    request_snapshot *s=&e->state[(unsigned)phase];
    n=snprintf(out,sizeof(out),"index=%u phase=%u tick=%llu actor=%u owner=%016llx ordinal=%llu role=%u valid=%u requests=%u other_requests=%u other_first_caller=%08x model_same=%u solver_same=%u read=%u write=%u model_present=%u model_rva=%08x solver_present=%u main=%u main_capacity=%u pending=%u pending_capacity=%u unknown_refs=%u resource_refs=%u",
        index,(unsigned)phase,(unsigned long long)e->tick,e->actor,(unsigned long long)e->owner,(unsigned long long)e->ordinal,
        e->role,e->valid,e->requests,e->other_requests,e->other_first_caller,e->model_same,e->solver_same,s->read_bank,s->write_bank,s->model_present,s->model_rva,s->solver_present,
        s->queue_count[0],s->queue_capacity[0],s->queue_count[1],s->queue_capacity[1],s->unknown_refs,s->resource_refs);
    const unsigned char *blocks[]={s->banks,s->params,s->model_state,s->queue[0],s->queue[1]};
    const unsigned sizes[]={0x340,0x140,16,s->queue_count[0]*0x90,s->queue_count[1]*0x90};
    const char *names[]={"banks","params","model","main_data","pending_data"};
    for(unsigned i=0;i<5;i++) {n=request_hex(out,sizeof(out),n,names[i],blocks[i],sizes[i]);if(n<0) {p_lua_pushnil(L);return 1;}}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
