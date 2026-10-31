/* Platform-neutral gate helpers shared by the Windows gate (lockstep_gate.c) and
 * the Linux gate (lockstep_gate_linux.c): relay address parsing, hex checks.
 * Included at one fixed point of each translation unit. */
static int relay_host_parse(const char *text,uint32_t *out) {
    unsigned parts[4],n=0,value=0,digits=0;
    if(!text) return 0;
    for(const char *c=text;;c++) {
        if(*c>='0' && *c<='9') {if(++digits>3) return 0;value=value*10+(unsigned)(*c-'0');if(value>255) return 0;continue;}
        if((*c=='.' || !*c) && digits && n<4) {parts[n++]=value;value=0;digits=0;if(!*c) break;continue;}
        return 0;
    }
    if(n!=4) return 0;
    *out=htonl((parts[0]<<24)|(parts[1]<<16)|(parts[2]<<8)|parts[3]);return 1;
}
static char initial_state_digest[65];
typedef void (*tick_fn)(void *, unsigned char);

static int valid_hex(const char *s, unsigned n) {
    if (!s) return 0;
    for (unsigned i=0;i<n;i++) if (!s[i] || !((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;
    return s[n]==0;
}
static void hex32(const unsigned char *raw, char out[65]) {
    const char *h="0123456789abcdef";
    for (unsigned i=0;i<32;i++) {out[i*2]=h[raw[i]>>4];out[i*2+1]=h[raw[i]&15];} out[64]=0;
}
