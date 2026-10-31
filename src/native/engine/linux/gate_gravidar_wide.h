/* The Gravidar's zone groups in a portable order for every gated pass, on the Linux X4 9.00 executable:
 * the Linux twin of lockstep_gravidar_wide.h's 'sort' mode, whose plan, permutation, settle and status are
 * shared. Included by gate_gravidar.h, between the AI schedule and the gated frame. The mechanism and the
 * evidence are in that header. vps-linux-19 and 20 show the same symptom as Windows: of 474 scripts through
 * tick 60 only fight.attack.object.station differs, because a station's event_gravidar_has_scanned and its
 * count_gravidar_contacts see other contacts on one client.
 *
 * Linux layout: the sector list +0x70c0 of 32-byte entries, each [0] the sector and a group vector at +8; a
 * group of 0x58 bytes, [0] the zone, near list +8, wide list +0x20, neighbour group indices (u64) +0x38, done
 * +0x50; the list lock +0x7090, the ready lock +0x7168 and the ready byte +0x7198. All but the worker's own
 * offsets are those of Windows, since libstdc++'s vector is as large as MSVC's. The scan is 0x1caeab0.
 *
 * Only 'sort' is ported. It patches no code. 'wide' changes play and Windows does not play with it. */
#define GWIDE_OWN_PLATFORM
#define GWIDE_WORKER (LX_GRAVIDAR_WORKER_VA-LX_IMAGE_BASE)
#define GWIDE_SECTORS LXG_LIST
#define GWIDE_PUBLISH (LX_GRAVIDAR_PUBLISH_VA-LX_IMAGE_BASE)
#define GWIDE_LIST_LOCK LXG_LIST_LOCK
#define GWIDE_READY_LOCK LXG_READY_LOCK
#define GWIDE_READY LXG_READY
#define CRITICAL_SECTION void
#define EnterCriticalSection(mutex) lxg_lock(mutex)
#define LeaveCriticalSection(mutex) lxg_unlock(mutex)
#define Sleep(ms) usleep((ms)*1000u)
#define __try
#define __finally
#define AbnormalTermination() 0
#define LONG long /* the status prints the active word with %ld */
#define thunk_slot_ready(offset) 0 /* 'wide' has no thunk here, so it is refused */
static int gwide_pinned(void) {return 1;}      /* the gate's site pins, checked at start */
static int gwide_sort_pinned(void) {return 1;}
static int gwide_prepare(void) {return 0;}
static int gwide_swap(int32_t expected,int32_t value) {(void)expected;(void)value;return 0;}
static int gwide_owned(void) {return 1;}
static uintptr_t gwide_hint; /* the arming clears it; only 'wide' uses it */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "lockstep_gravidar_wide.h"
#pragma GCC diagnostic pop
#undef CRITICAL_SECTION
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef Sleep
#undef __try
#undef __finally
#undef AbnormalTermination
#undef LONG
#undef thunk_slot_ready
static int lua_gravidar_wide(lua_State *L) {
    size_t size=0;const char *mode=p_lua_tolstring(L,1,&size);
    p_lua_pushnumber(L,mode && size==4 && !memcmp(mode,"sort",4) && gwide_arm(GWIDE_MODE_SORT,gravidar_installed));
    return 1;
}
static int lua_gravidar_wide_status(lua_State *L) {
    char out[640];int n=accept_ui()?gwide_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
