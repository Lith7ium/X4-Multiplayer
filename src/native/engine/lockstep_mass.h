/* Observe which native shape changes recompute fixture mass properties. */
#define MASS_ENTRY 0x14061e0u
#define MASS_EVENTS 2048u
typedef void (*mass_fn)(void *,const uint32_t *,const float *,unsigned char,unsigned);
typedef struct {uint64_t tick,source;unsigned actor,which,update_mass,activate,id;uint32_t before[8],after[8],stack[8];} mass_event;
static mass_event mass_events[MASS_EVENTS];
static unsigned mass_event_count;
static uint64_t mass_calls,mass_failures,mass_original_word,mass_patched_word;
static int mass_installed,mass_restored;
static unsigned char *mass_body(void *interface,const uint32_t *idptr,uint32_t *id) {
    unsigned char *lock,*manager,*bodies,*body;uint64_t count;uint32_t actual;
    if(!read_local(idptr,id,4) || *id==0xffffffffu || !read_local(interface,&lock,8) ||
       !read_local(lock+8,&manager,8) || !read_local(manager,&count,8) || count>0x800000u || (*id&0x7fffffu)>=count ||
       !read_local(manager+0x10,&bodies,8) || !read_local(bodies+(*id&0x7fffffu)*8,&body,8) || !body || ((uintptr_t)body&1) ||
       !read_local(body+0x70,&actual,4) || actual!=*id) return NULL;
    return body;
}
static void mass_notify(void *interface,const uint32_t *idptr,const float *center,unsigned char update_mass,unsigned activate) {
    mass_fn original=(mass_fn)(thunk+512);unsigned actor=0,which=0;unsigned char *body=NULL,*record,*object,*motion=NULL;uint32_t id=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) {
        body=mass_body(interface,idptr,&id);
        if(body && read_local(body+0x50,&record,8) && read_local(record,&object,8)) {
            registry_observe_object(object,REGISTRY_MASS);
            for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
                uint64_t actual;void *a,*b;
                if(object!=actor_components[i] || !object || !read_local(object+8,&actual,8) || actual!=actor_ids[i]) continue;
                if(!read_local(object+0x260,&a,8) || !read_local(object+0x268,&b,8)) break;
                if(record==a) {actor=i+1;which=0;break;}if(record==b) {actor=i+1;which=1;break;}
            }
        }
    }
    if(!actor) {original(interface,idptr,center,update_mass,activate);return;}
    if(mass_event_count==MASS_EVENTS) {++mass_failures;original(interface,idptr,center,update_mass,activate);return;}
    mass_event *e=&mass_events[mass_event_count++];memset(e,0,sizeof(*e));++mass_calls;
    e->actor=actor;e->which=which;e->tick=stats.issued;e->id=id;e->update_mass=update_mass;e->activate=activate;
    unsigned char *caller=__builtin_return_address(0);
    e->source=caller>=image && caller<image+0x6fa9000?(uint64_t)(caller-image):0;
    if(update_mass) {
        void *frames[16];unsigned n=CaptureStackBackTrace(0,16,frames,NULL),j=0;
        for(unsigned i=0;i<n && j<8;i++) {
            unsigned char *frame=frames[i];if(frame>=image && frame<image+0x6fa9000) e->stack[j++]=(uint32_t)(frame-image);
        }
    }
    if(!read_local(body+0x48,&motion,8) || !motion || !read_local(motion+0x20,e->before,sizeof(e->before))) ++mass_failures;
    original(interface,idptr,center,update_mass,activate);
    if(!motion || !read_local(motion+0x20,e->after,sizeof(e->after))) ++mass_failures;
}
static int mass_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+MASS_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+MASS_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+MASS_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+MASS_ENTRY,8);return actual==expected;
}
static int mass_install(void) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+MASS_ENTRY,32,"797691d3acd16061692c52618652e76fc936f079571bb1fd5d21e08477ed6738")) return 0;
    intptr_t distance=(intptr_t)(thunk+544)-(intptr_t)(image+MASS_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(512)) return 0;
    memcpy(&mass_original_word,image+MASS_ENTRY,8);mass_patched_word=mass_original_word;
    unsigned char *patch=(unsigned char*)&mass_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);patch[5]=patch[6]=0x90;
    if(!mass_swap(mass_original_word,mass_patched_word)) return 0;
    mass_installed=1;return 1;
}
static void mass_restore(void) {
    if(!mass_installed) return;
    mass_restored=mass_swap(mass_patched_word,mass_original_word);if(mass_restored) mass_installed=0;
}
