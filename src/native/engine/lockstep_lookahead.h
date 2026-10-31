/* Opt-in protocol 4: the protocol 3 input wire with a lookahead window of
 * k = 1..GATE_LOOKAHEAD_MAX ticks (docs/LOCKSTEP-NSLOT-WIRE.txt, LOOKAHEAD).
 * The relay releases the permit for tick T once every client reported tick
 * T-1-k (tick 0 for T <= k+1), and that permit carries exactly the proposals
 * sealed with the reports of tick T-1-k (all empty for T <= k). A client thus
 * runs at most k ticks ahead of the last tick every client reported, and every
 * input it applies at T arrived with the permit for T: it never guesses one.
 * The relay still compares each tick's digests, at most k ticks late.
 * Client window: the sealed reports of ticks completed-k..completed (each sent
 * once when sealed and resent while waiting, at most once per 100 ms) and the
 * permits received for ticks completed+1..completed+1+k, read without blocking
 * every frame (so a relay stop is seen at once). Fail closed: a permit
 * beyond that window stops with reason 102, two different permits for one
 * tick with 103, and a permit returning a different local proposal with 56.
 * k = 0 is protocol 3 and never reaches this code. */
#define LOOKAHEAD_RING (GATE_LOOKAHEAD_MAX+1u)
#define LOOKAHEAD_STOP_AHEAD 102u    /* a permit beyond the window, or for a tick without proposals */
#define LOOKAHEAD_STOP_CONFLICT 103u /* two different permits for one tick */
#define LOOKAHEAD_RESEND_MS 100u
typedef struct {
    uint64_t tick;ULONGLONG sent;size_t length;int used;
    char packet[GATE_STATE_MAX];char blob[GATE_BLOB_HEX_MAX+1];
} lookahead_report;
typedef struct {uint64_t tick;int used;char blobs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];} lookahead_permit;
static lookahead_report lookahead_reports[LOOKAHEAD_RING];
static lookahead_permit lookahead_permits[LOOKAHEAD_RING];
static char lookahead_scratch[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];
static uint64_t lookahead_ready,lookahead_ahead_max,lookahead_idle,lookahead_resends,lookahead_stale,lookahead_duplicates;
static void lookahead_reset(void) {
    memset(lookahead_reports,0,sizeof(lookahead_reports));memset(lookahead_permits,0,sizeof(lookahead_permits));
    lookahead_ready=lookahead_ahead_max=lookahead_idle=lookahead_resends=lookahead_stale=lookahead_duplicates=0;
}
/* Canonical decimal (no sign, no leading zero) at s[0..n): digits used, or 0. */
static size_t lookahead_decimal(const char *s,size_t n,uint64_t *out) {
    size_t i=0;uint64_t value=0;
    for(;i<n && s[i]>='0' && s[i]<='9';i++) {
        unsigned digit=(unsigned)(s[i]-'0');
        if((i && s[0]=='0') || value>(UINT64_MAX-digit)/10) return 0;
        value=value*10+digit;
    }
    if(i) *out=value;
    return i;
}
/* Keep the report of tick: its datagram and the sealed local proposal. The
 * ring holds k+1 consecutive ticks, so their slots never collide. */
static int lookahead_seal(uint64_t tick,const char *packet,size_t length,const char *blob) {
    size_t size=blob?strlen(blob):GATE_BLOB_HEX_MAX+1;
    if(!packet || !length || length>=GATE_STATE_MAX || !gate_blob_valid(blob,size)) return 0;
    lookahead_report *r=&lookahead_reports[tick%LOOKAHEAD_RING];
    r->tick=tick;r->used=1;r->sent=0;r->length=length;memcpy(r->packet,packet,length);r->packet[length]=0;memcpy(r->blob,blob,size+1);
    return 1;
}
static void lookahead_send(lookahead_report *r,ULONGLONG now) {
    send(connection,r->packet,(int)r->length,0);r->sent=now;++stats.waits;
}
/* While waiting: every report of the window not sent for 100 ms. The report
 * of tick completed-k is the one the awaited permit needs. */
static void lookahead_resend(uint64_t completed,ULONGLONG now) {
    for(uint64_t t=completed>wire_lookahead?completed-wire_lookahead:0;t<=completed;t++) {
        lookahead_report *r=&lookahead_reports[t%LOOKAHEAD_RING];
        if(r->used && r->tick==t && now-r->sent>=LOOKAHEAD_RESEND_MS) {lookahead_send(r,now);++lookahead_resends;}
    }
}
/* NGGO|4|<token>|<slot>|<T>|<k>|<blob 1>|...|<blob N>. 1: held (or an
 * identical copy); 0: not this client's permit, malformed, or for a tick
 * already run (ignored); -1: a different local proposal (56); -2: beyond the
 * window or non-empty proposals before tick 0 (102); -3: a different copy of a
 * held permit (103). */
static int lookahead_permit_receive(const char *reply,size_t length,uint64_t completed) {
    char head[64],prefix[96];uint64_t tick=0,k=wire_lookahead;
    int n=snprintf(head,sizeof(head),"NGGO|4|%s|%u|",secret,stats.slot);
    if(!reply || n<=0 || (size_t)n>=sizeof(head) || length>=GATE_PERMIT_MAX || length<=(size_t)n || memcmp(reply,head,(size_t)n) ||
       !lookahead_decimal(reply+n,length-(size_t)n,&tick)) return 0;
    int p=snprintf(prefix,sizeof(prefix),"%s%llu|%u",head,(unsigned long long)tick,wire_lookahead);
    if(p<=0 || (size_t)p>=sizeof(prefix) || length<(size_t)p || memcmp(reply,prefix,(size_t)p)) return 0;
    if(tick<=completed) {++lookahead_stale;return 0;}
    if(tick-completed>1+k) return -2;
    const char *own="";
    if(tick>k) {
        uint64_t source=tick-1-k;const lookahead_report *r=&lookahead_reports[source%LOOKAHEAD_RING];
        if(!r->used || r->tick!=source) return -2;
        own=r->blob;
    }
    int ok=gate_permit_blobs(reply,length,prefix,stats.slot,wire_clients,own,lookahead_scratch);
    if(ok<=0) return ok;
    if(tick<=k) for(unsigned i=0;i<wire_clients;i++) if(lookahead_scratch[i][0]) return -2;
    lookahead_permit *e=&lookahead_permits[tick%LOOKAHEAD_RING];
    if(e->used && e->tick==tick) {
        for(unsigned i=0;i<wire_clients;i++) if(strcmp(e->blobs[i],lookahead_scratch[i])) return -3;
        ++lookahead_duplicates;return 1;
    }
    e->tick=tick;e->used=1;
    for(unsigned i=0;i<wire_clients;i++) memcpy(e->blobs[i],lookahead_scratch[i],strlen(lookahead_scratch[i])+1);
    if(tick-completed>lookahead_ahead_max) lookahead_ahead_max=tick-completed;
    return 1;
}
/* The held permit for tick becomes s.inputs (the proposals of tick-1-k). */
static int lookahead_take(uint64_t tick) {
    lookahead_permit *e=&lookahead_permits[tick%LOOKAHEAD_RING];
    if(!e->used || e->tick!=tick) return 0;
    for(unsigned i=0;i<wire_clients;i++) memcpy(blob_inputs[i],e->blobs[i],strlen(e->blobs[i])+1);
    e->used=0;return 1;
}
/* NGSTOP|4|<token>|<slot>|<tick> for any tick this client reported: the relay
 * answers the report it is holding, which may lag the newest by up to k. */
static int lookahead_stop(const char *reply,size_t length,uint64_t completed) {
    char head[64];uint64_t tick=0;size_t digits;
    int n=snprintf(head,sizeof(head),"NGSTOP|4|%s|%u|",secret,stats.slot);
    if(!reply || n<=0 || (size_t)n>=sizeof(head) || length<=(size_t)n || memcmp(reply,head,(size_t)n)) return 0;
    digits=lookahead_decimal(reply+n,length-(size_t)n,&tick);
    return digits && (size_t)n+digits==length && tick<=completed;
}
/* One received datagram: 0 to go on, otherwise the stop reason. */
static unsigned lookahead_datagram(char *reply,int count) {
    reply[count]=0;
    if((size_t)count!=strlen(reply)) return 0;
    if(lookahead_stop(reply,(size_t)count,stats.completed)) return 1;
    int accepted=lookahead_permit_receive(reply,(size_t)count,stats.completed);
    return accepted==-1?56u:accepted==-2?LOOKAHEAD_STOP_AHEAD:accepted==-3?LOOKAHEAD_STOP_CONFLICT:0u;
}
/* Every datagram already queued, without blocking (bounded per call): held
 * permits are counted when they arrive and a relay stop is seen next frame. */
static unsigned lookahead_drain(char *reply,size_t capacity) {
    for(unsigned i=0;i<4*LOOKAHEAD_RING;i++) {
        u_long queued=0;
        if(ioctlsocket(connection,FIONREAD,&queued) || !queued) return 0;
        int count=recv(connection,reply,(int)capacity-1,0);
        if(count<0) {int error=WSAGetLastError();return error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK?8u:0u;}
        unsigned reason=lookahead_datagram(reply,count);
        if(reason) return reason;
    }
    return 0;
}
/* 0: the permit for completed+1 is taken; otherwise the stop reason. */
static unsigned lookahead_wait(char *reply,size_t capacity) {
    uint64_t want=stats.completed+1;ULONGLONG start=GetTickCount64();int blocked=0;
    for(;;) {
        ULONGLONG now=GetTickCount64();
        double drift=fabs(*(double*)(image+GAME_RVA)-stats.game);
        if(drift>stats.wait_game_delta) stats.wait_game_delta=drift;
        if(drift>0) return 5;
        if(closed_window()) return 6;
        unsigned reason=lookahead_drain(reply,capacity);
        if(reason) return reason;
        if(lookahead_take(want)) {if(!blocked) ++lookahead_ready;return 0;}
        if(now-start>timeout_ms) return 7;
        lookahead_resend(stats.completed,now);
        blocked=1;
        int count=recv(connection,reply,(int)capacity-1,0);
        if(count<0) {
            int error=WSAGetLastError();
            if(error!=WSAETIMEDOUT&&error!=WSAEWOULDBLOCK) return 8;
            ++lookahead_idle;continue;
        }
        reason=lookahead_datagram(reply,count);
        if(reason) return reason;
    }
}
/* One gated frame: seal and send the report of stats.completed, then wait for
 * the permit of the next tick (often already held). */
static unsigned lookahead_frame(const char *packet,size_t length,char *reply,size_t capacity) {
    if(!lookahead_seal(stats.completed,packet,length,blob_proposal)) return 56;
    lookahead_send(&lookahead_reports[stats.completed%LOOKAHEAD_RING],GetTickCount64());
    return lookahead_wait(reply,capacity);
}
static int lookahead_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"lookahead=%u ready=%llu ahead_max=%llu idle=%llu resends=%llu stale=%llu duplicates=%llu",wire_lookahead,
        (unsigned long long)lookahead_ready,(unsigned long long)lookahead_ahead_max,(unsigned long long)lookahead_idle,(unsigned long long)lookahead_resends,
        (unsigned long long)lookahead_stale,(unsigned long long)lookahead_duplicates);
    return n>0 && (size_t)n<capacity?n:0;
}
