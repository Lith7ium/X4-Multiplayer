#undef NDEBUG
#include <assert.h>
#include "lockstep_getup_diagnostic_wire.h"
#include "lockstep_wire.h"
static const char token[]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char run[]="3ba3f5f43b92602683c19aee62a20342b084dd5971ddd33808d81a328879a547";
int main(void) {
    char packet[GD_PACKET_MAX],other[GD_PACKET_MAX],closed[GD_PACKET_MAX];unsigned reason=0;
    char hash[65];memset(hash,'b',64);hash[64]=0;double game=20,real=80;uint64_t g,r;memcpy(&g,&game,8);memcpy(&r,&real,8);
    assert(gd_state(packet,sizeof(packet),token,run,0,g,r,hash,"00000000"));
    assert(strstr(packet,"|1|1|0|4034000000000000|4054000000000000|"));
    assert(!gd_state(packet,sizeof(packet),token,run,0,g,r,hash,"87010000"));
    assert(!gd_state(packet,sizeof(packet),token,run,121,g,r,hash,"00000000"));
    assert(!gd_state(packet,5,token,run,0,g,r,hash,"00000000"));
    for(unsigned tick=0;tick<120;tick++) {
        int n=gd_header(packet,sizeof(packet),"NGDGO",token,run,tick+1);assert(n);
        snprintf(packet+n,sizeof(packet)-(size_t)n,"|%s",gd_command(tick+1));
        assert(gd_reply(packet,strlen(packet),token,run,tick,NULL,&reason)==GD_GO);
        assert(gd_reply(packet,strlen(packet),token,run,tick+1,NULL,&reason)==GD_INVALID);
        char expected[128],a[9],b[9];snprintf(expected,sizeof(expected),"NGGO|2|%s|1|%u",token,tick+1);
        assert(gate_permit_inputs(packet,expected,1,"00000000",a,b)==0);
    }
    int n=gd_header(packet,sizeof(packet),"NGDSTOP",token,run,120);snprintf(packet+n,sizeof(packet)-(size_t)n,"|1");
    assert(gd_reply(packet,strlen(packet),token,run,120,NULL,&reason)==GD_STOP && reason==1);
    assert(gd_reply(packet,strlen(packet),token,run,119,NULL,&reason)==GD_INVALID);
    assert(gd_terminal_packet(packet,sizeof(packet),"NGDDONE",token,run,120,1,1,121,121,0,0));
    assert(gd_terminal_packet(closed,sizeof(closed),"NGDCLOSED",token,run,120,1,1,121,121,0,0));
    assert(gd_reply(closed,strlen(closed),token,run,120,closed,&reason)==GD_CLOSED);
    for(unsigned i=0;i<strlen(closed);i++) {
        strcpy(other,closed);other[i]=other[i]=='0'?'1':'0';
        assert(gd_reply(other,strlen(other),token,run,120,closed,&reason)==GD_INVALID);
    }
    assert(!gd_terminal_packet(packet,sizeof(packet),"NGDDONE",token,run,120,1,1,121,121,1,0));
    const char *ordinary[]={"NGSTATE","NGGO","NGSTOP","NGDONE","NGCLOSED"};
    for(unsigned i=0;i<5;i++) {
        n=gd_header(other,sizeof(other),ordinary[i],token,run,30);strcpy(other+n,"|87010000");
        assert(gd_reply(other,strlen(other),token,run,29,NULL,&reason)==GD_INVALID);
    }
    uint64_t integer;assert(!gd_uint("18446744073709551616",UINT64_MAX,&integer));
    assert(gd_uint("18446744073709551615",UINT64_MAX,&integer) && integer==UINT64_MAX);
    assert(!gd_uint("+1",120,&integer) && !gd_uint("01",120,&integer) && !gd_uint("121",120,&integer));
    puts("NGD production codec: exact typed contract, 120 grants, namespace isolation and terminal tamper refusals PASS");return 0;
}
