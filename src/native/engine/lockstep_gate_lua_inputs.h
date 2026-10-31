/* Shared poll inputs (s.wire_version, s.clients, s.input1/2, s.inputs, s.lookahead). */
static void push_inputs(lua_State *L) {
    p_lua_pushnumber(L,wire_version);p_lua_setfield(L,-2,"wire_version");
    p_lua_pushnumber(L,wire_clients);p_lua_setfield(L,-2,"clients");
    if(wire_version==2) {
        p_lua_pushlstring(L,inputs[0],8);p_lua_setfield(L,-2,"input1");
        p_lua_pushlstring(L,inputs[1],8);p_lua_setfield(L,-2,"input2");
    }
    if(!p_lua_rawseti) return;
    p_lua_createtable(L,(int)wire_clients,0);
    for(unsigned i=0;i<wire_clients;i++) {
        const char *value=wire_version==3?blob_inputs[i]:inputs[i];
        p_lua_pushlstring(L,value,strlen(value));p_lua_rawseti(L,-2,(int)i+1);
    }
    p_lua_setfield(L,-2,"inputs");
    /* Protocol 4: s.inputs are the proposals sealed with tick issued-1-k
     * (s.inputs_tick; absent, and every blob empty, for issued <= k). */
    if(!wire_lookahead) return;
    p_lua_pushnumber(L,wire_lookahead);p_lua_setfield(L,-2,"lookahead");
    if(stats.issued>wire_lookahead) {p_lua_pushnumber(L,(double)(stats.issued-1-wire_lookahead));p_lua_setfield(L,-2,"inputs_tick");}
}
