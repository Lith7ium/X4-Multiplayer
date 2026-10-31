/* Load-time AI Wait durations on per-wait streams on the Linux X4 9.00 executable: the Linux
 * twin of lockstep_ai_wait_rng.h, whose keys, rows and status are shared. Included by
 * gate_linux.c only. The mechanism and the evidence are in that header; the Linux facts below
 * are from reports/LINUX-SCOPED-WAIT-COMPATIBILITY-20261027.txt and the pins in gate_sites.h.
 *
 * The AI importer 0x10de190 calls the restore of each blocked action at 0x10df368 (call
 * [rax+0x30], return 0x10df36b; RDI action, RSI Exec, RDX the context Value at Exec+0x38).
 * U::AI::WaitAction's vtable 0x3b2a650 is the only holder of its restore 0x10c12b0, so the gate
 * swaps that vtable slot instead of patching the entry as Windows does.
 *
 * Layouts (Windows in brackets): Value tag 0x66, payload at +8 is the context + 0xa0 (+0xa8);
 * context vtable 0x3b28df0, lifetime +8, entity +0x120, script descriptor +0xd0 (+0xd8), due
 * +0x190; descriptor version +0x58, name a libstdc++ string at +0x10 (pointer, length; MSVC's
 * size +0x20); Exec program +0x10, PC +0x18, slot +0x20, start +0x28, blocked +0x30.
 *
 * Identities: the AI import runs before the load clears the import map, so the gate reads the
 * nodes recorded so far when it needs an owner (world_identity_loading). A mixed session needs
 * more: slot and PC are in the key, and Linux's compiled slots can differ from Windows'. */
#define AI_WAIT_OWN_PLATFORM
#define WORLD_WAIT_CALLER (LX_AI_WAIT_RETURN_VA-LX_IMAGE_BASE)
#include "lockstep_text_hash.h"
typedef unsigned char (*world_wait_fn)(void *,void *,void *);
typedef struct {
    uintptr_t self,exec,value,context,entity,script,program,begin,end;
    uint64_t lifetime,runtime,saved,pc,slot,time;
    uint32_t version,tag;unsigned char blocking;char name[96];
} world_wait_guard;
static int world_wait_installed;
static int lxa_script_name(uintptr_t script,char text[96]) {
    uintptr_t data=0;uint64_t size=0;
    if(!read_local((void*)(script+0x10),&data,8) || !data || !read_local((void*)(script+0x18),&size,8) || !size || size>=96 ||
       !read_local((void*)data,text,(size_t)size)) return 0;
    text[size]=0;
    for(uint64_t i=0;i<size;i++) if((unsigned char)text[i]<32 || (unsigned char)text[i]>126 || text[i]=='|' || text[i]==' ') return 0;
    return 1;
}
static int world_wait_guard_read(void *self,void *exec,void *value,world_wait_guard *g) {
    memset(g,0,sizeof(*g));g->self=(uintptr_t)self;g->exec=(uintptr_t)exec;g->value=(uintptr_t)value;
    uintptr_t payload=0,vtable=0;
    if(!self || !exec || g->value!=g->exec+0x38 || !read_local(value,&g->tag,4) || g->tag!=0x66 ||
       !read_local((unsigned char*)value+8,&payload,8) || payload<0xa0) return 0;
    g->context=payload-0xa0;
    if(!read_local((void*)g->context,&vtable,8) || vtable!=(uintptr_t)LX(LX_AI_CONTEXT_VTABLE) ||
       !read_local((void*)(g->context+8),&g->lifetime,8) || !g->lifetime ||
       !read_local((void*)(g->context+0x120),&g->entity,8) || !g->entity ||
       !read_local((void*)(g->entity+8),&g->runtime,8) || !g->runtime) return 0;
    g->saved=world_identity_loading(g->runtime);
    if(!read_local((void*)(g->context+0xd0),&g->script,8) || !g->script ||
       !read_local((void*)(g->script+0x58),&g->version,4) || !lxa_script_name(g->script,g->name) ||
       !read_local((unsigned char*)exec+0x10,&g->program,8) || !g->program ||
       !read_local((void*)g->program,&g->begin,8) || !read_local((void*)(g->program+8),&g->end,8) ||
       !read_local((unsigned char*)exec+0x18,&g->pc,8) || !read_local((unsigned char*)exec+0x20,&g->slot,8) ||
       !read_local((unsigned char*)exec+0x28,&g->time,8) || !read_local((unsigned char*)exec+0x30,&g->blocking,1)) return 0;
    if(!g->begin || g->end<g->begin || (g->end-g->begin)%8 || g->end-g->begin>65536*8 || g->slot>=(g->end-g->begin)/8) return 0;
    uintptr_t node=0;return read_local((void*)(g->begin+g->slot*8),&node,8) && node==g->self;
}
#define __try
#define __finally
#define AbnormalTermination() 0
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_ai_wait_rng.h"
#undef __try
#undef __finally
#undef AbnormalTermination
#undef GetLastError
#undef SetLastError
#undef LONG
static unsigned char lxa_wait_restore(void *self,void *exec,void *value) {
    return ai_wait_run((uintptr_t)__builtin_return_address(0),(world_wait_fn)(void*)LX(LX_AI_WAIT_RESTORE_VA),ai_wait_word(),self,exec,value);
}
static int ai_wait_owned(void) {
    uint64_t slot=0;
    return !world_wait_installed || (gate_read_local(LX(LX_AI_WAIT_SLOT_VA),&slot,8) && slot==(uint64_t)(uintptr_t)lxa_wait_restore);
}
/* Armed once at boot, before the load (idempotent on a loading-UI reload). */
static int ai_wait_arm(void) {
    if(ai_wait_armed) return stats.active && InterlockedCompareExchange(&ai_wait_active,0,0) && ai_wait_owned();
    uint64_t slot=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed ||
       !gate_read_local(LX(LX_AI_WAIT_SLOT_VA),&slot,8) || slot!=(uint64_t)(uintptr_t)LX(LX_AI_WAIT_RESTORE_VA) ||
       !gate_data_swap64(LX(LX_AI_WAIT_SLOT_VA),slot,(uint64_t)(uintptr_t)lxa_wait_restore)) return 0;
    world_wait_installed=1;ai_wait_armed=1;InterlockedExchange(&ai_wait_active,1);return 1;
}
static void ai_wait_restore(void) {
    if(!ai_wait_armed) return;
    InterlockedExchange(&ai_wait_active,0);
    if(world_wait_installed && gate_data_swap64(LX(LX_AI_WAIT_SLOT_VA),(uint64_t)(uintptr_t)lxa_wait_restore,(uint64_t)(uintptr_t)LX(LX_AI_WAIT_RESTORE_VA)))
        world_wait_installed=0;
    ai_wait_restored=!world_wait_installed;
}
static int lua_ai_wait_rng(lua_State *L) {p_lua_pushnumber(L,ai_wait_arm());return 1;}
static int lua_ai_wait_rng_status(lua_State *L) {
    char out[1024];int n=accept_ui()?ai_wait_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_ai_wait_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=ai_wait_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
