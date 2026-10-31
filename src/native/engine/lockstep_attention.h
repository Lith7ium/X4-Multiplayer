/* Keep native distance-based attention selection, but align this fixture's
 * randomized refresh schedule. Attention changes rebuild physical child shapes. */
#define ATTENTION_ENTRY 0x6d1ff0u
typedef void (*attention_fn)(void *);
/* ATTENTION_OWN_PLATFORM (linux/gate_attention.h) ports 'all' only: the fixture actors' path, the hook and its
 * install are each platform's own; the next due's offset is ATTENTION_DUE (Windows +0x2e0). */
#ifndef ATTENTION_DUE
#define ATTENTION_DUE 0x2e0
#endif
#ifndef ATTENTION_OWN_PLATFORM
typedef struct {uint64_t tick,calls,time,before,after,due_before,due_after;unsigned level_before,level_after,initialized;} attention_sample;
static attention_sample attention_samples[ACTOR_CAPACITY];
static unsigned attention_initialized[ACTOR_CAPACITY],attention_inits;
#endif
static uint64_t attention_calls,attention_failures,attention_original_word,attention_patched_word;
static int attention_installed,attention_restored,attention_seeded,attention_initial;
#ifndef ATTENTION_OWN_PLATFORM
static void attention_execute(attention_fn original,attention_sample *s,unsigned actor,uint64_t *rng,void *self,double time) {
    unsigned char *object=self;uint64_t saved=rng?*rng:0;s->initialized=0;
    memcpy(&s->due_before,object+0x2e0,8);memcpy(&s->level_before,object+0x1ec,4);
    if(attention_initial && !attention_initialized[actor-1]) {
        *(double*)(object+0x2e0)=time-gate_tick_dt();attention_initialized[actor-1]=1;s->initialized=1;++attention_inits;
    }
    const int restore_rng=attention_seeded && rng;
    __try {
        if(restore_rng) *rng=actor_seed(0x415454454e54494fULL,stats.issued,actor,0,s->calls,0);
        s->before=rng?*rng:0;original(self);s->after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&attention_failures);
        SetLastError(error);
    }
    memcpy(&s->due_after,object+0x2e0,8);memcpy(&s->level_after,object+0x1ec,4);
}
#endif
/* Every portable object, DEFAULT OFF (native_gate_attention_all; needs the
 * attention hook, i.e. native_gate_attention_rng or _initial_refresh). The
 * object attention timer 0x6d1ff0 (from U::Object::Move 0x6ccf90 and the
 * container Move tail 0x7f5e14): once the TLS game time reaches object+0x2e0
 * it sets +0x2e0 = now + 0.75 s + Random::FloatState(TLS+0x318, 0.5 s) and
 * calls SetAttention(object,1). Loading leaves +0x2e0 at a loading-time value
 * from that client's loading frames, so the first admitted re-evaluation of a
 * non-fixture object lands on a client-dependent tick, and since every later
 * due starts from that tick the schedule never realigns (long-L9, identical
 * cameras: one level-9 evaluation alternating c2/c1 at ticks 48/63, 100/135,
 * 154/197, ... every 45-75 ticks). With this, at the first admitted call per
 * portable key (saved ID, or created ID with portable created keys) +0x2e0 =
 * now - dt (the object re-evaluates at once), and every admitted call runs
 * with the TLS word seeded from (tick, key) and restored afterwards, so the
 * next due no longer depends on the frame's earlier draws either. Fixture
 * actors keep their own path; unmapped objects keep the native one (counted).
 * MoveWorker and admitted frames only. Insert-only key table; a full table is
 * a failure (stop reason 54). */
#define ATTENTION_ALL_DOMAIN 0x415454414c4c4b59ULL /* "ATTALLKY" */
#define ATTENTION_ALL_TABLE 262144u
static uint64_t attention_all_table[ATTENTION_ALL_TABLE];
static unsigned attention_all_count;
/* Distinct objects without a portable key (by runtime ID, this client only; the
 * count compares across clients), capped at the table size. */
#define ATTENTION_ALL_UNMAPPED 4096u
static uint64_t attention_all_unmapped_table[ATTENTION_ALL_UNMAPPED];
static unsigned attention_all_unmapped_ids;
static void attention_all_note_unmapped(uint64_t runtime) {
    if(!runtime) return;
    for(unsigned n=0,h=(unsigned)(lockstep_mix(runtime)&(ATTENTION_ALL_UNMAPPED-1));n<ATTENTION_ALL_UNMAPPED/2;n++,h=(h+1)&(ATTENTION_ALL_UNMAPPED-1)) {
        if(attention_all_unmapped_table[h]==runtime) return;
        if(!attention_all_unmapped_table[h]) {attention_all_unmapped_table[h]=runtime;++attention_all_unmapped_ids;return;}
    }
}
static int attention_all_armed;
static const char *attention_all_stage="idle";
static uint64_t attention_all_calls,attention_all_inits,attention_all_unmapped,attention_all_first,attention_all_tick=UINT64_MAX,
    attention_all_frame_calls,attention_all_frame_inits;
static int attention_all_first_call(uint64_t key) {
    for(unsigned n=0,h=(unsigned)(lockstep_mix(key)&(ATTENTION_ALL_TABLE-1));n<ATTENTION_ALL_TABLE;n++,h=(h+1)&(ATTENTION_ALL_TABLE-1)) {
        if(attention_all_table[h]==key) return 0;
        if(!attention_all_table[h]) {
            if(attention_all_count>=ATTENTION_ALL_TABLE/4*3) return -1;
            attention_all_table[h]=key;++attention_all_count;return 1;
        }
    }
    return -1;
}
static void attention_all_apply(attention_fn original,void *self,uint64_t *rng,double time) {
    unsigned char *object=self;uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(attention_all_tick!=stats.issued) {attention_all_tick=stats.issued;attention_all_frame_calls=attention_all_frame_inits=0;}
    if(read_local(object+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key) {++attention_all_unmapped;attention_all_note_unmapped(runtime);original(self);return;}
    if(!rng || !isfinite(time)) {attention_all_stage="tls";++attention_failures;original(self);return;}
    int first=attention_all_first_call(key);
    if(first<0) {attention_all_stage="table";++attention_failures;original(self);return;}
    ++attention_all_calls;++attention_all_frame_calls;
    if(first) {
        double now=time-gate_tick_dt();memcpy(object+ATTENTION_DUE,&now,8);++attention_all_inits;++attention_all_frame_inits;
        if(!attention_all_first) attention_all_first=stats.issued;
    }
    uint64_t saved=*rng;
    __try {*rng=lockstep_seed(ATTENTION_ALL_DOMAIN,stats.issued,key,klass,0,0);original(self);}
    __finally {
        DWORD error=GetLastError();*rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&attention_failures);
        SetLastError(error);
    }
}
/* Before the first admitted frame, with the attention hook installed. */
static int attention_all_arm(void) {
    if(attention_all_armed) return attention_installed;
    if(!attention_installed || !stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    memset(attention_all_table,0,sizeof(attention_all_table));attention_all_count=0;
    memset(attention_all_unmapped_table,0,sizeof(attention_all_unmapped_table));attention_all_unmapped_ids=0;
    attention_all_calls=attention_all_inits=attention_all_unmapped=attention_all_first=attention_all_frame_calls=attention_all_frame_inits=0;
    attention_all_tick=UINT64_MAX;attention_all_stage="armed";attention_all_armed=1;return 1;
}
static int attention_all_status(char *out,size_t capacity) {
    int fresh=attention_all_tick==stats.issued;
    int n=snprintf(out,capacity,"armed=%d installed=%d frame=%llu calls=%llu inits=%llu total_calls=%llu total_inits=%llu ids=%u "
        "unmapped=%llu unmapped_ids=%u first=%llu stage=%s failures=%llu",attention_all_armed,attention_installed,(unsigned long long)stats.issued,
        (unsigned long long)(fresh?attention_all_frame_calls:0),(unsigned long long)(fresh?attention_all_frame_inits:0),
        (unsigned long long)attention_all_calls,(unsigned long long)attention_all_inits,attention_all_count,
        (unsigned long long)attention_all_unmapped,attention_all_unmapped_ids,(unsigned long long)attention_all_first,attention_all_stage,(unsigned long long)attention_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
#ifndef ATTENTION_OWN_PLATFORM
static void attention_refresh(void *self) {
    registry_observe_object(self,REGISTRY_ATTENTION);
    attention_fn original=(attention_fn)(thunk+576);unsigned actor=0;
    int admitted=stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread;
    if(admitted) {
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
            uint64_t id;if(self==actor_components[i] && read_local((unsigned char*)self+8,&id,8) && id==actor_ids[i]) {actor=i+1;break;}
        }
    }
    if(!actor && admitted && attention_all_armed) {
        void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time=NAN;
        uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
        if(rng && !read_local((unsigned char*)tls[0]+0x300,&time,8)) time=NAN;
        attention_all_apply(original,self,rng,time);return;
    }
    if(!actor) {original(self);return;}
    attention_sample *s=&attention_samples[actor-1];
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}++s->calls;++attention_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng || !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time)) {++attention_failures;original(self);return;}
    memcpy(&s->time,&time,8);attention_execute(original,s,actor,rng,self,time);
}
static int attention_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+ATTENTION_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+ATTENTION_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+ATTENTION_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+ATTENTION_ENTRY,8);return actual==expected;
}
static int attention_install(int seed,int initial) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+ATTENTION_ENTRY,188,"b89efdd1ce0a8d0901c88c63c0d7ac54f14cc90138459ccf7bf8e7835b19401f")) return 0;
    intptr_t distance=(intptr_t)(thunk+608)-(intptr_t)(image+ATTENTION_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(576)) return 0;
    if(!thunk_unwind_prepare(576)) return 0;
    memcpy(&attention_original_word,image+ATTENTION_ENTRY,8);attention_patched_word=attention_original_word;
    unsigned char *patch=(unsigned char*)&attention_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);patch[5]=0x90;
    if(!attention_swap(attention_original_word,attention_patched_word)) return 0;
    attention_seeded=seed;attention_initial=initial;attention_installed=1;return 1;
}
static void attention_restore(void) {
    if(!attention_installed) return;
    attention_restored=attention_swap(attention_patched_word,attention_original_word);if(attention_restored) attention_installed=0;
}
#endif
