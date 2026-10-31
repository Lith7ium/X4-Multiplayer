/* Three bounded rows per completed action; raw pointers are local evidence. */
#define WORLD_ACTION_ROWS 3u
#define WORLD_ACTION_ROW_CAP 2048u
static int lua_world_action_trace_config(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,world_action_configure(text,length));return 1;
}
static int world_action_status(char *out,size_t capacity) {
    AcquireSRWLockShared(&world_action_lock);
    int n=snprintf(out,capacity,
        "schema=1 capacity=%u selected=%u reserved=%llu complete=%u rows=%u failures=%llu dropped=%llu calls=%llu other_owner=%llu other_script=%llu other_context=%llu other_phase=%llu unknown=%llu threads=%llu open=%llu peak=%llu restore_open=%llu closed=%u unknown_rng=%llu value_coverage=0 chance_coverage=0 exception_cleanup=0",
        WORLD_ACTION_MAX,world_action_selected_count,(unsigned long long)world_action_reserved,world_action_ready(),world_action_exported,
        (unsigned long long)world_action_failures,(unsigned long long)world_action_dropped,(unsigned long long)world_action_calls,
        (unsigned long long)world_action_other_owner,(unsigned long long)world_action_other_script,(unsigned long long)world_action_other_context,
        (unsigned long long)world_action_other_phase,(unsigned long long)world_action_unknown,(unsigned long long)world_action_threads,
        (unsigned long long)world_action_open,(unsigned long long)world_action_peak,(unsigned long long)world_action_restore_open,
        world_action_closing!=0,(unsigned long long)world_action_unknown_rng);
    ReleaseSRWLockShared(&world_action_lock);return n<0||(size_t)n>=capacity?-1:n;
}
static int lua_world_action_trace_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_action_started) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=world_action_status(out,sizeof(out));
    if(n<0) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int world_action_format(const world_action_event *e,unsigned event,unsigned part,char *out,size_t capacity) {
    int n=-1;const world_action_guard *g=&e->before;
    if(part==0) n=snprintf(out,capacity,
        "WORLD_ACTION_EVENT event=%u part=0 selected=%u errors=%u stage=%u thread=%u ui=%u boot=%u tick=%llu enter=%llu leave=%llu parent=%llu depth=%u children=%u unobserved_children=%u id=%016llx name=%s version=%u slot=%llu program_hash=%016llx type=%llx action=%llx chance=%x rng_before=%016llx rng_after=%016llx pc_before=%llu pc_after=%llu flags_before=%x flags_after=%x",
        event,e->selected,e->errors,e->stage,e->thread,e->ui,e->boot,(unsigned long long)e->tick,
        (unsigned long long)e->enter,(unsigned long long)e->leave,(unsigned long long)e->parent,e->depth,e->children,e->unobserved_children,
        (unsigned long long)g->saved,g->name[0]?g->name:"-",g->version,(unsigned long long)g->index,(unsigned long long)g->program_hash,
        (unsigned long long)(g->vtable>=(uintptr_t)image?g->vtable-(uintptr_t)image:0),
        (unsigned long long)(g->action>=(uintptr_t)image?g->action-(uintptr_t)image:0),g->chance,
        (unsigned long long)e->rng_before,(unsigned long long)e->rng_after,(unsigned long long)e->pc_before,(unsigned long long)e->pc_after,e->flags_before,e->flags_after);
    else if(part==1||part==2) {
        if(part==2) g=&e->after;const world_queue_phase_sample *p=part==1?&e->phase_before:&e->phase_after;
        n=snprintf(out,capacity,
            "WORLD_ACTION_EVENT event=%u part=%u node=%llx exec=%llx frame=%llx current=%llx context=%llx lifetime=%llx entity=%llx runtime=%llx script=%llx program=%llx begin=%llx end=%llx vtable=%llx target=%llx id=%016llx name=%s version=%u slot=%llu program_hash=%016llx tag=%x chance=%x phase_status=%u eligible=%u elapsed=%llu app_flags=%x vbi_loading=%u view=%u main=%u tls_time=%016llx game=%016llx real=%016llx saved_time=%016llx table=%llx",
            event,part,(unsigned long long)g->node,(unsigned long long)g->exec,(unsigned long long)g->frame,(unsigned long long)g->current,
            (unsigned long long)g->context,(unsigned long long)g->lifetime,(unsigned long long)g->entity,(unsigned long long)g->runtime,
            (unsigned long long)g->script,(unsigned long long)g->program,(unsigned long long)g->begin,(unsigned long long)g->end,
            (unsigned long long)g->vtable,(unsigned long long)g->action,(unsigned long long)g->saved,g->name[0]?g->name:"-",g->version,
            (unsigned long long)g->index,(unsigned long long)g->program_hash,g->tag,g->chance,p->status,p->eligible,(unsigned long long)p->elapsed,
            p->app_flags,p->vbi_loading,p->view,p->main,(unsigned long long)p->tls_time,(unsigned long long)p->game,
            (unsigned long long)p->real,(unsigned long long)p->saved,(unsigned long long)p->table);
    }
    return n<0||(size_t)n>=capacity?-1:n;
}
static int lua_world_action_trace_row(lua_State *L) {
    double value=p_lua_tonumber(L,1);unsigned ready=world_action_ready()*WORLD_ACTION_ROWS;
    if(GetCurrentThreadId()!=ui_thread||!world_action_started||!isfinite(value)||value<0||floor(value)!=value) {p_lua_pushnil(L);return 1;}
    if(value==0) {p_lua_pushnumber(L,ready);return 1;}
    if(value>ready||value!=world_action_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned row=(unsigned)value-1;char out[WORLD_ACTION_ROW_CAP];
    int n=world_action_format(&world_action_events[row/WORLD_ACTION_ROWS],row/WORLD_ACTION_ROWS+1,row%WORLD_ACTION_ROWS,out,sizeof(out));
    if(n<0) {p_lua_pushnil(L);return 1;}++world_action_exported;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
