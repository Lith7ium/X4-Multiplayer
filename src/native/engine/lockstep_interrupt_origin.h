/* Shared build-local IRQ call provenance. An image address is not by itself a known
 * origin; only these pinned direct calls may receive a normalized identity. */
typedef struct {uint32_t caller;unsigned char call[5];} interrupt_callsite;
static const interrupt_callsite interrupt_callsites[]={
    {0x10662ef,{0xe8,0xf1,0x29,0xaf,0xff}},
    {0x1066d8d,{0xe8,0x53,0x1f,0xaf,0xff}},
    {0x1066dc6,{0xe8,0x1a,0x1f,0xaf,0xff}},
    {0x106b082,{0xe8,0x5e,0xdc,0xae,0xff}},
    {0x106d8db,{0xe8,0x05,0xb4,0xae,0xff}},
    {0x106e2f4,{0xe8,0xec,0xa9,0xae,0xff}},
    {0x106e34f,{0xe8,0x91,0xa9,0xae,0xff}},
    {0x106fef1,{0xe8,0xef,0x8d,0xae,0xff}},
    {0x10a7996,{0xe8,0x4a,0x13,0xab,0xff}},
};
typedef struct {uint64_t raw,caller;unsigned source;} interrupt_origin;
static int interrupt_origin_ready;
static int interrupt_origin_owned(void) {
    unsigned __int128 ai_word=0;uint64_t irq_word=0;
    return ai_installed && interrupt_installed && read_local(image+AI_WORD,&ai_word,16) && ai_word==ai_patched_word &&
        read_local(image+INTERRUPT_ENTRY,&irq_word,8) && irq_word==interrupt_patched_word;
}
static int interrupt_origin_pinned(void) {
    unsigned char director[5134],irq[743];
    if(!interrupt_origin_owned() || !read_local(image+0x106d250,director,sizeof(director)) ||
       !read_local(image+INTERRUPT_ENTRY,irq,sizeof(irq))) return 0;
    /* Only exact patches owned by this gate may be reconstructed for hashing. */
    memcpy(director+AI_WORD-0x106d250,&ai_original_word,16);memcpy(irq,&interrupt_original_word,8);
    if(!hash_ok(director,sizeof(director),"360cdc20473b7acde457f95c71540da76295a706b8e7509d9b8466a19e706018") ||
       !hash_ok(irq,sizeof(irq),"4ceef100cc2a6436c60f61363cfd00b94fb43caf4d9285a95bff8b6aa3147cd3") ||
       !hash_ok(image+0x10661b0,742,"eff3e06f02e6ccad956ee15fef010d3715582fa91cb765850e0fb8dba6fc5f53") ||
       !hash_ok(image+0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434") ||
       !hash_ok(image+0x106afb0,460,"fd0ae7e962d43c2ef060800ecd6e15dc449bdcc687a7fd2e597c5e02f9ef2091") ||
       !hash_ok(image+0x106fd90,639,"ce77f18afebac5c16a182b5306fcca94255fa7f4a8e2b8c1b43392ebbcb57849") ||
       !hash_ok(image+0x10a7930,464,"7b310ca736662b45f51e00ec22a67756000f5d64f4f313e432047800fcdb7e06")) return 0;
    for(unsigned i=0;i<sizeof(interrupt_callsites)/sizeof(*interrupt_callsites);i++) {
        unsigned char bytes[5];const interrupt_callsite *site=&interrupt_callsites[i];
        if(!read_local(image+site->caller-5,bytes,5) || memcmp(bytes,site->call,5)) return 0;
    }
    return interrupt_origin_owned();
}
/* Consumers activate this independently after the existing AI/IRQ hooks are
 * installed. A failed later check revokes all shared normalization authority. */
static __attribute__((unused)) int interrupt_origin_prepare(void) {interrupt_origin_ready=interrupt_origin_pinned();return interrupt_origin_ready;}
static __attribute__((unused)) interrupt_origin interrupt_origin_read(uintptr_t address) {
    interrupt_origin result={0,0,3}; /* Outside-image/unverified; never infer an owned adapter. */
    if(address<(uintptr_t)image || address>=(uintptr_t)image+0x6fa9000) return result;
    result.raw=address-(uintptr_t)image;result.source=2; /* Unknown image call. */
    for(unsigned i=0;i<sizeof(interrupt_callsites)/sizeof(*interrupt_callsites);i++) {
        const interrupt_callsite *site=&interrupt_callsites[i];if(result.raw!=site->caller) continue;
        unsigned char bytes[5];result.source=4; /* Known site, unverified or changed ownership/bytes. */
        if(interrupt_origin_ready && interrupt_origin_owned() && read_local(image+site->caller-5,bytes,5) && !memcmp(bytes,site->call,5))
            {result.caller=site->caller;result.source=1;}
        break;
    }
    return result;
}
