/* Closed single-native diagnostic protocol. Never accepted by the paired gate. */
#ifndef LOCKSTEP_GETUP_DIAGNOSTIC_WIRE_H
#define LOCKSTEP_GETUP_DIAGNOSTIC_WIRE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define GD_CONTRACT "diagnostic-getup-1"
#define GD_LIMIT 120u
#define GD_OPTIONS 70377334178981ULL /* serial + saved IDs + passive motion/AI observers */
#define GD_PACKET_MAX 512u
enum {GD_INVALID=-1,GD_IGNORE=0,GD_GO=1,GD_STOP=2,GD_CLOSED=3};
static int gd_hex(const char *s,unsigned n) {
    if(!s) return 0;
    for(unsigned i=0;i<n;i++) if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return s[n]==0;
}
static int gd_uint(const char *s,uint64_t maximum,uint64_t *out) {
    uint64_t value=0;if(!s || !*s || (s[0]=='0' && s[1])) return 0;
    for(;*s;s++) {if(*s<'0' || *s>'9' || (unsigned)(*s-'0')>maximum || value>(maximum-(unsigned)(*s-'0'))/10) return 0;value=value*10+(unsigned)(*s-'0');}
    *out=value;return 1;
}
static const char *gd_command(unsigned tick) {return tick==30?"87010000":"00000000";}
static int gd_header(char *out,size_t size,const char *tag,const char *token,const char *run,unsigned tick) {
    if(!gd_hex(token,32)||!gd_hex(run,64)||tick>GD_LIMIT) return 0;
    int n=snprintf(out,size,"%s|1|%s|%s|%s|1|1|%u",tag,GD_CONTRACT,token,run,tick);
    return n>0 && (size_t)n<size?n:0;
}
static int gd_state(char *out,size_t size,const char *token,const char *run,unsigned tick,
                    uint64_t game,uint64_t real,const char *hash,const char *proposal) {
    double g,r;memcpy(&g,&game,8);memcpy(&r,&real,8);
    if(!isfinite(g)||!isfinite(r)||g<0||r<0||!gd_hex(hash,64)||!gd_hex(proposal,8)||
       strcmp(proposal,tick==29?"87010000":"00000000")) return 0;
    int n=gd_header(out,size,"NGDSTATE",token,run,tick);if(!n) return 0;
    int m=snprintf(out+n,size-(size_t)n,"|%016llx|%016llx|%s|%s",(unsigned long long)game,(unsigned long long)real,hash,proposal);
    return m>0 && (size_t)m<size-(size_t)n?n+m:0;
}
static int gd_terminal_packet(char *out,size_t size,const char *tag,const char *token,const char *run,unsigned tick,
                       unsigned reason,unsigned restored,uint64_t calls,uint64_t returns,uint64_t unwinds,uint64_t failures) {
    if((strcmp(tag,"NGDDONE") && strcmp(tag,"NGDCLOSED")) || !reason || restored>1 || calls>GD_LIMIT+1 ||
       returns>calls || unwinds>calls || returns+unwinds>calls || failures>UINT32_MAX) return 0;
    int n=gd_header(out,size,tag,token,run,tick);if(!n) return 0;
    int m=snprintf(out+n,size-(size_t)n,"|%u|%u|%llu|%llu|%llu|%llu",reason,restored,
        (unsigned long long)calls,(unsigned long long)returns,(unsigned long long)unwinds,(unsigned long long)failures);
    return m>0 && (size_t)m<size-(size_t)n?n+m:0;
}
/* Expected terminal text is an immutable DONE with its tag changed to CLOSED.
 * An authenticated stale/future/malformed message is a refusal, never a permit. */
static int gd_reply(const char *packet,size_t length,const char *token,const char *run,unsigned tick,
                    const char *closed,unsigned *reason) {
    if(!packet || !length || length>=GD_PACKET_MAX || memchr(packet,0,length) || tick>GD_LIMIT ||
       !gd_hex(token,32)||!gd_hex(run,64)) return GD_INVALID;
    char copy[GD_PACKET_MAX],*fields[16];memcpy(copy,packet,length);copy[length]=0;
    unsigned count=1;fields[0]=copy;
    for(size_t i=0;i<length;i++) if(copy[i]=='|') {if(count==16) return GD_INVALID;copy[i]=0;fields[count++]=copy+i+1;}
    if(count<8 || strcmp(fields[1],"1") || strcmp(fields[2],GD_CONTRACT) || strcmp(fields[3],token) ||
       strcmp(fields[4],run) || strcmp(fields[5],"1") || strcmp(fields[6],"1")) return GD_INVALID;
    uint64_t value;if(!gd_uint(fields[7],GD_LIMIT,&value)) return GD_INVALID;
    if(closed) return !strcmp(fields[0],"NGDCLOSED") && strlen(closed)==length && !memcmp(packet,closed,length)?GD_CLOSED:GD_INVALID;
    if(!strcmp(fields[0],"NGDGO") && count==9 && tick<GD_LIMIT && value==tick+1 && !strcmp(fields[8],gd_command((unsigned)value))) return GD_GO;
    if(!strcmp(fields[0],"NGDSTOP") && count==9 && value==tick && gd_uint(fields[8],UINT32_MAX,&value) && value &&
       (value!=1 || tick==GD_LIMIT)) {*reason=(unsigned)value;return GD_STOP;}
    return GD_INVALID;
}
#endif
