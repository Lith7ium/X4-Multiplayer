/* New capability only; the old portable_config path never calls this API. */
static int lua_portable_rotation_rejection_enable(lua_State *L) {
    double value=p_lua_tonumber(L,1);int ok=0;
    if(value==0 || value==1)ok=rotation_rejection_enable((unsigned)value);
    p_lua_pushnumber(L,ok);return 1;
}
/* Caller holds rotation_lock. Only retained DLL-owned bytes are exported;
 * restoration/identity getters must never be invoked at this boundary. */
static int rotation_rejection_text(unsigned index,char *out,size_t capacity) {
    if(!rotation_stop || rotation_scope_open || rotation_load_open)return 0;
    const rotation_reject_entry *e=&rotation_reject_first;const rotation_reject_trace *t=&e->trace;int n=0;
    if(index==0) {
        n=snprintf(out,capacity,
            "schema=x4-rotation-reject-v1 enabled=%u present=%u readout_drained=1 capture_scope=failing_stage context_scope=predicate_only "
            "stage=%u predicate=%u helper_known=%u helper_result=%d operands_known=%u actual=%016llx expected=%016llx "
            "serial=%llu role=%u class=%u accounted=%u owner=%u thread=%u phase=%u scope_open=%u load_open=%u "
            "primary_failure_at_entry=%u selected0=%llu selected1=%llu load_calls=%llu load_returns=%llu "
            "image_base=%016llx basis=%016llx xml=%016llx agent=%016llx movement=%016llx bank=%016llx "
            "records=%u bytes=%u truncated=%u failed_reads=%u attempted_reads=%u dropped_records=%u",
            (unsigned)(rotation_reject_mode==1),e->present,e->stage,e->predicate,e->helper_known,e->helper_result,e->operands_known,
            (unsigned long long)e->actual,(unsigned long long)e->expected,(unsigned long long)e->serial,e->role,e->present?e->class_id:2,
            e->accounted,e->owner,e->thread,e->phase,e->scope_open,e->load_open,e->primary_failure,
            (unsigned long long)e->selected[0],(unsigned long long)e->selected[1],(unsigned long long)e->load_calls,(unsigned long long)e->load_returns,
            (unsigned long long)e->image_base,(unsigned long long)e->basis,(unsigned long long)e->xml,(unsigned long long)e->agent,
            (unsigned long long)e->movement,(unsigned long long)e->bank,t->records,t->bytes,t->truncated,t->failed_reads,t->attempted_reads,t->dropped_records);
    }else if(e->present && index<=t->records) {
        const rotation_reject_read *r=&t->reads[index-1];char hex[ROTATION_REJECT_READ_MAX*2+1];
        if(r->copied>ROTATION_REJECT_READ_MAX || r->offset>t->bytes || r->copied>t->bytes-r->offset)return 0;
        static const char digits[]="0123456789abcdef";
        for(unsigned i=0;i<r->copied;i++){unsigned b=t->data[r->offset+i];hex[i*2]=digits[b>>4];hex[i*2+1]=digits[b&15];}
        hex[r->copied*2]=0;
        n=snprintf(out,capacity,"schema=x4-rotation-reject-read-v1 index=%u address=%016llx requested_bytes=%llu copied_bytes=%u ok=%u data=%s",
            index,(unsigned long long)r->address,(unsigned long long)r->requested,r->copied,r->ok,r->copied?hex:"-");
    }else if(e->present && index==t->records+1 && rotation_reject_done.present) {
        const rotation_reject_completion *c=&rotation_reject_done;char raw[17];
        if(c->return_valid)snprintf(raw,sizeof raw,"%016llx",(unsigned long long)c->raw_return);else strcpy(raw,"-");
        n=snprintf(out,capacity,"schema=x4-rotation-reject-completion-v1 return_valid=%u raw_return=%s unwind=%u primary_failure_after=%u "
            "scope_open=%u load_open=%u role0_returns=%llu role1_returns=%llu",
            c->return_valid,raw,c->unwind,c->primary_failure,c->scope_open,c->load_open,
            (unsigned long long)c->role_returns[0],(unsigned long long)c->role_returns[1]);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_portable_rotation_rejection(lua_State *L) {
    double value=p_lua_tonumber(L,1);char text[2048];int n=0;
    if(value>=0 && value<=ROTATION_REJECT_RECORDS+1 && value==(unsigned)value) {
        AcquireSRWLockShared(&rotation_lock);n=rotation_rejection_text((unsigned)value,text,sizeof text);ReleaseSRWLockShared(&rotation_lock);
    }
    if(n)p_lua_pushlstring(L,text,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_portable_rotation_config(lua_State *L) {
    size_t a=0,b=0,c=0,d=0;
    const char *mode=p_lua_tolstring(L,1,&a),*descriptor=p_lua_tolstring(L,2,&b);
    const char *snapshot=p_lua_tolstring(L,3,&c),*contract=p_lua_tolstring(L,4,&d);
    p_lua_pushnumber(L,rotation_native_config(mode,a,descriptor,b,snapshot,c,contract,d));return 1;
}
static int lua_portable_rotation_ready(lua_State *L) {
    AcquireSRWLockShared(&rotation_lock);rotation_identity now;
    int ready=GetCurrentThreadId()==ui_thread && rotation_configured && !rotation_is_stopped(NULL) &&
        boot_mode && !boot_phase && !stats.issued && rotation_phase==ROTATION_WAITING &&
        !rotation_load_calls && !rotation_load_open && !rotation_tail_calls &&
        rotation_identity_read(&now,1) && rotation_same_host(&rotation_menu,&now,1) &&
        rotation_patch_owned(&rotation_patches[0]) && rotation_patch_owned(&rotation_patches[1]);
    ReleaseSRWLockShared(&rotation_lock);p_lua_pushnumber(L,ready);return 1;
}
static int lua_portable_rotation_status(lua_State *L) {
    /* Diagnostic reads after STOP do not adopt a new UI thread or grant control. */
    char text[2048];AcquireSRWLockShared(&rotation_lock);
    int n=snprintf(text,sizeof(text),
        "schema=x4-rotation-status-v2 mode=%s descriptor=%s contract=%s snapshot=%s configured=%ld phase=%u stop=%ld failure=%ld "
        "load_calls=%llu load_returns=%llu load_true=%llu load_false=%llu load_abnormal=%llu load_open=%u "
        "role0_calls=%llu role0_returns=%llu role0_euler=%llu role1_calls=%llu role1_returns=%llu role1_euler=%llu "
        "foreign=%llu open=%u owned=%u restore_error=%u restored=%u complete=%u inputs=%s "
        "role0_nav_calls=%llu role0_nav_returns=%llu role0_nav_euler=%llu "
        "role0_player_calls=%llu role0_player_returns=%llu role0_player_euler=%llu "
        "role1_nav_calls=%llu role1_nav_returns=%llu role1_nav_euler=%llu "
        "role1_player_calls=%llu role1_player_returns=%llu role1_player_euler=%llu lifetimes=%u paired=%u",
        X4ROT_IMPORT_MODE,X4ROT_IMPORT_DESCRIPTOR_SHA256,X4ROT_IMPORT_CONTRACT_SHA256,X4ROT_IMPORT_SNAPSHOT_SHA256,
        (long)rotation_configured,rotation_phase,(long)rotation_stop,(long)rotation_failure,
        (unsigned long long)rotation_load_calls,(unsigned long long)rotation_load_returns,
        (unsigned long long)rotation_load_true,(unsigned long long)rotation_load_false,
        (unsigned long long)rotation_load_abnormal,rotation_load_open,
        (unsigned long long)rotation_selected[0],(unsigned long long)rotation_role_returns[0],(unsigned long long)rotation_written[0],
        (unsigned long long)rotation_selected[1],(unsigned long long)rotation_role_returns[1],(unsigned long long)rotation_written[1],
        (unsigned long long)rotation_foreign,rotation_scope_open,
        rotation_patches[0].owned|(rotation_patches[1].owned<<1),rotation_restore_error,
        (unsigned)rotation_native_restored(),(unsigned)(!rotation_failure && rotation_counts_complete()),X4ROT_IMPORT_INPUTS_SHA256,
        (unsigned long long)rotation_class_calls[0][0],(unsigned long long)rotation_class_returns[0][0],(unsigned long long)rotation_class_written[0][0],
        (unsigned long long)rotation_class_calls[0][1],(unsigned long long)rotation_class_returns[0][1],(unsigned long long)rotation_class_written[0][1],
        (unsigned long long)rotation_class_calls[1][0],(unsigned long long)rotation_class_returns[1][0],(unsigned long long)rotation_class_written[1][0],
        (unsigned long long)rotation_class_calls[1][1],(unsigned long long)rotation_class_returns[1][1],(unsigned long long)rotation_class_written[1][1],
        rotation_lifetime_count,rotation_paired);
    ReleaseSRWLockShared(&rotation_lock);
    if(n<0 || (size_t)n>=sizeof(text)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,text,(size_t)n);return 1;
}
