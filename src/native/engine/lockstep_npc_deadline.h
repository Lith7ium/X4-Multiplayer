/* DEFAULT OFF hypothesis: only an already-finished audio source for an exact
 * registered NPC request is routed through the native deadline check. Native
 * Tick owns the transition to state4 and the later completion callback. */
#define NPC_DEADLINE_WORD 0x563e80u
#define NPC_DEADLINE_LIMIT 512u
typedef struct {
    uint64_t tick,ordinal,enqueue,origin,saved,start,duration,clock;
    uint32_t page,line;unsigned early;
} npc_deadline_sample;
static npc_deadline_sample npc_deadlines[NPC_DEADLINE_LIMIT];
static unsigned npc_deadline_count;
static uint64_t npc_deadline_calls,npc_deadline_foreign;
static volatile LONG64 npc_deadline_seen,npc_deadline_foreign_thread;
static unsigned __int128 npc_deadline_original,npc_deadline_patched;
static int npc_deadline_installed,npc_deadline_restored;
/* native_gate_speech_game_time, DEFAULT OFF (stop reason 120).
 * sc-highway-fly2/3a/3b: EntitySpeakData::Tick completes a line on the state5
 * audio-finished path when the audio source reports finished, i.e. on the wall
 * clock: the ship computer's line 10002/920 ended at different ticks on the two
 * clients (ms 10066/10116/10616) and woke different MD cues. This mode routes
 * every eligible audio-finished completion, of any speaker, through the native
 * deadline check that the NPC experiment below uses for registered NPCs: native
 * Tick then owns state4 and completes the line at start+duration on the game
 * clock, with the actual audio duration the readiness store wrote to
 * SpeakData+0x18. The speaker's portable key is only recorded. Eligibility:
 * state5, timer/interruption flags clear, audio duration equal to the SpeakData
 * duration, finite positive duration, finite start and clock >= start. A line
 * with a flag set keeps the native path and is counted (flagged=); anything
 * unreadable or inconsistent is a failure and stops the gate. Residual case: a
 * client whose audio becomes ready only after the game deadline completes on the
 * initial estimate through native state5; rows record the duration used. */
#define SPEECH_TIME_STOP_REASON 120u
#define SPEECH_TIME_RING 256u
typedef struct {uint64_t tick,key,duration,start,clock;uint32_t page,line;unsigned klass,early;} speech_time_row_t;
static speech_time_row_t speech_time_rows[SPEECH_TIME_RING];
static volatile LONG speech_time_armed;
static volatile LONG64 speech_time_published,speech_time_routed,speech_time_flagged,speech_time_unportable,speech_time_foreign_thread,speech_time_failures;
static int speech_time_route(void *self,void *audio) {
    if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&speech_time_foreign_thread);
    uint64_t entity=0,clock,ready;npc_speak_state state;unsigned char flags[3];
    if(!read_local((unsigned char*)self+8,&entity,8) || !npc_speak_read(self,&state) || !read_local((unsigned char*)self+0x90,flags,3) ||
       !read_local(image+0x6b60e48,&clock,8) || !audio || !read_local((unsigned char*)audio+0x110,&ready,8)) {
        InterlockedIncrement64(&speech_time_failures);return 0;
    }
    if(flags[0] || flags[1] || flags[2]) {InterlockedIncrement64(&speech_time_flagged);return 0;}
    double duration,start,now;memcpy(&duration,&state.duration,8);memcpy(&start,&state.start,8);memcpy(&now,&clock,8);
    if(state.state!=5 || ready!=state.duration || !isfinite(duration) || duration<=0 || !isfinite(start) || !isfinite(now) || now<start ||
       !isfinite(start+duration)) {InterlockedIncrement64(&speech_time_failures);return 0;}
    unsigned klass=WORLD_KEY_NONE;uint64_t key=entity?world_identity_key(entity,&klass):0;
    if(!key) InterlockedIncrement64(&speech_time_unportable);
    speech_time_row_t *r=&speech_time_rows[speech_time_published%SPEECH_TIME_RING];
    r->tick=stats.issued;r->key=key;r->klass=key?klass:WORLD_KEY_NONE;r->page=state.page;r->line=state.line;
    r->duration=state.duration;r->start=state.start;r->clock=clock;r->early=now<start+duration;
    InterlockedIncrement64(&speech_time_published);InterlockedIncrement64(&speech_time_routed);return 1;
}
static int speech_time_row_text(uint64_t index,char *out,size_t capacity) {
    static const char *const kinds[3]={"saved","created","none"};
    LONG64 published=InterlockedCompareExchange64(&speech_time_published,0,0);
    if((LONG64)index>=published) return 0;
    int n;
    if(published>(LONG64)SPEECH_TIME_RING && index<(uint64_t)(published-(LONG64)SPEECH_TIME_RING))
        n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const speech_time_row_t *r=&speech_time_rows[index%SPEECH_TIME_RING];
        n=snprintf(out,capacity,"index=%llu tick=%llu key=%016llx kind=%s page=%u line=%u duration=%016llx start=%016llx clock=%016llx early=%u",
            (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->key,kinds[r->klass<3?r->klass:2],r->page,r->line,
            (unsigned long long)r->duration,(unsigned long long)r->start,(unsigned long long)r->clock,r->early);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int speech_time_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld installed=%d routed=%lld flagged=%lld unportable=%lld foreign_thread=%lld failures=%lld rows=%lld",
        speech_time_armed,npc_deadline_installed,(long long)speech_time_routed,(long long)speech_time_flagged,(long long)speech_time_unportable,
        (long long)speech_time_foreign_thread,(long long)speech_time_failures,(long long)speech_time_published);
    return n>0 && (size_t)n<capacity?n:0;
}
static int npc_deadline_route(void *self,void *audio) {
    if(!stats.active || boot_phase!=3) return 0;
    if(speech_time_armed) return speech_time_route(self,audio);
    InterlockedIncrement64(&npc_deadline_seen);
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&npc_deadline_foreign_thread);return 0;}
    uint64_t entity;
    if(!read_local((unsigned char*)self+8,&entity,8) || !entity) {InterlockedIncrement64(&interrupt_failures);return 0;}
    npc_speaker *speaker=npc_speaker_find(self,entity,0);
    if(!speaker || !speaker->active_enqueue) {++npc_deadline_foreign;return 0;}
    npc_speak_state state;unsigned char flags[3];uint64_t clock,ready,saved=world_identity_lookup(entity);
    if(!saved || !speaker->active_valid || !speaker->ready_count || speaker->ready_serial!=speaker->active_serial ||
       speaker->ready_start_event!=speaker->active_start_event || npc_deadline_count==NPC_DEADLINE_LIMIT || !npc_speak_read(self,&state) ||
       state.state!=5 || state.page!=speaker->active_page || state.line!=speaker->active_line || state.start!=speaker->active_start ||
       !read_local((unsigned char*)self+0x90,flags,3) || flags[0] || flags[1] || flags[2] ||
       !read_local(image+0x6b60e48,&clock,8) || !audio ||
       !read_local((unsigned char*)audio+0x110,&ready,8) || ready!=state.duration || ready!=speaker->ready_duration) {InterlockedIncrement64(&interrupt_failures);return 0;}
    double duration,start,now;memcpy(&duration,&state.duration,8);memcpy(&start,&state.start,8);memcpy(&now,&clock,8);
    if(!isfinite(duration) || duration<=0 || !isfinite(start) || !isfinite(now) || now<start || !isfinite(start+duration)) {
        InterlockedIncrement64(&interrupt_failures);return 0;
    }
    npc_deadline_sample *s=&npc_deadlines[npc_deadline_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_deadline_calls;s->enqueue=speaker->active_enqueue;s->origin=speaker->active_origin;
    s->saved=saved;s->page=state.page;s->line=state.line;s->duration=state.duration;s->start=state.start;s->clock=clock;s->early=now<start+duration;
    return 1;
}
static size_t npc_deadline_stub(unsigned char *out,size_t capacity,void *helper,void *fallthrough,void *immediate,void *deadline) {
    if(capacity<320) return 0;unsigned char *p=out,*not_finished,*use_deadline;
#define NPC_EMIT(...) do {const unsigned char bytes[]={__VA_ARGS__};memcpy(p,bytes,sizeof(bytes));p+=sizeof(bytes);} while(0)
#define NPC_ABS_JUMP(target) do {NPC_EMIT(0xff,0x25,0,0,0,0);memcpy(p,&(target),8);p+=8;} while(0)
    NPC_EMIT(0x41,0x83,0xba,0x28,0x01,0,0,0x0b); /* Original cmp [r10+128],11 exactly once. */
    NPC_EMIT(0x0f,0x85);not_finished=p;memset(p,0,4);p+=4;
    NPC_EMIT(0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53);
    NPC_EMIT(0x48,0x81,0xec,0x80,0,0,0); /* Aligned body RSP; 32 shadow + 96 XMM spills. */
    for(unsigned i=0;i<6;i++) NPC_EMIT(0xf3,0x0f,0x7f,0x44+(i<<3),0x24,0x20+i*16);
    NPC_EMIT(0x48,0x8b,0xcf,0x49,0x8b,0xd2,0x48,0xb8);memcpy(p,&helper,8);p+=8;NPC_EMIT(0xff,0xd0,0x84,0xc0);
    NPC_EMIT(0x0f,0x85);use_deadline=p;memset(p,0,4);p+=4;
    for(unsigned route=0;route<2;route++) {
        if(route) {int32_t rel=(int32_t)(p-(use_deadline+4));memcpy(use_deadline,&rel,4);}
        for(unsigned i=0;i<6;i++) NPC_EMIT(0xf3,0x0f,0x6f,0x44+(i<<3),0x24,0x20+i*16);
        NPC_EMIT(0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d);
        void *target=route?deadline:immediate;NPC_ABS_JUMP(target);
    }
    int32_t rel=(int32_t)(p-(not_finished+4));memcpy(not_finished,&rel,4);NPC_ABS_JUMP(fallthrough);
#undef NPC_ABS_JUMP
#undef NPC_EMIT
    return (size_t)(p-out);
}
static int npc_deadline_swap(unsigned __int128 expected,unsigned __int128 value) {
    DWORD old,ignored;if(!VirtualProtect(image+NPC_DEADLINE_WORD,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)(image+NPC_DEADLINE_WORD),expected,value);
    VirtualProtect(image+NPC_DEADLINE_WORD,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+NPC_DEADLINE_WORD,16);return actual==expected;
}
static int npc_deadline_restore(void) {
    if(!npc_deadline_installed) return 1;
    npc_deadline_restored=npc_deadline_swap(npc_deadline_patched,npc_deadline_original);
    if(npc_deadline_restored) npc_deadline_installed=0;return npc_deadline_restored;
}
/* observers=1: the NPC experiment, which needs its readiness/finish observers.
 * observers=0: native_gate_speech_game_time; the readiness patch, when present,
 * is still reconstructed for the whole-function pin. */
static int npc_deadline_patch(int observers) {
    const unsigned char expected[16]={0x84,0xcb,0,0,0,0x41,0x83,0xba,0x28,1,0,0,0x0b,0x0f,0x84,0xde};
    unsigned char original[1555];memcpy(original,image+0x563d30,sizeof(original));
    if(npc_ready_installed) {
        if(*(unsigned __int128*)(image+NPC_READY_WORD)!=npc_ready_patched) return 0;
        memcpy(original+NPC_READY_WORD-0x563d30,&npc_ready_original,16);
    } else if(observers) return 0;
    if((observers && !npc_finish_installed) || !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) ||
       !hash_ok(original,1555,"8567c0d7897abe3c839a6485e88f7330b1061d635d422a50ce9d8fce0c0ee576") ||
       memcmp(image+NPC_DEADLINE_WORD,expected,16)) return 0;
    intptr_t distance=(intptr_t)(thunk+1344)-(intptr_t)(image+0x563e8a);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(1344)) return 0;
    memcpy(&npc_deadline_original,image+NPC_DEADLINE_WORD,16);npc_deadline_patched=npc_deadline_original;
    unsigned char *patch=(unsigned char*)&npc_deadline_patched;patch[5]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+6,&rel,4);memset(patch+10,0x90,3);
    if(!npc_deadline_swap(npc_deadline_original,npc_deadline_patched)) return 0;
    npc_deadline_installed=1;return 1;
}
static int npc_deadline_install(void) {return npc_deadline_patch(1);}
/* UI thread, before the first gated frame; a loading-UI reload re-arms the owned patch. */
static int speech_time_arm(void) {
    if(speech_time_armed) return stats.active && npc_deadline_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!npc_deadline_installed && !npc_deadline_patch(0)) return 0;
    InterlockedExchange(&speech_time_armed,1);return 1;
}
