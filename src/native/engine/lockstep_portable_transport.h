/* Explicit opt-in Windows NGP transport, on the existing joined main barrier.
 * No new engine hook, player command, state correction or Lua polling transport. */
#include "lockstep_portable_wire.h"
static unsigned portable_wire_enabled,portable_wire_draining,portable_wire_closed;
static unsigned portable_wire_finish_started;
static unsigned portable_wire_startup_ms=600000;
static uint64_t portable_wire_epoch;
static SRWLOCK portable_wire_lock=SRWLOCK_INIT;
static SRWLOCK portable_wire_restore_lock=SRWLOCK_INIT;
static volatile LONG portable_wire_restore_complete;
static char portable_wire_agent[65];
static const char *portable_wire_stage="off";
static unsigned portable_wire_terminal_restored;
static void finish(unsigned reason);
static void close_connection(void);
static int closed_window(void);
static void gate_original_once(tick_fn original,void *frame,unsigned char loading);

static int portable_transport_config(const char *agent,size_t length,unsigned startup) {
    if(!agent || length!=64 || !pw_hex(agent,64) || startup<timeout_ms || startup>900000 || startup<1000) return 0;
    if(portable_wire_enabled) return stats.active && !portable_wire_finish_started && startup==portable_wire_startup_ms && !memcmp(portable_wire_agent,agent,64);
    /* The first mixed-platform contract is the untouched saved fixture. Do not
     * silently combine legacy diagnostic interventions with the native peer. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || stats.slot!=1 || !wire_pair() ||
       GetCurrentThreadId()!=ui_thread || !portable_native_configured || portable_native_failures ||
       boot_options!=70368744177664ULL || strcmp(proposal,"00000000")) return 0;
    memcpy(portable_wire_agent,agent,64);portable_wire_startup_ms=startup;portable_wire_enabled=1;portable_wire_stage="configured";return 1;
}
static int portable_transport_digest(const unsigned char *bytes,unsigned length,char out[65]) {
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char raw[32];int ok=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
       BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0 ||
       BCryptHashData(hash,(PUCHAR)bytes,length,0)<0 || BCryptFinishHash(hash,raw,32,0)<0) goto done;
    hex32(raw,out);ok=1;
done:if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
static void portable_transport_fail(const char *stage) {
    /* Cancellation may restore owned hooks while an unlocked hold read runs.
     * Publish a failure only if it wins the existing admission/stop lock. */
    AcquireSRWLockExclusive(&portable_wire_lock);
    int failed=stats.active && !portable_wire_finish_started;
    if(failed) {portable_wire_stage=stage;InterlockedIncrement64(&portable_native_failures);}
    ReleaseSRWLockExclusive(&portable_wire_lock);
    if(failed) finish(76);
}
static int portable_transport_send(const char *packet,int length) {
    int sent=send(connection,packet,length,0);
    if(sent==length) return 1;
    if(sent==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK) return 1; /* next immutable retransmit */
    return 0;
}
static int portable_transport_held(unsigned tick,void *frame,unsigned char loading) {
    portable_native_hold held;unsigned char bytes[PORTABLE_FRAME_CAPACITY];
    if(tick>portable_native_limit || tick>PORTABLE_CAPTURE_LIMIT || portable_native_count<=(LONG)tick ||
       !portable_native_lengths[tick]) return 0;
    size_t length=portable_native_two_pass(tick,bytes,sizeof(bytes),&held,frame,loading);
    return length==portable_native_lengths[tick] && !memcmp(bytes,portable_native_bytes[tick],length) &&
        portable_native_hold_same(&held,&portable_native_holds[tick]);
}
static int portable_transport_accept(unsigned tick,int result,void *frame,unsigned char loading) {
    int accepted=0;uint64_t epoch;
    AcquireSRWLockExclusive(&portable_wire_lock);
    if(!stats.active || portable_wire_finish_started || connection==INVALID_SOCKET) {ReleaseSRWLockExclusive(&portable_wire_lock);return 0;}
    epoch=portable_wire_epoch;ReleaseSRWLockExclusive(&portable_wire_lock);
    /* The diagnostic reread is outside the cancellation lock. Native producers
     * are not excluded by this lock; it only orders this client's grant/stop. */
    int held=portable_transport_held(tick,frame,loading);
    AcquireSRWLockExclusive(&portable_wire_lock);
    if(stats.active && !portable_wire_finish_started && connection!=INVALID_SOCKET && epoch==portable_wire_epoch) {
        if(!held || !portable_native_hold_current(&portable_native_holds[tick],frame,loading)) accepted=-1;
        else if(result==PW_GO) {previous_game=stats.game;++stats.issued;++portable_wire_epoch;portable_wire_stage="permitted";accepted=1;}
        else {
            portable_wire_draining=1;stats.reason=result==PW_STOP?1:76;
            ++portable_wire_epoch;InterlockedExchange((volatile LONG*)&stats.active,0);accepted=1;
        }
    }
    ReleaseSRWLockExclusive(&portable_wire_lock);return accepted;
}
static void portable_transport_terminal(unsigned stop_kind) {
    portable_wire_terminal_restored=stats.restored && dt_restored && !dt_installed && !installed && !world_identity_installed;
    char packet[256],reply[256];
    if(stop_kind==PW_STOP && (stats.completed!=portable_native_limit ||
       portable_native_calls!=(LONG64)portable_native_limit+1 || portable_native_returns!=portable_native_calls ||
       portable_native_count!=(LONG)portable_native_limit+1 || !portable_wire_terminal_restored ||
       portable_native_failures || stats.reason!=1)) {
        portable_wire_stage="terminal_receipt_failed";InterlockedIncrement64(&portable_native_failures);goto done;
    }
    int length=pw_done(packet,sizeof(packet),secret,stats.slot,(unsigned)stats.completed,
                       (unsigned)portable_native_calls,(unsigned)portable_native_returns,portable_wire_terminal_restored);
    if(!length) {portable_wire_stage="terminal_counts";InterlockedIncrement64(&portable_native_failures);goto done;}
    if(stop_kind==PW_FAILED) {portable_transport_send(packet,length);portable_wire_stage="peer_failed";goto done;}
    portable_wire_stage="terminal_wait";
    ULONGLONG start=GetTickCount64(),last=0;
    while(GetTickCount64()-start<5000) {
        ULONGLONG now=GetTickCount64();
        if(!last || now-last>=100) {if(!portable_transport_send(packet,length)) break;last=now;}
        int count=recv(connection,reply,sizeof(reply)-1,0);
        if(count==SOCKET_ERROR) {int error=WSAGetLastError();if(error==WSAETIMEDOUT || error==WSAEWOULDBLOCK) continue;break;}
        int result=pw_reply(reply,(size_t)count,secret,stats.slot,(unsigned)stats.completed,portable_native_limit,1);
        if(result==PW_CLOSED) {portable_wire_closed=1;portable_wire_stage="closed";goto done;}
        if(result==PW_FAILED || result==PW_INVALID) break;
    }
    portable_wire_stage="terminal_not_acknowledged";InterlockedIncrement64(&portable_native_failures);
done:
    close_connection();portable_wire_draining=0;
}
static void portable_transport_tick(tick_fn original,void *frame,unsigned char loading) {
    unsigned tick=(unsigned)stats.completed,stop_kind=0;
    char packet[PORTABLE_WIRE_PACKET_MAX],hello[512],reply[256],diagnostic[65];uint64_t real_bits;
    if(tick>portable_native_limit || portable_native_count!=(LONG)tick+1 ||
       !portable_transport_digest(portable_native_bytes[tick],portable_native_lengths[tick],diagnostic)) {
        portable_transport_fail("frame_unavailable");goto original_once;
    }
    memcpy(&real_bits,&stats.real,8);
    int length=pw_state(packet,sizeof(packet),secret,stats.slot,real_bits,diagnostic,
                        portable_native_bytes[tick],portable_native_lengths[tick]);
    int hello_length=pw_hello(hello,sizeof(hello),secret,stats.slot,"windows",EXE_SHA,portable_wire_agent,
                              portable_native_contract_text,PORTABLE_SCHEMA_SHA);
    if(!length || !hello_length) {portable_transport_fail("packet_encoding");goto original_once;}
    stats.report_tick=stats.completed;memcpy(digest,diagnostic,65);portable_wire_stage="waiting";
    ULONGLONG start=GetTickCount64(),last=0;
    while(InterlockedCompareExchange((volatile LONG*)&stats.active,0,0)) {
        ULONGLONG now=GetTickCount64();
        if(!portable_native_hold_current(&portable_native_holds[tick],frame,loading)) {portable_transport_fail("held_local_guard_changed");break;}
        if(closed_window()) {portable_transport_fail("window_closed");break;}
        if(now-start>(tick?timeout_ms:portable_wire_startup_ms)) {portable_transport_fail("peer_timeout");break;}
        if(!last || now-last>=100) {
            if(!portable_transport_held(tick,frame,loading)) {portable_transport_fail("held_native_state_changed");break;}
            if(!portable_transport_send(hello,hello_length) || !portable_transport_send(packet,length)) {portable_transport_fail("send_failed");break;}
            last=now;++stats.waits;
        }
        int count=recv(connection,reply,sizeof(reply)-1,0);
        if(count==SOCKET_ERROR) {
            int error=WSAGetLastError();if(error==WSAETIMEDOUT || error==WSAEWOULDBLOCK) continue;
            portable_transport_fail("receive_failed");break;
        }
        int result=pw_reply(reply,(size_t)count,secret,stats.slot,tick,portable_native_limit,0);
        if(result==PW_IGNORE) continue;
        if(result==PW_GO || result==PW_STOP || result==PW_FAILED) {
            int accepted=portable_transport_accept(tick,result,frame,loading);
            if(accepted<0) portable_transport_fail("state_changed_at_permit");
            if(accepted<=0 || result==PW_GO) break;
            stop_kind=(unsigned)result;
            if(result==PW_FAILED) {portable_wire_stage="peer_failed";InterlockedIncrement64(&portable_native_failures);}
            finish(result==PW_STOP?1:76);break;
        }
        portable_transport_fail("invalid_permit");break;
    }
original_once:
    gate_original_once(original,frame,loading);
    if(portable_wire_draining) portable_transport_terminal(stop_kind);
}
