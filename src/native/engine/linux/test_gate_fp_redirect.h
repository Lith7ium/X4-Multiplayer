/* The refresh twin (gate_fp_redirect.h): the strict vectors; on the synthetic image a stand-in refresh behind the
 * real prologue bytes that calls G4, G0 and M through call sites at their addresses (sub rsp,8; call; add rsp,8;
 * ret), so the entry patch, the trampoline and the four rel32 swaps run for real; counters, rows, ownership and
 * the restoration. On the executable's bytes: entry word, sites and frame facts, its own L (0x1a48040) and M
 * (0x1429e80) run against ours, and the patch with its exact restoration. Included by test_gate_linux.c. */
#include "test_fp_redirect_vectors.h"
static unsigned char tlf_comp[0x700],tlf_ship[0x20];
static float tlf_w;
static int tlf_branch;
static unsigned tlf_refreshes;
typedef float (*tlf_getter_site)(void *,void *,void *);
typedef float (*tlf_m_site)(uint64_t,float,float,float);
typedef void (*tlf_p_site)(void *,unsigned char *,uint64_t,uint64_t,uint64_t,double);
/* The first call: writes +0x10c (10.0) on every path. */
static void tlf_params(void *o20,unsigned char *slot,uint64_t flag,uint64_t a4,uint64_t a5,double time) {
    (void)flag;(void)a4;(void)a5;(void)time;assert(o20==tlf_ship);const float ten=10.0f;memcpy(slot+0x10c,&ten,4);
}
static float tlf_g4(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tlf_comp && o20==tlf_ship);return fr_f32(0x43c9859bu);}
static float tlf_g0(void *comp,void *o20,void *buf) {(void)buf;assert(comp==tlf_comp && o20==tlf_ship);return fr_f32(0x4383fc81u);}
static float tlf_m(uint64_t flag,float a,float b,float c) {return fr_m((unsigned)(flag&0xffu),a,b,c);}
/* The executable's order: G4 -> +0x4, G0, +0x0 = G0 * L, +0x24, +0x8/+0xc = 400; the M branch on request. */
static void tlf_refresh(void *owner,void *slot,uint64_t flag) {
    unsigned char frame[0x100] __attribute__((aligned(16))),*buf=frame+0x70,*s=slot;(void)owner;++tlf_refreshes;
    ((tlf_p_site)(void*)(LX(LX_FR_P_SITE_VA)-4))(tlf_ship,s,flag,0,0,0.0);
    if(flag&1) return;
    memcpy(buf+FR_W_FROM_BUFFER,&tlf_w,4);
    const float g4=((tlf_getter_site)(void*)(LX(LX_FR_G4_SITE_VA)-4))(tlf_comp,tlf_ship,buf);memcpy(s+4,&g4,4);
    const float g0=((tlf_getter_site)(void*)(LX(LX_FR_G0_SITE_VA)-4))(tlf_comp,tlf_ship,buf);
    uint32_t ca,cb,out[3],four=0x43c80000u;memcpy(&ca,tlf_comp+FR_COMP_A,4);memcpy(&cb,tlf_comp+FR_COMP_B,4);
    fr_recompute(ca,cb,fr_bits(g4),fr_bits(g0),fr_bits(tlf_w),out);
    memcpy(s,&out[0],4);memcpy(s+0x24,&out[2],4);memcpy(s+8,&four,4);memcpy(s+0xc,&four,4);
    if(tlf_branch) {
        float a,b,v,m;memcpy(&a,s+0x2c,4);memcpy(&b,s+0x24,4);
        m=((tlf_m_site)(void*)(LX(LX_FR_M4_SITE_VA)-4))(1,a,b,fr_f32(0x43960000u));memcpy(&v,s+4,4);v=m*v;memcpy(s+4,&v,4);
        m=((tlf_m_site)(void*)(LX(LX_FR_M0_SITE_VA)-4))(1,b,a,fr_f32(0x43960000u));memcpy(&v,s,4);v=m*v;memcpy(s,&v,4);
    }
}
/* The writer's detour entered as at 0x148cd53: rdi the detour, rsi o20 (-> r12), rdx D (-> [rsp+0x28]), rcx A (-> r14
 * and rdx), r8 slot (-> rbx); rsp 16-aligned as at the call; 0x148cd63 jumps to test_lxw_back. */
__asm__(
".text\n"
"test_lxw_run:\n push %rbx\n push %r12\n push %r14\n push %rbp\n sub $0x38,%rsp\n"
" mov %rsi,%r12\n mov %rcx,%r14\n mov %r8,%rbx\n mov %rdx,0x28(%rsp)\n mov %r14,%rdx\n jmp *%rdi\n"
"test_lxw_back:\n add $0x38,%rsp\n pop %rbp\n pop %r14\n pop %r12\n pop %rbx\n ret\n"
);
extern void test_lxw_run(void *entry,void *o20,void *data,void *a,void *slot);
extern char test_lxw_back[];
/* The fake ship's five getters for the executable's writer (test_fp_redirect_elf). */
static float tlw_r[5];
static float tlw_r0(void *o) {(void)o;return tlw_r[0];}
static float tlw_r1(void *o) {(void)o;return tlw_r[1];}
static float tlw_r2(void *o) {(void)o;return tlw_r[2];}
static float tlw_r3(void *o) {(void)o;return tlw_r[3];}
static float tlw_r4(void *o) {(void)o;return tlw_r[4];}
static void tlf_site_code(uint32_t site_va,uint32_t target_va) {
    unsigned char code[14]={0x48,0x83,0xec,0x08,0xe8,0,0,0,0,0x48,0x83,0xc4,0x08,0xc3};
    const int32_t rel=(int32_t)((intptr_t)LX(target_va)-(intptr_t)(LX(site_va)+5));memcpy(code+5,&rel,4);
    tl_write(LX(site_va)-4,code,sizeof(code));
}
static void tlf_jump_code(uint32_t va,const void *target) {unsigned char code[14]={0xff,0x25,0,0,0,0};memcpy(code+6,&target,8);tl_write(LX(va),code,14);}
static void test_fp_redirect_linux(void) {
    test_fr_vectors();
    /* The entry: its real 8 bytes, then pop r12..r15 and jmp [rip] -> tlf_refresh. */
    unsigned char entry[30]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0xff,0x25,0,0,0,0};
    void *target=(void*)tlf_refresh;memcpy(entry+22,&target,8);tl_write(LX(LX_FR_REFRESH_VA),entry,sizeof(entry));
    tlf_site_code(LX_FR_G4_SITE_VA,LX_FR_G4_VA);tlf_site_code(LX_FR_G0_SITE_VA,LX_FR_G0_VA);
    tlf_site_code(LX_FR_M4_SITE_VA,LX_FR_M_VA);tlf_site_code(LX_FR_M0_SITE_VA,LX_FR_M_VA);tlf_site_code(LX_FR_P_SITE_VA,LX_FR_P_VA);
    tlf_jump_code(LX_FR_G4_VA,(void*)tlf_g4);tlf_jump_code(LX_FR_G0_VA,(void*)tlf_g0);tlf_jump_code(LX_FR_M_VA,(void*)tlf_m);tlf_jump_code(LX_FR_P_VA,(void*)tlf_params);
    for(unsigned j=0;j<LXF_DETOURS;j++) tl_write(LX(lxf_detours[j].at),lxf_detours[j].orig,lxf_detours[j].len); /* the first call's three detours */
    assert(rd64(LX(LX_FR_REFRESH_VA))==LX_FR_ENTRY_ORIGINAL);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9201,0x7dcd0));
    const uint64_t runtime=9201;memcpy(tlf_ship+8,&runtime,8);
    const uint32_t ca=tfr_vectors[0].ca,cb=tfr_vectors[0].cb;memcpy(tlf_comp+FR_COMP_A,&ca,4);memcpy(tlf_comp+FR_COMP_B,&cb,4);
    tlf_w=fr_f32(tfr_vectors[0].w);
    fr_id_count=0;fr_installed=0;
    assert(!fr_arm(7,3) && !strcmp(fr_stage,"mode"));
    assert(fr_watch(0x7dcd0) && fr_watch(0x7dcd0) && fr_id_count==1);
    boot_phase=2;assert(!fr_arm(FR_SHADOW,3) && !strcmp(fr_stage,"refused"));boot_phase=0;
    assert(fr_arm(FR_SHADOW,3) && fr_installed && fr_arm(FR_SHADOW,3) && !fr_arm(FR_SHADOW,4));
    unsigned char *at=LX(LX_FR_REFRESH_VA);int32_t rel;memcpy(&rel,at+1,4);
    assert(at[0]==0xe9 && at+5+rel==thunk+LXF_HOOK && !memcmp(at+5,"\x55\x41\x54",3) && !memcmp(thunk+LXF_THUNK,"\x41\x57\x41\x56\x41\x55\xff\x25",8));
    for(unsigned i=0;i<LXF_SITES;i++) assert(lxf_site_state(i)==1);
    assert(fr_native_refresh==(fr_refresh_fn)(void*)(thunk+LXF_THUNK) && fr_owned() && fr_check());
    /* One refresh in the load (tick 0), then three in tick 1 through the patched entry: equal, the flag path, M. */
    void (*refresh)(void *,void *,uint64_t)=(void (*)(void *,void *,uint64_t))(void*)at;
    static __attribute__((aligned(16))) unsigned char slot[0x1a0];memset(slot,0,sizeof(slot));
    const float s2c=fr_f32(0x43820000u);memcpy(slot+0x2c,&s2c,4);
    char out[2048];
    tlf_refreshes=0;boot_phase=2;stats.issued=0;refresh((void*)0x1234,slot,0);assert(!fr_row(0,out,sizeof(out)));
    boot_phase=3;stats.issued=1;
    refresh((void*)0x1234,slot,0);refresh((void*)0x1234,slot,1);tlf_branch=1;refresh((void*)0x1234,slot,0);tlf_branch=0;
    assert(tlf_refreshes==4);
    assert(fr_status(out,sizeof(out))>0);
    assert(strstr(out,"calls=4 compared=3 load_compared=1 skipped=1 unfinished=0 no_context=0 orphans=0 m_branch=1 v8_branch=0 comp_changed=0"));
    assert(strstr(out," f0=0:0:3 f4=0:0:3 f24=0:0:3 m=0:0:2 f0_out=0:0:3 f4_out=0:0:3 p0=0:0:0 p4=0:0:0 p8=0:0:0 pc=0:0:0 w0=0:0:0 w4=0:0:0 w8=0:0:0 wc=0:0:0 hits=g4:3,g0:3,m4:1,m0:1 params=4 params_flag=1 v=0 mulps=0 writer=0 w_unread=0 w_written=0 ids=1 ticks=3 records=7 dropped=0 scanned=0 keyed=0 rows=0 failures=0"));
    assert(fr_row(0,out,sizeof(out))>0 && strstr(out,"index=0 record=0 kind=params tick=0 key=000000000007dcd0 ordinal=0 thread=ui flag=0 v_path=0 mulps=0 d=00000000") && strstr(out,"post100=00000000000000000000000000002041"));
    assert(fr_row(1,out,sizeof(out))>0 && strstr(out,"index=1 record=1 kind=compare tick=0 key=000000000007dcd0 ordinal=1 thread=ui seen=3 ca=3b23d70a cb=3b83126f g4=43c9859b g0=4383fc81 w=43960000"));
    assert(fr_row(2,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=0"));
    assert(fr_row(3,out,sizeof(out))>0 && strstr(out,"index=3 record=3 kind=compare tick=1 key=000000000007dcd0 ordinal=1"));
    assert(strstr(out,"native=468a2c56,43c9859b,43c80000 ours=468a2c56,43c9859b,43c80000"));
    assert(fr_row(4,out,sizeof(out))>0 && strstr(out,"kind=params tick=1 key=000000000007dcd0 ordinal=2 thread=ui flag=1"));
    assert(fr_row(5,out,sizeof(out))>0 && strstr(out,"kind=params"));
    assert(fr_row(6,out,sizeof(out))>0 && strstr(out,"ordinal=4 thread=ui seen=f") && strstr(out,"m4=43820000,43c80000,43960000,1,3f124925,3f124925,43c9859b"));
    assert(!fr_row(7,out,sizeof(out)) && fr_status(out,sizeof(out))>0 && strstr(out,"scanned=7 keyed=7 rows=7"));
    /* Ownership, then the restoration. */
    unsigned char *word=(unsigned char*)((uintptr_t)(lxf_site(1)+1)&~(uintptr_t)7);const uint64_t before=rd64(word);
    assert(gate_code_swap64(word,before,before^((uint64_t)1<<(8*(unsigned)(lxf_site(1)+2-word)))) && !fr_owned());
    assert(gate_code_swap64(word,rd64(word),before) && fr_owned() && fr_check());
    fr_restore();assert(!fr_installed && fr_restored && fr_mode==FR_OFF && rd64(at)==LX_FR_ENTRY_ORIGINAL);
    for(unsigned i=0;i<LXF_SITES;i++) assert(lxf_site_state(i)==0);
    fr_id_count=0;fr_restored=0;fr_stage="idle";fr_records=0;fr_scan=fr_emitted=fr_scanned=fr_keyed=0;
    world_identity_installed=0;world_identity_count=0;
    printf("fp redirect prologue/sites/flow/rows/restore PASS\n");
}
/* The executable's bytes: what the hooks rely on, its L and M against ours, the patch and its exact restoration. */
static void test_fp_redirect_elf(void) {
    assert(rd64(LX(LX_FR_REFRESH_VA))==LX_FR_ENTRY_ORIGINAL && (LX_FR_REFRESH_VA&7)==0);
    for(unsigned i=0;i<LXF_SITES;i++) {
        const unsigned char *site=lxf_site(i);int32_t d;memcpy(&d,site+1,4);
        assert(site[0]==0xe8 && site+5+d==LX(lxf_sites[i].target) && ((uintptr_t)(site+1)&7)<=4);
    }
    {const unsigned char *call=LX(0x1437c79);int32_t d;memcpy(&d,call+1,4);assert(call[0]==0xe8 && call+5+d==LX(LX_FR_LERP_VA));}
    /* The first call's detours: the moved instructions, the v call after the first, d at [rsp] (0x148d7e5), K's table
     * (0x148d02a), the branch call after the second, and no line crossing for the 5 patched bytes. */
    for(unsigned j=0;j<LXF_DETOURS;j++) assert(lxf_detour_state(j)==0 && ((uintptr_t)LX(lxf_detours[j].at)&15)+5<=16);
    {const unsigned char *call=LX(0x148d80d);int32_t d;memcpy(&d,call+1,4);assert(call[0]==0xe8 && call+5+d==LX(LX_FR_V_VA) && call+5==LX(LX_FR_V_BACK_VA));}
    {const unsigned char *call=LX(0x148d94e);int32_t d;memcpy(&d,call+1,4);assert(call[0]==0xe8 && call+5+d==LX(LX_FR_WRITER_VA) && call+5==LX(LX_FR_MULPS_BACK_VA));}
    {const unsigned char *call=LX(0x148cd5e);int32_t d;memcpy(&d,call+1,4);assert(call[0]==0xe8 && call+5+d==LX(LX_FR_WRITER_VA) && call+5==LX(LX_FR_WRITER_BACK_VA));}
    /* The watched path before the writer: the ship's virtual +0x2030, rdx = r14 (0x148cd4a), the defaults' rep movsq. */
    assert(!memcmp(LX(0x148cd3c),"\xff\x90\x30\x20\x00\x00",6) && !memcmp(LX(0x148cd4a),"\x4c\x89\xf2",3) && !memcmp(LX(0x148cd50),"\xf3\x48\xa5",3));
    for(unsigned j=0;j<LXF_DETOURS;j++) assert(LX(lxf_detours[j].at)+lxf_detours[j].len==LX(lxf_detours[j].back));
    assert(!memcmp(LX(0x148d7e5),"\xf3\x0f\x11\x0c\x24",5) && !memcmp(LX(0x148d7ea),"\x48\x8b\x74\x24\x20",5) && !memcmp(LX(0x148cb85),"\x48\x8d\xb4\x24\x20\x01\x00\x00",8));
    {const unsigned char *at=LX(0x148d02a);int32_t d;memcpy(&d,at+3,4);assert(!memcmp(at,"\x4c\x8b\x2d",3) && at+7+d==LX(LX_FR_K_TABLE_VA));}
    assert(!memcmp(LX(0x148d828),"\xf3\x41\x0f\x10\x85\x18\x03\x00\x00",9) && !memcmp(LX(0x148d840),"\xf3\x0f\x5e\xc1",4));
    assert(!memcmp(LX(0x14379fc),"\x4c\x8d\x74\x24\x70",5) && !memcmp(LX(0x1437c05),"\x4c\x89\xf2\x4c\x89\xe7",6) && !memcmp(LX(0x1437c19),"\x4c\x89\xf2\x4c\x89\xe7",6));
    assert(!memcmp(LX(0x1437c3d),"\xf3\x0f\x5d\x54\x24\x38",6) && !memcmp(LX(0x1437be6),"\x48\x8d\x74\x24\x30",5));
    assert(!memcmp(LX(0x1437c30),"\xf3\x41\x0f\x5e\x8c\x24\xd4\x05\x00\x00",10) && !memcmp(LX(0x1437c50),"\xf3\x41\x0f\x5e\x84\x24\xd8\x05\x00\x00",10));
    assert(!memcmp(LX(0x1437cc7),"\xf3\x41\x0f\x5e\x84\x24\xd4\x05\x00\x00",10) && !memcmp(LX(0x1437cdc),"\xf3\x0f\x11\x43\x24",5));
    /* Its L and M, run where the image holds them (their constants are RIP-relative inside it). */
    for(unsigned k=0;k<2;k++) {uintptr_t page=(uintptr_t)LX(k?LX_FR_M_VA:LX_FR_LERP_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));}
    float (*lerp)(float,float,float,float,float,float)=(float (*)(float,float,float,float,float,float))(void*)LX(LX_FR_LERP_VA);
    float (*m)(uint64_t,float,float,float)=(float (*)(uint64_t,float,float,float))(void*)LX(LX_FR_M_VA);
    const float a=fr_f32(FR_LERP_A),one=fr_f32(FR_ONE),eps=fr_f32(FR_LERP_EPS);unsigned l_equal=0,m_equal=0,m_negzero=0;
    for(unsigned i=0;i<20000;i++) {
        const float ca=tfr_range(0.0005f,0.02f),cb=(i%9==0)?ca:tfr_range(0.0005f,0.02f),x1=one/ca,x0=one/cb;
        float x=tfr_range(0.0f,2500.0f);
        if(i%11==0) x=x0;else if(i%13==0) x=x1;
        if(i%2) {const float mm=x1<x?x1:x;x=x0>mm?x0:mm;} /* half clamped as the refresh does, half raw */
        const uint32_t n=fr_bits(lerp(x0,x1,a,one,x,eps)),o=fr_bits(fr_lerp(x0,x1,a,one,x,eps));
        if(n!=o) {printf("fp redirect L %u: x0=%08x x1=%08x x=%08x native %08x ours %08x\n",i,fr_bits(x0),fr_bits(x1),fr_bits(x),n,o);assert(0);}
        ++l_equal;
        const float ma=tfr_range(-500.0f,500.0f),mb=(i%7==0)?ma+tfr_range(-5e-5f,5e-5f):tfr_range(-500.0f,500.0f),mc=(i%5==0)?ma:tfr_range(-800.0f,800.0f);
        const uint64_t flag=tfr_next()&1u;
        const uint32_t mn=fr_bits(m(flag,ma,mb,mc)),mo=fr_bits(fr_m((unsigned)flag,ma,mb,mc));
        if(mn!=mo) {printf("fp redirect M %u: native %08x ours %08x\n",i,mn,mo);assert(0);}
        ++m_equal;if(mo==0x80000000u) ++m_negzero;
    }
    assert(m_negzero>0);
    /* Its writer 0x14811c0 (with 0x1481050 and the tail 0x1481150) on its own bytes: random inputs through a fake ship
     * (the five getters at vtable +0x2078..+0x2098, p behind +0x398) and the table pointer [0x3db83c8] of this image;
     * native equals ours (Linux's order) always. */
    {uintptr_t page=(uintptr_t)LX(LX_FR_WRITER_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));}
    assert(!memcmp(LX(0x1481380),"\xf3\x0f\x5e\xeb",4) && !memcmp(LX(0x1481389),"\xf3\x0f\x59\xab\x18\x09\x00\x00",8)); /* a0/den, then *D918 */
    const fr_writer_fn native_w=(fr_writer_fn)(void*)LX(LX_FR_WRITER_VA);
    static void *wvt[0x420];static unsigned char wship[0x400],wdata[0xa00],wa[0x40],wp[0x60],wtable[0x5100];
    static float (*const wfns[5])(void *)={tlw_r0,tlw_r1,tlw_r2,tlw_r3,tlw_r4};
    unsigned w_equal=0,w_k=0;
    for(unsigned i=0;i<20000;i++) {
        fr_w_in in,got;tfr_w_random(i,&in);
        void *vt=wvt,*t=wtable,*p=in.has_p?(void*)wp:NULL;memcpy(wship,&vt,8);memcpy(wship+0x398,&p,8);memcpy(wp+0x50,&in.p,4);
        for(unsigned k=0;k<5;k++) {wvt[(0x2078u>>3)+k]=(void*)wfns[k];memcpy(&tlw_r[k],&in.r[k],4);}
        memcpy(wa,in.a,16);memcpy(wdata+0x914,in.d,20);memcpy(wdata+0x998,in.dm,16);
        memcpy(wtable+0x4fd0,&in.t_scale,4);memcpy(wtable+0x318,&in.k,4);memcpy(wtable+0x330,&in.t_clamp,4);memcpy(LX(LX_FR_K_TABLE_VA),&t,8);
        unsigned char wslot[0x60] __attribute__((aligned(16)));memset(wslot,0,sizeof(wslot));
        (void)native_w(wship,wdata,wa,wslot);
        assert(fr_w_read(wship,wdata,wa,&got) && got.has_p==in.has_p && (!in.has_p || got.p==in.p) && !memcmp(got.a,in.a,16) && !memcmp(got.d,in.d,20));
        assert(!memcmp(got.dm,in.dm,16) && !memcmp(got.r,in.r,20) && got.t_scale==in.t_scale && got.k==in.k && got.t_clamp==in.t_clamp);
        uint32_t n[4],ours[4];memcpy(n,wslot,16);fr_w_recompute(&got,ours);
        for(unsigned k=0;k<4;k++) if(n[k]!=ours[k]) {printf("fp redirect writer %u field %u: native %08x ours %08x\n",i,k,n[k],ours[k]);assert(0);}
        ++w_equal;if(i%17==0) ++w_k;
    }
    memset(LX(LX_FR_K_TABLE_VA),0,8);
    printf("fp redirect on the executable: writer 0x14811c0 equals ours on %u random inputs (%u all-K) PASS\n",w_equal,w_k);
    /* The patch on these bytes and its exact restoration. */
    unsigned char saved_entry[16],saved_sites[LXF_SITES][16];memcpy(saved_entry,LX(LX_FR_REFRESH_VA),16);
    for(unsigned i=0;i<LXF_SITES;i++) memcpy(saved_sites[i],lxf_site(i)-4,16);
    unsigned char saved_detours[LXF_DETOURS][16];for(unsigned j=0;j<LXF_DETOURS;j++) memcpy(saved_detours[j],LX(lxf_detours[j].at),16);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);fr_id_count=0;fr_installed=0;
    assert(fr_arm(FR_SHADOW,3) && LX(LX_FR_REFRESH_VA)[0]==0xe9 && !memcmp(LX(LX_FR_REFRESH_VA)+5,saved_entry+5,11) && fr_owned());
    for(unsigned j=0;j<LXF_DETOURS;j++) assert(lxf_detour_state(j)==1);
    {   /* The writer's detour run for real: a frame as at 0x148cd53 (r12 o20, [rsp+0x28] D, rdx = r14 A, rbx slot); the
         * stub replays the moves and calls fr_writer_run -> the executable's writer, then 0x148cd63 (here a jmp back). */
        fr_w_in in,got;tfr_w_random(2,&in);
        void *vt=wvt,*t=wtable,*p=in.has_p?(void*)wp:NULL;memcpy(wship,&vt,8);memcpy(wship+0x398,&p,8);memcpy(wp+0x50,&in.p,4);
        for(unsigned k=0;k<5;k++) {wvt[(0x2078u>>3)+k]=(void*)wfns[k];memcpy(&tlw_r[k],&in.r[k],4);}
        memcpy(wa,in.a,16);memcpy(wdata+0x914,in.d,20);memcpy(wdata+0x998,in.dm,16);
        memcpy(wtable+0x4fd0,&in.t_scale,4);memcpy(wtable+0x318,&in.k,4);memcpy(wtable+0x330,&in.t_clamp,4);memcpy(LX(LX_FR_K_TABLE_VA),&t,8);
        uintptr_t page=(uintptr_t)LX(LX_FR_WRITER_AT_VA)&~(uintptr_t)(GATE_PAGE-1);assert(!mprotect((void*)page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
        assert(((uintptr_t)LX(LX_FR_WRITER_BACK_VA)&~(uintptr_t)(GATE_PAGE-1))==page);
        unsigned char back[14]={0xff,0x25,0,0,0,0},saved_back[14];void *to=(void*)test_lxw_back;memcpy(back+6,&to,8);
        memcpy(saved_back,LX(LX_FR_WRITER_BACK_VA),14);memcpy(LX(LX_FR_WRITER_BACK_VA),back,14);
        const LONG64 hits=fr_hits[FR_HIT_WRITER],n=fr_n[FR_W0],diff=fr_diff[FR_W0]+fr_diff[FR_W4]+fr_diff[FR_W8]+fr_diff[FR_WC];
        unsigned char wslot[0x60] __attribute__((aligned(16)));memset(wslot,0,sizeof(wslot));
        test_lxw_run(LX(LX_FR_WRITER_AT_VA),wship,wdata,wa,wslot);
        uint32_t ours[4];assert(fr_w_read(wship,wdata,wa,&got));fr_w_recompute(&got,ours);
        assert(!memcmp(wslot,ours,16) && fr_hits[FR_HIT_WRITER]==hits+1 && fr_n[FR_W0]==n+1);
        assert(fr_diff[FR_W0]+fr_diff[FR_W4]+fr_diff[FR_W8]+fr_diff[FR_WC]==diff);
        /* 'redirect' on Linux: ours is native's own order, nothing is written. */
        const LONG64 written=fr_w_written;fr_mode=FR_REDIRECT;memset(wslot,0,sizeof(wslot));
        test_lxw_run(LX(LX_FR_WRITER_AT_VA),wship,wdata,wa,wslot);
        assert(!memcmp(wslot,ours,16) && fr_w_written==written && fr_hits[FR_HIT_WRITER]==hits+2 && !fr_failures);fr_mode=FR_SHADOW;
        memcpy(LX(LX_FR_WRITER_BACK_VA),saved_back,14);memset(LX(LX_FR_K_TABLE_VA),0,8);
    }
    assert(!memcmp(thunk+LXF_V_STUB,"\x64\xf2\x0f\x10\x04\x25\xd8\xd8\xff\xff\xf3\x0f\x10\x0c\x24\xff\x15",17));
    assert(!memcmp(thunk+LXF_MULPS_STUB,"\x48\x8b\x74\x24\x28\x48\x89\xd9\x4c\x89\xf2\x4c\x89\xe7\xff\x15",16));
    assert(!memcmp(thunk+LXF_WRITER_STUB,"\x48\x8b\x74\x24\x28\x48\x89\xd9\x4c\x89\xe7\xff\x15",13));
    {void *t;memcpy(&t,thunk+LXF_V_STUB+0x38,8);assert(t==(void*)LX(LX_FR_V_BACK_VA));memcpy(&t,thunk+LXF_MULPS_STUB+0x38,8);assert(t==(void*)LX(LX_FR_MULPS_BACK_VA));}
    {void *t;memcpy(&t,thunk+LXF_WRITER_STUB+0x38,8);assert(t==(void*)LX(LX_FR_WRITER_BACK_VA));memcpy(&t,thunk+LXF_WRITER_STUB+0x30,8);assert(t==(void*)fr_writer_run);}
    {void *t;memcpy(&t,thunk+LXF_MULPS_STUB+0x30,8);assert(t==(void*)lxf_mulps_hook);}
    fr_restore();assert(!fr_installed && !memcmp(LX(LX_FR_REFRESH_VA),saved_entry,16));
    for(unsigned i=0;i<LXF_SITES;i++) assert(!memcmp(lxf_site(i)-4,saved_sites[i],16));
    for(unsigned j=0;j<LXF_DETOURS;j++) assert(!memcmp(LX(lxf_detours[j].at),saved_detours[j],16));
    fr_restored=0;fr_stage="idle";
    printf("fp redirect on the executable: sites, frame facts, L %u and M %u equal (%u of them -0), patch/restore PASS\n",l_equal,m_equal,m_negzero);
}
