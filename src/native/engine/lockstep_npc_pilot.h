/* Selected saved-pilot observation. No actor enrollment, native Value decoding,
 * RNG scope or engine call is introduced. Local context tokens are not identity. */
#define NPC_PILOT_SAVED 0x976abULL
#define NPC_PILOT_LIMIT 4096u
#define NPC_PILOT_CONTEXTS 1024u
#define NPC_PILOT_DEPTH 64u
typedef struct {uintptr_t context;uint64_t id,token;} npc_pilot_context;
typedef struct {
    uint64_t tick,end_tick,ordinal,nest,enter,exit,context,pc_before,pc_after,rng_before,rng_after,begin,end;
    uint64_t parent_saved,parent_pc,parent_caller;
    interrupt_origin irq_origin;
    unsigned kind,depth,mode,event,types[2],flags_before,flags_after,errors,after_status,returned;
    unsigned parent_kind,parent_status,parent_self,parent_caller_source;
    char script[288],script_after[288],parent_name[256];
} npc_pilot_sample;
typedef struct npc_pilot_scope {
    struct npc_pilot_scope *previous;npc_pilot_sample *sample;
    uintptr_t context,exec,value,entity;uint64_t id,entity_id;
} npc_pilot_scope;
static npc_pilot_sample npc_pilots[NPC_PILOT_LIMIT];
static npc_pilot_context npc_pilot_contexts[NPC_PILOT_CONTEXTS];
static unsigned npc_pilot_count,npc_pilot_context_count;
static int npc_pilot_enabled;
static volatile LONG64 npc_pilot_calls,npc_pilot_failures,npc_pilot_overflows,npc_pilot_foreign,npc_pilot_unmapped,npc_pilot_unreadable;
static uint64_t npc_pilot_retired,npc_pilot_after_unresolved,npc_pilot_sequence;
static _Thread_local npc_pilot_scope *npc_pilot_current;
static _Thread_local unsigned npc_pilot_overflow_depth;
static int npc_pilot_configure(uint64_t saved) {
    int ok=saved==NPC_PILOT_SAVED && boot_mode && stats.active &&
        GetCurrentThreadId()==ui_thread && (boot_options&281474976710656ULL) && npc_provenance_enabled &&
        world_identity_installed && interrupt_installed && interrupt_event_installed;
    if(!ok) return 0;
    if(npc_pilot_enabled) return interrupt_origin_ready; /* Same immutable watch survives a UI reload. */
    if(boot_phase!=0 || stats.issued || npc_pilot_calls) return 0;
    if(!interrupt_origin_ready && !interrupt_origin_prepare()) return 0;
    npc_pilot_enabled=1;return 1;
}
static int npc_pilot_clear(void) {
    if(GetCurrentThreadId()!=ui_thread || npc_pilot_current || npc_pilot_overflow_depth) return 0;
    npc_pilot_count=0;return 1;
}
static void npc_pilot_failure(void) {InterlockedIncrement64(&npc_pilot_failures);InterlockedIncrement64(&interrupt_failures);}
static uint64_t npc_pilot_token(uintptr_t context,uint64_t id) {
    for(unsigned i=0;i<npc_pilot_context_count;i++) {
        npc_pilot_context *entry=&npc_pilot_contexts[i];
        if(entry->context==context && entry->id==id) return entry->token;
        if(entry->id==id && entry->context!=context) return 0;
    }
    if(npc_pilot_context_count==NPC_PILOT_CONTEXTS) {InterlockedIncrement64(&npc_pilot_overflows);return 0;}
    npc_pilot_context *entry=&npc_pilot_contexts[npc_pilot_context_count++];
    entry->context=context;entry->id=id;entry->token=npc_pilot_context_count;return entry->token;
}
static void npc_pilot_parent(npc_pilot_sample *s,uintptr_t exec) {
    uintptr_t current=0,again=0;strcpy(s->parent_name,"none");
    if(!read_local(image+NPC_PROVENANCE_CURRENT,&current,8)) {s->errors|=4;return;}
    if(!current) return;
    s->parent_self=current==exec;npc_provenance_guard guard;
    if(!npc_provenance_guard_read((void*)current,&guard)) {s->parent_status=3;s->errors|=4;return;}
    s->parent_kind=guard.tag==0x66?2:guard.tag==0x5d?1:3;s->parent_status=2;
    if(npc_provenance_current_matches(current)) {
        s->parent_status=1;s->parent_caller=npc_provenance_current->caller;
        s->parent_caller_source=npc_provenance_current->caller_source;
    }
    if(guard.tag==0x66) {
        char raw[96];uintptr_t entity=0,entity_again=0;uint64_t id=0,id_again=0;
        if(!ai_script((void*)guard.context,raw) || !npc_encode(raw,s->parent_name,sizeof(s->parent_name)) ||
           !read_local((void*)(guard.context+0x120),&entity,8) || !entity || !read_local((void*)(entity+8),&id,8) ||
           !read_local((void*)(guard.context+0x120),&entity_again,8) || entity_again!=entity ||
           !read_local((void*)(entity+8),&id_again,8) || id_again!=id) s->errors|=4;
        else s->parent_saved=world_identity_lookup(id);
    } else if(guard.tag==0x5d) {if(npc_cue_name_ex((void*)current,s->parent_name,0)!=1) s->errors|=4;}
    else strcpy(s->parent_name,"unsupported");
    uint64_t pc_again=0;
    if(!read_local((void*)(current+0x18),&s->parent_pc,8) || !npc_provenance_guard_same(&guard) ||
       !read_local((void*)(current+0x18),&pc_again,8) || pc_again!=s->parent_pc ||
       !read_local(image+NPC_PROVENANCE_CURRENT,&again,8) || again!=current) s->errors|=4;
}
static int npc_pilot_begin(npc_pilot_scope *scope,void *exec,void *event_context,unsigned kind,uint64_t *rng,
                           uint64_t begin,uint64_t end,unsigned mode) {
    if(!npc_pilot_enabled || !(boot_options&281474976710656ULL) || !stats.active || boot_phase!=3) return 0;
    memset(scope,0,sizeof(*scope));scope->exec=(uintptr_t)exec;
    unsigned tag=0;
    if(kind==3) scope->context=(uintptr_t)event_context;
    else {
        if(!exec || !read_local((unsigned char*)exec+0x38,&tag,4)) {InterlockedIncrement64(&npc_pilot_unreadable);return 0;}
        if(tag!=0x66) return 0;
        if(!read_local((unsigned char*)exec+0x40,&scope->value,8) || scope->value<0xa8) {InterlockedIncrement64(&npc_pilot_unreadable);return 0;}
        scope->context=scope->value-0xa8;
    }
    if(!scope->context || !read_local((void*)(scope->context+8),&scope->id,8) || !scope->id ||
       !read_local((void*)(scope->context+0x120),&scope->entity,8) || !scope->entity ||
       !read_local((void*)(scope->entity+8),&scope->entity_id,8) || !scope->entity_id) {InterlockedIncrement64(&npc_pilot_unreadable);return 0;}
    uint64_t saved=world_identity_lookup(scope->entity_id);
    if(!saved) {InterlockedIncrement64(&npc_pilot_unmapped);return 0;}
    if(saved!=NPC_PILOT_SAVED) return 0;
    if(GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&npc_pilot_foreign);return 0;}
    if(npc_pilot_overflow_depth || (npc_pilot_current && npc_pilot_current->sample->depth>=NPC_PILOT_DEPTH)) {
        ++npc_pilot_overflow_depth;InterlockedIncrement64(&npc_pilot_overflows);npc_pilot_failure();return 2;
    }
    if(npc_pilot_count==NPC_PILOT_LIMIT) {InterlockedIncrement64(&npc_pilot_overflows);npc_pilot_failure();return 0;}
    npc_pilot_sample *s=&npc_pilots[npc_pilot_count++];memset(s,0,sizeof(*s));scope->sample=s;
    scope->previous=npc_pilot_current;s->depth=scope->previous?scope->previous->sample->depth+1:1;
    s->nest=scope->previous?scope->previous->sample->ordinal:0;s->tick=stats.issued;s->ordinal=(uint64_t)InterlockedIncrement64(&npc_pilot_calls);
    s->enter=++npc_pilot_sequence;
    s->kind=kind;s->begin=begin;s->end=end;s->mode=mode;s->rng_before=rng?*rng:0;
    s->context=npc_pilot_token(scope->context,scope->id);strcpy(s->script,"unreadable");strcpy(s->script_after,"unresolved");
    char raw[96];uint64_t id_again=0,entity_id_again=0;uintptr_t entity_again=0,value_again=0;unsigned tag_again=0;
    uintptr_t pc=exec?(uintptr_t)exec+0x18:scope->context+0x140;
    if(!s->context || !rng || !ai_script((void*)scope->context,raw) || !npc_encode(raw,s->script,sizeof(s->script)) ||
       !read_local((void*)pc,&s->pc_before,8) || !read_local((void*)(scope->context+0x18),&s->event,4) ||
       !read_local((void*)(scope->context+0x20),&s->types[0],4) || !read_local((void*)(scope->context+0x30),&s->types[1],4) ||
       !read_local((void*)(scope->context+0x188),&s->flags_before,4)) s->errors|=1;
    npc_pilot_parent(s,(uintptr_t)exec);
    if(!read_local((void*)(scope->context+8),&id_again,8) || id_again!=scope->id ||
       !read_local((void*)(scope->context+0x120),&entity_again,8) || entity_again!=scope->entity ||
       !read_local((void*)(scope->entity+8),&entity_id_again,8) || entity_id_again!=scope->entity_id) s->errors|=2;
    if(exec && (!read_local((unsigned char*)exec+0x38,&tag_again,4) || tag_again!=0x66 ||
       !read_local((unsigned char*)exec+0x40,&value_again,8) || value_again!=scope->value)) s->errors|=2;
    uint64_t pc_again=0;unsigned event_again=0,types_again[2]={0},flags_again=0;
    if(!read_local((void*)pc,&pc_again,8) || pc_again!=s->pc_before ||
       !read_local((void*)(scope->context+0x18),&event_again,4) || event_again!=s->event ||
       !read_local((void*)(scope->context+0x20),&types_again[0],4) || types_again[0]!=s->types[0] ||
       !read_local((void*)(scope->context+0x30),&types_again[1],4) || types_again[1]!=s->types[1] ||
       !read_local((void*)(scope->context+0x188),&flags_again,4) || flags_again!=s->flags_before) s->errors|=2;
    if(s->errors) npc_pilot_failure();npc_pilot_current=scope;return 1;
}
static void npc_pilot_end(npc_pilot_scope *scope,int entered,uint64_t *rng) {
    if(entered==2) {if(npc_pilot_overflow_depth)--npc_pilot_overflow_depth;else npc_pilot_failure();return;}
    if(!entered) return;
    npc_pilot_sample *s=scope->sample;s->end_tick=stats.issued;s->exit=++npc_pilot_sequence;s->returned=1;s->rng_after=rng?*rng:0;
    if(npc_pilot_current!=scope || !rng || s->end_tick!=s->tick) {s->errors|=8;npc_pilot_failure();}
    npc_pilot_current=scope->previous;
    uint64_t id=0,entity_id=0;uintptr_t value=0,entity=0;unsigned tag=0;char raw[96];
    if(!read_local((void*)(scope->context+8),&id,8)) s->after_status=4;
    else if(id!=scope->id) s->after_status=2;
    else if(scope->exec && (!read_local((void*)(scope->exec+0x38),&tag,4) || !read_local((void*)(scope->exec+0x40),&value,8))) s->after_status=4;
    else if(scope->exec && (tag!=0x66 || value!=scope->value)) s->after_status=3;
    else if(!read_local((void*)(scope->context+0x120),&entity,8) || !read_local((void*)(scope->entity+8),&entity_id,8)) s->after_status=4;
    else if(entity!=scope->entity || entity_id!=scope->entity_id) s->after_status=5;
    else {
        uintptr_t pc=scope->exec?scope->exec+0x18:scope->context+0x140;uint64_t pc_again=0;
        if(!read_local((void*)pc,&s->pc_after,8) || !read_local((void*)(scope->context+0x188),&s->flags_after,4) ||
           !ai_script((void*)scope->context,raw) || !npc_encode(raw,s->script_after,sizeof(s->script_after)) ||
           !read_local((void*)(scope->context+8),&id,8) || id!=scope->id ||
           !read_local((void*)pc,&pc_again,8) || pc_again!=s->pc_after ||
           !read_local((void*)(scope->context+0x120),&entity,8) || entity!=scope->entity ||
           !read_local((void*)(scope->entity+8),&entity_id,8) || entity_id!=scope->entity_id ||
           (scope->exec && (!read_local((void*)(scope->exec+0x38),&tag,4) || tag!=0x66 ||
                           !read_local((void*)(scope->exec+0x40),&value,8) || value!=scope->value))) s->after_status=4;
        else s->after_status=1;
    }
    if(s->after_status!=1) {s->pc_after=0;s->flags_after=0;strcpy(s->script_after,"unresolved");}
    if(s->after_status==2 || s->after_status==3) ++npc_pilot_retired;
    if(s->after_status==4 || s->after_status==5) ++npc_pilot_after_unresolved;
}
