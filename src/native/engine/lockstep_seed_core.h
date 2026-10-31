/* Fixture RNG schema 2. Each tuple field is mixed separately: XOR-packing actor
 * and weapon slot into the same high bits aliases distinct native streams.
 * This is deterministic stream keying, not a cryptographic random generator. */
#define LOCKSTEP_RNG_SCHEMA 2u
static uint64_t lockstep_mix(uint64_t x) {
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
static uint64_t lockstep_seed(uint64_t domain,uint64_t tick,uint64_t actor,
                              uint64_t stream,uint64_t ordinal,uint64_t detail) {
    uint64_t x=lockstep_mix(0x58344c5345454432ULL^domain);
    x=lockstep_mix(x^tick);x=lockstep_mix(x^actor);x=lockstep_mix(x^stream);
    x=lockstep_mix(x^ordinal);return lockstep_mix(x^detail);
}
