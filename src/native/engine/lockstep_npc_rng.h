/* Hypothesis intervention, DEFAULT OFF. Name/program/PC/tick ordered invocation
 * keying isolates selection RNG before the original native MD execution. It is
 * NOT a stable semantic identity for duplicate cue instances and cannot repair
 * different scheduling. Full named actions remain strictly compared. */
#define NPC_EXEC_ENTRY 0xb58bb0u
#define NPC_EXEC_LIMIT 4096u
#define NPC_EXEC_KEYS 512u
typedef void (*npc_exec_fn)(void *);
typedef struct {
    uint64_t tick,ordinal,parent,key,call,program,pc,before,scoped,after,restored;
    unsigned depth;char cue[256];
} npc_exec_sample;
typedef struct {uint64_t key,program,pc,tick,calls;char cue[256];} npc_exec_key;
static npc_exec_sample npc_exec_samples[NPC_EXEC_LIMIT],*npc_exec_current;
static npc_exec_key npc_exec_keys[NPC_EXEC_KEYS];
static unsigned npc_exec_count;
static uint64_t npc_exec_calls,npc_exec_original_word,npc_exec_patched_word;
static int npc_exec_installed,npc_exec_restored;

static npc_exec_key *npc_exec_key_for(const char *cue,uint64_t program,uint64_t pc) {
    uint64_t key=14695981039346656037ULL;
    for(size_t i=0;cue[i];i++) key=(key^(unsigned char)cue[i])*1099511628211ULL;
    key=lockstep_mix(key^program);key=lockstep_mix(key^pc);if(!key) return NULL;
    unsigned first=(unsigned)(key%NPC_EXEC_KEYS);
    for(unsigned n=0;n<NPC_EXEC_KEYS;n++) {
        npc_exec_key *entry=&npc_exec_keys[(first+n)%NPC_EXEC_KEYS];
        if(!entry->key) {entry->key=key;entry->program=program;entry->pc=pc;strcpy(entry->cue,cue);return entry;}
        if(entry->key==key) return entry->program==program && entry->pc==pc && !strcmp(entry->cue,cue)?entry:NULL;
    }
    return NULL;
}
static void npc_exec_observe(npc_exec_fn original,void *exec,uint64_t *rng) {
    if(!stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) {original(exec);return;}
    char cue[256];int named=npc_cue_name(exec,cue);
    if(named==2) {original(exec);return;}
    uint64_t program,pc;
    if(!named || !rng || npc_exec_count==NPC_EXEC_LIMIT || !nested_program_hash(exec,&program) ||
       !read_local((unsigned char*)exec+0x18,&pc,8) || pc>=1048576) {
        InterlockedIncrement64(&interrupt_failures);original(exec);return;
    }
    npc_exec_key *key=npc_exec_key_for(cue,program,pc);
    if(!key) {InterlockedIncrement64(&interrupt_failures);original(exec);return;}
    if(key->tick!=stats.issued) {key->tick=stats.issued;key->calls=0;}
    npc_exec_sample *s=&npc_exec_samples[npc_exec_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_exec_calls;s->key=key->key;s->call=++key->calls;s->program=program;s->pc=pc;strcpy(s->cue,cue);
    s->parent=npc_exec_current?npc_exec_current->ordinal:0;s->depth=npc_exec_current?npc_exec_current->depth+1:1;
    if(s->depth>64) InterlockedIncrement64(&interrupt_failures);
    s->before=*rng;s->scoped=lockstep_seed(0x4e50435f45584543ULL,s->tick,s->key,s->program,s->call,s->pc);
    npc_exec_sample *previous=npc_exec_current;npc_exec_current=s;*rng=s->scoped;
    original(exec);
    s->after=*rng;*rng=s->before;s->restored=*rng;npc_exec_current=previous;
}
static void npc_exec_selected(void *exec) {
    if(boot_options&140737488355328ULL) npc_exec_observe((npc_exec_fn)(thunk+1088),exec,interrupt_rng());
    else ((npc_exec_fn)(thunk+1088))(exec);
}
/* Keep the native entry's routing testable without its engine TLS/trampoline.
 * In particular, legacy bit47 alone must never enter the trigger observer.
 * The optional per-script RNG scope (lockstep_script_rng.h) encloses it all. */
static void npc_exec_entry(npc_exec_fn selected,void *exec,uint64_t *rng,uint64_t caller) {
    script_rng_frame scope;int scoped=script_rng_enter(&scope,SR_EXEC,exec,NULL,0,0,0,rng);
    __try {
        if(boot_options&281474976710656ULL) {
            npc_pilot_scope pilot;int pilot_entered=npc_pilot_begin(&pilot,exec,NULL,1,rng,0,0,0);
            npc_provenance_frame frame;int entered=npc_provenance_enter(&frame,exec,caller);
            npc_trigger_observe(selected,exec,rng,caller);npc_provenance_leave(&frame,entered);
            npc_pilot_end(&pilot,pilot_entered,rng);
        }
        else selected(exec);
    } __finally {script_rng_leave(&scope,scoped,rng,AbnormalTermination());}
}
static void npc_exec_run(void *exec) {
    uintptr_t ret=(uintptr_t)__builtin_return_address(0),base=(uintptr_t)image;
    npc_exec_entry(npc_exec_selected,exec,interrupt_rng(),ret>=base && ret<base+0x6fa9000?ret-base:0);
}
static int npc_exec_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_EXEC_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+NPC_EXEC_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+NPC_EXEC_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_EXEC_ENTRY,8);return actual==expected;
}
static int npc_exec_restore(void) {
    if(!npc_exec_installed) return 1;
    npc_exec_restored=npc_exec_swap(npc_exec_patched_word,npc_exec_original_word);
    if(npc_exec_restored) {npc_exec_installed=0;npc_provenance_enabled=0;}return npc_exec_restored;
}
static int npc_exec_install(void) {
    if((boot_options&281474976710656ULL) && !npc_provenance_pinned()) return 0;
    /* Validate the complete original function, including the dispatch site now
     * occupied by our independently pinned observer. No foreign patch accepted. */
    unsigned char original[303];
    if(!nested_action_installed || *(unsigned __int128*)(image+NESTED_ACTION_WORD)!=nested_action_patched ||
       !read_local(image+NPC_EXEC_ENTRY,original,sizeof(original))) return 0;
    memcpy(original+(NESTED_ACTION_WORD-NPC_EXEC_ENTRY),&nested_action_original,16);
    if(!hash_ok(original,sizeof(original),"7509510d2582f326fb87c58a23b715d1f7499577bf681c67377347314e3aa759")) return 0;
    intptr_t distance=(intptr_t)(thunk+1120)-(intptr_t)(image+NPC_EXEC_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1088)) return 0;
    memcpy(&npc_exec_original_word,image+NPC_EXEC_ENTRY,8);npc_exec_patched_word=npc_exec_original_word;
    unsigned char *patch=(unsigned char*)&npc_exec_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!npc_exec_swap(npc_exec_original_word,npc_exec_patched_word)) return 0;
    npc_exec_installed=1;npc_provenance_enabled=(boot_options&281474976710656ULL)!=0;return 1;
}
