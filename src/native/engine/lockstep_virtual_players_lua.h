/* Lua surface of the virtual players module (lockstep_virtual_players.h). */
/* gate.virtual_players(mode[, tick]): 'observe' (read-only, step 1) or
 * 'construct' with the admitted tick of the step 3 spike (2..18000). */
static int lua_virtual_players(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double tick=p_lua_tonumber(L,2);int ok=0;
    if(mode && length==7 && !memcmp(mode,"observe",7)) ok=!vps_mode && vpl_arm();
    else if(mode && length==9 && !memcmp(mode,"construct",9)) ok=isfinite(tick) && floor(tick)==tick && vps_arm((uint64_t)tick);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_virtual_players_status(lua_State *L) {
    char out[1024];int n=accept_ui()?vpl_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* name:class:changes:first:last for every region of the block. */
static int lua_virtual_players_regions(lua_State *L) {
    char out[4096];int n=accept_ui() && vpl_armed?vpl_regions_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Guard-page bytes outside the declared regions that changed: rva+length,... */
static int lua_virtual_players_undeclared(lua_State *L) {
    char out[2048];int n=accept_ui() && vpl_armed?vpl_undeclared_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Neighbour globals on the guard pages: name:passes with a change,... */
static int lua_virtual_players_neighbours(lua_State *L) {
    char out[1024];int n=accept_ui() && vpl_armed?vpl_neighbours_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* The InputMapping census (nil until the first observing pass took it). */
static int lua_virtual_players_map(lua_State *L) {
    char out[4096];int n=accept_ui() && vpl_armed && vpl_census_done?vpl_census_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* name:fnv64 per region of the last pass (nil before the first observing pass). */
static int lua_virtual_players_hashes(lua_State *L) {
    char out[2048];int n=accept_ui() && vpl_armed?vpl_hashes_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.virtual_players_raw(class): 'config' or 'option' bytes of the last pass as hex. */
static int lua_virtual_players_raw(lua_State *L) {
    size_t length=0;const char *name=p_lua_tolstring(L,1,&length);char out[1024];int n=0;
    unsigned cls=name && length==6 && !memcmp(name,"config",6)?VPL_CONFIG:name && length==6 && !memcmp(name,"option",6)?VPL_OPTION:0;
    if(cls && accept_ui() && vpl_armed) n=vpl_raw_text(cls,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
