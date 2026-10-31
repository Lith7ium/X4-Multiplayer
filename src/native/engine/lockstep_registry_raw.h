/* Passive reserved-ship motion evidence. These are stored frame-relative
 * pathing fields, not a sector transform. No navigation/transform getter or
 * enrollment/preparation function is called. Two sequential captures detect
 * changed fields; they are not a native atomic snapshot or generation lease. */
typedef struct {
    void *object,*type,*macro,*faction,*controller,*slots,*agent,*parent;
    uint64_t runtime,parent_runtime,refs[2][2];
    unsigned read,write;
    unsigned char vectors[2][72],mode[2][4],layer[2],attention[4],controller_time[21],bank_time[2][10];
} registry_raw_capture;
typedef int (*registry_raw_reader)(const void *,void *,size_t);
static int registry_raw_identity(unsigned actor,uint64_t runtime,const dynamic_ship *reservation,
        snapshot_find_fn find,void *map,registry_raw_reader reader,registry_raw_capture *c,const char **why) {
    unsigned char *o=find(map,runtime,4);void *owner,*type;uint64_t actual;
    *why="identity";
    if(!o || !reader(o+8,&actual,8) || actual!=runtime || world_identity_lookup(runtime) ||
       !registry_macro_detail(o,reservation->macro,why) || !registry_owner_detail(o,reservation->owner,why) ||
       !reader(o,&c->type,8) || !reader(o+0x60,&c->macro,8) || !reader(o+0x348,&c->faction,8) ||
       !reader(o+0x200,&c->controller,8) || !c->controller ||
       !reader(c->controller,&type,8) || type!=image+0x2c51158 ||
       !reader((unsigned char*)c->controller+0x28,&owner,8) || owner!=o ||
       !reader((unsigned char*)c->controller+0x30,&c->slots,8) || (uintptr_t)c->slots<0xad0 ||
       !reader((unsigned char*)c->controller+0x40,&c->agent,8) || c->agent!=(unsigned char*)c->slots-0xad0 ||
       !reader((unsigned char*)c->agent+0xef0,&owner,8) || owner!=o) return 0;
    if(reservation->bound_tick && (actor_components[actor-1]!=o || actor_ids[actor-1]!=runtime)) return 0;
    if(!reservation->bound_tick && (actor_components[actor-1] || actor_ids[actor-1])) return 0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(i!=actor-1 &&
       (actor_components[i]==o || actor_ids[i]==runtime)) return 0;
    c->object=o;c->runtime=runtime;return 1;
}
static int registry_raw_once(unsigned actor,uint64_t runtime,const dynamic_ship *reservation,
        snapshot_find_fn find,void *map,registry_raw_reader reader,registry_raw_capture *c,const char **why) {
    memset(c,0,sizeof(*c));
    if(!registry_raw_identity(actor,runtime,reservation,find,map,reader,c,why)) return 0;
    *why="selectors";
    if(!reader(image+0x38f4ee4,&c->read,4) || !reader(image+0x2f4b6f4,&c->write,4) ||
       c->read>1 || c->write>1 || c->read+c->write!=1) return 0;
    *why="fields";
    if(!reader((unsigned char*)c->object+0x70,&c->parent,8) ||
       (c->parent && !reader((unsigned char*)c->parent+8,&c->parent_runtime,8)) ||
       !reader((unsigned char*)c->object+0x1ec,c->attention,4) ||
       !reader((unsigned char*)c->controller+0xc0,c->controller_time,12) ||
       !reader((unsigned char*)c->controller+0xd0,c->controller_time+12,9)) return 0;
    if(c->parent && !c->parent_runtime) return 0;
    for(unsigned i=0;i<2;i++) {
        unsigned char *b=(unsigned char*)c->slots+(i?c->write:c->read)*0x1a0;
        /* Match the existing world schema's six xyz vectors: SIMD fourth lanes
         * are not included. The first four vectors are translation and basis. */
        for(unsigned v=0;v<6;v++) if(!reader(b+v*16,c->vectors[i]+v*12,12)) return 0;
        if(!reader(b+0xb0,c->refs[i],16) || !reader(b+0xcc,c->mode[i],4) ||
           !reader(b+0x15b,c->layer+i,1) || !reader(b+0x150,c->bank_time[i],8) ||
           !reader(b+0x159,c->bank_time[i]+8,2)) return 0;
    }
    unsigned read,write;registry_raw_capture identity={0};*why="identity_recheck";
    if(!registry_raw_identity(actor,runtime,reservation,find,map,reader,&identity,why) ||
       identity.object!=c->object || identity.type!=c->type || identity.macro!=c->macro || identity.faction!=c->faction ||
       identity.controller!=c->controller || identity.slots!=c->slots || identity.agent!=c->agent) return 0;
    *why="selectors_recheck";
    return reader(image+0x38f4ee4,&read,4) && reader(image+0x2f4b6f4,&write,4) && read==c->read && write==c->write;
}
static void registry_raw_hex(char *out,const void *bytes,unsigned count) {
    static const char digits[]="0123456789abcdef";const unsigned char *p=bytes;
    for(unsigned i=0;i<count;i++) {out[i*2]=digits[p[i]>>4];out[i*2+1]=digits[p[i]&15];}out[count*2]=0;
}
static int registry_raw_motion(unsigned actor,uint64_t runtime,snapshot_find_fn find,void *map,registry_raw_reader reader,
        char *out,size_t capacity,char *detail,size_t detail_capacity,const char **why) {
    dynamic_ship reservation;uint64_t tick=stats.issued;*why="arguments";
    if(actor_role(actor)!=ACTOR_DYNAMIC_SHIP || !runtime || !find || !map || !reader) return 0;
    AcquireSRWLockShared(&registry_lock);reservation=dynamic_ships[actor-ACTOR_DYNAMIC_FIRST];ReleaseSRWLockShared(&registry_lock);
    registry_raw_capture first,second;
    if(!registry_raw_once(actor,runtime,&reservation,find,map,reader,&first,why) ||
       !registry_raw_once(actor,runtime,&reservation,find,map,reader,&second,why)) return 0;
    *why="unstable";if(memcmp(&first,&second,sizeof(first)) || stats.issued!=tick) return 0;
    AcquireSRWLockShared(&registry_lock);
    int unchanged=!memcmp(&reservation,&dynamic_ships[actor-ACTOR_DYNAMIC_FIRST],sizeof(reservation));
    ReleaseSRWLockShared(&registry_lock);if(!unchanged) return 0;
    uint64_t raw[5]={first.parent_runtime,first.refs[0][0],first.refs[0][1],first.refs[1][0],first.refs[1][1]},saved[5];
    unsigned flags=0,unknown=0;
    for(unsigned i=0;i<5;i++) {saved[i]=world_identity_lookup(raw[i]);if(raw[i] && !saved[i]) {flags|=1u<<i;++unknown;}}
    /* Byte-for-byte world nav motion/timing bodies, plus an independently
     * comparable raw transform/reference subset. Never convert to UIPosRot. */
    unsigned char motion[190],timing[45],pose[136];unsigned m=4,t=25,p=8;unsigned controller=0x2c51158;
    memcpy(motion,&controller,4);memcpy(timing,first.attention,4);memcpy(timing+4,first.controller_time,21);memcpy(pose,saved,8);
    for(unsigned i=0;i<2;i++) {
        memcpy(motion+m,first.vectors[i],72);m+=72;memcpy(motion+m,saved+1+i*2,16);m+=16;
        memcpy(motion+m,first.mode[i],4);m+=4;motion[m++]=first.layer[i];
        memcpy(timing+t,first.bank_time[i],10);t+=10;
        memcpy(pose+p,first.vectors[i],48);p+=48;memcpy(pose+p,saved+1+i*2,16);p+=16;
    }
    char mh[381],th[91],ph[273];registry_raw_hex(mh,motion,sizeof(motion));registry_raw_hex(th,timing,sizeof(timing));registry_raw_hex(ph,pose,sizeof(pose));
    int n=snprintf(out,capacity,"SHIP_RAW_MOTION_1;handle=%016llx;slot=%u;tick=%llu;sector_pose_supported=0;identity_ok=1;stable=1;read_before=%u;write_before=%u;read_after=%u;write_after=%u;unknown_references=%u;reference_flags=%08x;parent=%016llx;raw_pose=%s;motion=%s;timing=%s",
        reservation.handle,actor,tick,first.read,first.write,second.read,second.write,unknown,flags,saved[0],ph,mh,th);
    int d=snprintf(detail,detail_capacity,"SHIP_RAW_MOTION_LOCAL_1;runtime=%016llx;refs=%016llx,%016llx,%016llx,%016llx,%016llx",
        runtime,raw[0],raw[1],raw[2],raw[3],raw[4]);
    *why="format";return n>0 && (size_t)n<capacity && d>0 && (size_t)d<detail_capacity;
}
