/* Observe actual native speech selection without changing playback or timing.
 * Only speakers initiated inside a named NPC action, and subsequent lines of
 * that same native speaker object/EntityID pair, enter this bounded trace. */
#define NPC_SPEAK_ENTRY 0x561330u
#define NPC_SPEAK_LIMIT 1024u
#define NPC_SPEAKERS 128u
typedef void (*npc_speak_fn)(void *);
typedef struct {
    void *object;uint64_t entity,request_serial,enqueue,origin,active_enqueue,active_origin,active_start;
    uint64_t active_serial,active_start_event,ready_serial,ready_start_event,ready_duration;
    uint32_t active_page,active_line;unsigned active_valid,ready_count;
} npc_speaker;
typedef struct {uint32_t state,handle,page,line;uint64_t duration,start;} npc_speak_state;
typedef struct {
    uint64_t tick,ordinal,action,exec,saved,entity,caller,clock,before,after,enqueue,origin;
    npc_speak_state a,b;
} npc_speak_sample;
static npc_speaker npc_speakers[NPC_SPEAKERS];
static uint64_t npc_request_serial; /* Local lifetime guard only; never a seed/identity. */
static npc_speak_sample npc_speaks[NPC_SPEAK_LIMIT];
static unsigned npc_speak_count;
static uint64_t npc_speak_calls,npc_speak_original_word,npc_speak_patched_word;
static volatile LONG64 npc_speak_seen,npc_speak_foreign_thread,npc_speak_unowned;
static int npc_speak_installed,npc_speak_restored;
static npc_speaker *npc_speaker_find(void *self,uint64_t entity,int create) {
    npc_speaker *empty=NULL;
    for(unsigned i=0;i<NPC_SPEAKERS;i++) {
        if(!npc_speakers[i].object && !empty) empty=&npc_speakers[i];
        if(npc_speakers[i].object==self && npc_speakers[i].entity==entity) return &npc_speakers[i];
    }
    return create?empty:NULL;
}
static int npc_speak_read(void *self,npc_speak_state *s) {
    unsigned char *p=self;
    return read_local(p+0x10,&s->state,4) && read_local(p+0x14,&s->handle,4) &&
        read_local(p+0x18,&s->duration,8) && read_local(p+0x20,&s->start,8) &&
        read_local(p+0x28,&s->page,4) && read_local(p+0x2c,&s->line,4);
}
static void npc_speak_observe(npc_speak_fn original,void *self,uint64_t *rng,uint64_t caller) {
    if(!stats.active || boot_phase!=3) {original(self);return;}
    InterlockedIncrement64(&npc_speak_seen);
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&npc_speak_foreign_thread);original(self);return;}
    uint64_t entity=0;if(!read_local((unsigned char*)self+8,&entity,8) || !entity) {
        InterlockedIncrement64(&interrupt_failures);original(self);return;
    }
    npc_speaker *speaker=npc_speaker_find(self,entity,0);
    if(!speaker) {InterlockedIncrement64(&npc_speak_unowned);original(self);return;}
    if(!speaker->enqueue) {
        speaker->active_enqueue=speaker->active_origin=0;speaker->active_valid=0;
        InterlockedIncrement64(&npc_speak_unowned);original(self);return;
    }
    if(npc_speak_count==NPC_SPEAK_LIMIT || !rng) {InterlockedIncrement64(&interrupt_failures);original(self);return;}
    npc_speak_sample *s=&npc_speaks[npc_speak_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_speak_calls;s->entity=entity;s->saved=world_identity_lookup(entity);s->caller=caller;
    s->action=npc_current?npc_current->state.ordinal:0;s->exec=npc_exec_current?npc_exec_current->ordinal:0;
    s->enqueue=speaker->enqueue;s->origin=speaker->origin;
    uint64_t request_serial=speaker->request_serial;
    if(speaker->active_enqueue!=s->enqueue) speaker->active_valid=0;
    speaker->active_enqueue=s->enqueue;speaker->active_origin=s->origin;
    speaker->active_serial=request_serial;speaker->active_start_event=s->ordinal;speaker->ready_count=0;
    if(!s->saved || !npc_speak_read(self,&s->a) || !read_local(image+0x6b60e48,&s->clock,8)) InterlockedIncrement64(&interrupt_failures);
    s->before=*rng;original(self);s->after=*rng;
    uint64_t after_entity;
    if(!npc_speak_read(self,&s->b) || !read_local((unsigned char*)self+8,&after_entity,8) || after_entity!=entity)
        InterlockedIncrement64(&interrupt_failures);
    else if(speaker->request_serial==request_serial && speaker->active_enqueue==s->enqueue) {
        speaker->active_start=s->b.start;speaker->active_page=s->b.page;speaker->active_line=s->b.line;
        speaker->active_valid=1;
    } else InterlockedIncrement64(&interrupt_failures); /* Reentrant replacement cannot masquerade as this start. */
}
static void npc_speak_run(void *self) {
    uintptr_t ret=(uintptr_t)__builtin_return_address(0),base=(uintptr_t)image;
    npc_speak_observe((npc_speak_fn)(thunk+1152),self,interrupt_rng(),ret>=base && ret<base+0x6fa9000?ret-base:0);
}
static int npc_speak_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_SPEAK_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+NPC_SPEAK_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+NPC_SPEAK_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_SPEAK_ENTRY,8);return actual==expected;
}
static int npc_speak_restore(void) {
    if(!npc_speak_installed) return 1;
    npc_speak_restored=npc_speak_swap(npc_speak_patched_word,npc_speak_original_word);
    if(npc_speak_restored) npc_speak_installed=0;return npc_speak_restored;
}
static int npc_speak_install(void) {
    if(!world_identity_installed || !hash_ok(image+NPC_SPEAK_ENTRY,1755,"b12df7d08158c17fb56d9d31c4605e65ab7c5de1a677b92c103adec1575ba457") ||
       !hash_ok(image+0x563d30,1555,"8567c0d7897abe3c839a6485e88f7330b1061d635d422a50ce9d8fce0c0ee576")) return 0;
    intptr_t distance=(intptr_t)(thunk+1184)-(intptr_t)(image+NPC_SPEAK_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1152)) return 0;
    memcpy(&npc_speak_original_word,image+NPC_SPEAK_ENTRY,8);npc_speak_patched_word=npc_speak_original_word;
    unsigned char *patch=(unsigned char*)&npc_speak_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);patch[5]=patch[6]=0x90;
    if(!npc_speak_swap(npc_speak_original_word,npc_speak_patched_word)) return 0;
    npc_speak_installed=1;return 1;
}
