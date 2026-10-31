/* X4 deliberately removes ffi.load. Use the same Lua 5.1 module ABI as the
 * existing fixedtick module, resolving symbols from X4's own loaded runtime. */
typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *);
#define API(ret,name,args) static ret (*p_##name) args
API(double,lua_tonumber,(lua_State *,int));
API(const char *,lua_tolstring,(lua_State *,int,size_t *));
API(void,lua_pushnumber,(lua_State *,double));
API(void,lua_pushnil,(lua_State *));
API(void,lua_pushlstring,(lua_State *,const char *,size_t));
API(void,lua_createtable,(lua_State *,int,int));
API(void,lua_setfield,(lua_State *,int,const char *));
API(void,lua_pushcclosure,(lua_State *,lua_CFunction,int));
API(void,lua_rawseti,(lua_State *,int,int)); /* optional: protocol 3 needs it for s.inputs */
/* relay_host("a.b.c.d"): opt-in remote relay, set only before the gate starts. Once
 * started, only the same address returns 1: the probe repeats its configuration
 * after the snapshot load reloads the UI (like gate.wire and gate.lookahead). */
static int lua_relay_host(lua_State *L) {
    uint32_t address;int ok=relay_host_parse(p_lua_tolstring(L,1,NULL),&address);
    if(ok && used) ok=address==relay_address;
    else if(ok) relay_address=address;
    p_lua_pushnumber(L,ok);return 1;
}
