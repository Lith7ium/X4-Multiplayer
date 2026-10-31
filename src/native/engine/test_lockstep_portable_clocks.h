/* Exact pinned image, with only owned private words changed for the test. */
static void test_portable_clock_image(void) {
    uint64_t native_word,native_dt;unsigned short native_branch;unsigned char native_disabled;
    memcpy(&native_word,image+WORD_RVA,8);memcpy(&native_dt,image+0x6adfc60,8);
    memcpy(&native_branch,image+0xfa3302,2);memcpy(&native_disabled,image+0x6adfc58,1);
    int prior_installed=installed,prior_dt=dt_installed,prior_restored=dt_restored;
    uint64_t prior_patch=patched_word,prior_fixed=fixed_dt_bits;
    double fixed=1.0/60.0;memcpy(&fixed_dt_bits,&fixed,8);
    installed=dt_installed=1;dt_restored=0;patched_word=native_word;
    *(unsigned short*)(image+0xfa3302)=0x9066;*(uint64_t*)(image+0x6adfc60)=fixed_dt_bits;image[0x6adfc58]=0;
    assert(portable_native_clock_pins());
    const unsigned sites[]={0x4b5e50+56,0xfa335e + 7,TICK_RVA+2126};
    for(unsigned i=0;i<sizeof(sites)/sizeof(*sites);i++) {
        image[sites[i]]^=1;assert(!portable_native_clock_pins());image[sites[i]]^=1;assert(portable_native_clock_pins());
    }
    *(unsigned short*)(image+0xfa3302)=0x0874;assert(!portable_native_clock_pins());*(unsigned short*)(image+0xfa3302)=0x9066;
    dt_installed=0;assert(!portable_native_clock_pins());dt_installed=1;
    dt_restored=1;assert(!portable_native_clock_pins());dt_restored=0;
    *(uint64_t*)(image+WORD_RVA)^=1;assert(!portable_native_clock_pins());*(uint64_t*)(image+WORD_RVA)^=1;
    *(uint64_t*)(image+0x6adfc60)^=1;assert(!portable_native_clock_pins());*(uint64_t*)(image+0x6adfc60)^=1;
    image[0x6adfc58]=1;assert(!portable_native_clock_pins());image[0x6adfc58]=0;assert(portable_native_clock_pins());
    memcpy(image+WORD_RVA,&native_word,8);memcpy(image+0x6adfc60,&native_dt,8);
    memcpy(image+0xfa3302,&native_branch,2);memcpy(image+0x6adfc58,&native_disabled,1);
    installed=prior_installed;dt_installed=prior_dt;dt_restored=prior_restored;patched_word=prior_patch;fixed_dt_bits=prior_fixed;
    puts("portable clock reader/writer full pins and exact owned fixed-dt reconstruction verified");
}
