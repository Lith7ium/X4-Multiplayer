/* Wire/RNG contract only: no engine, hooks, platform API or simulation proof. */
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lockstep_wire.h"
#include "lockstep_seed_core.h"

#ifdef NDEBUG
#error Contract checks require assertions
#endif

int main(void) {
    static const uint64_t vectors[][7] = {
        {0,0,0,0,0,0,UINT64_C(0x04f8c8fd61cdaccb)},
        {7,1,2,1,1,0,UINT64_C(0xd22ec0dd368c4af0)},
        {7,1,1,2,1,0,UINT64_C(0x1b1b3f854c06ba4d)},
        {7,1,1,1,1,1,UINT64_C(0x979903915cdd1fc3)},
        {UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,
         UINT64_C(0x51ef5be0dcafd31c)},
        {UINT64_C(0x42554c4c45545f31),18000,12,255,65535,UINT32_MAX,
         UINT64_C(0x680128f736132764)}
    };
    assert(LOCKSTEP_RNG_SCHEMA==2);
    printf("{\"rng_schema\":%u,\"seeds\":[",LOCKSTEP_RNG_SCHEMA);
    for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++) {
        const uint64_t *v=vectors[i];
        uint64_t got=lockstep_seed(v[0],v[1],v[2],v[3],v[4],v[5]);
        assert(got==v[6]);
        printf("%s\"%016" PRIx64 "\"",i?",":"",got);
    }
    const char *valid[]={"00000000","ffffffff","02010000","01070100"};
    const char *invalid[]={NULL,"","0000000","000000000","0000000G","0000000F","0000000|","0000000\n","0000000\x80"};
    unsigned cases=0;
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++) {
        assert(gate_command_valid(valid[i]));++cases;
    }
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!gate_command_valid(invalid[i]));++cases;
    }
    const char *prefix="NGGO|2|contract|1|61";
    struct permit_case {const char *reply;unsigned slot;const char *proposal;int result;} permits[]={
        {"NGGO|2|contract|1|61|02010000|01070100",1,"02010000",1},
        {"NGGO|2|contract|1|61|02010000|01070100",2,"01070100",1},
        {"NGGO|2|contract|1|61|02010000|01070100",2,"02010000",-1},
        {"NGGO|2|contract|1|61|02010000|01070100",0,"02010000",0},
        {"NGGO|2|contract|1|61|02010000|01070100",3,"02010000",0},
        {"NGGO|1|contract|1|61|02010000|01070100",1,"02010000",0},
        {"NGGO|2|contract|1|62|02010000|01070100",1,"02010000",0},
        {"NGGO|2|contract|1|61|02010000|0107010G",1,"02010000",0},
        {"NGGO|2|contract|1|61|02010000|01070100x",1,"02010000",0},
        {"NGGO|2|contract|1|61|02010000",1,"02010000",0}
    };
    for(unsigned i=0;i<sizeof(permits)/sizeof(permits[0]);i++) {
        char first[9]="aaaaaaaa",second[9]="bbbbbbbb";
        struct permit_case p=permits[i];
        assert(gate_permit_inputs(p.reply,prefix,p.slot,p.proposal,first,second)==p.result);
        assert(!strcmp(first,p.result==1?"02010000":"aaaaaaaa"));
        assert(!strcmp(second,p.result==1?"01070100":"bbbbbbbb"));
        ++cases;
    }
    printf("],\"wire_cases\":%u}\n",cases);
    return 0;
}
