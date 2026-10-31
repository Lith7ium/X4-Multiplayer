/* Default-off, read-only semantic snapshot at the joined main FrameTick gate.
 * No extra engine patch, getter, registry enrollment, normalization or option
 * bit. Existing diagnostic hashes/wire protocol remain unchanged here. */
#include "lockstep_portable_state.h"
#define PORTABLE_SCHEMA_SHA "57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e"
#define PORTABLE_CAPTURE_LIMIT 120u
#define PORTABLE_UNPAUSED_RVA 0x6b60e48u

/* Physical selectors and the two noncanonical clocks are local hold guards,
 * never cross-platform identity or portable-fixture-v1 bytes. */
typedef struct {
    uint64_t game,real,unpaused,speed;
    uintptr_t frame,table;uint32_t loading;
    world_bank_phase banks;
} portable_native_hold;
static portable_native_hold portable_native_holds[PORTABLE_CAPTURE_LIMIT+1];
static int portable_native_clock_owned(void) {
    uint64_t main_word,dt;unsigned short branch;unsigned char disabled;
    double fixed=1.0/60.0;uint64_t expected;memcpy(&expected,&fixed,8);
    return installed && dt_installed && !dt_restored && fixed_dt_bits==expected &&
        read_local(image+WORD_RVA,&main_word,8) && main_word==patched_word &&
        read_local(image+0xfa3302,&branch,2) && branch==0x9066 &&
        read_local(image+0x6adfc60,&dt,8) && dt==expected &&
        read_local(image+0x6adfc58,&disabled,1) && !disabled;
}
static int portable_native_clock_pins(void) {
    /* Only the owned fixed-dt word differs in FrameTick. The native writer
     * updates game/real/unpaused at fa3341/fa3372/fa335e respectively. The
     * read-only getter pins the independent unpaused-real address as well. */
    unsigned char tick[2127];unsigned short original=0x0874,observed;
    if(!portable_native_clock_owned() || !read_local(image+TICK_RVA,tick,sizeof(tick))) return 0;
    memcpy(&observed,tick+0xfa3302-TICK_RVA,2);if(observed!=0x9066) return 0;
    memcpy(tick+0xfa3302-TICK_RVA,&original,2);
    return hash_ok(tick,sizeof(tick),"5a966bc940eec10249ced85daf2751984ee5f1a2bfd28889d812831b221519be") &&
        hash_ok(image+0x4b5e50,57,"966a7e1a514a5efd1fa5b17080ae88c684a8a60fbee16ddcbe6c9e730ac11dfa") &&
        portable_native_clock_owned();
}
static int portable_native_hold_read(portable_native_hold *hold,void *caller_frame,unsigned char loading) {
    memset(hold,0,sizeof(*hold));
    uint64_t imported;hold->frame=(uintptr_t)caller_frame;
    return caller_frame && !loading && portable_native_clock_owned() && world_identity_installed && !world_identity_restored &&
        read_local(image+WORLD_IDENTITY_ENTRY,&imported,8) && imported==world_identity_patched_word &&
        read_local(image+TABLE_RVA,&hold->table,8) && hold->table &&
        read_local((void*)(hold->table+0x130),&hold->loading,4) && !hold->loading &&
        read_local(image+SPEED_RVA,&hold->speed,8) && hold->speed==UINT64_C(0x3ff0000000000000) &&
        read_local(image+GAME_RVA,&hold->game,8) &&
        read_local(image+REAL_RVA,&hold->real,8) && read_local(image+PORTABLE_UNPAUSED_RVA,&hold->unpaused,8) &&
        portable_finite64(hold->game,0) && portable_finite64(hold->real,0) && portable_finite64(hold->unpaused,0) &&
        world_banks_read(&hold->banks);
}
static int portable_native_hold_same(const portable_native_hold *a,const portable_native_hold *b) {
    return a->game==b->game && a->real==b->real && a->unpaused==b->unpaused &&
        a->speed==b->speed && a->frame==b->frame && a->table==b->table && a->loading==b->loading &&
        a->banks.read==b->banks.read && a->banks.write==b->banks.write;
}
static int portable_native_hold_current(const portable_native_hold *held,void *caller_frame,unsigned char loading) {
    portable_native_hold first,second;
    return !memcmp(&held->game,&stats.game,8) && !memcmp(&held->real,&stats.real,8) &&
        portable_native_hold_read(&first,caller_frame,loading) && portable_native_hold_read(&second,caller_frame,loading) &&
        portable_native_hold_same(held,&first) && portable_native_hold_same(held,&second);
}
typedef struct {uint64_t saved,runtime;unsigned char *object;unsigned slot,imports;} portable_native_identity;
static portable_native_identity portable_native_ids[PORTABLE_ACTORS];
static char portable_native_manifest[204],portable_native_contract_text[65];
static unsigned char portable_native_contract[32];
static unsigned char portable_native_bytes[PORTABLE_CAPTURE_LIMIT+1][PORTABLE_FRAME_CAPACITY];
static unsigned portable_native_lengths[PORTABLE_CAPTURE_LIMIT+1];
static unsigned portable_native_limit,portable_native_configured;
static volatile LONG portable_native_count;
static volatile LONG64 portable_native_failures,portable_native_calls,portable_native_returns;
static const char *portable_native_stage="off";
static uint64_t portable_native_failure_id;
static SRWLOCK portable_native_import_lock=SRWLOCK_INIT;
/* The existing runtime->saved diagnostic index permits aliases. Portable
 * references require the inverse to be unambiguous as well. Owned storage,
 * populated only by the existing native import observer; never game memory. */
typedef struct {uint64_t saved,runtime;} portable_native_inverse;
static portable_native_inverse *portable_native_inverse_ids;
static unsigned portable_native_inverse_count;
static int portable_native_inverse_record(uint64_t saved,uint64_t runtime) {
    if(!portable_native_inverse_ids || !saved || !runtime) return 0;
    unsigned bucket=world_identity_bucket(saved);
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        portable_native_inverse *entry=&portable_native_inverse_ids[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(entry->saved==saved) return entry->runtime==runtime;
        if(!entry->saved) {
            if(portable_native_inverse_count>=WORLD_IDENTITY_CAPACITY/2) return 0;
            *entry=(portable_native_inverse){saved,runtime};++portable_native_inverse_count;return 1;
        }
    }
    return 0;
}
static uint64_t portable_native_inverse_lookup(uint64_t saved) {
    if(!portable_native_inverse_ids || !saved) return 0;
    unsigned bucket=world_identity_bucket(saved);
    for(unsigned i=0;i<WORLD_IDENTITY_PROBES;i++) {
        portable_native_inverse *entry=&portable_native_inverse_ids[(bucket+i)&(WORLD_IDENTITY_CAPACITY-1)];
        if(entry->saved==saved) return entry->runtime;
        if(!entry->saved) break;
    }
    return 0;
}

static int portable_native_hex(const char *text,unsigned length,unsigned char *bytes) {
    for(unsigned i=0;i<length;i+=2) {
        unsigned value=0;
        for(unsigned j=0;j<2;j++) {
            char c=text[i+j];unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
            if(digit>15) return 0;value=(value<<4)|digit;
        }
        bytes[i/2]=(unsigned char)value;
    }
    return 1;
}
static int portable_native_config(const char *manifest,size_t length,const char *contract,size_t digest_length,unsigned last) {
    portable_native_identity ids[PORTABLE_ACTORS]={0};unsigned char digest_bytes[32];
    if(!manifest || length!=203 || !contract || digest_length!=64 || !last || last>PORTABLE_CAPTURE_LIMIT ||
       !portable_native_hex(contract,64,digest_bytes)) return 0;
    for(unsigned i=0;i<PORTABLE_ACTORS;i++) {
        unsigned char bytes[8];uint64_t saved=0;
        if(!portable_native_hex(manifest+i*17,16,bytes) || (i<11 && manifest[i*17+16]!=',')) return 0;
        for(unsigned j=0;j<8;j++) saved=(saved<<8)|bytes[j];
        if(!saved) return 0;unsigned position=i;
        while(position && ids[position-1].saved>saved) {ids[position]=ids[position-1];--position;}
        if((position && ids[position-1].saved==saved) || (position<i && ids[position].saved==saved)) return 0;
        ids[position]=(portable_native_identity){.saved=saved,.slot=i+1};
    }
    if(portable_native_configured)
        return last==portable_native_limit && !memcmp(manifest,portable_native_manifest,203) &&
            !memcmp(contract,portable_native_contract_text,64);
    /* The portable contract and its validators are fixed at 1/60 s. */
    if(gate_tick_hz!=GATE_TICK_HZ_DEFAULT || !stats.active || !boot_mode || boot_phase || stats.issued || !wire_pair() || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || world_identity_count || !portable_native_clock_pins() ||
       !hash_ok(image+0xced70,32,"de126e38d2532250fc386f851f355cdb4812bf6b4507ace0549728e27505f268") ||
       !hash_ok(image+0xea5a70,25,"11a6d2c6519e9c4b3763d2e36fd123b4d831564236675cf5165a1b7acf159c08") ||
       !hash_ok(image+0xea5a90,2025,"1d90a6295a8658de5d2ba9cf75b16d37cb74bfcc5f9a517eec08202c3f4dcfd4")) return 0;
    portable_native_inverse_ids=VirtualAlloc(NULL,sizeof(*portable_native_inverse_ids)*WORLD_IDENTITY_CAPACITY,
        MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!portable_native_inverse_ids) return 0;
    memcpy(portable_native_ids,ids,sizeof(ids));memcpy(portable_native_contract,digest_bytes,32);
    memcpy(portable_native_manifest,manifest,203);memcpy(portable_native_contract_text,contract,64);
    portable_native_limit=last;portable_native_stage="configured";portable_native_configured=1;return 1;
}
static void portable_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    if(!portable_native_configured) return;
    AcquireSRWLockExclusive(&portable_native_import_lock);
    if(portable_native_count || !object || !runtime || boot_phase>=3 ||
       !portable_native_inverse_record(saved,runtime)) InterlockedIncrement64(&portable_native_failures);
    for(unsigned i=0;i<PORTABLE_ACTORS;i++) if(portable_native_ids[i].saved==saved) {
        portable_native_identity *id=&portable_native_ids[i];
        if(id->imports || portable_native_count || !object || !runtime || boot_phase>=3)
            InterlockedIncrement64(&portable_native_failures);
        else {id->object=object;id->runtime=runtime;id->imports=1;}
        break;
    }
    ReleaseSRWLockExclusive(&portable_native_import_lock);
}
static int portable_native_string(const void *storage,char out[64]) {
    uint64_t length,capacity;const void *data=storage;
    if(!storage || !read_local((const unsigned char*)storage+16,&length,8) || !length || length>63 ||
       !read_local((const unsigned char*)storage+24,&capacity,8) || capacity<length ||
       (capacity>=16 && (!read_local(storage,&data,8) || !data)) || !read_local(data,out,(size_t)length+1) || out[length]) return 0;
    for(unsigned i=0;i<length;i++) {
        char c=out[i];if(!((c>='a'&&c<='z') || (i && ((c>='0'&&c<='9') || c=='_')))) return 0;
    }
    return 1;
}
static int portable_native_reference(snapshot_find_fn find,void *map,uint64_t runtime,uint64_t *saved) {
    *saved=0;if(!runtime) return 1;
    unsigned char *object=find(map,runtime,4);uint64_t actual;
    return object && read_local(object+8,&actual,8) && actual==runtime && (*saved=world_identity_lookup(runtime))!=0 &&
        portable_native_inverse_lookup(*saved)==runtime;
}
static int portable_native_actor(unsigned index,snapshot_find_fn find,void *map,const world_bank_phase *phase,portable_actor *out) {
    portable_native_identity *id=&portable_native_ids[index];
    unsigned char *object=id->object,*macro,*faction,*parent,*controller,*slots,*parameters,*owner;
    void *type,*getter;uint64_t runtime;
    portable_native_failure_id=id->saved;portable_native_stage="identity";
    if(id->imports!=1 || !object || !id->runtime || find(map,id->runtime,4)!=object ||
       world_identity_lookup(id->runtime)!=id->saved || portable_native_inverse_lookup(id->saved)!=id->runtime ||
       !read_local(object+8,&runtime,8) || runtime!=id->runtime ||
       !read_local(object,&type,8) || type!=image+0x2b7feb8 ||
       !read_local(object+0x60,&macro,8) || !macro || !read_local(macro,&type,8) || type!=image+0x2ba5430 ||
       !read_local((unsigned char*)type+18*8,&getter,8) || getter!=image+0xefb10 ||
       !portable_native_string(macro+0x20,out->macro)) return 0;
    portable_native_stage="true_owner";
    if(!read_local(object,&type,8) || !read_local((unsigned char*)type+0x1620,&getter,8) || getter!=image+0x352b10 ||
       !read_local(object+0x348,&faction,8) || !faction || !portable_native_string(faction+0x10,out->owner)) return 0;
    if(strcmp(out->macro,id->slot<=6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro") ||
       strcmp(out->owner,id->slot<=6?"argon":"xenon")) return 0;
    out->saved=id->saved;out->fixture_slot=(uint16_t)id->slot;
    out->present=out->kind=out->controller=1;out->coverage=PORTABLE_COVERAGE;
    portable_native_stage="parent";
    if(!read_local(object+0x70,&parent,8)) return 0;
    if(parent && (!read_local(parent+8,&runtime,8) || !portable_native_reference(find,map,runtime,&out->parent) || find(map,runtime,4)!=parent)) return 0;
    portable_native_stage="hull";
    if(!read_local(object+0x280,&out->hull,8) || !portable_finite64(out->hull,1)) return 0;
    portable_native_stage="controller_lifetime";
    if(!read_local(object+0x200,&controller,8) || !controller || !read_local(controller,&type,8) || type!=image+0x2c51158 ||
       !read_local((unsigned char*)type+0x30,&getter,8) || getter!=image+0xea5a70 ||
       !read_local(controller+0x28,&owner,8) || owner!=object || !read_local(controller+0x30,&slots,8) || !slots ||
       (uintptr_t)slots<0xad0 || !read_local(controller+0x40,&parameters,8) || parameters!=slots-0xad0 ||
       !read_local(parameters+0xef0,&owner,8) || owner!=object) return 0;
    portable_native_stage="local_transform";
    for(unsigned bank=0;bank<2;bank++) {
        const unsigned char *state=slots+(bank?phase->write:phase->read)*0x1a0;
        portable_bank *target=bank?&out->other:&out->active;uint64_t refs[2];
        for(unsigned vector=0;vector<4;vector++) if(!read_local(state+vector*16,target->transform+vector*3,12)) return 0;
        if(!read_local(state+0xb0,refs,16)) return 0;
        for(unsigned ref=0;ref<2;ref++) if(!portable_native_reference(find,map,refs[ref],&target->refs[ref])) return 0;
    }
    portable_native_stage="identity_recheck";
    return read_local(object+8,&runtime,8) && runtime==id->runtime && find(map,runtime,4)==object;
}
static int portable_native_sample(portable_frame *frame,uint64_t tick,portable_native_hold *hold,void *caller_frame,unsigned char loading) {
    memset(frame,0,sizeof(*frame));
    portable_native_hold before,after;void *map;portable_native_stage="joined_boundary";
    if(GetCurrentThreadId()!=main_thread || boot_phase!=3 || !stats.active || !wire_pair() || world_identity_failures ||
       portable_native_calls!=(LONG64)tick || portable_native_returns!=(LONG64)tick ||
       strcmp(proposal,"00000000") || strcmp(inputs[0],"00000000") || strcmp(inputs[1],"00000000") ||
       !read_local(image+0x6cf1500,&map,8) || !map || !portable_native_hold_read(&before,caller_frame,loading)) return 0;
    memcpy(frame->contract,portable_native_contract,32);frame->tick=frame->calls=frame->returns=frame->receipt_tick=tick;
    frame->game=before.game;
    for(unsigned i=0;i<PORTABLE_ACTORS;i++)
        if(!portable_native_actor(i,(snapshot_find_fn)(image+0xced70),map,&before.banks,&frame->actors[i])) return 0;
    portable_native_stage="local_hold_recheck";
    if(!portable_native_hold_read(&after,caller_frame,loading) || !portable_native_hold_same(&before,&after)) return 0;
    *hold=before;return 1;
}
static size_t portable_native_two_pass(uint64_t tick,unsigned char *bytes,size_t capacity,portable_native_hold *hold,void *caller_frame,unsigned char loading) {
    portable_frame first={0},second={0};portable_native_hold first_hold,second_hold;
    unsigned char verify[PORTABLE_FRAME_CAPACITY];
    if(!portable_native_sample(&first,tick,&first_hold,caller_frame,loading)) return 0;
    portable_native_stage="encode";size_t length=portable_encode_frame(&first,bytes,capacity);
    if(!length || !portable_native_sample(&second,tick,&second_hold,caller_frame,loading)) return 0;
    portable_native_stage="same_boundary_recheck";
    if(!portable_native_hold_same(&first_hold,&second_hold) || portable_encode_frame(&second,verify,sizeof(verify))!=length ||
       memcmp(verify,bytes,length) || !portable_native_hold_current(&first_hold,caller_frame,loading)) return 0;
    *hold=first_hold;portable_native_failure_id=0;portable_native_stage="complete";return length;
}
static int portable_native_capture(uint64_t tick,void *caller_frame,unsigned char loading) {
    if(!portable_native_configured || tick>portable_native_limit) return 1;
    if(portable_native_failures || tick!=(uint64_t)portable_native_count) return 0;
    portable_native_hold held;
    size_t length=portable_native_two_pass(tick,portable_native_bytes[tick],PORTABLE_FRAME_CAPACITY,&held,caller_frame,loading);
    if(!length) goto refused;
    portable_native_holds[tick]=held;
    portable_native_lengths[tick]=(unsigned)length;portable_native_failure_id=0;portable_native_stage="complete";
    InterlockedIncrement(&portable_native_count);return 1;
refused:
    InterlockedIncrement64(&portable_native_failures);return 0;
}
