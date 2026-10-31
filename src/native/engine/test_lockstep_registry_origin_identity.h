static unsigned test_presence_calls,test_presence_fault_call,test_presence_change_call;
static int test_presence_reader(const void *source,void *destination,size_t bytes) {
    ++test_presence_calls;
    if(test_presence_calls==test_presence_fault_call) {memset(destination,255,bytes);return 0;}
    int result=read_local(source,destination,bytes);
    if(result && test_presence_calls==test_presence_change_call) *(uint64_t*)destination^=1;
    return result;
}
static void test_registry_origin_identity(void *object,void *controller,void *agent) {
    registry_origin_identity n={0};unsigned char before[0x400];memcpy(before,object,sizeof(before));
    registry_origin_identity_capture(&n,object,999);
    assert(n.recorded && n.stable==1 && n.first.reads==1023 && n.second.reads==1023);
    assert(n.first.object_state==5 && n.first.owner_state==5 && n.first.controller_state==5 && n.first.agent_state==5);
    assert(!memcmp(before,object,sizeof(before)) && n.first.object==(uintptr_t)object && n.first.actual==999);
    assert(n.first.controller==(uintptr_t)controller && n.first.slots==(uintptr_t)agent+0xad0 && n.first.agent_owner==(uintptr_t)object);
    /* All read failures stay explicit; a partial copy may not publish its bytes. */
    for(unsigned failure=1;failure<=20;failure++) {
        test_presence_calls=0;test_presence_fault_call=failure;test_presence_change_call=0;
        registry_origin_identity_capture_using(&n,object,999,test_presence_reader);
        assert(n.stable==3);
        if(failure==1) assert(!(n.first.reads&1) && !n.first.type);
    }
    test_presence_calls=test_presence_fault_call=0;test_presence_change_call=12;
    registry_origin_identity_capture_using(&n,object,999,test_presence_reader);
    assert(n.stable==2 && n.first.object_state==5 && n.second.object_state==4 && n.first.actual==999 && n.second.actual==998);
    test_presence_change_call=0;
    registry_origin_identity_capture(&n,NULL,999);assert(n.stable==1 && n.first.object_state==1 && !n.first.reads);
    registry_origin_identity_capture(&n,(void*)1,999);assert(n.stable==3 && n.first.object_state==2 && !n.first.reads);
    registry_origin_identity_capture(&n,object,998);assert(n.stable==1 && n.first.object_state==4 && n.first.reads==3);
    void *type=*(void**)object;*(void**)object=image+0x2b9bff8;
    test_presence_calls=0;registry_origin_identity_capture_using(&n,object,999,test_presence_reader);
    assert(n.first.object_state==3 && n.first.reads==3 && test_presence_calls==4 && !n.first.controller && !n.first.owner);
    *(void**)object=(void*)1;test_presence_calls=0;test_presence_change_call=3;
    registry_origin_identity_capture_using(&n,object,999,test_presence_reader);
    assert(n.first.type==0 && n.second.type==0 && n.first.raw_type!=n.second.raw_type && n.stable==2);
    test_presence_change_call=0;
    *(void**)object=type;
    uint64_t *owner=(uint64_t*)((unsigned char*)object+0x348);uint64_t prior_owner=*owner;*owner=0;
    registry_origin_identity_capture(&n,object,999);assert(n.first.owner_state==1 && n.first.controller_state==5);*owner=prior_owner;
    void **getter=(void**)((unsigned char*)type+0x1620);void *prior_getter=*getter;*getter=image+0x352af0;
    registry_origin_identity_capture(&n,object,999);assert(n.first.owner_state==3 && !(n.first.reads&8) && !n.first.owner);*getter=prior_getter;
    void **ctrl=(void**)((unsigned char*)object+0x200);*ctrl=NULL;
    registry_origin_identity_capture(&n,object,999);assert(n.first.controller_state==1 && !n.first.agent_state && n.first.reads==31);*ctrl=controller;
    void *controller_type=*(void**)controller;*(void**)controller=image+0x12345;
    test_presence_calls=0;registry_origin_identity_capture_using(&n,object,999,test_presence_reader);
    assert(n.first.controller_state==3 && !n.first.agent_state && n.first.reads==63 && test_presence_calls==12 && !n.first.agent);
    *(void**)controller=controller_type;
    void **controller_owner=(void**)((unsigned char*)controller+0x28);*controller_owner=NULL;
    registry_origin_identity_capture(&n,object,999);assert(n.first.controller_state==4 && n.first.agent_state==5);*controller_owner=object;
    void **parameters=(void**)((unsigned char*)controller+0x40);*parameters=NULL;
    registry_origin_identity_capture(&n,object,999);assert(n.first.controller_state==5 && n.first.agent_state==1 && n.first.reads==511);*parameters=agent;
    void **slots=(void**)((unsigned char*)controller+0x30);*slots=(unsigned char*)agent+0xad1;
    registry_origin_identity_capture(&n,object,999);assert(n.first.agent_state==4 && !(n.first.reads&512));*slots=(unsigned char*)agent+0xad0;
    void **agent_owner=(void**)((unsigned char*)agent+0xef0);*agent_owner=NULL;
    registry_origin_identity_capture(&n,object,999);assert(n.first.agent_state==4 && n.first.reads==1023);*agent_owner=object;
    assert(!memcmp(before,object,sizeof(before)));
    registry_origin_identity_capture(&n,object,999);
    registry_pending_count=1;registry_pending[0]=(registry_pending_update){.runtime=999,.order=12,.thread=123};registry_pending[0].origin.identity=n;
    char row[1024];assert(registry_origin_identity_format(0,row,sizeof(row))>0 && strlen(row)<900 && strstr(row,"reads=3ff rereads=3ff") && strstr(row,"stable=1"));
    assert(!registry_origin_identity_format(0,row,10) && !registry_origin_identity_format(1,row,sizeof(row)));
    memset(&registry_pending[0].origin.identity,255,sizeof(n));
    assert(registry_origin_identity_format(0,row,sizeof(row))>0 && strlen(row)<900);
    memset(registry_pending,0,sizeof(registry_pending));registry_pending_count=0;
}
