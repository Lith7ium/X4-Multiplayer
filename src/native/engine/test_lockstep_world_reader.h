/* World capture reads (world_hash_reader): the guarded copy used in production and
 * ReadProcessMemory (read_local) give byte-identical WORLD_OBJECT pages (all four domain
 * hashes, timing fields), WORLD_DETAIL rows, WORLD_BANKS, summaries and states over three
 * captures of a synthetic world (ships with health and vector or legacy Nav controllers, an
 * unknown controller, static objects, an unmapped parent, a removed object), and fail the same
 * way on a no-access page. The guarded capture makes no ReadProcessMemory call. */
#define WCR_OBJECTS 8
static unsigned char wcr_ship[0x300],wcr_ship_controller[0x80],wcr_shield[0xa8],wcr_odd[0x300],wcr_odd_controller[0x80];
static unsigned char wcr_nav_ship[0x300],wcr_nav[0xe0],wcr_slots[0x340],wcr_static[0x300],wcr_zone[0x300],wcr_orphan[0x300],wcr_stranger[0x40];
static unsigned char *wcr_objects[WCR_OBJECTS];static uint64_t wcr_runtimes[WCR_OBJECTS];static unsigned wcr_count;
static char wcr_text[16384];static double wcr_arg,wcr_number;
static double wcr_tonumber(lua_State *L,int index) {(void)L;(void)index;return wcr_arg;}
static void wcr_pushnumber(lua_State *L,double n) {(void)L;wcr_number=n;}
static void wcr_pushnil(lua_State *L) {(void)L;strcpy(wcr_text,"nil");}
static void wcr_pushlstring(lua_State *L,const char *s,size_t n) {(void)L;assert(n<sizeof(wcr_text));memcpy(wcr_text,s,n);wcr_text[n]=0;}
static void *wcr_find(void *map,uint64_t runtime,unsigned mode) {
    assert(map==(void*)wcr_stranger && mode==4);
    for(unsigned i=0;i<wcr_count;i++) if(wcr_runtimes[i]==runtime) return wcr_objects[i];
    return NULL;
}
static void wcr_object(unsigned char *object,unsigned vtable_rva,uint64_t runtime,void *parent,void *controller,unsigned attention) {
    memset(object,0,0x300);*(void**)object=image+vtable_rva;*(uint64_t*)(object+8)=runtime;*(void**)(object+0x70)=parent;
    *(unsigned*)(object+0x68)=runtime%5;object[0xd3]=(unsigned char)runtime;*(unsigned*)(object+0x1ec)=attention;
    *(void**)(object+0x200)=controller;*(double*)(object+0x280)=100.0;
}
static void wcr_fixture(void) {
    memset(wcr_ship_controller,0,sizeof(wcr_ship_controller));memset(wcr_shield,0,sizeof(wcr_shield));
    memset(wcr_odd_controller,0,sizeof(wcr_odd_controller));memset(wcr_nav,0,sizeof(wcr_nav));memset(wcr_slots,0,sizeof(wcr_slots));
    memset(wcr_stranger,0,sizeof(wcr_stranger));*(uint64_t*)(wcr_stranger+8)=99999; /* runtime without a saved identity */
    wcr_object(wcr_zone,0x4100,2000,NULL,NULL,1);
    wcr_object(wcr_ship,0x4000,2001,wcr_zone,wcr_ship_controller,9);
    *(void**)(wcr_ship+0x290)=wcr_shield;*(void**)(wcr_shield+0x90)=image+0x2b19700;*(void**)(wcr_shield+0x98)=wcr_shield;
    *(uintptr_t*)(wcr_shield+0x10)=8;*(uintptr_t*)(wcr_shield+0x18)=24;*(double*)(wcr_shield+0x68)=2.5;*(uint32_t*)(wcr_shield+0x28)=7;
    *(void**)wcr_ship_controller=image+0x2a34b38;*(void**)(image+0x2a34b38+0x30)=image+0x10ba20;
    for(unsigned i=0;i<16;i++) ((uint32_t*)(wcr_ship_controller+0x10))[i]=0x3f800000u+i;
    wcr_object(wcr_odd,0x4000,2002,wcr_zone,wcr_odd_controller,6);
    *(void**)wcr_odd_controller=image+0x2a34c00;*(void**)(image+0x2a34c00+0x30)=image+0x123450; /* unknown getter: flags 8 */
    wcr_object(wcr_nav_ship,0x4000,2003,wcr_zone,wcr_nav,9);
    *(void**)wcr_nav=image+0x2c51158;*(void**)(wcr_nav+0x30)=wcr_slots;*(double*)(wcr_nav+0xc0)=10.5;*(double*)(wcr_nav+0xd0)=17.25;wcr_nav[0xd8]=1;
    for(unsigned p=0;p<2;p++) {
        unsigned char *bank=wcr_slots+p*0x1a0;
        for(unsigned i=0;i<24;i++) ((uint32_t*)bank)[i]=0x40000000u+p*100+i;
        *(uint64_t*)(bank+0xb0)=2001;*(uint64_t*)(bank+0xb8)=2000;*(uint32_t*)(bank+0xcc)=8+p;*(double*)(bank+0x150)=11.5+p;
        bank[0x159]=(unsigned char)p;bank[0x15a]=(unsigned char)(1-p);bank[0x15b]=4;
    }
    wcr_object(wcr_static,0x4100,2004,wcr_zone,NULL,1);
    wcr_object(wcr_orphan,0x4200,2005,wcr_stranger,NULL,2); /* unknown class, unmapped parent */
    unsigned char *objects[]={wcr_zone,wcr_ship,wcr_odd,wcr_nav_ship,wcr_static,wcr_orphan};
    wcr_count=0;for(unsigned i=0;i<6;i++) {wcr_objects[wcr_count]=objects[i];wcr_runtimes[wcr_count++]=*(uint64_t*)(objects[i]+8);}
    wcr_runtimes[wcr_count]=2006;wcr_objects[wcr_count++]=NULL; /* imported, never present */
    *(uint32_t*)(image+0x38f4ee4)=0;*(uint32_t*)(image+0x2f4b6f4)=1;*(void**)(image+0x6cf1500)=wcr_stranger;
}
static void wcr_reset(void) {
    for(unsigned i=0;i<world_hash_count;i++) {uint64_t runtime=world_hash_items[i].runtime,saved=world_hash_items[i].saved;
        world_hash_items[i]=(world_hash_item){.runtime=runtime,.saved=saved};}
    for(unsigned i=0;i<world_hash_watch_count;i++) {uint64_t saved=world_hash_watches[i].saved;
        memset(&world_hash_watches[i],0,sizeof(world_hash_watches[i]));world_hash_watches[i].saved=saved;}
    world_hash_scans=world_hash_tick=world_hash_failures=world_hash_failure_id=0;world_hash_changes=0;world_hash_schema=1;
    world_class_count=2;world_classes[0]=(world_class_cache){(uintptr_t)(image+0x4000),3};world_classes[1]=(world_class_cache){(uintptr_t)(image+0x4100),0};
    *(uint64_t*)(image+0x4200+0x11e0)=0; /* the unknown class's IsRealClass slot: not a verified stub */
}
static void wcr_append(char *out,size_t capacity,const char *text) {
    size_t used=strlen(out),n=strlen(text);assert(used+n+2<capacity);memcpy(out+used,text,n);out[used+n]='\n';out[used+n+1]=0;
}
static void wcr_collect(char *out,size_t capacity) {
    wcr_append(out,capacity,world_hash_summary);
    wcr_arg=0;assert(lua_world_page(NULL)==1);unsigned pages=(unsigned)wcr_number;
    for(unsigned page=1;page<=pages;page++) {wcr_arg=page;assert(lua_world_page(NULL)==1 && strcmp(wcr_text,"nil"));wcr_append(out,capacity,wcr_text);}
    wcr_arg=0;assert(lua_world_detail(NULL)==1);unsigned details=(unsigned)wcr_number;assert(details==2*WORLD_HASH_DOMAINS);
    for(unsigned row=1;row<=details;row++) {wcr_arg=row;assert(lua_world_detail(NULL)==1 && strcmp(wcr_text,"nil"));wcr_append(out,capacity,wcr_text);}
    assert(lua_world_banks(NULL)==1 && strcmp(wcr_text,"nil"));wcr_append(out,capacity,wcr_text);
}
static void wcr_run(int (*reader)(const void *,void *,size_t),char *out,size_t capacity,unsigned char *noaccess) {
    out[0]=0;wcr_fixture();wcr_reset();world_hash_reader=reader;
    assert(world_hash_capture_with(1,1,wcr_find));wcr_collect(out,capacity);
    *(double*)(wcr_ship+0x280)=99.0;((uint32_t*)(wcr_ship_controller+0x10))[5]^=1; /* health and motion */
    *(unsigned*)(wcr_static+0x1ec)=9;((uint32_t*)wcr_slots)[3]^=4;*(double*)(wcr_nav+0xc0)=10.75; /* timing and Nav */
    assert(world_hash_capture_with(2,1,wcr_find));wcr_collect(out,capacity);
    wcr_objects[4]=NULL;*(uint64_t*)(wcr_stranger+8)=2000; /* static object gone, the orphan's parent now the zone */
    assert(world_hash_capture_with(3,1,wcr_find));wcr_collect(out,capacity);
    assert(world_hash_capture_with(4,1,wcr_find));wcr_collect(out,capacity); /* nothing changed: no page */
    *(void**)(wcr_ship+0x70)=noaccess;assert(!world_hash_capture_with(5,1,wcr_find));
    char failure[96];snprintf(failure,sizeof(failure),"FAILED stage=%s saved=%016llx",world_hash_stage,(unsigned long long)world_hash_failure_id);
    wcr_append(out,capacity,failure);
}
static void test_world_reader_equivalence(void) {
    double (*tonumber)(lua_State *,int)=p_lua_tonumber;void (*pushnumber)(lua_State *,double)=p_lua_pushnumber;
    void (*pushnil)(lua_State *)=p_lua_pushnil;void (*pushlstring)(lua_State *,const char *,size_t)=p_lua_pushlstring;
    p_lua_tonumber=wcr_tonumber;p_lua_pushnumber=wcr_pushnumber;p_lua_pushnil=wcr_pushnil;p_lua_pushlstring=wcr_pushlstring;
    DWORD prior_ui=ui_thread;ui_thread=GetCurrentThreadId();
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(world_identities);world_identity_count=0;world_identity_sealed=0;
    for(uint64_t runtime=2000;runtime<=2006;runtime++) assert(world_identity_record(runtime,0x500+(runtime-2000)*0x11));
    world_hash_count=7;world_hash_items=calloc(world_hash_count,sizeof(*world_hash_items));
    world_hash_changed=calloc(world_hash_count+WORLD_HASH_CREATED_MAX,sizeof(*world_hash_changed));assert(world_hash_items && world_hash_changed);
    for(unsigned i=0;i<world_hash_count;i++) world_hash_items[i]=(world_hash_item){.runtime=2000+i,.saved=0x500+i*0x11};
    assert(BCryptOpenAlgorithmProvider(&world_sha_algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 &&
           BCryptCreateHash(world_sha_algorithm,&world_sha_row,NULL,0,NULL,0,BCRYPT_HASH_REUSABLE_FLAG)>=0);
    for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) assert(BCryptCreateHash(world_sha_algorithm,&world_sha_roots[d],NULL,0,NULL,0,BCRYPT_HASH_REUSABLE_FLAG)>=0);
    world_hash_watch_count=0;assert(world_hash_watch_add("0000000000000511",16) && world_hash_watch_add("0000000000000533",16));
    unsigned char *noaccess=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);assert(noaccess);
    static char by_system_call[65536],guarded[65536];
    wcr_run(read_local,by_system_call,sizeof(by_system_call),noaccess);
    uint64_t calls=read_local_calls;
    wcr_run(sample_read,guarded,sizeof(guarded),noaccess);
    assert(read_local_calls==calls); /* the guarded capture makes no ReadProcessMemory call */
    assert(!strcmp(by_system_call,guarded));
    assert(strstr(guarded,"WORLD_OBJECT tick=1 id=0000000000000500 ") && strstr(guarded,"WORLD_OBJECT tick=2 id=0000000000000511 ") &&
           strstr(guarded,"WORLD_OBJECT tick=3 id=0000000000000544 ") && strstr(guarded," timing_fields=") &&
           strstr(guarded,"tick=1 objects=7 present=6 changed=7 ") && strstr(guarded,"tick=4 objects=7 present=5 changed=0 ") &&
           strstr(guarded,"domain=health bytes=") && strstr(guarded,"FAILED stage=topology saved=0000000000000511"));
    assert(strstr(guarded,"type=4000 flags=7 ") && strstr(guarded,"flags=f ") && strstr(guarded,"flags=31 "));
    world_hash_reader=sample_read;
    for(unsigned d=0;d<WORLD_HASH_DOMAINS;d++) BCryptDestroyHash(world_sha_roots[d]);
    BCryptDestroyHash(world_sha_row);BCryptCloseAlgorithmProvider(world_sha_algorithm,0);
    world_sha_row=NULL;world_sha_algorithm=NULL;memset(world_sha_roots,0,sizeof(world_sha_roots));
    free(world_hash_items);free(world_hash_changed);world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;
    world_hash_scans=world_hash_tick=world_hash_failures=world_hash_failure_id=0;world_hash_watch_count=0;world_class_count=0;
    world_hash_stage="not_started";world_hash_changes=0;
    VirtualFree(noaccess,0,MEM_RELEASE);VirtualFree(world_identities,0,MEM_RELEASE);world_identities=NULL;world_identity_count=0;
    p_lua_tonumber=tonumber;p_lua_pushnumber=pushnumber;p_lua_pushnil=pushnil;p_lua_pushlstring=pushlstring;ui_thread=prior_ui;
    printf("world capture reader PASS: guarded copy and ReadProcessMemory give identical pages, details, banks and states; %u bytes compared\n",
           (unsigned)strlen(guarded));
}
