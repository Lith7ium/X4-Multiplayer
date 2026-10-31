/* One immutable completed event is exported in four bounded lines. Local raw
 * pointers/bytes remain distinct from canonical saved identity/manifest data. */
#define WORLD_WAIT_ROW_CAP 2048u
#define WORLD_WAIT_ROWS 4u
static int lua_world_wait_trace_config(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,world_wait_configure(text,length));return 1;
}
static int lua_world_wait_trace_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !world_wait_started) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&world_wait_lock);
    char out[768];int n=snprintf(out,sizeof(out),
        "schema=1 capacity=%u selected=%u reserved=%llu complete=%u rows=%u failures=%llu dropped=%llu calls=%llu import_calls=%llu foreign_calls=%llu unknown=%llu installed=%u restored=%u open=%llu peak=%llu restore_open=%llu index_coverage=0",
        WORLD_WAIT_MAX,world_wait_selected_count,(unsigned long long)world_wait_count,world_wait_ready(),world_wait_exported,
        (unsigned long long)world_wait_failures,(unsigned long long)world_wait_dropped,(unsigned long long)world_wait_calls,
        (unsigned long long)world_wait_import_calls,(unsigned long long)world_wait_foreign_calls,(unsigned long long)world_wait_unknown,
        world_wait_installed,!world_wait_installed,(unsigned long long)world_wait_open,(unsigned long long)world_wait_peak,(unsigned long long)world_wait_restore_open);
    ReleaseSRWLockShared(&world_wait_lock);p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int world_wait_format(const world_wait_event *e,unsigned event,unsigned part,char *out,size_t capacity) {
    int n=-1;const world_wait_guard *g=&e->before;
    if(part==0) {
        const world_wait_selection empty={0},*s=e->selected && e->selected<=world_wait_selected_count?&world_wait_selected[e->selected-1]:&empty;
        n=snprintf(out,capacity,
            "WORLD_WAIT_EVENT event=%u part=0 selected=%u errors=%u enter=%llu leave=%llu result=%u tick=%llu boot=%u thread=%u ui=%u caller=%llx id=%016llx name=%s version=%u since=%u xml_index=%u index_status=%u slot=%llu pc=%llu time=%016llx expected_time=%016llx minimum=%016llx maximum=%016llx rng_before=%016llx rng_after=%016llx due_before=%016llx due_after=%016llx flags_before=%x flags_after=%x reason_before=%x reason_after=%x",
            event,e->selected,e->errors,(unsigned long long)e->enter,(unsigned long long)e->leave,e->result,(unsigned long long)e->tick,e->boot,e->thread,e->ui,
            (unsigned long long)(e->caller>=(uintptr_t)image?e->caller-(uintptr_t)image:0),(unsigned long long)g->saved,g->name[0]?g->name:"-",g->version,g->since,s->index,e->index_status,
            (unsigned long long)g->slot,(unsigned long long)g->pc,(unsigned long long)g->time,(unsigned long long)s->time,(unsigned long long)s->minimum,(unsigned long long)s->maximum,
            (unsigned long long)e->rng_before,(unsigned long long)e->rng_after,(unsigned long long)e->due_before,(unsigned long long)e->due_after,
            e->flags_before,e->flags_after,e->reason_before,e->reason_after);
    } else if(part==1 || part==2) {
        if(part==2) g=&e->after;const world_queue_phase_sample *p=part==1?&e->phase_before:&e->phase_after;
        char expression[209],timeout[33];const char *digits="0123456789abcdef";
        for(unsigned i=0;i<104;i++) {expression[i*2]=digits[g->expression[i]>>4];expression[i*2+1]=digits[g->expression[i]&15];}expression[208]=0;
        for(unsigned i=0;i<16;i++) {timeout[i*2]=digits[g->timeout[i]>>4];timeout[i*2+1]=digits[g->timeout[i]&15];}timeout[32]=0;
        n=snprintf(out,capacity,
            "WORLD_WAIT_EVENT event=%u part=%u self=%llx exec=%llx value=%llx context=%llx context_vtable=%llx lifetime=%llx entity=%llx runtime=%llx script=%llx program=%llx begin=%llx end=%llx vtable=%llx blocking=%u listeners=%llx timeout=%s expression=%s phase_status=%u eligible=%u elapsed=%llu app_flags=%x vbi_loading=%u view=%u main=%u tls_time=%016llx game=%016llx real=%016llx saved_time=%016llx table=%llx",
            event,part,(unsigned long long)g->self,(unsigned long long)g->exec,(unsigned long long)g->value,(unsigned long long)g->context,(unsigned long long)g->context_vtable,(unsigned long long)g->lifetime,
            (unsigned long long)g->entity,(unsigned long long)g->runtime,(unsigned long long)g->script,(unsigned long long)g->program,(unsigned long long)g->begin,(unsigned long long)g->end,
            (unsigned long long)g->vtable,g->blocking,(unsigned long long)g->listeners,timeout,expression,p->status,p->eligible,(unsigned long long)p->elapsed,p->app_flags,p->vbi_loading,p->view,p->main,
            (unsigned long long)p->tls_time,(unsigned long long)p->game,(unsigned long long)p->real,(unsigned long long)p->saved,(unsigned long long)p->table);
    } else if(part==3) {
        g=&e->after;n=snprintf(out,capacity,
            "WORLD_WAIT_EVENT event=%u part=3 id=%016llx name=%s version=%u since=%u slot=%llu pc=%llu time=%016llx tag=%x",
            event,(unsigned long long)g->saved,g->name[0]?g->name:"-",g->version,g->since,(unsigned long long)g->slot,(unsigned long long)g->pc,(unsigned long long)g->time,g->tag);
    }
    return n<0 || (size_t)n>=capacity?-1:n;
}
static int lua_world_wait_trace_row(lua_State *L) {
    double value=p_lua_tonumber(L,1);unsigned ready=world_wait_ready()*WORLD_WAIT_ROWS;
    if(GetCurrentThreadId()!=ui_thread || !world_wait_started || !isfinite(value) || value<0 || floor(value)!=value) {p_lua_pushnil(L);return 1;}
    if(value==0) {p_lua_pushnumber(L,ready);return 1;}
    if(value>ready || value!=world_wait_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned row=(unsigned)value-1;char out[WORLD_WAIT_ROW_CAP];int n=world_wait_format(&world_wait_events[row/WORLD_WAIT_ROWS],row/WORLD_WAIT_ROWS+1,row%WORLD_WAIT_ROWS,out,sizeof(out));
    if(n<0) {p_lua_pushnil(L);return 1;}++world_wait_exported;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_wait_rng_config(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,world_wait_rng_configure(text,length));return 1;
}
static int world_wait_rng_format(const world_wait_event *e,unsigned event,char *out,size_t capacity) {
    const world_wait_rng_selection empty={0},*r=e->selected&&e->selected<=world_wait_selected_count?&world_wait_rng_selected[e->selected-1]:&empty;
    int n=snprintf(out,capacity,
        "WORLD_WAIT_RNG event=%u selected=%u configured=%u applied=%u refusal=%u restored=%u domain=%016llx key_schema=%u script_hash=%016llx time_hash=%016llx seed=%016llx caller_before=%016llx scoped_before=%016llx scoped_after=%016llx caller_restored=%016llx expected_slot=%llu expected_pc=%llu expected_since=%u",
        event,e->selected,e->rng_configured,e->rng_applied,e->rng_refusal,e->rng_restored,(unsigned long long)(r->policy==WWR_OBSERVE?0:WORLD_WAIT_RNG_DOMAIN),r->policy==WWR_OBSERVE?0:1,
        (unsigned long long)r->script_hash,(unsigned long long)r->time_hash,(unsigned long long)e->rng_seed,
        (unsigned long long)e->rng_before,(unsigned long long)e->rng_scoped_before,(unsigned long long)e->rng_scoped_after,
        (unsigned long long)e->rng_caller_restored,(unsigned long long)r->slot,(unsigned long long)r->pc,r->since);
    if(n<0||(size_t)n>=capacity) return -1;
    if(world_wait_rng_schema==2) {
        int extra=snprintf(out+n,capacity-(size_t)n," policy=%u natural_after=%016llx",e->rng_policy,
            (unsigned long long)(e->rng_policy==WWR_OBSERVE?e->rng_after:0));
        if(extra<0||(size_t)extra>=capacity-(size_t)n) return -1;n+=extra;
    }
    return n;
}
static int lua_world_wait_rng_row(lua_State *L) {
    double value=p_lua_tonumber(L,1);unsigned ready=world_wait_ready();
    if(GetCurrentThreadId()!=ui_thread||!world_wait_rng_started||!isfinite(value)||value<0||floor(value)!=value) {p_lua_pushnil(L);return 1;}
    if(value==0) {p_lua_pushnumber(L,ready);return 1;}
    if(value>ready||value!=world_wait_rng_exported+1) {p_lua_pushnil(L);return 1;}
    char out[WORLD_WAIT_ROW_CAP];int n=world_wait_rng_format(&world_wait_events[(unsigned)value-1],(unsigned)value,out,sizeof(out));
    if(n<0) {p_lua_pushnil(L);return 1;}++world_wait_rng_exported;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_wait_rng_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_wait_rng_started) {p_lua_pushnil(L);return 1;}
    AcquireSRWLockShared(&world_wait_lock);char out[512];int n=snprintf(out,sizeof(out),
        "schema=%u selected=%u rows=%u applied=%u refused=%u open=%llu restore_open=%llu closed=%u global_sync=0 exception_cleanup=0",
        world_wait_rng_schema,world_wait_selected_count,world_wait_rng_exported,world_wait_rng_applied,world_wait_rng_refused,
        (unsigned long long)world_wait_rng_open,(unsigned long long)world_wait_rng_restore_open,world_wait_restoring!=0);
    if(n>=0 && (size_t)n<sizeof(out) && world_wait_rng_schema==2) {
        int extra=snprintf(out+n,sizeof(out)-(size_t)n," observed=%u",world_wait_rng_observed);
        if(extra<0||(size_t)extra>=sizeof(out)-(size_t)n) n=-1;else n+=extra;
    }
    ReleaseSRWLockShared(&world_wait_lock);
    if(n<0||(size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
