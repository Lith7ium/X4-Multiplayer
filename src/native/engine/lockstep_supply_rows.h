/* Diagnostic, DEFAULT OFF: who adds or removes a station's supply wares
 * (native_gate_supply_rows = true, needs native_gate_world_identities).
 *
 * full-Eo and full-G8 (overlay-FullG3 stacks): client 1 alone delivers
 * U::SupplyWaresAddedEvent (vtable 0x2ba72f0) and U::SupplyWaresRemovedEvent
 * (0x2ba7398) on stations, the first at 1078, 1152, 1596, 1637, 1926, 2316,
 * 2353 (full-G3's frame word ticks), each followed by about three more
 * portable deliveries; full-Eo2 had the same 62 deliveries on both clients.
 * The events are posted at due = the thread's game time by four functions
 * (U::Container slot 1101 0x42c720, Container::UpdateTradeOffers 0x4079e0,
 * Container slot 85 0x40e810 and the script actions ClampTradeAmount /
 * AddOrRemoveSupplies reach them):
 *   added    0x428970 (post 0x428a41), 0x428a90 (post 0x428c2a)
 *   removed  0x428c80 (post 0x428e1c), 0x428e50 (post 0x428fda)
 * The four PostEvent 0x99a3f0 calls go through stubs on a private page (jmp
 * [rip] to one entry per site) that record, for each post in an admitted
 * frame: tick, site, station portable key, due time bits, thread (ui = the
 * MoveWorker, main, other) and eight image return addresses above the post,
 * then post natively. Two clients' rows name the path that changes a
 * station's supplies on one client alone. Pins: the four functions with the
 * original displacements. Written by CAS at arm, re-checked every gate tick
 * (stop reason 154), restored at stop. */
#define SPR_SITES 4u
#define SPR_POST 0x99a3f0u /* Component::PostEvent(component, event, due, flag) */
#define SPR_STOP_REASON 154u /* 153 is the UI cargo module's */
#define SPR_RING 8192u
#define SPR_CHAIN 8u
#define SPR_STUB 0x20u
typedef void (*spr_post_fn)(void *,void *,double,uint64_t);
static const struct {uint32_t func,size,site;const char *sha,*name;} spr_sites[SPR_SITES]={
    {0x428970u,0x114u,0x428a41u,"79803a51effc07baf8383b6c2a8cae6d25a38d11cb6bb11b6c962a44775392b6","added"},
    {0x428a90u,0x1ecu,0x428c2au,"3f01810558cac1c2bc644ef0ffc452c5a4898f06bd9b62e0a70b85fc8355f7dd","added2"},
    {0x428c80u,0x1c2u,0x428e1cu,"f69d905426da1f0455c02eb483900798d9e671dcec9247ca2ec44c23a72d9846","removed"},
    {0x428e50u,0x1dcu,0x428fdau,"ebd1d5220bbe66992748b57a046aa56eec27dd223d02faa6989f30ace3a546da","removed2"},
};
typedef struct {volatile LONG64 ready;uint64_t tick,key,due;uint32_t chain[SPR_CHAIN];uint8_t klass,thread,site;} spr_row_t;
static int spr_installed,spr_restored;
static unsigned spr_done;
static unsigned char *spr_page;
static spr_row_t *spr_ring;
static volatile LONG64 spr_rows,spr_failures;
static uint64_t spr_posts,spr_outside;
static const char *spr_stage="idle";
static spr_post_fn spr_native=NULL; /* tests substitute */
static int32_t spr_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+spr_sites[i].site+5));}
static uint32_t spr_rva(const void *p) {
    const unsigned char *q=p;return q>=image && q<image+0x6fa9000?(uint32_t)(q-image):0xffffffffu;
}
static void spr_chain(uint32_t *chain) {
    for(unsigned k=0;k<SPR_CHAIN;k++) chain[k]=0xffffffffu;
    void *frames[24];USHORT n=RtlCaptureStackBackTrace(0,24,frames,NULL);unsigned k=0;
    for(USHORT j=0;j<n && k<SPR_CHAIN;j++) {uint32_t rva=spr_rva(frames[j]);if(rva!=0xffffffffu) chain[k++]=rva;}
}
static __attribute__((noinline)) void spr_post_at(unsigned site,void *station,void *event,double due,uint64_t flag) {
    spr_post_fn post=spr_native?spr_native:(spr_post_fn)(void *)(image+SPR_POST);
    if(spr_installed && spr_ring && stats.active && boot_phase==3) {
        LONG64 index=InterlockedIncrement64(&spr_rows)-1;spr_row_t *r=&spr_ring[(uint64_t)index%SPR_RING];
        InterlockedExchange64(&r->ready,-1);
        uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
        if(station && read_local((unsigned char*)station+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
        DWORD thread=GetCurrentThreadId();
        r->tick=stats.issued;r->key=key;memcpy(&r->due,&due,8);r->klass=(uint8_t)klass;r->site=(uint8_t)site;
        r->thread=(uint8_t)(thread==ui_thread?0:thread==main_thread?1:2);spr_chain(r->chain);
        InterlockedExchange64(&r->ready,index);++spr_posts;
    } else ++spr_outside;
    post(station,event,due,flag);
}
static void spr_post0(void *s,void *e,double d,uint64_t f) {spr_post_at(0,s,e,d,f);}
static void spr_post1(void *s,void *e,double d,uint64_t f) {spr_post_at(1,s,e,d,f);}
static void spr_post2(void *s,void *e,double d,uint64_t f) {spr_post_at(2,s,e,d,f);}
static void spr_post3(void *s,void *e,double d,uint64_t f) {spr_post_at(3,s,e,d,f);}
static const spr_post_fn spr_entries[SPR_SITES]={spr_post0,spr_post1,spr_post2,spr_post3};
/* jmp [rip]; abs64 entry */
static void spr_write_stub(unsigned char *p,unsigned site) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};void *t=(void*)spr_entries[site];memcpy(p,code,6);memcpy(p+6,&t,8);
}
static int spr_stub_ok(const unsigned char *p,unsigned site) {unsigned char want[14];spr_write_stub(want,site);return p && !memcmp(p,want,14);}
static int spr_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+spr_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==spr_rel(i,image+SPR_POST)) return 0;
    if(spr_page && d==spr_rel(i,spr_page+SPR_STUB*i)) return 1;
    return -1;
}
static int spr_pins(void) {
    static unsigned char copy[0x200];
    for(unsigned i=0;i<SPR_SITES;i++) {
        if(spr_state(i)<0 || spr_sites[i].size>sizeof(copy) || !read_local(image+spr_sites[i].func,copy,spr_sites[i].size)) return 0;
        int32_t d=spr_rel(i,image+SPR_POST);memcpy(copy+(spr_sites[i].site-spr_sites[i].func)+1,&d,4);
        if(!hash_ok(copy,spr_sites[i].size,spr_sites[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *spr_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+spr_sites[0].site)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+spr_sites[0].site+5);
            if(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int spr_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(spr_sites[i].site+1,e,v,4);
}
static void spr_restore(void) {
    if(!spr_installed) return;
    for(unsigned i=0;i<SPR_SITES;i++)
        if((spr_done>>i)&1u && spr_state(i)==1 && spr_cas(i,spr_rel(i,spr_page+SPR_STUB*i),spr_rel(i,image+SPR_POST))) spr_done&=~(1u<<i);
    if(!spr_done) {spr_installed=0;spr_restored=1;}
}
static int spr_arm(void) {
    if(spr_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {spr_stage="needs";return 0;}
    for(unsigned i=0;i<SPR_SITES;i++) if(spr_state(i)!=0) {spr_stage="pins";return 0;}
    if(!spr_pins()) {spr_stage="pins";return 0;}
    if(!spr_ring) spr_ring=VirtualAlloc(NULL,sizeof(spr_row_t)*SPR_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!spr_page) spr_page=spr_alloc_page();
    if(!spr_ring || !spr_page) {spr_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(spr_page,0x1000,PAGE_READWRITE,&old)) {spr_stage="page";return 0;}
    for(unsigned i=0;i<SPR_SITES;i++) spr_write_stub(spr_page+SPR_STUB*i,i);
    if(!VirtualProtect(spr_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {spr_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),spr_page,0x1000);
    spr_rows=0;spr_posts=spr_outside=0;spr_failures=0;spr_restored=0;spr_installed=1;
    for(unsigned i=0;i<SPR_SITES;i++) {
        if(!spr_cas(i,spr_rel(i,image+SPR_POST),spr_rel(i,spr_page+SPR_STUB*i))) {spr_stage="cas";spr_restore();spr_installed=0;return 0;}
        spr_done|=1u<<i;
    }
    spr_stage="armed";return 1;
}
static int spr_owned(void) {
    for(unsigned i=0;i<SPR_SITES;i++) if((spr_done>>i)&1u && (spr_state(i)!=1 || !spr_stub_ok(spr_page+SPR_STUB*i,i))) return 0;
    return 1;
}
static int spr_check(void) {
    if(InterlockedCompareExchange64(&spr_failures,0,0)) return 0;
    const char *why=!spr_owned()?"site_changed":!spr_pins()?"block_changed":NULL;
    if(why) {spr_stage=why;InterlockedIncrement64(&spr_failures);return 0;}
    return 1;
}
static int spr_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&spr_rows,0,0);int n;
    if(!spr_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)SPR_RING && index<(uint64_t)(count-(LONG64)SPR_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const spr_row_t *r=&spr_ring[index%SPR_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"},*const threads[3]={"ui","main","other"};
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s class=%s key=%016llx due=%016llx thread=%s chain=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
        (unsigned long long)index,(unsigned long long)r->tick,spr_sites[r->site<SPR_SITES?r->site:0].name,classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,(unsigned long long)r->due,threads[r->thread<3?r->thread:2],
        r->chain[0],r->chain[1],r->chain[2],r->chain[3],r->chain[4],r->chain[5],r->chain[6],r->chain[7]);
    return n>0 && (size_t)n<cap?n:0;
}
static int spr_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d frame=%llu posts=%llu outside=%llu rows=%lld restored=%d stage=%s failures=%lld",spr_installed,
        (unsigned long long)stats.issued,(unsigned long long)spr_posts,(unsigned long long)spr_outside,(long long)spr_rows,spr_restored,spr_stage,
        (long long)spr_failures);
    return n>0 && (size_t)n<cap?n:0;
}
