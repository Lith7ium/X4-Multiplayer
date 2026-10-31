/* The gate C API shared by both platforms; GATE_EXPORT is the platform export
 * attribute. Needs start_gate, finish, accept_ui and the session state. */
GATE_EXPORT int x4gate_start(unsigned slot,unsigned port,const char *token,unsigned timeout,const char *initial_digest) {
    return start_gate(slot,port,token,timeout,initial_digest,0);
}
/* Before start/boot only; a reloaded UI may repeat the same configuration. */
GATE_EXPORT int x4gate_wire(unsigned version,unsigned clients) {
    int valid=(version==2 && clients==2) || (version==3 && clients>=2 && clients<=GATE_CLIENTS_MAX);
    if(used) return valid && version==wire_version && clients==wire_clients;
    if(!valid) return 0;
    if(version!=3) wire_lookahead=0;
    wire_version=version;wire_clients=clients;return 1;
}
/* Protocol 4 window k (0 keeps protocol 3): after x4gate_wire(3,N), before
 * start/boot; once started, only the identical window returns 1 (UI reloads). */
GATE_EXPORT int x4gate_lookahead(unsigned k) {
    int valid=wire_version==3 && k<=GATE_LOOKAHEAD_MAX;
    if(used) return valid && k==wire_lookahead;
    if(!valid) return 0;
    wire_lookahead=k;return 1;
}
GATE_EXPORT int x4gate_report(uint64_t tick,const char *hash) {
    if(!stats.active||!accept_ui()||tick!=stats.issued||!valid_hex(hash,64)) return 0;
    if(stats.report_tick==tick) return !strcmp(hash,digest);
    strcpy(digest,hash);stats.report_tick=tick;return 1;
}
GATE_EXPORT int x4gate_poll(gate_stats *out) {
    if(!out||!accept_ui()) return 0;*out=stats;return 1;
}
GATE_EXPORT int x4gate_stop(void) {
    if(GetCurrentThreadId()!=ui_thread) return 0;finish(1);return stats.restored!=0;
}
