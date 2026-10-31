/* Default-off observations of manager ownership transfer and Agent append.
 * No command/native state is modified. An enqueue's command belongs to the
 * consumer after original(); do not read it, even for validation, on return. */
#define WORLD_QUEUE_MAX 512u
#define WORLD_QUEUE_ENQUEUE 0xeacd40u
#define WORLD_QUEUE_APPEND 0xe89e40u
#define WORLD_QUEUE_DISPATCH 0xeacf60u
#define WORLD_QUEUE_ORDER_VTABLE 0x2c513d8u
#define WORLD_QUEUE_ABORT_VTABLE 0x2c513b8u
#define WORLD_QUEUE_ROLL_VTABLE 0x2c51538u
#define WORLD_QUEUE_ROTATION_VTABLE 0x2c51518u
#define WORLD_QUEUE_POSITION_VTABLE 0x2c514f8u
#define WORLD_QUEUE_MODEL_VTABLE 0x2c51c38u
typedef void (*world_queue_enqueue_fn)(void *,uint64_t,void *);
typedef void (*world_queue_append_fn)(void *,void *);
typedef void (*world_queue_dispatch_fn)(void *,void *);
#include "lockstep_world_queue_phase.h"
typedef struct {uintptr_t exec,value,context;uint64_t lifetime;unsigned tag;} world_queue_guard;
typedef struct {
    volatile LONG complete;
    unsigned kind,errors,thread,ui,boot,view,caller,order_type,payload_type,source_status,source_tag,name_status;
    uint64_t tick,saved,related,reference,enter,leave,enqueue,trace_event,rng,time,source_saved,source_pc;
    uintptr_t command,record,raw_caller;
    uint64_t dispatch_parent,order_value;
    unsigned order_size,caller_source,receipt_index,receipt_valid;
    unsigned char command_header[16],flags_before[13],flags_after[13];
    world_queue_guard source;
    world_queue_phase_sample phase;
    char name[96];unsigned char payload[WORLD_TRACE_COMMAND_BYTES];
} world_queue_event;
/* Optional read-only consumer receives a stack copy, never a mutable ledger
 * entry or permission to retain/dereference a transferred native command. */
static void *(*world_queue_model_begin)(const world_queue_event *,void *);
static void (*world_queue_model_end)(void *,const world_queue_event *,void *);
/* Independent transition schema2 observers. They never alter this raw ledger
 * or call original. Enqueue end gets no transferred command pointer. */
static void *(*world_queue_transition_enqueue_begin)(void *,uint64_t,void *,uintptr_t,void *);
static void (*world_queue_transition_enqueue_end)(void *);
static void *(*world_queue_transition_dispatch_begin)(void *,void *,uintptr_t,void *);
static void (*world_queue_transition_dispatch_end)(void *,void *,void *);
typedef struct {uintptr_t command,record;uint64_t runtime;unsigned event,state;} world_queue_lifetime;
static world_queue_event world_queue_events[WORLD_QUEUE_MAX];
static void *world_queue_receipt_begin(world_queue_event *e,void *agent) {
    if(!e || !world_queue_model_begin) return NULL;
    world_queue_event copy=*e;copy.receipt_index=(unsigned)(e-world_queue_events)+1;
    return world_queue_model_begin(&copy,agent);
}
static world_queue_lifetime world_queue_lifetimes[WORLD_QUEUE_MAX];
static volatile LONG64 world_queue_count,world_queue_order,world_queue_failures,world_queue_dropped,world_queue_preimport_calls;
static LONG64 world_queue_open,world_queue_peak,world_queue_restore_open;
static int world_queue_restoring;
static SRWLOCK world_queue_lock=SRWLOCK_INIT;
static unsigned world_queue_exported,world_queue_sites,world_queue_started;
static volatile LONG world_queue_enabled;
static uint64_t world_queue_original[3],world_queue_patched[3];
typedef struct world_queue_dispatch_scope {
    struct world_queue_dispatch_scope *previous;
    world_queue_event *event;
    void *agent,*command;
    uint64_t runtime;
    unsigned token,thread,active,append_count;
} world_queue_dispatch_scope;
static DWORD world_queue_dispatch_tls=TLS_OUT_OF_INDEXES;
static RUNTIME_FUNCTION world_queue_forward_unwind;
static DWORD64 world_queue_forward_base;
/* The integrated NPC TLS guard proves only its current live ExecState. A false
 * result means unbracketed regardless of thread, native CURRENT, name or view. */
static int (*world_queue_owner_matches)(uintptr_t native_current)=npc_provenance_current_matches;
static const unsigned world_queue_entries[3]={WORLD_QUEUE_ENQUEUE,WORLD_QUEUE_APPEND,WORLD_QUEUE_DISPATCH};
static void *world_queue_tls(void);
static int world_queue_early_context(const world_queue_phase_sample *s,ULONGLONG now) {
    AcquireSRWLockShared(&world_queue_lock);
    int ok=world_queue_phase_verified && world_queue_enabled && !world_queue_restoring && stats.active &&
        world_trace_early && boot_mode && boot_phase==2 && !stats.issued && GetCurrentThreadId()==ui_thread &&
        now>=boot_started && now-boot_started<=120000 && s->status==1 && s->view==1;
    ReleaseSRWLockShared(&world_queue_lock);return ok;
}
/* Optional pure NPC ownership predicate. Root wires this after both headers.
 * A true result means bounded pre-admission eligibility, never "inside Load". */
static int __attribute__((unused)) world_queue_early_eligible(void) {
    world_queue_phase_sample phase;
    if(!world_queue_phase_verified || !world_queue_enabled || !stats.active || boot_phase!=2 || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !world_queue_phase_read(read_local,world_queue_tls(),&phase)) return 0;
    return world_queue_early_context(&phase,GetTickCount64());
}
static int world_queue_admit(void) {
    AcquireSRWLockExclusive(&world_queue_lock);
    int admitted=world_queue_enabled && !world_queue_restoring && stats.active && stats.issued<=world_trace_last_tick;
    if(admitted && ++world_queue_open>world_queue_peak) world_queue_peak=world_queue_open;
    ReleaseSRWLockExclusive(&world_queue_lock);return admitted;
}
static void world_queue_leave(void) {
    AcquireSRWLockExclusive(&world_queue_lock);
    if(world_queue_open<=0) InterlockedIncrement64(&world_queue_failures);else --world_queue_open;
    ReleaseSRWLockExclusive(&world_queue_lock);
}

static int world_queue_guard_read(uintptr_t exec,world_queue_guard *g) {
    memset(g,0,sizeof(*g));g->exec=exec;
    if(!exec || !read_local((void*)(exec+0x38),&g->tag,4) || !read_local((void*)(exec+0x40),&g->value,8)) return 0;
    unsigned offset=g->tag==0x66?0xa8:g->tag==0x5d?0xb0:0;
    if(!offset) return 1;
    if(g->value<offset) return 0;g->context=g->value-offset;
    return read_local((void*)(g->context+8),&g->lifetime,8) && g->lifetime;
}
static int world_queue_guard_same(const world_queue_guard *g) {
    world_queue_guard again;return world_queue_guard_read(g->exec,&again) && !memcmp(g,&again,sizeof(again));
}
static void world_queue_source(world_queue_event *e) {
    /* status0 absent,1 stable unbracketed,2 proven local bracket,3 unsupported
     * Value,4 unreadable,5 changed. MD naming remains explicitly unsupported. */
    uintptr_t current=0,again=0;uint64_t pc_again=0;
    if(!read_local(image+0x38f4c60,&current,8)) {e->source_status=4;return;}
    if(!current) return;
    if(!world_queue_guard_read(current,&e->source) || !read_local((void*)(current+0x18),&e->source_pc,8)) {e->source_status=4;return;}
    e->source_tag=e->source.tag;
    if(!e->source.context) {e->source_status=3;return;}
    e->source_status=1;
    if(e->source.tag==0x66) {
        uintptr_t entity=0,entity_again=0;uint64_t runtime=0,runtime_again=0;
        if(ai_script((void*)e->source.context,e->name) &&
           read_local((void*)(e->source.context+0x120),&entity,8) && entity &&
           read_local((void*)(entity+8),&runtime,8) && runtime &&
           read_local((void*)(e->source.context+0x120),&entity_again,8) && entity_again==entity &&
           read_local((void*)(entity+8),&runtime_again,8) && runtime_again==runtime) {
            e->source_saved=world_identity_lookup(runtime);e->name_status=e->source_saved?1:2;
        } else e->name_status=3;
    }
    if(world_queue_owner_matches && world_queue_owner_matches(current)) e->source_status=2;
    char name_again[96]={0};
    if(!read_local(image+0x38f4c60,&again,8) || again!=current || !world_queue_guard_same(&e->source) ||
       !read_local((void*)(current+0x18),&pc_again,8) || pc_again!=e->source_pc ||
       (e->source.tag==0x66 && e->name_status && e->name_status!=3 &&
        (!ai_script((void*)e->source.context,name_again) || strcmp(name_again,e->name))) ||
       (e->source_status==2 && !world_queue_owner_matches(current))) e->source_status=5;
}
static int world_queue_watch(uint64_t runtime) {
    int index=-1;unsigned imports=0;AcquireSRWLockShared(&world_trace_import_lock);
    for(unsigned i=0;i<world_trace_watch_count;i++) {
        imports+=world_trace_imports[i];
        if(runtime && runtime==world_trace_watches[i].runtime) index=(int)i;
    }
    ReleaseSRWLockShared(&world_trace_import_lock);
    if(imports<world_trace_watch_count) InterlockedIncrement64(&world_queue_preimport_calls);
    return index;
}
static world_queue_event *world_queue_begin(unsigned kind,unsigned index,uintptr_t caller,void *tls) {
    LONG64 ordinal=InterlockedIncrement64(&world_queue_count);
    if(ordinal>WORLD_QUEUE_MAX) {InterlockedIncrement64(&world_queue_dropped);return NULL;}
    world_queue_event *e=&world_queue_events[ordinal-1];e->kind=kind;e->saved=world_trace_watches[index].saved;
    e->raw_caller=caller;
    e->tick=stats.issued;e->boot=boot_phase;e->thread=GetCurrentThreadId();e->ui=e->thread==ui_thread;
    e->enter=(uint64_t)InterlockedIncrement64(&world_queue_order);
    if(caller>=(uintptr_t)image && caller<(uintptr_t)(image+0x6fa9000)) e->caller=(unsigned)(caller-(uintptr_t)image);
    else {e->errors|=1;e->caller_source=2;}
    if(!tls || !read_local((unsigned char*)tls+0x300,&e->time,8) ||
       !read_local((unsigned char*)tls+0x314,&e->view,4) || !read_local((unsigned char*)tls+0x318,&e->rng,8)) e->errors|=2;
    if(world_queue_phase_verified) {
        ULONGLONG now=GetTickCount64();
        if(!world_queue_phase_read(read_local,tls,&e->phase) || e->phase.tls_time!=e->time || e->phase.view!=e->view) e->errors|=128;
        e->phase.elapsed=now>=boot_started?now-boot_started:UINT64_MAX;e->phase.main=e->thread==main_thread;
        e->phase.eligible=world_queue_early_context(&e->phase,now);
    }
    world_queue_source(e);return e;
}
static void world_queue_end(world_queue_event *e) {
    if(!e) return;e->leave=(uint64_t)InterlockedIncrement64(&world_queue_order);
    if(e->errors) InterlockedIncrement64(&world_queue_failures);
    InterlockedExchange(&e->complete,1);
}
static int world_queue_payload(world_queue_event *e,const void *record) {
    unsigned char again[WORLD_TRACE_COMMAND_BYTES];uintptr_t vtable;uint64_t runtime;
    if(!read_local(record,e->payload,sizeof(e->payload)) || !read_local(record,again,sizeof(again)) ||
       memcmp(e->payload,again,sizeof(again))) return 0;
    memcpy(&vtable,e->payload,8);memcpy(&runtime,e->payload+0x10,8);
    if(vtable!=(uintptr_t)(image+0x2c4f578)) return 0;
    e->payload_type=(unsigned)(vtable-(uintptr_t)image);e->reference=runtime?world_identity_lookup(runtime):0;
    return !runtime || e->reference;
}
static int world_queue_command(world_queue_event *e,void *command) {
    uintptr_t vtable=0;uint64_t related=0,related_again=0;unsigned char again[16];
    if(!read_local(command,&vtable,8)) return 0;
    if(vtable>=(uintptr_t)image && vtable<(uintptr_t)(image+0x6fa9000)) e->order_type=(unsigned)(vtable-(uintptr_t)image);
    if(e->order_type!=WORLD_QUEUE_ORDER_VTABLE && e->order_type!=WORLD_QUEUE_ABORT_VTABLE &&
       e->order_type!=WORLD_QUEUE_ROLL_VTABLE && e->order_type!=WORLD_QUEUE_ROTATION_VTABLE &&
       e->order_type!=WORLD_QUEUE_POSITION_VTABLE && e->order_type!=WORLD_QUEUE_MODEL_VTABLE) return 0;
    e->command=(uintptr_t)command;e->order_size=e->order_type==WORLD_QUEUE_ORDER_VTABLE?0xb0:0x10;
    if(!read_local(command,e->command_header,16)) return 0;
    if(e->order_type==WORLD_QUEUE_ORDER_VTABLE) {
        e->record=e->command+0x10;
        if(!world_queue_payload(e,(void*)e->record) || !read_local((unsigned char*)command+0xa0,&related,8) ||
           !read_local((unsigned char*)command+0xa0,&related_again,8) || related_again!=related) return 0;
    } else if(e->order_type==WORLD_QUEUE_ABORT_VTABLE) memcpy(&related,e->command_header+8,8);
    else if(e->order_type==WORLD_QUEUE_MODEL_VTABLE) {
        uint32_t mode;memcpy(&mode,e->command_header+8,4);e->order_value=mode;if(mode>6) return 0;
    } else {e->order_value=e->command_header[8];if(e->order_value>1) return 0;}
    e->related=related?world_identity_lookup(related):0;
    return (!related || e->related) && !memcmp(e->command_header,&vtable,8) &&
        read_local(command,again,16) && !memcmp(e->command_header,again,16);
}
static int world_queue_command_same(const world_queue_event *a,const world_queue_event *b) {
    return a->order_type==b->order_type && a->order_size==b->order_size && a->order_value==b->order_value &&
        a->related==b->related && a->reference==b->reference && a->payload_type==b->payload_type &&
        !memcmp(a->command_header,b->command_header,16) && !memcmp(a->payload,b->payload,sizeof(a->payload));
}
static unsigned world_queue_transfer(world_queue_event *e,uint64_t runtime) {
    unsigned token=(unsigned)(e-world_queue_events)+1;AcquireSRWLockExclusive(&world_queue_lock);
    for(unsigned i=0;i<WORLD_QUEUE_MAX;i++) if(world_queue_lifetimes[i].state && world_queue_lifetimes[i].state!=3 &&
        world_queue_lifetimes[i].command==e->command) {ReleaseSRWLockExclusive(&world_queue_lock);e->errors|=16;return 0;}
    world_queue_lifetimes[token-1]=(world_queue_lifetime){e->command,e->record,runtime,token,1};
    ReleaseSRWLockExclusive(&world_queue_lock);return token;
}
static unsigned world_queue_claim(world_queue_event *e,uint64_t runtime) {
    unsigned token=0;AcquireSRWLockExclusive(&world_queue_lock);
    for(unsigned i=0;i<WORLD_QUEUE_MAX;i++) {
        world_queue_lifetime *l=&world_queue_lifetimes[i];
        if(l->command!=e->command || l->state==0 || l->state==3) continue;
        if(l->state!=1 || l->runtime!=runtime || token) {e->errors|=16;token=0;break;}
        token=l->event;
    }
    if(token) {world_queue_lifetime *l=&world_queue_lifetimes[token-1];l->state=2;e->command=l->command;}
    ReleaseSRWLockExclusive(&world_queue_lock);return token;
}
static int world_queue_receipt_token(const world_queue_event *e,uint64_t runtime,unsigned state) {
    if(!e->enqueue || e->enqueue>WORLD_QUEUE_MAX) return 0;
    AcquireSRWLockShared(&world_queue_lock);const world_queue_lifetime *l=&world_queue_lifetimes[e->enqueue-1];
    int same=l->event==e->enqueue && l->state==state && l->runtime==runtime &&
        l->command==e->command && l->record==e->record;
    ReleaseSRWLockShared(&world_queue_lock);return same;
}
static int world_queue_retire(const world_queue_event *e,uint64_t runtime) {
    if(!e->enqueue || e->enqueue>WORLD_QUEUE_MAX) return 0;AcquireSRWLockExclusive(&world_queue_lock);
    world_queue_lifetime *l=&world_queue_lifetimes[e->enqueue-1];
    int same=l->event==e->enqueue && l->state==2 && l->runtime==runtime && l->command==e->command && l->record==e->record;
    if(!same) InterlockedIncrement64(&world_queue_failures);else l->state=3;
    ReleaseSRWLockExclusive(&world_queue_lock);return same;
}
static void world_queue_enqueue_observe(world_queue_enqueue_fn original,void *manager,uint64_t runtime,void *command,uintptr_t caller,void *tls) {
    world_queue_event *e=NULL;int index=-1;
    int admitted=world_queue_admit();if(admitted) index=world_queue_watch(runtime);
    if(index>=0) {
        e=world_queue_begin(0,(unsigned)index,caller,tls);
        if(e) {
            uintptr_t expected=0;uint64_t identity=0;e->command=(uintptr_t)command;
            if(!read_local(world_trace_watches[index].object+8,&identity,8) || identity!=runtime ||
               !read_local(image+0x6cf21c8,&expected,8) || expected!=(uintptr_t)manager) e->errors|=4;
            else if(!world_queue_command(e,command)) e->errors|=8;
            /* This positive proof follows a successful typed copy and exact
             * manager/recipient guard; never infer it from an allowed-error
             * mask after a partial read left only order_type initialized. */
            if(!e->errors) {
                e->enqueue=world_queue_transfer(e,runtime);
                e->receipt_valid=world_queue_receipt_token(e,runtime,1);
            }
        }
    }
    /* Receipt precedes ownership transfer, even if consumer dispatches before
     * enqueue returns. The callback cannot read command on the return path. */
    world_queue_receipt_begin(e,NULL);
    void *secondary=world_queue_transition_enqueue_begin?world_queue_transition_enqueue_begin(manager,runtime,command,caller,tls):NULL;
    original(manager,runtime,command);
    if(world_queue_transition_enqueue_end) world_queue_transition_enqueue_end(secondary);
    /* The native queue owns command now. Only the recipient lifetime is read;
     * the command pointer/record are never dereferenced after this transfer. */
    if(e) {uint64_t identity=0;if(!read_local(world_trace_watches[index].object+8,&identity,8) || identity!=runtime) e->errors|=4;}
    world_queue_end(e);
    if(admitted) world_queue_leave();
}
static int world_queue_agent_watch(void *agent) {
    unsigned char *owner=NULL;uint64_t runtime=0;
    if(!agent || !read_local((unsigned char*)agent+0xef0,&owner,8) || !owner || !read_local(owner+8,&runtime,8)) return -1;
    int index=world_queue_watch(runtime);
    return index>=0 && owner==world_trace_watches[index].object?index:-1;
}
static int world_queue_flags(void *agent,unsigned char flags[13]) {
    unsigned char again[13];
    return read_local((unsigned char*)agent+0xf33,flags,1) && read_local((unsigned char*)agent+0xf70,flags+1,12) &&
        read_local((unsigned char*)agent+0xf33,again,1) && read_local((unsigned char*)agent+0xf70,again+1,12) && !memcmp(flags,again,13);
}
static world_queue_dispatch_scope *world_queue_append_scope(void *agent,void *record,uintptr_t caller) {
    world_queue_dispatch_scope *s=world_queue_dispatch_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(world_queue_dispatch_tls);
    if(!s || !s->active || !s->event || s->thread!=GetCurrentThreadId() || s->agent!=agent ||
       s->event->order_type!=WORLD_QUEUE_ORDER_VTABLE || s->event->record!=(uintptr_t)record ||
       s->event->command!=(uintptr_t)s->command || !s->token || caller!=(uintptr_t)(thunk+2698)) return NULL;
    AcquireSRWLockShared(&world_queue_lock);world_queue_lifetime *l=&world_queue_lifetimes[s->token-1];
    int same=l->state==2 && l->event==s->token && l->runtime==s->runtime &&
        l->command==(uintptr_t)s->command && l->record==(uintptr_t)record;
    ReleaseSRWLockShared(&world_queue_lock);return same?s:NULL;
}
static void world_queue_append_observe(world_queue_append_fn original,void *agent,void *record,uintptr_t caller,void *tls) {
    world_queue_event *e=NULL;world_trace_event *trace=NULL;int index=-1;
    int admitted=world_queue_admit();if(admitted) index=world_queue_agent_watch(agent);
    if(index>=0) {
        world_trace_watch *w=&world_trace_watches[index];e=world_queue_begin(1,(unsigned)index,caller,tls);
        if(e) {
            e->record=(uintptr_t)record;
            world_queue_dispatch_scope *scope=world_queue_append_scope(agent,record,caller);
            if(!world_trace_bound || !world_trace_identity(w) || !world_queue_payload(e,record)) e->errors|=8;
            if(!scope) e->errors|=32;
            else {
                e->errors&=~1u;e->caller=0xeacf66;e->caller_source=1;e->dispatch_parent=(uint64_t)(scope->event-world_queue_events)+1;
                e->enqueue=scope->token;e->command=(uintptr_t)scope->command;
                const world_queue_event *source=&world_queue_events[scope->token-1];
                e->order_type=source->order_type;e->related=source->related;e->order_size=source->order_size;
                memcpy(e->command_header,source->command_header,16);
                if(source->saved!=e->saved || memcmp(source->payload,e->payload,sizeof(e->payload))) e->errors|=64;
                if(++scope->append_count!=1) e->errors|=32;
            }
            if(!world_trace_bound || !world_trace_identity(w) || !world_queue_flags(agent,e->flags_before)) e->errors|=256;
            trace=world_trace_begin_context(WT_PENDING,w->object,NULL,0,0,0,0,tls);
            if(trace) e->trace_event=(uint64_t)(trace-world_trace_events)+1;else e->errors|=128;
        }
    }
    original(agent,record);
    if(e) {
        unsigned char again[WORLD_TRACE_COMMAND_BYTES];
        /* Manager dispatch deletes its source only after append returns. */
        if(!read_local(record,again,sizeof(again)) || memcmp(again,e->payload,sizeof(again))) e->errors|=64;
        world_trace_end(trace);if(trace && trace->errors) e->errors|=128;
        if(!world_trace_bound || !world_trace_identity(&world_trace_watches[index]) || !world_queue_flags(agent,e->flags_after)) e->errors|=256;
        world_queue_end(e);
    }
    if(admitted) world_queue_leave();
}
static void world_queue_dispatch_observe(world_queue_dispatch_fn original,void *command,void *agent,uintptr_t caller,void *tls) {
    world_queue_event *e=NULL;world_trace_event *trace=NULL;int index=-1;
    int admitted=world_queue_admit();if(admitted) index=world_queue_agent_watch(agent);
    world_queue_dispatch_scope scope={0};scope.agent=agent;scope.command=command;scope.thread=GetCurrentThreadId();
    if(index>=0) {
        world_trace_watch *w=&world_trace_watches[index];e=world_queue_begin(2,(unsigned)index,caller,tls);scope.runtime=w->runtime;scope.event=e;
        if(e) {
            e->command=(uintptr_t)command;
            if(caller!=(uintptr_t)(image+0xeacf66)) e->errors|=1;
            int decoded=world_queue_command(e,command);if(!decoded) e->errors|=8;
            scope.token=world_queue_claim(e,w->runtime);e->enqueue=scope.token;
            int same=scope.token && world_queue_command_same(e,&world_queue_events[scope.token-1]);
            if(!scope.token) e->errors|=32;
            else if(!same) e->errors|=64;
            e->receipt_valid=decoded && same && agent==w->agent && caller==(uintptr_t)(image+0xeacf66) &&
                world_queue_events[scope.token-1].receipt_valid && world_queue_events[scope.token-1].saved==e->saved &&
                world_queue_receipt_token(e,w->runtime,2);
            if(!world_trace_bound || agent!=w->agent || !world_trace_identity(w) || !world_queue_flags(agent,e->flags_before)) e->errors|=256;
            trace=world_trace_begin_context(WT_ORDER,w->object,NULL,0,0,0,0,tls);
            if(trace) e->trace_event=(uint64_t)(trace-world_trace_events)+1;else e->errors|=128;
            scope.active=scope.token && !e->errors;
        }
    }
    /* Even an unselected/denied nested dispatch replaces the previous scope.
     * This prevents an inner call from borrowing an outer command lifetime. */
    int scoped=world_queue_dispatch_tls!=TLS_OUT_OF_INDEXES;
    if(scoped) {
        scope.previous=TlsGetValue(world_queue_dispatch_tls);
        if(!TlsSetValue(world_queue_dispatch_tls,&scope)) {scoped=0;if(e) e->errors|=512;}
    } else if(e) e->errors|=512;
    unsigned previous_active=scope.previous?scope.previous->active:0;
    if(!scoped && scope.previous) scope.previous->active=0;
    void *receipt=world_queue_receipt_begin(e,agent);
    void *secondary=world_queue_transition_dispatch_begin?world_queue_transition_dispatch_begin(command,agent,caller,tls):NULL;
    original(command,agent);
    if(world_queue_transition_dispatch_end) world_queue_transition_dispatch_end(secondary,command,agent);
    if(!scoped && scope.previous) scope.previous->active=previous_active;
    if(scoped && (!TlsSetValue(world_queue_dispatch_tls,scope.previous))) {if(e) e->errors|=512;else if(admitted) InterlockedIncrement64(&world_queue_failures);}
    if(e) {
        world_queue_event after={0};
        /* Native command destruction follows this call site, outside this
         * bracket. Unlike enqueue, this dispatch still owns a readable input. */
        if(!world_queue_command(&after,command) || !world_queue_command_same(e,&after)) {e->errors|=64;e->receipt_valid=0;}
        if(!world_queue_receipt_token(e,world_trace_watches[index].runtime,2)) {e->errors|=16;e->receipt_valid=0;}
        if(!world_trace_bound || !world_trace_identity(&world_trace_watches[index]) || !world_queue_flags(agent,e->flags_after)) e->errors|=256;
        world_trace_end(trace);if(trace && trace->errors) e->errors|=128;
        if(!world_queue_retire(e,world_trace_watches[index].runtime)) {e->errors|=16;e->receipt_valid=0;}
        world_queue_end(e);
        if(world_queue_model_end) {world_queue_event copy=*e;copy.receipt_index=(unsigned)(e-world_queue_events)+1;world_queue_model_end(receipt,&copy,agent);}
    }
    if(admitted) world_queue_leave();
}
static void *world_queue_tls(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);return slots?slots[0]:NULL;
}
static void world_queue_enqueue(void *manager,uint64_t runtime,void *command) {
    world_queue_enqueue_observe((world_queue_enqueue_fn)(thunk+2560),manager,runtime,command,(uintptr_t)__builtin_return_address(0),world_queue_tls());
}
static void world_queue_append(void *agent,void *record) {
    world_queue_append_observe((world_queue_append_fn)(thunk+2624),agent,record,(uintptr_t)__builtin_return_address(0),world_queue_tls());
}
static void world_queue_dispatch(void *command,void *agent) {
    world_queue_dispatch_observe((world_queue_dispatch_fn)(thunk+2688),command,agent,(uintptr_t)__builtin_return_address(0),world_queue_tls());
}
static int world_queue_model_pins(void) {
    return hash_ok(image+0xeac900,94,"b646fbc88682b1d2c94cc22b2feeb2acb7c50881a8a84e9154f12505d28579a5") &&
        hash_ok(image+0xe979d0,14,"22550dfdea5fb4c9914cf2f22613a65353fd3e1bdd23f9ac64cd7634e7889249") &&
        hash_ok(image+0xe87cf0,1124,"c9d5c8e4b36b4814dc9bf479faa0c8921aeaf4904465cb827f4f4a0634779f5b") &&
        *(void**)(image+0x2c51c40)==image+0xe979d0;
}
static int world_queue_pins(void) {
    return ai_full_layout_verified && world_queue_phase_pins() && world_queue_model_pins() &&
        hash_ok(image+WORLD_QUEUE_ENQUEUE,245,"2b270bde3dbf64151f2a3d69077f5dc90efd758101981317fcdc05decfcbe9dd") &&
        hash_ok(image+WORLD_QUEUE_APPEND,141,"4acf143b3200d1632cd984d9881f2a43a07d3d6786059c4dd0d8659821dbbc9e") &&
        hash_ok(image+0xeadf30,142,"5b340dfed542b9b2deed1aee352bfe20627adebd7d33ab4a8d1bd45c684d30c7") &&
        hash_ok(image+0xeaa6f0,109,"3585026bc4afdb07fa3d1af3488c353d92cf47ec4da0da825cdab13fb5c33c6d") &&
        hash_ok(image+0xe98000,60,"8bcc56a748535ef501b6b7f548d6fbbe3cd5b27d5212b13d428d24be2075b2b0") &&
        hash_ok(image+0xea17a0,201,"c7e5c22502b78acc05a12b166b00c5517a912c3e37a9681427c1a416e2a2143f") &&
        hash_ok(image+0x6e1940,117,"7aed6fcc09f036f63d30c9a911cd5ed6ac2d29196f941e43d83ac6672b15bb9c") &&
        hash_ok(image+0x6e19c0,117,"e6dcc2456175a5644c6ae5c3199aa82828362c56694f697a3c236347302054e2") &&
        hash_ok(image+0x6e1a40,117,"8fdae13e962b99704cf27a1182e27f416d2cd1274d04bfcceee36e8e0325e471") &&
        hash_ok(image+0xe98040,55,"8918662a034aeabfdc63810686cfd906f906d8d41581e9141fcb79d79d59c764") &&
        hash_ok(image+0xe980b0,11,"a1acc045188cbd2be1b9461a03c2e4f4bc10aab8ee9e90992d983e6e559c3d04") &&
        hash_ok(image+0xe980c0,11,"091619c74c9e3f6a05ea5577e8456e30c24de25f8d527632cd5bd6fc2c57a8f3") &&
        hash_ok(image+0xe980d0,11,"11294c3dfd93488ff2aa33caad79a92019de81142a348f6a8739eaa62af9e3b8") &&
        hash_ok(image+0xe89f10,382,"122cfef79d4d41f3c350c46f35d352019dca1bd58cbe93e2f8a4511318beb99f") &&
        hash_ok(image+0xeace40,785,"475eca6da99a7a143f3f4ceb4ece14b45c36bbbbee2cb624a1c268d8cc062818") &&
        hash_ok(image+0x10631b0,472,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d") &&
        hash_ok(image+0x1066ba0,613,"ab00b7598d2b6cce00d9d0135844f47bd2c9a94d9a9fcc1cf9508a994a8d9434") &&
        hash_ok(image+0x106d5b1,40,"98cec6c522690356d88f9c9b8be8ef9e0cd59054dc9c2ea9db26b123bcd5b154");
}
static int world_queue_swap(unsigned site,uint64_t expected,uint64_t value) {
    DWORD old,ignored;unsigned char *entry=image+world_queue_entries[site];
    if(!VirtualProtect(entry,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)entry,(LONG64)value,(LONG64)expected);
    VirtualProtect(entry,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),entry,8);return actual==expected;
}
static void world_queue_restore(void) {
    AcquireSRWLockExclusive(&world_queue_lock);world_queue_restoring=1;
    InterlockedExchange(&world_queue_enabled,0);
    if(world_queue_open>world_queue_restore_open) world_queue_restore_open=world_queue_open;
    ReleaseSRWLockExclusive(&world_queue_lock);
    for(unsigned i=0;i<3;i++) if((world_queue_sites&(1u<<i)) && world_queue_swap(i,world_queue_patched[i],world_queue_original[i])) world_queue_sites&=~(1u<<i);
}
static int world_queue_patch(int (*swap)(unsigned,uint64_t,uint64_t)) {
    for(unsigned i=0;i<3;i++) {
        if(!swap(i,world_queue_original[i],world_queue_patched[i])) {
            for(unsigned j=0;j<i;j++) if(swap(j,world_queue_patched[j],world_queue_original[j])) world_queue_sites&=~(1u<<j);
            return 0;
        }
        world_queue_sites|=1u<<i;
    }
    return 1;
}
/* Retained as the independent actual-image ABI emitter used by the test harness. */
static __attribute__((unused)) int world_queue_emit(unsigned char *trampoline,unsigned char *dispatch,unsigned char *entry,void *target,unsigned site) {
    if(site>2) return 0;
    if(site==2) {
        const unsigned char call[8]={0x48,0x8b,0x01,0xff,0x50,0x08,0x48,0x8b};
        if(memcmp(entry,call,8)) return 0;
        /* A proper ABI call gives nested append the exact return at +10.
         * Its non-leaf stack allocation has resident registered unwind data. */
        const unsigned char forward[15]={0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x50,0x08,0x48,0x83,0xc4,0x28,0xc3};
        const unsigned char unwind[8]={1,4,1,0,4,0x42,0,0};
        memcpy(trampoline,forward,sizeof(forward));memcpy(trampoline+16,unwind,sizeof(unwind));
        memcpy(dispatch,&target,8);return 1; /* resident RIP-relative CALL pointer */
    } else {
        const unsigned char prefix[5]={0x48,0x89,0x5c,0x24,site?0x08:0x10};
        if(memcmp(entry,prefix,5)) return 0;
        memcpy(trampoline,entry,5);trampoline[5]=0xff;trampoline[6]=0x25;memset(trampoline+7,0,4);
        void *next=entry+5;memcpy(trampoline+11,&next,8);
    }
    dispatch[0]=0xff;dispatch[1]=0x25;memset(dispatch+2,0,4);memcpy(dispatch+6,&target,8);return 1;
}
static int world_queue_register_unwind(BOOLEAN (WINAPI *add)(PRUNTIME_FUNCTION,DWORD,DWORD64)) {
    if(world_queue_forward_base) return world_queue_forward_base==(DWORD64)(uintptr_t)thunk;
    memset(&world_queue_forward_unwind,0,sizeof(world_queue_forward_unwind));
    world_queue_forward_unwind.BeginAddress=2688;world_queue_forward_unwind.EndAddress=2703;world_queue_forward_unwind.UnwindData=2704;
    if(!add(&world_queue_forward_unwind,1,(DWORD64)(uintptr_t)thunk)) return 0;
    world_queue_forward_base=(DWORD64)(uintptr_t)thunk;return 1;
}
static int world_queue_prepare_hooks(void) {
    intptr_t distances[3];for(unsigned i=0;i<3;i++) {
        distances[i]=(intptr_t)(thunk+2592+i*64)-(intptr_t)(image+world_queue_entries[i]+(i==2?6:5));
        if(distances[i]<INT32_MIN || distances[i]>INT32_MAX) return 0;
    }
    for(unsigned i=0;i<3;i++) if(!thunk_slot_ready(2560+i*64)) return 0;
    for(unsigned i=0;i<3;i++) {
        memcpy(&world_queue_original[i],image+world_queue_entries[i],8);world_queue_patched[i]=world_queue_original[i];
        unsigned char *patch=(unsigned char*)&world_queue_patched[i];int32_t relative=(int32_t)distances[i];
        if(i==2) {patch[0]=0xff;patch[1]=0x15;memcpy(patch+2,&relative,4);} /* native return stays eacf66 */
        else {patch[0]=0xe9;memcpy(patch+1,&relative,4);}
    }
    return world_queue_register_unwind(RtlAddFunctionTable);
}
static int world_queue_configure(void) {
    if(world_queue_started) return world_queue_sites==7 && world_queue_enabled;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_trace_early || !world_queue_pins()) return 0;
    if(world_queue_dispatch_tls==TLS_OUT_OF_INDEXES) world_queue_dispatch_tls=TlsAlloc();
    if(world_queue_dispatch_tls==TLS_OUT_OF_INDEXES || !world_queue_prepare_hooks()) return 0;
    world_queue_phase_verified=1;world_queue_started=1;if(!world_queue_patch(world_queue_swap)) return 0;
    AcquireSRWLockExclusive(&world_queue_lock);world_queue_restoring=0;InterlockedExchange(&world_queue_enabled,1);
    ReleaseSRWLockExclusive(&world_queue_lock);return 1;
}
static unsigned world_queue_ready(void) {
    unsigned cap=(unsigned)(world_queue_count>WORLD_QUEUE_MAX?WORLD_QUEUE_MAX:world_queue_count),ready=0;
    while(ready<cap && InterlockedCompareExchange(&world_queue_events[ready].complete,0,0)) ++ready;return ready;
}
