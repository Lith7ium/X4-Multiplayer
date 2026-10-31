/* Read-only motion solver observation. No native call is added: the entry
 * wrapper forwards each existing invocation once, including diagnostic failure.
 * Raw object/solver pointers never leave this module. */
#define SOLVER_RVA 0x1197fe0u
#define SOLVER_TRAMPOLINE 2304u
#define SOLVER_DISPATCH 2368u
#define SOLVER_OPTION (1ULL<<50)
#define SOLVER_CAPACITY 8192u
#define SOLVER_CONTEXT_BYTES 504u
#define SOLVER_MOTION_BYTES 160u
typedef uintptr_t (*solver_fn)(void *,void *,void *,void *,void *);
typedef struct {
    uint64_t tick,owner,ordinal,caller,solve_tick,solve_ordinal;
    unsigned kind,role,actor,bracket,result,valid;
    unsigned char context[2][SOLVER_CONTEXT_BYTES],motion[2][SOLVER_MOTION_BYTES];
    unsigned char source[96],configuration[8],published[96];
    volatile LONG ready;
} solver_event;
static solver_event solver_events[SOLVER_CAPACITY];
static volatile LONG solver_count,solver_failures,solver_excluded,solver_open_scopes;
static int solver_installed,solver_restored;
static uint64_t solver_original_word,solver_patched_word,solver_enabled_tick;
static __thread solver_scope *solver_current_scope;
typedef struct { uint64_t tick,ordinal;LONG latest,published;volatile LONG busy; } solver_owner_state;
static solver_owner_state solver_owners[13];

static int solver_context_read(void *self,unsigned char *out) {
    unsigned char *p=self;
    /* Selected native fields without vtables/self-pointers. The complete header
     * also retains constructor-unwritten alignment bytes: raw equality is strict,
     * and their diagnostic interpretation must not silently exclude them. */
    return read_local(p,out,0x78) && read_local(p+0xa0d8,out+0x78,0x30) &&
           read_local(p+0x163a8,out+0xa8,0x30) && read_local(p+0x18e40,out+0xd8,0x120);
}
static int solver_motion_read(void *acceleration,void *local_velocity,void *pose,void *velocity,unsigned char *out) {
    return read_local(acceleration,out,32) && read_local(local_velocity,out+32,32) &&
           read_local(pose,out+64,64) && read_local(velocity,out+128,32);
}
static solver_event *solver_record(unsigned kind,unsigned owner,uint64_t saved) {
    LONG n=InterlockedIncrement(&solver_count);
    if(n<1 || (unsigned)n>SOLVER_CAPACITY) {InterlockedIncrement(&solver_failures);return NULL;}
    solver_event *e=&solver_events[n-1];memset(e,0,sizeof(*e));
    e->kind=kind;e->actor=owner;e->owner=saved;e->tick=stats.issued;
    DWORD thread=GetCurrentThreadId();e->role=thread==ui_thread?1:thread==main_thread?2:3;
    return e;
}
static int solver_identity(unsigned owner,uint64_t *saved) {
    void *component=owner?actor_components[owner-1]:player_observer_object;uint64_t runtime=0;
    if(!component || !read_local((unsigned char*)component+8,&runtime,8) || !runtime) return 0;
    *saved=world_identity_lookup(runtime);
    return *saved && (owner?runtime==actor_ids[owner-1]:*saved==player_observer_actor);
}
static int solver_claim(void *self,unsigned *owner,uint64_t *saved) {
    unsigned matches=0;void *candidate=NULL;
    if(player_observer_controller && self==(unsigned char*)player_observer_controller+0x3b0) {++matches;*owner=0;}
    for(unsigned i=0;i<12;i++) if(flight_samples[i].agent &&
       read_local((unsigned char*)flight_samples[i].agent+0xf80,&candidate,8) && candidate==self) {
        ++matches;*owner=i+1;
    }
    if(!self || !matches) return 0;
    if(matches!=1 || !solver_identity(*owner,saved)) return -1;
    void *claimed_component=*owner?actor_components[*owner-1]:player_observer_object;
    for(unsigned i=0;i<12;i++) if(i+1!=*owner && actor_components[i] && actor_components[i]==claimed_component) return -1;
    if(*owner && claimed_component==player_observer_object) return -1;
    if(*owner) {
        void *component=NULL;
        if(!read_local((unsigned char*)flight_samples[*owner-1].agent+0xef0,&component,8) ||
           component!=actor_components[*owner-1]) return -1;
    } else {
        void *component=NULL;
        if(!read_local((unsigned char*)player_observer_controller+0x28,&component,8) || component!=player_observer_object) return -1;
    }
    return 1;
}
static int solver_bracket_valid(unsigned owner,uintptr_t caller,void *acceleration,void *local_velocity,void *pose,void *velocity) {
    solver_scope *s=solver_current_scope;
    if(!owner) {
        unsigned char *p=player_observer_controller;
        if(caller!=0xd93559 || GetCurrentThreadId()!=ui_thread || acceleration!=p+0x1a0 ||
           local_velocity!=p+0x1c0 || pose!=p+0x100 || velocity!=p+0x160) return 0;
        return !s || (s->active && s->kind==1 && s->self==p && s->tick==stats.issued && s->thread==GetCurrentThreadId());
    }
    return caller==0xec92c5 && s && s->active && s->kind==2 && s->actor==owner &&
           s->agent==flight_samples[owner-1].agent && s->self==flight_samples[owner-1].model &&
           s->thread==GetCurrentThreadId() && s->tick==stats.issued;
}
static uintptr_t solver_observe(solver_fn original,void *self,void *acceleration,void *local_velocity,
                                void *pose,void *velocity,uintptr_t caller) {
    if(!solver_installed || !stats.active || boot_phase!=3) return original(self,acceleration,local_velocity,pose,velocity);
    unsigned owner=0;uint64_t saved=0;int claimed=solver_claim(self,&owner,&saved);
    if(!claimed) {InterlockedIncrement(&solver_excluded);return original(self,acceleration,local_velocity,pose,velocity);}
    if(claimed<0 || !solver_bracket_valid(owner,caller,acceleration,local_velocity,pose,velocity)) {
        InterlockedIncrement(&solver_failures);return original(self,acceleration,local_velocity,pose,velocity);
    }
    solver_owner_state *state=&solver_owners[owner];
    if(InterlockedCompareExchange(&state->busy,1,0)) {
        InterlockedIncrement(&solver_failures);return original(self,acceleration,local_velocity,pose,velocity);
    }
    if(state->tick!=stats.issued) {state->tick=stats.issued;state->ordinal=0;}
    ++state->ordinal;
    solver_event *e=solver_record(1,owner,saved);
    if(e) {
        e->ordinal=state->ordinal;e->caller=caller;e->bracket=solver_current_scope?solver_current_scope->kind:0;
        void *configuration=NULL;
        unsigned char *source=owner?(unsigned char*)flight_samples[owner-1].agent+0x340:
                                   (unsigned char*)player_observer_controller+0x19430;
        e->valid=solver_context_read(self,e->context[0]) &&
            solver_motion_read(acceleration,local_velocity,pose,velocity,e->motion[0]) &&
            read_local(source,e->source,sizeof(e->source)) && read_local(image+0x6cf1538,&configuration,8) && configuration &&
            read_local((unsigned char*)configuration+0x5054,e->configuration,8);
    }
    uintptr_t result=original(self,acceleration,local_velocity,pose,velocity);
    if(e) {
        uint64_t checked=0;unsigned checked_owner=0;
        e->result=(unsigned)(result&255);
        e->valid &= solver_claim(self,&checked_owner,&checked)==1 && checked_owner==owner && checked==saved &&
            solver_context_read(self,e->context[1]) && solver_motion_read(acceleration,local_velocity,pose,velocity,e->motion[1]);
        if(!e->valid) InterlockedIncrement(&solver_failures);
        state->latest=(LONG)(e-solver_events)+1;InterlockedExchange(&e->ready,1);
    }
    InterlockedExchange(&state->busy,0);return result;
}
static uintptr_t solver_dispatch(void *self,void *acceleration,void *local_velocity,void *pose,void *velocity) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    return solver_observe((solver_fn)(thunk+SOLVER_TRAMPOLINE),self,acceleration,local_velocity,pose,velocity,caller);
}
static void solver_scope_begin(solver_scope *s,unsigned kind,void *self,void *agent,void *slot,unsigned actor) {
    memset(s,0,sizeof(*s));
    if(!solver_installed || !stats.active || boot_phase!=3) return;
    /* The shared flight wrapper also serves dynamic registry ships. This
     * observer claims only the saved fixture subset; their solver calls still
     * pass through solver_claim and are counted as excluded coverage. */
    if(kind==2 && actor>ACTOR_FIXTURE_COUNT) return;
    s->previous=solver_current_scope;s->kind=kind;s->self=self;s->agent=agent;s->slot=slot;s->actor=actor;
    s->tick=stats.issued;s->thread=GetCurrentThreadId();s->active=1;solver_current_scope=s;
    InterlockedIncrement(&solver_open_scopes);
}
static void solver_scope_end(solver_scope *s) {
    if(!s->active) return;
    int balanced=solver_current_scope==s && s->thread==GetCurrentThreadId();
    if(!balanced) {InterlockedIncrement(&solver_failures);solver_current_scope=NULL;}
    else if(s->kind) {
        unsigned owner=s->kind==1?0:s->actor;uint64_t saved=0;
        if((s->kind!=1 && s->kind!=2) || owner>12 || (s->kind==1 && s->self!=player_observer_controller) || !solver_identity(owner,&saved)) {
            InterlockedIncrement(&solver_failures);
        } else {
            solver_owner_state *state=&solver_owners[owner];LONG latest=state->latest;
            if(latest>state->published && latest>0 && latest<=(LONG)SOLVER_CAPACITY) {
                solver_event *solve=&solver_events[latest-1],*e=solver_record(2,owner,saved);
                if(e) {
                    e->solve_tick=solve->tick;e->solve_ordinal=solve->ordinal;e->bracket=s->kind;
                    if(owner) e->valid=s->agent==flight_samples[owner-1].agent && s->slot && read_local(s->slot,e->published,96);
                    else {
                        void *slots=NULL;unsigned index=0;
                        e->valid=read_local((unsigned char*)s->self+0x30,&slots,8) && slots &&
                            read_local(image+0x2f4b6f4,&index,4) && index<2 &&
                            read_local((unsigned char*)slots+index*0x1a0,e->published,96);
                    }
                    e->valid &= solve->ready && solve->valid && solve->owner==saved && solve->tick<=e->tick;
                    if(!e->valid) InterlockedIncrement(&solver_failures);
                    InterlockedExchange(&e->ready,1);
                }
                state->published=latest;
            }
        }
    }
    /* Kind0 shadows excluded native flight callbacks without claiming owner0
     * or publishing an outer saved owner's most recent result. */
    if(balanced) solver_current_scope=s->previous;
    s->active=0;InterlockedDecrement(&solver_open_scopes);
}
static int solver_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+SOLVER_RVA,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t found=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+SOLVER_RVA),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+SOLVER_RVA,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+SOLVER_RVA,8);return found==expected;
}
static void solver_restore(void) {
    if(!solver_installed) return;
    solver_restored=solver_swap(solver_patched_word,solver_original_word);
    if(solver_restored) solver_installed=0;
    if(solver_open_scopes) InterlockedIncrement(&solver_failures);
}
/* Retained as the independent actual-image ABI emitter used by the test harness. */
static __attribute__((unused)) int solver_emit_thunks(unsigned char *trampoline,unsigned char *dispatch,const unsigned char *entry,void *target) {
    static const unsigned char prefix[6]={0x40,0x53,0x48,0x83,0xec,0x60};
    if(memcmp(entry,prefix,6)) return 0; /* REX-prefixed push (2), sub (4); no RIP-relative relocation */
    memcpy(trampoline,entry,6);trampoline[6]=0x49;trampoline[7]=0xbb;
    const void *continuation=entry+6;memcpy(trampoline+8,&continuation,8);
    trampoline[16]=0x41;trampoline[17]=0xff;trampoline[18]=0xe3;
    const unsigned char unwind[8]={1,6,2,0,6,0xb2,2,0x30};memcpy(trampoline+32,unwind,8);
    dispatch[0]=0xff;dispatch[1]=0x25;memset(dispatch+2,0,4);memcpy(dispatch+6,&target,8);return 1;
}
static int solver_pins(void);
static int solver_install(void) {
    if(!(boot_options&SOLVER_OPTION) || !boot_mode || !stats.active || stats.issued!=1 || boot_phase!=3 ||
       !player_observer_enabled_tick || !flight_installed || !path_installed || !(path_options&1) || !(boot_options&1) ||
       solver_enabled_tick || GetCurrentThreadId()!=ui_thread || !solver_pins()) return 0;
    intptr_t distance=(intptr_t)(thunk+SOLVER_DISPATCH)-(intptr_t)(image+SOLVER_RVA+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(2304)) return 0;
    if(!thunk_unwind_prepare(2304)) return 0;
    memcpy(&solver_original_word,image+SOLVER_RVA,8);solver_patched_word=solver_original_word;
    unsigned char *p=(unsigned char*)&solver_patched_word;p[0]=0xe9;int32_t rel=(int32_t)distance;memcpy(p+1,&rel,4);p[5]=0x90;
    if(!solver_swap(solver_original_word,solver_patched_word)) return 0;
    solver_installed=1;solver_enabled_tick=stats.issued;return 1;
}

static int solver_pins(void) {
    return hash_ok(image+0x004500a0,730,"9c05b270f67d60de38a00713724c8e6cce9e2934692151be662e878d62b883d9") &&
           hash_ok(image+0x00d83550,787,"e97ff648019827beca69d4b62e3411c90d59f846d6eae31a1c4209477c1a70df") &&
           hash_ok(image+0x00d84c20,1244,"be54c529ecbce01377450ef1c583fe747c80399bac1b226fd9d58ce41f973a17") &&
           hash_ok(image+0x00d895a0,666,"1cc04226f205a1c9bf2c47cb42b84ed3bfbd8a78aab05e31e37fd77e286f12d9") &&
           hash_ok(image+0x00d8fb60,3271,"56a5a8448725204bc198db77afdeceb0bde31896453191eb9e09b65acbf0dab9") &&
           hash_ok(image+0x00d95180,7221,"c55c35cea23632e2ab86e60c68f3222b7cab0160a788e662d67cde1309e63d33") &&
           hash_ok(image+0x00d93440,4657,"b6342c28509fe6964c27cea83c31e82867b38b5e20634afafb93d0d38bb4e5af") &&
           hash_ok(image+0x00d94700,2686,"96e1a1dd1cc86844faf8f85332cf964f19a9c8178ca6aca04e61c257156b4590") &&
           hash_ok(image+0x00d924d0,3221,"63cf643f55130eb8c05098e93155fd88b2d42a36a1cbba73c96ce6fd27343403") &&
           hash_ok(image+0x00d96dc0,2922,"04fb88198f59a34b15cf8f855b66ffdfbd13d65888f7a314ef22118f8a2443f1") &&
           hash_ok(image+0x00d90ed0,348,"54fa81b9fb10251ceb63225ad042db51621a44d538d5754ef430bbfcb9d764ac") &&
           hash_ok(image+0x00d91030,345,"47cfdca551dd733b7653e51ff2c88fb2042113ccbbcaaa00d9ba4617f851839a") &&
           hash_ok(image+0x00d91190,348,"31eb5a540d4ae40bf34df612be3f7ddfc0abf06b29463679816b63bfa562c353") &&
           hash_ok(image+0x00d91da0,416,"177f27fde91b797aecff49ddb765deeb6d4322ce583b8c1a6adc5ecb095333ee") &&
           hash_ok(image+0x00d91f40,456,"9e1a5a2022e086599ebf50b25288ffa148be99899193e92715540a0d34f9365f") &&
           hash_ok(image+0x00d92110,459,"c8af22f868f60fd51b567340d02fd8efb56b9ee150ad071559611209694eecc7") &&
           hash_ok(image+0x00d47ac0,1676,"b6ca142e4a9713bdd14f9f9676bdc1621b6185186d0aec7572d0cf7e014ca396") &&
           hash_ok(image+0x00e95fb0,2048,"c72b49324fed4c46ad78a1b40c100ec7c710e9ae636e6f5cfe6ccd19d258e20d") &&
           hash_ok(image+0x00e5d380,214,"ef39acf755eaecacd63bb4b33fe48ad93094952929135412787ef0efe0b3b4b4") &&
           hash_ok(image+0x01197fe0,224,"51bfe36495c5f64db4c3c10acd96d19dd5248d0d60f271e12aa3d4829a9e1f28") &&
           hash_ok(image+0x01197970,1635,"9c1b408beba4208821aa973fc2d1e729389872624a533e0105766e0cab09fe9b") &&
           hash_ok(image+0x01191740,803,"2ecbd9c56befa11cf670f1604baf597ae88bab365a6b08503ee9b85354495e77") &&
           hash_ok(image+0x01195570,7923,"a47386260e080a19a66783454404db0cc484661dee90e3cf87d457f2635bcfa6") &&
           hash_ok(image+0x01191a70,941,"a3d1059fef01c6285b77a152b45a8df15ff2f9e6c8ad4c12b24e1653805d2477") &&
           hash_ok(image+0x01197470,1273,"eb6d80c0f88b9b8644bea274bc5ed74a1ea34c8e9ce7956e38326318f9c8d8b5") &&
           hash_ok(image+0x011980c0,1067,"91a96969ce261f97e9c86da0b121d62b1f357d5366a4c70514cf494a2e1aa4d4") &&
           hash_ok(image+0x00ec7200,10109,"bff420d07fb655ff195560b395af5decc0ab5b2ecf9691481e578eb3282eb796") &&
           hash_ok(image+0x007e8bc0,667,"b43214a22c64011894baecf9da07b63500d8e4fbb35bfed141f057b7a070227a");
}
