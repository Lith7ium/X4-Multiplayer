/* Pure codec/refusal tests. Native gameplay and native scheduling are separate. */
#include "lockstep_portable_wire.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static const char token[]="0123456789abcdef0123456789abcdef";
static const char digest[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static int reply(const char *packet,unsigned tick,int draining) {return pw_reply(packet,strlen(packet),token,1,tick,12,draining);}
int main(void) {
    char out[PORTABLE_WIRE_PACKET_MAX],expected[512];unsigned char bytes[PORTABLE_WIRE_FRAME_MAX];
    memset(bytes,0xa5,sizeof(bytes));
    int n=pw_hello(out,sizeof(out),token,1,"windows",digest,digest,digest,digest);
    snprintf(expected,sizeof(expected),"NGPHELLO|1|%s|1|windows|%s|%s|%s|%s",token,digest,digest,digest,digest);
    assert(n==(int)strlen(expected) && !strcmp(out,expected));
    assert(!pw_hello(out,sizeof(out),token,1,"linux",digest,digest,digest,digest));
    assert(!pw_hello(out,sizeof(out),token,0,"windows",digest,digest,digest,digest));
    assert(!pw_hello(out,1,token,1,"windows",digest,digest,digest,digest) && !out[0]);
    assert(!pw_hello(out,sizeof(out),token,1,"windows","bad",digest,digest,digest));
    assert(pw_state(out,sizeof(out),token,1,UINT64_C(0x4018000000000000),digest,bytes,sizeof(bytes))>8192);
    n=pw_state(out,sizeof(out),token,1,UINT64_C(0x4018000000000000),digest,bytes,2);
    snprintf(expected,sizeof(expected),"NGPSTATE|1|%s|1|4018000000000000|%s|a5a5",token,digest);
    assert(n==(int)strlen(expected) && !strcmp(out,expected));
    assert(!pw_state(out,(size_t)n,token,1,0,digest,bytes,2));
    assert(!pw_state(out,sizeof(out),token,1,0,digest,bytes,4097));
    assert(!pw_state(out,sizeof(out),token,1,0,digest,bytes,0));
    snprintf(expected,sizeof(expected),"NGPGO|1|%s|1|4|00000000|00000000",token);
    assert(reply(expected,3,0)==PW_GO && reply(expected,4,0)==PW_IGNORE && reply(expected,2,0)==PW_INVALID);
    assert(reply(expected,3,1)==PW_IGNORE);
    char changed[512];strcpy(changed,expected);changed[strlen(changed)-1]='1';assert(reply(changed,3,0)==PW_INVALID);
    strcpy(changed,expected);changed[8]='f';assert(reply(changed,3,0)==PW_IGNORE);
    assert(pw_reply(expected,strlen(expected)+1,token,1,3,12,0)==PW_INVALID);
    assert(pw_reply("foreign\0packet",14,token,1,3,12,0)==PW_IGNORE);
    snprintf(expected,sizeof(expected),"NGPGO|1|%s|1|04|00000000|00000000",token);assert(reply(expected,3,0)==PW_INVALID);
    snprintf(expected,sizeof(expected),"NGPGO|1|%s|1|429496729600000000000|00000000|00000000",token);assert(reply(expected,3,0)==PW_INVALID);
    snprintf(expected,sizeof(expected),"NGPSTOP|1|%s|1|3|0",token);assert(reply(expected,3,0)==PW_INVALID);
    snprintf(expected,sizeof(expected),"NGPSTOP|1|%s|1|3|1",token);assert(reply(expected,3,0)==PW_FAILED);
    assert(reply(expected,4,0)==PW_IGNORE && reply(expected,2,0)==PW_INVALID);
    snprintf(expected,sizeof(expected),"NGPSTOP|1|%s|1|12|0",token);assert(reply(expected,12,0)==PW_STOP);
    snprintf(expected,sizeof(expected),"NGPCLOSED|1|%s|1|12",token);
    assert(reply(expected,12,1)==PW_CLOSED && reply(expected,12,0)==PW_IGNORE);
    assert(pw_done(out,sizeof(out),token,1,12,13,13,1));
    snprintf(expected,sizeof(expected),"NGPDONE|1|%s|1|12|13|13|1",token);assert(!strcmp(out,expected));
    assert(!pw_done(out,sizeof(out),token,1,12,122,13,1));
    puts("portable NGP bounded codec, exact zero permits, stale/future/authentication and terminal checks pass");
    return 0;
}
