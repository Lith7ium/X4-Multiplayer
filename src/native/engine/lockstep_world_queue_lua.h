static int lua_world_queue_trace_config(lua_State *L) {
    int configured=world_queue_configure();
    /* The optional provenance extension becomes reachable only after the
     * explicit early queue manifest and native hook admission succeed. */
    if(configured) npc_provenance_early_eligible=world_queue_early_eligible;
    p_lua_pushnumber(L,configured);return 1;
}
static int lua_world_queue_trace_status(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !world_queue_started) {p_lua_pushnil(L);return 1;}
    unsigned ready=world_queue_ready(),outstanding=0;AcquireSRWLockShared(&world_queue_lock);
    for(unsigned i=0;i<WORLD_QUEUE_MAX;i++) if(world_queue_lifetimes[i].state==1 || world_queue_lifetimes[i].state==2) ++outstanding;
    LONG64 open=world_queue_open,peak=world_queue_peak,restore_open=world_queue_restore_open;
    ReleaseSRWLockShared(&world_queue_lock);
    char out[512];int n=snprintf(out,sizeof(out),
        "schema=3 capacity=%u reserved=%llu complete=%u rows=%u failures=%llu dropped=%llu installed=%u restored=%u outstanding=%u preimport_calls=%llu open=%llu peak=%llu restore_open=%llu",
        WORLD_QUEUE_MAX,(unsigned long long)world_queue_count,ready,world_queue_exported,(unsigned long long)world_queue_failures,
        (unsigned long long)world_queue_dropped,world_queue_sites,!world_queue_sites,outstanding,(unsigned long long)world_queue_preimport_calls,
        (unsigned long long)open,(unsigned long long)peak,(unsigned long long)restore_open);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
#define WORLD_QUEUE_ROW_CAP 2048u
static int world_queue_format(const world_queue_event *e,unsigned ordinal,char *out,size_t capacity) {
    char payload[WORLD_TRACE_COMMAND_BYTES*2+1];
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<WORLD_TRACE_COMMAND_BYTES;i++) {payload[i*2]=digits[e->payload[i]>>4];payload[i*2+1]=digits[e->payload[i]&15];}
    payload[sizeof(payload)-1]=0;int n=snprintf(out,capacity,
        "WORLD_QUEUE_EVENT event=%u kind=%u tick=%llu id=%016llx related=%016llx reference=%016llx enter=%llu leave=%llu enqueue=%llu trace_event=%llu errors=%u thread=%u ui=%u boot=%u view=%u caller=%x order_type=%x payload_type=%x rng=%016llx time=%016llx source_status=%u source_tag=%x name_status=%u source_saved=%016llx source_pc=%llx name=%s command=%llx record=%llx exec=%llx context=%llx lifetime=%llx payload=%s",
        ordinal,e->kind,(unsigned long long)e->tick,(unsigned long long)e->saved,(unsigned long long)e->related,(unsigned long long)e->reference,
        (unsigned long long)e->enter,(unsigned long long)e->leave,(unsigned long long)e->enqueue,(unsigned long long)e->trace_event,
        e->errors,e->thread,e->ui,e->boot,e->view,e->caller,e->order_type,e->payload_type,(unsigned long long)e->rng,(unsigned long long)e->time,
        e->source_status,e->source_tag,e->name_status,(unsigned long long)e->source_saved,(unsigned long long)e->source_pc,e->name[0]?e->name:"-",
        (unsigned long long)e->command,(unsigned long long)e->record,(unsigned long long)e->source.exec,(unsigned long long)e->source.context,(unsigned long long)e->source.lifetime,payload);
    if(n>0 && (size_t)n<capacity) {
        int added=snprintf(out+n,capacity-(size_t)n,
            " phase_status=%u early_eligible=%u app_flags=%x vbi_loading=%u main=%u elapsed=%llu phase_view=%u phase_tls=%016llx game=%016llx real=%016llx saved_time=%016llx table=%llx",
            e->phase.status,e->phase.eligible,e->phase.app_flags,e->phase.vbi_loading,e->phase.main,(unsigned long long)e->phase.elapsed,e->phase.view,
            (unsigned long long)e->phase.tls_time,(unsigned long long)e->phase.game,(unsigned long long)e->phase.real,
            (unsigned long long)e->phase.saved,(unsigned long long)e->phase.table);
        if(added<0 || (size_t)added>=capacity-(size_t)n) n=-1;else n+=added;
    }
    if(n>0 && (size_t)n<capacity) {
        char header[33],before[27],after[27];
        for(unsigned i=0;i<16;i++) {header[i*2]=digits[e->command_header[i]>>4];header[i*2+1]=digits[e->command_header[i]&15];}
        for(unsigned i=0;i<13;i++) {
            before[i*2]=digits[e->flags_before[i]>>4];before[i*2+1]=digits[e->flags_before[i]&15];
            after[i*2]=digits[e->flags_after[i]>>4];after[i*2+1]=digits[e->flags_after[i]&15];
        }
        header[32]=before[26]=after[26]=0;
        int added=snprintf(out+n,capacity-(size_t)n,
            " order_size=%u order_value=%llu raw_caller=%llx caller_source=%u dispatch_parent=%llu command_header=%s flags_before=%s flags_after=%s",
            e->order_size,(unsigned long long)e->order_value,(unsigned long long)e->raw_caller,e->caller_source,
            (unsigned long long)e->dispatch_parent,header,before,after);
        if(added<0 || (size_t)added>=capacity-(size_t)n) n=-1;else n+=added;
    }
    return n<0 || (size_t)n>=capacity?-1:n;
}
static int lua_world_queue_trace_row(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !world_queue_started || !isfinite(number) || floor(number)!=number || number<0) {p_lua_pushnil(L);return 1;}
    unsigned ready=world_queue_ready();if(number==0) {p_lua_pushnumber(L,ready);return 1;}
    if(number>ready || number!=world_queue_exported+1) {p_lua_pushnil(L);return 1;}
    char out[WORLD_QUEUE_ROW_CAP];int n=world_queue_format(&world_queue_events[(unsigned)number-1],(unsigned)number,out,sizeof(out));
    if(n<0) {p_lua_pushnil(L);return 1;}
    ++world_queue_exported;p_lua_pushlstring(L,out,(size_t)n);return 1;
}
