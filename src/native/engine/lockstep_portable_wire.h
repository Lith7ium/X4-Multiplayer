/* Closed, command-free NGP v1 messages. No engine access or transport here.
 * Packet identity is a declaration; the owning runner verifies native PIDs and
 * artifacts separately. Accepting a message is never gameplay evidence. */
#ifndef X4MP_PORTABLE_WIRE_H
#define X4MP_PORTABLE_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PORTABLE_WIRE_FRAME_MAX 4096u
#define PORTABLE_WIRE_PACKET_MAX 8448u
enum portable_wire_result { PW_INVALID=-1, PW_IGNORE=0, PW_GO=1, PW_STOP=2, PW_FAILED=3, PW_CLOSED=4 };

static inline int pw_hex(const char *text,unsigned size) {
    if(!text) return 0;
    for(unsigned i=0;i<size;i++) if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f'))) return 0;
    return text[size]==0;
}
static inline int pw_identity(const char *token,unsigned slot) {return pw_hex(token,32) && (slot==1 || slot==2);}
static inline int pw_length(int n,size_t capacity) {return n>0 && (size_t)n<capacity?n:0;}
static inline int pw_hello(char *out,size_t capacity,const char *token,unsigned slot,const char *platform,
                           const char *exe,const char *agent,const char *contract,const char *schema) {
    if(!pw_identity(token,slot) || !platform || strcmp(platform,slot==1?"windows":"linux") ||
       !pw_hex(exe,64) || !pw_hex(agent,64) || !pw_hex(contract,64) || !pw_hex(schema,64)) return 0;
    return pw_length(snprintf(out,capacity,"NGPHELLO|1|%s|%u|%s|%s|%s|%s|%s",token,slot,platform,exe,agent,contract,schema),capacity);
}
static inline int pw_state(char *out,size_t capacity,const char *token,unsigned slot,uint64_t real,
                           const char *diagnostic,const unsigned char *frame,size_t length) {
    if(!pw_identity(token,slot) || !pw_hex(diagnostic,64) || !frame || !length || length>PORTABLE_WIRE_FRAME_MAX) return 0;
    int n=snprintf(out,capacity,"NGPSTATE|1|%s|%u|%016llx|%s|",token,slot,(unsigned long long)real,diagnostic);
    if(n<0 || (size_t)n>=capacity || length>(capacity-(size_t)n-1)/2) return 0;
    static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<length;i++) {out[n+i*2]=digits[frame[i]>>4];out[n+i*2+1]=digits[frame[i]&15];}
    out[n+length*2]=0;return n+(int)(length*2);
}
static inline int pw_done(char *out,size_t capacity,const char *token,unsigned slot,unsigned tick,
                          unsigned calls,unsigned returns,unsigned restored) {
    if(!pw_identity(token,slot) || tick>120 || calls>121 || returns>121 || restored>1) return 0;
    return pw_length(snprintf(out,capacity,"NGPDONE|1|%s|%u|%u|%u|%u|%u",token,slot,tick,calls,returns,restored),capacity);
}
static inline int pw_number(const char **cursor,unsigned *value) {
    const char *p=*cursor;unsigned n=0;
    if(*p<'0'||*p>'9'||(*p=='0' && p[1]>='0' && p[1]<='9')) return 0;
    while(*p>='0'&&*p<='9') {n=n*10+(unsigned)(*p++-'0');if(n>121) return 0;}
    if(*p && *p!='|') return 0;
    *cursor=p;*value=n;return 1;
}
static inline int pw_reply(const char *packet,size_t length,const char *token,unsigned slot,unsigned tick,unsigned limit,int draining) {
    if(!pw_identity(token,slot) || tick>limit || !limit || limit>120 || !packet || !length || length>=256) return PW_IGNORE;
    char text[256];memcpy(text,packet,length);text[length]=0;
    const char *kind=NULL;
    if(!strncmp(text,"NGPGO|",6)) kind="NGPGO";
    else if(!strncmp(text,"NGPSTOP|",8)) kind="NGPSTOP";
    else if(!strncmp(text,"NGPCLOSED|",10)) kind="NGPCLOSED";
    else return PW_IGNORE;
    char prefix[80];int n=snprintf(prefix,sizeof(prefix),"%s|1|%s|%u|",kind,token,slot);
    if(n<=0 || (size_t)n>=sizeof(prefix) || strncmp(text,prefix,(size_t)n)) return PW_IGNORE;
    if(memchr(text,0,length)) return PW_INVALID;
    const char *p=text+n;unsigned received;
    if(!pw_number(&p,&received)) return PW_INVALID;
    if(!strcmp(kind,"NGPGO")) {
        if(strcmp(p,"|00000000|00000000")) return PW_INVALID;
        if(draining || received<tick+1) return PW_IGNORE;
        return received==tick+1 && tick<limit?PW_GO:PW_INVALID;
    }
    if(!strcmp(kind,"NGPCLOSED")) {
        if(*p) return PW_INVALID;
        return draining && received==tick?PW_CLOSED:PW_IGNORE;
    }
    if(strcmp(p,"|0") && strcmp(p,"|1")) return PW_INVALID;
    if(received<tick) return PW_IGNORE;
    if(received!=tick) return PW_INVALID;
    if(p[1]=='1') return PW_FAILED;
    return tick==limit?PW_STOP:PW_INVALID;
}
#endif
