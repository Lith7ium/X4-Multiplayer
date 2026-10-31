/* A slot ship's death does not end the session (native_gate_pilot_death, default off; stop reason
 * 163; report LOCKSTEP-VIRTUAL-PLAYERS-RE-20261029 section 16.17).
 *
 * What X4 does (build 9.00). A hull reaching zero runs U::Destructible::vfunc150 518210. If the object
 * is the one the singleton player occupies and it is no spacesuit: when the player's emergency eject
 * is active (710a90: Player+0x6a90, set by the constructor and the save, and the user option
 * "emergencyeject", default on) 70efd0(player, 1,1,1,1) builds a spacesuit next to the ship, with
 * minimum hull 100%, and Player::TeleportTo 7154b0 puts the player in it at once (event: vfunc76
 * 7052f0, which also deletes the old controller). Then, always, Kill 3e51b0 posts a deferred KillEvent;
 * KillRecursive 3e5240 takes the ship and everything below it. A player who is still aboard then gets
 * U::Player::vfunc9 7046e0 with itself ("Game Over - Player has been killed by"), which calls the game
 * over 5c3660: flag 0x400 of 6adf9fc (IsGameOver, read by some 60 functions), SETA off, GameOverEvents
 * for U::Galaxy::vfunc6 5c3d20, the "showGameOver" UI event (CUIController::vfunc89 b3d3e0) and the
 * menu with the load prompt. So in a stock session a ship's death is the eject; the game over needs a
 * player who dies with what it sits in. A later teleport away from a suit kills the suit (vfunc76).
 *
 * Slot 1, X4's own player. The eject above runs inside the simulation, on every client in the same
 * tick, so the slot's dead state is X4's own: the player floats in a suit, the HUD and camera are
 * X4's. The stop reason was only the stock controller going away with it (155). The user option is
 * refused when off (a client without the eject would keep a dead player). The stock kill of the
 * player cannot be reached by a ship death with the eject on, and a player killed in any other way
 * (the suit by a script) never reaches the game over: the guard re-points the call in vfunc9 while
 * the session runs and stops with player_killed (163), no flag, no menu, no save touched.
 *
 * Slots 2..N. P_k is in no child list, so the kill does not touch it, and the eject is never reached
 * for it (the singleton's ship is what vfunc150 compares). Its seat ends: released in its scope when
 * the ship is still whole (the order of the stock eject: release, then the kill arrives), else the
 * controller stays with the dying ship under its vtable copy, its scoped entries doing nothing
 * (pslot_retire). Client k presents X4's own player without the overlay meanwhile: slot 1's view, or
 * slot 1's suit.
 *
 * A suit for slot k (native_gate_pilot_death_suit, default off). X4's way to a suit, 70efd0, ends in
 * Player::TeleportTo, whose event runs on a player outside any scope. The seat needs none of it: MD
 * builds a spacesuit of the player's macro (create_ship, as stock does for an entity; minimum hull as
 * the eject), and P_k takes it with the seat's own take in its scope (U::SpaceSuit keeps Ship's
 * 1074 and 1075). The overlay then presents it with X4's camera and HUD, and client k sees what client 1
 * sees. Its records stay consumed and ignored. Seated PDEATH_SUIT_LAG ticks after the death; the next
 * seat releases it and kills it with reason 13, as vfunc76 does with the player's old suit.
 *
 * The slot state, every client, from the pass of the first tick that sees it (the hull is simulation
 * state, so this is the same tick everywhere): no record of the slot is applied, each is still
 * consumed and its hash stays in the declared state, flight control goes off once as the stock
 * callback would have turned it off, the guns are given up. A controller change whose cause is not the
 * slot's own ship dying still stops with 155. The dead mask is part of the declared state (|D).
 *
 * Respawn (native_gate_pilot_respawn = delay in ticks, needs the MD file of that name): at dead+delay
 * the probe raises the UI event, MD builds a ship of the slot's macro at its start, X4's create_ship
 * and for slot 1 X4's teleport_player, on every client in the same frame. At create+PDEATH_SEAT_LAG
 * (the MD answer can lag) every client takes the slot back: slot 1's controller and ship are latched
 * again and what its last records left held is released; slot k is seated as at the start with its
 * first store. A slot's input held across the death has to be pressed again. */
#define PDEATH_STOP_REASON 163u
#define PDEATH_LIFECYCLE 0xd3u                /* Component+0xd3: 1 dying, 2 dead (KillRecursive) */
#define PDEATH_SEAT_LAG 15u                   /* past the identity's FOLD_LAG (10): the MD answer is back by then */
#define PDEATH_GAME_OVER 0x5c3660u
#define PDEATH_GAME_OVER_CALL 0x70498au       /* in U::Player::vfunc9 */
#define PDEATH_STUB 0x80u                     /* on the slots' near page, after the presentation's */
#define PDEATH_PLAYER_KILLED_SLOT 9u
#define PDEATH_SUIT_LAG 5u                    /* the MD answer came in MD's own frame in the respawn runs */
#define PDEATH_KILL_SLOT (0x1548u/8u)         /* component vtable: Kill */
#define PDEATH_KILL_REMOVED 13u               /* what vfunc76 kills the player's old suit with */
enum {PDEATH_ALIVE,PDEATH_DEAD,PDEATH_CREATING};
typedef struct {unsigned state,deaths,respawns;uint64_t dead_tick,create_tick,seat_tick,ship,suit_tick,suit;int in_suit;} pdeath_slot;
static pdeath_slot pdeath_slots[PILOT_SLOTS];
static unsigned pdeath_delay;
static int pdeath_suit;
static int pdeath_pins_checked,pdeath_guard_installed;
static unsigned char pdeath_guard_patched[5];
static const char *pdeath_stage="off";
static const char *pdeath_why="-";
static char pdeath_events[128];
static size_t pdeath_event_length;
static volatile LONG64 pdeath_game_overs;
static unsigned pdeath_released,pdeath_retired;   /* how seats ended: whole ship, or the kill already through */
static const struct {unsigned rva,size;const char *sha;} pdeath_pin_table[]={
    {0x518210,1396,"521c1f3b8aee2f13c5ca701477cc2c8c7061ed928f17bb9774072cdf28734906"},  /* the hull at zero */
    {0x710a90,60,"a45746776fb1d406eab65098063421f2423e9c6be634f6320f876859515db7ba"},    /* eject active */
    {0x70efd0,5055,"00ae115c9190fe969bdaa82d59007880d8281133f9a0e05d2596c4fb728b9bff"},  /* the eject */
    {0x7046e0,1343,"454f24948e328debb1f721c1bfa1ec978d99da02994af76a488c472680845c76"},  /* the player killed */
    {0x5c3660,1726,"72630dcf8622f42576b5979a635ebfde6f52e2653961ea310e942d9975d192f9"},  /* the game over */
    {0x7052f0,1965,"c1344ac923adb1aca9fcf4ef5ed44173145a42631183992a873e131a6d61443c"},  /* the teleport event */
    {0x7154b0,4538,"7f5210c56c082b9baf59be35b6d58c1f4072ee18a3ddcd57ffdd0bea9d77d9ce"}}; /* the teleport */
static int pdeath_pins(void) {
    if(!pdeath_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<sizeof(pdeath_pin_table)/sizeof(*pdeath_pin_table);i++)
            if(!(ok=hash_ok(image+pdeath_pin_table[i].rva,pdeath_pin_table[i].size,pdeath_pin_table[i].sha))) pin_failed_rva=pdeath_pin_table[i].rva;
        if(ok && !(ok=((void**)(image+VPS_PLAYER_VTABLE))[PDEATH_PLAYER_KILLED_SLOT]==(void*)(image+0x7046e0))) pin_failed_rva=VPS_PLAYER_VTABLE;
        pdeath_pins_checked=ok?1:-1;
    }
    return pdeath_pins_checked==1;
}
static int pdeath_fail(const char *why) {
    pdeath_stage="failed";if(!strcmp(pdeath_why,"-")) pdeath_why=why;
    pdeath_failed=1;return 0;
}
static void pdeath_event(const char *kind,unsigned k) {
    int n=snprintf(pdeath_events+pdeath_event_length,sizeof(pdeath_events)-pdeath_event_length,"%s%s:%u",pdeath_event_length?",":"",kind,k);
    if(n>0 && pdeath_event_length+(size_t)n<sizeof(pdeath_events)) pdeath_event_length+=(size_t)n;
}
/* The game over call in U::Player::vfunc9: the player was killed some other way than with a ship. */
static void pdeath_game_over(void) {
    InterlockedIncrement64(&pdeath_game_overs);
    pdeath_stage="failed";if(!strcmp(pdeath_why,"-")) pdeath_why="player_killed";
    pdeath_failed=1;pnat_async_fail("player_killed");
}
static int pdeath_guard_install(void) {
    if(pdeath_guard_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) return 0;
    if(!pres_site_install(PDEATH_GAME_OVER_CALL,PDEATH_GAME_OVER,PDEATH_STUB,(void*)pdeath_game_over,pdeath_guard_patched)) return 0;
    pdeath_guard_installed=1;return 1;
}
static void pdeath_restore(void) {
    if(pdeath_guard_installed && pres_site_uninstall(PDEATH_GAME_OVER_CALL,PDEATH_GAME_OVER,pdeath_guard_patched)) pdeath_guard_installed=0;
}
/* What a slot 1 record left held (states and ranges): released at the seat, on every client. */
static unsigned char pdeath_held[2][512];
static void pdeath_note(const pstk_event *x) {
    if((x->kind==PSTK_STATE || x->kind==PSTK_RANGE) && x->id<512) pdeath_held[x->kind==PSTK_RANGE][x->id]=x->value!=0;
}
static int pdeath_release_held(void) {
    unsigned char *map=pnat_input+0x63e0;
    for(unsigned kind=0;kind<2;kind++) for(unsigned id=0;id<512;id++) {
        if(!pdeath_held[kind][id]) continue;
        unsigned which=kind?PSTK_RANGE:PSTK_STATE;
        pnat_node *upper=NULL,*n=pnat_first((pnat_node*)(map+0x40*which),(int)id,&upper);
        for(;n && n!=upper;n=pnat_next(n)) if(!pstk_handler_ok(n->handler)) return 0;
        pstk_walk(map+0x40*which,which,(int)id,0);
        pdeath_held[kind][id]=0;
    }
    return 1;
}
/* The slot's ship is going or gone; a ship that resolved once and a registry that no longer knows it,
 * or a hull at zero (518210's own test). */
static int pdeath_dying(const pilot_slot *s) {
    double hull=1.0;
    if(!s->seen) return 0;
    if(!s->found) return 1;
    return read_local(s->found+PDEATH_HULL,&hull,8) && fabs(hull)<PDEATH_HULL_EPSILON;
}
static int pdeath_die(unsigned k,uint64_t tick) {
    pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
    if(k==1) {
        /* X4's own controller went with the ship: the stock callback's half of leaving flight control,
         * on every client, because its local half (pstk_space) only records. */
        if(pstk_flight) {
            double now=0;
            if(!pnat_fn.time(&now)) return pdeath_fail("die_time");
            pstk_space_replay(0,now);
        }
        pstk_flight=0;pstk_flight_known=1;pnat_controller=NULL;pnat_ship=NULL;
    } else {
        pslot_slot *x=&pslots[k];unsigned char lifecycle=1;
        if(x->seated) {
            if(s->found==x->ship && read_local(x->ship+PDEATH_LIFECYCLE,&lifecycle,1) && !lifecycle && pslot_controller_has(x->ship,0,x->controller)==1) {
                if(!pslot_release(k)) {pdeath_why=pslot_why;return 0;}
                ++pdeath_released;
            } else {pslot_retire(k);++pdeath_retired;}
        }
        pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
        if(k==pres_slot) {pres_anchor_home();pres_target_set=0;pres_local_target=0;pview_slot_target=0;}
    }
    if(pfire_armed) {(void)pfire_end(s,&pfire_slots[k-1],k);memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));}
    s->dead=1;s->resolved=0;s->object=s->agent=s->solver=NULL;s->player=0;
    pdeath_dead|=1u<<(k-1);
    d->state=PDEATH_DEAD;d->dead_tick=tick;++d->deaths;d->ship=0;
    if(pdeath_delay) {d->create_tick=tick+pdeath_delay;d->seat_tick=d->create_tick+PDEATH_SEAT_LAG;}
    pdeath_event("die",k);
    if(pdeath_suit && k>=2) {d->suit_tick=tick+PDEATH_SUIT_LAG;d->suit=0;d->in_suit=0;pdeath_event("suit",k);}
    return 1;
}
/* A dead slot k: into the suit MD built, on every client at the same tick. */
static int pdeath_suit_pass(unsigned k,uint64_t tick) {
    pslot_slot *x=&pslots[k];pdeath_slot *d=&pdeath_slots[k-1];
    if(d->in_suit) {
        /* Not by a hit (minimum hull). The seat ends, the slot stays dead: slot 1's view again. */
        if(x->seated && pdeath_find(pdeath_map,x->runtime,4)!=x->ship) {pslot_retire(k);d->in_suit=0;pdeath_event("nosuit",k);}
        return 1;
    }
    if(!d->suit_tick || tick<d->suit_tick) return 1;
    d->suit_tick=0;
    if(!d->suit) return pdeath_fail("suit_no_ship");
    unsigned char *suit=pdeath_find(pdeath_map,d->suit,4);
    if(!suit) return pdeath_fail("suit_gone");
    if(!pslot_seat_ship(k,suit)) {pdeath_why=pslot_why;return 0;}
    d->in_suit=1;pdeath_event("insuit",k);
    return 1;
}
/* create+lag: the ship MD built is the slot's now. */
static int pdeath_seat(unsigned k,unsigned char *controller) {
    pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];unsigned char *back=NULL;
    if(!d->ship) return pdeath_fail("respawn_no_ship");
    if(k>=2 && d->in_suit) {
        /* Out of the suit first, and it goes as the player's old suit goes after a teleport (7052f0). */
        unsigned char *suit=pslots[k].ship;void **vtable=NULL;double now=0;
        if(pslots[k].seated && !pslot_release(k)) {pdeath_why=pslot_why;return 0;}
        d->in_suit=0;
        if(pnat_fn.time(&now) && read_local(suit,&vtable,8) && vtable) ((hitn_kill_fn)vtable[PDEATH_KILL_SLOT])(suit,PDEATH_KILL_REMOVED,NULL,now);
    }
    s->runtime=d->ship;s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
    if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player)) {s->dead=1;return pdeath_fail("respawn_unresolved");}
    if(k==1) {
        if(!controller || !read_local(controller+0x28,&back,8) || back!=s->object) {
            s->dead=1;s->resolved=0;s->object=NULL;return pdeath_fail("respawn_not_in_control");
        }
        pnat_controller=controller;pnat_ship=s->object;
        pstk_flight=*PNAT_U8(PNAT_FLIGHT);pstk_flight_known=1;
        if(!pdeath_release_held()) return pdeath_fail("respawn_foreign_handler");
    } else {
        pstk_slot_frames[k-1].flight=0;pstk_slot_frames[k-1].flight_known=1;
        if(!pslot_seat(k)) {pdeath_why=pslot_why;return 0;}
    }
    memset(&pfire_slots[k-1],0,sizeof(pfire_slots[k-1]));
    /* The local controls stayed active through the death: the edge the new flight control needs. */
    if(pstk_capturing && pstk_capture_slot==k && pstk_local_active==1) pstk_record_event(PSTK_SPACE,8,1u);
    pdeath_dead&=~(1u<<(k-1));
    d->state=PDEATH_ALIVE;++d->respawns;
    pdeath_event("live",k);
    return 1;
}
/* First thing in a pass, before the pieces that rely on the slot ships. */
static int pdeath_pass(uint64_t tick,unsigned char *controller) {
    uint64_t take=pilot_start-PILOT_SUPPRESS_LEAD;
    if(pdeath_failed) return pdeath_fail(pdeath_why);
    if(!pdeath_guard_installed && !pdeath_guard_install()) return pdeath_fail("guard_install");
    if(memcmp(pnat_base+PDEATH_GAME_OVER_CALL,pdeath_guard_patched,5)) return pdeath_fail("guard_lost");
    if(tick<=take+1 || tick>pilot_stop) return 1;
    for(unsigned k=1;k<=pilot_slot_count;k++) {
        if(!(pstk_slot_mask>>(k-1)&1u)) continue;
        pilot_slot *s=&pilot_slots[k-1];pdeath_slot *d=&pdeath_slots[k-1];
        if(!s->dead && pdeath_dying(s) && !pdeath_die(k,tick)) return 0;
        if(!s->dead) continue;
        if(pdeath_suit && k>=2 && !pdeath_suit_pass(k,tick)) return 0;
        if(!pdeath_delay) continue;
        if(d->state==PDEATH_DEAD && tick>=d->create_tick && d->seat_tick<pilot_stop) {d->state=PDEATH_CREATING;pdeath_event("create",k);}
        if(d->state==PDEATH_CREATING && tick>=d->seat_tick && !pdeath_seat(k,controller)) return 0;
    }
    pdeath_stage=pdeath_dead?"dead":"live";
    return 1;
}
/* Arming (Lua, before the first tick, after gate.pilot_stock). delay 0: a dead slot stays dead. */
static int pdeath_arm_using(unsigned delay,int suit) {
    unsigned char *options=NULL;
    if(pdeath_armed) return pdeath_delay==delay && pdeath_suit==suit?1:pstk_arm_refuse("death_rerun");
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) return pstk_arm_refuse("death_without_stock");
    if(delay>PILOT_TICK_MAX) return pstk_arm_refuse("death_delay");
    if(!read_local(pnat_base+PNAT_OPTIONS,&options,8) || !options) return pstk_arm_refuse("death_options");
    if(!pnat_fn.option(options+0x118,"emergencyeject",1)) return pstk_arm_refuse("death_eject_off");
    memset(pdeath_slots,0,sizeof(pdeath_slots));memset(pdeath_held,0,sizeof(pdeath_held));
    pdeath_delay=delay;pdeath_suit=suit;pdeath_dead=0;pdeath_failed=0;pdeath_stage="armed";pdeath_why="-";pdeath_event_length=0;pdeath_events[0]=0;
    pdeath_guard_installed=0;pdeath_game_overs=0;pdeath_released=pdeath_retired=0;
    pdeath_armed=1;return 1;
}
static int pdeath_arm(unsigned delay,int suit) {
    if(!pdeath_armed) {
        if(stats.issued) return pstk_arm_refuse("death_after_first_tick");
        if(!pdeath_pins()) {snprintf(pstk_arm_why,sizeof(pstk_arm_why),"pins_death:%x",pin_failed_rva);return 0;}
    }
    return pdeath_arm_using(delay,suit);
}
/* gate.pilot_death_frame: what changed since the last call, "die:1,create:2,live:1". */
static const char *pdeath_take_events(void) {
    if(!pdeath_event_length) return NULL;
    pdeath_event_length=0;return pdeath_events;
}
/* gate.pilot_death_suit: the MD answer; only for a slot waiting for one. */
static int pdeath_suit_ship(unsigned k,uint64_t runtime) {
    if(!pdeath_armed || !pdeath_suit || k<2 || k>pilot_slot_count || !runtime || !pdeath_slots[k-1].suit_tick || pdeath_slots[k-1].suit) return 0;
    pdeath_slots[k-1].suit=runtime;return 1;
}
/* gate.pilot_death_ship: the MD answer; only for a slot waiting for one. */
static int pdeath_ship(unsigned k,uint64_t runtime) {
    if(!pdeath_armed || k<1 || k>pilot_slot_count || !runtime || pdeath_slots[k-1].state!=PDEATH_CREATING || pdeath_slots[k-1].ship) return 0;
    pdeath_slots[k-1].ship=runtime;return 1;
}
static int pdeath_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," death=%s death_why=%s dead=%x delay=%u suit=%d guard=%d game_overs=%lld seats=%u/%u",pdeath_stage,pdeath_why,pdeath_dead,pdeath_delay,
        pdeath_suit,pdeath_guard_installed,(long long)pdeath_game_overs,pdeath_released,pdeath_retired);
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=pilot_slot_count;k++) {
        const pdeath_slot *d=&pdeath_slots[k-1];
        int more=snprintf(out+n,capacity-(size_t)n," d%u=%u/%u/%u/%llu%s",k,d->state,d->deaths,d->respawns,(unsigned long long)d->dead_tick,d->in_suit?"/suit":"");
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
