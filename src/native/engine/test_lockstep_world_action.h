static unsigned test_wa_calls,test_wa_mode,test_wa_nesting,test_wa_tls_mismatch;
static unsigned char *test_wa_tls,*test_wa_context,*test_wa_script,*test_wa_exec,*test_wa_node;
static HANDLE test_wa_entered,test_wa_release;
static int test_wa_early(void) {return 1;}
static void *test_wa_tls_reader(void) {return test_wa_tls_mismatch?NULL:test_wa_tls;}
static void test_wa_original(void *node,void *exec,void *frame) {
    assert(node==test_wa_node&&exec==test_wa_exec&&frame==test_wa_exec+0x38);++test_wa_calls;
    *(uint64_t*)(test_wa_tls+0x318)+=7;
    if(test_wa_mode==1) ++*(uint64_t*)(test_wa_context+8);
    if(test_wa_mode==2) world_action_restore();
    if(test_wa_mode==3) {assert(SetEvent(test_wa_entered));assert(WaitForSingleObject(test_wa_release,2000)==WAIT_OBJECT_0);}
    if((test_wa_mode==4||test_wa_mode==5)&&!test_wa_nesting) {
        test_wa_nesting=1;if(test_wa_mode==5) test_wa_script[0x10]='x';
        world_action_observe_with_tls(test_wa_original,node,exec,frame,test_wa_tls_reader);
        test_wa_script[0x10]='m';test_wa_nesting=0;
    }
    if(test_wa_mode==6) ++*(unsigned*)(image+0x6adf9fc);
    if(test_wa_mode==7) ++*(uint64_t*)(test_wa_exec+0x18); /* legitimate native PC movement */
    if(test_wa_mode==8&&test_wa_nesting<2) {
        ++test_wa_nesting;test_wa_script[0x10]=test_wa_nesting==1?'x':'m';
        world_action_observe_with_tls(test_wa_original,node,exec,frame,test_wa_tls_reader);
        --test_wa_nesting;test_wa_script[0x10]=test_wa_nesting?'x':'m';
    }
    if(test_wa_mode==9) *(void**)(image+NPC_PROVENANCE_CURRENT)=NULL;
    if(test_wa_mode==10) ++*(uintptr_t*)(*(uintptr_t*)(test_wa_exec+0x10)+8);
    if(test_wa_mode==11) *(void**)node=image+0x2c0cc90;
    if(test_wa_mode==12) test_wa_tls_mismatch=1;
}
static unsigned test_wa_callback_calls;
static void test_wa_callback(nested_action_fn original,void *node,void *exec,void *frame) {
    ++test_wa_callback_calls;assert(original==nested_action_chain);original(node,exec,frame);
}
static void test_wa_reset(void) {
    memset(world_action_events,0,sizeof(world_action_events));world_action_current=NULL;world_action_depth=0;
    world_action_calls=world_action_reserved=world_action_sequence=world_action_failures=world_action_dropped=0;
    world_action_other_owner=world_action_other_script=world_action_other_context=world_action_other_phase=0;
    world_action_unknown=world_action_threads=world_action_open=world_action_peak=world_action_restore_open=world_action_unknown_rng=0;
    world_action_exported=0;world_action_closing=0;world_action_enabled=1;world_action_started=1;
}
static void test_wa_call(void) {
    npc_provenance_frame frame;int entered=npc_provenance_enter(&frame,test_wa_exec,0);assert(entered==1);
    unsigned calls=test_wa_calls;uint64_t before=*(uint64_t*)(test_wa_tls+0x318);
    world_action_observe_with_tls(test_wa_original,test_wa_node,test_wa_exec,test_wa_exec+0x38,test_wa_tls_reader);
    assert(test_wa_calls>calls&&*(uint64_t*)(test_wa_tls+0x318)==before+7*(test_wa_calls-calls));
    /* For the deliberate native CURRENT mutation, restore only the synthetic
     * fixture before closing its enclosing NPC bracket; the Action event has
     * already retained the postcondition failure. */
    if(test_wa_mode==9) *(void**)(image+NPC_PROVENANCE_CURRENT)=test_wa_exec;
    npc_provenance_leave(&frame,entered);
}
static DWORD WINAPI test_wa_worker(void *unused) {(void)unused;ui_thread=GetCurrentThreadId();test_wa_call();return 0;}
static void test_wa_image(const char *manifest) {
    if(!test_wq_image_path) return;
    FILE *f=fopen(test_wq_image_path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&!fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);assert(hash_ok(bytes,(ULONG)size,EXE_SHA));
    unsigned char *saved_image=image;int saved_dt=dt_installed;image=VirtualAlloc(NULL,0x6fa9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(image);dt_installed=0;
    const unsigned phase_rvas[]={TICK_RVA,0xac2a50,0xac2c70,0x9ee7a0,0x14b8dc0,0x14b8e20},phase_sizes[]={2127,522,446,9772,89,101};
    for(unsigned i=0;i<6;i++) test_ww_copy_rva(bytes,(size_t)size,phase_rvas[i],image+phase_rvas[i],phase_sizes[i]);
    for(unsigned i=0;i<sizeof(world_action_pin_list)/sizeof(world_action_pin_list[0]);i++) {
        const world_action_pin *p=&world_action_pin_list[i];test_ww_copy_rva(bytes,(size_t)size,p->rva,image+p->rva,p->size);
    }
    for(unsigned i=0;i<sizeof(world_values_pin_list)/sizeof(*world_values_pin_list);i++) {
        const world_action_pin *p=&world_values_pin_list[i];test_ww_copy_rva(bytes,(size_t)size,p->rva,image+p->rva,p->size);
    }
    uint64_t component_target;test_ww_copy_rva(bytes,(size_t)size,0x2c0f3c8+0x18,&component_target,8);
    assert(component_target==0x140b87a30ULL);*(void**)(image+0x2c0f3c8+0x18)=image+0xb87a30;
    const unsigned types[]={0x2c0cc88,0x2c0cdd0,0x2c0fb30},targets[]={0xbe2320,0xbe0ba0,0xb77830};
    for(unsigned i=0;i<3;i++) {
        uint64_t target;test_ww_copy_rva(bytes,(size_t)size,types[i]+0x18,&target,8);assert(target==0x140000000ULL+targets[i]);
        *(uintptr_t*)(image+types[i]+0x18)=(uintptr_t)(image+targets[i]);
    }
    test_ww_copy_rva(bytes,(size_t)size,NPC_EXEC_ENTRY,image+NPC_EXEC_ENTRY,303);
    uint64_t saved_original=npc_exec_original_word,saved_patched=npc_exec_patched_word;
    unsigned __int128 saved_nested_original=nested_action_original,saved_nested_patched=nested_action_patched;
    int saved_npc=npc_exec_installed,saved_nested=nested_action_installed;
    memcpy(&npc_exec_original_word,image+NPC_EXEC_ENTRY,8);npc_exec_patched_word=npc_exec_original_word^0x12345;
    memcpy(&nested_action_original,image+NESTED_ACTION_WORD,16);nested_action_patched=nested_action_original^0x56789;
    memcpy(image+NPC_EXEC_ENTRY,&npc_exec_patched_word,8);memcpy(image+NESTED_ACTION_WORD,&nested_action_patched,16);
    npc_exec_installed=nested_action_installed=1;assert(world_action_pins());
    assert(world_values_pins());
    for(unsigned i=0;i<sizeof(world_values_pin_list)/sizeof(*world_values_pin_list);i++) {
        const world_action_pin *p=&world_values_pin_list[i];image[p->rva+p->size-1]^=1;assert(!world_values_pins());image[p->rva+p->size-1]^=1;
    }
    for(unsigned i=0;i<sizeof(world_action_pin_list)/sizeof(world_action_pin_list[0]);i++) {
        const world_action_pin *p=&world_action_pin_list[i];image[p->rva+p->size-1]^=1;assert(!world_action_pins());image[p->rva+p->size-1]^=1;
    }
    image[NPC_EXEC_ENTRY+20]^=1;assert(!world_action_pins());image[NPC_EXEC_ENTRY+20]^=1;
    image[NPC_EXEC_ENTRY]^=1;assert(!world_action_pins());image[NPC_EXEC_ENTRY]^=1;
    image[NESTED_ACTION_WORD]^=1;assert(!world_action_pins());image[NESTED_ACTION_WORD]^=1;
    ++*(uintptr_t*)(image+types[0]+0x18);assert(!world_action_pins());--*(uintptr_t*)(image+types[0]+0x18);
    world_action_started=0;world_action_enabled=0;world_action_closing=0;world_trace_early=1;world_queue_enabled=1;boot_phase=0;
    assert(world_action_configure(manifest,strlen(manifest))&&world_action_started&&world_action_callback==(PVOID)world_action_observe);
    boot_phase=2;assert(world_action_configure(manifest,strlen(manifest)));assert(!world_action_configure(manifest,strlen(manifest)-1));
    world_action_restore();assert(!world_action_configure(manifest,strlen(manifest)));world_action_started=0;
    assert(!world_action_configure(manifest,strlen(manifest))); /* STOP cannot reopen even before first configuration */
    world_trace_early=0;world_queue_enabled=0;world_action_callback=NULL;
    npc_exec_original_word=saved_original;npc_exec_patched_word=saved_patched;npc_exec_installed=saved_npc;
    nested_action_original=saved_nested_original;nested_action_patched=saved_nested_patched;nested_action_installed=saved_nested;
    VirtualFree(image,0,MEM_RELEASE);image=saved_image;dt_installed=saved_dt;free(bytes);
    puts("world Action exact image pins, owned dispatch reconstruction and immutable configuration PASS");
}
#include "test_lockstep_world_values.h"
static void test_world_action(void) {
    unsigned char *outer_image=image;image=VirtualAlloc(NULL,0x6fa9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(image);
    const char *manifest="0000000000000099,4,move.idle";world_action_selection parsed[WORLD_ACTION_SELECTED];unsigned count;
    assert(world_action_parse(manifest,strlen(manifest),parsed,&count)&&count==1);
    assert(!world_action_parse(manifest,strlen(manifest)+1,parsed,&count));assert(!world_action_parse("",0,parsed,&count));
    const char *bad[]={"99,4,move.idle;","99,-1,move.idle","99,4,move.idle;99,4,move.idle","99,4,a;98,4,b;97,4,c","99,4,a b","0,4,move.idle"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!world_action_parse(bad[i],strlen(bad[i]),parsed,&count));
    assert(world_action_parse(manifest,strlen(manifest),parsed,&count));world_action_selected[0]=parsed[0];world_action_selected_count=1;
    unsigned char node[0x240]={0},exec[0x60]={0},context[0x130]={0},entity[16]={0},script[0x60]={0},tls[0x320]={0},table[0x134]={0};
    uintptr_t nodes[]={(uintptr_t)node},program[]={(uintptr_t)nodes,(uintptr_t)(nodes+1)};
    *(void**)node=image+0x2c0cc88;*(void**)(image+0x2c0cc88+0x18)=image+0xbe2320;
    *(void**)(exec+0x10)=program;*(uint64_t*)(exec+0x18)=1;*(unsigned*)(exec+0x38)=0x66;*(void**)(exec+0x40)=context+0xa8;
    *(void**)context=image+0x2c6a270;*(uint64_t*)(context+8)=7;*(void**)(context+0x120)=entity;*(void**)(context+0xd8)=script;*(uint64_t*)(entity+8)=0x9999;
    memcpy(script+0x10,"move.idle",9);*(uint64_t*)(script+0x20)=9;*(uint64_t*)(script+0x28)=15;*(unsigned*)(script+0x58)=4;
    *(void**)(image+TABLE_RVA)=table;*(unsigned*)(image+0x6adf9fc)=0x20;*(unsigned*)(tls+0x314)=1;
    *(double*)(image+GAME_RVA)=*(double*)(image+REAL_RVA)=*(double*)(image+0x3d19790)=*(double*)(tls+0x300)=7.083;
    *(void**)(image+NPC_PROVENANCE_CURRENT)=exec;
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(world_identities);assert(world_identity_record(0x9999,0x99));
    test_wa_tls=tls;test_wa_node=node;test_wa_exec=exec;test_wa_context=context;test_wa_script=script;
    stats.active=1;stats.issued=0;boot_mode=1;boot_phase=2;boot_started=GetTickCount64();ui_thread=GetCurrentThreadId();main_thread=0;
    uint64_t options=boot_options;boot_options=281474976710656ULL;npc_provenance_enabled=1;npc_provenance_early_eligible=test_wa_early;
    world_action_enabled=0;test_wa_calls=0;test_wa_mode=0;test_wa_call();assert(test_wa_calls==1&&!world_action_reserved); /* absent opt-in is inert */
    /* Exercise the real existing dispatch chain with and without the optional
     * callback; it adds no second original and preserves all three arguments. */
    *(void**)(image+0x2c0cc88+0x18)=(void*)test_wa_original;unsigned chain_calls=test_wa_calls;
    nested_action_run(node,exec,exec+0x38);assert(test_wa_calls==chain_calls+1);
    world_action_callback=(PVOID)test_wa_callback;nested_action_run(node,exec,exec+0x38);
    assert(test_wa_calls==chain_calls+2&&test_wa_callback_calls==1);world_action_callback=NULL;
    *(void**)(image+0x2c0cc88+0x18)=image+0xbe2320;
    test_wa_reset();test_wa_call();assert(world_action_ready()==1&&!world_action_events[0].errors&&world_action_events[0].stage==1);
    assert(world_action_events[0].rng_after==world_action_events[0].rng_before+7&&!world_action_open);
    /* Export a real production observer record for the independent reader. */
    world_action_restore();for(unsigned p=0;p<WORLD_ACTION_ROWS;p++) {char out[WORLD_ACTION_ROW_CAP];assert(world_action_format(&world_action_events[0],1,p,out,sizeof(out))>0);printf("NATIVE_GATE %s\n",out);}
    world_action_exported=3;char status[1024];assert(world_action_status(status,sizeof(status))>0);printf("NATIVE_GATE WORLD_ACTION_STOP %s\n",status);
    world_action_event maximum;memset(&maximum,0xff,sizeof(maximum));memset(maximum.before.name,'a',95);memset(maximum.after.name,'a',95);maximum.before.name[95]=maximum.after.name[95]=0;
    for(unsigned p=0;p<WORLD_ACTION_ROWS;p++) {char row[2048],small[32];int n=world_action_format(&maximum,UINT32_MAX,p,row,sizeof(row));assert(n>0&&n+sizeof("[X4LAN] NATIVE_GATE ")+1<2048);assert(world_action_format(&maximum,1,p,small,sizeof(small))==-1);}
    test_wa_reset();test_wa_mode=4;test_wa_call();assert(world_action_ready()==2&&!world_action_failures&&world_action_events[0].children==1&&world_action_events[1].parent==1&&world_action_peak==2);
    test_wa_reset();test_wa_mode=5;test_wa_call();assert(world_action_ready()==1&&!world_action_failures&&world_action_events[0].unobserved_children==1&&world_action_other_script==1);
    test_wa_reset();test_wa_mode=8;test_wa_call();assert(world_action_ready()==2&&!world_action_failures&&world_action_events[0].unobserved_children==1&&world_action_other_script==1);
    assert(world_action_events[1].parent==0&&world_action_events[1].depth==3&&world_action_peak==3);
    test_wa_mode=0;test_wa_reset();++*(unsigned*)(script+0x58);test_wa_call();assert(world_action_events[0].errors&WA_VERSION);--*(unsigned*)(script+0x58);
    test_wa_reset();test_wa_mode=1;test_wa_call();assert(world_action_events[0].errors&WA_LIFETIME);--*(uint64_t*)(context+8);
    test_wa_reset();test_wa_mode=6;test_wa_call();assert(world_action_events[0].errors&WA_PHASE);*(unsigned*)(image+0x6adf9fc)=0x20;
    test_wa_reset();test_wa_mode=7;test_wa_call();assert(!world_action_events[0].errors&&world_action_events[0].pc_after==2);*(uint64_t*)(exec+0x18)=1;
    unsigned char input_exec[sizeof(exec)],input_tls[sizeof(tls)],output_exec[sizeof(exec)],output_tls[sizeof(tls)];
    memcpy(input_exec,exec,sizeof(exec));memcpy(input_tls,tls,sizeof(tls));test_wa_original(node,exec,exec+0x38);
    memcpy(output_exec,exec,sizeof(exec));memcpy(output_tls,tls,sizeof(tls));memcpy(exec,input_exec,sizeof(exec));memcpy(tls,input_tls,sizeof(tls));
    test_wa_reset();test_wa_call();assert(!memcmp(exec,output_exec,sizeof(exec))&&!memcmp(tls,output_tls,sizeof(tls))&&!world_action_failures);*(uint64_t*)(exec+0x18)=1;
    test_wa_reset();test_wa_mode=9;test_wa_call();assert(world_action_events[0].errors&WA_LIFETIME);
    test_wa_reset();test_wa_mode=10;test_wa_call();assert(world_action_events[0].errors&WA_LIFETIME);--program[1];
    test_wa_reset();test_wa_mode=11;test_wa_call();assert(world_action_events[0].errors&WA_LIFETIME);*(void**)node=image+0x2c0cc88;
    test_wa_reset();test_wa_mode=12;test_wa_call();assert(world_action_events[0].errors&WA_READ);test_wa_tls_mismatch=0;
    test_wa_reset();test_wa_mode=0;*(uint64_t*)(entity+8)=0x9998;test_wa_call();assert(world_action_unknown==1&&!world_action_reserved);*(uint64_t*)(entity+8)=0x9999;
    test_wa_reset();*(void**)(image+NPC_PROVENANCE_CURRENT)=NULL;test_wa_call();assert(world_action_events[0].errors&WA_OWNER);*(void**)(image+NPC_PROVENANCE_CURRENT)=exec;
    test_wa_reset();*(void**)(image+0x2c0cc88+0x18)=image+0xbe2321;test_wa_call();assert(world_action_events[0].errors&WA_NODE);*(void**)(image+0x2c0cc88+0x18)=image+0xbe2320;
    test_wa_reset();*(void**)node=image+0x2c0cc90;*(void**)(image+0x2c0cc90+0x18)=image+0xbe2320;test_wa_call();assert(!world_action_failures&&world_action_unknown_rng==1&&!world_action_events[0].stage);*(void**)node=image+0x2c0cc88;
    test_wa_reset();world_action_depth=64;test_wa_call();assert(world_action_events[0].errors&WA_DEPTH);world_action_depth=0;
    test_wa_reset();world_action_reserved=WORLD_ACTION_MAX;unsigned calls=test_wa_calls;test_wa_call();assert(world_action_dropped==1&&test_wa_calls==calls+1);
    test_wa_reset();test_wa_mode=2;test_wa_call();assert(world_action_restore_open==1&&!world_action_open&&(world_action_events[0].errors&WA_STOP));calls=test_wa_calls;test_wa_call();assert(test_wa_calls==calls+1&&world_action_ready()==1);
    test_wa_reset();test_wa_mode=3;test_wa_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_wa_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE worker=CreateThread(NULL,0,test_wa_worker,NULL,0,NULL);assert(worker&&WaitForSingleObject(test_wa_entered,2000)==WAIT_OBJECT_0);
    assert(world_action_open==1&&!world_action_ready());world_action_restore();assert(world_action_restore_open==1&&!world_action_admit());
    assert(SetEvent(test_wa_release)&&WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);assert(world_action_ready()==1&&(world_action_events[0].errors&WA_STOP)&&!world_action_open);
    CloseHandle(worker);CloseHandle(test_wa_entered);CloseHandle(test_wa_release);ui_thread=GetCurrentThreadId();test_wa_mode=0;
    test_world_values();
    test_wa_image(manifest);
    VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;world_action_callback=NULL;
    test_wa_reset();world_action_started=world_action_enabled=world_action_selected_count=0;world_action_manifest[0]=0;
    stats.active=0;boot_phase=0;ui_thread=0;boot_options=options;npc_provenance_enabled=0;npc_provenance_early_eligible=NULL;
    VirtualFree(image,0,MEM_RELEASE);image=outer_image;
    puts("world Action selected/foreign nesting, natural RNG, lifecycle, overflow, original-once and STOP overlap PASS");
}
