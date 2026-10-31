/* Read-only fixture AI radar-event delivery trace. The native handler still
 * receives every call once, with its arguments and event scheduling unchanged. */
#define RADAR_ENTRY 0x2c6a970u
#define RADAR_HANDLER 0xcb3da0u
#define RADAR_EVENT_LIMIT 4096u
typedef void (*radar_fn)(void *,void *,unsigned);
typedef struct {uint64_t tick,ordinal;unsigned actor,source,flags;char script[96];} radar_event;
static radar_event radar_events[RADAR_EVENT_LIMIT];
static unsigned radar_event_count;
static uint64_t radar_calls;
static volatile LONG64 radar_failures;
static uint64_t radar_dropped,radar_unreadable;
static int radar_installed,radar_restored;
static void radar_execute(radar_fn original,void *context,void *source,unsigned flags,uintptr_t caller) {
    void *tls=registry_origin_tls();registry_origin_scope origin;
    registry_origin_enter(&origin,REGISTRY_ORIGIN_RADAR,caller,NULL,context,source,0,0,flags,tls,registry_origin_rng(tls));
    unsigned actor=stats.active && boot_phase==3?ai_actor(context):0;
    if(actor) {
        if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&radar_failures);
        else if(gate_play_mode) ++radar_calls;
        else {
            ++radar_calls;
            char script[96]={0};uint64_t source_id=0;unsigned source_actor=0;
            /* Rows only: a full table or an unreadable name loses a row, not the session. */
            if(radar_event_count==RADAR_EVENT_LIMIT) ++radar_dropped;
            else if(!ai_script(context,script) || (source && !read_local((unsigned char*)source+8,&source_id,8))) ++radar_unreadable;
            else {
                for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(source==actor_components[i] && source_id==actor_ids[i] && source) source_actor=i+1;
                radar_event *e=&radar_events[radar_event_count++];
                e->tick=stats.issued;e->ordinal=radar_calls;e->actor=actor;e->source=source_actor;e->flags=flags;
                strcpy(e->script,script);
            }
        }
    }
    __try {original(context,source,flags);registry_origin_leave(&origin,0);}
    __finally {if(AbnormalTermination()) {InterlockedIncrement64(&radar_failures);registry_origin_abort(&origin);}}
}
static __attribute__((noinline)) void radar_handler(void *context,void *source,unsigned flags) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    registry_observe_chain(source,REGISTRY_RADAR);
    radar_execute((radar_fn)(image+RADAR_HANDLER),context,source,flags,caller);
}
static int radar_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+RADAR_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)(image+RADAR_ENTRY),value,expected);
    VirtualProtect(image+RADAR_ENTRY,8,old,&ignored);return actual==expected;
}
static int radar_install(void) {
    if(!boot_mode || !ai_installed ||
       !hash_ok(image+RADAR_HANDLER,115,"7c63edcea72f348448ceedbd0f1793f89770d5ffa5f02f057110d5d6e159e6f1") ||
       !radar_swap(image+RADAR_HANDLER,(void*)radar_handler)) return 0;
    radar_installed=1;return 1;
}
static void radar_restore(void) {
    if(!radar_installed) return;
    radar_restored=radar_swap((void*)radar_handler,image+RADAR_HANDLER);
    if(radar_restored) radar_installed=0;
}
