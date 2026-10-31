/* Determinism fix, DEFAULT OFF: camera-independent bullet hit-test cadence
 * (native_gate_bullet_cadence; player tour runs in reports/LOCKSTEP-CENSUS-20261028.txt).
 * U::Bullet::UpdateFlight 0xd4f240 ends every hit test by scheduling the next
 * one (+0x268 last test = now, +0x270 next test). The block at 0xd503ea keeps
 * next = now while the bullet's view-cache dirty word (vc+0x3c, read through
 * the MoveWorker's cache index) has bit 0x40000 set, or while
 * GetCachedDistanceSqToCamera 0x3e40a0 (cutscene camera, else the player
 * camera g_Universe+0x3d0, from its per-cache-index context cache) is below
 * 2.5e7 (5 km); otherwise next = now + 0.1 s + Random::FloatState(TLS+0x318,
 * 0.1). So the cadence of every bullet hit test, and the frame in which its
 * damage lands, depends on the camera: near it every frame without a draw;
 * far from it (player tour) one interval per test, so the frame at which the
 * camera is first reported far, or any per-bullet difference in the flag or
 * distance, moves every later test frame. Measured: with the camera warped
 * 70 km from the fixture battle, the fixture bullets switch to drawn intervals
 * three frames after the warp (tour T3 rows), and the first hits land in a
 * different set of frames per client and per run (T1-T4) at equal positions,
 * while the camera-near control (C1) lands them together and stays identical.
 * Which input differs between clients is not yet measured (see the counters).
 * 'every' rewrites the jne at 0xd50414 (75 33 -> 0xd50449) to jmp (eb 33):
 * every bullet is tested every frame, as near the camera, independent of the
 * camera, the view cache and the thread RNG; nothing else changes. The
 * scheduling block is pinned byte for byte (SHA-256 with the original jne),
 * the rewrite is a 16-bit CAS, re-checked every gate tick (stop reason 94 on
 * any change) and restored by CAS on stop; a foreign value is never
 * overwritten. 'observe' only counts. Both modes count, per admitted frame and
 * for fixture-gun bullets and all other bullets apart, the Move calls on the
 * MoveWorker, the ones that ran a hit test and whether the next test was
 * scheduled for the same frame or later; the counters come from the bullet
 * module's Move wrapper, so native_gate_bullet_rng or native_gate_bullet_diag
 * is required. The status also samples the player camera: mode, anchor and the
 * two per-cache-index context caches GetCachedDistanceSqToCamera reads. */
#define BULLET_CADENCE_SITE 0xd50414u
#define BULLET_CADENCE_BLOCK 0xd503eau
#define BULLET_CADENCE_BLOCK_SIZE 0x95u
#define BULLET_CADENCE_ORIGINAL 0x3375u /* jne rel8 +0x33 (little endian 75 33) */
#define BULLET_CADENCE_PATCHED 0x33ebu  /* jmp rel8 +0x33 (eb 33) */
#define BULLET_CADENCE_BLOCK_SHA "6ecd9dafff6d0b4d5ac12e34aad61f4b6901ab66b9cc5ebfc1debf5c22f95d99"
#define BULLET_CADENCE_CUTSCENE 0x3d19900u /* cutscene manager; +0xb0 camera (preferred by 0x3e40a0) */
enum {BULLET_CADENCE_OFF,BULLET_CADENCE_EVERY,BULLET_CADENCE_OBSERVE};
typedef struct {uint64_t moves,tested,same_frame,later;} bullet_cadence_counts;
static int bullet_cadence_mode,bullet_cadence_installed,bullet_cadence_restored,bullet_cadence_patched;
static volatile LONG bullet_cadence_active;
static bullet_cadence_counts bullet_cadence_frame[2],bullet_cadence_total[2]; /* [0] fixture guns, [1] other bullets */
static uint64_t bullet_cadence_tick=UINT64_MAX,bullet_cadence_first_later,bullet_cadence_unreadable;
static volatile LONG64 bullet_cadence_failures;
static const char *bullet_cadence_stage="idle";
/* Pin of the pinned build's scheduling block with the original jne in place,
 * so the same check holds before the rewrite and (on a copy) after it. */
static int bullet_cadence_block_ok(void) {
    unsigned char copy[BULLET_CADENCE_BLOCK_SIZE];uint16_t site;
    if(!read_local(image+BULLET_CADENCE_BLOCK,copy,sizeof(copy))) return 0;
    memcpy(&site,copy+(BULLET_CADENCE_SITE-BULLET_CADENCE_BLOCK),2);
    if(site!=BULLET_CADENCE_ORIGINAL && site!=BULLET_CADENCE_PATCHED) return 0;
    site=BULLET_CADENCE_ORIGINAL;memcpy(copy+(BULLET_CADENCE_SITE-BULLET_CADENCE_BLOCK),&site,2);
    return hash_ok(copy,sizeof(copy),BULLET_CADENCE_BLOCK_SHA);
}
static int bullet_cadence_pins(void) {
    /* Bullet::UpdateFlight's entry (the start of the tested hit-test path) and the scheduling block. */
    return hash_ok(image+0xd4f240,32,"01ffd1932d8f63d8b118c69a668b24e20baf29b691ee3acbf4738ff52d8e29f1") && bullet_cadence_block_ok();
}
static uint16_t bullet_cadence_site(void) {uint16_t v=0;read_local(image+BULLET_CADENCE_SITE,&v,2);return v;}
static int bullet_cadence_swap(uint16_t expected,uint16_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+BULLET_CADENCE_SITE,2,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint16_t actual=(uint16_t)InterlockedCompareExchange16((volatile SHORT*)(image+BULLET_CADENCE_SITE),(SHORT)value,(SHORT)expected);
    VirtualProtect(image+BULLET_CADENCE_SITE,2,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+BULLET_CADENCE_SITE,2);
    return actual==expected;
}
static int bullet_cadence_foreign_module(void) {
    return GetModuleHandleW(L"x4lan_fixedtick_64.dll")!=NULL || GetModuleHandleW(L"x4mp_combat.dll")!=NULL;
}
static void bullet_cadence_roll(void) {
    if(bullet_cadence_tick==stats.issued) return;
    bullet_cadence_tick=stats.issued;memset(bullet_cadence_frame,0,sizeof(bullet_cadence_frame));
}
/* Called by the bullet module's Move wrapper on the MoveWorker, around the
 * original Move (registered fixture bullets and all others). */
static void bullet_cadence_enter_tls(const void *bullet,bullet_cadence_sample *c,const unsigned char *tls) {
    c->valid=0;
    if(!InterlockedCompareExchange(&bullet_cadence_active,0,0)) return;
    if(!tls || !read_local(tls+0x300,&c->now,8) || !read_local((const unsigned char*)bullet+0x270,&c->next,8) ||
       !read_local((const unsigned char*)bullet+0x268,&c->last,8)) {++bullet_cadence_unreadable;return;}
    c->valid=1;
}
static void bullet_cadence_enter(const void *bullet,bullet_cadence_sample *c) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    bullet_cadence_enter_tls(bullet,c,slots?(const unsigned char*)slots[0]:NULL);
}
static void bullet_cadence_leave(const void *bullet,const bullet_cadence_sample *c,unsigned other) {
    if(!c->valid) return;
    double last,next;unsigned k=other?1u:0u;
    bullet_cadence_roll();++bullet_cadence_frame[k].moves;++bullet_cadence_total[k].moves;
    if(!read_local((const unsigned char*)bullet+0x268,&last,8) || !read_local((const unsigned char*)bullet+0x270,&next,8)) {++bullet_cadence_unreadable;return;}
    /* A hit test ran when it was due and stamped this frame's time as its last test. */
    if(!(c->next<=c->now) || last!=c->now || c->last==c->now) return;
    ++bullet_cadence_frame[k].tested;++bullet_cadence_total[k].tested;
    if(next<=c->now) {++bullet_cadence_frame[k].same_frame;++bullet_cadence_total[k].same_frame;}
    else {
        ++bullet_cadence_frame[k].later;++bullet_cadence_total[k].later;
        if(!bullet_cadence_first_later) bullet_cadence_first_later=stats.issued;
    }
}
static int bullet_cadence_arm(int mode) {
    if(bullet_cadence_installed) return stats.active && mode==bullet_cadence_mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=BULLET_CADENCE_EVERY && mode!=BULLET_CADENCE_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread) return 0;
    if(!bullet_installed || !bullet_owned()) {bullet_cadence_stage="bullet_module";return 0;}
    if(bullet_cadence_foreign_module()) {bullet_cadence_stage="foreign_module";return 0;}
    if(!bullet_cadence_pins()) {bullet_cadence_stage="pins";return 0;}
    if(bullet_cadence_site()!=BULLET_CADENCE_ORIGINAL) {bullet_cadence_stage="site";return 0;}
    memset(bullet_cadence_frame,0,sizeof(bullet_cadence_frame));memset(bullet_cadence_total,0,sizeof(bullet_cadence_total));
    bullet_cadence_tick=UINT64_MAX;bullet_cadence_first_later=bullet_cadence_unreadable=0;bullet_cadence_restored=0;
    if(mode==BULLET_CADENCE_EVERY) {
        if(!bullet_cadence_swap(BULLET_CADENCE_ORIGINAL,BULLET_CADENCE_PATCHED)) {bullet_cadence_stage="cas";return 0;}
        bullet_cadence_patched=1;
    }
    bullet_cadence_mode=mode;bullet_cadence_installed=1;bullet_cadence_stage="armed";
    InterlockedExchange(&bullet_cadence_active,1);return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 94. */
static int bullet_cadence_check(void) {
    if(InterlockedCompareExchange64(&bullet_cadence_failures,0,0)) return 0;
    const char *why=bullet_cadence_foreign_module()?"foreign_module":
        (bullet_cadence_patched && bullet_cadence_site()!=BULLET_CADENCE_PATCHED)?"site_changed":
        (!bullet_cadence_patched && bullet_cadence_site()!=BULLET_CADENCE_ORIGINAL)?"site_changed":
        !bullet_cadence_block_ok()?"block_changed":(!bullet_installed || !bullet_owned())?"bullet_module":NULL;
    if(why) {bullet_cadence_stage=why;InterlockedIncrement64(&bullet_cadence_failures);return 0;}
    return 1;
}
static int bullet_cadence_owned(void) {
    return !bullet_cadence_patched || bullet_cadence_site()==BULLET_CADENCE_PATCHED;
}
static void bullet_cadence_restore(void) {
    InterlockedExchange(&bullet_cadence_active,0);
    if(!bullet_cadence_installed) return;
    if(bullet_cadence_patched) {
        if(!bullet_cadence_swap(BULLET_CADENCE_PATCHED,BULLET_CADENCE_ORIGINAL)) return; /* a foreign value stays */
        bullet_cadence_patched=0;
    }
    bullet_cadence_restored=1;bullet_cadence_installed=0;
}
/* Diagnostic snapshot of the player camera as GetCachedDistanceSqToCamera sees
 * it: mode +0x868, anchor +0x770, and per view-cache index i the cached
 * attached component (+0x30+i*0x2b0) and the number of cached context
 * transforms (+0x2d0+i*0x2b0). Raw reads only; IDs are UniverseIDs. */
static int bullet_cadence_camera(char *out,size_t capacity) {
    unsigned char *universe=NULL,*camera=NULL,*cutscene=NULL,*anchor=NULL,*cut_camera=NULL;
    uint32_t mode=0,alt=0,index=0;uint64_t anchor_id=0;
    if(!read_local(image+0x3d198f8,&universe,8) || !universe || !read_local(universe+0x3d0,&camera,8) || !camera)
        return snprintf(out,capacity,"none");
    if(read_local(image+BULLET_CADENCE_CUTSCENE,&cutscene,8) && cutscene) read_local(cutscene+0xb0,&cut_camera,8);
    read_local(camera+0x868,&mode,4);
    if(read_local(camera+0x770,&anchor,8) && anchor) read_local(anchor+8,&anchor_id,8);
    read_local(image+0x38f4eb4,&index,4);read_local(image+0x38f4ee0,&alt,4);
    int n=snprintf(out,capacity,"%u:%llx:%u:%u:%u",mode,(unsigned long long)anchor_id,cut_camera!=NULL,index,alt);
    for(unsigned i=0;i<2 && n>0 && (size_t)n<capacity;i++) {
        unsigned char *attached=NULL;uint64_t attached_id=0,count=UINT64_MAX;
        if(read_local(camera+0x30+i*0x2b0,&attached,8) && attached) read_local(attached+8,&attached_id,8);
        if(!read_local(camera+0x2d0+i*0x2b0,&count,8)) count=UINT64_MAX;
        n+=snprintf(out+n,capacity-(size_t)n,":%llx/%lld",(unsigned long long)attached_id,count==UINT64_MAX?-1LL:(long long)count);
    }
    return n;
}
static int bullet_cadence_status(char *out,size_t capacity) {
    char camera[160];if(bullet_cadence_camera(camera,sizeof(camera))<=0) strcpy(camera,"error");
    const bullet_cadence_counts *f=bullet_cadence_frame,*t=bullet_cadence_total;
    int fresh=bullet_cadence_tick==stats.issued;
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d site=%04x frame=%llu "
        "fixture=%llu/%llu/%llu/%llu other=%llu/%llu/%llu/%llu total_fixture=%llu/%llu/%llu/%llu total_other=%llu/%llu/%llu/%llu "
        "first_later=%llu unreadable=%llu camera=%s restored=%d stage=%s failures=%lld",
        bullet_cadence_mode==BULLET_CADENCE_EVERY?"every":bullet_cadence_mode==BULLET_CADENCE_OBSERVE?"observe":"off",
        bullet_cadence_active,bullet_cadence_installed,bullet_cadence_site(),
        (unsigned long long)(bullet_cadence_tick==UINT64_MAX?0:bullet_cadence_tick),
        (unsigned long long)(fresh?f[0].moves:0),(unsigned long long)(fresh?f[0].tested:0),
        (unsigned long long)(fresh?f[0].same_frame:0),(unsigned long long)(fresh?f[0].later:0),
        (unsigned long long)(fresh?f[1].moves:0),(unsigned long long)(fresh?f[1].tested:0),
        (unsigned long long)(fresh?f[1].same_frame:0),(unsigned long long)(fresh?f[1].later:0),
        (unsigned long long)t[0].moves,(unsigned long long)t[0].tested,(unsigned long long)t[0].same_frame,(unsigned long long)t[0].later,
        (unsigned long long)t[1].moves,(unsigned long long)t[1].tested,(unsigned long long)t[1].same_frame,(unsigned long long)t[1].later,
        (unsigned long long)bullet_cadence_first_later,(unsigned long long)bullet_cadence_unreadable,camera,
        bullet_cadence_restored,bullet_cadence_stage,(long long)bullet_cadence_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
