/* Native controls for the lockstep pilot (X4's own input), DEFAULT OFF; stop
 * reason 113 (native_gate_pilot_capture_source = 'native'). Needs the parked
 * pilot (suppress) and reuses its symmetric transition: at start-3 every client
 * takes its deactivation share (the stock SPACE callback d88710 drops flight
 * control 0x2f4b828 and queues the Steering model), at start-2 this module
 * installs and releases the share, so the PlayerController's input contexts
 * come back and the stock input processing (cursor and direct mouse steering,
 * joystick and pad axes, throttle axis and keys, the user's bindings) fills the
 * processed channels that pilot_capture reads, while flight control stays 0:
 * every inlined authority test (PlayerController vfunc6/7/10/22/33/34/50/52/53,
 * d9c250, d9c590, d8a310, d9c050) keeps choosing the Agent, and only the
 * Steering substitution of lockstep_pilot_inputs.h moves the slot ships.
 *
 * Pieces (all inert until live, removed at stop+1 or on any stop):
 * (1) authority  16-byte entry patch of d8dd50 (mov rax,imm64; jmp rax) to
 *                pnat_authority: 1 for the routed PlayerController, the stock
 *                predicate (re-stated) for any other controller.
 * (2) routing    PlayerController vtable 0x2c39488 slots 6/7/10 (transform,
 *                velocity, acceleration; the per-frame sync at the end of d95180)
 *                point at routers that jump to the NavigationController base
 *                (ea5a70/ea6280/ea66c0 = the PlayerController's own Agent branch)
 *                for the routed controller and to the original otherwise.
 * (3) SPACE      the context callback vector entry d88710 is swapped for
 *                pnat_space: input side only (mouse steering save/restore,
 *                timers, channel zeroing, 0x9062/0x9064, d9cc30). Skipped:
 *                d9c590 (model 0, transform adoption, environment event),
 *                d9c250 (model 6, AbortPathOrder, reference order, stop-in-menu
 *                throttle), flight control = 1, FlightControlStarted/Stopped.
 * (4) actions    entry patches of the three InputMapping dispatchers
 *                (actions 1255cd0, states 1255dd0, ranges 1255ee0), re-stated
 *                exactly (equal range, successor before each call): ids that
 *                change ship state are refused and logged; the F-gated stock
 *                handlers run inside a flight-control window (state 0xf full
 *                forward, range 4 throttle axis); handlers whose stock body
 *                also touches the occupied ship's engines (states 0xd/0xe/0x10,
 *                range 10 wheel, action 0x7b stop) run as input-only restatements.
 *                Refused fire inputs still set the record's trigger bits (0
 *                primary, 2 secondary) for native_gate_pilot_fire.
 * (5) cursor     16-byte entry patch of d88a10 (cursor and direct mouse
 *                steering, gated on flight control): the stock body runs inside
 *                a flight-control window through a trampoline holding its first
 *                18 bytes (position independent, pinned).
 * (6) requests   the PlayerController's own velocity requests (e5d380 on
 *                controller+0x6db0 linear, +0x13080 angular) are recorded and
 *                replaced by zero, so its private integration (d93440, whose
 *                trigger-object sweep runs on it) never follows local input.
 *                For the slot whose ship is the PlayerController's ship, the
 *                recorded requests (stock scheme: autoroll, roll limits, assist)
 *                are normalised by that ship's Agent limits into the record
 *                (mode bit 0x10): pilot_target maps them back unchanged.
 * Every window and restatement requires: flight control 0, no autopilot, no
 * highway, the player's assist not suspended (+0x6aad) and no player activity
 * (+0x6ab4: travel, scan, long-range scan, SETA); otherwise it refuses and the
 * session stops (113). Per pass: patches intact, flight control 0, the stock
 * predicate 1, assist 1, same controller and ship, no activity. The game option
 * "stop ship in menus" (0x2f4b9bf) makes PlayerController vfunc33's Agent
 * branch zero the throttle setpoint on every update: local input only, so it is
 * reported (stopship=1), not a stop; the stock throttle then stays at zero. Only the
 * MoveWorker installs, dispatches and restores (path_release, slots 3/4/6).
 * Mode 'stock' (lockstep_pilot_stock.h, stop reason 155) installs pieces (3) to
 * (5) only, with flight control and the authority predicate stock: slot 1 flies
 * with X4's own path on every client from the recorded dispatch stream. */
#define PNAT_STOP_REASON 113u
#define PNAT_FLIGHT 0x2f4b828u           /* flight control */
#define PNAT_CONTEXTS 0x2f4b829u         /* PlayerController input contexts requested */
#define PNAT_COUNTER 0x2f4b7e4u          /* control deactivation count */
#define PNAT_AUTOPILOT 0x2f4b7d8u
#define PNAT_HIGHWAY 0x6b661d0u
#define PNAT_STOP_IN_MENU 0x2f4b9bfu     /* game option: vfunc33's Agent branch zeroes the setpoint every update */
#define PNAT_THROTTLE_LOCK 0x38f4c31u
#define PNAT_INPUT 0x6cf1560u
#define PNAT_OPTIONS 0x6cf1520u
#define PNAT_CONFIG 0x6cf1538u
#define PNAT_UNIVERSE 0x3d198f8u
#define PNAT_UNIVERSE_NONE 0x3d196d8u
#define PNAT_CONTROLLER_KEY 0x3d196f0u
#define PNAT_PERSISTENT_NAME 0x2a6c098u  /* "persistentmousesteering" */
#define PNAT_LEAD 0x2cbe7e8u             /* double 0.5 */
#define PNAT_STEER_SAVED 0x38f4db6u
#define PNAT_DIRECT_SAVED 0x38f4db5u
#define PNAT_TIMERS 0x2f4b800u           /* five doubles */
#define PNAT_SETPOINT 0x2f4b75cu
#define PNAT_AXIS_LAST 0x2f4b770u
#define PNAT_HOLD 0x2f4b778u
#define PNAT_BASE_SETPOINT 0x2f4b780u
#define PNAT_FULL_STOP 0x2f4b7d0u
#define PNAT_KEY_ACCEL 0x2f4b7fau
#define PNAT_KEY_DECEL 0x2f4b7fbu
#define PNAT_KEY_REVERSE 0x2f4b7fdu
#define PNAT_VTABLE 0x2c39488u
#define PNAT_AUTHORITY_RVA 0xd8dd50u
#define PNAT_CURSOR_RVA 0xd88a10u
#define PNAT_CURSOR_RESUME 0xd88a22u
#define PNAT_SPACE_RVA 0xd88710u
#define PNAT_ACTION_HANDLER 0xd86c80u
#define PNAT_STATE_HANDLER 0xd870b0u
#define PNAT_RANGE_HANDLER 0xd87f70u
#define PNAT_LINEAR 0x6db0u              /* controller solver sub-objects */
#define PNAT_ANGULAR 0x13080u
enum {PNAT_ACTION=0,PNAT_STATE=1,PNAT_RANGE=2,PNAT_KINDS=3};
typedef struct {unsigned char kind;unsigned short id;const char *name;} pnat_id;
/* Refused while live (every handler of the id). */
static const pnat_id pnat_blocked_ids[]={
    {PNAT_ACTION,0x12f,"TOGGLE_TRAVEL_MODE"},{PNAT_ACTION,0xe1,"TOGGLE_SETA_MODE"},{PNAT_ACTION,0x130,"TOGGLE_SCAN_MODE"},
    {PNAT_ACTION,0x131,"TOGGLE_LONGRANGE_SCAN_MODE"},{PNAT_ACTION,0xb3,"TOGGLE_AUTOPILOT"},{PNAT_ACTION,0xdd,"TOGGLE_FLIGHT_ASSIST"},
    {PNAT_ACTION,0x13f,"TOGGLE_GUIDANCE"},{PNAT_ACTION,0xaf,"DOCK_ACTION"},{PNAT_ACTION,0xd3,"UNDOCK"},{PNAT_ACTION,0x115,"GET_UP"},
    {PNAT_ACTION,0x133,"DEPLOY_COUNTERMEASURE"},{PNAT_ACTION,0x148,"TOGGLE_TURRET_ACTIVE"},{PNAT_ACTION,0x149,"TOGGLE_DRONE_LAUNCH"},
    {PNAT_ACTION,0x157,"SQUAD_ATTACK_TARGET"},{PNAT_ACTION,0x158,"SQUAD_RESUME_ASSIGNMENT"},{PNAT_ACTION,0x159,"DEPLOY_SATELLITE"},
    {PNAT_ACTION,0x15a,"DEPLOY_NAVBEACON"},{PNAT_ACTION,0x15b,"DEPLOY_RESOURCEPROBE"},{PNAT_ACTION,0x15c,"DEPLOY_MINE"},
    {PNAT_ACTION,0x15d,"DEPLOY_LASERTOWER"},{PNAT_ACTION,0x15e,"REPEAT_DEPLOYMENT"},{PNAT_ACTION,0x15f,"REPEAT_CIVILIAN_DEPLOYMENT"},
    {PNAT_ACTION,0x160,"REPEAT_MILITARY_DEPLOYMENT"},{PNAT_ACTION,0x179,"TOGGLE_COVER"},{PNAT_ACTION,0x17d,"TOGGLE_STANCE"},
    {PNAT_ACTION,0x176,"TOGGLE_REDUCED_SPEED_MODE"},{PNAT_ACTION,0x122,"DEBUG_FEATURE_3"},{PNAT_ACTION,0x123,"DEBUG_FEATURE_4"},
    {PNAT_ACTION,0x8,"CYCLE_NEXT_PRIMARY_WEAPONGROUP"},{PNAT_ACTION,0x9,"CYCLE_PREV_PRIMARY_WEAPONGROUP"},
    {PNAT_ACTION,0xa,"CYCLE_NEXT_SECONDARY_WEAPONGROUP"},{PNAT_ACTION,0xb,"CYCLE_PREV_SECONDARY_WEAPONGROUP"},
    {PNAT_ACTION,0x8b,"SELECT_PRIMARY_WEAPONGROUP_1"},{PNAT_ACTION,0x8c,"SELECT_PRIMARY_WEAPONGROUP_2"},
    {PNAT_ACTION,0x8d,"SELECT_PRIMARY_WEAPONGROUP_3"},{PNAT_ACTION,0x8e,"SELECT_PRIMARY_WEAPONGROUP_4"},
    {PNAT_ACTION,0x8f,"SELECT_PRIMARY_WEAPONGROUP_5"},{PNAT_ACTION,0x90,"SELECT_PRIMARY_WEAPONGROUP_6"},
    {PNAT_ACTION,0x91,"SELECT_PRIMARY_WEAPONGROUP_7"},{PNAT_ACTION,0x92,"SELECT_PRIMARY_WEAPONGROUP_8"},
    {PNAT_ACTION,0x93,"SELECT_PRIMARY_WEAPONGROUP_9"},{PNAT_ACTION,0x94,"SELECT_PRIMARY_WEAPONGROUP_10"},
    {PNAT_ACTION,0x95,"SELECT_SECONDARY_WEAPONGROUP_1"},{PNAT_ACTION,0x96,"SELECT_SECONDARY_WEAPONGROUP_2"},
    {PNAT_ACTION,0x97,"SELECT_SECONDARY_WEAPONGROUP_3"},{PNAT_ACTION,0x98,"SELECT_SECONDARY_WEAPONGROUP_4"},
    {PNAT_ACTION,0x99,"SELECT_SECONDARY_WEAPONGROUP_5"},{PNAT_ACTION,0x9a,"SELECT_SECONDARY_WEAPONGROUP_6"},
    {PNAT_ACTION,0x9b,"SELECT_SECONDARY_WEAPONGROUP_7"},{PNAT_ACTION,0x9c,"SELECT_SECONDARY_WEAPONGROUP_8"},
    {PNAT_ACTION,0x9d,"SELECT_SECONDARY_WEAPONGROUP_9"},{PNAT_ACTION,0x9e,"SELECT_SECONDARY_WEAPONGROUP_10"},
    {PNAT_ACTION,0x141,"CYCLE_NEXT_ACTIVE_WEAPON_AMMO"},{PNAT_ACTION,0x147,"SCAN_ACTION"},{PNAT_ACTION,0x81,"COMM_ACTION"},
    {PNAT_ACTION,0x16c,"TELEPORT_ACTION"},{PNAT_ACTION,0x16d,"ADDON_DETAILMONITOR_TELEPORT"},
    {PNAT_ACTION,0xd0,"ADDON_DETAILMONITOR_A_SHIFT"},{PNAT_ACTION,0x142,"ADDON_DETAILMONITOR_TRAVELMODE"},
    {PNAT_ACTION,0x143,"ADDON_DETAILMONITOR_SETA"},{PNAT_ACTION,0x107,"ADDON_DETAILMONITOR_REMOVE_ORDER"},
    {PNAT_STATE,0x11,"BOOST"},{PNAT_STATE,0x18,"FIRE_PRIMARY_WEAPON"},{PNAT_STATE,0x19,"FIRE_SECONDARY_WEAPON"},
    {PNAT_STATE,0x30,"FIRE_PRIMARY_WEAPON_AT_CURSOR"},{PNAT_STATE,0x31,"FIRE_SECONDARY_WEAPON_AT_CURSOR"},
    {PNAT_STATE,0x50,"LOOTMAGNET"},{PNAT_STATE,0x85,"SUSPEND_FLIGHT_ASSIST"},{PNAT_STATE,0x5b,"MATCH_SPEED"},
    {PNAT_RANGE,0x11,"FIRE_PRIMARY_WEAPON"},{PNAT_RANGE,0x12,"FIRE_SECONDARY_WEAPON"},
    {PNAT_RANGE,0x13,"FIRE_PRIMARY_WEAPON_AT_CURSOR"},{PNAT_RANGE,0x14,"FIRE_SECONDARY_WEAPON_AT_CURSOR"}};
#define PNAT_BLOCKED_COUNT (sizeof(pnat_blocked_ids)/sizeof(*pnat_blocked_ids))
typedef struct {
    void (*refresh)(char);                          /* d9cc30 */
    void *(*controller)(void *);                    /* 9fc550 */
    unsigned char (*option)(void *,const char *,unsigned char); /* 1265960 */
    void (*steer_mode)(void *,char);                /* dd030 */
    void (*steer_direct)(void *,char);              /* dd080 */
    void (*steer_reset)(void *,char);               /* ddb60 */
    void (*throttle)(float,char,char,char);         /* d88fe0 */
    void (*match_reset)(void);                      /* d97b50 */
    void (*state_handler)(int,unsigned char);       /* d870b0 */
    void (*range_handler)(int,float);               /* d87f70 */
    void (*space_original)(int,unsigned char);      /* d88710 */
    void (*cursor_original)(void *);                /* trampoline into d88a10 */
    int (*time)(double *);                          /* game time of this thread (TLS +0x300) */
} pnat_fns;
typedef struct {unsigned rva;void *target;unsigned char expected[16];unsigned __int128 original,patched;int installed;} pnat_patch;
typedef struct {uint64_t tick;unsigned kind,id;int value;} pnat_refusal;
#define PNAT_REFUSAL_RING 64u
#define PNAT_SLOTS_ROUTED 3u
static const unsigned pnat_vf_slots[PNAT_SLOTS_ROUTED]={6,7,10};
static const unsigned pnat_vf_originals[PNAT_SLOTS_ROUTED]={0xd8a680,0xd8b3e0,0xd8b790};
static const unsigned pnat_vf_bases[PNAT_SLOTS_ROUTED]={0xea5a70,0xea6280,0xea66c0};
static int pilot_device_armed; /* lockstep_pilot_device.h (tentative definition) */
static unsigned char *pnat_base;
static pnat_fns pnat_fn;
static int pnat_armed,pnat_capture_requests,pnat_installed,pnat_restored=1,pnat_pins_checked,pnat_failed;
enum {PNAT_MODE_NATIVE=1,PNAT_MODE_STOCK=2};
static unsigned pnat_mode;   /* set when armed */
static volatile LONG pnat_live;
static const char *pnat_stage="off",*pnat_fail_stage;
static uint32_t pnat_block_bits[PNAT_KINDS][16];
static unsigned char *pnat_controller,*pnat_ship,*pnat_input;
static void **pnat_vector_entry;
__attribute__((used)) static void *volatile pnat_route_controller;  /* read by the routers below */
__attribute__((used)) static void *pnat_vf_orig[PNAT_SLOTS_ROUTED],*pnat_vf_base[PNAT_SLOTS_ROUTED];
__attribute__((used)) static volatile LONG64 pnat_vf_hits[PNAT_SLOTS_ROUTED];
__attribute__((used)) static void *pnat_cursor_resume;
static int pnat_vf_installed[PNAT_SLOTS_ROUTED];
static float pnat_req_values[6];
static unsigned pnat_req_mask;
static uint64_t pnat_req_tick;
static pnat_refusal pnat_refusals[PNAT_REFUSAL_RING];
static unsigned pnat_refusal_head,pnat_refusal_tail;
static unsigned char pnat_range_active[64];
/* Refused fire inputs still reach the record (trigger bits as the stock
 * callback 7e8ef0 would set 38f0728 / 38f072a; native_gate_pilot_fire fires
 * from bit 0 on every client): primary = states 0x18/0x30, ranges 0x11/0x13;
 * secondary = states 0x19/0x31, ranges 0x12/0x14. Cleared when SPACE leaves. */
static unsigned char pnat_fire_held[8];
static unsigned pnat_natural=2,pnat_flight_seen=2,pnat_assist=2,pnat_stop_in_menu=2;
static volatile LONG64 pnat_auth_routed,pnat_auth_natural,pnat_auth_override,pnat_space_on,pnat_space_off,pnat_cursor_calls,
    pnat_windows,pnat_dispatched[PNAT_KINDS],pnat_refused,pnat_refusal_dropped,pnat_wrapped,pnat_req_calls,pnat_req_records,
    pnat_ui_passes,pnat_passes,pnat_async_failures;

/* (2) Routers: compare the controller, then a plain jump; every register and
 * stack argument reaches the target unchanged. */
__attribute__((naked)) static void pnat_vf6(void) {
    __asm__ volatile("cmpq pnat_route_controller(%rip), %rcx\n\tjne 1f\n\tlock incq pnat_vf_hits(%rip)\n\t"
        "jmpq *pnat_vf_base(%rip)\n1:\n\tjmpq *pnat_vf_orig(%rip)\n\t");
}
__attribute__((naked)) static void pnat_vf7(void) {
    __asm__ volatile("cmpq pnat_route_controller(%rip), %rcx\n\tjne 1f\n\tlock incq pnat_vf_hits+8(%rip)\n\t"
        "jmpq *pnat_vf_base+8(%rip)\n1:\n\tjmpq *pnat_vf_orig+8(%rip)\n\t");
}
__attribute__((naked)) static void pnat_vf10(void) {
    __asm__ volatile("cmpq pnat_route_controller(%rip), %rcx\n\tjne 1f\n\tlock incq pnat_vf_hits+16(%rip)\n\t"
        "jmpq *pnat_vf_base+16(%rip)\n1:\n\tjmpq *pnat_vf_orig+16(%rip)\n\t");
}
/* (5) The first 18 bytes of d88a10 (pinned): mov rax,rsp; mov [rax+8],rcx;
 * push rbx; push rbp; push r14; sub rsp,0xa0. No RIP-relative operand, no
 * branch target; the body continues at d88a22 with the stock frame. */
__attribute__((naked)) static void pnat_cursor_trampoline(void) {
    __asm__ volatile(".byte 0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x53,0x55,0x41,0x56\n\t"
        ".byte 0x48,0x81,0xec,0xa0,0x00,0x00,0x00\n\tjmpq *pnat_cursor_resume(%rip)\n\t");
}
static const unsigned char pnat_cursor_prologue[18]={0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x53,0x55,0x41,0x56,0x48,0x81,0xec,0xa0,0x00,0x00,0x00};

static unsigned char pnat_authority(void *controller);
static void pnat_dispatch_action(unsigned char *map,int id);
static void pnat_dispatch_state(unsigned char *map,int id,unsigned char active);
static void pnat_dispatch_range(unsigned char *map,int id,float value);
static void pnat_cursor(void *controller);
static pnat_patch pnat_patches[]={
    {PNAT_AUTHORITY_RVA,(void*)pnat_authority,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x83,0x3d,0x7a,0xda,0x1b,0x02,0x00,0x48,0x8b},0,0,0},
    {0x1255cd0u,(void*)pnat_dispatch_action,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48},0,0,0},
    {0x1255dd0u,(void*)pnat_dispatch_state,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57},0,0,0},
    {0x1255ee0u,(void*)pnat_dispatch_range,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48},0,0,0},
    {PNAT_CURSOR_RVA,(void*)pnat_cursor,{0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x53,0x55,0x41,0x56,0x48,0x81,0xec,0xa0,0x00},0,0,0}};
#define PNAT_PATCH_COUNT (sizeof(pnat_patches)/sizeof(*pnat_patches))

#define PNAT_U8(rva) ((volatile unsigned char*)(pnat_base+(rva)))
#define PNAT_F32(rva) ((volatile float*)(pnat_base+(rva)))
static int pnat_is_live(void) {return InterlockedCompareExchange(&pnat_live,0,0)!=0;}
static int pnat_blocked(unsigned kind,int id) {
    return kind<PNAT_KINDS && id>=0 && id<512 && (pnat_block_bits[kind][(unsigned)id>>5]>>((unsigned)id&31)&1u);
}
static const char *pnat_name(unsigned kind,unsigned id) {
    for(unsigned i=0;i<PNAT_BLOCKED_COUNT;i++) if(pnat_blocked_ids[i].kind==kind && pnat_blocked_ids[i].id==id) return pnat_blocked_ids[i].name;
    return "?";
}
static void pnat_tables(void) {
    memset(pnat_block_bits,0,sizeof(pnat_block_bits));
    for(unsigned i=0;i<PNAT_BLOCKED_COUNT;i++) pnat_block_bits[pnat_blocked_ids[i].kind][pnat_blocked_ids[i].id>>5]|=1u<<(pnat_blocked_ids[i].id&31);
}
/* Asynchronous failure (a handler or window found its precondition broken):
 * the next gate tick stops with 113; the refused input never ran. */
static void pnat_async_fail(const char *why) {
    if(!pnat_fail_stage) pnat_fail_stage=why;
    pnat_failed=1;InterlockedIncrement64(&pnat_async_failures);InterlockedIncrement64(&pilot_failures);
}
static unsigned char *pnat_player(void) {
    unsigned char *universe=NULL,*player=NULL;
    if(!read_local(pnat_base+PNAT_UNIVERSE,&universe,8)) return NULL;
    if(!read_local(universe?universe+0x230:pnat_base+PNAT_UNIVERSE_NONE,&player,8)) return NULL;
    return player;
}
/* The player's flight assist is not suspended and no player activity runs:
 * then neither d88fe0 nor d95180 reach 710ad0 (engine reset) or 9fd330. */
static int pnat_player_quiet(void) {
    unsigned char *player=pnat_player(),suspended=1;int activity=-1;
    return player && read_local(player+0x6aad,&suspended,1) && !suspended && read_local(player+0x6ab4,&activity,4) && activity==0;
}
static int pnat_window_ok(void) {
    return pnat_is_live() && !*PNAT_U8(PNAT_FLIGHT) && !*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT) && !*PNAT_U8(PNAT_HIGHWAY) &&
        pnat_player_quiet();
}
static void pnat_refuse(unsigned kind,int id,int value) {
    InterlockedIncrement64(&pnat_refused);
    if(kind==PNAT_STATE && !value) return;                /* releases are counted, presses logged */
    if(kind==PNAT_RANGE) {                                 /* log the edge from rest to deflection */
        unsigned slot=(unsigned)id&63;int deflected=value!=0;
        if(pnat_range_active[slot]==deflected) return;
        pnat_range_active[slot]=(unsigned char)deflected;
        if(!deflected) return;
    }
    if(pnat_refusal_head-pnat_refusal_tail>=PNAT_REFUSAL_RING) {InterlockedIncrement64(&pnat_refusal_dropped);return;}
    pnat_refusal *r=&pnat_refusals[pnat_refusal_head%PNAT_REFUSAL_RING];
    r->tick=stats.issued;r->kind=kind;r->id=(unsigned)id;r->value=value;++pnat_refusal_head;
}
static void pnat_fire_input(unsigned kind,int id,int held) {
    static const int states[8]={0x18,0x30,-1,-1,0x19,0x31,-1,-1},ranges[8]={-1,-1,0x11,0x13,-1,-1,0x12,0x14};
    for(unsigned i=0;i<8;i++) if(id==(kind==PNAT_STATE?states[i]:kind==PNAT_RANGE?ranges[i]:-1)) pnat_fire_held[i]=(unsigned char)(held!=0);
}
static unsigned char pnat_fire_buttons(void) {
    unsigned char b=0;
    for(unsigned i=0;i<4;i++) if(pnat_fire_held[i]) b|=1;
    for(unsigned i=4;i<8;i++) if(pnat_fire_held[i]) b|=4;
    return b;
}
/* (1) The stock predicate d8dd50, re-stated: autopilot, or no flight control
 * with assist (vfunc slot 49, argument 0) outside a highway, or a ship whose
 * +0xd1 is clear. */
static unsigned char pnat_authority_stock(void *controller) {
    unsigned char *ship=NULL,flag=0;
    if(*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) return 1;
    if(!*PNAT_U8(PNAT_FLIGHT)) {
        void **vtable=*(void***)controller;
        if(((unsigned char(*)(void*,unsigned char))vtable[49])(controller,0) && !*PNAT_U8(PNAT_HIGHWAY)) return 1;
    }
    if(!read_local((unsigned char*)controller+0x28,&ship,8) || !ship || !read_local(ship+0xd1,&flag,1)) return 1;
    return !flag;
}
static int pslot_authority(void *controller,unsigned char *answer); /* lockstep_pilot_slots.h */
static unsigned char pnat_authority(void *controller) {
    unsigned char answer=0;
    if(pslot_authority(controller,&answer)) return answer;
    if(controller && controller==pnat_route_controller && pnat_is_live()) {
        InterlockedIncrement64(&pnat_auth_routed);
        if(!pnat_authority_stock(controller)) InterlockedIncrement64(&pnat_auth_override); /* the stock answer would hand motion over */
        return 1;
    }
    InterlockedIncrement64(&pnat_auth_natural);return pnat_authority_stock(controller);
}
static unsigned char pnat_assist_of(void *controller) {
    void **vtable=*(void***)controller;return ((unsigned char(*)(void*,unsigned char))vtable[49])(controller,0);
}
/* (5) Cursor and direct mouse steering with flight control for its body. */
static void pstk_cursor(void *controller);
static void pnat_cursor(void *controller) {
    if(pnat_mode==PNAT_MODE_STOCK) {pstk_cursor(controller);return;}
    InterlockedIncrement64(&pnat_cursor_calls);
    if(!pnat_window_ok()) {pnat_fn.cursor_original(controller);return;} /* flight control 0: the stock body zeroes */
    volatile unsigned char *flight=PNAT_U8(PNAT_FLIGHT);
    *flight=1;InterlockedIncrement64(&pnat_windows);
    __try {pnat_fn.cursor_original(controller);} __finally {*flight=0;}
}
/* (4) Handlers. The window form runs the stock handler with flight control;
 * the restatements keep only the input side of the stock body. */
static int pnat_handler_ok(void) {
    if(pnat_window_ok()) return 1;
    pnat_async_fail("input_precondition");return 0;
}
static void pnat_state_window(int id,unsigned char active) {          /* 0xf FULLFORWARD */
    if(!pnat_handler_ok()) return;
    volatile unsigned char *flight=PNAT_U8(PNAT_FLIGHT);
    *flight=1;InterlockedIncrement64(&pnat_windows);
    __try {pnat_fn.state_handler(id,active);} __finally {*flight=0;}
}
static void pnat_range_window(int id,float value) {                   /* 4 THROTTLE */
    if(!pnat_handler_ok()) return;
    volatile unsigned char *flight=PNAT_U8(PNAT_FLIGHT);
    *flight=1;InterlockedIncrement64(&pnat_windows);
    __try {pnat_fn.range_handler(id,value);} __finally {*flight=0;}
}
/* 0xd ACCELERATE / 0xe DECELERATE: key flag and ramp base only (the stock
 * press also resets the occupied ship's engine factors, 6d9c10/6d8960). */
static void pnat_state_accel(int id,unsigned char active) {
    if(!pnat_handler_ok() || *PNAT_U8(PNAT_THROTTLE_LOCK)) return;
    *PNAT_F32(PNAT_BASE_SETPOINT)=*PNAT_F32(PNAT_SETPOINT);
    *PNAT_U8(id==0xd?PNAT_KEY_ACCEL:PNAT_KEY_DECEL)=active;
    if(!active && fabsf(*PNAT_F32(PNAT_AXIS_LAST))<0.0001f) *(volatile double*)(pnat_base+PNAT_HOLD)=-1.0;
}
/* 0x10 FULLREVERSE with flight control and no autopilot: the throttle step only
 * (no autopilot stop, highway brake, stop-activity event or engine reset). */
static void pnat_state_reverse(int id,unsigned char active) {
    (void)id;
    if(!pnat_handler_ok() || *PNAT_U8(PNAT_THROTTLE_LOCK)) return;
    volatile unsigned char *held=PNAT_U8(PNAT_KEY_REVERSE);
    if(!active) {if(*held) {pnat_fn.throttle(-*PNAT_F32(PNAT_SETPOINT),0,0,1);*held=0;}}
    else if(!*held) {pnat_fn.throttle(-1.0f-*PNAT_F32(PNAT_SETPOINT),0,0,1);*held=1;}
}
/* 10 MOUSEWHEEL: the stock throttle step (no feedback or engine reset). */
static void pnat_range_wheel(int id,float value) {
    (void)id;
    if(!pnat_handler_ok() || *PNAT_U8(PNAT_THROTTLE_LOCK) || fabsf(value)<0.0001f) return;
    unsigned char *config=NULL;float step=0;
    if(!read_local(pnat_base+PNAT_CONFIG,&config,8) || !config || !read_local(config+0x6c,&step,4)) {pnat_async_fail("wheel_config");return;}
    *PNAT_F32(PNAT_BASE_SETPOINT)=*PNAT_F32(PNAT_SETPOINT);
    pnat_fn.throttle(value*step*0.008333334f,1,1,0);
}
/* 0x7b STOP: throttle to zero, full stop, match speed off (no SETA cancel,
 * autopilot stop, highway brake or boost/engine calls). */
static void pnat_action_stop(int id) {
    (void)id;
    if(!pnat_handler_ok() || *PNAT_U8(PNAT_THROTTLE_LOCK)) return;
    pnat_fn.throttle(-*PNAT_F32(PNAT_SETPOINT),0,1,0);*PNAT_U8(PNAT_FULL_STOP)=1;pnat_fn.match_reset();
}
/* The InputMapping multimap: header +8 root, node parent/left/right, key +0x20,
 * handler +0x28. Equal range and successor exactly as 1255cd0/1255dd0/1255ee0. */
typedef struct pnat_node {struct pnat_node *parent,*left,*right;int color,spare,key,spare2;void *handler;} pnat_node;
static pnat_node *pnat_first(pnat_node *header,int key,pnat_node **upper) {
    pnat_node *x=header->left,*up=header;
    while(x) {
        if(key<x->key) {up=x;x=x->left;}
        else if(key>x->key) x=x->right;
        else {
            for(pnat_node *r=x->right;r;) if(key<r->key) {up=r;r=r->left;} else r=r->right;
            pnat_node *low=x;
            for(pnat_node *l=x->left;l;) if(l->key>=key) {low=l;l=l->left;} else l=l->right;
            *upper=up;return low;
        }
    }
    return NULL;
}
static pnat_node *pnat_next(pnat_node *n) {
    if(n->right) {n=n->right;while(n->left) n=n->left;return n;}
    pnat_node *p=n->parent;
    while(p->left!=n) {n=p;p=p->parent;}
    return p;
}
#include "lockstep_pilot_stock.h"
/* Slots 2..N (lockstep_pilot_slots.h, included after lockstep_virtual_players.h). */
static int pslot_pass(uint64_t tick);static int pslot_replay(uint64_t tick);static void pslot_restore(void);
/* A slot ship's death (lockstep_pilot_death.h, included after the presentation). */
static int pdeath_pass(uint64_t tick,unsigned char *controller);static void pdeath_restore(void);static const char *pdeath_why;
static const char *pslot_why;static int pres_pass(uint64_t tick);static void pres_restore(void);static const char *pres_why;static int vps_pins(void);static int vpl_pins(void);
static void pnat_dispatch_action(unsigned char *map,int id) {
    InterlockedIncrement64(&pnat_dispatched[PNAT_ACTION]);
    if(pnat_mode==PNAT_MODE_STOCK) {pstk_dispatch(PSTK_ACTION,map,id,0);return;}
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)map,id,&upper);
    if(!n) return;
    int live=pnat_is_live(),blocked=live && pnat_blocked(PNAT_ACTION,id);
    if(blocked && n!=upper) pnat_refuse(PNAT_ACTION,id,1);
    while(n!=upper) {
        pnat_node *next=pnat_next(n);void *h=n->handler;
        if(blocked) {}
        else if(live && id==0x7b && h==pnat_base+PNAT_ACTION_HANDLER) {InterlockedIncrement64(&pnat_wrapped);pnat_action_stop(id);}
        else ((void(*)(int))h)(id);
        n=next;
    }
}
static void pnat_dispatch_state(unsigned char *map,int id,unsigned char active) {
    InterlockedIncrement64(&pnat_dispatched[PNAT_STATE]);
    if(pnat_mode==PNAT_MODE_STOCK) {pstk_dispatch(PSTK_STATE,map,id,active?1u:0u);return;}
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40),id,&upper);
    if(!n) return;
    int live=pnat_is_live(),blocked=live && pnat_blocked(PNAT_STATE,id);
    if(blocked && n!=upper) {pnat_refuse(PNAT_STATE,id,active);pnat_fire_input(PNAT_STATE,id,active);}
    while(n!=upper) {
        pnat_node *next=pnat_next(n);void *h=n->handler;
        if(blocked) {}
        else if(live && id>=0xd && id<=0x10 && h==pnat_base+PNAT_STATE_HANDLER) {
            InterlockedIncrement64(&pnat_wrapped);
            if(id==0xf) pnat_state_window(id,active);else if(id==0x10) pnat_state_reverse(id,active);else pnat_state_accel(id,active);
        }
        else ((void(*)(int,unsigned char))h)(id,active);
        n=next;
    }
}
static void pnat_dispatch_range(unsigned char *map,int id,float value) {
    InterlockedIncrement64(&pnat_dispatched[PNAT_RANGE]);
    if(pnat_mode==PNAT_MODE_STOCK) {uint32_t bits;memcpy(&bits,&value,4);pstk_dispatch(PSTK_RANGE,map,id,bits);return;}
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x80),id,&upper);
    if(!n) return;
    int live=pnat_is_live(),blocked=live && pnat_blocked(PNAT_RANGE,id);
    if(blocked && n!=upper) {pnat_refuse(PNAT_RANGE,id,fabsf(value)>=0.1f?(value>0?1:-1):0);pnat_fire_input(PNAT_RANGE,id,fabsf(value)>=0.1f);}
    while(n!=upper) {
        pnat_node *next=pnat_next(n);void *h=n->handler;
        if(blocked) {}
        else if(live && (id==4 || id==10) && h==pnat_base+PNAT_RANGE_HANDLER) {
            InterlockedIncrement64(&pnat_wrapped);
            if(id==4) pnat_range_window(id,value);else pnat_range_wheel(id,value);
        }
        else ((void(*)(int,float))h)(id,value);
        n=next;
    }
}
/* (3) SPACE context callback: the input side of d88710 (and of d9c250 /
 * d9c590's tails) in the stock order; flight control and motion untouched. */
static void pnat_space(int context,unsigned char active) {
    if(context!=8) return;                                 /* d88710 handles SPACE only */
    if(!pnat_is_live()) {pnat_fn.space_original(context,active);return;}
    if(pnat_mode==PNAT_MODE_STOCK) {pstk_space(active);return;}
    volatile unsigned char *input=NULL;double now=0;
    if(!read_local(pnat_base+PNAT_INPUT,(void*)&input,8) || !input || !pnat_fn.time(&now)) {pnat_async_fail("space_input");return;}
    if(active) {
        double lead=*(volatile double*)(pnat_base+PNAT_LEAD);
        for(unsigned i=0;i<5;i++) ((volatile double*)(pnat_base+PNAT_TIMERS))[i]=now+lead;
        if(input[0x9053]) {
            unsigned char *options=NULL;
            if(read_local(pnat_base+PNAT_OPTIONS,&options,8) && options &&
               pnat_fn.option(options+0x118,(const char*)(pnat_base+PNAT_PERSISTENT_NAME),0)) {
                pnat_fn.steer_mode((void*)input,(char)*PNAT_U8(PNAT_STEER_SAVED));
                pnat_fn.steer_direct((void*)input,(char)*PNAT_U8(PNAT_DIRECT_SAVED));
            }
            *PNAT_U8(PNAT_STEER_SAVED)=0;*PNAT_U8(PNAT_DIRECT_SAVED)=0;
        }
        input[0x9064]=1;input[0x9062]=1;InterlockedIncrement64(&pnat_space_on);
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
        static const unsigned zero8[]={0x2f4b750,0x2f4b788,0x2f4b720,0x2f4b794},zero4[]={0x2f4b758,0x2f4b728,0x2f4b790};
        for(unsigned i=0;i<4;i++) *(volatile uint64_t*)(pnat_base+zero8[i])=0;
        for(unsigned i=0;i<3;i++) *(volatile uint32_t*)(pnat_base+zero4[i])=0;
        input[0x9064]=0;
        for(unsigned i=0;i<5;i++) ((volatile double*)(pnat_base+PNAT_TIMERS))[i]=now+0.5;
        input[0x9062]=0;memset(pnat_fire_held,0,sizeof(pnat_fire_held));InterlockedIncrement64(&pnat_space_off);
    }
    pnat_fn.refresh((char)(pnat_fn.controller(pnat_base+PNAT_CONTROLLER_KEY)!=NULL));
}
/* (6) The PlayerController's own requests: recorded, replaced by zero. */
static uintptr_t pnat_request(request_fn original,void *self,double value,int axis,double epsilon,unsigned char *controller) {
    unsigned group=(unsigned char*)self==controller+PNAT_LINEAR; /* 0 angular, 1 linear, as request_site */
    InterlockedIncrement64(&pnat_req_calls);
    if(axis<0 || axis>2 || !isfinite(value)) return original(self,value,axis,epsilon);
    if(pnat_req_tick!=stats.issued) {pnat_req_tick=stats.issued;pnat_req_mask=0;}
    pnat_req_values[group*3+(unsigned)axis]=(float)value;pnat_req_mask|=1u<<(group*3+(unsigned)axis);
    return original(self,0.0,axis,epsilon);
}
/* Hot path of every e5d380 call (pathing workers included): plain reads only.
 * The routed controller is set last at installation and cleared first. */
static unsigned char *pnat_request_owner(void *self) {
    unsigned char *controller=(unsigned char*)pnat_route_controller;
    if(!controller || ((unsigned char*)self!=controller+PNAT_LINEAR && (unsigned char*)self!=controller+PNAT_ANGULAR)) return NULL;
    return controller;
}
/* The recorded requests as fractions of the slot ship's own Agent limits, the
 * inverse of pilot_target: angular 0/1/2 by limits 6/5/7, linear x by 3, y by 4,
 * z by 0 forward or |2| reverse. */
static float pnat_fraction(float value,float limit) {return limit>0?value/limit:0.0f;}
static int pnat_request_record(uint64_t tick,const float limits[8],pilot_record *r) {
    if(pnat_req_tick!=tick || pnat_req_mask!=63u) return 0;
    static const unsigned angular[3]={6,5,7};
    for(unsigned a=0;a<3;a++) r->q[3+a]=pilot_quantize(pnat_fraction(pnat_req_values[a],limits[angular[a]]));
    r->q[0]=pilot_quantize(pnat_fraction(pnat_req_values[3],limits[3]));
    r->q[1]=pilot_quantize(pnat_fraction(pnat_req_values[4],limits[4]));
    float z=pnat_req_values[5];
    r->q[2]=pilot_quantize(z>=0?pnat_fraction(z,limits[0]):pnat_fraction(z,fabsf(limits[2])));
    r->mode|=PILOT_MODE_REQUESTS;InterlockedIncrement64(&pnat_req_records);return 1;
}
static int pnat_time_tls(double *now) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!slots || !slots[0] || !((unsigned char*)slots[0])[0x10]) return 0;
    memcpy(now,(unsigned char*)slots[0]+0x300,8);return isfinite(*now);
}
/* Whole-function pins (SHA-256 of the build 9.00 bytes): the patched entries,
 * the re-stated and skipped bodies, their callers and the routed vfuncs. */
static const struct {unsigned rva,size;const char *sha;} pnat_pin_table[]={
    {0xd8dd50,81,"d819afff6cb97c4b41d44bfb9a52a6290cf93cc53659d34ac994e1428cbc0600"},
    {0xd88a10,1476,"96b47ad180d1d361a92e06b9a81baac8eb69f7ce61da8b6f2a9e004baaa43c33"},
    {0xd88710,759,"e9600674841db99135a2509e27eca8147552ad5b6b9b383508e43c4c21613890"},
    {0x1255cd0,249,"dd68eff14ac7f102e961be6d0a2664b7919e333de8477cf6e83279e45199db5f"},
    {0x1255dd0,259,"21c54fb4bc0b12b0da42f5ba984f0c6f952621748e3d9a2491671dd90c4d2c0b"},
    {0x1255ee0,257,"aed9220ab8c1fd3b5ab649b587d579661a4f879e40b9e68f57877ac56c4c17c3"},
    {0x1251fe0,823,"bc6db3411bd918f6a3839f3b41b8def54544c4b53e390ef5e825fbe52768109b"},
    {0x1252320,823,"1c648d2881f5769cb3f3af586072818deb29ad174a60f2fe23efa3d3c35b9eed"},
    {0x1250170,1531,"6e5b37c6a65e7ea7dad86edc214cbea96184fa87f92d35fd1dbc368de4f18ab6"},
    {0xd870b0,2678,"8969d883e70ae2a200c26920930a32bc12828c1cec13ff60f71cdc8d76c8d48e"},
    {0xd87f70,2224,"93622b2cb9a655baf7be620258715f6261fff210e06d9c0af87fcb14d3fb2fce"},
    {0xd86c80,1058,"1fb85a66fe1f2d78992805a4c5226901e47242a26476de1258214dc88393a3a6"},
    {0xd88fe0,1371,"d30e434ac0bf2d4c61a5a06581240bf18a7560759775b206c42d690d611d8355"},
    {0xd97b50,118,"93bd60ff6d99194488dcb262ef08259c55751ef1565e912d155e1ef1fbc68800"},
    {0x1265960,334,"7cde4c286b0764bfc93dde6a8ca3eca2be8061b7e674b8bedec0732f3216d490"},
    {0xdd030,73,"ef27179544a73e5ce72198b6f02879370c283c7711e0669a90238bf0b6cbf1ce"},
    {0xdd080,114,"f57d25b8bacb6b6347102c18bd52c8c8ff74202408c85b92e9f1895b8e62a8eb"},
    {0xddb60,657,"743d435181b28145c49675ac223471429a37bd8aa5134e7d7d9a6035d6f81662"},
    {0xd898c0,1115,"53766ee9ce21098b21408c31564e9f7269968034675e837994b3c6bf758f580c"},
    {0xd95180,7221,"c55c35cea23632e2ab86e60c68f3222b7cab0160a788e662d67cde1309e63d33"},
    {0xd93440,4657,"b6342c28509fe6964c27cea83c31e82867b38b5e20634afafb93d0d38bb4e5af"},
    {0xd8a680,3416,"b0b97f49d47dc07a8093fce44c74184f39020b66a00b67ea8c26953db9a32f6d"},
    {0xd8b3e0,932,"1263a86c343dcf94c13414cd826c3772db6f1623d886d4f8a23626473900e25b"},
    {0xd8b790,536,"00d51884aa385b3e085f78e78c754d7de98a664281fd0f7276932cc3481f0792"},
    {0xd9bf70,163,"b8c0b8b83decfbed296f7de7dc147d4d8e81e16a9ee811f4be000a88cd36b4c8"},
    {0xd9c020,43,"b4e8add47540eeaeff0a325d5cb821aeceae4bc8c9d11b37e30d71e065ed4654"},
    {0xea5a70,25,"11a6d2c6519e9c4b3763d2e36fd123b4d831564236675cf5165a1b7acf159c08"},
    {0xea5a90,2025,"1d90a6295a8658de5d2ba9cf75b16d37cb74bfcc5f9a517eec08202c3f4dcfd4"},
    {0xea6280,16,"143b55e46279245a9ebd64c4d06aaaddb4d9c17be6de135b2c62327c9f15750b"},
    {0xea6290,1065,"de93c2c309e8cf5260428962447feffdb5873c08992b8fbbc322a814f5c133bf"},
    {0xea66c0,102,"2750f7c7998f61e698dfbc98c71f0e0596ca99f1514bc13eac4f124a4d202326"},
    {0xd9c590,902,"5986110543f3a5e70fb3fc671e121375b6f136cb633922e616b234afd8c67cd0"},
    {0xd9c250,831,"3b8bc7c183957b354e5b52bd2a47754504e24a396081aeb586fe4af1a2372e81"},
    {0xd97e80,3926,"2b297f805f6795a221c5c4ab139615e396380a46d000085902f41cca2aa50724"}};
static unsigned pin_failed_rva;  /* the first pin that failed in the table that refused (any stock piloting table) */
static int pnat_pins(void) {
    if(!pnat_pins_checked) {
        int ok=!memcmp((const void*)pnat_cursor_trampoline,image+PNAT_CURSOR_RVA,sizeof(pnat_cursor_prologue)) &&
            !memcmp(pnat_cursor_prologue,image+PNAT_CURSOR_RVA,sizeof(pnat_cursor_prologue));
        if(!ok) pin_failed_rva=PNAT_CURSOR_RVA;
        for(unsigned i=0;ok && i<sizeof(pnat_pin_table)/sizeof(*pnat_pin_table);i++)
            if(!(ok=hash_ok(image+pnat_pin_table[i].rva,pnat_pin_table[i].size,pnat_pin_table[i].sha))) pin_failed_rva=pnat_pin_table[i].rva;
        pnat_pins_checked=ok?1:-1;
    }
    return pnat_pins_checked==1;
}
/* Production: the loaded image and its functions. Tests replace both. */
static void pnat_use_image(void) {
    pnat_base=image;
    pnat_fn.refresh=(void(*)(char))(image+0xd9cc30);pnat_fn.controller=(void*(*)(void*))(image+0x9fc550);
    pnat_fn.option=(unsigned char(*)(void*,const char*,unsigned char))(image+0x1265960);
    pnat_fn.steer_mode=(void(*)(void*,char))(image+0xdd030);pnat_fn.steer_direct=(void(*)(void*,char))(image+0xdd080);
    pnat_fn.steer_reset=(void(*)(void*,char))(image+0xddb60);pnat_fn.throttle=(void(*)(float,char,char,char))(image+0xd88fe0);
    pnat_fn.match_reset=(void(*)(void))(image+0xd97b50);pnat_fn.state_handler=(void(*)(int,unsigned char))(image+PNAT_STATE_HANDLER);
    pnat_fn.range_handler=(void(*)(int,float))(image+PNAT_RANGE_HANDLER);
    pnat_fn.space_original=(void(*)(int,unsigned char))(image+PNAT_SPACE_RVA);
    pnat_fn.cursor_original=(void(*)(void*))pnat_cursor_trampoline;pnat_fn.time=pnat_time_tls;
    pstk_fn.handoff=(pstk_handoff_fn)(image+0xd9c590);pstk_fn.release=(pstk_release_fn)(image+0xd9c250);
    pstk_fn.started=(pstk_event_fn)(image+0x70eb10);pstk_fn.stopped=(pstk_event_fn)(image+0x70ec00);
    pstk_fn.curve=(pstk_curve_fn)(image+0x14e0180);pstk_fn.authority=(unsigned char(*)(void*))(image+PNAT_AUTHORITY_RVA);
    pstk_fn.aim=(pstk_aim_fn)(image+PSTK_AIM_RVA);pstk_fn.transform=(pstk_transform_fn)(image+0x3dde10);
    pstk_fn.ship=(pstk_ship_fn)(image+0x55aeb0);pstk_fn.ray=(pstk_ray_fn)(image+0x719750);pstk_fn.zone=pstk_zone_native;
    pview_fn.fov=(float(*)(void*))(image+0x97c140);pview_fn.floating=(unsigned char(*)(void))(image+0x9fbf50);
    pview_fn.portable=world_identity_portable;
    for(unsigned i=0;i<PSTK_HANDLERS;i++) pstk_handler_ptrs[i]=image+pstk_handlers[i];
}
/* Stock mode's own pins: the Flight Started/Stopped posts, the response curve, the
 * player handlers a replayed id may reach, the controller getter (the rest is in
 * pnat_pin_table). */
static const struct {unsigned rva,size;const char *sha;} pstk_pin_table[]={
    {0x70eb10,230,"1a4d39658051fd2b3ad9856415fd425850f0f5093798fe1f4cba798dfba84a6e"},
    {0x70ec00,230,"2865ebcc782e41edae8e5a8f87ab1ce21656f4bde4236f33849a4c70ead0c82c"},
    {0x14e0180,188,"4da5ae6c797fe2ad25f6d3abaccf33ac1e2d0d97a92036b173660554da0e969b"},
    {0x71bac0,2935,"a491b89effdc2077f03828adaa8efa6aad734413f50f833d5b0bbef6c1063698"},
    {0x71c640,727,"73d5dc691616f3479bb21e38bb9aff2dbb8e03b1c3b1cc213ecf3f7f1a238815"},
    {0x9fc550,277,"fca3a29a2c0ca40fafa27dac44da67e9b8bd0aa52b3d812c72b72f4ed212ca67"},
    {PSTK_AIM_RVA,1379,"f20d8c3f389550961e8198ce56ee827dd4c612acdfd13ce6c5dbfd52f73893dd"},  /* PlayerShootController::vfunc21 */
    {0x7e8e60,40,"f98e25330df0d4fd58bc9216ee921abea58906f1d77f560b7231e580dadb7f2f"},     /* vfunc15: manual aim (+0x9053) */
    {0x719750,286,"291c0efe3d419fb4640c464a7528a070e2903e76a9c4588c9cce2603fe796ab7"},    /* camera cursor ray point */
    {0x55aeb0,142,"b95c394b389e23c19cd668e9d15d5210cafde0810e5ebe1d3d957a73757b2a51"},    /* the player's ship */
    {0x3dde10,2065,"3b757e4ac925248a081c1a675a3491b28a368289fb7434ced473c75dd073d0eb"},   /* transform relative to */
    {0xd924d0,3221,"63cf643f55130eb8c05098e93155fd88b2d42a36a1cbba73c96ce6fd27343403"},   /* coefficients (view block d93052) */
    {0x54ee80,443,"d796f5c9c0109cb7da0d3b56876b2fec9ba2a121ed6c37586b048243dc2b853f"},    /* Engine cockpit factor (54efb5) */
    {0x7d4d40,383,"be9dd487351f9d994c463aabb7e42c88fabd5e710ead1b547330a8a55b7c72da"},    /* Ship::vfunc836 (7d4d7a) */
    {0x81bb50,108,"82bf6a4b51b01ca86081f1d577f2f5e28b432434305316f6fe1229e7724def36"},    /* Weapon::vfunc1041 (81bba3) */
    {0x6093e0,121,"d1e8da6a6ed527239f63ac04dcdffeb102038d50da905c4754c0740639e29c42"},    /* MissileLauncher::vfunc1041 (609440) */
    {0x9fbf50,327,"4d1a9d4f7ef426b13b73a81b627af6091350cc16bcd0dc77338b959d0eee8dce"},    /* floating view */
    {0x97c140,64,"77e5a7025cc9214fb24212fd50f05f2f158e159ab000d24ae649826227b5e11b"}};    /* camera FOV factor (entry) */
static int pstk_pins_checked;
static int pstk_pins(void) {
    if(!pstk_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(pstk_pin_table)/sizeof(*pstk_pin_table);i++)
            if(!(ok=hash_ok(image+pstk_pin_table[i].rva,pstk_pin_table[i].size,pstk_pin_table[i].sha))) pin_failed_rva=pstk_pin_table[i].rva;
        pstk_pins_checked=ok?1:-1;
    }
    return pstk_pins_checked==1;
}
static int pnat_arm_using(int requests) {
    if(pnat_armed) return pnat_mode==PNAT_MODE_NATIVE && requests==pnat_capture_requests; /* identical rerun after a loading-UI reload */
    if(!pilot_armed || !pilot_suppress || pilot_device_armed || !pnat_base || !pnat_fn.refresh) return 0;
    pnat_tables();pnat_mode=PNAT_MODE_NATIVE;pnat_capture_requests=requests!=0;pnat_armed=1;pnat_stage="armed";pnat_restored=1;return 1;
}
/* Why gate.pilot_stock or gate.pilot_stock_pins refused (returned to the probe): a
 * stage name, with the RVA of the first failing pin for a pin table. */
static char pstk_arm_why[48]="-";
static int pstk_arm_refuse(const char *why) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"%s",why);return 0;}
static int pstk_arm_refuse_pin(const char *table) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"%s:%x",table,pin_failed_rva);return 0;}
/* Stock mode: every client of a wire session with parked controls. slots = the
 * stock slots (bit s-1; slot 1, the ship every client sits in, always): capture=1
 * on the client whose own slot is one of them, which uses X4's own input (no device
 * sampler); every other client samples its own slot. Slots 2..N fly in slot scopes
 * (lockstep_pilot_slots.h). */
static int pslot_arm_using(unsigned mask); /* lockstep_pilot_slots.h */
static int pnat_arm_stock_using(int capture,unsigned slots) {
    if(pnat_armed) {
        if(pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("rerun_mode");
        if((capture!=0)!=pstk_capturing) return pstk_arm_refuse("rerun_capture");
        return slots==pstk_slot_mask?1:pstk_arm_refuse("rerun_slots");
    }
    unsigned all=pilot_slot_count>=1 && pilot_slot_count<=PILOT_SLOTS?(1u<<pilot_slot_count)-1u:0u;
    int mine=stats.slot>=1 && stats.slot<=pilot_slot_count && (slots>>(stats.slot-1)&1u);
    if(!pilot_armed) return pstk_arm_refuse("pilot_not_armed");
    if(!pilot_suppress) return pstk_arm_refuse("no_suppress");
    if(pilot_source!=PILOT_SOURCE_WIRE) return pstk_arm_refuse("not_wire");
    if(!pnat_base || !pnat_fn.refresh || !pstk_fn.handoff) return pstk_arm_refuse("no_functions");
    if(!(slots&1u)) return pstk_arm_refuse("slots_without_1");
    if(slots&~all) return pstk_arm_refuse("slots_range");
    if((capture!=0)!=mine) return pstk_arm_refuse(capture?"capture_not_a_stock_slot":"stock_slot_not_capturing");
    if(capture && pilot_device_armed) return pstk_arm_refuse("capture_with_device");
    if((slots&~1u) && !pslot_arm_using(slots)) return 0;
    pnat_tables();pnat_mode=PNAT_MODE_STOCK;pnat_capture_requests=0;pstk_capturing=capture!=0;pstk_slot=1;pilot_stock_slots=slots;
    pstk_slot_mask=slots;pstk_capture_slot=capture?stats.slot:0;pstk_scope_slot=0;
    memset(&pstk_build,0,sizeof(pstk_build));memset(pstk_ring,0,sizeof(pstk_ring));memset(pstk_locals,0,sizeof(pstk_locals));
    memset(pstk_slot_frames,0,sizeof(pstk_slot_frames));
    pstk_capture_start=pstk_first_apply=pstk_cursor_tick=0;pstk_flight=0;pstk_flight_known=0;pstk_local_active=-1;pstk_cursor_live_set=0;
    pnat_armed=1;pnat_stage="armed";pnat_restored=1;return 1;
}
static int pnat_arm(int requests) {
    if(!pnat_armed && (stats.issued || GetCurrentThreadId()!=ui_thread || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !pnat_pins())) return 0;
    if(!pnat_armed) pnat_use_image();
    return pnat_arm_using(requests);
}
static int pslot_pins(void);static void pslot_use_image(void); /* lockstep_pilot_slots.h */
/* Every pin table stock piloting needs, first failure named. Cached per table: the probe
 * calls this early (gate.pilot_stock_pins), before other modules patch X4; every table
 * then answers from that check when gate.pilot_stock arms after them. */
static int pnat_stock_pins(unsigned slots) {
    if(!pnat_pins()) return pstk_arm_refuse_pin("pins_native");
    if(!pstk_pins()) return pstk_arm_refuse_pin("pins_stock");
    if(!(slots&~1u)) return 1;
    if(!pslot_pins()) return pstk_arm_refuse_pin("pins_slots");
    if(!vps_pins()) return pstk_arm_refuse_pin("pins_construct");
    if(!vpl_pins()) return pstk_arm_refuse_pin("pins_players");
    return 1;
}
static int pnat_arm_stock(int capture,unsigned slots) {
    if(!pnat_armed) {
        if(stats.issued) return pstk_arm_refuse("after_first_tick");
        if(GetCurrentThreadId()!=ui_thread) return pstk_arm_refuse("thread");
        if(!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) return pstk_arm_refuse("cx16");
        if(!pnat_stock_pins(slots)) return 0;
    }
    if(!pnat_armed) {pnat_use_image();if(slots&~1u) pslot_use_image();}
    return pnat_arm_stock_using(capture,slots);
}
/* Stock mode keeps the authority predicate (patch 0; with slots 2..N it answers their
 * controllers from their snapshots, lockstep_pilot_slots.h) and the transform vtable slots. */
static int pnat_patch_used(unsigned i) {
    return pnat_mode!=PNAT_MODE_STOCK || pnat_patches[i].rva!=PNAT_AUTHORITY_RVA || (pstk_slot_mask&~1u);
}
static int pnat_routers_used(void) {return pnat_mode!=PNAT_MODE_STOCK;}
static int pnat_swap16(unsigned rva,unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;unsigned char *p=pnat_base+rva;
    if(((uintptr_t)p&15) || !VirtualProtect(p,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)p,expected,value);
    VirtualProtect(p,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p,16);return actual==expected;
}
static int pnat_swap8(void *slot,void *expected,void *value,int protect) {
    DWORD old=0,ignored;
    if(((uintptr_t)slot&7) || (protect && !VirtualProtect(slot,8,PAGE_READWRITE,&old))) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)slot,value,expected);
    if(protect) VirtualProtect(slot,8,old,&ignored);
    return actual==expected;
}
static unsigned __int128 pnat_jump(void *target) {
    unsigned char b[16]={0x48,0xb8};unsigned __int128 v;
    memcpy(b+2,&target,8);b[10]=0xff;b[11]=0xe0;memset(b+12,0xcc,4);memcpy(&v,b,16);return v;
}
/* Stock aim: PlayerShootController vtable slot 21 (lockstep_pilot_stock.h pstk_aim). */
static void **pstk_aim_slot(void) {return (void**)(pnat_base+PSTK_SHOOT_VTABLE+8*PSTK_AIM_SLOT);}
static int pstk_aim_install(void) {
    if(pstk_aim_installed) return 1;
    if(!pstk_fn.aim || !pnat_swap8(pstk_aim_slot(),(void*)pstk_fn.aim,(void*)pstk_aim,1)) return 0;
    pstk_aim_installed=1;InterlockedExchange(&pstk_aim_on,1);return 1;
}
static int pstk_aim_uninstall(void) {
    InterlockedExchange(&pstk_aim_on,0);
    if(!pstk_aim_installed) return 1;
    if(!pnat_swap8(pstk_aim_slot(),(void*)pstk_aim,(void*)pstk_fn.aim,1)) return 0;
    pstk_aim_installed=0;return 1;
}
static int pstk_aim_intact(void) {return pstk_aim_installed && *(void*volatile*)pstk_aim_slot()==(void*)pstk_aim;}
/* The context callback vector of the input object (+0x10 begin, +0x18 end). */
static unsigned pnat_vector_find(unsigned char *input,void *value,void ***where) {
    void **begin=NULL,**end=NULL,*entry=NULL;unsigned found=0;
    if(!input || !read_local(input+0x10,&begin,8) || !read_local(input+0x18,&end,8) || !begin || end<begin || end-begin>256) return 0;
    for(void **p=begin;p<end;p++) if(read_local(p,&entry,8) && entry==value) {++found;if(where) *where=p;}
    return found;
}
static void pnat_uninstall(void) {
    InterlockedExchange(&pnat_live,0);pnat_route_controller=NULL;
    int ok=1;
    if(pnat_vector_entry) { /* found again by value: the vector may have grown */
        void **where=NULL;
        if(pnat_vector_find(pnat_input,(void*)pnat_space,&where)==1 && pnat_swap8(where,(void*)pnat_space,pnat_base+PNAT_SPACE_RVA,0))
            pnat_vector_entry=NULL;
        else ok=0;
    }
    for(unsigned i=PNAT_PATCH_COUNT;i-->0;) {
        pnat_patch *p=&pnat_patches[i];
        if(p->installed) {if(pnat_swap16(p->rva,p->patched,p->original)) p->installed=0;else ok=0;}
    }
    static void (*const routers[PNAT_SLOTS_ROUTED])(void)={pnat_vf6,pnat_vf7,pnat_vf10};
    for(unsigned i=0;i<PNAT_SLOTS_ROUTED;i++) if(pnat_vf_installed[i]) {
        void **slot=(void**)(pnat_base+PNAT_VTABLE+8*pnat_vf_slots[i]);
        if(pnat_swap8(slot,(void*)routers[i],pnat_vf_orig[i],1)) pnat_vf_installed[i]=0;else ok=0;
    }
    if(!pstk_aim_uninstall()) ok=0;
    if(!pview_uninstall()) ok=0;
    pnat_installed=!ok;pnat_restored=ok;
}
/* At start-2 on the MoveWorker, controls parked by our share: everything is
 * installed inert, then switched on together. */
static int pnat_install(unsigned char *controller) {
    unsigned char *ship=NULL,*input=NULL;
    if(pnat_installed || !controller || (pnat_base==image && !pnat_pins()) || !read_local(controller+0x28,&ship,8) || !ship ||
       !read_local(pnat_base+PNAT_INPUT,&input,8) || !input || pnat_vector_find(input,pnat_base+PNAT_SPACE_RVA,&pnat_vector_entry)!=1 ||
       pnat_vector_find(input,(void*)pnat_space,NULL)) {pnat_vector_entry=NULL;return 0;}
    void **vector_entry=pnat_vector_entry;pnat_vector_entry=NULL;
    static void (*const routers[PNAT_SLOTS_ROUTED])(void)={pnat_vf6,pnat_vf7,pnat_vf10};
    pnat_installed=1;pnat_restored=0;pnat_cursor_resume=pnat_base+PNAT_CURSOR_RESUME;
    for(unsigned i=0;pnat_routers_used() && i<PNAT_SLOTS_ROUTED;i++) {
        void **slot=(void**)(pnat_base+PNAT_VTABLE+8*pnat_vf_slots[i]);
        pnat_vf_orig[i]=pnat_base+pnat_vf_originals[i];pnat_vf_base[i]=pnat_base+pnat_vf_bases[i];
        if(!pnat_swap8(slot,pnat_vf_orig[i],(void*)routers[i],1)) {pnat_uninstall();return 0;}
        pnat_vf_installed[i]=1;
    }
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++) {
        pnat_patch *p=&pnat_patches[i];
        if(!pnat_patch_used(i)) continue;
        memcpy(&p->original,pnat_base+p->rva,16);
        if(memcmp(&p->original,p->expected,16)) {pnat_uninstall();return 0;}
        p->patched=pnat_jump(p->target);
        if(!pnat_swap16(p->rva,p->original,p->patched)) {pnat_uninstall();return 0;}
        p->installed=1;
    }
    if(!pnat_swap8(vector_entry,pnat_base+PNAT_SPACE_RVA,(void*)pnat_space,0)) {pnat_uninstall();return 0;}
    pnat_vector_entry=vector_entry;pnat_controller=controller;pnat_ship=ship;pnat_input=input;
    pnat_req_tick=0;pnat_req_mask=0;memset(pnat_range_active,0,sizeof(pnat_range_active));memset(pnat_fire_held,0,sizeof(pnat_fire_held));
    if(pnat_mode!=PNAT_MODE_STOCK) pnat_route_controller=controller;
    InterlockedExchange(&pnat_live,1);
    return 1;
}
static int pnat_intact(void) {
    static void (*const routers[PNAT_SLOTS_ROUTED])(void)={pnat_vf6,pnat_vf7,pnat_vf10};
    for(unsigned i=0;pnat_routers_used() && i<PNAT_SLOTS_ROUTED;i++)
        if(!pnat_vf_installed[i] || *(void*volatile*)(pnat_base+PNAT_VTABLE+8*pnat_vf_slots[i])!=(void*)routers[i]) return 0;
    for(unsigned i=0;i<PNAT_PATCH_COUNT;i++)
        if(pnat_patch_used(i) && (!pnat_patches[i].installed || memcmp(pnat_base+pnat_patches[i].rva,&pnat_patches[i].patched,16))) return 0;
    void **where=NULL;
    if(pnat_vector_find(pnat_input,(void*)pnat_space,&where)!=1 || pnat_vector_find(pnat_input,pnat_base+PNAT_SPACE_RVA,NULL)) return 0;
    pnat_vector_entry=where;return 1;
}
static int pnat_fail(const char *stage) {pnat_stage=stage;pilot_stage=stage;pnat_failed=1;return 0;}
/* One live pass (start-1 .. stop): every invariant the pieces rely on. */
static int pnat_check(unsigned char *controller,const uint32_t *counter,const unsigned char *contexts) {
    unsigned char *ship=NULL,*input=NULL;uint32_t count=0;unsigned char requested=0;
    InterlockedIncrement64(&pnat_passes);
    if(pnat_fail_stage) return pnat_fail(pnat_fail_stage);
    if(!pnat_intact()) return pnat_fail("native_patch_lost");
    pnat_flight_seen=*PNAT_U8(PNAT_FLIGHT);
    if(pnat_mode==PNAT_MODE_STOCK) {
        /* Flight control is the replayed state: anything else moved it on this client only. A dead slot 1
         * has neither (lockstep_pilot_death.h): X4's player sits in what the eject built. */
        int dead=pilot_slots[0].dead!=0;
        if(!dead && pnat_flight_seen!=pstk_flight) return pnat_fail("stock_flight_drift");
        if((!dead && (!controller || controller!=pnat_controller || !read_local(controller+0x28,&ship,8) || ship!=pnat_ship)) ||
           !read_local(pnat_base+PNAT_INPUT,&input,8) || input!=pnat_input) return pnat_fail("stock_controller_changed");
        pnat_stop_in_menu=*PNAT_U8(PNAT_STOP_IN_MENU);
        if(counter && contexts && read_local(counter,&count,4) && read_local(contexts,&requested,1) && (count || !requested))
            InterlockedIncrement64(&pnat_ui_passes);
        return 1;
    }
    if(pnat_flight_seen) return pnat_fail("native_flight_control");
    if(*(volatile uint64_t*)(pnat_base+PNAT_AUTOPILOT)) return pnat_fail("native_autopilot");
    if(*PNAT_U8(PNAT_HIGHWAY)) return pnat_fail("native_highway");
    if(!controller || controller!=pnat_controller || !read_local(controller+0x28,&ship,8) || ship!=pnat_ship ||
       !read_local(pnat_base+PNAT_INPUT,&input,8) || input!=pnat_input) return pnat_fail("native_controller_changed");
    pnat_natural=pnat_authority_stock(controller);
    if(!pnat_natural) return pnat_fail("native_authority");
    pnat_assist=pnat_assist_of(controller);
    if(!pnat_assist) return pnat_fail("native_assist_off");
    if(!pnat_player_quiet()) return pnat_fail("native_player_activity");
    pnat_stop_in_menu=*PNAT_U8(PNAT_STOP_IN_MENU); /* local throttle only: reported, not a stop */
    if(counter && contexts && read_local(counter,&count,4) && read_local(contexts,&requested,1) && (count || !requested))
        InterlockedIncrement64(&pnat_ui_passes);
    return 1;
}
/* A menu, the map or another view holds controls on this client. */
static int pnat_ui_hold(void) {
    uint32_t count=0;unsigned char requested=0;
    if(pilot_controls_owned) return pilot_controls_ui_hold();
    return !read_local(pnat_base+PNAT_COUNTER,&count,4) || !read_local(pnat_base+PNAT_CONTEXTS,&requested,1) || count || !requested;
}
/* The native schedule inside pilot_before_release_using (suppress on). */
static int pnat_pass(uint64_t tick,unsigned char *controller,pilot_refresh_fn refresh,uint32_t *counter,const unsigned char *contexts,
                     const unsigned char *flight,pilot_authority_fn authority) {
    int present=controller!=NULL;uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    /* A failure found outside the pass stops here, on the frame thread, with its own stage and reason. */
    if(pnat_mode==PNAT_MODE_STOCK && pnat_failed && pnat_fail_stage) return pnat_fail(pnat_fail_stage);
    /* A slot's ship died: before the pieces below rely on it. */
    if(pnat_mode==PNAT_MODE_STOCK && pdeath_armed && !pdeath_pass(tick,controller)) return pnat_fail(pdeath_why);
    /* UI replay: its hooks from the first pass of the session, off behind the window (lockstep_ui_replay.h). */
    if(pnat_mode==PNAT_MODE_STOCK && uirep_armed && !uirep_pass(tick)) return pnat_fail(uirep_why);
    /* Presentation of this client's own slot: started at the window start, ended at stop+1 before the release. */
    if(pnat_mode==PNAT_MODE_STOCK && pres_armed && !pres_pass(tick)) return pnat_fail(pres_why);
    /* Slots 2..N: players built at take-2, seated at take-1, checked, released at stop+1. */
    if(pnat_mode==PNAT_MODE_STOCK && (pstk_slot_mask&~1u) && !pslot_pass(tick)) return pnat_fail(pslot_why);
    /* Stock aim from the first pass of the session (neutral until the first replayed record). */
    if(pnat_mode==PNAT_MODE_STOCK && tick<=pilot_stop) {
        if(pnat_fail_stage) return pnat_fail(pnat_fail_stage);
        if(!pstk_aim_installed) {if(!pstk_aim_install()) return pnat_fail("stock_aim_install");}
        else if(!pstk_aim_intact()) return pnat_fail("stock_aim_lost");
        if(!pview_installed) {if(!pview_install()) return pnat_fail("stock_view_install");}
        else if(!pview_intact()) return pnat_fail("stock_view_lost");
    }
    if(tick==take) {
        if(!pilot_controls_take(refresh,counter,contexts,present)) return pnat_fail("controls_take");
        pnat_stage="parked";return 1;
    }
    if(tick==take+1) {
        if(!pilot_controls_check(refresh,counter,contexts,flight,authority,controller,present)) return pnat_fail("authority_lost");
        if(pnat_mode==PNAT_MODE_STOCK && (!pstk_slot || pstk_slot>pilot_slot_count || !pilot_slots[pstk_slot-1].player))
            return pnat_fail("stock_slot_not_occupied"); /* the stock slot must be the ship every client sits in */
        if(!pnat_install(controller)) return pnat_fail("native_install");
        if(pnat_mode==PNAT_MODE_STOCK) { /* parked: flight control 0 on every client; records from this tick on */
            pstk_flight=0;pstk_flight_known=1;pstk_capture_start=tick;pstk_first_apply=tick+pilot_delay;
        }
        pilot_controls_release(refresh,counter,present); /* contexts come back; pnat_space answers SPACE */
        pnat_stage="live";return 1;
    }
    if(tick>take+1 && tick<=pilot_stop) {
        if(!pnat_check(controller,counter,contexts)) return 0;
        if(pnat_mode==PNAT_MODE_STOCK) {
            const char *why="stock";
            if(!pstk_frame(tick,&why)) return pnat_fail(why);
            if((pstk_slot_mask&~1u) && tick>=pstk_first_apply && !pslot_replay(tick)) return pnat_fail(pslot_why);
            if(uirep_armed && !uirep_frame(tick,&why)) return pnat_fail(why);
        }
        return 1;
    }
    if(tick==pilot_stop+1) {
        pnat_uninstall();
        if(!pnat_restored) return pnat_fail("native_restore");
        /* Hand controls back through the stock path: a share now (the stock
         * callback leaves SPACE), released next pass (d9c590 takes over). */
        pnat_stage=pilot_controls_take(refresh,counter,contexts,present)?"returning":"released";return 1;
    }
    if(tick==pilot_stop+2 && pilot_controls_owned) {pilot_controls_release(refresh,counter,present);pnat_stage="returned";}
    return 1;
}
static void pnat_restore(void) {
    if(!pnat_armed) return;
    uirep_restore(); /* first: its adapter needs the seats */
    pres_restore();  /* the presentation next: its scope needs the seats */
    pslot_restore(); /* slots 2..N next: their seats use the authority patch */
    if(pdeath_armed) pdeath_restore();
    if(pnat_installed || pstk_aim_installed || pview_installed) pnat_uninstall();
    if(pnat_installed) pnat_stage="restore_refused";
}
static int pnat_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(pnat_refusal_tail!=pnat_refusal_head) {
        pnat_refusal *r=&pnat_refusals[pnat_refusal_tail%PNAT_REFUSAL_RING];
        static const char *const kinds[PNAT_KINDS]={"action","state","range"};
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:0x%x:%s:%d",n?",":"",(unsigned long long)r->tick,kinds[r->kind],r->id,
            pnat_name(r->kind,r->id),r->value);
        if(more<0 || (size_t)(n+more)>=capacity) break;
        n+=more;++pnat_refusal_tail;
    }
    return n;
}
static int pnat_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," native=%s mode=%s live=%d requests=%d auth=%lld/%lld/%lld natural=%u flight=%u assist=%u vf=%lld/%lld/%lld "
        "space=%lld/%lld cursor=%lld windows=%lld dispatch=%lld/%lld/%lld refused=%lld dropped=%lld wrapped=%lld req=%lld/%lld "
        "uihold=%lld passes=%lld async=%lld stopship=%u restored=%d",
        pnat_stage,pnat_mode==PNAT_MODE_STOCK?"stock":"native",pnat_is_live(),pnat_capture_requests,(long long)pnat_auth_routed,(long long)pnat_auth_natural,(long long)pnat_auth_override,
        pnat_natural,pnat_flight_seen,pnat_assist,(long long)pnat_vf_hits[0],(long long)pnat_vf_hits[1],(long long)pnat_vf_hits[2],
        (long long)pnat_space_on,(long long)pnat_space_off,(long long)pnat_cursor_calls,(long long)pnat_windows,
        (long long)pnat_dispatched[0],(long long)pnat_dispatched[1],(long long)pnat_dispatched[2],(long long)pnat_refused,
        (long long)pnat_refusal_dropped,(long long)pnat_wrapped,(long long)pnat_req_calls,(long long)pnat_req_records,
        (long long)pnat_ui_passes,(long long)pnat_passes,(long long)pnat_async_failures,pnat_stop_in_menu,pnat_restored);
    if(n>0 && (size_t)n<capacity && pnat_mode==PNAT_MODE_STOCK) {
        int more=pstk_status(out+n,capacity-(size_t)n);
        if(!more) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
