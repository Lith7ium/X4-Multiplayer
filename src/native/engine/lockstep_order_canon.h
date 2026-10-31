/* Determinism fix, DEFAULT OFF: two more results that follow the heap addresses
 * of wares and macros (native_gate_order_canon = 'observe' | 'canonical', with
 * native_gate_order_canon_rows = {from, to}: ticks, at most 600, optional), and a
 * diagnostic that measures how those addresses order
 * (native_gate_address_order = a tick).
 *
 * The same class as native_gate_loadout_order, confirmed there by full-L95: a
 * macro's or ware's address is per process, so an order taken from it differs
 * between clients. Both sites here read that order directly.
 *
 * Generated loadouts: U::Defensible::GenerateLoadout 0x505fb0 picks each piece
 * of equipment at 14 calls of 0x5012f0 (0x5065fe .. 0x508ed9), which walks its
 * candidates' score groups with draws on the thread word and takes an index into
 * a group. The candidates (0x28-byte entries {ware, macro, ...}) come from
 * U::Container::GetEquipmentWares 0x4354e0, whose list is sorted by ware address
 * at 0x436ab7 (through 0x4bd9d0). The same draws can pick another macro.
 * 'canonical' sorts the candidates by macro id before the call. Any process
 * could have that order: it only fixes which of the address orders is used.
 *
 * Script tables: a table's map compares its keys with 0xcca610, and for the types
 * 0xa..0x14, 0x47..0x52 and others that is the unsigned value (0xcca8bf), the
 * address itself for type 0x47 (a ware) and 0x50 (a macro). $table.keys.list,
 * .keys.random, .random and the key order that scripts then shuffle and search
 * follow it (aiscripts/trade.find.free.xml, order.trade.middleman.xml,
 * order.mining.routine.xml). 'canonical' detours 0xcca8bf (E9 to a stub, the
 * three replaced instructions end at 0xcca8ca) and compares those two types by
 * id (std::string at ware +0x10, macro +0x20); every other type, and equal ids,
 * compare as native. All table operations go through this one comparator, so
 * the tree stays consistent, and it is fixed before the snapshot load builds any
 * table. 'observe' compares natively and counts how many comparisons the
 * canonical order would answer differently (less= in the status).
 *
 * Rows in the window, ORDER_CANON_ROW: one per pick with at least two candidates,
 * the macros in the order they arrived, moved=1 when id order differs. Calls go
 * through one stub (jmp [rip]); pins: the 32 bytes around each call with the
 * original displacement, the 11 replaced bytes. Written by CAS at arm (before the
 * snapshot load), re-checked every gate tick (stop reason 173), restored at stop.
 * A candidate list that cannot be ordered stops the gate in 'canonical'.
 *
 * native_gate_address_order: read only. At arm and once at the given tick, the
 * macro database ([0x6cf1628] +0x40) and the ware database ([0x6cf1620] +0x10),
 * maps of id hash (FNV-1a over the id, +0x20) to object (+0x28), are listed in
 * address order: ADDRESS_ORDER_ROW when=arm|tick kind=macro|ware total=N
 * part=K at=address of the row's first id ids=hash,...
 * tools/dev/census_address_order.py compares two clients: how many exist at arm,
 * which relative orders differ, and whether an object seen at arm and at the tick
 * moved, which says it was rebuilt. The arm sample is the start
 * menu's: LoadLibraries 0x9f3610 runs again for the saved game and clears the
 * macros (0xa2d950), so only the tick sample describes the macros the game plays
 * with. Wares keep their addresses across that load and their order agrees between
 * clients (full-A101), which makes the ware half of the comparator above harmless
 * but unneeded; the macro half is what native_gate_macro_arena also gives. */
#define OC_PICKS 14u
#define OC_TARGET 0x5012f0u
#define OC_LESS_SITE 0xcca8bfu
#define OC_LESS_BACK 0xcca8cau
#define OC_LESS_SIZE 11u
#define OC_LESS_SHA "2560d50ea78fe4306bface1ff13aaf7a28076ddc1a1c9543e31f6b3dc88b94a1"
#define OC_STOP_REASON 173u /* 172 loadout order */
#define OC_RING 8192u
#define OC_SHOWN 4u
#define OC_STUB_PICK 0x00u
#define OC_STUB_LESS 0x40u
#define OC_TYPE_WARE 0x47
#define OC_TYPE_MACRO 0x50
#define OC_WARE_ID 0x10u
#define OC_MACRO_ID 0x20u
enum {OC_OFF=0,OC_OBSERVE=1,OC_CANONICAL=2};
static const struct {uint32_t site;const char *sha;} oc_picks[OC_PICKS]={
    {0x5065feu,"5d4e33d8d1174c682669c3e5eab3b5611d2c89f1b047799a44105e5aa4124423"},
    {0x506638u,"cd5f8e8de30ba4f4726fda10cefb4a708bc92b4b0772ed00bdf89529d4e3a50d"},
    {0x506b5au,"020f7103cc79e2ef034f3a4545b9bfeaaab565d2805b27feb3e983e03cf87287"},
    {0x506b90u,"7135051389c97c4f8a19a8765b3e0153cc41083d1f76a29f1a7f500fb403bfe8"},
    {0x506f06u,"e92fe4e94b2eea9f6c1aeef4de18f6dde4f96c239c082e1d93ef9f772928603c"},
    {0x50737au,"c0726a17e4f01fb00bdd1d7ca5831716ab34a5c2cad5280c4c35aed80b127d89"},
    {0x5073bau,"a74533857961d2f2cb6bf77fa01f3ece06fe6694fb599c30bd2b806722bb7830"},
    {0x507a11u,"ecd1a1e5be562dbc64bc124049655ea5d889cacdcdd4dd83e83bf3db73b281e0"},
    {0x507a4au,"4e555f6187d2cc685abedaf88ff4c07c3ed3f2ff71838fb5b74b84773a5985d1"},
    {0x507f26u,"d619d321f90e098e8c9d807a184fead2c3afad914f555115ea572db5706add3e"},
    {0x507f5eu,"12dee695aff4d7165493179b90ec3e42f4f3ba71e134876680532499f462b588"},
    {0x5086a0u,"9720819bfc2f8351e4ad5375bdedf07c932334f17a24f9656cc234606211c7ff"},
    {0x508e9fu,"c3c495412dad6bff4d87286402b7d3bcf54c59e9f3e2b1192d894127947e352a"},
    {0x508ed9u,"8b8d49773fd6da91bc35c934ed303a4c2b41676873e6bac8982aae0d3be62da9"},
};
static const unsigned char oc_less_orig[OC_LESS_SIZE]={0x48,0x8b,0x47,0x08,0x48,0x39,0x43,0x08,0x0f,0x92,0xc0};
typedef uint64_t (*oc_fn)(void *,void *,void *,void *,void *,void *,void *,void *);
typedef struct {volatile LONG64 ready;uint64_t tick,ptr[OC_SHOWN];uint32_t site,count;char name[OC_SHOWN][LL_ROW_NAME];uint8_t moved;} oc_row_t;
static int oc_mode,oc_installed,oc_restored;
static unsigned oc_done; /* bit i: pick site i; bit OC_PICKS: the comparator */
static unsigned char *oc_page;
static oc_row_t *oc_ring;
static volatile LONG64 oc_rows,oc_failures;
static uint64_t oc_from,oc_to,oc_picks_seen,oc_moved,oc_less_ware,oc_less_macro,oc_inverted_ware,oc_inverted_macro;
static const char *oc_stage="idle";
static oc_fn oc_native; /* tests substitute */
static void oc_fail(const char *why) {oc_stage=why;InterlockedIncrement64(&oc_failures);}
/* An MSVC std::string in an object that lives for the whole process: size +0x10, capacity +0x18. */
static int oc_id(const unsigned char *object,unsigned at,const char **text,size_t *length) {
    if(!object) return 0;
    const unsigned char *s=object+at;uint64_t size=*(const uint64_t*)(s+0x10),capacity=*(const uint64_t*)(s+0x18);
    if(!size || size>4096 || capacity<size) return 0;
    *text=capacity<16?(const char*)s:*(const char *const*)s;*length=(size_t)size;
    return *text!=NULL;
}
/* The comparator's type-0x47/0x50 answer: -1 no answer, else whether a sorts before b by id. */
static int oc_id_less(int type,const unsigned char *a,const unsigned char *b) {
    unsigned at=type==OC_TYPE_WARE?OC_WARE_ID:OC_MACRO_ID;const char *ta,*tb;size_t na,nb;
    if(!oc_id(a,at,&ta,&na) || !oc_id(b,at,&tb,&nb)) return -1;
    int c=memcmp(ta,tb,na<nb?na:nb);
    if(!c && na==nb) return -1;
    return c?c<0:na<nb;
}
/* Called from the stub at 0xcca8bf: a and b are script values of the same type {int type; ...; value +8}. */
static uint64_t oc_less(const unsigned char *a,const unsigned char *b) {
    uint64_t x=*(const uint64_t*)(a+8),y=*(const uint64_t*)(b+8);int native=x<y,type=*(const int*)a;
    if((type==OC_TYPE_WARE || type==OC_TYPE_MACRO) && x!=y) {
        if(type==OC_TYPE_WARE) ++oc_less_ware;else ++oc_less_macro;
        int by_id=oc_id_less(type,(const unsigned char*)(uintptr_t)x,(const unsigned char*)(uintptr_t)y);
        if(by_id>=0 && by_id!=native) {
            if(type==OC_TYPE_WARE) ++oc_inverted_ware;else ++oc_inverted_macro;
            if(oc_mode==OC_CANONICAL) return (uint64_t)by_id;
        }
    }
    return (uint64_t)native;
}
static int oc_in_window(void) {return oc_ring && oc_to && stats.active && boot_phase==3 && stats.issued>=oc_from && stats.issued<=oc_to;}
static void oc_note(uint32_t site,const uint64_t *macros,unsigned count,int moved) {
    LONG64 index=InterlockedIncrement64(&oc_rows)-1;oc_row_t *r=&oc_ring[(uint64_t)index%OC_RING];
    InterlockedExchange64(&r->ready,-1);
    memset(r->ptr,0,sizeof(r->ptr));memset(r->name,0,sizeof(r->name));
    r->tick=stats.issued;r->site=site;r->count=count;r->moved=(uint8_t)moved;
    for(unsigned i=0;i<count && i<OC_SHOWN;i++) {r->ptr[i]=macros[i];ll_macro_name((const void*)(uintptr_t)macros[i],r->name[i],LL_ROW_NAME);}
    InterlockedExchange64(&r->ready,index);
}
/* All 14 sites call this through one stub: the site is the return address. */
static __attribute__((noinline)) uint64_t oc_pick(uint32_t site,void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    ++oc_picks_seen;
    unsigned char *vec=a,*begin=*(unsigned char **)vec,*end=*(unsigned char **)(vec+8);
    size_t count=(size_t)(end-begin)/0x28u;
    if(count>=2) {
        uint64_t before[LL_NODES],after;unsigned shown=count<LL_NODES?(unsigned)count:LL_NODES;
        for(unsigned i=0;i<shown;i++) before[i]=*(const uint64_t*)(begin+i*0x28u+8);
        int moved=0;
        if(oc_mode==OC_CANONICAL) {
            const char *why=ll_sort_candidates(vec);
            if(why) oc_fail(why);
            for(unsigned i=0;i<shown && !moved;i++) {after=*(const uint64_t*)(begin+i*0x28u+8);moved=after!=before[i];}
        } else {
            /* Would id order differ? */
            char names[2][LL_NAME];
            for(unsigned i=0;i+1<shown && !moved;i++) {
                ll_macro_name((const void*)(uintptr_t)before[i],names[0],LL_NAME);ll_macro_name((const void*)(uintptr_t)before[i+1],names[1],LL_NAME);
                moved=strcmp(names[0],names[1])>0;
            }
        }
        if(moved) ++oc_moved;
        if(oc_in_window()) oc_note(site,before,shown,moved);
    }
    oc_fn fn=oc_native?oc_native:(oc_fn)(void *)(image+OC_TARGET);
    return fn(a,b,c,d,e,f,g,h);
}
static uint64_t oc_pick_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    uintptr_t back=(uintptr_t)__builtin_return_address(0);
    uint32_t site=back>=(uintptr_t)image+5 && back<(uintptr_t)image+0x6fa9000?(uint32_t)(back-(uintptr_t)image-5):0;
    return oc_pick(site,a,b,c,d,e,f,g,h);
}
static void oc_write_pick_stub(unsigned char *p) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};void *t=(void*)oc_pick_entry;memcpy(p,code,6);memcpy(p+6,&t,8);
}
/* Reached by jmp at 0xcca8bf: values rbx and rdi; 0xcca8ca reads al only. The stack is aligned as there. */
static void oc_write_less_stub(unsigned char *p,const void *back) {
    static const unsigned char head[6]={0x48,0x89,0xd9,0x48,0x89,0xfa}; /* mov rcx,rbx; mov rdx,rdi */
    unsigned n=0;int32_t d;void *entry=(void*)oc_less;
    memset(p,0xcc,0x40);memcpy(p,head,sizeof(head));n=sizeof(head);
    p[n++]=0x48;p[n++]=0x83;p[n++]=0xec;p[n++]=0x20; /* sub rsp,0x20 */
    p[n++]=0xff;p[n++]=0x15;d=(int32_t)(0x30-(n+4));memcpy(p+n,&d,4);n+=4; /* call [rip] -> +0x30 */
    p[n++]=0x48;p[n++]=0x83;p[n++]=0xc4;p[n++]=0x20; /* add rsp,0x20 */
    p[n++]=0xff;p[n++]=0x25;d=(int32_t)(0x38-(n+4));memcpy(p+n,&d,4);n+=4; /* jmp [rip] -> +0x38 */
    memcpy(p+0x30,&entry,8);memcpy(p+0x38,&back,8);
}
static int32_t oc_pick_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+oc_picks[i].site+5));}
static int32_t oc_less_rel(void) {return (int32_t)((intptr_t)(oc_page+OC_STUB_LESS)-(intptr_t)(image+OC_LESS_SITE+5));}
static int oc_pick_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+oc_picks[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==oc_pick_rel(i,image+OC_TARGET)) return 0;
    if(oc_page && d==oc_pick_rel(i,oc_page+OC_STUB_PICK)) return 1;
    return -1;
}
static int oc_less_state(void) {
    unsigned char now[OC_LESS_SIZE];if(!read_local(image+OC_LESS_SITE,now,sizeof(now))) return -1;
    if(!memcmp(now,oc_less_orig,sizeof(now))) return 0;
    if(oc_page && now[0]==0xe9 && !memcmp(now+5,oc_less_orig+5,OC_LESS_SIZE-5)) {int32_t d;memcpy(&d,now+1,4);if(d==oc_less_rel()) return 1;}
    return -1;
}
static int oc_pins(void) {
    for(unsigned i=0;i<OC_PICKS;i++) {
        unsigned char copy[0x20];
        if(oc_pick_state(i)<0 || !read_local(image+oc_picks[i].site-0x10,copy,sizeof(copy))) return 0;
        int32_t d=oc_pick_rel(i,image+OC_TARGET);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),oc_picks[i].sha)) return 0;
    }
    return oc_less_state()>=0 && hash_ok(oc_less_orig,OC_LESS_SIZE,OC_LESS_SHA);
}
static unsigned char *oc_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x800000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<OC_PICKS && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+oc_picks[i].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            {intptr_t d=(intptr_t)p-(intptr_t)(image+OC_LESS_SITE+5);ok=ok && d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int oc_pick_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(oc_picks[i].site+1,e,v,4);
}
static void oc_restore(void) {
    if(!oc_installed) return;
    for(unsigned i=0;i<OC_PICKS;i++)
        if((oc_done>>i)&1u && oc_pick_state(i)==1 && oc_pick_cas(i,oc_pick_rel(i,oc_page+OC_STUB_PICK),oc_pick_rel(i,image+OC_TARGET))) oc_done&=~(1u<<i);
    if((oc_done>>OC_PICKS)&1u && oc_less_state()==1) {
        unsigned char now[5];memcpy(now,image+OC_LESS_SITE,5);
        if(view_cas_bytes(OC_LESS_SITE,now,oc_less_orig,5)) oc_done&=~(1u<<OC_PICKS);
    }
    if(!oc_done) {oc_installed=0;oc_restored=1;oc_mode=OC_OFF;}
}
static int oc_window(uint64_t from,uint64_t to) {
    if(oc_installed) return from==oc_from && to==oc_to;
    if(to && (from<1 || to<from || to-from>600)) {oc_stage="window";return 0;}
    oc_from=from;oc_to=to;return 1;
}
static void oc_write_code(void) {oc_write_pick_stub(oc_page+OC_STUB_PICK);oc_write_less_stub(oc_page+OC_STUB_LESS,image+OC_LESS_BACK);}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int oc_arm(int mode) {
    if(oc_installed) return stats.active && oc_mode==mode;
    if(mode!=OC_OBSERVE && mode!=OC_CANONICAL) {oc_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    for(unsigned i=0;i<OC_PICKS;i++) if(oc_pick_state(i)!=0) {oc_stage="pins";return 0;}
    if(oc_less_state()!=0 || !oc_pins()) {oc_stage="pins";return 0;}
    if(!oc_ring) oc_ring=VirtualAlloc(NULL,sizeof(oc_row_t)*OC_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!oc_page) oc_page=oc_alloc_page();
    if(!oc_ring || !oc_page) {oc_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(oc_page,0x1000,PAGE_READWRITE,&old)) {oc_stage="page";return 0;}
    oc_write_code();
    if(!VirtualProtect(oc_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {oc_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),oc_page,0x1000);
    oc_picks_seen=oc_moved=oc_less_ware=oc_less_macro=oc_inverted_ware=oc_inverted_macro=0;oc_rows=0;oc_failures=0;oc_restored=0;
    oc_mode=mode;oc_installed=1;
    for(unsigned i=0;i<OC_PICKS;i++) {
        if(!oc_pick_cas(i,oc_pick_rel(i,image+OC_TARGET),oc_pick_rel(i,oc_page+OC_STUB_PICK))) {oc_stage="cas";oc_restore();oc_installed=0;oc_mode=OC_OFF;return 0;}
        oc_done|=1u<<i;
    }
    unsigned char patch[5]={0xe9};int32_t d=oc_less_rel();memcpy(patch+1,&d,4);
    if(!view_cas_bytes(OC_LESS_SITE,oc_less_orig,patch,5)) {oc_stage="cas";oc_restore();oc_installed=0;oc_mode=OC_OFF;return 0;}
    oc_done|=1u<<OC_PICKS;
    oc_stage="armed";return 1;
}
static int oc_owned(void) {
    for(unsigned i=0;i<OC_PICKS;i++) if((oc_done>>i)&1u && oc_pick_state(i)!=1) return 0;
    if((oc_done>>OC_PICKS)&1u && oc_less_state()!=1) return 0;
    if(oc_done) {
        unsigned char pick[14],less[0x40];oc_write_pick_stub(pick);oc_write_less_stub(less,image+OC_LESS_BACK);
        if(memcmp(oc_page+OC_STUB_PICK,pick,14) || memcmp(oc_page+OC_STUB_LESS,less,0x40)) return 0;
    }
    return 1;
}
static int oc_check(void) {
    if(InterlockedCompareExchange64(&oc_failures,0,0)) return 0;
    const char *why=!oc_owned()?"site_changed":!oc_pins()?"block_changed":NULL;
    if(why) {oc_stage=why;InterlockedIncrement64(&oc_failures);return 0;}
    return 1;
}
static int oc_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&oc_rows,0,0);int n;
    if(!oc_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)OC_RING && index<(uint64_t)(count-(LONG64)OC_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const oc_row_t *r=&oc_ring[index%OC_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    n=snprintf(out,cap,"index=%llu tick=%llu site=%x count=%u moved=%u macros=",(unsigned long long)index,(unsigned long long)r->tick,r->site,r->count,(unsigned)r->moved);
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<OC_SHOWN && i<r->count;i++) {
        int m=snprintf(out+n,cap-(size_t)n,"%s%s@%llx",i?",":"",r->name[i][0]?r->name[i]:"?",(unsigned long long)r->ptr[i]);
        if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;
    }
    return n;
}
static int oc_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","observe","canonical"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu picks=%llu moved=%llu less=%llu/%llu inverted=%llu/%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld",
        oc_installed,modes[oc_mode>=0 && oc_mode<3?oc_mode:0],(unsigned long long)stats.issued,(unsigned long long)oc_picks_seen,(unsigned long long)oc_moved,
        (unsigned long long)oc_less_ware,(unsigned long long)oc_less_macro,(unsigned long long)oc_inverted_ware,(unsigned long long)oc_inverted_macro,(long long)oc_rows,(unsigned long long)oc_from,(unsigned long long)oc_to,
        oc_restored,oc_stage,(long long)oc_failures);
    return n>0 && (size_t)n<cap?n:0;
}
/* --- native_gate_address_order --- */
#define AO_IDS_PER_ROW 32u
#define AO_RING 8192u
#define AO_MAX 200000u
#define AO_MACRO_DB 0x6cf1628u
#define AO_MACRO_MAP 0x40u
#define AO_WARE_DB 0x6cf1620u
#define AO_WARE_MAP 0x10u
typedef struct {uint64_t address;uint32_t id;} ao_entry_t;
typedef struct {uint32_t ids[AO_IDS_PER_ROW];uint64_t at;uint32_t total,part,count;uint8_t when,kind,partial;} ao_row_t;
static ao_row_t *ao_ring;
static volatile LONG64 ao_rows;
static uint64_t ao_tick,ao_totals[2][2];
static int ao_armed;
static volatile LONG ao_sampled;
static const char *ao_stage="idle";
static int ao_by_address(const void *a,const void *b) {
    uint64_t x=((const ao_entry_t*)a)->address,y=((const ao_entry_t*)b)->address;
    return x<y?-1:x>y;
}
/* The map walked through guarded reads: another thread may be creating macros. Returns entries, *partial when the walk broke off. */
static size_t ao_walk(const unsigned char *map,ao_entry_t *entries,size_t room,int *partial) {
    uint64_t first=0;size_t k=0;*partial=0;
    if(!read_local(map+0x20,&first,8)) {*partial=1;return 0;}
    const unsigned char *n=(const unsigned char*)(uintptr_t)first;
    while(n && n!=map) {
        unsigned char node[0x30];
        if(k>=room || !read_local(n,node,sizeof(node))) {*partial=1;break;}
        uint64_t id,object,right;memcpy(&id,node+0x20,8);memcpy(&object,node+0x28,8);memcpy(&right,node+0x10,8);
        entries[k].address=object;entries[k].id=(uint32_t)id;k++;
        if(right) {
            const unsigned char *r=(const unsigned char*)(uintptr_t)right;unsigned guard=0;
            for(;;) {
                uint64_t left=0;
                if(++guard>4096 || !read_local(r+8,&left,8)) {*partial=1;return k;}
                if(!left) break;
                r=(const unsigned char*)(uintptr_t)left;
            }
            n=r;
        } else {
            uint64_t parent=0,left=0;const unsigned char *c=n;unsigned guard=0;
            for(;;) {
                if(++guard>4096 || !read_local(c,&parent,8) || !read_local((const unsigned char*)(uintptr_t)parent+8,&left,8)) {*partial=1;return k;}
                if(left==(uint64_t)(uintptr_t)c) break;
                c=(const unsigned char*)(uintptr_t)parent;
            }
            n=(const unsigned char*)(uintptr_t)parent;
        }
    }
    return k;
}
static void ao_emit(unsigned when,unsigned kind,const ao_entry_t *entries,size_t total,int partial) {
    for(size_t at=0,part=0;at<total || part==0;at+=AO_IDS_PER_ROW,part++) {
        LONG64 index=InterlockedIncrement64(&ao_rows)-1;if((uint64_t)index>=AO_RING) return;
        ao_row_t *r=&ao_ring[index];unsigned count=total-at<AO_IDS_PER_ROW?(unsigned)(total-at):AO_IDS_PER_ROW;
        for(unsigned i=0;i<count;i++) r->ids[i]=entries[at+i].id;
        r->at=count?entries[at].address:0;
        r->total=(uint32_t)total;r->part=(uint32_t)part;r->count=count;r->when=(uint8_t)when;r->kind=(uint8_t)kind;r->partial=(uint8_t)partial;
    }
}
static void ao_sample_map(unsigned when,unsigned kind,const unsigned char *map) {
    ao_entry_t *entries=VirtualAlloc(NULL,sizeof(ao_entry_t)*AO_MAX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!entries) {ao_stage="page";return;}
    int partial=0;size_t total=ao_walk(map,entries,AO_MAX,&partial);
    qsort(entries,total,sizeof(*entries),ao_by_address);
    ao_totals[when][kind]=total;
    ao_emit(when,kind,entries,total,partial);
    VirtualFree(entries,0,MEM_RELEASE);
}
static void ao_sample(unsigned when) {
    uint64_t macro=0,ware=0;
    if(read_local(image+AO_MACRO_DB,&macro,8) && macro) ao_sample_map(when,0,(const unsigned char*)(uintptr_t)macro+AO_MACRO_MAP);
    if(read_local(image+AO_WARE_DB,&ware,8) && ware) ao_sample_map(when,1,(const unsigned char*)(uintptr_t)ware+AO_WARE_MAP);
}
/* At arm, on the MoveWorker; the second sample comes from ao_on_tick. A rerun with the same tick is accepted. */
static int ao_arm(uint64_t tick) {
    if(ao_armed) return ao_tick==tick;
    if(tick<1 || tick>18000) {ao_stage="tick";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!ao_ring) ao_ring=VirtualAlloc(NULL,sizeof(ao_row_t)*AO_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ao_ring) {ao_stage="page";return 0;}
    ao_tick=tick;ao_rows=0;ao_sampled=0;ao_armed=1;ao_stage="armed";
    ao_sample(0);
    return 1;
}
/* From the row drain, on the UI thread: gate_tick does not run there, so a check of ui_thread would never pass from it. */
static void ao_on_tick(void) {
    if(ao_armed && stats.active && boot_phase==3 && stats.issued>=ao_tick && GetCurrentThreadId()==ui_thread && !InterlockedCompareExchange(&ao_sampled,1,0)) {ao_sample(1);ao_stage="sampled";}
}
static int ao_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ao_rows,0,0);int n;
    if(!ao_ring || (LONG64)index>=count || index>=AO_RING) return 0;
    const ao_row_t *r=&ao_ring[index];
    n=snprintf(out,cap,"index=%llu when=%s kind=%s total=%u part=%u partial=%u at=%llx ids=",(unsigned long long)index,r->when?"tick":"arm",r->kind?"ware":"macro",r->total,r->part,(unsigned)r->partial,(unsigned long long)r->at);
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<r->count;i++) {
        int m=snprintf(out+n,cap-(size_t)n,"%s%08x",i?",":"",r->ids[i]);
        if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;
    }
    return n;
}
static int ao_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d frame=%llu tick=%llu sampled=%d macros=%llu/%llu wares=%llu/%llu rows=%lld stage=%s",ao_armed,(unsigned long long)stats.issued,(unsigned long long)ao_tick,(int)ao_sampled,
        (unsigned long long)ao_totals[0][0],(unsigned long long)ao_totals[1][0],(unsigned long long)ao_totals[0][1],(unsigned long long)ao_totals[1][1],(long long)ao_rows,ao_stage);
    return n>0 && (size_t)n<cap?n:0;
}
