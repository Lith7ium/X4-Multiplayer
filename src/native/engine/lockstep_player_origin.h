/* Read-only GetUp provenance core. Native capture/pins and hook publication
 * belong to the separate, explicitly configured origin observer.
 * The capture callback performs guarded reads only; it must not call getters.
 * This ledger never retains native ownership or establishes an atomic lease. */
#ifndef LOCKSTEP_PLAYER_ORIGIN_H
#define LOCKSTEP_PLAYER_ORIGIN_H
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#define PORIGIN_MAX 32u
#define PORIGIN_LINK_MAX 128u
enum {PO_POST=0,PO_DELIVERY=1,PO_AFTER=2};
enum {PO_CAPTURE=1,PO_IDENTITY=2,PO_CALLER=4,PO_DUPLICATE=8,PO_CAPACITY=16,PO_TIME=32,PO_LINK=64,PO_UNWIND=128};
typedef struct {
    uint64_t player_saved,ship_saved,player_runtime,ship_runtime;
    uintptr_t player,ship,agent,ship_owner;
} porigin_binding;
typedef struct {
    porigin_binding binding;
    uintptr_t controller;
    uint64_t tick,clock_bits,transition_order;
} porigin_sample;
typedef struct {
    void *receiver,*event,*source;
    uint64_t time_bits;
    unsigned caller,flags;
} porigin_arguments;
/* Return 1 only after the independently pinned native identity/event guards.
 * PO_AFTER reads the currently published controller, never the old controller. */
typedef int (*porigin_capture)(void *,unsigned,const porigin_arguments *,porigin_sample *);
typedef uint64_t (*porigin_post_fn)(void *,void *,double,unsigned char);
typedef uint64_t (*porigin_delivery_fn)(void *,void *,void *,double);
typedef struct {
    uintptr_t event,controller_before,controller_after;
    uint64_t due,post_tick,delivery_tick,post_rax,delivery_rax,post_clock,delivery_clock,after_clock;
    uint64_t request,post_enter,post_leave,delivery_enter,delivery_leave,after_tick;
    uint64_t transition_before,transition_after;
    unsigned posted_returned,delivery_state,errors,thread,delivery_thread;
} porigin_ticket;
typedef struct {unsigned ticket;uint64_t transition;} porigin_link;
typedef struct {
    SRWLOCK lock;
    porigin_binding binding;
    porigin_ticket tickets[PORIGIN_MAX];
    porigin_link links[PORIGIN_LINK_MAX];
    uint64_t post_calls,post_returns,delivery_calls,delivery_returns;
    uint64_t post_unwinds,delivery_unwinds;
    uint64_t other_receivers,unknown,failures,open,peak,stop_open,excluded;
    unsigned count,link_count,configured,enabled,closing;
    uint64_t order;
    uint64_t (*request)(void); /* Optional native public-action scope, schema3. */
    int (*window)(void); /* Gate phase/tick reads only, called under this lock. */
} porigin_ledger;
typedef struct porigin_scope {
    struct porigin_scope *parent;
    porigin_ledger *ledger;
    unsigned admitted,ticket,selected;
} porigin_scope;
static _Thread_local porigin_scope *porigin_current;
static int porigin_same(const porigin_binding *a,const porigin_binding *b) {
    return a->player_saved==b->player_saved && a->ship_saved==b->ship_saved &&
        a->player_runtime==b->player_runtime && a->ship_runtime==b->ship_runtime &&
        a->player==b->player && a->ship==b->ship && a->agent==b->agent && a->ship_owner==b->ship_owner;
}
static int porigin_configure(porigin_ledger *l,const porigin_binding *b) {
    int ok=0;AcquireSRWLockExclusive(&l->lock);
    if(!l->closing && b->player_saved && b->ship_saved && b->player_saved!=b->ship_saved &&
       b->player_runtime && b->ship_runtime && b->player_runtime!=b->ship_runtime &&
       b->player && b->ship && b->agent && b->ship_owner && b->player!=b->ship &&
       b->player!=b->agent && b->ship!=b->agent) {
        if(!l->configured) {l->binding=*b;l->configured=l->enabled=1;ok=1;}
        else ok=porigin_same(&l->binding,b);
    }
    ReleaseSRWLockExclusive(&l->lock);return ok;
}
static void porigin_stop(porigin_ledger *l) {
    AcquireSRWLockExclusive(&l->lock);l->closing=1;l->enabled=0;
    if(l->open>l->stop_open) l->stop_open=l->open;
    ReleaseSRWLockExclusive(&l->lock);
}
static void porigin_enter(porigin_ledger *l,porigin_scope *s,unsigned kind) {
    *s=(porigin_scope){.parent=porigin_current,.ledger=l,.ticket=UINT_MAX};
    AcquireSRWLockExclusive(&l->lock);
    if(l->configured && l->enabled && !l->closing && (!l->window || l->window())) {
        s->admitted=1;if(kind==PO_POST) ++l->post_calls;else ++l->delivery_calls;
        if(++l->open>l->peak) l->peak=l->open;
    } else if(l->configured) ++l->excluded;
    ReleaseSRWLockExclusive(&l->lock);
    /* Every forwarded call shadows its parent, including excluded calls. */
    porigin_current=s;
}
static void porigin_leave(porigin_scope *s,unsigned kind,uint64_t result) {
    porigin_ledger *l=s->ledger;
    if(s->admitted) {
        AcquireSRWLockExclusive(&l->lock);
        if(s->ticket<l->count) {
            porigin_ticket *t=&l->tickets[s->ticket];
            if(kind==PO_POST) {t->posted_returned=1;t->post_rax=result;t->post_leave=++l->order;}
            else if(s->selected) {t->delivery_state=2;t->delivery_rax=result;t->delivery_leave=++l->order;}
        }
        if(kind==PO_POST) ++l->post_returns;else ++l->delivery_returns;
        if(!l->open) ++l->failures;else --l->open;
        ReleaseSRWLockExclusive(&l->lock);
    }
    porigin_current=s->parent;
}
/* Abnormal cleanup accesses only resident observer storage. The native event
 * may already be destroyed and no native after-state can be sampled here. */
static void porigin_unwind(porigin_scope *s,unsigned kind) {
    porigin_ledger *l=s->ledger;
    if(s->admitted) {
        AcquireSRWLockExclusive(&l->lock);
        if(s->ticket<l->count) l->tickets[s->ticket].errors|=PO_UNWIND;
        if(kind==PO_POST) ++l->post_unwinds;else ++l->delivery_unwinds;
        ++l->failures;if(!l->open) ++l->failures;else --l->open;
        ReleaseSRWLockExclusive(&l->lock);
    }
    porigin_current=s->parent;
}
static unsigned porigin_sample_errors(porigin_ledger *l,const porigin_sample *s) {
    return !porigin_same(&l->binding,&s->binding) || !s->controller ? PO_IDENTITY:0;
}
static uint64_t porigin_bits(double time) {uint64_t bits;memcpy(&bits,&time,8);return bits;}
static void porigin_post_begin(porigin_scope *scope,porigin_capture capture,void *user,
                               const porigin_arguments *a) {
    if(!scope->admitted) return;
    porigin_ledger *l=scope->ledger;porigin_sample s={0};unsigned error=0;
    if(!capture(user,PO_POST,a,&s)) error|=PO_CAPTURE;
    error|=porigin_sample_errors(l,&s);
    double due;memcpy(&due,&a->time_bits,8);
    if(!isfinite(due) || due<0) error|=PO_TIME;
    if(a->caller!=0x1abb0c || a->flags!=1) error|=PO_CALLER;
    if(!a->event || (uintptr_t)a->receiver!=l->binding.player || a->source!=a->receiver) error|=PO_IDENTITY;
    AcquireSRWLockExclusive(&l->lock);
    for(unsigned i=0;i<l->count;i++) if(l->tickets[i].event==(uintptr_t)a->event) error|=PO_DUPLICATE;
    if(l->count==PORIGIN_MAX) error|=PO_CAPACITY;
    if(error) ++l->failures;
    else {
        scope->ticket=l->count++;
        l->tickets[scope->ticket]=(porigin_ticket){.event=(uintptr_t)a->event,.due=a->time_bits,
            .post_tick=s.tick,.controller_before=s.controller,.post_clock=s.clock_bits,.thread=GetCurrentThreadId(),
            .request=l->request?l->request():0,.post_enter=++l->order};
    }
    ReleaseSRWLockExclusive(&l->lock);
}
static void porigin_delivery_begin(porigin_scope *scope,porigin_capture capture,void *user,
                                   const porigin_arguments *a) {
    if(!scope->admitted) return;
    porigin_ledger *l=scope->ledger;unsigned found=UINT_MAX,error=0;
    AcquireSRWLockExclusive(&l->lock);
    for(unsigned i=0;i<l->count;i++) if(l->tickets[i].event==(uintptr_t)a->event) {found=i;break;}
    if(found==UINT_MAX) {++l->unknown;if((uintptr_t)a->receiver==l->binding.player) ++l->failures;}
    else if((uintptr_t)a->source!=l->binding.player) {++l->failures;l->tickets[found].errors|=PO_IDENTITY;}
    else if((uintptr_t)a->receiver!=l->binding.player) ++l->other_receivers;
    else {
        scope->ticket=found;porigin_ticket *t=&l->tickets[found];
        if(t->delivery_state) {++l->failures;t->errors|=PO_DUPLICATE;}
        else {t->delivery_state=1;t->delivery_enter=++l->order;scope->selected=1;}
    }
    ReleaseSRWLockExclusive(&l->lock);
    if(!scope->selected) return;
    porigin_sample s={0};if(!capture(user,PO_DELIVERY,a,&s)) error|=PO_CAPTURE;
    error|=porigin_sample_errors(l,&s);
    if(a->caller!=0x99c5b0 && a->caller!=0x99c614) error|=PO_CALLER;
    AcquireSRWLockExclusive(&l->lock);porigin_ticket *t=&l->tickets[found];
    if(s.controller!=t->controller_before) error|=PO_IDENTITY;
    if(a->time_bits!=t->due || s.tick<t->post_tick) error|=PO_TIME;
    t->delivery_tick=s.tick;t->delivery_clock=s.clock_bits;t->delivery_thread=GetCurrentThreadId();t->transition_before=s.transition_order;
    if(error) {t->errors|=error;++l->failures;}
    ReleaseSRWLockExclusive(&l->lock);
}
static void porigin_delivery_after(porigin_scope *scope,porigin_capture capture,void *user,
                                   const porigin_arguments *a) {
    if(!scope->selected) return;
    porigin_ledger *l=scope->ledger;porigin_sample s={0};unsigned error=0;
    if(!capture(user,PO_AFTER,a,&s)) error|=PO_CAPTURE;
    error|=porigin_sample_errors(l,&s);
    AcquireSRWLockExclusive(&l->lock);porigin_ticket *t=&l->tickets[scope->ticket];
    if(s.tick<t->delivery_tick) error|=PO_TIME;t->controller_after=s.controller;t->after_clock=s.clock_bits;t->after_tick=s.tick;
    t->transition_after=s.transition_order;
    if(error) {t->errors|=error;++l->failures;}
    ReleaseSRWLockExclusive(&l->lock);
}
/* This independent edge never edits/rebinds the existing transition receipt.
 * Invalid or nested excluded delivery scopes cannot inherit their parent's edge. */
static int porigin_link_transition(porigin_ledger *l,uint64_t event) {
    porigin_scope *s=porigin_current;int ok=0;
    if(!s || s->ledger!=l || !s->admitted || !s->selected) return 0;
    AcquireSRWLockExclusive(&l->lock);unsigned error=!event?PO_LINK:0;
    if(s->ticket>=l->count || l->tickets[s->ticket].errors) error|=PO_LINK;
    for(unsigned i=0;i<l->link_count;i++) if(l->links[i].transition==event) error|=PO_LINK;
    if(l->link_count==PORIGIN_LINK_MAX) error|=PO_CAPACITY;
    if(error) {++l->failures;if(s->ticket<l->count) l->tickets[s->ticket].errors|=error;}
    else {l->links[l->link_count++]=(porigin_link){s->ticket+1,event};ok=1;}
    ReleaseSRWLockExclusive(&l->lock);return ok;
}
static uint64_t porigin_observe_post(porigin_ledger *l,porigin_capture capture,void *user,
        porigin_post_fn original,void *receiver,void *event,double due,unsigned char flags,unsigned caller) {
    porigin_scope scope;porigin_enter(l,&scope,PO_POST);
    porigin_arguments a={receiver,event,receiver,porigin_bits(due),caller,flags};
    uint64_t result=0;
    __try {
        porigin_post_begin(&scope,capture,user,&a);
        result=original(receiver,event,due,flags);
    } __finally {
        /* Only observer storage after native event ownership transfer. */
        if(AbnormalTermination()) porigin_unwind(&scope,PO_POST);
        else porigin_leave(&scope,PO_POST,result);
    }
    return result;
}
static uint64_t porigin_observe_delivery(porigin_ledger *l,porigin_capture capture,void *user,
        porigin_delivery_fn original,void *event,void *receiver,void *source,double when,unsigned caller) {
    porigin_scope scope;porigin_enter(l,&scope,PO_DELIVERY);
    porigin_arguments a={receiver,event,source,porigin_bits(when),caller,0};
    uint64_t result=0;
    __try {
        porigin_delivery_begin(&scope,capture,user,&a);
        result=original(event,receiver,source,when); /* original forwarder owns handler */
        porigin_delivery_after(&scope,capture,user,&a);
    } __finally {
        if(AbnormalTermination()) porigin_unwind(&scope,PO_DELIVERY);
        else porigin_leave(&scope,PO_DELIVERY,result);
    }
    return result;
}
/* Read-only completeness, not controller/model success or action authority. */
static int porigin_complete(porigin_ledger *l) {
    int ok;AcquireSRWLockExclusive(&l->lock);
    ok=l->configured && l->closing && !l->open && !l->stop_open && !l->failures && l->count &&
        l->post_calls==l->post_returns && l->delivery_calls==l->delivery_returns;
    for(unsigned i=0;i<l->count;i++) if(!l->tickets[i].posted_returned || l->tickets[i].delivery_state!=2 || l->tickets[i].errors) ok=0;
    ReleaseSRWLockExclusive(&l->lock);return ok;
}
#endif
