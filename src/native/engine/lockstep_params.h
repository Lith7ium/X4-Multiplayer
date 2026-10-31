/* Observe all native parameter-builder callers before changing their scheduling.
 * The 16-byte entry CAS preserves byte 15, the first byte of the next instruction.
 * A trampoline executes exactly the first 15 complete prologue bytes. */
#define PARAMS_ENTRY 0xea1e80u
#define PARAMS_TRACE_MAX 32u
typedef struct {uint64_t source,time,rng_before,rng_after;int destination;unsigned base,view;uint32_t values[24];unsigned char flags[4];} params_record;
typedef struct {uint64_t tick;unsigned count;params_record records[PARAMS_TRACE_MAX];} params_trace;
static params_trace params_traces[ACTOR_CAPACITY];
static unsigned __int128 params_original_word,params_patched_word;
static int params_installed,params_restored;
static uint64_t params_trace_calls,params_trace_failures;
static void params_observe(void *controller,void *destination,unsigned char base) {
    registry_observe_owner(controller,0x28,REGISTRY_PARAMS);
    params_fn original=(params_fn)(thunk+320);unsigned actor=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        unsigned char *object=actor_components[i];void *actual;uint64_t id;
        if(object && read_local(object+8,&id,8) && id==actor_ids[i] && read_local(object+0x200,&actual,8) && actual==controller) {actor=i+1;break;}
    }
    if(!actor) {original(controller,destination,base);return;}
    params_trace *trace=&params_traces[actor-1];
    if(trace->tick!=stats.issued) {trace->tick=stats.issued;trace->count=0;}
    if(trace->count>=PARAMS_TRACE_MAX) {++params_trace_failures;original(controller,destination,base);return;}
    params_record *record=&trace->records[trace->count++];memset(record,0,sizeof(*record));
    unsigned char *caller=__builtin_return_address(0),*buffers=NULL;
    record->source=caller>=image && caller<image+0x6fa9000?(uint64_t)(caller-image):0;
    record->base=base;record->destination=-1;
    if(read_local((unsigned char*)controller+0x40,&buffers,8)) {
        if(destination==buffers) record->destination=0;
        else if(destination==buffers+0x1a0) record->destination=1;
        else if(destination==buffers+0x340) record->destination=2;
    }
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(rng) {
        record->rng_before=*rng;memcpy(&record->time,(unsigned char*)tls[0]+0x300,8);
        memcpy(&record->view,(unsigned char*)tls[0]+0x314,4);
    } else ++params_trace_failures;
    original(controller,destination,base);
    record->rng_after=rng?*rng:0;
    if(!read_local(destination,record->values,sizeof(record->values)) ||
       !read_local((unsigned char*)destination+0x190,record->flags,sizeof(record->flags))) ++params_trace_failures;
    ++params_trace_calls;
}
static int params_swap(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+PARAMS_ENTRY,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+PARAMS_ENTRY),expected,value);
    VirtualProtect(image+PARAMS_ENTRY,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+PARAMS_ENTRY,16);
    return actual==expected;
}
static int params_install(void) {
    if(!boot_mode || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !hash_ok(image+PARAMS_ENTRY,0xb9f,"2db0c3a56629a30189d8112ee5e9dec726ddabec4805380cdb7e8a6ec4c2deff")) return 0;
    if(!thunk_slot_ready(320)) return 0;
    if(!thunk_unwind_prepare(320)) return 0;
    memcpy(&params_original_word,image+PARAMS_ENTRY,16);params_patched_word=params_original_word;
    unsigned char *patch=(unsigned char*)&params_patched_word;patch[0]=0xff;patch[1]=0x25;memset(patch+2,0,4);
    void *target=(void*)params_observe;memcpy(patch+6,&target,8);patch[14]=0x90;
    if(!params_swap(params_original_word,params_patched_word)) return 0;
    params_installed=1;return 1;
}
static void params_restore(void) {
    if(!params_installed) return;
    params_restored=params_swap(params_patched_word,params_original_word);
    if(params_restored) params_installed=0;
}
