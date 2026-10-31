/* native_gate_fp_redirect on the Windows X4 9.00 executable: the hooks of lockstep_fp_redirect.h. Included by
 * lockstep_gate.c only. Linux twin: linux/gate_fp_redirect.h.
 *
 * The refresh 0xea1e80 (rcx owner, rdx slot = r15 throughout, r8b flag; 0xb9f bytes). Its callers are not patched:
 * other modules pin their code (the world hash's nav layout 0xea4e90 holds two of them, 0xea50ec and 0xea50fc; the
 * creation trace and registry origin pins 0xea8cf0, the virtual players 0xd8ddb0), and the FR1 run stopped there.
 * Everything patched lies inside the refresh, whose bytes only this module and the parameter option (4096, at boot)
 * pin. The getter path begins at G4 0x5f91b0, called at 0xea22b3: its stub passes r15 (the slot) as a fourth
 * argument. G0 0x5f9340 at 0xea22ca (rcx comp = r14, rdx [owner+0x28], r8 rbp+0x50), M 0xea3dc0 at 0xea28fd (+0x4)
 * and 0xea2932 (+0x0) (xmm0 a, xmm1 b, xmm2 c, r9b flag). Every path to the single ret passes 0xea29bc (mov r13d,
 * 0x300; reached from 0xea1f45, 0xea2993, 0xea29a6, 0xea29b4 and by falling through, nothing enters its bytes
 * 1..5), after the last write to +0x0/+0x4/+0x24 and with every volatile register dead: a jmp there leads to a stub
 * that calls fr_tail_hook(r15) with rsp 16-aligned (the body's frame), does the mov and jumps back to 0xea29c2.
 * The frame: rbp = entry rsp - 0x128, the query vector at rbp-0x10 (w at rbp-0x8), the getters' buffer at
 * rbp+0x50: w = buffer - 0x58. comp[A] = +0x5f4 (+0x24 = 1/that, 0xea23da), comp[B] = +0x5f8.
 * Pins: the refresh's bytes (its patched ones as the file has them) and M's. */
#define FR_COMP_A 0x5f4
#define FR_COMP_B 0x5f8
#define FR_W_FROM_BUFFER (-0x58)
#define FR_M_PARAMS float a,float b,float c,uint64_t flag
#define FR_M_ARGS a,b,c,flag
#define FR_P_PARAMS void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6 /* rcx, rdx, r8b, xmm3, two on the stack */
#define FR_P_ARGS o20,slot,flag,time,a5,a6
#include "lockstep_fp_redirect.h"
#define FR_REFRESH 0xea1e80u
#define FR_REFRESH_SIZE 0xb9fu
#define FR_REFRESH_SHA "2db0c3a56629a30189d8112ee5e9dec726ddabec4805380cdb7e8a6ec4c2deff"
#define FR_M_RVA 0xea3dc0u
#define FR_M_SIZE 0x44u
#define FR_M_SHA "6b00884c932de1ad5daa186fd5f34fdfbae3475228f5d91230359013a21c7694"
#define FR_SITES 8u
#define FR_TAIL 0xea29bcu
#define FR_TAIL_BACK 0xea29c2u
#define FR_TAIL_STUB 0x80u
#define FR_TAIL_DONE 0x100u
#define FR_PARAMS_STUB 0xc0u
#define FR_MULPS_STUB 0xe0u
#define FR_V_STUB 0x100u
#define FR_WRITER_STUB 0x160u
#define FR_LEN_STUB 0x180u      /* the two length stubs (below), 0x110 bytes each */
#define FR_LEN_STUB_SIZE 0x110u
#define FR_LEN_SITES 2u
#define FR_LEN_DONE 0x200u      /* fr_done bits 9 and 10 */
#define FR_H_STUB 0x3a0u        /* FR13: the four handle stubs, FR_LEN_STUB_SIZE bytes each */
#define FR_H_SITES 4u
#define FR_H_DONE 0x800u        /* fr_done bits 11..14 */
#define FR_PG_STUB (FR_H_STUB+FR_H_SITES*FR_LEN_STUB_SIZE) /* FR16: the PointGuard angle stub (0x7e0) */
#define FR_PG_DONE 0x8000u      /* fr_done bit 15 */
#define FR_CODE (FR_PG_STUB+FR_LEN_STUB_SIZE) /* bytes of stub code on the page (0x8f0) */
#define FR_P_BLOCK 0x6dcbb0u /* the first call 0x6dc610 from its table load to the +0x0..+0x1c block's end */
#define FR_P_BLOCK_SIZE 0x1eeu
#define FR_P_BLOCK_SHA "120b298b57b3001f3ae6971073a4d86d63fb4822e1be030e0631c42d50c4f9b7"
#define FR_W_SITE_BLOCK 0x6dca83u /* the defaults struct's last movups to the writer's call (rel32 as the file has it) */
#define FR_W_SITE_BLOCK_SIZE 0x31u
#define FR_W_SITE_BLOCK_SHA "845c9e9d967a86e07c60f19aa3293314c3f2d1d74f0cc0ad79c57ee469df3fdd"
#define FR_W_BODY 0x6db200u /* the writer and its multiplier helper 0x6db600, to that one's ret */
#define FR_W_BODY_SIZE 0x4fbu
#define FR_W_BODY_SHA "8aaec3da97c133118e58b96427cd2852fde5c97a6af460e9a647595e97fc09f7"
static const struct {uint32_t site,target;unsigned stub;const char *name;} fr_sites[FR_SITES]={
    {0xea22b3,0x5f91b0,0x00,"g4"},{0xea22ca,0x5f9340,0x20,"g0"},{0xea28fd,FR_M_RVA,0x40,"m4"},{0xea2932,FR_M_RVA,0x60,"m0"},
    {0xea1f2a,0x6dc610,FR_PARAMS_STUB,"params"}, /* the first call, every path: rcx [owner+0x28], rdx slot, r8b flag, xmm3 time */
    {0x6dcbda,0x6db200,FR_MULPS_STUB,"mulps"},   /* inside it: the branch that scales +0x0..+0xc and d */
    {0x6dcce6,0x3dc040,FR_V_STUB,"v"},           /* inside it: v into rsp+0x40 (rdx), right before the max block */
    {0x6dcaaf,0x6db200,FR_WRITER_STUB,"writer"}, /* inside it: the writer on the watched ships' path (rcx o20 = rdi, rdx r13, r8 rsp+0x70, r9 slot) */
};
static const unsigned char fr_tail_original[5]={0x41,0xbd,0x00,0x03,0x00}; /* mov r13d,0x300 (its last byte stays) */
/* FR10/FR11 (lockstep_fp_redirect.h fr_len_seen): the two composite-spline builders that inline BezierSpline::Set,
 * 0x857960 (CompositeSpline<5>: the path spline build 0xeb1e60 calls it at 0xeb2774; also 0x81190d, 0xebca7c) and
 * 0x31c640 (the same template, 8 callers), end every segment with the same tail:
 *   mulss V,xmm13 (0.5); ucomiss V,xmm14 (0.0); movss [r10+rax+0x18],V; jne +9; mov dword [r10+rax+0x18],1.0
 * V = xmm4 at 0x857f71, xmm9 at 0x31cc01. The segment record is r10+rax; its control points are stored at
 * +0x60..+0x90 before (0x857e5c..0x857e92, 0x31caf2..0x31cb22; rax loaded at 0x857e45 / 0x31cad5, neither register
 * written since). A jmp over the mulss (5 bytes, the whole instruction) leads to a stub: the mulss; rax, rcx, rdx,
 * r8-r11, xmm0-5 and V saved (rsp 16-aligned below the old one, rbp kept); fr_len_seen(which, r10+rax, V[0]) stores
 * the length; V[0] = its result, lanes 1-3 as they were; everything back; jmp to the tail's end (0x857f8c /
 * 0x31cc1c), where the flags are dead (inc rbx, then add rdx writes them all). The tails' other bytes stay. No
 * branch of either function and no rel32 call/jmp of .text lands inside a tail. No exact-reciprocal window overlaps a
 * tail (the last ends at 0x857f67 / 0x31cbf8); arming refuses if any patch does (fr_len_tail_clear). Pins: each block from its rax load
 * to the tail's end through the view (its exact-reciprocal windows as the file has them). */
static const struct {uint32_t site,back,pin,pin_size;unsigned reg;const char *sha;} fr_len_sites[FR_LEN_SITES]={
    {0x857f71u,0x857f8cu,0x857e45u,0x159u,4,"24bd229b7ebe7a0474ffd3a6e2d35d1d190736d5c3addcd39c9075b3bd8f8bf5"},
    {0x31cc01u,0x31cc1cu,0x31cad5u,0x160u,9,"a117e928fccb5687bde699598f8312b7150b065e35bf0dcbf12a590c3870c565"}};
static const unsigned char fr_len_original[FR_LEN_SITES][5]={{0xf3,0x41,0x0f,0x59,0xe5},{0xf3,0x45,0x0f,0x59,0xcd}}; /* mulss V,xmm13 */
/* FR13 (lockstep_fp_redirect.h fr_handle_seen): the same builders' Bezier handle length where the caller left it 0,
 * s = d * xmm8 with xmm8 = 0.333333343f (Linux divides by 3.0): mulss xmm2,xmm8 (f3 41 0f 59 d0, the whole
 * instruction) right after an exact-reciprocal window that makes d = sqrt(...) (the windows {0x857d11,8}, {0x857de8,8},
 * {0x31c9a1,8}, {0x31ca78,8} end at the sites; their trampolines return there). 0x857d19 / 0x31c9a9 the out-handle
 * (then jmp to its store [r9+rbx*8+4]), 0x857df0 / 0x31ca80 the in-handle (store [rcx+rbx*8+8] next). A jmp over the
 * mulss leads to a stub (the length stub's frame, no leading instruction): fr_handle_seen(which, xmm2[0], xmm8[0]);
 * xmm2[0] = its result, lanes 1-3 as they were; back after the site. Flags are dead there (comiss 0x857dbe / 0x31ca4e,
 * ucomiss 0x857f76 / 0x31cc06 come first); no branch of either builder and no .text rel32 lands in bytes 1..4; xmm8 is
 * written only by the prologue's load (0x857c7b / 0x31c90b) and the epilogue. Pins: each builder from that load to its
 * in-handle's end, through the view (the windows and these sites as the file has them). */
static const struct {uint32_t site,back;unsigned pin;} fr_h_sites[FR_H_SITES]={
    {0x857d19u,0x857d1eu,0},{0x857df0u,0x857df5u,0},{0x31c9a9u,0x31c9aeu,1},{0x31ca80u,0x31ca85u,1}};
static const struct {uint32_t pin,pin_size;const char *sha;} fr_h_pins[2]={
    {0x857c7bu,0x17au,"91d418701453a49e8e6dcd5a1a86fd8215a44e45903d8fe991816b7612090b25"},
    {0x31c90bu,0x17au,"b0540d91a5daf95243441ba3ed4109e47680e67f870dc5cdd76ce460311d67cf"}};
static const unsigned char fr_h_original[5]={0xf3,0x41,0x0f,0x59,0xd0}; /* mulss xmm2,xmm8 */
/* FR16 (lockstep_fp_redirect.h fr_pg_seen): PointGuardFormationShape vtable slot 6 0xe9c380 forms the slot's angle as
 *   0xe9c4bc movss xmm0,[0x2cbeab4] (pi_f, f3 0f 10 05 f0 25 e2 01); 0xe9c4c4 divss xmm0,xmm2 (xmm2 = (float)(m+1));
 *   0xe9c4c8 mulss xmm1,xmm0 (xmm1 = (float)rem); 0xe9c4cc movaps xmm0,xmm1; call 0x1750210 (sincos).
 * A jmp on the movss leads to a stub (the length stub's frame, no leading instruction, V = xmm1):
 * fr_pg_seen(0, xmm1[0], xmm2[0]); xmm1[0] = its result, lanes 1-3 as they were; back to 0xe9c4cc, whose movaps
 * hands it on (xmm0, dead until there, is not restored by native either). Both conversion paths (signed and the
 * >= 2^63 halving one) join at the movss; the only branch to it is 0xe9c4a5 (to its first byte), no branch of the
 * function and no .text rel32 lands in 0xe9c4bd..0xe9c4cb; flags are dead (the call at 0xe9c4cf comes first); no
 * volatile register is read after the call before being written. Pins: the whole function through the view, and
 * pi_f at 0x2cbeab4. No other module patches or pins this function. */
#define FR_PG_SITE 0xe9c4bcu
#define FR_PG_BACK 0xe9c4ccu
#define FR_PG_PIN 0xe9c380u
#define FR_PG_PIN_SIZE 0x27au
#define FR_PG_PIN_SHA "de55bc8f392aaeeae12e556805cb1d3087c641c6aa09f9e1daebeccd05aac051"
#define FR_PG_PI 0x2cbeab4u
#define FR_PG_PI_BITS 0x40490fdbu
static const unsigned char fr_pg_original[5]={0xf3,0x0f,0x10,0x05,0xf0}; /* movss xmm0,[rip+0x1e225f0], first 5 bytes */
static unsigned char *fr_page;
static uint32_t fr_done;
/* G4 with the slot: the refresh's path with the getters begins here. */
static float fr_g4_slot_hook(void *comp,void *o20,void *buf,void *slot) {
    fr_begin(slot);
    return fr_g4_hook(comp,o20,buf);
}
/* Every exit of the refresh, with its slot. */
static void fr_tail_hook(void *slot) {
    InterlockedIncrement64(&fr_calls);
    fr_ctx *c=fr_context(0);
    if(!c || !c->depth || c->slot!=slot) {if(fr_comparing()) InterlockedIncrement64(&fr_skipped);return;} /* the flag or early path */
    fr_end(c);
}
/* The first call's v and branch (lockstep_fp_redirect.h fr_p_new): 0x3dc040 at 0x6dcce6 (rcx ship, rdx v = rsp+0x40,
 * xmm2 time, r9, three on the stack; d is xmm7, callee-saved, which the stub passes as an eighth argument) and
 * 0x6db200 at 0x6dcbda (four arguments). K = [[0x146cf1538]+0x300] (r15, loaded at 0x6dcbb0). */
typedef uint64_t (*fr_v_fn)(void *,unsigned char *,double,uint64_t,uint64_t,uint64_t,uint64_t);
static fr_v_fn fr_native_v;
static uint64_t fr_v_hook(void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,float d) {
    const uint64_t r=fr_native_v(o20,buf,time,a4,a5,a6,a7);fr_v_seen(buf,d);return r;
}
/* Both calls of the writer 0x6db200 (rcx o20, rdx D, r8 A, r9 slot) run through fr_writer_run: the 'mulps' branch's
 * at 0x6dcbda (marked first) and the watched ships' at 0x6dcaaf (its stub jumps straight there). */
static uint64_t fr_mulps_hook(void *a,void *b,void *c,void *e) {fr_mulps_seen();return fr_writer_run(a,b,c,e);}
static uint32_t fr_k_read(void) {
    const unsigned char *table=NULL;uint32_t k=0;
    if(!read_local(image+0x6cf1538u,&table,8) || !table || !read_local(table+0x300,&k,4)) return 0;
    return k;
}
/* The writer's layout on Windows: table [0x146cf1538] (+0x5050 scale, +0x300 K, +0x318 clamp), D +0x904..+0x914,
 * multipliers +0x988..+0x994 (0x6db600), virtuals +0x1fe8..+0x2008. */
static int fr_w_read(void *o20,const unsigned char *data,const unsigned char *a,fr_w_in *in) {
    return fr_w_gather(o20,data,a,image+0x6cf1538u,0x5050,0x300,0x318,0x904,0x988,0x1fe8,in);
}
static void fr_write_jump(unsigned char *p,const void *target) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};memcpy(p,code,6);memcpy(p+6,&target,8);
}
/* [REX.R] 0F op modrm(reg, [rsp+disp32]): movups loads (0x10) and stores (0x11) of the length stub's frame. */
static unsigned fr_len_sse(unsigned char *p,unsigned op,unsigned reg,uint32_t disp) {
    unsigned n=0;if(reg>=8) p[n++]=0x44;
    p[n++]=0x0f;p[n++]=(unsigned char)op;p[n++]=(unsigned char)(0x84|(reg&7)<<3);p[n++]=0x24;memcpy(p+n,&disp,4);return n+4;
}
/* The length stub of site i (FR_LEN_STUB_SIZE bytes; the hook and back pointers at +0xf8 / +0x100):
 *   mulss V,xmm13; push rbp; mov rbp,rsp; and rsp,-16; sub rsp,0xd0; rax rcx rdx r8-r11 -> [rsp+0x20..0x58];
 *   xmm0-5 -> [rsp+0x60..0xb0]; V -> [rsp+0xc0]; mov ecx,i; lea rdx,[r10+rax]; mov r8d,[rsp+0xc0];
 *   call [rip] -> fr_len_seen; mov [rsp+0xc0],eax; xmm0-5 back; V <- [rsp+0xc0]; the GPRs back; mov rsp,rbp; pop rbp;
 *   jmp [rip] -> the tail's end. */
static void fr_stub_write(unsigned char *p,const unsigned char *first,unsigned first_n,unsigned v,uint32_t which,
                          const unsigned char *args,unsigned args_n,void *hook,void *back) {
    unsigned n=0;
    memset(p,0xcc,FR_LEN_STUB_SIZE);
    if(first_n) {memcpy(p,first,first_n);n=first_n;}
    static const unsigned char enter[]={0x55,0x48,0x89,0xe5,0x48,0x83,0xe4,0xf0,0x48,0x81,0xec,0xd0,0x00,0x00,0x00,
        0x48,0x89,0x44,0x24,0x20,0x48,0x89,0x4c,0x24,0x28,0x48,0x89,0x54,0x24,0x30,0x4c,0x89,0x44,0x24,0x38,
        0x4c,0x89,0x4c,0x24,0x40,0x4c,0x89,0x54,0x24,0x48,0x4c,0x89,0x5c,0x24,0x50};
    memcpy(p+n,enter,sizeof(enter));n+=sizeof(enter);
    for(unsigned r=0;r<6;r++) n+=fr_len_sse(p+n,0x11,r,0x60+0x10*r);
    n+=fr_len_sse(p+n,0x11,v,0xc0);
    p[n++]=0xb9;memcpy(p+n,&which,4);n+=4;                                             /* mov ecx,which */
    memcpy(p+n,args,args_n);n+=args_n;                                                   /* ...; call [rip] (ff 15) */
    int32_t d=(int32_t)(0xf8-(n+4));memcpy(p+n,&d,4);n+=4;
    static const unsigned char keep[]={0x89,0x84,0x24,0xc0,0x00,0x00,0x00};                /* mov [rsp+0xc0],eax */
    memcpy(p+n,keep,sizeof(keep));n+=sizeof(keep);
    for(unsigned r=0;r<6;r++) n+=fr_len_sse(p+n,0x10,r,0x60+0x10*r);
    n+=fr_len_sse(p+n,0x10,v,0xc0);
    static const unsigned char leave[]={0x48,0x8b,0x44,0x24,0x20,0x48,0x8b,0x4c,0x24,0x28,0x48,0x8b,0x54,0x24,0x30,
        0x4c,0x8b,0x44,0x24,0x38,0x4c,0x8b,0x4c,0x24,0x40,0x4c,0x8b,0x54,0x24,0x48,0x4c,0x8b,0x5c,0x24,0x50,
        0x48,0x89,0xec,0x5d,0xff,0x25};
    memcpy(p+n,leave,sizeof(leave));n+=sizeof(leave);
    d=(int32_t)(0x100-(n+4));memcpy(p+n,&d,4);
    memcpy(p+0xf8,&hook,8);memcpy(p+0x100,&back,8);
}
static void fr_len_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char args[]={0x49,0x8d,0x14,0x02,0x44,0x8b,0x84,0x24,0xc0,0x00,0x00,0x00,0xff,0x15}; /* lea rdx,[r10+rax]; mov r8d,[rsp+0xc0] */
    fr_stub_write(p,fr_len_original[i],5,fr_len_sites[i].reg,i,args,sizeof(args),(void*)fr_len_seen,(void*)(image+fr_len_sites[i].back));
}
/* FR13: the handle stub of site i: the same frame without a leading instruction, V = xmm2;
 *   mov ecx,i; movd edx,xmm2 (d); movd r8d,xmm8 (k); call [rip] -> fr_handle_seen; xmm2[0] = its result; back after the site. */
static void fr_h_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char args[]={0x66,0x0f,0x7e,0xd2,0x66,0x45,0x0f,0x7e,0xc0,0xff,0x15};
    fr_stub_write(p,NULL,0,2,i,args,sizeof(args),(void*)fr_handle_seen,(void*)(image+fr_h_sites[i].back));
}
/* FR16: the PointGuard angle stub: the same frame, V = xmm1;
 *   mov ecx,0; movd edx,xmm1 (rem); movd r8d,xmm2 (m+1); call [rip] -> fr_pg_seen; xmm1[0] = its result; back to 0xe9c4cc. */
static void fr_pg_write_stub(unsigned char *p) {
    static const unsigned char args[]={0x66,0x0f,0x7e,0xca,0x66,0x41,0x0f,0x7e,0xd0,0xff,0x15};
    fr_stub_write(p,NULL,0,1,0,args,sizeof(args),(void*)fr_pg_seen,(void*)(image+FR_PG_BACK));
}
/* 0x00 mov r9,r15; jmp [rip] -> fr_g4_slot_hook | 0x20/0x40/0x60 jmp [rip] -> G0, M4, M0 hooks | 0x80 the tail:
 * mov rcx,r15; sub rsp,0x20; call [rip] -> fr_tail_hook; add rsp,0x20; mov r13d,0x300; jmp [rip] -> 0xea29c2 |
 * 0xc0 jmp [rip] -> fr_params_hook | 0xe0 -> fr_mulps_hook | 0x100 the v stub | 0x160 jmp [rip] -> fr_writer_run. */
static void fr_write_code(unsigned char *p) {
    memset(p,0xcc,FR_CODE);
    p[0]=0x4d;p[1]=0x89;p[2]=0xf9;fr_write_jump(p+3,(void*)fr_g4_slot_hook);
    fr_write_jump(p+0x20,(void*)fr_g0_hook);fr_write_jump(p+0x40,(void*)fr_m4_hook);fr_write_jump(p+0x60,(void*)fr_m0_hook);
    static const unsigned char tail[]={0x4c,0x89,0xf9,0x48,0x83,0xec,0x20,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x20,0x41,0xbd,0x00,0x03,0x00,0x00,0xff,0x25,0,0,0,0};
    unsigned char *t=p+FR_TAIL_STUB;memcpy(t,tail,sizeof(tail));
    int32_t d=(int32_t)(0x30-13);memcpy(t+9,&d,4);d=(int32_t)(0x38-(int32_t)sizeof(tail));memcpy(t+25,&d,4);
    void *hook=(void*)fr_tail_hook,*back=(void*)(image+FR_TAIL_BACK);memcpy(t+0x30,&hook,8);memcpy(t+0x38,&back,8);
    fr_write_jump(p+FR_PARAMS_STUB,(void*)fr_params_hook);fr_write_jump(p+FR_MULPS_STUB,(void*)fr_mulps_hook);
    /* 0x100: sub rsp,0x58; movss [rsp+0x38],xmm7; the three stack arguments down by 0x58; call [rip] -> fr_v_hook;
     * add rsp,0x58; ret (rsp 16-aligned at the call: the call site's alignment minus 8 minus 0x58). */
    static const unsigned char vstub[]={0x48,0x83,0xec,0x58,0xf3,0x0f,0x11,0x7c,0x24,0x38,
        0x48,0x8b,0x84,0x24,0x80,0,0,0,0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0x84,0x24,0x88,0,0,0,0x48,0x89,0x44,0x24,0x28,
        0x48,0x8b,0x84,0x24,0x90,0,0,0,0x48,0x89,0x44,0x24,0x30,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x58,0xc3};
    unsigned char *s=p+FR_V_STUB;memcpy(s,vstub,sizeof(vstub));
    d=(int32_t)(0x48-0x37);memcpy(s+0x33,&d,4);void *vh=(void*)fr_v_hook;memcpy(s+0x48,&vh,8);
    fr_write_jump(p+FR_WRITER_STUB,(void*)fr_writer_run); /* 0x160 */
    for(unsigned i=0;i<FR_LEN_SITES;i++) fr_len_write_stub(p+FR_LEN_STUB+FR_LEN_STUB_SIZE*i,i);
    for(unsigned i=0;i<FR_H_SITES;i++) fr_h_write_stub(p+FR_H_STUB+FR_LEN_STUB_SIZE*i,i);
    fr_pg_write_stub(p+FR_PG_STUB);
}
static int32_t fr_rel(uint32_t from,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+from+5));}
static int32_t fr_rel_ours(unsigned i) {return fr_rel(fr_sites[i].site,fr_page+fr_sites[i].stub);}
static int32_t fr_rel_native(unsigned i) {return fr_rel(fr_sites[i].site,image+fr_sites[i].target);}
/* 0 original, 1 ours, -1 anything else. */
static int fr_site_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+fr_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==fr_rel_native(i)) return 0;
    if(fr_page && d==fr_rel_ours(i)) return 1;
    return -1;
}
static void fr_tail_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_TAIL,fr_page+FR_TAIL_STUB);memcpy(out+1,&d,4);}
static int fr_tail_state(void) {
    unsigned char now[5],ours[5];if(!read_local(image+FR_TAIL,now,5)) return -1;
    if(!memcmp(now,fr_tail_original,5)) return 0;
    if(fr_page) {fr_tail_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
/* The length sites: 0 original, 1 our jmp, -1 anything else (the raw bytes: the view shows the original ones). */
static unsigned char *fr_len_stub(unsigned i) {return fr_page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i;}
static int fr_len_reach(unsigned i) {
    const intptr_t d=(intptr_t)fr_len_stub(i)-(intptr_t)(image+fr_len_sites[i].site+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_len_jump(unsigned i,unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(fr_len_sites[i].site,fr_len_stub(i));memcpy(out+1,&d,4);}
static int fr_len_state(unsigned i) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+fr_len_sites[i].site),5);
    if(!memcmp(now,fr_len_original[i],5)) return 0;
    if(fr_page && fr_len_reach(i)) {fr_len_jump(i,ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
/* The original-byte view (lockstep_gate.c fp_patch_view) over an installed length site. */
static int fr_len_overlaps(const void *source,size_t bytes) {
    const uintptr_t a=(uintptr_t)source,b=a+bytes;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fr_len_sites[i].site);
        if(fr_done&(FR_LEN_DONE<<i) && s<b && s+5>a) return 1;
    }
    for(unsigned i=0;i<FR_H_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fr_h_sites[i].site);
        if(fr_done&(FR_H_DONE<<i) && s<b && s+5>a) return 1;
    }
    const uintptr_t s=(uintptr_t)(image+FR_PG_SITE);
    if(fr_done&FR_PG_DONE && s<b && s+5>a) return 1;
    return 0;
}
static void fr_len_unpatch_copy(const void *source,void *copy,size_t bytes) {
    const uintptr_t a=(uintptr_t)source;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        if(!(fr_done&(FR_LEN_DONE<<i))) continue;
        const uintptr_t s=(uintptr_t)(image+fr_len_sites[i].site);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_len_original[i][k];
    }
    for(unsigned i=0;i<FR_H_SITES;i++) {
        if(!(fr_done&(FR_H_DONE<<i))) continue;
        const uintptr_t s=(uintptr_t)(image+fr_h_sites[i].site);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_h_original[k];
    }
    if(fr_done&FR_PG_DONE) {
        const uintptr_t s=(uintptr_t)(image+FR_PG_SITE);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_pg_original[k];
    }
}
/* No other module has patched site i's tail (site..back): its raw bytes are what the original-byte view shows. (Not
 * fp_patch_overlaps: the exact-reciprocal view answers 'yes' for the whole image once it is armed, which it is
 * before this module in the boot; FR10/FR11/W5 refused there.) */
static int fr_len_tail_clear(unsigned i) {
    unsigned char raw[0x20],viewed[0x20];const unsigned n=fr_len_sites[i].back-fr_len_sites[i].site;
    if(n>sizeof(raw) || !read_local(image+fr_len_sites[i].site,viewed,n)) return 0;
    memcpy(raw,(const void*)(image+fr_len_sites[i].site),n);
    return !memcmp(raw,viewed,n);
}
/* FR13: the handle sites, as the length sites (0 original, 1 our jmp, -1 anything else; clear = raw bytes are what the
 * view shows; pins through the view). */
static unsigned char *fr_h_stub(unsigned i) {return fr_page+FR_H_STUB+FR_LEN_STUB_SIZE*i;}
static int fr_h_reach(unsigned i) {
    const intptr_t d=(intptr_t)fr_h_stub(i)-(intptr_t)(image+fr_h_sites[i].site+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_h_jump(unsigned i,unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(fr_h_sites[i].site,fr_h_stub(i));memcpy(out+1,&d,4);}
static int fr_h_state(unsigned i) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+fr_h_sites[i].site),5);
    if(!memcmp(now,fr_h_original,5)) return 0;
    if(fr_page && fr_h_reach(i)) {fr_h_jump(i,ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_h_clear(unsigned i) {
    unsigned char viewed[5];
    return read_local(image+fr_h_sites[i].site,viewed,5) && !memcmp((const void*)(image+fr_h_sites[i].site),viewed,5);
}
static int fr_h_pins_ok(void) {
    static unsigned char block[0x200];
    for(unsigned i=0;i<2;i++)
        if(fr_h_pins[i].pin_size>sizeof(block) || !read_local(image+fr_h_pins[i].pin,block,fr_h_pins[i].pin_size) ||
           !hash_ok(block,fr_h_pins[i].pin_size,fr_h_pins[i].sha)) return 0;
    return 1;
}
/* FR16: the PointGuard angle site, as the handle sites; clear = the raw bytes of site..back are what the view shows. */
static unsigned char *fr_pg_stub(void) {return fr_page+FR_PG_STUB;}
static int fr_pg_reach(void) {
    const intptr_t d=(intptr_t)fr_pg_stub()-(intptr_t)(image+FR_PG_SITE+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_pg_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_PG_SITE,fr_pg_stub());memcpy(out+1,&d,4);}
static int fr_pg_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+FR_PG_SITE),5);
    if(!memcmp(now,fr_pg_original,5)) return 0;
    if(fr_page && fr_pg_reach()) {fr_pg_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_pg_clear(void) {
    unsigned char viewed[FR_PG_BACK-FR_PG_SITE];
    return read_local(image+FR_PG_SITE,viewed,sizeof(viewed)) && !memcmp((const void*)(image+FR_PG_SITE),viewed,sizeof(viewed));
}
static int fr_pg_pins_ok(void) {
    static unsigned char block[FR_PG_PIN_SIZE];uint32_t pi=0;
    return read_local(image+FR_PG_PIN,block,FR_PG_PIN_SIZE) && hash_ok(block,FR_PG_PIN_SIZE,FR_PG_PIN_SHA) &&
           read_local(image+FR_PG_PI,&pi,4) && pi==FR_PG_PI_BITS;
}
static int fr_len_pins(void) {
    static unsigned char block[0x200];
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_len_sites[i].pin_size>sizeof(block) || !read_local(image+fr_len_sites[i].pin,block,fr_len_sites[i].pin_size) ||
           !hash_ok(block,fr_len_sites[i].pin_size,fr_len_sites[i].sha)) return 0;
    return 1;
}
/* The refresh's bytes with the patched ones as the file has them, and M's. */
static int fr_pins(void) {
    static unsigned char copy[FR_REFRESH_SIZE];
    static unsigned char block[FR_P_BLOCK_SIZE],wsite[FR_W_SITE_BLOCK_SIZE],wbody[FR_W_BODY_SIZE];
    if(!read_local(image+FR_REFRESH,copy,FR_REFRESH_SIZE) || !read_local(image+FR_P_BLOCK,block,FR_P_BLOCK_SIZE) || fr_tail_state()<0) return 0;
    if(!read_local(image+FR_W_SITE_BLOCK,wsite,FR_W_SITE_BLOCK_SIZE) || !read_local(image+FR_W_BODY,wbody,FR_W_BODY_SIZE)) return 0;
    for(unsigned i=0;i<FR_SITES;i++) {
        if(fr_site_state(i)<0) return 0;
        const uint32_t s=fr_sites[i].site;const int32_t d=fr_rel_native(i);
        if(s>=FR_REFRESH && s+5<=FR_REFRESH+FR_REFRESH_SIZE) memcpy(copy+(s-FR_REFRESH)+1,&d,4);
        if(s>=FR_P_BLOCK && s+5<=FR_P_BLOCK+FR_P_BLOCK_SIZE) memcpy(block+(s-FR_P_BLOCK)+1,&d,4);
        if(s>=FR_W_SITE_BLOCK && s+5<=FR_W_SITE_BLOCK+FR_W_SITE_BLOCK_SIZE) memcpy(wsite+(s-FR_W_SITE_BLOCK)+1,&d,4);
    }
    memcpy(copy+(FR_TAIL-FR_REFRESH),fr_tail_original,5);
    unsigned char m[FR_M_SIZE];
    return hash_ok(copy,FR_REFRESH_SIZE,FR_REFRESH_SHA) && hash_ok(block,FR_P_BLOCK_SIZE,FR_P_BLOCK_SHA) &&
        hash_ok(wsite,FR_W_SITE_BLOCK_SIZE,FR_W_SITE_BLOCK_SHA) && hash_ok(wbody,FR_W_BODY_SIZE,FR_W_BODY_SHA) &&
        read_local(image+FR_M_RVA,m,FR_M_SIZE) && hash_ok(m,FR_M_SIZE,FR_M_SHA) && fr_len_pins() && fr_h_pins_ok() && fr_pg_pins_ok();
}
static unsigned char *fr_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+FR_REFRESH)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+FR_REFRESH);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int fr_site_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(fr_sites[i].site+1,e,v,4);
}
static void fr_restore(void) {
    if(!fr_installed) return;
    /* G4 first: no new refresh begins while the rest goes back. */
    for(unsigned i=0;i<FR_SITES;i++)
        if((fr_done>>i)&1u && fr_site_state(i)==1 && fr_site_cas(i,fr_rel_ours(i),fr_rel_native(i))) fr_done&=~(1u<<i);
    if(fr_done&FR_TAIL_DONE && fr_tail_state()==1) {
        unsigned char jump[5];fr_tail_jump(jump);
        if(view_cas_bytes(FR_TAIL,jump,fr_tail_original,5)) fr_done&=~FR_TAIL_DONE;
    }
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_done&(FR_LEN_DONE<<i) && fr_len_state(i)==1) {
            unsigned char jump[5];fr_len_jump(i,jump);
            if(view_cas_bytes(fr_len_sites[i].site,jump,fr_len_original[i],5)) fr_done&=~(FR_LEN_DONE<<i);
        }
    for(unsigned i=0;i<FR_H_SITES;i++)
        if(fr_done&(FR_H_DONE<<i) && fr_h_state(i)==1) {
            unsigned char jump[5];fr_h_jump(i,jump);
            if(view_cas_bytes(fr_h_sites[i].site,jump,fr_h_original,5)) fr_done&=~(FR_H_DONE<<i);
        }
    if(fr_done&FR_PG_DONE && fr_pg_state()==1) {
        unsigned char jump[5];fr_pg_jump(jump);
        if(view_cas_bytes(FR_PG_SITE,jump,fr_pg_original,5)) fr_done&=~FR_PG_DONE;
    }
    if(!fr_done) {fr_installed=0;fr_restored=1;fr_mode=FR_OFF;}
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int fr_arm(int mode,uint64_t ticks) {
    if(fr_installed) return stats.active && fr_mode==mode && fr_ticks==ticks;
    if(!fr_arm_ready(mode,ticks)) return 0;
    if(fr_tail_state()!=0) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_SITES;i++) if(fr_site_state(i)!=0) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_len_state(i)!=0 || !fr_len_tail_clear(i)) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_H_SITES;i++)
        if(fr_h_state(i)!=0 || !fr_h_clear(i)) {fr_stage="pins";return 0;}
    if(fr_pg_state()!=0 || !fr_pg_clear()) {fr_stage="pins";return 0;}
    if(!fr_pins()) {fr_stage="pins";return 0;}
    if(!fr_page) fr_page=fr_alloc_page();
    if(!fr_page) {fr_stage="page";return 0;}
    for(unsigned i=0;i<FR_LEN_SITES;i++) if(!fr_len_reach(i)) {fr_stage="page";return 0;}
    for(unsigned i=0;i<FR_H_SITES;i++) if(!fr_h_reach(i)) {fr_stage="page";return 0;}
    if(!fr_pg_reach()) {fr_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old)) {fr_stage="page";return 0;}
    fr_write_code(fr_page);
    if(!VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {fr_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),fr_page,0x1000);
    fr_native_refresh=(fr_refresh_fn)(void*)(image+FR_REFRESH);
    fr_native_g4=(fr_getter_fn)(void*)(image+0x5f91b0u);fr_native_g0=(fr_getter_fn)(void*)(image+0x5f9340u);
    fr_native_m=(fr_m_fn)(void*)(image+FR_M_RVA);fr_native_params=(fr_params_fn)(void*)(image+0x6dc610u);
    fr_native_v=(fr_v_fn)(void*)(image+0x3dc040u);fr_native_writer=(fr_writer_fn)(void*)(image+FR_W_BODY);
    fr_mode=mode;fr_installed=1;fr_restored=0;fr_done=0;
    /* The length sites first (independent of the refresh), then G0, the Ms and the first call, then the tail, G4
     * last: a refresh that begins finds the rest in place. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char jump[5];fr_len_jump(i,jump);
        if(!view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_LEN_DONE<<i;
    }
    for(unsigned i=0;i<FR_H_SITES;i++) { /* FR13: as the length sites, independent of the refresh */
        unsigned char jump[5];fr_h_jump(i,jump);
        if(!view_cas_bytes(fr_h_sites[i].site,fr_h_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_H_DONE<<i;
    }
    { /* FR16: the PointGuard angle, likewise */
        unsigned char jump[5];fr_pg_jump(jump);
        if(!view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_PG_DONE;
    }
    for(unsigned k=1;k<=FR_SITES;k++) {
        const unsigned i=k%FR_SITES;
        if(i==0) {
            unsigned char jump[5];fr_tail_jump(jump);
            if(!view_cas_bytes(FR_TAIL,fr_tail_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
            fr_done|=FR_TAIL_DONE;
        }
        if(!fr_site_cas(i,fr_rel_native(i),fr_rel_ours(i))) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=1u<<i;
    }
    fr_stage="armed";return 1;
}
static int fr_owned(void) {
    for(unsigned i=0;i<FR_SITES;i++) if((fr_done>>i)&1u && fr_site_state(i)!=1) return 0;
    if(fr_done&FR_TAIL_DONE && fr_tail_state()!=1) return 0;
    for(unsigned i=0;i<FR_LEN_SITES;i++) if(fr_done&(FR_LEN_DONE<<i) && fr_len_state(i)!=1) return 0;
    for(unsigned i=0;i<FR_H_SITES;i++) if(fr_done&(FR_H_DONE<<i) && fr_h_state(i)!=1) return 0;
    if(fr_done&FR_PG_DONE && fr_pg_state()!=1) return 0;
    unsigned char want[FR_CODE];fr_write_code(want);
    return !memcmp(fr_page,want,FR_CODE);
}
static int fr_check(void) {
    if(InterlockedCompareExchange64(&fr_failures,0,0)) return 0;
    if(!fr_owned()) {fr_stage="site_changed";InterlockedIncrement64(&fr_failures);return 0;}
    return 1;
}
/* The Lua methods (lockstep_fp_redirect_lua.h) come with the gate's other ones, after its Lua ABI. */
