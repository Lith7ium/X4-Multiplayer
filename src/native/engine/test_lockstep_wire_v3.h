/* Protocol 3: N-client blob permits, the Lua proposal seal and s.inputs.
 * Pure parsing plus the Lua entry points through fake API pointers; no
 * engine hooks, sockets or native simulation are exercised here. */
static double wire3_numbers[8];
static const char *wire3_strings[8];
static double wire3_pushed;
static unsigned wire3_pushes;
static char wire3_log[4096];
static void wire3_append(const char *text) {
    size_t n=strlen(wire3_log);assert(n+strlen(text)<sizeof(wire3_log));strcpy(wire3_log+n,text);
}
static double wire3_number(lua_State *L,int index) {(void)L;assert(index>=1 && index<=7);return wire3_numbers[index];}
static const char *wire3_string(lua_State *L,int index,size_t *length) {
    (void)L;assert(index>=1 && index<=7);const char *s=wire3_strings[index];
    if(s && length) *length=strlen(s);
    return s;
}
static void wire3_push(lua_State *L,double value) {
    (void)L;char text[32];snprintf(text,sizeof(text),"n%g;",value);wire3_append(text);wire3_pushed=value;++wire3_pushes;
}
static void wire3_pushstring(lua_State *L,const char *s,size_t length) {
    (void)L;char text[GATE_BLOB_HEX_MAX+16];assert(length<=GATE_BLOB_HEX_MAX && strlen(s)==length);
    snprintf(text,sizeof(text),"s%u:%s;",(unsigned)length,s);wire3_append(text);
}
static void wire3_table(lua_State *L,int array,int record) {
    (void)L;char text[32];snprintf(text,sizeof(text),"t%d,%d;",array,record);wire3_append(text);
}
static void wire3_field(lua_State *L,int index,const char *name) {
    (void)L;assert(index==-2);wire3_append("f");wire3_append(name);wire3_append(";");
}
static void wire3_rawseti(lua_State *L,int index,int n) {
    (void)L;char text[32];assert(index==-2);snprintf(text,sizeof(text),"i%d;",n);wire3_append(text);
}
static double wire3_call(int (*fn)(lua_State *),double number,const char *second,const char *third) {
    wire3_numbers[1]=number;wire3_strings[1]=NULL;wire3_strings[2]=second;wire3_strings[3]=third;
    unsigned pushes=wire3_pushes;assert(fn(NULL)==1 && wire3_pushes==pushes+1);return wire3_pushed;
}
static double wire3_configure(double version,double clients) {
    wire3_numbers[1]=version;wire3_numbers[2]=clients;
    unsigned pushes=wire3_pushes;assert(lua_wire(NULL)==1 && wire3_pushes==pushes+1);return wire3_pushed;
}
static void wire3_keep(char blobs[][GATE_BLOB_HEX_MAX+1]) {for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) strcpy(blobs[i],"keep");}
static int wire3_kept(char blobs[][GATE_BLOB_HEX_MAX+1]) {
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) if(strcmp(blobs[i],"keep")) return 0;
    return 1;
}
static void test_wire_v3_parse(void) {
    char big[GATE_BLOB_HEX_MAX+3];memset(big,'f',sizeof(big)-1);big[sizeof(big)-1]=0;
    assert(gate_blob_valid("",0) && gate_blob_valid("00",2) && gate_blob_valid("0a1bff",6));
    assert(gate_blob_valid(big+2,GATE_BLOB_HEX_MAX) && !gate_blob_valid(big,GATE_BLOB_HEX_MAX+2));
    assert(!gate_blob_valid("0",1) && !gate_blob_valid("0A",2) && !gate_blob_valid("0g",2) && !gate_blob_valid("0|",2));
    assert(!gate_blob_valid(NULL,0) && !gate_blob_valid("0\0",2) && !gate_blob_valid("00 ",2));
    char blobs[GATE_CLIENTS_MAX][GATE_BLOB_HEX_MAX+1];const char *prefix="NGGO|3|test|2|61";
#define PERMIT(text,slot,clients,own) gate_permit_blobs(text,strlen(text),prefix,slot,clients,own,blobs)
    wire3_keep(blobs);
    assert(PERMIT("NGGO|3|test|2|61|0102||ff",2,3,"")==1);
    assert(!strcmp(blobs[0],"0102") && !strcmp(blobs[1],"") && !strcmp(blobs[2],"ff") && !strcmp(blobs[3],"keep"));
    wire3_keep(blobs);
    assert(PERMIT("NGGO|3|test|2|61|0102|aa|ff",2,3,"ab")==-1);  /* never accept a changed own proposal */
    assert(PERMIT("NGGO|3|test|2|61|0102|aa|ff",2,3,"")==-1);
    assert(PERMIT("NGGO|3|test|2|61|0102||ff",2,3,"aa")==-1);
    assert(!PERMIT("NGGO|3|test|2|62|0102||ff",2,3,""));          /* other tick */
    assert(!PERMIT("NGGO|3|test|2|611|0102||ff",2,3,""));
    assert(!PERMIT("NGGO|2|test|2|61|02010000|01070100",2,3,""));  /* protocol 2 permit */
    assert(!PERMIT("NGGO|3|test|2|61|0102|",2,3,""));              /* too few blobs */
    assert(!PERMIT("NGGO|3|test|2|61|0102|||ff",2,3,""));          /* too many blobs */
    assert(!PERMIT("NGGO|3|test|2|61|0102||ff|",2,3,""));
    assert(!PERMIT("NGGO|3|test|2|61",2,3,""));
    assert(!PERMIT("NGGO|3|test|2|61|0102||FF",2,3,""));           /* charset */
    assert(!PERMIT("NGGO|3|test|2|61|010||ff",2,3,""));            /* whole bytes */
    assert(!PERMIT("NGGO|3|test|2|61|0102| |ff",2,3,""));
    assert(!PERMIT("NGGO|3|test|2|61|0102||ff",4,3,""));           /* slot outside N */
    assert(!PERMIT("NGGO|3|test|2|61|0102|",2,1,"") && !PERMIT("NGGO|3|test|2|61|||||||||",2,9,""));
    char wide[GATE_PERMIT_MAX+64];
    int n=snprintf(wide,sizeof(wide),"%s|%s||ff",prefix,big);       /* 482 characters */
    assert(n>0 && !gate_permit_blobs(wide,(size_t)n,prefix,2,3,"",blobs));
    assert(!gate_permit_blobs("NGGO|3|test|2|61|0102||ff",GATE_PERMIT_MAX,prefix,2,3,"",blobs));
    assert(!gate_permit_blobs("NGGO|3|test|2|61|0102||ff\0",26,prefix,2,3,"",blobs));
    assert(wire3_kept(blobs));
    n=snprintf(wide,sizeof(wide),"%s|%s||ff",prefix,big+2);         /* exactly 480 characters */
    assert(n>0 && gate_permit_blobs(wide,(size_t)n,prefix,2,3,"",blobs)==1 && !strcmp(blobs[0],big+2));
    /* Eight maximal blobs (3,910 bytes) fit the native permit buffer. */
    const char *eight="NGGO|3|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|8|18446744073709551615";n=snprintf(wide,sizeof(wide),"%s",eight);
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) {
        wide[n++]='|';memset(wide+n,"0123456789abcdef"[i],GATE_BLOB_HEX_MAX);n+=GATE_BLOB_HEX_MAX;
    }
    wide[n]=0;assert(n==3910 && (size_t)n<GATE_PERMIT_MAX);
    char own[GATE_BLOB_HEX_MAX+1];memset(own,'7',GATE_BLOB_HEX_MAX);own[GATE_BLOB_HEX_MAX]=0;
    wire3_keep(blobs);
    assert(gate_permit_blobs(wide,(size_t)n,eight,8,8,own,blobs)==1);
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++)
        assert(strlen(blobs[i])==GATE_BLOB_HEX_MAX && blobs[i][0]=="0123456789abcdef"[i] && blobs[i][GATE_BLOB_HEX_MAX-1]==blobs[i][0]);
#undef PERMIT
    /* A maximal protocol 3 state report stays inside the native state buffer. */
    char state[GATE_STATE_MAX];
    n=snprintf(state,sizeof(state),"NGSTATE|3|%s|%u|%llu|%016llx|%016llx|%s|%u|%s","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",8u,
               18446744073709551615ULL,0ULL,0ULL,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",8u,own);
    assert(n==647 && (size_t)n<sizeof(state));
}
static void test_wire_v3_config(void) {
    const char *token="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char *hash="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    assert(!used && !installed && wire_version==2 && wire_clients==2 && wire_pair());
    assert(!x4gate_wire(3,1) && !x4gate_wire(3,9) && !x4gate_wire(2,3) && !x4gate_wire(4,3) && !x4gate_wire(1,2));
    assert(wire_version==2 && wire_clients==2);
    assert(x4gate_start(3,12345,token,1000,hash)==10);  /* protocol 2 admits two players only */
    assert(x4gate_wire(3,8) && wire_version==3 && wire_clients==8 && x4gate_wire(3,3) && wire_clients==3);
    assert(!wire_pair());                                /* docking/construction/GetUp/portable refuse */
    assert(x4gate_start(4,12345,token,1000,hash)==10 && x4gate_start(0,12345,token,1000,hash)==10);
    assert(x4gate_start(3,12345,token,1000,hash)==11);  /* slot 3 admitted; still never patches a non-X4 process */
    assert(!installed && !used);
    assert(x4gate_wire(3,2) && !wire_pair());            /* protocol 3 with two clients is not the legacy pair */
    used=1;
    assert(x4gate_wire(3,2) && !x4gate_wire(3,3) && !x4gate_wire(2,2) && wire_version==3 && wire_clients==2);
    used=0;
    /* Lua entry: integral arguments only; protocol 3 needs lua_rawseti for s.inputs. */
    double (*old_number)(lua_State *,int)=p_lua_tonumber;
    void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    void (*old_rawseti)(lua_State *,int,int)=p_lua_rawseti;
    p_lua_tonumber=wire3_number;p_lua_pushnumber=wire3_push;p_lua_rawseti=NULL;wire3_log[0]=0;
    assert(x4gate_wire(2,2) && wire3_configure(3,3)==0 && wire_version==2);
    p_lua_rawseti=wire3_rawseti;
    assert(wire3_configure(3,2.5)==0 && wire3_configure(3,NAN)==0 && wire3_configure(3,9)==0 && wire3_configure(2,3)==0);
    assert(wire3_configure(3,3)==1 && wire_version==3 && wire_clients==3);
    assert(wire3_configure(2,2)==1 && wire_version==2 && wire_clients==2);
    p_lua_tonumber=old_number;p_lua_pushnumber=old_push;p_lua_rawseti=old_rawseti;
}
static void test_wire_v3_lua(void) {
    gate_stats saved=stats;char saved_digest[sizeof(digest)];memcpy(saved_digest,digest,sizeof(digest));
    double (*old_number)(lua_State *,int)=p_lua_tonumber;
    const char *(*old_string)(lua_State *,int,size_t *)=p_lua_tolstring;
    void (*old_push)(lua_State *,double)=p_lua_pushnumber;
    void (*old_pushstring)(lua_State *,const char *,size_t)=p_lua_pushlstring;
    void (*old_table)(lua_State *,int,int)=p_lua_createtable;
    void (*old_field)(lua_State *,int,const char *)=p_lua_setfield;
    void (*old_rawseti)(lua_State *,int,int)=p_lua_rawseti;
    p_lua_tonumber=wire3_number;p_lua_tolstring=wire3_string;p_lua_pushnumber=wire3_push;
    p_lua_pushlstring=wire3_pushstring;p_lua_createtable=wire3_table;p_lua_setfield=wire3_field;p_lua_rawseti=wire3_rawseti;
    const char *h="cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    char big[GATE_BLOB_HEX_MAX+3];memset(big,'e',sizeof(big)-1);big[sizeof(big)-1]=0;
    wire_version=3;wire_clients=3;stats.active=1;stats.issued=5;stats.report_tick=4;ui_thread=GetCurrentThreadId();
    blob_proposal[0]=0;blob_staged_set=0;wire3_log[0]=0;
    /* Stage, then seal with the report; the sealed blob can never change. */
    assert(wire3_call(lua_propose,5,"0a0b",NULL)==1 && blob_staged_set && blob_staged_tick==5);
    assert(wire3_call(lua_propose,6,"0a0b",NULL)==0 && wire3_call(lua_propose,5,"0A0B",NULL)==0);
    assert(wire3_call(lua_propose,5,big,NULL)==0 && wire3_call(lua_propose,5,"abc",NULL)==0 && wire3_call(lua_propose,5,NULL,NULL)==0);
    assert(wire3_call(lua_propose,5,"0a0b",NULL)==1);
    assert(wire3_call(lua_report,5,h,"0c")==0 && stats.report_tick==4);  /* disagrees with the staged blob */
    assert(wire3_call(lua_report,5,h,NULL)==1 && !strcmp(blob_proposal,"0a0b") && stats.report_tick==5);
    assert(wire3_call(lua_propose,5,"0a0b",NULL)==1 && wire3_call(lua_propose,5,"0c",NULL)==0);
    assert(wire3_call(lua_report,5,h,"0c")==0 && wire3_call(lua_report,5,h,"0a0b")==1 && wire3_call(lua_report,5,h,NULL)==1);
    assert(!strcmp(blob_proposal,"0a0b"));
    stats.issued=6;
    assert(wire3_call(lua_report,6,h,"abc")==0 && wire3_call(lua_report,6,h,"ABCD")==0 && wire3_call(lua_report,6,h,big)==0);
    assert(wire3_call(lua_report,7,h,NULL)==0 && stats.report_tick==5);
    assert(wire3_call(lua_report,6,h,NULL)==1 && blob_proposal[0]==0 && stats.report_tick==6);  /* no staged blob: empty */
    stats.issued=7;
    assert(wire3_call(lua_propose,7,"01",NULL)==1 && wire3_call(lua_report,7,h,"02")==0);
    assert(wire3_call(lua_report,7,h,"01")==1 && !strcmp(blob_proposal,"01"));
    stats.issued=8;
    assert(wire3_call(lua_report,8,h,big+2)==1 && strlen(blob_proposal)==GATE_BLOB_HEX_MAX);
    stats.active=0;stats.issued=9;
    assert(wire3_call(lua_propose,9,"01",NULL)==0 && wire3_call(lua_report,9,h,"01")==0);
    stats.active=1;wire_version=2;wire_clients=2;
    assert(wire3_call(lua_propose,9,"01",NULL)==0);    /* protocol 2 has no blobs */
    /* s.inputs: protocol 3 blobs in slot order, no input1/input2. */
    wire_version=3;wire_clients=3;strcpy(blob_inputs[0],"0102");blob_inputs[1][0]=0;strcpy(blob_inputs[2],"ff");
    wire3_log[0]=0;push_inputs(NULL);
    assert(!strcmp(wire3_log,"n3;fwire_version;n3;fclients;t3,0;s4:0102;i1;s0:;i2;s2:ff;i3;finputs;"));
    /* Protocol 2: unchanged input1/input2 plus the same commands as s.inputs. */
    wire_version=2;wire_clients=2;strcpy(inputs[0],"02010000");strcpy(inputs[1],"00000000");
    wire3_log[0]=0;push_inputs(NULL);
    assert(!strcmp(wire3_log,"n2;fwire_version;n2;fclients;s8:02010000;finput1;s8:00000000;finput2;"
                             "t2,0;s8:02010000;i1;s8:00000000;i2;finputs;"));
    p_lua_rawseti=NULL;wire3_log[0]=0;push_inputs(NULL);
    assert(!strcmp(wire3_log,"n2;fwire_version;n2;fclients;s8:02010000;finput1;s8:00000000;finput2;"));
    p_lua_tonumber=old_number;p_lua_tolstring=old_string;p_lua_pushnumber=old_push;p_lua_pushlstring=old_pushstring;
    p_lua_createtable=old_table;p_lua_setfield=old_field;p_lua_rawseti=old_rawseti;
    stats=saved;memcpy(digest,saved_digest,sizeof(digest));ui_thread=0;
    wire_version=2;wire_clients=2;blob_proposal[0]=blob_staged[0]=0;blob_staged_set=0;blob_staged_tick=0;
    for(unsigned i=0;i<GATE_CLIENTS_MAX;i++) blob_inputs[i][0]=0;
    strcpy(inputs[0],"00000000");strcpy(inputs[1],"00000000");
}
static void test_wire_v3(void) {test_wire_v3_parse();test_wire_v3_config();test_wire_v3_lua();}
