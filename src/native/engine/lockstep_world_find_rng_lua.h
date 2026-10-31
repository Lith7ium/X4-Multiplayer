static unsigned world_find_ready(void) {unsigned n=0,limit=world_find_reserved>WORLD_FIND_CAP?WORLD_FIND_CAP:world_find_reserved;while(n<limit&&InterlockedCompareExchange(&world_find_events[n].complete,0,0)) ++n;return n;}
static int world_find_status(char *out,size_t size) {
    char digest[65];hex32(world_find_digest,digest);AcquireSRWLockShared(&world_find_lock);
    int n=snprintf(out,size,"schema=%u capacity=%u reserved=%u complete=%u rows=%u calls=%llu open=%llu restore_open=%llu failures=%llu dropped=%llu closed=%u used=%u anchor_calls=%llu anchor_ready=%u anchor_pending=%u anchor_errors=%u seed=%016llx digest=%s saved_pilot=000000000009d3a4 saved_order=0000000000003cdb saved_script_decimal=38361 scope_exception_cleanup=1 enclosing_exception_cleanup=0",
        world_find_composition,WORLD_FIND_CAP,world_find_reserved,world_find_ready(),world_find_exported,(unsigned long long)world_find_calls,(unsigned long long)world_find_open,
        (unsigned long long)world_find_restore_open,(unsigned long long)world_find_failures,(unsigned long long)world_find_dropped,world_find_closed!=0,world_find_used,
        (unsigned long long)world_find_anchor_calls,world_find_anchor_ready,world_find_anchor_pending,world_find_anchor_errors,(unsigned long long)world_find_seed,digest);
    if(world_find_composition==2&&n>=0&&(size_t)n<size) {int extra=snprintf(out+n,size-(size_t)n," composition=%s",WORLD_FIND_THIRD_CONTRACT);n=extra<0?-1:n+extra;}
    ReleaseSRWLockShared(&world_find_lock);return n<0||(size_t)n>=size?-1:n;
}
static int world_find_format(const world_find_event *e,unsigned number,unsigned part,char *out,size_t size) {
    int n=-1;
    if(!part) n=snprintf(out,size,"WORLD_FIND_EVENT event=%u part=0 errors=%u applied=%u restored=%u abnormal=%u thread=%u action=%u candidate=%u ordinal=%u caller_before=%016llx seed=%016llx scoped_before=%016llx scoped_after=%016llx caller_after=%016llx result=%016llx assigned=%016llx",
        number,e->errors,e->applied,e->restored,e->abnormal,e->thread,e->action,e->candidate,e->ordinal,(unsigned long long)e->caller_before,
        (unsigned long long)e->seed,(unsigned long long)e->scoped_before,(unsigned long long)e->scoped_after,(unsigned long long)e->caller_after,
        (unsigned long long)e->result,(unsigned long long)e->assigned);
    else if(part<=3) {
        const world_find_identity *i=part==1?&world_find_anchor:part==2?&e->before:&e->after;
        n=snprintf(out,size,"WORLD_FIND_IDENTITY event=%u part=%u context=%llx lifetime=%llx entity=%llx runtime=%llx saved=%016llx script=%llx version=%u exec=%llx program=%llx order=%llx order_id=%llx state=%u backlink=%llx registry=%llx group=%llx flags=%x generation=%x",
            number,part,(unsigned long long)i->context,(unsigned long long)i->context_id,(unsigned long long)i->entity,(unsigned long long)i->runtime,
            (unsigned long long)i->saved,(unsigned long long)i->script,i->version,(unsigned long long)i->exec,(unsigned long long)i->program,
            (unsigned long long)i->order,(unsigned long long)i->order_id,i->state,(unsigned long long)i->backlink,(unsigned long long)i->registry,
            (unsigned long long)i->group,i->flags,i->generation);
    }return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_find_rng_config(lua_State *L) {size_t length=0;const char *text=p_lua_tolstring(L,1,&length);p_lua_pushnumber(L,world_find_configure(text,length));return 1;}
static int lua_world_find_rng_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_find_started) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=world_find_status(out,sizeof(out));if(n<0) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_find_rng_row(lua_State *L) {
    double row=p_lua_tonumber(L,1);unsigned ready=world_find_ready()*4;
    if(GetCurrentThreadId()!=ui_thread||!world_find_started||!isfinite(row)||row<0||floor(row)!=row) {p_lua_pushnil(L);return 1;}
    if(!row) {p_lua_pushnumber(L,ready);return 1;}
    if(row>ready||row!=world_find_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned nrow=(unsigned)row-1;char out[1536];int n=world_find_format(&world_find_events[nrow/4],nrow/4+1,nrow%4,out,sizeof(out));
    if(n<0) p_lua_pushnil(L);else {++world_find_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
