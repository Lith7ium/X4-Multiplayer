/* Owned memory only; no engine function is invoked. Exact-image class code is
 * copied read-only solely to exercise the pinned decoder and private export. */
typedef struct {
    unsigned char *image,*guard;unsigned calls,fail_at,stop_at,mutate_at,stub_loaded,pause_at;
    HANDLE paused,resume;wui_result threaded;
    unsigned char node[32],exec[0x60],context[0x130],descriptor[0x60],objects[10][0x80],registry[0x200],page[128];
    uintptr_t program[2],nodes[1];uint64_t saved_runtime;
} test_wui_fixture;
static int test_wui_read(void *arg,uint64_t address,void *out,size_t size) {
    test_wui_fixture *f=arg;++f->calls;
    if(f->calls==f->fail_at) return 0;
    int ok=read_local((void*)(uintptr_t)address,out,size);
    if(f->calls==f->mutate_at) ++*(uint64_t*)(f->context+8);
    if(f->calls==f->pause_at) {assert(SetEvent(f->paused));assert(WaitForSingleObject(f->resume,2000)==WAIT_OBJECT_0);}
    return ok;
}
static uint64_t test_wui_saved(void *arg,uint64_t runtime) {return runtime==((test_wui_fixture*)arg)->saved_runtime?0x1234:0;}
static int test_wui_stop(void *arg) {test_wui_fixture *f=arg;return f->stop_at&&f->calls>=f->stop_at;}
static int test_wui_stub(void *arg,const unsigned char bytes[36]) {
    test_wui_fixture *f=arg;if(f->stub_loaded) return world_unknown_identity_stub(NULL,bytes);
    unsigned char synthetic[36]={0};return !memcmp(synthetic,bytes,36);
}
static void test_wui_init(test_wui_fixture *f) {
    f->calls=f->fail_at=f->stop_at=f->mutate_at=f->pause_at=0;f->saved_runtime=(1ULL<<25)|2;
    memset(f->objects,0,sizeof(f->objects));memset(f->registry,0,sizeof(f->registry));memset(f->page,0,sizeof(f->page));
    memset(f->context,0,sizeof(f->context));memset(f->exec,0,sizeof(f->exec));memset(f->descriptor,0,sizeof(f->descriptor));
    uintptr_t base=(uintptr_t)f->image;
    *(uintptr_t*)(f->image+0x6cf1500)=(uintptr_t)f->registry;*(uintptr_t*)(f->registry+0x58)=(uintptr_t)f->page;
    for(unsigned i=0;i<10;i++) {
        uint64_t runtime=(1ULL<<25)|(i+1);*(uintptr_t*)f->objects[i]=base+0x30000;*(uint64_t*)(f->objects[i]+8)=runtime;
        unsigned lane=i%3;unsigned char *block=f->page+(i/3)*32;
        *(uintptr_t*)(block+lane*8)=(uintptr_t)f->objects[i];*(uint16_t*)(block+24+lane*2)=1;
        *(uint32_t*)(block+28)|=5u<<(16+lane*4);
    }
    *(uintptr_t*)(f->objects[0]+0x70)=(uintptr_t)f->objects[1];
    *(uintptr_t*)(f->image+0x30000-8)=base+0x40000;*(uintptr_t*)(f->image+0x30000+0x11e0)=base+0x60000;
    const uint32_t col[6]={1,0,0,0x50000,0x50080,0x40000};memcpy(f->image+0x40000,col,24);
    strcpy((char*)f->image+0x50010,".?AVEntity@U@@");
    *(int32_t*)(f->image+0x60006)=0x70000-0x6000a;
    memset(f->image+0x70000,0,32);*(uint32_t*)(f->image+0x70004)=2u<<(2*(22%16));*(uint32_t*)(f->image+0x70008)=2u<<(2*(44%16));
    *(uintptr_t*)f->node=base+0x80000;*(uintptr_t*)(f->image+0x80018)=base+0x81000;
    f->nodes[0]=(uintptr_t)f->node;f->program[0]=(uintptr_t)f->nodes;f->program[1]=(uintptr_t)(f->nodes+1);
    *(uintptr_t*)(f->exec+0x10)=(uintptr_t)f->program;*(uint64_t*)(f->exec+0x18)=1;
    *(uint32_t*)(f->exec+0x38)=0x66;*(uintptr_t*)(f->exec+0x40)=(uintptr_t)f->context+0xa8;
    *(uintptr_t*)(f->image+0x38f4c60)=(uintptr_t)f->exec;
    *(uintptr_t*)f->context=base+0x2c6a270;*(uint64_t*)(f->context+8)=0x44;*(uintptr_t*)(f->context+0x120)=(uintptr_t)f->objects[0];
    *(uintptr_t*)(f->context+0xd8)=(uintptr_t)f->descriptor;*(uintptr_t*)f->descriptor=base+0x2c69030;
    memcpy(f->descriptor+0x10,"move.idle",10);*(uint64_t*)(f->descriptor+0x20)=9;*(uint64_t*)(f->descriptor+0x28)=15;*(uint32_t*)(f->descriptor+0x58)=4;
}
static void test_wui_capture(test_wui_fixture *f,wui_result *r) {
    const wui_api api={f,test_wui_read,test_wui_saved,test_wui_stop,test_wui_stub};
    wui_observe(&api,(uintptr_t)f->image,(uintptr_t)f->node,(uintptr_t)f->exec,(uintptr_t)f->exec+0x38,r);
    assert(r->reads<=WUI_READS&&r->bytes<=WUI_BYTES);
}
static DWORD WINAPI test_wui_worker(void *arg) {test_wui_fixture *f=arg;test_wui_capture(f,&f->threaded);return 0;}
static void test_world_unknown_identity(void) {
    test_wui_fixture *f=calloc(1,sizeof(*f));assert(f);
    f->image=VirtualAlloc(NULL,0x6fa9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(f->image);
    if(test_wq_image_path) {
        FILE *file=fopen(test_wq_image_path,"rb");assert(file&&!fseek(file,0,SEEK_END));long length=ftell(file);assert(length>0&&!fseek(file,0,SEEK_SET));
        unsigned char *raw=malloc((size_t)length);assert(raw&&fread(raw,1,(size_t)length,file)==(size_t)length&&!fclose(file));assert(hash_ok(raw,(ULONG)length,EXE_SHA));
        test_ww_copy_rva(raw,(size_t)length,0x34e4e0,f->image+0x60000,36);free(raw);f->stub_loaded=1;
    }
    wui_result r;test_wui_init(f);test_wui_capture(f,&r);
    assert(!r.error&&!r.stopped&&r.before.script.d[3]==7&&r.before.script.d[5]==1&&r.before.script.d[6]==WUI_END_SAVED);
    assert(r.before.owner.d[0]==4095&&r.before.owner.d[1]==WUI_REG_OK&&r.before.owner.d[2]==WUI_TYPE_OK);
    unsigned good_reads=r.reads,good_bytes=r.bytes;
    transition_registry_identity identity;
    for(unsigned i=0;i<10;i++) {
        wui_object o={0};wui_reader reader={0};const wui_api api={f,test_wui_read,test_wui_saved,test_wui_stop,test_wui_stub};
        wui_result one={0};reader.api=&api;reader.result=&one;reader.image=(uintptr_t)f->image;
        assert(wui_object_read(&reader,&o,(uintptr_t)f->objects[i],1));
        assert(transition_registry_once(read_local,f->image+0x6cf1500,o.q[1],o.q[0],&identity));
        assert(identity.registry==o.q[4]&&identity.page==o.q[5]&&identity.block==o.q[6]&&identity.runtime==o.q[7]);
    }
    if(f->stub_loaded) {
        r.boundary=3;
        for(unsigned p=0;p<2;p++) {r.phase_flags[p][0]=0x20;r.phase_flags[p][2]=1;for(unsigned i=0;i<4;i++) {double t=7.083;memcpy(&r.phase_clocks[p][i],&t,8);}}
        FILE *export=fopen(".local/native-lockstep/world-unknown-identity-c-export.log","wb");assert(export);
        world_values_unknown_event old={0};old.reason=6;old.reads=31;old.owned=old.ui=old.phase.eligible=old.phase.status=1;old.boot=2;old.raw_tag=old.owner.tag=0x66;old.thread=GetCurrentThreadId();
        old.exec=old.current=(uintptr_t)f->exec;old.raw_value=(uintptr_t)f->context+0xa8;old.owner.context=(uintptr_t)f->context;old.owner.lifetime=0x44;
        old.owner.entity=(uintptr_t)f->objects[0];old.owner.runtime=(1ULL<<25)|1;old.phase.app_flags=0x20;old.phase.view=1;
        old.phase.tls_time=old.phase.game=old.phase.real=old.phase.saved=r.phase_clocks[0][0];
        char row[2048];assert(world_values_unknown_format(&old,1,row,sizeof(row))>0);fprintf(export,"NATIVE_GATE %s\n",row);
        for(unsigned part=0;part<3;part++) {assert(world_unknown_identity_format(&r,1,part,row,sizeof(row))>0);fprintf(export,"NATIVE_GATE %s\n",row);}
        fprintf(export,"NATIVE_GATE WORLD_UNKNOWN_IDENTITY_STOP schema=1 samples=1 complete=1 rows=3 exported=3 open=0 closed=1 read_limit=512 byte_limit=16384 hop_limit=8\n");assert(!fclose(export));
    }
    for(unsigned n=1;n<=good_reads;n++) {
        test_wui_init(f);f->fail_at=n;test_wui_capture(f,&r);assert(r.error&WUI_READ);
        test_wui_init(f);f->stop_at=n;test_wui_capture(f,&r);assert(r.stopped&&(r.error&WUI_STOP));
        test_wui_init(f);f->mutate_at=n;test_wui_capture(f,&r);
        /* The final lifetime read happens before the final type reads. A
         * mutation after that is precisely the documented lease/ABA limit. */
        if(n>=4&&n<good_reads/2) assert(r.error&WUI_CHANGED);
    }
    for(unsigned stop=0;stop<2;stop++) {
        test_wui_init(f);f->pause_at=good_reads/2;
        f->paused=CreateEventW(NULL,TRUE,FALSE,NULL);f->resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(f->paused&&f->resume);
        HANDLE worker=CreateThread(NULL,0,test_wui_worker,f,0,NULL);assert(worker&&WaitForSingleObject(f->paused,2000)==WAIT_OBJECT_0);
        if(stop) f->stop_at=f->pause_at;else ++*(uint64_t*)(f->context+8);
        assert(SetEvent(f->resume)&&WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
        assert(f->threaded.error&(stop?WUI_STOP:WUI_CHANGED));
        CloseHandle(worker);CloseHandle(f->paused);CloseHandle(f->resume);
    }
    for(unsigned kind=0;kind<10;kind++) {
        test_wui_init(f);
        if(kind==0) *(uint16_t*)(f->page+24)=2;
        if(kind==1) *(uint32_t*)(f->page+28)&=~(5u<<16);
        if(kind==2) *(uintptr_t*)f->page=(uintptr_t)f->objects[1];
        if(kind==3) *(uint64_t*)(f->objects[0]+8)|=1ULL<<55;
        if(kind==4) *(uint32_t*)(f->image+0x40004)=8;
        if(kind==5) *(uintptr_t*)(f->image+0x30000+0x11e0)=UINTPTR_MAX-10;
        if(kind==6) *(uint32_t*)(f->image+0x70008)=0;
        if(kind==7) *(uint64_t*)(f->descriptor+0x28)=4097;
        if(kind==8) f->descriptor[0x10+9]='x';
        if(kind==9) f->nodes[0]++;
        test_wui_capture(f,&r);
        /* High full-ID bits are compared exactly to the live object. They
         * may be valid if every raw full-ID agrees; do not invent a mask. */
        if(kind==3) assert(!r.error);else assert(r.error);
    }
    test_wui_init(f);f->guard=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);assert(f->guard);
    *(uintptr_t*)(f->context+0x120)=(uintptr_t)f->guard;test_wui_capture(f,&r);assert(r.error&WUI_READ);VirtualFree(f->guard,0,MEM_RELEASE);
    for(unsigned end=0;end<4;end++) {
        test_wui_init(f);f->saved_runtime=0;
        if(end==0) *(uintptr_t*)(f->objects[0]+0x70)=0;
        if(end==1) *(uintptr_t*)(f->objects[1]+0x70)=(uintptr_t)f->objects[0];
        if(end>=2) for(unsigned i=0;i<9;i++) *(uintptr_t*)(f->objects[i]+0x70)=(uintptr_t)f->objects[i+1];
        if(end==3) *(uintptr_t*)f->objects[3]=1;
        test_wui_capture(f,&r);assert(!r.error);assert(r.before.script.d[6]==(end==0?WUI_END_NULL:end==1?WUI_END_CYCLE:end==2?WUI_END_LIMIT:WUI_END_UNSUPPORTED));
    }
    test_wui_init(f);f->saved_runtime=0;
    for(unsigned i=0;i<9;i++) *(uintptr_t*)(f->objects[i]+0x70)=(uintptr_t)f->objects[i+1];
    unsigned char long_name[96];memset(long_name,'x',95);long_name[95]=0;
    *(uintptr_t*)(f->descriptor+0x10)=(uintptr_t)long_name;*(uint64_t*)(f->descriptor+0x20)=95;*(uint64_t*)(f->descriptor+0x28)=95;
    memset(f->image+0x50010,'x',127);memcpy(f->image+0x50010,".?AV",4);f->image[0x50010+127]=0;
    test_wui_capture(f,&r);assert(!r.error&&r.reads==398&&r.before.script.d[5]==8&&r.before.script.d[6]==WUI_END_LIMIT);
    /* Existing unknown-owner admission/phase reads plus the new final guard
     * use at most85 additional guarded reads, each <=8 bytes. */
    assert(r.reads+85<=WUI_READS&&r.bytes+85*8<=WUI_BYTES);
    printf("unknown identity maximal heap-name/RTTI/8-hop repeat PASS (%u reads/%u bytes; guarded wrapper ceiling483 reads)\n",r.reads,r.bytes);
    test_wui_init(f);wui_object exact={0};wui_result rr={0};
    const wui_api registry_api={f,test_wui_read,test_wui_saved,test_wui_stop,test_wui_stub};wui_reader registry_reader={&registry_api,&rr,(uintptr_t)f->image};
    exact.q[0]=(uintptr_t)f->objects[0];exact.q[1]=*(uint64_t*)(f->objects[0]+8);*(uint64_t*)(f->objects[0]+8)^=1ULL<<55;
    assert(wui_registry(&registry_reader,&exact)==WUI_REG_ID);
    exact.q[1]=0;assert(wui_registry(&registry_reader,&exact)==WUI_REG_ENTRY);
    test_wui_init(f);const wui_api api={f,test_wui_read,test_wui_saved,test_wui_stop,test_wui_stub};wui_reader reader={&api,&r,(uintptr_t)f->image};uint64_t word;
    memset(&r,0,sizeof(r));r.reads=WUI_READS;assert(!wui_read(&reader,(uintptr_t)f->exec,&word,8)&&(r.error&WUI_BUDGET));
    memset(&r,0,sizeof(r));r.bytes=WUI_BYTES-7;assert(!wui_read(&reader,(uintptr_t)f->exec,&word,8)&&(r.error&WUI_BUDGET));
    memset(&r,0,sizeof(r));assert(!wui_at(&reader,UINT64_MAX-2,8,&word,8)&&(r.error&WUI_READ));
    memset(&r,0xff,sizeof(r));char row[2048];
    for(unsigned part=0;part<10;part++) {int n=world_unknown_identity_format(&r,UINT32_MAX,part,row,sizeof(row));assert(n>0&&(unsigned)n+32<sizeof(row));}
    VirtualFree(f->image,0,MEM_RELEASE);free(f);
    printf("unknown identity bounded/guarded reads, registry differential, noaccess, mutations and STOP PASS (%u reads/%u bytes baseline)\n",good_reads,good_bytes);
}
