/* Lua surface of the UI replay (lockstep_ui_replay.h, tools/dev/native_gate_ui_replay_core.lua). */
/* gate.ui_replay_pins(): every pin table first failure named (1 or 0,why). The probe calls it where the other
 * pins are checked, before modules patch X4; gate.ui_replay answers from the cached check. */
static int lua_ui_replay_pins(lua_State *L) {
    char why[32];
    int ok=accept_ui() && uirep_pins();
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    snprintf(why,sizeof(why),"pins_ui_replay:%x",pin_failed_rva);p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_replay(families): bit mask 1 travel, 2 target, 4 orders. After gate.pilot_stock and gate.pilot_present,
 * before the first admitted tick; an identical rerun after a reload answers 1. 1, or 0 and the reason. */
static int lua_ui_replay(lua_State *L) {
    double families=p_lua_tonumber(L,1);
    uirep_refuse("arguments");
    int ok=isfinite(families) && floor(families)==families && families>=1 && families<=UIREP_FAMILIES && accept_ui() && uirep_arm((unsigned)families);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    p_lua_pushlstring(L,uirep_arm_why,strlen(uirep_arm_why));return 2;
}
/* gate.ui_replay_take(tick, budget bytes): this tick's section from the queue (hex), or nil when none fits. */
static int lua_ui_replay_take(lua_State *L) {
    double tick=p_lua_tonumber(L,1),budget=p_lua_tonumber(L,2);unsigned char section[UIREP_SECTION_MAX];char hex[2*UIREP_SECTION_MAX+1];
    unsigned n=accept_ui() && uirep_armed && isfinite(tick) && tick>=1 && floor(tick)==tick && isfinite(budget) && budget>=0 && budget<=255?
        uirep_take((uint64_t)tick,(unsigned)budget,section):0;
    if(!n) {p_lua_pushnil(L);return 1;}
    pilot_hex(section,n,hex);p_lua_pushlstring(L,hex,2*n);return 1;
}
/* gate.ui_replay_stage(slot, hex): stage an admitted section (every client). 1, or 0 and why; a refusal stops. */
static int lua_ui_replay_stage(lua_State *L) {
    double slot=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char section[UIREP_SECTION_MAX];unsigned n=0;const char *why="ui_section";
    int ok=accept_ui() && uirep_armed && stats.active && boot_phase==3 && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS &&
        pstk_unhex(text,length,section,sizeof(section),&n) && uirep_stage((unsigned)slot,section,n,stats.issued,&why);
    p_lua_pushnumber(L,ok);
    if(ok) return 1;
    if(uirep_armed) uirep_async_fail(why);
    p_lua_pushlstring(L,why,strlen(why));return 2;
}
/* gate.ui_replay_push(op, hex): an event Lua captured (a wrapped Lua C function); Lua runs it again at apply. */
static int lua_ui_replay_push(lua_State *L) {
    double op=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;
    if(!accept_ui() || !uirep_armed || op!=UIREP_OP_LUA_CALL) {p_lua_pushnumber(L,0);return 1;}
    if(length>2*UIREP_EVENT_MAX) {uirep_note_refusal(uirep_op_name((unsigned)op),"too_long");p_lua_pushnumber(L,0);return 1;}
    if(!pstk_unhex(text,length,data,sizeof(data),&n)) {uirep_note_refusal(uirep_op_name((unsigned)op),"encoding");p_lua_pushnumber(L,0);return 1;}
    p_lua_pushnumber(L,uirep_push((unsigned)op,data,n));return 1;
}
/* gate.ui_replay_ref(decimal runtime ID): its reference (hex), nil when it names no component. */
static int lua_ui_replay_ref(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);unsigned char ref[UIREP_REF];char hex[2*UIREP_REF+1];
    uint64_t runtime=text && length && length<21?strtoull(text,NULL,10):0;
    if(!accept_ui() || !uirep_armed || !runtime || !uirep_component(runtime)) {p_lua_pushnil(L);return 1;}
    uirep_put_ref(ref,runtime);pilot_hex(ref,UIREP_REF,hex);p_lua_pushlstring(L,hex,2*UIREP_REF);return 1;
}
/* gate.ui_replay_resolve(hex): the decimal runtime ID, nil,'gone' (the same everywhere) or nil,'mismatch' (stops). */
static int lua_ui_replay_resolve(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);unsigned char ref[UIREP_REF];unsigned n=0;uint64_t runtime=0;unsigned char *component=NULL;
    if(!accept_ui() || !uirep_armed || !pstk_unhex(text,length,ref,sizeof(ref),&n) || n!=UIREP_REF) {p_lua_pushnil(L);p_lua_pushlstring(L,"encoding",8);return 2;}
    int resolved=uirep_resolve(ref,&runtime,&component);
    if(resolved<0) {uirep_async_fail("ui_ref_mismatch");p_lua_pushnil(L);p_lua_pushlstring(L,"mismatch",8);return 2;}
    if(!resolved) {InterlockedIncrement64(&uirep_stale);p_lua_pushnil(L);p_lua_pushlstring(L,"gone",4);return 2;}
    char decimal[24];int m=snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)runtime);
    p_lua_pushlstring(L,decimal,(size_t)m);return 1;
}
/* gate.ui_replay_lua(tick): the Lua-run events due now, "slot:op:hex;...", nil when none. */
static int lua_ui_replay_lua(lua_State *L) {
    static char out[PILOT_SLOTS*UIREP_EVENTS_MAX*32+4096];double tick=p_lua_tonumber(L,1);
    int n=accept_ui() && uirep_armed && isfinite(tick) && tick>=1 && floor(tick)==tick?uirep_lua_events((uint64_t)tick,out,sizeof(out)):0;
    if(n<0) {uirep_async_fail("ui_lua_overflow");n=0;}
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_replay_bypass(1|0): the probe's own calls of stock functions at apply reach them unchanged. */
static int lua_ui_replay_bypass(lua_State *L) {
    double on=p_lua_tonumber(L,1);
    if(accept_ui() && uirep_armed) InterlockedExchange(&uirep_replaying,on==1);
    return 0;
}
/* gate.ui_replay_result(tick, ok): what X4 made of a Lua-run event; joins the receipt. */
static int lua_ui_replay_result(lua_State *L) {
    double tick=p_lua_tonumber(L,1),ok=p_lua_tonumber(L,2);
    if(accept_ui() && uirep_armed && isfinite(tick) && tick>=1) uirep_result((uint64_t)tick,ok==1);
    return 0;
}
static int lua_ui_replay_frame(lua_State *L) {
    char out[192];int n=accept_ui() && uirep_armed?uirep_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_replay_status(lua_State *L) {
    char out[640];int n=accept_ui() && uirep_armed?uirep_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_replay_refusals(): "tick:what:why,..." since the last call, nil when none. */
static int lua_ui_replay_refusals(lua_State *L) {
    char out[2048];int n=accept_ui() && uirep_armed?uirep_refusals_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.ui_replay_fail(why): Lua found a section it cannot apply; the next pass stops with 164. */
static int lua_ui_replay_fail(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);static char why[64];
    if(accept_ui() && uirep_armed) {
        snprintf(why,sizeof(why),"%.*s",(int)(length<sizeof(why)-1?length:sizeof(why)-1),text?text:"ui_lua");
        uirep_async_fail(why);
    }
    return 0;
}
/* gate.ui_replay_saved(16 hex): the decimal runtime ID of a saved object, nil when not exactly one (scripted runs). */
static int lua_ui_replay_saved(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t saved=text && length==16?strtoull(text,NULL,16):0,runtime=0;
    if(accept_ui() && uirep_armed && saved) runtime=pilot_reverse(saved);
    if(!runtime) {p_lua_pushnil(L);return 1;}
    char decimal[24];int m=snprintf(decimal,sizeof(decimal),"%llu",(unsigned long long)runtime);
    p_lua_pushlstring(L,decimal,(size_t)m);return 1;
}
