/* Closed fixture manifest and existing schema2 key arithmetic. No memory write,
 * hook installation, player authority or native call is implemented here. */
#ifndef X4LAN_CREATION_RNG_CONTRACT_H
#define X4LAN_CREATION_RNG_CONTRACT_H
#define CREATION_RNG_CAPACITY 2u
#define CREATION_RNG_ACK_SUFFIX "|CREATION_RNG_ACK_1"
#define CREATION_RNG_ACK2_SUFFIX "|CREATION_RNG_ACK_2"
#define CREATION_RNG_MANIFEST_CAP 512u
#define CREATION_RNG_OPTIONS 0x84fffffffffffULL
#define CREATION_RNG_SNAPSHOT "7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8"
#define CREATION_RNG_PLAN "ed86c6c938e57dd0cc15046bf49ed814f0e7b4833241f04010e016179924585c"
#define CREATION_RNG_MACRO "ship_arg_xl_builder_01_a_macro"
#define CREATION_RNG_PREFIX "CREATION_RNG_1;epoch="
#define CREATION_RNG_SUFFIX ";handle=0000000500000003;reserved_tick=5;sequence=3;authority=0;world_seed=276944566;macro=" CREATION_RNG_MACRO ";owner=player;snapshot=" CREATION_RNG_SNAPSHOT ";plan=" CREATION_RNG_PLAN ";policy=AB;schema=1"
#define CREATION_RNG_BOOTSTRAP "SNAPSHOT_BYTES_1|" CREATION_RNG_SNAPSHOT "|SNAPSHOT_FIXTURE_1|0000000000176a13,0000000000176a74,0000000000176ad5,0000000000176b36,0000000000176b97,0000000000176bf8,0000000000176c59,0000000000176c77,0000000000176c95,0000000000176cb3,0000000000176cd1,0000000000176cef|"
static uint64_t creation_rng_fnv(const char *text) {
    uint64_t value=14695981039346656037ULL;
    for(;*text;text++){value^=(unsigned char)*text;value*=1099511628211ULL;}return value;
}
static uint64_t creation_rng_sha_word(const char *text) {
    uint64_t value=0;
    for(unsigned i=0;i<16;i++) {unsigned char c=(unsigned char)text[i];value=(value<<4)|(c<='9'?c-'0':c-'a'+10);}
    return value;
}
static uint64_t creation_rng_key(uint64_t domain,uint64_t epoch,uint64_t reserved_tick,uint64_t handle,
        uint64_t sequence,uint64_t world_seed,const char *macro,const char *owner,const char *snapshot,const char *plan) {
    uint64_t value=lockstep_seed(domain,reserved_tick,handle,epoch,sequence,1);
    value=lockstep_mix(value^world_seed);value=lockstep_mix(value^creation_rng_fnv(macro));
    value=lockstep_mix(value^creation_rng_fnv(owner));
    for(unsigned i=0;i<4;i++)value=lockstep_mix(value^creation_rng_sha_word(snapshot+i*16));
    for(unsigned i=0;i<4;i++)value=lockstep_mix(value^creation_rng_sha_word(plan+i*16));
    return value;
}
static int creation_rng_parse(const char *text,size_t length,uint32_t *epoch) {
    const size_t prefix=sizeof(CREATION_RNG_PREFIX)-1,suffix=sizeof(CREATION_RNG_SUFFIX)-1;
    if(!text || !epoch || length>=CREATION_RNG_MANIFEST_CAP || length<=prefix+suffix ||
       memcmp(text,CREATION_RNG_PREFIX,prefix) || memcmp(text+length-suffix,CREATION_RNG_SUFFIX,suffix))return 0;
    size_t digits=length-prefix-suffix;if(digits>10 || text[prefix]<'1' || text[prefix]>'9')return 0;
    uint64_t value=0;
    for(size_t i=0;i<digits;i++) {char c=text[prefix+i];if(c<'0'||c>'9')return 0;value=value*10+(unsigned)(c-'0');}
    if(value>UINT32_MAX)return 0;*epoch=(uint32_t)value;return 1;
}
static unsigned creation_rng_preimage_mode(const char *text,size_t length,const char *manifest,size_t manifest_length) {
    if(!text || !manifest || length!=sizeof(CREATION_RNG_BOOTSTRAP)-1+manifest_length+sizeof(CREATION_RNG_ACK_SUFFIX)-1 ||
       memcmp(text,CREATION_RNG_BOOTSTRAP,sizeof(CREATION_RNG_BOOTSTRAP)-1) ||
       memcmp(text+sizeof(CREATION_RNG_BOOTSTRAP)-1,manifest,manifest_length))return 0;
    const char *suffix=text+sizeof(CREATION_RNG_BOOTSTRAP)-1+manifest_length;
    if(!memcmp(suffix,CREATION_RNG_ACK_SUFFIX,sizeof(CREATION_RNG_ACK_SUFFIX)-1))return 1;
    if(!memcmp(suffix,CREATION_RNG_ACK2_SUFFIX,sizeof(CREATION_RNG_ACK2_SUFFIX)-1))return 2;
    return 0;
}
/* Preserve the legacy helper's exact mode1 contract for old callers/tests. */
static int creation_rng_preimage(const char *text,size_t length,const char *manifest,size_t manifest_length) {
    return creation_rng_preimage_mode(text,length,manifest,manifest_length)==1;
}
#endif
