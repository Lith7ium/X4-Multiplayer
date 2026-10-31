/* Generator steps of the engine PCG (Random::U64State) from seed to exit. */
static uint32_t script_rng_steps(uint64_t from,uint64_t to) {
    for(uint32_t n=0;n<=SCRIPT_RNG_STEP_CAP;n++) {
        if(from==to) return n;
        from=from*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;from=(from>>30)|(from<<34);
    }
    return UINT32_MAX;
}
