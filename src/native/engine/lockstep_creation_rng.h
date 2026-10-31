/* Exact selected constructor A / lazy Navigation B intervention. Only this
 * thread's validated TLS RNG word is borrowed; no native identity is enrolled. */
#include "lockstep_creation_rng_contract.h"
enum {CRNG_CONFIG=1,CRNG_TOKEN=2,CRNG_SCOPE=4,CRNG_TLS=8,CRNG_PINS=16,
    CRNG_STOP=32,CRNG_UNWIND=64,CRNG_CHILD=128,CRNG_RESTORE=256,CRNG_NATIVE=512};
typedef struct {
    uint64_t handle,epoch,tick,phase1,enter,leave,key,saved,scoped_after,restored_word;
    uint64_t tls,address,time,allocation,region,region_size;
    uint64_t failures_before[12],failures_after[12];
    unsigned scope,creation,thread,errors,admitted,original,normal,closed,restored,stop_overlap;
    unsigned protection,type;
} creation_rng_record;
static creation_rng_record creation_rng_records[CREATION_RNG_CAPACITY];
static char creation_rng_manifest[CREATION_RNG_MANIFEST_CAP];
static uint64_t creation_rng_keys[2];
static uint32_t creation_rng_epoch;
static volatile LONG creation_rng_started;
static unsigned creation_rng_closing,creation_rng_count,creation_rng_active,creation_rng_attempted;
static uint64_t creation_rng_failures,creation_rng_restore_failures,creation_rng_restore_open;
/* Local boot identity only, never a seed or peer-canonical value. CNG does not
 * consume the game's RNG. An acknowledgement cannot reset scope admission. */
static char creation_rng_boot[33],creation_rng_boot_secret[33],creation_rng_boot_digest[65];
static unsigned creation_rng_acks,creation_rng_boot_slot,creation_rng_boot_port;
static uint64_t creation_rng_boot_started;
static unsigned creation_rng_ack_mode,creation_rng_ack_phase,creation_rng_ack_registry_phase,creation_rng_ack_registry;
static char creation_rng_ack_receipt[384];
static unsigned creation_attach_rejected;
enum {CREATION_RNG_ACK_LIMIT=8};

/* No runtime mask reduction: support exactly the already exercised016 writers.
 * Optional Lua-configured writers do not all live in boot_options. */
static int creation_rng_composition(void) {
    return boot_options==CREATION_RNG_OPTIONS && flight_seeded && ai_seeded && agent_seeded && ship_seeded &&
        weapon_seeded && nav_seeded && shape_seeded && bullet_seeded && death_seeded && attention_seeded &&
        gravidar_seeded && interrupt_seeded && !world_wait_started && !world_wait_rng_started && !world_find_started &&
        !world_action_started && !world_values_started && !player_fire_enabled && !player_weapon_seeded &&
        !transition_started && !origin_started && !getup_ledger.configured && !rotation_configured;
}
static void creation_rng_failure_locked(creation_rng_record *r,unsigned error) {
    if(!error)return;if(r)r->errors|=error;++creation_rng_failures;
    creation_error_locked(CREATION_E_SCOPE);
}
static void creation_rng_child_failures(uint64_t out[12]) {
    volatile LONG64 *sources[]={
        (volatile LONG64*)&flight_failures,(volatile LONG64*)&ai_failures,(volatile LONG64*)&agent_failures,
        (volatile LONG64*)&ship_failures,(volatile LONG64*)&weapon_failures,(volatile LONG64*)&nav_failures,
        (volatile LONG64*)&shape_failures,(volatile LONG64*)&bullet_failures,(volatile LONG64*)&death_failures,
        (volatile LONG64*)&attention_failures,&gravidar_failures,&interrupt_failures};
    for(unsigned i=0;i<12;i++)out[i]=(uint64_t)InterlockedCompareExchange64(sources[i],0,0);
}
/* Lock held. Reconstruct only exact still-owned patch bytes in private buffers,
 * then recheck full original bodies, including caller/initializer coverage. */
static int creation_rng_pins_locked(void) {
    if(creation_owned!=(1u<<CREATION_HOOK_COUNT)-1)return 0;
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++) {
        const creation_patch_contract *p=&creation_patch_contracts[i];unsigned char bytes[16];
        if(!read_local(image+p->word,bytes,p->width) || memcmp(bytes,creation_patched_words[i],p->width))return 0;
    }
    unsigned char bytes[16384];
    for(unsigned i=0;i<sizeof(creation_pins)/sizeof(*creation_pins);i++) {
        const creation_pin *p=&creation_pins[i];
        if(p->length>sizeof(bytes) || !read_local(image+p->rva,bytes,p->length))return 0;
        for(unsigned j=0;j<CREATION_HOOK_COUNT;j++) {
            const creation_patch_contract *patch=&creation_patch_contracts[j];
            if(patch->word>=p->rva && patch->word+patch->width<=p->rva+p->length)
                memcpy(bytes+patch->word-p->rva,creation_original_words[j],patch->width);
        }
        if(!hash_ok(bytes,p->length,p->sha256))return 0;
    }
    return 1;
}
#include "lockstep_creation_attach.h"
static int creation_rng_memory(uint64_t address,MEMORY_BASIC_INFORMATION *info) {
    return address && !(address&7) && address<=UINTPTR_MAX-8 &&
        VirtualQuery((void*)(uintptr_t)address,info,sizeof(*info))==sizeof(*info) && info->State==MEM_COMMIT &&
        (info->Protect==PAGE_READWRITE || info->Protect==PAGE_WRITECOPY) &&
        address>=(uintptr_t)info->BaseAddress && info->RegionSize>=8 &&
        address-(uintptr_t)info->BaseAddress<=info->RegionSize-8;
}
static int creation_rng_tls_read(void *tls,uint64_t expected_time,uint64_t *word,MEMORY_BASIC_INFORMATION *info) {
    uint64_t a=0,b=0,time=0,again=0;unsigned char view=0,view_after=0;MEMORY_BASIC_INFORMATION second;
    double clock_value;memcpy(&clock_value,&expected_time,8);
    uint64_t address=(uintptr_t)registry_origin_rng(tls);
    return tls && isfinite(clock_value) && creation_rng_memory(address,info) &&
        creation_read((uintptr_t)tls,0x300,&time,8) && creation_read((uintptr_t)tls,0x314,&view,1) &&
        time==expected_time && view==1 && creation_read(address,0,&a,8) && creation_read(address,0,&b,8) && a==b &&
        creation_read((uintptr_t)tls,0x300,&again,8) && time==again &&
        creation_read((uintptr_t)tls,0x314,&view_after,1) && view_after==view && creation_rng_memory(address,&second) &&
        second.AllocationBase==info->AllocationBase && second.BaseAddress==info->BaseAddress &&
        second.RegionSize==info->RegionSize && second.Protect==info->Protect && second.Type==info->Type && (*word=a,1);
}
/* This only contains our aligned write. A fault does not escape while holding
 * creation_lock and does not intercept any exception raised by a native call. */
static int creation_rng_write(uint64_t address,uint64_t value) {
    __try {InterlockedExchange64((volatile LONG64*)(uintptr_t)address,(LONG64)value);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return 0;}
    return 1;
}
static int creation_rng_boot_matches(unsigned mode,uint32_t epoch,const char *manifest,size_t length) {
    return creation_rng_started && creation_rng_ack_mode==mode && !creation_rng_closing && !creation_rng_failures &&
        creation_rng_epoch==epoch && strlen(creation_rng_manifest)==length && !memcmp(creation_rng_manifest,manifest,length) &&
        creation_rng_acks>0 && creation_rng_acks<CREATION_RNG_ACK_LIMIT &&
        creation_rng_boot_slot==stats.slot && creation_rng_boot_port==remote_port &&
        creation_rng_boot_started==boot_started && !strcmp(creation_rng_boot_secret,secret) &&
        !strcmp(creation_rng_boot_digest,initial_state_digest) &&
        (mode==1 || (creation_attach_boot.valid && !creation_attach_rejected &&
                     !strcmp(creation_rng_boot,creation_attach_boot.nonce)));
}
static void creation_attach_reject(void) {
    AcquireSRWLockExclusive(&creation_lock);
    if(creation_attach_boot.valid && GetCurrentThreadId()==creation_attach_boot.thread)creation_attach_rejected=1;
    ReleaseSRWLockExclusive(&creation_lock);
}
static int creation_rng_configure(const char *manifest,size_t length,const char *preimage,size_t preimage_length) {
    uint32_t epoch=0;unsigned mode=creation_rng_preimage_mode(preimage,preimage_length,manifest,length);
    unsigned registry=mode==0;
    if(!mode)mode=(unsigned)registry_creation_rng_preimage(preimage,preimage_length,manifest,length);
    registry_creation_attachment attachment={0};
    if(!creation_rng_parse(manifest,length,&epoch) || !mode ||
       !hash_ok(preimage,preimage_length,initial_state_digest) ||
       !registry_creation_attachment_snapshot(&attachment)) {creation_attach_reject();return 0;}
    AcquireSRWLockExclusive(&creation_lock);
    int ok=stats.active && boot_mode && GetCurrentThreadId()==ui_thread && creation_enabled && !creation_closing &&
        !creation_failures && !registry_md_failures && !registry_origin_failures && !registry_failures &&
        creation_rng_composition() && creation_rng_pins_locked();
    ok=ok && used && wire_pair() && stats.slot>=1 && stats.slot<=2 && valid_hex(secret,32) &&
        !stats.issued && !creation_selected.state && !creation_count &&
        !creation_open && !creation_rng_count && !creation_rng_active && !creation_rng_attempted;
    unsigned phase=(unsigned)boot_phase,prior_acks=creation_rng_started?creation_rng_acks:0;
    if(mode==1)ok=ok && phase==0;
    else {
        ok=ok && !creation_attach_rejected && !creation_rng_restore_failures && !creation_rng_restore_open &&
            !creation_restore_failures && !creation_restore_open &&
            attachment.configured==registry && (!registry || (attachment.mode==2 && attachment.acks==prior_acks+1)) &&
            (registry || attachment.acks==0) && attachment.phase<=phase;
        if(creation_rng_started)ok=ok && phase>=1 && phase<=3 && phase>=creation_rng_ack_phase &&
            creation_attach_matches(manifest,length,preimage,preimage_length,epoch,registry);
        else ok=ok && phase==0 && creation_attach_prepare(manifest,length,preimage,preimage_length,epoch,registry);
    }
    if(creation_rng_started) {
        ok=ok && creation_rng_boot_matches(mode,epoch,manifest,length);
    } else {
        unsigned char nonce[16];
        if(ok && mode==1 && BCryptGenRandom(NULL,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)ok=0;
        if(ok) {
            if(mode==1)for(unsigned i=0;i<16;i++)snprintf(creation_rng_boot+i*2,3,"%02x",nonce[i]);
            else strcpy(creation_rng_boot,creation_attach_boot.nonce);
            strcpy(creation_rng_boot_secret,secret);strcpy(creation_rng_boot_digest,initial_state_digest);
            creation_rng_boot_slot=stats.slot;creation_rng_boot_port=remote_port;
            creation_rng_boot_started=boot_started;creation_rng_acks=0;
            memcpy(creation_rng_manifest,manifest,length);creation_rng_manifest[length]=0;creation_rng_epoch=epoch;
            for(unsigned i=0;i<2;i++)creation_rng_keys[i]=creation_rng_key(i?0x43524541545f4231ULL:0x43524541545f4131ULL,
                epoch,5,0x0000000500000003ULL,3,276944566,CREATION_RNG_MACRO,"player",CREATION_RNG_SNAPSHOT,CREATION_RNG_PLAN);
            InterlockedExchange(&creation_rng_started,1);
        }
    }
    unsigned accepted_phase=(unsigned)boot_phase;
    ok=ok && stats.active && !creation_closing && (mode==1?accepted_phase==0:
        prior_acks?(accepted_phase>=phase && accepted_phase>=1 && accepted_phase<=3):accepted_phase==0);
    /* Phase3 can stay unchanged while the main thread admits its first frame.
     * A fresh atomic observation at ACK2 commit must not reuse the early load. */
    if(mode==2)ok=ok && InterlockedCompareExchange64((volatile LONG64*)&stats.issued,0,0)==0;
    if(ok) {
        ++creation_rng_acks;creation_rng_ack_mode=mode;creation_rng_ack_phase=accepted_phase;
        creation_rng_ack_registry=registry?attachment.acks:0;creation_rng_ack_registry_phase=registry?attachment.phase:0;
        if(mode==1)snprintf(creation_rng_ack_receipt,sizeof(creation_rng_ack_receipt),
            "schema=1 ordinal=%u boot=%s digest=%s slot=%u phase=0 issued=0 state=0 records=0 scopes=0 attempted=0",
            creation_rng_acks,creation_rng_boot,creation_rng_boot_digest,creation_rng_boot_slot);
        else snprintf(creation_rng_ack_receipt,sizeof(creation_rng_ack_receipt),
            "schema=2 ordinal=%u boot=%s digest=%s slot=%u phase=%u registry_phase=%u reattach=%u registry_acks=%u issued=0 state=0 records=0 scopes=0 attempted=0",
            creation_rng_acks,creation_rng_boot,creation_rng_boot_digest,creation_rng_boot_slot,accepted_phase,
            creation_rng_ack_registry_phase,creation_rng_acks>1,creation_rng_ack_registry);
    } else if(mode==2 && creation_attach_boot.valid && GetCurrentThreadId()==creation_attach_boot.thread)creation_attach_rejected=1;
    ReleaseSRWLockExclusive(&creation_lock);return ok;
}

static int creation_rng_ack(char *out,size_t capacity) {
    AcquireSRWLockShared(&creation_lock);
    int n=creation_rng_started?snprintf(out,capacity,"%s",creation_rng_ack_receipt):0;
    ReleaseSRWLockShared(&creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
typedef void *(*creation_rng_tls_reader)(void);
static int creation_rng_prior_static(const creation_record *record) {
    uint64_t a=0,b=0;
    return record && record->before.identity.stable==1 &&
        record->before.identity.first.controller==creation_selected.prior_static &&
        record->before.identity.first.controller_type==0x2a34b38 &&
        creation_read(creation_selected.object,0x200,&a,8) &&
        creation_read(creation_selected.object,0x200,&b,8) && a==b && a==creation_selected.prior_static;
}
static void creation_rng_begin_using(unsigned index,unsigned kind,uintptr_t caller,const uint64_t *args,unsigned *scope,
        creation_rng_tls_reader current_tls) {
    *scope=0;if(!InterlockedCompareExchange(&creation_rng_started,0,0))return;
    DWORD saved_error=GetLastError();unsigned n=kind==CREATION_GENERATE?0:1;
    AcquireSRWLockExclusive(&creation_lock);
    /* Unrelated creation calls outside the selected lifetime remain untouched. */
    if((kind!=CREATION_GENERATE && kind!=CREATION_NAVIGATION) || !creation_selected.state || creation_selected.state>4 ||
       (kind==CREATION_NAVIGATION && args[1]!=creation_selected.object))goto done;
    if(creation_rng_closing || !stats.active || creation_closing) {creation_rng_failure_locked(NULL,CRNG_STOP);goto done;}
    if(creation_rng_attempted&(1u<<n)) {creation_rng_failure_locked(NULL,CRNG_SCOPE);goto done;}
    creation_rng_attempted|=1u<<n;
    creation_rng_record *r=&creation_rng_records[n];memset(r,0,sizeof(*r));r->scope=n+1;r->creation=index;
    r->epoch=creation_rng_epoch;r->handle=creation_selected.handle;r->tick=creation_selected.execution_tick;
    r->phase1=creation_selected.phase_order;r->thread=GetCurrentThreadId();r->key=creation_rng_keys[n];
    ++creation_rng_count;*scope=n+1;
    unsigned error=0;creation_record *record=index && index<=creation_count?&creation_records[index-1]:NULL;
    if(!record || record->kind!=kind || record->error || record->foreign || record->parent || record->complete ||
       record->stage!=n+1 || record->handle!=0x0000000500000003ULL || r->handle!=record->handle ||
       !r->phase1 || record->phase_order!=r->phase1 ||
       creation_current!=index || creation_open!=1 || creation_failures || creation_rng_failures || creation_rng_active ||
       registry_md_failures || registry_origin_failures || registry_failures ||
       boot_phase!=3 || stats.issued!=r->tick || r->tick<5 || r->tick>6 || creation_selected.reserved_tick!=5 ||
       r->thread!=ui_thread || r->thread!=creation_selected.thread)error|=CRNG_TOKEN;
    if(caller!=(uintptr_t)(image+(n?0xea4b5e:0xba1b8b)))error|=CRNG_PINS;
    if(!creation_rng_composition())error|=CRNG_CONFIG;
    if(!creation_rng_pins_locked())error|=CRNG_PINS;
    if(!n && (creation_selected.state!=2 || creation_selected.generate_count!=1 || args[3]))error|=CRNG_SCOPE;
    if(n && (!creation_selected.a_closed || creation_selected.state!=4 || !creation_rng_records[0].closed ||
       !creation_rng_records[0].normal || !creation_rng_records[0].restored || creation_rng_records[0].errors ||
       creation_selected.navigation_count!=1 || creation_selected.agent_count || !creation_selected.prior_static ||
       args[1]!=creation_selected.object || args[2]!=creation_selected.prior_static ||
       !creation_type(args[2],0x2a34b38) || !creation_rng_prior_static(record) ||
       creation_selected_identity(&creation_selected)))error|=CRNG_TOKEN;
    void *tls=current_tls();MEMORY_BASIC_INFORMATION info;
    if(record) {r->enter=record->enter;r->time=record->before.time;}
    if(!record || record->before.tls!=(uintptr_t)tls || record->before.thread!=r->thread ||
       !creation_rng_tls_read(tls,r->time,&r->saved,&info) || record->before.rng!=r->saved ||
       current_tls()!=tls)error|=CRNG_TLS;
    if(error) {creation_rng_failure_locked(r,error);goto done;}
    r->tls=(uintptr_t)tls;r->address=(uintptr_t)registry_origin_rng(tls);
    r->allocation=(uintptr_t)info.AllocationBase;r->region=(uintptr_t)info.BaseAddress;r->region_size=info.RegionSize;
    r->protection=info.Protect;r->type=info.Type;creation_rng_child_failures(r->failures_before);
    for(unsigned i=0;i<12;i++)if(r->failures_before[i])error|=CRNG_CHILD;
    if(error) {creation_rng_failure_locked(r,error);goto done;}
    /* Publish the owning wrapper's restore obligation before the first write. */
    r->admitted=1;creation_rng_active=n+1;
    if(!creation_rng_write(r->address,r->key))creation_rng_failure_locked(r,CRNG_TLS);
    else {uint64_t check=0;if(!creation_read(r->address,0,&check,8) || check!=r->key)creation_rng_failure_locked(r,CRNG_TLS);}
done:
    ReleaseSRWLockExclusive(&creation_lock);SetLastError(saved_error);
}
static void creation_rng_begin(unsigned index,unsigned kind,uintptr_t caller,const uint64_t *args,unsigned *scope) {
    creation_rng_begin_using(index,kind,caller,args,scope,registry_origin_tls);
}
static void creation_rng_original(unsigned scope) {
    if(!scope)return;DWORD error=GetLastError();AcquireSRWLockExclusive(&creation_lock);
    if(scope>2 || !creation_rng_records[scope-1].scope)creation_rng_failure_locked(NULL,CRNG_SCOPE);
    else {creation_rng_record *r=&creation_rng_records[scope-1];++r->original;
        if(r->original!=1 || r->closed || r->thread!=GetCurrentThreadId())creation_rng_failure_locked(r,CRNG_SCOPE);}
    ReleaseSRWLockExclusive(&creation_lock);SetLastError(error);
}
static void creation_rng_end_using(unsigned scope,int normal,creation_rng_tls_reader current_tls) {
    if(!scope)return;DWORD saved_error=GetLastError();
    AcquireSRWLockExclusive(&creation_lock);
    if(scope>2 || !creation_rng_records[scope-1].scope || creation_rng_records[scope-1].closed) {
        creation_rng_failure_locked(NULL,CRNG_SCOPE);goto done;
    }
    creation_rng_record *r=&creation_rng_records[scope-1];unsigned error=normal?0:CRNG_UNWIND;
    r->normal=normal!=0;
    if(normal && r->original!=1)error|=CRNG_NATIVE;
    if(r->admitted) {
        void *tls=current_tls();MEMORY_BASIC_INFORMATION info;
        if(GetCurrentThreadId()!=r->thread || tls!=(void*)(uintptr_t)r->tls ||
           !creation_rng_tls_read(tls,r->time,&r->scoped_after,&info) || current_tls()!=tls ||
           (uintptr_t)info.AllocationBase!=r->allocation || (uintptr_t)info.BaseAddress!=r->region ||
           info.RegionSize!=r->region_size || info.Protect!=r->protection || info.Type!=r->type)error|=CRNG_RESTORE;
        else if(!creation_rng_write(r->address,r->saved) ||
           !creation_read(r->address,0,&r->restored_word,8) || r->restored_word!=r->saved)error|=CRNG_RESTORE;
        else r->restored=1;
        if(error&CRNG_RESTORE)++creation_rng_restore_failures;
        if(creation_rng_active!=scope)error|=CRNG_SCOPE;else creation_rng_active=0;
        creation_rng_child_failures(r->failures_after);
        if(memcmp(r->failures_before,r->failures_after,sizeof(r->failures_before)))error|=CRNG_CHILD;
        if(!creation_rng_composition())error|=CRNG_CONFIG;
    }
    /* Resident records only on unwind: no borrowed outref/object/context reads. */
    if(normal && r->creation && r->creation<=creation_count) {
        const creation_record *record=&creation_records[r->creation-1];r->leave=record->leave;
        if(!record->complete || record->error || record->unwound || !record->leave || creation_failures ||
           registry_md_failures || registry_origin_failures || registry_failures)error|=CRNG_NATIVE;
    } else if(normal)error|=CRNG_NATIVE;
    if(creation_rng_closing || !stats.active || r->stop_overlap)error|=CRNG_STOP;
    r->closed=1;creation_rng_failure_locked(r,error);
done:
    ReleaseSRWLockExclusive(&creation_lock);SetLastError(saved_error);
}
static void creation_rng_end(unsigned scope,int normal) {creation_rng_end_using(scope,normal,registry_origin_tls);}
static void creation_rng_stop_locked(void) {
    if(!creation_rng_started || creation_rng_closing)return;creation_rng_closing=1;
    if(creation_rng_active) {
        creation_rng_record *r=&creation_rng_records[creation_rng_active-1];r->stop_overlap=1;
        ++creation_rng_restore_open;creation_rng_failure_locked(r,CRNG_STOP);
    }
    if(creation_rng_count!=2 || creation_rng_attempted!=3 || !creation_rng_records[0].closed ||
       !creation_rng_records[1].closed)creation_rng_failure_locked(NULL,CRNG_SCOPE);
}
static void creation_rng_stop(void) {
    if(!InterlockedCompareExchange(&creation_rng_started,0,0))return;
    AcquireSRWLockExclusive(&creation_lock);creation_rng_stop_locked();ReleaseSRWLockExclusive(&creation_lock);
}
static int creation_rng_receipt(char *out,size_t capacity) {
    AcquireSRWLockShared(&creation_lock);
    int n=creation_rng_started?snprintf(out,capacity,"%s;seed_a=%016llx;seed_b=%016llx",
        creation_rng_manifest,creation_rng_keys[0],creation_rng_keys[1]):0;
    ReleaseSRWLockShared(&creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
static int creation_rng_status(char *out,size_t capacity) {
    AcquireSRWLockShared(&creation_lock);
    int n=snprintf(out,capacity,"schema=1 configured=%ld closing=%u count=%u active=%u attempted=%u failures=%llu restore_failures=%llu restore_open=%llu epoch=%u options=%llx seed_a=%016llx seed_b=%016llx",
        creation_rng_started,creation_rng_closing,creation_rng_count,creation_rng_active,creation_rng_attempted,
        creation_rng_failures,creation_rng_restore_failures,creation_rng_restore_open,creation_rng_epoch,boot_options,
        creation_rng_keys[0],creation_rng_keys[1]);
    ReleaseSRWLockShared(&creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
static int creation_rng_ack_status(char *out,size_t capacity) {
    registry_creation_attachment attachment={0};registry_creation_attachment_snapshot(&attachment);
    AcquireSRWLockShared(&creation_lock);
    int mode2=creation_rng_ack_mode==2 || creation_attach_boot.valid;
    unsigned registry_acks=attachment.mode==2?attachment.acks:0;
    int n=mode2?snprintf(out,capacity,"schema=2 acks=%u registry_acks=%u boot=%s digest=%s slot=%u phase=%u registry_phase=%u failed=%u",
        creation_rng_started?creation_rng_acks:0,registry_acks,creation_attach_boot.nonce,creation_attach_boot.digest,
        creation_attach_boot.slot,creation_rng_started?creation_rng_ack_phase:0,attachment.mode==2?attachment.phase:0,
        creation_attach_rejected || registry_acks!=(creation_attach_boot.registry?(creation_rng_started?creation_rng_acks:0):0)):
        snprintf(out,capacity,"schema=1 acks=%u boot=%s digest=%s slot=%u",
        creation_rng_acks,creation_rng_boot,creation_rng_boot_digest,creation_rng_boot_slot);
    ReleaseSRWLockShared(&creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
static int creation_rng_row(unsigned scope,unsigned part,char *out,size_t capacity) {
    if(scope<1 || scope>2 || part>1)return 0;creation_rng_record r;
    AcquireSRWLockShared(&creation_lock);r=creation_rng_records[scope-1];int visible=r.scope && (r.closed || creation_rng_closing);
    ReleaseSRWLockShared(&creation_lock);if(!visible)return 0;
    int n=snprintf(out,capacity,"schema=1 scope=%u part=%u creation=%u epoch=%llu handle=%016llx tick=%llu thread=%u phase1=%llu enter=%llu leave=%llu error=%x admitted=%u original=%u normal=%u closed=%u restored=%u stop_overlap=%u",
        scope,part,r.creation,r.epoch,r.handle,r.tick,r.thread,r.phase1,r.enter,r.leave,r.errors,r.admitted,r.original,
        r.normal,r.closed,r.restored,r.stop_overlap);
    if(n<=0 || (size_t)n>=capacity)return 0;
    char before[204]={0},after[204]={0};
    for(unsigned i=0;i<12;i++) {snprintf(before+i*17,sizeof(before)-i*17,"%016llx%s",r.failures_before[i],i==11?"":",");
        snprintf(after+i*17,sizeof(after)-i*17,"%016llx%s",r.failures_after[i],i==11?"":",");}
    int added=part==0?snprintf(out+n,capacity-(size_t)n," key=%016llx scoped_after=%016llx",r.key,r.scoped_after):
        snprintf(out+n,capacity-(size_t)n," tls=%016llx address=%016llx time=%016llx caller=%016llx restored_word=%016llx allocation=%016llx region=%016llx region_size=%llu protection=%x type=%x child_before=%s child_after=%s",
            r.tls,r.address,r.time,r.saved,r.restored_word,r.allocation,r.region,r.region_size,r.protection,r.type,before,after);
    return added>0 && (size_t)added<capacity-(size_t)n?n+added:0;
}
