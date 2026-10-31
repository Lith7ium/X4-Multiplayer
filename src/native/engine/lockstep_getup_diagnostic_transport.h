/* Explicit one-native local diagnostic on the existing joined gate. No new hook.
 * The portable transport and this mode are mutually exclusive; their existing
 * admission/restoration locks also order UI cancellation against continuation. */
static unsigned gd_draining,gd_closed,gd_terminal_started,gd_terminal_finished,gd_terminal_restored;
static LONG64 gd_calls,gd_returns,gd_unwinds,gd_failures;
static const char *gd_stage="off";
static void finish(unsigned);
static void close_connection(void);
static int closed_window(void);
static void gate_original_once(tick_fn,void *,unsigned char);
static int gd_observers_clean(void) {
    return !origin_failures && !origin_unwinds && !origin_ledger.failures && !origin_ledger.post_unwinds && !origin_ledger.delivery_unwinds &&
        !transition_failures && !transition_dropped && !transition_saved_failures && !transition_saved_queue_failures && !transition_direct_unwinds &&
        !world_trace_failures && !world_trace_dropped && !world_trace_early_failures && !world_queue_failures && !world_queue_dropped &&
        !world_identity_failures && !world_hash_failures && !ai_failures && !agent_failures && !ship_failures && !nav_failures &&
        !flight_failures && !spline_failures && !getup_failed();
}
typedef struct {
    uint64_t game,real,issued,report;uintptr_t table;
    char hash[65],command[9];
} gd_hold;
static int gd_hold_read(gd_hold *out) {
    memset(out,0,sizeof(*out));unsigned loading=0;double game,real;
    if(!gd_enabled || !wire_pair() || boot_phase!=3 || boot_options!=GD_OPTIONS || stats.slot!=1 || !gd_observers_clean() ||
       !path_installed || !(path_options&1) || !world_identity_installed || !ai_installed || !ai_full_layout_verified ||
       !ship_installed || !nav_installed || !agent_installed || !flight_installed || !getup_diagnostic_spline_owned() ||
       !world_trace_early || !world_trace_watch_count || !gate_sites_owned() || !getup_hooks_owned() || !world_queue_enabled || world_queue_sites!=7 ||
       !read_local(image+TABLE_RVA,&out->table,8) || !out->table ||
       !read_local((void*)(out->table+0x130),&loading,4) || loading ||
       !read_local(image+GAME_RVA,&out->game,8) || !read_local(image+REAL_RVA,&out->real,8)) return 0;
    for(unsigned i=0;i<3;i++) {uint64_t word;if(!read_local(image+world_queue_entries[i],&word,8) || word!=world_queue_patched[i]) return 0;}
    memcpy(&game,&out->game,8);memcpy(&real,&out->real,8);
    if(!isfinite(game)||!isfinite(real)||game<0||real<0) return 0;
    out->issued=stats.issued;out->report=stats.report_tick;memcpy(out->hash,digest,65);memcpy(out->command,proposal,9);
    return out->issued==stats.completed && out->report==out->issued && gd_hex(out->hash,64) &&
        !strcmp(out->command,out->issued==29?"87010000":"00000000") && game==stats.game && real==stats.real;
}
static void gd_fail(const char *stage) {
    AcquireSRWLockExclusive(&portable_wire_lock);int own=stats.active && !portable_wire_finish_started;
    if(own) {gd_stage=stage;InterlockedIncrement64(&gd_failures);}
    ReleaseSRWLockExclusive(&portable_wire_lock);if(own) finish(78);
}
static int gd_send(const char *packet,int length) {
    int n=send(connection,packet,length,0);return n==length || (n==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK);
}
static void gd_terminal(void) {
    AcquireSRWLockExclusive(&portable_wire_lock);
    int own=!gd_terminal_started;if(own) {gd_terminal_started=1;gd_draining=1;}
    ReleaseSRWLockExclusive(&portable_wire_lock);if(!own) return;
    gd_terminal_restored=stats.restored && dt_restored && !installed && !dt_installed && !path_installed && path_restored &&
        !world_identity_installed && !ai_installed && !agent_installed && !ship_installed && !nav_installed && !flight_installed && !spline_installed &&
        !origin_sites && !origin_open && !origin_restore_open && !origin_ledger.open && !origin_ledger.stop_open &&
        !transition_sites && !transition_open && !transition_restore_open && !world_queue_sites && !world_queue_open && !world_queue_restore_open;
    if(!gd_terminal_restored || !gd_observers_clean() || stats.completed!=GD_LIMIT || stats.issued!=GD_LIMIT ||
       gd_calls!=GD_LIMIT+1 || gd_returns!=gd_calls || gd_unwinds || stats.reason!=1 || getup_ledger.processed!=GD_LIMIT ||
       getup_ledger.calls!=1 || getup_ledger.returns!=1 || !getup_ledger.accepted || getup_ledger.open || getup_ledger.stop_open)
        InterlockedIncrement64(&gd_failures);
    char packet[GD_PACKET_MAX],closed[GD_PACKET_MAX],reply[GD_PACKET_MAX];
    int length=gd_terminal_packet(packet,sizeof(packet),"NGDDONE",secret,getup_ledger.run,(unsigned)stats.completed,
        stats.reason,gd_terminal_restored,gd_calls,gd_returns,gd_unwinds,gd_failures);
    int expected=gd_terminal_packet(closed,sizeof(closed),"NGDCLOSED",secret,getup_ledger.run,(unsigned)stats.completed,
        stats.reason,gd_terminal_restored,gd_calls,gd_returns,gd_unwinds,gd_failures);
    if(!length || !expected) {gd_stage="terminal_encoding";InterlockedIncrement64(&gd_failures);goto done;}
    gd_stage="terminal_wait";ULONGLONG start=GetTickCount64(),last=0;
    while(GetTickCount64()-start<5000) {
        ULONGLONG now=GetTickCount64();if(!last || now-last>=100) {if(!gd_send(packet,length)) break;last=now;}
        int n=recv(connection,reply,sizeof(reply),0);
        if(n==SOCKET_ERROR) {int error=WSAGetLastError();if(error==WSAETIMEDOUT || error==WSAEWOULDBLOCK) continue;break;}
        unsigned reason=0;int result=gd_reply(reply,(size_t)n,secret,getup_ledger.run,(unsigned)stats.completed,closed,&reason);
        if(result==GD_CLOSED) {gd_closed=1;gd_stage="closed";goto done;}
        break;
    }
    gd_stage="terminal_unacknowledged";InterlockedIncrement64(&gd_failures);
done:close_connection();gd_draining=0;InterlockedExchange((volatile LONG*)&gd_terminal_finished,1);
}
static void gd_original(tick_fn original,void *frame,unsigned char loading) {
    int claimed=0;AcquireSRWLockExclusive(&portable_wire_lock);
    if(stats.active && !portable_wire_finish_started) {++gd_calls;claimed=1;}
    ReleaseSRWLockExclusive(&portable_wire_lock);
    if(!claimed) {
        AcquireSRWLockShared(&portable_wire_restore_lock);
        if(!InterlockedCompareExchange(&portable_wire_restore_complete,0,0)) InterlockedIncrement64(&gd_failures);
        ReleaseSRWLockShared(&portable_wire_restore_lock);InterlockedIncrement64(&gd_calls);
    }
    int normal=0;
    __try {original(frame,loading);normal=1;}
    __finally {if(AbnormalTermination()) {InterlockedIncrement64(&gd_unwinds);InterlockedIncrement64(&gd_failures);} else InterlockedIncrement64(&gd_returns);}
    if(normal && !stats.active) {
        /* A stop ordered after this continuation claim may still be restoring.
         * The original was allowed to run, but DONE must await that attempt. */
        AcquireSRWLockShared(&portable_wire_restore_lock);
        if(!InterlockedCompareExchange(&portable_wire_restore_complete,0,0)) InterlockedIncrement64(&gd_failures);
        ReleaseSRWLockShared(&portable_wire_restore_lock);gd_terminal();
    }
}
static void gd_tick(tick_fn original,void *frame,unsigned char loading) {
    unsigned tick=(unsigned)stats.completed;char packet[GD_PACKET_MAX],reply[GD_PACKET_MAX];gd_hold before,current;
    if(stats.completed>GD_LIMIT || !gd_hold_read(&before)) {gd_fail("initial_hold");goto original_once;}
    int length=gd_state(packet,sizeof(packet),secret,getup_ledger.run,tick,before.game,before.real,before.hash,before.command);
    if(!length) {gd_fail("state_encoding");goto original_once;}
    gd_stage="waiting";ULONGLONG start=GetTickCount64(),last=0;
    while(InterlockedCompareExchange((volatile LONG*)&stats.active,0,0)) {
        ULONGLONG now=GetTickCount64();
        if(closed_window()) {gd_fail("window_closed");break;}
        if(now-start>timeout_ms) {gd_fail("timeout");break;}
        if(!last || now-last>=100) {
            if(!gd_hold_read(&current) || memcmp(&current,&before,sizeof(before))) {gd_fail("held_changed");break;}
            if(!gd_send(packet,length)) {gd_fail("send");break;}last=now;++stats.waits;
        }
        int n=recv(connection,reply,sizeof(reply),0);
        if(n==SOCKET_ERROR) {int error=WSAGetLastError();if(error==WSAETIMEDOUT || error==WSAEWOULDBLOCK) continue;gd_fail("receive");break;}
        unsigned reason=0;int result=gd_reply(reply,(size_t)n,secret,getup_ledger.run,tick,NULL,&reason);
        if(result!=GD_GO && result!=GD_STOP) {gd_fail("invalid_reply");break;}
        uint64_t epoch;AcquireSRWLockShared(&portable_wire_lock);epoch=portable_wire_epoch;ReleaseSRWLockShared(&portable_wire_lock);
        int valid=gd_hold_read(&current) && !memcmp(&current,&before,sizeof(before)),accepted=0;
        AcquireSRWLockExclusive(&portable_wire_lock);
        if(stats.active && !portable_wire_finish_started && epoch==portable_wire_epoch) {
            /* Cancellation is excluded here; these bounded guarded reads do
             * not call a getter or establish an engine-worker lifetime lease. */
            if(valid && gd_hold_read(&current) && !memcmp(&current,&before,sizeof(before))) {
                ++portable_wire_epoch;accepted=1;
                if(result==GD_GO) {strcpy(inputs[0],gd_command(tick+1));strcpy(inputs[1],"00000000");previous_game=stats.game;++stats.issued;gd_stage="permitted";}
                else gd_draining=1;
            }
        }
        ReleaseSRWLockExclusive(&portable_wire_lock);
        if(!accepted) {gd_fail("changed_at_reply");break;}
        if(result==GD_STOP) finish(reason);break;
    }
original_once:gate_original_once(original,frame,loading);
}
