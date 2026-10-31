/* find_* candidate lists on saved IDs on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_find_order.h, whose canonical order, rows and status are shared. Included by
 * gate_linux.c only, after the script RNG, whose scope names a row. The mechanism and the census
 * evidence are in that header.
 *
 * Linux sites: Scripts::FindComponentAction vtable 0x3b00a68, Execute 0xd87ad0 in slot 4 (Windows 3,
 * one more destructor), builds the candidate vector at rbp-0x40 and calls the result store 0x1d2b330
 * at 0xd87b33 with rdi the declaration, rsi the vector, rdx the Exec, rcx the component. The rel32
 * lies in the aligned 8-byte word 0xd87b30, so that word changes in one compare-exchange.
 *
 * Windows tail-jumps to the store so an observer behind it sees the native return address. No such
 * observer is ported, so this hook calls it. */
#define FIND_ORDER_OWN_PLATFORM
#define FIND_ORDER_SITE (LX_FIND_ORDER_SITE_VA-LX_IMAGE_BASE)
#define FIND_ORDER_ASSIGN (LX_FIND_ORDER_ASSIGN_VA-LX_IMAGE_BASE)
#define FIND_ORDER_THUNK 0x580u /* jmp [rip] -> find_order_assign */
#define __try
#define __except(filter) if(0)
#define LONG long /* the status prints the active word with %ld */
#define thunk_slot_ready(offset) 1
static int find_order_pinned(void) {return 1;} /* the gate's site pins, checked at start */
static int find_order_swap(int32_t expected,int32_t value);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_find_order.h"
#pragma GCC diagnostic pop
#undef __try
#undef __except
#undef LONG
#undef thunk_slot_ready
/* Two bytes of the word stay as they are: the end of mov rsi,r13 and the E8. */
static int find_order_swap(int32_t expected,int32_t value) {
    unsigned char *word=LX(LX_FIND_ORDER_SITE_VA&~7u);
    const unsigned at=(LX_FIND_ORDER_SITE_VA&7u)+1u;
    uint64_t before=rd64(word),after=before;int32_t rel;
    memcpy(&rel,(unsigned char*)&before+at,4);
    if(rel!=expected) return 0;
    memcpy((unsigned char*)&after+at,&value,4);
    return gate_code_swap64(word,before,after);
}
static __attribute__((noinline)) uintptr_t find_order_assign(void *declaration,void *vector,void *exec,void *component) {
    find_order_process((uintptr_t)__builtin_return_address(0),vector,read_local,world_identity_key);
    return ((find_assign_fn)(void*)LX(LX_FIND_ORDER_ASSIGN_VA))(declaration,vector,exec,component);
}
static void lxo_thunk_write(unsigned char *page) {lxw_jump_write(page+FIND_ORDER_THUNK,(void*)find_order_assign);}
static int lua_find_order(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mode) && (mode==FIND_ORDER_OBSERVE || mode==FIND_ORDER_SORT)?find_order_arm((int)mode):0);
    return 1;
}
static int lua_find_order_status(lua_State *L) {
    char out[768];int n=accept_ui()?find_order_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_find_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=find_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
