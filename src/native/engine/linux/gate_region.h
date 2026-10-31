/* Hazard region updates on per-region streams on the Linux X4 9.00 executable: the Linux
 * twin of lockstep_region_rng.h, whose keys, rows and status are shared. Included by
 * gate_linux.c only. The mechanism and the census evidence are in that header.
 *
 * Linux sites: U::Region vtable 0x3d15408 (gate_world_classes.h), Move 0x12c7f80 in its slot
 * at +0x1558 (Windows +0x1510), reached only through that slot (0x1193339, 0x18cbc07,
 * 0x18cbc77, no direct call). Move calls slot +0x48 of each field (Windows +0x40) of the
 * field vector +0x280..+0x288 (Windows +0x288..+0x290). U::Regions::DamageField vtable
 * 0x3b57b88, per-frame update 0x12d7a80, last pass +0x10f0 and next pass +0x10f8 as on Windows.
 * The vtable sits in RELRO, so its slot is swapped with the page writable for the moment.
 *
 * Windows restores the word in a __finally when an exception crosses the scope. This scope
 * has no such cleanup: X4's exceptions end the process, so nothing resumes on that word. */
#define REGION_RNG_OWN_PLATFORM
#define REGION_RNG_SLOT (LX_REGION_SLOT_VA-LX_IMAGE_BASE)
#define REGION_RNG_MOVE (LX_REGION_MOVE_VA-LX_IMAGE_BASE)
#define REGION_RNG_FIELD_VTABLE (LX_DAMAGE_FIELD_VTABLE-LX_IMAGE_BASE)
#define REGION_RNG_FIELDS 0x280u
#define script_rng_saved(runtime) world_identity_lookup(runtime) /* the sealed table only, as before the script RNG came */
static int region_rng_pinned(void); /* below: lx_sites hold the code, these the vtable slots */
#define __try
#define __finally
#define AbnormalTermination() 0
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define LONG long /* the status prints the active word with %ld */
#include "lockstep_region_rng.h"
#undef script_rng_saved
#undef __try
#undef __finally
#undef AbnormalTermination
#undef GetLastError
#undef SetLastError
#undef LONG
static int region_rng_pinned(void) {
    void *move=NULL,*update=NULL;
    return gate_read_local(LX(LX_REGION_SLOT_VA),&move,8) && move==(void*)LX(LX_REGION_MOVE_VA) &&
           gate_read_local(LX(LX_DAMAGE_FIELD_VTABLE+0x48u),&update,8) && update==(void*)LX(LX_DAMAGE_FIELD_UPDATE_VA);
}
static int lua_region_rng(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(mode==0.0 || mode==1.0)?region_rng_arm(mode==1.0?REGION_RNG_KEY:REGION_RNG_OBSERVE):0);return 1;
}
static int lua_region_rng_status(lua_State *L) {
    char out[1536];int n=accept_ui()?region_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_region_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=region_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
