/* Loaded-DLL fixture for production Load/configuration/ACK functions. No game
 * process or engine constructor executes. Every CHECK is active at -O2. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <string.h>
static void creation_load_acquire(PSRWLOCK lock);
static int creation_load_memcmp(const void *a,const void *b,size_t size);
#define AcquireSRWLockExclusive creation_load_acquire
#define memcmp creation_load_memcmp
#include "test_lockstep_registry_creation.c"
#undef AcquireSRWLockExclusive
#undef memcmp
static unsigned load_stop_before_registry,load_input,load_registry,load_ack_mode;
static unsigned load_progress_armed,load_progress_count;
static const char plain2[]=CREATION_RNG_BOOTSTRAP CREATION_RNG_PREFIX "1" CREATION_RNG_SUFFIX CREATION_RNG_ACK2_SUFFIX;
static const char registry2[]=CREATION_RNG_BOOTSTRAP CREATION_RNG_PREFIX "1" CREATION_RNG_SUFFIX CREATION_RNG_ACK2_SUFFIX REGISTRY_CREATION_SUFFIX;
static const char *load_preimage;
static char load_manifest_input[512],load_preimage_input[1024],load_marker_input[256];
static char load_token_input[33],load_digest_input[65];
static unsigned load_slot_input,load_port_input;static uint64_t load_options_input;
static unsigned load_nil_count;
static int creation_load_memcmp(const void *a,const void *b,size_t size) {
    int result=memcmp(a,b,size);
    if(load_progress_armed && a==creation_attach_boot.preimage) {
        load_progress_armed=0;++load_progress_count;
        InterlockedExchange64((volatile LONG64*)&stats.issued,1);
    }
    return result;
}
static void creation_load_acquire(PSRWLOCK lock) {
    if(load_stop_before_registry && lock==&registry_creation_lock && GetCurrentThreadId()==ui_thread) {
        load_stop_before_registry=0;x4gate_stop();CHECK(!stats.active);
    }
    AcquireSRWLockExclusive(lock);
}
static const char *creation_load_string(lua_State *state,int index,size_t *length) {
    (void)state;const char *s=load_input==1?(index==3?load_token_input:load_digest_input):load_input==8?CREATION_MANIFEST:
        load_input==2?(index==1?load_marker_input:index==2?load_manifest_input:load_preimage_input):index==1?load_manifest_input:load_preimage_input;
    if(length)*length=strlen(s);return s;
}
static double creation_load_number(lua_State *state,int index) {
    (void)state;return index==1?load_slot_input:index==2?load_port_input:index==4?60000:(double)load_options_input;
}
static void creation_load_nil(lua_State *state) {(void)state;++load_nil_count;}
static void creation_load_lua(void) {
    p_lua_tolstring=creation_load_string;p_lua_tonumber=creation_load_number;
    p_lua_pushnumber=rc_lua_number;p_lua_pushlstring=ack_lua_push;p_lua_pushnil=creation_load_nil;
}
static void creation_load_reset(unsigned registry,unsigned ack_mode) {
    CHECK(registry<=1&&(ack_mode==1||ack_mode==2));rc_clear();player_fire_enabled=0;reset_before_activity();
    creation_rng_started=0;creation_rng_acks=0;creation_rng_ack_mode=0;creation_rng_ack_receipt[0]=0;
    creation_rng_boot[0]=creation_rng_boot_secret[0]=creation_rng_boot_digest[0]=0;
    memset(creation_rng_keys,0,sizeof(creation_rng_keys));memset(&creation_attach_boot,0,sizeof(creation_attach_boot));
    creation_attach_rejected=0;registry_origin_closing=0;registry_origin_current=NULL;
    registry_origin_open=registry_origin_restore_open=registry_origin_peak=0;
    load_registry=registry;load_ack_mode=ack_mode;load_stop_before_registry=0;
    load_progress_armed=load_progress_count=0;
    load_preimage=ack_mode==2?(registry?registry2:plain2):(registry?rc_preimage:preimage);
    strcpy(initial_state_digest,ack_mode==2?(registry?"12674fe96e5dd2db165934cb30fc50d88ee5047c5036cbba38738ef127e29586":
        "792966722d11de00bbd39f887142ad17e73e9ff8c4cbd248c6c29f13c1c0286a"):
        registry?"91b30261372f883f52286bd23a19ccbabd2c03b60b7d6627de662c731af6bf85":
        "1ce03def6a724e38f50bb9f32096f7829a91f0f47b32147153b1d2eeca1a79ba");
    creation_load_lua();CHECK(hash_ok(load_preimage,strlen(load_preimage),initial_state_digest));
    strcpy(load_manifest_input,manifest);strcpy(load_preimage_input,load_preimage);strcpy(load_marker_input,REGISTRY_CREATION_MANIFEST);
    strcpy(load_token_input,secret);strcpy(load_digest_input,initial_state_digest);
    load_slot_input=stats.slot;load_port_input=remote_port;load_options_input=boot_options;
}
static int creation_load_call_inner(unsigned operation) {
    ack_push_count=load_nil_count=rc_lua_pushes=0;rc_lua_value=-99;load_input=operation;
    if(operation==1){CHECK(lua_boot(NULL)==1);return (int)rc_lua_value;}
    if(operation==2){CHECK(lua_registry_creation_config(NULL)==1);return (int)rc_lua_value;}
    if(operation==3){int n=lua_creation_rng_config(NULL);CHECK(n==1||n==2);return n==2&&ack_push_count==2&&!load_nil_count;}
    if(operation==4){CHECK(lua_load_requested(NULL)==1);return (int)rc_lua_value;}
    if(operation==5){boot_phase=boot_next(boot_phase,1);return boot_phase;}
    if(operation==6){boot_phase=boot_next(boot_phase,0);return boot_phase;}
    if(operation==7){x4gate_stop();return !stats.active;}
    if(operation==8){CHECK(lua_creation_trace_config(NULL)==1);return (int)rc_lua_value;}
    CHECK(0);return 0;
}
static void creation_load_initial(void) {
    CHECK(creation_load_call_inner(1)==0);
    if(load_registry)CHECK(creation_load_call_inner(2)==1);
    CHECK(creation_load_call_inner(3)==1&&creation_rng_acks==1);
}
static int creation_load_no_random(unsigned char bytes[16]) {(void)bytes;return 0;}
static DWORD WINAPI creation_load_foreign_worker(LPVOID unused) {
    (void)unused;return (DWORD)creation_rng_configure(manifest,sizeof(manifest)-1,load_preimage,strlen(load_preimage));
}
static void creation_load_mutation(unsigned which) {
    switch(which) {
    case 1:stats.issued=1;break;case 2:creation_selected.state=1;break;case 3:creation_count=1;break;
    case 4:creation_open=1;break;case 5:creation_rng_count=1;break;case 6:creation_rng_active=1;break;
    case 7:creation_rng_attempted=1;break;case 8:creation_rng_restore_failures=1;break;case 9:creation_rng_restore_open=1;break;
    case 10:creation_failures=1;break;case 11:registry_failures=1;break;case 12:registry_md_failures=1;break;
    case 13:registry_origin_failures=1;break;case 14:creation_closing=1;break;case 15:creation_rng_closing=1;break;
    case 16:stats.active=0;break;case 17:used=0;break;case 18:stats.slot=2;break;case 19:++remote_port;break;
    case 20:secret[0]^=1;break;case 21:initial_state_digest[0]^=1;break;case 22:boot_options^=1;break;
    case 23:++boot_started;break;case 24:++ui_thread;break;case 25:creation_rng_boot[0]^=1;break;
    case 26:creation_attach_boot.nonce[0]^=1;break;case 27:creation_rng_manifest[0]^=1;break;case 28:++creation_rng_epoch;break;
    case 29:creation_attach_boot.options^=1;break;case 30:++creation_attach_boot.image;break;case 31:boot_mode=0;break;
    case 32:creation_rng_ack_mode=1;break;case 33:creation_rng_failures=1;break;case 34:creation_restore_failures=1;break;
    case 35:creation_restore_open=1;break;case 36:creation_attach_boot.registry^=1;break;case 37:++creation_attach_boot.slot;break;
    case 38:creation_attach_boot.secret[0]^=1;break;case 39:creation_attach_boot.digest[0]^=1;break;case 40:creation_attach_boot.preimage[0]^=1;break;
    case 41:++creation_attach_boot.thread;break;case 42:++creation_attach_boot.started;break;case 43:creation_attach_boot.manifest[0]^=1;break;
    case 44:creation_attach_boot.valid=0;break;case 45:boot_phase=0;break;case 46:boot_phase=4;break;case 47:boot_phase=-1;break;
    case 48:creation_rng_ack_phase=3;break;case 49:flight_seeded=0;break;case 50:player_fire_enabled=1;break;
    case 51:registry_creation_sequence=1;break;case 52:registry_creation_open=1;break;case 53:registry_creation_identity_open=1;break;
    case 54:registry_creation_failures=1;break;case 55:registry_creation_closing=1;break;case 56:registry_creation_ack_mode=1;break;
    case 57:++registry_creation_acks;break;case 58:registry_creation_ack_phase=3;break;case 59:registry_creation_rng_manifest[0]^=1;break;
    case 60:registry_creation_digest[0]^=1;break;
    case 61:strcpy(load_preimage_input,load_registry?rc_preimage:preimage);break;
    case 62:strcat(load_preimage_input,CREATION_RNG_ACK2_SUFFIX);break;
    case 63:load_manifest_input[sizeof(CREATION_RNG_PREFIX)-1]='2';break;
    case 64:load_marker_input[0]^=1;break;
    default:CHECK(0);
    }
}
static void creation_load_mutations(void) {
    for(unsigned registry=0;registry<=1;registry++)for(unsigned stage=0;stage<=(registry?1u:0u);stage++)for(unsigned which=1;which<=64;which++) {
        if(!registry && ((which>=51&&which<=60)||which==64))continue;
        if(stage&&which==64)continue;
        creation_load_reset(registry,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
        CHECK(creation_load_call_inner(5)==2);if(stage)CHECK(creation_load_call_inner(2)==1);creation_load_mutation(which);
        unsigned rng_acks=creation_rng_acks,registry_acks=registry_creation_acks;
        uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));char nonce[33];memcpy(nonce,creation_rng_boot,sizeof(nonce));
        CHECK(creation_load_call_inner(registry&&!stage?2:3)==0);
        CHECK(creation_rng_acks==rng_acks&&registry_creation_acks==registry_acks);
        CHECK(!memcmp(keys,creation_rng_keys,sizeof(keys))&&!memcmp(nonce,creation_rng_boot,sizeof(nonce)));
    }
}
static void creation_load_guards(void) {
    for(unsigned registry=0;registry<=1;registry++) {
        creation_load_reset(registry,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
        CHECK(creation_load_call_inner(5)==2);CHECK(creation_load_call_inner(6)==3);
        if(registry)CHECK(creation_load_call_inner(2)==1);
        uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));char receipt[384];strcpy(receipt,creation_rng_ack_receipt);
        load_progress_armed=1;CHECK(creation_load_call_inner(3)==0);
        CHECK(load_progress_count==1&&stats.issued==1&&creation_rng_acks==1);
        CHECK(registry_creation_acks==(registry?2u:0u));CHECK(!memcmp(keys,creation_rng_keys,sizeof(keys)));
        CHECK(!strcmp(receipt,creation_rng_ack_receipt)&&creation_attach_rejected&&!stats.active);
    }
    for(unsigned registry=0;registry<=1;registry++)for(unsigned phase=1;phase<=3;phase++) {
        creation_load_reset(registry,2);creation_load_initial();char boot[33];strcpy(boot,creation_rng_boot);
        uint64_t keys[2];memcpy(keys,creation_rng_keys,sizeof(keys));CHECK(creation_load_call_inner(4)==1&&boot_phase==1);
        if(phase>=2)CHECK(creation_load_call_inner(5)==2);if(phase==3)CHECK(creation_load_call_inner(6)==3);
        CHECK(creation_load_call_inner(1)==0);if(registry)CHECK(creation_load_call_inner(2)==1);
        CHECK(creation_load_call_inner(3)==1&&creation_rng_acks==2&&creation_rng_ack_phase==phase);
        CHECK(!strcmp(boot,creation_rng_boot)&&!memcmp(keys,creation_rng_keys,sizeof(keys)));
        CHECK(registry_creation_acks==(registry?2u:0u)&&!creation_count&&!registry_creation_sequence);
    }
    for(unsigned registry=0;registry<=1;registry++) {
        creation_load_reset(registry,1);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
        CHECK(creation_load_call_inner(registry?2:3)==0);CHECK(!stats.active&&creation_rng_acks==1);
    }
    creation_load_reset(1,2);CHECK(creation_load_call_inner(2)==1&&registry_creation_acks==1);
    CHECK(creation_load_call_inner(4)==1);CHECK(creation_load_call_inner(3)==0&&!creation_rng_started);
    CHECK(registry_creation_acks==1&&!creation_rng_acks);
    creation_load_reset(0,2);CHECK(creation_load_call_inner(4)==1);
    CHECK(creation_load_call_inner(3)==0&&!creation_rng_started&&!creation_attach_boot.valid);
    creation_load_reset(1,2);CHECK(creation_load_call_inner(3)==0&&!creation_rng_started&&!registry_creation_acks);
    creation_load_reset(1,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
    CHECK(creation_load_call_inner(2)==1&&registry_creation_acks==2);CHECK(creation_load_call_inner(2)==0);
    CHECK(registry_creation_acks==2&&creation_rng_acks==1);
    creation_load_reset(1,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
    CHECK(creation_load_call_inner(2)==1);CHECK(creation_load_call_inner(7)==1);
    CHECK(creation_load_call_inner(3)==0&&creation_rng_acks==1&&registry_creation_acks==2);
    char half_footer[384];CHECK(creation_rng_ack_status(half_footer,sizeof(half_footer)));
    CHECK(strstr(half_footer,"acks=1 registry_acks=2")&&strstr(half_footer,"failed=1"));
    creation_load_reset(1,2);CHECK(creation_load_call_inner(2)==1);
    CHECK(!creation_rng_configure(manifest,sizeof(manifest)-1,plain2,sizeof(plain2)-1));
    CHECK(registry_creation_acks==1&&!creation_rng_started&&!creation_rng_acks);
    creation_load_reset(1,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
    CHECK(creation_load_call_inner(2)==1);CHECK(creation_load_call_inner(5)==2);
    CHECK(creation_load_call_inner(3)==1&&creation_rng_ack_registry_phase==1&&creation_rng_ack_phase==2);
    creation_load_reset(1,2);load_stop_before_registry=1;CHECK(creation_load_call_inner(2)==0);
    CHECK(!load_stop_before_registry&&!stats.active&&creation_attach_boot.valid&&!registry_creation_acks&&!creation_rng_acks);
    creation_load_reset(0,2);AcquireSRWLockExclusive(&creation_lock);
    CHECK(!creation_attach_prepare_using(manifest,sizeof(manifest)-1,plain2,sizeof(plain2)-1,1,0,creation_load_no_random));
    CHECK(!creation_attach_boot.valid);ReleaseSRWLockExclusive(&creation_lock);
    creation_load_reset(0,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
    HANDLE foreign=CreateThread(NULL,0,creation_load_foreign_worker,NULL,0,NULL);CHECK(foreign);
    CHECK(WaitForSingleObject(foreign,5000)==WAIT_OBJECT_0);DWORD result=99;CHECK(GetExitCodeThread(foreign,&result)&&!result);
    CHECK(CloseHandle(foreign));CHECK(creation_rng_acks==1&&!creation_attach_rejected);
    CHECK(creation_load_call_inner(3)==1);
    for(unsigned registry=0;registry<=1;registry++) {
        creation_load_reset(registry,2);creation_load_initial();CHECK(creation_load_call_inner(4)==1);
        for(unsigned ordinal=2;ordinal<=8;ordinal++){if(registry)CHECK(creation_load_call_inner(2)==1);CHECK(creation_load_call_inner(3)==1);}
        CHECK(creation_load_call_inner(registry?2:3)==0&&creation_rng_acks==8&&registry_creation_acks==(registry?8u:0u));
    }
    creation_load_mutations();
}
__declspec(dllexport) int creation_load_open(const char *path) {
    CHECK(!image);load_image(path);tls=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    replacement=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(tls&&replacement);
    tls_slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);CHECK(tls_slots);old_tls=tls_slots[0];
    creation_load_guards();tls_slots[0]=old_tls;
    printf("Construction Load production lifecycle: %u active checks PASS\n",checks);return 0;
}
__declspec(dllexport) void creation_load_fixture(unsigned registry,unsigned ack_mode) {
    void *prior=tls_slots[0];creation_load_reset(registry,ack_mode);tls_slots[0]=prior;
}
__declspec(dllexport) int creation_load_call(unsigned operation) {
    void *prior=tls_slots[0];tls_slots[0]=tls;int result=creation_load_call_inner(operation);tls_slots[0]=prior;return result;
}
__declspec(dllexport) void creation_load_inputs(const char *m,const char *p,const char *r) {
    CHECK(m&&p&&r&&strlen(m)<sizeof(load_manifest_input)&&strlen(p)<sizeof(load_preimage_input)&&strlen(r)<sizeof(load_marker_input));
    strcpy(load_manifest_input,m);strcpy(load_preimage_input,p);strcpy(load_marker_input,r);
}
__declspec(dllexport) void creation_load_boot_inputs(unsigned slot,unsigned port,const char *token,const char *digest,uint64_t options) {
    CHECK(token&&digest&&strlen(token)<sizeof(load_token_input)&&strlen(digest)<sizeof(load_digest_input));
    load_slot_input=slot;load_port_input=port;load_options_input=options;strcpy(load_token_input,token);strcpy(load_digest_input,digest);
}
__declspec(dllexport) const char *creation_load_output(unsigned part) {
    static char footer[1536];if(part<2)return ack_pushes[part];
    if(part==2){CHECK(creation_rng_ack_status(footer,sizeof(footer)));return footer;}
    if(part==3){CHECK(creation_rng_status(footer,sizeof(footer)));return footer;}
    if(part==4){CHECK(registry_creation_status(footer,sizeof(footer)));return footer;}
    if(part==5){CHECK(creation_status(footer,sizeof(footer)));return footer;}
    CHECK(0);return NULL;
}
__declspec(dllexport) int creation_load_force_fail(void) {CHECK(0);return 0;}
