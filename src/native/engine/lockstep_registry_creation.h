/* Provisional creation evidence only. This module never writes an actor array,
 * cache, native object, timer, RNG, prepared tick or readiness flag. */
typedef struct {
    uint64_t sequence,md_anchor,creation,creation_enter,creation_leave,tick,handle;
    uint64_t object,runtime,owner,macro,lookup_first,lookup_second,controller,agent,slots,map_before,map_after;
    uint64_t pending_order;unsigned pending_kind;
    unsigned edge,thread,error,lookup,admitted,relationship,open_scopes,creation_error,complete,closed;
    registry_origin_identity presence;
} registry_creation_identity_record;
typedef struct {
    uint64_t enter,leave,md_enter,md_leave,tick,handle,object,runtime,owner,caller,verified;
    uint64_t context,exec,program,begin,end,result,script_hash,creation;
    unsigned kind,source,thread,error,paired,closed,normal,unwound,stop_overlap,mode,classification;
    unsigned identity_at_entry,captured;
    char script[96];
} registry_creation_operation;
static SRWLOCK registry_creation_lock=SRWLOCK_INIT;
static volatile LONG registry_creation_configured;
static unsigned registry_creation_closing,registry_creation_identity_count,registry_creation_operation_count;
static uint64_t registry_creation_sequence,registry_creation_failures,registry_creation_dropped;
static unsigned registry_creation_open,registry_creation_peak,registry_creation_stop_open;
static unsigned registry_creation_identity_open,registry_creation_identity_stop_open;
static unsigned registry_creation_unpaired,registry_creation_workers,registry_creation_kinds,registry_creation_identity_admitted;
static uint64_t registry_creation_handle,registry_creation_runtime,registry_creation_object,registry_creation_owner;
static uint64_t registry_creation_tick;static unsigned registry_creation_thread;
static char registry_creation_digest[65],registry_creation_rng_manifest[CREATION_RNG_MANIFEST_CAP];
static unsigned registry_creation_ack_mode,registry_creation_acks,registry_creation_ack_phase;
static registry_creation_identity_record registry_creation_identities[REGISTRY_CREATION_IDENTITIES];
static registry_creation_operation registry_creation_operations[REGISTRY_CREATION_OPERATIONS];

static uint64_t registry_creation_anchor(void) {
    return (uint64_t)InterlockedCompareExchange64(&registry_md_order,0,0);
}
static void registry_creation_error_locked(unsigned error) {
    if(error){++registry_creation_failures;registry_creation_identity_admitted=0;}
}
static int registry_creation_extended(const char *text,size_t length,const char *manifest,size_t manifest_length) {
    size_t suffix=sizeof(REGISTRY_CREATION_SUFFIX)-1;
    return text && length>suffix && !memcmp(text+length-suffix,REGISTRY_CREATION_SUFFIX,suffix) &&
        creation_rng_preimage(text,length-suffix,manifest,manifest_length);
}
static unsigned registry_creation_extended_mode(const char *text,size_t length,const char *manifest,size_t manifest_length) {
    if(registry_creation_extended(text,length,manifest,manifest_length))return 1;
    size_t suffix=sizeof(REGISTRY_CREATION_SUFFIX)-1;
    return text && length>suffix && !memcmp(text+length-suffix,REGISTRY_CREATION_SUFFIX,suffix)?
        creation_rng_preimage_mode(text,length-suffix,manifest,manifest_length):0;
}
static int registry_creation_attachment_snapshot(registry_creation_attachment *out) {
    AcquireSRWLockShared(&registry_creation_lock);
    *out=(registry_creation_attachment){(unsigned)registry_creation_configured,registry_creation_ack_mode,
        registry_creation_acks,registry_creation_ack_phase,registry_creation_closing};
    int ok=!registry_creation_configured || (!registry_creation_closing && !registry_creation_sequence &&
        !registry_creation_failures && !registry_creation_open && !registry_creation_identity_open);
    ReleaseSRWLockShared(&registry_creation_lock);return ok;
}
static int registry_creation_rng_preimage(const char *text,size_t length,const char *manifest,size_t manifest_length) {
    unsigned mode=registry_creation_extended_mode(text,length,manifest,manifest_length);
    if(!InterlockedCompareExchange(&registry_creation_configured,0,0) ||
       !mode)return 0;
    AcquireSRWLockShared(&registry_creation_lock);
    int ok=!registry_creation_closing && !registry_creation_sequence && !registry_creation_failures &&
        strlen(registry_creation_rng_manifest)==manifest_length && !memcmp(registry_creation_rng_manifest,manifest,manifest_length) &&
        !strcmp(registry_creation_digest,initial_state_digest) && registry_creation_ack_mode==mode;
    ReleaseSRWLockShared(&registry_creation_lock);return ok?(int)mode:0;
}
static int registry_creation_configure(const char *marker,size_t marker_length,const char *manifest,size_t manifest_length,
        const char *preimage,size_t preimage_length) {
    uint32_t epoch=0;unsigned mode=registry_creation_extended_mode(preimage,preimage_length,manifest,manifest_length);
    if(!marker || marker_length!=sizeof(REGISTRY_CREATION_MANIFEST)-1 || memcmp(marker,REGISTRY_CREATION_MANIFEST,marker_length) ||
       !creation_rng_parse(manifest,manifest_length,&epoch) || !mode ||
       !hash_ok(preimage,preimage_length,initial_state_digest) || !registry_pins() ||
       !hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36")) {creation_attach_reject();return 0;}
    registry_creation_attachment prior={0};
    if(!registry_creation_attachment_snapshot(&prior)){creation_attach_reject();return 0;}
    AcquireSRWLockShared(&creation_lock);
    int ok=stats.active && used && boot_mode && !stats.issued && GetCurrentThreadId()==ui_thread &&
        creation_enabled && !creation_closing && !creation_count && !creation_selected.state && !creation_failures &&
        !creation_rng_count && !creation_rng_active && !creation_rng_attempted && !creation_rng_closing &&
        registry_origin_enabled && !registry_origin_closing && !registry_failures && !registry_origin_failures &&
        !registry_md_failures && creation_rng_composition() && creation_rng_pins_locked();
    unsigned phase=(unsigned)boot_phase,expected_rng=creation_rng_started?creation_rng_acks:0;
    if(mode==1)ok=ok && phase==0;
    else ok=ok && !creation_attach_rejected && !creation_open && !creation_restore_failures && !creation_restore_open &&
        !creation_rng_restore_failures && !creation_rng_restore_open &&
        (prior.configured?(prior.mode==2 && prior.acks==expected_rng && phase>=1 && phase<=3 &&
            phase>=prior.phase && phase>=creation_rng_ack_phase && creation_rng_boot_matches(2,epoch,manifest,manifest_length) &&
            creation_attach_matches(manifest,manifest_length,preimage,preimage_length,epoch,1)):
            (!creation_rng_started && phase==0 && !prior.acks));
    ReleaseSRWLockShared(&creation_lock);
    /* Prepared ACK2 identity may be published only under the exclusive creation
     * lock. Registry lock is not held, and no native original is called. */
    if(ok && mode==2 && !prior.configured) {
        AcquireSRWLockExclusive(&creation_lock);
        ok=creation_attach_prepare(manifest,manifest_length,preimage,preimage_length,epoch,1);
        ReleaseSRWLockExclusive(&creation_lock);
    }
    AcquireSRWLockExclusive(&registry_creation_lock);
    unsigned accepted_phase=(unsigned)boot_phase;
    ok=ok && stats.active && !stats.issued && !registry_creation_closing && !registry_creation_sequence && !registry_creation_failures;
    if(mode==1)ok=ok && accepted_phase==0;
    else ok=ok && !registry_creation_open && !registry_creation_identity_open &&
        (unsigned)registry_creation_configured==prior.configured && registry_creation_acks==expected_rng && expected_rng<CREATION_RNG_ACK_LIMIT &&
        (prior.configured?(accepted_phase>=phase && accepted_phase<=3 && accepted_phase>=1):accepted_phase==0);
    if(registry_creation_configured)ok=ok && !strcmp(registry_creation_digest,initial_state_digest) &&
        strlen(registry_creation_rng_manifest)==manifest_length && !memcmp(registry_creation_rng_manifest,manifest,manifest_length) &&
        registry_creation_ack_mode==mode;
    else if(ok) {
        strcpy(registry_creation_digest,initial_state_digest);
        memcpy(registry_creation_rng_manifest,manifest,manifest_length);registry_creation_rng_manifest[manifest_length]=0;
        registry_creation_ack_mode=mode;
        InterlockedExchange(&registry_creation_configured,1);
    }
    if(ok && mode==2){++registry_creation_acks;registry_creation_ack_phase=accepted_phase;}
    ReleaseSRWLockExclusive(&registry_creation_lock);
    if(!ok && mode==2)creation_attach_reject();
    return ok;
}

/* Only exact native registry-load read AVs from this added diagnostic lookup
 * are recoverable. All other exceptions reach the enclosing creation finally. */
static LONG registry_creation_query_filter(const EXCEPTION_POINTERS *p) {
    if(!p || !p->ExceptionRecord || !p->ContextRecord)return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD *r=p->ExceptionRecord;
    if(r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || r->NumberParameters<2 || r->ExceptionInformation[0]!=0 ||
       (uintptr_t)r->ExceptionAddress!=(uintptr_t)p->ContextRecord->Rip)return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t pc=(uintptr_t)r->ExceptionAddress,base=(uintptr_t)image;
    return pc==base+0xceda1 || pc==base+0xcedcb || pc==base+0xcedd8 || pc==base+0xcedfc?
        EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
static void *registry_creation_query(snapshot_find_fn find,void *map,uint64_t runtime,unsigned *fault) {
    void *result=NULL;
    __try {result=find(map,runtime,4);}
    __except(registry_creation_query_filter(GetExceptionInformation())) {*fault=1;}
    return result;
}
static void registry_creation_identity_read(registry_creation_identity_record *r,const creation_token *t,
        snapshot_find_fn find,void *map) {
    r->object=t->object;r->runtime=t->runtime;r->owner=t->true_owner;
    if(!find || !map || !read_local(image+0x6cf1500,&r->map_before,8) || r->map_before!=(uintptr_t)map){r->lookup=4;r->error|=RC_LOOKUP;return;}
    unsigned fault=0;void *first=registry_creation_query(find,map,t->runtime,&fault);
    r->lookup_first=(uintptr_t)first;
    if(fault){r->lookup=5;r->error|=RC_LOOKUP;return;}
    r->lookup=!first?1:first!=(void*)(uintptr_t)t->object?3:2;
    registry_origin_identity_capture(&r->presence,(void*)(uintptr_t)t->object,t->runtime);
    const char *why=NULL;
    if(r->presence.stable!=1 || r->presence.first.object_state!=PRESENCE_PRESENT ||
       r->presence.first.type!=0x2b99a88 || r->presence.first.owner_state!=PRESENCE_PRESENT ||
       r->presence.first.owner!=t->true_owner || !t->true_owner ||
       !registry_macro_detail((void*)(uintptr_t)t->object,CREATION_RNG_MACRO,&why) ||
       !registry_owner_detail((void*)(uintptr_t)t->object,"player",&why) ||
       !creation_read(t->object,0x60,&r->macro,8))r->error|=RC_IDENTITY;
    r->controller=r->presence.first.controller;r->agent=r->presence.first.agent;r->slots=r->presence.first.slots;
    void *second=registry_creation_query(find,map,t->runtime,&fault);r->lookup_second=(uintptr_t)second;
    if(fault){r->lookup=5;r->error|=RC_LOOKUP;return;}
    if(r->lookup!=2 || second!=first){r->error|=RC_LOOKUP;if(second!=first)r->lookup=3;}
    registry_origin_identity again;registry_origin_identity_capture(&again,(void*)(uintptr_t)t->object,t->runtime);
    if(memcmp(&again,&r->presence,sizeof(again)))r->error|=RC_LIFETIME;
    if(!read_local(image+0x6cf1500,&r->map_after,8) || r->map_after!=r->map_before)r->error|=RC_LOOKUP;
    if(r->edge==3) {
        r->relationship=r->presence.first.controller_state==PRESENCE_PRESENT && r->presence.first.agent_state==PRESENCE_PRESENT &&
            r->controller==t->controller && r->agent==t->agent && r->slots==t->agent+0xad0;
        if(!r->relationship)r->error|=RC_IDENTITY;
    }
}

static void registry_creation_trace_using(unsigned index,unsigned edge,snapshot_find_fn find,void *map) {
    if(!InterlockedCompareExchange(&registry_creation_configured,0,0) || !index)return;
    if(edge==2) {
        DWORD last=GetLastError();AcquireSRWLockExclusive(&registry_creation_lock);
        for(unsigned i=0;i<registry_creation_identity_count;i++)if(registry_creation_identities[i].creation==index && !registry_creation_identities[i].closed) {
            registry_creation_identities[i].error|=RC_UNWIND;registry_creation_identities[i].closed=1;
            if(registry_creation_identity_open)--registry_creation_identity_open;
        }
        registry_creation_error_locked(RC_UNWIND);ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);return;
    }
    DWORD last=GetLastError();creation_record c;creation_token t;int exists=0,parent_valid=0;
    AcquireSRWLockShared(&creation_lock);
    if(index<=creation_count){c=creation_records[index-1];t=creation_selected;exists=1;
        if(c.parent && c.parent<=creation_count) {
            const creation_record *p=&creation_records[c.parent-1];
            if(p->kind==CREATION_INITIALIZE && p->parent && p->parent<=creation_count) {
                const creation_record *g=&creation_records[p->parent-1];
                parent_valid=g->kind==CREATION_GENERATE && !g->parent && p->handle==c.handle && g->handle==c.handle && !p->complete && !g->complete;
            }
        }
    }
    ReleaseSRWLockShared(&creation_lock);if(!exists){SetLastError(last);return;}
    uint64_t pending_order=0;unsigned pending_kind=0;
    if(c.kind==CREATION_PILOT && edge==0 && t.runtime) {
        AcquireSRWLockShared(&registry_lock);
        for(unsigned i=0;i<registry_pending_count;i++)if(registry_pending[i].runtime==t.runtime) {
            pending_order=registry_pending[i].order;pending_kind=registry_pending[i].kind;break;
        }
        ReleaseSRWLockShared(&registry_lock);
    }
    AcquireSRWLockExclusive(&registry_creation_lock);
    if(registry_creation_closing || !stats.active){ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);return;}
    if(!registry_creation_handle && c.kind==CREATION_GENERATE && edge==0 && !c.error && !c.parent && t.handle==0x500000003ULL) {
        registry_creation_handle=t.handle;registry_creation_tick=t.execution_tick;registry_creation_thread=t.thread;
    }
    if(!registry_creation_handle || c.handle!=registry_creation_handle){ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);return;}
    if(t.object) {
        if(registry_creation_object && (registry_creation_object!=t.object || registry_creation_runtime!=t.runtime))registry_creation_error_locked(RC_LIFETIME);
        else {registry_creation_object=t.object;registry_creation_runtime=t.runtime;}
    }
    if(t.true_owner) {
        if(registry_creation_owner && registry_creation_owner!=t.true_owner)registry_creation_error_locked(RC_LIFETIME);
        else registry_creation_owner=t.true_owner;
    }
    int selected=c.subject==1 && !c.foreign;
    unsigned stage=c.kind==CREATION_PILOT && selected?(edge?1:0):
        c.kind==CREATION_GENERATE && edge==1?2:c.kind==CREATION_SETTER && selected && c.stage==2 && edge==1?3:4;
    if(stage==4){ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);return;}
    registry_creation_identity_record r={0};r.edge=stage;r.sequence=++registry_creation_sequence;r.md_anchor=registry_creation_anchor();
    r.creation=index;r.creation_enter=c.enter;r.creation_leave=c.leave;r.tick=stats.issued;r.thread=GetCurrentThreadId();
    r.handle=t.handle;r.open_scopes=registry_creation_open;r.creation_error=c.error;
    r.pending_order=pending_order;r.pending_kind=pending_kind;
    if(stage!=registry_creation_identity_count || stage>=REGISTRY_CREATION_IDENTITIES) {
        registry_creation_error_locked(RC_DUPLICATE);++registry_creation_dropped;ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);return;
    }
    ++registry_creation_identity_count;
    if(c.error || t.execution_tick!=stats.issued || t.thread!=r.thread || t.handle!=registry_creation_handle)r.error|=RC_SCOPE;
    if(stage==0 && (c.kind!=CREATION_PILOT || c.caller!=0xc8224f || !parent_valid || pending_kind || registry_creation_operation_count))r.error|=RC_SCOPE;
    r.object=t.object;r.runtime=t.runtime;r.owner=t.true_owner;
    registry_creation_identities[stage]=r;++registry_creation_identity_open;
    ReleaseSRWLockExclusive(&registry_creation_lock);
    registry_creation_identity_read(&r,&t,find,map);
    AcquireSRWLockShared(&creation_lock);
    int unchanged=index<=creation_count && !memcmp(&creation_records[index-1],&c,sizeof(c)) &&
        creation_selected.object==t.object && creation_selected.runtime==t.runtime && creation_selected.true_owner==t.true_owner &&
        creation_selected.handle==t.handle && creation_selected.execution_tick==t.execution_tick;
    ReleaseSRWLockShared(&creation_lock);
    AcquireSRWLockExclusive(&registry_creation_lock);
    if(!unchanged)r.error|=RC_LIFETIME;
    if(registry_creation_closing || !stats.active)r.error|=RC_STOP;
    if(registry_creation_object!=t.object || registry_creation_runtime!=t.runtime || registry_creation_owner!=t.true_owner)r.error|=RC_LIFETIME;
    if(stage && (registry_creation_identities[0].object!=r.object || registry_creation_identities[0].runtime!=r.runtime ||
       registry_creation_identities[0].owner!=r.owner || registry_creation_identities[0].macro!=r.macro))r.error|=RC_LIFETIME;
    if(!r.error && !registry_creation_failures && stage<=2){r.admitted=1;registry_creation_identity_admitted=1;}
    r.complete=r.closed=1;
    if(registry_creation_identity_open)--registry_creation_identity_open;else r.error|=RC_SCOPE;
    registry_creation_identities[stage]=r;
    registry_creation_error_locked(r.error);
    ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);
}
static void registry_creation_trace(unsigned index,unsigned edge) {
    if(!InterlockedCompareExchange(&registry_creation_configured,0,0))return;
    if(edge==2){registry_creation_trace_using(index,edge,NULL,NULL);return;}
    DWORD last=GetLastError();
    void *map=NULL;
    if(!hash_ok(image+0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36") ||
       !read_local(image+0x6cf1500,&map,8))map=NULL;
    registry_creation_trace_using(index,edge,(snapshot_find_fn)(image+0xced70),map);
    SetLastError(last);
}

static void registry_creation_origin_enter(registry_origin_scope *scope) {
    if(InterlockedCompareExchange(&registry_creation_configured,0,0))scope->creation_owner_thread=GetCurrentThreadId();
}
static int registry_creation_scope_matches(const registry_origin_scope *scope,void *object,uint64_t runtime) {
    if(!scope || !scope->admitted || scope->closed || scope->creation_owner_thread!=GetCurrentThreadId() || !scope->context)return 0;
    void *entity=NULL;uint64_t actual=0;
    if(!read_local(object?(unsigned char*)object+8:NULL,&actual,8) || actual!=runtime ||
       !read_local((unsigned char*)scope->context+0x120,&entity,8))return 0;
    for(unsigned depth=0;entity && depth<16;depth++) {
        if(entity==object)return 1;
        void *next=NULL;if(!read_local((unsigned char*)entity+0x70,&next,8) || next==entity)return 0;entity=next;
    }
    return 0;
}
static void registry_creation_observe(void *object,uint64_t runtime,unsigned kind) {
    if(!InterlockedCompareExchange(&registry_creation_configured,0,0))return;
    DWORD last=GetLastError();registry_origin_scope *scope=registry_origin_current;
    registry_creation_operation r={0};r.object=(uintptr_t)object;r.runtime=runtime;r.kind=kind;r.thread=GetCurrentThreadId();r.tick=stats.issued;
    AcquireSRWLockShared(&registry_creation_lock);
    int selected=!registry_creation_closing && stats.active && registry_creation_runtime &&
        (runtime==registry_creation_runtime || r.object==registry_creation_object);
    ReleaseSRWLockShared(&registry_creation_lock);
    if(!selected){SetLastError(last);return;}
    /* Do not inspect unrelated or pre-selection native contexts. This passive
     * read is outside all locks; admission is checked again before retaining. */
    int scope_matches=kind==REGISTRY_AI && registry_creation_scope_matches(scope,object,runtime);
    AcquireSRWLockExclusive(&registry_creation_lock);
    if(registry_creation_closing || !stats.active || !registry_creation_runtime ||
       (runtime!=registry_creation_runtime && r.object!=registry_creation_object))goto done;
    r.handle=registry_creation_handle;r.identity_at_entry=registry_creation_identity_admitted;
    r.enter=++registry_creation_sequence;r.md_enter=registry_creation_anchor();
    if(registry_creation_operation_count==REGISTRY_CREATION_OPERATIONS){++registry_creation_dropped;registry_creation_error_locked(RC_OVERFLOW);goto done;}
    if(runtime!=registry_creation_runtime || r.object!=registry_creation_object)r.error|=RC_LIFETIME;
    if(r.thread!=registry_creation_thread){r.error|=RC_THREAD;++registry_creation_workers;}
    if(r.tick!=registry_creation_tick)r.error|=RC_SCOPE;
    if(kind<1 || kind>17)r.error|=RC_UNSUPPORTED;else registry_creation_kinds|=1u<<(kind-1);
    /* Only kind6 observed inside the matching four original-owned scopes has
     * an exact return boundary. A surrounding AI call cannot pair Params/Nav. */
    if(kind==REGISTRY_AI && scope_matches && scope && scope->admitted && !scope->closed && scope->creation_owner_thread==r.thread &&
       scope->source>=1 && scope->source<=4 && scope->creation_note_count<16) {
        r.paired=1;r.source=scope->source;r.caller=registry_local_rva((void*)scope->caller);
        r.context=(uintptr_t)scope->context;r.exec=(uintptr_t)scope->exec;r.program=(uintptr_t)scope->program;
        r.begin=scope->begin;r.end=scope->end;r.mode=scope->mode;r.creation=scope->creation_index;
        ++registry_creation_open;if(registry_creation_open>registry_creation_peak)registry_creation_peak=registry_creation_open;
        scope->creation_notes[scope->creation_note_count++]=registry_creation_operation_count+1;
    } else {r.error|=RC_UNSUPPORTED;++registry_creation_unpaired;
        if(kind==REGISTRY_AI && scope && !scope_matches)r.error|=RC_SCOPE;
        if(scope && scope->creation_note_count>=16)r.error|=RC_OVERFLOW;
    }
    /* Metadata reads are deferred outside our lock; the resident row retains
     * any refusal and never grants an operational actor lookup. */
    unsigned index=registry_creation_operation_count++;registry_creation_operations[index]=r;
    ReleaseSRWLockExclusive(&registry_creation_lock);
    if(object && !creation_read((uintptr_t)object,0x348,&r.owner,8))r.error|=RC_READ;
    if(r.paired) {
        r.script_hash=scope->context?ai_script(scope->context,r.script):0;
        if(scope->source==REGISTRY_ORIGIN_RANGE) {
            interrupt_origin origin=interrupt_origin_read(scope->caller);if(origin.source==1)r.verified=origin.caller;
            if(r.verified==0x1066dc6 && r.begin==0 && r.end==4 && r.mode==1 && !strcmp(r.script,"orders.base"))r.classification=1;
        } else if(scope->source==REGISTRY_ORIGIN_AI && r.caller==0x106e3fb && interrupt_origin_ready && interrupt_origin_owned()) {
            r.verified=r.caller;r.classification=2;
        }
        if(!r.classification)r.error|=RC_UNSUPPORTED;
    }
    AcquireSRWLockExclusive(&registry_creation_lock);
    if(registry_creation_closing || !stats.active){r.error|=RC_STOP;r.stop_overlap=1;}
    if(registry_creation_owner && r.owner!=registry_creation_owner)r.error|=RC_LIFETIME;
    r.captured=1;registry_creation_operations[index]=r;registry_creation_error_locked(r.error);
done:
    ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);
}
static void registry_creation_origin_end(registry_origin_scope *scope,uint64_t result,int normal) {
    if(!scope->creation_note_count)return;DWORD last=GetLastError();
    AcquireSRWLockExclusive(&registry_creation_lock);
    for(unsigned n=0;n<scope->creation_note_count;n++) {
        unsigned index=scope->creation_notes[n];
        if(!index || index>registry_creation_operation_count){registry_creation_error_locked(RC_SCOPE);continue;}
        registry_creation_operation *r=&registry_creation_operations[index-1];
        if(r->closed){registry_creation_error_locked(RC_DUPLICATE);continue;}
        r->leave=++registry_creation_sequence;r->md_leave=registry_creation_anchor();r->closed=1;r->normal=normal!=0;r->unwound=!normal;
        if(normal)r->result=result;else r->error|=RC_UNWIND;
        if(registry_creation_closing){r->stop_overlap=1;r->error|=RC_STOP;}
        if(r->thread!=GetCurrentThreadId())r->error|=RC_THREAD;
        if(registry_creation_open)--registry_creation_open;else r->error|=RC_SCOPE;
        registry_creation_error_locked(r->error);
    }
    scope->creation_note_count=0;ReleaseSRWLockExclusive(&registry_creation_lock);SetLastError(last);
}
static void registry_creation_stop(void) {
    AcquireSRWLockExclusive(&registry_creation_lock);registry_creation_closing=1;
    /* Also closes a configuration that already checked the creation state but
     * has not yet acquired our admission lock. No native state is touched. */
    if(!registry_creation_configured){ReleaseSRWLockExclusive(&registry_creation_lock);return;}
    if(registry_creation_open>registry_creation_stop_open)registry_creation_stop_open=registry_creation_open;
    if(registry_creation_identity_open>registry_creation_identity_stop_open)registry_creation_identity_stop_open=registry_creation_identity_open;
    for(unsigned i=0;i<registry_creation_operation_count;i++)if(registry_creation_operations[i].paired && !registry_creation_operations[i].closed) {
        registry_creation_operations[i].stop_overlap=1;registry_creation_operations[i].error|=RC_STOP;
    }
    if(registry_creation_open || registry_creation_identity_open)registry_creation_error_locked(RC_STOP);
    ReleaseSRWLockExclusive(&registry_creation_lock);
}
static int registry_creation_status(char *out,size_t capacity) {
    AcquireSRWLockShared(&registry_creation_lock);
    int n=snprintf(out,capacity,"schema=1 configured=%ld closing=%u sequence=%llu identities=%u operations=%u failures=%llu dropped=%llu open=%u peak=%u stop_open=%u identity_open=%u identity_stop_open=%u unpaired=%u workers=%u kinds=%x handle=%016llx runtime=%016llx tick=%llu thread=%u identity_admitted=%u publication=0 ready=0 preparation_supported=0 worker_exclusion_supported=0 lifetime_lease_supported=0",
        registry_creation_configured,registry_creation_closing,registry_creation_sequence,registry_creation_identity_count,
        registry_creation_operation_count,registry_creation_failures,registry_creation_dropped,registry_creation_open,
        registry_creation_peak,registry_creation_stop_open,registry_creation_identity_open,registry_creation_identity_stop_open,
        registry_creation_unpaired,registry_creation_workers,registry_creation_kinds,registry_creation_handle,
        registry_creation_runtime,registry_creation_tick,registry_creation_thread,registry_creation_identity_admitted);
    ReleaseSRWLockShared(&registry_creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
static int registry_creation_identity_format(unsigned index,unsigned part,char *out,size_t capacity) {
    registry_creation_identity_record r;AcquireSRWLockShared(&registry_creation_lock);
    int exists=index>=1 && index<=registry_creation_identity_count && part<=2;
    if(exists){r=registry_creation_identities[index-1];exists=r.closed || registry_creation_closing;}
    ReleaseSRWLockShared(&registry_creation_lock);if(!exists)return 0;
    int n=snprintf(out,capacity,"schema=1 index=%u part=%u sequence=%llu md_anchor=%llu creation=%llu edge=%u complete=%u closed=%u error=%x",
        index,part,r.sequence,r.md_anchor,r.creation,r.edge,r.complete,r.closed,r.error);
    if(n<=0 || (size_t)n>=capacity)return 0;
    if(!part)n+=snprintf(out+n,capacity-(size_t)n," creation_enter=%llu creation_leave=%llu tick=%llu thread=%u handle=%016llx object=%016llx runtime=%016llx owner=%016llx macro=%016llx lookup=%u lookup_first=%016llx lookup_second=%016llx map_before=%016llx map_after=%016llx controller=%016llx agent=%016llx slots=%016llx admitted=%u relationship=%u open_scopes=%u creation_error=%x presence_recorded=%u presence_stable=%u pending_order=%llu pending_kind=%u",
        r.creation_enter,r.creation_leave,r.tick,r.thread,r.handle,r.object,r.runtime,r.owner,r.macro,r.lookup,
        r.lookup_first,r.lookup_second,r.map_before,r.map_after,r.controller,r.agent,r.slots,r.admitted,r.relationship,
        r.open_scopes,r.creation_error,r.presence.recorded,r.presence.stable,r.pending_order,r.pending_kind);
    else {
        registry_presence_sample *p=part==1?&r.presence.first:&r.presence.second;
        n+=snprintf(out+n,capacity-(size_t)n," object=%016llx type=%llx actual=%016llx owner_getter=%llx owner=%016llx controller=%016llx controller_type=%llx controller_owner=%016llx slots=%016llx agent=%016llx agent_owner=%016llx reads=%x object_state=%u owner_state=%u controller_state=%u agent_state=%u",
            p->object,p->type,p->actual,p->owner_getter,p->owner,p->controller,p->controller_type,p->controller_owner,
            p->slots,p->agent,p->agent_owner,p->reads,p->object_state,p->owner_state,p->controller_state,p->agent_state);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int registry_creation_operation_format(unsigned index,char *out,size_t capacity) {
    registry_creation_operation r;AcquireSRWLockShared(&registry_creation_lock);
    int exists=index>=1 && index<=registry_creation_operation_count;
    if(exists){r=registry_creation_operations[index-1];exists=(r.captured && (!r.paired || r.closed)) || registry_creation_closing;}
    ReleaseSRWLockShared(&registry_creation_lock);if(!exists)return 0;
    int n=snprintf(out,capacity,"schema=1 index=%u enter=%llu leave=%llu md_enter=%llu md_leave=%llu tick=%llu thread=%u handle=%016llx object=%016llx runtime=%016llx owner=%016llx kind=%u source=%u caller=%llx verified=%llx context=%016llx exec=%016llx program=%016llx begin=%llx end=%llx mode=%u script_hash=%016llx script=%s creation=%llu classification=%u paired=%u captured=%u closed=%u normal=%u unwound=%u stop_overlap=%u error=%x identity_at_entry=%u result=%016llx",
        index,r.enter,r.leave,r.md_enter,r.md_leave,r.tick,r.thread,r.handle,r.object,r.runtime,r.owner,r.kind,r.source,
        r.caller,r.verified,r.context,r.exec,r.program,r.begin,r.end,r.mode,r.script_hash,r.script[0]?r.script:"none",r.creation,
        r.classification,r.paired,r.captured,r.closed,r.normal,r.unwound,r.stop_overlap,r.error,r.identity_at_entry,r.result);
    return n>0 && (size_t)n<capacity?n:0;
}
