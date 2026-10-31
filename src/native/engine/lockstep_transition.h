/* Read-only native controller transitions. Independent of world_trace's fixed
 * NavigationController binding: its lifetime failures remain strict. Sequential
 * reads are diagnostics, never an atomic owner/generation lease. No getters. */
#define TRANSITION_MAX 512u
#define TRANSITION_TRANSFER 0xea4c00u
#define TRANSITION_ACTIVATION 0xd88710u
#define TRANSITION_PUBLICATION 0x2b818a0u
#include "lockstep_transition_identity.h"
enum { TR_TRANSFER=1,TR_PUBLICATION,TR_ACTIVATION,TR_MODEL_ENQUEUE,TR_MODEL_DISPATCH };
typedef uintptr_t (*transition_transfer_fn)(void *,void *,unsigned char,int);
typedef void (*transition_publish_fn)(void *,void *,unsigned char);
typedef void (*transition_activate_fn)(int,unsigned char);
typedef struct {
    uint64_t saved,runtime;
    uintptr_t object,controller,agent,slots,faction,model;
    unsigned controller_type,model_type;
    unsigned char owns_agent,player;
    char owner[64];
} transition_state;
typedef struct {
    volatile LONG complete;
    unsigned kind,errors,thread,boot,caller,arg,extra,queue_errors,queue_valid;
    uint64_t tick,enter,leave,parent,queue,enqueue,transfer,publication;
    uintptr_t old_controller,new_controller,result;
    transition_state before,after;
    unsigned char old_owns_after,active_before,active_after;
} transition_event;
static transition_event transition_events[TRANSITION_MAX];
static LONG64 transition_count,transition_order,transition_failures,transition_excluded,transition_dropped;
static LONG64 transition_open,transition_peak,transition_restore_open;
/* Direct transfer/publication/activation scopes only. Queue unwind coverage is
 * deliberately separate and not established by these counters. */
static LONG64 transition_direct_calls,transition_direct_returns,transition_direct_unwinds;
static SRWLOCK transition_lock=SRWLOCK_INIT;
static unsigned transition_started,transition_enabled,transition_closing,transition_sites,transition_rows;
static unsigned transition_prebind_enabled;
static void transition_prebind_activation(const transition_event *);
static DWORD transition_tls=TLS_OUT_OF_INDEXES;
static uint64_t transition_original[3],transition_patched[3];
static const unsigned transition_entries[3]={TRANSITION_TRANSFER,TRANSITION_ACTIVATION,TRANSITION_PUBLICATION};
static RUNTIME_FUNCTION transition_unwind;
static DWORD64 transition_unwind_base;
/* Each entry, including an excluded nested callback, shadows the parent. */
typedef struct transition_scope {struct transition_scope *previous;transition_event *event;} transition_scope;
static int transition_admit(void) {
    AcquireSRWLockExclusive(&transition_lock);
    int admitted=transition_enabled && !transition_closing && stats.active;
    if(admitted && ++transition_open>transition_peak) transition_peak=transition_open;
    ReleaseSRWLockExclusive(&transition_lock);return admitted;
}
static void transition_leave(void) {
    AcquireSRWLockExclusive(&transition_lock);
    if(transition_open<=0) ++transition_failures;else --transition_open;
    ReleaseSRWLockExclusive(&transition_lock);
}
#include "lockstep_transition_watch.h"
static uint64_t transition_index(const transition_event *e) {return e?(uint64_t)(e-transition_events)+1:0;}
static transition_event *transition_begin(unsigned kind,uintptr_t caller) {
    LONG64 index=InterlockedIncrement64(&transition_count);
    if(index>TRANSITION_MAX) {InterlockedIncrement64(&transition_dropped);return NULL;}
    transition_event *e=&transition_events[index-1];e->kind=kind;e->tick=stats.issued;e->boot=boot_phase;
    e->thread=GetCurrentThreadId();e->enter=(uint64_t)InterlockedIncrement64(&transition_order);
    if(caller>=(uintptr_t)image && caller<(uintptr_t)(image+0x6fa9000)) e->caller=(unsigned)(caller-(uintptr_t)image);
    else e->errors|=1;
    transition_scope *p=transition_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(transition_tls);
    e->parent=p?transition_index(p->event):0;return e;
}
static void transition_end(transition_event *e) {
    if(!e) return;e->leave=(uint64_t)InterlockedIncrement64(&transition_order);
    if(e->errors) InterlockedIncrement64(&transition_failures);
    InterlockedExchange(&e->complete,1);
    origin_transition_edge(transition_index(e)); /* Separate provenance edge, never a success verdict. */
}
static int transition_scope_enter(transition_scope *s,transition_event *e) {
    s->event=e;s->previous=transition_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(transition_tls);
    if(transition_tls!=TLS_OUT_OF_INDEXES && TlsSetValue(transition_tls,s)) return 1;
    if(e) e->errors|=2;return 0;
}
static void transition_scope_leave(transition_scope *s,int scoped) {
    if(scoped && !TlsSetValue(transition_tls,s->previous)) InterlockedIncrement64(&transition_failures);
}
static void transition_scope_restore(transition_scope *s,int scoped,transition_event *previous) {
    if(!scoped && s->previous) s->previous->event=previous;
    transition_scope_leave(s,scoped);
}
static void transition_direct_finish(int admitted,transition_event *e,int abnormal) {
    if(!admitted) return;
    if(abnormal) {
        InterlockedIncrement64(&transition_direct_unwinds);
        if(e && !InterlockedCompareExchange(&e->complete,0,0)) {e->errors|=64;transition_end(e);}
        else InterlockedIncrement64(&transition_failures);
    } else InterlockedIncrement64(&transition_direct_returns);
    transition_leave();
}
static int transition_object_watch(void *object) {
    if(transition_saved_count) return transition_saved_object(object);
    int found=-1;AcquireSRWLockShared(&world_trace_import_lock);
    for(unsigned i=0;i<world_trace_watch_count;i++) if(world_trace_watches[i].object==object && object) {
        if(found>=0 || world_trace_imports[i]!=1) {found=-2;break;}found=(int)i;
    }
    if(found>=0) for(unsigned i=0;i<world_trace_watch_count;i++) if(i!=(unsigned)found &&
        (world_trace_watches[i].saved==world_trace_watches[found].saved ||
         (world_trace_watches[i].runtime && world_trace_watches[i].runtime==world_trace_watches[found].runtime))) {found=-2;break;}
    ReleaseSRWLockShared(&world_trace_import_lock);return found;
}
static int transition_selected(void *object) {
    int index=transition_object_watch(object);
    if(index==-2) InterlockedIncrement64(&transition_failures); /* ambiguous claimed owner, never mere exclusion */
    return index>=0;
}
static int transition_owner(uintptr_t faction,char owner[64]) {
    uint64_t size,capacity;void *data=(void*)(faction+0x10);
    if(!faction || !read_local((void*)(faction+0x20),&size,8) || !size || size>=64 ||
       !read_local((void*)(faction+0x28),&capacity,8) || capacity<size ||
       (capacity>=16 && (!read_local(data,&data,8) || !data)) || !read_local(data,owner,(size_t)size+1) || owner[size]) return 0;
    for(unsigned i=0;i<size;i++) if(!((owner[i]>='a'&&owner[i]<='z') || (owner[i]>='0'&&owner[i]<='9') || owner[i]=='_')) return 0;
    return 1;
}
static int transition_read_native(void *object,void *controller,uint64_t saved,uint64_t runtime,transition_state *s) {
    memset(s,0,sizeof(*s));uintptr_t type,owner;s->object=(uintptr_t)object;s->saved=saved;
    /* Only saved Small ships in the imported watch manifest. Dynamic objects,
     * XL/Large/base types and unobserved imports have excluded coverage. */
    if(!read_local(object,&type,8) || type!=(uintptr_t)(image+0x2b7feb8) ||
       !read_local((unsigned char*)object+8,&s->runtime,8) || s->runtime!=runtime ||
       world_identity_lookup(s->runtime)!=s->saved || !read_local((void*)(type+0x1620),&owner,8) || owner!=(uintptr_t)(image+0x352b10) ||
       !read_local((unsigned char*)object+0x348,&s->faction,8) || !transition_owner(s->faction,s->owner)) return 0;
    if(!controller && !read_local((unsigned char*)object+0x200,&controller,8)) return 0;
    s->controller=(uintptr_t)controller;
    if(!controller || !read_local(controller,&type,8)) return 0;
    type=pslot_canonical_vtable(type); /* a stock slot's PlayerController is a PlayerController */
    if((type!=(uintptr_t)(image+0x2c51158) && type!=(uintptr_t)(image+0x2c39488)) ||
       !read_local((unsigned char*)controller+0x28,&owner,8) || owner!=(uintptr_t)object ||
       !read_local((unsigned char*)controller+0x30,&s->slots,8) ||
       !read_local((unsigned char*)controller+0x40,&s->agent,8) || !s->agent || s->slots!=s->agent+0xad0 ||
       !read_local((unsigned char*)controller+0x58,&s->owns_agent,1) || s->owns_agent>1 ||
       !read_local((void*)(s->agent+0xef0),&owner,8) || owner!=(uintptr_t)object ||
       !read_local((void*)(s->agent+0xf79),&s->player,1) || s->player>1 ||
       !read_local((void*)(s->agent+0xf10),&s->model,8)) return 0;
    s->controller_type=(unsigned)(type-(uintptr_t)image);
    if(s->model) {
        if(!read_local((void*)s->model,&type,8) || type<(uintptr_t)image || type>=(uintptr_t)(image+0x6fa9000) ||
           !read_local((void*)(s->model+0x10),&owner,8) || owner!=s->agent) return 0;
        s->model_type=(unsigned)(type-(uintptr_t)image);
    }
    return 1;
}
static int transition_read_once(void *object,void *controller,transition_state *s) {
    int index=transition_object_watch(object);if(index<0) return 0;
    if(transition_saved_count) {
        transition_saved_watch w;
        return transition_saved_copy((unsigned)index,&w) && transition_read_native(object,controller,w.saved,w.runtime,s) && transition_saved_guard(&w,s);
    }
    world_trace_watch *w=&world_trace_watches[index];return transition_read_native(object,controller,w->saved,w->runtime,s);
}
static int transition_read(void *object,void *controller,transition_state *s) {
    transition_state again;
    return transition_read_once(object,controller,s) && transition_read_once(object,controller,&again) && !memcmp(s,&again,sizeof(again));
}
static int transition_same_ship(const transition_state *a,const transition_state *b) {
    return a->saved && a->saved==b->saved && a->runtime==b->runtime && a->object==b->object && a->agent==b->agent &&
        a->slots==b->slots && a->faction==b->faction && !strcmp(a->owner,b->owner);
}
static void transition_links(transition_event *e) {
    unsigned count=(unsigned)(transition_count>TRANSITION_MAX?TRANSITION_MAX:transition_count);
    for(unsigned i=0;i<count;i++) {
        transition_event *p=&transition_events[i];
        if(!InterlockedCompareExchange(&p->complete,0,0) || p->errors || p->leave>=e->enter ||
           !transition_same_ship(&p->after,&e->before) || p->new_controller!=e->before.controller) continue;
        if(p->kind==TR_TRANSFER) e->transfer=i+1;
        if(p->kind==TR_PUBLICATION) e->publication=i+1;
    }
    /* Native owner binding can synchronously activate input after component
     * publication but before the publication wrapper returns. Record the open
     * ancestor token; a consumer must still require its completed after-state. */
    transition_scope *scope=transition_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(transition_tls);
    for(;scope && scope->event;scope=scope->previous) {
        transition_event *p=scope->event;
        if(p->kind==TR_PUBLICATION && !p->errors && p->new_controller==e->before.controller &&
           transition_same_ship(&p->before,&e->before)) {e->publication=transition_index(p);break;}
    }
}
static uintptr_t transition_transfer_observe(transition_transfer_fn original,void *next,void *old,unsigned char flag,int extra,uintptr_t caller) {
    int admitted=transition_admit();transition_event *e=NULL;void *object=NULL;
    transition_scope scope={0};int scoped=0,scope_started=0,scope_done=0;transition_event *previous=NULL;uintptr_t result=0;
    if(admitted) InterlockedIncrement64(&transition_direct_calls);
    __try {
        if(admitted && read_local((unsigned char*)old+0x28,&object,8) && transition_selected(object)) {
            e=transition_begin(TR_TRANSFER,caller);
            if(e) {e->old_controller=(uintptr_t)old;e->new_controller=(uintptr_t)next;e->arg=flag;e->extra=(unsigned)extra;
                if(flag>1 || extra<0 || extra>1 || !transition_read(object,old,&e->before)) e->errors|=4;}
        } else if(admitted) InterlockedIncrement64(&transition_excluded);
        scoped=transition_scope_enter(&scope,e);scope_started=1;previous=scope.previous?scope.previous->event:NULL;
        if(!scoped && scope.previous) scope.previous->event=NULL;
        result=original(next,old,flag,extra);
        transition_scope_restore(&scope,scoped,previous);scope_done=1;
        if(e) {
            e->result=result;
            if(!transition_read(object,next,&e->after) || !transition_same_ship(&e->before,&e->after) ||
               !read_local((unsigned char*)old+0x58,&e->old_owns_after,1) || e->old_owns_after ||
               !e->after.owns_agent || e->after.player!=flag || result!=(uintptr_t)next) e->errors|=8;
            transition_end(e);
        }
    } __finally {
        if(scope_started && !scope_done) transition_scope_restore(&scope,scoped,previous);
        transition_direct_finish(admitted,e,AbnormalTermination());
    }
    return result;
}
static void transition_publish_observe(transition_publish_fn original,void *object,void *next,unsigned char detach,uintptr_t caller) {
    int admitted=transition_admit();transition_event *e=NULL;
    transition_scope scope={0};int scoped=0,scope_started=0,scope_done=0;transition_event *previous=NULL;
    if(admitted) InterlockedIncrement64(&transition_direct_calls);
    __try {
        if(admitted && transition_selected(object)) {
            e=transition_begin(TR_PUBLICATION,caller);
            if(e) {e->new_controller=(uintptr_t)next;e->arg=detach;
                if(detach>1 || !transition_read(object,NULL,&e->before)) e->errors|=4;
                e->old_controller=e->before.controller;}
        } else if(admitted) InterlockedIncrement64(&transition_excluded);
        scoped=transition_scope_enter(&scope,e);scope_started=1;previous=scope.previous?scope.previous->event:NULL;
        if(!scoped && scope.previous) scope.previous->event=NULL;
        original(object,next,detach);
        transition_scope_restore(&scope,scoped,previous);scope_done=1;
        /* Old controller may have been destroyed. Never read it on return. The
         * incoming controller may also have been rejected: read published first. */
        if(e) {
            if(!transition_read(object,NULL,&e->after) || e->after.controller!=(uintptr_t)next ||
               !transition_same_ship(&e->before,&e->after)) e->errors|=8;
            transition_end(e);
        }
    } __finally {
        if(scope_started && !scope_done) transition_scope_restore(&scope,scoped,previous);
        transition_direct_finish(admitted,e,AbnormalTermination());
    }
}
static void transition_activate_observe(transition_activate_fn original,int context,unsigned char active,uintptr_t caller) {
    int admitted=transition_admit();transition_event *e=NULL;
    transition_scope scope={0};int scoped=0,scope_started=0,scope_done=0;transition_event *previous=NULL;
    if(admitted) InterlockedIncrement64(&transition_direct_calls);
    __try {
        if(admitted && context==8) {
            e=transition_begin(TR_ACTIVATION,caller);
            if(e) {e->arg=active;e->extra=(unsigned)context;if(active>1 || !read_local(image+0x2f4b828,&e->active_before,1)) e->errors|=4;
                transition_prebind_activation(e);}
        } else if(admitted) InterlockedIncrement64(&transition_excluded);
        scoped=transition_scope_enter(&scope,e);scope_started=1;previous=scope.previous?scope.previous->event:NULL;
        if(!scoped && scope.previous) scope.previous->event=NULL;
        original(context,active);
        transition_scope_restore(&scope,scoped,previous);scope_done=1;
        if(e) {if(!read_local(image+0x2f4b828,&e->active_after,1)) e->errors|=8;transition_end(e);}
    } __finally {
        if(scope_started && !scope_done) transition_scope_restore(&scope,scoped,previous);
        transition_direct_finish(admitted,e,AbnormalTermination());
    }
}
/* Immutable copies supplied while command ownership is known. No command is
 * read here. The enqueue record completes BEFORE native ownership transfer. */
static void *transition_queue_begin(const world_queue_event *q,void *agent) {
    if(q->order_type!=WORLD_QUEUE_MODEL_VTABLE || !transition_admit()) return NULL;
    transition_event *e=transition_begin(agent?TR_MODEL_DISPATCH:TR_MODEL_ENQUEUE,(uintptr_t)image+q->caller);
    if(e) {
        e->queue=(uint64_t)q->receipt_index;e->enqueue=q->enqueue;e->arg=(unsigned)q->order_value;e->queue_errors=q->errors;e->queue_valid=q->receipt_valid;
        if(!q->receipt_valid || (q->errors&~(agent?(128u|256u):0u))) e->errors|=32;
        void *object=NULL;
        if(agent) read_local((unsigned char*)agent+0xef0,&object,8);
        else {
            AcquireSRWLockShared(&world_trace_import_lock);
            for(unsigned i=0;i<world_trace_watch_count;i++) {
                if(world_trace_watches[i].saved==q->saved) object=world_trace_watches[i].object;
            }
            ReleaseSRWLockShared(&world_trace_import_lock);
        }
        if(!transition_read(object,NULL,&e->before) || (agent && e->before.agent!=(uintptr_t)agent)) e->errors|=4;
        transition_links(e);
        if(agent) {
            unsigned count=(unsigned)(transition_count>TRANSITION_MAX?TRANSITION_MAX:transition_count),matches=0;
            for(unsigned i=0;i<count;i++) {
                transition_event *p=&transition_events[i];
                if(!InterlockedCompareExchange(&p->complete,0,0) || p->kind!=TR_MODEL_ENQUEUE || p->queue!=q->enqueue) continue;
                ++matches;
                if(p->errors || !transition_same_ship(&p->before,&e->before) || p->before.controller!=e->before.controller) e->errors|=16;
                else {e->transfer=p->transfer;e->publication=p->publication;}
            }
            if(matches!=1) e->errors|=16;
        }
    }
    if(!agent) {transition_end(e);transition_leave();return NULL;}
    if(!e) transition_leave();return e;
}
static void transition_queue_end(void *token,const world_queue_event *q,void *agent) {
    transition_event *e=token;if(!e) return;e->queue_errors=q->errors;e->queue_valid=q->receipt_valid;
    if(!q->receipt_valid || (q->errors&~(128u|256u))) e->errors|=32;
    if(!transition_read((void*)e->before.object,NULL,&e->after) || !transition_same_ship(&e->before,&e->after) ||
       e->after.agent!=(uintptr_t)agent || e->after.controller!=e->before.controller) e->errors|=8;
    /* A zero model alone is not a takeover ACK: consumers must join explicit
     * transfer/publication and enqueue records; this is only observed dispatch. */
    transition_end(e);transition_leave();
}
#include "lockstep_transition_queue.h"
static uintptr_t transition_transfer(void *next,void *old,unsigned char flag,int extra) {
    return transition_transfer_observe((transition_transfer_fn)(thunk+2816),next,old,flag,extra,(uintptr_t)__builtin_return_address(0));
}
static void transition_activate(int context,unsigned char active) {
    transition_activate_observe((transition_activate_fn)(thunk+2880),context,active,(uintptr_t)__builtin_return_address(0));
}
static void transition_publish(void *object,void *next,unsigned char detach) {
    transition_publish_observe((transition_publish_fn)(image+0x6cd8b0),object,next,detach,(uintptr_t)__builtin_return_address(0));
}
#include "lockstep_transition_hooks.h"
