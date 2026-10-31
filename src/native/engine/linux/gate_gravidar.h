/* The Gravidar on the Linux X4 9.00 executable: the gated serial pass, keyed units, its thread's
 * game time and the sector round-robin. Included by gate_linux.c only. What each part fixes and
 * the evidence are in lockstep_gravidar.h and lockstep_ai_schedule.h; the second is shared, the
 * first is tied to the Windows actor trace, so its core is written again here without that trace.
 *
 * Linux sites: the MoveWorker publishes at 0x12125a9 (AI::Gravidar 0x3dfa4a0, publish 0x1109380,
 * rdi worker, esi reset), a rel32 inside one aligned qword. ActiveGravidarUnit's apply 0x10fd7b0 is
 * held only by its vtable slot +0x40 (Windows +0x38). The worker's fields: running +0x40, enabled
 * +0x44, list lock +0x7090, sector list +0x70c0 (32-byte entries), the player's index +0x70d8, ready
 * lock +0x7168, ready byte +0x7198. X4's mutex is recursive, its pthread mutex at +8, 0x30 bytes
 * (Windows' critical section 0x28, hence the offsets). A unit's and a sector's runtime ID at +8.
 *
 * Windows reads a thread's TLS through its TEB. Linux has no such view of another thread, and X4
 * keeps no thread handle, so the gate finds the thread named "Gravidar" in /proc/self/task and has
 * it answer one signal with its TLS base and stack pointer. That happens once, while it is idle.
 *
 * The round-robin cursor is a stack local of Run ([rbp-0xf0]). Linux's Run keeps rbp, and stores
 * worker+0x40 at [rbp-0xa0], so its frame is that word with a return address 0xa8 above it. */
#include <dirent.h>
#define LXG_READY_LOCK 0x7168u
#define LXG_READY 0x7198u
#define LXG_LIST_LOCK 0x7090u
#define LXG_LIST 0x70c0u
#define LXG_PRIORITY 0x70d8u
#define LXG_RUN_MARKER 0xa8u /* below the return address: worker+0x40 */
#define LXG_RUN_CURSOR 0xf8u /* below the return address: the cursor */
#define LXG_THUNK_HOOK 0x280u /* jmp [rip] -> gravidar_defer */
#define GRAVIDAR_FRAME_TIMEOUT 5000u /* ms: Windows 1000; a loaded virtual machine should not stop a run on it */
typedef void (*gravidar_publish_fn)(void *,unsigned char);
typedef void (*gravidar_unit_fn)(void *,void *,void *);
typedef void (*lxg_mutex_fn)(void *);
static void lxg_lock(void *mutex) {((lxg_mutex_fn)(void*)LX(LX_MUTEX_LOCK_VA))(mutex);}
static void lxg_unlock(void *mutex) {((lxg_mutex_fn)(void*)LX(LX_MUTEX_UNLOCK_VA))(mutex);}
static uint64_t gravidar_original_word,gravidar_patched_word,gravidar_frames,gravidar_waits,gravidar_timeouts;
static uint64_t gravidar_time_syncs,gravidar_time_stale,gravidar_boot_skipped,gravidar_boot_resets,gravidar_boot_drains;
static volatile int64_t gravidar_keyed,gravidar_unkeyed,gravidar_key_failures,gravidar_time_failures;
static volatile int gravidar_in_frame,gravidar_key_all,gravidar_time_sync;
static int gravidar_unit_installed;
static uint64_t *volatile gravidar_thread_rng;
static unsigned char *gravidar_thread_slot;
static uintptr_t lxg_thread_sp;
static volatile int lxg_answered;
static unsigned char *volatile lxg_answer_tls;
static volatile uintptr_t lxg_answer_sp;
static void lxg_answer(int sig,siginfo_t *info,void *context) {
    (void)sig;(void)info;
    ucontext_t *u=context;
    lxg_answer_sp=(uintptr_t)u->uc_mcontext.gregs[REG_RSP];lxg_answer_tls=gate_tls_self();
    __atomic_store_n(&lxg_answered,1,__ATOMIC_SEQ_CST);
}
/* The one thread with this name, or 0. */
static pid_t lxg_thread_id(void) {
    DIR *tasks=opendir("/proc/self/task");if(!tasks) return 0;
    pid_t found=0;unsigned matches=0;struct dirent *entry;
    while((entry=readdir(tasks))) {
        char path[64],name[32];
        if(entry->d_name[0]=='.' || snprintf(path,sizeof(path),"/proc/self/task/%s/comm",entry->d_name)>=(int)sizeof(path)) continue;
        int file=open(path,O_RDONLY|O_CLOEXEC);if(file<0) continue;
        ssize_t got=read(file,name,sizeof(name)-1);close(file);
        if(got!=9 || memcmp(name,"Gravidar\n",9)) continue;
        found=(pid_t)atoi(entry->d_name);++matches;
    }
    closedir(tasks);return matches==1?found:0;
}
/* Once: its TLS base and a stack pointer inside it. The signal's handler is ours for the moment. */
static int lxg_thread_find(void) {
    if(gravidar_thread_slot) return 1;
    pid_t thread=lxg_thread_id();int number=SIGRTMAX-3,ok=0;
    struct sigaction own,prior;memset(&own,0,sizeof(own));sigemptyset(&own.sa_mask);own.sa_sigaction=lxg_answer;own.sa_flags=SA_SIGINFO|SA_RESTART;
    if(!thread) return 0;
    while(number>SIGRTMAX-8 && (sigaction(number,NULL,&prior) || prior.sa_handler!=SIG_DFL)) --number; /* a free one */
    if(number<=SIGRTMAX-8 || sigaction(number,&own,NULL)) return 0;
    __atomic_store_n(&lxg_answered,0,__ATOMIC_SEQ_CST);
    if(!syscall(SYS_tgkill,getpid(),thread,number)) {
        for(unsigned i=0;i<2000 && !__atomic_load_n(&lxg_answered,__ATOMIC_SEQ_CST);i++) usleep(1000);
        ok=__atomic_load_n(&lxg_answered,__ATOMIC_SEQ_CST) && lxg_answer_tls && lxg_answer_sp;
    }
    /* A late answer lands in the default action only if it never came within 2 s: fail closed, stay installed. */
    if(ok) {
        sigaction(number,&prior,NULL);
        gravidar_thread_slot=lxg_answer_tls;lxg_thread_sp=lxg_answer_sp;
    }
    return ok;
}
static int gravidar_wait(void *worker,unsigned timeout) {
    unsigned char *w=worker;void *vtable=NULL;uint32_t running=0,enabled=0;ULONGLONG started=GetTickCount64();int waited=0;
    for(;;) {
        if(!gate_read_local(w,&vtable,8) || vtable!=(void*)LX(LX_GRAVIDAR_VTABLE) ||
           !gate_read_local(w+0x40,&running,4) || running!=2 || !gate_read_local(w+0x44,&enabled,4) || enabled!=1) return 0;
        lxg_lock(w+LXG_READY_LOCK);
        unsigned char ready=*(volatile unsigned char*)(w+LXG_READY);
        lxg_unlock(w+LXG_READY_LOCK);
        if(ready==1) return 1;
        if(ready!=0) return 0;
        if(!waited) {++gravidar_waits;waited=1;}
        if(GetTickCount64()-started>=timeout) {++gravidar_timeouts;return 0;}
        usleep(1000);
    }
}
static int gravidar_frame(gravidar_publish_fn original,void *worker,unsigned timeout) {
    if(!gravidar_wait(worker,timeout)) return 0;
    if((gravidar_key_all || gravidar_time_sync) && !gravidar_thread_slot) {
        if(!lxg_thread_find()) {InterlockedIncrement64(&gravidar_time_failures);return 0;}
    }
    if(gravidar_thread_slot && !gravidar_thread_rng) gravidar_thread_rng=gate_tls_rng(gravidar_thread_slot);
    /* The worker reported ready: its thread is idle, so its TLS copies can be written. */
    if(gravidar_key_all && gravidar_thread_rng) *gravidar_thread_rng=lockstep_seed(0x4752415646524d45ULL,stats.issued,0,0,0,0);
    if(gravidar_time_sync) {
        double now,before;
        lxg_lock(LX(LX_GAME_TIME_LOCK_VA));memcpy(&now,LX(LX_GAME_TIME_GLOBAL_VA),8);lxg_unlock(LX(LX_GAME_TIME_LOCK_VA));
        memcpy(&before,gravidar_thread_slot+GATE_TLS_LINUX_TIME,8);
        if(!isfinite(now) || now<before) {InterlockedIncrement64(&gravidar_time_failures);return 0;}
        if(before!=now) ++gravidar_time_stale;
        memcpy(gravidar_thread_slot+GATE_TLS_LINUX_PREVIOUS_TIME,&before,8);memcpy(gravidar_thread_slot+GATE_TLS_LINUX_TIME,&now,8);++gravidar_time_syncs;
    }
    __atomic_store_n(&gravidar_in_frame,1,__ATOMIC_SEQ_CST);
    original(worker,0);++gravidar_frames;
    int ok=gravidar_wait(worker,timeout);
    __atomic_store_n(&gravidar_in_frame,0,__ATOMIC_SEQ_CST);
    return ok;
}
/* A pass in flight when the gate admits frame 1 would post scan events partly before and after it. */
static int gravidar_boot_drain(void) {
    void *worker=*(void**)LX(LX_GRAVIDAR_WORKER_VA);
    if(!worker) return 0;
    int ok=gravidar_wait(worker,5000);if(ok) ++gravidar_boot_drains;
    return ok;
}
/* The MoveWorker's own publish, before admission: from frame 1 the gated frame publishes instead. */
static void gravidar_defer(void *worker,unsigned char reset) {
    if(stats.active && boot_phase==3) {
        if(reset) InterlockedIncrement64(&gravidar_failures);
        return;
    }
    if(gravidar_time_sync && stats.active && boot_mode && (boot_phase==1 || boot_phase==2)) {
        if(!reset) {++gravidar_boot_skipped;return;}
        ++gravidar_boot_resets;
    }
    ((gravidar_publish_fn)(void*)LX(LX_GRAVIDAR_PUBLISH_VA))(worker,reset);
}
/* Same key as Windows: tick and the unit's saved ID; the thread's word is given back. */
static void gravidar_unit(void *unit,void *queue,void *result) {
    const gravidar_unit_fn native=(gravidar_unit_fn)(void*)LX(LX_GRAVIDAR_UNIT_APPLY_VA);
    if(!gravidar_key_all || !gravidar_in_frame || !stats.active || boot_phase!=3) {native(unit,queue,result);return;}
    uint64_t runtime=0,saved=0;unsigned klass=WORLD_KEY_NONE;unsigned char *base=gate_tls_self();
    if(!gate_read_local((unsigned char*)unit+8,&runtime,8) || !(saved=world_identity_key(runtime,&klass)) || !base) {
        InterlockedIncrement64(&gravidar_unkeyed);native(unit,queue,result);return;
    }
    uint64_t *rng=gate_tls_rng(base),prior=*rng;
    if(gravidar_thread_rng!=rng) InterlockedIncrement64(&gravidar_key_failures);
    *rng=lockstep_seed(0x4752415649444152ULL,stats.issued,saved,0,0,0);
    native(unit,queue,result);
    *rng=prior;InterlockedIncrement64(&gravidar_keyed);
}
static void lxg_thunk_write(unsigned char *page) {lxw_jump_write(page+LXG_THUNK_HOOK,(void*)gravidar_defer);}
static int gravidar_owned(void) {
    uint64_t slot=0;
    return (!gravidar_installed || rd64(LX(LX_GRAVIDAR_CALL_WORD_VA))==gravidar_patched_word) &&
           (!gravidar_unit_installed || (gate_read_local(LX(LX_GRAVIDAR_UNIT_SLOT_VA),&slot,8) && slot==(uint64_t)(uintptr_t)gravidar_unit));
}
static void gravidar_restore(void) {
    int owned=gravidar_installed || gravidar_unit_installed;
    if(gravidar_installed && gate_code_swap64(LX(LX_GRAVIDAR_CALL_WORD_VA),gravidar_patched_word,gravidar_original_word)) gravidar_installed=0;
    if(gravidar_unit_installed && gate_data_swap64(LX(LX_GRAVIDAR_UNIT_SLOT_VA),(uint64_t)(uintptr_t)gravidar_unit,(uint64_t)(uintptr_t)LX(LX_GRAVIDAR_UNIT_APPLY_VA))) gravidar_unit_installed=0;
    if(owned) gravidar_restored=!gravidar_installed && !gravidar_unit_installed;
}
static int gravidar_install(void) {
    unsigned char *word=LX(LX_GRAVIDAR_CALL_WORD_VA);uint64_t slot=0;int32_t rel;
    if(!boot_mode || !(path_options&1) || !thunk || word[0]!=0xf6 || word[1]!=0xe8) return 0;
    memcpy(&rel,word+2,4);if((uintptr_t)(word+6)+(intptr_t)rel!=(uintptr_t)LX(LX_GRAVIDAR_PUBLISH_VA)) return 0;
    if(!gate_read_local(LX(LX_GRAVIDAR_UNIT_SLOT_VA),&slot,8) || slot!=(uint64_t)(uintptr_t)LX(LX_GRAVIDAR_UNIT_APPLY_VA)) return 0;
    intptr_t distance=(intptr_t)(thunk+LXG_THUNK_HOOK)-(intptr_t)(word+6);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    gravidar_original_word=rd64(word);gravidar_patched_word=gravidar_original_word;
    rel=(int32_t)distance;memcpy((unsigned char*)&gravidar_patched_word+2,&rel,4);
    if(!gate_code_swap64(word,gravidar_original_word,gravidar_patched_word)) return 0;
    gravidar_installed=1;
    if(!gate_data_swap64(LX(LX_GRAVIDAR_UNIT_SLOT_VA),slot,(uint64_t)(uintptr_t)gravidar_unit)) {gravidar_restore();return 0;}
    gravidar_unit_installed=1;return 1;
}
static int gravidar_key_arm(void) {
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return gravidar_key_all!=0;
    __atomic_store_n(&gravidar_key_all,1,__ATOMIC_SEQ_CST);return 1;
}
static int gravidar_time_arm(void) {
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return gravidar_time_sync!=0;
    __atomic_store_n(&gravidar_time_sync,1,__ATOMIC_SEQ_CST);return 1;
}
static int gravidar_key_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d time_sync=%d installed=%d frames=%llu keyed=%lld unkeyed=%lld time_syncs=%llu time_stale=%llu "
        "time_failures=%lld boot_skipped=%llu boot_resets=%llu boot_drains=%llu failures=%lld created=0",gravidar_key_all,gravidar_time_sync,gravidar_unit_installed,(unsigned long long)gravidar_frames,
        (long long)gravidar_keyed,(long long)gravidar_unkeyed,(unsigned long long)gravidar_time_syncs,(unsigned long long)gravidar_time_stale,
        (long long)gravidar_time_failures,(unsigned long long)gravidar_boot_skipped,(unsigned long long)gravidar_boot_resets,(unsigned long long)gravidar_boot_drains,(long long)gravidar_key_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
/* The sector round-robin, shared. */
#define AI_SCHED_OWN_PLATFORM
#define AI_SCHED_WORKER (LX_GRAVIDAR_WORKER_VA-LX_IMAGE_BASE)
#define AI_SCHED_VTABLE (LX_GRAVIDAR_VTABLE-LX_IMAGE_BASE)
#define AI_SCHED_PUBLISH (LX_GRAVIDAR_PUBLISH_VA-LX_IMAGE_BASE)
#define AI_SCHED_W_READY_LOCK LXG_READY_LOCK
#define AI_SCHED_W_READY LXG_READY
#define AI_SCHED_W_LIST LXG_LIST
#define AI_SCHED_W_PRIORITY LXG_PRIORITY
#define AI_SCHED_W_LIST_LOCK LXG_LIST_LOCK
#define CRITICAL_SECTION void
#define EnterCriticalSection(mutex) lxg_lock(mutex)
#define LeaveCriticalSection(mutex) lxg_unlock(mutex)
#define Sleep(ms) usleep((ms)*1000u)
#define __try
#define __finally
#define LONG long /* the status prints the armed word with %ld */
#include "lockstep_ai_schedule.h"
#undef __try
#undef __finally
#undef LONG
#undef CRITICAL_SECTION
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef Sleep
static uint64_t lxg_return_address;
static int ai_sched_frame_ok(void) {
    uint64_t back=0,marker=0;
    return ai_sched_entry && gate_read_local((void*)ai_sched_entry,&back,8) && back==lxg_return_address &&
        gate_read_local((void*)(ai_sched_entry-LXG_RUN_MARKER),&marker,8) && marker==(uint64_t)(uintptr_t)ai_sched_worker+0x40;
}
/* The worker must be the one bound first; the frame is found once and then re-checked. */
static int ai_sched_bind(void) {
    unsigned char *worker=*(unsigned char**)(image+AI_SCHED_WORKER);void *vtable=NULL;
    if(!worker || !gate_read_local(worker,&vtable,8) || vtable!=image+AI_SCHED_VTABLE) {ai_sched_stage="worker";return 0;}
    if(ai_sched_worker) {
        if(worker!=ai_sched_worker || !ai_sched_frame_ok()) {ai_sched_stage="frame_moved";return 0;}
        return 1;
    }
    if(!lxg_thread_find()) {ai_sched_stage="thread";return 0;}
    uintptr_t low=lxg_thread_sp&~(uintptr_t)7,entry=0;size_t bytes=0;unsigned matches=0;
    while(bytes+GATE_PAGE<=AI_SCHED_WINDOW && gate_read_local((void*)(low+bytes),(unsigned char*)ai_sched_window+bytes,GATE_PAGE-((low+bytes)&(GATE_PAGE-1)))) {
        bytes+=GATE_PAGE-((low+bytes)&(GATE_PAGE-1));
    }
    const uint64_t marker=(uint64_t)(uintptr_t)worker+0x40,text=(uint64_t)(uintptr_t)LX(LX_TEXT_VA),text_end=(uint64_t)(uintptr_t)LX(LX_TEXT_END_VA);
    for(size_t i=0;i+LXG_RUN_MARKER/8<bytes/8;i++) {
        uint64_t back=ai_sched_window[i+LXG_RUN_MARKER/8];
        if(ai_sched_window[i]==marker && back>=text && back<text_end) {entry=low+i*8+LXG_RUN_MARKER;++matches;}
    }
    if(matches!=1 || entry<low+LXG_RUN_CURSOR) {ai_sched_stage="frame";return 0;}
    gate_read_local((void*)entry,&lxg_return_address,8);
    ai_sched_worker=worker;ai_sched_entry=entry;ai_sched_slot=(volatile uint64_t*)(entry-LXG_RUN_CURSOR);++ai_sched_binds;
    if(!ai_sched_frame_ok()) {ai_sched_stage="frame";return 0;}
    return 1;
}
/* A sector without a saved ID: its macro's name (U::Macro, name getter slot 19 returns +0x20, a libstdc++ string). */
static int ai_sched_macro(const unsigned char *component,char out[96]) {
    unsigned char *macro=NULL;void *vtable=NULL,*getter=NULL;uint64_t length=0;const unsigned char *data=NULL;
    if(!gate_read_local(component+0x60,&macro,8) || !macro || !gate_read_local(macro,&vtable,8) || vtable!=(void*)LX(LX_MACRO_VTABLE) ||
       !gate_read_local((unsigned char*)vtable+19*8,&getter,8) || getter!=(void*)LX(LX_MACRO_NAME_GETTER_VA) ||
       !gate_read_local(macro+0x28,&length,8) || !length || length>=96 || !gate_read_local(macro+0x20,&data,8) || !data) return 0;
    if(!gate_read_local(data,out,(size_t)length+1) || out[length]) return 0;
    for(uint64_t i=0;i<length;i++) {char c=out[i];if(!((c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_')) return 0;}
    return 1;
}
#include "gate_gravidar_wide.h"
/* Before the pass is released: the schedule's check, the gated Gravidar pass, the address order back. */
static int lxg_before_release(void) {
    if(ai_sched_armed && !ai_sched_frame()) {InterlockedIncrement64(&ai_sched_failures);finish(84);return 0;}
    if(gravidar_installed) {
        gravidar_publish_fn publish=(ai_sched_mask&AI_SCHED_ORDER)?ai_sched_publish:(gravidar_publish_fn)(void*)LX(LX_GRAVIDAR_PUBLISH_VA);
        if(gwide_mode==GWIDE_MODE_SORT) {gwide_inner=publish;publish=gwide_publish;} /* the zone groups in portable order for the pass */
        if(!gravidar_frame(publish,*(void**)LX(LX_GRAVIDAR_WORKER_VA),GRAVIDAR_FRAME_TIMEOUT)) {InterlockedIncrement64(&gravidar_failures);finish(62);}
    }
    /* The pass is joined: the zone groups, then the sector list, back into address order. */
    if(stats.active && gwide_permuted && !gwide_settle()) finish(GWIDE_STOP_REASON);
    if(stats.active && ai_sched_armed && !ai_sched_settle()) {InterlockedIncrement64(&ai_sched_failures);finish(84);}
    return 1;
}
static int lua_gravidar_key(lua_State *L) {p_lua_pushnumber(L,gravidar_key_arm());return 1;}
static int lua_gravidar_time(lua_State *L) {p_lua_pushnumber(L,gravidar_time_arm());return 1;}
static int lua_gravidar_key_status(lua_State *L) {
    char out[384];int n=accept_ui()?gravidar_key_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* The Windows actor trace is not ported: no registered ship ever has a unit row. */
static int lua_gravidar(lua_State *L) {p_lua_pushlstring(L,"tick=0 calls=0",14);return 1;}
static int lua_ai_schedule(lua_State *L) {
    double mask=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mask) && mask>=1 && mask<=7 && floor(mask)==mask && ai_sched_arm((unsigned)mask));return 1;
}
static int lua_ai_schedule_status(lua_State *L) {
    char out[1024];int n=accept_ui()?ai_sched_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_ai_schedule_fallbacks(lua_State *L) {
    char out[1200];int n=accept_ui()?ai_sched_fallbacks(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
