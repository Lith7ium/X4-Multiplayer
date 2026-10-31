/* Prospective ordinary-return receipt. Never consumes copied diagnostic rows.
 * Sequential identity guards are not an atomic native lifetime/owner lease. */
#define TRANSITION_PREBIND_SUFFIX "|PLAYER_TRANSITION_PREBIND_1"
typedef struct {
    unsigned phase,thread,boot,caller;uint64_t tick,clock,event,ticket,parent;
    uintptr_t command;transition_state state;transition_registry_identity registry;
    unsigned char bytes[16];
} transition_prebind_row;
typedef struct {
    unsigned reserved,poison,pending,returned,finalized,count,exported,thread;
    uint64_t parent,event,ticket;uintptr_t command;transition_saved_watch watch;
    transition_prebind_row rows[3];
    uintptr_t image,thunk;uint64_t started;unsigned port,timeout;
    uint64_t action_tick,action_clock,action_ticket,action_dispatch;unsigned action_thread;
    char run[65],digest[65];
} transition_prebind_ledger;
static transition_prebind_ledger transition_prebind;
static void transition_prebind_poison_locked(unsigned reason) {
    if(!transition_prebind.poison) InterlockedIncrement64(&transition_failures);
    transition_prebind.poison|=reason;
}
static int transition_prebind_healthy_locked(void) {
    return !transition_prebind_enabled || (!transition_prebind.poison && transition_prebind.finalized);
}
static void transition_prebind_activation(const transition_event *e) {
    if(!transition_prebind_enabled || e->boot!=2 || e->tick || e->kind!=TR_ACTIVATION) return;
    AcquireSRWLockExclusive(&transition_lock);
    if(transition_prebind.parent || e->arg!=1 || e->errors || e->active_before || transition_closing || !stats.active)
        transition_prebind_poison_locked(1);
    else transition_prebind.parent=transition_index(e);
    ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_prebind_config(const char *run) {
    char preimage[448];uint64_t main_word=0;uintptr_t slot=0;
    if(!run || strlen(run)!=64 || !hash_ok(secret,32,run)) return 0;
    int n=snprintf(preimage,sizeof(preimage),
        "SNAPSHOT_BYTES_1|7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8"
        "|PLAYER_GETUP_LOCAL_PLAN_1:diagnostic-getup-1:1:%s:1:30:120:0000000000011e12:00000000000ae79c"
        "|PLAYER_TRANSITION_QUEUE_DIAG_1" TRANSITION_PREBIND_SUFFIX,run);
    if(n<0 || (size_t)n>=sizeof(preimage) || !hash_ok(preimage,(ULONG)n,initial_state_digest)) return 0;
    int ok=0;AcquireSRWLockExclusive(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    if(!stats.active || !used || !image || !thunk || !boot_mode || GetCurrentThreadId()!=ui_thread ||
       !read_local(image+WORD_RVA,&main_word,8) || main_word!=patched_word || transition_closing ||
       !read_local(image+0x2c39488+0x198,&slot,8) || slot!=(uintptr_t)(image+0xd8dd00) ||
       boot_phase<0 || boot_phase>3 || stats.issued>120 || stats.slot!=1 || boot_options!=70377334178981ULL ||
       transition_saved_count!=1 || transition_saved_last!=120 ||
       strcmp(transition_saved_manifest,"2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player") ||
       transition_failures || transition_saved_failures || transition_saved_queue_failures || transition_dropped || p->poison) goto done;
    if(transition_prebind_enabled) {
        ok=transition_started && transition_enabled && transition_sites==7 && !transition_open &&
           p->image==(uintptr_t)image && p->thunk==(uintptr_t)thunk && p->started==boot_started &&
           p->port==remote_port && p->timeout==timeout_ms && !strcmp(p->run,run) && !strcmp(p->digest,initial_state_digest);
        for(unsigned i=0;ok && i<3;i++) {uint64_t word=0;ok=read_local(image+transition_entries[i],&word,8) && word==transition_patched[i];}
        goto done;
    }
    if(boot_phase || stats.issued || transition_started || transition_saved_bound) goto done;
    memcpy(p->run,run,65);memcpy(p->digest,initial_state_digest,65);p->image=(uintptr_t)image;p->thunk=(uintptr_t)thunk;
    p->started=boot_started;p->port=remote_port;p->timeout=timeout_ms;transition_prebind_enabled=1;ok=1;
done:ReleaseSRWLockExclusive(&transition_lock);return ok;
}
/* Return token means this callback is accounted by this ledger, even on denial.
 * Reservation precedes all borrowed reads and survives until STOP. */
static void *transition_prebind_enqueue(transition_reader read,void *manager,uint64_t runtime,void *command,uintptr_t caller) {
    if(!transition_prebind_enabled) return NULL;
    transition_prebind_ledger *p=&transition_prebind;transition_saved_watch w={0};int candidate=0,duplicate=0;
    AcquireSRWLockExclusive(&transition_lock);
    if(p->reserved && p->command==(uintptr_t)command) {transition_prebind_poison_locked(2);duplicate=1;}
    if(!transition_saved_bound && transition_saved_count==1 && runtime && transition_saved_watches[0].runtime==runtime &&
       caller==(uintptr_t)(image+0xd8dd26)) {
        candidate=1;w=transition_saved_watches[0];if(p->reserved) duplicate=1;
        transition_scope *scope=transition_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(transition_tls);
        transition_event *parent=scope?scope->event:NULL;
        if(p->reserved || p->poison || !command || w.imports!=1 || transition_saved_failures || boot_phase!=2 || stats.issued ||
           transition_closing || !stats.active || !parent || transition_index(parent)!=p->parent ||
           parent->kind!=TR_ACTIVATION || parent->extra!=8 || parent->arg!=1 || parent->errors || parent->active_before ||
           parent->complete || parent->thread!=GetCurrentThreadId()) transition_prebind_poison_locked(4);
        if(!p->reserved) {p->reserved=1;p->pending=1;p->command=(uintptr_t)command;p->watch=w;p->thread=GetCurrentThreadId();}
    }
    int allowed=candidate && !p->poison;ReleaseSRWLockExclusive(&transition_lock);
    if(duplicate) {InterlockedIncrement64(&transition_saved_queue_failures);return (void*)2;}
    if(!candidate) return NULL;
    if(!allowed) {InterlockedIncrement64(&transition_saved_queue_failures);return p;}
    transition_prebind_row row={0};transition_state again;transition_registry_identity registry;uintptr_t expected=0,slot=0;
    unsigned mode=0;int good=transition_model_copy_read(read,command,row.bytes,&mode)==1 && mode==0 &&
        read(image+0x6cf21c8,&expected,8) && expected==(uintptr_t)manager &&
        read(image+0x2c39488+0x198,&slot,8) && slot==(uintptr_t)(image+0xd8dd00) &&
        transition_read_native((void*)w.object,NULL,w.saved,w.runtime,&row.state) &&
        row.state.controller_type==w.initial_type && w.initial_type==0x2c39488 && row.state.owns_agent==1 && row.state.player==1 &&
        transition_saved_guard(&w,&row.state) && transition_registry_read(read,image+0x6cf1500,w.runtime,w.object,&row.registry) &&
        transition_read_native((void*)w.object,NULL,w.saved,w.runtime,&again) && !memcmp(&row.state,&again,sizeof(again)) &&
        transition_saved_guard(&w,&again) && transition_registry_read(read,image+0x6cf1500,w.runtime,w.object,&registry) &&
        !memcmp(&row.registry,&registry,sizeof(registry));
    transition_event *e=transition_begin(TR_MODEL_ENQUEUE,caller);
    if(!e) good=0;
    AcquireSRWLockExclusive(&transition_lock);
    if(!good || !e || e->parent!=p->parent || e->thread!=p->thread || e->boot!=2 || e->tick ||
       p->poison || transition_closing || !stats.active || !boot_mode || boot_phase!=2 || stats.issued ||
       stats.slot!=1 || boot_options!=70377334178981ULL ||
       transition_saved_bound || transition_saved_failures || memcmp(&w,&transition_saved_watches[0],sizeof(w)) ||
       p->image!=(uintptr_t)image || p->thunk!=(uintptr_t)thunk || p->started!=boot_started ||
       p->port!=remote_port || p->timeout!=timeout_ms || strcmp(p->digest,initial_state_digest) ||
       transition_saved_tickets_count>=TRANSITION_QUEUE_MAX) transition_prebind_poison_locked(8);
    if(!p->poison && e) {
        unsigned n=transition_saved_tickets_count;
        transition_saved_ticket *t=&transition_saved_tickets[n];
        *t=(transition_saved_ticket){.runtime=runtime,.event=transition_index(e),.command=(uintptr_t)command,
            .agent=row.state.agent,.state=0,.watch=0,.mode=0};memcpy(t->bytes,row.bytes,16);++transition_saved_tickets_count;
        p->event=transition_index(e);p->ticket=n+1;e->before=row.state;e->queue=e->enqueue=n+1;e->queue_valid=1;
        row.phase=1;row.thread=p->thread;row.boot=2;row.event=p->event;row.ticket=p->ticket;row.parent=p->parent;
        row.caller=0xd8dd26;row.command=p->command;row.clock=e->enter;p->rows[0]=row;p->count=1;
    } else if(e) e->errors|=32;
    ReleaseSRWLockExclusive(&transition_lock);
    if(e) transition_end(e);else InterlockedIncrement64(&transition_saved_queue_failures);
    return p;
}
static void transition_prebind_ack(void) {
    AcquireSRWLockExclusive(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    if(!p->reserved || !p->pending || p->returned || p->poison || p->count!=1 || transition_closing || !stats.active ||
       boot_phase!=2 || stats.issued || p->thread!=GetCurrentThreadId() || !p->event ||
       !transition_events[p->event-1].complete || transition_events[p->event-1].errors) transition_prebind_poison_locked(16);
    p->pending=0;++p->returned;
    if(!p->poison) {transition_prebind_row row=p->rows[0];row.phase=2;row.caller=0;
        row.clock=(uint64_t)InterlockedCompareExchange64(&transition_order,0,0);p->rows[1]=row;p->count=2;}
    ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_prebind_bind_locked(const transition_saved_watch *w,unsigned count,const transition_registry_identity *r) {
    if(!transition_prebind_enabled) return 1;
    transition_prebind_ledger *p=&transition_prebind;transition_event *parent=p->parent && p->parent<=TRANSITION_MAX && p->parent<=(uint64_t)transition_count?&transition_events[p->parent-1]:NULL;
    if(p->poison || count!=1 || p->count!=2 || p->pending || p->returned!=1 || p->finalized || transition_open!=1 ||
       !boot_mode || boot_phase!=3 || stats.issued || stats.slot!=1 || boot_options!=70377334178981ULL ||
       p->image!=(uintptr_t)image || p->thunk!=(uintptr_t)thunk || p->started!=boot_started || p->port!=remote_port ||
       p->timeout!=timeout_ms || strcmp(p->digest,initial_state_digest) ||
       !parent || !parent->complete || parent->errors || parent->kind!=TR_ACTIVATION || parent->boot!=2 || parent->tick ||
       parent->arg!=1 || parent->extra!=8 || parent->active_before || parent->active_after!=1 || parent->thread!=p->thread ||
       !(parent->enter<p->rows[0].clock && p->rows[0].clock<p->rows[1].clock && p->rows[1].clock<parent->leave) ||
       memcmp(&w[0].initial,&p->rows[0].state,sizeof(w[0].initial)) || memcmp(&r[0],&p->rows[0].registry,sizeof(r[0])) ||
       p->ticket!=1 || transition_saved_tickets_count!=1 || transition_saved_tickets[0].state!=0 ||
       memcmp(&transition_saved_watches[0],&p->watch,sizeof(p->watch))) {transition_prebind_poison_locked(32);return 0;}
    transition_prebind_row row=p->rows[0];row.phase=3;row.caller=0;row.boot=3;row.thread=GetCurrentThreadId();
    row.clock=(uint64_t)InterlockedCompareExchange64(&transition_order,0,0);row.state=w[0].initial;row.registry=r[0];
    if(row.clock<parent->leave) {transition_prebind_poison_locked(32);return 0;}
    p->rows[2]=row;p->count=3;p->finalized=1;transition_saved_tickets[0].state=1;
    return 1; /* caller publishes canonical watch under this same held lock */
}
static void transition_prebind_dispatch(void *command) {
    if(!transition_prebind_enabled) return;
    AcquireSRWLockExclusive(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    if(p->reserved && p->command==(uintptr_t)command &&
       (!p->finalized || p->poison || p->pending || p->returned!=1 || !p->ticket || p->ticket>transition_saved_tickets_count ||
        transition_saved_tickets[p->ticket-1].command!=p->command || transition_saved_tickets[p->ticket-1].state!=1))
        transition_prebind_poison_locked(64);
    ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_prebind_ticket_good(unsigned number) {
    if(!transition_prebind_enabled) return 1;
    AcquireSRWLockShared(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    int related=number && number<=transition_saved_tickets_count && transition_saved_tickets[number-1].command==p->command;
    int ok=!related || (number==p->ticket && p->finalized && !p->poison && !p->pending && p->returned==1);
    ReleaseSRWLockShared(&transition_lock);return ok;
}
/* Initialization must have returned before the later public action. This is a
 * local observer prerequisite, not a model-effect or native lifetime lease. */
static int transition_prebind_action_check(uint64_t tick,int record) {
    if(!transition_prebind_enabled) return 1;
    AcquireSRWLockExclusive(&transition_lock);transition_prebind_ledger *p=&transition_prebind;
    int ok=tick==30 && stats.active && !transition_closing && !transition_open && !transition_failures &&
        !p->poison && p->finalized && p->count==3 && !p->pending && p->returned==1 &&
        p->ticket && p->ticket<=transition_saved_tickets_count;
    if(ok) {
        transition_saved_ticket *t=&transition_saved_tickets[p->ticket-1];
        ok=t->state==3 && t->command==p->command && t->event==p->event && t->dispatch &&
            t->dispatch<=TRANSITION_MAX && t->dispatch<=(uint64_t)transition_count;
        if(ok) {
            transition_event *e=&transition_events[t->dispatch-1];
            ok=InterlockedCompareExchange(&e->complete,0,0) && e->kind==TR_MODEL_DISPATCH && e->boot==3 &&
                e->tick && e->tick<tick && e->leave>e->enter && !e->errors && !e->queue_errors &&
                e->queue_valid && e->queue==p->ticket && e->enqueue==p->ticket;
        }
    }
    if(record) {
        if(p->action_tick) ok=0;
        if(ok) {
            p->action_tick=tick;p->action_clock=(uint64_t)InterlockedCompareExchange64(&transition_order,0,0);
            p->action_thread=GetCurrentThreadId();p->action_ticket=p->ticket;
            p->action_dispatch=transition_saved_tickets[p->ticket-1].dispatch;
        }
    }
    ReleaseSRWLockExclusive(&transition_lock);return ok;
}
