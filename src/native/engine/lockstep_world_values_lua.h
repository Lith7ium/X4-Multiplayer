#define WORLD_VALUES_ROW_CAP 2048u
static void world_values_hex(const void *data,size_t size,char *out) {
    const unsigned char *bytes=data;const char *digits="0123456789abcdef";
    for(size_t i=0;i<size;i++) {out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}out[size*2]=0;
}
static unsigned world_values_event_rows(const world_values_event *e) {
    return 3+(e->kind==1?(unsigned)e->before.count+(unsigned)e->after.count:0);
}
static unsigned world_values_rows(void) {
    unsigned rows=0,ready=world_values_ready();for(unsigned i=0;i<ready;i++) rows+=world_values_event_rows(&world_values_events[i]);return rows;
}
static int world_values_status(char *out,size_t size) {
    AcquireSRWLockShared(&world_values_lock);
    int n=snprintf(out,size,"schema=1 capacity=%u candidate_capacity=%u reserved=%llu complete=%u rows=%u failures=%llu dropped=%llu calls=%llu other_scope=%llu other_type=%llu open=%llu peak=%llu restore_open=%llu sites=%u closed=%u patch_failures=%llu patch_errors=%u protection_pending=%u flush_pending=%u unknown_total=%llu unknown_samples=%u unknown_rows=%u unknown_omitted=%llu reason1=%llu reason2=%llu reason3=%llu reason4=%llu reason5=%llu reason6=%llu reason7=%llu exception_cleanup=0",
        WORLD_VALUES_MAX,WORLD_VALUES_CANDIDATES,(unsigned long long)world_values_reserved,world_values_ready(),world_values_exported,
        (unsigned long long)world_values_failures,(unsigned long long)world_values_dropped,(unsigned long long)world_values_calls,
        (unsigned long long)world_values_other_scope,(unsigned long long)world_values_other_type,(unsigned long long)world_values_open,
        (unsigned long long)world_values_peak,(unsigned long long)world_values_restore_open,world_values_sites,world_values_closing!=0,(unsigned long long)world_values_patch_failures,(unsigned)world_values_patch_errors,world_values_pending(0),world_values_pending(1),
        (unsigned long long)world_values_unknown_total,world_values_unknown_reserved,world_values_unknown_exported,(unsigned long long)world_values_unknown_omitted,
        (unsigned long long)world_values_unknown_counts[1],(unsigned long long)world_values_unknown_counts[2],(unsigned long long)world_values_unknown_counts[3],
        (unsigned long long)world_values_unknown_counts[4],(unsigned long long)world_values_unknown_counts[5],(unsigned long long)world_values_unknown_counts[6],(unsigned long long)world_values_unknown_counts[7]);
    ReleaseSRWLockShared(&world_values_lock);return n<0||(size_t)n>=size?-1:n;
}
static int world_values_format(const world_values_event *e,unsigned event,unsigned part,char *out,size_t size) {
    int n=-1;char raw[33],payload[33],declaration[69],optional_before[65],optional_after[65];
    if(part==0) n=snprintf(out,size,
        "WORLD_VALUES_EVENT event=%u kind=%u errors=%u thread=%u action=%u candidate=%u children=%u enter=%llu leave=%llu caller=%llx result=%llx rng_before=%016llx rng_after=%016llx pc_before=%llu pc_after=%llu flags_before=%x flags_after=%x a=%llx b=%llx c=%llx d=%llx",
        event,e->kind,e->errors,e->thread,e->parent_action,e->parent_candidate,e->children,(unsigned long long)e->enter,(unsigned long long)e->leave,
        (unsigned long long)e->caller,(unsigned long long)e->result,(unsigned long long)e->rng_before,(unsigned long long)e->rng_after,
        (unsigned long long)e->pc_before,(unsigned long long)e->pc_after,e->flags_before,e->flags_after,
        (unsigned long long)e->arguments[0],(unsigned long long)e->arguments[1],(unsigned long long)e->arguments[2],(unsigned long long)e->arguments[3]);
    else if(part==1) {
        world_values_hex(e->value.raw,16,raw);world_values_hex(e->value.payload,16,payload);world_values_hex(e->declaration,sizeof(e->declaration),declaration);
        world_values_hex(&e->optional_before,sizeof(e->optional_before),optional_before);world_values_hex(&e->optional_after,sizeof(e->optional_after),optional_after);
        n=snprintf(out,size,"WORLD_VALUES_VALUE event=%u tag=%x kind=%u raw=%s payload=%s runtime=%llx saved=%016llx declaration=%s optional_before=%s optional_after=%s begin_before=%llx end_before=%llx capacity_before=%llx count_before=%llu begin_after=%llx end_after=%llx capacity_after=%llx count_after=%llu",
            event,e->value.tag,e->value.kind,raw,payload,(unsigned long long)e->value.runtime,(unsigned long long)e->value.saved,declaration,optional_before,optional_after,
            (unsigned long long)e->before.begin,(unsigned long long)e->before.end,(unsigned long long)e->before.capacity,(unsigned long long)e->before.count,
            (unsigned long long)e->after.begin,(unsigned long long)e->after.end,(unsigned long long)e->after.capacity,(unsigned long long)e->after.count);
    } else if(part==2) {
        const world_queue_phase_sample *a=&e->phase_before,*b=&e->phase_after;
        n=snprintf(out,size,"WORLD_VALUES_PHASE event=%u status_before=%u eligible_before=%u app_before=%x vbi_before=%u view_before=%u main_before=%u elapsed_before=%llu tls_before=%016llx game_before=%016llx real_before=%016llx saved_before=%016llx status_after=%u eligible_after=%u app_after=%x vbi_after=%u view_after=%u main_after=%u elapsed_after=%llu tls_after=%016llx game_after=%016llx real_after=%016llx saved_after=%016llx",
            event,a->status,a->eligible,a->app_flags,a->vbi_loading,a->view,a->main,(unsigned long long)a->elapsed,
            (unsigned long long)a->tls_time,(unsigned long long)a->game,(unsigned long long)a->real,(unsigned long long)a->saved,
            b->status,b->eligible,b->app_flags,b->vbi_loading,b->view,b->main,(unsigned long long)b->elapsed,
            (unsigned long long)b->tls_time,(unsigned long long)b->game,(unsigned long long)b->real,(unsigned long long)b->saved);
    } else if(e->kind==1) {
        unsigned index=part-3,phase=index>=e->before.count;
        if(phase) index-=(unsigned)e->before.count;
        const world_values_candidates *v=phase?&e->after:&e->before;
        if(index>=v->count||index>=WORLD_VALUES_CANDIDATES) return -1;
        const world_values_component *item=&v->items[index];
        n=snprintf(out,size,"WORLD_VALUES_ARRAY event=%u phase=%u index=%u object=%llx runtime=%llx saved=%016llx type=%x valid=%u",
            event,phase,index,(unsigned long long)item->object,(unsigned long long)item->runtime,(unsigned long long)item->saved,item->type,item->valid);
    }
    return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_values_config(lua_State *L) {p_lua_pushnumber(L,world_values_configure());return 1;}
static int lua_world_values_cursors(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_values_started) {p_lua_pushnil(L);return 1;}
    p_lua_pushnumber(L,world_values_exported);p_lua_pushnumber(L,world_values_unknown_exported);p_lua_pushnumber(L,world_unknown_identity_exported);return 3;
}
static unsigned world_unknown_identity_parts(const wui_result *r) {
    unsigned count=r->before.script.d[5]>r->after.script.d[5]?r->before.script.d[5]:r->after.script.d[5];
    return 2+(count>WUI_HOPS?WUI_HOPS:count);
}
static unsigned world_unknown_identity_rows(void) {
    unsigned rows=0,ready=world_values_unknown_ready();
    for(unsigned i=0;i<ready;i++) rows+=world_unknown_identity_parts(&world_values_unknown_events[i].identity);
    return rows;
}
static int world_unknown_identity_format(const wui_result *r,unsigned sample,unsigned part,char *out,size_t size) {
    unsigned char bytes[WUI_OBJECT_BYTES];char before[WUI_OBJECT_BYTES*2+1],after[WUI_OBJECT_BYTES*2+1];
    if(part>=world_unknown_identity_parts(r)) return -1;
    unsigned n=wui_encode(r,0,part,bytes);world_values_hex(bytes,n,before);
    n=wui_encode(r,1,part,bytes);world_values_hex(bytes,n,after);
    int length=snprintf(out,size,"WORLD_UNKNOWN_IDENTITY schema=1 sample=%u part=%u reads=%u bytes=%u error=%u stopped=%u boundary=%u before=%s after=%s",
        sample,part,r->reads,r->bytes,r->error,r->stopped,r->boundary,before,after);
    return length<0||(size_t)length>=size?-1:length;
}
static int lua_world_values_unknown_identity_row(lua_State *L) {
    double value=p_lua_tonumber(L,1);unsigned rows=world_unknown_identity_rows();
    if(GetCurrentThreadId()!=ui_thread||!world_values_started||!isfinite(value)||value<0||floor(value)!=value) {p_lua_pushnil(L);return 1;}
    if(!value) {p_lua_pushnumber(L,rows);return 1;}
    if(value>rows||value!=world_unknown_identity_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned part=(unsigned)value-1,ready=world_values_unknown_ready();
    for(unsigned i=0;i<ready;i++) {
        unsigned count=world_unknown_identity_parts(&world_values_unknown_events[i].identity);
        if(part>=count) {part-=count;continue;}
        char out[WORLD_VALUES_ROW_CAP];int n=world_unknown_identity_format(&world_values_unknown_events[i].identity,i+1,part,out,sizeof(out));
        if(n<0) p_lua_pushnil(L);else {++world_unknown_identity_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
    }
    p_lua_pushnil(L);return 1;
}
static int lua_world_values_unknown_identity_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_values_started) {p_lua_pushnil(L);return 1;}
    char out[256];unsigned samples,complete,rows,exported,closed;uint64_t open;
    AcquireSRWLockShared(&world_values_lock);
    samples=world_values_unknown_reserved;complete=world_values_unknown_ready();rows=world_unknown_identity_rows();
    exported=world_unknown_identity_exported;open=(uint64_t)world_values_open;closed=world_values_closing!=0;
    ReleaseSRWLockShared(&world_values_lock);
    int n=snprintf(out,sizeof(out),"schema=1 samples=%u complete=%u rows=%u exported=%u open=%llu closed=%u read_limit=%u byte_limit=%u hop_limit=%u",
        samples,complete,rows,exported,(unsigned long long)open,closed,WUI_READS,WUI_BYTES,WUI_HOPS);
    if(n<0||(size_t)n>=sizeof(out)) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int world_values_unknown_format(const world_values_unknown_event *e,unsigned index,char *out,size_t size) {
    const world_action_guard *g=&e->owner;const world_queue_phase_sample *p=&e->phase;
    int n=snprintf(out,size,"WORLD_VALUES_UNKNOWN index=%u reason=%u reads=%u owned=%u errors=%u thread=%u ui=%u boot=%u tick=%llu exec=%llx current=%llx raw_tag=%x raw_value=%llx tag=%x context=%llx lifetime=%llx entity=%llx runtime=%llx saved=%016llx phase_status=%u eligible=%u app=%x vbi=%u view=%u main=%u elapsed=%llu tls=%016llx game=%016llx real=%016llx saved_time=%016llx",
        index,e->reason,e->reads,e->owned,e->errors,e->thread,e->ui,e->boot,(unsigned long long)e->tick,(unsigned long long)e->exec,(unsigned long long)e->current,
        e->raw_tag,(unsigned long long)e->raw_value,g->tag,(unsigned long long)g->context,(unsigned long long)g->lifetime,(unsigned long long)g->entity,
        (unsigned long long)g->runtime,(unsigned long long)g->saved,p->status,p->eligible,p->app_flags,p->vbi_loading,p->view,p->main,(unsigned long long)p->elapsed,
        (unsigned long long)p->tls_time,(unsigned long long)p->game,(unsigned long long)p->real,(unsigned long long)p->saved);
    return n<0||(size_t)n>=size?-1:n;
}
static int lua_world_values_unknown_row(lua_State *L) {
    double row=p_lua_tonumber(L,1);unsigned ready=world_values_unknown_ready();
    if(GetCurrentThreadId()!=ui_thread||!world_values_started||!isfinite(row)||row<0||floor(row)!=row) {p_lua_pushnil(L);return 1;}
    if(!row) {p_lua_pushnumber(L,ready);return 1;}
    if(row>ready||row!=world_values_unknown_exported+1) {p_lua_pushnil(L);return 1;}
    char out[2048];int n=world_values_unknown_format(&world_values_unknown_events[(unsigned)row-1],(unsigned)row,out,sizeof(out));
    if(n<0) p_lua_pushnil(L);else {++world_values_unknown_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
}
static int lua_world_values_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread||!world_values_started) {p_lua_pushnil(L);return 1;}
    char out[1024];int n=world_values_status(out,sizeof(out));if(n<0) p_lua_pushnil(L);else p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_values_row(lua_State *L) {
    double value=p_lua_tonumber(L,1);unsigned rows=world_values_rows();
    if(GetCurrentThreadId()!=ui_thread||!world_values_started||!isfinite(value)||value<0||floor(value)!=value) {p_lua_pushnil(L);return 1;}
    if(!value) {p_lua_pushnumber(L,rows);return 1;}
    if(value>rows||value!=world_values_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned part=(unsigned)value-1,ready=world_values_ready();
    for(unsigned i=0;i<ready;i++) {
        unsigned count=world_values_event_rows(&world_values_events[i]);if(part>=count) {part-=count;continue;}
        char out[WORLD_VALUES_ROW_CAP];int n=world_values_format(&world_values_events[i],i+1,part,out,sizeof(out));
        if(n<0) p_lua_pushnil(L);else {++world_values_exported;p_lua_pushlstring(L,out,(size_t)n);}return 1;
    }
    p_lua_pushnil(L);return 1;
}
