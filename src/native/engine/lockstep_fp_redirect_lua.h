/* The Lua methods of native_gate_fp_redirect (lockstep_fp_redirect.h), the same on both platforms; included by
 * lockstep_fp_redirect_win.h and linux/gate_fp_redirect.h after their fr_arm. */
/* fp_redirect(mode, ticks): 'shadow' or 'redirect'; FP_REDIRECT_ROWs in ticks 1..ticks (default 3, at most 600). */
static int lua_fp_redirect(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double ticks=p_lua_tonumber(L,2);
    if(ticks==0) ticks=3;
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=600 && floor(ticks)==ticks && fr_arm(fr_lua_mode(mode,length),(uint64_t)ticks));return 1;
}
/* fp_redirect_watch(id): a saved ID (16 hex digits) whose calls give rows; before fp_redirect. */
static int lua_fp_redirect_watch(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];ok=(c>='0' && c<='9') || (c>='a' && c<='f');
        id=id*16+(uint64_t)(c<='9'?c-'0':c-'a'+10);
    }
    p_lua_pushnumber(L,ok && fr_watch(id));return 1;
}
static int lua_fp_redirect_status(lua_State *L) {
    char out[2048];int n=accept_ui()?fr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_fp_redirect_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[2048];int n=0; /* a params row with its writer fields: about 1,100 */
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=fr_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
