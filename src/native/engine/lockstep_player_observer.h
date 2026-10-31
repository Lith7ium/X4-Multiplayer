/* Default-off read-only player update/trigger observations. These wrappers do
 * not create another context, replace input, or switch any global player data.
 * Installed only after first admitted UI: earlier updates are not covered. */
#define PLAYER_UPDATE_ENTRY 0x2c39590u
#define PLAYER_UPDATE_RVA 0xd97e80u
#define PLAYER_SHOOT_ENTRY 0x2b7d218u
#define PLAYER_SHOOT_RVA 0x7e8bc0u
#define PLAYER_OBSERVER_CAPACITY 16384u
#define PLAYER_GLOBAL_BYTES 79u
#define PLAYER_LOCAL_BYTES 49u
typedef void (*player_update_fn)(void *,double);
typedef uintptr_t (*player_shoot_fn)(void *,void *);
typedef struct {
    uint64_t tick,actor,owner,time;
    DWORD thread;
    unsigned kind,role,result,valid;
    unsigned char globals[2][PLAYER_GLOBAL_BYTES],local[2][PLAYER_LOCAL_BYTES];
    volatile LONG ready;
} player_observation;
static player_observation player_observations[PLAYER_OBSERVER_CAPACITY];
static volatile LONG player_observer_count,player_observer_failures;
static int player_update_installed,player_shoot_installed,player_observer_restored;
static uint64_t player_observer_enabled_tick,player_observer_actor;
static void *player_observer_controller,*player_observer_object;

static int player_observer_globals(unsigned char out[PLAYER_GLOBAL_BYTES]) {
    static const unsigned axes[]={0x2f4b750,0x2f4b754,0x2f4b758,0x2f4b75c,0x2f4b760,0x2f4b764,
                                  0x2f4b788,0x2f4b78c,0x2f4b790,0x2f4b79c,0x2f4b838};
    static const unsigned timers[]={0x2f4b830,0x2f4b850,0x2f4b860};
    static const unsigned flags[]={0x2f4b828,0x2f4b858,0x6b661d0,0x38f0728,0x38f0729,0x38f072a,0x38f072b};
    unsigned n=0;
    for(unsigned i=0;i<11;i++,n+=4) if(!read_local(image+axes[i],out+n,4)) return 0;
    for(unsigned i=0;i<3;i++,n+=8) if(!read_local(image+timers[i],out+n,8)) return 0;
    if(!read_local(image+0x2f4b7d8,out+n,4)) return 0;n+=4;
    for(unsigned i=0;i<7;i++,n++) if(!read_local(image+flags[i],out+n,1)) return 0;
    return n==PLAYER_GLOBAL_BYTES;
}
static int player_observer_local(unsigned kind,void *self,unsigned char out[PLAYER_LOCAL_BYTES]) {
    unsigned char *p=self;
    if(kind==33) {
        static const unsigned times[]={0xf0,0xf8,0x19370,0x19378};
        for(unsigned i=0;i<4;i++) if(!read_local(p+times[i],out+8*i,8)) return 0;
        unsigned char *root=NULL;uint64_t runtime=0,saved=0;
        if(!read_local(p+0x2a8,&root,8) || !read_local(p+0x194f8,&runtime,8)) return 0;
        out[40]=root!=NULL;
        if(root && !read_local(root+0x20,out+32,8)) return 0;
        if(runtime) {saved=world_identity_lookup(runtime);if(!saved) return 0;}
        memcpy(out+41,&saved,8);return 1;
    }
    return read_local(p+0xa0,out,3) && read_local(p+0xa4,out+3,4) &&
           read_local(p+0xac,out+7,4) && read_local(p+0xb0,out+11,8) && read_local(p+0xb8,out+19,1);
}
static player_observation *player_observer_begin(unsigned kind,void *self,void *object,double time) {
    if(!stats.active || boot_phase!=3) return NULL;
    LONG n=InterlockedIncrement(&player_observer_count);
    if(n<1 || (unsigned)n>PLAYER_OBSERVER_CAPACITY) {InterlockedIncrement(&player_observer_failures);return NULL;}
    player_observation *s=&player_observations[n-1];
    s->kind=kind;s->tick=stats.issued;s->thread=GetCurrentThreadId();
    s->role=s->thread==ui_thread?1:s->thread==main_thread?2:3;
    memcpy(&s->time,&time,8);
    uint64_t runtime=0;
    s->valid=read_local((unsigned char*)object+8,&runtime,8)!=0;
    if(s->valid) {s->actor=world_identity_lookup(runtime);s->valid=s->actor!=0;}
    unsigned char *parent=object;
    for(unsigned i=0;parent && i<64;i++) {
        if(parent==player_observer_object) {s->owner=player_observer_actor;break;}
        if(!read_local(parent+0x70,&parent,8)) {s->valid=0;break;}
    }
    if(kind==33 && (self!=player_observer_controller || s->actor!=player_observer_actor)) s->valid=0;
    s->valid &= player_observer_globals(s->globals[0]) && player_observer_local(kind,self,s->local[0]);
    return s;
}
static void player_observer_end(player_observation *s,void *self,uintptr_t result) {
    if(!s) return;
    s->result=(unsigned)(result&255); /* Native boolean return: upper bits unspecified. */
    s->valid &= player_observer_globals(s->globals[1]) && player_observer_local(s->kind,self,s->local[1]);
    if(!s->valid) InterlockedIncrement(&player_observer_failures);
    InterlockedExchange(&s->ready,1);
}
static void player_update_observe(player_update_fn original,void *self,double time) {
    void *object=NULL;read_local((unsigned char*)self+0x28,&object,8);
    player_observation *s=player_observer_begin(33,self,object,time);
    solver_scope observed;
    solver_scope_begin(&observed,1,self,NULL,NULL,0);
    original(self,time);
    solver_scope_end(&observed);
    player_observer_end(s,self,0);
}
static uintptr_t player_shoot_observe(player_shoot_fn original,void *self,void *weapon) {
    player_observation *s=player_observer_begin(14,self,weapon,0);
    uintptr_t result=original(self,weapon);player_observer_end(s,self,result);return result;
}
static void player_update_observer(void *self,double time) {
    player_update_observe((player_update_fn)(image+PLAYER_UPDATE_RVA),self,time);
}
static uintptr_t player_shoot_observer(void *self,void *weapon) {
    return player_shoot_observe((player_shoot_fn)(image+PLAYER_SHOOT_RVA),self,weapon);
}
static int player_observer_swap(unsigned entry,void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+entry,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+entry),value,expected);
    VirtualProtect(image+entry,8,old,&ignored);return actual==expected;
}
static void player_observer_restore(void) {
    if(!player_update_installed && !player_shoot_installed) return;
    if(player_update_installed && player_observer_swap(PLAYER_UPDATE_ENTRY,(void*)player_update_observer,image+PLAYER_UPDATE_RVA)) player_update_installed=0;
    if(player_shoot_installed && player_observer_swap(PLAYER_SHOOT_ENTRY,(void*)player_shoot_observer,image+PLAYER_SHOOT_RVA)) player_shoot_installed=0;
    player_observer_restored=!player_update_installed && !player_shoot_installed;
}
static int player_observer_install(void *controller,uint64_t actor) {
    if(!controller || !actor || stats.issued!=1 || player_observer_enabled_tick || !stats.active ||
       !read_local((unsigned char*)controller+0x28,&player_observer_object,8) ||
       !hash_ok(image+PLAYER_UPDATE_RVA,3926,"2b297f805f6795a221c5c4ab139615e396380a46d000085902f41cca2aa50724") ||
       !hash_ok(image+PLAYER_SHOOT_RVA,667,"b43214a22c64011894baecf9da07b63500d8e4fbb35bfed141f057b7a070227a") ||
       *(void**)(image+PLAYER_UPDATE_ENTRY)!=image+PLAYER_UPDATE_RVA ||
       *(void**)(image+PLAYER_SHOOT_ENTRY)!=image+PLAYER_SHOOT_RVA) return 0;
    player_observer_controller=controller;player_observer_actor=actor;player_observer_enabled_tick=stats.issued;
    if(!player_observer_swap(PLAYER_UPDATE_ENTRY,image+PLAYER_UPDATE_RVA,(void*)player_update_observer)) return 0;
    player_update_installed=1;
    if(!player_observer_swap(PLAYER_SHOOT_ENTRY,image+PLAYER_SHOOT_RVA,(void*)player_shoot_observer)) {player_observer_restore();return 0;}
    player_shoot_installed=1;return 1;
}
