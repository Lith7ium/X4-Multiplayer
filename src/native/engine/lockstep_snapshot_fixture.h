/* Explicit, bounded snapshot fixture enrollment. Imported IDs select objects;
 * completed native type/macro/owner relationships are checked before binding.
 * This is not a whole-world identity or state normalization mechanism. */
typedef void *(*snapshot_find_fn)(void *,uint64_t,unsigned);
typedef struct {
    uint64_t saved,runtime;void *object;
    unsigned imports,enrolled;nav_sample first_nav;
} snapshot_fixture_actor;
static snapshot_fixture_actor snapshot_actors[12];
static SRWLOCK snapshot_lock=SRWLOCK_INIT;
static unsigned snapshot_configured,snapshot_bound,snapshot_prepared,snapshot_enrolled;
static uint64_t snapshot_bound_tick,snapshot_prepared_tick;
static world_bank_phase snapshot_bound_banks,snapshot_prepared_banks;
static volatile LONG64 snapshot_failures;
static const char *snapshot_prepare_stage="none";
static uint64_t snapshot_attempt_tick,snapshot_attempt_time;
static unsigned snapshot_attempt_actor,snapshot_attempt_ui,snapshot_attempt_main,snapshot_attempt_view;
static unsigned snapshot_attempt_model;
static const char *snapshot_flight_kinds[12];

static int snapshot_manifest(const char *text,size_t length) {
    uint64_t ids[12];
    if(!text || length!=12*16+11) return 0;
    for(unsigned i=0;i<12;i++) {
        uint64_t id=0;
        for(unsigned j=0;j<16;j++) {
            char c=text[i*17+j];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;
            if(d>15) return 0;id=(id<<4)|d;
        }
        if(!id || (i<11 && text[i*17+16]!=',')) return 0;
        for(unsigned j=0;j<i;j++) if(ids[j]==id) return 0;
        ids[i]=id;
    }
    if(snapshot_configured) {
        for(unsigned i=0;i<12;i++) if(snapshot_actors[i].saved!=ids[i]) return 0;
        return 1; /* Exact UI-reload retry; no state or engine API calls. */
    }
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !path_installed || !(path_options&1)) return 0;
    for(unsigned i=0;i<12;i++) snapshot_actors[i].saved=ids[i];
    snapshot_configured=1;return 1;
}
static void snapshot_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    if(!snapshot_configured) return;
    for(unsigned i=0;i<12;i++) if(snapshot_actors[i].saved==saved) {
        AcquireSRWLockExclusive(&snapshot_lock);
        snapshot_fixture_actor *a=&snapshot_actors[i];
        if(a->imports || !object || !runtime || snapshot_bound) InterlockedIncrement64(&snapshot_failures);
        else {a->object=object;a->runtime=runtime;a->imports=1;}
        ReleaseSRWLockExclusive(&snapshot_lock);return;
    }
}
static int snapshot_native_actor(unsigned index,snapshot_find_fn find,void *map,unsigned char **slots_out) {
    snapshot_fixture_actor *a=&snapshot_actors[index];unsigned char *object=a->object,*macro,*controller,*slots,*parameters;
    void *vtable,*owner,*getter;uint64_t id,length,capacity;unsigned char *name;char text[64];
    const char *expected=index<6?"ship_arg_s_fighter_01_a_macro":"ship_xen_s_fighter_01_a_macro";
    if(!find || !map || a->imports!=1 || !a->runtime || !object || find(map,a->runtime,4)!=object ||
       world_identity_lookup(a->runtime)!=a->saved || !read_local(object+8,&id,8) || id!=a->runtime ||
       !read_local(object,&vtable,8) || vtable!=image+0x2b7feb8 ||
       !read_local(object+0x60,&macro,8) || !macro || !read_local(macro,&vtable,8) || vtable!=image+0x2ba5430 ||
       !read_local((unsigned char*)vtable+18*8,&getter,8) || getter!=image+0xefb10 ||
       !read_local(macro+0x30,&length,8) || length!=strlen(expected) || length>=sizeof(text) ||
       !read_local(macro+0x38,&capacity,8) || capacity<length) return 0;
    name=macro+0x20;
    if(capacity>=16 && (!read_local(name,&name,8) || !name)) return 0;
    if(!read_local(name,text,(size_t)length+1) || memcmp(text,expected,(size_t)length+1) ||
       !read_local(object+0x200,&controller,8) || !controller || !read_local(controller,&vtable,8) || vtable!=image+0x2c51158 ||
       !read_local(controller+0x28,&owner,8) || owner!=object ||
       !read_local(controller+0x30,&slots,8) || !slots || (uintptr_t)slots<0xad0 ||
       !read_local(controller+0x40,&parameters,8) || parameters!=slots-0xad0 ||
       !read_local(parameters+0xef0,&owner,8) || owner!=object) return 0;
    *slots_out=slots;return 1;
}
static int snapshot_bind(snapshot_find_fn find,void *map) {
    if(!snapshot_configured) return 1;
    if(snapshot_bound || snapshot_failures || stats.issued || boot_phase!=3 || !map) return 0;
    if(!world_banks_read(&snapshot_bound_banks)) return 0;
    unsigned char *slots[12];
    for(unsigned i=0;i<12;i++) {
        if(actor_components[i] || actor_ids[i] || !snapshot_native_actor(i,find,map,&slots[i])) return 0;
        for(unsigned j=0;j<i;j++) if(snapshot_actors[i].object==snapshot_actors[j].object || snapshot_actors[i].runtime==snapshot_actors[j].runtime) return 0;
    }
    /* Only our registration tables change on the main thread. Engine-mutating
     * preparations wait for the first simulation/UI path-release boundary. */
    for(unsigned i=0;i<12;i++) {
        actor_components[i]=snapshot_actors[i].object;actor_ids[i]=snapshot_actors[i].runtime;
        if(agent_installed) agent_samples[i].agent=slots[i]-0xad0;
        if(flight_installed) flight_samples[i].agent=slots[i]-0xad0;
    }
    snapshot_bound=12;snapshot_bound_tick=stats.issued;return 1;
}
static int snapshot_flight_register(unsigned actor,unsigned char *slots) {
    if(actor<1 || actor>12 || !slots) return 0;
    unsigned i=actor-1;void *model,*vtable,*owner;unsigned char *agent=slots-0xad0;
    if(!read_local(agent+0xf10,&model,8)) return 0;
    snapshot_attempt_model=0;
    if(!model) {flight_samples[i].model=NULL;snapshot_flight_kinds[i]="none";return 1;}
    if(!read_local(model,&vtable,8) || !read_local((unsigned char*)model+0x10,&owner,8) || owner!=agent) return 0;
    if((unsigned char*)vtable>=image && (unsigned char*)vtable<image+0x6fa9000) snapshot_attempt_model=(unsigned)((unsigned char*)vtable-image);
    if(vtable==image+SPLINE_ENTRY-0x70) {
        uint32_t state[4];if(!spline_state(model,agent,state)) return 0;
        /* The snapshot can restore a spline model. It is already covered by
         * the spline hook; a later steering construction uses its existing
         * constructor hook. Do not replace or initialize another model type. */
        flight_samples[i].model=NULL;snapshot_flight_kinds[i]="spline";return 1;
    }
    if(vtable!=image+FLIGHT_ENTRY-0x70) return 0;
    snapshot_flight_kinds[i]="steering";return flight_register(actor,slots);
}
static int snapshot_prepare(void) {
    if(!snapshot_configured || snapshot_prepared) return 1;
    snapshot_prepare_stage="guard";snapshot_attempt_tick=stats.issued;
    snapshot_attempt_ui=GetCurrentThreadId()==ui_thread;snapshot_attempt_main=GetCurrentThreadId()==main_thread;
    if(snapshot_bound!=12 || snapshot_failures || stats.issued!=1 || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_banks_read(&snapshot_prepared_banks)) return 0;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    snapshot_prepare_stage="tls";
    if(tls && tls[0]) {snapshot_attempt_view=*((unsigned char*)tls[0]+0x314);read_local((unsigned char*)tls[0]+0x300,&snapshot_attempt_time,8);}
    if(!tls || !tls[0] || *((unsigned char*)tls[0]+0x314)!=1 ||
       !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time)) return 0;
    void *map=*(void**)(image+0x6cf1500);snapshot_find_fn find=(snapshot_find_fn)(image+0xced70);
    unsigned char *slots[12];
    snapshot_prepare_stage="identity";
    for(unsigned i=0;i<12;i++) {snapshot_attempt_actor=i+1;if(!snapshot_native_actor(i,find,map,&slots[i])) return 0;}
    snapshot_prepare_stage="radar_wait";snapshot_attempt_actor=0;
    if(gravidar_initial && !gravidar_wait(*(void**)(image+0x6cf1650),1000)) return 0;
    for(unsigned i=0;i<12;i++) {
        snapshot_attempt_actor=i+1;snapshot_prepare_stage="radar_registration";
        if(gravidar_initial && !gravidar_register(i+1)) return 0;
        snapshot_prepare_stage="parameters";
        if(params_enabled) {
            void *controller;unsigned write_index=*(unsigned*)(image+0x2f4b6f4);
            if(write_index>1 || !read_local((unsigned char*)actor_components[i]+0x200,&controller,8) ||
               !agent_identity(i,slots[i]-0xad0) || !params_once((params_fn)(image+0xea1e80),i+1,controller,slots[i]-0xad0+write_index*0x1a0)) return 0;
        }
        snapshot_prepare_stage="steering";
        if(flight_installed && !snapshot_flight_register(i+1,slots[i])) return 0;
    }
    snapshot_prepared=12;snapshot_prepared_tick=stats.issued;snapshot_prepare_stage="complete";snapshot_attempt_actor=0;return 1;
}
static void snapshot_nav_observe(unsigned actor,const void *sample) {
    if(snapshot_configured && actor>=1 && actor<=12 && !snapshot_actors[actor-1].first_nav.calls)
        snapshot_actors[actor-1].first_nav=*(const nav_sample*)sample;
}
static int snapshot_enroll(unsigned actor,uint64_t runtime) {
    if(actor<1 || actor>12 || snapshot_bound!=12 || snapshot_prepared!=12 || snapshot_failures) return 0;
    snapshot_fixture_actor *a=&snapshot_actors[actor-1];uint64_t current;
    if(runtime!=a->runtime || actor_components[actor-1]!=a->object || actor_ids[actor-1]!=runtime ||
       !read_local((unsigned char*)a->object+8,&current,8) || current!=runtime || world_identity_lookup(runtime)!=a->saved) return 0;
    if(!a->enrolled) {a->enrolled=1;++snapshot_enrolled;}
    return 1;
}
