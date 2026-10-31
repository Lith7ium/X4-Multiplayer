/* Determinism fix, DEFAULT OFF: the economy schedule a station gets from the save is keyed
 * (native_gate_economy_load_rng = true; needs the world identities).
 *
 * play29/30/32/33-long part at 16021-16140 and the relay sees it at 16073-16127. All four clients
 * of play29 and play30 run the same game until then: frame word equal through tick 18000, every
 * event delivery's draws equal until 16020. play33 (UI_LAZY_ROW over every call of the lazy update,
 * SCRIPT_RNG rows): at 15991 both clients run the same find_trade_offer, under the same script key,
 * and call UpdateTradeOffers 0x4079e0 on station 139b9 with the same next-offer time; client 1 goes
 * on into its economy update 0x431f90 (brings cargo up from game time 538 to 540), client 2 returns
 * there because the station's next economy run (+0xa38) is still ahead. From then on each client runs
 * a different set of stations' economy updates (client 2 alone: 417a8, 42b3e at 16048), and their
 * production cycles, trade offer draws and orders follow.
 *
 * The save holds the last economy run only. Container::Import (U::Container vfunc568, 0x3f0530)
 * reads it into +0xa40 and calls 0x430cc0 at 0x3f0d95, which sets
 *   +0xa38 = last + base + spread - Random::FloatRange(TLS slot0+0x318, 2 spread)
 * from the global economy settings (*0x6cf1538 +0x5a38, +0x5a40). Nothing encloses the import in a
 * key, so that draw takes the loading thread's natural word, which differs between processes: each
 * client gives every station its own first economy run, and nothing differs until the first of them
 * comes due on one client only, about 540 s after the load. The component import 0x8e2ec0 reads the
 * component's ID (0x79f720, at 0x8e5685/0x8e578b) before it calls vfunc568 (0x8e6032), so the saved
 * ID is in the identity map when the draw is made.
 *
 * Fix: the call at 0x3f0d95 goes through a stub (jmp [rip]) to eco_entry, which runs 0x430cc0 with
 *   TLS+0x318 = lockstep_seed(ECO_DOMAIN, tick, saved ID, last run bits, 0, 0)
 * and puts the loading thread's word back afterwards. A container without a saved ID is keyed by the
 * count of such imports instead (the save is read in one order) and counted (unmapped=). The caller
 * reads no volatile register after the call before writing it (0x3f0d9a: r8, edx, rcx), and 0x430cc0
 * keeps rbx, rdi and xmm6, so the stub carries nothing.
 * Status: calls, keyed, unmapped, other threads, draws, and two order-free sums two clients compare:
 * keyset (the keys) and nextset (saved ID and resulting +0xa38 of every container).
 * Fail closed (stop reason 177): no TLS word, an unreadable container ID or schedule, an exception
 * through the scope; the site no longer ours or a pinned byte changed (the call window, 0x430cc0,
 * FloatRange). Written by CAS at arm, before the snapshot load; re-checked every gate tick; restored
 * at stop. */
#define ECO_STOP_REASON 177u /* 176 path budget */
#define ECO_SITE 0x3f0d95u
#define ECO_SCHEDULE 0x430cc0u
#define ECO_SCHEDULE_SIZE 0x72u
#define ECO_FLOAT_RANGE 0x14ddc90u
#define ECO_DOMAIN 0x58344c45434f4e4fULL   /* "X4LECONO" */
#define ECO_UNMAPPED 0x7f45434f554e4d50ULL /* "\x7fECOUNMP" */
#define ECO_STUB 0x20u
static const char *const eco_site_sha="c9f5937d8d55c962b4dbf0aec237c65ac64b2c4df43deb7193efd193b080c45f";
static const char *const eco_schedule_sha="1107bb64251d4f08c36e6dff4e3dc11fa38a45d6ff99063623b00b048bf26522";
static const char *const eco_float_range_sha="b979e629881a2dacae664e2e1849bc808bf1f2e12be10ab9deeb875f076fc027";
typedef void (*eco_schedule_fn)(void *,double);
static int eco_installed,eco_restored;
static unsigned char *eco_page;
static volatile LONG64 eco_calls,eco_keyed,eco_unmapped,eco_threads,eco_failures;
static uint64_t eco_draws,eco_keyset,eco_nextset;
static const char *eco_stage="idle";
static eco_schedule_fn eco_native; /* tests substitute */
static uint64_t *(*eco_word_fn)(void);
static uint64_t *eco_word(void) {
    if(eco_word_fn) return eco_word_fn();
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
static void eco_fail(const char *why) {eco_stage=why;InterlockedIncrement64(&eco_failures);}
/* At 0x3f0d95: the container in rcx, its last economy run in xmm1. */
static void eco_entry(void *container,double last) {
    eco_schedule_fn native=eco_native?eco_native:(eco_schedule_fn)(void *)(image+ECO_SCHEDULE);
    InterlockedIncrement64(&eco_calls);
    uint64_t *rng=eco_word(),runtime=0,saved=0,bits;memcpy(&bits,&last,8);
    if(!rng || !container || !read_local((unsigned char*)container+8,&runtime,8) || !runtime) {eco_fail("unreadable");native(container,last);return;}
    if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&eco_threads);
    saved=world_identity_lookup(runtime);
    uint64_t owner=saved?saved:ECO_UNMAPPED,ordinal=saved?0:(uint64_t)InterlockedIncrement64(&eco_unmapped);
    uint64_t key=lockstep_seed(ECO_DOMAIN,stats.issued,owner,bits,ordinal,0),natural=*rng,exit=key;
    DWORD error=GetLastError();*rng=key;SetLastError(error);
    __try {native(container,last);exit=*rng;}
    __finally {
        error=GetLastError();*rng=natural;
        if(AbnormalTermination()) eco_fail("unwound");
        SetLastError(error);
    }
    uint64_t next=0;
    if(!read_local((unsigned char*)container+0xa38,&next,8)) {eco_fail("schedule");return;}
    uint32_t steps=script_rng_steps(key,exit);
    InterlockedIncrement64(&eco_keyed);
    eco_draws+=steps==UINT32_MAX?0:steps;
    eco_keyset+=lockstep_mix(key);eco_nextset+=lockstep_mix(owner^lockstep_mix(next^ordinal));
}
static void eco_write_stub(unsigned char *p) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *to=(void*)eco_entry;
    memset(p,0xcc,ECO_STUB);memcpy(p,jump,sizeof(jump));memcpy(p+sizeof(jump),&to,8);
}
static int32_t eco_rel(const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+ECO_SITE+5));}
/* 0 native call, 1 ours, -1 anything else. */
static int eco_state(void) {
    unsigned char now[5];if(!read_local(image+ECO_SITE,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==eco_rel(image+ECO_SCHEDULE)) return 0;
    if(eco_page && d==eco_rel(eco_page)) return 1;
    return -1;
}
static int eco_pins(void) {
    unsigned char copy[0x20];
    if(eco_state()<0 || !read_local(image+ECO_SITE-0x10,copy,sizeof(copy))) return 0;
    int32_t d=eco_rel(image+ECO_SCHEDULE);memcpy(copy+0x11,&d,4);
    return hash_ok(copy,sizeof(copy),eco_site_sha) && hash_ok(image+ECO_SCHEDULE,ECO_SCHEDULE_SIZE,eco_schedule_sha) &&
        hash_ok(image+ECO_FLOAT_RANGE,32,eco_float_range_sha);
}
static unsigned char *eco_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+ECO_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+ECO_SITE+5);
            if(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int eco_cas(int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(ECO_SITE+1,e,v,4);
}
static void eco_restore(void) {
    if(!eco_installed) return;
    if(eco_state()==1 && eco_cas(eco_rel(eco_page),eco_rel(image+ECO_SCHEDULE))) {eco_installed=0;eco_restored=1;}
    else if(eco_state()==0) {eco_installed=0;eco_restored=1;}
}
/* Before the snapshot load, on the MoveWorker; a rerun is accepted. */
static int eco_arm(void) {
    if(eco_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identities) return 0;
    if(eco_state()!=0 || !eco_pins()) {eco_stage="pins";return 0;}
    if(!eco_page) eco_page=eco_alloc_page();
    if(!eco_page) {eco_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(eco_page,0x1000,PAGE_READWRITE,&old)) {eco_stage="page";return 0;}
    eco_write_stub(eco_page);
    if(!VirtualProtect(eco_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {eco_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),eco_page,0x1000);
    eco_restored=0;
    if(!eco_cas(eco_rel(image+ECO_SCHEDULE),eco_rel(eco_page))) {eco_stage="cas";return 0;}
    eco_installed=1;eco_stage="armed";return 1;
}
static int eco_owned(void) {
    if(!eco_installed) return 1;
    unsigned char want[ECO_STUB];eco_write_stub(want);
    return eco_state()==1 && !memcmp(eco_page,want,ECO_STUB);
}
static int eco_check(void) {
    if(InterlockedCompareExchange64(&eco_failures,0,0)) return 0;
    const char *why=!eco_owned()?"site_changed":!eco_pins()?"block_changed":NULL;
    if(why) {eco_fail(why);return 0;}
    return 1;
}
static int eco_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d calls=%lld keyed=%lld unmapped=%lld threads=%lld draws=%llu keyset=%016llx nextset=%016llx restored=%d stage=%s failures=%lld",
        eco_installed,(long long)eco_calls,(long long)eco_keyed,(long long)eco_unmapped,(long long)eco_threads,(unsigned long long)eco_draws,
        (unsigned long long)eco_keyset,(unsigned long long)eco_nextset,eco_restored,eco_stage,(long long)eco_failures);
    return n>0 && (size_t)n<cap?n:0;
}
