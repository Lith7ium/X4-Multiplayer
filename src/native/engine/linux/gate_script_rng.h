/* Per-script RNG streams on the Linux X4 9.00 executable: the Linux twin of lockstep_script_rng.h,
 * lockstep_script_rng_hooks.h and lockstep_script_rng_md.h, whose scopes, keys, rows and status are
 * shared. Included by gate_linux.c only. The mechanism and the census evidence are in those headers.
 *
 * Windows' scopes ride on hooks that other modules own (the interrupt and nested action traces);
 * none of those is ported, so this module has its own, and needs only the world identities:
 *   ExecState_Run 0xd5fbb0 (rdi Exec) and the run range 0xd5fc30 (rdi Exec, rsi program, rdx begin,
 *   rcx end, r8d mode): entry patches, a trampoline with the relocated prologue;
 *   the event handlers of U::AI::AIScriptContext 0x10d4270, AI::AICallStackContext 0x10d4060 and
 *   MD::Cue 0xf799c0 (a tail jump into Cue_CheckConditions 0xf76110): vtable slots 566, one past
 *   Windows' 565 because the Itanium ABI has two destructors. Each slot is its handler's only holder.
 *
 * The three direct calls of Cue_CheckConditions (native_gate_md_timed_rng) are MDScheduler_Update
 * 0xf79d18, Cue_EnterWaiting 0xf7650d and Cue_RaiseCueEvent 0xf78d33. Two change by their rel32 in
 * the aligned 16-byte block, as on Windows. Cue_EnterWaiting's call starts 13 bytes into its block,
 * so its rel32 crosses into the next one and no single compare-exchange covers it: a 5-byte jump
 * goes over the 3-byte mov rdi,r14 before it and the call's first bytes, and the thunk does the mov,
 * pushes the address after the call and jumps to the hook. Nothing branches into those bytes. The
 * hook returns past them, so the return address still names the site.
 *
 * Layouts (Windows in brackets): a context's Value payload is the context + 0xa0 (+0xa8), a cue's
 * the cue + 0xa0 (+0xb0); script descriptor +0xd0 (+0xd8), its name a libstdc++ string at +0x10;
 * the rest as on Windows: Exec program +0x10, PC +0x18, Value +0x38; context entity +0x120, entity
 * runtime ID +8; stack context owner +0xc8; event record +0x18. A cue's or script's name hash is
 * the same on both platforms; whether program lengths and PCs are too, which a mixed session's keys
 * need, shows when rows of both are compared.
 *
 * Windows restores the word in a __finally when an exception crosses a scope. These scopes have no
 * such cleanup: X4's exceptions end the process, so nothing resumes on that word. Enter and leave run
 * on the UI thread only, so the frame chain needs no thread-local storage, which would add a
 * dependency to this module. */
#define SCRIPT_RNG_OWN_PLATFORM
#define SCRIPT_RNG_AI_VT (LX_AI_CONTEXT_VTABLE-LX_IMAGE_BASE)
#define SCRIPT_RNG_STACK_VT (LX_STACK_VTABLE-LX_IMAGE_BASE)
#define SCRIPT_RNG_CUE_VT (LX_CUE_VTABLE-LX_IMAGE_BASE)
#define SCRIPT_RNG_AI_VALUE 0xa0
#define SCRIPT_RNG_MD_VALUE 0xa0
#define SCRIPT_RNG_CONTEXT_SCRIPT 0xd0
#define SCRIPT_RNG_CUE_SLOT (LX_CUE_EVENT_SLOT_VA-LX_IMAGE_BASE)
#define SCRIPT_RNG_CUE_HANDLER (LX_CUE_EVENT_VA-LX_IMAGE_BASE)
#define SCRIPT_RNG_STACK_SLOT (LX_STACK_EVENT_SLOT_VA-LX_IMAGE_BASE)
#define SCRIPT_RNG_STACK_HANDLER (LX_STACK_EVENT_VA-LX_IMAGE_BASE)
#define MD_TIMED_CHECK (LX_CUE_CHECK_VA-LX_IMAGE_BASE)
#define MD_TIMED_THUNK 0x400u        /* jmp [rip] -> script_rng_md_check */
#define LXS_WAIT_THUNK 0x440u        /* mov rdi,r14; push the address after the call; jmp [rip] -> script_rng_md_check */
#define LXS_EXEC_THUNK 0x480u        /* the relocated prologue, then jmp [rip] -> ExecState_Run+5 */
#define LXS_EXEC_HOOK 0x4c0u         /* jmp [rip] -> lxs_exec */
#define LXS_RANGE_THUNK 0x500u       /* the relocated prologue, then jmp [rip] -> the run range+6 */
#define LXS_RANGE_HOOK 0x540u        /* jmp [rip] -> lxs_range */
#define NPC_NAME_LIMIT 512u
#define NPC_STRING_LIBSTDCXX
typedef struct {uintptr_t definition;char cue[256];} npc_name_entry;
static npc_name_entry npc_names[NPC_NAME_LIMIT];
#include "lockstep_cue_name.h"
static int script_rng_read(const void *source,void *dest,size_t bytes) {return gate_read_local(source,dest,bytes);}
static uint64_t *interrupt_rng(void) {return gate_tls_rng(gate_tls_self());}
static int script_rng_slot_swap(unsigned rva,void *expected,void *value) {
    return gate_data_swap64(image+rva,(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* The script's name, a libstdc++ string, and its hash: the same characters and hash as Windows. */
static uint64_t ai_script_with(npc_reader read,void *context,char text[96]) {
    unsigned char *script=NULL;const char *data=NULL;uint64_t size=0;
    if(!read((unsigned char*)context+SCRIPT_RNG_CONTEXT_SCRIPT,&script,8) || !script || !read(script+0x10,&data,8) || !data ||
       !read(script+0x18,&size,8) || !size || size>=96 || !read(data,text,(size_t)size)) return 0;
    text[size]=0;
    uint64_t hash=14695981039346656037ULL;
    for(uint64_t i=0;i<size;i++) {
        unsigned char c=(unsigned char)text[i];
        if(c<32 || c>126 || c=='|' || c==' ') return 0;
        hash=(hash^c)*1099511628211ULL;
    }
    return hash;
}
#define __try
#define __finally
#define AbnormalTermination() 0
#define GetLastError() 0u
#define SetLastError(error) ((void)(error))
#define LONG long /* the status prints the active word with %ld */
#define _Thread_local
static int npc_exec_installed; /* the status' exec_hook: both entries here */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_script_rng.h"
#define world_identity_sealed ((long)world_identity_sealed) /* the status prints it with %ld */
#include "lockstep_script_rng_hooks.h"
#undef world_identity_sealed
static const uint32_t md_timed_site[3]={LX_CUE_TIMED_SITE_VA-LX_IMAGE_BASE,LX_CUE_WAIT_SITE_VA-LX_IMAGE_BASE,LX_CUE_RAISE_SITE_VA-LX_IMAGE_BASE};
#define thunk_slot_ready(offset) 1
#include "lockstep_script_rng_md.h"
#undef thunk_slot_ready
#pragma GCC diagnostic pop
#undef __try
#undef __finally
#undef AbnormalTermination
#undef GetLastError
#undef SetLastError
#undef LONG
#undef _Thread_local
/* The gate's site pins, checked at start, hold the callers and the callee. */
static int md_timed_pinned(void) {return 1;}
static int md_timed_swap(unsigned i,unsigned __int128 expected,unsigned __int128 value) {
    return gate_code_swap128(image+(md_timed_site[i]&~15u),expected,value);
}
/* Original and patched block of site i. */
static int md_timed_prepare(unsigned i) {
    const uint32_t site=md_timed_site[i],at=site&15u;unsigned char *call=image+site,bytes[16];int32_t rel;
    memcpy(bytes,image+(site&~15u),16);
    if(call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);
    if((uintptr_t)(call+5)+(intptr_t)rel!=(uintptr_t)(image+MD_TIMED_CHECK)) return 0;
    if(at<=11) {
        const intptr_t distance=(intptr_t)(thunk+MD_TIMED_THUNK)-(intptr_t)(call+5);
        if(distance<INT32_MIN || distance>INT32_MAX) return 0;
        rel=(int32_t)distance;memcpy(bytes+at+1,&rel,4);
    } else {
        if(at<3 || memcmp(bytes+at-3,"\x4c\x89\xf7",3)) return 0; /* mov rdi,r14 */
        const intptr_t distance=(intptr_t)(thunk+LXS_WAIT_THUNK)-(intptr_t)(call+2);
        if(distance<INT32_MIN || distance>INT32_MAX) return 0;
        rel=(int32_t)distance;bytes[at-3]=0xe9;memcpy(bytes+at-2,&rel,4);
    }
    memcpy(&md_timed_original[i],image+(site&~15u),16);memcpy(&md_timed_patched[i],bytes,16);
    return 1;
}
typedef void (*lxs_exec_fn)(void *);
typedef uintptr_t (*lxs_range_fn)(void *,void *,uint64_t,uint64_t,uint32_t);
static int lxs_exec_installed,lxs_range_installed,lxs_event_installed;
static uint64_t lxs_exec_patched,lxs_range_patched;
static __attribute__((noinline)) void lxs_exec(void *exec) {
    uint64_t *rng=interrupt_rng();
    script_rng_frame scope;const int scoped=script_rng_enter(&scope,SR_EXEC,exec,NULL,0,0,0,rng);
    ((lxs_exec_fn)(void*)(thunk+LXS_EXEC_THUNK))(exec);
    script_rng_leave(&scope,scoped,rng,0);
}
static __attribute__((noinline)) uintptr_t lxs_range(void *exec,void *program,uint64_t begin,uint64_t end,uint32_t mode) {
    uint64_t *rng=interrupt_rng();
    script_rng_frame scope;const int scoped=script_rng_enter(&scope,SR_RANGE,exec,program,begin,end,mode,rng);
    const uintptr_t result=((lxs_range_fn)(void*)(thunk+LXS_RANGE_THUNK))(exec,program,begin,end,mode);
    script_rng_leave(&scope,scoped,rng,0);
    return result;
}
static __attribute__((noinline)) uintptr_t lxs_ai_event(void *self,void *a,void *b,void *c) {
    return script_rng_event_run(SR_EVENT,(script_rng_event_fn)(void*)LX(LX_AI_EVENT_VA),interrupt_rng(),self,a,b,c);
}
static void lxs_thunk_write(unsigned char *page) {
    lxw_jump_write(page+MD_TIMED_THUNK,(void*)script_rng_md_check);
    unsigned char *p=page+LXS_WAIT_THUNK;const uint64_t after=(uint64_t)(uintptr_t)(LX(LX_CUE_WAIT_SITE_VA)+5);
    memcpy(p,"\x4c\x89\xf7\x48\xb8",5);memcpy(p+5,&after,8);p[13]=0x50; /* mov rdi,r14; mov rax,after; push rax */
    lxw_jump_write(p+14,(void*)script_rng_md_check);
    p=page+LXS_EXEC_THUNK;memcpy(p,"\x55\x53\x48\x89\xfb",LX_EXEC_RUN_PROLOGUE); /* push rbp; push rbx; mov rbx,rdi */
    lxw_jump_write(p+LX_EXEC_RUN_PROLOGUE,LX(LX_EXEC_RUN_VA)+LX_EXEC_RUN_PROLOGUE);
    lxw_jump_write(page+LXS_EXEC_HOOK,(void*)lxs_exec);
    p=page+LXS_RANGE_THUNK;memcpy(p,"\x41\x57\x41\x56\x41\x55",LX_EXEC_RANGE_PROLOGUE); /* push r15; push r14; push r13 */
    lxw_jump_write(p+LX_EXEC_RANGE_PROLOGUE,LX(LX_EXEC_RANGE_VA)+LX_EXEC_RANGE_PROLOGUE);
    lxw_jump_write(page+LXS_RANGE_HOOK,(void*)lxs_range);
}
static int lxs_owned(void) {
    return script_rng_owned() && (!lxs_exec_installed || rd64(LX(LX_EXEC_RUN_VA))==lxs_exec_patched) && (!lxs_range_installed || rd64(LX(LX_EXEC_RANGE_VA))==lxs_range_patched) &&
           (!lxs_event_installed || rd64(LX(LX_AI_EVENT_SLOT_VA))==(uint64_t)(uintptr_t)lxs_ai_event);
}
static void lxs_entries_back(void) {
    if(lxs_exec_installed && gate_code_swap64(LX(LX_EXEC_RUN_VA),lxs_exec_patched,LX_EXEC_RUN_ENTRY_ORIGINAL)) lxs_exec_installed=0;
    if(lxs_range_installed && gate_code_swap64(LX(LX_EXEC_RANGE_VA),lxs_range_patched,LX_EXEC_RANGE_ENTRY_ORIGINAL)) lxs_range_installed=0;
    if(lxs_event_installed && script_rng_slot_swap(LX_AI_EVENT_SLOT_VA-LX_IMAGE_BASE,(void*)lxs_ai_event,LX(LX_AI_EVENT_VA))) lxs_event_installed=0;
    npc_exec_installed=lxs_exec_installed && lxs_range_installed;
}
/* Open scopes still restore their own words; this gives the entries and slots back. */
static void lxs_restore(void) {
    if(!script_rng_armed) return;
    script_rng_restore();
    lxs_entries_back();
    script_rng_restored=script_rng_restored && !lxs_exec_installed && !lxs_range_installed && !lxs_event_installed;
}
/* Armed once at boot, before the load (idempotent on a loading-UI reload). The modes, mask and
 * options as on Windows, without the traces Windows' scopes ride on. */
static int script_rng_arm(int mode,unsigned mask,int rows,unsigned options) {
    mode=mode==SCRIPT_RNG_KEY?SCRIPT_RNG_KEY:SCRIPT_RNG_OBSERVE;rows=rows!=0;
    if(script_rng_armed) return stats.active && script_rng_mode==mode && script_rng_mask==mask && script_rng_rows==rows && script_rng_options==options;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !mask || mask>7 || !(mask&(SCRIPT_RNG_AI|SCRIPT_RNG_MD)) || options>SCRIPT_RNG_OPTIONS ||
       ((options&SCRIPT_RNG_OPT_NATURAL) && !rows) || ((options&SCRIPT_RNG_OPT_UNMAPPED) && !(mask&SCRIPT_RNG_AI))) return 0;
    if(!world_identity_installed || !thunk) return 0;
    if(rd64(LX(LX_EXEC_RUN_VA))!=LX_EXEC_RUN_ENTRY_ORIGINAL || rd64(LX(LX_EXEC_RANGE_VA))!=LX_EXEC_RANGE_ENTRY_ORIGINAL ||
       rd64(LX(LX_AI_EVENT_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LX_AI_EVENT_VA) || rd64(LX(LX_STACK_EVENT_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LX_STACK_EVENT_VA) ||
       rd64(LX(LX_CUE_EVENT_SLOT_VA))!=(uint64_t)(uintptr_t)LX(LX_CUE_EVENT_VA)) return 0;
    if(!(lxs_exec_installed=lxw_patch(LX(LX_EXEC_RUN_VA),LX_EXEC_RUN_ENTRY_ORIGINAL,LXS_EXEC_HOOK,&lxs_exec_patched))) goto refuse;
    if(!(lxs_range_installed=lxw_patch(LX(LX_EXEC_RANGE_VA),LX_EXEC_RANGE_ENTRY_ORIGINAL,LXS_RANGE_HOOK,&lxs_range_patched))) goto refuse;
    if(!(lxs_event_installed=script_rng_slot_swap(LX_AI_EVENT_SLOT_VA-LX_IMAGE_BASE,LX(LX_AI_EVENT_VA),(void*)lxs_ai_event))) goto refuse;
    if((mask&SCRIPT_RNG_MD) && !(script_rng_cue_installed=script_rng_slot_swap(SCRIPT_RNG_CUE_SLOT,image+SCRIPT_RNG_CUE_HANDLER,(void*)script_rng_cue_event))) goto refuse;
    if((mask&SCRIPT_RNG_AI) && !(script_rng_stack_installed=script_rng_slot_swap(SCRIPT_RNG_STACK_SLOT,image+SCRIPT_RNG_STACK_HANDLER,(void*)script_rng_stack_event))) goto refuse;
    npc_exec_installed=1;script_rng_exec_owned=1;
    script_rng_mode=mode;script_rng_mask=mask;script_rng_rows=rows;script_rng_options=options;script_rng_armed=1;
    script_rng_cache_flush();script_rng_clock_start();InterlockedExchange(&script_rng_active,1);return 1;
refuse:
    script_rng_unswap();lxs_entries_back();return 0;
}
static int lua_script_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double mask=p_lua_tonumber(L,2),rows=p_lua_tonumber(L,3);
    double options=p_lua_tonumber(L,4); /* absent: 0 */
    int m=mode && length==3 && !memcmp(mode,"key",3)?SCRIPT_RNG_KEY:mode && length==7 && !memcmp(mode,"observe",7)?SCRIPT_RNG_OBSERVE:-1;
    p_lua_pushnumber(L,m>=0 && isfinite(mask) && mask>=1 && mask<=7 && floor(mask)==mask && (rows==0 || rows==1) &&
        isfinite(options) && options>=0 && options<=SCRIPT_RNG_OPTIONS && floor(options)==options &&
        script_rng_arm(m,(unsigned)mask,rows==1,(unsigned)options));
    return 1;
}
static int lua_script_rng_status(lua_State *L) {
    char out[2560];int n=accept_ui()?script_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_script_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[640];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=script_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* Tells the probe the scopes need none of Windows' traces (native_gate_interrupt_diag, native_gate_nested_actions). */
static int lua_script_rng_own_entries(lua_State *L) {p_lua_pushnumber(L,1);return 1;}
static int lua_md_timed_rng(lua_State *L) {p_lua_pushnumber(L,md_timed_arm());return 1;}
static int lua_md_timed_rng_status(lua_State *L) {
    char out[384];int n=accept_ui()?md_timed_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
