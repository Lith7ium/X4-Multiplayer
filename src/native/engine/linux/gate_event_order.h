/* Portable delivery order of due events on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_event_order.h, whose sort, order hash, rows and status are shared. Included by
 * gate_linux.c only. The mechanism is in that header.
 *
 * Linux sites: ProcessDueEvents 0x16c6320 delivers the batch 0x3eedfa0 (count 0x3eedf90), the same
 * 0x38-byte slots as Windows (+0x18 source, +0x28 event, +0x30 time), through DeliverEvent 0x16c5db0
 * at 0x16c6b63, return 0x16c6b68: rdi source, rsi event, xmm0 time. The rel32 ends one aligned
 * qword, so one compare-exchange changes it. U::UIElement's vtable is 0x3d3b040 (view-local runs).
 * The event RNG scope of lockstep_event_rng.h is not ported: deliveries stay on the caller's word.
 * Created identities are not ported either: a source without a saved ID ranks by its runtime ID. */
#define EVENT_ORDER_OWN_PLATFORM
#define EVENT_ORDER_DELIVER (LX_EVENT_DELIVER_VA-LX_IMAGE_BASE)
#define EVENT_ORDER_BATCH (LX_EVENT_BATCH_VA-LX_IMAGE_BASE)
#define EVENT_ORDER_COUNT (LX_EVENT_COUNT_VA-LX_IMAGE_BASE)
#define EVENT_ORDER_VIEW_LOCAL (LX_UI_ELEMENT_VTABLE-LX_IMAGE_BASE)
#define EVENT_ORDER_TLS_TIME GATE_TLS_LINUX_TIME
#define EVENT_ORDER_ROW_TICKS_MAX 20000 /* long census runs: about 60 rows a tick, drained every tick */
#define event_rng_deliver(native,source,event,due) (native)((source),(event),(due))
#define LONG long /* the status prints the armed word with %ld */
#include "lockstep_event_order.h"
#undef LONG
#undef event_rng_deliver
#define LXE_THUNK_HOOK 0x240u /* jmp [rip] -> event_order_deliver */
static void lxe_thunk_write(unsigned char *page) {lxw_jump_write(page+LXE_THUNK_HOOK,(void*)event_order_deliver);}
static int event_order_owned(void) {return !event_order_installed || rd64(LX(LX_EVENT_CALL_WORD_VA))==event_order_patched_word;}
/* A loading-UI reload reruns configuration: accept the installed owner. */
static int event_order_arm(void) {
    if(event_order_armed) return stats.active && event_order_installed;
    unsigned char *word=LX(LX_EVENT_CALL_WORD_VA);
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !thunk || rd64(word)!=LX_EVENT_CALL_WORD_ORIGINAL) return 0;
    intptr_t distance=(intptr_t)(thunk+LXE_THUNK_HOOK)-(intptr_t)(word+8);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    event_order_original_word=LX_EVENT_CALL_WORD_ORIGINAL;event_order_patched_word=event_order_original_word;
    int32_t rel=(int32_t)distance;memcpy((unsigned char*)&event_order_patched_word+4,&rel,4);
    if(!gate_code_swap64(word,event_order_original_word,event_order_patched_word)) return 0;
    event_order_installed=1;InterlockedExchange(&event_order_armed,1);return 1;
}
static void event_order_restore(void) {
    if(!event_order_installed) return;
    event_order_restored=gate_code_swap64(LX(LX_EVENT_CALL_WORD_VA),event_order_patched_word,event_order_original_word);
    if(event_order_restored) event_order_installed=0;
}
static int lua_event_order(lua_State *L) {p_lua_pushnumber(L,event_order_arm());return 1;}
static int lua_event_order_status(lua_State *L) {
    char out[768];int n=accept_ui()?event_order_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_event_rows(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=0 && ticks<=EVENT_ORDER_ROW_TICKS_MAX && floor(ticks)==ticks && event_order_rows_arm((unsigned)ticks));return 1;
}
static int lua_event_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=event_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
