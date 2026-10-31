/* Trampolines that replace RCPSS/RSQRTSS by exactly rounded arithmetic; shared by the
 * Windows gate (lockstep_exact_reciprocal.h) and the Linux preload. Original code.
 *   rcpss  d, s  ->  d[0] = 1.0f / s[0]            (DIVSS)
 *   rsqrtss d, s ->  d[0] = 1.0f / sqrt(s[0])      (SQRTSS, then DIVSS)
 * and the approximate length rcp(rsqrt(x)) as what it stands for:
 *   rsqrtss d, s ... rcpss d, d  ->  sqrtss d, s ... (nothing)   d[0] = sqrt(s[0])
 * when no instruction in between touches d and no branch enters the chain (the site
 * generator proves this; such a chain's two halves are kinds XRCP_SQRT and
 * XRCP_CHAIN_END and are only ever patched together). That is the value a compiler
 * without fast-math computes for sqrt(x), as the Linux build does where X4.exe has the
 * pair (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt, C7), and the same whether the
 * compiler placed the two instructions next to each other or apart. GCC also emits the
 * pair with the result in a copy, rsqrtss r,s ... movaps c,r ... rcpss c,r (kinds XRCP_SQRT
 * and XRCP_COPY_END):
 *   -> sqrtss r,s ... movaps c,r ... r[0] = 1.0f / r[0]   c[0] = sqrt(s[0]),
 * r[0] = 1/sqrt(s[0]) exactly as XRCP_RSQRTSS rounds it, so r needs no liveness proof.
 * MSVC emits it without the copy, rsqrtss r,s ... rcpss e,r (kinds XRCP_SQRT and XRCP_MOVE_END):
 *   -> sqrtss r,s ... movss e,r; r[0] = 1.0f / r[0]   e[0] = sqrt(s[0]), e's lanes 1-3 kept,
 * r as above.
 * lanes 1-3 of d unchanged, flags untouched, s untouched. The value goes through a
 * 16-byte slot below RSP: 'drop' is 16 on Windows (no red zone) and 144 on Linux, where
 * the 128-byte System V red zone below RSP may hold the function's data. A window is a
 * run of whole instructions starting at a rewritten one; copied instructions keep their
 * bytes, RIP displacements re-targeted. DIVSS and SQRTSS are correctly rounded under the
 * thread's MXCSR on every x86-64 CPU, so the result does not depend on the vendor. */
#ifndef X4_EXACT_RECIPROCAL_EMIT_H
#define X4_EXACT_RECIPROCAL_EMIT_H
#include <stdint.h>
#include <string.h>

typedef struct {unsigned char offset,length,kind,rip;} xrcp_insn;
typedef struct {unsigned rva;unsigned char length,count;uint64_t digest;xrcp_insn insn[4];} xrcp_window;
enum {XRCP_COPY=0,XRCP_RCPSS=1,XRCP_RSQRTSS=2,XRCP_SQRT=3,XRCP_CHAIN_END=4,XRCP_COPY_END=5,XRCP_MOVE_END=6};
#define XRCP_SLOT 128u
#define XRCP_CONSTANTS 64u
#define XRCP_MAX_WINDOW 24u
static const unsigned char xrcp_constants[32]={
    0,0,0,0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, /* mask: keep lanes 1-3 */
    0,0,0x80,0x3f,0,0,0,0,0,0,0,0,0,0,0,0};                              /* 1.0f in lane 0 */

static uint64_t xrcp_fnv(const unsigned char *p,size_t n) {
    uint64_t h=0xcbf29ce484222325ULL;for(size_t i=0;i<n;i++) {h^=p[i];h*=0x100000001b3ULL;}return h;
}
/* The register operands of a replaced instruction: F3 [REX] 0F 52/53 modrm (mod 3);
 * RCPSS for kinds XRCP_RCPSS, XRCP_CHAIN_END, XRCP_COPY_END and XRCP_MOVE_END, RSQRTSS for XRCP_RSQRTSS and XRCP_SQRT. */
static int xrcp_decode(const unsigned char *insn,unsigned length,unsigned kind,unsigned *d,unsigned *s) {
    unsigned at=1,rex=0,rcp=kind==XRCP_RCPSS || kind==XRCP_CHAIN_END || kind==XRCP_COPY_END || kind==XRCP_MOVE_END;
    if(length<4 || insn[0]!=0xf3) return 0;
    if((insn[1]&0xf0)==0x40) {rex=insn[1];at=2;}
    if(rex&0x08 || length!=at+3 || insn[at]!=0x0f || insn[at+1]!=(rcp?0x53:0x52) || (insn[at+2]>>6)!=3) return 0;
    *d=((insn[at+2]>>3)&7)|(rex&4?8:0);*s=(insn[at+2]&7)|(rex&1?8:0);return 1;
}
/* [F3] [REX.R] 0F op modrm sib for op xmm<reg>, [rsp]; returns the length. */
static unsigned xrcp_rsp_op(unsigned char *p,unsigned op,unsigned reg) {
    unsigned n=0;p[n++]=0xf3;if(reg>=8) p[n++]=0x44;
    p[n++]=0x0f;p[n++]=(unsigned char)op;p[n++]=(unsigned char)(0x04|(reg&7)<<3);p[n++]=0x24;return n;
}
/* [REX.R] 0F op modrm disp32 for op xmm<reg>, [rip+target] (ANDPS/ORPS, aligned constant). */
static int xrcp_rip_op(unsigned char *p,unsigned op,unsigned reg,const unsigned char *target,unsigned *n) {
    unsigned k=0;if(reg>=8) p[k++]=0x44;
    p[k++]=0x0f;p[k++]=(unsigned char)op;p[k++]=(unsigned char)(0x05|(reg&7)<<3);
    intptr_t d=(intptr_t)target-(intptr_t)(p+k+4);if(d<INT32_MIN || d>INT32_MAX) return 0;
    int32_t d32=(int32_t)d;memcpy(p+k,&d32,4);*n=k+4;return 1;
}
/* lea rsp, [rsp+delta]: no flags; disp8 when it fits. */
static unsigned xrcp_lea_rsp(unsigned char *p,int delta) {
    p[0]=0x48;p[1]=0x8d;
    if(delta>=-128 && delta<=127) {p[2]=0x64;p[3]=0x24;p[4]=(unsigned char)(int8_t)delta;return 5;}
    int32_t d32=delta;p[2]=0xa4;p[3]=0x24;memcpy(p+4,&d32,4);return 8;
}
/* d[0] = 1/s[0] or 1/sqrt(s[0]), lanes 1-3 of d kept; constants at the region start. */
static int xrcp_emit_exact(unsigned char *p,unsigned kind,unsigned d,unsigned s,const unsigned char *constants,
                           unsigned drop,unsigned *n) {
    unsigned k=0,len;
    k+=xrcp_lea_rsp(p+k,-(int)drop);
    k+=xrcp_rsp_op(p+k,0x11,s);                         /* movss [rsp], s */
    if(kind==XRCP_RSQRTSS) {
        k+=xrcp_rsp_op(p+k,0x51,d);                     /* sqrtss d, [rsp] */
        k+=xrcp_rsp_op(p+k,0x11,d);                     /* movss [rsp], d */
    }
    if(!xrcp_rip_op(p+k,0x54,d,constants,&len)) return 0;    /* andps d, mask */
    k+=len;
    if(!xrcp_rip_op(p+k,0x56,d,constants+16,&len)) return 0; /* orps d, 1.0 */
    k+=len;
    k+=xrcp_rsp_op(p+k,0x5e,d);                         /* divss d, [rsp] */
    k+=xrcp_lea_rsp(p+k,(int)drop);
    *n=k;return 1;
}
/* One trampoline for window w whose original bytes are 'original' and which sits at
 * 'site' in memory: rewritten and copied instructions, then jmp back to site+length. */
static int xrcp_emit_window(unsigned char *out,const xrcp_window *w,const unsigned char *original,
                            const unsigned char *site,const unsigned char *constants,unsigned drop,unsigned *size) {
    unsigned k=0;
    for(unsigned i=0;i<w->count;i++) {
        const xrcp_insn *x=&w->insn[i];const unsigned char *insn=original+x->offset;
        if(x->offset+x->length>w->length) return 0;
        if(x->kind==XRCP_COPY) {
            memcpy(out+k,insn,x->length);
            if(x->rip) {
                int32_t disp;
                if(x->rip+4>x->length) return 0;
                memcpy(&disp,insn+x->rip,4);
                intptr_t target=(intptr_t)(site+x->offset+x->length)+disp,now=target-(intptr_t)(out+k+x->length);
                if(now<INT32_MIN || now>INT32_MAX) return 0;
                int32_t d32=(int32_t)now;memcpy(out+k+x->rip,&d32,4);
            }
            k+=x->length;
        }else if(x->kind==XRCP_SQRT) {                  /* sqrtss d, s: the same bytes, opcode 51 */
            unsigned d,s;
            if(!xrcp_decode(insn,x->length,x->kind,&d,&s)) return 0;
            memcpy(out+k,insn,x->length);out[k+x->length-2]=0x51;
            k+=x->length;
        }else if(x->kind==XRCP_CHAIN_END) {             /* rcpss d, d after sqrtss d, s: nothing */
            unsigned d,s;
            if(!xrcp_decode(insn,x->length,x->kind,&d,&s) || d!=s) return 0;
        }else if(x->kind==XRCP_COPY_END) {              /* rcpss c, r after sqrtss r, s; movaps c, r: r[0] = 1/r[0] */
            unsigned d,s,len;
            if(!xrcp_decode(insn,x->length,x->kind,&d,&s) || d==s || !xrcp_emit_exact(out+k,XRCP_RCPSS,s,s,constants,drop,&len)) return 0;
            k+=len;
        }else if(x->kind==XRCP_MOVE_END) {              /* rcpss e, r after sqrtss r, s: movss e, r; r[0] = 1/r[0] */
            unsigned d,s,len;
            if(!xrcp_decode(insn,x->length,x->kind,&d,&s) || d==s) return 0;
            out[k]=0xf3;k++;if(d>=8 || s>=8) out[k++]=(unsigned char)(0x40|(d>=8)<<2|(s>=8));
            out[k++]=0x0f;out[k++]=0x10;out[k++]=(unsigned char)(0xc0|(d&7)<<3|(s&7));
            if(!xrcp_emit_exact(out+k,XRCP_RCPSS,s,s,constants,drop,&len)) return 0;
            k+=len;
        }else if(x->kind==XRCP_RCPSS || x->kind==XRCP_RSQRTSS) {
            unsigned d,s,len;
            if(!xrcp_decode(insn,x->length,x->kind,&d,&s) || !xrcp_emit_exact(out+k,x->kind,d,s,constants,drop,&len)) return 0;
            k+=len;
        }else return 0;
        if(k+5>XRCP_SLOT) return 0;
    }
    intptr_t back=(intptr_t)(site+w->length)-(intptr_t)(out+k+5);
    if(back<INT32_MIN || back>INT32_MAX) return 0;
    int32_t b32=(int32_t)back;out[k]=0xe9;memcpy(out+k+1,&b32,4);k+=5;
    *size=k;return 1;
}
#endif
