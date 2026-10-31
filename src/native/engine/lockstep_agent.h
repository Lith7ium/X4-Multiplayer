/* Fixture-only bracket around the complete pathing Agent update. The budget
 * flag affects behaviour selection before SteeringFlightControlModel::Update,
 * so a steering-only bracket cannot make that earlier decision deterministic. */
#define AGENT_REL 0xeaed70u
#define AGENT_ORIGINAL 0xe8f250u
typedef void (*agent_fn)(void *,void *,float,double,unsigned char);
typedef struct {
    void *agent;
    uint64_t tick,calls,cheap,applied_cheap,rng_before,rng_after,time;
    uint64_t tls_time;
    uint32_t dt,tls_view,limits_before[24],limits_after[24];
} agent_sample;
static agent_sample agent_samples[ACTOR_CAPACITY];
static int32_t agent_original_rel,agent_patched_rel;
static int agent_installed,agent_restored,agent_seeded,agent_full;
static uint64_t agent_calls,agent_failures;
static int params_enabled;
static unsigned params_done[ACTOR_CAPACITY],params_calls;
typedef void (*params_fn)(void *,void *,unsigned char);
static int params_once(params_fn original,unsigned actor,void *controller,void *destination) {
    if(!actor_is_ship(actor) || !controller || !destination) return 0;
    if(params_done[actor-1]) return 1;
    original(controller,destination,0);params_done[actor-1]=1;++params_calls;return 1;
}

static int agent_identity(unsigned index,void *self) {
    unsigned char *object=actor_components[index],*controller,*slots;uint64_t id;void *vtable;
    return object && read_local(object+8,&id,8) && id==actor_ids[index] &&
        read_local(object+0x200,&controller,8) && read_local(controller,&vtable,8) &&
        (vtable==image+0x2c51158 || pslot_vtable_is_copy((uintptr_t)vtable)) && /* a stock slot's ship keeps its Agent */
        read_local(controller+0x30,&slots,8) &&
        slots==(unsigned char*)self+0xad0;
}
static void agent_execute(agent_fn original,agent_sample *s,unsigned actor,uint64_t *rng,
                          void *self,void *commands,float dt,double time,unsigned char cheap) {
    uint64_t saved=rng?*rng:0;
    const int restore_rng=agent_seeded && rng;
    ++actor_all_agent_depth; /* nested agents continue this stream (native_gate_actor_rng_all) */
    __try {
        if(restore_rng) *rng=actor_seed(0x4147454e545f524eULL,stats.issued,actor,0,s->calls,0);
        s->rng_before=rng?*rng:0;
        request_agent_scope observed;
        request_agent_begin(&observed,actor,self);
        original(self,commands,dt,time,agent_full?0:cheap);
        request_agent_end(&observed);
        s->rng_after=rng?*rng:0;
    } __finally {
        DWORD error=GetLastError();
        --actor_all_agent_depth;
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64((volatile LONG64*)&agent_failures);
        SetLastError(error);
    }
}
static void agent_update(void *self,void *commands,float dt,double time,unsigned char cheap) {
    registry_observe_owner(self,0xef0,REGISTRY_AGENT);
    agent_fn original=(agent_fn)(image+AGENT_ORIGINAL);agent_sample *s=NULL;unsigned actor=0;
    if(stats.active && boot_phase==3) for(unsigned i=0;i<ACTOR_CAPACITY;i++) {
        if(agent_samples[i].agent==self && agent_identity(i,self)) {s=&agent_samples[i];actor=i+1;break;}
    }
    if(!s) {
        /* An unregistered nested Agent must shadow a registered parent's input
         * provenance even though it produces no saved-fixture scope record. */
        request_agent_scope excluded;request_agent_begin(&excluded,0,self);
        world_trace_event *event=world_trace_begin(WT_AGENT,self,NULL,dt,time,cheap,0);
        if(stats.active && boot_phase==3 && actor_all_on()) actor_all_agent_execute(original,self,commands,dt,time,cheap,actor_all_tls_rng());
        else original(self,commands,dt,time,cheap);
        world_trace_end(event);request_agent_end(&excluded);return;
    }
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=s->cheap=s->applied_cheap=0;}
    ++s->calls;s->cheap+=cheap!=0;s->applied_cheap+=!agent_full && cheap!=0;
    memcpy(&s->dt,&dt,4);memcpy(&s->time,&time,8);
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    uint64_t *rng=tls && tls[0]?(uint64_t*)((unsigned char*)tls[0]+0x318):NULL;
    if(rng) {memcpy(&s->tls_time,(unsigned char*)tls[0]+0x300,8);memcpy(&s->tls_view,(unsigned char*)tls[0]+0x314,4);}
    memcpy(s->limits_before,(unsigned char*)self+0x340,sizeof(s->limits_before));
    if(!rng) InterlockedIncrement64((volatile LONG64*)&agent_failures);
    agent_execute(original,s,actor,rng,self,commands,dt,time,cheap);
    memcpy(s->limits_after,(unsigned char*)self+0x340,sizeof(s->limits_after));
    InterlockedIncrement64((volatile LONG64*)&agent_calls);
}
static int agent_swap(int32_t expected,int32_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+AGENT_REL,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    LONG actual=InterlockedCompareExchange((volatile LONG*)(image+AGENT_REL),(LONG)value,(LONG)expected);
    VirtualProtect(image+AGENT_REL,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+AGENT_REL,4);
    return actual==expected;
}
static int agent_install(int seed,int full) {
    if(!boot_mode || GetModuleHandleW(L"x4lan_fixedtick_64.dll") ||
       !hash_ok(image+0xeaed50,48,"b7217cf64eecbc09931e473bd1ad2eecc9fb32c6fcf5ed940484233f954a06b2") ||
       !hash_ok(image+AGENT_ORIGINAL,32,"2b6565dd2dcb1f98be975bb39d5722950f962be5c9d21edd7de343910ee27405")) return 0;
    unsigned char *stub=thunk+256;
    intptr_t distance=(intptr_t)stub-(intptr_t)(image+AGENT_REL+4);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(256)) return 0;
    memcpy(&agent_original_rel,image+AGENT_REL,4);agent_patched_rel=(int32_t)distance;
    if(!agent_swap(agent_original_rel,agent_patched_rel)) return 0;
    agent_seeded=seed;agent_full=full;agent_installed=1;return 1;
}
static void agent_restore(void) {
    if(!agent_installed) return;
    agent_restored=agent_swap(agent_patched_rel,agent_original_rel);
    if(agent_restored) agent_installed=0;
}
