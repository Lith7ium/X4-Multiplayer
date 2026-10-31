/* The same runtime component IDs on every client, set mode, on the Linux X4 9.00 executable: the
 * Linux twin of lockstep_runtime_ids.h, whose free list work, map digest and status are shared.
 * Included by gate_linux.c only. Why IDs differ and what set mode does are in that header.
 *
 * Linux sites: the map pointer is at 0x3daa860, the same structure as Windows with every field 8
 * bytes later because X4's mutex at +0x10 is 0x30 bytes (a critical section 0x28): inserts +0x40,
 * live +0x48, free +0x4c, generation +0x54, head +0x58, tail +0x5c, blocks +0x60. A slot's bucket
 * is unchanged. Remove 0xee3850 (rdi map, rsi ID, al removed) holds the only append to the free
 * list; EnsureFreeSlots is inlined into AssignNewID 0xeed870, which the gate does not touch.
 *
 * The table of who allocates (native_gate_runtime_ids_callers, default off) has its own hook here:
 * AssignNewID's entry, rdi the object, rax the ID. Before the seal it notes the return address only,
 * but for the few hundred components made after the import, where clients part (vps-linux-17: 375
 * against 372 and 370, the only load row that differs); after it the stack, through glibc's backtrace
 * and X4's .eh_frame. The rows are those of Windows, with
 * Linux addresses, so census_runtime_callers.py compares two clients of either platform. It costs one
 * unwind per allocation in play, about 16 a tick, and it only reports.
 *
 * Not ported: the local lists for view objects (native_gate_runtime_ids_local), whose route table
 * needs the Linux stacks of the rows above. Without them a view object still moves every later
 * simulation ID, and next= shows it. */
#include <execinfo.h>
#define RID_OWN_PLATFORM
#define RID_MAP (LX_ID_MAP_VA-LX_IMAGE_BASE)
#define RID_REMOVE_ENTRY (LX_ID_REMOVE_VA-LX_IMAGE_BASE)
#define RID_REMOVE_THUNK 0x300u  /* the relocated prologue, then jmp [rip] -> Remove+6 */
#define RID_F_INSERTS 0x40
#define RID_F_LIVE 0x48
#define RID_F_FREE 0x4c
#define RID_F_GENERATION 0x54
#define RID_F_HEAD 0x58
#define RID_F_TAIL 0x5c
#define RID_F_BLOCKS 0x60
#define RID_F_BLOCK1 0x68
#define LXI_THUNK_HOOK 0x340u    /* jmp [rip] -> rid_remove */
#define RID_ASSIGN_THUNK 0x380u  /* the relocated prologue, then jmp [rip] -> AssignNewID+6 */
#define LXI_ASSIGN_HOOK 0x3c0u   /* jmp [rip] -> rid_assign */
#define CRITICAL_SECTION void
#define EnterCriticalSection(mutex) lxg_lock(mutex)
#define LeaveCriticalSection(mutex) lxg_unlock(mutex)
#define LONG long /* the status prints no active word; rid_local_on stays this type */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_runtime_ids.h"
#pragma GCC diagnostic pop
#undef CRITICAL_SECTION
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef LONG
static int rid_pins(void) {return 1;} /* the gate's site pins, checked at start */
static int rid_swap(uint64_t expected,uint64_t value) {return gate_code_swap64(LX(LX_ID_REMOVE_VA),expected,value);}
static int rid_callers_on; /* the table of who allocates; the hook also serves the created identities */
static uint64_t rid_assign_patched_word;
/* The X4 return addresses of an allocation, innermost first: the direct caller before the seal, the stack after it and for components. */
static unsigned rid_frames_of(void *caller,uint32_t *frames) {
    const uintptr_t low=(uintptr_t)image,high=low+(LX_TEXT_END_VA-LX_IMAGE_BASE);unsigned n=0;
    if(rid_sealed || (uintptr_t)caller==(uintptr_t)LX(LX_ID_COMPONENT_RETURN_VA)) {
        void *stack[RID_STACK];const int got=backtrace(stack,RID_STACK);
        for(int i=0;i<got;i++) if((uintptr_t)stack[i]>=low && (uintptr_t)stack[i]<high) frames[n++]=(uint32_t)((uintptr_t)stack[i]-low);
    }
    if(!n && (uintptr_t)caller>=low && (uintptr_t)caller<high) frames[n++]=(uint32_t)((uintptr_t)caller-low);
    return n;
}
/* The thunk jumps, so the return address is AssignNewID's caller. */
static __attribute__((noinline)) uint64_t rid_assign(void *object) {
    const created_assign_fn original=(created_assign_fn)(void*)(thunk+RID_ASSIGN_THUNK);
    if(!rid_callers_on) return created_assign_execute(original,object);
    void *caller=__builtin_return_address(0);uint32_t frames[RID_STACK];
    const unsigned count=rid_frames_of(caller,frames);
    const DWORD thread=GetCurrentThreadId();
    const uint64_t result=created_assign_execute(original,object);
    rid_note_caller(frames,count,RID_ROUTE_SHARED,thread==ui_thread?0u:thread==main_thread?1u:2u);
    return result;
}
static void lxi_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXI_THUNK_HOOK,(void*)rid_remove);
    unsigned char *p=page+RID_REMOVE_THUNK;memcpy(p,"\x41\x57\x41\x56\x41\x55",LX_ID_REMOVE_PROLOGUE); /* push r15; push r14; push r13 */
    lxw_jump_write(p+LX_ID_REMOVE_PROLOGUE,LX(LX_ID_REMOVE_VA)+LX_ID_REMOVE_PROLOGUE);
    lxw_jump_write(page+LXI_ASSIGN_HOOK,(void*)rid_assign);
    p=page+RID_ASSIGN_THUNK;memcpy(p,"\x41\x56\x41\x55\x41\x54",LX_ID_ASSIGN_PROLOGUE); /* push r14; push r13; push r12 */
    lxw_jump_write(p+LX_ID_ASSIGN_PROLOGUE,LX(LX_ID_ASSIGN_VA)+LX_ID_ASSIGN_PROLOGUE);
}
static int rid_install(void) {
    unsigned char *entry=LX(LX_ID_REMOVE_VA);
    if(!thunk) {rid_stage="thunk";return 0;}
    if(rd64(entry)!=LX_ID_REMOVE_ENTRY_ORIGINAL) {rid_stage="entry";return 0;}
    rid_original_word=LX_ID_REMOVE_ENTRY_ORIGINAL;
    if(!lxw_patch(entry,rid_original_word,LXI_THUNK_HOOK,&rid_patched_word)) {rid_stage="swap";return 0;}
    rid_installed=1;return 1;
}
static int rid_owned(void) {
    return (!rid_installed || rd64(LX(LX_ID_REMOVE_VA))==rid_patched_word) && (!rid_assign_installed || rd64(LX(LX_ID_ASSIGN_VA))==rid_assign_patched_word);
}
/* AssignNewID's entry, once, for the table of who allocates and the created identities. */
static int rid_assign_install(void) {
    if(rid_assign_installed) return 1;
    if(rd64(LX(LX_ID_ASSIGN_VA))!=LX_ID_ASSIGN_ENTRY_ORIGINAL) return 0;
    if(!lxw_patch(LX(LX_ID_ASSIGN_VA),LX_ID_ASSIGN_ENTRY_ORIGINAL,LXI_ASSIGN_HOOK,&rid_assign_patched_word)) return 0;
    rid_assign_installed=1;return 1;
}
/* After a set rid_arm, before the load. The first backtrace loads libgcc_s, so that happens here and not on some X4 thread. */
static int rid_callers_arm(void) {
    if(rid_callers_on) return rid_mode==RID_SET;
    if(rid_mode!=RID_SET || !stats.active || rid_sealed || GetCurrentThreadId()!=ui_thread || !thunk) {rid_stage="callers_phase";return 0;}
    void *first[2];backtrace(first,2);
    if(!rid_assign_install()) {rid_stage="callers_entry";return 0;}
    rid_callers_on=1;return 1;
}
/* With rid_restore: the entry back; a call already in the hook finishes through the thunk, which stays. */
static void rid_callers_restore(void) {
    if(rid_assign_installed && gate_code_swap64(LX(LX_ID_ASSIGN_VA),rid_assign_patched_word,LX_ID_ASSIGN_ENTRY_ORIGINAL)) rid_assign_installed=0;
    if(rid_assign_installed) rid_restored=0;
}
static int lua_runtime_ids_callers(lua_State *L) {p_lua_pushnumber(L,rid_callers_arm());return 1;}
static int lua_runtime_ids(lua_State *L) {
    double base=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(base) && base>=0 && base<=RID_BASE_MAX && floor(base)==base && rid_arm((uint32_t)base));return 1;
}
static int lua_runtime_ids_status(lua_State *L) {
    char out[900];int n=accept_ui()?rid_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* The ID the next allocation would get, for native_gate_runtime_ids_barrier: not ported, so never read. */
static int lua_runtime_ids_word(lua_State *L) {
    char out[32];int n=accept_ui()?snprintf(out,sizeof(out),"%016llx",(unsigned long long)rid_next_id):0;
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_runtime_ids_map_row(lua_State *L) {
    char out[640];int n=accept_ui()?rid_map_row(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_runtime_ids_caller_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];
    int n=accept_ui() && isfinite(index) && index>=0 && index<RID_CALLERS && floor(index)==index?rid_caller_row((unsigned)index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
