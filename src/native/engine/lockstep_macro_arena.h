/* Determinism fix, DEFAULT OFF: macros sit in id hash order on every client
 * (native_gate_macro_arena = true).
 *
 * A macro's address is per process, and any map keyed by macro pointer walks in
 * that order: full-A101 has 6-7% of all macro pairs inverted between two clients
 * at tick 150, with the same 5157 macros on both. native_gate_loadout_order and
 * order_canon fix two such walks; this fixes the order itself, for the walks the
 * scan missed too (0x4fae20, 0x4fc610).
 *
 * Macros are created after the gate arms: LoadLibraries 0x9f3610 runs for the saved
 * game, and 0xa2d3d0 deletes every macro (0xa2d950) before loading them again
 * (full-A101: every macro compared across the arm moved). Arming before the load is
 * therefore enough.
 *
 * Every U::Macro of the database comes from 0xa2e7e0: 0x200 bytes, alignment 0x10.
 * That call goes to a region of 2 TiB reserved at arm, one slot of 0x200 per value of
 * the low 32 bits of the id hash, so address order is hash order whatever the base
 * and whatever the order of creation. The region is asked for at one fixed address,
 * so the addresses themselves agree too, and with them anything derived from a macro
 * pointer; if that address is taken the region goes elsewhere (base= says so) and the
 * order still agrees. The hash is the database's own (FNV-1 over
 * the id with a 64-bit state, 0xa2fe21, its key at node +0x20). The constructor has
 * not run yet at that call, so the id is worked out the way it will: attribute 0x1ac
 * of the node (0xccff0) if the node's +0xc is set, defaulting to the template's id,
 * lowercased (0xa266cc .. 0xa2673b). A slot is committed when first used and kept
 * for its id: a macro created again gets the same address, zeroed each time, so what
 * an uninitialised read sees agrees as well.
 *
 * Two different ids on one slot, or a second live macro with one id (a template
 * instance without a name of its own), take the next free slot. Those are the only
 * two whose relative order can follow creation; they are counted and logged
 * (collisions, duplicates; full-A101's 5157 hashes have none of the first).
 *
 * The other U::Macro constructor (0xa27180 from 0x8d21a0) serves only the Lua export
 * SetClusterBackground: a temporary with an empty id, never in the database. It
 * stays on the heap and is counted (ui=, its call 0x8d21d2 goes through a counting
 * stub).
 *
 * Macros die in the deleting destructor (0xa27100, vtable slot 0). Its free at
 * 0xa27148 goes to a route that takes arena pointers and hands everything else to
 * the native free 0x14dbee0. THE ONE EXCEPTION to "restored at stop": this route stays
 * until the process exits, because arena pointers still exist then and the native
 * allocator cannot free them. Not routed: the unwinding of a constructor that throws
 * (0xb3590 from 0xa2e7a0's exception table), which would hand an arena pointer to
 * the native free. The constructor throws only when an allocation fails.
 *
 * Fail closed: from the row drain, on the thread that creates macros, the database
 * is audited at the first admitted tick and every 600 ticks. A macro outside the
 * arena, or at a slot its id does not lead to, stops the gate (stop reason 175) with
 * a row naming it. So does a failed commit, after which the macro is on the heap.
 * Pins: the 32 bytes around each of the three calls. Written by CAS at arm, re-checked
 * every gate tick, the two allocation sites restored at stop. */
#define MA_SITES 3u
#define MA_STOP_REASON 175u /* 174 pick order */
#define MA_SLOT 0x200u
#define MA_REGION (((SIZE_T)1<<32)*MA_SLOT)
#define MA_BASE 0x200000000000ull
#define MA_TABLE_BITS 18u
#define MA_TABLE (1u<<MA_TABLE_BITS)
#define MA_PROBES 65536u
#define MA_RING 1024u
#define MA_STUB 0x20u
#define MA_ALLOC 0x14b7140u
#define MA_FREE 0x14dbee0u
#define MA_ATTR 0xccff0u
#define MA_NAME_ATTR 0x1acu
#define MA_ID 0x20u
#define MA_AUDIT_EVERY 600u
enum {MA_SITE_MACRO=0,MA_SITE_UI=1,MA_SITE_FREE=2};
enum {MA_ROW_COLLISION=0,MA_ROW_DUPLICATE=1,MA_ROW_UI=2,MA_ROW_FALLBACK=3,MA_ROW_OUTSIDE=4,MA_ROW_MISPLACED=5};
static const struct {uint32_t site,target;const char *sha,*name;} ma_sites[MA_SITES]={
    {0xa2e7e0u,MA_ALLOC,"8d9a945ed5fa3f5293701742e76099a911aa13796f632a1fba62b50c8305991c","macro"},
    {0x8d21d2u,MA_ALLOC,"0f258b7d37830e8191913a403a5ee47cabb4d1d7de5cf2cbf237c79c123bb41b","ui"},
    {0xa27148u,MA_FREE,"190fa620073cee897396b2429b30811c02702f7c0847ecc92e768a861ba48fa0","free"},
};
typedef void *(*ma_alloc_fn)(size_t,void *,void *,void *,uint32_t);
typedef void (*ma_free_fn)(void *);
typedef const unsigned char *(*ma_attr_fn)(const unsigned char *,uint32_t,const unsigned char *);
typedef struct {uint64_t id;uint32_t slot;uint8_t used,live;} ma_entry_t;
typedef struct {volatile LONG64 ready;uint64_t tick,id,other;uint32_t slot;uint8_t kind;} ma_row_t;
static int ma_installed,ma_restored,ma_free_kept;
static unsigned ma_done;
static unsigned char *ma_page,*ma_base;
static ma_entry_t *ma_table;
static ma_row_t *ma_ring;
static ao_entry_t *ma_audit_entries;
static SRWLOCK ma_lock;
static volatile LONG64 ma_rows,ma_failures;
static uint64_t ma_created,ma_freed,ma_live,ma_ui,ma_fallback,ma_collisions,ma_duplicates,ma_unique,ma_bad_free;
static uint64_t ma_db,ma_outside,ma_misplaced,ma_probed,ma_audits,ma_partial,ma_next_audit;
static const char *ma_stage="idle";
static ma_alloc_fn ma_native_alloc; /* tests substitute */
static ma_free_fn ma_native_free;
static ma_attr_fn ma_attr;
static void ma_fail(const char *why) {ma_stage=why;InterlockedIncrement64(&ma_failures);}
static void ma_note(unsigned kind,uint64_t id,uint32_t slot,uint64_t other) {
    if(!ma_ring) return;
    LONG64 index=InterlockedIncrement64(&ma_rows)-1;ma_row_t *r=&ma_ring[(uint64_t)index%MA_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->id=id;r->other=other;r->slot=slot;r->kind=(uint8_t)kind;
    InterlockedExchange64(&r->ready,index);
}
/* The database's hash over the bytes it will hash: FNV-1 with a 64-bit state, the byte sign-extended; lowercased here. */
static uint64_t ma_hash_lower(const char *text,size_t length) {
    uint64_t h=0x811c9dc5u;
    for(size_t i=0;i<length;i++) {
        signed char c=(signed char)text[i];
        if(c>='A' && c<='Z') c=(signed char)(c+('a'-'A'));
        h*=0x1000193u;h^=(uint64_t)(int64_t)c;
    }
    return h;
}
/* An MSVC std::string (size +0x10, capacity +0x18) or nothing: the id it holds, hashed. */
static uint64_t ma_hash_string(const unsigned char *s) {
    if(!s) return ma_hash_lower("",0);
    uint64_t size=*(const uint64_t*)(s+0x10),capacity=*(const uint64_t*)(s+0x18);
    if(!size || size>4096) return ma_hash_lower("",0);
    const char *text=capacity<16?(const char*)s:*(const char *const*)s;
    return text?ma_hash_lower(text,(size_t)size):ma_hash_lower("",0);
}
/* The id the constructor will give this macro (0xa25904, 0xa266cc .. 0xa2673b). */
static uint64_t ma_predict(const unsigned char *node,const unsigned char *macro_template) {
    const unsigned char *fallback=macro_template?macro_template+MA_ID:NULL;
    if(!node || !*(const int32_t*)(node+0xc)) return ma_hash_string(fallback);
    ma_attr_fn get=ma_attr?ma_attr:(ma_attr_fn)(void *)(image+MA_ATTR);
    return ma_hash_string(get(node,MA_NAME_ATTR,fallback));
}
static ma_entry_t *ma_find(uint32_t slot) {
    uint32_t at=(slot*2654435761u)>>(32-MA_TABLE_BITS);
    while(ma_table[at].used && ma_table[at].slot!=slot) at=(at+1)&(MA_TABLE-1);
    return &ma_table[at];
}
static void *ma_heap_alloc(size_t size) {
    ma_alloc_fn f=ma_native_alloc?ma_native_alloc:(ma_alloc_fn)(void *)(image+MA_ALLOC);
    return f(size,NULL,NULL,NULL,0x10);
}
static unsigned char *ma_reserve(void) {
    unsigned char *p=VirtualAlloc((void*)(uintptr_t)MA_BASE,MA_REGION,MEM_RESERVE,PAGE_NOACCESS);
    return p?p:VirtualAlloc(NULL,MA_REGION,MEM_RESERVE,PAGE_NOACCESS);
}
/* A slot for this id: its own if free, else the next free one, one count and row per macro
 * that could not have its own. NULL when there is none. */
static void *ma_place(uint64_t id) {
    uint32_t slot=(uint32_t)id;void *p=NULL;int collided=0,duplicated=0;uint64_t other=0;
    AcquireSRWLockExclusive(&ma_lock);
    for(unsigned probes=0;probes<MA_PROBES;probes++,slot++) {
        ma_entry_t *e=ma_find(slot);
        if(!e->used) {
            if(ma_unique>=MA_TABLE/2) break;
            e->used=1;e->slot=slot;e->id=id;++ma_unique;
        } else if(e->id!=id || e->live) {
            if(!collided && !duplicated) other=e->id;
            if(e->id!=id) collided=1;else duplicated=1;
            continue;
        }
        p=ma_base+(uint64_t)slot*MA_SLOT;
        if(!VirtualAlloc(p,MA_SLOT,MEM_COMMIT,PAGE_READWRITE)) {p=NULL;break;}
        memset(p,0,MA_SLOT);
        e->live=1;++ma_live;++ma_created;
        if(collided) {++ma_collisions;ma_note(MA_ROW_COLLISION,id,slot,other);}
        else if(duplicated) {++ma_duplicates;ma_note(MA_ROW_DUPLICATE,id,slot,other);}
        break;
    }
    ReleaseSRWLockExclusive(&ma_lock);
    return p;
}
/* At 0xa2e7e0: size rcx, the node (r13) rdx, the template (r14) r8. */
static void *ma_alloc_entry(size_t size,const unsigned char *node,const unsigned char *macro_template) {
    uint64_t id=ma_predict(node,macro_template);
    void *p=size==MA_SLOT?ma_place(id):NULL;
    if(p) return p;
    ++ma_fallback;ma_note(MA_ROW_FALLBACK,id,(uint32_t)id,0);ma_fail("fallback");
    return ma_heap_alloc(size);
}
/* At 0x8d21d2: the temporary for SetClusterBackground. */
static void *ma_ui_entry(size_t size) {
    ++ma_ui;ma_note(MA_ROW_UI,0,0,0);
    return ma_heap_alloc(size);
}
/* At 0xa27148, and for as long as the process lives. */
static void ma_free_entry(void *p) {
    uintptr_t at=(uintptr_t)p-(uintptr_t)ma_base;
    if(ma_base && at<MA_REGION) {
        AcquireSRWLockExclusive(&ma_lock);
        ma_entry_t *e=ma_find((uint32_t)(at/MA_SLOT));
        if(e->used && e->live) {e->live=0;--ma_live;++ma_freed;} else ++ma_bad_free;
        ReleaseSRWLockExclusive(&ma_lock);
        return;
    }
    ma_free_fn f=ma_native_free?ma_native_free:(ma_free_fn)(void *)(image+MA_FREE);
    f(p);
}
static void *const ma_entries[MA_SITES]={(void*)ma_alloc_entry,(void*)ma_ui_entry,(void*)ma_free_entry};
static void ma_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char regs[6]={0x4c,0x89,0xea,0x4d,0x89,0xf0}; /* mov rdx,r13; mov r8,r14 */
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};
    unsigned n=0;
    memset(p,0xcc,MA_STUB);
    if(i==MA_SITE_MACRO) {memcpy(p,regs,sizeof(regs));n=sizeof(regs);}
    memcpy(p+n,jump,sizeof(jump));memcpy(p+n+sizeof(jump),&ma_entries[i],8);
}
static int32_t ma_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+ma_sites[i].site+5));}
static int ma_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+ma_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==ma_rel(i,image+ma_sites[i].target)) return 0;
    if(ma_page && d==ma_rel(i,ma_page+MA_STUB*i)) return 1;
    return -1;
}
static int ma_pins(void) {
    for(unsigned i=0;i<MA_SITES;i++) {
        unsigned char copy[0x20];
        if(ma_state(i)<0 || !read_local(image+ma_sites[i].site-0x10,copy,sizeof(copy))) return 0;
        int32_t d=ma_rel(i,image+ma_sites[i].target);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),ma_sites[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *ma_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x900000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<MA_SITES && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+ma_sites[i].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int ma_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(ma_sites[i].site+1,e,v,4);
}
/* At stop: the allocation sites. The free route stays, see the header. */
static void ma_restore(void) {
    if(!ma_installed) return;
    for(unsigned i=0;i<MA_SITES;i++) {
        if(i==MA_SITE_FREE || !((ma_done>>i)&1u) || ma_state(i)!=1) continue;
        if(ma_cas(i,ma_rel(i,ma_page+MA_STUB*i),ma_rel(i,image+ma_sites[i].target))) ma_done&=~(1u<<i);
    }
    if(!(ma_done&~(1u<<MA_SITE_FREE))) {ma_installed=0;ma_restored=1;ma_free_kept=(ma_done>>MA_SITE_FREE)&1u;}
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun is accepted. */
static int ma_arm(void) {
    if(ma_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    for(unsigned i=0;i<MA_SITES;i++) if(ma_state(i)!=0) {ma_stage="pins";return 0;}
    if(!ma_pins()) {ma_stage="pins";return 0;}
    if(!ma_ring) ma_ring=VirtualAlloc(NULL,sizeof(ma_row_t)*MA_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ma_table) ma_table=VirtualAlloc(NULL,sizeof(ma_entry_t)*MA_TABLE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ma_audit_entries) ma_audit_entries=VirtualAlloc(NULL,sizeof(ao_entry_t)*AO_MAX,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ma_page) ma_page=ma_alloc_page();
    if(!ma_ring || !ma_table || !ma_audit_entries || !ma_page) {ma_stage="page";return 0;}
    if(!ma_base) ma_base=ma_reserve();
    if(!ma_base) {ma_stage="reserve";return 0;}
    DWORD old,ignored;if(!VirtualProtect(ma_page,0x1000,PAGE_READWRITE,&old)) {ma_stage="page";return 0;}
    for(unsigned i=0;i<MA_SITES;i++) ma_write_stub(ma_page+MA_STUB*i,i);
    if(!VirtualProtect(ma_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {ma_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ma_page,0x1000);
    ma_rows=0;ma_failures=0;ma_restored=0;ma_next_audit=1;ma_installed=1;
    /* The free route first: no arena pointer exists before it does. */
    for(unsigned i=MA_SITES;i-->0;) {
        if(!ma_cas(i,ma_rel(i,image+ma_sites[i].target),ma_rel(i,ma_page+MA_STUB*i))) {
            ma_stage="cas";
            /* Nothing lives in the arena yet: every site goes back, the free route too. */
            for(unsigned k=0;k<MA_SITES;k++) if((ma_done>>k)&1u && ma_cas(k,ma_rel(k,ma_page+MA_STUB*k),ma_rel(k,image+ma_sites[k].target))) ma_done&=~(1u<<k);
            ma_installed=0;return 0;
        }
        ma_done|=1u<<i;
    }
    ma_stage="armed";return 1;
}
static int ma_owned(void) {
    if(!ma_installed) return 1;
    for(unsigned i=0;i<MA_SITES;i++) if((ma_done>>i)&1u) {
        unsigned char want[MA_STUB];ma_write_stub(want,i);
        if(ma_state(i)!=1 || memcmp(ma_page+MA_STUB*i,want,MA_STUB)) return 0;
    }
    return 1;
}
static int ma_check(void) {
    if(InterlockedCompareExchange64(&ma_failures,0,0)) return 0;
    const char *why=!ma_owned()?"site_changed":!ma_pins()?"block_changed":NULL;
    if(why) {ma_stage=why;InterlockedIncrement64(&ma_failures);return 0;}
    return 1;
}
/* Every macro of the database: inside the arena, at the slot its id leads to or one probed from it. */
static void ma_audit_map(const unsigned char *map) {
    int partial=0;size_t total=ao_walk(map,ma_audit_entries,AO_MAX,&partial);
    uint64_t outside=0,misplaced=0,probed=0;
    AcquireSRWLockShared(&ma_lock);
    for(size_t i=0;i<total;i++) {
        uintptr_t at=(uintptr_t)ma_audit_entries[i].address-(uintptr_t)ma_base;
        uint32_t id=ma_audit_entries[i].id;
        if(at>=MA_REGION) {++outside;ma_note(MA_ROW_OUTSIDE,id,0,ma_audit_entries[i].address);continue;}
        uint32_t slot=(uint32_t)(at/MA_SLOT);
        if(slot==id) continue;
        ma_entry_t *e=ma_find(slot);
        if(e->used && (uint32_t)e->id==id) ++probed;
        else {++misplaced;ma_note(MA_ROW_MISPLACED,id,slot,e->used?e->id:0);}
    }
    ReleaseSRWLockShared(&ma_lock);
    ma_db=total;ma_outside=outside;ma_misplaced=misplaced;ma_probed=probed;++ma_audits;
    if(partial) ++ma_partial; /* a walk that ended early proves only what it saw: reported, not a stop */
    if(outside) ma_fail("outside");else if(misplaced) ma_fail("misplaced");
}
static void ma_on_drain(void) {
    if(!ma_installed || !stats.active || boot_phase!=3 || !stats.issued || stats.issued<ma_next_audit || GetCurrentThreadId()!=ui_thread) return;
    ma_next_audit=stats.issued+MA_AUDIT_EVERY;
    uint64_t db=0;
    if(read_local(image+AO_MACRO_DB,&db,8) && db) ma_audit_map((const unsigned char*)(uintptr_t)db+AO_MACRO_MAP);
}
static int ma_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ma_rows,0,0);int n;
    if(!ma_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)MA_RING && index<(uint64_t)(count-(LONG64)MA_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ma_row_t *r=&ma_ring[index%MA_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const kinds[6]={"collision","duplicate","ui","fallback","outside","misplaced"};
    n=snprintf(out,cap,"index=%llu tick=%llu kind=%s id=%016llx slot=%08x other=%016llx",(unsigned long long)index,(unsigned long long)r->tick,kinds[r->kind<6?r->kind:0],
        (unsigned long long)r->id,r->slot,(unsigned long long)r->other);
    return n>0 && (size_t)n<cap?n:0;
}
static int ma_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"installed=%d base=%llx frame=%llu created=%llu live=%llu freed=%llu unique=%llu ui=%llu fallback=%llu collisions=%llu duplicates=%llu db=%llu outside=%llu misplaced=%llu probed=%llu audits=%llu partial=%llu bad_free=%llu free_kept=%d rows=%lld restored=%d stage=%s failures=%lld",
        ma_installed,(unsigned long long)(uintptr_t)ma_base,(unsigned long long)stats.issued,(unsigned long long)ma_created,(unsigned long long)ma_live,(unsigned long long)ma_freed,(unsigned long long)ma_unique,(unsigned long long)ma_ui,(unsigned long long)ma_fallback,
        (unsigned long long)ma_collisions,(unsigned long long)ma_duplicates,(unsigned long long)ma_db,(unsigned long long)ma_outside,(unsigned long long)ma_misplaced,(unsigned long long)ma_probed,(unsigned long long)ma_audits,(unsigned long long)ma_partial,(unsigned long long)ma_bad_free,
        ma_free_kept,(long long)ma_rows,ma_restored,ma_stage,(long long)ma_failures);
    return n>0 && (size_t)n<cap?n:0;
}
