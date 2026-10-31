/* Ship loadouts from the caller's explicit generator state on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_spawn_rng.h, whose keys, rows and status are shared. Included by gate_linux.c only, after the script
 * RNG. The mechanism and the census evidence are in that header. vps-linux-20 shows the symptom: the only load
 * rows of the caller table that differ are 6 against 4 of one component and 28 against 29 turrets, on the
 * stack of the ship's loadout.
 *
 * Linux sites: U::ShipGenerator::Generate 0xd2a470 (Windows 0xc841b0), called from the job spawn 0x1593ed0, the
 * god spawn 0x162a450 and create_ship 0xdc7ebd. rdi the result, rsi the generator, rdx and rcx, r8d and r9 as on
 * Windows in the same order; on the stack the explicit state pointer, then three more. Its prologue is
 * relocated into a trampoline. The core 0xd28fc0 it calls holds the macro choice and InitializeShip 0xd029a0.
 *
 * One route is not covered: the job's own second spawn 0x1594310 calls the core directly, with the macro choice
 * inlined. Its draws stay on the word they find, and the rows do not count it. Whether that route runs in a load
 * shows in the caller table: its frames carry 0x11943a5. */
#define SPAWN_RNG_OWN_PLATFORM
#define SPAWN_RNG_ENTRY (LX_GENERATE_VA-LX_IMAGE_BASE)
#define LXN_THUNK 0x5c0u      /* the relocated prologue, then jmp [rip] -> Generate+6 */
#define LXN_HOOK 0x600u       /* jmp [rip] -> lxn_generate */
#define __try
#define __finally
#define LONG long /* the status prints the active word with %ld */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_spawn_rng.h"
#pragma GCC diagnostic pop
#undef __try
#undef __finally
#undef LONG
typedef uintptr_t (*lxn_generate_fn)(void *,void *,void *,void *,uintptr_t,void *,void *,uintptr_t,uintptr_t,uintptr_t);
/* Generate's four stack arguments pass through. The word is restored on return; X4's exceptions end the process. */
static __attribute__((noinline)) uintptr_t lxn_generate(void *result,void *generator,void *c,void *d,uintptr_t e,void *f,void *state,uintptr_t h,uintptr_t i,uintptr_t j) {
    spawn_rng_scope scope;
    spawn_rng_begin((uintptr_t)state,&scope);
    const uintptr_t value=((lxn_generate_fn)(void*)(thunk+LXN_THUNK))(result,generator,c,d,e,f,state,h,i,j);
    spawn_rng_end(&scope,1);
    return value;
}
static void lxn_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXN_HOOK,(void*)lxn_generate);
    unsigned char *p=page+LXN_THUNK;memcpy(p,"\x55\x48\x89\xe5\x41\x57",LX_GENERATE_PROLOGUE); /* push rbp; mov rbp,rsp; push r15 */
    lxw_jump_write(p+LX_GENERATE_PROLOGUE,LX(LX_GENERATE_VA)+LX_GENERATE_PROLOGUE);
}
static int spawn_rng_swap(uint64_t expected,uint64_t value) {return gate_code_swap64(LX(LX_GENERATE_VA),expected,value);}
static int spawn_rng_owned(void) {return !spawn_rng_installed || rd64(LX(LX_GENERATE_VA))==spawn_rng_patched_word;}
/* Armed before loading, so the load's job spawns are covered; a loading-UI reload with the same mode is accepted. */
static int spawn_rng_arm(int mode) {
    mode=mode==SPAWN_RNG_KEY?SPAWN_RNG_KEY:SPAWN_RNG_OBSERVE;
    if(spawn_rng_armed) return stats.active && spawn_rng_installed && spawn_rng_mode==mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       rd64(LX(LX_GENERATE_VA))!=LX_GENERATE_ENTRY_ORIGINAL) return 0;
    spawn_rng_original_word=LX_GENERATE_ENTRY_ORIGINAL;spawn_rng_mode=mode;
    if(!lxw_patch(LX(LX_GENERATE_VA),spawn_rng_original_word,LXN_HOOK,&spawn_rng_patched_word)) return 0;
    spawn_rng_installed=1;spawn_rng_armed=1;InterlockedExchange(&spawn_rng_active,1);return 1;
}
static int lua_spawn_rng(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(mode==0.0 || mode==1.0)?spawn_rng_arm(mode==1.0?SPAWN_RNG_KEY:SPAWN_RNG_OBSERVE):0);
    return 1;
}
static int lua_spawn_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?spawn_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_spawn_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=spawn_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
