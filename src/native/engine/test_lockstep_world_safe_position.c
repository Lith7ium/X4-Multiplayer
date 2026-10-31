/* Production passive callbacks through an authored native-shaped caller chain.
 * The exact image is read-only input to pins; only owned replacements execute. */
#define main position_fixture_main
#include "test_lockstep_world_position.c"
#undef main
extern unsigned char gps_test_action[],gps_test_action_end[],gps_test_evaluator[],gps_test_evaluator_end[];
extern unsigned char gps_test_outer[],gps_test_outer_end[],gps_test_outer_data_end[],gps_test_enter[],gps_test_finish[];
static RUNTIME_FUNCTION gps_functions[5];
static unsigned detached,finish_order;
static HANDLE gps_entered,gps_release;static unsigned gps_worker_mode,gps_worker_caught;
static uintptr_t gps_foreign_original(void *a,void *b,void *c,void *d){(void)a;(void)c;(void)d;return (uintptr_t)b;}
static void gps_rng_finish_probe(wp_scope *s,unsigned abnormal){(void)s;(void)abnormal;CHECK(finish_order==0&&gps_current);finish_order=1;}
static void gps_rng_begin_probe(wp_scope *s){s->rng_finish_callback=gps_rng_finish_probe;}
static void gps_passive_finish_probe(wp_scope *s,unsigned abnormal){CHECK(finish_order==1&&wp_current==s&&wp_open==1);gps_finish(s,abnormal);CHECK(!gps_current&&!gps_open);finish_order=2;}
static wp_passive_callbacks gps_order_callbacks;
static unsigned gps_tib_edge;
static void gps_mutate_outer_tib(gps_record *r){
    if(gps_tib_edge&1)r->witness[0][1]-=4096;
    if(gps_tib_edge&2)r->witness[0][2]+=4096;
}
static void gps_tib_sample_begin(wp_scope *scope,void *rng,float span,unsigned distribution,unsigned parameter,uintptr_t caller,uintptr_t t){
    gps_record *r=scope->passive_record;CHECK(r);uint64_t lo=r->witness[0][1],hi=r->witness[0][2];
    gps_mutate_outer_tib(r);gps_sample_begin(scope,rng,span,distribution,parameter,caller,t);
    r->witness[0][1]=lo;r->witness[0][2]=hi;
}
static void gps_tib_sample_normal(wp_scope *scope,float value){
    gps_record *r=scope->passive_record;CHECK(r);uint64_t lo=r->witness[0][1],hi=r->witness[0][2];
    gps_mutate_outer_tib(r);gps_sample_normal(scope,value);r->witness[0][1]=lo;r->witness[0][2]=hi;
}


static void gps_owned_enter(void){++native_calls[0];if(mode==9||mode==10){unsigned throwing=mode==10;CHECK(SetEvent(gps_entered));CHECK(WaitForSingleObject(gps_release,5000)==WAIT_OBJECT_0);if(throwing)mode=3;}if(mode==1)throw_test();if(mode==5){wp_passive=NULL;detached=1;}if(mode==6)world_safe_position_stop();}
static void gps_owned_finish(void *out){
    *(uint64_t*)(tls+0x318)=step(step(*(uint64_t*)(tls+0x318)));
    if(mode==7)wp_radius_observe((wp_radius_fn)(image+0xc59b00),node+0x90,exec_state+0x38,NULL,(uintptr_t)image+0xc6fb6f,0,0);
    if(mode==8)wp_outer_observe(gps_foreign_original,node+0x28,out,exec_state+0x38,reference,(uintptr_t)image+0xc7284a,test_read,test_tls);
    const float result[4]={40,20,30,0};memcpy(out,result,16);if(mode==4)throw_test();
}
static void gps_owned_reset(void){
    reset();memset(gps_records,0,sizeof(gps_records));gps_started=gps_enabled=1;gps_closing=0;
    gps_calls=gps_open=gps_peak=gps_restore_open=gps_failures=gps_dropped=gps_sequence=0;gps_reserved=gps_exported=0;
    gps_current=NULL;wp_passive=&gps_callbacks;wp_rng_begin=NULL;wp_rng_sample=NULL;wp_rng_finish=NULL;detached=finish_order=0;
    memset(descriptor,0,sizeof(descriptor));memcpy(descriptor+0x10,"move.idle",9);*(uint64_t*)(descriptor+0x20)=9;*(uint64_t*)(descriptor+0x28)=15;*(unsigned*)(descriptor+0x58)=4;
    *(void**)node=image+0x2c0cc88;*(void**)(image+0x2c0cc88+0x18)=image+0xbe2320;*(uint64_t*)(exec_state+0x18)=52;
    *(unsigned*)(node+0x70)=0;
    world_action_event *p=world_action_current;p->stage=1;world_action_guard *g=&p->before;
    g->saved=0x80d54;g->index=51;g->vtable=(uintptr_t)image+0x2c0cc88;g->action=(uintptr_t)image+0xbe2320;g->version=4;strcpy(g->name,"move.idle");
}
static uintptr_t gps_owned_run(void){return ((wp_outer_fn)(image+0xbe2320))(node+0x28,output,exec_state+0x38,reference);}
static void gps_copy_frame(unsigned i,unsigned rva,unsigned char *start,unsigned char *end){
    CHECK(end>start&&(size_t)(end-start)<0x2000);memcpy(image+rva,start,(size_t)(end-start));
    DWORD64 base=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)start,&base,NULL);CHECK(f);
    unsigned char *u=(unsigned char*)(base+f->UnwindData);CHECK((u[0]&0xf8)==0&&u[2]<32);
    unsigned bytes=4+((u[2]+1)&~1u)*2;memcpy(image+0x82000+i*80,u,bytes);
    gps_functions[i]=(RUNTIME_FUNCTION){rva,rva+f->EndAddress-f->BeginAddress,0x82000+i*80};
}
static void gps_prepare_owned(void){
    DWORD old;const unsigned pages[]={0xbe2000,0xc72000,0xc58000,0xc6f000,0x14dd000};
    for(unsigned i=0;i<5;i++)CHECK(VirtualProtect(image+pages[i],0x2000,PAGE_READWRITE,&old));
    gps_copy_frame(0,0xbe2320,gps_test_action,gps_test_action_end);
    gps_copy_frame(1,0xc58da0,wp_test_range,wp_test_radius);
    gps_copy_frame(2,0xc59b00,wp_test_radius,wp_test_frame_end);
    gps_copy_frame(3,WP_OUTER,gps_test_outer,gps_test_outer_data_end);
    gps_copy_frame(4,0xc72380,gps_test_evaluator,gps_test_evaluator_end);
    CHECK(RtlAddFunctionTable(gps_functions,5,(DWORD64)image));
    *(int32_t*)(image+0xbe237e)=0xc72380-0xbe2382;
    *(int32_t*)(image+0xc72901)=WP_OUTER-0xc72905;
    *(int32_t*)(image+0xc6fb6b)=0xc59b00-0xc6fb6f;
    *(int32_t*)(image+0xc5994d)=WP_SAMPLER-0xc59951;
    *(void**)(image+0xc58da0+(wp_test_rng-wp_test_range))=tls+0x318;
    *(void**)(image+WP_OUTER+(gps_test_enter-gps_test_outer))=gps_owned_enter;
    *(void**)(image+WP_OUTER+(gps_test_finish-gps_test_outer))=gps_owned_finish;
    /* Count the actual authored radius entry in unused owned padding. */
    unsigned char *count=image+0xc59b0e;count[0]=0x48;count[1]=0xb8;*(uintptr_t*)(count+2)=(uintptr_t)&native_calls[2];count[10]=0xff;count[11]=0x00;
    const unsigned char prefix[]={0x48,0x89,0x6c,0x24,0x10};memcpy(image+WP_SAMPLER,prefix,5);thunk_absolute_jump(image+WP_SAMPLER+5,(void*)sampler);
    for(unsigned i=0;i<5;i++)CHECK(VirtualProtect(image+pages[i],0x2000,PAGE_EXECUTE_READ,&old));
    for(unsigned i=0;i<4;i++){
        wp_patch *p=&wp_patches[i];p->owned=p->protection_pending=p->flush_pending=0;p->original=0;
        CHECK(read_local(image+p->rva,&p->original,p->size));p->patched=p->original;unsigned char *b=(void*)&p->patched;
        if(i==0||i==3){b[0]=0xe9;*(int32_t*)(b+1)=(int32_t)((thunk+p->offset+32)-(image+p->rva+5));}
        else *(int32_t*)(b+p->displacement)=(int32_t)((thunk+p->offset)-(image+p->rva+(i==1?13:15)));
        if(i!=1){CHECK(wp_swap(i,p->original,p->patched).ok);p->owned=1;}
    }
}
static void gps_check_normal(void){
    CHECK(gps_owned_run()==(uintptr_t)output);gps_record *r=&gps_records[0];
    printf("safe-position reads=%u bytes=%u errors=%u counts=%u/%u/%u/%u\n",r->e.reads,r->e.bytes,r->e.errors,r->e.calls[0],r->e.calls[1],r->e.calls[2],r->e.calls[3]);
    CHECK(gps_reserved==1&&r->e.complete&&!r->e.errors&&!gps_failures&&!gps_open&&!gps_current&&!wp_current&&!wp_radius_current);
    const unsigned expected[]={1,0,1,1};CHECK(!memcmp(native_calls,expected,sizeof(expected))&&!memcmp(r->e.calls,expected,sizeof(expected))&&!memcmp(r->e.returns,expected,sizeof(expected)));
    CHECK(!wp_reserved&&*(uint64_t*)(tls+0x318)==step(step(step(123))));
    CHECK(r->witness[0][3]==(uintptr_t)image+0xc72905&&r->witness[0][4]==(uintptr_t)image+0xbe2382);
    CHECK(r->e.arguments[1]==r->entry+0xc8&&r->e.sample[0].slot&&r->e.sample[0].normal);
}
static void gps_failure_tests(void){
    gps_owned_reset();gps_check_normal();unsigned count=gps_records[0].e.reads;
    for(unsigned i=1;i<=count;i++){
        gps_owned_reset();fail_read=i;CHECK(gps_owned_run()==(uintptr_t)output);
        CHECK(gps_records[0].e.complete&&gps_records[0].e.errors&&!gps_open&&!gps_current);
        CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
        gps_owned_reset();stop_read=i;CHECK(gps_owned_run()==(uintptr_t)output);
        CHECK(gps_records[0].e.complete&&(gps_records[0].e.errors&WP_STOP)&&!gps_open&&!gps_current);
        CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
    }
    const unsigned throws[]={1,3,4};for(unsigned i=0;i<3;i++){
        gps_owned_reset();mode=throws[i];unsigned caught=0;__try{gps_owned_run();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
        CHECK(caught&&gps_records[0].e.complete&&(gps_records[0].e.errors&WP_UNWIND)&&!gps_open&&!gps_current&&!wp_current&&!wp_radius_current);
        CHECK(native_calls[0]==1&&native_calls[2]==(throws[i]!=1)&&native_calls[3]==(throws[i]!=1));
    }
    gps_owned_reset();mode=5;gps_check_normal();CHECK(detached&&!wp_passive);
    gps_owned_reset();mode=6;CHECK(gps_owned_run()==(uintptr_t)output);CHECK(gps_restore_open==1&&!gps_open&&(gps_records[0].e.errors&WP_STOP));
    gps_owned_reset();world_action_current=NULL;CHECK(gps_owned_run()==(uintptr_t)output);CHECK(!gps_reserved&&!gps_open&&!gps_current&&native_calls[0]==1);
}


static void gps_owned_word(unsigned rva,const void *bytes,size_t size){DWORD old;CHECK(VirtualProtect(image+rva,size,PAGE_READWRITE,&old));memcpy(image+rva,bytes,size);CHECK(VirtualProtect(image+rva,size,old,&old));CHECK(FlushInstructionCache(GetCurrentProcess(),image+rva,size));}
static void gps_mutation_tests(void){
    /* Change a real caller continuation by moving the owned CALL earlier;
     * original still executes through its unchanged wrapper/trampoline. */
    unsigned char calls[0xc5];memcpy(calls,image+0xc72840,sizeof(calls));
    unsigned char alternate[sizeof(calls)];memcpy(alternate,calls,sizeof(calls));memset(alternate+0xc0,0x90,5);
    alternate[5]=0xe8;int32_t displacement=WP_OUTER-0xc7284a;memcpy(alternate+6,&displacement,4);
    gps_owned_word(0xc72840,alternate,sizeof(alternate));gps_owned_reset();CHECK(gps_owned_run()==(uintptr_t)output);
    CHECK(gps_records[0].e.complete&&(gps_records[0].e.errors&WP_SCOPE)&&gps_records[0].e.caller==(uintptr_t)image+0xc7284a&&native_calls[0]==1);
    gps_owned_word(0xc72840,calls,sizeof(calls));
    unsigned char call[5],nops[5]={0x90,0x90,0x90,0x90,0x90};memcpy(call,image+0xc6fb6a,5);
    gps_owned_word(0xc6fb6a,nops,5);gps_owned_reset();CHECK(gps_owned_run()==(uintptr_t)output);
    CHECK(gps_records[0].e.errors&WP_COUNTS);CHECK(native_calls[0]==1&&!native_calls[2]&&!native_calls[3]);gps_owned_word(0xc6fb6a,call,5);
    memcpy(call,image+0xc5994c,5);gps_owned_word(0xc5994c,nops,5);gps_owned_reset();CHECK(gps_owned_run()==(uintptr_t)output);
    CHECK(gps_records[0].e.errors&WP_COUNTS);CHECK(native_calls[0]==1&&native_calls[2]==1&&!native_calls[3]);gps_owned_word(0xc5994c,call,5);
    for(unsigned m=7;m<=8;m++){gps_owned_reset();mode=m;CHECK(gps_owned_run()==(uintptr_t)output);CHECK(gps_records[0].e.errors&&!gps_current&&!gps_open&&!wp_current);}
    for(unsigned fault=0;fault<5;fault++){
        gps_owned_reset();void *a=node+0x28,*frame=exec_state+0x38,*ref=reference;
        if(fault==0)a=node+0x30;if(fault==1)frame=exec_state+0x39;if(fault==2)ref=NULL;
        if(fault==3)++*(uint64_t*)(context+8);if(fault==4)++*(uint64_t*)(exec_state+0x18);
        CHECK(((wp_outer_fn)(image+0xbe2320))(a,output,frame,ref)==(uintptr_t)output);
        CHECK(gps_records[0].e.complete&&gps_records[0].e.errors&&!gps_open&&!gps_current&&native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
    }
    gps_owned_reset();gps_order_callbacks=gps_callbacks;gps_order_callbacks.finish=gps_passive_finish_probe;wp_passive=&gps_order_callbacks;wp_rng_begin=gps_rng_begin_probe;
    CHECK(gps_owned_run()==(uintptr_t)output);CHECK(finish_order==2&&!wp_open&&!gps_records[0].e.errors);
    gps_owned_reset();gps_order_callbacks=gps_callbacks;gps_order_callbacks.finish=gps_passive_finish_probe;wp_passive=&gps_order_callbacks;wp_rng_begin=gps_rng_begin_probe;mode=3;
    unsigned caught=0;__try{gps_owned_run();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
    CHECK(caught&&finish_order==2&&!wp_open&&!gps_open&&(gps_records[0].e.errors&WP_UNWIND));
    gps_owned_reset();for(unsigned i=0;i<GPS_CAP+1;i++)CHECK(gps_owned_run()==(uintptr_t)output);
    CHECK(gps_reserved==GPS_CAP+1&&gps_dropped==1&&gps_failures>=GPS_CAP&&!gps_open&&!gps_current);
    gps_owned_reset();unsigned char *page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE),copy[8];CHECK(page);DWORD old;
    CHECK(VirtualProtect(page,4096,PAGE_NOACCESS,&old));wp_scope scope={0};scope.event=&gps_records[0].e;scope.reader=read_local;
    CHECK(!wp_read(&scope,(uintptr_t)page,copy,8)&&(scope.event->errors&WP_READ));CHECK(!wp_at(&scope,UINTPTR_MAX-2,4,copy,8));
    scope.event->reads=WP_READS;CHECK(!wp_read(&scope,(uintptr_t)output,copy,8)&&(scope.event->errors&WP_BUDGET));
    CHECK(VirtualFree(page,0,MEM_RELEASE));
}

static void gps_tib_join_tests(void){
    /* Corrupt only retained observer witness words around the real callback;
     * the OS TIB/native stack and original function are never changed. Restore
     * before outer recheck so only the new cross-boundary join can reject. */
    for(unsigned after=0;after<2;after++)for(gps_tib_edge=1;gps_tib_edge<=3;gps_tib_edge++){
        gps_owned_reset();gps_order_callbacks=gps_callbacks;
        if(after)gps_order_callbacks.sample_normal=gps_tib_sample_normal;
        else gps_order_callbacks.sample_begin=gps_tib_sample_begin;
        wp_passive=&gps_order_callbacks;CHECK(gps_owned_run()==(uintptr_t)output);
        gps_record *r=&gps_records[0];CHECK(r->e.complete&&r->e.errors==WP_CHANGED&&gps_failures==1&&!gps_open&&!gps_current);
        CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
        CHECK(!memcmp(r->witness[0],r->witness[1],sizeof(r->witness[0])));
    }
}
static DWORD WINAPI gps_worker(void *unused){(void)unused;gps_owned_reset();mode=gps_worker_mode;gps_worker_caught=0;
    __try{CHECK(gps_owned_run()==(uintptr_t)output);}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){gps_worker_caught=1;}return 0;
}
static void gps_worker_tests(void){
    gps_entered=CreateEventW(NULL,TRUE,FALSE,NULL);gps_release=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(gps_entered&&gps_release);
    for(unsigned m=9;m<=10;m++){
        CHECK(ResetEvent(gps_entered)&&ResetEvent(gps_release));gps_worker_mode=m;HANDLE thread=CreateThread(NULL,0,gps_worker,NULL,0,NULL);CHECK(thread);
        CHECK(WaitForSingleObject(gps_entered,5000)==WAIT_OBJECT_0);CHECK(gps_open==1&&wp_open==1);
        world_safe_position_stop();world_position_restore();CHECK(gps_restore_open==1&&gps_open==1&&wp_restore_open==1&&wp_open==1&&!wp_patch_mask(0));
        CHECK(SetEvent(gps_release));CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CHECK(CloseHandle(thread));
        CHECK(!gps_open&&!wp_open&&gps_records[0].e.complete&&(gps_records[0].e.errors&WP_STOP)&&gps_worker_caught==(m==10));
        CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
        for(unsigned i=0;i<4;i++)if(i!=1){CHECK(wp_swap(i,wp_patches[i].original,wp_patches[i].patched).ok);wp_patches[i].owned=1;}
    }
    CHECK(CloseHandle(gps_entered)&&CloseHandle(gps_release));gps_entered=gps_release=NULL;
}
static void gps_config_tests(void){
    configure_reset();unsigned count=0;const char *m="0000000000080d54,4,move.idle";
    CHECK(world_action_parse(m,strlen(m),world_action_selected,&count)&&count==1);world_action_selected_count=count;strcpy(world_action_manifest,m);
    CHECK(world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair));
    gps_started=gps_enabled=gps_closing=0;gps_failures=gps_dropped=0;wp_passive=NULL;
    CHECK(gps_configure_using(gps_pins)&&gps_started&&gps_enabled&&wp_passive==&gps_callbacks);
    gps_exported=3;gps_calls=29;gps_sequence=2;
    for(unsigned phase=0;phase<=3;phase++){
        boot_phase=phase;unsigned p=protect_calls,f=flush_calls;unsigned char binding[sizeof(gps_binding)];memcpy(binding,gps_binding,sizeof(binding));
        CHECK(gps_configure_using(gps_pins));CHECK(gps_exported==3&&gps_calls==29&&gps_sequence==2&&protect_calls==p&&flush_calls==f&&!memcmp(binding,gps_binding,sizeof(binding)));
    }
    for(unsigned fault=0;fault<13;fault++){
        if(fault==0)gps_failures=1;if(fault==1)gps_closing=1;if(fault==2)world_values_failures=1;if(fault==3)world_action_closing=1;
        if(fault==4)++secret[0];if(fault==5)++boot_started;if(fault==6)wp_passive=NULL;if(fault==7)wp_patches[0].protection_pending=1;
        if(fault==8)wp_patches[0].flush_pending=1;if(fault==9)wp_patches[0].owned=0;if(fault==10)++world_action_selected[0].version;
        if(fault==11)++stats.slot;if(fault==12)wp_failures=1;
        unsigned p=protect_calls,f=flush_calls;CHECK(!gps_configure_using(gps_pins));CHECK(gps_exported==3&&gps_calls==29&&gps_sequence==2&&p==protect_calls&&f==flush_calls);
        if(fault==0)gps_failures=0;if(fault==1)gps_closing=0;if(fault==2)world_values_failures=0;if(fault==3)world_action_closing=0;
        if(fault==4)--secret[0];if(fault==5)--boot_started;if(fault==6)wp_passive=&gps_callbacks;if(fault==7)wp_patches[0].protection_pending=0;
        if(fault==8)wp_patches[0].flush_pending=0;if(fault==9)wp_patches[0].owned=1;if(fault==10)--world_action_selected[0].version;
        if(fault==11)--stats.slot;if(fault==12)wp_failures=0;
    }
    for(unsigned i=0;i<4;i++){
        wp_patch *site=&wp_patches[i];unsigned char saved[16];memcpy(saved,image+site->rva,site->size);unsigned char bad[16];memcpy(bad,saved,site->size);bad[site->size-1]^=1;
        gps_owned_word(site->rva,bad,site->size);unsigned p=protect_calls,f=flush_calls;CHECK(!gps_configure_using(gps_pins));
        CHECK(p==protect_calls&&f==flush_calls&&gps_exported==3&&gps_calls==29&&gps_sequence==2);gps_owned_word(site->rva,saved,site->size);
    }
    world_safe_position_stop();world_position_restore();world_values_restore();CHECK(!wp_patch_mask(0));
}
static uintptr_t gps_assigned(void *a,void *b,void *c,void *d){
    CHECK((uintptr_t)a==(uintptr_t)node+0x1b8&&b==exec_state+0x38&&(uintptr_t)c==(uintptr_t)node+0x1c8&&wp_d(d)==0x63);return 0x123456789abcde01ULL;
}
static void gps_action_original(void *n,void *exec,void *frame){
    CHECK(n==node&&exec==exec_state&&frame==exec_state+0x38);CHECK(gps_owned_run()==(uintptr_t)output);
    /* Deliberately model later avoidance changing the result and consuming two
     * draws: the passive receipt must accept inequality without inventing a cause. */
    const float final[4]={7,8,9,0};unsigned char value[16]={0},position[0x30]={0};*(unsigned*)position=1;
    memcpy(position+0x20,final,16);*(unsigned*)value=0x63;*(void**)(value+8)=position;
    *(uint64_t*)(tls+0x318)=step(step(*(uint64_t*)(tls+0x318)));
    CHECK(world_values_assign_observe(gps_assigned,(void*)((uintptr_t)node+0x1b8),frame,(void*)((uintptr_t)node+0x1c8),value,(uintptr_t)image+0xbe23af,read_local,test_tls)==0x123456789abcde01ULL);
}
static void gps_export(const char *path){
    gps_owned_reset();world_action_current=NULL;world_action_calls=world_action_reserved=world_action_sequence=world_action_failures=world_action_dropped=0;
    npc_provenance_early_eligible=yes;
    world_action_other_owner=world_action_other_script=world_action_other_context=world_action_other_phase=world_action_unknown=world_action_threads=world_action_unknown_rng=0;
    world_action_open=world_action_peak=world_action_restore_open=0;world_action_depth=0;world_action_enabled=world_action_started=1;world_action_closing=0;
    const char *manifest="0000000000080d54,4,move.idle";unsigned count=0;CHECK(world_action_parse(manifest,strlen(manifest),world_action_selected,&count)&&count==1);world_action_selected_count=count;
    memset(world_values_events,0,sizeof(world_values_events));world_values_reserved=world_values_calls=world_values_open=world_values_failures=world_values_dropped=world_values_sequence=0;
    world_values_peak=world_values_restore_open=world_values_other_scope=world_values_other_type=0;world_values_enabled=world_values_started=1;world_values_closing=0;
    world_action_observe_with_tls(gps_action_original,node,exec_state,exec_state+0x38,test_tls);
    CHECK(world_action_ready()==1&&world_values_ready()==1&&gps_ready()==1&&!wp_reserved);
    CHECK(!world_action_events[0].errors&&!world_values_events[0].errors&&!gps_records[0].e.errors);
    world_action_closing=world_values_closing=1;world_action_exported=world_values_exported=3;gps_exported=GPS_ROWS;
    world_safe_position_stop();world_position_restore();wp_patch_failures=wp_patch_errors=0;world_values_sites=0;world_values_patch_failures=world_values_patch_errors=0;
    char row[2048];FILE *file=path?fopen(path,"wb"):NULL;if(path)CHECK(file);
    for(unsigned i=0;i<3;i++){CHECK(world_action_format(&world_action_events[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(world_action_status(row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE WORLD_ACTION_STOP %s\n",row);
    for(unsigned i=0;i<3;i++){CHECK(world_values_format(&world_values_events[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(world_values_status(row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE WORLD_VALUES_STOP %s\n",row);
    for(unsigned i=0;i<GPS_ROWS;i++){CHECK(gps_format(&gps_records[0],1,i,row,sizeof(row))>0);if(file)fprintf(file,"NATIVE_GATE %s\n",row);}
    CHECK(gps_status(row,sizeof(row))>0);if(file){fprintf(file,"NATIVE_GATE WORLD_SAFE_POSITION_STOP %s\n",row);CHECK(!fclose(file));}
}

int main(int argc,char **argv){
    CHECK(argc==2||argc==3);load_image(argv[1]);*(void**)(image+0x2c0cc88+0x18)=image+0xbe2320;CHECK(gps_pins());
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));CHECK(world_identities);
    CHECK(world_identity_record(0x100,0x80d54)&&world_identity_record((1ULL<<25)|1,0x3fd)&&world_identity_record((1ULL<<25)|2,0x3fc));
    thunk=make_thunk();CHECK(thunk&&thunk_page_rx(thunk));gps_config_tests();gps_prepare_owned();gps_failure_tests();gps_mutation_tests();gps_tib_join_tests();gps_worker_tests();gps_export(argc==3?argv[2]:NULL);
    world_safe_position_stop();world_position_restore();CHECK(!wp_patch_mask(0)&&!wp_patch_mask(1)&&!wp_patch_mask(2));CHECK(RtlDeleteFunctionTable(gps_functions));
    printf("safe-position checks=%u\n",checks);return 0;
}


