/* Determinism fix, DEFAULT OFF: a build storage's next UpdateBuildEvent is
 * scheduled from a keyed stream (native_gate_build_rng = true, needs
 * native_gate_world_identities).
 *
 * full-Ekb (overlay-FullG3 + native_gate_event_rng = 'key', build74): every
 * keyed delivery agrees on both clients except U::UpdateBuildEvent
 * (vtable 0x2ac1710, type 0x20d) on U::BuildStorage sources (0x2aaca18): 1432
 * of 1436 deliveries carry other due times per client, all inside
 * [67.083, 77.083) s, i.e. the snapshot's load time 7.083 + 60 + [0, 10). The
 * scheduler 0x428800 (BuildStorage, time) posts at the given time, or for
 * time <= 0 at now + 60 + Random::FloatRange(TLS+0x318, 10.0) (call 0x42892e);
 * the snapshot load reschedules nearly every build storage that way on the
 * loading thread, whose word differs per client. From tick 3600 on, the build
 * storages update at other ticks on each client (construction resources and
 * their trade offers), and natively the listeners' draws then shift the frame
 * word: the frame word of full-G4, G5, G7a and G7b parts at 3601 for this.
 *
 * With this, the call at 0x42892e goes through a trampoline on a private page
 * (mov r8, rdi: the build storage, held in rdi from 0x428816; then jmp) to
 * brng_draw: the draw runs on a private state seeded from (storage portable
 * key, the thread's game time TLS+0x300) instead of the thread's word, which is
 * left untouched, during the load as in admitted frames (both clients load the
 * same save at the same game time). A storage without a portable key draws
 * from (its class vtable, game time), the same for every such storage of that
 * class at that time (counted fallback=); one whose vtable is not in the image
 * draws natively (counted). Pins: the scheduler 0x428800..0x428970 with our
 * displacement read as the original, and Random::FloatRange 0x14ddc90.
 * Written by CAS at arm (before the snapshot load), re-checked every gate tick
 * (stop reason 152), restored at stop. */
#define BRNG_DOMAIN 0x58344c4255494c44ULL /* "X4LBUILD" */
#define BRNG_SITE 0x42892eu  /* call Random::FloatRange in the build storage's update scheduler */
#define BRNG_DRAW 0x14ddc90u /* Random::FloatRange(uint64_t *word, float range) */
#define BRNG_SCHEDULER 0x428800u
#define BRNG_STOP_REASON 152u /* 150 gravidar wide, 151 event RNG */
typedef float (*brng_draw_fn)(uint64_t *,float);
static int brng_installed,brng_restored,brng_done;
static unsigned char *brng_page;
static uint64_t brng_keyed,brng_loading,brng_native,brng_unmapped,brng_fallback;
static volatile LONG64 brng_failures;
static const char *brng_stage="idle";
static int32_t brng_rel(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+BRNG_SITE+5));}
/* The keyed draw (testable with any draw function). word is the thread's TLS+0x318; its game time is TLS+0x300. */
static float brng_run(brng_draw_fn draw,uint64_t *word,float range,const void *storage) {
    uint64_t now=0,runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(!word || !read_local((const unsigned char*)word-0x18,&now,8)) {++brng_unmapped;return draw(word,range);}
    if(storage && read_local((const unsigned char*)storage+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!(stats.active && boot_phase==3)) ++brng_loading;
    if(!key) {
        void *vt=NULL;uintptr_t base=(uintptr_t)image;
        if(storage && read_local(storage,&vt,8) && (uintptr_t)vt>=base && (uintptr_t)vt<base+0x6fa9000) {
            uint64_t state=lockstep_seed(BRNG_DOMAIN,0,0,(uint64_t)((uintptr_t)vt-base),now,WORLD_KEY_NONE);++brng_fallback;
            return draw(&state,range);
        }
        ++brng_unmapped;return draw(word,range);
    }
    uint64_t state=lockstep_seed(BRNG_DOMAIN,0,key,now,0,klass);++brng_keyed;
    return draw(&state,range);
}
static float brng_draw(uint64_t *word,float range,void *storage) {
    return brng_run((brng_draw_fn)(void *)(image+BRNG_DRAW),word,range,storage);
}
/* mov r8, rdi; jmp [rip]; abs64 brng_draw */
static void brng_write_trampoline(unsigned char *p) {
    static const unsigned char code[9]={0x4c,0x8b,0xc7,0xff,0x25,0,0,0,0};void *t=(void*)brng_draw;
    memcpy(p,code,9);memcpy(p+9,&t,8);
}
static int brng_trampoline_ok(const unsigned char *p) {
    unsigned char want[17];brng_write_trampoline(want);return p && !memcmp(p,want,17);
}
/* 0 original, 1 ours, -1 anything else. */
static int brng_state(void) {
    unsigned char now[5];if(!read_local(image+BRNG_SITE,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==brng_rel(image+BRNG_DRAW)) return 0;
    if(brng_page && d==brng_rel(brng_page)) return 1;
    return -1;
}
static int brng_pins(void) {
    static unsigned char copy[0x170];
    if(brng_state()<0 || !read_local(image+BRNG_SCHEDULER,copy,sizeof(copy))) return 0;
    int32_t d=brng_rel(image+BRNG_DRAW);memcpy(copy+(BRNG_SITE-BRNG_SCHEDULER)+1,&d,4);
    return hash_ok(copy,sizeof(copy),"33a993a1da6cfc2c3ec78e34133f0d8eba867b88e67b22b9bbbd596468a49a11") &&
           hash_ok(image+BRNG_DRAW,0x78,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e");
}
static unsigned char *brng_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+BRNG_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+BRNG_SITE+5);
            if(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int brng_cas(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(BRNG_SITE+1,e,v,4);
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; needs the world identities. */
static int brng_arm(void) {
    if(brng_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {brng_stage="needs";return 0;}
    if(!brng_pins() || brng_state()!=0) {brng_stage="pins";return 0;}
    if(!brng_page) brng_page=brng_alloc_page();
    if(!brng_page) {brng_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(brng_page,0x1000,PAGE_READWRITE,&old)) {brng_stage="page";return 0;}
    brng_write_trampoline(brng_page);
    if(!VirtualProtect(brng_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {brng_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),brng_page,0x1000);
    brng_keyed=brng_loading=brng_native=brng_unmapped=brng_fallback=0;brng_failures=0;brng_restored=0;
    if(!brng_cas(brng_rel(image+BRNG_DRAW),brng_rel(brng_page))) {brng_stage="cas";return 0;}
    brng_done=1;brng_installed=1;brng_stage="armed";return 1;
}
static int brng_owned(void) {return !brng_done || (brng_state()==1 && brng_trampoline_ok(brng_page));}
/* Main-thread tick check; any failure stops the gate with reason 152. */
static int brng_check(void) {
    if(InterlockedCompareExchange64(&brng_failures,0,0)) return 0;
    const char *why=!brng_owned()?"site_changed":!brng_pins()?"block_changed":NULL;
    if(why) {brng_stage=why;InterlockedIncrement64(&brng_failures);return 0;}
    return 1;
}
static void brng_restore(void) {
    if(!brng_installed) return;
    if(brng_done && brng_state()==1 && brng_cas(brng_rel(brng_page),brng_rel(image+BRNG_DRAW))) brng_done=0;
    if(!brng_done) {brng_installed=0;brng_restored=1;}
}
static int brng_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d frame=%llu keyed=%llu loading=%llu native=%llu unmapped=%llu fallback=%llu restored=%d stage=%s failures=%lld",
        brng_installed,(unsigned long long)stats.issued,(unsigned long long)brng_keyed,(unsigned long long)brng_loading,
        (unsigned long long)brng_native,(unsigned long long)brng_unmapped,(unsigned long long)brng_fallback,brng_restored,brng_stage,
        (long long)brng_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
