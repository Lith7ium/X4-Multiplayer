/* Virtual players, DEFAULT OFF (native_gate_virtual_players): step 1 of
 * reports/LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029.txt. Every client is to run X4's
 * own per-frame player control path once per slot k, with the player-side state
 * of slot k swapped in. This step defines that state (the "player block"), the
 * swap primitives the later steps use, and a read-only live mode:
 *
 *   'observe'  once per admitted frame, in path_release on the MoveWorker before
 *              the pathing pass is released (no pathing worker runs; slots 3/4/6
 *              of the previous frame are complete): gather the canonical
 *              player's block (the Universe player, its PlayerController via
 *              9fc550), hash it per class, count per-region changes, prove the
 *              CONFIG regions constant and the reentry flag clear, diff the guard
 *              pages around the block for writes outside the declared regions,
 *              and once take a census of the InputMapping multimap (every
 *              registered action/state/range handler, classified). No game write,
 *              no hook, no game call except the pinned getter 9fc550.
 *   'construct' (step 3 spike, stop reason 160): 'observe' plus one second
 *              U::Player built by X4's own factory at a given admitted tick,
 *              unparented and unseated, every side effect on the canonical
 *              player put back (section "Step 3 spike" below).
 *
 * Block classes (build 9.00 RVAs; evidence in the report):
 *   SIM     per slot simulation state of the stock path: the processed channels,
 *           throttle and vfunc33 state of the input bank (d84c20 resets
 *           0x2f4b720..0x2f4b998 from d85100 except 0x2f4b7e0/7e4), the
 *           controller environment 0x6b661d0.. (d8dfe0), coefficients
 *           0x31be630.. (d924d0) and bind state 0x31be770.., triggers
 *           0x38f0728.. (7e8ef0), the player's assist/activity (+0x6aac..+0x6ab7,
 *           d9bf70/9fd120/710ed0), the soft target (+0x6dd0, 9fcd70) and the
 *           shoot flag (+0x6e20). A scope swaps exactly these.
 *           (The camera pick block +0x6c00..+0x6d34, aim and picked component
 *           included, is written every frame by 717ed0 from the local camera:
 *           DEVICE, live test A.)
 *   DEVICE  device stage of d95180 (d952fa..d95bdc) and of the bank handlers:
 *           raw mouse/joystick/key state of the capturing client. Never swapped;
 *           the later record carries what the device stage produced.
 *   LOCAL   this client's UI share of the controls (0x2f4b7e0/7e4, 0x2f4b829).
 *   OPTION  user options that change simulation (autoroll, stop ship in menus,
 *           aim assist, collision avoidance assist): per machine today.
 *   SHARED  MD input locks (1132900): world state, identical for every slot.
 *   CONFIG  read-only tables (d94700 coefficients, 3d19b10.. flight settings).
 * Identity (observed, never swapped): Universe player, its PlayerController
 * (vtable 0x2c39488), the controller's component (+0x28) and bound flag (+0xe0),
 * the d95180 reentry flag 0x38f4db4 (must be 0 at every scope boundary). */
#define VPL_UNIVERSE 0x3d198f8u
#define VPL_PLAYER_NONE 0x3d196d8u
#define VPL_GAME 0x3d196f0u                 /* 9fc550's argument: +0x208 = the Universe pointer */
#define VPL_CONTROLLER_RVA 0x9fc550u
#define VPL_CONTROLLER_VTABLE 0x2c39488u
#define VPL_INPUT 0x6cf1560u
#define VPL_MAPPING 0x63e0u                 /* InputMapping multimaps in the input object (d898c0) */
#define VPL_REENTRY 0x38f4db4u
#define VPL_SLOTS_MAX 4u
#define VPL_IMAGE_SPAN 0x6fa9000u
enum {VPL_SIM=1,VPL_DEVICE=2,VPL_LOCAL=3,VPL_OPTION=4,VPL_SHARED=5,VPL_CONFIG=6,VPL_CLASSES=7};
enum {VPL_BASE_IMAGE=0,VPL_BASE_PLAYER=1};
typedef struct {unsigned char base,cls;unsigned offset,size;const char *name;unsigned char pointer;} vpl_region;
/* pointer = 1: the region is one component pointer (process address); hashes use its
 * portable identity (world identities), the swap moves the pointer itself. */
static const vpl_region vpl_regions[]={
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b720,0x30,"bank_device_rot",0},      /* mouse 720..728, key ramps 72c..734, analog 738..740 (d88a10), axes 744..74c */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b750,0x20,"bank_rot_throttle",0},       /* processed yaw/pitch/roll 750..758, setpoint 75c, desired 760, smoothed 764, min/max 768/76c */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b770,0x14,"bank_device_throttle",0}, /* axis last 770, 774, hold 778, ramp base 780 */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b784,0x1,"bank_bound",0},               /* d84c20 sets, d95180 consumes */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b785,0x1,"bank_invert_throttle",0},  /* state 0x5a */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b786,0x1,"bank_match_speed",0},         /* d9cd00 / d97b50, read by vfunc33 */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b787,0x1,"bank_steering_mode",0},
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b788,0xc,"bank_linear",0},              /* processed x/y/z 788..790 */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b794,0x3c,"bank_device_linear",0},   /* analog 794..79c, key ramps 7a0..7a8 */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b7d0,0x10,"bank_stop_autopilot",0},     /* full stop 7d0/7d1, autopilot target 7d8 */
    {VPL_BASE_IMAGE,VPL_LOCAL,0x2f4b7e0,0x8,"bank_controls_share",0},    /* enabled 7e0, deactivation count 7e4 (menus, map) */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x2f4b7e8,0x40,"bank_device_keys",0},     /* key flags 7e8..7fe, axis timers 800..820 */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b828,0x1,"bank_flight_control",0},
    {VPL_BASE_IMAGE,VPL_LOCAL,0x2f4b829,0x1,"bank_contexts",0},          /* PlayerController input contexts requested */
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b82a,0x16e,"bank_controller_state",0},  /* highway brake 82a, 82c, vfunc33 state 830..994 */
    {VPL_BASE_IMAGE,VPL_OPTION,0x2f4b9be,0x2,"option_autoroll_stopship",0},
    {VPL_BASE_IMAGE,VPL_OPTION,0x2f4b9c0,0x8,"option_aim_assist",0},
    {VPL_BASE_IMAGE,VPL_OPTION,0x2f4b9cc,0x1,"option_collision_assist",0},
    {VPL_BASE_IMAGE,VPL_SIM,0x2f4b6d8,0x1,"fire_at_cursor",0},           /* 7e8ef0 */
    {VPL_BASE_IMAGE,VPL_SIM,0x31be630,0x54,"coefficients",0},            /* d924d0 writes 630..670, vfunc33 reads 67c/680 */
    {VPL_BASE_IMAGE,VPL_CONFIG,0x31be6b8,0x61,"coefficient_tables",0},   /* read by d94700 only, no writer; 0x31be718 a bool */
    {VPL_BASE_IMAGE,VPL_CONFIG,0x31be720,0x4c,"coefficient_tables_2",0}, /* 0x31be719..71b padding (vp-D1), 0x31be71c a neighbour (vp-G2) */
    {VPL_BASE_IMAGE,VPL_SIM,0x31be770,0xc,"bind_state",0},               /* d84c20 / d8ca10 / d8ddb0 */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x31c1fd8,0x8,"hold_timer",0},
    {VPL_BASE_IMAGE,VPL_SIM,0x38f0728,0x4,"triggers",0},                 /* primary 728/729, secondary 72a/72b */
    {VPL_BASE_IMAGE,VPL_SHARED,0x38f072c,0x2,"fire_locks",0},            /* MD input lock */
    {VPL_BASE_IMAGE,VPL_DEVICE,0x38f4db5,0x2,"steering_saved",0},        /* d88710 mouse steering save */
    {VPL_BASE_IMAGE,VPL_SHARED,0x38f4dc8,0x8,"input_locks",0},           /* MD input lock */
    {VPL_BASE_IMAGE,VPL_CONFIG,0x3d19b10,0x30,"flight_settings",0},      /* 9e7dc0 at load */
    {VPL_BASE_IMAGE,VPL_SIM,0x6b661d0,0x18,"environment",0},             /* highway flags 6b661d0..d2, floats 6b661d4..e3 (d8dfe0) */
    {VPL_BASE_IMAGE,VPL_SIM,0x6b661e8,0x8,"environment_zone",1},       /* the zone pointer d95180 compares (8d2b80, d84730) */
    {VPL_BASE_PLAYER,VPL_SIM,0x6aac,0xc,"player_assist_activity",0},     /* assist 6aac, suspended 6aad, override 6aae, axis 6ab0, activity 6ab4 */
    {VPL_BASE_PLAYER,VPL_DEVICE,0x6c00,0x138,"player_camera_pick",0},   /* 717ed0 every frame from the camera: pick 6c00..6c10, aim 6c60..6c80, picked 6c88, last 6ce0..6d34 */
    {VPL_BASE_PLAYER,VPL_SIM,0x6dd0,0x8,"player_target",0},              /* soft target ID (9fcd70; match speed, shoot controller ctor) */
    {VPL_BASE_PLAYER,VPL_SIM,0x6e20,0x1,"player_shoot_flag",0}};
#define VPL_REGION_COUNT (sizeof(vpl_regions)/sizeof(*vpl_regions))
#define VPL_BLOCK_MAX 0x600u
static const char *const vpl_class_names[VPL_CLASSES]={"none","sim","device","local","option","shared","config"};
/* Guard pages: every page holding a region. Undeclared bytes there that change
 * are reported (RVA ranges), never interpreted; the reentry flag counts as declared. */
#define VPL_GUARD_COUNT 7u
static const unsigned vpl_guard_pages[VPL_GUARD_COUNT]={0x2f4b000,0x31be000,0x31c1000,0x38f0000,0x38f4000,0x3d19000,0x6b66000};
/* Neighbours: engine globals on the guard pages that belong to other subsystems
 * (live test A, vp-A1: they changed on both clients; readers/writers named from
 * the image). Not player state: masked out of the undeclared diff, their changes
 * counted per entry (passes in which any byte changed; VPLAYER_NEIGHBOURS). */
typedef struct {unsigned offset,size;const char *name;} vpl_neighbour;
static const vpl_neighbour vpl_neighbours[]={
    {0x2f4b6c8,0x10,"zone_move"},               /* U::Zone::Move, 8419b0 */
    {0x2f4b6e8,0x8,"fn_8d27f0"},                /* 8d2690 / 8d27f0 */
    {0x2f4b6f4,0x4,"pathing_buffer_index"},     /* g_PathingStateBufferIndex2: frame double buffer, ~50 pathing/nav readers */
    {0x2f4b718,0x8,"view_cache_index"},         /* g_ViewCacheWriteIndex(+Alt): UniverseFrameUpdate, PublishComponentViewCache */
    {0x2f4b99c,0x4,"gravidar"},                 /* AI::Gravidar::Run */
    {0x2f4bb18,0x8,"particle_emitters"},        /* frkPEmitterEmit / frkPEmitterRestart */
    {0x31be71c,0x4,"dword_31be71c"},           /* no RIP-relative reference in the image; vp-G2: set on one client after the controls returned */
    {0x31befd0,0x8,"fn_fa7600"},
    {0x31c1fa0,0x20,"production_trade"},        /* ProductionModule::ComputeEfficiency 1fa0, Player::AddTradeSubscription 1fb0, 6dd921 1fb8 */
    {0x38f06d8,0x10,"fn_db3b0"},
    {0x38f0720,0x4,"scene"},                    /* Scene::vfunc915, 77b7de */
    {0x38f0ae8,0x8,"game_load_save"},           /* GameClass NewGame / Load / Save, 6ea780 */
    {0x38f0bb8,0x4008,"event_listener_stack"}}; /* g_EventDeliveryListenerStackDepth + 0x800 entries (99b450, 99c48e) */
#define VPL_NEIGHBOUR_COUNT (sizeof(vpl_neighbours)/sizeof(*vpl_neighbours))

/* InputMapping census: every registration is static except the dynamic ones
 * (AddonManager, CommAction, InfoAction, BasicScanAction, TeleportLuaAction). */
enum {VPL_H_UNKNOWN=0,VPL_H_FLIGHT,VPL_H_PLAYER,VPL_H_WEAPON,VPL_H_HUD,VPL_H_WALK,VPL_H_CLIENT,VPL_H_UI,VPL_H_ADDON,VPL_H_KINDS};
static const char *const vpl_handler_names[VPL_H_KINDS]={"unknown","flight","player","weapon","hud","walk","client","ui","addon"};
static const struct {unsigned rva;unsigned char kind;} vpl_handlers[]={
    {0xd86c80,VPL_H_FLIGHT},{0xd870b0,VPL_H_FLIGHT},{0xd87f70,VPL_H_FLIGHT},      /* d898c0: PlayerController flight */
    {0x71bac0,VPL_H_PLAYER},{0x71c640,VPL_H_PLAYER},{0x71ca10,VPL_H_PLAYER},      /* 71b6c0: player entity */
    {0x7e8ef0,VPL_H_WEAPON},{0x7eb070,VPL_H_WEAPON},{0x7e90b0,VPL_H_WEAPON},{0x7eb160,VPL_H_WEAPON}, /* 9ed1f0: triggers */
    {0xd7d3b0,VPL_H_WALK},{0xd7d5c0,VPL_H_WALK},{0xd7d690,VPL_H_WALK},            /* 9ed1f0: first person */
    {0xaf1db0,VPL_H_HUD},{0xaf3e70,VPL_H_HUD},{0xaf3ee0,VPL_H_HUD},               /* af12b0: weapon groups, dialogs */
    {0x122c070,VPL_H_CLIENT},{0x122d740,VPL_H_CLIENT},{0x122d970,VPL_H_CLIENT},   /* GameClient::Init: camera, options */
    {0x3035a0,VPL_H_UI},{0x303a50,VPL_H_UI},{0x303cf0,VPL_H_UI},                  /* 304f10: widgets */
    {0x8c7a70,VPL_H_UI},{0xe1c280,VPL_H_UI},{0xe1c710,VPL_H_UI},{0x10f2180,VPL_H_UI}, /* menus, map, cinematic */
    {0x1177440,VPL_H_UI},{0x8c5e10,VPL_H_UI},{0xac2c60,VPL_H_UI},                 /* dialog, HUD toggle, start */
    {0xad1e00,VPL_H_ADDON},{0xad1e70,VPL_H_ADDON}};
#define VPL_HANDLER_COUNT (sizeof(vpl_handlers)/sizeof(*vpl_handlers))
static unsigned vpl_handler_kind(uintptr_t handler,const unsigned char *base) {
    if(handler<(uintptr_t)base || handler-(uintptr_t)base>=VPL_IMAGE_SPAN) return VPL_H_UNKNOWN;
    unsigned rva=(unsigned)(handler-(uintptr_t)base);
    for(unsigned i=0;i<VPL_HANDLER_COUNT;i++) if(vpl_handlers[i].rva==rva) return vpl_handlers[i].kind;
    return VPL_H_UNKNOWN;
}
/* Whole-function pins (build 9.00): the getter called live, the layout evidence
 * of every region and of the multimap walk, and every static registration
 * function of the handler table (their lea displacements fix the handler RVAs). */
static const struct {unsigned rva,size;const char *sha;} vpl_pin_table[]={
    {0x9fc550,277,"fca3a29a2c0ca40fafa27dac44da67e9b8bd0aa52b3d812c72b72f4ed212ca67"},   /* occupied controller */
    {0xd84c20,1244,"be54c529ecbce01377450ef1c583fe747c80399bac1b226fd9d58ce41f973a17"},  /* bank reset 0x278 bytes */
    {0xd95180,7221,"c55c35cea23632e2ab86e60c68f3222b7cab0160a788e662d67cde1309e63d33"},  /* PlayerController update */
    {0xd97e80,3926,"2b297f805f6795a221c5c4ab139615e396380a46d000085902f41cca2aa50724"},  /* vfunc33 */
    {0xd924d0,3221,"63cf643f55130eb8c05098e93155fd88b2d42a36a1cbba73c96ce6fd27343403"},  /* coefficients */
    {0xd8dfe0,1022,"c860534e68204c9e8309d38549afbd3a00c6372f8c564d96ef88893497f57e9c"},  /* environment */
    {0xd8ddb0,551,"f8db7671820371a5fc3094ce34b37ea0b073ee530d45019f3a62c3e1cb725a2b"},   /* throttle range bind */
    {0x7e8ef0,448,"605405f7391c0afb49e071e63cedce457bc8e3ccf8076dde1d70b15fc9cc4a3c"},   /* triggers */
    {0x7e8bc0,669,"5819f0a66ffd2d81124b022217b9eee0678448fb27024a8a49d8d4c149391351"},   /* shoot decision */
    {0x7e8650,571,"14b9911767da0d3f65fa068c9c34dcd2d6b3aff872063498de4002fab8bb74fb"},   /* aim, player target */
    {0xd9bf70,163,"b8c0b8b83decfbed296f7de7dc147d4d8e81e16a9ee811f4be000a88cd36b4c8"},   /* assist fields */
    {0x9fd120,521,"3a686a762638a9d022d99bd44ab484ea5b225701adddf72b6cf40bf0d938df76"},   /* start activity */
    {0x710ed0,1268,"9ec1a432995414451cee06a8e063412551a756680fb20e3e6a32097dfa90c4c9"},  /* activity writer */
    {0x1255cd0,249,"dd68eff14ac7f102e961be6d0a2664b7919e333de8477cf6e83279e45199db5f"},  /* action dispatcher */
    {0x1255dd0,259,"21c54fb4bc0b12b0da42f5ba984f0c6f952621748e3d9a2491671dd90c4d2c0b"},  /* state dispatcher */
    {0x1255ee0,257,"aed9220ab8c1fd3b5ab649b587d579661a4f879e40b9e68f57877ac56c4c17c3"},  /* range dispatcher */
    {0xd898c0,1115,"53766ee9ce21098b21408c31564e9f7269968034675e837994b3c6bf758f580c"},  /* flight registrations, input+0x63e0 */
    {0x71b6c0,1023,"0e9acd0dbf23c0708ade6da15f9a05f5f3a9e37fdbac8cde7e8ec1c10eac6998"},  /* player entity registrations */
    {0x9ed1f0,957,"ff9bfcb2989d3a16c2ca948a48f39cb212981efcfea323ebe109fab205bb5c85"},   /* weapon / first person registrations */
    {0xaf12b0,1270,"8275feef85539718482ecc17c6a3cf81954c8e1f011e5f11d434e1f929997745"},  /* HUD registrations */
    {0x1223ce0,11423,"694bb7ae85184641ef3e042b0be6ffa1388298f43535bbb2aede13ef524cd8e7"},/* GameClient::Init registrations */
    {0x304f10,1210,"d82e0e52c5d844e57608e0ca7fd3f966d3ac75a1b7121546d37764b5882604b5"},  /* widget registrations */
    {0x8d3090,2027,"e15435220b4465d5dc5e761042f2117d72f68ca052c13c95527d598667ab31db"},  /* menu registrations */
    {0xe1c020,599,"ac7e9bd1b966f76274fd393bffbcd74a50711f2a4c4c3066a5a87c4d9526e7b8"},   /* map registrations */
    {0x10ea140,886,"9b39fc8641f8de54fc29873524d453db983f89b9df8cff42d68ab1ecb86f2eb3"},  /* cinematic registrations */
    {0x1177590,662,"bac446e318003348144648b08872853860e83b71d0ff8bd95285d86d72a384d0"},  /* dialog registrations */
    {0x121c390,1835,"57d3ea4b329aeb8f68cf0b069f632b5ba93ebbab781ed99be08ec85e0be071b9"}, /* GameClient ctor registrations */
    {0xac2aa0,203,"db9bb1424ed84be849f4f6a00a9e324dee467a444173c6de3ba7bbd9ea4a6e24"},   /* start registration */
    {0xad5a10,1546,"84817918dd0989286851722ad2657e3ae6d0dc0e214a276b272121ca69c0cb61"}}; /* AddonManager bindings */
#define VPL_PIN_COUNT (sizeof(vpl_pin_table)/sizeof(*vpl_pin_table))

typedef void *(*vpl_controller_fn)(void *);
typedef struct {
    unsigned char *base;               /* image (production) or a test region */
    vpl_controller_fn controller;       /* 9fc550 */
} vpl_env;
static vpl_env vpl_e;
static int vpl_armed,vpl_pins_checked,vpl_census_done;
static const char *vpl_stage="off";
static uint64_t vpl_passes,vpl_missing,vpl_identity_changes,vpl_config_changes,vpl_reentry_seen,vpl_unbound,vpl_read_failures,vpl_last_tick;
static uint64_t vpl_class_changes[VPL_CLASSES],vpl_class_hash[VPL_CLASSES];
static uint64_t vpl_region_changes[VPL_REGION_COUNT],vpl_region_first[VPL_REGION_COUNT],vpl_region_last[VPL_REGION_COUNT];
static uint64_t vpl_changed_mask;      /* regions that changed in the last pass */
static unsigned char vpl_now[VPL_BLOCK_MAX],vpl_prev[VPL_BLOCK_MAX];
static int vpl_have_prev;
static void *vpl_seen_player,*vpl_seen_controller,*vpl_seen_component;
static unsigned char vpl_guard_prev[VPL_GUARD_COUNT][4096],vpl_guard_declared[VPL_GUARD_COUNT][4096],vpl_guard_ever[VPL_GUARD_COUNT][4096/8];
static int vpl_guard_have;
static uint64_t vpl_undeclared_changes,vpl_neighbour_changes[VPL_NEIGHBOUR_COUNT];
static unsigned vpl_census[3][VPL_H_KINDS],vpl_census_nodes;
#define VPL_CENSUS_LIST 48u
static struct {unsigned char kind,cls;unsigned short id;unsigned rva;} vpl_census_list[VPL_CENSUS_LIST];
static unsigned vpl_census_listed,vpl_census_dropped,vpl_census_failures;

static int vpl_block_bytes(unsigned cls_mask) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) if(cls_mask>>vpl_regions[i].cls&1u) n+=vpl_regions[i].size;
    return (int)n;
}
static unsigned char *vpl_region_at(const vpl_region *r,unsigned char *base,unsigned char *player) {
    return r->base==VPL_BASE_IMAGE?base+r->offset:player?player+r->offset:NULL;
}
/* Copy the regions of the selected classes, in table order, live -> out. */
static int vpl_gather(unsigned char *base,unsigned char *player,unsigned cls_mask,unsigned char *out,size_t capacity) {
    size_t n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(!(cls_mask>>r->cls&1u)) continue;
        unsigned char *at=vpl_region_at(r,base,player);
        if(!at || n+r->size>capacity || !sample_read(at,out+n,r->size)) return -1;
        n+=r->size;
    }
    return (int)n;
}
/* live <-> store in place, byte by byte (no allocation, no temporary block). */
static int vpl_exchange(unsigned char *base,unsigned char *player,unsigned cls_mask,unsigned char *store,size_t length) {
    size_t n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(!(cls_mask>>r->cls&1u)) continue;
        unsigned char *at=vpl_region_at(r,base,player);
        if(!at || n+r->size>length) return -1;
        for(unsigned j=0;j<r->size;j++) {unsigned char t=at[j];at[j]=store[n+j];store[n+j]=t;}
        n+=r->size;
    }
    return n==length?(int)n:-1;
}
static uint64_t vpl_fnv(const unsigned char *data,size_t n,uint64_t h) {
    for(size_t i=0;i<n;i++) {h^=data[i];h*=0x100000001b3ULL;}
    return h;
}

/* Scopes (step 2 onwards; offline-tested now, not reachable from Lua in step 1,
 * hence the unused attributes): while slot k's scope is open, the live SIM regions hold
 * slot k's state and slot k's store holds the canonical (slot 1) state. Only the
 * MoveWorker opens a scope, never nested, never inside d95180 (reentry flag 0),
 * and leaving must give back exactly the canonical bytes it took. */
typedef struct {unsigned char data[VPL_BLOCK_MAX];unsigned length;int valid;} vpl_store;
static vpl_store vpl_stores[VPL_SLOTS_MAX+1];
static unsigned vpl_scope_slot;            /* 0: none open */
static DWORD vpl_scope_thread;
static uint64_t vpl_scope_hash;
static unsigned char *vpl_scope_player;
static uint64_t vpl_scope_opened,vpl_scope_refused;
static DWORD vpl_thread_now(void) {return GetCurrentThreadId();}
static DWORD (*vpl_thread_fn)(void)=vpl_thread_now;
static int vpl_reentry_clear(unsigned char *base) {unsigned char r=1;return sample_read(base+VPL_REENTRY,&r,1) && r==0;}
/* A slot's store starts as a copy of the live SIM block (the stock reset path of
 * a newly bound controller then rewrites it inside its own scope). */
__attribute__((unused)) static int vpl_store_init(unsigned slot,unsigned char *base,unsigned char *player) {
    if(slot<2 || slot>VPL_SLOTS_MAX || vpl_scope_slot) return 0;
    int n=vpl_gather(base,player,1u<<VPL_SIM,vpl_stores[slot].data,sizeof(vpl_stores[slot].data));
    if(n<0) return 0;
    vpl_stores[slot].length=(unsigned)n;vpl_stores[slot].valid=1;return 1;
}
static uint64_t vpl_sim_hash(unsigned char *base,unsigned char *player,int *ok) {
    unsigned char block[VPL_BLOCK_MAX];int n=vpl_gather(base,player,1u<<VPL_SIM,block,sizeof(block));
    *ok=n>=0;return n>=0?vpl_fnv(block,(size_t)n,0xcbf29ce484222325ULL):0;
}
__attribute__((unused)) static int vpl_enter(unsigned slot,unsigned char *base,unsigned char *player,DWORD worker) {
    int ok=0;
    if(vpl_scope_slot || slot<2 || slot>VPL_SLOTS_MAX || !vpl_stores[slot].valid || !player || vpl_thread_fn()!=worker ||
       !vpl_reentry_clear(base)) {++vpl_scope_refused;return 0;}
    uint64_t hash=vpl_sim_hash(base,player,&ok);
    if(!ok || vpl_exchange(base,player,1u<<VPL_SIM,vpl_stores[slot].data,vpl_stores[slot].length)<0) {++vpl_scope_refused;return 0;}
    vpl_scope_slot=slot;vpl_scope_thread=worker;vpl_scope_hash=hash;vpl_scope_player=player;++vpl_scope_opened;return 1;
}
__attribute__((unused)) static int vpl_leave(unsigned char *base) {
    int ok=0;unsigned slot=vpl_scope_slot;
    if(!slot || vpl_thread_fn()!=vpl_scope_thread || !vpl_reentry_clear(base)) {++vpl_scope_refused;return 0;}
    if(vpl_exchange(base,vpl_scope_player,1u<<VPL_SIM,vpl_stores[slot].data,vpl_stores[slot].length)<0) {++vpl_scope_refused;return 0;}
    vpl_scope_slot=0;
    uint64_t hash=vpl_sim_hash(base,vpl_scope_player,&ok);vpl_scope_player=NULL;
    if(!ok || hash!=vpl_scope_hash) {++vpl_scope_refused;return 0;} /* canonical bytes changed while stored: fail */
    return 1;
}

/* Census of the three multimaps (node: +0 parent, +8 left, +0x10 right, +0x20
 * key, +0x28 handler; the map object's +8 is the root, as 1255cd0 walks it). */
static int vpl_census_tree(unsigned char *header,unsigned kind,const unsigned char *base) {
    unsigned char *x=NULL,*next=NULL;unsigned steps=0;
    if(!sample_read(header+8,&x,8)) return 0;
    if(!x) return 1;
    for(;;) {if(!sample_read(x+8,&next,8)) return 0;if(!next) break;x=next;if(++steps>4096) return 0;}
    while(x && x!=header) {
        int key=0;void *handler=NULL;unsigned char *right=NULL;
        if(++steps>8192 || !sample_read(x+0x20,&key,4) || !sample_read(x+0x28,&handler,8) || !sample_read(x+0x10,&right,8)) return 0;
        unsigned cls=vpl_handler_kind((uintptr_t)handler,base);
        ++vpl_census[kind][cls];++vpl_census_nodes;
        if(cls==VPL_H_UNKNOWN || cls==VPL_H_ADDON) {
            if(vpl_census_listed<VPL_CENSUS_LIST) {
                uintptr_t h=(uintptr_t)handler,b=(uintptr_t)base;
                vpl_census_list[vpl_census_listed].kind=(unsigned char)kind;vpl_census_list[vpl_census_listed].cls=(unsigned char)cls;
                vpl_census_list[vpl_census_listed].id=(unsigned short)key;
                vpl_census_list[vpl_census_listed].rva=h>=b && h-b<VPL_IMAGE_SPAN?(unsigned)(h-b):0xffffffffu;++vpl_census_listed;
            } else ++vpl_census_dropped;
        }
        if(right) {x=right;for(;;) {if(!sample_read(x+8,&next,8)) return 0;if(!next) break;x=next;if(++steps>8192) return 0;}}
        else {
            unsigned char *p=NULL,*c=x,*pl=NULL;
            for(;;) {
                if(!sample_read(c,&p,8) || !p || !sample_read(p+8,&pl,8)) return 0;
                if(pl==c) break;
                c=p;if(++steps>8192) return 0;
            }
            x=p;
        }
    }
    return 1;
}
static int vpl_census_take(unsigned char *base) {
    unsigned char *input=NULL;
    memset(vpl_census,0,sizeof(vpl_census));vpl_census_nodes=vpl_census_listed=vpl_census_dropped=0;
    if(!sample_read(base+VPL_INPUT,&input,8) || !input) return 0;
    for(unsigned k=0;k<3;k++) if(!vpl_census_tree(input+VPL_MAPPING+0x40*k,k,base)) return 0;
    return 1;
}
static int vpl_pins(void) {
    if(!vpl_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<VPL_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+vpl_pin_table[i].rva,vpl_pin_table[i].size,vpl_pin_table[i].sha))) pin_failed_rva=vpl_pin_table[i].rva;
        vpl_pins_checked=ok?1:-1;
    }
    return vpl_pins_checked==1;
}
static void vpl_guard_setup(void) {
    memset(vpl_guard_declared,0,sizeof(vpl_guard_declared));
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++)
        for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
            const vpl_region *r=&vpl_regions[i];
            if(r->base!=VPL_BASE_IMAGE) continue;
            for(unsigned j=0;j<r->size;j++) {
                unsigned a=r->offset+j;
                if(a>=vpl_guard_pages[g] && a<vpl_guard_pages[g]+4096u) vpl_guard_declared[g][a-vpl_guard_pages[g]]=1;
            }
        }
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++)
        if(VPL_REENTRY>=vpl_guard_pages[g] && VPL_REENTRY<vpl_guard_pages[g]+4096u) vpl_guard_declared[g][VPL_REENTRY-vpl_guard_pages[g]]=1;
    /* Neighbour k owns its guard bytes as 2+k (never a declared byte: the tables are disjoint). */
    for(unsigned k=0;k<VPL_NEIGHBOUR_COUNT;k++)
        for(unsigned g=0;g<VPL_GUARD_COUNT;g++) {
            unsigned lo=vpl_neighbours[k].offset,hi=lo+vpl_neighbours[k].size,page=vpl_guard_pages[g];
            for(unsigned a=lo>page?lo:page;a<hi && a<page+4096u;a++) if(!vpl_guard_declared[g][a-page]) vpl_guard_declared[g][a-page]=(unsigned char)(2u+k);
        }
    vpl_guard_have=0;memset(vpl_guard_ever,0,sizeof(vpl_guard_ever));vpl_undeclared_changes=0;
    memset(vpl_neighbour_changes,0,sizeof(vpl_neighbour_changes));
}
static int vpl_arm_using(unsigned char *base,vpl_controller_fn controller) {
    if(vpl_armed) return vpl_e.base==base;
    if(!base || !controller || vpl_block_bytes(0xffu)>(int)VPL_BLOCK_MAX) return 0;
    vpl_e.base=base;vpl_e.controller=controller;vpl_guard_setup();
    vpl_armed=1;vpl_stage="armed";return 1;
}
/* gate.virtual_players('observe'): before the first admitted tick, MoveWorker,
 * pins intact. An identical rerun after a loading-UI reload succeeds. */
static int vpl_arm(void) {
    if(vpl_armed) return vpl_e.base==image;
    if(stats.issued || GetCurrentThreadId()!=ui_thread || !image || !vpl_pins()) return 0;
    return vpl_arm_using(image,(vpl_controller_fn)(image+VPL_CONTROLLER_RVA));
}
static unsigned char *vpl_player_of(unsigned char *base) {
    unsigned char *universe=NULL,*player=NULL;
    if(!sample_read(base+VPL_UNIVERSE,&universe,8)) return NULL;
    if(!sample_read(universe?universe+0x230:base+VPL_PLAYER_NONE,&player,8)) return NULL;
    return player;
}
static void vpl_guard_pass(unsigned char *base) {
    unsigned char moved[VPL_NEIGHBOUR_COUNT]={0};
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) {
        unsigned char page[4096];
        if(!sample_read(base+vpl_guard_pages[g],page,sizeof(page))) {++vpl_read_failures;continue;}
        if(vpl_guard_have)
            for(unsigned i=0;i<4096u;i++) {
                unsigned char owner=vpl_guard_declared[g][i];
                if(owner==1 || page[i]==vpl_guard_prev[g][i]) continue;
                if(owner>=2) {moved[owner-2]=1;continue;}
                if(!(vpl_guard_ever[g][i>>3]>>(i&7)&1u)) vpl_guard_ever[g][i>>3]|=(unsigned char)(1u<<(i&7));
                ++vpl_undeclared_changes;
            }
        memcpy(vpl_guard_prev[g],page,sizeof(page));
    }
    for(unsigned k=0;k<VPL_NEIGHBOUR_COUNT;k++) vpl_neighbour_changes[k]+=moved[k];
    vpl_guard_have=1;
}
/* One region's bytes into a hash: a pointer region by its component's portable ID
 * (0 for none, 1 without one), so clients compare what it points to, not where. */
static uint64_t vpl_region_hash(const vpl_region *r,const unsigned char *bytes,uint64_t h) {
    if(!r->pointer) return vpl_fnv(bytes,r->size,h);
    unsigned char *object=NULL;uint64_t runtime=0,portable=0;memcpy(&object,bytes,8);
    if(object) portable=read_local(object+8,&runtime,8) && runtime?world_identity_portable(runtime):0;
    if(object && !portable) portable=1;
    return vpl_fnv((const unsigned char*)&portable,8,h);
}
/* ---- Step 3 spike, sub-mode 'construct' (DEFAULT OFF, stop reason 160) ----
 * gate.virtual_players('construct', tick): 'observe' plus, at the first pass at
 * or after the admitted tick (path_release, the frame thread that runs MD, AI
 * scripts and every other simulation creation; the pathing pass joined), one
 * second U::Player built by X4's own factory, unparented and unseated:
 *   ComponentFactory::Create 8e0250(universe, &out, parent 0, connection 0, 0, 0,
 *     macro of the Universe player (+0x60; class +0x44 must be 0x4c), 0,
 *     activate 1, 0, 0, "", "", "", 0, mode 0, post-init 1)
 * exactly as Universe::CreateComponent (338660) calls it for runtime creations
 * (338ae7 call setup, mode 0 as Scripts::CreateObjectAction and 908580 pass).
 * Case 0x4c allocates 0x8a7e0 bytes (879bc0) and runs the Player constructor
 * 6fa0b0 -> 6fa8e0, whose side effects are undone or accounted here:
 *   - Universe+0x230 = the new player, the old player's reference dropped (it
 *     would be destroyed if the Universe held the only one: refused below 2):
 *     put back, the old reference restored, the new player's Universe reference
 *     dropped (the factory's out reference stays ours);
 *   - two Random::FloatState draws from the calling thread's TLS word
 *     (slot0+0x318): the word is put back, so the simulation stream is unchanged;
 *   - *(0x6cf1580)+8 (a flag from the new player's macro data) and the 0x38f0719
 *     context flag of 730df0: put back;
 *   - three input context requests (contexts 9, 10, 6, requester 6) and the
 *     0x46 one of 730df0: already present, refused by InputMapping with a log
 *     line, nothing changes;
 *   - the UniverseID (AssignNewID in Component::Component), the owner change to
 *     the ownerless faction (554a70: ChangedOwner events on the unparented
 *     player), Player::vfunc578 (HelmetMovementController, 8e8450): identical on
 *     every client at the same point of the same frame; three
 *     RenderStateRequester objects: presentation, per client [I].
 * The canonical player block (every region, the Universe player's fields) must
 * be byte-identical before and after; guard-page bytes the call changed are
 * counted. Afterwards every pass checks the singleton is still the canonical
 * player and the new player intact and unparented. The new player is kept (one
 * reference) for the rest of the process. Seating (control position, a
 * PlayerController for slot 2's ship) is the next spike step: it needs the
 * scope of step 4 (716670 and 70e630/70dc50 assume "the" player). */
#define VPS_STOP_REASON 160u
#define VPS_FACTORY 0x8e0250u          /* U::ComponentFactory::Create */
#define VPS_PLAYER_VTABLE 0x2b329a8u   /* U::Player, primary vtable */
#define VPS_PLAYER_CLASS 0x4c
#define VPS_EMPTY_STRING 0x31c0f60u    /* the engine's empty std::string (size at +0x10) */
#define VPS_RENDER_FLAG 0x6cf1580u     /* pointer; +8 written by 6fa8e0 */
#define VPS_CONTEXT_FLAG 0x38f0719u    /* 730df0: context 0x46 requested */
#define VPS_SINGLETON 0x230u           /* Universe player */
typedef void **(*vps_create_fn)(void *,void **,void *,void *,void *,void *,void *,void *,uint64_t,void *,uint64_t,
    const void *,const void *,const void *,void *,uint64_t,uint64_t);
static const struct {unsigned rva,size;const char *sha;} vps_pin_table[]={
    {0x8e0250,10656,"9b7b76642404e75a04ada6f4178e68447c6e7b60f7ed4078428db1ad0d1804c5"},  /* ComponentFactory::Create */
    {0x6fa0b0,2085,"f93982a1ae6eb03f500a5e31826664c8754a9b7b4a14373435cd2cdd897234b0"},   /* Player constructor */
    {0x6fa8e0,2377,"a669ec285e10150457eade7c50c09708a5f0abf93731315416a35624059ec63e"},   /* Player init: singleton, RNG, contexts */
    {0x879bc0,27,"931ef573ad163e3c1f84eeed139b08cc67170a82f742e78496ee2a7a948249b8"},     /* allocate 0x8a7e0 */
    {0x6fb230,401,"32fff926c235b9f2d3de1e5f3c5d456ea0883970fd3236f7e8aee6691771f321"},    /* Player::vfunc578 (post-init) */
    {0x3d2400,352,"ed89094fc31b8fce5953daded20b73001c5af55c978acd85991917a9d8000d8a"},    /* Component::Component */
    {0x551d70,583,"61c2e4f9dece1f0ff2445d4b0d25efdc4ef6d36b6fd16e507937c85c5f15401a"},    /* Entity base */
    {0x8e2bf0,90,"ea775b0536683bdde05502a8a3ea0fd71c8c58f118d9950d6606349e29055300"},     /* factory post-init */
    {0x554a70,612,"1a8b86c4b54cc19ea2c957122c490a348f15a4fd1ac4cef1555d2de8b5b09d3c"},    /* SetOwner (vfunc 721) */
    {0x47e7b0,101,"daa5496a17ba992cd0d5bd02b6b207079673c1a343034b3c33fa8bcfb56a2c34"}};   /* out reference */
#define VPS_PIN_COUNT (sizeof(vps_pin_table)/sizeof(*vps_pin_table))
static vps_create_fn vps_create;            /* production: image+VPS_FACTORY; tests override */
static uint64_t *(*vps_rng_word)(void);     /* the calling thread's TLS RNG word */
static int vps_mode,vps_pins_checked;       /* mode 1 = construct */
static uint64_t vps_tick,vps_at,vps_checks,vps_guard_bytes;
static const char *vps_stage="off",*vps_why="-";
static unsigned char *vps_p1,*vps_p2;
static int vps_p1_refs,vps_p2_refs,vps_rng_moved,vps_render_moved,vps_context_moved,vps_singleton_moved;
#define VPS_GUARD_LIST 8u
static unsigned vps_guard_rva[VPS_GUARD_LIST],vps_guard_listed;  /* the first guard bytes the call moved */
static uint64_t *vps_tls_rng(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots && slots[0]?(uint64_t*)((unsigned char*)slots[0]+0x318):NULL;
}
static int vps_pins(void) {
    if(!vps_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<VPS_PIN_COUNT;i++)
            if(!(ok=hash_ok(image+vps_pin_table[i].rva,vps_pin_table[i].size,vps_pin_table[i].sha))) pin_failed_rva=vps_pin_table[i].rva;
        /* case 0x4c of the factory's class switch (RVA table at 0x8e2a38) is the Player block 0x8e2149. */
        uint32_t entry=0;
        if(ok && !(ok=sample_read(image+0x8e2a38+4*VPS_PLAYER_CLASS,&entry,4) && entry==0x8e2149u)) pin_failed_rva=0x8e2a38u;
        vps_pins_checked=ok?1:-1;
    }
    return vps_pins_checked==1;
}
static int vps_fail(const char *why) {vps_stage="failed";vps_why=why;return 0;}
/* One U::Player from X4's factory, every side effect on the canonical player put
 * back (the spike below and lockstep_pilot_slots.h): *p1 the canonical player, *made
 * the new one (one reference, ours). 0 = refused or failed (vps_why). */
static int vps_make(unsigned char *base,unsigned char **p1_out,unsigned char **made) {
    unsigned char *universe=NULL,*p1=NULL,*macro=NULL,*render=NULL,*now=NULL;uintptr_t vt=0;
    int cls=0,refs=0,render_value=0;uint64_t empty_size=1;unsigned char context=0;
    static unsigned char before[VPL_BLOCK_MAX],after[VPL_BLOCK_MAX],pages[VPL_GUARD_COUNT][4096];
    if(!vps_create || !vps_rng_word) return vps_fail("no_factory");
    if(!sample_read(base+VPL_UNIVERSE,&universe,8) || !universe || !sample_read(universe+VPS_SINGLETON,&p1,8) || !p1) return vps_fail("no_player");
    if(!sample_read(p1,&vt,8) || vt!=(uintptr_t)(base+VPS_PLAYER_VTABLE)) return vps_fail("player_vtable");
    if(!sample_read(p1+0x60,&macro,8) || !macro || !sample_read(macro+0x44,&cls,4) || cls!=VPS_PLAYER_CLASS) return vps_fail("player_class");
    if(!sample_read(p1+0x40,&refs,4) || refs<2) return vps_fail("player_refs");
    if(!sample_read(base+VPS_EMPTY_STRING+0x10,&empty_size,8) || empty_size) return vps_fail("empty_string");
    if(!vpl_reentry_clear(base)) return vps_fail("reentry");
    if(!sample_read(base+VPS_RENDER_FLAG,&render,8) || !render || !sample_read(render+8,&render_value,4) ||
       !sample_read(base+VPS_CONTEXT_FLAG,&context,1)) return vps_fail("flags");
    uint64_t *rng=vps_rng_word();
    if(!rng) return vps_fail("rng");
    int n=vpl_gather(base,p1,0xffu,before,sizeof(before));
    if(n<0) return vps_fail("gather");
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) if(!sample_read(base+vpl_guard_pages[g],pages[g],4096)) return vps_fail("guard_read");
    uint64_t rng_before=*rng;void *out=NULL;int fault=0;const void *empty=base+VPS_EMPTY_STRING;
    vps_p1_refs=refs;
    __try {vps_create(universe,&out,NULL,NULL,NULL,NULL,macro,NULL,1,NULL,0,empty,empty,empty,NULL,0,1);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    /* The calling thread's stream, the two flags: as they were. */
    vps_rng_moved=*rng!=rng_before;*rng=rng_before;
    int render_now=0;unsigned char context_now=0;
    if(sample_read(render+8,&render_now,4) && render_now!=render_value) {vps_render_moved=1;*(volatile int*)(render+8)=render_value;}
    if(sample_read(base+VPS_CONTEXT_FLAG,&context_now,1) && context_now!=context) {vps_context_moved=1;*(volatile unsigned char*)(base+VPS_CONTEXT_FLAG)=context;}
    if(!sample_read(universe+VPS_SINGLETON,&now,8)) return vps_fail("singleton_read");
    if(fault) {if(now!=p1) *(unsigned char *volatile*)(universe+VPS_SINGLETON)=p1;return vps_fail("construct_fault");}
    unsigned char *p2=(unsigned char*)out;
    if(!p2 || p2==p1) {if(now!=p1) *(unsigned char *volatile*)(universe+VPS_SINGLETON)=p1;return vps_fail("no_object");}
    if(now!=p2) return vps_fail("singleton_unexpected"); /* 6fa8e0 always repoints: anything else is unknown */
    /* Put the canonical player back: its Universe reference returns, the new one's leaves. */
    vps_singleton_moved=1;
    *(unsigned char *volatile*)(universe+VPS_SINGLETON)=p1;
    InterlockedIncrement((volatile LONG*)(p1+0x40));
    LONG left=InterlockedDecrement((volatile LONG*)(p2+0x40));
    vps_p2_refs=(int)left;
    unsigned char *p2_macro=NULL,*parent=(unsigned char*)1;
    if(left<1) return vps_fail("p2_refs");
    if(!sample_read(p2,&vt,8) || vt!=(uintptr_t)(base+VPS_PLAYER_VTABLE) || !sample_read(p2+0x60,&p2_macro,8) || p2_macro!=macro)
        return vps_fail("p2_identity");
    if(!sample_read(p2+0x70,&parent,8) || parent) return vps_fail("p2_parent");
    if(!sample_read(p1+0x40,&refs,4) || refs!=vps_p1_refs) return vps_fail("p1_refs");
    if(vpl_gather(base,p1,0xffu,after,sizeof(after))!=n || memcmp(before,after,(size_t)n)) return vps_fail("block_changed");
    vps_guard_bytes=0;vps_guard_listed=0;
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++) {
        unsigned char page[4096];
        if(!sample_read(base+vpl_guard_pages[g],page,sizeof(page))) return vps_fail("guard_read");
        for(unsigned i=0;i<4096u;i++) if(page[i]!=pages[g][i]) {
            ++vps_guard_bytes;if(vps_guard_listed<VPS_GUARD_LIST) vps_guard_rva[vps_guard_listed++]=vpl_guard_pages[g]+i;
        }
    }
    *p1_out=p1;*made=p2;vps_why="-";
    return 1;
}
static int vps_construct(unsigned char *base) {
    unsigned char *p1=NULL,*p2=NULL;
    if(!vps_make(base,&p1,&p2)) return 0;
    vps_p1=p1;vps_p2=p2;vps_stage="constructed";
    return 1;
}
/* Every pass after the construction: the canonical player is still the singleton,
 * the new player intact, referenced and unparented. */
static int vps_check(unsigned char *base) {
    unsigned char *universe=NULL,*now=NULL,*parent=(unsigned char*)1;uintptr_t vt=0;int refs=0;
    ++vps_checks;
    if(!sample_read(base+VPL_UNIVERSE,&universe,8) || !universe || !sample_read(universe+VPS_SINGLETON,&now,8) || now!=vps_p1)
        return vps_fail("singleton_changed");
    if(!sample_read(vps_p2,&vt,8) || vt!=(uintptr_t)(base+VPS_PLAYER_VTABLE) || !sample_read(vps_p2+0x40,&refs,4) || refs<1)
        return vps_fail("p2_lost");
    if(!sample_read(vps_p2+0x70,&parent,8) || parent) return vps_fail("p2_parented");
    vps_p2_refs=refs;
    return 1;
}
/* The spike's part of a pass (before the observation): 0 stops the gate (160). */
static int vps_pass(uint64_t tick,unsigned char *base) {
    if(!vps_mode || !strcmp(vps_stage,"failed")) return !vps_mode;
    if(!vps_p2) {
        if(tick<vps_tick) {vps_stage="waiting";return 1;}
        vps_at=tick;
        return vps_construct(base);
    }
    return vps_check(base);
}
static int vps_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," spike=construct spike_stage=%s why=%s at=%llu due=%llu p1_refs=%d p2_refs=%d singleton=%d rng=%d render=%d "
        "context=%d guard=%llu checks=%llu",vps_stage,vps_why,(unsigned long long)vps_at,(unsigned long long)vps_tick,vps_p1_refs,vps_p2_refs,
        vps_singleton_moved,vps_rng_moved,vps_render_moved,vps_context_moved,(unsigned long long)vps_guard_bytes,(unsigned long long)vps_checks);
    if(n<=0 || (size_t)n>=capacity) return 0;
    for(unsigned i=0;i<vps_guard_listed;i++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%x",i?",":" guard_at=",vps_guard_rva[i]);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
/* One observation per admitted frame (MoveWorker, pathing pass not released);
 * 0 = the spike failed (the gate stops with VPS_STOP_REASON). */
static int vpl_pass_using(uint64_t tick,unsigned char *base) {
    if(!vpl_armed) return 1;
    if(!vps_pass(tick,base)) return 0;
    ++vpl_passes;vpl_last_tick=tick;
    unsigned char *player=vpl_player_of(base),*controller=NULL,*component=NULL;uintptr_t vtable=0;unsigned char bound=0,reentry=1;
    if(!sample_read(base+VPL_REENTRY,&reentry,1) || reentry) ++vpl_reentry_seen;
    if(!player) {++vpl_missing;vpl_stage="no_player";vpl_have_prev=0;return 1;}
    controller=(unsigned char*)vpl_e.controller(base+VPL_GAME);
    if(controller && (!sample_read(controller,&vtable,8) || vtable!=(uintptr_t)(base+VPL_CONTROLLER_VTABLE))) controller=NULL;
    if(controller && (!sample_read(controller+0x28,&component,8) || !sample_read(controller+0xe0,&bound,1))) controller=NULL;
    if(controller && !bound) ++vpl_unbound;
    if(vpl_seen_player && (player!=vpl_seen_player || controller!=vpl_seen_controller || component!=vpl_seen_component)) ++vpl_identity_changes;
    vpl_seen_player=player;vpl_seen_controller=controller;vpl_seen_component=component;
    int n=vpl_gather(base,player,0xffu,vpl_now,sizeof(vpl_now));
    if(n<0) {++vpl_read_failures;vpl_stage="read_failed";vpl_have_prev=0;return 1;}
    uint64_t hash[VPL_CLASSES];
    for(unsigned c=0;c<VPL_CLASSES;c++) hash[c]=0xcbf29ce484222325ULL;
    size_t at=0;vpl_changed_mask=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        hash[r->cls]=vpl_region_hash(r,vpl_now+at,hash[r->cls]);
        if(vpl_have_prev && memcmp(vpl_now+at,vpl_prev+at,r->size)) {
            vpl_changed_mask|=1ULL<<i;
            if(!vpl_region_changes[i]++) vpl_region_first[i]=tick;
            vpl_region_last[i]=tick;++vpl_class_changes[r->cls];
            if(r->cls==VPL_CONFIG) ++vpl_config_changes;
        }
        at+=r->size;
    }
    memcpy(vpl_class_hash,hash,sizeof(hash));memcpy(vpl_prev,vpl_now,(size_t)n);vpl_have_prev=1;
    vpl_guard_pass(base);
    if(!vpl_census_done && !vpl_census_take(base)) {if(++vpl_census_failures>=600) vpl_census_done=-1;} /* retried while the input object settles */
    else if(!vpl_census_done) vpl_census_done=1;
    vpl_stage=vpl_config_changes?"config_changed":controller?"observing":"no_controller";
    return 1;
}
static int vpl_pass(uint64_t tick) {return !vpl_armed || GetCurrentThreadId()!=ui_thread || vpl_pass_using(tick,vpl_e.base);}
/* gate.virtual_players('construct', tick): 'observe' plus the spike; an identical
 * rerun after a loading-UI reload succeeds, any other combination is refused. */
static int vps_arm(uint64_t tick) {
    if(tick<2 || tick>18000) return 0;
    if(vps_mode) return vpl_armed && vps_tick==tick;
    if(vpl_armed || !vps_pins() || !vpl_arm()) return 0;
    vps_create=(vps_create_fn)(image+VPS_FACTORY);vps_rng_word=vps_tls_rng;
    vps_mode=1;vps_tick=tick;vps_stage="waiting";vps_why="-";
    return 1;
}

static int vpl_status(char *out,size_t capacity) {
    if(!vpl_armed) return 0;
    int n=snprintf(out,capacity,"mode=%s stage=%s passes=%llu tick=%llu player=%d controller=%d missing=%llu unbound=%llu reentry=%llu "
        "identity=%llu sim=%016llx device=%016llx local=%016llx option=%016llx shared=%016llx config=%016llx changed=%llx "
        "changes=%llu/%llu/%llu/%llu/%llu config_changes=%llu undeclared=%llu census=%d nodes=%u read_failures=%llu",
        vps_mode?"construct":"observe",vpl_stage,(unsigned long long)vpl_passes,(unsigned long long)vpl_last_tick,vpl_seen_player!=NULL,vpl_seen_controller!=NULL,
        (unsigned long long)vpl_missing,(unsigned long long)vpl_unbound,(unsigned long long)vpl_reentry_seen,(unsigned long long)vpl_identity_changes,
        (unsigned long long)vpl_class_hash[VPL_SIM],(unsigned long long)vpl_class_hash[VPL_DEVICE],(unsigned long long)vpl_class_hash[VPL_LOCAL],
        (unsigned long long)vpl_class_hash[VPL_OPTION],(unsigned long long)vpl_class_hash[VPL_SHARED],(unsigned long long)vpl_class_hash[VPL_CONFIG],
        (unsigned long long)vpl_changed_mask,(unsigned long long)vpl_class_changes[VPL_SIM],(unsigned long long)vpl_class_changes[VPL_DEVICE],
        (unsigned long long)vpl_class_changes[VPL_LOCAL],(unsigned long long)vpl_class_changes[VPL_OPTION],(unsigned long long)vpl_class_changes[VPL_SHARED],
        (unsigned long long)vpl_config_changes,(unsigned long long)vpl_undeclared_changes,vpl_census_done,vpl_census_nodes,
        (unsigned long long)vpl_read_failures);
    if(n<=0 || (size_t)n>=capacity) return 0;
    if(vps_mode) {int more=vps_status(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    return n;
}
/* name:class:changes:first:last per region. */
static int vpl_regions_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%s:%llu:%llu:%llu",n?",":"",vpl_regions[i].name,vpl_class_names[vpl_regions[i].cls],
            (unsigned long long)vpl_region_changes[i],(unsigned long long)vpl_region_first[i],(unsigned long long)vpl_region_last[i]);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
/* Per-region FNV-1a of the last pass, in table order (lanes compare these to find a differing region). */
static int vpl_hashes_text(char *out,size_t capacity) {
    int n=0;size_t at=0;out[0]=0;
    if(!vpl_have_prev) return 0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        uint64_t h=vpl_region_hash(&vpl_regions[i],vpl_prev+at,0xcbf29ce484222325ULL);at+=vpl_regions[i].size;
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%016llx",n?",":"",vpl_regions[i].name,(unsigned long long)h);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
/* Raw bytes of one class from the last pass (hex, table order): config/option values to diff lanes. */
static int vpl_raw_text(unsigned cls,char *out,size_t capacity) {
    size_t at=0;int n=0;out[0]=0;
    if(!vpl_have_prev || cls<VPL_SIM || cls>=VPL_CLASSES) return 0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->cls==cls) {
            if((size_t)n+2u*r->size+1u>=capacity) return 0;
            pilot_hex(vpl_prev+at,r->size,out+n);n+=(int)(2u*r->size);
        }
        at+=r->size;
    }
    return n;
}
/* name:passes for every neighbour (passes in which any of its guard bytes changed). */
static int vpl_neighbours_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    for(unsigned k=0;k<VPL_NEIGHBOUR_COUNT;k++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%llu",n?",":"",vpl_neighbours[k].name,(unsigned long long)vpl_neighbour_changes[k]);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
/* Undeclared guard-page bytes that ever changed, as RVA ranges (at most 64). */
static int vpl_undeclared_text(char *out,size_t capacity) {
    int n=0;unsigned ranges=0;out[0]=0;
    for(unsigned g=0;g<VPL_GUARD_COUNT;g++)
        for(unsigned i=0;i<4096u;) {
            if(!(vpl_guard_ever[g][i>>3]>>(i&7)&1u)) {++i;continue;}
            unsigned j=i;while(j<4096u && (vpl_guard_ever[g][j>>3]>>(j&7)&1u)) ++j;
            if(ranges++>=64) return n;
            int more=snprintf(out+n,capacity-(size_t)n,"%s%x+%u",n?",":"",vpl_guard_pages[g]+i,j-i);
            if(more<0 || (size_t)(n+more)>=capacity) return n;
            n+=more;i=j;
        }
    return n;
}
/* kind:class=count per kind, then the unknown/addon registrations kind:id:rva. */
static int vpl_census_text(char *out,size_t capacity) {
    static const char *const kinds[3]={"action","state","range"};
    int n=snprintf(out,capacity,"census=%d nodes=%u",vpl_census_done,vpl_census_nodes);
    if(n<0 || (size_t)n>=capacity) return 0;
    for(unsigned k=0;k<3;k++) for(unsigned c=0;c<VPL_H_KINDS;c++) if(vpl_census[k][c]) {
        int more=snprintf(out+n,capacity-(size_t)n," %s.%s=%u",kinds[k],vpl_handler_names[c],vpl_census[k][c]);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    for(unsigned i=0;i<vpl_census_listed;i++) {
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%s:0x%x:%x",i?",":" list=",kinds[vpl_census_list[i].kind],
            vpl_handler_names[vpl_census_list[i].cls],vpl_census_list[i].id,vpl_census_list[i].rva);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    if(vpl_census_dropped) {
        int more=snprintf(out+n,capacity-(size_t)n," dropped=%u",vpl_census_dropped);
        if(more<0 || (size_t)(n+more)>=capacity) return 0;
        n+=more;
    }
    return n;
}
