/* Shared Lua wire API (gate.lookahead/lookahead_status/wire/propose/report). */
/* Protocol 3: the proposal blob is sealed by the tick's report exactly like
 * the protocol 2 command. nil uses the blob gate.propose staged for this tick
 * (or the empty blob); an explicit blob must agree with a staged one. */
static int report_blob(uint64_t tick,const char *hash,const char *blob,size_t length) {
    int staged=blob_staged_set && blob_staged_tick==tick;
    if(!blob) {blob=staged?blob_staged:"";length=strlen(blob);}
    else if(!gate_blob_valid(blob,length) || (staged && strcmp(blob,blob_staged))) return 0;
    if(stats.report_tick==tick && strcmp(blob,blob_proposal)) return 0;
    int ok=x4gate_report(tick,hash);
    if(ok) memcpy(blob_proposal,blob,length+1);
    return ok;
}
/* gate.lookahead(k): protocol 4 permit window, see x4gate_lookahead. */
static int lua_lookahead(lua_State *L) {
    double k=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(k) && floor(k)==k && k>=0 && k<=GATE_LOOKAHEAD_MAX && p_lua_rawseti && x4gate_lookahead((unsigned)k));
    return 1;
}
static int lua_lookahead_status(lua_State *L) {
    char out[192];int n=lookahead_status(out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_wire(lua_State *L) {
    double version=p_lua_tonumber(L,1),clients=p_lua_tonumber(L,2);int ok=0;
    if(isfinite(version) && isfinite(clients) && floor(version)==version && floor(clients)==clients &&
       version>=2 && version<=3 && clients>=2 && clients<=GATE_CLIENTS_MAX && (version==2 || p_lua_rawseti))
        ok=x4gate_wire((unsigned)version,(unsigned)clients);
    p_lua_pushnumber(L,ok);return 1;
}
/* gate.propose(tick,blob): stage this client's protocol 3 proposal for the
 * current tick before its report; after the report only the sealed blob passes. */
static int lua_propose(lua_State *L) {
    size_t length=0;double tick=p_lua_tonumber(L,1);const char *blob=p_lua_tolstring(L,2,&length);int ok=0;
    if(wire_version==3 && stats.active && accept_ui() && isfinite(tick) && tick>=0 && floor(tick)==tick &&
       (uint64_t)tick==stats.issued && gate_blob_valid(blob,length)) {
        if(stats.report_tick==stats.issued) ok=!strcmp(blob,blob_proposal);
        else {memcpy(blob_staged,blob,length+1);blob_staged_tick=stats.issued;blob_staged_set=1;ok=1;}
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_report(lua_State *L) {
    size_t command_length=0;
    const char *command=p_lua_tolstring(L,3,&command_length);
    if(wire_version==3) {p_lua_pushnumber(L,report_blob((uint64_t)p_lua_tonumber(L,1),p_lua_tolstring(L,2,NULL),command,command_length));return 1;}
    if(!command) command="00000000";
    else if(command_length!=8) {p_lua_pushnumber(L,0);return 1;}
    uint64_t tick=(uint64_t)p_lua_tonumber(L,1);
    if(!gate_command_valid(command)||(stats.report_tick==tick&&strcmp(command,proposal))) {p_lua_pushnumber(L,0);return 1;}
    int ok=x4gate_report(tick,p_lua_tolstring(L,2,NULL));
    if(ok) memcpy(proposal,command,9);
    p_lua_pushnumber(L,ok);return 1;
}
