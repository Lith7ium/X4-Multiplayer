/* The boot hold on the Linux X4 9.00 executable: the Linux twin of lockstep_boot_hold.h, whose
 * logic and status are shared. Included by gate_linux.c only. The mechanism is in that header.
 *
 * Linux sites: UniverseFrameUpdate calls AIDirector::Move 0x10e27a0 at 0xfeb2bc, its only caller,
 * with the director from 0x3dea760; wake vectors +0x200/+0x218 and index +0x230 as on Windows.
 * That call's rel32 straddles a cache line, so the entry is patched instead (one aligned 8-byte
 * compare-exchange), and the relocated prologue runs Move: push rbp; mov rbp,rsp; push r15. */
#define BOOT_HOLD_OWN_PLATFORM
#define BOOT_HOLD_DIRECTOR (LX_BOOT_HOLD_DIRECTOR_VA-LX_IMAGE_BASE)
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_boot_hold.h"
#undef LONG
#define LXB_THUNK_HOOK 0x180u       /* jmp [rip] -> boot_hold_move */
#define LXB_THUNK_TRAMPOLINE 0x1c0u /* the relocated prologue, then jmp [rip] -> Move+6 */
static __attribute__((noinline)) void boot_hold_move(void *director) {
    boot_hold_execute((boot_hold_fn)(void*)(thunk+LXB_THUNK_TRAMPOLINE),director);
}
static void lxb_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXB_THUNK_HOOK,(void*)boot_hold_move);
    unsigned char *p=page+LXB_THUNK_TRAMPOLINE;memcpy(p,"\x55\x48\x89\xe5\x41\x57",LX_BOOT_HOLD_PROLOGUE);
    lxw_jump_write(p+LX_BOOT_HOLD_PROLOGUE,LX(LX_BOOT_HOLD_MOVE_VA)+LX_BOOT_HOLD_PROLOGUE);
}
static int boot_hold_owned(void) {return !boot_hold_installed || rd64(LX(LX_BOOT_HOLD_MOVE_VA))==boot_hold_patched_word;}
/* Armed once at boot, before the load (idempotent on a loading-UI reload). */
static int boot_hold_arm(void) {
    if(boot_hold_armed) return stats.active && boot_hold_installed;
    unsigned char *entry=LX(LX_BOOT_HOLD_MOVE_VA);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       rd64(entry)!=LX_BOOT_HOLD_ENTRY_ORIGINAL) return 0;
    boot_hold_original_word=LX_BOOT_HOLD_ENTRY_ORIGINAL;
    if(!lxw_patch(entry,boot_hold_original_word,LXB_THUNK_HOOK,&boot_hold_patched_word)) return 0;
    boot_hold_installed=1;boot_hold_armed=1;boot_hold_stage="armed";InterlockedExchange(&boot_hold_active,1);return 1;
}
static void boot_hold_restore(void) {
    if(!boot_hold_armed) return;
    InterlockedExchange(&boot_hold_active,0);
    if(boot_hold_installed && gate_code_swap64(LX(LX_BOOT_HOLD_MOVE_VA),boot_hold_patched_word,boot_hold_original_word)) boot_hold_installed=0;
    boot_hold_restored=!boot_hold_installed;
}
static int lua_boot_hold(lua_State *L) {p_lua_pushnumber(L,boot_hold_arm());return 1;}
static int lua_boot_hold_status(lua_State *L) {
    char out[512];int n=accept_ui()?boot_hold_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
