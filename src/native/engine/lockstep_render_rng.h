/* Determinism experiment, DEFAULT OFF: render RNG split (the frame diet's
 * rng_split site, reimplemented for the gate; reports/REWRITE-FRAME-DIET.md).
 * VBI slot 10 is X4::GameRender::Move 0x121c1f0: Render (vfunc2) of the four
 * render banks (VbiTable +0x110..+0x128: UI, character animation, GameClient),
 * then the release tail at *(0x6adfc30). The GameClient bank 0x122bfc0 renders
 * every universe camera (U::Camera slot 3 0x983160 -> Scene::Render): about 150
 * thread-RNG steps per frame on the MoveWorker word TLS+0x318 (scene render
 * helper 0x77b890 per rendered zone, SpawnEffect 0xd73e00, effect creator
 * 0xd717a0), as a function of what the camera sees.
 * MoveWorker frame (MoveWorker::Run 0xef4400): Gravidar PublishResults, wait
 * for FrameTick, pathing kick = path_release (ReleaseSemaphore at 0xef4614; with
 * native_gate_seed_rng the per-frame seed of TLS+0x318 is written here),
 * SyncThreadGameTime, the camera pre-pass 0x97a160 (list resets, no RNG), then
 * VBI slots 0..16 in index order (0xef4681): slot 3 GameClient::Move runs
 * events, MD and AI scripts, slot 6 UI Lua, slot 10 this render, slot 11
 * U::JobEngine 0xf0b870, whose job-ship spawns (ShipGenerator::Generate
 * 0xc841b0 -> Random::U64 0x14dde20) draw from the same word right after the
 * render. Seeded, the view reaches slot 11 of the same frame; unseeded, every
 * later draw. Split: the X4::GameRender vtable entry 0x2c8ac10 (slot 1, Move)
 * points at a wrapper (data only, no thunk) that runs slot 10 with TLS+0x318
 * holding a private render word, seeded at arm and continued across frames, and
 * puts the caller's word back afterwards, also when an exception unwinds.
 * Only the MoveWorker (TLS+0x314 == 1, as accept_ui) is split; the loading
 * bank on the main thread passes through. Observe mode keeps the wrapper but
 * not the swap, measuring slot 10's draws on the simulation word instead.
 * Not covered: UniverseIDs. The render walk creates welding effects of
 * construction mass traffic (WeldingMassTrafficBehavior 0x89a7d0 -> SpawnEffect
 * -> CreateEffectObjectCreator 0xd73060 -> AssignNewID 0xcc220) from the one
 * FIFO free list of the AutoIDMap (*0x6cf1500: head +0x50, tail +0x54, insert
 * counter +0x38). ids= counts insertions while slot 10 runs (any thread). The
 * framediet DLL patches this entry and the camera context entries: either one
 * refuses when it finds the other's patch. */
#define RENDER_RNG_ENTRY 0x2c8ac10u        /* X4::GameRender vtable 0x2c8ac08 slot 1 */
#define RENDER_RNG_MOVE 0x121c1f0u
#define RENDER_RNG_CLIENT_ENTRY 0x2c8abe0u /* X4::GameClient render bank (framediet camera context) */
#define RENDER_RNG_CLIENT 0x122bfc0u
#define RENDER_RNG_CAMERA_ENTRY 0x2bae1b8u /* U::Camera slot 3 Render */
#define RENDER_RNG_CAMERA 0x983160u
#define RENDER_RNG_ID_MAP 0x6cf1500u
#define RENDER_RNG_DOMAIN 0x58344c52454e4452ULL /* "X4LRENDR" */
#define RENDER_RNG_STEP_BOUND 65536u
enum {RENDER_RNG_OFF,RENDER_RNG_SPLIT,RENDER_RNG_OBSERVE};
typedef void (*render_rng_fn)(void *);
/* Presentation prediction (lockstep_pilot_predict.h): a scoped, render-only
 * pose substitution around the original slot 10; inert unless armed. */
static int predict_present_enter(void);
static void predict_present_leave(int entered);
/* Slot 10 inside the presentation scope (lockstep_pilot_present.h); the original alone when off. */
static void pres_render_call(render_rng_fn original,void *self);
static volatile LONG render_rng_active;
/* Inside VBI slot 10 (GameRender::Move: UI, animation, every camera): the frame thread is
 * rendering, not simulating. Read by counters that separate presentation callers. */
__attribute__((used)) static volatile LONG render_rng_in_slot;
static int render_rng_mode,render_rng_installed,render_rng_restored;
static uint64_t render_rng_state; /* private render word, persists across frames */
static uint64_t render_rng_calls,render_rng_steps,render_rng_unknown,render_rng_ids,render_rng_foreign;
static uint64_t render_rng_frame=UINT64_MAX,render_rng_frame_calls,render_rng_frame_steps,render_rng_frame_ids;
static uint64_t render_rng_entry,render_rng_exit;
static volatile LONG64 render_rng_failures;
static const char *render_rng_stage="idle";
/* X4's generator step (Random::*State 0x14ddb30..0x14ddcf0; tools/dev/rng_steps.c). */
static uint64_t render_rng_step(uint64_t v) {v=v*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return (v>>30)|(v<<34);}
static uint64_t render_rng_distance(uint64_t from,uint64_t to) {
    for(uint64_t n=0;n<=RENDER_RNG_STEP_BOUND;n++) {if(from==to) return n;from=render_rng_step(from);}
    return UINT64_MAX;
}
static void render_rng_execute(render_rng_fn original,void *self,volatile uint64_t *word,const volatile uint64_t *inserts) {
    const int split=render_rng_mode==RENDER_RNG_SPLIT;
    const uint64_t caller=*word,start=split?render_rng_state:caller,ids=inserts?*inserts:0;
    volatile uint64_t after=start;
    if(stats.issued!=render_rng_frame) {render_rng_frame=stats.issued;render_rng_frame_calls=render_rng_frame_steps=render_rng_frame_ids=0;}
    render_rng_entry=caller;++render_rng_calls;++render_rng_frame_calls;
    const int presented=predict_present_enter();
    __try {
        if(split) *word=start;
        InterlockedExchange(&render_rng_in_slot,1);
        pres_render_call(original,self);
    } __finally {
        DWORD error=GetLastError();
        InterlockedExchange(&render_rng_in_slot,0);
        predict_present_leave(presented);
        after=*word;
        if(split) {render_rng_state=after;*word=caller;}
        render_rng_exit=*word;
        if(AbnormalTermination()) {InterlockedIncrement64(&render_rng_failures);render_rng_stage="exception";}
        SetLastError(error);
    }
    uint64_t steps=render_rng_distance(start,after);
    if(steps==UINT64_MAX) ++render_rng_unknown;
    else {render_rng_steps+=steps;render_rng_frame_steps+=steps;}
    if(inserts) {uint64_t added=*inserts-ids;render_rng_ids+=added;render_rng_frame_ids+=added;}
    render_rng_stage="done";
}
static void render_rng_move(void *self) {
    render_rng_fn original=(render_rng_fn)(image+RENDER_RNG_MOVE);
    if(!InterlockedCompareExchange(&render_rng_active,0,0)) {original(self);return;}
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    unsigned char *tls=slots?(unsigned char*)slots[0]:NULL;
    if(!tls || *(volatile uint32_t*)(tls+0x314)!=1) {++render_rng_foreign;original(self);return;}
    unsigned char *map=*(unsigned char*volatile*)(image+RENDER_RNG_ID_MAP);
    render_rng_execute(original,self,(volatile uint64_t*)(tls+0x318),map?(volatile uint64_t*)(map+0x38):NULL);
}
static int render_rng_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+RENDER_RNG_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+RENDER_RNG_ENTRY),value,expected);
    VirtualProtect(image+RENDER_RNG_ENTRY,8,old,&ignored);return actual==expected;
}
static int render_rng_pins(void) {
    return hash_ok(image+RENDER_RNG_MOVE,106,"5d20ad384ae9ce74bb15625b923fd33638ad20d3f9e553c5d9cb7d264b20ed6f") &&
        hash_ok(image+RENDER_RNG_CLIENT,174,"faf61b940146abf385a9fb155bf180348fcfc7b7246cd52284fa227bfa2629b2") &&
        hash_ok(image+RENDER_RNG_CAMERA,32,"67aa70787d093d7f2511df0076d4e86a07c3adb0a846cfeb50568d75f504b55d") &&
        /* Random::FloatState: the step model above. */
        hash_ok(image+0x14ddc90,0x78,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e") &&
        /* AutoIDMap InsertAtSlot: inc qword [r10+0x38]. */
        hash_ok(image+0xcc64d,4,"7e104436070ef74ec041571d6ced9696c075c6783c63a65a92125a49fc548d74") &&
        /* VBI dispatch: slots 0..16 in index order. */
        hash_ok(image+0xef4681,64,"161234434ac6f861075f55a3b7b5e4643a45c7b72045b225e4c6a3cdbc78cd6d");
}
/* The framediet DLL wraps this entry (rng_split) and the camera context
 * entries (camera); its skips would change what slot 10 draws and allocates. */
static int render_rng_foreign_patch(void) {
    return GetModuleHandleW(L"x4lan_framediet_64.dll")!=NULL ||
        *(void*volatile*)(image+RENDER_RNG_CLIENT_ENTRY)!=(void*)(image+RENDER_RNG_CLIENT) ||
        *(void*volatile*)(image+RENDER_RNG_CAMERA_ENTRY)!=(void*)(image+RENDER_RNG_CAMERA);
}
static int render_rng_arm(int mode) {
    if(render_rng_installed) return stats.active && mode==render_rng_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=RENDER_RNG_SPLIT && mode!=RENDER_RNG_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !render_rng_pins()) return 0;
    if(render_rng_foreign_patch()) {render_rng_stage="foreign_patch";return 0;}
    render_rng_state=lockstep_seed(RENDER_RNG_DOMAIN,0,0,0,0,0);render_rng_mode=mode;
    render_rng_calls=render_rng_steps=render_rng_unknown=render_rng_ids=render_rng_foreign=0;render_rng_frame=UINT64_MAX;
    if(!render_rng_swap(image+RENDER_RNG_MOVE,(void*)render_rng_move)) {render_rng_stage="entry";return 0;}
    render_rng_installed=1;render_rng_stage="armed";InterlockedExchange(&render_rng_active,1);return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 90. */
static int render_rng_check(void) {
    if(InterlockedCompareExchange64(&render_rng_failures,0,0)) return 0;
    const char *why=*(void*volatile*)(image+RENDER_RNG_ENTRY)!=(void*)render_rng_move?"entry_changed":
        render_rng_foreign_patch()?"foreign_patch":NULL;
    if(why) {render_rng_stage=why;InterlockedIncrement64(&render_rng_failures);return 0;}
    return 1;
}
static void render_rng_restore(void) {
    InterlockedExchange(&render_rng_active,0);
    if(!render_rng_installed) return;
    render_rng_restored=render_rng_swap((void*)render_rng_move,image+RENDER_RNG_MOVE); /* a foreign value stays */
    if(render_rng_restored) render_rng_installed=0;
}
static int render_rng_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d frame=%llu calls=%llu frame_calls=%llu steps=%llu frame_steps=%llu "
        "unknown=%llu ids=%llu frame_ids=%llu foreign=%llu entry=%016llx exit=%016llx render=%016llx restored=%d stage=%s failures=%lld",
        render_rng_mode==RENDER_RNG_SPLIT?"split":render_rng_mode==RENDER_RNG_OBSERVE?"observe":"off",render_rng_active,render_rng_installed,
        (unsigned long long)(render_rng_frame==UINT64_MAX?0:render_rng_frame),(unsigned long long)render_rng_calls,
        (unsigned long long)render_rng_frame_calls,(unsigned long long)render_rng_steps,(unsigned long long)render_rng_frame_steps,
        (unsigned long long)render_rng_unknown,(unsigned long long)render_rng_ids,(unsigned long long)render_rng_frame_ids,
        (unsigned long long)render_rng_foreign,(unsigned long long)render_rng_entry,(unsigned long long)render_rng_exit,
        (unsigned long long)render_rng_state,render_rng_restored,render_rng_stage,(long long)render_rng_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
