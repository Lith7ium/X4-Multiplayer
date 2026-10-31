static const void *test_raw_change_address;
static unsigned test_raw_change_count,test_raw_change_at,test_raw_change_mode;
static int test_raw_reader(const void *source,void *out,size_t size) {
    if(source==test_raw_change_address && ++test_raw_change_count==test_raw_change_at) {
        if(test_raw_change_mode==1) return 0;
        if(test_raw_change_mode==2) {++stats.issued;return read_local(source,out,size);}
        if(!read_local(source,out,size)) return 0;
        ((unsigned char*)out)[0]^=1;return 1;
    }
    return read_local(source,out,size);
}
static void test_raw_field(const char *text,const char *field,char *out,size_t capacity) {
    const char *start=strstr(text,field);assert(start);start+=strlen(field);
    const char *end=strchr(start,';');size_t length=end?(size_t)(end-start):strlen(start);
    assert(length<capacity);memcpy(out,start,length);out[length]=0;
}
static void test_registry_raw(unsigned char *object,unsigned char *controller,unsigned char *agent) {
    unsigned char parent[16]={0};*(uint64_t*)(parent+8)=700;*(void**)(object+0x70)=parent;
    *(unsigned*)(object+0x1ec)=3;*(double*)(controller+0xc0)=7.5;*(unsigned*)(controller+0xc8)=1;
    *(double*)(controller+0xd0)=8.25;controller[0xd8]=1;
    unsigned char *slots=agent+0xad0;
    for(unsigned b=0;b<2;b++) {
        unsigned char *bank=slots+b*0x1a0;
        for(unsigned v=0;v<6;v++) for(unsigned k=0;k<3;k++) ((float*)(bank+v*16))[k]=(float)(b*100+v*10+k);
        *(uint64_t*)(bank+0xb0)=b?702:701;*(uint64_t*)(bank+0xb8)=b?703:0;
        *(unsigned*)(bank+0xcc)=48;bank[0x15b]=2;*(double*)(bank+0x150)=9.0+b;bank[0x159]=1;bank[0x15a]=0;
    }
    *(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities && world_identity_record(700,42) && world_identity_record(701,43) && world_identity_record(702,44));
    unsigned char object_before[0x400],controller_before[0xe0],agent_before[0xf20];
    memcpy(object_before,object,sizeof(object_before));memcpy(controller_before,controller,sizeof(controller_before));memcpy(agent_before,agent,sizeof(agent_before));
    dynamic_ship reservation=dynamic_ships[0];char out[1536],detail[256],changed[1536],local[256];const char *why;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,out,sizeof(out),detail,sizeof(detail),&why));
    assert(strstr(out,"sector_pose_supported=0;identity_ok=1;stable=1;read_before=0;write_before=1;read_after=0;write_after=1;unknown_references=1;reference_flags=00000010;parent=000000000000002a"));
    assert(!strcmp(detail,"SHIP_RAW_MOTION_LOCAL_1;runtime=00000000000003e7;refs=00000000000002bc,00000000000002bd,0000000000000000,00000000000002be,00000000000002bf"));
    assert(!memcmp(object,object_before,sizeof(object_before)) && !memcmp(controller,controller_before,sizeof(controller_before)) &&
        !memcmp(agent,agent_before,sizeof(agent_before)) && !memcmp(&reservation,&dynamic_ships[0],sizeof(reservation)) &&
        !actor_ids[14] && !actor_components[14] && !registry_pending_count && !registry_failures);
    /* Canonical motion/timing bytes are the exact existing world schema. */
    world_hash_data motion={0},timing={0};unsigned flags=0;assert(world_hash_add(&timing,object+0x1ec,4));
    assert(world_hash_motion(object,controller,&motion,&timing,&flags,NULL) && motion.length==190 && timing.length==45 && flags==16);
    char field[512],expected[512];test_raw_field(out,";motion=",field,sizeof(field));registry_raw_hex(expected,motion.data,motion.length);assert(!strcmp(field,expected));
    test_raw_field(out,";timing=",field,sizeof(field));registry_raw_hex(expected,timing.data,timing.length);assert(!strcmp(field,expected));
    char pose[273],pose_changed[273];test_raw_field(out,";raw_pose=",pose,sizeof(pose));assert(strlen(pose)==272);
    /* Velocity/timing changes do not impersonate a transform change. */
    slots[0x40]^=1;controller[0xd0]^=1;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    test_raw_field(changed,";raw_pose=",pose_changed,sizeof(pose_changed));assert(!strcmp(pose,pose_changed) && strcmp(out,changed));
    slots[0x40]^=1;controller[0xd0]^=1;slots[0]^=1;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    test_raw_field(changed,";raw_pose=",pose_changed,sizeof(pose_changed));assert(strcmp(pose,pose_changed));slots[0]^=1;
    /* Unresolved frame changes remain visible in the mandatory local receipt. */
    *(uint64_t*)(slots+0x1a0+0xb8)=704;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    assert(!strcmp(out,changed) && strcmp(detail,local));*(uint64_t*)(slots+0x1a0+0xb8)=703;
    /* A full second read, selectors and identity are all required. */
    const void *addresses[]={slots,controller+0xd0,object+8,image+0x38f4ee4,controller+0x30,object+0x70};
    const unsigned at[]={2,2,3,2,3,2};
    for(unsigned i=0;i<6;i++) for(unsigned mode=0;mode<2;mode++) {
        test_raw_change_address=addresses[i];test_raw_change_count=0;test_raw_change_at=at[i];test_raw_change_mode=mode;
        assert(!registry_raw_motion(15,999,test_dynamic_find,object,test_raw_reader,changed,sizeof(changed),local,sizeof(local),&why));
    }
    test_raw_change_address=NULL;
    test_raw_change_address=slots;test_raw_change_count=0;test_raw_change_at=2;test_raw_change_mode=2;
    uint64_t tick=stats.issued;assert(!registry_raw_motion(15,999,test_dynamic_find,object,test_raw_reader,changed,sizeof(changed),local,sizeof(local),&why));
    stats.issued=tick;test_raw_change_address=NULL;
    *(unsigned*)(image+0x38f4ee4)=1;*(unsigned*)(image+0x2f4b6f4)=0;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    assert(strstr(changed,"read_before=1;write_before=0;read_after=1;write_after=0;unknown_references=1;reference_flags=00000004"));
    assert(strstr(local,"refs=00000000000002bc,00000000000002be,00000000000002bf,00000000000002bd,0000000000000000"));
    *(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    *(unsigned*)(image+0x2f4b6f4)=0;assert(!registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    *(unsigned*)(image+0x2f4b6f4)=1;
    assert(!registry_raw_motion(13,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    assert(!registry_raw_motion(15,998,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    assert(!registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,10,local,sizeof(local),&why));
    assert(!registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,10,&why));
    dynamic_ships[0].bound_tick=6;actor_components[14]=object;actor_ids[14]=999;
    assert(registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    actor_ids[14]=998;assert(!registry_raw_motion(15,999,test_dynamic_find,object,read_local,changed,sizeof(changed),local,sizeof(local),&why));
    actor_ids[14]=0;actor_components[14]=NULL;dynamic_ships[0]=reservation;
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    *(void**)(object+0x70)=NULL;
}
