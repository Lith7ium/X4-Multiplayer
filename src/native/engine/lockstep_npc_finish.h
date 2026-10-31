/* Native completion provenance, independently observed under --npc-diag.
 * The active start owner differs from a pending replacement request: Speak may
 * finish the old line while enqueuing the new one. Never relabel that callback. */
#define NPC_FINISH_ENTRY 0x562590u
#define NPC_FINISH_LIMIT 1024u
typedef void (*npc_finish_fn)(void*,unsigned char);
typedef struct {
    uint64_t tick,ordinal,enqueue,origin,saved,caller,clock,before,after;
    unsigned interrupted,audio,flags[3];npc_speak_state state;
} npc_finish_sample;
static npc_finish_sample npc_finishes[NPC_FINISH_LIMIT];
static unsigned npc_finish_count;
static uint64_t npc_finish_calls,npc_finish_original_word,npc_finish_patched_word;
static volatile LONG64 npc_finish_seen,npc_finish_unowned,npc_finish_foreign_thread;
static int npc_finish_installed,npc_finish_restored;
static void npc_finish_observe(npc_finish_fn original,void *self,unsigned char interrupted,uint64_t *rng,uint64_t caller) {
    if(!stats.active || boot_phase!=3) {original(self,interrupted);return;}
    InterlockedIncrement64(&npc_finish_seen);
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&npc_finish_foreign_thread);original(self,interrupted);return;}
    uint64_t entity;uintptr_t audio;unsigned char enabled,flags[3];
    if(!read_local((unsigned char*)self+8,&entity,8) || !entity) {
        InterlockedIncrement64(&interrupt_failures);original(self,interrupted);return;
    }
    npc_speaker *speaker=npc_speaker_find(self,entity,0);
    if(!speaker || !speaker->active_enqueue) {InterlockedIncrement64(&npc_finish_unowned);original(self,interrupted);return;}
    if(npc_finish_count==NPC_FINISH_LIMIT || !rng) {InterlockedIncrement64(&interrupt_failures);original(self,interrupted);return;}
    npc_finish_sample *s=&npc_finishes[npc_finish_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_finish_calls;s->saved=world_identity_lookup(entity);
    s->enqueue=speaker->active_enqueue;s->origin=speaker->active_origin;s->interrupted=interrupted;s->caller=caller;
    if(!s->saved || !s->enqueue || !npc_speak_read(self,&s->state) ||
       !read_local(image+0x6b60e48,&s->clock,8) || !read_local(image+0x6cf15b8,&audio,8) || !audio ||
       !read_local((void*)(audio+0x78),&enabled,1) || !read_local((unsigned char*)self+0x90,flags,3))
        InterlockedIncrement64(&interrupt_failures);
    else {s->audio=enabled;for(unsigned i=0;i<3;i++) s->flags[i]=flags[i];}
    /* Retire the old active line before native completion can start another.
     * Pending ownership remains for subsequent lines of the same request. */
    speaker->active_enqueue=0;speaker->active_origin=0;speaker->active_valid=0;
    s->before=*rng;original(self,interrupted);s->after=*rng;
}
static void npc_finish_run(void *self,unsigned char interrupted) {
    uintptr_t ret=(uintptr_t)__builtin_return_address(0),base=(uintptr_t)image;
    npc_finish_observe((npc_finish_fn)(thunk+1280),self,interrupted,interrupt_rng(),ret>=base && ret<base+0x6fa9000?ret-base:0);
}
static int npc_finish_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_FINISH_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+NPC_FINISH_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+NPC_FINISH_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_FINISH_ENTRY,8);return actual==expected;
}
static int npc_finish_restore(void) {
    if(!npc_finish_installed) return 1;
    npc_finish_restored=npc_finish_swap(npc_finish_patched_word,npc_finish_original_word);
    if(npc_finish_restored) npc_finish_installed=0;return npc_finish_restored;
}
static int npc_finish_install(void) {
    if(!npc_enqueue_installed || !hash_ok(image+NPC_FINISH_ENTRY,1331,"906a0fe860fd06dfaa55bcc9db2988c2eddd83d28a98c764f490bd94e8429446")) return 0;
    intptr_t distance=(intptr_t)(thunk+1312)-(intptr_t)(image+NPC_FINISH_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1280)) return 0;
    memcpy(&npc_finish_original_word,image+NPC_FINISH_ENTRY,8);npc_finish_patched_word=npc_finish_original_word;
    unsigned char *patch=(unsigned char*)&npc_finish_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!npc_finish_swap(npc_finish_original_word,npc_finish_patched_word)) return 0;
    npc_finish_installed=1;return 1;
}
