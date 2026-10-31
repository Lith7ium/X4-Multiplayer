static unsigned char test_md_prefix[5]={0x48,0x89,0x5c,0x24,0x08};
static void test_registry_md_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt));
    assert(nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt.FileHeader.TimeDateStamp==0x6a1eaded && nt.OptionalHeader.SizeOfImage==0x6fa9000 && nt.FileHeader.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64));
    unsigned rvas[]={REGISTRY_MD_RVA,0xc20810},lengths[]={162,884},offsets[2]={0};
    const char *hashes[]={REGISTRY_MD_SHA,REGISTRY_MD_ACTION_SHA};
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));
        for(unsigned j=0;j<2;j++) if(rvas[j]>=section.VirtualAddress && rvas[j]+lengths[j]<=section.VirtualAddress+section.SizeOfRawData)
            offsets[j]=section.PointerToRawData+rvas[j]-section.VirtualAddress;
    }
    unsigned char bytes[884];
    for(unsigned j=0;j<2;j++) {
        assert(offsets[j] && !fseek(file,(long)offsets[j],SEEK_SET) && fread(bytes,1,lengths[j],file)==lengths[j]);
        assert(hash_ok(bytes,lengths[j],hashes[j]));
        if(!j) memcpy(test_md_prefix,bytes,5);
        else for(unsigned k=0;k<2;k++) {
            unsigned call=k?0x145:0xb8;int32_t relative;memcpy(&relative,bytes+call+1,4);
            assert(bytes[call]==0xe8 && (int64_t)rvas[j]+call+5+relative==REGISTRY_MD_RVA);
        }
    }
    fclose(file);puts("pinned native MD functions/callers and actual 5-byte prologue verified");
}
static registry_md_fn test_md_trampoline;
static void *test_md_holder;
static const registry_md_string *test_md_name;
static uint64_t test_md_payload;
static unsigned test_md_alignment,test_md_calls;
static uint64_t test_md_rbx;
static unsigned char test_md_result;
static unsigned char test_md_original(void *holder,const registry_md_string *name,uint64_t payload) {
    assert(holder==test_md_holder && name==test_md_name && payload==test_md_payload);++test_md_calls;return test_md_result;
}
static unsigned char test_md_borrowed_original(void *holder,const registry_md_string *name,uint64_t payload) {
    unsigned char result=test_md_original(holder,name,payload);memset((void*)name->data,'x',(size_t)name->length);return result;
}
static HANDLE test_md_entered,test_md_release;
static unsigned char test_md_open_original(void *holder,const registry_md_string *name,uint64_t payload) {
    unsigned char result=test_md_original(holder,name,payload);assert(SetEvent(test_md_entered));
    assert(WaitForSingleObject(test_md_release,2000)==WAIT_OBJECT_0);return result;
}
static DWORD WINAPI test_md_open_thread(LPVOID unused) {
    (void)unused;return registry_md_observe(test_md_open_original,test_md_holder,test_md_name,test_md_payload,image+0xc2095a);
}
static unsigned char test_md_hook(void *holder,const registry_md_string *name,uint64_t payload) {
    return registry_md_observe(test_md_trampoline,holder,name,payload,image+0xc208cd);
}
static void test_registry_md_abi(void) {
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(code);
    memcpy(code,test_md_prefix,5);unsigned n=5;
    unsigned char head[]={0x48,0x83,0xec,0x28,0x48,0x89,0xe0,0x83,0xe0,0x0f,0x48,0xbb};
    memcpy(code+n,head,sizeof(head));n+=sizeof(head);void *address=&test_md_alignment;memcpy(code+n,&address,8);n+=8;
    code[n++]=0x89;code[n++]=0x03;code[n++]=0x48;code[n++]=0xb8;address=(void*)test_md_original;memcpy(code+n,&address,8);n+=8;
    unsigned char tail[]={0xff,0xd0,0x48,0x8b,0x5c,0x24,0x30,0x48,0x83,0xc4,0x28,0xc3};memcpy(code+n,tail,sizeof(tail));
    thunk_recipe recipe=*thunk_recipe_at(2432);recipe.target=(void*)test_md_hook;
    assert(thunk_emit_slot_at(code+256,&recipe,code));test_md_trampoline=(registry_md_fn)(code+256);
    void *continuation=NULL;memcpy(&continuation,code+267,8);assert(continuation==code+5 && !memcmp(code+256,test_md_prefix,5));
    unsigned char retained[3];memcpy(retained,code+5,3);code[0]=0xe9;int32_t distance=283;memcpy(code+1,&distance,4);
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    /* A caller with a nonvolatile-register sentinel also verifies RBX through
     * dispatch, recorder, relocated original prologue and return. */
    unsigned char *call=code+512;unsigned k=0;
    const unsigned char caller_head[]={0x53,0x48,0x83,0xec,0x20,0x48,0xbb};
    memcpy(call+k,caller_head,sizeof(caller_head));k+=sizeof(caller_head);
    uint64_t sentinel=0x123456789abcdef0ULL;memcpy(call+k,&sentinel,8);k+=8;
    call[k++]=0x48;call[k++]=0xb8;address=code;memcpy(call+k,&address,8);k+=8;call[k++]=0xff;call[k++]=0xd0;
    call[k++]=0x48;call[k++]=0xba;address=&test_md_rbx;memcpy(call+k,&address,8);k+=8;
    const unsigned char caller_tail[]={0x48,0x89,0x1a,0x48,0x83,0xc4,0x20,0x5b,0xc3};memcpy(call+k,caller_tail,sizeof(caller_tail));
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    registry_md_string name={"X4LAN_NativeConstruction.Builder",sizeof("X4LAN_NativeConstruction.Builder")-1};test_md_holder=&name;test_md_name=&name;
    test_md_payload=0xfedcba9876543210ULL;test_md_calls=0;
    for(unsigned i=0;i<3;i++) {
        test_md_result=i==2?0xa5:(unsigned char)i;test_md_alignment=99;
        unsigned char result=((registry_md_fn)call)(test_md_holder,test_md_name,test_md_payload);
        assert(result==test_md_result && test_md_calls==i+1 && test_md_alignment==0 && test_md_rbx==sentinel && !memcmp(retained,code+5,3));
    }
    code[0]=0;assert(!registry_md_emit(code+256,code+320,code,(void*)test_md_hook));VirtualFree(code,0,MEM_RELEASE);
}
static void test_registry_md(void) {
    registry_md_event event={0};registry_md_string name={"X4LAN_NativeConstruction.Boundary",sizeof("X4LAN_NativeConstruction.Boundary")-1};
    assert(registry_md_name(&name,&event) && event.kind==1 && event.reads==1 && !event.error);
    name.data="X4LAN_NativeConstruction.Builder";name.length=strlen(name.data);memset(&event,0,sizeof(event));
    assert(registry_md_name(&name,&event) && event.kind==2 && !event.error);
    name.data="X4LAN_NativeConstruction.Station";name.length=strlen(name.data);memset(&event,0,sizeof(event));
    assert(registry_md_name(&name,&event) && event.kind==3 && !event.error && !strcmp(event.name,name.data));
    name.data="X4LAN_NativeConstruction.Storage";name.length=strlen(name.data);memset(&event,0,sizeof(event));
    assert(registry_md_name(&name,&event) && event.kind==4 && !event.error && !strcmp(event.name,name.data));
    name.data="X4LAN_NativeConstruction.Nope";name.length=strlen(name.data);memset(&event,0,sizeof(event));
    assert(registry_md_name(&name,&event) && event.error==REGISTRY_MD_UNKNOWN && !strcmp(event.name,name.data));
    name.data="X4LAN_NativeConstruction.Bad Name";name.length=strlen(name.data);memset(&event,0,sizeof(event));
    assert(registry_md_name(&name,&event) && event.error==REGISTRY_MD_UNKNOWN && !strncmp(event.name,"hex_58344c414e",14));
    name.length=80;memset(&event,0,sizeof(event));assert(registry_md_name(&name,&event) && event.error==REGISTRY_MD_NAME);
    name.data="Some unrelated native event name";name.length=strlen(name.data);memset(&event,0,sizeof(event));assert(!registry_md_name(&name,&event));
    memset(&event,0,sizeof(event));assert(registry_md_name(NULL,&event) && event.error==REGISTRY_MD_NAME);
    unsigned char holder[8]={0},sink[8]={0},type[0x200]={0},tls[0x320]={0};
    *(void**)holder=sink;*(void**)sink=type;*(void**)(type+0x1f0)=image+0x12345;
    tls[0x314]=1;*(uint64_t*)(tls+0x300)=0x123456789abcdef0ULL;
    main_thread=ui_thread=GetCurrentThreadId();image[0x38f0a52]=1;
    memset(&event,0,sizeof(event));event.reads=1;registry_md_snapshot_context(&event,holder,tls);
    assert(event.reads==31 && !event.error && event.time==0x123456789abcdef0ULL && event.sink==0x12345 && event.ready==1 && event.main && event.ui);
    ++ui_thread;tls[0x314]=0;memset(&event,0,sizeof(event));event.reads=1;
    registry_md_snapshot_context(&event,holder,tls);
    assert(event.reads==31 && !event.error && !event.ui && event.main && !event.view && event.thread==GetCurrentThreadId());--ui_thread;tls[0x314]=1;
    memset(&event,0,sizeof(event));registry_md_snapshot_context(&event,NULL,NULL);assert(event.error && event.reads==2);
    stats.active=1;boot_phase=3;stats.issued=6;registry_md_installed=1;
    assert(registry_reserve(0x500000003ULL,"test_macro",10,"player",6,5)==15);
    test_registry_md_abi(); /* Complete active recorder, entry patch, dispatch and relocated native prefix. */
    assert(registry_md_count==6 && registry_md_order==6 && !actor_components[14] && !dynamic_ships[0].bound_tick);
    for(unsigned i=0;i<6;i++) assert(registry_md_events[i].order==i+1 && registry_md_events[i].pair==(i/2)*2+1 && registry_md_events[i].payload==test_md_payload);
    uint64_t before=(uint64_t)registry_md_order;registry_pending_ai ai=registry_read_ai(NULL,NULL,NULL,NULL,0,NULL);
    assert(ai.order==before+1 && ai.recorded && !ai.reads);
    registry_md_event row={.kind=1,.phase=1,.handle=0x500000003ULL};registry_md_append(&row);
    assert(row.order==ai.order+1);char out[768];assert(registry_md_format(6,out,sizeof(out)) && strstr(out,"order=8 pair=8"));
    assert(!registry_md_format(7,out,sizeof(out)) && !registry_md_format(0,out,2));
    /* Unknown caller/phase cannot look like eligible boundary evidence. Both
     * still forward the original call exactly once and preserve its AL. */
    registry_md_string invalid={"X4LAN_NativeConstruction.Boundary",sizeof("X4LAN_NativeConstruction.Boundary")-1};
    test_md_holder=holder;test_md_name=&invalid;test_md_payload=9;test_md_result=1;unsigned calls=test_md_calls;
    assert(registry_md_observe(test_md_original,holder,&invalid,9,image+0x12345)==1 && test_md_calls==calls+1);
    assert((registry_md_events[7].error&(REGISTRY_MD_CALLER|REGISTRY_MD_PHASE))==(REGISTRY_MD_CALLER|REGISTRY_MD_PHASE));
    dynamic_ships[1].handle=0x500000004ULL;dynamic_ship_count=2;test_md_payload=1;
    assert(registry_md_observe(test_md_original,holder,&invalid,1,image+0xc2095a)==1 && test_md_calls==calls+2);
    assert(registry_md_events[9].error&REGISTRY_MD_PENDING);dynamic_ship_count=1;
    char borrowed[]="X4LAN_NativeConstruction.Builder";registry_md_string temporary={borrowed,sizeof(borrowed)-1};
    test_md_name=&temporary;
    assert(registry_md_observe(test_md_borrowed_original,holder,&temporary,1,image+0xc208cd)==1 && test_md_calls==calls+3);
    assert(!strcmp(registry_md_events[11].name,"X4LAN_NativeConstruction.Builder") &&
           !strcmp(registry_md_events[12].name,"X4LAN_NativeConstruction.Builder") && borrowed[0]=='x');test_md_name=&invalid;
    stats.active=0;unsigned count=registry_md_count;
    assert(registry_md_observe(test_md_original,holder,&invalid,1,image+0xc2095a)==1 && test_md_calls==calls+4 && registry_md_count==count);stats.active=1;
    LONG64 failures=registry_md_failures;registry_md_count=REGISTRY_MD_CAPACITY;registry_md_append(&row);
    assert(registry_md_failures==failures+1 && registry_md_count==REGISTRY_MD_CAPACITY && (row.error&REGISTRY_MD_OVERFLOW));
    registry_md_original_word=123;registry_md_patched_word=456;*(uint64_t*)(image+REGISTRY_MD_RVA)=789;
    assert(!registry_md_swap(456,123));registry_md_restore();assert(registry_md_installed && !registry_md_restored && *(uint64_t*)(image+REGISTRY_MD_RVA)==789);
    *(uint64_t*)(image+REGISTRY_MD_RVA)=456;registry_md_restore();assert(!registry_md_installed && registry_md_restored && *(uint64_t*)(image+REGISTRY_MD_RVA)==123);
    assert(!registry_md_open && registry_md_peak==1 && !registry_md_restore_open);
    /* Restoration never waits for an engine original. A capture still open at
     * restore remains explicit incomplete evidence, even after its leave lands. */
    registry_md_installed=1;registry_md_restored=registry_md_restoring=0;registry_md_count=0;
    *(uint64_t*)(image+REGISTRY_MD_RVA)=456;
    test_md_holder=holder;test_md_name=&invalid;test_md_payload=1;test_md_result=1;
    test_md_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_md_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(test_md_entered && test_md_release);HANDLE worker=CreateThread(NULL,0,test_md_open_thread,NULL,0,NULL);assert(worker);
    assert(WaitForSingleObject(test_md_entered,2000)==WAIT_OBJECT_0 && registry_md_count==1 && registry_md_open==1);
    registry_md_restore();assert(registry_md_restored && !registry_md_installed && registry_md_open==1 && registry_md_restore_open==1);
    assert(SetEvent(test_md_release) && WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);
    DWORD result;assert(GetExitCodeThread(worker,&result) && result==1 && registry_md_count==2 && !registry_md_open && registry_md_restore_open==1);
    assert(registry_md_events[0].thread==registry_md_events[1].thread && registry_md_events[0].thread!=GetCurrentThreadId());
    CloseHandle(worker);CloseHandle(test_md_entered);CloseHandle(test_md_release);
    assert(!registry_md_next_order());registry_md_original_word=registry_md_patched_word=0;registry_md_restored=0;
    registry_md_restoring=0;registry_md_restore_open=registry_md_peak=0;
    registry_md_count=0;registry_md_order=registry_md_failures=0;memset(registry_md_events,0,sizeof(registry_md_events));
    memset(dynamic_ships,0,sizeof(dynamic_ships));dynamic_ship_count=0;InterlockedExchange(&registry_observing,0);
    stats.active=0;stats.issued=0;boot_phase=0;
}

/* Exercise actual lua_boot reload admission without installing any hook. Root
 * extends this dependency test when combining its additional option families. */
static double test_md_option_input,test_md_option_result;
static unsigned test_md_option_pushes;
static double test_md_option_number(lua_State *state,int index) {
    (void)state;if(index==1)return 1;if(index==2)return 12345;assert(index==6);return test_md_option_input;
}
static const char *test_md_option_string(lua_State *state,int index,size_t *length) {
    (void)state;assert(!length && (index==3 || index==5));return index==3?secret:initial_state_digest;
}
static void test_md_option_push(lua_State *state,double value) {(void)state;test_md_option_result=value;++test_md_option_pushes;}
static void test_md_option_check(double input,uint64_t owned,int expected) {
    test_md_option_input=input;test_md_option_result=-1;test_md_option_pushes=0;boot_options=owned;gate_stats before=stats;
    assert(lua_boot(NULL)==1 && test_md_option_pushes==1 && test_md_option_result==expected);
    assert(!memcmp(&before,&stats,sizeof(stats)) && boot_options==owned && !installed && used && boot_mode);
}
static void test_registry_md_options(void) {
    assert(!used && !installed && !stats.active && !boot_mode);gate_stats before=stats;
    uint64_t old_options=boot_options;unsigned old_port=remote_port;
    char old_secret[sizeof(secret)],old_digest[sizeof(initial_state_digest)];memcpy(old_secret,secret,sizeof(secret));memcpy(old_digest,initial_state_digest,sizeof(initial_state_digest));
    double (*old_number)(lua_State *,int)=p_lua_tonumber;const char *(*old_string)(lua_State *,int,size_t *)=p_lua_tolstring;
    void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    p_lua_tonumber=test_md_option_number;p_lua_tolstring=test_md_option_string;p_lua_pushnumber=test_md_option_push;
    used=boot_mode=1;stats.active=1;stats.slot=1;remote_port=12345;
    memset(secret,'a',32);secret[32]=0;memset(initial_state_digest,'b',64);initial_state_digest[64]=0;
    uint64_t valid=REGISTRY_MD_BIT|(1ULL<<46)|(1ULL<<41)|32|1;
    test_md_option_check((double)valid,valid,0);test_md_option_check(0,0,0);
    test_md_option_check((double)(((1ULL<<47)-1)|REGISTRY_MD_BIT),((1ULL<<47)-1)|REGISTRY_MD_BIT,0);
    for(unsigned i=0;i<4;i++) {uint64_t invalid=valid&~(i==0?1ULL:i==1?32ULL:i==2?1ULL<<46:1ULL<<41);test_md_option_check((double)invalid,invalid,10);}
    /* These individual bits lack their NPC/solver dependencies, even though the
     * integrated build supports each family. Complete combinations must pass. */
    for(unsigned bit=47;bit<=50;bit++) if(bit!=48) test_md_option_check((double)(valid|(1ULL<<bit)),valid|(1ULL<<bit),10);
    test_md_option_check((double)(valid|(1ULL<<48)),valid|(1ULL<<48),0);
    uint64_t combined=valid|(1ULL<<41)|(1ULL<<47)|(1ULL<<48)|(1ULL<<49)|SOLVER_OPTION|4;
    test_md_option_check((double)combined,combined,0);
    test_md_option_check((double)(1ULL<<52),1ULL<<52,10);
    test_md_option_check((double)valid,0,10);test_md_option_check(-1,0,10);test_md_option_check(0.5,0,10);
    test_md_option_check(NAN,0,10);test_md_option_check(INFINITY,0,10);
    p_lua_tonumber=old_number;p_lua_tolstring=old_string;p_lua_pushnumber=old_push;
    stats=before;boot_options=old_options;remote_port=old_port;used=boot_mode=0;
    memcpy(secret,old_secret,sizeof(secret));memcpy(initial_state_digest,old_digest,sizeof(initial_state_digest));
}
