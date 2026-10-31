/* Read-only body property changes that can remove/reappend active Jolt bodies.
 * Included by lockstep_contact.h after its checked body reader. */
#define CONTACT_PROPERTIES_ENTRY 0x51bd40u
#define CONTACT_PROPERTIES_EVENTS 256u
typedef void (*contact_properties_fn)(void *);
typedef struct {unsigned present;contact_body body;uint32_t group,filter;unsigned char quality;} contact_properties_body;
typedef struct {
    uint64_t tick,ordinal;unsigned actor;uint32_t stack[8];
    contact_properties_body before[2],after[2];
} contact_properties_event;
static contact_properties_event contact_properties_events[CONTACT_PROPERTIES_EVENTS];
static unsigned contact_properties_count;
static uint64_t contact_properties_calls,contact_properties_original_word,contact_properties_patched_word;
static int contact_properties_installed,contact_properties_restored;

static int contact_properties_read(void *component,unsigned which,contact_properties_body *out) {
    unsigned char *record,*xbody,*scene,*system,*iface,*manager,*bodies,*body,*motion;
    void *owner,*userdata;uint64_t count;uint32_t id,actual;
    memset(out,0,sizeof(*out));
    if(which>1 || !read_local((unsigned char*)component+0x260+which*8,&record,8)) return 0;
    if(!record) return 1;
    if(!read_local(record,&owner,8) || owner!=component ||
       !read_local(record+0x10,&xbody,8) || !xbody || !read_local(xbody+8,&id,4) || id==UINT32_MAX ||
       !read_local(record+0x18,&scene,8) || !scene || !read_local(scene+0x20,&system,8) || !system ||
       !read_local(system+0x1b8,&iface,8) || !iface || !read_local(iface+8,&manager,8) || !manager ||
       !read_local(manager,&count,8) || count>0x800000u || (id&0x7fffffu)>=count ||
       !read_local(manager+0x10,&bodies,8) || !bodies ||
       !read_local(bodies+(id&0x7fffffu)*8,&body,8) || !body || ((uintptr_t)body&1) ||
       !read_local(body+0x70,&actual,4) || actual!=id ||
       !read_local(body+0x50,&userdata,8) || userdata!=record || !contact_body_read(body,&out->body) ||
       !read_local(record+0x28,&out->group,4) || !read_local(record+0x2c,&out->filter,4) ||
       !read_local(body+0x48,&motion,8) || (motion && !read_local(motion+0x78,&out->quality,1))) return 0;
    if(out->quality>1) return 0;
    out->present=1;return 1;
}
static void contact_properties_observe(contact_properties_fn original,contact_properties_event *e,void *component) {
    for(unsigned i=0;i<2;i++) if(!contact_properties_read(component,i,&e->before[i])) InterlockedIncrement64(&contact_failures);
    original(component);
    for(unsigned i=0;i<2;i++) if(!contact_properties_read(component,i,&e->after[i])) InterlockedIncrement64(&contact_failures);
}
static void contact_properties_update(void *component) {
    contact_properties_fn original=(contact_properties_fn)(thunk+2048);unsigned actor=0;uint64_t id;
    if(stats.active && boot_phase==3 && component && read_local((unsigned char*)component+8,&id,8))
        for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(component==actor_components[i] && id==actor_ids[i]) {actor=i+1;break;}
    if(!actor) {original(component);return;}
    if(GetCurrentThreadId()!=ui_thread || contact_properties_count==CONTACT_PROPERTIES_EVENTS) {
        InterlockedIncrement64(&contact_failures);original(component);return;
    }
    contact_properties_event *e=&contact_properties_events[contact_properties_count++];memset(e,0,sizeof(*e));
    e->actor=actor;e->tick=stats.issued;e->ordinal=++contact_properties_calls;
    void *frames[20];unsigned n=CaptureStackBackTrace(0,20,frames,NULL),j=0;
    for(unsigned i=0;i<n && j<8;i++) {
        unsigned char *frame=frames[i];if(frame>=image && frame<image+0x6fa9000) e->stack[j++]=(uint32_t)(frame-image);
    }
    contact_properties_observe(original,e,component);
}
static int contact_properties_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+CONTACT_PROPERTIES_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+CONTACT_PROPERTIES_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+CONTACT_PROPERTIES_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+CONTACT_PROPERTIES_ENTRY,8);return actual==expected;
}
static int contact_properties_install(void) {
    if(!hash_ok(image+CONTACT_PROPERTIES_ENTRY,213,"124ab1d12945425cd1741e3ed766259ffa9c18d8e01887a072e83ddfc5ff7da0") ||
       !hash_ok(image+0xbf3b0,962,"b72407d5e66b9c5bd07321df84a6dd75bffbe6067f1af26d9ce0b1d39c216753")) return 0;
    intptr_t distance=(intptr_t)(thunk+2080)-(intptr_t)(image+CONTACT_PROPERTIES_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(2048)) return 0;
    memcpy(&contact_properties_original_word,image+CONTACT_PROPERTIES_ENTRY,8);contact_properties_patched_word=contact_properties_original_word;
    unsigned char *patch=(unsigned char*)&contact_properties_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!contact_properties_swap(contact_properties_original_word,contact_properties_patched_word)) return 0;
    contact_properties_installed=1;return 1;
}
static void contact_properties_restore(void) {
    if(!contact_properties_installed) return;
    contact_properties_restored=contact_properties_swap(contact_properties_patched_word,contact_properties_original_word);
    if(contact_properties_restored) contact_properties_installed=0;
}
