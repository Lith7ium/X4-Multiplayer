/* Mixed policy tests reuse owned synthetic Wait memory; no game process. */
static char test_policy_status[512];
static void test_policy_pushstring(lua_State *L,const char *text,size_t length) {
    (void)L;assert(length<sizeof(test_policy_status));memcpy(test_policy_status,text,length);test_policy_status[length]=0;
}
static void test_policy_pushnil(lua_State *L) {(void)L;assert(0);}
static uint64_t *test_policy_entity_id(void) {
    uintptr_t context=*(uintptr_t*)((unsigned char*)test_ww_value+8)-0xa8;
    return (uint64_t*)(*(uintptr_t*)(context+0x120)+8);
}
static unsigned char test_policy_nested(void *self,void *exec,void *value) {
    ++test_wwr_calls;uint64_t *rng=(uint64_t*)((unsigned char*)test_ww_tls+0x318);*rng=test_wwr_step(*rng);
    uint64_t id=*test_policy_entity_id(),time=*(uint64_t*)((unsigned char*)exec+0x28);
    *test_policy_entity_id()=0x999b;*(uint64_t*)((unsigned char*)exec+0x28)=world_wait_selected[2].time;
    assert(world_wait_observe_with_tls(test_wwr_original,self,exec,value,(uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls,NULL)==1);
    *test_policy_entity_id()=id;*(uint64_t*)((unsigned char*)exec+0x28)=time;return 1;
}
static void test_world_wait_policy(void) {
    world_wait_selection saved=world_wait_selected[0];unsigned saved_count=world_wait_selected_count;
    world_wait_rng_selection legacy[WORLD_WAIT_SELECTED],mixed[WORLD_WAIT_SELECTED];uint64_t snapshot[4];unsigned schema;
    const char *sha="7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8";
    char wire[512],old_wire[512];world_wait_selected[1]=world_wait_selected[2]=saved;
    world_wait_selected[1].saved=0x100;world_wait_selected[2].saved=0x101;
    assert(world_wait_rng_time("6.98333",&world_wait_selected[2].time)&&world_wait_selected[2].time==0x401beeee0f3cb3e6ULL);
    assert(world_identity_record(0x999a,0x100)&&world_identity_record(0x999b,0x101));
    world_wait_selected_count=2;
    snprintf(old_wire,sizeof(old_wire),"%s;1,0,17,0,6.91667;2,0,17,0,6.91667",sha);
    assert(world_wait_rng_parse(old_wire,strlen(old_wire),legacy,snapshot,&schema)&&schema==1);
    world_wait_selected_count=3;
    snprintf(wire,sizeof(wire),"WAIT_POLICY_2;%s;1,intervene,0,17,0,6.91667;2,intervene,0,17,0,6.91667;3,observe,6.98333",sha);
    assert(world_wait_rng_parse(wire,strlen(wire),mixed,snapshot,&schema)&&schema==2);
    assert(!memcmp(legacy,mixed,2*sizeof(*mixed))); /* Old key/guard bytes untouched. */
    assert(mixed[2].policy==WWR_OBSERVE&&!mixed[2].seed&&!mixed[2].slot&&!mixed[2].pc&&!mixed[2].since);
    const char *bad[]={";3,observe,0,17,0,6.98333",";3,other,6.98333",";2,observe,6.98333",";3,observe,6.98334",";3,observe,6.98333;4,observe,6.98333"};
    char changed[512];const char *tail=strstr(wire,";3,");assert(tail);
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        memcpy(changed,wire,(size_t)(tail-wire));strcpy(changed+(tail-wire),bad[i]);
        assert(!world_wait_rng_parse(changed,strlen(changed),mixed,snapshot,&schema));
    }
    assert(!world_wait_rng_parse(old_wire,strlen(old_wire),mixed,snapshot,&schema)); /* Missing natural selection. */
    assert(world_wait_rng_parse(wire,strlen(wire),mixed,snapshot,&schema));
    world_wait_rng_started=0;test_wwr_reset();boot_phase=0;world_wait_installed=1;
    assert(world_wait_rng_configure(wire,strlen(wire))&&world_wait_rng_schema==2);
    assert(world_wait_rng_configure(wire,strlen(wire))&&!world_wait_rng_configure(old_wire,strlen(old_wire)));
    test_wwr_reset();
    for(unsigned i=0;i<3;i++) {
        *test_policy_entity_id()=0x9999+i;*(uint64_t*)((unsigned char*)test_ww_exec+0x28)=world_wait_selected[i].time;
        uint64_t incoming=123+901*i;*(uint64_t*)((unsigned char*)test_ww_tls+0x318)=incoming;
        assert(world_wait_observe_with_tls(test_wwr_original,test_ww_self,test_ww_exec,test_ww_value,
            (uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls,i==2?NULL:test_wwr_current)==1);
        world_wait_event *e=&world_wait_events[i];assert(!e->errors&&e->selected==i+1&&e->rng_policy==(i==2?WWR_OBSERVE:WWR_INTERVENE));
        if(i<2) assert(e->rng_applied&&e->rng_restored&&e->rng_seed==legacy[i].seed&&e->rng_after==incoming);
        else assert(!e->rng_applied&&!e->rng_restored&&!e->rng_refusal&&!e->rng_seed&&!world_wait_rng_selected[i].used&&e->rng_after==test_wwr_step(incoming));
    }
    assert(test_wwr_calls==3&&world_wait_rng_applied==2&&world_wait_rng_observed==1&&!world_wait_rng_refused&&!world_wait_rng_open);
    world_wait_restore();world_wait_rng_exported=3;
    void (*old_string)(lua_State*,const char*,size_t)=p_lua_pushlstring;
    void (*old_nil)(lua_State*)=p_lua_pushnil;
    p_lua_pushlstring=test_policy_pushstring;p_lua_pushnil=test_policy_pushnil;
    assert(lua_world_wait_rng_status(NULL)==1);
    assert(!strcmp(test_policy_status,"schema=2 selected=3 rows=3 applied=2 refused=0 open=0 restore_open=0 closed=1 global_sync=0 exception_cleanup=0 observed=1"));
    p_lua_pushlstring=old_string;p_lua_pushnil=old_nil;
    const char *export_path=getenv("X4_TEST_WAIT_POLICY_OUT");
    if(export_path) {
        FILE *out=fopen(export_path,"wb");assert(out);char row[WORLD_WAIT_ROW_CAP];
        for(unsigned i=0;i<3;i++) {
            for(unsigned part=0;part<4;part++){assert(world_wait_format(&world_wait_events[i],i+1,part,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);}
            assert(world_wait_rng_format(&world_wait_events[i],i+1,row,sizeof(row))>0);fprintf(out,"NATIVE_GATE %s\n",row);
        }
        fputs("NATIVE_GATE WORLD_WAIT_STOP schema=1 capacity=128 selected=3 reserved=3 complete=3 rows=12 failures=0 dropped=0 calls=3 import_calls=3 foreign_calls=0 unknown=0 installed=0 restored=1 open=0 peak=1 restore_open=0 index_coverage=0\n",out);
        fprintf(out,"NATIVE_GATE WORLD_WAIT_RNG_STOP %s\n",test_policy_status);fclose(out);
    }
    /* Natural duplicate/STOP/changed-input paths never gain intervention effects. */
    for(unsigned mode=0;mode<3;mode++) {
        test_wwr_reset();test_wwr_mode=mode==1?2:0;
        *test_policy_entity_id()=0x999b;*(uint64_t*)((unsigned char*)test_ww_exec+0x28)=world_wait_selected[2].time;
        if(mode==2) ++world_wait_selected[2].version;
        for(unsigned call=0;call<(mode==0?2u:1u);call++) {
            uint64_t before=*(uint64_t*)((unsigned char*)test_ww_tls+0x318);
            assert(world_wait_observe_with_tls(test_wwr_original,test_ww_self,test_ww_exec,test_ww_value,(uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls,NULL)==1);
            assert(*(uint64_t*)((unsigned char*)test_ww_tls+0x318)==test_wwr_step(before));
        }
        if(mode==1) assert(world_wait_events[0].errors&128);
        if(mode==2) {assert(world_wait_events[0].errors&8);--world_wait_selected[2].version;}
        assert(!world_wait_rng_applied&&!world_wait_rng_refused&&!world_wait_rng_open&&!world_wait_rng_selected[2].used);
    }
    test_wwr_reset();*test_policy_entity_id()=0x9999;*(uint64_t*)((unsigned char*)test_ww_exec+0x28)=saved.time;
    assert(world_wait_observe_with_tls(test_policy_nested,test_ww_self,test_ww_exec,test_ww_value,(uintptr_t)(image+WORLD_WAIT_CALLER),test_ww_tls,test_wwr_current)==1);
    assert(test_wwr_calls==2&&(world_wait_events[0].rng_refusal&WWR_NESTED)&&world_wait_events[0].rng_restored);
    assert(world_wait_events[1].rng_policy==WWR_OBSERVE&&!world_wait_events[1].rng_applied&&!world_wait_events[1].rng_refusal);
    assert(world_wait_rng_applied==1&&world_wait_rng_observed==1&&!world_wait_rng_refused&&!world_wait_rng_selected[2].used);
    world_wait_event maximum;memset(&maximum,0xff,sizeof(maximum));char row[WORLD_WAIT_ROW_CAP];
    assert(world_wait_rng_format(&maximum,UINT32_MAX,row,sizeof(row))>0&&strlen(row)+32<2048);
    world_wait_selected[0]=saved;memset(&world_wait_selected[1],0,2*sizeof(saved));world_wait_selected_count=saved_count;
    puts("saved Wait policy2: unchanged intervention bytes, natural observation/duplicate/STOP, mixed original-once and seeded-parent nesting PASS");
}
