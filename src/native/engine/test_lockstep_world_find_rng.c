/* Actual production observers; owned memory and exceptions only, no game. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned char context[0x200],entity[16],script[0x80],exec_state[0x80],node[0x240],order[0x78],registry[0xa0],bank[32],map_node[0x30],table[0x140];
static unsigned char components[4][0xe0],value[16],*native_tls,*abi_stub;
static uintptr_t program[2],nodes[234],items[4],vector[3];
static world_wait_event anchor_event;
static unsigned native_calls,assign_calls,mode,nested,foreign_tls;
static world_action_event *nested_parent;
static HANDLE reached,resume_event;
static HANDLE config_entered,config_release,stop_entered;static unsigned config_result,read_mutation,read_count;
static const char wire[]="FIND_COMPONENT_RNG_1;" WORLD_FIND_SNAPSHOT ";985d66488a43c795;65d8202ad9940908;";
static const char third_wire[]="FIND_COMPONENT_RNG_2;" WORLD_FIND_SNAPSHOT ";985d66488a43c795;65d8202ad9940908;" WORLD_FIND_THIRD_CONTRACT ";";
static unsigned test_third;
static void third_dependencies(void) {
    static const char observed[]=
      "0000000000080d54,4,5,401baaab8a5ce5b5,3fb999999999999a,3fe0000000000000,move.idle;"
      "000000000009d3a4,10,1,4019444523f67f4e,3f50624dd2f1a9fc,3ff0000000000000,order.fight.patrol;"
      "000000000006d945,4,5,401beeee0f3cb3e6,3fb999999999999a,3fe0000000000000,move.idle";
    assert(world_wait_parse(observed,sizeof(observed)-1,world_wait_selected,&world_wait_selected_count));
    assert(world_wait_rng_parse(WORLD_FIND_THIRD_POLICY,sizeof(WORLD_FIND_THIRD_POLICY)-1,world_wait_rng_selected,world_wait_rng_snapshot,&world_wait_rng_schema));
    strcpy(world_wait_rng_manifest,WORLD_FIND_THIRD_POLICY);
}
static int yes(void) {return 1;}
static int delayed_pins(void) {assert(SetEvent(config_entered));assert(WaitForSingleObject(config_release,5000)==WAIT_OBJECT_0);return 1;}
static int mutating_reader(const void *source,void *out,size_t size) {
    int ok=read_local(source,out,size);
    if(read_mutation==1&&source==order+8&&++read_count==1) ++*(uint64_t*)(order+8);
    if(read_mutation==2&&source==map_node+0x20&&size==16&&++read_count==1) ++*(uint64_t*)(map_node+0x28);
    return ok;
}
static void *tls_reader(void) {return foreign_tls?NULL:native_tls;}
static uintptr_t native_assign(void *a,void *b,void *c,void *d) {
    assert(a==node+0xf0&&b==exec_state+0x38&&c==node+0x100&&d==value);++assign_calls;return 0xfedcba9876543201ULL;
}
static uintptr_t native_candidate(void *a,void *b,void *c,void *d) {
    assert(a==node+0xf0&&b==vector&&c==exec_state+0x38&&!d);++native_calls;
    if(mode==5) RaiseException(0xe0427777,0,0,NULL);
    uint64_t *rng=(uint64_t*)(native_tls+0x318);*rng=world_find_step(*rng);
    *(unsigned*)value=0xa;*(uint64_t*)(value+8)=0x9000+(*rng%4);
    assert(world_values_assign_observe(native_assign,a,c,node+0x100,value,(uintptr_t)(image+0xd2ad67),read_local,tls_reader)==0xfedcba9876543201ULL);
    if(mode==1) ++*(uint64_t*)(order+8);
    if(mode==2) ++*(uint64_t*)(components[0]+8);
    if(mode==3) foreign_tls=1;
    if(mode==4) world_find_restore();
    if(mode==6) RaiseException(0xe0427777,0,0,NULL);
    if(mode==7) {DWORD old;assert(VirtualProtect(native_tls,4096,PAGE_NOACCESS,&old));RaiseException(0xe0427777,0,0,NULL);}
    if(mode==8&&!nested) {
        nested=1;world_action_event *parent=world_action_current;nested_parent=parent;world_action_current=NULL;
        world_values_candidate_observe(native_candidate,a,b,c,d,(uintptr_t)(image+0xb87ae8),read_local,tls_reader);
        world_action_current=parent;nested=0;
    } else if(mode==8&&nested==1) {
        nested=2;world_action_current=nested_parent;
        world_values_candidate_observe(native_candidate,a,b,c,d,(uintptr_t)(image+0xb87ae8),read_local,tls_reader);
        world_action_current=NULL;nested=1;
    }
    if(mode==9) {assert(SetEvent(reached));assert(WaitForSingleObject(resume_event,5000)==WAIT_OBJECT_0);}
    if(mode==10) {
        world_wait_rng_scope other={0};world_wait_event receipt=anchor_event;receipt.rng_before=*rng;
        uint64_t natural=*rng;world_wait_rng_begin(&other,&receipt,native_tls,tls_reader);
        assert((receipt.rng_refusal&WWR_NESTED)&&!receipt.rng_applied&&*rng==natural&&!other.active);world_wait_rng_end(&other);
    }
    return 0xaabbccdd11223344ULL;
}
static void native_action(void *a,void *b,void *c) {
    assert(a==node&&b==exec_state&&c==exec_state+0x38);
    if(mode==12) *(uint64_t*)(native_tls+0x318)=world_find_step(*(uint64_t*)(native_tls+0x318));
    if(mode==11) foreign_tls=1;
    assert(world_values_candidate_observe((world_values_fn)(abi_stub?abi_stub:(unsigned char*)native_candidate),node+0xf0,vector,c,NULL,
        (uintptr_t)(image+0xb87ae8),read_local,tls_reader)==0xaabbccdd11223344ULL);
    if(mode==12) *(uint64_t*)(native_tls+0x318)=world_find_step(*(uint64_t*)(native_tls+0x318));
}
static void call(void) {
    npc_provenance_frame frame;int entered=npc_provenance_enter(&frame,exec_state,0);assert(entered==1);
    __try {world_action_observe_with_tls(native_action,node,exec_state,exec_state+0x38,tls_reader);}
    __finally {npc_provenance_leave(&frame,entered);}
}
static DWORD WINAPI worker(void *unused) {(void)unused;ui_thread=GetCurrentThreadId();call();return 0;}
static DWORD WINAPI config_worker(void *unused) {(void)unused;ui_thread=GetCurrentThreadId();config_result=world_find_configure_using(wire,strlen(wire),delayed_pins);return 0;}
static DWORD WINAPI stop_worker(void *unused) {(void)unused;assert(SetEvent(stop_entered));world_find_restore();return 0;}
static void reset(void) {
    memset(context,0,sizeof(context));memset(entity,0,sizeof(entity));memset(script,0,sizeof(script));memset(exec_state,0,sizeof(exec_state));
    memset(node,0,sizeof(node));memset(order,0,sizeof(order));memset(registry,0,sizeof(registry));memset(bank,0,sizeof(bank));memset(map_node,0,sizeof(map_node));
    memset(world_find_events,0,sizeof(world_find_events));world_find_current=NULL;world_find_reserved=world_find_exported=world_find_used=0;
    world_find_anchor_ready=world_find_anchor_pending=world_find_anchor_errors=0;world_find_anchor_calls=world_find_calls=world_find_open=world_find_restore_open=world_find_failures=world_find_dropped=0;
    world_find_started=world_find_enabled=world_find_closed=0;world_find_composition=0;world_find_manifest[0]=0;
    memset(world_values_events,0,sizeof(world_values_events));world_values_reserved=world_values_calls=world_values_failures=world_values_dropped=world_values_sequence=0;
    world_values_open=world_values_restore_open=0;world_values_candidate_current=NULL;world_values_enabled=world_values_started=1;world_values_closing=0;world_values_sites=3;
    memset(world_action_events,0,sizeof(world_action_events));world_action_reserved=world_action_calls=world_action_failures=world_action_unknown=world_action_sequence=0;
    world_action_open=world_action_restore_open=0;world_action_current=NULL;world_action_depth=0;world_action_enabled=world_action_started=1;world_action_closing=0;
    world_action_selected_count=1;world_action_selected[0].saved=WORLD_FIND_SAVED;world_action_selected[0].version=10;strcpy(world_action_selected[0].name,"order.fight.patrol");
    world_wait_enabled=world_wait_rng_started=1;world_wait_restoring=0;world_wait_selected_count=3;world_wait_selected[0].saved=WORLD_FIND_SAVED;
    strcpy(world_wait_selected[0].name,"order.fight.patrol");world_wait_selected[0].version=10;world_wait_selected[0].index=1;
    world_wait_rng_selected[0].policy=WWR_INTERVENE;world_wait_rng_selected[0].slot=121;world_wait_rng_selected[0].pc=122;world_wait_rng_selected[0].seed=0x8b0f91e8910d9f7cULL;
    for(unsigned i=1;i<=2;i++) {world_wait_selected[i].saved=i==1?0x80d54:0x6d945;strcpy(world_wait_selected[i].name,"move.idle");world_wait_selected[i].version=4;world_wait_selected[i].index=5;}
    world_wait_rng_selected[1].policy=WWR_INTERVENE;world_wait_rng_selected[1].slot=70;world_wait_rng_selected[1].pc=71;world_wait_rng_selected[1].seed=0x41aa3ad655ad9a9cULL;
    world_wait_rng_selected[2].policy=WWR_OBSERVE;
    native_calls=assign_calls=mode=nested=foreign_tls=0;DWORD old;assert(VirtualProtect(native_tls,4096,PAGE_READWRITE,&old));memset(native_tls,0,4096);
    *(void**)context=image+0x2c6a270;*(uint64_t*)(context+8)=77;*(void**)(context+0x120)=entity;*(uint64_t*)(entity+8)=0x8111;
    *(void**)(context+0xd8)=script;*(void**)(script+0x10)="order.fight.patrol";*(uint64_t*)(script+0x20)=18;*(uint64_t*)(script+0x28)=31;*(unsigned*)(script+0x58)=10;
    *(void**)(context+0x148)=order;*(void**)order=image+0x2c69060;*(uint64_t*)(order+8)=0x40001;*(unsigned*)(order+0x20)=1;*(void**)(order+0x50)=context;
    *(void**)(image+0x6cf1750)=registry;*(void**)(registry+0x58)=bank;*(void**)bank=order;*(unsigned*)(bank+0x18)=1;*(unsigned*)(bank+0x1c)=0x10000;
    *(void**)(image+0x31c2de8)=map_node;*(uint64_t*)(map_node+0x20)=WORLD_FIND_ORDER;*(uint64_t*)(map_node+0x28)=0x40001;
    *(void**)node=image+0x2c0f3c8;*(void**)(image+0x2c0f3c8+0x18)=image+0xb87a30;*(unsigned*)(node+0xf0)=0x5b;*(unsigned*)(node+0x100)=0x54;
    for(unsigned i=0;i<234;i++) nodes[i]=(uintptr_t)node;program[0]=(uintptr_t)nodes;program[1]=(uintptr_t)(nodes+234);
    *(void**)(exec_state+0x10)=program;*(uint64_t*)(exec_state+0x18)=130;*(unsigned*)(exec_state+0x38)=0x66;*(void**)(exec_state+0x40)=context+0xa8;
    *(void**)(image+NPC_PROVENANCE_CURRENT)=exec_state;*(void**)(image+TABLE_RVA)=table;*(unsigned*)(image+0x6adf9fc)=0x20;*(unsigned*)(native_tls+0x314)=1;
    *(uint64_t*)(image+GAME_RVA)=*(uint64_t*)(image+REAL_RVA)=*(uint64_t*)(image+0x3d19790)=*(uint64_t*)(native_tls+0x300)=0x401c54fdf3b645a2ULL;
    *(uint64_t*)(native_tls+0x318)=0x0123456789abcdefULL;stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;boot_started=GetTickCount64();ui_thread=GetCurrentThreadId();main_thread=0;
    boot_options=281474976710656ULL;npc_provenance_enabled=1;npc_provenance_early_eligible=yes;npc_provenance_current=NULL;npc_provenance_denied_depth=npc_provenance_overflow_depth=0;
    if(test_third) third_dependencies();const char *configured=test_third?third_wire:wire;
    assert(world_find_configure_using(configured,strlen(configured),yes));assert(world_find_configure_using(configured,strlen(configured),yes));boot_phase=2;
    for(unsigned i=0;i<4;i++) {memset(components[i],0,sizeof(components[i]));*(void**)components[i]=image+0x2b66080;*(uint64_t*)(components[i]+8)=0x9000+i;components[i][0xd1]=1;items[i]=(uintptr_t)components[i];}
    vector[0]=(uintptr_t)items;vector[1]=(uintptr_t)(items+4);vector[2]=vector[1];
    memset(&anchor_event,0,sizeof(anchor_event));anchor_event.before.saved=WORLD_FIND_SAVED;strcpy(anchor_event.before.name,"order.fight.patrol");
    anchor_event.before.context=(uintptr_t)context;anchor_event.before.exec=(uintptr_t)exec_state;anchor_event.before.version=10;world_wait_rng_time("6.31667",&anchor_event.before.time);
    anchor_event.before.slot=121;anchor_event.before.pc=122;anchor_event.caller=(uintptr_t)(image+WORLD_WAIT_CALLER);anchor_event.boot=2;
    anchor_event.ui=anchor_event.selected=1;anchor_event.phase_before.view=anchor_event.phase_before.eligible=anchor_event.phase_before.vbi_loading=1;anchor_event.phase_before.app_flags=0x26;
    anchor_event.phase_before.game=anchor_event.phase_before.saved=anchor_event.phase_before.tls_time=0x401c54fdf3b645a2ULL;
    world_find_wait_anchor(&anchor_event,0);anchor_event.after=anchor_event.before;anchor_event.complete=1;world_find_wait_anchor(&anchor_event,1);assert(world_find_anchor_ready);
    /* The synthetic234-node program has its own honest shape hash. Native
     * configuration above separately refuses any non-approved shape. */
    world_action_guard g={0};g.begin=program[0];g.end=program[1];assert(world_action_program(&g,&world_find_shape));
}
static void verify_export(void) {
    char out[1536];for(unsigned part=0;part<4;part++) {assert(world_find_format(&world_find_events[0],1,part,out,sizeof(out))>0);printf("NATIVE_GATE %s\n",out);}
    world_find_exported=4;world_find_restore();assert(world_find_status(out,sizeof(out))>0);printf("NATIVE_GATE WORLD_FIND_STOP %s\n",out);
}
static void foreign_anchor(const world_wait_event *e,unsigned after) {(void)e;(void)after;}
static int foreign_conflict(void) {return 0;}
static uintptr_t foreign_candidate(world_values_fn original,void *a,void *b,void *c,void *d,world_values_event *e,
                                    world_action_event *p,void *tls,void*(*reader)(void)) {
    (void)original;(void)a;(void)b;(void)c;(void)d;(void)e;(void)p;(void)tls;(void)reader;return 0;
}
static void retry_unchanged(const char *text,unsigned expected) {
    char before[1024],after[1024];world_find_event events[WORLD_FIND_CAP];memcpy(events,world_find_events,sizeof(events));
    world_find_identity anchor=world_find_anchor;uint64_t rng=*(uint64_t*)(native_tls+0x318),shape=world_find_shape;
    unsigned calls=native_calls;assert(world_find_status(before,sizeof(before))>0);
    assert((unsigned)world_find_configure_using(text,strlen(text),yes)==expected);
    assert(world_find_status(after,sizeof(after))>0&&!strcmp(before,after)&&!memcmp(events,world_find_events,sizeof(events))&&
        !memcmp(&anchor,&world_find_anchor,sizeof(anchor))&&world_find_shape==shape&&*(uint64_t*)(native_tls+0x318)==rng&&native_calls==calls);
}
static void config_reload(void) {
    /* Original-call tests use an honest synthetic program hash. These separate
     * acknowledgment tests restore the exact retained production config shape;
     * they do not assert that the synthetic program has the native shape. */
    for(unsigned phase=1;phase<=3;phase++) for(unsigned used=0;used<=1;used++) {
        reset();if(used) call();world_find_shape=WORLD_FIND_SHAPE;boot_phase=phase;stats.issued=phase==3;
        retry_unchanged(wire,1);assert(world_wait_anchor_observer==world_find_wait_anchor&&world_wait_rng_foreign_active==world_find_conflict&&world_values_candidate_call==world_find_call);
    }
    for(unsigned which=0;which<24;which++) {
        reset();world_find_shape=WORLD_FIND_SHAPE;
        if(which==0) world_find_failures=1;if(which==1) world_find_dropped=1;if(which==2) world_find_restore_open=1;
        if(which==3) world_find_anchor_errors=WF_ANCHOR;if(which==4) world_find_open=1;if(which==5) world_find_anchor_pending=1;
        if(which==6) world_wait_anchor_observer=NULL;if(which==7) world_wait_rng_foreign_active=NULL;if(which==8) world_values_candidate_call=NULL;
        if(which==9) world_wait_anchor_observer=foreign_anchor;if(which==10) world_wait_rng_foreign_active=foreign_conflict;if(which==11) world_values_candidate_call=foreign_candidate;
        if(which==12) world_wait_enabled=0;if(which==13) world_wait_restoring=1;if(which==14) world_action_closing=1;
        if(which==15) world_values_closing=1;if(which==16) world_values_sites=1;if(which==17) ++world_wait_rng_selected[0].seed;
        if(which==18) world_wait_rng_selected[2].policy=WWR_INTERVENE;if(which==19) world_wait_selected_count=4;
        if(which==20) world_find_seed^=1;if(which==21) world_find_digest[0]^=1;if(which==22) world_find_shape^=1;
        if(which==23) ui_thread=GetCurrentThreadId()+1;
        retry_unchanged(wire,0);
        if(which==6) assert(!world_wait_anchor_observer);if(which==7) assert(!world_wait_rng_foreign_active);if(which==8) assert(!world_values_candidate_call);
        if(which==9) assert(world_wait_anchor_observer==foreign_anchor);if(which==10) assert(world_wait_rng_foreign_active==foreign_conflict);if(which==11) assert(world_values_candidate_call==foreign_candidate);
    }
    reset();mode=1;call();assert(world_find_failures&&world_find_events[0].errors);world_find_shape=WORLD_FIND_SHAPE;retry_unchanged(wire,0);
    reset();world_find_shape=WORLD_FIND_SHAPE;char changed[sizeof(wire)];memcpy(changed,wire,sizeof(wire));changed[sizeof(wire)-3]='0';retry_unchanged(changed,0);
    reset();world_find_shape=WORLD_FIND_SHAPE;world_find_restore();retry_unchanged(wire,0);
    for(unsigned phase=1;phase<=3;phase++) {reset();world_find_started=world_find_enabled=0;boot_phase=phase;retry_unchanged(wire,0);}
    reset();world_action_unknown=5;world_values_failures=6;world_find_shape=WORLD_FIND_SHAPE;retry_unchanged(wire,1); /* unrelated raw failures retained */
    reset();call();uint64_t synthetic=world_find_shape;world_find_shape=WORLD_FIND_SHAPE;retry_unchanged(wire,1);world_find_shape=synthetic;
    call();assert(native_calls==2&&(world_find_events[1].errors&WF_DUPLICATE)&&!world_find_events[1].applied);
    puts("Find native UI retry: healthy phase1/2/3 immutable acknowledgment, failed/debt/foreign dependency refusal, no late install or reseed PASS");
}
static void third_composition(void) {
    test_third=1;
    for(unsigned phase=1;phase<=3;phase++) for(unsigned used=0;used<=1;used++) {
        reset();if(used) call();world_find_shape=WORLD_FIND_SHAPE;boot_phase=phase;stats.issued=phase==3;
        retry_unchanged(third_wire,1);retry_unchanged(wire,0);
        char out[1024];assert(world_find_status(out,sizeof(out))>0&&strstr(out,"schema=2 ")&&strstr(out,"composition=" WORLD_FIND_THIRD_CONTRACT));
    }
    for(unsigned initial=0;initial<=1;initial++) for(unsigned role=0;role<3;role++) for(unsigned field=0;field<17;field++) {
        reset();world_find_shape=WORLD_FIND_SHAPE;world_wait_selection *w=&world_wait_selected[role];world_wait_rng_selection *r=&world_wait_rng_selected[role];
        if(field==0) ++w->saved;if(field==1) w->name[0]^=1;if(field==2) ++w->version;if(field==3) ++w->index;
        if(field==4) ++w->time;if(field==5) ++w->minimum;if(field==6) ++w->maximum;if(field==7) r->policy=WWR_OBSERVE;
        if(field==8) ++r->slot;if(field==9) ++r->pc;if(field==10) ++r->since;if(field==11) ++r->seed;if(field==12) r->used=2;
        if(field==13) r->text[0]^=1;if(field==14) ++r->script_hash;if(field==15) ++r->time_hash;if(field==16) ++world_wait_rng_snapshot[role];
        if(initial) {world_find_started=world_find_enabled=0;world_find_composition=0;world_find_manifest[0]=0;boot_phase=0;}
        retry_unchanged(third_wire,0);
    }
    for(unsigned field=0;field<12;field++) {
        reset();world_find_shape=WORLD_FIND_SHAPE;
        if(field==0) world_wait_rng_manifest[sizeof(WORLD_FIND_THIRD_POLICY)-2]^=1;
        if(field==1) world_wait_selected_count=2;if(field==2) world_wait_rng_schema=1;if(field==3) world_find_composition=1;
        if(field==4) world_find_failures=1;if(field==5) world_find_restore_open=1;if(field==6) world_find_open=1;
        if(field==7) world_wait_restoring=1;if(field==8) world_action_closing=1;if(field==9) world_values_closing=1;
        if(field==10) world_values_candidate_call=foreign_candidate;
        if(field==11) {world_wait_selection t=world_wait_selected[0];world_wait_selected[0]=world_wait_selected[2];world_wait_selected[2]=t;}
        retry_unchanged(third_wire,0);
    }
    reset();world_find_shape=WORLD_FIND_SHAPE;world_find_restore();retry_unchanged(third_wire,0);
    for(unsigned phase=1;phase<=3;phase++) {reset();world_find_started=world_find_enabled=0;world_find_composition=0;boot_phase=phase;retry_unchanged(third_wire,0);}
    reset();call();assert(native_calls==1&&assign_calls==1&&!world_find_events[0].errors&&world_find_events[0].restored);
    for(unsigned m=5;m<=6;m++) {reset();mode=m;unsigned caught=0;__try {call();} __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
        assert(caught&&native_calls==1&&!world_find_open&&world_find_events[0].abnormal&&world_find_events[0].errors);}
    test_third=0;reset();world_find_shape=WORLD_FIND_SHAPE;retry_unchanged(third_wire,0);
    third_dependencies();retry_unchanged(wire,0); /* No reinterpretation of legacy mode. */
    puts("Versioned exact third-Wait composition: parser/config/first-install/reload/dependency/STOP/original/TLS/SEH PASS");
}
static void image_pins_and_abi(const char *path) {
    FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&!fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size);assert(bytes&&fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);assert(hash_ok(bytes,(ULONG)size,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(void*)bytes;IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    unsigned char *saved=image;image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {IMAGE_SECTION_HEADER *s=&sections[i];assert((uint64_t)s->PointerToRawData+s->SizeOfRawData<=(uint64_t)size);memcpy(image+s->VirtualAddress,bytes+s->PointerToRawData,s->SizeOfRawData);}
    *(void**)(image+0x2c69060)=image+0x1075cb0;*(void**)(image+0x2c69068)=image+0xb3d60;assert(world_find_pins());
    const unsigned ends[]={0x1063750+3437,0x473600+107,0x473670+160,0x33d420+184,0x34dde0+296,0x1075c30+125,0x107b450+5231};
    for(unsigned i=0;i<7;i++) {image[ends[i]-1]^=1;assert(!world_find_pins());image[ends[i]-1]^=1;}
    image[0x31fc850]^=1;assert(!world_find_pins());image[0x31fc850]^=1;
    unsigned char *page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    assert(!memcmp(image+WORLD_VALUES_CANDIDATE,"\x48\x89\x5c\x24\x10",5));memcpy(page,image+WORLD_VALUES_CANDIDATE,5);thunk_absolute_jump(page+5,(void*)native_candidate);
    DWORD old;assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),page,4096));abi_stub=page;
    VirtualFree(image,0,MEM_RELEASE);image=saved;free(bytes);
}
int main(int argc,char **argv) {
    image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);native_tls=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image&&native_tls);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(world_identities);
    assert(world_identity_record(0x8111,WORLD_FIND_SAVED));const uint64_t saved[]={0x28,0x29,0x2b,0x428af};for(unsigned i=0;i<4;i++) assert(world_identity_record(0x9000+i,saved[i]));
    if(argc>1) image_pins_and_abi(argv[1]);
    unsigned char hash[32];uint64_t seed=0,shape=0;unsigned composition=0;assert(world_find_parse_composition(wire,strlen(wire),&shape,&seed,hash,&composition)&&composition==1);char digest_text[65];hex32(hash,digest_text);
    assert(!strcmp(digest_text,"080994d92a20d8658e7e391ea47ffdef7afe3ca8175ecabcb6fe34b7ba4020bc")&&seed==0x65d8202ad9940908ULL&&world_find_step(seed)==0x22403ede5ea8c4ddULL);
    unsigned char snapshot[32];for(unsigned i=0;i<32;i++) {unsigned byte=0;assert(sscanf(&WORLD_FIND_SNAPSHOT[i*2],"%2x",&byte)==1);snapshot[i]=(unsigned char)byte;}
    const char *vectors[]={"33efec817f40adf3957f98969a7bd6958ccd96b307d6c5f5a4a14e7b043831a0","90d684141195bbdc0d6b0b7011b8f1b5d8e63e58f004ac515560273fcac89a61","2480dc269c1cbc48b21c50ba13bf223e6234e08e81e501cede6aa26817b1e295","e09bc2b1dccf946977006a1a25399acd868394d1295a1e404c7109cf4156a02d"};
    for(unsigned i=0;i<4;i++) {assert(world_find_key(snapshot,WORLD_FIND_SAVED+(i==0),WORLD_FIND_ORDER+(i==1),i==2,i==3,hash,&seed));hex32(hash,digest_text);assert(!strcmp(digest_text,vectors[i]));}
    assert(!world_find_parse_composition(wire,strlen(wire)+1,&shape,&seed,hash,&composition));char bad[256];strcpy(bad,wire);bad[strlen(bad)-2]='0';assert(!world_find_parse_composition(bad,strlen(bad),&shape,&seed,hash,&composition));
    assert(world_find_parse_composition(third_wire,strlen(third_wire),&shape,&seed,hash,&composition)&&composition==2&&seed==0x65d8202ad9940908ULL);
    strcpy(bad,third_wire);bad[strlen(bad)-2]^=1;assert(!world_find_parse_composition(bad,strlen(bad),&shape,&seed,hash,&composition));
    reset();uint64_t before=*(uint64_t*)(native_tls+0x318);call();assert(native_calls==1&&assign_calls==1);
    assert(world_find_reserved==1&&world_find_events[0].complete&&world_find_events[0].applied&&world_find_events[0].restored&&!world_find_events[0].errors&&world_find_events[0].assigned==0x29);
    assert(*(uint64_t*)(native_tls+0x318)==before&&!world_find_open&&!world_find_current);verify_export();
    config_reload();
    third_composition();
    reset();world_values_candidate_call=NULL;call();uint64_t natural=*(uint64_t*)(native_tls+0x318);assert(natural==world_find_step(before)&&!world_find_reserved);
    reset();world_find_enabled=0;call();assert(*(uint64_t*)(native_tls+0x318)==natural&&!world_find_reserved);
    reset();world_action_selected[0].saved=0x9d3a5;call();assert(native_calls==1&&*(uint64_t*)(native_tls+0x318)==natural&&!world_find_reserved);
    reset();mode=12;call();assert(!world_find_events[0].errors&&world_find_events[0].assigned==0x29&&world_find_events[0].caller_before==natural&&
        world_find_events[0].caller_after==natural&&*(uint64_t*)(native_tls+0x318)==world_find_step(natural));
    for(unsigned which=0;which<11;which++) {
        reset();if(which==0) world_find_anchor_ready=0;if(which==1) ++*(uint64_t*)(order+8);if(which==2) *(void**)(order+0x50)=NULL;
        if(which==3) ++*(unsigned*)(bank+0x18);if(which==4) vector[1]-=8;if(which==5) node[0x110]=1;
        if(which==6) world_find_used=1;if(which==7) mode=11;if(which==8) ++*(uint64_t*)(context+8);
        if(which==9) *(unsigned*)(order+0x20)=2;if(which==10) *(void**)order=image+0x2c69068;
        call();assert(native_calls==1&&world_find_reserved==1&&!world_find_events[0].applied&&!world_find_events[0].restored&&world_find_events[0].errors);
        assert(*(uint64_t*)(native_tls+0x318)==natural&&!world_find_open);
    }
    reset();call();call();assert(native_calls==2&&world_find_events[1].errors&WF_DUPLICATE&&!world_find_events[1].applied);
    for(unsigned m=1;m<=10;m++) {
        if(m==9) continue;reset();mode=m;unsigned caught=0;
        __try {call();} __except(GetExceptionCode()==0xe0427777?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
        assert(caught==(m>=5&&m<=7));world_find_event *e=&world_find_events[0];assert(e->complete&&e->applied&&e->errors&&!world_find_open&&!world_find_current);
        if(m==3||m==7) assert(!e->restored&&(e->errors&WF_RESTORE));else assert(e->restored&&*(uint64_t*)(native_tls+0x318)==before);
        if(caught) assert(e->abnormal&&(e->errors&WF_UNWIND)&&world_values_open&&world_action_open); /* enclosing failure retained */
        if(m==8) assert(native_calls==3&&world_find_reserved==2&&(world_find_events[1].errors&WF_NESTED)&&!world_find_events[1].applied);
    }
    reset();mode=9;reached=CreateEventW(NULL,TRUE,FALSE,NULL);resume_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);assert(thread&&WaitForSingleObject(reached,5000)==WAIT_OBJECT_0);world_find_restore();assert(world_find_restore_open==1);
    assert(SetEvent(resume_event)&&WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);assert(world_find_events[0].restored&&(world_find_events[0].errors&WF_STOP)&&!world_find_open);
    CloseHandle(thread);CloseHandle(reached);CloseHandle(resume_event);reset();world_find_restore();call();assert(native_calls==1&&!world_find_reserved);
    reset();world_find_reserved=WORLD_FIND_CAP;call();assert(world_find_dropped==1&&!world_find_used&&native_calls==1&&*(uint64_t*)(native_tls+0x318)==natural);
    reset();world_find_wait_anchor(&anchor_event,0);assert(world_find_anchor_errors&WF_DUPLICATE);call();assert(world_find_events[0].errors&WF_ANCHOR&&!world_find_events[0].applied);
    world_find_identity immutable=world_find_anchor;world_wait_event duplicate=anchor_event;duplicate.before.context=0;
    world_find_wait_anchor(&duplicate,0);world_find_wait_anchor(&duplicate,1);assert(!memcmp(&immutable,&world_find_anchor,sizeof(immutable)));
    reset();uint64_t id=0;*(uint64_t*)(map_node+0x20)=1;*(void**)(map_node+16)=map_node;assert(!world_find_import_order(read_local,&id));
    reset();read_mutation=1;read_count=0;world_find_identity unstable;assert(!world_find_identity_stable(mutating_reader,(uintptr_t)context,(uintptr_t)exec_state,&unstable));
    reset();read_mutation=2;read_count=0;assert(!world_find_import_order(mutating_reader,&id));read_mutation=0;
    reset();world_find_restore();boot_phase=0;assert(!world_find_configure_using(wire,strlen(wire),yes));
    reset();world_find_started=world_find_enabled=0;world_find_manifest[0]=0;boot_phase=0;
    config_entered=CreateEventW(NULL,TRUE,FALSE,NULL);config_release=CreateEventW(NULL,TRUE,FALSE,NULL);stop_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE configuring=CreateThread(NULL,0,config_worker,NULL,0,NULL);assert(configuring&&WaitForSingleObject(config_entered,5000)==WAIT_OBJECT_0);
    HANDLE stopping=CreateThread(NULL,0,stop_worker,NULL,0,NULL);assert(stopping&&WaitForSingleObject(stop_entered,5000)==WAIT_OBJECT_0);
    assert(SetEvent(config_release)&&WaitForSingleObject(configuring,5000)==WAIT_OBJECT_0&&WaitForSingleObject(stopping,5000)==WAIT_OBJECT_0);
    assert(config_result&&!world_find_enabled&&world_find_closed&&!world_find_open);ui_thread=GetCurrentThreadId();assert(!world_find_configure_using(wire,strlen(wire),yes));
    CloseHandle(configuring);CloseHandle(stopping);CloseHandle(config_entered);CloseHandle(config_release);CloseHandle(stop_entered);
    world_find_event maximum;memset(&maximum,0xff,sizeof(maximum));for(unsigned part=0;part<4;part++) {char out[1536];int n=world_find_format(&maximum,UINT32_MAX,part,out,sizeof(out));assert(n>0&&n+64<1536);}
    if(abi_stub) VirtualFree(abi_stub,0,MEM_RELEASE);VirtualFree(native_tls,0,MEM_RELEASE);VirtualFree(world_identities,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    puts("FindComponent RNG key/order-generation/borrowed-input/refusal/original-once/fullRAX/real-SEH/STOP/overflow tests PASS");return 0;
}
