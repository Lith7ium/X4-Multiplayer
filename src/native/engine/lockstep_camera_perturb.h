/* Test harness, DEFAULT OFF: presentation-only camera perturbation for the
 * render parity-leak test (native_gate_camera_perturb). One client changes
 * what its camera renders every admitted tick; nothing else.
 * No X4 Lua view API is presentation-only: the view-mode setters (cockpit,
 * external, floating, target, cinematic: 0x9f9fe0, 0x9fa3f0, 0x9fb120,
 * 0x9faad0, 0x9fb870) post events, and all but the last create components
 * (UniverseIDs) and draw thread RNG; the holomap (AddHoloMap 0x22c270) creates
 * a component; SetSceneCameraActive posts events; SetFOVOption 0x2a8440 also
 * writes the options file on every call; SetFollowCameraBasePos moves the
 * camera (camera distance feeds Object attention, 0x6d20b0 via
 * U::Camera::GetOffset 0x97a300) and acts only in follow view (+0x780).
 * This writes only the player camera's field of view, the float that
 * SetFOVOption writes (g_Universe 0x3d198f8 +0x3d0 camera, +0x85c, live in
 * camera modes +0x868 0..2 and 4), in memory: base FOV times 1, 0.7, 0.4, 0.7
 * by tick. Attention uses camera position, not FOV; zone attention uses the
 * player entity. Nothing per frame rewrites +0x85c (writers: options, display
 * restore, camera setup, radar, cutscenes), so a changed value means another
 * writer and fails closed. The original value is restored when the gate
 * finishes, unless another writer replaced ours. */
#define CAMERA_UNIVERSE 0x3d198f8u
#define CAMERA_PLAYER 0x3d0u
#define CAMERA_FOV 0x85cu
#define CAMERA_MODE 0x868u
static const float camera_perturb_factors[4]={1.0f,0.7f,0.4f,0.7f};
static int camera_perturb_verified,camera_perturb_saved,camera_perturb_restored;
static unsigned char *camera_perturb_camera;
static uint32_t camera_perturb_original,camera_perturb_written;
static uint64_t camera_perturb_calls,camera_perturb_skipped,camera_perturb_tick,camera_perturb_failures;
static const char *camera_perturb_stage="idle";
static int camera_perturb_pins(void) {
    /* Lua::SetFOVOption: g_Universe+0x3d0 player camera, mode +0x868, FOV +0x85c. */
    if(!camera_perturb_verified) camera_perturb_verified=hash_ok(image+0x2a8515,0x97,"cd4b345f7b749277f93056a7534e49dd45aa567aec0d6880bbd62e8d422f0ffd");
    return camera_perturb_verified;
}
static unsigned char *camera_perturb_player(void) {
    unsigned char *universe=NULL,*camera=NULL;
    return read_local(image+CAMERA_UNIVERSE,&universe,8) && universe && read_local(universe+CAMERA_PLAYER,&camera,8)?camera:NULL;
}
static int camera_perturb_fail(const char *why) {camera_perturb_stage=why;++camera_perturb_failures;return 0;}
static int camera_perturb_apply(uint64_t tick) {
    if(!stats.active || GetCurrentThreadId()!=ui_thread) return camera_perturb_fail("guard");
    if(!camera_perturb_pins()) return camera_perturb_fail("pins");
    unsigned char *camera=camera_perturb_player();uint32_t now,mode;float base,value;
    if(!camera || !read_local(camera+CAMERA_FOV,&now,4) || !read_local(camera+CAMERA_MODE,&mode,4)) return camera_perturb_fail("camera");
    if(!camera_perturb_saved) {camera_perturb_camera=camera;camera_perturb_original=camera_perturb_written=now;camera_perturb_saved=1;}
    else if(camera!=camera_perturb_camera) return camera_perturb_fail("camera_replaced");
    else if(now!=camera_perturb_written) return camera_perturb_fail("fov_changed");
    camera_perturb_tick=tick;
    if(mode>2 && mode!=4) {++camera_perturb_skipped;camera_perturb_stage="mode";return 1;} /* FOV not live in this mode */
    memcpy(&base,&camera_perturb_original,4);
    if(!isfinite(base) || base<=0.0f || base>4.0f) return camera_perturb_fail("fov_range");
    value=base*camera_perturb_factors[tick%4];memcpy(&camera_perturb_written,&value,4);
    InterlockedExchange((volatile LONG*)(camera+CAMERA_FOV),(LONG)camera_perturb_written);
    ++camera_perturb_calls;camera_perturb_stage="applied";return 1;
}
static void camera_perturb_restore(void) {
    if(!camera_perturb_saved || camera_perturb_restored) return;
    unsigned char *camera=camera_perturb_player();
    if(camera!=camera_perturb_camera) {camera_perturb_stage="restore_camera";return;}
    camera_perturb_restored=(uint32_t)InterlockedCompareExchange((volatile LONG*)(camera+CAMERA_FOV),
        (LONG)camera_perturb_original,(LONG)camera_perturb_written)==camera_perturb_written;
    if(!camera_perturb_restored) camera_perturb_stage="restore_foreign";
}
static int camera_perturb_status(char *out,size_t capacity) {
    float original,written;memcpy(&original,&camera_perturb_original,4);memcpy(&written,&camera_perturb_written,4);
    int n=snprintf(out,capacity,"tick=%llu calls=%llu skipped=%llu factor=%.2f original=%.6f written=%.6f restored=%d stage=%s failures=%llu",
        (unsigned long long)camera_perturb_tick,(unsigned long long)camera_perturb_calls,(unsigned long long)camera_perturb_skipped,
        (double)camera_perturb_factors[camera_perturb_tick%4],(double)original,(double)written,camera_perturb_restored,camera_perturb_stage,
        (unsigned long long)camera_perturb_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
