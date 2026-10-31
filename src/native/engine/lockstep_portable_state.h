/* Portable-fixture-v1 canonical bytes. No native layouts, process access,
 * pointers, compiler struct padding, floating-point operations or hashing API.
 * Platform adapters must prove every identity/read before calling this encoder. */
#ifndef X4MP_LOCKSTEP_PORTABLE_STATE_H
#define X4MP_LOCKSTEP_PORTABLE_STATE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PORTABLE_ACTORS 12u
#define PORTABLE_FRAME_CAPACITY 4096u
#define PORTABLE_COVERAGE 31u

typedef struct {
    uint32_t transform[12]; /* local position xyz, followed by three basis xyz vectors */
    uint64_t refs[2];       /* saved component IDs for native ref_b0 and ref_b8 */
} portable_bank;
typedef struct {
    uint64_t saved,parent,hull;
    uint16_t fixture_slot;
    uint8_t present,kind,controller;
    uint32_t coverage;
    char macro[64],owner[64];
    portable_bank active,other;
} portable_actor;
typedef struct {
    unsigned char contract[32];
    uint64_t tick,game,calls,returns,receipt_tick;
    uint32_t commands[2];
    portable_actor actors[PORTABLE_ACTORS];
} portable_frame;
typedef struct {unsigned char *data;size_t capacity,length;int failed;} portable_writer;

static void portable_bytes(portable_writer *w,const void *data,size_t size) {
    if(w->failed || w->length>w->capacity || size>w->capacity-w->length) {w->failed=1;return;}
    memcpy(w->data+w->length,data,size);w->length+=size;
}
static void portable_integer(portable_writer *w,uint64_t value,unsigned size) {
    unsigned char bytes[8];
    if(size>8) {w->failed=1;return;}
    for(unsigned i=0;i<size;i++) bytes[i]=(unsigned char)(value>>(8*i));
    portable_bytes(w,bytes,size);
}
static int portable_finite32(uint32_t bits) {return (bits&0x7f800000u)!=0x7f800000u;}
static int portable_finite64(uint64_t bits,int nonnegative) {
    return (bits&UINT64_C(0x7ff0000000000000))!=UINT64_C(0x7ff0000000000000) &&
        (!nonnegative || !(bits>>63) || !(bits&UINT64_C(0x7fffffffffffffff)));
}
static void portable_identifier(portable_writer *w,const char value[64]) {
    unsigned size=0;
    while(size<64 && value[size]) {
        char c=value[size];
        if(!((c>='a'&&c<='z') || (size && ((c>='0'&&c<='9') || c=='_')))) {w->failed=1;return;}
        ++size;
    }
    if(!size || size>=64) {w->failed=1;return;}
    portable_integer(w,size,1);portable_bytes(w,value,size);
}
static void portable_encode_bank(portable_writer *w,const portable_bank *bank) {
    for(unsigned i=0;i<12;i++) {
        if(!portable_finite32(bank->transform[i])) {w->failed=1;return;}
        portable_integer(w,bank->transform[i],4);
    }
    portable_integer(w,bank->refs[0],8);portable_integer(w,bank->refs[1],8);
}
static size_t portable_encode_actor(const portable_actor *actor,unsigned char *out,size_t capacity) {
    portable_writer w={out,capacity,0,0};
    if(!actor || !out || !actor->saved || !actor->fixture_slot || actor->fixture_slot>PORTABLE_ACTORS ||
       actor->present!=1 || actor->kind!=1 || actor->controller!=1 || actor->coverage!=PORTABLE_COVERAGE ||
       !portable_finite64(actor->hull,1)) return 0;
    portable_bytes(&w,"X4P1ROW\0",8);portable_integer(&w,actor->saved,8);
    portable_integer(&w,actor->fixture_slot,2);portable_integer(&w,actor->present,1);
    portable_integer(&w,actor->kind,1);portable_integer(&w,actor->controller,1);portable_integer(&w,actor->coverage,4);
    portable_identifier(&w,actor->macro);portable_identifier(&w,actor->owner);
    portable_integer(&w,actor->parent,8);portable_integer(&w,actor->hull,8);
    portable_encode_bank(&w,&actor->active);portable_encode_bank(&w,&actor->other);
    return w.failed?0:w.length;
}
static size_t portable_encode_frame(const portable_frame *frame,unsigned char *out,size_t capacity) {
    portable_writer w={out,capacity,0,0};unsigned slots=0;
    if(!frame || !out || frame->tick>120 || frame->calls!=frame->tick || frame->returns!=frame->tick ||
       frame->receipt_tick!=frame->tick || frame->commands[0] || frame->commands[1] || !portable_finite64(frame->game,1)) return 0;
    portable_bytes(&w,"X4P1FRM\0",8);portable_bytes(&w,frame->contract,32);
    portable_integer(&w,frame->tick,8);portable_integer(&w,frame->game,8);portable_integer(&w,frame->calls,8);
    portable_integer(&w,frame->returns,8);portable_integer(&w,frame->receipt_tick,8);
    portable_integer(&w,frame->commands[0],4);portable_integer(&w,frame->commands[1],4);portable_integer(&w,PORTABLE_ACTORS,2);
    for(unsigned i=0;i<PORTABLE_ACTORS;i++) {
        unsigned char row[320];const portable_actor *actor=&frame->actors[i];
        if(i && frame->actors[i-1].saved>=actor->saved) return 0;
        size_t length=portable_encode_actor(actor,row,sizeof(row));if(!length) return 0;
        unsigned mask=1u<<(actor->fixture_slot-1);if(slots&mask) return 0;slots|=mask;
        portable_integer(&w,length,2);portable_bytes(&w,row,length);
    }
    return w.failed || slots!=0xfffu?0:w.length;
}
#endif
