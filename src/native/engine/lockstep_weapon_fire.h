/* Determinism fix, DEFAULT OFF: every weapon shot draws from its own keyed
 * stream (native_gate_weapon_fire_rng).
 *
 * The weapon fire procedure 0x817d20 (from Weapon::Move, 0x819a90 and
 * Scripts::TurretAction::Execute) loops over the shots due this frame and calls
 * vtable slot 998 (+0x1f30) once per shot: U::Weapon::vfunc998 0x8185b0 (also
 * the U::Turret slot). It stamps the shot time n * interval + base + a jitter
 * drawn from the calling thread's word (Random::FloatState(TLS+0x318) at
 * 0x8186b2), creates the bullet and spawns the muzzle effect with that time;
 * 0xd73060 then creates each effect element at once or defers it to an
 * ActivateEffectEvent (time + delay ahead of the game time). Later draws of the
 * shot (spread 0x81900d.., effect chance and placement) use the same word.
 * cam-B10 EFFECT_ROW: at tick 20 the game time is identical on both clients
 * but five fixture weapons' shot times differ by 1..21 ms (176c5f 7.422033 vs
 * 7.400649, deferred vs immediate) while the first weapon of the same ships
 * agrees: the word reaching the jitter differs, i.e. some earlier consumer of
 * that stream drew differently on one client.
 *
 * With this, the two slot-998 entries (U::Weapon 0x2b6a1a8, U::Turret 0x2b70840)
 * point at weapon_fire_shot: on the MoveWorker in admitted frames the shot
 * runs with the TLS word seeded from (tick, weapon portable key, shot index,
 * per-tick call ordinal) and the caller's word restored afterwards, so the
 * shot's time, spread, bullet and effects depend only on the shot's identity.
 * A weapon without a portable key, another thread or a loading frame runs
 * natively (counted). Pins: the vfunc head and the fire loop's slot-998 call;
 * entries swapped by CAS of the data pointer; every gate tick re-checks them
 * (stop reason 132); stop restores. */
#define WFIRE_DOMAIN 0x5746495245534854ULL /* "WFIRESHT" */
#define WFIRE_FN 0x8185b0u
#define WFIRE_ENTRIES 2u
#define WFIRE_STOP_REASON 132u /* 131 is the command order module's */
#define WFIRE_ORDINALS 4096u
static const unsigned wfire_entries[WFIRE_ENTRIES]={0x2b6a1a8u,0x2b70840u}; /* U::Weapon, U::Turret slot 998 */
typedef double (*wfire_fn)(void *,void *,void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static int wfire_installed,wfire_restored;
static unsigned wfire_swapped;
static uint64_t wfire_keyed,wfire_native,wfire_unmapped,wfire_tick=UINT64_MAX,wfire_frame_keyed,wfire_table_full;
static volatile LONG64 wfire_failures;
static const char *wfire_stage="idle";
static struct {uint64_t key,tick,count;} wfire_ordinals[WFIRE_ORDINALS];
static uint64_t wfire_ordinal(uint64_t key) {
    unsigned h=(unsigned)lockstep_mix(key)&(WFIRE_ORDINALS-1u);
    for(unsigned n=0;n<64;n++,h=(h+1)&(WFIRE_ORDINALS-1u)) {
        if(wfire_ordinals[h].tick!=stats.issued || !wfire_ordinals[h].key) {wfire_ordinals[h].key=key;wfire_ordinals[h].tick=stats.issued;wfire_ordinals[h].count=1;return 1;}
        if(wfire_ordinals[h].key==key) return ++wfire_ordinals[h].count;
    }
    ++wfire_table_full;return 0;
}
/* One shot: the keyed scope around the original (testable with any original and word). */
static double wfire_run(wfire_fn original,uint64_t *rng,void *weapon,void *a2,void *a3,uint64_t shot,uint64_t a5,uint64_t a6,uint64_t a7,
                        uint64_t a8,uint64_t a9,uint64_t a10,uint64_t a11,uint64_t a12) {
    if(wfire_tick!=stats.issued) {wfire_tick=stats.issued;wfire_frame_keyed=0;}
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(read_local((unsigned char*)weapon+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key || !rng) {++wfire_unmapped;return original(weapon,a2,a3,shot,a5,a6,a7,a8,a9,a10,a11,a12);}
    uint64_t ordinal=wfire_ordinal(key),saved=*rng;double result=0;
    ++wfire_keyed;++wfire_frame_keyed;
    __try {
        *rng=lockstep_seed(WFIRE_DOMAIN,stats.issued,key,shot,ordinal,klass);
        result=original(weapon,a2,a3,shot,a5,a6,a7,a8,a9,a10,a11,a12);
    } __finally {
        DWORD error=GetLastError();*rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64(&wfire_failures);
        SetLastError(error);
    }
    return result;
}
static double wfire_shot(void *weapon,void *a2,void *a3,uint64_t shot,uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,
                         uint64_t a10,uint64_t a11,uint64_t a12) {
    wfire_fn original=(wfire_fn)(void *)(image+WFIRE_FN);
    if(!(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread)) {++wfire_native;return original(weapon,a2,a3,shot,a5,a6,a7,a8,a9,a10,a11,a12);}
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    return wfire_run(original,rng,weapon,a2,a3,shot,a5,a6,a7,a8,a9,a10,a11,a12);
}
static int wfire_swap(unsigned i,void *expected,void *value) {
    unsigned char *p=image+wfire_entries[i];DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int wfire_pins(void) {
    return hash_ok(image+WFIRE_FN,0x110,"3087d540223f57a242f3dc851cac03c5ce61ab5bf52d5d573b8236fdf3484848") &&
           hash_ok(image+0x818120,0x80,"706cfd677a54317fd43fcb37f671702cf418c3842da1b2fa4a998bf12291df0b");
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int wfire_arm(void) {
    if(wfire_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {wfire_stage="needs";return 0;}
    if(!wfire_pins()) {wfire_stage="pins";return 0;}
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if(*(void**)(image+wfire_entries[i])!=(void*)(image+WFIRE_FN)) {wfire_stage="entry";return 0;}
    memset(wfire_ordinals,0,sizeof(wfire_ordinals));
    wfire_keyed=wfire_native=wfire_unmapped=wfire_frame_keyed=wfire_table_full=0;wfire_tick=UINT64_MAX;wfire_failures=0;wfire_restored=0;
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) {
        if(!wfire_swap(i,image+WFIRE_FN,(void*)wfire_shot)) {
            for(unsigned j=0;j<i;j++) wfire_swap(j,(void*)wfire_shot,image+WFIRE_FN);
            wfire_swapped=0;wfire_stage="cas";return 0;
        }
        wfire_swapped|=1u<<i;
    }
    wfire_installed=1;wfire_stage="armed";return 1;
}
static int wfire_owned(void) {
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if((wfire_swapped&(1u<<i)) && *(void**)(image+wfire_entries[i])!=(void*)wfire_shot) return 0;
    return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 132. */
static int wfire_check(void) {
    if(InterlockedCompareExchange64(&wfire_failures,0,0)) return 0;
    const char *why=!wfire_owned()?"entry_changed":!wfire_pins()?"block_changed":wfire_table_full?"ordinal_table":NULL;
    if(why) {wfire_stage=why;InterlockedIncrement64(&wfire_failures);return 0;}
    return 1;
}
static void wfire_restore(void) {
    if(!wfire_installed) return;
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if((wfire_swapped&(1u<<i)) && wfire_swap(i,(void*)wfire_shot,image+WFIRE_FN)) wfire_swapped&=~(1u<<i);
    if(!wfire_swapped) {wfire_installed=0;wfire_restored=1;}
}
static int wfire_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d frame=%llu keyed=%llu total_keyed=%llu native=%llu unmapped=%llu table_full=%llu restored=%d stage=%s failures=%lld",
        wfire_installed,(unsigned long long)stats.issued,(unsigned long long)(wfire_tick==stats.issued?wfire_frame_keyed:0),
        (unsigned long long)wfire_keyed,(unsigned long long)wfire_native,(unsigned long long)wfire_unmapped,
        (unsigned long long)wfire_table_full,wfire_restored,wfire_stage,(long long)wfire_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
