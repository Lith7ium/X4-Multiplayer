/* Determinism fix, DEFAULT OFF: what a station wants to restock does not depend
 * on where the macros happen to sit in this process's heap
 * (native_gate_loadout_order = 'observe' | 'canonical', with
 * native_gate_loadout_order_rows = {from, to}: ticks, at most 600, needs the
 * world identities).
 *
 * U::Container::GetWantedSupplyOrders 0x429ad0 keeps no target: every call
 * recomputes what the station should carry from its macro and modules (ammunition
 * 0x505730, deployables 0x5046b0, units 0x503c40, all with the range {-1, 0} at
 * 0x31be918, so no random fill) and subtracts the stock (+0x510, a map by macro
 * pointer). Each generator returns a map keyed by the macro's address, and that
 * address order decides results in two places:
 *  - the division of a module's unit or deployable allowance by the weights
 *    truncates, and 0x502ee0 (loop at 0x503349) and 0x504080 (0x5043ce) hand out
 *    the rest one by one in map order: the macro with the lowest address gets it;
 *  - 0x429ad0 walks the map in that order to build the wanted list and to spend
 *    the free capacity, and the listener 0x42b900 draws an index into that list.
 * Heap addresses are per process (SMem::Allocate 0x14b7140 goes through a
 * per-thread allocator, TLS+0x348), so two clients with the same save can
 * disagree about which drone macro is lower. The expected case, from static
 * reading and the rows of full-W187/R187, to be confirmed by 'observe': station
 * 3bccb holds 76 units and wants cargo drone +1, repair drone -1, the same total
 * with one unit on the other macro, on the first evaluation since the load (trade
 * state -1 at tick 187), with nothing changed before it. On the other client
 * the list is empty and the branch at 0x407df0 is not taken; at 188 the listener
 * removes the repair drone on one client only (a3=1: slot 0x2268).
 *
 * 'observe' changes nothing and keeps rows in the window: one LOADOUT_ROW per
 * generator return (kind=out: target/stock and macro address of up to four
 * macros, in the native map order) and per non-zero rest (kind=remainder: the
 * rest, its macros in native order with their counts before it, moved=1 when
 * the canonical order would hand it to other macros). Two clients' rows show the order that differs.
 * 'canonical' fixes the order to the macro id, a choice any process could have
 * made by chance: the rest loops are replaced (E9 to a stub, same arithmetic, the
 * nodes in id order), the generators' result maps are relinked as a chain in id
 * order before 0x429ad0 walks them (they are local, never searched again and
 * freed by their node chunks), and the same for what feeds them: the ware list of
 * 0x429ad0's first pass (sorted by address at 0x429c64), the map of existing
 * users 0x504970 walks (0x505ef8) and the candidates of its random pick
 * (0x504e20). Calls go through stubs (jmp [rip]) at their six sites. Pins: the 32
 * bytes around each call with the original displacement, the whole of each rest
 * loop. Written by CAS at arm (before the snapshot load), re-checked every gate
 * tick (stop reason 172), restored at stop. An order that cannot be established
 * (more than 64 nodes, a macro without a readable id) stops the gate in
 * 'canonical' rather than run on the native order. */
#define LL_CALLS 6u
#define LL_DETOURS 2u
#define LL_STOP_REASON 172u /* 170 UI lazy, 171 supply wants */
#define LL_RING 8192u
#define LL_SHOWN 4u
#define LL_NODES 64u
#define LL_NAME 96u
#define LL_ROW_NAME 40u
#define LL_STUB 0x20u
#define LL_DETOUR_BASE 0x100u
#define LL_DETOUR_STUB 0x40u
#define LL_PIN_MAX 0x100u
enum {LL_OFF=0,LL_OBSERVE=1,LL_CANONICAL=2};
enum {LL_WARES=0,LL_AMMO=1,LL_DEPLOYABLE=2,LL_UNIT=3,LL_EXISTING=4,LL_PICK=5};
enum {LL_KIND_OUT=0,LL_KIND_REMAINDER=1};
/* LL_OWN_PLATFORM (gate_loadout_order.h on Linux) defines ll_calls and ll_detours with the same fields, its macro
 * name, stock and ware list readers and everything from the stubs to the per-tick check; the rest is shared. */
#ifndef LL_OWN_PLATFORM
static const struct {uint32_t site,target;const char *sha,*name;} ll_calls[LL_CALLS]={
    {0x429c64u,0x316f60u,"824dd2eea992444cbb1b8aa05564f34627e1359548b24cecc4571562e3de02a0","wares"},
    {0x42a40eu,0x505730u,"1cb70eae526272a4dda0045d976ac6393637d930682ef3e7657788788e3e2899","ammo"},
    {0x42a6eeu,0x5046b0u,"4c25ab4e0059b68fefd46ad2afd30dd28e00199074372c9ea197cc1774b6457f","deployable"},
    {0x42a9a8u,0x503c40u,"9c874524f1b4e146145bf9c38bcd7c252acfca4b73c7435c6ddd4692712d2142","unit"},
    {0x505ef8u,0x504970u,"f663ae604c41fb681bf47edc162293ce39bf852cc645b3e6ec59a8925313bd17","existing"},
    {0x504e20u,0x5012f0u,"a1f0319aea4b671dd0482eb0109725d6daaa3c15a8bddff39c152df63bbe8d93","pick"},
};
static const struct {uint32_t site,exit;const char *sha;unsigned char orig[5];const char *name;} ll_detours[LL_DETOURS]={
    {0x503349u,0x5033e4u,"f9b809017723f6d34fe3e7ac4cab2fb0a101bbda159fdc3f0e06dee636b1891c",{0x49,0x8b,0x4a,0x28,0x48},"remainder"},
    {0x5043ceu,0x504464u,"11be163bb0f4d421389849cea464493ac0044704854d5de8cecf0c37be875de0",{0x4c,0x8b,0x5d,0x57,0x49},"remainder2"},
};
#endif
typedef uint64_t (*ll_fn)(void *,void *,void *,void *,void *,void *,void *,void *);
typedef struct {volatile LONG64 ready;uint64_t tick,key,ptr[LL_SHOWN];int32_t amount[LL_SHOWN],stock[LL_SHOWN];uint32_t count,todo,rem;
    char name[LL_SHOWN][LL_ROW_NAME];uint8_t site,klass,kind,moved;} ll_row_t;
static int ll_mode,ll_installed,ll_restored;
static unsigned ll_done;
static unsigned char *ll_page;
static ll_row_t *ll_ring;
static volatile LONG64 ll_rows,ll_failures;
static uint64_t ll_from,ll_to,ll_count[LL_CALLS],ll_rests,ll_moved,ll_relinked;
static const char *ll_stage="idle";
static ll_fn ll_native[LL_CALLS]; /* tests substitute */
static _Thread_local const void *ll_current; /* the container the generator on this thread works for */
static void ll_fail(const char *why) {ll_stage=why;InterlockedIncrement64(&ll_failures);}
/* The macro id: MSVC std::string at +0x20 (size +0x10, capacity +0x18). 0 when unreadable. */
#ifndef LL_OWN_PLATFORM
static size_t ll_macro_name(const void *macro,char *out,size_t cap) {
    unsigned char s[0x20];uint64_t size,capacity,p;out[0]=0;
    if(!macro || !read_local((const unsigned char*)macro+0x20,s,sizeof(s))) return 0;
    memcpy(&size,s+0x10,8);memcpy(&capacity,s+0x18,8);memcpy(&p,s,8);
    if(!size || capacity<size) return 0;
    size_t n=size<cap-1?(size_t)size:cap-1;
    if(capacity<16) {if(size>15) return 0;memcpy(out,s,n);}
    else if(!read_local((const void*)(uintptr_t)p,out,n)) {out[0]=0;return 0;}
    out[n]=0;return n;
}
#else
static size_t ll_macro_name(const void *macro,char *out,size_t cap);
#endif
/* index := names in ascending order; 0 for an empty name or a tie. */
static int ll_sorted(char names[][LL_NAME],unsigned count,unsigned char *index) {
    for(unsigned i=0;i<count;i++) {
        unsigned char at=(unsigned char)i;unsigned k=i;
        while(k && strcmp(names[index[k-1]],names[at])>0) {index[k]=index[k-1];k--;}
        index[k]=at;
    }
    for(unsigned i=0;i<count;i++) if(!names[index[i]][0] || (i && !strcmp(names[index[i-1]],names[index[i]]))) return 0;
    return 1;
}
/* The game's map: node {parent, left, right, _, key +0x20}, header {self, root, ..., first +0x20, size +0x28}. */
static unsigned char *ll_next(const unsigned char *node) {
    unsigned char *right=*(unsigned char *const*)(node+0x10);
    if(right) {
        for(unsigned char *left;(left=*(unsigned char **)(right+8))!=NULL;) right=left;
        return right;
    }
    unsigned char *parent=*(unsigned char *const*)node;
    while(*(const unsigned char *const*)(parent+8)!=node) {node=parent;parent=*(unsigned char **)parent;}
    return parent;
}
/* The nodes in map order; UINT_MAX when they do not fit or the walk does not match the size. */
static unsigned ll_nodes(unsigned char *map,unsigned char **nodes) {
    uint64_t size=*(uint64_t*)(map+0x28);if(size>LL_NODES) return 0xffffffffu;
    unsigned k=0;unsigned char *n=*(unsigned char **)(map+0x20);
    while(n!=map) {if(k>=LL_NODES) return 0xffffffffu;nodes[k++]=n;n=ll_next(n);}
    return k==size?k:0xffffffffu;
}
static void ll_relink(unsigned char *map,unsigned char **nodes,const unsigned char *index,unsigned count) {
    for(unsigned i=0;i<count;i++) {
        unsigned char *n=nodes[index[i]];
        *(unsigned char **)n=i?nodes[index[i-1]]:map;*(unsigned char **)(n+8)=NULL;*(unsigned char **)(n+0x10)=i+1<count?nodes[index[i+1]]:NULL;
    }
    *(unsigned char **)(map+8)=count?nodes[index[0]]:NULL;*(unsigned char **)(map+0x20)=count?nodes[index[0]]:map;
}
/* A result map or the existing-users map as a chain in id order (the second key, when set, breaks the tie). */
static void ll_canonical_map(unsigned char *map,int second_key) {
    unsigned char *nodes[LL_NODES],index[LL_NODES];char names[LL_NODES][LL_NAME];
    unsigned count=ll_nodes(map,nodes);
    if(count==0xffffffffu) {ll_fail("nodes");return;}
    if(count<2) return;
    for(unsigned i=0;i<count;i++) {
        size_t n=ll_macro_name(*(const void **)(nodes[i]+0x20),names[i],LL_NAME);
        if(n && second_key && n+2<LL_NAME) {names[i][n]='/';ll_macro_name(*(const void **)(nodes[i]+0x28),names[i]+n+1,LL_NAME-n-1);}
    }
    if(!ll_sorted(names,count,index)) {ll_fail("name");return;}
    ll_relink(map,nodes,index,count);++ll_relinked;
}
static int ll_in_window(void) {return ll_ring && ll_to && stats.active && boot_phase==3 && stats.issued>=ll_from && stats.issued<=ll_to;}
static ll_row_t *ll_claim(LONG64 *index,const void *container,unsigned site,unsigned kind) {
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(container && read_local((const unsigned char*)container+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    *index=InterlockedIncrement64(&ll_rows)-1;ll_row_t *r=&ll_ring[(uint64_t)*index%LL_RING];
    InterlockedExchange64(&r->ready,-1);
    memset(r->ptr,0,sizeof(r->ptr));memset(r->amount,0,sizeof(r->amount));memset(r->stock,0,sizeof(r->stock));memset(r->name,0,sizeof(r->name));
    r->tick=stats.issued;r->key=key;r->site=(uint8_t)site;r->klass=(uint8_t)klass;r->kind=(uint8_t)kind;r->moved=0;r->count=0;r->todo=0;r->rem=0;
    return r;
}
/* The station's stock of a macro: the map at +0x510, count at +0x28 of the node. */
#ifndef LL_OWN_PLATFORM
static int32_t ll_stock(const unsigned char *container,const void *macro) {
    const unsigned char *map=container+0x510,*best=map,*n=*(const unsigned char *const*)(map+8);
    while(n) {
        if(*(const uintptr_t*)(n+0x20)<(uintptr_t)macro) n=*(const unsigned char *const*)(n+0x10);
        else {best=n;n=*(const unsigned char *const*)(n+8);}
    }
    return best!=map && *(const uintptr_t*)(best+0x20)==(uintptr_t)macro?*(const int32_t*)(best+0x28):0;
}
#else
static int32_t ll_stock(const unsigned char *container,const void *macro);
#endif
/* After a generator, before any relinking: the target against the stock, in native order. */
static void ll_note_out(unsigned site,const unsigned char *container,unsigned char *map) {
    if(!ll_in_window()) return;
    unsigned char *nodes[LL_NODES];unsigned count=ll_nodes(map,nodes);if(count==0xffffffffu) return;
    LONG64 index;ll_row_t *r=ll_claim(&index,container,site,LL_KIND_OUT);r->count=count;
    for(unsigned i=0;i<count && i<LL_SHOWN;i++) {
        const void *macro=*(const void **)(nodes[i]+0x20);r->ptr[i]=(uint64_t)(uintptr_t)macro;
        r->amount[i]=*(const int32_t*)(nodes[i]+(site==LL_AMMO?0x28:0x30));r->stock[i]=container?ll_stock(container,macro):0;
        ll_macro_name(macro,r->name[i],LL_ROW_NAME);
    }
    InterlockedExchange64(&r->ready,index);
}
static void ll_note_rest(unsigned detour,unsigned char **nodes,unsigned count,uint32_t todo,uint32_t rem,int moved) {
    if(!ll_in_window()) return;
    LONG64 index;ll_row_t *r=ll_claim(&index,ll_current,LL_CALLS+detour,LL_KIND_REMAINDER);r->count=count;r->todo=todo;r->rem=rem;r->moved=(uint8_t)moved;
    for(unsigned i=0;i<count && i<LL_SHOWN;i++) {
        const void *macro=*(const void **)(nodes[i]+0x20);r->ptr[i]=(uint64_t)(uintptr_t)macro;r->amount[i]=*(const int32_t*)(nodes[i]+0x30);
        ll_macro_name(macro,r->name[i],LL_ROW_NAME);
    }
    InterlockedExchange64(&r->ready,index);
}
/* The native rest loop (0x503356 / 0x5043df), with the order chosen here; returns what is left of `left`.
 * Native: q = todo / size to each node, the first todo % size nodes one more, until todo is used up. */
static uint32_t ll_rest(unsigned char *map,uint32_t todo,uint32_t left,uint32_t detour) {
    uint64_t size=*(uint64_t*)(map+0x28);
    if(!size) return left;
    uint32_t q=todo/(uint32_t)size,rem=todo%(uint32_t)size;
    unsigned char *nodes[LL_NODES],*order[LL_NODES];unsigned count=ll_nodes(map,nodes);
    if(count==0xffffffffu) {
        if(ll_mode==LL_CANONICAL) ll_fail("nodes");
        for(unsigned char *n=*(unsigned char **)(map+0x20);n!=map && todo;n=ll_next(n)) {
            if(q) {*(uint32_t*)(n+0x30)+=q;todo-=q;left-=q;}
            if(rem && todo) {++*(uint32_t*)(n+0x30);--rem;--todo;--left;}
        }
        return left;
    }
    for(unsigned i=0;i<count;i++) order[i]=nodes[i];
    if(rem) {
        ++ll_rests;
        char names[LL_NODES][LL_NAME];unsigned char index[LL_NODES];int moved=0;
        for(unsigned i=0;i<count;i++) ll_macro_name(*(const void **)(nodes[i]+0x20),names[i],LL_NAME);
        if(ll_sorted(names,count,index)) {
            /* Would the canonical order give the rest to other macros? */
            for(unsigned i=0;i<rem && !moved;i++) {
                int found=0;for(unsigned k=0;k<rem;k++) if(index[k]==i) found=1;
                moved=!found;
            }
            if(moved) ++ll_moved;
            ll_note_rest(detour,nodes,count,todo,rem,moved);
            if(ll_mode==LL_CANONICAL) for(unsigned i=0;i<count;i++) order[i]=nodes[index[i]];
        } else {
            ll_note_rest(detour,nodes,count,todo,rem,0);
            if(ll_mode==LL_CANONICAL) ll_fail("name");
        }
    }
    for(unsigned i=0;i<count;i++) {
        unsigned char *n=order[i];
        if(q) {*(uint32_t*)(n+0x30)+=q;todo-=q;left-=q;}
        if(rem) {
            if(!todo) break;
            ++*(uint32_t*)(n+0x30);--rem;--todo;--left;
        }
        if(!todo) break;
    }
    return left;
}
static uint64_t ll_forward(unsigned i,void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    ll_fn fn=ll_native[i]?ll_native[i]:(ll_fn)(void *)(image+ll_calls[i].target);
    return fn(a,b,c,d,e,f,g,h);
}
/* 0x316f60 sorts and uniques the ware list by address: a small vector, inline capacity 4, count +0x28. */
#ifndef LL_OWN_PLATFORM
static void ll_canonical_wares(unsigned char *vec) {
    uint64_t capacity=*(uint64_t*)(vec+0x20),count=*(uint64_t*)(vec+0x28);
    if(count<2) return;
    if(count>LL_NODES) {ll_fail("nodes");return;}
    uint64_t *data=capacity==4?(uint64_t*)vec:*(uint64_t**)vec,copy[LL_NODES];
    char names[LL_NODES][LL_NAME];unsigned char index[LL_NODES];
    for(unsigned i=0;i<count;i++) {
        copy[i]=data[i];sw_ware_id_cap((const void*)(uintptr_t)data[i],names[i],LL_NAME);
        if(!strcmp(names[i],"?")) names[i][0]=0;
    }
    if(!ll_sorted(names,(unsigned)count,index)) {ll_fail("name");return;}
    for(unsigned i=0;i<count;i++) data[i]=copy[index[i]];
}
#else
static void ll_canonical_wares(unsigned char *vec);
#endif
static uint64_t ll_wares_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    uint64_t result=ll_forward(LL_WARES,a,b,c,d,e,f,g,h);++ll_count[LL_WARES];
    if(ll_mode==LL_CANONICAL) ll_canonical_wares(a);
    return result;
}
/* Generator (container, result map, ...): the row, then the result in id order. */
static __attribute__((noinline)) uint64_t ll_generate(unsigned i,void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    const void *saved=ll_current;ll_current=a;
    uint64_t result=ll_forward(i,a,b,c,d,e,f,g,h);ll_current=saved;++ll_count[i];
    ll_note_out(i,a,b);
    if(ll_mode==LL_CANONICAL) ll_canonical_map(b,0);
    return result;
}
static uint64_t ll_ammo_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {return ll_generate(LL_AMMO,a,b,c,d,e,f,g,h);}
static uint64_t ll_deployable_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {return ll_generate(LL_DEPLOYABLE,a,b,c,d,e,f,g,h);}
static uint64_t ll_unit_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {return ll_generate(LL_UNIT,a,b,c,d,e,f,g,h);}
/* 0x504970(result, existing users, ...): it only walks the second map, keyed by macro and an optional macro at +0x28. */
static uint64_t ll_existing_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    ++ll_count[LL_EXISTING];
    if(ll_mode==LL_CANONICAL) ll_canonical_map(b,1);
    return ll_forward(LL_EXISTING,a,b,c,d,e,f,g,h);
}
/* 0x5012f0(candidates, ...): std::vector of 0x28-byte entries {ware, macro, ...}, built from a map by address.
 * NULL when sorted by macro id, else why not. */
static const char *ll_sort_candidates(unsigned char *vec) {
    unsigned char *begin=*(unsigned char **)vec,*end=*(unsigned char **)(vec+8);
    size_t count=(size_t)(end-begin)/0x28u;
    if(count<2) return NULL;
    if(count>LL_NODES) return "nodes";
    char names[LL_NODES][LL_NAME];unsigned char index[LL_NODES],copy[LL_NODES][0x28];
    for(size_t i=0;i<count;i++) {memcpy(copy[i],begin+i*0x28u,0x28);ll_macro_name(*(const void **)(begin+i*0x28u+8),names[i],LL_NAME);}
    if(!ll_sorted(names,(unsigned)count,index)) return "name";
    for(size_t i=0;i<count;i++) memcpy(begin+i*0x28u,copy[index[i]],0x28);
    return NULL;
}
static void ll_canonical_candidates(unsigned char *vec) {const char *why=ll_sort_candidates(vec);if(why) ll_fail(why);}
static uint64_t ll_pick_entry(void *a,void *b,void *c,void *d,void *e,void *f,void *g,void *h) {
    ++ll_count[LL_PICK];
    if(ll_mode==LL_CANONICAL) ll_canonical_candidates(a);
    return ll_forward(LL_PICK,a,b,c,d,e,f,g,h);
}
static const ll_fn ll_entries[LL_CALLS]={ll_wares_entry,ll_ammo_entry,ll_deployable_entry,ll_unit_entry,ll_existing_entry,ll_pick_entry};
#ifndef LL_OWN_PLATFORM
static void ll_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};void *t=(void*)ll_entries[i];memcpy(p,code,6);memcpy(p+6,&t,8);
}
/* Reached by jmp at the start of the rest loop, stack aligned as there:
 * 0x503349: map r10, todo ebx, left r11d (read back at 0x5033e4);
 * 0x5043ce: map [rbp+0x57], todo and left r10d (read back at 0x504464).
 * The loops' volatile registers are not used after them. */
static void ll_write_detour(unsigned char *p,unsigned j,const void *exit) {
    static const unsigned char head0[8]={0x4c,0x89,0xd1,0x89,0xda,0x45,0x89,0xd8}; /* mov rcx,r10; mov edx,ebx; mov r8d,r11d */
    static const unsigned char head1[10]={0x48,0x8b,0x4d,0x57,0x44,0x89,0xd2,0x45,0x89,0xd0}; /* mov rcx,[rbp+0x57]; mov edx,r10d; mov r8d,r10d */
    unsigned n=0;int32_t d;void *entry=(void*)ll_rest;
    memset(p,0xcc,LL_DETOUR_STUB);
    if(j==0) {memcpy(p,head0,sizeof(head0));n=sizeof(head0);} else {memcpy(p,head1,sizeof(head1));n=sizeof(head1);}
    p[n++]=0x41;p[n++]=0xb9;p[n++]=(unsigned char)j;p[n++]=0;p[n++]=0;p[n++]=0; /* mov r9d,j */
    p[n++]=0x48;p[n++]=0x83;p[n++]=0xec;p[n++]=0x20; /* sub rsp,0x20 */
    p[n++]=0xff;p[n++]=0x15;d=(int32_t)(0x30-(n+4));memcpy(p+n,&d,4);n+=4; /* call [rip] -> +0x30 */
    p[n++]=0x48;p[n++]=0x83;p[n++]=0xc4;p[n++]=0x20; /* add rsp,0x20 */
    p[n++]=0x41;p[n++]=0x89;p[n++]=j==0?0xc3:0xc2; /* mov r11d/r10d,eax */
    p[n++]=0xff;p[n++]=0x25;d=(int32_t)(0x38-(n+4));memcpy(p+n,&d,4);n+=4; /* jmp [rip] -> +0x38 */
    memcpy(p+0x30,&entry,8);memcpy(p+0x38,&exit,8);
}
static unsigned char *ll_detour_stub(unsigned j) {return ll_page+LL_DETOUR_BASE+LL_DETOUR_STUB*j;}
static int32_t ll_call_rel(unsigned i,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+ll_calls[i].site+5));}
static int32_t ll_detour_rel(unsigned j) {return (int32_t)((intptr_t)ll_detour_stub(j)-(intptr_t)(image+ll_detours[j].site+5));}
/* 0 original, 1 ours, -1 anything else. */
static int ll_call_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+ll_calls[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==ll_call_rel(i,image+ll_calls[i].target)) return 0;
    if(ll_page && d==ll_call_rel(i,ll_page+LL_STUB*i)) return 1;
    return -1;
}
static int ll_detour_state(unsigned j) {
    unsigned char now[5];if(!read_local(image+ll_detours[j].site,now,5)) return -1;
    if(!memcmp(now,ll_detours[j].orig,5)) return 0;
    if(ll_page && now[0]==0xe9) {int32_t d;memcpy(&d,now+1,4);if(d==ll_detour_rel(j)) return 1;}
    return -1;
}
static int ll_pins(void) {
    for(unsigned i=0;i<LL_CALLS;i++) {
        unsigned char copy[0x20];
        if(ll_call_state(i)<0 || !read_local(image+ll_calls[i].site-0x10,copy,sizeof(copy))) return 0;
        int32_t d=ll_call_rel(i,image+ll_calls[i].target);memcpy(copy+0x11,&d,4);
        if(!hash_ok(copy,sizeof(copy),ll_calls[i].sha)) return 0;
    }
    for(unsigned j=0;j<LL_DETOURS;j++) {
        unsigned char copy[LL_PIN_MAX];unsigned size=ll_detours[j].exit-ll_detours[j].site;
        if(ll_detour_state(j)<0 || size>sizeof(copy) || !read_local(image+ll_detours[j].site,copy,size)) return 0;
        memcpy(copy,ll_detours[j].orig,5);
        if(!hash_ok(copy,size,ll_detours[j].sha)) return 0;
    }
    return 1;
}
static unsigned char *ll_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x480000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned i=0;i<LL_CALLS && ok;i++) {intptr_t d=(intptr_t)p-(intptr_t)(image+ll_calls[i].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            for(unsigned j=0;j<LL_DETOURS && ok;j++) {intptr_t d=(intptr_t)p-(intptr_t)(image+ll_detours[j].site+5);ok=d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int ll_call_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(ll_calls[i].site+1,e,v,4);
}
static void ll_restore(void) {
    if(!ll_installed) return;
    for(unsigned i=0;i<LL_CALLS;i++)
        if((ll_done>>i)&1u && ll_call_state(i)==1 && ll_call_cas(i,ll_call_rel(i,ll_page+LL_STUB*i),ll_call_rel(i,image+ll_calls[i].target))) ll_done&=~(1u<<i);
    for(unsigned j=0;j<LL_DETOURS;j++) {
        if(!((ll_done>>(LL_CALLS+j))&1u) || ll_detour_state(j)!=1) continue;
        unsigned char now[5];memcpy(now,image+ll_detours[j].site,5);
        if(view_cas_bytes(ll_detours[j].site,now,ll_detours[j].orig,5)) ll_done&=~(1u<<(LL_CALLS+j));
    }
    if(!ll_done) {ll_installed=0;ll_restored=1;ll_mode=LL_OFF;}
}
#endif
/* The row window; a configuration rerun with the same window is accepted. Before arming. */
static int ll_window(uint64_t from,uint64_t to) {
    if(ll_installed) return from==ll_from && to==ll_to;
    if(to && (from<1 || to<from || to-from>600)) {ll_stage="window";return 0;}
    ll_from=from;ll_to=to;return 1;
}
#ifndef LL_OWN_PLATFORM
static void ll_write_code(void) {
    for(unsigned i=0;i<LL_CALLS;i++) ll_write_stub(ll_page+LL_STUB*i,i);
    for(unsigned j=0;j<LL_DETOURS;j++) ll_write_detour(ll_detour_stub(j),j,image+ll_detours[j].exit);
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int ll_arm(int mode) {
    if(ll_installed) return stats.active && ll_mode==mode;
    if(mode!=LL_OBSERVE && mode!=LL_CANONICAL) {ll_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(ll_to && !world_identity_installed) {ll_stage="needs";return 0;}
    for(unsigned i=0;i<LL_CALLS;i++) if(ll_call_state(i)!=0) {ll_stage="pins";return 0;}
    for(unsigned j=0;j<LL_DETOURS;j++) if(ll_detour_state(j)!=0) {ll_stage="pins";return 0;}
    if(!ll_pins()) {ll_stage="pins";return 0;}
    if(!ll_ring) ll_ring=VirtualAlloc(NULL,sizeof(ll_row_t)*LL_RING,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!ll_page) ll_page=ll_alloc_page();
    if(!ll_ring || !ll_page) {ll_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(ll_page,0x1000,PAGE_READWRITE,&old)) {ll_stage="page";return 0;}
    ll_write_code();
    if(!VirtualProtect(ll_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {ll_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ll_page,0x1000);
    memset(ll_count,0,sizeof(ll_count));ll_rests=ll_moved=ll_relinked=0;ll_rows=0;ll_failures=0;ll_restored=0;
    ll_mode=mode;ll_installed=1;
    for(unsigned i=0;i<LL_CALLS;i++) {
        if(!ll_call_cas(i,ll_call_rel(i,image+ll_calls[i].target),ll_call_rel(i,ll_page+LL_STUB*i))) {ll_stage="cas";ll_restore();ll_installed=0;ll_mode=LL_OFF;return 0;}
        ll_done|=1u<<i;
    }
    for(unsigned j=0;j<LL_DETOURS;j++) {
        unsigned char patch[5]={0xe9};int32_t d=ll_detour_rel(j);memcpy(patch+1,&d,4);
        if(!view_cas_bytes(ll_detours[j].site,ll_detours[j].orig,patch,5)) {ll_stage="cas";ll_restore();ll_installed=0;ll_mode=LL_OFF;return 0;}
        ll_done|=1u<<(LL_CALLS+j);
    }
    ll_stage="armed";return 1;
}
static int ll_owned(void) {
    for(unsigned i=0;i<LL_CALLS;i++) if((ll_done>>i)&1u) {
        unsigned char want[14];ll_write_stub(want,i);
        if(ll_call_state(i)!=1 || memcmp(ll_page+LL_STUB*i,want,14)) return 0;
    }
    for(unsigned j=0;j<LL_DETOURS;j++) if((ll_done>>(LL_CALLS+j))&1u) {
        unsigned char want[LL_DETOUR_STUB];ll_write_detour(want,j,image+ll_detours[j].exit);
        if(ll_detour_state(j)!=1 || memcmp(ll_detour_stub(j),want,LL_DETOUR_STUB)) return 0;
    }
    return 1;
}
static int ll_check(void) {
    if(InterlockedCompareExchange64(&ll_failures,0,0)) return 0;
    const char *why=!ll_owned()?"site_changed":!ll_pins()?"block_changed":NULL;
    if(why) {ll_stage=why;InterlockedIncrement64(&ll_failures);return 0;}
    return 1;
}
#endif
static const char *ll_site_name(unsigned site) {
    return site<LL_CALLS?ll_calls[site].name:site<LL_CALLS+LL_DETOURS?ll_detours[site-LL_CALLS].name:"?";
}
static int ll_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ll_rows,0,0);int n;
    if(!ll_ring || (LONG64)index>=count) return 0;
    if(count>(LONG64)LL_RING && index<(uint64_t)(count-(LONG64)LL_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ll_row_t *r=&ll_ring[index%LL_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const classes[3]={"saved","created","none"};
    n=snprintf(out,cap,"index=%llu tick=%llu kind=%s site=%s class=%s key=%016llx count=%u todo=%u rem=%u moved=%u macros=",
        (unsigned long long)index,(unsigned long long)r->tick,r->kind==LL_KIND_OUT?"out":"remainder",ll_site_name(r->site),classes[r->klass<3?r->klass:2],
        (unsigned long long)r->key,r->count,r->todo,r->rem,(unsigned)r->moved);
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned i=0;i<LL_SHOWN && i<r->count;i++) {
        int m=r->kind==LL_KIND_OUT?
            snprintf(out+n,cap-(size_t)n,"%s%s:%d/%d@%llx",i?",":"",r->name[i][0]?r->name[i]:"?",r->amount[i],r->stock[i],(unsigned long long)r->ptr[i]):
            snprintf(out+n,cap-(size_t)n,"%s%s:%d@%llx",i?",":"",r->name[i][0]?r->name[i]:"?",r->amount[i],(unsigned long long)r->ptr[i]);
        if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;
    }
    return n;
}
static int ll_status(char *out,size_t cap) {
    static const char *const modes[3]={"off","observe","canonical"};
    int n=snprintf(out,cap,"installed=%d mode=%s frame=%llu calls=%llu/%llu/%llu/%llu/%llu/%llu rests=%llu moved=%llu relinked=%llu rows=%lld window=%llu-%llu restored=%d stage=%s failures=%lld",
        ll_installed,modes[ll_mode>=0 && ll_mode<3?ll_mode:0],(unsigned long long)stats.issued,(unsigned long long)ll_count[0],(unsigned long long)ll_count[1],
        (unsigned long long)ll_count[2],(unsigned long long)ll_count[3],(unsigned long long)ll_count[4],(unsigned long long)ll_count[5],(unsigned long long)ll_rests,
        (unsigned long long)ll_moved,(unsigned long long)ll_relinked,(long long)ll_rows,(unsigned long long)ll_from,(unsigned long long)ll_to,ll_restored,ll_stage,(long long)ll_failures);
    return n>0 && (size_t)n<cap?n:0;
}
