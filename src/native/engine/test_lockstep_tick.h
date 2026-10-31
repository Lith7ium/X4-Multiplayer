/* native_gate_tick_hz: configuration, the pace schedule and a real hold; with X4.exe: the
 * patched dt word and the clock branch at another rate, restored. */
static void test_tick(void) {
    unsigned saved_hz=gate_tick_hz;int saved_pace=gate_tick_pace;
    assert(gate_tick_hz==GATE_TICK_HZ_DEFAULT && gate_tick_dt()==1.0/60.0 && !gate_tick_pace);
    assert(!gate_tick_configure(25,1,0) && !gate_tick_configure(30.5,1,0) && !gate_tick_configure(30,2,0) && !gate_tick_configure(NAN,1,0) && gate_tick_hz==60);
    for(unsigned i=0;i<4;i++) {
        static const unsigned rates[4]={60,40,30,20};
        assert(gate_tick_configure(rates[i],1,0) && gate_tick_hz==rates[i] && gate_tick_pace==1 && gate_tick_dt()==1.0/(double)rates[i]);
    }
    assert(gate_tick_configure(30,1,0) && gate_tick_configure(30,1,1) && !gate_tick_configure(60,1,1) && !gate_tick_configure(30,0,1) && gate_tick_hz==30);
    /* The throttle ramp and the prediction step follow the rate. */
    assert(PILOT_THROTTLE_STEP==1.0f/30.0f && PREDICT_DT==1.0/30.0);
    /* Schedule: origin, then n/hz; a late frame runs at once and the next one catches up; too late re-bases. */
    gate_pace_state p={0};const int64_t f=10000000;
    assert(!gate_pace_deadline(&p,5000,f,30) && p.origin==5000);
    assert(gate_pace_deadline(&p,6000,f,30)==5000+f/30);
    assert(!gate_pace_deadline(&p,5000+f*3/30,f,30) && p.count==2);                /* 33 ms late: no hold */
    assert(gate_pace_deadline(&p,5000+f*3/30,f,30)==5000+f*3/30 || p.count==3);
    assert(!gate_pace_deadline(&p,5000+f*10,f,30) && p.rebases==1 && p.origin==5000+f*10 && !p.count);
    /* A real hold: 7 frames at 60 Hz take at least 6/60 s, each frame cheap. */
    assert(gate_tick_configure(60,1,0));memset(&gate_pace,0,sizeof(gate_pace));gate_pace_sum_us=0;
    LARGE_INTEGER a,b,q;QueryPerformanceFrequency(&q);QueryPerformanceCounter(&a);
    for(unsigned i=0;i<7;i++) gate_pace_wait();
    QueryPerformanceCounter(&b);
    double seconds=(double)(b.QuadPart-a.QuadPart)/(double)q.QuadPart;
    assert(seconds>=6.0/60.0-0.001 && seconds<1.0 && gate_pace_sum_us>50000);
    /* Off: nothing held. */
    gate_tick_pace=0;gate_pace_wait();assert(!gate_pace_us);
    memset(&gate_pace,0,sizeof(gate_pace));gate_pace_sum_us=0;gate_tick_hz=saved_hz;gate_tick_pace=saved_pace;
    printf("tick rate configuration, pace schedule and hold PASS (%.1f ms for 6 frames at 60 Hz)\n",seconds*1000.0);
}
static void test_tick_native_image(const char *path) {
    unsigned char *prior=image;int saved_installed=dt_installed,saved_restored=dt_restored;
    uint64_t saved_fixed=fixed_dt_bits,saved_saved=saved_dt_bits;unsigned saved_hz=gate_tick_hz;
    image=test_pmath_map(path);
    double clamp=0.5;memcpy(image+0x6adfc60,&clamp,8);image[0x6adfc58]=0;dt_installed=0;dt_restored=0;
    static const unsigned rates[4]={30,20,40,60};
    for(unsigned i=0;i<4;i++) {
        assert(gate_tick_configure(rates[i],0,0));
        assert(install_dt() && dt_installed && *(unsigned short*)(image+0xfa3302)==0x9066 && *(double*)(image+0x6adfc60)==1.0/(double)rates[i]);
        double fixed;memcpy(&fixed,&fixed_dt_bits,8);assert(fixed==1.0/(double)rates[i]);
        restore_dt();assert(dt_restored && !dt_installed && *(unsigned short*)(image+0xfa3302)==0x0874 && *(double*)(image+0x6adfc60)==0.5);
    }
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    dt_installed=saved_installed;dt_restored=saved_restored;fixed_dt_bits=saved_fixed;saved_dt_bits=saved_saved;gate_tick_hz=saved_hz;gate_tick_pace=0;
    puts("tick rate on X4.exe: dt word and clock branch at 30, 20, 40 and 60 Hz, each restored");
}
