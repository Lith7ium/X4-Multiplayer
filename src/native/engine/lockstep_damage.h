/* Read-only native shield absorption trace for registered fixture groups. */
#define DAMAGE_ENTRY 0x791760u
#define DAMAGE_EVENTS 2048u
typedef double (*damage_fn)(void *,void *,double,int,double);
typedef struct {uint64_t tick,ordinal,time,amount,absorbed,before[2],after[2];unsigned actor;int priority;uint32_t stack[8];} damage_event;
static void player_damage_observed(const damage_event *event);
static damage_event damage_events[DAMAGE_EVENTS];
static unsigned damage_event_count;
static uint64_t damage_calls,damage_original_word,damage_patched_word;
static volatile LONG64 damage_failures;
static int damage_installed,damage_restored;
static double damage_observe(damage_fn original,damage_event *e,void *group,void *victim,double amount,int priority,double time) {
    e->priority=priority;memcpy(&e->amount,&amount,8);memcpy(&e->time,&time,8);
    if(!read_local((unsigned char*)group+0x68,e->before,16)) InterlockedIncrement64(&damage_failures);
    double result=original(group,victim,amount,priority,time);memcpy(&e->absorbed,&result,8);
    if(!read_local((unsigned char*)group+0x68,e->after,16)) InterlockedIncrement64(&damage_failures);
    return result;
}
static double damage_absorb(void *group,void *victim,double amount,int priority,double time) {
    registry_observe_chain(victim,REGISTRY_DAMAGE);
    damage_fn original=(damage_fn)(thunk+736);unsigned actor=0;
    if(stats.active && boot_phase==3) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        unsigned char *object=actor_components[i];uint64_t id;void *actual;
        if(object && read_local(object+8,&id,8) && id==actor_ids[i] && read_local(object+0x290,&actual,8) && actual==group) {actor=i+1;break;}
    }
    if(!actor) return original(group,victim,amount,priority,time);
    if(GetCurrentThreadId()!=ui_thread || damage_event_count==DAMAGE_EVENTS) {
        InterlockedIncrement64(&damage_failures);return original(group,victim,amount,priority,time);
    }
    damage_event *e=&damage_events[damage_event_count++];memset(e,0,sizeof(*e));
    e->actor=actor;e->tick=stats.issued;e->ordinal=++damage_calls;
    void *frames[20];unsigned n=CaptureStackBackTrace(0,20,frames,NULL),j=0;
    for(unsigned i=0;i<n && j<8;i++) {
        unsigned char *frame=frames[i];if(frame>=image && frame<image+0x6fa9000) e->stack[j++]=(uint32_t)(frame-image);
    }
    double result=damage_observe(original,e,group,victim,amount,priority,time);
    player_damage_observed(e);return result;
}
static int damage_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+DAMAGE_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+DAMAGE_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+DAMAGE_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+DAMAGE_ENTRY,8);return actual==expected;
}
static int damage_install(void) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_combat.dll") ||
       !hash_ok(image+DAMAGE_ENTRY,1133,"859c640f260d11c5b7ec23e885426eb8288888a716cf6d1e1346a29ddfb03292")) return 0;
    intptr_t distance=(intptr_t)(thunk+768)-(intptr_t)(image+DAMAGE_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(736)) return 0;
    if(!thunk_unwind_prepare(736)) return 0;
    memcpy(&damage_original_word,image+DAMAGE_ENTRY,8);damage_patched_word=damage_original_word;
    unsigned char *patch=(unsigned char*)&damage_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!damage_swap(damage_original_word,damage_patched_word)) return 0;
    damage_installed=1;return 1;
}
static void damage_restore(void) {
    if(!damage_installed) return;
    damage_restored=damage_swap(damage_patched_word,damage_original_word);if(damage_restored) damage_installed=0;
}
