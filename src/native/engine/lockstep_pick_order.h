/* Determinism fix, DEFAULT OFF: weighted picks from the libraries' selection
 * tables follow the order of the file, not the heap (native_gate_pick_order =
 * 'observe' | 'canonical', with native_gate_pick_order_rows = {from, to}: ticks,
 * at most 600, optional).
 *
 * A selection table is a map from a parsed record (a macro selection, a generator
 * alternative, a drop entry) to its weight: node {.., key +0x20, weight +0x28},
 * header {.., first +0x20, size +0x28, chunk list +0x38, mode +0x40, total +0x44}.
 * The four template copies 0x4759e0, 0x475b60, 0x476cf0 and 0xe5a450 pick from it:
 * modes 0-2 return the first key; mode 3 draws an index below the total and walks
 * the map subtracting weights (a weight of 0 counts as 1); mode 4 does the same and,
 * if the walk runs out, once more as mode 3. The map is keyed by the record's
 * address, so the walk order, and with it the first key, is heap order. Reach:
 * ShipGenerator (0xc7d3f6, 0xc8212a), NPCGenerator (0xc77fc5), the station, factory
 * and prop generators (0x9254c0, 0x931360), mass traffic quotas, drops
 * (0x54933c, 0x549532, 0x5496ec) and collectables (0x3cc8fd, 0x3cda4e, 0x3cf2dd).
 *
 * The tables are built by the library imports: LoadLibraries 0x9f3610 runs at each
 * module entry, also for a saved game (0x9eebab). What that rebuilds differs: macros
 * yes (0xa2d950, full-A101), wares no (full-A101), drops clear first in the code
 * (DropDBClass::Import 0x548400). Either way the records come from the shared heap.
 *
 * What is stable is the node pool. Nodes come from chunks (0x320850: {count +0,
 * older chunk +8, nodes +0x10, 0x30 bytes each}), handed out in ascending order, and
 * the chunk list only grows. A table built once and never erased from therefore
 * holds its entries in insertion order, which is the order of the XML file:
 * oldest chunk first, ascending, skipping free nodes (parent 0). That order is
 * independent of any address, needs no hook at build time, and holds for tables
 * built before the gate arms too.
 *
 * Each function's entry goes through a stub (E9) to one C implementation of the
 * logic above; it takes the same draws from the same generator (0x14ddf70, or
 * 0x14ddc40 with the state in r9 for 0x476cf0 and 0xe5a450) and walks in the order
 * the mode asks for: 'canonical' pool order, 'observe' the map's own order, as
 * native. Both compute both answers. The stub keeps r9, r10, r11 and xmm0-5, which
 * the native functions leave alone and a caller may rely on. Rows in the window,
 * PICK_ORDER_ROW, one per pick from a table of two or more: caller, size, mode,
 * total, draw and the pool position of what each order picks; moved=1 when they
 * differ. A pool position names the same entry on every client, so two clients'
 * native= columns differing at one tick is the table order differing. Pins: each
 * function's whole body. Written by CAS at arm (before the snapshot load),
 * re-checked every gate tick (stop reason 174), restored at stop. A table whose pool
 * does not account for its size stops the gate in 'canonical'. */
#define PO_TARGETS 4u
#define PO_STOP_REASON 174u /* 173 order canon */
#define PO_RING 8192u
#define PO_MAX 1024u
#define PO_CHUNKS 48u
#define PO_NODE 0x30u
#define PO_STUB 0x80u
enum {PO_OFF=0,PO_OBSERVE=1,PO_CANONICAL=2};
static const struct {uint32_t rva,end;const char *sha;unsigned char orig[5];int state;} po_targets[PO_TARGETS]={
    {0x4759e0u,0x475b51u,"60e9ea4fb91f49f573bf8672a0801083d4dea5f048d40ede296c4c87234761f8",{0x40,0x53,0x48,0x83,0xec},0},
    {0x475b60u,0x475cd1u,"21df2d6562e15ad1ddb8f218e6048f77444b29c9d77608b95710bbc2a1d5e2a7",{0x40,0x53,0x48,0x83,0xec},0},
    {0x476cf0u,0x476e8bu,"50d165c4e94358916c9018f27ea30d36d70a6df5cd93a6db129a92cdc50bb22c",{0x48,0x89,0x5c,0x24,0x08},1},
    {0xe5a450u,0xe5a5ebu,"77b5261d33b8d0806ea0ea3350af70538f23abc82906fed5a5dc17c1203d4730",{0x48,0x89,0x5c,0x24,0x08},1},
};
#define PO_RNG_PLAIN 0x14ddf70u
#define PO_RNG_STATE 0x14ddc40u
typedef uint32_t (*po_rng_plain_fn)(uint32_t);
typedef uint32_t (*po_rng_state_fn)(void *,uint32_t);
typedef struct {volatile LONG64 ready;uint64_t tick;uint32_t caller,size,total,draw,target,native,canon;int32_t mode;uint8_t moved;} po_row_t;
static int po_mode,po_installed,po_restored;
static unsigned po_done;
static unsigned char *po_page;
static po_row_t *po_ring;
static volatile LONG64 po_rows,po_failures;
static uint64_t po_from,po_to,po_picks,po_moved,po_small;
static const char *po_stage="idle";
static po_rng_plain_fn po_plain; /* tests substitute */
static po_rng_state_fn po_state;
static void po_fail(const char *why) {po_stage=why;InterlockedIncrement64(&po_failures);}
/* The entries in insertion order; 0xffffffff when the pool does not account for the map's size. */
static unsigned po_pool(const unsigned char *map,const unsigned char **nodes,unsigned room) {
    const unsigned char *chunks[PO_CHUNKS];unsigned n=0,k=0;
    for(const unsigned char *c=*(const unsigned char *const*)(map+0x38);c;c=*(const unsigned char *const*)(c+8)) {
        if(n>=PO_CHUNKS) return 0xffffffffu;
        chunks[n++]=c;
    }
    while(n) {
        const unsigned char *c=chunks[--n],*node=c+0x10;uint64_t count=*(const uint64_t*)c;
        for(uint64_t i=0;i<count;i++,node+=PO_NODE) {
            if(!*(const void *const*)node) continue; /* free: no parent */
            if(k>=room) return 0xffffffffu;
            nodes[k++]=node;
        }
    }
    return k==*(const uint64_t*)(map+0x28)?k:0xffffffffu;
}
/* The map's own order. */
static unsigned po_tree(unsigned char *map,const unsigned char **nodes,unsigned room) {
    unsigned k=0;
    for(unsigned char *n=*(unsigned char **)(map+0x20);n!=map;n=ll_next(n)) {
        if(k>=room) return 0xffffffffu;
        nodes[k++]=n;
    }
    return k;
}
static uint32_t po_draw(unsigned target,int32_t total,void *state) {
    if(po_targets[target].state && state) {
        po_rng_state_fn f=po_state?po_state:(po_rng_state_fn)(void *)(image+PO_RNG_STATE);
        return f(state,(uint32_t)total);
    }
    po_rng_plain_fn f=po_plain?po_plain:(po_rng_plain_fn)(void *)(image+PO_RNG_PLAIN);
    return f((uint32_t)total);
}
/* The walk: subtract weights until below zero; a weight of 0 counts as 1. */
static const unsigned char *po_walk(const unsigned char *const *nodes,unsigned count,int32_t r) {
    for(unsigned i=0;i<count;i++) {
        int32_t weight=*(const int32_t*)(nodes[i]+0x28);
        r-=weight?weight:1;
        if(r<0) return nodes[i];
    }
    return NULL;
}
static int po_position(const unsigned char *const *pool,unsigned count,const unsigned char *node) {
    for(unsigned i=0;i<count;i++) if(pool[i]==node) return (int)i;
    return -1;
}
static void po_note(unsigned target,uint32_t caller,const unsigned char *map,int32_t mode,int32_t total,uint32_t draw,int native,int canon) {
    LONG64 index=InterlockedIncrement64(&po_rows)-1;po_row_t *r=&po_ring[(uint64_t)index%PO_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=stats.issued;r->caller=caller;r->size=(uint32_t)*(const uint64_t*)(map+0x28);r->total=(uint32_t)total;r->draw=draw;r->target=target;
    r->native=(uint32_t)native;r->canon=(uint32_t)canon;r->mode=mode;r->moved=native!=canon;
    InterlockedExchange64(&r->ready,index);
}
/* The native logic with the order chosen here. */
static uint64_t po_pick(unsigned target,uint32_t caller,unsigned char *map,void *state) {
    int32_t mode=*(const int32_t*)(map+0x40),total=*(const int32_t*)(map+0x44);uint64_t size=*(const uint64_t*)(map+0x28);
    ++po_picks;
    if(size<2) {
        /* One order only: the draws are all that matters. */
        ++po_small;
        const unsigned char *only=size?*(const unsigned char *const*)(map+0x20):NULL;
        if(mode==3 || mode==4) {
            for(int pass=mode==4?2:1;pass>0;pass--) {
                int32_t r=total>1?(int32_t)po_draw(target,total,state):total;
                if(only && po_walk(&only,1,r)) return *(const uint64_t*)(only+0x20);
            }
        }
        return only?*(const uint64_t*)(only+0x20):0;
    }
    const unsigned char *pool[PO_MAX],*tree[PO_MAX];
    unsigned pooled=po_pool(map,pool,PO_MAX),treed=po_tree(map,tree,PO_MAX);
    int canonical=po_mode==PO_CANONICAL;
    if(pooled==0xffffffffu || treed==0xffffffffu) {
        if(canonical) po_fail(treed==0xffffffffu?"nodes":"pool");
        canonical=0;
    }
    if(treed==0xffffffffu) {
        /* Too large to hold: the native walk, unrecorded. */
        if(mode==3 || mode==4) {
            for(int pass=mode==4?2:1;pass>0;pass--) {
                int32_t r=total>1?(int32_t)po_draw(target,total,state):total;
                for(unsigned char *n=*(unsigned char **)(map+0x20);n!=map;n=ll_next(n)) {
                    int32_t weight=*(const int32_t*)(n+0x28);
                    r-=weight?weight:1;
                    if(r<0) return *(const uint64_t*)(n+0x20);
                }
            }
        }
        return *(const uint64_t*)(*(unsigned char **)(map+0x20)+0x20);
    }
    if(!treed) return 0;
    const unsigned char *native=NULL,*canon=NULL;uint32_t draw=0;
    if(mode==3 || mode==4) {
        for(int pass=mode==4?2:1;pass>0;pass--) {
            int32_t r=total>1?(int32_t)po_draw(target,total,state):total;
            draw=(uint32_t)r;
            const unsigned char *a=po_walk(tree,treed,r),*b=pooled!=0xffffffffu?po_walk(pool,pooled,r):NULL;
            /* Keep going only if the order in use ran out. */
            if(canonical?b!=NULL:a!=NULL) {native=a;canon=b;break;}
            native=a;canon=b;
        }
    }
    /* Modes 0-2, and a walk that ran out: the first key. */
    if(!native) native=tree[0];
    if(!canon && pooled!=0xffffffffu && pooled) canon=pool[0];
    const unsigned char *picked=canonical?canon:native;
    if(pooled!=0xffffffffu) {
        int a=po_position(pool,pooled,native),b=canon?po_position(pool,pooled,canon):-1;
        if(a!=b) ++po_moved;
        if(po_ring && po_to && stats.active && boot_phase==3 && stats.issued>=po_from && stats.issued<=po_to) po_note(target,caller,map,mode,total,draw,a,b);
    }
    return *(const uint64_t*)(picked+0x20);
}
static uint64_t po_entry(unsigned char *map,uint64_t back,uint32_t target,void *state) {
    uintptr_t base=(uintptr_t)image;
    return po_pick(target,back>=base+5 && back<base+0x6fa9000?(uint32_t)(back-base-5):0,map,state);
}
/* Entered by jmp from the function's first byte: return address on the stack, map in rcx, state in r9. */
static void po_write_stub(unsigned char *p,unsigned target) {
    static const unsigned char save[]={
        0x41,0x51,0x41,0x52,0x41,0x53,                         /* push r9, r10, r11 */
        0x48,0x81,0xec,0x80,0,0,0,                             /* sub rsp,0x80 */
        0x0f,0x11,0x44,0x24,0x20,0x0f,0x11,0x4c,0x24,0x30,     /* movups [rsp+0x20],xmm0; [rsp+0x30],xmm1 */
        0x0f,0x11,0x54,0x24,0x40,0x0f,0x11,0x5c,0x24,0x50,     /* xmm2, xmm3 */
        0x0f,0x11,0x64,0x24,0x60,0x0f,0x11,0x6c,0x24,0x70,     /* xmm4, xmm5 */
        0x48,0x8b,0x94,0x24,0x98,0,0,0,                        /* mov rdx,[rsp+0x98]: the return address */
    };
    static const unsigned char load[]={
        0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0xc3, /* add rsp,0x80; pop r11, r10, r9; ret */
    };
    static const unsigned char restore[]={
        0x0f,0x10,0x44,0x24,0x20,0x0f,0x10,0x4c,0x24,0x30,
        0x0f,0x10,0x54,0x24,0x40,0x0f,0x10,0x5c,0x24,0x50,
        0x0f,0x10,0x64,0x24,0x60,0x0f,0x10,0x6c,0x24,0x70,
    };
    unsigned n=0;int32_t d;void *entry=(void*)po_entry;
    memset(p,0xcc,PO_STUB);memcpy(p,save,sizeof(save));n=sizeof(save);
    p[n++]=0x41;p[n++]=0xb8;p[n++]=(unsigned char)target;p[n++]=0;p[n++]=0;p[n++]=0; /* mov r8d,target */
    p[n++]=0xff;p[n++]=0x15;d=(int32_t)(0x78-(n+4));memcpy(p+n,&d,4);n+=4; /* call [rip] -> +0x78 */
    memcpy(p+n,restore,sizeof(restore));n+=sizeof(restore);
    memcpy(p+n,load,sizeof(load));n+=sizeof(load);
    memcpy(p+0x78,&entry,8);
}
static int32_t po_rel(unsigned i) {return (int32_t)((intptr_t)(po_page+PO_STUB*i)-(intptr_t)(image+po_targets[i].rva+5));}
/* 0 original, 1 ours, -1 anything else. */
static int po_state_of(unsigned i) {
    unsigned char now[5];if(!read_local(image+po_targets[i].rva,now,5)) return -1;
    if(!memcmp(now,po_targets[i].orig,5)) return 0;
    if(po_page && now[0]==0xe9) {int32_t d;memcpy(&d,now+1,4);if(d==po_rel(i)) return 1;}
    return -1;
}
static int po_pins(void) {
    for(unsigned i=0;i<PO_TARGETS;i++) {
        unsigned char copy[0x200];unsigned size=po_targets[i].end-po_targets[i].rva;
        if(po_state_of(i)<0 || size>sizeof(copy) || !read_local(image+po_targets[i].rva,copy,size)) return 0;
        memcpy(copy,po_targets[i].orig,5);
        if(!hash_ok(copy,size,po_targets[i].sha)) return 0;
    }
    return 1;
}
static unsigned char *po_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x700000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<PO_TARGETS && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+po_targets[i].rva+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void po_restore(void) {
    if(!po_installed) return;
    for(unsigned i=0;i<PO_TARGETS;i++) {
        if(!((po_done>>i)&1u) || po_state_of(i)!=1) continue;
        unsigned char now[5];memcpy(now,image+po_targets[i].rva,5);
        if(view_cas_bytes(po_targets[i].rva,now,po_targets[i].orig,5)) po_done&=~(1u<<i);
    }
    if(!po_done) {po_installed=0;po_restored=1;po_mode=PO_OFF;}
}
static int po_window(uint64_t from,uint64_t to) {
    if(po_installed) return from==po_from && to==po_to;
    if(to && (from<1 || to<from || to-from>600)) {po_stage="window";return 0;}
    po_from=from;po_to=to;return 1;
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int po_arm(int mode) {
    if(po_installed) return stats.active && po_mode==mode;
    if(mode!=PO_OBSERVE && mode!=PO_CANONICAL) {po_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    for(unsigned i=0;i<PO_TARGETS;i++) if(po_state_of(i)!=0) {po_stage="pins";return 0;}
    if(!po_pins()) {po_stage="pins";return 0;}
    if(!po_ring) po_ring=VirtualAlloc(NULL,sizeof(po_row_t)*PO_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!po_page) po_page=po_alloc_page();
    if(!po_ring || !po_page) {po_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(po_page,0x1000,PAGE_READWRITE,&old)) {po_stage="page";return 0;}
    for(unsigned i=0;i<PO_TARGETS;i++) po_write_stub(po_page+PO_STUB*i,i);
    if(!VirtualProtect(po_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {po_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),po_page,0x1000);
    po_picks=po_moved=po_small=0;po_rows=0;po_failures=0;po_restored=0;
    po_mode=mode;po_installed=1;
    for(unsigned i=0;i<PO_TARGETS;i++) {
        unsigned char patch[5]={0xe9};int32_t d=po_rel(i);memcpy(patch+1,&d,4);
        if(!view_cas_bytes(po_targets[i].rva,po_targets[i].orig,patch,5)) {po_stage="cas";po_restore();po_installed=0;po_mode=PO_OFF;return 0;}
        po_done|=1u<<i;
    }
    po_stage="armed";return 1;
}
static int po_owned(void) {
    for(unsigned i=0;i<PO_TARGETS;i++) if((po_done>>i)&1u) {
        unsigned char want[PO_STUB];po_write_stub(want,i);
        if(po_state_of(i)!=1 || memcmp(po_page+PO_STUB*i,want,PO_STUB)) return 0;
    }
    return 1;
}
static int po_check(void) {
    if(InterlockedCompareExchange64(&po_failures,0,0)) return 0;
    const char *why=!po_owned()?"site_changed":!po_pins()?"block_changed":NULL;
    if(why) {po_stage=why;InterlockedIncrement64(&po_failures);return 0;}
    return 1;
}
static int po_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&po_rows,0,0);int n;
    if(!po_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)PO_RING && index<(uint64_t)(count-(LONG64)PO_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const po_row_t *r=&po_ring[index%PO_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    n=snprintf(out,cap,"index=%llu tick=%llu fn=%x caller=%x size=%u mode=%d total=%u draw=%u native=%d canon=%d moved=%u",(unsigned long long)index,(unsigned long long)r->tick,
        po_targets[r->target<PO_TARGETS?r->target:0].rva,r->caller,r->size,r->mode,r->total,r->draw,(int)r->native,(int)r->canon,(unsigned)r->moved);
    return n>0 && (size_t)n<cap?n:0;
}
static int po_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","observe","canonical"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu picks=%llu small=%llu moved=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld",po_installed,modes[po_mode>=0 && po_mode<3?po_mode:0],
        (unsigned long long)stats.issued,(unsigned long long)po_picks,(unsigned long long)po_small,(unsigned long long)po_moved,(long long)po_rows,(unsigned long long)po_from,(unsigned long long)po_to,po_restored,po_stage,(long long)po_failures);
    return n>0 && (size_t)n<cap?n:0;
}
