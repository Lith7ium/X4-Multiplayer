/* Executable owned-memory ABI/lifecycle tests. No native engine call executes. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uintptr_t address;SIZE_T length;BOOL ok;} test_read_entry;
static test_read_entry test_read_trace[4096];static unsigned test_read_count,test_read_enabled;
static const void *test_failed_read;
static unsigned test_fail_at=UINT32_MAX,test_partial_read,test_throw_at=UINT32_MAX;
static BOOL WINAPI test_read_api(HANDLE process,LPCVOID address,LPVOID out,SIZE_T length,SIZE_T *got) {
    BOOL ok;
    if(test_read_enabled && test_read_count==test_throw_at)RaiseException(0xe0424242,0,0,NULL);
    if((address==test_failed_read && address) || (test_read_enabled && test_read_count==test_fail_at)) {
        memset(out,0xcc,length);*got=test_partial_read?length-1:length;ok=test_partial_read?TRUE:FALSE;
    }
    else ok=ReadProcessMemory(process,address,out,length,got);
    if(test_read_enabled) {
        if(test_read_count<4096)test_read_trace[test_read_count++]=(test_read_entry){(uintptr_t)address,length,ok};
        else abort();
    }
    return ok;
}
static unsigned test_protect_calls,test_flush_calls;
static const WCHAR *test_process_path;
static BOOL WINAPI test_protect_api(LPVOID p,SIZE_T n,DWORD access,PDWORD old) {
    ++test_protect_calls;return VirtualProtect(p,n,access,old);
}
static BOOL WINAPI test_flush_api(HANDLE process,LPCVOID p,SIZE_T n) {
    ++test_flush_calls;return FlushInstructionCache(process,p,n);
}
static DWORD WINAPI test_module_path(HMODULE module,LPWSTR path,DWORD size) {
    if(!test_process_path) return GetModuleFileNameW(module,path,size);
    size_t n=wcslen(test_process_path);if(n>=size) return size;
    memcpy(path,test_process_path,(n+1)*sizeof(WCHAR));return (DWORD)n;
}
#define VirtualProtect test_protect_api
#define FlushInstructionCache test_flush_api
#define GetModuleFileNameW test_module_path
#define ReadProcessMemory test_read_api
#include "lockstep_gate.c"
#undef ReadProcessMemory
#undef VirtualProtect
#undef FlushInstructionCache
#undef GetModuleFileNameW
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static __declspec(align(16)) unsigned char test_table[0x200],test_client[0x400],test_module[0x200],test_manager[0x200];
static __declspec(align(16)) unsigned char test_agent[0x1100],test_xml[0x200];
static unsigned char *current_agent=test_agent,*current_movement;
static unsigned char *cohort_agents,*cohort_xml;
static uintptr_t arguments[5],tail_result=UINT64_C(0x8877665544332211),load_result=UINT64_C(0x123456789abcde01);
static unsigned original_loads,original_tails,test_mode,test_iterations;
static unsigned failure_at_original[4];
static unsigned char after_original[0x1a0];
static unsigned tests;
static RUNTIME_FUNCTION test_unwind[3];
static HANDLE test_entered,test_resume,test_stopped;
static char lua_text[2048];static double lua_number;
static const char *lua_arguments[4];static size_t lua_lengths[4];
static int reattach(void);
#define TEST(x) do {++tests;assert(x);} while(0)
static int reattach(void) {
    unsigned protects=test_protect_calls,flushes=test_flush_calls;
    uint64_t serial=rotation_serial,calls=rotation_load_calls,returns=rotation_load_returns;
    uint64_t tails=rotation_tail_calls,tail_returns=rotation_tail_returns;
    unsigned phase=rotation_phase,open=rotation_load_open,scope=rotation_scope_open;DWORD owner=rotation_owner;
    rotation_patch patches[2];memcpy(patches,rotation_patches,sizeof(patches));
    uint64_t selected[2],role_returns[2],written[2];
    unsigned lifetimes=rotation_lifetime_count,paired=rotation_paired;
    uint64_t class_calls[2][2],class_returns[2][2],class_written[2][2];
    memcpy(class_calls,rotation_class_calls,sizeof class_calls);memcpy(class_returns,rotation_class_returns,sizeof class_returns);
    memcpy(class_written,rotation_class_written,sizeof class_written);
    memcpy(selected,rotation_selected,sizeof(selected));memcpy(role_returns,rotation_role_returns,sizeof(role_returns));
    memcpy(written,rotation_written,sizeof(written));
    int ok=rotation_native_config(X4ROT_IMPORT_MODE,strlen(X4ROT_IMPORT_MODE),X4ROT_IMPORT_DESCRIPTOR_SHA256,64,
        X4ROT_IMPORT_SNAPSHOT_SHA256,64,X4ROT_IMPORT_CONTRACT_SHA256,64);
    TEST(test_protect_calls==protects && test_flush_calls==flushes);
    TEST(rotation_serial==serial && rotation_load_calls==calls && rotation_load_returns==returns &&
        rotation_tail_calls==tails && rotation_tail_returns==tail_returns && rotation_phase==phase &&
        rotation_owner==owner && rotation_load_open==open && rotation_scope_open==scope);
    TEST(!memcmp(patches,rotation_patches,sizeof(patches)) && !memcmp(selected,rotation_selected,sizeof(selected)) &&
        !memcmp(role_returns,rotation_role_returns,sizeof(role_returns)) && !memcmp(written,rotation_written,sizeof(written)));
    TEST(lifetimes==rotation_lifetime_count && paired==rotation_paired &&
        !memcmp(class_calls,rotation_class_calls,sizeof class_calls) && !memcmp(class_returns,rotation_class_returns,sizeof class_returns) &&
        !memcmp(class_written,rotation_class_written,sizeof class_written));
    return ok;
}
static uintptr_t call_role(unsigned role,void *agent) {
    typedef uintptr_t (*entry_fn)(void *,void *,void *,void *);
    return ((entry_fn)(image+(role?0xe870ef:0xe86d06)))(current_agent+0xae0+role*0x1a0,test_xml,agent,current_movement);
}
static uintptr_t owned_tail(void *basis,void *xml) {
    if(original_tails<4)failure_at_original[original_tails]=(unsigned)rotation_failure;
    ++original_tails;TEST(xml==test_xml);
    memset((unsigned char*)basis-16,0x5a,0x1a0);memcpy(after_original,(unsigned char*)basis-16,sizeof(after_original));
    if(test_mode==2) RaiseException(0xe0424242,0,0,NULL);
    if(test_mode==3) rotation_native_stop();
    if(test_mode==4) test_xml[0x20]^=1;
    if(test_mode==5) *(void**)(test_client+0x2a0)=test_manager;
    if(test_mode==7) {TEST(SetEvent(test_entered));TEST(WaitForSingleObject(test_resume,5000)==WAIT_OBJECT_0);}
    if(test_mode==8) *(unsigned*)(image+0x2f4b6f4)=0;
    if(test_mode==9) *(unsigned*)(test_manager+0x40)=1;
    if(test_mode==10) test_manager[0x89]=0;
    if(test_mode==11) *(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=0;
    if(test_mode==13) current_movement[0x20]^=1;
    if(test_mode==14) *(void**)(current_movement+0xc8)=test_xml;
    return tail_result;
}
static uintptr_t owned_load(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    ++original_loads;arguments[0]=a;arguments[1]=b;arguments[2]=c;arguments[3]=d;arguments[4]=e;
    /* Explicit early-reset state: no import manager has been established yet. */
    if(!rotation_failure) TEST(rotation_phase==ROTATION_ENTERED && !rotation_loaded.manager && rotation_entry_manager==(uintptr_t)test_manager);
    else TEST(rotation_phase==ROTATION_FAILED);
    if(test_mode==1) RaiseException(0xe0424242,0,0,NULL);
    if(test_mode==12) {TEST(reattach());TEST(rotation_phase==ROTATION_ENTERED && rotation_load_open==1 && rotation_serial==1);}
    test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
    for(unsigned i=0;i<test_iterations;i++) {
        current_agent=cohort_agents+(size_t)i*0x1100;current_movement=cohort_xml+(size_t)i*0xc00;
        TEST(call_role(0,test_mode==6?test_module:current_agent)==tail_result);
        TEST(call_role(1,current_agent)==tail_result);
        if(rotation_failure) break;
    }
    test_manager[0x89]=0;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=0;
    return load_result;
}
static void protect_page(unsigned rva,DWORD access) {
    DWORD old;TEST(VirtualProtect(image+(rva&~4095u),4096,access,&old));
}
static void relative_call(unsigned rva,unsigned target) {
    image[rva]=0xe8;int32_t displacement=(int32_t)(target-rva-5);memcpy(image+rva+1,&displacement,4);
}
static void owned_code(void) {
    unsigned pages[]={ROTATION_LOAD_WORD,ROTATION_LOAD_TARGET,ROTATION_TAIL_WORD,ROTATION_TAIL_TARGET,0xe86d00,0xe870f0};
    for(unsigned i=0;i<sizeof(pages)/sizeof(*pages);i++) protect_page(pages[i],PAGE_READWRITE);
    for(unsigned i=0;i<2;i++) {
        unsigned site=i?0xe870fc:0xe86d13;unsigned char pre[]={0x57,0x41,0x55,0x48,0x83,0xec,0x28,0x49,0x8b,0xf8,0x4d,0x8b,0xe9};
        unsigned char post[]={0x48,0x83,0xc4,0x28,0x41,0x5d,0x5f,0xc3};
        memcpy(image+site-13,pre,sizeof(pre));relative_call(site,ROTATION_TAIL_SITE);memcpy(image+site+5,post,sizeof(post));
    }
    /* Move all five opaque arguments through an actual call at the pinned RVA.
       R11 forwards the incoming fifth stack argument into the outgoing slot. */
    unsigned char pre[]={0x48,0x83,0xec,0x28,0x4c,0x8b,0x5c,0x24,0x50,0x4c,0x89,0x5c,0x24,0x20};
    memcpy(image+ROTATION_LOAD_SITE-sizeof(pre),pre,sizeof(pre));relative_call(ROTATION_LOAD_SITE,ROTATION_LOAD_TARGET);
    unsigned char post[]={0x48,0x83,0xc4,0x28,0xc3};memcpy(image+ROTATION_LOAD_SITE+5,post,sizeof(post));
    image[ROTATION_TAIL_SITE]=0xe9;int32_t d=ROTATION_TAIL_TARGET-ROTATION_TAIL_SITE-5;
    memcpy(image+ROTATION_TAIL_SITE+1,&d,4);
    thunk_absolute_jump(image+ROTATION_LOAD_TARGET,(void*)owned_load);
    thunk_absolute_jump(image+ROTATION_TAIL_TARGET,(void*)owned_tail);
    for(unsigned i=0;i<sizeof(pages)/sizeof(*pages);i++) protect_page(pages[i],PAGE_EXECUTE_READ);
    TEST(FlushInstructionCache(GetCurrentProcess(),image,0x6fa9000));
}
static void owned_unwind(void) {
    const unsigned char load_info[8]={1,14,1,0,4,0x42,0,0};
    const unsigned char role_info[12]={1,13,3,0,7,0x42,3,0xd0,1,0x70,0,0};
    memcpy(image+0x1000,load_info,8);memcpy(image+0x1010,role_info,sizeof(role_info));
    test_unwind[0]=(RUNTIME_FUNCTION){ROTATION_LOAD_SITE-14,ROTATION_LOAD_SITE+10,0x1000};
    test_unwind[1]=(RUNTIME_FUNCTION){0xe86d06,0xe86d20,0x1010};
    test_unwind[2]=(RUNTIME_FUNCTION){0xe870ef,0xe87109,0x1010};
    TEST(RtlAddFunctionTable(test_unwind,3,(DWORD64)(uintptr_t)image));
}
static void cohort_setup(void) {
    cohort_agents=calloc(X4ROT_IMPORT_EULER_PER_BANK,0x1100);
    cohort_xml=calloc(X4ROT_IMPORT_EULER_PER_BANK,0xc00);TEST(cohort_agents && cohort_xml);
    for(unsigned i=0;i<X4ROT_IMPORT_EULER_PER_BANK;i++) {
        unsigned char *m=cohort_xml+(size_t)i*0xc00;
        const unsigned tags[6]={0x2b2,0x340,0x382,0x52f,0x2dc,0x2dc};
        for(unsigned j=0;j<6;j++) {
            unsigned char *n=m+j*0x200;*(unsigned*)(n+12)=tags[j];
            *(uint64_t*)(n+0xe8)=*(uint64_t*)(n+0xb8)=4;
        }
        const char *cls=i+1==X4ROT_IMPORT_EULER_PER_BANK?"player":"navigation";
        *(unsigned*)(m+0x18)=0xbb;memcpy(m+0x20,cls,strlen(cls)+1);
        *(uint64_t*)(m+0x30)=strlen(cls);*(uint64_t*)(m+0x38)=15;*(uint64_t*)(m+0xc0)=1;
        *(void**)(m+0xc8)=m+0x200;*(uint64_t*)(m+0xf0)=1;
        *(void**)(m+0x200+0xc8)=m+0x400;*(void**)(m+0x200+0xd0)=m+0x600;*(uint64_t*)(m+0x200+0xf0)=2;
        for(unsigned role=0;role<2;role++) {
            unsigned char *state=m+0x400+role*0x200,*offset=m+0x800+role*0x200;
            *(void**)(state+0xc8)=offset;*(uint64_t*)(state+0xf0)=1;
            *(void**)(offset+0xc8)=test_xml;*(uint64_t*)(offset+0xf0)=1;
        }
    }
}
static void reset_state(void) {
    rotation_native_restore();
    rotation_configured=rotation_stop=rotation_failure=0;rotation_phase=ROTATION_OFF;
    rotation_reject_mode=-1;memset(&rotation_reject_first,0,sizeof rotation_reject_first);memset(&rotation_reject_done,0,sizeof rotation_reject_done);
    memset(&rotation_reject_boot,0,sizeof rotation_reject_boot);
    rotation_restore_error=rotation_scope_open=rotation_load_open=0;
    rotation_serial=rotation_load_calls=rotation_load_returns=rotation_tail_calls=rotation_tail_returns=0;
    rotation_load_true=rotation_load_false=rotation_load_abnormal=0;
    memset(rotation_selected,0,sizeof(rotation_selected));memset(rotation_role_returns,0,sizeof(rotation_role_returns));
    memset(rotation_written,0,sizeof(rotation_written));rotation_foreign=0;
    memset(rotation_class_calls,0,sizeof(rotation_class_calls));memset(rotation_class_returns,0,sizeof(rotation_class_returns));
    memset(rotation_class_written,0,sizeof(rotation_class_written));memset(rotation_lifetimes,0,sizeof(rotation_lifetimes));
    memset(rotation_agent_index,0,sizeof(rotation_agent_index));memset(rotation_movement_index,0,sizeof(rotation_movement_index));
    rotation_lifetime_count=rotation_paired=0;current_agent=test_agent;current_movement=cohort_xml;
    memset(&rotation_loaded,0,sizeof(rotation_loaded));memset(&rotation_menu,0,sizeof(rotation_menu));
    memset(test_table,0,sizeof(test_table));memset(test_client,0,sizeof(test_client));memset(test_module,0,sizeof(test_module));
    memset(test_manager,0,sizeof(test_manager));memset(test_agent,0,sizeof(test_agent));memset(test_xml,0,sizeof(test_xml));
    original_loads=original_tails=0;test_mode=0;test_iterations=1;load_result=UINT64_C(0x123456789abcde01);
    memset(failure_at_original,0,sizeof failure_at_original);test_failed_read=NULL;
    *(void**)(image+TABLE_RVA)=test_table;*(void**)(image+ROTATION_CLIENT_GLOBAL)=test_client;
    *(void**)(image+ROTATION_MANAGER_GLOBAL)=test_manager;*(void**)test_client=image+ROTATION_CLIENT_VTABLE;
    *(void**)(image+ROTATION_CLIENT_VTABLE+8)=image+0x122b360;
    *(void**)(image+ROTATION_EP1_VTABLE+8)=image+0xab1100;
    *(void**)(image+ROTATION_MENU_VTABLE+8)=image+0xac4120;
    *(void**)(test_client+0x2a0)=test_module;*(void**)test_module=image+ROTATION_EP1_VTABLE;
    *(void**)(test_table+3*8)=test_client;
    *(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    *(unsigned*)(test_xml+12)=0x3e7;*(uint64_t*)(test_xml+0xe8)=4;
    *(uint64_t*)(test_xml+0xb8)=4;*(uint64_t*)(test_xml+0xc0)=1;
    *(unsigned*)(test_xml+0x18)=0x532;memcpy(test_xml+0x20,"90",3);
    *(uint64_t*)(test_xml+0x30)=2;*(uint64_t*)(test_xml+0x38)=15;
    stats.active=1;used=installed=1;boot_mode=1;boot_phase=1;ui_thread=GetCurrentThreadId();stats.issued=stats.completed=0;
    stats.slot=1;remote_port=47111;boot_options=0;boot_started=123456;
    strcpy(secret,"0123456789abcdef0123456789abcdef");
    strcpy(initial_state_digest,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    portable_native_limit=12;memset(portable_native_manifest,'a',203);portable_native_manifest[203]=0;
    portable_native_configured=1;strcpy(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256);
    TEST(rotation_identity_read(&rotation_menu,0));
    owned_code();
    for(unsigned i=0;i<2;i++) {
        rotation_patches[i].owned=0;TEST(rotation_prepare_patch(&rotation_patches[i]));int changed=0;
        TEST(rotation_exchange(&rotation_patches[i],rotation_patches[i].original,rotation_patches[i].patched,&changed) && changed);
        rotation_patches[i].owned=1;
    }
    rotation_phase=ROTATION_WAITING;rotation_configured=1;TEST(rotation_boot_read(&rotation_boot));
}
static uintptr_t run_load(void) {
    rotation_load_fn f=(rotation_load_fn)(image+ROTATION_LOAD_SITE-14);
    return f((uintptr_t)(image+ROTATION_GAME_THIS),UINT64_C(0x9988776655443322),UINT64_C(0x1122334455667788),
        UINT64_C(0xabc0000000000000),UINT64_C(0xdef0000000000000));
}
static DWORD WINAPI thread_load(void *unused) {(void)unused;(void)run_load();return 0;}
static DWORD WINAPI thread_stop(void *unused) {
    SetEvent(test_entered);if(unused) rotation_native_stop();else rotation_native_restore();SetEvent(test_stopped);return 0;
}
static void stop_interleavings(void) {
    test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);test_resume=CreateEventW(NULL,TRUE,FALSE,NULL);
    test_stopped=CreateEventW(NULL,TRUE,FALSE,NULL);TEST(test_entered && test_resume && test_stopped);
    /* STOP wins while the original is still running: it may finish once, but
       cannot publish a write or a completed correction. */
    reset_state();test_mode=7;HANDLE worker=CreateThread(NULL,0,thread_load,NULL,0,NULL);TEST(worker);
    TEST(WaitForSingleObject(test_entered,5000)==WAIT_OBJECT_0);rotation_native_stop();SetEvent(test_resume);
    TEST(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    TEST(original_loads==1 && original_tails==2 && !rotation_written[0] && !rotation_written[1] && rotation_failure);
    TEST(!rotation_scope_open && !rotation_load_open);rotation_native_restore();TEST(rotation_native_restored());
    /* Writer owns the exact production lock first. STOP cannot publish or
       restore either code word until the bounded write has completed. */
    reset_state();ResetEvent(test_entered);ResetEvent(test_stopped);
    TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
    test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;rotation_scope_open=1;
    rotation_call call={0};call.agent=test_agent;call.basis=test_agent+0xae0;call.xml=test_xml;call.accounted=1;
    struct x4rx_guard guard;struct x4rx_plan plan;memset(test_agent+0xad0,0x5a,0x1a0);
    for(unsigned i=0;i<9;i++) plan.words[i]=(struct x4rx_word){(uint16_t)(16+(i/3)*16+(i%3)*4),UINT32_C(0x5a5a5a5a),UINT32_C(0x3f000000)};
    AcquireSRWLockExclusive(&rotation_lock);TEST(rotation_guard_locked(&call,&guard));
    worker=CreateThread(NULL,0,thread_stop,NULL,0,NULL);TEST(worker);
    TEST(WaitForSingleObject(test_entered,5000)==WAIT_OBJECT_0);
    TEST(WaitForSingleObject(test_stopped,0)==WAIT_TIMEOUT && !rotation_stop);
    TEST(rotation_write_locked(&call,&plan) && rotation_written[0]==1 && !rotation_stop);
    TEST(rotation_patch_owned(&rotation_patches[0]) && rotation_patch_owned(&rotation_patches[1]));
    ReleaseSRWLockExclusive(&rotation_lock);
    TEST(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    TEST(rotation_stop && rotation_native_restored());
    /* STOP must also participate before the final configured store. Hold the
       real production lock at the pre-CAS point; no published ownership yet. */
    reset_state();rotation_native_restore();rotation_configured=rotation_stop=rotation_failure=0;
    rotation_phase=ROTATION_OFF;ResetEvent(test_entered);ResetEvent(test_stopped);
    AcquireSRWLockExclusive(&rotation_lock);
    worker=CreateThread(NULL,0,thread_stop,(void*)1,0,NULL);TEST(worker);
    TEST(WaitForSingleObject(test_entered,5000)==WAIT_OBJECT_0);
    TEST(WaitForSingleObject(test_stopped,50)==WAIT_TIMEOUT && !rotation_stop);
    for(unsigned i=0;i<2;i++) {
        TEST(rotation_prepare_patch(&rotation_patches[i]));int changed=0;
        TEST(rotation_exchange(&rotation_patches[i],rotation_patches[i].original,rotation_patches[i].patched,&changed) && changed);
        rotation_patches[i].owned=1;
    }
    rotation_configured=1;rotation_phase=ROTATION_WAITING;
    ReleaseSRWLockExclusive(&rotation_lock);
    TEST(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    TEST(rotation_stop);rotation_native_restore();TEST(rotation_native_restored());
    CloseHandle(test_entered);CloseHandle(test_resume);CloseHandle(test_stopped);
}
static void rollback_case(void) {
    reset_state();rotation_native_restore();rotation_configured=rotation_stop=rotation_failure=0;
    AcquireSRWLockExclusive(&rotation_lock);
    for(unsigned i=0;i<2;i++) TEST(rotation_prepare_patch(&rotation_patches[i]));
    int changed=0;TEST(rotation_exchange(&rotation_patches[0],rotation_patches[0].original,rotation_patches[0].patched,&changed));
    rotation_patches[0].owned=1;
    protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);image[ROTATION_TAIL_WORD]^=1;protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    unsigned __int128 foreign;memcpy(&foreign,image+ROTATION_TAIL_WORD,16);
    TEST(!rotation_exchange(&rotation_patches[1],rotation_patches[1].original,rotation_patches[1].patched,&changed) && !changed);
    rotation_restore_locked();TEST(!rotation_patches[0].owned && !rotation_patches[1].owned);
    unsigned __int128 actual;memcpy(&actual,image+ROTATION_LOAD_WORD,16);TEST(actual==rotation_patches[0].original);
    memcpy(&actual,image+ROTATION_TAIL_WORD,16);TEST(actual==foreign);
    protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);memcpy(image+ROTATION_TAIL_WORD,&rotation_patches[1].original,16);protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    ReleaseSRWLockExclusive(&rotation_lock);
}
static unsigned os_fault,os_protects,os_flushes;
static void *os_address;
static BOOL WINAPI fault_protect(LPVOID address,SIZE_T length,DWORD mode,PDWORD old) {
    if(address==os_address) {
        ++os_protects;
        if((os_fault==1 && os_protects==2) || (os_fault==3 && mode!=PAGE_EXECUTE_READWRITE)) return FALSE;
    }
    return VirtualProtect(address,length,mode,old);
}
static BOOL WINAPI fault_flush(HANDLE process,LPCVOID address,SIZE_T length) {
    if(address==os_address) {
        ++os_flushes;
        if((os_fault==2 && os_flushes==1) || os_fault==4) return FALSE;
    }
    return FlushInstructionCache(process,address,length);
}
static void fault_restore(void) {
    AcquireSRWLockExclusive(&rotation_lock);rotation_stop=1;
    rotation_restore_locked_with(fault_protect,fault_flush);
    ReleaseSRWLockExclusive(&rotation_lock);
}
static void patch_os_failure_cases(void) {
    /* Both sites, installation and restoration, transient and persistent RX/
       cache failures. These are real owned pages and production CAS/cleanup. */
    unsigned cases=0;
    for(unsigned site=0;site<2;site++) for(unsigned fault=1;fault<=4;fault++) for(unsigned restoring=0;restoring<2;restoring++) {
        os_fault=0;os_address=NULL;reset_state();
        if(!restoring) {
            rotation_native_restore();rotation_configured=rotation_stop=rotation_failure=rotation_restore_error=0;
        }
        rotation_patch *p=&rotation_patches[site];os_address=image+p->rva;os_protects=os_flushes=0;os_fault=fault;
        if(restoring) fault_restore();
        else {
            int changed=0;AcquireSRWLockExclusive(&rotation_lock);
            TEST(!rotation_exchange_with(p,p->original,p->patched,&changed,fault_protect,fault_flush) && changed);
            p->owned=1;rotation_fault(ROTATION_E_PATCH);ReleaseSRWLockExclusive(&rotation_lock);fault_restore();
        }
        TEST(p->protection==PAGE_EXECUTE_READ && rotation_failure && !rotation_counts_complete());
        if(fault>=3) TEST((p->protection_pending || p->flush_pending) && !rotation_native_restored());
        LONG failure=rotation_failure;unsigned restore_error=rotation_restore_error;
        unsigned flushes_before=os_flushes;unsigned pending_flush=p->flush_pending;
        os_fault=0;fault_restore();
        unsigned __int128 actual;MEMORY_BASIC_INFORMATION mbi;memcpy(&actual,os_address,16);
        TEST(VirtualQuery(os_address,&mbi,sizeof(mbi))==sizeof(mbi));
        TEST(actual==p->original && !p->owned && !p->protection_pending && !p->flush_pending && mbi.Protect==PAGE_EXECUTE_READ);
        TEST(rotation_failure==failure && rotation_restore_error==restore_error);
        if(pending_flush) TEST(os_flushes>flushes_before);
        unsigned flushed=os_flushes;fault_restore();TEST(os_flushes==flushed);++cases;
    }
    TEST(cases==16);
    /* A foreign replacement while RX repair is pending must survive both the
       failed CAS and the later permission/cache-only retry. */
    for(unsigned owned=0;owned<2;owned++) {
        os_fault=0;os_address=NULL;reset_state();rotation_patch *p=&rotation_patches[0];
        os_address=image+p->rva;os_protects=os_flushes=0;os_fault=3;
        if(!owned) fault_restore();
        DWORD ignored;TEST(VirtualProtect(os_address,16,PAGE_EXECUTE_READWRITE,&ignored));
        unsigned __int128 foreign=p->original^1;memcpy(os_address,&foreign,16);
        fault_restore();TEST(rotation_restore_error && p->protection_pending);
        os_fault=0;fault_restore();unsigned __int128 actual;memcpy(&actual,os_address,16);
        MEMORY_BASIC_INFORMATION mbi;TEST(VirtualQuery(os_address,&mbi,sizeof(mbi))==sizeof(mbi));
        TEST(actual==foreign && mbi.Protect==PAGE_EXECUTE_READ && !p->protection_pending && !p->flush_pending);
        TEST(!rotation_native_restored() && p->owned==owned);
        /* Restore only this test's foreign bytes so later tests have no debt. */
        TEST(VirtualProtect(os_address,16,PAGE_READWRITE,&ignored));
        memcpy(os_address,owned?&p->patched:&p->original,16);
        TEST(VirtualProtect(os_address,16,PAGE_EXECUTE_READ,&ignored));rotation_native_restore();
    }
    os_address=NULL;
}
static const char *fake_string(lua_State *unused,int index,size_t *length) {
    (void)unused;*length=lua_lengths[index-1];return lua_arguments[index-1];
}
static void fake_number(lua_State *unused,double value) {(void)unused;lua_number=value;}
static void fake_lstring(lua_State *unused,const char *value,size_t length) {
    (void)unused;TEST(length<sizeof(lua_text));memcpy(lua_text,value,length);lua_text[length]=0;
}
static void fake_nil(lua_State *unused) {(void)unused;strcpy(lua_text,"nil");}
static void lua_cases(void) {
    p_lua_tolstring=fake_string;p_lua_pushnumber=fake_number;p_lua_pushlstring=fake_lstring;p_lua_pushnil=fake_nil;
    reset_state();rotation_native_restore();rotation_configured=rotation_stop=rotation_failure=0;
    rotation_phase=ROTATION_OFF;boot_phase=0;portable_native_configured=1;
    strcpy(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256);*(void**)test_module=image+ROTATION_MENU_VTABLE;
    const char *expected[]={X4ROT_IMPORT_MODE,X4ROT_IMPORT_DESCRIPTOR_SHA256,X4ROT_IMPORT_SNAPSHOT_SHA256,X4ROT_IMPORT_CONTRACT_SHA256};
    for(unsigned i=0;i<4;i++) {lua_arguments[i]=expected[i];lua_lengths[i]=strlen(expected[i]);}
    for(unsigned i=0;i<4;i++) {
        lua_arguments[i]="wrong";lua_lengths[i]=5;lua_number=-1;
        TEST(lua_portable_rotation_config(NULL)==1 && lua_number==0 && !rotation_configured && !rotation_patches[0].owned && !rotation_patches[1].owned);
        lua_arguments[i]=expected[i];lua_lengths[i]=strlen(expected[i]);
        lua_lengths[i]++;TEST(lua_portable_rotation_config(NULL)==1 && lua_number==0 && !rotation_configured);lua_lengths[i]--;
    }
    /* Even correct contract input cannot bypass the actual process-image pin. */
    rotation_failure=0;installed=1;
    TEST(lua_portable_rotation_config(NULL)==1 && lua_number==0 && !rotation_configured && rotation_failure==ROTATION_E_PIN);
    reset_state();boot_phase=0;*(void**)test_module=image+ROTATION_MENU_VTABLE;
    TEST(rotation_identity_read(&rotation_menu,1));
    TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==1);
    *(void**)(test_table+4*8)=test_client;TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==0);*(void**)(test_table+4*8)=NULL;
    rotation_native_stop();TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==0);
    rotation_native_restore();TEST(lua_portable_rotation_status(NULL)==1);
    TEST(strstr(lua_text,"schema=x4-rotation-status-v2 mode=") &&
        strstr(lua_text,"complete=0") && strstr(lua_text,"owned=0 restore_error=0 restored=1"));
    /* Max-width diagnostic values cannot overflow the fixed serializer. */
    rotation_foreign=UINT64_MAX;rotation_selected[0]=rotation_selected[1]=UINT64_MAX;
    rotation_role_returns[0]=rotation_role_returns[1]=UINT64_MAX;rotation_written[0]=rotation_written[1]=UINT64_MAX;
    for(unsigned role=0;role<2;role++)for(unsigned cls=0;cls<2;cls++)
        rotation_class_calls[role][cls]=rotation_class_returns[role][cls]=rotation_class_written[role][cls]=UINT64_MAX;
    rotation_lifetime_count=rotation_paired=UINT_MAX;
    TEST(lua_portable_rotation_status(NULL)==1 && strcmp(lua_text,"nil") && strlen(lua_text)<2048);
}
static void reattach_cases(void) {
    reset_state();boot_phase=0;TEST(reattach() && !rotation_failure);
    /* A reloaded Lua thread only acknowledges immutable configuration; it
       does not acquire the Load owner or another pre-load ready grant. */
    ui_thread^=1;TEST(reattach());TEST(rotation_owner==0 || rotation_owner!=ui_thread);
    TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==0);ui_thread=GetCurrentThreadId();
    TEST(lua_load_requested(NULL)==1 && lua_number==1 && boot_phase==1);
    TEST(reattach());TEST(lua_load_requested(NULL)==1 && lua_number==0 && boot_phase==1);
    test_iterations=X4ROT_IMPORT_EULER_PER_BANK;test_mode=12;
    TEST(run_load()==load_result && rotation_counts_complete() && !rotation_failure);
    TEST(reattach() && rotation_native_frame_ready());
    TEST(original_loads==1 && original_tails==2*X4ROT_IMPORT_EULER_PER_BANK && rotation_serial==1);
    /* Returning configuration success cannot substitute for missing math. */
    reset_state();TEST(reattach());TEST(!rotation_native_frame_ready() && rotation_failure==ROTATION_E_COUNTS);
    for(unsigned which=0;which<25;which++) {
        reset_state();unsigned char changed_byte=0;unsigned rva=0;
        switch(which) {
        case 0:stats.slot++;break;case 1:remote_port++;break;case 2:secret[0]^=1;break;
        case 3:initial_state_digest[0]^=1;break;case 4:boot_options++;break;case 5:boot_started++;break;
        case 6:portable_native_manifest[0]^=1;break;case 7:portable_native_limit++;break;
        case 8:portable_native_contract_text[0]^=1;break;case 9:used=0;break;case 10:stats.active=0;break;
        case 11:boot_mode=0;break;case 12:installed=0;break;case 13:portable_native_configured=0;break;
        case 14:rotation_native_stop();break;case 15:rotation_fault(ROTATION_E_INPUT);break;
        case 16:rotation_restore_error=1;break;case 17:rotation_patches[0].protection_pending=1;break;
        case 18:rotation_patches[1].flush_pending=1;break;
        case 19:*(void**)(image+ROTATION_CLIENT_GLOBAL)=test_module;break;
        case 20:*(void**)(test_table+4*8)=test_client;break;
        case 21:boot_phase=4;break;case 22:rotation_phase=ROTATION_OFF;break;
        default:rva=rotation_patches[which-23].rva;changed_byte=image[rva];
            protect_page(rva,PAGE_READWRITE);image[rva]^=1;protect_page(rva,PAGE_EXECUTE_READ);break;
        }
        LONG failure=rotation_failure;TEST(!reattach() && rotation_failure);
        if(failure) TEST(rotation_failure==failure);
        if(rva) {TEST(image[rva]==(unsigned char)(changed_byte^1));protect_page(rva,PAGE_READWRITE);
            image[rva]=changed_byte;protect_page(rva,PAGE_EXECUTE_READ);}
    }
    for(unsigned which=0;which<4;which++) {
        reset_state();const char *args[]={X4ROT_IMPORT_MODE,X4ROT_IMPORT_DESCRIPTOR_SHA256,X4ROT_IMPORT_SNAPSHOT_SHA256,X4ROT_IMPORT_CONTRACT_SHA256};
        size_t lengths[]={strlen(args[0]),64,64,64};args[which]="wrong";lengths[which]=5;
        TEST(!rotation_native_config(args[0],lengths[0],args[1],lengths[1],args[2],lengths[2],args[3],lengths[3]));
        TEST(rotation_failure==ROTATION_E_CONFIG && !rotation_load_calls && rotation_patches[0].owned && rotation_patches[1].owned);
    }
    reset_state();protect_page(ROTATION_LOAD_WORD,PAGE_EXECUTE_READWRITE);
    TEST(!reattach());protect_page(ROTATION_LOAD_WORD,PAGE_EXECUTE_READ);rotation_native_restore();
}
static void cohort_cases(void) {
    /* These use the production table and selected-call wrappers; no count or
       layout override turns a malformed class/path into successful correction. */
    for(unsigned which=0;which<10;which++) {
        reset_state();TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
        test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
        current_agent=cohort_agents;current_movement=cohort_xml;
        unsigned char saved[0xc00];memcpy(saved,current_movement,sizeof saved);
        switch(which) {
        case 0:current_movement[0x20]='x';break;
        case 1:*(void**)(current_movement+0xc8)=test_xml;break;
        case 2:*(unsigned*)(current_movement+0x800+12)=0x340;break;
        case 3:*(void**)(current_movement+0x800+0xc8)=test_module;break;
        case 4:*(uint64_t*)(current_movement+0xf0)=33;break;
        case 5:current_movement=NULL;break;
        case 6:current_movement=(void*)(UINTPTR_MAX-8);break;
        case 7:test_mode=13;break;
        case 8:test_mode=14;break;
        case 9:*(unsigned*)(current_movement+0x400+12)=0x52f;break;
        }
        TEST(call_role(0,current_agent)==tail_result);
        TEST(original_tails==1 && rotation_tail_returns==1 && rotation_failure && !rotation_written[0]);
        memcpy(cohort_xml,saved,sizeof saved);rotation_native_restore();TEST(rotation_native_restored());
    }
    for(unsigned which=0;which<5;which++) {
        reset_state();TEST(rotation_load_enter((uintptr_t)(image+ROTATION_LOAD_SITE+5),(uintptr_t)(image+ROTATION_GAME_THIS)));
        test_manager[0x89]=1;*(unsigned*)(image+ROTATION_APP_FLAGS_RVA)=4;
        current_agent=cohort_agents;current_movement=cohort_xml;
        TEST(call_role(0,current_agent)==tail_result && !rotation_failure && rotation_written[0]==1);
        unsigned role=1;char cls[16];memcpy(cls,cohort_xml+0x20,16);uint64_t length=*(uint64_t*)(cohort_xml+0x30);
        if(which==0)role=0; /* duplicate role */
        if(which==1)current_movement+=0xc00; /* reused Agent, changed XML */
        if(which==2)current_agent+=0x1100; /* reused XML, changed Agent */
        if(which==3){memcpy(cohort_xml+0x20,"player",7);*(uint64_t*)(cohort_xml+0x30)=6;}
        if(which==4){*(unsigned*)(image+0x38f4ee4)=1;*(unsigned*)(image+0x2f4b6f4)=0;}
        TEST(call_role(role,current_agent)==tail_result && rotation_failure && original_tails==2);
        TEST(rotation_lifetime_count==1 && !rotation_paired && rotation_written[0]==1 && !rotation_written[1]);
        memcpy(cohort_xml+0x20,cls,16);*(uint64_t*)(cohort_xml+0x30)=length;
        rotation_native_restore();TEST(rotation_native_restored());
    }
}
static void exact_app_guard(void) {
    /* Derive the native flag location and mask from the independently pinned
       Load instructions, not from ROTATION_APP_FLAGS_RVA or the guard's test
       fixture. Decode only; no engine instruction executes in this test. */
    TEST(hash_ok(image+ROTATION_LOAD_TARGET,9772,
        "38df90e5d9385e30279abbccfdb9f12f4789fe61cc2bed4632dc007cc2f54f9c"));
    TEST(image[0x9ee7df]==0x8b && image[0x9ee7e0]==0x0d);
    TEST(image[0x9ee7e5]==0x8b && image[0x9ee7e6]==0xc1 &&
        image[0x9ee7e7]==0x83 && image[0x9ee7e8]==0xe0);
    int32_t displacement;memcpy(&displacement,image+0x9ee7e1,4);
    uintptr_t flags_rva=(uintptr_t)((int64_t)0x9ee7e5+displacement);
    unsigned mask=image[0x9ee7e9];TEST(mask==4 && flags_rva==0x6adf9fc);
    /* The exact configuration reader stores the "adapter" result separately. */
    TEST(hash_ok(image+0x121df00,10488,
        "a982fff5d501af6a57117bea1bfcedf85404658b63f8ab14c1d212a4de63d726"));
    TEST(image[0x121dfca]==0x89 && image[0x121dfcb]==0x05);
    memcpy(&displacement,image+0x121dfcc,4);
    uintptr_t adapter_rva=(uintptr_t)((int64_t)0x121dfd0+displacement);
    TEST(adapter_rva==flags_rva+4 && !strcmp((const char*)image+0x2a75e18,"adapter"));

    /* Owned identities satisfy the other production guard predicates. Both
       adjacent fields are independent inputs; neither is an engine write. */
    *(void**)test_module=image+ROTATION_EP1_VTABLE;
    *(void**)(image+ROTATION_EP1_VTABLE+8)=image+0xab1100;
    rotation_failure=rotation_stop=0;rotation_phase=ROTATION_ENTERED;
    rotation_owner=GetCurrentThreadId();rotation_serial=1;
    rotation_load_calls=rotation_load_open=1;rotation_load_returns=0;rotation_scope_open=1;
    TEST(rotation_identity_read(&rotation_loaded,0));rotation_loaded.manager=0;
    test_manager[0x89]=1;*(unsigned*)(test_manager+0x40)=0;
    *(unsigned*)(image+0x38f4ee4)=0;*(unsigned*)(image+0x2f4b6f4)=1;
    const unsigned adapters[]={0,1,4};
    AcquireSRWLockExclusive(&rotation_lock);
    for(unsigned i=0;i<sizeof(adapters)/sizeof(*adapters);i++) {
        *(unsigned*)(image+flags_rva)=0x20|mask;
        *(unsigned*)(image+adapter_rva)=adapters[i];
        rotation_call c={0};c.agent=test_agent;c.basis=test_agent+0xae0;c.xml=test_xml;
        struct x4rx_guard guard={0};
        TEST(rotation_guard_locked(&c,&guard) && x4rx_guard_valid(&guard));
        TEST(*(unsigned*)(image+flags_rva)==(0x20|mask) &&
            *(unsigned*)(image+adapter_rva)==adapters[i]);
    }
    *(unsigned*)(image+flags_rva)=0x20;*(unsigned*)(image+adapter_rva)=4;
    rotation_call c={0};c.agent=test_agent;c.basis=test_agent+0xae0;c.xml=test_xml;
    struct x4rx_guard guard={0};TEST(!rotation_guard_locked(&c,&guard));
    TEST(!rotation_failure && !rotation_written[0] && !rotation_written[1]);
    ReleaseSRWLockExclusive(&rotation_lock);
    rotation_scope_open=rotation_load_open=0;
}
static void exact_image(const char *path) {
    reset_state();rotation_native_restore();
    FILE *f=fopen(path,"rb");TEST(f);TEST(!fseek(f,0,SEEK_END));long length=ftell(f);TEST(length==55412768);rewind(f);
    unsigned char *file=malloc((size_t)length);TEST(file && fread(file,1,(size_t)length,f)==(size_t)length);fclose(f);
    TEST(hash_ok(file,(ULONG)length,EXE_SHA));
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)file;IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(file+dos->e_lfanew);
    unsigned char *mapped=VirtualAlloc(NULL,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(mapped);
    memcpy(mapped,file,nt->OptionalHeader.SizeOfHeaders);IMAGE_SECTION_HEADER *section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) memcpy(mapped+section[i].VirtualAddress,file+section[i].PointerToRawData,section[i].SizeOfRawData);
    unsigned char *saved=image;image=mapped;TEST(rotation_code_pins());
    /* Independent pinned input ABI: importer retains RDX in R13 before both
       selected calls; the existing offset tail restores Agent RDI. */
    const unsigned char retained_movement[]={0x4c,0x8b,0xea};
    const unsigned char retained_agent[]={0x48,0x8b,0xf9};
    TEST(!memcmp(mapped+0xe86bd2,retained_movement,sizeof retained_movement));
    TEST(!memcmp(mapped+0xe86bd5,retained_agent,sizeof retained_agent));
    for(unsigned role=0;role<2;role++) {
        unsigned call=role?0xe870fc:0xe86d13;int32_t d;memcpy(&d,mapped+call+1,4);
        TEST(mapped[call]==0xe8 && call+5+d==0x47f730);
    }
    mapped[ROTATION_TAIL_TARGET]^=1;TEST(!rotation_code_pins());mapped[ROTATION_TAIL_TARGET]^=1;
    for(unsigned i=0;i<2;i++) {
        unsigned site=i?ROTATION_TAIL_SITE:ROTATION_LOAD_SITE,target=i?ROTATION_TAIL_TARGET:ROTATION_LOAD_TARGET;
        int32_t d;memcpy(&d,mapped+site+1,4);TEST(site+5+d==target);
        TEST(rotation_patches[i].displacement+4<=16);
    }
    /* Exercise the real initial config/file/hash/CAS path on a private PE copy.
       Only GetModuleFileNameW's test pathname is redirected to the immutable
       verified input. No game instruction is executed and no file is written. */
    WCHAR relative[32768],full[32768];
    TEST(MultiByteToWideChar(CP_UTF8,0,path,-1,relative,32768)>0);
    DWORD path_length=GetFullPathNameW(relative,32768,full,NULL);TEST(path_length && path_length<32768);
    test_process_path=full;
    rotation_configured=rotation_stop=rotation_failure=0;rotation_phase=ROTATION_OFF;boot_phase=0;
    *(void**)(mapped+TABLE_RVA)=test_table;*(void**)(mapped+ROTATION_CLIENT_GLOBAL)=test_client;
    *(void**)(mapped+ROTATION_MANAGER_GLOBAL)=test_manager;*(void**)test_client=mapped+ROTATION_CLIENT_VTABLE;
    *(void**)(mapped+ROTATION_CLIENT_VTABLE+8)=mapped+0x122b360;
    *(void**)(mapped+ROTATION_MENU_VTABLE+8)=mapped+0xac4120;
    *(void**)test_module=mapped+ROTATION_MENU_VTABLE;
    for(unsigned i=0;i<2;i++) protect_page(rotation_patches[i].rva,PAGE_EXECUTE_READ);
    unsigned protects=test_protect_calls,flushes=test_flush_calls;
    TEST(rotation_native_config(X4ROT_IMPORT_MODE,strlen(X4ROT_IMPORT_MODE),X4ROT_IMPORT_DESCRIPTOR_SHA256,64,
        X4ROT_IMPORT_SNAPSHOT_SHA256,64,X4ROT_IMPORT_CONTRACT_SHA256,64));
    TEST(rotation_configured && rotation_phase==ROTATION_WAITING && !rotation_failure);
    TEST(test_protect_calls==protects+4 && test_flush_calls==flushes+2);
    TEST(rotation_patch_owned(&rotation_patches[0]) && rotation_patch_owned(&rotation_patches[1]));
    TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==1);
    TEST(reattach());TEST(lua_load_requested(NULL)==1 && lua_number==1);
    TEST(reattach());TEST(lua_portable_rotation_ready(NULL)==1 && lua_number==0);
    TEST(!rotation_native_frame_ready() && rotation_failure==ROTATION_E_COUNTS);
    TEST(!rotation_load_calls && !rotation_tail_calls && !original_loads && !original_tails);
    exact_app_guard();
    rotation_native_restore();TEST(rotation_native_restored());test_process_path=NULL;
    image=saved;VirtualFree(mapped,0,MEM_RELEASE);free(file);
}
#include "test_lockstep_rotation_rejection.h"
int main(int argc,char **argv) {
    image=VirtualAlloc(NULL,0x6fa9000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);TEST(image);
    owned_unwind();cohort_setup();
    thunk=make_thunk();TEST(thunk && thunk_slot_ready(ROTATION_LOAD_THUNK) && thunk_slot_ready(ROTATION_TAIL_THUNK));
    TEST(thunk[ROTATION_TAIL_THUNK]==0x4c && thunk[ROTATION_TAIL_THUNK+1]==0x8b && thunk[ROTATION_TAIL_THUNK+2]==0xc7);
    TEST(thunk[ROTATION_TAIL_THUNK+3]==0x4d && thunk[ROTATION_TAIL_THUNK+4]==0x8b && thunk[ROTATION_TAIL_THUNK+5]==0xcd);
    TEST(!rotation_native_config("portable-fixture12-zero-v1",26,X4ROT_IMPORT_DESCRIPTOR_SHA256,64,
        X4ROT_IMPORT_SNAPSHOT_SHA256,64,X4ROT_IMPORT_CONTRACT_SHA256,64) && !rotation_configured);
    rotation_failure=0;portable_native_configured=0;TEST(rotation_native_frame_ready());
    portable_native_configured=1;strcpy(portable_native_contract_text,"516ef8728fad1e9c99da5e3fa864d5d05366525ba7576c5256b2e9926b10bd50");
    TEST(rotation_native_frame_ready() && !rotation_failure);
    strcpy(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256);
    TEST(!rotation_native_frame_ready() && rotation_failure==ROTATION_E_CONFIG);
    reset_state();test_iterations=X4ROT_IMPORT_EULER_PER_BANK;
    TEST(run_load()==load_result);TEST(original_loads==1 && original_tails==2*X4ROT_IMPORT_EULER_PER_BANK);
    TEST(arguments[0]==(uintptr_t)(image+ROTATION_GAME_THIS) && arguments[1]==UINT64_C(0x9988776655443322) &&
        arguments[2]==UINT64_C(0x1122334455667788) && arguments[3]==UINT64_C(0xabc0000000000000) && arguments[4]==UINT64_C(0xdef0000000000000));
    TEST(!rotation_failure && rotation_counts_complete() && rotation_native_frame_ready());
    uint32_t expected[9];struct x4rot_attribute attrs[3]={{NULL,0,0},{"90",2,1},{NULL,0,0}};
    TEST(x4rot_offset(X4ROT_EULER,attrs,expected)==X4ROT_OK);
    for(unsigned role=0;role<2;role++) for(unsigned i=0;i<0x1a0;i++) {
        unsigned char *bank=current_agent+0xad0+role*0x1a0;
        if(i>=16 && i<64 && i%16<12) {
            unsigned scalar=(i/16-1)*3+(i%16)/4;TEST(bank[i]==((unsigned char*)&expected[scalar])[i%4]);
        } else TEST(bank[i]==0x5a);
    }
    rotation_native_restore();TEST(rotation_native_restored() && !rotation_patches[0].owned && !rotation_patches[1].owned);
    p_lua_pushlstring=fake_lstring;p_lua_pushnil=fake_nil;TEST(lua_portable_rotation_status(NULL)==1 && strstr(lua_text,"complete=1"));
    if(getenv("X4ROT_TEST_RECEIPT")) {
        FILE *out=fopen(getenv("X4ROT_TEST_RECEIPT"),"wb");TEST(out);
        TEST(fwrite(lua_text,1,strlen(lua_text),out)==strlen(lua_text));TEST(!fclose(out));
    }
    for(unsigned mode=1;mode<=6;mode++) {
        reset_state();test_mode=mode;int caught=0;
        __try {(void)run_load();} __except(EXCEPTION_EXECUTE_HANDLER) {caught=1;}
        if(!(rotation_failure && !rotation_counts_complete() && !rotation_scope_open && !rotation_load_open && original_loads==1))
            fprintf(stderr,"mode=%u caught=%d failure=%ld phase=%u scope=%u loadopen=%u loads=%u raw=%llu/%llu abnormal=%llu\n",mode,caught,
                (long)rotation_failure,rotation_phase,rotation_scope_open,rotation_load_open,original_loads,
                (unsigned long long)rotation_load_calls,(unsigned long long)rotation_load_returns,(unsigned long long)rotation_load_abnormal);
        TEST(rotation_failure && !rotation_counts_complete() && !rotation_scope_open && !rotation_load_open && original_loads==1);
        TEST(caught==(mode==1 || mode==2));
        if(mode==1) TEST(rotation_load_abnormal==1 && !rotation_load_returns && !original_tails);
        if(mode==2) TEST(rotation_load_abnormal==1 && !rotation_tail_returns && original_tails==1);
        if(mode>=3) TEST(original_tails==2 && rotation_tail_returns==2);
        rotation_native_restore();TEST(rotation_native_restored());
    }
    for(unsigned mode=8;mode<=11;mode++) {
        reset_state();test_mode=mode;TEST(run_load()==load_result);
        TEST(rotation_failure && original_loads==1 && original_tails==2 && !rotation_written[0] && !rotation_written[1]);
    }
    reset_state();load_result=UINT64_C(0xabcdef1234567800);TEST(run_load()==load_result);
    TEST(rotation_load_false==1 && rotation_failure && !rotation_counts_complete());
    reset_state();load_result=UINT64_C(0xabcdef1234567802);TEST(run_load()==load_result);
    TEST(!rotation_load_true && !rotation_load_false && rotation_failure==ROTATION_E_RETURN && !rotation_counts_complete());
    reset_state();test_iterations=0;TEST(run_load()==load_result && rotation_failure && !rotation_native_frame_ready());
    reset_state();rotation_load_fn direct=rotation_load_run;TEST(direct((uintptr_t)(image+ROTATION_GAME_THIS),1,2,3,4)==load_result);
    TEST(rotation_failure && original_loads==1); /* Foreign Load caller cannot establish ownership. */
    reset_state();uintptr_t result=rotation_tail_run(test_agent+0xae0,test_xml,test_agent,current_movement);
    TEST(result==tail_result && original_tails==1 && rotation_foreign==1 && !rotation_selected[0] && !rotation_failure);
    stop_interleavings();
    rollback_case();
    patch_os_failure_cases();
    reset_state();unsigned char original_word[16];memcpy(original_word,image+ROTATION_TAIL_WORD,16);
    protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);image[ROTATION_TAIL_WORD]^=1;protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    rotation_native_restore();TEST(rotation_restore_error && !rotation_native_restored() && image[ROTATION_TAIL_WORD]==(unsigned char)(original_word[0]^1));
    /* Restore test-owned bytes only, then finish the owned CAS cleanup. */
    protect_page(ROTATION_TAIL_WORD,PAGE_READWRITE);memcpy(image+ROTATION_TAIL_WORD,original_word,16);protect_page(ROTATION_TAIL_WORD,PAGE_EXECUTE_READ);
    rotation_native_restore();
    lua_cases();
    reattach_cases();
    cohort_cases();
    rejection_cases();
    if(argc==2) exact_image(argv[1]);
    printf("PASS Windows rotation wrapper: %u checks; owned code/data, no native execution\n",tests);
    return 0;
}
