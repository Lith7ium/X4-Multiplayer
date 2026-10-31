/* Read-only receipts inside selected early Actions. Engine-owned Values and
 * candidate vectors are borrowed, never retained, evaluated or refcounted. */
#define WORLD_VALUES_ASSIGN 0xcc6710u
#define WORLD_VALUES_CANDIDATE 0xd2ab50u
#define WORLD_VALUES_MAX 256u
#define WORLD_VALUES_CANDIDATES 128u
enum {WV_PARENT=1,WV_CALLER=2,WV_READ=4,WV_TYPE=8,WV_REFERENCE=16,
      WV_PHASE=32,WV_LIFETIME=64,WV_BOUND=128,WV_STOP=256,WV_NEST=512};
typedef uintptr_t (*world_values_fn)(void*,void*,void*,void*);
typedef int (*world_values_reader)(const void*,void*,size_t);
typedef struct {
    uintptr_t object;uint64_t runtime,saved;uint32_t type;unsigned char valid,pad[3];
} world_values_component;
_Static_assert(sizeof(world_values_component)==32,"candidate wire layout");
typedef struct {
    uintptr_t begin,end,capacity;uint64_t count;
    world_values_component items[WORLD_VALUES_CANDIDATES];
} world_values_candidates;
typedef struct {
    unsigned tag,kind;unsigned char raw[16],payload[16];uint64_t runtime,saved;
} world_values_value;
typedef struct {
    volatile LONG complete;unsigned kind,errors,thread,parent_action,parent_candidate,children;
    uint64_t enter,leave,caller,result,rng_before,rng_after,pc_before,pc_after;
    uintptr_t arguments[4];unsigned flags_before,flags_after;
    unsigned char declaration[0x22];
    world_values_value value;
    world_values_candidates before,after;
    world_values_component optional_before,optional_after;
    world_queue_phase_sample phase_before,phase_after;
} world_values_event;
static uintptr_t (*world_values_candidate_call)(world_values_fn,void*,void*,void*,void*,world_values_event*,world_action_event*,void*,void*(*)(void));
static world_values_event world_values_events[WORLD_VALUES_MAX];
static SRWLOCK world_values_lock=SRWLOCK_INIT;
static LONG64 world_values_calls,world_values_reserved,world_values_sequence,world_values_failures,world_values_dropped;
static LONG64 world_values_other_scope,world_values_other_type,world_values_open,world_values_peak,world_values_restore_open;
static unsigned world_values_exported,world_values_sites;
static int world_values_started,world_values_enabled,world_values_closing;
static uint64_t world_values_original[2],world_values_patched[2];
typedef struct {unsigned changed,ok;} world_values_swap_result;
typedef struct {DWORD protection;unsigned protection_pending,flush_pending;} world_values_patch_state;
static world_values_patch_state world_values_patch_states[2];
static LONG64 world_values_patch_failures;
static LONG world_values_patch_errors;
enum {WV_PATCH_OPEN=1,WV_PATCH_PROTECTION=2,WV_PATCH_FLUSH=4,WV_PATCH_FOREIGN=8,WV_PATCH_REPAIR_PROTECTION=16,WV_PATCH_REPAIR_FLUSH=32};
static _Thread_local world_values_event *world_values_candidate_current;

static int world_values_component_read(world_values_reader read,uintptr_t object,world_values_component *out) {
    memset(out,0,sizeof(*out));out->object=object;if(!object) return 1;
    uintptr_t type=0;uint64_t id=0;unsigned char valid=0;
    if(object<0x10000||object>UINTPTR_MAX-0xd1||!read((void*)object,&type,8)||type<(uintptr_t)image||type>=(uintptr_t)(image+0x6fa9000)||
       !read((void*)(object+8),&id,8)||!id||!read((void*)(object+0xd1),&valid,1)) return 0;
    out->runtime=id;out->saved=world_identity_lookup(id);out->type=(unsigned)(type-(uintptr_t)image);out->valid=valid;
    return out->saved!=0;
}
static int world_values_component_stable(world_values_reader read,uintptr_t object,world_values_component *out) {
    world_values_component again;
    return world_values_component_read(read,object,out)&&world_values_component_read(read,object,&again)&&!memcmp(out,&again,sizeof(again));
}
static int world_values_vector_read(world_values_reader read,void *header,world_values_candidates *out) {
    uintptr_t pointers[WORLD_VALUES_CANDIDATES],again[3];memset(out,0,sizeof(*out));
    if(!header||!read(header,&out->begin,24)||out->end<out->begin||out->capacity<out->end||
       ((out->end-out->begin)&7)||((out->capacity-out->begin)&7)||
       (out->capacity-out->begin)/8>WORLD_VALUES_CANDIDATES||(!out->begin&&(out->end||out->capacity))||
       (out->begin&&(out->begin<0x10000||(out->begin&7)))) return 0;
    out->count=(out->end-out->begin)/8;
    if(out->count&&!read((void*)out->begin,pointers,(size_t)out->count*8)) return 0;
    for(unsigned i=0;i<out->count;i++) if(!world_values_component_stable(read,pointers[i],&out->items[i])) return 0;
    return read(header,again,24)&&!memcmp(again,&out->begin,24);
}
static int world_values_vector_stable(world_values_reader read,void *header,world_values_candidates *out) {
    world_values_candidates again;
    return world_values_vector_read(read,header,out)&&world_values_vector_read(read,header,&again)&&!memcmp(out,&again,sizeof(again));
}
static int world_values_value_read(world_values_reader read,void *source,world_values_value *out) {
    memset(out,0,sizeof(*out));if(!source||!read(source,out->raw,16)) return 0;memcpy(&out->tag,out->raw,4);
    uintptr_t payload;memcpy(&payload,out->raw+8,8);
    if(out->tag<=9) {uint64_t numeric;if(!world_wait_numeric(out->raw,&numeric)) return 0;out->kind=1;}
    else if(out->tag==0xa) {
        out->kind=3;out->runtime=payload;out->saved=world_identity_lookup(payload);if(!payload||!out->saved) return 0;
    } else if(out->tag==0x63) {
        uint32_t references=0;
        if(!payload||payload>UINTPTR_MAX-0x30||!read((void*)payload,&references,4)||!references||
           !read((void*)(payload+0x20),out->payload,16)) return 0;
        for(unsigned i=0;i<3;i++) {float n;memcpy(&n,out->payload+i*4,4);if(!isfinite(n)) return 0;}out->kind=2;
    } else return 0;
    return 1;
}
static int world_values_value_stable(world_values_reader read,void *source,world_values_value *out) {
    world_values_value again;
    return world_values_value_read(read,source,out)&&world_values_value_read(read,source,&again)&&!memcmp(out,&again,sizeof(again));
}
static int world_values_parent_same(world_action_event *parent) {
    world_action_guard now;
    return parent&&world_action_current==parent&&!parent->errors&&parent->before.node&&
        world_action_guard_read((void*)parent->before.node,(void*)parent->before.exec,(void*)parent->before.frame,parent->before.index,&now)&&
        !memcmp(&now,&parent->before,sizeof(now));
}
static unsigned world_values_parent_type(const world_action_event *parent) {
    if(!parent) return 0;const world_action_guard *g=&parent->before;
    if(g->vtable==(uintptr_t)(image+0x2c0f3c8)&&g->action==(uintptr_t)(image+0xb87a30)) return 4;
    return parent->stage>=1&&parent->stage<=3?parent->stage:0;
}
static int world_values_admit(void) {
    AcquireSRWLockExclusive(&world_values_lock);int ok=world_values_enabled&&!world_values_closing&&stats.active;
    if(ok) {++world_values_calls;if(++world_values_open>world_values_peak) world_values_peak=world_values_open;}
    ReleaseSRWLockExclusive(&world_values_lock);return ok;
}
static world_values_event *world_values_begin(unsigned kind,void *a,void *b,void *c,void *d,uintptr_t caller,
                                             world_action_event *parent,void *tls,void *(*tls_reader)(void)) {
    if(!parent) {InterlockedIncrement64(&world_values_other_scope);return NULL;}
    unsigned type=world_values_parent_type(parent);if(!type||(kind==1&&type!=4)) {InterlockedIncrement64(&world_values_other_type);return NULL;}
    LONG64 index=InterlockedIncrement64(&world_values_reserved)-1;
    if(index>=WORLD_VALUES_MAX) {InterlockedIncrement64(&world_values_dropped);return NULL;}
    world_values_event *e=&world_values_events[index];e->kind=kind;e->thread=GetCurrentThreadId();
    e->parent_action=(unsigned)(parent-world_action_events)+1;e->enter=InterlockedIncrement64(&world_values_sequence);
    e->caller=caller>=(uintptr_t)image&&caller<(uintptr_t)(image+0x6fa9000)?caller-(uintptr_t)image:0;
    e->arguments[0]=(uintptr_t)a;e->arguments[1]=(uintptr_t)b;e->arguments[2]=(uintptr_t)c;e->arguments[3]=(uintptr_t)d;
    if(!world_values_parent_same(parent)) e->errors|=WV_PARENT;
    if(!world_action_sample(tls,tls_reader,&e->rng_before,(void*)parent->before.exec,&e->pc_before,&e->flags_before)) e->errors|=WV_READ;
    if(!world_action_phase(&e->phase_before,tls)) e->errors|=WV_PHASE;
    uintptr_t node=parent->before.node,frame=parent->before.frame;unsigned expected_offset=type==1?0x1b8:type==2?0xf8:type==3?0x28:0xf0;
    if((uintptr_t)a!=node+expected_offset||(uintptr_t)(kind?c:b)!=frame||(!kind&&(uintptr_t)c!=(uintptr_t)a+0x10)) e->errors|=WV_CALLER;
    if(kind) {if(e->caller!=0xb87ae8) e->errors|=WV_CALLER;}
    else {
        int known=(type==1&&e->caller==0xbe23af)||(type==2&&e->caller==0xbe0c8b)||
            (type==3&&(e->caller==0xc5b2ac||e->caller==0xc5b2c5||e->caller==0xc5ba03))||
            (type==4&&e->caller==0xd2ad67);
        if(!known) e->errors|=WV_CALLER;
    }
    return e;
}
static void world_values_end(world_values_event *e,world_action_event *parent,void *tls,void *(*tls_reader)(void)) {
    if(e) {
        if(!world_values_parent_same(parent)) e->errors|=WV_LIFETIME;
        if(!world_action_sample(tls,tls_reader,&e->rng_after,(void*)parent->before.exec,&e->pc_after,&e->flags_after)) e->errors|=WV_READ;
        if(!world_action_phase(&e->phase_after,tls)||e->phase_before.app_flags!=e->phase_after.app_flags||
           e->phase_before.vbi_loading!=e->phase_after.vbi_loading||e->phase_before.tls_time!=e->phase_after.tls_time||
           e->phase_before.game!=e->phase_after.game||e->phase_before.saved!=e->phase_after.saved) e->errors|=WV_PHASE;
        e->leave=InterlockedIncrement64(&world_values_sequence);
    }
    AcquireSRWLockExclusive(&world_values_lock);
    if(e) {if(world_values_closing) e->errors|=WV_STOP;if(e->errors) ++world_values_failures;InterlockedExchange(&e->complete,1);}
    if(world_values_open<=0) ++world_values_failures;else --world_values_open;
    ReleaseSRWLockExclusive(&world_values_lock);
}
static uintptr_t world_values_assign_observe(world_values_fn original,void *a,void *b,void *c,void *d,uintptr_t caller,
                                              world_values_reader read,void *(*tls_reader)(void)) {
    if(!world_values_admit()) return original(a,b,c,d);
    world_action_event *parent=world_action_current;void *tls=tls_reader();
    world_values_event *e=world_values_begin(0,a,b,c,d,caller,parent,tls,tls_reader);
    if(e) {
        if(!world_values_value_stable(read,d,&e->value)) e->errors|=WV_TYPE;
        if(world_values_parent_type(parent)==4) {
            world_values_event *candidate=world_values_candidate_current;
            if(!candidate||candidate->errors||candidate->parent_action!=e->parent_action||candidate->arguments[0]!=(uintptr_t)a||
               candidate->arguments[2]!=(uintptr_t)b) e->errors|=WV_NEST;
            else {
                e->parent_candidate=(unsigned)(candidate-world_values_events)+1;++candidate->children;
                int found=0;for(unsigned i=0;i<candidate->before.count;i++) if(candidate->before.items[i].runtime==e->value.runtime&&
                    candidate->before.items[i].saved==e->value.saved&&e->value.runtime) found=1;
                if(e->value.kind!=3||!found) e->errors|=WV_REFERENCE;
            }
        }
    }
    uintptr_t result=original(a,b,c,d); /* Full RAX preserved for every native caller. */
    if(e) e->result=result; /* Never read the borrowed Value after original. */
    world_values_end(e,parent,tls,tls_reader);return result;
}
static uintptr_t world_values_candidate_observe(world_values_fn original,void *a,void *b,void *c,void *d,uintptr_t caller,
                                                 world_values_reader read,void *(*tls_reader)(void)) {
    if(!world_values_admit()) return original(a,b,c,d);
    world_values_event *previous=world_values_candidate_current;world_values_candidate_current=NULL;
    world_action_event *parent=world_action_current;void *tls=tls_reader();
    world_values_event *e=world_values_begin(1,a,b,c,d,caller,parent,tls,tls_reader);
    if(e) {
        unsigned char again[sizeof(e->declaration)];
        if(!read(a,e->declaration,sizeof(e->declaration))||!read(a,again,sizeof(again))||memcmp(again,e->declaration,sizeof(again))) e->errors|=WV_READ;
        if(!world_values_vector_stable(read,b,&e->before)) e->errors|=WV_BOUND;
        if(!world_values_component_stable(read,(uintptr_t)d,&e->optional_before)) e->errors|=WV_REFERENCE;
        world_values_candidate_current=e;
    }
    uintptr_t result=world_values_candidate_call?
        world_values_candidate_call(original,a,b,c,d,e,parent,tls,tls_reader):original(a,b,c,d);
    if(e) {
        e->result=result;unsigned char again[sizeof(e->declaration)];
        if(!read(a,again,sizeof(again))||memcmp(again,e->declaration,sizeof(again))) e->errors|=WV_LIFETIME;
        if(!world_values_vector_stable(read,b,&e->after)||memcmp(&e->before,&e->after,sizeof(e->before))) e->errors|=WV_LIFETIME;
        if(!world_values_component_stable(read,(uintptr_t)d,&e->optional_after)||memcmp(&e->optional_before,&e->optional_after,sizeof(e->optional_before))) e->errors|=WV_LIFETIME;
        if(world_values_candidate_current!=e) e->errors|=WV_NEST;
    }
    world_values_candidate_current=previous;
    world_values_end(e,parent,tls,tls_reader);return result;
}
static uintptr_t world_values_assign(void *a,void *b,void *c,void *d) {
    return world_values_assign_observe((world_values_fn)(thunk+3584),a,b,c,d,(uintptr_t)__builtin_return_address(0),read_local,world_queue_tls);
}
static uintptr_t world_values_candidate(void *a,void *b,void *c,void *d) {
    return world_values_candidate_observe((world_values_fn)(thunk+3648),a,b,c,d,(uintptr_t)__builtin_return_address(0),read_local,world_queue_tls);
}
static unsigned world_values_ready(void) {
    unsigned limit=world_values_reserved>WORLD_VALUES_MAX?WORLD_VALUES_MAX:(unsigned)world_values_reserved,ready=0;
    while(ready<limit&&InterlockedCompareExchange(&world_values_events[ready].complete,0,0)) ++ready;return ready;
}
