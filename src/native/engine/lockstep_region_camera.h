/* Determinism fix, DEFAULT OFF: a simulation camera at the first latched
 * viewpoint for the camera-centred parts of the simulation, in three groups:
 *   region   native_gate_region_camera = 'view' (needs native_gate_view_points
 *            and native_gate_region_pass = 'sync')
 *   fields   native_gate_region_fields = 'view' (needs native_gate_view_points)
 *   traffic  native_gate_mass_traffic_camera = 'view' (needs
 *            native_gate_view_points and native_gate_mass_traffic = 'sync')
 *   effects  native_gate_effect_camera = 'view' (needs native_gate_view_points)
 *   effect_rows  native_gate_effect_rows = true (diagnostic, see below)
 *   highway  native_gate_highway_camera = 'view' (needs native_gate_view_points)
 *   scenes   native_gate_physics_scenes = 'viewpoints' (needs native_gate_view_points)
 *   context  native_gate_camera_context = 'view' (needs native_gate_view_points)
 *   policy   native_gate_viewpoint_policy = 'sector' (needs the traffic or the
 *            highway group); native_gate_viewpoint_standins = 2..4 view points answered
 *            for (default 2: view points 1 and 2)
 *   shape_refresh  native_gate_shape_refresh = 'view' (needs native_gate_view_points)
 *   mining   native_gate_mining_camera = 'view' (needs native_gate_view_points)
 *   xs_detail  native_gate_xs_camera = 'view' (needs native_gate_view_points)
 *   camera_probe  native_gate_camera_probe = true (diagnostic; needs native_gate_view_points)
 *   highway_rows  native_gate_highway_rows = ticks, _from = first tick (diagnostic; needs
 *            native_gate_view_points)
 *   highway_speed  native_gate_highway_speed = 'key' (needs native_gate_view_points and
 *            native_gate_world_identities)
 *   sound_rng  native_gate_sound_rng = 'split' (needs native_gate_view_points)
 *   view_scenes  native_gate_view_scenes = true (needs the scenes group; lockstep_view_scenes.h)
 *   field_fill  native_gate_field_fill = 'view' | 'block' (needs the fields group)
 *   traffic_views  native_gate_traffic_views = 'block' (needs native_gate_view_points)
 *   traffic_rows   native_gate_traffic_rows = true (diagnostic; lockstep_traffic_rows.h)
 *   traffic_pass   native_gate_traffic_views = 'observe' | 'view' (needs traffic_rows; lockstep_traffic_rows.h)
 *
 * region: U::Regions::RegionManager::Run 0xef30b0 -> 0xeea8a0 generates region
 * content (asteroid fields and the temp zones holding them) around the active
 * camera (the cutscene manager's camera [0x3d19900]+0xb0, else the player
 * camera g_Universe+0x3d0): the parent of its view bank (camera+0x30+i*0x2b0),
 * that parent's class-0x58 ancestor (0xcd350), its region holder (0x490040)
 * and the camera position from the bank's cached transforms (0x9fcc10 ->
 * 0x979ef0: entries {key, translation, three rows} of 0x50 bytes inline at
 * bank+0x40, capacity bank+0x2c0, count bank+0x2d0). cam-B6: only the
 * offset client drew (34 at tick 4) and created three temp zones; cam-B7 with
 * this group: region draws and centres equal on both clients.
 * fields: U::Region::Move 0x767410 (simulation thread) calls slot 10 of every
 * field of an active region. The base Field::vfunc10 0xedc540 removes field
 * objects (asteroids, debris) farther than the field's radius from the camera,
 * in the camera's zone (0x9fc950) at Camera::GetOffset 0x97a300 (anchor +0x770,
 * +0x778, mode +0x868, local PosRot +0x590..+0x5cf); EffectField::vfunc10
 * 0xee07c0 and VolumetricCloudField::vfunc10 0xee2740 spawn effects (U::Effect-
 * Object components) when the local player object g_Universe+0x230 is near
 * them. cam-B7: client 1 alone connected 15..32 unmapped EffectObjects per
 * effect tick from tick 15 (ConnectTo rows, level 9); the player object's
 * position follows the camera (view points 'ship' source).
 * traffic: mass traffic decides near the camera: 0x83ef20 (from Zone::Move
 * 0x834950, MassTrafficUpdateTask::Run 0x8c06d0, Zone::vfunc693 via 0x82f1e0)
 * tests a component against the active camera (bank parent, 0x3e35d0, the
 * camera's sector 0x9fcad0, 0x3e2ca0 -> GetOffset); 0x8ab1e0 (MassTraffic
 * thread, from 0x831440) places the camera (0x9f9eb0) in the player's zone
 * (g_Universe+0x230); 0x8be450 (Zone::Move) removes a zone's temp components
 * farther from the camera (0x3e3a60) unless the player camera is anchored to
 * them. cam-B7: the mass-traffic status still sampled the real camera.
 * effects: every EffectObject is created by 0xd717a0 (from SpawnEffect 0xd73e00
 * via 0xd73060, and from ActivateEffectEvent::Dispatch via EffectObjectCreator
 * slot 206). A short-lived, non-persistent effect (duration below a constant)
 * is not created when the active camera is farther from it than the effect's
 * visible distance (0x9fcb90 -> GetOffset, counters 0x6adfd28 attempts and
 * 0x6adfd2c culled). The culled ones never draw their placement RNG, never get
 * a component ID and never connect. cam-B8b (chain= rows): client 1 alone
 * created 9..32 EffectObjects per tick from tick 15 (weapon fire 0x8185b0 and
 * 0x8132d0, bullet impacts 0xd519e0, ActivateEffectEvent); long-L12 (equal
 * cameras) parts in the same rows from tick 31, since the camera's local
 * PosRot is rewritten every frame from game and real time.
 * effect_rows (diagnostic): cam-B9 with the effects group still parted at tick
 * 20: per weapon salvo the same effects were split differently between
 * immediate creation and an ActivateEffectEvent one tick later. That split is
 * made by 0xd73060 before 0xd717a0: an element whose shot time + delay
 * (+0x68) is still ahead of the thread's game time (TLS+0x300) is deferred.
 * The call 0xd74ac6 in SpawnEffect goes through a stub that records one row
 * per element (owner portable key from the spawn context +0x38, delay bits,
 * shot time bits, game time bits, deferred) and calls 0xd73060 unchanged, so
 * two clients' rows name the input that differs.
 * highway: cam-B15 (motion watch target rows): a214c's tick-194 flight model
 * target (Agent_UpdatePathing 0xe8f250 -> 0xe8c380 -> SetTarget) is its path
 * point (zone 0x72f, equal on both clients) expressed in highway 0x72a; only
 * its z differed in the last bits on the camera-moved client. A Highway's frame
 * carries its current spline parameter: Highway::GetLocalTransform 0x5f79f0
 * adds the spline position at it (+0x570, set with the parameter +0x560 by
 * 0x5f7a50), and the children's transforms go through the highway's slot
 * 0x1520/0x1530 spline maps, so the conversion's rounding depends on that
 * parameter. Highway::vfunc674 0x5f4a40 (Zone::Move of the highway, simulation
 * thread) sets it, for a highway in the player object's sector
 * (g_Universe+0x230, load 0x5f4a85) with attention 5..7, from the closest spline
 * point to the player camera: Camera::GetOffset 0x97a300 of g_Universe+0x3d0
 * (load 0x5f4d30) relative to the highway at 0x5f4d7a, so the camera 70 km off
 * moved the parameter on client 2. Both loads read the shadow universe (the
 * viewpoint and the stand-in camera at its origin). U::UniverseFrameUpdate
 * (slot 3) sets the parameter of a highway with attention >= 8 that holds the
 * camera anchor's controllable (0x9f9da0: the outermost class-0x75 object with
 * the anchor, through class-0x6f parents) from that controllable's position
 * (0x9fe3fd): each client's own ship in a session (the same ship in the camera
 * test). Its two calls of 0x9f9da0 (0x9fe2e0, 0x9fe337) go through stubs that
 * answer the same walk for the viewpoint while the stand-in is set, else call
 * the original (its other callers, HighwayScene and the render, stay native).
 * scenes: XPhys::CreateGlobalScenes 0x83f7d0 always creates physics scenes A
 * and B (and a third); U::UniverseFrameUpdate steps A and B every frame. A
 * zone's scene is U::Zone::vfunc943 0x83fe70 (U::Highway::vfunc943 0x5f9680
 * for highways): A when the player object (g_Universe+0x230; slot 1's ship on
 * every client) is in the zone's class-0x58 ancestor, B when a cutscene
 * camera ([0x3d19900]+0xb0, other than the player camera) is there (0x9fca10
 * / 0x9fc950 / 0x9f9eb0: the active camera's class-0x58 / class-0x6d
 * ancestor, the active camera), else none. Natively B is never used here, so
 * nothing near slot 2's ship has physics on any client. With this group the
 * three cutscene-manager loads of the two functions read a second stand-in
 * manager whose camera is a stand-in at viewpoint 2 (view point 1 of the
 * latched list, slot 2's ship) while it is latched, NULL otherwise, and their
 * five active-camera calls (0x83ff84, 0x5f975e, 0x5f97fa, 0x5f9809, 0x5f9923)
 * answer for viewpoint 2 (its class-0x58 / class-0x6d ancestors, the stand-in)
 * or call the original: scene A follows slot 1, scene B slot 2, on every
 * client. Rendering and the real cutscene manager stay native, so a
 * presentation camera on a client does not change any scene.
 * context: U::Object::SetParent 0x6cdab0 tests whether the re-parented object
 * is the player camera's anchor or one of the anchor's ancestors (g_Universe
 * +0x3d0, +0x770, walked up +0x70; load 0x6ce5cc) and if so calls 0x97dff0
 * (0x6ce60d: camera, old parent, new parent), which posts CameraLeftEvent and
 * CameraEnteredEvent immediately (not through the schedule) to the old and new
 * parents' ancestors. The handlers re-evaluate attention (Ship slots 366/367
 * also start and stop the ship's sounds, Zone/Highway 0x83a3d0 also its
 * class-6 children, Sector/Space/Galaxy 0x6b8540 by tail call, Cluster
 * 0x3ba8b0). cam-C1c: with client 2's camera anchored at slot 2's ship, that
 * ship's start warp at tick 22 (WarpEvent, re-parented into sector001) posted
 * them on client 2 alone; 12 objects there started their attention timers at
 * tick 22 on client 2 and at tick 23 (the next view point latch) on client 1,
 * and the world parted from tick 22. With this group the load reads the shadow
 * universe, so the test walks up from viewpoint 1 (slot 1's ship, the player
 * object) on every client, as client 1's own camera does, and the call goes
 * through a stub that hands 0x97dff0 the live player camera when the test read
 * a stand-in (0x97dff0 compares it with the cutscene manager's cameras and the
 * player camera and puts its +0x874 into the events); no live camera: no call
 * (counted). Native camera changes (view switches through 0x97de40) still post
 * per client; native_gate_view_triggers = 'block' keeps their attention out.
 * policy: the traffic and highway groups answer for view point 1 everywhere, so
 * nothing near slot 2's ship (in another sector) gets camera-centred mass
 * traffic or highway parameters on any client. With the policy each camera
 * read of those groups that has its object at hand answers for the view point
 * in the object's sector: view point 2 when the object's class-0x58 ancestor
 * (Sector; 0x6d is Zone, 0xf Cluster, 0x57 any Space) is view point 2's and
 * view point 1's is another, else view point 1, identically on every client:
 *  - the ten loads of the traffic and highway groups (0x83ef20 component r12;
 *    0x8ab1e0 traffic object r12 through its zone +0x108, else +0x100;
 *    0x8be450 component rdi; Highway::vfunc674 highway rdi) become
 *    call rel32 + 66 90 into a thunk that saves rax..r11, flags and
 *    xmm0..xmm5, calls rc_pick(object, site) and returns the chosen slot's
 *    value (the shadow universe or manager of view point 1, or the second
 *    shadow universe / manager at view point 2) in the load's register;
 *  - the camera-cluster call 0x83f2ff and the active-camera call 0x8ab40a get
 *    the object through their stubs (mov r8, r12 / mov rcx, r12);
 *  - UniverseFrameUpdate's highway parameter (one controllable per frame,
 *    0x9fe2e0/0x9fe337) alternates by tick: odd admitted ticks view point 2
 *    when it is in another sector, else view point 1 (each highway that holds
 *    a slot ship is set every other frame).
 * In one sector both view points answer as view point 1 (one parameter per
 * highway, one camera per traffic test). Two stand-ins only, for any client count:
 * view point 2 is the first view point in slot order in another sector than view
 * point 1's, else the second, so 2..N ships in one other sector are covered by
 * whichever comes first. A view point in a third sector gets no scene (no physics
 * near it), no camera-centred traffic and no highway parameter, the same on every
 * client: status uncovered=ticks/now counts it. Asteroid fields stay on view point
 * 1: U::Regions::RegionManager keeps a single holder (the camera's sector,
 * 0xeea8a0 -> 0x490040), a changed holder unloads the regions (0xeeb530),
 * and region content exists only in that holder's sector, so alternating
 * would unload and regenerate both sectors' fields every tick; a second
 * sector's fields need a second RegionManager. Field::vfunc10 removes only
 * that content; EffectField/VolumetricCloudField (player object near) the same.
 * Policy rows (native_gate_viewpoint_policy_rows = ticks, diagnostic): one row
 * per policy decision while the tick is within the limit: tick, site (the
 * rc_sites index, 0x100 + call index for the camera-cluster/active-camera
 * stubs), the choice, the object's portable key and runtime ID (a mass
 * traffic object: its zone's) and the thread (mover=1 MoveWorker, 2 main
 * thread, 3 any other: the mass traffic pass, a job or render thread). cam-C1f:
 * picks= parted by 3/3 at tick 93 (client 2 one extra 0x8ab1e0 and one extra
 * 0x83ef20 camera test, all view point 2) and MASS_TRAFFIC's first spawn
 * draws came at tick 93 on client 2 and 96 on client 1, with different
 * schedules after; the rows name the object and site where the two clients'
 * mass traffic near view point 2 first differ. Status: site_picks= (per policy
 * site, table order).
 * cam-C1g (rows): only zone 0x4d4 (zone003_cluster_01_sector001, view point
 * 2's) differed: its traffic object's 0.2 s update (0x8ab1e0's camera step,
 * last update +0x248, first one at activation: +0x248 starts at -1.0) ran at
 * ticks 88, 100, 112 on client 2 and 89, 101, 113 on client 1, so the zone's
 * traffic was activated one mass traffic pass earlier on client 2. A zone's
 * traffic is activated by the pass when 0x83ef20(zone, 1) finds it within
 * 150 km of the camera (0x82ed50 work item, 0x8aacb0 sets +0x1d0). That test
 * (0x83f26f -> 0x3e35d0) and the lane camera (0x8ab419 -> 0x979ef0) take the
 * positions from the published component view caches (0x3dde10 /
 * 0x979ef0: g_ComponentViewCacheIndex off the MoveWorker), the render's
 * double-buffered copies, not the simulation's transforms. Suspected: for
 * view point 2's sector, which no client renders, the cached transform of the
 * moving ship is not the same frame's on both clients, so the 150 km crossing
 * lands one pass apart. With the policy the two calls go through stubs: for
 * view point 2 they compute from the simulation's transforms at the pass's
 * game time (U::Component::GetTransformRelativeTo 0x3db8a0, TLS+0x300; the
 * sync pass runs while the MoveWorker is parked): the zone test as the zone's
 * minus view point 2's position in their common cluster (only the length is
 * used), the lane camera as view point 2's transform in the frame zone (the
 * stand-in sits at its origin). View point 1 keeps the original calls. With
 * policy rows both log their result (site 0x103 zone test, 0x104 lane camera,
 * pos= the vector) for every view point.
 * cam-C1g3: the zone test's vectors now agree on both clients (key 4d4 at tick
 * 100: 68625.5,-29999.9,-37723.7, about 84 km), yet zone 4d4's traffic object
 * was switched on (its first 0x8ab1e0 camera step) at tick 88 on client 1 and
 * 90 on client 2. The switch-on: per pass, for a zone within range,
 * 0x82ed50(network, 0) takes the first inactive traffic object of the network
 * (+0x40, else the list +0x48, count +0x370) and calls 0x8aacb0, which builds
 * its lane network (0x8ad220) and sets +0x1d0 when that succeeds; a failed
 * build that leaves +0x228 set moves the object to the end of the list. The
 * build starts from the zone's objects that have cached waypoints (0x8afaf0:
 * ComponentFilterByHasCachedWaypoints -> object slot 670), and a station's
 * waypoints come from its loaded model. Suspected: those are ready at a
 * wall-clock time (asynchronous asset loading, like the nav octrees of
 * native_gate_nav_octree), so the build succeeds one or two passes apart.
 * Activation rows (policy rows): site 0x105 the pass's call 0x82ee9e, 0x106
 * and 0x107 the traffic update's two (0x831a54, 0x831ab9), each with a= the
 * object's +0x1d0 before and b= after (and +0x228 as pend=); site 0x108 the
 * build's object collection (0x8ad25d -> 0x8afaf0) with a= the number of
 * objects with cached waypoints it found. The key is the traffic object's
 * zone.
 * shape_refresh: st3-P15L/P15B (3 clients, clients 2 and 3 anchored in other
 * sectors): fighters near client 1's camera took collision damage on client 1
 * alone, motion equal everywhere; the physics trace (scene A: the same 37
 * bodies, BodyIDs, masses and velocities) parts on the compound child poses
 * (shape, tick 2, client 1 alone) and Jolt's shape-changed flag (flags). Each
 * step XPhys::Scene::Step 0xba600 calls Body_UpdateShape 0xbff80 per body,
 * which calls 0xc2c00(holder = record+0x20, owner, force): once the holder's
 * due time (+0x28) has passed (or for the player's own chain) it moves every
 * compound child to its part's current pose (NotifyShapeChanged) and sets the
 * next due time to now + 0xc2810(holder, owner). That interval reads the
 * published component view caches (render-side, current only where a client
 * renders): the player object's and the body's class-0x58 ancestors through
 * the cached parents (another sector: 5 s), and the body's position relative
 * to the player's class-0x49 ancestor (0x9fc7b0 -> 0x3dde10); then
 * clamp((distance - size) / size, 0.5, 5) - Random::FloatState(TLS+0x318, 0.1)
 * with size the compound's bounds diagonal. Where clients render differently
 * the children move at different steps and the thread RNG draws differ. With
 * this group:
 *  - the interval call (0xc2dac) goes through a stub to the same rule on the
 *    simulation: +0x70 parents, GetTransformRelativeTo at the thread's game
 *    time in the common sector, measured from the nearest view point latched
 *    in the body's sector (rc_frame; no view point: the player object's
 *    class-0x49 ancestor), exact square roots; the macro flag (0x7ad890, +0x278:
 *    0 s) and the bounds (shape slot 3) stay native, the draw (native FloatState)
 *    takes a word keyed by the tick and the object's portable key instead of the
 *    frame's: sc-h103b's word differs between clients by one draw on 46 of 1800
 *    ticks after the last script row, and in sc-dw108a this is the only group with
 *    the draws to account for that, 4626 of 13411 frame-level draws, while a draw
 *    of the frame's word makes one refresh's jitter every later consumer's in the
 *    frame and each due time depends on what drew before it in the step;
 *  - Body_UpdateShape's call (0xbffb7) goes through a stub that, at the first
 *    call per object key (saved or created) after the install, sets the due
 *    time to the thread's game time, so the due times set while loading (per
 *    client) give way to one refresh at the same step on every client.
 * Status: shape_refresh= the two stubs' calls, aligned=, unmapped= (no key: the
 * due time stays), near= / far= intervals, shape_native= (before the first
 * latch). The key table is insert-only (RC_SHAPE_KEYS); full stops the gate
 * (reason 130).
 * mining: AI mining's region object search 0x7884a0 (from
 * Scripts::FindRegionObjectWithYieldInSectorAction and 0x7880c0) returns
 * nothing when the region's class-0xf (Cluster) ancestor is the player
 * object's (found through the player's cached view-cache parents) and the
 * cutscene camera, else the player camera, is within 50 km (2.5e9 squared) of
 * the requested position (the camera's bank transform in the caller's frame,
 * 0x979ef0): so each client's camera picked the AI miners' asteroids. With
 * this group the block from the player load (0x78854a) to the answer is a
 * jump to a thunk (rc_jump_stub_bytes) that calls rc_mining_near(cluster rdi,
 * position r13, frame rsi) and resumes at 0x788720 in the state of the
 * native no-reference path (bl the answer, r12d 2, r14d 0x17 also in the
 * frame's +0x1e8, [rsp+0x60] 0): near when a view point latched in that
 * cluster (rc_frame, its +0x70 parents) is within 50 km, from
 * GetTransformRelativeTo at the thread's game time in the cluster, the
 * position taken in the frame (translation and rows); a frame outside the
 * cluster answers not near (counted foreign). The same group routes the
 * Crystal pass (0x73f3e0, from Crystal::vfunc9): for an object of the sector
 * whose crystal is due it respawns the crystal (0x9082c0: kill, reason 0xd,
 * then Random::U64 picks the replacement from a list and spawns it at the same
 * spot) only when the object is more than 3 km from the camera (0x73f6a3 ->
 * 0x3e3a60, squared distance against 9e6): each client's camera decided which
 * crystals respawned, and drew the thread RNG. Its distance is now the squared
 * distance to the nearest view point latched in the object's sector (none:
 * 3e38), so a crystal respawns only out of every player's sight; the call is
 * counted by thread with the respawns allowed (status crystal=). Stop reason
 * 166.
 * field_fill: the RegionManager keeps generated content as records by cell (store
 * DAT+0x60: entries {+0x10 tree field -> list of 0xa0-byte nodes, record at +0x10,
 * consumed flag +0x90; +0x8a0 tree of dirty fields; +0x8e0 the cell's zone, 0x785db0 at
 * the cell's centre from 0xee7290; +0x8f0 lock}), drawn as instances. They become real
 * objects in the render pass: Zone::vfunc693 0x8429a0, per view, zone attention >= 8 ->
 * 0xeeb650 tests each node of an active field against the view; a node within the
 * field's radius (the table Field::vfunc10 removes by) and the view's camera being
 * g_Universe+0x3d0 goes into the field's pending tree (+0x50, under +0x20, keyed by the
 * cell's zone), and Field::vfunc10 calls slot 21 for the pending ones in the next
 * Region::Move. So each client's real camera decided which field objects exist.
 * reg-tour: slot 1 warped to cluster_31_sector001 at tick 150; client 1's camera renders
 * it, client 2's stays in Argon Prime, and at tick 156 client 1 alone made 46 U::Asteroid
 * (0xedee30 -> 0xedfaf0 -> the factory; REGION_RNG draws 232 against 84, inside
 * Region::Move; next= 138 apart with their shapes), then the pool (0xedfaf0 keeps removed
 * ones, parent 0) and everything after diverged. Any player near a field does this.
 * The site's universe load (0xeec6f0) reads the shadow universe, whose camera is the
 * stand-in, so the render pass never inserts. 'view' fills instead at rc_frame (slot 6,
 * MoveWorker) from view point 1, the holder of the region content and the centre
 * Field::vfunc10 removes around, entries in store order: the dirty fields refreshed
 * (0xee6a80, which sets the nodes' ware and yield the AI's search reads; the render
 * pass's call now does nothing, so no client's render pass does it first), then with the
 * same zone tests (not class 0x33, attention >= 8) and the cell's zone active, each
 * field's list in order: a node within the radius is inserted as natively and freed at
 * once (natively it stays flagged until the client's next render pass, and the AI's
 * search does not look at the flag). The tree walks fields by pointer, so a field's
 * decision may not depend on another's: a field takes nodes only while its pending tree
 * is empty, one cell per pass, because Field::vfunc10 walks pending by zone pointer and
 * two cells' nodes would spawn in a per-process order. The refresh walks its dirty fields
 * by pointer too and 0x78a690 draws the thread word when several resource areas fit, so
 * that call runs on a word keyed by the node's position. A node outside its cell's box is
 * left to slot 21, which finds or makes its zone. 'block' only closes the render pass's
 * insert (no field objects from views at all), to tell the two apart in a run. View
 * points 2..N make no field objects and none are removed for them: one RegionManager,
 * see below.
 * traffic_views: mass traffic becomes real ships in the render pass. Zone::vfunc693 0x8429a0
 * (per view of each rendered zone) calls 0x82f1e0(network, zone, view) for a zone with a traffic
 * network (+0x4f0); for every traffic object that calls 0x8aba40(object, view, player view, time),
 * which, when the view's camera (+0x90) is the player camera (0x82f224: the cutscene manager's
 * +0xb0, else 0x82f23c: g_Universe+0x3d0), puts each member within the distance setting
 * (DAT_146cf1538+0x5358) of the view, inside its lane window, into the object's pending tree
 * (+0x290, 0x91eec0); U::Zone::Move's temp-component update 0x8be450 then makes it a real ship
 * (0x8bfb40 -> MassTrafficBehavior::vfunc20, ship, weapons, engines, AI script). So each client's
 * camera decided which mass traffic ships exist. reg-tour4 (caller rows every tick): the first
 * difference at 208, one ship (15 component rows, all through 0x8be450 -> 0x8bfb40) on client 1
 * only, whose camera follows slot 1 through the tour while client 2's renders slot 2's sector;
 * the policy rows show the render pass's zone tests (0x83ef20 sites, every 60 ticks) on the
 * zones each client renders. With this group both loads read the stand-ins: the view's camera is
 * never the stand-in camera, so no view makes a mass traffic ship real; the rest of the render
 * update (instances, the collision filters of ships already real, 0x8c0180) is unchanged.
 * Ships the simulation asks for (0x8bd190 from Zone::Move) still become real. Cost: a mass
 * traffic ship near a player stays mass traffic (no target, no collision), on every client.
 * Residual: 0x8c0180's filter updates for real traffic ships still run only in rendered zones.
 * xs_detail: ShipExtraSmall::vfunc876 0x7e5310 (WantsPhysicsBody's slot 876
 * for XS ships, attention above 8) keeps a body beyond 2 km of the player
 * when IsExternalViewActive (0x7e5412 -> 0x9fc0e0) and the ship is within
 * 2 km of the camera (0x7e541e -> GetCachedDistanceSqToCamera 0x3e40a0): so
 * XS ships near each client's camera had bodies on that client alone. With
 * this group the external view answers yes and the distance is the squared
 * distance to the nearest view point latched in the ship's sector
 * (simulation, as above; none: 3e38): every XS ship within 2 km of a player's
 * ship keeps its body on every client. In a stock session (the virtual
 * players' per-slot view, lockstep_pilot_stock_view.h: vpl_slot_count > 0) the
 * three camera-side inputs follow the slots' replayed records instead: the
 * soft target call (0x7e5346 -> 0x9fcd70, the ship moved into rcx by its stub)
 * answers the ship when it is some slot's soft target (portable IDs; a part of
 * the ship as target does not match), the external view answers whether any
 * slot is in external view, and the distance is the squared distance to the
 * nearest such slot's ship in the ship's sector. The player terms (0x3e1d00,
 * 0x55aeb0) stay the canonical player's. Stop reason 167.
 * sound_rng: sc-dw108a's frame-level draws include eight XSfx stacks: engines
 * (U::Engine::vfunc1025 -> 0x3e7ee0), attention (U::Positional::vfunc868), effect
 * creators (0xd798a8), speech, Lua::PlaySound. All pass 0x14e8ec0 -> 0x14eed60 (its
 * only caller), which draws at two calls, both presentation (pitch, sample choice):
 * FloatState on TLS+0x318 at 0x14eefdf and 0x14f2550 (Random::I32 inside) at 0x14eeee5.
 * Whether a sound starts is the client's: local input, sound voices, the UI. st-C2Ra:
 * keyed ship scopes draw 20 at a time on one client only at ticks that differ between
 * clients (client 2 at 128, 218, 233, 248; client 1 at 263, 268, 273, 283), the static
 * candidate being PlayerController::vfunc33's manual-flight branch into this path; a
 * draw there moves every later draw of that scope, one at frame level every later
 * consumer of the frame. Both calls go through stubs: the float call draws from a
 * private word, the choice runs with TLS+0x318 holding one and puts the caller's back,
 * also when an exception unwinds. The words are seeded from a call counter, never the
 * simulation's, so a sound consumes no simulation draw whichever client plays it. It
 * covers what native_gate_ship_sound_rng covers (slot 1143) and everything else.
 * Status: sound=float calls/choice calls.
 * camera_probe (diagnostic): pass-through stubs that count, by thread
 * (MoveWorker, main, other), the camera reads of the highway scene's anchor
 * walks (0x9f9da0 from HighwayScene
 * 674/684, HighwaySceneController and HighwayShipController 6/7, 0x5ec543,
 * 0x5ef2c0, 0x5f1110), HighwayBooster::vfunc779's two camera vectors
 * (0x5ec3d1 -> 0x3e35d0, 0x5ec414 -> 0x3e3d80) and Zone::vfunc921's camera
 * offset (0x8408a0 -> GetOffset). Two clients' counts name which of them run
 * in the simulation and depend on the camera. st3-P15M: only Zone::vfunc921
 * ran (MoveWorker, 981 calls on client 1, 6 on clients 2 and 3); it is the
 * HUD's line-of-sight test (callers UI::ICom::VisibilityData::IsObstructed and
 * Scene::vfunc921; camera offset, GetTransformRelativeTo and ray queries 0x8401f0
 * -> 0xbc4d0, no draw), cached per object for the HUD only: presentation.
 * Stop reason 168.
 * highway_rows (diagnostic): sc-highway87a (slot 1's ship, the only view point,
 * in highway02 0x72e of cluster_14_sector001 from tick 1093 to 1694; highway
 * group on) parted in one object: the Argon frigate 0xa214c (Patrol order,
 * NavigationController), equal at 900 and different at 1200, while it rode
 * the sector's other highway (highway01 0x72a, ticks 1154 to 1654) at
 * attention 7. In full-G2, full-G8 and sc-missions70a (same snapshot) it rides
 * highway01 from 1154 to 1607 at attention 5 and stays equal. It is not the
 * highway scene's traffic: HighwayShipController and HighwaySceneController
 * (the camera probe's walks) move the HighwayScene and the effect objects the
 * effect movement factory 0xd64820 (EffectObject creation 0xd717a0, 0xd708b0)
 * and the macro movement factory 0xd67970 build; per-tick walk and stub counts
 * were identical on both clients, whose cameras follow the same ship. A ship in
 * a highway moves in the highway's frame, whose spline parameter (+0x560;
 * +0x570 the spline position at it) 0x5f7a50 sets from two places:
 * Highway::vfunc674 for a highway with attention 5..7 in the player object's
 * sector, from the spline point closest to the camera's GetOffset relative to
 * the highway (0x5f4d7a -> 0x97a300; with the highway group the stand-in at
 * view point 1), call 0x5f4fee; UniverseFrameUpdate for the highway with
 * attention >= 8 that holds the camera anchor's controllable, from that
 * controllable's controller (0x9fe3fd). The census does not cover the player
 * ship's own state (its controller is not a NavigationController: flags 8), so
 * a difference of the view point's position would first show in a covered ship
 * moving in such a frame. With this group the three calls go through stubs that
 * call the originals unchanged and, for ticks from .. from+ticks-1, record one
 * row each: tick, site (offset / move / frame), thread class, the highway's
 * portable key and attention, its parameter before the call (bits), and the
 * GetOffset translation (x, y, z, w bits) or the new parameter and the spline
 * position it set (+0x570..+0x578, bits). Two clients' rows name the first
 * input that differs. Stop reason 169. sc-highway94a: all 4075 rows identical
 * on both clients, so the parameter and its inputs are not the difference.
 * highway_speed: sc-highway94a's motion watch of 0xa214c parts at tick 1154,
 * when the ship enters highway01 at attention 7 and gets a
 * HighwayFlightControlModel (0xe87cf0 -> vfunc8 0xeb07a0): model +0x80 is
 * 0.00844616 on client 1 and 0.0294471 on client 2, with target, reference and
 * path equal; the pose's z and z velocity part from 1155. +0x80 is an AI ship's
 * tube speed jitter: the ship's runtime component ID (+8) through one PCG step
 * (id * 0x5851f42d4c957f2d + 0x14057b7ef767814f, ror 30, low word * 2^-32)
 * times the tube's aifactor speed (0.05 for local highways), and runtime IDs
 * differ between processes (IDs 0x5bcd0 and 0x5bcd9 reproduce both values to
 * the float). With this group the jump at 0xeb0a2c (the multiplier's load)
 * replaces that ID with the ship's portable key before the same PCG step
 * (rc_hw_speed_seed); a ship without one (its creation frame) seeds 0, on every
 * client. A model built while loading keeps its native seed until the ship's
 * next model change (snapshot 005 has no ship in a highway at tick 1). The
 * player branch (+0x80 = 1) and the acceleration factor stay native. Status:
 * highway_speed= keyed/unmapped. A failed check stops the gate with 130.
 * sc-highway97a (keyed +0x80, equal on both clients) still parts at 1155: x and
 * x velocity. The flight step vfunc14 0xeb0b70 seeds two more draws from the same
 * ID: the lateral wobble (0xeb0f19: FloatState on a local state set to the ID, for
 * period and phase) and the bump's sign (0xeb1092: ID as a double times the bump's
 * start time, one PCG step). The same group replaces both with the key.
 *
 * Mechanism: a private page near the image holds two pointer slots, a stand-in
 * cutscene manager (+0xb0 = stand-in camera) and a shadow universe (+0x230 =
 * viewpoint, +0x3d0 = stand-in camera). The listed mov r64,[rip+d32] loads of
 * 0x3d19900 read the manager slot, the listed loads of g_Universe (only used
 * for +0x230/+0x3d0 there) read the universe slot, and three e8 calls (0x9fc950
 * camera zone, 0x9fcad0 camera sector, 0x9f9eb0 active camera; their other
 * callers, Highway::vfunc943 and LookAtPlayerController, stay native) go
 * through jmp stubs on an executable page to replacements that answer for the
 * stand-in and otherwise call the original. The stand-in camera: both view
 * banks parented to the viewpoint with one cached identity entry, anchor and
 * +0x778 the viewpoint, mode 1 (anchor-relative), identity local PosRot, so
 * every reader sees a camera at the viewpoint's origin. Two stand-ins alternate;
 * the manager and universe pointers are swapped in slot 6 of every admitted
 * frame (rc_frame) while the synced passes are parked and the simulation is not
 * running. Without a viewpoint the manager's camera is NULL, the shadow universe
 * holds the live player object and player camera, and the replacements call
 * the originals (counted, native_frames=). One viewpoint (the native code has
 * one camera): with several players these follow pilot slot 1.
 * Arming (before the first admitted frame; needs the pass join, i.e.
 * native_gate_mass_traffic or native_gate_region_pass) checks the pins
 * (SHA-256 with our displacements read as the originals) and allocates the
 * pages. The sites are written at the gate tick after the first rc_frame, right
 * after the synced passes are joined (rc_join_point, main thread): the
 * MoveWorker is parked in its frame-start wait and no armed group's pass runs,
 * so nothing executes the patched code. They are written for every armed group
 * at once by CAS of each aligned 16-byte block a displacement touches (several
 * straddle two blocks). The pass kicked by the last loading frame is not joined
 * before the first admitted frame (cam-B8: busy at the first frame), so if a
 * pass is still running the install waits for a later gate tick (counted,
 * install_waits=, at most RC_INSTALL_WAITS ticks, then stop) and install_tick=
 * names the frame. Every gate tick re-checks sites, slots and pins (stop
 * reason 130). Stop withdraws the stand-in at once and restores the sites if no
 * pass runs, else reports not restored. Pages are never freed. */
#define RC_CUTSCENE 0x3d19900u
#define RC_UNIVERSE 0x3d198f8u
#define RC_PAGE_SIZE 0xa000u
#define RC_STUB_SIZE 0x2000u
#define RC_STOP_REASON 130u /* 101 is the prediction module's */
#define RC_MANAGER 0x100u   /* stand-in cutscene manager on the page; +0xb0 its camera */
#define RC_SHADOW 0x400u    /* shadow universe; +0x230 the viewpoint, +0x3d0 the stand-in camera */
#define RC_CAMERAS 0x1000u  /* two stand-in cameras, RC_CAMERA_SIZE apart */
#define RC_MANAGER2 0x200u  /* second stand-in manager (scenes); +0xb0 the viewpoint-2 camera */
#define RC_CAMERAS2 0x3000u /* two stand-in cameras at viewpoint 2 */
#define RC_CAMERA_SIZE 0x1000u
#define RC_SHADOW2 0x800u   /* second shadow universe; +0x230 viewpoint 2, +0x3d0 its stand-in camera */
/* Stand-ins by index k: 0 view point 1, 1 view point 2, 2 and 3 the first view point of a third and a fourth sector
 * (native_gate_viewpoint_standins): their manager (+0xb0 the camera), shadow universe (+0x230, +0x3d0) and camera pair. */
_Static_assert(VIEW_MAX==4u,"one stand-in per view point");
static const unsigned rc_k_manager[VIEW_MAX]={RC_MANAGER,RC_MANAGER2,0x300u,0x5000u},rc_k_shadow[VIEW_MAX]={RC_SHADOW,RC_SHADOW2,0xc00u,0x5400u},
    rc_k_cameras[VIEW_MAX]={RC_CAMERAS,RC_CAMERAS2,0x6000u,0x8000u};
#define RC_THUNKS 0x700u    /* policy thunks on the stub page, RC_THUNK_SIZE apart */
#define RC_JUMP_STUBS 0xd80u /* jump thunks (rc_jumps) on the stub page, 0x80 apart */
#define RC_MINING_STOP 166u
#define RC_XS_STOP 167u
#define RC_PROBE_STOP 168u
#define RC_HIGHWAY_ROWS_STOP 169u
#define RC_THUNK_SIZE 0xa0u
#define RC_BANK 0x2b0u
#define RC_INSTALL_WAITS 600u
enum {RC_OFF,RC_VIEW};
enum {RC_REGION,RC_FIELDS,RC_TRAFFIC,RC_EFFECTS,RC_EFFECT_ROWS,RC_HIGHWAY,RC_SCENES,RC_CONTEXT,RC_POLICY,RC_SHAPE_REFRESH,
      RC_MINING,RC_XS,RC_PROBE,RC_HIGHWAY_ROWS,RC_HIGHWAY_SPEED,RC_SOUND,RC_VIEW_SCENES,RC_FIELD_FILL,RC_TRAFFIC_VIEWS,RC_TRAFFIC_ROWS,RC_TRAFFIC_PASS,RC_GROUPS};
/* The object a policy site's thunk reads: none, r12 (component), r12 (mass traffic object: its zone), rdi (component). */
enum {RC_OBJ_NONE,RC_OBJ_R12,RC_OBJ_R12_TRAFFIC,RC_OBJ_RDI};
enum {RC_SLOT_MANAGER,RC_SLOT_UNIVERSE,RC_SLOT_MANAGER2};
#define RC_SITES 25u
#define RC_CALLS 54u
#define RC_FILL_AREA_CALL 42u      /* 0xee6d32 in the refresh 0xee6a80: 0x78a690, a node's resource area */
#define RC_FILL_REFRESH_CALL 43u   /* 0xeeb98a in the render pass 0xeeb650: the refresh 0xee6a80 */
#define RC_VIEW_STEP_CALL 41u     /* 0x9fe48b in UniverseFrameUpdate: Scene::Step of B */
#define RC_SOUND_CALL 39u         /* 39 0x14eefdf FloatState, 40 0x14eeee5 the choice 0x14f2550, both in 0x14eed60 */
#define RC_HWROW_CALL 36u         /* 36 GetOffset 0x5f4d7a in Highway::vfunc674, 37 its 0x5f7a50 at 0x5f4fee, 38 UniverseFrameUpdate's at 0x9fe3fd */
#define RC_HWROW_RING 65536u
#define RC_XS_TARGET_CALL 35u     /* 0x7e5346 in ShipExtraSmall::vfunc876: the soft target 0x9fcd70 (mov rcx, rsi: the ship) */
#define RC_XS_EXTERNAL_CALL 20u   /* 0x7e5412 in ShipExtraSmall::vfunc876: IsExternalViewActive 0x9fc0e0 */
#define RC_XS_DISTANCE_CALL 21u   /* 0x7e541e: GetCachedDistanceSqToCamera 0x3e40a0(ship) */
#define RC_PROBE_CALL 22u         /* 22 the Crystal pass (group mining), 23..31 highway scene anchor walks, 32/33 booster, 34 Zone::vfunc921 */
#define RC_PROBE_SITES 13u
#define RC_JUMPS 6u
#define RC_SHAPE_UPDATE_CALL 18u   /* 0xbffb7 in Body_UpdateShape 0xbff80: 0xc2c00(holder, owner, force) */
#define RC_SHAPE_INTERVAL_CALL 19u /* 0xc2dac in 0xc2c00: 0xc2810(holder, owner), the next refresh's interval */
#define RC_ACTIVATE_CALL 14u    /* 0x82ee9e, 0x831a54, 0x831ab9: 0x8aacb0 (switch a traffic object on); then 0x8ad25d */
#define RC_COLLECT_CALL 17u     /* 0x8ad25d in 0x8ad220: 0x8afaf0, the zone's objects with cached waypoints */
#define RC_ZONE_TEST_CALL 12u   /* 0x83f26f in 0x83ef20: 0x3e35d0, component vs camera (view caches) */
#define RC_LANE_CAMERA_CALL 13u /* 0x8ab419 in 0x8ab1e0: 0x979ef0, the camera in the frame zone (view caches) */
#define RC_PINS 95u
#define RC_THUNK_SITES 10u
#define RC_CONTEXT_CALL 11u /* index of SetParent's 0x97dff0 call in rc_calls */
#define RC_EFFECT_ROW_RING 65536u
#define RC_EFFECT_ATTEMPTS 0x6adfd28u /* EffectObject creations tried (0xd717a0) */
#define RC_EFFECT_CULLS 0x6adfd2cu    /* of which culled by the camera distance */
typedef struct {unsigned rva;unsigned char prefix[3],slot,group,object;} rc_site_t;
static const rc_site_t rc_sites[RC_SITES]={
    {0xeea964u,{0x4c,0x8b,0x0d},RC_SLOT_MANAGER,RC_REGION,RC_OBJ_NONE},
    {0xeea9cbu,{0x4c,0x8b,0x0d},RC_SLOT_MANAGER,RC_REGION,RC_OBJ_NONE},
    {0x9fcc20u,{0x48,0x8b,0x0d},RC_SLOT_MANAGER,RC_REGION,RC_OBJ_NONE},
    {0xedc5ebu,{0x48,0x8b,0x0d},RC_SLOT_MANAGER,RC_FIELDS,RC_OBJ_NONE},
    {0xee086au,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_FIELDS,RC_OBJ_NONE},
    {0xee0939u,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_FIELDS,RC_OBJ_NONE},
    {0xee28e5u,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_FIELDS,RC_OBJ_NONE},
    {0x83f00au,{0x48,0x8b,0x05},RC_SLOT_MANAGER,RC_TRAFFIC,RC_OBJ_R12},
    {0x83f23du,{0x48,0x8b,0x05},RC_SLOT_MANAGER,RC_TRAFFIC,RC_OBJ_R12},
    {0x83f35bu,{0x4c,0x8b,0x05},RC_SLOT_MANAGER,RC_TRAFFIC,RC_OBJ_R12},
    {0x8ab2a7u,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_TRAFFIC,RC_OBJ_R12_TRAFFIC},
    {0x8ab359u,{0x48,0x8b,0x05},RC_SLOT_MANAGER,RC_TRAFFIC,RC_OBJ_R12_TRAFFIC},
    {0x8be7aeu,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_TRAFFIC,RC_OBJ_RDI},
    {0x8be7dfu,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_TRAFFIC,RC_OBJ_RDI},
    {0x8be85bu,{0x48,0x8b,0x15},RC_SLOT_MANAGER,RC_TRAFFIC,RC_OBJ_RDI},
    {0x9fcba0u,{0x48,0x8b,0x0d},RC_SLOT_MANAGER,RC_EFFECTS,RC_OBJ_NONE},
    {0x5f4a85u,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_HIGHWAY,RC_OBJ_RDI},
    {0x5f4d30u,{0x48,0x8b,0x0d},RC_SLOT_UNIVERSE,RC_HIGHWAY,RC_OBJ_RDI},
    {0x83ff38u,{0x48,0x8b,0x0d},RC_SLOT_MANAGER2,RC_SCENES,RC_OBJ_NONE},
    {0x5f9711u,{0x48,0x8b,0x0d},RC_SLOT_MANAGER2,RC_SCENES,RC_OBJ_NONE},
    {0x5f98d8u,{0x48,0x8b,0x0d},RC_SLOT_MANAGER2,RC_SCENES,RC_OBJ_NONE},
    {0x6ce5ccu,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_CONTEXT,RC_OBJ_NONE},
    {0xeec6f0u,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_FIELD_FILL,RC_OBJ_NONE},
    {0x82f224u,{0x48,0x8b,0x05},RC_SLOT_MANAGER,RC_TRAFFIC_VIEWS,RC_OBJ_NONE},
    {0x82f23cu,{0x48,0x8b,0x05},RC_SLOT_UNIVERSE,RC_TRAFFIC_VIEWS,RC_OBJ_NONE}};
typedef struct {unsigned rva,target;unsigned char group,move[3];} rc_call_t;
/* e8 call sites (rva of the e8) and their original targets; stub i replaces call i.
 * move: three bytes the stub runs before its jump (the policy's object into a
 * free volatile register: mov r8, r12 / mov rcx, r12), none when zero. */
static const rc_call_t rc_calls[RC_CALLS]={{0xedc57au,0x9fc950u,RC_FIELDS,{0,0,0}},{0x83f2ffu,0x9fcad0u,RC_TRAFFIC,{0x4d,0x89,0xe0}},
    {0x8ab40au,0x9f9eb0u,RC_TRAFFIC,{0x4c,0x89,0xe1}},
    {0xd74ac6u,0xd73060u,RC_EFFECT_ROWS,{0,0,0}},{0x9fe2e0u,0x9f9da0u,RC_HIGHWAY,{0,0,0}},{0x9fe337u,0x9f9da0u,RC_HIGHWAY,{0,0,0}},
    {0x83ff84u,0x9fca10u,RC_SCENES,{0,0,0}},{0x5f975eu,0x9fc950u,RC_SCENES,{0,0,0}},{0x5f97fau,0x9f9eb0u,RC_SCENES,{0,0,0}},
    {0x5f9809u,0x9fca10u,RC_SCENES,{0,0,0}},{0x5f9923u,0x9fca10u,RC_SCENES,{0,0,0}},{0x6ce60du,0x97dff0u,RC_CONTEXT,{0,0,0}},
    {0x83f26fu,0x3e35d0u,RC_POLICY,{0,0,0}},{0x8ab419u,0x979ef0u,RC_POLICY,{0,0,0}},
    {0x82ee9eu,0x8aacb0u,RC_POLICY,{0,0,0}},{0x831a54u,0x8aacb0u,RC_POLICY,{0,0,0}},{0x831ab9u,0x8aacb0u,RC_POLICY,{0,0,0}},
    {0x8ad25du,0x8afaf0u,RC_POLICY,{0,0,0}},{0xbffb7u,0xc2c00u,RC_SHAPE_REFRESH,{0,0,0}},{0xc2dacu,0xc2810u,RC_SHAPE_REFRESH,{0,0,0}},
    {0x7e5412u,0x9fc0e0u,RC_XS,{0,0,0}},{0x7e541eu,0x3e40a0u,RC_XS,{0,0,0}},
    {0x73f6a3u,0x3e3a60u,RC_MINING,{0,0,0}},
    {0x5ec5ccu,0x9f9da0u,RC_PROBE,{0,0,0}},{0x5edf47u,0x9f9da0u,RC_PROBE,{0,0,0}},{0x5ee8ceu,0x9f9da0u,RC_PROBE,{0,0,0}},
    {0x5ef31du,0x9f9da0u,RC_PROBE,{0,0,0}},{0x5f155fu,0x9f9da0u,RC_PROBE,{0,0,0}},{0xda0a6du,0x9f9da0u,RC_PROBE,{0,0,0}},
    {0xda0cf6u,0x9f9da0u,RC_PROBE,{0,0,0}},{0xda0ff1u,0x9f9da0u,RC_PROBE,{0,0,0}},{0xda161bu,0x9f9da0u,RC_PROBE,{0,0,0}},
    {0x5ec3d1u,0x3e35d0u,RC_PROBE,{0,0,0}},{0x5ec414u,0x3e3d80u,RC_PROBE,{0,0,0}},{0x8408a0u,0x97a300u,RC_PROBE,{0,0,0}},
    {0x7e5346u,0x9fcd70u,RC_XS,{0x48,0x89,0xf1}},
    {0x5f4d7au,0x97a300u,RC_HIGHWAY_ROWS,{0,0,0}},{0x5f4feeu,0x5f7a50u,RC_HIGHWAY_ROWS,{0,0,0}},{0x9fe3fdu,0x5f7a50u,RC_HIGHWAY_ROWS,{0,0,0}},
    {0x14eefdfu,0x14ddc90u,RC_SOUND,{0,0,0}},{0x14eeee5u,0x14f2550u,RC_SOUND,{0,0,0}},
    {0x9fe48bu,0xba600u,RC_VIEW_SCENES,{0,0,0}},
    {0xee6d32u,0x78a690u,RC_FIELD_FILL,{0,0,0}},{0xeeb98au,0xee6a80u,RC_FIELD_FILL,{0,0,0}},
    {0x834c95u,0x82d680u,RC_TRAFFIC_ROWS,{0,0,0}}, /* Zone::Move: rdx is the zone already */
    {0x834ca9u,0x831390u,RC_TRAFFIC_ROWS,{0x4c,0x89,0xf2}}, /* Zone::Move: mov rdx, r14 (the zone) */
    {0x8ac5afu,0x91eec0u,RC_TRAFFIC_ROWS,{0,0,0}},
    {0x8abbb4u,0x3dde10u,RC_TRAFFIC_PASS,{0,0,0}},{0x8ac0dau,0x3dde10u,RC_TRAFFIC_PASS,{0,0,0}},{0x8ac6a3u,0x3dde10u,RC_TRAFFIC_PASS,{0,0,0}},
    {0x8ac354u,0x6e59b0u,RC_TRAFFIC_PASS,{0,0,0}},{0x8ac8fcu,0x6e59b0u,RC_TRAFFIC_PASS,{0,0,0}},
    {0x8ac642u,0x8c0180u,RC_TRAFFIC_PASS,{0,0,0}},{0x8acc2cu,0xfb36d0u,RC_TRAFFIC_PASS,{0,0,0}}};
/* Jumps (7-byte instructions replaced by jmp rel32 + 66 90 into a thunk that resumes at a later address). */
typedef struct {unsigned rva,resume;unsigned char original[7],group;} rc_jump_t;
static const rc_jump_t rc_jumps[RC_JUMPS]={
    {0x78854au,0x788720u,{0x48,0x8b,0x05,0xa7,0x13,0x59,0x03},RC_MINING}, /* mov rax,[rip -> g_Universe] in 0x7884a0 */
    {0xeb0a2cu,0xeb0a36u,{0x48,0xb8,0x2d,0x7f,0x95,0x4c,0x2d},RC_HIGHWAY_SPEED}, /* movabs rax,PCG multiplier in HighwayFlightControlModel::vfunc8; its imm64 ends at the resume */
    {0xeb0f19u,0xeb0f22u,{0x48,0x8b,0x48,0x08,0x48,0x89,0x4c},RC_HIGHWAY_SPEED}, /* vfunc14: mov rcx,[rax+8]; mov [rsp+0x38],rcx (the wobble's state) */
    {0xeb1092u,0xeb1099u,{0x48,0x8b,0x51,0x08,0x48,0x85,0xd2},RC_HIGHWAY_SPEED}, /* vfunc14: mov rdx,[rcx+8]; test rdx,rdx (the bump's seed) */
    {0x8abf2du,0x8abf34u,{0xff,0x50,0x48,0x84,0xc0,0x75,0x12},RC_TRAFFIC_ROWS}, /* 0x8aba40: call [rax+0x48]; test al,al; jne 0x8abf46 */
    {0x8abd6fu,0x8abd76u,{0x41,0x0f,0x28,0xd4,0x48,0x8b,0xd6},RC_TRAFFIC_PASS}}; /* 0x8aba40: movaps xmm2,xmm12; mov rdx,rsi (the hook's args) */
static const struct {unsigned rva,size;unsigned char group;const char *sha;} rc_pins[RC_PINS]={
    {0xeea8a0u,0x458u,RC_REGION,"a21ec75c7a77b3202a3e8e43885de755ae239eb90cee363e97c9f495ddc47d4a"},
    {0x9fcc10u,0x80u,RC_REGION,"5a7c74eb919c300afe6fb1455c89d109549804af962026b85c257f3629f4d601"},
    {0x979ef0u,0x264u,RC_REGION,"81de8348fb8fc02d3c34c7a8a35b225a0c29b0a1b2ac668597f57da80ecd88ca"},
    {0xedc540u,0x455u,RC_FIELDS,"34e4832e204a847f65610a507bc6ce8250353de23e40770b0c35f076ed58d8f1"},
    {0xee07c0u,0x280u,RC_FIELDS,"2f82a6e01930a2db0f0cc70ca8d33286b7b5b7551ef23b8213cda3ef5f8cb244"},
    {0xee2740u,0x220u,RC_FIELDS,"b247636b6a142754604f1e3dc360ad27172e9797e9221e9363fb31c57bceb455"},
    {0x97a300u,0x3dcu,RC_FIELDS,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},
    {0x9fc950u,0xb8u,RC_FIELDS,"2def123cc68b8d9f1a3480e6f830671c358bb879355a873255f19de9035a551a"},
    {0x979ef0u,0x264u,RC_FIELDS,"81de8348fb8fc02d3c34c7a8a35b225a0c29b0a1b2ac668597f57da80ecd88ca"},
    {0x83ef20u,0x4b0u,RC_TRAFFIC,"60cad76de849689c8c1cc54b5f8768af178359bab3244b06590cc8db2cdcd4cc"},
    {0x8ab1e0u,0x240u,RC_TRAFFIC,"245598f2ae57a46151b0923a7aa9e06d4bd166f343b87e591e8b50e8dbd98f94"},
    {0x8be780u,0x140u,RC_TRAFFIC,"389b2e30f1efde9e317ef419c52d4c0afa6621694f654caabb61bce2a1a1a4f7"},
    {0x9fcad0u,0xb8u,RC_TRAFFIC,"205f225d403c1f77cc346be368fb230495a37ddfef28395eacd65f24ecc0dbd7"},
    {0x9f9eb0u,0x2du,RC_TRAFFIC,"6f53146b4806047eede8246538dbf147b7fc55a686178b4aa415e0a20299fe0b"},
    {0x3e3a60u,0x2fau,RC_TRAFFIC,"a9c34424ac5ea770397bbc020ef6827f94601886819abe9d7b91aa5070f419fe"},
    {0x3e2ca0u,0x172u,RC_TRAFFIC,"3839d98565b648c49dbdd4d7a85b1d4a7fc3ed9b4520668d97f03c7cb85d3ca5"},
    {0x3e35d0u,0x17du,RC_TRAFFIC,"efba2a969047bbb9c79cb9c45b49db1bae2e45e8a2c413cd835eb04b93927087"},
    {0x97a300u,0x3dcu,RC_TRAFFIC,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},
    {0x979ef0u,0x264u,RC_TRAFFIC,"81de8348fb8fc02d3c34c7a8a35b225a0c29b0a1b2ac668597f57da80ecd88ca"},
    {0x9fcb90u,0x80u,RC_EFFECTS,"e0732e11627447b08a4a04d3685c43021e2e9ea46c2c43ad4e224d43ae85e73f"},
    {0xd72370u,0xf0u,RC_EFFECTS,"7eb648e1190d2199a7c674d9bf8be466bf4b5bfdf78157b3aa407bac6a78344a"},
    {0x97a300u,0x3dcu,RC_EFFECTS,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},
    {0xd74a62u,0x6eu,RC_EFFECT_ROWS,"9fe08a4b063b13010791612d3b4fb5fe657f441b070e602a1841edc586a3f496"},
    {0xd73060u,0xb0u,RC_EFFECT_ROWS,"08300420ff6118795721ed295170e9850dfd0075be3c4ecc8e6028f3d2c1927f"},
    {0x5f4a40u,0x64bu,RC_HIGHWAY,"e7f21cbee34e028483e8f2c1b6c252b28c84b81ce35bf561aabf318ad663e03b"},
    {0x97a300u,0x3dcu,RC_HIGHWAY,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},
    {0x9f9da0u,0x10bu,RC_HIGHWAY,"ed4a7c81450163592f2afd81d87ac3735f1da93b476f23a91baf0e1b53c04023"},
    {0x9fe2dcu,0x126u,RC_HIGHWAY,"8a7555494e8a6a041d969850f40b087a6e2a8d8e88edab42aef2dae5b0dd32c2"},
    {0x83fe70u,0x134u,RC_SCENES,"35042feff4f61e35faab624017af0cf53ccf9eecd4352e0f0ec9ada2342fef0e"},
    {0x5f9680u,0x2cbu,RC_SCENES,"91ce15004a86fefc09156f1ae102e65f8bc57fe5b7a89cdeeecdee97f946b8f8"},
    {0x9fca10u,0xb8u,RC_SCENES,"7fcfee40eaef867110020157dee3cc570b64966a0da60085cc3215e1bc3e6ccc"},
    {0x9fc950u,0xb8u,RC_SCENES,"2def123cc68b8d9f1a3480e6f830671c358bb879355a873255f19de9035a551a"},
    {0x9f9eb0u,0x2du,RC_SCENES,"6f53146b4806047eede8246538dbf147b7fc55a686178b4aa415e0a20299fe0b"},
    {0x6ce5c4u,0x4eu,RC_CONTEXT,"9fc611239cfac8086f39a6d8315bb9cf73fa5514a72ebe1621476a04d07e2bc4"},
    {0x97dff0u,0x2bu,RC_CONTEXT,"6062cf6c43fb0d1d97c3d751976657d863b83ab9fa29b5492fd4cbe143fe60be"},
    {0x97e3ecu,0x47u,RC_CONTEXT,"a1d173836350e752ba89dca44faa526d973b15724127ce8ad30259fde7198660"},
    {0x8be450u,0x330u,RC_POLICY,"840a76f7a89e7ec2f840707c0252561cb9cbef5fbb4b4fa9fbfb6dbe5c4fb171"},
    {0x83ef20u,0x4b0u,RC_POLICY,"60cad76de849689c8c1cc54b5f8768af178359bab3244b06590cc8db2cdcd4cc"},
    {0x8ab1e0u,0x240u,RC_POLICY,"245598f2ae57a46151b0923a7aa9e06d4bd166f343b87e591e8b50e8dbd98f94"},
    {0x3db8a0u,0x40u,RC_POLICY,"bb8585368571f53e76ee392e85d1477845539c1e2db1ad1abf3a37c8c4dec521"},
    {0x82ed50u,0x3b3u,RC_POLICY,"1679bb67caf72e2560802f2607e21792eee8154b51f5207a5f0c9769ba54fc88"},
    {0x831440u,0x6edu,RC_POLICY,"3a08a078266a106b0bc20a1dc18cd71204e31f138c40ede795183ffb1388c0ef"},
    {0x8ad220u,0x100u,RC_POLICY,"b060f65c035fa5188672d2348a8bbfc7d7fc748263e463ac6dc27980a92ec2d9"},
    /* Body_UpdateShape from +8 (lockstep_shape.h may own its first eight bytes), 0xc2c00, the interval 0xc2810. */
    {0xbff88u,0x82u,RC_SHAPE_REFRESH,"abe31c3b4bd459895d03b0cda8d9c375c29852a56a3fe55169be72cd487737fb"},
    {0xc2c00u,0x388u,RC_SHAPE_REFRESH,"ce34f4bbe700f06364ec545aaa2fe9e7be198c751ad0ed0e06239ecf0b56cdf4"},
    {0xc2810u,0x3e4u,RC_SHAPE_REFRESH,"af541073310dc1a7ac2e0d773dd87c041c76021abbda46e9b6309e1ef4d4aee9"},
    {0x7884a0u,0x652u,RC_MINING,"177df54ffb9cbd5b623db8471df40ad59ebc502752dc95c2b8671ee9b8418c88"},
    {0x7e5310u,0x140u,RC_XS,"9b4b87442556ffbdebc25bd8a273edf967966441b661cd551abb987ffb6a3d52"},
    {0x73f3e0u,0x34bu,RC_MINING,"e431da3ed148b419a6b4b7d5abc5c3d673f61f127168ef9162f10ebd042d873e"},
    {0x5ec370u,0x12du,RC_PROBE,"09cdfca58e31fcd5bf451a0f99baefd3368ca08395acc999b9da0428085674ec"},
    {0x5ec5bcu,0x20u,RC_PROBE,"e0c13d39e894645d0fe6464eb9f67f6d405d5f49b3dee981de3f581f3ec12106"},
    {0x5edf37u,0x20u,RC_PROBE,"1fb71ad5524535981c74939e33591ccc9f76099142b6b24d64f0aee3a99181a8"},
    {0x5ee8beu,0x20u,RC_PROBE,"836d32ea8cafe977950d4c1185f7948b50147555e0cb100f5996b4b5f72673c3"},
    {0x5ef30du,0x20u,RC_PROBE,"a56a9f7618ca61938dcfe1ddade6a02abd9f8c2bc069e40c977ff81c9f70a696"},
    {0x5f154fu,0x20u,RC_PROBE,"bf692598f540c6be6842931c4fa70dcb69f502c6bed744bce9d10f5ff46d50c3"},
    {0xda0a5du,0x20u,RC_PROBE,"0de18910a848407d8cec25d4369e89b9bb7c81477879adb0e5e22a2f59b3e78b"},
    {0xda0ce6u,0x20u,RC_PROBE,"b3275f876c8ade2cbeb3855df65bd45c77434f3f80a6780b6008aca41c5dc836"},
    {0xda0fe1u,0x20u,RC_PROBE,"55bd9e852f3e51b480df49d6d9466ff5ec380a2c95f95237cd65c0c759a57eaa"},
    {0xda160bu,0x20u,RC_PROBE,"80342dd70425f28a8551eaca0d4ea0a4f7ec94ba211a4fc7d7b14a21ab686096"},
    {0x840890u,0x20u,RC_PROBE,"47a16fafa4ee1868ac97ec9cdbb55aee383eec52335588977a7f05c18a451bb6"},
    /* highway_rows: Highway::vfunc674, UniverseFrameUpdate's highway block, the parameter setter 0x5f7a50, GetOffset. */
    {0x5f4a40u,0x64bu,RC_HIGHWAY_ROWS,"e7f21cbee34e028483e8f2c1b6c252b28c84b81ce35bf561aabf318ad663e03b"},
    {0x9fe2dcu,0x126u,RC_HIGHWAY_ROWS,"8a7555494e8a6a041d969850f40b087a6e2a8d8e88edab42aef2dae5b0dd32c2"},
    {0x5f7a50u,0x492u,RC_HIGHWAY_ROWS,"a2fa1dfed033a87f578ac2a037ee185eb3ebcaabb492e259b764830c271d366b"},
    {0x97a300u,0x3dcu,RC_HIGHWAY_ROWS,"f494c5a2d068a5be0bdf2897a6def0ad99b378c609919e47a7fdc8452374950d"},
    {0xeb07a0u,0x369u,RC_HIGHWAY_SPEED,"70bec78ee3fdb3a21234a7a99e8bfdb2a699be2ccae1ecc6b988a44a5be06d07"},
    {0xeb0ec5u,0x24bu,RC_HIGHWAY_SPEED,"2d7e4cbbbd902a220954750a498d97ff4a1e14089d0636f21bbe7417444d10f7"},
    /* sound_rng: around the two calls in 0x14eed60, the choice, FloatState. */
    {0x14eefc0u,0x40u,RC_SOUND,"2dc7b0b4b84eb692f448e2652373a31cb94b51b1b5bd0662733187e389f05862"},
    {0x14eeed0u,0x30u,RC_SOUND,"6bfee37e287f7db63fb8add192362b7e1ccb1cd46895c76d1e93ead3e9bf4ef5"},
    {0x14f2550u,0x1fbu,RC_SOUND,"c1b248238316a228ce0baac9df613a6b4da43ef9bd4b196a0bb37c026c92c954"},
    /* view_scenes: the scene constructor, the anchor call, B's step. */
    {0xba090u,0x134u,RC_VIEW_SCENES,"eadb7181c78d1e585fba5a779e4b52232389dbb76f144e1f11079fb7e35925de"},
    {0xbd410u,0x6aau,RC_VIEW_SCENES,"91da8ccfc069e2f2abea63c27479494f0b3a4b3f6401f7817cba8005dbf65e54"},
    {0x9fe480u,0x20u,RC_VIEW_SCENES,"fef2667c24ebedc2aa16a74bf208af697eecb9bd0083774824537f9b9c6898c2"},
    {0x14ddc90u,0x78u,RC_SOUND,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e"},
    /* field_fill: the render pass's node walk and insert, what the fill calls instead of it. */
    {0xeeb980u,0x6f0u,RC_FIELD_FILL,"3f550be6f495c5d159f1ffcf11cdac17968c26fa646663b26b4749cb6b2f2ea4"},
    {0xeec6c0u,0x370u,RC_FIELD_FILL,"1dea69ae0af5f952ac61627fadde24fd35b5b593afa613b770c947844bdfc615"},
    {0xee5b30u,0x379u,RC_FIELD_FILL,"940a0522f41e4d5795213b593b8a6e0db6a5a1ff37ac0a3210a32b25dd2580fd"},
    {0xee6a80u,0x40fu,RC_FIELD_FILL,"e1bfd71f2e5f2d756ee21408f20bfaa093e4e5a03b566107afa3be4412e786e6"},
    {0xf33dd0u,0x1b9u,RC_FIELD_FILL,"1ebc0197cb78d2806cf182ec8cb67ce174e39f0fa084b51388e95690462e171d"},
    {0x6976e0u,0x104u,RC_FIELD_FILL,"f326624236ab286a58b56b5ee02b59b6aadcd548dcbbbca12e6e2d64c6bbddc8"},
    {0xf35460u,0x96u,RC_FIELD_FILL,"227b819fdb9c5fdda5ebd3d334f9a30a546c1424f7b1409a5b633321e5443247"},
    {0x174f310u,0x5u,RC_FIELD_FILL,"2bfe6841644386f7ccbe96cec53800ac8bd03bbe22208861b894f64db857be9c"},
    {0x14b7100u,0x3cu,RC_FIELD_FILL,"9d79eff4f0e84350a5db2150c3fa0eab68667aa53fc70ee563ad9b109ac4c525"},
    {0x78a690u,0x369u,RC_FIELD_FILL,"37786baac77fe4e75ebfc0e1668a59264e11f8c6207a895be701927a6a25f457"},
    /* traffic_views: the render pass's traffic call, both camera loads and the compare with the view's camera. */
    {0x82f1e0u,0x1a2u,RC_TRAFFIC_VIEWS,"1d3450cf1a6750b86c4f949c7d85aa425121076422c6065521ebc06ccf56a90f"},
    /* traffic_rows: 0x8aba40 around the exact-reciprocal window at 0x8ac501, Zone::Move's two calls, their targets, the queue. */
    {0x8aba40u,0x700u,RC_TRAFFIC_ROWS,"121569ed9b1be54a3c25a88056ea5ef97e4913319e2ad826d0d06821cc93fc2a"},
    {0x8ac140u,0x3c1u,RC_TRAFFIC_ROWS,"248ebfaa1a8a04e42d095e9197ff79136a9775555664cc4b955fc545acecf001"},
    {0x8ac509u,0x700u,RC_TRAFFIC_ROWS,"9de0deb337652abf5902bd731fbe97f99fbb41a13f079dc6e3880a953821e6a9"},
    {0x8acc09u,0x133u,RC_TRAFFIC_ROWS,"5d11eae47fbb3ad9db4fcb4b652901479acbbd51e50dd94853c5eac9ffacd88d"},
    {0x834c74u,0x60u,RC_TRAFFIC_ROWS,"fc9951a39a21bb76a2227956b8c5fc94f0640e42d9db54919d711cf7db89655b"},
    {0x82d680u,0x2b0u,RC_TRAFFIC_ROWS,"c4e6e023dcf8479d75d5b572530fda82eb421f13c05a30908892ae15040dd2b2"},
    {0x831390u,0xa8u,RC_TRAFFIC_ROWS,"4e1fb13fcf7f398527a26caa065ba3393fa13c40a4501a84ac1a1f7ff32a7ccb"},
    {0x91eec0u,0xcfu,RC_TRAFFIC_ROWS,"c09ef02e17ec292adb060e81536377c6d6a087e805e4ffcf34bb750e024d0214"},
    /* traffic_pass: the traffic call the pass makes, the placement and the filters it runs or skips. */
    {0x82f1e0u,0x1a2u,RC_TRAFFIC_PASS,"1d3450cf1a6750b86c4f949c7d85aa425121076422c6065521ebc06ccf56a90f"},
    {0x6e59b0u,0x15au,RC_TRAFFIC_PASS,"c939d2af7fa79df13b7ef99aae95117081999b25b915363e830acd6aed8b7875"},
    {0x8c0180u,0x2cfu,RC_TRAFFIC_PASS,"a53207497e887ccba90982cec7470a1a2fc91a27721ed4442af314728a7e6568"}};
static const char *const rc_group_names[RC_GROUPS]={"region","fields","traffic","effects","effect_rows","highway","scenes","context","policy",
                                                     "shape_refresh","mining","xs_detail","camera_probe","highway_rows","highway_speed","sound_rng","view_scenes","field_fill","traffic_views","traffic_rows","traffic_pass"};
static int rc_mode,rc_installed,rc_restored,rc_patched;
static unsigned rc_armed_groups,rc_installed_groups;
static unsigned char *rc_page,*rc_stubs;
static unsigned char rc_site_done[RC_SITES],rc_call_done[RC_CALLS],rc_jump_done[RC_JUMPS];
static unsigned rc_stop=RC_STOP_REASON; /* the stop reason of the last failure: 130, or a group's own */
static unsigned rc_next; /* the stand-in camera written by the next update */
static unsigned rc_next2; /* the same for viewpoint 2 */
static void *volatile rc_vp2_zone,*volatile rc_vp2_sector; /* viewpoint 2's class-0x6d and class-0x58 ancestors */
static uint64_t rc_vp2_id,rc_vp2_key,rc_vp2_updates,rc_uncovered_ticks;
static unsigned rc_uncovered_now; /* latched view points in neither stand-in's sector */
static void *volatile rc_view_zone,*volatile rc_view_sector; /* the viewpoint's class-0x6d and class-0xf ancestors (itself included) */
static void *volatile rc_view_sector58,*volatile rc_vp2_cluster; /* viewpoint 1's class-0x58 (Sector), viewpoint 2's class-0xf (Cluster) */
static int rc_policy; /* native_gate_viewpoint_policy armed: the traffic/highway sites take the thunk form */
static volatile LONG64 rc_picks,rc_picks_two,rc_alternations;
static volatile LONG64 rc_site_picks[RC_SITES];
#define RC_PICK_ROWS 65536u
typedef struct {uint64_t tick,key,runtime;uint32_t site,a,b,pend;unsigned char two,mover,has_pos,has_ab;float pos[3];} rc_pick_row_t;
static rc_pick_row_t rc_pick_ring[RC_PICK_ROWS];
static volatile LONG64 rc_pick_rows;
static unsigned rc_pick_rows_ticks;
static uint64_t rc_updates,rc_native_frames,rc_viewpoint_id,rc_viewpoint_key,rc_tick=UINT64_MAX,rc_install_tick,rc_install_waits;
static volatile LONG64 rc_stub_calls[RC_CALLS];
static volatile LONG64 rc_context_skipped; /* SetParent's context call with a stand-in and no live camera */
typedef void (*rc_context_fn)(void *,void *,void *);
static rc_context_fn rc_context_native; /* tests only: replaces image+0x97dff0 */
static volatile LONG64 rc_failures;
static const char *rc_stage="idle";
typedef char (*rc_is_class_fn)(void *,int);
static int rc_fail(const char *why) {rc_stage=why;InterlockedIncrement64(&rc_failures);return 0;}
static int32_t rc_disp_to(unsigned site,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+site+7));}
static int32_t rc_rel_to(unsigned call,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+call+5));}
static int rc_reach(intptr_t d) {return d>=INT32_MIN && d<=INT32_MAX;}
static unsigned rc_site_target(unsigned i) {return rc_sites[i].slot==RC_SLOT_UNIVERSE?RC_UNIVERSE:RC_CUTSCENE;}
static unsigned char *rc_slot(unsigned i) {return rc_page+8u*rc_sites[i].slot;}
static unsigned char *rc_stub(unsigned i) {return rc_stubs+0x20u*i;}
/* The thunk of policy site i (sites with an object, in table order); NULL for others. */
static unsigned char *rc_thunk(unsigned i) {
    if(!rc_stubs || i>=RC_SITES || !rc_sites[i].object) return NULL;
    unsigned k=0;for(unsigned j=0;j<i;j++) k+=rc_sites[j].object!=RC_OBJ_NONE;
    return rc_stubs+RC_THUNKS+RC_THUNK_SIZE*k;
}
/* The original seven bytes of site i and its thunk form (call rel32 to the thunk, 66 90). */
static void rc_site_original(unsigned i,unsigned char out[7]) {
    memcpy(out,rc_sites[i].prefix,3);int32_t d=rc_disp_to(rc_sites[i].rva,image+rc_site_target(i));memcpy(out+3,&d,4);
}
static int rc_site_thunk_form(unsigned i,unsigned char out[7]) {
    unsigned char *code=rc_thunk(i);if(!code) return 0;
    int32_t d=(int32_t)((intptr_t)code-(intptr_t)(image+rc_sites[i].rva+5));
    out[0]=0xe8;memcpy(out+1,&d,4);out[5]=0x66;out[6]=0x90;return 1;
}
/* 0 original, 1 ours (the page slot), 2 ours (the policy thunk), -1 anything else. */
static int rc_site_state(unsigned i) {
    unsigned char now[7],code[7];if(!read_local(image+rc_sites[i].rva,now,7)) return -1;
    if(rc_site_thunk_form(i,code) && !memcmp(now,code,7)) return 2;
    if(memcmp(now,rc_sites[i].prefix,3)) return -1;
    int32_t d;memcpy(&d,now+3,4);
    if(d==rc_disp_to(rc_sites[i].rva,image+rc_site_target(i))) return 0;
    if(rc_page && d==rc_disp_to(rc_sites[i].rva,rc_slot(i))) return 1;
    return -1;
}
/* 0 original (the native callee), 1 ours (its stub), -1 anything else. */
static int rc_call_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+rc_calls[i].rva,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==rc_rel_to(rc_calls[i].rva,image+rc_calls[i].target)) return 0;
    if(rc_stubs && d==rc_rel_to(rc_calls[i].rva,rc_stub(i))) return 1;
    return -1;
}
/* Jump i: its thunk on the stub page; the patched form jmp rel32 + 66 90. */
static unsigned char *rc_jump_stub(unsigned i) {return rc_stubs?rc_stubs+RC_JUMP_STUBS+0x80u*i:NULL;}
static int rc_jump_form(unsigned i,unsigned char out[7]) {
    unsigned char *stub=rc_jump_stub(i);if(!stub) return 0;
    int32_t d=(int32_t)((intptr_t)stub-(intptr_t)(image+rc_jumps[i].rva+5));
    out[0]=0xe9;memcpy(out+1,&d,4);out[5]=0x66;out[6]=0x90;return 1;
}
/* 0 original, 1 ours (the jump), -1 anything else. */
static int rc_jump_state(unsigned i) {
    unsigned char now[7],code[7];if(!read_local(image+rc_jumps[i].rva,now,7)) return -1;
    if(!memcmp(now,rc_jumps[i].original,7)) return 0;
    return rc_jump_form(i,code) && !memcmp(now,code,7)?1:-1;
}
/* Pins of the given groups; sites, calls and jumps inside a pin read as the originals. */
/* A copy of image bytes [rva, rva+size) with every site, call and jump of ours read as the original
 * (0 when one of them holds a foreign value). Also used by the mass traffic pins over Zone::Move. */
static int rc_read_original(unsigned rva,unsigned char *copy,unsigned size) {
        for(unsigned i=0;i<RC_SITES;i++) {
            unsigned at=rc_sites[i].rva;if(at<rva || at+7>rva+size) continue;
            if(rc_site_state(i)<0) return 0;
            rc_site_original(i,copy+(at-rva));
        }
        for(unsigned i=0;i<RC_CALLS;i++) {
            unsigned at=rc_calls[i].rva;if(at<rva || at+5>rva+size) continue;
            if(rc_call_state(i)<0) return 0;
            int32_t d=rc_rel_to(at,image+rc_calls[i].target);memcpy(copy+(at-rva)+1,&d,4);
        }
        for(unsigned i=0;i<RC_JUMPS;i++) {
            unsigned at=rc_jumps[i].rva;if(at<rva || at+7>rva+size) continue;
            if(rc_jump_state(i)<0) return 0;
            memcpy(copy+(at-rva),rc_jumps[i].original,7);
        }
        return 1;
}
static int rc_pins_ok(unsigned groups) {
    static unsigned char copy[0x700];
    for(unsigned p=0;p<RC_PINS;p++) {
        if(!(groups&(1u<<rc_pins[p].group))) continue;
        unsigned rva=rc_pins[p].rva,size=rc_pins[p].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size) || !rc_read_original(rva,copy,size)) return 0;
        if(!hash_ok(copy,size,rc_pins[p].sha)) return 0;
    }
    return 1;
}
/* 4 bytes by CAS of each aligned 16-byte block they touch (several straddle two;
 * written only while no pass can run). */
static int rc_cas4(unsigned at,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);
    return view_cas_bytes(at,e,v,4);
}
/* No pass that reads a patched site can be running (region: the region pass,
 * traffic: the mass traffic pass; fields run on the simulation thread): no task
 * yet, or its last pass done (the next kick comes from the MoveWorker's frame end). */
static int rc_passes_idle(void) {
    unsigned char *task;
    const unsigned context=rc_armed_groups&(1u<<RC_CONTEXT); /* SetParent runs in both passes too */
    if((context || (rc_armed_groups&(1u<<RC_REGION))) && (task=mt_task(&mt_region)) && !mt_ready(&mt_region,task)) return 0;
    if((context || (rc_armed_groups&(1u<<RC_TRAFFIC))) && (task=mt_task(&mt_traffic)) && !mt_ready(&mt_traffic,task)) return 0;
    return 1;
}
static void *volatile *rc_manager_camera(void) {return (void *volatile *)(rc_page+RC_MANAGER+0xb0);}
static void *volatile *rc_shadow_player(void) {return (void *volatile *)(rc_page+RC_SHADOW+0x230);}
static void *volatile *rc_shadow_camera(void) {return (void *volatile *)(rc_page+RC_SHADOW+0x3d0);}
static void *volatile *rc_manager2_camera(void) {return (void *volatile *)(rc_page+RC_MANAGER2+0xb0);}
static void *volatile *rc_k_manager_camera(unsigned k) {return (void *volatile *)(rc_page+rc_k_manager[k]+0xb0);}
/* Stand-ins 2..3 (k): the first view point of each sector neither view point 1 nor 2 is in, slot order, while
 * native_gate_viewpoint_standins allows (rc_standins_max, 2..4); rc_more of them are set. */
static unsigned rc_standins_max=2u,rc_k_next[VIEW_MAX];
static volatile LONG rc_more;
static void *volatile rc_k_sector[VIEW_MAX],*volatile rc_k_cluster[VIEW_MAX],*volatile rc_k_zone[VIEW_MAX];
static void *rc_k_cluster_of(unsigned k);
/* Replacements reached through the stubs: the stand-in's answer while it is set. */
static void **rc_camera_zone(void *unused,void **out) {
    InterlockedIncrement64(&rc_stub_calls[0]);
    if(*rc_manager_camera()) {*out=rc_view_zone;return out;}
    return ((void **(*)(void *,void **))(void *)(image+rc_calls[0].target))(unused,out);
}
static int rc_object_two(void *object,unsigned kind);
static int rc_object_k(void *object,unsigned kind);
static void rc_pick_note(uint32_t site,void *object,unsigned kind,int two);
/* 0x9fcad0 (the camera's class-0xf Cluster) at 0x83f2ff; the stub puts 0x83ef20's component (r12) in r8. */
static void **rc_camera_sector(void *unused,void **out,void *object) {
    InterlockedIncrement64(&rc_stub_calls[1]);const int k=rc_object_k(object,RC_OBJ_R12);
    if(rc_policy && rc_pick_rows_ticks && object) rc_pick_note(0x101u,object,RC_OBJ_R12,k);
    if(k) {*out=rc_k_cluster_of((unsigned)k);return out;}
    if(*rc_manager_camera()) {*out=rc_view_sector;return out;}
    return ((void **(*)(void *,void **))(void *)(image+rc_calls[1].target))(unused,out);
}
/* 0x9f9da0(unused, out) for the viewpoint: its class-0x75 ancestor (itself
 * included), then upwards every class-0x6f parent that is also class 0x75
 * replaces it until a class-0x6f parent is not (the native walk from the
 * camera anchor). */
static void *rc_ancestor(void *object,int klass);
static int rc_is_class(void *object,int klass) {
    void **vtable=NULL;rc_is_class_fn is_class=NULL;
    return object && read_local(object,&vtable,8) && vtable && read_local((unsigned char*)vtable+0x11e8,&is_class,8) && is_class && is_class(object,klass);
}
/* Viewpoint 2 is latched in a sector other than viewpoint 1's. */
static int rc_two_sectors(void) {return rc_policy && *rc_manager2_camera() && rc_vp2_sector && rc_vp2_sector!=rc_view_sector58;}
static void **rc_controllable(unsigned call,void *unused,void **out) {
    InterlockedIncrement64(&rc_stub_calls[call]);void *camera=*rc_manager_camera();
    if(camera && rc_two_sectors()) { /* the sectors' view points by turns, one per tick */
        const unsigned k=(unsigned)(rc_tick%(2u+(unsigned)rc_more));
        if(k) {camera=k==1?*rc_manager2_camera():*rc_k_manager_camera(k);InterlockedIncrement64(&rc_alternations);}
    }
    if(!camera) return ((void **(*)(void *,void **))(void *)(image+rc_calls[call].target))(unused,out);
    void *viewpoint=NULL;read_local((unsigned char*)camera+0x770,&viewpoint,8);
    unsigned char *p=viewpoint?rc_ancestor(viewpoint,0x75):NULL;*out=p;
    while(p) {
        unsigned char *parent=NULL;if(!read_local(p+0x70,&parent,8)) break;
        for(p=NULL;parent;) {
            if(rc_is_class(parent,0x6f)) {if(rc_is_class(parent,0x75)) {*out=parent;p=parent;} break;}
            if(!read_local(parent+0x70,&parent,8)) break;
        }
    }
    return out;
}
static void **rc_controllable_a(void *unused,void **out) {return rc_controllable(4,unused,out);}
static void **rc_controllable_b(void *unused,void **out) {return rc_controllable(5,unused,out);}
/* 0x9f9eb0 (the active camera) at 0x8ab40a; the stub puts 0x8ab1e0's traffic object (r12) in rcx. */
static void *rc_active_camera(void *object) {
    InterlockedIncrement64(&rc_stub_calls[2]);void *camera=*rc_manager_camera();
    const int k=camera?rc_object_k(object,RC_OBJ_R12_TRAFFIC):0;
    if(rc_policy && rc_pick_rows_ticks && object) rc_pick_note(0x102u,object,RC_OBJ_R12_TRAFFIC,k);
    if(k) return k==1?*rc_manager2_camera():*rc_k_manager_camera((unsigned)k);
    return camera?camera:((void *(*)(void))(void *)(image+rc_calls[2].target))();
}
/* The scene choice's active-camera calls: viewpoint 2 while its stand-in is set. */
static void **rc_scene_ancestor(unsigned call,void *unused,void **out,void *volatile *answer) {
    InterlockedIncrement64(&rc_stub_calls[call]);
    if(*rc_manager2_camera()) {*out=*answer;return out;}
    return ((void **(*)(void *,void **))(void *)(image+rc_calls[call].target))(unused,out);
}
static void **rc_scene_sector6(void *unused,void **out) {return rc_scene_ancestor(6,unused,out,&rc_vp2_sector);}
static void **rc_scene_zone7(void *unused,void **out) {return rc_scene_ancestor(7,unused,out,&rc_vp2_zone);}
static void *rc_scene_active8(void) {
    InterlockedIncrement64(&rc_stub_calls[8]);void *camera=*rc_manager2_camera();
    return camera?camera:((void *(*)(void))(void *)(image+rc_calls[8].target))();
}
static void **rc_scene_sector9(void *unused,void **out) {return rc_scene_ancestor(9,unused,out,&rc_vp2_sector);}
static void **rc_scene_sector10(void *unused,void **out) {return rc_scene_ancestor(10,unused,out,&rc_vp2_sector);}
/* SetParent's camera context call (0x6ce60d): the anchor test above it read the
 * shadow universe; the events go out for the live player camera, as natively. */
static void rc_context_events(void *camera,void *old_parent,void *new_parent) {
    InterlockedIncrement64(&rc_stub_calls[RC_CONTEXT_CALL]);
    if(rc_page && (camera==rc_page+RC_CAMERAS || camera==rc_page+RC_CAMERAS+RC_CAMERA_SIZE)) {
        unsigned char *universe=*(unsigned char*volatile*)(image+RC_UNIVERSE);camera=NULL;
        if(universe) read_local(universe+0x3d0,&camera,8);
        if(!camera) {InterlockedIncrement64(&rc_context_skipped);return;}
    }
    (rc_context_native?rc_context_native:(rc_context_fn)(void *)(image+rc_calls[RC_CONTEXT_CALL].target))(camera,old_parent,new_parent);
}
/* Effect deferral rows (diagnostic). */
typedef struct {uint64_t tick,key,time,now,word;uint32_t delay;unsigned char deferred,klass,unreadable;} rc_effect_row_t;
static rc_effect_row_t rc_effect_ring[RC_EFFECT_ROW_RING];
static volatile LONG64 rc_effect_rows;
static void rc_effect_note(uint64_t element,uint64_t context,uint64_t time) {
    rc_effect_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.time=time;r.klass=WORLD_KEY_NONE;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);void *owner=NULL;uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;float delay=0;double now=0,shot;
    if(!tls || !tls[0] || !read_local((unsigned char*)tls[0]+0x300,&r.now,8) || !read_local((unsigned char*)tls[0]+0x318,&r.word,8) ||
       !read_local((void*)(uintptr_t)(element+0x68),&r.delay,4)) r.unreadable=1;
    if(context && read_local((void*)(uintptr_t)(context+0x38),&owner,8) && owner && read_local((unsigned char*)owner+8,&runtime,8) && runtime)
        {r.key=world_identity_key(runtime,&klass);r.klass=(unsigned char)klass;}
    memcpy(&delay,&r.delay,4);memcpy(&now,&r.now,8);memcpy(&shot,&time,8);
    r.deferred=!r.unreadable && (double)delay+shot>now;
    LONG64 index=InterlockedIncrement64(&rc_effect_rows)-1;rc_effect_ring[(uint64_t)index%RC_EFFECT_ROW_RING]=r;
}
typedef void *(*rc_creator_fn)(void *,void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static void *rc_effect_creator(void *a1,void *a2,void *a3,void *a4,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,
                               uint64_t a10,uint64_t a11,uint64_t a12) {
    InterlockedIncrement64(&rc_stub_calls[3]);rc_effect_note(a5,a6,a8);
    return ((rc_creator_fn)(void *)(image+rc_calls[3].target))(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12);
}
static int rc_effect_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&rc_effect_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)RC_EFFECT_ROW_RING && index<(uint64_t)(count-(LONG64)RC_EFFECT_ROW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const rc_effect_row_t *r=&rc_effect_ring[index%RC_EFFECT_ROW_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu key=%016llx klass=%u delay=%08x time=%016llx now=%016llx deferred=%u unreadable=%u word=%016llx",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->key,(unsigned)r->klass,r->delay,
        (unsigned long long)r->time,(unsigned long long)r->now,(unsigned)r->deferred,(unsigned)r->unreadable,(unsigned long long)r->word);
    return n>0 && (size_t)n<cap?n:0;
}
/* A stand-in camera at the viewpoint's origin. Only pointers change between
 * writes, each by one aligned store. */
static void rc_write_camera(unsigned char *camera,void *viewpoint) {
    static const float rows[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
    for(unsigned b=0;b<2;b++) {
        unsigned char *bank=camera+b*RC_BANK,*entry=bank+0x40;uint64_t capacity=8,count=1;
        memcpy(entry+0x20,rows,sizeof(rows));memset(entry+0x10,0,16);
        memcpy(bank+0x2c0,&capacity,8);memcpy(bank+0x2d0,&count,8);
        InterlockedExchangePointer((void *volatile *)(bank+0x30),viewpoint);InterlockedExchangePointer((void *volatile *)entry,viewpoint);
    }
    memset(camera+0x590,0,16);memcpy(camera+0x5a0,rows,sizeof(rows)); /* local PosRot: translation, three rows */
    int32_t mode=1;memcpy(camera+0x868,&mode,4);
    InterlockedExchangePointer((void *volatile *)(camera+0x770),viewpoint);InterlockedExchangePointer((void *volatile *)(camera+0x778),viewpoint);
}
/* The first of object and its ancestors (+0x70) of the class, through the vtable's
 * class test (slot 0x11e8 / 8), read-only otherwise. */
/* The policy's choice for an object: 1 when it sits in viewpoint 2's sector and
 * viewpoint 1 is elsewhere (a mass traffic object through its zone). Hot path
 * (every traffic test and highway update while the view points are apart): the
 * object is live in the native caller, which walks the same fields itself
 * (+0x70 parents through the class test slot, the traffic object's zone), so
 * they are read directly here instead of through read_local's system call. */
static int rc_object_two(void *object,unsigned kind) {return rc_object_k(object,kind)!=0;}
/* The stand-in for the object's sector: 1 view point 2's, 2..3 a further one's, 0 view point 1 (or none). */
static int rc_object_k(void *object,unsigned kind) {
    if(!object || !rc_two_sectors()) return 0;
    unsigned char *p=object;
    if(kind==RC_OBJ_R12_TRAFFIC) {
        unsigned char *zone=*(unsigned char *volatile *)(p+0x108);
        p=zone?zone:*(unsigned char *volatile *)(p+0x100);
    }
    void *vp2=rc_vp2_sector;const LONG more=rc_more;
    for(unsigned depth=0;p && depth<17;depth++) {
        void **vtable=*(void **volatile *)p;rc_is_class_fn is_class=vtable?(rc_is_class_fn)vtable[0x11e8/8]:NULL;
        if(!is_class) return 0;
        if(is_class(p,0x58)) {
            if(p==vp2) return 1;
            for(LONG k=2;k<2+more && k<(LONG)VIEW_MAX;k++) if(p==rc_k_sector[k]) return (int)k;
            return 0;
        }
        p=*(unsigned char *volatile *)(p+0x70);
    }
    return 0;
}
static void *rc_k_cluster_of(unsigned k) {return k==1?rc_vp2_cluster:k<VIEW_MAX?rc_k_cluster[k]:NULL;}
/* A policy row (diagnostic) for an object the policy decided on. */
static void rc_pick_note_full(uint32_t site,void *object,unsigned kind,int two,const float *pos,const uint32_t *ab) {
    if(!rc_pick_rows_ticks || stats.issued>rc_pick_rows_ticks) return;
    rc_pick_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.site=site;r.two=(unsigned char)two;
    if(pos) {r.has_pos=1;memcpy(r.pos,pos,12);}
    if(ab) {r.has_ab=1;r.a=ab[0];r.b=ab[1];r.pend=ab[2];}
    unsigned char *p=object;
    if(p && kind==RC_OBJ_R12_TRAFFIC) {unsigned char *zone=*(unsigned char *volatile *)(p+0x108);p=zone?zone:*(unsigned char *volatile *)(p+0x100);}
    if(p) {unsigned klass;r.runtime=*(volatile uint64_t *)(p+8);r.key=r.runtime?world_identity_key(r.runtime,&klass):0;}
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    r.mover=tls && tls[0] && *(volatile uint32_t *)((unsigned char*)tls[0]+0x314)!=0?1:GetCurrentThreadId()==ui_thread?2:3;
    LONG64 index=InterlockedIncrement64(&rc_pick_rows)-1;rc_pick_ring[(uint64_t)index%RC_PICK_ROWS]=r;
}
static void rc_pick_note_pos(uint32_t site,void *object,unsigned kind,int two,const float *pos) {rc_pick_note_full(site,object,kind,two,pos,NULL);}
static void rc_pick_note(uint32_t site,void *object,unsigned kind,int two) {rc_pick_note_full(site,object,kind,two,NULL,NULL);}
/* A row with two counters (activation: +0x1d0 before/after, +0x228; collection: the count). */
static void rc_pick_note_ab(uint32_t site,void *object,unsigned kind,int two,uint32_t a,uint32_t b,uint32_t pend) {
    const uint32_t ab[3]={a,b,pend};rc_pick_note_full(site,object,kind,two,NULL,ab);
}
/* The calling thread's game time (TLS+0x300; the sync mass traffic pass's own). */
static double rc_thread_time(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double now=0;
    if(tls && tls[0]) memcpy(&now,(unsigned char*)tls[0]+0x300,8);
    return now;
}
typedef void (*rc_gtr_fn)(void *,float *,double,void *,unsigned char);
static rc_gtr_fn rc_gtr_native; /* tests only: replaces U::Component::GetTransformRelativeTo 0x3db8a0 */
/* GetTransformRelativeTo composes each object's local transform (slot 0x1440) into the matrix it is
 * given, up the +0x70 chain to the ancestor (self == ancestor: unchanged), so the matrix starts as the
 * identity PosRot its callers set (translation 0 then the rows of 0x22c01d0..0x22c01f0). cam-C1g2's
 * pos=3e10 came from an uninitialised stack matrix. */
static void rc_gtr(void *self,float out[16],double time,void *ancestor) {
    static const float identity[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    memcpy(out,identity,sizeof(identity));
    (rc_gtr_native?rc_gtr_native:(rc_gtr_fn)(void *)(image+0x3db8a0u))(self,out,time,ancestor,0);
}
/* The stand-in a camera belongs to (either buffer): 1..3, 0 for view point 1's or another. */
static int rc_camera_k(const void *camera) {
    if(!rc_page || !camera) return 0;
    for(unsigned k=1;k<VIEW_MAX;k++)
        if(camera==rc_page+rc_k_cameras[k] || camera==rc_page+rc_k_cameras[k]+RC_CAMERA_SIZE) return k==1 || (LONG)k<2+rc_more?(int)k:0;
    return 0;
}
/* A stand-in camera other than view point 1's. */
static int rc_is_camera2(const void *camera) {return rc_camera_k(camera)!=0;}
typedef void (*rc_zone_test_fn)(void *,float *,void *);
static rc_zone_test_fn rc_zone_test_native; /* tests only: replaces 0x3e35d0 */
/* 0x83ef20's zone test (0x83f26f): view point 2 from the simulation's transforms. */
static void rc_zone_test(void *component,float *out,void *camera) {
    InterlockedIncrement64(&rc_stub_calls[RC_ZONE_TEST_CALL]);
    const int k=rc_policy?rc_camera_k(camera):0,two=k!=0;void *vp2=NULL,*cluster=k?rc_k_cluster_of((unsigned)k):NULL;
    if(two) memcpy(&vp2,(unsigned char*)camera+0x770,8);
    if(two && vp2 && cluster && component) {
        __declspec(align(16)) float a[16],b[16];double now=rc_thread_time();
        rc_gtr(component,a,now,cluster);rc_gtr(vp2,b,now,cluster);
        memcpy(out,a,64);out[0]=a[0]-b[0];out[1]=a[1]-b[1];out[2]=a[2]-b[2];out[3]=0;
    } else (rc_zone_test_native?rc_zone_test_native:(rc_zone_test_fn)(void *)(image+rc_calls[RC_ZONE_TEST_CALL].target))(component,out,camera);
    if(rc_pick_rows_ticks && camera) rc_pick_note_pos(0x103u,component,RC_OBJ_R12,two,out);
}
typedef void (*rc_activate_fn)(void *);
static rc_activate_fn rc_activate_native; /* tests only: replaces 0x8aacb0 */
/* A traffic object switched on (diagnostic): +0x1d0 before and after, +0x228. */
static void rc_activate(unsigned call,uint32_t site,unsigned char *traffic) {
    InterlockedIncrement64(&rc_stub_calls[call]);
    unsigned before=traffic?*(volatile unsigned char *)(traffic+0x1d0):0;
    (rc_activate_native?rc_activate_native:(rc_activate_fn)(void *)(image+rc_calls[call].target))(traffic);
    if(rc_pick_rows_ticks && traffic)
        rc_pick_note_ab(site,traffic,RC_OBJ_R12_TRAFFIC,rc_object_two(traffic,RC_OBJ_R12_TRAFFIC),before,
                        *(volatile unsigned char *)(traffic+0x1d0),*(volatile unsigned char *)(traffic+0x228));
}
static void rc_activate_pass(void *traffic) {rc_activate(RC_ACTIVATE_CALL,0x105u,traffic);}
static void rc_activate_update_a(void *traffic) {rc_activate(RC_ACTIVATE_CALL+1,0x106u,traffic);}
static void rc_activate_update_b(void *traffic) {rc_activate(RC_ACTIVATE_CALL+2,0x107u,traffic);}
typedef void *(*rc_collect_fn)(void *,void **);
static rc_collect_fn rc_collect_native; /* tests only: replaces 0x8afaf0 */
/* The build's objects with cached waypoints (diagnostic): how many it found. */
static void *rc_collect(void *traffic,void **vector) {
    InterlockedIncrement64(&rc_stub_calls[RC_COLLECT_CALL]);
    void *result=(rc_collect_native?rc_collect_native:(rc_collect_fn)(void *)(image+rc_calls[RC_COLLECT_CALL].target))(traffic,vector);
    if(rc_pick_rows_ticks && traffic && vector) {
        uint64_t count=vector[1]>=vector[0]?((uintptr_t)vector[1]-(uintptr_t)vector[0])/8:0;
        rc_pick_note_ab(0x108u,traffic,RC_OBJ_R12_TRAFFIC,rc_object_two(traffic,RC_OBJ_R12_TRAFFIC),(uint32_t)count,0,0);
    }
    return result;
}
typedef void *(*rc_lane_camera_fn)(void *,float *,void *);
static rc_lane_camera_fn rc_lane_camera_native; /* tests only: replaces 0x979ef0 */
/* 0x8ab1e0's camera in the frame zone (0x8ab419): view point 2's own transform there. */
static void *rc_lane_camera(void *camera,float *out,void *frame_zone) {
    InterlockedIncrement64(&rc_stub_calls[RC_LANE_CAMERA_CALL]);
    void *vp2=NULL,*result=out;const int two=rc_policy && rc_is_camera2(camera);
    if(two) memcpy(&vp2,(unsigned char*)camera+0x770,8);
    if(two && vp2 && frame_zone) {__declspec(align(16)) float m[16];rc_gtr(vp2,m,rc_thread_time(),frame_zone);memcpy(out,m,64);}
    else result=(rc_lane_camera_native?rc_lane_camera_native:(rc_lane_camera_fn)(void *)(image+rc_calls[RC_LANE_CAMERA_CALL].target))(camera,out,frame_zone);
    if(rc_pick_rows_ticks && camera) rc_pick_note_pos(0x104u,frame_zone,RC_OBJ_R12,two,out);
    return result;
}
/* Shape refresh: the view points rc_frame latched for the next frame's physics steps (-1: none yet). */
static void *volatile rc_shape_points[VIEW_MAX],*volatile rc_shape_sectors[VIEW_MAX],*volatile rc_shape_clusters[VIEW_MAX];
static volatile LONG rc_shape_count=-1;
#define RC_SHAPE_KEYS 262144u
static volatile LONG64 rc_shape_keys[RC_SHAPE_KEYS]; /* object keys whose due time was aligned (insert-only) */
static volatile LONG64 rc_shape_aligned,rc_shape_unmapped,rc_shape_near,rc_shape_far,rc_shape_native,rc_shape_unkeyed;
#define RC_SHAPE_RNG_DOMAIN 0x5348415045445545ULL /* "SHAPEDUE" */
typedef char (*rc_shape_update_fn)(void *,void *,unsigned char);
typedef float (*rc_shape_interval_fn)(void *,void *);
typedef unsigned char *(*rc_shape_macro_fn)(void *);
typedef float (*rc_shape_rand_fn)(void *,float);
typedef float *(*rc_shape_bounds_fn)(void *,float *);
static rc_shape_update_fn rc_shape_update_native;     /* tests only: replaces 0xc2c00 */
static rc_shape_interval_fn rc_shape_interval_native; /* tests only: replaces 0xc2810 */
static rc_shape_macro_fn rc_shape_macro_native;       /* tests only: replaces 0x7ad890 */
static rc_shape_rand_fn rc_shape_rand_native;         /* tests only: replaces Random::FloatState 0x14ddc90 */
/* 1 when the key is new (now recorded), 0 when seen, -1 when the table is full. */
static int rc_shape_key_first(uint64_t key) {
    unsigned h=(unsigned)(lockstep_mix(key)&(RC_SHAPE_KEYS-1));
    for(unsigned n=0;n<RC_SHAPE_KEYS/2;n++,h=(h+1)&(RC_SHAPE_KEYS-1)) {
        LONG64 seen=InterlockedCompareExchange64(&rc_shape_keys[h],(LONG64)key,0);
        if(!seen) return 1;
        if((uint64_t)seen==key) return 0;
    }
    return -1;
}
/* The first class-0x58 (Sector) object from p up the +0x70 parents; p is live in the native caller (hot path:
 * direct reads, as rc_object_two). */
static void *rc_shape_sector(unsigned char *p) {
    for(unsigned depth=0;p && depth<17;depth++) {
        void **vtable=*(void **volatile *)p;rc_is_class_fn is_class=vtable?(rc_is_class_fn)vtable[0x11e8/8]:NULL;
        if(!is_class) return NULL;
        if(is_class(p,0x58)) return p;
        p=*(unsigned char *volatile *)(p+0x70);
    }
    return NULL;
}
/* Body_UpdateShape's refresh call (0xbffb7): the first call per object key after the install refreshes at
 * once (the due time set to the thread's game time); then the native call. */
static char rc_shape_update(void *holder,void *owner,unsigned char force) {
    InterlockedIncrement64(&rc_stub_calls[RC_SHAPE_UPDATE_CALL]);
    if(holder && owner && InterlockedCompareExchange(&rc_shape_count,0,0)>=0) {
        const uint64_t runtime=*(volatile uint64_t *)((unsigned char*)owner+8);unsigned klass;
        const uint64_t key=runtime?world_identity_key(runtime,&klass):0;
        if(!key) InterlockedIncrement64(&rc_shape_unmapped);
        else {
            const int first=rc_shape_key_first(key);
            if(first<0) rc_fail("shape_keys");
            else if(first) {const double now=rc_thread_time();memcpy((unsigned char*)holder+0x28,&now,8);InterlockedIncrement64(&rc_shape_aligned);}
        }
    }
    return (rc_shape_update_native?rc_shape_update_native:(rc_shape_update_fn)(void *)(image+rc_calls[RC_SHAPE_UPDATE_CALL].target))(holder,owner,force);
}
/* The next refresh's interval (0xc2dac -> 0xc2810), from the simulation and the latched view points. */
static float rc_shape_interval(void *holder,void *owner) {
    InterlockedIncrement64(&rc_stub_calls[RC_SHAPE_INTERVAL_CALL]);
    const LONG count=InterlockedCompareExchange(&rc_shape_count,0,0);
    if(count<0) {
        InterlockedIncrement64(&rc_shape_native);
        return (rc_shape_interval_native?rc_shape_interval_native:(rc_shape_interval_fn)(void *)(image+rc_calls[RC_SHAPE_INTERVAL_CALL].target))(holder,owner);
    }
    if(!owner || !holder) return 0.5f;
    unsigned char *o=owner;
    const unsigned char *macro=(rc_shape_macro_native?rc_shape_macro_native:(rc_shape_macro_fn)(void *)(image+0x7ad890u))(*(void *volatile *)(o+0x60));
    if(macro && macro[0x278]) return 0.0f;
    void *sector=rc_shape_sector(*(unsigned char *volatile *)(o+0x70));
    const double now=rc_thread_time();float best=-1.0f;
    for(LONG i=0;sector && i<count && i<(LONG)VIEW_MAX;i++) {
        void *point=rc_shape_points[i];
        if(!point || rc_shape_sectors[i]!=sector) continue;
        __declspec(align(16)) float a[16],b[16];rc_gtr(point,a,now,sector);rc_gtr(owner,b,now,sector);
        const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],d=sqrtf(dx*dx+dy*dy+dz*dz);
        if(best<0 || d<best) best=d;
    }
    if(best<0) {InterlockedIncrement64(&rc_shape_far);return 5.0f;}
    InterlockedIncrement64(&rc_shape_near);
    void *shape=*(void *volatile *)(*(unsigned char *volatile *)((unsigned char*)holder+0x10)+8);
    __declspec(align(16)) float box_buffer[16];
    const float *box=((rc_shape_bounds_fn)(*(void ***)shape)[0x18/8])(shape,box_buffer);
    const float ex=box[4]-box[0],ey=box[5]-box[1],ez=box[6]-box[2],size=sqrtf(ex*ex+ey*ey+ez*ez);
    if(!(size>=0.0001f)) return 0.5f;
    float v=(best-size)/size;
    if(v>5.0f) v=5.0f;
    if(!(v>0.5f)) v=0.5f; /* maxss: 0.5 also for NaN */
    /* Its own word: no frame-level draw, no due time depending on what drew before it. An object without a key: the tick's. */
    const uint64_t runtime=*(volatile uint64_t *)(o+8);unsigned klass;
    const uint64_t key=runtime?world_identity_key(runtime,&klass):0;
    if(!key) InterlockedIncrement64(&rc_shape_unkeyed);
    uint64_t word=lockstep_seed(RC_SHAPE_RNG_DOMAIN,stats.issued,key,0,0,0);
    return v-(rc_shape_rand_native?rc_shape_rand_native:(rc_shape_rand_fn)(void *)(image+0x14ddc90u))(&word,0.1f);
}
/* sound_rng: both draws of a sound start on a word of their own. */
typedef uint64_t (*rc_sound_choice_fn)(void *,void *,void *,void *);
static rc_shape_rand_fn rc_sound_float_native;      /* tests only: replaces 0x14ddc90 */
static rc_sound_choice_fn rc_sound_choice_native;   /* tests only: replaces 0x14f2550 */
static volatile LONG64 rc_sound_seeds;
static uint64_t rc_sound_word(void) {return lockstep_mix(0x534f554e445f5247ULL^(uint64_t)InterlockedIncrement64(&rc_sound_seeds));}
static float rc_sound_float(void *state,float range) {
    (void)state;InterlockedIncrement64(&rc_stub_calls[RC_SOUND_CALL]);
    uint64_t word=rc_sound_word();
    return (rc_sound_float_native?rc_sound_float_native:(rc_shape_rand_fn)(void *)(image+0x14ddc90u))(&word,range);
}
static uint64_t rc_sound_choice(void *a,void *b,void *c,void *d) {
    InterlockedIncrement64(&rc_stub_calls[RC_SOUND_CALL+1u]);
    const rc_sound_choice_fn native=rc_sound_choice_native?rc_sound_choice_native:(rc_sound_choice_fn)(void *)(image+0x14f2550u);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *word=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!word) return native(a,b,c,d);
    const uint64_t saved=*word;uint64_t result=0;
    __try {*word=rc_sound_word();result=native(a,b,c,d);}
    __finally {*word=saved;}
    return result;
}
/* Slot 6 (rc_frame): the latched view points and their sectors for the next frame's intervals; without
 * view points the player object's class-0x49 ancestor (or the player object). */
static void rc_shape_latch(void) {
    InterlockedExchange(&rc_shape_count,0);LONG n=0;
    for(unsigned i=0;view_latched && i<view_count && i<VIEW_MAX;i++) {
        void *p=view_points[i];uint64_t id=0;
        if(!p || !read_local((unsigned char*)p+8,&id,8) || id!=view_ids[i]) continue;
        rc_shape_points[n]=p;rc_shape_sectors[n]=rc_ancestor(p,0x58);rc_shape_clusters[n]=rc_ancestor(p,0xf);++n;
    }
    if(!n) {
        unsigned char *universe=*(unsigned char*volatile*)(image+RC_UNIVERSE);void *player=NULL,*parent=NULL;
        if(universe) read_local(universe+0x230,&player,8);
        if(player && read_local((unsigned char*)player+0x70,&parent,8)) {
            void *ship=parent?rc_ancestor(parent,0x49):NULL;if(!ship) ship=player;
            rc_shape_points[0]=ship;rc_shape_sectors[0]=rc_ancestor(ship,0x58);rc_shape_clusters[0]=rc_ancestor(ship,0xf);n=1;
        }
    }
    MemoryBarrier();InterlockedExchange(&rc_shape_count,n);
}
/* The thread class of a call: 0 MoveWorker (TLS+0x314 set), 1 the gate's thread, 2 any other. */
static unsigned rc_thread_class(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0] && *(volatile uint32_t *)((unsigned char*)tls[0]+0x314)!=0?0u:GetCurrentThreadId()==ui_thread?1u:2u;
}
/* 1 when frame is ancestor itself or below it (+0x70 parents; live objects of the native caller). */
static int rc_is_below(unsigned char *frame,const void *ancestor) {
    for(unsigned depth=0;frame && depth<17;depth++) {if(frame==ancestor) return 1;frame=*(unsigned char *volatile *)(frame+0x70);}
    return 0;
}
/* mining: 0x7884a0's camera test from the latched view points. */
static volatile LONG64 rc_mining_calls,rc_mining_near_count,rc_mining_foreign;
static int rc_mining_near(void *cluster,const float *pos,void *frame) {
    InterlockedIncrement64(&rc_mining_calls);
    const LONG count=InterlockedCompareExchange(&rc_shape_count,0,0);float best=-1.0f;int candidates=0;
    if(cluster && pos && frame) {
        if(!rc_is_below(frame,cluster)) InterlockedIncrement64(&rc_mining_foreign);
        else {
            const double now=rc_thread_time();__declspec(align(16)) float f[16];int have_frame=0;
            for(LONG i=0;i<count && i<(LONG)VIEW_MAX;i++) {
                void *point=rc_shape_points[i];
                if(!point || rc_shape_clusters[i]!=cluster) continue;
                if(!have_frame) {rc_gtr(frame,f,now,cluster);have_frame=1;}
                __declspec(align(16)) float v[16];rc_gtr(point,v,now,cluster);
                const float dx=v[0]-f[0],dy=v[1]-f[1],dz=v[2]-f[2];
                const float lx=dx*f[4]+dy*f[5]+dz*f[6]-pos[0],ly=dx*f[8]+dy*f[9]+dz*f[10]-pos[1],lz=dx*f[12]+dy*f[13]+dz*f[14]-pos[2];
                const float d=lx*lx+ly*ly+lz*lz;++candidates;
                if(best<0 || d<best) best=d;
            }
        }
    }
    const int is_near=best>=0 && best<2.5e9f;
    if(is_near) InterlockedIncrement64(&rc_mining_near_count);
    if(rc_pick_rows_ticks && cluster) {const float row[3]={best,(float)candidates,(float)is_near};rc_pick_note_pos(0x109u,cluster,RC_OBJ_R12,is_near,row);}
    return is_near;
}
/* highway_speed: the seed of an AI ship's tube speed jitter, from the runtime ID the native code loaded (rbp). */
static volatile LONG64 rc_hw_speed_keyed,rc_hw_speed_unmapped;
static uint64_t rc_hw_speed_seed(uint64_t runtime) {
    unsigned klass=WORLD_KEY_NONE;const uint64_t key=runtime?world_identity_key(runtime,&klass):0;
    InterlockedIncrement64(key?&rc_hw_speed_keyed:&rc_hw_speed_unmapped);
    return key;
}
/* The jump thunk of 0xeb0a2c: rbp = rc_hw_speed_seed(rbp), xmm0 (the tube factor) kept, the multiplier in rax as the
 * replaced movabs leaves it, and back to the imul at resume. Volatile registers are not read after the site. */
static unsigned rc_hw_speed_stub_bytes(unsigned char *p,const void *resume) {
    static const unsigned char head[]={0x48,0x83,0xec,0x30, 0xf3,0x0f,0x11,0x44,0x24,0x20, 0x48,0x89,0xe9, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0x48,0x89,0xc5, 0xf3,0x0f,0x10,0x44,0x24,0x20, 0x48,0x83,0xc4,0x30,
        0x48,0xb8,0x2d,0x7f,0x95,0x4c,0x2d,0xf4,0x51,0x58, 0xff,0x25,0,0,0,0};
    unsigned n=0;void *fn=(void*)rc_hw_speed_seed;
    memcpy(p+n,head,sizeof(head));n+=sizeof(head);memcpy(p+n,&fn,8);n+=8;memcpy(p+n,tail,sizeof(tail));n+=sizeof(tail);
    memcpy(p+n,&resume,8);n+=8;return n;
}
/* The jump thunk of 0x78854a: rc_mining_near(rdi, r13, rsi), then the native no-reference state and back to resume. */
/* A thunk made of head, rc_hw_speed_seed, tail, resume. */
static unsigned rc_hw_seed_thunk(unsigned char *p,const unsigned char *head,unsigned heads,const unsigned char *tail,unsigned tails,const void *resume) {
    unsigned n=0;void *fn=(void*)rc_hw_speed_seed;
    memcpy(p+n,head,heads);n+=heads;memcpy(p+n,&fn,8);n+=8;memcpy(p+n,tail,tails);n+=tails;memcpy(p+n,&resume,8);n+=8;return n;
}
/* 0xeb0f19: [rsp+0x38] (the wobble's FloatState state) = rc_hw_speed_seed([rax+8]); xmm1, FloatState's range, kept. */
static unsigned rc_hw_wobble_stub_bytes(unsigned char *p,const void *resume) {
    static const unsigned char head[]={0x48,0x83,0xec,0x30, 0xf3,0x0f,0x7f,0x4c,0x24,0x20, 0x48,0x8b,0x48,0x08, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0xf3,0x0f,0x6f,0x4c,0x24,0x20, 0x48,0x89,0x44,0x24,0x68, 0x48,0x83,0xc4,0x30, 0xff,0x25,0,0,0,0};
    return rc_hw_seed_thunk(p,head,sizeof(head),tail,sizeof(tail),resume);
}
/* 0xeb1092: rdx = rc_hw_speed_seed([rcx+8]) and test rdx,rdx for the js at the resume; xmm4 kept, xmm1 zero again. */
static unsigned rc_hw_bump_stub_bytes(unsigned char *p,const void *resume) {
    static const unsigned char head[]={0x48,0x83,0xec,0x30, 0xf3,0x0f,0x7f,0x64,0x24,0x20, 0x48,0x8b,0x49,0x08, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0x48,0x89,0xc2, 0xf3,0x0f,0x6f,0x64,0x24,0x20, 0x0f,0x57,0xc9, 0x48,0x83,0xc4,0x30, 0x48,0x85,0xd2, 0xff,0x25,0,0,0,0};
    return rc_hw_seed_thunk(p,head,sizeof(head),tail,sizeof(tail),resume);
}
#include "lockstep_traffic_rows.h"
static unsigned rc_jump_stub_bytes(unsigned i,unsigned char *p,const void *resume) {
    if(rc_jumps[i].rva==TQ_EARLY_SITE) return tq_early_stub_bytes(p);
    if(rc_jumps[i].rva==TP_HOOK_SITE) return tp_hook_stub_bytes(p);
    if(rc_jumps[i].rva==0xeb0a2cu) return rc_hw_speed_stub_bytes(p,resume);
    if(rc_jumps[i].rva==0xeb0f19u) return rc_hw_wobble_stub_bytes(p,resume);
    if(rc_jumps[i].rva==0xeb1092u) return rc_hw_bump_stub_bytes(p,resume);
    static const unsigned char head[]={0x48,0x83,0xec,0x20, 0x48,0x89,0xf9, 0x4c,0x89,0xea, 0x49,0x89,0xf0, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0x48,0x83,0xc4,0x20, 0x0f,0xb6,0xd8, 0x41,0xbc,0x02,0,0,0, 0x41,0xbe,0x17,0,0,0,
        0x44,0x89,0xb5,0xe8,0x01,0,0, 0x48,0xc7,0x44,0x24,0x60,0,0,0,0, 0xff,0x25,0,0,0,0};
    unsigned n=0;void *fn=(void*)rc_mining_near;
    memcpy(p+n,head,sizeof(head));n+=sizeof(head);memcpy(p+n,&fn,8);n+=8;memcpy(p+n,tail,sizeof(tail));n+=sizeof(tail);
    memcpy(p+n,&resume,8);n+=8;return n;
}
/* xs_detail: ShipExtraSmall::vfunc876's soft target, external view and camera distance. */
typedef char (*rc_xs_external_fn)(void);
typedef float (*rc_xs_distance_fn)(void *);
typedef void **(*rc_xs_target_fn)(void *,void **);
static rc_xs_external_fn rc_xs_external_native; /* tests only: replaces 0x9fc0e0 */
static rc_xs_distance_fn rc_xs_distance_native; /* tests only: replaces 0x3e40a0 */
static rc_xs_target_fn rc_xs_target_native;     /* tests only: replaces 0x9fcd70 */
static volatile LONG64 rc_xs_near,rc_xs_far,rc_xs_targets;
/* The virtual players' per-slot view (lockstep_pilot_stock_view.h), through pointers tests can stand in for. */
static void *rc_vpl_ship_native(unsigned k) {return k>=1 && k<=pilot_slot_count && pilot_slots[k-1].resolved?pilot_slots[k-1].object:NULL;}
static unsigned (*rc_vpl_count)(void)=vpl_slot_count;
static uint64_t (*rc_vpl_target)(unsigned)=vpl_soft_target;
static int (*rc_vpl_external)(unsigned)=vpl_external_view;
static void *(*rc_vpl_ship)(unsigned)=rc_vpl_ship_native;
/* 0x7e5346: in a stock session the ship when it is some slot's soft target (else NULL), as the getter writes it. */
static void **rc_xs_target(void *ship,void **out) {
    InterlockedIncrement64(&rc_stub_calls[RC_XS_TARGET_CALL]);
    const unsigned slots=rc_vpl_count();
    if(!slots || InterlockedCompareExchange(&rc_shape_count,0,0)<0)
        return (rc_xs_target_native?rc_xs_target_native:(rc_xs_target_fn)(void *)(image+rc_calls[RC_XS_TARGET_CALL].target))(ship,out);
    *out=NULL;
    const uint64_t runtime=ship?*(volatile uint64_t *)((unsigned char*)ship+8):0,portable=runtime?world_identity_portable(runtime):0;
    for(unsigned k=1;portable && k<=slots;k++) {
        const uint64_t t=rc_vpl_target(k);
        if(t>1 && t==portable) {*out=ship;InterlockedIncrement64(&rc_xs_targets);break;}
    }
    return out;
}
static char rc_xs_external(void) {
    InterlockedIncrement64(&rc_stub_calls[RC_XS_EXTERNAL_CALL]);
    if(InterlockedCompareExchange(&rc_shape_count,0,0)<0)
        return (rc_xs_external_native?rc_xs_external_native:(rc_xs_external_fn)(void *)(image+rc_calls[RC_XS_EXTERNAL_CALL].target))();
    const unsigned slots=rc_vpl_count();
    if(!slots) return 1;
    for(unsigned k=1;k<=slots;k++) if(rc_vpl_external(k)) return 1;
    return 0;
}
static float rc_xs_distance(void *ship) {
    InterlockedIncrement64(&rc_stub_calls[RC_XS_DISTANCE_CALL]);
    const LONG count=InterlockedCompareExchange(&rc_shape_count,0,0);
    if(count<0) return (rc_xs_distance_native?rc_xs_distance_native:(rc_xs_distance_fn)(void *)(image+rc_calls[RC_XS_DISTANCE_CALL].target))(ship);
    void *sector=ship?rc_shape_sector(*(unsigned char *volatile *)((unsigned char*)ship+0x70)):NULL;
    const double now=rc_thread_time();float best=-1.0f;
    const unsigned slots=rc_vpl_count();
    /* A stock session: the ships of the slots in external view; else the latched view points. */
    const unsigned candidates=slots?slots:(unsigned)(count>0?count:0);
    for(unsigned i=0;sector && i<candidates && i<(slots?slots:VIEW_MAX);i++) {
        void *point;
        if(slots) {
            if(!rc_vpl_external(i+1) || !(point=rc_vpl_ship(i+1))) continue;
            if(rc_shape_sector(*(unsigned char *volatile *)((unsigned char*)point+0x70))!=sector) continue;
        } else {
            point=rc_shape_points[i];
            if(!point || rc_shape_sectors[i]!=sector) continue;
        }
        __declspec(align(16)) float a[16],b[16];rc_gtr(point,a,now,sector);rc_gtr(ship,b,now,sector);
        const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],d=dx*dx+dy*dy+dz*dz;
        if(best<0 || d<best) best=d;
    }
    if(best<0) best=3.0e38f;
    InterlockedIncrement64(best<4.0e6f?&rc_xs_near:&rc_xs_far);
    if(rc_pick_rows_ticks && ship) {const float row[3]={best,0,0};rc_pick_note_pos(0x10au,ship,RC_OBJ_R12,best<4.0e6f,row);}
    return best;
}
/* camera_probe: pass-through counters by thread class. */
static volatile LONG64 rc_probe_counts[RC_PROBE_SITES][3],rc_probe_crystal_far;
typedef float (*rc_probe_distance_fn)(void *,void *);
typedef void **(*rc_probe_walk_fn)(void *,void **);
typedef void *(*rc_probe_vector_fn)(void *,float *,void *);
typedef void *(*rc_probe_offset_fn)(void *,float *,double,void *);
static rc_probe_distance_fn rc_probe_distance_native; /* tests only */
static rc_probe_walk_fn rc_probe_walk_native;
static rc_probe_vector_fn rc_probe_vector_native;
static rc_probe_offset_fn rc_probe_offset_native;
static void rc_probe_note(unsigned k) {
    InterlockedIncrement64(&rc_stub_calls[RC_PROBE_CALL+k]);InterlockedIncrement64(&rc_probe_counts[k][rc_thread_class()]);
}
/* mining: the Crystal pass's camera distance (0x73f6a3) from the view points latched in the object's sector. */
static float rc_probe_crystal(void *object,void *camera) {
    rc_probe_note(0);
    const LONG count=InterlockedCompareExchange(&rc_shape_count,0,0);float d;
    if(count<0) d=(rc_probe_distance_native?rc_probe_distance_native:(rc_probe_distance_fn)(void *)(image+rc_calls[RC_PROBE_CALL].target))(object,camera);
    else {
        void *sector=object?rc_shape_sector(*(unsigned char *volatile *)((unsigned char*)object+0x70)):NULL;
        const double now=rc_thread_time();float best=-1.0f;
        for(LONG i=0;sector && i<count && i<(LONG)VIEW_MAX;i++) {
            void *point=rc_shape_points[i];
            if(!point || rc_shape_sectors[i]!=sector) continue;
            __declspec(align(16)) float a[16],b[16];rc_gtr(point,a,now,sector);rc_gtr(object,b,now,sector);
            const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],e=dx*dx+dy*dy+dz*dz;
            if(best<0 || e<best) best=e;
        }
        d=best<0?3.0e38f:best;
    }
    if(d>9.0e6f) InterlockedIncrement64(&rc_probe_crystal_far);
    return d;
}
static void **rc_probe_walk(unsigned k,void *unused,void **out) {
    rc_probe_note(k);
    return (rc_probe_walk_native?rc_probe_walk_native:(rc_probe_walk_fn)(void *)(image+rc_calls[RC_PROBE_CALL+k].target))(unused,out);
}
#define RC_PROBE_WALK(k) static void **rc_probe_walk##k(void *unused,void **out) {return rc_probe_walk(k,unused,out);}
RC_PROBE_WALK(1) RC_PROBE_WALK(2) RC_PROBE_WALK(3) RC_PROBE_WALK(4) RC_PROBE_WALK(5) RC_PROBE_WALK(6) RC_PROBE_WALK(7) RC_PROBE_WALK(8) RC_PROBE_WALK(9)
#undef RC_PROBE_WALK
static void *rc_probe_vector(unsigned k,void *a,float *out,void *camera) {
    rc_probe_note(k);
    return (rc_probe_vector_native?rc_probe_vector_native:(rc_probe_vector_fn)(void *)(image+rc_calls[RC_PROBE_CALL+k].target))(a,out,camera);
}
static void *rc_probe_booster_a(void *a,float *out,void *camera) {return rc_probe_vector(10,a,out,camera);}
static void *rc_probe_booster_b(void *a,float *out,void *camera) {return rc_probe_vector(11,a,out,camera);}
static void *rc_probe_zone921(void *camera,float *out,double time,void *frame) {
    rc_probe_note(12);
    return (rc_probe_offset_native?rc_probe_offset_native:(rc_probe_offset_fn)(void *)(image+rc_calls[RC_PROBE_CALL+12].target))(camera,out,time,frame);
}
/* highway_rows: the inputs and settings of the highway spline parameter, passed through. */
typedef struct {uint64_t tick,key;uint32_t site,attention,before,v[4];unsigned char mover;} rc_hw_row_t;
static rc_hw_row_t rc_hw_ring[RC_HWROW_RING];
static volatile LONG64 rc_hw_rows;
static uint64_t rc_hw_from,rc_hw_to; /* the rows' tick window, inclusive; from 0: none */
typedef void (*rc_hw_set_fn)(void *,float);
static rc_probe_offset_fn rc_hw_offset_native; /* tests only: replaces GetOffset 0x97a300 */
static rc_hw_set_fn rc_hw_set_native;          /* tests only: replaces 0x5f7a50 */
/* The highway is live in the native caller (rdi of Highway::vfunc674, rbx of UniverseFrameUpdate). */
static uint32_t rc_hw_param(void *highway) {return highway?*(volatile uint32_t *)((unsigned char*)highway+0x560):0u;}
static void rc_hw_note(uint32_t site,void *highway,uint32_t before,const uint32_t v[4]) {
    const uint64_t tick=stats.issued;
    if(!rc_hw_from || tick<rc_hw_from || tick>rc_hw_to || !highway) return;
    rc_hw_row_t r;memset(&r,0,sizeof(r));r.tick=tick;r.site=site;r.before=before;memcpy(r.v,v,sizeof(r.v));r.mover=(unsigned char)rc_thread_class();
    const unsigned char *p=highway;r.attention=*(volatile uint32_t *)(p+0x1ec);
    const uint64_t runtime=*(volatile uint64_t *)(p+8);unsigned klass;r.key=runtime?world_identity_key(runtime,&klass):0;
    LONG64 index=InterlockedIncrement64(&rc_hw_rows)-1;rc_hw_ring[(uint64_t)index%RC_HWROW_RING]=r;
}
/* 0x5f4d7a: GetOffset(camera, out, time, highway), the camera relative to the highway. */
static void *rc_hw_offset(void *camera,float *out,double time,void *highway) {
    InterlockedIncrement64(&rc_stub_calls[RC_HWROW_CALL]);const uint32_t before=rc_hw_param(highway);
    void *result=(rc_hw_offset_native?rc_hw_offset_native:(rc_probe_offset_fn)(void *)(image+rc_calls[RC_HWROW_CALL].target))(camera,out,time,highway);
    if(out) {uint32_t v[4];memcpy(v,out,sizeof(v));rc_hw_note(0,highway,before,v);}
    return result;
}
/* 0x5f4fee (k 1) and 0x9fe3fd (k 2): 0x5f7a50(highway, parameter). */
static void rc_hw_set(unsigned k,void *highway,float t) {
    InterlockedIncrement64(&rc_stub_calls[RC_HWROW_CALL+k]);const uint32_t before=rc_hw_param(highway);
    (rc_hw_set_native?rc_hw_set_native:(rc_hw_set_fn)(void *)(image+rc_calls[RC_HWROW_CALL+k].target))(highway,t);
    uint32_t v[4]={0,0,0,0};memcpy(v,&t,4);if(highway) memcpy(v+1,(unsigned char*)highway+0x570,12);
    rc_hw_note(k,highway,before,v);
}
static void rc_hw_set_move(void *highway,float t) {rc_hw_set(1,highway,t);}
static void rc_hw_set_frame(void *highway,float t) {rc_hw_set(2,highway,t);}
static int rc_hw_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&rc_hw_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)RC_HWROW_RING && index<(uint64_t)(count-(LONG64)RC_HWROW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const rc_hw_row_t *r=&rc_hw_ring[index%RC_HWROW_RING];static const char *const sites[3]={"offset","move","frame"};
    float f[4],b;memcpy(f,r->v,sizeof(f));memcpy(&b,&r->before,4);
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s key=%016llx attention=%u mover=%u before=%08x v=%08x,%08x,%08x,%08x f=%.9g,%.9g,%.9g,%.9g b=%.9g",
        (unsigned long long)index,(unsigned long long)r->tick,r->site<3?sites[r->site]:"?",(unsigned long long)r->key,r->attention,(unsigned)r->mover,
        r->before,r->v[0],r->v[1],r->v[2],r->v[3],(double)f[0],(double)f[1],(double)f[2],(double)f[3],(double)b);
    return n>0 && (size_t)n<cap?n:0;
}
/* Before the first admitted frame, with the group armed: rows for ticks from..from+ticks-1. */
static int rc_hw_rows_arm(unsigned ticks,unsigned from) {
    if(rc_hw_from) return stats.active && rc_hw_from==from && rc_hw_to==(uint64_t)from+ticks-1;
    if(!(rc_armed_groups&(1u<<RC_HIGHWAY_ROWS)) || !stats.active || !boot_mode || boot_phase || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !ticks || ticks>3600 || !from || from>1000000) return 0;
    rc_hw_rows=0;rc_hw_from=from;rc_hw_to=(uint64_t)from+ticks-1;return 1;
}
static int rc_pick_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&rc_pick_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)RC_PICK_ROWS && index<(uint64_t)(count-(LONG64)RC_PICK_ROWS))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const rc_pick_row_t *r=&rc_pick_ring[index%RC_PICK_ROWS];
    n=snprintf(out,cap,"index=%llu tick=%llu site=%x two=%u key=%016llx runtime=%llx mover=%u",(unsigned long long)index,
        (unsigned long long)r->tick,r->site,(unsigned)r->two,(unsigned long long)r->key,(unsigned long long)r->runtime,(unsigned)r->mover);
    if(n>0 && r->has_pos && (size_t)n<cap)
        n+=snprintf(out+n,cap-(size_t)n," pos=%.3f,%.3f,%.3f",(double)r->pos[0],(double)r->pos[1],(double)r->pos[2]);
    if(n>0 && r->has_ab && (size_t)n<cap) n+=snprintf(out+n,cap-(size_t)n," a=%u b=%u pend=%u",r->a,r->b,r->pend);
    return n>0 && (size_t)n<cap?n:0;
}
/* Before the first admitted frame, with the policy armed: rows for ticks 1..ticks. */
static int rc_pick_rows_arm(unsigned ticks) {
    if(rc_pick_rows_ticks) return stats.active && rc_pick_rows_ticks==ticks;
    if(!(rc_policy || (rc_armed_groups&((1u<<RC_MINING)|(1u<<RC_XS)))) || !stats.active || !boot_mode || boot_phase || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !ticks || ticks>600) return 0;
    rc_pick_rows=0;rc_pick_rows_ticks=ticks;return 1;
}
/* Reached from a policy thunk: the value the site's load would read, for the object's viewpoint. */
static void *rc_pick(void *object,uint64_t site) {
    InterlockedIncrement64(&rc_picks);
    if(site>=RC_SITES || !rc_page) return NULL;
    const int k=rc_object_k(object,rc_sites[site].object);
    if(k) InterlockedIncrement64(&rc_picks_two);
    InterlockedIncrement64(&rc_site_picks[site]);
    if(rc_pick_rows_ticks) rc_pick_note((uint32_t)site,object,rc_sites[site].object,k);
    if(rc_sites[site].slot==RC_SLOT_UNIVERSE) return rc_page+rc_k_shadow[k];
    return rc_page+rc_k_manager[k];
}
/* The thunk of a policy site (entered by the site's call, rsp = 8 mod 16): saves
 * rax..r11, flags and xmm0..xmm5, calls rc_pick(object, site) and puts the result
 * into the slot of the load's register, so the pops leave only that register changed. */
static unsigned rc_thunk_bytes(unsigned site,unsigned char *p) {
    static const unsigned char head[]={0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,0x9c,0x48,0x81,0xec,0x88,0,0,0,
        0xf3,0x0f,0x7f,0x44,0x24,0x20,0xf3,0x0f,0x7f,0x4c,0x24,0x30,0xf3,0x0f,0x7f,0x54,0x24,0x40,
        0xf3,0x0f,0x7f,0x5c,0x24,0x50,0xf3,0x0f,0x7f,0x64,0x24,0x60,0xf3,0x0f,0x7f,0x6c,0x24,0x70};
    static const unsigned char tail[]={0xf3,0x0f,0x6f,0x44,0x24,0x20,0xf3,0x0f,0x6f,0x4c,0x24,0x30,0xf3,0x0f,0x6f,0x54,0x24,0x40,
        0xf3,0x0f,0x6f,0x5c,0x24,0x50,0xf3,0x0f,0x6f,0x64,0x24,0x60,0xf3,0x0f,0x6f,0x6c,0x24,0x70,
        0x48,0x81,0xc4,0x88,0,0,0,0x9d,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0xc3};
    const rc_site_t *s=&rc_sites[site];unsigned n=0;
    const unsigned reg=((s->prefix[2]>>3)&7u)|(s->prefix[0]&4u?8u:0u); /* ModRM.reg with REX.R */
    const uint32_t slot=reg==0?0xc0u:reg==1?0xb8u:reg==2?0xb0u:reg==8?0xa8u:0u;
    if(!slot || !s->object) return 0;
    memcpy(p+n,head,sizeof(head));n+=sizeof(head);
    if(s->object==RC_OBJ_RDI) {p[n]=0x48;p[n+1]=0x89;p[n+2]=0xf9;} else {p[n]=0x4c;p[n+1]=0x89;p[n+2]=0xe1;} /* mov rcx, rdi / r12 */
    n+=3;p[n++]=0xba;uint32_t index=site;memcpy(p+n,&index,4);n+=4;                                     /* mov edx, site */
    p[n++]=0x48;p[n++]=0xb8;void *fn=(void*)rc_pick;memcpy(p+n,&fn,8);n+=8;                              /* mov rax, rc_pick */
    p[n++]=0xff;p[n++]=0xd0;                                                                                 /* call rax */
    p[n++]=0x48;p[n++]=0x89;p[n++]=0x84;p[n++]=0x24;memcpy(p+n,&slot,4);n+=4;                            /* mov [rsp+slot], rax */
    memcpy(p+n,tail,sizeof(tail));n+=sizeof(tail);
    return n;
}
static void *rc_ancestor(void *object,int klass) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<17;depth++) {
        void **vtable=NULL;rc_is_class_fn is_class=NULL;
        if(!read_local(p,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+0x11e8,&is_class,8) || !is_class) return NULL;
        if(is_class(p,klass)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* The state a written site of ours must be in: the thunk form under the policy, else the page slot. */
static int rc_site_ours(unsigned i) {return rc_policy && rc_sites[i].object?2:1;}
/* field_fill: region content becomes objects from view point 1 at a fixed point of the frame. */
#define RC_FILL_MANAGER 0x6cf21e0u /* the U::Regions::RegionManager; its store at +0x60 */
#define RC_FILL_RADII 0x6cf2980u   /* {begin, end} of rows of 5 floats: field value from [0], radius [3] */
#define RC_FILL_ENTRIES 1024u
#define RC_FILL_DOMAIN 0x58344c4649454c44ULL
#define RC_FILL_AREA_DOMAIN 0x58344c4641524541ULL
typedef struct {
    void (*position)(void *record,float *out,void *zone,void *cell,unsigned char late);
    void *(*emplace)(void *tree,void *out,void *hint,void **key,void *record);
    void *(*node)(void *tree);
    void (*construct)(void *at,void **key,void *record);
    void (*release)(void *node,size_t size);
    void (*refresh)(void *entry);
    void *(*area)(void *sector,float *pos,void *wares,char a,char b,char c,char d,unsigned e,float *factor);
} rc_fill_calls_t;
static const rc_fill_calls_t *rc_fill_test; /* tests only: replaces the native functions */
static int rc_fill_view; /* 'view': the fill runs; 'block': only the render pass's insert is closed */
static volatile LONG64 rc_fill_frames,rc_fill_cells,rc_fill_nodes,rc_fill_spawned;
static rc_fill_calls_t rc_fill_calls(void) {
    if(rc_fill_test) return *rc_fill_test;
    rc_fill_calls_t c;
    c.position=(void (*)(void *,float *,void *,void *,unsigned char))(void *)(image+0xee5b30u);
    c.emplace=(void *(*)(void *,void *,void *,void **,void *))(void *)(image+0xf33dd0u);
    c.node=(void *(*)(void *))(void *)(image+0x6976e0u);
    c.construct=(void (*)(void *,void **,void *))(void *)(image+0xf35460u);
    c.release=(void (*)(void *,size_t))(void *)(image+0x174f310u);
    c.refresh=(void (*)(void *))(void *)(image+0xee6a80u);
    c.area=(void *(*)(void *,float *,void *,char,char,char,char,unsigned,float *))(void *)(image+0x78a690u);
    return c;
}
/* A node's resource area on a word from its position: the same whatever walks the dirty fields and on whichever thread. */
static void *rc_fill_area_tls(unsigned char *tls,void *sector,float *pos,void *wares,char a,char b,char c,char d,unsigned e,float *factor) {
    const rc_fill_calls_t calls=rc_fill_calls();
    if(!tls) return calls.area(sector,pos,wares,a,b,c,d,e,factor);
    uint32_t bits[3];memcpy(bits,pos,sizeof(bits));
    const uint64_t word=lockstep_seed(RC_FILL_AREA_DOMAIN,((uint64_t)bits[0]<<32)|bits[1],bits[2],0,0,0);
    uint64_t saved;memcpy(&saved,tls+0x318,8);memcpy(tls+0x318,&word,8);
    void *const area=calls.area(sector,pos,wares,a,b,c,d,e,factor);
    memcpy(tls+0x318,&saved,8);
    return area;
}
static void *rc_fill_area(void *sector,float *pos,void *wares,char a,char b,char c,char d,unsigned e,float *factor) {
    InterlockedIncrement64(&rc_stub_calls[RC_FILL_AREA_CALL]);
    void **tls=*(void ***)((unsigned char *)NtCurrentTeb()+0x58);
    return rc_fill_area_tls(tls?(unsigned char *)tls[0]:NULL,sector,pos,wares,a,b,c,d,e,factor);
}
/* The render pass's refresh: with the fill running it has done this frame's already. */
static void rc_fill_render_refresh(void *entry) {
    InterlockedIncrement64(&rc_stub_calls[RC_FILL_REFRESH_CALL]);
    if(!rc_fill_view) rc_fill_calls().refresh(entry);
}
static void *rc_fill_slot(void *object,unsigned offset) {return (*(void ***)object)[offset/8u];}
/* The field's radius, squared, as Field::vfunc10 and the render pass take it. */
static float rc_fill_radius_sq(unsigned char *field) {
    const float value=((float (*)(void *))rc_fill_slot(field,0x20))(field);
    float *const *table=*(float *const *const *)(image+RC_FILL_RADII);
    const float *p=table[0];
    while(p!=table[1] && !(*p<=value)) p+=5;
    if(p==table[1]) p=table[1]-5;
    return p[3]*p[3];
}
/* Into the field's pending tree (root +0x58, leftmost +0x70, count +0x78), as the render pass does. */
static void rc_fill_insert(const rc_fill_calls_t *c,unsigned char *field,void *cell,void *record) {
    unsigned char *tree=field+0x50;void *key=cell;
    EnterCriticalSection((LPCRITICAL_SECTION)(field+0x20));
    if(*(void **)(field+0x58)) {unsigned char out[16];c->emplace(tree,out,NULL,&key,record);}
    else {
        unsigned char *node=c->node(tree);
        *(void **)(field+0x58)=node;*(void **)(field+0x70)=node;*(void **)node=tree;
        c->construct(node+0x20,&key,record);
        *(uint64_t *)(field+0x78)=1;*(uint32_t *)(node+0x18)=1;
    }
    LeaveCriticalSection((LPCRITICAL_SECTION)(field+0x20));
}
/* One tree node of an entry (key +0x20 the field, list +0x28, count +0x30). */
static void rc_fill_field(const rc_fill_calls_t *c,unsigned char *cell,unsigned char *hit,void *zone,const float *at) {
    unsigned char *const field=*(unsigned char **)(hit+0x20);
    if(!field || !((char (*)(void *))rc_fill_slot(field,0xd8))(field)) return;
    void *const *variants=((void *const *(*)(void *))rc_fill_slot(field,0xe8))(field);
    if(variants[0]==variants[1]) return;
    EnterCriticalSection((LPCRITICAL_SECTION)(field+0x20));
    const int busy=*(uint64_t *)(field+0x78)!=0;
    LeaveCriticalSection((LPCRITICAL_SECTION)(field+0x20));
    if(busy) return;
    const float radius=rc_fill_radius_sq(field);
    unsigned char *const list=*(unsigned char **)(hit+0x28);
    for(unsigned char *node=*(unsigned char **)list,*next;node!=list;node=next) {
        next=*(unsigned char **)node;
        if(node[0x90]) continue;
        InterlockedIncrement64(&rc_fill_nodes);
        __declspec(align(16)) float m[16];
        c->position(node+0x10,m,zone,cell,0);
        const float dx=m[0]-at[0],dy=m[1]-at[1],dz=m[2]-at[2];
        if(!(dx*dx+dy*dy+dz*dz<=radius)) continue;
        rc_fill_insert(c,field,cell,node+0x10);
        unsigned char *const prev=*(unsigned char **)(node+8);
        *(unsigned char **)prev=next;*(unsigned char **)(next+8)=prev;--*(uint64_t *)(hit+0x30);
        c->release(node,0xa0);
        InterlockedIncrement64(&rc_fill_spawned);
    }
}
/* The next tree node in order (parent +0, left +8, right +0x10); the header is the root's parent, its left the root. */
static unsigned char *rc_fill_next(unsigned char *n) {
    unsigned char *right=*(unsigned char **)(n+0x10);
    if(right) {
        for(unsigned char *left;(left=*(unsigned char **)(right+8))!=NULL;) right=left;
        return right;
    }
    unsigned char *parent=*(unsigned char **)n;
    while(*(unsigned char **)(parent+8)!=n) {n=parent;parent=*(unsigned char **)n;}
    return parent;
}
/* 0 only when the store has more entries than the copy holds: never a partial pass. */
static int rc_fill_frame_tls(void *viewpoint,uint64_t tick,unsigned char *tls) {
    unsigned char *const zone=rc_view_zone;int32_t attention=0;
    if(!tls || !zone || !rc_view_sector) return 1;
    const int insert=!rc_is_class(zone,0x33) && read_local(zone+0x1ec,&attention,4) && attention>=8;
    unsigned char *const manager=*(unsigned char **)(image+RC_FILL_MANAGER);
    if(!manager) return 1;
    unsigned char *const store=manager+0x60;
    static unsigned char *copy[RC_FILL_ENTRIES];size_t count;
    EnterCriticalSection((LPCRITICAL_SECTION)(store+0x68));
    unsigned char **const begin=*(unsigned char ***)(store+0x40),**const end=*(unsigned char ***)(store+0x48);
    count=(size_t)(end-begin);
    if(count<=RC_FILL_ENTRIES && count) memcpy(copy,begin,count*sizeof(*copy));
    LeaveCriticalSection((LPCRITICAL_SECTION)(store+0x68));
    if(count>RC_FILL_ENTRIES) return 0;
    const rc_fill_calls_t c=rc_fill_calls();
    double now;memcpy(&now,tls+0x300,8);
    __declspec(align(16)) float at[16];rc_gtr(viewpoint,at,now,zone);
    uint64_t saved;memcpy(&saved,tls+0x318,8);
    const uint64_t word=lockstep_seed(RC_FILL_DOMAIN,tick,0,0,0,0);memcpy(tls+0x318,&word,8);
    for(size_t i=0;i<count;i++) {
        unsigned char *const entry=copy[i];
        EnterCriticalSection((LPCRITICAL_SECTION)(entry+0x8f0));
        unsigned char *const cell=*(unsigned char **)(entry+0x8e0);
        if(cell && !(*(uint32_t *)(cell+0x1e4)&0x20000u)) {
            c.refresh(entry);
            InterlockedIncrement64(&rc_fill_cells);
            if(insert && ((char (*)(void *,int32_t,int))rc_fill_slot(cell,0x1580))(cell,*(int32_t *)(cell+0x1ec),0)) {
                unsigned char *const header=entry+0x10;
                for(unsigned char *walk=*(unsigned char **)(entry+0x30);walk!=header;walk=rc_fill_next(walk)) rc_fill_field(&c,cell,walk,zone,at);
            }
        }
        LeaveCriticalSection((LPCRITICAL_SECTION)(entry+0x8f0));
    }
    memcpy(tls+0x318,&saved,8);
    InterlockedIncrement64(&rc_fill_frames);
    return 1;
}
static int rc_fill_frame(void *viewpoint,uint64_t tick) {
    void **tls=*(void ***)((unsigned char *)NtCurrentTeb()+0x58);
    return rc_fill_frame_tls(viewpoint,tick,tls?(unsigned char *)tls[0]:NULL);
}
#include "lockstep_view_scenes.h"
static int rc_install_group(unsigned g) {
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].group==g) {
        if(rc_site_ours(i)==2) {
            unsigned char original[7],code[7];rc_site_original(i,original);
            if(!rc_site_thunk_form(i,code) || !view_cas_bytes(rc_sites[i].rva,original,code,7)) return 0;
        }
        else if(!rc_cas4(rc_sites[i].rva+3,rc_disp_to(rc_sites[i].rva,image+rc_site_target(i)),rc_disp_to(rc_sites[i].rva,rc_slot(i)))) return 0;
        rc_site_done[i]=1;
    }
    for(unsigned i=0;i<RC_CALLS;i++) if(rc_calls[i].group==g) {
        if(!rc_cas4(rc_calls[i].rva+1,rc_rel_to(rc_calls[i].rva,image+rc_calls[i].target),rc_rel_to(rc_calls[i].rva,rc_stub(i)))) return 0;
        rc_call_done[i]=1;
    }
    for(unsigned i=0;i<RC_JUMPS;i++) if(rc_jumps[i].group==g) {
        unsigned char code[7];
        if(!rc_jump_form(i,code) || !view_cas_bytes(rc_jumps[i].rva,rc_jumps[i].original,code,7)) return 0;
        rc_jump_done[i]=1;
    }
    if(g==RC_VIEW_SCENES && !vsc_install()) return 0;
    rc_installed_groups|=1u<<g;rc_patched=1;return 1;
}
/* Restore every site and call still ours; 1 when none is left patched. */
static int rc_remove_patches(void) {
    int all=1;
    for(unsigned i=0;i<RC_SITES;i++) if(rc_site_done[i]) {
        int state=rc_site_state(i);unsigned char original[7],code[7];
        if(state==1 && rc_cas4(rc_sites[i].rva+3,rc_disp_to(rc_sites[i].rva,rc_slot(i)),rc_disp_to(rc_sites[i].rva,image+rc_site_target(i)))) rc_site_done[i]=0;
        else if(state==2 && (rc_site_original(i,original),rc_site_thunk_form(i,code)) && view_cas_bytes(rc_sites[i].rva,code,original,7)) rc_site_done[i]=0;
        else if(state==0) rc_site_done[i]=0;
        else all=0;
    }
    for(unsigned i=0;i<RC_CALLS;i++) if(rc_call_done[i]) {
        int state=rc_call_state(i);
        if(state==1 && rc_cas4(rc_calls[i].rva+1,rc_rel_to(rc_calls[i].rva,rc_stub(i)),rc_rel_to(rc_calls[i].rva,image+rc_calls[i].target))) rc_call_done[i]=0;
        else if(state==0) rc_call_done[i]=0;
        else all=0;
    }
    for(unsigned i=0;i<RC_JUMPS;i++) if(rc_jump_done[i]) {
        int state=rc_jump_state(i);unsigned char code[7];
        if(state==1 && rc_jump_form(i,code) && view_cas_bytes(rc_jumps[i].rva,code,rc_jumps[i].original,7)) rc_jump_done[i]=0;
        else if(state==0) rc_jump_done[i]=0;
        else all=0;
    }
    if(!vsc_remove()) all=0;
    if(all) rc_installed_groups=0;
    return all;
}
static unsigned char *rc_alloc_near(unsigned size,DWORD protect) {
    uintptr_t at=(uintptr_t)(image+rc_sites[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,protect);
            if(!p) continue;
            int fits=1;
            for(unsigned i=0;fits && i<RC_SITES;i++) fits=rc_reach((intptr_t)p-(intptr_t)(image+rc_sites[i].rva+7)) &&
                rc_reach((intptr_t)(p+size)-(intptr_t)(image+rc_sites[i].rva+7));
            for(unsigned i=0;fits && i<RC_CALLS;i++) fits=rc_reach((intptr_t)p-(intptr_t)(image+rc_calls[i].rva+5)) &&
                rc_reach((intptr_t)(p+size)-(intptr_t)(image+rc_calls[i].rva+5));
            for(unsigned i=0;fits && i<RC_JUMPS;i++) fits=rc_reach((intptr_t)p-(intptr_t)(image+rc_jumps[i].rva+5)) &&
                rc_reach((intptr_t)(p+size)-(intptr_t)(image+rc_jumps[i].rva+5));
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* The replacement behind stub i (call i of rc_calls). */
static void *rc_stub_target(unsigned i) {
    void *const targets[RC_CALLS]={(void*)rc_camera_zone,(void*)rc_camera_sector,(void*)rc_active_camera,(void*)rc_effect_creator,
                                   (void*)rc_controllable_a,(void*)rc_controllable_b,(void*)rc_scene_sector6,(void*)rc_scene_zone7,
                                   (void*)rc_scene_active8,(void*)rc_scene_sector9,(void*)rc_scene_sector10,(void*)rc_context_events,
                                   (void*)rc_zone_test,(void*)rc_lane_camera,(void*)rc_activate_pass,(void*)rc_activate_update_a,
                                   (void*)rc_activate_update_b,(void*)rc_collect,(void*)rc_shape_update,(void*)rc_shape_interval,
                                   (void*)rc_xs_external,(void*)rc_xs_distance,(void*)rc_probe_crystal,
                                   (void*)rc_probe_walk1,(void*)rc_probe_walk2,(void*)rc_probe_walk3,(void*)rc_probe_walk4,(void*)rc_probe_walk5,
                                   (void*)rc_probe_walk6,(void*)rc_probe_walk7,(void*)rc_probe_walk8,(void*)rc_probe_walk9,
                                   (void*)rc_probe_booster_a,(void*)rc_probe_booster_b,(void*)rc_probe_zone921,(void*)rc_xs_target,
                                   (void*)rc_hw_offset,(void*)rc_hw_set_move,(void*)rc_hw_set_frame,(void*)rc_sound_float,(void*)rc_sound_choice,
                                   (void*)vsc_step,(void*)rc_fill_area,(void*)rc_fill_render_refresh,
                                   (void*)tq_sim,(void*)tq_check,(void*)tq_late,(void*)tp_relative_a,(void*)tp_relative_b,(void*)tp_relative_c,
                                   (void*)tp_place_a,(void*)tp_place_b,(void*)tp_filter_call,(void*)tp_draw};
    return i<RC_CALLS?targets[i]:NULL;
}
/* Bytes before the stub's jump: the call's move, if any. */
static unsigned rc_stub_move(unsigned i) {return i<RC_CALLS && rc_calls[i].move[0]?3u:0u;}
static int rc_write_stubs(void) {
    DWORD old;if(!VirtualProtect(rc_stubs,RC_STUB_SIZE,PAGE_READWRITE,&old)) return 0;
    for(unsigned i=0;i<RC_CALLS;i++) {
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};unsigned at=rc_stub_move(i);
        void *target=rc_stub_target(i);memcpy(rc_stub(i),rc_calls[i].move,at);memcpy(rc_stub(i)+at,jump,6);memcpy(rc_stub(i)+at+6,&target,8);
    }
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object) {
        unsigned char *code=rc_thunk(i);memset(code,0xcc,RC_THUNK_SIZE);
        if(!rc_thunk_bytes(i,code)) {VirtualProtect(rc_stubs,RC_STUB_SIZE,old,&old);return 0;}
    }
    for(unsigned i=0;i<RC_JUMPS;i++) {memset(rc_jump_stub(i),0xcc,0x80);rc_jump_stub_bytes(i,rc_jump_stub(i),image+rc_jumps[i].resume);}
    DWORD ignored;int ok=VirtualProtect(rc_stubs,RC_STUB_SIZE,PAGE_EXECUTE_READ,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),rc_stubs,RC_STUB_SIZE);return ok;
}
static int rc_stubs_owned(void) {
    for(unsigned i=0;i<RC_CALLS;i++) {
        unsigned at=rc_stub_move(i);void *t=NULL;
        if(memcmp(rc_stub(i),rc_calls[i].move,at) || !read_local(rc_stub(i)+at+6,&t,8) || t!=rc_stub_target(i) ||
           rc_stub(i)[at]!=0xff || rc_stub(i)[at+1]!=0x25) return 0;
    }
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object) {
        unsigned char expected[RC_THUNK_SIZE];unsigned n=rc_thunk_bytes(i,expected);
        if(!n || memcmp(rc_thunk(i),expected,n)) return 0;
    }
    for(unsigned i=0;i<RC_JUMPS;i++) {
        unsigned char expected[0x80];unsigned n=rc_jump_stub_bytes(i,expected,image+rc_jumps[i].resume);
        if(memcmp(rc_jump_stub(i),expected,n)) return 0;
    }
    return 1;
}
/* Before the first admitted frame, on the MoveWorker: check one group and its
 * prerequisites, allocate the pages; the sites follow at the first rc_frame. */
static int rc_arm(int group) {
    if(group<0 || group>=RC_GROUPS) return 0;
    if(rc_armed_groups&(1u<<group)) return stats.active!=0; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!view_installed || !mt_installed || (group==RC_REGION && mt_region.mode!=MT_SYNC) || (group==RC_TRAFFIC && mt_traffic.mode!=MT_SYNC) ||
       (group==RC_POLICY && !(rc_armed_groups&((1u<<RC_TRAFFIC)|(1u<<RC_HIGHWAY)))) ||
       (group==RC_VIEW_SCENES && !(rc_armed_groups&(1u<<RC_SCENES))) ||
       (group==RC_FIELD_FILL && !(rc_armed_groups&(1u<<RC_FIELDS))) ||
       (group==RC_TRAFFIC_PASS && !(rc_armed_groups&(1u<<RC_TRAFFIC_ROWS)))) {rc_stage="needs";return 0;}
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll")) {rc_stage="foreign_module";return 0;}
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].group==group && rc_site_state(i)!=0) {rc_stage="site";return 0;}
    for(unsigned i=0;i<RC_CALLS;i++) if(rc_calls[i].group==group && rc_call_state(i)!=0) {rc_stage="site";return 0;}
    for(unsigned i=0;i<RC_JUMPS;i++) if(rc_jumps[i].group==group && rc_jump_state(i)!=0) {rc_stage="site";return 0;}
    if(!rc_pins_ok(1u<<group)) {rc_stage="pins";return 0;}
    if(group==RC_VIEW_SCENES && !vsc_slots_native()) {rc_stage="site";return 0;}
    if(!rc_page) rc_page=rc_alloc_near(RC_PAGE_SIZE,PAGE_READWRITE);
    if(!rc_stubs) rc_stubs=rc_alloc_near(RC_STUB_SIZE,PAGE_EXECUTE_READ);
    if(!rc_page || !rc_stubs) {rc_stage="page";return 0;}
    if(!rc_armed_groups) {
        memset(rc_page,0,RC_PAGE_SIZE);
        void *manager=rc_page+RC_MANAGER,*shadow=rc_page+RC_SHADOW,*manager2=rc_page+RC_MANAGER2;rc_policy=0;
        memcpy(rc_page,&manager,8);memcpy(rc_page+8,&shadow,8);memcpy(rc_page+16,&manager2,8);
        if(!rc_write_stubs()) {rc_stage="stubs";return 0;}
        rc_next=0;rc_updates=rc_native_frames=rc_viewpoint_id=rc_viewpoint_key=rc_install_tick=rc_install_waits=0;rc_tick=UINT64_MAX;rc_failures=0;rc_restored=0;
        rc_view_zone=rc_view_sector=NULL;for(unsigned i=0;i<RC_CALLS;i++) rc_stub_calls[i]=0;rc_installed_groups=0;rc_effect_rows=0;
        rc_next2=0;rc_vp2_zone=rc_vp2_sector=NULL;rc_vp2_id=rc_vp2_key=rc_vp2_updates=0;rc_context_skipped=0;
        rc_view_sector58=rc_vp2_cluster=NULL;rc_picks=rc_picks_two=rc_alternations=0;rc_pick_rows=0;rc_pick_rows_ticks=0;
        for(unsigned i=0;i<RC_SITES;i++) rc_site_picks[i]=0;
        InterlockedExchange(&rc_shape_count,-1);rc_shape_aligned=rc_shape_unmapped=rc_shape_near=rc_shape_far=rc_shape_native=0;
        rc_mining_calls=rc_mining_near_count=rc_mining_foreign=rc_xs_near=rc_xs_far=rc_xs_targets=rc_probe_crystal_far=0;rc_stop=RC_STOP_REASON;
        memset((void *)rc_probe_counts,0,sizeof(rc_probe_counts));memset(rc_jump_done,0,sizeof(rc_jump_done));
        rc_hw_rows=0;rc_hw_from=rc_hw_to=0;rc_hw_speed_keyed=rc_hw_speed_unmapped=0;
        rc_uncovered_ticks=0;rc_uncovered_now=0;rc_shape_unkeyed=0;rc_sound_seeds=0;
        rc_fill_view=0;rc_fill_frames=rc_fill_cells=rc_fill_nodes=rc_fill_spawned=0;tq_reset();tp_reset();
        rc_standins_max=2u;rc_more=0;memset(rc_k_next,0,sizeof(rc_k_next));
    }
    if(group==RC_SHAPE_REFRESH) memset((void *)rc_shape_keys,0,sizeof(rc_shape_keys));
    if(group==RC_POLICY) rc_policy=1;
    if(group==RC_VIEW_SCENES) vsc_armed=1;
    rc_armed_groups|=1u<<group;rc_mode=RC_VIEW;rc_installed=1;rc_stage="armed";return 1;
}
/* native_gate_viewpoint_standins: how many view points the policy answers for (2..4), before the first frame. */
static int rc_standins_arm(unsigned n) {
    if(n<2u || n>VIEW_MAX || !rc_policy || !rc_installed || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    rc_standins_max=n;return 1;
}
/* Slot 6 of every admitted frame, after the view points latch: the stand-in
 * for the next region and traffic passes and the next frame's simulation. */
static int rc_frame(uint64_t tick) {
    if(!rc_installed || !stats.active || GetCurrentThreadId()!=ui_thread || tick!=stats.issued) return rc_fail("frame_guard");
    void *viewpoint=NULL;uint64_t id=0;
    if(view_latched && view_count) {
        viewpoint=view_points[0];
        if(!read_local((unsigned char*)viewpoint+8,&id,8) || id!=view_ids[0]) {viewpoint=NULL;id=0;}
    }
    if(!viewpoint) {
        unsigned char *universe=*(unsigned char*volatile*)(image+RC_UNIVERSE);void *player=NULL,*camera=NULL;
        if(universe) {read_local(universe+0x230,&player,8);read_local(universe+0x3d0,&camera,8);}
        InterlockedExchangePointer(rc_manager_camera(),NULL);
        InterlockedExchangePointer(rc_shadow_player(),player);InterlockedExchangePointer(rc_shadow_camera(),camera);
        ++rc_native_frames;
    } else {
        unsigned char *camera=rc_page+RC_CAMERAS+rc_next*RC_CAMERA_SIZE;
        rc_view_zone=rc_ancestor(viewpoint,0x6d);rc_view_sector=rc_ancestor(viewpoint,0xf);rc_view_sector58=rc_ancestor(viewpoint,0x58);
        rc_write_camera(camera,viewpoint);MemoryBarrier();
        InterlockedExchangePointer(rc_shadow_player(),viewpoint);InterlockedExchangePointer(rc_shadow_camera(),camera);
        InterlockedExchangePointer(rc_manager_camera(),camera);rc_next^=1;
    }
    if(vsc_armed && viewpoint && !vsc_frame(tick)) return rc_fail("view_scenes");
    /* Viewpoint 2 for the scene choice: the first view point in slot order in another sector, else the second. */
    void *viewpoint2=NULL;uint64_t id2=0;
    if(viewpoint && view_count>=2) {
        unsigned chosen=1;
        for(unsigned i=1;i<view_count && i<VIEW_MAX;i++) {
            void *p=view_points[i];uint64_t pid=0;
            if(!p || !read_local((unsigned char*)p+8,&pid,8) || pid!=view_ids[i]) continue;
            void *sector=rc_ancestor(p,0x58);
            if(sector && sector!=rc_view_sector58) {chosen=i;break;}
        }
        viewpoint2=view_points[chosen];
        if(!viewpoint2 || !read_local((unsigned char*)viewpoint2+8,&id2,8) || id2!=view_ids[chosen]) {viewpoint2=NULL;id2=0;}
    }
    if(!viewpoint2) {
        InterlockedExchangePointer(rc_manager2_camera(),NULL);rc_vp2_zone=rc_vp2_sector=rc_vp2_cluster=NULL;
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x230),*rc_shadow_player());
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x3d0),*rc_shadow_camera());
    } else {
        unsigned char *camera2=rc_page+RC_CAMERAS2+rc_next2*RC_CAMERA_SIZE;
        rc_vp2_zone=rc_ancestor(viewpoint2,0x6d);rc_vp2_sector=rc_ancestor(viewpoint2,0x58);rc_vp2_cluster=rc_ancestor(viewpoint2,0xf);
        rc_write_camera(camera2,viewpoint2);MemoryBarrier();
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x230),viewpoint2);
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x3d0),camera2);
        InterlockedExchangePointer(rc_manager2_camera(),camera2);rc_next2^=1;++rc_vp2_updates;
    }
    unsigned klass2;rc_vp2_key=id2?world_identity_key(id2,&klass2):0;rc_vp2_id=id2;
    /* Stand-ins 2..3: the first view point of each further sector, while view point 2 is in a sector of its own. */
    unsigned more=0;
    if(rc_standins_max>2u && viewpoint2 && rc_vp2_sector && rc_vp2_sector!=rc_view_sector58) {
        for(unsigned i=1;i<view_count && i<VIEW_MAX && 2u+more<rc_standins_max;i++) {
            void *p=view_points[i];uint64_t pid=0;
            if(!p || !read_local((unsigned char*)p+8,&pid,8) || pid!=view_ids[i]) continue;
            void *sector=rc_ancestor(p,0x58);int seen=!sector || sector==rc_view_sector58 || sector==rc_vp2_sector;
            for(unsigned k=2;!seen && k<2u+more;k++) seen=rc_k_sector[k]==sector;
            if(seen) continue;
            const unsigned k=2u+more;unsigned char *camera=rc_page+rc_k_cameras[k]+rc_k_next[k]*RC_CAMERA_SIZE;
            rc_k_zone[k]=rc_ancestor(p,0x6d);rc_k_sector[k]=sector;rc_k_cluster[k]=rc_ancestor(p,0xf);
            rc_write_camera(camera,p);MemoryBarrier();
            InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x230),p);
            InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x3d0),camera);
            InterlockedExchangePointer(rc_k_manager_camera(k),camera);rc_k_next[k]^=1;++more;
        }
    }
    InterlockedExchange(&rc_more,(LONG)more);
    for(unsigned k=2u+more;k<VIEW_MAX;k++) { /* unused: no camera, the shadow as view point 1's */
        InterlockedExchangePointer(rc_k_manager_camera(k),NULL);rc_k_sector[k]=rc_k_cluster[k]=rc_k_zone[k]=NULL;
        InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x230),*rc_shadow_player());
        InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x3d0),*rc_shadow_camera());
    }
    /* View points neither stand-in answers for: no scene, no traffic, no highway parameter near them. */
    rc_uncovered_now=0;
    for(unsigned i=1;viewpoint && i<view_count && i<VIEW_MAX;i++) {
        void *p=view_points[i];uint64_t pid=0;
        if(!p || !read_local((unsigned char*)p+8,&pid,8) || pid!=view_ids[i]) continue;
        void *sector=rc_ancestor(p,0x58);
        int covered=!sector || sector==rc_view_sector58 || sector==rc_vp2_sector;
        for(unsigned k=2;!covered && k<2u+(unsigned)rc_more;k++) covered=rc_k_sector[k]==sector;
        if(!covered) ++rc_uncovered_now;
    }
    if(rc_uncovered_now) ++rc_uncovered_ticks;
    rc_shape_latch();
    if(rc_fill_view && viewpoint && (rc_installed_groups&(1u<<RC_FIELD_FILL)) && !rc_fill_frame(viewpoint,tick)) return rc_fail("field_fill_entries");
    unsigned klass;rc_viewpoint_key=id?world_identity_key(id,&klass):0;
    rc_viewpoint_id=id;rc_tick=tick;++rc_updates;return 1;
}
/* Gate tick, main thread, right after mt_join: the sites of every armed group,
 * once an admitted frame has set the stand-in, while the MoveWorker is parked
 * and no armed group's pass runs; otherwise again at the next gate tick. */
static int rc_join_point(uint64_t tick) {
    unsigned pending=rc_armed_groups&~rc_installed_groups;
    if(!rc_installed || !pending || !rc_updates) return 1;
    if(!mt_worker_parked() || !rc_passes_idle()) {
        if(++rc_install_waits>=RC_INSTALL_WAITS) return rc_fail("busy");
        return 1;
    }
    for(unsigned g=0;g<RC_GROUPS;g++) if((pending&(1u<<g)) && !rc_install_group(g)) {rc_remove_patches();return rc_fail("cas");}
    rc_install_tick=tick;rc_stage="installed";return 1;
}
static int rc_owned(void) {
    if(!rc_patched) return 1;
    for(unsigned i=0;i<RC_SITES;i++) if(rc_site_done[i] && rc_site_state(i)!=rc_site_ours(i)) return 0;
    for(unsigned i=0;i<RC_CALLS;i++) if(rc_call_done[i] && rc_call_state(i)!=1) return 0;
    for(unsigned i=0;i<RC_JUMPS;i++) if(rc_jump_done[i] && rc_jump_state(i)!=1) return 0;
    void *slots[3];memcpy(slots,rc_page,24);
    if(slots[0]!=rc_page+RC_MANAGER || slots[1]!=rc_page+RC_SHADOW || slots[2]!=rc_page+RC_MANAGER2) return 0;
    if(!rc_stubs_owned() || !vsc_owned()) return 0;
    void *camera=*rc_manager_camera(),*camera2=*rc_manager2_camera();
    for(unsigned k=2;k<VIEW_MAX;k++) {
        void *ck=*rc_k_manager_camera(k);
        if(ck && ck!=rc_page+rc_k_cameras[k] && ck!=rc_page+rc_k_cameras[k]+RC_CAMERA_SIZE) return 0;
    }
    return (!camera || camera==rc_page+RC_CAMERAS || camera==rc_page+RC_CAMERAS+RC_CAMERA_SIZE) &&
           (!camera2 || camera2==rc_page+RC_CAMERAS2 || camera2==rc_page+RC_CAMERAS2+RC_CAMERA_SIZE);
}
/* A group's own calls and jumps still in the state written. */
static int rc_group_owned(unsigned g) {
    for(unsigned i=0;i<RC_CALLS;i++) if(rc_calls[i].group==g && rc_call_done[i] && rc_call_state(i)!=1) return 0;
    for(unsigned i=0;i<RC_JUMPS;i++) if(rc_jumps[i].group==g && rc_jump_done[i] && rc_jump_state(i)!=1) return 0;
    return 1;
}
static unsigned rc_group_stop(unsigned g) {
    return g==RC_MINING?RC_MINING_STOP:g==RC_XS?RC_XS_STOP:g==RC_PROBE?RC_PROBE_STOP:g==RC_HIGHWAY_ROWS?RC_HIGHWAY_ROWS_STOP:RC_STOP_REASON;
}
/* The stop reason for the gate after a failed check or join: a group's own (166..169) or 130. */
static unsigned rc_stop_reason(void) {return rc_stop;}
/* Main-thread tick check; a failure stops the gate with the failing group's reason (mining 166, xs_detail 167,
 * camera_probe 168, highway_rows 169), else 130. */
static int rc_check(void) {
    if(InterlockedCompareExchange64(&rc_failures,0,0)) return 0;
    static const unsigned own[4]={RC_MINING,RC_XS,RC_PROBE,RC_HIGHWAY_ROWS};
    for(unsigned k=0;k<4;k++) if(rc_armed_groups&(1u<<own[k])) {
        const char *why=!rc_group_owned(own[k])?"site_changed":!rc_pins_ok(1u<<own[k])?"block_changed":NULL;
        if(why) {rc_stop=rc_group_stop(own[k]);return rc_fail(why);}
    }
    const char *why=!rc_owned()?"site_changed":!rc_pins_ok(rc_armed_groups)?"block_changed":NULL;
    if(why) rc_stop=RC_STOP_REASON;
    return why?rc_fail(why):1;
}
static void rc_restore(void) {
    if(!rc_installed) return;
    InterlockedExchange(&vsc_active,0); /* no more steps of C and D, whatever the patches do next */
    InterlockedExchange(&rc_shape_count,-1);
    if(rc_page) {
        unsigned char *universe=*(unsigned char*volatile*)(image+RC_UNIVERSE);void *player=NULL,*camera=NULL;
        if(universe) {read_local(universe+0x230,&player,8);read_local(universe+0x3d0,&camera,8);}
        InterlockedExchangePointer(rc_manager_camera(),NULL);InterlockedExchangePointer(rc_manager2_camera(),NULL);
        InterlockedExchangePointer(rc_shadow_player(),player);InterlockedExchangePointer(rc_shadow_camera(),camera);
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x230),player);
        InterlockedExchangePointer((void *volatile *)(rc_page+RC_SHADOW2+0x3d0),camera);
        InterlockedExchange(&rc_more,0);
        for(unsigned k=2;k<VIEW_MAX;k++) {
            InterlockedExchangePointer(rc_k_manager_camera(k),NULL);
            InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x230),player);
            InterlockedExchangePointer((void *volatile *)(rc_page+rc_k_shadow[k]+0x3d0),camera);
        }
    }
    if(rc_patched) {
        if(!rc_passes_idle()) {rc_stage="restore_busy";return;}
        if(!rc_remove_patches()) return;
        rc_patched=0;
    }
    rc_restored=1;rc_installed=0;rc_armed_groups=0;vsc_armed=0;
}
static int rc_status(char *out,size_t capacity) {
    char groups[288]="-",installed[288]="-",site_picks[240]="-",scenes[220]="0",fill[120]="off",rows[120]="off",pass[320];tp_status(pass,sizeof(pass));int a=0,b=0,c=0;uint32_t attempts=0,culls=0;
    if(vsc_armed) vsc_status(scenes,sizeof(scenes));
    if(rc_armed_groups&(1u<<RC_TRAFFIC_ROWS)) snprintf(rows,sizeof(rows),"early:%llu,late:%llu,sim:%llu,outside:%llu",
        (unsigned long long)tq_early_count,(unsigned long long)tq_late_count,(unsigned long long)tq_sim_count,(unsigned long long)tq_outside_count);
    if(rc_armed_groups&(1u<<RC_FIELD_FILL)) snprintf(fill,sizeof(fill),"%s,frames:%llu,cells:%llu,nodes:%llu,spawned:%llu",rc_fill_view?"view":"block",
        (unsigned long long)rc_fill_frames,(unsigned long long)rc_fill_cells,(unsigned long long)rc_fill_nodes,(unsigned long long)rc_fill_spawned);
    uint64_t walk[3]={0,0,0};for(unsigned k=1;k<=9;k++) for(unsigned t=0;t<3;t++) walk[t]+=(uint64_t)rc_probe_counts[k][t];
    for(unsigned i=0;i<RC_SITES;i++) if(rc_sites[i].object)
        c+=snprintf(site_picks+c,sizeof(site_picks)-(size_t)c,"%s%llu",c?"/":"",(unsigned long long)rc_site_picks[i]);
    read_local(image+RC_EFFECT_ATTEMPTS,&attempts,4);read_local(image+RC_EFFECT_CULLS,&culls,4);
    for(unsigned g=0;g<RC_GROUPS;g++) {
        if(rc_armed_groups&(1u<<g)) a+=snprintf(groups+a,sizeof(groups)-(size_t)a,"%s%s",a?",":"",rc_group_names[g]);
        if(rc_installed_groups&(1u<<g)) b+=snprintf(installed+b,sizeof(installed)-(size_t)b,"%s%s",b?",":"",rc_group_names[g]);
    }
    int n=snprintf(out,capacity,"mode=%s installed=%d frame=%llu viewpoint=%llx key=%016llx updates=%llu native_frames=%llu restored=%d stage=%s "
        "failures=%lld groups=%s patched=%s stub_calls=%llu/%llu/%llu/%llu/%llu/%llu install_tick=%llu install_waits=%llu effects=%u/%u effect_rows=%llu key2=%016llx scene_calls=%llu "
        "context_calls=%llu context_skipped=%llu policy=%s picks=%llu/%llu alternations=%llu pick_rows=%lld site_picks=%s "
        "shape_refresh=%llu/%llu aligned=%llu unmapped=%llu near=%llu far=%llu shape_native=%llu "
        "mining=%llu/%llu/%llu crystal=%llu/%llu/%llu,far:%llu xs=%llu/%llu/%llu/%llu xs_target=%llu/%llu probe=walk:%llu/%llu/%llu,booster:%llu/%llu/%llu,"
        "zone921:%llu/%llu/%llu highway_rows=%llu/%llu/%llu,rows:%lld highway_speed=%llu/%llu sound=%llu/%llu standins=%u/%u uncovered=%llu/%u shape_unkeyed=%llu view_scenes=%s field_fill=%s traffic_rows=%s traffic_pass=%s stop=%u",
        rc_mode==RC_VIEW?"view":"off",rc_installed,(unsigned long long)(rc_tick==UINT64_MAX?0:rc_tick),(unsigned long long)rc_viewpoint_id,
        (unsigned long long)rc_viewpoint_key,(unsigned long long)rc_updates,(unsigned long long)rc_native_frames,rc_restored,rc_stage,
        (long long)rc_failures,groups,installed,(unsigned long long)rc_stub_calls[0],(unsigned long long)rc_stub_calls[1],
        (unsigned long long)rc_stub_calls[2],(unsigned long long)rc_stub_calls[3],(unsigned long long)rc_stub_calls[4],
        (unsigned long long)rc_stub_calls[5],(unsigned long long)rc_install_tick,
        (unsigned long long)rc_install_waits,attempts,culls,(unsigned long long)rc_effect_rows,(unsigned long long)rc_vp2_key,
        (unsigned long long)(rc_stub_calls[6]+rc_stub_calls[7]+rc_stub_calls[8]+rc_stub_calls[9]+rc_stub_calls[10]),
        (unsigned long long)rc_stub_calls[RC_CONTEXT_CALL],(unsigned long long)rc_context_skipped,rc_policy?"sector":"off",
        (unsigned long long)rc_picks,(unsigned long long)rc_picks_two,(unsigned long long)rc_alternations,(long long)rc_pick_rows,site_picks,
        (unsigned long long)rc_stub_calls[RC_SHAPE_UPDATE_CALL],(unsigned long long)rc_stub_calls[RC_SHAPE_INTERVAL_CALL],
        (unsigned long long)rc_shape_aligned,(unsigned long long)rc_shape_unmapped,(unsigned long long)rc_shape_near,(unsigned long long)rc_shape_far,
        (unsigned long long)rc_shape_native,(unsigned long long)rc_mining_calls,(unsigned long long)rc_mining_near_count,
        (unsigned long long)rc_mining_foreign,(unsigned long long)rc_probe_counts[0][0],(unsigned long long)rc_probe_counts[0][1],
        (unsigned long long)rc_probe_counts[0][2],(unsigned long long)rc_probe_crystal_far,
        (unsigned long long)rc_stub_calls[RC_XS_EXTERNAL_CALL],(unsigned long long)rc_stub_calls[RC_XS_DISTANCE_CALL],
        (unsigned long long)rc_xs_near,(unsigned long long)rc_xs_far,(unsigned long long)rc_stub_calls[RC_XS_TARGET_CALL],(unsigned long long)rc_xs_targets,
        (unsigned long long)walk[0],(unsigned long long)walk[1],(unsigned long long)walk[2],
        (unsigned long long)(rc_probe_counts[10][0]+rc_probe_counts[11][0]),(unsigned long long)(rc_probe_counts[10][1]+rc_probe_counts[11][1]),
        (unsigned long long)(rc_probe_counts[10][2]+rc_probe_counts[11][2]),(unsigned long long)rc_probe_counts[12][0],
        (unsigned long long)rc_probe_counts[12][1],(unsigned long long)rc_probe_counts[12][2],
        (unsigned long long)rc_stub_calls[RC_HWROW_CALL],(unsigned long long)rc_stub_calls[RC_HWROW_CALL+1],
        (unsigned long long)rc_stub_calls[RC_HWROW_CALL+2],(long long)rc_hw_rows,(unsigned long long)rc_hw_speed_keyed,(unsigned long long)rc_hw_speed_unmapped,
        (unsigned long long)rc_stub_calls[RC_SOUND_CALL],(unsigned long long)rc_stub_calls[RC_SOUND_CALL+1u],
        (unsigned)(rc_two_sectors()?2+rc_more:rc_page && *rc_manager_camera()?1:0),rc_standins_max,(unsigned long long)rc_uncovered_ticks,rc_uncovered_now,(unsigned long long)rc_shape_unkeyed,scenes,fill,rows,pass,rc_stop);
    return n>0 && (size_t)n<capacity?n:0;
}
