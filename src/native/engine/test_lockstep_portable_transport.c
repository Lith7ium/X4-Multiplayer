/* Production Windows gate/decoder/transport, owned synthetic memory and real
 * loopback UDP. No X4 process, executable data or game installation is used.
 * The two forwarding stubs below live only in this process's owned allocation.
 * Optional: --python-peer PORT CONTRACT_SHA lets the production Python barrier
 * validate HELLO/state/DONE independently, using tests/test_native_portable.py's
 * synthetic contract/frame and a synthetic Linux declaration. This is never
 * native gameplay or Linux engine evidence.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
static int WSAAPI transport_recv(SOCKET,char *,int,int);
static int WSAAPI transport_close(SOCKET);
static int WSAAPI transport_cleanup(void);
static void WINAPI transport_unlock(PSRWLOCK);
static BOOL WINAPI transport_read(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);
#define recv transport_recv
#define closesocket transport_close
#define WSACleanup transport_cleanup
#define ReleaseSRWLockExclusive transport_unlock
#define ReadProcessMemory transport_read
/* Reuse the actual decoder fixture and its string representation helper. */
#define main portable_decoder_fixture_main
#include "test_lockstep_portable_windows.c"
#undef main
#undef recv
#undef closesocket
#undef WSACleanup
#undef ReleaseSRWLockExclusive
#undef ReadProcessMemory
#include <stdlib.h>

enum transport_case {
    TC_GO,TC_STALE,TC_FUTURE,TC_INPUT1,TC_INPUT2,TC_NATIVE_AT_GO,
    TC_GAME_AT_GO,TC_REAL_AT_GO,TC_CANCEL_AT_GO,TC_TIMEOUT,TC_CANCEL_WAIT,
    TC_TERMINAL_RETRY,TC_BAD_CLOSED,TC_RESTORE_GATE,TC_RESTORE_DT,
    TC_RESTORE_IMPORT,TC_BAD_FINAL_COUNT,TC_NATIVE_AT_STOP,TC_PEER_FAILED,
    TC_EARLY_OWNERSHIP,TC_EARLY_CAPTURE,TC_EARLY_CLOCK,TC_CANCEL_DURING_SAMPLE,TC_STARTUP_BUDGET,
    TC_CANCEL_RESTORE_PENDING,TC_STOP_AFTER_CLAIM,TC_SYNC_STOP,
    TC_UNPAUSED_AT_GO,TC_SELECTORS_AT_GO,TC_UNPAUSED_AT_STOP,TC_SELECTORS_AT_STOP,
    TC_UNPAUSED_FINAL,TC_SELECTORS_FINAL,TC_LOCAL_ADVANCE,
    TC_MAIN_AT_GO,TC_DT_DATA_AT_GO,TC_DT_BRANCH_AT_GO,TC_IMPORT_AT_GO,TC_SPEED_AT_GO,TC_TABLE_AT_GO,TC_LOADING_AT_GO,
    TC_CANCEL_DURING_HOLD,TC_COUNT
};
static const char *case_names[TC_COUNT]={
    "zero-input grants","stale and unauthenticated grants","future grant","input1 nonzero",
    "input2 nonzero","native mutation at GO","game mutation at GO","real mutation at GO",
    "concurrent cancellation at GO","timeout","cancellation while waiting",
    "lost DONE and stale CLOSED","invalid CLOSED","gate restoration ownership loss",
    "dt restoration ownership loss","import restoration ownership loss","final count mismatch",
    "native mutation at STOP","peer failed","pre-transport ownership failure",
    "pre-transport capture failure","pre-transport clock failure","cancellation during fresh sample",
    "startup budget then strict frame timeout","cancellation restoration still in progress",
    "admitted continuation before later stop","synchronous stop from admitted original",
    "unpaused mutation at GO","identical-bank selector flip at GO","unpaused mutation at STOP",
    "identical-bank selector flip at STOP","unpaused mutation after full permit samples",
    "identical-bank selector flip after full permit samples","local clocks and selectors advance only between frames",
    "main hook ownership changed at GO","fixed-dt data changed at GO","fixed-dt branch changed at GO",
    "identity hook ownership changed at GO","speed changed at GO","table identity changed at GO","loading flag changed at GO",
    "cancellation during held ownership reread"
};
static unsigned char transport_objects[12][0x400],transport_nav[12][0x80],transport_agents[12][0x1000];
static unsigned char transport_parent[0x80],transport_macros[2][0x80],transport_factions[2][0x80],transport_table[0x200];
static unsigned char transport_other_table[0x200];
static const char test_token[]="11111111111111111111111111111111";
static const char test_agent[]="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
static const uint64_t foreign_word=UINT64_C(0xdeadbeef12345678);
static unsigned current_case,python_peer,injected,cancel_pending,fixture_startup_override;
static volatile LONG original_calls,original_returned,close_calls,cleanup_calls,done_seen,ignored_seen;
static SOCKET server_socket=INVALID_SOCKET;
static HANDLE server_stop,server_thread,cancel_start,cancel_done,cancel_thread;
static HANDLE cancel_deactivated,cancel_allow_restore,restore_thread;
static HANDLE claimed_original_entered;
static volatile LONG restore_pause_once,claim_stop_once;
static char first_done[256];
static ULONGLONG fixture_started,first_original_at;
static unsigned local_mutation,local_guard_reads,local_mutation_at;
static void cancel_at_receive(void);
static char status_text[640];
static int status_nil;
static void status_pushnil(lua_State *state) {assert(!state);status_nil=1;}
static void status_pushstring(lua_State *state,const char *text,size_t length) {
    assert(!state && length<sizeof(status_text));memcpy(status_text,text,length);status_text[length]=0;
}
static DWORD WINAPI status_reader(void *unused) {
    (void)unused;
    assert(GetCurrentThreadId()!=ui_thread && GetCurrentThreadId()!=main_thread);
    assert(lua_portable_status(NULL)==1);
    /* Read access must not adopt this thread or grant stop/control authority. */
    assert(x4gate_stop()==0);return 0;
}
static void check_status_from_other_thread(int stopped) {
    DWORD owner=ui_thread;int adopted=boot_adopted;
    p_lua_pushnil=status_pushnil;p_lua_pushlstring=status_pushstring;
    status_nil=0;status_text[0]=0;
    HANDLE reader=CreateThread(NULL,0,status_reader,NULL,0,NULL);assert(reader);
    assert(WaitForSingleObject(reader,3000)==WAIT_OBJECT_0);CloseHandle(reader);
    assert(ui_thread==owner && boot_adopted==adopted);
    if(!stopped) {assert(status_nil && !status_text[0]);return;}
    assert(!status_nil && strlen(status_text)<640);
    char expected[160];
    snprintf(expected,sizeof(expected)," active=0 reason=%u issued=%llu completed=%llu",stats.reason,
        (unsigned long long)stats.issued,(unsigned long long)stats.completed);
    assert(strstr(status_text,expected));
    snprintf(expected,sizeof(expected)," calls=%lld returns=%lld failures=%lld ",
        (long long)portable_native_calls,(long long)portable_native_returns,(long long)portable_native_failures);
    assert(strstr(status_text,expected));
    assert(strstr(status_text,portable_wire_closed?" closed=1 ":" closed=0 "));
    /* Exercise the production Lua status, including b97's post-stop reader.
     * CLOSED/counts/restoration remain independent success requirements; a
     * completed native reread alone never establishes wire/session success. */
    if(portable_wire_closed) {
        assert(!portable_native_failures && portable_wire_terminal_restored && stats.reason==1);
        assert(strstr(status_text," stage=complete ") && strstr(status_text," wire_stage=closed "));
    }
}

static void mutate_local_hold(unsigned mutation) {
    if(mutation==1) *(double*)(image+PORTABLE_UNPAUSED_RVA)+=1.0;
    else {assert(mutation==2);*(uint32_t*)(image+0x38f4ee4)^=1;*(uint32_t*)(image+0x2f4b6f4)^=1;}
}
static BOOL WINAPI transport_read(HANDLE process,LPCVOID source,LPVOID target,SIZE_T size,SIZE_T *copied) {
    if(local_mutation && source==image+GAME_RVA && ++local_guard_reads==local_mutation_at) {
        unsigned mutation=local_mutation;local_mutation=0;
        if(mutation==3)cancel_at_receive();else mutate_local_hold(mutation);
    }
    return ReadProcessMemory(process,source,target,size,copied);
}

static void *transport_lookup(void *map,uint64_t id,unsigned flags) {
    assert(map==transport_objects && flags==4);
    /* Stop wins after the accept path releases its lock for native rereading,
     * rather than merely before its initial active check. This also catches
     * holding the cancellation lock over a potentially reentrant native read. */
    if(cancel_pending) {cancel_pending=0;cancel_at_receive();}
    if(id==200) return transport_parent;
    return id>=100 && id<112?transport_objects[id-100]:NULL;
}
static void transport_original(void *frame,unsigned char loading) {
    assert(frame==transport_table && !loading);
    unsigned call=(unsigned)InterlockedIncrement(&original_calls);
    if(call==1) first_original_at=GetTickCount64();
    assert(portable_native_calls==(LONG64)call && portable_native_returns==(LONG64)call-1);
    if(stats.active) {
        assert(stats.issued==call && installed && dt_installed && world_identity_installed);
        if(current_case==TC_SYNC_STOP) {
            assert(call==1 && GetCurrentThreadId()==ui_thread && x4gate_stop()==1);
            assert(!stats.active && stats.restored && dt_restored && !installed && !dt_installed && !world_identity_installed);
            assert(portable_native_calls==1 && !portable_native_returns);
        }
        *(double*)(image+GAME_RVA)=7.0+call/60.0;
        *(double*)(image+REAL_RVA)=100.0+call/60.0;
        *(double*)(image+PORTABLE_UNPAUSED_RVA)=90.0+call/60.0;
        if(current_case==TC_LOCAL_ADVANCE) mutate_local_hold(2);
    } else {
        /* Stop-before-claim requires restoration first. A recorded continuation
         * claim may precede a later stop and enter original during restoration. */
        assert(portable_wire_finish_started);
        if(current_case==TC_STOP_AFTER_CLAIM) {
            assert(call==1 && claim_stop_once && restore_pause_once && stats.issued==1);
            assert(installed && dt_installed && world_identity_installed && !stats.restored);
            assert(SetEvent(claimed_original_entered));
            assert(WaitForSingleObject(cancel_done,3000)==WAIT_OBJECT_0);
        }
        if(current_case==TC_RESTORE_GATE || current_case==TC_EARLY_OWNERSHIP || current_case==TC_MAIN_AT_GO) {
            assert(installed && !stats.restored && *(uint64_t*)(image+WORD_RVA)==foreign_word);
        } else assert(!installed && stats.restored && *(uint64_t*)(image+WORD_RVA)==original_word);
        if(current_case==TC_RESTORE_DT || current_case==TC_DT_DATA_AT_GO)
            assert(dt_installed && !dt_restored && *(uint64_t*)(image+0x6adfc60)==foreign_word);
        else if(current_case==TC_DT_BRANCH_AT_GO)
            assert(dt_installed && !dt_restored && *(uint64_t*)(image+0x6adfc60)==saved_dt_bits);
        else assert(!dt_installed && dt_restored && *(uint64_t*)(image+0x6adfc60)==saved_dt_bits);
        assert(*(unsigned short*)(image+0xfa3302)==(current_case==TC_DT_BRANCH_AT_GO?0x9090:0x0874));
        if(current_case==TC_RESTORE_IMPORT || current_case==TC_IMPORT_AT_GO)
            assert(world_identity_installed && !world_identity_restored && *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)==foreign_word);
        else assert(!world_identity_installed && world_identity_restored &&
                    *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)==world_identity_original_word);
        if(current_case==TC_BAD_FINAL_COUNT) InterlockedIncrement64(&portable_native_returns);
    }
    InterlockedIncrement(&original_returned);
}
static void owned_forwarder(unsigned rva,void *target) {
    unsigned char bytes[14]={0xff,0x25,0,0,0,0};DWORD old;
    assert(VirtualProtect(image+rva,sizeof(bytes),PAGE_READWRITE,&old));
    memcpy(bytes+6,&target,8);memcpy(image+rva,bytes,sizeof(bytes));
    assert(VirtualProtect(image+rva,sizeof(bytes),PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),image+rva,sizeof(bytes)));
}
static DWORD WINAPI transport_cancel(void *unused) {
    (void)unused;
    assert(WaitForSingleObject(cancel_start,3000)==WAIT_OBJECT_0);
    assert(x4gate_stop()==1);assert(SetEvent(cancel_done));return 0;
}
static void WINAPI transport_unlock(PSRWLOCK lock) {
    ReleaseSRWLockExclusive(lock);
    if(current_case==TC_STOP_AFTER_CLAIM && lock==&portable_wire_lock && GetCurrentThreadId()==main_thread &&
       portable_native_calls==1 && !original_calls && stats.issued==1 &&
       !InterlockedCompareExchange(&claim_stop_once,1,0)) {
        /* The production helper has recorded its claim under the admission
         * lock; only now allow the UI cancellation to win the next lock turn. */
        assert(SetEvent(cancel_start));
        assert(WaitForSingleObject(cancel_deactivated,3000)==WAIT_OBJECT_0);
    }
    if((current_case==TC_CANCEL_RESTORE_PENDING || current_case==TC_STOP_AFTER_CLAIM) && lock==&portable_wire_lock &&
       GetCurrentThreadId()==ui_thread && portable_wire_finish_started && !stats.active &&
       !InterlockedCompareExchange(&restore_pause_once,1,0)) {
        /* Admission is closed but no restore function has run. Case24's
         * unclaimed original must wait; case25's earlier claim may execute. */
        assert(installed && dt_installed && world_identity_installed && !stats.restored);
        assert(SetEvent(cancel_deactivated));
        assert(WaitForSingleObject(cancel_allow_restore,3000)==WAIT_OBJECT_0);
    }
}
static DWORD WINAPI allow_restore(void *unused) {
    (void)unused;assert(WaitForSingleObject(cancel_deactivated,3000)==WAIT_OBJECT_0);
    if(current_case==TC_STOP_AFTER_CLAIM) {
        assert(WaitForSingleObject(claimed_original_entered,3000)==WAIT_OBJECT_0);
        assert(original_calls==1 && portable_native_calls==1 && !original_returned && !portable_native_returns && stats.issued==1);
    } else {
        Sleep(40);
        assert(!original_calls && !original_returned && !portable_native_calls && !portable_native_returns && !stats.issued);
    }
    assert(installed && dt_installed && world_identity_installed && !stats.restored);
    assert(SetEvent(cancel_allow_restore));return 0;
}
static void cancel_at_receive(void) {
    assert(SetEvent(cancel_start));
    assert(WaitForSingleObject(cancel_done,3000)==WAIT_OBJECT_0);
    assert(!stats.active && stats.reason==1 && stats.issued==0);
    assert(!portable_native_calls && !portable_native_returns && !original_calls && !original_returned);
    assert(stats.restored && dt_restored && !installed && !dt_installed && !world_identity_installed);
}
static int WSAAPI transport_recv(SOCKET socket,char *buffer,int length,int flags) {
    int count=recv(socket,buffer,length,flags),error=count==SOCKET_ERROR?WSAGetLastError():0;
    if(!python_peer && !injected) {
        int go=count>6 && !memcmp(buffer,"NGPGO|",6);
        int stop=count>8 && !memcmp(buffer,"NGPSTOP|",8);
        if(go && (current_case==TC_NATIVE_AT_GO || current_case==TC_GAME_AT_GO ||
                  current_case==TC_REAL_AT_GO || current_case==TC_CANCEL_AT_GO ||
                  current_case==TC_CANCEL_DURING_SAMPLE || current_case==TC_CANCEL_RESTORE_PENDING ||
                  current_case==TC_UNPAUSED_AT_GO || current_case==TC_SELECTORS_AT_GO ||
                  current_case==TC_UNPAUSED_FINAL || current_case==TC_SELECTORS_FINAL || current_case>=TC_MAIN_AT_GO)) {
            injected=1;
            if(current_case==TC_NATIVE_AT_GO) *(double*)(transport_objects[0]+0x280)=99.0;
            if(current_case==TC_GAME_AT_GO) *(double*)(image+GAME_RVA)+=1.0;
            if(current_case==TC_REAL_AT_GO) *(double*)(image+REAL_RVA)+=1.0;
            if(current_case==TC_MAIN_AT_GO) *(uint64_t*)(image+WORD_RVA)=foreign_word;
            if(current_case==TC_DT_DATA_AT_GO) *(uint64_t*)(image+0x6adfc60)=foreign_word;
            if(current_case==TC_DT_BRANCH_AT_GO) *(unsigned short*)(image+0xfa3302)=0x9090;
            if(current_case==TC_IMPORT_AT_GO) *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=foreign_word;
            if(current_case==TC_SPEED_AT_GO) *(double*)(image+SPEED_RVA)=2.0;
            if(current_case==TC_TABLE_AT_GO) *(void**)(image+TABLE_RVA)=transport_other_table;
            if(current_case==TC_LOADING_AT_GO) *(uint32_t*)(transport_table+0x130)=1;
            if(current_case==TC_UNPAUSED_AT_GO) mutate_local_hold(1);
            if(current_case==TC_SELECTORS_AT_GO) mutate_local_hold(2);
            if(current_case==TC_UNPAUSED_FINAL || current_case==TC_SELECTORS_FINAL) {
                /* Two complete samples plus their closing guard have finished.
                 * Mutate on the first read inside the acceptance lock. */
                local_mutation=current_case==TC_UNPAUSED_FINAL?1:2;local_guard_reads=0;local_mutation_at=7;
            }
            if(current_case==TC_CANCEL_AT_GO) cancel_at_receive();
            if(current_case==TC_CANCEL_DURING_SAMPLE) cancel_pending=1;
            if(current_case==TC_CANCEL_RESTORE_PENDING) {
                assert(SetEvent(cancel_start));
                assert(WaitForSingleObject(cancel_deactivated,3000)==WAIT_OBJECT_0);
            }
        }
        if(stop) {
            injected=1;
            if(current_case==TC_NATIVE_AT_STOP) *(float*)(transport_agents[0]+0xad0)=42.0f;
            if(current_case==TC_UNPAUSED_AT_STOP) mutate_local_hold(1);
            if(current_case==TC_SELECTORS_AT_STOP) mutate_local_hold(2);
            if(current_case==TC_RESTORE_GATE) *(uint64_t*)(image+WORD_RVA)=foreign_word;
            if(current_case==TC_RESTORE_DT) *(uint64_t*)(image+0x6adfc60)=foreign_word;
            if(current_case==TC_RESTORE_IMPORT) *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=foreign_word;
        }
        if(count==SOCKET_ERROR && current_case==TC_CANCEL_WAIT) {injected=1;cancel_at_receive();}
        if(count==SOCKET_ERROR && current_case==TC_CANCEL_DURING_HOLD) {
            injected=1;local_mutation=3;local_guard_reads=0;local_mutation_at=1;
        }
    }
    if(current_case==TC_STALE && count>0) {
        char stale[128];snprintf(stale,sizeof(stale),"NGPGO|1|%s|1|%llu|00000000|00000000",test_token,
                                 (unsigned long long)stats.completed);
        if(count==(int)strlen(stale) && !memcmp(buffer,stale,(size_t)count)) {
            assert(original_calls==(LONG)stats.completed);InterlockedIncrement(&ignored_seen);
        }
    }
    if(count==SOCKET_ERROR) WSASetLastError(error);
    return count;
}
static int WSAAPI transport_close(SOCKET socket) {
    InterlockedIncrement(&close_calls);return closesocket(socket);
}
static int WSAAPI transport_cleanup(void) {
    InterlockedIncrement(&cleanup_calls);return WSACleanup();
}
static void peer_reply(const struct sockaddr_in *address,const char *kind,unsigned tick,const char *suffix) {
    char packet[256];int length=snprintf(packet,sizeof(packet),"%s|1|%s|1|%u%s",kind,test_token,tick,suffix);
    assert(length>0 && length<(int)sizeof(packet));
    assert(sendto(server_socket,packet,length,0,(const struct sockaddr*)address,sizeof(*address))==length);
}
static DWORD WINAPI transport_peer(void *unused) {
    (void)unused;char states[3][PORTABLE_WIRE_PACKET_MAX]={{0}};
    while(WaitForSingleObject(server_stop,0)==WAIT_TIMEOUT) {
        char packet[PORTABLE_WIRE_PACKET_MAX],copy[PORTABLE_WIRE_PACKET_MAX];struct sockaddr_in address;
        int size=sizeof(address),count=recvfrom(server_socket,packet,sizeof(packet)-1,0,(struct sockaddr*)&address,&size);
        if(count==SOCKET_ERROR) {int e=WSAGetLastError();assert(e==WSAETIMEDOUT || e==WSAEWOULDBLOCK);continue;}
        assert(count>0 && address.sin_addr.s_addr==htonl(INADDR_LOOPBACK));packet[count]=0;
        assert(strlen(packet)==(size_t)count);memcpy(copy,packet,(size_t)count+1);
        char *fields[10]={copy};unsigned parts=1;
        for(char *p=copy;*p;p++) if(*p=='|') {assert(parts<10);*p=0;fields[parts++]=p+1;}
        assert(parts>=4 && !strcmp(fields[1],"1") && !strcmp(fields[2],test_token) && !strcmp(fields[3],"1"));
        if(!strcmp(fields[0],"NGPHELLO")) {
            assert(parts==9 && !strcmp(fields[4],"windows") && !strcmp(fields[5],EXE_SHA) &&
                   !strcmp(fields[6],test_agent) && !strcmp(fields[7],portable_native_contract_text) &&
                   !strcmp(fields[8],PORTABLE_SCHEMA_SHA));continue;
        }
        if(!strcmp(fields[0],"NGPDONE")) {
            assert(parts==8 && original_calls==original_returned && !stats.active);
            unsigned tick=(unsigned)strtoul(fields[4],NULL,10),calls=(unsigned)strtoul(fields[5],NULL,10);
            assert(calls==tick+1 && calls==(unsigned)original_calls && calls==strtoul(fields[6],NULL,10));
            assert(!strcmp(fields[7],"1") && stats.restored && dt_restored && !installed && !dt_installed && !world_identity_installed);
            LONG seen=InterlockedIncrement(&done_seen);
            if(seen==1) strcpy(first_done,packet);else assert(!strcmp(first_done,packet));
            if(current_case==TC_PEER_FAILED) continue;
            assert(tick==portable_native_limit);
            if(current_case==TC_TERMINAL_RETRY && seen==1) continue; /* lost DONE */
            if(current_case==TC_TERMINAL_RETRY && seen==2) {peer_reply(&address,"NGPCLOSED",tick-1,"");continue;}
            if(current_case==TC_BAD_CLOSED) {peer_reply(&address,"NGPCLOSED",tick,"|unexpected");continue;}
            peer_reply(&address,"NGPCLOSED",tick,"");continue;
        }
        assert(!strcmp(fields[0],"NGPSTATE") && parts==7 && strlen(fields[4])==16 && strlen(fields[5])==64);
        unsigned char decoded[PORTABLE_FRAME_CAPACITY];size_t hex_length=strlen(fields[6]);
        assert(hex_length && !(hex_length&1) && hex_length/2<=sizeof(decoded));
        assert(portable_native_hex(fields[6],(unsigned)hex_length,decoded));
        assert(hash_ok(decoded,(ULONG)(hex_length/2),fields[5]));
        assert(!memcmp(decoded,"X4P1FRM\0",8));uint64_t tick;memcpy(&tick,decoded+40,8);
        assert(tick<=portable_native_limit && tick<3 && tick<=(uint64_t)original_calls);
        unsigned first=!states[tick][0];
        if(!first) assert(!strcmp(states[tick],packet));else strcpy(states[tick],packet);
        if(current_case==TC_TIMEOUT || current_case==TC_CANCEL_WAIT || current_case==TC_CANCEL_DURING_HOLD) continue;
        if(current_case==TC_STARTUP_BUDGET) {
            if(tick) continue;
            if(first) Sleep(200); /* exceeds frame budget, within startup budget */
        }
        if(current_case==TC_PEER_FAILED) {peer_reply(&address,"NGPSTOP",(unsigned)tick,"|1");continue;}
        if(tick==portable_native_limit) {peer_reply(&address,"NGPSTOP",(unsigned)tick,"|0");continue;}
        if(current_case==TC_STALE) {
            peer_reply(&address,"NGPGO",(unsigned)tick,"|00000000|00000000");
            const char wrong[]="NGPGO|1|22222222222222222222222222222222|1|1|00000000|00000000";
            assert(sendto(server_socket,wrong,sizeof(wrong)-1,0,(struct sockaddr*)&address,sizeof(address))==(int)sizeof(wrong)-1);
        }
        peer_reply(&address,"NGPGO",(unsigned)tick+(current_case==TC_FUTURE?2:1),
                   current_case==TC_INPUT1?"|00000001|00000000":current_case==TC_INPUT2?"|00000000|00000001":"|00000000|00000000");
    }
    return 0;
}

static void transport_initialize(unsigned remote,const char *contract) {
    memset(&stats,0,sizeof(stats));memset(transport_objects,0,sizeof(transport_objects));
    memset(transport_nav,0,sizeof(transport_nav));memset(transport_agents,0,sizeof(transport_agents));
    memset(transport_parent,0,sizeof(transport_parent));memset(transport_table,0,sizeof(transport_table));
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    portable_native_inverse_ids=VirtualAlloc(NULL,sizeof(*portable_native_inverse_ids)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && portable_native_inverse_ids);
    world_identity_count=world_identity_failures=portable_native_inverse_count=0;
    portable_native_count=0;portable_native_calls=portable_native_returns=portable_native_failures=0;
    portable_wire_enabled=portable_wire_draining=portable_wire_closed=portable_wire_finish_started=0;
    portable_wire_epoch=0;
    portable_wire_restore_complete=0;
    portable_wire_terminal_restored=0;portable_native_configured=1;portable_native_limit=2;
    original_calls=original_returned=close_calls=cleanup_calls=done_seen=ignored_seen=0;
    injected=cancel_pending=0;local_mutation=local_guard_reads=local_mutation_at=0;
    restore_pause_once=claim_stop_once=0;first_done[0]=0;first_original_at=0;
    stats.active=1;stats.slot=1;boot_mode=1;used=1;boot_adopted=0;boot_phase=0;boot_options=UINT64_C(70368744177664);
    ui_thread=main_thread=GetCurrentThreadId();timeout_ms=current_case==TC_STARTUP_BUDGET?100:300;
    strcpy(secret,test_token);strcpy(proposal,"00000000");strcpy(inputs[0],proposal);strcpy(inputs[1],proposal);
    strcpy(portable_native_contract_text,contract);assert(portable_native_hex(contract,64,portable_native_contract));
    unsigned startup=fixture_startup_override?fixture_startup_override:python_peer?5000:1000;
    assert(!portable_transport_config(test_agent,64,999));
    assert(!portable_transport_config(test_agent,64,900001));
    assert(portable_transport_config(test_agent,64,startup));
    assert(portable_transport_config(test_agent,64,startup));
    assert(!portable_transport_config(test_agent,64,startup+1));boot_phase=3;
    *(void**)(image+TABLE_RVA)=transport_table;*(void**)(image+0x6cf1500)=transport_objects;
    *(double*)(image+GAME_RVA)=7.0;*(double*)(image+REAL_RVA)=100.0;*(double*)(image+SPEED_RVA)=1.0;
    *(double*)(image+PORTABLE_UNPAUSED_RVA)=90.0;
    *(unsigned*)(image+0x2f4b6f0)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    original_word=UINT64_C(0x1111222233334444);patched_word=UINT64_C(0x5555666677778888);
    *(uint64_t*)(image+WORD_RVA)=patched_word;installed=1;
    double saved=.02,fixed=1.0/60.0;memcpy(&saved_dt_bits,&saved,8);memcpy(&fixed_dt_bits,&fixed,8);
    *(uint64_t*)(image+0x6adfc60)=fixed_dt_bits;*(unsigned short*)(image+0xfa3302)=0x9066;
    dt_installed=1;dt_restored=0;
    world_identity_original_word=UINT64_C(0x1111222233334444);world_identity_patched_word=UINT64_C(0x5555666677778888);
    *(uint64_t*)(image+WORLD_IDENTITY_ENTRY)=world_identity_patched_word;world_identity_installed=1;world_identity_restored=0;
    *(uint64_t*)(transport_parent+8)=200;
    assert(world_identity_record(200,256) && portable_native_inverse_record(256,200));
    *(void**)(image+0x2b7feb8+0x1620)=image+0x352b10;
    *(void**)(image+0x2ba5430+18*8)=image+0xefb10;*(void**)(image+0x2c51158+0x30)=image+0xea5a70;
    for(unsigned i=0;i<2;i++) {
        *(void**)transport_macros[i]=image+0x2ba5430;
        string_set(transport_macros[i]+0x20,i?"ship_xen_s_fighter_01_a_macro":"ship_arg_s_fighter_01_a_macro");
        string_set(transport_factions[i]+0x10,i?"xenon":"argon");
    }
    for(unsigned i=0;i<12;i++) {
        unsigned char *o=transport_objects[i],*n=transport_nav[i],*a=transport_agents[i];
        portable_native_ids[i]=(portable_native_identity){i+1,i+100,o,i+1,1};
        assert(world_identity_record(i+100,i+1) && portable_native_inverse_record(i+1,i+100));
        *(void**)o=image+0x2b7feb8;*(uint64_t*)(o+8)=i+100;*(void**)(o+0x60)=transport_macros[i>=6];
        *(void**)(o+0x70)=transport_parent;*(void**)(o+0x200)=n;*(double*)(o+0x280)=100.0;
        *(void**)(o+0x348)=transport_factions[i>=6];*(void**)n=image+0x2c51158;
        *(void**)(n+0x28)=o;*(void**)(n+0x30)=a+0xad0;*(void**)(n+0x40)=a;*(void**)(a+0xef0)=o;
        for(unsigned b=0;b<2;b++) {
            unsigned char *s=a+0xad0+b*0x1a0;
            *(float*)(s+16)=*(float*)(s+36)=*(float*)(s+56)=1.0f;
            *(uint64_t*)(s+0xb0)=*(uint64_t*)(s+0xb8)=200;
        }
    }
    owned_forwarder(0xced70,(void*)transport_lookup);owned_forwarder(TICK_RVA,(void*)transport_original);
    /* Public reader must define zero commands and absent parent even when the
     * destination was poisoned; no test replacement for native_sample. */
    portable_frame poisoned;memset(&poisoned,0xa5,sizeof(poisoned));
    *(void**)(transport_objects[0]+0x70)=NULL;
    portable_native_hold local;
    assert(portable_native_sample(&poisoned,0,&local,transport_table,0));
    assert(!poisoned.commands[0] && !poisoned.commands[1] && !poisoned.actors[0].parent);
    *(void**)(transport_objects[0]+0x70)=transport_parent;
    WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data)); /* owning gate reference */
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)remote);
    DWORD receive_timeout=25;assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&receive_timeout,sizeof(receive_timeout)));
    assert(!connect(connection,(struct sockaddr*)&address,sizeof(address)));
}
static void transport_dispose(void) {
    unsigned reason=stats.reason;LONG closes=close_calls,cleanups=cleanup_calls,calls=original_calls;
    finish(99);close_connection();
    assert(stats.reason==(portable_wire_enabled?reason:99) && close_calls==closes && cleanup_calls==cleanups && original_calls==calls);
    assert(close_calls==1 && cleanup_calls==1 && connection==INVALID_SOCKET);
    if(cancel_thread) {
        assert(WaitForSingleObject(cancel_thread,3000)==WAIT_OBJECT_0);
        CloseHandle(cancel_thread);CloseHandle(cancel_start);CloseHandle(cancel_done);cancel_thread=NULL;
    }
    if(restore_thread) {
        assert(WaitForSingleObject(restore_thread,3000)==WAIT_OBJECT_0);CloseHandle(restore_thread);restore_thread=NULL;
        CloseHandle(cancel_deactivated);CloseHandle(cancel_allow_restore);
        if(claimed_original_entered) {CloseHandle(claimed_original_entered);claimed_original_entered=NULL;}
    }
    if(server_thread) {
        assert(SetEvent(server_stop));assert(WaitForSingleObject(server_thread,3000)==WAIT_OBJECT_0);
        CloseHandle(server_thread);CloseHandle(server_stop);server_thread=NULL;
        assert(!closesocket(server_socket));server_socket=INVALID_SOCKET;
    }
    assert(!WSACleanup()); /* peer/fixture reference, separate from owned gate */
    assert(VirtualFree(portable_native_inverse_ids,0,MEM_RELEASE));portable_native_inverse_ids=NULL;
    assert(VirtualFree(world_identities,0,MEM_RELEASE));world_identities=NULL;
    assert(VirtualFree(image,0,MEM_RELEASE));image=NULL;
}
static void run_transport_case(unsigned which,unsigned remote,const char *contract) {
    current_case=which;WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));
    if(!python_peer) {
        server_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(server_socket!=INVALID_SOCKET);
        struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(!bind(server_socket,(struct sockaddr*)&address,sizeof(address)));int length=sizeof(address);
        assert(!getsockname(server_socket,(struct sockaddr*)&address,&length));remote=ntohs(address.sin_port);
        DWORD timeout=25;assert(!setsockopt(server_socket,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout)));
    }
    transport_initialize(remote,contract);
    if(which==TC_CANCEL_AT_GO || which==TC_CANCEL_WAIT || which==TC_CANCEL_DURING_SAMPLE ||
       which==TC_CANCEL_RESTORE_PENDING || which==TC_STOP_AFTER_CLAIM || which==TC_CANCEL_DURING_HOLD) {
        cancel_start=CreateEventW(NULL,TRUE,FALSE,NULL);cancel_done=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(cancel_start && cancel_done);DWORD thread_id;
        cancel_thread=CreateThread(NULL,0,transport_cancel,NULL,0,&thread_id);assert(cancel_thread);ui_thread=thread_id;
    }
    if(which==TC_CANCEL_RESTORE_PENDING || which==TC_STOP_AFTER_CLAIM) {
        cancel_deactivated=CreateEventW(NULL,TRUE,FALSE,NULL);cancel_allow_restore=CreateEventW(NULL,TRUE,FALSE,NULL);
        assert(cancel_deactivated && cancel_allow_restore);
        if(which==TC_STOP_AFTER_CLAIM) {claimed_original_entered=CreateEventW(NULL,TRUE,FALSE,NULL);assert(claimed_original_entered);}
        restore_thread=CreateThread(NULL,0,allow_restore,NULL,0,NULL);assert(restore_thread);
    }
    if(!python_peer) {
        server_stop=CreateEventW(NULL,TRUE,FALSE,NULL);assert(server_stop);
        server_thread=CreateThread(NULL,0,transport_peer,NULL,0,NULL);assert(server_thread);
    } else timeout_ms=5000;
    if(which==TC_EARLY_OWNERSHIP) *(uint64_t*)(image+WORD_RVA)=foreign_word;
    if(which==TC_EARLY_CAPTURE) *(uint64_t*)(transport_objects[0]+8)=999;
    if(which==TC_EARLY_CLOCK) {
        stats.issued=1;previous_game=5.0;portable_native_calls=portable_native_returns=1;
        original_calls=original_returned=1; /* one preceding returned native frame */
    }
    unsigned calls_before=(unsigned)original_calls;
    if(!stats.issued) {
        check_status_from_other_thread(0);
        /* Acceptance publishes inactive before finish acquires restoration.
         * That interval must not adopt a post-stop reader either. */
        stats.active=0;check_status_from_other_thread(0);stats.active=1;
    }
    fixture_started=GetTickCount64();
    while(stats.active) {gate_tick(transport_table,0);assert((unsigned)original_calls<=calls_before+3);}
    assert(original_calls==original_returned && portable_native_calls==original_calls);
    assert(portable_native_returns==original_calls+(which==TC_BAD_FINAL_COUNT?1:0));
    unsigned successful=which==TC_GO || which==TC_STALE || which==TC_TERMINAL_RETRY || which==TC_LOCAL_ADVANCE;
    if(successful) {
        assert(stats.issued==2 && stats.completed==2 && original_calls==3 && !portable_native_failures && portable_wire_closed);
        if(!python_peer) assert(done_seen>=(which==TC_TERMINAL_RETRY?3:1));
        if(which==TC_STALE) assert(ignored_seen==2);
    } else {
        assert(!portable_wire_closed);
        if(which==TC_CANCEL_AT_GO || which==TC_CANCEL_WAIT || which==TC_CANCEL_DURING_SAMPLE || which==TC_CANCEL_RESTORE_PENDING ||
           which==TC_CANCEL_DURING_HOLD)
            assert(!portable_native_failures && !stats.issued && stats.reason==1 && original_calls==1);
        else if(which==TC_STOP_AFTER_CLAIM)
            assert(!portable_native_failures && stats.issued==1 && !stats.completed && stats.reason==1 && original_calls==1 && claim_stop_once);
        else if(which==TC_SYNC_STOP)
            assert(!portable_native_failures && stats.issued==1 && !stats.completed && stats.reason==1 && original_calls==1);
        else if(which==TC_STARTUP_BUDGET) {
            assert(stats.issued==1 && stats.completed==1 && original_calls==2 && portable_native_failures==1);
            assert(!strcmp(portable_wire_stage,"peer_timeout") && first_original_at-fixture_started>=150);
            assert(GetTickCount64()-first_original_at<700); /* later tick did not reuse startup allowance */
        }
        else if(which>=TC_UNPAUSED_AT_GO) {
            unsigned stopped=which==TC_UNPAUSED_AT_STOP || which==TC_SELECTORS_AT_STOP;
            assert(stats.issued==(stopped?2u:0u) && original_calls==(stopped?3:1) && portable_native_failures>0);
            assert(!strcmp(portable_wire_stage,"state_changed_at_permit"));
            if(which==TC_UNPAUSED_FINAL || which==TC_SELECTORS_FINAL) assert(!local_mutation && local_guard_reads==7);
        }
        else if(which>=TC_EARLY_OWNERSHIP) assert(original_calls==(LONG)calls_before+1 && stats.reason!=1);
        else assert(portable_native_failures>0);
        if(which<TC_TERMINAL_RETRY && which!=TC_CANCEL_AT_GO && which!=TC_CANCEL_WAIT)
            assert(!stats.issued && original_calls==1);
        if(which==TC_RESTORE_GATE || which==TC_RESTORE_DT || which==TC_RESTORE_IMPORT)
            assert(!done_seen && !strcmp(portable_wire_stage,"state_changed_at_permit"));
        if(which==TC_BAD_FINAL_COUNT)
            assert(!done_seen && !strcmp(portable_wire_stage,"terminal_receipt_failed"));
    }
    assert(!portable_transport_config(test_agent,64,portable_wire_startup_ms));
    check_status_from_other_thread(1); /* Includes issued0 refusals and actual CLOSED. */
    printf("PASS portable transport: %s (calls=%ld, issued=%llu, failures=%lld, stage=%s)\n",
           case_names[which],original_calls,(unsigned long long)stats.issued,(long long)portable_native_failures,portable_wire_stage);
    transport_dispose();
}
static void transport_boot_original(void *frame,unsigned char loading) {
    assert(frame==transport_table && loading==1);
    assert(!portable_native_calls && !portable_native_returns && !portable_native_count);
    if(stats.active) assert(boot_phase==2 && installed && dt_installed && world_identity_installed);
    else assert(stats.reason==26 && stats.restored && dt_restored && !installed && !dt_installed && !world_identity_installed);
    InterlockedIncrement(&original_calls);InterlockedIncrement(&original_returned);
}
static void run_boot_watchdogs(void) {
    const char *contract="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    for(unsigned which=0;which<3;which++) {
        WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));
        current_case=TC_GO;fixture_startup_override=600000;
        transport_initialize(9,contract);fixture_startup_override=0;
        owned_forwarder(TICK_RVA,(void*)transport_boot_original);
        boot_phase=1;boot_started=GetTickCount64()-(which==1?600001:121000);
        if(which==2) portable_wire_enabled=0; /* unchanged legacy 120s watchdog */
        gate_tick(transport_table,1);
        assert(original_calls==1 && original_returned==1 && !portable_native_calls && !portable_native_returns && !stats.issued);
        if(!which) {
            assert(stats.active && boot_phase==2 && !stats.reason && !close_calls);
            finish(1);
        } else assert(!stats.active && stats.reason==26 && close_calls==1);
        assert(!portable_native_failures);
        check_status_from_other_thread(which!=2);
        printf("PASS portable boot watchdog: %s (no waiting, no portable counters)\n",
               which==0?"121s within configured 600s":which==1?"configured 600s expired":"legacy 120s expired");
        transport_dispose();
    }
}
static void run_local_hold_mutants(void) {
    const char *contract="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    for(unsigned mutation=1;mutation<=2;mutation++) {
        WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));current_case=TC_GO;
        transport_initialize(9,contract);stats.game=7.0;stats.real=100.0;
        assert(portable_native_capture(0,transport_table,0) && portable_transport_held(0,transport_table,0));
        assert(!portable_transport_held(0,transport_table,1) && !portable_transport_held(0,NULL,0));
        assert(portable_transport_accept(0,PW_GO,transport_other_table,0)==-1 && !stats.issued && stats.active);
        assert(portable_transport_accept(0,PW_GO,transport_table,1)==-1 && !stats.issued && stats.active);
        assert(portable_transport_held(0,transport_table,0));
        unsigned char baseline[PORTABLE_FRAME_CAPACITY],current[PORTABLE_FRAME_CAPACITY];
        unsigned length=portable_native_lengths[0];memcpy(baseline,portable_native_bytes[0],length);
        portable_native_hold retained=portable_native_holds[0],changed;
        mutate_local_hold(mutation);
        assert(portable_native_two_pass(0,current,sizeof(current),&changed,transport_table,0)==length && !memcmp(current,baseline,length));
        assert(!portable_native_hold_same(&retained,&changed) && !portable_transport_held(0,transport_table,0));
        assert(portable_native_hold_same(&retained,&portable_native_holds[0])); /* Never rebase a held boundary. */
        if(mutation==1) *(double*)(image+PORTABLE_UNPAUSED_RVA)-=1.0;else mutate_local_hold(2);
        assert(portable_transport_held(0,transport_table,0));
        for(unsigned point=2;point<=3;point++) {
            portable_native_count=0;portable_native_lengths[0]=0;portable_native_failures=0;
            local_mutation=mutation;local_guard_reads=0;local_mutation_at=point;
            assert(!portable_native_capture(0,transport_table,0) && !local_mutation && portable_native_failures==1 && !portable_native_count && !portable_native_lengths[0]);
            assert(strcmp(portable_native_stage,"complete")); /* Preserve the failed read/recheck stage. */
            if(mutation==1) *(double*)(image+PORTABLE_UNPAUSED_RVA)-=1.0;else mutate_local_hold(2);
        }
        portable_native_hold held;
        *(uint64_t*)(image+PORTABLE_UNPAUSED_RVA)=UINT64_C(0x7ff0000000000000);assert(!portable_native_hold_read(&held,transport_table,0));
        *(double*)(image+PORTABLE_UNPAUSED_RVA)=90.0;
        *(uint32_t*)(image+0x38f4ee4)=2;assert(!portable_native_hold_read(&held,transport_table,0));*(uint32_t*)(image+0x38f4ee4)=0;
        *(void**)(image+TABLE_RVA)=NULL;assert(!portable_native_hold_read(&held,transport_table,0));*(void**)(image+TABLE_RVA)=transport_table;
        finish(1);transport_dispose();
        printf("PASS portable local hold: %s, unchanged canonical bytes, retransmit/within-pass/between-pass refusals\n",mutation==1?"unpaused clock":"identical physical banks");
    }
}
int main(int argc,char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const char *contract="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    if(argc==4 && !strcmp(argv[1],"--python-peer")) {
        unsigned port=(unsigned)strtoul(argv[2],NULL,10);assert(port>0 && port<=65535 && pw_hex(argv[3],64));
        python_peer=1;run_transport_case(TC_GO,port,argv[3]);return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--case")) {
        unsigned which=(unsigned)strtoul(argv[2],NULL,10);assert(which<TC_COUNT);run_transport_case(which,0,contract);return 0;
    }
    assert(argc==1);
    for(unsigned i=0;i<TC_COUNT;i++) run_transport_case(i,0,contract);
    run_boot_watchdogs();
    run_local_hold_mutants();
    puts("portable production transport: synthetic memory/real loopback UDP tests pass; no native gameplay claim");return 0;
}
