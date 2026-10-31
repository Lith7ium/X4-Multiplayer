/* Observer only. A fixture cause/path is a candidate identity, conditional on
 * independently matching parent IRQ/instruction traces. No RNG is written here.
 * Identical repeated causes and ambient cue instances are explicitly unresolved.
 * fixture_trigger_v1 is a pinned-Windows-build diagnostic: caller/type/action
 * RVAs and nested_program_hash's vtable RVAs are not cross-platform identities.
 * A future semantic RNG mode requires canonical native-operation mappings. */
#define NPC_TRIGGER_LIMIT 4096u
#define NPC_CAUSE_LIMIT 8192u
#define NPC_CAUSE_KEYS 512u
enum {NPC_TRIGGER_CANDIDATE=1,NPC_TRIGGER_AMBIENT=2,NPC_TRIGGER_PARAMETER=3,
      NPC_TRIGGER_FOREIGN=4,NPC_TRIGGER_UNREADABLE=5,NPC_TRIGGER_AMBIGUOUS=6,
      NPC_TRIGGER_ACTOR=7,NPC_TRIGGER_OVERFLOW=8};
typedef void (*npc_trigger_fn)(void*);
typedef struct {
    uint64_t tick,irq,actor,key,occurrence,script,pc,begin,end;
    unsigned event,mode,status;
} npc_cause_sample;
typedef struct {
    uint64_t tick,ordinal,parent,key,cause,irq,step,actor,visit,child,program,pc,caller;
    uint64_t parent_key,action_program,action_pc,action_type,action,before,after,action_visits;
    unsigned depth,status,legacy;
    char cue[256];void *native_exec;interrupt_sample *native_irq;
} npc_trigger_sample;
typedef struct npc_trigger_action_context {
    struct npc_trigger_action_context *previous;
    npc_trigger_sample *owner;
    uint64_t visit,children,program,pc,type,action;
    unsigned valid;
} npc_trigger_action_context;
typedef struct {npc_cause_sample cause;char script[96];uint64_t calls;} npc_cause_key;
static npc_cause_sample npc_causes[NPC_CAUSE_LIMIT];
static npc_trigger_sample npc_triggers[NPC_TRIGGER_LIMIT];
static npc_cause_key npc_cause_keys[NPC_CAUSE_KEYS];
static unsigned npc_cause_count,npc_trigger_count;
static uint64_t npc_cause_calls,npc_trigger_calls,npc_cause_key_tick;
static volatile LONG64 npc_trigger_seen,npc_trigger_foreign_thread,npc_trigger_unsupported;
static _Thread_local npc_trigger_sample *npc_trigger_current;
static _Thread_local npc_trigger_action_context *npc_trigger_action_current;
static _Thread_local unsigned npc_trigger_foreign_depth;

static uint64_t npc_trigger_text(const char *text) {
    uint64_t h=14695981039346656037ULL;
    for(size_t i=0;text[i];i++) h=(h^(unsigned char)text[i])*1099511628211ULL;
    return h;
}
static uint64_t npc_trigger_fold(uint64_t h,uint64_t v) {return lockstep_mix(h^v);}
/* Explicit fixed-fixture subset. Integration may delegate to actor_rng_identity
 * only after role/creation binding is proven; never silently hash a dynamic slot. */
static uint64_t npc_trigger_actor(unsigned actor) {return actor>=1 && actor<=12?actor:0;}
static int npc_cause_same(const npc_cause_key *a,const npc_cause_sample *b,const char *script) {
    return a->cause.actor==b->actor && a->cause.tick==b->tick && a->cause.script==b->script &&
        a->cause.pc==b->pc && a->cause.begin==b->begin && a->cause.end==b->end &&
        a->cause.event==b->event && a->cause.mode==b->mode && !strcmp(a->script,script);
}
static void npc_trigger_irq_begin(interrupt_sample *irq) {
    if(!(boot_options&281474976710656ULL)) return;
    irq->trigger_status=NPC_TRIGGER_UNREADABLE;
    if(npc_cause_count==NPC_CAUSE_LIMIT) {InterlockedIncrement64(&interrupt_failures);return;}
    npc_cause_sample *s=&npc_causes[npc_cause_count++];memset(s,0,sizeof(*s));
    ++npc_cause_calls;s->tick=irq->tick;s->irq=irq->ordinal;s->actor=npc_trigger_actor(irq->actor);
    s->script=npc_trigger_text(irq->script);s->pc=irq->pc;s->begin=irq->begin;s->end=irq->end;s->event=irq->event;s->mode=irq->mode;
    s->status=!s->actor?NPC_TRIGGER_ACTOR:(!irq->script[0] || irq->kind!=1?NPC_TRIGGER_UNREADABLE:NPC_TRIGGER_CANDIDATE);
    /* Zero bytes are exact observed data, not a claim about their variant type.
     * Nonzero payloads require separately pinned semantic canonicalization. */
    if(s->status==NPC_TRIGGER_CANDIDATE && (irq->types[0] || irq->types[1] || irq->values[0] || irq->values[1])) s->status=NPC_TRIGGER_PARAMETER;
    if(s->status==NPC_TRIGGER_CANDIDATE) {
        uint64_t h=0x4649585f43415531ULL;
        const uint64_t fields[]={s->tick,s->actor,s->script,s->pc,s->begin,s->end,s->event,s->mode};
        for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) h=npc_trigger_fold(h,fields[i]);
        s->key=h;
        if(npc_cause_key_tick!=s->tick) {npc_cause_key_tick=s->tick;memset(npc_cause_keys,0,sizeof(npc_cause_keys));}
        unsigned first=(unsigned)(h%NPC_CAUSE_KEYS);npc_cause_key *entry=NULL;
        for(unsigned i=0;i<NPC_CAUSE_KEYS;i++) {
            npc_cause_key *candidate=&npc_cause_keys[(first+i)%NPC_CAUSE_KEYS];
            if(!candidate->calls) {entry=candidate;entry->cause=*s;strcpy(entry->script,irq->script);break;}
            if(candidate->cause.key==h) {if(npc_cause_same(candidate,s,irq->script)) entry=candidate;break;}
        }
        if(!entry) {s->status=NPC_TRIGGER_OVERFLOW;InterlockedIncrement64(&interrupt_failures);}
        else {s->occurrence=++entry->calls;if(s->occurrence>1) s->status=NPC_TRIGGER_AMBIGUOUS;}
    }
    irq->trigger_cause=s->key;irq->trigger_occurrence=s->occurrence;irq->trigger_status=s->status;
}
static void npc_trigger_action_begin(npc_trigger_action_context *context,void *node,void *exec) {
    memset(context,0,sizeof(*context));context->previous=npc_trigger_action_current;npc_trigger_action_current=context;
    if(!npc_trigger_current || npc_trigger_foreign_depth || npc_trigger_current->native_exec!=exec) return;
    nested_action_sample action={0};
    if(!nested_action_read(node,exec,&action)) {InterlockedIncrement64(&interrupt_failures);return;}
    context->owner=npc_trigger_current;context->visit=++npc_trigger_current->action_visits;
    context->program=action.program;context->pc=action.pc;context->type=action.type;context->action=action.action;context->valid=1;
}
static void npc_trigger_action_end(npc_trigger_action_context *context) {
    if(npc_trigger_action_current!=context) InterlockedIncrement64(&interrupt_failures);
    npc_trigger_action_current=context->previous;
}
static void npc_trigger_observe(npc_trigger_fn original,void *exec,uint64_t *rng,uint64_t caller) {
    if(!stats.active || boot_phase!=3) {original(exec);return;}
    if(GetCurrentThreadId()!=ui_thread) {
        uint32_t tag=0;
        /* Do not touch the UI-owned cue-name cache on another thread. Unknown
         * reads or any MD context remain explicit missing-coverage evidence. */
        if(!read_local((unsigned char*)exec+0x38,&tag,4) || tag==0x5d) InterlockedIncrement64(&npc_trigger_foreign_thread);
        original(exec);return;
    }
    char cue[256]="unreadable";int named=npc_cue_name(exec,cue);
    if(named==2) {
        ++npc_trigger_foreign_depth;original(exec);--npc_trigger_foreign_depth;return;
    }
    InterlockedIncrement64(&npc_trigger_seen);
    if(npc_trigger_count==NPC_TRIGGER_LIMIT) {InterlockedIncrement64(&interrupt_failures);++npc_trigger_foreign_depth;original(exec);--npc_trigger_foreign_depth;return;}
    npc_trigger_sample *s=&npc_triggers[npc_trigger_count++];memset(s,0,sizeof(*s));
    s->tick=stats.issued;s->ordinal=++npc_trigger_calls;s->caller=caller;s->native_exec=exec;s->native_irq=interrupt_current;
    s->legacy=(boot_options&140737488355328ULL)!=0;strcpy(s->cue,cue);
    npc_trigger_sample *parent=npc_trigger_current;
    s->parent=parent?parent->ordinal:0;s->depth=parent?parent->depth+1:1;
    s->status=NPC_TRIGGER_AMBIENT;
    if(interrupt_current && interrupt_step_active && interrupt_step_active->parent==interrupt_current->ordinal &&
       interrupt_current->tick==s->tick && interrupt_step_active->tick==s->tick) {
        s->irq=interrupt_current->ordinal;s->step=interrupt_step_active->ordinal;s->actor=npc_trigger_actor(interrupt_current->actor);
        s->cause=interrupt_current->trigger_cause;s->status=interrupt_current->trigger_status;
    }
    if(npc_trigger_foreign_depth) s->status=NPC_TRIGGER_FOREIGN;
    if(!named || !rng || s->depth>64 || !caller || caller>=0x6fa9000 || !nested_program_hash(exec,&s->program) ||
       !read_local((unsigned char*)exec+0x18,&s->pc,8) || s->pc>=1048576) s->status=NPC_TRIGGER_UNREADABLE;
    if(s->status==NPC_TRIGGER_CANDIDATE) {
        npc_trigger_action_context *action=npc_trigger_action_current;
        uint64_t parent_key=0;
        if(parent && parent->native_irq==interrupt_current) {
            if(parent->status!=NPC_TRIGGER_CANDIDATE || !action || !action->valid || action->owner!=parent) s->status=NPC_TRIGGER_FOREIGN;
            else {parent_key=parent->key;s->visit=action->visit;s->child=++action->children;
                s->action_program=action->program;s->action_pc=action->pc;s->action_type=action->type;s->action=action->action;}
        } else if(action) s->status=NPC_TRIGGER_FOREIGN;
        else {s->visit=interrupt_step_active->trigger_visit;s->child=++interrupt_step_active->trigger_children;
            s->action_pc=interrupt_step_active->pc;s->action_type=interrupt_step_active->type;s->action=interrupt_step_active->action;}
        if(s->status==NPC_TRIGGER_CANDIDATE) {
            s->parent_key=parent_key;uint64_t h=0x4649585f50415431ULL;
            const uint64_t fields[]={s->cause,parent_key,s->visit,s->child,s->action_program,s->action_pc,s->action_type,s->action,
                npc_trigger_text(cue),s->program,s->pc,s->caller};
            for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) h=npc_trigger_fold(h,fields[i]);s->key=h;
        }
    }
    if(s->status!=NPC_TRIGGER_CANDIDATE) InterlockedIncrement64(&npc_trigger_unsupported);
    npc_provenance_capture(exec,s->tick,s->ordinal);
    s->before=rng?*rng:0;npc_trigger_current=s;original(exec);s->after=rng?*rng:0;
    if(npc_trigger_current!=s) InterlockedIncrement64(&interrupt_failures);npc_trigger_current=parent;
}
