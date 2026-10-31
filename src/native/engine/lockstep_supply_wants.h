/* Diagnostic, DEFAULT OFF: what a station's wanted supply orders are, call by
 * call (native_gate_supply_wants = {from, to}: ticks, at most 600; needs the
 * world identities).
 *
 * full-R187b/c/d: UpdateTradeOffers(3bccb) is entered identically on both
 * clients at tick 187 (same trade rows), but only one client then brings
 * 3bccb's cargo up to date from 0x407ebe / 0x407f24 and posts its supply-added
 * event. Both are inside the branch at 0x407df0 (cmp [rbp+0x28], 0; je
 * 0x408100): the list 0x42ae40 fills from U::Container::GetWantedSupplyOrders
 * 0x429ad0 (the units, deployables and ammunition the station wants to restock,
 * converted to their resource wares) is empty on the other client. Its inputs
 * are the station's unit and deployable stock (Defensible 0x4d8a20 / 0x4db410
 * / 0x4e0840 / 0x4e0ff0) against its loadout (VerifyLoadout 0x503c40,
 * GenerateDeployableLoadout 0x5046b0 / 0x505730).
 *
 * The four calls of 0x429ad0 (GetNumSupplyOrderResources 0x1e836f, 0x42ae87 in
 * 0x42ae40, the supply listener 0x42b900 at 0x42b98a and 0x42bd9a) go through
 * stubs (jmp [rip] to one entry per site) that run it natively and then, in
 * an admitted frame of the window, keep a SUPPLY_WANTS_ROW: tick, site,
 * container portable key, the third and fourth arguments, the number of
 * wanted orders and up to three of them (ware id, amount). The list stores
 * each entry obfuscated: ware = qword ^ entry address, amount = (low dword &
 * 0x7fffffff) ^ dword +8, signed: target minus stock, so a surplus is negative
 * (0x42af14 skips it with jle, 0x407fec negates it); other=1 marks a call off the
 * MoveWorker; the ware id is the first std::string of the ware
 * object that reads as an identifier ("?" when none). Two clients' rows name
 * the ware one station wants on one client alone. Pins: the 32 bytes around
 * each call with the original displacement. Written by CAS at arm, re-checked
 * every gate tick (stop reason 171), restored at stop. */
#define SW_SITES 4u
#define SW_TARGET 0x429ad0u /* U::Container::GetWantedSupplyOrders(container, list, a3, a4) */
#define SW_STOP_REASON 171u /* 170 is the UI lazy module's */
#define SW_RING 8192u
#define SW_SHOWN 3u
#define SW_STUB 0x20u
typedef void *(*sw_fn)(void *,void *,uint64_t,uint64_t);
static const struct {uint32_t site;const char *sha,*name;} sw_sites[SW_SITES]={
    {0x1e836fu,"95957f0fbcd1f620d94d0af57296d89a3b13a8d95bf0599088df93aa0e089b15","ui"},
    {0x42ae87u,"2321311f49d10860431b17c8f67471eee8d8a88882480984c92b72695ca9085c","trade"},
    {0x42b98au,"8a024b8182479b006466a38546a63cc634b8b575b188a910b73616efbc09542c","listener"},
    {0x42bd9au,"1fc63596ec1f371e16e6a7dcaffd144d97647ae019ca1c3cd438c47814125732","listener2"},
};
typedef struct {volatile LONG64 ready;uint64_t tick,key,a3,a4;uint32_t count;int32_t amount[SW_SHOWN];char ware[SW_SHOWN][24];uint8_t site,klass,other;} sw_row_t;
static int sw_installed,sw_restored;
static unsigned sw_done;
static unsigned char *sw_page;
static sw_row_t *sw_ring;
static volatile LONG64 sw_rows,sw_failures;
static uint64_t sw_from,sw_to,sw_calls;
static const char *sw_stage="idle";
static sw_fn sw_native=NULL; /* tests substitute */
static int32_t sw_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+sw_sites[i].site+5));}
/* The first MSVC std::string in the ware object (+0 .. +0x100) that reads as an identifier. */
static void sw_ware_id_cap(const void *ware,char *out,size_t room) {
    strcpy(out,"?");if(!ware) return;
    unsigned char obj[0x100];if(!read_local(ware,obj,sizeof(obj))) return;
    for(unsigned at=0;at+0x20<=sizeof(obj);at+=8) {
        uint64_t size,cap;memcpy(&size,obj+at+0x10,8);memcpy(&cap,obj+at+0x18,8);
        if(size<2 || size>63 || cap<size || cap>4096) continue;
        char text[64];const void *src=cap<16?(const void*)(obj+at):NULL;
        if(!src) {uint64_t p;memcpy(&p,obj+at,8);src=(const void*)(uintptr_t)p;}
        if(cap<16) memcpy(text,src,(size_t)size);else if(!read_local(src,text,(size_t)size)) continue;
        int ok=1;for(uint64_t k=0;k<size && ok;k++) ok=(text[k]>='a' && text[k]<='z') || (text[k]>='0' && text[k]<='9') || text[k]=='_';
        if(!ok) continue;
        size_t n=size<room-1?(size_t)size:room-1;memcpy(out,text,n);out[n]=0;return;
    }
}
static void sw_ware_id(const void *ware,char *out) {sw_ware_id_cap(ware,out,24);}
static void sw_note(unsigned site,const void *container,const unsigned char *list,uint64_t a3,uint64_t a4) {
    if(!sw_ring || !sw_to || !(stats.active && boot_phase==3) || stats.issued<sw_from || stats.issued>sw_to || !list) return;
    uint64_t runtime=0,key=0,capacity=0,count=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!read_local(list+0x20,&capacity,8) || !read_local(list+0x28,&count,8)) return;
    const unsigned char *data=list;if(capacity!=2) {uint64_t p=0;read_local(list,&p,8);data=(const unsigned char*)(uintptr_t)p;}
    LONG64 index=InterlockedIncrement64(&sw_rows)-1;sw_row_t *r=&sw_ring[(uint64_t)index%SW_RING];
    InterlockedExchange64(&r->ready,-1);
    memset(r->amount,0,sizeof(r->amount));memset(r->ware,0,sizeof(r->ware));
    r->tick=stats.issued;r->key=key;r->a3=a3;r->a4=a4;r->count=(uint32_t)count;r->site=(uint8_t)site;r->klass=(uint8_t)klass;r->other=GetCurrentThreadId()!=ui_thread;
    for(unsigned k=0;k<SW_SHOWN && k<count && data;k++) {
        const unsigned char *entry=data+16u*k;uint64_t q=0;uint32_t d=0;
        if(!read_local(entry,&q,8) || !read_local(entry+8,&d,4)) break;
        r->amount[k]=(int32_t)(((uint32_t)q&0x7fffffffu)^d);sw_ware_id((const void*)(uintptr_t)(q^(uint64_t)(uintptr_t)entry),r->ware[k]);
    }
    InterlockedExchange64(&r->ready,index);
}
static __attribute__((noinline)) void *sw_call(unsigned site,void *container,void *list,uint64_t a3,uint64_t a4) {
    sw_fn f=sw_native?sw_native:(sw_fn)(void *)(image+SW_TARGET);
    void *result=f(container,list,a3,a4);++sw_calls;
    sw_note(site,container,list,a3&0xff,a4&0xff);
    return result;
}
static void *sw_entry0(void *c,void *l,uint64_t a,uint64_t b) {return sw_call(0,c,l,a,b);}
static void *sw_entry1(void *c,void *l,uint64_t a,uint64_t b) {return sw_call(1,c,l,a,b);}
static void *sw_entry2(void *c,void *l,uint64_t a,uint64_t b) {return sw_call(2,c,l,a,b);}
static void *sw_entry3(void *c,void *l,uint64_t a,uint64_t b) {return sw_call(3,c,l,a,b);}
static const sw_fn sw_entries[SW_SITES]={sw_entry0,sw_entry1,sw_entry2,sw_entry3};
static void sw_write_stub(unsigned char *p,unsigned site) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};void *t=(void*)sw_entries[site];memcpy(p,code,6);memcpy(p+6,&t,8);
}
static int sw_stub_ok(const unsigned char *p,unsigned site) {unsigned char want[14];sw_write_stub(want,site);return p && !memcmp(p,want,14);}
static int sw_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+sw_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==sw_rel(i,image+SW_TARGET)) return 0;
    if(sw_page && d==sw_rel(i,sw_page+SW_STUB*i)) return 1;
    return -1;
}
static int sw_pins(void) {
    for(unsigned i=0;i<SW_SITES;i++) {
        unsigned char copy[0x20];
        if(sw_state(i)<0 || !read_local(image+sw_sites[i].site-0x10,copy,sizeof(copy))) return 0;
        int32_t d=sw_rel(i,image+SW_TARGET);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),sw_sites[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *sw_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x300000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<SW_SITES && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+sw_sites[i].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int sw_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(sw_sites[i].site+1,e,v,4);
}
static void sw_restore(void) {
    if(!sw_installed) return;
    for(unsigned i=0;i<SW_SITES;i++)
        if((sw_done>>i)&1u && sw_state(i)==1 && sw_cas(i,sw_rel(i,sw_page+SW_STUB*i),sw_rel(i,image+SW_TARGET))) sw_done&=~(1u<<i);
    if(!sw_done) {sw_installed=0;sw_restored=1;}
}
/* Before the first admitted frame, on the MoveWorker; a rerun with the same window is accepted. */
static int sw_arm(uint64_t from,uint64_t to) {
    if(sw_installed) return stats.active && sw_from==from && sw_to==to;
    if(from<1 || to<from || to-from>600) {sw_stage="window";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {sw_stage="needs";return 0;}
    for(unsigned i=0;i<SW_SITES;i++) if(sw_state(i)!=0) {sw_stage="pins";return 0;}
    if(!sw_pins()) {sw_stage="pins";return 0;}
    if(!sw_ring) sw_ring=VirtualAlloc(NULL,sizeof(sw_row_t)*SW_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!sw_page) sw_page=sw_alloc_page();
    if(!sw_ring || !sw_page) {sw_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(sw_page,0x1000,PAGE_READWRITE,&old)) {sw_stage="page";return 0;}
    for(unsigned i=0;i<SW_SITES;i++) sw_write_stub(sw_page+SW_STUB*i,i);
    if(!VirtualProtect(sw_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {sw_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),sw_page,0x1000);
    sw_from=from;sw_to=to;sw_rows=0;sw_calls=0;sw_failures=0;sw_restored=0;sw_installed=1;
    for(unsigned i=0;i<SW_SITES;i++) {
        if(!sw_cas(i,sw_rel(i,image+SW_TARGET),sw_rel(i,sw_page+SW_STUB*i))) {sw_stage="cas";sw_restore();sw_installed=0;return 0;}
        sw_done|=1u<<i;
    }
    sw_stage="armed";return 1;
}
static int sw_owned(void) {
    for(unsigned i=0;i<SW_SITES;i++) if((sw_done>>i)&1u && (sw_state(i)!=1 || !sw_stub_ok(sw_page+SW_STUB*i,i))) return 0;
    return 1;
}
static int sw_check(void) {
    if(InterlockedCompareExchange64(&sw_failures,0,0)) return 0;
    const char *why=!sw_owned()?"site_changed":!sw_pins()?"block_changed":NULL;
    if(why) {sw_stage=why;InterlockedIncrement64(&sw_failures);return 0;}
    return 1;
}
static int sw_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&sw_rows,0,0);int n;
    if(!sw_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)SW_RING && index<(uint64_t)(count-(LONG64)SW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const sw_row_t *r=&sw_ring[index%SW_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu site=%s class=%s key=%016llx a3=%llx a4=%llx count=%u wants=%s:%d,%s:%d,%s:%d other=%u",
        (unsigned long long)index,(unsigned long long)r->tick,sw_sites[r->site<SW_SITES?r->site:0].name,classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,(unsigned long long)r->a3,(unsigned long long)r->a4,r->count,
        r->ware[0][0]?r->ware[0]:"-",r->amount[0],r->ware[1][0]?r->ware[1]:"-",r->amount[1],r->ware[2][0]?r->ware[2]:"-",r->amount[2],(unsigned)r->other);
    return n>0 && (size_t)n<cap?n:0;
}
static int sw_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d frame=%llu calls=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld",sw_installed,
        (unsigned long long)stats.issued,(unsigned long long)sw_calls,(long long)sw_rows,(unsigned long long)sw_from,(unsigned long long)sw_to,
        sw_restored,sw_stage,(long long)sw_failures);
    return n>0 && (size_t)n<cap?n:0;
}
