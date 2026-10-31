/* Production observer over an independent pinned image and owned synthetic
 * object memory. No X4 process, constructor, RNG or event queue is executed. */
static void test_creation_reset(void) {
    assert(!creation_owned);memset(creation_records,0,sizeof(creation_records));memset(&creation_selected,0,sizeof(creation_selected));
    creation_started=creation_enabled=creation_closing=creation_window=0;creation_count=creation_current=0;
    creation_failures=creation_dropped=creation_open=creation_peak=creation_restore_open=creation_restore_failures=0;creation_first_error=0;
    memset(creation_original_words,0,sizeof(creation_original_words));memset(creation_patched_words,0,sizeof(creation_patched_words));
}
static void test_creation_start(void) {
    test_creation_reset();stats.active=1;stats.issued=0;boot_mode=1;boot_phase=0;ui_thread=GetCurrentThreadId();
    registry_md_installed=registry_origin_enabled=world_identity_installed=1;
    assert(creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1));
    assert(creation_hooks_owned() && creation_started && creation_enabled);
    assert(creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1));
    memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;registry_failures=0;
    for(unsigned actor=ACTOR_DYNAMIC_FIRST;actor<=ACTOR_CAPACITY;actor++) {actor_components[actor-1]=NULL;actor_ids[actor-1]=0;}
    const char *macro="ship_arg_xl_builder_01_a_macro";
    assert(registry_reserve(0x0000000500000003ULL,macro,strlen(macro),"player",6,5)==ACTOR_DYNAMIC_FIRST);
    stats.issued=6;boot_phase=3; /* queued tick-5 Setup executes in native tick 6 */
}
static void test_creation_arm(void) {
    creation_md_note(1,1,1,registry_md_next_order(),stats.issued,0x0000000500000003ULL,1,1,0,ui_thread,31);
    assert(creation_selected.state==1 && !creation_failures);
    assert(creation_selected.reserved_tick==5 && creation_selected.execution_tick==stats.issued);
}
static void test_creation_reservation_admission(void) {
    for(unsigned bit=47;bit<=49;bit++) {
        test_creation_start();uint64_t options=boot_options;boot_options|=1ULL<<bit;
        assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1));
        creation_restore();test_creation_reset();stats.issued=0;boot_phase=0;
        assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1) && !creation_started && !creation_owned);
        boot_options=options;
    }
    /* Exercise the production MD append path: admission consumes event.tick,
     * not a fresh issued-tick substitution in the forwarding call. */
    for(unsigned tick=5;tick<=6;tick++) {
        test_creation_start();stats.issued=tick;
        registry_md_event event={.tick=tick,.handle=0x0000000500000003ULL,.kind=1,.phase=1,.edge=1,
            .payload=1,.pending=1,.thread=ui_thread,.reads=31};
        registry_md_append(&event);assert(creation_window && !creation_failures && creation_selected.execution_tick==tick);
        creation_md_note(1,1,1,registry_md_next_order(),tick,event.handle,1,1,0,ui_thread,31);
        assert(!creation_window && creation_first_error==CREATION_E_TOKEN && creation_selected.execution_tick==tick);
        creation_restore();
    }
    test_creation_start();
    registry_md_event stale_event={.tick=5,.handle=0x0000000500000003ULL,.kind=1,.phase=1,.edge=1,
        .payload=1,.pending=1,.thread=ui_thread,.reads=31};
    registry_md_append(&stale_event);
    assert(!creation_window && creation_first_error==CREATION_E_TOKEN);creation_restore();
    for(unsigned mutation=0;mutation<19;mutation++) {
        test_creation_start();uint64_t tick=6;unsigned thread=ui_thread,pending=1,error=0,reads=31;
        switch(mutation) {
            case 0:dynamic_ship_count=0;break;
            case 1:++dynamic_ships[0].handle;break;
            case 2:dynamic_ships[0].reserved_tick=6;break;
            case 3:dynamic_ships[0].macro[0]='x';break;
            case 4:dynamic_ships[0].owner[0]='x';break;
            case 5:dynamic_ships[0].bound_tick=5;break;
            case 6:dynamic_ships[0].prepared_tick=5;break;
            case 7:actor_ids[ACTOR_DYNAMIC_FIRST-1]=1;break;
            case 8:actor_components[ACTOR_DYNAMIC_FIRST-1]=(void*)1;break;
            case 9:dynamic_ships[1]=dynamic_ships[0];++dynamic_ships[1].handle;dynamic_ship_count=2;break;
            case 10:tick=stats.issued=4;break;
            case 11:tick=stats.issued=7;break;
            case 12:stats.issued=5;break;
            case 13:++thread;break;
            case 14:pending=2;break;
            case 15:error=1;break;
            case 16:reads=30;break;
            case 17:creation_error_locked(CREATION_E_READ);break;
            case 18:registry_failures=1;break;
        }
        creation_md_note(1,1,1,registry_md_next_order(),tick,0x0000000500000003ULL,1,pending,error,thread,reads);
        assert(!creation_window && !creation_selected.state && creation_failures);
        /* Restoring a valid entry/clock cannot erase failed admission. */
        memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;registry_failures=0;stats.issued=6;
        actor_ids[ACTOR_DYNAMIC_FIRST-1]=0;actor_components[ACTOR_DYNAMIC_FIRST-1]=NULL;
        const char *macro="ship_arg_xl_builder_01_a_macro";
        assert(registry_reserve(0x0000000500000003ULL,macro,strlen(macro),"player",6,5)==ACTOR_DYNAMIC_FIRST);
        creation_md_note(1,1,1,registry_md_next_order(),6,0x0000000500000003ULL,1,1,0,ui_thread,31);
        assert(!creation_window && !creation_selected.state);creation_restore();
    }
    test_creation_start();creation_restore();
    creation_md_note(1,1,1,registry_md_next_order(),6,0x0000000500000003ULL,1,1,0,ui_thread,31);
    assert(!creation_window && !creation_selected.state && !creation_enabled);
    for(unsigned phase=2;phase<=3;phase++) {
        test_creation_start();test_creation_arm();stats.issued=7;
        if(phase==2) {
            creation_selected.state=3;creation_selected.generate_count=creation_selected.initialize_count=creation_selected.pilot_count=1;
            creation_selected.navigation_count=creation_selected.agent_count=creation_selected.setter_count=1;
            creation_md_note(1,2,0,registry_md_next_order(),7,creation_selected.handle,2,1,0,ui_thread,31);
        } else {creation_window=0;creation_selected.state=4;creation_selected.runtime=123;
            creation_md_note(2,0,0,registry_md_next_order(),7,creation_selected.handle,123,1,0,ui_thread,31);}
        assert(creation_failures);creation_restore();
    }
}
static unsigned test_creation_enter(unsigned kind,uint64_t *args,unsigned count,void *tls) {
    unsigned caller=kind==CREATION_RANDOM?0xe85531:creation_patch_contracts[kind-1].expected_caller;
    return creation_enter_context(kind,(uintptr_t)(image+caller),args,count,tls);
}
typedef struct {uint64_t args[6];void *tls;unsigned kind,index,error,thread;} test_creation_worker_args;
static DWORD WINAPI test_creation_worker(LPVOID argument) {
    test_creation_worker_args *a=argument;a->thread=GetCurrentThreadId();
    a->index=test_creation_enter(a->kind,a->args,a->kind==CREATION_AGENT?4:6,a->tls);
    if(a->index){creation_leave_context(a->index,0,0,a->tls);a->error=creation_records[a->index-1].error;}
    return 0;
}
static unsigned test_creation_fail_index,test_creation_fail_after;
static int test_creation_partial_swap(unsigned index,const uint64_t *expected,const uint64_t *desired,int *changed) {
    if(index==test_creation_fail_index && !test_creation_fail_after){*changed=0;return 0;}
    int result=creation_swap(index,expected,desired,changed);
    return index==test_creation_fail_index?0:result;
}
static unsigned test_creation_api_failure,test_creation_protect_calls;
static BOOL WINAPI test_creation_protect(LPVOID address,SIZE_T size,DWORD protection,PDWORD prior) {
    ++test_creation_protect_calls;
    if(test_creation_api_failure==1 && test_creation_protect_calls==2)return FALSE;
    return VirtualProtect(address,size,protection,prior);
}
static BOOL WINAPI test_creation_flush(HANDLE process,LPCVOID address,SIZE_T size) {
    return test_creation_api_failure==2?FALSE:FlushInstructionCache(process,address,size);
}
static int test_creation_failed_restore(unsigned index,const uint64_t *expected,const uint64_t *desired,int *changed) {
    if(index!=3)return creation_swap(index,expected,desired,changed);
    test_creation_protect_calls=0;
    return creation_swap_using(index,expected,desired,changed,test_creation_protect,test_creation_flush);
}
static void *test_creation_tls_value;
static volatile LONG test_creation_tls_calls;
static void *test_creation_read_tls(void) {InterlockedIncrement(&test_creation_tls_calls);return test_creation_tls_value;}
static unsigned test_creation_stop_read;
static void *test_creation_callback_stop_tls(void) {
    if((unsigned)InterlockedIncrement(&test_creation_tls_calls)==test_creation_stop_read)creation_restore();
    return test_creation_tls_value;
}
static DWORD WINAPI test_creation_foreign_callback(LPVOID unused) {
    (void)unused;creation_bind_note_using(creation_selected.handle,creation_selected.runtime,test_creation_read_tls);return 0;
}
static DWORD WINAPI test_creation_outside_window(LPVOID unused) {
    (void)unused;uint64_t args[10]={0};
    assert(!creation_enter_with_tls_reader(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),args,10,test_creation_read_tls));
    creation_leave_with_tls_reader(0,0,0,test_creation_read_tls);return 0;
}
static void test_creation_native_image(void) {
    registry_md_event saved_md_events[REGISTRY_MD_CAPACITY];memcpy(saved_md_events,registry_md_events,sizeof(saved_md_events));
    unsigned saved_md_count=registry_md_count;LONG64 saved_md_order=registry_md_order,saved_md_failures=registry_md_failures;
    dynamic_ship saved_dynamic[ACTOR_DYNAMIC_COUNT];memcpy(saved_dynamic,dynamic_ships,sizeof(saved_dynamic));
    unsigned saved_dynamic_count=dynamic_ship_count;LONG64 saved_registry_failures=registry_failures;LONG saved_observing=registry_observing;
    void *saved_components[ACTOR_DYNAMIC_COUNT];uint64_t saved_ids[ACTOR_DYNAMIC_COUNT];
    memcpy(saved_components,actor_components+ACTOR_DYNAMIC_FIRST-1,sizeof(saved_components));
    memcpy(saved_ids,actor_ids+ACTOR_DYNAMIC_FIRST-1,sizeof(saved_ids));
    unsigned char *prior=thunk;thunk=make_thunk();assert(thunk);unsigned char frozen[4096];memcpy(frozen,thunk,4096);
    assert(creation_pins_valid());
    for(unsigned i=0;i<sizeof(creation_pins)/sizeof(*creation_pins);i++) {
        unsigned last=creation_pins[i].rva+creation_pins[i].length-1;image[last]^=1;
        assert(!creation_pins_valid());image[last]^=1;assert(creation_pins_valid());
    }
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++) {
        const creation_patch_contract *p=&creation_patch_contracts[i];assert(!(p->word%(p->width)));
        if(p->call_offset){int32_t rel;memcpy(&rel,image+p->word+p->call_offset+1,4);
            assert(image[p->word+p->call_offset]==0xe8 && (int64_t)p->word+p->call_offset+5+rel==p->entry);}
    }
    for(unsigned i=0;i<sizeof(creation_ancillary_callers)/sizeof(*creation_ancillary_callers);i++) {
        const creation_caller_pin *p=&creation_ancillary_callers[i];
        unsigned kind=creation_ancillary_caller(CREATION_SETTER,p->caller)?CREATION_SETTER:CREATION_RANDOM;
        assert(creation_ancillary_caller(kind,p->caller));image[p->site]^=1;
        assert(!creation_ancillary_caller(kind,p->caller));image[p->site]^=1;
    }
    assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1) && !creation_started);
    stats.active=boot_mode=registry_md_installed=registry_origin_enabled=world_identity_installed=1;ui_thread=GetCurrentThreadId();
    assert(!creation_configure("invalid",7) && !creation_started);
    boot_phase=3;assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1) && !creation_started);boot_phase=0;
    registry_origin_enabled=0;assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1));registry_origin_enabled=1;
    assert(thunk_unwind_prepare(3456));test_tu_unwind_offsets(4);test_tu_executable_abi(4);
    for(unsigned after=0;after<2;after++)for(unsigned index=0;index<CREATION_HOOK_COUNT;index++) {
        test_creation_reset();test_creation_fail_index=index;test_creation_fail_after=after;
        assert(!creation_configure_using(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1,test_creation_partial_swap));
        assert(!creation_owned && !creation_enabled && creation_closing && creation_failures && creation_pins_valid());
        assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1) && !memcmp(frozen,thunk,4096));
    }
    test_creation_reset();
    test_creation_reservation_admission();test_creation_reset();
    unsigned char object[0x400]={0},macro[0x40]={0},owner[0x40]={0},controller[0xe0]={0},agent[0xf20]={0},tls[0x320]={0},static_controller[0x60]={0};
    const char *name="ship_arg_xl_builder_01_a_macro";void *object_ref=object,*local_object_ref=object;
    uint64_t saved_macro_getter=*(uint64_t*)(image+0x2ba5430+18*8),saved_owner_getter=*(uint64_t*)(image+0x2b99a88+0x1620);
    unsigned saved_read=*(unsigned*)(image+0x38f4ee4),saved_write=*(unsigned*)(image+0x2f4b6f4);
    *(void**)object=image+0x2b99a88;*(uint64_t*)(object+8)=0x987654321ULL;*(void**)(object+0x60)=macro;
    *(void**)macro=image+0x2ba5430;*(const char**)(macro+0x20)=name;*(uint64_t*)(macro+0x30)=strlen(name);*(uint64_t*)(macro+0x38)=31;
    *(void**)(image+0x2ba5430+18*8)=image+0xefb10;*(void**)(image+0x2b99a88+0x1620)=image+0x352b10;
    memcpy(owner+0x10,"player",7);*(uint64_t*)(owner+0x20)=6;*(uint64_t*)(owner+0x28)=15;
    *(double*)(tls+0x300)=100.0;tls[0x314]=1;*(uint64_t*)(tls+0x318)=1001;
    *(unsigned*)(image+0x38f4ee4)=1;*(unsigned*)(image+0x2f4b6f4)=0;
    uint64_t generate[10]={0,(uintptr_t)&object_ref},initialize[9]={0,(uintptr_t)&local_object_ref},pilot[7]={0,0,(uintptr_t)&local_object_ref};
    assert(generate[1]!=initialize[1] && pilot[2]==initialize[1]);
    *(void**)static_controller=image+0x2a34b38;
    uint64_t nav[6]={(uintptr_t)controller,(uintptr_t)object,(uintptr_t)static_controller,0,1,0},make_agent[4]={(uintptr_t)agent,(uintptr_t)object};
    uint64_t setter[3]={(uintptr_t)object,(uintptr_t)controller,1},random[2]={(uintptr_t)(tls+0x318),0x3f000000};
    /* A generic nested setter is retained but cannot acquire selected identity.
     * The same address as the eventual selected object is not an exclusion. */
    test_creation_start();test_creation_arm();
    unsigned outer=test_creation_enter(CREATION_GENERATE,generate,10,tls);
    uint64_t child_setter[3]={(uintptr_t)macro,0,1};
    unsigned child=creation_enter_context(CREATION_SETTER,(uintptr_t)(image+0x7458c7),child_setter,3,tls);
    creation_leave_context(child,0,0,tls);
    assert(creation_records[child-1].subject==3 && !creation_records[child-1].error && creation_selected.unresolved_count==1);
    unsigned selected_init=test_creation_enter(CREATION_INITIALIZE,initialize,9,tls);
    child=creation_enter_context(CREATION_SETTER,(uintptr_t)(image+0x51b71e),child_setter,3,tls);
    creation_leave_context(child,0,0,tls);
    assert(creation_records[child-1].subject==2 && !creation_records[child-1].error && creation_selected.excluded_count==1 && !creation_selected.setter_count);
    *(void**)(object+0x348)=owner;unsigned child_pilot=test_creation_enter(CREATION_PILOT,pilot,7,tls);
    uint64_t private_rng=99,other_rng_args[]={(uintptr_t)&private_rng,0x3f800000};
    unsigned incidental=creation_enter_context(CREATION_RANDOM,(uintptr_t)(image+0x14ddd3a),other_rng_args,2,tls);
    private_rng=100;creation_leave_context(incidental,0x3f000000,2,tls);
    assert(!creation_records[incidental-1].error && creation_records[incidental-1].subject==4 &&
        creation_records[incidental-1].rng_before==99 && creation_records[incidental-1].rng_after==100 && creation_records[incidental-1].aux_reads==12);
    creation_leave_context(child_pilot,0,1,tls);creation_leave_context(selected_init,0,0,tls);creation_leave_context(outer,(uintptr_t)&object_ref,1,tls);creation_restore();
    *(void**)(object+0x348)=NULL;
    test_creation_start();assert(!creation_enter_context(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),generate,10,tls));
    test_creation_tls_value=tls;test_creation_tls_calls=0;
    /* Holding the actual production lock makes this a bounded no-lock proof. */
    AcquireSRWLockExclusive(&creation_lock);
    HANDLE outside=CreateThread(NULL,0,test_creation_outside_window,NULL,0,NULL);assert(outside);
    DWORD outside_wait=WaitForSingleObject(outside,2000);ReleaseSRWLockExclusive(&creation_lock);
    assert(outside_wait==WAIT_OBJECT_0 && !test_creation_tls_calls);CloseHandle(outside);
    test_creation_arm();unsigned g=test_creation_enter(CREATION_GENERATE,generate,10,tls);assert(g==1 && !creation_failures);
    unsigned init=test_creation_enter(CREATION_INITIALIZE,initialize,9,tls);assert(init==2 && !creation_failures);
    assert(creation_records[init-1].before.identity.first.owner_state==PRESENCE_ABSENT);
    *(void**)(object+0x348)=owner;unsigned pilot_index=test_creation_enter(CREATION_PILOT,pilot,7,tls);assert(pilot_index==3);
    creation_leave_context(pilot_index,0,1,tls);assert(!creation_failures);
    creation_leave_context(init,0,0,tls);creation_leave_context(g,(uintptr_t)&object_ref,1,tls);
    uint64_t static_setter[3]={(uintptr_t)object,(uintptr_t)static_controller,1};
    unsigned static_index=test_creation_enter(CREATION_SETTER,static_setter,3,tls);*(void**)(object+0x200)=static_controller;
    creation_leave_context(static_index,0,0,tls);assert(!creation_failures && creation_selected.prior_static==(uintptr_t)static_controller);
    creation_md_note(1,2,0,registry_md_next_order(),stats.issued,creation_selected.handle,2,1,0,ui_thread,31);
    assert(creation_window && creation_selected.a_closed && !creation_selected.a_error);
    creation_md_note(2,0,0,registry_md_next_order(),stats.issued,creation_selected.handle,*(uint64_t*)(object+8),1,0,ui_thread,31);
    unsigned nav_index=test_creation_enter(CREATION_NAVIGATION,nav,6,tls);assert(nav_index==5 && !creation_failures);
    assert(creation_records[nav_index-1].before.direct_reads==1); /* Pre-vtable is retained, never readiness. */
    *(void**)controller=image+0x2c51158;*(void**)(controller+0x28)=object;*(unsigned*)(controller+0xc8)=2;
    unsigned agent_index=test_creation_enter(CREATION_AGENT,make_agent,4,tls);unsigned rng_index=test_creation_enter(CREATION_RANDOM,random,2,tls);
    *(uint64_t*)(tls+0x318)=1002;creation_leave_context(rng_index,0x3e800000,2,tls);
    *(void**)(agent+0xef0)=object;*(double*)(agent+0xac0)=100.75;memset(agent,0x31,832);memset(agent+0xad0,0x42,832);
    creation_leave_context(agent_index,(uintptr_t)agent,1,tls);assert(!creation_failures);
    *(void**)(controller+0x40)=agent;*(void**)(controller+0x30)=agent+0xad0;
    uint64_t query[5]={(uintptr_t)object,0x20f,0,0,0};unsigned query_index=test_creation_enter(CREATION_EVENT_QUERY,query,5,tls);
    creation_leave_context(query_index,0,3,tls);random[1]=0x40000000;
    rng_index=creation_enter_context(CREATION_RANDOM,(uintptr_t)(image+0xea90e1),random,2,tls);
    *(uint64_t*)(tls+0x318)=1003;creation_leave_context(rng_index,0x3f000000,2,tls);
    double due=108.5;uint64_t due_bits;memcpy(&due_bits,&due,8);uint64_t post[4]={(uintptr_t)object,0x20f,due_bits,0};
    unsigned post_index=test_creation_enter(CREATION_POST,post,4,tls);creation_leave_context(post_index,0,0,tls);
    creation_leave_context(nav_index,0,0,tls);assert(!creation_failures);
    unsigned set_index=test_creation_enter(CREATION_SETTER,setter,3,tls);*(void**)(object+0x200)=controller;
    creation_leave_context(set_index,0,0,tls);
    /* Reproduce a publication changing between the separately stable identity
     * and auxiliary samples. The production join must not accept either edge. */
    creation_record inconsistent=creation_records[set_index-1];
    inconsistent.before.identity=inconsistent.after.identity;
    *(void**)(object+0x200)=static_controller;
    assert(creation_aux_read(&inconsistent,0)&CREATION_E_IDENTITY);
    *(void**)(object+0x200)=controller;inconsistent=creation_records[set_index-1];
    inconsistent.after.identity=inconsistent.before.identity;
    assert(creation_aux_read(&inconsistent,1)&CREATION_E_IDENTITY);
    creation_token ready_callback=creation_selected;
    creation_token before_nav=ready_callback;before_nav.controller=before_nav.agent=0;
    before_nav.navigation_count=before_nav.agent_count=before_nav.setter_count=0;
    const char *bad_handles[]={"","000000050000000","000000050000000x","00000005000000030"};
    const char *bad_runtimes[]={"","-1","1.0","1ULL","18446744073709551616"};
    for(unsigned i=0;i<sizeof(bad_handles)/sizeof(*bad_handles);i++) {
        uint64_t parsed_handle=0,parsed_runtime=0;
        assert(!creation_bind_arguments(bad_handles[i],strlen(bad_handles[i]),"1",1,&parsed_handle,&parsed_runtime));
    }
    for(unsigned i=0;i<sizeof(bad_runtimes)/sizeof(*bad_runtimes);i++) {
        uint64_t parsed_handle=0,parsed_runtime=0;
        assert(!creation_bind_arguments("0000000500000003",16,bad_runtimes[i],strlen(bad_runtimes[i]),&parsed_handle,&parsed_runtime));
    }
    assert(creation_selected.state==4 && !creation_selected.b_closed && !creation_failures);
    creation_bind_note_using(creation_selected.handle+1,creation_selected.runtime,test_creation_read_tls);
    creation_bind_note_using(creation_selected.handle,creation_selected.runtime+1,test_creation_read_tls);
    assert(creation_selected.foreign_callbacks==2 && creation_selected.state==4 && !test_creation_tls_calls);
    creation_bind_note_using(creation_selected.handle,creation_selected.runtime,test_creation_read_tls);
    assert(!creation_failures && creation_selected.state==5 && !creation_open && creation_count==12);
    assert(!creation_window && creation_selected.b_closed && !creation_selected.b_error);
    test_creation_outside_window(NULL);assert(test_creation_tls_calls==2);
    assert(creation_records[5].after.timer==*(uint64_t*)(agent+0xac0) && creation_records[9].args[2]==due_bits);
    assert(creation_records[6].before.rng==1001 && creation_records[6].after.rng==1002 && creation_records[8].after.rng==1003);
    char row[1536];assert(creation_status(row,sizeof(row)) && strstr(row,"failures=0") && strstr(row,"sector_pose_supported=0"));
    for(unsigned i=0;i<creation_count;i++)for(unsigned part=0;part<CREATION_PARTS;part++) {
        assert(creation_row(i,part,row,sizeof(row)) && strstr(row,"complete=1"));assert(!creation_row(i,part,row,16));
    }
    assert(!creation_row(creation_count,0,row,sizeof(row)) && !creation_row(0,CREATION_PARTS,row,sizeof(row)));
    creation_restore();assert(!creation_owned && !creation_open && !creation_restore_open && !creation_enabled && !memcmp(frozen,thunk,4096));
    assert(!creation_configure(CREATION_MANIFEST,sizeof(CREATION_MANIFEST)-1));
    /* Actual production closure: stale/wrong-thread callbacks cannot consume
     * the selected deadline. STOP before admission and while the paired
     * passive sample is open both retain an invalid observation. */
    for(unsigned mutation=0;mutation<5;mutation++) {
        test_creation_start();creation_selected=ready_callback;creation_window=1;test_creation_tls_calls=0;
        if(mutation==0) {
            ++stats.issued;creation_bind_note_using(ready_callback.handle,ready_callback.runtime,test_creation_read_tls);
            assert(!creation_selected.b_closed && !test_creation_tls_calls && creation_selected.b_error==CREATION_E_TOKEN);
        } else if(mutation==1) {
            HANDLE worker=CreateThread(NULL,0,test_creation_foreign_callback,NULL,0,NULL);assert(worker && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);CloseHandle(worker);
            assert(!creation_selected.b_closed && !test_creation_tls_calls && creation_selected.b_error==CREATION_E_THREAD);
        } else if(mutation==2 || mutation==3) {
            test_creation_stop_read=mutation-1;
            creation_bind_note_using(ready_callback.handle,ready_callback.runtime,test_creation_callback_stop_tls);
            assert(creation_selected.b_error && !creation_window && !creation_open);
            assert(creation_restore_open==(mutation==3));
        } else {
            ++*(uint64_t*)(object+8);creation_bind_note_using(ready_callback.handle,ready_callback.runtime,test_creation_read_tls);
            --*(uint64_t*)(object+8);assert(creation_selected.b_closed && creation_selected.b_error);
        }
        creation_restore();assert(creation_failures && !creation_owned);
    }
    /* Non-null is not enough: exactly the captured prior Static must still be
     * published and supplied in R8. No mutation grants a candidate controller. */
    for(unsigned mutation=0;mutation<5;mutation++) {
        test_creation_start();creation_selected=before_nav;creation_window=1;
        *(void**)(object+0x200)=static_controller;uint64_t changed_nav[6];memcpy(changed_nav,nav,sizeof(nav));
        if(mutation==0)changed_nav[2]=0;
        if(mutation==1)changed_nav[2]=(uintptr_t)owner;
        if(mutation==2)*(void**)(object+0x200)=controller;
        if(mutation==3)*(void**)static_controller=image+0x2c51158;
        if(mutation==4)changed_nav[0]=(uintptr_t)static_controller;
        unsigned index=test_creation_enter(CREATION_NAVIGATION,changed_nav,6,tls);
        assert(index && (creation_records[index-1].error&CREATION_E_IDENTITY) && !creation_selected.controller);
        creation_leave_context(index,0,0,tls);creation_restore();*(void**)static_controller=image+0x2a34b38;
    }
    *(void**)(object+0x200)=controller;
    /* A native lifetime change is retained as a refusal; no lookup/rebind retry. */
    test_creation_start();test_creation_arm();g=test_creation_enter(CREATION_GENERATE,generate,10,tls);
    init=test_creation_enter(CREATION_INITIALIZE,initialize,9,tls);++*(uint64_t*)(object+8);
    creation_leave_context(init,0,0,tls);creation_leave_context(g,(uintptr_t)&object_ref,1,tls);
    assert(creation_failures && (creation_records[g-1].error&CREATION_E_RETURN));--*(uint64_t*)(object+8);creation_restore();
    /* An in-flight original owns its pair even if STOP restores the hook. */
    test_creation_start();test_creation_arm();test_creation_tls_calls=0;
    g=creation_enter_with_tls_reader(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),generate,10,test_creation_read_tls);assert(test_creation_tls_calls==1);
    assert(!creation_row(0,0,row,sizeof(row)));creation_restore();assert(creation_restore_open==1 && !creation_owned);
    assert(!creation_window && creation_row(0,0,row,sizeof(row)) && strstr(row,"complete=0"));
    creation_leave_with_tls_reader(g,(uintptr_t)&object_ref,1,test_creation_read_tls);assert(test_creation_tls_calls==2);
    test_creation_outside_window(NULL);assert(test_creation_tls_calls==2);
    assert(!creation_open && creation_restore_open==1 && creation_records[0].complete);
    /* Real exception unwind is distinct from a normal return after STOP. Both
     * nested resident records close once without consulting a dead outref/TLS. */
    for(unsigned stop=0;stop<2;stop++) {
        test_creation_start();test_creation_arm();test_creation_tls_calls=0;
        g=creation_enter_with_tls_reader(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),generate,10,test_creation_read_tls);
        init=creation_enter_with_tls_reader(CREATION_INITIALIZE,(uintptr_t)(image+0xc84531),initialize,9,test_creation_read_tls);
        assert(g && init && creation_open==2 && creation_current==init && test_creation_tls_calls==2);
        /* Deliberately poison borrowed after-read addresses in the retained
         * record; abort must use only resident parent/accounting fields. */
        creation_records[g-1].args[1]=1;creation_selected.object=1;
        unsigned caught=0;
        __try {
            __try {
                if(stop)creation_restore();
                RaiseException(0xe04d5042,0,0,NULL);
            } __finally {if(AbnormalTermination())creation_abort(init);}
        } __except(GetExceptionCode()==0xe04d5042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=1;}
        assert(caught && creation_current==g && creation_open==1);
        creation_abort(g);creation_abort(g); /* duplicate cannot decrement again */
        assert(!creation_current && !creation_open && !creation_window && test_creation_tls_calls==2);
        assert(creation_restore_open==(stop?2u:0u));
        for(unsigned n=0;n<2;n++)assert(creation_records[n].unwound && !creation_records[n].complete &&
            !creation_records[n].leave && !creation_records[n].result && !creation_records[n].after.reads &&
            (creation_records[n].error&CREATION_E_UNWIND));
        if(!stop)creation_restore();
    }
    /* A native parent can catch the inner exception and return normally.
     * Its own bracket remains live; a sibling is forwarded outside capture
     * after the sticky failure, without inheriting the aborted child's index. */
    test_creation_start();test_creation_arm();test_creation_tls_calls=0;
    g=creation_enter_with_tls_reader(CREATION_GENERATE,(uintptr_t)(image+0xba1b8b),generate,10,test_creation_read_tls);
    init=creation_enter_with_tls_reader(CREATION_INITIALIZE,(uintptr_t)(image+0xc84531),initialize,9,test_creation_read_tls);
    unsigned inner_caught=0;
    __try {
        __try {RaiseException(0xe04d5042,0,0,NULL);}
        __finally {if(AbnormalTermination())creation_abort(init);}
    } __except(GetExceptionCode()==0xe04d5042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {inner_caught=1;}
    assert(inner_caught && creation_current==g && creation_open==1);
    LONG before_sibling_reads=test_creation_tls_calls;
    assert(!creation_enter_with_tls_reader(CREATION_PILOT,(uintptr_t)(image+0xc8224f),pilot,7,test_creation_read_tls));
    assert(creation_current==g && creation_open==1 && test_creation_tls_calls==before_sibling_reads);
    creation_leave_with_tls_reader(g,(uintptr_t)&object_ref,1,test_creation_read_tls);
    assert(!creation_current && !creation_open && creation_records[g-1].complete &&
        !creation_records[init-1].complete && creation_records[init-1].unwound && creation_failures);
    creation_restore();
    /* Exact candidate access from another thread cannot update the token. */
    test_creation_start();test_creation_arm();g=test_creation_enter(CREATION_GENERATE,generate,10,tls);
    init=test_creation_enter(CREATION_INITIALIZE,initialize,9,tls);
    for(unsigned kind=CREATION_NAVIGATION;kind<=CREATION_AGENT;kind++) {
        test_creation_worker_args worker={.tls=tls,.kind=kind};memcpy(worker.args,kind==CREATION_AGENT?make_agent:nav,kind==CREATION_AGENT?sizeof(make_agent):sizeof(nav));
        HANDLE thread=CreateThread(NULL,0,test_creation_worker,&worker,0,NULL);assert(thread && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
        assert(worker.index && (worker.error&CREATION_E_THREAD) && creation_records[worker.index-1].before.thread==worker.thread && !creation_selected.controller && !creation_selected.agent);
    }
    creation_leave_context(init,0,0,tls);creation_leave_context(g,(uintptr_t)&object_ref,1,tls);creation_restore();
    /* Scope capacity refuses further capture while existing pairs still close. */
    test_creation_start();test_creation_arm();unsigned indexes[CREATION_DEPTH];
    for(unsigned i=0;i<CREATION_DEPTH;i++)indexes[i]=test_creation_enter(CREATION_GENERATE,generate,10,tls);
    assert(!test_creation_enter(CREATION_GENERATE,generate,10,tls) && creation_dropped==1 && creation_open==CREATION_DEPTH);
    for(unsigned i=CREATION_DEPTH;i;i--)creation_leave_context(indexes[i-1],(uintptr_t)&object_ref,1,tls);
    assert(!creation_open);creation_restore();
    test_creation_start();test_creation_arm();
    for(unsigned i=0;i<CREATION_CAPACITY;i++) {
        unsigned index=test_creation_enter(CREATION_GENERATE,generate,10,tls);assert(index==i+1);
        creation_leave_context(index,(uintptr_t)&object_ref,1,tls);
    }
    assert(!test_creation_enter(CREATION_GENERATE,generate,10,tls) && creation_count==CREATION_CAPACITY && creation_dropped==1);
    assert(!creation_open);creation_restore();
    test_creation_start();test_creation_arm();
    g=creation_enter_context(CREATION_GENERATE,(uintptr_t)(image+0xba1b8c),generate,10,tls);
    assert(g==1 && (creation_records[0].error&CREATION_E_CALLER));creation_leave_context(g,(uintptr_t)&object_ref,1,tls);creation_restore();
    /* A changed native word is not overwritten during cleanup. */
    test_creation_start();image[creation_patch_contracts[2].word]^=1;assert(!creation_hooks_owned());creation_restore();
    assert(creation_owned==(1u<<2) && creation_first_error==CREATION_E_OWNERSHIP);
    image[creation_patch_contracts[2].word]^=1;creation_restore();assert(!creation_owned && creation_failures);
    /* Restoration CAS success does not erase a failed protection/flush result. */
    for(unsigned failure=1;failure<=2;failure++) {
        test_creation_start();test_creation_api_failure=failure;creation_restore_using(test_creation_failed_restore);
        assert(!creation_owned && creation_restore_failures==1 && creation_pins_valid());
        DWORD prior_protection;
        assert(VirtualProtect(image+creation_patch_contracts[3].word,8,PAGE_READWRITE,&prior_protection));
        assert(FlushInstructionCache(GetCurrentProcess(),image+creation_patch_contracts[3].word,8));
        registry_md_installed=registry_origin_enabled=world_identity_installed=0;
        stats.restored=1;finish(75);assert(!stats.restored && creation_restore_failures==1);
    }
    assert(creation_pins_valid() && !memcmp(frozen,thunk,4096));test_tu_unwind_offsets(4);
    *(uint64_t*)(image+0x2ba5430+18*8)=saved_macro_getter;*(uint64_t*)(image+0x2b99a88+0x1620)=saved_owner_getter;
    *(unsigned*)(image+0x38f4ee4)=saved_read;*(unsigned*)(image+0x2f4b6f4)=saved_write;
    test_creation_reset();stats.active=0;stats.issued=0;boot_mode=boot_phase=0;ui_thread=0;
    registry_md_installed=registry_origin_enabled=world_identity_installed=0;registry_md_order=0;
    memcpy(registry_md_events,saved_md_events,sizeof(saved_md_events));registry_md_count=saved_md_count;
    registry_md_order=saved_md_order;registry_md_failures=saved_md_failures;
    registry_origin_closing=0;stats.reason=stats.restored=0;
    memcpy(dynamic_ships,saved_dynamic,sizeof(saved_dynamic));dynamic_ship_count=saved_dynamic_count;
    registry_failures=saved_registry_failures;registry_observing=saved_observing;
    memcpy(actor_components+ACTOR_DYNAMIC_FIRST-1,saved_components,sizeof(saved_components));
    memcpy(actor_ids+ACTOR_DYNAMIC_FIRST-1,saved_ids,sizeof(saved_ids));
    request_closing=world_queue_restoring=world_wait_restoring=0;
    assert(RtlDeleteFunctionTable(&thunk_unwind_functions[4]));thunk_unwind_bases[4]=0;
    assert(VirtualFree(thunk,0,MEM_RELEASE));thunk=prior;
    puts("creation observer: full pins/tamper, five-argument query/four-argument post, two RNG returns, lifetime/scope/foreign-thread guards, paired STOP and FloatState unwind verified on private memory");
}
