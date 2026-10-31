/* Pending creation evidence is read-only. A callback that follows any observed
 * native update of this exact lifetime is refused before initialization. */
#define REGISTRY_PENDING_CAPACITY 256u
typedef struct {
    uint64_t tick,pc,caller,type,time,script_hash,order;
    unsigned recorded,reads,depth,ui,view,thread;
    char script[96],operation[32];
} registry_pending_ai;
typedef struct {void *object;uint64_t runtime,tick,order;unsigned kind,thread;registry_pending_ai ai;registry_pending_origin origin;} registry_pending_update;
static registry_pending_update registry_pending[REGISTRY_PENDING_CAPACITY];
static unsigned registry_pending_count,registry_preparing_actor;
static const char *registry_stage="none";
static char registry_identity_note[2048];
static nav_sample registry_first_nav[ACTOR_DYNAMIC_COUNT];
static void registry_nav_observe(unsigned actor,const void *sample) {
    if(actor_role(actor)==ACTOR_DYNAMIC_SHIP && !registry_first_nav[actor-ACTOR_DYNAMIC_FIRST].calls)
        registry_first_nav[actor-ACTOR_DYNAMIC_FIRST]=*(const nav_sample*)sample;
}

static int registry_string(void *storage,const char *expected) {
    uint64_t length,capacity;char text[64];void *data=storage;size_t expected_length=strlen(expected);
    if(!expected_length || expected_length>=sizeof(text) || !read_local((unsigned char*)storage+16,&length,8) ||
       length!=expected_length || !read_local((unsigned char*)storage+24,&capacity,8) || capacity<length ||
       (capacity>=16 && (!read_local(storage,&data,8) || !data)) || !read_local(data,text,expected_length+1)) return 0;
    return !memcmp(text,expected,expected_length+1);
}
static int registry_identity_check(int ok,const char **stage,const char *failure) {
    if(!ok && stage) *stage=failure;return ok;
}
/* Exact completed classes with zero-offset Ship/Object bases. Shared by the
 * pending observer and enrollment; transient/base or unknown classes refuse. */
static int registry_supported_ship_type(void *type) {return type==image+0x2b7feb8 || type==image+0x2b99a88;}
static int registry_macro_detail(void *object,const char *expected,const char **stage) {
    void *type,*macro,*getter;
    return registry_identity_check(read_local(object,&type,8) && registry_supported_ship_type(type),stage,"identity_object_type") &&
        registry_identity_check(read_local((unsigned char*)object+0x60,&macro,8) && macro,stage,"identity_macro_pointer") &&
        registry_identity_check(read_local(macro,&type,8) && type==image+0x2ba5430,stage,"identity_macro_type") &&
        registry_identity_check(read_local((unsigned char*)type+18*8,&getter,8) && getter==image+0xefb10,stage,"identity_macro_getter") &&
        registry_identity_check(registry_string((unsigned char*)macro+0x20,expected),stage,"identity_macro_name");
}
static int registry_macro(void *object,const char *expected) {return registry_macro_detail(object,expected,NULL);}
static int registry_owner_detail(void *object,const char *expected,const char **stage) {
    void *type,*getter,*faction;
    return registry_identity_check(read_local(object,&type,8) && registry_supported_ship_type(type),stage,"identity_owner_type") &&
        registry_identity_check(read_local((unsigned char*)type+0x1620,&getter,8) && getter==image+0x352b10,stage,"identity_owner_getter") &&
        registry_identity_check(read_local((unsigned char*)object+0x348,&faction,8) && faction,stage,"identity_owner_pointer") &&
        registry_identity_check(registry_string((unsigned char*)faction+0x10,expected),stage,"identity_owner_name");
}
static unsigned long long registry_local_rva(void *pointer) {
    uintptr_t value=(uintptr_t)pointer,base=(uintptr_t)image;
    return value>=base && value-base<0x6fa9000?(unsigned long long)(value-base):0;
}
typedef struct {uint64_t length,capacity;unsigned read;char text[64];} registry_string_note;
static registry_string_note registry_describe_string(void *storage) {
    registry_string_note note={0};void *data=storage;
    if(!storage || !read_local((unsigned char*)storage+16,&note.length,8) ||
       !read_local((unsigned char*)storage+24,&note.capacity,8)) return note;
    note.read=1;
    if(note.capacity<note.length || note.length>=sizeof(note.text) ||
       (note.capacity>=16 && (!read_local(storage,&data,8) || !data)) ||
       !read_local(data,note.text,(size_t)note.length+1)) return note;
    note.read=2;
    for(unsigned i=0;i<note.length;i++) if(!((note.text[i]>='a'&&note.text[i]<='z') ||
       (note.text[i]>='0'&&note.text[i]<='9') || note.text[i]=='_')) note.text[i]='.';
    note.text[note.length]=0;return note;
}
/* Refusal-only evidence. RVAs and bounded native names are diagnostic, never
 * identity/seed/hash inputs. No type or relationship is accepted from this read. */
static void registry_describe_identity(void *object,uint64_t runtime) {
    unsigned char *o=object,*macro=NULL,*faction=NULL,*controller=NULL,*slots=NULL,*agent=NULL;
    void *type=NULL,*mt=NULL,*mg=NULL,*og=NULL,*ct=NULL,*co=NULL,*ao=NULL;uint64_t actual=0,tick=0;unsigned kind=0;
    unsigned reads=0;
    if(o) {
        reads|=(unsigned)read_local(o,&type,8);
        reads|=(unsigned)read_local(o+8,&actual,8)<<1;
        reads|=(unsigned)read_local(o+0x60,&macro,8)<<2;
        reads|=(unsigned)read_local(o+0x348,&faction,8)<<3;
        reads|=(unsigned)read_local(o+0x200,&controller,8)<<4;
    }
    if(type) reads|=(unsigned)read_local((unsigned char*)type+0x1620,&og,8)<<5;
    if(macro) reads|=(unsigned)read_local(macro,&mt,8)<<6;
    if(mt) reads|=(unsigned)read_local((unsigned char*)mt+18*8,&mg,8)<<7;
    if(controller) {
        reads|=(unsigned)read_local(controller,&ct,8)<<8;
        reads|=(unsigned)read_local(controller+0x28,&co,8)<<9;
        reads|=(unsigned)read_local(controller+0x30,&slots,8)<<10;
        reads|=(unsigned)read_local(controller+0x40,&agent,8)<<11;
    }
    if(agent) reads|=(unsigned)read_local(agent+0xef0,&ao,8)<<12;
    registry_string_note mn=registry_describe_string(macro?macro+0x20:NULL),on=registry_describe_string(faction?faction+0x10:NULL);
    uint64_t order=0;unsigned thread=0;registry_pending_ai ai={0};AcquireSRWLockShared(&registry_lock);
    for(unsigned i=0;i<registry_pending_count;i++) if(registry_pending[i].runtime==runtime) {
        tick=registry_pending[i].tick;kind=registry_pending[i].kind;ai=registry_pending[i].ai;
        order=registry_pending[i].order;thread=registry_pending[i].thread;break;
    }
    ReleaseSRWLockShared(&registry_lock);
    int length=snprintf(registry_identity_note,sizeof(registry_identity_note),
        " lookup=%u runtime_equal=%u imported=%u reads=%x object_type=%llx macro_type=%llx macro_getter=%llx"
        " macro_length=%llu macro_capacity=%llu macro_read=%u macro_name=%s owner_getter=%llx"
        " owner_length=%llu owner_capacity=%llu owner_read=%u owner_name=%s controller_type=%llx"
        " controller_owner=%u slots=%u parameters=%u agent_owner=%u pending_tick=%llu pending_kind=%u pending_ai=%u pending_order=%llu pending_thread=%u",
        object!=NULL,object && actual==runtime,world_identity_lookup(runtime)!=0,reads,registry_local_rva(type),registry_local_rva(mt),registry_local_rva(mg),
        mn.length,mn.capacity,mn.read,mn.read==2?mn.text:"unread",registry_local_rva(og),
        on.length,on.capacity,on.read,on.read==2?on.text:"unread",registry_local_rva(ct),
        object && co==object,slots!=NULL,slots && (uintptr_t)slots>=0xad0 && agent==slots-0xad0,object && ao==object,tick,kind,ai.recorded,order,thread);
    if(ai.recorded && length>0 && (size_t)length<sizeof(registry_identity_note))
        snprintf(registry_identity_note+length,sizeof(registry_identity_note)-(size_t)length,
            " ai_tick=%llu ai_stage=unbound ai_operation=%s ai_depth=%u ai_class=%llx ai_caller=%llx ai_pc=%llx"
            " ai_ui=%u ai_view=%u ai_time=%016llx ai_script_hash=%016llx ai_reads=%x ai_script=%s ai_order=%llu ai_thread=%u",
            ai.tick,ai.operation,ai.depth,ai.type,ai.caller,ai.pc,ai.ui,ai.view,ai.time,ai.script_hash,ai.reads,ai.script,ai.order,ai.thread);
}
static int registry_has_pending(void) {
    for(unsigned i=0;i<dynamic_ship_count;i++) if(!dynamic_ships[i].bound_tick) return 1;
    return 0;
}
static void registry_record_update(void *object,uint64_t runtime,unsigned kind) {
    /* Every selected observation is retained before the legacy first-only
     * deduplication. The additive observer owns no legacy arrays or records. */
    registry_creation_observe(object,runtime,kind);
    AcquireSRWLockExclusive(&registry_lock);
    for(unsigned i=0;i<dynamic_ship_count;i++) {
        unsigned actor=ACTOR_DYNAMIC_FIRST+i;dynamic_ship *s=&dynamic_ships[i];
        if(actor_ids[actor-1]==runtime && (!object || actor_components[actor-1]==object)) {
            if(registry_preparing_actor==actor && GetCurrentThreadId()==ui_thread && kind==REGISTRY_PARAMS) {
                ReleaseSRWLockExclusive(&registry_lock);return;
            }
            if(!s->first_update_tick) {s->first_update_tick=stats.issued;s->first_update_kind=kind;}
            if(!s->prepared_tick) {++s->late_updates;InterlockedIncrement64(&registry_failures);}
            ReleaseSRWLockExclusive(&registry_lock);return;
        }
    }
    if(!registry_has_pending()) {ReleaseSRWLockExclusive(&registry_lock);return;}
    for(unsigned i=0;i<registry_pending_count;i++) if(registry_pending[i].runtime==runtime) {
        if(object && registry_pending[i].object && registry_pending[i].object!=object) InterlockedIncrement64(&registry_failures);
        ReleaseSRWLockExclusive(&registry_lock);return;
    }
    if(registry_pending_count==REGISTRY_PENDING_CAPACITY) InterlockedIncrement64(&registry_failures);
    else {
        registry_pending_update *p=&registry_pending[registry_pending_count++];
        *p=(registry_pending_update){.object=object,.runtime=runtime,.tick=stats.issued,.kind=kind,
            .order=registry_md_next_order(),.thread=GetCurrentThreadId()};
        if(kind==REGISTRY_AI) registry_origin_capture(&p->origin,object,runtime);
    }
    ReleaseSRWLockExclusive(&registry_lock);
}
static int registry_update_format(unsigned index,char *out,size_t capacity) {
    registry_pending_update p;AcquireSRWLockShared(&registry_lock);
    int exists=index<registry_pending_count;if(exists) p=registry_pending[index];ReleaseSRWLockShared(&registry_lock);
    if(!exists) return 0;
    int n=snprintf(out,capacity,"schema=1 runtime=%016llx tick=%llu kind=%u order=%llu thread=%u object=%u ai_order=%llu ai_thread=%u",
        p.runtime,p.tick,p.kind,p.order,p.thread,p.object!=NULL,p.ai.order,p.ai.thread);
    return n>0 && (size_t)n<capacity?n:0;
}
static void registry_observe_object(void *object,unsigned kind) {
    if(!registry_is_observing() || !stats.active || boot_phase!=3 || !object) return;
    uint64_t runtime;if(!read_local((unsigned char*)object+8,&runtime,8) || !runtime) return;
    if(world_identity_lookup(runtime)) return;
    int observed=0;AcquireSRWLockShared(&registry_lock);
    for(unsigned i=0;i<dynamic_ship_count;i++) {
        if(actor_components[ACTOR_DYNAMIC_FIRST-1+i]==object && actor_ids[ACTOR_DYNAMIC_FIRST-1+i]==runtime) {observed=1;break;}
        if(!dynamic_ships[i].bound_tick && registry_macro(object,dynamic_ships[i].macro)) {observed=1;break;}
    }
    ReleaseSRWLockShared(&registry_lock);
    if(observed) registry_record_update(object,runtime,kind);
}
static void registry_observe_runtime(uint64_t runtime,unsigned kind) {
    if(!registry_is_observing() || !stats.active || boot_phase!=3 || !runtime || world_identity_lookup(runtime)) return;
    void *object=NULL;AcquireSRWLockShared(&registry_lock);int observed=registry_has_pending();
    for(unsigned i=0;i<dynamic_ship_count;i++) if(actor_ids[ACTOR_DYNAMIC_FIRST-1+i]==runtime) {
        object=actor_components[ACTOR_DYNAMIC_FIRST-1+i];observed=1;break;
    }
    ReleaseSRWLockShared(&registry_lock);
    if(observed) registry_record_update(object,runtime,kind);
}
static void registry_observe_owner(void *object,unsigned offset,unsigned kind) {
    if(!registry_is_observing() || !object) return;
    void *owner;if(read_local((unsigned char*)object+offset,&owner,8)) registry_observe_object(owner,kind);
}
static void registry_observe_chain(void *object,unsigned kind) {
    if(!registry_is_observing()) return;
    for(unsigned depth=0;object && depth<16;depth++) {
        registry_observe_object(object,kind);void *next;
        if(!read_local((unsigned char*)object+0x70,&next,8) || next==object) return;object=next;
    }
}
/* First attempted read wins even when a field is unsupported. This diagnostic
 * annotates an existing pending lifetime; it never creates a reservation,
 * registers an actor, changes RNG, or calls the original AI implementation. */
static registry_pending_ai registry_read_ai(void *exec,void *context,void *caller,void *object,unsigned depth,void *tls_slot) {
    registry_pending_ai note={0};void *type=NULL;unsigned char view=0;
    note.recorded=1;note.order=registry_md_next_order();note.tick=stats.issued;note.depth=depth;note.thread=GetCurrentThreadId();note.ui=note.thread==ui_thread;
    note.caller=registry_local_rva(caller);snprintf(note.operation,sizeof(note.operation),"%s",registry_stage);
    if(read_local(object,&type,8)) {note.reads|=1;note.type=registry_local_rva(type);}
    if(exec && read_local((unsigned char*)exec+0x18,&note.pc,8)) note.reads|=2;
    if(tls_slot) {
        if(read_local((unsigned char*)tls_slot+0x314,&view,1)) {note.reads|=4;note.view=view;}
        if(read_local((unsigned char*)tls_slot+0x300,&note.time,8)) note.reads|=8;
    }
    note.script_hash=context?ai_script(context,note.script):0;
    if(note.script_hash) note.reads|=16;else strcpy(note.script,"unsupported");
    return note;
}
static void registry_attach_ai(void *exec,void *context,void *caller,void *object,uint64_t runtime,unsigned depth,void *tls_slot) {
    AcquireSRWLockExclusive(&registry_lock);
    for(unsigned i=0;i<registry_pending_count;i++) {
        registry_pending_update *p=&registry_pending[i];
        if(p->runtime==runtime && (!p->object || p->object==object) && !p->ai.recorded) {
            p->ai=registry_read_ai(exec,context,caller,object,depth,tls_slot);break;
        }
    }
    ReleaseSRWLockExclusive(&registry_lock);
}
static void registry_observe_ai(void *exec,void *context,void *caller) {
    if(!registry_is_observing() || !stats.active || boot_phase!=3 || GetCurrentThreadId()!=ui_thread) return;
    AcquireSRWLockShared(&registry_lock);int pending=registry_has_pending();ReleaseSRWLockShared(&registry_lock);
    if(!pending) return;
    unsigned char *entity;void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58),*tls_slot=NULL;
    if(tls) read_local(tls,&tls_slot,8);
    if(!read_local((unsigned char*)context+0x120,&entity,8)) return;
    for(unsigned depth=0;entity && depth<16;depth++) {
        uint64_t runtime;if(!read_local(entity+8,&runtime,8)) return;
        registry_attach_ai(exec,context,caller,entity,runtime,depth,tls_slot);
        unsigned char *next;if(!read_local(entity+0x70,&next,8) || next==entity) return;entity=next;
    }
}
static int registry_native_ship(unsigned actor,uint64_t runtime,snapshot_find_fn find,void *map,void **out,unsigned char **slots_out) {
    registry_identity_note[0]=0;unsigned char *object=NULL,*controller,*slots,*agent;
    registry_stage="identity_arguments";
    if(actor_role(actor)!=ACTOR_DYNAMIC_SHIP || !runtime || !find || !map) goto refused;
    registry_stage="identity_imported";if(world_identity_lookup(runtime)) goto refused;
    dynamic_ship *s=&dynamic_ships[actor-ACTOR_DYNAMIC_FIRST];object=find(map,runtime,4);
    void *type,*owner;uint64_t actual;
    registry_stage="identity_lookup";if(!object) goto refused;
    registry_stage="identity_runtime";if(!read_local(object+8,&actual,8) || actual!=runtime) goto refused;
    if(!registry_macro_detail(object,s->macro,&registry_stage) || !registry_owner_detail(object,s->owner,&registry_stage)) goto refused;
    registry_stage="identity_controller_pointer";if(!read_local(object+0x200,&controller,8) || !controller) goto refused;
    registry_stage="identity_controller_type";if(!read_local(controller,&type,8) || type!=image+0x2c51158) goto refused;
    registry_stage="identity_controller_owner";if(!read_local(controller+0x28,&owner,8) || owner!=object) goto refused;
    registry_stage="identity_slots";if(!read_local(controller+0x30,&slots,8) || !slots || (uintptr_t)slots<0xad0) goto refused;
    registry_stage="identity_parameters";if(!read_local(controller+0x40,&agent,8) || agent!=slots-0xad0) goto refused;
    registry_stage="identity_agent_owner";if(!read_local(agent+0xef0,&owner,8) || owner!=object) goto refused;
    *out=object;*slots_out=slots;return 1;
refused:
    registry_describe_identity(object,runtime);return 0;
}
static int registry_late_update(dynamic_ship *s,uint64_t runtime) {
    int found=0;AcquireSRWLockExclusive(&registry_lock);
    for(unsigned i=0;i<registry_pending_count;i++) if(registry_pending[i].runtime==runtime) {
        s->late_updates=1;s->first_update_tick=registry_pending[i].tick;s->first_update_kind=registry_pending[i].kind;found=1;break;
    }
    ReleaseSRWLockExclusive(&registry_lock);return found;
}
static int registry_bind(unsigned actor,uint64_t runtime,snapshot_find_fn find,void *map) {
    void *object;unsigned char *slots;
    registry_stage="identity";
    if(registry_failures || !registry_native_ship(actor,runtime,find,map,&object,&slots)) return 0;
    registry_describe_identity(object,runtime); /* Retain actual class/layout for later boundary refusals too. */
    dynamic_ship *s=&dynamic_ships[actor-ACTOR_DYNAMIC_FIRST];unsigned index=actor-1;
    if(s->bound_tick) return s->prepared_tick && actor_components[index]==object && actor_ids[index]==runtime;
    registry_stage="boundary";
    if(!stats.active || !stats.issued || GetCurrentThreadId()!=ui_thread || stats.issued<s->reserved_tick || stats.issued>s->reserved_tick+1) return 0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(actor_components[i]==object || actor_ids[i]==runtime) return 0;
    if(registry_late_update(s,runtime)) {registry_stage="late_update";return 0;}
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);double time;
    if(!tls || !tls[0] || *((unsigned char*)tls[0]+0x314)!=1 ||
       !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time)) return 0;
    registry_stage="radar_wait";
    if(gravidar_initial && !gravidar_wait(*(void**)(image+0x6cf1650),1000)) return 0;
    AcquireSRWLockExclusive(&registry_lock);
    for(unsigned i=0;i<registry_pending_count;i++) if(registry_pending[i].runtime==runtime) {
        s->late_updates=1;s->first_update_tick=registry_pending[i].tick;s->first_update_kind=registry_pending[i].kind;registry_stage="late_update";
        ReleaseSRWLockExclusive(&registry_lock);return 0;
    }
    if(registry_failures) {ReleaseSRWLockExclusive(&registry_lock);return 0;}
    actor_components[index]=object;actor_ids[index]=runtime;s->bound_tick=stats.issued;
    agent_samples[index].agent=slots-0xad0;flight_samples[index].agent=slots-0xad0;
    registry_preparing_actor=actor;ReleaseSRWLockExclusive(&registry_lock);int ok=1;
    registry_stage="radar_registration";if(gravidar_initial && !gravidar_register(actor)) ok=0;
    if(ok && params_enabled) {
        registry_stage="parameters";
        void *controller;unsigned write_index=*(unsigned*)(image+0x2f4b6f4);
        ok=write_index<=1 && read_local((unsigned char*)object+0x200,&controller,8) && agent_identity(index,slots-0xad0) &&
            params_once((params_fn)(image+0xea1e80),actor,controller,slots-0xad0+write_index*0x1a0);
    }
    if(ok && flight_installed) {
        registry_stage="steering";
        void *model,*type,*owner;unsigned char *agent=slots-0xad0;
        ok=read_local(agent+0xf10,&model,8);
        if(ok && model) {
            ok=read_local(model,&type,8) && read_local((unsigned char*)model+0x10,&owner,8) && owner==agent;
            if(ok && type==image+SPLINE_ENTRY-0x70) {uint32_t values[4];ok=spline_state(model,agent,values);}
            else if(ok) ok=type==image+FLIGHT_ENTRY-0x70 && flight_register(actor,slots);
        }
    }
    AcquireSRWLockExclusive(&registry_lock);registry_preparing_actor=0;
    ok=ok && !registry_failures && !s->late_updates;
    if(ok) {s->prepared_tick=stats.issued;registry_stage="complete";}
    ReleaseSRWLockExclusive(&registry_lock);return ok;
}
static int registry_runtime_text(const char *text,size_t length,uint64_t *out) {
    uint64_t value=0;if(!text || !length || length>20) return 0;
    for(size_t i=0;i<length;i++) {
        unsigned d=(unsigned)(text[i]-'0');if(d>9 || value>(UINT64_MAX-d)/10) return 0;value=value*10+d;
    }
    if(!value) return 0;*out=value;return 1;
}
static int registry_pins(void) {
    return hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36") &&
        hash_ok(image+0xefb10,5,"a8adead0a89e8bcc0f6079669194e35b31c483df478a1515e46c55a9cf74fb51") &&
        hash_ok(image+0x352b10,8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53") &&
        hash_ok(image+0xafdfd0,269,"fa051cf54d291f07522934fbe06d2d1b9d6275743fda9f85de430ba446232883");
}
static int registry_enabled(void) {
    return stats.active && boot_mode && boot_phase==3 && GetCurrentThreadId()==ui_thread &&
        world_identity_installed && !world_identity_failures && path_installed && (path_options&1) &&
        ship_installed && ship_owned_slots==3 && death_owned_slots==3 && nav_installed && agent_installed && flight_installed && spline_installed &&
        ai_installed && attention_installed && shape_installed && gravidar_installed && gravidar_unit_installed && params_installed &&
        velocity_installed && weapon_installed && damage_installed && death_installed && mass_installed && contact_installed &&
        nav_seeded && nav_initial_refresh && ship_seeded && agent_seeded && flight_seeded && gravidar_initial && params_enabled;
}
