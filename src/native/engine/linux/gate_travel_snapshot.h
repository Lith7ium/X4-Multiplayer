/* The wingman's travel decision from a per-pass snapshot on the Linux X4 9.00 executable: the Linux
 * twin of lockstep_travel_snapshot.h, whose walk, join and status are shared. Included by
 * gate_linux.c only. The audit behind it is in that header, on the Windows addresses.
 *
 * Linux sites: the agent's flag is +0xf9b (Windows +0xf7b), its object +0xf10, and the block is one
 * 0xfb0-byte allocation whose last field is +0xfa0, so the copy lives in the padding at +0xfa8. The
 * wingman reads its leader's flag at 0x1446b3b (cmp byte [rdx+0xf9b],0), out of line of the block at
 * 0x14458df; the disp32's low byte is byte 5 of the qword at 0x1446b38. The pass 0x13e3d86 hands the
 * job thunk 0x13c45c0 to the scheduler through two LEAs, at 0x13e3db9 and 0x13e3e2d. The manager's
 * unassigned list is +0x2a8, its batch vectors +0x2b0 and +0x2c8.
 *
 * Windows reads the walk under a fault guard; here it reads directly, like gate_path.h, and for the
 * same reason: the pass is joined and the lists stay unchanged until the next release. */
#include <sched.h>
#define TSNAP_OWN_PLATFORM
#define TSNAP_READ_WORD (LX_TRAVEL_READ_WORD_VA-LX_IMAGE_BASE)
#define TSNAP_LEA_A (LX_PATH_JOB_LEA_A_VA-LX_IMAGE_BASE)
#define TSNAP_LEA_B (LX_PATH_JOB_LEA_B_VA-LX_IMAGE_BASE)
#define TSNAP_JOB_THUNK (LX_PATH_JOB_VA-LX_IMAGE_BASE)
#define TSNAP_FLAG 0xf9bu
#define TSNAP_PAD 0xfa8u
#define TSNAP_DISP_FLAG 0x9bu /* low byte of disp32 0x00000f9b; 0x0f stays */
#define TSNAP_DISP_PAD 0xa8u  /* low byte of disp32 0x00000fa8 */
#define TSNAP_SLOT 0x2c0u     /* jmp [rip] -> tsnap_job */
#define TSNAP_AGENT_OBJECT LXW_AGENT_OWNER
#define TSNAP_FRESH 0x2a8
#define TSNAP_BATCHES 0x2b0
#define TSNAP_BATCHES_END 0x2b8
static int tsnap_swap8(unsigned site,uint64_t expected,uint64_t value) {return gate_code_swap64(image+site,expected,value);}
static int tsnap_swap16(unsigned site,unsigned __int128 expected,unsigned __int128 value) {return gate_code_swap128(image+site,expected,value);}
#define SwitchToThread() sched_yield()
#define __try
#define __finally
#define __except(filter) if(0)
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_travel_snapshot.h"
#undef __try
#undef __finally
#undef __except
#undef LONG
#undef SwitchToThread
static void lxt_thunk_write(unsigned char *page) {lxw_jump_write(page+TSNAP_SLOT,(void*)tsnap_job);}
static int lxt_lea_target(unsigned site,unsigned displacement,unsigned char *target) {
    int32_t rel;memcpy(&rel,image+site+displacement,4);
    return image+site+displacement+4+(intptr_t)rel==target;
}
static int tsnap_arm(void) {
    if(tsnap_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !thunk ||
       !path_installed || !(path_options&1) || !(path_det_mask&PATH_DET_BUDGET_BIT)) return 0;
    memcpy(&tsnap_lea_original[0],image+TSNAP_LEA_A,16);memcpy(&tsnap_lea_original[1],image+TSNAP_LEA_B,16);
    memcpy(&tsnap_read_original,image+TSNAP_READ_WORD,8);
    if(((tsnap_read_original>>40)&0xff)!=TSNAP_DISP_FLAG || ((tsnap_read_original>>48)&0xff)!=0x0f ||
       !lxt_lea_target(TSNAP_LEA_A,LX_PATH_JOB_LEA_A_DISP,image+TSNAP_JOB_THUNK) || !lxt_lea_target(TSNAP_LEA_B,LX_PATH_JOB_LEA_B_DISP,image+TSNAP_JOB_THUNK) ||
       !tsnap_lea_word(TSNAP_LEA_A,LX_PATH_JOB_LEA_A_DISP,tsnap_lea_original[0],&tsnap_lea_patched[0]) ||
       !tsnap_lea_word(TSNAP_LEA_B,LX_PATH_JOB_LEA_B_DISP,tsnap_lea_original[1],&tsnap_lea_patched[1])) return 0;
    tsnap_read_patched=(tsnap_read_original&~(0xffULL<<40))|((uint64_t)TSNAP_DISP_PAD<<40);
    tsnap_installed=1;tsnap_idle=tsnap_released=0;
    /* Completion wrappers only; the read moves after the first joined live pass. */
    tsnap_lea_installed[0]=tsnap_swap16(TSNAP_LEA_A,tsnap_lea_original[0],tsnap_lea_patched[0]);
    tsnap_lea_installed[1]=tsnap_lea_installed[0] && tsnap_swap16(TSNAP_LEA_B,tsnap_lea_original[1],tsnap_lea_patched[1]);
    if(!tsnap_lea_installed[1]) {tsnap_restore();tsnap_installed=0;return 0;}
    InterlockedExchange(&tsnap_active,1);return 1;
}
static int lua_travel_snapshot(lua_State *L) {p_lua_pushnumber(L,tsnap_arm());return 1;}
static int lua_travel_snapshot_status(lua_State *L) {
    char out[640];int n=accept_ui()?tsnap_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
