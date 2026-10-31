/* Production NGD transport, owned synthetic code/data and a real Python UDP
 * coordinator. The synthetic frame callback is not a native gameplay proof. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
static int WSAAPI gd_test_recv(SOCKET,char *,int,int);
static int WSAAPI gd_test_close(SOCKET);
static void WINAPI gd_test_unlock(PSRWLOCK);
static void WINAPI gd_test_read_lock(PSRWLOCK);
#define recv gd_test_recv
#define closesocket gd_test_close
#define ReleaseSRWLockExclusive gd_test_unlock
#define AcquireSRWLockShared gd_test_read_lock
#define main unused_decoder_fixture_entry
#include "test_lockstep_portable_windows.c"
#undef main
#undef recv
#undef closesocket
#undef ReleaseSRWLockExclusive
#undef AcquireSRWLockShared
#include <stdlib.h>
static unsigned test_case,test_injected,test_closes,test_originals,test_actions;
static HANDLE stop_worker,stop_deactivated,stop_restore;
static unsigned stop_paused,claimed_stop,wait_asserted;
static unsigned char test_table[0x200];
static const char test_run[]="3ba3f5f43b92602683c19aee62a20342b084dd5971ddd33808d81a328879a547";
static int WSAAPI gd_test_close(SOCKET s) {++test_closes;return closesocket(s);}
static DWORD WINAPI test_stop_worker(void *unused) {(void)unused;finish(1);return 0;}
static void start_stopped_worker(void) {
    stop_deactivated=CreateEventW(NULL,TRUE,FALSE,NULL);stop_restore=CreateEventW(NULL,TRUE,FALSE,NULL);assert(stop_deactivated && stop_restore);
    stop_worker=CreateThread(NULL,0,test_stop_worker,NULL,0,NULL);assert(stop_worker);
    assert(WaitForSingleObject(stop_deactivated,3000)==WAIT_OBJECT_0);
}
static void WINAPI gd_test_unlock(PSRWLOCK lock) {
    int pause=lock==&portable_wire_lock && portable_wire_finish_started && !stats.active && !stop_paused && (test_case==12 || test_case==13);
    if(pause) stop_paused=1;
    ReleaseSRWLockExclusive(lock);
    if(pause) {assert(SetEvent(stop_deactivated));assert(WaitForSingleObject(stop_restore,3000)==WAIT_OBJECT_0);}
    if(test_case==13 && lock==&portable_wire_lock && gd_calls==1 && stats.active && !claimed_stop) {
        claimed_stop=1;start_stopped_worker();assert(gd_calls==1 && !portable_wire_restore_complete);
    }
    if(test_case==14 && lock==&portable_wire_lock && portable_wire_finish_started && !stats.active && !test_injected) {
        test_injected=1;*(uint64_t*)(image+WORD_RVA)=123456;
    }
}
static void WINAPI gd_test_read_lock(PSRWLOCK lock) {
    if(test_case==12 && lock==&portable_wire_restore_lock && stop_paused && !wait_asserted) {
        wait_asserted=1;
        assert(gd_calls==0 && !portable_wire_restore_complete);assert(SetEvent(stop_restore));
    }
    AcquireSRWLockShared(lock);
}
static int WSAAPI gd_test_recv(SOCKET s,char *p,int size,int flags) {
    int n=recv(s,p,size,flags);
    if(n>0 && !test_injected && n>6 && !memcmp(p,"NGDGO|",6)) {
        if(test_case!=14) test_injected=1;
        if(test_case==1) *(double*)(image+GAME_RVA)+=1;
        if(test_case==2) *(double*)(image+REAL_RVA)+=1;
        if(test_case==3) digest[0]='c';
        if(test_case==4) *(uint64_t*)(image+WORLD_QUEUE_ENQUEUE)^=1;
        if(test_case==5) finish(1);
        if(test_case==12) start_stopped_worker();
        if(test_case==15) *(void**)(image+SPLINE_ENTRY)=image+SPLINE_UPDATE;
    }
    return n;
}
static int test_guard(void *unused,uint64_t tick,int action) {
    (void)unused;return stats.active && tick==stats.issued && stats.report_tick+1==tick &&
        pgetup_words(tick,inputs[0],inputs[1]) && action==(tick==30);
}
static bool test_action(void) {++test_actions;assert(test_actions==1 && getup_request_id()==1);return true;}
static void test_original(void *frame,unsigned char loading) {
    assert(frame==test_table && !loading);++test_originals;
    if(!stats.active) {
        if(test_case==13) {assert(claimed_stop && gd_calls==1 && !portable_wire_restore_complete);assert(SetEvent(stop_restore));}
        else assert(portable_wire_restore_complete && (!installed || test_case==14));
        return;
    }
    assert(stats.issued==test_originals && !strcmp(inputs[1],"00000000"));
    assert(pgetup_batch(&getup_ledger,test_run,1,stats.issued,inputs[0],inputs[1],test_guard,NULL,test_action));
    if(test_case==10 && stats.issued==30) {finish(1);return;}
    if(test_case==11 && stats.issued==30) RaiseException(0xe0424242,0,0,NULL);
    *(double*)(image+GAME_RVA)=20+(double)stats.issued/60;
    *(double*)(image+REAL_RVA)=80+(double)stats.issued/60;
    char hash[65];memset(hash,'b',64);hash[64]=0;assert(x4gate_report(stats.issued,hash));
    strcpy(proposal,stats.issued==29?"87010000":"00000000");
}
static void setup(unsigned port) {
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    stats.active=1;stats.slot=1;boot_mode=used=1;boot_phase=3;boot_options=GD_OPTIONS;ui_thread=main_thread=GetCurrentThreadId();timeout_ms=300;
    memset(secret,'a',32);secret[32]=0;memset(digest,'b',64);digest[64]=0;
    assert(pgetup_configure(&getup_ledger,test_run,1));gd_enabled=1;
    *(void**)(image+TABLE_RVA)=test_table;*(double*)(image+SPEED_RVA)=1;
    *(double*)(image+GAME_RVA)=20;*(double*)(image+REAL_RVA)=80;
    original_word=0;patched_word=17;*(uint64_t*)(image+WORD_RVA)=patched_word;installed=1;
    double saved=.02,fixed=1.0/60;memcpy(&saved_dt_bits,&saved,8);memcpy(&fixed_dt_bits,&fixed,8);
    *(uint64_t*)(image+0x6adfc60)=fixed_dt_bits;*(unsigned short*)(image+0xfa3302)=0x9066;dt_installed=1;
    path_installed=1;path_options=1;path_patched=19;*(unsigned __int128*)(image+0xef4610)=path_patched;
    world_identity_installed=1;world_identity_patched_word=23;*(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=world_identity_patched_word;
    flight_installed=ai_installed=agent_installed=ship_installed=nav_installed=spline_installed=1;
    ai_full_layout_verified=1;world_trace_early=1;world_trace_watch_count=1;
    *(void**)(image+FLIGHT_ENTRY)=(void*)flight_update;*(void**)(image+SHIP_ENTRY)=(void*)ship_move;ship_owned_slots=1;
    *(void**)(image+NAV_ENTRY)=(void*)nav_move;*(void**)(image+SPLINE_ENTRY)=(void*)spline_update;
    ai_patched_word=29;*(unsigned __int128*)(image+AI_WORD)=ai_patched_word;agent_patched_rel=31;*(int32_t*)(image+AGENT_REL)=agent_patched_rel;
    origin_started=1;origin_sites=3;origin_original_word=0;origin_patched_word=37;origin_patched_slot=41;
    *(unsigned __int128*)(image+PORIGIN_WORD)=origin_patched_word;*(uint64_t*)(image+PORIGIN_SLOT)=origin_patched_slot;
    transition_started=transition_enabled=1;transition_sites=7;world_queue_started=1;world_queue_enabled=1;world_queue_sites=7;
    for(unsigned i=0;i<3;i++) {
        transition_patched[i]=43+i;*(uint64_t*)(image+transition_entries[i])=transition_patched[i];
        world_queue_patched[i]=47+i;*(uint64_t*)(image+world_queue_entries[i])=world_queue_patched[i];
    }
    WSADATA ws;assert(!WSAStartup(MAKEWORD(2,2),&ws));connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    struct sockaddr_in remote={0};remote.sin_family=AF_INET;remote.sin_addr.s_addr=htonl(INADDR_LOOPBACK);remote.sin_port=htons((u_short)port);
    DWORD timeout=25;assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout)));
    assert(!connect(connection,(struct sockaddr*)&remote,sizeof(remote)));
}
int main(int argc,char **argv) {
    assert(argc==3);test_case=(unsigned)atoi(argv[2]);setup((unsigned)atoi(argv[1]));unsigned caught=0;
    __try {
        while(stats.active) {
            stats.completed=stats.issued;stats.game=*(double*)(image+GAME_RVA);stats.real=*(double*)(image+REAL_RVA);
            gd_tick(test_original,test_table,0);
        }
    } __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
    if(test_case==11) {assert(caught && gd_unwinds==1 && !gd_closed);finish(78);close_connection();}
    else assert(!caught && gd_terminal_finished);
    if(test_case==0) assert(gd_closed && !gd_failures && gd_terminal_restored && test_actions==1 && test_originals==121 && gd_returns==121);
    else assert(gd_failures || test_case==5 || test_case==11);
    if((test_case>=1 && test_case<=7) || test_case==12 || test_case==13 || test_case==15) assert(test_actions==0);
    if(test_case==15) assert(!stats.issued && gd_calls==1 && gd_returns==1 && !gd_terminal_restored && spline_installed && !spline_restored);
    if(stop_worker) {assert(WaitForSingleObject(stop_worker,3000)==WAIT_OBJECT_0);CloseHandle(stop_worker);CloseHandle(stop_deactivated);CloseHandle(stop_restore);}
    unsigned closes=test_closes;finish(99);close_connection();assert(test_closes==closes && closes==1 && connection==INVALID_SOCKET);
    assert(gd_calls==(LONG64)test_originals && gd_returns+gd_unwinds==gd_calls);
    printf("NGD_TEST case=%u calls=%lld returns=%lld actions=%u closed=%u failures=%lld restored=%u closes=%u\n",test_case,
        (long long)gd_calls,(long long)gd_returns,test_actions,gd_closed,(long long)gd_failures,gd_terminal_restored,test_closes);
    VirtualFree(image,0,MEM_RELEASE);return 0;
}
