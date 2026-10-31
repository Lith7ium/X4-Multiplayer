/* Read-only WORLD_IMPORTED_2 Nav capture for the pinned Windows image.
 * The two 16-byte pending entries are Agent+ed0/ee0, NOT controller-owned
 * storage. ea2a20 selects an entry through controller+50; ea35e0 resolves its
 * leading runtime ID and may emit events. e8f250 maintains the second u64.
 * No engine getter is invoked. Two matching reads detect observed changes;
 * they do not establish a native lifetime lock or exclude an unobserved ABA. */
#ifndef LOCKSTEP_WORLD_NAV_H
#define LOCKSTEP_WORLD_NAV_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define WORLD_NAV_BANK_BYTES 0x1a0u
#define WORLD_NAV_RAW_BYTES 99u
#define WORLD_NAV_TIMING_BYTES 79u
#define WORLD_NAV_MOTION_BYTES 190u
enum { WORLD_NAV_NULL, WORLD_NAV_SAVED, WORLD_NAV_UNKNOWN };
typedef int (*world_nav_reader)(const void *,void *,size_t);
typedef uint64_t (*world_nav_resolver)(uint64_t);
typedef struct {
    uintptr_t object,object_vtable,controller,vtable,owner,slots,params,agent,commands,pending,agent_owner;
    uint64_t runtime;
    uint32_t attention,read,write;
} world_nav_links;
typedef struct {
    world_nav_links links;
    unsigned char nav[21],banks[2][WORLD_NAV_BANK_BYTES];
    uint64_t pending[2][2],saved[2],bank_saved[2][2];
    unsigned char states[2];
} world_nav_snapshot;
static int world_nav_address(uintptr_t p,size_t span) {
    return p>=0x10000 && !(p&7) && span<=UINTPTR_MAX-p;
}
static int world_nav_links_read(world_nav_reader reader,uintptr_t image_base,
                               uintptr_t object,uintptr_t controller,uint64_t runtime,
                               world_nav_links *s,const char **why) {
    memset(s,0,sizeof(*s));s->object=object;s->controller=controller;
    *why="nav_links_incomplete";
    if(!world_nav_address(object,0x208) || !world_nav_address(controller,0xd9) ||
       !reader((void*)object,&s->object_vtable,8) || !reader((void*)(object+8),&s->runtime,8) || !reader((void*)(object+0x1ec),&s->attention,4) ||
       !reader((void*)(object+0x200),&s->controller,8) || !reader((void*)controller,&s->vtable,8) ||
       !reader((void*)(controller+0x28),&s->owner,8) || !reader((void*)(controller+0x30),&s->slots,8) ||
       !reader((void*)(controller+0x38),&s->params,8) || !reader((void*)(controller+0x40),&s->agent,8) ||
       !reader((void*)(controller+0x48),&s->commands,8) || !reader((void*)(controller+0x50),&s->pending,8)) return 0;
    *why="nav_owner_invalid";
    if(!runtime || s->runtime!=runtime || s->controller!=controller || s->vtable!=image_base+0x2c51158 ||
       s->owner!=object || !world_nav_address(s->agent,0xef8) || s->slots!=s->agent+0xad0 ||
       s->params!=s->agent+0xe10 || s->commands!=s->agent+0x6e0 || s->pending!=s->agent+0xed0) return 0;
    *why="nav_links_incomplete";
    if(!reader((void*)(s->agent+0xef0),&s->agent_owner,8) ||
       !reader((void*)(image_base+0x38f4ee4),&s->read,4) ||
       !reader((void*)(image_base+0x2f4b6f4),&s->write,4)) return 0;
    *why="nav_owner_invalid";if(s->agent_owner!=object) return 0;
    *why="nav_selector_invalid";
    return s->read<=1 && s->write<=1 && s->read+s->write==1;
}
static int world_nav_take(world_nav_reader reader,uintptr_t image_base,uintptr_t object,
                          uintptr_t controller,uint64_t runtime,world_nav_snapshot *s,const char **why) {
    world_nav_links end;memset(s,0,sizeof(*s));
    if(!world_nav_links_read(reader,image_base,object,controller,runtime,&s->links,why)) return 0;
    *why="nav_tuple_incomplete";
    if(!reader((void*)(controller+0xc0),s->nav,12) || !reader((void*)(controller+0xd0),s->nav+12,9) ||
       !reader((void*)s->links.slots,s->banks,sizeof(s->banks)) ||
       !reader((void*)s->links.pending,s->pending,sizeof(s->pending))) return 0;
    if(!world_nav_links_read(reader,image_base,object,controller,runtime,&end,why)) return 0;
    *why="nav_links_changed";
    return !memcmp(&s->links,&end,sizeof(end));
}
static void world_nav_resolve(world_nav_snapshot *s,world_nav_resolver resolve) {
    for(unsigned p=0;p<2;p++) {
        uint64_t runtime=s->pending[p][0];s->saved[p]=runtime?resolve(runtime):0;
        s->states[p]=(unsigned char)(!runtime?WORLD_NAV_NULL:s->saved[p]?WORLD_NAV_SAVED:WORLD_NAV_UNKNOWN);
        uint64_t refs[2];memcpy(refs,s->banks[p]+0xb0,sizeof(refs));
        for(unsigned j=0;j<2;j++) s->bank_saved[p][j]=refs[j]?resolve(refs[j]):0;
    }
}
static int world_nav_capture(world_nav_reader reader,world_nav_resolver resolve,uintptr_t image_base,
                             uintptr_t object,uintptr_t controller,uint64_t runtime,
                             world_nav_snapshot *s,const char **why) {
    world_nav_snapshot end;
    if(!world_nav_take(reader,image_base,object,controller,runtime,s,why)) return 0;
    world_nav_resolve(s,resolve);
    if(!world_nav_take(reader,image_base,object,controller,runtime,&end,why)) return 0;
    world_nav_resolve(&end,resolve);
    *why="nav_tuple_changed";if(memcmp(s,&end,sizeof(end))) return 0;
    uint32_t cursor;memcpy(&cursor,s->nav+8,4);
    *why="nav_cursor_invalid";if(cursor>2) return 0;
    *why="nav_pending_reference_unknown";
    if(s->states[0]==WORLD_NAV_UNKNOWN || s->states[1]==WORLD_NAV_UNKNOWN) return 0;
    *why="nav_complete";return 1;
}
/* Pack explicit fields, never struct padding, into versioned little-endian
 * Windows-local preimages. The legacy schema is implemented separately. */
static void world_nav_encode(const world_nav_snapshot *s,unsigned char timing[WORLD_NAV_TIMING_BYTES],
                             unsigned char motion[WORLD_NAV_MOTION_BYTES],
                             unsigned char raw[WORLD_NAV_RAW_BYTES],unsigned *flags) {
    uint32_t cursor,relation,kind=0x2c51158;memcpy(&cursor,s->nav+8,4);
    relation=cursor==2?2:cursor==s->links.read?0:1;
    memcpy(timing,&s->links.attention,4);memcpy(timing+4,s->nav,8);memcpy(timing+12,&relation,4);
    memcpy(timing+16,s->nav+12,9);memcpy(motion,&kind,4);
    for(unsigned role=0;role<2;role++) {
        unsigned p=role?s->links.write:s->links.read;const unsigned char *bank=s->banks[p];
        unsigned char *m=motion+4+role*93,*t=timing+25+role*10;
        for(unsigned i=0;i<6;i++) memcpy(m+i*12,bank+i*16,12);
        memcpy(m+72,s->bank_saved[p],16);memcpy(m+88,bank+0xcc,4);m[92]=bank[0x15b];
        uint64_t refs[2];memcpy(refs,bank+0xb0,16);
        for(unsigned j=0;j<2;j++) if(refs[j] && !s->bank_saved[p][j]) *flags|=16;
        memcpy(t,bank+0x150,8);memcpy(t+8,bank+0x159,2);
        t=timing+45+role*17;t[0]=s->states[p];memcpy(t+1,&s->saved[p],8);memcpy(t+9,&s->pending[p][1],8);
    }
    memcpy(raw,&s->links.read,4);memcpy(raw+4,&s->links.write,4);memcpy(raw+8,s->nav,21);
    memcpy(raw+29,s->pending,32);memcpy(raw+61,s->saved,16);memcpy(raw+77,s->states,2);
    for(unsigned p=0;p<2;p++) {memcpy(raw+79+p*10,s->banks[p]+0x150,8);memcpy(raw+87+p*10,s->banks[p]+0x159,2);}
}
#endif
