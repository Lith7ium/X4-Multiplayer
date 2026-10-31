/* Exercise the actual Lua boot entry through the owned-session reload path.
 * This validates merged high option bits without installing engine hooks. */
static double option_input,option_result;
static unsigned option_pushes;
static const char *option_token="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char *option_hash="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static double option_number(lua_State *state,int index) {
    (void)state;
    if(index==1)return 1;
    if(index==2)return 12345;
    assert(index==6);return option_input;
}
static const char *option_string(lua_State *state,int index,size_t *length) {
    (void)state;assert(!length);
    assert(index==3 || index==5);return index==3?option_token:option_hash;
}
static void option_push(lua_State *state,double value) {
    (void)state;option_result=value;++option_pushes;
}
static void option_check(double input,uint64_t owned,int expected) {
    option_input=input;option_result=-1;option_pushes=0;boot_options=owned;
    gate_stats before=stats;
    assert(lua_boot(NULL)==1 && option_pushes==1 && option_result==expected);
    assert(!memcmp(&before,&stats,sizeof(stats)) && !installed && used && boot_mode);
    assert(boot_options==owned); /* Rejection cannot alter the existing session. */
}
static void test_boot_option_composition(void) {
    assert(!used && !installed && !stats.active && !boot_mode);
    gate_stats before=stats;uint64_t old_options=boot_options;unsigned old_port=remote_port;
    char old_secret[sizeof(secret)],old_digest[sizeof(initial_state_digest)];
    memcpy(old_secret,secret,sizeof(secret));memcpy(old_digest,initial_state_digest,sizeof(initial_state_digest));
    double (*old_number)(lua_State *,int)=p_lua_tonumber;
    const char *(*old_string)(lua_State *,int,size_t *)=p_lua_tolstring;
    void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    p_lua_tonumber=option_number;p_lua_tolstring=option_string;p_lua_pushnumber=option_push;
    used=boot_mode=1;stats.active=1;stats.slot=1;remote_port=12345;
    strcpy(secret,option_token);strcpy(initial_state_digest,option_hash);
    uint64_t solver=(1ULL<<50)|(1ULL<<46)|4|1;
    uint64_t npc=(1ULL<<48)|(1ULL<<46)|(1ULL<<41)|32;
    option_check(0,0,0);option_check((double)solver,solver,0);
    for(unsigned choice=0;choice<4;choice++) {
        uint64_t combined=solver|npc|((choice&1)?1ULL<<47:0)|((choice&2)?1ULL<<49:0);
        option_check((double)combined,combined,0);
        option_check((double)(combined|REGISTRY_MD_BIT),combined|REGISTRY_MD_BIT,0);
        uint64_t with_request=combined|REGISTRY_MD_BIT|REQUEST_OPTION|128;
        option_check((double)with_request,with_request,0);
        option_check((double)(with_request&~SOLVER_OPTION),with_request&~SOLVER_OPTION,10);
        option_check((double)(with_request&~896ULL),with_request&~896ULL,10);
        option_check((double)(combined&~(1ULL<<46)),combined&~(1ULL<<46),10);
        option_check((double)(combined&~(1ULL<<41)),combined&~(1ULL<<41),10);
        option_check((double)(combined&~1ULL),combined&~1ULL,10);
    }
    uint64_t all=(1ULL<<53)-1;option_check((double)all,all,0);
    option_check((double)(1ULL<<53),1ULL<<53,10);
    uint64_t md=REGISTRY_MD_BIT|(1ULL<<46)|(1ULL<<41)|32|1;
    option_check((double)md,md,0);
    option_check((double)(md&~32ULL),md&~32ULL,10);
    option_check((double)(md&~1ULL),md&~1ULL,10);
    option_check((double)(md&~(1ULL<<46)),md&~(1ULL<<46),10);
    option_check((double)(md&~(1ULL<<41)),md&~(1ULL<<41),10);
    option_check((double)(1ULL<<52),1ULL<<52,10);
    option_check((double)(solver&~4ULL),solver&~4ULL,10);
    option_check((double)(solver|(1ULL<<47)),solver|(1ULL<<47),10);
    option_check((double)(solver|(1ULL<<49)),solver|(1ULL<<49),10);
    option_check((double)solver,solver|npc,10); /* A reload cannot change the mask. */
    option_check(-1,0,10);option_check(0.5,0,10);
    option_check(NAN,0,10);option_check(INFINITY,0,10);
    p_lua_tonumber=old_number;p_lua_tolstring=old_string;p_lua_pushnumber=old_push;
    stats=before;boot_options=old_options;remote_port=old_port;used=boot_mode=0;
    memcpy(secret,old_secret,sizeof(secret));memcpy(initial_state_digest,old_digest,sizeof(initial_state_digest));
}
