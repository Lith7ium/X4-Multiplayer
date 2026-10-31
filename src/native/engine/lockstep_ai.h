/* Fixture-only AI run bracket. Intercept the original AIDirector call site,
 * resolve the ExecState's owning context and walk its component parent chain
 * to a registered fixture actor. No script or order is replaced. */
#define AI_WORD 0x106e3f0u
#define AI_ORIGINAL 0xb58bb0u
typedef void (*ai_run_fn)(void *);
typedef struct {
    uint64_t tick,calls,script_hash,pc_before,pc_after,rng_before,rng_after;
    char script[96];
} ai_sample;
static ai_sample ai_samples[ACTOR_CAPACITY];
static unsigned __int128 ai_original_word,ai_patched_word;
static int ai_installed,ai_restored,ai_seeded;
static int ai_full_layout_verified;
static uint64_t ai_calls,ai_failures;

/* Diagnostic provenance only. A marker is live solely around our exact native
 * interpreter call and can be consumed once, before that interpreter executes.
 * Neither an arbitrary DLL return address nor a reused ExecState is sufficient. */
typedef struct ai_origin_frame {
    struct ai_origin_frame *previous;void *exec;uintptr_t value;uint64_t id;unsigned valid;
} ai_origin_frame;
typedef struct ai_origin_dispatch {
    struct ai_origin_dispatch *previous;ai_origin_frame *owner;unsigned consumed;
} ai_origin_dispatch;
static _Thread_local ai_origin_frame *ai_origin_current;
static _Thread_local ai_origin_dispatch *ai_origin_dispatch_current;
static int ai_origin_identity(void *exec,uintptr_t *value,uint64_t *id) {
    unsigned tag=0;
    return exec && read_local((unsigned char*)exec+0x38,&tag,4) && tag==0x66 &&
        read_local((unsigned char*)exec+0x40,value,8) && *value>=0xa8 &&
        read_local((void*)(*value-0xa8+8),id,8) && *id;
}
static int ai_origin_same(const ai_origin_frame *frame,void *exec) {
    uintptr_t value=0;uint64_t id=0;
    return frame && frame->valid && frame->exec==exec && ai_origin_identity(exec,&value,&id) &&
        value==frame->value && id==frame->id;
}
static int ai_origin_begin(ai_origin_frame *frame,void *exec,uintptr_t caller) {
    if(!(boot_options&281474976710656ULL) || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) return 0;
    memset(frame,0,sizeof(*frame));frame->previous=ai_origin_current;frame->exec=exec;
    unsigned __int128 word=0;
    frame->valid=ai_installed && caller==(uintptr_t)(image+0x106e3fb) &&
        read_local(image+AI_WORD,&word,16) && word==ai_patched_word && ai_origin_identity(exec,&frame->value,&frame->id);
    ai_origin_current=frame;return 1;
}
static void ai_origin_end(ai_origin_frame *frame,int entered) {
    if(!entered) return;
    if(ai_origin_current!=frame) ++ai_failures;
    ai_origin_current=frame->previous;
}
static uint64_t ai_origin_consume(void *exec) {
    ai_origin_dispatch *dispatch=ai_origin_dispatch_current;
    if(!(boot_options&281474976710656ULL) || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread ||
       !dispatch || dispatch->consumed || dispatch->owner!=ai_origin_current ||
       !ai_origin_same(dispatch->owner,exec)) return 0;
    dispatch->consumed=1;return 0x106e3fb;
}
static void ai_call_original(ai_run_fn original,void *exec) {
    if(!(boot_options&281474976710656ULL) || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread ||
       original!=(ai_run_fn)(image+AI_ORIGINAL) || !ai_origin_same(ai_origin_current,exec)) {original(exec);return;}
    ai_origin_dispatch dispatch={ai_origin_dispatch_current,ai_origin_current,0};ai_origin_dispatch_current=&dispatch;
    original(exec);
    if(ai_origin_dispatch_current!=&dispatch || !dispatch.consumed) ++ai_failures;
    ai_origin_dispatch_current=dispatch.previous;
}
/* Pass this original-once adapter through any enclosing read-only observer. */
static void ai_dispatch_original(void *exec) {ai_call_original((ai_run_fn)(image+AI_ORIGINAL),exec);}

static unsigned ai_actor(void *context) {
    unsigned char *entity;
    if(!read_local((unsigned char*)context+0x120,&entity,8)) return 0;
    registry_observe_chain(entity,REGISTRY_AI);
    for(unsigned depth=0;entity && depth<16;depth++) {
        uint64_t id;if(!read_local(entity+8,&id,8)) return 0;
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(entity==actor_components[i] && id==actor_ids[i]) return i+1;
        unsigned char *next;
        if(!read_local(entity+0x70,&next,8) || next==entity) return 0;
        entity=next;
    }
    return 0;
}
/* One script-name decoder for any reader with read_local's contract (the
 * per-script RNG hot path passes a guarded direct read). */
typedef int (*ai_script_reader)(const void *,void *,size_t);
static uint64_t ai_script_with(ai_script_reader read,void *context,char text[96]) {
    unsigned char *script;char str[32];uint64_t size,capacity;const char *data;
    if(!read((unsigned char*)context+0xd8,&script,8) || !script || !read(script+0x10,str,32)) return 0;
    memcpy(&size,str+16,8);memcpy(&capacity,str+24,8);
    if(!size || size>=96 || capacity<size || capacity>4096) return 0;
    if(capacity<16) data=(const char*)script+0x10;else memcpy(&data,str,8);
    if(!read(data,text,(size_t)size)) return 0;text[size]=0;
    uint64_t hash=14695981039346656037ULL;
    for(unsigned i=0;i<size;i++) {
        unsigned char c=(unsigned char)text[i];
        if(c<32 || c>126 || c=='|' || c==' ') return 0;
        hash=(hash^c)*1099511628211ULL;
    }
    return hash;
}
static uint64_t ai_script(void *context,char text[96]) {return ai_script_with(read_local,context,text);}
static void *world_trace_ai_owner(void *context) {
    if(!world_trace_early || !world_trace_enabled) return NULL;
    unsigned char *entity;
    if(!read_local((unsigned char*)context+0x120,&entity,8)) return NULL;
    for(unsigned depth=0;entity && depth<16;depth++) {
        uint64_t id;if(!read_local(entity+8,&id,8)) return NULL;
        for(unsigned i=0;i<world_trace_watch_count;i++)
            if(entity==world_trace_watches[i].object && id==world_trace_watches[i].runtime) return entity;
        unsigned char *next;if(!read_local(entity+0x70,&next,8) || next==entity) return NULL;entity=next;
    }
    return NULL;
}
static void world_trace_ai_execute(ai_run_fn original,world_trace_event *event,void *owner,void *exec,void *context,uintptr_t caller) {
    if(event) {
        if(caller!=(uintptr_t)(image+0x106e3fb) ||
           !ai_script(context,event->script) || !read_local((unsigned char*)exec+0x18,&event->pc_before,8)) event->errors|=32;
        else event->caller=(unsigned)(caller-(uintptr_t)image);
    }
    original(exec);
    if(event) {
        if(!read_local((unsigned char*)exec+0x18,&event->pc_after,8) || world_trace_ai_owner(context)!=owner) event->errors|=64;
        world_trace_end(event);
    }
}
static void world_trace_ai_observe(ai_run_fn original,void *exec,void *context,uintptr_t caller) {
    void *owner=world_trace_ai_owner(context);
    world_trace_event *event=owner?world_trace_begin(WT_AI,owner,NULL,0,0,0,0):NULL;
    world_trace_ai_execute(original,event,owner,exec,context,caller);
}
static void ai_execute(ai_run_fn original,ai_sample *s,unsigned actor,uint64_t *rng,void *exec) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=ai_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x41495f5343524950ULL,stats.issued,actor,s->script_hash,s->calls,s->pc_before);
        s->rng_before=rng?*rng:0;original(exec);s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&ai_failures);
        SetLastError(error);
    }
}
/* Keep the original fresh TLS selection immediately before ai_execute. The
 * noinline boundary keeps MinGW's NtCurrentTeb inline out of a Clang SEH funclet;
 * using the earlier origin sample would change normal-path selection timing. */
static __attribute__((noinline)) uint64_t *ai_current_rng(void) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
}
static __attribute__((noinline)) void ai_run(void *exec) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    ai_origin_frame frame;int entered=ai_origin_begin(&frame,exec,caller);
    registry_origin_scope origin;int origin_entered=0;
    ai_run_fn original=ai_dispatch_original;
    __try {
    unsigned char *es=exec;uint32_t tag;uintptr_t value;
    if(!stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread ||
       !read_local(es+0x38,&tag,4) || tag!=0x66 || !read_local(es+0x40,&value,8) || value<0xa8) {
        original(exec);goto done;
    }
    void *context=(void*)(value-0xa8),*origin_tls=registry_origin_tls();
    registry_origin_enter(&origin,REGISTRY_ORIGIN_AI,caller,exec,context,NULL,0,0,0,origin_tls,registry_origin_rng(origin_tls));
    origin_entered=1;
    unsigned actor=ai_actor(context);
    if(!actor) registry_observe_ai(exec,context,(void*)caller);
    if(!actor) {world_trace_ai_observe(original,exec,context,caller);goto done;}
    ai_sample *s=&ai_samples[actor-1];char script[96]={0};uint64_t hash=ai_script(context,script);
    if(!hash) {++ai_failures;original(exec);goto done;}
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++ai_calls;s->script_hash=hash;strcpy(s->script,script);
    memcpy(&s->pc_before,es+0x18,8);
    uint64_t *rng=ai_current_rng();
    if(!rng) ++ai_failures;
    ai_execute(original,s,actor,rng,exec);
    memcpy(&s->pc_after,es+0x18,8);
done:
    if(origin_entered) registry_origin_leave(&origin,0);
    } __finally {
        if(AbnormalTermination()) {++ai_failures;if(origin_entered)registry_origin_abort(&origin);}
        ai_origin_end(&frame,entered);
    }
}
static int ai_swap(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+AI_WORD,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+AI_WORD),expected,value);
    VirtualProtect(image+AI_WORD,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+AI_WORD,16);
    return actual==expected;
}
static int ai_install(int seed) {
    if(!boot_mode || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !hash_ok(image+0x106e3e0,48,"fad322a196c7bfa17c02d56cf2e0c1ff711a3e81dc61573d16314c19f8cfe20a") ||
       !hash_ok(image+AI_ORIGINAL,303,"7509510d2582f326fb87c58a23b715d1f7499577bf681c67377347314e3aa759")) return 0;
    unsigned char *stub=thunk+128;
    if(!thunk_slot_ready(128)) return 0;
    intptr_t distance=(intptr_t)stub-(intptr_t)(image+0x106e3fb);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&ai_original_word,image+AI_WORD,16);ai_patched_word=ai_original_word;
    int32_t relative=(int32_t)distance;memcpy((unsigned char*)&ai_patched_word+7,&relative,4);
    if(!ai_swap(ai_original_word,ai_patched_word)) return 0;
    ai_seeded=seed;ai_installed=1;ai_full_layout_verified=1;return 1;
}
static void ai_restore(void) {
    if(!ai_installed) return;
    ai_restored=ai_swap(ai_patched_word,ai_original_word);
    if(ai_restored) ai_installed=0;
}
