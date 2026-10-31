/* Default-off hit claims, native side (native_gate_hit_claims; design in
 * reports/LOCKSTEP-PREDICTION-20261029.txt section 7, this implementation in
 * reports/LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt). Needs the piloting
 * prototype (slot manifest, wire delay) and world identities.
 *
 * Hook: the nearest-hit ray cast of U::Bullet::UpdateFlight 0xd4f240 (call
 * 0xbc4d0 at 0xd4fc61; R14 holds the bullet for the whole function, set at
 * 0xd4f292) is redirected to a stub on a private page near the image that
 * stores R14 and jumps to hitn_cast; the original cast always runs once with
 * the original arguments. One 16-byte CAS of the pinned block 0xd4fc60
 * installs and restores it; no thunk recipe, no entry patch. Cast ABI: 0xbc4d0
 * takes FIFTEEN arguments (system, result, origin, ray, then eleven stack
 * slots a5..a15 that it reads up to entry+0x78 and hands on to
 * System_CastRay 0xbb5d0: an object, 0, 0, byte 0, an unset slot, a qword,
 * byte 1, three dwords (a global, 7, a register value) and byte 1, as
 * UpdateFlight stores them at 0xd4fc13..0xd4fc4a). Every slot is forwarded
 * unchanged, to the original and to the owner's extra casts (forwarding
 * only eight crashed both H1 clients in System_CastRay at 0xbb6ba). Result
 * +0x00 point, +0x10 normal, +0x20 fraction (f32, -1 none), +0x28 hit
 * component; origin and ray are zone-frame float4. UpdateFlight treats fraction >= 0 with a
 * component as a hit and passes it to OnHit 0xd50500.
 * Identity: a projectile gets its portable key at its first nearest cast (the
 * creation frame casts once from BulletSetup), from the portable identity of
 * its shooter component (bullet+0x2a0, the weapon) and the shooter's ordinal
 * of first casts in that tick: hit_projectile_key(tick, shooter, 0, ordinal).
 * Cast order is the deterministic component walk order, so every client
 * derives the same key. Beams (defaults +0x4a0), piercing bullets (hit limit
 * +0x354 > 1) and shooters without a portable identity get no key and stay
 * fully native.
 * Suppression (every client, rule over the agreed slot manifest): a keyed
 * projectile's native hit is blanked (fraction -1, component 0) when the hit
 * component belongs to a predicted slot ship, or when the shooter belongs to
 * a slot ship and the hit component belongs to a ship with a pathing read
 * bank (a claimable target). Hits on other objects stay native.
 * Owner evaluation (this client only, its own slot ship L): incoming - a
 * projectile not shot by L is cast again with its segment moved from the
 * displayed pose D (lockstep_pilot_predict.h, previous tick) into the pose
 * the body has now (L's read bank), so the cast answers "does it hit the ship
 * the player sees"; outgoing - L's projectile is cast again shifted by d ticks
 * of its own flight against the world as it is. A hit becomes a claim (hit
 * point in the target's frame) proposed in this tick's hits section.
 * Resolution (slot 6, every client, lockstep_hit_claims.h rules): accepted
 * claims run OnHit(bullet, target, hit pose, normal, now) on the MoveWorker
 * in claim order with a keyed RNG word, then the bullet's hit count, the
 * event cancel 0x99aa90(bullet, 0xeb) and Kill (vfunc +0x1548 = 0x3e51b0,
 * reason 2) as UpdateFlight does after a non-ricochet hit; claimed hits never
 * ricochet. Every outcome enters the HITS_1 receipt of the declared state.
 * Stop reason 111: the call block, stub or handler changed, or an injection
 * unwound. */
#include "lockstep_hit_claims.h"
#define HITN_SITE 0xd4fc61u
#define HITN_BLOCK 0xd4fc60u
#define HITN_CAST 0xbc4d0u
#define HITN_UPDATE 0xd4f240u
#define HITN_ONHIT 0xd50500u
#define HITN_CANCEL 0x99aa90u
#define HITN_DEFAULTS 0xe645b0u
#define HITN_KILL 0x3e51b0u
#define HITN_FIND 0xced70u
#define HITN_MAP 0x6cf1500u
#define HITN_BULLET_VTABLE 0x2c42ac8u
#define HITN_LINEAR_VTABLE 0x2c3f7c0u
#define HITN_NAV_VTABLE 0x2c51158u
#define HITN_PLAYER_VTABLE 0x2c39488u
#define HITN_KILL_SLOT 0x1548u
#define HITN_STOP_REASON 111u
#define HITN_TABLE 8192u          /* projectile identities, open addressing by pointer */
#define HITN_ORDINALS 1024u       /* per-tick shooter ordinals */
#define HITN_EVICT_TICKS 1800u    /* identities older than 30 s may be reused */
#define HITN_PAGE 0x2000u         /* page 0 code (RX), page 1 data (RW) */
#define HITN_RADIUS_MM 1500000    /* claim point bound around the target */
#define HITN_RANGE 2000.0f        /* m: owner evaluation radius around its ship */
#define HITN_DEPTH 16u
static const unsigned char hitn_block_original[16]={0xc8,0xe8,0x6a,0xc8,0x36,0xff,0xf3,0x0f,0x10,0x45,0x40,0xbf,0x10,0x00,0x00,0x00};
static const struct {unsigned rva,size;const char *sha;} hitn_pins_list[]={
    {HITN_BLOCK,0x10u,"eb7b3b1ef0543dcef6643066be82e1f94ba2c25875acfa8a3b1a8f3fd833083c"},
    {HITN_UPDATE,0x60u,"39ae065c42c7708e6dec09326157db9779e943116c83ed68668a854034718794"}, /* prologue; R14 = bullet */
    {HITN_CAST,0x7cu,"04ec9321fcf84631fd64d5474383f9d77ac235c7388e2708cfab82cda04097c8"},
    {HITN_ONHIT,0x179u,"69cefeeff88c8704648ce8e254bed3bcf3773c424a3fae596300360517110ad7"},
    {HITN_CANCEL,0x84u,"395c5fb2eff06c142fc90e48fcc67d4bb26384cfe89886d88280f740d4fedb12"},
    {HITN_DEFAULTS,0xc0u,"a9e422d6e9752bb78457efaab2b9b40da8a0ddf0132a5c77f6713e27168249dd"},
    {HITN_KILL,0x87u,"2cae2b261a6065ebbfada548ae7e86f05ff2d82f09857665408d21c99ccc16d3"},
    {HITN_FIND,0xb8u,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"}};
/* The eleven stack slots a5..a15 of the cast, forwarded verbatim. */
typedef struct {uintptr_t a[11];} hitn_cast_tail;
typedef uintptr_t (*hitn_cast_fn)(void *,void *,const float *,const float *,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,
                                  uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
#define HITN_TAIL(t) (t)->a[0],(t)->a[1],(t)->a[2],(t)->a[3],(t)->a[4],(t)->a[5],(t)->a[6],(t)->a[7],(t)->a[8],(t)->a[9],(t)->a[10]
typedef void (*hitn_onhit_fn)(void *,void *,const float *,const float *,double);
typedef void (*hitn_cancel_fn)(void *,unsigned);
typedef void (*hitn_kill_fn)(void *,unsigned,void *,double);
typedef unsigned char *(*hitn_defaults_fn)(uintptr_t);
typedef void *(*hitn_find_fn)(void *,uint64_t,unsigned);
typedef struct {
    unsigned char *object;uint64_t runtime,key,shooter,tick;unsigned ordinal,flags,shooter_slot;
} hitn_projectile;
/* This client's slot ship as the casts of one tick see it (read once per tick). */
typedef struct {uint64_t tick;int valid,moved;unsigned char *own;float body[24];predict_pose display;} hitn_owner_view;
static hitn_owner_view hitn_owner;
enum {HITN_KEYED=1,HITN_BEAM=2,HITN_PIERCE=4,HITN_UNMAPPED=8,HITN_CLAIMED=16}; /* CLAIMED: owner-local, never re-claimed */
typedef struct {uint64_t shooter;unsigned count;} hitn_ordinal;
typedef struct {hit_claim claim;unsigned char *bullet;uint64_t observe;} hitn_candidate;
static hitn_projectile hitn_table[HITN_TABLE];
static hitn_ordinal hitn_ordinals[HITN_ORDINALS];
static uint64_t hitn_ordinal_tick=UINT64_MAX;
/* This client's claims not yet proposed, oldest first: the multiplexer offers
 * each section a byte budget, so claims that do not fit wait for later ticks
 * (until their observe tick leaves the window). */
#define HITN_QUEUE 32u
static hitn_candidate hitn_queue[HITN_QUEUE];
static unsigned hitn_queue_count;
static uint64_t hitn_queue_expired;
static unsigned char *hitn_page;   /* code at +0, R14 slot at +0x1000, handler at +0x1008 */
static unsigned char hitn_block_patched[16];
static int hitn_armed,hitn_installed,hitn_restored,hitn_policy,hitn_pins_checked;
static volatile LONG hitn_live;
static uint64_t hitn_casts,hitn_foreign,hitn_keyed,hitn_unkeyed,hitn_full,hitn_suppressed_in,hitn_suppressed_out,
    hitn_eval_in,hitn_eval_out,hitn_claims_in,hitn_claims_out,hitn_candidates_dropped,hitn_proposals,hitn_admitted,
    hitn_admit_refused,hitn_resolved,hitn_applied,hitn_frames,hitn_receipt,hitn_last_tick;
static volatile LONG64 hitn_failures;
static const char *hitn_stage="off";
/* Test seams (NULL/0 = the engine): registry lookup, TLS game time, RNG word. */
static hitn_find_fn hitn_find_override;
static double hitn_now_override;
static volatile uint64_t *hitn_word_override;

/* ---- engine access ---- */
static void *hitn_find(uint64_t runtime) {
    void *map=NULL;
    if(hitn_find_override) return hitn_find_override(NULL,runtime,4);
    if(!runtime || !read_local(image+HITN_MAP,&map,8) || !map) return NULL;
    return ((hitn_find_fn)(image+HITN_FIND))(map,runtime,4);
}
static int hitn_write(void *destination,const void *source,size_t bytes) {
    SIZE_T done=0;return destination && WriteProcessMemory(GetCurrentProcess(),destination,source,bytes,&done) && done==bytes;
}
static int hitn_vtable_is(const unsigned char *object,unsigned rva) {
    uintptr_t vtable=0;return object && read_local(object,&vtable,8) && pslot_canonical_vtable(vtable)==(uintptr_t)(image+rva);
}
/* Slot (1..N) of the resolved pilot slot ship that contains component, else 0. */
static unsigned hitn_slot_of(unsigned char *component) {
    for(unsigned depth=0;component && depth<HITN_DEPTH;depth++) {
        for(unsigned i=0;i<pilot_slot_count;i++) if(pilot_slots[i].resolved && pilot_slots[i].object==component) return i+1;
        unsigned char *parent=NULL;
        if(!read_local(component+0x70,&parent,8) || parent==component) return 0;
        component=parent;
    }
    return 0;
}
/* The read bank of a ship driven by a navigation/player controller. */
static int hitn_bank(unsigned char *ship,float bank[24]) {
    unsigned char *controller=NULL,*slots=NULL;unsigned index=2;
    return ship && read_local(ship+0x200,&controller,8) && controller &&
        (hitn_vtable_is(controller,HITN_NAV_VTABLE) || hitn_vtable_is(controller,HITN_PLAYER_VTABLE)) &&
        read_local(controller+0x30,&slots,8) && slots && read_local(image+0x38f4ee4,&index,4) && index<2 &&
        read_local(slots+index*0x1a0,bank,24*sizeof(float));
}
/* The ship (component with a read bank) that contains component. */
static unsigned char *hitn_ship_of(unsigned char *component) {
    float bank[24];
    for(unsigned depth=0;component && depth<HITN_DEPTH;depth++) {
        if(hitn_bank(component,bank)) return component;
        unsigned char *parent=NULL;
        if(!read_local(component+0x70,&parent,8) || parent==component) return NULL;
        component=parent;
    }
    return NULL;
}
static uint64_t hitn_runtime(const unsigned char *object) {uint64_t id=0;return object && read_local(object+8,&id,8)?id:0;}
static uint64_t hitn_runtime_of(uint64_t portable) {
    /* A slot ship built after a death answers to the slot's saved ID (lockstep_pilot_death.h). */
    for(unsigned i=0;portable && i<pilot_slot_count;i++) if(pilot_slots[i].renewed && pilot_slots[i].saved==portable) return pilot_slots[i].runtime;
    uint64_t created=created_runtime_of(portable);
    return created?created:pilot_reverse(portable);
}
/* Bullet facts: defaults via the native getter (a lookup by macro). */
static int hitn_bullet_flags(unsigned char *bullet,unsigned *flags,unsigned *limit) {
    uintptr_t macro=0;unsigned char *defaults=NULL,beam=0;unsigned hits_limit=0;
    if(!read_local(bullet+0x60,&macro,8)) return 0;
    defaults=((hitn_defaults_fn)(image+HITN_DEFAULTS))(macro);
    if(!defaults || !read_local(defaults+0x4a0,&beam,1) || !read_local(defaults+0x354,&hits_limit,4)) return 0;
    *flags=(beam?HITN_BEAM:0)|(hits_limit>1?HITN_PIERCE:0);*limit=hits_limit;return 1;
}
/* Position (zone frame, as the native controller extrapolates it) and velocity. */
static int hitn_bullet_motion(unsigned char *bullet,double now,float position[3],float velocity[3]) {
    unsigned char *controller=NULL;float start[3];double t0=0;
    if(!read_local(bullet+0x200,&controller,8) || !hitn_vtable_is(controller,HITN_LINEAR_VTABLE) ||
       !read_local(controller+0x20,start,12) || !read_local(controller+0x60,&t0,8) || !read_local(controller+0x80,velocity,12)) return 0;
    float dt=(float)(now-t0);
    for(unsigned i=0;i<3;i++) {position[i]=dt*velocity[i]+start[i];if(!isfinite(position[i]) || !isfinite(velocity[i])) return 0;}
    return 1;
}
static double hitn_now(void) {
    if(hitn_now_override) return hitn_now_override;
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0;
    if(!slots || !slots[0] || !read_local((unsigned char*)slots[0]+0x300,&now,8)) return 0;
    return now;
}

/* ---- projectile identities ---- */
static hitn_projectile *hitn_lookup(unsigned char *object,uint64_t runtime,int insert,uint64_t tick) {
    unsigned home=(unsigned)lockstep_mix((uintptr_t)object)&(HITN_TABLE-1),victim=UINT32_MAX;
    for(unsigned n=0;n<64;n++) {
        hitn_projectile *e=&hitn_table[(home+n)&(HITN_TABLE-1)];
        if(e->object==object && e->runtime==runtime) return e;
        if(!insert) {if(!e->object) return NULL;continue;}
        if(!e->object) {victim=(home+n)&(HITN_TABLE-1);break;}
        if(victim==UINT32_MAX && (e->tick+HITN_EVICT_TICKS<tick || (e->object==object && e->runtime!=runtime))) victim=(home+n)&(HITN_TABLE-1);
    }
    if(!insert || victim==UINT32_MAX) return NULL;
    hitn_projectile *e=&hitn_table[victim];memset(e,0,sizeof(*e));e->object=object;e->runtime=runtime;e->tick=tick;return e;
}
static unsigned hitn_next_ordinal(uint64_t shooter,uint64_t tick) {
    if(hitn_ordinal_tick!=tick) {memset(hitn_ordinals,0,sizeof(hitn_ordinals));hitn_ordinal_tick=tick;}
    unsigned home=(unsigned)lockstep_mix(shooter)&(HITN_ORDINALS-1);
    for(unsigned n=0;n<HITN_ORDINALS;n++) {
        hitn_ordinal *o=&hitn_ordinals[(home+n)&(HITN_ORDINALS-1)];
        if(!o->shooter) o->shooter=shooter;
        if(o->shooter==shooter) return o->count++;
    }
    return UINT32_MAX;
}
/* First sight assigns the key; later casts return the same entry. The bullet
 * is the running UpdateFlight's own object, so its header is read directly. */
static hitn_projectile *hitn_identify(unsigned char *bullet,uint64_t tick) {
    uint64_t runtime=*(volatile uint64_t*)(bullet+8);
    if(!runtime || *(uintptr_t volatile*)bullet!=(uintptr_t)(image+HITN_BULLET_VTABLE)) return NULL;
    hitn_projectile *e=hitn_lookup(bullet,runtime,0,tick);
    if(e) return e;
    if(!(e=hitn_lookup(bullet,runtime,1,tick))) {++hitn_full;return NULL;}
    unsigned char *shooter=NULL;unsigned flags=0,limit=0;
    if(!read_local(bullet+0x2a0,&shooter,8) || !hitn_bullet_flags(bullet,&flags,&limit)) flags|=HITN_UNMAPPED;
    e->shooter=shooter?world_identity_portable(hitn_runtime(shooter)):0;e->shooter_slot=hitn_slot_of(shooter);
    if(!e->shooter) flags|=HITN_UNMAPPED;
    if(!(flags&(HITN_UNMAPPED|HITN_BEAM|HITN_PIERCE))) {
        unsigned ordinal=hitn_next_ordinal(e->shooter,tick);
        if(ordinal!=UINT32_MAX) {e->ordinal=ordinal;e->key=hit_projectile_key(tick,e->shooter,0,ordinal);flags|=HITN_KEYED;}
    }
    e->flags=flags;
    if(flags&HITN_KEYED) ++hitn_keyed;else ++hitn_unkeyed;
    return e;
}
static hitn_projectile *hitn_by_key(uint64_t key) {
    if(!key) return NULL;
    for(unsigned i=0;i<HITN_TABLE;i++) if(hitn_table[i].object && hitn_table[i].key==key) return &hitn_table[i];
    return NULL;
}

/* ---- geometry (floats in, floats out; owner-only, never in the receipt) ---- */
static void hitn_local(const float bank[16],const float point[3],float out[3]) {
    float d[3]={point[0]-bank[0],point[1]-bank[1],point[2]-bank[2]};
    for(unsigned j=0;j<3;j++) out[j]=d[0]*bank[4*(j+1)]+d[1]*bank[4*(j+1)+1]+d[2]*bank[4*(j+1)+2];
}
static int hitn_quantize(const float local[3],int16_t point[3]) {
    for(unsigned i=0;i<3;i++) {
        float q=local[i]*8.0f;
        if(!isfinite(q) || q>32767.0f || q<-32767.0f) return 0;
        point[i]=(int16_t)lrintf(q);
    }
    return 1;
}
/* x' = S o D^-1 (x): a zone point relative to the displayed pose D, re-expressed
 * relative to the body pose S. Rotation only for directions (w = 0). */
static void hitn_rebase(const predict_pose *display,const float body[16],const float in[4],float out[4],int direction) {
    double d[3],local[3];
    for(unsigned i=0;i<3;i++) d[i]=in[i]-(direction?0.0:display->p[i]);
    for(unsigned j=0;j<3;j++) local[j]=d[0]*display->r[3*j]+d[1]*display->r[3*j+1]+d[2]*display->r[3*j+2];
    for(unsigned i=0;i<3;i++) out[i]=(float)((direction?0.0:body[i])+local[0]*body[4+i]+local[1]*body[8+i]+local[2]*body[12+i]);
    out[3]=in[3];
}
static void hitn_candidate_add(unsigned kind,unsigned char *bullet,uint64_t key,uint64_t target,const int16_t point[3],uint64_t tick) {
    for(unsigned i=0;i<hitn_queue_count;i++) if(hitn_queue[i].claim.projectile==key) return;
    if(hitn_queue_count>=HITN_QUEUE) {++hitn_candidates_dropped;return;}
    hitn_candidate *c=&hitn_queue[hitn_queue_count++];memset(c,0,sizeof(*c));
    c->claim.kind=kind;c->claim.observe16=(uint16_t)tick;c->claim.projectile=key;c->claim.target=target;
    memcpy(c->claim.point,point,sizeof(c->claim.point));c->bullet=bullet;c->observe=tick;
    if(kind==HIT_INCOMING) ++hitn_claims_in;else ++hitn_claims_out;
}
static uint64_t hitn_slot_saved(unsigned slot) {return slot>=1 && slot<=pilot_slot_count && pilot_slots[slot-1].resolved?pilot_slots[slot-1].saved:0;}

/* Once per tick: the local slot ship's body pose (its read bank before this
 * tick's join) and the pose the player saw for it (previous tick's display). */
static const hitn_owner_view *hitn_owner_now(uint64_t tick) {
    if(hitn_owner.tick==tick) return hitn_owner.valid?&hitn_owner:NULL;
    memset(&hitn_owner,0,sizeof(hitn_owner));hitn_owner.tick=tick;
    unsigned local=stats.slot;
    if(local<1 || local>pilot_slot_count || !pilot_slots[local-1].resolved) return NULL;
    hitn_owner.own=pilot_slots[local-1].object;
    if(!hitn_bank(hitn_owner.own,hitn_owner.body)) return NULL;
    if(predict_armed && local<=PILOT_SLOTS && predict_slots[local-1].enabled && predict_slots[local-1].display_tick+1==tick) {
        hitn_owner.display=predict_slots[local-1].display;hitn_owner.moved=1;
    }
    hitn_owner.valid=1;return &hitn_owner;
}
/* ---- the cast wrapper (MoveWorker, inside UpdateFlight) ---- */
static void hitn_after_cast(hitn_cast_fn original,unsigned char *bullet,void *system,unsigned char *result,
                            const float *origin,const float *ray,const hitn_cast_tail *tail) {
    uint64_t tick=stats.issued;
    hitn_projectile *e=hitn_identify(bullet,tick);
    if(!e || !(e->flags&HITN_KEYED)) return;
    /* The result block lives in UpdateFlight's frame (our caller's stack). */
    unsigned shooter_slot=e->shooter_slot;
    float fraction=*(volatile float*)(result+0x20);unsigned char *component=*(unsigned char*volatile*)(result+0x28);
    /* Suppression: identical on every client. */
    if(fraction>=0.0f && component) {
        unsigned target_slot=hitn_slot_of(component);
        if(target_slot || (shooter_slot && hitn_ship_of(component))) {
            *(volatile float*)(result+0x20)=-1.0f;*(unsigned char*volatile*)(result+0x28)=NULL;
            if(target_slot) ++hitn_suppressed_in;else ++hitn_suppressed_out;
        }
    }
    /* Owner evaluation: only this client's own slot ship, once per projectile. */
    const hitn_owner_view *view=hitn_owner_now(tick);
    if(!view || (e->flags&HITN_CLAIMED)) return;
    unsigned local=stats.slot;unsigned char *own=view->own;const float *body=view->body;
    unsigned char scratch[0x40];float o2[4],r2[4];
    if(shooter_slot!=local) {
        float d2=0;for(unsigned i=0;i<3;i++) {float d=origin[i]-body[i];d2+=d*d;}
        float reach=HITN_RANGE+fabsf(ray[0])+fabsf(ray[1])+fabsf(ray[2]);
        if(!(d2<=reach*reach)) return;
        if(view->moved) {hitn_rebase(&view->display,body,origin,o2,0);hitn_rebase(&view->display,body,ray,r2,1);}
        else {memcpy(o2,origin,16);memcpy(r2,ray,16);}
        memset(scratch,0,sizeof(scratch));*(float*)(scratch+0x20)=-1.0f;
        original(system,scratch,o2,r2,HITN_TAIL(tail));++hitn_eval_in;
        unsigned char *hit=*(unsigned char**)(scratch+0x28);
        if(*(float*)(scratch+0x20)>=0.0f && hit && hitn_slot_of(hit)==local) {
            float local_point[3];int16_t q[3];hitn_local(body,(const float*)scratch,local_point);
            if(hitn_quantize(local_point,q)) {hitn_candidate_add(HIT_INCOMING,bullet,e->key,pilot_slots[local-1].saved,q,tick);e->flags|=HITN_CLAIMED;}
        }
    } else {
        float position[3],velocity[3];
        if(!hitn_bullet_motion(bullet,hitn_now(),position,velocity)) return;
        float shift=(float)pilot_delay*(float)PREDICT_DT;
        for(unsigned i=0;i<3;i++) o2[i]=origin[i]+velocity[i]*shift;
        o2[3]=origin[3];
        memset(scratch,0,sizeof(scratch));*(float*)(scratch+0x20)=-1.0f;
        original(system,scratch,o2,ray,HITN_TAIL(tail));++hitn_eval_out;
        unsigned char *hit=*(unsigned char**)(scratch+0x28),*ship=NULL;float bank[24];
        if(*(float*)(scratch+0x20)>=0.0f && hit && (ship=hitn_ship_of(hit)) && ship!=own && hitn_bank(ship,bank)) {
            unsigned target_slot=hitn_slot_of(ship);float local_point[3];int16_t q[3];
            uint64_t target=target_slot && pilot_slots[target_slot-1].renewed?pilot_slots[target_slot-1].saved:world_identity_portable(hitn_runtime(ship));
            hitn_local(bank,(const float*)scratch,local_point);
            if(target && hitn_quantize(local_point,q)) {hitn_candidate_add(HIT_OUTGOING,bullet,e->key,target,q,tick);e->flags|=HITN_CLAIMED;}
        }
    }
}
static uintptr_t hitn_cast(void *system,unsigned char *result,const float *origin,const float *ray,uintptr_t a5,uintptr_t a6,uintptr_t a7,
                           uintptr_t a8,uintptr_t a9,uintptr_t a10,uintptr_t a11,uintptr_t a12,uintptr_t a13,uintptr_t a14,uintptr_t a15) {
    hitn_cast_fn original=(hitn_cast_fn)(image+HITN_CAST);
    const hitn_cast_tail tail={{a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15}};
    if(!InterlockedCompareExchange(&hitn_live,0,0) || GetCurrentThreadId()!=ui_thread || !hitn_page)
        {++hitn_foreign;return original(system,result,origin,ray,HITN_TAIL(&tail));}
    unsigned char *bullet=*(unsigned char*volatile*)(hitn_page+0x1000); /* R14 at the call */
    ++hitn_casts;
    uintptr_t value=original(system,result,origin,ray,HITN_TAIL(&tail)); /* exactly once, all fifteen arguments */
    if(bullet && result) hitn_after_cast(original,bullet,system,result,origin,ray,&tail);
    return value;
}

/* ---- install / check / restore ---- */
static int hitn_pins(void) {
    if(!hitn_pins_checked) {
        hitn_pins_checked=1;
        for(unsigned i=0;i<sizeof(hitn_pins_list)/sizeof(*hitn_pins_list);i++)
            if(!hash_ok(image+hitn_pins_list[i].rva,hitn_pins_list[i].size,hitn_pins_list[i].sha)) hitn_pins_checked=-1;
        uintptr_t kill=0;
        if(!read_local(image+HITN_BULLET_VTABLE+HITN_KILL_SLOT,&kill,8) || kill!=(uintptr_t)(image+HITN_KILL)) hitn_pins_checked=-1;
    }
    return hitn_pins_checked==1;
}
/* Stub: mov [rip->data+0], r14 ; jmp [rip->data+8]. */
static int hitn_stub(unsigned char *code,unsigned char *data,unsigned char out[13]) {
    intptr_t store=(intptr_t)data-(intptr_t)(code+7),jump=(intptr_t)(data+8)-(intptr_t)(code+13);
    if(store<INT32_MIN || store>INT32_MAX || jump<INT32_MIN || jump>INT32_MAX) return 0;
    int32_t s=(int32_t)store,j=(int32_t)jump;
    out[0]=0x4c;out[1]=0x89;out[2]=0x35;memcpy(out+3,&s,4);out[7]=0xff;out[8]=0x25;memcpy(out+9,&j,4);return 1;
}
static unsigned char *hitn_page_near(unsigned char *site) {
    uintptr_t at=(uintptr_t)site&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,HITN_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t rel=(intptr_t)p-(intptr_t)(site+5);
            if(rel>=INT32_MIN && rel<=INT32_MAX) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int hitn_swap(const unsigned char expected[16],const unsigned char value[16]) {
    unsigned char *p=image+HITN_BLOCK;DWORD old,ignored;
    if(!VirtualProtect(p,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 e,v;memcpy(&e,expected,16);memcpy(&v,value,16);
    int ok=__sync_val_compare_and_swap((unsigned __int128*)p,e,v)==e;
    VirtualProtect(p,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p,16);return ok;
}
static int hitn_stub_intact(void) {
    unsigned char want[13],now[13];void *handler=NULL;
    return hitn_page && hitn_stub(hitn_page,hitn_page+0x1000,want) && read_local(hitn_page,now,13) && !memcmp(want,now,13) &&
        read_local(hitn_page+0x1008,&handler,8) && handler==(void*)hitn_cast;
}
static int hitn_install(void) {
    if(hitn_installed) return 1;
    unsigned char now[16];
    if(!read_local(image+HITN_BLOCK,now,16) || memcmp(now,hitn_block_original,16)) {hitn_stage="block";return 0;}
    if(!hitn_page && !(hitn_page=hitn_page_near(image+HITN_SITE))) {hitn_stage="page";return 0;}
    unsigned char stub[13];void *handler=(void*)hitn_cast;DWORD old;
    if(!hitn_stub(hitn_page,hitn_page+0x1000,stub)) {hitn_stage="stub";return 0;}
    memcpy(hitn_page,stub,13);memset(hitn_page+0x1000,0,8);memcpy(hitn_page+0x1008,&handler,8);
    if(!VirtualProtect(hitn_page,0x1000,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),hitn_page,0x1000)) {hitn_stage="protect";return 0;}
    int32_t rel=(int32_t)((intptr_t)hitn_page-(intptr_t)(image+HITN_SITE+5));
    memcpy(hitn_block_patched,hitn_block_original,16);memcpy(hitn_block_patched+2,&rel,4);
    if(!hitn_swap(hitn_block_original,hitn_block_patched)) {hitn_stage="cas";return 0;}
    hitn_installed=1;hitn_restored=0;return 1;
}
static int hitn_owned(void) {
    unsigned char now[16];
    return !hitn_installed || (read_local(image+HITN_BLOCK,now,16) && !memcmp(now,hitn_block_patched,16) && hitn_stub_intact());
}
static int hitn_check(void) {
    if(!hitn_armed) return 1;
    if(InterlockedCompareExchange64(&hitn_failures,0,0)) return 0;
    if(!hitn_owned()) {hitn_stage="site_changed";InterlockedIncrement64(&hitn_failures);return 0;}
    return 1;
}
/* Stop: the site goes back by CAS; a foreign value is left in place. The
 * stub page stays allocated (a call already past the redirect may still be
 * returning through the handler, which then passes through). */
static void hitn_restore(void) {
    InterlockedExchange(&hitn_live,0);
    if(!hitn_installed) return;
    if(hitn_swap(hitn_block_patched,hitn_block_original)) {hitn_installed=0;hitn_restored=1;}
    else hitn_stage="restore_foreign";
}
static int hitn_arm(int policy) {
    if(hitn_armed) return policy==hitn_policy && stats.active;
    if(policy<HIT_POLICY_SHOOTER || policy>HIT_POLICY_FIRST || !pilot_armed || !stats.active || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !world_identity_installed || !pilot_delay ||
       GetModuleHandleW(L"x4mp_combat.dll") || !hitn_pins()) {hitn_stage="refused";return 0;}
    hit_reset();memset(hitn_table,0,sizeof(hitn_table));hitn_queue_count=0;hitn_ordinal_tick=UINT64_MAX;
    memset(&hitn_owner,0,sizeof(hitn_owner));hitn_owner.tick=UINT64_MAX;
    if(!hitn_install()) return 0;
    hitn_policy=policy;hitn_armed=1;hitn_stage="armed";
    InterlockedExchange(&hitn_live,1);return 1;
}

/* ---- slot 6: proposal, admission, resolution ---- */
/* This tick's proposal: the oldest queued claims that fit in budget bytes
 * (at most HIT_CLAIMS_MAX); claims whose observe tick would leave the window
 * before admission are dropped (counted). The rest stay queued. */
static int hitn_proposal(uint64_t tick,unsigned char *out,size_t capacity,size_t *length) {
    *length=0;
    if(!hitn_armed) return 1;
    unsigned kept=0;
    for(unsigned i=0;i<hitn_queue_count;i++) {
        if(hitn_queue[i].observe+HIT_WINDOW<tick) {++hitn_queue_expired;continue;}
        hitn_queue[kept++]=hitn_queue[i];
    }
    hitn_queue_count=kept;
    size_t fit=capacity>1?(capacity-1)/HIT_CLAIM_BYTES:0;
    unsigned n=hitn_queue_count<HIT_CLAIMS_MAX?hitn_queue_count:HIT_CLAIMS_MAX;
    if(n>fit) n=(unsigned)fit;
    if(!n) return 1;
    hit_claim claims[HIT_CLAIMS_MAX];
    for(unsigned i=0;i<n;i++) claims[i]=hitn_queue[i].claim;
    *length=hit_payload_encode(claims,n,out,capacity);
    if(!*length) return 0;
    memmove(hitn_queue,hitn_queue+n,(hitn_queue_count-n)*sizeof(*hitn_queue));hitn_queue_count-=n;
    ++hitn_proposals;return 1;
}
static int hitn_admit(unsigned slot,const unsigned char *payload,unsigned length,uint64_t tick) {
    hit_claim claims[HIT_CLAIMS_MAX];unsigned count=0;
    if(!hitn_armed || slot<1 || slot>pilot_slot_count || !hit_payload_decode(payload,length,claims,&count)) {++hitn_admit_refused;return 0;}
    hitn_admitted+=hit_stage(slot,claims,count,tick,pilot_delay,wire_lookahead);return 1;
}
/* World view for lockstep_hit_claims.h validation, from simulated state only. */
typedef struct {unsigned char *bullet,*target;uint64_t zone,tick;float forward[3];double now;} hitn_world_context;
static int hitn_world_projectile(void *context,uint64_t key,hit_projectile *out) {
    hitn_world_context *c=context;hitn_projectile *e=hitn_by_key(key);float position[3],velocity[3];
    unsigned char *zone=NULL,*weapon=NULL;int hits=0;unsigned flags=0,limit=0;
    c->bullet=NULL;
    if(!e || !(e->flags&HITN_KEYED) || hitn_find(e->runtime)!=e->object || !hitn_vtable_is(e->object,HITN_BULLET_VTABLE) ||
       !read_local(e->object+0x280,&hits,4) || !hitn_bullet_flags(e->object,&flags,&limit) || (flags&(HITN_BEAM|HITN_PIERCE)) ||
       (limit && (unsigned)hits>=limit) || !hitn_bullet_motion(e->object,c->now,position,velocity) || !read_local(e->object+0x70,&zone,8)) return 0;
    read_local(e->object+0x2a0,&weapon,8);
    unsigned slot=hitn_slot_of(weapon);
    out->shooter=slot?hitn_slot_saved(slot):e->shooter;
    double speed=sqrt((double)velocity[0]*velocity[0]+(double)velocity[1]*velocity[1]+(double)velocity[2]*velocity[2]);
    for(unsigned i=0;i<3;i++) {
        out->position[i]=llround((double)position[i]*1000.0);out->velocity[i]=llround((double)velocity[i]*1000.0*PREDICT_DT);
        c->forward[i]=speed>0?(float)(velocity[i]/speed):0.0f;
    }
    c->bullet=e->object;c->zone=(uint64_t)(uintptr_t)zone;return 1;
}
static int hitn_world_target(void *context,uint64_t id,hit_target *out) {
    hitn_world_context *c=context;float bank[24];unsigned char *zone=NULL;
    c->target=NULL;
    unsigned char *object=hitn_find(hitn_runtime_of(id));
    if(!object || !hitn_bank(object,bank) || !read_local(object+0x70,&zone,8) || (uint64_t)(uintptr_t)zone!=c->zone) return 0;
    double speed=sqrt((double)bank[16]*bank[16]+(double)bank[17]*bank[17]+(double)bank[18]*bank[18]); /* frame-free magnitude */
    if(!isfinite(speed)) return 0;
    for(unsigned i=0;i<3;i++) {
        if(!isfinite(bank[i])) return 0;
        out->position[i]=llround((double)bank[i]*1000.0);out->velocity[i]=0;
        for(unsigned j=0;j<3;j++) {float r=bank[4*(i+1)+j];if(!isfinite(r) || fabsf(r)>1.01f) return 0;out->rows[3*i+j]=(int32_t)lrint((double)r*1073741824.0);}
    }
    out->velocity[0]=(int64_t)ceil(speed*1000.0*PREDICT_DT*1.7320508075688772);out->radius=HITN_RADIUS_MM;
    c->target=object;return 1;
}
static uint64_t hitn_world_slot_ship(void *context,unsigned slot) {(void)context;return hitn_slot_saved(slot);}
static int hitn_world_apply(void *context,const hit_staged *s,const int64_t world_mm[3]) {
    hitn_world_context *c=context;(void)world_mm;
    if(!c->bullet || !c->target) return 0;
    float bank[24];if(!hitn_bank(c->target,bank)) return 0;
    float info[16]={0},normal[4]={0};
    for(unsigned i=0;i<3;i++) info[i]=(float)s->claim.point[i]*0.125f;
    info[4]=info[9]=info[14]=1.0f; /* identity rotation rows; w = 0 */
    for(unsigned j=0;j<3;j++) normal[j]=-(c->forward[0]*bank[4*(j+1)]+c->forward[1]*bank[4*(j+1)+1]+c->forward[2]*bank[4*(j+1)+2]);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    volatile uint64_t *word=hitn_word_override?hitn_word_override:tls && tls[0]?(volatile uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!word) return 0;
    const uint64_t saved=*word;volatile int done=0;int hits=0;
    __try {
        *word=lockstep_seed(HIT_DOMAIN,c->tick,s->claim.projectile,s->claim.target,s->slot,0x494e4a); /* "INJ" */
        ((hitn_onhit_fn)(image+HITN_ONHIT))(c->bullet,c->target,info,normal,c->now);
        if(read_local(c->bullet+0x280,&hits,4)) {++hits;hitn_write(c->bullet+0x280,&hits,4);}
        ((hitn_cancel_fn)(image+HITN_CANCEL))(c->bullet,0xeb);
        ((hitn_kill_fn)(image+HITN_KILL))(c->bullet,2,c->target,c->now);
        done=1;
    } __finally {
        DWORD error=GetLastError();
        *word=saved; /* injected draws never reach the simulation stream */
        if(AbnormalTermination()) {InterlockedIncrement64(&hitn_failures);hitn_stage="inject_unwound";}
        SetLastError(error);
    }
    if(done) ++hitn_applied;
    return done;
}
static int hitn_resolve(uint64_t tick,char *out,size_t capacity) {
    if(!hitn_armed) return 0;
    hitn_world_context context={.tick=tick,.now=hitn_now()};
    hit_world world={&context,hitn_world_projectile,hitn_world_target,hitn_world_slot_ship,hitn_world_apply};
    hit_outcome outcomes[HIT_STAGED_MAX];uint64_t receipt=0;
    unsigned n=hit_resolve(&world,tick,pilot_slot_count,pilot_delay,wire_lookahead,hitn_policy,outcomes,HIT_STAGED_MAX,&receipt);
    hitn_resolved+=n;hitn_receipt+=receipt;hitn_last_tick=tick;++hitn_frames;
    int length=snprintf(out,capacity,"HITS_1:%llu:%u:%016llx",(unsigned long long)tick,n,(unsigned long long)receipt);
    for(unsigned i=0;length>0 && (size_t)length<capacity && i<n;i++)
        length+=snprintf(out+length,capacity-(size_t)length,",%u/%016llx/%016llx/%s",outcomes[i].slot,
            (unsigned long long)outcomes[i].projectile,(unsigned long long)outcomes[i].target,hit_result_names[outcomes[i].result]);
    return length>0 && (size_t)length<capacity?length:0;
}
static int hitn_status(char *out,size_t capacity) {
    static const char *policies[4]={"none","shooter","victim","first"};
    int n=snprintf(out,capacity,"armed=%d installed=%d live=%ld policy=%s casts=%llu foreign=%llu keyed=%llu unkeyed=%llu full=%llu "
        "suppressed_in=%llu suppressed_out=%llu eval_in=%llu eval_out=%llu claims_in=%llu claims_out=%llu dropped=%llu proposals=%llu "
        "admitted=%llu admit_refused=%llu resolved=%llu applied=%llu frames=%llu last=%llu receipt=%016llx overflow=%llu queued=%u expired=%llu",
        hitn_armed,hitn_installed,hitn_live,policies[hitn_policy>=0 && hitn_policy<=3?hitn_policy:0],
        (unsigned long long)hitn_casts,(unsigned long long)hitn_foreign,(unsigned long long)hitn_keyed,(unsigned long long)hitn_unkeyed,
        (unsigned long long)hitn_full,(unsigned long long)hitn_suppressed_in,(unsigned long long)hitn_suppressed_out,
        (unsigned long long)hitn_eval_in,(unsigned long long)hitn_eval_out,(unsigned long long)hitn_claims_in,
        (unsigned long long)hitn_claims_out,(unsigned long long)hitn_candidates_dropped,(unsigned long long)hitn_proposals,
        (unsigned long long)hitn_admitted,(unsigned long long)hitn_admit_refused,(unsigned long long)hitn_resolved,
        (unsigned long long)hitn_applied,(unsigned long long)hitn_frames,(unsigned long long)hitn_last_tick,
        (unsigned long long)hitn_receipt,(unsigned long long)hit_stage_overflow,hitn_queue_count,(unsigned long long)hitn_queue_expired);
    for(unsigned r=0;n>0 && (size_t)n<capacity && r<HIT_RESULTS;r++)
        n+=snprintf(out+n,capacity-(size_t)n," %s=%llu",hit_result_names[r],(unsigned long long)hit_counts[r]);
    if(n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n," restored=%d stage=%s failures=%lld",hitn_restored,hitn_stage,
        (long long)hitn_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
