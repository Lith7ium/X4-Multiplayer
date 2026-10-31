/* Determinism fix, DEFAULT OFF: Lua UI queries do not bring containers up to
 * date (native_gate_ui_lazy = 'observe' | 'block', needs the world identities).
 *
 * The 3600 transporter class now starts with one keyed due event at tick 1078:
 * station 340cf's SupplyWaresAddedEvent listener (0x42b900, wanted supply
 * orders 0x429ad0, one Random::U64 draw) and the supply changes of stations
 * 1f2df and 2bb56 that a script's trade offer query brings up at 1077
 * (DoIf -> PropertyLookup -> 0x40a3f0 -> Container::UpdateTradeOffers 0x4079e0
 * -> supply-added post 0x428970) happen on both clients, on neither or on one
 * (full-G9 both, full-G10s neither, full-G8/G10 one): the stations' cargo,
 * unit and trade offer state already differs before 1077, with the same
 * scripts. That state is evaluated lazily, at whatever game time something
 * asks: cargo and production by 0x41a910 (container, time, force), units in
 * flight by Defensible slot 1105 0x4d8110 (applies arrived units and posts
 * their supply changes), trade offers by 0x4079e0. Besides the simulation, the
 * Lua UI asks through GetComponentData property lookups and the Lua storage
 * functions (GetStorageData, GetUnitStorageData from the target monitor, the
 * map, the station overview) on what each client shows, with refreshes paced
 * by real time; native_gate_ui_cargo covers only ten explicit exports and saw
 * none of them.
 *
 * The three functions get an entry detour (E9 to a stub on a private page;
 * the overwritten whole instructions, 7, 7 and 5 bytes, run in a trampoline
 * that jumps back). In an admitted frame on the MoveWorker, a call with the
 * Lua runtime (lua51_64.dll) on the stack is a UI query: 'observe' counts it
 * per function, keeps one UI_LAZY_ROW (tick, function, container portable key,
 * six image return addresses) and runs natively; 'block' also returns without
 * the update (units: the container's cached unit total +0x680; the UI shows
 * the state the simulation last brought it to). Every other call runs
 * natively. With a row window (native_gate_ui_lazy_rows = {from, to}, at
 * most 600 ticks) every call of an admitted frame in it is a row, simulation
 * calls too (lua=0), with the arguments and the state that decide the work:
 * cargo a1 = game time bits, a2 = force, state = +0xa18 (the time it was last
 * brought to); trade a1 = force, a2 = third argument, state = +0x928 (next
 * update time); units a1 = the filter's class vtable RVA (0: none, ffffffff:
 * not in the image), a2 = flag, state = +0x680 cached total | +0x684 << 32.
 * Calls of any other thread run natively: counted per function (other= in the
 * status) and, in the window, a row with other=1 (the gate's frames do not bound
 * these rows: main-thread calls between two admitted frames are rows too, with
 * the last issued tick).
 * Pins: each function's first bytes. Written by CAS at arm (before
 * the snapshot load), re-checked every gate tick (stop reason 170), restored at
 * stop. */
#define UL_TARGETS 3u
#define UL_STOP_REASON 170u /* 155 stock piloting, 160-164 virtual players, 165 Jolt, 166-169 camera */
#define UL_RING 16384u
#define UL_CHAIN 6u
#define UL_STUB 0x40u
enum {UL_OFF=0,UL_OBSERVE=1,UL_BLOCK=2};
enum {UL_CARGO=0,UL_TRADE=1,UL_UNITS=2};
static const struct {uint32_t rva,stolen,pin_size;const char *sha,*name;} ul_targets[UL_TARGETS]={
    {0x41a910u,7u,0x70u,"5b94da94f107e9c097551381a91e1ff9868ace79480c4cbaf7958292f77f666a","cargo"},
    {0x4079e0u,7u,0x60u,"ed89bcfb65a436f4e90ab33fea2493352ab953f8c6df8ec3d3fa6c07815b45e7","trade"},
    {0x4d8110u,5u,0x40u,"43aa700d29d4015a81690be9b8847b50f6db872c42dd133e7029fc69430027b4","units"},
};
typedef void (*ul_cargo_fn)(void *,double,uint64_t);
typedef uint64_t (*ul_trade_fn)(void *,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*ul_units_fn)(void *,void *,uint64_t);
typedef struct {volatile LONG64 ready;uint64_t tick,key,a1,a2,state;uint32_t chain[UL_CHAIN];uint8_t target,klass,blocked,lua,other;} ul_row_t;
static uint64_t ul_window_from,ul_window_to; /* every call of these ticks is a row; 0/0: UI queries only */
static int ul_mode,ul_installed,ul_restored;
static unsigned ul_done;
static unsigned char *ul_page; /* per target: trampoline at +0, entry stub at +0x20 */
static ul_row_t *ul_ring;
static volatile LONG64 ul_rows,ul_failures,ul_other[UL_TARGETS];
static uint64_t ul_lua[UL_TARGETS],ul_sim[UL_TARGETS],ul_blocked,ul_unmapped;
static uintptr_t ul_lua_base,ul_lua_end;
static const char *ul_stage="idle";
static int (*ul_from_lua_fn)(uint32_t *chain); /* tests substitute */
static uint32_t ul_rva(const void *p) {const unsigned char *q=p;return q>=image && q<image+0x6fa9000?(uint32_t)(q-image):0xffffffffu;}
/* The Lua runtime somewhere above us; the image frames go to chain. */
static int ul_stack_from_lua(uint32_t *chain) {
    void *frames[48];USHORT n=RtlCaptureStackBackTrace(1,48,frames,NULL);int lua=0;unsigned k=0;
    for(unsigned i=0;i<UL_CHAIN;i++) chain[i]=0xffffffffu;
    for(USHORT j=0;j<n;j++) {
        uintptr_t a=(uintptr_t)frames[j];
        if(a>=ul_lua_base && a<ul_lua_end) {lua=1;break;}
        uint32_t rva=ul_rva(frames[j]);if(rva!=0xffffffffu && k<UL_CHAIN) chain[k++]=rva;
    }
    return lua;
}
static unsigned char *ul_trampoline(unsigned i) {return ul_page+UL_STUB*i;}
static void ul_record(unsigned target,const void *container,const uint32_t *chain,int blocked,int lua,uint64_t a1,uint64_t a2,uint64_t state) {
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key && lua) ++ul_unmapped;
    if(!ul_ring) return;
    LONG64 index=InterlockedIncrement64(&ul_rows)-1;ul_row_t *r=&ul_ring[(uint64_t)index%UL_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->key=key;memcpy(r->chain,chain,sizeof(r->chain));r->target=(uint8_t)target;r->klass=(uint8_t)klass;r->blocked=(uint8_t)blocked;
    r->lua=(uint8_t)lua;r->a1=a1;r->a2=a2;r->state=state;r->other=GetCurrentThreadId()!=ui_thread;
    InterlockedExchange64(&r->ready,index);
}
/* What decides the call's work (see the header). */
static uint64_t ul_state_of(unsigned target,const void *container) {
    const unsigned char *c=container;uint64_t v=0;uint32_t total=0;uint8_t pending=0;
    if(!c) return 0;
    if(target==UL_CARGO) read_local(c+0xa18,&v,8);
    else if(target==UL_TRADE) read_local(c+0x928,&v,8);
    else {read_local(c+0x680,&total,4);read_local(c+0x684,&pending,1);v=total|((uint64_t)pending<<32);}
    return v;
}
/* 1: a UI query in an admitted frame that 'block' skips; 0: run natively. */
static int ul_ui_query(unsigned target,const void *container,uint64_t a1,uint64_t a2) {
    if(!ul_mode || !(stats.active && boot_phase==3)) return 0;
    int window=ul_window_to && stats.issued>=ul_window_from && stats.issued<=ul_window_to;
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&ul_other[target]);
        if(window) {uint32_t off[UL_CHAIN];(ul_from_lua_fn?ul_from_lua_fn:ul_stack_from_lua)(off);ul_record(target,container,off,0,0,a1,a2,ul_state_of(target,container));}
        return 0;
    }
    uint32_t chain[UL_CHAIN];int lua=(ul_from_lua_fn?ul_from_lua_fn:ul_stack_from_lua)(chain);
    if(!lua) {
        ++ul_sim[target];
        if(window) ul_record(target,container,chain,0,0,a1,a2,ul_state_of(target,container));
        return 0;
    }
    ++ul_lua[target];int block=ul_mode==UL_BLOCK;if(block) ++ul_blocked;
    ul_record(target,container,chain,block,1,a1,a2,ul_state_of(target,container));
    return block;
}
/* The row window; a configuration rerun with the same window is accepted. Before arming. */
static int ul_window(uint64_t from,uint64_t to) {
    if(ul_installed) return from==ul_window_from && to==ul_window_to;
    if(to && (from<1 || to<from || to-from>600)) return 0;
    ul_window_from=from;ul_window_to=to;return 1;
}
static uint64_t ul_filter_class(const void *filter) {
    void *vt=NULL;if(!filter) return 0;
    if(!read_local(filter,&vt,8)) return 0xffffffffu;
    return ul_rva(vt);
}
static void ul_cargo(void *container,double now,uint64_t force) {
    uint64_t bits;memcpy(&bits,&now,8);
    if(ul_ui_query(UL_CARGO,container,bits,force&0xff)) return;
    ((ul_cargo_fn)(void *)ul_trampoline(UL_CARGO))(container,now,force);
}
static uint64_t ul_trade(void *container,uint64_t force,uint64_t a3,uint64_t a4) {
    if(ul_ui_query(UL_TRADE,container,force&0xff,a3&0xff)) return 0;
    return ((ul_trade_fn)(void *)ul_trampoline(UL_TRADE))(container,force,a3,a4);
}
static uint64_t ul_units(void *container,void *filter,uint64_t flag) {
    if(ul_ui_query(UL_UNITS,container,ul_filter_class(filter),flag&0xff)) {uint32_t total=0;read_local((unsigned char*)container+0x680,&total,4);return total;}
    return ((ul_units_fn)(void *)ul_trampoline(UL_UNITS))(container,filter,flag);
}
static void *const ul_entries[UL_TARGETS]={(void*)ul_cargo,(void*)ul_trade,(void*)ul_units};
/* Trampoline: the stolen bytes, then jmp [rip] back; entry stub (+0x20): jmp [rip] to the C entry. */
static void ul_write_code(unsigned char *p,unsigned i,const unsigned char *stolen) {
    static const unsigned char jmp[6]={0xff,0x25,0,0,0,0};
    memset(p,0xcc,UL_STUB);memcpy(p,stolen,ul_targets[i].stolen);
    void *back=image+ul_targets[i].rva+ul_targets[i].stolen;memcpy(p+ul_targets[i].stolen,jmp,6);memcpy(p+ul_targets[i].stolen+6,&back,8);
    memcpy(p+0x20,jmp,6);memcpy(p+0x26,&ul_entries[i],8);
}
static int32_t ul_rel(unsigned i) {return (int32_t)((intptr_t)(ul_page+UL_STUB*i+0x20)-(intptr_t)(image+ul_targets[i].rva+5));}
/* 0 original (pins), 1 ours, -1 anything else. */
static int ul_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+ul_targets[i].rva,now,5)) return -1;
    if(now[0]==0xe9 && ul_page) {int32_t d;memcpy(&d,now+1,4);return d==ul_rel(i)?1:-1;}
    return hash_ok(image+ul_targets[i].rva,ul_targets[i].pin_size,ul_targets[i].sha)?0:-1;
}
static int ul_code_ok(unsigned i) {
    unsigned char want[UL_STUB],stolen[8];memcpy(stolen,ul_page+UL_STUB*i,ul_targets[i].stolen);
    ul_write_code(want,i,stolen);return !memcmp(want,ul_page+UL_STUB*i,UL_STUB);
}
static unsigned char *ul_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x420000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<UL_TARGETS && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+ul_targets[i].rva+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void ul_restore(void) {
    if(!ul_installed) return;
    for(unsigned i=0;i<UL_TARGETS;i++) {
        if(!((ul_done>>i)&1u) || ul_state(i)!=1) continue;
        unsigned char now[5],orig[5];read_local(image+ul_targets[i].rva,now,5);memcpy(orig,ul_page+UL_STUB*i,5);
        if(view_cas_bytes(ul_targets[i].rva,now,orig,5)) ul_done&=~(1u<<i);
    }
    if(!ul_done) {ul_installed=0;ul_restored=1;ul_mode=UL_OFF;}
}
static int ul_lua_module(void) {
    HMODULE lua=GetModuleHandleW(L"lua51_64.dll");if(!lua) return 0;
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    if(!read_local(lua,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || !read_local((unsigned char*)lua+dos.e_lfanew,&nt,sizeof(nt))) return 0;
    ul_lua_base=(uintptr_t)lua;ul_lua_end=ul_lua_base+nt.OptionalHeader.SizeOfImage;return 1;
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker. */
static int ul_arm(int mode) {
    if(ul_installed) return stats.active && ul_mode==mode;
    if(mode!=UL_OBSERVE && mode!=UL_BLOCK) {ul_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {ul_stage="needs";return 0;}
    for(unsigned i=0;i<UL_TARGETS;i++) if(ul_state(i)!=0) {ul_stage="pins";return 0;}
    if(!ul_from_lua_fn && !ul_lua_module()) {ul_stage="lua";return 0;}
    if(!ul_ring) ul_ring=VirtualAlloc(NULL,sizeof(ul_row_t)*UL_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ul_page) ul_page=ul_alloc_page();
    if(!ul_ring || !ul_page) {ul_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(ul_page,0x1000,PAGE_READWRITE,&old)) {ul_stage="page";return 0;}
    for(unsigned i=0;i<UL_TARGETS;i++) ul_write_code(ul_page+UL_STUB*i,i,image+ul_targets[i].rva);
    if(!VirtualProtect(ul_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {ul_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ul_page,0x1000);
    memset(ul_lua,0,sizeof(ul_lua));memset(ul_sim,0,sizeof(ul_sim));for(unsigned i=0;i<UL_TARGETS;i++) ul_other[i]=0;ul_blocked=ul_unmapped=0;ul_rows=0;ul_failures=0;ul_restored=0;
    ul_mode=mode;ul_installed=1;
    for(unsigned i=0;i<UL_TARGETS;i++) {
        unsigned char orig[5],patch[5]={0xe9};int32_t d=ul_rel(i);memcpy(orig,image+ul_targets[i].rva,5);memcpy(patch+1,&d,4);
        if(!view_cas_bytes(ul_targets[i].rva,orig,patch,5)) {ul_stage="cas";ul_restore();ul_installed=0;ul_mode=UL_OFF;return 0;}
        ul_done|=1u<<i;
    }
    ul_stage="armed";return 1;
}
static int ul_owned(void) {
    for(unsigned i=0;i<UL_TARGETS;i++) if((ul_done>>i)&1u && (ul_state(i)!=1 || !ul_code_ok(i))) return 0;
    return 1;
}
static int ul_check(void) {
    if(InterlockedCompareExchange64(&ul_failures,0,0)) return 0;
    if(!ul_owned()) {ul_stage="site_changed";InterlockedIncrement64(&ul_failures);return 0;}
    return 1;
}
static int ul_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ul_rows,0,0);int n;
    if(!ul_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)UL_RING && index<(uint64_t)(count-(LONG64)UL_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ul_row_t *r=&ul_ring[index%UL_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu target=%s class=%s key=%016llx lua=%u blocked=%u a1=%llx a2=%llx state=%016llx chain=%08x,%08x,%08x,%08x,%08x,%08x other=%u",
        (unsigned long long)index,(unsigned long long)r->tick,ul_targets[r->target<UL_TARGETS?r->target:0].name,classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,(unsigned)r->lua,(unsigned)r->blocked,(unsigned long long)r->a1,(unsigned long long)r->a2,(unsigned long long)r->state,
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],r->chain[4],r->chain[5],(unsigned)r->other);
    return n>0 && (size_t)n<cap?n:0;
}
static int ul_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","observe","block"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu lua=%llu/%llu/%llu sim=%llu/%llu/%llu other=%lld/%lld/%lld blocked=%llu unmapped=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld",
        ul_installed,modes[ul_mode>=0 && ul_mode<3?ul_mode:0],(unsigned long long)stats.issued,(unsigned long long)ul_lua[0],(unsigned long long)ul_lua[1],
        (unsigned long long)ul_lua[2],(unsigned long long)ul_sim[0],(unsigned long long)ul_sim[1],(unsigned long long)ul_sim[2],(long long)ul_other[0],(long long)ul_other[1],(long long)ul_other[2],(unsigned long long)ul_blocked,
        (unsigned long long)ul_unmapped,(long long)ul_rows,(unsigned long long)ul_window_from,(unsigned long long)ul_window_to,ul_restored,ul_stage,(long long)ul_failures);
    return n>0 && (size_t)n<cap?n:0;
}
