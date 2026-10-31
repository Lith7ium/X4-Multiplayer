/* Synthetic encoding vectors; does not load or call X4. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "lockstep_portable_state.h"

int main(int argc,char **argv) {
    portable_frame frame={0};unsigned char out[PORTABLE_FRAME_CAPACITY],changed[PORTABLE_FRAME_CAPACITY];
    assert(argc==2 && strlen(argv[1])==64);
    for(unsigned i=0;i<32;i++) {
        unsigned value=0;assert(sscanf(argv[1]+2*i,"%2x",&value)==1);frame.contract[i]=(unsigned char)value;
    }
    frame.game=UINT64_C(0x401c000000000000);
    for(unsigned i=0;i<PORTABLE_ACTORS;i++) {
        portable_actor *actor=&frame.actors[i];actor->saved=i+1;actor->fixture_slot=(uint16_t)(i+1);
        actor->present=actor->kind=actor->controller=1;actor->coverage=PORTABLE_COVERAGE;
        strcpy(actor->macro,i<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro");
        strcpy(actor->owner,i<6?"argon":"xenon");actor->parent=0x100;actor->hull=UINT64_C(0x4059000000000000);
        for(unsigned bank=0;bank<2;bank++) {
            portable_bank *b=bank?&actor->other:&actor->active;
            b->transform[3]=b->transform[7]=b->transform[11]=0x3f800000u;
            b->refs[0]=b->refs[1]=0x100;
        }
    }
    size_t size=portable_encode_frame(&frame,out,sizeof(out));assert(size && size<sizeof(out));
    assert(!portable_encode_frame(&frame,changed,size-1));
    frame.commands[1]=1;assert(!portable_encode_frame(&frame,changed,sizeof(changed)));frame.commands[1]=0;
    frame.returns=1;assert(!portable_encode_frame(&frame,changed,sizeof(changed)));frame.returns=0;
    frame.actors[0].active.transform[0]=0x7f800000u;
    assert(!portable_encode_frame(&frame,changed,sizeof(changed)));
    frame.actors[0].active.transform[0]=0x80000000u;
    assert(portable_encode_frame(&frame,changed,sizeof(changed))==size && memcmp(out,changed,size));
    frame.actors[0].active.transform[0]=0;
    frame.actors[0].coverage=0;assert(!portable_encode_frame(&frame,changed,sizeof(changed)));
    frame.actors[0].coverage=PORTABLE_COVERAGE;
    frame.actors[1].fixture_slot=1;assert(!portable_encode_frame(&frame,changed,sizeof(changed)));
    frame.actors[1].fixture_slot=2;
    frame.actors[1].saved=1;assert(!portable_encode_frame(&frame,changed,sizeof(changed)));frame.actors[1].saved=2;
    assert(portable_encode_frame(&frame,changed,sizeof(changed))==size && !memcmp(out,changed,size));
    for(size_t i=0;i<size;i++) {printf("%02x",out[i]);}
    putchar('\n');return 0;
}
