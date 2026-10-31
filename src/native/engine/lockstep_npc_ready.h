/* Read-only observation of the native audio-ready duration store. */
#define NPC_READY_WORD 0x563fb0u
#define NPC_READY_LIMIT 1024u
typedef struct {
    uint64_t tick,ordinal,enqueue,origin,saved,start_event,generation,duration,estimate,start,clock;
    uint32_t page,line;unsigned repeat;
} npc_ready_sample;
static npc_ready_sample npc_ready_samples[NPC_READY_LIMIT];
static unsigned npc_ready_count;
static uint64_t npc_ready_calls,npc_ready_foreign;
static volatile LONG64 npc_ready_seen,npc_ready_foreign_thread;
static unsigned __int128 npc_ready_original,npc_ready_patched;
static int npc_ready_installed,npc_ready_restored;
static void npc_ready_observe(void *self,double actual) {
    if(!stats.active || boot_phase!=3) return;
    InterlockedIncrement64(&npc_ready_seen);
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&npc_ready_foreign_thread);return;}
    uint64_t entity;
    if(!read_local((unsigned char*)self+8,&entity,8) || !entity) {InterlockedIncrement64(&interrupt_failures);return;}
    npc_speaker *speaker=npc_speaker_find(self,entity,0);
    if(!speaker || !speaker->active_enqueue) {++npc_ready_foreign;return;}
    npc_speak_state state;uint64_t clock,bits,saved=world_identity_lookup(entity);memcpy(&bits,&actual,8);
    if(!saved || !speaker->active_valid || !speaker->active_serial || !speaker->active_start_event ||
       !isfinite(actual) || actual<=0 || npc_ready_count==NPC_READY_LIMIT || !npc_speak_read(self,&state) ||
       state.state!=3 || state.page!=speaker->active_page || state.line!=speaker->active_line || state.start!=speaker->active_start ||
       !read_local(image+0x6b60e48,&clock,8) || (speaker->ready_count &&
       (speaker->ready_serial!=speaker->active_serial || speaker->ready_start_event!=speaker->active_start_event || speaker->ready_duration!=bits))) {
        InterlockedIncrement64(&interrupt_failures);return;
    }
    npc_ready_sample *s=&npc_ready_samples[npc_ready_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_ready_calls;s->enqueue=speaker->active_enqueue;s->origin=speaker->active_origin;
    s->saved=saved;s->start_event=speaker->active_start_event;s->generation=speaker->active_serial;
    s->page=state.page;s->line=state.line;s->duration=bits;s->estimate=state.duration;s->start=state.start;s->clock=clock;
    s->repeat=++speaker->ready_count;speaker->ready_serial=speaker->active_serial;
    speaker->ready_start_event=speaker->active_start_event;speaker->ready_duration=bits;
}
static size_t npc_ready_stub(unsigned char *out,size_t capacity,void *helper,void *continuation) {
    if(capacity<320) return 0;unsigned char *p=out;
#define R_EMIT(...) do {const unsigned char bytes[]={__VA_ARGS__};memcpy(p,bytes,sizeof(bytes));p+=sizeof(bytes);} while(0)
    R_EMIT(0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53);
    R_EMIT(0x48,0x81,0xec,0x80,0,0,0);
    for(unsigned i=0;i<6;i++) R_EMIT(0xf3,0x0f,0x7f,0x44+(i<<3),0x24,0x20+i*16);
    /* RCX=self; XMM1 is already the Windows ABI second double argument. */
    R_EMIT(0x48,0x8b,0xcf,0x48,0xb8);memcpy(p,&helper,8);p+=8;R_EMIT(0xff,0xd0);
    for(unsigned i=0;i<6;i++) R_EMIT(0xf3,0x0f,0x6f,0x44+(i<<3),0x24,0x20+i*16);
    R_EMIT(0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d);
    R_EMIT(0xf2,0x0f,0x11,0x4f,0x18); /* Original store exactly once, with restored XMM1. */
    R_EMIT(0xff,0x25,0,0,0,0);memcpy(p,&continuation,8);p+=8;
#undef R_EMIT
    return (size_t)(p-out);
}
static int npc_ready_swap(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_READY_WORD,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+NPC_READY_WORD),expected,value);
    VirtualProtect(image+NPC_READY_WORD,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_READY_WORD,16);return actual==expected;
}
static int npc_ready_restore(void) {
    if(!npc_ready_installed) return 1;
    npc_ready_restored=npc_ready_swap(npc_ready_patched,npc_ready_original);
    if(npc_ready_restored) npc_ready_installed=0;return npc_ready_restored;
}
static int npc_ready_install(void) {
    const unsigned char expected[16]={0xc8,0x0f,0x86,0x5d,3,0,0,0xf2,0x0f,0x11,0x4f,0x18,0x83,0x7f,0x10,0};
    if(!npc_finish_installed || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !hash_ok(image+0x563d30,1555,"8567c0d7897abe3c839a6485e88f7330b1061d635d422a50ce9d8fce0c0ee576") ||
       memcmp(image+NPC_READY_WORD,expected,16)) return 0;
    intptr_t distance=(intptr_t)(thunk+1664)-(intptr_t)(image+0x563fbc);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1664)) return 0;
    memcpy(&npc_ready_original,image+NPC_READY_WORD,16);npc_ready_patched=npc_ready_original;
    unsigned char *patch=(unsigned char*)&npc_ready_patched;patch[7]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+8,&rel,4);
    if(!npc_ready_swap(npc_ready_original,npc_ready_patched)) return 0;npc_ready_installed=1;return 1;
}
