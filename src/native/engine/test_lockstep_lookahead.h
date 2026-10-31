/* Protocol 4 client window (lockstep_lookahead.h): parsing and window rules,
 * then the wait loop against a loopback UDP socket standing in for the relay.
 * No engine hooks or native simulation are exercised here. */
#define LA_TOKEN "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
static SOCKET la_relay=INVALID_SOCKET;
static struct sockaddr_in la_client;
static char la_reply[GATE_PERMIT_MAX];
static int la_permit(const char *text) {return lookahead_permit_receive(text,strlen(text),stats.completed);}
static void la_state(char *out,size_t capacity,uint64_t tick,const char *blob) {
    int n=snprintf(out,capacity,"NGSTATE|4|%s|%u|%llu|%016llx|%016llx|%s|%u|%u|%s",LA_TOKEN,stats.slot,(unsigned long long)tick,
                   0ULL,0ULL,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",wire_clients,wire_lookahead,blob);
    assert(n>0 && (size_t)n<capacity);
}
/* The relay's view: slot s proposes la_blob(s,t) at tick t; this client is slot 2. */
static void la_blob(unsigned slot,uint64_t tick,char out[GATE_BLOB_HEX_MAX+1]) {
    if((tick+slot)%3==0) {out[0]=0;return;}
    snprintf(out,GATE_BLOB_HEX_MAX+1,"%02x%02x",slot,(unsigned)(tick&0xff));
}
static void la_permit_text(char *out,size_t capacity,uint64_t tick) {
    int n=snprintf(out,capacity,"NGGO|4|%s|%u|%llu|%u",LA_TOKEN,stats.slot,(unsigned long long)tick,wire_lookahead);
    for(unsigned s=1;s<=wire_clients;s++) {
        char blob[GATE_BLOB_HEX_MAX+1];blob[0]=0;
        if(tick>wire_lookahead) la_blob(s,tick-1-wire_lookahead,blob);
        n+=snprintf(out+n,capacity-(size_t)n,"|%s",blob);
    }
    assert(n>0 && (size_t)n<capacity);
}
static void la_send(const char *text,size_t length) {
    assert(sendto(la_relay,text,(int)length,0,(struct sockaddr*)&la_client,sizeof(la_client))==(int)length);
}
static void la_send_permit(uint64_t tick) {char text[GATE_PERMIT_MAX];la_permit_text(text,sizeof(text),tick);la_send(text,strlen(text));}
/* Count the NGSTATE|4 reports the relay socket received, per tick 0..15. */
static unsigned la_drain(unsigned counts[16]) {
    char data[GATE_STATE_MAX+16];unsigned total=0;memset(counts,0,16*sizeof(unsigned));
    for(;;) {
        int n=recv(la_relay,data,sizeof(data)-1,0);
        if(n<0) break;
        data[n]=0;char head[64];snprintf(head,sizeof(head),"NGSTATE|4|%s|2|",LA_TOKEN);
        assert(!strncmp(data,head,strlen(head)));
        uint64_t tick=0;assert(lookahead_decimal(data+strlen(head),(size_t)n-strlen(head),&tick) && tick<16);++counts[tick];++total;
    }
    return total;
}
/* The first copy of report 4 was lost: the relay answers its resend with the
 * permit that report releases (4+1+k). */
static DWORD WINAPI la_resend_relay(LPVOID unused) {
    (void)unused;char data[GATE_STATE_MAX+16],head[64];unsigned copies=0;ULONGLONG start=GetTickCount64();
    snprintf(head,sizeof(head),"NGSTATE|4|%s|2|4|",LA_TOKEN);
    while(GetTickCount64()-start<3000) {
        int n=recv(la_relay,data,sizeof(data)-1,0);
        if(n<0) continue;
        data[n]=0;
        if(!strncmp(data,head,strlen(head)) && ++copies==1) {la_send_permit(4+1+wire_lookahead);return 0;}
    }
    return 1;
}
static void test_lookahead_parse(void) {
    uint64_t v=7;
    assert(lookahead_decimal("0",1,&v)==1 && v==0 && lookahead_decimal("12|",3,&v)==2 && v==12);
    assert(!lookahead_decimal("01",2,&v) && !lookahead_decimal("",0,&v) && !lookahead_decimal("x",1,&v) && !lookahead_decimal("-1",2,&v));
    assert(lookahead_decimal("18446744073709551615",20,&v)==20 && v==UINT64_MAX && !lookahead_decimal("18446744073709551616",20,&v));
    strcpy(secret,LA_TOKEN);stats.slot=2;wire_version=3;wire_clients=3;wire_lookahead=2;stats.completed=0;
    lookahead_reset();
    char packet[GATE_STATE_MAX];la_state(packet,sizeof(packet),0,"0a");
    assert(!lookahead_seal(0,packet,strlen(packet),"0A") && !lookahead_seal(0,packet,strlen(packet),NULL) &&
           !lookahead_seal(0,packet,GATE_STATE_MAX,"0a") && !lookahead_seal(0,packet,0,"0a"));
    assert(lookahead_seal(0,packet,strlen(packet),"0a") && lookahead_reports[0].tick==0 && !strcmp(lookahead_reports[0].blob,"0a"));
    /* Before tick 0 there are no proposals: permits 1..k carry empty blobs only. */
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|1|2|||")==1 && lookahead_take(1) && !lookahead_take(1));
    assert(!blob_inputs[0][0] && !blob_inputs[1][0] && !blob_inputs[2][0]);
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|2|2|ff||")==-2);
    /* Permit k+1 carries the proposals of tick 0, including this client's own. */
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|3|2|01|0b|")==-1 && la_permit("NGGO|4|" LA_TOKEN "|2|3|2|01||")==-1);
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|3|2|01|0a|")==1 && lookahead_ahead_max==3);
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|3|2|01|0a|")==1 && lookahead_duplicates==1);    /* identical copy */
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|3|2|02|0a|")==-3);                              /* changed copy */
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|4|2|01|0a|")==-2);                              /* beyond completed+1+k */
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|99|2|01|0a|")==-2);
    /* Not this client's permit or malformed: ignored. */
    const char *ignored[]={"NGGO|4|" LA_TOKEN "|2|2|1|||","NGGO|4|" LA_TOKEN "|2|2|23|||","NGGO|4|" LA_TOKEN "|1|2|2|||",
        "NGGO|4|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb|2|2|2|||","NGGO|3|" LA_TOKEN "|2|2|||","NGGO|4|" LA_TOKEN "|2|02|2|||",
        "NGGO|4|" LA_TOKEN "|2|2|2||","NGGO|4|" LA_TOKEN "|2|2|2||||","NGGO|4|" LA_TOKEN "|2|2|2|AA||","NGGO|4|" LA_TOKEN "|2|2|2|0||",
        "NGGO|4|" LA_TOKEN "|2|2|2","NGGO|4|" LA_TOKEN "|2|2|","NGGO|4|" LA_TOKEN "|2||2|||","NGGO|4|" LA_TOKEN "|2|+2|2|||",
        "NGSTOP|4|" LA_TOKEN "|2|0",""};
    for(unsigned i=0;i<sizeof(ignored)/sizeof(*ignored);i++) assert(la_permit(ignored[i])==0);
    assert(!lookahead_permit_receive(NULL,0,0) && !lookahead_permit_receive("NGGO|4|" LA_TOKEN "|2|2|2|||",GATE_PERMIT_MAX,0));
    assert(la_permit("NGGO|4|" LA_TOKEN "|2|2|2|||")==1);
    stats.completed=1;assert(la_permit("NGGO|4|" LA_TOKEN "|2|1|2|||")==0 && lookahead_stale==1);  /* already run */
    assert(lookahead_take(2) && !lookahead_take(4));stats.completed=2;
    assert(lookahead_take(3) && !strcmp(blob_inputs[0],"01") && !strcmp(blob_inputs[1],"0a") && !blob_inputs[2][0]);
    /* NGSTOP for any tick this client reported; never for a later one. */
    assert(lookahead_stop("NGSTOP|4|" LA_TOKEN "|2|0",strlen("NGSTOP|4|" LA_TOKEN "|2|0"),2));
    assert(lookahead_stop("NGSTOP|4|" LA_TOKEN "|2|2",strlen("NGSTOP|4|" LA_TOKEN "|2|2"),2));
    const char *stops[]={"NGSTOP|4|" LA_TOKEN "|2|3","NGSTOP|4|" LA_TOKEN "|2|02","NGSTOP|3|" LA_TOKEN "|2|0","NGSTOP|4|" LA_TOKEN "|2|0x",
        "NGSTOP|4|" LA_TOKEN "|1|0","NGSTOP|4|" LA_TOKEN "|2|","NGSTOP|4|" LA_TOKEN "|2|0|"};
    for(unsigned i=0;i<sizeof(stops)/sizeof(*stops);i++) assert(!lookahead_stop(stops[i],strlen(stops[i]),2));
    /* Every window size over 64 ticks: permits for completed+1..completed+1+k
     * arrive in any order and repeated; the taken inputs are exactly the
     * proposals of tick T-1-k, and the ring never confuses two ticks. */
    for(unsigned k=1;k<=GATE_LOOKAHEAD_MAX;k++) {
        wire_lookahead=k;lookahead_reset();uint64_t next=1,seed=k*0x9e3779b97f4a7c15ULL;
        for(uint64_t c=0;c<64;c++) {
            char own[GATE_BLOB_HEX_MAX+1];la_blob(2,c,own);la_state(packet,sizeof(packet),c,own);
            stats.completed=c;assert(lookahead_seal(c,packet,strlen(packet),own));
            for(;next<=c+1+k;next++) {
                /* next itself plus a repeat of next-1, in either order */
                seed=seed*6364136223846793005ULL+1442695040888963407ULL;
                uint64_t ticks[2]={next,next-1};unsigned first=(unsigned)(seed>>33)&1;
                for(unsigned j=0;j<2;j++) {
                    uint64_t tick=ticks[j^first];char text[GATE_PERMIT_MAX];la_permit_text(text,sizeof(text),tick);
                    int got=la_permit(text);assert(got==1 || (got==0 && tick<=c));
                }
            }
            assert(lookahead_take(c+1));
            for(unsigned s=1;s<=wire_clients;s++) {
                char expected[GATE_BLOB_HEX_MAX+1];expected[0]=0;
                if(c+1>k) la_blob(s,c-k,expected);
                assert(!strcmp(blob_inputs[s-1],expected));
            }
            char beyond[GATE_PERMIT_MAX];la_permit_text(beyond,sizeof(beyond),c+2+k);assert(la_permit(beyond)==-2);
        }
    }
    /* The largest protocol 4 report and permit stay inside the native buffers. */
    char big[GATE_BLOB_HEX_MAX+1];memset(big,'e',GATE_BLOB_HEX_MAX);big[GATE_BLOB_HEX_MAX]=0;
    wire_clients=8;wire_lookahead=GATE_LOOKAHEAD_MAX;stats.slot=8;
    int n=snprintf(packet,sizeof(packet),"NGSTATE|4|%s|%u|%llu|%016llx|%016llx|%s|%u|%u|%s",LA_TOKEN,8u,18446744073709551615ULL,0ULL,0ULL,
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",8u,GATE_LOOKAHEAD_MAX,big);
    assert(n==649 && (size_t)n<GATE_STATE_MAX);
    char permit[GATE_PERMIT_MAX];
    n=snprintf(permit,sizeof(permit),"NGGO|4|%s|8|18446744073709551615|%u",LA_TOKEN,GATE_LOOKAHEAD_MAX);
    for(unsigned i=0;i<8;i++) n+=snprintf(permit+n,sizeof(permit)-(size_t)n,"|%s",big);
    assert(n==3912 && (size_t)n<GATE_PERMIT_MAX);
    stats.slot=2;wire_clients=3;wire_lookahead=2;
    assert(pilot_delay_covers(PILOT_SOURCE_WIRE,3,0) && pilot_delay_covers(PILOT_SOURCE_WIRE,3,1) && !pilot_delay_covers(PILOT_SOURCE_WIRE,3,2));
    assert(pilot_delay_covers(PILOT_SOURCE_WIRE,4,2) && pilot_delay_covers(PILOT_SOURCE_SCRIPT,0,GATE_LOOKAHEAD_MAX));
    wire_version=2;wire_clients=2;wire_lookahead=0;stats.completed=0;
}
static void test_lookahead_config(void) {
    assert(!used && wire_version==2 && !wire_lookahead);
    assert(!x4gate_lookahead(1) && !x4gate_lookahead(0) && !wire_lookahead);    /* protocol 2 has no window */
    assert(x4gate_wire(3,3) && !x4gate_lookahead(GATE_LOOKAHEAD_MAX+1) && x4gate_lookahead(2) && wire_lookahead==2);
    assert(x4gate_wire(3,4) && wire_lookahead==2 && x4gate_lookahead(0) && !wire_lookahead && x4gate_lookahead(GATE_LOOKAHEAD_MAX));
    assert(x4gate_wire(2,2) && !wire_lookahead);                                   /* back to protocol 2 clears it */
    assert(x4gate_wire(3,3) && x4gate_lookahead(2));
    used=1;assert(x4gate_lookahead(2) && !x4gate_lookahead(1) && !x4gate_lookahead(0) && wire_lookahead==2);used=0;
    double (*old_number)(lua_State *,int)=p_lua_tonumber;void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    void (*old_rawseti)(lua_State *,int,int)=p_lua_rawseti;
    p_lua_tonumber=wire3_number;p_lua_pushnumber=wire3_push;p_lua_rawseti=NULL;
    assert(wire3_call(lua_lookahead,1,NULL,NULL)==0 && wire_lookahead==2);          /* needs lua_rawseti like protocol 3 */
    p_lua_rawseti=wire3_rawseti;
    assert(wire3_call(lua_lookahead,1.5,NULL,NULL)==0 && wire3_call(lua_lookahead,NAN,NULL,NULL)==0 && wire3_call(lua_lookahead,-1,NULL,NULL)==0);
    assert(wire3_call(lua_lookahead,9,NULL,NULL)==0 && wire_lookahead==2 && wire3_call(lua_lookahead,1,NULL,NULL)==1 && wire_lookahead==1);
    /* s.inputs_tick names the tick whose proposals s.inputs are. */
    void (*old_pushstring)(lua_State *,const char *,size_t)=p_lua_pushlstring;void (*old_table)(lua_State *,int,int)=p_lua_createtable;
    void (*old_field)(lua_State *,int,const char *)=p_lua_setfield;
    p_lua_pushlstring=wire3_pushstring;p_lua_createtable=wire3_table;p_lua_setfield=wire3_field;
    wire_lookahead=2;strcpy(blob_inputs[0],"0102");blob_inputs[1][0]=0;strcpy(blob_inputs[2],"ff");
    stats.issued=7;wire3_log[0]=0;push_inputs(NULL);
    assert(!strcmp(wire3_log,"n3;fwire_version;n3;fclients;t3,0;s4:0102;i1;s0:;i2;s2:ff;i3;finputs;n2;flookahead;n4;finputs_tick;"));
    stats.issued=2;wire3_log[0]=0;push_inputs(NULL);
    assert(!strcmp(wire3_log,"n3;fwire_version;n3;fclients;t3,0;s4:0102;i1;s0:;i2;s2:ff;i3;finputs;n2;flookahead;"));
    wire_lookahead=0;wire3_log[0]=0;push_inputs(NULL);                                /* protocol 3: unchanged */
    assert(!strcmp(wire3_log,"n3;fwire_version;n3;fclients;t3,0;s4:0102;i1;s0:;i2;s2:ff;i3;finputs;"));
    p_lua_tonumber=old_number;p_lua_pushnumber=old_push;p_lua_rawseti=old_rawseti;
    p_lua_pushlstring=old_pushstring;p_lua_createtable=old_table;p_lua_setfield=old_field;
    stats.issued=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    assert(x4gate_wire(2,2) && !wire_lookahead);
}
static void test_lookahead_wait(void) {
    WSADATA w;assert(!WSAStartup(MAKEWORD(2,2),&w));
    SOCKET saved_connection=connection;unsigned saved_timeout=timeout_ms;double saved_game=*(double*)(image+GAME_RVA);
    la_relay=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(la_relay!=INVALID_SOCKET);
    struct sockaddr_in relay;memset(&relay,0,sizeof(relay));relay.sin_family=AF_INET;relay.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int size=sizeof(relay);
    assert(!bind(la_relay,(struct sockaddr*)&relay,sizeof(relay)) && !getsockname(la_relay,(struct sockaddr*)&relay,&size));
    DWORD drain_ms=60,client_ms=25;
    assert(!setsockopt(la_relay,SOL_SOCKET,SO_RCVTIMEO,(const char*)&drain_ms,sizeof(drain_ms)));
    connection=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(connection!=INVALID_SOCKET);
    assert(!setsockopt(connection,SOL_SOCKET,SO_RCVTIMEO,(const char*)&client_ms,sizeof(client_ms)));
    assert(!connect(connection,(struct sockaddr*)&relay,sizeof(relay)));
    size=sizeof(la_client);assert(!getsockname(connection,(struct sockaddr*)&la_client,&size));
    strcpy(secret,LA_TOKEN);stats.slot=2;wire_version=3;wire_clients=3;wire_lookahead=2;lookahead_reset();
    stats.game=20.0;*(double*)(image+GAME_RVA)=20.0;stats.issued=stats.completed=0;stats.waits=0;timeout_ms=2000;
    char packet[GATE_STATE_MAX];unsigned counts[16];
    /* Tick 0: permits 1 and 3 are queued before the report is even sealed.
     * This client (slot 2) proposes la_blob(2,tick) like the relay expects. */
    la_send_permit(1);la_send_permit(3);
    la_blob(2,0,blob_proposal);la_state(packet,sizeof(packet),0,blob_proposal);
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && !blob_inputs[0][0] && !blob_inputs[1][0]);
    assert(la_drain(counts)==1 && counts[0]==1 && stats.waits==1);
    /* Tick 1: permit 3 (still queued) and 2 arrive; tick 2: permit 3 is held, no receive. */
    stats.completed=++stats.issued;la_send_permit(2);
    la_blob(2,1,blob_proposal);la_state(packet,sizeof(packet),1,blob_proposal);
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)));
    stats.completed=++stats.issued;la_blob(2,2,blob_proposal);la_state(packet,sizeof(packet),2,blob_proposal);
    uint64_t idle=lookahead_idle,ready=lookahead_ready;ULONGLONG start=GetTickCount64();
    assert(!lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply)) && lookahead_idle==idle && GetTickCount64()-start<50);
    assert(lookahead_ready==ready+1 && lookahead_ahead_max==3);
    char expected[GATE_BLOB_HEX_MAX+1];
    for(unsigned s=1;s<=3;s++) {la_blob(s,0,expected);assert(!strcmp(blob_inputs[s-1],expected));}
    la_drain(counts);assert(counts[1]==1 && counts[2]==1 && !counts[3]);
    /* Tick 3 without a permit: every window report (1..3) is resent at most
     * once per 100 ms, tick 0 (outside the window) never; then reason 7. */
    stats.completed=++stats.issued;la_blob(2,3,blob_proposal);la_state(packet,sizeof(packet),3,blob_proposal);timeout_ms=350;
    uint64_t resends=lookahead_resends;
    assert(lookahead_frame(packet,strlen(packet),la_reply,sizeof(la_reply))==7 && lookahead_idle>idle && lookahead_resends>resends);
    assert(la_drain(counts)>=6 && !counts[0] && counts[1]>=2 && counts[2]>=2 && counts[3]>=2 && counts[3]<=5);
    /* A stale permit, a protocol 3 permit and a stop for a later tick are ignored. */
    timeout_ms=150;la_send_permit(2);
    const char *other="NGGO|3|" LA_TOKEN "|2|4|||",*later="NGSTOP|4|" LA_TOKEN "|2|4";
    la_send(other,strlen(other));la_send(later,strlen(later));
    uint64_t stale=lookahead_stale;
    assert(lookahead_wait(la_reply,sizeof(la_reply))==7 && lookahead_stale==stale+1);
    /* The relay stops the session: NGSTOP for a report it holds, seen even
     * while the next permit is already held. */
    la_send_permit(4);const char *stop="NGSTOP|4|" LA_TOKEN "|2|1";la_send(stop,strlen(stop));Sleep(20);
    assert(lookahead_wait(la_reply,sizeof(la_reply))==1);
    assert(lookahead_take(4));                                  /* held, but the stop came first */
    /* Fail closed: beyond the window (3+1+2=6), a changed copy, a changed own proposal. */
    la_send_permit(7);assert(lookahead_wait(la_reply,sizeof(la_reply))==LOOKAHEAD_STOP_AHEAD);
    la_send_permit(5);char changed[GATE_PERMIT_MAX];la_permit_text(changed,sizeof(changed),5);
    changed[strlen(changed)-1]=changed[strlen(changed)-1]=='0'?'1':'0';
    la_send(changed,strlen(changed));assert(lookahead_wait(la_reply,sizeof(la_reply))==LOOKAHEAD_STOP_CONFLICT);
    const char *mine="NGGO|4|" LA_TOKEN "|2|4|2|01|0c|";                                /* tick 1 sealed "" */
    la_send(mine,strlen(mine));assert(lookahead_wait(la_reply,sizeof(la_reply))==56);
    /* Oversize datagram (8,192 bytes or more) and game time moving while gated. */
    static char huge[9000];memset(huge,'N',sizeof(huge));la_send(huge,sizeof(huge));
    assert(lookahead_wait(la_reply,sizeof(la_reply))==8);
    *(double*)(image+GAME_RVA)=20.5;assert(lookahead_wait(la_reply,sizeof(la_reply))==5);*(double*)(image+GAME_RVA)=20.0;
    la_drain(counts);
    /* A lost report or permit is recovered by the resend: the relay answers only
     * the second copy of report 4 with permit 4+1+k=7. */
    lookahead_reset();stats.issued=stats.completed=0;timeout_ms=3000;
    for(uint64_t c=0;c<=6;c++) {
        char own[GATE_BLOB_HEX_MAX+1];la_blob(2,c,own);la_state(packet,sizeof(packet),c,own);assert(lookahead_seal(c,packet,strlen(packet),own));
    }
    for(uint64_t t=1;t<=6;t++) {la_permit_text(changed,sizeof(changed),t);assert(la_permit(changed)==1);stats.completed=t;assert(lookahead_take(t));}
    stats.issued=stats.completed=6;la_drain(counts);
    for(uint64_t c=4;c<=6;c++) lookahead_reports[c%LOOKAHEAD_RING].sent=GetTickCount64(); /* just sent once */
    HANDLE thread=CreateThread(NULL,0,la_resend_relay,NULL,0,NULL);assert(thread);
    start=GetTickCount64();
    assert(!lookahead_wait(la_reply,sizeof(la_reply)) && GetTickCount64()-start>=90 && GetTickCount64()-start<1000);
    assert(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0);DWORD code=1;GetExitCodeThread(thread,&code);CloseHandle(thread);assert(!code);
    for(unsigned s=1;s<=3;s++) {la_blob(s,4,expected);assert(!strcmp(blob_inputs[s-1],expected));}
    closesocket(connection);closesocket(la_relay);la_relay=INVALID_SOCKET;connection=saved_connection;timeout_ms=saved_timeout;
    WSACleanup();*(double*)(image+GAME_RVA)=saved_game;
}
static void test_lookahead(void) {
    gate_stats saved=stats;char saved_secret[sizeof(secret)];memcpy(saved_secret,secret,sizeof(secret));
    test_lookahead_parse();test_lookahead_config();test_lookahead_wait();
    stats=saved;memcpy(secret,saved_secret,sizeof(secret));lookahead_reset();
    wire_version=2;wire_clients=2;wire_lookahead=0;blob_proposal[0]=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    puts("protocol 4 lookahead window: canonical permits, window/conflict/own-proposal refusals, ring over k=1..8, "
         "resend recovery, stop, timeout and Lua surface PASS");
}
