/* Opt-in X4 9.00 Linux pre-FrameTick admission: the core of the Windows gate
 * (../lockstep_gate.c) on the native Linux executable (no Wine). Separate from
 * x4lan_net. Loaded by the probe through package.loadlib like the Windows DLL,
 * it exposes the same `gate` Lua table for the core subset and the same wire:
 * protocol 2 (two 8-hex commands), 3 (N=2..8 opaque blobs) and 4 (lookahead).
 *
 * The main loop has acquired semSimDone (MoveWorker idle) before its FrameTick
 * call; this gate replaces that call's target and waits here for the relay
 * permit, exactly like the Windows gate. Fixed dt (1/60, or 1/hz with
 * native_gate_tick_hz, lockstep_tick.h) uses the natively validated x4mp-agent fixed60
 * patch (two qwords in FrameTick). Default off,
 * fail closed: every refusal leaves the executable untouched, every stop
 * restores what was patched and never overwrites a foreign owner.
 *
 * Shared with Windows unchanged: lockstep_wire.h, lockstep_lookahead.h,
 * lockstep_seed_core.h, lockstep_gate_{common,timing,exports}.h and the
 * lockstep_gate_lua_{abi,wire,misc,inputs}.h Lua wrappers. Linux-only: this file,
 * gate_platform.h (Win32 spellings + Linux primitives) and gate_sites.h (pins).
 * World identities and the world hash (gate_world.h) give the Windows WORLD_IMPORTED_1/2
 * rows and summaries, so a census compares a Linux with a Windows client.
 * Not ported yet: every fix module (see reports/LOCKSTEP-LINUX-GATE-CORE-20261029.txt);
 * their Lua methods are absent, so a probe configuration that needs one refuses.
 * No original executable bytes or files are changed on disk. */
#define _GNU_SOURCE
#ifdef GATE_LINUX_TEST
#define GATE_TLS_TEST /* test build only: thread "TLS" from a buffer of this module */
#endif
#include "lockstep_sha256.h"
#include "gate_platform.h"
#include "lockstep_wire.h"
#define GATE_EXPORT __attribute__((visibility("default")))
#include "gate_sites.h"

/* Engine addresses relative to the verified image (0x400000 in X4, a fake image in tests). */
#define LX(va) (image+((uintptr_t)(va)-LX_IMAGE_BASE))
#define GAME_RVA (LX_GAME_VA-LX_IMAGE_BASE) /* lockstep_lookahead.h reads image+GAME_RVA */
/* Snapshot loads on a Linux host (VPS, lavapipe) are slower than the Windows
 * 120 s; still bounded. Unmeasured: refine after the first native boot. */
#define LX_BOOT_TIMEOUT_MS 600000u
#define LX_PARK_MS 2000u
#define LX_STOP_UNPORTED_OPTION 105u
#define LX_SEED_DOMAIN 0x4e41544956454741ULL /* the Windows path_release seed domain */

typedef struct {
    uint64_t issued, completed, waits, report_tick;
    double game, real, wait_game_delta, max_dt_error;
    uint32_t active, reason, restored, slot;
} gate_stats;
static gate_stats stats;
static unsigned char *image, *thunk;
static unsigned __int128 call_original, call_patched;
static SOCKET connection = INVALID_SOCKET;
static char secret[33], digest[65];
static char proposal[9]="00000000",inputs[2][9]={"00000000","00000000"};
static unsigned wire_version=2,wire_clients=2;
static unsigned wire_lookahead;
static char blob_proposal[GATE_BLOB_HEX_MAX+1],blob_staged[GATE_BLOB_HEX_MAX+1];
static char blob_inputs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
static uint64_t blob_staged_tick;
static int blob_staged_set;
static DWORD ui_thread, main_thread;
static unsigned timeout_ms;
static int installed, used;
static double previous_game;
static uint64_t rng_seeds,seed_skips;
static int boot_mode, boot_phase, boot_adopted, dt_installed, dt_restored;
static ULONGLONG boot_started, boot_admitted_ms;
static uint64_t boot_options;
static unsigned remote_port;
static uint32_t relay_address;
#include "lockstep_gate_common.h"
#include "lockstep_tick.h"
#include "lockstep_seed_core.h"
#include "lockstep_tls.h"
/* World identities (gate_world.h, included below). */
static void world_identity_restore(void);
static int world_identity_owned(void);
static void world_identity_seal(void);
static volatile int64_t world_identity_failures;
static int world_identity_installed;
/* Serial pathing and path determinism (gate_path.h, included below). */
static void path_restore(void);
static int path_owned(void);
static void path_det_restore(void);
static int path_installed,path_det_budget_applied;
/* The pathing budget by count (gate_path_budget.h, included below). */
static void pbud_restore(void);
static int pbud_owned(void);
static int pbud_installed;
/* Hazard region updates (gate_region.h, included below). */
static void region_rng_restore(void);
static int region_rng_owned(void);
static int region_rng_armed,region_rng_restored;
static volatile int64_t region_rng_failures;
/* AI Wait restores (gate_ai_wait.h, included below). */
static void ai_wait_restore(void);
static int ai_wait_owned(void);
static int ai_wait_armed,ai_wait_restored;
static volatile int64_t ai_wait_failures;
/* MD cue delays and the boot hold (gate_md_resume.h, gate_boot_hold.h, included below). */
static void md_resume_restore(void);
static int md_resume_owned(void);
static int md_resume_armed,md_resume_restored;
static volatile int64_t md_resume_failures;
static void boot_hold_restore(void);
static int boot_hold_owned(void);
static int boot_hold_admit(void);
static int boot_hold_armed,boot_hold_restored;
static volatile int64_t boot_hold_failures;
/* Event order (gate_event_order.h, included below). */
static void event_order_restore(void);
static int event_order_owned(void);
static volatile long event_order_armed;
static int event_order_installed;
static volatile int64_t event_order_failures;
/* The Gravidar and its sector round-robin (gate_gravidar.h, included below). */
static void gravidar_restore(void);
static int gravidar_owned(void);
static int gravidar_boot_drain(void);
static int gravidar_installed,gravidar_restored;
static volatile int gravidar_time_sync;
static volatile int64_t gravidar_failures,gravidar_key_failures,gravidar_time_failures;
static void tsnap_restore(void);
static int tsnap_owned(void);
static int tsnap_installed;
static volatile int64_t tsnap_failures;
/* The navigation deadlines (gate_nav.h, included below). */
static void nav_cadence_restore(void);
static int nav_cadence_owned(void);
static int nav_cadence_installed;
static volatile int64_t nav_cadence_failures;
/* Created identities (gate_world_created.h, included below). */
static void created_stop(void);
static void created_restore(void);
static int created_begin(void);
static int created_frame(uint64_t tick);
static int created_installed;
static volatile long created_armed;
static volatile int64_t created_failures;
/* The Gravidar's zone groups (gate_gravidar_wide.h) and ship loadouts (gate_spawn_rng.h), included below. */
static void gwide_restore(void);
static int gwide_check(void);
static int gwide_armed,gwide_restored;
static volatile long gwide_active;
static void spawn_rng_restore(void);
static int spawn_rng_owned(void);
static int spawn_rng_armed,spawn_rng_restored;
static volatile int64_t spawn_rng_failures;
/* find_* candidate lists (gate_find_order.h, included below). */
static void find_order_restore(void);
static int find_order_owned(void);
static int find_order_armed,find_order_restored;
static volatile int64_t find_order_failures;
/* Function coverage (gate_coverage.h, included below). */
static void cov_unmark(void);
/* The Linear step trace (gate_step_trace.h, included below). */
static void st_restore(void);
static unsigned st_installed,st_chain_installed;
/* The flight parameter refresh recomputed next to native (gate_fp_redirect.h, included below). */
static void fr_restore(void);
static int fr_check(void);
static unsigned fr_installed;
/* Objects' attention timer (gate_attention.h, included below). */
static int attention_install(void);
static void attention_restore(void);
static int attention_owned(void);
static int attention_installed;
static uint64_t attention_failures;
/* Stations' loadout targets (gate_loadout_order.h, included below). */
static void ll_restore(void);
static int ll_owned(void);
static int ll_check(void);
static int ll_installed;
/* The script RNG (gate_script_rng.h, included below). */
static void md_timed_restore(void);
static int md_timed_owned(void);
static int md_timed_armed,md_timed_restored;
static volatile int64_t md_timed_failures;
static void lxs_restore(void);
static int lxs_owned(void);
static int script_rng_armed,script_rng_restored;
static volatile int64_t script_rng_failures;
static void rid_restore(void);
static void rid_callers_restore(void);
static int rid_owned(void);
static int rid_seal(void);
static int rid_frame(uint64_t tick);
static int rid_installed;
static void gdl_restore(void);
static int gdl_owned(void);
static int gdl_admit(void);
static int gdl_armed,gdl_restored;
static volatile int64_t gdl_failures;
static void ai_sched_restore(void);
static int ai_sched_boot(int phase);
static int ai_sched_release(void);
static int ai_sched_admit(void);
static volatile long ai_sched_armed;
static volatile int64_t ai_sched_failures;
static unsigned path_det_mask;
static volatile LONG64 path_det_failures;

/* Production defaults. Only linux/test_gate_linux.c changes them, to run the same
 * code on a fake image: its file, the /proc/self/maps checks, the module pin. */
static struct {const char *exe;int file,maps,pin;} gate_env={"/proc/self/exe",1,1,1};
static tick_fn gate_frame_tick;      /* X4::FrameTick, or the test's stand-in */
static const lx_site *gate_sites=lx_sites;
static unsigned gate_site_count=sizeof(lx_sites)/sizeof(*lx_sites);
static const char *verify_stage="none";
static unsigned gate_park_ms=LX_PARK_MS;
/* The UI (MoveWorker) thread's TLS base, refreshed by each accepted UI call, and
 * stats.issued at that refresh: the main thread may use it only for the frame the
 * UI just ran, while MoveWorker is parked at semSimStart. */
static unsigned char *ui_tls;
static uint64_t ui_tls_tick=UINT64_MAX;
static int seed_frames;

static int digest_hex(const void *data,size_t length,char out[65]) {
    unsigned char raw[32];lockstep_sha256(data,length,raw);hex32(raw,out);return 1;
}
static int hash_ok(const void *data,size_t length,const char *expected) {
    char hex[65];return digest_hex(data,length,hex) && !strcmp(hex,expected);
}
static uint16_t rd16(const unsigned char *p) {uint16_t v;memcpy(&v,p,2);return v;}
static uint32_t rd32(const unsigned char *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t rd64(const unsigned char *p) {uint64_t v;memcpy(&v,p,8);return v;}
static const lx_site *site_named(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* movsd xmm1,[rip+d] retargeted from the 1.0 clamp to a double 1/hz: the 1/60 constant of
 * .rodata, or for another rate a read-only page of this gate within rel32 reach (the
 * image has no 1/30 or 1/40). */
static double *dt_own;
static uint64_t dt_fixed_load(void) {
    int32_t d=gate_tick_hz==GATE_TICK_HZ_DEFAULT?(int32_t)((int64_t)LX_ONE_SIXTIETH_VA-(int64_t)(LX_DT_LOAD_VA+8)):
        (int32_t)((int64_t)(uintptr_t)dt_own-(int64_t)(uintptr_t)(LX(LX_DT_LOAD_VA)+8));
    return (LX_DT_LOAD_ORIGINAL&0xffffffffull)|((uint64_t)(uint32_t)d<<32);
}
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static int dt_prepare(void) {
    if(gate_tick_hz==GATE_TICK_HZ_DEFAULT || dt_own) return 1;
    uintptr_t at=(uintptr_t)LX(LX_DT_LOAD_VA)&~(uintptr_t)0xffff;
    for(uintptr_t distance=0x10000;distance<0x70000000;distance+=0x10000)
        for(int sign=-1;sign<=1;sign+=2) {
            if(sign<0 && distance>=at) continue;
            uintptr_t candidate=sign<0?at-distance:at+distance;
            if(candidate<0x10000) continue;
            void *p=mmap((void*)candidate,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
            if(p==MAP_FAILED) continue;
            if(p!=(void*)candidate) {munmap(p,4096);continue;}
            *(double*)p=gate_tick_dt();
            if(mprotect(p,4096,PROT_READ)) {munmap(p,4096);return 0;}
            dt_own=p;return 1;
        }
    return 0;
}
enum {DT_FOREIGN=-1,DT_ORIGINAL=0,DT_FIXED=1};
static int dt_state(void) {
    uint64_t load=__atomic_load_n((uint64_t*)LX(LX_DT_LOAD_VA),__ATOMIC_SEQ_CST);
    uint64_t min=__atomic_load_n((uint64_t*)LX(LX_DT_MIN_VA),__ATOMIC_SEQ_CST);
    if(load==LX_DT_LOAD_ORIGINAL && min==LX_DT_MIN_ORIGINAL) return DT_ORIGINAL;
    if(load==dt_fixed_load() && min==LX_DT_MIN_PATCHED) return DT_FIXED;
    return DT_FOREIGN;
}
/* A site's bytes against its pin; the dt site is compared in its original form
 * when the fixed60 patch (ours or another owner's) is present. */
static int site_ok(const lx_site *s) {
    unsigned char copy[0x200];
    if(!s) return 0;
    if(strcmp(s->name,LX_SITE_DT)) return hash_ok(LX(s->va),s->length,s->sha256);
    if(s->length>sizeof(copy)) return 0;
    memcpy(copy,LX(s->va),s->length);
    if(dt_state()==DT_FIXED) {
        uint64_t load=LX_DT_LOAD_ORIGINAL,min=LX_DT_MIN_ORIGINAL;
        if(LX_DT_LOAD_VA<s->va || LX_DT_MIN_VA+8>s->va+s->length) return 0;
        memcpy(copy+(LX_DT_LOAD_VA-s->va),&load,8);memcpy(copy+(LX_DT_MIN_VA-s->va),&min,8);
    }
    return hash_ok(copy,s->length,s->sha256);
}
static unsigned char *call_target(void) {
    int32_t rel;memcpy(&rel,LX(LX_CALL_VA)+1,4);return LX(LX_CALL_VA)+5+rel;
}
/* The in-memory image: ELF header, the TLS and text segments, every site pin and
 * the FrameTick call. Stage names a refusal for gate.verify_stage(). */
static int verify_image(void) {
    const unsigned char *e=image;
    verify_stage="elf";
    if(memcmp(e,"\177ELF\2\1\1",7) || rd16(e+16)!=2 || rd16(e+18)!=62 || rd64(e+32)!=64 ||
       rd16(e+54)!=56 || rd16(e+56)!=12) return 0;
    int tls=0,text=0;
    for(unsigned i=0;i<12;i++) {
        const unsigned char *p=e+64+56*i;
        if(rd32(p)==7) tls=rd64(p+16)==LX_TLS_VADDR && rd64(p+40)==LX_TLS_MEMSZ && rd64(p+48)==LX_TLS_ALIGN;
        if(rd32(p)==1 && rd64(p+16)==LX_TEXT_VA) text=rd32(p+4)==5;
    }
    verify_stage="segments";
    if(!tls || !text) return 0;
    for(unsigned i=0;i<gate_site_count;i++) {verify_stage=gate_sites[i].name;if(!site_ok(&gate_sites[i])) return 0;}
    verify_stage="call";
    if(*LX(LX_CALL_VA)!=0xe8 || call_target()!=LX(LX_TICK_VA)) return 0;
    verify_stage="ok";return 1;
}
/* glibc keeps the TCB self pointer at fs:0 and fs:0x10; the static TLS offsets
 * of lockstep_tls.h are relative to it. */
static int tls_base_ok(void) {
#ifdef GATE_TLS_TEST
    return 1;
#else
    unsigned char *self;__asm__("mov %%fs:0x10,%0":"=r"(self));
    return self && self==gate_tls_self();
#endif
}
static int verify(void) {
    unsigned char raw[32];char hex[65];
    verify_stage="file";
    if(gate_env.file && (!gate_file_sha256(gate_env.exe,LX_FILE_SIZE,raw) || (hex32(raw,hex),strcmp(hex,LX_FILE_SHA)))) return 0;
    if(!image) image=(unsigned char*)(uintptr_t)LX_IMAGE_BASE;
    verify_stage="maps";
    if(gate_env.maps && (!gate_maps_exe(LX_IMAGE_BASE,"r--p") || !gate_maps_exe(LX_CALL_WINDOW,"r-xp") ||
       !gate_maps_exe(LX_DT_LOAD_VA,"r-xp"))) return 0;
    verify_stage="tls";
    if(gate_env.maps && !tls_base_ok()) return 0;
    return verify_image();
}
/* The main thread is outside FrameTick until MoveWorker posts semSimDone. Only
 * other threads may wait for it: the main thread itself never sees it set here. */
static int wait_parked(unsigned ms) {
    ULONGLONG start=GetTickCount64();
    for(;;) {
        if(__atomic_load_n(LX(LX_IN_FLIGHT_VA),__ATOMIC_SEQ_CST)==1) return 1;
        if(GetTickCount64()-start>=ms) return 0;
        struct timespec pause={0,200000};nanosleep(&pause,NULL);
    }
}
/* Two qword compare-exchanges: the load first (dt = min(1/hz, dt)), then the
 * minsd (dt = 1/hz). Every intermediate state is a valid dt; restore reverses. */
static int install_dt(void) {
    if(dt_state()!=DT_ORIGINAL || *(volatile unsigned char*)LX(LX_MOVIE_FLAG_VA) || !site_ok(site_named("rodata_one_sixtieth"))) return 0;
    if(!dt_prepare() || !wait_parked(gate_park_ms)) return 0;
    if(!gate_code_swap64(LX(LX_DT_LOAD_VA),LX_DT_LOAD_ORIGINAL,dt_fixed_load())) return 0;
    if(!gate_code_swap64(LX(LX_DT_MIN_VA),LX_DT_MIN_ORIGINAL,LX_DT_MIN_PATCHED)) {
        gate_code_swap64(LX(LX_DT_LOAD_VA),dt_fixed_load(),LX_DT_LOAD_ORIGINAL);return 0;
    }
    dt_installed=1;return 1;
}
static void restore_dt(void) {
    if(!dt_installed) return;
    /* A foreign owner must never be overwritten. */
    int min=gate_code_swap64(LX(LX_DT_MIN_VA),LX_DT_MIN_PATCHED,LX_DT_MIN_ORIGINAL);
    int load=min && gate_code_swap64(LX(LX_DT_LOAD_VA),dt_fixed_load(),LX_DT_LOAD_ORIGINAL);
    dt_restored=min && load;
    if(dt_restored) dt_installed=0;
}
static int call_swap(unsigned __int128 expected,unsigned __int128 value) {
    return gate_code_swap128(LX(LX_CALL_WINDOW),expected,value);
}
/* Same phase rule as lockstep_gate.c. */
static int boot_next(int phase,int loading) {
    if(phase==1 && loading) return 2;
    if(phase==2 && !loading) return 3;
    return phase;
}
static int table_loading(void *table) {int32_t v;memcpy(&v,(unsigned char*)table+LX_VBI_LOADING,4);return v!=0;}
static int accept_ui(void) {
    DWORD thread=GetCurrentThreadId();
    if(thread==ui_thread) {
        __atomic_store_n(&ui_tls,gate_tls_self(),__ATOMIC_SEQ_CST);
        __atomic_store_n(&ui_tls_tick,stats.issued,__ATOMIC_SEQ_CST);
        return 1;
    }
    if(boot_mode && boot_phase==3 && !boot_adopted && stats.issued && thread!=main_thread) {
        unsigned char *base=gate_tls_self();
        if(gate_tls_worker(base)) {
            ui_thread=thread;boot_adopted=1;
            __atomic_store_n(&ui_tls,base,__ATOMIC_SEQ_CST);__atomic_store_n(&ui_tls_tick,stats.issued,__ATOMIC_SEQ_CST);
            return 1;
        }
    }
    return 0;
}
static void close_connection(void) {
    SOCKET owned=__atomic_exchange_n(&connection,INVALID_SOCKET,__ATOMIC_SEQ_CST);
    if(owned!=INVALID_SOCKET) closesocket(owned);
}
static int gate_sites_owned(void) {
    unsigned __int128 window;double speed;
    memcpy(&window,LX(LX_CALL_WINDOW),16);memcpy(&speed,LX(LX_SPEED_VA),8);
    return window==call_patched && dt_state()==DT_FIXED && !*(volatile unsigned char*)LX(LX_MOVIE_FLAG_VA) && speed==1.0 &&
           world_identity_owned() && path_owned() && pbud_owned() && region_rng_owned() && ai_wait_owned() && md_resume_owned() && boot_hold_owned() && event_order_owned() && gravidar_owned() && gdl_owned() && tsnap_owned() && rid_owned() && md_timed_owned() && lxs_owned() && find_order_owned() && ll_owned() && attention_owned() && spawn_rng_owned() && nav_cadence_owned();
}
static void finish(unsigned reason) {
    cov_unmark(); /* first: every coverage mark and page back before any other restore changes a page */
    stats.reason=reason;
    created_stop(); /* No created identity is recorded once STOP has begun. */
    InterlockedExchange(&stats.active,0);
    if(installed) {stats.restored=call_swap(call_patched,call_original);if(stats.restored) installed=0;}
    /* From the UI thread, let the main thread leave FrameTick first (best effort). */
    if(dt_installed && main_thread && GetCurrentThreadId()!=main_thread) wait_parked(500);
    restore_dt();
    if(dt_installed) stats.restored=0;
    world_identity_restore();
    if(world_identity_installed) stats.restored=0;
    pbud_restore();
    if(pbud_installed) stats.restored=0;
    path_restore();
    if(path_installed) stats.restored=0;
    path_det_restore();
    if(path_det_budget_applied) stats.restored=0;
    region_rng_restore();
    if(region_rng_armed && !region_rng_restored) stats.restored=0;
    ai_wait_restore();
    if(ai_wait_armed && !ai_wait_restored) stats.restored=0;
    md_resume_restore();
    if(md_resume_armed && !md_resume_restored) stats.restored=0;
    boot_hold_restore();
    if(boot_hold_armed && !boot_hold_restored) stats.restored=0;
    event_order_restore();
    if(event_order_installed) stats.restored=0;
    gravidar_restore();
    if(gravidar_installed) stats.restored=0;
    nav_cadence_restore();
    if(nav_cadence_installed) stats.restored=0;
    gwide_restore();
    if(gwide_armed && !gwide_restored) stats.restored=0;
    spawn_rng_restore();
    if(spawn_rng_armed && !spawn_rng_restored) stats.restored=0;
    find_order_restore();
    if(find_order_armed && !find_order_restored) stats.restored=0;
    ll_restore();
    if(ll_installed) stats.restored=0;
    attention_restore();
    if(attention_installed) stats.restored=0;
    st_restore();
    if(st_installed || st_chain_installed) stats.restored=0;
    fr_restore();
    if(fr_installed) stats.restored=0;
    md_timed_restore();
    if(md_timed_armed && !md_timed_restored) stats.restored=0;
    lxs_restore();
    if(script_rng_armed && !script_rng_restored) stats.restored=0;
    gdl_restore();
    if(gdl_armed && !gdl_restored) stats.restored=0;
    tsnap_restore();
    if(tsnap_installed) stats.restored=0;
    rid_restore();
    rid_callers_restore();
    if(rid_installed) stats.restored=0;
    created_restore();
    if(created_installed) stats.restored=0;
    ai_sched_restore();
    gate_fetch_remove();
    close_connection();
}
static void gate_original_once(void *frame,unsigned char loading) {gate_frame_tick(frame,loading);}
static int closed_window(void) {return 0;} /* no cheap SDL quit check from this thread yet; the timeout bounds the wait */
/* The per-frame reads of the timing rows: every guarded read, also those not through read_local. */
#define read_local_calls gate_read_count
#include "lockstep_gate_timing.h"
/* ReadProcessMemory on Windows; module ports call it under this name. */
static __attribute__((unused)) int read_local(const void *source,void *dest,size_t bytes) {
    return gate_read_local(source,dest,bytes);
}
#include "lockstep_lookahead.h"
/* Protocol 2/3 permit wait, as the inline loop of the Windows gate_tick. 0: the
 * permit for completed+1 arrived; otherwise the stop reason. */
static unsigned gate_wait_permit(const char *packet,int length,char *reply,size_t capacity) {
    ULONGLONG start=GetTickCount64(),last=0;
    for(;;) {
        ULONGLONG now=GetTickCount64();
        double game;memcpy(&game,image+GAME_RVA,8);
        double drift=fabs(game-stats.game);
        if(drift>stats.wait_game_delta) stats.wait_game_delta=drift;
        if(drift>0) return 5;
        if(closed_window()) return 6;
        if(now-start>timeout_ms) return 7;
        if(!last||now-last>=100) {send(connection,packet,(size_t)length,0);last=now;++stats.waits;}
        int count=(int)recv(connection,reply,capacity-1,0);
        if(count<0) {int error=WSAGetLastError();if(error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK) return 8;continue;}
        reply[count]=0;
        if((size_t)count!=strlen(reply)) continue;
        char permit[128],stop[128];
        snprintf(permit,sizeof(permit),"NGGO|%u|%s|%u|%llu",wire_version,secret,stats.slot,(unsigned long long)(stats.completed+1));
        snprintf(stop,sizeof(stop),"NGSTOP|%u|%s|%u|%llu",wire_version,secret,stats.slot,(unsigned long long)stats.completed);
        if(!strcmp(reply,stop)) return 1;
        int accepted=wire_version==3?gate_permit_blobs(reply,(size_t)count,permit,stats.slot,wire_clients,blob_proposal,blob_inputs):
            gate_permit_inputs(reply,permit,stats.slot,proposal,inputs[0],inputs[1]);
        if(accepted<0) return 56;
        if(accepted) return 0;
    }
}
/* Opt-in per-frame MoveWorker stream (boot/start option 2, test-only like the
 * Windows option): Windows seeds after the MoveWorker's pathing release, Linux at
 * admission, before FrameTick starts the frame. Only through the TLS base the UI
 * thread refreshed during the frame just completed. */
static void seed_frame(void) {
    unsigned char *base=__atomic_load_n(&ui_tls,__ATOMIC_SEQ_CST);
    uint64_t tick=__atomic_load_n(&ui_tls_tick,__ATOMIC_SEQ_CST);
    if(!base || !stats.completed || tick!=stats.completed || !gate_tls_worker(base)) {++seed_skips;return;}
    *gate_tls_rng(base)=lockstep_seed(LX_SEED_DOMAIN,stats.issued,0,0,0,0);++rng_seeds;
}
static void gate_tick(void *frame,unsigned char loading) {
    if(!stats.active) {gate_original_once(frame,loading);return;}
    LARGE_INTEGER ft_entry;QueryPerformanceCounter(&ft_entry);
    DWORD thread=GetCurrentThreadId();if(!main_thread) main_thread=thread;
    gate_fetch_check();
    void *table;memcpy(&table,LX(LX_VBI_TABLE_VA),8);
    if(world_identity_installed && world_identity_failures) {finish(72);gate_original_once(frame,loading);return;}
    if(path_det_mask && path_det_failures) {finish(79);gate_original_once(frame,loading);return;}
    if(region_rng_armed && region_rng_failures) {finish(93);gate_original_once(frame,loading);return;}
    if(ai_wait_armed && ai_wait_failures) {finish(88);gate_original_once(frame,loading);return;}
    if(boot_hold_armed && boot_hold_failures) {finish(86);gate_original_once(frame,loading);return;}
    if(md_resume_armed && md_resume_failures) {finish(87);gate_original_once(frame,loading);return;}
    if(event_order_armed && event_order_failures) {finish(80);gate_original_once(frame,loading);return;}
    if(gravidar_installed && gravidar_failures) {finish(62);gate_original_once(frame,loading);return;}
    if(gravidar_key_failures || gravidar_time_failures) {finish(83);gate_original_once(frame,loading);return;}
    if(ai_sched_armed && ai_sched_failures) {finish(84);gate_original_once(frame,loading);return;}
    if(gdl_armed && gdl_failures) {finish(121);gate_original_once(frame,loading);return;}
    if(tsnap_installed && tsnap_failures) {finish(89);gate_original_once(frame,loading);return;}
    if(script_rng_armed && script_rng_failures) {finish(82);gate_original_once(frame,loading);return;}
    if(md_timed_armed && md_timed_failures) {finish(86);gate_original_once(frame,loading);return;}
    if(find_order_armed && find_order_failures) {finish(92);gate_original_once(frame,loading);return;}
    if(ll_installed && !ll_check()) {finish(172);gate_original_once(frame,loading);return;} /* LL_STOP_REASON */
    if(fr_installed && !fr_check()) {finish(179);gate_original_once(frame,loading);return;} /* FR_STOP_REASON */
    if(attention_installed && attention_failures) {finish(54);gate_original_once(frame,loading);return;}
    if(created_armed && created_failures) {finish(96);gate_original_once(frame,loading);return;}
    if(nav_cadence_installed && nav_cadence_failures) {finish(95);gate_original_once(frame,loading);return;}
    if(gwide_active && !gwide_check()) {finish(150);gate_original_once(frame,loading);return;}
    if(spawn_rng_armed && spawn_rng_failures) {finish(110);gate_original_once(frame,loading);return;}
    if(boot_mode && boot_phase<3) {
        if(thread!=main_thread || GetTickCount64()-boot_started>LX_BOOT_TIMEOUT_MS) {finish(26);gate_original_once(frame,loading);return;}
        boot_phase=boot_next(boot_phase,loading || (table && table_loading(table)));
        if(boot_phase<3) {
            /* Zero the idle Gravidar round-robin cursor before the load pass. */
            if(ai_sched_armed && !ai_sched_boot(boot_phase)) {InterlockedIncrement64(&ai_sched_failures);finish(84);}
            gate_original_once(frame,loading);return;
        }
        boot_admitted_ms=GetTickCount64()-boot_started;
        if(ai_sched_armed && !ai_sched_release()) {InterlockedIncrement64(&ai_sched_failures);finish(84);gate_original_once(frame,loading);return;}
        world_identity_seal(); /* Imports are over (Windows seals here too). */
        /* Later creations get created identities; with native_gate_created_load the load-created ones too, here. */
        if(!created_begin()) {finish(96);gate_original_once(frame,loading);return;}
        if(!rid_seal()) {finish(130);gate_original_once(frame,loading);return;}
        if(world_identity_installed && world_identity_failures) {finish(72);gate_original_once(frame,loading);return;}
        if(gravidar_time_sync && !gravidar_boot_drain()) {InterlockedIncrement64(&gravidar_time_failures);finish(83);gate_original_once(frame,loading);return;}
        if(ai_sched_armed && !ai_sched_admit()) {InterlockedIncrement64(&ai_sched_failures);finish(84);gate_original_once(frame,loading);return;}
        if(boot_hold_armed && !boot_hold_admit()) {InterlockedIncrement64(&boot_hold_failures);finish(86);gate_original_once(frame,loading);return;}
        if(gdl_armed && !gdl_admit()) {InterlockedIncrement64(&gdl_failures);finish(121);gate_original_once(frame,loading);return;}
    }
    if(thread!=main_thread||loading||!table||table_loading(table)) {finish(2);gate_original_once(frame,loading);return;}
    if(!gate_sites_owned()) {finish(3);gate_original_once(frame,loading);return;}
    memcpy(&stats.game,LX(LX_GAME_VA),8);memcpy(&stats.real,LX(LX_REAL_VA),8);
    if(stats.issued) {
        double error=fabs((stats.game-previous_game)-gate_tick_dt());
        if(error>stats.max_dt_error) stats.max_dt_error=error;
        if(error>1e-8||stats.report_tick!=stats.issued) {finish(4);gate_original_once(frame,loading);return;}
    }
    stats.completed=stats.issued;
    /* The frame just run is complete and its MoveWorker joined: resolve its creations. */
    if(!created_frame(stats.completed)) {finish(96);gate_original_once(frame,loading);return;}
    if(!rid_frame(stats.completed)) {finish(130);gate_original_once(frame,loading);return;}
    static char packet[GATE_STATE_MAX],reply[GATE_PERMIT_MAX];uint64_t gamebits,realbits;
    memcpy(&gamebits,&stats.game,8);memcpy(&realbits,&stats.real,8);
    int length=wire_lookahead?
        snprintf(packet,sizeof(packet),"NGSTATE|4|%s|%u|%llu|%016llx|%016llx|%s|%u|%u|%s",secret,stats.slot,
                 (unsigned long long)stats.completed,(unsigned long long)gamebits,(unsigned long long)realbits,digest,wire_clients,wire_lookahead,blob_proposal):
        wire_version==3?
        snprintf(packet,sizeof(packet),"NGSTATE|3|%s|%u|%llu|%016llx|%016llx|%s|%u|%s",secret,stats.slot,
                 (unsigned long long)stats.completed,(unsigned long long)gamebits,(unsigned long long)realbits,digest,wire_clients,blob_proposal):
        snprintf(packet,sizeof(packet),"NGSTATE|2|%s|%u|%llu|%016llx|%016llx|%s|%s",secret,stats.slot,
                 (unsigned long long)stats.completed,(unsigned long long)gamebits,(unsigned long long)realbits,digest,proposal);
    LARGE_INTEGER ft_begin,ft_permit,ft_end;QueryPerformanceCounter(&ft_begin);
    if(ft_last_end) {LARGE_INTEGER prior;prior.QuadPart=(LONGLONG)ft_last_end;ft_outside=ft_us(prior,ft_begin);ft_sum_outside+=ft_outside;}
    ft_pre=ft_us(ft_entry,ft_begin);ft_sum_pre+=ft_pre;
    {uint64_t reads=read_local_calls;ft_reads=reads-ft_reads_last;ft_reads_last=reads;}
    unsigned reason;
    if(length<0 || (size_t)length>=sizeof(packet)) reason=56;
    else if(wire_lookahead) reason=lookahead_frame(packet,(size_t)length,reply,sizeof(reply)); /* protocol 4 */
    else reason=gate_wait_permit(packet,length,reply,sizeof(reply));
    if(reason) finish(reason);
    else {
        previous_game=stats.game;++stats.issued;
        if(seed_frames && !path_installed) seed_frame(); /* else after the release, as on Windows (gate_path.h) */
    }
    QueryPerformanceCounter(&ft_permit);
    ft_wait=ft_us(ft_begin,ft_permit);
    if(stats.active) gate_pace_wait(); /* native_gate_tick_hz: real time, after the report left */
    QueryPerformanceCounter(&ft_permit);
    gate_original_once(frame,loading);
    QueryPerformanceCounter(&ft_end);
    ft_frame=ft_us(ft_permit,ft_end);ft_sum_wait+=ft_wait;ft_sum_frame+=ft_frame;
    ++ft_frames;ft_last_end=(uint64_t)ft_end.QuadPart;
}
/* The permanent thunk page within rel32 reach of the call: jmp [rip+0] to
 * gate_tick. A jump keeps the caller's return address, so no frame or unwind
 * entry of its own exists. Written before it is executable, then r-x for good. */
static void lxw_thunk_write(unsigned char *page);
static void lxp_thunk_write(unsigned char *page);
static void lxm_thunk_write(unsigned char *page);
static void lxb_thunk_write(unsigned char *page);
static void lxe_thunk_write(unsigned char *page);
static void lxg_thunk_write(unsigned char *page);
static void lxt_thunk_write(unsigned char *page);
static void lxi_thunk_write(unsigned char *page);
static void lxs_thunk_write(unsigned char *page);
static void lxo_thunk_write(unsigned char *page);
static void lxn_thunk_write(unsigned char *page);
static void lxl_thunk_write(unsigned char *page);
static void lxa_thunk_write(unsigned char *page);
static void lxf_thunk_write(unsigned char *page);
static void lxc_thunk_write(unsigned char *page);
static unsigned char *make_thunk(void) {
    unsigned char *p=gate_near_page(LX(LX_CALL_VA));if(!p) return NULL;
    void *target=(void*)gate_tick;
    memset(p,0xcc,GATE_PAGE);p[0]=0xff;p[1]=0x25;memset(p+2,0,4);memcpy(p+6,&target,8);
    lxw_thunk_write(p); /* world identity hook and trampoline (gate_world.h), used only when installed */
    lxp_thunk_write(p); /* the pathing release (gate_path.h), likewise */
    lxm_thunk_write(p); /* the MD delay call (gate_md_resume.h), likewise */
    lxb_thunk_write(p); /* AIDirector::Move (gate_boot_hold.h), likewise */
    lxe_thunk_write(p); /* the event delivery call (gate_event_order.h), likewise */
    lxg_thunk_write(p); /* the MoveWorker's Gravidar publish (gate_gravidar.h), likewise */
    lxt_thunk_write(p); /* the batch job (gate_travel_snapshot.h), likewise */
    lxi_thunk_write(p); /* the ID map's Remove (gate_runtime_ids.h), likewise */
    lxs_thunk_write(p); /* the script entries (gate_script_rng.h), likewise */
    lxo_thunk_write(p); /* the result store call (gate_find_order.h), likewise */
    lxn_thunk_write(p); /* the ship generator (gate_spawn_rng.h), likewise */
    lxl_thunk_write(p); /* the loadout calls and rests (gate_loadout_order.h), likewise */
    lxa_thunk_write(p); /* the attention timer (gate_attention.h), likewise */
    lxf_thunk_write(p); /* the flight parameter refresh (gate_fp_redirect.h), likewise */
    lxc_thunk_write(p); /* FR8's three calls in the Linear step (gate_step_trace.h), likewise */
    if(mprotect(p,GATE_PAGE,PROT_READ|PROT_EXEC)) {munmap(p,GATE_PAGE);return NULL;}
    return p;
}
/* Keep this module loaded for the process: the thunk jumps into it. */
static int start_gate(unsigned slot,unsigned port,const char *token,unsigned timeout,const char *initial_digest,int boot);
static int pin_module(void) {
    Dl_info info;
    if(!dladdr((void*)start_gate,&info) || !info.dli_fname) return 0;
    return dlopen(info.dli_fname,RTLD_NOW|RTLD_NOLOAD|RTLD_NODELETE)!=NULL;
}
static int start_gate(unsigned slot,unsigned port,const char *token,unsigned timeout,const char *initial_digest,int boot) {
    if(used||slot<1||slot>wire_clients||(wire_lookahead && wire_version!=3)||port<1024||port>65535||timeout<1000||timeout>120000||!valid_hex(token,32)||!valid_hex(initial_digest,64)) return 10;
    if(!verify()) return 11;
    if(*(volatile unsigned char*)LX(LX_MOVIE_FLAG_VA)) return boot?27:12;
    /* start: another owner fixed dt (the x4mp-agent fixed60 pattern); boot: the
     * original FrameTick, which the gate fixes itself. */
    if(!boot && dt_state()!=DT_FIXED) return 12;
    if(boot && dt_state()!=DT_ORIGINAL) return 27;
    void *table;memcpy(&table,LX(LX_VBI_TABLE_VA),8);if(!table||(!boot && table_loading(table))) return 13;
    if(gate_env.pin && !pin_module()) return 15;
    if(boot && !install_dt()) return 27;
    connection=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,IPPROTO_UDP);if(connection==INVALID_SOCKET) {restore_dt();return 17;}
    struct sockaddr_in remote;memset(&remote,0,sizeof(remote));remote.sin_family=AF_INET;remote.sin_port=htons((uint16_t)port);
    remote.sin_addr.s_addr=relay_address?relay_address:htonl(INADDR_LOOPBACK);
    struct timeval receive_timeout={0,25000};
    if(setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,&receive_timeout,sizeof(receive_timeout))) {finish(77);return 77;}
    if(connect(connection,(struct sockaddr*)&remote,sizeof(remote))) {finish(18);return 18;}
    if(!gate_frame_tick) gate_frame_tick=(tick_fn)(void*)LX(LX_TICK_VA);
    thunk=make_thunk();if(!thunk) {finish(19);return 19;}
    intptr_t distance=(intptr_t)thunk-(intptr_t)(LX(LX_CALL_VA)+5);
    if(distance<INT32_MIN||distance>INT32_MAX) {finish(19);return 19;}
    memcpy(&call_original,LX(LX_CALL_WINDOW),16);call_patched=call_original;
    int32_t rel=(int32_t)distance;memcpy((unsigned char*)&call_patched+(LX_CALL_VA+1-LX_CALL_WINDOW),&rel,4);
    memset(&stats,0,sizeof(stats));stats.slot=slot;strcpy(secret,token);strcpy(digest,initial_digest);lookahead_reset();
    strcpy(initial_state_digest,initial_digest);remote_port=port;
    ui_thread=GetCurrentThreadId();__atomic_store_n(&ui_tls,gate_tls_self(),__ATOMIC_SEQ_CST);
    gate_fetch_install();
    timeout_ms=timeout;stats.active=1;used=1;
    boot_mode=boot;boot_started=GetTickCount64();
    if(!call_swap(call_original,call_patched)) {finish(20);return 20;}
    installed=1;return 0;
}
#include "lockstep_gate_exports.h"
#include "lockstep_gate_lua_abi.h"
static int lua_start(lua_State *L) {
    int result=x4gate_start((unsigned)p_lua_tonumber(L,1),(unsigned)p_lua_tonumber(L,2),
        p_lua_tolstring(L,3,NULL),(unsigned)p_lua_tonumber(L,4),p_lua_tolstring(L,5,NULL));
    double options=p_lua_tonumber(L,6);
    /* Option 2 (frame seed) only; 1 and 3 need the serial pathing hook, not ported. */
    if(!result && options==2) seed_frames=1;
    else if(!result && options!=0) {finish(21);result=21;}
    p_lua_pushnumber(L,result);return 1;
}
#include "lockstep_gate_lua_wire.h"
#include "gate_world.h"
#include "gate_world_created.h"
#include "gate_path.h"
#include "gate_path_budget.h"
#include "gate_script_rng.h"
#include "gate_find_order.h"
#include "gate_spawn_rng.h"
#include "gate_loadout_order.h"
#include "gate_attention.h"
#include "gate_step_trace.h"
#include "gate_fp_redirect.h"
#include "gate_coverage.h"
#include "gate_nav.h"
#include "gate_region.h"
#include "gate_ai_wait.h"
#include "gate_md_resume.h"
#include "gate_boot_hold.h"
#include "gate_event_order.h"
#include "gate_gravidar.h"
#include "gate_gravidar_deadlines.h"
#include "gate_travel_snapshot.h"
#include "gate_runtime_ids.h"
static int lua_read_cost(lua_State *L) {
    char out[256];
    int n=accept_ui()?snprintf(out,sizeof(out),"fast=%d installed=%d reads=%llu faults=%llu lost=%llu slow_ns=%llu fast_ns=%llu.%llu gettid_ns=%llu",
        gate_fetch_on,gate_fetch_installed,(unsigned long long)gate_read_count,(unsigned long long)gate_fetch_faults,(unsigned long long)gate_fetch_lost,
        (unsigned long long)gate_fetch_cost_slow_ns,(unsigned long long)(gate_fetch_cost_fast_dns/10),(unsigned long long)(gate_fetch_cost_fast_dns%10),
        (unsigned long long)gate_fetch_cost_tid_ns):0;
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* Ported boot options; every other bit names a module the Linux gate lacks.
 * 1: serial pathing; 2: per-frame MoveWorker seed; 2^37: the gated Gravidar pass (native_gate_radar_serial,
 * needs 1); 2^46: world identities (native_gate_world_identities). */
#define LX_BOOT_GRAVIDAR 137438953472ull
#define LX_BOOT_WORLD_IDENTITIES 70368744177664ull
/* The attention hook (gate_attention.h): attention_rng or its initial refresh. Only 'all' is ported, which needs either. */
#define LX_BOOT_ATTENTION (536870912ull|1073741824ull)
#define LX_BOOT_OPTIONS (1ull|2ull|LX_BOOT_ATTENTION|LX_BOOT_GRAVIDAR|LX_BOOT_WORLD_IDENTITIES)
static int boot_gate(unsigned slot,unsigned port,const char *token,unsigned timeout,const char *hash,double options) {
    if(!isfinite(options) || options<0 || options>9007199254740991.0 || floor(options)!=options) return 10;
    if((uint64_t)options & ~LX_BOOT_OPTIONS) return LX_STOP_UNPORTED_OPTION;
    /* UI reloads are allowed only for this same process-owned session. Unlike the
     * Windows lua_boot, also after it stopped (nothing is re-armed): a relay stop
     * during the first admitted frame lands before the reloaded probe re-arms, and
     * the probe then reports the stop reason instead of REFUSED code=10. */
    if(boot_mode && used && slot==stats.slot && port==remote_port && (uint64_t)options==boot_options &&
       valid_hex(token,32) && !strcmp(secret,token) && valid_hex(hash,64) && !strcmp(hash,initial_state_digest)) return 0;
    if(used) return 10;
    int result=start_gate(slot,port,token,timeout,hash,1);
    if(!result) {
        boot_options=(uint64_t)options;seed_frames=(boot_options&2)!=0;
        if((boot_options&LX_BOOT_WORLD_IDENTITIES) && !world_identity_install()) {finish(71);result=71;}
        if(!result && (boot_options&1) && !path_install((unsigned)(boot_options&3))) {finish(21);result=21;}
        if(!result && (boot_options&LX_BOOT_GRAVIDAR) && !gravidar_install()) {finish(61);result=61;}
        if(!result && (boot_options&LX_BOOT_ATTENTION) && !attention_install()) {finish(53);result=53;}
    }
    return result;
}
static int lua_boot(lua_State *L) {
    p_lua_pushnumber(L,boot_gate((unsigned)p_lua_tonumber(L,1),(unsigned)p_lua_tonumber(L,2),p_lua_tolstring(L,3,NULL),
        (unsigned)p_lua_tonumber(L,4),p_lua_tolstring(L,5,NULL),p_lua_tonumber(L,6)));
    return 1;
}
static int load_requested(void) {
    int ok=boot_mode && stats.active && boot_phase==0 && GetCurrentThreadId()==ui_thread;
    if(ok) boot_phase=1;
    return ok;
}
static int lua_load_requested(lua_State *L) {p_lua_pushnumber(L,load_requested());return 1;}
#include "lockstep_gate_lua_misc.h"
#include "lockstep_gate_lua_inputs.h"
static int lua_poll(lua_State *L) {
    gate_stats s;if(!x4gate_poll(&s)) {p_lua_pushnil(L);return 1;}
    p_lua_createtable(L,0,32);
    push_inputs(L);
#define FIELD(name) p_lua_pushnumber(L,(double)s.name);p_lua_setfield(L,-2,#name)
    FIELD(issued);FIELD(completed);FIELD(waits);FIELD(report_tick);FIELD(game);FIELD(real);
    FIELD(wait_game_delta);FIELD(max_dt_error);FIELD(active);FIELD(reason);FIELD(restored);FIELD(slot);
#undef FIELD
#define NUMBER(name,value) p_lua_pushnumber(L,(double)(value));p_lua_setfield(L,-2,name)
    NUMBER("gravidar_frames",gravidar_frames);NUMBER("gravidar_waits",gravidar_waits);NUMBER("gravidar_timeouts",gravidar_timeouts);NUMBER("gravidar_calls",0);
    NUMBER("gravidar_restored",gravidar_restored);NUMBER("gravidar_failures",gravidar_failures);NUMBER("gravidar_inits",0);
    NUMBER("path_joins",path_joins);NUMBER("path_restored",path_restored);NUMBER("rng_seeds",rng_seeds);NUMBER("seed_skips",seed_skips);
    NUMBER("boot_phase",boot_phase);NUMBER("boot_admitted_ms",boot_admitted_ms);NUMBER("dt_restored",dt_restored);
    NUMBER("rng_schema",LOCKSTEP_RNG_SCHEMA);NUMBER("registry_count",0);
    NUMBER("world_identity_calls",world_identity_calls);NUMBER("world_identity_count",world_identity_count);
    NUMBER("world_identity_late",world_identity_late);NUMBER("world_identity_failures",world_identity_failures);
    NUMBER("world_identity_restored",world_identity_restored);
    NUMBER("world_hash_scans",world_hash_scans);NUMBER("world_hash_failures",world_hash_failures);
#undef NUMBER
    p_lua_pushlstring(L,"linux",5);p_lua_setfield(L,-2,"platform");
    /* Observation only: the calling UI thread's own RNG word (MoveWorker). */
    uint64_t *rng=gate_tls_rng(gate_tls_self());
    if(rng) {
        char value[17];snprintf(value,sizeof(value),"%016llx",(unsigned long long)*rng);
        p_lua_pushlstring(L,value,16);p_lua_setfield(L,-2,"rng");
    }
    return 1;
}
static int lua_stop(lua_State *L) {p_lua_pushnumber(L,x4gate_stop());return 1;}
static int lua_platform(lua_State *L) {p_lua_pushlstring(L,"linux",5);return 1;}
/* Why the last start/boot refused (code 11): file, maps, tls, elf, segments, a site name, call. */
static int lua_verify_stage(lua_State *L) {p_lua_pushlstring(L,verify_stage,strlen(verify_stage));return 1;}
/* X4 removes ffi.load. Lua 5.1 module ABI resolved from X4's own LuaJIT
 * (libluajit-5.1.so.2, linked by the executable, so in the global scope). */
GATE_EXPORT int luaopen_x4lan_gate(lua_State *L) {
#define RESOLVE(name) p_##name=(void*)dlsym(RTLD_DEFAULT,#name);if(!p_##name) return 0
    RESOLVE(lua_tonumber);RESOLVE(lua_tolstring);RESOLVE(lua_pushnumber);RESOLVE(lua_pushnil);RESOLVE(lua_pushlstring);
    RESOLVE(lua_createtable);RESOLVE(lua_setfield);RESOLVE(lua_pushcclosure);
#undef RESOLVE
    p_lua_rawseti=(void*)dlsym(RTLD_DEFAULT,"lua_rawseti");
    p_lua_createtable(L,0,16);
#define METHOD(name) p_lua_pushcclosure(L,lua_##name,0);p_lua_setfield(L,-2,#name)
    METHOD(sha256);METHOD(start);METHOD(boot);METHOD(load_requested);METHOD(report);METHOD(poll);METHOD(stop);
    METHOD(frame_timing);METHOD(qpc_us);METHOD(tick_rate);METHOD(wire);METHOD(propose);METHOD(relay_host);METHOD(lookahead);METHOD(lookahead_status);
    METHOD(platform);METHOD(verify_stage);
    METHOD(world_watch);METHOD(world_capture);METHOD(world_capture_nav_v2);METHOD(world_page);METHOD(world_detail);
    METHOD(world_banks);METHOD(saved_identity);
    METHOD(path_determinism);METHOD(path_determinism_status);METHOD(path_budget);METHOD(path_budget_status);
    METHOD(region_rng);METHOD(region_rng_status);METHOD(region_rng_row);
    METHOD(ai_wait_rng);METHOD(ai_wait_rng_status);METHOD(ai_wait_rng_row);
    METHOD(md_resume_rng);METHOD(md_resume_rng_status);METHOD(md_resume_rng_row);
    METHOD(boot_hold);METHOD(boot_hold_status);
    METHOD(event_order);METHOD(event_order_status);METHOD(event_rows);METHOD(event_order_row);
    METHOD(gravidar_key);METHOD(gravidar_time);METHOD(gravidar_key_status);METHOD(gravidar);
    METHOD(ai_schedule);METHOD(ai_schedule_status);METHOD(ai_schedule_fallbacks);
    METHOD(gravidar_deadlines);METHOD(gravidar_deadlines_status);
    METHOD(travel_snapshot);METHOD(travel_snapshot_status);
    METHOD(runtime_ids);METHOD(runtime_ids_status);METHOD(runtime_ids_word);METHOD(runtime_ids_map_row);METHOD(runtime_ids_caller_row);METHOD(runtime_ids_callers);
    METHOD(script_rng);METHOD(script_rng_status);METHOD(script_rng_row);METHOD(script_rng_own_entries);METHOD(md_timed_rng);METHOD(md_timed_rng_status);METHOD(find_order);METHOD(find_order_status);METHOD(find_order_row);METHOD(loadout_order);METHOD(loadout_order_status);METHOD(loadout_order_row);METHOD(nav_key);METHOD(attention_all);METHOD(attention_all_status);METHOD(step_trace);METHOD(step_trace_arm);METHOD(step_trace_status);METHOD(step_trace_row);METHOD(step_chain);METHOD(fp_redirect);METHOD(fp_redirect_watch);METHOD(fp_redirect_status);METHOD(fp_redirect_row);METHOD(coverage);METHOD(coverage_frame);METHOD(coverage_status);METHOD(coverage_row);METHOD(nav_cadence);METHOD(nav_cadence_status);METHOD(nav_cadence_row);METHOD(nav_cadence_watch);METHOD(nav_probe_init);METHOD(nav_params_init);METHOD(created_identities);METHOD(portable_created);METHOD(created_load);METHOD(created_identities_status);METHOD(created_row);METHOD(created_identity);METHOD(created_runtime);METHOD(gravidar_wide);METHOD(gravidar_wide_status);METHOD(spawn_rng);METHOD(spawn_rng_status);METHOD(spawn_rng_row);
    METHOD(read_cost);
#undef METHOD
    return 1;
}
#ifdef GATE_LINUX_TEST
/* Test build only (x4lan_gate_test.so from tools/dev/check_lockstep_gate_linux.py,
 * never packaged): the same gate on a caller-owned fake image. exe NULL skips the
 * file pin (synthetic image); sites NULL keeps the real pins (image from the ELF). */
GATE_EXPORT int x4gate_test_setup(unsigned char *fake,const char *exe,tick_fn frame_tick,const lx_site *sites,unsigned count,unsigned park_ms) {
    if(used || !fake || !frame_tick) return 0;
    image=fake;gate_env.exe=exe;gate_env.file=exe!=NULL;gate_env.maps=0;gate_env.pin=0;
    gate_frame_tick=frame_tick;gate_park_ms=park_ms;
    if(sites) {gate_sites=sites;gate_site_count=count;}
    return 1;
}
/* The world hash on a fake image: the engine's find and its registry pointer. */
GATE_EXPORT int x4gate_test_world(void *find,void **map_slot) {
    if(used || !find || !map_slot) return 0;
    world_find=(lxw_find_fn)find;world_component_map=map_slot;return 1;
}
static __thread unsigned char gate_test_tls[0x2800] __attribute__((aligned(16)));
static unsigned char *gate_tls_test_base(void) {return gate_test_tls+sizeof(gate_test_tls);}
GATE_EXPORT unsigned char *x4gate_test_tls(void) {return gate_tls_test_base();}
#endif
