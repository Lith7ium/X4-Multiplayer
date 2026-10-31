/* Opt-in saved navigation Euler import. Source-only until separately reviewed.
 * The old portable mode never configures this capability. No lazy getters. */
#include "rotation_xml_scope.h"
#include "portable_rotation.h"
#include "portable_import_capability.h"
#define ROTATION_LOAD_SITE 0xab1a82u
#define ROTATION_LOAD_WORD 0xab1a80u
#define ROTATION_LOAD_TARGET 0x9ee7a0u
#define ROTATION_TAIL_SITE 0x47f828u
#define ROTATION_TAIL_WORD 0x47f820u
#define ROTATION_TAIL_TARGET 0x49cbb0u
#define ROTATION_LOAD_THUNK 3712u
#define ROTATION_TAIL_THUNK 3744u
#define ROTATION_CLIENT_GLOBAL 0x6cf1520u
#define ROTATION_CLIENT_VTABLE 0x2c8abd0u
#define ROTATION_EP1_VTABLE 0x2bbf580u
#define ROTATION_MENU_VTABLE 0x2bbf528u
#define ROTATION_MANAGER_GLOBAL 0x6cf21c8u
#define ROTATION_GAME_THIS 0x3d196f0u
/* Pinned native Load reads/sets bit 4 here at 9ee7df/9ee801.
 * The adjacent DWORD at 6adfa00 is the graphics adapter setting. */
#define ROTATION_APP_FLAGS_RVA 0x6adf9fcu
enum rotation_phase { ROTATION_OFF,ROTATION_WAITING,ROTATION_ENTERED,ROTATION_IMPORTED,ROTATION_RETURNED,ROTATION_FAILED };
enum rotation_error { ROTATION_E_NONE,ROTATION_E_CONFIG,ROTATION_E_PIN,ROTATION_E_PATCH,ROTATION_E_OWNER,
    ROTATION_E_SCOPE,ROTATION_E_INPUT,ROTATION_E_MATH,ROTATION_E_RETURN,ROTATION_E_STOP,
    ROTATION_E_CHANGED,ROTATION_E_WRITE,ROTATION_E_COUNTS,ROTATION_E_UNWIND,ROTATION_E_RESTORE };
typedef uintptr_t (*rotation_load_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef uintptr_t (*rotation_tail_fn)(void *,void *);
typedef struct {
    uintptr_t table,client,module,manager;unsigned slot,module_state;
} rotation_identity;
typedef struct {
    unsigned rva,displacement,thunk_offset;unsigned __int128 original,patched;
    DWORD protection;unsigned owned,protection_pending,flush_pending;
} rotation_patch;
static rotation_patch rotation_patches[2]={
    {ROTATION_LOAD_WORD,3,ROTATION_LOAD_THUNK,0,0,0,0,0,0},
    {ROTATION_TAIL_WORD,9,ROTATION_TAIL_THUNK,0,0,0,0,0,0}
};
static SRWLOCK rotation_lock=SRWLOCK_INIT;
static volatile LONG rotation_configured,rotation_stop,rotation_failure;
static unsigned rotation_phase,rotation_restore_error,rotation_scope_open,rotation_load_open;
static uint64_t rotation_serial,rotation_load_calls,rotation_load_returns,rotation_tail_calls,rotation_tail_returns;
static uint64_t rotation_selected[2],rotation_role_returns[2],rotation_written[2],rotation_foreign;
static uint64_t rotation_class_calls[2][2],rotation_class_returns[2][2],rotation_class_written[2][2];
/* Single native Load; no entry reuse or replacement. Two bounded indices make
 * both Agent reuse and movement reuse with a different Agent explicit. */
#define ROTATION_COHORT_CAPACITY 32768u
typedef struct {uintptr_t agent,movement,banks[2];unsigned class_id,roles;} rotation_lifetime;
static rotation_lifetime rotation_lifetimes[ROTATION_COHORT_CAPACITY];
static unsigned rotation_agent_index[ROTATION_COHORT_CAPACITY],rotation_movement_index[ROTATION_COHORT_CAPACITY];
static unsigned rotation_lifetime_count,rotation_paired;
#include "lockstep_rotation_rejection.h"
static unsigned rotation_index_slot(uintptr_t key,const unsigned *index,int movement) {
    unsigned at=(unsigned)(((uint64_t)key>>4)*UINT64_C(0x9e3779b97f4a7c15))&(ROTATION_COHORT_CAPACITY-1);
    for(unsigned n=0;n<ROTATION_COHORT_CAPACITY;n++,at=(at+1)&(ROTATION_COHORT_CAPACITY-1)) {
        unsigned entry=index[at];
        if(!entry || (movement?rotation_lifetimes[entry-1].movement:rotation_lifetimes[entry-1].agent)==key)return at;
    }
    return ROTATION_COHORT_CAPACITY;
}
/* Requires rotation_lock and a valid current Load guard. The role is claimed
 * before original; failure later is sticky and never frees this identity. */
static int rotation_lifetime_enter_observed(uintptr_t agent,uintptr_t movement,uintptr_t bank,unsigned role,unsigned cls,rotation_reject_trace *d) {
    if(!agent || !movement || !bank || role>1 || cls>1)return rotation_reject_pred(d,RP_ARGUMENTS,role,cls);
    unsigned a=rotation_index_slot(agent,rotation_agent_index,0),m=rotation_index_slot(movement,rotation_movement_index,1);
    if(a==ROTATION_COHORT_CAPACITY || m==ROTATION_COHORT_CAPACITY)return rotation_reject_pred(d,RP_INDEX_FULL,a,m);
    unsigned ai=rotation_agent_index[a],mi=rotation_movement_index[m];rotation_lifetime *entry;
    if(ai || mi) {
        if(!ai || ai!=mi)return rotation_reject_pred(d,RP_INDEX_JOIN,ai,mi);entry=&rotation_lifetimes[ai-1];
        if(entry->agent!=agent || entry->movement!=movement)return rotation_reject_pred(d,RP_LIFETIME_IDENTITY,
            entry->agent!=agent?entry->agent:entry->movement,entry->agent!=agent?agent:movement);
        if(entry->class_id!=cls)return rotation_reject_pred(d,RP_LIFETIME_CLASS,entry->class_id,cls);
        if(entry->roles&(1u<<role))return rotation_reject_pred(d,RP_ROLE_DUPLICATE,entry->roles,role);
        if(entry->banks[1-role]==bank)return rotation_reject_pred(d,RP_BANK_DUPLICATE,bank,entry->banks[1-role]);
    }else {
        if(rotation_lifetime_count>=X4ROT_IMPORT_EULER_PER_BANK)return rotation_reject_pred(d,RP_CAPACITY,rotation_lifetime_count,X4ROT_IMPORT_EULER_PER_BANK);
        entry=&rotation_lifetimes[rotation_lifetime_count];
        *entry=(rotation_lifetime){agent,movement,{0,0},cls,0};
        ++rotation_lifetime_count;rotation_agent_index[a]=rotation_movement_index[m]=rotation_lifetime_count;
    }
    entry->banks[role]=bank;entry->roles|=1u<<role;if(entry->roles==3)++rotation_paired;
    return 1;
}
static int __attribute__((unused)) rotation_lifetime_enter(uintptr_t agent,uintptr_t movement,uintptr_t bank,unsigned role,unsigned cls) {
    return rotation_lifetime_enter_observed(agent,movement,bank,role,cls,NULL);
}
static uint64_t rotation_load_true,rotation_load_false,rotation_load_abnormal;
static DWORD rotation_owner;
static rotation_identity rotation_menu,rotation_loaded;
static uintptr_t rotation_entry_manager,rotation_load_result;
/* Immutable process-owned boot binding. UI reload acknowledgement never adopts
 * a different session or resets an in-progress/completed native Load. */
typedef struct {
    uintptr_t image,thunk;uint64_t options;ULONGLONG started;
    unsigned slot,port,limit;char token[33],initial[65],manifest[204];
} rotation_boot_binding;
static rotation_boot_binding rotation_boot;
static rotation_boot_binding rotation_reject_boot;

static void rotation_fault(unsigned reason) {
    InterlockedCompareExchange(&rotation_failure,(LONG)reason,ROTATION_E_NONE);
}
static int rotation_is_stopped(void *context) {
    rotation_reject_trace *d=(rotation_reject_trace*)context;LONG value=InterlockedCompareExchange(&rotation_stop,0,0);
    if(value)return !rotation_reject_pred(d,RP_STOPPED,(uint64_t)value,0);
    if(!stats.active)return !rotation_reject_pred(d,RP_INACTIVE,0,1);
    value=InterlockedCompareExchange(&rotation_failure,0,0);
    if(value)return !rotation_reject_pred(d,RP_ALREADY_FAILED,(uint64_t)value,0);
    return 0;
}
static int rotation_read(void *context,uint64_t address,void *out,size_t length) {
    return address && address<=UINTPTR_MAX && length && length-1<=UINTPTR_MAX-(uintptr_t)address &&
        rotation_observed_read((rotation_reject_trace*)context,(void*)(uintptr_t)address,out,length);
}
static int rotation_read_at_observed(uintptr_t base,size_t offset,void *out,size_t length,rotation_reject_trace *d) {
    return base && offset<=UINTPTR_MAX-base && length && length-1<=UINTPTR_MAX-base-offset &&
        rotation_observed_read(d,(void*)(base+offset),out,length);
}
static int rotation_read_at(uintptr_t base,size_t offset,void *out,size_t length) {
    return rotation_read_at_observed(base,offset,out,length,NULL);
}
static int rotation_identity_read_observed(rotation_identity *out,int menu,rotation_reject_trace *d) {
    rotation_identity s={0};uintptr_t vt=0,entry=0;unsigned matches=0,loading=0;
#define RR_CHECK(condition,predicate,a,b) do {if(condition)return rotation_reject_pred(d,predicate,(uint64_t)(a),(uint64_t)(b));} while(0)
    RR_CHECK(!rotation_observed_read(d,image+TABLE_RVA,&s.table,8),RP_TABLE_READ,0,0);
    RR_CHECK(!s.table,RP_TABLE_NULL,s.table,1);
    RR_CHECK(!rotation_observed_read(d,image+ROTATION_CLIENT_GLOBAL,&s.client,8),RP_CLIENT_READ,0,0);
    RR_CHECK(!s.client,RP_CLIENT_NULL,s.client,1);
    RR_CHECK(!rotation_read_at_observed(s.client,0,&vt,8,d),RP_CLIENT_TYPE_READ,0,0);
    RR_CHECK(vt!=(uintptr_t)(image+ROTATION_CLIENT_VTABLE),RP_CLIENT_TYPE,vt,image+ROTATION_CLIENT_VTABLE);
    RR_CHECK(!rotation_read_at_observed(vt,8,&entry,8,d),RP_CLIENT_ENTRY_READ,0,0);
    RR_CHECK(entry!=(uintptr_t)(image+0x122b360),RP_CLIENT_ENTRY,entry,image+0x122b360);
    RR_CHECK(!rotation_read_at_observed(s.client,0x2a0,&s.module,8,d),RP_MODULE_READ,0,0);
    RR_CHECK(!s.module,RP_MODULE_NULL,s.module,1);
    RR_CHECK(!rotation_read_at_observed(s.module,0,&vt,8,d),RP_MODULE_TYPE_READ,0,0);
    RR_CHECK(vt!=(uintptr_t)(image+(menu?ROTATION_MENU_VTABLE:ROTATION_EP1_VTABLE)),RP_MODULE_TYPE,vt,image+(menu?ROTATION_MENU_VTABLE:ROTATION_EP1_VTABLE));
    RR_CHECK(!rotation_read_at_observed(vt,8,&entry,8,d),RP_MODULE_ENTRY_READ,0,0);
    RR_CHECK(entry!=(uintptr_t)(image+(menu?0xac4120:0xab1100)),RP_MODULE_ENTRY,entry,image+(menu?0xac4120:0xab1100));
    RR_CHECK(!rotation_read_at_observed(s.module,0x70,&s.module_state,4,d),RP_MODULE_STATE_READ,0,0);
    RR_CHECK(!rotation_observed_read(d,image+ROTATION_MANAGER_GLOBAL,&s.manager,8),RP_MANAGER_READ,0,0);
    RR_CHECK(!s.manager,RP_MANAGER_NULL,s.manager,1);
    RR_CHECK(!rotation_read_at_observed(s.table,0x130,&loading,4,d),RP_LOADING_READ,0,0);
    RR_CHECK(menu && loading,RP_LOADING,loading,0);
    for(unsigned i=0;i<17;i++) {
        RR_CHECK(!rotation_read_at_observed(s.table,i*8,&entry,8,d),RP_TABLE_ENTRY_READ,i,17);
        if(entry==s.client) {++matches;s.slot=i;}
    }
    RR_CHECK(matches!=1,RP_TABLE_MATCHES,matches,1);*out=s;return 1;
}
static int rotation_identity_read(rotation_identity *out,int menu) {return rotation_identity_read_observed(out,menu,NULL);}
static int rotation_same_host(const rotation_identity *a,const rotation_identity *b,int module) {
    return a->table==b->table && a->client==b->client && a->slot==b->slot &&
        (!module || (a->module==b->module && a->module_state==b->module_state));
}
static int rotation_patch_owned_observed(const rotation_patch *p,rotation_reject_trace *d) {
    unsigned __int128 actual;
    RR_CHECK(!p->owned,RP_PATCH_OWNED,p->rva,1);
    RR_CHECK(p->protection_pending || p->flush_pending,RP_PATCH_DEBT,p->protection_pending,p->flush_pending);
    RR_CHECK(!rotation_observed_read(d,image+p->rva,&actual,16),RP_PATCH_READ,p->rva,16);
    RR_CHECK(actual!=p->patched,RP_PATCH_BYTES,p->rva,0);return 1;
}
static int rotation_patch_owned(const rotation_patch *p) {return rotation_patch_owned_observed(p,NULL);}
typedef BOOL (WINAPI *rotation_protect_fn)(LPVOID,SIZE_T,DWORD,PDWORD);
typedef BOOL (WINAPI *rotation_flush_fn)(HANDLE,LPCVOID,SIZE_T);
static int rotation_repair_with(rotation_patch *p,rotation_protect_fn protect,rotation_flush_fn flush) {
    DWORD ignored;
    if(p->protection_pending && protect(image+p->rva,16,p->protection,&ignored)) p->protection_pending=0;
    if(p->flush_pending && flush(GetCurrentProcess(),image+p->rva,16)) p->flush_pending=0;
    return !p->protection_pending && !p->flush_pending;
}
/* Changes only the owned displacement. A protection/cache failure remains
 * sticky even when the CAS itself succeeded. Byte ownership and outstanding OS
 * work are independent; a failed RX restore never makes temporary RWX the new
 * target permission. All callers hold rotation_lock. */
static int rotation_exchange_with(rotation_patch *p,unsigned __int128 expected,unsigned __int128 desired,int *changed,
        rotation_protect_fn protect,rotation_flush_fn flush) {
    DWORD old=0;*changed=0;
    if(!protect(image+p->rva,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int protection_known=old==p->protection || (p->protection_pending && old==PAGE_EXECUTE_READWRITE);
    p->protection_pending=1;
    if(protection_known) {
        unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+p->rva),expected,desired);
        *changed=actual==expected;
    }
    p->flush_pending=1;
    int repaired=rotation_repair_with(p,protect,flush);
    return *changed && repaired && protection_known;
}
static int rotation_exchange(rotation_patch *p,unsigned __int128 expected,unsigned __int128 desired,int *changed) {
    return rotation_exchange_with(p,expected,desired,changed,VirtualProtect,FlushInstructionCache);
}
static int rotation_code_pins(void) {
    /* A full exact executable check plus root spans actually enclosing the
       proposed sites/callers and the Windows-specific owner layout. */
    return hash_ok(image+0xab1100,2703,"af165a1bad6b94f81c519f9968e3b75750da41baaf1a24ec6d245e00aed8a7c2") &&
        hash_ok(image+ROTATION_LOAD_TARGET,9772,"38df90e5d9385e30279abbccfdb9f12f4789fe61cc2bed4632dc007cc2f54f9c") &&
        hash_ok(image+0x9ec1d0,4114,"c816ee2042e118c907dcdbb2f40fd74e43d648139eb02f59f0e12669c9c01afd") &&
        hash_ok(image+0x122b360,3153,"e764663920e9cf186717d15f20dff1945ae65239cb04316318aca035b65f1083") &&
        hash_ok(image+0xef4400,1457,"52bb5fc3c7a2df65acc46fa7c14b0b3d8c20515ccca98cdd97aff11c88cc9130") &&
        hash_ok(image+0xe86bb0,4416,"f9058c5168d815dbde0ae41dc46a62c378907fdd513a8f7c5cee58c711923ba0") &&
        hash_ok(image+0x47f730,320,"5b3553532f6688a3d0b37d6c966693463956bc50f1ac1fa8dfb9d38ad115a311") &&
        hash_ok(image+0x47f870,135,"b1f008ddab998f585c74afe8b5fb827a698653070cb418cf4cf3fc2cf83e92bc") &&
        hash_ok(image+0x47f8f7,16,"7a34a7e08b5a8ee43883cf839d8a8f77fad778047cd9d825c296134a25bbca7f") &&
        hash_ok(image+ROTATION_TAIL_TARGET,555,"d47d33dc9251df096f60846c4ec43f75e1718d3233b1335d3c37f893efec251b") &&
        hash_ok(image+0x129150,143,"8d3b1f01b1534ccdc1cae5eb1802ebbb081232ffd4a0a9e650e31bc682e44f03") &&
        hash_ok(image+ROTATION_LOAD_WORD,16,"775dac6b2cc7a6183461c5669591fc1eeecb8817740665f72008c2bb791cf935") &&
        hash_ok(image+ROTATION_TAIL_WORD,16,"21140e50b0bd5d7131eb578e7dfb1d42396da3330a2fce603f0620418a75e7ba");
}
static int rotation_prepare_patch(rotation_patch *p) {
    MEMORY_BASIC_INFORMATION mbi;intptr_t relative=(intptr_t)(thunk+p->thunk_offset)-(intptr_t)(image+p->rva+p->displacement+4);
    if(p->owned || p->protection_pending || p->flush_pending || relative<INT32_MIN || relative>INT32_MAX || p->displacement>12 ||
       !VirtualQuery(image+p->rva,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT || mbi.Protect!=PAGE_EXECUTE_READ ||
       !read_local(image+p->rva,&p->original,16)) return 0;
    p->protection=mbi.Protect;p->patched=p->original;int32_t d=(int32_t)relative;
    memcpy((unsigned char*)&p->patched+p->displacement,&d,4);return 1;
}
static void rotation_restore_locked_with(rotation_protect_fn protect,rotation_flush_fn flush) {
    for(unsigned i=0;i<2;i++) {
        rotation_patch *p=&rotation_patches[i];
        if(p->owned) {
            int changed=0;int ok=rotation_exchange_with(p,p->patched,p->original,&changed,protect,flush);
            if(changed) p->owned=0;
            if(!ok) {rotation_restore_error=1;rotation_fault(ROTATION_E_RESTORE);}
        }
        /* Retry even after the bytes are original or a foreign word prevented
           CAS. Permission/cache repair never writes those bytes. */
        if(!rotation_repair_with(p,protect,flush)) {rotation_restore_error=1;rotation_fault(ROTATION_E_RESTORE);}
    }
}
static void rotation_restore_locked(void) {rotation_restore_locked_with(VirtualProtect,FlushInstructionCache);}
static void rotation_native_stop(void) {
    /* Participate even before configured is published: configuration may hold
       this lock between its first CAS and the final configured store. */
    AcquireSRWLockExclusive(&rotation_lock);
    InterlockedExchange(&rotation_stop,1);
    ReleaseSRWLockExclusive(&rotation_lock);
}
static void rotation_native_restore(void) {
    AcquireSRWLockExclusive(&rotation_lock);
    InterlockedExchange(&rotation_stop,1); /* Ordered against the bounded writer. */
    rotation_restore_locked();
    ReleaseSRWLockExclusive(&rotation_lock);
}
static int rotation_native_restored(void) {
    for(unsigned i=0;i<2;i++) if(rotation_patches[i].protection_pending || rotation_patches[i].flush_pending) return 0;
    if(!rotation_configured) return !rotation_patches[0].owned && !rotation_patches[1].owned && !rotation_restore_error;
    if(rotation_restore_error || rotation_patches[0].owned || rotation_patches[1].owned) return 0;
    for(unsigned i=0;i<2;i++) {
        unsigned __int128 actual;MEMORY_BASIC_INFORMATION m;
        if(!read_local(image+rotation_patches[i].rva,&actual,16) || actual!=rotation_patches[i].original ||
           !VirtualQuery(image+rotation_patches[i].rva,&m,sizeof(m)) || m.Protect!=rotation_patches[i].protection) return 0;
    }
    return 1;
}
static int rotation_counts_complete(void) {
    for(unsigned role=0;role<2;role++)for(unsigned cls=0;cls<2;cls++) {
        uint64_t expected=cls?X4ROT_IMPORT_PLAYER_EULER:X4ROT_IMPORT_NAVIGATION_EULER;
        if(rotation_class_calls[role][cls]!=expected || rotation_class_returns[role][cls]!=expected ||
           rotation_class_written[role][cls]!=expected)return 0;
    }
    if(rotation_lifetime_count!=X4ROT_IMPORT_EULER_PER_BANK || rotation_paired!=rotation_lifetime_count)return 0;
    return rotation_load_calls==1 && rotation_load_returns==1 && rotation_phase==ROTATION_RETURNED &&
        !rotation_scope_open && !rotation_load_open && rotation_tail_calls==rotation_tail_returns &&
        rotation_load_true==1 && !rotation_load_false && !rotation_load_abnormal &&
        rotation_selected[0]==X4ROT_IMPORT_EULER_PER_BANK && rotation_selected[1]==X4ROT_IMPORT_EULER_PER_BANK &&
        rotation_role_returns[0]==X4ROT_IMPORT_EULER_PER_BANK && rotation_role_returns[1]==X4ROT_IMPORT_EULER_PER_BANK &&
        rotation_written[0]==X4ROT_IMPORT_EULER_PER_BANK && rotation_written[1]==X4ROT_IMPORT_EULER_PER_BANK;
}
static int rotation_native_frame_ready(void) {
    int required=portable_native_configured && !strcmp(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256);
    /* A caller cannot advertise the new contract while omitting the separate
       correction configuration. The old contract never enables correction. */
    if(!rotation_configured) {
        if(required) rotation_fault(ROTATION_E_CONFIG);
        return !required;
    }
    if(!required) {rotation_fault(ROTATION_E_CONFIG);return 0;}
    AcquireSRWLockShared(&rotation_lock);
    rotation_identity now;unsigned char flag=1;int ok=!rotation_is_stopped(NULL) && rotation_counts_complete() &&
        rotation_identity_read(&now,0) && rotation_same_host(&rotation_loaded,&now,0) &&
        now.module==rotation_loaded.module && now.manager==rotation_loaded.manager &&
        rotation_read_at(now.manager,0x89,&flag,1) && flag==0 &&
        rotation_patch_owned(&rotation_patches[0]) && rotation_patch_owned(&rotation_patches[1]);
    ReleaseSRWLockShared(&rotation_lock);
    if(!ok) rotation_fault(ROTATION_E_COUNTS);return ok;
}
static int rotation_exact_text(const char *s,size_t n,const char *expected) {
    return s && n==strlen(expected) && !memcmp(s,expected,n);
}
static int rotation_boot_read(rotation_boot_binding *out) {
    if(!used || !stats.active || !boot_mode || boot_phase<0 || boot_phase>3 || !installed || !portable_native_configured ||
       !valid_hex(secret,32) || !valid_hex(initial_state_digest,64) ||
       strcmp(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256) ||
       !portable_native_limit || portable_native_limit>PORTABLE_CAPTURE_LIMIT) return 0;
    rotation_boot_binding s={0};s.image=(uintptr_t)image;s.thunk=(uintptr_t)thunk;
    s.options=boot_options;s.started=boot_started;s.slot=stats.slot;s.port=remote_port;s.limit=portable_native_limit;
    memcpy(s.token,secret,sizeof(s.token));memcpy(s.initial,initial_state_digest,sizeof(s.initial));
    memcpy(s.manifest,portable_native_manifest,sizeof(s.manifest));*out=s;return 1;
}
static int rotation_boot_same(const rotation_boot_binding *a,const rotation_boot_binding *b) {
    return a->image==b->image && a->thunk==b->thunk && a->options==b->options && a->started==b->started &&
        a->slot==b->slot && a->port==b->port && a->limit==b->limit &&
        !memcmp(a->token,b->token,sizeof(a->token)) && !memcmp(a->initial,b->initial,sizeof(a->initial)) &&
        !memcmp(a->manifest,b->manifest,sizeof(a->manifest));
}
static int rotation_reattach_locked(void) {
    rotation_boot_binding now;uintptr_t table=0,client=0,vt=0,entry=0;unsigned matches=0;
    if(rotation_stop || rotation_failure || rotation_restore_error ||
       rotation_phase<ROTATION_WAITING || rotation_phase>ROTATION_RETURNED ||
       !rotation_boot_read(&now) || !rotation_boot_same(&now,&rotation_boot) ||
       !read_local(image+TABLE_RVA,&table,8) || table!=rotation_menu.table ||
       !read_local(image+ROTATION_CLIENT_GLOBAL,&client,8) || client!=rotation_menu.client ||
       !rotation_read_at(client,0,&vt,8) || vt!=(uintptr_t)(image+ROTATION_CLIENT_VTABLE)) return 0;
    for(unsigned i=0;i<17;i++) {
        if(!rotation_read_at(table,i*8,&entry,8)) return 0;
        if(entry==client) {if(i!=rotation_menu.slot) return 0;++matches;}
    }
    if(matches!=1 || !thunk_slot_ready(ROTATION_LOAD_THUNK) || !thunk_slot_ready(ROTATION_TAIL_THUNK)) return 0;
    for(unsigned i=0;i<2;i++) {
        MEMORY_BASIC_INFORMATION m;rotation_patch *p=&rotation_patches[i];
        if(!rotation_patch_owned(p) || !VirtualQuery(image+p->rva,&m,sizeof(m)) ||
           m.State!=MEM_COMMIT || m.Protect!=p->protection) return 0;
    }
    return 1;
}
static int rotation_rejection_enable(unsigned enabled) {
    AcquireSRWLockExclusive(&rotation_lock);rotation_boot_binding now;int ok=0;
    if(enabled>1 || GetCurrentThreadId()!=ui_thread || !rotation_boot_read(&now) || rotation_stop || rotation_failure)goto done;
    if(rotation_reject_mode>=0) {
        if((unsigned)rotation_reject_mode!=enabled || !rotation_boot_same(&now,&rotation_reject_boot))goto done;
        if(rotation_configured) {
            /* Mode acknowledgement is DLL-state-only. The immediately following
             * rotation_native_config performs the existing native ownership
             * checks exactly once; this is not a second health/ready probe. */
            ok=!rotation_restore_error && rotation_phase>=ROTATION_WAITING && rotation_phase<=ROTATION_RETURNED &&
                rotation_boot_same(&now,&rotation_boot);
            for(unsigned i=0;ok && i<2;i++)ok=rotation_patches[i].owned &&
                !rotation_patches[i].protection_pending && !rotation_patches[i].flush_pending;
            goto done;
        }
    }else if(rotation_configured)goto done; /* No late enable or disable. */
    if(boot_phase || stats.issued || stats.completed || rotation_load_calls || rotation_load_open || rotation_tail_calls)goto done;
    if(rotation_reject_mode<0) {rotation_reject_boot=now;InterlockedExchange(&rotation_reject_mode,(LONG)enabled);}
    ok=1;
done:ReleaseSRWLockExclusive(&rotation_lock);return ok;
}
/* Deliberately not exposed/called by old portable_config. Root owns the new
 * capability Lua/contract plumbing. Wrong requests cannot enable this module. */
static int __attribute__((unused)) rotation_native_config(const char *mode,size_t mode_len,
        const char *descriptor,size_t desc_len,const char *snapshot,size_t snapshot_len,
        const char *contract,size_t contract_len) {
    if(!rotation_exact_text(mode,mode_len,X4ROT_IMPORT_MODE) ||
       !rotation_exact_text(descriptor,desc_len,X4ROT_IMPORT_DESCRIPTOR_SHA256) ||
       !rotation_exact_text(snapshot,snapshot_len,X4ROT_IMPORT_SNAPSHOT_SHA256) ||
       !rotation_exact_text(contract,contract_len,X4ROT_IMPORT_CONTRACT_SHA256)) {rotation_fault(ROTATION_E_CONFIG);return 0;}
    AcquireSRWLockExclusive(&rotation_lock);int ok=0;rotation_boot_binding binding;
    if(rotation_configured) {
        /* Read-only acknowledgement of the same boot after a fresh Lua state.
         * Native Load/tail ownership, original calls, serial and counters remain
         * untouched. In particular this is not a second pre-load ready grant. */
        ok=rotation_reattach_locked();if(!ok) rotation_fault(ROTATION_E_CONFIG);goto done;
    }
    if(rotation_stop || rotation_failure || !rotation_boot_read(&binding) || boot_phase ||
       stats.issued || stats.completed || GetCurrentThreadId()!=ui_thread || !portable_native_configured ||
       strcmp(portable_native_contract_text,X4ROT_IMPORT_CONTRACT_SHA256) || !installed) {rotation_fault(ROTATION_E_CONFIG);goto done;}
    if(rotation_reject_mode>=0 && !rotation_boot_same(&binding,&rotation_reject_boot)) {rotation_fault(ROTATION_E_CONFIG);goto done;}
    if(rotation_reject_mode<0) {rotation_reject_boot=binding;InterlockedExchange(&rotation_reject_mode,0);}
    if(!rotation_identity_read(&rotation_menu,1)) {rotation_fault(ROTATION_E_OWNER);goto done;}
    if(!thunk_slot_ready(ROTATION_LOAD_THUNK) || !thunk_slot_ready(ROTATION_TAIL_THUNK) ||
       !file_ok() || !rotation_code_pins()) {rotation_fault(ROTATION_E_PIN);goto done;}
    for(unsigned i=0;i<2;i++) if(!rotation_prepare_patch(&rotation_patches[i])) {rotation_fault(ROTATION_E_PIN);goto rollback;}
    for(unsigned i=0;i<2;i++) {
        int changed=0;int swapped=rotation_exchange(&rotation_patches[i],rotation_patches[i].original,rotation_patches[i].patched,&changed);
        rotation_patches[i].owned=(unsigned)changed;
        if(!swapped) {rotation_fault(ROTATION_E_PATCH);goto rollback;}
    }
    rotation_boot=binding;rotation_phase=ROTATION_WAITING;InterlockedExchange(&rotation_configured,1);ok=1;goto done;
rollback:
    InterlockedExchange(&rotation_stop,1);rotation_restore_locked();
done:ReleaseSRWLockExclusive(&rotation_lock);return ok;
}

/* The native Load resets the preceding world before acquiring the pathing
 * manager guard. Entry manager is diagnostic; it is NOT an import lifetime. */
static int rotation_load_enter(uintptr_t caller,uintptr_t self) {
    AcquireSRWLockExclusive(&rotation_lock);rotation_identity now;int ok=0;
    ++rotation_load_calls;++rotation_load_open;
    if(!rotation_configured || rotation_is_stopped(NULL) || rotation_phase!=ROTATION_WAITING ||
       rotation_load_calls!=1 || rotation_load_open!=1 || caller!=(uintptr_t)(image+ROTATION_LOAD_SITE+5) ||
       self!=(uintptr_t)(image+ROTATION_GAME_THIS) || (boot_phase!=1 && boot_phase!=2) ||
       !rotation_identity_read(&now,0) || !rotation_same_host(&rotation_menu,&now,0) ||
       !rotation_patch_owned(&rotation_patches[0]) || !rotation_patch_owned(&rotation_patches[1])) goto done;
    rotation_loaded=now;rotation_entry_manager=now.manager;rotation_loaded.manager=0;
    rotation_owner=GetCurrentThreadId();++rotation_serial;rotation_phase=ROTATION_ENTERED;ok=1;
done:if(!ok) {rotation_fault(ROTATION_E_OWNER);rotation_phase=ROTATION_FAILED;}
    ReleaseSRWLockExclusive(&rotation_lock);return ok;
}
static void rotation_load_leave(uintptr_t result,int normal) {
    AcquireSRWLockExclusive(&rotation_lock);
    rotation_identity now;unsigned char flag=1;
    if(normal) {
        ++rotation_load_returns;rotation_load_result=result;
        if((result&255u)==1) ++rotation_load_true;
        else if(!(result&255u)) ++rotation_load_false;
        else rotation_fault(ROTATION_E_RETURN);
    }
    if(!rotation_load_open) rotation_fault(ROTATION_E_SCOPE);else --rotation_load_open;
    if(!normal) {++rotation_load_abnormal;rotation_fault(ROTATION_E_UNWIND);}
    else if((result&255u)!=1 || rotation_owner!=GetCurrentThreadId() || rotation_scope_open || rotation_is_stopped(NULL))
        rotation_fault(ROTATION_E_RETURN);
    else if(!rotation_identity_read(&now,0) || !rotation_same_host(&rotation_loaded,&now,1) ||
        !rotation_loaded.manager || now.manager!=rotation_loaded.manager ||
        !rotation_read_at(now.manager,0x89,&flag,1) || flag) rotation_fault(ROTATION_E_RETURN);
    if(!rotation_failure && rotation_phase==ROTATION_IMPORTED) rotation_phase=ROTATION_RETURNED;
    else rotation_phase=ROTATION_FAILED;
    if(!rotation_failure && !rotation_counts_complete()) {rotation_fault(ROTATION_E_COUNTS);rotation_phase=ROTATION_FAILED;}
    ReleaseSRWLockExclusive(&rotation_lock);
}
static uintptr_t rotation_load_run(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e) {
    rotation_load_fn original=(rotation_load_fn)(image+ROTATION_LOAD_TARGET);uintptr_t result=0;
    rotation_load_enter((uintptr_t)__builtin_return_address(0),a);
    __try {result=original(a,b,c,d,e);} __finally {rotation_load_leave(result,!AbnormalTermination());}
    return result; /* Full opaque RAX, even though the native caller tests AL. */
}
typedef struct {
    void *basis,*xml,*agent,*movement;uintptr_t bank,result;unsigned role,selector,class_id,accounted;
    rotation_identity identity;uint64_t serial;
    rotation_reject_trace *diagnostic;unsigned diagnostic_claimed;
} rotation_call;
static void rotation_reject_publish_locked(rotation_call *c) {
    rotation_reject_trace *d=c->diagnostic;
    if(!d || rotation_reject_first.present)return;
    rotation_reject_entry *e=&rotation_reject_first;
    e->stage=d->stage;e->predicate=d->predicate;e->helper_known=d->helper_known;e->helper_result=d->helper_result;e->operands_known=d->operands_known;
    e->actual=d->actual;e->expected=d->expected;e->serial=rotation_serial;
    memcpy(e->selected,rotation_selected,sizeof e->selected);e->load_calls=rotation_load_calls;e->load_returns=rotation_load_returns;
    e->image_base=(uintptr_t)image;e->basis=(uintptr_t)c->basis;e->xml=(uintptr_t)c->xml;
    e->agent=(uintptr_t)c->agent;e->movement=(uintptr_t)c->movement;e->bank=c->bank;
    e->role=c->role;e->class_id=c->class_id;e->accounted=c->accounted;e->owner=rotation_owner;e->thread=GetCurrentThreadId();
    e->phase=rotation_phase;e->scope_open=rotation_scope_open;e->load_open=rotation_load_open;e->primary_failure=(unsigned)rotation_failure;
    e->trace=*d;e->present=1;c->diagnostic_claimed=1;
}
static void rotation_reject_publish(rotation_call *c) {
    AcquireSRWLockExclusive(&rotation_lock);rotation_reject_publish_locked(c);ReleaseSRWLockExclusive(&rotation_lock);
}
static void rotation_reject_complete_locked(rotation_call *c,int returned,int abnormal) {
    if(!c->diagnostic_claimed || rotation_reject_done.present)return;
    rotation_reject_done=(rotation_reject_completion){1,(unsigned)returned,(unsigned)abnormal,(unsigned)rotation_failure,
        rotation_scope_open,rotation_load_open,returned?c->result:0,{rotation_role_returns[0],rotation_role_returns[1]}};
}
static int rotation_guard_locked(rotation_call *c,struct x4rx_guard *out) {
    rotation_reject_trace *d=c->diagnostic;
    rotation_identity now;unsigned char flag=0;unsigned readers=1,selector=2,other=2,app=0;
    if(rotation_is_stopped(d))return 0;
    RR_CHECK(!rotation_configured,RP_CONFIGURED,rotation_configured,1);
    DWORD current=GetCurrentThreadId();RR_CHECK(rotation_owner!=current,RP_OWNER_THREAD,current,rotation_owner);
    RR_CHECK(rotation_phase!=ROTATION_ENTERED && rotation_phase!=ROTATION_IMPORTED,RP_PHASE,rotation_phase,ROTATION_ENTERED);
    RR_CHECK(rotation_load_calls!=1,RP_LOAD_CALLS,rotation_load_calls,1);
    RR_CHECK(rotation_load_returns,RP_LOAD_RETURNS,rotation_load_returns,0);
    RR_CHECK(!rotation_scope_open,RP_SCOPE_OPEN,rotation_scope_open,1);
    if(!rotation_identity_read_observed(&now,0,d))return 0;
    RR_CHECK(!rotation_same_host(&rotation_loaded,&now,1),RP_HOST_CHANGED,now.module,rotation_loaded.module);
    RR_CHECK(!rotation_read_at_observed(now.manager,0x89,&flag,1,d),RP_MANAGER_FLAG_READ,0,0);
    RR_CHECK(flag!=1,RP_MANAGER_FLAG,flag,1);
    RR_CHECK(!rotation_read_at_observed(now.manager,0x40,&readers,4,d),RP_READERS_READ,0,0);
    RR_CHECK(readers,RP_READERS,readers,0);
    RR_CHECK(!rotation_observed_read(d,image+ROTATION_APP_FLAGS_RVA,&app,4),RP_APP_READ,0,0);
    RR_CHECK(!(app&4),RP_APP,app,4);
    RR_CHECK(!rotation_observed_read(d,image+(c->role?0x2f4b6f4:0x38f4ee4),&selector,4),RP_SELECTOR_READ,0,0);
    RR_CHECK(selector>1,RP_SELECTOR,selector,1);
    RR_CHECK(!rotation_observed_read(d,image+(c->role?0x38f4ee4:0x2f4b6f4),&other,4),RP_OTHER_READ,0,0);
    RR_CHECK(other!=1-selector,RP_OTHER,other,1-selector);
    if(!rotation_patch_owned_observed(&rotation_patches[0],d) || !rotation_patch_owned_observed(&rotation_patches[1],d))return 0;
    if(!rotation_loaded.manager) {rotation_loaded.manager=now.manager;rotation_phase=ROTATION_IMPORTED;}
    RR_CHECK(now.manager!=rotation_loaded.manager,RP_MANAGER_CHANGED,now.manager,rotation_loaded.manager);
    RR_CHECK(!c->agent,RP_AGENT_NULL,c->agent,1);
    RR_CHECK((uintptr_t)c->agent&15,RP_AGENT_ALIGN,c->agent,16);
    RR_CHECK((uintptr_t)c->agent>UINTPTR_MAX-0xad0-0x1a0-selector*0x1a0,RP_AGENT_OVERFLOW,c->agent,selector);
    RR_CHECK((uintptr_t)c->basis!=(uintptr_t)c->agent+0xae0+selector*0x1a0,RP_BASIS,c->basis,(uintptr_t)c->agent+0xae0+selector*0x1a0);
    if(c->serial) {
        RR_CHECK(c->serial!=rotation_serial,RP_CALL_SERIAL,c->serial,rotation_serial);
        RR_CHECK(c->selector!=selector,RP_CALL_SELECTOR,c->selector,selector);
        RR_CHECK(!rotation_same_host(&c->identity,&now,1),RP_CALL_HOST,now.module,c->identity.module);
        RR_CHECK(c->identity.manager!=now.manager,RP_CALL_MANAGER,now.manager,c->identity.manager);
    }
    c->serial=rotation_serial;c->selector=selector;c->identity=now;c->bank=(uintptr_t)c->basis-16;
    *out=(struct x4rx_guard){rotation_serial,rotation_owner,GetCurrentThreadId(),now.module,now.manager,1,0,0};return 1;
}
static int rotation_guard(void *context,struct x4rx_guard *out) {
    AcquireSRWLockExclusive(&rotation_lock);int ok=rotation_guard_locked((rotation_call*)context,out);
    ReleaseSRWLockExclusive(&rotation_lock);return ok;
}
static int rotation_evaluate(void *unused,const struct x4rx_input *input,uint32_t out[9]) {
    (void)unused;struct x4rot_attribute a[3];
    for(unsigned i=0;i<3;i++) a[i]=(struct x4rot_attribute){input->angles[i].present?input->angles[i].text:NULL,
        input->angles[i].length,input->angles[i].present};
    return x4rot_offset(input->orientation,a,out)==X4ROT_OK?0:-1;
}
static void rotation_call_original(void *context) {
    rotation_call *c=(rotation_call*)context;
    c->result=((rotation_tail_fn)(image+ROTATION_TAIL_TARGET))(c->basis,c->xml);
}
static int rotation_writable(uintptr_t address,size_t bytes) {
    if(!address || bytes>UINTPTR_MAX-address) return 0;uintptr_t end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION m;if(!VirtualQuery((void*)address,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
            m.Protect!=PAGE_READWRITE || (uintptr_t)m.BaseAddress>address || m.RegionSize>UINTPTR_MAX-(uintptr_t)m.BaseAddress) return 0;
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;if(next<=address) return 0;address=next;
    }
    return 1;
}
static int rotation_write_locked(rotation_call *c,const struct x4rx_plan *plan) {
    rotation_reject_trace *d=c->diagnostic;
    struct x4rx_guard g;unsigned char before[X4RX_BANK_SIZE],check[X4RX_BANK_SIZE],expected[X4RX_BANK_SIZE];
    RR_CHECK(!c->accounted || c->class_id>1,RP_ACCOUNTED,c->accounted,c->class_id);
    if(!rotation_guard_locked(c,&g))return 0;
    RR_CHECK(!rotation_writable(c->bank,sizeof(before)),RP_WRITABLE,c->bank,sizeof(before));
    RR_CHECK(!rotation_observed_read(d,(void*)c->bank,before,sizeof(before)),RP_READ,c->bank,sizeof(before));
    RR_CHECK(!rotation_observed_read(d,(void*)c->bank,check,sizeof(check)),RP_READ,c->bank,sizeof(check));
    RR_CHECK(memcmp(before,check,sizeof(before)),RP_BANK_CHANGED,0,0);
    memcpy(expected,before,sizeof(before));
    for(unsigned i=0;i<9;i++) {
        unsigned offset=0x10+(i/3)*16+(i%3)*4;uint32_t word;
        memcpy(&word,before+offset,4);
        RR_CHECK(plan->words[i].offset!=offset,RP_PLAN_WORD,plan->words[i].offset,offset);
        RR_CHECK(word!=plan->words[i].before,RP_PLAN_WORD,word,plan->words[i].before);
        memcpy(expected+offset,&plan->words[i].candidate,4);
    }
    if(!rotation_guard_locked(c,&g)) return 0;
    for(unsigned i=0;i<9;i++) *(volatile uint32_t*)(c->bank+plan->words[i].offset)=plan->words[i].candidate;
    /* No rollback claim: a fault/foreign change is an unpublishable world. */
    RR_CHECK(!rotation_observed_read(d,(void*)c->bank,check,sizeof(check)),RP_READ,c->bank,sizeof(check));
    RR_CHECK(memcmp(expected,check,sizeof(check)),RP_WRITE_CHANGED,0,0);
    ++rotation_written[c->role];++rotation_class_written[c->role][c->class_id];return 1;
}
static uintptr_t rotation_tail_selected(rotation_call *c) {
    rotation_reject_trace *d=c->diagnostic;
    rotation_reject_stage_begin(d,RR_ADMISSION);
    AcquireSRWLockExclusive(&rotation_lock);
    ++rotation_tail_calls;++rotation_selected[c->role];++rotation_scope_open;
    if(rotation_scope_open!=1) {
        rotation_reject_pred(d,RP_SCOPE_DEPTH,rotation_scope_open,1);rotation_reject_publish_locked(c);rotation_fault(ROTATION_E_SCOPE);
    }
    if(rotation_selected[c->role]>X4ROT_IMPORT_EULER_PER_BANK) {
        rotation_reject_pred(d,RP_COUNT,rotation_selected[c->role],X4ROT_IMPORT_EULER_PER_BANK);rotation_reject_publish_locked(c);rotation_fault(ROTATION_E_COUNTS);
    }
    ReleaseSRWLockExclusive(&rotation_lock);
    int returned=0,original_open=0;struct x4rx_plan plan;struct x4rs_snapshot snapshot;
    struct x4rx_memory memory={d,rotation_read,rotation_is_stopped};
    __try {
        struct x4rx_guard before={0},after={0};uint32_t candidate[9];
        rotation_reject_stage_begin(d,RR_PRE_GUARD);
        int r=rotation_guard(c,&before)?X4RX_OK:X4RX_GUARD;
        rotation_reject_helper(d,r);if(r)rotation_reject_publish(c);
        if(!r) {
            rotation_reject_stage_begin(d,RR_CAPTURE);
            r=x4rs_capture(&memory,&x4rx_windows,(uintptr_t)c->movement,c->role,(uintptr_t)c->xml,X4RX_ROTATION_NODE,&snapshot);
            rotation_reject_helper(d,r);if(r)rotation_reject_publish(c);else c->class_id=snapshot.class_id;
        }
        if(!r) {
            rotation_reject_stage_begin(d,RR_CLAIM_GUARD);
            AcquireSRWLockExclusive(&rotation_lock);
            if(!rotation_guard_locked(c,&after))r=X4RX_GUARD;
            else if(!x4rx_same_guard(&before,&after)) {rotation_reject_pred(d,RP_GUARD_CHANGED,0,0);r=X4RX_GUARD;}
            else {
                rotation_reject_stage_begin(d,RR_LIFETIME);
                if(!rotation_lifetime_enter_observed((uintptr_t)c->agent,(uintptr_t)c->movement,c->bank,c->role,snapshot.class_id,d))r=X4RX_GUARD;
                else {
                    c->class_id=snapshot.class_id;c->accounted=1;++rotation_class_calls[c->role][c->class_id];
                    if(rotation_class_calls[c->role][c->class_id]>(c->class_id?X4ROT_IMPORT_PLAYER_EULER:X4ROT_IMPORT_NAVIGATION_EULER)) {
                        rotation_reject_stage_begin(d,RR_CLASS_COUNT);
                        rotation_reject_pred(d,RP_LIMIT,rotation_class_calls[c->role][c->class_id],c->class_id?X4ROT_IMPORT_PLAYER_EULER:X4ROT_IMPORT_NAVIGATION_EULER);r=X4RX_BOUNDS;
                    }
                }
            }
            rotation_reject_helper(d,r);if(r)rotation_reject_publish_locked(c);
            ReleaseSRWLockExclusive(&rotation_lock);
        }
        if(!r) {
            rotation_reject_stage_begin(d,RR_EVALUATE);
            if(rotation_evaluate(NULL,&snapshot.xml.input,candidate)) {rotation_reject_pred(d,RP_MATH,0,0);r=X4RX_EVALUATOR;}
            rotation_reject_helper(d,r);if(r)rotation_reject_publish(c);
        }
        original_open=1;rotation_call_original(c);returned=1;original_open=0; /* Original once; fault publication timing is unchanged. */
        if(!r) {
            rotation_reject_stage_begin(d,RR_POST_GUARD);
            if(!rotation_guard(c,&after))r=X4RX_GUARD;
            else if(!x4rx_same_guard(&before,&after)){rotation_reject_pred(d,RP_GUARD_CHANGED,0,0);r=X4RX_GUARD;}
            rotation_reject_helper(d,r);if(r)rotation_reject_publish(c);
        }
        if(!r) {
            rotation_reject_stage_begin(d,RR_REPLAY);r=x4rs_unchanged(&memory,&snapshot);
            rotation_reject_helper(d,r);if(r)rotation_reject_publish(c);
        }
        if(!r)rotation_reject_stage_begin(d,RR_PLAN);
        for(unsigned i=0;!r && i<9;i++) {
            unsigned char bytes[4];uint64_t address;
            if((candidate[i]&UINT32_C(0x7f800000))==UINT32_C(0x7f800000)){rotation_reject_pred(d,RP_NONFINITE,candidate[i],i);r=X4RX_EVALUATOR;break;}
            plan.words[i].offset=(uint16_t)(16+(i/3)*16+(i%3)*4);
            r=x4rx_address(c->bank,plan.words[i].offset,4,&address);
            if(r)rotation_reject_pred(d,RP_ADDRESS,c->bank,plan.words[i].offset);
            if(!r){r=x4rx_copy(&memory,address,bytes,4);if(r)rotation_reject_pred(d,RP_READ,address,4);}
            if(!r){plan.words[i].before=x4rx_u32(bytes);plan.words[i].candidate=candidate[i];}
        }
        if(r && d && !c->diagnostic_claimed) {rotation_reject_helper(d,r);rotation_reject_publish(c);}
        if(r!=X4RX_OK) rotation_fault(r==X4RX_EVALUATOR?ROTATION_E_MATH:ROTATION_E_INPUT);
        else {
            AcquireSRWLockExclusive(&rotation_lock);
            __try {
                rotation_reject_stage_begin(d,RR_PUBLISH_REPLAY);int checked=x4rs_unchanged(&memory,&snapshot);
                rotation_reject_helper(d,checked);
                if(checked) {rotation_reject_publish_locked(c);rotation_fault(ROTATION_E_WRITE);}
                else {
                    rotation_reject_stage_begin(d,RR_PUBLISH_GUARD);
                    if(!rotation_write_locked(c,&plan)){rotation_reject_publish_locked(c);rotation_fault(ROTATION_E_WRITE);}
                }
            }
            __finally {ReleaseSRWLockExclusive(&rotation_lock);}
        }
    } __finally {
        AcquireSRWLockExclusive(&rotation_lock);
        if(returned) {++rotation_tail_returns;++rotation_role_returns[c->role];if(c->accounted)++rotation_class_returns[c->role][c->class_id];}
        int abnormal=AbnormalTermination();
        if(abnormal) {
            if(d && !c->diagnostic_claimed) {
                if(original_open)rotation_reject_stage_begin(d,RR_ORIGINAL);
                d->helper_known=0;d->helper_result=0;rotation_reject_pred(d,RP_UNWIND,0,0);rotation_reject_publish_locked(c);
            }
            rotation_fault(ROTATION_E_UNWIND);
        }
        if(!rotation_scope_open) rotation_fault(ROTATION_E_SCOPE);else --rotation_scope_open;
        rotation_reject_complete_locked(c,returned,abnormal);
        ReleaseSRWLockExclusive(&rotation_lock);
    }
    return c->result;
}
/* Keep the optional bounded recorder off the ordinary caller's stack. */
static __attribute__((noinline)) uintptr_t rotation_tail_observed(rotation_call *c) {
    rotation_reject_trace trace;c->diagnostic=&trace;return rotation_tail_selected(c);
}
static uintptr_t rotation_tail_run(void *basis,void *xml,void *agent,void *movement) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    rotation_tail_fn original=(rotation_tail_fn)(image+ROTATION_TAIL_TARGET);
    if(caller!=(uintptr_t)(image+0xe86d18) && caller!=(uintptr_t)(image+0xe87101)) {
        InterlockedIncrement64((volatile LONG64*)&rotation_foreign);return original(basis,xml);
    }
    rotation_call c={0};c.basis=basis;c.xml=xml;c.agent=agent;c.movement=movement;c.class_id=2;
    c.role=caller==(uintptr_t)(image+0xe87101);if((uintptr_t)basis>=16)c.bank=(uintptr_t)basis-16;
    return InterlockedCompareExchange(&rotation_reject_mode,0,0)==1?rotation_tail_observed(&c):rotation_tail_selected(&c);
}
#undef RR_CHECK
