/* Load-time MD cue delays on per-cue streams on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_md_resume_rng.h, whose keys, rows and status are shared. Included by gate_linux.c
 * only. The mechanism and the census evidence are in that header.
 *
 * Linux sites: Cue_ResumeAfterLoad 0xf77160 calls the delay evaluator 0xf5b380 at 0xf77325 with
 * (cue, the slot at cue+0x200) and adds the activation time cue+0x1f8, the same cue fields as
 * Windows. The E8's rel32 straddles an 8-byte boundary, so it changes with the 16-byte window
 * around it. The evaluator has two other callers, which keep it.
 *
 * The cue's name comes from the same fields too: cue+0x160 definition, definition+0x150 module,
 * module+0x18 its name, module+0x60 the tree of cue names, whose nodes hold the name at +0x20 and
 * the definition at +0x40. Only the strings differ (libstdc++ here), so the owner hash of a cue
 * is the same on both platforms. */
#define MD_RESUME_OWN_PLATFORM
#define MD_RESUME_SITE (LX_MD_RESUME_SITE_VA-LX_IMAGE_BASE)
#define MD_RESUME_DELAY (LX_MD_RESUME_DELAY_VA-LX_IMAGE_BASE)
#define __try
#define __finally
#define AbnormalTermination() 0
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_md_resume_rng.h"
#undef __try
#undef __finally
#undef AbnormalTermination
#undef GetLastError
#undef SetLastError
#undef LONG
#define LXM_THUNK_HOOK 0x200u /* jmp [rip] -> md_resume_delay */
static unsigned __int128 lxm_window_original,lxm_window_patched;
static unsigned char *lxm_window(void) {return (unsigned char*)((uintptr_t)LX(LX_MD_RESUME_SITE_VA)&~(uintptr_t)15);}
static void lxm_thunk_write(unsigned char *page) {lxw_jump_write(page+LXM_THUNK_HOOK,(void*)md_resume_delay);}
static int md_resume_owned(void) {
    int32_t rel=0;if(!md_resume_installed) return 1;
    memcpy(&rel,LX(LX_MD_RESUME_SITE_VA)+1,4);return rel==md_resume_patched_rel;
}
/* Armed once at boot, before the load (idempotent on a loading-UI reload). */
static int md_resume_arm(void) {
    if(md_resume_armed) return stats.active && md_resume_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk) return 0;
    unsigned char *site=LX(LX_MD_RESUME_SITE_VA),*window=lxm_window();int32_t rel;
    if(site[0]!=0xe8) return 0;
    memcpy(&rel,site+1,4);if((uintptr_t)(site+5)+(intptr_t)rel!=(uintptr_t)LX(LX_MD_RESUME_DELAY_VA)) return 0;
    intptr_t distance=(intptr_t)(thunk+LXM_THUNK_HOOK)-(intptr_t)(site+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    md_resume_original_rel=rel;md_resume_patched_rel=(int32_t)distance;
    memcpy(&lxm_window_original,window,16);lxm_window_patched=lxm_window_original;
    memcpy((unsigned char*)&lxm_window_patched+(site+1-window),&md_resume_patched_rel,4);
    if(!gate_code_swap128(window,lxm_window_original,lxm_window_patched)) return 0;
    md_resume_installed=1;md_resume_armed=1;InterlockedExchange(&md_resume_active,1);return 1;
}
/* Open scopes still restore their own words; this gives the call back. */
static void md_resume_restore(void) {
    if(!md_resume_armed) return;
    InterlockedExchange(&md_resume_active,0);
    if(md_resume_installed && gate_code_swap128(lxm_window(),lxm_window_patched,lxm_window_original)) md_resume_installed=0;
    md_resume_restored=!md_resume_installed;
}
static int lua_md_resume_rng(lua_State *L) {p_lua_pushnumber(L,md_resume_arm());return 1;}
static int lua_md_resume_rng_status(lua_State *L) {
    char out[768];int n=accept_ui()?md_resume_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_md_resume_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=md_resume_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
