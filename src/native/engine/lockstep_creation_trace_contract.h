/* Exact-image read-only construction observer contract. This header alone does
 * not install a hook, seed RNG, acquire an engine reference or enroll an actor.
 * Windows x64 argument positions below were checked against caller AND callee.
 * Qword pointer slots remain uintptr_t so borrowed opaque references are not
 * dereferenced by the forwarding ABI. Narrow flags keep their native widths. */
#ifndef X4LAN_CREATION_TRACE_CONTRACT_H
#define X4LAN_CREATION_TRACE_CONTRACT_H

typedef uintptr_t (*creation_generate_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    unsigned char,uintptr_t,uintptr_t,unsigned,unsigned char,unsigned char);
typedef void (*creation_initialize_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    uintptr_t,unsigned char,uintptr_t,unsigned,unsigned char);
typedef uintptr_t (*creation_pilot_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    uintptr_t,uintptr_t,uintptr_t);
/* Native slot four is not read, but it occupies R9 before stack flags five/six. */
typedef void (*creation_navigation_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    unsigned char,unsigned char);
typedef uintptr_t (*creation_agent_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef void (*creation_setter_fn)(uintptr_t,uintptr_t,unsigned char);
typedef float (*creation_random_fn)(uint64_t *,float);
typedef unsigned char (*creation_event_query_fn)(uintptr_t,unsigned,double,double,unsigned char);
/* No fifth stack argument is consumed by the complete pinned PostEvent body. */
typedef void (*creation_post_fn)(uintptr_t,uintptr_t,double,unsigned char);

enum creation_kind {
    CREATION_GENERATE=1,CREATION_INITIALIZE,CREATION_PILOT,CREATION_NAVIGATION,
    CREATION_AGENT,CREATION_SETTER,CREATION_RANDOM,CREATION_EVENT_QUERY,CREATION_POST,
    CREATION_RANGE,CREATION_TOKEN,CREATION_BUILDER
};
#define CREATION_CAPACITY 128u
#define CREATION_DEPTH 8u
#define CREATION_THUNK_FIRST 3072u
#define CREATION_THUNK_END 3584u
#define CREATION_HOOK_COUNT 9u

typedef struct {
    unsigned rva,length;
    const char *sha256;
} creation_pin;
static const creation_pin creation_pins[]={
    {0x2cbe008u,4u,"b7d41d79e52b4090852073f63ecbadf7dbc031e0004382988b247b05c125ac72"}, /* FloatState scale 2^-32 */
    /* Full enclosing bodies for observed ancillary RNG/setter callers. */
    {0x4fdfe0u,5668u,"330561122ba3b0f0793aec2383694f483b54d0ebe696aec5340c04b85ec96092"},
    {0x5012f0u,589u,"3d63d65ff92215d2242059703e1e3fd137920c327a4a15997d6361655b43e188"},
    {0x505fb0u,13427u,"c8649473700b5cf6a1e0e9bfe1a9d887512116de7efc8b0f2f02c5279cd28789"},
    {0x50e4a0u,4119u,"bb77fd0b832098a6478588b40f756ee84ab6c6a9af321826c742900ae428761c"},
    {0x51b710u,32u,"924d84d3ca43cfd32ed104d4ea7a7545f9b593bd1ee240f4aa96227aa4250bd5"},
    {0x6bd830u,256u,"37a40a4e25d23a4cb6e93767836171e396e4cee1c2224df8c596f0564ed57e80"},
    {0x6c65c0u,2880u,"5a2f095fea01699a535cdc9b9dfcb51767661815b09c784b47808dc30a086e09"},
    {0x745700u,533u,"c9f9f2b472bd1073b8aba6d14ae9bc5d3c47b30192ba35a2ceaefff9343788e8"},
    {0x746980u,766u,"c0e3af96eb9ff0715dc0180cecf023d4e47e8a6ffe25063791fc87e9fe0772b6"},
    /* Multiple PE unwind ranges form this one helper; include its branches and return. */
    {0x14ddd10u,257u,"1db1ba392a17c5d2bd2cbca21a6d2f7234bbf6e6c726a0ef0d72594c19acf921"},
    {0xba1890u,3012u,"eac00a81382d825a10008bc4f48960df8b77ff6e59b012779142655381bd51c6"},
    {0xc841b0u,1090u,"ed86616b763698739cd1635d6de0e430f804971eb10f63e9c1ae78e856b7cb5e"},
    {0xc82060u,3073u,"fbfd2881e3e95f5225c74c2646046cf2ca3b7f27f9c9f3f77fb3e37becdf78ba"},
    {0xc7d4c0u,674u,"2db9de7694668281ec0229ff0ba597508b6dc6852a07297103b0ebe3589a7966"},
    {0xea8cf0u,1332u,"8d94c7bcd214c10c531b48f7bcd8f3ddb87b49025a437416d0a9123c429a0615"},
    {0xe85360u,1368u,"c565989a8232aecf63974a4ae52a53edcaae07c632a0d8c5cb3e5b540f8f4a7e"},
    {0x745e00u,711u,"acd257ef830505bca734583af1fbb2460f8bb09d2bb07d323cbe4596856343b2"},
    {0x14ddc90u,120u,"584854a7bdbf956043c7756157888674152bd0ffc0a307c00f29537332e8780e"},
    {0x99a210u,468u,"0e3ed2e1d468f72dd886645a3270366cf4eca25fb29f06fb58e0128d53c4dede"},
    {0x99a3f0u,807u,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"},
    {0xeac530u,582u,"7b02058e82f6d8acf92e189f5982eae449af06dc045fcb41be1d182ea374267c"},
    {0x6cd8b0u,39u,"9317be9e33cdf30695ab01f4dfb062d85b836a872a3019c103cf9279c4ff2a32"},
    {0xea4a00u,375u,"c55b28f9e7b1f10a740d2436077b498e29a6940031da6d5d0c04afeeb8fba117"}
};

/* Native call instructions are rechecked on every captured call. Indirect
 * sites are admitted only after their full enclosing body was pinned above;
 * reaching our exact entry wrapper supplies the actual callee identity. */
typedef struct {unsigned caller,site,width;unsigned char bytes[6];} creation_caller_pin;
static const creation_caller_pin creation_ancillary_callers[]={
    {0x4fe58b,0x4fe586,5,{0xe8,0x05,0xf7,0xfd,0x00}},
    {0x501440,0x50143b,5,{0xe8,0x50,0xc8,0xfd,0x00}},
    {0x5061ef,0x5061ea,5,{0xe8,0xa1,0x7a,0xfd,0x00}},
    {0x50e56e,0x50e569,5,{0xe8,0x22,0xf7,0xfc,0x00}},
    {0x50e6d9,0x50e6d4,5,{0xe8,0xb7,0xf5,0xfc,0x00}},
    {0x50ee6c,0x50ee67,5,{0xe8,0x24,0xee,0xfc,0x00}},
    {0x50f2f2,0x50f2ed,5,{0xe8,0x9e,0xe9,0xfc,0x00}},
    {0x51b71e,0x51b719,5,{0xe8,0xe2,0xa6,0x22,0x00}},
    {0x6bd8fd,0x6bd8f8,5,{0xe8,0x93,0x03,0xe2,0x00}},
    {0x6c6e5a,0x6c6e54,6,{0xff,0x90,0xe8,0x19,0x00,0x00}},
    {0x6cd8be,0x6cd8b9,5,{0xe8,0x42,0x85,0x07,0x00}},
    {0x7458c7,0x7458c5,2,{0xff,0xd5}},
    {0x746bb8,0x746bb6,2,{0xff,0xd5}},
    {0x14ddd3a,0x14ddd35,5,{0xe8,0x56,0xff,0xff,0xff}},
};

typedef struct {
    unsigned kind,entry,word,width,thunk,slot_size,prefix,call_offset;
    unsigned expected_caller;
    unsigned char expected[16];
} creation_patch_contract;
/* Aligned native words only. Call patches preserve all bytes except their rel32.
 * FloatState's prefix changes RSP/XMM6 and needs reviewed prefix-only unwind;
 * its 64-byte slot reserves offset+48 for 12 metadata bytes. */
static const creation_patch_contract creation_patch_contracts[]={
    {CREATION_GENERATE,0xc841b0,0xc841b0,8,3072,64,5,0,0xba1b8b,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74}},
    {CREATION_INITIALIZE,0xc82060,0xc82060,8,3136,64,5,0,0xc84531,{0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44}},
    {CREATION_PILOT,0xc7d4c0,0xc7d4c0,8,3200,64,5,0,0xc8224f,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c}},
    {CREATION_NAVIGATION,0xea8cf0,0xea8cf0,8,3264,64,7,0,0xea4b5e,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x55}},
    {CREATION_AGENT,0xe85360,0xe85360,8,3328,64,5,0,0xeac5ff,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c}},
    {CREATION_SETTER,0x745e00,0x745e00,8,3392,64,5,0,0x6cd8be,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c}},
    {CREATION_RANDOM,0x14ddc90,0x14ddc90,8,3456,64,9,0,0,{0x48,0x83,0xec,0x38,0x0f,0x29,0x74,0x24,0x20}},
    {CREATION_EVENT_QUERY,0x99a210,0xea9060,16,3520,32,0,9,0xea906e,
        {0xba,0x0f,0x02,0x00,0x00,0x48,0x8b,0x4f,0x28,0xe8,0xa2,0x11,0xaf,0xff,0x84,0xc0}},
    {CREATION_POST,0x99a3f0,0xea9100,16,3552,32,0,5,0xea910a,
        {0x8b,0xd6,0x49,0x8b,0xcc,0xe8,0xe6,0x12,0xaf,0xff,0x45,0x33,0xe4,0x48,0x8b,0x4f}}
};
_Static_assert(sizeof(creation_patch_contracts)/sizeof(*creation_patch_contracts)==CREATION_HOOK_COUNT,"creation hook inventory");
#endif
