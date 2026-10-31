/* Gravidar factor-pass deadlines on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_gravidar_deadlines.h, whose keys and status are shared. Included by gate_linux.c only.
 * The mechanism and the evidence are in that header.
 *
 * Linux sites: U::Sector::Move 0x127cf40, held only by the vtable slot 0x3d1eda8 (+0x1558 as on
 * U::Region). The field flag is +0x43a and the deadline +0x440 (Windows +0x442, +0x448), attention
 * +0x1ec. A due pass at attention below 5 schedules now + 5 s + Random::FloatState(1.0) 0x1a491c0.
 *
 * Windows also sets the active units' scan times back to the constructor's at admission, for a
 * load pass that ran before it. The Linux gate holds that pass (ai_schedule mask 2), so it requires
 * the hold instead of walking the unit tables, which it has no other use for. */
#define GDL_OWN_PLATFORM
#define GDL_SLOT (LX_SECTOR_MOVE_SLOT_VA-LX_IMAGE_BASE)
#define GDL_MOVE (LX_SECTOR_MOVE_VA-LX_IMAGE_BASE)
#define GDL_FIELD 0x43au
#define GDL_DUE 0x440u
#define GDL_TLS_READY GATE_TLS_LINUX_VIEW
#define GDL_TLS_TIME GATE_TLS_LINUX_TIME
#define GDL_TLS_WORD GATE_TLS_LINUX_RNG
#define script_rng_saved_with(runtime,klass) world_identity_key((runtime),(klass))
static unsigned char *gdl_tls(void) {return gate_tls_self();}
#define __try
#define __finally
#define AbnormalTermination() 0
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_gravidar_deadlines.h"
#undef __try
#undef __finally
#undef AbnormalTermination
#undef GetLastError
#undef SetLastError
#undef LONG
#undef script_rng_saved_with
static int gdl_pinned(void) {
    uint64_t slot=0;
    return gate_read_local(LX(LX_SECTOR_MOVE_SLOT_VA),&slot,8) && slot==(uint64_t)(uintptr_t)LX(LX_SECTOR_MOVE_VA);
}
/* Boot-phase-3 entry: no pass ran before it, or the first gated visits would not be alike. */
static int gdl_admit(void) {
    if(!gdl_armed || gdl_admitted) return 1;
    if(!ai_sched_armed || !(ai_sched_mask&AI_SCHED_HOLD)) {gdl_stage="no_hold";return 0;}
    gdl_admitted=1;gdl_stage="admitted";return 1;
}
static int lua_gravidar_deadlines(lua_State *L) {p_lua_pushnumber(L,gdl_arm());return 1;}
static int lua_gravidar_deadlines_status(lua_State *L) {
    char out[1536];int n=accept_ui()?gdl_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
