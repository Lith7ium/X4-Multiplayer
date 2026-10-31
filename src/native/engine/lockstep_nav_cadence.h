/* Determinism fix, DEFAULT OFF: camera-independent navigation refresh cadence
 * (native_gate_nav_cadence). U::NavigationController::vfunc33 0xea77f0 (ship
 * nav upkeep, through the vtable entry the nav module wraps and directly from
 * PlayerController::vfunc33) schedules two refreshes by the camera distance:
 * - parameters (ctrl+0xd0, when ctrl+0xd8): after the parameter build 0xea1e80,
 *   distance GetCachedDistanceSqToCamera core 0x3e3a60 (cutscene camera, else
 *   g_Universe+0x3d0; call 0xea7953): below 1 km next = now; below 5 km now +
 *   0.5 + FloatState(TLS+0x318, 0.5); else now + 1 + FloatState(TLS+0x318, 1);
 * - obstacle probes ((ctrl+0x48)+0x3e0, when +0x3e8): the same three intervals
 *   from the wrapper 0x3e40a0 (call 0xea7c02); the probes are ray casts through
 *   the zone scene (0xbc4d0) whose results the steering reads.
 * So which ships refresh in which frame, and how many draws the thread word
 * takes, depend on the local camera: two clients with different cameras (each
 * following its own ship) diverge. A fixed choice removes the camera input; the
 * distance is still computed (it only fills the camera's own cache) but ignored:
 * 'far'   every refresh takes the 1-2 s interval (the choice native X4 already
 *         makes for every ship more than 5 km from the camera); its draw is the
 *         same single FloatState call, keyed per fixture by native_gate_nav_rng
 *         and otherwise taken from the per-frame seeded word in walk order;
 * 'every' every refresh is due the next frame without a draw (the choice for
 *         ships within 1 km); RNG-free but costs a parameter build and a probe
 *         set per ship per frame;
 * 'observe' patches nothing and only counts.
 * Sites (2-byte branches, each rewritten by a CAS of its aligned 16 bytes):
 *   0xea795c jae ->near-or-draw: 'far' jmp (73 05 -> eb 05), 'every' nop (66 90)
 *   0xea7977 jae ->far:          'far' jmp (73 14 -> eb 14)
 *   0xea7c0b jb  ->near:         'far' nop (72 42 -> 66 90), 'every' jmp (eb 42)
 *   0xea7c21 jae ->far:          'far' jmp (73 17 -> eb 17)
 * Both cadence blocks are pinned (SHA-256 with the original branches), every
 * gate tick re-checks sites and pins (stop reason 95), stop restores by CAS and
 * never overwrites a foreign value. Counting comes from the nav module's Move
 * wrapper (native_gate_nav_rng/diag/initial_refresh required): per admitted
 * frame, fixture controllers and others apart, calls and for each block the
 * refreshes and their interval class (now / 0.5-1 s / 1-2 s). The player's own
 * controller (PlayerController::vfunc33) is patched but not counted. Fixture
 * controllers, and ships whose saved ID is watched (gate.nav_cadence_watch),
 * also log a NAV_CADENCE_ROW on every change of their enables, deadlines,
 * ship attention, main physics body and zone physics scene. */
/* The Linux gate (linux/gate_nav.h) has its own sites and hook and shares the deadline initialisation below. */
#ifndef NAV_CADENCE_OWN_PLATFORM
#define NAV_F_OWNER 0x28
#define NAV_F_PROBE 0x48
#define NAV_CADENCE_SITES 4u
#define NAV_CADENCE_PIN_A 0xea7866u
#define NAV_CADENCE_PIN_A_SIZE 0x147u
#define NAV_CADENCE_PIN_A_SHA "401b3a3662a733a64afb2c08769015bf2b8b8c4669845f72f6dcaa8fe92509ab"
#define NAV_CADENCE_PIN_B 0xea7bd9u
#define NAV_CADENCE_PIN_B_SIZE 0x87u
#define NAV_CADENCE_PIN_B_SHA "f6e8a6b73096312e4fc0974e88f230fb80f8028c9c03823c07d7a907967a0f06"
enum {NAV_CADENCE_OFF,NAV_CADENCE_FAR,NAV_CADENCE_EVERY,NAV_CADENCE_OBSERVE};
/* Little-endian 2-byte values: original, 'far' (distant), 'every' (equal to original when untouched). */
static const struct {unsigned rva;uint16_t original,distant,every;} nav_cadence_sites[NAV_CADENCE_SITES]={
    {0xea795cu,0x0573u,0x05ebu,0x9066u},{0xea7977u,0x1473u,0x14ebu,0x1473u},
    {0xea7c0bu,0x4272u,0x9066u,0x42ebu},{0xea7c21u,0x1773u,0x17ebu,0x1773u}};
/* [group: 0 fixture, 1 other] calls, then per block (params, probes) refreshed/now/mid (0.5-1 s)/slow (1-2 s). */
typedef struct {uint64_t calls,refresh[2],now[2],mid[2],slow[2];} nav_cadence_counts;
static int nav_cadence_mode,nav_cadence_installed,nav_cadence_restored,nav_cadence_patched;
static volatile LONG nav_cadence_active;
static nav_cadence_counts nav_cadence_frame[2],nav_cadence_total[2];
static uint64_t nav_cadence_tick=UINT64_MAX,nav_cadence_first_now,nav_cadence_first_mid,nav_cadence_unreadable;
static volatile LONG64 nav_cadence_failures;
static const char *nav_cadence_stage="idle";
static uint16_t nav_cadence_value(unsigned i,int mode) {
    return mode==NAV_CADENCE_FAR?nav_cadence_sites[i].distant:mode==NAV_CADENCE_EVERY?nav_cadence_sites[i].every:nav_cadence_sites[i].original;
}
static uint16_t nav_cadence_site(unsigned i) {uint16_t v=0;read_local(image+nav_cadence_sites[i].rva,&v,2);return v;}
/* Hash a pinned range with every site inside it read as its original branch. */
static int nav_cadence_pin_ok(unsigned rva,unsigned size,const char *sha) {
    unsigned char copy[0x150];
    if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {
        unsigned at=nav_cadence_sites[i].rva;if(at<rva || at+2>rva+size) continue;
        uint16_t v;memcpy(&v,copy+(at-rva),2);
        if(v!=nav_cadence_sites[i].original && v!=nav_cadence_sites[i].distant && v!=nav_cadence_sites[i].every) return 0;
        memcpy(copy+(at-rva),&nav_cadence_sites[i].original,2);
    }
    return hash_ok(copy,size,sha);
}
static int nav_cadence_pins(void) {
    /* The vfunc33 entry the nav module pins, and both scheduling blocks. */
    return hash_ok(image+NAV_MOVE,32,"acc248e006b7b6889930478f2b42aa2d7a011fc1107095b78229142f08336964") &&
        nav_cadence_pin_ok(NAV_CADENCE_PIN_A,NAV_CADENCE_PIN_A_SIZE,NAV_CADENCE_PIN_A_SHA) &&
        nav_cadence_pin_ok(NAV_CADENCE_PIN_B,NAV_CADENCE_PIN_B_SIZE,NAV_CADENCE_PIN_B_SHA);
}
/* The branch sites are not 2-byte aligned; CAS the aligned 16 bytes holding each. */
static int nav_cadence_swap(unsigned i,uint16_t expected,uint16_t value) {
    unsigned rva=nav_cadence_sites[i].rva,offset=rva&15u;unsigned char *block=image+(rva&~15u);
    if(offset>14) return 0;
    DWORD old,ignored;if(!VirtualProtect(block,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 current,desired;memcpy(&current,(const void*)block,16);
    uint16_t now;memcpy(&now,(unsigned char*)&current+offset,2);
    int ok=0;
    if(now==expected) {
        desired=current;memcpy((unsigned char*)&desired+offset,&value,2);
        ok=__sync_val_compare_and_swap((unsigned __int128*)block,current,desired)==current;
    }
    VirtualProtect(block,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),block,16);
    return ok;
}
static int nav_cadence_sites_are(int mode) {
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) if(nav_cadence_site(i)!=nav_cadence_value(i,mode)) return 0;
    return 1;
}
static void nav_cadence_roll(void) {
    if(nav_cadence_tick==stats.issued) return;
    nav_cadence_tick=stats.issued;memset(nav_cadence_frame,0,sizeof(nav_cadence_frame));
}
static int nav_cadence_read(const unsigned char *controller,nav_cadence_sample *c) {
    unsigned char *probe=NULL;unsigned char on=0;
    c->probe_on=0;c->probe=0;
    if(!read_local(controller+0xd8,&on,1) || !read_local(controller+0xd0,&c->params,8) ||
       !read_local(controller+0x48,&probe,8)) return 0;
    c->params_on=on;
    if(probe) {
        if(!read_local(probe+0x3e8,&on,1) || !read_local(probe+0x3e0,&c->probe,8)) return 0;
        c->probe_on=on;
    }
    return 1;
}
#endif
/* Probe deadline initialisation, DEFAULT OFF (native_gate_nav_probe_init;
 * needs native_gate_nav_cadence). The pathing Agent constructor 0xe85360
 * (0xe8551c) sets the obstacle-probe deadline (Agent+0xac0, the probe block's
 * +0x3e0) to now + 0.5 + FloatState(TLS word), and vfunc33 rewrites it only
 * while avoidance (+0x3e8) is on. Ships loaded with the save got theirs from
 * unkeyed draws during loading, so each client starts with its own deadlines:
 * T9 74d83 had 401fd724.. vs 401f813a.. at its first call (tick 32) and ran its
 * first obstacle probe at tick 53 vs 48 after avoidance turned on at 33 on
 * both; its motion diverged by capture 60 with identical nav timing fields.
 * With this flag, before each admitted vfunc33 call:
 * - avoidance off: the deadline is pinned to the frame's time - dt, so the
 *   first call with avoidance on probes at once (with the keyed draw) on every
 *   client, whatever an earlier or a newly constructed Agent stored;
 * - avoidance on at the first call per portable ID (saved, or created once
 *   resolved; an insert-only table of IDs): the deadline is set the same way.
 * Native keeps an old deadline across an off/on change and may wait for it;
 * here a re-enabled probe runs at once. */
#define NAV_PROBE_TABLE 65536u
static uint64_t nav_probe_table[NAV_PROBE_TABLE];
static unsigned nav_probe_count;
static int nav_probe_armed;
static uint64_t nav_probe_pins,nav_probe_inits,nav_probe_unmapped,nav_probe_first,nav_probe_tick=UINT64_MAX,nav_probe_frame_pins,nav_probe_frame_inits;
/* Insert-only ID table of NAV_PROBE_TABLE slots: 1 when the ID was new (and is
 * now recorded), 0 when known, -1 when the table is full. */
static int nav_first_call(uint64_t *table,unsigned *count,uint64_t key) {
    unsigned h=(unsigned)((key*0x9e3779b97f4a7c15ULL)>>48)&(NAV_PROBE_TABLE-1);
    for(unsigned n=0;n<NAV_PROBE_TABLE;n++,h=(h+1)&(NAV_PROBE_TABLE-1)) {
        if(table[h]==key) return 0;
        if(!table[h]) {
            if(*count>=NAV_PROBE_TABLE/4*3) return -1;
            table[h]=key;++*count;return 1;
        }
    }
    return -1;
}
static int nav_probe_first_call(uint64_t key) {return nav_first_call(nav_probe_table,&nav_probe_count,key);}
/* The portable key of a controller's ship (controller+0x28), 0 when unmapped;
 * *klass is its WORLD_KEY_* class. */
static uint64_t nav_controller_key(const unsigned char *controller,unsigned *klass) {
    unsigned char *ship=NULL;uint64_t runtime=0;*klass=WORLD_KEY_NONE;
    return read_local(controller+NAV_F_OWNER,&ship,8) && ship && read_local(ship+8,&runtime,8) && runtime?world_identity_key(runtime,klass):0;
}
/* Created IDs carry their creation tick (lockstep_world_created.h:
 * CREATED_BIT | tick << CREATED_TICK_SHIFT | hash; load-created objects tick 0). */
#define NAV_CREATED_TICK_SHIFT 40u
#define NAV_CREATED_TICK_LIMIT 0x7fffffULL
static uint64_t nav_created_tick(uint64_t key) {return (key>>NAV_CREATED_TICK_SHIFT)&NAV_CREATED_TICK_LIMIT;}
static void nav_probe_apply(const unsigned char *controller,double time) {
    unsigned char *probe=NULL,*ship=NULL,on=0;double due=0,now=time-gate_tick_dt();
    if(!isfinite(time) || !read_local(controller+NAV_F_PROBE,&probe,8) || !probe || !read_local(probe+0x3e8,&on,1) ||
       !read_local(probe+0x3e0,&due,8)) return;
    if(nav_probe_tick!=stats.issued) {nav_probe_tick=stats.issued;nav_probe_frame_pins=nav_probe_frame_inits=0;}
    uint64_t runtime=0,key=0;unsigned klass;
    if(read_local(controller+NAV_F_OWNER,&ship,8) && ship && read_local(ship+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    int first=key?nav_probe_first_call(key):0;
    if(first<0) {nav_cadence_stage="probe_table";InterlockedIncrement64(&nav_cadence_failures);return;}
    if(!key) ++nav_probe_unmapped;
    if(!on) {
        if(memcmp(&due,&now,8)) {memcpy(probe+0x3e0,&now,8);++nav_probe_pins;++nav_probe_frame_pins;}
        return;
    }
    if(first==1) {
        memcpy(probe+0x3e0,&now,8);++nav_probe_inits;++nav_probe_frame_inits;
        if(!nav_probe_first) nav_probe_first=stats.issued;
    }
}
/* Before the first admitted frame, after native_gate_nav_cadence. */
static int nav_probe_arm(void) {
    if(nav_probe_armed) return nav_cadence_installed;
    if(!nav_cadence_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;
    nav_probe_pins=nav_probe_inits=nav_probe_unmapped=nav_probe_first=nav_probe_frame_pins=nav_probe_frame_inits=0;nav_probe_tick=UINT64_MAX;
    nav_probe_armed=1;return 1;
}
/* Parameter deadline initialisation, DEFAULT OFF (native_gate_nav_params_init;
 * needs native_gate_nav_cadence). The parameter refresh deadline (ctrl+0xd0,
 * rewritten by vfunc33 after the parameter build 0xea1e80 only while ctrl+0xd8
 * is set) also keeps its loading-time value, and native_gate_nav_initial_refresh
 * re-bases it for fixture actors only. fly7a, the player's controller (through
 * native_gate_nav_player): 40208d6d.. (8.28 s) on client 1 and 4021510c..
 * (8.66 s) on client 2 at tick 1, so its parameter builds ran on other ticks
 * from the first one on (100 of 600 rows apart; fly7b parted at ticks 202 and
 * 226, other trace branches). Same rules as the probe init, before each
 * bracketed vfunc33 call (the nav wrapper and native_gate_nav_player):
 * - parameters off: the deadline is pinned to the frame's time - dt, so the
 *   first call with parameters on builds them at once;
 * - parameters on at the first call per portable ID (own insert-only table):
 *   the deadline is set the same way, once.
 * A fixture actor that native_gate_nav_initial_refresh re-bases gets the same
 * value. Unmapped ships are pinned while off and left alone while on.
 * Only loading-time deadlines are re-based: saved ships and ships created while
 * loading (created tick 0). A ship created in an admitted frame (created tick
 * >= 1) got its deadline from that frame's own scheduling and is left alone
 * (params_created=): big-BS6, 80 ships spawned at tick 15 had their first
 * parameter build forced at their first upkeep and 12 of them parted in motion
 * by the tick-60 digest with equal draw sets, event order and rows, while
 * big-BS6p without this key passed 600 ticks. */
static uint64_t nav_params_table[NAV_PROBE_TABLE];
static unsigned nav_params_count;
static int nav_params_armed;
static uint64_t nav_params_pins,nav_params_inits,nav_params_unmapped,nav_params_created,nav_params_first,nav_params_tick=UINT64_MAX,nav_params_frame_pins,nav_params_frame_inits;
static void nav_params_apply(const unsigned char *controller,double time) {
    unsigned char on=0;double due=0,now=time-gate_tick_dt();
    if(!isfinite(time) || !read_local(controller+0xd8,&on,1) || !read_local(controller+0xd0,&due,8)) return;
    if(nav_params_tick!=stats.issued) {nav_params_tick=stats.issued;nav_params_frame_pins=nav_params_frame_inits=0;}
    unsigned klass;uint64_t key=nav_controller_key(controller,&klass);
    if(key && klass==WORLD_KEY_CREATED && nav_created_tick(key)) {++nav_params_created;return;} /* created in an admitted frame */
    int first=key?nav_first_call(nav_params_table,&nav_params_count,key):0;
    if(first<0) {nav_cadence_stage="params_table";InterlockedIncrement64(&nav_cadence_failures);return;}
    if(!key) ++nav_params_unmapped;
    if(!on) {
        if(memcmp(&due,&now,8)) {memcpy((unsigned char*)controller+0xd0,&now,8);++nav_params_pins;++nav_params_frame_pins;}
        return;
    }
    if(first==1) {
        memcpy((unsigned char*)controller+0xd0,&now,8);++nav_params_inits;++nav_params_frame_inits;
        if(!nav_params_first) nav_params_first=stats.issued;
    }
}
/* Before the first admitted frame, after native_gate_nav_cadence. */
static int nav_params_arm(void) {
    if(nav_params_armed) return nav_cadence_installed;
    if(!nav_cadence_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;
    nav_params_pins=nav_params_inits=nav_params_unmapped=nav_params_created=nav_params_first=nav_params_frame_pins=nav_params_frame_inits=0;nav_params_tick=UINT64_MAX;
    nav_params_armed=1;return 1;
}
#ifndef NAV_CADENCE_OWN_PLATFORM
static void nav_cadence_enter(const void *controller,double time,nav_cadence_sample *c) {
    c->valid=0;
    if(!InterlockedCompareExchange(&nav_cadence_active,0,0)) return;
    if(nav_probe_armed) nav_probe_apply(controller,time);
    if(nav_params_armed) nav_params_apply(controller,time);
    c->time=time;
    if(!nav_cadence_read(controller,c)) {++nav_cadence_unreadable;return;}
    c->valid=1;
}
static void nav_cadence_class(nav_cadence_counts *f,nav_cadence_counts *t,unsigned block,double time,double after) {
    ++f->refresh[block];++t->refresh[block];
    double interval=after-time;
    if(interval<=0) {++f->now[block];++t->now[block];if(!nav_cadence_first_now) nav_cadence_first_now=stats.issued;}
    else if(interval<1.0) {++f->mid[block];++t->mid[block];if(!nav_cadence_first_mid) nav_cadence_first_mid=stats.issued;}
    else {++f->slow[block];++t->slow[block];}
}
/* Fixture state rows: every change of a fixture controller's refresh state
 * (enables and deadlines of both blocks), its ship's attention, the ship's main
 * physics body and the physics scene of its zone, after each vfunc33 call.
 * The probe block is the pathing Agent's avoidance block: PathingManager
 * registration 0xeac530 stores the Agent at controller+0x40 and Agent+0x6e0 at
 * +0x48, so the probe enable is Agent+0xac8. Agent::UpdateAvoidance 0xe91e60
 * (from the flight-behaviour orders and behaviours) sets it from the flight
 * behaviour (Agent+0xf10: vfunc +0x80 with flags +0xf5c/+0xf5d, or vfunc +0x88);
 * rows also carry that behaviour's vtable RVA and the two flags. */
#define NAV_CADENCE_RING 4096u
typedef struct {
    uint64_t tick;double params,probe;int attention;uint32_t behaviour;
    unsigned char params_on,probe_on,probe_present,body,zone_scene,avoid;
} nav_cadence_state;
#define NAV_CADENCE_WATCH 128u /* the world-hash watch list (64) plus native_gate_nav_cadence_watch */
static nav_cadence_state nav_cadence_last[ACTOR_CAPACITY],nav_cadence_watch_last[NAV_CADENCE_WATCH];
static unsigned char nav_cadence_seen[ACTOR_CAPACITY],nav_cadence_watch_seen[NAV_CADENCE_WATCH];
static uint64_t nav_cadence_watch_ids[NAV_CADENCE_WATCH]; /* saved IDs of watched non-fixture ships */
static unsigned nav_cadence_watch_count;
static struct {unsigned actor;uint64_t saved;nav_cadence_state state;} nav_cadence_ring[NAV_CADENCE_RING];
static volatile LONG64 nav_cadence_rows;
/* 0 none, 1 the Main scene (g_PhysicsSceneA 0x38f0748), 2 another scene, 3 unreadable. */
static unsigned char nav_cadence_scene_role(const void *scene) {
    void *main=NULL;if(!scene) return 0;
    if(!read_local(image+0x38f0748,&main,8)) return 3;
    return scene==main?1:2;
}
static void nav_cadence_fixture_state(const unsigned char *controller,const nav_cadence_sample *a,nav_cadence_state *out) {
    unsigned char *ship=NULL,*record=NULL,*scene=NULL,*p,*probe=NULL;
    memset(out,0,sizeof(*out));out->tick=stats.issued;out->params=a->params;out->probe=a->probe;
    out->params_on=a->params_on;out->probe_on=a->probe_on;out->attention=-1;
    out->probe_present=read_local(controller+0x48,&probe,8) && probe;
    /* Agent flight behaviour: vtable RVA (0 none, ffffffff unreadable or outside
     * the image); avoid = flag +0xf5c | +0xf5d << 1, 0xff unreadable. */
    unsigned char *agent=NULL,*behaviour=NULL,*vt=NULL,flags[2];
    out->behaviour=0xffffffffu;out->avoid=0xff;
    if(read_local(controller+0x40,&agent,8) && agent) {
        if(read_local(agent+0xf5c,flags,2)) out->avoid=(unsigned char)((flags[0]?1:0)|(flags[1]?2:0));
        if(read_local(agent+0xf10,&behaviour,8)) {
            if(!behaviour) out->behaviour=0;
            else if(read_local(behaviour,&vt,8) && vt>=image && vt<image+0x6fa9000) out->behaviour=(uint32_t)(vt-image);
        }
    }
    if(!read_local(controller+0x28,&ship,8) || !ship) {out->body=out->zone_scene=3;return;}
    if(!read_local(ship+0x1ec,&out->attention,4)) out->attention=-1;
    out->body=read_local(ship+0x260,&record,8)?(record?(read_local(record+0x18,&scene,8)?nav_cadence_scene_role(scene):3):0):3;
    /* The zone (class 0x6d) and its physics scene (Zone vfunc 943, +0x1d78; a pure getter). */
    out->zone_scene=3;p=ship;
    for(unsigned depth=0;p && depth<16;depth++) {
        void **vtable=NULL;int (*is_a)(void *,int)=NULL;void *(*get_scene)(void *)=NULL;
        if(!read_local(p,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) break;
        if(is_a(p,0x6d)) {
            if(read_local((unsigned char*)vtable+0x1d78,&get_scene,8) && get_scene) out->zone_scene=nav_cadence_scene_role(get_scene(p));
            break;
        }
        if(!read_local(p+0x70,&p,8)) break;
    }
}
static int nav_cadence_state_same(const nav_cadence_state *a,const nav_cadence_state *b) {
    return a->params_on==b->params_on && a->probe_on==b->probe_on && a->probe_present==b->probe_present &&
        !memcmp(&a->params,&b->params,8) && !memcmp(&a->probe,&b->probe,8) && a->attention==b->attention &&
        a->body==b->body && a->zone_scene==b->zone_scene && a->behaviour==b->behaviour && a->avoid==b->avoid;
}
static void nav_cadence_push(unsigned actor,uint64_t saved,const nav_cadence_state *now) {
    LONG64 index=InterlockedIncrement64(&nav_cadence_rows)-1;unsigned at=(unsigned)((uint64_t)index%NAV_CADENCE_RING);
    nav_cadence_ring[at].actor=actor;nav_cadence_ring[at].saved=saved;nav_cadence_ring[at].state=*now;
}
static void nav_cadence_fixture_row(const void *controller,const nav_cadence_sample *a,unsigned actor) {
    if(actor<1 || actor>ACTOR_CAPACITY) return;
    nav_cadence_state now;nav_cadence_fixture_state(controller,a,&now);
    if(nav_cadence_seen[actor-1] && nav_cadence_state_same(&now,&nav_cadence_last[actor-1])) return;
    nav_cadence_seen[actor-1]=1;nav_cadence_last[actor-1]=now;nav_cadence_push(actor,0,&now);
}
/* A watched non-fixture ship, by the saved ID of its controller's ship. */
static void nav_cadence_watch_row(const void *controller,const nav_cadence_sample *a) {
    unsigned char *ship=NULL;uint64_t runtime=0,saved;
    if(!nav_cadence_watch_count || !read_local((const unsigned char*)controller+0x28,&ship,8) || !ship ||
       !read_local(ship+8,&runtime,8) || !(saved=world_identity_lookup(runtime))) return;
    for(unsigned i=0;i<nav_cadence_watch_count;i++) if(nav_cadence_watch_ids[i]==saved) {
        nav_cadence_state now;nav_cadence_fixture_state(controller,a,&now);
        if(nav_cadence_watch_seen[i] && nav_cadence_state_same(&now,&nav_cadence_watch_last[i])) return;
        nav_cadence_watch_seen[i]=1;nav_cadence_watch_last[i]=now;nav_cadence_push(0,saved,&now);return;
    }
}
static int nav_cadence_watch_add(uint64_t saved) {
    if(!saved || nav_cadence_watch_count>=NAV_CADENCE_WATCH || GetCurrentThreadId()!=ui_thread) return 0;
    for(unsigned i=0;i<nav_cadence_watch_count;i++) if(nav_cadence_watch_ids[i]==saved) return 1;
    nav_cadence_watch_ids[nav_cadence_watch_count++]=saved;return 1;
}
static int nav_cadence_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&nav_cadence_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)NAV_CADENCE_RING && index<(uint64_t)(count-(LONG64)NAV_CADENCE_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const nav_cadence_state *r=&nav_cadence_ring[index%NAV_CADENCE_RING].state;uint64_t params,probe;
    memcpy(&params,&r->params,8);memcpy(&probe,&r->probe,8);
    n=snprintf(out,cap,"index=%llu tick=%llu id=%u saved=%016llx attention=%d params_on=%u params_due=%016llx probe=%u probe_on=%u probe_due=%016llx body=%u zone_scene=%u behaviour=%08x avoid=%u",
        (unsigned long long)index,(unsigned long long)r->tick,nav_cadence_ring[index%NAV_CADENCE_RING].actor,
        (unsigned long long)nav_cadence_ring[index%NAV_CADENCE_RING].saved,r->attention,r->params_on,
        (unsigned long long)params,r->probe_present,r->probe_on,(unsigned long long)probe,r->body,r->zone_scene,r->behaviour,r->avoid);
    return n>0 && (size_t)n<cap?n:0;
}
static void nav_cadence_leave(const void *controller,const nav_cadence_sample *c,unsigned other,unsigned actor) {
    if(!c->valid) return;
    nav_cadence_sample a;unsigned k=other?1u:0u;
    nav_cadence_roll();++nav_cadence_frame[k].calls;++nav_cadence_total[k].calls;
    if(!nav_cadence_read(controller,&a)) {++nav_cadence_unreadable;return;}
    /* A refresh rewrites its deadline; this frame's time is the base of the new one. */
    if(c->params_on && a.params!=c->params) nav_cadence_class(&nav_cadence_frame[k],&nav_cadence_total[k],0,c->time,a.params);
    if(c->probe_on && a.probe_on && a.probe!=c->probe) nav_cadence_class(&nav_cadence_frame[k],&nav_cadence_total[k],1,c->time,a.probe);
    if(!other) nav_cadence_fixture_row(controller,&a,actor);
    else nav_cadence_watch_row(controller,&a);
}
static int nav_cadence_owned_entry(void) {return nav_installed && *(void*volatile*)(image+NAV_ENTRY)==(void*)nav_move;}
static int nav_cadence_install_sites(int mode) {
    for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {
        uint16_t value=nav_cadence_value(i,mode);
        if(value==nav_cadence_sites[i].original) continue;
        if(!nav_cadence_swap(i,nav_cadence_sites[i].original,value)) {
            for(unsigned j=0;j<i;j++) nav_cadence_swap(j,nav_cadence_value(j,mode),nav_cadence_sites[j].original);
            return 0;
        }
    }
    return 1;
}
static int nav_cadence_arm(int mode) {
    if(nav_cadence_installed) return stats.active && mode==nav_cadence_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=NAV_CADENCE_FAR && mode!=NAV_CADENCE_EVERY && mode!=NAV_CADENCE_OBSERVE) || !stats.active || (boot_mode && boot_phase) ||
       stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!nav_cadence_owned_entry()) {nav_cadence_stage="nav_module";return 0;}
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll")) {nav_cadence_stage="foreign_module";return 0;}
    if(!nav_cadence_pins()) {nav_cadence_stage="pins";return 0;}
    if(!nav_cadence_sites_are(NAV_CADENCE_OBSERVE)) {nav_cadence_stage="site";return 0;}
    memset(nav_cadence_frame,0,sizeof(nav_cadence_frame));memset(nav_cadence_total,0,sizeof(nav_cadence_total));
    nav_cadence_tick=UINT64_MAX;nav_cadence_first_now=nav_cadence_first_mid=nav_cadence_unreadable=0;nav_cadence_restored=0;
    memset(nav_cadence_seen,0,sizeof(nav_cadence_seen));memset(nav_cadence_watch_seen,0,sizeof(nav_cadence_watch_seen));nav_cadence_rows=0;
    if(mode!=NAV_CADENCE_OBSERVE) {
        if(!nav_cadence_install_sites(mode)) {nav_cadence_stage="cas";return 0;}
        nav_cadence_patched=1;
    }
    nav_cadence_mode=mode;nav_cadence_installed=1;nav_cadence_stage="armed";
    InterlockedExchange(&nav_cadence_active,1);return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 95. */
static int nav_cadence_check(void) {
    if(InterlockedCompareExchange64(&nav_cadence_failures,0,0)) return 0;
    const char *why=GetModuleHandleW(L"x4lan_fixedtick_64.dll")?"foreign_module":
        !nav_cadence_sites_are(nav_cadence_patched?nav_cadence_mode:NAV_CADENCE_OBSERVE)?"site_changed":
        !nav_cadence_pin_ok(NAV_CADENCE_PIN_A,NAV_CADENCE_PIN_A_SIZE,NAV_CADENCE_PIN_A_SHA) ||
        !nav_cadence_pin_ok(NAV_CADENCE_PIN_B,NAV_CADENCE_PIN_B_SIZE,NAV_CADENCE_PIN_B_SHA)?"block_changed":
        !nav_cadence_owned_entry()?"nav_module":NULL;
    if(why) {nav_cadence_stage=why;InterlockedIncrement64(&nav_cadence_failures);return 0;}
    return 1;
}
static int nav_cadence_owned(void) {return !nav_cadence_patched || nav_cadence_sites_are(nav_cadence_mode);}
static void nav_cadence_restore(void) {
    InterlockedExchange(&nav_cadence_active,0);
    if(!nav_cadence_installed) return;
    if(nav_cadence_patched) {
        /* Restore every site still holding our value; any foreign site keeps the patch marked. */
        int all=1;
        for(unsigned i=0;i<NAV_CADENCE_SITES;i++) {
            uint16_t value=nav_cadence_value(i,nav_cadence_mode);
            if(value==nav_cadence_sites[i].original || nav_cadence_site(i)==nav_cadence_sites[i].original) continue;
            if(!nav_cadence_swap(i,value,nav_cadence_sites[i].original)) all=0;
        }
        if(!all) return;
        nav_cadence_patched=0;
    }
    nav_cadence_restored=1;nav_cadence_installed=0;
}
static int nav_cadence_counts_text(char *out,size_t capacity,const nav_cadence_counts *c) {
    return snprintf(out,capacity,"%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu",(unsigned long long)c->calls,
        (unsigned long long)c->refresh[0],(unsigned long long)c->now[0],(unsigned long long)c->mid[0],(unsigned long long)c->slow[0],
        (unsigned long long)c->refresh[1],(unsigned long long)c->now[1],(unsigned long long)c->mid[1],(unsigned long long)c->slow[1]);
}
static int nav_cadence_status(char *out,size_t capacity) {
    static const nav_cadence_counts zero;char groups[4][200];
    int fresh=nav_cadence_tick==stats.issued;
    nav_cadence_counts_text(groups[0],sizeof(groups[0]),fresh?&nav_cadence_frame[0]:&zero);
    nav_cadence_counts_text(groups[1],sizeof(groups[1]),fresh?&nav_cadence_frame[1]:&zero);
    nav_cadence_counts_text(groups[2],sizeof(groups[2]),&nav_cadence_total[0]);
    nav_cadence_counts_text(groups[3],sizeof(groups[3]),&nav_cadence_total[1]);
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d sites=%04x:%04x:%04x:%04x frame=%llu "
        "fixture=%s other=%s total_fixture=%s total_other=%s first_now=%llu first_mid=%llu unreadable=%llu restored=%d stage=%s failures=%lld "
        "probe_init=%d probe_frame=%llu/%llu probe_pins=%llu probe_inits=%llu probe_ids=%u probe_unmapped=%llu probe_first=%llu "
        "params_init=%d params_frame=%llu/%llu params_pins=%llu params_inits=%llu params_ids=%u params_unmapped=%llu params_first=%llu params_created=%llu",
        nav_cadence_mode==NAV_CADENCE_FAR?"far":nav_cadence_mode==NAV_CADENCE_EVERY?"every":nav_cadence_mode==NAV_CADENCE_OBSERVE?"observe":"off",
        nav_cadence_active,nav_cadence_installed,nav_cadence_site(0),nav_cadence_site(1),nav_cadence_site(2),nav_cadence_site(3),
        (unsigned long long)(nav_cadence_tick==UINT64_MAX?0:nav_cadence_tick),groups[0],groups[1],groups[2],groups[3],
        (unsigned long long)nav_cadence_first_now,(unsigned long long)nav_cadence_first_mid,(unsigned long long)nav_cadence_unreadable,
        nav_cadence_restored,nav_cadence_stage,(long long)nav_cadence_failures,nav_probe_armed,
        (unsigned long long)(nav_probe_tick==stats.issued?nav_probe_frame_pins:0),(unsigned long long)(nav_probe_tick==stats.issued?nav_probe_frame_inits:0),
        (unsigned long long)nav_probe_pins,(unsigned long long)nav_probe_inits,nav_probe_count,(unsigned long long)nav_probe_unmapped,
        (unsigned long long)nav_probe_first,nav_params_armed,
        (unsigned long long)(nav_params_tick==stats.issued?nav_params_frame_pins:0),(unsigned long long)(nav_params_tick==stats.issued?nav_params_frame_inits:0),
        (unsigned long long)nav_params_pins,(unsigned long long)nav_params_inits,nav_params_count,(unsigned long long)nav_params_unmapped,
        (unsigned long long)nav_params_first,(unsigned long long)nav_params_created);
    return n>0 && (size_t)n<capacity?n:0;
}
#endif
