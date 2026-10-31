/* Owned-memory reads and actual-image data pins only. Never executes X4 code. */
#include "lockstep_gate.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned checks;
#define TEST(x) do {++checks;assert(x);} while(0)
static __declspec(align(16)) unsigned char nav_object[0x300],nav_controller[0xe0],nav_agent[0x1000];
static unsigned char *change_address;static const void *change_after;
static unsigned change_visit,visits,reader_calls,fail_read,flip_phase;
static const char *why;
static unsigned resolve_calls;
static uint64_t changing_resolve(uint64_t runtime) {
    return world_identity_lookup(runtime)+(++resolve_calls>6);
}
static char exported[16384];static double lua_argument,lua_result;
static double number_arg(lua_State *L,int index) {(void)L;(void)index;return lua_argument;}
static void number_result(lua_State *L,double n) {(void)L;lua_result=n;}
static void nil_result(lua_State *L) {(void)L;strcpy(exported,"nil");}
static void text_result(lua_State *L,const char *s,size_t n) {(void)L;TEST(n<sizeof(exported));memcpy(exported,s,n);exported[n]=0;}
static void relabel(int pending) {
    unsigned char temp[WORLD_NAV_BANK_BYTES];
    memcpy(temp,nav_agent+0xad0,sizeof(temp));memcpy(nav_agent+0xad0,nav_agent+0xc70,sizeof(temp));memcpy(nav_agent+0xc70,temp,sizeof(temp));
    if(pending) {memcpy(temp,nav_agent+0xed0,16);memcpy(nav_agent+0xed0,nav_agent+0xee0,16);memcpy(nav_agent+0xee0,temp,16);}
    *(uint32_t*)(image+0x38f4ee4)^=1;*(uint32_t*)(image+0x2f4b6f4)^=1;
    if(*(uint32_t*)(nav_controller+0xc8)<2) *(uint32_t*)(nav_controller+0xc8)^=1;
}
static int owned_reader(const void *source,void *dest,size_t length) {
    if(++reader_calls==fail_read) return 0;
    int ok=read_local(source,dest,length);
    if(source==change_after && ++visits==change_visit) {
        if(flip_phase) relabel(1);else *change_address^=1;
    }
    return ok;
}
static void fixture(void) {
    memset(nav_object,0,sizeof(nav_object));memset(nav_controller,0,sizeof(nav_controller));memset(nav_agent,0,sizeof(nav_agent));
    *(void**)nav_object=image+0x4000;*(uint64_t*)(nav_object+8)=100;*(void**)(nav_object+0x200)=nav_controller;
    *(uint32_t*)(nav_object+0x1ec)=2;*(void**)nav_controller=image+0x2c51158;
    *(void**)(nav_controller+0x28)=nav_object;*(void**)(nav_controller+0x30)=nav_agent+0xad0;
    *(void**)(nav_controller+0x38)=nav_agent+0xe10;*(void**)(nav_controller+0x48)=nav_agent+0x6e0;
    *(void**)(nav_controller+0x40)=nav_agent;*(void**)(nav_controller+0x50)=nav_agent+0xed0;
    *(void**)(nav_agent+0xef0)=nav_object;*(double*)(nav_controller+0xc0)=10.5;*(double*)(nav_controller+0xd0)=17.25;
    nav_controller[0xd8]=1;*(uint32_t*)(image+0x38f4ee4)=0;*(uint32_t*)(image+0x2f4b6f4)=1;
    for(unsigned p=0;p<2;p++) {
        unsigned char *bank=nav_agent+0xad0+p*WORLD_NAV_BANK_BYTES;
        for(unsigned i=0;i<24;i++) ((uint32_t*)bank)[i]=1000+p*100+i;
        *(uint64_t*)(bank+0xb0)=101;*(uint64_t*)(bank+0xb8)=102;
        *(uint32_t*)(bank+0xcc)=8+p;*(double*)(bank+0x150)=11.5+p;
        bank[0x159]=(unsigned char)p;bank[0x15a]=(unsigned char)(1-p);bank[0x15b]=4;
        *(uint64_t*)(nav_agent+0xed0+p*16)=101+p;*(uint64_t*)(nav_agent+0xed8+p*16)=3+p*2;
    }
    change_after=NULL;change_address=NULL;change_visit=visits=reader_calls=fail_read=flip_phase=0;
    world_banks_before=(world_bank_phase){0,1};world_hash_schema=2;world_hash_failures=0;
}
static int capture(world_nav_snapshot *s) {
    return world_nav_capture(owned_reader,world_identity_lookup,(uintptr_t)image,(uintptr_t)nav_object,
                             (uintptr_t)nav_controller,100,s,&why);
}
static void encode(world_nav_snapshot *s,unsigned char *timing,unsigned char *motion,unsigned char *raw) {
    unsigned flags=5;world_nav_encode(s,timing,motion,raw,&flags);TEST(flags==5);
}
static void semantics(void) {
    world_nav_snapshot a,b;unsigned char ta[79],tb[79],ma[190],mb[190],ra[99],rb[99];
    fixture();TEST(capture(&a));encode(&a,ta,ma,ra);relabel(1);TEST(capture(&b));encode(&b,tb,mb,rb);
    TEST(!memcmp(ta,tb,sizeof(ta)) && !memcmp(ma,mb,sizeof(ma)) && memcmp(ra,rb,sizeof(ra)));
    /* Equal banks do not excuse pending entries which were not transposed. */
    fixture();memcpy(nav_agent+0xc70,nav_agent+0xad0,WORLD_NAV_BANK_BYTES);
    TEST(capture(&a));encode(&a,ta,ma,ra);relabel(0);TEST(capture(&b));encode(&b,tb,mb,rb);
    TEST(memcmp(ta,tb,sizeof(ta)) && !memcmp(ma,mb,sizeof(ma)));
    TEST(!memcmp(ta,tb,45)); /* The historical cursor-only proposal misses this. */
    for(unsigned cursor=0;cursor<3;cursor++) {
        fixture();*(uint32_t*)(nav_controller+0xc8)=cursor;TEST(capture(&a));encode(&a,ta,ma,ra);
        TEST(*(uint32_t*)(ta+12)==cursor);relabel(1);TEST(capture(&b));encode(&b,tb,mb,rb);TEST(!memcmp(ta,tb,sizeof(ta)));
    }
    fixture();*(uint64_t*)(nav_agent+0xed0)=0;TEST(capture(&a));encode(&a,ta,ma,ra);
    TEST(ta[45]==WORLD_NAV_NULL && !*(uint64_t*)(ta+46) && *(uint64_t*)(ta+54)==3);
    fixture();TEST(capture(&a));encode(&a,ta,ma,ra);*(uint64_t*)(nav_agent+0xed0)=901;
    TEST(capture(&b));encode(&b,tb,mb,rb);TEST(!memcmp(ta,tb,sizeof(ta)) && memcmp(ra,rb,sizeof(ra)));
    for(unsigned p=0;p<2;p++) {
        fixture();TEST(capture(&a));encode(&a,ta,ma,ra);*(uint64_t*)(nav_agent+0xed0+p*16)=103;
        TEST(capture(&b));encode(&b,tb,mb,rb);TEST(memcmp(ta,tb,sizeof(ta)));
        for(unsigned j=0;j<2;j++) {
            fixture();TEST(capture(&a));encode(&a,ta,ma,ra);*(uint64_t*)(nav_agent+0xad0+p*416+0xb0+j*8)=103;
            TEST(capture(&b));encode(&b,tb,mb,rb);TEST(memcmp(ma,mb,sizeof(ma)));
        }
    }
    /* Every byte of attention, both deadlines, gate, both ETA/notification
     * tuples and both u64 counters survives canonicalization. */
    unsigned char *fields[]={nav_object+0x1ec,nav_controller+0xc0,nav_controller+0xd0,nav_controller+0xd8,
        nav_agent+0xad0+0x150,nav_agent+0xad0+0x159,nav_agent+0xc70+0x150,nav_agent+0xc70+0x159,
        nav_agent+0xed8,nav_agent+0xee8};
    unsigned lengths[]={4,8,8,1,8,2,8,2,8,8};
    for(unsigned field=0;field<10;field++) for(unsigned byte=0;byte<lengths[field];byte++) {
        fixture();TEST(capture(&a));encode(&a,ta,ma,ra);fields[field][byte]^=1;
        TEST(capture(&b));encode(&b,tb,mb,rb);TEST(memcmp(ta,tb,sizeof(ta)));
    }
}
static void failures(void) {
    world_nav_snapshot s;fixture();TEST(capture(&s));unsigned total=reader_calls;
    for(unsigned i=1;i<=total;i++) {fixture();fail_read=i;TEST(!capture(&s) && strstr(why,"incomplete"));}
    unsigned char *links[]={nav_object+8,nav_object+0x200,nav_controller,nav_controller+0x28,
        nav_controller+0x30,nav_controller+0x38,nav_controller+0x40,nav_controller+0x48,nav_controller+0x50,nav_agent+0xef0};
    for(unsigned i=0;i<sizeof(links)/sizeof(*links);i++) {fixture();*links[i]^=8;TEST(!capture(&s));}
    fixture();*(uintptr_t*)(nav_controller+0x40)=UINTPTR_MAX-7;TEST(!capture(&s));
    for(unsigned value=2;value<5;value++) {fixture();*(uint32_t*)(image+0x38f4ee4)=value;TEST(!capture(&s) && !strcmp(why,"nav_selector_invalid"));}
    fixture();*(uint32_t*)(image+0x2f4b6f4)=0;TEST(!capture(&s));
    const uint32_t cursors[]={3,4,UINT32_MAX};
    for(unsigned i=0;i<3;i++) {fixture();*(uint32_t*)(nav_controller+0xc8)=cursors[i];TEST(!capture(&s) && !strcmp(why,"nav_cursor_invalid"));}
    for(unsigned p=0;p<2;p++) {fixture();*(uint64_t*)(nav_agent+0xed0+p*16)=999;TEST(!capture(&s) && !strcmp(why,"nav_pending_reference_unknown"));}
    /* Mutation after the first copy of each raw block must not pass, including
     * padding which is intentionally absent from semantic motion hashes. */
    const void *blocks[]={nav_controller+0xc0,nav_controller+0xd0,nav_agent+0xad0,nav_agent+0xed0};
    const unsigned lengths[]={12,9,832,32};
    for(unsigned block=0;block<4;block++) for(unsigned byte=0;byte<lengths[block];byte++) {
        fixture();change_after=blocks[block];change_address=(unsigned char*)blocks[block]+byte;change_visit=1;
        TEST(!capture(&s));
    }
    for(unsigned i=0;i<sizeof(links)/sizeof(*links);i++) {
        fixture();change_after=nav_agent+0xed0;change_address=links[i];change_visit=1;TEST(!capture(&s));
    }
    /* A whole, canonically equal relabel between reads still fails raw stability. */
    fixture();change_after=image+0x2f4b6f4;change_visit=2;flip_phase=1;
    TEST(!capture(&s) && !strcmp(why,"nav_tuple_changed"));
    fixture();resolve_calls=0;
    TEST(!world_nav_capture(owned_reader,changing_resolve,(uintptr_t)image,(uintptr_t)nav_object,
          (uintptr_t)nav_controller,100,&s,&why) && !strcmp(why,"nav_tuple_changed"));
}
static void production_record(void) {
    world_hash_item item={.runtime=100,.saved=77};world_hash_data a[4]={0},b[4]={0};
    world_class_count=1;world_classes[0]=(world_class_cache){(uintptr_t)(image+0x4000),2};
    fixture();TEST(world_hash_record(&item,a,nav_object));TEST(item.nav_raw_length==99 && a[3].length==95 && a[2].length==206);
    relabel(1);world_banks_before=(world_bank_phase){1,0};TEST(world_hash_record(&item,b,nav_object));TEST(!memcmp(a,b,sizeof(a)));
    fixture();world_hash_schema=1;memset(a,0,sizeof(a));memset(b,0,sizeof(b));
    TEST(world_hash_record(&item,a,nav_object));relabel(1);TEST(world_hash_record(&item,b,nav_object));
    TEST(memcmp(&a[3],&b[3],sizeof(a[3])) && !item.nav_raw_length && a[3].length==61);
    for(unsigned i=0;i<3;i++) TEST(!memcmp(a+i,b+i,sizeof(*a))); /* Legacy raw cursor failure preserved. */
    fixture();*(uint64_t*)(nav_agent+0xed0)=999;memset(a,0,sizeof(a));TEST(!world_hash_record(&item,a,nav_object));
    fixture();world_banks_before=(world_bank_phase){1,0};memset(a,0,sizeof(a));
    TEST(!world_hash_record(&item,a,nav_object) && !strcmp(world_hash_stage,"nav_census_phase_changed"));
    fixture();*(uint64_t*)(nav_agent+0xad0+0xb0)=999;memset(a,0,sizeof(a));TEST(world_hash_record(&item,a,nav_object) && (item.flags&16));
    world_hash_scans=1;world_hash_schema=2;TEST(!world_hash_capture(2,1) && !strcmp(world_hash_stage,"schema"));world_hash_scans=0;
}
static void nav_test_digest(const void *data,unsigned size,unsigned char result[32]) {
    BCRYPT_ALG_HANDLE algorithm;BCRYPT_HASH_HANDLE hash;
    TEST(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0);
    TEST(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)>=0);
    TEST(BCryptHashData(hash,(PUCHAR)data,size,0)>=0 && BCryptFinishHash(hash,result,32,0)>=0);
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
}
static void exports(const char *path) {
    fixture();world_hash_item items[12];unsigned changed[12];world_hash_data data[4]={0};
    items[0]=(world_hash_item){.runtime=100,.saved=77};TEST(world_hash_record(items,data,nav_object));
    items[0].timing_length=(unsigned char)(data[3].length-16);memcpy(items[0].timing,data[3].data,items[0].timing_length);
    for(unsigned d=0;d<4;d++) nav_test_digest(data[d].data,data[d].length,items[0].hash[d]);
    for(unsigned i=0;i<12;i++) {items[i]=items[0];items[i].saved=77+i;changed[i]=i;}
    world_hash_items=items;world_hash_changed=changed;world_hash_changes=12;world_hash_scans=1;world_hash_tick=1;
    ui_thread=GetCurrentThreadId();p_lua_tonumber=number_arg;p_lua_pushnumber=number_result;p_lua_pushnil=nil_result;p_lua_pushlstring=text_result;
    lua_argument=0;TEST(lua_world_page(NULL)==1 && lua_result==1);lua_argument=1;
    TEST(lua_world_page(NULL)==1 && strcmp(exported,"nil") && strlen(exported)>9000 && strlen(exported)<12288);
    if(path) {
        FILE *f=fopen(path,"wb");TEST(f);
        fprintf(f,"NATIVE_GATE WORLD_BANKS tick=1 read_before=0 write_before=1 read_after=0 write_after=1\n%s",exported);
        fprintf(f,"NATIVE_GATE WORLD_HASH tick=1 objects=12 present=12 changed=12 unknown_class=0 unknown_motion=0 unknown_references=0 state=WORLD_IMPORTED_2:1:12");
        for(unsigned d=0;d<4;d++) {
            unsigned char preimage[12*40],sha[32];char hex[65];
            for(unsigned i=0;i<12;i++) {memcpy(preimage+i*40,&items[i].saved,8);memcpy(preimage+i*40+8,items[i].hash[d],32);}
            nav_test_digest(preimage,sizeof(preimage),sha);hex32(sha,hex);fprintf(f,":%s",hex);
        }
        fprintf(f,"\nNATIVE_GATE WORLD_HASH_STOP scans=1 failures=0\n");fclose(f);
    }
    for(unsigned i=0;i<12;i++) {items[i].timing_length=WORLD_TIMING_BYTES;items[i].type=items[i].flags=UINT32_MAX;items[i].saved=items[i].parent=UINT64_MAX;}
    world_hash_tick=UINT64_MAX;TEST(lua_world_page(NULL)==1 && strcmp(exported,"nil") && strlen(exported)<12288);
    const double invalid[]={-1,0.5,NAN,INFINITY,2};
    for(unsigned i=0;i<5;i++) {lua_argument=invalid[i];TEST(lua_world_page(NULL)==1 && !strcmp(exported,"nil"));}
    lua_argument=1;items[0].nav_raw_length=WORLD_NAV_RAW_BYTES+1;TEST(lua_world_page(NULL)==1 && !strcmp(exported,"nil") && world_hash_failures==1);
    items[0].nav_raw_length=WORLD_NAV_RAW_BYTES;TEST(lua_world_page(NULL)==1 && !strcmp(exported,"nil"));
    TEST(lua_world_banks(NULL)==1 && !strcmp(exported,"nil"));
    world_hash_items=NULL;world_hash_changed=NULL;world_hash_scans=world_hash_failures=0;
}
static void *owned_find(void *map,uint64_t runtime,unsigned mode) {
    TEST(map==nav_agent && mode==4);return runtime==100?nav_object:NULL;
}
static void production_census(void) {
    fixture();memset(world_identities,0,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY);world_identity_count=0;
    TEST(world_identity_record(100,77) && world_identity_record(101,1001) && world_identity_record(102,1002));
    world_class_count=1;world_classes[0]=(world_class_cache){(uintptr_t)(image+0x4000),2};
    *(void**)(image+0x6cf1500)=nav_agent;world_hash_items=NULL;world_hash_count=0;world_hash_tick=0;
    world_hash_watch_count=0;TEST(world_hash_watch_add("000000000000004d",16));
    TEST(world_hash_capture_with(1,2,owned_find));TEST(world_hash_count==3 && world_hash_changes==3 && world_hash_alive==1);
    unsigned char first[4][32];memcpy(first,world_hash_items[0].hash,sizeof(first));
    TEST(world_hash_watches[0].data[3].length==95);
    relabel(1);TEST(world_hash_capture_with(2,2,owned_find));
    TEST(!memcmp(first,world_hash_items[0].hash,sizeof(first)) && world_hash_changes==1 && world_hash_changed[0]==0);
    TEST(world_hash_items[0].nav_raw[0]==1 && world_hash_items[0].nav_raw[16]==1);
    TEST(world_hash_capture_with(3,2,owned_find) && !world_hash_changes);
    TEST(!world_hash_capture_with(4,1,owned_find) && !strcmp(world_hash_stage,"schema"));
    *(uint64_t*)(nav_agent+0xed0)=999;
    TEST(!world_hash_capture_with(4,2,owned_find) && !strcmp(world_hash_stage,"nav_pending_reference_unknown"));
    TEST(world_hash_tick==3 && world_hash_scans==3 && world_hash_failure_id==77);
}
/* WORLD_IMPORTED_2 with Nav rows: the production guarded reader and ReadProcessMemory give the
 * same summaries, WORLD_OBJECT pages (hashes, timing fields, nav_raw) and WORLD_DETAIL rows. */
static void reader_census_text(int (*reader)(const void *,void *,size_t),char *out,size_t capacity) {
    world_hash_reader=reader;out[0]=0;size_t used=0;
    fixture();memset(world_identities,0,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY);world_identity_count=0;
    TEST(world_identity_record(100,77) && world_identity_record(101,1001) && world_identity_record(102,1002));
    world_class_count=1;world_classes[0]=(world_class_cache){(uintptr_t)(image+0x4000),2};
    *(void**)(image+0x6cf1500)=nav_agent;world_hash_items=NULL;world_hash_changed=NULL;world_hash_count=0;world_hash_tick=0;
    world_hash_scans=world_hash_failures=0;world_hash_watch_count=0;TEST(world_hash_watch_add("000000000000004d",16));
    for(unsigned tick=1;tick<=3;tick++) {
        if(tick==2) relabel(1);
        if(tick==3) {*(double*)(nav_controller+0xc0)=12.5;*(uint32_t*)(nav_object+0x1ec)=5;}
        TEST(world_hash_capture_with(tick,2,owned_find));
        used+=(size_t)snprintf(out+used,capacity-used,"%s\n",world_hash_summary);
        lua_argument=0;TEST(lua_world_page(NULL)==1);unsigned pages=(unsigned)lua_result;
        for(unsigned page=1;page<=pages;page++) {lua_argument=page;TEST(lua_world_page(NULL)==1 && strcmp(exported,"nil"));
            used+=(size_t)snprintf(out+used,capacity-used,"%s",exported);}
        lua_argument=0;TEST(lua_world_detail(NULL)==1 && lua_result==WORLD_HASH_DOMAINS);
        for(unsigned row=1;row<=WORLD_HASH_DOMAINS;row++) {lua_argument=row;TEST(lua_world_detail(NULL)==1 && strcmp(exported,"nil"));
            used+=(size_t)snprintf(out+used,capacity-used,"%s\n",exported);}
        TEST(used<capacity-1);
    }
    world_hash_reader=sample_read;
}
static void reader_parity(void) {
    static char by_system_call[65536],guarded[65536];
    uint64_t before=read_local_calls;reader_census_text(read_local,by_system_call,sizeof(by_system_call));
    uint64_t system_calls=read_local_calls-before;before=read_local_calls;
    reader_census_text(sample_read,guarded,sizeof(guarded));uint64_t pin_calls=read_local_calls-before;
    /* Only the per-capture code checks (two entry prefixes and the layout pins) still use
     * ReadProcessMemory; every object read goes through the guarded copy. */
    const uint64_t per_capture=2+sizeof(world_nav_pins)/sizeof(*world_nav_pins);
    TEST(pin_calls==3*per_capture && system_calls>pin_calls+3*20 && !strcmp(by_system_call,guarded));
    TEST(strstr(guarded,"WORLD_OBJECT tick=1 id=000000000000004d ") && strstr(guarded," nav_raw=") &&
         strstr(guarded,"WORLD_OBJECT tick=2 id=000000000000004d ") && strstr(guarded,"WORLD_OBJECT tick=3 id=000000000000004d ") &&
         strstr(guarded,"domain=timing bytes="));
}
static void exact_image(const char *path) {
    FILE *f=fopen(path,"rb");TEST(f);TEST(!fseek(f,0,SEEK_END));long length=ftell(f);TEST(length==55412768);rewind(f);
    unsigned char *file=malloc((size_t)length);TEST(file && fread(file,1,(size_t)length,f)==(size_t)length);fclose(f);
    TEST(hash_ok(file,(ULONG)length,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)file;IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(file+dos->e_lfanew);
    unsigned char *mapped=VirtualAlloc(NULL,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(mapped);
    memcpy(mapped,file,nt->OptionalHeader.SizeOfHeaders);IMAGE_SECTION_HEADER *sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) memcpy(mapped+sections[i].VirtualAddress,file+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    unsigned char *saved=image;image=mapped;TEST(world_nav_layout_pins());
    const unsigned entries[]={0xea4c00,0xe85360};
    for(unsigned i=0;i<2;i++) for(unsigned byte=0;byte<8;byte++) {
        mapped[entries[i]+byte]^=1;TEST(!world_nav_layout_pins());mapped[entries[i]+byte]^=1;
    }
    for(unsigned i=0;i<sizeof(world_nav_pins)/sizeof(*world_nav_pins);i++) {
        unsigned offsets[]={world_nav_pins[i].rva,world_nav_pins[i].rva+world_nav_pins[i].length-1};
        for(unsigned j=0;j<2;j++) {mapped[offsets[j]]^=1;TEST(!world_nav_layout_pins());mapped[offsets[j]]^=1;}
    }
    /* Verify compatibility with existing immutable thunks on an owned PE copy.
     * These synthetic entry words are never executed or installed into X4. */
    unsigned char *old_thunk=thunk;thunk=make_thunk();TEST(thunk);
    unsigned char prefixes[2][8];const unsigned dispatch[]={2848,3360};
    for(unsigned i=0;i<2;i++) {
        memcpy(prefixes[i],mapped+entries[i],8);
        intptr_t distance=(intptr_t)(thunk+dispatch[i])-(intptr_t)(mapped+entries[i]+5);
        TEST(distance>=INT32_MIN && distance<=INT32_MAX);int32_t relative=(int32_t)distance;
        mapped[entries[i]]=0xe9;memcpy(mapped+entries[i]+1,&relative,4);
    }
    TEST(world_nav_layout_pins());
    for(unsigned i=0;i<2;i++) {mapped[entries[i]+1]^=1;TEST(!world_nav_layout_pins());mapped[entries[i]+1]^=1;}
    for(unsigned i=0;i<2;i++) memcpy(mapped+entries[i],prefixes[i],8);
    thunk=old_thunk;
    TEST(world_nav_layout_pins());production_census();reader_parity();image=saved;VirtualFree(mapped,0,MEM_RELEASE);free(file);
}
int main(int argc,char **argv) {
    image=VirtualAlloc(NULL,0x6fa9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(image);
    world_identities=VirtualAlloc(NULL,sizeof(*world_identities)*WORLD_IDENTITY_CAPACITY,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(world_identities);
    TEST(world_identity_record(101,1001) && world_identity_record(102,1002) && world_identity_record(103,1003) && world_identity_record(901,1001));
    semantics();failures();production_record();exports(argc==3?argv[2]:NULL);if(argc>=2) exact_image(argv[1]);
    printf("PASS World Nav V2: %u checks; owned memory and image bytes only\n",checks);return 0;
}
