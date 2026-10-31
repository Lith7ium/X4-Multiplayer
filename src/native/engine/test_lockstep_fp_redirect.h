/* native_gate_fp_redirect (lockstep_fp_redirect.h, lockstep_fp_redirect_win.h): the strict vectors; the shadow
 * flow with stand-in natives (getters, M, and a refresh that calls the hooks as the executable's does, its frame
 * as there), the load's calls keyed after admission; counters, status and rows; arming refusals; the G4 and tail
 * stubs run; site CAS, ownership and restore on the plain image. On
 * the native image: the pins, and the executable's own M (0xea3dc0) and inlined +0x0 block (0xea22cf..0xea2396)
 * run on its bytes against ours. */
#include "test_fp_redirect_vectors.h"
static unsigned char tfr_comp[0x700],tfr_ship[0x20];
static float tfr_w;
static uint32_t tfr_perturb;
static int tfr_branch;
static float tfr_g4(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tfr_comp && o20==tfr_ship);return fr_f32(0x43c9859bu);}
static float tfr_g0(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tfr_comp && o20==tfr_ship);return fr_f32(0x4383fc81u);}
static float tfr_m(float a,float b,float c,uint64_t flag) {return fr_m((unsigned)(flag&0xffu),a,b,c);}
/* The first call: writes +0x10c (10.0) on every path. */
static void tfr_params(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)flag;(void)time;(void)a5;(void)a6;assert(o20==tfr_ship);const float ten=10.0f;memcpy(slot+0x10c,&ten,4);
}
/* The executable's order: G4 (where the path begins, with the slot) -> +0x4, G0, +0x0 = G0 * L, +0x24, +0x8/+0xc =
 * 400; the M branch on request; every path ends at the tail. */
static void tfr_refresh(void *owner,void *slot,uint64_t flag) {
    unsigned char frame[0x100],*buf=frame+0x80,*s=slot;(void)owner;
    fr_params_hook(tfr_ship,s,flag,0.0,0,0);
    if(!(flag&1)) {
        memcpy(buf+FR_W_FROM_BUFFER,&tfr_w,4);
        const float g4=fr_g4_slot_hook(tfr_comp,tfr_ship,buf,slot);memcpy(s+4,&g4,4);
        const float g0=fr_g0_hook(tfr_comp,tfr_ship,buf);
        uint32_t ca,cb,out[3],four=0x43c80000u;memcpy(&ca,tfr_comp+FR_COMP_A,4);memcpy(&cb,tfr_comp+FR_COMP_B,4);
        fr_recompute(ca,cb,fr_bits(g4),fr_bits(g0),fr_bits(tfr_w),out);out[0]+=tfr_perturb;
        memcpy(s,&out[0],4);memcpy(s+0x24,&out[2],4);memcpy(s+8,&four,4);memcpy(s+0xc,&four,4);
        if(tfr_branch) {
            float a,b,v,m;memcpy(&a,s+0x2c,4);memcpy(&b,s+0x24,4);
            m=fr_m4_hook(a,b,fr_f32(0x43960000u),1);memcpy(&v,s+4,4);v=m*v;memcpy(s+4,&v,4);
            m=fr_m0_hook(b,a,fr_f32(0x43960000u),1);memcpy(&v,s,4);v=m*v;memcpy(s,&v,4);
        }
    }
    fr_tail_hook(slot);
}
/* The page's two code stubs run for real: G4's (mov r9,r15; jmp) and the tail's (call with r15, mov r13d, jmp back). */
__asm__(
".text\n"
"test_fr_g4_run:\n push %r15\n mov 0x30(%rsp),%r15\n mov %rcx,%r10\n mov %rdx,%rcx\n mov %r8,%rdx\n mov %r9,%r8\n sub $0x20,%rsp\n call *%r10\n add $0x20,%rsp\n pop %r15\n ret\n"
"test_fr_tail_run:\n push %rbp\n push %r15\n push %r13\n push %rbx\n mov %rsp,%rbp\n and $-16,%rsp\n mov %rdx,%r15\n xor %r13d,%r13d\n jmp *%rcx\n"
"test_fr_tail_back:\n mov %r13d,%eax\n mov %rbp,%rsp\n pop %rbx\n pop %r13\n pop %r15\n pop %rbp\n ret\n"
);
extern float test_fr_g4_run(const void *stub,void *comp,void *o20,void *buf,void *slot);
/* FR10/FR11: a length stub runs for real. r10 + rax = the segment record, xmm0-5 and xmm9 from the block (+0x00..+0x70),
 * xmm13 from +0x70 (0.5), sentinels in rcx, rdx, r8, r9, r11, rsp 8 off 16; the stub's back jump (planted at
 * image+back) leads to test_fr_len_back, which stores xmm0-5, xmm9 (+0x60) and rax, rcx, rdx, r8-r11, rbx (+0x80..). */
__asm__(
".text\n"
"test_fr_len_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm9,(%rsp)\n movdqu %xmm13,0x10(%rsp)\n"
" mov %r9,%rsi\n mov %rcx,%rbx\n mov %rdx,%r10\n mov %r8,%rax\n"
" movups (%rsi),%xmm0\n movups 0x10(%rsi),%xmm1\n movups 0x20(%rsi),%xmm2\n movups 0x30(%rsi),%xmm3\n movups 0x40(%rsi),%xmm4\n"
" movups 0x50(%rsi),%xmm5\n movups 0x60(%rsi),%xmm9\n movups 0x70(%rsi),%xmm13\n"
" movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n movabs $0x9999999999999999,%r9\n"
" movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *%rbx\n"
"test_fr_len_back:\n movups %xmm0,(%rsi)\n movups %xmm1,0x10(%rsi)\n movups %xmm2,0x20(%rsi)\n movups %xmm3,0x30(%rsi)\n movups %xmm4,0x40(%rsi)\n"
" movups %xmm5,0x50(%rsi)\n movups %xmm9,0x60(%rsi)\n mov %rax,0x80(%rsi)\n mov %rcx,0x88(%rsi)\n mov %rdx,0x90(%rsi)\n mov %r8,0x98(%rsi)\n"
" mov %r9,0xa0(%rsi)\n mov %r10,0xa8(%rsi)\n mov %r11,0xb0(%rsi)\n mov %rbx,0xb8(%rsi)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm9\n movdqu 0x10(%rsp),%xmm13\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_len_run(const void *stub,unsigned char *r10,uint64_t rax,unsigned char *block);
extern char test_fr_len_back[];
/* Synthetic (not recorded) control points: the canonical length and Windows' pairwise one, by numpy float32. */
static const struct {uint32_t p[16],canonical,pairwise;} tfl_vectors[]={
    {{0xc459c812u,0x43d4eac9u,0xc50bec69u,0x448d5898u,0x45107ec8u,0x448c18c5u,0xc3a39c4fu,0x448a544eu,0x44ecabffu,0x449d0b6au,0x44fb79f3u,0x450b610du,0xc50ee54au,0x42b4e758u,0xc50705e2u,0xc47973cfu},0x45d50687u,0x45d50688u},
    {{0x45364516u,0x45350560u,0xc4fce866u,0xc5360ca1u,0x44dd4fffu,0x4400b6e4u,0x44e62cbau,0xc52bb60bu,0x42e202edu,0x452263f9u,0xc3fc140cu,0x43ccd111u,0x44e7a6a8u,0x4417e44au,0xc4f63aabu,0x44176ef3u},0x45d74566u,0x45d74566u},
    {{0xc4118ff0u,0x44432bd6u,0xc39ac802u,0x44c4d652u,0xc4cb368au,0xc53b6975u,0xc44d35b7u,0xc4637806u,0xc460abcau,0x44748f2eu,0xc4230a71u,0x448db214u,0xc3f7fe7eu,0xc5289833u,0x452acad1u,0x42530a4au},0x4609c5ccu,0x4609c5ccu},
    {{0x4491ba1du,0xc4952879u,0x452d0f2fu,0xc50d334eu,0xc51bb2a3u,0xc4fcaa20u,0x449330cdu,0xc511e2b7u,0x45076034u,0xc3a6cb98u,0xc4fc0404u,0x44fe93feu,0xc4a42e62u,0xc50c99abu,0xc53b62f9u,0xc514ff54u},0x461f9127u,0x461f9126u},
    {{0x450df357u,0xc4f5aae3u,0xc52f410du,0xc457e454u,0xc380155du,0xc4e95c64u,0xc50d5606u,0x4538d95eu,0x44f86424u,0x44168729u,0xc53a8ba7u,0x453730ceu,0xc4f11011u,0xc52e061fu,0xc4a83f0bu,0xc3a51e35u},0x45f93b66u,0x45f93b67u},
    {{0x43bbf96eu,0x44919b55u,0xc3bb887au,0x445e3aa8u,0xc330f90fu,0x42d6412du,0x4528794bu,0x4532f7c5u,0x442fe982u,0x451dc4fdu,0x44bc69b8u,0x43af99deu,0xc4b0e98fu,0x44e88f07u,0xc481170au,0xc4363875u},0x45b433f9u,0x45b433fau},
};
/* One stub run: segment record seg (points set), V = 2 * native (the stub's mulss halves it), every other register
 * a pattern; checks the store, V (lane 0 = want_v, lanes 1-3 kept) and that nothing else changed. */
static void tfl_run(const unsigned char *page,unsigned i,unsigned char *seg,uint32_t native,uint32_t want_store,uint32_t want_v) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x70;k++) block[k]=(unsigned char)(0x40+k);
    const unsigned v=fr_len_sites[i].reg;const float twice=fr_f32(native)*2.0f;unsigned char *vslot=block+(v==4?0x40:0x60);
    memcpy(vslot,&twice,4);const float half=0.5f;memset(block+0x70,0,16);memcpy(block+0x70,&half,4);memcpy(in,block,sizeof(in));
    memset(seg+0x18,0xee,4);
    test_fr_len_run(page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i,seg-0x30,0x30,block);
    uint32_t stored,got;memcpy(&stored,seg+0x18,4);memcpy(&got,vslot,4);
    assert(stored==want_store && got==want_v && !memcmp(vslot+4,(v==4?in+0x44:in+0x64),12));
    for(unsigned k=0;k<0x70;k+=16) if(block+k!=vslot) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x30 && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==(uint64_t)(uintptr_t)(seg-0x30) && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i));
}
static void test_fr_len_stubs(void) {
    for(unsigned k=0;k<sizeof(tfl_vectors)/sizeof(*tfl_vectors);k++) {
        assert(sl_length((const unsigned char*)tfl_vectors[k].p)==tfl_vectors[k].canonical);
        assert(sl_length_pairwise((const unsigned char*)tfl_vectors[k].p)==tfl_vectors[k].pairwise);
    }
    assert(sl_stored_linux(0)==SL_ONE && sl_stored_linux(0x80000000u)==SL_ONE && sl_stored_linux(0x7fc00000u)==0x7fc00000u);
    assert(sl_stored_windows(0)==SL_ONE && sl_stored_windows(0x7fc00000u)==SL_ONE && sl_stored_windows(0x3f000000u)==0x3f000000u);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_len_back;memcpy(back+6,&to,8);
        unsigned char *at=image+fr_len_sites[i].back;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    static unsigned char seg[0xa0];
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_LEN],d0=fr_diff[FR_LEN],h0=fr_hits[FR_HIT_LEN0],h1=fr_hits[FR_HIT_LEN1],w0=fr_len_written;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        memcpy(seg+0x60,tfl_vectors[0].p,64);
        const uint32_t ours=tfl_vectors[0].canonical,native=tfl_vectors[0].pairwise;
        fr_mode=FR_OFF;tfl_run(page,i,seg,native,native,native);                       /* off: the block's own */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,native,native,native);                    /* shadow: compared, native kept */
        fr_mode=FR_WRITER;tfl_run(page,i,seg,native,native,native);                    /* writer only: the length shadowed */
        fr_mode=FR_REDIRECT;tfl_run(page,i,seg,native,ours,ours);                      /* redirect: ours, Linux's rule */
        memcpy(seg+0x60,tfl_vectors[1].p,64);tfl_run(page,i,seg,tfl_vectors[1].pairwise,tfl_vectors[1].canonical,tfl_vectors[1].canonical);
        memset(seg+0x60,0,64);                                                         /* zero length: 1.0 stored, V = 0 */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,0,SL_ONE,0);fr_mode=FR_REDIRECT;tfl_run(page,i,seg,0,SL_ONE,0);
        const uint32_t nan=0x7fc00000u;memcpy(seg+0x60,&nan,4);                        /* NaN: Windows stores 1.0, Linux keeps it */
        fr_mode=FR_SHADOW;tfl_run(page,i,seg,nan,SL_ONE,nan);
        fr_mode=FR_REDIRECT;tfl_run(page,i,seg,nan,sl_length(seg+0x60),sl_length(seg+0x60));
        assert(sl_is_nan(sl_length(seg+0x60)));
    }
    /* per site: 9 runs, compared 8 (all but off), differing 3 (vector 0 in shadow, writer and redirect; the NaN
     * propagates unchanged), written 2 in redirect (vector 0, and the NaN: Linux keeps it where Windows stores 1.0) */
    assert(fr_n[FR_LEN]==n0+16 && fr_diff[FR_LEN]==d0+6 && fr_hits[FR_HIT_LEN0]==h0+9 && fr_hits[FR_HIT_LEN1]==h1+9 && fr_len_written==w0+4);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," len=1:") && strstr(out," len_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    for(unsigned i=0;i<FR_LEN_SITES;i++) memset(image+fr_len_sites[i].back,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    assert(fr_lua_mode("writer",6)==FR_WRITER && fr_lua_mode("shadow",6)==FR_SHADOW && fr_lua_mode("redirect",8)==FR_REDIRECT && fr_lua_mode("writex",6)<0);
    printf("fp redirect length: %u strict vectors, both stubs off/shadow/writer/redirect (zero, NaN), registers kept PASS\n",
        (unsigned)(sizeof(tfl_vectors)/sizeof(*tfl_vectors)));
}
/* FR13: a handle stub runs for real. xmm0-5 from the block (+0x00..+0x5f, xmm2 = d in lane 0), xmm8 from +0x60 (k),
 * sentinels in rax, rcx, rdx, r8-r11, rsp 8 off 16; the back jump (planted at image+back) leads to test_fr_h_back,
 * which stores xmm0-5, xmm8 (+0x60) and rax, rcx, rdx, r8-r11, rbx (+0x80..). */
__asm__(
".text\n"
"test_fr_h_run:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n mov %rsp,%rbp\n sub $0x40,%rsp\n movdqu %xmm8,(%rsp)\n"
" mov %rdx,%rsi\n mov %rcx,%rbx\n"
" movups (%rsi),%xmm0\n movups 0x10(%rsi),%xmm1\n movups 0x20(%rsi),%xmm2\n movups 0x30(%rsi),%xmm3\n movups 0x40(%rsi),%xmm4\n"
" movups 0x50(%rsi),%xmm5\n movups 0x60(%rsi),%xmm8\n"
" movabs $0x0123456789abcdef,%rax\n movabs $0x1111111111111111,%rcx\n movabs $0x2222222222222222,%rdx\n movabs $0x8888888888888888,%r8\n"
" movabs $0x9999999999999999,%r9\n movabs $0xaaaaaaaaaaaaaaaa,%r10\n movabs $0xbbbbbbbbbbbbbbbb,%r11\n and $-16,%rsp\n sub $8,%rsp\n jmp *%rbx\n"
"test_fr_h_back:\n movups %xmm0,(%rsi)\n movups %xmm1,0x10(%rsi)\n movups %xmm2,0x20(%rsi)\n movups %xmm3,0x30(%rsi)\n movups %xmm4,0x40(%rsi)\n"
" movups %xmm5,0x50(%rsi)\n movups %xmm8,0x60(%rsi)\n mov %rax,0x80(%rsi)\n mov %rcx,0x88(%rsi)\n mov %rdx,0x90(%rsi)\n mov %r8,0x98(%rsi)\n"
" mov %r9,0xa0(%rsi)\n mov %r10,0xa8(%rsi)\n mov %r11,0xb0(%rsi)\n mov %rbx,0xb8(%rsi)\n"
" lea -0x40(%rbp),%rsp\n movdqu (%rsp),%xmm8\n mov %rbp,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_h_run(const void *stub,unsigned char *block);
extern char test_fr_h_back[];
#define TFH_K 0x3eaaaaabu /* 0.333333343f, Windows' xmm8 */
/* One stub run: xmm2 lane 0 = d, lanes 1-3 and the other registers a pattern; checks xmm2 = want (lanes 1-3 kept) and
 * that nothing else changed. */
static void tfh_run(const unsigned char *page,unsigned i,uint32_t d,uint32_t want) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    memcpy(block+0x20,&d,4);const uint32_t k8[4]={TFH_K,0x11223344u,0x55667788u,0x99aabbccu};memcpy(block+0x60,k8,16);
    memcpy(in,block,sizeof(in));
    test_fr_h_run(page+FR_H_STUB+FR_LEN_STUB_SIZE*i,block);
    uint32_t got;memcpy(&got,block+0x20,4);
    if(got!=want) {printf("fp redirect handle stub %u: d %08x got %08x want %08x\n",i,d,got,want);assert(0);}
    assert(!memcmp(block+0x24,in+0x24,12) && !memcmp(block+0x60,in+0x60,16));
    for(unsigned k=0;k<0x60;k+=16) if(k!=0x20) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_H_STUB+FR_LEN_STUB_SIZE*i));
}
static void test_fr_h_stubs(void) {
    /* Strict vectors (numpy float32): 100/3 = 42055555, 100 * 0.333333343 = 42055556; 3 -> 1.0 both ways. */
    assert(sl_handle(0x42c80000u)==0x42055555u && sl_handle_windows(0x42c80000u,TFH_K)==0x42055556u);
    assert(sl_handle(0x40400000u)==0x3f800000u && sl_handle_windows(0x40400000u,TFH_K)==0x3f800000u);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    for(unsigned i=0;i<FR_H_SITES;i++) {
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_back;memcpy(back+6,&to,8);
        unsigned char *at=image+fr_h_sites[i].back;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_HANDLE],d0=fr_diff[FR_HANDLE],w0=fr_handle_written;LONG64 h0[FR_H_SITES];
    for(unsigned i=0;i<FR_H_SITES;i++) h0[i]=fr_hits[FR_HIT_HANDLE0+i];
    for(unsigned i=0;i<FR_H_SITES;i++) {
        fr_mode=FR_OFF;tfh_run(page,i,0x42c80000u,0x42055556u);       /* off: native d*k */
        fr_mode=FR_SHADOW;tfh_run(page,i,0x42c80000u,0x42055556u);    /* shadow: compared, native kept */
        fr_mode=FR_WRITER;tfh_run(page,i,0x42c80000u,0x42055556u);    /* writer only: the handle shadowed */
        fr_mode=FR_REDIRECT;tfh_run(page,i,0x42c80000u,0x42055555u);  /* redirect: ours, Linux's division */
        tfh_run(page,i,0x40400000u,0x3f800000u);                       /* equal both ways: nothing written */
    }
    /* per site: 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_HANDLE]==n0+16 && fr_diff[FR_HANDLE]==d0+12 && fr_handle_written==w0+4);
    for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_hits[FR_HIT_HANDLE0+i]==h0[i]+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," handle=1:") && strstr(out," handle_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    for(unsigned i=0;i<FR_H_SITES;i++) memset(image+fr_h_sites[i].back,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect handle: strict vectors, four stubs off/shadow/writer/redirect, registers kept PASS\n");
}
/* FR16: the PointGuard angle stub runs for real in test_fr_h_run's frame: xmm1 lane 0 = rem (+0x10), xmm2 lane 0 = m+1
 * (+0x20); checks xmm1 = want (lanes 1-3 kept) and that nothing else changed. */
static void tpg_run(const unsigned char *page,uint32_t rem,uint32_t m1,uint32_t want) {
    unsigned char block[0xc0],in[0xc0];
    for(unsigned k=0;k<0x60;k++) block[k]=(unsigned char)(0x40+k);
    memcpy(block+0x10,&rem,4);memcpy(block+0x20,&m1,4);
    const uint32_t k8[4]={0x3eaaaaabu,0x11223344u,0x55667788u,0x99aabbccu};memcpy(block+0x60,k8,16);
    memcpy(in,block,sizeof(in));
    test_fr_h_run(page+FR_PG_STUB,block);
    uint32_t got;memcpy(&got,block+0x10,4);
    if(got!=want) {printf("fp redirect pg stub: rem %08x m1 %08x got %08x want %08x\n",rem,m1,got,want);assert(0);}
    assert(!memcmp(block+0x14,in+0x14,12) && !memcmp(block+0x60,in+0x60,16));
    for(unsigned k=0;k<0x60;k+=16) if(k!=0x10) assert(!memcmp(block+k,in+k,16));
    uint64_t r[8];memcpy(r,block+0x80,64);
    assert(r[0]==0x0123456789abcdefull && r[1]==0x1111111111111111ull && r[2]==0x2222222222222222ull && r[3]==0x8888888888888888ull);
    assert(r[4]==0x9999999999999999ull && r[5]==0xaaaaaaaaaaaaaaaaull && r[6]==0xbbbbbbbbbbbbbbbbull && r[7]==(uint64_t)(uintptr_t)(page+FR_PG_STUB));
}
static void test_fr_pg_stubs(void) {
    /* Strict vectors (numpy float32): rem 1, m+1 7: Windows 1 * (pi/7) = 3ee5c8fa, Linux (1/14) * 2pi = 3ee5c8fb; rem 3, m+1 5:
     * 3ff1463a / 3ff1463b; rem 1, m+1 4: 3f490fdb both; rem 2, m+1 3: 40060a92 both; rem 0: 0 both. */
    assert(fr_pg_angle_windows(0x3f800000u,0x40e00000u)==0x3ee5c8fau && fr_pg_angle(0x3f800000u,0x40e00000u)==0x3ee5c8fbu);
    assert(fr_pg_angle_windows(0x40400000u,0x40a00000u)==0x3ff1463au && fr_pg_angle(0x40400000u,0x40a00000u)==0x3ff1463bu);
    assert(fr_pg_angle_windows(0x3f800000u,0x40800000u)==0x3f490fdbu && fr_pg_angle(0x3f800000u,0x40800000u)==0x3f490fdbu);
    assert(fr_pg_angle_windows(0x40000000u,0x40400000u)==0x40060a92u && fr_pg_angle(0x40000000u,0x40400000u)==0x40060a92u);
    assert(fr_pg_angle_windows(0,0x40400000u)==0 && fr_pg_angle(0,0x40400000u)==0);
    unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
    assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
    assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    {   unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_back;memcpy(back+6,&to,8);
        unsigned char *at=image+FR_PG_BACK;DWORD old;assert(VirtualProtect(at,14,PAGE_EXECUTE_READWRITE,&old));
        memcpy(at,back,14);FlushInstructionCache(GetCurrentProcess(),at,14);
    }
    const int saved_mode=fr_mode;const uint32_t saved_active=stats.active;stats.active=1;
    const LONG64 n0=fr_n[FR_PG],d0=fr_diff[FR_PG],w0=fr_pg_written,h0=fr_hits[FR_HIT_PG];
    fr_mode=FR_OFF;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fau);      /* off: native rem * (pi/(m+1)) */
    fr_mode=FR_SHADOW;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fau);   /* shadow: compared, native kept */
    fr_mode=FR_WRITER;tpg_run(page,0x40400000u,0x40a00000u,0x3ff1463au);   /* writer only: the angle shadowed */
    fr_mode=FR_REDIRECT;tpg_run(page,0x3f800000u,0x40e00000u,0x3ee5c8fbu); /* redirect: ours, Linux's order */
    tpg_run(page,0x3f800000u,0x40800000u,0x3f490fdbu);                      /* equal both ways: nothing written */
    /* 5 runs, compared 4, differing 3, written 1 */
    assert(fr_n[FR_PG]==n0+4 && fr_diff[FR_PG]==d0+3 && fr_pg_written==w0+1 && fr_hits[FR_HIT_PG]==h0+5);
    char out[2048];assert(fr_status(out,sizeof(out))>0 && strstr(out," pg=1:") && strstr(out," pg_hits=") && strstr(out," pg_written="));
    fr_mode=saved_mode;stats.active=saved_active;
    memset(image+FR_PG_BACK,0,14);
    VirtualFree(page,0,MEM_RELEASE);
    printf("fp redirect pointguard angle: strict vectors, stub off/shadow/writer/redirect, registers kept PASS\n");
}
/* The v stub as 0x6dcce6 calls it: rcx, rdx, xmm2, r9, three on the stack, and d in xmm7 (callee-saved). */
__asm__(
".text\n"
"test_fr_v_run:\n push %rbx\n sub $0x50,%rsp\n movdqu %xmm7,0x40(%rsp)\n mov %rcx,%r10\n mov %rdx,%rcx\n mov %r8,%rdx\n movaps %xmm3,%xmm2\n"
" mov 0x80(%rsp),%r9\n mov 0x88(%rsp),%rax\n mov %rax,0x20(%rsp)\n mov 0x90(%rsp),%rax\n mov %rax,0x28(%rsp)\n mov 0x98(%rsp),%rax\n mov %rax,0x30(%rsp)\n"
" movss 0xa0(%rsp),%xmm7\n call *%r10\n movdqu 0x40(%rsp),%xmm7\n add $0x50,%rsp\n pop %rbx\n ret\n"
);
extern uint64_t test_fr_v_run(const void *stub,void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,float d);
static unsigned char *tfr_vstub;
static int tfr_win_form;
static uint64_t tfr_v(void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    assert(o20==tfr_ship && time==2.5 && a4==4 && a5==5 && a6==6 && a7==7);
    const float v[8]={-1.0f,2.0f,-100.0f,0,0,0,0,0};memcpy(buf,v,32);return 0x1234;
}
static uint64_t tfr_mulps(void *a,void *b,void *c,void *e) {(void)b;(void)c;(void)e;assert(a==tfr_ship);return 0;}
/* The first call's v path: the branch, then v through the stub, then the max block in Linux's or Windows' form. */
static void tfr_params_v(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)flag;(void)time;(void)a5;(void)a6;(void)fr_mulps_hook(o20,0,0,0);
    unsigned char v[32];const float d=3.0f;static const unsigned lanes[4]={2,2,0,1};
    assert(test_fr_v_run(tfr_vstub,o20,v,2.5,4,5,6,7,d)==0x1234);
    for(unsigned i=0;i<4;i++) {
        float a,c;memcpy(&a,v+4*lanes[i],4);a=fr_abs(a);memcpy(&c,slot+4*i,4);
        const float n=tfr_win_form?a*(1.0f/d):a/d,o=n>c?n:c;memcpy(slot+4*i,&o,4);
    }
}
/* A fake ship for the writer: its vtable's five getters (+0x1fe8..+0x2008) return tfw_r, +0x398 points at tfw_p when
 * p is present; D, A and the table ([0x146cf1538] in this image) in Windows' layout. */
static float tfw_r[5];
static float tfw_r0(void *o) {(void)o;return tfw_r[0];}
static float tfw_r1(void *o) {(void)o;return tfw_r[1];}
static float tfw_r2(void *o) {(void)o;return tfw_r[2];}
static float tfw_r3(void *o) {(void)o;return tfw_r[3];}
static float tfw_r4(void *o) {(void)o;return tfw_r[4];}
static void *tfw_vt[0x410];
static unsigned char tfw_ship[0x400],tfw_data[0xa00],tfw_a[0x40],tfw_p[0x60],tfw_table[0x5100];
static void tfw_load(const fr_w_in *in) {
    static float (*const fns[5])(void *)={tfw_r0,tfw_r1,tfw_r2,tfw_r3,tfw_r4};
    void *vt=tfw_vt,*t=tfw_table,*p=in->has_p?(void*)tfw_p:NULL;memcpy(tfw_ship,&vt,8);memcpy(tfw_ship+0x398,&p,8);memcpy(tfw_p+0x50,&in->p,4);
    for(unsigned i=0;i<5;i++) {tfw_vt[(0x1fe8u>>3)+i]=(void*)fns[i];memcpy(&tfw_r[i],&in->r[i],4);}
    memcpy(tfw_a,in->a,16);memcpy(tfw_data+0x904,in->d,20);memcpy(tfw_data+0x988,in->dm,16);
    memcpy(tfw_table+0x5050,&in->t_scale,4);memcpy(tfw_table+0x300,&in->k,4);memcpy(tfw_table+0x318,&in->t_clamp,4);
    memcpy(image+0x6cf1538u,&t,8);
}
/* A stand-in writer: writer vector 0's +0x0..+0xc in Linux's order, or Windows' (tfr_win_form). */
static uint64_t tfr_writer(void *o20,void *data,void *a,void *slot) {
    assert(o20==tfw_ship && data==tfw_data && a==tfw_a);
    uint32_t q[4];if(tfr_win_form) tfr_w_windows(&tfr_w_vectors[0].in,q);else fr_w_recompute(&tfr_w_vectors[0].in,q);
    memcpy(slot,q,16);return 0;
}
static unsigned char *tfr_wstub;
static void tfr_params_w(void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6) {
    (void)o20;(void)flag;(void)time;(void)a5;(void)a6;(void)((fr_writer_fn)(void*)tfr_wstub)(tfw_ship,tfw_data,tfw_a,slot);
}
extern uint32_t test_fr_tail_run(const void *stub,void *slot);
extern char test_fr_tail_back[];
static void test_fp_redirect(void) {
    test_fr_vectors();
    const uint64_t saved_issued=stats.issued;const DWORD saved_ui=ui_thread;const uint32_t saved_active=stats.active;
    const int saved_boot=boot_mode,saved_phase=boot_phase,saved_ids=world_identity_installed;
    world_identity_entry *prior=world_identities;const unsigned prior_count=world_identity_count;const LONG prior_sealed=world_identity_sealed;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(0x5151,0x7dcd0));
    const uint64_t runtime=0x5151;memcpy(tfr_ship+8,&runtime,8);
    const uint32_t ca=tfr_vectors[0].ca,cb=tfr_vectors[0].cb;memcpy(tfr_comp+FR_COMP_A,&ca,4);memcpy(tfr_comp+FR_COMP_B,&cb,4);
    tfr_w=fr_f32(tfr_vectors[0].w);
    fr_native_refresh=tfr_refresh;fr_native_g4=tfr_g4;fr_native_g0=tfr_g0;fr_native_m=tfr_m;fr_native_params=tfr_params;
    /* Arming refusals: mode, ticks, the watched IDs' identities, then the zero image's pins. */
    stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    assert(!fr_arm(7,3) && !strcmp(fr_stage,"mode") && !fr_arm(FR_SHADOW,0) && !strcmp(fr_stage,"ticks"));
    assert(fr_watch(0x7dcd0) && fr_watch(0x7dcd0) && fr_id_count==1 && !fr_watch(0));
    world_identity_installed=0;assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"needs"));world_identity_installed=1;
    assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"pins") && !fr_installed);
    boot_phase=1;assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"refused"));boot_phase=0;
    /* The flow, as armed: one call in the load (tick 0), then ticks. */
    assert(fr_arm_ready(FR_SHADOW,3));fr_mode=FR_SHADOW;
    unsigned char slot[0x1a0];memset(slot,0,sizeof(slot));const float s2c=fr_f32(0x43820000u);memcpy(slot+0x2c,&s2c,4);
    void *owner=(void*)0x1234;
    boot_phase=2;stats.issued=0;tfr_refresh(owner,slot,0);          /* the load: compared, kept unkeyed */
    char out[2048];assert(!fr_row(0,out,sizeof(out)));              /* rows only after admission */
    boot_phase=3;stats.issued=1;
    tfr_refresh(owner,slot,0);                                      /* equal */
    tfr_refresh(owner,slot,1);                                      /* the flag path: skipped */
    tfr_perturb=1;tfr_refresh(owner,slot,0);tfr_perturb=0;          /* native +0x0 one ulp off */
    tfr_branch=1;tfr_refresh(owner,slot,0);tfr_branch=0;            /* the M branch */
    stats.issued=4;tfr_refresh(owner,slot,0);                       /* compared, no row (tick 4 > 3) */
    unsigned char frame[0x100];memset(frame,0,sizeof(frame));(void)fr_g4_hook(tfr_comp,tfr_ship,frame+0x80); /* outside a refresh */
    fr_mode=FR_OFF;tfr_refresh(owner,slot,0);fr_mode=FR_SHADOW;     /* off: native only */
    assert(fr_status(out,sizeof(out))>0);
    assert(strstr(out,"calls=7 compared=5 load_compared=1 skipped=1 unfinished=0 no_context=0 orphans=1 m_branch=1 v8_branch=0 comp_changed=0"));
    assert(strstr(out," f0=1:1:5 f4=0:0:5 f24=0:0:5 m=0:0:2 f0_out=1:1:5 f4_out=0:0:5 p0=0:0:0 p4=0:0:0 p8=0:0:0 pc=0:0:0 w0=0:0:0 w4=0:0:0 w8=0:0:0 wc=0:0:0 hits=g4:7,g0:6,m4:1,m0:1 params=7 params_flag=1 v=0 mulps=0 writer=0 w_unread=0 w_written=0 ids=1 ticks=3 records=9 dropped=0 scanned=0 keyed=0 rows=0 failures=0"));
    assert(!fr_row(1,out,sizeof(out)));                             /* in order from 0 */
    assert(fr_row(0,out,sizeof(out))>0 && strstr(out,"index=0 record=0 kind=params tick=0 key=000000000007dcd0 ordinal=0 thread=ui flag=0 v_path=0 mulps=0 d=00000000") && strstr(out,"post100=00000000000000000000000000002041"));
    assert(fr_row(1,out,sizeof(out))>0 && strstr(out,"index=1 record=1 kind=compare tick=0 key=000000000007dcd0 ordinal=1 thread=ui seen=3 ca=3b23d70a cb=3b83126f g4=43c9859b g0=4383fc81 w=43960000"));
    assert(strstr(out,"native=468a2c56,43c9859b,43c80000 ours=468a2c56,43c9859b,43c80000 v8=43c80000 v12=43c80000 m4=00000000"));
    assert(fr_row(2,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=0 thread=ui flag=0"));
    assert(fr_row(3,out,sizeof(out))>0 && strstr(out,"index=3 record=3 kind=compare tick=1 key=000000000007dcd0 ordinal=1"));
    assert(fr_row(4,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=2 thread=ui flag=1"));
    assert(fr_row(5,out,sizeof(out))>0 && strstr(out,"kind=params"));
    assert(fr_row(6,out,sizeof(out))>0 && strstr(out,"kind=compare tick=1 key=000000000007dcd0 ordinal=4") && strstr(out,"native=468a2c57,"));
    assert(fr_row(7,out,sizeof(out))>0 && strstr(out,"kind=params"));
    assert(fr_row(8,out,sizeof(out))>0 && strstr(out,"ordinal=6 thread=ui seen=f") && strstr(out,"m4=43820000,43c80000,43960000,1,3f124925,3f124925,43c9859b"));
    assert(!fr_row(9,out,sizeof(out)));
    assert(fr_status(out,sizeof(out))>0 && strstr(out,"records=9 dropped=0 scanned=9 keyed=9 rows=9 failures=0"));
    /* The first call's v path: the v stub (d from xmm7) runs for real; +0x0..+0xc as Linux writes them, then as Windows. */
    {
        unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
        assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
        assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        /* No K table ([image+0x6cf1538], also the economy settings pointer an earlier synthetic test may have left set,
         * whose +0x300 then depends on the memory layout): k=0 below. */
        memset(image+0x6cf1538u,0,8);
        tfr_vstub=page+FR_V_STUB;fr_native_params=tfr_params_v;fr_native_v=tfr_v;fr_native_writer=tfr_mulps;stats.issued=2;
        static unsigned char slot2[0x1a0];
        memset(slot2,0,sizeof(slot2));tfr_win_form=0;fr_params_hook(tfr_ship,slot2,0,0.0,0,0);
        memset(slot2,0,sizeof(slot2));tfr_win_form=1;fr_params_hook(tfr_ship,slot2,0,0.0,0,0);
        assert(fr_status(out,sizeof(out))>0 && strstr(out," p0=1:1:2 p4=1:1:2 p8=0:0:2 pc=0:0:2 ") && strstr(out," v=2 mulps=2 "));
        assert(fr_row(9,out,sizeof(out))>0 && strstr(out,"kind=params tick=2 key=000000000007dcd0 ordinal=0 thread=ui flag=0 v_path=1 mulps=1 d=40400000 k=00000000 "
            "v=bf800000,40000000,c2c80000,00000000,00000000,00000000,00000000,00000000 cur=00000000,00000000,00000000,00000000 "
            "ours=42055555,42055555,3eaaaaab,3f2aaaab pre0=") && strstr(out,"post0=5555054255550542abaaaa3eabaa2a3f"));
        assert(fr_row(10,out,sizeof(out))>0 && strstr(out,"ours=42055555,42055555,3eaaaaab,3f2aaaab pre0=") && strstr(out,"post0=5655054256550542abaaaa3eabaa2a3f"));
        fr_native_params=tfr_params;stats.issued=4;VirtualFree(page,0,MEM_RELEASE);
    }
    /* The writer at 0x6dcaaf inside a first call, through its page stub (a jmp to fr_writer_run): writer vector 0's
     * inputs read back through the fake ship's vtable and the table pointer; a stand-in writer in Linux's order,
     * then in Windows' (all four one ulp apart). */
    {
        unsigned char *page=fr_alloc_page(),*saved=fr_page;DWORD o2,i2;assert(page);
        assert(VirtualProtect(page,0x1000,PAGE_READWRITE,&o2));fr_page=page;fr_write_code(page);fr_page=saved;
        assert(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&i2));FlushInstructionCache(GetCurrentProcess(),page,0x1000);
        tfw_load(&tfr_w_vectors[0].in);tfr_wstub=page+FR_WRITER_STUB;fr_native_params=tfr_params_w;fr_native_writer=tfr_writer;stats.issued=2;
        static unsigned char slot3[0x1a0];
        memset(slot3,0,sizeof(slot3));tfr_win_form=0;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(fr_status(out,sizeof(out))>0 && strstr(out," w0=1:1:2 w4=1:1:2 w8=1:1:2 wc=1:1:2 ") && strstr(out," mulps=2 writer=4 w_unread=0 "));
        assert(fr_row(11,out,sizeof(out))>0 && strstr(out,"kind=params tick=2 key=000000000007dcd0 ordinal=2") && strstr(out," writer=1:1 wa=42c80000,42480000,41a00000,41200000 "
            "wd=3f800000,3f800000,3f800000,3f800000,3f800000 wm=3f800000,3f800000,3f800000,3f800000 wt=3f800000,00000000,3f800000 wp=0:3f800000 "
            "wr=3f800000,3f800000,3f800000,3f800000,00000000 wnative=42055555,41855555,40d55555,40555555 wours=42055555,41855555,40d55555,40555555"));
        assert(fr_row(12,out,sizeof(out))>0 && strstr(out," wnative=42055556,41855556,40d55556,40555556 wours=42055555,41855555,40d55555,40555555"));
        /* 'redirect': Windows' bits are replaced by ours before the first call goes on (post0 = Linux's); Linux's
         * order writes nothing; inputs that cannot be read are a failure (the table pointer gone). */
        fr_mode=FR_REDIRECT;
        memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(!memcmp(slot3,tfr_w_vectors[0].linux_form,16));
        memset(slot3,0,sizeof(slot3));tfr_win_form=0;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(!memcmp(slot3,tfr_w_vectors[0].linux_form,16) && fr_status(out,sizeof(out))>0 && strstr(out,"mode=redirect ") && strstr(out," w_unread=0 w_written=1 "));
        assert(fr_row(13,out,sizeof(out))>0 && strstr(out,"post0=55550542555585415555d54055555540") && strstr(out," wnative=42055556,41855556,40d55556,40555556 "));
        memset(image+0x6cf1538u,0,8);memset(slot3,0,sizeof(slot3));tfr_win_form=1;fr_params_hook(tfr_ship,slot3,0,0.0,0,0);
        assert(fr_w_unread==1 && fr_failures==1 && !fr_check() && fr_w_written==1 && !memcmp(slot3,tfr_w_vectors[0].windows_form,16));
        fr_failures=0;fr_mode=FR_SHADOW;fr_stage="idle";
        memset(image+0x6cf1538u,0,8);fr_native_params=tfr_params;stats.issued=4;VirtualFree(page,0,MEM_RELEASE);
    }
    test_fr_len_stubs();
    test_fr_h_stubs();
    test_fr_pg_stubs();
    /* A G4 without its tail: the next one restarts. */
    (void)fr_g4_slot_hook(tfr_comp,tfr_ship,frame+0x80,slot);(void)fr_g4_slot_hook(tfr_comp,tfr_ship,frame+0x80,slot);assert(fr_unfinished==1);
    fr_tail_hook(slot);
    /* Site CAS, ownership and restore on the plain image; the two code stubs run. */
    unsigned char *saved_page=fr_page;fr_page=fr_alloc_page();assert(fr_page);DWORD old,ignored;
    for(unsigned i=0;i<FR_SITES;i++) {image[fr_sites[i].site]=0xe8;int32_t d=fr_rel_native(i);memcpy(image+fr_sites[i].site+1,&d,4);assert(fr_site_state(i)==0);}
    memcpy(image+FR_TAIL,fr_tail_original,5);image[FR_TAIL+5]=0;assert(fr_tail_state()==0);
    {unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_tail_back;memcpy(back+6,&to,8);memcpy(image+FR_TAIL_BACK,back,14);}
    assert(VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old));fr_write_code(fr_page);
    assert(VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&ignored));
    assert(VirtualProtect(image+(FR_TAIL_BACK&~0xfffu),0x2000,PAGE_EXECUTE_READWRITE,&old));
    FlushInstructionCache(GetCurrentProcess(),image+(FR_TAIL_BACK&~0xfffu),0x2000);
    {   /* G4's stub: the slot arrives from r15 and a refresh begins. */
        const LONG64 unfinished=fr_unfinished;
        assert(test_fr_g4_run(fr_page,tfr_comp,tfr_ship,frame+0x80,slot)==fr_f32(0x43c9859bu));
        fr_ctx *c=fr_context(0);assert(c && c->depth==1 && c->slot==slot && c->seen==FR_SEEN_G4 && fr_unfinished==unfinished);
        (void)fr_g0_hook(tfr_comp,tfr_ship,frame+0x80);
        /* The tail's stub: fr_tail_hook(r15) ends it (compared), r13d = 0x300, back at 0xea29c2. */
        const LONG64 compared=fr_compared,calls=fr_calls;
        assert(test_fr_tail_run(fr_page+FR_TAIL_STUB,slot)==0x300u && fr_compared==compared+1 && fr_calls==calls+1 && !c->depth);
    }
    fr_done=0;
    for(unsigned i=0;i<FR_SITES;i++) {assert(fr_site_cas(i,fr_rel_native(i),fr_rel_ours(i)) && fr_site_state(i)==1);fr_done|=1u<<i;}
    {unsigned char jump[5];fr_tail_jump(jump);assert(view_cas_bytes(FR_TAIL,fr_tail_original,jump,5) && fr_tail_state()==1);fr_done|=FR_TAIL_DONE;}
    /* The length sites: CAS in, the view shows the original mulss to pins (read_local, hash_ok), state per site. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        memcpy(image+fr_len_sites[i].site,fr_len_original[i],5);assert(fr_len_state(i)==0 && fr_len_reach(i));
        assert(!fp_patch_overlaps(image+fr_len_sites[i].site,5) && fr_len_tail_clear(i));
        unsigned char jump[5];fr_len_jump(i,jump);assert(view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5) && fr_len_state(i)==1);
        fr_done|=FR_LEN_DONE<<i;
        unsigned char seen[16];assert(fp_patch_overlaps(image+fr_len_sites[i].site-3,16) && read_local(image+fr_len_sites[i].site-3,seen,16));
        assert(!memcmp(seen+3,fr_len_original[i],5) && image[fr_len_sites[i].site]==0xe9);
    }
    /* FR16: the PointGuard angle site likewise; clear covers site..back, which the jmp makes differ from the view. */
    memcpy(image+FR_PG_SITE,fr_pg_original,5);assert(fr_pg_state()==0 && fr_pg_reach() && fr_pg_clear());
    {   unsigned char jump[5];fr_pg_jump(jump);assert(view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5) && fr_pg_state()==1);
        fr_done|=FR_PG_DONE;
        unsigned char seen[16];assert(fp_patch_overlaps(image+FR_PG_SITE-3,16) && read_local(image+FR_PG_SITE-3,seen,16));
        assert(!memcmp(seen+3,fr_pg_original,5) && image[FR_PG_SITE]==0xe9 && !fr_pg_clear());
    }
    fr_installed=1;fr_failures=0;assert(fr_owned() && fr_check());
    image[FR_PG_SITE+1]^=1;assert(fr_pg_state()<0 && !fr_owned());image[FR_PG_SITE+1]^=1;assert(fr_owned());
    image[fr_len_sites[1].site+2]^=1;assert(fr_len_state(1)<0 && !fr_owned());image[fr_len_sites[1].site+2]^=1;assert(fr_owned());
    image[fr_sites[1].site+2]^=1;assert(fr_site_state(1)<0 && !fr_owned());image[fr_sites[1].site+2]^=1;assert(fr_owned());
    image[FR_TAIL+3]^=1;assert(fr_tail_state()<0 && !fr_owned());image[FR_TAIL+3]^=1;assert(fr_owned());
    image[fr_sites[2].site+3]^=1;assert(!fr_check() && !strcmp(fr_stage,"site_changed") && fr_failures==1);image[fr_sites[2].site+3]^=1;
    assert(!fr_check()); /* a failure stays */
    fr_restore();assert(!fr_installed && fr_restored && !fr_done && fr_mode==FR_OFF && fr_tail_state()==0);
    for(unsigned i=0;i<FR_SITES;i++) {assert(fr_site_state(i)==0);memset(image+fr_sites[i].site,0,5);}
    for(unsigned i=0;i<FR_LEN_SITES;i++) {assert(fr_len_state(i)==0 && !fp_patch_overlaps(image+fr_len_sites[i].site,5));memset(image+fr_len_sites[i].site,0,5);}
    assert(fr_pg_state()==0 && !fp_patch_overlaps(image+FR_PG_SITE,5));memset(image+FR_PG_SITE,0,5);
    memset(image+FR_TAIL,0,20);
    VirtualFree(fr_page,0,MEM_RELEASE);fr_page=saved_page;
    fr_native_refresh=NULL;fr_native_g4=fr_native_g0=NULL;fr_native_m=NULL;fr_native_params=NULL;fr_native_writer=NULL;fr_id_count=0;fr_restored=0;fr_failures=0;fr_stage="idle";
    fr_records=0;fr_scan=fr_emitted=fr_scanned=fr_keyed=0;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=prior;world_identity_count=prior_count;world_identity_sealed=prior_sealed;
    world_identity_installed=saved_ids;
    stats.issued=saved_issued;ui_thread=saved_ui;stats.active=saved_active;boot_mode=saved_boot;boot_phase=saved_phase;
    printf("fp redirect shadow flow/load rows/status/arm/stubs/site tests PASS\n");
}
/* The executable's +0x0 block, entered at 0xea22cf with xmm0 = G0, r14 = comp, rbp = frame, r15 = slot, xmm8 = the
 * abs mask (set by the prologue at 0xea1edc); 0xea239b (after the store to [r15]) jumps back here. */
__asm__(
".text\n"
"test_fr_win_block:\n push %rbp\n push %r14\n push %r15\n push %rbx\n sub $0x28,%rsp\n movdqu %xmm7,(%rsp)\n movdqu %xmm8,0x10(%rsp)\n"
" mov %rdx,%r14\n mov %r8,%rbp\n mov %r9,%r15\n mov $0x7fffffff,%eax\n movd %eax,%xmm8\n movss (%r15),%xmm0\n jmp *%rcx\n"
"test_fr_win_back:\n movss (%r15),%xmm0\n movdqu (%rsp),%xmm7\n movdqu 0x10(%rsp),%xmm8\n add $0x28,%rsp\n pop %rbx\n pop %r15\n pop %r14\n pop %rbp\n ret\n"
);
extern float test_fr_win_block(const void *entry,void *comp,void *frame,void *slot);
extern char test_fr_win_back[];
/* An inlined Set block of 0x857960 / 0x31c640, entered at its rax load (0x857e45 / 0x31cad5) with rbp = frame
 * ([rbp+0x10] = the segment array), r10 = the record's offset, the absolute control points P0..P3 in xmm10, xmm3 and
 * xmm2 (P1: 0x857960 reads xmm3, 0x31c640 xmm2), xmm5, xmm9, xmm11 = 3.0 (the coefficients'), xmm13 = 0.5,
 * xmm14 = 0; it subtracts the record's centre (+0x00), stores P0..P3 at +0x60..+0x90 and the length at +0x18; the jump
 * planted at the tail's end (0x857f8c / 0x31cc1c) returns through test_fr_len_block_back. */
__asm__(
".text\n"
"test_fr_len_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x68,%rsp\n movdqu %xmm9,(%rsp)\n movdqu %xmm10,0x10(%rsp)\n"
" movdqu %xmm11,0x20(%rsp)\n movdqu %xmm13,0x30(%rsp)\n movdqu %xmm14,0x40(%rsp)\n mov %rsp,%rbx\n mov %rdx,%rbp\n mov %r8,%r10\n"
" movups (%r9),%xmm10\n movups 0x10(%r9),%xmm3\n movups 0x10(%r9),%xmm2\n movups 0x20(%r9),%xmm5\n movups 0x30(%r9),%xmm9\n"
" mov $0x40400000,%eax\n movd %eax,%xmm11\n shufps $0,%xmm11,%xmm11\n mov $0x3f000000,%eax\n movd %eax,%xmm13\n xorps %xmm14,%xmm14\n jmp *%rcx\n"
"test_fr_len_block_back:\n mov %rbx,%rsp\n movdqu (%rsp),%xmm9\n movdqu 0x10(%rsp),%xmm10\n movdqu 0x20(%rsp),%xmm11\n movdqu 0x30(%rsp),%xmm13\n"
" movdqu 0x40(%rsp),%xmm14\n add $0x68,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_len_block(const void *entry,void *frame,uint64_t r10,const float *points);
extern char test_fr_len_block_back[];
/* FR13: a handle site's exact-reciprocal window (rsqrtss xmm2,xmm2; rcpss xmm2,xmm2 at site-8), the site and the
 * store after it, on the executable's own bytes: entered at the window with xmm2 = x (the squared chord), xmm8 = k,
 * r9 = rcx = buf, rbx = 0; the out-handle stores [r9+rbx*8+4], the in-handle [rcx+rbx*8+8]; a jump planted after the
 * store returns through test_fr_h_block_back. */
__asm__(
".text\n"
"test_fr_h_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x28,%rsp\n movdqu %xmm8,(%rsp)\n mov %rsp,%rsi\n"
" mov %rcx,%rax\n mov %r9,%rcx\n movd %edx,%xmm2\n movd %r8d,%xmm8\n xor %ebx,%ebx\n jmp *%rax\n"
"test_fr_h_block_back:\n mov %rsi,%rsp\n movdqu (%rsp),%xmm8\n add $0x28,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern void test_fr_h_block(const void *entry,uint32_t x,uint32_t k,void *buf);
extern char test_fr_h_block_back[];
static const uint32_t tfh_after[FR_H_SITES]={0x857db2u,0x857dfbu,0x31ca42u,0x31ca8bu}; /* the instruction after each store */
/* rounds per site; mode as set; returns the splits (d/3 != d*k) seen and checks each stored value against the mode's rule. */
static unsigned tfh_native_runs(unsigned rounds,int through_stub) {
    uint32_t k;memcpy(&k,image+0x2cbe3a4u,4);assert(k==TFH_K);
    unsigned split=0;
    for(unsigned i=0;i<FR_H_SITES;i++) for(unsigned r=0;r<rounds;r++) {
        const float dist=tfr_range(0.001f,90000.0f),x=dist*dist;uint32_t xb;memcpy(&xb,&x,4);
        uint32_t buf[4]={0xdeadbeefu,0xdeadbeefu,0xdeadbeefu,0xdeadbeefu};
        test_fr_h_block(image+fr_h_sites[i].site-8,xb,k,buf);
        const uint32_t d=fr_bits(sl_sqrt(x)),native=sl_handle_windows(d,k),ours=sl_handle(d);
        const uint32_t got=buf[(i&1)?2:1],want=through_stub && fr_mode==FR_REDIRECT?ours:native;
        if(got!=want) {printf("fp redirect handle %u/%u: x %08x got %08x want %08x (native %08x ours %08x)\n",i,r,xb,got,want,native,ours);assert(0);}
        assert(buf[0]==0xdeadbeefu && buf[3]==0xdeadbeefu && buf[(i&1)?1:2]==0xdeadbeefu);
        split+=native!=ours;
    }
    return split;
}
/* FR16: the PointGuard angle on the executable's own bytes: entered at 0xe9c4bc with xmm1 = rem, xmm2 = m+1 (rsp 16-
 * aligned, as in the function's body); a jump planted at 0xe9c4cc returns through test_fr_pg_block_back with xmm1[0]. */
__asm__(
".text\n"
"test_fr_pg_block:\n push %rbp\n push %rbx\n push %rsi\n push %rdi\n sub $0x28,%rsp\n mov %rsp,%rsi\n"
" mov %rcx,%rax\n movd %edx,%xmm1\n movd %r8d,%xmm2\n jmp *%rax\n"
"test_fr_pg_block_back:\n mov %rsi,%rsp\n movd %xmm1,%eax\n add $0x28,%rsp\n pop %rdi\n pop %rsi\n pop %rbx\n pop %rbp\n ret\n"
);
extern uint32_t test_fr_pg_block(const void *entry,uint32_t rem,uint32_t m1);
extern char test_fr_pg_block_back[];
#define TPG_M1 200u
#define TPG_RUNS (TPG_M1*(TPG_M1+1u)/2u) /* every (m+1, rem) with m+1 = 1..200, rem = 0..m */
/* Mode as set; returns the splits (ours != native) seen and checks each angle against the mode's rule. */
static unsigned tpg_native_runs(int through_stub) {
    uint32_t pi;memcpy(&pi,image+FR_PG_PI,4);assert(pi==FR_PG_PI_BITS);
    unsigned split=0;
    for(unsigned m1=1;m1<=TPG_M1;m1++) for(unsigned rem=0;rem<m1;rem++) {
        const float fr=(float)rem,fm=(float)m1;uint32_t rb,mb;memcpy(&rb,&fr,4);memcpy(&mb,&fm,4);
        const uint32_t got=test_fr_pg_block(image+FR_PG_SITE,rb,mb),native=fr_pg_angle_windows(rb,mb),ours=fr_pg_angle(rb,mb);
        const uint32_t want=through_stub && fr_mode==FR_REDIRECT?ours:native;
        if(got!=want) {printf("fp redirect pg m+1 %u rem %u: got %08x want %08x (native %08x ours %08x)\n",m1,rem,got,want,native,ours);assert(0);}
        split+=native!=ours;
    }
    return split;
}
/* Both inlined blocks on their own bytes (exact-reciprocal windows installed, as in the game): native, then through
 * the length stubs in 'shadow' and 'redirect'. Returns the splits (canonical != pairwise) seen. */
static unsigned tfl_native_runs(unsigned rounds,unsigned *zeros) {
    static unsigned char segs[0x200];static uint64_t frame[8];unsigned char *const seg=segs+0x40;
    frame[2]=(uint64_t)(uintptr_t)segs;
    unsigned split=0;
    for(unsigned i=0;i<FR_LEN_SITES;i++) for(unsigned k=0;k<rounds;k++) {
        float pts[16],centre[4];
        for(unsigned c=0;c<4;c++) centre[c]=tfr_range(-50000.0f,50000.0f);
        for(unsigned c=0;c<16;c++) pts[c]=centre[c&3]+tfr_range(-4000.0f,4000.0f);
        if(k%97==0) for(unsigned c=4;c<16;c++) pts[c]=pts[c&3];             /* one point four times: length 0 */
        memset(seg,0x5a,0xa0);memcpy(seg,centre,16);
        test_fr_len_block(image+fr_len_sites[i].pin,frame,0x40,pts);
        uint32_t stored;memcpy(&stored,seg+0x18,4);
        const uint32_t ours=sl_length(seg+0x60),pairwise=sl_length_pairwise(seg+0x60);
        if(fr_mode==FR_REDIRECT) assert(stored==sl_stored_linux(ours));
        else if(stored!=sl_stored_windows(pairwise)) {printf("fp redirect length %u/%u: stored %08x, pairwise %08x\n",i,k,stored,pairwise);assert(0);}
        split+=ours!=pairwise;*zeros+=stored==SL_ONE;
    }
    return split;
}
static void test_fp_redirect_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {
            assert(!fseek(file,section.PointerToRawData,SEEK_SET));
            assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);
        }
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    unsigned char *saved_page=fr_page;fr_page=NULL;
    assert(fr_pins());
    image[FR_REFRESH+0x400]^=1;assert(!fr_pins());image[FR_REFRESH+0x400]^=1;
    image[FR_M_RVA+0x10]^=1;assert(!fr_pins());image[FR_M_RVA+0x10]^=1;assert(fr_pins());
    image[FR_W_BODY+0x1e3]^=1;assert(!fr_pins());image[FR_W_BODY+0x1e3]^=1;              /* the writer and its call site */
    image[FR_W_SITE_BLOCK+0x10]^=1;assert(!fr_pins());image[FR_W_SITE_BLOCK+0x10]^=1;assert(fr_pins());
    for(unsigned i=0;i<2;i++) {image[fr_h_pins[i].pin+0x20]^=1;assert(!fr_pins() && !fr_h_pins_ok());image[fr_h_pins[i].pin+0x20]^=1;} /* FR13 */
    for(unsigned i=0;i<FR_H_SITES;i++) assert(!memcmp(image+fr_h_sites[i].site,fr_h_original,5) && fr_h_sites[i].site-8>=fr_h_pins[fr_h_sites[i].pin].pin);
    /* FR16: the PointGuard function and pi_f pinned; the site's three instructions and the movaps/call after them. */
    image[FR_PG_PIN+0x20]^=1;assert(!fr_pins() && !fr_pg_pins_ok());image[FR_PG_PIN+0x20]^=1;
    image[FR_PG_PI]^=1;assert(!fr_pins() && !fr_pg_pins_ok());image[FR_PG_PI]^=1;
    assert(!memcmp(image+FR_PG_SITE,"\xf3\x0f\x10\x05\xf0\x25\xe2\x01\xf3\x0f\x5e\xc2\xf3\x0f\x59\xc8\x0f\x28\xc1\xe8",20) && FR_PG_BACK-FR_PG_SITE==16);
    assert(FR_PG_SITE+8+0x1e225f0u==FR_PG_PI && FR_PG_SITE>FR_PG_PIN && FR_PG_BACK<FR_PG_PIN+FR_PG_PIN_SIZE);
    assert(fr_pins());
    /* The live boot's order: the exact-reciprocal windows armed first (their view then covers the whole image), then
     * this module through fr_arm itself, in 'redirect': pins (both length blocks hold windows), tails clear, every
     * site installed and owned, restored. FR10/FR11/W5 refused here with stage=pins. */
    {
        assert(xrcp_pins() && xrcp_build() && xrcp_switch(1)==1);xrcp_installed=1;
        const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;const unsigned sids=fr_id_count;
        stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=0;fr_id_count=0;
        for(unsigned i=0;i<FR_LEN_SITES;i++) assert(fp_patch_overlaps(image+fr_len_sites[i].site,5) && fr_len_tail_clear(i));
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0 && fr_h_clear(i));
        assert(fr_pg_state()==0 && fr_pg_clear());
        assert(fr_pins() && fr_arm(FR_REDIRECT,3) && fr_installed && !strcmp(fr_stage,"armed") && fr_check());
        for(unsigned i=0;i<FR_LEN_SITES;i++) assert(fr_len_state(i)==1 && !fr_len_tail_clear(i)); /* ours, shown as the original */
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==1 && !fr_h_clear(i));
        assert(fr_pg_state()==1 && !fr_pg_clear() && (fr_done&FR_PG_DONE) && fr_owned());
        assert(fr_len_pins() && fr_h_pins_ok() && fr_pg_pins_ok() && fr_arm(FR_REDIRECT,3)); /* the view keeps the pins passing; a rerun is accepted */
        fr_restore();assert(!fr_installed && fr_restored && !fr_done && fr_len_state(0)==0 && fr_len_state(1)==0 && fr_tail_state()==0);
        for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0);
        assert(fr_pg_state()==0 && fr_pg_clear());
        for(unsigned i=0;i<FR_SITES;i++) assert(fr_site_state(i)==0);
        VirtualFree(fr_page,0,MEM_RELEASE);fr_page=NULL;fr_restored=0;fr_stage="idle";fr_mode=FR_OFF;
        fr_native_refresh=NULL;fr_native_g4=fr_native_g0=NULL;fr_native_m=NULL;fr_native_params=NULL;fr_native_v=NULL;fr_native_writer=NULL;
        stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;fr_id_count=sids;
        xrcp_restore();assert(!xrcp_installed);VirtualFree(xrcp_region,0,MEM_RELEASE);xrcp_region=NULL;xrcp_restored=0;xrcp_stage="idle";xrcp_attempts=xrcp_busy=0;
        assert(fr_pins());
    }
    /* The frame and field facts the hooks rely on: the getters' buffer lea r8,[rbp+0x50]; w from [rbp-0x8]; comp[A]/[B]. */
    assert(!memcmp(image+0xea22a7,"\x4c\x8d\x45\x50",4) && !memcmp(image+0xea22be,"\x4c\x8d\x45\x50",4) && !memcmp(image+0xea2307,"\xf3\x0f\x10\x4d\xf8",5));
    assert(!memcmp(image+0xea229e,"\x48\x8d\x55\xf0",4) && !memcmp(image+0xea22e2,"\xf3\x41\x0f\x5e\x96\xf4\x05\x00\x00",9) && !memcmp(image+0xea22f6,"\xf3\x41\x0f\x5e\x9e\xf8\x05\x00\x00",9));
    assert(!memcmp(image+0xea23da,"\xf3\x41\x0f\x5e\xbe\xf4\x05\x00\x00",9) && !memcmp(image+0xea23e3,"\xf3\x41\x0f\x11\x7f\x24",6));
    /* The first call's max block: 1.0/d (xmm6 = 1.0 from 0x6dc6d5, d = xmm7) at 0x6dcd14, then mulss by it at 0x6dcd18;
     * K from [r15+0x300] (0x6dcceb), r15 = [0x146cf1538] (0x6dcbb0); v from rdx = rsp+0x40 (0x6dccc2). */
    static const unsigned char rcp[8]={0xf3,0x0f,0x5e,0xf7,0xf3,0x0f,0x59,0xc6},one[4]={0xf3,0x0f,0x10,0x35};
    static const unsigned char kload[9]={0xf3,0x41,0x0f,0x10,0x87,0x00,0x03,0x00,0x00},vbuf[5]={0x48,0x8d,0x54,0x24,0x40},table[3]={0x4c,0x8b,0x3d};
    assert(!memcmp(image+0x6dcd14,rcp,8) && !memcmp(image+0x6dc6d5,one,4) && !memcmp(image+0x6dcceb,kload,9) && !memcmp(image+0x6dccc2,vbuf,5));
    {int32_t d;memcpy(&d,image+0x6dcbb3,4);assert(!memcmp(image+0x6dcbb0,table,3) && 0x6dcbb7u+(uint32_t)d==0x6cf1538u);}
    /* Run the executable's M and +0x0 block on its own bytes (this copy only). */
    DWORD old;assert(VirtualProtect(image+0xea2000,0x2000,PAGE_EXECUTE_READWRITE,&old));
    unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_win_back;memcpy(back+6,&to,8);memcpy(image+0xea239b,back,14);
    FlushInstructionCache(GetCurrentProcess(),image+0xea2000,0x2000);
    const fr_m_fn native_m=(fr_m_fn)(void*)(image+FR_M_RVA);
    unsigned m_equal=0,m_negzero=0;
    for(unsigned i=0;i<20000;i++) {
        const float a=tfr_range(-500.0f,500.0f),b=(i%7==0)?a+tfr_range(-5e-5f,5e-5f):tfr_range(-500.0f,500.0f),c=(i%5==0)?a:tfr_range(-800.0f,800.0f);
        const uint64_t flag=tfr_next()&1u;
        const uint32_t n=fr_bits(native_m(a,b,c,flag)),o=fr_bits(fr_m((unsigned)flag,a,b,c));
        if(n==o) {++m_equal;continue;}
        assert(o==0x80000000u && n==0u && c==a && b-a<0.0f);++m_negzero; /* r = -0: Linux keeps it, Windows' maxss gives +0 */
    }
    assert(m_equal>15000 && m_negzero>0);
    static unsigned char frame[0x200];unsigned char slot[0x40];unsigned block_equal=0;
    unsigned char *rbp=frame+0x20;
    for(unsigned i=0;i<20000;i++) {
        const float ca=tfr_range(0.0005f,0.02f),cb=(i%9==0)?ca:tfr_range(0.0005f,0.02f),g0=tfr_range(0.01f,2000.0f);
        float w=tfr_range(0.0f,2500.0f);
        if(i%11==0) w=1.0f/cb;else if(i%13==0) w=1.0f/ca;
        memcpy(tfr_comp+FR_COMP_A,&ca,4);memcpy(tfr_comp+FR_COMP_B,&cb,4);memcpy(rbp-8,&w,4);memcpy(slot,&g0,4);
        const uint32_t n=fr_bits(test_fr_win_block(image+0xea22cf,tfr_comp,rbp,slot));
        uint32_t ours[3];fr_recompute(fr_bits(ca),fr_bits(cb),0,fr_bits(g0),fr_bits(w),ours);
        if(n!=ours[0]) {printf("fp redirect block %u: ca=%08x cb=%08x g0=%08x w=%08x native %08x ours %08x\n",i,fr_bits(ca),fr_bits(cb),fr_bits(g0),fr_bits(w),n,ours[0]);assert(0);}
        ++block_equal;
    }
    /* The writer 0x6db200 (and its helper 0x6db600) on its own bytes, random inputs through the fake ship and the table
     * pointer of this copy: native equals Windows' order always; against ours (Linux's order) it splits. */
    assert(!memcmp(image+0x6db3e3,"\xf3\x0f\x5e\xe2",4)); /* divss xmm4,xmm2: 1.0/den, then mulss by it */
    assert(VirtualProtect(image+0x6db000,0x1000,PAGE_EXECUTE_READWRITE,&old));FlushInstructionCache(GetCurrentProcess(),image+0x6db000,0x1000);
    const fr_writer_fn native_w=(fr_writer_fn)(void*)(image+FR_W_BODY);
    unsigned w_equal=0,w_split=0,w_k=0,w_fields[4]={0,0,0,0};uint32_t w_max=0;
    for(unsigned i=0;i<20000;i++) {
        fr_w_in in,got;tfr_w_random(i,&in);tfw_load(&in);
        unsigned char wslot[0x60];memset(wslot,0,sizeof(wslot));
        (void)native_w(tfw_ship,tfw_data,tfw_a,wslot);
        assert(fr_w_read(tfw_ship,tfw_data,tfw_a,&got) && got.has_p==in.has_p && (!in.has_p || got.p==in.p) && !memcmp(got.a,in.a,16) && !memcmp(got.d,in.d,20));
        assert(!memcmp(got.dm,in.dm,16) && !memcmp(got.r,in.r,20) && got.t_scale==in.t_scale && got.k==in.k && got.t_clamp==in.t_clamp);
        uint32_t n[4],ours[4],win[4];memcpy(n,wslot,16);fr_w_recompute(&got,ours);tfr_w_windows(&got,win);
        if(i%17==0) ++w_k;
        int split=0;
        for(unsigned k=0;k<4;k++) {
            if(n[k]!=win[k]) {printf("fp redirect writer %u field %u: native %08x, Windows' order %08x\n",i,k,n[k],win[k]);assert(0);}
            if(n[k]!=ours[k]) {split=1;++w_fields[k];const uint32_t u=fr_ulp(n[k],ours[k]);if(u>w_max) w_max=u;}
        }
        if(split) ++w_split;else ++w_equal;
    }
    memset(image+0x6cf1538u,0,8);
    assert(w_split>1000 && w_max<=8);
    /* FR10/FR11: the length pins; then both inlined Set blocks on their own bytes with the exact-reciprocal windows
     * installed (their rcp(rsqrt) chains as sqrt, as in the game): native stores Windows' pairwise length on every
     * input; through the stubs 'shadow' stores the same, 'redirect' the canonical length with Linux's zero rule. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        assert(!memcmp(image+fr_len_sites[i].site,fr_len_original[i],5));
        image[fr_len_sites[i].pin+0x30]^=1;assert(!fr_len_pins());image[fr_len_sites[i].pin+0x30]^=1;
    }
    assert(fr_len_pins() && xrcp_pins() && xrcp_build() && xrcp_switch(1)==1);xrcp_installed=1;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char *at=image+(fr_len_sites[i].pin&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_len_block_back;memcpy(back+6,&to,8);
        memcpy(image+fr_len_sites[i].back,back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    const uint32_t saved_active=stats.active;stats.active=1;unsigned zeros=0;
    fr_mode=FR_OFF;const unsigned native_split=tfl_native_runs(10000,&zeros);
    fr_page=fr_alloc_page();assert(fr_page && VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old));fr_write_code(fr_page);
    assert(VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),fr_page,0x1000);
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char jump[5];fr_len_jump(i,jump);assert(fr_len_reach(i) && view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5));
        fr_done|=FR_LEN_DONE<<i;assert(fr_len_state(i)==1);
        unsigned char seen[5];assert(read_local(image+fr_len_sites[i].site,seen,5) && !memcmp(seen,fr_len_original[i],5)); /* the view */
    }
    const LONG64 n0=fr_n[FR_LEN],d0=fr_diff[FR_LEN],h0=fr_hits[FR_HIT_LEN0],h1=fr_hits[FR_HIT_LEN1];
    fr_mode=FR_SHADOW;const unsigned shadow_split=tfl_native_runs(10000,&zeros);
    assert(fr_n[FR_LEN]==n0+20000 && fr_diff[FR_LEN]==d0+shadow_split && fr_hits[FR_HIT_LEN0]==h0+10000 && fr_hits[FR_HIT_LEN1]==h1+10000);
    fr_mode=FR_REDIRECT;const unsigned redirect_split=tfl_native_runs(10000,&zeros);
    /* FR13: the handle sites on their own bytes (windows installed): native = d * 0.333333343f, then through the stubs
     * 'shadow' keeps it and 'redirect' stores d / 3.0f. */
    assert(fr_h_pins_ok());
    for(unsigned i=0;i<FR_H_SITES;i++) {
        assert(fr_h_state(i)==0 && fr_h_clear(i));
        unsigned char *at=image+(tfh_after[i]&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_h_block_back;memcpy(back+6,&to,8);
        memcpy(image+tfh_after[i],back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    fr_mode=FR_OFF;const unsigned h_native=tfh_native_runs(5000,0);
    for(unsigned i=0;i<FR_H_SITES;i++) {
        unsigned char jump[5];fr_h_jump(i,jump);assert(fr_h_reach(i) && view_cas_bytes(fr_h_sites[i].site,fr_h_original,jump,5));
        fr_done|=FR_H_DONE<<i;assert(fr_h_state(i)==1 && !fr_h_clear(i));
        unsigned char seen[5];assert(read_local(image+fr_h_sites[i].site,seen,5) && !memcmp(seen,fr_h_original,5)); /* the view */
    }
    const LONG64 hn0=fr_n[FR_HANDLE];
    fr_mode=FR_SHADOW;const unsigned h_shadow=tfh_native_runs(5000,1);
    assert(fr_n[FR_HANDLE]==hn0+20000 && fr_diff[FR_HANDLE]==(LONG64)h_shadow);
    fr_mode=FR_REDIRECT;const unsigned h_redirect=tfh_native_runs(5000,1);
    assert(fr_handle_written==(LONG64)h_redirect && h_native>1000 && h_shadow>1000 && h_redirect>1000);
    /* FR16: the PointGuard angle on its own bytes for every (m+1, rem) up to 200: native = rem * (pi/(m+1)); through the
     * stub 'shadow' keeps it and 'redirect' leaves (rem / ((m+1)+(m+1))) * 2pi. */
    assert(fr_pg_pins_ok() && fr_pg_state()==0 && fr_pg_clear());
    {   unsigned char *at=image+(FR_PG_BACK&~0xfffu);assert(VirtualProtect(at,0x2000,PAGE_EXECUTE_READWRITE,&old));
        unsigned char back[14]={0xff,0x25,0,0,0,0};void *to=(void*)test_fr_pg_block_back;memcpy(back+6,&to,8);
        memcpy(image+FR_PG_BACK,back,14);FlushInstructionCache(GetCurrentProcess(),at,0x2000);
    }
    fr_mode=FR_OFF;const unsigned pg_native=tpg_native_runs(0);
    {   unsigned char jump[5];fr_pg_jump(jump);assert(fr_pg_reach() && view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5));
        fr_done|=FR_PG_DONE;assert(fr_pg_state()==1 && !fr_pg_clear());
        unsigned char seen[5];assert(read_local(image+FR_PG_SITE,seen,5) && !memcmp(seen,fr_pg_original,5)); /* the view */
    }
    const LONG64 pn0=fr_n[FR_PG],ph0=fr_hits[FR_HIT_PG];
    fr_mode=FR_SHADOW;const unsigned pg_shadow=tpg_native_runs(1);
    assert(fr_n[FR_PG]==pn0+TPG_RUNS && fr_diff[FR_PG]==(LONG64)pg_shadow && fr_hits[FR_HIT_PG]==ph0+TPG_RUNS);
    fr_mode=FR_REDIRECT;const unsigned pg_redirect=tpg_native_runs(1);
    assert(fr_pg_written==(LONG64)pg_redirect && pg_native>1000 && pg_shadow==pg_native && pg_redirect==pg_native && fr_max_ulp[FR_PG]<=2);
    fr_installed=1;fr_restore();assert(!fr_installed && !fr_done && fr_len_state(0)==0 && fr_len_state(1)==0);
    for(unsigned i=0;i<FR_H_SITES;i++) assert(fr_h_state(i)==0);
    assert(fr_pg_state()==0);
    printf("fp redirect native image: PointGuard angle 0xe9c4bc on its own bytes: native = rem*(pi/(m+1)) on %u (m+1 <= %u; %u split from "
        "(rem/2(m+1))*2pi, max %lld ulp); shadow keeps it (%u split), redirect leaves ours (%u written) PASS\n",
        TPG_RUNS,TPG_M1,pg_native,(long long)fr_max_ulp[FR_PG],pg_shadow,pg_redirect);
    fr_n[FR_PG]=fr_diff[FR_PG]=fr_max_ulp[FR_PG]=0;fr_pg_written=0;
    fr_n[FR_HANDLE]=fr_diff[FR_HANDLE]=fr_max_ulp[FR_HANDLE]=0;fr_handle_written=0;
    printf("fp redirect native image: handle sites 0x857d19/0x857df0/0x31c9a9/0x31ca80 on their own bytes (exact reciprocal installed): "
        "native = d*0.333333343f on 20000 (%u split from d/3); shadow keeps it (%u split), redirect stores d/3 (%u written) PASS\n",h_native,h_shadow,h_redirect);
    fr_restored=0;fr_mode=FR_OFF;stats.active=saved_active;fr_n[FR_LEN]=fr_diff[FR_LEN]=fr_max_ulp[FR_LEN]=0;fr_len_written=0;
    VirtualFree(fr_page,0,MEM_RELEASE);
    xrcp_restore();assert(!xrcp_installed);VirtualFree(xrcp_region,0,MEM_RELEASE);xrcp_region=NULL;xrcp_restored=0;xrcp_stage="idle";xrcp_attempts=xrcp_busy=0;
    assert(native_split>500 && shadow_split>500 && redirect_split>500 && zeros>=6*(10000/97));
    printf("fp redirect native image: Set inlined at 0x857960/0x31c640 on its own bytes (exact reciprocal installed): native = pairwise "
        "on 20000 (%u split from the canonical); shadow stores native (%u split), redirect the canonical (%u split); %u zero lengths stored 1.0 PASS\n",
        native_split,shadow_split,redirect_split,zeros);
    fr_page=saved_page;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    printf("fp redirect native image: pins, frame facts, M %u equal + %u -0 cases, +0x0 block %u equal PASS\n",m_equal,m_negzero,block_equal);
    printf("fp redirect native image: writer 0x6db200 = Windows' order on 20000 (%u all-K); vs ours %u equal, %u split (fields %u/%u/%u/%u), max %u ulp PASS\n",
        w_k,w_equal,w_split,w_fields[0],w_fields[1],w_fields[2],w_fields[3],w_max);
}
