/* Separate, default-off player weapon launch RNG experiment. Bullet Move remains
 * native. Only the original gun call is scoped; no timer/transform correction. */
#define PLAYER_LAUNCH_CAP 8192u
#define PLAYER_LAUNCH_SCHEMA 1u
#define PLAYER_LAUNCH_DOMAIN 0x504c5957504e3031ULL
typedef struct {
    uint64_t tick,gun,call,spawn,caller,seed,after,restored,pre[3],post[3];
    unsigned kind,original,valid;
} player_launch_event;
static player_launch_event player_launch_events[PLAYER_LAUNCH_CAP];
static unsigned player_launch_count;
static uint64_t player_launch_scopes,player_launch_restored,player_launch_failures;
static int player_weapon_seeded;
static player_launch_event *player_launch_context;
static int player_launch_pins(void) {
    return hash_ok(image+0x817d20,2189,"3a982fcf264402e4833300daa8c0f5175f2280738710902357a65b98198b32a5") &&
           hash_ok(image+0x8185b0,2968,"7a394490908faf59f734ff0713f162d8d507f29f0727d56f057996426d58d58a") &&
           hash_ok(image+0xd50f30,2726,"aea405f11901e0a3de8ff4a80837708e1a829cd2480296ca363243ecf469cec2") &&
           hash_ok(image+0xd4f240,4769,"9a4064a23c8cda1d311a3b6a4c51e0eb862f0d04f750926e6c347f6d03a78392") &&
           *(void**)(image+weapon_vtables[0]+0x1f30)==image+0x8185b0;
}
static int player_launch_enable(void) {
    if(!player_fire_enabled || player_weapon_seeded || stats.issued!=1 || GetCurrentThreadId()!=ui_thread ||
       !stats.active || boot_phase!=3 || !player_fire_player_valid() || !player_launch_pins()) return 0;
    player_weapon_seeded=1;return 1;
}
static uint64_t player_launch_seed(uint64_t tick,uint64_t ship,uint64_t gun,uint64_t call) {
    return lockstep_seed(PLAYER_LAUNCH_DOMAIN,tick,ship,gun,call,PLAYER_LAUNCH_SCHEMA);
}
static player_launch_event *player_launch_record(unsigned kind,uint64_t gun,uint64_t call) {
    if(player_launch_count==PLAYER_LAUNCH_CAP) {++player_launch_failures;return NULL;}
    player_launch_event *e=&player_launch_events[player_launch_count++];memset(e,0,sizeof(*e));
    e->kind=kind;e->tick=stats.issued;e->gun=gun;e->call=call;return e;
}
static void player_launch_created(player_projectile *p) {
    if(!player_weapon_seeded || !player_launch_context) return;
    player_launch_event *e=player_launch_record(2,p->gun,player_launch_context->call);
    if(!e) return;
    e->spawn=p->spawn;e->valid=read_local((unsigned char*)p->object+0x268,e->pre,sizeof(e->pre));
    p->init_record=player_launch_count;
}
static void player_weapon_scope(weapon_fn original,player_gun *gun,void *self,uint64_t *rng) {
    player_gun *outer_gun=player_gun_context;player_launch_event *outer_scope=player_launch_context;
    player_gun_context=gun;player_launch_context=NULL;
    if(!player_weapon_seeded) {original(self);player_gun_context=outer_gun;player_launch_context=outer_scope;return;}
    player_launch_event *e=player_launch_record(1,gun->saved,gun->calls);
    uintptr_t vtable=0;
    if(!e || !rng || !read_local(self,&vtable,8) || vtable!=(uintptr_t)(image+weapon_vtables[0])) {
        ++player_launch_failures;if(e)e->original=1;original(self);
        player_gun_context=outer_gun;player_launch_context=outer_scope;return;
    }
    unsigned first=player_projectile_count;
    e->caller=*rng;e->seed=player_launch_seed(stats.issued,player_observer_actor,gun->saved,gun->calls);
    *rng=e->seed;player_launch_context=e;++player_launch_scopes;e->original=1;
    original(self); /* Exactly once, including nested native calls. */
    e->after=*rng;*rng=e->caller;e->restored=*rng;
    e->valid=e->caller==e->restored;if(e->valid)++player_launch_restored;else ++player_launch_failures;
    for(unsigned i=first;i<player_projectile_count;i++) {
        player_projectile *p=&player_projectiles[i];
        if(p->init_record && p->init_record<=player_launch_count) {
            player_launch_event *init=&player_launch_events[p->init_record-1];
            if(init->kind==2 && init->tick==e->tick && init->gun==e->gun && init->call==e->call) {
                uint64_t runtime=0;
                init->valid &= read_local((unsigned char*)p->object+8,&runtime,8) && runtime==p->runtime &&
                               read_local((unsigned char*)p->object+0x268,init->post,sizeof(init->post));
                if(!init->valid)++player_launch_failures;
            }
        }
    }
    player_gun_context=outer_gun;player_launch_context=outer_scope;
}
static void player_weapon_execute(weapon_fn original,player_gun *gun,void *self) {
    uint64_t *rng=NULL;
    if(player_weapon_seeded) {
        void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
        if(tls && tls[0])rng=(uint64_t*)((unsigned char*)tls[0]+0x318);
    }
    player_weapon_scope(original,gun,self,rng);
}
