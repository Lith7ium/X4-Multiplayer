/* Storage slots and deterministic identities are distinct. Slots 13/14 remain
 * dock anchors; dynamic ships have a bounded, non-recycled reservation range. */
#define ACTOR_FIXTURE_COUNT 12u
#define ACTOR_DOCK_FIRST 13u
#define ACTOR_DOCK_LAST 14u
#define ACTOR_DYNAMIC_FIRST 15u
#define ACTOR_DYNAMIC_COUNT 16u
#define ACTOR_CAPACITY (ACTOR_DYNAMIC_FIRST+ACTOR_DYNAMIC_COUNT-1u)
enum actor_role {ACTOR_NONE,ACTOR_FIXTURE_SHIP,ACTOR_DOCK_ANCHOR,ACTOR_DYNAMIC_SHIP};
typedef struct {
    uint64_t handle,reserved_tick,bound_tick,prepared_tick,first_update_tick;
    unsigned first_update_kind,late_updates;
    char macro[64],owner[32];
} dynamic_ship;
static dynamic_ship dynamic_ships[ACTOR_DYNAMIC_COUNT];
static unsigned dynamic_ship_count;
static volatile LONG64 registry_failures;
static SRWLOCK registry_lock=SRWLOCK_INIT;
static volatile LONG registry_observing;
static int registry_is_observing(void) {return InterlockedCompareExchange(&registry_observing,0,0)!=0;}
enum registry_update_kind {REGISTRY_SHIP=1,REGISTRY_NAV,REGISTRY_AGENT,REGISTRY_FLIGHT,REGISTRY_SPLINE,
    REGISTRY_AI,REGISTRY_ATTENTION,REGISTRY_SHAPE,REGISTRY_RADAR,REGISTRY_GRAVIDAR,
    REGISTRY_WEAPON,REGISTRY_DAMAGE,REGISTRY_DEATH,REGISTRY_MASS,REGISTRY_VELOCITY,REGISTRY_CONTACT,REGISTRY_PARAMS};
static void registry_observe_object(void *,unsigned);
static void registry_observe_owner(void *,unsigned,unsigned);
static void registry_observe_chain(void *,unsigned);
static void registry_observe_runtime(uint64_t,unsigned);
static void registry_nav_observe(unsigned,const void *);
static void registry_observe_ai(void *,void *,void *);
static uint64_t registry_md_next_order(void);
#include "lockstep_registry_origin_scope.h"
#include "lockstep_registry_creation_contract.h"
static void registry_creation_observe(void *,uint64_t,unsigned);
static void registry_creation_origin_end(registry_origin_scope *,uint64_t,int);
static void registry_creation_origin_enter(registry_origin_scope *);
static void registry_creation_trace(unsigned,unsigned);
static void registry_creation_stop(void);
static int registry_creation_rng_preimage(const char *,size_t,const char *,size_t);
static int registry_creation_attachment_snapshot(registry_creation_attachment *);
static void *actor_components[ACTOR_CAPACITY];
static uint64_t actor_ids[ACTOR_CAPACITY];
static enum actor_role actor_role(unsigned actor) {
    if(actor>=1 && actor<=ACTOR_FIXTURE_COUNT) return ACTOR_FIXTURE_SHIP;
    if(actor>=ACTOR_DOCK_FIRST && actor<=ACTOR_DOCK_LAST) return ACTOR_DOCK_ANCHOR;
    if(actor>=ACTOR_DYNAMIC_FIRST && actor<=ACTOR_CAPACITY && dynamic_ships[actor-ACTOR_DYNAMIC_FIRST].handle)
        return ACTOR_DYNAMIC_SHIP;
    return ACTOR_NONE;
}
static int actor_is_ship(unsigned actor) {
    enum actor_role role=actor_role(actor);return role==ACTOR_FIXTURE_SHIP || role==ACTOR_DYNAMIC_SHIP;
}
static int actor_is_dock(unsigned actor) {return actor_role(actor)==ACTOR_DOCK_ANCHOR;}
static int actor_number_is_ship(double actor) {
    return isfinite(actor) && actor>=1 && actor<=ACTOR_CAPACITY && floor(actor)==actor && actor_is_ship((unsigned)actor);
}
static uint64_t actor_rng_identity(unsigned actor) {
    if(actor_role(actor)==ACTOR_DYNAMIC_SHIP)
        return 0x8000000000000000ULL|dynamic_ships[actor-ACTOR_DYNAMIC_FIRST].handle;
    return actor;
}
static int registry_handle(const char *text,size_t length,uint64_t *out) {
    if(!text || length!=16) return 0;
    uint64_t value=0;
    for(unsigned i=0;i<16;i++) {
        unsigned d=text[i]>='0'&&text[i]<='9'?text[i]-'0':text[i]>='a'&&text[i]<='f'?text[i]-'a'+10:16;
        if(d>15) return 0;value=(value<<4)|d;
    }
    if(value>>63 || !(value>>32) || ((value>>24)&255)>2 || !(value&0xffffff)) return 0;
    *out=value;return 1;
}
static int registry_name(const char *text,size_t length,size_t capacity) {
    if(!text || !length || length>=capacity) return 0;
    for(size_t i=0;i<length;i++) if(!((text[i]>='a'&&text[i]<='z')||(text[i]>='0'&&text[i]<='9')||text[i]=='_')) return 0;
    return 1;
}
static unsigned registry_find(uint64_t handle) {
    for(unsigned i=0;i<dynamic_ship_count;i++) if(dynamic_ships[i].handle==handle) return i+ACTOR_DYNAMIC_FIRST;
    return 0;
}
static unsigned registry_reserve_record(uint64_t handle,const char *macro,size_t ml,const char *owner,size_t ol,uint64_t tick) {
    if(!tick || tick!=handle>>32 || handle>>63 || ((handle>>24)&255)>2 || !(handle&0xffffff) ||
       !registry_name(macro,ml,64) || !registry_name(owner,ol,32)) return 0;
    unsigned existing=registry_find(handle);
    if(existing) {
        dynamic_ship *s=&dynamic_ships[existing-ACTOR_DYNAMIC_FIRST];
        return s->reserved_tick==tick && strlen(s->macro)==ml && strlen(s->owner)==ol &&
            !memcmp(s->macro,macro,ml) && !memcmp(s->owner,owner,ol)?existing:0;
    }
    if(dynamic_ship_count==ACTOR_DYNAMIC_COUNT || (dynamic_ship_count && dynamic_ships[dynamic_ship_count-1].handle>=handle)) return 0;
    dynamic_ship *s=&dynamic_ships[dynamic_ship_count];s->handle=handle;s->reserved_tick=tick;
    memcpy(s->macro,macro,ml);memcpy(s->owner,owner,ol);
    return ACTOR_DYNAMIC_FIRST+dynamic_ship_count++;
}
static unsigned registry_reserve(uint64_t handle,const char *macro,size_t ml,const char *owner,size_t ol,uint64_t tick) {
    AcquireSRWLockExclusive(&registry_lock);
    unsigned actor=registry_reserve_record(handle,macro,ml,owner,ol,tick);
    if(actor) InterlockedExchange(&registry_observing,1);
    ReleaseSRWLockExclusive(&registry_lock);return actor;
}
