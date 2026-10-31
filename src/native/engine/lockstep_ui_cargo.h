/* Determinism fix, DEFAULT OFF: the UI's cargo and production queries do not
 * advance the simulation (native_gate_ui_cargo = 'block' | 'observe').
 *
 * Container cargo and production are evaluated lazily: 0x41a910(container,
 * now, force) brings the container up to the given game time once (+0xa18
 * holds the last time; a second call at the same time returns at once): it
 * walks the production modules (0x761720: cycle completion and the next
 * cycle's timing, Random::FloatRange on TLS+0x318) and the cargo they move.
 * The station trade offers are computed from that state. Besides simulation
 * callers, ten UI exports call it on whatever the client's UI shows, at the
 * frame's game time, on the MoveWorker in the UI slot: GetCargo, GetNumCargo,
 * GetMissileCargo, GetNumMissileCargo, GetUnitCargo, GetNumUnitCargo,
 * AreWaresWithinContainerProductionLimits, Lua::GetStorageData (the target
 * monitor's storage row, every refresh of the current target),
 * Lua::GetProductionModuleData and PushVentureLogbookInfo. A container a
 * client looks at is therefore brought up to date at other times on each
 * client, with draws from the UI slot's word: its production cycles, cargo and
 * trade offers part (play16E-soak with native_gate_event_rng = 'key': the
 * play configuration still parts at 3600 with the trade seeds a1df9 and
 * acf94, the full configuration does not).
 *
 * The ten calls (E8 to 0x41a910, all with force 0 and an unused result) go
 * through stubs on a private page (mov r9d, site; jmp [rip]) to uic_call. In
 * an admitted frame on the MoveWorker, 'block' returns without the update (the
 * query then reads the state the simulation last brought the container to)
 * and 'observe' updates natively; both count the call per site and keep one
 * row (tick, site, container portable key) per call, so two clients' rows
 * name the containers one client evaluated alone. Loading frames and other
 * threads stay native. Pins: 0x41a910 to +0x70 (the once-per-time check and
 * the time store) and a 32-byte window around each site with the original
 * displacement. Written by CAS at arm (before the first admitted frame),
 * re-checked every gate tick (stop reason 153), restored at stop. */
#define UIC_SITES 10u
#define UIC_TARGET 0x41a910u
#define UIC_STOP_REASON 153u /* 150 gravidar wide, 151 event RNG, 152 build RNG */
#define UIC_STUB 0x20u
#define UIC_RING 16384u
enum {UIC_OFF=0,UIC_BLOCK=1,UIC_OBSERVE=2};
typedef void (*uic_update_fn)(void *,double,uint64_t);
static const struct {uint32_t rva;const char *sha;const char *name;} uic_sites[UIC_SITES]={
    {0x1de7f0u,"157c41973f527c8e5bc926d60583c4b52c244feae3f5ba0195ac4bab831ad75a","AreWaresWithinContainerProductionLimits"},
    {0x1e074du,"83c44abd79eda8ce744029e4e0c95db46122af15db17cd3393a2640a15791e87","GetCargo"},
    {0x1e515du,"c1982d05e0e5aec073faf57538c336929a59be61ae1d70d23007a37b3efe58bb","GetMissileCargo"},
    {0x1e5bb4u,"f10850ffa2a54e03326dcd8d6b2d9ae82ac266cde81e9797b40fc05481b86bde","GetNumCargo"},
    {0x1e6eb9u,"48d1137b3d3d77004d67b853ee636b8ae25de367c918b01e815f21b6bcd485a0","GetNumMissileCargo"},
    {0x1e906fu,"56b0684148f84cb595821b4373fa51a0b349f2c3dec1cbf3ad0481a8bcf3cf1e","GetNumUnitCargo"},
    {0x1ec135u,"d0bfd1c78b2616d5a7bc91290c8891c0ffa284faef1521d1210af73007f2e0ef","GetUnitCargo"},
    {0x28ae0eu,"5e78ae1c713c00528d9a0ea016690cdce138191a61ec554b3abf3d39dabf0a55","GetStorageData"},
    {0x28c974u,"6a395daa84148978b6a2463df72f9eecc149a7582729327afc5fc7b82659ead2","GetProductionModuleData"},
    {0x2e93a0u,"0895d454843b69ca564ea575cf5f6d9fc2f33730a5816c58fbe70c3d2fc8dbc2","PushVentureLogbookInfo"},
};
typedef struct {volatile LONG64 ready;uint64_t tick,key;uint32_t site;uint8_t klass,blocked;} uic_row_t;
static unsigned char *uic_page;
static int uic_installed,uic_restored,uic_mode;
static unsigned uic_done; /* bit per site: its call goes through our stub */
static uint64_t uic_calls[UIC_SITES],uic_blocked,uic_passed,uic_native,uic_unmapped,uic_keyset;
static volatile LONG64 uic_failures,uic_rows;
static uic_row_t *uic_ring;
static const char *uic_stage="idle";
static uic_update_fn uic_update=NULL; /* the native update (tests substitute) */
static int32_t uic_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+uic_sites[i].rva+5));}
static void uic_record(unsigned site,uint64_t key,unsigned klass,int blocked) {
    if(!uic_ring) return;
    LONG64 index=InterlockedIncrement64(&uic_rows)-1;uic_row_t *r=&uic_ring[(uint64_t)index%UIC_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->key=key;r->site=site;r->klass=(uint8_t)klass;r->blocked=(uint8_t)blocked;
    InterlockedExchange64(&r->ready,index);
}
/* The stubs' target: (container, now, force, site) in rcx, xmm1, r8, r9. */
static void uic_call(void *container,double now,uint64_t force,uint64_t site) {
    uic_update_fn update=uic_update?uic_update:(uic_update_fn)(void *)(image+UIC_TARGET);
    if(site>=UIC_SITES || !uic_mode || !(stats.active && boot_phase==3) || GetCurrentThreadId()!=ui_thread) {
        ++uic_native;update(container,now,force);return;
    }
    ++uic_calls[site];
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key) ++uic_unmapped;
    uic_keyset+=lockstep_mix(key^((uint64_t)site<<56)^lockstep_mix(stats.issued));
    int block=uic_mode==UIC_BLOCK;
    uic_record((unsigned)site,key,klass,block);
    if(block) {++uic_blocked;return;}
    ++uic_passed;update(container,now,force);
}
/* mov r9d, site; jmp [rip]; abs64 uic_call */
static void uic_write_stub(unsigned char *p,unsigned site) {
    static const unsigned char code[12]={0x41,0xb9,0,0,0,0,0xff,0x25,0,0,0,0};void *t=(void*)uic_call;
    memcpy(p,code,12);memcpy(p+2,&site,4);memcpy(p+12,&t,8);
}
static int uic_stub_ok(const unsigned char *p,unsigned site) {
    unsigned char want[20];uic_write_stub(want,site);return p && !memcmp(p,want,20);
}
/* 0 original, 1 ours, -1 anything else. */
static int uic_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+uic_sites[i].rva,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==uic_rel(i,image+UIC_TARGET)) return 0;
    if(uic_page && d==uic_rel(i,uic_page+UIC_STUB*i)) return 1;
    return -1;
}
static int uic_pins(void) {
    if(!hash_ok(image+UIC_TARGET,0x70,"5b94da94f107e9c097551381a91e1ff9868ace79480c4cbaf7958292f77f666a")) return 0;
    for(unsigned i=0;i<UIC_SITES;i++) {
        unsigned char copy[0x20];
        if(uic_state(i)<0 || !read_local(image+uic_sites[i].rva-0x10,copy,sizeof(copy))) return 0;
        int32_t d=uic_rel(i,image+UIC_TARGET);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),uic_sites[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *uic_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x200000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<UIC_SITES && ok;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+uic_sites[i].rva+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;
            }
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int uic_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(uic_sites[i].rva+1,e,v,4);
}
static void uic_restore(void) {
    if(!uic_installed) return;
    for(unsigned i=0;i<UIC_SITES;i++)
        if((uic_done>>i)&1u && uic_state(i)==1 && uic_cas(i,uic_rel(i,uic_page+UIC_STUB*i),uic_rel(i,image+UIC_TARGET))) uic_done&=~(1u<<i);
    if(!uic_done) {uic_installed=0;uic_restored=1;uic_mode=UIC_OFF;}
}
/* Before the first admitted frame, on the MoveWorker. */
static int uic_arm(int mode) {
    if(uic_installed) return stats.active && uic_mode==mode;
    if(mode!=UIC_BLOCK && mode!=UIC_OBSERVE) {uic_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {uic_stage="needs";return 0;}
    for(unsigned i=0;i<UIC_SITES;i++) if(uic_state(i)!=0) {uic_stage="pins";return 0;}
    if(!uic_pins()) {uic_stage="pins";return 0;}
    if(!uic_ring) uic_ring=VirtualAlloc(NULL,sizeof(uic_row_t)*UIC_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!uic_page) uic_page=uic_alloc_page();
    if(!uic_ring || !uic_page) {uic_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(uic_page,0x1000,PAGE_READWRITE,&old)) {uic_stage="page";return 0;}
    for(unsigned i=0;i<UIC_SITES;i++) uic_write_stub(uic_page+UIC_STUB*i,i);
    if(!VirtualProtect(uic_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {uic_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),uic_page,0x1000);
    memset(uic_calls,0,sizeof(uic_calls));uic_blocked=uic_passed=uic_native=uic_unmapped=uic_keyset=0;uic_failures=0;uic_rows=0;uic_restored=0;
    uic_mode=mode;uic_installed=1;
    for(unsigned i=0;i<UIC_SITES;i++) {
        if(!uic_cas(i,uic_rel(i,image+UIC_TARGET),uic_rel(i,uic_page+UIC_STUB*i))) {uic_stage="cas";uic_restore();uic_installed=0;uic_mode=UIC_OFF;return 0;}
        uic_done|=1u<<i;
    }
    uic_stage="armed";return 1;
}
static int uic_owned(void) {
    for(unsigned i=0;i<UIC_SITES;i++)
        if((uic_done>>i)&1u && (uic_state(i)!=1 || !uic_stub_ok(uic_page+UIC_STUB*i,i))) return 0;
    return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 153. */
static int uic_check(void) {
    if(InterlockedCompareExchange64(&uic_failures,0,0)) return 0;
    const char *why=!uic_owned()?"site_changed":!uic_pins()?"block_changed":NULL;
    if(why) {uic_stage=why;InterlockedIncrement64(&uic_failures);return 0;}
    return 1;
}
static int uic_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&uic_rows,0,0);int n;
    if(!uic_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)UIC_RING && index<(uint64_t)(count-(LONG64)UIC_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const uic_row_t *r=&uic_ring[index%UIC_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s class=%s key=%016llx blocked=%u",(unsigned long long)index,(unsigned long long)r->tick,
        uic_sites[r->site<UIC_SITES?r->site:0].name,classes[r->klass<3?r->klass:2],(unsigned long long)r->key,(unsigned)r->blocked);
    return n>0 && (size_t)n<cap?n:0;
}
static int uic_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","block","observe"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu blocked=%llu passed=%llu native=%llu unmapped=%llu rows=%lld keyset=%016llx calls=",
        uic_installed,modes[uic_mode>=0 && uic_mode<3?uic_mode:0],(unsigned long long)stats.issued,(unsigned long long)uic_blocked,
        (unsigned long long)uic_passed,(unsigned long long)uic_native,(unsigned long long)uic_unmapped,(long long)uic_rows,(unsigned long long)uic_keyset);
    for(unsigned i=0;n>0 && (size_t)n<cap && i<UIC_SITES;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%llu",i?"/":"",(unsigned long long)uic_calls[i]);
    if(n>0 && (size_t)n<cap) n+=snprintf(out+n,cap-(size_t)n," restored=%d stage=%s failures=%lld",uic_restored,uic_stage,(long long)uic_failures);
    return n>0 && (size_t)n<cap?n:0;
}
