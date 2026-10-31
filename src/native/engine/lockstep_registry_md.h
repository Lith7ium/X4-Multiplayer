/* Read-only evaluated MD event boundary. No expression is re-evaluated and no
 * engine helper, registry preparation, seed, or object mutation occurs here. */
#define REGISTRY_MD_RVA 0xb3b090u
#define REGISTRY_MD_BIT (1ULL<<51)
#define REGISTRY_MD_CAPACITY 128u
#define REGISTRY_MD_NAME_BYTES 64u
#define REGISTRY_MD_SHA "0282b43031fdfcbe102ce450101f9e64d4da19cb44379eda4ade9f15a3204fb7"
#define REGISTRY_MD_ACTION_SHA "fa9788a77ad21d7b1e6b6ee7a333eff741723c8efe17b7dfeafab01ce1c9cb78"
typedef struct {const char *data;uint64_t length;} registry_md_string;
typedef unsigned char (*registry_md_fn)(void *,const registry_md_string *,uint64_t);
typedef struct {
    uint64_t order,pair,tick,handle,caller,payload,time,sink;
    unsigned edge,kind,phase,reads,error,ui,main,view,ready,result,pending,thread;
    char name[136];
} registry_md_event;
enum registry_md_error {REGISTRY_MD_NAME=1,REGISTRY_MD_UNKNOWN=2,REGISTRY_MD_CALLER=4,
    REGISTRY_MD_PHASE=8,REGISTRY_MD_PENDING=16,REGISTRY_MD_READ=32,
    REGISTRY_MD_OVERFLOW=128};
static registry_md_event registry_md_events[REGISTRY_MD_CAPACITY];
static unsigned registry_md_count;
static volatile LONG64 registry_md_order,registry_md_failures;
static volatile LONG64 registry_md_open;
static LONG64 registry_md_restore_open,registry_md_peak;
static SRWLOCK registry_md_lock=SRWLOCK_INIT;
static int registry_md_installed,registry_md_restored,registry_md_owned,registry_md_restoring;
static uint64_t registry_md_original_word,registry_md_patched_word;
static void creation_md_note(unsigned,unsigned,unsigned,uint64_t,uint64_t,uint64_t,uint64_t,unsigned,unsigned,unsigned,unsigned);
static uint64_t registry_md_next_order(void) {
    return registry_md_installed?(uint64_t)InterlockedIncrement64(&registry_md_order):0;
}
/* Borrowed name is copied only while the native call owns it. Unrelated names
 * need no full copy. Unknown names in our namespace remain explicit evidence. */
static int registry_md_name(const registry_md_string *source,registry_md_event *event) {
    static const char prefix[]="X4LAN_NativeConstruction.";
    registry_md_string value;char beginning[sizeof(prefix)-1];
    if(!source || !read_local(source,&value,sizeof(value))) {event->error|=REGISTRY_MD_NAME;return 1;}
    if(value.length<sizeof(beginning)) return 0;
    if(!value.data || !read_local(value.data,beginning,sizeof(beginning))) {event->error|=REGISTRY_MD_NAME;return 1;}
    if(memcmp(beginning,prefix,sizeof(beginning))) return 0;
    if(value.length>=REGISTRY_MD_NAME_BYTES || !read_local(value.data,event->name,(size_t)value.length)) {
        event->error|=REGISTRY_MD_NAME;strcpy(event->name,"unsupported");return 1;
    }
    event->name[value.length]=0;event->reads|=1;
    if(value.length==strlen("X4LAN_NativeConstruction.Boundary") && !memcmp(event->name,"X4LAN_NativeConstruction.Boundary",value.length)) event->kind=1;
    else if(value.length==strlen("X4LAN_NativeConstruction.Builder") && !memcmp(event->name,"X4LAN_NativeConstruction.Builder",value.length)) event->kind=2;
    else if(value.length==strlen("X4LAN_NativeConstruction.Station") && !memcmp(event->name,"X4LAN_NativeConstruction.Station",value.length)) event->kind=3;
    else if(value.length==strlen("X4LAN_NativeConstruction.Storage") && !memcmp(event->name,"X4LAN_NativeConstruction.Storage",value.length)) event->kind=4;
    else {
        event->error|=REGISTRY_MD_UNKNOWN;int plain=1;
        for(unsigned i=0;i<value.length;i++) {unsigned char c=(unsigned char)event->name[i];
            if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='.')) plain=0;
        }
        if(!plain) {
            unsigned char raw[REGISTRY_MD_NAME_BYTES];memcpy(raw,event->name,(size_t)value.length);strcpy(event->name,"hex_");
            const char *hex="0123456789abcdef";for(unsigned i=0;i<value.length;i++) {event->name[4+2*i]=hex[raw[i]>>4];event->name[5+2*i]=hex[raw[i]&15];}
            event->name[4+2*value.length]=0;
        }
    }
    return 1;
}
static void registry_md_snapshot_context(registry_md_event *event,void *holder,void *slot) {
    unsigned char ready=0,view=0;void *sink=NULL,*type=NULL,*target=NULL;
    event->thread=GetCurrentThreadId();event->ui=event->thread==ui_thread;event->main=event->thread==main_thread;
    /* This byte is sampled outside the native lock. It is NOT the branch taken. */
    if(read_local(image+0x38f0a52,&ready,1)) {event->reads|=2;event->ready=ready;}
    if(holder && read_local(holder,&sink,8) && sink && read_local(sink,&type,8) && type &&
       read_local((unsigned char*)type+0x1f0,&target,8)) {event->reads|=4;event->sink=registry_local_rva(target);}
    if(slot) {
        if(read_local((unsigned char*)slot+0x314,&view,1)) {event->reads|=8;event->view=view;}
        if(read_local((unsigned char*)slot+0x300,&event->time,8)) event->reads|=16;
    }
    if(event->reads!=31 || !event->sink || event->view>1) event->error|=REGISTRY_MD_READ;
}
static void registry_md_snapshot(registry_md_event *event,void *holder) {
    void **tls=NULL,*slot=NULL;
    if(!read_local((unsigned char*)NtCurrentTeb()+0x58,&tls,8) || !tls || !read_local(tls,&slot,8)) slot=NULL;
    registry_md_snapshot_context(event,holder,slot);
}
static void registry_md_append(registry_md_event *event) {
    AcquireSRWLockExclusive(&registry_md_lock);
    event->order=(uint64_t)InterlockedIncrement64(&registry_md_order);
    if(!event->edge) event->pair=event->order;
    if(registry_md_count==REGISTRY_MD_CAPACITY) {event->error|=REGISTRY_MD_OVERFLOW;InterlockedIncrement64(&registry_md_failures);}
    else {registry_md_events[registry_md_count++]=*event;if(event->error) InterlockedIncrement64(&registry_md_failures);}
    ReleaseSRWLockExclusive(&registry_md_lock);
    DWORD saved_error=GetLastError();
    creation_md_note(event->kind,event->phase,event->edge,event->order,event->tick,event->handle,event->payload,
        event->pending,event->error,event->thread,event->reads);
    SetLastError(saved_error);
}
static unsigned char registry_md_observe(registry_md_fn original,void *holder,const registry_md_string *name,uint64_t payload,void *caller) {
    if(!registry_md_installed || !stats.active || boot_phase!=3 || !registry_is_observing()) return original(holder,name,payload);
    registry_md_event event={0};event.tick=stats.issued;event.payload=payload;event.caller=registry_local_rva(caller);
    AcquireSRWLockShared(&registry_lock);
    for(unsigned i=0;i<dynamic_ship_count;i++) if(!dynamic_ships[i].bound_tick) {event.handle=dynamic_ships[i].handle;++event.pending;}
    ReleaseSRWLockShared(&registry_lock);
    if(!event.pending || !registry_md_name(name,&event)) return original(holder,name,payload);
    /* Serialize capture admission with restoration, without waiting for an
     * original engine call to finish. Thunk storage remains alive after restore. */
    AcquireSRWLockExclusive(&registry_md_lock);
    int capture=registry_md_installed && stats.active && !registry_md_restoring;
    if(capture) {
        LONG64 open=InterlockedIncrement64(&registry_md_open);
        if(open>registry_md_peak) registry_md_peak=open;
        if(open>REGISTRY_MD_CAPACITY) InterlockedIncrement64(&registry_md_failures);
    }
    ReleaseSRWLockExclusive(&registry_md_lock);
    if(!capture) return original(holder,name,payload);
    if(event.pending!=1) event.error|=REGISTRY_MD_PENDING;
    if((event.kind==1 && event.caller!=0xc2095a) || (event.kind>=2 && event.kind<=4 && event.caller!=0xc208cd) ||
       (event.caller!=0xc208cd && event.caller!=0xc2095a)) event.error|=REGISTRY_MD_CALLER;
    if(event.kind==1) {if(payload>=1 && payload<=8) event.phase=(unsigned)payload;else event.error|=REGISTRY_MD_PHASE;}
    else if(event.kind>=2 && event.kind<=4 && !payload) event.error|=REGISTRY_MD_PHASE;
    registry_md_snapshot(&event,holder);registry_md_append(&event);
    unsigned char result=original(holder,name,payload);
    event.edge=1;event.result=result;event.reads&=1;event.error&=~REGISTRY_MD_READ;
    registry_md_snapshot(&event,holder);registry_md_append(&event);
    InterlockedDecrement64(&registry_md_open);return result;
}
static unsigned char registry_md_record(void *holder,const registry_md_string *name,uint64_t payload) {
    return registry_md_observe((registry_md_fn)(thunk+2432),holder,name,payload,__builtin_return_address(0));
}
/* Retained as the independent actual-image ABI emitter used by the test harness. */
static __attribute__((unused)) int registry_md_emit(unsigned char *trampoline,unsigned char *dispatch,unsigned char *entry,void *target) {
    static const unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08};
    if(memcmp(entry,prefix,sizeof(prefix))) return 0;
    memcpy(trampoline,entry,5);trampoline[5]=0xff;trampoline[6]=0x25;memset(trampoline+7,0,4);
    void *continuation=entry+5;memcpy(trampoline+11,&continuation,8);
    dispatch[0]=0xff;dispatch[1]=0x25;memset(dispatch+2,0,4);memcpy(dispatch+6,&target,8);return 1;
}
static int registry_md_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+REGISTRY_MD_RVA,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+REGISTRY_MD_RVA),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+REGISTRY_MD_RVA,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+REGISTRY_MD_RVA,8);return actual==expected;
}
static int registry_md_install(void) {
    if(!boot_mode || !world_identity_installed || !ai_installed || !path_installed || !(path_options&1) ||
       GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_physics.dll") ||
       !hash_ok(image+REGISTRY_MD_RVA,162,REGISTRY_MD_SHA) || !hash_ok(image+0xc20810,884,REGISTRY_MD_ACTION_SHA)) return 0;
    intptr_t distance=(intptr_t)(thunk+2464)-(intptr_t)(image+REGISTRY_MD_RVA+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(2432)) return 0;
    memcpy(&registry_md_original_word,image+REGISTRY_MD_RVA,8);registry_md_patched_word=registry_md_original_word;
    unsigned char *patch=(unsigned char*)&registry_md_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!registry_md_swap(registry_md_original_word,registry_md_patched_word)) return 0;
    registry_md_installed=registry_md_owned=1;return 1;
}
static void registry_md_restore(void) {
    if(!registry_md_installed) return;
    AcquireSRWLockExclusive(&registry_md_lock);registry_md_restoring=1;
    LONG64 open=InterlockedCompareExchange64(&registry_md_open,0,0);
    if(open>registry_md_restore_open) registry_md_restore_open=open;
    ReleaseSRWLockExclusive(&registry_md_lock);
    registry_md_restored=registry_md_swap(registry_md_patched_word,registry_md_original_word);
    if(registry_md_restored) registry_md_installed=0;
}
static int registry_md_format(unsigned index,char *out,size_t capacity) {
    registry_md_event e;AcquireSRWLockShared(&registry_md_lock);
    int exists=index<registry_md_count;if(exists) e=registry_md_events[index];ReleaseSRWLockShared(&registry_md_lock);
    if(!exists) return 0;
    int n=snprintf(out,capacity,"schema=2 order=%llu pair=%llu tick=%llu handle=%016llx edge=%u kind=%u phase=%u caller=%llx payload=%016llx"
        " ui=%u main=%u thread=%u view=%u time=%016llx ready=%u sink=%llx reads=%x error=%x result=%u pending=%u name=%s",
        e.order,e.pair,e.tick,e.handle,e.edge,e.kind,e.phase,e.caller,e.payload,e.ui,e.main,e.thread,e.view,e.time,e.ready,e.sink,e.reads,e.error,e.result,e.pending,e.name[0]?e.name:"unsupported");
    return n>0 && (size_t)n<capacity?n:0;
}
