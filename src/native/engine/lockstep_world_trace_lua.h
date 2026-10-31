/* Append-only diagnostic export; native state hashes remain unchanged. */
#define WORLD_TRACE_ROWS 27u
static int lua_world_trace_config(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);double last=p_lua_tonumber(L,2);
    /* The manifest owns first-configuration thread/phase checks and permits an
     * exact, read-only retry from a loading UI before simulation UI adoption. */
    p_lua_pushnumber(L,isfinite(last) && floor(last)==last && last>=2 && last<=12 &&
        world_trace_manifest(text,size,(unsigned)last));return 1;
}
static int lua_world_trace(lua_State *L) {
    double last=p_lua_tonumber(L,1);
    if(!accept_ui() || !stats.active || stats.issued!=1 || !isfinite(last) || floor(last)!=last ||
       last<2 || last>12 || !world_hash_scans || world_hash_failures || !world_hash_watch_count ||
       (!world_trace_early && world_trace_events) || !ship_installed || !nav_installed || !agent_installed || !flight_installed || !spline_installed ||
       !world_trace_layout_pins()) {
        p_lua_pushnumber(L,0);return 1;
    }
    if(world_trace_early) {
        if(world_trace_early_failures || world_trace_failures || world_trace_bound!=world_hash_watch_count ||
           world_trace_last_tick!=(unsigned)last) {p_lua_pushnumber(L,0);return 1;}
        for(unsigned i=0;i<world_hash_watch_count;i++) {
            world_hash_watch *a=&world_hash_watches[i];world_trace_watch *b=&world_trace_watches[i];
            if(a->saved!=b->saved || a->runtime!=b->runtime || a->object!=b->object) {p_lua_pushnumber(L,0);return 1;}
        }
        p_lua_pushnumber(L,world_trace_boundary(WT_BASELINE));return 1;
    }
    for(unsigned i=0;i<world_hash_watch_count;i++) {
        world_hash_watch *source=&world_hash_watches[i];world_trace_watch *w=&world_trace_watches[i];
        unsigned char *slots;
        for(unsigned j=0;j<ACTOR_CAPACITY;j++) if(source->object==actor_components[j]) {p_lua_pushnumber(L,0);return 1;}
        *w=(world_trace_watch){.saved=source->saved,.runtime=source->runtime,.object=source->object};
        if(!w->object || !read_local(w->object+0x200,&w->controller,8) ||
           !read_local(w->controller+0x30,&slots,8) || !slots) {p_lua_pushnumber(L,0);return 1;}
        w->agent=slots-0xad0;
    }
    world_trace_watch_count=world_hash_watch_count;
    if(!world_trace_prepare((unsigned)last)) {p_lua_pushnumber(L,0);return 1;}
    for(unsigned i=0;i<world_trace_watch_count;i++) {
        world_trace_event *event=world_trace_begin(WT_BASELINE,world_trace_watches[i].object,NULL,0,0,0,0);
        world_trace_end(event);
    }
    p_lua_pushnumber(L,world_trace_failures==0);return 1;
}
static int lua_world_trace_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || (!world_trace_events && !world_trace_early)) {p_lua_pushnil(L);return 1;}
    unsigned ready=world_trace_ready();LONG64 count=InterlockedCompareExchange64(&world_trace_count,0,0);
    unsigned imports=0;for(unsigned i=0;i<world_trace_watch_count;i++) imports+=world_trace_imports[i];
    char out[512];int n=snprintf(out,sizeof(out),
        "schema=3 watches=%u first=%u last=%u capacity=%u reserved=%llu complete=%u rows=%u failures=%llu dropped=%llu unmatched=%llu early=%u early_bound=%u early_imports=%u early_failures=%llu early_stage=%s",
        world_trace_watch_count,world_trace_early?0:2,world_trace_last_tick,WORLD_TRACE_MAX,(unsigned long long)count,ready,world_trace_exported,
        (unsigned long long)InterlockedCompareExchange64(&world_trace_failures,0,0),
        (unsigned long long)InterlockedCompareExchange64(&world_trace_dropped,0,0),
        (unsigned long long)((count>WORLD_TRACE_MAX?WORLD_TRACE_MAX:count)-ready),
        (unsigned)world_trace_early,world_trace_bound,imports,(unsigned long long)world_trace_early_failures,world_trace_early_stage);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_trace_row(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || (!world_trace_events && !world_trace_early) || !isfinite(number) || floor(number)!=number || number<0) {p_lua_pushnil(L);return 1;}
    unsigned count=world_trace_ready()*WORLD_TRACE_ROWS;
    if(number==0) {p_lua_pushnumber(L,count);return 1;}
    if(number>count || number!=world_trace_exported+1) {p_lua_pushnil(L);return 1;}
    unsigned index=((unsigned)number-1)/WORLD_TRACE_ROWS,row=((unsigned)number-1)%WORLD_TRACE_ROWS;
    world_trace_event *e=&world_trace_events[index];world_trace_watch *w=&world_trace_watches[e->watch];
    const char *kinds[]={"baseline","ship","nav","agent","steering","spline","loaded","path_before","path_after","ai","pending","order"};char out[8192];int n;
    if(!row) n=snprintf(out,sizeof(out),
        "WORLD_TRACE_EVENT event=%u tick=%llu id=%016llx hook=%s parent=%llu enter=%llu leave=%llu thread=%u ui=%u errors=%u dt=%08x time=%016llx cheap=%u player=%u boot=%u caller=%x script=%s pc_before=%llx pc_after=%llx",
        index+1,(unsigned long long)e->tick,(unsigned long long)w->saved,kinds[e->kind],
        (unsigned long long)e->parent,(unsigned long long)e->enter,(unsigned long long)e->leave,e->thread,e->ui,e->errors,
        e->dt,(unsigned long long)e->time,e->cheap,e->player,e->boot,e->caller,e->script[0]?e->script:"-",(unsigned long long)e->pc_before,(unsigned long long)e->pc_after);
    else {
        unsigned phase=(row-1)/13,block=(row-1)%13;world_trace_snapshot *s=phase?&e->after:&e->before;
        const char *name=phase?"after":"before";
        if(!block) n=snprintf(out,sizeof(out),
            "WORLD_TRACE_STATE event=%u phase=%s rng=%016llx tls_time=%016llx view=%u read=%u write=%u read_end=%u write_end=%u model=%x model_bytes=%u stage=%u main_count=%u main_capacity=%u pending_count=%u pending_capacity=%u unknown_refs=%u",
            index+1,name,(unsigned long long)s->rng,(unsigned long long)s->tls_time,s->tls_view,s->read,s->write,s->read_end,s->write_end,s->model_rva,s->model_length,s->stage,
            s->queue_count[0],s->queue_capacity[0],s->queue_count[1],s->queue_capacity[1],s->queue_unknown_refs);
        else {
            const char *names[]={"banks","params","agent","model","slot","nav","timer","manager","main_queue","pending_queue","main_refs","pending_refs"};
            const unsigned char *data[]={s->banks,s->params,s->agent_state,s->model,s->slot,s->nav,s->agent_timer,s->manager,s->queue[0],s->queue[1],s->queue_refs[0],s->queue_refs[1]};
            const unsigned lengths[]={sizeof(s->banks),sizeof(s->params),sizeof(s->agent_state),s->model_length,e->slot?sizeof(s->slot):0,sizeof(s->nav),sizeof(s->agent_timer),sizeof(s->manager),s->queue_count[0]*WORLD_TRACE_COMMAND_BYTES,s->queue_count[1]*WORLD_TRACE_COMMAND_BYTES,s->queue_count[0]*12,s->queue_count[1]*12};
            n=snprintf(out,sizeof(out),"WORLD_TRACE_DATA event=%u phase=%s block=%s bytes=",index+1,name,names[block-1]);
            static const char digits[]="0123456789abcdef";unsigned length=lengths[block-1];
            if(n<0 || (size_t)n+length*2>=sizeof(out)) {p_lua_pushnil(L);return 1;}
            for(unsigned i=0;i<length;i++) {unsigned char value=data[block-1][i];out[n++]=digits[value>>4];out[n++]=digits[value&15];}
            if(!length) out[n++]='-';
        }
    }
    if(n<0 || (size_t)n>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    ++world_trace_exported;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
