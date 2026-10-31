/* Bind the resolved native speech request to its later SpeakNextLine calls.
 * No expression is reevaluated. Register before the original: Speak can start
 * synchronously as well as queue work. Preserve every argument and bool result. */
#define NPC_ENQUEUE_ENTRY 0x561a10u
#define NPC_ENQUEUE_LIMIT 512u
#define NPC_ENQUEUE_LINES 64u
typedef unsigned char (*npc_enqueue_fn)(void*,void*,int,unsigned char,void*,void*,unsigned,void*);
typedef struct {
    uint64_t tick,ordinal,action,exec,saved,recipients[2],before,after;
    int priority;unsigned interrupt,flags,count,result;uint32_t lines[NPC_ENQUEUE_LINES][2];
} npc_enqueue_sample;
static npc_enqueue_sample npc_enqueues[NPC_ENQUEUE_LIMIT];
static unsigned npc_enqueue_count;
static uint64_t npc_enqueue_calls,npc_enqueue_original_word,npc_enqueue_patched_word;
static int npc_enqueue_installed,npc_enqueue_restored;
static int npc_enqueue_recipient(void *entity,uint64_t *saved) {
    uint64_t id;*saved=0;if(!entity) return 1;
    if(!read_local((unsigned char*)entity+8,&id,8) || !id) return 0;
    *saved=world_identity_lookup(id);return *saved!=0;
}
static unsigned char npc_enqueue_observe(npc_enqueue_fn original,void *self,void *vector,int priority,
        unsigned char interrupt,void *recipient,void *source,unsigned flags,void *text,uint64_t *rng) {
    if(!stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread)
        return original(self,vector,priority,interrupt,recipient,source,flags,text);
    uint64_t entity;uintptr_t span[3];
    int owned=npc_speech_owner!=NULL;
    if(!owned) {
        /* A known speaker can later be used by another MD module/native caller.
         * Invalidate pending NPC ownership BEFORE the call, which may start a
         * line synchronously. Rejected requests preserve the previous owner. */
        npc_speaker *speaker=NULL;
        if(read_local((unsigned char*)self+8,&entity,8) && entity) speaker=npc_speaker_find(self,entity,0);
        if(!speaker) return original(self,vector,priority,interrupt,recipient,source,flags,text);
        npc_speaker previous=*speaker;uint64_t serial=++npc_request_serial;
        speaker->request_serial=serial;speaker->enqueue=speaker->origin=0;
        unsigned char result=original(self,vector,priority,interrupt,recipient,source,flags,text);
        if(!result && speaker->request_serial==serial) *speaker=previous;
        return result;
    }
    if(npc_enqueue_count==NPC_ENQUEUE_LIMIT || !rng ||
       !read_local((unsigned char*)self+8,&entity,8) || !entity ||
       !read_local(vector,span,sizeof(span)) || span[1]<span[0] || span[2]<span[1] ||
       (span[1]-span[0])%0x30 || (span[1]-span[0])/0x30>NPC_ENQUEUE_LINES) {
        InterlockedIncrement64(&interrupt_failures);
        return original(self,vector,priority,interrupt,recipient,source,flags,text);
    }
    npc_speaker *speaker=npc_speaker_find(self,entity,1);
    if(!speaker) {InterlockedIncrement64(&interrupt_failures);return original(self,vector,priority,interrupt,recipient,source,flags,text);}
    npc_enqueue_sample *s=&npc_enqueues[npc_enqueue_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_enqueue_calls;s->action=npc_speech_owner->state.ordinal;
    s->exec=npc_exec_current?npc_exec_current->ordinal:0;s->saved=world_identity_lookup(entity);
    s->priority=priority;s->interrupt=interrupt;s->flags=flags;s->count=(unsigned)((span[1]-span[0])/0x30);
    if(!s->saved || !npc_enqueue_recipient(recipient,&s->recipients[0]) || !npc_enqueue_recipient(source,&s->recipients[1]))
        InterlockedIncrement64(&interrupt_failures);
    for(unsigned i=0;i<s->count;i++) if(!read_local((void*)(span[0]+i*0x30),s->lines[i],8)) InterlockedIncrement64(&interrupt_failures);
    npc_speaker previous=*speaker;uint64_t serial=++npc_request_serial;
    speaker->object=self;speaker->entity=entity;speaker->request_serial=serial;speaker->enqueue=s->ordinal;speaker->origin=s->action;
    s->before=*rng;s->result=original(self,vector,priority,interrupt,recipient,source,flags,text);s->after=*rng;
    if(!s->result && speaker->request_serial==serial) *speaker=previous;
    return (unsigned char)s->result;
}
static unsigned char npc_enqueue_run(void *self,void *vector,int priority,unsigned char interrupt,
        void *recipient,void *source,unsigned flags,void *text) {
    return npc_enqueue_observe((npc_enqueue_fn)(thunk+1216),self,vector,priority,interrupt,recipient,source,flags,text,interrupt_rng());
}
static int npc_enqueue_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_ENQUEUE_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+NPC_ENQUEUE_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+NPC_ENQUEUE_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_ENQUEUE_ENTRY,8);return actual==expected;
}
static int npc_enqueue_restore(void) {
    if(!npc_enqueue_installed) return 1;
    npc_enqueue_restored=npc_enqueue_swap(npc_enqueue_patched_word,npc_enqueue_original_word);
    if(npc_enqueue_restored) npc_enqueue_installed=0;return npc_enqueue_restored;
}
static int npc_enqueue_install(void) {
    if(!npc_speak_installed || !hash_ok(image+NPC_ENQUEUE_ENTRY,2929,"d792806fec28f4635fd8411e3b085784c2ce27a47edd4f8065b971b4d7f0c568") ||
       !hash_ok(image+0xbf1e80,686,"9b4b323f2f897ab13159e7c1bf9ecfc4575fb0d1e5e2b843cb960a7c15131a8e")) return 0;
    intptr_t distance=(intptr_t)(thunk+1248)-(intptr_t)(image+NPC_ENQUEUE_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1216)) return 0;
    memcpy(&npc_enqueue_original_word,image+NPC_ENQUEUE_ENTRY,8);npc_enqueue_patched_word=npc_enqueue_original_word;
    unsigned char *patch=(unsigned char*)&npc_enqueue_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!npc_enqueue_swap(npc_enqueue_original_word,npc_enqueue_patched_word)) return 0;
    npc_enqueue_installed=1;return 1;
}
