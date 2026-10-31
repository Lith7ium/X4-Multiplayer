/* Native contact inputs for collisions involving registered fixture ships.
 * Diagnostic only: no contact, body, shape or solver setting is changed. */
#define CONTACT_ENTRY 0xb75a0u
#define CONTACT_EVENTS 2048u
#define CONTACT_POINTS 64u
typedef void (*contact_fn)(void *,void *,void *,void *,void *,unsigned char);
typedef struct {
    unsigned actor,body_id,record_kind;uint64_t component_id,component_type,shape_type,shape_due;
    uint32_t pose[8],velocity[8];
    uint32_t active_index,island_index;unsigned scene;uint16_t layer;
    unsigned char body_type,broadphase_layer,motion_type,flags;
} contact_body;
typedef struct {
    uint64_t tick,ordinal;unsigned added,count;
    contact_body bodies[2];
    uint32_t base[4],normal[4],depth,subshape[2],points[2][CONTACT_POINTS][4];
    uint32_t settings[2][16];
} contact_event;
static contact_event contact_events[CONTACT_EVENTS];
static unsigned contact_event_count;
static uint64_t contact_calls,contact_original_word,contact_patched_word;
static volatile LONG64 contact_failures;
static int contact_installed,contact_restored;

static unsigned contact_actor(void *body) {
    void *record,*component;uint64_t id;
    if(!read_local((unsigned char*)body+0x50,&record,8) || !read_local(record,&component,8) ||
       !read_local((unsigned char*)component+8,&id,8)) return 0;
    registry_observe_object(component,REGISTRY_CONTACT);
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(component==actor_components[i] && id==actor_ids[i]) return i+1;
    return 0;
}
static void *volatile vsc_extra[2]; /* native_gate_view_scenes: scenes C and D (lockstep_view_scenes.h) */
static int contact_body_read(void *body,contact_body *out) {
    void *record,*component,*type,*shape,*shape_type,*motion,*holder,*scene,*known;
    static const unsigned scene_rvas[3]={0x38f0748u,0x38f0740u,0x38f0738u};
    memset(out,0,sizeof(*out));out->actor=contact_actor(body);
    out->active_index=out->island_index=UINT32_MAX;out->scene=3;
    if(!read_local((unsigned char*)body+0x50,&record,8) || !read_local(record,&component,8) ||
       !read_local((unsigned char*)component+8,&out->component_id,8) || !read_local(component,&type,8) ||
       !read_local((unsigned char*)body+0x40,&shape,8) || !read_local(shape,&shape_type,8) ||
       !read_local((unsigned char*)body+0x70,&out->body_id,4) || !read_local(body,out->pose,32) ||
       !read_local((unsigned char*)record+0x20,&holder,8) || (holder && !read_local((unsigned char*)holder+0x28,&out->shape_due,8)) ||
       !read_local((unsigned char*)body+0x48,&motion,8) || (motion && (!read_local(motion,out->velocity,32) ||
       !read_local((unsigned char*)motion+0x70,&out->active_index,4) ||
       !read_local((unsigned char*)motion+0x74,&out->island_index,4))) ||
       !read_local((unsigned char*)record+0x18,&scene,8) || !scene ||
       !read_local((unsigned char*)body+0x74,&out->layer,2) ||
       !read_local((unsigned char*)body+0x76,&out->body_type,1) ||
       !read_local((unsigned char*)body+0x77,&out->broadphase_layer,1) ||
       !read_local((unsigned char*)body+0x78,&out->motion_type,1) ||
       !read_local((unsigned char*)body+0x79,&out->flags,1)) return 0;
    for(unsigned i=0;i<3;i++) if(read_local(image+scene_rvas[i],&known,8) && known==scene) {
        if(out->scene!=3) return 0;
        out->scene=i;
    }
    for(unsigned i=0;i<2u;i++) if(vsc_extra[i] && vsc_extra[i]==scene) out->scene=4u+i;
    if(out->scene==3 || out->body_type>1 || out->motion_type>2) return 0;
    if((uintptr_t)type<(uintptr_t)image || (uintptr_t)type>=(uintptr_t)(image+0x6fa9000) ||
       (uintptr_t)shape_type<(uintptr_t)image || (uintptr_t)shape_type>=(uintptr_t)(image+0x6fa9000)) return 0;
    out->component_type=(uintptr_t)type-(uintptr_t)image;
    out->shape_type=(uintptr_t)shape_type-(uintptr_t)image;
    void *main_record=NULL,*second_record=NULL;out->record_kind=2;
    if(read_local((unsigned char*)component+0x260,&main_record,8) &&
       read_local((unsigned char*)component+0x268,&second_record,8)) {
        if(record==main_record) out->record_kind=0;
        else if(record==second_record) out->record_kind=1;
    }
    return 1;
}
static int contact_read(contact_event *e,void *a,void *b,void *manifold,void *settings) {
    unsigned char *m=manifold;unsigned count2;
    /* Jolt 5.5 single-precision ContactManifold: two inline arrays of 64 Vec3.
     * Their live counts are at 30/440 and points at 40/450. Unused capacity and
     * vector storage lanes are excluded when formatting diagnostic records. */
    if(!contact_body_read(a,&e->bodies[0]) || !contact_body_read(b,&e->bodies[1]) ||
       !read_local(m,e->base,16) || !read_local(m+0x10,e->normal,16) ||
       !read_local(m+0x20,&e->depth,4) || !read_local(m+0x24,e->subshape,8) ||
       !read_local(m+0x30,&e->count,4) || !read_local(m+0x440,&count2,4) ||
       e->count>CONTACT_POINTS || count2!=e->count ||
       !read_local(m+0x40,e->points[0],e->count*16) ||
       !read_local(m+0x450,e->points[1],e->count*16) ||
       !read_local(settings,e->settings[0],sizeof(e->settings[0]))) return 0;
    return 1;
}
static void contact_observe(contact_fn original,contact_event *e,void *self,void *a,void *b,
                            void *manifold,void *settings,unsigned char added) {
    e->added=added;
    if(!contact_read(e,a,b,manifold,settings)) {e->count=0;InterlockedIncrement64(&contact_failures);}
    original(self,a,b,manifold,settings,added);
    if(!read_local(settings,e->settings[1],sizeof(e->settings[1]))) InterlockedIncrement64(&contact_failures);
}
static void contact_record(void *self,void *a,void *b,void *manifold,void *settings,unsigned char added) {
    contact_fn original=(contact_fn)(thunk+848);
    if(!stats.active || boot_phase!=3 || (!contact_actor(a) && !contact_actor(b))) {
        original(self,a,b,manifold,settings,added);return;
    }
    if(GetCurrentThreadId()!=ui_thread || contact_event_count==CONTACT_EVENTS || added>1) {
        InterlockedIncrement64(&contact_failures);original(self,a,b,manifold,settings,added);return;
    }
    contact_event *e=&contact_events[contact_event_count++];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->ordinal=++contact_calls;
    contact_observe(original,e,self,a,b,manifold,settings,added);
}
static int contact_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+CONTACT_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+CONTACT_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+CONTACT_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+CONTACT_ENTRY,8);return actual==expected;
}
#include "lockstep_contact_properties.h"
static int contact_install(void) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_physics.dll") ||
       !hash_ok(image+CONTACT_ENTRY,1846,"cc0e51a03cdf9622a94f8b239bfd2ba7c3487219802b201a75e9ef2b2aad24ff")) return 0;
    intptr_t distance=(intptr_t)(thunk+880)-(intptr_t)(image+CONTACT_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(848)) return 0;
    memcpy(&contact_original_word,image+CONTACT_ENTRY,8);contact_patched_word=contact_original_word;
    unsigned char *patch=(unsigned char*)&contact_patched_word;patch[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(patch+1,&rel,4);
    if(!contact_swap(contact_original_word,contact_patched_word)) return 0;
    contact_installed=1;return contact_properties_install();
}
static void contact_restore(void) {
    contact_properties_restore();
    if(!contact_installed) return;
    contact_restored=contact_swap(contact_patched_word,contact_original_word);if(contact_restored) contact_installed=0;
}
