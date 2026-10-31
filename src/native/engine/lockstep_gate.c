/* Opt-in X4 9.00 Windows pre-FrameTick admission. Separate from x4lan_net.
 * MainMessageLoop has joined MoveWorker before this call. Networking stays alive
 * here while the simulation/UI thread is idle. This gates frames, not all async
 * engine workers; battle-state equality is checked separately by the coordinator.
 * No original executable bytes or files are changed on disk. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lockstep_wire.h"
#define GATE_EXPORT __declspec(dllexport)

#define CALL_RVA 0x12ae9d2u
#define WORD_RVA 0x12ae9d0u
#define TICK_RVA 0xfa2d70u
#define GAME_RVA 0x6b60e38u
#define REAL_RVA 0x6b60e40u
#define SPEED_RVA 0x6b60e60u
#define TABLE_RVA 0x6cf1570u
#define EXE_SHA "19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891"
#define CALL_SHA "119ee2003ab898668e3e485723f196ff83e43d99b3e8f26f1394c59b24541d26"
#define TICK_SHA "238662a6fc129b269aec1dbf9fff25fe1aa80138c8fc3be6f996dd65921f30e7"

typedef struct {
    uint64_t issued, completed, waits, report_tick;
    double game, real, wait_game_delta, max_dt_error;
    uint32_t active, reason, restored, slot;
} gate_stats;
static gate_stats stats;
static unsigned char *image, *thunk;
#include "lockstep_thunk_page.h"
#include "lockstep_thunk_unwind.h"
static uint64_t original_word, patched_word;
static SOCKET connection = INVALID_SOCKET;
static char secret[33], digest[65];
static char proposal[9]="00000000",inputs[2][9]={"00000000","00000000"};
/* Protocol 3 is opt-in through x4gate_wire before start/boot; protocol 2 and
 * its two 8-hex commands remain the default. Side features that bind two
 * players (docking, construction, GetUp, portable) require wire_pair(). */
static unsigned wire_version=2,wire_clients=2;
/* Protocol 4 (lockstep_lookahead.h): k>0 runs the protocol 3 input wire with a
 * permit window of k ticks; opt-in through x4gate_lookahead after x4gate_wire. */
static unsigned wire_lookahead;
static char blob_proposal[GATE_BLOB_HEX_MAX+1],blob_staged[GATE_BLOB_HEX_MAX+1];
static char blob_inputs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
static uint64_t blob_staged_tick;
static int blob_staged_set;
static int wire_pair(void) {return wire_version==2 && wire_clients==2;}
static DWORD ui_thread, main_thread;
static unsigned timeout_ms;
static int installed, used;
static double previous_game;
static unsigned __int128 path_original, path_patched;
static int path_installed, path_restored;
static uint64_t path_joins;
static unsigned path_options;
static uint64_t rng_seeds;
/* Set from a joined pathing pass's release to its join: a thread other than the
 * MoveWorker and the main thread that runs code in that window is taken for one
 * of the pass's batch jobs (warp and tempzone attribution only). */
static volatile LONG path_pass_open;
enum {PATH_THREAD_NONE,PATH_THREAD_MOVEWORKER,PATH_THREAD_MAIN,PATH_THREAD_PASS,PATH_THREAD_OTHER};
static unsigned path_thread_class(void) {
    DWORD id=GetCurrentThreadId();
    if(id==ui_thread) return PATH_THREAD_MOVEWORKER;
    if(id==main_thread) return PATH_THREAD_MAIN;
    return InterlockedCompareExchange(&path_pass_open,0,0)?PATH_THREAD_PASS:PATH_THREAD_OTHER;
}
static const char *path_thread_name(unsigned c) {
    static const char *const names[]={"none","moveworker","main","pass","other"};
    return c<sizeof(names)/sizeof(*names)?names[c]:"?";
}
/* Snapshot boot owns fixed dt itself: the ordinary fixedtick Lua owner cannot
 * survive a UI-state reload. Never combine this mode with that DLL. Phases:
 * 0 menu; 1 load requested; 2 loading observed; 3 first live frame admitted. */
static int boot_mode, boot_phase, boot_adopted, dt_installed, dt_restored;
static uint64_t saved_dt_bits, fixed_dt_bits;
static ULONGLONG boot_started;
static uint64_t boot_options;
static unsigned remote_port;
/* Relay address in network order; loopback unless the probe sets a remote
 * relay (native_gate_relay_host) before the gate starts. */
static uint32_t relay_address;
#include "lockstep_gate_common.h"
#include "lockstep_tick.h"
/* The floating-point modules' code patches (lockstep_exact_reciprocal.h windows, the
 * lockstep_portable_math.h and lockstep_decimal.h entry jumps): while one is in place,
 * code reads through hash_ok, read_local and sample_read see the original bytes, so other
 * modules' pins over those functions keep passing in either arming order (defined after
 * the module includes). The modules themselves read their own bytes raw. */
static int fp_patch_overlaps(const void *source,size_t bytes);
static void fp_patch_view(const void *source,void *copy,size_t bytes);
static int hash_ok(const void *data, ULONG length, const char *expected) {
    BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;unsigned char raw[32];char hex[65];int ok=0;unsigned char *view=NULL;
    if (fp_patch_overlaps(data,length)) {
        if (!(view=malloc(length))) return 0;
        memcpy(view,data,length);fp_patch_view(data,view,length);data=view;
    }
    if (BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,NULL,0)<0) goto done;
    if (BCryptCreateHash(a,&h,NULL,0,NULL,0,0)<0) goto done;
    if (BCryptHashData(h,(PUCHAR)data,length,0)<0 || BCryptFinishHash(h,raw,32,0)<0) goto done;
    hex32(raw,hex);ok=strcmp(hex,expected)==0;
done: if(h) BCryptDestroyHash(h);if(a) BCryptCloseAlgorithmProvider(a,0);free(view);return ok;
}
/* gate.sha256: the probe's per-tick declared-state digest (64 lowercase hex, as X4LAN_SHA256.digest).
 * Pure-Lua SHA-256 costs ~4 ms per play config tick (~16 KB) without the JIT; BCrypt ~7 us. UI thread
 * only; the algorithm handle is opened once and kept for the process. */
static BCRYPT_ALG_HANDLE digest_alg;
static int digest_hex(const void *data,size_t length,char out[65]) {
    BCRYPT_HASH_HANDLE h=NULL;unsigned char raw[32];int ok=0;
    if(length>0xffffffffu) return 0;
    if(!digest_alg && BCryptOpenAlgorithmProvider(&digest_alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0) {digest_alg=NULL;return 0;}
    if(BCryptCreateHash(digest_alg,&h,NULL,0,NULL,0,0)>=0 && BCryptHashData(h,(PUCHAR)data,(ULONG)length,0)>=0 &&
       BCryptFinishHash(h,raw,32,0)>=0) {hex32(raw,out);ok=1;}
    if(h) BCryptDestroyHash(h);
    return ok;
}
static int file_ok(void) {
    WCHAR path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768),got;
    if(!n||n>=32768) return 0;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;BCRYPT_ALG_HANDLE a=NULL;BCRYPT_HASH_HANDLE h=NULL;
    unsigned char buffer[65536],raw[32];char hex[65];int ok=0;
    if(!GetFileSizeEx(f,&size)||size.QuadPart!=55412768) goto done;
    if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 || BCryptCreateHash(a,&h,NULL,0,NULL,0,0)<0) goto done;
    for(;;) {if(!ReadFile(f,buffer,sizeof(buffer),&got,NULL)) goto done;if(!got) break;if(BCryptHashData(h,buffer,got,0)<0) goto done;}
    if(BCryptFinishHash(h,raw,32,0)<0) goto done;hex32(raw,hex);ok=strcmp(hex,EXE_SHA)==0;
done: if(h) BCryptDestroyHash(h);if(a) BCryptCloseAlgorithmProvider(a,0);CloseHandle(f);return ok;
}
static int verify(void) {
    image=(unsigned char*)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *d=(IMAGE_DOS_HEADER*)image;
    if(d->e_magic!=IMAGE_DOS_SIGNATURE||d->e_lfanew<0||d->e_lfanew>4096) return 0;
    IMAGE_NT_HEADERS64 *n=(IMAGE_NT_HEADERS64*)(image+d->e_lfanew);
    if(n->Signature!=IMAGE_NT_SIGNATURE||n->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
       n->FileHeader.TimeDateStamp!=0x6a1eaded||n->OptionalHeader.SizeOfImage!=0x6fa9000||!file_ok()) return 0;
    return hash_ok(image+0x12ae9c7,24,CALL_SHA)&&hash_ok(image+TICK_RVA,32,TICK_SHA);
}
static int swap_word(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+WORD_RVA,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+WORD_RVA),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+WORD_RVA,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+WORD_RVA,8);
    return actual==expected;
}
static int swap_path(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;
    if(!VirtualProtect(image+0xef4610,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+0xef4610),expected,value);
    VirtualProtect(image+0xef4610,16,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),image+0xef4610,16);return actual==expected;
}
static int swap_dt_branch(unsigned short expected,unsigned short value) {
    DWORD old,ignored;if(!VirtualProtect(image+0xfa3302,2,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned short actual=(unsigned short)InterlockedCompareExchange16((volatile SHORT*)(image+0xfa3302),(SHORT)value,(SHORT)expected);
    VirtualProtect(image+0xfa3302,2,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+0xfa3302,2);
    return actual==expected;
}
static void restore_dt(void) {
    if(!dt_installed) return;
    /* A foreign owner must never be overwritten, including the data word. */
    int branch=swap_dt_branch(0x9066,0x0874);
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+0x6adfc60),
        (LONG64)saved_dt_bits,(LONG64)fixed_dt_bits);
    dt_restored=branch && actual==fixed_dt_bits;
    if(dt_restored) dt_installed=0;
}
static int install_dt(void) {
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll") || *(unsigned char*)(image+0x6adfc58) ||
       !hash_ok(image+0xfa32d2,58,"a5bc59769a92153bec1c80192aa0d71459768e85442c9c5b8d2c72536701bfd7")) return 0;
    double fixed=gate_tick_dt();memcpy(&fixed_dt_bits,&fixed,8);memcpy(&saved_dt_bits,image+0x6adfc60,8);
    if(!isfinite(*(double*)(image+0x6adfc60))) return 0;
    if((uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+0x6adfc60),
        (LONG64)fixed_dt_bits,(LONG64)saved_dt_bits)!=saved_dt_bits) return 0;
    if(!swap_dt_branch(0x0874,0x9066)) {
        InterlockedCompareExchange64((volatile LONG64*)(image+0x6adfc60),(LONG64)saved_dt_bits,(LONG64)fixed_dt_bits);return 0;
    }
    dt_installed=1;return 1;
}
static int boot_next(int phase,int loading) {
    if(phase==1 && loading) return 2;
    if(phase==2 && !loading) return 3;
    return phase;
}
static int accept_ui(void) {
    DWORD thread=GetCurrentThreadId();
    if(thread==ui_thread) return 1;
    if(boot_mode && boot_phase==3 && !boot_adopted && stats.issued && thread!=main_thread) {
        void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
        if(slots && slots[0] && *((unsigned char*)slots[0]+0x314)==1) {
            ui_thread=thread;boot_adopted=1;return 1;
        }
    }
    return 0;
}
/* Shared guarded native identities; pointer reuse alone is never identity. */
#include "lockstep_registry.h"
static void snapshot_import_capture(void *,uint64_t,uint64_t);
static void world_trace_import_capture(void *,uint64_t,uint64_t);
static void transition_import_capture(void *,uint64_t,uint64_t);
static void origin_import_capture(void *,uint64_t,uint64_t);
static void origin_transition_edge(uint64_t);
static void portable_import_capture(void *,uint64_t,uint64_t);
static void snapshot_nav_observe(unsigned,const void *);
static int read_local(const void *,void *,size_t);
static int sample_read(const void *,void *,size_t);
static int read_local_fast;
/* native_gate_play_mode, default off: no observer sample tables at all (lockstep_interrupt_faults.h).
 * The keys, the digests and the world captures are untouched; only rows nobody plays with go. */
static int gate_play_mode;
static int hex_words(char *,size_t,const uint32_t *,unsigned);
#include "lockstep_seed.h"
#include "lockstep_solver_scope.h"
#include "lockstep_request_scope.h"
#include "lockstep_world_identity.h"
#include "lockstep_actor_all.h"
#include "lockstep_world_trace.h"
#include "lockstep_flight.h"
#include "lockstep_ai.h"
#include "lockstep_interrupt.h"
#include "lockstep_timer_resolution.h"
#include "lockstep_job_rng.h"
#include "lockstep_script_rng_hooks.h"
#include "lockstep_script_rng_md.h"
#include "lockstep_boot_hold.h"
#include "lockstep_md_resume_rng.h"
#include "lockstep_find_order.h"
#include "lockstep_region_rng.h"
#include "lockstep_radar.h"
#include "lockstep_gravidar_wide.h"
#include "lockstep_gravidar_posts.h"
#include "lockstep_gravidar.h"
/* Stock slots 2..N give their PlayerControllers a private vtable copy (lockstep_pilot_slots.h):
 * readers that identify a controller by its vtable map a copy to the stock PlayerController one,
 * and readers of a ship's state accept a copy (never a stock PlayerController: that is the local
 * player's own controller). Report section 16.8 lists every reader and its decision. */
static uintptr_t pslot_canonical_vtable(uintptr_t vtable);
static int pslot_vtable_is_copy(uintptr_t vtable);
#include "lockstep_agent.h"
#include "lockstep_spline.h"
#include "lockstep_ship.h"
#include "lockstep_weapon.h"
#include "lockstep_params.h"
#include "lockstep_nav.h"
#include "lockstep_nav_cadence.h"
#include "lockstep_nav_player.h"
#include "lockstep_ship_sound.h"
#include "lockstep_draw_watch.h"
#include "lockstep_player_timer.h"
#include "lockstep_warp_rng.h"
#include "lockstep_transition_rows.h"
#include "lockstep_tempzone_order.h"
#include "lockstep_state.h"
#include "lockstep_dock.h"
#include "lockstep_damage.h"
#include "lockstep_contact.h"
#include "lockstep_velocity.h"
#include "lockstep_body.h"
#include "lockstep_shape.h"
#include "lockstep_bullet.h"
#include "lockstep_bullet_cadence.h"
#include "lockstep_speak_timer.h"
#include "lockstep_death.h"
#include "lockstep_mass.h"
#include "lockstep_attention.h"
#include "lockstep_world_hash.h"
#include "lockstep_world_created.h"
#include "lockstep_runtime_ids.h"
#include "lockstep_world_trace_early.h"
#include "lockstep_world_queue.h"
#include "lockstep_world_wait.h"
#include "lockstep_world_action.h"
#include "lockstep_world_values.h"
#include "lockstep_world_unknown.h"
#include "lockstep_world_values_hooks.h"
#include "lockstep_world_find_rng.h"
#include "lockstep_world_position.h"
#include "lockstep_world_position_hooks.h"
#include "lockstep_world_position_rng.h"
#include "lockstep_world_position_rng_pins.h"
#include "lockstep_world_position_rng_config.h"
#include "lockstep_world_safe_position.h"
#include "lockstep_player_observer.h"
#include "lockstep_snapshot_fixture.h"
#include "lockstep_solver.h"
#include "lockstep_request.h"
#include "lockstep_player_fire.h"
#include "lockstep_player_rng.h"
#include "lockstep_registry_native.h"
#include "lockstep_registry_raw.h"
#include "lockstep_registry_origin_identity.h"
#include "lockstep_registry_origin.h"
#include "lockstep_registry_md.h"
#include "lockstep_spawn_rng.h"
#include "lockstep_creation_trace.h"
#include "lockstep_portable_windows.h"
#include "lockstep_rotation_win.h"
#include "lockstep_portable_transport.h"
#include "lockstep_transition.h"
#include "lockstep_player_origin_native.h"
static int world_nav_live_prefixes(void) {
    static const unsigned char original[8]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c};
    const unsigned sites[2]={0xea4c00,0xe85360},dispatch[2]={2848,3360};
    for(unsigned i=0;i<2;i++) {
        unsigned char prefix[8];int32_t displacement;
        if(!read_local(image+sites[i],prefix,sizeof(prefix))) return 0;
        if(!memcmp(prefix,original,sizeof(prefix))) continue;
        if(!thunk || prefix[0]!=0xe9 || memcmp(prefix+5,original+5,3)) return 0;
        memcpy(&displacement,prefix+1,4);
        if((uintptr_t)(image+sites[i]+5)+(intptr_t)displacement!=(uintptr_t)(thunk+dispatch[i])) return 0;
        if(i) {if(!thunk_slot_ready(3328)) return 0;}
        else {
            unsigned char expected[64]={0};
            if(!transition_encode(expected,expected+32,image+sites[i],(void*)transition_transfer,0,0) ||
               !thunk_bytes_match(2816,expected,sizeof(expected))) return 0;
        }
    }
    return 1;
}
#include "lockstep_player_getup_native.h"
#include "lockstep_creation_rng.h"
#include "lockstep_registry_creation.h"
#include "lockstep_path_determinism.h"
#include "lockstep_travel_determinism.h"
#include "lockstep_travel_snapshot.h"
#include "lockstep_command_order.h"
#include "lockstep_event_rng.h"
#include "lockstep_event_order.h"
#include "lockstep_ai_schedule.h"
#include "lockstep_render_rng.h"
#include "lockstep_camera_perturb.h"
#include "lockstep_camera_offset.h"
#include "lockstep_pilot_inputs.h"
#include "lockstep_pilot_predict.h"
#include "lockstep_hit_native.h"
#include "lockstep_pilot_fire.h"
#include "lockstep_virtual_players.h"
#include "lockstep_pilot_slots.h"
#include "lockstep_pilot_present.h"
#include "lockstep_pilot_death.h"
#include "lockstep_crash_note.h"
#include "lockstep_cutscene_ready.h"
#include "lockstep_ui_replay.h"
#include "lockstep_view_points.h"
#include "lockstep_camera_anchor.h"
#include "lockstep_mass_traffic.h"
#include "lockstep_region_camera.h"
#include "lockstep_weapon_fire.h"
#include "lockstep_engine_rng.h"
#include "lockstep_build_rng.h"
#include "lockstep_ui_cargo.h"
#include "lockstep_supply_rows.h"
#include "lockstep_ui_lazy.h"
#include "lockstep_supply_wants.h"
#include "lockstep_loadout_order.h"
#include "lockstep_order_canon.h"
#include "lockstep_pick_order.h"
#include "lockstep_macro_arena.h"
#include "lockstep_build_rows.h"
#include "lockstep_economy_load.h"
#include "lockstep_motion_watch.h"
/* The Linear step trace (lockstep_step_trace.h; diagnostic, Option B evidence E2). */
#define ST_SLOT 0x2c4f680u /* slot 14 of U::Path::LinearFlightControlModel's vtable 0x2c4f610 */
#define ST_STEP 0xeb27c0u
#define ST_MODEL_AGENT 0x10
#define ST_AGENT_OWNER 0xef0
#define ST_AGENT_POSE 0xad0 /* the two pathing state banks ([NavigationController+0x30]; Linux +0xaf0) */
#define ST_BANK_READ (image+0x38f4ee4u)
#define ST_BANK_WRITE (image+0x2f4b6f4u)
#define ST_CHAIN_SITE_COUNT 10u /* FR8 (lockstep_step_chain_win.h), then FR17's two, then FR18's two */
#define ST_PT_REF 0x10 /* FR18: a path point's reference object runtime id (+0x18 its sub id, +0x20 the offset mat4) */
/* FR19: the Linear model's SetTarget slot (vtable 0x2c4f610 +0x18; 0x2c4f628 -> 0xeaf620, pinned), its target, the agent's path point pointer. */
#define ST_SG_SLOT 0x2c4f628u
#define ST_SG_FN 0xeaf620u
#define ST_SG_SIZE 0x31u
#define ST_SG_SHA "0d2c642e7809f6d293ce4b5bcaee47a8443e49fb4ab7c644df2b8f0a0f475e73"
#define ST_MODEL_TARGET 0x20
#define ST_AGENT_POINT 0x6a8
#define ST_MODEL_SPLINE 0xe0 /* FR9: Math::CompositeSpline<5> (vtable 0x2b6c6d0), built by 0xeb1e60 at 0xeb2ab2 */
#include "lockstep_step_trace.h"
static int st_slot_swap(void *expected,void *value) {
    unsigned char *p=image+ST_SLOT;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int st_sg_swap(void *expected,void *value) {
    unsigned char *p=image+ST_SG_SLOT;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
#include "lockstep_step_chain_win.h"
/* The flight parameter refresh recomputed next to native (lockstep_fp_redirect.h; Option B slice 1, default off). */
#include "lockstep_fp_redirect_win.h"
/* Function coverage (lockstep_coverage.h; diagnostic, Option B evidence E1): int3 marks taken by a first
 * vectored handler, the list coverage.txt next to this DLL (image RVAs), the marked pages r-w-x for the window. */
static int cov_page_open(uintptr_t address);
#include "lockstep_coverage.h"
#define COV_PAGES 2048u
static uintptr_t cov_pages[COV_PAGES];
static DWORD cov_page_old[COV_PAGES];
static unsigned cov_page_count;
static PVOID cov_veh;
static int cov_in_text(uintptr_t address) {return address>=(uintptr_t)image+0x1000u && address<(uintptr_t)image+0x1000u+0x1bbf70cu;} /* .text */
static void cov_flush(uintptr_t address) {FlushInstructionCache(GetCurrentProcess(),(void*)address,1);}
static int cov_page_open(uintptr_t address) {
    const uintptr_t page=address&~(uintptr_t)0xfff;DWORD old;
    for(unsigned i=0;i<cov_page_count;i++) if(cov_pages[i]==page) return 1;
    if(cov_page_count>=COV_PAGES || !VirtualProtect((void*)page,0x1000,PAGE_EXECUTE_READWRITE,&old)) return 0;
    cov_pages[cov_page_count]=page;cov_page_old[cov_page_count]=old;cov_page_count++;return 1;
}
static int cov_pages_close(void) {
    int ok=1;DWORD ignored;
    for(unsigned i=0;i<cov_page_count;i++) if(!VirtualProtect((void*)cov_pages[i],0x1000,cov_page_old[i],&ignored)) ok=0;
    cov_page_count=0;return ok;
}
static LONG CALLBACK cov_vectored(PEXCEPTION_POINTERS e) {
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT) {
        const uintptr_t address=(uintptr_t)e->ExceptionRecord->ExceptionAddress;
        if(cov_trap(address)) {e->ContextRecord->Rip=address;return EXCEPTION_CONTINUE_EXECUTION;}
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static int cov_install_handler(void) {
    if(!cov_veh) cov_veh=AddVectoredExceptionHandler(1,cov_vectored);
    return cov_veh!=NULL;
}
/* coverage.txt next to this DLL. */
static int cov_load(void) {
    HMODULE module=NULL;WCHAR path[MAX_PATH+16];
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(void*)cov_load,&module)) return 0;
    DWORD n=GetModuleFileNameW(module,path,MAX_PATH);if(!n || n>=MAX_PATH) return 0;
    while(n && path[n-1]!=(WCHAR)0x5c && path[n-1]!=L'/') n--; /* 0x5c: backslash */
    static const WCHAR name[]=L"coverage.txt";memcpy(path+n,name,sizeof(name));
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    static char text[COV_MAX*20];DWORD got=0;BOOL ok=ReadFile(f,text,sizeof(text),&got,NULL);
    LARGE_INTEGER size;BOOL sized=GetFileSizeEx(f,&size);CloseHandle(f);
    return ok && sized && size.QuadPart==(LONGLONG)got && cov_load_text(text,got);
}
#include "lockstep_physics_trace.h"
#include "lockstep_body_order.h"
#include "lockstep_shape_order.h"
#include "lockstep_nav_octree_sites.h"
#include "lockstep_nav_octree.h"
#include "lockstep_gravidar_digest.h"
#include "lockstep_gravidar_deadlines.h"
#include "lockstep_portable_math.h"
#include "lockstep_exact_reciprocal.h"
#include "lockstep_fp_env.h"
#include "lockstep_decimal.h"
#include "lockstep_path_budget.h"
static int fp_patch_overlaps(const void *source,size_t bytes) {
    return xrcp_overlaps(source,bytes) || pmath_overlaps(source,bytes) || pdec_overlaps(source,bytes) || pbud_overlaps(source,bytes) ||
        stc_overlaps(source,bytes) || fr_len_overlaps(source,bytes);
}
static void fp_patch_view(const void *source,void *copy,size_t bytes) {
    xrcp_unpatch_copy(source,copy,bytes);pmath_unpatch_copy(source,copy,bytes);pdec_unpatch_copy(source,copy,bytes);pbud_unpatch_copy(source,copy,bytes);
    stc_unpatch_copy(source,copy,bytes);fr_len_unpatch_copy(source,copy,bytes);
}
static void close_connection(void) {
    SOCKET owned=(SOCKET)InterlockedExchange64((volatile LONG64*)&connection,(LONG64)INVALID_SOCKET);
    if(owned!=INVALID_SOCKET) {closesocket(owned);WSACleanup();}
}
static int gate_sites_owned(void) {
    return !(*(uint64_t*)(image+WORD_RVA)!=patched_word || (path_installed && *(unsigned __int128*)(image+0xef4610)!=path_patched) || !travel_owned() || !tsnap_owned() || !pilot_owned() ||
       (ship_installed && *(void**)(image+SHIP_ENTRY)!=(void*)ship_move) ||
       !weapon_owned() || !bullet_owned() || !bullet_cadence_owned() || !speak_timer_owned() || !cr_owned() || !nav_cadence_owned() || !nav_player_owned() || !ss_owned() || !pt_owned() || !wr_owned() || !trr_owned() || !tz_owned() || !gwide_owned() || !view_owned() || !mt_owned() || !rc_owned() || !wfire_owned() || !motion_order_owned() || !ptrace_owned() || !border_owned() || !sorder_owned() || !erng_owned() || !brng_owned() || !uic_owned() || !spr_owned() || !ul_owned() || !noct_owned() || !pmath_owned() || !xrcp_owned() || !fpenv_owned() || !pdec_owned() || !pbud_owned() || !sw_owned() || !ll_owned() || !oc_owned() || !po_owned() || !ma_owned() || !br_owned() || !eco_owned() ||
       (attention_installed && *(uint64_t*)(image+ATTENTION_ENTRY)!=attention_patched_word) ||
       (mass_installed && *(uint64_t*)(image+MASS_ENTRY)!=mass_patched_word) ||
       (death_installed && *(void**)(image+DEATH_ENTRY)!=(void*)death_listener) ||
       (params_installed && *(unsigned __int128*)(image+PARAMS_ENTRY)!=params_patched_word) ||
       (nav_installed && *(void**)(image+NAV_ENTRY)!=(void*)nav_move) ||
       (velocity_installed && *(void**)(image+VELOCITY_ENTRY)!=(void*)velocity_move) ||
       (shape_installed && *(uint64_t*)(image+SHAPE_ENTRY)!=shape_patched_word) ||
       (agent_installed && *(int32_t*)(image+AGENT_REL)!=agent_patched_rel) ||
       (ai_installed && *(unsigned __int128*)(image+AI_WORD)!=ai_patched_word) ||
       (radar_installed && *(void**)(image+RADAR_ENTRY)!=(void*)radar_handler) ||
       (interrupt_installed && *(uint64_t*)(image+INTERRUPT_ENTRY)!=interrupt_patched_word) ||
       (job_rng_installed && *(uint64_t*)(image+JOB_QUEUE_ENTRY)!=job_rng_patched_word) || !script_rng_owned() ||
       !md_timed_owned() || !boot_hold_owned() || !md_resume_owned() || !find_order_owned() || !region_rng_owned() || !ai_wait_owned() ||
       !spawn_rng_owned() || !gdl_owned() ||
       (interrupt_step_installed && *(uint64_t*)(image+INTERRUPT_STEP_ENTRY)!=interrupt_step_patched_word) ||
       (nested_action_installed && *(unsigned __int128*)(image+NESTED_ACTION_WORD)!=nested_action_patched) ||
       (npc_exec_installed && *(uint64_t*)(image+NPC_EXEC_ENTRY)!=npc_exec_patched_word) ||
       (npc_speak_installed && *(uint64_t*)(image+NPC_SPEAK_ENTRY)!=npc_speak_patched_word) ||
       (npc_enqueue_installed && *(uint64_t*)(image+NPC_ENQUEUE_ENTRY)!=npc_enqueue_patched_word) ||
       (npc_finish_installed && *(uint64_t*)(image+NPC_FINISH_ENTRY)!=npc_finish_patched_word) ||
       (npc_ready_installed && *(unsigned __int128*)(image+NPC_READY_WORD)!=npc_ready_patched) ||
       (npc_deadline_installed && *(unsigned __int128*)(image+NPC_DEADLINE_WORD)!=npc_deadline_patched) ||
       (world_identity_installed && *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)!=world_identity_patched_word) ||
       (created_installed && *(uint64_t*)(image+CREATED_ENTRY)!=created_patched_word) ||
       (rid_installed && *(uint64_t*)(image+RID_REMOVE_ENTRY)!=rid_patched_word) ||
       (interrupt_event_installed && *(void**)(image+INTERRUPT_EVENT_ENTRY)!=(void*)interrupt_event) ||
       (damage_installed && *(uint64_t*)(image+DAMAGE_ENTRY)!=damage_patched_word) ||
       (contact_installed && *(uint64_t*)(image+CONTACT_ENTRY)!=contact_patched_word) ||
       (contact_properties_installed && *(uint64_t*)(image+CONTACT_PROPERTIES_ENTRY)!=contact_properties_patched_word) ||
       (gravidar_installed && *(uint64_t*)(image+GRAVIDAR_WORD)!=gravidar_patched_word) ||
       (gravidar_unit_installed && *(void**)(image+GRAVIDAR_UNIT_ENTRY)!=(void*)gravidar_unit) ||
       (flight_installed && *(void**)(image+FLIGHT_ENTRY)!=(void*)flight_update) || *(double*)(image+SPEED_RVA)!=1.0 ||
       (flight_ctor_installed && *(uint64_t*)(image+FLIGHT_CTOR)!=flight_ctor_patched_word) ||
       (event_order_installed && *(uint64_t*)(image+EVENT_ORDER_SITE)!=event_order_patched_word) ||
       *(unsigned short*)(image+0xfa3302)!=0x9066||*(double*)(image+0x6adfc60)!=gate_tick_dt() || *(unsigned char*)(image+0x6adfc58));
}
#include "lockstep_getup_diagnostic_transport.h"
static void finish(unsigned reason) {
    cov_unmark(); /* first: every coverage mark and page back before any other restore changes a page */
    timer_resolution_restore();
    int transport_owned=portable_wire_enabled || gd_enabled;
    AcquireSRWLockExclusive(&portable_wire_lock);
    if(transport_owned && portable_wire_finish_started) {ReleaseSRWLockExclusive(&portable_wire_lock);return;}
    if(!transport_owned || stats.active || !stats.reason) stats.reason=reason;
    if(transport_owned) {
        /* Acquire before publishing inactive: a suspended main continuation
         * cannot pass the shared side before this stop owns restoration. */
        AcquireSRWLockExclusive(&portable_wire_restore_lock);
        portable_wire_finish_started=1;++portable_wire_epoch;
        InterlockedExchange(&portable_wire_restore_complete,0);
    }
    rotation_native_stop();
    pgetup_stop(&getup_ledger);
    registry_creation_stop(); /* Additive evidence admission closes before native active clears. */
    created_stop(); /* No created identity is recorded once STOP has begun. */
    creation_rng_stop(); /* Close admission under the same lock as the first RNG write. */
    world_position_rng_restore(); /* Seed admission closes before active clears. */
    InterlockedExchange((volatile LONG*)&stats.active,0);
    render_rng_restore();
    camera_perturb_restore();
    camoff_restore();
    camfol_restore();
    view_restore();
    mt_restore();
    rc_restore();
    wfire_restore();
    erng_restore();
    brng_restore();
    uic_restore();
    spr_restore();
    ul_restore();
    pmath_restore();
    xrcp_restore();
    fpenv_restore();
    pdec_restore();
    pbud_restore();
    sw_restore();
    ll_restore();
    oc_restore();
    po_restore();
    ma_restore();
    br_restore();
    eco_restore();
    motion_order_restore();
    st_restore();
    fr_restore();
    ptrace_restore();
    border_restore();
    sorder_restore();
    noct_restore();
    nos_restore();
    ReleaseSRWLockExclusive(&portable_wire_lock);
    InterlockedExchange(&world_trace_enabled,0);
    rotation_native_restore();
    origin_restore();transition_restore();
    hitn_restore(); /* the call site goes back before the claims' slot ships are released */
    pfire_restore(); /* triggers still ours are cleared */
    pilot_restore(); /* Before the shared request entry's observer restore. */
    request_restore();
    solver_restore();
    world_safe_position_stop();
    world_position_restore();
    world_find_restore();
    world_values_restore();
    world_action_restore();
    world_wait_restore();
    ai_wait_restore(); /* after world_wait_restore: the entry is shared */
    world_queue_restore();
    player_observer_restore();
    creation_restore();
    spawn_rng_restore(); /* the Generate entry word; the creation trace never owns it at the same time */
    registry_origin_close();
    registry_md_restore();
    world_identity_restore();
    job_rng_restore();
    script_rng_restore();
    md_timed_restore();
    boot_hold_restore();
    md_resume_restore();
    find_order_restore();
    region_rng_restore();
    gdl_restore(); /* the U::Sector slot */
    interrupt_restore();
    contact_restore();
    damage_restore();
    gravidar_restore();
    gpost_restore();
    gwide_restore();
    attention_restore();
    mass_restore();
    death_restore();
    speak_timer_restore();
    cr_restore();
    bullet_cadence_restore(); /* the code site first, then the Move wrapper that counts for it */
    bullet_restore();
    shape_restore();
    velocity_restore();
    ss_restore();
    pt_restore();
    wr_restore();
    trr_restore();
    tz_restore();
    dw_stop();
    nav_player_restore(); /* the player controller entry first: it counts through the cadence */
    nav_cadence_restore(); /* the code sites first, then the Move wrapper that counts for them */
    nav_restore();
    params_restore();
    weapon_restore();
    ship_restore();
    spline_restore();
    agent_restore();
    radar_restore();
    ai_restore();
    flight_restore();
    if(path_installed) {path_restored=swap_path(path_patched,path_original);if(path_restored) path_installed=0;}
    if(installed) {stats.restored=swap_word(patched_word,original_word);if(stats.restored) installed=0;}
    path_det_restore();if(path_det_budget_applied) stats.restored=0;
    travel_restore();if(travel_installed) stats.restored=0;
    tsnap_restore();if(tsnap_installed) stats.restored=0;
    ai_sched_restore();
    event_rng_stop();
    event_order_restore();if(event_order_installed) stats.restored=0;
    created_restore();if(created_installed) stats.restored=0;
    rid_restore();if(rid_installed) stats.restored=0;
    if(render_rng_installed) stats.restored=0;
    if(bullet_cadence_installed) stats.restored=0;
    if(speak_timer_installed) stats.restored=0;
    if(cr_patched) stats.restored=0;
    if(nav_cadence_installed) stats.restored=0;
    if(nav_player_installed) stats.restored=0;
    if(ss_installed) stats.restored=0;
    if(pt_installed) stats.restored=0;
    if(wr_installed) stats.restored=0;
    if(trr_installed) stats.restored=0;
    if(tz_installed) stats.restored=0;
    if(gwide_armed && !gwide_restored) stats.restored=0;
    if(view_installed) stats.restored=0;
    if(st_installed || st_chain_installed) stats.restored=0;
    if(fr_installed) stats.restored=0;
    if(mt_installed) stats.restored=0;
    if(rc_installed) stats.restored=0;
    if(wfire_installed) stats.restored=0;
    if(erng_installed) stats.restored=0;
    if(brng_installed) stats.restored=0;
    if(uic_installed) stats.restored=0;
    if(spr_installed) stats.restored=0;
    if(ul_installed) stats.restored=0;
    if(pmath_installed) stats.restored=0;
    if(xrcp_installed) stats.restored=0;
    if(fpenv_installed) stats.restored=0;
    if(pdec_installed) stats.restored=0;
    if(sw_installed) stats.restored=0;
    if(ll_installed) stats.restored=0;
    if(oc_installed) stats.restored=0;
    if(po_installed) stats.restored=0;
    if(ma_installed) stats.restored=0;
    if(br_installed) stats.restored=0;
    if(eco_installed) stats.restored=0;
    if(motion_order_installed) stats.restored=0;
    if(ptrace_installed || border_installed || sorder_installed || noct_installed) stats.restored=0;
    if(registry_md_owned && !registry_md_restored) stats.restored=0;
    if(pilot_armed && !pilot_restored) stats.restored=0;
    if(pres_armed && !pres_restored) stats.restored=0;
    if(uirep_armed && !uirep_restored) stats.restored=0;
    if(hitn_installed) stats.restored=0;
    if(script_rng_armed && !script_rng_restored) stats.restored=0;
    if((md_timed_armed && !md_timed_restored) || (boot_hold_armed && !boot_hold_restored)) stats.restored=0;
    if(md_resume_armed && !md_resume_restored) stats.restored=0;
    if(find_order_armed && !find_order_restored) stats.restored=0;
    if(region_rng_armed && !region_rng_restored) stats.restored=0;
    if(gdl_armed && !gdl_restored) stats.restored=0;
    if(spawn_rng_armed && !spawn_rng_restored) stats.restored=0;
    if(ai_wait_armed && !ai_wait_restored) stats.restored=0;
    if(speech_time_armed && npc_deadline_installed) stats.restored=0; /* interrupt_restore owns the shared deadline word */
    if(creation_owned || creation_restore_failures || creation_rng_active || creation_rng_restore_failures) stats.restored=0;
    if(!rotation_native_restored()) stats.restored=0;
    restore_dt();
    if(!portable_wire_draining && !(gd_enabled && boot_phase==3)) close_connection();
    if(transport_owned) {
        InterlockedExchange(&portable_wire_restore_complete,1);
        ReleaseSRWLockExclusive(&portable_wire_restore_lock);
    }
}
static void gate_original_once(tick_fn original,void *frame,unsigned char loading) {
    if(gd_enabled && boot_phase==3) {gd_original(original,frame,loading);return;}
    int counted=portable_native_configured && boot_phase==3,claimed=0;
    if(counted && portable_wire_enabled) {
        /* Order the continuation claim against cancellation, without holding a
         * lock across original: its Lua callbacks may synchronously stop us.
         * A later stop may overlap an already claimed original. An earlier
         * stop must finish its restoration attempt before we continue. */
        AcquireSRWLockExclusive(&portable_wire_lock);
        if(stats.active && !portable_wire_finish_started) {
            InterlockedIncrement64(&portable_native_calls);claimed=1;
        }
        ReleaseSRWLockExclusive(&portable_wire_lock);
        if(!claimed) {
            AcquireSRWLockShared(&portable_wire_restore_lock);
            if(!InterlockedCompareExchange(&portable_wire_restore_complete,0,0)) {
                portable_wire_stage="restoration_not_published";InterlockedIncrement64(&portable_native_failures);
            }
            ReleaseSRWLockShared(&portable_wire_restore_lock);
        }
    }
    if(counted && !claimed) InterlockedIncrement64(&portable_native_calls);
    original(frame,loading);
    if(counted) InterlockedIncrement64(&portable_native_returns);
}
/* Ordering experiment: the worker still computes its own movement, and the
 * original late join still applies queued orders and swaps state buffers. Wait
 * for the pass before universe/AI updates can race its reads; replace exactly
 * the one completion token we consumed for the original join to consume later. */
static BOOL WINAPI path_release(HANDLE semaphore,LONG count,LPLONG previous) {
    if(boot_mode && boot_phase<3) return ReleaseSemaphore(semaphore,count,previous);
    if(boot_mode) accept_ui();
    unsigned char *manager=*(unsigned char**)(image+0x6cf21c8);
    int valid=stats.active && GetCurrentThreadId()==ui_thread && manager &&
        count==1 && previous==NULL && semaphore==*(HANDLE*)(manager+0x18) &&
        *(int*)(manager+0x84)==2 && manager[0x88]==0;
    if(valid) gate_sim_begin();
    if(valid && !snapshot_prepare()) {
        InterlockedIncrement64(&snapshot_failures);finish(74);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    if(valid && !world_trace_boundary(WT_PATH_BEFORE)) {
        InterlockedIncrement64(&world_trace_early_failures);finish(74);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    if(valid && ai_sched_armed && !ai_sched_frame()) {
        InterlockedIncrement64(&ai_sched_failures);finish(84);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    gravidar_publish_fn publish=(ai_sched_mask&AI_SCHED_ORDER)?ai_sched_publish:(gravidar_publish_fn)(image+GRAVIDAR_PUBLISH);
    if(gwide_mode==GWIDE_MODE_SORT) {gwide_inner=publish;publish=gwide_publish;} /* census-zone: zone groups in portable order for the pass */
    if(valid && gravidar_installed && !gravidar_frame(publish,
        *(void**)(image+0x6cf1650),1000)) {InterlockedIncrement64(&gravidar_failures);finish(62);}
    /* The pass is joined: put the Gravidar zone groups, then the sector list, back into address order. */
    if(valid && stats.active && gwide_permuted && !gwide_settle()) finish(GWIDE_STOP_REASON);
    if(valid && stats.active && ai_sched_armed && !ai_sched_settle()) {InterlockedIncrement64(&ai_sched_failures);finish(84);}
    if(valid && (path_det_mask&PATH_DET_BUDGET_BIT) && !path_det_budget_frame(manager)) {
        InterlockedIncrement64(&path_det_failures);finish(79);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    if(valid && InterlockedCompareExchange(&tsnap_active,0,0) && !tsnap_before_release(manager)) {
        InterlockedIncrement64(&tsnap_failures);finish(89);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    if(valid && stats.active && pilot_armed && !pilot_before_release()) {
        InterlockedIncrement64(&pilot_failures);finish(pilot_stop_reason());
        return ReleaseSemaphore(semaphore,count,previous);
    }
    /* Pilot firing: this tick's triggers, after the pilot frame table, before slot 3. */
    if(valid && stats.active && pfire_armed && !pfire_before_release()) {
        InterlockedIncrement64(&pfire_failures);finish(PFIRE_STOP_REASON);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    /* Cutscene ready on the game clock (lockstep_cutscene_ready.h): the active list after the last frame. */
    if(valid && stats.active && cr_armed && !cr_before_release()) {finish(CR_STOP_REASON);return ReleaseSemaphore(semaphore,count,previous);}
    /* Virtual players (observe): the canonical player block after the previous frame, before the pass;
     * the step 3 spike ('construct') builds its second player here and stops the gate on any doubt. */
    if(valid && stats.active && vpl_armed && !vpl_pass(stats.issued)) {finish(VPS_STOP_REASON);return ReleaseSemaphore(semaphore,count,previous);}
    if(valid && stats.active) co_before_release();
    /* Tempzone order: RunPass's slot clear, then the slot parked for the pass. */
    if(valid && stats.active && !tz_before_release()) {
        InterlockedIncrement64(&tz_failures);finish(TZ_STOP_REASON);
        return ReleaseSemaphore(semaphore,count,previous);
    }
    if(valid && (path_options&1)) InterlockedExchange(&path_pass_open,1);
    BOOL result=ReleaseSemaphore(semaphore,count,previous);
    if(!valid||!result) {InterlockedExchange(&path_pass_open,0);finish(22);return result;}
    if(path_options&2) {
        void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
        if(!slots||!slots[0]) {finish(25);return result;}
        /* Test-only, deterministic MoveWorker stream per admitted frame. This
         * does not seed pathing/region/traffic threads or persistent object RNG. */
        uint64_t seed=lockstep_seed(0x4e41544956454741ULL,stats.issued,0,0,0,0);
        *(uint64_t*)((unsigned char*)slots[0]+0x318)=seed;++rng_seeds;
        dw_frame_begin((uint64_t*)((unsigned char*)slots[0]+0x318));
    }
    if(!(path_options&1)) return result;
    HANDLE done=*(HANDLE*)(manager+0x10);
    DWORD waited=WaitForSingleObject(done,5000);
    if(waited!=WAIT_OBJECT_0) {finish(23);return result;}
    LONG prior=-1;
    if(!ReleaseSemaphore(done,1,&prior)||prior!=0) {finish(24);return result;}
    ++path_joins;InterlockedExchange(&path_pass_open,0);
    /* Before anything flushes events: wait for late batch command-list splices. */
    if(InterlockedCompareExchange(&tsnap_active,0,0) && stats.active && !tsnap_after_join(manager)) {
        InterlockedIncrement64(&tsnap_failures);finish(89);
    }
    if(!world_trace_boundary(WT_PATH_AFTER)) {InterlockedIncrement64(&world_trace_early_failures);finish(74);}
    /* Late splices drained: the parked slot back and at most one tempzone grant, before the stamping. */
    if(stats.active && !tz_after_join()) {InterlockedIncrement64(&tz_failures);finish(TZ_STOP_REASON);}
    /* The pass is joined; its batch lists stay unchanged until the next release. */
    if((path_det_mask&PATH_DET_REFRESH_BIT) && stats.active && !path_det_frame(manager)) {
        InterlockedIncrement64(&path_det_failures);finish(79);
    }
    /* After the stamping: its refreshes post transition and waypoint events too. */
    if(stats.active) co_after_join(InterlockedCompareExchange(&tsnap_active,0,0)!=0);
    return result;
}
static int closed_window(void) {
    MSG message;
    return PeekMessageW(&message,NULL,WM_QUIT,WM_QUIT,PM_NOREMOVE)||PeekMessageW(&message,NULL,WM_CLOSE,WM_CLOSE,PM_NOREMOVE);
}
#include "lockstep_gate_timing.h"
#include "lockstep_lookahead.h"
static void gate_tick(void *frame,unsigned char loading) {
    tick_fn original=(tick_fn)(image+TICK_RVA);
    if(!stats.active) {gate_original_once(original,frame,loading);return;}
    LARGE_INTEGER ft_entry,ft_mark;QueryPerformanceCounter(&ft_entry);
    if(radar_installed && radar_failures) {finish(60);gate_original_once(original,frame,loading);return;}
    if(gravidar_installed && gravidar_failures) {finish(62);gate_original_once(original,frame,loading);return;}
    if(gravidar_key_failures || gravidar_time_failures) {finish(83);gate_original_once(original,frame,loading);return;}
    if((interrupt_installed || interrupt_event_installed) && interrupt_failures) {finish(68);gate_original_once(original,frame,loading);return;}
    if(job_rng_installed && job_rng_failures) {finish(81);gate_original_once(original,frame,loading);return;}
    if(script_rng_armed && script_rng_failures) {finish(82);gate_original_once(original,frame,loading);return;}
    if((md_timed_armed && md_timed_failures) || (boot_hold_armed && boot_hold_failures)) {finish(86);gate_original_once(original,frame,loading);return;}
    if(md_resume_armed && md_resume_failures) {finish(87);gate_original_once(original,frame,loading);return;}
    if(find_order_armed && find_order_failures) {finish(92);gate_original_once(original,frame,loading);return;}
    if(region_rng_armed && region_rng_failures) {finish(93);gate_original_once(original,frame,loading);return;}
    if(gdl_armed && gdl_failures) {finish(GDL_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(spawn_rng_armed && spawn_rng_failures) {finish(SPAWN_RNG_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(ai_wait_armed && ai_wait_failures) {finish(88);gate_original_once(original,frame,loading);return;}
    if(speech_time_armed && speech_time_failures) {finish(SPEECH_TIME_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(damage_installed && damage_failures) {finish(66);gate_original_once(original,frame,loading);return;}
    if(contact_installed && contact_failures) {finish(70);gate_original_once(original,frame,loading);return;}
    if(ai_seeded && ai_failures) {finish(30);gate_original_once(original,frame,loading);return;}
    if(agent_seeded && agent_failures) {finish(32);gate_original_once(original,frame,loading);return;}
    if(ship_seeded && ship_failures) {finish(34);gate_original_once(original,frame,loading);return;}
    if(weapon_seeded && weapon_failures) {finish(38);gate_original_once(original,frame,loading);return;}
    if(params_installed && params_trace_failures) {finish(40);gate_original_once(original,frame,loading);return;}
    if(nav_installed && nav_failures) {finish(42);gate_original_once(original,frame,loading);return;}
    if(velocity_installed && velocity_failures) {finish(44);gate_original_once(original,frame,loading);return;}
    if(shape_installed && shape_failures) {finish(46);gate_original_once(original,frame,loading);return;}
    if(bullet_installed && bullet_failures) {finish(48);gate_original_once(original,frame,loading);return;}
    if(death_installed && death_failures) {finish(50);gate_original_once(original,frame,loading);return;}
    if(mass_installed && mass_failures) {finish(52);gate_original_once(original,frame,loading);return;}
    if(attention_installed && attention_failures) {finish(54);gate_original_once(original,frame,loading);return;}
    if(flight_installed && flight_failures) {finish(55);gate_original_once(original,frame,loading);return;}
    if(spline_installed && spline_failures) {finish(58);gate_original_once(original,frame,loading);return;}
    if(path_det_mask && path_det_failures) {finish(79);gate_original_once(original,frame,loading);return;}
    if(travel_installed && travel_failures) {finish(85);gate_original_once(original,frame,loading);return;}
    if(tsnap_installed && tsnap_failures) {finish(89);gate_original_once(original,frame,loading);return;}
    if(pilot_armed && pilot_failures) {
        if(!pslot_stop_deferred()) finish(pilot_stop_reason());
        gate_original_once(original,frame,loading);return;
    }
    if(event_order_armed && event_order_failures) {finish(80);gate_original_once(original,frame,loading);return;}
    if(ai_sched_armed && ai_sched_failures) {finish(84);gate_original_once(original,frame,loading);return;}
    if(render_rng_installed && !render_rng_check()) {finish(90);gate_original_once(original,frame,loading);return;}
    if(bullet_cadence_installed && !bullet_cadence_check()) {finish(94);gate_original_once(original,frame,loading);return;}
    if(speak_timer_installed && !speak_timer_check()) {finish(SPEAK_TIMER_STOP);gate_original_once(original,frame,loading);return;}
    if(nav_cadence_installed && !nav_cadence_check()) {finish(95);gate_original_once(original,frame,loading);return;}
    if(nav_player_installed && !nav_player_check()) {finish(95);gate_original_once(original,frame,loading);return;}
    if(co_armed && !co_check()) {finish(CO_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(ss_installed && !ss_check()) {finish(SS_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if((dw_armed || dw_frame_armed) && !dw_check()) {finish(DW_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(pt_installed && !pt_check()) {finish(PT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(wr_installed && !wr_check()) {finish(WR_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(trr_installed && !trr_check()) {finish(TRR_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(tz_installed && !tz_check()) {finish(TZ_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(gwide_active && !gwide_check()) {finish(GWIDE_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(view_installed && !view_check()) {finish(97);gate_original_once(original,frame,loading);return;}
    if(camoff_applied && !camoff_check()) {finish(98);gate_original_once(original,frame,loading);return;}
    if(camfol_applied && !camfol_check()) {finish(CAMFOL_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(mt_installed && !mt_check()) {finish(MT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(rc_installed && !rc_check()) {finish(rc_stop_reason());gate_original_once(original,frame,loading);return;}
    if(wfire_installed && !wfire_check()) {finish(WFIRE_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(erng_installed && !erng_check()) {finish(ERNG_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(brng_installed && !brng_check()) {finish(BRNG_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(uic_installed && !uic_check()) {finish(UIC_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(spr_installed && !spr_check()) {finish(SPR_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(ul_installed && !ul_check()) {finish(UL_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(pmath_installed && !pmath_check()) {finish(PMATH_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(xrcp_installed && !xrcp_check()) {finish(XRCP_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(fpenv_installed && !fpenv_tick(stats.issued)) {finish(FPENV_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(pdec_installed && !pdec_check()) {finish(PDEC_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(pbud_installed && !pbud_check()) {finish(PBUD_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(sw_installed && !sw_check()) {finish(SW_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(ll_installed && !ll_check()) {finish(LL_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(oc_installed && !oc_check()) {finish(OC_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(po_installed && !po_check()) {finish(PO_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(ma_installed && !ma_check()) {finish(MA_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(eco_installed && !eco_check()) {finish(ECO_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(fr_installed && !fr_check()) {finish(FR_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(event_rng_armed && !event_rng_check(event_order_installed && event_order_armed && !event_order_failures)) {
        finish(EVENT_RNG_STOP_REASON);gate_original_once(original,frame,loading);return;
    }
    if(ptrace_installed && !ptrace_check()) {finish(PTRACE_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(border_installed && !border_check()) {finish(BORDER_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(sorder_installed && !sorder_check()) {finish(SORDER_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(noct_installed && !noct_check()) {finish(NOCT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(!predict_check_live()) {finish(PREDICT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(!hitn_check()) {finish(HITN_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(!pfire_check()) {finish(PFIRE_STOP_REASON);gate_original_once(original,frame,loading);return;}
    DWORD thread=GetCurrentThreadId();if(!main_thread) main_thread=thread;
    void *table=*(void**)(image+TABLE_RVA);
    if(registry_md_installed && (registry_md_failures || registry_origin_failures || creation_failures)) {finish(75);gate_original_once(original,frame,loading);return;}
    if(world_identity_installed && world_identity_failures) {finish(72);gate_original_once(original,frame,loading);return;}
    if(created_armed && created_failures) {finish(CREATED_STOP_REASON);gate_original_once(original,frame,loading);return;}
    if(origin_failed() || getup_failed()) {finish(74);gate_original_once(original,frame,loading);return;}
    if(snapshot_configured && (snapshot_failures || (stats.issued>=4 && snapshot_enrolled!=12))) {finish(74);gate_original_once(original,frame,loading);return;}
    if(boot_mode && boot_phase<3) {
        unsigned boot_timeout=portable_wire_enabled?portable_wire_startup_ms:120000;
        if(thread!=main_thread || GetTickCount64()-boot_started>boot_timeout) {finish(26);gate_original_once(original,frame,loading);return;}
        boot_phase=boot_next(boot_phase,loading || (table && *(int*)((unsigned char*)table+0x130)));
        if(boot_phase<3) {
            /* Zero the idle Gravidar round-robin cursor before the load pass. */
            if(ai_sched_armed && !ai_sched_boot(boot_phase)) {InterlockedIncrement64(&ai_sched_failures);finish(84);}
            gate_original_once(original,frame,loading);return;
        }
        if(ai_sched_armed && !ai_sched_release()) {InterlockedIncrement64(&ai_sched_failures);finish(84);gate_original_once(original,frame,loading);return;}
        world_identity_seal(); /* Imports are over: lookups go lock-free. */
        /* Later creations get created identities; with native_gate_created_load
         * the load-created ones too, here (default off). */
        if(!created_begin()) {finish(CREATED_STOP_REASON);gate_original_once(original,frame,loading);return;}
        if(!rid_seal()) {finish(RID_STOP_REASON);gate_original_once(original,frame,loading);return;}
        if(gravidar_time_sync && !gravidar_boot_drain()) {InterlockedIncrement64(&gravidar_time_failures);finish(83);gate_original_once(original,frame,loading);return;}
        if(ai_sched_armed && !ai_sched_admit()) {InterlockedIncrement64(&ai_sched_failures);finish(84);gate_original_once(original,frame,loading);return;}
        if(boot_hold_armed && !boot_hold_admit()) {InterlockedIncrement64(&boot_hold_failures);finish(86);gate_original_once(original,frame,loading);return;}
        /* Gravidar idle after the boot drain: active units back to their constructor times. */
        if(gdl_armed && !gdl_admit()) {InterlockedIncrement64(&gdl_failures);finish(GDL_STOP_REASON);gate_original_once(original,frame,loading);return;}
    }
    if(thread!=main_thread||loading||!table||*(int*)((unsigned char*)table+0x130)) {finish(2);gate_original_once(original,frame,loading);return;}
    if(!gate_sites_owned()) {
        finish(3);gate_original_once(original,frame,loading);return;
    }
    if(snapshot_configured && !snapshot_bound &&
       !snapshot_bind((snapshot_find_fn)(image+0xced70),*(void**)(image+0x6cf1500))) {
        InterlockedIncrement64(&snapshot_failures);finish(74);gate_original_once(original,frame,loading);return;
    }
    if(!world_trace_bind()) {
        InterlockedIncrement64(&world_trace_early_failures);finish(74);gate_original_once(original,frame,loading);return;
    }
    if(!transition_saved_bind()) {finish(74);gate_original_once(original,frame,loading);return;}
    if(!origin_bind()) {finish(74);gate_original_once(original,frame,loading);return;}
    stats.game=*(double*)(image+GAME_RVA);stats.real=*(double*)(image+REAL_RVA);
    if(stats.issued) {
        double error=fabs((stats.game-previous_game)-gate_tick_dt());
        if(error>stats.max_dt_error) stats.max_dt_error=error;
        if(error>1e-8||(!portable_wire_enabled && stats.report_tick!=stats.issued)) {finish(4);gate_original_once(original,frame,loading);return;}
    }
    stats.completed=stats.issued;
    /* The frame just run is complete and its MoveWorker joined: resolve its creations. */
    QueryPerformanceCounter(&ft_mark);
    if(!created_frame(stats.completed)) {finish(CREATED_STOP_REASON);gate_original_once(original,frame,loading);return;}
    {LARGE_INTEGER now;QueryPerformanceCounter(&now);ft_created=ft_us(ft_mark,now);ft_mark=now;}
    actor_all_frame(stats.completed); /* fold the frame's all-ship actor RNG tallies (default off) */
    /* Mass traffic and region passes (default off): the passes kicked at the end of the frame just run complete here. */
    if(mt_installed && !mt_join(stats.completed,MT_TIMEOUT_MS)) {finish(MT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    /* Object nav octrees: 'sync' builds the frame's requests here, 'deadline' shows those due (native_gate_nav_octree, default off). */
    if(noct_installed && !noct_join(stats.completed,NOCT_JOIN_MS)) {noct_open_builds();finish(NOCT_STOP_REASON);gate_original_once(original,frame,loading);return;}
    /* Runtime IDs (default off): every thread that allocates in the frame has joined. */
    if(!rid_frame(stats.completed)) {finish(RID_STOP_REASON);gate_original_once(original,frame,loading);return;}
    /* The simulation camera's sites, at the first join after its stand-in is set (default off). */
    if(rc_installed && !rc_join_point(stats.completed)) {finish(rc_stop_reason());gate_original_once(original,frame,loading);return;}
    {LARGE_INTEGER now;QueryPerformanceCounter(&now);ft_join=ft_us(ft_mark,now);}
    if(!rotation_native_frame_ready() || !portable_native_capture(stats.completed,frame,loading)) {finish(74);gate_original_once(original,frame,loading);return;}
    if(portable_wire_enabled) {portable_transport_tick(original,frame,loading);return;}
    if(gd_enabled) {gd_tick(original,frame,loading);return;}
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
    ULONGLONG start=GetTickCount64(),last=0;
    LARGE_INTEGER ft_begin,ft_permit,ft_end;QueryPerformanceCounter(&ft_begin);
    if(ft_last_end) {LARGE_INTEGER prior;prior.QuadPart=(LONGLONG)ft_last_end;ft_outside=ft_us(prior,ft_begin);ft_sum_outside+=ft_outside;}
    ft_pre=ft_us(ft_entry,ft_begin);ft_sum_pre+=ft_pre;
    {uint64_t reads=read_local_calls;ft_reads=reads-ft_reads_last;ft_reads_last=reads;}
    if(length<0 || (size_t)length>=sizeof(packet)) finish(56);
    else if(wire_lookahead) {
        /* Protocol 4: the permit for the next tick is often held already. */
        unsigned reason=lookahead_frame(packet,(size_t)length,reply,sizeof(reply));
        if(!reason && portable_native_configured) {
            portable_native_stage="nonzero_permit";InterlockedIncrement64(&portable_native_failures);reason=56;
        }
        if(reason) finish(reason);
        else {previous_game=stats.game;++stats.issued;}
    }
    else for(;;) {
        ULONGLONG now=GetTickCount64();
        double drift=fabs(*(double*)(image+GAME_RVA)-stats.game);
        if(drift>stats.wait_game_delta) stats.wait_game_delta=drift;
        if(drift>0) {finish(5);break;}
        if(closed_window()) {finish(6);break;}
        if(now-start>timeout_ms) {finish(7);break;}
        if(!last||now-last>=100) {send(connection,packet,length,0);last=now;++stats.waits;}
        int count=recv(connection,reply,sizeof(reply)-1,0);
        if(count<0) {int error=WSAGetLastError();if(error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK) {finish(8);break;}continue;}
        reply[count]=0;
        if((size_t)count!=strlen(reply)) continue;
        char permit[128],stop[128];
        snprintf(permit,sizeof(permit),"NGGO|%u|%s|%u|%llu",wire_version,secret,stats.slot,(unsigned long long)(stats.completed+1));
        snprintf(stop,sizeof(stop),"NGSTOP|%u|%s|%u|%llu",wire_version,secret,stats.slot,(unsigned long long)stats.completed);
        if(!strcmp(reply,stop)) {finish(1);break;}
        int accepted=wire_version==3?gate_permit_blobs(reply,(size_t)count,permit,stats.slot,wire_clients,blob_proposal,blob_inputs):
            gate_permit_inputs(reply,permit,stats.slot,proposal,inputs[0],inputs[1]);
        if(accepted<0) {finish(56);break;}
        if(!accepted) continue;
        if(portable_native_configured && (!wire_pair() || strcmp(inputs[0],"00000000") || strcmp(inputs[1],"00000000"))) {
            portable_native_stage="nonzero_permit";InterlockedIncrement64(&portable_native_failures);finish(56);break;
        }
        previous_game=stats.game;++stats.issued;break;
    }
    QueryPerformanceCounter(&ft_permit);
    ft_wait=ft_us(ft_begin,ft_permit);
    if(stats.active) gate_pace_wait(); /* native_gate_tick_hz: real time, after the report left */
    QueryPerformanceCounter(&ft_permit);
    gate_original_once(original,frame,loading);
    QueryPerformanceCounter(&ft_end);
    ft_frame=ft_us(ft_permit,ft_end);ft_sum_wait+=ft_wait;ft_sum_frame+=ft_frame;
    ++ft_frames;ft_last_end=(uint64_t)ft_end.QuadPart;
}
#include "lockstep_thunk_emit.h"
typedef LPVOID (WINAPI *thunk_allocate_fn)(LPVOID,SIZE_T,DWORD,DWORD);
typedef BOOL (WINAPI *thunk_protect_fn)(LPVOID,SIZE_T,DWORD,PDWORD);
typedef BOOL (WINAPI *thunk_flush_fn)(HANDLE,LPCVOID,SIZE_T);
typedef BOOL (WINAPI *thunk_free_fn)(LPVOID,SIZE_T,DWORD);
static unsigned char *make_thunk_using(thunk_allocate_fn allocate,thunk_protect_fn protect,thunk_flush_fn flush,thunk_free_fn release) {
    uintptr_t at=(uintptr_t)(image+CALL_RVA)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536) {
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=allocate((void*)candidate,THUNK_PAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            DWORD old;
            if(!thunk_prepare_page(p) || !protect(p,THUNK_PAGE_SIZE,PAGE_EXECUTE_READ,&old) ||
               !flush(GetCurrentProcess(),p,THUNK_PAGE_SIZE) || !thunk_page_rx(p)) {release(p,0,MEM_RELEASE);return NULL;}
            return p;
        }
    }
    return NULL;
}
static unsigned char *make_thunk(void) {return make_thunk_using(VirtualAlloc,VirtualProtect,FlushInstructionCache,VirtualFree);}
static int enable_path_experiment(unsigned options) {
    if(!stats.active||GetCurrentThreadId()!=ui_thread||!IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128)) return 0;
    if(!hash_ok(image+0xef4609,24,"9521924be3f2996ee22a20824955a1792ccacf464894df9c1f7ce1cf90c56103") ||
       !hash_ok(image+0xeac170,123,"8487d9a8922f92b790f68759933ac3011a10746f828a634ffc5fcb68ab24bd36") ||
       !hash_ok(image+0xeabf00,610,"ab33c0bdd8e30609ba6f21c60741387dcec42558efd38d3e7749185ef2d937ce")) return 0;
    /* The gate thunk's permanent page also holds this indirect-call pointer. */
    if(!thunk_slot_ready(32)) return 0;
    intptr_t distance=(intptr_t)(thunk+32)-(intptr_t)(image+0xef461a);
    if(distance<INT32_MIN||distance>INT32_MAX) return 0;
    memcpy(&path_original,image+0xef4610,16);path_patched=path_original;
    int32_t relative=(int32_t)distance;memcpy((unsigned char*)&path_patched+6,&relative,4);
    if(!swap_path(path_original,path_patched)) return 0;
    path_options=options;path_installed=1;return 1;
}
static int start_gate(unsigned slot,unsigned port,const char *token,unsigned timeout,const char *initial_digest,int boot) {
    if(used||slot<1||slot>wire_clients||(wire_lookahead && wire_version!=3)||port<1024||port>65535||timeout<1000||timeout>120000||!valid_hex(token,32)||!valid_hex(initial_digest,64)) return 10;
    if(!verify()) return 11;
    if(!boot && (*(unsigned short*)(image+0xfa3302)!=0x9066||*(double*)(image+0x6adfc60)!=gate_tick_dt())) return 12;
    void *table=*(void**)(image+TABLE_RVA);if(!table||(!boot && *(int*)((unsigned char*)table+0x130))) return 13;
    if(GetModuleHandleW(L"x4lan_hooks_64.dll")) return 14;
    HMODULE pin;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(void*)start_gate,&pin)) return 15;
    if(boot && !install_dt()) return 27;
    WSADATA w;if(WSAStartup(MAKEWORD(2,2),&w)) {restore_dt();return 16;}
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(connection==INVALID_SOCKET) {WSACleanup();restore_dt();return 17;}
    struct sockaddr_in remote;memset(&remote,0,sizeof(remote));remote.sin_family=AF_INET;remote.sin_port=htons((u_short)port);remote.sin_addr.s_addr=relay_address?relay_address:htonl(INADDR_LOOPBACK);
    DWORD receive_timeout=25;
    if(setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&receive_timeout,sizeof(receive_timeout))) {finish(77);return 77;}
    if(connect(connection,(struct sockaddr*)&remote,sizeof(remote))) {finish(18);return 18;}
    thunk=make_thunk();if(!thunk) {finish(19);return 19;}
    memcpy(&original_word,image+WORD_RVA,8);patched_word=original_word;
    int32_t rel=(int32_t)(thunk-(image+CALL_RVA+5));memcpy((unsigned char*)&patched_word+3,&rel,4);
    memset(&stats,0,sizeof(stats));stats.slot=slot;strcpy(secret,token);strcpy(digest,initial_digest);lookahead_reset();
    strcpy(initial_state_digest,initial_digest);remote_port=port;
    ui_thread=GetCurrentThreadId();timeout_ms=timeout;stats.active=1;used=1;
    boot_mode=boot;boot_started=GetTickCount64();
    if(!swap_word(original_word,patched_word)) {finish(20);return 20;}
    installed=1;return 0;
}
#include "lockstep_gate_exports.h"

#include "lockstep_gate_lua_abi.h"
static int lua_start(lua_State *L) {
    int result=x4gate_start((unsigned)p_lua_tonumber(L,1),(unsigned)p_lua_tonumber(L,2),
        p_lua_tolstring(L,3,NULL),(unsigned)p_lua_tonumber(L,4),p_lua_tolstring(L,5,NULL));
    double options=p_lua_tonumber(L,6);
    if(!result && options!=0 && (options<1||options>3||floor(options)!=options||!enable_path_experiment((unsigned)options))) {finish(21);result=21;}
    p_lua_pushnumber(L,result);return 1;
}
#include "lockstep_gate_lua_wire.h"
static int boot_option_mask_valid(double options) {
    return isfinite(options) && options>=0 && options<=9007199254740991.0 && floor(options)==options &&
        !((uint64_t)options & ~(1125899906842623ULL|SOLVER_OPTION|REGISTRY_MD_BIT|REQUEST_OPTION));
}
static int lua_boot(lua_State *L) {
    unsigned slot=(unsigned)p_lua_tonumber(L,1),port=(unsigned)p_lua_tonumber(L,2);
    const char *token=p_lua_tolstring(L,3,NULL),*hash=p_lua_tolstring(L,5,NULL);
    double options=p_lua_tonumber(L,6);
    /* UI reloads are allowed only for this same process-owned session. */
    int result=10;
    if(!boot_option_mask_valid(options) ||
       (((uint64_t)options&REQUEST_OPTION) && (!((uint64_t)options&SOLVER_OPTION) || !((uint64_t)options&896))) ||
       (((uint64_t)options&SOLVER_OPTION) && (!((uint64_t)options&70368744177664ULL) || !((uint64_t)options&28) || !((uint64_t)options&1))) ||
       (((uint64_t)options&562949953421312ULL) && !((uint64_t)options&281474976710656ULL)) ||
       (((uint64_t)options&140737488355328ULL) && !((uint64_t)options&281474976710656ULL)) ||
       (((uint64_t)options&281474976710656ULL) && (((uint64_t)options&72567767433216ULL)!=72567767433216ULL)) ||
       (((uint64_t)options&REGISTRY_MD_BIT) && (!((uint64_t)options&70368744177664ULL) || !((uint64_t)options&96) || !((uint64_t)options&1) || !((uint64_t)options&2199023255552ULL))) ||
       (((uint64_t)options&35184372088832ULL) && !((uint64_t)options&17592186044416ULL)) ||
       (((uint64_t)options&17592186044416ULL) && (((uint64_t)options&6291456)!=6291456)) ||
       (((uint64_t)options&4398046511104ULL) && !((uint64_t)options&2199023255552ULL)) ||
       (((uint64_t)options&549755813888ULL) && !((uint64_t)options&274877906944ULL)) ||
       (((uint64_t)options&274877906944ULL) && !((uint64_t)options&137438953472ULL)) ||
       (((uint64_t)options&137438953472ULL) && (!((uint64_t)options&68719476736ULL) || !((uint64_t)options&1))) ||
       (((uint64_t)options&68719476736ULL) && !((uint64_t)options&96)) ||
       (((uint64_t)options&34359738368ULL) && (!((uint64_t)options&8589934592ULL) || !((uint64_t)options&512) || !((uint64_t)options&1))) ||
       (((uint64_t)options&17179869184ULL) && (!((uint64_t)options&8589934592ULL) || !((uint64_t)options&512))) ||
       (((uint64_t)options&0x80000000ULL) && !((uint64_t)options&16)) ||
       (((uint64_t)options&0x100000000ULL) && !((uint64_t)options&8))) {p_lua_pushnumber(L,result);return 1;}
    if(boot_mode && used && stats.active && slot==stats.slot && port==remote_port && (uint64_t)options==boot_options &&
       valid_hex(token,32) && !strcmp(secret,token) && valid_hex(hash,64) && !strcmp(hash,initial_state_digest)) result=0;
    else if(!used) {
        result=start_gate(slot,port,token,(unsigned)p_lua_tonumber(L,4),hash,1);
        if(!result) {
            boot_options=(uint64_t)options;
            if((boot_options&70368744177664ULL) && !world_identity_install()) {finish(71);result=71;}
            if(!result && (boot_options&3) && !enable_path_experiment(boot_options&3)) {finish(21);result=21;}
            if(!result && (boot_options&28) && !flight_install((boot_options&8)!=0,(boot_options&16)!=0,(boot_options&0x80000000ULL)!=0,(boot_options&0x100000000ULL)!=0)) {finish(28);result=28;}
            if(!result && (boot_options&96) && !ai_install((boot_options&64)!=0)) {finish(29);result=29;}
            if(!result && (boot_options&REGISTRY_MD_BIT) && !registry_md_install()) {finish(75);result=75;}
            if(!result && (boot_options&2199023255552ULL) && !interrupt_install((boot_options&4398046511104ULL)!=0)) {finish(67);result=67;}
            if(!result && (boot_options&REGISTRY_MD_BIT) && !registry_origin_enable()) {finish(75);result=75;}
            if(!result && (boot_options&68719476736ULL) && !radar_install()) {finish(59);result=59;}
            if(!result && (boot_options&137438953472ULL) && !gravidar_install((boot_options&274877906944ULL)!=0,(boot_options&549755813888ULL)!=0)) {finish(61);result=61;}
            if(!result && (boot_options&1099511627776ULL) && !damage_install()) {finish(65);result=65;}
            if(!result && (boot_options&8796093022208ULL) && !contact_install()) {finish(69);result=69;}
            if(!result && (boot_options&896) && !agent_install((boot_options&256)!=0,(boot_options&512)!=0)) {finish(31);result=31;}
            if(!result && (boot_options&8589934592ULL) && !spline_install((boot_options&17179869184ULL)!=0,(boot_options&34359738368ULL)!=0)) {finish(57);result=57;}
            if(!result && (boot_options&3072) && !ship_install((boot_options&2048)!=0)) {finish(33);result=33;}
            if(!result && (boot_options&24576) && !weapon_install((boot_options&16384)!=0)) {finish(37);result=37;}
            if(!result && (boot_options&4096)) {
                if(!hash_ok(image+0xea1e80,0xb9f,"2db0c3a56629a30189d8112ee5e9dec726ddabec4805380cdb7e8a6ec4c2deff")) {finish(35);result=35;}
                else params_enabled=1;
            }
            if(!result && (boot_options&32768) && !params_install()) {finish(39);result=39;}
            if(!result && (boot_options&458752) && !nav_install((boot_options&131072)!=0,(boot_options&262144)!=0)) {finish(41);result=41;}
            if(!result && (boot_options&524288) && !velocity_install()) {finish(43);result=43;}
            if(!result && (boot_options&7340032) && !shape_install((boot_options&2097152)!=0,(boot_options&4194304)!=0,(boot_options&17592186044416ULL)!=0,(boot_options&35184372088832ULL)!=0)) {finish(45);result=45;}
            if(!result && (boot_options&25165824) && !bullet_install((boot_options&16777216)!=0)) {finish(47);result=47;}
            if(!result && (boot_options&100663296) && !death_install((boot_options&67108864)!=0)) {finish(49);result=49;}
            if(!result && (boot_options&134217728) && !mass_install()) {finish(51);result=51;}
            if(!result && (boot_options&1879048192) && !attention_install((boot_options&536870912)!=0,(boot_options&1073741824)!=0)) {finish(53);result=53;}
        }
    }
    p_lua_pushnumber(L,result);return 1;
}
static int lua_load_requested(lua_State *L) {
    int ok=boot_mode && stats.active && boot_phase==0 && GetCurrentThreadId()==ui_thread;
    if(ok) ok=getup_extensions_load_ready() && stats.active && boot_phase==0;
    if(ok) boot_phase=1;
    p_lua_pushnumber(L,ok);return 1;
}
#include "lockstep_thread_rng.h"
static int lua_thread_rng(lua_State *L) {
    static char out[24576];int n=accept_ui()?thread_rng_census(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_gravidar_key(lua_State *L) {p_lua_pushnumber(L,gravidar_key_arm());return 1;}
static int lua_gravidar_time(lua_State *L) {p_lua_pushnumber(L,gravidar_time_arm());return 1;}
static int lua_gravidar_key_status(lua_State *L) {
    char out[384];int n=accept_ui()?gravidar_key_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Read-only Gravidar has_scanned contact changes (native_gate_gravidar_posts), rides the unit keying. */
static int lua_gravidar_posts(lua_State *L) {p_lua_pushnumber(L,gpost_arm(InterlockedCompareExchange(&gravidar_key_all,0,0)!=0));return 1;}
static int lua_gravidar_posts_status(lua_State *L) {
    char out[448];int n=accept_ui()?gpost_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* native_gate_gravidar_wide: 'wide' (wide-range units applied after the sector's last pair check) or 'sort' (zone groups in portable order). */
static int lua_gravidar_wide(lua_State *L) {
    size_t size=0;const char *mode=p_lua_tolstring(L,1,&size);
    int value=!mode || (size==4 && !memcmp(mode,"wide",4))?GWIDE_MODE_WIDE:size==4 && !memcmp(mode,"sort",4)?GWIDE_MODE_SORT:GWIDE_MODE_OFF;
    p_lua_pushnumber(L,value!=GWIDE_MODE_OFF && gwide_arm(value,gravidar_installed));return 1;
}
static int lua_gravidar_wide_status(lua_State *L) {
    char out[640];int n=accept_ui()?gwide_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_gravidar_post_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);static char out[4608];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=gpost_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Read-only Gravidar contact watch (native_gate_gravidar_watch), rides the unit keying. */
static int lua_gravidar_watch(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);double slot=p_lua_tonumber(L,2);
    int ok=accept_ui() && gravidar_key_all && isfinite(slot) && slot>=1 && slot<=GRAVIDAR_WATCH_MAX && floor(slot)==slot &&
        gravidar_watch_add(text,size,(unsigned)slot);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_index_arg(lua_State *L,int at,uint64_t *index) {
    double value=p_lua_tonumber(L,at);
    if(!isfinite(value) || value<0 || value>=9007199254740992.0 || floor(value)!=value) return 0;
    *index=(uint64_t)value;return 1;
}
static int lua_gravidar_watch_row(lua_State *L) {
    uint64_t index;char out[256];int n=accept_ui() && lua_index_arg(L,1,&index)?gravidar_watch_row_text(index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_gravidar_watch_set(lua_State *L) {
    static char out[6144];uint64_t index,set;
    int n=accept_ui() && lua_index_arg(L,1,&index) && lua_index_arg(L,2,&set) && set<=7?gravidar_watch_set_text(index,(unsigned)set,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_gravidar_watch_status(lua_State *L) {
    char out[256];int n=accept_ui()?gravidar_watch_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
#include "lockstep_gate_lua_misc.h"
static int lua_timer_resolution(lua_State *L) {
    double ms=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ms) && ms>=1 && ms<=15 && floor(ms)==ms && timer_resolution_arm((unsigned)ms));return 1;
}
static int lua_timer_resolution_status(lua_State *L) {
    char out[64];int n=accept_ui()?timer_resolution_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_boot_rng_anchor(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t seed=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        char c=text[i];unsigned v=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16u;
        if(v>15) ok=0;else seed=(seed<<4)|v;
    }
    double mask=p_lua_tonumber(L,2);if(!isfinite(mask) || mask<1 || mask>7 || floor(mask)!=mask) ok=0;
    p_lua_pushnumber(L,ok && !script_rng_phase2_armed() && boot_rng_anchor_arm(seed,(unsigned)mask));return 1;
}
static int lua_boot_rng_anchor_status(lua_State *L) {
    char out[384];int n=accept_ui()?boot_rng_anchor_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_path_determinism(lua_State *L) {
    double mask=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mask) && mask>=1 && mask<=3 && floor(mask)==mask && path_det_arm((unsigned)mask));return 1;
}
static int lua_ai_schedule(lua_State *L) {
    double mask=p_lua_tonumber(L,1);if(mask==0) mask=AI_SCHED_GRAVIDAR; /* no argument: cursor checks only */
    p_lua_pushnumber(L,isfinite(mask) && mask>=1 && mask<=7 && floor(mask)==mask && ai_sched_arm((unsigned)mask));return 1;
}
static int lua_ai_schedule_status(lua_State *L) {
    char out[1024];int n=accept_ui()?ai_sched_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ai_schedule_fallbacks(lua_State *L) {
    char out[1200];int n=accept_ui()?ai_sched_fallbacks(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_path_determinism_status(lua_State *L) {
    char out[640];int n=accept_ui()?path_det_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_travel_determinism(lua_State *L) {p_lua_pushnumber(L,travel_arm());return 1;}
static int lua_render_rng_split(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"split",5)?RENDER_RNG_SPLIT:
          mode && length==7 && !memcmp(mode,"observe",7)?RENDER_RNG_OBSERVE:RENDER_RNG_OFF;
    p_lua_pushnumber(L,m!=RENDER_RNG_OFF && render_rng_arm(m));return 1;
}
static int lua_render_rng_split_status(lua_State *L) {
    char out[640];int n=accept_ui()?render_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_bullet_cadence(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"every",5)?BULLET_CADENCE_EVERY:
          mode && length==7 && !memcmp(mode,"observe",7)?BULLET_CADENCE_OBSERVE:BULLET_CADENCE_OFF;
    p_lua_pushnumber(L,m!=BULLET_CADENCE_OFF && bullet_cadence_arm(m));return 1;
}
static int lua_bullet_cadence_status(lua_State *L) {
    char out[768];int n=accept_ui()?bullet_cadence_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_speak_timer(lua_State *L) {p_lua_pushnumber(L,speak_timer_arm());return 1;}
static int lua_speak_timer_status(lua_State *L) {
    char out[256];int n=accept_ui()?speak_timer_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_cadence(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==3 && !memcmp(mode,"far",3)?NAV_CADENCE_FAR:
          mode && length==5 && !memcmp(mode,"every",5)?NAV_CADENCE_EVERY:
          mode && length==7 && !memcmp(mode,"observe",7)?NAV_CADENCE_OBSERVE:NAV_CADENCE_OFF;
    p_lua_pushnumber(L,m!=NAV_CADENCE_OFF && nav_cadence_arm(m));return 1;
}
static int lua_nav_cadence_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=nav_cadence_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_cadence_watch(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t saved=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        char ch=text[i];unsigned v=ch>='0'&&ch<='9'?(unsigned)(ch-'0'):ch>='a'&&ch<='f'?(unsigned)(ch-'a'+10):16u;
        if(v>15) ok=0;else saved=(saved<<4)|v;
    }
    p_lua_pushnumber(L,ok && nav_cadence_watch_add(saved));return 1;
}
static int lua_nav_probe_init(lua_State *L) {p_lua_pushnumber(L,nav_probe_arm());return 1;}
static int lua_draw_watch_add(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t saved=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        char ch=text[i];unsigned v=ch>='0'&&ch<='9'?(unsigned)(ch-'0'):ch>='a'&&ch<='f'?(unsigned)(ch-'a'+10):16u;
        if(v>15) ok=0;else saved=(saved<<4)|v;
    }
    p_lua_pushnumber(L,ok && dw_add(saved));return 1;
}
static int lua_draw_watch(lua_State *L) {p_lua_pushnumber(L,dw_arm());return 1;}
static int lua_draw_watch_frame(lua_State *L) {p_lua_pushnumber(L,dw_frame_arm());return 1;}
static int lua_draw_watch_frame_end(lua_State *L) {(void)L;if(accept_ui()) dw_frame_end();return 0;}
static int lua_draw_watch_status(lua_State *L) {
    char out[600];int n=accept_ui()?dw_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_draw_watch_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=dw_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_draw_watch_stray(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=dw_stray_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t saved=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        char ch=text[i];unsigned v=ch>='0'&&ch<='9'?(unsigned)(ch-'0'):ch>='a'&&ch<='f'?(unsigned)(ch-'a'+10):16u;
        if(v>15) ok=0;else saved=(saved<<4)|v;
    }
    p_lua_pushnumber(L,ok && motion_watch_add(saved));return 1;
}
static int lua_motion_watch_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && motion_watch_frame((uint64_t)tick));return 1;
}
static int lua_motion_watch_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[3072];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=motion_watch_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch_orders(lua_State *L) {p_lua_pushnumber(L,motion_order_arm());return 1;}
static int lua_motion_watch_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=motion_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch_target_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=motion_target_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch_event_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=motion_event_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch_refresh_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=motion_refresh_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_motion_watch_status(lua_State *L) {
    char out[256];int n=accept_ui()?motion_watch_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_params_init(lua_State *L) {p_lua_pushnumber(L,nav_params_arm());return 1;}
static int lua_nav_player(lua_State *L) {p_lua_pushnumber(L,nav_player_arm());return 1;}
static int lua_ship_sound_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"split",5)?SS_SPLIT:mode && length==7 && !memcmp(mode,"observe",7)?SS_OBSERVE:SS_OFF;
    p_lua_pushnumber(L,m!=SS_OFF && ss_arm(m));return 1;
}
static int lua_player_timer(lua_State *L) {p_lua_pushnumber(L,pt_arm());return 1;}
static int lua_warp_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"split",5)?WR_SPLIT:mode && length==7 && !memcmp(mode,"observe",7)?WR_OBSERVE:WR_OFF;
    p_lua_pushnumber(L,m!=WR_OFF && wr_arm(m));return 1;
}
static int lua_warp_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?wr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_transition_rows(lua_State *L) {p_lua_pushnumber(L,trr_arm());return 1;}
static int lua_transition_rows_status(lua_State *L) {
    char out[256];int n=accept_ui()?trr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_transition_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=trr_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_tempzone_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"order",5)?TZ_ORDER:mode && length==7 && !memcmp(mode,"observe",7)?TZ_OBSERVE:TZ_OFF;
    p_lua_pushnumber(L,m!=TZ_OFF && tz_arm(m,path_options));return 1;
}
static int lua_tempzone_order_status(lua_State *L) {
    char out[896];int n=accept_ui()?tz_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_tempzone_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=tz_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_warp_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=wr_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_player_timer_status(lua_State *L) {
    char out[512];int n=accept_ui()?pt_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ship_sound_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?ss_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_command_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==7 && !memcmp(mode,"observe",7)?CO_OBSERVE:mode && length==4 && !memcmp(mode,"sort",4)?CO_SORT:CO_OFF;
    p_lua_pushnumber(L,m!=CO_OFF && co_arm(m,path_options));return 1;
}
static int lua_command_order_rows(lua_State *L) {p_lua_pushnumber(L,co_trace_arm());return 1;}
static int lua_command_order_status(lua_State *L) {
    char out[1024];int n=accept_ui()?co_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_command_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=co_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_player_status(lua_State *L) {
    char out[512];int n=accept_ui()?nav_player_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_attention_all(lua_State *L) {p_lua_pushnumber(L,attention_all_arm());return 1;}
static int lua_attention_all_status(lua_State *L) {
    char out[512];int n=accept_ui()?attention_all_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_cadence_status(lua_State *L) {
    char out[1536];int n=accept_ui()?nav_cadence_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_view_points(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==7 && !memcmp(mode,"players",7)?VIEW_PLAYERS:
          mode && length==7 && !memcmp(mode,"observe",7)?VIEW_OBSERVE:VIEW_OFF;
    p_lua_pushnumber(L,m!=VIEW_OFF && view_arm(m));return 1;
}
static int lua_view_points_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && view_frame((uint64_t)tick));return 1;
}
static int lua_view_points_rows(lua_State *L) {p_lua_pushnumber(L,view_rows_arm());return 1;}
static int lua_view_points_triggers(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"block",5)?VIEW_TRIGGER_BLOCK:
          mode && length==7 && !memcmp(mode,"observe",7)?VIEW_TRIGGER_OBSERVE:VIEW_TRIGGER_OFF;
    p_lua_pushnumber(L,m!=VIEW_TRIGGER_OFF && view_triggers_arm(m));return 1;
}
static int lua_view_points_source(lua_State *L) {
    size_t length=0;const char *name=p_lua_tolstring(L,1,&length);char text[8]={0};
    int ok=name && length>0 && length<sizeof(text);if(ok) memcpy(text,name,length);
    p_lua_pushnumber(L,ok && view_source_arm(text));return 1;
}
static int lua_view_points_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=view_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_view_points_status(lua_State *L) {
    char out[2048];int n=accept_ui()?view_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.fast_reads(): native_gate_fast_reads, see read_local. */
static int lua_fast_reads(lua_State *L) {(void)L;read_local_fast=1;p_lua_pushnumber(L,1);return 1;}
/* gate.path_budget(N): native_gate_path_budget, lockstep_path_budget.h. */
static int lua_path_budget(lua_State *L) {
    double n=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(n) && n>=1 && n<=PBUD_MAX && floor(n)==n && pbud_arm((unsigned)n));return 1;
}
static int lua_path_budget_status(lua_State *L) {
    char out[160];int n=accept_ui()?pbud_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gate.pass_stride(N): native_gate_pass_stride, lockstep_mass_traffic.h. */
static int lua_pass_stride(lua_State *L) {
    double n=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(n) && n>=1 && n<=MT_STRIDE_MAX && floor(n)==n && mt_stride_arm((unsigned)n));return 1;
}
static int lua_mass_traffic(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==4 && !memcmp(mode,"sync",4)?MT_SYNC:
          mode && length==7 && !memcmp(mode,"observe",7)?MT_OBSERVE:MT_OFF;
    p_lua_pushnumber(L,m!=MT_OFF && mt_arm(m));return 1;
}
static int lua_region_camera(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int group=mode && length==4 && !memcmp(mode,"view",4)?RC_REGION:mode && length==6 && !memcmp(mode,"fields",6)?RC_FIELDS:
              mode && length==7 && !memcmp(mode,"traffic",7)?RC_TRAFFIC:mode && length==7 && !memcmp(mode,"effects",7)?RC_EFFECTS:
              mode && length==11 && !memcmp(mode,"effect_rows",11)?RC_EFFECT_ROWS:
              mode && length==7 && !memcmp(mode,"highway",7)?RC_HIGHWAY:
              mode && length==6 && !memcmp(mode,"scenes",6)?RC_SCENES:
              mode && length==7 && !memcmp(mode,"context",7)?RC_CONTEXT:
              mode && length==6 && !memcmp(mode,"policy",6)?RC_POLICY:
              mode && length==13 && !memcmp(mode,"shape_refresh",13)?RC_SHAPE_REFRESH:
              mode && length==6 && !memcmp(mode,"mining",6)?RC_MINING:
              mode && length==9 && !memcmp(mode,"xs_detail",9)?RC_XS:
              mode && length==12 && !memcmp(mode,"camera_probe",12)?RC_PROBE:
              mode && length==12 && !memcmp(mode,"highway_rows",12)?RC_HIGHWAY_ROWS:
              mode && length==13 && !memcmp(mode,"highway_speed",13)?RC_HIGHWAY_SPEED:
              mode && length==9 && !memcmp(mode,"sound_rng",9)?RC_SOUND:
              mode && length==11 && !memcmp(mode,"view_scenes",11)?RC_VIEW_SCENES:
              mode && length==10 && !memcmp(mode,"field_fill",10)?RC_FIELD_FILL:
              mode && length==16 && !memcmp(mode,"field_fill_block",16)?RC_FIELD_FILL:
              mode && length==13 && !memcmp(mode,"traffic_views",13)?RC_TRAFFIC_VIEWS:
              mode && length==12 && !memcmp(mode,"traffic_rows",12)?RC_TRAFFIC_ROWS:
              mode && length==20 && !memcmp(mode,"traffic_pass_observe",20)?RC_TRAFFIC_PASS:
              mode && length==17 && !memcmp(mode,"traffic_pass_view",17)?RC_TRAFFIC_PASS:-1;
    const int pass=group==RC_TRAFFIC_PASS?(length==17?TP_VIEW:TP_OBSERVE):TP_OFF;
    /* 'view' writes from the pass alone: the render pass's views must not be player views (traffic_views first). */
    if((pass==TP_VIEW && !(rc_armed_groups&(1u<<RC_TRAFFIC_VIEWS))) || (pass && tp_mode && tp_mode!=pass)) {p_lua_pushnumber(L,0);return 1;}
    const int armed=group>=0 && rc_arm(group);
    if(armed && group==RC_FIELD_FILL) rc_fill_view=length==10;
    if(armed && pass) tp_mode=pass;
    p_lua_pushnumber(L,armed);return 1;
}
static int lua_region_camera_standins(lua_State *L) {
    double n=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(n) && n>=2 && n<=VIEW_MAX && floor(n)==n && rc_standins_arm((unsigned)n));return 1;
}
static int lua_region_camera_uncovered(lua_State *L) {p_lua_pushnumber(L,accept_ui()?rc_uncovered_now:0);return 1;}
static int lua_region_camera_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && rc_frame((uint64_t)tick));return 1;
}
static int lua_weapon_fire_rng(lua_State *L) {p_lua_pushnumber(L,wfire_arm());return 1;}
static int lua_weapon_fire_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?wfire_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_engine_refresh_rng(lua_State *L) {p_lua_pushnumber(L,erng_arm());return 1;}
/* portable_math(): the CRT math entries and sin/cos helpers to src/native/portable_math (lockstep_portable_math.h). */
static int lua_portable_math(lua_State *L) {p_lua_pushnumber(L,pmath_arm());return 1;}
static int lua_portable_math_status(lua_State *L) {
    char out[320];int n=accept_ui()?pmath_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* exact_reciprocal(): RCPSS/RSQRTSS to exactly rounded division and square root (lockstep_exact_reciprocal.h). */
static int lua_exact_reciprocal(lua_State *L) {p_lua_pushnumber(L,xrcp_arm());return 1;}
static int lua_exact_reciprocal_status(lua_State *L) {
    char out[320];int n=accept_ui()?xrcp_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* fp_env(mode): 'default' (MXCSR 0x1f80 on every X4 thread) or 'observe' (lockstep_fp_env.h). */
static int lua_fp_env(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==7 && !memcmp(mode,"default",7)?FPENV_DEFAULT:mode && length==7 && !memcmp(mode,"observe",7)?FPENV_OBSERVE:FPENV_OFF;
    p_lua_pushnumber(L,m!=FPENV_OFF && fpenv_arm(m));return 1;
}
static int lua_fp_env_status(lua_State *L) {
    char out[512];int n=accept_ui()?fpenv_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* decimal(): the saved-value readers to src/native/portable_math/portable_decimal.c (lockstep_decimal.h). */
static int lua_decimal(lua_State *L) {p_lua_pushnumber(L,pdec_arm());return 1;}
static int lua_decimal_status(lua_State *L) {
    char out[256];int n=accept_ui()?pdec_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_build_rng(lua_State *L) {p_lua_pushnumber(L,brng_arm());return 1;}
/* ui_cargo(mode): 'block' or 'observe'. */
static int lua_ui_cargo(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"block",5)?UIC_BLOCK:mode && length==7 && !memcmp(mode,"observe",7)?UIC_OBSERVE:-1;
    p_lua_pushnumber(L,m>0 && uic_arm(m));return 1;
}
static int lua_supply_rows(lua_State *L) {p_lua_pushnumber(L,spr_arm());return 1;}
static int lua_supply_rows_status(lua_State *L) {
    char out[256];int n=accept_ui()?spr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_supply_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[320];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=spr_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* ui_lazy(mode): 'observe' or 'block'. */
static int lua_ui_lazy(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"block",5)?UL_BLOCK:mode && length==7 && !memcmp(mode,"observe",7)?UL_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        ul_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && ul_arm(m));return 1;
}
/* supply_wants(from, to): the row window in ticks. */
static int lua_supply_wants(lua_State *L) {
    double from=p_lua_tonumber(L,1),to=p_lua_tonumber(L,2);
    int ok=isfinite(from) && isfinite(to) && from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to;
    p_lua_pushnumber(L,ok && sw_arm((uint64_t)from,(uint64_t)to));return 1;
}
static int lua_supply_wants_status(lua_State *L) {
    char out[256];int n=accept_ui()?sw_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_supply_wants_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=sw_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* loadout_order(mode, from, to): 'observe' or 'canonical'; the row window in ticks is optional. */
static int lua_loadout_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==9 && !memcmp(mode,"canonical",9)?LL_CANONICAL:mode && length==7 && !memcmp(mode,"observe",7)?LL_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        ll_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && ll_arm(m));return 1;
}
static int lua_loadout_order_status(lua_State *L) {
    char out[384];int n=accept_ui()?ll_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_loadout_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=ll_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
#include "lockstep_fp_redirect_lua.h"
/* step_trace(id): a saved ID (16 hex digits) to trace; step_trace_arm(ticks); status; rows. */
static int lua_step_trace(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);uint64_t id=0;int ok=text && length==16;
    for(size_t i=0;ok && i<16;i++) {
        const char c=text[i];ok=(c>='0' && c<='9') || (c>='a' && c<='f');
        id=id*16+(uint64_t)(c<='9'?c-'0':c-'a'+10);
    }
    p_lua_pushnumber(L,ok && st_watch(id));return 1;
}
static int lua_step_trace_arm(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=600 && floor(ticks)==ticks && st_arm((uint64_t)ticks));return 1;
}
static int lua_step_trace_status(lua_State *L) {
    char out[768];int n=accept_ui()?st_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* step_chain(): FR8's hooks with the step trace (native_gate_step_chain), before step_trace_arm. */
static int lua_step_chain(lua_State *L) {p_lua_pushnumber(L,st_chain_want());return 1;}
static int lua_step_trace_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=st_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_coverage(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);int ok=isfinite(ticks) && ticks>=1 && ticks<=6000 && floor(ticks)==ticks;
    if(ok && !cov_armed && !cov_count && !cov_load()) {cov_stage="list";ok=0;}
    p_lua_pushnumber(L,ok && cov_arm((unsigned)ticks));return 1;
}
static int lua_coverage_frame(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    if(accept_ui() && isfinite(tick) && tick>=1 && floor(tick)==tick) cov_frame((uint64_t)tick);
    p_lua_pushnumber(L,1);return 1;
}
static int lua_coverage_status(lua_State *L) {
    char out[384];int n=accept_ui()?cov_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_coverage_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[128];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && floor(index)==index) n=cov_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* macro_arena(): macros in id hash order. */
static int lua_macro_arena(lua_State *L) {
    p_lua_pushnumber(L,ma_arm());return 1;
}
static int lua_macro_arena_status(lua_State *L) {
    char out[512];int n=accept_ui()?ma_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* The drain is also where the database is audited: on the thread that creates macros. */
static int lua_macro_arena_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui()) ma_on_drain();
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=ma_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* pick_order(mode, from, to): 'observe' or 'canonical'; the row window in ticks is optional. */
static int lua_pick_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==9 && !memcmp(mode,"canonical",9)?PO_CANONICAL:mode && length==7 && !memcmp(mode,"observe",7)?PO_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        po_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && po_arm(m));return 1;
}
static int lua_pick_order_status(lua_State *L) {
    char out[384];int n=accept_ui()?po_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_pick_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=po_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* order_canon(mode, from, to): 'observe' or 'canonical'; the row window in ticks is optional. */
static int lua_order_canon(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==9 && !memcmp(mode,"canonical",9)?OC_CANONICAL:mode && length==7 && !memcmp(mode,"observe",7)?OC_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        oc_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && oc_arm(m));return 1;
}
static int lua_order_canon_status(lua_State *L) {
    char out[384];int n=accept_ui()?oc_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_order_canon_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=oc_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* address_order(tick): the macro and ware orders now and at that tick. */
static int lua_address_order(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    int ok=isfinite(tick) && tick>=1 && tick<=18000 && floor(tick)==tick;
    p_lua_pushnumber(L,ok && ao_arm((uint64_t)tick));return 1;
}
static int lua_address_order_status(lua_State *L) {
    char out[256];int n=accept_ui()?ao_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_address_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui()) ao_on_tick();
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=ao_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_lazy_status(lua_State *L) {
    char out[384];int n=accept_ui()?ul_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_lazy_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[288];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=ul_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_cargo_status(lua_State *L) {
    char out[512];int n=accept_ui()?uic_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ui_cargo_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=uic_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_build_rng_status(lua_State *L) {
    char out[256];int n=accept_ui()?brng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_engine_refresh_rng_status(lua_State *L) {
    char out[256];int n=accept_ui()?erng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* physics_trace(rows): rows = the last tick with body/contact rows (0: step rows only). */
static int lua_physics_trace(lua_State *L) {
    double rows=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(rows) && rows>=0 && rows<=PTRACE_ROW_TICKS_MAX && floor(rows)==rows && ptrace_arm((unsigned)rows));return 1;
}
/* physics_trace_children(ticks[, from]): PHYSICS_CHILD rows for ticks from..ticks (default from 1; after physics_trace). */
static int lua_physics_trace_children(lua_State *L) {
    double ticks=p_lua_tonumber(L,1),from=p_lua_tonumber(L,2);if(from==0) from=1; /* absent: 1 */
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=PTRACE_CHILD_TICKS_MAX && floor(ticks)==ticks && isfinite(from) && from>=1 &&
                       from<=ticks && floor(from)==from && ptrace_children_arm((unsigned)ticks,(unsigned)from));return 1;
}
/* physics_trace_saved(): PHYSICS_BODY rows for saved bodies too (after physics_trace with rows >= 1). */
static int lua_physics_trace_saved(lua_State *L) {p_lua_pushnumber(L,ptrace_saved_arm());return 1;}
static int lua_physics_trace_status(lua_State *L) {
    char out[512];int n=accept_ui()?ptrace_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* physics_trace_row(kind, index, final): kind 0 step, 1 body, 2 contact, 3 child; body,
 * contact and child rows of the current tick only with final (at stop). */
static int lua_physics_trace_row(lua_State *L) {
    double kind=p_lua_tonumber(L,1),index=p_lua_tonumber(L,2),final=p_lua_tonumber(L,3);char out[1024];int n=0;
    if(accept_ui() && isfinite(kind) && kind>=0 && kind<=3 && floor(kind)==kind && isfinite(index) && index>=0 &&
       index<9007199254740992.0 && floor(index)==index) n=ptrace_row((unsigned)kind,(uint64_t)index,final==1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_body_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==4 && !memcmp(mode,"sort",4)?BORDER_SORT:mode && length==7 && !memcmp(mode,"observe",7)?BORDER_OBSERVE:BORDER_OFF;
    p_lua_pushnumber(L,m!=BORDER_OFF && border_arm(m));return 1;
}
static int lua_body_order_status(lua_State *L) {
    char out[512];int n=accept_ui()?border_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* shape_order(mode): 'place' (true), 'wait' or 'sync'. */
static int lua_shape_order(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==5 && !memcmp(mode,"place",5)?SORDER_PLACE:mode && length==4 && !memcmp(mode,"wait",4)?SORDER_WAIT:
          mode && length==4 && !memcmp(mode,"sync",4)?SORDER_SYNC:SORDER_OFF;
    p_lua_pushnumber(L,m!=SORDER_OFF && sorder_arm(m));return 1;
}
static int lua_shape_order_status(lua_State *L) {
    char out[1024];int n=accept_ui()?sorder_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_shape_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[160];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=sorder_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* nav_octree(mode, delay, snapshot): 'observe', 'sync', 'deadline' (delay: ticks from request to visibility, 1..3600) or 'agreed'. */
static int lua_nav_octree(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double delay=p_lua_tonumber(L,2);
    int m=mode && length==7 && !memcmp(mode,"observe",7)?NOCT_OBSERVE:mode && length==4 && !memcmp(mode,"sync",4)?NOCT_SYNC:
          mode && length==8 && !memcmp(mode,"deadline",8)?NOCT_DEADLINE:mode && length==6 && !memcmp(mode,"agreed",6)?NOCT_AGREED:NOCT_OFF;
    unsigned ticks=m==NOCT_DEADLINE && isfinite(delay) && delay>=1 && delay<=NOCT_DELAY_MAX && floor(delay)==delay?(unsigned)delay:0;
    double snapshot=p_lua_tonumber(L,3); /* 1: build every octree from its request's join snapshot (native_gate_nav_octree_snapshot) */
    p_lua_pushnumber(L,m!=NOCT_OFF && (m!=NOCT_DEADLINE || ticks) && (snapshot==0 || snapshot==1) && noct_arm(m,ticks,snapshot==1));return 1;
}
static int lua_nav_octree_status(lua_State *L) {
    char out[1536];int n=accept_ui()?noct_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* nav_octree_report(): this client's 'octree' input section (40 hex), nil when every index is shown. */
static int lua_nav_octree_report(lua_State *L) {
    char out[2*NOCT_REPORT_BYTES+1];int n=accept_ui()?noct_report_text(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* nav_octree_agree(tick, clients, payloads): the admitted 'octree' sections of every slot, comma
 * separated ('' for none); returns the receipt for the declared state, nil when refused. */
static int lua_nav_octree_agree(lua_State *L) {
    double tick=p_lua_tonumber(L,1),clients=p_lua_tonumber(L,2);const char *payloads=p_lua_tolstring(L,3,NULL);char out[160];int n=0;
    if(accept_ui() && isfinite(tick) && tick>=0 && floor(tick)==tick && isfinite(clients) && floor(clients)==clients && clients>=1 &&
       clients<=GATE_CLIENTS_MAX && clients==wire_clients)
        n=noct_agree_text((uint64_t)tick,(unsigned)clients,payloads,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* nav_octree_lookup_row(index): per-frame lookup and invalidation rows ('deadline', 'agreed'). */
static int lua_nav_octree_lookup_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=noct_look_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* nav_octree_sites(): per-site invalidation counts in the nav octree module's stubs ('deadline'/'agreed' installed). */
static int lua_nav_octree_sites(lua_State *L) {p_lua_pushnumber(L,nos_arm(noct_installed && noct_mode>=NOCT_DEADLINE));return 1;}
static int lua_nav_octree_sites_status(lua_State *L) {
    char out[768];int n=accept_ui()?nos_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_octree_site_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[320];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=nos_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_nav_octree_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=noct_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_body_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1),final=p_lua_tonumber(L,2);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=border_row_text((uint64_t)index,final==1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_camera_effect_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=rc_effect_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_camera_policy_rows(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=1 && ticks<=600 && floor(ticks)==ticks && rc_pick_rows_arm((unsigned)ticks));return 1;
}
static int lua_region_camera_policy_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[192];
    int n=isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index?rc_pick_row((uint64_t)index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* highway_rows(ticks[, from]): HIGHWAY_PARAM rows for ticks from..from+ticks-1 (from absent or 0: 1). */
static int lua_region_camera_highway_rows(lua_State *L) {
    double ticks=p_lua_tonumber(L,1),from=p_lua_tonumber(L,2);if(from==0) from=1;
    p_lua_pushnumber(L,isfinite(ticks) && isfinite(from) && ticks>=1 && ticks<=3600 && floor(ticks)==ticks && from>=1 && from<=1000000 &&
        floor(from)==from && rc_hw_rows_arm((unsigned)ticks,(unsigned)from));return 1;
}
static int lua_region_camera_highway_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[320];
    int n=isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index?rc_hw_row((uint64_t)index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_camera_status(lua_State *L) {
    char out[3072];int n=accept_ui()?rc_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gravidar_digest(rows): rows = 0 (or none) for digests only, else the ticks with every unit change as a row. */
static int lua_gravidar_digest(lua_State *L) {p_lua_pushnumber(L,gdig_arm(p_lua_tonumber(L,1)));return 1;}
static int lua_gravidar_digest_frame(lua_State *L) {
    char out[1024];int n=0;
    if(accept_ui() && gdig_armed) {gdig_frame();n=gdig_status(out,sizeof(out));}
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* gravidar_deadlines(): sector factor-pass deadlines and keyed draws, unit times at admission. Before loading. */
static int lua_gravidar_deadlines(lua_State *L) {p_lua_pushnumber(L,gdl_arm());return 1;}
static int lua_gravidar_deadlines_status(lua_State *L) {
    char out[1536];int n=accept_ui()?gdl_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_gravidar_digest_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=gdig_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_pass(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==4 && !memcmp(mode,"sync",4)?MT_SYNC:
          mode && length==7 && !memcmp(mode,"observe",7)?MT_OBSERVE:MT_OFF;
    p_lua_pushnumber(L,m!=MT_OFF && mt_region_arm(m));return 1;
}
static int lua_region_pass_status(lua_State *L) {
    char out[1024];int n=accept_ui()?mt_region_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_mass_traffic_status(lua_State *L) {
    char out[1024];int n=accept_ui()?mt_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_camera_offset(lua_State *L) {
    double tick=p_lua_tonumber(L,1),x=p_lua_tonumber(L,2),y=p_lua_tonumber(L,3),z=p_lua_tonumber(L,4);
    p_lua_pushnumber(L,isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && camoff_apply((uint64_t)tick,x,y,z));return 1;
}
/* camera_anchor(tick, slot, back, up[, orbit]): back/up in metres (chase view; both 0 keep the camera's
 * local PosRot); orbit 1 lets the keypad turn the chase rig (fixed from the first call). */
static int lua_camera_anchor(lua_State *L) {
    double tick=p_lua_tonumber(L,1),slot=p_lua_tonumber(L,2),back=p_lua_tonumber(L,3),up=p_lua_tonumber(L,4),orbit=p_lua_tonumber(L,5);
    int ok=isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && isfinite(slot) && slot>=1 && slot<=VIEW_MAX &&
        floor(slot)==slot && (orbit==0 || orbit==1);
    if(ok && !camfol_calls) camfol_orbit=orbit==1;
    else if(ok && camfol_orbit!=(orbit==1)) ok=camfol_fail("orbit");
    p_lua_pushnumber(L,ok && camfol_apply((uint64_t)tick,(unsigned)slot,back,up));return 1;
}
/* camera_anchor_keys(modifier, yaw_left, yaw_right, pitch_up, pitch_down, zoom_in, zoom_out, reset): virtual keys. */
static int lua_camera_anchor_keys(lua_State *L) {
    int keys[CAMFOL_KEYS],ok=1;
    for(unsigned i=0;i<CAMFOL_KEYS;i++) {double v=p_lua_tonumber(L,(int)i+1);ok=ok && isfinite(v) && floor(v)==v;keys[i]=ok?(int)v:-1;}
    p_lua_pushnumber(L,ok && camfol_keys_set(keys));return 1;
}
static int lua_camera_anchor_status(lua_State *L) {
    char out[768];int n=accept_ui()?camfol_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_camera_offset_status(lua_State *L) {
    char out[768];int n=accept_ui()?camoff_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_camera_perturb(lua_State *L) {
    double tick=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(tick) && tick>=1 && tick<9007199254740992.0 && floor(tick)==tick && camera_perturb_apply((uint64_t)tick));return 1;
}
static int lua_camera_perturb_status(lua_State *L) {
    char out[384];int n=accept_ui()?camera_perturb_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_travel_snapshot(lua_State *L) {p_lua_pushnumber(L,tsnap_arm());return 1;}
static int lua_travel_snapshot_status(lua_State *L) {
    char out[640];int n=accept_ui()?tsnap_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_travel_determinism_status(lua_State *L) {
    char out[512];int n=accept_ui()?travel_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_job_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==3 && !memcmp(mode,"key",3)?JOB_RNG_KEY:
          mode && length==7 && !memcmp(mode,"observe",7)?JOB_RNG_OBSERVE:-1;
    p_lua_pushnumber(L,m>=0 && job_rng_arm(m));return 1;
}
static int lua_job_rng_status(lua_State *L) {
    char out[768];int n=accept_ui()?job_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_job_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=job_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_event_order(lua_State *L) {p_lua_pushnumber(L,event_order_arm());return 1;}
/* event_rng(mode): 'observe' or 'key'; after event_order. */
static int lua_event_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    int m=mode && length==3 && !memcmp(mode,"key",3)?EVENT_RNG_KEY:mode && length==7 && !memcmp(mode,"observe",7)?EVENT_RNG_OBSERVE:-1;
    double from=p_lua_tonumber(L,2),to=p_lua_tonumber(L,3);int window=1;
    if(isfinite(from) && isfinite(to) && to>0) window=from>=1 && to>=from && to<9007199254740992.0 && floor(from)==from && floor(to)==to &&
        event_rng_window((uint64_t)from,(uint64_t)to);
    p_lua_pushnumber(L,m>0 && window && event_rng_arm(m,event_order_armed && event_order_installed));return 1;
}
static int lua_event_rng_status(lua_State *L) {
    char out[768];int n=accept_ui()?event_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_event_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[320];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=event_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_event_order_status(lua_State *L) {
    char out[768];int n=accept_ui()?event_order_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_event_rows(lua_State *L) {
    double ticks=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(ticks) && ticks>=0 && ticks<=600 && floor(ticks)==ticks && event_order_rows_arm((unsigned)ticks));return 1;
}
static int lua_event_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=event_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_script_rng(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);double mask=p_lua_tonumber(L,2),rows=p_lua_tonumber(L,3);
    double options=p_lua_tonumber(L,4); /* absent: 0 */
    int m=mode && length==3 && !memcmp(mode,"key",3)?SCRIPT_RNG_KEY:
          mode && length==7 && !memcmp(mode,"observe",7)?SCRIPT_RNG_OBSERVE:-1;
    p_lua_pushnumber(L,m>=0 && isfinite(mask) && mask>=1 && mask<=7 && floor(mask)==mask && (rows==0 || rows==1) &&
        isfinite(options) && options>=0 && options<=SCRIPT_RNG_OPTIONS && floor(options)==options &&
        script_rng_arm(m,(unsigned)mask,rows==1,(unsigned)options));return 1;
}
static int lua_script_rng_status(lua_State *L) {
    char out[2560];int n=accept_ui()?script_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_script_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[640];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=script_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_md_timed_rng(lua_State *L) {p_lua_pushnumber(L,md_timed_arm());return 1;}
static int lua_md_timed_rng_status(lua_State *L) {
    char out[384];int n=accept_ui()?md_timed_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_boot_hold(lua_State *L) {p_lua_pushnumber(L,boot_hold_arm());return 1;}
static int lua_boot_hold_status(lua_State *L) {
    char out[512];int n=accept_ui()?boot_hold_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_md_resume_rng(lua_State *L) {p_lua_pushnumber(L,md_resume_arm());return 1;}
static int lua_md_resume_rng_status(lua_State *L) {
    char out[768];int n=accept_ui()?md_resume_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_md_resume_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=md_resume_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_find_order(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mode) && (mode==FIND_ORDER_OBSERVE || mode==FIND_ORDER_SORT)?find_order_arm((int)mode):0);return 1;
}
static int lua_find_order_status(lua_State *L) {
    char out[768];int n=accept_ui()?find_order_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_find_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=find_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* region_rng(mode): 1 keys every Region::Move, 0 observes (same keys and rows, no write). */
static int lua_region_rng(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(mode==0.0 || mode==1.0)?region_rng_arm(mode==1.0?REGION_RNG_KEY:REGION_RNG_OBSERVE):0);return 1;
}
static int lua_region_rng_status(lua_State *L) {
    char out[1536];int n=accept_ui()?region_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_region_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=region_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* spawn_rng(mode): 1 keys ship loadouts by the caller's explicit generator
 * state, 0 observes (same keys and rows, no write). Before loading. */
static int lua_spawn_rng(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(mode==0.0 || mode==1.0)?spawn_rng_arm(mode==1.0?SPAWN_RNG_KEY:SPAWN_RNG_OBSERVE):0);return 1;
}
static int lua_spawn_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?spawn_rng_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_spawn_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=spawn_rng_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ai_wait_rng(lua_State *L) {p_lua_pushnumber(L,ai_wait_arm());return 1;}
static int lua_ai_wait_rng_status(lua_State *L) {
    char out[1024];int n=accept_ui()?ai_wait_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_ai_wait_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[512];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=ai_wait_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_snapshot_fixture(lua_State *L) {
    size_t length=0;const char *text=p_lua_tolstring(L,1,&length);
    int pins=snapshot_configured || (image &&
        hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36") &&
        hash_ok(image+0xefb10,5,"a8adead0a89e8bcc0f6079669194e35b31c483df478a1515e46c55a9cf74fb51"));
    p_lua_pushnumber(L,pins && snapshot_manifest(text,length));return 1;
}
static int lua_snapshot_actor(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || snapshot_bound!=12 || !isfinite(number) || number<1 || number>12 || floor(number)!=number) {p_lua_pushnil(L);return 1;}
    unsigned i=(unsigned)number-1;snapshot_fixture_actor *a=&snapshot_actors[i];nav_sample *s=&a->first_nav;char out[768];
    int n=snprintf(out,sizeof(out),"id=%u saved=%016llx runtime=%016llx flight=%s bound_tick=%llu prepared_tick=%llu nav_tick=%llu nav_calls=%llu initialized=%u time=%016llx deadline_before=%016llx deadline_after=%016llx rng_before=%016llx rng_after=%016llx",
        i+1,(unsigned long long)a->saved,(unsigned long long)a->runtime,snapshot_flight_kinds[i]?snapshot_flight_kinds[i]:"disabled",(unsigned long long)snapshot_bound_tick,
        (unsigned long long)snapshot_prepared_tick,(unsigned long long)s->tick,(unsigned long long)s->calls,s->initialized,
        (unsigned long long)s->time,(unsigned long long)s->deadline_before,(unsigned long long)s->deadline_after,
        (unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_snapshot_enroll(lua_State *L) {
    double actor=p_lua_tonumber(L,1);size_t length=0;const char *text=p_lua_tolstring(L,2,&length);uint64_t runtime=0;
    int ok=accept_ui() && stats.active && isfinite(actor) && actor>=1 && actor<=12 && floor(actor)==actor && text && length && length<=20;
    for(size_t i=0;ok && i<length;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) ok=0;
        else runtime=runtime*10+digit;
    }
    ok=ok && snapshot_enroll((unsigned)actor,runtime);
    if(!ok) InterlockedIncrement64(&snapshot_failures);
    p_lua_pushnumber(L,ok);return 1;
}
static int registry_refuse(lua_State *L,const char *why) {
    char out[2304];int n=snprintf(out,sizeof(out),"registry=%s stage=%s%s",why,registry_stage,registry_identity_note);
    InterlockedIncrement64(&registry_failures);finish(75);
    p_lua_pushnil(L);p_lua_pushlstring(L,out,(size_t)n);return 2;
}
static int lua_ship_reserve(lua_State *L) {
    size_t hl=0,ml=0,ol=0;const char *h=p_lua_tolstring(L,1,&hl),*macro=p_lua_tolstring(L,2,&ml),*owner=p_lua_tolstring(L,3,&ol);
    double tick=p_lua_tonumber(L,4);uint64_t handle;registry_stage="reservation";registry_identity_note[0]=0;
    if(!registry_enabled() || registry_failures || !registry_handle(h,hl,&handle) || !isfinite(tick) || tick<1 || tick>2147483647 ||
       floor(tick)!=tick || (tick!=(double)stats.issued && !registry_find(handle)) || !registry_pins()) return registry_refuse(L,"reservation_guard");
    unsigned actor=registry_reserve(handle,macro,ml,owner,ol,(uint64_t)tick);
    if(!actor) return registry_refuse(L,"reservation_record");
    p_lua_pushnumber(L,actor);return 1;
}
static int lua_ship_bind(lua_State *L) {
    size_t hl=0,rl=0;const char *h=p_lua_tolstring(L,1,&hl),*r=p_lua_tolstring(L,2,&rl);uint64_t handle,runtime;
    registry_stage="bind_guard";registry_identity_note[0]=0;
    if(!registry_enabled() || !creation_bind_arguments(h,hl,r,rl,&handle,&runtime)) return registry_refuse(L,"bind_guard");
    unsigned actor=registry_find(handle);void *map=*(void**)(image+0x6cf1500);
    if(!actor || !registry_bind(actor,runtime,(snapshot_find_fn)(image+0xced70),map)) return registry_refuse(L,"bind");
    registry_describe_identity(actor_components[actor-1],runtime);
    char evidence[2560];int n=snprintf(evidence,sizeof(evidence),
        "SHIP_BIND_IDENTITY_1 pinset=small_xl_v1 move_pin=7cc810/1503/a0346a6203409b640354b5bef3df9521b06d53da40fd23a1c701e562a863ec8b"
        " death_pin=7cd610/180/acf5deebe3cd68de95084938ccf71d601f4c5f1d1f18b2ecf4826ee85d43a99e%s",registry_identity_note);
    p_lua_pushnumber(L,1);p_lua_pushlstring(L,evidence,(size_t)n);return 2;
}
static int lua_ship_status(lua_State *L) {
    size_t hl=0;const char *h=p_lua_tolstring(L,1,&hl);uint64_t handle;
    registry_stage="status";registry_identity_note[0]=0;
    if(!registry_enabled() || registry_failures || !registry_handle(h,hl,&handle)) return registry_refuse(L,"status_guard");
    unsigned actor=registry_find(handle);if(!actor) return registry_refuse(L,"unknown_handle");
    dynamic_ship *s=&dynamic_ships[actor-ACTOR_DYNAMIC_FIRST];unsigned identity=0;
    if(s->bound_tick) {
        void *object;unsigned char *slots;
        identity=registry_native_ship(actor,actor_ids[actor-1],(snapshot_find_fn)(image+0xced70),*(void**)(image+0x6cf1500),&object,&slots) &&
            object==actor_components[actor-1] && s->prepared_tick && !s->late_updates;
        if(!identity) return registry_refuse(L,"lifetime");
    }
    char out[512];int n=snprintf(out,sizeof(out),"SHIP_REGISTRY_1;handle=%016llx;slot=%u;reserved_tick=%llu;bound_tick=%llu;prepared_tick=%llu;first_update_tick=%llu;first_update_kind=%u;late_updates=%u;identity_ok=%u",
        (unsigned long long)handle,actor,(unsigned long long)s->reserved_tick,(unsigned long long)s->bound_tick,(unsigned long long)s->prepared_tick,
        (unsigned long long)s->first_update_tick,s->first_update_kind,s->late_updates,identity);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_ship_raw_motion(lua_State *L) {
    size_t hl=0,rl=0;const char *h=p_lua_tolstring(L,1,&hl),*r=p_lua_tolstring(L,2,&rl);uint64_t handle,runtime;
    registry_stage="raw_motion_guard";registry_identity_note[0]=0;
    if(!registry_enabled() || registry_failures || !registry_handle(h,hl,&handle) ||
       !registry_runtime_text(r,rl,&runtime)) return registry_refuse(L,"raw_motion_guard");
    unsigned actor=registry_find(handle);void *map=NULL;char out[1536],detail[256];const char *why="map";
    if(!actor || !read_local(image+0x6cf1500,&map,8) || !map ||
       !registry_raw_motion(actor,runtime,(snapshot_find_fn)(image+0xced70),map,read_local,out,sizeof(out),detail,sizeof(detail),&why)) {
        registry_stage=why;return registry_refuse(L,"raw_motion");
    }
    p_lua_pushlstring(L,out,strlen(out));p_lua_pushlstring(L,detail,strlen(detail));return 2;
}
static int lua_registry_md(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[768];int n=0;
    if(accept_ui() && isfinite(index) && index>=1 && index<=REGISTRY_MD_CAPACITY && floor(index)==index)
        n=registry_md_format((unsigned)index-1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_update(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[384];int n=0;
    if(accept_ui() && isfinite(index) && index>=1 && index<=REGISTRY_PENDING_CAPACITY && floor(index)==index)
        n=registry_update_format((unsigned)index-1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_origin(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1536];int n=0;
    if(accept_ui() && isfinite(index) && index>=1 && index<=REGISTRY_PENDING_CAPACITY && floor(index)==index)
        n=registry_origin_format((unsigned)index-1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_origin_identity(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=1 && index<=REGISTRY_PENDING_CAPACITY && floor(index)==index)
        n=registry_origin_identity_format((unsigned)index-1,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_creation_trace_config(lua_State *L) {
    size_t length=0;const char *manifest=p_lua_tolstring(L,1,&length);
    int ok=GetCurrentThreadId()==ui_thread && creation_configure(manifest,length);
    if(!ok && stats.active)finish(75);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_creation_rng_config(lua_State *L) {
    size_t length=0,preimage_length=0;const char *manifest=p_lua_tolstring(L,1,&length);
    const char *preimage=p_lua_tolstring(L,2,&preimage_length);char out[768],ack[384];
    int ok=GetCurrentThreadId()==ui_thread && creation_rng_configure(manifest,length,preimage,preimage_length);
    int n=ok?creation_rng_receipt(out,sizeof(out)):0;
    int m=n?creation_rng_ack(ack,sizeof(ack)):0;
    if((!n || !m) && stats.active)finish(75);
    if(n && m){p_lua_pushlstring(L,out,(size_t)n);p_lua_pushlstring(L,ack,(size_t)m);return 2;}
    p_lua_pushnil(L);return 1;
}
static int lua_creation_rng_status(lua_State *L) {
    char out[768];int n=accept_ui()?creation_rng_status(out,sizeof(out)):0;
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_creation_rng_ack_status(lua_State *L) {
    char out[384];int n=accept_ui()?creation_rng_ack_status(out,sizeof(out)):0;
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_creation_config(lua_State *L) {
    size_t ml=0,rl=0,pl=0;const char *marker=p_lua_tolstring(L,1,&ml),*manifest=p_lua_tolstring(L,2,&rl),*preimage=p_lua_tolstring(L,3,&pl);
    int ok=registry_creation_configure(marker,ml,manifest,rl,preimage,pl);
    if(!ok && stats.active)finish(75);p_lua_pushnumber(L,ok);return 1;
}
static int lua_registry_creation_status(lua_State *L) {
    char out[1024];int n=accept_ui()?registry_creation_status(out,sizeof(out)):0;
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_creation_identity(lua_State *L) {
    double index=p_lua_tonumber(L,1),part=p_lua_tonumber(L,2);char out[1536];int n=0;
    if(accept_ui() && isfinite(index) && floor(index)==index && index>=1 && index<=4 &&
       isfinite(part) && floor(part)==part && part>=0 && part<=2)n=registry_creation_identity_format((unsigned)index,(unsigned)part,out,sizeof(out));
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_registry_creation_operation(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1536];int n=0;
    if(accept_ui() && isfinite(index) && floor(index)==index && index>=1 && index<=256)n=registry_creation_operation_format((unsigned)index,out,sizeof(out));
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_creation_rng_row(lua_State *L) {
    double scope=p_lua_tonumber(L,1),part=p_lua_tonumber(L,2);char out[1536];int n=0;
    if(accept_ui() && isfinite(scope) && isfinite(part) && scope>=1 && scope<=2 && floor(scope)==scope &&
       part>=0 && part<=1 && floor(part)==part)n=creation_rng_row((unsigned)scope,(unsigned)part,out,sizeof(out));
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_creation_trace_status(lua_State *L) {
    char out[1536];int n=accept_ui()?creation_status(out,sizeof(out)):0;
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_creation_trace_row(lua_State *L) {
    double index=p_lua_tonumber(L,1),part=p_lua_tonumber(L,2);char out[1536];int n=0;
    if(accept_ui() && isfinite(index) && index>=1 && index<=CREATION_CAPACITY && floor(index)==index &&
       isfinite(part) && part>=0 && part<CREATION_PARTS && floor(part)==part)
        n=creation_row((unsigned)index-1,(unsigned)part,out,sizeof(out));
    if(n)p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* s.inputs[slot] is the admitted input of the current tick in slot order and
 * identical on every client. Protocol 2 keeps input1/input2 unchanged. */
#include "lockstep_gate_lua_inputs.h"
static int lua_poll(lua_State *L) {
    gate_stats s;if(!x4gate_poll(&s)) {p_lua_pushnil(L);return 1;}
    p_lua_createtable(L,0,12);
    p_lua_pushnumber(L,2);p_lua_setfield(L,-2,"creation_trace_schema");
    p_lua_pushnumber(L,1);p_lua_setfield(L,-2,"creation_rng_schema");
    p_lua_pushnumber(L,1);p_lua_setfield(L,-2,"creation_rng_ack_schema");
    p_lua_pushnumber(L,2);p_lua_setfield(L,-2,"creation_rng_ack2_schema");
    p_lua_pushnumber(L,1);p_lua_setfield(L,-2,"registry_creation_schema");
    p_lua_pushnumber(L,1);p_lua_setfield(L,-2,"registry_origin_schema");
    p_lua_pushnumber(L,1);p_lua_setfield(L,-2,"registry_origin_identity_schema");
    AcquireSRWLockShared(&registry_lock);
    int origin_enabled=registry_origin_enabled,origin_closed=registry_origin_closing;
    LONG64 origin_open=registry_origin_open,origin_restore_open=registry_origin_restore_open,
        origin_failures=registry_origin_failures,origin_peak=registry_origin_peak;
    ReleaseSRWLockShared(&registry_lock);
    p_lua_pushnumber(L,origin_enabled);p_lua_setfield(L,-2,"registry_origin_enabled");
    p_lua_pushnumber(L,origin_closed);p_lua_setfield(L,-2,"registry_origin_closed");
    p_lua_pushnumber(L,(double)origin_open);p_lua_setfield(L,-2,"registry_origin_open");
    p_lua_pushnumber(L,(double)origin_restore_open);p_lua_setfield(L,-2,"registry_origin_restore_open");
    p_lua_pushnumber(L,(double)origin_failures);p_lua_setfield(L,-2,"registry_origin_failures");
    p_lua_pushnumber(L,(double)origin_peak);p_lua_setfield(L,-2,"registry_origin_peak");
    p_lua_pushnumber(L,2);p_lua_setfield(L,-2,"registry_md_schema");
    p_lua_pushnumber(L,registry_pending_count);p_lua_setfield(L,-2,"registry_pending_count");
    p_lua_pushnumber(L,registry_md_installed);p_lua_setfield(L,-2,"registry_md_installed");
    p_lua_pushnumber(L,registry_md_restored);p_lua_setfield(L,-2,"registry_md_restored");
    p_lua_pushnumber(L,registry_md_count);p_lua_setfield(L,-2,"registry_md_count");
    p_lua_pushnumber(L,(double)registry_md_failures);p_lua_setfield(L,-2,"registry_md_failures");
    p_lua_pushnumber(L,(double)registry_md_order);p_lua_setfield(L,-2,"registry_md_order");
    p_lua_pushnumber(L,(double)InterlockedCompareExchange64(&registry_md_open,0,0));p_lua_setfield(L,-2,"registry_md_open");
    p_lua_pushnumber(L,(double)registry_md_restore_open);p_lua_setfield(L,-2,"registry_md_restore_open");
    p_lua_pushnumber(L,(double)registry_md_peak);p_lua_setfield(L,-2,"registry_md_peak");
    push_inputs(L);
#define FIELD(name) p_lua_pushnumber(L,(double)s.name);p_lua_setfield(L,-2,#name)
    FIELD(issued);FIELD(completed);FIELD(waits);FIELD(report_tick);FIELD(game);FIELD(real);
    p_lua_pushnumber(L,dynamic_ship_count);p_lua_setfield(L,-2,"registry_count");
    p_lua_pushnumber(L,(double)registry_failures);p_lua_setfield(L,-2,"registry_failures");
    char registry_manifest[2048],registry_evidence[2048];int rm=0,re=0;
    for(unsigned i=0;i<dynamic_ship_count;i++) {
        dynamic_ship *d=&dynamic_ships[i];
        rm+=snprintf(registry_manifest+rm,sizeof(registry_manifest)-(size_t)rm,"%s%u/%016llx/%s/%s",i?",":"",i+ACTOR_DYNAMIC_FIRST,(unsigned long long)d->handle,d->macro,d->owner);
        re+=snprintf(registry_evidence+re,sizeof(registry_evidence)-(size_t)re,"%s%u/%016llx/%llu/%llu/%llu/%llu/%u/%u",i?",":"",i+ACTOR_DYNAMIC_FIRST,(unsigned long long)d->handle,
            (unsigned long long)d->reserved_tick,(unsigned long long)d->bound_tick,(unsigned long long)d->prepared_tick,(unsigned long long)d->first_update_tick,d->first_update_kind,d->late_updates);
    }
    p_lua_pushlstring(L,registry_manifest,(size_t)rm);p_lua_setfield(L,-2,"registry_manifest");
    p_lua_pushlstring(L,registry_evidence,(size_t)re);p_lua_setfield(L,-2,"registry_evidence");
    char registry_navigation[4096];int rn=0;
    for(unsigned i=0;i<dynamic_ship_count;i++) {
        nav_sample *v=&registry_first_nav[i];if(!v->calls) continue;
        rn+=snprintf(registry_navigation+rn,sizeof(registry_navigation)-(size_t)rn,
            "%s%u/%016llx/%llu/%llu/%u/%016llx/%016llx/%016llx/%016llx/%016llx",rn?",":"",i+ACTOR_DYNAMIC_FIRST,
            (unsigned long long)dynamic_ships[i].handle,(unsigned long long)v->tick,(unsigned long long)v->calls,v->initialized,
            (unsigned long long)v->time,(unsigned long long)v->deadline_before,(unsigned long long)v->deadline_after,
            (unsigned long long)v->rng_before,(unsigned long long)v->rng_after);
    }
    p_lua_pushlstring(L,registry_navigation,(size_t)rn);p_lua_setfield(L,-2,"registry_navigation");
    p_lua_pushnumber(L,snapshot_bound);p_lua_setfield(L,-2,"snapshot_bound");
    p_lua_pushnumber(L,snapshot_prepared);p_lua_setfield(L,-2,"snapshot_prepared");
    p_lua_pushnumber(L,snapshot_enrolled);p_lua_setfield(L,-2,"snapshot_enrolled");
    p_lua_pushnumber(L,(double)snapshot_failures);p_lua_setfield(L,-2,"snapshot_failures");
    char boundary[384];int boundary_size=snprintf(boundary,sizeof(boundary),"stage=%s actor=%u tick=%llu ui=%u main=%u view=%u time=%016llx model=%x bind_banks=%u/%u prepare_banks=%u/%u",
        snapshot_prepare_stage,snapshot_attempt_actor,(unsigned long long)snapshot_attempt_tick,snapshot_attempt_ui,snapshot_attempt_main,
        snapshot_attempt_view,(unsigned long long)snapshot_attempt_time,snapshot_attempt_model,
        snapshot_bound_banks.read,snapshot_bound_banks.write,snapshot_prepared_banks.read,snapshot_prepared_banks.write);
    p_lua_pushlstring(L,boundary,(size_t)boundary_size);p_lua_setfield(L,-2,"snapshot_boundary");
    FIELD(wait_game_delta);FIELD(max_dt_error);FIELD(active);FIELD(reason);FIELD(restored);FIELD(slot);
#undef FIELD
    p_lua_pushnumber(L,(double)path_joins);p_lua_setfield(L,-2,"path_joins");
    p_lua_pushnumber(L,path_restored);p_lua_setfield(L,-2,"path_restored");
    p_lua_pushnumber(L,(double)rng_seeds);p_lua_setfield(L,-2,"rng_seeds");
    p_lua_pushnumber(L,boot_phase);p_lua_setfield(L,-2,"boot_phase");
    p_lua_pushnumber(L,dt_restored);p_lua_setfield(L,-2,"dt_restored");
    p_lua_pushnumber(L,flight_restored);p_lua_setfield(L,-2,"flight_restored");
    p_lua_pushnumber(L,(double)flight_calls);p_lua_setfield(L,-2,"flight_calls");
    p_lua_pushnumber(L,(double)flight_failures);p_lua_setfield(L,-2,"flight_failures");
    p_lua_pushnumber(L,(double)flight_refreshes);p_lua_setfield(L,-2,"flight_refreshes");
    p_lua_pushnumber(L,(double)flight_avoidance_truncated);p_lua_setfield(L,-2,"flight_avoidance_truncated");
    p_lua_pushnumber(L,(double)flight_inits);p_lua_setfield(L,-2,"flight_inits");
    p_lua_pushnumber(L,(double)flight_ctor_calls);p_lua_setfield(L,-2,"flight_ctor_calls");
    p_lua_pushnumber(L,flight_ctor_restored);p_lua_setfield(L,-2,"flight_ctor_restored");
    p_lua_pushnumber(L,(double)flight_registration_inits);p_lua_setfield(L,-2,"flight_registration_inits");
    p_lua_pushnumber(L,(double)shield_refresh_calls);p_lua_setfield(L,-2,"shield_refresh_calls");
    p_lua_pushnumber(L,(double)shield_refresh_failures);p_lua_setfield(L,-2,"shield_refresh_failures");
    p_lua_pushnumber(L,(double)spline_calls);p_lua_setfield(L,-2,"spline_calls");
    p_lua_pushnumber(L,(double)spline_failures);p_lua_setfield(L,-2,"spline_failures");
    p_lua_pushnumber(L,spline_restored);p_lua_setfield(L,-2,"spline_restored");
    p_lua_pushnumber(L,(double)spline_refreshes);p_lua_setfield(L,-2,"spline_refreshes");
    p_lua_pushnumber(L,(double)spline_joins);p_lua_setfield(L,-2,"spline_joins");
    p_lua_pushnumber(L,(double)spline_waits);p_lua_setfield(L,-2,"spline_waits");
    p_lua_pushnumber(L,(double)spline_timeouts);p_lua_setfield(L,-2,"spline_timeouts");
    char flight_totals[1024];int flight_n=0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(actor_is_ship(i+1)) flight_n+=snprintf(flight_totals+flight_n,sizeof(flight_totals)-(size_t)flight_n,
        "%s%u/%llu",flight_n?",":"",i+1,(unsigned long long)flight_samples[i].init_generation);
    p_lua_pushlstring(L,flight_totals,(size_t)flight_n);p_lua_setfield(L,-2,"flight_init_totals");
    p_lua_pushnumber(L,LOCKSTEP_RNG_SCHEMA);p_lua_setfield(L,-2,"rng_schema");
    p_lua_pushnumber(L,ai_restored);p_lua_setfield(L,-2,"ai_restored");
    p_lua_pushnumber(L,(double)ai_calls);p_lua_setfield(L,-2,"ai_calls");
    p_lua_pushnumber(L,(double)ai_failures);p_lua_setfield(L,-2,"ai_failures");
    p_lua_pushnumber(L,radar_restored);p_lua_setfield(L,-2,"radar_restored");
    p_lua_pushnumber(L,(double)radar_calls);p_lua_setfield(L,-2,"radar_calls");
    p_lua_pushnumber(L,(double)radar_failures);p_lua_setfield(L,-2,"radar_failures");
    p_lua_pushnumber(L,gravidar_restored);p_lua_setfield(L,-2,"gravidar_restored");
    p_lua_pushnumber(L,(double)gravidar_frames);p_lua_setfield(L,-2,"gravidar_frames");
    p_lua_pushnumber(L,(double)gravidar_waits);p_lua_setfield(L,-2,"gravidar_waits");
    p_lua_pushnumber(L,(double)gravidar_timeouts);p_lua_setfield(L,-2,"gravidar_timeouts");
    p_lua_pushnumber(L,(double)gravidar_calls);p_lua_setfield(L,-2,"gravidar_calls");
    p_lua_pushnumber(L,(double)gravidar_failures);p_lua_setfield(L,-2,"gravidar_failures");
    p_lua_pushnumber(L,gravidar_inits);p_lua_setfield(L,-2,"gravidar_inits");
    p_lua_pushnumber(L,(double)interrupt_seeds);p_lua_setfield(L,-2,"interrupt_seeds");
    p_lua_pushnumber(L,interrupt_restored);p_lua_setfield(L,-2,"interrupt_restored");
    p_lua_pushnumber(L,(double)interrupt_calls);p_lua_setfield(L,-2,"interrupt_calls");
    p_lua_pushnumber(L,(double)interrupt_failures);p_lua_setfield(L,-2,"interrupt_failures");
    p_lua_pushnumber(L,(double)interrupt_step_calls);p_lua_setfield(L,-2,"interrupt_step_calls");
    p_lua_pushnumber(L,(double)nested_action_calls);p_lua_setfield(L,-2,"nested_action_calls");
    p_lua_pushnumber(L,(double)npc_action_calls);p_lua_setfield(L,-2,"npc_action_calls");
    p_lua_pushnumber(L,(double)npc_trigger_calls);p_lua_setfield(L,-2,"npc_trigger_calls");
    p_lua_pushnumber(L,(double)npc_cause_calls);p_lua_setfield(L,-2,"npc_cause_calls");
    p_lua_pushnumber(L,(double)npc_trigger_seen);p_lua_setfield(L,-2,"npc_trigger_seen");
    p_lua_pushnumber(L,(double)npc_trigger_foreign_thread);p_lua_setfield(L,-2,"npc_trigger_foreign_thread");
    p_lua_pushnumber(L,(double)npc_trigger_unsupported);p_lua_setfield(L,-2,"npc_trigger_unsupported");
    p_lua_pushnumber(L,(double)npc_provenance_calls);p_lua_setfield(L,-2,"npc_provenance_calls");
    p_lua_pushnumber(L,(double)npc_provenance_unresolved);p_lua_setfield(L,-2,"npc_provenance_unresolved");
    p_lua_pushnumber(L,(double)npc_provenance_unowned);p_lua_setfield(L,-2,"npc_provenance_unowned");
    p_lua_pushnumber(L,(double)npc_provenance_failures);p_lua_setfield(L,-2,"npc_provenance_failures");
    p_lua_pushnumber(L,(double)npc_provenance_overflows);p_lua_setfield(L,-2,"npc_provenance_overflows");
    p_lua_pushnumber(L,(double)npc_pilot_calls);p_lua_setfield(L,-2,"npc_pilot_calls");
    p_lua_pushnumber(L,(double)npc_pilot_failures);p_lua_setfield(L,-2,"npc_pilot_failures");
    p_lua_pushnumber(L,(double)npc_pilot_overflows);p_lua_setfield(L,-2,"npc_pilot_overflows");
    p_lua_pushnumber(L,(double)npc_pilot_foreign);p_lua_setfield(L,-2,"npc_pilot_foreign");
    p_lua_pushnumber(L,(double)npc_pilot_unmapped);p_lua_setfield(L,-2,"npc_pilot_unmapped");
    p_lua_pushnumber(L,(double)npc_pilot_unreadable);p_lua_setfield(L,-2,"npc_pilot_unreadable");
    p_lua_pushnumber(L,(double)npc_pilot_retired);p_lua_setfield(L,-2,"npc_pilot_retired");
    p_lua_pushnumber(L,(double)npc_pilot_after_unresolved);p_lua_setfield(L,-2,"npc_pilot_after_unresolved");
    p_lua_pushnumber(L,(double)npc_exec_calls);p_lua_setfield(L,-2,"npc_exec_calls");
    p_lua_pushnumber(L,npc_exec_restored);p_lua_setfield(L,-2,"npc_exec_restored");
    p_lua_pushnumber(L,(double)npc_speak_calls);p_lua_setfield(L,-2,"npc_speak_calls");
    p_lua_pushnumber(L,npc_speak_restored);p_lua_setfield(L,-2,"npc_speak_restored");
    p_lua_pushnumber(L,(double)npc_speak_seen);p_lua_setfield(L,-2,"npc_speak_seen");
    p_lua_pushnumber(L,(double)npc_speak_foreign_thread);p_lua_setfield(L,-2,"npc_speak_foreign_thread");
    p_lua_pushnumber(L,(double)npc_speak_unowned);p_lua_setfield(L,-2,"npc_speak_unowned");
    p_lua_pushnumber(L,(double)npc_enqueue_calls);p_lua_setfield(L,-2,"npc_enqueue_calls");
    p_lua_pushnumber(L,npc_enqueue_restored);p_lua_setfield(L,-2,"npc_enqueue_restored");
    p_lua_pushnumber(L,(double)npc_finish_calls);p_lua_setfield(L,-2,"npc_finish_calls");
    p_lua_pushnumber(L,npc_finish_restored);p_lua_setfield(L,-2,"npc_finish_restored");
    p_lua_pushnumber(L,(double)npc_finish_seen);p_lua_setfield(L,-2,"npc_finish_seen");
    p_lua_pushnumber(L,(double)npc_finish_foreign_thread);p_lua_setfield(L,-2,"npc_finish_foreign_thread");
    p_lua_pushnumber(L,(double)npc_finish_unowned);p_lua_setfield(L,-2,"npc_finish_unowned");
    p_lua_pushnumber(L,(double)npc_ready_calls);p_lua_setfield(L,-2,"npc_ready_calls");
    p_lua_pushnumber(L,(double)npc_ready_seen);p_lua_setfield(L,-2,"npc_ready_seen");
    p_lua_pushnumber(L,(double)npc_ready_foreign);p_lua_setfield(L,-2,"npc_ready_foreign");
    p_lua_pushnumber(L,(double)npc_ready_foreign_thread);p_lua_setfield(L,-2,"npc_ready_foreign_thread");
    p_lua_pushnumber(L,npc_ready_restored);p_lua_setfield(L,-2,"npc_ready_restored");
    p_lua_pushnumber(L,(double)npc_deadline_calls);p_lua_setfield(L,-2,"npc_deadline_calls");
    p_lua_pushnumber(L,(double)npc_deadline_seen);p_lua_setfield(L,-2,"npc_deadline_seen");
    p_lua_pushnumber(L,(double)npc_deadline_foreign);p_lua_setfield(L,-2,"npc_deadline_foreign");
    p_lua_pushnumber(L,(double)npc_deadline_foreign_thread);p_lua_setfield(L,-2,"npc_deadline_foreign_thread");
    p_lua_pushnumber(L,npc_deadline_restored);p_lua_setfield(L,-2,"npc_deadline_restored");
    p_lua_pushnumber(L,nested_action_restored);p_lua_setfield(L,-2,"nested_action_restored");
    p_lua_pushnumber(L,(double)world_identity_calls);p_lua_setfield(L,-2,"world_identity_calls");
    p_lua_pushnumber(L,(double)world_hash_scans);p_lua_setfield(L,-2,"world_hash_scans");
    p_lua_pushnumber(L,(double)world_hash_failures);p_lua_setfield(L,-2,"world_hash_failures");
    p_lua_pushnumber(L,(double)world_identity_count);p_lua_setfield(L,-2,"world_identity_count");
    p_lua_pushnumber(L,(double)world_identity_late);p_lua_setfield(L,-2,"world_identity_late");
    p_lua_pushnumber(L,(double)world_identity_failures);p_lua_setfield(L,-2,"world_identity_failures");
    p_lua_pushnumber(L,world_identity_restored);p_lua_setfield(L,-2,"world_identity_restored");
    p_lua_pushnumber(L,interrupt_step_restored);p_lua_setfield(L,-2,"interrupt_step_restored");
    p_lua_pushnumber(L,damage_restored);p_lua_setfield(L,-2,"damage_restored");
    p_lua_pushnumber(L,(double)damage_calls);p_lua_setfield(L,-2,"damage_calls");
    p_lua_pushnumber(L,(double)damage_failures);p_lua_setfield(L,-2,"damage_failures");
    p_lua_pushnumber(L,contact_restored);p_lua_setfield(L,-2,"contact_restored");
    p_lua_pushnumber(L,(double)contact_calls);p_lua_setfield(L,-2,"contact_calls");
    p_lua_pushnumber(L,(double)contact_failures);p_lua_setfield(L,-2,"contact_failures");
    p_lua_pushnumber(L,(double)contact_properties_calls);p_lua_setfield(L,-2,"contact_properties_calls");
    p_lua_pushnumber(L,contact_properties_restored);p_lua_setfield(L,-2,"contact_properties_restored");
    p_lua_pushnumber(L,(double)shape_lifetime_calls);p_lua_setfield(L,-2,"shape_lifetime_calls");
    p_lua_pushnumber(L,agent_restored);p_lua_setfield(L,-2,"agent_restored");
    p_lua_pushnumber(L,(double)agent_calls);p_lua_setfield(L,-2,"agent_calls");
    p_lua_pushnumber(L,(double)agent_failures);p_lua_setfield(L,-2,"agent_failures");
    p_lua_pushnumber(L,ship_restored);p_lua_setfield(L,-2,"ship_restored");
    p_lua_pushnumber(L,(double)ship_calls);p_lua_setfield(L,-2,"ship_calls");
    p_lua_pushnumber(L,(double)ship_failures);p_lua_setfield(L,-2,"ship_failures");
    p_lua_pushnumber(L,params_calls);p_lua_setfield(L,-2,"params_calls");
    p_lua_pushnumber(L,weapon_restored);p_lua_setfield(L,-2,"weapon_restored");
    p_lua_pushnumber(L,(double)weapon_calls);p_lua_setfield(L,-2,"weapon_calls");
    p_lua_pushnumber(L,(double)weapon_failures);p_lua_setfield(L,-2,"weapon_failures");
    p_lua_pushnumber(L,weapon_registered);p_lua_setfield(L,-2,"weapon_registered");
    p_lua_pushnumber(L,params_restored);p_lua_setfield(L,-2,"params_restored");
    p_lua_pushnumber(L,(double)params_trace_calls);p_lua_setfield(L,-2,"params_trace_calls");
    p_lua_pushnumber(L,(double)params_trace_failures);p_lua_setfield(L,-2,"params_trace_failures");
    p_lua_pushnumber(L,nav_restored);p_lua_setfield(L,-2,"nav_restored");
    p_lua_pushnumber(L,(double)nav_calls);p_lua_setfield(L,-2,"nav_calls");
    p_lua_pushnumber(L,(double)nav_failures);p_lua_setfield(L,-2,"nav_failures");
    p_lua_pushnumber(L,nav_inits);p_lua_setfield(L,-2,"nav_inits");
    p_lua_pushnumber(L,velocity_restored);p_lua_setfield(L,-2,"velocity_restored");
    p_lua_pushnumber(L,(double)velocity_calls);p_lua_setfield(L,-2,"velocity_calls");
    p_lua_pushnumber(L,(double)velocity_failures);p_lua_setfield(L,-2,"velocity_failures");
    p_lua_pushnumber(L,attention_restored);p_lua_setfield(L,-2,"attention_restored");
    p_lua_pushnumber(L,(double)attention_calls);p_lua_setfield(L,-2,"attention_calls");
    p_lua_pushnumber(L,(double)attention_failures);p_lua_setfield(L,-2,"attention_failures");
    p_lua_pushnumber(L,attention_inits);p_lua_setfield(L,-2,"attention_inits");
    p_lua_pushnumber(L,mass_restored);p_lua_setfield(L,-2,"mass_restored");
    p_lua_pushnumber(L,(double)mass_calls);p_lua_setfield(L,-2,"mass_calls");
    p_lua_pushnumber(L,(double)mass_failures);p_lua_setfield(L,-2,"mass_failures");
    p_lua_pushnumber(L,death_restored);p_lua_setfield(L,-2,"death_restored");
    p_lua_pushnumber(L,(double)death_calls);p_lua_setfield(L,-2,"death_calls");
    p_lua_pushnumber(L,(double)death_failures);p_lua_setfield(L,-2,"death_failures");
    p_lua_pushnumber(L,bullet_restored);p_lua_setfield(L,-2,"bullet_restored");
    p_lua_pushnumber(L,(double)bullet_calls);p_lua_setfield(L,-2,"bullet_calls");
    p_lua_pushnumber(L,(double)bullet_created);p_lua_setfield(L,-2,"bullet_created");
    p_lua_pushnumber(L,(double)bullet_failures);p_lua_setfield(L,-2,"bullet_failures");
    p_lua_pushnumber(L,(double)bullet_unknown);p_lua_setfield(L,-2,"bullet_unknown");
    p_lua_pushnumber(L,(double)bullet_outside);p_lua_setfield(L,-2,"bullet_outside");
    p_lua_pushnumber(L,(double)bullet_unsupported);p_lua_setfield(L,-2,"bullet_unsupported");
    p_lua_pushnumber(L,shape_restored);p_lua_setfield(L,-2,"shape_restored");
    p_lua_pushnumber(L,(double)shape_calls);p_lua_setfield(L,-2,"shape_calls");
    p_lua_pushnumber(L,(double)shape_failures);p_lua_setfield(L,-2,"shape_failures");
    p_lua_pushnumber(L,shape_inits);p_lua_setfield(L,-2,"shape_inits");
    char init_totals[4096];int init_n=0;uint64_t extra_calls=0,dock_calls=0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(actor_is_ship(i+1) || (shape_docking && actor_is_dock(i+1))) for(unsigned j=0;j<(actor_is_dock(i+1)?SHAPE_RECORDS:2u);j++) {
        if(j>=2 && !shape_extra[i-12][j-2].record) continue;
        init_n+=snprintf(init_totals+init_n,sizeof(init_totals)-(size_t)init_n,
            "%s%u/%u/%llu",init_n?",":"",i+1,j,(unsigned long long)shape_samples[i][j].init_total);
        if(j>=2) extra_calls+=shape_samples[i][j].total_calls;
        if(actor_is_dock(i+1)) dock_calls+=shape_samples[i][j].total_calls;
    }
    p_lua_pushlstring(L,init_totals,(size_t)init_n);p_lua_setfield(L,-2,"shape_init_totals");
    p_lua_pushnumber(L,shape_dock_registered);p_lua_setfield(L,-2,"shape_dock_registered");
    p_lua_pushnumber(L,shape_extra_registered);p_lua_setfield(L,-2,"shape_extra_registered");
    p_lua_pushnumber(L,shape_extra_replaced);p_lua_setfield(L,-2,"shape_extra_replaced");
    p_lua_pushnumber(L,(double)extra_calls);p_lua_setfield(L,-2,"shape_extra_calls");
    p_lua_pushnumber(L,(double)dock_calls);p_lua_setfield(L,-2,"shape_dock_calls");
    p_lua_pushnumber(L,(double)shape_forced_refreshes);p_lua_setfield(L,-2,"shape_forced_refreshes");
    /* Observation only: UI runs on MoveWorker here; never read another thread's
     TLS and never equate this stream with all RNG state in the engine. */
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(slots && slots[0]) {
        uint64_t rng=*(uint64_t*)((unsigned char*)slots[0]+0x318);char value[17];
        snprintf(value,sizeof(value),"%016llx",(unsigned long long)rng);
        p_lua_pushlstring(L,value,16);p_lua_setfield(L,-2,"rng");
    }
    return 1;
}
static int lua_stop(lua_State *L) {p_lua_pushnumber(L,x4gate_stop());return 1;}
static __attribute__((noinline)) void sample_copy(void *dest,const void *source,size_t bytes) {memcpy(dest,source,bytes);}
static void (*volatile sample_copy_fn)(void *,const void *,size_t)=sample_copy;
/* native_gate_fast_reads, default off: small reads by a guarded copy (about 1 ns) instead of
 * ReadProcessMemory (about 255 ns, 10-15 thousand per tick in the play stack, most of them
 * on the pathing workers, all counting on one shared word). A fault costs about 1.2 us
 * instead of 0.2 us and fails the read the same way. Large reads, such as another
 * thread's stack with its guard page, stay system calls. reads= then counts only those. */
#define READ_LOCAL_FAST_MAX 4096u
static int read_local(const void *source,void *dest,size_t bytes) {
    if(read_local_fast && bytes<=READ_LOCAL_FAST_MAX) {
        if(!source) return 0;
        __try {sample_copy_fn(dest,source,bytes);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION || GetExceptionCode()==EXCEPTION_GUARD_PAGE?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
        fp_patch_view(source,dest,bytes);return 1;
    }
    ++*(volatile uint64_t*)&read_local_calls;
    SIZE_T got=0;
    if(!source || !ReadProcessMemory(GetCurrentProcess(),source,dest,bytes,&got) || got!=bytes) return 0;
    fp_patch_view(source,dest,bytes);return 1;
}
/* The probe's per-tick samplers (gate.actor, gate.body, shield state) read ~1,300 times a
 * tick: a guarded copy instead of a ReadProcessMemory system call. A faulting read fails
 * the same way (clang's __try covers faults only inside calls: the copy goes through an
 * opaque pointer, as in lockstep_script_rng.h). */
static int sample_read(const void *source,void *dest,size_t bytes) {
    if(!source) return 0;
    __try {sample_copy_fn(dest,source,bytes);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
    fp_patch_view(source,dest,bytes);return 1;
}
/* count words as %08x each (lowercase), without a snprintf call per word; 0 when out is too small. */
static int hex_words(char *out,size_t capacity,const uint32_t *words,unsigned count) {
    static const char digits[]="0123456789abcdef";
    if(capacity<(size_t)count*8u+1u) {if(capacity) out[0]=0;return 0;}
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<8;j++) out[i*8u+j]=digits[(words[i]>>(28u-4u*j))&15u];
    out[count*8u]=0;return (int)(count*8u);
}
/* Diagnostic only: observe the actual navigation buffers before a UI transform
 * getter refreshes its cache. No raw pointers or instance IDs enter the output. */
static int actor_unavailable(lua_State *L,const char *stage,const void *detail) {
    uintptr_t value=(uintptr_t)detail,base=(uintptr_t)image;
    unsigned long long rva=value>=base && value-base<0x6fa9000 ? (unsigned long long)(value-base) : 0;
    char message[128];int n=snprintf(message,sizeof(message),"stage=%s detail_rva=%llx",stage,rva);
    p_lua_pushnil(L);p_lua_pushlstring(L,message,(size_t)n);return 2;
}
static int lua_dock_actor(lua_State *L,unsigned char *object,unsigned char *controller,double actor) {
    uint64_t hull,id;unsigned attention;char shield[192],dock[512],out[1024];
    if(!actor_number_is_ship(actor) || actor_components[(unsigned)actor-1]!=object ||
       !read_local(object+8,&id,8) || id!=actor_ids[(unsigned)actor-1] ||
       !dock_state_pins() || !dock_controller_state(controller,object,dock) ||
       !read_local(object+0x280,&hull,8) || !read_local(object+0x1ec,&attention,4) || !shield_state(object,shield))
        return actor_unavailable(L,"dock_state",NULL);
    double refresh=p_lua_tonumber(L,3);
    if(refresh!=0 && (refresh!=1 || !boot_mode || boot_phase!=3 || !shield_refresh_native(object) || !shield_state(object,shield))) {
        ++shield_refresh_failures;return actor_unavailable(L,"dock_shield",NULL);
    }
    int n=snprintf(out,sizeof(out),"attention=%u;hull_bits=%016llx;shield=%s;controller=dock;dock_state=%s",attention,(unsigned long long)hull,shield,dock);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static void *(*actor_find_override)(void *,uint64_t,unsigned); /* tests; NULL runs X4's lookup */
static int lua_actor(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !isfinite(number) || number<1 || number>2199023255551.0 || floor(number)!=number ||
       !hash_ok(image+0xced70,32,"de126e38d2532250fc386f851f355cdb4812bf6b4507ace0549728e27505f268")) {return actor_unavailable(L,"guard",NULL);}
    typedef void *(*find_fn)(void *,uint64_t,unsigned);
    void *map=*(void**)(image+0x6cf1500);
    find_fn find=actor_find_override?actor_find_override:(find_fn)(image+0xced70);
    unsigned char *object=map?find(map,(uint64_t)number,4):NULL;
    unsigned char object_data[0x288],ctrl_data[0xc0];
    if(!sample_read(object,object_data,sizeof(object_data))) {return actor_unavailable(L,"object_read",NULL);}
    unsigned char *controller;memcpy(&controller,object_data+0x200,8);
    if(!sample_read(controller,ctrl_data,sizeof(ctrl_data))) {return actor_unavailable(L,"controller_read",NULL);}
    void *vtable;memcpy(&vtable,ctrl_data,8);
    double actor=p_lua_tonumber(L,2);
    if(snapshot_configured && actor>=1 && actor<=12 && floor(actor)==actor &&
       (!snapshot_actors[(unsigned)actor-1].enrolled || actor_components[(unsigned)actor-1]!=object ||
        actor_ids[(unsigned)actor-1]!=(uint64_t)number)) return actor_unavailable(L,"snapshot_enrollment",NULL);
    if(actor_number_is_ship(actor) && actor_role((unsigned)actor)==ACTOR_DYNAMIC_SHIP &&
       (!dynamic_ships[(unsigned)actor-ACTOR_DYNAMIC_FIRST].prepared_tick ||
        actor_components[(unsigned)actor-1]!=object || actor_ids[(unsigned)actor-1]!=(uint64_t)number))
        return actor_unavailable(L,"dynamic_enrollment",NULL);
    if(vtable==image+DOCK_CONTROLLER_VTABLE || vtable==image+UNDOCK_CONTROLLER_VTABLE) return lua_dock_actor(L,object,controller,actor);
    /* A stock slot's PlayerController derives from NavigationController: the fields read below
     * (+0x30 banks, +0x40 Agent, +0xc0 refresh) are the same ones on the same Agent. */
    int slot_controller=pslot_vtable_is_copy((uintptr_t)vtable);
    if(vtable!=image+0x2c51158 && !slot_controller) {return actor_unavailable(L,"controller_type",vtable);}
    unsigned char *slots;memcpy(&slots,ctrl_data+0x30,8);
    if(actor_number_is_ship(actor)) {
        actor_components[(unsigned)actor-1]=object;memcpy(&actor_ids[(unsigned)actor-1],object_data+8,8);
        if(gravidar_initial && !gravidar_register((unsigned)actor)) {finish(64);return actor_unavailable(L,"radar_registration",NULL);}
        if(agent_installed && slots) agent_samples[(unsigned)actor-1].agent=slots-0xad0;
        if(params_enabled && !params_done[(unsigned)actor-1]) {
            unsigned char *parameters;memcpy(&parameters,ctrl_data+0x40,8);
            unsigned write_index=*(unsigned*)(image+0x2f4b6f4);
            if(!slots || parameters!=slots-0xad0 || write_index>1 || !agent_identity((unsigned)actor-1,parameters) ||
               !params_once((params_fn)(image+0xea1e80),(unsigned)actor,controller,parameters+write_index*0x1a0)) {
                finish(36);return actor_unavailable(L,"parameter_initialization",NULL);
            }
        }
    }
    int registered=0;
    if(flight_installed && actor_number_is_ship(actor)) registered=flight_register((unsigned)actor,slots);
    unsigned index=*(unsigned*)(image+0x38f4ee4);
    if(index>1) {return actor_unavailable(L,"state_index",NULL);}
    uint32_t state[2][24],cache[16];double hull;unsigned attention;
    for(unsigned i=0;i<2;i++) if(!sample_read(slots+i*0x1a0,state[i],sizeof(state[i]))) {return actor_unavailable(L,"state_read",NULL);}
    memcpy(cache,ctrl_data+0x60,sizeof(cache));memcpy(&hull,object_data+0x280,8);memcpy(&attention,object_data+0x1ec,4);
    char out[6144];int n=snprintf(out,sizeof(out),"attention=%u;hull=%.17g;index=%u;current=",attention,hull,index);
    n+=hex_words(out+n,sizeof(out)-(size_t)n,state[index],24);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";previous=");
    n+=hex_words(out+n,sizeof(out)-(size_t)n,state[1-index],24);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";cache=");
    n+=hex_words(out+n,sizeof(out)-(size_t)n,cache,16);
    uint64_t hull_bits;memcpy(&hull_bits,&hull,8);char shield[192];
    int shield_ok=shield_state(object,shield);
    double refresh=p_lua_tonumber(L,3);
    if(refresh!=0) {
        if(refresh!=1 || !boot_mode || boot_phase!=3 || !actor_number_is_ship(actor) ||
           !shield_ok || !shield_refresh_native(object) || !shield_state(object,shield)) {
            ++shield_refresh_failures;return actor_unavailable(L,"shield_refresh",NULL);
        }
    }
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";hull_bits=%016llx;shield=%s",(unsigned long long)hull_bits,shield_ok?shield:"unavailable");
    if(flight_initial) {
        uint32_t angles[2];flight_sample *s=registered?&flight_samples[(unsigned)actor-1]:NULL;
        if(s && sample_read((unsigned char*)s->model+0x33c,angles,8)) {
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";steering=%llu:%08x:%08x",(unsigned long long)s->init_generation,angles[0],angles[1]);
        } else {
            void *model,*model_vtable;
            uint64_t generation=(actor_number_is_ship(actor))?flight_samples[(unsigned)actor-1].init_generation:0;
            if(!slots || !sample_read(slots-0xad0+0xf10,&model,8)) {return actor_unavailable(L,"steering_model_read",NULL);}
            if(!model) n+=snprintf(out+n,sizeof(out)-(size_t)n,";steering=none:%llu",(unsigned long long)generation);
            else if(sample_read(model,&model_vtable,8)) {
                if(model_vtable==image+0x2c4f480) n+=snprintf(out+n,sizeof(out)-(size_t)n,";steering=spline:%llu",(unsigned long long)generation);
                else if(slot_controller) n+=snprintf(out+n,sizeof(out)-(size_t)n,";steering=slot:%llu:%llx",(unsigned long long)generation,(unsigned long long)((unsigned char*)model_vtable-image));
                else n+=snprintf(out+n,sizeof(out)-(size_t)n,";steering=unsupported:%llx",(unsigned long long)((unsigned char*)model_vtable-image));
            }
            else {return actor_unavailable(L,"steering_type_read",NULL);}
        }
    }
    if(spline_installed) {
        void *model,*model_vtable;uint32_t values[4];
        if(!slots || !sample_read(slots-0xad0+0xf10,&model,8)) {return actor_unavailable(L,"spline_model_read",NULL);}
        if(model && !sample_read(model,&model_vtable,8)) {return actor_unavailable(L,"spline_type_read",NULL);}
        if(model && model_vtable==image+SPLINE_ENTRY-0x70) {
            if(!spline_state(model,slots-0xad0,values)) {return actor_unavailable(L,"spline_state",model_vtable);}
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";spline_state=%08x%08x%08x%08x",values[0],values[1],values[2],values[3]);
        } else n+=snprintf(out+n,sizeof(out)-(size_t)n,";spline_state=none");
    }
    if(agent_installed && actor_number_is_ship(actor) && agent_identity((unsigned)actor-1,slots-0xad0)) {
        unsigned char *agent=slots-0xad0;uint32_t limits[24];uint64_t stamp;
        for(unsigned j=0;j<3;j++) {
            unsigned offset=j*0x1a0;
            if(!sample_read(agent+offset,limits,sizeof(limits)) || !sample_read(agent+offset+0x178,&stamp,8)) break;
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";limits%u=",j);
            n+=hex_words(out+n,sizeof(out)-(size_t)n,limits,24);
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";stamp%u=%016llx",j,(unsigned long long)stamp);
        }
        unsigned char *role,*posts,*pilot,*controlled,skill_byte;uint64_t role_index;void *controlled_ship,*pilot_vtable;
        if(sample_read(image+0x38f4d00,&role,8) && sample_read(role+0x1b0,&role_index,8) && role_index<64 &&
           sample_read(object+0x418,&posts,8) && sample_read(posts+role_index*16+8,&pilot,8) &&
           sample_read(pilot+0x308,&controlled,8) && sample_read(controlled+role_index*8,&controlled_ship,8) && controlled_ship==object &&
           sample_read(pilot+0x3d6,&skill_byte,1) && sample_read(pilot,&pilot_vtable,8)) {
            n+=snprintf(out+n,sizeof(out)-(size_t)n,";pilot_piloting=%u;pilot_packed=%02x;pilot_vtable=%llx",skill_byte>>4,skill_byte,
                (unsigned long long)((unsigned char*)pilot_vtable-image));
        }
    }
    /* Diagnostic only: the remaining state contains references, flags and
     * padding, so it must not enter the portable gameplay digest as raw bytes.
     * Capture it to distinguish late waypoint publication from physics drift. */
    uint64_t refresh_time;unsigned refresh_index;
    if(!sample_read(controller+0xc0,&refresh_time,8) || !sample_read(controller+0xc8,&refresh_index,4))
        return actor_unavailable(L,"navigation_refresh",NULL);
    n+=snprintf(out+n,sizeof(out)-(size_t)n,";nav_refresh=%016llx:%u",(unsigned long long)refresh_time,refresh_index);
    for(unsigned j=0;j<2;j++) {
        uint32_t tail[0x140/4];
        if(!sample_read(slots+(j?1-index:index)*0x1a0+0x60,tail,sizeof(tail))) return actor_unavailable(L,"state_tail",NULL);
        n+=snprintf(out+n,sizeof(out)-(size_t)n,";nav_tail%u=",j);
        n+=hex_words(out+n,sizeof(out)-(size_t)n,tail,0x140/4);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_flight(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!flight_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    flight_sample *s=&flight_samples[(unsigned)actor-1];
    char out[8192];int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu cheap=%llu applied_cheap=%llu dt=%08x rng_before=%016llx rng_after=%016llx before=",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->cheap,(unsigned long long)s->applied_cheap,s->dt,
        (unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    for(unsigned i=0;i<24;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," after=");
    for(unsigned i=0;i<24;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->after[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," behaviour=%llx model=",(unsigned long long)s->behaviour_rva);
    for(unsigned i=0;i<0x3a0/4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->model_before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," limits=");
    for(unsigned i=0;i<0x60/4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->agent_limits[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," input=");
    for(unsigned i=0;i<0x1a0/4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->slot_before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," params=");
    for(unsigned i=0;i<0x1a0/4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->params_before[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_spline(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    /* This getter reads only captured arrays. Keep the final sample available
     * after owned restoration, so sparse logging cannot hide the failed tick. */
    if((!spline_installed && !spline_restored) || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    spline_sample *s=&spline_samples[(unsigned)actor-1];char out[8192];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu cheap=%llu applied_cheap=%llu player=%llu dt=%08x time=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->cheap,
        (unsigned long long)s->applied_cheap,(unsigned long long)s->player,s->dt,(unsigned long long)s->time);
    const uint32_t *blocks[]={s->model[0],s->model[1],s->slot[0],s->slot[1],s->params};
    const unsigned counts[]={0x4b0/4,0x4b0/4,0x1a0/4,0x1a0/4,0x1a0/4};
    const char *names[]={"model_before","model_after","before","after","params"};
    n+=snprintf(out+n,sizeof(out)-(size_t)n," waited=%llu",(unsigned long long)s->waited);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," pending_before=%llu:%08x:%08x pending_after=%llu:%08x:%08x",
        (unsigned long long)s->pending_count[0],s->pending_status[0],s->pending_flags[0],
        (unsigned long long)s->pending_count[1],s->pending_status[1],s->pending_flags[1]);
    for(unsigned block=0;block<5;block++) {
        n+=snprintf(out+n,sizeof(out)-(size_t)n," %s=",names[block]);
        for(unsigned i=0;i<counts[block];i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",blocks[block][i]);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_avoidance(lua_State *L) {
    double actor=p_lua_tonumber(L,1),phase=p_lua_tonumber(L,2);
    if(!flight_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor) ||
       phase<0 || phase>1 || floor(phase)!=phase) {p_lua_pushnil(L);return 1;}
    flight_sample *s=&flight_samples[(unsigned)actor-1];unsigned p=(unsigned)phase;
    char out[FLIGHT_OBSTACLES*256+256];
    int n=snprintf(out,sizeof(out),"tick=%llu phase=%u time=%016llx count=%u failures=%llu entries=",
        (unsigned long long)s->tick,p,(unsigned long long)s->avoidance_time[p],s->avoidance_count[p],
        (unsigned long long)flight_failures);
    for(unsigned j=0;j<s->avoidance_count[p];j++)
        for(unsigned i=0;i<0x80/4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->avoidance[p][j][i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_ai(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!ai_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    ai_sample *s=&ai_samples[(unsigned)actor-1];char out[512];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu script=%s pc=%llu next=%llu rng_before=%016llx rng_after=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,s->script,(unsigned long long)s->pc_before,
        (unsigned long long)s->pc_after,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_agent(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!agent_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    agent_sample *s=&agent_samples[(unsigned)actor-1];char out[1024];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu cheap=%llu applied_cheap=%llu dt=%08x time=%016llx rng_before=%016llx rng_after=%016llx tls_time=%016llx tls_view=%u before=",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->cheap,(unsigned long long)s->applied_cheap,s->dt,
        (unsigned long long)s->time,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after,(unsigned long long)s->tls_time,s->tls_view);
    for(unsigned i=0;i<24;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->limits_before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," after=");
    for(unsigned i=0;i<24;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",s->limits_after[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_ship(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!ship_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    ship_sample *s=&ship_samples[(unsigned)actor-1];char out[256];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu rng_before=%016llx rng_after=%016llx",(unsigned long long)s->tick,
        (unsigned long long)s->calls,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_register_weapon(lua_State *L) {
    double number=p_lua_tonumber(L,1),actor=p_lua_tonumber(L,2),slot=p_lua_tonumber(L,3);
    int ok=0;
    if(stats.active && weapon_installed && GetCurrentThreadId()==ui_thread && isfinite(number) && number>=1 &&
       number<=2199023255551.0 && floor(number)==number && actor_number_is_ship(actor) &&
       slot>=1 && slot<=WEAPON_SLOTS && floor(slot)==slot &&
       hash_ok(image+0xced70,32,"de126e38d2532250fc386f851f355cdb4812bf6b4507ace0549728e27505f268")) {
        typedef void *(*find_fn)(void *,uint64_t,unsigned);
        void *map=*(void**)(image+0x6cf1500);
        void *object=map?((find_fn)(image+0xced70))(map,(uint64_t)number,4):NULL;
        ok=weapon_register(object,(uint64_t)number,(unsigned)actor,(unsigned)slot);
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_weapon(lua_State *L) {
    double actor=p_lua_tonumber(L,1),slot=p_lua_tonumber(L,2);
    if(!weapon_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor) ||
       slot<1 || slot>WEAPON_SLOTS || floor(slot)!=slot) {p_lua_pushnil(L);return 1;}
    weapon_sample *s=&weapon_samples[(unsigned)actor-1][(unsigned)slot-1];char out[256];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu rng_before=%016llx rng_after=%016llx",(unsigned long long)s->tick,
        (unsigned long long)s->calls,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_params(lua_State *L) {
    double actor=p_lua_tonumber(L,1),ordinal=p_lua_tonumber(L,2);
    if(!params_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor) ||
       ordinal<0 || ordinal>PARAMS_TRACE_MAX || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    params_trace *trace=&params_traces[(unsigned)actor-1];unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal==0) {p_lua_pushnumber(L,count);return 1;}
    if(ordinal>count) {p_lua_pushnil(L);return 1;}
    params_record *r=&trace->records[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu source=%llx destination=%d base=%u time=%016llx view=%u rng_before=%016llx rng_after=%016llx flags=%02x%02x%02x%02x values=",
        (unsigned long long)trace->tick,(unsigned long long)r->source,r->destination,r->base,(unsigned long long)r->time,r->view,
        (unsigned long long)r->rng_before,(unsigned long long)r->rng_after,r->flags[0],r->flags[1],r->flags[2],r->flags[3]);
    for(unsigned i=0;i<24;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",r->values[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_nav(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!nav_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    nav_sample *s=&nav_samples[(unsigned)actor-1];char out[512];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu time=%016llx deadline_before=%016llx deadline_after=%016llx initialized=%u rng_before=%016llx rng_after=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,(unsigned long long)s->deadline_before,
        (unsigned long long)s->deadline_after,s->initialized,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_velocity(lua_State *L) {
    double actor=p_lua_tonumber(L,1),ordinal=p_lua_tonumber(L,2);
    if(!velocity_installed || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor) ||
       ordinal<0 || ordinal>VELOCITY_MAX || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    velocity_trace *trace=&velocity_traces[(unsigned)actor-1];unsigned count=trace->tick==stats.issued?trace->count:0;
    if(ordinal==0) {p_lua_pushnumber(L,count);return 1;}
    if(ordinal>count) {p_lua_pushnil(L);return 1;}
    velocity_record *r=&trace->records[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"tick=%llu source=%llx time=%016llx input=",(unsigned long long)trace->tick,
        (unsigned long long)r->source,(unsigned long long)r->time);
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",r->input[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," before=");
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",r->before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," after=");
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",r->after[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_body(lua_State *L) {
    double actor=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2);char out[1024];uint64_t id;
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !actor_number_is_ship(actor) ||
       which<0 || which>1 || floor(which)!=which) {p_lua_pushnil(L);return 1;}
    unsigned i=(unsigned)actor-1;unsigned char *object=actor_components[i];
    if(!object || !read_local(object+8,&id,8) || id!=actor_ids[i] || !body_state(object,(unsigned)which,out)) {p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,strlen(out));return 1;
}
static int lua_shape(lua_State *L) {
    double actor=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2);
    if(!shape_installed || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || actor<1 || actor>ACTOR_CAPACITY || floor(actor)!=actor || (!actor_is_ship((unsigned)actor) && !(shape_docking && actor_is_dock((unsigned)actor))) ||
       which<0 || which>(actor_is_dock((unsigned)actor)?SHAPE_RECORDS-1:1) || floor(which)!=which) {p_lua_pushnil(L);return 1;}
    if(which>=2 && !shape_extra[(unsigned)actor-13][(unsigned)which-2].record) {p_lua_pushnil(L);return 1;}
    shape_sample *s=&shape_samples[(unsigned)actor-1][(unsigned)which];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu time=%016llx flag=%u due_before=%016llx due_after=%016llx initialized=%u rng_before=%016llx rng_after=%016llx init_total=%llu last_init=%llu",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,s->flag,(unsigned long long)s->due_before,
        (unsigned long long)s->due_after,s->initialized,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after,
        (unsigned long long)s->init_total,(unsigned long long)s->last_init);
    if(actor_is_dock((unsigned)actor)) n+=snprintf(out+n,sizeof(out)-(size_t)n," component_id=%llx native_due_after=%016llx forced=%u",
        (unsigned long long)shape_dock_ids[(unsigned)actor-13],(unsigned long long)s->native_due_after,s->forced);
    if(which>=2) n+=snprintf(out+n,sizeof(out)-(size_t)n," scene=%u layer=%u body_id=%08x total_calls=%llu replaced=%u",
        ((unsigned)which-2)/5,((unsigned)which-2)%5,shape_extra[(unsigned)actor-13][(unsigned)which-2].body_id,
        (unsigned long long)s->total_calls,shape_extra[(unsigned)actor-13][(unsigned)which-2].replaced);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_register_dock_collider(lua_State *L) {
    double slot=p_lua_tonumber(L,1),anchor=p_lua_tonumber(L,2),object=p_lua_tonumber(L,3);int ok=0;
    if(stats.active && boot_phase==3 && shape_docking && wire_pair() && GetCurrentThreadId()==ui_thread &&
       isfinite(slot) && slot>=1 && slot<=2 && floor(slot)==slot &&
       isfinite(anchor) && anchor>=1 && anchor<=2199023255551.0 && floor(anchor)==anchor &&
       isfinite(object) && object>=1 && object<=2199023255551.0 && floor(object)==object) {
        typedef void *(*find_fn)(void *,uint64_t,unsigned);
        void *map=*(void**)(image+0x6cf1500);find_fn find=(find_fn)(image+0xced70);
        if(map) ok=shape_bind_dock((unsigned)slot,find(map,(uint64_t)anchor,4),find(map,(uint64_t)object,4));
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_shape_lifetime(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 ||
       floor(ordinal)!=ordinal || ordinal>shape_lifetime_count) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,shape_lifetime_count);return 1;}
    shape_lifetime *e=&shape_lifetimes[(unsigned)ordinal-1];char out[384];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu id=%u which=%u seen=%u initialized=%u due_before=%016llx before=%u:%llx:%llx:%08x:%016llx after=%u:%llx:%llx:%08x:%016llx",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal,e->actor,e->which,e->seen,e->initialized,
        (unsigned long long)e->due_before,e->before.present,(unsigned long long)(uintptr_t)e->before.record,
        (unsigned long long)(uintptr_t)e->before.holder,e->before.body_id,(unsigned long long)e->before.holder_id,e->after.present,
        (unsigned long long)(uintptr_t)e->after.record,(unsigned long long)(uintptr_t)e->after.holder,e->after.body_id,(unsigned long long)e->after.holder_id);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_shape_lifetime_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) shape_lifetime_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_bullet(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,bullet_event_count);return 1;}
    if(ordinal>bullet_event_count) {p_lua_pushnil(L);return 1;}
    bullet_event *e=&bullet_events[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"id=%u slot=%u tick=%llu spawn=%llu call=%llu created=%u rng_before=%016llx rng_after=%016llx check_before=%016llx check_after=%016llx hits_before=%d hits_after=%d",
        e->actor,e->slot,(unsigned long long)e->tick,(unsigned long long)e->spawn,(unsigned long long)e->call,e->created,
        (unsigned long long)e->before,(unsigned long long)e->after,(unsigned long long)e->check_before,
        (unsigned long long)e->check_after,e->hits_before,e->hits_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_bullet_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) bullet_event_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_death(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,death_event_count);return 1;}
    if(ordinal>death_event_count) {p_lua_pushnil(L);return 1;}
    death_event *e=&death_events[(unsigned)ordinal-1];char out[256];
    int n=snprintf(out,sizeof(out),"id=%u tick=%llu call=%llu reason=%u time=%016llx rng_before=%016llx rng_after=%016llx",
        e->actor,(unsigned long long)e->tick,(unsigned long long)e->call,e->reason,(unsigned long long)e->time,
        (unsigned long long)e->before,(unsigned long long)e->after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_death_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) death_event_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_mass(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,mass_event_count);return 1;}
    if(ordinal>mass_event_count) {p_lua_pushnil(L);return 1;}
    mass_event *e=&mass_events[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"id=%u which=%u tick=%llu source=%llx update_mass=%u activate=%u body_id=%08x before=",
        e->actor,e->which,(unsigned long long)e->tick,(unsigned long long)e->source,e->update_mass,e->activate,e->id);
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->before[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," after=");
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->after[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," stack=");
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%x",i?",":"",e->stack[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_mass_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) mass_event_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_interrupt(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,interrupt_count);return 1;}
    if(ordinal>interrupt_count) {p_lua_pushnil(L);return 1;}
    interrupt_sample *e=&interrupt_samples[(unsigned)ordinal-1];char out[640];
    snprintf(out,sizeof(out),"tick=%llu ordinal=%llu actor=%u kind=%u event=%x flags=%x/%x script=%s pc=%llu range=%llu/%llu mode=%u rng=%016llx/%016llx values=%x:%016llx,%x:%016llx result=%llx",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal,e->actor,e->kind,e->event,e->flags_before,e->flags_after,e->script,
        (unsigned long long)e->pc,(unsigned long long)e->begin,(unsigned long long)e->end,e->mode,
        (unsigned long long)e->rng_before,(unsigned long long)e->rng_after,e->types[0],(unsigned long long)e->values[0],e->types[1],(unsigned long long)e->values[1],(unsigned long long)e->result);
    p_lua_pushlstring(L,out,strlen(out));return 1;
}
/* gate.play_mode(): native_gate_play_mode, before the boot. */
static int lua_play_mode(lua_State *L) {gate_play_mode=1;p_lua_pushnumber(L,1);return 1;}
/* gate.npc_action_rows(): native_gate_npc_lifecycle, something drains the NPC action rows. */
static int lua_npc_action_rows(lua_State *L) {npc_action_rows=1;p_lua_pushnumber(L,1);return 1;}
static int lua_interrupt_faults(lua_State *L) {
    char out[1024];int n=accept_ui()?ifault_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_interrupt_fault_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<IFAULT_RING && floor(index)==index) n=ifault_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* economy_load_rng(): the economy schedule each container gets from the save, keyed (lockstep_economy_load.h). */
static int lua_economy_load_rng(lua_State *L) {p_lua_pushnumber(L,eco_arm());return 1;}
static int lua_economy_load_rng_status(lua_State *L) {
    char out[384];int n=accept_ui()?eco_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* build_rows(): who gets built, in the order it is decided (lockstep_build_rows.h). */
static int lua_build_rows(lua_State *L) {p_lua_pushnumber(L,br_arm());return 1;}
static int lua_build_rows_status(lua_State *L) {
    char out[384];int n=accept_ui()?br_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_build_rows_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[2048];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=br_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_flight_faults(lua_State *L) {
    char out[512];int n=accept_ui()?flight_fault_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_flight_fault_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<FLIGHT_FAULT_RING && floor(index)==index) n=flight_fault_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* What the row tables lost, from any module: never a reason to stop, always worth seeing. */
static int lua_observer_status(lua_State *L) {
    char out[384];int n=snprintf(out,sizeof(out),"play_mode=%d interrupt=%llu step=%llu nested=%llu npc=%llu radar=%llu radar_unreadable=%llu bullet=%llu death=%llu shape=%llu contacts_truncated=%llu",
        gate_play_mode,(unsigned long long)interrupt_dropped,(unsigned long long)interrupt_step_dropped,(unsigned long long)nested_action_dropped,(unsigned long long)npc_action_dropped,
        (unsigned long long)radar_dropped,(unsigned long long)radar_unreadable,(unsigned long long)bullet_dropped,(unsigned long long)death_dropped,
        (unsigned long long)shape_lifetime_dropped,(unsigned long long)gravidar_contacts_truncated);
    if(n>0 && (size_t)n<sizeof(out)) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_interrupt_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) interrupt_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_world_watch(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);
    p_lua_pushnumber(L,accept_ui() && world_hash_watch_add(text,size));return 1;
}
#include "lockstep_world_trace_lua.h"
#include "lockstep_world_queue_lua.h"
#include "lockstep_world_wait_lua.h"
#include "lockstep_world_action_lua.h"
#include "lockstep_world_values_lua.h"
#include "lockstep_world_find_rng_lua.h"
#include "lockstep_world_position_lua.h"
#include "lockstep_world_safe_position_lua.h"
#include "lockstep_world_position_rng_lua.h"
#include "lockstep_transition_lua.h"
#include "lockstep_player_origin_lua.h"
#include "lockstep_player_getup_lua.h"
#include "lockstep_pilot_inputs_lua.h"
#include "lockstep_ui_replay_lua.h"
#include "lockstep_pilot_predict_lua.h"
#include "lockstep_virtual_players_lua.h"
#include "lockstep_hit_native_lua.h"
static int lua_world_detail(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<0 || floor(number)!=number || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    unsigned count=world_hash_watch_count*WORLD_HASH_DOMAINS;
    if(number==0) {p_lua_pushnumber(L,count);return 1;}
    if(number>count) {p_lua_pushnil(L);return 1;}
    unsigned index=(unsigned)number-1,domain=index%WORLD_HASH_DOMAINS;
    const char *names[WORLD_HASH_DOMAINS]={"topology","health","motion","timing"};
    world_hash_watch *watch=&world_hash_watches[index/WORLD_HASH_DOMAINS];world_hash_data *data=&watch->data[domain];
    char out[2300];int n=snprintf(out,sizeof(out),"tick=%llu id=%016llx domain=%s bytes=",
        (unsigned long long)world_hash_tick,(unsigned long long)watch->saved,names[domain]);
    static const char digits[]="0123456789abcdef";
    if(n<0 || (size_t)n+data->length*2>=sizeof(out)) {p_lua_pushnil(L);return 1;}
    for(unsigned i=0;i<data->length;i++) {out[n++]=digits[data->data[i]>>4];out[n++]=digits[data->data[i]&15];}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_banks(lua_State *L) {
    if(GetCurrentThreadId()!=ui_thread || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    char out[160];int n=snprintf(out,sizeof(out),"tick=%llu read_before=%u write_before=%u read_after=%u write_after=%u",
        (unsigned long long)world_hash_tick,world_banks_before.read,world_banks_before.write,world_banks_after.read,world_banks_after.write);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_capture_schema(lua_State *L,unsigned schema) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !stats.active || !boot_mode || boot_phase!=3 || !world_identity_installed ||
       !isfinite(number) || number<1 || number!=(double)stats.issued || !world_hash_capture((uint64_t)number,schema)) {
        char error[128];snprintf(error,sizeof(error),"saved=%016llx stage=%s",(unsigned long long)world_hash_failure_id,world_hash_stage);
        ++world_hash_failures;p_lua_pushnil(L);p_lua_pushlstring(L,error,strlen(error));return 2;
    }
    p_lua_pushlstring(L,world_hash_summary,strlen(world_hash_summary));return 1;
}
static int lua_world_capture(lua_State *L) {return lua_world_capture_schema(L,1);}
/* native_gate_world_hash_shields = 'canonical', before the first capture (lockstep_state.h). */
static int lua_world_hash_shields(lua_State *L) {
    size_t length=0;const char *mode=p_lua_tolstring(L,1,&length);
    p_lua_pushnumber(L,mode && length==9 && !memcmp(mode,"canonical",9) && (shield_canonical || (!world_hash_scans && shield_canonical_pins())) &&
        (shield_canonical=1));return 1;
}
static int lua_world_hash_shields_status(lua_State *L) {
    char out[96];int n=snprintf(out,sizeof(out),"mode=%s canonical=%llu",shield_canonical?"canonical":"off",(unsigned long long)shield_canonical_count);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_world_capture_nav_v2(lua_State *L) {return lua_world_capture_schema(L,2);}
static int lua_world_page(lua_State *L) {
    double number=p_lua_tonumber(L,1);
    if(!accept_ui() || !isfinite(number) || number<0 || floor(number)!=number || !world_hash_scans || world_hash_failures) {p_lua_pushnil(L);return 1;}
    unsigned pages=(world_hash_changes+WORLD_HASH_PAGE-1)/WORLD_HASH_PAGE;
    if(number==0) {p_lua_pushnumber(L,pages);return 1;}
    if(number>pages) {p_lua_pushnil(L);return 1;}
    char out[12288];int n=0;unsigned begin=((unsigned)number-1)*WORLD_HASH_PAGE,end=begin+WORLD_HASH_PAGE;
    if(end>world_hash_changes) end=world_hash_changes;
    for(unsigned i=begin;i<end;i++) {
        world_hash_item *item=world_hash_item_at(world_hash_changed[i]);char hashes[WORLD_HASH_DOMAINS][65],timing[WORLD_TIMING_BYTES*2+1];
        if(item->timing_length>WORLD_TIMING_BYTES || item->nav_raw_length>WORLD_NAV_RAW_BYTES) {++world_hash_failures;p_lua_pushnil(L);return 1;}
        for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) hex32(item->hash[d],hashes[d]);
        static const char digits[]="0123456789abcdef";
        for(unsigned j=0;j<item->timing_length;j++) {timing[2*j]=digits[item->timing[j]>>4];timing[2*j+1]=digits[item->timing[j]&15];}
        timing[2*item->timing_length]=0;
        char raw[WORLD_NAV_RAW_BYTES*2+1],suffix[WORLD_NAV_RAW_BYTES*2+16];suffix[0]=0;
        if(world_hash_schema==2) {
            for(unsigned j=0;j<item->nav_raw_length;j++) {raw[2*j]=digits[item->nav_raw[j]>>4];raw[2*j+1]=digits[item->nav_raw[j]&15];}
            raw[2*item->nav_raw_length]=0;snprintf(suffix,sizeof(suffix)," nav_raw=%s",item->nav_raw_length?raw:"-");
        }
        n+=snprintf(out+n,sizeof(out)-(size_t)n,"[X4LAN] NATIVE_GATE WORLD_OBJECT tick=%llu id=%016llx type=%x flags=%x parent=%016llx topology=%s health=%s motion=%s timing=%s timing_fields=%s%s\n",
            (unsigned long long)world_hash_tick,(unsigned long long)item->saved,item->type,item->flags,(unsigned long long)item->parent,hashes[0],hashes[1],hashes[2],hashes[3],item->timing_length?timing:"-",suffix);
        if(n<0 || (size_t)n>=sizeof(out)) {++world_hash_failures;p_lua_pushnil(L);return 1;}
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
#include "lockstep_player.h"
static int lua_saved_identity(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t runtime=0;
    if(!text || !size || size>20 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<size;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) {p_lua_pushnil(L);return 1;}
        runtime=runtime*10+digit;
    }
    char out[17];snprintf(out,sizeof(out),"%016llx",(unsigned long long)world_identity_lookup(runtime));
    p_lua_pushlstring(L,out,16);return 1;
}
/* gate.created_identities(rows): arm before loading (rows 1 also keeps the
 * CREATED_ROW ring). Status and rows mirror the other census observers. */
static int lua_created_identities(lua_State *L) {
    double rows=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,(rows==0 || rows==1) && created_arm(rows==1));return 1;
}
/* gate.portable_created(): after created_identities, before loading; the
 * ordering/keying modules then use created IDs (native_gate_portable_created). */
static int lua_portable_created(lua_State *L) {p_lua_pushnumber(L,portable_created_arm());return 1;}
/* gate.created_load(): after created_identities, before loading. */
static int lua_created_load(lua_State *L) {p_lua_pushnumber(L,created_load_arm());return 1;}
/* gate.actor_rng_all(): before loading, after the boot installed the seeded
 * agent and ship modules (native_gate_actor_rng_all, lockstep_actor_all.h). */
static int actor_all_arm(void) {
    if(actor_all_armed) return stats.active && agent_installed && agent_seeded && ship_installed && ship_seeded;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !world_identity_installed ||
       !agent_installed || !agent_seeded || !ship_installed || !ship_seeded) return 0;
    QueryPerformanceCounter(&actor_all_qpc0);actor_all_tsc0=__builtin_ia32_rdtsc();
    InterlockedExchange(&actor_all_armed,1);return 1;
}
static int lua_actor_rng_all(lua_State *L) {p_lua_pushnumber(L,actor_all_arm());return 1;}
/* Scope rows: after the all-ship RNG is armed, before the first admitted frame. */
static int lua_actor_rng_rows(lua_State *L) {
    int ok=actor_all_on() && !stats.issued && GetCurrentThreadId()==ui_thread;
    if(ok && !actor_all_rows_armed) {actor_all_rows=0;InterlockedExchange(&actor_all_rows_armed,1);}
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_actor_rng_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=actor_all_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_actor_rng_all_status(lua_State *L) {
    char out[3072];int n=accept_ui()?actor_all_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* runtime_ids(base): right before LoadGame; base 0 observes (lockstep_runtime_ids.h). */
static int lua_runtime_ids(lua_State *L) {
    double base=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(base) && base>=0 && base<=RID_BASE_MAX && floor(base)==base && rid_arm((uint32_t)base));return 1;
}
/* runtime_ids_local(): right after a set runtime_ids; the view's allocations on their own lists. */
static int lua_runtime_ids_local(lua_State *L) {p_lua_pushnumber(L,accept_ui() && rid_local_arm());return 1;}
/* runtime_ids_probe(1|0): around the probe's own frame, which runs in slot 6 and is not the view. */
static int lua_runtime_ids_probe(lua_State *L) {
    if(accept_ui()) InterlockedExchange(&rid_probe_frame,p_lua_tonumber(L,1)!=0);
    return 0;
}
static int lua_runtime_ids_status(lua_State *L) {
    char out[900];int n=accept_ui()?rid_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_runtime_ids_word(lua_State *L) {
    char out[32];int n=accept_ui() && rid_mode?snprintf(out,sizeof(out),"%016llx",(unsigned long long)rid_next_id):0;
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_runtime_ids_map_row(lua_State *L) {
    char out[640];int n=accept_ui()?rid_map_row(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_runtime_ids_caller_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];   /* the widest row, with a cue name, as the Linux gate's */
    int n=accept_ui() && isfinite(index) && index>=0 && index<RID_CALLERS && floor(index)==index?rid_caller_row((unsigned)index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_created_identities_status(lua_State *L) {
    char out[1024];int n=accept_ui()?created_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_created_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=created_row_text((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
/* Runtime ID (decimal, as ConvertIDTo64Bit) -> created ID, or zeros. */
static int lua_created_identity(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t runtime=0;
    if(!text || !size || size>20 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<size;i++) {
        unsigned digit=(unsigned)(text[i]-'0');
        if(digit>9 || runtime>(UINT64_MAX-digit)/10) {p_lua_pushnil(L);return 1;}
        runtime=runtime*10+digit;
    }
    char out[17];snprintf(out,sizeof(out),"%016llx",(unsigned long long)created_lookup(runtime));
    p_lua_pushlstring(L,out,16);return 1;
}
/* Created ID (16 hex) -> live runtime ID (decimal), or nil. */
static int lua_created_runtime(lua_State *L) {
    size_t size=0;const char *text=p_lua_tolstring(L,1,&size);uint64_t id=0;
    if(!text || size!=16 || !accept_ui()) {p_lua_pushnil(L);return 1;}
    for(size_t i=0;i<16;i++) {
        unsigned digit=text[i]>='0' && text[i]<='9'?(unsigned)(text[i]-'0'):text[i]>='a' && text[i]<='f'?(unsigned)(text[i]-'a'+10):16;
        if(digit>15) {p_lua_pushnil(L);return 1;}
        id=(id<<4)|digit;
    }
    uint64_t runtime=created_runtime_of(id);
    if(!runtime) {p_lua_pushnil(L);return 1;}
    char out[21];int n=snprintf(out,sizeof(out),"%llu",(unsigned long long)runtime);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_interrupt_step(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,interrupt_step_count);return 1;}
    if(ordinal>interrupt_step_count) {p_lua_pushnil(L);return 1;}
    interrupt_step_sample *s=&interrupt_steps[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu parent=%llu actor=%u pc=%llu next=%llu type=%llx action=%llx chance=%u flags=%u/%u rng=%016llx/%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->parent,s->actor,
        (unsigned long long)s->pc,(unsigned long long)s->next,(unsigned long long)s->type,(unsigned long long)s->action,
        s->chance,s->flags_before,s->flags_after,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_interrupt_step_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) interrupt_step_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_nested_action(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,nested_action_count);return 1;}
    if(ordinal>nested_action_count) {p_lua_pushnil(L);return 1;}
    nested_action_sample *s=&nested_actions[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu interrupt=%llu step=%llu actor=%u parent=%llu depth=%u owner=%x program=%016llx pc=%llu next=%llu type=%llx action=%llx chance=%u flags=%u/%u rng=%016llx/%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->interrupt,(unsigned long long)s->step,s->actor,
        (unsigned long long)s->parent,s->depth,s->owner,(unsigned long long)s->program,
        (unsigned long long)s->pc,(unsigned long long)s->next,(unsigned long long)s->type,(unsigned long long)s->action,
        s->chance,s->flags_before,s->flags_after,(unsigned long long)s->before,(unsigned long long)s->after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_nested_action_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) nested_action_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_action(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_action_count);return 1;}
    if(ordinal>npc_action_count) {p_lua_pushnil(L);return 1;}
    npc_action_sample *record=&npc_actions[(unsigned)ordinal-1];nested_action_sample *s=&record->state;char out[1024];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu interrupt=%llu step=%llu actor=%u parent=%llu depth=%u owner=%x program=%016llx pc=%llu next=%llu type=%llx action=%llx chance=%u flags=%u/%u rng=%016llx/%016llx named=%u cue=%s",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->interrupt,(unsigned long long)s->step,s->actor,
        (unsigned long long)s->parent,s->depth,s->owner,(unsigned long long)s->program,
        (unsigned long long)s->pc,(unsigned long long)s->next,(unsigned long long)s->type,(unsigned long long)s->action,
        s->chance,s->flags_before,s->flags_after,(unsigned long long)s->before,(unsigned long long)s->after,record->named,record->cue);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_action_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_action_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_cause(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_cause_count);return 1;}
    if(ordinal>npc_cause_count) {p_lua_pushnil(L);return 1;}
    npc_cause_sample *s=&npc_causes[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu irq=%llu actor=%llu key=%016llx occurrence=%llu script=%016llx pc=%llu range=%llu/%llu event=%x mode=%u status=%u",
        (unsigned long long)s->tick,(unsigned long long)s->irq,(unsigned long long)s->actor,(unsigned long long)s->key,(unsigned long long)s->occurrence,
        (unsigned long long)s->script,(unsigned long long)s->pc,(unsigned long long)s->begin,(unsigned long long)s->end,s->event,s->mode,s->status);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_trigger(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_trigger_count);return 1;}
    if(ordinal>npc_trigger_count) {p_lua_pushnil(L);return 1;}
    npc_trigger_sample *s=&npc_triggers[(unsigned)ordinal-1];char out[1536];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu parent=%llu depth=%u status=%u legacy=%u key=%016llx cause=%016llx irq=%llu step=%llu actor=%llu parent_key=%016llx visit=%llu child=%llu action_program=%016llx action_pc=%llu type=%llx action=%llx program=%016llx pc=%llu caller=%llx rng=%016llx/%016llx cue=%s",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->parent,s->depth,s->status,s->legacy,(unsigned long long)s->key,
        (unsigned long long)s->cause,(unsigned long long)s->irq,(unsigned long long)s->step,(unsigned long long)s->actor,(unsigned long long)s->parent_key,
        (unsigned long long)s->visit,(unsigned long long)s->child,(unsigned long long)s->action_program,(unsigned long long)s->action_pc,
        (unsigned long long)s->action_type,(unsigned long long)s->action,(unsigned long long)s->program,(unsigned long long)s->pc,(unsigned long long)s->caller,
        (unsigned long long)s->before,(unsigned long long)s->after,s->cue);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_trigger_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread && !npc_trigger_current && !npc_trigger_action_current && !npc_trigger_foreign_depth &&
        !npc_provenance_current && !npc_provenance_overflow_depth;
    if(ok) {npc_cause_count=0;npc_trigger_count=0;npc_provenance_count=0;}p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_provenance(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_provenance_count);return 1;}
    if(ordinal>npc_provenance_count) {p_lua_pushnil(L);return 1;}
    npc_provenance_sample *s=&npc_provenance_samples[(unsigned)ordinal-1];char out[8192];
    const char *event=s->event==0x580?"object_signalled":s->event==0x637?"cue_signalled":"unresolved";
    const char *time=s->state==2 && s->pending==2?"action_block_time":s->state==1 && s->pending==1?"waiting_time":"state_time";
    int n=snprintf(out,sizeof(out),"tick=%llu trigger=%llu flags=%u depth=%u cue=%llu:%s state=%u/%u block=%u time=%016llx time_role=%s event=%x event_name=%s",
        (unsigned long long)s->tick,(unsigned long long)s->trigger,s->flags,s->depth,(unsigned long long)s->cue.token,s->cue.name,
        s->state,s->pending,s->block,(unsigned long long)s->time,time,s->event,event);
    const char *links[]={"parent","template","root","namespace"};
    for(unsigned i=0;i<4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n," %s=%llu:%s",links[i],(unsigned long long)s->links[i].token,s->links[i].name);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," ancestry=");
    if(!s->ancestry_count) n+=snprintf(out+n,sizeof(out)-(size_t)n,"none");
    for(unsigned i=0;i<s->ancestry_count;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%llu:%s",i?",":"",(unsigned long long)s->ancestry[i].token,s->ancestry[i].name);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," values=");
    for(unsigned i=0;i<4;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%x:%u:%016llx",i?",":"",s->values[i].tag,s->values[i].kind,(unsigned long long)s->values[i].value);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," owner_kind=%u owner_status=%u owner=%llu:%s owner_saved=%016llx owner_pc=%llu owner_type=%llx owner_action=%llx owner_caller=%llx owner_caller_raw=%llx owner_caller_source=%u",
        s->owner_kind,s->owner_status,(unsigned long long)s->owner.token,s->owner.name,(unsigned long long)s->owner_saved,
        (unsigned long long)s->owner_pc,(unsigned long long)s->owner_type,(unsigned long long)s->owner_action,(unsigned long long)s->owner_caller,
        (unsigned long long)s->owner_caller_raw,s->owner_caller_source);
    if(n<0 || n>=(int)sizeof(out)) {npc_provenance_failure();p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_pilot_watch(lua_State *L) {
    double saved=p_lua_tonumber(L,1);
    int ok=saved==(double)NPC_PILOT_SAVED && npc_pilot_configure(NPC_PILOT_SAVED);
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_pilot(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_pilot_count);return 1;}
    if(ordinal>npc_pilot_count) {p_lua_pushnil(L);return 1;}
    /* Version2 integer maxima plus bounded names require1734 bytes with NUL. */
    npc_pilot_sample *s=&npc_pilots[(unsigned)ordinal-1];char out[2048];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu end_tick=%llu kind=%u nest=%llu depth=%u enter=%llu exit=%llu context=%llu saved=%016llx script=%s after_script=%s pc=%llu/%llu pc_role=%s flags=%x/%x event=%x event_role=stored_context types=%x/%x payloads=opaque mode=%u range=%llu/%llu rng=%016llx/%016llx returned=%u after_status=%u errors=%u parent_kind=%u parent_status=%u parent_self=%u parent_saved=%016llx parent_name=%s parent_pc=%llu parent_caller=%llx parent_caller_source=%u irq_caller_raw=%llx irq_caller=%llx irq_caller_source=%u",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->end_tick,s->kind,
        (unsigned long long)s->nest,s->depth,(unsigned long long)s->enter,(unsigned long long)s->exit,
        (unsigned long long)s->context,(unsigned long long)NPC_PILOT_SAVED,s->script,s->script_after,
        (unsigned long long)s->pc_before,(unsigned long long)s->pc_after,s->kind==3?"stored_context":"execution",
        s->flags_before,s->flags_after,s->event,s->types[0],s->types[1],s->mode,
        (unsigned long long)s->begin,(unsigned long long)s->end,(unsigned long long)s->rng_before,(unsigned long long)s->rng_after,
        s->returned,s->after_status,s->errors,s->parent_kind,s->parent_status,s->parent_self,(unsigned long long)s->parent_saved,s->parent_name,
        (unsigned long long)s->parent_pc,(unsigned long long)s->parent_caller,s->parent_caller_source,
        (unsigned long long)s->irq_origin.raw,(unsigned long long)s->irq_origin.caller,s->irq_origin.source);
    if(n<0 || n>=(int)sizeof(out)) {npc_pilot_failure();p_lua_pushnil(L);return 1;}
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_pilot_clear(lua_State *L) {
    p_lua_pushnumber(L,npc_pilot_clear());return 1;
}
static int lua_npc_exec(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_exec_count);return 1;}
    if(ordinal>npc_exec_count) {p_lua_pushnil(L);return 1;}
    npc_exec_sample *s=&npc_exec_samples[(unsigned)ordinal-1];char out[1024];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu parent=%llu depth=%u key=%016llx call=%llu program=%016llx pc=%llu before=%016llx scoped=%016llx after=%016llx restored=%016llx cue=%s",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->parent,s->depth,
        (unsigned long long)s->key,(unsigned long long)s->call,(unsigned long long)s->program,(unsigned long long)s->pc,
        (unsigned long long)s->before,(unsigned long long)s->scoped,(unsigned long long)s->after,(unsigned long long)s->restored,s->cue);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_exec_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_exec_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_speak(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_speak_count);return 1;}
    if(ordinal>npc_speak_count) {p_lua_pushnil(L);return 1;}
    npc_speak_sample *s=&npc_speaks[(unsigned)ordinal-1];char out[1024];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu action=%llu exec=%llu saved=%016llx entity=%016llx caller=%llx clock=%016llx state=%u/%u handle=%x/%x page=%u/%u line=%u/%u duration=%016llx/%016llx start=%016llx/%016llx rng=%016llx/%016llx enqueue=%llu origin=%llu",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->action,(unsigned long long)s->exec,
        (unsigned long long)s->saved,(unsigned long long)s->entity,(unsigned long long)s->caller,(unsigned long long)s->clock,
        s->a.state,s->b.state,s->a.handle,s->b.handle,s->a.page,s->b.page,s->a.line,s->b.line,
        (unsigned long long)s->a.duration,(unsigned long long)s->b.duration,(unsigned long long)s->a.start,(unsigned long long)s->b.start,
        (unsigned long long)s->before,(unsigned long long)s->after,(unsigned long long)s->enqueue,(unsigned long long)s->origin);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_speak_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_speak_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_enqueue(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_enqueue_count);return 1;}
    if(ordinal>npc_enqueue_count) {p_lua_pushnil(L);return 1;}
    npc_enqueue_sample *s=&npc_enqueues[(unsigned)ordinal-1];char out[2048];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu action=%llu exec=%llu saved=%016llx priority=%d interrupt=%u recipients=%016llx/%016llx flags=%x count=%u result=%u rng=%016llx/%016llx lines=",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->action,(unsigned long long)s->exec,
        (unsigned long long)s->saved,s->priority,s->interrupt,(unsigned long long)s->recipients[0],(unsigned long long)s->recipients[1],
        s->flags,s->count,s->result,(unsigned long long)s->before,(unsigned long long)s->after);
    if(!s->count) out[n++]='-';
    for(unsigned i=0;i<s->count;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%08x/%08x",i?",":"",s->lines[i][0],s->lines[i][1]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_enqueue_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_enqueue_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_finish(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_finish_count);return 1;}
    if(ordinal>npc_finish_count) {p_lua_pushnil(L);return 1;}
    npc_finish_sample *s=&npc_finishes[(unsigned)ordinal-1];char out[1024];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu enqueue=%llu origin=%llu saved=%016llx caller=%llx interrupted=%u audio=%u state=%u handle=%x page=%u line=%u duration=%016llx start=%016llx clock=%016llx flags=%u/%u/%u rng=%016llx/%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->enqueue,(unsigned long long)s->origin,
        (unsigned long long)s->saved,(unsigned long long)s->caller,s->interrupted,s->audio,s->state.state,s->state.handle,s->state.page,s->state.line,
        (unsigned long long)s->state.duration,(unsigned long long)s->state.start,(unsigned long long)s->clock,s->flags[0],s->flags[1],s->flags[2],
        (unsigned long long)s->before,(unsigned long long)s->after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_finish_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_finish_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_ready(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_ready_count);return 1;}
    if(ordinal>npc_ready_count) {p_lua_pushnil(L);return 1;}
    npc_ready_sample *s=&npc_ready_samples[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu enqueue=%llu origin=%llu saved=%016llx start_event=%llu generation=%llu page=%u line=%u duration=%016llx estimate=%016llx start=%016llx clock=%016llx repeat=%u",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->enqueue,(unsigned long long)s->origin,(unsigned long long)s->saved,
        (unsigned long long)s->start_event,(unsigned long long)s->generation,s->page,s->line,(unsigned long long)s->duration,(unsigned long long)s->estimate,
        (unsigned long long)s->start,(unsigned long long)s->clock,s->repeat);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_npc_ready_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_ready_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_npc_deadline(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,npc_deadline_count);return 1;}
    if(ordinal>npc_deadline_count) {p_lua_pushnil(L);return 1;}
    npc_deadline_sample *s=&npc_deadlines[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu enqueue=%llu origin=%llu saved=%016llx page=%u line=%u duration=%016llx start=%016llx clock=%016llx early=%u",
        (unsigned long long)s->tick,(unsigned long long)s->ordinal,(unsigned long long)s->enqueue,(unsigned long long)s->origin,(unsigned long long)s->saved,
        s->page,s->line,(unsigned long long)s->duration,(unsigned long long)s->start,(unsigned long long)s->clock,s->early);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_speech_time(lua_State *L) {p_lua_pushnumber(L,speech_time_arm());return 1;}
static int lua_speech_time_status(lua_State *L) {
    char out[384];int n=accept_ui()?speech_time_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_speech_time_row(lua_State *L) {
    uint64_t index;char out[384];int n=accept_ui() && lua_index_arg(L,1,&index)?speech_time_row_text(index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_npc_deadline_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) npc_deadline_count=0;p_lua_pushnumber(L,ok);return 1;
}
static int lua_contact(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,contact_event_count);return 1;}
    if(ordinal>contact_event_count) {p_lua_pushnil(L);return 1;}
    contact_event *e=&contact_events[(unsigned)ordinal-1];char out[8192];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu added=%u count=%u subshape=%08x,%08x base=",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal,e->added,e->count,e->subshape[0],e->subshape[1]);
    for(unsigned i=0;i<3;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->base[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," normal=");
    for(unsigned i=0;i<3;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->normal[i]);
    n+=snprintf(out+n,sizeof(out)-(size_t)n," depth=%08x",e->depth);
    for(unsigned j=0;j<2;j++) {
        contact_body *b=&e->bodies[j];
        n+=snprintf(out+n,sizeof(out)-(size_t)n," shape_due%u=%016llx",j,(unsigned long long)b->shape_due);
        n+=snprintf(out+n,sizeof(out)-(size_t)n," record%u=%u",j,b->record_kind);
        n+=snprintf(out+n,sizeof(out)-(size_t)n," body%u=%u:%08x:%llx:%llx:%llx:",j,b->actor,b->body_id,
            (unsigned long long)b->component_id,(unsigned long long)b->component_type,(unsigned long long)b->shape_type);
        for(unsigned i=0;i<8;i++) if(i!=3) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",b->pose[i]);
        n+=snprintf(out+n,sizeof(out)-(size_t)n,":");
        for(unsigned i=0;i<8;i++) if(i!=3 && i!=7) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",b->velocity[i]);
        n+=snprintf(out+n,sizeof(out)-(size_t)n," points%u=",j);
        for(unsigned point=0;point<e->count;point++) for(unsigned k=0;k<3;k++)
            n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->points[j][point][k]);
        n+=snprintf(out+n,sizeof(out)-(size_t)n," settings%u=",j);
        for(unsigned i=0;i<6;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->settings[j][i]);
        n+=snprintf(out+n,sizeof(out)-(size_t)n,":%02x:",((unsigned char*)e->settings[j])[0x18]);
        for(unsigned i=8;i<15;i++) if(i!=11) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x",e->settings[j][i]);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_contact_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) {contact_event_count=0;contact_properties_count=0;}
    p_lua_pushnumber(L,ok);return 1;
}
/* Process-local active/island indices explain traversal order. They are kept
 * in a separate trace so physical contact comparison remains unchanged. */
static int lua_contact_order(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<1 ||
       floor(ordinal)!=ordinal || ordinal>contact_event_count) {p_lua_pushnil(L);return 1;}
    contact_event *e=&contact_events[(unsigned)ordinal-1];char out[256];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal);
    for(unsigned i=0;i<2;i++) {
        contact_body *b=&e->bodies[i];
        n+=snprintf(out+n,sizeof(out)-(size_t)n," body%u=%08x:%08x:%u:%04x:%02x:%02x:%02x:%02x",
            i,b->active_index,b->island_index,b->scene,b->layer,b->body_type,
            b->broadphase_layer,b->motion_type,b->flags);
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_contact_properties(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 ||
       floor(ordinal)!=ordinal || ordinal>contact_properties_count) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,contact_properties_count);return 1;}
    contact_properties_event *e=&contact_properties_events[(unsigned)ordinal-1];char out[768];
    int n=snprintf(out,sizeof(out),"tick=%llu ordinal=%llu id=%u",
        (unsigned long long)e->tick,(unsigned long long)e->ordinal,e->actor);
    for(unsigned stage=0;stage<2;stage++) for(unsigned i=0;i<2;i++) {
        contact_properties_body *s=stage?&e->after[i]:&e->before[i];contact_body *b=&s->body;
        n+=snprintf(out+n,sizeof(out)-(size_t)n," %s%u=",stage?"after":"before",i);
        if(!s->present) n+=snprintf(out+n,sizeof(out)-(size_t)n,"none");
        else n+=snprintf(out+n,sizeof(out)-(size_t)n,"%08x:%08x:%08x:%u:%04x:%02x:%02x:%08x:%08x:%02x",
            b->body_id,b->active_index,b->island_index,b->scene,b->layer,b->motion_type,s->quality,s->group,s->filter,b->flags);
    }
    n+=snprintf(out+n,sizeof(out)-(size_t)n," stack=");
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%x",i?",":"",e->stack[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_damage(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,damage_event_count);return 1;}
    if(ordinal>damage_event_count) {p_lua_pushnil(L);return 1;}
    damage_event *e=&damage_events[(unsigned)ordinal-1];char out[512];
    int n=snprintf(out,sizeof(out),"id=%u tick=%llu ordinal=%llu time=%016llx amount=%016llx absorbed=%016llx priority=%d before=%016llx,%016llx after=%016llx,%016llx stack=",
        e->actor,(unsigned long long)e->tick,(unsigned long long)e->ordinal,(unsigned long long)e->time,
        (unsigned long long)e->amount,(unsigned long long)e->absorbed,e->priority,(unsigned long long)e->before[0],
        (unsigned long long)e->before[1],(unsigned long long)e->after[0],(unsigned long long)e->after[1]);
    for(unsigned i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-(size_t)n,"%s%x",i?",":"",e->stack[i]);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_damage_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) damage_event_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_gravidar(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if((!gravidar_installed && !gravidar_restored) || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    gravidar_sample *s=&gravidar_samples[(unsigned)actor-1];char out[384];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu time=%016llx before=%016llx,%016llx,%016llx after=%016llx,%016llx,%016llx rng_before=%016llx rng_after=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,
        (unsigned long long)s->before[0],(unsigned long long)s->before[1],(unsigned long long)s->before[2],
        (unsigned long long)s->after[0],(unsigned long long)s->after[1],(unsigned long long)s->after[2],
        (unsigned long long)s->rng_before,(unsigned long long)s->rng_after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_gravidar_contacts(lua_State *L) {
    double actor=p_lua_tonumber(L,1),stage=p_lua_tonumber(L,2);
    if((!gravidar_installed && !gravidar_restored) || GetCurrentThreadId()!=ui_thread ||
       !isfinite(actor) || !actor_number_is_ship(actor) ||
       !isfinite(stage) || stage<1 || stage>8 || floor(stage)!=stage) {p_lua_pushnil(L);return 1;}
    gravidar_sample *s=&gravidar_samples[(unsigned)actor-1];
    gravidar_contacts *v=&s->contacts[(unsigned)stage-1];char out[8192];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu stage=%u present=%u count=%u ids=",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned)stage,v->present,v->count);
    if(!v->count) out[n++]='-';
    for(unsigned i=0;i<v->count;i++) {
        unsigned mapped=0;
        for(unsigned j=0;j<ACTOR_CAPACITY;j++) if(actor_ids[j] && actor_ids[j]==v->ids[i]) {mapped=j+1;break;}
        int used=snprintf(out+n,sizeof(out)-(size_t)n,"%s%016llx/%u",i?",":"",(unsigned long long)v->ids[i],mapped);
        if(used<0 || (size_t)used>=sizeof(out)-(size_t)n) {p_lua_pushnil(L);return 1;}n+=used;
    }
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_radar(lua_State *L) {
    double ordinal=p_lua_tonumber(L,1);
    if(GetCurrentThreadId()!=ui_thread || !isfinite(ordinal) || ordinal<0 || floor(ordinal)!=ordinal) {p_lua_pushnil(L);return 1;}
    if(ordinal==0) {p_lua_pushnumber(L,radar_event_count);return 1;}
    if(ordinal>radar_event_count) {p_lua_pushnil(L);return 1;}
    radar_event *e=&radar_events[(unsigned)ordinal-1];char out[256];
    int n=snprintf(out,sizeof(out),"id=%u tick=%llu ordinal=%llu source=%u flags=%u script=%s",
        e->actor,(unsigned long long)e->tick,(unsigned long long)e->ordinal,e->source,e->flags,e->script);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
static int lua_radar_clear(lua_State *L) {
    int ok=GetCurrentThreadId()==ui_thread;if(ok) radar_event_count=0;
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_attention(lua_State *L) {
    double actor=p_lua_tonumber(L,1);
    if(!attention_installed || GetCurrentThreadId()!=ui_thread || !isfinite(actor) || !actor_number_is_ship(actor)) {p_lua_pushnil(L);return 1;}
    attention_sample *s=&attention_samples[(unsigned)actor-1];char out[384];
    int n=snprintf(out,sizeof(out),"tick=%llu calls=%llu time=%016llx level_before=%u level_after=%u due_before=%016llx due_after=%016llx initialized=%u rng_before=%016llx rng_after=%016llx",
        (unsigned long long)s->tick,(unsigned long long)s->calls,(unsigned long long)s->time,s->level_before,s->level_after,
        (unsigned long long)s->due_before,(unsigned long long)s->due_after,s->initialized,(unsigned long long)s->before,(unsigned long long)s->after);
    p_lua_pushlstring(L,out,(size_t)n);return 1;
}
#include "lockstep_portable_windows_lua.h"
#include "lockstep_rotation_win_lua.h"
__declspec(dllexport) int luaopen_x4lan_gate(lua_State *L) {
    HMODULE runtime=GetModuleHandleW(L"lua51_64.dll");if(!runtime) return 0;
#define RESOLVE(name) p_##name=(void*)GetProcAddress(runtime,#name);if(!p_##name) return 0
    RESOLVE(lua_tonumber);RESOLVE(lua_tolstring);RESOLVE(lua_pushnumber);RESOLVE(lua_pushnil);RESOLVE(lua_pushlstring);
    RESOLVE(lua_createtable);RESOLVE(lua_setfield);RESOLVE(lua_pushcclosure);
#undef RESOLVE
    p_lua_rawseti=(void*)GetProcAddress(runtime,"lua_rawseti");
    p_lua_createtable(L,0,4);
#define METHOD(name) p_lua_pushcclosure(L,lua_##name,0);p_lua_setfield(L,-2,#name)
    METHOD(sha256);
    METHOD(start);METHOD(report);METHOD(poll);METHOD(stop);METHOD(actor);METHOD(boot);METHOD(load_requested);METHOD(frame_timing);METHOD(qpc_us);METHOD(timer_resolution);METHOD(timer_resolution_status);METHOD(boot_rng_anchor);METHOD(boot_rng_anchor_status);METHOD(path_determinism);METHOD(path_determinism_status);METHOD(travel_determinism);METHOD(travel_determinism_status);METHOD(ai_schedule);METHOD(ai_schedule_status);METHOD(ai_schedule_fallbacks);METHOD(thread_rng);METHOD(event_order);METHOD(event_order_status);METHOD(event_rows);METHOD(event_order_row);METHOD(script_rng);METHOD(script_rng_status);METHOD(script_rng_row);METHOD(md_timed_rng);METHOD(md_timed_rng_status);METHOD(boot_hold);METHOD(boot_hold_status);METHOD(md_resume_rng);METHOD(md_resume_rng_status);METHOD(md_resume_rng_row);METHOD(ai_wait_rng);METHOD(ai_wait_rng_status);METHOD(ai_wait_rng_row);METHOD(gravidar_key);METHOD(gravidar_time);METHOD(gravidar_key_status);METHOD(gravidar_watch);METHOD(gravidar_watch_row);METHOD(gravidar_watch_set);METHOD(gravidar_watch_status);METHOD(gravidar_posts);METHOD(gravidar_posts_status);METHOD(gravidar_post_row);METHOD(gravidar_wide);METHOD(gravidar_wide_status);METHOD(job_rng);METHOD(job_rng_status);METHOD(job_rng_row);METHOD(flight);METHOD(avoidance);METHOD(ai);METHOD(agent);METHOD(ship);METHOD(travel_snapshot);METHOD(travel_snapshot_status);METHOD(event_rng);METHOD(event_rng_status);METHOD(event_rng_row);
    METHOD(render_rng_split);METHOD(render_rng_split_status);METHOD(camera_perturb);METHOD(camera_perturb_status);
    METHOD(bullet_cadence);METHOD(bullet_cadence_status);METHOD(speak_timer);METHOD(speak_timer_status);METHOD(world_hash_shields);METHOD(world_hash_shields_status);METHOD(nav_cadence);METHOD(nav_cadence_status);METHOD(nav_cadence_row);METHOD(nav_cadence_watch);METHOD(nav_probe_init);METHOD(draw_watch_add);METHOD(draw_watch_frame);METHOD(draw_watch_frame_end);METHOD(draw_watch);METHOD(draw_watch_status);METHOD(draw_watch_row);METHOD(nav_params_init);METHOD(nav_player);METHOD(nav_player_status);METHOD(command_order);METHOD(command_order_status);METHOD(command_order_row);METHOD(attention_all);METHOD(attention_all_status);METHOD(command_order_rows);METHOD(ship_sound_rng);METHOD(ship_sound_rng_status);METHOD(draw_watch_stray);METHOD(player_timer);METHOD(player_timer_status);METHOD(warp_rng);METHOD(warp_rng_status);METHOD(warp_rng_row);METHOD(transition_rows);METHOD(transition_rows_status);METHOD(transition_row);METHOD(tempzone_order);METHOD(tempzone_order_status);METHOD(tempzone_order_row);
    METHOD(view_points);METHOD(view_points_frame);METHOD(view_points_status);METHOD(view_points_rows);METHOD(view_points_row);METHOD(view_points_triggers);METHOD(view_points_source);METHOD(region_camera);METHOD(region_camera_frame);METHOD(region_camera_uncovered);METHOD(region_camera_standins);METHOD(region_camera_status);METHOD(region_camera_effect_row);METHOD(region_camera_policy_rows);METHOD(region_camera_policy_row);METHOD(region_camera_highway_rows);METHOD(region_camera_highway_row);METHOD(weapon_fire_rng);METHOD(weapon_fire_rng_status);METHOD(motion_watch);METHOD(motion_watch_frame);METHOD(motion_watch_row);METHOD(motion_watch_status);METHOD(motion_watch_orders);METHOD(motion_watch_order_row);METHOD(motion_watch_target_row);METHOD(motion_watch_event_row);METHOD(motion_watch_refresh_row);METHOD(engine_refresh_rng);METHOD(engine_refresh_rng_status);METHOD(build_rng);METHOD(build_rng_status);METHOD(ui_cargo);METHOD(ui_cargo_status);METHOD(ui_cargo_row);METHOD(supply_rows);METHOD(supply_rows_status);METHOD(supply_row);METHOD(ui_lazy);METHOD(ui_lazy_status);METHOD(ui_lazy_row);METHOD(portable_math);METHOD(portable_math_status);METHOD(exact_reciprocal);METHOD(exact_reciprocal_status);METHOD(fp_env);METHOD(fp_env_status);METHOD(decimal);METHOD(decimal_status);METHOD(supply_wants);METHOD(supply_wants_status);METHOD(supply_wants_row);METHOD(macro_arena);METHOD(macro_arena_status);METHOD(macro_arena_row);METHOD(pick_order);METHOD(pick_order_status);METHOD(pick_order_row);METHOD(order_canon);METHOD(order_canon_status);METHOD(order_canon_row);METHOD(address_order);METHOD(address_order_status);METHOD(address_order_row);METHOD(loadout_order);METHOD(loadout_order_status);METHOD(loadout_order_row);METHOD(step_trace);METHOD(step_trace_arm);METHOD(step_trace_status);METHOD(step_trace_row);METHOD(step_chain);METHOD(fp_redirect);METHOD(fp_redirect_watch);METHOD(fp_redirect_status);METHOD(fp_redirect_row);METHOD(coverage);METHOD(coverage_frame);METHOD(coverage_status);METHOD(coverage_row);METHOD(runtime_ids);METHOD(runtime_ids_local);METHOD(runtime_ids_probe);METHOD(runtime_ids_status);METHOD(runtime_ids_word);METHOD(runtime_ids_map_row);METHOD(runtime_ids_caller_row);
    METHOD(physics_trace);METHOD(physics_trace_status);METHOD(physics_trace_row);METHOD(body_order);METHOD(body_order_status);METHOD(body_order_row);METHOD(physics_trace_children);METHOD(physics_trace_saved);
    METHOD(shape_order);METHOD(shape_order_status);METHOD(shape_order_row);
    METHOD(nav_octree);METHOD(nav_octree_status);METHOD(nav_octree_row);METHOD(nav_octree_report);METHOD(nav_octree_agree);METHOD(nav_octree_lookup_row);METHOD(nav_octree_sites);METHOD(nav_octree_sites_status);METHOD(nav_octree_site_row);
    METHOD(camera_offset);METHOD(camera_offset_status);METHOD(camera_anchor);METHOD(camera_anchor_keys);METHOD(camera_anchor_status);
    METHOD(mass_traffic);METHOD(mass_traffic_status);METHOD(region_pass);METHOD(region_pass_status);METHOD(gravidar_digest);METHOD(gravidar_digest_frame);METHOD(gravidar_digest_row);
    METHOD(gravidar_deadlines);METHOD(gravidar_deadlines_status);
    METHOD(economy_load_rng);METHOD(economy_load_rng_status);METHOD(build_rows);METHOD(build_rows_status);METHOD(build_rows_row);METHOD(flight_faults);METHOD(flight_fault_row);METHOD(play_mode);METHOD(observer_status);METHOD(npc_action_rows);METHOD(interrupt_faults);METHOD(interrupt_fault_row);METHOD(wire);METHOD(propose);METHOD(relay_host);METHOD(lookahead);METHOD(lookahead_status);METHOD(tick_rate);METHOD(fast_reads);METHOD(path_budget);METHOD(path_budget_status);METHOD(pass_stride);
    METHOD(find_order);METHOD(find_order_status);METHOD(find_order_row);
    METHOD(region_rng);METHOD(region_rng_status);METHOD(region_rng_row);METHOD(spawn_rng);METHOD(spawn_rng_status);METHOD(spawn_rng_row);
    METHOD(register_weapon);METHOD(weapon);
    METHOD(params);METHOD(spline);
    METHOD(nav);
    METHOD(radar);METHOD(radar_clear);
    METHOD(gravidar);
    METHOD(gravidar_contacts);
    METHOD(damage);METHOD(damage_clear);
    METHOD(contact);METHOD(contact_clear);METHOD(contact_order);METHOD(contact_properties);
    METHOD(register_dock_collider);
    METHOD(interrupt);METHOD(interrupt_clear);
    METHOD(interrupt_step);METHOD(interrupt_step_clear);
    METHOD(nested_action);METHOD(nested_action_clear);
    METHOD(npc_action);METHOD(npc_action_clear);
    METHOD(npc_exec);METHOD(npc_exec_clear);
    METHOD(npc_cause);METHOD(npc_trigger);METHOD(npc_trigger_clear);
    METHOD(npc_provenance);
    METHOD(npc_pilot_watch);METHOD(npc_pilot);METHOD(npc_pilot_clear);
    METHOD(npc_speak);METHOD(npc_speak_clear);
    METHOD(npc_enqueue);METHOD(npc_enqueue_clear);
    METHOD(npc_finish);METHOD(npc_finish_clear);
    METHOD(npc_ready);METHOD(npc_ready_clear);
    METHOD(npc_deadline);METHOD(npc_deadline_clear);METHOD(speech_time);METHOD(speech_time_status);METHOD(speech_time_row);
    METHOD(saved_identity);
    METHOD(created_identities);METHOD(created_load);METHOD(created_identities_status);METHOD(created_row);METHOD(created_identity);METHOD(created_runtime);
    METHOD(portable_created);METHOD(actor_rng_all);METHOD(actor_rng_all_status);METHOD(actor_rng_rows);METHOD(actor_rng_row);
    METHOD(snapshot_fixture);METHOD(snapshot_actor);METHOD(snapshot_enroll);
    METHOD(world_capture);METHOD(world_capture_nav_v2);METHOD(world_page);METHOD(world_watch);METHOD(world_detail);METHOD(world_banks);
    METHOD(world_trace);METHOD(world_trace_status);METHOD(world_trace_row);METHOD(world_trace_config);
    METHOD(world_queue_trace_config);METHOD(world_queue_trace_status);METHOD(world_queue_trace_row);
    METHOD(world_wait_trace_config);METHOD(world_wait_trace_status);METHOD(world_wait_trace_row);
    METHOD(world_wait_rng_config);METHOD(world_wait_rng_status);METHOD(world_wait_rng_row);
    METHOD(world_action_trace_config);METHOD(world_action_trace_status);METHOD(world_action_trace_row);
    METHOD(world_values_config);METHOD(world_values_cursors);METHOD(world_values_status);METHOD(world_values_row);METHOD(world_values_unknown_row);METHOD(world_values_unknown_identity_row);METHOD(world_values_unknown_identity_status);
    METHOD(world_find_rng_config);METHOD(world_find_rng_status);METHOD(world_find_rng_row);
    METHOD(world_safe_position_config);METHOD(world_safe_position_cursor);METHOD(world_safe_position_status);METHOD(world_safe_position_row);
    METHOD(world_position_config);METHOD(world_position_cursor);METHOD(world_position_status);METHOD(world_position_row);
    METHOD(world_position_rng_config);METHOD(world_position_rng_cursor);METHOD(world_position_rng_status);METHOD(world_position_rng_row);
    METHOD(player_transition_config);METHOD(player_transition_status);METHOD(player_transition_row);
    METHOD(player_transition_watch_config);METHOD(player_transition_ticket);METHOD(player_transition_watch);
    METHOD(player_transition_queue_evidence_config);METHOD(player_transition_queue_evidence_status);METHOD(player_transition_queue_evidence_row);
    METHOD(player_transition_prebind_config);METHOD(player_transition_prebind_status);METHOD(player_transition_prebind_row);
    METHOD(player_origin_config);METHOD(player_origin_status);METHOD(player_origin_ticket);METHOD(player_origin_edge);
    METHOD(player_getup_config);METHOD(player_getup_batch);METHOD(player_getup_state);METHOD(player_getup_status);METHOD(player_getup_diagnostic_status);
    METHOD(portable_config);METHOD(portable_ready);METHOD(portable_status);METHOD(portable_frame);
    METHOD(portable_rotation_config);METHOD(portable_rotation_ready);METHOD(portable_rotation_status);
    METHOD(portable_rotation_rejection_enable);METHOD(portable_rotation_rejection);
    METHOD(portable_transport);METHOD(portable_cancel);
    METHOD(player_controls);METHOD(player_throttle);
    METHOD(player_observe_enable);METHOD(player_observe);
    METHOD(player_solver_enable);METHOD(player_solver_event);
    METHOD(player_request_enable);METHOD(player_request_event);METHOD(player_request_scope);
    METHOD(player_fire_enable);METHOD(player_trigger);METHOD(player_target);METHOD(player_fire_state);METHOD(player_fire_event);METHOD(player_trigger_cleanup);
    METHOD(player_launch_enable);METHOD(player_launch_event);
    METHOD(pilot_inputs);METHOD(pilot_inputs_status);METHOD(pilot_inputs_frame);METHOD(pilot_capture);METHOD(pilot_apply);METHOD(pilot_pending);
    METHOD(pilot_device);METHOD(pilot_camera_follow);METHOD(pilot_camera);METHOD(pilot_camera_status);METHOD(pilot_ship);METHOD(pilot_native_capture);
    METHOD(pilot_native);METHOD(pilot_native_refusals);METHOD(pilot_stock);METHOD(pilot_stock_pins);METHOD(pilot_stock_callers);METHOD(pilot_present);METHOD(pilot_present_pause);METHOD(pilot_present_resume);METHOD(pilot_present_diff);METHOD(pilot_death);METHOD(pilot_death_frame);METHOD(pilot_death_ship);METHOD(pilot_death_suit);METHOD(crash_note);METHOD(cutscene_ready);METHOD(cutscene_ready_status);METHOD(cutscene_ready_row);METHOD(pilot_death_status);METHOD(pilot_stock_capture);METHOD(pilot_stock_apply);METHOD(pilot_stock_inject);METHOD(pilot_stock_frame);
    METHOD(virtual_players);METHOD(virtual_players_status);METHOD(virtual_players_regions);METHOD(virtual_players_undeclared);METHOD(virtual_players_map);METHOD(virtual_players_hashes);METHOD(virtual_players_raw);METHOD(virtual_players_neighbours);
    METHOD(ui_replay);METHOD(ui_replay_pins);METHOD(ui_replay_take);METHOD(ui_replay_stage);METHOD(ui_replay_push);METHOD(ui_replay_ref);METHOD(ui_replay_resolve);METHOD(ui_replay_lua);METHOD(ui_replay_bypass);METHOD(ui_replay_result);METHOD(ui_replay_frame);METHOD(ui_replay_status);METHOD(ui_replay_refusals);METHOD(ui_replay_fail);METHOD(ui_replay_saved);
    METHOD(pilot_predict);METHOD(pilot_predict_frame);METHOD(pilot_predict_status);METHOD(pilot_predict_raw);
    METHOD(hit_claims);METHOD(hit_claims_admit);METHOD(hit_claims_resolve);METHOD(hit_claims_proposal);METHOD(hit_claims_status);
    METHOD(input_mux);METHOD(input_split);METHOD(pilot_fire);METHOD(pilot_fire_frame);METHOD(pilot_fire_guns);METHOD(pilot_fire_status);
    METHOD(ship_reserve);METHOD(ship_bind);METHOD(ship_status);METHOD(ship_raw_motion);
    METHOD(registry_md);METHOD(registry_update);METHOD(registry_origin);METHOD(registry_origin_identity);
    METHOD(creation_trace_config);METHOD(creation_trace_status);METHOD(creation_trace_row);
    METHOD(creation_rng_config);METHOD(creation_rng_status);METHOD(creation_rng_row);METHOD(creation_rng_ack_status);
    METHOD(registry_creation_config);METHOD(registry_creation_status);METHOD(registry_creation_identity);METHOD(registry_creation_operation);
    METHOD(velocity);
    METHOD(body);
    METHOD(shape);METHOD(bullet);METHOD(bullet_clear);METHOD(death);METHOD(death_clear);METHOD(mass);METHOD(mass_clear);METHOD(attention);
    METHOD(shape_lifetime);METHOD(shape_lifetime_clear);
#undef METHOD
    return 1;
}
