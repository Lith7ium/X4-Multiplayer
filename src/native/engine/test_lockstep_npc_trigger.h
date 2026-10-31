static void *trigger_test_node;
static void trigger_test_legacy_selected(void *exec) {
    assert(boot_options==140737488355328ULL);
    npc_exec_observe(test_npc_exec_original,exec,&test_rng);
}
static void test_npc_trigger_legacy_only(void *exec) {
    uint64_t options=boot_options;boot_options=140737488355328ULL;
    assert(!npc_trigger_count && !npc_cause_count && !npc_exec_count && !npc_exec_calls && !interrupt_failures);
    test_calls=0;test_rng=123;
    for(unsigned full=0;full<2;full++) {
        npc_trigger_count=full?NPC_TRIGGER_LIMIT:0;
        npc_exec_entry(trigger_test_legacy_selected,exec,&test_rng,0x111c821);
        npc_exec_sample *record=&npc_exec_samples[full];
        assert(test_calls==full+1 && test_rng==123 && npc_exec_calls==full+1 && npc_exec_count==full+1);
        assert(record->call==full+1 && record->before==123 && record->scoped==test_npc_draw &&
               record->after==test_npc_draw+11 && record->restored==123);
        assert(npc_trigger_count==(full?NPC_TRIGGER_LIMIT:0) && !npc_cause_count &&
               !npc_trigger_calls && !npc_cause_calls && !npc_trigger_seen &&
               !npc_trigger_unsupported && !npc_trigger_foreign_thread && !interrupt_failures);
        assert(!npc_trigger_current && !npc_trigger_action_current && !npc_trigger_foreign_depth && !npc_exec_current);
    }
    npc_trigger_count=0;npc_exec_count=0;npc_exec_calls=0;memset(npc_exec_keys,0,sizeof(npc_exec_keys));
    /* The same entry router still enables read-only observation with bit48. */
    boot_options=281474976710656ULL;npc_exec_entry(test_npc_exec_original,exec,&test_rng,0x111c821);
    assert(test_calls==3 && test_rng==134 && npc_trigger_count==1 && npc_trigger_calls==1 &&
           npc_trigger_seen==1 && npc_trigger_unsupported==1 && npc_triggers[0].status==NPC_TRIGGER_AMBIENT && !interrupt_failures);
    npc_trigger_count=0;npc_trigger_calls=npc_trigger_seen=npc_trigger_unsupported=0;boot_options=options;
}
static void trigger_test_children(void *exec) {
    unsigned char child[0x50];memcpy(child,exec,sizeof(child));*(uint64_t*)(child+0x18)=0;
    *(uint64_t*)((unsigned char*)exec+0x18)=1;
    for(unsigned visit=0;visit<2;visit++) {
        npc_trigger_action_context action;npc_trigger_action_begin(&action,trigger_test_node,exec);
        assert(action.valid && action.visit==visit+1 && !action.children);
        for(unsigned child_index=0;child_index<2;child_index++)
            npc_trigger_observe(test_npc_exec_original,child,&test_rng,0x111c821);
        assert(action.children==2);npc_trigger_action_end(&action);
    }
    /* A foreign direct dispatch cannot inherit the parent named executor. */
    npc_trigger_action_context foreign;npc_trigger_action_begin(&foreign,trigger_test_node,child);
    assert(!foreign.valid);npc_trigger_observe(test_npc_exec_original,child,&test_rng,0x111c821);
    npc_trigger_action_end(&foreign);
    *(uint64_t*)((unsigned char*)exec+0x18)=0;
}
static void trigger_test_foreign_body(void *exec) {
    unsigned char child[0x50];memcpy(child,exec,sizeof(child));*(uint32_t*)(child+0x38)=0x5d;
    npc_trigger_observe(test_npc_exec_original,child,&test_rng,0x111c821);
}
static void trigger_test_reentry(void *exec) {
    npc_trigger_sample *parent=npc_trigger_current;assert(parent);
    interrupt_sample *saved_irq=interrupt_current;interrupt_step_sample *saved_step=interrupt_step_active;
    interrupt_sample irq={0};irq.actor=6;irq.tick=stats.issued;irq.ordinal=999;irq.kind=1;irq.end=3;
    strcpy(irq.script,"fight.attack.object.fighter");npc_trigger_irq_begin(&irq);
    interrupt_step_sample step={0};step.tick=stats.issued;step.parent=999;step.ordinal=1000;step.actor=6;
    step.pc=2;step.type=0x2c0de40;step.action=0xbc39c0;step.trigger_visit=1;
    interrupt_current=&irq;interrupt_step_active=&step;
    npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
    assert(npc_trigger_current==parent && npc_triggers[npc_trigger_count-1].actor==6 && !npc_triggers[npc_trigger_count-1].parent_key);
    interrupt_current=saved_irq;interrupt_step_active=saved_step;
}
static volatile LONG trigger_thread_calls;
static void trigger_test_thread_original(void *exec) {
    assert(exec && !npc_trigger_current && !npc_trigger_action_current && !npc_trigger_foreign_depth);InterlockedIncrement(&trigger_thread_calls);
}
static DWORD WINAPI trigger_test_thread(LPVOID exec) {
    npc_trigger_observe(trigger_test_thread_original,exec,NULL,0x111c821);return 0;
}
static void test_npc_trigger_identity(void *exec,void *node) {
    uint64_t options=boot_options;boot_options=281474976710656ULL;
    interrupt_sample irq={0};irq.actor=5;irq.tick=stats.issued;irq.ordinal=172;irq.kind=1;
    irq.pc=23;irq.end=3;strcpy(irq.script,"fight.attack.object.fighter");
    interrupt_step_sample step={0};step.tick=stats.issued;step.parent=172;step.ordinal=908;step.actor=5;
    step.pc=2;step.type=0x2c0de40;step.action=0xbc39c0;step.trigger_visit=1;
    uint64_t first_key=0;test_rng=900;test_calls=0;
    for(unsigned permutation=0;permutation<3;permutation++) {
        memset(npc_cause_keys,0,sizeof(npc_cause_keys));step.trigger_children=0;
        interrupt_current=NULL;interrupt_step_active=NULL;
        if(permutation==1) npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
        npc_trigger_irq_begin(&irq);interrupt_current=&irq;interrupt_step_active=&step;
        unsigned before=npc_trigger_count;uint64_t rng_before=test_rng;
        npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
        npc_trigger_sample *record=&npc_triggers[before];
        assert(record->status==NPC_TRIGGER_CANDIDATE && record->key && record->child==1 && record->before==rng_before && record->after==rng_before+11);
        if(!permutation) first_key=record->key;else assert(first_key==record->key);
        interrupt_current=NULL;interrupt_step_active=NULL;
        if(permutation==2) npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
        assert(!npc_trigger_current && !npc_trigger_action_current && !npc_trigger_foreign_depth);
    }
    assert(test_rng==955 && test_calls==5 && npc_trigger_unsupported==2);
    /* Repeated identical causes are NOT asserted to have semantic identity. */
    npc_trigger_irq_begin(&irq);assert(irq.trigger_status==NPC_TRIGGER_AMBIGUOUS && irq.trigger_occurrence==2);
    interrupt_current=&irq;interrupt_step_active=&step;
    npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
    assert(npc_triggers[npc_trigger_count-1].status==NPC_TRIGGER_AMBIGUOUS && !npc_triggers[npc_trigger_count-1].key);
    memset(npc_cause_keys,0,sizeof(npc_cause_keys));irq.values[0]=123;npc_trigger_irq_begin(&irq);
    assert(irq.trigger_status==NPC_TRIGGER_PARAMETER);irq.values[0]=0;
    memset(npc_cause_keys,0,sizeof(npc_cause_keys));npc_trigger_irq_begin(&irq);step.trigger_children=0;
    *(void**)(image+0x1018)=image+0x2000;trigger_test_node=node;
    unsigned root=npc_trigger_count;npc_trigger_observe(trigger_test_children,exec,&test_rng,0x111c821);
    assert(npc_trigger_count==root+6 && npc_triggers[root].status==NPC_TRIGGER_CANDIDATE);
    for(unsigned i=1;i<=4;i++) {
        npc_trigger_sample *child=&npc_triggers[root+i];
        assert(child->status==NPC_TRIGGER_CANDIDATE && child->parent_key==npc_triggers[root].key && child->depth==2);
        assert(child->visit==(i-1)/2+1 && child->child==(i-1)%2+1);
        for(unsigned j=1;j<i;j++) assert(child->key!=npc_triggers[root+j].key);
    }
    assert(npc_triggers[root+5].status==NPC_TRIGGER_FOREIGN && !npc_triggers[root+5].key);
    unsigned char foreign_exec[0x50];memcpy(foreign_exec,exec,sizeof(foreign_exec));*(uint32_t*)(foreign_exec+0x38)=0x66;
    npc_trigger_observe(trigger_test_foreign_body,foreign_exec,&test_rng,0x111c821);
    assert(npc_triggers[npc_trigger_count-1].status==NPC_TRIGGER_FOREIGN && !npc_trigger_foreign_depth);
    assert(!npc_trigger_current && !npc_trigger_action_current && !interrupt_failures);
    npc_trigger_observe(trigger_test_reentry,exec,&test_rng,0x111c821);
    assert(!npc_trigger_current && !npc_trigger_action_current && interrupt_current==&irq && interrupt_step_active==&step);
    npc_trigger_current=&npc_triggers[0];
    HANDLE thread=CreateThread(NULL,0,trigger_test_thread,exec,0,NULL);assert(thread);
    assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    assert(npc_trigger_current==&npc_triggers[0] && trigger_thread_calls==1 && npc_trigger_foreign_thread==1);npc_trigger_current=NULL;
    unsigned calls_before=test_calls;npc_trigger_count=NPC_TRIGGER_LIMIT;
    npc_trigger_observe(test_npc_exec_original,exec,&test_rng,0x111c821);
    assert(test_calls==calls_before+1 && interrupt_failures==1 && !npc_trigger_foreign_depth);
    npc_trigger_count=0;npc_cause_count=0;npc_trigger_calls=npc_cause_calls=0;
    npc_trigger_seen=npc_trigger_unsupported=npc_trigger_foreign_thread=0;interrupt_failures=0;
    memset(npc_cause_keys,0,sizeof(npc_cause_keys));interrupt_current=NULL;interrupt_step_active=NULL;boot_options=options;
}
