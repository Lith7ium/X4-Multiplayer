/* Native singleton controls, not a second player implementation. No hooks.
 * The exact native setter is the same one used by input dispatch. Its throttle
 * state is process-global; expose only the existing p1 context, fail closed on
 * transfer, and never use two calls as evidence of two independently owned ships.
 */
static int player_pins_checked;
static uint64_t player_action_tick;
typedef void (*player_value_fn)(void *,unsigned,float);
static int player_pins(void) {
    if(player_pins_checked) return 1;
    if(!hash_ok(image+0xd8fb60,3271,"56a5a8448725204bc198db77afdeceb0bde31896453191eb9e09b65acbf0dab9") ||
       !hash_ok(image+0xd95180,7221,"c55c35cea23632e2ab86e60c68f3222b7cab0160a788e662d67cde1309e63d33") ||
       !hash_ok(image+0xd8a680,3416,"b0b97f49d47dc07a8093fce44c74184f39020b66a00b67ea8c26953db9a32f6d") ||
       !hash_ok(image+0x9fc550,277,"fca3a29a2c0ca40fafa27dac44da67e9b8bd0aa52b3d812c72b72f4ed212ca67")) return 0;
    player_pins_checked=1;return 1;
}
static void *player_context(uint64_t *saved) {
    if(!stats.active || !boot_mode || boot_phase!=3 || !world_identity_installed || !accept_ui() || !player_pins()) return NULL;
    typedef void *(*controller_fn)(void *);
    unsigned char *controller=((controller_fn)(image+0x9fc550))(image+0x3d196f0);
    uintptr_t vtable;unsigned char *object;uint64_t runtime;
    if(!read_local(controller,&vtable,8) || vtable!=(uintptr_t)(image+0x2c39488) ||
       !read_local(controller+0x28,&object,8) || !read_local(object+8,&runtime,8)) return NULL;
    *saved=world_identity_lookup(runtime);
    return *saved?controller:NULL;
}
static int player_throttle_once(player_value_fn native,void *controller,uint64_t tick,unsigned owner,
                                uint64_t expected,uint64_t actual,double value) {
    if(!controller || !tick || tick!=stats.issued || tick<=player_action_tick || owner!=1 ||
       !expected || expected!=actual || !isfinite(value) || value < -1 || value > 1) return 0;
    native(controller,0x6e,(float)value);player_action_tick=tick;return 1;
}
static int lua_player_throttle(lua_State *L) {
    double tick=p_lua_tonumber(L,1),owner=p_lua_tonumber(L,2),value=p_lua_tonumber(L,4);
    size_t length=0;const char *expected=p_lua_tolstring(L,3,&length);uint64_t saved=0,actual=0;
    if(!isfinite(tick) || tick<1 || tick>18000 || floor(tick)!=tick || owner!=1 || !expected || length!=16 ||
       stats.report_tick+1!=stats.issued) {
        p_lua_pushnumber(L,0);return 1;
    }
    for(unsigned i=0;i<16;i++) {
        unsigned digit=expected[i]>='0' && expected[i]<='9' ? expected[i]-'0' : expected[i]>='a' && expected[i]<='f' ? expected[i]-'a'+10 : 16;
        if(digit>15) {p_lua_pushnumber(L,0);return 1;}saved=(saved<<4)|digit;
    }
    void *controller=player_context(&actual);
    int ok=player_throttle_once((player_value_fn)(image+0xd8fb60),controller,(uint64_t)tick,1,saved,actual,value);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_controls(lua_State *L) {
    uint64_t saved=0;if(!player_context(&saved)) {p_lua_pushnil(L);return 1;}
    static const unsigned offsets[]={0x2f4b750,0x2f4b754,0x2f4b758,0x2f4b75c,0x2f4b788,0x2f4b78c,0x2f4b790};
    char out[192];int n=snprintf(out,sizeof(out),"PLAYER_AXES_1:%016llx:",(unsigned long long)saved);
    for(unsigned i=0;i<7;i++) {
        uint32_t bits;float value;
        if(!read_local(image+offsets[i],&bits,4)) {p_lua_pushnil(L);return 1;}
        memcpy(&value,&bits,4);if(!isfinite(value)) {p_lua_pushnil(L);return 1;}
        n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",bits);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_observe_enable(lua_State *L) {
    uint64_t saved=0;void *controller=player_context(&saved);
    p_lua_pushnumber(L,controller && player_observer_install(controller,saved));return 1;
}
static int lua_player_solver_enable(lua_State *L) {p_lua_pushnumber(L,solver_install());return 1;}
#include "lockstep_request_api.h"
static int lua_player_solver_event(lua_State *L) {
    double requested=p_lua_tonumber(L,1);char out[4096];int n;
    if(GetCurrentThreadId()!=ui_thread || !solver_enabled_tick || !isfinite(requested) || requested<0 ||
       requested>SOLVER_CAPACITY || floor(requested)!=requested) {p_lua_pushnil(L);return 1;}
    if(!requested) {
        n=snprintf(out,sizeof(out),"schema=1 count=%ld failures=%ld excluded=%ld open=%ld restored=%d active=%d early_coverage=0",
            solver_count,solver_failures,solver_excluded,solver_open_scopes,solver_restored,solver_installed);
    } else {
        unsigned index=(unsigned)requested;solver_event *e=&solver_events[index-1];
        if(index>(unsigned)solver_count || !InterlockedCompareExchange(&e->ready,1,1)) {p_lua_pushnil(L);return 1;}
        n=snprintf(out,sizeof(out),"index=%u tick=%llu kind=%u actor=%u owner=%016llx ordinal=%llu role=%u bracket=%u caller=%08llx result=%u valid=%u solve_tick=%llu solve_ordinal=%llu",
            index,(unsigned long long)e->tick,e->kind,e->actor,(unsigned long long)e->owner,(unsigned long long)e->ordinal,
            e->role,e->bracket,(unsigned long long)e->caller,e->result,e->valid,(unsigned long long)e->solve_tick,(unsigned long long)e->solve_ordinal);
        const unsigned char *blocks[]={e->context[0],e->context[1],e->motion[0],e->motion[1],e->source,e->configuration,e->published};
        const unsigned sizes[]={SOLVER_CONTEXT_BYTES,SOLVER_CONTEXT_BYTES,SOLVER_MOTION_BYTES,SOLVER_MOTION_BYTES,96,8,96};
        const char *names[]={"c0","c1","m0","m1","source","configuration","published"};
        static const char hex[]="0123456789abcdef";
        for(unsigned b=0;b<7;b++) {
            n+=snprintf(out+n,sizeof(out)-(size_t)n," %s=",names[b]);
            for(unsigned j=0;j<sizes[b];j++) {unsigned v=blocks[b][j];out[n++]=hex[v>>4];out[n++]=hex[v&15];}
        }
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_observe(lua_State *L) {
    double requested=p_lua_tonumber(L,1);char out[2048];int n;
    if(GetCurrentThreadId()!=ui_thread || !player_observer_enabled_tick || !isfinite(requested) ||
       requested<0 || requested>PLAYER_OBSERVER_CAPACITY || floor(requested)!=requested) {p_lua_pushnil(L);return 1;}
    if(!requested) {
        snprintf(out,sizeof(out),"enabled=%llu count=%ld failures=%ld restored=%d active=%d early_coverage=0",
                 (unsigned long long)player_observer_enabled_tick,player_observer_count,player_observer_failures,
                 player_observer_restored,player_update_installed || player_shoot_installed);
    } else {
        unsigned index=(unsigned)requested;player_observation *s=&player_observations[index-1];
        if(index>(unsigned)player_observer_count || !InterlockedCompareExchange(&s->ready,1,1)) {p_lua_pushnil(L);return 1;}
        n=snprintf(out,sizeof(out),"index=%u tick=%llu kind=%u role=%u thread=%lu actor=%016llx owner=%016llx time=%016llx result=%u valid=%u",
                   index,(unsigned long long)s->tick,s->kind,s->role,s->thread,(unsigned long long)s->actor,
                   (unsigned long long)s->owner,(unsigned long long)s->time,s->result,s->valid);
        for(unsigned phase=0;phase<2;phase++) {
            n+=snprintf(out+n,sizeof(out)-n," g%u=",phase);
            for(unsigned i=0;i<PLAYER_GLOBAL_BYTES;i++) n+=snprintf(out+n,sizeof(out)-n,"%02x",s->globals[phase][i]);
            n+=snprintf(out+n,sizeof(out)-n," c%u=",phase);
            for(unsigned i=0;i<PLAYER_LOCAL_BYTES;i++) n+=snprintf(out+n,sizeof(out)-n,"%02x",s->local[phase][i]);
        }
    }
    p_lua_pushlstring(L,out,strlen(out));return 1;
}
static int lua_player_fire_enable(lua_State *L) {
    uint64_t saved=0;void *controller=player_context(&saved);
    p_lua_pushnumber(L,controller && player_fire_enable(controller,saved));return 1;
}
static int lua_player_launch_enable(lua_State *L) {p_lua_pushnumber(L,player_launch_enable());return 1;}
static int lua_player_launch_event(lua_State *L) {
    double request=p_lua_tonumber(L,1);char out[1024];int n;
    if(!player_weapon_seeded || GetCurrentThreadId()!=ui_thread || !isfinite(request) || request<0 || floor(request)!=request || request>PLAYER_LAUNCH_CAP) {p_lua_pushnil(L);return 1;}
    if(!request) {
        n=snprintf(out,sizeof(out),"schema=1 domain=504c5957504e3031 count=%u failures=%llu scopes=%llu restored=%llu bullet_rng=0",
            player_launch_count,(unsigned long long)player_launch_failures,(unsigned long long)player_launch_scopes,(unsigned long long)player_launch_restored);
    } else {
        if(request>player_launch_count){p_lua_pushnil(L);return 1;}
        player_launch_event *e=&player_launch_events[(unsigned)request-1];
        n=snprintf(out,sizeof(out),"index=%u tick=%llu kind=%u ship=%016llx gun=%016llx call=%llu spawn=%llu caller=%016llx seed=%016llx after=%016llx restored=%016llx original=%u valid=%u pre=",
            (unsigned)request,(unsigned long long)e->tick,e->kind,(unsigned long long)player_observer_actor,(unsigned long long)e->gun,
            (unsigned long long)e->call,(unsigned long long)e->spawn,(unsigned long long)e->caller,(unsigned long long)e->seed,
            (unsigned long long)e->after,(unsigned long long)e->restored,e->original,e->valid);
        for(unsigned i=0;i<3;i++)n+=snprintf(out+n,sizeof(out)-n,"%016llx",(unsigned long long)e->pre[i]);
        n+=snprintf(out+n,sizeof(out)-n," post=");
        for(unsigned i=0;i<3;i++)n+=snprintf(out+n,sizeof(out)-n,"%016llx",(unsigned long long)e->post[i]);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static void *player_fire_action_context(lua_State *L,uint64_t *tick) {
    double t=p_lua_tonumber(L,1);size_t length=0;
    const char *expected=p_lua_tolstring(L,3,&length);uint64_t actual=0,wanted=0;
    if(!player_fire_enabled || !isfinite(t) || t<1 || t>18000 || floor(t)!=t || p_lua_tonumber(L,2)!=1 ||
       !expected || length!=16 || stats.report_tick+1!=stats.issued || (uint64_t)t!=stats.issued ||
       (uint64_t)t<=player_action_tick || !player_fire_player_valid()) return NULL;
    for(unsigned i=0;i<16;i++) {
        unsigned d=expected[i]>='0' && expected[i]<='9'?expected[i]-'0':expected[i]>='a' && expected[i]<='f'?expected[i]-'a'+10:16;
        if(d>15) return NULL;wanted=(wanted<<4)|d;
    }
    void *controller=player_context(&actual);
    if(!controller || !wanted || wanted!=actual || wanted!=player_observer_actor) return NULL;
    *tick=(uint64_t)t;return controller;
}
static int player_trigger_apply(player_trigger_fn original,uint64_t tick,unsigned pressed) {
    unsigned char flags[7],history;uint64_t time;
    if(pressed>1 || pressed==(unsigned)player_trigger_owned || !player_fire_state(flags,&history,&time) ||
       flags[0]!=(unsigned)player_trigger_owned || flags[1] || flags[2] || flags[3] || flags[4]) return 0;
    player_action_tick=tick;player_trigger_owned=(int)pressed;
    original(0x18,(unsigned char)pressed); /* Native input handler, once. */
    int ok=player_fire_state(flags,&history,&time) && flags[0]==pressed && (history&1)==pressed;
    if(!ok) ++player_fire_failures;return ok;
}
static int lua_player_trigger(lua_State *L) {
    uint64_t tick;double value=p_lua_tonumber(L,4);int ok=0;
    if((value==0 || value==1) && player_fire_action_context(L,&tick))
        ok=player_trigger_apply((player_trigger_fn)(image+0x7e8ef0),tick,(unsigned)value);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_target(lua_State *L) {
    uint64_t tick;double fixture=p_lua_tonumber(L,4);int ok=0;
    if(isfinite(fixture) && fixture>=1 && fixture<=12 && floor(fixture)==fixture && player_fire_action_context(L,&tick)) {
        unsigned i=(unsigned)fixture-1;uint64_t actual;
        if(read_local((unsigned char*)actor_components[i]+8,&actual,8) && actual==actor_ids[i] && world_identity_lookup(actual)) {
            typedef unsigned char (*target_fn)(uint64_t,const char *);
            player_action_tick=tick;ok=((target_fn)(image+0xb01790))(actual,"")!=0;
        }
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_trigger_cleanup(lua_State *L) {
    int ok=1;
    if(player_trigger_owned) {
        if(GetCurrentThreadId()!=ui_thread) {p_lua_pushnumber(L,0);return 1;}
        uint64_t actual=0;typedef void *(*controller_fn)(void *);
        unsigned char *controller=((controller_fn)(image+0x9fc550))(image+0x3d196f0),*object=NULL;
        ok=GetCurrentThreadId()==ui_thread && player_fire_player_valid() && controller==player_observer_controller &&
           read_local(controller+0x28,&object,8) && object==player_observer_object &&
           read_local(object+8,&actual,8) && world_identity_lookup(actual)==player_observer_actor;
        if(ok) {
            ((player_trigger_fn)(image+0x7e8ef0))(0x18,0);player_trigger_owned=0;++player_trigger_cleanup_calls;
            unsigned char flags[7],history;uint64_t time;
            ok=player_fire_state(flags,&history,&time) && !flags[0] && !(history&1);
        }
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_player_fire_state(lua_State *L) {
    unsigned char flags[7],history;uint64_t time;char out[160];
    if(!player_fire_enabled || GetCurrentThreadId()!=ui_thread || !player_fire_state(flags,&history,&time)) {p_lua_pushnil(L);return 1;}
    int n=snprintf(out,sizeof(out),"PLAYER_FIRE_1:%016llx:",(unsigned long long)player_observer_actor);
    for(unsigned i=0;i<7;i++) n+=snprintf(out+n,sizeof(out)-n,"%02x",flags[i]);
    n+=snprintf(out+n,sizeof(out)-n,":%02x:%016llx",history,(unsigned long long)time);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_player_fire_event(lua_State *L) {
    double request=p_lua_tonumber(L,1);char out[768];int n;
    if(!player_fire_enabled || GetCurrentThreadId()!=ui_thread || !isfinite(request) || request<0 || floor(request)!=request || request>PLAYER_FIRE_EVENTS) {p_lua_pushnil(L);return 1;}
    if(!request) {
        n=snprintf(out,sizeof(out),"guns=%u created=%u moves=%llu damage=%llu events=%u failures=%llu held=%d cleanup=%d rng_changes=%d",
                   player_gun_count,player_projectile_count,(unsigned long long)player_fire_moves,(unsigned long long)player_fire_damage,
                   player_fire_event_count,(unsigned long long)player_fire_failures,player_trigger_owned,player_trigger_cleanup_calls,player_weapon_seeded);
    } else {
        if(request>player_fire_event_count) {p_lua_pushnil(L);return 1;}
        player_fire_event *e=&player_fire_events[(unsigned)request-1];
        n=snprintf(out,sizeof(out),"index=%u tick=%llu kind=%u ship=%016llx gun=%016llx spawn=%llu target=%016llx call=%llu amount=%016llx absorbed=%016llx before=%016llx after=%016llx hits_before=%d hits_after=%d",
                   (unsigned)request,(unsigned long long)e->tick,e->kind,(unsigned long long)player_observer_actor,
                   (unsigned long long)e->gun,(unsigned long long)e->spawn,(unsigned long long)e->target,
                   (unsigned long long)e->call,(unsigned long long)e->amount,(unsigned long long)e->absorbed,
                   (unsigned long long)e->before,(unsigned long long)e->after,e->hits_before,e->hits_after);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
