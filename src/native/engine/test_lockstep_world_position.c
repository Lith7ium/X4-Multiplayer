/* Production observers exercised on owned memory only. No native game code runs. */
#include <stddef.h>
static void *test_tls(void);
static int test_read(const void*,void*,size_t);
#define WP_NATIVE_READER test_read
#define WP_NATIVE_TLS_READER test_tls
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"position check %u line%d: %s\n",checks,__LINE__,#x);exit(42);}}while(0)
static unsigned char context[0x160],entity[16],descriptor[0x60],exec_state[0x60],node[0x140],space[0x80],reference[0x80],registry[0x80],bank[32],table[0x140],tls[0x340],output[16],transform[64];
static uintptr_t program[2],nodes[234];static unsigned mode,native_calls[4],read_calls,fail_read,stop_read;static int with_reference;
static npc_provenance_frame provenance;
extern unsigned char wp_test_range[],wp_test_radius[],wp_test_frame_end[],wp_test_rng[];
extern unsigned char wp_test_optional_outer[],wp_test_optional_end[];
static unsigned real_frame;
static uintptr_t forward_entry;static unsigned forwarded;
static uintptr_t forward_outer(void *a,void *b,void *c,void *d){
    CHECK(a==(void*)0x1111222233334444ULL&&b==(void*)0x5555666677778888ULL&&c==(void*)0x9999aaaabbbbccccULL&&d==(void*)0xddddeeeeffff0000ULL);
    CHECK(forward_entry==(uintptr_t)_AddressOfReturnAddress());++forwarded;return 0xfedcba9876543210ULL;
}
static void forward_transform(void *a,void *b,double time,void *d,unsigned char fifth){
    uint64_t bits;memcpy(&bits,&time,8);CHECK(a==(void*)1&&b==(void*)2&&d==(void*)3&&bits==0x8000000000000000ULL&&fifth==0xa5);++forwarded;
}
static float forward_radius(void *a,void *b,void *c){CHECK(a==(void*)1&&b==(void*)2&&c==(void*)3);++forwarded;unsigned bits=0xffc12345;float f;memcpy(&f,&bits,4);return f;}
static float forward_sampler(void *a,float span,unsigned d,unsigned parameter){unsigned bits;memcpy(&bits,&span,4);CHECK(a==(void*)1&&bits==0x80000000&&d==0xf1234567&&parameter==0x87654321);++forwarded;bits=0x7fc67890;float f;memcpy(&f,&bits,4);return f;}
static uint64_t step(uint64_t x){x=x*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return(x>>30)|(x<<34);}
static void *test_tls(void){return tls;}
static int test_read(const void *address,void *out,size_t size){++read_calls;if(read_calls==stop_read)wp_closing=1;if(read_calls==fail_read)return 0;return read_local(address,out,size);}
static float from_bits(unsigned bits){float f;memcpy(&f,&bits,4);return f;}
static void throw_test(void){RaiseException(0xe042c0de,0,0,NULL);}
static float sampler(void *rng,float span,unsigned distribution,unsigned parameter){
    ++native_calls[3];CHECK(rng==tls+0x318&&wp_fbits(span)==0x42c80000&&distribution==1&&parameter==1);
    if(mode==3)throw_test();*(uint64_t*)rng=step(*(uint64_t*)rng);return from_bits(0x42200000);
}
static float radius(void *range,void *frame,void *optional){
    ++native_calls[2];CHECK(range==node+0x90&&frame==exec_state+0x38&&!optional);if(mode==2)throw_test();
    if(real_frame)return ((wp_radius_fn)(image+0xc59b00))(range,frame,optional);
    __declspec(align(16)) unsigned char storage[0x170]={0};unsigned char *owned_frame=storage+8+(mode==8);
    uintptr_t address=(uintptr_t)(image+0xc59951);memcpy(owned_frame,&address,8);
    const unsigned tag=5,span_bits=0x42c80000;memcpy(owned_frame+0x38,&tag,4);memcpy(owned_frame+0x40,&span_bits,4);memcpy(owned_frame+0x58,&tag,4);
    address=(uintptr_t)(image+0xc59b2f);memcpy(owned_frame+0x140,&address,8);
    address=(uintptr_t)range+(mode==7);memcpy(owned_frame+0x148,&address,8);address=(uintptr_t)frame;memcpy(owned_frame+0x150,&address,8);
    return wp_sampler_observe(sampler,tls+0x318,from_bits(0x42c80000),1,1,(uintptr_t)owned_frame,(uintptr_t)(image+0xc59951));
}
static void transform_original(void *s,void *out,double time,void *ref,unsigned char fifth){
    ++native_calls[1];uint64_t bits;memcpy(&bits,&time,8);CHECK(s==space&&out==transform&&bits==0x401c54fdf3b645a2ULL&&ref==(with_reference?reference:NULL)&&!fifth);
    if(mode==1)throw_test();for(unsigned i=0;i<16;i++)((unsigned char*)out)[i]=(unsigned char)i;
    if(mode==5)++*(uint64_t*)(context+8);if(mode==6)wp_closing=1;
}
static uintptr_t outer(void *d,void *out,void *frame,void *optional){
    ++native_calls[0];CHECK(d==node+0x28&&out==output&&frame==exec_state+0x38&&!optional);
    double time;uint64_t bits=0x401c54fdf3b645a2ULL;memcpy(&time,&bits,8);
    wp_transform_observe(transform_original,space,transform,time,with_reference?reference:NULL,0,(uintptr_t)(image+0xc6fb4d));
    memcpy(output,transform,16);
    CHECK(wp_fbits(wp_radius_observe(radius,node+0x90,frame,NULL,(uintptr_t)(image+0xc6fb6f),0,0))==0x42200000);
    *(uint64_t*)(tls+0x318)=step(step(*(uint64_t*)(tls+0x318)));memset(output,0x31,16);if(mode==4)throw_test();return (uintptr_t)out;
}
static void reset(void){
    memset(wp_events,0,sizeof(wp_events));wp_started=wp_enabled=1;wp_closing=0;wp_reserved=wp_exported=0;
    wp_calls=wp_open=wp_peak=wp_restore_open=wp_failures=wp_dropped=wp_sequence=0;wp_current=wp_radius_current=NULL;
    memset(native_calls,0,sizeof(native_calls));mode=read_calls=fail_read=stop_read=0;
    memset(context,0,sizeof(context));memset(exec_state,0,sizeof(exec_state));memset(node,0,sizeof(node));memset(tls,0,sizeof(tls));
    *(void**)context=image+0x2c6a270;*(uint64_t*)(context+8)=77;*(void**)(context+0x120)=entity;*(void**)(context+0xd8)=descriptor;
    *(uint64_t*)(entity+8)=0x100;*(void**)(descriptor+0x10)="order.fight.patrol";*(uint64_t*)(descriptor+0x20)=18;*(uint64_t*)(descriptor+0x28)=31;*(unsigned*)(descriptor+0x58)=10;
    *(void**)node=image+0x2c0cdd0;*(void**)(image+0x2c0cdd0+0x18)=image+0xbe0ba0;
    *(unsigned*)(node+0x28+0x48)=0x5b;*(unsigned*)(node+0x28+0x70)=2;
    for(unsigned i=0;i<234;i++)nodes[i]=(uintptr_t)node;program[0]=(uintptr_t)nodes;program[1]=(uintptr_t)(nodes+234);
    *(void**)(exec_state+0x10)=program;*(uint64_t*)(exec_state+0x18)=216;*(unsigned*)(exec_state+0x38)=0x66;*(void**)(exec_state+0x40)=context+0xa8;
    *(void**)(image+NPC_PROVENANCE_CURRENT)=exec_state;*(void**)(image+TABLE_RVA)=table;*(unsigned*)(image+0x6adf9fc)=0x20;*(unsigned*)(tls+0x314)=1;
    *(uint64_t*)(image+GAME_RVA)=*(uint64_t*)(image+REAL_RVA)=*(uint64_t*)(image+0x3d19790)=*(uint64_t*)(tls+0x300)=0x401c54fdf3b645a2ULL;
    *(uint64_t*)(tls+0x318)=123;stats.active=1;stats.issued=0;boot_mode=1;boot_phase=2;ui_thread=GetCurrentThreadId();main_thread=0;
    boot_started=GetTickCount64();boot_options=281474976710656ULL;npc_provenance_enabled=1;
    memset(&provenance,0,sizeof(provenance));provenance.readable=1;provenance.scope_phase=2;provenance.self.exec=(uintptr_t)exec_state;provenance.self.tag=0x66;
    provenance.self.value=(uintptr_t)context+0xa8;provenance.self.context=(uintptr_t)context;provenance.self.id=77;npc_provenance_current=&provenance;npc_provenance_overflow_depth=npc_provenance_denied_depth=0;
    memset(world_action_events,0,sizeof(world_action_events));world_action_current=&world_action_events[0];world_action_event *p=world_action_current;p->stage=2;p->thread=ui_thread;p->ui=1;p->boot=2;p->depth=1;
    world_action_guard *g=&p->before;g->node=(uintptr_t)node;g->exec=(uintptr_t)exec_state;g->frame=g->exec+0x38;g->current=g->exec;g->context=(uintptr_t)context;g->lifetime=77;g->entity=(uintptr_t)entity;g->runtime=0x100;g->saved=0x9d3a4;
    g->script=(uintptr_t)descriptor;g->program=(uintptr_t)program;g->begin=program[0];g->end=program[1];g->index=215;g->vtable=(uintptr_t)(image+0x2c0cdd0);g->action=(uintptr_t)(image+0xbe0ba0);g->version=10;g->tag=0x66;strcpy(g->name,"order.fight.patrol");
    p->phase_before.table=(uintptr_t)table;p->phase_before.game=0x401c54fdf3b645a2ULL;p->phase_before.app_flags=0x20;
    memset(space,0,sizeof(space));memset(reference,0,sizeof(reference));memset(bank,0,sizeof(bank));
    *(void**)space=image+0x30000;*(uint64_t*)(space+8)=(1ULL<<25)|1;*(void**)reference=image+0x30000;*(uint64_t*)(reference+8)=(1ULL<<25)|2;
    *(void**)(image+0x6cf1500)=registry;*(void**)(registry+0x58)=bank;*(void**)bank=space;*(void**)(bank+8)=reference;
    *(unsigned*)(bank+24)=0x00010001;*(unsigned*)(bank+28)=0x00550000;
    *(void**)(image+0x30000-8)=image+0x40000;*(void**)(image+0x30000+0x11e0)=image+0x60000;
    const unsigned col[]={1,0,0,0x50000,0x50080,0x40000};memcpy(image+0x40000,col,24);strcpy((char*)image+0x50010,".?AVZone@U@@");
    *(int32_t*)(image+0x60006)=0x70000-0x6000a;memset(image+0x70000,0,32);*(unsigned*)(image+0x70004)=2u<<(2*(22%16));
}
static void run(void){CHECK(wp_outer_observe(outer,node+0x28,output,exec_state+0x38,NULL,(uintptr_t)(image+0xbe0bd1),test_read,test_tls)==(uintptr_t)output);}
static void load_image(const char *path){
    FILE *f=fopen(path,"rb");CHECK(f&&!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>0&&!fseek(f,0,SEEK_SET));unsigned char *bytes=malloc((size_t)size);CHECK(bytes&&fread(bytes,1,(size_t)size,f)==(size_t)size&&!fclose(f));CHECK(hash_ok(bytes,(ULONG)size,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(void*)bytes;IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(image);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++){IMAGE_SECTION_HEADER *s=&sections[i];CHECK((uint64_t)s->PointerToRawData+s->SizeOfRawData<=(uint64_t)size);memcpy(image+s->VirtualAddress,bytes+s->PointerToRawData,s->SizeOfRawData);}
    free(bytes);CHECK(wp_pins_ok());for(unsigned i=0;i<sizeof(wp_pins)/sizeof(*wp_pins);i++){const world_action_pin *p=&wp_pins[i];image[p->rva+p->size-1]^=1;CHECK(!wp_pins_ok());image[p->rva+p->size-1]^=1;}
    memcpy(image+0x60000,image+0x34e4e0,36);
}
static unsigned protect_calls,flush_calls,inject_protect,inject_flush,persistent,stop_after;
static BOOL WINAPI test_protect(LPVOID address,SIZE_T size,DWORD permission,PDWORD old){
    ++protect_calls;if(inject_protect&&permission!=PAGE_EXECUTE_READWRITE){if(!persistent)inject_protect=0;return FALSE;}return VirtualProtect(address,size,permission,old);
}
static BOOL WINAPI test_flush(HANDLE process,LPCVOID address,SIZE_T size){++flush_calls;if(inject_flush){if(!persistent)inject_flush=0;return FALSE;}return FlushInstructionCache(process,address,size);}
static world_values_swap_result test_swap(unsigned i,unsigned __int128 expected,unsigned __int128 desired){world_values_swap_result r=wp_swap_with(i,expected,desired,test_protect,test_flush);if(stop_after==i+1)stats.active=0;return r;}
static int test_repair(unsigned i){return wp_repair_with(i,test_protect,test_flush);}
static int yes(void){return 1;}
static void bound_tests(void){
    unsigned char *page=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),copied[16];CHECK(page);DWORD old;
    CHECK(VirtualProtect(page,4096,PAGE_NOACCESS,&old));reset();wp_scope scope={0};scope.event=&wp_events[0];scope.reader=read_local;
    CHECK(!wp_read(&scope,(uintptr_t)page,copied,16)&&(scope.event->errors&WP_READ)&&scope.event->reads==1);
    CHECK(!wp_at(&scope,UINTPTR_MAX-2,4,copied,4));CHECK(scope.event->reads==1);
    scope.event->bytes=WP_BYTES-1;CHECK(!wp_read(&scope,(uintptr_t)output,copied,16)&&(scope.event->errors&WP_BUDGET));
    scope.event->bytes=0;scope.event->reads=WP_READS;CHECK(!wp_read(&scope,(uintptr_t)output,copied,1));
    CHECK(VirtualFree(page,0,MEM_RELEASE));
    reset();for(unsigned i=0;i<WP_CAP+2;i++)run();CHECK(wp_reserved==WP_CAP+2&&wp_ready()==WP_CAP&&wp_dropped==2&&wp_failures>=2&&!wp_open);
    for(unsigned i=0;i<4;i++)CHECK(native_calls[i]==WP_CAP+2);
}
static unsigned branch_transform_calls,branch_radius_calls,branch_no_sampler;
static float unsampled_radius(void *a,void *b,void *c){CHECK(a==node+0x90&&b==exec_state+0x38&&!c);++native_calls[2];return 0;}
static uintptr_t branch_original(void *a,void *b,void *c,void *d){
    ++native_calls[0];CHECK(a==node+0x28&&b==output&&c==exec_state+0x38&&!d);memset(output,0,16);
    double time;uint64_t bits=0x401c54fdf3b645a2ULL;memcpy(&time,&bits,8);
    for(unsigned i=0;i<branch_transform_calls;i++){wp_transform_observe(transform_original,space,transform,time,NULL,0,(uintptr_t)(image+0xc6fb4d));memcpy(output,transform,16);}
    for(unsigned i=0;i<branch_radius_calls;i++){
        float r=wp_radius_observe(branch_no_sampler?unsampled_radius:radius,node+0x90,c,NULL,(uintptr_t)(image+0xc6fb6f),0,0);
        if(r>0){*(uint64_t*)(tls+0x318)=step(step(*(uint64_t*)(tls+0x318)));memset(output,0x31,16);}
    }return (uintptr_t)b;
}
static void optional_branch_tests(void){
    /* space tag, range kind, transform calls, radius calls, unsampled, accepts */
    const unsigned cases[][6]={{0,2,0,1,0,1},{0x5b,0,1,0,0,1},{0,0,0,0,0,1},
        {0,2,1,1,0,0},{0x5b,0,0,0,0,0},{0,0,0,1,0,0},{0,2,0,0,0,0},
        {0x5b,2,2,1,0,0},{0x5b,2,1,2,0,0},{0x5b,2,1,1,1,0},{0x5b,1,1,1,1,0},{0x5b,3,1,1,1,0}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){
        reset();with_reference=0;const unsigned *c=cases[i];*(unsigned*)(node+0x70)=c[0];*(unsigned*)(node+0x98)=c[1];
        branch_transform_calls=c[2];branch_radius_calls=c[3];branch_no_sampler=c[4];
        CHECK(wp_outer_observe(branch_original,node+0x28,output,exec_state+0x38,NULL,(uintptr_t)(image+0xbe0bd1),test_read,test_tls)==(uintptr_t)output);
        wp_event *e=&wp_events[0];CHECK((!e->errors)==c[5]&&e->complete&&!wp_open&&!wp_current&&!wp_radius_current);
        CHECK(e->calls[0]==1&&e->returns[0]==1&&e->calls[1]==c[2]&&e->returns[1]==c[2]&&e->calls[2]==c[3]&&e->returns[2]==c[3]);
        CHECK(native_calls[0]==1&&native_calls[1]==c[2]&&native_calls[2]==c[3]&&native_calls[3]==(c[4]?0:c[3]));
    }
    puts("optional native branch policy: absent/executed, forbidden/repeated/missing calls and unsupported unsampled paths PASS");
    with_reference=1;
}
static uintptr_t assigned(void *a,void *b,void *c,void *d){CHECK(a==node+0xf8&&b==exec_state+0x38&&c==node+0x108&&wp_d(d)==0x63);return 0x123456789abcde01ULL;}
static void action_original(void *n,void *exec,void *frame){
    CHECK(n==node&&exec==exec_state&&frame==exec_state+0x38);run();unsigned char value[16]={0},position[0x30]={0};*(unsigned*)position=1;memcpy(position+0x20,output,16);*(unsigned*)value=0x63;*(void**)(value+8)=position;
    CHECK(world_values_assign_observe(assigned,node+0xf8,frame,node+0x108,value,(uintptr_t)image+0xbe0c8b,read_local,test_tls)==0x123456789abcde01ULL);
}
static void export_test(const char *path){
    reset();world_action_current=NULL;world_action_calls=world_action_reserved=world_action_sequence=world_action_failures=world_action_dropped=0;
    npc_provenance_early_eligible=yes;
    world_action_other_owner=world_action_other_script=world_action_other_context=world_action_other_phase=world_action_unknown=world_action_threads=world_action_unknown_rng=0;
    world_action_open=world_action_peak=world_action_restore_open=0;world_action_depth=0;world_action_enabled=world_action_started=1;world_action_closing=0;
    const char *manifest="000000000009d3a4,10,order.fight.patrol";unsigned count=0;CHECK(world_action_parse(manifest,strlen(manifest),world_action_selected,&count)&&count==1);world_action_selected_count=count;
    memset(world_values_events,0,sizeof(world_values_events));world_values_reserved=world_values_calls=world_values_open=world_values_failures=world_values_dropped=world_values_sequence=0;
    world_values_peak=world_values_restore_open=world_values_other_scope=world_values_other_type=0;world_values_enabled=world_values_started=1;world_values_closing=0;
    world_action_observe_with_tls(action_original,node,exec_state,exec_state+0x38,test_tls);
    printf("position parent export action=%u values=%u position=%u owner=%llu script=%llu unknown=%llu error=%u\n",world_action_ready(),world_values_ready(),wp_ready(),(unsigned long long)world_action_other_owner,(unsigned long long)world_action_other_script,(unsigned long long)world_action_unknown,world_action_events[0].errors);
    CHECK(world_action_ready()==1&&world_values_ready()==1&&wp_ready()==1);
    CHECK(!world_action_events[0].errors&&!world_values_events[0].errors&&!wp_events[0].errors);
    world_action_closing=world_values_closing=wp_closing=1;world_action_exported=world_values_exported=3;wp_exported=WP_ROWS;
    wp_patch_failures=wp_patch_errors=0;world_values_sites=0;world_values_patch_failures=world_values_patch_errors=0;
    char row[2048];FILE *file=path?fopen(path,"wb"):NULL;if(path)CHECK(file);
    for(unsigned i=0;i<3;i++){CHECK(world_action_format(&world_action_events[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(world_action_status(row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE WORLD_ACTION_STOP %s\n",row);
    for(unsigned i=0;i<3;i++){CHECK(world_values_format(&world_values_events[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(world_values_status(row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE WORLD_VALUES_STOP %s\n",row);
    for(unsigned i=0;i<WP_ROWS;i++){CHECK(wp_format(&wp_events[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(wp_status(row,sizeof(row))>0);if(file){fprintf(file,"NATIVE_GATE WORLD_POSITION_STOP %s\n",row);CHECK(!fclose(file));}
}
static void production_frame_tests(void){
    DWORD old;CHECK(VirtualProtect(image+0xc58000,0x2000,PAGE_READWRITE,&old));
    CHECK(VirtualProtect(image+WP_SAMPLER,0x1000,PAGE_READWRITE,&old));
    size_t size=(size_t)(wp_test_frame_end-wp_test_range);CHECK(size<0x1000&&wp_test_radius-wp_test_range==0xd60);
    memcpy(image+0xc58da0,wp_test_range,size);*(void**)(image+0xc58da0+(wp_test_rng-wp_test_range))=tls+0x318;
    *(int32_t*)(image+0xc5994d)=WP_SAMPLER-0xc59951;
    const unsigned char prefix[]={0x48,0x89,0x6c,0x24,0x10};memcpy(image+WP_SAMPLER,prefix,5);
    thunk_absolute_jump(image+WP_SAMPLER+5,(void*)sampler);
    /* Register real unwind descriptions for the authored seven-push/0x100
     * frame and its caller. No copied native code or guessed unwind model. */
    RUNTIME_FUNCTION functions[2];unsigned char *starts[]={wp_test_range,wp_test_radius};unsigned rvas[]={0xc58da0,0xc59b00};
    for(unsigned i=0;i<2;i++){DWORD64 base=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)starts[i],&base,NULL);CHECK(f);
        unsigned char *u=(unsigned char*)(base+f->UnwindData);CHECK((u[0]&0xf8)==0&&u[2]<24);
        unsigned n=4+((u[2]+1)&~1u)*2;memcpy(image+0x81000+i*64,u,n);
        functions[i]=(RUNTIME_FUNCTION){rvas[i],rvas[i]+f->EndAddress-f->BeginAddress,0x81000+i*64};}
    CHECK(RtlAddFunctionTable(functions,2,(DWORD64)image));
    CHECK(VirtualProtect(image+0xc58000,0x2000,PAGE_EXECUTE_READ,&old));CHECK(VirtualProtect(image+WP_SAMPLER,0x1000,PAGE_EXECUTE_READ,&old));
    wp_patch *p=&wp_patches[3];p->owned=p->protection_pending=p->flush_pending=0;p->original=0;memcpy(&p->original,image+WP_SAMPLER,8);p->patched=p->original;
    unsigned char *bytes=(void*)&p->patched;bytes[0]=0xe9;*(int32_t*)(bytes+1)=(int32_t)((thunk+3936)-(image+WP_SAMPLER+5));
    CHECK(wp_swap(3,p->original,p->patched).ok);p->owned=1;real_frame=1;
    reset();run();CHECK(!wp_events[0].errors&&wp_events[0].sample[0].caller==(uintptr_t)image+0xc59951&&wp_events[0].sample[0].parent_return==(uintptr_t)image+0xc59b2f);
    CHECK(wp_events[0].sample[0].slot>=wp_events[0].sample[0].stack_limit&&wp_events[0].sample[0].normal);
    reset();mode=3;unsigned caught=0;__try{run();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
    CHECK(caught&&wp_events[0].errors&WP_UNWIND);CHECK(!wp_current&&!wp_radius_current&&!wp_open&&wp_events[0].complete);
    for(unsigned i=0;i<4;i++)CHECK(native_calls[i]==1);
    world_position_restore();CHECK(!p->owned);CHECK(RtlDeleteFunctionTable(functions));real_frame=0;
    puts("production sampler wrapper: real incoming T, owned seven-push frame, original-once and foreign exception PASS");
}
static void production_optional_frame_tests(void){
    DWORD old;CHECK(VirtualProtect(image+WP_OUTER,0x500,PAGE_READWRITE,&old));CHECK(VirtualProtect(image+0xbe0bb0,0x40,PAGE_READWRITE,&old));
    CHECK(VirtualProtect(image+0xc59b00,32,PAGE_READWRITE,&old));
    size_t size=(size_t)(wp_test_optional_end-wp_test_optional_outer);CHECK(size<0x500);
    memcpy(image+WP_OUTER,wp_test_optional_outer,size);*(int32_t*)(image+0xc6fb6b)=0xc59b00-0xc6fb6f;
    thunk_absolute_jump(image+0xc59b00,(void*)radius);
    memset(image+0xbe0bb0,0x90,0x40);const unsigned char begin[]={0x48,0x83,0xec,0x28},end[]={0x48,0x83,0xc4,0x28,0xc3};
    memcpy(image+0xbe0bb0,begin,4);image[0xbe0bcc]=0xe8;*(int32_t*)(image+0xbe0bcd)=WP_OUTER-0xbe0bd1;memcpy(image+0xbe0bd1,end,5);
    const unsigned char caller_unwind[]={1,4,1,0,4,0x42,0,0};memcpy(image+0x81200,caller_unwind,8);
    DWORD64 base=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)wp_test_optional_outer,&base,NULL);CHECK(f);
    unsigned char *u=(unsigned char*)(base+f->UnwindData);CHECK((u[0]&0xf8)==0&&u[2]<28);
    unsigned n=4+((u[2]+1)&~1u)*2;memcpy(image+0x81240,u,n);
    RUNTIME_FUNCTION functions[2]={{0xbe0bb0,0xbe0bd6,0x81200},{WP_OUTER,WP_OUTER+(DWORD)size,0x81240}};
    CHECK(RtlAddFunctionTable(functions,2,(DWORD64)image));
    CHECK(VirtualProtect(image+WP_OUTER,0x500,PAGE_EXECUTE_READ,&old));CHECK(VirtualProtect(image+0xbe0bb0,0x40,PAGE_EXECUTE_READ,&old));CHECK(VirtualProtect(image+0xc59b00,32,PAGE_EXECUTE_READ,&old));
    CHECK(thunk_slot_ready(3776)&&thunk_slot_ready(3872));
    const unsigned indices[]={0,2};
    for(unsigned j=0;j<2;j++){unsigned i=indices[j];wp_patch *p=&wp_patches[i];p->original=0;memcpy(&p->original,image+p->rva,p->size);p->patched=p->original;
        unsigned char *b=(void*)&p->patched;if(!i){b[0]=0xe9;*(int32_t*)(b+1)=(int32_t)((thunk+3808)-(image+WP_OUTER+5));}
        else *(int32_t*)(b+11)=(int32_t)((thunk+3872)-(image+0xc6fb6f));
        CHECK(wp_swap(i,p->original,p->patched).ok);p->owned=1;}
    reset();CHECK(((wp_outer_fn)(image+0xbe0bb0))(node+0x28,output,exec_state+0x38,NULL)==(uintptr_t)output);
    wp_event *e=&wp_events[0];const unsigned want[]={1,0,1,1};CHECK(!e->errors&&e->complete&&!wp_open&&!wp_current&&!wp_radius_current);
    CHECK(!memcmp(e->calls,want,sizeof(want))&&!memcmp(e->returns,want,sizeof(want)));
    CHECK(e->radius_witness[5]==e->radius_witness[0]+0x138&&e->radius_witness[3]==(uintptr_t)image+0xc6fb6f&&!e->radius_witness[6]&&!e->radius_witness[7]);
    CHECK(e->radius_witness[4]!=(uintptr_t)image+0xbe0bd1); /* Original was called by the production wrapper. */
    unsigned reads=e->reads;printf("optional production incoming frame: reads=%u bytes=%u\n",reads,e->bytes);
    for(unsigned i=1;i<=reads;i++){reset();fail_read=i;((wp_outer_fn)(image+0xbe0bb0))(node+0x28,output,exec_state+0x38,NULL);CHECK(e->errors&&e->complete&&!wp_open);CHECK(native_calls[2]==1&&native_calls[3]==1);}
    for(unsigned i=1;i<=reads;i++){reset();stop_read=i;((wp_outer_fn)(image+0xbe0bb0))(node+0x28,output,exec_state+0x38,NULL);CHECK(e->errors&WP_STOP);CHECK(e->complete&&!wp_open&&native_calls[2]==1&&native_calls[3]==1);}
    /* Mutate the owned body's native-produced RAX pointer, not a declared
     * producer success flag. The real modified thunk must forward the bad value. */
    unsigned lea=0;const unsigned char pattern[]={0x48,0x8d,0x45,0x67};
    for(unsigned i=7;i+4<size;i++)if(!memcmp(image+WP_OUTER+i,pattern,4)){CHECK(!lea);lea=i;}
    CHECK(lea);CHECK(VirtualProtect(image+WP_OUTER+lea,4,PAGE_READWRITE,&old));image[WP_OUTER+lea+3]=0x68;
    CHECK(VirtualProtect(image+WP_OUTER+lea,4,PAGE_EXECUTE_READ,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),image+WP_OUTER+lea,4));
    reset();((wp_outer_fn)(image+0xbe0bb0))(node+0x28,output,exec_state+0x38,NULL);CHECK((e->errors&WP_SCOPE)&&e->complete&&!wp_open&&native_calls[2]==1&&native_calls[3]==1);
    CHECK(VirtualProtect(image+WP_OUTER+lea,4,PAGE_READWRITE,&old));image[WP_OUTER+lea+3]=0x67;
    CHECK(VirtualProtect(image+WP_OUTER+lea,4,PAGE_EXECUTE_READ,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),image+WP_OUTER+lea,4));
    reset();mode=2;unsigned caught=0;__try{((wp_outer_fn)(image+0xbe0bb0))(node+0x28,output,exec_state+0x38,NULL);}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
    CHECK(caught&&(e->errors&WP_UNWIND)&&e->complete&&!wp_open&&!wp_current&&!wp_radius_current);CHECK(native_calls[2]==1&&!native_calls[3]&&!e->returns[0]&&!e->returns[2]);
    world_position_restore();CHECK(!wp_patch_mask(0));CHECK(RtlDeleteFunctionTable(functions));
    puts("production outer wrapper/trampoline/body/forwarded RAX/radius wrapper: incoming-frame null witness, guarded failures, STOP and foreign SEH PASS");
}
static void production_forward_tests(void){
    DWORD old;const unsigned rvas[]={WP_OUTER,0x3db8a0,0xc59b00,WP_SAMPLER,0xc6fb30};
    for(unsigned i=0;i<5;i++)CHECK(VirtualProtect(image+rvas[i],128,PAGE_READWRITE,&old));
    const unsigned char outer_prefix[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};memcpy(image+WP_OUTER,outer_prefix,7);
    unsigned char *at=image+WP_OUTER+7;at[0]=0x49;at[1]=0xbb;*(uintptr_t*)(at+2)=(uintptr_t)&forward_entry;at[10]=0x49;at[11]=0x89;at[12]=0x03;thunk_absolute_jump(at+13,(void*)forward_outer);
    thunk_absolute_jump(image+0x3db8a0,(void*)forward_transform);thunk_absolute_jump(image+0xc59b00,(void*)forward_radius);thunk_absolute_jump(image+WP_SAMPLER+5,(void*)forward_sampler);
    /* Authored callers retain the selected native CALL addresses, so the real
     * CAS displacement and fifth stack argument are exercised together. */
    memset(image+0xc6fb30,0x90,0x50);const unsigned char transform_caller[]={0x48,0x83,0xec,0x38,0x8a,0x44,0x24,0x60,0x88,0x44,0x24,0x20};memcpy(image+0xc6fb30,transform_caller,sizeof(transform_caller));
    image[0xc6fb48]=0xe8;*(int32_t*)(image+0xc6fb49)=0x3db8a0-0xc6fb4d;
    const unsigned char transform_end[]={0x48,0x83,0xc4,0x38,0xc3};memcpy(image+0xc6fb4d,transform_end,5);
    const unsigned char radius_caller[]={0x48,0x83,0xec,0x28};memcpy(image+0xc6fb60,radius_caller,4);image[0xc6fb6a]=0xe8;*(int32_t*)(image+0xc6fb6b)=0xc59b00-0xc6fb6f;
    const unsigned char radius_end[]={0x48,0x83,0xc4,0x28,0xc3};memcpy(image+0xc6fb6f,radius_end,5);
    for(unsigned i=0;i<5;i++)CHECK(VirtualProtect(image+rvas[i],128,PAGE_EXECUTE_READ,&old));
    CHECK(wp_prepare());for(unsigned i=0;i<4;i++){CHECK(wp_swap(i,wp_patches[i].original,wp_patches[i].patched).ok);wp_patches[i].owned=1;}
    reset();world_action_current=NULL;forwarded=0;
    CHECK(((wp_outer_fn)(image+WP_OUTER))((void*)0x1111222233334444ULL,(void*)0x5555666677778888ULL,(void*)0x9999aaaabbbbccccULL,(void*)0xddddeeeeffff0000ULL)==0xfedcba9876543210ULL);
    double time;uint64_t bits=0x8000000000000000ULL;memcpy(&time,&bits,8);
    ((wp_transform_fn)(image+0xc6fb30))((void*)1,(void*)2,time,(void*)3,0xa5);
    CHECK(wp_fbits(((wp_radius_fn)(image+0xc6fb60))((void*)1,(void*)2,(void*)3))==0xffc12345);
    CHECK(wp_fbits(((wp_sampler_fn)(image+WP_SAMPLER))((void*)1,from_bits(0x80000000),0xf1234567,0x87654321))==0x7fc67890);
    CHECK(forwarded==4&&!wp_reserved&&!wp_open&&!wp_current&&!wp_radius_current);world_position_restore();CHECK(!wp_patch_mask(0));
    puts("production four-site forwarding: full RAX/replayed RAX, XMM2, fifth byte, opaque XMM0 and foreign scope PASS");
}
static void configure_reset(void){
    reset();wp_started=wp_enabled=0;wp_patch_failures=wp_patch_errors=0;boot_phase=0;stop_after=0;
    for(unsigned i=0;i<4;i++){wp_patches[i].owned=wp_patches[i].protection_pending=wp_patches[i].flush_pending=0;}
    world_action_started=world_action_enabled=1;world_action_closing=0;world_action_failures=world_action_dropped=0;
    strcpy(world_action_manifest,"9d3a4,10,order.fight.patrol");world_values_started=world_values_enabled=world_values_closing=0;
    world_values_failures=world_values_dropped=world_values_patch_failures=world_values_patch_errors=0;
    CHECK(world_values_configure_using(yes,world_values_prepare,world_values_swap));
    protect_calls=flush_calls=inject_protect=inject_flush=persistent=0;
}
static void retry(unsigned expected){
    char a[1024],b[1024];unsigned p=protect_calls,f=flush_calls;wp_event copy[WP_CAP];memcpy(copy,wp_events,sizeof(copy));
    CHECK(wp_status(a,sizeof(a))>0);CHECK((unsigned)world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair)==expected);
    CHECK(wp_status(b,sizeof(b))>0&&!strcmp(a,b)&&!memcmp(copy,wp_events,sizeof(copy))&&p==protect_calls&&f==flush_calls);
}
static void patch_tests(void){
    thunk=make_thunk();CHECK(thunk&&thunk_page_rx(thunk));
    for(unsigned i=0;i<4;i++){DWORD old;CHECK(VirtualProtect(image+wp_patches[i].rva,wp_patches[i].size,PAGE_EXECUTE_READ,&old));}
    configure_reset();CHECK(world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair));CHECK(wp_patch_mask(0)==15&&wp_pins_ok());
    for(unsigned phase=1;phase<=3;phase++){boot_phase=phase;retry(1);}
    for(unsigned kind=0;kind<14;kind++){
        LONG64 save_fail=wp_failures;unsigned save_export=wp_exported;wp_exported=7;
        if(kind==0)wp_failures=1;if(kind==1)world_values_failures=1;if(kind==2)world_action_closing=1;
        if(kind==3)++secret[0];if(kind==4)++initial_state_digest[0];if(kind==5)++boot_options;if(kind==6)++boot_started;
        if(kind==7)++stats.slot;if(kind==8)++remote_port;if(kind==9)++world_action_manifest[0];
        if(kind==10)world_values_sites^=1;if(kind==11)world_action_unknown_callback=NULL;if(kind==12)wp_patches[0].flush_pending=1;if(kind==13)wp_closing=1;
        retry(0);CHECK(wp_exported==7);
        if(kind==0)wp_failures=save_fail;if(kind==1)world_values_failures=0;if(kind==2)world_action_closing=0;
        if(kind==3)--secret[0];if(kind==4)--initial_state_digest[0];if(kind==5)--boot_options;if(kind==6)--boot_started;
        if(kind==7)--stats.slot;if(kind==8)--remote_port;if(kind==9)--world_action_manifest[0];
        if(kind==10)world_values_sites^=1;if(kind==11)world_action_unknown_callback=world_values_unknown_capture;if(kind==12)wp_patches[0].flush_pending=0;if(kind==13)wp_closing=0;wp_exported=save_export;
    }
    world_position_restore();CHECK(!wp_patch_mask(0)&&!wp_patch_mask(1)&&!wp_patch_mask(2));world_values_restore();
    for(unsigned site=0;site<4;site++){
        configure_reset();CHECK(world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair));
        wp_patch *p=&wp_patches[site];DWORD old;CHECK(VirtualProtect(image+p->rva,p->size,PAGE_READWRITE,&old));image[p->rva+p->size-1]^=1;
        unsigned char foreign[16];memcpy(foreign,image+p->rva,p->size);CHECK(VirtualProtect(image+p->rva,p->size,PAGE_EXECUTE_READ,&old));retry(0);
        world_position_restore();CHECK(p->owned&&!memcmp(foreign,image+p->rva,p->size)&&wp_patch_failures);
        CHECK(VirtualProtect(image+p->rva,p->size,PAGE_READWRITE,&old));memcpy(image+p->rva,&p->patched,p->size);CHECK(VirtualProtect(image+p->rva,p->size,PAGE_EXECUTE_READ,&old));
        world_position_restore();CHECK(!wp_patch_mask(0));world_values_restore();
    }
    for(unsigned stop=1;stop<=4;stop++){configure_reset();stop_after=stop;CHECK(!world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair));CHECK(wp_closing&&!wp_enabled&&!wp_patch_mask(0)&&!wp_patch_mask(1)&&!wp_patch_mask(2));world_values_restore();}
    stats.active=1;stop_after=0;CHECK(wp_prepare());
    for(unsigned i=0;i<4;i++)for(unsigned phase=0;phase<2;phase++)for(unsigned fault=0;fault<2;fault++)for(unsigned persist=0;persist<2;persist++){
        wp_patch *p=&wp_patches[i];wp_patch_failures=wp_patch_errors=0;wp_closing=0;inject_protect=inject_flush=0;persistent=persist;
        world_values_swap_result r;
        if(phase){r=wp_swap_with(i,p->original,p->patched,test_protect,test_flush);CHECK(r.ok);p->owned=1;}
        if(fault)inject_flush=1;else inject_protect=1;
        if(!phase){r=wp_swap_with(i,p->original,p->patched,test_protect,test_flush);CHECK(r.changed&&!r.ok);p->owned=1;}
        wp_restore_locked(test_swap,test_repair);CHECK(wp_patch_failures);
        inject_protect=inject_flush=persistent=0;wp_restore_locked(test_swap,test_repair);
        unsigned __int128 actual=0;MEMORY_BASIC_INFORMATION memory;
        CHECK(read_local(image+p->rva,&actual,p->size)&&actual==p->original&&!p->owned&&!p->protection_pending&&!p->flush_pending);
        CHECK(VirtualQuery(image+p->rva,&memory,sizeof(memory))&&memory.Protect==PAGE_EXECUTE_READ&&wp_patch_failures);
    }
    puts("position exact pins/owned CAS/readonly reattach/STOP install/32 cleanup-debt cases pass");
}
int main(int argc,char **argv){
    CHECK(argc==2||argc==3);load_image(argv[1]);world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));CHECK(world_identities);
    CHECK(world_identity_record(0x100,0x9d3a4));CHECK(world_identity_record((1ULL<<25)|1,0x3fd));CHECK(world_identity_record((1ULL<<25)|2,0x3fc));
    for(unsigned ref=0;ref<2;ref++){with_reference=ref;reset();run();wp_event *e=&wp_events[0];
        printf("position supported ref=%u reads=%u bytes=%u errors=%u\n",ref,e->reads,e->bytes,e->errors);
        CHECK(e->complete&&!e->errors&&!wp_open&&!wp_current&&!wp_radius_current);CHECK(*(uint64_t*)(tls+0x318)==step(step(step(123))));
        for(unsigned i=0;i<4;i++)CHECK(native_calls[i]==1&&e->calls[i]==1&&e->returns[i]==1);
        unsigned char encoded[WP_WIRE_SIZE];CHECK(wp_encode(e,1,encoded)==WP_WIRE_SIZE);char row[2048];for(unsigned i=0;i<WP_ROWS;i++)CHECK(wp_format(e,1,i,row,sizeof(row))>0&&strlen(row)<2048);
    }
    unsigned good_reads=wp_events[0].reads;
    for(unsigned i=1;i<=good_reads;i++){reset();fail_read=i;run();CHECK(wp_events[0].complete&&wp_events[0].errors&&!wp_open);for(unsigned j=0;j<4;j++)CHECK(native_calls[j]==1);}
    for(unsigned i=1;i<=good_reads;i++){reset();stop_read=i;run();CHECK(wp_events[0].complete&&(wp_events[0].errors&WP_STOP)&&!wp_open);for(unsigned j=0;j<4;j++)CHECK(native_calls[j]==1);}
    for(unsigned m=1;m<=4;m++){reset();mode=m;unsigned caught=0;__try{run();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
        CHECK(caught&&wp_events[0].complete&&(wp_events[0].errors&WP_UNWIND)&&!wp_open&&!wp_current&&!wp_radius_current);
        CHECK(native_calls[0]==1&&native_calls[1]==1&&native_calls[2]==(m!=1)&&native_calls[3]==(m!=1&&m!=2));}
    for(unsigned m=5;m<=8;m++){reset();mode=m;run();CHECK(wp_events[0].complete&&wp_events[0].errors&&!wp_open);if(m==8){CHECK(wp_events[0].errors&WP_SCOPE);for(unsigned i=0;i<4;i++)CHECK(native_calls[i]==1);}}
    reset();world_action_current=NULL;run();CHECK(!wp_reserved&&!wp_open&&!wp_current&&!wp_radius_current);
    bound_tests();optional_branch_tests();export_test(argc==3?argv[2]:NULL);
    puts("production position owned-memory capture/original-once/refusal/exception checks pass");patch_tests();production_frame_tests();production_optional_frame_tests();production_forward_tests();printf("checks=%u\n",checks);return 0;
}
