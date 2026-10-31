/* Stations' loadout targets and macro order on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_loadout_order.h ('observe' and 'canonical'), whose canonical order, rest arithmetic, rows and
 * status are shared. Included by gate_linux.c only. The mechanism and the Windows evidence are in that header.
 *
 * vps-linux-25L shows why it comes now. The two clients' due events are the same through tick 1092; at 1093 and
 * 1094 client 1 alone delivers U::SupplyWaresAddedEvent and two SupplyWaresRemovedEvents on saved station 0x1f2df,
 * the station of Windows' 1078 event, and the same follows on 0x1b2a0 (2855) and 0x1451a (2966). On Windows that
 * class is rooted in the heap order of drone macros and gone with 'canonical' (LOCKSTEP-CENSUS-20261028.txt).
 *
 * Linux sites, all read from the executable (pairs in tools/dev/lockstep_linux_known_pairs.json):
 * U::Container::GetWantedSupplyOrders 0x1839ea0 calls the ware sort 0xb03680 at 0x183a470 (rdi the list) and the
 * targets for deployables 0x17e89d0 at 0x183a537, units 0x17e86d0 at 0x183a6d7 and ammunition 0x17e8bd0 at
 * 0x183ac7f (rdi the container, rsi the result map, as on Windows; GCC lays the calls out in another order). The
 * ammunition target calls the existing-users walk 0x1def210 at 0x17e9063 (rsi its map), and that walk calls the
 * candidate pick 0x17b5f20 at 0x1defce1 (rdi the candidates: 0x28-byte entries, the macro at +8). Every rel32 lies
 * in one aligned 8-byte word, so one compare-exchange swaps it. None of these takes a float argument.
 * The maps are X4's own tree with Windows' layout: parent +0, left +8, right +0x10, key +0x20; the header's root
 * +8, first +0x20, size +0x28; the amount +0x30 (ammunition +0x28), the second key of the existing users +0x28.
 * The macro id is the libstdc++ string at +0x20 of a U::Macro (its name getter, slot 19, is checked). The ware
 * list is a small vector with an 8-byte head: capacity +0x28 (4 inline), the data inline at +8 or behind +8,
 * the count +0x30; a ware's id is its first libstdc++ string that reads as an identifier.
 *
 * The rests. Units: 0x17e86d0 -> 0x1df83c0 -> 0x1e9e3d0 (Windows 0x502ee0); deployables: 0x1df78e0 (Windows
 * 0x504080). Each first gives the rest to the macros that got nothing, in their input order (the loadout's own
 * order, not the heap's), and left native. What is left then, or all of it when there are no such macros, goes
 * to every node in map order: the loop at 0x1e9e7b0 (map [rsp+0x18], count r15d; leaves to 0x1e9e6b2 with rdi =
 * [rsp+0x60]) and the loop at 0x1df8193 (map [rsp+0x40], count [rsp+0x4c]; leaves to 0x1df8230, r14 kept). Each
 * is entered only at that address or just before it, the 5-byte instruction there lies inside an aligned 16
 * bytes, and nothing after the exits reads the count left. A jmp there leads to a stub on the thunk page that
 * calls ll_rest. test_gate_linux runs both native loops on the executable's bytes against the stubs.
 *
 * Not ported: the stock in the rows (prints 0), and the per-thread container of a rest row (one global here;
 * rows only). */
#define LL_OWN_PLATFORM
#define LXL_STUB 0x640u        /* six jmp [rip] -> ll_entries[i], 0x20 apart */
#define LXL_DETOUR 0x700u      /* two rest stubs, 0x40 apart */
static const struct {uint32_t site,target;const char *sha,*name;} ll_calls[]={
    {LX_LL_WARES_SITE_VA-LX_IMAGE_BASE,LX_LL_WARES_VA-LX_IMAGE_BASE,NULL,"wares"},
    {LX_LL_AMMO_SITE_VA-LX_IMAGE_BASE,LX_LL_AMMO_VA-LX_IMAGE_BASE,NULL,"ammo"},
    {LX_LL_DEPLOYABLE_SITE_VA-LX_IMAGE_BASE,LX_LL_DEPLOYABLE_VA-LX_IMAGE_BASE,NULL,"deployable"},
    {LX_LL_UNIT_SITE_VA-LX_IMAGE_BASE,LX_LL_UNIT_VA-LX_IMAGE_BASE,NULL,"unit"},
    {LX_LL_EXISTING_SITE_VA-LX_IMAGE_BASE,LX_LL_EXISTING_VA-LX_IMAGE_BASE,NULL,"existing"},
    {LX_LL_PICK_SITE_VA-LX_IMAGE_BASE,LX_LL_PICK_VA-LX_IMAGE_BASE,NULL,"pick"},
};
static const struct {uint32_t site,exit;const char *sha;unsigned char orig[5];const char *name;} ll_detours[]={
    {LX_LL_REST_UNIT_VA-LX_IMAGE_BASE,LX_LL_REST_UNIT_EXIT_VA-LX_IMAGE_BASE,NULL,{0x4c,0x8b,0x4c,0x24,0x18},"remainder"},           /* mov r9,[rsp+0x18] */
    {LX_LL_REST_DEPLOYABLE_VA-LX_IMAGE_BASE,LX_LL_REST_DEPLOYABLE_EXIT_VA-LX_IMAGE_BASE,NULL,{0x4c,0x8b,0x5c,0x24,0x40},"remainder2"}, /* mov r11,[rsp+0x40] */
};
#define _Thread_local /* no TLS in this module: ll_current is one global (rows only) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable" /* ll_page: Windows' code page; here the thunk page */
#include "lockstep_loadout_order.h"
#pragma GCC diagnostic pop
#undef _Thread_local
/* The macro id: a U::Macro's libstdc++ string at +0x20 (pointer, length +0x28). 0 when it is not one. */
static size_t ll_macro_name(const void *macro,char *out,size_t cap) {
    const unsigned char *vtable=NULL,*getter=NULL,*data=NULL;uint64_t length=0;out[0]=0;
    if(!macro || !read_local(macro,&vtable,8) || !vtable || !read_local(vtable+19*8,&getter,8) || getter!=LX(LX_MACRO_NAME_GETTER_VA) ||
       !read_local((const unsigned char*)macro+0x28,&length,8) || !length || !read_local((const unsigned char*)macro+0x20,&data,8) || !data) return 0;
    size_t n=length<cap-1?(size_t)length:cap-1;
    if(!read_local(data,out,n)) {out[0]=0;return 0;}
    out[n]=0;return n;
}
static int32_t ll_stock(const unsigned char *container,const void *macro) {(void)container;(void)macro;return 0;} /* not ported */
/* A ware's id: the first libstdc++ string in the ware (+0 .. +0x100) that reads as an identifier; "" when none. */
static void ll_ware_id(const void *ware,char *out,size_t room) {
    out[0]=0;
    unsigned char obj[0x100];if(!ware || !read_local(ware,obj,sizeof(obj))) return;
    for(unsigned at=0;at+0x20<=sizeof(obj);at+=8) {
        uint64_t p,size,capacity;memcpy(&p,obj+at,8);memcpy(&size,obj+at+8,8);memcpy(&capacity,obj+at+0x10,8);
        const int local=p==(uint64_t)(uintptr_t)ware+at+0x10;
        if(size<2 || size>63 || (local?size>15:capacity<size || capacity>4096)) continue;
        char text[64];
        if(local) memcpy(text,obj+at+0x10,(size_t)size);else if(!read_local((const void*)(uintptr_t)p,text,(size_t)size)) continue;
        int ok=1;for(uint64_t k=0;k<size && ok;k++) ok=(text[k]>='a' && text[k]<='z') || (text[k]>='0' && text[k]<='9') || text[k]=='_';
        if(!ok) continue;
        size_t n=size<room-1?(size_t)size:room-1;memcpy(out,text,n);out[n]=0;return;
    }
}
/* 0xb03680 sorts and uniques the ware list by address; afterwards, the same wares in id order. */
static void ll_canonical_wares(unsigned char *vec) {
    uint64_t capacity=*(uint64_t*)(vec+0x28),count=*(uint64_t*)(vec+0x30);
    if(count<2) return;
    if(count>LL_NODES) {ll_fail("nodes");return;}
    uint64_t *data=capacity==4?(uint64_t*)(vec+8):*(uint64_t**)(vec+8),copy[LL_NODES];
    char names[LL_NODES][LL_NAME];unsigned char index[LL_NODES];
    for(unsigned i=0;i<count;i++) {copy[i]=data[i];ll_ware_id((const void*)(uintptr_t)data[i],names[i],LL_NAME);}
    if(!ll_sorted(names,(unsigned)count,index)) {ll_fail("name");return;}
    for(unsigned i=0;i<count;i++) data[i]=copy[index[i]];
}
/* The rest stubs, entered by jmp with the loop's stack; ll_rest(map, count, count, j), then the loop's exit.
 * The stack is 16-byte aligned there in both functions (six pushes and 0x88 / 0x178). */
static void ll_write_detour(unsigned char *p,unsigned j,const void *exit) {
    static const unsigned char head0[13]={0x48,0x8b,0x7c,0x24,0x18,0x44,0x89,0xfe,0x44,0x89,0xfa,0x31,0xc9}; /* mov rdi,[rsp+0x18]; mov esi,r15d; mov edx,r15d; xor ecx,ecx */
    static const unsigned char head1[16]={0x48,0x8b,0x7c,0x24,0x40,0x8b,0x74,0x24,0x4c,0x89,0xf2,0xb9,0x01,0,0,0}; /* mov rdi,[rsp+0x40]; mov esi,[rsp+0x4c]; mov edx,esi; mov ecx,1 */
    unsigned n;int32_t d;void *entry=(void*)ll_rest;
    memset(p,0xcc,0x40);
    if(j==0) {memcpy(p,head0,sizeof(head0));n=sizeof(head0);} else {memcpy(p,head1,sizeof(head1));n=sizeof(head1);}
    p[n++]=0xff;p[n++]=0x15;d=(int32_t)(0x30-(n+4));memcpy(p+n,&d,4);n+=4; /* call [rip] -> +0x30 */
    if(j==0) {static const unsigned char reload[5]={0x48,0x8b,0x7c,0x24,0x60};memcpy(p+n,reload,5);n+=5;} /* mov rdi,[rsp+0x60] */
    p[n++]=0xff;p[n++]=0x25;d=(int32_t)(0x38-(n+4));memcpy(p+n,&d,4); /* jmp [rip] -> +0x38 */
    memcpy(p+0x30,&entry,8);memcpy(p+0x38,&exit,8);
}
/* The thunk page (gate_linux.c make_thunk): the six call stubs and the two rest stubs, used only when armed. */
static void lxl_thunk_write(unsigned char *page) {
    for(unsigned i=0;i<LL_CALLS;i++) lxw_jump_write(page+LXL_STUB+0x20u*i,(void*)ll_entries[i]);
    for(unsigned j=0;j<LL_DETOURS;j++) ll_write_detour(page+LXL_DETOUR+0x40u*j,j,LX(ll_detours[j].exit+LX_IMAGE_BASE));
}
static unsigned char *ll_site(unsigned i) {return LX(ll_calls[i].site+LX_IMAGE_BASE);}
static unsigned char *ll_detour_at(unsigned j) {return LX(ll_detours[j].site+LX_IMAGE_BASE);}
static int64_t ll_rel_to(const unsigned char *from_end,const void *target) {return (intptr_t)target-(intptr_t)from_end;}
/* 0 original, 1 ours, -1 anything else. */
static int ll_call_state(unsigned i) {
    const unsigned char *site=ll_site(i);int32_t d;
    if(site[0]!=0xe8) return -1;
    memcpy(&d,site+1,4);
    if(site+5+d==LX(ll_calls[i].target+LX_IMAGE_BASE)) return 0;
    if(thunk && site+5+d==thunk+LXL_STUB+0x20u*i) return 1;
    return -1;
}
static int ll_detour_state(unsigned j) {
    const unsigned char *at=ll_detour_at(j);int32_t d;
    if(!memcmp(at,ll_detours[j].orig,5)) return 0;
    if(thunk && at[0]==0xe9) {memcpy(&d,at+1,4);if(at+5+d==thunk+LXL_DETOUR+0x40u*j) return 1;}
    return -1;
}
/* The rel32 of a call: one aligned 8-byte word (checked offline for every site). */
static int ll_call_swap(unsigned i,const void *expected,const void *value) {
    unsigned char *site=ll_site(i),*word=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)7);
    const unsigned at=(unsigned)(site+1-word);
    int64_t e=ll_rel_to(site+5,expected),v=ll_rel_to(site+5,value);
    if(at>4 || e<INT32_MIN || e>INT32_MAX || v<INT32_MIN || v>INT32_MAX) return 0;
    int32_t e32=(int32_t)e,v32=(int32_t)v,now;
    uint64_t before=rd64(word),after=before;memcpy(&now,(unsigned char*)&before+at,4);
    if(now!=e32) return 0;
    memcpy((unsigned char*)&after+at,&v32,4);
    return gate_code_swap64(word,before,after);
}
/* The first 5 bytes of a rest loop: one aligned 16-byte block. */
static int ll_detour_swap(unsigned j,const unsigned char *expected,const unsigned char *value) {
    unsigned char *at=ll_detour_at(j),*block=(unsigned char*)((uintptr_t)at&~(uintptr_t)15);
    const unsigned offset=(unsigned)(at-block);
    unsigned __int128 current,desired;memcpy(&current,block,16);desired=current;
    if(offset+5>16 || memcmp((unsigned char*)&current+offset,expected,5)) return 0;
    memcpy((unsigned char*)&desired+offset,value,5);
    return gate_code_swap128(block,current,desired);
}
static int ll_detour_jump(unsigned j,unsigned char out[5]) {
    int64_t d=ll_rel_to(ll_detour_at(j)+5,thunk+LXL_DETOUR+0x40u*j);
    if(d<INT32_MIN || d>INT32_MAX) return 0;
    int32_t d32=(int32_t)d;out[0]=0xe9;memcpy(out+1,&d32,4);return 1;
}
static void ll_restore(void) {
    if(!ll_installed) return;
    for(unsigned i=0;i<LL_CALLS;i++)
        if((ll_done>>i)&1u && ll_call_state(i)==1 && ll_call_swap(i,thunk+LXL_STUB+0x20u*i,LX(ll_calls[i].target+LX_IMAGE_BASE))) ll_done&=~(1u<<i);
    for(unsigned j=0;j<LL_DETOURS;j++) {
        unsigned char jump[5];
        if(!((ll_done>>(LL_CALLS+j))&1u) || ll_detour_state(j)!=1 || !ll_detour_jump(j,jump)) continue;
        if(ll_detour_swap(j,jump,ll_detours[j].orig)) ll_done&=~(1u<<(LL_CALLS+j));
    }
    if(!ll_done) {ll_installed=0;ll_restored=1;ll_mode=LL_OFF;}
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted.
 * The gate's site pins (lx_sites) were checked at start. */
static int ll_arm(int mode) {
    if(ll_installed) return stats.active && ll_mode==mode;
    if(mode!=LL_OBSERVE && mode!=LL_CANONICAL) {ll_stage="mode";return 0;}
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!thunk) {ll_stage="page";return 0;}
    if(ll_to && !world_identity_installed) {ll_stage="needs";return 0;}
    for(unsigned i=0;i<LL_CALLS;i++) if(ll_call_state(i)!=0) {ll_stage="pins";return 0;}
    for(unsigned j=0;j<LL_DETOURS;j++) if(ll_detour_state(j)!=0) {ll_stage="pins";return 0;}
    if(!ll_ring) {
        void *ring=mmap(NULL,sizeof(ll_row_t)*LL_RING,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(ring==MAP_FAILED) {ll_stage="page";return 0;}
        ll_ring=ring;
    }
    memset(ll_count,0,sizeof(ll_count));ll_rests=ll_moved=ll_relinked=0;ll_rows=0;ll_failures=0;ll_restored=0;
    ll_mode=mode;ll_installed=1;
    for(unsigned i=0;i<LL_CALLS;i++) {
        if(!ll_call_swap(i,LX(ll_calls[i].target+LX_IMAGE_BASE),thunk+LXL_STUB+0x20u*i)) {ll_stage="cas";ll_restore();ll_installed=0;ll_mode=LL_OFF;return 0;}
        ll_done|=1u<<i;
    }
    for(unsigned j=0;j<LL_DETOURS;j++) {
        unsigned char jump[5];
        if(!ll_detour_jump(j,jump) || !ll_detour_swap(j,ll_detours[j].orig,jump)) {ll_stage="cas";ll_restore();ll_installed=0;ll_mode=LL_OFF;return 0;}
        ll_done|=1u<<(LL_CALLS+j);
    }
    ll_stage="armed";return 1;
}
/* The thunk page is r-x for good; the sites must still lead there. */
static int ll_owned(void) {
    for(unsigned i=0;i<LL_CALLS;i++) if((ll_done>>i)&1u && ll_call_state(i)!=1) return 0;
    for(unsigned j=0;j<LL_DETOURS;j++) if((ll_done>>(LL_CALLS+j))&1u && ll_detour_state(j)!=1) return 0;
    return 1;
}
static int ll_check(void) {
    if(InterlockedCompareExchange64(&ll_failures,0,0)) return 0;
    if(!ll_owned()) {ll_stage="site_changed";InterlockedIncrement64(&ll_failures);return 0;}
    return 1;
}
/* loadout_order(mode, from, to): 'observe' or 'canonical'; the row window in ticks is optional. */
static int lua_loadout_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==9 && !memcmp(mode,"canonical",9)?LL_CANONICAL:mode && length==7 && !memcmp(mode,"observe",7)?LL_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        ll_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && ll_arm(m));return 1;
}
static int lua_loadout_order_status(lua_State *L) {
    char out[384];int n=accept_ui()?ll_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_loadout_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=ll_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
