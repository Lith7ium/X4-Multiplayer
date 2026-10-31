/* Shared Lua helpers: gate.qpc_us, gate.sha256 (digest_hex per platform), gate.frame_timing, gate.tick_rate. */
/* gate.tick_rate(hz, pace): native_gate_tick_hz, before gate.boot (lockstep_tick.h). */
static int lua_tick_rate(lua_State *L) {
    p_lua_pushnumber(L,gate_tick_configure(p_lua_tonumber(L,1),p_lua_tonumber(L,2),used));return 1;
}
static int lua_qpc_us(lua_State *L) {
    LARGE_INTEGER now,f;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&f);
    p_lua_pushnumber(L,f.QuadPart?(double)now.QuadPart*1e6/(double)f.QuadPart:0);return 1;
}
static int lua_sha256(lua_State *L) {
    size_t length=0;const char *data=p_lua_tolstring(L,1,&length);char out[65];
    if(data && digest_hex(data,length,out)) p_lua_pushlstring(L,out,64);else p_lua_pushnil(L);return 1;
}
static int lua_frame_timing(lua_State *L) {
    char out[352];int n=accept_ui()?frame_timing_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
