/* The navigation upkeep's refresh deadlines on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_nav_cadence.h's 'far' and 'observe' modes and its probe and parameter deadline initialisation, whose
 * apply code is shared. Included by gate_linux.c only. The mechanism and the evidence are in that header.
 *
 * vps-linux-19, 20 and 21 show why it comes now. The only two saved objects that differ are fixture ships 0x176bf8
 * and 0x176cef, in nav.parameters_due, set during the load from the natural word. vps-linux-24 shows this module
 * removes that: timing is 0 at ticks 1 and 60. It does not make the natural word equal, as those runs had suggested.
 * The word still parts at tick 47 while the ships' deadlines agree through tick 180, and the ships' later refreshes
 * draw from it: timing is 6 at tick 300 and 9 at tick 600, all of them ships of this fixture.
 *
 * Linux sites: U::NavigationController::vfunc33 is 0x144a9e0 (rdi controller, xmm0 time), held by slot 40 of two
 * vtables of that class, 0x3b697e8 and 0x3b94bb0. PlayerController's direct call 0x1672d71 is not covered
 * (native_gate_nav_player is not ported). Layout: ship +0x20, probe block +0x40 (Windows +0x28, +0x48); parameter
 * deadline +0xd0, enable +0xd8, probe deadline +0x3e0, enable +0x3e8 as on Windows. Both scheduling blocks have the
 * same shape: ja near, and jbe far, around the mid interval. 'far' removes the ja and makes the jbe a jmp, each by
 * one compare-exchange of its aligned 16 bytes. 'every' is not ported: Windows does not play with it.
 *
 * The status has Windows' deadline fields. The refresh counts and NAV_CADENCE_ROW lines are not ported.
 *
 * The refresh's draws, keyed (native_gate_nav_key, Linux only): win-sub4's frame draw rows show this function's draw
 * (Windows 0xea7982) as one of the two call chains that draw from the natural word on one client alone. Windows keys it
 * inside actor_rng_all's ship scope, an umbrella over the whole ship Move, its agents on the job threads and its
 * weapons, not ported. Here only this call is keyed, in the hook above: in an admitted frame on the MoveWorker, a
 * controller whose ship (+0x20) has a portable key runs with the thread's word seeded from (tick, key, class, the
 * ship's ordinal of this call in the tick, the time's bits) and gets its word back after it. Its own domain, so its
 * draws differ from Windows' umbrella even where both key the same call; an unmapped ship keeps the natural word. */
#define NAV_CADENCE_OWN_PLATFORM
#define NAV_F_OWNER 0x20
#define NAV_F_PROBE 0x40
enum {NAV_CADENCE_OFF,NAV_CADENCE_FAR,NAV_CADENCE_OBSERVE};
static const char *nav_cadence_stage="idle";
static volatile int64_t nav_cadence_failures;
static int nav_cadence_installed,nav_cadence_restored,nav_cadence_mode,nav_cadence_patched;
static volatile long nav_cadence_active;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_nav_cadence.h"
#pragma GCC diagnostic pop
typedef struct {uint32_t va;unsigned char size,original[6],far[6];} lxv_site_t;
static const lxv_site_t lxv_sites[4]={
    {LX_NAV_PARAMS_NEAR_VA,2,{0x77,0x2e},{0x66,0x90}},                 /* ja -> nop */
    {LX_NAV_PARAMS_FAR_VA,6,{0x0f,0x86,0x71,0x08,0,0},{0x90,0xe9,0x71,0x08,0,0}}, /* jbe -> nop; jmp */
    {LX_NAV_PROBE_NEAR_VA,2,{0x77,0x2e},{0x66,0x90}},
    {LX_NAV_PROBE_FAR_VA,6,{0x0f,0x86,0x48,0x08,0,0},{0x90,0xe9,0x48,0x08,0,0}}};
static int lxv_slots_installed;
/* The nav key: per-tick ordinals by ship (an open table cleared by epoch), tallies. */
#define LXK_DOMAIN 0x4c584e41564b4559ULL /* "LXNAVKEY" */
#define LXK_ORDINALS 4096u
typedef struct {uint64_t id;uint32_t epoch,count;} lxk_slot;
static lxk_slot lxk_slots[LXK_ORDINALS];
static uint32_t lxk_epoch;
static uint64_t lxk_epoch_tick=UINT64_MAX,lxk_calls,lxk_keyed,lxk_unmapped,lxk_full;
static int lxk_armed;
static uint32_t lxk_ordinal(uint64_t id) {
    if(lxk_epoch_tick!=stats.issued) {lxk_epoch_tick=stats.issued;if(!++lxk_epoch) {memset(lxk_slots,0,sizeof(lxk_slots));lxk_epoch=1;}}
    unsigned at=(unsigned)lockstep_mix(id)&(LXK_ORDINALS-1u);
    for(unsigned n=0;n<64;n++,at=(at+1)&(LXK_ORDINALS-1u)) {
        lxk_slot *s=&lxk_slots[at];
        if(s->epoch!=lxk_epoch) {s->epoch=lxk_epoch;s->id=id;s->count=1;return 1;}
        if(s->id==id) return ++s->count;
    }
    return 0; /* full: the caller keeps the natural word */
}
typedef void (*lxv_move_fn)(void *,double);
static void lxk_move(void *self,double time) {
    lxv_move_fn native=(lxv_move_fn)(void*)LX(LX_NAV_MOVE_VA);
    unsigned char *ship=NULL;uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    ++lxk_calls;
    if(read_local((unsigned char*)self+NAV_F_OWNER,&ship,8) && ship && read_local(ship+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    uint64_t *rng=gate_tls_rng(gate_tls_self());
    const uint32_t ordinal=key && rng?lxk_ordinal(key):0;
    if(!key || !rng || !ordinal) {if(key && rng) ++lxk_full;else ++lxk_unmapped;native(self,time);return;}
    uint64_t bits;memcpy(&bits,&time,8);
    const uint64_t saved=*rng;*rng=lockstep_seed(LXK_DOMAIN,stats.issued,key,klass,ordinal,bits);++lxk_keyed;
    native(self,time); /* X4's exceptions end the process: nothing to unwind */
    *rng=saved;
}
static __attribute__((noinline)) void lxv_move(void *self,double time) {
    if(InterlockedCompareExchange(&nav_cadence_active,0,0) && stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) {
        if(nav_probe_armed) nav_probe_apply(self,time);
        if(nav_params_armed) nav_params_apply(self,time);
        if(lxk_armed) {lxk_move(self,time);return;}
    }
    ((lxv_move_fn)(void*)LX(LX_NAV_MOVE_VA))(self,time);
}
/* With the cadence hook, before the first admitted frame; a rerun is accepted. */
static int lxk_arm(void) {
    if(lxk_armed) return nav_cadence_installed;
    if(!nav_cadence_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed) return 0;
    lxk_calls=lxk_keyed=lxk_unmapped=lxk_full=0;lxk_epoch_tick=UINT64_MAX;lxk_armed=1;return 1;
}
static int lxv_site_is(const lxv_site_t *s,const unsigned char *bytes) {return !memcmp(LX(s->va),bytes,s->size);}
static int lxv_swap(const lxv_site_t *s,const unsigned char *expected,const unsigned char *value) {
    unsigned char *block=(unsigned char*)((uintptr_t)LX(s->va)&~(uintptr_t)15);const unsigned at=(unsigned)(LX(s->va)-block);
    unsigned __int128 current,desired;memcpy(&current,block,16);desired=current;
    if(at+s->size>16 || memcmp((unsigned char*)&current+at,expected,s->size)) return 0;
    memcpy((unsigned char*)&desired+at,value,s->size);
    return gate_code_swap128(block,current,desired);
}
static int lxv_slots_swap(void *expected,void *value) {
    int a=gate_data_swap64(LX(LX_NAV_MOVE_SLOT_A_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
    int b=gate_data_swap64(LX(LX_NAV_MOVE_SLOT_B_VA),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
    return a && b;
}
static int nav_cadence_owned(void) {
    if(!nav_cadence_installed) return 1;
    if(rd64(LX(LX_NAV_MOVE_SLOT_A_VA))!=(uint64_t)(uintptr_t)lxv_move || rd64(LX(LX_NAV_MOVE_SLOT_B_VA))!=(uint64_t)(uintptr_t)lxv_move) return 0;
    for(unsigned i=0;i<4;i++) if(!lxv_site_is(&lxv_sites[i],nav_cadence_patched?lxv_sites[i].far:lxv_sites[i].original)) return 0;
    return 1;
}
/* Before the first admitted frame (idempotent on a loading-UI reload). */
static int nav_cadence_arm(int mode) {
    if(nav_cadence_installed) return stats.active && mode==nav_cadence_mode;
    if((mode!=NAV_CADENCE_FAR && mode!=NAV_CADENCE_OBSERVE) || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {nav_cadence_stage="phase";return 0;}
    for(unsigned i=0;i<4;i++) if(!lxv_site_is(&lxv_sites[i],lxv_sites[i].original)) {nav_cadence_stage="site";return 0;}
    if(rd64(LX(LX_NAV_MOVE_SLOT_A_VA))!=(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA) || rd64(LX(LX_NAV_MOVE_SLOT_B_VA))!=(uint64_t)(uintptr_t)LX(LX_NAV_MOVE_VA)) {nav_cadence_stage="slots";return 0;}
    if(!lxv_slots_swap(LX(LX_NAV_MOVE_VA),(void*)lxv_move)) {lxv_slots_swap((void*)lxv_move,LX(LX_NAV_MOVE_VA));nav_cadence_stage="slot_swap";return 0;}
    lxv_slots_installed=1;
    if(mode==NAV_CADENCE_FAR) {
        nav_cadence_patched=1;
        for(unsigned i=0;i<4;i++) if(!lxv_swap(&lxv_sites[i],lxv_sites[i].original,lxv_sites[i].far)) {
            for(unsigned j=0;j<i;j++) lxv_swap(&lxv_sites[j],lxv_sites[j].far,lxv_sites[j].original);
            nav_cadence_patched=0;lxv_slots_swap((void*)lxv_move,LX(LX_NAV_MOVE_VA));lxv_slots_installed=0;nav_cadence_stage="cas";return 0;
        }
    }
    nav_cadence_mode=mode;nav_cadence_installed=1;nav_cadence_stage="armed";InterlockedExchange(&nav_cadence_active,1);return 1;
}
/* A foreign value keeps the patch marked, and the stats unrestored. */
static void nav_cadence_restore(void) {
    InterlockedExchange(&nav_cadence_active,0);
    if(!nav_cadence_installed) return;
    int all=1;
    if(nav_cadence_patched) {
        for(unsigned i=0;i<4;i++) if(!lxv_site_is(&lxv_sites[i],lxv_sites[i].original) && !lxv_swap(&lxv_sites[i],lxv_sites[i].far,lxv_sites[i].original)) all=0;
        if(all) nav_cadence_patched=0;
    }
    if(lxv_slots_installed) {if(lxv_slots_swap((void*)lxv_move,LX(LX_NAV_MOVE_VA))) lxv_slots_installed=0;else all=0;}
    if(all) {nav_cadence_restored=1;nav_cadence_installed=0;}
}
static int nav_cadence_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d restored=%d stage=%s failures=%lld "
        "probe_init=%d probe_frame=%llu/%llu probe_pins=%llu probe_inits=%llu probe_ids=%u probe_unmapped=%llu probe_first=%llu "
        "params_init=%d params_frame=%llu/%llu params_pins=%llu params_inits=%llu params_ids=%u params_unmapped=%llu params_first=%llu params_created=%llu "
        "key=%d key_calls=%llu key_keyed=%llu key_unmapped=%llu key_full=%llu",
        nav_cadence_mode==NAV_CADENCE_FAR?"far":nav_cadence_mode==NAV_CADENCE_OBSERVE?"observe":"off",nav_cadence_active,nav_cadence_installed,nav_cadence_restored,nav_cadence_stage,(long long)nav_cadence_failures,nav_probe_armed,
        (unsigned long long)(nav_probe_tick==stats.issued?nav_probe_frame_pins:0),(unsigned long long)(nav_probe_tick==stats.issued?nav_probe_frame_inits:0),
        (unsigned long long)nav_probe_pins,(unsigned long long)nav_probe_inits,nav_probe_count,(unsigned long long)nav_probe_unmapped,(unsigned long long)nav_probe_first,nav_params_armed,
        (unsigned long long)(nav_params_tick==stats.issued?nav_params_frame_pins:0),(unsigned long long)(nav_params_tick==stats.issued?nav_params_frame_inits:0),
        (unsigned long long)nav_params_pins,(unsigned long long)nav_params_inits,nav_params_count,(unsigned long long)nav_params_unmapped,(unsigned long long)nav_params_first,(unsigned long long)nav_params_created,
        lxk_armed,(unsigned long long)lxk_calls,(unsigned long long)lxk_keyed,(unsigned long long)lxk_unmapped,(unsigned long long)lxk_full);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_nav_cadence(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);
    const int mode=text && size==3 && !memcmp(text,"far",3)?NAV_CADENCE_FAR:text && size==7 && !memcmp(text,"observe",7)?NAV_CADENCE_OBSERVE:NAV_CADENCE_OFF;
    p_lua_pushnumber(L,mode!=NAV_CADENCE_OFF && nav_cadence_arm(mode));
    return 1;
}
static int lua_nav_cadence_status(lua_State *L) {
    char out[900];int n=accept_ui()?nav_cadence_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_nav_cadence_row(lua_State *L) {p_lua_pushnil(L);return 1;} /* the rows are not ported */
static int lua_nav_cadence_watch(lua_State *L) {p_lua_pushnumber(L,1);return 1;}
static int lua_nav_probe_init(lua_State *L) {p_lua_pushnumber(L,nav_probe_arm());return 1;}
static int lua_nav_params_init(lua_State *L) {p_lua_pushnumber(L,nav_params_arm());return 1;}
static int lua_nav_key(lua_State *L) {p_lua_pushnumber(L,lxk_arm());return 1;}
