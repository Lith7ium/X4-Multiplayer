/* Production scope executors over a pinned image and owned memory. Native
 * engine instructions never execute. Assertions remain active at -O2. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned checks,calls,mode,scope_kind;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"CHECK line %u: %s\n",__LINE__,#x);exit(97);}}while(0)
static unsigned char object[0x400],macro[0x40],owner[0x40],controller[0xe0],agent[0xf20],fixed[0x60];
static unsigned char *tls,*replacement;static void *object_ref,*local_ref,*old_tls;
static void **tls_slots;
static const char manifest[]=CREATION_RNG_PREFIX "1" CREATION_RNG_SUFFIX;
static const char preimage[]=CREATION_RNG_BOOTSTRAP CREATION_RNG_PREFIX "1" CREATION_RNG_SUFFIX CREATION_RNG_ACK_SUFFIX;
static uint64_t caller_word=0x1122334455667788ULL;
static void *test_tls;
static void *read_test_tls(void) {return test_tls;}
static HANDLE stop_entered,stop_finished,test_stop_thread;static unsigned stop_reads;
static DWORD WINAPI concurrent_stop(LPVOID unused) {
    (void)unused;SetEvent(stop_entered);finish(1);SetEvent(stop_finished);return 0;
}
static void *read_with_pending_stop(void) {
    if(++stop_reads==1) {
        CHECK(ResumeThread(test_stop_thread)==1);
        CHECK(WaitForSingleObject(stop_entered,5000)==WAIT_OBJECT_0);
        CHECK(WaitForSingleObject(stop_finished,20)==WAIT_TIMEOUT);
        CHECK(stats.active==1);
    }
    return test_tls;
}
static void set_seed_flags(void) {
    flight_seeded=ai_seeded=agent_seeded=ship_seeded=weapon_seeded=nav_seeded=shape_seeded=bullet_seeded=
        death_seeded=attention_seeded=gravidar_seeded=interrupt_seeded=1;
}
static void load_image(const char *path) {
    FILE *file=fopen(path,"rb");CHECK(file&&!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0&&!fseek(file,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)size);CHECK(bytes&&fread(bytes,1,(size_t)size,file)==(size_t)size);fclose(file);
    CHECK(hash_ok(bytes,(ULONG)size,EXE_SHA));IMAGE_DOS_HEADER *dos=(void*)bytes;IMAGE_NT_HEADERS64 *nt=(void*)(bytes+dos->e_lfanew);
    IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);image=VirtualAlloc(NULL,0x6fa9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(image);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {IMAGE_SECTION_HEADER *s=sections+i;
        CHECK((uint64_t)s->PointerToRawData+s->SizeOfRawData<=(uint64_t)size);memcpy(image+s->VirtualAddress,bytes+s->PointerToRawData,s->SizeOfRawData);}
    free(bytes);for(unsigned i=0;i<sizeof(creation_pins)/sizeof(*creation_pins);i++)
        CHECK(hash_ok(image+creation_pins[i].rva,creation_pins[i].length,creation_pins[i].sha256));
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++) {const creation_patch_contract *p=creation_patch_contracts+i;
        memcpy(creation_original_words[i],image+p->word,p->width);memcpy(creation_patched_words[i],image+p->word,p->width);
        creation_patched_words[i][0]^=0x40;memcpy(image+p->word,creation_patched_words[i],p->width);}
    creation_owned=(1u<<CREATION_HOOK_COUNT)-1;CHECK(creation_rng_pins_locked());
}
static unsigned enter(unsigned kind,uint64_t *args,unsigned count) {
    return creation_enter_context(kind,(uintptr_t)(image+creation_patch_contracts[kind-1].expected_caller),args,count,tls);
}
static void reset_before_activity(void) {
    DWORD old;CHECK(VirtualProtect(tls,4096,PAGE_READWRITE,&old));tls_slots[0]=tls;
    memset(tls,0,4096);*(double*)(tls+0x300)=100.0;tls[0x314]=1;*(uint64_t*)(tls+0x318)=caller_word;
    memset(object,0,sizeof(object));memset(macro,0,sizeof(macro));memset(owner,0,sizeof(owner));memset(controller,0,sizeof(controller));
    memset(agent,0,sizeof(agent));memset(fixed,0,sizeof(fixed));object_ref=local_ref=object;
    *(void**)object=image+0x2b99a88;*(uint64_t*)(object+8)=0x987654321ULL;*(void**)(object+0x60)=macro;
    *(void**)macro=image+0x2ba5430;*(const char**)(macro+0x20)=CREATION_RNG_MACRO;*(uint64_t*)(macro+0x30)=strlen(CREATION_RNG_MACRO);*(uint64_t*)(macro+0x38)=31;
    *(void**)(image+0x2ba5430+18*8)=image+0xefb10;*(void**)(image+0x2b99a88+0x1620)=image+0x352b10;
    memcpy(owner+0x10,"player",7);*(uint64_t*)(owner+0x20)=6;*(uint64_t*)(owner+0x28)=15;*(void**)fixed=image+0x2a34b38;
    *(unsigned*)(image+0x38f4ee4)=1;*(unsigned*)(image+0x2f4b6f4)=0;
    memset(creation_records,0,sizeof(creation_records));memset(&creation_selected,0,sizeof(creation_selected));
    creation_started=creation_enabled=1;creation_closing=creation_window=creation_count=creation_current=0;
    creation_failures=creation_dropped=creation_open=creation_peak=creation_restore_open=creation_restore_failures=creation_first_error=0;
    memset(creation_rng_records,0,sizeof(creation_rng_records));creation_rng_manifest[0]=0;creation_rng_started=0;
    memset(&creation_attach_boot,0,sizeof(creation_attach_boot));creation_attach_rejected=0;
    creation_rng_ack_mode=creation_rng_ack_phase=creation_rng_ack_registry_phase=creation_rng_ack_registry=0;
    creation_rng_ack_receipt[0]=0;
    creation_rng_closing=creation_rng_count=creation_rng_active=creation_rng_attempted=0;
    creation_rng_failures=creation_rng_restore_failures=creation_rng_restore_open=0;creation_rng_epoch=0;
    memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;registry_failures=registry_md_failures=registry_origin_failures=registry_md_order=0;
    memset(actor_components+ACTOR_DYNAMIC_FIRST-1,0,sizeof(void*)*ACTOR_DYNAMIC_COUNT);
    memset(actor_ids+ACTOR_DYNAMIC_FIRST-1,0,sizeof(uint64_t)*ACTOR_DYNAMIC_COUNT);
    flight_failures=ai_failures=agent_failures=ship_failures=weapon_failures=nav_failures=shape_failures=bullet_failures=
        death_failures=attention_failures=gravidar_failures=interrupt_failures=0;
    stats.active=1;stats.issued=0;stats.slot=1;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();boot_options=CREATION_RNG_OPTIONS;
    used=1;remote_port=22529;boot_started=42;strcpy(secret,"0123456789abcdef0123456789abcdef");
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++)memcpy(image+creation_patch_contracts[i].word,creation_patched_words[i],creation_patch_contracts[i].width);
    creation_owned=(1u<<CREATION_HOOK_COUNT)-1;
    registry_md_installed=registry_origin_enabled=world_identity_installed=1;set_seed_flags();mode=calls=0;
    strcpy(initial_state_digest,"1ce03def6a724e38f50bb9f32096f7829a91f0f47b32147153b1d2eeca1a79ba");
    CHECK(creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
    CHECK(creation_rng_keys[0]==0x8b34a17d4ae84a2fULL&&creation_rng_keys[1]==0x5311c417920e6e36ULL);
}
static void reset(void) {
    reset_before_activity();
    CHECK(registry_reserve(0x0000000500000003ULL,CREATION_RNG_MACRO,strlen(CREATION_RNG_MACRO),"player",6,5)==ACTOR_DYNAMIC_FIRST);
    boot_phase=3;stats.issued=6;
    creation_md_note(1,1,1,registry_md_next_order(),6,0x0000000500000003ULL,1,1,0,ui_thread,31);
    CHECK(creation_selected.state==1&&!creation_failures);
}
static unsigned child_calls;
static void child_throw(void *self) {
    CHECK(self==object);++child_calls;*(uint64_t*)(tls+0x318)^=0xfedcba9876543210ULL;
    ULONG_PTR args[]={0x0123456789abcdefULL,0xfedcba9876543210ULL};RaiseException(0xe0428888,0,2,args);
}
static void child_return(void *self) {CHECK(self==object);++child_calls;*(uint64_t*)(tls+0x318)^=0x8888888888888888ULL;}
static int child_exception(EXCEPTION_POINTERS *p) {
    CHECK(p->ExceptionRecord->ExceptionCode==0xe0428888&&p->ExceptionRecord->NumberParameters==2);
    CHECK(p->ExceptionRecord->ExceptionInformation[0]==0x0123456789abcdefULL&&p->ExceptionRecord->ExceptionInformation[1]==0xfedcba9876543210ULL);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void behavior(void) {
    ++calls;CHECK(*(uint64_t*)(tls+0x318)==creation_rng_keys[scope_kind-1] || mode==10 || mode==11);
    *(uint64_t*)(tls+0x318)^=0x123456789abcdef0ULL;
    if(mode==1)RaiseException(0xe0427777,0,0,NULL);
    if(mode==2)creation_rng_stop();
    if(mode==3){creation_rng_stop();RaiseException(0xe0427777,0,0,NULL);}
    if(mode==4)tls_slots[0]=replacement;
    if(mode==5)*(double*)(tls+0x300)=101.0;
    if(mode==6)++ship_failures;
    if(mode==7){DWORD old;CHECK(VirtualProtect(tls,4096,PAGE_NOACCESS,&old));RaiseException(0xe0427777,0,0,NULL);}
    if(mode==8)flight_seeded=0;
    if(mode==9) {
        uint64_t parent=*(uint64_t*)(tls+0x318);ship_sample sample={.calls=1};child_calls=0;unsigned caught=0;
        __try {ship_execute(child_throw,&sample,1,(uint64_t*)(tls+0x318),object);}
        __except(child_exception(GetExceptionInformation())) {caught=1;}
        CHECK(caught&&child_calls==1&&*(uint64_t*)(tls+0x318)==parent&&ship_failures==1);
        sample.calls=2;ship_execute(child_return,&sample,1,(uint64_t*)(tls+0x318),object);
        CHECK(child_calls==2&&*(uint64_t*)(tls+0x318)==parent&&ship_failures==1);
    }
    if(mode==12||mode==13) {
        /* Those observers are modeled as ready for input admission, not
         * installed native patches. Actual creation words are owned below. */
        registry_md_installed=world_identity_installed=0;x4gate_stop();
        CHECK(!stats.active&&creation_rng_closing&&!creation_owned&&creation_rng_restore_open==1);
        if(mode==13)RaiseException(0xe0427777,0,0,NULL);
    }
    SetLastError(0x56781234);
}
static uintptr_t original_generate(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,unsigned char e,uintptr_t f,uintptr_t g,unsigned h,unsigned char i,unsigned char j) {
    CHECK(a==0xabc&&b==(uintptr_t)&object_ref&&c==0xdef&&!d&&e==1&&f==2&&g==3&&h==4&&i==1&&!j);
    behavior();if(mode==4||mode==12)return (uintptr_t)&object_ref;
    uint64_t init[]={0,(uintptr_t)&local_ref,0,0,0,0,0,0,1};unsigned n=enter(CREATION_INITIALIZE,init,9);CHECK(n);
    *(void**)(object+0x348)=owner;uint64_t pilot[]={0,0,(uintptr_t)&local_ref,(uintptr_t)owner,0,0,0};unsigned p=enter(CREATION_PILOT,pilot,7);CHECK(p);
    creation_leave_context(p,0,1,tls);creation_leave_context(n,0,0,tls);SetLastError(0x56781234);return (uintptr_t)&object_ref;
}
static void call_a(void) {
    scope_kind=1;CHECK(creation_generate_execute(original_generate,(uintptr_t)(image+0xba1b8b),0xabc,(uintptr_t)&object_ref,0xdef,0,1,2,3,4,1,0)==(uintptr_t)&object_ref);
}
static void prepare_b(void) {
    uint64_t setter[]={(uintptr_t)object,(uintptr_t)fixed,1};unsigned n=enter(CREATION_SETTER,setter,3);CHECK(n);
    *(void**)(object+0x200)=fixed;creation_leave_context(n,0,0,tls);CHECK(!creation_failures);
    creation_md_note(1,2,0,registry_md_next_order(),6,creation_selected.handle,2,1,0,ui_thread,31);
    creation_md_note(2,0,0,registry_md_next_order(),6,creation_selected.handle,*(uint64_t*)(object+8),1,0,ui_thread,31);
    CHECK(creation_selected.state==4&&creation_selected.a_closed&&!creation_failures);
}
static void original_nav(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,unsigned char e,unsigned char f) {
    CHECK(a==(uintptr_t)controller&&b==(uintptr_t)object&&c==(uintptr_t)fixed&&!d&&e==1&&!f);behavior();if(mode==4||mode==12)return;
    *(void**)controller=image+0x2c51158;*(void**)(controller+0x28)=object;*(unsigned*)(controller+0xc8)=2;
    uint64_t args[]={(uintptr_t)agent,(uintptr_t)object,0,0};unsigned n=enter(CREATION_AGENT,args,4);CHECK(n);
    *(void**)(agent+0xef0)=object;*(double*)(agent+0xac0)=100.75;creation_leave_context(n,(uintptr_t)agent,1,tls);
    *(void**)(controller+0x40)=agent;*(void**)(controller+0x30)=agent+0xad0;SetLastError(0x56781234);
}
static void call_b(void) {
    scope_kind=2;creation_navigation_execute(original_nav,(uintptr_t)(image+0xea4b5e),(uintptr_t)controller,(uintptr_t)object,(uintptr_t)fixed,0,1,0);
}
static void run_scope(unsigned which,unsigned action) {
    reset();if(which==2){call_a();prepare_b();calls=0;}mode=action;unsigned caught=0;
    __try {if(which==1)call_a();else call_b();}
    __except(GetExceptionCode()==0xe0427777?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=1;}
    CHECK(caught==(action==1||action==3||action==7||action==13));CHECK(calls==1);creation_rng_record *r=&creation_rng_records[which-1];
    CHECK(r->scope==which&&r->admitted&&r->closed&&r->original==1&&!creation_rng_active);
    if(action==4||action==5||action==7){CHECK(!r->restored&&(r->errors&CRNG_RESTORE)&&creation_rng_restore_failures==1);}
    else {CHECK(r->restored&&*(uint64_t*)(tls+0x318)==caller_word);}
    if(action)CHECK(r->errors&&creation_rng_failures);else CHECK(!r->errors&&!creation_rng_failures&&r->normal);
    if(caught)CHECK((r->errors&CRNG_UNWIND)&&creation_records[r->creation-1].unwound&&!creation_current&&!creation_open);
    else CHECK(GetLastError()==0x56781234);
    CHECK(*(uint64_t*)(replacement+0x318)==0x7777777777777777ULL);tls_slots[0]=tls;
}
static void ownership_failures(void) {
    /* The real wrapper tests above use the real TLS selector. Destructive
     * memory tests use the same production begin/end with an injected reader,
     * leaving compiler TLS live while our separate borrowed block disappears. */
    for(unsigned which=0;which<6;which++) {
        reset();uint64_t args[]={0xabc,(uintptr_t)&object_ref,0xdef,0,1,2,3,4,1,0};
        unsigned index=creation_enter_context(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,10,tls),scope=0;
        CHECK(index);test_tls=tls;creation_rng_begin_using(index,CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,&scope,read_test_tls);
        CHECK(scope==1&&creation_rng_records[0].admitted);tls_slots[0]=old_tls;
        DWORD old;
        if(which==0)test_tls=replacement;
        if(which==1)CHECK(VirtualProtect(tls,4096,PAGE_NOACCESS,&old));
        if(which==2)CHECK(VirtualProtect(tls,4096,PAGE_READONLY,&old));
        if(which==3)++*(uint64_t*)(tls+0x300);
        if(which==4)tls[0x314]=0;
        if(which==5) {test_tls=tls+0x400;*(double*)(tls+0x700)=100;tls[0x714]=1;*(uint64_t*)(tls+0x718)=0x7777777777777777ULL;}
        creation_rng_end_using(scope,0,read_test_tls);
        CHECK(creation_rng_records[0].closed&&!creation_rng_records[0].restored&&creation_rng_restore_failures==1);
        CHECK(creation_rng_records[0].errors&(CRNG_RESTORE|CRNG_UNWIND));
        CHECK(*(uint64_t*)(replacement+0x318)==0x7777777777777777ULL);
        if(which==5)CHECK(*(uint64_t*)(tls+0x718)==0x7777777777777777ULL);
        CHECK(VirtualProtect(tls,4096,PAGE_READWRITE,&old));
        CHECK(*(uint64_t*)(tls+0x318)==creation_rng_keys[0]);tls_slots[0]=tls;
    }
}
static void refused_admissions(void) {
    for(unsigned which=0;which<13;which++) {
        reset();uint64_t args[]={0xabc,(uintptr_t)&object_ref,0xdef,0,1,2,3,4,1,0};
        unsigned index=creation_enter_context(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,10,tls),scope=0;
        CHECK(index);test_tls=tls;uintptr_t caller=(uintptr_t)(image+0xba1b8b);
        if(which==0)++caller;if(which==1)++stats.issued;if(which==2)++creation_selected.handle;
        if(which==3)creation_current=0;if(which==4)creation_rng_active=2;if(which==5)++ship_failures;
        if(which==6)boot_options^=1ULL<<48;if(which==7)flight_seeded=0;if(which==8)test_tls=NULL;
        if(which==9)++*(uint64_t*)(tls+0x318);if(which==10)args[3]=1;if(which==11)creation_owned^=1;
        if(which==12)image[creation_pins[0].rva+creation_pins[0].length-1]^=1;
        uint64_t before=*(uint64_t*)(tls+0x318);
        creation_rng_begin_using(index,CREATION_GENERATE,caller,args,&scope,read_test_tls);
        if(scope!=1||creation_rng_records[0].admitted||!creation_rng_records[0].errors)fprintf(stderr,"refusal case %u admitted %u error %x scope %u\n",which,creation_rng_records[0].admitted,creation_rng_records[0].errors,scope);
        CHECK(scope==1&&!creation_rng_records[0].admitted&&creation_rng_records[0].errors&&*(uint64_t*)(tls+0x318)==before);
        creation_rng_end_using(scope,0,read_test_tls);CHECK(creation_rng_records[0].closed);
        if(which==11)creation_owned^=1;if(which==12)image[creation_pins[0].rva+creation_pins[0].length-1]^=1;
    }
    reset();call_a();uint64_t before=*(uint64_t*)(tls+0x318);unsigned scope=99;uint64_t args[10]={0};
    creation_rng_begin_using(1,CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,&scope,read_test_tls);
    CHECK(scope==0&&creation_rng_count==1&&creation_rng_failures&&*(uint64_t*)(tls+0x318)==before);
    for(unsigned which=0;which<9;which++) {
        reset();call_a();prepare_b();uint64_t nav[]={(uintptr_t)controller,(uintptr_t)object,(uintptr_t)fixed,0,1,0};
        unsigned index=enter(CREATION_NAVIGATION,nav,6),slot=0;CHECK(index);test_tls=tls;
        if(which==0)nav[2]=0;if(which==1)*(void**)(object+0x200)=controller;if(which==2)*(uint64_t*)(object+8)^=1;
        if(which==3)*(void**)(object+0x348)=NULL;if(which==4)owner[0x10]='x';if(which==5)creation_selected.true_owner=0;
        if(which==6)creation_rng_records[0].restored=0;if(which==7)creation_selected.agent_count=1;
        if(which==8)creation_records[index-1].before.identity.first.controller=0;
        creation_rng_begin_using(index,CREATION_NAVIGATION,(uintptr_t)(image+0xea4b5e),nav,&slot,read_test_tls);
        CHECK(slot==2&&!creation_rng_records[1].admitted&&creation_rng_records[1].errors&&*(uint64_t*)(tls+0x318)==caller_word);
        creation_rng_end_using(slot,0,read_test_tls);
    }
}
static void manifest_tests(void) {
    uint32_t epoch=0;CHECK(creation_rng_parse(manifest,sizeof(manifest)-1,&epoch)&&epoch==1);
    CHECK(!creation_rng_parse(manifest,sizeof(manifest),&epoch));CHECK(!creation_rng_parse(NULL,0,&epoch));
    const char *bad[]={"0","01","-1","+1","4294967296","1 ","1;epoch=1","1\n","1.0"};char text[512];
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {snprintf(text,sizeof(text),"%s%s%s",CREATION_RNG_PREFIX,bad[i],CREATION_RNG_SUFFIX);CHECK(!creation_rng_parse(text,strlen(text),&epoch));}
    for(unsigned i=sizeof(CREATION_RNG_PREFIX);i<sizeof(manifest)-1;i++) {strcpy(text,manifest);text[i]^=1;CHECK(!creation_rng_parse(text,strlen(text),&epoch));}
    CHECK(creation_rng_key(0x43524541545f4131ULL,2,5,0x500000003ULL,3,276944566,CREATION_RNG_MACRO,"player",CREATION_RNG_SNAPSHOT,CREATION_RNG_PLAN)==0xc647f830596c5aeeULL);
    CHECK(creation_rng_key(0x43524541545f4231ULL,2,5,0x500000003ULL,3,276944566,CREATION_RNG_MACRO,"player",CREATION_RNG_SNAPSHOT,CREATION_RNG_PLAN)==0x7fffcd27bc36f0c6ULL);
    CHECK(creation_rng_key(0x43524541545f4131ULL,1,5,0x500000004ULL,4,276944566,CREATION_RNG_MACRO,"player",CREATION_RNG_SNAPSHOT,CREATION_RNG_PLAN)==0x1fa22405fef708c3ULL);
    CHECK(creation_rng_key(0x43524541545f4231ULL,1,6,0x600000003ULL,3,276944566,CREATION_RNG_MACRO,"player",CREATION_RNG_SNAPSHOT,CREATION_RNG_PLAN)==0x0e9071a83adfc469ULL);
}
static void stop_linearization(void) {
    reset();uint64_t args[]={0xabc,(uintptr_t)&object_ref,0xdef,0,1,2,3,4,1,0};
    unsigned index=creation_enter_context(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,10,tls),scope=0;
    CHECK(index);test_tls=tls;stop_reads=0;
    registry_md_installed=world_identity_installed=0;
    stop_entered=CreateEventW(NULL,TRUE,FALSE,NULL);stop_finished=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(stop_entered&&stop_finished);
    /* Queue the stop behind admission's actual lock. The injected TLS reader
     * confirms stop cannot close between its admission check and first write. */
    HANDLE thread=CreateThread(NULL,0,concurrent_stop,NULL,CREATE_SUSPENDED,NULL);CHECK(thread);
    /* Start it from the first read, which is inside the production lock. */
    /* A helper resumes the suspended stopper only once the lock is held. */
    test_stop_thread=thread;
    creation_rng_begin_using(index,CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,&scope,read_with_pending_stop);
    CHECK(scope==1&&creation_rng_records[0].admitted&&WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CHECK(creation_rng_records[0].stop_overlap&&*(uint64_t*)(tls+0x318)==creation_rng_keys[0]);
    creation_rng_end_using(scope,0,read_test_tls);CHECK(creation_rng_records[0].restored&&*(uint64_t*)(tls+0x318)==caller_word);
    CloseHandle(thread);CloseHandle(stop_entered);CloseHandle(stop_finished);
}
static void configuration_guards(void) {
    for(unsigned bit=0;bit<53;bit++) {
        reset();boot_options^=1ULL<<bit;
        CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
        CHECK(!creation_rng_count&&*(uint64_t*)(tls+0x318)==caller_word);
    }
    for(unsigned which=0;which<10;which++) {
        reset();
        if(which==0)world_wait_started=1;if(which==1)world_wait_rng_started=1;if(which==2)world_find_started=1;
        if(which==3)world_action_started=1;if(which==4)world_values_started=1;if(which==5)player_fire_enabled=1;
        if(which==6)player_weapon_seeded=1;if(which==7)transition_started=1;if(which==8)origin_started=1;if(which==9)getup_ledger.configured=1;
        CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
        world_wait_started=world_wait_rng_started=world_find_started=world_action_started=world_values_started=0;
        player_fire_enabled=player_weapon_seeded=transition_started=origin_started=getup_ledger.configured=0;
    }
    reset();rotation_configured=1;CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));rotation_configured=0;
    reset();creation_rng_stop();CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
}
static unsigned pushed_nil;
static const char *invalid_lua_manifest(lua_State *state,int index,size_t *length) {
    (void)state;const char *text=index==1?"invalid":preimage;*length=strlen(text);return text;
}
static void record_nil(lua_State *state) {(void)state;++pushed_nil;}
static void lua_refusal_stops(void) {
    reset();registry_md_installed=world_identity_installed=0;
    p_lua_tolstring=invalid_lua_manifest;p_lua_pushnil=record_nil;
    CHECK(lua_creation_rng_config(NULL)==1&&pushed_nil==1&&!stats.active&&creation_rng_closing&&!creation_owned);
    CHECK(stats.reason==75&&!creation_rng_count&&*(uint64_t*)(tls+0x318)==caller_word);
}
static char ack_pushes[2][768];static unsigned ack_push_count;
static const char *ack_lua_input(lua_State *state,int index,size_t *length) {
    (void)state;const char *text=index==1?manifest:preimage;*length=strlen(text);return text;
}
static void ack_lua_push(lua_State *state,const char *text,size_t length) {
    (void)state;CHECK(ack_push_count<2&&length<sizeof(ack_pushes[0]));
    memcpy(ack_pushes[ack_push_count],text,length);ack_pushes[ack_push_count++][length]=0;
}
static void acknowledgements(void) {
    reset_before_activity();char first[384],next[384],footer[384];
    CHECK(creation_rng_ack(first,sizeof(first)));CHECK(strstr(first,"ordinal=1 ")&&valid_hex(creation_rng_boot,32));
    uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));char boot[33];strcpy(boot,creation_rng_boot);
    p_lua_tolstring=ack_lua_input;p_lua_pushlstring=ack_lua_push;ack_push_count=0;
    CHECK(lua_creation_rng_config(NULL)==2&&ack_push_count==2&&creation_rng_acks==2);
    CHECK(!memcmp(keys,creation_rng_keys,sizeof(keys))&&!strcmp(boot,creation_rng_boot));
    CHECK(creation_rng_ack(next,sizeof(next))&&!strcmp(next,ack_pushes[1]));
    CHECK(creation_rng_ack_status(footer,sizeof(footer)));
    /* Exact production C -> Lua return strings for the modelled reload test. */
    printf("ACK_TEST_CONFIG %s\nACK_TEST_FIRST %s\nACK_TEST_SECOND %s\nACK_TEST_STOP %s\n",
        ack_pushes[0],first,next,footer);
    for(unsigned i=0;i<19;i++) {
        reset_before_activity();unsigned before=creation_rng_acks;
        switch(i) {
        case 0:boot_phase=1;break;case 1:stats.issued=1;break;case 2:creation_selected.state=1;break;
        case 3:creation_count=1;break;case 4:creation_open=1;break;case 5:creation_rng_count=1;break;
        case 6:creation_rng_active=1;break;case 7:creation_rng_attempted=1;break;case 8:creation_rng_failures=1;break;
        case 9:creation_rng_closing=1;break;case 10:stats.active=0;break;case 11:boot_started++;break;
        case 12:remote_port++;break;case 13:stats.slot=2;break;case 14:secret[0]^=1;break;
        case 15:initial_state_digest[0]^=1;break;case 16:used=0;break;
        case 17:creation_rng_manifest[0]^=1;break;case 18:creation_rng_epoch++;break;
        }
        CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
        CHECK(creation_rng_acks==before);
    }
    reset_before_activity();
    CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-sizeof(CREATION_RNG_ACK_SUFFIX)));
    char bad_preimage[sizeof(preimage)];memcpy(bad_preimage,preimage,sizeof(preimage));
    for(size_t i=sizeof(preimage)-sizeof(CREATION_RNG_ACK_SUFFIX);i<sizeof(preimage)-1;i++) {
        bad_preimage[i]^=1;
        CHECK(!creation_rng_preimage(bad_preimage,sizeof(preimage)-1,manifest,sizeof(manifest)-1));
        bad_preimage[i]^=1;
    }
    for(unsigned i=2;i<=CREATION_RNG_ACK_LIMIT;i++)CHECK(creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1)&&creation_rng_acks==i);
    CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1)&&creation_rng_acks==CREATION_RNG_ACK_LIMIT);
    reset();CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
}
static int run_tests(int argc,char **argv) {
    CHECK(argc==2);load_image(argv[1]);tls=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    replacement=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(tls&&replacement);
    *(uint64_t*)(replacement+0x318)=0x7777777777777777ULL;tls_slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);CHECK(tls_slots);old_tls=tls_slots[0];
    for(unsigned which=1;which<=2;which++)for(unsigned action=0;action<=13;action++)if(action!=10&&action!=11)run_scope(which,action);
    manifest_tests();ownership_failures();refused_admissions();stop_linearization();configuration_guards();lua_refusal_stops();acknowledgements();
    reset();call_a();prepare_b();call_b();CHECK(calls==2&&!creation_rng_failures);creation_rng_stop();CHECK(!creation_rng_failures&&creation_rng_closing);
    char row[1536],status[1024];CHECK(creation_rng_status(status,sizeof(status)));for(unsigned s=1;s<=2;s++)for(unsigned p=0;p<2;p++)CHECK(creation_rng_row(s,p,row,sizeof(row)));
    reset();creation_rng_started=0;mode=10;call_a();CHECK(calls==1&&!creation_rng_count&&*(uint64_t*)(tls+0x318)==(caller_word^0x123456789abcdef0ULL));
    reset();creation_rng_stop();mode=11;call_a();CHECK(calls==1&&!creation_rng_count&&creation_rng_failures);
    reset_before_activity();uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));CHECK(creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
    CHECK(!memcmp(keys,creation_rng_keys,sizeof(keys)));initial_state_digest[0]^=1;CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,preimage,sizeof(preimage)-1));
    memset(&creation_rng_records[0],0xff,sizeof(creation_rng_records[0]));creation_rng_closing=1;
    for(unsigned part=0;part<2;part++){int length=creation_rng_row(1,part,row,sizeof(row));CHECK(length>0&&length<1400);}
    tls_slots[0]=old_tls;printf("Construction RNG production executors/pinned image/ABI/original once/SEH/STOP/TLS refusal: %u checks PASS\n",checks);return 0;
}
/* Real X4 places its native TLS at slot0 and the loaded gate's compiler TLS at
 * a distinct loader slot. Keep that separation in this synthetic harness. */
extern DWORD _tls_index;
__declspec(dllexport) int creation_rng_test(const char *path) {
    CHECK(_tls_index!=0);char *argv[]={"creation_rng_test",(char*)path};return run_tests(2,argv);
}
