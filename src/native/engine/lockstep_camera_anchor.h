/* Presentation harness, DEFAULT OFF: on one client the player camera follows
 * another pilot slot's ship (native_gate_camera_anchor = {client=2, slot=2}),
 * the missing piece for players in different sectors: the player object
 * (g_Universe+0x230) is slot 1's ship on every client, and the stock external
 * view (SetPlayerCameraTargetView 0x1b9230 -> 0x9faad0) refuses a target
 * outside the player object's class-0x58 sector (0x9fa960).
 *
 * Every admitted tick, in slot 6 on the named client after the view points
 * latch, the player camera's anchor pair (+0x770 anchor, +0x778; one 16-byte
 * CAS, as the stand-in cameras set them) becomes the latched view point of
 * that slot, and the camera's control update is paused as for
 * native_gate_camera_offset (+0x838 = -1.0: no look input or smoothing; the
 * camera keeps its mode +0x868 and local PosRot, so it sits where it sat
 * behind slot 1's ship, now behind slot 2's). Later calls verify the camera
 * object and the written pair and fail closed otherwise (stop reason 138;
 * exclusive with native_gate_camera_offset and native_gate_pilot_camera_follow,
 * the stock target view that also writes the anchor); a
 * replaced view point (another ship in the slot) is retargeted by CAS. Stop
 * puts the original pair back when ours is still there.
 *
 * Chase view (back, up metres; default 40, 10; both 0 keep the camera's own
 * local PosRot): cam-C1c kept the cockpit offset of slot 1's ship, so client 2
 * looked out of slot 2's hull from inside. With a chase view the first call
 * also writes the local PosRot (+0x590 translation x, y, z, w kept; rows
 * +0x5a0, +0x5b0, +0x5c0, each row's fourth float kept) to identity rows and
 * the translation (0, up, -back) in the anchor's frame (Camera::GetOffset
 * applies it to the anchor transform in every mode, see lockstep_camera_offset.h),
 * by one 16-byte CAS per block; the held control update leaves it alone, later
 * calls verify it (fail closed: overwritten) and stop puts the original back
 * before the anchor pair.
 * cam-C1d: the eye did not move (client 2 still saw the same lit interior).
 * The render eye is the camera's view bank entry for the anchor's zone
 * (Camera slot 3 0x983160 picks the anchor's class-0x6d zone, 0x9835a0 reads
 * 0x979ef0 for it; UniverseFrameUpdate refills the banks every frame,
 * 0x9fe573 -> 0x979ce0 -> 0x9799f0: GetOffset relative to the anchor and each
 * ancestor). GetOffset's base depends on the mode +0x868 (table 0x97a6c0):
 * 1 and 6 the anchor (+0x770); 0 the anchor plus the camera controller's
 * (+0x20) transform for the player camera; 2..5 the class-0x57 (Space: zone,
 * sector, cluster) ancestor of +0x778, so with a ship there the base is the
 * ship's zone and the eye sits at that zone's origin plus the local PosRot,
 * whatever the anchor is. Mode 0 with a ship anchor (cam-C1e: client 2's
 * camera was in mode 0) ends at the anchor's origin without the local PosRot
 * when 0x9fc670/0xd7d2e0 say the player is in its cockpit (0x97a459), i.e.
 * inside the followed ship: the "interior" of cam-C1c and cam-C1d.
 * cam-C1e set mode 1 on the camera and failed closed at tick 22: U::Player
 * Move 0x7113e0 checks a camera in a mode other than 0 every frame (0x713037:
 * the anchor must pass 0x9fa960, the target-view rule: the player's own ship
 * or something in the player object's sector) and otherwise resets it
 * (0x9f9fe0 -> SetMode 0x97cb80, which posts PlayerCameraModeChangedEvent to
 * the player object, and SetAnchor 0x97de40); slot 2's ship left the player's
 * sector with its start warp. That reset runs in the simulation on one client.
 * So the camera keeps its own mode and only the bank refill sees mode 1: the
 * per-camera refill call 0x9fe573 in UniverseFrameUpdate goes through a jump
 * stub to camfol_refill, which for our camera (anchor pair ours) sets mode 1
 * by CAS for the duration of the original call and puts the prior mode back
 * by CAS. Everything else that reads the camera's mode (Player Move, direct
 * GetOffset callers, the render's scene choice: mode 0 and 1 both take the
 * anchor's zone) sees the engine's mode, as in cam-C1d. The call is pinned
 * (0x979ce0, 0x9799f0, the camera-list loop) and patched by CAS of its rel32
 * (one aligned 16-byte block) at the first chase call, in slot 6 with the
 * MoveWorker parked; every call checks it; stop puts it back (the stub page is
 * never freed). It refuses (pins) unless the mode table still bases mode 1 on
 * the anchor.
 * Engine rewrites (cam-C1e: a reset at a warp): when the anchor pair or the
 * local PosRot no longer hold our values but the pair names two components
 * (vtables in the image) and the mode is 0..6, the engine moved the camera:
 * those values become the ones stop restores, ours are written again by CAS
 * (resets=, reset_tick=, reset_key=). A replaced player camera object is the
 * same case: the old camera gets its values back by CAS where ours are still
 * there, the new one is followed from then on. At most CAMFOL_RESET_LIMIT
 * resets per CAMFOL_RESET_WINDOW ticks (a long session sees view changes);
 * anything else, or more resets in one window, fail closed.
 * Play (cam-C1e2 passed: slot 2's ship from behind in sector001, 0 differing):
 * the probe's 'own' binding follows the local pilot slot's ship on every
 * client whose slot is not 1 (slot 1's ship is the player object; its client
 * keeps the native camera). The followed ship may change sector (gate,
 * highway, warp: cam-C1e2's start warp at tick 22): the anchor is the ship, so
 * the camera, its render zone (Camera slot 3) and the bank refill follow it;
 * simulation readers of the camera are routed (view points, region-camera
 * groups, camera context) or were census-clean in cam-C1d/C1e2. When the slot
 * has no ship (destroyed, between ships, unlatched) the camera is parked: the
 * engine's pair goes back by CAS so it never points at a ship that may be
 * gone (parks=, stage=parked); when the slot has a ship again ours are written
 * from the engine's pair (stage=resumed).
 * Orbit (free look, presentation only, DEFAULT OFF: orbit=true in the anchor
 * table): while this X4 window is in the foreground (two clients on one PC
 * must not share a keyboard), a key map turns the chase rig around the ship:
 * yaw left/right, pitch up/down (90 degrees a second, pitch within +-80),
 * zoom in/out (x1.02 a tick, 0.25..20 times), reset to the plain chase; each
 * step needs the map's modifier (none, or one virtual key held). Default map
 * (X4 7.x default bindings, libraries/inputmap.xml and the WASD profile, bind
 * every unmodified letter and the numeric keypad: 2/4/6/8 camera, 5 camera
 * reset, 0 camera mode, +/- camera zoom, and the arrows steer; left Alt is the
 * compass menu; Ctrl+I/J/K/L/U/O/P are unbound): Ctrl+J/L yaw, Ctrl+I/K pitch,
 * Ctrl+U/O zoom in/out, Ctrl+P reset. The probe sets another map by names
 * ('numpad' is the earlier 4/6/8/2, +/-, 5; it shares keys with X4's own camera
 * actions). A key counts when it is held at the tick or was pressed since the
 * last tick (GetAsyncKeyState's low bit), so a scripted tap (SendKeys) moves
 * one step. Status: input=foreground,modifier,mask (the map's keys seen this
 * tick: 02 yaw left, 04 yaw right, 08 pitch up, 10 pitch down, 20 zoom in,
 * 40 zoom out, 80 reset; recorded in the foreground only), taps= (keys of
 * the map seen only by the low bit) and last_move= (the tick of the last
 * step). The
 * rig is a rotation R = Ry(yaw) Rx(pitch) of the chase pose: eye R (0, up s,
 * -back s), rows R e0, R e1, R e2 (GetOffset reads the local rows as the
 * camera's axes in the anchor's frame), so the ship keeps its place on the
 * screen; yaw = pitch = 0, s = 1 is the chase view itself. A changed pose is
 * written by the same per-block CAS from our last value (an engine rewrite in
 * between is a reset as before). Status: orbit= yaw= pitch= scale= moves=.
 * Status: mode= (the camera's live mode), refills=/refill_skips= and eye=
 * (the published bank's camera translation relative to the followed ship;
 * expected 0,up,-back).
 *
 * Presentation only, but the native code that still reads the player camera
 * now sees another place on that client. The simulation readers routed by the
 * region-camera groups (region, fields, traffic, effects, highway, scenes) read
 * the stand-ins instead; unrouted readers of the camera's anchor include the
 * highway ship/scene controllers (0x9f9da0 callers), 0xe33b1a (0x9fcad0), and
 * the camera-distance helpers' remaining callers (ShipExtraSmall::vfunc876,
 * Bullet::UpdateFlight, NavigationController slot 33, AsteroidMovement-
 * Controller, 0x70c711, 0x73f6a3, 0x8c2f75, 0x8c3027, 0x97a79d): a census
 * with this harness names the ones that matter (cam-C1c: SetParent's camera
 * context test, routed by native_gate_camera_context). cam-C1c: the renderer
 * drew geometry at slot 2's ship (from inside its hull). */
#define CAMFOL_ANCHOR 0x770u
#define CAMFOL_STOP_REASON 138u /* 136 engine refresh, 137 nav octree */
#define CAMFOL_LOCAL 0x590u     /* local PosRot: translation, then three rows (4 floats each) */
#define CAMFOL_CHASE_LIMIT 5000.0
#define CAMFOL_MODE 0x868u      /* GetOffset's base: 1 = the anchor (+0x770) */
#define CAMFOL_CHASE_MODE 1
#define CAMFOL_VIEW_INDEX 0x38f4eb4u /* g_ComponentViewCacheIndex: the published view bank */
#define CAMFOL_BANK 0x2b0u
#define CAMFOL_MODE_TABLE 0x97a6c0u  /* GetOffset's mode jump table (image-relative dwords) */
#define CAMFOL_MODE_ANCHOR 0x97a36eu /* modes 1 and 6: the anchor's transform */
#define CAMFOL_REFILL_CALL 0x9fe573u /* UniverseFrameUpdate: call 0x979ce0 for each listed camera */
#define CAMFOL_REFILL 0x979ce0u      /* the camera's view bank refill (0x9799f0: GetOffset per ancestor) */
#define CAMFOL_RESET_LIMIT 30u   /* resets per window */
#define CAMFOL_RESET_WINDOW 600u /* ticks */
static int camfol_applied,camfol_restored,camfol_chase,camfol_local_restored,camfol_patched,camfol_unpatched;
static unsigned char *camfol_stub; /* jump stub near the image; never freed */
static volatile LONG64 camfol_refills,camfol_refill_skips;
static uint64_t camfol_resets,camfol_reset_tick,camfol_reset_key,camfol_window_start,camfol_window_resets,camfol_parks;
static int camfol_parked;
static int camfol_orbit;
/* The orbit key map (virtual keys): modifier (0 none), yaw left, yaw right, pitch up, pitch down, zoom in, zoom out, reset. */
enum {CAMFOL_KEY_MOD,CAMFOL_KEY_YAW_LEFT,CAMFOL_KEY_YAW_RIGHT,CAMFOL_KEY_PITCH_UP,CAMFOL_KEY_PITCH_DOWN,CAMFOL_KEY_ZOOM_IN,
      CAMFOL_KEY_ZOOM_OUT,CAMFOL_KEY_RESET,CAMFOL_KEYS};
static int camfol_keys[CAMFOL_KEYS]={VK_CONTROL,'J','L','I','K','U','O','P'};
static unsigned camfol_input_fg,camfol_input_mod,camfol_input_mask;
static uint64_t camfol_taps,camfol_last_move;
static double camfol_yaw,camfol_pitch,camfol_scale=1.0;
static uint64_t camfol_moves;
typedef SHORT (WINAPI *camfol_key_fn)(int);
static camfol_key_fn camfol_key_override; /* tests only: replaces GetAsyncKeyState */
static int camfol_foreground_override;    /* tests only: 1 foreground, -1 not */
typedef void (*camfol_refill_fn)(void *);
static camfol_refill_fn camfol_refill_native; /* tests only: replaces image+0x979ce0 */
static int camfol_pins_override;              /* tests only: the plain image has no pinned code */
static double camfol_back,camfol_up;
static __declspec(align(16)) float camfol_local_original[16],camfol_local_written[16];
static unsigned char *camfol_camera;
static void *camfol_original[2],*camfol_written[2];
static unsigned camfol_slot;
static uint64_t camfol_tick,camfol_calls,camfol_first,camfol_retargets,camfol_absent,camfol_key;
static volatile LONG64 camfol_failures;
static const char *camfol_stage="idle";
static int camfol_fail(const char *why) {camfol_stage=why;InterlockedIncrement64(&camfol_failures);return 0;}
static int camfol_cas(unsigned char *p,void *const expected[2],void *const value[2]) {
    if((uintptr_t)p&15) return 0;
    unsigned __int128 e,v;memcpy(&e,expected,16);memcpy(&v,value,16);
    return __sync_val_compare_and_swap((unsigned __int128*)p,e,v)==e;
}
/* All four 16-byte blocks from expected to value; on a refused block the ones
 * already written go back. */
static int camfol_local_cas(unsigned char *camera,const float expected[16],const float value[16]) {
    for(unsigned b=0;b<4;b++) if(!camoff_cas(camera+CAMFOL_LOCAL+16u*b,expected+4*b,value+4*b)) {
        while(b--) camoff_cas(camera+CAMFOL_LOCAL+16u*b,value+4*b,expected+4*b);
        return 0;
    }
    return 1;
}
/* The camera's translation relative to `key` in the published view bank
 * (entries {key, translation, rows} of 0x50 bytes at bank+0x40; capacity +0x280,
 * 8 = inline; count +0x290), as 0x979ef0 reads it; 0 if none. */
static int camfol_eye(const unsigned char *camera,const void *key,float out[3]) {
    uint32_t index=0;uint64_t capacity=0,count=0;const unsigned char *entries,*base;
    if(!camera || !key || !read_local(image+CAMFOL_VIEW_INDEX,&index,4) || index>1) return 0;
    base=camera+0x40+index*CAMFOL_BANK;
    if(!read_local(base+0x280,&capacity,8) || !read_local(base+0x290,&count,8) || count>64) return 0;
    if(capacity==8) entries=base;else if(!read_local(base,&entries,8) || !entries) return 0;
    for(uint64_t i=0;i<count;i++) {
        const void *k=NULL;if(!read_local(entries+i*0x50,&k,8)) return 0;
        if(k==key) return read_local(entries+i*0x50+0x10,out,12);
    }
    return 0;
}
/* GetOffset still bases mode 1 on the anchor (the table outside camoff_pins' hash). */
static int camfol_mode_pin(void) {
    uint32_t entry=0;return read_local(image+CAMFOL_MODE_TABLE+4u*CAMFOL_CHASE_MODE,&entry,4) && entry==CAMFOL_MODE_ANCHOR;
}
/* The refill (0x979ce0), its GetOffset walk (0x9799f0) and UniverseFrameUpdate's camera-list loop. */
static int camfol_refill_pins(void) {
    return camfol_pins_override || (hash_ok(image+CAMFOL_REFILL,0xdd,"dbcedb05d414d1b42c092b8ac98f15ad81442e524a020496c4a59bf94150c1e7") &&
        hash_ok(image+0x9799f0,0x2f0,"100d13e4f113faad4eb5bf9cddc4737097aebb3596631b33098246717d3b8da4") &&
        hash_ok(image+0x9fe55a,0x32,"e1658b86f806df2d83e4129e52a4d59715d886d7bcc7ed7b686b420a94d709b3"));
}
/* The refill call's rel32: 0 the original, 1 our stub, -1 anything else. */
static int32_t camfol_rel_to(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+CAMFOL_REFILL_CALL+5));}
static int camfol_refill_state(void) {
    unsigned char call[5];int32_t d;if(!read_local(image+CAMFOL_REFILL_CALL,call,5) || call[0]!=0xe8) return -1;
    memcpy(&d,call+1,4);
    if(d==camfol_rel_to(image+CAMFOL_REFILL)) return 0;
    return camfol_stub && d==camfol_rel_to(camfol_stub)?1:-1;
}
/* UniverseFrameUpdate's refill of one listed camera: ours sees mode 1 for the call. */
static void camfol_refill(unsigned char *camera) {
    camfol_refill_fn original=camfol_refill_native?camfol_refill_native:(camfol_refill_fn)(void *)(image+CAMFOL_REFILL);
    if(camfol_applied && !camfol_restored && camfol_chase && camera && camera==camfol_camera) {
        void *anchor=*(void *volatile *)(camera+CAMFOL_ANCHOR);LONG prior=*(volatile LONG *)(camera+CAMFOL_MODE);
        if(anchor==camfol_written[0] && prior!=CAMFOL_CHASE_MODE &&
           InterlockedCompareExchange((volatile LONG *)(camera+CAMFOL_MODE),CAMFOL_CHASE_MODE,prior)==prior) {
            original(camera);
            InterlockedCompareExchange((volatile LONG *)(camera+CAMFOL_MODE),prior,CAMFOL_CHASE_MODE);
            InterlockedIncrement64(&camfol_refills);return;
        }
        InterlockedIncrement64(&camfol_refill_skips);
    }
    original(camera);
}
static unsigned char *camfol_alloc_stub(void) {
    uintptr_t at=(uintptr_t)(image+CAMFOL_REFILL_CALL)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+CAMFOL_REFILL_CALL+5);
            if(d>=INT32_MIN+4096 && d<=INT32_MAX-4096) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* Slot 6, MoveWorker parked: the refill call through our stub. */
static int camfol_patch(void) {
    if(camfol_patched) return camfol_refill_state()==1;
    if(!camfol_refill_pins() || camfol_refill_state()!=0) return 0;
    if(!camfol_stub) {
        unsigned char *p=camfol_alloc_stub();if(!p) return 0;
        static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=(void*)camfol_refill;
        memcpy(p,jump,6);memcpy(p+6,&target,8);DWORD old;
        if(!VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old)) {VirtualFree(p,0,MEM_RELEASE);return 0;}
        FlushInstructionCache(GetCurrentProcess(),p,4096);camfol_stub=p;
    }
    int32_t e=camfol_rel_to(image+CAMFOL_REFILL),v=camfol_rel_to(camfol_stub);
    if(!view_cas_bytes(CAMFOL_REFILL_CALL+1,(const unsigned char*)&e,(const unsigned char*)&v,4)) return 0;
    camfol_patched=1;return 1;
}
static void camfol_unpatch(void) {
    if(!camfol_patched || camfol_unpatched) return;
    if(camfol_refill_state()==1) {
        int32_t e=camfol_rel_to(camfol_stub),v=camfol_rel_to(image+CAMFOL_REFILL);
        camfol_unpatched=view_cas_bytes(CAMFOL_REFILL_CALL+1,(const unsigned char*)&e,(const unsigned char*)&v,4);
    }
}
/* One more engine rewrite at tick: refused (storm) past the limit within one window. */
static int camfol_count_reset(uint64_t tick) {
    if(!camfol_window_resets || tick-camfol_window_start>=CAMFOL_RESET_WINDOW) {camfol_window_start=tick;camfol_window_resets=0;}
    if(camfol_window_resets>=CAMFOL_RESET_LIMIT) return 0;
    ++camfol_window_resets;++camfol_resets;camfol_reset_tick=tick;camfol_reset_key=0;return 1;
}
/* The anchor pair names two components (vtables in the image) and the mode is 0..6:
 * the engine moved the camera (a reset at a warp, a view change). */
static int camfol_engine_values(const unsigned char *camera,void *const pair[2]) {
    LONG mode=-1;if(!read_local(camera+CAMFOL_MODE,&mode,4) || mode<0 || mode>6) return 0;
    for(unsigned i=0;i<2;i++) {
        uintptr_t vtable=0;
        if(!pair[i] || !read_local(pair[i],&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)image+0x6fa9000u) return 0;
    }
    return 1;
}
/* The chase pose turned by the orbit: R = Ry(yaw) Rx(pitch); eye R (0, up s, -back s); rows R e_k
 * (the camera's axes in the anchor's frame); every fourth float kept. yaw = pitch = 0, s = 1: the chase. */
static void camfol_pose_local(const float now[16],float out[16]) {
    const double cy=cos(camfol_yaw),sy=sin(camfol_yaw),cp=cos(camfol_pitch),sp=sin(camfol_pitch);
    const double r[3][3]={{cy,sy*sp,sy*cp},{0,cp,-sp},{-sy,cy*sp,cy*cp}};
    const double eye[3]={0,camfol_up*camfol_scale,-camfol_back*camfol_scale};
    memcpy(out,now,64);
    for(unsigned i=0;i<3;i++) out[i]=(float)(r[i][0]*eye[0]+r[i][1]*eye[1]+r[i][2]*eye[2]);
    for(unsigned k=0;k<3;k++) for(unsigned i=0;i<3;i++) out[4+4*k+i]=(float)r[i][k];
}
/* 2 held now, 1 pressed since the last query only (the low bit: a scripted tap between two ticks), 0 neither. */
static int camfol_key_state(int vk) {
    if(!vk) return 0;
    SHORT state=(camfol_key_override?camfol_key_override:GetAsyncKeyState)(vk);
    return (state&0x8000)?2:(state&1)?1:0;
}
/* A key map from the probe (before the first call): every entry a virtual key 1..254, the modifier also 0. */
static int camfol_keys_set(const int keys[CAMFOL_KEYS]) {
    if(camfol_calls) return 0;
    for(unsigned i=0;i<CAMFOL_KEYS;i++) if(keys[i]<(i==CAMFOL_KEY_MOD?0:1) || keys[i]>254) return 0;
    for(unsigned i=1;i<CAMFOL_KEYS;i++) for(unsigned j=i+1;j<CAMFOL_KEYS;j++) if(keys[i]==keys[j]) return 0;
    memcpy(camfol_keys,keys,sizeof(camfol_keys));return 1;
}
static int camfol_foreground(void) {
    if(camfol_foreground_override) return camfol_foreground_override>0;
    HWND window=GetForegroundWindow();DWORD pid=0;
    return window && GetWindowThreadProcessId(window,&pid) && pid==GetCurrentProcessId();
}
/* One tick of key map input; 1 when the pose changed. Every key of the map is read every tick, so a
 * press without the modifier or while another window had the focus leaves no stale low bit behind;
 * keys are only recorded (input mask, taps) while this window is in the foreground. */
static int camfol_orbit_input(uint64_t tick) {
    camfol_input_fg=camfol_input_mod=camfol_input_mask=0;
    if(!camfol_orbit) return 0;
    const int mod=!camfol_keys[CAMFOL_KEY_MOD] || camfol_key_state(camfol_keys[CAMFOL_KEY_MOD]);
    unsigned seen=0,tapped=0;
    for(unsigned i=1;i<CAMFOL_KEYS;i++) {const int s=camfol_key_state(camfol_keys[i]);if(s) seen|=1u<<i;if(s==1) ++tapped;}
    if(!(camfol_input_fg=camfol_foreground()!=0)) return 0;
    camfol_input_mod=mod!=0;camfol_input_mask=seen;camfol_taps+=tapped;
    if(!mod) return 0;
    const double step=1.5*3.14159265358979323846/180.0,limit=80.0*3.14159265358979323846/180.0;
    double yaw=camfol_yaw,pitch=camfol_pitch,scale=camfol_scale;const unsigned m=camfol_input_mask;
    if(m&(1u<<CAMFOL_KEY_RESET)) {yaw=pitch=0;scale=1;}
    else {
        if(m&(1u<<CAMFOL_KEY_YAW_LEFT)) yaw-=step;
        if(m&(1u<<CAMFOL_KEY_YAW_RIGHT)) yaw+=step;
        if(m&(1u<<CAMFOL_KEY_PITCH_UP)) pitch+=step;
        if(m&(1u<<CAMFOL_KEY_PITCH_DOWN)) pitch-=step;
        if(m&(1u<<CAMFOL_KEY_ZOOM_OUT)) scale*=1.02;
        if(m&(1u<<CAMFOL_KEY_ZOOM_IN)) scale/=1.02;
    }
    if(pitch>limit) pitch=limit;
    if(pitch<-limit) pitch=-limit;
    if(scale<0.25) scale=0.25;
    if(scale>20.0) scale=20.0;
    yaw=fmod(yaw,2*3.14159265358979323846);
    if(yaw==camfol_yaw && pitch==camfol_pitch && scale==camfol_scale) return 0;
    camfol_yaw=yaw;camfol_pitch=pitch;camfol_scale=scale;camfol_last_move=tick;return 1;
}
static int camfol_chase_valid(double back,double up) {
    return isfinite(back) && isfinite(up) && fabs(back)<=CAMFOL_CHASE_LIMIT && fabs(up)<=CAMFOL_CHASE_LIMIT;
}
/* The latched view point of pilot slot `slot` (1-based), checked against its ID; NULL if none. */
static void *camfol_target(unsigned slot) {
    if(!view_latched || slot<1 || slot>VIEW_MAX || view_count<slot) return NULL;
    void *p=view_points[slot-1];uint64_t id=0;
    if(!p || !read_local((unsigned char*)p+8,&id,8) || id!=view_ids[slot-1]) return NULL;
    return p;
}
static int camfol_apply(uint64_t tick,unsigned slot,double back,double up) {
    if(!stats.active || GetCurrentThreadId()!=ui_thread) return camfol_fail("guard");
    if(slot<1 || slot>VIEW_MAX || (camfol_slot && slot!=camfol_slot)) return camfol_fail("slot");
    if(!camfol_chase_valid(back,up) || (camfol_calls && (back!=camfol_back || up!=camfol_up))) return camfol_fail("chase");
    if(!camoff_pins()) return camfol_fail("pins");
    if(camoff_applied) return camfol_fail("camera_offset"); /* both hold the control update */
    unsigned char *camera=camoff_player_camera();void *now[2];
    if(!camera || !read_local(camera+CAMFOL_ANCHOR,now,16)) return camfol_fail("camera");
    camfol_tick=tick;++camfol_calls;camfol_slot=slot;camfol_back=back;camfol_up=up;
    void *target=camfol_target(slot);
    const int chase=back!=0 || up!=0;
    if(!camfol_applied) {
        if(!target) {++camfol_absent;camfol_stage="waiting";return 1;}
        if(chase) {
            if(!camfol_mode_pin() || !camfol_patch()) return camfol_fail("pins");
            if(((uintptr_t)(camera+CAMFOL_LOCAL)&15) || !read_local(camera+CAMFOL_LOCAL,camfol_local_original,64)) return camfol_fail("camera");
            camfol_pose_local(camfol_local_original,camfol_local_written);
            if(!camfol_local_cas(camera,camfol_local_original,camfol_local_written)) return camfol_fail("cas");
        }
        void *value[2]={target,target};
        if(!camfol_cas(camera+CAMFOL_ANCHOR,now,value)) {
            if(chase) camfol_local_cas(camera,camfol_local_written,camfol_local_original);
            return camfol_fail("cas");
        }
        camfol_chase=chase;camfol_local_restored=0;
        memcpy(camfol_original,now,16);memcpy(camfol_written,value,16);camfol_camera=camera;camfol_applied=1;camfol_first=tick;
        unsigned klass;camfol_key=world_identity_key(view_ids[slot-1],&klass);
        camoff_hold(camera);camfol_stage="applied";return 1;
    }
    if(camfol_chase && camfol_refill_state()!=1) return camfol_fail("refill_site");
    __declspec(align(16)) float local[16];
    if(camfol_chase && !read_local(camera+CAMFOL_LOCAL,local,64)) return camfol_fail("camera");
    const int replaced=camera!=camfol_camera;
    void *const *expect=camfol_parked?camfol_original:camfol_written;
    const int pair_moved=replaced || memcmp(now,expect,16)!=0;
    const int local_moved=camfol_chase && (replaced || memcmp(local,camfol_local_written,64)!=0);
    if(pair_moved || local_moved) {
        /* The engine moved or replaced the camera: its values become the ones stop restores; ours go back in. */
        if(!camfol_engine_values(camera,pair_moved?now:camfol_original))
            return camfol_fail(replaced?"camera_replaced":pair_moved?"overwritten":"overwritten_local");
        if(!camfol_count_reset(tick)) return camfol_fail("reset_storm");
        uint64_t id=0;unsigned klass;
        if(pair_moved && read_local((unsigned char*)now[0]+8,&id,8) && id) camfol_reset_key=world_identity_key(id,&klass);
        if(replaced) {
            if(!camfol_parked) camfol_cas(camfol_camera+CAMFOL_ANCHOR,camfol_written,camfol_original);
            if(camfol_chase) camfol_local_cas(camfol_camera,camfol_local_written,camfol_local_original);
            camfol_camera=camera;
        }
        if(local_moved) {
            __declspec(align(16)) float written[16];
            camfol_pose_local(local,written);
            if(!camfol_local_cas(camera,local,written)) return camfol_fail("cas");
            memcpy(camfol_local_original,local,64);memcpy(camfol_local_written,written,64);
        }
        if(pair_moved) {
            memcpy(camfol_original,now,16);
            if(target) {
                void *value[2]={target,target};
                if(!camfol_cas(camera+CAMFOL_ANCHOR,now,value)) return camfol_fail("cas");
                memcpy(camfol_written,value,16);camfol_parked=0;
            } else if(!camfol_parked) {camfol_parked=1;++camfol_parks;}
        }
        camoff_hold(camera);camfol_stage="reset";return 1;
    }
    if(!target) {
        /* No ship in the slot: the engine's pair goes back so the camera never points at a ship that may be gone. */
        ++camfol_absent;
        if(!camfol_parked) {
            if(!camfol_cas(camera+CAMFOL_ANCHOR,camfol_written,camfol_original)) return camfol_fail("cas");
            camfol_parked=1;++camfol_parks;
        }
        camoff_hold(camera);camfol_stage="parked";return 1;
    }
    if(camfol_parked || target!=camfol_written[0]) {
        void *value[2]={target,target};
        if(!camfol_cas(camera+CAMFOL_ANCHOR,camfol_parked?camfol_original:camfol_written,value)) return camfol_fail("cas");
        memcpy(camfol_written,value,16);++camfol_retargets;
        unsigned klass;camfol_key=world_identity_key(view_ids[slot-1],&klass);
        camfol_stage=camfol_parked?"resumed":"retargeted";camfol_parked=0;camoff_hold(camera);return 1;
    }
    if(camfol_chase && camfol_orbit_input(tick)) {
        __declspec(align(16)) float written[16];camfol_pose_local(camfol_local_written,written);
        if(!camfol_local_cas(camera,camfol_local_written,written)) return camfol_fail("cas");
        memcpy(camfol_local_written,written,64);++camfol_moves;
    }
    camoff_hold(camera);camfol_stage="kept";return 1;
}
static int camfol_check(void) {return !InterlockedCompareExchange64(&camfol_failures,0,0);}
static void camfol_restore(void) {
    if(!camfol_applied || camfol_restored) return;
    camfol_unpatch();
    if(camoff_player_camera()!=camfol_camera) {camfol_stage="restore_camera";return;}
    if(camfol_chase && !camfol_local_restored) camfol_local_restored=camfol_local_cas(camfol_camera,camfol_local_written,camfol_local_original);
    if(camfol_parked) { /* the engine's pair is already in place */
        void *now[2];camfol_restored=read_local(camfol_camera+CAMFOL_ANCHOR,now,16) && !memcmp(now,camfol_original,16);
    } else camfol_restored=camfol_cas(camfol_camera+CAMFOL_ANCHOR,camfol_written,camfol_original);
    if(!camfol_restored) camfol_stage="restore_foreign";
}
static int camfol_status(char *out,size_t capacity) {
    char eye[64]="-";float at[3];LONG mode=-1;
    if(camfol_applied && !camfol_restored && camfol_camera) {
        read_local(camfol_camera+CAMFOL_MODE,&mode,4);
        if(camfol_eye(camfol_camera,camfol_written[0],at)) snprintf(eye,sizeof(eye),"%.1f,%.1f,%.1f",(double)at[0],(double)at[1],(double)at[2]);
    }
    int n=snprintf(out,capacity,"tick=%llu calls=%llu first=%llu applied=%d slot=%u key=%016llx retargets=%llu absent=%llu restored=%d stage=%s failures=%lld "
        "chase=%.1f,%.1f local_restored=%d mode=%ld refills=%llu refill_skips=%llu unpatched=%d resets=%llu reset_tick=%llu reset_key=%016llx "
        "parked=%d parks=%llu orbit=%d yaw=%.1f pitch=%.1f scale=%.2f moves=%llu input=%u,%u,%02x taps=%llu last_move=%llu stub=%llx eye=%s",
        (unsigned long long)camfol_tick,(unsigned long long)camfol_calls,(unsigned long long)camfol_first,camfol_applied,camfol_slot,
        (unsigned long long)camfol_key,(unsigned long long)camfol_retargets,(unsigned long long)camfol_absent,camfol_restored,camfol_stage,
        (long long)camfol_failures,camfol_back,camfol_up,camfol_local_restored,(long)mode,(unsigned long long)camfol_refills,
        (unsigned long long)camfol_refill_skips,camfol_unpatched,(unsigned long long)camfol_resets,(unsigned long long)camfol_reset_tick,
        (unsigned long long)camfol_reset_key,camfol_parked,(unsigned long long)camfol_parks,camfol_orbit,camfol_yaw*180.0/3.14159265358979323846,
        camfol_pitch*180.0/3.14159265358979323846,camfol_scale,(unsigned long long)camfol_moves,camfol_input_fg,camfol_input_mod,camfol_input_mask,
        (unsigned long long)camfol_taps,(unsigned long long)camfol_last_move,(unsigned long long)(uintptr_t)camfol_stub,eye);
    return n>0 && (size_t)n<capacity?n:0;
}
