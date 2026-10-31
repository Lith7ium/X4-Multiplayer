/* Read-only instruction trace within the owned fixture interrupt bracket.
 * The original instruction evaluates its own chance, conditions and action. */
#define INTERRUPT_STEP_ENTRY 0xb58b00u
#define INTERRUPT_STEP_LIMIT 8192u
typedef void (*interrupt_step_fn)(void *);
typedef struct {
    uint64_t tick,ordinal,parent,pc,next,type,action,rng_before,rng_after;
    uint64_t trigger_visit,trigger_children;
    unsigned actor,chance,flags_before,flags_after;
} interrupt_step_sample;
static interrupt_sample *interrupt_current;
static interrupt_step_sample *interrupt_step_active;
static interrupt_step_sample interrupt_steps[INTERRUPT_STEP_LIMIT];
static unsigned interrupt_step_count;
static uint64_t interrupt_step_calls,interrupt_step_original_word,interrupt_step_patched_word;
static int interrupt_step_installed,interrupt_step_restored;
static int interrupt_step_read(void *exec,interrupt_step_sample *s) {
    unsigned char *e=exec;uintptr_t program,bounds[2],node,type,action;uint16_t flags;
    if(!read_local(e+0x10,&program,8) || !read_local(e+0x18,&s->pc,8) ||
       !read_local((void*)program,bounds,16) || !bounds[0] || bounds[1]<bounds[0] ||
       (bounds[0]&7) || ((bounds[1]-bounds[0])&7) || (bounds[1]-bounds[0])/8>1048576 ||
       s->pc>=(bounds[1]-bounds[0])/8 || !read_local((void*)(bounds[0]+s->pc*8),&node,8) ||
       !read_local((void*)node,&type,8) || type<(uintptr_t)image || type>(uintptr_t)(image+0x6fa9000-32) ||
       !read_local((void*)(type+0x18),&action,8) || action<(uintptr_t)image || action>=(uintptr_t)(image+0x6fa9000) ||
       !read_local((void*)(node+0x10),&s->chance,4) || !read_local(e+0x30,&flags,2)) return 0;
    s->type=type-(uintptr_t)image;s->action=action-(uintptr_t)image;s->flags_before=flags;return 1;
}
static void interrupt_step_observe(interrupt_step_fn original,void *exec,uint64_t *rng) {
    if(GetCurrentThreadId()!=ui_thread || !interrupt_current) {original(exec);return;}
    if(interrupt_step_count==INTERRUPT_STEP_LIMIT) {++interrupt_step_dropped;original(exec);return;}
    if(!rng) {interrupt_fault(IFAULT_STEP_RNG,interrupt_current->actor,0,0,interrupt_current->script);original(exec);return;}
    interrupt_step_sample *s=&interrupt_steps[interrupt_step_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++interrupt_step_calls;s->parent=interrupt_current->ordinal;s->actor=interrupt_current->actor;
    s->trigger_visit=++interrupt_current->trigger_steps;
    if(!interrupt_step_read(exec,s)) interrupt_fault(IFAULT_STEP_READ,s->actor,s->pc,0,interrupt_current->script);
    s->rng_before=*rng;interrupt_step_sample *previous=interrupt_step_active;interrupt_step_active=s;
    __try {
        original(exec);interrupt_step_active=previous;s->rng_after=*rng;
        uint16_t flags;
        if(!read_local((unsigned char*)exec+0x18,&s->next,8) || !read_local((unsigned char*)exec+0x30,&flags,2))
            interrupt_fault(IFAULT_STEP_AFTER,s->actor,s->pc,0,interrupt_current?interrupt_current->script:NULL);
        else s->flags_after=flags;
    } __finally {
        interrupt_step_active=previous;
        if(AbnormalTermination()) interrupt_fault(IFAULT_STEP_UNWIND,s->actor,s->pc,0,interrupt_current?interrupt_current->script:NULL);
    }
}
static void interrupt_step_run(void *exec) {
    interrupt_step_observe((interrupt_step_fn)(thunk+896),exec,interrupt_rng());
}
static int interrupt_step_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+INTERRUPT_STEP_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+INTERRUPT_STEP_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+INTERRUPT_STEP_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+INTERRUPT_STEP_ENTRY,8);return actual==expected;
}
static int interrupt_step_restore(void) {
    if(!interrupt_step_installed) return 1;
    interrupt_step_restored=interrupt_step_swap(interrupt_step_patched_word,interrupt_step_original_word);
    if(interrupt_step_restored) interrupt_step_installed=0;return interrupt_step_restored;
}
static int interrupt_step_install(void) {
    if(!hash_ok(image+INTERRUPT_STEP_ENTRY,164,"9e6d5b305b24883bac346b7663422c1e3401a889f54230b3ae37bc4f5da83106")) return 0;
    intptr_t distance=(intptr_t)(thunk+928)-(intptr_t)(image+INTERRUPT_STEP_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(896)) return 0;
    memcpy(&interrupt_step_original_word,image+INTERRUPT_STEP_ENTRY,8);interrupt_step_patched_word=interrupt_step_original_word;
    unsigned char *patch=(unsigned char*)&interrupt_step_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!interrupt_step_swap(interrupt_step_original_word,interrupt_step_patched_word)) return 0;
    interrupt_step_installed=1;return 1;
}
