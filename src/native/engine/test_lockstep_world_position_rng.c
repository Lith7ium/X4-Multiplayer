/* Production Position wrappers and RNG scope on owned memory. The pinned PE
 * is read, never executed. Owned originals retain exact native arithmetic. */
#include <stdint.h>
static int rng_test_store(uint64_t*,uint64_t);
static int rng_test_pins(void);
#define WPR_NATIVE_STORE rng_test_store
#define WPR_RUNTIME_PINS rng_test_pins
#define main position_baseline_main
#include "test_lockstep_world_position.c"
#undef main
static unsigned wpr_test_mode,wpr_store_calls,wpr_pin_calls,wpr_real;
static unsigned wpr_stop_read,wpr_nested_depth;
static world_action_event *wpr_nested_parent;
static HANDLE rng_store_entered,rng_store_release,rng_stop_entered,rng_stop_complete;
extern unsigned char wpr_test_range[],wpr_test_radius[],wpr_test_frame_end[],wpr_test_rng[],wpr_test_result_sink[];
static uintptr_t wpr_actual_scratch;
static unsigned char order[0x80],order_registry[0xa0],order_bank[32],import_node[0x40];
static int rng_test_pins(void){++wpr_pin_calls;return wpr_test_mode!=20;}
static int rng_test_store(uint64_t *p,uint64_t value){
    ++wpr_store_calls;
    if(wpr_test_mode==30&&wpr_store_calls==1){CHECK(wpr_events[0].write_attempted&&wpr_events[0].restore_required&&wpr_events[0].debt);CHECK(SetEvent(rng_store_entered));CHECK(WaitForSingleObject(rng_store_release,5000)==WAIT_OBJECT_0);}
    if(wpr_test_mode==10&&wpr_store_calls==1){*(uint32_t*)p=(uint32_t)value;return 0;}
    if(wpr_test_mode==19&&wpr_store_calls==1){*(uint32_t*)p=(uint32_t)value;RaiseException(0xe042c0de,0,0,NULL);}
    if(wpr_test_mode==11&&wpr_store_calls==2)return 0;
    return wpr_native_store(p,value);
}
static void *rng_test_tls(void){if(wpr_test_mode==18&&wpr_store_calls)RaiseException(0xe042c0de,0,0,NULL);return wpr_test_mode==12&&wpr_store_calls?NULL:tls;}
static int rng_reader(const void *p,void *out,size_t size){int ok=test_read(p,out,size);if(wpr_stop_read&&read_calls==wpr_stop_read)world_position_rng_restore();return ok;}
static const char wpr_wire1[]="POSITION_RNG_1;" WORLD_FIND_SNAPSHOT ";1;985d66488a43c795;17bafa51eebe82a5;";
static const char wpr_wire2[]="POSITION_RNG_1;" WORLD_FIND_SNAPSHOT ";2;985d66488a43c795;17bafa51eebe82a5;";
static void rng_dependencies(unsigned composition){
    static const char observed[]=
      "0000000000080d54,4,5,401baaab8a5ce5b5,3fb999999999999a,3fe0000000000000,move.idle;"
      "000000000009d3a4,10,1,4019444523f67f4e,3f50624dd2f1a9fc,3ff0000000000000,order.fight.patrol;"
      "000000000006d945,4,5,401beeee0f3cb3e6,3fb999999999999a,3fe0000000000000,move.idle";
    CHECK(world_wait_parse(observed,sizeof(observed)-1,world_wait_selected,&world_wait_selected_count));
    CHECK(world_wait_rng_parse(WORLD_FIND_THIRD_POLICY,sizeof(WORLD_FIND_THIRD_POLICY)-1,world_wait_rng_selected,world_wait_rng_snapshot,&world_wait_rng_schema));
    strcpy(world_wait_rng_manifest,WORLD_FIND_THIRD_POLICY);if(composition==1)world_wait_rng_selected[2].policy=WWR_OBSERVE;
    world_find_started=world_find_enabled=world_wait_enabled=world_wait_rng_started=world_action_enabled=world_values_enabled=1;
    world_find_closed=world_wait_restoring=world_action_closing=world_values_closing=0;world_values_sites=3;world_find_composition=composition;
    world_find_failures=world_find_dropped=world_find_open=world_find_restore_open=world_find_anchor_pending=world_find_anchor_errors=0;
    world_wait_anchor_observer=world_find_wait_anchor;world_wait_rng_foreign_active=world_find_conflict;world_values_candidate_call=world_find_call;
    world_find_shape=WORLD_FIND_SHAPE;world_action_current->before.program_hash=WORLD_FIND_SHAPE;
    *(void**)(context+0x148)=order;*(void**)order=image+0x2c69060;*(uint64_t*)(order+8)=0x40001;*(unsigned*)(order+0x20)=1;*(void**)(order+0x50)=context;
    *(void**)(image+0x6cf1750)=order_registry;*(void**)(order_registry+0x58)=order_bank;*(void**)order_bank=order;
    *(unsigned*)(order_bank+0x18)=1;*(unsigned*)(order_bank+0x1c)=0x10000;
    *(void**)(image+0x31c2de8)=import_node;*(uint64_t*)(import_node+0x20)=0x3cdb;*(uint64_t*)(import_node+0x28)=0x40001;
    CHECK(world_find_identity_stable(read_local,(uintptr_t)context,(uintptr_t)exec_state,&world_find_anchor));
    world_find_anchor_ready=1;world_find_anchor_event=&world_wait_events[0];memset(world_wait_events,0,sizeof(world_wait_events));world_wait_events[0].complete=1;
    world_find_reserved=world_find_used=1;memset(world_find_events,0,sizeof(world_find_events));world_find_events[0].complete=1;
    world_find_events[0].applied=world_find_events[0].restored=1;world_find_events[0].assigned=0x29;world_find_events[0].candidate=1;
    world_find_events[0].action=1;world_find_events[0].thread=ui_thread;
}
static void rng_reset(unsigned composition){
    wp_rng_begin=NULL;wp_rng_sample=NULL;wp_rng_finish=NULL;world_find_extra_conflict=NULL;reset();
    /* Modeled completed Find Action1 precedes the current Position Action2.
     * Preserve actual formatter ordinals; never synthesize a Find Action0. */
    world_action_events[1]=world_action_events[0];world_action_current=&world_action_events[1];
    memset(wpr_events,0,sizeof(wpr_events));wpr_started=wpr_enabled=1;wpr_closed=wpr_reserved=wpr_exported=wpr_used=0;
    wpr_calls=wpr_open=wpr_peak=wpr_restore_open=wpr_failures=wpr_dropped=wpr_debt=0;wpr_current=NULL;
    wpr_test_mode=wpr_store_calls=wpr_pin_calls=wpr_real=wpr_stop_read=wpr_nested_depth=0;world_find_current=NULL;
    unsigned short control=0x27f;__asm__ volatile("fldcw %0"::"m"(control));
    *(unsigned*)(node+0x70)=0;*(unsigned*)(node+0x80)=0;memset(output,0,16);
    *(unsigned*)(image+0x38edec0)=1;*(unsigned*)(image+0x2f3f220)=2;
    rng_dependencies(composition);wpr_composition=composition;CHECK(wpr_key(wpr_digest,&wpr_seed));
    strcpy(wpr_manifest,composition==1?wpr_wire1:wpr_wire2);wpr_bind();wp_bind();
    wp_rng_begin=wpr_begin;wp_rng_sample=wpr_sample;wp_rng_finish=wpr_finish;world_find_extra_conflict=wpr_conflict;
}
static uintptr_t rng_nested_original(void *a,void *b,void *c,void *d){
    (void)a;(void)c;(void)d;
    if(wpr_nested_depth==1&&wpr_test_mode==14){++wpr_nested_depth;world_action_current=wpr_nested_parent;
        CHECK(wp_outer_observe(rng_nested_original,node+0x28,b,exec_state+0x38,NULL,(uintptr_t)(image+0xbe0bd1),rng_reader,rng_test_tls)==(uintptr_t)b);
        world_action_current=NULL;}
    return(uintptr_t)b;
}
static float rng_sample_native(void *rng,float span,unsigned distribution,unsigned parameter){
    ++native_calls[3];CHECK(rng==tls+0x318&&wp_fbits(span)==0x47435000&&distribution==1&&parameter==1);
    if(wpr_test_mode==30)CHECK(WaitForSingleObject(rng_stop_complete,5000)==WAIT_OBJECT_0);
    if(wpr_test_mode==2)throw_test();
    *(uint64_t*)rng=step(*(uint64_t*)rng);volatile float low=(float)(uint32_t)*(uint64_t*)rng,unit=low*from_bits(0x2f800000);float result=unit*span;
    if(wpr_test_mode==3)world_position_rng_restore();
    if(wpr_test_mode==4)++*(uint64_t*)(order+8);
    if(wpr_test_mode==5)++*(unsigned*)(image+0x2f3f220);
    if(wpr_test_mode==6)throw_test();
    if(wpr_test_mode==13||wpr_test_mode==14){
        wpr_nested_parent=world_action_current;wpr_nested_depth=1;if(wpr_test_mode==14)world_action_current=NULL;
        CHECK(wp_outer_observe(rng_nested_original,node+0x28,output,exec_state+0x38,NULL,(uintptr_t)(image+0xbe0bd1),rng_reader,rng_test_tls)==(uintptr_t)output);
        world_action_current=wpr_nested_parent;
    }
    if(wpr_test_mode==15)wp_rng_finish=NULL;
    if(wpr_test_mode==16)++wpr_events[0].thread;
    if(wpr_test_mode==17)boot_phase=3;
    return result;
}
static float rng_radius_native(void *range,void *frame,void *optional){
    ++native_calls[2];CHECK(range==node+0x90&&frame==exec_state+0x38&&!optional);
    if(wpr_test_mode==21)return from_bits(0x46c881ce);
    if(wpr_real)return ((wp_radius_fn)(image+0xc59b00))(range,frame,optional);
    __declspec(align(16)) unsigned char raw[0x190]={0};unsigned char *t=raw+8;
    *(uintptr_t*)t=(uintptr_t)(image+0xc59951);*(unsigned*)(t+0x38)=5;*(unsigned*)(t+0x40)=0x47435000;
    *(unsigned*)(t+0x78)=5;*(unsigned*)(t+0x80)=0x47435000;
    *(uintptr_t*)(t+0x140)=(uintptr_t)(image+0xc59b2f);*(uintptr_t*)(t+0x148)=(uintptr_t)range;*(uintptr_t*)(t+0x150)=(uintptr_t)frame;
    if(wpr_test_mode==7)t[0x80]^=1;if(wpr_test_mode==8)t[0x178]=1;
    float result=wp_sampler_observe(rng_sample_native,tls+0x318,from_bits(0x47435000),1,1,(uintptr_t)t,(uintptr_t)(image+0xc59951));
    if(wpr_test_mode==22)result=wp_sampler_observe(rng_sample_native,tls+0x318,from_bits(0x47435000),1,1,(uintptr_t)t,(uintptr_t)(image+0xc59951));
    return result;
}
static uintptr_t rng_outer_native(void *a,void *b,void *c,void *d){
    ++native_calls[0];CHECK(a==node+0x28&&b==output&&c==exec_state+0x38&&!d);memset(output,0,16);
    float radius=wp_radius_observe(rng_radius_native,node+0x90,c,NULL,(uintptr_t)(image+0xc6fb6f),0,0);
    if(radius>0){if(wpr_test_mode!=23)*(uint64_t*)(tls+0x318)=step(step(*(uint64_t*)(tls+0x318)));if(wpr_test_mode==24)*(uint64_t*)(tls+0x318)=step(*(uint64_t*)(tls+0x318));memset(output,0x31,16);}
    if(wpr_test_mode==9)throw_test();return(uintptr_t)b;
}
static void rng_call(void){CHECK(wp_outer_observe(rng_outer_native,node+0x28,output,exec_state+0x38,NULL,(uintptr_t)(image+0xbe0bd1),rng_reader,rng_test_tls)==(uintptr_t)output);}
static void rng_export(const char *path){
    if(!path)return;FILE *out=fopen(path,"wb");CHECK(out);char row[2048];
    /* Parent/Find/Wait metadata are declared owned fixtures. These are actual
     * Position/RNG wrapper captures, not a game or whole-script execution. */
    world_position_rng_restore();wp_closing=1;wpr_exported=WPR_ROWS;wp_exported=WP_ROWS;
    for(unsigned i=0;i<WPR_ROWS;i++){CHECK(wpr_format(&wpr_events[0],1,i,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);}
    CHECK(wpr_status(row,sizeof(row))>0);fprintf(out,"NATIVE_GATE WORLD_POSITION_RNG_STOP %s\n",row);
    for(unsigned i=0;i<WP_ROWS;i++){CHECK(wp_format(&wp_events[0],1,i,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);}
    CHECK(wp_status(row,sizeof(row))>0);fprintf(out,"NATIVE_GATE WORLD_POSITION_STOP %s\n",row);CHECK(!fclose(out));
}
static DWORD WINAPI rng_race_worker(void *unused){
    (void)unused;ui_thread=GetCurrentThreadId();world_action_current=&world_action_events[1];world_action_current->thread=ui_thread;
    world_find_events[0].thread=ui_thread;
    npc_provenance_current=&provenance;rng_call();return 0;
}
static DWORD WINAPI rng_stop_worker(void *unused){(void)unused;CHECK(SetEvent(rng_stop_entered));world_position_rng_restore();CHECK(SetEvent(rng_stop_complete));return 0;}
static void rng_stop_race(void){
    rng_reset(2);wpr_test_mode=30;rng_store_entered=CreateEventA(NULL,TRUE,FALSE,NULL);rng_store_release=CreateEventA(NULL,TRUE,FALSE,NULL);
    rng_stop_entered=CreateEventA(NULL,TRUE,FALSE,NULL);rng_stop_complete=CreateEventA(NULL,TRUE,FALSE,NULL);
    CHECK(rng_store_entered&&rng_store_release&&rng_stop_entered&&rng_stop_complete);
    HANDLE worker=CreateThread(NULL,0,rng_race_worker,NULL,0,NULL);CHECK(worker&&WaitForSingleObject(rng_store_entered,5000)==WAIT_OBJECT_0);
    HANDLE stop=CreateThread(NULL,0,rng_stop_worker,NULL,0,NULL);CHECK(stop&&WaitForSingleObject(rng_stop_entered,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(stop,0)==WAIT_TIMEOUT&&wpr_events[0].write_attempted&&wpr_events[0].debt&&!wpr_events[0].applied);
    CHECK(SetEvent(rng_store_release));CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0&&WaitForSingleObject(stop,5000)==WAIT_OBJECT_0);
    CHECK(wpr_events[0].complete&&(wpr_events[0].errors&WPR_STOP)&&wpr_events[0].restored&&!wpr_debt&&!wpr_open&&wpr_restore_open==1&&*(uint64_t*)(tls+0x318)==123);
    CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
    CHECK(CloseHandle(worker)&&CloseHandle(stop)&&CloseHandle(rng_store_entered)&&CloseHandle(rng_store_release)&&CloseHandle(rng_stop_entered)&&CloseHandle(rng_stop_complete));
    puts("Position RNG controlled seed-publication/STOP worker interleaving PASS");
}
static unsigned rng_ack_pin_calls;
static int rng_ack_pins(void){++rng_ack_pin_calls;return wpr_pins();}
static void rng_ack(const char *wire,unsigned expected){
    wpr_event saved[WPR_CAP];char before[1024],after[1024];memcpy(saved,wpr_events,sizeof(saved));unsigned pins=rng_ack_pin_calls,stores=wpr_store_calls;uint64_t state=*(uint64_t*)(tls+0x318);
    CHECK(wpr_status(before,sizeof(before))>0);CHECK((unsigned)wpr_configure_using(wire,strlen(wire),rng_ack_pins)==expected);
    CHECK(wpr_status(after,sizeof(after))>0&&!strcmp(before,after)&&!memcmp(saved,wpr_events,sizeof(saved))&&pins==rng_ack_pin_calls&&stores==wpr_store_calls&&state==*(uint64_t*)(tls+0x318));
}
static void rng_config_tests(void){
    thunk=make_thunk();CHECK(thunk&&thunk_page_rx(thunk));
    configure_reset();CHECK(world_position_configure_using(wp_pins_ok,wp_prepare,test_swap,test_repair));rng_dependencies(1);
    world_find_started=world_find_enabled=world_find_reserved=world_find_used=world_find_anchor_ready=0;world_find_manifest[0]=0;
    const char find[]="FIND_COMPONENT_RNG_1;" WORLD_FIND_SNAPSHOT ";985d66488a43c795;65d8202ad9940908;";
    CHECK(world_find_configure(find,sizeof(find)-1));
    wp_rng_begin=NULL;wp_rng_sample=NULL;wp_rng_finish=NULL;world_find_extra_conflict=NULL;
    memset(wpr_events,0,sizeof(wpr_events));wpr_started=wpr_enabled=wpr_closed=wpr_reserved=wpr_exported=wpr_used=0;
    wpr_calls=wpr_open=wpr_peak=wpr_restore_open=wpr_failures=wpr_dropped=wpr_debt=0;wpr_current=NULL;wpr_test_mode=wpr_stop_read=wpr_store_calls=0;
    CHECK(wpr_configure_using(wpr_wire1,strlen(wpr_wire1),rng_ack_pins));CHECK(rng_ack_pin_calls==1);
    for(unsigned phase=1;phase<=3;phase++){boot_phase=phase;stats.issued=phase==3;rng_ack(wpr_wire1,1);}
    stats.issued=0;
    for(unsigned fault=0;fault<16;fault++){
        if(fault==0)wpr_failures=1;if(fault==1)wpr_dropped=1;if(fault==2)wpr_restore_open=1;if(fault==3)wpr_debt=1;
        if(fault==4)wpr_open=1;if(fault==5)wp_rng_finish=NULL;if(fault==6)wp_rng_sample=NULL;if(fault==7)world_find_extra_conflict=NULL;
        if(fault==8)++secret[0];if(fault==9)++world_find_manifest[0];if(fault==10)world_find_anchor_errors=1;
        if(fault==11)ui_thread=0;if(fault==12)boot_phase=4;if(fault==13)wp_patches[0].flush_pending=1;
        if(fault==14)++wpr_seed;if(fault==15)wpr_closed=1;
        rng_ack(wpr_wire1,0);
        if(fault==0)wpr_failures=0;if(fault==1)wpr_dropped=0;if(fault==2)wpr_restore_open=0;if(fault==3)wpr_debt=0;
        if(fault==4)wpr_open=0;if(fault==5)wp_rng_finish=wpr_finish;if(fault==6)wp_rng_sample=wpr_sample;if(fault==7)world_find_extra_conflict=wpr_conflict;
        if(fault==8)--secret[0];if(fault==9)--world_find_manifest[0];if(fault==10)world_find_anchor_errors=0;
        if(fault==11)ui_thread=GetCurrentThreadId();if(fault==12)boot_phase=3;if(fault==13)wp_patches[0].flush_pending=0;
        if(fault==14)--wpr_seed;if(fault==15)wpr_closed=0;
    }
    rng_ack(wpr_wire2,0);rng_ack("POSITION_RNG_1;bad;",0);
    rng_dependencies(1);boot_phase=2;stats.issued=0;*(unsigned*)(node+0x70)=*(unsigned*)(node+0x80)=0;
    unsigned short control=0x27f;__asm__ volatile("fldcw %0"::"m"(control));
    rng_call();CHECK(wpr_events[0].complete&&!wpr_events[0].errors&&wpr_used==1);rng_ack(wpr_wire1,1);
    rng_call();CHECK(wpr_reserved==2&&(wpr_events[1].errors&WPR_DUPLICATE)&&!wpr_events[1].write_attempted);rng_ack(wpr_wire1,0);
    world_position_rng_restore();rng_ack(wpr_wire1,0);world_position_restore();world_values_restore();
    puts("Position RNG native config/healthy retry/used receipt retention/duplicate/failed acknowledgement PASS");
}
static void rng_real_frames(void){
    thunk=make_thunk();CHECK(thunk&&thunk_page_rx(thunk));DWORD old;
    CHECK(VirtualProtect(image+0xc58000,0x2000,PAGE_READWRITE,&old));CHECK(VirtualProtect(image+WP_SAMPLER,0x1000,PAGE_READWRITE,&old));
    size_t size=(size_t)(wpr_test_frame_end-wpr_test_range);CHECK(size<0x1000&&wpr_test_radius-wpr_test_range==0xd60);
    memcpy(image+0xc58da0,wpr_test_range,size);*(void**)(image+0xc58da0+(wpr_test_rng-wpr_test_range))=tls+0x318;
    *(void**)(image+0xc58da0+(wpr_test_result_sink-wpr_test_range))=&wpr_actual_scratch;*(int32_t*)(image+0xc5994d)=WP_SAMPLER-0xc59951;
    const unsigned char prefix[]={0x48,0x89,0x6c,0x24,0x10};memcpy(image+WP_SAMPLER,prefix,5);thunk_absolute_jump(image+WP_SAMPLER+5,(void*)rng_sample_native);
    RUNTIME_FUNCTION functions[2];unsigned char *starts[]={wpr_test_range,wpr_test_radius};unsigned rvas[]={0xc58da0,0xc59b00};
    for(unsigned i=0;i<2;i++){DWORD64 base=0;PRUNTIME_FUNCTION f=RtlLookupFunctionEntry((DWORD64)starts[i],&base,NULL);CHECK(f);
        unsigned char *u=(unsigned char*)(base+f->UnwindData);CHECK((u[0]&0xf8)==0&&u[2]<24);unsigned n=4+((u[2]+1)&~1u)*2;
        memcpy(image+0x81000+i*64,u,n);functions[i]=(RUNTIME_FUNCTION){rvas[i],rvas[i]+f->EndAddress-f->BeginAddress,0x81000+i*64};}
    CHECK(RtlAddFunctionTable(functions,2,(DWORD64)image));
    CHECK(VirtualProtect(image+0xc58000,0x2000,PAGE_EXECUTE_READ,&old));CHECK(VirtualProtect(image+WP_SAMPLER,0x1000,PAGE_EXECUTE_READ,&old));
    wp_patch *p=&wp_patches[3];p->owned=p->protection_pending=p->flush_pending=0;p->original=0;memcpy(&p->original,image+WP_SAMPLER,8);p->patched=p->original;
    unsigned char *bytes=(void*)&p->patched;bytes[0]=0xe9;*(int32_t*)(bytes+1)=(int32_t)((thunk+3936)-(image+WP_SAMPLER+5));
    CHECK(wp_swap(3,p->original,p->patched).ok);p->owned=1;
    for(unsigned scenario=0;scenario<4;scenario++){
        rng_reset(2);wpr_real=1;wpr_test_mode=scenario==1?2:scenario==2?9:0;if(scenario==3)wpr_enabled=0;
        unsigned caught=0;__try{rng_call();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
        CHECK(caught==(scenario==1||scenario==2)&&!wp_open&&!wpr_open&&!wpr_current&&native_calls[3]==1);
        if(scenario==3)CHECK(!wpr_reserved&&!wpr_store_calls&&*(uint64_t*)(tls+0x318)==step(step(step(123))));
        else {wpr_event *e=&wpr_events[0];CHECK(e->complete&&e->applied&&e->restored&&!e->debt&&*(uint64_t*)(tls+0x318)==123);
            CHECK(wpr_actual_scratch==e->sampler_slot+0x178);CHECK((e->sampler_slot&15)==8);CHECK((!e->errors)==(scenario==0));}
    }
    world_position_restore();CHECK(!p->owned&&RtlDeleteFunctionTable(functions));
    puts("Position RNG actual sampler wrapper/thunk/incoming T+78/T+178 normal, disabled and foreign-SEH paths PASS");
}
int main(int argc,char **argv){
    CHECK(argc==2||argc==3);load_image(argv[1]);*(void**)(image+0x2c69060)=image+0x1075cb0;*(void**)(image+0x2c69068)=image+0xb3d60;
    CHECK(wpr_pins());world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));CHECK(world_identities&&world_identity_record(0x100,0x9d3a4));
    for(unsigned composition=1;composition<=2;composition++){
        rng_reset(composition);rng_call();wpr_event *e=&wpr_events[0];
        printf("position RNG compose%u errors=%u reads=%u bytes=%u applied=%u restored=%u wp=%u\n",composition,e->errors,e->reads,e->bytes,e->applied,e->restored,wp_events[0].errors);
        CHECK(!e->errors&&e->complete&&e->applied&&e->write_attempted&&e->restore_required&&e->restored&&!e->debt&&!wpr_debt);
        CHECK(*(uint64_t*)(tls+0x318)==123&&e->caller_before==123&&e->caller_after==123&&e->sampler_after==0xdef54d4283678009ULL&&e->scoped_after==0x0c1c63b492dca87dULL);
        CHECK(!wp_open&&!wpr_open&&!wpr_current&&wpr_store_calls==2&&native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
        unsigned char wire[WPR_WIRE_SIZE];CHECK(wpr_encode(e,1,wire)==WPR_WIRE_SIZE);
        for(unsigned part=0;part<WPR_ROWS;part++){char line[1024];CHECK(wpr_format(e,1,part,line,sizeof(line))>0&&strlen(line)<1024);}
        if(composition==2)rng_export(argc==3?argv[2]:NULL);
    }
    for(unsigned m=2;m<=24;m++){
        rng_reset(2);wpr_test_mode=m;unsigned caught=0;__try{rng_call();}__except(GetExceptionCode()==0xe042c0de?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
        wpr_event *e=&wpr_events[0];CHECK(e->complete&&e->errors&&!wpr_open&&!wp_open&&!wpr_current);
        CHECK(caught==(m==2||m==6||m==9||m==18));CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==(m==21?0u:m==22?2u:1u));
        if(m==7||m==8||m==20||m==21){CHECK(e->prewrite_refused&&!e->write_attempted&&!e->restored&&!wpr_store_calls);}
        else if(m==11||m==12||m==16||m==18){CHECK(e->write_attempted&&!e->restored&&e->debt&&wpr_debt==1);}
        else {CHECK(e->write_attempted&&e->restored&&!e->debt&&!wpr_debt&&*(uint64_t*)(tls+0x318)==123);}
        if(m==10)CHECK(!e->applied&&e->write_attempted&&wpr_store_calls==2);
    }
    rng_reset(1);wpr_enabled=0;rng_call();CHECK(!wpr_reserved&&!wpr_store_calls&&*(uint64_t*)(tls+0x318)==step(step(step(123))));
    rng_reset(2);rng_call();unsigned reads=read_calls;
    for(unsigned fault=0;fault<2;fault++)for(unsigned i=1;i<=reads;i++){
        rng_reset(2);if(fault)wpr_stop_read=i;else fail_read=i;rng_call();wpr_event *e=&wpr_events[0];
        if(!wpr_reserved){CHECK(fault&&wpr_closed&&!wpr_store_calls&&!wpr_open&&!wp_open&&!wpr_current);CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);continue;}
        if(!(e->complete&&e->errors&&!wpr_open&&!wp_open&&!wpr_current&&!e->debt&&!wpr_debt))fprintf(stderr,"read case fault=%u i=%u total=%u errors=%u wp=%u applied=%u debt=%u restored=%u\n",fault,i,read_calls,e->errors,wp_events[0].errors,e->applied,e->debt,e->restored);
        CHECK(e->complete&&e->errors&&!wpr_open&&!wp_open&&!wpr_current&&!e->debt&&!wpr_debt);
        CHECK(native_calls[0]==1&&native_calls[2]==1&&native_calls[3]==1);
        if(e->write_attempted)CHECK(e->restored&&*(uint64_t*)(tls+0x318)==123);else CHECK(!wpr_store_calls);
    }
    printf("Position RNG controlled read/STOP positions=%u each PASS\n",reads);
    rng_stop_race();rng_config_tests();rng_real_frames();
    printf("Position RNG production initial cases checks=%u\n",checks);return 0;
}
