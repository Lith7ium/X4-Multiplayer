/* World identities and world hash of the Linux gate (gate_world.h), included by
 * test_gate_linux.c. Owned memory and our own code only; no X4 code runs.
 *  - The import hook on a stand-in insert function at the insert VA of the fake image
 *    (our bytes, the same first instructions as the pinned original): install bytes,
 *    trampoline, map filter, boot phase window, seal, refusals, restore. A stand-in map
 *    clear frees the nodes as the real one does: the live failure of the first native
 *    run (reading the nodes at the first live frame) must stay impossible.
 *  - The synthetic world of test_lockstep_world_scenario.h in the Linux layout must print
 *    exactly the text the Windows world hash printed for it (test_gate_world_golden.txt,
 *    from test_lockstep_world_golden.c on Windows with the pinned X4.exe). */
#include "test_lockstep_world_scenario.h"

static char wt_exported[16384];static double wt_argument,wt_result;
static double wt_number_arg(lua_State *L,int index) {(void)L;(void)index;return wt_argument;}
static void wt_number_result(lua_State *L,double n) {(void)L;wt_result=n;}
static void wt_nil_result(lua_State *L) {(void)L;strcpy(wt_exported,"nil");}
static void wt_text_result(lua_State *L,const char *s,size_t n) {(void)L;assert(n<sizeof(wt_exported));memcpy(wt_exported,s,n);wt_exported[n]=0;}
static void wt_lua(void) {
    p_lua_tonumber=wt_number_arg;p_lua_pushnumber=wt_number_result;p_lua_pushnil=wt_nil_result;p_lua_pushlstring=wt_text_result;
    ui_thread=GetCurrentThreadId();
}

/* ---- the import hook ---- */
static __attribute__((aligned(16))) unsigned char wt_nodes[16][0x30];
/* Stand-in map insert: push r14; push r13; push r12; mov r12,rsi (the pinned original's
 * first 9 bytes); node = nodes + (key & 15) * 0x30; node->key = key; return {node, 1}. */
static void wt_standin(unsigned char *at) {
    unsigned char code[64];unsigned n=0;
    const unsigned char head[]={0x41,0x56,0x41,0x55,0x41,0x54,0x49,0x89,0xf4};
    memcpy(code,head,sizeof(head));n=sizeof(head);
    const unsigned char key[]={0x49,0x8b,0x04,0x24,0x48,0x83,0xe0,0x0f,0x48,0x6b,0xc0,0x30,0x48,0xb9};
    memcpy(code+n,key,sizeof(key));n+=sizeof(key);
    uint64_t base=(uint64_t)(uintptr_t)wt_nodes;memcpy(code+n,&base,8);n+=8;
    const unsigned char tail[]={0x48,0x01,0xc8,0x49,0x8b,0x14,0x24,0x48,0x89,0x50,0x20,0xba,0x01,0x00,0x00,0x00,
                                0x41,0x5c,0x41,0x5d,0x41,0x5e,0xc3};
    memcpy(code+n,tail,sizeof(tail));n+=sizeof(tail);
    uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,2*GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    memcpy(at,code,n);
    assert(!mprotect((void*)page,2*GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(rd64(at)==LX_IMPORT_ENTRY_ORIGINAL);
}
/* Stand-in map clear: the pinned original's first 9 bytes (push rbx; sub rsp,0x40;
 * mov rdx,[rdi+8]), then a call of wt_cleared(map), which scribbles over the nodes of the
 * import map as the freed blocks are reused. */
static unsigned wt_clears,wt_clears_import;
static void wt_cleared(void *map) {
    ++wt_clears;
    if(map==(void*)LX(LX_IMPORT_MAP_VA)) {++wt_clears_import;memset(wt_nodes,0xa5,sizeof(wt_nodes));}
}
static void wt_clear_standin(unsigned char *at) {
    const unsigned char head[]={0x53,0x48,0x83,0xec,0x40,0x48,0x8b,0x57,0x08,0x48,0xb8};
    const unsigned char tail[]={0xff,0xd0,0x48,0x83,0xc4,0x40,0x5b,0xc3}; /* call rax; add rsp,0x40; pop rbx; ret */
    uint64_t callee=(uint64_t)(uintptr_t)wt_cleared;
    uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    assert(!mprotect((void*)page,2*GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    memcpy(at,head,sizeof(head));memcpy(at+sizeof(head),&callee,8);memcpy(at+sizeof(head)+8,tail,sizeof(tail));
    assert(!mprotect((void*)page,2*GATE_PAGE,PROT_READ|PROT_EXEC));
    assert(rd64(at)==LX_IMPORT_CLEAR_ORIGINAL);
}
static void wt_clear(void *map) {((lxw_clear_fn)(void*)LX(LX_IMPORT_CLEAR_VA))(map);}
static void wt_flip(unsigned char *entry) {
    unsigned char *page=(unsigned char*)((uintptr_t)entry&~(uintptr_t)(GATE_PAGE-1));
    assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));entry[7]^=1;
    assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_EXEC));
}
static lxw_insert_result wt_insert(void *map,uint64_t key) {
    return ((lxw_insert_fn)(void*)LX(LX_IMPORT_INSERT_VA))(map,&key);
}
static void wt_reset_identities(void) {
    if(world_identities) memset(world_identities,0,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY);
    world_identity_count=world_identity_calls=world_identity_failures=world_identity_late=0;
    world_identity_sealed=0;lxw_insert_count=0;world_identity_restored=0;
}
static void test_world_hook(void) {
    reset_session();wt_reset_identities();
    unsigned char *entry=LX(LX_IMPORT_INSERT_VA),*clear=LX(LX_IMPORT_CLEAR_VA);wt_standin(entry);wt_clear_standin(clear);
    thunk=make_thunk();assert(thunk);
    /* Refused without the original entry or a thunk; nothing patched. */
    unsigned char *saved_thunk=thunk;thunk=NULL;assert(!world_identity_install() && rd64(entry)==LX_IMPORT_ENTRY_ORIGINAL);thunk=saved_thunk;
    assert(world_identity_install() && world_identity_installed);
    uint64_t word=rd64(entry);int32_t rel;memcpy(&rel,entry+1,4);
    assert(entry[0]==0xe9 && entry+5+rel==thunk+LXW_THUNK_HOOK && !memcmp(entry+5,"\x54\x49\x89",3));
    memcpy(&rel,clear+1,4);
    assert(clear[0]==0xe9 && clear+5+rel==thunk+LXW_THUNK_CLEAR_HOOK && !memcmp(clear+5,"\x48\x8b\x57",3));
    assert(!world_identity_install()); /* once */
    assert(world_identity_owned());
    /* Outside the load window: counted late, not recorded; other maps pass through untouched. */
    unsigned char other_map[16];
    lxw_insert_result r=wt_insert(LX(LX_IMPORT_MAP_VA),0x51);
    assert(r.node==wt_nodes[1] && r.inserted==1 && world_identity_late==1 && !lxw_insert_count);
    boot_mode=1;boot_phase=1;
    r=wt_insert(other_map,0x52);assert(r.node==wt_nodes[2] && !lxw_insert_count && world_identity_late==1);
    /* During the load: recorded; the caller stores the runtime ID in the node. */
    const uint64_t keys[3]={0x101,0x102,0x103},runtimes[3]={7001,7002,7003};
    for(unsigned i=0;i<3;i++) {
        r=wt_insert(LX(LX_IMPORT_MAP_VA),keys[i]);assert(r.node==wt_nodes[1+i] && rd64(r.node+0x20)==keys[i]);
        memcpy(r.node+0x28,&runtimes[i],8);
    }
    boot_phase=2;r=wt_insert(LX(LX_IMPORT_MAP_VA),keys[0]); /* a repeated import of the same ID */
    assert(world_identity_calls==4 && lxw_insert_count==4 && !world_identity_failures);
    assert(!world_identity_lookup(7001)); /* nothing before the seal */
    boot_phase=3;world_identity_seal();
    assert(world_identity_sealed && world_identity_count==3 && !world_identity_failures);
    for(unsigned i=0;i<3;i++) assert(world_identity_lookup(runtimes[i])==keys[i]);
    assert(!world_identity_record(7004,0x104)); /* sealed */
    r=wt_insert(LX(LX_IMPORT_MAP_VA),0x105);assert(world_identity_late==2 && !lxw_insert_count);
    /* The load clears the map, which frees the nodes, long before the first live frame. Other
     * maps' clears read nothing; the import map's clear reads every node first. */
    wt_reset_identities();boot_phase=1;wt_clears=wt_clears_import=0;
    for(unsigned i=0;i<3;i++) {r=wt_insert(LX(LX_IMPORT_MAP_VA),keys[i]);memcpy(r.node+0x28,&runtimes[i],8);}
    wt_clear(other_map);assert(wt_clears==1 && !wt_clears_import && lxw_insert_count==3 && !world_identity_count);
    wt_clear(LX(LX_IMPORT_MAP_VA));
    assert(wt_clears==2 && wt_clears_import==1 && !lxw_insert_count && world_identity_count==3 && !world_identity_failures);
    assert(rd64(wt_nodes[1]+0x28)==0xa5a5a5a5a5a5a5a5ull); /* really gone */
    wt_clear(LX(LX_IMPORT_MAP_VA));assert(world_identity_count==3 && !world_identity_failures); /* twice: nothing to read */
    /* An import after a clear (the first clear of the load runs before its import). */
    r=wt_insert(LX(LX_IMPORT_MAP_VA),0x10a);{uint64_t runtime=7005;memcpy(r.node+0x28,&runtime,8);}
    assert(lxw_insert_count==1);
    boot_phase=3;world_identity_seal();
    assert(world_identity_sealed && world_identity_count==4 && !world_identity_failures && !lxw_insert_count);
    for(unsigned i=0;i<3;i++) assert(world_identity_lookup(runtimes[i])==keys[i]);
    assert(world_identity_lookup(7005)==0x10a);
    /* A clear after the seal: nothing recorded, nothing read. */
    wt_clear(LX(LX_IMPORT_MAP_VA));assert(world_identity_count==4 && !world_identity_failures);
    /* A node without a runtime ID fails the seal (stop 72). */
    wt_reset_identities();boot_phase=1;r=wt_insert(LX(LX_IMPORT_MAP_VA),0x106);memset(r.node+0x28,0,8);
    boot_phase=3;world_identity_seal();assert(world_identity_failures==1);
    /* Two saved IDs for one runtime ID fail as on Windows. */
    wt_reset_identities();boot_phase=1;
    r=wt_insert(LX(LX_IMPORT_MAP_VA),0x107);memcpy(r.node+0x28,&runtimes[0],8);
    r=wt_insert(LX(LX_IMPORT_MAP_VA),0x108);memcpy(r.node+0x28,&runtimes[0],8);
    boot_phase=3;world_identity_seal();assert(world_identity_failures==1 && world_identity_count==1);
    /* A foreign owner of the entry is never overwritten; the own patch is restored exactly. */
    wt_flip(entry);
    assert(!world_identity_owned());world_identity_restore();assert(!world_identity_restored && world_identity_installed);
    assert(rd64(clear)==LX_IMPORT_CLEAR_ORIGINAL); /* the other one is back already */
    wt_flip(entry);
    world_identity_restore();assert(world_identity_restored && !world_identity_installed && rd64(entry)==LX_IMPORT_ENTRY_ORIGINAL && word!=rd64(entry));
    /* The same for a foreign owner of the clear entry. */
    assert(world_identity_install());word=rd64(clear);wt_flip(clear);
    assert(!world_identity_owned());world_identity_restore();assert(!world_identity_restored && world_identity_installed);
    assert(rd64(entry)==LX_IMPORT_ENTRY_ORIGINAL && rd64(clear)!=word);
    wt_flip(clear);world_identity_restore();assert(world_identity_restored && !world_identity_installed && rd64(clear)==LX_IMPORT_CLEAR_ORIGINAL);
    /* Refused, nothing patched, when either entry is not the original. */
    wt_flip(clear);assert(!world_identity_install() && rd64(entry)==LX_IMPORT_ENTRY_ORIGINAL);wt_flip(clear);
    wt_flip(entry);assert(!world_identity_install() && rd64(clear)==LX_IMPORT_CLEAR_ORIGINAL);wt_flip(entry);
    /* After the restore the original runs directly; nothing is recorded. */
    wt_reset_identities();boot_phase=1;r=wt_insert(LX(LX_IMPORT_MAP_VA),0x109);assert(r.node==wt_nodes[9] && !world_identity_calls);
    boot_mode=0;boot_phase=0;wt_reset_identities();
    printf("world identity hook PASS\n");
}

/* ---- the golden world ---- */
static void *wt_map_slot;
static void *wt_find(void *map,uint64_t runtime,unsigned mode,unsigned zero) {
    assert(map==(void*)wscen_agent && mode==4 && !zero);return wscen_find(runtime);
}
static char wt_text[262144];static size_t wt_used;
static void wt_emit(const char *s) {size_t n=strlen(s);assert(wt_used+n+1<sizeof(wt_text));memcpy(wt_text+wt_used,s,n);wt_used+=n;wt_text[wt_used]=0;}
static void wt_run(unsigned schema,const wscen_layout *layout) {
    char line[1024];
    wscen_build(layout);wt_reset_identities();
    for(unsigned i=0;i<WSCEN_IDENTITIES;i++) assert(world_identity_record(wscen_saved_runtime[i],wscen_saved[i]));
    world_identity_sealed=1;
    free(world_hash_items);free(world_hash_changed);world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;world_hash_tick=0;
    world_hash_scans=world_hash_failures=0;world_hash_watch_count=0;world_hash_schema=1;
    wt_map_slot=wscen_agent;world_component_map=&wt_map_slot;world_find=wt_find;
    assert(world_hash_watch_add("000000000000004d",16) && world_hash_watch_add("000000000000004f",16));
    for(unsigned tick=1;tick<=3;tick++) {
        wscen_step(tick);
        if(!world_hash_capture(tick,schema)) {printf("capture failed tick=%u stage=%s saved=%llx\n",tick,world_hash_stage,(unsigned long long)world_hash_failure_id);assert(0);}
        snprintf(line,sizeof(line),"WORLD_HASH %.900s\n",world_hash_summary);wt_emit(line);
        assert(lua_world_banks(NULL)==1 && strlen(wt_exported)<sizeof(line)-16);snprintf(line,sizeof(line),"WORLD_BANKS %.900s\n",wt_exported);wt_emit(line);
        wt_argument=0;assert(lua_world_page(NULL)==1);unsigned pages=(unsigned)wt_result;
        for(unsigned page=1;page<=pages;page++) {wt_argument=page;assert(lua_world_page(NULL)==1 && strcmp(wt_exported,"nil"));wt_emit(wt_exported);}
        wt_argument=0;assert(lua_world_detail(NULL)==1);unsigned rows=(unsigned)wt_result;assert(rows==2*WORLD_HASH_DOMAINS);
        for(unsigned row=1;row<=rows;row++) {wt_argument=row;assert(lua_world_detail(NULL)==1 && strcmp(wt_exported,"nil"));wt_emit("WORLD_DETAIL ");wt_emit(wt_exported);wt_emit("\n");}
    }
}
static const wscen_layout *wt_linux_layout(void) {
    static wscen_layout l;
    l=(wscen_layout){
        .obj_runtime=LXW_OBJ_RUNTIME,.obj_mode=LXW_OBJ_MODE,.obj_parent=LXW_OBJ_PARENT,.obj_state=LXW_OBJ_STATE,.obj_attention=LXW_OBJ_ATTENTION,
        .obj_controller=LXW_OBJ_CONTROLLER,.obj_hull=LXW_OBJ_HULL,.obj_shield=LXW_OBJ_SHIELD,
        .sg_begin=LXW_SG_BEGIN,.sg_end=LXW_SG_END,.sg_rate=LXW_SG_RATE,.sg_maximum=LXW_SG_MAXIMUM,.sg_delay=LXW_SG_DELAY,.sg_factor=LXW_SG_FACTOR,
        .sg_value=LXW_SG_VALUE,.sg_changed=LXW_SG_CHANGED,.sg_source_vtable=LXW_SG_SOURCE_VTABLE,.sg_source_group=LXW_SG_SOURCE_GROUP,.sg_modifiers=LXW_SG_MODIFIERS,
        .static_transform=LXW_TRANSFORM,.movement_transform=LXW_TRANSFORM,.controller_interface=0x58,
        .dock_poses=LXW_DOCK_POSES,.dock_time=LXW_DOCK_TIME,.dock_owner=LXW_DOCK_OWNER,.dock_more=LXW_DOCK_MORE,.dock_last=LXW_DOCK_LAST,
        .dock_animation=LXW_DOCK_ANIMATION,.dock_flags=LXW_DOCK_FLAGS,.undock_animation=LXW_UNDOCK_ANIMATION,.undock_flag=LXW_UNDOCK_FLAG,
        .nav_owner=LXW_NAV_OWNER,.nav_slots=LXW_NAV_SLOTS,.nav_params=LXW_NAV_PARAMS,.nav_agent=LXW_NAV_AGENT,.nav_commands=LXW_NAV_COMMANDS,
        .nav_pending=LXW_NAV_PENDING,.nav_time=LXW_NAV_TIME,.nav_deadline=LXW_NAV_DEADLINE,
        .agent_commands=LXW_AGENT_COMMANDS,.agent_slots=LXW_AGENT_SLOTS,.agent_params=LXW_AGENT_PARAMS,.agent_pending=LXW_AGENT_PENDING,.agent_owner=LXW_AGENT_OWNER,
        /* Linux vtables of U::ShipSmall, Turret, Zone, BuildProcessor and the controllers' interface
         * subobjects (gate_world_classes.h). */
        .ship_vtable=LX(0x3d21748),.turret_vtable=LX(0x3d38dd8),.zone_vtable=LX(0x3c6ca30),.processor_vtable=LX(0x3b37268),
        .nav_vtable=LX(0x3b697e8),.static_vtable=LX(0x3b00110),.movement_vtable=LX(0x3b0d600),.dock_vtable=LX(0x3b8c1a8),
        .undock_vtable=LX(0x3b4bcb8),.other_vtable=LX(0x3b540f8),.shield_source_vtable=LX(LX_SHIELD_SOURCE_VTABLE),
        .bank_read=(uint32_t*)LX(LX_BANK_READ_VA),.bank_write=(uint32_t*)LX(LX_BANK_WRITE_VA)};
    return &l;
}
static void test_world_golden(const char *golden) {
    reset_session();wt_lua();
    if(!world_identities) world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));
    assert(world_identities);wt_used=0;
    const wscen_layout *linux_layout=wt_linux_layout();
    /* The class table pairs these vtables with the Windows ones the golden world used. */
    const struct {void *vtable;unsigned windows,family;int top;} expect[]={
        {linux_layout->ship_vtable,0x2b7feb8,0,0},{linux_layout->turret_vtable,0x2b6e910,0,0},{linux_layout->zone_vtable,0x2b66080,0,0},
        {linux_layout->processor_vtable,0x2aaa9d8,0,0},{linux_layout->nav_vtable,0x2c51158,LXW_FAMILY_NAV,0},
        {linux_layout->static_vtable,0x2a34b38,LXW_FAMILY_STATIC,0},{linux_layout->movement_vtable,0x2abd888,LXW_FAMILY_MOVEMENT,-0x58},
        {linux_layout->dock_vtable,0x2c3fdc0,LXW_FAMILY_DOCK,-0x58},{linux_layout->undock_vtable,0x2c3e568,LXW_FAMILY_UNDOCK,-0x58},
        {linux_layout->other_vtable,0x2b17c08,LXW_FAMILY_OTHER,-0x58}};
    for(unsigned i=0;i<sizeof(expect)/sizeof(*expect);i++) {
        const lxw_class *c=lxw_class_of((uintptr_t)expect[i].vtable);
        assert(c && c->windows_rva==expect[i].windows && c->family==expect[i].family && c->top==expect[i].top);
    }
    /* The controllers every Windows census shows without state: found through their interface vtables. */
    const struct {uint32_t linux_vtable;unsigned windows;} census[]={{0x3b8e878,0x2c40b40},{0x3b6c440,0x2c406b8},{0x3affe50,0x2c3f5f0},
        {0x3b37030,0x2ad0c20},{0x3b5edf8,0x2c3eec0},{0x3b7c208,0x2c39488}};
    for(unsigned i=0;i<sizeof(census)/sizeof(*census);i++) {
        const lxw_class *c=lxw_class_of((uintptr_t)LX(census[i].linux_vtable));
        assert(c && c->windows_rva==census[i].windows && c->family==LXW_FAMILY_OTHER);
    }
    /* A controller's primary vtable is not what an object points to. */
    assert(!lxw_class_of((uintptr_t)LX(0x3b8e6e8)) && !lxw_class_of((uintptr_t)LX(0x3b0d470)));
    assert(!lxw_class_of((uintptr_t)LX(0x3d21750)) && !lxw_class_of(0));
    wt_emit("# WORLD_IMPORTED_2\n");wt_run(2,linux_layout);
    wt_emit("# WORLD_IMPORTED_1\n");wt_run(1,linux_layout);
    if(!golden) {printf("world golden SKIPPED (GATE_WORLD_GOLDEN unset)\n");return;}
    FILE *f=fopen(golden,"rb");assert(f);
    static char expected[262144];size_t n=fread(expected,1,sizeof(expected)-1,f);fclose(f);
    /* A Windows checkout (core.autocrlf) turns the file's line ends into CRLF. */
    {size_t kept=0;for(size_t i=0;i<n;i++) if(expected[i]!='\r') expected[kept++]=expected[i];n=kept;}
    expected[n]=0;
    if(n!=wt_used || memcmp(expected,wt_text,n)) {
        size_t at=0;while(at<n && at<wt_used && expected[at]==wt_text[at]) ++at;
        printf("world golden MISMATCH at byte %zu\n--- windows\n%.300s\n--- linux\n%.300s\n",at,expected+(at>200?at-200:0),wt_text+(at>200?at-200:0));
        assert(0);
    }
    /* Refusals: an unmapped object or controller class fails the capture closed. */
    wscen_build(linux_layout);memcpy(wscen_object[5],&(void*){LX(0x3d21750)},8);
    world_hash_failures=0;world_hash_scans=0;world_hash_tick=0;
    assert(!world_hash_capture(1,2) && !strcmp(world_hash_stage,"object_class_unmapped"));
    wscen_build(linux_layout);memcpy(wscen_controller[4]+0x58,&(void*){LX(0x3d21750)},8);
    assert(!world_hash_capture(1,2) && !strcmp(world_hash_stage,"controller_class_unmapped"));
    /* One capture names every failure at once: it reads all objects and publishes nothing. */
    wscen_build(linux_layout);
    memcpy(wscen_object[5],&(void*){LX(0x3d21750)},8);                  /* saved 82: an unmapped object class */
    memcpy(wscen_controller[4]+0x58,&(void*){LX(0x3b0d470)},8);         /* saved 81: a controller's primary vtable */
    memcpy(wscen_controller[2]+LXW_DOCK_OWNER,&(void*){wscen_object[6]},8); /* saved 79: a dock link that does not hold */
    world_hash_failures=0;world_hash_scans=0;world_hash_tick=0;
    assert(!world_hash_capture(1,2));
    assert(lxw_capture_problem_count==3 && lxw_capture_failed_objects==3 && !lxw_capture_problems_dropped);
    assert(world_hash_failure_id==79 && !strcmp(world_hash_stage,"dock_controller"));
    assert(!strcmp(lxw_capture_problems[1].stage,"controller_class_unmapped") && lxw_capture_problems[1].first_saved==81 && lxw_capture_problems[1].count==1);
    assert(!strcmp(lxw_capture_problems[2].stage,"object_class_unmapped") && lxw_capture_problems[2].first_saved==82);
    static char problems[8000];lxw_capture_problems_text(problems,sizeof(problems));
    assert(strstr(problems," objects=3 distinct=3\n[X4LAN] NATIVE_GATE WORLD_CAPTURE_PROBLEM stage=dock_controller count=1 first_saved=000000000000004f vtable=3b8c1a8 "));
    assert(strstr(problems,"stage=controller_class_unmapped count=1 first_saved=0000000000000051 vtable=3b0d470 "));
    assert(strstr(problems,"stage=object_class_unmapped count=1 first_saved=0000000000000052 vtable=3d21750 "));
    /* A row names its class from the RTTI (Itanium layout, in owned memory here). */
    static const char rtti_name[]="N1U4TestE";
    const void *rtti_info[2]={NULL,rtti_name};
    const void *rtti_vtable[4]={(void*)(intptr_t)-88,rtti_info,NULL,NULL};
    lxw_capture_problem_count=0;lxw_capture_failed_objects=lxw_capture_problems_dropped=0;world_hash_stage="controller_class_unmapped";
    lxw_capture_vtable=(uintptr_t)&rtti_vtable[2];lxw_capture_problem_note(7);
    lxw_capture_problems_text(problems,sizeof(problems));
    assert(strstr(problems,"stage=controller_class_unmapped count=1 first_saved=0000000000000007 vtable=outside top=-88 class=N1U4TestE"));
    lxw_capture_vtable=0;lxw_capture_problem_count=0;lxw_capture_problem_note(8);
    lxw_capture_problems_text(problems,sizeof(problems));
    assert(strstr(problems,"vtable=outside top=0 class=?")); /* a failure before any class was read */
    /* The same problem on many objects is one row with a count; more than fit are counted. */
    lxw_capture_problem_count=0;lxw_capture_failed_objects=lxw_capture_problems_dropped=0;world_hash_stage="x";
    for(unsigned i=0;i<5;i++) {lxw_capture_vtable=0x1000;lxw_capture_problem_note(100+i);}
    assert(lxw_capture_problem_count==1 && lxw_capture_problems[0].count==5 && lxw_capture_problems[0].first_saved==100);
    for(unsigned i=0;i<LXW_CAPTURE_PROBLEMS_MAX+3;i++) {lxw_capture_vtable=0x2000+i;lxw_capture_problem_note(200+i);}
    assert(lxw_capture_problem_count==LXW_CAPTURE_PROBLEMS_MAX && lxw_capture_problems_dropped==4);
    lxw_capture_problems_text(problems,sizeof(problems));
    assert(strlen(problems)<8000 && strstr(problems," more=4"));
    static char tight[900];lxw_capture_problems_text(tight,sizeof(tight));
    assert(strlen(tight)<900 && strstr(tight," more="));
    lxw_capture_problem_count=0;lxw_capture_failed_objects=lxw_capture_problems_dropped=0;
    printf("world golden PASS (%zu bytes equal to the Windows text)\n",wt_used);
}
