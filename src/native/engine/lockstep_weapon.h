/* Fixture guns are identified by actor and physical upgrade slot, never by
 * process-local UniverseID ordering. The original native Move still runs once. */
#define WEAPON_ENTRY 0x2b69788u
#define WEAPON_MOVE 0x8132d0u
#define WEAPON_SLOTS 32u
static const unsigned weapon_entries[2]={WEAPON_ENTRY,0x2b03e98u};
static const unsigned weapon_vtables[2]={0x2b68278u,0x2b02988u};
typedef void (*weapon_fn)(void *);
static int player_weapon_observe(weapon_fn original,void *self);
typedef struct {void *object;uint64_t id,tick,calls,rng_before,rng_after,spawns;} weapon_sample;
static weapon_sample weapon_samples[ACTOR_CAPACITY][WEAPON_SLOTS];
/* Only read on the owning MoveWorker. Save/restore permits nested native walks. */
static weapon_sample *weapon_context;
static unsigned weapon_context_actor,weapon_context_slot;
static int weapon_installed,weapon_restored,weapon_seeded;
static uint64_t weapon_calls,weapon_failures;
static unsigned weapon_registered;
static int weapon_actor(void *object,unsigned actor) {
    unsigned char *entity=object;
    for(unsigned depth=0;entity && depth<16;depth++) {
        uint64_t id;unsigned char *next;
        if(!read_local(entity+8,&id,8)) return 0;
        if(entity==actor_components[actor-1] && id==actor_ids[actor-1]) return 1;
        if(!read_local(entity+0x70,&next,8) || next==entity) return 0;
        entity=next;
    }
    return 0;
}
static int weapon_register(void *object,uint64_t id,unsigned actor,unsigned slot) {
    void *vtable;uint64_t actual;
    if(!actor_is_ship(actor) || slot<1 || slot>WEAPON_SLOTS || !id ||
       !read_local(object,&vtable,8) || (vtable!=image+weapon_vtables[0] && vtable!=image+weapon_vtables[1]) ||
       !read_local((unsigned char*)object+8,&actual,8) || actual!=id || !weapon_actor(object,actor)) return 0;
    weapon_sample *s=&weapon_samples[actor-1][slot-1];
    if(s->object) return s->object==object && s->id==id;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) for(unsigned j=0;j<WEAPON_SLOTS;j++) if(weapon_samples[i][j].object==object) return 0;
    s->object=object;s->id=id;++weapon_registered;return 1;
}
static void weapon_execute(weapon_fn original,weapon_sample *s,unsigned actor,unsigned slot,uint64_t *rng,void *self) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=weapon_seeded && rng;
    weapon_sample *outer=weapon_context;unsigned outer_actor=weapon_context_actor,outer_slot=weapon_context_slot;
    __try {
        if(restore_rng) *rng=actor_seed(0x574541504f4e5f4dULL,stats.issued,actor,slot,s->calls,0);
        weapon_context=s;weapon_context_actor=actor;weapon_context_slot=slot;
        s->rng_before=rng?*rng:0;original(self);s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        weapon_context=outer;weapon_context_actor=outer_actor;weapon_context_slot=outer_slot;
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&weapon_failures);
        SetLastError(error);
    }
}
static void weapon_move(void *self) {
    registry_observe_chain(self,REGISTRY_WEAPON);
    weapon_fn original=(weapon_fn)(image+WEAPON_MOVE);weapon_sample *s=NULL;unsigned actor=0,slot=0;
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread) {
        for(unsigned i=0;i<ACTOR_CAPACITY && !s;i++) for(unsigned j=0;j<WEAPON_SLOTS;j++) {
            weapon_sample *candidate=&weapon_samples[i][j];
            if(candidate->object==self && *(uint64_t*)((unsigned char*)self+8)==candidate->id) {s=candidate;actor=i+1;slot=j+1;break;}
        }
    }
    if(!s) {
        if(player_weapon_observe(original,self)) return;
        if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && actor_all_on()) actor_all_weapon_execute(original,self,actor_all_tls_rng());
        else original(self);
        return;
    }
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++weapon_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng) ++weapon_failures;
    weapon_execute(original,s,actor,slot,rng,self);
}
static int weapon_swap(unsigned index,void *expected,void *value) {
    unsigned entry=weapon_entries[index];
    DWORD old,ignored;if(!VirtualProtect(image+entry,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+entry),value,expected);
    VirtualProtect(image+entry,8,old,&ignored);return actual==expected;
}
static int weapon_owned(void) {
    for(unsigned i=0;i<2;i++) if((weapon_installed&(1<<i)) && *(void**)(image+weapon_entries[i])!=(void*)weapon_move) return 0;
    return 1;
}
static void weapon_restore(void) {
    if(!weapon_installed) return;
    for(unsigned i=0;i<2;i++) if(weapon_installed&(1<<i)) {
        if(weapon_swap(i,(void*)weapon_move,image+WEAPON_MOVE)) weapon_installed&=~(1<<i);
    }
    weapon_restored=weapon_installed==0;
}
static int weapon_install(int seed) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_combat.dll") ||
       !hash_ok(image+WEAPON_MOVE,32,"2f3c1a7982a384fd4036b3bbd5051f9bee25ce0946e9211136c7ddbfb1217993")) return 0;
    for(unsigned i=0;i<2;i++) if(*(void**)(image+weapon_entries[i])!=image+WEAPON_MOVE) return 0;
    for(unsigned i=0;i<2;i++) {
        if(!weapon_swap(i,image+WEAPON_MOVE,(void*)weapon_move)) {weapon_restore();return 0;}
        weapon_installed|=1<<i;
    }
    weapon_seeded=seed;return 1;
}
