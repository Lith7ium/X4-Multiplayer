/* Player input callback and read-only attribution at existing weapon/bullet/
 * shield wrappers. No AI orders, substitute gun or transform writes. Launch RNG
 * intervention is a separate default-off option in lockstep_player_rng.h. */
#define PLAYER_GUN_CAP 32u
#define PLAYER_BULLET_CAP 1024u
#define PLAYER_FIRE_EVENTS 8192u
typedef void (*player_trigger_fn)(unsigned,unsigned char);
typedef struct {void *object;uint64_t runtime,saved,spawns,tick,calls;} player_gun;
typedef struct {void *object;uint64_t runtime,gun,spawn,moves;unsigned init_record;} player_projectile;
static void player_weapon_execute(weapon_fn original,player_gun *gun,void *self);
static void player_launch_created(player_projectile *projectile);
typedef struct {
    uint64_t tick,gun,spawn,target,call,amount,absorbed,before,after;
    unsigned kind;int hits_before,hits_after;
} player_fire_event;
static player_gun player_guns[PLAYER_GUN_CAP],*player_gun_context;
static player_projectile player_projectiles[PLAYER_BULLET_CAP],*player_projectile_context;
static player_fire_event player_fire_events[PLAYER_FIRE_EVENTS];
static unsigned player_gun_count,player_projectile_count,player_fire_event_count;
static uint64_t player_fire_moves,player_fire_damage,player_fire_failures;
static int player_fire_enabled,player_trigger_owned,player_trigger_cleanup_calls;
static unsigned char *player_fire_player;
static uint64_t player_fire_player_saved;

static int player_fire_pins(void) {
    return hash_ok(image+0x7e8ef0,448,"605405f7391c0afb49e071e63cedce457bc8e3ccf8076dde1d70b15fc9cc4a3c") &&
           hash_ok(image+0x9fedf0,201,"8ce33b1b535b9975e5a44bc7d3b41aecd6a30cf182259f84fc262d1b0083b804") &&
           hash_ok(image+0x9ed1f0,957,"ff9bfcb2989d3a16c2ca948a48f39cb212981efcfea323ebe109fab205bb5c85") &&
           hash_ok(image+0xb01790,600,"c34a08a9c160efc79fe9023e0fa0149cc588065c427e4fec4bf1a1c0535f0124") &&
           hash_ok(image+WEAPON_MOVE,5713,"0b79f8f9b390ea52241a6c13375953a9f07f0603febc82958d4213fdea7fe80f") &&
           hash_ok(image+BULLET_CREATE,159,"e1fc6990a8a63c8cc44476ef678b7ea8a1bb5e0275695a10d7da0f2118ceb54e") &&
           hash_ok(image+BULLET_MOVE,428,"1690c279aeac79136f9234dd4f6144e59e653bb06a3c6dc101d3b4b67b397a5f");
}
static int player_fire_player_valid(void) {
    unsigned char *universe,*player;uint64_t runtime;
    return read_local(image+0x3d198f8,&universe,8) && universe &&
           read_local(universe+0x230,&player,8) && player && player==player_fire_player &&
           read_local(player+8,&runtime,8) && world_identity_lookup(runtime)==player_fire_player_saved;
}
static int player_fire_state(unsigned char flags[7],unsigned char *history,uint64_t *time) {
    return player_fire_player_valid() && read_local(image+0x38f0728,flags,6) &&
           read_local(image+0x2f4b6d8,flags+6,1) && read_local(player_fire_player+0x8a550,history,1) &&
           read_local(player_fire_player+0x8a558,time,8);
}
static int player_fire_enable(void *controller,uint64_t actor) {
    unsigned char *universe;uint64_t runtime;unsigned char flags[7],history;uint64_t time;
    if(player_fire_enabled || stats.issued!=1 || controller!=player_observer_controller || actor!=player_observer_actor ||
       !player_update_installed || !player_shoot_installed || weapon_installed!=3 || bullet_installed!=3 || !damage_installed ||
       !player_fire_pins() || !read_local(image+0x3d198f8,&universe,8) || !universe ||
       !read_local(universe+0x230,&player_fire_player,8) || !player_fire_player ||
       !read_local(player_fire_player+8,&runtime,8)) return 0;
    player_fire_player_saved=world_identity_lookup(runtime);
    if(!player_fire_player_saved || !player_fire_state(flags,&history,&time) || flags[0] || flags[1] || flags[2] || flags[3] || (history&15)) return 0;
    player_fire_enabled=1;return 1;
}
static int player_gun_owned(void *self) {
    unsigned char *parent=self;uint64_t runtime;
    for(unsigned i=0;parent && i<64;i++) {
        if(parent==player_observer_object) return read_local(parent+8,&runtime,8) && world_identity_lookup(runtime)==player_observer_actor;
        unsigned char *next;if(!read_local(parent+0x70,&next,8) || next==parent) return 0;parent=next;
    }
    return 0;
}
static player_fire_event *player_fire_record(unsigned kind,player_projectile *projectile) {
    if(player_fire_event_count>=PLAYER_FIRE_EVENTS) {++player_fire_failures;return NULL;}
    player_fire_event *e=&player_fire_events[player_fire_event_count++];memset(e,0,sizeof(*e));
    e->kind=kind;e->tick=stats.issued;e->gun=projectile->gun;e->spawn=projectile->spawn;e->call=projectile->moves;return e;
}
static int player_weapon_observe(weapon_fn original,void *self) {
    if(!player_fire_enabled || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread || !player_gun_owned(self)) return 0;
    uint64_t runtime,saved;uintptr_t vtable;
    if(!read_local(self,&vtable,8) || (vtable!=(uintptr_t)(image+weapon_vtables[0]) && vtable!=(uintptr_t)(image+weapon_vtables[1])) ||
       !read_local((unsigned char*)self+8,&runtime,8) || !(saved=world_identity_lookup(runtime))) {
        ++player_fire_failures;original(self);return 1;
    }
    player_gun *gun=NULL;
    for(unsigned i=0;i<player_gun_count;i++) if(player_guns[i].saved==saved) {gun=&player_guns[i];break;}
    if(!gun && player_gun_count<PLAYER_GUN_CAP) {
        gun=&player_guns[player_gun_count++];*gun=(player_gun){.object=self,.runtime=runtime,.saved=saved};
    }
    if(!gun || gun->object!=self || gun->runtime!=runtime) {++player_fire_failures;original(self);return 1;}
    if(gun->tick!=stats.issued) {gun->tick=stats.issued;gun->calls=0;}
    ++gun->calls;player_weapon_execute(original,gun,self);return 1;
}
static void player_bullet_created(void *object) {
    if(!player_fire_enabled || !player_gun_context) return;
    uint64_t runtime;uintptr_t vtable;
    if(!read_local(object,&vtable,8) || vtable!=(uintptr_t)(image+0x2c42ac8) ||
       !read_local((unsigned char*)object+8,&runtime,8) || !runtime || player_projectile_count>=PLAYER_BULLET_CAP) {++player_fire_failures;return;}
    for(unsigned i=0;i<player_projectile_count;i++) if(player_projectiles[i].runtime==runtime) {++player_fire_failures;return;}
    player_projectile *p=&player_projectiles[player_projectile_count++];
    *p=(player_projectile){.object=object,.runtime=runtime,.gun=player_gun_context->saved,.spawn=++player_gun_context->spawns};
    player_fire_record(1,p);
    player_launch_created(p);
}
static int player_bullet_observe(weapon_fn original,void *self) {
    if(!player_fire_enabled) return 0;
    uint64_t runtime;if(!read_local((unsigned char*)self+8,&runtime,8)) return 0;
    player_projectile *p=NULL;
    for(unsigned i=0;i<player_projectile_count;i++) if(player_projectiles[i].object==self && player_projectiles[i].runtime==runtime) {p=&player_projectiles[i];break;}
    if(!p) return 0;
    int before=0,after=0;
    if(!read_local((unsigned char*)self+0x280,&before,4)) ++player_fire_failures;
    ++p->moves;++player_fire_moves;
    player_fire_event *move=player_fire_record(2,p);
    if(move) {
        move->hits_before=before;
        if(!read_local((unsigned char*)self+0x268,&move->before,8)) ++player_fire_failures;
    }
    player_projectile *outer=player_projectile_context;player_projectile_context=p;
    original(self);player_projectile_context=outer;
    if(!read_local((unsigned char*)self+0x280,&after,4)) ++player_fire_failures;
    if(move) {
        move->hits_after=after;
        if(!read_local((unsigned char*)self+0x268,&move->after,8)) ++player_fire_failures;
    }
    if(before!=after) {player_fire_event *e=player_fire_record(3,p);if(e) {e->hits_before=before;e->hits_after=after;}}
    return 1;
}
static void player_damage_observed(const damage_event *event) {
    if(!player_fire_enabled || !player_projectile_context) return;
    player_fire_event *e=player_fire_record(4,player_projectile_context);++player_fire_damage;
    if(e) {
        e->target=world_identity_lookup(actor_ids[event->actor-1]);e->amount=event->amount;e->absorbed=event->absorbed;
        e->before=event->before[0];e->after=event->after[0];
        if(!e->target) ++player_fire_failures;
    }
}
