/* Stock piloting, DEFAULT OFF, stop reason 155 (native_gate_pilot_stock): step 2
 * of reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt. Slot 1, the ship every
 * client sits in, flies with X4's own player control path on every client, from
 * the capturing client's input delayed through the wire. It reuses the
 * native-controls pieces of lockstep_pilot_native.h (dispatcher, cursor and SPACE
 * entries) in their 'stock' mode; the authority predicate and the transform
 * routers stay stock (the PlayerController moves the ship itself).
 *   capture  the client whose slot is the stock slot (capture source 'stock'):
 *            the dispatchers record every control id of the replay set as (kind,
 *            id, value) in dispatch order instead of running it; the SPACE
 *            context edge (menus, map) becomes an event in the same order; the
 *            three outputs of cursor/direct mouse steering (d88a10, 2f4b738..740)
 *            of the frame are recorded. Presentation ids (camera, menus, map,
 *            targets, walking, mouse steering modes) run locally as before.
 *   wire     one record per tick, input section 'stock' (kind 0x53).
 *   replay   every client, in path_release of tick c+d (MoveWorker, pathing pass
 *            joined, slots 3/4/6 of the previous frame complete): SPACE edges
 *            through the simulation half of d88710 (flight control 2f4b828, the
 *            bank fields it zeroes or times, d9c250 / d9c590, the Flight Stopped /
 *            Started events; the input object's flags 9062/9064 stay this
 *            client's), each recorded id through every handler the multimap holds
 *            for it (only the stock handlers d86c80, d870b0, d87f70, 71bac0,
 *            71c640 are accepted), and d88a10's output in this frame's d95180
 *            replaced by the recorded values. d95180, the control scheme, the
 *            solver d93440, boost, travel / long-range scan / SETA (md/modes.xml)
 *            and flight assist then run stock on every client.
 *   local    a client that does not capture runs no control id of the replay or
 *            refused sets (counted as dropped); every client keeps the input
 *            object side of its own SPACE edges and its own d9cc30 refresh.
 * Refused everywhere (logged): fire and weapon groups (aim is the camera pick,
 * 717ed0), match speed and scan mode (they follow the soft target, which is
 * still local), autopilot, guidance, dock/undock, get up, squad orders, deploys,
 * countermeasures and drones (object creation), cover/stance, the option
 * toggles (autoroll, aim assist, reduced speed) and the debug features.
 * Settings the stock path reads that are user options (autoroll, stop ship in
 * menus, aim assist, collision assist, boost toggle, force shooting at cursor,
 * throttle axis mode, invert flags, roll mix, hold/mix timings, wheel step, aim
 * assist angle and range, the mouse response curve sampled through 14e0180)
 * form a fingerprint in the per-tick receipt: differing settings are a relay
 * desync at the first tick.
 * Aim (live finding st3-P15C: slot 1's gimbal weapons posed from each client's
 * camera). Weapon::Move asks its controller for the aim point at 8141c2 (vtable
 * slot 21; for the player's guns U::PlayerShootController::vfunc21 7e8650).
 * 7e8650 reads the camera pick of 717ed0 (player+0x6c60 point in the frame of the
 * player's class 0x6d ancestor, +0x6c80 distance, +0x6c88 picked component) or,
 * without a pick or on the own ship, the cursor ray of the local camera (719750),
 * and decides manual aim from the input object's mouse steering flag (+0x9053,
 * vfunc15 7e8e60, also aim assist mode 2). Every other input is simulation or a
 * fingerprinted option. So the record carries slot 1's aim point in the ship's
 * frame and its +0x9053 (capture: after this frame's 717ed0, from the pick or
 * 719750 relative to the ship); every client replays it: the PlayerShootController
 * vtable slot 21 is replaced (from the first pass of the session) by a wrapper
 * that, around each stock call on the frame thread, writes the replayed point
 * (transformed into the player's class 0x6d frame), a valid distance, a picked
 * component that is not the ship and the replayed +0x9053, and puts the four
 * fields back afterwards. Before the first replayed record (and for a record
 * without aim) the aim is neutral: straight ahead, no mouse steering.
 * Slots 2..N (native_gate_pilot_stock_slots, lockstep_pilot_slots.h, stop reason
 * 161): every stock slot's own client captures its record the same way (no aim:
 * the aim stays slot 1's; travel, long-range scan and SETA refused for slots
 * 2..N, they are MD activities of player.entity); every client stages every stock
 * slot's record per slot, pstk_frame keeps slot k's record of the tick and the
 * slot scope replays it (pstk_slot_replay) with slot k's player as the singleton
 * and slot k's bank; d88a10's outputs and the view answer per scope. */
#define PSTK_STOP_REASON 155u
#define PSTK_VERSION 3u
#define PSTK_HEAD 43u
#define PSTK_EVENT 6u
#define PSTK_EVENTS_MAX 31u
#define PSTK_BYTES_MAX (PSTK_HEAD+PSTK_EVENT*PSTK_EVENTS_MAX) /* 229: the section fits a 240-byte blob alone */
#define PSTK_FLAG_ACTIVE 1u   /* this tick ended with the capturing client's SPACE context active */
#define PSTK_FLAG_CURSOR 2u   /* the cursor values are this frame's d88a10 outputs */
#define PSTK_FLAG_AIM 4u      /* the aim point (ship frame) and mouse steering flag of this frame */
#define PSTK_FLAG_VIEW 8u     /* the view of this frame: cockpit/floating bits, FOV factor, soft target */
#define PSTK_FLAG_FIRE 16u    /* primary fire held (only with native_gate_pilot_fire, lockstep_pilot_fire.h) */
#define PSTK_FLAGS_KNOWN (PSTK_FLAG_ACTIVE|PSTK_FLAG_CURSOR|PSTK_FLAG_AIM|PSTK_FLAG_VIEW|PSTK_FLAG_FIRE)
#define PSTK_SHOOT_VTABLE 0x2b7d1a8u  /* U::PlayerShootController */
#define PSTK_AIM_SLOT 21u
#define PSTK_AIM_RVA 0x7e8650u        /* PlayerShootController::vfunc21 */
#define PSTK_AIM_ZONE_CLASS 0x6d
#define PSTK_AIM_NEUTRAL 1000.0f      /* straight ahead (+z of the ship) [I] */
#define PSTK_RING 64u
#define PSTK_DOMAIN 0x5053544f434b3156ULL /* "PSTOCK1V" */
enum {PSTK_ACTION=0,PSTK_STATE=1,PSTK_RANGE=2,PSTK_SPACE=3,PSTK_KINDS=4};
enum {PSTK_LOCAL=0,PSTK_REPLAY=1,PSTK_REFUSE=2};
typedef struct {unsigned char kind;uint16_t id;uint32_t value;} pstk_event; /* state/space 0|1, range float bits, action 0 */
typedef struct {unsigned char flags;uint16_t capture16;float cursor[3];float aim[3];unsigned char steer,view;float fov;uint64_t target;
    unsigned count;pstk_event events[PSTK_EVENTS_MAX];} pstk_record;
typedef struct {uint64_t tick;unsigned length,present;unsigned char bytes[PSTK_BYTES_MAX];} pstk_staged;
typedef struct {uint64_t capture,apply;unsigned length,used;unsigned char bytes[PSTK_BYTES_MAX];} pstk_local;
typedef void (*pstk_handoff_fn)(void *,char);   /* d9c590(controller, authority) */
typedef void (*pstk_release_fn)(void *);        /* d9c250(controller) */
typedef void (*pstk_event_fn)(void *);          /* 70eb10 / 70ec00 (player): Flight Started / Stopped */
typedef float (*pstk_curve_fn)(void *,float);   /* 14e0180 */
typedef unsigned char (*pstk_aim_fn)(void *,float *,float *,void *,void *,void *,float,float,unsigned char,unsigned char,
    unsigned char,unsigned char,unsigned char);  /* PlayerShootController::vfunc21 7e8650 */
typedef void (*pstk_transform_fn)(void *,float *,void *,uint64_t); /* 3dde10: from's transform in to's frame (pos, three axes) */
typedef void **(*pstk_ship_fn)(void *,void **);  /* 55aeb0: the player's ship */
typedef void (*pstk_ray_fn)(void *,float *,void *,uint64_t); /* 719750: the camera cursor ray point relative to a component */
typedef struct {
    pstk_handoff_fn handoff;pstk_release_fn release;pstk_event_fn started,stopped;pstk_curve_fn curve;
    unsigned char (*authority)(void *);         /* d8dd50, stock */
    pstk_aim_fn aim;pstk_transform_fn transform;pstk_ship_fn ship;pstk_ray_fn ray;
    unsigned char *(*zone)(unsigned char *);    /* the player's first class 0x6d ancestor (IsClass, vtable +0x11e8) */
} pstk_fns;
static pstk_fns pstk_fn;
static int pstk_capturing;                      /* this client records its own slot's input */
static unsigned pstk_slot;                      /* 1: the slot flown through the canonical player */
static unsigned pstk_capture_slot;              /* the slot this client records (1 in slot-1 mode) */
static unsigned pstk_slot_mask;                 /* bit s-1: slot s is flown through the stock path */
static unsigned pstk_scope_slot;                /* 2..N inside a slot scope (lockstep_pilot_slots.h), else 0 */
static pstk_record pstk_build;                  /* the tick being captured */
static float pstk_cursor_live[3];
static int pstk_cursor_live_set,pstk_local_active=-1;
static pstk_staged pstk_ring[PILOT_SLOTS][PSTK_RING];
/* Slots 2..N: this tick's record, replayed in the slot's scope after the canonical update. */
typedef struct {uint64_t tick,hash;pstk_record r;unsigned flight,flight_known;} pstk_slot_frame;
static pstk_slot_frame pstk_slot_frames[PILOT_SLOTS];
static pstk_local pstk_locals[PSTK_RING];
static float pstk_cursor_replay[3];
static uint64_t pstk_cursor_tick,pstk_capture_start,pstk_first_apply,pstk_frame_tick,pstk_frame_hash,pstk_settings;
static unsigned pstk_frame_events,pstk_flight,pstk_flight_known;
static volatile LONG64 pstk_recorded,pstk_dropped,pstk_replayed,pstk_records,pstk_staged_count,pstk_space_local,pstk_space_replayed,
    pstk_cursor_calls,pstk_injected,pstk_overflows,pstk_max_events,pstk_unfocused;
/* Aim: the replayed sample of this tick, the vtable slot, counters. */
static int pfire_armed;       /* lockstep_pilot_fire.h */
static int pstk_focused(void);
/* The trigger of the record applied at each slot's last tick (native_gate_pilot_fire). */
static uint64_t pstk_fire_tick[PILOT_SLOTS];
static unsigned char pstk_fire_on[PILOT_SLOTS];
static float pstk_aim_replay[3];
static unsigned char pstk_aim_steer_replay;
static int pstk_aim_valid_replay,pstk_aim_installed;
static uint64_t pstk_aim_tick;
static volatile LONG pstk_aim_on;
static volatile LONG64 pstk_aim_calls,pstk_aim_replaced,pstk_aim_neutral,pstk_aim_samples,pstk_aim_sample_failures,pstk_aim_foreign,
    pstk_aim_scoped;
#include "lockstep_pilot_stock_view.h"
static const uint16_t pstk_replay_actions[]={0x7b,0x12f,0x131,0xe1,0xdd,0x105,0x148};
static const uint16_t pstk_replay_states[]={2,3,4,5,6,7,8,9,0xd,0xe,0xf,0x10,0x11,0x12,0x13,0x5a,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x50,0x85};
static const uint16_t pstk_replay_ranges[]={1,2,3,4,5,6,0xa};
static const uint16_t pstk_refuse_actions[]={0x130,0xb3,0x13f,0xaf,0xd3,0x115,0x133,0x149,0x157,0x158,0x159,0x15a,0x15b,0x15c,0x15d,0x15e,
    0x15f,0x160,0x179,0x17d,0x122,0x123,0x8,0x9,0xa,0xb,0x8b,0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0x9b,0x9c,0x9d,0x9e,0x141,0x14a,0x14b,0x176,0x147,0x81,0x16c,0x16d,0xd0,0x142,0x143,0x107};
static const uint16_t pstk_refuse_states[]={0x5b,0x18,0x19,0x30,0x31};
static const uint16_t pstk_refuse_ranges[]={0x11,0x12,0x13,0x14};
static int pstk_listed(const uint16_t *list,unsigned count,int id) {
    for(unsigned i=0;i<count;i++) if(list[i]==id) return 1;
    return 0;
}
#define PSTK_COUNT(a) (sizeof(a)/sizeof(*(a)))
static unsigned pstk_verdict(unsigned kind,int id) {
    /* Match speed follows the soft target: with the target replayed it can be replayed too. */
    if(kind==PSTK_STATE && id==0x5b && (uirep_families&UIREP_FAMILY_TARGET)) return PSTK_REPLAY;
    if(kind==PSTK_ACTION) return pstk_listed(pstk_replay_actions,PSTK_COUNT(pstk_replay_actions),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_actions,PSTK_COUNT(pstk_refuse_actions),id)?PSTK_REFUSE:PSTK_LOCAL;
    if(kind==PSTK_STATE) return pstk_listed(pstk_replay_states,PSTK_COUNT(pstk_replay_states),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_states,PSTK_COUNT(pstk_refuse_states),id)?PSTK_REFUSE:PSTK_LOCAL;
    if(kind==PSTK_RANGE) return pstk_listed(pstk_replay_ranges,PSTK_COUNT(pstk_replay_ranges),id)?PSTK_REPLAY:
        pstk_listed(pstk_refuse_ranges,PSTK_COUNT(pstk_refuse_ranges),id)?PSTK_REFUSE:PSTK_LOCAL;
    return PSTK_LOCAL;
}
/* Slots 2..N run in a scope whose singleton is the slot's own player: every handler
 * then acts on the slot's ship, except the MD activities (travel, long range scan,
 * SETA: md/modes.xml listens on player.entity, which stays slot 1's for MD). */
static const uint16_t pstk_slot_refused_actions[]={0x12f,0x131,0xe1};
static unsigned pstk_verdict_slot(unsigned kind,int id,unsigned slot) {
    unsigned v=pstk_verdict(kind,id);
    if(slot>=2 && v==PSTK_REPLAY && kind==PSTK_ACTION && pstk_listed(pstk_slot_refused_actions,PSTK_COUNT(pstk_slot_refused_actions),id) &&
       !(id==0x12f && (uirep_families&UIREP_FAMILY_TRAVEL))) /* travel: the UI replay's adapter acts on the slot's player */
        return PSTK_REFUSE;
    return v;
}
static int pstk_slot_record_ok(const pstk_record *r,unsigned slot) {
    for(unsigned i=0;i<r->count;i++)
        if(r->events[i].kind!=PSTK_SPACE && pstk_verdict_slot(r->events[i].kind,r->events[i].id,slot)!=PSTK_REPLAY) return 0;
    return slot<2 || !(r->flags&PSTK_FLAG_AIM);
}
/* The stock handlers a replayed id may reach (anything else registered under a
 * replay id fails closed); production pointers are image+RVA (pnat_use_image). */
static const unsigned pstk_handlers[]={0xd86c80,0xd870b0,0xd87f70,0x71bac0,0x71c640};
#define PSTK_HANDLERS (sizeof(pstk_handlers)/sizeof(*pstk_handlers))
static const void *pstk_handler_ptrs[PSTK_HANDLERS];
static int pstk_handler_ok(const void *handler) {
    for(unsigned i=0;i<PSTK_HANDLERS;i++) if(handler && handler==pstk_handler_ptrs[i]) return 1;
    return 0;
}

/* Record v2, little endian: 0 version | 1 flags | 2..3 low 16 bits of the capture
 * tick | 4..15 cursor outputs, three float32 (zero bits without the cursor flag) |
 * 16..27 aim point in the ship's frame, three float32, and 28 the mouse steering
 * flag 0|1 (zero without the aim flag) | 29 view bits (1 cockpit, 2 floating),
 * 30..33 FOV factor float32, 34..41 soft target u64 (portable ID, 1 = unportable,
 * 0 = none; all zero without the view flag) | 42 event count n (0..31) | then n
 * events of 6 bytes: u16 kind<<14|id (id < 512), u32 value (action 0, state and
 * SPACE 0|1, range float32 bits, finite). */
static unsigned pstk_encode(const pstk_record *r,unsigned char *out) {
    out[0]=PSTK_VERSION;out[1]=r->flags;out[2]=(unsigned char)r->capture16;out[3]=(unsigned char)(r->capture16>>8);
    for(unsigned i=0;i<3;i++) {uint32_t v;memcpy(&v,&r->cursor[i],4);for(unsigned b=0;b<4;b++) out[4+4*i+b]=(unsigned char)(v>>(8*b));}
    for(unsigned i=0;i<3;i++) {uint32_t v;memcpy(&v,&r->aim[i],4);for(unsigned b=0;b<4;b++) out[16+4*i+b]=(unsigned char)(v>>(8*b));}
    out[28]=r->steer;out[29]=r->view;
    {uint32_t v;memcpy(&v,&r->fov,4);for(unsigned b=0;b<4;b++) out[30+b]=(unsigned char)(v>>(8*b));}
    for(unsigned b=0;b<8;b++) out[34+b]=(unsigned char)(r->target>>(8*b));
    out[42]=(unsigned char)r->count;
    for(unsigned i=0;i<r->count;i++) {
        unsigned char *e=out+PSTK_HEAD+PSTK_EVENT*i;uint16_t head=(uint16_t)(r->events[i].kind<<14|r->events[i].id);
        e[0]=(unsigned char)head;e[1]=(unsigned char)(head>>8);
        for(unsigned b=0;b<4;b++) e[2+b]=(unsigned char)(r->events[i].value>>(8*b));
    }
    return PSTK_HEAD+PSTK_EVENT*r->count;
}
static int pstk_decode(const unsigned char *in,unsigned length,pstk_record *r) {
    if(length<PSTK_HEAD || in[0]!=PSTK_VERSION || (in[1]&~PSTK_FLAGS_KNOWN) || in[42]>PSTK_EVENTS_MAX || length!=PSTK_HEAD+PSTK_EVENT*in[42]) return 0;
    memset(r,0,sizeof(*r));r->flags=in[1];r->capture16=(uint16_t)(in[2]|in[3]<<8);r->count=in[42];r->steer=in[28];r->view=in[29];
    {uint32_t v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[30+b]<<(8*b);memcpy(&r->fov,&v,4);}
    for(unsigned b=0;b<8;b++) r->target|=(uint64_t)in[34+b]<<(8*b);
    if(r->flags&PSTK_FLAG_VIEW) {if(r->view>3 || !isfinite(r->fov)) return 0;}
    else if(r->view || r->target) return 0;
    else {uint32_t v;memcpy(&v,&r->fov,4);if(v) return 0;}
    for(unsigned i=0;i<3;i++) {
        uint32_t v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[4+4*i+b]<<(8*b);
        memcpy(&r->cursor[i],&v,4);
        if(!isfinite(r->cursor[i]) || (!(r->flags&PSTK_FLAG_CURSOR) && v)) return 0;
        v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[16+4*i+b]<<(8*b);
        memcpy(&r->aim[i],&v,4);
        if(!isfinite(r->aim[i]) || (!(r->flags&PSTK_FLAG_AIM) && v)) return 0;
    }
    if(r->steer>1 || (!(r->flags&PSTK_FLAG_AIM) && r->steer)) return 0;
    for(unsigned i=0;i<r->count;i++) {
        const unsigned char *e=in+PSTK_HEAD+PSTK_EVENT*i;uint16_t head=(uint16_t)(e[0]|e[1]<<8);uint32_t v=0;
        for(unsigned b=0;b<4;b++) v|=(uint32_t)e[2+b]<<(8*b);
        pstk_event *x=&r->events[i];x->kind=(unsigned char)(head>>14);x->id=(uint16_t)(head&0x3fff);x->value=v;
        if(x->id>=512) return 0;
        if(x->kind==PSTK_ACTION && v) return 0;
        if((x->kind==PSTK_STATE || x->kind==PSTK_SPACE) && v>1) return 0;
        if(x->kind==PSTK_SPACE && x->id!=8) return 0;
        if(x->kind==PSTK_RANGE) {float f;memcpy(&f,&v,4);if(!isfinite(f)) return 0;}
        if(x->kind!=PSTK_SPACE && pstk_verdict(x->kind,x->id)!=PSTK_REPLAY) return 0;
    }
    return 1;
}
static uint64_t pstk_fnv(const unsigned char *b,size_t n,uint64_t h) {
    for(size_t i=0;i<n;i++) {h^=b[i];h*=0x100000001b3ULL;}
    return h;
}
/* Capture side (MoveWorker): one event in dispatch order; an overflowing tick fails closed. */
static void pstk_async_fail(const char *why);
static int pstk_record_event(unsigned kind,unsigned id,uint32_t value) {
    if(pstk_build.count>=PSTK_EVENTS_MAX) {InterlockedIncrement64(&pstk_overflows);pstk_async_fail("stock_overflow");return 0;}
    pstk_event *e=&pstk_build.events[pstk_build.count++];e->kind=(unsigned char)kind;e->id=(uint16_t)id;e->value=value;
    if((LONG64)pstk_build.count>pstk_max_events) pstk_max_events=(LONG64)pstk_build.count;
    InterlockedIncrement64(&pstk_recorded);return 1;
}
/* Point p (from's frame) into to's frame with t = 3dde10(from, t, to): pos + p.x*ax + p.y*ay + p.z*az. */
static void pstk_apply(const float t[16],const float p[3],float out[3]) {
    for(unsigned i=0;i<3;i++) out[i]=t[i]+p[0]*t[4+i]+p[1]*t[8+i]+p[2]*t[12+i];
}
static void pstk_identity(float t[16]) {memset(t,0,64);t[4]=1.0f;t[9]=1.0f;t[14]=1.0f;}
static unsigned char *pstk_ship_of(unsigned char *player) {
    void *slot=NULL;
    if(!player || !pstk_fn.ship) return NULL;
    pstk_fn.ship(player,&slot);return (unsigned char*)slot;
}
/* The class 0x6d ancestor 7e8650 transforms the pick from (parent chain at +0x70). */
static unsigned char *pstk_zone_native(unsigned char *player) {
    unsigned char *c=NULL;
    if(!player || !read_local(player+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<32;depth++) {
        void **vt=NULL;
        if(!read_local(c,&vt,8) || !vt) return NULL;
        if(((unsigned char(*)(void*,int))vt[0x11e8/8])(c,PSTK_AIM_ZONE_CLASS)) return c;
        if(!read_local(c+0x70,&c,8)) return NULL;
    }
    return NULL;
}
/* Slot 1's aim of this frame (capturing client, Lua, after 717ed0): what 7e8650
 * would aim at, as a point in the ship's frame, and the mouse steering flag. */
static int pstk_aim_sample(float out[3],unsigned char *steer) {
    unsigned char *player=pnat_player(),*ship=pstk_ship_of(player),*picked=NULL,*input=NULL;float distance=-1.0f,p[4]={0},t[16];
    if(!player || !ship || !read_local(pnat_base+PNAT_INPUT,&input,8) || !input || !pstk_fn.transform || !pstk_fn.ray || !pstk_fn.zone ||
       !read_local(player+0x6c80,&distance,4) || !read_local(player+0x6c88,&picked,8) || !read_local(input+0x9053,steer,1)) return 0;
    if(!(distance<0.0f) && picked && picked!=ship) {
        unsigned char *zone=pstk_fn.zone(player);
        if(!zone || !read_local(player+0x6c60,p,16)) return 0;
        pstk_identity(t);pstk_fn.transform(zone,t,ship,0);pstk_apply(t,p,out);
    } else {
        pstk_fn.ray(player,p,ship,0);memcpy(out,p,12);
    }
    *steer=*steer!=0;
    return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}
/* This tick's record (Lua, slot 6, after slot 4's dispatch of the frame). */
static int pstk_capture(uint64_t tick,unsigned char out[PSTK_BYTES_MAX],unsigned *length,const char **why) {
    pstk_local *l=&pstk_locals[tick%PSTK_RING];
    if(!pstk_capturing || !tick) {*why="not_capturing";return 0;}
    if(l->used && l->capture==tick) {memcpy(out,l->bytes,l->length);*length=l->length;return 1;}
    if(!pstk_capture_start || tick<pstk_capture_start) {*why="not_live";return 0;}
    pstk_build.capture16=(uint16_t)tick;pstk_build.flags=pstk_local_active==1?PSTK_FLAG_ACTIVE:0;
    if(pstk_cursor_live_set) {pstk_build.flags|=PSTK_FLAG_CURSOR;memcpy(pstk_build.cursor,pstk_cursor_live,sizeof(pstk_cursor_live));}
    else memset(pstk_build.cursor,0,sizeof(pstk_build.cursor));
    /* The aim is slot 1's only (the local crosshair is the canonical player's; slots 2..N have no player guns yet).
     * A dead slot has no aim and no view to record: its records are consumed and ignored. */
    int own_dead=pstk_capture_slot>=1 && pstk_capture_slot<=PILOT_SLOTS && pilot_slots[pstk_capture_slot-1].dead;
    if(pfire_armed && (pnat_fire_buttons()&1) && pstk_focused()) pstk_build.flags|=PSTK_FLAG_FIRE;
    if(pstk_capture_slot==1 && !own_dead && pstk_aim_sample(pstk_build.aim,&pstk_build.steer)) {pstk_build.flags|=PSTK_FLAG_AIM;InterlockedIncrement64(&pstk_aim_samples);}
    else {memset(pstk_build.aim,0,sizeof(pstk_build.aim));pstk_build.steer=0;if(pstk_capture_slot==1 && !own_dead) InterlockedIncrement64(&pstk_aim_sample_failures);}
    if(!own_dead && pview_sample(&pstk_build.view,&pstk_build.fov,&pstk_build.target)) pstk_build.flags|=PSTK_FLAG_VIEW;
    else {pstk_build.view=0;pstk_build.fov=0.0f;pstk_build.target=0;}
    *length=pstk_encode(&pstk_build,out);
    memset(&pstk_build,0,sizeof(pstk_build));pstk_cursor_live_set=0;
    if(l->used && l->apply>pstk_frame_tick) {*why="local_ring_full";return 0;}
    l->capture=tick;l->apply=tick+pilot_delay;l->length=*length;l->used=1;memcpy(l->bytes,out,*length);
    return 1;
}
/* Staging from the wire (every client): the capture tick plus the agreed delay is the
 * one apply tick, still ahead of every pass, never before the capture window. */
static int pstk_stage(unsigned slot,const unsigned char *bytes,unsigned length,uint64_t now) {
    pstk_record r;
    if(!pstk_slot_mask || slot<1 || slot>PILOT_SLOTS || !(pstk_slot_mask>>(slot-1)&1u) || !pstk_decode(bytes,length,&r) ||
       !pstk_slot_record_ok(&r,slot)) return 0;
    uint64_t back=(uint16_t)(now-r.capture16);
    if(back>now) return 0;
    uint64_t capture=now-back,apply=capture+pilot_delay;
    if(!capture || capture<pstk_capture_start || apply<=now || apply>=now+PSTK_RING) return 0;
    if(pstk_capturing && slot==pstk_capture_slot) for(unsigned i=0;i<PSTK_RING;i++) {
        pstk_local *l=&pstk_locals[i];
        if(l->used && l->capture==capture && (l->length!=length || memcmp(l->bytes,bytes,length))) return 0; /* echo mismatch */
    }
    pstk_staged *e=&pstk_ring[slot-1][apply%PSTK_RING];
    if(e->present && e->tick==apply) return e->length==length && !memcmp(e->bytes,bytes,length);
    if(e->present && e->tick>now) return 0;
    memset(e,0,sizeof(*e));e->tick=apply;e->length=length;memcpy(e->bytes,bytes,length);e->present=1;
    InterlockedIncrement64(&pstk_staged_count);return 1;
}
/* Settings the stock path reads that are user options; the response curve is
 * sampled through its own evaluator (its points live on the heap). */
static uint64_t pstk_settings_using(unsigned char *base) {
    unsigned char *input=NULL,*config=NULL,bytes[64];unsigned n=0;
    uint64_t h=0xcbf29ce484222325ULL;
    static const unsigned options[]={0x2f4b9be,0x2f4b9bf,0x2f4b9c0,0x2f4b9c1,0x2f4b9c2,0x2f4b9c3,0x2f4b9c4,0x2f4b9c5,0x2f4b9c6,0x2f4b9c7,
        0x2f4b9cc,0x38f52d7,0x38f52d8};  /* ... boost toggle, force shooting at cursor (7e8e60) */
    for(unsigned i=0;i<PSTK_COUNT(options);i++) if(!sample_read(base+options[i],&bytes[n++],1)) return 0;
    if(!sample_read(base+0x6cf1560,&input,8) || !input || !sample_read(base+0x6cf1538,&config,8) || !config) return 0;
    static const unsigned input_fields[]={0x8e20,0x905b,0x905c,0x912c,0x912d,0x912e,0x912f};
    for(unsigned i=0;i<PSTK_COUNT(input_fields);i++) if(!sample_read(input+input_fields[i],&bytes[n++],1)) return 0;
    h=pstk_fnv(bytes,n,h);
    static const struct {unsigned offset,size;} config_fields[]={{0x68,4},{0x6c,4},{0x80,8},{0x88,8},{0x90,4},{0x94,4},
        {0xc8,4},{0x294,4}};  /* ..., aim assist angle and range (7e8650) */
    for(unsigned i=0;i<PSTK_COUNT(config_fields);i++) {
        if(!sample_read(config+config_fields[i].offset,bytes,config_fields[i].size)) return 0;
        h=pstk_fnv(bytes,config_fields[i].size,h);
    }
    static const float samples[]={0.0f,0.1f,0.25f,0.5f,0.75f,1.0f};
    for(unsigned i=0;pstk_fn.curve && i<PSTK_COUNT(samples);i++) {
        float y=pstk_fn.curve(config+8,samples[i]);
        h=pstk_fnv((const unsigned char*)&y,4,h);
    }
    return h|1u;
}

/* ---- live pieces: the pnat entries in stock mode (lockstep_pilot_native.h) ---- */
static void pstk_async_fail(const char *why) {pnat_async_fail(why);}
/* Every handler of one id, in the stock order of 1255cd0/1255dd0/1255ee0. */
static void pstk_call(void *handler,unsigned kind,int id,uint32_t value) {
    if(kind==PSTK_ACTION) ((void(*)(int))handler)(id);
    else if(kind==PSTK_STATE) ((void(*)(int,unsigned char))handler)(id,(unsigned char)value);
    else {float f;memcpy(&f,&value,4);((void(*)(int,float))handler)(id,f);}
}
static void pstk_walk(unsigned char *header,unsigned kind,int id,uint32_t value) {
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)header,id,&upper);
    if(!n) return;
    while(n!=upper) {pnat_node *next=pnat_next(n);pstk_call(n->handler,kind,id,value);n=next;}
}
static int pres_local_enter(void);static void pres_local_leave(int entered); /* lockstep_pilot_present.h */
static void pdeath_note(const pstk_event *x);                                /* lockstep_pilot_death.h */
static unsigned pres_slot;static int pres_has_seat(void);
/* Two windows on one PC: keyboard and mouse only reach the window with the focus, a pad may reach both.
 * A control that engages is recorded only with the focus (a release always, so nothing stays held);
 * scripted input does not come through here. Tests: 1 focused, -1 not, 0 the real window. */
static int pstk_focus_forced;
static int pstk_focused(void) {
    if(pstk_focus_forced) return pstk_focus_forced>0;
    DWORD owner=0;HWND window=GetForegroundWindow();
    if(window) GetWindowThreadProcessId(window,&owner);
    return owner==GetCurrentProcessId();
}
static int pstk_is_fire(unsigned kind,int id) {
    if(kind==PSTK_STATE) return id==0x18 || id==0x19 || id==0x30 || id==0x31;
    return kind==PSTK_RANGE && id>=0x11 && id<=0x14;
}
static int pstk_engages(unsigned kind,uint32_t value) {
    if(kind==PSTK_ACTION) return 1;
    if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);return f!=0.0f;}
    return value!=0;
}
/* A dispatch on this client: presentation ids run (on a presenting client as its own slot's
 * player: the camera, target and menu handlers read the singleton), replay ids are recorded (the
 * capturing client) or dropped, refused ids are logged. map = 1255xx0's argument. */
static void pstk_dispatch(unsigned kind,unsigned char *map,int id,uint32_t value) {
    unsigned verdict=pnat_is_live()?pstk_verdict_slot(kind,id,pstk_capturing?pstk_capture_slot:1):PSTK_LOCAL;
    if(verdict==PSTK_LOCAL) {
        /* A presenting client whose slot is dead and not in a suit has no player to act as: X4's singleton is slot 1's. */
        if(pres_slot && pilot_slots[pres_slot-1].dead && !pres_has_seat()) {InterlockedIncrement64(&pstk_dropped);return;}
        int presented=pres_local_enter();
        InterlockedIncrement(&uirep_dispatch_depth);
        pstk_walk(map+0x40*kind,kind,id,value);
        InterlockedDecrement(&uirep_dispatch_depth);
        pres_local_leave(presented);
        return;
    }
    if(verdict==PSTK_REFUSE) {
        int shown=kind==PSTK_ACTION?1:kind==PSTK_STATE?(int)value:0;
        if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);shown=fabsf(f)>=0.1f?(f>0?1:-1):0;}
        /* Fire never runs in X4's own path here: with pilot fire its held state rides the record. */
        pnat_fire_input(kind,id,shown!=0);
        if(pfire_armed && pstk_is_fire(kind,id)) return;
        pnat_refuse(kind,id,shown);return;
    }
    if(pstk_capturing && pstk_engages(kind,value) && !pstk_focused()) {InterlockedIncrement64(&pstk_unfocused);return;}
    if(pstk_capturing) pstk_record_event(kind,(unsigned)id,value);else InterlockedIncrement64(&pstk_dropped);
}
/* SPACE on this client: only the input object's side of d88710 (mouse steering
 * save/restore, 9062/9064 as d9c590/d9c250 leave them) and the context refresh;
 * the capturing client records the edge. Flight control and the handoff are replayed. */
static void pstk_space(unsigned char active) {
    volatile unsigned char *input=NULL;
    if(!read_local(pnat_base+PNAT_INPUT,(void*)&input,8) || !input) {pstk_async_fail("space_input");return;}
    if(active) {
        if(input[0x9053]) {
            unsigned char *options=NULL;
            if(read_local(pnat_base+PNAT_OPTIONS,&options,8) && options &&
               pnat_fn.option(options+0x118,(const char*)(pnat_base+PNAT_PERSISTENT_NAME),0)) {
                pnat_fn.steer_mode((void*)input,(char)*PNAT_U8(PNAT_STEER_SAVED));
                pnat_fn.steer_direct((void*)input,(char)*PNAT_U8(PNAT_DIRECT_SAVED));
            }
            *PNAT_U8(PNAT_STEER_SAVED)=0;*PNAT_U8(PNAT_DIRECT_SAVED)=0;
        }
        input[0x9064]=1;input[0x9062]=1;
    } else {
        unsigned char was=input[0x9056];void **cursor=NULL;
        *PNAT_U8(PNAT_STEER_SAVED)=was;
        if(was) {
            input[0x9056]=0;memcpy(&cursor,(const void*)(input+0x8fe0),8);
            if(cursor) ((void(*)(void*,char))(*(void***)cursor)[1])(cursor,0);
        }
        was=input[0x9058];*PNAT_U8(PNAT_DIRECT_SAVED)=was;
        if(was) {
            input[0x9058]=0;pnat_fn.steer_reset((void*)input,0);memcpy(&cursor,(const void*)(input+0x8fe0),8);
            if(cursor) ((void(*)(void*,char))(*(void***)cursor)[2])(cursor,0);
        }
        input[0x9064]=0;input[0x9062]=0;
    }
    pstk_local_active=active?1:0;InterlockedIncrement64(&pstk_space_local);
    if(pstk_capturing) pstk_record_event(PSTK_SPACE,8,active?1u:0u);
    pnat_fn.refresh((char)(pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY)!=NULL));
}
/* The simulation half of d88710 on every client, in its order; the input object's
 * 9062/9064 written by d9c590/d9c250 are put back (they follow this client's UI). */
static void pstk_space_replay_slot(unsigned slot,unsigned char active,double now) {
    unsigned char *controller=(unsigned char*)pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY),*player=pnat_player();
    volatile unsigned char *input=pnat_input;unsigned char keep62=input?input[0x9062]:0,keep64=input?input[0x9064]:0;
    if(!active) {
        *PNAT_U8(PNAT_FLIGHT)=0;
        static const unsigned zero8[]={0x2f4b750,0x2f4b788,0x2f4b720,0x2f4b794},zero4[]={0x2f4b758,0x2f4b728};
        for(unsigned i=0;i<4;i++) *(volatile uint64_t*)(pnat_base+zero8[i])=0;
        for(unsigned i=0;i<2;i++) *(volatile uint32_t*)(pnat_base+zero4[i])=0;
        if(controller && !*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) pstk_fn.release(controller);
        if(player) pstk_fn.stopped(player);
    } else {
        for(unsigned i=0;i<5;i++) ((volatile double*)(pnat_base+PNAT_TIMERS))[i]=now+0.5;
        if(controller) {
            unsigned char authority=pstk_fn.authority(controller);
            if(!*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) pstk_fn.handoff(controller,(char)authority);
        }
        *PNAT_U8(PNAT_FLIGHT)=1;
        if(player) pstk_fn.started(player);
    }
    if(input) {input[0x9062]=keep62;input[0x9064]=keep64;}
    if(slot>=2) {pstk_slot_frames[slot-1].flight=active?1u:0u;pstk_slot_frames[slot-1].flight_known=1;}
    else {pstk_flight=active?1u:0u;pstk_flight_known=1;}
    InterlockedIncrement64(&pstk_space_replayed);
}
static void pstk_space_replay(unsigned char active,double now) {pstk_space_replay_slot(1,active,now);}
/* d88a10 in stock mode, every client, inside this frame's d95180: the stock body
 * runs (with flight control for its own call on the capturing client while its
 * own controls are active but the replayed state is still off), the capturing
 * client keeps its outputs for the record, and every client's d95180 then reads
 * the replayed values of this tick (zero before the first replayed record). */
static void pstk_cursor(void *controller) {
    InterlockedIncrement64(&pstk_cursor_calls);
    volatile unsigned char *flight=PNAT_U8(PNAT_FLIGHT);unsigned char was=*flight;
    unsigned scope=pstk_scope_slot;
    int window=pstk_capturing && pstk_local_active==1 && !was && !scope;
    if(window) *flight=1;
    __try {pnat_fn.cursor_original(controller);} __finally {if(window) *flight=was;}
    volatile float *out=PNAT_F32(0x2f4b738);
    /* The local device is read in the canonical update only (the slot scopes run on stored banks). */
    if(pstk_capturing && !scope) {for(unsigned i=0;i<3;i++) pstk_cursor_live[i]=out[i];pstk_cursor_live_set=1;}
    int replay;const float *source;
    if(scope>=2) {
        const pstk_slot_frame *f=&pstk_slot_frames[scope-1];
        replay=f->tick && f->tick==stats.issued && (f->r.flags&PSTK_FLAG_CURSOR);source=f->r.cursor;
    } else {replay=pstk_cursor_tick && pstk_cursor_tick==stats.issued;source=pstk_cursor_replay;}
    for(unsigned i=0;i<3;i++) out[i]=replay?source[i]:0.0f;
}
/* PlayerShootController::vfunc21 on every client (vtable slot 21, from the first
 * pass of the session to stop+1): the stock call runs on the replayed aim of this
 * tick (or the neutral one) and the four camera fields are put back afterwards.
 * Weapon::Move calls it on the frame thread; a call from any other thread (a
 * presentation reader) gets the stock result and is counted (foreign). */
static unsigned char pstk_aim(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                              unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    InterlockedIncrement64(&pstk_aim_calls);
    if(!InterlockedCompareExchange(&pstk_aim_on,0,0))
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&pstk_aim_foreign);
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(pstk_scope_slot) {InterlockedIncrement64(&pstk_aim_scoped);return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);}
    unsigned char *player=pnat_player(),*ship=pstk_ship_of(player),*zone=player && pstk_fn.zone?pstk_fn.zone(player):NULL;
    volatile unsigned char *input=NULL;
    if(!read_local(pnat_base+PNAT_INPUT,(void*)&input,8) || !player || !ship || !zone || !input || !pstk_fn.transform) {
        /* A dying or dead slot 1: X4's player may be between ships. */
        int dying=pilot_slots[0].dead;double hull=1.0;
        if(!dying && pdeath_armed && pilot_slots[0].found) dying=read_local(pilot_slots[0].found+PDEATH_HULL,&hull,8) && fabs(hull)<PDEATH_HULL_EPSILON;
        if(!dying) pstk_async_fail("stock_aim_resolve");
        return pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    float p[3]={0.0f,0.0f,PSTK_AIM_NEUTRAL},t[16],q[4]={0.0f,0.0f,0.0f,0.0f},valid=1.0f,saved_distance=0.0f;unsigned char steer=0;
    if(pstk_aim_tick && pstk_aim_tick==stats.issued && pstk_aim_valid_replay) {memcpy(p,pstk_aim_replay,sizeof(p));steer=pstk_aim_steer_replay;}
    else InterlockedIncrement64(&pstk_aim_neutral);
    pstk_identity(t);pstk_fn.transform(ship,t,zone,0);pstk_apply(t,p,q);
    unsigned char saved_point[16],saved_picked[8],saved_steer=input[0x9053];
    memcpy(saved_point,player+0x6c60,16);memcpy(&saved_distance,player+0x6c80,4);memcpy(saved_picked,player+0x6c88,8);
    memcpy(player+0x6c60,q,16);memcpy(player+0x6c80,&valid,4);memcpy(player+0x6c88,&player,8);input[0x9053]=steer;
    unsigned char result=pstk_fn.aim(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    memcpy(player+0x6c60,saved_point,16);memcpy(player+0x6c80,&saved_distance,4);memcpy(player+0x6c88,saved_picked,8);input[0x9053]=saved_steer;
    InterlockedIncrement64(&pstk_aim_replaced);
    return result;
}
/* Scripted input for the capturing client (smokes without a human): one control id
 * of the replay set, recorded as if X4 had dispatched it. */
static int pstk_inject(unsigned kind,unsigned id,uint32_t value) {
    if(!pstk_capturing || !pnat_is_live() || kind>PSTK_RANGE || pstk_verdict_slot(kind,(int)id,pstk_capture_slot)!=PSTK_REPLAY) return 0;
    if(kind==PSTK_ACTION && value) return 0;
    if(kind==PSTK_STATE && value>1) return 0;
    if(kind==PSTK_RANGE) {float f;memcpy(&f,&value,4);if(!isfinite(f)) return 0;}
    if(!pstk_record_event(kind,id,value)) return 0;
    InterlockedIncrement64(&pstk_injected);return 1;
}
/* Replay of this tick's record (every client, path_release, before the pass). */
static int pstk_frame(uint64_t tick,const char **why) {
    pstk_frame_tick=tick;pstk_frame_events=0;pstk_frame_hash=0;
    if(!(pstk_settings=pstk_settings_using(pnat_base))) {*why="stock_settings";return 0;}
    if(tick<pstk_first_apply || tick>pilot_stop) return 1;
    pstk_staged *e=&pstk_ring[0][tick%PSTK_RING];pstk_record r;double now=0;
    if(!e->present || e->tick!=tick) {*why="stock_missing";return 0;}
    if(!pstk_decode(e->bytes,e->length,&r) || r.capture16!=(uint16_t)(tick-pilot_delay)) {*why="stock_delay_mismatch";return 0;}
    if(!pnat_input || !pnat_fn.time(&now)) {*why="stock_time";return 0;}
    unsigned char *map=pnat_input+0x63e0;
    if(pilot_slots[0].dead) { /* consumed and ignored: its hash stays in the declared state, cursor and aim stay neutral */
        pview_replay(1,tick,0,0,0.0f,0);
        pstk_fire_tick[0]=tick;pstk_fire_on[0]=0;
    } else {
        for(unsigned i=0;i<r.count;i++) { /* every handler is a stock one before any runs */
            pstk_event *x=&r.events[i];
            if(x->kind==PSTK_SPACE) continue;
            pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*x->kind),x->id,&upper);
            for(;n && n!=upper;n=pnat_next(n)) if(!pstk_handler_ok(n->handler)) {*why="stock_foreign_handler";return 0;}
        }
        for(unsigned i=0;i<r.count;i++) {
            pstk_event *x=&r.events[i];
            if(x->kind==PSTK_SPACE) pstk_space_replay((unsigned char)x->value,now);
            else {pstk_walk(map+0x40*x->kind,x->kind,x->id,x->value);if(pdeath_armed) pdeath_note(x);}
        }
        InterlockedExchangeAdd64(&pstk_replayed,(LONG64)r.count);
        if(r.flags&PSTK_FLAG_CURSOR) memcpy(pstk_cursor_replay,r.cursor,sizeof(pstk_cursor_replay));
        else memset(pstk_cursor_replay,0,sizeof(pstk_cursor_replay));
        pstk_aim_valid_replay=(r.flags&PSTK_FLAG_AIM)!=0;memcpy(pstk_aim_replay,r.aim,sizeof(pstk_aim_replay));pstk_aim_steer_replay=r.steer;
        pstk_aim_tick=tick;
        pview_replay(1,tick,(r.flags&PSTK_FLAG_VIEW)!=0,r.view,r.fov,r.target);
        pstk_fire_tick[0]=tick;pstk_fire_on[0]=(r.flags&PSTK_FLAG_FIRE)!=0;
        pstk_cursor_tick=tick;pstk_frame_events=r.count;
    }
    pstk_frame_hash=pstk_fnv(e->bytes,e->length,PSTK_DOMAIN);
    e->present=0;InterlockedIncrement64(&pstk_records);
    /* Slots 2..N: staged here, replayed in their scopes after the canonical update (lockstep_pilot_slots.h). */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        pstk_staged *s=&pstk_ring[k-1][tick%PSTK_RING];pstk_slot_frame *f=&pstk_slot_frames[k-1];pstk_record q;
        if(!s->present || s->tick!=tick) {*why="stock_missing_slot";return 0;}
        if(!pstk_decode(s->bytes,s->length,&q) || q.capture16!=(uint16_t)(tick-pilot_delay) || !pstk_slot_record_ok(&q,k)) {
            *why="stock_delay_mismatch";return 0;
        }
        for(unsigned i=0;i<q.count;i++) {
            pstk_event *x=&q.events[i];
            if(x->kind==PSTK_SPACE) continue;
            pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*x->kind),x->id,&upper);
            for(;n && n!=upper;n=pnat_next(n)) if(!pstk_handler_ok(n->handler)) {*why="stock_foreign_handler";return 0;}
        }
        f->tick=tick;f->r=q;f->hash=pstk_fnv(s->bytes,s->length,PSTK_DOMAIN+k);s->present=0;
        /* A dead slot's record is consumed the same way: pslot_replay skips it (not seated). */
        int dead=pilot_slots[k-1].dead!=0;
        pview_replay(k,tick,!dead && (q.flags&PSTK_FLAG_VIEW)!=0,q.view,q.fov,q.target);
        pstk_fire_tick[k-1]=tick;pstk_fire_on[k-1]=!dead && (q.flags&PSTK_FLAG_FIRE)!=0;
        InterlockedIncrement64(&pstk_records);
    }
    return 1;
}
/* Slot k's record of this tick, replayed inside its scope (singleton = the slot's
 * player, the bank = the slot's): SPACE edges through the simulation half of
 * d88710, every other event through the stock handlers. */
static int pstk_slot_replay(unsigned k,double now) {
    pstk_slot_frame *f=&pstk_slot_frames[k-1];
    if(k<2 || k>PILOT_SLOTS || f->tick!=stats.issued || !pnat_input) return 0;
    unsigned char *map=pnat_input+0x63e0;
    for(unsigned i=0;i<f->r.count;i++) {
        pstk_event *x=&f->r.events[i];
        if(x->kind==PSTK_SPACE) pstk_space_replay_slot(k,(unsigned char)x->value,now);
        else pstk_walk(map+0x40*x->kind,x->kind,x->id,x->value);
    }
    InterlockedExchangeAdd64(&pstk_replayed,(LONG64)f->r.count);
    return 1;
}
/* The declared-state receipt of this tick: applied record, flight state, settings. */
static int pstk_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"PSTOCK_1:%llu:%u:%016llx:%u:%016llx",(unsigned long long)pstk_frame_tick,pstk_frame_events,
        (unsigned long long)pstk_frame_hash,pstk_flight,(unsigned long long)pstk_settings);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        const pstk_slot_frame *f=&pstk_slot_frames[k-1];
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        int fresh=f->tick==pstk_frame_tick;
        int more=snprintf(out+n,capacity-(size_t)n,"|S%u:%u:%016llx:%u",k,fresh?f->r.count:0u,(unsigned long long)(fresh?f->hash:0),f->flight);
        if(more<0) return 0;
        n+=more;
    }
    if(n>0 && (size_t)n<capacity && pdeath_armed) { /* who has no ship: the same tick on every client */
        int more=snprintf(out+n,capacity-(size_t)n,"|D%x",pdeath_dead);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int pdeath_status(char *out,size_t capacity);                       /* lockstep_pilot_death.h */
static int pslot_armed;static int pslot_status(char *out,size_t capacity); /* lockstep_pilot_slots.h */
static int pres_armed;static int pres_status(char *out,size_t capacity);   /* lockstep_pilot_present.h */
static int pstk_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," stock=%s slot=%u slots=%x flight=%u/%u capture_start=%llu first_apply=%llu recorded=%lld dropped=%lld "
        "replayed=%lld records=%lld staged=%lld space=%lld/%lld cursor=%lld injected=%lld unfocused=%lld overflows=%lld max_events=%lld settings=%016llx "
        "aim=%d/%lld/%lld/%lld/%lld/%lld/%lld",
        pstk_capturing?"capture":"replay",pstk_capturing?pstk_capture_slot:pstk_slot,pstk_slot_mask,pstk_flight,pstk_flight_known,
        (unsigned long long)pstk_capture_start,
        (unsigned long long)pstk_first_apply,(long long)pstk_recorded,(long long)pstk_dropped,(long long)pstk_replayed,(long long)pstk_records,
        (long long)pstk_staged_count,(long long)pstk_space_local,(long long)pstk_space_replayed,(long long)pstk_cursor_calls,
        (long long)pstk_injected,(long long)pstk_unfocused,(long long)pstk_overflows,(long long)pstk_max_events,(unsigned long long)pstk_settings,
        pstk_aim_installed,(long long)pstk_aim_calls,(long long)pstk_aim_replaced,(long long)pstk_aim_neutral,(long long)pstk_aim_samples,
        (long long)pstk_aim_sample_failures,(long long)pstk_aim_foreign);
    if(n<=0 || (size_t)n>=capacity) return 0;
    int more=pview_status(out+n,capacity-(size_t)n);
    if(!more) return 0;
    n+=more;
    if(pslot_armed) {more=pslot_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pres_armed) {more=pres_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(pdeath_armed) {more=pdeath_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    return n;
}
