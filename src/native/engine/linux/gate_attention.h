/* Every portable object's attention timer on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_attention.h's 'all' (native_gate_attention_all, with the hook from native_gate_attention_rng or
 * _initial_refresh), whose first-call deadline, keyed word, table and status are shared. Included by gate_linux.c
 * only. The mechanism is in that header: the load leaves each object's next due at a value from that client's
 * loading frames, so the first re-evaluation lands on a client-dependent tick and draws from the natural word.
 *
 * win-sub4 shows why it comes now: of about 23,000 frame-level draws, only two call chains are ever on one client
 * alone, and this timer is one of them (49 of 113 draws, the first at tick 47). The Linux word first parts at tick
 * 46-47 once the nav deadlines agree (runs 24, 24L, 25L).
 *
 * Linux site: PeriodicAttentionRefresh 0x1465030 (Windows 0x6d1ff0): when the thread's game time (fs:-0x2728)
 * reaches object+0x2d8 (Windows +0x2e0) it sets +0x2d8 = now + 0.75 + the thread word (fs:-0x2720) as a float of
 * 0.5, inlined PCG, and tail-jumps to SetAttention. Its first 5 bytes (push rbp; push rbx; mov rbx,rdi) move into
 * a trampoline; the entry's aligned 8 bytes change in one compare-exchange.
 *
 * Not ported: the fixture actors' own path (attention_execute; the fixtures' actors are not ported) and the
 * diagnostic. With only the hook, every call runs natively. */
#define ATTENTION_OWN_PLATFORM
#define ATTENTION_DUE LX_ATTENTION_DUE
#define LXA_THUNK 0x780u /* the relocated prologue, then jmp [rip] -> the timer + 5 */
#define LXA_HOOK 0x7c0u  /* jmp [rip] -> lxa_refresh */
#define __try
#define __finally
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define AbnormalTermination() 0 /* X4's exceptions end the process */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable" /* the fixture actors' counters and flags */
#include "lockstep_attention.h"
#pragma GCC diagnostic pop
#undef __try
#undef __finally
#undef GetLastError
#undef SetLastError
#undef AbnormalTermination
static void lxa_refresh(void *self) {
    attention_fn original=(attention_fn)(void*)(thunk+LXA_THUNK);
    if(attention_all_armed && stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) {
        unsigned char *base=gate_tls_self();double time=NAN;
        if(base) memcpy(&time,gate_tls_time(base),8);
        attention_all_apply(original,self,gate_tls_rng(base),time);return;
    }
    original(self);
}
static void lxa_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXA_HOOK,(void*)lxa_refresh);
    unsigned char *p=page+LXA_THUNK;memcpy(p,"\x55\x53\x48\x89\xfb",LX_ATTENTION_PROLOGUE); /* push rbp; push rbx; mov rbx,rdi */
    lxw_jump_write(p+LX_ATTENTION_PROLOGUE,LX(LX_ATTENTION_VA)+LX_ATTENTION_PROLOGUE);
}
/* From the start options, in the boot before loading; a rerun keeps the installed hook. */
static int attention_install(void) {
    if(attention_installed) return 1;
    if(!boot_mode || !thunk || rd64(LX(LX_ATTENTION_VA))!=LX_ATTENTION_ENTRY_ORIGINAL) return 0;
    attention_original_word=LX_ATTENTION_ENTRY_ORIGINAL;
    if(!lxw_patch(LX(LX_ATTENTION_VA),attention_original_word,LXA_HOOK,&attention_patched_word)) return 0;
    attention_installed=1;attention_restored=0;return 1;
}
static void attention_restore(void) {
    if(!attention_installed) return;
    attention_restored=gate_code_swap64(LX(LX_ATTENTION_VA),attention_patched_word,attention_original_word);
    if(attention_restored) attention_installed=0;
}
static int attention_owned(void) {return !attention_installed || rd64(LX(LX_ATTENTION_VA))==attention_patched_word;}
static int lua_attention_all(lua_State *L) {p_lua_pushnumber(L,attention_all_arm());return 1;}
static int lua_attention_all_status(lua_State *L) {
    char out[512];int n=accept_ui()?attention_all_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
