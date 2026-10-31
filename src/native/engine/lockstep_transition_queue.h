/* Schema2 model tickets are private to the saved transition watch. This ledger
 * never creates raw world rows or changes raw world coverage/error acceptance. */
#define TRANSITION_QUEUE_MAX 256u
typedef struct {
    uint64_t runtime,event;uintptr_t command,agent;
    unsigned state,watch,mode;uint64_t dispatch;unsigned char bytes[16],after[16];
} transition_saved_ticket;
static transition_saved_ticket transition_saved_tickets[TRANSITION_QUEUE_MAX];
static unsigned transition_saved_tickets_count;
static LONG64 transition_saved_queue_calls,transition_saved_queue_returns,transition_saved_queue_other,transition_saved_queue_unknown;
static LONG64 transition_saved_queue_failures;
static int transition_saved_queue_admit(void) {
    AcquireSRWLockExclusive(&transition_lock);
    int ok=transition_saved_count && transition_enabled && !transition_closing && stats.active && stats.issued<=transition_saved_last;
    if(ok) {++transition_saved_queue_calls;if(++transition_open>transition_peak) transition_peak=transition_open;}
    ReleaseSRWLockExclusive(&transition_lock);return ok;
}
static void transition_saved_queue_leave(void) {
    AcquireSRWLockExclusive(&transition_lock);++transition_saved_queue_returns;
    if(transition_open<=0) ++transition_failures;else --transition_open;ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_saved_runtime(uint64_t runtime,transition_saved_watch *out) {
    int index=-1;AcquireSRWLockExclusive(&transition_lock);
    for(unsigned i=0;i<transition_saved_count;i++) if(runtime && transition_saved_watches[i].runtime==runtime) {
        if(!transition_saved_bound) {++transition_saved_prebind;break;}
        if(index>=0 || transition_saved_watches[i].imports!=1 || transition_saved_failures) {index=-2;break;}
        index=(int)i;*out=transition_saved_watches[i];
    }
    ReleaseSRWLockExclusive(&transition_lock);return index;
}
/* 1=model,0=other in-image vtable (excluded),-1=unreadable/invalid model. */
static int transition_model_copy_read(transition_reader read,void *command,unsigned char bytes[16],unsigned *mode) {
    uintptr_t type,copied;unsigned char again[16];
    if(!read(command,&type,8) || type<(uintptr_t)image || type>=(uintptr_t)(image+0x6fa9000)) return -1;
    if(type!=(uintptr_t)(image+WORLD_QUEUE_MODEL_VTABLE)) return 0;
    if(!read(command,bytes,16) || !read(command,again,16) || memcmp(bytes,again,16)) return -1;
    memcpy(&copied,bytes,8);if(copied!=type) return -1;
    memcpy(mode,bytes+8,4);return *mode<=6?1:-1;
}
static int transition_model_copy(void *command,unsigned char bytes[16],unsigned *mode) {
    return transition_model_copy_read(read_local,command,bytes,mode);
}
#include "lockstep_transition_prebind.h"
#include "lockstep_transition_queue_diag.h"
static void *transition_saved_enqueue_begin(void *manager,uint64_t runtime,void *command,uintptr_t caller,void *tls) {
    (void)tls;if(!transition_saved_queue_admit()) return NULL;
    void *early=transition_prebind_enqueue(read_local,manager,runtime,command,caller);
    transition_diag_capture(read_local,1,command,manager,runtime,caller);
    if(early) return early;
    transition_saved_watch w;int index=transition_saved_runtime(runtime,&w);
    if(index<0) {InterlockedIncrement64(index==-2?&transition_saved_queue_failures:&transition_saved_queue_unknown);return (void*)1;}
    unsigned char bytes[16]={0};unsigned mode=0;int decoded=transition_model_copy(command,bytes,&mode);
    if(!decoded) {InterlockedIncrement64(&transition_saved_queue_other);return (void*)1;}
    transition_event *e=transition_begin(TR_MODEL_ENQUEUE,caller);
    if(!e) {InterlockedIncrement64(&transition_saved_queue_failures);return (void*)1;}
    uintptr_t expected=0;e->arg=mode;
    if(decoded!=1 || !transition_read((void*)w.object,NULL,&e->before) ||
       !read_local(image+0x6cf21c8,&expected,8) || expected!=(uintptr_t)manager) e->errors|=32;
    if(!e->errors) {
        AcquireSRWLockExclusive(&transition_lock);unsigned n=transition_saved_tickets_count;
        for(unsigned i=0;i<n;i++) if(transition_saved_tickets[i].command==(uintptr_t)command && transition_saved_tickets[i].state!=3) e->errors|=16;
        if(n>=TRANSITION_QUEUE_MAX) e->errors|=16;
        if(!e->errors) {
            transition_saved_ticket *ticket=&transition_saved_tickets[n];
            *ticket=(transition_saved_ticket){.runtime=runtime,.event=transition_index(e),.command=(uintptr_t)command,.agent=w.agent,.state=1,.watch=(unsigned)index,.mode=mode};
            memcpy(ticket->bytes,bytes,16);++transition_saved_tickets_count;e->queue=e->enqueue=n+1;e->queue_valid=1;
        }
        ReleaseSRWLockExclusive(&transition_lock);
    }
    transition_links(e);transition_end(e);
    /* The model receipt is complete before original transfers ownership. */
    return (void*)1;
}
static void transition_saved_enqueue_end(void *scope) {
    if(scope==&transition_prebind) transition_prebind_ack();
    if(scope) transition_saved_queue_leave(); /* no native input access */
}
static void *transition_saved_dispatch_begin(void *command,void *agent,uintptr_t caller,void *tls) {
    (void)tls;if(!transition_saved_queue_admit()) return NULL;
    transition_prebind_dispatch(command);
    transition_diag_capture(read_local,2,command,agent,0,caller);
    transition_saved_ticket ticket={0};unsigned number=0,matches=0;
    AcquireSRWLockShared(&transition_lock);
    for(unsigned i=0;i<transition_saved_tickets_count;i++) if(transition_saved_tickets[i].command==(uintptr_t)command && transition_saved_tickets[i].state!=3) {
        ticket=transition_saved_tickets[i];number=i+1;++matches;
    }
    ReleaseSRWLockShared(&transition_lock);
    transition_saved_watch w;int index=-1;
    if(matches==1 && transition_saved_copy(ticket.watch,&w)) index=(int)ticket.watch;
    else {
        uintptr_t object=0;uint64_t runtime=0;
        if(read_local((unsigned char*)agent+0xef0,&object,8) && transition_read_offset(read_local,object,8,&runtime,8)) index=transition_saved_runtime(runtime,&w);
    }
    if(index<0 && !matches) {InterlockedIncrement64(index==-2?&transition_saved_queue_failures:&transition_saved_queue_unknown);return (void*)1;}
    unsigned char bytes[16]={0};unsigned mode=0;int decoded=transition_model_copy(command,bytes,&mode);
    if(!decoded && !matches) {InterlockedIncrement64(&transition_saved_queue_other);return (void*)1;}
    transition_event *e=transition_begin(TR_MODEL_DISPATCH,caller);
    if(!e) {InterlockedIncrement64(&transition_saved_queue_failures);return (void*)1;}
    e->arg=mode;e->queue=e->enqueue=number;
    if(index<0 || matches!=1 || decoded!=1 || !transition_prebind_ticket_good(number) || caller!=(uintptr_t)(image+0xeacf66) ||
       !transition_read((void*)w.object,NULL,&e->before) || e->before.agent!=(uintptr_t)agent ||
       ticket.runtime!=w.runtime || ticket.agent!=(uintptr_t)agent || memcmp(ticket.bytes,bytes,16) || ticket.state!=1 ||
       !ticket.event || ticket.event>TRANSITION_MAX || ticket.event>(uint64_t)transition_count) e->errors|=32;
    if(!e->errors) {
        transition_event *source=&transition_events[ticket.event-1];
        if(!InterlockedCompareExchange(&source->complete,0,0) || source->errors || !source->queue_valid ||
           !transition_same_ship(&source->before,&e->before) || source->before.controller!=e->before.controller) e->errors|=16;
        else {e->transfer=source->transfer;e->publication=source->publication;}
        AcquireSRWLockExclusive(&transition_lock);
        if(memcmp(&ticket,&transition_saved_tickets[number-1],sizeof(ticket))) e->errors|=16;
        if(!e->errors) {transition_saved_tickets[number-1].state=2;transition_saved_tickets[number-1].dispatch=transition_index(e);e->queue_valid=1;}
        ReleaseSRWLockExclusive(&transition_lock);
    }
    return e;
}
static void transition_saved_dispatch_end(void *scope,void *command,void *agent) {
    if(!scope) return;
    if(scope!=(void*)1) {
        transition_event *e=scope;unsigned char bytes[16]={0};unsigned mode=0;
        if(!e->queue_valid || transition_model_copy(command,bytes,&mode)!=1 || mode!=e->arg ||
           !transition_read((void*)e->before.object,NULL,&e->after) || !transition_same_ship(&e->before,&e->after) ||
           e->after.agent!=(uintptr_t)agent || e->after.controller!=e->before.controller) {e->errors|=32;e->queue_valid=0;}
        AcquireSRWLockExclusive(&transition_lock);
        if(e->queue_valid && e->enqueue && e->enqueue<=transition_saved_tickets_count) {
            transition_saved_ticket *ticket=&transition_saved_tickets[e->enqueue-1];
            if(ticket->state!=2 || ticket->runtime!=e->after.runtime || ticket->agent!=(uintptr_t)agent ||
               ticket->command!=(uintptr_t)command || memcmp(ticket->bytes,bytes,16)) {e->errors|=16;e->queue_valid=0;}
            else {memcpy(ticket->after,bytes,16);ticket->state=3;}
        } else {e->errors|=16;e->queue_valid=0;}
        ReleaseSRWLockExclusive(&transition_lock);transition_end(e);
    }
    transition_saved_queue_leave();
}
static unsigned transition_saved_outstanding(void) {
    unsigned count=0;AcquireSRWLockShared(&transition_lock);
    for(unsigned i=0;i<transition_saved_tickets_count;i++) if(transition_saved_tickets[i].state!=3) ++count;
    ReleaseSRWLockShared(&transition_lock);return count;
}
