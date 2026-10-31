/* Optional Lua control/export API. Published frame bytes are immutable; reads
 * never resample native state or call transform/shield lazy getters. */
static int lua_portable_config(lua_State *L) {
    size_t ids_length=0,digest_length=0;
    const char *ids=p_lua_tolstring(L,1,&ids_length),*hash=p_lua_tolstring(L,2,&digest_length);
    double last=p_lua_tonumber(L,3);
    int ok=accept_ui() && isfinite(last) && last>=1 && last<=PORTABLE_CAPTURE_LIMIT && floor(last)==last &&
        portable_native_config(ids,ids_length,hash,digest_length,(unsigned)last);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_portable_ready(lua_State *L) {
    int ready=accept_ui() && portable_native_configured && !portable_native_failures &&
        stats.active && boot_mode && !boot_phase && !stats.issued && world_identity_installed &&
        *(uint64_t*)(image+WORD_RVA)==patched_word &&
        *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)==world_identity_patched_word;
    p_lua_pushnumber(L,ready);return 1;
}
static int lua_portable_transport(lua_State *L) {
    size_t length=0;const char *agent=p_lua_tolstring(L,1,&length);
    double startup=p_lua_tonumber(L,2);
    p_lua_pushnumber(L,accept_ui() && isfinite(startup) && startup>=1000 && startup<=900000 && floor(startup)==startup &&
        portable_transport_config(agent,length,(unsigned)startup));return 1;
}
static int lua_portable_cancel(lua_State *L) {
    if(!accept_ui() || !boot_mode || !used) {p_lua_pushnumber(L,0);return 1;}
    finish(78);
    int restored=!stats.active && stats.restored && dt_restored && !dt_installed && !installed &&
        !world_identity_installed && connection==INVALID_SOCKET && !portable_wire_draining;
    p_lua_pushnumber(L,restored);return 1;
}
static int lua_portable_status(lua_State *L) {
    /* A failed frame0 may never adopt the loaded UI thread (issued is still
     * zero). Post-stop diagnostics read only our own state and must neither
     * adopt that thread nor grant it control/export authority. */
    AcquireSRWLockShared(&portable_wire_lock);
    unsigned active=stats.active,reason=stats.reason;
    uint64_t issued=stats.issued,completed=stats.completed;
    int stopped=boot_mode && used && portable_wire_enabled && portable_wire_finish_started && !active;
    ReleaseSRWLockShared(&portable_wire_lock);
    if(!stopped && (active?!accept_ui():GetCurrentThreadId()!=ui_thread)) {p_lua_pushnil(L);return 1;}
    char text[640];int length=snprintf(text,sizeof(text),
        "schema=%s configured=%u frames=%ld last=%u calls=%lld returns=%lld failures=%lld saved=%016llx stage=%s wire=%u draining=%u closed=%u terminal_restored=%u wire_stage=%s active=%u reason=%u issued=%llu completed=%llu",
        PORTABLE_SCHEMA_SHA,portable_native_configured,(long)InterlockedCompareExchange(&portable_native_count,0,0),
        portable_native_limit,(long long)InterlockedCompareExchange64(&portable_native_calls,0,0),
        (long long)InterlockedCompareExchange64(&portable_native_returns,0,0),
        (long long)InterlockedCompareExchange64(&portable_native_failures,0,0),
        (unsigned long long)portable_native_failure_id,portable_native_stage,portable_wire_enabled,portable_wire_draining,
        portable_wire_closed,portable_wire_terminal_restored,portable_wire_stage,active,reason,
        (unsigned long long)issued,(unsigned long long)completed);
    if(length<0 || (size_t)length>=sizeof(text)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,text,(size_t)length);return 1;
}
static int lua_portable_frame(lua_State *L) {
    double value=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(value) || value<0 || value>portable_native_limit || floor(value)!=value ||
       value>=(double)InterlockedCompareExchange(&portable_native_count,0,0)) {p_lua_pushnil(L);return 1;}
    unsigned tick=(unsigned)value,length=portable_native_lengths[tick];char text[PORTABLE_FRAME_CAPACITY*2];
    if(!length || length>PORTABLE_FRAME_CAPACITY) {p_lua_pushnil(L);return 1;}
    const char *hex="0123456789abcdef";
    for(unsigned i=0;i<length;i++) {unsigned char byte=portable_native_bytes[tick][i];text[i*2]=hex[byte>>4];text[i*2+1]=hex[byte&15];}
    p_lua_pushlstring(L,text,(size_t)length*2);return 1;
}
