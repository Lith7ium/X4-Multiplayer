static unsigned test_wv_calls,test_wv_mode,test_wv_candidate_mode,test_wv_fault_reads;
static unsigned char test_wv_value[16],test_wv_position[0x30],test_wv_components[2][0xe0];
static uintptr_t test_wv_items[2],test_wv_header[3];
static HANDLE test_wv_entered,test_wv_release;
static world_action_event *test_wv_outer_parent;
static void test_wv_reset(void) {
    memset(&world_values_binding,0,sizeof(world_values_binding));world_unknown_identity_exported=0;
    memset(world_values_events,0,sizeof(world_values_events));world_values_candidate_current=NULL;
    world_values_calls=world_values_reserved=world_values_sequence=world_values_failures=world_values_dropped=0;
    world_values_other_scope=world_values_other_type=world_values_open=world_values_peak=world_values_restore_open=0;
    world_values_sites=world_values_exported=0;world_values_patch_failures=0;world_values_patch_errors=0;memset(world_values_patch_states,0,sizeof(world_values_patch_states));world_values_closing=0;world_values_enabled=world_values_started=1;
    memset(world_values_unknown_events,0,sizeof(world_values_unknown_events));memset(world_values_unknown_counts,0,sizeof(world_values_unknown_counts));
    world_values_unknown_total=world_values_unknown_omitted=0;world_values_unknown_reserved=world_values_unknown_exported=0;world_action_unknown_callback=NULL;
    test_wa_reset();test_wv_calls=test_wv_mode=test_wv_candidate_mode=test_wv_fault_reads=0;
    memset(test_wv_value,0,sizeof(test_wv_value));memset(test_wv_position,0,sizeof(test_wv_position));
    *(unsigned*)test_wv_value=0x63;*(void**)(test_wv_value+8)=test_wv_position;*(unsigned*)test_wv_position=1;
    *(float*)(test_wv_position+0x20)=12.5f;*(float*)(test_wv_position+0x24)=-4.0f;*(float*)(test_wv_position+0x28)=1.0f;
    for(unsigned i=0;i<2;i++) {
        memset(test_wv_components[i],0,sizeof(test_wv_components[i]));*(void**)test_wv_components[i]=image+0x2b80000;
        *(uint64_t*)(test_wv_components[i]+8)=0x9001+i;test_wv_components[i][0xd1]=1;
        assert(world_identity_record(0x9001+i,0x501+i));test_wv_items[i]=(uintptr_t)test_wv_components[i];
    }
    test_wv_header[0]=(uintptr_t)test_wv_items;test_wv_header[1]=(uintptr_t)(test_wv_items+2);test_wv_header[2]=test_wv_header[1];
    *(void**)test_wa_node=image+0x2c0cc88;*(void**)(image+0x2c0f3c8+0x18)=image+0xb87a30;
}
static int test_wv_reader(const void *source,void *out,size_t size) {
    int result=read_local(source,out,size);
    if(test_wv_mode==9&&source==test_wv_value&&++test_wv_fault_reads==1) *(unsigned*)test_wv_value=4;
    return result;
}
static uintptr_t test_wv_original_assign(void *a,void *b,void *c,void *d) {
    assert(a&&b==test_wa_exec+0x38&&c==(unsigned char*)a+0x10&&d==test_wv_value);++test_wv_calls;
    *(uint64_t*)(test_wa_tls+0x318)+=7;
    if(test_wv_mode==1) memset(test_wv_value,0xcc,16); /* source ownership can end here */
    if(test_wv_mode==2) ++*(uint64_t*)(test_wa_context+8);
    if(test_wv_mode==3) world_values_restore();
    if(test_wv_mode==4) {assert(SetEvent(test_wv_entered));assert(WaitForSingleObject(test_wv_release,2000)==WAIT_OBJECT_0);}
    return 0xfedcba9876543201ULL;
}
static uintptr_t test_wv_foreign_candidate(void *a,void *b,void *c,void *d) {
    (void)b;(void)d;++test_wv_calls;assert(!world_values_candidate_current&&!world_action_current);
    world_action_current=test_wv_outer_parent;
    uintptr_t result=world_values_assign_observe(test_wv_original_assign,a,c,(unsigned char*)a+0x10,test_wv_value,
        (uintptr_t)(image+0xd2ad67),test_wv_reader,test_wa_tls_reader);
    world_action_current=NULL;return result;
}
static uintptr_t test_wv_original_candidate(void *a,void *b,void *c,void *d) {
    assert(a==test_wa_node+0xf0&&b==test_wv_header&&c==test_wa_exec+0x38&&!d);++test_wv_calls;
    uint64_t *rng=(uint64_t*)(test_wa_tls+0x318);*rng+=11;
    *(unsigned*)test_wv_value=0xa;*(uint64_t*)(test_wv_value+8)=0x9001;
    if(test_wv_candidate_mode==3) {
        test_wv_outer_parent=world_action_current;world_action_current=NULL;
        assert(world_values_candidate_observe(test_wv_foreign_candidate,a,b,c,d,(uintptr_t)(image+0xb87ae8),test_wv_reader,test_wa_tls_reader)==0xfedcba9876543201ULL);
        world_action_current=test_wv_outer_parent;return 0xaabbccdd11223344ULL;
    }
    uintptr_t result=world_values_assign_observe(test_wv_original_assign,a,c,(unsigned char*)a+0x10,test_wv_value,
        (uintptr_t)(image+0xd2ad67),test_wv_reader,test_wa_tls_reader);
    assert(result==0xfedcba9876543201ULL);
    if(test_wv_candidate_mode==1) ++*(uint64_t*)(test_wv_components[0]+8);
    if(test_wv_candidate_mode==2) ++test_wv_header[1];
    return 0xaabbccdd11223344ULL;
}
static void test_wv_action(void *node,void *exec,void *frame) {
    assert(node==test_wa_node&&exec==test_wa_exec&&frame==test_wa_exec+0x38);
    if(*(void**)node==image+0x2c0f3c8) {
        assert(world_values_candidate_observe(test_wv_original_candidate,(unsigned char*)node+0xf0,test_wv_header,frame,NULL,
            (uintptr_t)(image+0xb87ae8),test_wv_reader,test_wa_tls_reader)==0xaabbccdd11223344ULL);
    } else {
        uintptr_t caller=test_wv_mode==5?(uintptr_t)test_wv_action:(uintptr_t)(image+0xbe23af);
        assert(world_values_assign_observe(test_wv_original_assign,(unsigned char*)node+0x1b8,frame,(unsigned char*)node+0x1c8,test_wv_value,
            caller,test_wv_reader,test_wa_tls_reader)==0xfedcba9876543201ULL);
    }
}
static void test_wv_call(void) {
    npc_provenance_frame scope;int entered=npc_provenance_enter(&scope,test_wa_exec,0);assert(entered==1);
    world_action_observe_with_tls(test_wv_action,test_wa_node,test_wa_exec,test_wa_exec+0x38,test_wa_tls_reader);
    npc_provenance_leave(&scope,entered);
}
static DWORD WINAPI test_wv_worker(void *unused) {(void)unused;ui_thread=GetCurrentThreadId();test_wv_call();return 0;}
static unsigned test_wv_abi_calls,test_wv_abi_stop;
static uint64_t test_wv_words[2];static unsigned test_wv_swap_failure,test_wv_config_result;
static HANDLE test_wv_config_entered,test_wv_config_release,test_wv_stop_entered;
static unsigned test_wv_setup_calls;
static int test_wv_pins(void) {++test_wv_setup_calls;return 1;}
static int test_wv_prepare(void) {
    ++test_wv_setup_calls;
    world_values_original[0]=1;world_values_original[1]=2;world_values_patched[0]=11;world_values_patched[1]=12;
    if(test_wv_config_entered) {assert(SetEvent(test_wv_config_entered));assert(WaitForSingleObject(test_wv_config_release,2000)==WAIT_OBJECT_0);}return 1;
}
static world_values_swap_result test_wv_swap(unsigned index,uint64_t expected,uint64_t value) {
    ++test_wv_setup_calls;
    world_values_swap_result failed={0},ok={1,1};
    if(test_wv_swap_failure&&((index==1&&value==12)||(test_wv_swap_failure==2&&index==0&&value==1))) return failed;
    if(test_wv_words[index]!=expected) return failed;test_wv_words[index]=value;return ok;
}
static void test_wv_reattach(void) {
    test_wv_reset();world_values_started=world_values_enabled=0;boot_phase=0;test_wv_words[0]=1;test_wv_words[1]=2;
    unsigned char *prior=thunk,*page=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(page);thunk=page;
    uint64_t original[2];for(unsigned i=0;i<2;i++) {
        memcpy(&original[i],image+world_values_entry(i),8);const thunk_recipe *recipe=thunk_recipe_at(3584+i*64);
        assert(recipe&&thunk_emit_slot(page+recipe->offset,recipe));
    }
    DWORD old;assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
    assert(world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap));
    for(unsigned i=0;i<2;i++) memcpy(image+world_values_entry(i),&world_values_patched[i],8);
    world_values_reserved=world_values_calls=3;world_values_sequence=6;world_values_unknown_reserved=2;
    world_values_unknown_total=5;world_values_unknown_omitted=3;world_values_unknown_counts[6]=5;
    world_action_unknown=5; /* Coverage failure is not an explicit capture fault. */
    world_values_exported=3;world_values_unknown_exported=1;world_unknown_identity_exported=2;
    world_values_unknown_events[0].identity.before.script.q[7]=0x123456789ULL;
    world_values_unknown_event *snapshot=malloc(sizeof(world_values_unknown_events));assert(snapshot);memcpy(snapshot,world_values_unknown_events,sizeof(world_values_unknown_events));
    test_wv_setup_calls=0;
    for(unsigned phase=1;phase<=2;phase++) {
        boot_phase=phase;assert(world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap));
        assert(!test_wv_setup_calls&&!memcmp(snapshot,world_values_unknown_events,sizeof(world_values_unknown_events)));
        assert(world_values_reserved==3&&world_values_calls==3&&world_values_sequence==6&&world_values_unknown_reserved==2&&world_values_unknown_total==5&&world_values_unknown_omitted==3&&world_values_unknown_counts[6]==5);
        assert(world_values_exported==3&&world_values_unknown_exported==1&&world_unknown_identity_exported==2);
    }
    for(unsigned field=0;field<21;field++) {
        if(field==0) secret[0]^=1;if(field==1) initial_state_digest[0]^=1;if(field==2) ++remote_port;
        if(field==3) ++stats.slot;if(field==4) ++boot_started;if(field==5) boot_options^=1;
        if(field==6) world_values_closing=1;if(field==7) stats.active=0;if(field==8) world_values_patch_states[0].flush_pending=1;
        if(field==9) image[world_values_entry(0)]^=1;if(field==10) world_values_patch_failures=1;
        if(field==11) world_values_patch_states[0].protection_pending=1;if(field==12) boot_mode=0;
        if(field==13) world_values_failures=1;if(field==14) world_action_closing=1;
        if(field==15) world_action_enabled=0;if(field==16) world_action_started=0;
        if(field==17) world_action_failures=1;if(field==18) world_action_unknown_callback=NULL;
        if(field==19) world_values_dropped=1;if(field==20) world_action_dropped=1;
        assert(!world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap));
        assert(!test_wv_setup_calls&&!memcmp(snapshot,world_values_unknown_events,sizeof(world_values_unknown_events)));
        assert(world_action_unknown==5&&world_values_unknown_total==5&&world_values_unknown_omitted==3&&world_values_unknown_counts[6]==5);
        assert(world_values_exported==3&&world_values_unknown_exported==1&&world_unknown_identity_exported==2);
        if(field==13) assert(world_values_failures==1);if(field==17) assert(world_action_failures==1);
        if(field==19) assert(world_values_dropped==1);if(field==20) assert(world_action_dropped==1);
        if(field==0) secret[0]^=1;if(field==1) initial_state_digest[0]^=1;if(field==2) --remote_port;
        if(field==3) --stats.slot;if(field==4) --boot_started;if(field==5) boot_options^=1;
        if(field==6) world_values_closing=0;if(field==7) stats.active=1;if(field==8) world_values_patch_states[0].flush_pending=0;
        if(field==9) image[world_values_entry(0)]^=1;if(field==10) world_values_patch_failures=0;
        if(field==11) world_values_patch_states[0].protection_pending=0;if(field==12) boot_mode=1;
        if(field==13) world_values_failures=0;if(field==14) world_action_closing=0;
        if(field==15) world_action_enabled=1;if(field==16) world_action_started=1;
        if(field==17) world_action_failures=0;if(field==18) world_action_unknown_callback=world_values_unknown_capture;
        if(field==19) world_values_dropped=0;if(field==20) world_action_dropped=0;
    }
    for(unsigned i=0;i<2;i++) memcpy(image+world_values_entry(i),&original[i],8);
    free(snapshot);thunk=prior;VirtualFree(page,0,MEM_RELEASE);test_wv_reset();
    puts("world Values same-session phase1/2 reattach, retained buffers/cursors, changed-session/STOP/debt/foreign refusal PASS");
}
static DWORD WINAPI test_wv_config_worker(void *unused) {
    (void)unused;ui_thread=GetCurrentThreadId();test_wv_config_result=world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap);return 0;
}
static DWORD WINAPI test_wv_stop_worker(void *unused) {
    (void)unused;assert(SetEvent(test_wv_stop_entered));world_values_restore_using(test_wv_swap,world_values_repair);return 0;
}
static void test_wv_configuration(void) {
    test_wv_reset();world_values_started=world_values_enabled=0;boot_phase=0;test_wv_words[0]=1;test_wv_words[1]=2;
    world_values_restore_using(test_wv_swap,world_values_repair);assert(!world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap));
    assert(test_wv_words[0]==1&&test_wv_words[1]==2&&!world_values_sites); /* STOP before configuration */
    for(unsigned failure=1;failure<=2;failure++) {
        test_wv_reset();world_values_started=world_values_enabled=0;boot_phase=0;test_wv_words[0]=1;test_wv_words[1]=2;test_wv_swap_failure=failure;
        assert(!world_values_configure_using(test_wv_pins,test_wv_prepare,test_wv_swap)&&world_values_closing&&!world_values_enabled);
        assert(world_values_sites==(failure==2?1u:0u));test_wv_swap_failure=0;world_values_restore_using(test_wv_swap,world_values_repair);
        assert(!world_values_sites&&test_wv_words[0]==1&&test_wv_words[1]==2);
    }
    test_wv_reset();world_values_started=world_values_enabled=0;boot_phase=0;test_wv_words[0]=1;test_wv_words[1]=2;
    test_wv_config_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_wv_config_release=CreateEventW(NULL,TRUE,FALSE,NULL);test_wv_stop_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE configure=CreateThread(NULL,0,test_wv_config_worker,NULL,0,NULL);assert(configure&&WaitForSingleObject(test_wv_config_entered,2000)==WAIT_OBJECT_0);
    HANDLE stop=CreateThread(NULL,0,test_wv_stop_worker,NULL,0,NULL);assert(stop&&WaitForSingleObject(test_wv_stop_entered,2000)==WAIT_OBJECT_0);
    assert(!world_values_sites&&test_wv_words[0]==1);assert(SetEvent(test_wv_config_release));
    assert(WaitForSingleObject(configure,2000)==WAIT_OBJECT_0&&WaitForSingleObject(stop,2000)==WAIT_OBJECT_0);
    assert(test_wv_config_result&&!world_values_sites&&!world_values_enabled&&world_values_closing&&test_wv_words[0]==1&&test_wv_words[1]==2);
    CloseHandle(configure);CloseHandle(stop);CloseHandle(test_wv_config_entered);CloseHandle(test_wv_config_release);CloseHandle(test_wv_stop_entered);
    test_wv_config_entered=test_wv_config_release=test_wv_stop_entered=NULL;ui_thread=GetCurrentThreadId();boot_phase=2;
    puts("world Values serialized configure/STOP and partial-patch rollback schedules PASS");
}
static DWORD test_wv_permissions[2];
static unsigned test_wv_protect_calls,test_wv_flush_calls;
static uint64_t test_wv_protect_fail_at,test_wv_flush_fail_at;
static BOOL WINAPI test_wv_protect(LPVOID address,SIZE_T size,DWORD protection,PDWORD old) {
    unsigned index=(unsigned char*)address==image+WORLD_VALUES_CANDIDATE;
    assert((unsigned char*)address==image+world_values_entry(index)&&size==8);
    ++test_wv_protect_calls;
    if(test_wv_protect_fail_at&(1ULL<<test_wv_protect_calls)) return FALSE;
    *old=test_wv_permissions[index];test_wv_permissions[index]=protection;return TRUE;
}
static BOOL WINAPI test_wv_flush(HANDLE process,LPCVOID address,SIZE_T size) {
    assert(process==GetCurrentProcess()&&size==8&&((const unsigned char*)address==image+WORLD_VALUES_ASSIGN||(const unsigned char*)address==image+WORLD_VALUES_CANDIDATE));
    return !(test_wv_flush_fail_at&(1ULL<<++test_wv_flush_calls));
}
static world_values_swap_result test_wv_ops_swap(unsigned index,uint64_t expected,uint64_t value) {
    return world_values_swap_with(index,expected,value,test_wv_protect,test_wv_flush);
}
static int test_wv_ops_repair(unsigned index) {return world_values_repair_with(index,test_wv_protect,test_wv_flush);}
static void test_wv_os_reset(void) {
    test_wv_reset();test_wv_prepare();
    for(unsigned i=0;i<2;i++) {memcpy(image+world_values_entry(i),&world_values_original[i],8);test_wv_permissions[i]=PAGE_EXECUTE_READ;}
    test_wv_protect_calls=test_wv_flush_calls=0;test_wv_protect_fail_at=test_wv_flush_fail_at=0;
}
static void test_wv_patch_failures(void) {
    for(unsigned index=0;index<2;index++) for(unsigned flush=0;flush<2;flush++) {
        test_wv_os_reset();
        if(flush) test_wv_flush_fail_at=1ULL<<(index+1);else test_wv_protect_fail_at=1ULL<<(index*2+2);
        assert(!world_values_patch(test_wv_ops_swap)); /* CAS succeeded, post-publication failed. */
        assert(!world_values_sites&&world_values_patch_failures==1&&world_values_failures==1);
        assert(world_values_patch_errors==(flush?WV_PATCH_FLUSH:WV_PATCH_PROTECTION));
        for(unsigned i=0;i<2;i++) assert(!memcmp(image+world_values_entry(i),&world_values_original[i],8)&&test_wv_permissions[i]==PAGE_EXECUTE_READ);
        assert(!world_values_pending(0)&&!world_values_pending(1));
    }
    for(unsigned flush=0;flush<2;flush++) for(unsigned persistent=0;persistent<2;persistent++) {
        test_wv_os_reset();assert(world_values_patch(test_wv_ops_swap)&&world_values_sites==3);
        test_wv_protect_calls=test_wv_flush_calls=0;
        if(flush) test_wv_flush_fail_at=(1ULL<<1)|(persistent?(1ULL<<2):0);
        else test_wv_protect_fail_at=(1ULL<<2)|(persistent?(1ULL<<3):0);
        world_values_restore_using(test_wv_ops_swap,test_wv_ops_repair);
        assert(!world_values_sites&&world_values_patch_failures==(persistent?2:1)&&world_values_failures==world_values_patch_failures);
        assert(world_values_patch_errors==((flush?WV_PATCH_FLUSH:WV_PATCH_PROTECTION)|(persistent?(flush?WV_PATCH_REPAIR_FLUSH:WV_PATCH_REPAIR_PROTECTION):0)));
        assert(!memcmp(image+WORLD_VALUES_ASSIGN,&world_values_original[0],8));
        assert(world_values_pending(flush)==(persistent?1u:0u));
        if(!flush) assert(test_wv_permissions[0]==(persistent?PAGE_EXECUTE_READWRITE:PAGE_EXECUTE_READ));
        test_wv_protect_fail_at=test_wv_flush_fail_at=0;
        world_values_restore_using(test_wv_ops_swap,test_wv_ops_repair); /* bytes already restored, pending OS work still retried */
        assert(!world_values_pending(0)&&!world_values_pending(1)&&test_wv_permissions[0]==PAGE_EXECUTE_READ);
        assert(world_values_patch_failures==(persistent?2:1)); /* sticky failure never becomes a clean STOP */
    }
    test_wv_os_reset();test_wv_protect_fail_at=(1ULL<<2)|(1ULL<<3);
    assert(!world_values_patch(test_wv_ops_swap)&&world_values_sites==1&&world_values_pending(0)==1);
    assert(!memcmp(image+WORLD_VALUES_ASSIGN,&world_values_patched[0],8));
    test_wv_protect_fail_at=0;world_values_restore_using(test_wv_ops_swap,test_wv_ops_repair);
    assert(!world_values_sites&&!world_values_pending(0)&&test_wv_permissions[0]==PAGE_EXECUTE_READ&&world_values_patch_failures==2);
    assert(world_values_patch_errors==(WV_PATCH_PROTECTION|WV_PATCH_OPEN));
    test_wv_os_reset();assert(world_values_patch(test_wv_ops_swap));uint64_t foreign=0xdeadbeef;
    memcpy(image+WORLD_VALUES_ASSIGN,&foreign,8);world_values_restore_using(test_wv_ops_swap,test_wv_ops_repair);
    assert(world_values_sites==1&&!memcmp(image+WORLD_VALUES_ASSIGN,&foreign,8)&&world_values_patch_failures==1);
    assert(world_values_patch_errors==WV_PATCH_FOREIGN);
    memcpy(image+WORLD_VALUES_ASSIGN,&world_values_patched[0],8);world_values_restore_using(test_wv_ops_swap,test_wv_ops_repair);
    assert(!world_values_sites&&world_values_patch_failures==1);test_wv_reset();
    puts("world Values OS publication/restore failures, owned-current rollback, sticky evidence and foreign restoration PASS");
}
static uintptr_t test_wv_abi_target(void *a,void *b,void *c,void *d) {
    assert(a==(void*)0x11&&b==(void*)0x22&&c==(void*)0x33&&d==(void*)0x44);++test_wv_abi_calls;
    if(test_wv_abi_stop) world_values_restore();return 0xfedcba9876543201ULL;
}
static void test_wv_actual_abi(void) {
    if(!test_wq_image_path) return;
    FILE *f=fopen(test_wq_image_path,"rb");assert(f&&!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0&&!fseek(f,0,SEEK_SET));
    unsigned char *bytes=malloc((size_t)length);assert(bytes&&fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);assert(hash_ok(bytes,(ULONG)length,EXE_SHA));
    unsigned char *page=VirtualAlloc(NULL,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(page);
    for(unsigned i=0;i<2;i++) {
        unsigned char *entry=page+4096+i*128;unsigned rva=world_values_entry(i);
        test_ww_copy_rva(bytes,(size_t)length,rva,entry,8);const thunk_recipe *recipe=thunk_recipe_at(3584+i*64);
        assert(recipe&&recipe->prefix_size==5&&!memcmp(entry,recipe->prefix,5));
        assert(thunk_emit_slot_at(page+recipe->offset,recipe,entry));
        /* Test-only continuation after the exact native five-byte replay. */
        thunk_absolute_jump(entry+5,(void*)test_wv_abi_target);
    }
    DWORD old;assert(VirtualProtect(page,8192,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),page,8192);
    unsigned char *prior=thunk,*prior_image=image;thunk=page;world_values_enabled=0;world_values_sites=0;test_wv_abi_calls=0;
    for(unsigned i=0;i<2;i++) for(unsigned stop=0;stop<2;stop++) {
        unsigned char *entry=page+4096+i*128;image=(unsigned char*)((uintptr_t)entry-world_values_entry(i));
        memcpy(&world_values_original[i],entry,8);world_values_patched[i]=world_values_original[i];
        unsigned char *patch=(unsigned char*)&world_values_patched[i];patch[0]=0xe9;
        int32_t distance=(int32_t)((page+3584+i*64+32)-(entry+5));memcpy(patch+1,&distance,4);
        assert(!memcmp((unsigned char*)&world_values_original[i]+5,patch+5,3));
        assert(!world_values_swap(i,world_values_original[i]^1,world_values_patched[i]).changed); /* foreign bytes untouched */
        assert(world_values_swap(i,world_values_original[i],world_values_patched[i]).ok);world_values_sites=1u<<i;
        test_wv_abi_stop=stop;world_values_closing=0;
        assert(((world_values_fn)entry)((void*)0x11,(void*)0x22,(void*)0x33,(void*)0x44)==0xfedcba9876543201ULL);
        if(!stop) world_values_restore();
        assert(!world_values_sites&&!memcmp(entry,&world_values_original[i],8));
    }
    assert(test_wv_abi_calls==4);thunk=prior;image=prior_image;VirtualFree(page,0,MEM_RELEASE);free(bytes);test_wv_abi_stop=0;
    puts("world Value/candidate actual-image prefix replay, owned CAS, four arguments, fullRAX and restore-inside-original PASS");
}
static void test_wv_row_bounds(void) {
    world_values_event e;memset(&e,0xff,sizeof(e));e.kind=1;e.before.count=e.after.count=WORLD_VALUES_CANDIDATES;
    char row[2048];unsigned largest=0;
    for(unsigned p=0;p<world_values_event_rows(&e);p++) {
        int n=world_values_format(&e,UINT32_MAX,p,row,sizeof(row));assert(n>0&&(unsigned)n+strlen("[X4LAN] NATIVE_GATE \n")<sizeof(row));
        if((unsigned)n>largest) largest=(unsigned)n;
        char short_row[8];assert(world_values_format(&e,UINT32_MAX,p,short_row,sizeof(short_row))<0);
    }
    world_values_unknown_event unknown;memset(&unknown,0xff,sizeof(unknown));
    int n=world_values_unknown_format(&unknown,UINT32_MAX,row,sizeof(row));assert(n>0&&(unsigned)n+strlen("[X4LAN] NATIVE_GATE \n")<sizeof(row));
    test_wv_reset();world_values_calls=world_values_reserved=world_values_failures=world_values_dropped=INT64_MAX;
    world_values_other_scope=world_values_other_type=world_values_open=world_values_peak=world_values_restore_open=INT64_MAX;
    world_values_exported=world_values_sites=UINT32_MAX;world_values_unknown_total=world_values_unknown_omitted=INT64_MAX;world_values_patch_failures=INT64_MAX;world_values_patch_errors=INT32_MAX;
    world_values_unknown_exported=UINT32_MAX;
    for(unsigned i=1;i<=7;i++) world_values_unknown_counts[i]=INT64_MAX;
    char stop[1024];n=world_values_status(stop,sizeof(stop));assert(n>0&&(unsigned)n+strlen("[X4LAN] NATIVE_GATE WORLD_VALUES_STOP \n")<2048);
    printf("world Values worst-width formatter bounds PASS (largest event row %u bytes)\n",largest);
    test_wv_reset();
}
#include "test_lockstep_world_unknown_identity.h"
static void test_world_values(void) {
    test_wv_reset();world_values_enabled=0;uint64_t rng=*(uint64_t*)(test_wa_tls+0x318);test_wv_call();
    assert(test_wv_calls==1&&!world_values_reserved&&*(uint64_t*)(test_wa_tls+0x318)==rng+7);
    test_wv_reset();test_wv_call();assert(test_wv_calls==1&&world_values_ready()==1&&!world_values_failures);
    assert(world_values_events[0].value.kind==2&&world_values_events[0].result==0xfedcba9876543201ULL);
    assert(world_values_events[0].rng_after==world_values_events[0].rng_before+7);
    world_values_restore();world_action_restore();
    FILE *export=fopen(".local/native-lockstep/world-values-c-export.log","wb");assert(export);
    for(unsigned p=0;p<3;p++) {char row[2048];assert(world_action_format(&world_action_events[0],1,p,row,sizeof(row))>0);fprintf(export,"NATIVE_GATE %s\n",row);}
    world_action_exported=3;char status[1024];assert(world_action_status(status,sizeof(status))>0);fprintf(export,"NATIVE_GATE WORLD_ACTION_STOP %s\n",status);
    for(unsigned p=0;p<3;p++) {char row[2048];assert(world_values_format(&world_values_events[0],1,p,row,sizeof(row))>0);fprintf(export,"NATIVE_GATE %s\n",row);}
    world_values_exported=3;assert(world_values_status(status,sizeof(status))>0);fprintf(export,"NATIVE_GATE WORLD_VALUES_STOP %s\n",status);assert(!fclose(export));
    test_wv_reset();test_wv_mode=1;test_wv_call();assert(!world_values_failures&&world_values_events[0].value.tag==0x63&&test_wv_value[0]==0xcc);
    test_wv_reset();test_wv_mode=2;test_wv_call();assert(world_values_events[0].errors&WV_LIFETIME);--*(uint64_t*)(test_wa_context+8);
    test_wv_reset();test_wv_mode=5;test_wv_call();assert(test_wv_calls==1&&(world_values_events[0].errors&WV_CALLER));
    test_wv_reset();test_wv_mode=9;test_wv_call();assert(test_wv_calls==1&&(world_values_events[0].errors&WV_TYPE));
    test_wv_reset();*(unsigned*)test_wv_value=0x62;test_wv_call();assert(test_wv_calls==1&&(world_values_events[0].errors&WV_TYPE));
    unsigned char *guard=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(guard);DWORD old;
    assert(VirtualProtect(guard+4096,4096,PAGE_NOACCESS,&old));test_wv_reset();*(unsigned*)(guard+4096-0x28)=1;
    *(void**)(test_wv_value+8)=guard+4096-0x28;test_wv_call();assert(test_wv_calls==1&&(world_values_events[0].errors&WV_TYPE));
    world_values_value invalid;assert(!world_values_value_stable(read_local,guard+4096-8,&invalid));VirtualFree(guard,0,MEM_RELEASE);
    test_wv_reset();*(void**)test_wa_node=image+0x2c0f3c8;test_wv_call();assert(test_wv_calls==2&&world_values_ready()==2&&!world_values_failures);
    assert(world_values_events[0].kind==1&&world_values_events[0].children==1&&world_values_events[1].parent_candidate==1&&world_values_events[1].value.saved==0x501);
    assert(world_values_events[0].before.items[0].saved==0x501&&world_values_events[0].before.items[1].saved==0x502);
    test_wv_reset();*(void**)test_wa_node=image+0x2c0f3c8;test_wv_candidate_mode=1;test_wv_call();assert(world_values_events[0].errors&WV_LIFETIME);
    test_wv_reset();*(void**)test_wa_node=image+0x2c0f3c8;test_wv_candidate_mode=2;test_wv_call();assert(world_values_events[0].errors&WV_LIFETIME);
    test_wv_reset();*(void**)test_wa_node=image+0x2c0f3c8;test_wv_candidate_mode=3;test_wv_call();
    assert(test_wv_calls==3&&world_values_other_scope==1&&world_values_events[0].children==0&&!world_values_events[1].parent_candidate&&(world_values_events[1].errors&WV_NEST));
    test_wv_reset();test_wv_mode=3;test_wv_call();assert(world_values_restore_open==1&&!world_values_open&&(world_values_events[0].errors&WV_STOP));
    unsigned calls=test_wv_calls;test_wv_call();assert(test_wv_calls==calls+1&&world_values_ready()==1);
    test_wv_reset();test_wv_mode=4;test_wv_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_wv_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE worker=CreateThread(NULL,0,test_wv_worker,NULL,0,NULL);assert(worker&&WaitForSingleObject(test_wv_entered,2000)==WAIT_OBJECT_0);
    assert(world_values_open==1&&!world_values_ready());world_values_restore();assert(world_values_restore_open==1&&!world_values_admit());
    assert(SetEvent(test_wv_release)&&WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);assert(world_values_events[0].errors&WV_STOP);
    CloseHandle(worker);CloseHandle(test_wv_entered);CloseHandle(test_wv_release);ui_thread=GetCurrentThreadId();
    test_wv_reset();world_values_reserved=WORLD_VALUES_MAX;test_wv_call();assert(test_wv_calls==1&&world_values_dropped==1);
    test_wv_reset();world_action_unknown_callback=world_values_unknown_capture;
    unsigned char *entity=*(unsigned char**)(test_wa_context+0x120);*(uint64_t*)(entity+8)=0xdead;
    rng=*(uint64_t*)(test_wa_tls+0x318);
    for(unsigned i=0;i<6;i++) test_wv_call();
    assert(world_action_unknown==6&&world_values_unknown_total==6&&world_values_unknown_counts[6]==6&&world_values_unknown_ready()==4&&world_values_unknown_omitted==2);
    assert(test_wv_calls==6&&*(uint64_t*)(test_wa_tls+0x318)==rng+42&&!world_values_open);
    assert(world_values_unknown_events[0].raw_tag==0x66&&(world_values_unknown_events[0].reads&2)&&!world_values_unknown_events[0].owner.saved);
    *(uint64_t*)(entity+8)=0x9999;world_action_unknown_callback=NULL;
    test_wv_configuration();test_wv_reattach();test_wv_patch_failures();test_wv_actual_abi();test_wv_row_bounds();test_world_unknown_identity();
    test_wv_reset();world_values_enabled=world_values_started=0;test_wa_reset();
    puts("world typed Value/candidate original-once, fullRAX, borrowed payload, mutation, parent and STOP tests PASS");
}
