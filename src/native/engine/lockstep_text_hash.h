static uint64_t world_wait_rng_hash(const char *text) {
    uint64_t value=14695981039346656037ULL;
    for(;*text;text++) value=(value^(unsigned char)*text)*1099511628211ULL;
    return value;
}
