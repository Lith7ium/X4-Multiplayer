/* Test only: one synthetic imported world built in either executable's layout, so the
 * Windows world hash (lockstep_world_hash.h, test_lockstep_world_golden.c) and the Linux
 * world hash (linux/gate_world.h, linux/test_gate_linux.c) can be fed the same objects
 * and must print the same WORLD_HASH, WORLD_OBJECT, WORLD_DETAIL and WORLD_BANKS text.
 * Owned memory only; never executes X4 code. Objects (saved <- runtime):
 *   77 <- 100  ship: nav controller (both schemas), hull, shield with modifiers
 *   78 <- 110  turret: static controller, no shield, parent 83
 *   79 <- 120  ship: dock controller, shield without modifiers, unknown parent (runtime 999)
 *   80 <- 130  ship: undock controller, no shield
 *   81 <- 140  zone: movement controller
 *   82 <- 150  zone: controller of another family (state not claimed)
 *   83 <- 160  zone: no controller
 *   84 <- 170  absent (find returns nothing)
 *   85 <- 180  build processor: neither destructible nor positional
 *   1001 <- 101, 1002 <- 102: absent, referenced by the nav banks and pending entries
 * Controllers are built as complete objects; an object points to the interface subobject
 * (controller_interface: 0 on Windows, 0x58 on Linux except nav and static controllers).
 * Ticks: 1 as built; 2 swaps the nav banks (canonically equal) and changes 77's shield
 * value; 3 moves 78's static transform and 79's dock start time. */
#ifndef TEST_LOCKSTEP_WORLD_SCENARIO_H
#define TEST_LOCKSTEP_WORLD_SCENARIO_H
#include <stdint.h>
#include <string.h>
typedef struct {
    unsigned obj_runtime,obj_mode,obj_parent,obj_state,obj_attention,obj_controller,obj_hull,obj_shield;
    unsigned sg_begin,sg_end,sg_rate,sg_maximum,sg_delay,sg_factor,sg_value,sg_changed,sg_source_vtable,sg_source_group,sg_modifiers;
    unsigned static_transform,movement_transform,controller_interface;
    unsigned dock_poses,dock_time,dock_owner,dock_more,dock_last,dock_animation,dock_flags,undock_animation,undock_flag;
    unsigned nav_owner,nav_slots,nav_params,nav_agent,nav_commands,nav_pending,nav_time,nav_deadline;
    unsigned agent_commands,agent_slots,agent_params,agent_pending,agent_owner;
    void *ship_vtable,*turret_vtable,*zone_vtable,*processor_vtable;
    void *nav_vtable,*static_vtable,*movement_vtable,*dock_vtable,*undock_vtable,*other_vtable,*shield_source_vtable;
    uint32_t *bank_read,*bank_write;
} wscen_layout;
enum {WSCEN_OBJECTS=10};
static const uint64_t wscen_runtime[WSCEN_OBJECTS]={100,110,120,130,140,150,160,170,180,999};
static const uint64_t wscen_saved[]={77,78,79,80,81,82,83,84,85,1001,1002};
static const uint64_t wscen_saved_runtime[]={100,110,120,130,140,150,160,170,180,101,102};
#define WSCEN_IDENTITIES (sizeof(wscen_saved)/sizeof(*wscen_saved))
static __attribute__((aligned(16))) unsigned char wscen_object[9][0x400],wscen_controller[8][0x200],wscen_agent[0x1000],
    wscen_group[2][0x100],wscen_modifiers[0x40],wscen_elements[4][8];
static const wscen_layout *wscen_l;
#define WSCEN_PUT(p,type,value) do {type wscen_v_=(value);memcpy((p),&wscen_v_,sizeof(type));} while(0)
static void wscen_vectors(unsigned char *at,unsigned count,uint32_t seed) {
    for(unsigned i=0;i<count*4;i++) WSCEN_PUT(at+4*i,float,(float)(seed%97)*0.5f+(float)i*0.25f+(i%4==3?100.0f:0.0f));
}
static void wscen_object_base(unsigned index,void *vtable,uint64_t runtime,uint32_t mode,unsigned char state,uint32_t attention) {
    const wscen_layout *l=wscen_l;unsigned char *o=wscen_object[index];
    WSCEN_PUT(o,void*,vtable);WSCEN_PUT(o+l->obj_runtime,uint64_t,runtime);WSCEN_PUT(o+l->obj_mode,uint32_t,mode);
    o[l->obj_state]=state;WSCEN_PUT(o+l->obj_attention,uint32_t,attention);
}
static void wscen_shield(unsigned index,unsigned group,int modifiers,uint64_t value_bits) {
    const wscen_layout *l=wscen_l;unsigned char *g=wscen_group[group];
    WSCEN_PUT(wscen_object[index]+l->obj_shield,void*,g);
    WSCEN_PUT(g+l->sg_begin,void*,wscen_elements[0]);WSCEN_PUT(g+l->sg_end,void*,wscen_elements[group?1:3]);
    WSCEN_PUT(g+l->sg_rate,float,12.5f+group);WSCEN_PUT(g+l->sg_maximum,double,4000.0+group);
    WSCEN_PUT(g+l->sg_delay,double,2.25);WSCEN_PUT(g+l->sg_factor,float,0.75f);
    WSCEN_PUT(g+l->sg_value,uint64_t,value_bits);WSCEN_PUT(g+l->sg_changed,double,31.5+group);
    WSCEN_PUT(g+l->sg_source_vtable,void*,l->shield_source_vtable);WSCEN_PUT(g+l->sg_source_group,void*,g);
    WSCEN_PUT(g+l->sg_modifiers,void*,modifiers?wscen_modifiers:NULL);
    if(modifiers) {WSCEN_PUT(wscen_modifiers+0x18,float,1.25f);WSCEN_PUT(wscen_modifiers+0x1c,float,0.5f);WSCEN_PUT(wscen_modifiers+0x20,float,2.0f);}
}
static void wscen_dock(unsigned controller,unsigned object,int undock,double time) {
    const wscen_layout *l=wscen_l;unsigned char *c=wscen_controller[controller];
    WSCEN_PUT(c+l->controller_interface,void*,undock?l->undock_vtable:l->dock_vtable);WSCEN_PUT(c+l->dock_owner,void*,wscen_object[object]);
    wscen_vectors(c+l->dock_poses,4,11+controller);wscen_vectors(c+l->dock_more,undock?4:8,23+controller);
    if(!undock) wscen_vectors(c+l->dock_last,2,37);
    WSCEN_PUT(c+l->dock_time,double,time);
    WSCEN_PUT(c+(undock?l->undock_animation:l->dock_animation),uint32_t,undock?9u:7u);
    c[undock?l->undock_flag:l->dock_flags]=1;if(!undock) c[l->dock_flags+1]=0;
    WSCEN_PUT(wscen_object[object]+l->obj_controller,void*,c+l->controller_interface);
}
/* The nav controller, its agent and the two movement banks, as test_lockstep_world_nav.c. */
static void wscen_nav(void) {
    const wscen_layout *l=wscen_l;unsigned char *c=wscen_controller[0],*a=wscen_agent,*o=wscen_object[0];
    WSCEN_PUT(c,void*,l->nav_vtable);WSCEN_PUT(c+l->nav_owner,void*,o);
    WSCEN_PUT(c+l->nav_slots,void*,a+l->agent_slots);WSCEN_PUT(c+l->nav_params,void*,a+l->agent_params);
    WSCEN_PUT(c+l->nav_agent,void*,a);WSCEN_PUT(c+l->nav_commands,void*,a+l->agent_commands);
    WSCEN_PUT(c+l->nav_pending,void*,a+l->agent_pending);WSCEN_PUT(a+l->agent_owner,void*,o);
    WSCEN_PUT(c+l->nav_time,double,10.5);WSCEN_PUT(c+l->nav_time+8,uint32_t,1u);
    WSCEN_PUT(c+l->nav_deadline,double,17.25);c[l->nav_deadline+8]=1;
    *l->bank_read=0;*l->bank_write=1;
    for(unsigned p=0;p<2;p++) {
        unsigned char *bank=a+l->agent_slots+p*0x1a0;
        for(unsigned i=0;i<24;i++) WSCEN_PUT(bank+4*i,uint32_t,1000+p*100+i);
        WSCEN_PUT(bank+0xb0,uint64_t,101);WSCEN_PUT(bank+0xb8,uint64_t,102);
        WSCEN_PUT(bank+0xcc,uint32_t,8+p);WSCEN_PUT(bank+0x150,double,11.5+p);
        bank[0x159]=(unsigned char)p;bank[0x15a]=(unsigned char)(1-p);bank[0x15b]=4;
        WSCEN_PUT(a+l->agent_pending+p*16,uint64_t,101+p);WSCEN_PUT(a+l->agent_pending+8+p*16,uint64_t,3+p*2);
    }
    WSCEN_PUT(o+l->obj_controller,void*,c);
}
static void wscen_build(const wscen_layout *l) {
    wscen_l=l;
    memset(wscen_object,0,sizeof(wscen_object));memset(wscen_controller,0,sizeof(wscen_controller));
    memset(wscen_agent,0,sizeof(wscen_agent));memset(wscen_group,0,sizeof(wscen_group));memset(wscen_modifiers,0,sizeof(wscen_modifiers));
    wscen_object_base(0,l->ship_vtable,100,3,1,2);WSCEN_PUT(wscen_object[0]+l->obj_hull,double,950.0);
    wscen_shield(0,0,1,0x40a3880000000000ull);wscen_nav();
    wscen_object_base(1,l->turret_vtable,110,1,0,0);WSCEN_PUT(wscen_object[1]+l->obj_hull,double,120.0);
    WSCEN_PUT(wscen_object[1]+l->obj_parent,void*,wscen_object[6]);
    WSCEN_PUT(wscen_controller[1],void*,l->static_vtable);wscen_vectors(wscen_controller[1]+l->static_transform,4,5);
    WSCEN_PUT(wscen_object[1]+l->obj_controller,void*,wscen_controller[1]);
    wscen_object_base(2,l->ship_vtable,120,2,0,1);WSCEN_PUT(wscen_object[2]+l->obj_hull,double,330.5);
    wscen_shield(2,1,0,0x4059000000000000ull);
    WSCEN_PUT(wscen_object[2]+l->obj_parent,void*,wscen_object[8]); /* runtime 999 has no identity */
    wscen_dock(2,2,0,40.25);
    wscen_object_base(3,l->ship_vtable,130,2,1,3);WSCEN_PUT(wscen_object[3]+l->obj_hull,double,77.0);
    wscen_dock(3,3,1,41.75);
    wscen_object_base(4,l->zone_vtable,140,1,0,0);
    WSCEN_PUT(wscen_controller[4]+l->controller_interface,void*,l->movement_vtable);wscen_vectors(wscen_controller[4]+l->movement_transform,4,9);
    WSCEN_PUT(wscen_object[4]+l->obj_controller,void*,wscen_controller[4]+l->controller_interface);
    wscen_object_base(5,l->zone_vtable,150,1,0,0);
    WSCEN_PUT(wscen_controller[5]+l->controller_interface,void*,l->other_vtable);
    WSCEN_PUT(wscen_object[5]+l->obj_controller,void*,wscen_controller[5]+l->controller_interface);
    wscen_object_base(6,l->zone_vtable,160,4,0,0);
    wscen_object_base(7,l->processor_vtable,180,6,1,9);
    wscen_object_base(8,l->zone_vtable,999,1,0,0); /* parent only, not imported */
}
/* Object of a runtime ID as the engine's find reports it (NULL for 170, 101, 102). */
static void *wscen_find(uint64_t runtime) {
    const uint64_t runtimes[9]={100,110,120,130,140,150,160,180,999};
    for(unsigned i=0;i<9;i++) if(runtimes[i]==runtime) return wscen_object[i];
    return NULL;
}
static void wscen_step(unsigned tick) {
    const wscen_layout *l=wscen_l;
    if(tick==2) {
        unsigned char temp[0x1a0],*a=wscen_agent;
        memcpy(temp,a+l->agent_slots,0x1a0);memcpy(a+l->agent_slots,a+l->agent_slots+0x1a0,0x1a0);memcpy(a+l->agent_slots+0x1a0,temp,0x1a0);
        memcpy(temp,a+l->agent_pending,16);memcpy(a+l->agent_pending,a+l->agent_pending+16,16);memcpy(a+l->agent_pending+16,temp,16);
        *l->bank_read^=1;*l->bank_write^=1;
        uint32_t cursor;memcpy(&cursor,wscen_controller[0]+l->nav_time+8,4);if(cursor<2) {cursor^=1;memcpy(wscen_controller[0]+l->nav_time+8,&cursor,4);}
        WSCEN_PUT(wscen_group[0]+l->sg_value,uint64_t,0x40a3900000000000ull);
    }
    if(tick==3) {
        wscen_vectors(wscen_controller[1]+l->static_transform,4,6);
        WSCEN_PUT(wscen_controller[2]+l->dock_time,double,40.5);
    }
}
#endif
