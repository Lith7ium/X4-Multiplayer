/* Determinism fix, DEFAULT OFF: the engine-parameter refresh of a ship is
 * rescheduled from a keyed stream (native_gate_engine_refresh_rng = true, needs
 * native_gate_world_identities).
 *
 * A ship's flight model does not read its engines continuously: the flight
 * parameter slots of its pathing agent (agent + i*0x1a0: +0x24 max speed,
 * +0x191 boost, +0x192 travel = the engine's +0x340, +0x198 travel speed) are
 * refreshed by 0xea1e80. NavigationController::OnUpdateEngineParameters
 * 0xea1d70 (the UpdateEngineParametersEvent listener) refreshes the written
 * slot and posts the next UpdateEngineParametersEvent to the ship at
 * now + 8.0 + Random::FloatRange(TLS+0x318, 2.0) (call 0xea1e3b): the delay
 * comes from the calling thread's word, which earlier consumers on that thread
 * (presentation, camera-dependent effects) leave per client. cam-B17/B18:
 * 78855's travel drive engaged at tick 421 on both clients (TravelModeStarted,
 * same time), its flight model took the travel flag and speed at the next
 * refresh, 574 on client 1 and 598 on client 2; 896ff left travel at 576 on
 * client 1 only.
 *
 * With this, the call at 0xea1e3b goes through a trampoline on a private page
 * (mov r8, rsi: the ship, which the listener holds in rsi from 0xea1e14; then
 * jmp) to erng_draw: in admitted frames the draw runs on a private state
 * seeded from (tick, ship portable key) instead of the thread's word, which is
 * left untouched. A ship without a portable key (cam-L1: 63 of 36188 draws,
 * equal on both clients) draws from (tick, its class vtable) instead, the
 * same for every such ship of that class in that tick (counted fallback=); a
 * ship whose vtable is not in the image, or a loading frame, draws natively
 * (counted). Pins: the listener in two blocks around its refresh
 * call 0xea1d9a (the motion watch may route that one) with our displacement
 * read as the original, and Random::FloatRange 0x14ddc90. Written by CAS at
 * arm (before the first admitted frame), re-checked every gate tick (stop
 * reason 136), restored at stop. */
#define ERNG_DOMAIN 0x454e47524546524eULL /* "ENGREFRN" */
#define ERNG_SITE 0xea1e3bu  /* call Random::FloatRange in OnUpdateEngineParameters */
#define ERNG_DRAW 0x14ddc90u /* Random::FloatRange(uint64_t *word, float range) */
#define ERNG_STOP_REASON 136u /* 135 is the shape order module's */
typedef float (*erng_draw_fn)(uint64_t *,float);
static int erng_installed,erng_restored,erng_done;
static unsigned char *erng_page;
static uint64_t erng_keyed,erng_native,erng_unmapped,erng_fallback;
static volatile LONG64 erng_failures;
static const char *erng_stage="idle";
static int32_t erng_rel(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+ERNG_SITE+5));}
/* The keyed draw (testable with any draw function). */
static float erng_run(erng_draw_fn draw,uint64_t *word,float range,const void *ship) {
    if(!(stats.active && boot_phase==3)) {++erng_native;return draw(word,range);}
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(ship && read_local((const unsigned char*)ship+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key) {
        void *vt=NULL;uintptr_t base=(uintptr_t)image;
        if(ship && read_local(ship,&vt,8) && (uintptr_t)vt>=base && (uintptr_t)vt<base+0x6fa9000) {
            uint64_t state=lockstep_seed(ERNG_DOMAIN,stats.issued,0,(uint64_t)((uintptr_t)vt-base),1,WORLD_KEY_NONE);++erng_fallback;
            return draw(&state,range);
        }
        ++erng_unmapped;return draw(word,range);
    }
    uint64_t state=lockstep_seed(ERNG_DOMAIN,stats.issued,key,0,0,klass);++erng_keyed;
    return draw(&state,range);
}
static float erng_draw(uint64_t *word,float range,void *ship) {
    return erng_run((erng_draw_fn)(void *)(image+ERNG_DRAW),word,range,ship);
}
/* mov r8, rsi; jmp [rip]; abs64 erng_draw */
static void erng_write_trampoline(unsigned char *p) {
    static const unsigned char code[9]={0x4c,0x8b,0xc6,0xff,0x25,0,0,0,0};void *t=(void*)erng_draw;
    memcpy(p,code,9);memcpy(p+9,&t,8);
}
static int erng_trampoline_ok(const unsigned char *p) {
    unsigned char want[17];erng_write_trampoline(want);return p && !memcmp(p,want,17);
}
/* 0 original, 1 ours, -1 anything else. */
static int erng_state(void) {
    unsigned char now[5];if(!read_local(image+ERNG_SITE,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==erng_rel(image+ERNG_DRAW)) return 0;
    if(erng_page && d==erng_rel(erng_page)) return 1;
    return -1;
}
static int erng_pins(void) {
    static unsigned char copy[0xd9];
    if(erng_state()<0 || !hash_ok(image+0xea1d70,0x2a,"972959c3540466f2dce7bd97d07c4cfa3e74047b237c8a6b098d406062e1ade6") ||
       !read_local(image+0xea1d9f,copy,0xd9)) return 0;
    int32_t d=erng_rel(image+ERNG_DRAW);memcpy(copy+(ERNG_SITE-0xea1d9fu)+1,&d,4);
    return hash_ok(copy,0xd9,"8a7377b01030ca412452d976698b4fa071b41c8a2e7af5598e025a6bbb917f76") &&
           hash_ok(image+ERNG_DRAW,0x78,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e");
}
static unsigned char *erng_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+ERNG_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+ERNG_SITE+5);
            if(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int erng_cas(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(ERNG_SITE+1,e,v,4);
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int erng_arm(void) {
    if(erng_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {erng_stage="needs";return 0;}
    if(!erng_pins() || erng_state()!=0) {erng_stage="pins";return 0;}
    if(!erng_page) erng_page=erng_alloc_page();
    if(!erng_page) {erng_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(erng_page,0x1000,PAGE_READWRITE,&old)) {erng_stage="page";return 0;}
    erng_write_trampoline(erng_page);
    if(!VirtualProtect(erng_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {erng_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),erng_page,0x1000);
    erng_keyed=erng_native=erng_unmapped=erng_fallback=0;erng_failures=0;erng_restored=0;
    if(!erng_cas(erng_rel(image+ERNG_DRAW),erng_rel(erng_page))) {erng_stage="cas";return 0;}
    erng_done=1;erng_installed=1;erng_stage="armed";return 1;
}
static int erng_owned(void) {return !erng_done || (erng_state()==1 && erng_trampoline_ok(erng_page));}
/* Main-thread tick check; any failure stops the gate with reason 136. */
static int erng_check(void) {
    if(InterlockedCompareExchange64(&erng_failures,0,0)) return 0;
    const char *why=!erng_owned()?"site_changed":!erng_pins()?"block_changed":NULL;
    if(why) {erng_stage=why;InterlockedIncrement64(&erng_failures);return 0;}
    return 1;
}
static void erng_restore(void) {
    if(!erng_installed) return;
    if(erng_done && erng_state()==1 && erng_cas(erng_rel(erng_page),erng_rel(image+ERNG_DRAW))) erng_done=0;
    if(!erng_done) {erng_installed=0;erng_restored=1;}
}
static int erng_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d frame=%llu keyed=%llu native=%llu unmapped=%llu fallback=%llu restored=%d stage=%s failures=%lld",
        erng_installed,(unsigned long long)stats.issued,(unsigned long long)erng_keyed,(unsigned long long)erng_native,
        (unsigned long long)erng_unmapped,(unsigned long long)erng_fallback,erng_restored,erng_stage,(long long)erng_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
