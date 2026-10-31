/* Test harness, DEFAULT OFF: move the player camera's position on one client
 * only (native_gate_camera_offset = {client=..., x=..., y=..., z=...}), for the
 * camera A/B test of the viewpoint work. No X4 view API is used (they post
 * events and create components). Camera::GetOffset 0x97a300 ends, in every
 * camera mode, by applying the camera's local PosRot (g_Universe 0x3d198f8
 * +0x3d0 camera, +0x590 position, +0x5a0..+0x5c0 rows) to the anchor
 * transform: position += rows(anchor) * local position (0x97a5a8..0x97a658).
 * Besides camera creation and reset (0x71a560, Player::vfunc540 0x706d00) and
 * the re-expression on a player transfer (Player::vfunc114 0x703f40), the
 * player camera's control update rewrites that local PosRot every frame:
 * U::UniverseFrameUpdate (slot 3, after the universe tree update) calls
 * 0x9fd870, which runs 0x97fe50 on g_Universe+0x3d0 with the game and real
 * time elapsed since its last run (smoothing, head tracking, look input). Its
 * control step is skipped while its last-run game time +0x838 holds the
 * constructor's "not started" value -1.0 (0x97ff07: -0.9999 >= +0x838); the
 * skip path only stores the times again (0x9822ba). Every admitted tick, in
 * slot 6 on the named client, the first call adds (x, y, z) metres in the
 * anchor's frame to the local position (one 16-byte CAS, w kept); every call
 * then puts -1.0 back into +0x838, so the next control update skips and the
 * written value is held (presentation only: on that client the camera still
 * follows its anchor but ignores look input and smoothing). Later calls verify
 * that the camera object and the written value are unchanged and fail closed
 * otherwise (a view change, reset or player transfer), and the gate stops with
 * reason 98. Everything that reads the camera through GetOffset sees the moved
 * camera: the simulation of the next frame's slot 3 (before the control
 * update), the mass traffic pass and the render: every simulation input that
 * still depends on the camera (attention without native_gate_view_points, the
 * cadences without their fixes, mass traffic). The original value is restored
 * on stop when ours is still in place; the control update resumes by itself. */
#define CAMOFF_UNIVERSE 0x3d198f8u
#define CAMOFF_CAMERA 0x3d0u
#define CAMOFF_LOCAL 0x590u
#define CAMOFF_MODE 0x868u
#define CAMOFF_HOLD 0x838u   /* control update's last-run game time; -1.0 = not started */
#define CAMOFF_LIMIT 1.0e7f
static int camoff_verified,camoff_applied,camoff_restored;
static unsigned char *camoff_camera;
static float camoff_delta[3],camoff_original[4],camoff_written[4];
static uint32_t camoff_mode;
static uint64_t camoff_tick,camoff_calls,camoff_first,camoff_holds,camoff_resumed;
static volatile LONG64 camoff_failures;
static const char *camoff_stage="idle";
static int camoff_pins(void) {
    /* Camera::GetOffset from its entry to the jump table: anchor +0x770/+0x778, mode +0x868, the local PosRot application. */
    /* The control update 0x97fe50 from its entry through the +0x838/+0x840 gate, and its time store. */
    if(!camoff_verified) camoff_verified=hash_ok(image+0x97a300,0x3be,"1f4e10df76e0f90efd7d31a28830dea4a3de889d8787e9cc054f82b24763f068") &&
        hash_ok(image+0x97fe50,0x136,"e3fb5fdb0d450f0d67b623159da7858b639e0a602b571f27a8d47f4ffa039cd9") &&
        hash_ok(image+0x9822a0,0x2c,"3d026252fca5eca531e51170975db64119417bf7f295ee3e68411f43193aadd3");
    return camoff_verified;
}
static unsigned char *camoff_player_camera(void) {
    unsigned char *universe=NULL,*camera=NULL;
    return read_local(image+CAMOFF_UNIVERSE,&universe,8) && universe && read_local(universe+CAMOFF_CAMERA,&camera,8)?camera:NULL;
}
static int camoff_fail(const char *why) {camoff_stage=why;InterlockedIncrement64(&camoff_failures);return 0;}
static int camoff_cas(unsigned char *p,const float expected[4],const float value[4]) {
    if((uintptr_t)p&15) return 0;
    unsigned __int128 e,v;memcpy(&e,expected,16);memcpy(&v,value,16);
    return __sync_val_compare_and_swap((unsigned __int128*)p,e,v)==e;
}
/* Keep the control update in its "not started" state until our next call. */
static void camoff_hold(unsigned char *camera) {
    static const double not_started=-1.0;LONG64 bits;memcpy(&bits,&not_started,8);
    if((uintptr_t)(camera+CAMOFF_HOLD)&7) return;
    LONG64 prior=InterlockedExchange64((volatile LONG64*)(camera+CAMOFF_HOLD),bits);
    ++camoff_holds;if(prior!=bits) ++camoff_resumed; /* the update ran (its skip path stored the time) */
}
static int camoff_valid(double x,double y,double z) {
    return isfinite(x) && isfinite(y) && isfinite(z) && fabs(x)<=CAMOFF_LIMIT && fabs(y)<=CAMOFF_LIMIT && fabs(z)<=CAMOFF_LIMIT &&
        (x!=0 || y!=0 || z!=0);
}
static int camoff_apply(uint64_t tick,double x,double y,double z) {
    if(!stats.active || GetCurrentThreadId()!=ui_thread) return camoff_fail("guard");
    if(!camoff_valid(x,y,z)) return camoff_fail("offset");
    if(!camoff_pins()) return camoff_fail("pins");
    unsigned char *camera=camoff_player_camera();float now[4];
    if(!camera || !read_local(camera+CAMOFF_LOCAL,now,16)) return camoff_fail("camera");
    camoff_tick=tick;++camoff_calls;
    if(!camoff_applied) {
        float value[4];memcpy(camoff_original,now,16);memcpy(value,now,16);
        value[0]+=(float)x;value[1]+=(float)y;value[2]+=(float)z;
        if(!isfinite(value[0]) || !isfinite(value[1]) || !isfinite(value[2])) return camoff_fail("offset");
        if(!camoff_cas(camera+CAMOFF_LOCAL,now,value)) return camoff_fail("cas");
        camoff_delta[0]=(float)x;camoff_delta[1]=(float)y;camoff_delta[2]=(float)z;
        memcpy(camoff_written,value,16);camoff_camera=camera;camoff_applied=1;camoff_first=tick;
        read_local(camera+CAMOFF_MODE,&camoff_mode,4);camoff_hold(camera);camoff_stage="applied";return 1;
    }
    if(camoff_delta[0]!=(float)x || camoff_delta[1]!=(float)y || camoff_delta[2]!=(float)z) return camoff_fail("offset_changed");
    if(camera!=camoff_camera) return camoff_fail("camera_replaced");
    if(memcmp(now,camoff_written,16)) return camoff_fail("overwritten");
    read_local(camera+CAMOFF_MODE,&camoff_mode,4);camoff_hold(camera);camoff_stage="kept";return 1;
}
static int camoff_check(void) {return !InterlockedCompareExchange64(&camoff_failures,0,0);}
static void camoff_restore(void) {
    if(!camoff_applied || camoff_restored) return;
    if(camoff_player_camera()!=camoff_camera) {camoff_stage="restore_camera";return;}
    camoff_restored=camoff_cas(camoff_camera+CAMOFF_LOCAL,camoff_written,camoff_original);
    if(!camoff_restored) camoff_stage="restore_foreign";
}
static int camoff_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"tick=%llu calls=%llu first=%llu applied=%d mode=%u delta=%.3f,%.3f,%.3f original=%.3f,%.3f,%.3f written=%.3f,%.3f,%.3f "
        "holds=%llu resumed=%llu restored=%d stage=%s failures=%lld",(unsigned long long)camoff_tick,(unsigned long long)camoff_calls,(unsigned long long)camoff_first,
        camoff_applied,camoff_mode,(double)camoff_delta[0],(double)camoff_delta[1],(double)camoff_delta[2],
        (double)camoff_original[0],(double)camoff_original[1],(double)camoff_original[2],
        (double)camoff_written[0],(double)camoff_written[1],(double)camoff_written[2],(unsigned long long)camoff_holds,
        (unsigned long long)camoff_resumed,camoff_restored,camoff_stage,(long long)camoff_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
