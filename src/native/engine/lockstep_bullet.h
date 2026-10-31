/* Native bullets only. Creation inside a registered gun establishes a logical
 * actor/physical-slot/spawn identity; pointers and UniverseIDs are guards only.
 * The bounded trace survives UI sampling boundaries, including post-UI callbacks. */
#define BULLET_CAP 16384u
#define BULLET_EVENTS 2048u
#define BULLET_CREATE 0x83b500u
#define BULLET_MOVE 0xd4f090u
static const unsigned bullet_entries[2]={0x2b67d90u,0x2c43fd8u};
typedef void **(*bullet_create_fn)(void *,void **,uintptr_t,uintptr_t);
static void player_bullet_created(void *object);
static int player_bullet_observe(weapon_fn original,void *self);
/* Hit-test cadence counters (lockstep_bullet_cadence.h); inert unless armed. */
typedef struct {double now,next,last;unsigned valid;} bullet_cadence_sample;
static void bullet_cadence_enter(const void *bullet,bullet_cadence_sample *c);
static void bullet_cadence_leave(const void *bullet,const bullet_cadence_sample *c,unsigned other);
typedef struct {void *object;uint64_t id,spawn,tick,calls;unsigned actor,slot;} bullet_identity;
typedef struct {
    uint64_t tick,spawn,call,before,after,check_before,check_after;
    unsigned actor,slot,created;int hits_before,hits_after;
} bullet_event;
static bullet_identity bullet_registry[BULLET_CAP];
static bullet_event bullet_events[BULLET_EVENTS];
static unsigned bullet_event_count;
static uint64_t bullet_dropped;
static int bullet_installed,bullet_restored,bullet_seeded;
static uint64_t bullet_calls,bullet_created,bullet_failures,bullet_unknown,bullet_outside,bullet_unsupported;
static uint64_t bullet_mix(uint64_t x) {
    return lockstep_mix(x);
}
static uint64_t bullet_seed(uint64_t tick,unsigned actor,unsigned slot,uint64_t spawn,uint64_t call) {
    return actor_seed(0x42554c4c45545f31ULL,tick,actor,slot,spawn,call);
}
static bullet_identity *bullet_find(void *object,int insert) {
    unsigned index=(unsigned)bullet_mix((uintptr_t)object)&(BULLET_CAP-1);
    for(unsigned n=0;n<BULLET_CAP;n++,index=(index+1)&(BULLET_CAP-1)) {
        bullet_identity *s=&bullet_registry[index];
        if(s->object==object) return s;
        if(!s->object) return insert?s:NULL;
    }
    return NULL;
}
static bullet_event *bullet_record(bullet_identity *s,unsigned created) {
    if(gate_play_mode) return NULL;
    if(bullet_event_count==BULLET_EVENTS) {++bullet_dropped;return NULL;} /* rows only: the key is in bullet_execute */
    bullet_event *e=&bullet_events[bullet_event_count++];memset(e,0,sizeof(*e));
    e->tick=stats.issued;e->actor=s->actor;e->slot=s->slot;e->spawn=s->spawn;e->call=s->calls;e->created=created;
    return e;
}
static int bullet_register(void *object,weapon_sample *weapon,unsigned actor,unsigned slot) {
    void *vtable;uint64_t id;
    if(!object || !actor_is_ship(actor) || slot<1 || slot>WEAPON_SLOTS ||
       !read_local(object,&vtable,8) || !read_local((unsigned char*)object+8,&id,8) || !id) return 0;
    if(vtable!=image+0x2c42ac8) {++bullet_unsupported;return 0;}
    bullet_identity *s=bullet_find(object,1);if(!s || (s->object && s->id==id)) return 0;
    *s=(bullet_identity){.object=object,.id=id,.spawn=++weapon->spawns,.actor=actor,.slot=slot};
    ++bullet_created;bullet_record(s,1);return 1;
}
static void **bullet_create(void *zone,void **out,uintptr_t macro,uintptr_t flags) {
    void **result=((bullet_create_fn)(image+BULLET_CREATE))(zone,out,macro,flags);
    if(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread && result && *result) {
        if(weapon_context) {
            if(!bullet_register(*result,weapon_context,weapon_context_actor,weapon_context_slot)) ++bullet_failures;
        } else {++bullet_outside;player_bullet_created(*result);}
    }
    return result;
}
static void bullet_execute(weapon_fn original,bullet_identity *s,bullet_event *e,uint64_t *rng,void *self) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=bullet_seeded && rng;
    __try {
        if(restore_rng) *rng=bullet_seed(stats.issued,s->actor,s->slot,s->spawn,s->calls);
        if(e) e->before=rng?*rng:0;
        original(self);
        if(e) e->after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&bullet_failures);
        SetLastError(error);
    }
}
static void bullet_move(void *self) {
    weapon_fn original=(weapon_fn)(image+BULLET_MOVE);
    if(!stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) {original(self);return;}
    bullet_identity *s=bullet_find(self,0);uint64_t id;
    bullet_cadence_sample cadence;bullet_cadence_enter(self,&cadence);
    if(!s || !read_local((unsigned char*)self+8,&id,8) || s->id!=id) {
        ++bullet_unknown;if(!player_bullet_observe(original,self)) original(self);
        bullet_cadence_leave(self,&cadence,1);return;
    }
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}
    ++s->calls;++bullet_calls;bullet_event *e=bullet_record(s,0);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(!rng) ++bullet_failures;
    if(e && (!read_local((unsigned char*)self+0x268,&e->check_before,8) ||
             !read_local((unsigned char*)self+0x280,&e->hits_before,4))) ++bullet_failures;
    bullet_execute(original,s,e,rng,self);
    /* Native destruction is posted; the bullet survives its own Move. */
    if(e && (!read_local((unsigned char*)self+0x268,&e->check_after,8) ||
             !read_local((unsigned char*)self+0x280,&e->hits_after,4))) ++bullet_failures;
    bullet_cadence_leave(self,&cadence,0);
}
static void *bullet_original(unsigned i) {return image+(i?BULLET_MOVE:BULLET_CREATE);}
static void *bullet_wrapper(unsigned i) {return i?(void*)bullet_move:(void*)bullet_create;}
static int bullet_swap(unsigned i,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+bullet_entries[i],8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+bullet_entries[i]),value,expected);
    VirtualProtect(image+bullet_entries[i],8,old,&ignored);return actual==expected;
}
static int bullet_owned(void) {
    for(unsigned i=0;i<2;i++) if((bullet_installed&(1<<i)) && *(void**)(image+bullet_entries[i])!=bullet_wrapper(i)) return 0;
    return 1;
}
static void bullet_restore(void) {
    if(!bullet_installed) return;
    for(unsigned i=0;i<2;i++) if((bullet_installed&(1<<i)) && bullet_swap(i,bullet_wrapper(i),bullet_original(i))) bullet_installed&=~(1<<i);
    bullet_restored=bullet_installed==0;
}
static int bullet_install(int seed) {
    if(!boot_mode || weapon_installed!=3 || GetModuleHandleW(L"x4lan_fixedtick_64.dll") || GetModuleHandleW(L"x4mp_combat.dll") ||
       !hash_ok(image+BULLET_CREATE,32,"888c3c759fbd81b7da71c3005dc04088abc8dca28695bb53950761e6dc4b641d") ||
       !hash_ok(image+BULLET_MOVE,32,"5c2c66d25003bf15ecb36c641c6e46659067754dd45460a4b3575c1382e587bc")) return 0;
    for(unsigned i=0;i<2;i++) if(*(void**)(image+bullet_entries[i])!=bullet_original(i)) return 0;
    for(unsigned i=0;i<2;i++) {
        if(!bullet_swap(i,bullet_original(i),bullet_wrapper(i))) {bullet_restore();return 0;}
        bullet_installed|=1<<i;
    }
    bullet_seeded=seed;return 1;
}
