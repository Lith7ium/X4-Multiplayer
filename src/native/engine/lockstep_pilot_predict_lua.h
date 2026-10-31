/* Lua surface of the default-off presentation prediction (lockstep_pilot_predict.h).
 * gate.pilot_predict(mode, horizon, tau_ms): mode 'observe'; horizon 0 = the
 *   wire delay (wire source) or 1..30 (script source); tau_ms 0..2000 display
 *   smoothing. After gate.pilot_inputs, before the first admitted tick; an
 *   identical rerun after a loading-UI reload succeeds.
 * gate.pilot_predict_frame(tick): one prediction step (UI thread, slot 6, after
 *   the pilot frame); returns the per-tick row or nil and a reason.
 * gate.pilot_predict_status(): totals, error statistics and the fitted model.
 * gate.pilot_predict_raw(slot): this tick's raw sample of one predicted slot. */
static int lua_pilot_predict(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double horizon=p_lua_tonumber(L,2),tau=p_lua_tonumber(L,3);
    int kind=mode && !strcmp(mode,"observe")?PREDICT_OBSERVE:mode && !strcmp(mode,"present")?PREDICT_PRESENT:PREDICT_OFF;
    int ok=kind && accept_ui() && isfinite(horizon) && floor(horizon)==horizon && horizon>=0 && horizon<=PREDICT_HORIZON_MAX &&
        isfinite(tau) && floor(tau)==tau && tau>=0 && tau<=PREDICT_TAU_MAX_MS &&
        (predict_armed || !stats.issued) && predict_arm(kind,(unsigned)horizon,(unsigned)tau);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_pilot_predict_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);char out[512];const char *why="ui";int n=0;
    if(accept_ui() && stats.active && boot_phase==3 && isfinite(tick) && floor(tick)==tick && tick>=1 && tick<9007199254740992.0) {
        why="frame";
        if(predict_frame((uint64_t)tick)) n=predict_row(out,sizeof(out));
    }
    if(n) {p_lua_pushlstring(L,out,(size_t)n);return 1;}
    p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
}
static int lua_pilot_predict_status(lua_State *L) {
    char out[8192];int n=accept_ui() && predict_armed?predict_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_predict_raw(lua_State *L) {
    double slot=p_lua_tonumber(L,1);char out[768];
    int n=accept_ui() && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS?predict_raw((unsigned)slot,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
