/* lockstep_fp_redirect.h's strict recomputation against fixed bits, the same table on both platforms (Windows
 * test_lockstep_gate.c, Linux linux/test_gate_linux.c): the C source must give these bits under zig/clang and gcc.
 * The expected bits come from an independent single-precision model (each operation rounded to float, Linux's
 * order and its min/max/compare semantics), not from this code. Inputs: comp[A], comp[B], G4, G0, w; outputs
 * +0x0, +0x4, +0x24. Cases: interpolation (1, 2, 3, 8), x <= x0 (4: a), x >= x1 (5: b), |x0-x1| < eps (6), w NaN (7).
 * M: flag, a, b, c -> M; incl. |d| < 1e-4 (5), r < 0 (4), c == a with d < 0 (6: -0, where Windows' maxss gives +0). */
static const struct {uint32_t ca,cb,g4,g0,w,out[3];} tfr_vectors[]={
    {0x3b23d70a,0x3b83126f,0x43c9859b,0x4383fc81,0x43960000, {0x468a2c56,0x43c9859b,0x43c80000}},
    {0x3b23d70a,0x3b83126f,0x43c9859b,0x4383fc81,0x43820fbe, {0x46c0733d,0x43c9859b,0x43c80000}},
    {0x3b4b295f,0x3b9a0275,0x41480000,0x40e80000,0x437a0000, {0x43f0d1a5,0x41480000,0x43a14a52}},
    {0x3b4b295f,0x3b9a0275,0x41480000,0x40e80000,0x42c80000, {0x44354000,0x41480000,0x43a14a52}},
    {0x3b4b295f,0x3b9a0275,0x41480000,0x40e80000,0x43c80000, {0x40e80000,0x41480000,0x43a14a52}},
    {0x3ba3d70a,0x3ba3d70a,0x3f800000,0x40400000,0x43480000, {0x43178000,0x3f800000,0x43480000}},
    {0x3b03126f,0x3ba3d7e1,0x411c0000,0x3fc00000,0x7fc00000, {0x7fc00000,0x411c0000,0x43f9ffff}},
    {0x3c4985f0,0x3d3ac711,0x3eaa7efa,0x3f46e979,0x425e0000, {0x4208d250,0x3eaa7efa,0x42a29a05}},
};
static const struct {unsigned flag;uint32_t a,b,c,out;} tfr_m_vectors[]={
    {1,0x43820000,0x43c80000,0x43960000,0x3f124925},
    {0,0x43820000,0x43c80000,0x43fa0000,0x405b6db7},
    {1,0x43820000,0x43c80000,0x43fa0000,0x3f800000},
    {1,0x43c80000,0x43820000,0x43fa0000,0x00000000},
    {0,0x40a00000,0x40a00015,0x41100000,0x3f800000},
    {1,0x40e00000,0x40400000,0x40e00000,0x80000000},
    {0,0x3dcccccd,0x3f333333,0x3e99999a,0x3f2aaaac},
    {1,0x42f6e979,0x3a83126f,0xc2480000,0x3f800000},
};
/* The writer: {a[4], D 914..924, multipliers 998..9a4, virtuals 2078..2098, scale, K, clamp, p, has_p}, Linux's bits,
 * Windows' bits (independent float32 model of both orders). */
static const struct {fr_w_in in;uint32_t linux_form[4],windows_form[4];} tfr_w_vectors[]={
    {{{0x42c80000,0x42480000,0x41a00000,0x41200000},{0x3f800000,0x3f800000,0x3f800000,0x3f800000,0x3f800000},{0x3f800000,0x3f800000,0x3f800000,0x3f800000},
      {0x3f800000,0x3f800000,0x3f800000,0x3f800000,0x00000000},0x3f800000,0x00000000,0x3f800000,0x3f800000,0},
     {0x42055555,0x41855555,0x40d55555,0x40555555},{0x42055556,0x41855556,0x40d55556,0x40555556}},
    {{{0x4383fc29,0x43c9851f,0x427947ae,0x427947ae},{0x3eb33333,0x3fa66666,0x3f666666,0x3e99999a,0x3fd9999a},{0x3f800000,0x3f800000,0x3f800000,0x3f800000},
      {0x3f800000,0x3e4ccccd,0x00000000,0x3f800000,0x00000000},0x3f800000,0x00000000,0x3f800000,0x3f333333,1},
     {0x441bfb75,0x4424e147,0x41be5bfa,0x4306d67b},{0x441bfb75,0x4424e147,0x41be5bfa,0x4306d67c}},
    {{{0x3851b717,0xb851b717,0x00000000,0x38bcbe62},{0x3f800000,0x3f800000,0x3f800000,0x3f800000,0x3f800000},{0x40000000,0x40000000,0x40000000,0x40000000},
      {0x3f800000,0x3f800000,0x3f800000,0x3f800000,0x00000000},0x3f800000,0x40200000,0x3f000000,0x3f800000,0},
     {0x40a00000,0x40a00000,0x40200000,0x40200000},{0x40a00000,0x40a00000,0x40200000,0x40200000}},
    {{{0x41f00000,0x442f0000,0x44610000,0x44480000},{0x3f0ccccd,0x3f800000,0x3f800000,0x3f800000,0x3f800000},{0x3f800000,0x3f800000,0x3f800000,0x3f800000},
      {0x3f800000,0x3ecccccd,0x00000000,0x3f800000,0x00000000},0x3f800000,0x3f800000,0x3dcccccd,0x3f800000,0},
     {0x41fca1ae,0x443835e4,0x404a1af2,0x404a1af2},{0x41fca1ae,0x443835e4,0x404a1af2,0x404a1af2}},
    {{{0xc1480000,0x40400000,0x40800000,0x40a00000},{0x40000000,0x3e800000,0x40800000,0x3f800000,0x3f800000},{0x3f8ccccd,0x3f666666,0x3f800000,0x3fa66666},
      {0x3f400000,0x41100000,0xbf800000,0x3f800000,0x00000000},0x3e99999a,0x00000000,0x40000000,0x3ff33333,1},
     {0xc0480001,0x411d1747,0x00000000,0x00000000},{0xc0480002,0x411d1747,0x00000000,0x00000000}},
};
/* Windows 0x6db200's order (read at 0x6db3d2..0x6db469): inv = 1.0/den; q0 = (inv*a0)*D; q1 = (inv*a1)*D;
 * q2 = (p*D)*(inv*a2); q3 = (p*D)*(inv*a3), p = 1.0 without the pointer; the rest as Linux. Test only. */
static void tfr_w_windows(const fr_w_in *in,uint32_t out[4]) {
    const float eps=fr_f32(FR_M_EPS),k=fr_f32(in->k);float q[4];
    if(eps>fr_abs(fr_f32(in->a[0])) && eps>fr_abs(fr_f32(in->a[1])) && eps>fr_abs(fr_f32(in->a[2])) && eps>fr_abs(fr_f32(in->a[3]))) {
        q[0]=q[1]=q[2]=q[3]=k;
    } else {
        const float d0=fr_f32(in->d[0]),s=d0*fr_f32(in->t_scale),r80=fr_f32(in->r[1]),r88=fr_f32(in->r[2]),p=fr_f32(in->has_p?in->p:FR_ONE);
        float den=d0*fr_f32(in->r[0]);den=den+(s<r80?s:r80);den=den+(s<r88?s:r88);
        const float inv=fr_f32(FR_ONE)/den;float t;
        t=inv*fr_f32(in->a[0]);q[0]=t*fr_f32(in->d[1]);
        t=inv*fr_f32(in->a[1]);q[1]=t*fr_f32(in->d[2]);
        t=inv*fr_f32(in->a[2]);q[2]=p*fr_f32(in->d[3]);q[2]=q[2]*t;
        t=inv*fr_f32(in->a[3]);q[3]=p*fr_f32(in->d[4]);q[3]=q[3]*t;
    }
    for(unsigned i=0;i<4;i++) q[i]=q[i]*fr_f32(in->dm[i]);
    float x=q[0]*fr_f32(in->t_clamp);x=x>k?x:k;
    q[2]=x<q[2]?x:q[2];q[3]=x<q[3]?x:q[3];
    for(unsigned i=0;i<4;i++) out[i]=fr_bits(q[i]);
}
static void test_fr_vectors(void) {
    for(unsigned i=0;i<sizeof(tfr_vectors)/sizeof(*tfr_vectors);i++) {
        uint32_t out[3];fr_recompute(tfr_vectors[i].ca,tfr_vectors[i].cb,tfr_vectors[i].g4,tfr_vectors[i].g0,tfr_vectors[i].w,out);
        for(unsigned k=0;k<3;k++) if(out[k]!=tfr_vectors[i].out[k]) {printf("fp redirect vector %u field %u: %08x, want %08x\n",i,k,out[k],tfr_vectors[i].out[k]);assert(0);}
    }
    for(unsigned i=0;i<sizeof(tfr_m_vectors)/sizeof(*tfr_m_vectors);i++) {
        const uint32_t m=fr_bits(fr_m(tfr_m_vectors[i].flag,fr_f32(tfr_m_vectors[i].a),fr_f32(tfr_m_vectors[i].b),fr_f32(tfr_m_vectors[i].c)));
        if(m!=tfr_m_vectors[i].out) {printf("fp redirect M vector %u: %08x, want %08x\n",i,m,tfr_m_vectors[i].out);assert(0);}
    }
    /* The first call's +0x0..+0xc: max(max(K,|v|)/d, cur). Case 2 is 100/3, where Windows' max(K,|v|)*(1/d) gives
     * 0x42055556 (an independent model of both forms). */
    static const uint32_t pv[4][5]={{0x3f800000,0x43fa0000,0x3fc00000,0,0x43a6aaab},{0x41200000,0xc2c80000,0x40400000,0,0x42055555},
        {0x41200000,0x3f000000,0x3dcccccd,0x43c80000,0x43c80000},{0x40a00000,0x44000000,0x3f8ccccd,0,0x43e8ba2e}};
    for(unsigned i=0;i<4;i++) assert(fr_p_new(pv[i][0],pv[i][1],pv[i][2],pv[i][3])==pv[i][4]);
    {const float n=fr_f32(0x42c80000u)*(1.0f/fr_f32(0x40400000u));assert(fr_bits(n)==0x42055556u);} /* Windows' form */
    /* The writer's +0x0..+0xc (fr_w_recompute, Linux's order) and Windows' order (tfr_w_windows, test only), both
     * against the independent model. Case 0: den = 3, a = 100, 50, 20, 10: all four split by one ulp (100/3 is
     * 0x42055555, (1/3)*100 0x42055556). 1: p present, +0xc splits by the reassociation alone. 2: all |a| < 1e-4 (K).
     * 3: +0x8/+0xc clamped by x = +0x0 * clamp. 4: p present, a negative +0x0 splits; +0x8/+0xc clamped to K = 0. */
    for(unsigned i=0;i<sizeof(tfr_w_vectors)/sizeof(*tfr_w_vectors);i++) {
        uint32_t ours[4],win[4];fr_w_recompute(&tfr_w_vectors[i].in,ours);tfr_w_windows(&tfr_w_vectors[i].in,win);
        for(unsigned k=0;k<4;k++) {
            if(ours[k]!=tfr_w_vectors[i].linux_form[k]) {printf("fp redirect writer vector %u field %u: %08x, want %08x\n",i,k,ours[k],tfr_w_vectors[i].linux_form[k]);assert(0);}
            if(win[k]!=tfr_w_vectors[i].windows_form[k]) {printf("fp redirect writer vector %u Windows form field %u: %08x, want %08x\n",i,k,win[k],tfr_w_vectors[i].windows_form[k]);assert(0);}
        }
    }
    assert(fr_lua_mode("shadow",6)==FR_SHADOW && fr_lua_mode("redirect",8)==FR_REDIRECT && fr_lua_mode("redirec",7)<0 && fr_lua_mode(NULL,0)<0);
    /* ulp distance: neighbours, across zero, NaN. */
    assert(fr_ulp(0x3f800000u,0x3f800001u)==1 && fr_ulp(0x80000001u,0x00000001u)==2 && fr_ulp(0x80000000u,0u)==0);
    assert(fr_ulp(0x7fc00000u,0x3f800000u)==0xffffffffu && fr_ulp(0x7fc00000u,0x7fc00000u)==0);
    printf("fp redirect strict vectors PASS\n");
}
/* A small deterministic generator for the native-code comparisons (xorshift64*). */
static uint64_t tfr_state=0x9e3779b97f4a7c15ull;
static uint32_t tfr_next(void) {tfr_state^=tfr_state>>12;tfr_state^=tfr_state<<25;tfr_state^=tfr_state>>27;return (uint32_t)((tfr_state*0x2545f4914f6cdd1dull)>>32);}
/* A float in [lo, hi) with all mantissa bits in play. */
static float tfr_range(float lo,float hi) {return lo+(hi-lo)*(float)(tfr_next()>>8)*(1.0f/16777216.0f);}
/* Random writer inputs for the runs of the executables' own writers: every 17th all |a| < 1e-4 (K), a third with
 * r80 able to undercut s, a quarter with r88 != 0, half with p present. */
static void tfr_w_random(unsigned i,fr_w_in *in) {
    for(unsigned k=0;k<4;k++) in->a[k]=fr_bits(i%17==0?tfr_range(-9e-5f,9e-5f):tfr_range(-600.0f,600.0f));
    in->d[0]=fr_bits(tfr_range(0.05f,50.0f));for(unsigned k=1;k<5;k++) in->d[k]=fr_bits(tfr_range(0.1f,5.0f));
    for(unsigned k=0;k<4;k++) in->dm[k]=fr_bits(tfr_range(0.5f,2.0f));
    in->r[0]=fr_bits(tfr_range(0.5f,2.0f));in->r[1]=fr_bits(i%3==0?tfr_range(-5.0f,100.0f):tfr_range(0.0f,2.0f));
    in->r[2]=fr_bits(i%4==0?tfr_range(-5.0f,100.0f):0.0f);in->r[3]=fr_bits(tfr_range(0.5f,2.0f));in->r[4]=fr_bits(tfr_range(0.0f,1.0f));
    in->t_scale=fr_bits(tfr_range(0.1f,3.0f));in->k=fr_bits(tfr_range(0.0f,50.0f));in->t_clamp=fr_bits(tfr_range(0.01f,2.0f));
    in->has_p=i&1u;in->p=fr_bits(tfr_range(0.1f,3.0f));
}
