/* Included after the real owned-image harness helpers. No engine is launched. */
static double rejection_index;
static double rejection_number(lua_State *unused,int index) {(void)unused;TEST(index==1);return rejection_index;}
static void rejection_reattach_reads(void) {
    test_read_entry baseline[4096];unsigned count=0;
    for(unsigned mode=0;mode<2;mode++)for(unsigned phase=0;phase<4;phase++) {
        reset_state();boot_phase=(LONG)phase;rotation_reject_mode=(LONG)mode;rotation_reject_boot=rotation_boot;
        test_read_count=0;test_read_enabled=1;TEST(reattach());test_read_enabled=0;
        count=test_read_count;TEST(count==24);memcpy(baseline,test_read_trace,count*sizeof(*baseline));
        rotation_reject_first.present=1;rotation_reject_first.serial=17;
        rotation_reject_entry saved=rotation_reject_first;rotation_reject_completion done=rotation_reject_done;
        test_read_count=0;test_read_enabled=1;unsigned protects=test_protect_calls,flushes=test_flush_calls;
        TEST(rotation_rejection_enable(mode));TEST(test_read_count==0);
        TEST(test_protect_calls==protects && test_flush_calls==flushes);
        TEST(reattach());test_read_enabled=0;TEST(test_read_count==count);
        for(unsigned i=0;i<count;i++)TEST(test_read_trace[i].address==baseline[i].address &&
            test_read_trace[i].length==baseline[i].length && test_read_trace[i].ok==baseline[i].ok);
        TEST(!memcmp(&saved,&rotation_reject_first,sizeof saved) && !memcmp(&done,&rotation_reject_done,sizeof done));
    }
    for(unsigned which=0;which<12;which++) {
        reset_state();rotation_reject_mode=1;rotation_reject_boot=rotation_boot;
        switch(which) {
        case 0:rotation_failure=ROTATION_E_INPUT;break;
        case 1:rotation_stop=1;break;
        case 2:rotation_restore_error=1;break;
        case 3:rotation_phase=ROTATION_OFF;break;
        case 4:rotation_phase=ROTATION_FAILED;break;
        case 5:rotation_patches[0].owned=0;break;
        case 6:rotation_patches[1].protection_pending=1;break;
        case 7:rotation_patches[1].flush_pending=1;break;
        case 8:rotation_boot.started++;break;
        case 9:rotation_reject_boot.started++;break;
        case 10:ui_thread++;break;
        case 11:rotation_reject_mode=0;break;
        }
        test_read_count=0;test_read_enabled=1;TEST(!rotation_rejection_enable(1));test_read_enabled=0;
        TEST(!test_read_count);
    }
    /* A local mode ack does not attest foreign engine bytes. The unchanged
       configuration check still rejects them, with no extra preliminary read. */
    reset_state();rotation_reject_mode=1;rotation_reject_boot=rotation_boot;
    unsigned char byte=image[ROTATION_TAIL_WORD];protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);
    image[ROTATION_TAIL_WORD]^=1;protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    test_read_count=0;test_read_enabled=1;TEST(rotation_rejection_enable(1) && !test_read_count);
    TEST(!reattach());test_read_enabled=0;TEST(test_read_count==24 && rotation_failure==ROTATION_E_CONFIG);
    protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);image[ROTATION_TAIL_WORD]=byte;protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    rotation_native_restore();
}
static void rejection_read_failures(void) {
    /* Inject at every read in the actual successful selected-call path. This
       reaches pre/capture/claim/post/replay/plan/publication reads without a
       substitute implementation of the production wrapper. */
    unsigned total=0,stages=0,first_replay_read=UINT32_MAX;test_read_entry baseline[4096];unsigned count=0;
    for(unsigned fail=0;fail<=total;fail++) for(unsigned enabled=0;enabled<2;enabled++) {
        reset_state();rotation_reject_mode=(LONG)enabled;
        TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
        test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
        current_agent=cohort_agents;current_movement=cohort_xml;
        test_read_count=0;test_fail_at=fail?fail-1:UINT32_MAX;test_read_enabled=1;
        TEST(call_role(0,current_agent)==tail_result && original_tails==1 && rotation_tail_returns==1);
        test_read_enabled=0;test_fail_at=UINT32_MAX;
        if(!fail) {total=test_read_count;TEST(!rotation_failure && rotation_written[0]==1);}
        else {
            TEST(rotation_failure && !rotation_written[0]);
            if(enabled) {
                TEST(rotation_reject_first.present && rotation_reject_done.present && rotation_reject_done.return_valid);
                TEST(rotation_reject_first.trace.failed_reads==1);
                stages|=1u<<rotation_reject_first.stage;
                if(rotation_reject_first.stage==RR_REPLAY && first_replay_read==UINT32_MAX)first_replay_read=fail-1;
                for(unsigned n=0;n<rotation_reject_first.trace.records;n++)
                    if(!rotation_reject_first.trace.reads[n].ok)TEST(!rotation_reject_first.trace.reads[n].copied);
            }
        }
        if(!enabled){count=test_read_count;memcpy(baseline,test_read_trace,count*sizeof(*baseline));}
        else {
            TEST(count==test_read_count);
            for(unsigned n=0;n<count;n++)TEST(baseline[n].address==test_read_trace[n].address &&
                baseline[n].length==test_read_trace[n].length && baseline[n].ok==test_read_trace[n].ok);
        }
        rotation_native_restore();
    }
    unsigned required=(1u<<RR_PRE_GUARD)|(1u<<RR_CAPTURE)|(1u<<RR_CLAIM_GUARD)|(1u<<RR_POST_GUARD)|
        (1u<<RR_REPLAY)|(1u<<RR_PLAN)|(1u<<RR_PUBLISH_REPLAY)|(1u<<RR_PUBLISH_GUARD);
    TEST((stages&required)==required);
    /* Actual selected wrapper: original has returned before a foreign
       exception crosses the unlocked replay reader. No engine runs here. */
    TEST(first_replay_read!=UINT32_MAX);reset_state();rotation_reject_mode=1;
    TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
    test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
    current_agent=cohort_agents;current_movement=cohort_xml;
    test_read_count=0;test_throw_at=first_replay_read;test_read_enabled=1;int caught=0;
    __try {(void)call_role(0,current_agent);} __except(EXCEPTION_EXECUTE_HANDLER){caught=1;}
    test_read_enabled=0;test_throw_at=UINT32_MAX;
    TEST(caught && original_tails==1 && rotation_tail_returns==1 && !rotation_written[0] && !rotation_scope_open);
    TEST(rotation_reject_first.stage==RR_REPLAY && rotation_reject_first.predicate==RP_UNWIND);
    TEST(rotation_reject_done.return_valid && rotation_reject_done.unwind && rotation_reject_done.raw_return==tail_result);
    TEST(rotation_reject_done.primary_failure==ROTATION_E_UNWIND);
    rotation_load_leave(0,0);rotation_native_stop();
    if(getenv("X4ROT_TEST_REJECTION")) {
        char path[1024],row[2048];int n=snprintf(path,sizeof path,"%s.after",getenv("X4ROT_TEST_REJECTION"));TEST(n>0 && n<(int)sizeof path);
        FILE *out=fopen(path,"wb");TEST(out);
        for(unsigned i=0;i<=rotation_reject_first.trace.records+1;i++) {
            TEST(rotation_rejection_text(i,row,sizeof row)>0);fprintf(out,"%s\n",row);
        }
        TEST(!fclose(out));
    }
}
static void rejection_lifetimes(void) {
    const unsigned expected[]={RP_ROLE_DUPLICATE,RP_INDEX_JOIN,RP_INDEX_JOIN,RP_LIFETIME_CLASS,RP_BANK_DUPLICATE};
    for(unsigned which=0;which<5;which++) {
        reset_state();rotation_reject_mode=1;
        TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
        test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
        current_agent=cohort_agents;current_movement=cohort_xml;
        TEST(call_role(0,current_agent)==tail_result && !rotation_failure);
        unsigned role=1;char cls[16];memcpy(cls,cohort_xml+0x20,16);uint64_t length=*(uint64_t*)(cohort_xml+0x30);
        if(which==0)role=0;
        if(which==1)current_movement+=0xc00;
        if(which==2)current_agent+=0x1100;
        if(which==3){memcpy(cohort_xml+0x20,"player",7);*(uint64_t*)(cohort_xml+0x30)=6;}
        if(which==4){*(unsigned*)(image+0x38f4ee4)=1;*(unsigned*)(image+0x2f4b6f4)=0;}
        uintptr_t result;
        if(which==4) {
            typedef uintptr_t (*entry_fn)(void *,void *,void *,void *);
            result=((entry_fn)(image+0xe870ef))(current_agent+0xae0,test_xml,current_agent,current_movement);
        }else result=call_role(role,current_agent);
        TEST(result==tail_result && original_tails==2 && rotation_failure==ROTATION_E_INPUT);
        TEST(rotation_reject_first.stage==RR_LIFETIME && rotation_reject_first.predicate==expected[which]);
        TEST(!rotation_reject_first.trace.records && !rotation_reject_first.accounted && rotation_reject_done.return_valid);
        TEST(rotation_lifetime_count==1 && !rotation_paired && rotation_written[0]==1 && !rotation_written[1]);
        memcpy(cohort_xml+0x20,cls,16);*(uint64_t*)(cohort_xml+0x30)=length;
        rotation_native_restore();
    }
}
static void rejection_stop_export(void) {
    reset_state();rotation_reject_mode=1;test_mode=7;
    test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_resume=CreateEventW(NULL,TRUE,FALSE,NULL);
    TEST(test_entered && test_resume);HANDLE worker=CreateThread(NULL,0,thread_load,NULL,0,NULL);TEST(worker);
    TEST(WaitForSingleObject(test_entered,5000)==WAIT_OBJECT_0);
    rotation_native_stop();rejection_index=0;TEST(lua_portable_rotation_rejection(NULL)==1 && !strcmp(lua_text,"nil"));
    TEST(rotation_scope_open==1 && rotation_load_open==1 && !rotation_reject_first.present);
    TEST(SetEvent(test_resume));TEST(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CloseHandle(worker);CloseHandle(test_entered);CloseHandle(test_resume);
    TEST(!rotation_scope_open && !rotation_load_open && rotation_reject_first.predicate==RP_STOPPED);
    TEST(lua_portable_rotation_rejection(NULL)==1 && strstr(lua_text,"present=1 readout_drained=1"));
    rotation_reject_entry entry=rotation_reject_first;rotation_reject_completion completion=rotation_reject_done;
    for(unsigned i=0;i<=entry.trace.records+1;i++) {
        rejection_index=i;TEST(lua_portable_rotation_rejection(NULL)==1 && strcmp(lua_text,"nil"));
    }
    const double invalid[]={-1,0.5,194,1e100};
    for(unsigned i=0;i<4;i++){rejection_index=invalid[i];TEST(lua_portable_rotation_rejection(NULL)==1 && !strcmp(lua_text,"nil"));}
    TEST(!memcmp(&entry,&rotation_reject_first,sizeof entry) && !memcmp(&completion,&rotation_reject_done,sizeof completion));
}
static void rejection_cases(void) {
    p_lua_tonumber=rejection_number;
    test_read_entry baseline[4096];unsigned baseline_count=0;
    char status_before[2048];unsigned baseline_failure=0,baseline_original[4]={0};
    for(unsigned scenario=0;scenario<10;scenario++) for(unsigned enabled=0;enabled<2;enabled++) {
        reset_state();rotation_reject_mode=(LONG)enabled;rotation_reject_boot=rotation_boot;
        unsigned char movement_saved[0xc00];memcpy(movement_saved,cohort_xml,sizeof movement_saved);
        switch(scenario) {
        case 1:*(unsigned*)(cohort_xml+12)=0;break;
        case 2:test_mode=6;break;
        case 3:test_mode=4;break;
        case 4:test_mode=2;break;
        case 5:test_mode=10;break;
        case 6:test_mode=3;break;
        case 7:test_failed_read=cohort_xml+12;break;
        case 8:*(unsigned*)(cohort_xml+12)=0;test_mode=2;break;
        case 9:test_failed_read=cohort_xml+12;test_partial_read=1;break;
        }
        test_read_count=0;test_read_enabled=1;int caught=0;
        __try {(void)run_load();} __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
        test_read_enabled=0;test_failed_read=NULL;test_partial_read=0;
        TEST(caught==(scenario==4 || scenario==8));
        if(!enabled) {
            baseline_count=test_read_count;memcpy(baseline,test_read_trace,baseline_count*sizeof(*baseline));
            baseline_failure=(unsigned)rotation_failure;memcpy(baseline_original,failure_at_original,sizeof baseline_original);
            lua_portable_rotation_status(NULL);strcpy(status_before,lua_text);
        }else {
            TEST(test_read_count==baseline_count);
            for(unsigned i=0;i<baseline_count;i++)TEST(test_read_trace[i].address==baseline[i].address &&
                test_read_trace[i].length==baseline[i].length && test_read_trace[i].ok==baseline[i].ok);
            TEST((unsigned)rotation_failure==baseline_failure && !memcmp(baseline_original,failure_at_original,sizeof baseline_original));
            lua_portable_rotation_status(NULL);TEST(!strcmp(status_before,lua_text));
            if(scenario) {
                TEST(rotation_reject_first.present && rotation_reject_done.present);
                TEST(rotation_reject_done.return_valid==(scenario!=4 && scenario!=8));
                TEST(rotation_reject_done.unwind==(scenario==4 || scenario==8));
                if(scenario==1 || scenario==7 || scenario==9)TEST(rotation_reject_first.stage==RR_CAPTURE && rotation_reject_first.accounted==0);
                if(scenario==2)TEST(rotation_reject_first.stage==RR_PRE_GUARD && rotation_reject_first.predicate==RP_BASIS);
                if(scenario==3)TEST(rotation_reject_first.stage==RR_REPLAY && rotation_reject_first.helper_result==X4RX_CHANGED);
                if(scenario==4)TEST(rotation_reject_first.stage==RR_ORIGINAL && rotation_reject_first.predicate==RP_UNWIND);
                if(scenario==5)TEST(rotation_reject_first.stage==RR_POST_GUARD && rotation_reject_first.predicate==RP_MANAGER_FLAG);
                if(scenario==6)TEST(rotation_reject_first.stage==RR_POST_GUARD && rotation_reject_first.predicate==RP_STOPPED);
                if(scenario==8)TEST(rotation_reject_first.stage==RR_CAPTURE && rotation_reject_first.helper_result==X4RX_SHAPE && !rotation_reject_first.primary_failure);
                if(scenario==7 || scenario==9) {
                    TEST(rotation_reject_first.trace.records==1 && rotation_reject_first.trace.bytes==0 && rotation_reject_first.trace.failed_reads==1);
                    TEST(!rotation_reject_first.trace.reads[0].ok && !rotation_reject_first.trace.reads[0].copied);
                }
                rotation_reject_entry saved=rotation_reject_first;rotation_reject_completion done=rotation_reject_done;
                char header[2048];TEST(!rotation_rejection_text(0,header,sizeof header) || rotation_stop);
                rotation_native_stop();TEST(rotation_rejection_text(0,header,sizeof header)>0 && strstr(header,"readout_drained=1"));
                unsigned reads=test_read_count;test_read_enabled=1;
                for(unsigned repeat=0;repeat<2;repeat++) {
                    for(unsigned row=0;row<=saved.trace.records+1;row++)TEST(rotation_rejection_text(row,header,sizeof header)>0);
                    TEST(!rotation_rejection_text(saved.trace.records+2,header,sizeof header));
                    TEST(!memcmp(&saved,&rotation_reject_first,sizeof saved) && !memcmp(&done,&rotation_reject_done,sizeof done));
                }
                test_read_enabled=0;TEST(test_read_count==reads);
                if(scenario==1 && getenv("X4ROT_TEST_REJECTION")) {
                    FILE *out=fopen(getenv("X4ROT_TEST_REJECTION"),"wb");TEST(out);
                    for(unsigned row=0;row<=saved.trace.records+1;row++) {
                        TEST(rotation_rejection_text(row,header,sizeof header)>0);fprintf(out,"%s\n",header);
                    }
                    TEST(!fclose(out));
                }
            }else TEST(!rotation_reject_first.present && !rotation_reject_done.present);
        }
        memcpy(cohort_xml,movement_saved,sizeof movement_saved);
    }
    /* The actual optional recorder never copies an unsuccessful read's output;
       each capacity limit marks incomplete without changing a read result. */
    rotation_reject_trace d;unsigned char bytes[513];memset(bytes,0xcc,sizeof bytes);
    rotation_reject_stage_begin(&d,RR_CAPTURE);rotation_reject_copied(&d,1,bytes,4,0);
    TEST(d.records==1 && d.bytes==0 && d.failed_reads==1 && !d.reads[0].copied);
    rotation_reject_copied(&d,2,bytes,513,1);TEST(d.truncated && d.reads[1].ok && !d.reads[1].copied && !d.bytes);
    rotation_reject_stage_begin(&d,RR_CAPTURE);
    for(unsigned i=0;i<17;i++)rotation_reject_copied(&d,3,bytes,512,1);
    TEST(d.records==17 && d.bytes==8192 && d.truncated && !d.reads[16].copied && d.reads[16].ok);
    rotation_reject_stage_begin(&d,RR_CAPTURE);
    for(unsigned i=0;i<193;i++)rotation_reject_copied(&d,4,bytes,1,1);
    TEST(d.records==192 && d.bytes==192 && d.attempted_reads==193 && d.dropped_records==1 && d.truncated);

    /* Real admission and lifetime helpers: exact mode remains fixed through
       healthy reattach; mismatched or late enable never resets a record. */
    reset_state();rotation_configured=0;boot_phase=0;rotation_reject_mode=-1;
    TEST(rotation_rejection_enable(1) && rotation_rejection_enable(1));
    TEST(!rotation_rejection_enable(0) && !rotation_rejection_enable(2));
    rotation_configured=1;boot_phase=1;TEST(rotation_rejection_enable(1));
    rotation_reject_first.present=1;rotation_reject_first.serial=17;
    TEST(rotation_rejection_enable(1) && rotation_reject_first.serial==17);
    rotation_failure=ROTATION_E_INPUT;TEST(!rotation_rejection_enable(1) && rotation_reject_first.serial==17);
    reset_state();rotation_reject_mode=-1;TEST(!rotation_rejection_enable(1));
    reset_state();rotation_reject_mode=0;rotation_reject_boot=rotation_boot;TEST(!rotation_rejection_enable(1));
    reset_state();rotation_reject_mode=1;rotation_reject_boot=rotation_boot;rotation_native_stop();TEST(!rotation_rejection_enable(1));
    rejection_lifetimes();rejection_stop_export();rejection_read_failures();rejection_reattach_reads();
}
