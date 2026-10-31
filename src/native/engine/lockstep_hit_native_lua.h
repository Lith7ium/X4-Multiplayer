/* Lua surface of the hit claims (lockstep_hit_native.h) and of the input blob
 * container (lockstep_hit_claims.h), which the combined input adapter uses so
 * that the container format has one implementation.
 * gate.hit_claims(policy): 'shooter' | 'victim' | 'first'; after
 *   gate.pilot_inputs (wire), before loading; identical reruns succeed.
 * gate.hit_claims_admit(slot, hex): stage the hits section admitted for slot
 *   this tick (every client, every slot, slot order).
 * gate.hit_claims_resolve(tick): resolve and apply the claims due this tick;
 *   returns the HITS_1 receipt for the declared state.
 * gate.hit_claims_proposal(tick, budget): this client's hits section for this
 *   tick within budget hex characters (the multiplexer's offer; default the
 *   whole blob) or nil; claims that do not fit stay queued.
 * gate.input_mux(pilot_hex, hits_hex, orders_hex): one canonical blob (hex;
 *   '' or nil for an absent section); nil and a reason when it cannot be
 *   encoded. gate.input_split(blob_hex): pilot_hex, hits_hex, orders_hex
 *   ('' for absent) or nil and the refusal reason. */
static int hitn_lua_hex(lua_State *L,int index,unsigned char *out,size_t capacity,size_t *length) {
    size_t n=0;const char *text=p_lua_tolstring(L,index,&n);
    *length=0;
    if(!text || !n) return 1;
    if(n&1 || n/2>capacity) return 0;
    for(size_t i=0;i<n;i++) {
        char c=text[i];unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;
        if(d>15) return 0;
        if(i&1) out[i/2]=(unsigned char)(out[i/2]|d);else out[i/2]=(unsigned char)(d<<4);
    }
    *length=n/2;return 1;
}
static void hitn_lua_push_hex(lua_State *L,const unsigned char *bytes,size_t length) {
    static const char digits[]="0123456789abcdef";char text[2*INPUT_MUX_MAX+2];
    if(length>INPUT_MUX_MAX) length=INPUT_MUX_MAX;
    for(size_t i=0;i<length;i++) {text[2*i]=digits[bytes[i]>>4];text[2*i+1]=digits[bytes[i]&15];}
    p_lua_pushlstring(L,text,2*length);
}
static int lua_hit_claims(lua_State *L) {
    size_t length=0;const char *policy=p_lua_tolstring(L,1,&length);
    int kind=policy && !strcmp(policy,"shooter")?HIT_POLICY_SHOOTER:policy && !strcmp(policy,"victim")?HIT_POLICY_VICTIM:
        policy && !strcmp(policy,"first")?HIT_POLICY_FIRST:0;
    p_lua_pushnumber(L,kind && accept_ui() && pilot_source==PILOT_SOURCE_WIRE && hitn_arm(kind));return 1;
}
static int lua_hit_claims_admit(lua_State *L) {
    double slot=p_lua_tonumber(L,1);unsigned char payload[INPUT_MUX_MAX];size_t length=0;
    int ok=accept_ui() && stats.active && boot_phase==3 && isfinite(slot) && floor(slot)==slot && slot>=1 && slot<=PILOT_SLOTS &&
        hitn_lua_hex(L,2,payload,sizeof(payload),&length) && length && hitn_admit((unsigned)slot,payload,(unsigned)length,stats.issued);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_hit_claims_resolve(lua_State *L) {
    double tick=p_lua_tonumber(L,1);char out[4096];int n=0;
    if(accept_ui() && stats.active && boot_phase==3 && isfinite(tick) && floor(tick)==tick && tick==(double)stats.issued)
        n=hitn_resolve((uint64_t)tick,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_hit_claims_proposal(lua_State *L) {
    double tick=p_lua_tonumber(L,1);unsigned char payload[INPUT_MUX_MAX];size_t length=0,capacity=sizeof(payload),given=0;
    if(p_lua_tolstring(L,2,&given)) { /* a budget was passed (nil: the whole blob) */
        double budget=p_lua_tonumber(L,2);
        if(!isfinite(budget) || budget<0 || floor(budget)!=budget) {p_lua_pushnil(L);return 1;}
        if(budget/2<(double)capacity) capacity=(size_t)budget/2;
    }
    if(accept_ui() && stats.active && isfinite(tick) && floor(tick)==tick && tick==(double)stats.issued &&
       hitn_proposal((uint64_t)tick,payload,capacity,&length) && length) hitn_lua_push_hex(L,payload,length);
    else p_lua_pushnil(L);
    return 1;
}
static int lua_hit_claims_status(lua_State *L) {
    char out[2048];int n=accept_ui()?hitn_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pilot_fire(mode[, slots]): slots, a bit mask, limits 'script' to those slots (a test's shooter and target);
 * gate.pilot_fire(mode): 'record' (the trigger bit) or 'script' (fixed bursts);
 * after gate.pilot_inputs, before loading. gate.pilot_fire_frame(): the
 * PFIRE_1 receipt of this tick (slot 6). gate.pilot_fire_status(). */
static int lua_pilot_fire(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int kind=mode && !strcmp(mode,"record")?PFIRE_RECORD:mode && !strcmp(mode,"script")?PFIRE_SCRIPT:PFIRE_OFF;
    double slots=p_lua_tonumber(L,2);   /* nil reads 0: every slot */
    int ok=kind && isfinite(slots) && slots>=0 && slots<(1u<<PILOT_SLOTS) && floor(slots)==slots && accept_ui() && pfire_arm(kind);
    if(ok) pfire_script_mask=(unsigned)slots;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_pilot_fire_frame(lua_State *L) {
    char out[96];int n=accept_ui() && pfire_armed && stats.active?pfire_frame_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_fire_guns(lua_State *L) {
    char out[1024];int n=accept_ui() && pfire_armed?pfire_guns_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pilot_fire_status(lua_State *L) {
    char out[1024];int n=accept_ui()?pfire_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_input_mux(lua_State *L) {
    unsigned char parts[3][INPUT_MUX_MAX],blob[INPUT_MUX_MAX];size_t lengths[3];input_sections in;
    for(int i=0;i<3;i++) if(!hitn_lua_hex(L,i+1,parts[i],sizeof(parts[i]),&lengths[i])) {p_lua_pushnil(L);p_lua_pushlstring(L,"encoding",8);return 2;}
    memset(&in,0,sizeof(in));
    in.pilot=parts[0];in.pilot_len=(unsigned)lengths[0];in.hits=parts[1];in.hits_len=(unsigned)lengths[1];
    in.orders=parts[2];in.orders_len=(unsigned)lengths[2];
    if(!in.pilot_len && !in.hits_len && !in.orders_len) {p_lua_pushlstring(L,"",0);return 1;}
    size_t n=input_mux_encode(&in,blob,sizeof(blob));
    if(!n) {p_lua_pushnil(L);p_lua_pushlstring(L,"too_long",8);return 2;}
    hitn_lua_push_hex(L,blob,n);return 1;
}
static int lua_input_split(lua_State *L) {
    unsigned char blob[INPUT_MUX_MAX+1];size_t length=0;input_sections out;const char *why="encoding";
    if(!hitn_lua_hex(L,1,blob,sizeof(blob),&length) || !input_mux_decode(blob,length,&out,&why)) {
        p_lua_pushnil(L);p_lua_pushlstring(L,why,strlen(why));return 2;
    }
    hitn_lua_push_hex(L,out.pilot,out.pilot_len);hitn_lua_push_hex(L,out.hits,out.hits_len);hitn_lua_push_hex(L,out.orders,out.orders_len);
    return 3;
}
