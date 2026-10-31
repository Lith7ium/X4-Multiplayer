/* Portable SHA-256 (FIPS 180-4) for gate builds without BCrypt: the Linux gate's
 * build pins, its file pin and gate.sha256 (the probe's per-tick digest). Same
 * results as BCrypt SHA-256; vectors in linux/test_gate_linux.c. Original code. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef struct {uint32_t h[8];uint64_t bytes;unsigned char block[64];size_t used;} lockstep_sha256_ctx;
static const uint32_t lockstep_sha256_k[64]={
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static uint32_t lockstep_sha256_ror(uint32_t x,unsigned n) {return (x>>n)|(x<<(32u-n));}
static void lockstep_sha256_compress(lockstep_sha256_ctx *c,const unsigned char *p) {
    uint32_t w[64],a,b,d,e,f,g,h,s;
    for(unsigned i=0;i<16;i++) w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(unsigned i=16;i<64;i++) {
        uint32_t s0=lockstep_sha256_ror(w[i-15],7)^lockstep_sha256_ror(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=lockstep_sha256_ror(w[i-2],17)^lockstep_sha256_ror(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=c->h[0];b=c->h[1];s=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(unsigned i=0;i<64;i++) {
        uint32_t t1=h+(lockstep_sha256_ror(e,6)^lockstep_sha256_ror(e,11)^lockstep_sha256_ror(e,25))+((e&f)^(~e&g))+lockstep_sha256_k[i]+w[i];
        uint32_t t2=(lockstep_sha256_ror(a,2)^lockstep_sha256_ror(a,13)^lockstep_sha256_ror(a,22))+((a&b)^(a&s)^(b&s));
        h=g;g=f;f=e;e=d+t1;d=s;s=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=s;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void lockstep_sha256_init(lockstep_sha256_ctx *c) {
    static const uint32_t start[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memcpy(c->h,start,sizeof(start));c->bytes=0;c->used=0;
}
static void lockstep_sha256_update(lockstep_sha256_ctx *c,const void *data,size_t length) {
    const unsigned char *p=(const unsigned char*)data;c->bytes+=length; /* C and C++ */
    if(c->used) {
        size_t take=64-c->used<length?64-c->used:length;
        memcpy(c->block+c->used,p,take);c->used+=take;p+=take;length-=take;
        if(c->used<64) return;
        lockstep_sha256_compress(c,c->block);c->used=0;
    }
    for(;length>=64;p+=64,length-=64) lockstep_sha256_compress(c,p);
    if(length) {memcpy(c->block,p,length);c->used=length;}
}
static void lockstep_sha256_final(lockstep_sha256_ctx *c,unsigned char out[32]) {
    uint64_t bits=c->bytes*8u;unsigned char pad[72]={0x80};
    size_t n=c->used<56?56-c->used:120-c->used;
    for(unsigned i=0;i<8;i++) pad[n+i]=(unsigned char)(bits>>(56-8*i));
    lockstep_sha256_update(c,pad,n+8);
    for(unsigned i=0;i<8;i++) for(unsigned j=0;j<4;j++) out[4*i+j]=(unsigned char)(c->h[i]>>(24-8*j));
}
static void lockstep_sha256(const void *data,size_t length,unsigned char out[32]) {
    lockstep_sha256_ctx c;lockstep_sha256_init(&c);lockstep_sha256_update(&c,data,length);lockstep_sha256_final(&c,out);
}
