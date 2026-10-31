/* Experimental gate protocol 2. One bounded opaque command per player/frame.
 * Command semantics and ownership are checked by the fixture adapter. */
static int gate_command_valid(const char *s) {
    if(!s) return 0;
    for(unsigned i=0;i<8;i++)
        if(!s[i] || !((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return s[8]==0;
}
static int gate_permit_inputs(const char *reply,const char *prefix,unsigned slot,
                              const char *proposal,char first[9],char second[9]) {
    size_t n=strlen(prefix);
    if(strncmp(reply,prefix,n) || strlen(reply)!=n+18 || reply[n]!='|' || reply[n+9]!='|') return 0;
    char a[9],b[9];memcpy(a,reply+n+1,8);a[8]=0;memcpy(b,reply+n+10,8);b[8]=0;
    if(!gate_command_valid(a)||!gate_command_valid(b)||slot<1||slot>2) return 0;
    if(strcmp(slot==1?a:b,proposal)) return -1;
    memcpy(first,a,9);memcpy(second,b,9);return 1;
}
/* Experimental gate protocol 3: N=2..8 clients, one opaque variable-length
 * proposal per client and frame (0..480 lowercase hex characters, whole bytes).
 * The relay returns every client's sealed proposal in slot order with the
 * permit, so the blobs applied at a tick are identical on every client. Blob
 * semantics belong to the adapter; this transport never interprets them.
 * Inline: wire-only test units include this header without using protocol 3. */
#define GATE_CLIENTS_MAX 8u
#define GATE_BLOB_HEX_MAX 480u
#define GATE_STATE_MAX 1024u  /* NGSTATE|3 with a maximal blob is at most 647 bytes, NGSTATE|4 649 */
#define GATE_PERMIT_MAX 8192u /* NGGO|3 for eight maximal blobs is at most 3,910 bytes, NGGO|4 3,912 */
/* Protocol 4 (lockstep_lookahead.h): the protocol 3 input wire with a permit
 * window of 1..GATE_LOOKAHEAD_MAX ticks; matches LOOKAHEAD_MAX in the relay. */
#define GATE_LOOKAHEAD_MAX 8u
static inline int gate_blob_span(const char *s,size_t n) {
    if(!s || n>GATE_BLOB_HEX_MAX || n%2) return 0;
    for(size_t i=0;i<n;i++) if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return 1;
}
static inline int gate_blob_valid(const char *s,size_t n) {return gate_blob_span(s,n) && s[n]==0;}
/* 1: every slot's blob copied; 0: not this permit or malformed (nothing
 * copied); -1: the relay returned a different local proposal (fail closed). */
static inline int gate_permit_blobs(const char *reply,size_t length,const char *prefix,unsigned slot,unsigned clients,
                             const char *proposal,char blobs[][GATE_BLOB_HEX_MAX+1]) {
    size_t n=strlen(prefix),at=n,start[GATE_CLIENTS_MAX],size[GATE_CLIENTS_MAX];
    if(!reply || !proposal || clients<2 || clients>GATE_CLIENTS_MAX || slot<1 || slot>clients ||
       length>=GATE_PERMIT_MAX || length<n || memcmp(reply,prefix,n)) return 0;
    for(unsigned i=0;i<clients;i++) {
        if(at>=length || reply[at]!='|') return 0;
        start[i]=++at;
        while(at<length && reply[at]!='|') ++at;
        size[i]=at-start[i];
        if(!gate_blob_span(reply+start[i],size[i])) return 0;
    }
    if(at!=length) return 0;
    if(size[slot-1]!=strlen(proposal) || memcmp(reply+start[slot-1],proposal,size[slot-1])) return -1;
    for(unsigned i=0;i<clients;i++) {memcpy(blobs[i],reply+start[i],size[i]);blobs[i][size[i]]=0;}
    return 1;
}
