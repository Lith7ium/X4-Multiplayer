/* Observe native physics/controller velocity feedback without changing it. */
#define VELOCITY_ENTRY 0x2c51198u
#define VELOCITY_MOVE 0xea5790u
#define VELOCITY_MAX 32u
typedef void (*velocity_fn)(void *,const void *);
typedef struct {uint64_t source,time;uint32_t input[8],before[8],after[8];} velocity_record;
typedef struct {uint64_t tick;unsigned count;velocity_record records[VELOCITY_MAX];} velocity_trace;
static velocity_trace velocity_traces[ACTOR_CAPACITY];
static int velocity_installed,velocity_restored;
static uint64_t velocity_calls,velocity_failures;
static int velocity_state(void *controller,uint32_t state[8]) {
    unsigned char *slots;unsigned index=*(unsigned*)(image+0x38f4ee4);
    return index<2 && read_local((unsigned char*)controller+0x30,&slots,8) &&
        read_local(slots+index*0x1a0+0x40,state,32);
}
static void velocity_move(void *self,const void *value) {
    registry_observe_owner(self,0x28,REGISTRY_VELOCITY);
    velocity_fn original=(velocity_fn)(image+VELOCITY_MOVE);unsigned actor=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        unsigned char *object=actor_components[i];uint64_t id;void *controller;
        if(object && read_local(object+8,&id,8) && id==actor_ids[i] && read_local(object+0x200,&controller,8) && controller==self) {actor=i+1;break;}
    }
    if(!actor) {original(self,value);return;}
    velocity_trace *trace=&velocity_traces[actor-1];
    if(trace->tick!=stats.issued) {trace->tick=stats.issued;trace->count=0;}
    if(trace->count>=VELOCITY_MAX) {++velocity_failures;original(self,value);return;}
    velocity_record *r=&trace->records[trace->count++];memset(r,0,sizeof(*r));
    unsigned char *caller=__builtin_return_address(0);
    r->source=caller>=image && caller<image+0x6fa9000?(uint64_t)(caller-image):0;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(tls && tls[0]) memcpy(&r->time,(unsigned char*)tls[0]+0x300,8);else ++velocity_failures;
    if(!read_local(value,r->input,32) || !velocity_state(self,r->before)) ++velocity_failures;
    original(self,value);
    if(!velocity_state(self,r->after)) ++velocity_failures;
    ++velocity_calls;
}
static int velocity_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+VELOCITY_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+VELOCITY_ENTRY),value,expected);
    VirtualProtect(image+VELOCITY_ENTRY,8,old,&ignored);return actual==expected;
}
static int velocity_install(void) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+VELOCITY_MOVE,32,"846e0b6c963b5d1c56fc29abb07f663ed9357b194992419c76cb8f82871b330f")) return 0;
    if(!velocity_swap(image+VELOCITY_MOVE,(void*)velocity_move)) return 0;
    velocity_installed=1;return 1;
}
static void velocity_restore(void) {
    if(!velocity_installed) return;
    velocity_restored=velocity_swap((void*)velocity_move,image+VELOCITY_MOVE);
    if(velocity_restored) velocity_installed=0;
}
