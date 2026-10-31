/* Read-only first-origin presence. No native getter, reference acquisition,
 * controller initialization, transform query, registry binding or RNG write.
 * These full functions establish the exact fields; no default/base class layout
 * is inferred when the observed primary vtable differs. */
static int registry_origin_identity_pins(void) {
    return hash_ok(image+0x352b10,8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53") &&
        hash_ok(image+0x745e00,711,"acd257ef830505bca734583af1fbb2460f8bb09d2bb07d323cbe4596856343b2") &&
        hash_ok(image+0xea4a00,375,"c55b28f9e7b1f10a740d2436077b498e29a6940031da6d5d0c04afeeb8fba117") &&
        hash_ok(image+0xea8cf0,1332,"8d94c7bcd214c10c531b48f7bcd8f3ddb87b49025a437416d0a9123c429a0615") &&
        hash_ok(image+0xeac530,582,"7b02058e82f6d8acf92e189f5982eae449af06dc045fcb41be1d182ea374267c") &&
        hash_ok(image+0xe85360,1368,"c565989a8232aecf63974a4ae52a53edcaae07c632a0d8c5cb3e5b540f8f4a7e");
}
typedef int (*registry_presence_reader)(const void *,void *,size_t);
static int registry_presence_read(registry_presence_reader reader,uint64_t base,unsigned offset,uint64_t *value,unsigned bit,unsigned *reads) {
    uint64_t temporary=0;
    if(!base || base>UINTPTR_MAX-offset-8 || !reader((void*)(uintptr_t)(base+offset),&temporary,8)) return 0;
    *value=temporary;
    *reads|=bit;return 1;
}
static registry_presence_sample registry_presence_read_once(void *object,uint64_t runtime,registry_presence_reader reader) {
    registry_presence_sample n={0};n.object=(uintptr_t)object;
    if(!object) {n.object_state=PRESENCE_ABSENT;return n;}
    uint64_t type=0,controller_type=0;
    int type_read=registry_presence_read(reader,n.object,0,&type,1,&n.reads);
    int id_read=registry_presence_read(reader,n.object,8,&n.actual,2,&n.reads);
    n.raw_type=type;n.type=registry_local_rva((void*)(uintptr_t)type);
    if(!type_read || !id_read) {n.object_state=PRESENCE_UNREADABLE;return n;}
    if(!registry_supported_ship_type((void*)(uintptr_t)type)) {n.object_state=PRESENCE_UNSUPPORTED;return n;}
    if(!runtime || n.actual!=runtime) {n.object_state=PRESENCE_PARTIAL;return n;}
    n.object_state=PRESENCE_PRESENT;
    uint64_t getter=0;
    if(!registry_presence_read(reader,type,0x1620,&getter,4,&n.reads)) n.owner_state=PRESENCE_UNREADABLE;
    else {
        n.raw_owner_getter=getter;n.owner_getter=registry_local_rva((void*)(uintptr_t)getter);
        if(getter!=(uintptr_t)(image+0x352b10)) n.owner_state=PRESENCE_UNSUPPORTED;
        else if(!registry_presence_read(reader,n.object,0x348,&n.owner,8,&n.reads)) n.owner_state=PRESENCE_UNREADABLE;
        else n.owner_state=n.owner?PRESENCE_PRESENT:PRESENCE_ABSENT;
    }
    if(!registry_presence_read(reader,n.object,0x200,&n.controller,16,&n.reads)) n.controller_state=PRESENCE_UNREADABLE;
    else if(!n.controller) n.controller_state=PRESENCE_ABSENT;
    else if(!registry_presence_read(reader,n.controller,0,&controller_type,32,&n.reads)) n.controller_state=PRESENCE_UNREADABLE;
    else {
        n.raw_controller_type=controller_type;n.controller_type=registry_local_rva((void*)(uintptr_t)controller_type);
        if(controller_type!=(uintptr_t)(image+0x2c51158)) n.controller_state=PRESENCE_UNSUPPORTED;
        else {
            int owner_read=registry_presence_read(reader,n.controller,0x28,&n.controller_owner,64,&n.reads);
            int slots_read=registry_presence_read(reader,n.controller,0x30,&n.slots,128,&n.reads);
            int agent_read=registry_presence_read(reader,n.controller,0x40,&n.agent,256,&n.reads);
            if(!owner_read || !slots_read || !agent_read) n.controller_state=PRESENCE_UNREADABLE;
            else {
                n.controller_state=n.controller_owner==n.object?PRESENCE_PRESENT:PRESENCE_PARTIAL;
                if(!n.agent) n.agent_state=PRESENCE_ABSENT;
                else if(n.agent>UINTPTR_MAX-0xad0 || n.slots!=n.agent+0xad0) n.agent_state=PRESENCE_PARTIAL;
                else if(!registry_presence_read(reader,n.agent,0xef0,&n.agent_owner,512,&n.reads)) n.agent_state=PRESENCE_UNREADABLE;
                else n.agent_state=n.agent_owner==n.object?PRESENCE_PRESENT:PRESENCE_PARTIAL;
            }
        }
    }
    return n;
}
static unsigned registry_presence_stability(const registry_presence_sample *first,const registry_presence_sample *second) {
    if(first->object_state==PRESENCE_UNREADABLE || first->owner_state==PRESENCE_UNREADABLE ||
       first->controller_state==PRESENCE_UNREADABLE || first->agent_state==PRESENCE_UNREADABLE ||
       second->object_state==PRESENCE_UNREADABLE || second->owner_state==PRESENCE_UNREADABLE ||
       second->controller_state==PRESENCE_UNREADABLE || second->agent_state==PRESENCE_UNREADABLE) return 3;
    return memcmp(first,second,sizeof(*first))?2:1;
}
static void registry_origin_identity_capture_using(registry_origin_identity *n,void *object,uint64_t runtime,registry_presence_reader reader) {
    n->recorded=1;n->first=registry_presence_read_once(object,runtime,reader);n->second=registry_presence_read_once(object,runtime,reader);
    n->stable=registry_presence_stability(&n->first,&n->second);
}
static void registry_origin_identity_capture(registry_origin_identity *n,void *object,uint64_t runtime) {
    registry_origin_identity_capture_using(n,object,runtime,read_local);
}
static int registry_origin_identity_format(unsigned index,char *out,size_t capacity) {
    registry_pending_update p;AcquireSRWLockShared(&registry_lock);
    int exists=index<registry_pending_count;if(exists) p=registry_pending[index];ReleaseSRWLockShared(&registry_lock);
    if(!exists) return 0;registry_origin_identity n=p.origin.identity;registry_presence_sample a=n.first,b=n.second;
    int length=snprintf(out,capacity,"schema=1 runtime=%016llx order=%llu thread=%u recorded=%u object=%016llx type=%llx actual=%016llx object_state=%u owner_getter=%llx owner=%016llx owner_state=%u controller=%016llx controller_type=%llx controller_owner=%016llx controller_state=%u slots=%016llx agent=%016llx agent_owner=%016llx agent_state=%u reads=%x rereads=%x reobject=%u reowner=%u recontroller=%u reagent=%u stable=%u",
        p.runtime,p.order,p.thread,n.recorded,a.object,a.type,a.actual,a.object_state,a.owner_getter,a.owner,a.owner_state,
        a.controller,a.controller_type,a.controller_owner,a.controller_state,a.slots,a.agent,a.agent_owner,a.agent_state,
        a.reads,b.reads,b.object_state,b.owner_state,b.controller_state,b.agent_state,n.stable);
    return length>0 && (size_t)length<capacity?length:0;
}
