/* Owned synthetic memory only. Exercises the actual Windows decoder; never
 * launches X4, patches executable memory or invokes a native lazy getter. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static unsigned char object[0x400],parent[0x80],macro[0x80],faction[0x80],nav[0x80],agent[0x1000];
static void *lookup(void *map,uint64_t id,unsigned flags) {
    assert(map==object && flags==4);
    return id==100?object:id==200?parent:NULL;
}
static void string_set(unsigned char *storage,const char *value) {
    size_t length=strlen(value);memset(storage,0,32);
    if(length<16) memcpy(storage,value,length+1);
    else *(const char**)storage=value;
    *(uint64_t*)(storage+16)=length;*(uint64_t*)(storage+24)=length<16?15:length;
}
static int actor_read(portable_actor *out,world_bank_phase phase) {
    memset(out,0,sizeof(*out));return portable_native_actor(0,lookup,object,&phase,out);
}
int main(void) {
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    portable_native_inverse_ids=VirtualAlloc(NULL,sizeof(*portable_native_inverse_ids)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && portable_native_inverse_ids);
    assert(world_identity_record(100,1000) && world_identity_record(200,2000));
    assert(portable_native_inverse_record(1000,100) && portable_native_inverse_record(2000,200));
    assert(portable_native_inverse_record(1000,100) && !portable_native_inverse_record(1000,101));
    portable_native_ids[0]=(portable_native_identity){1000,100,object,1,1};
    *(void**)object=image+0x2b7feb8;*(uint64_t*)(object+8)=100;*(void**)(object+0x60)=macro;
    *(void**)(object+0x70)=parent;*(void**)(object+0x200)=nav;*(double*)(object+0x280)=100.0;
    *(void**)(object+0x348)=faction;*(uint64_t*)(parent+8)=200;
    *(void**)macro=image+0x2ba5430;*(void**)(image+0x2ba5430+18*8)=image+0xefb10;
    *(void**)(image+0x2b7feb8+0x1620)=image+0x352b10;
    string_set(macro+0x20,"ship_arg_s_fighter_01_a_macro");string_set(faction+0x10,"argon");
    *(void**)nav=image+0x2c51158;*(void**)(image+0x2c51158+0x30)=image+0xea5a70;
    *(void**)(nav+0x28)=object;*(void**)(nav+0x30)=agent+0xad0;*(void**)(nav+0x40)=agent;
    *(void**)(agent+0xef0)=object;
    for(unsigned bank=0;bank<2;bank++) {
        unsigned char *state=agent+0xad0+bank*0x1a0;
        for(unsigned vector=0;vector<4;vector++) {
            for(unsigned xyz=0;xyz<3;xyz++) *(float*)(state+vector*16+xyz*4)=(float)(bank*100+vector*10+xyz);
            *(uint32_t*)(state+vector*16+12)=0xdeadbeefu;
        }
        *(uint64_t*)(state+0xb0)=200;*(uint64_t*)(state+0xb8)=100;
    }
    unsigned char before[sizeof(object)+sizeof(parent)+sizeof(macro)+sizeof(faction)+sizeof(nav)+sizeof(agent)];
    unsigned char *sources[]={object,parent,macro,faction,nav,agent};
    size_t sizes[]={sizeof(object),sizeof(parent),sizeof(macro),sizeof(faction),sizeof(nav),sizeof(agent)},offset=0;
    for(unsigned i=0;i<6;i++) {memcpy(before+offset,sources[i],sizes[i]);offset+=sizes[i];}
    portable_actor a,b;world_bank_phase phase={0,1};
    assert(actor_read(&a,phase) && a.saved==1000 && a.parent==2000 && a.active.refs[0]==2000 && a.active.refs[1]==1000);
    assert(a.hull==UINT64_C(0x4059000000000000) && a.active.transform[0]==0 && a.other.transform[0]==0x42c80000u);
    unsigned char encoded[512];assert(portable_encode_actor(&a,encoded,sizeof(encoded))>0);
    offset=0;for(unsigned i=0;i<6;i++) {assert(!memcmp(before+offset,sources[i],sizes[i]));offset+=sizes[i];}
    assert(actor_read(&b,(world_bank_phase){1,0}) && !memcmp(&a.active,&b.other,sizeof(a.active)) && !memcmp(&a.other,&b.active,sizeof(a.other)));
    /* Vector padding and unsupported velocity-like lanes never enter v1. */
    *(uint32_t*)(agent+0xad0+12)=123;*(uint64_t*)(agent+0xad0+0x40)=UINT64_MAX;
    assert(actor_read(&b,phase) && !memcmp(&a,&b,sizeof(a)));
    *(uint64_t*)(object+8)=101;assert(!actor_read(&b,phase));*(uint64_t*)(object+8)=100;
    *(uint64_t*)(parent+8)=201;assert(!actor_read(&b,phase));*(uint64_t*)(parent+8)=200;
    *(uint64_t*)(agent+0xad0+0xb0)=999;assert(!actor_read(&b,phase));*(uint64_t*)(agent+0xad0+0xb0)=200;
    *(void**)(nav+0x28)=parent;assert(!actor_read(&b,phase));*(void**)(nav+0x28)=object;
    *(void**)(agent+0xef0)=parent;assert(!actor_read(&b,phase));*(void**)(agent+0xef0)=object;
    *(void**)(image+0x2c51158+0x30)=image+0xea5a71;assert(!actor_read(&b,phase));*(void**)(image+0x2c51158+0x30)=image+0xea5a70;
    *(uint64_t*)(object+0x280)=UINT64_C(0x7ff0000000000000);assert(!actor_read(&b,phase));*(double*)(object+0x280)=100.0;
    string_set(faction+0x10,"xenon");assert(!actor_read(&b,phase));string_set(faction+0x10,"argon");
    faction[0x12]=0;assert(!actor_read(&b,phase));string_set(faction+0x10,"argon");
    *(uint64_t*)(faction+0x20)=64;assert(!actor_read(&b,phase));string_set(faction+0x10,"argon");
    /* Alias introduced elsewhere in the snapshot is rejected before sampling. */
    portable_native_configured=1;boot_phase=1;
    portable_import_capture(parent,201,2000);assert(portable_native_failures==1);
    portable_native_failures=0;portable_import_capture(object,100,1000);assert(portable_native_failures==1); /* duplicate fixture */
    portable_native_configured=0;
    VirtualFree(portable_native_inverse_ids,0,MEM_RELEASE);VirtualFree(world_identities,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    puts("portable Windows semantic decoder/read-only/identity refusal tests pass (synthetic memory)");return 0;
}
