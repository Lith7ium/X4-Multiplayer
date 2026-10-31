/* First covered AI-family entry only. All metadata is local evidence, never a
 * deterministic identity. The original first kind/tick/order/thread is retained. */
static _Thread_local registry_origin_scope *registry_origin_current;
static int registry_origin_enabled,registry_origin_closing;
static LONG64 registry_origin_open,registry_origin_restore_open,registry_origin_peak,registry_origin_failures;
enum {ORIGIN_UNKNOWN=1,ORIGIN_READ=2,ORIGIN_OVERFLOW=4,ORIGIN_CALLER=8,ORIGIN_SCOPE=16,ORIGIN_UNWIND=32};
static void *registry_origin_tls(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58),*slot=NULL;
    if(slots) read_local(slots,&slot,8);return slot;
}
static uint64_t *registry_origin_rng(void *slot) {return slot?(uint64_t*)((unsigned char*)slot+0x318):NULL;}
static int registry_origin_enable(void) {
    if(!interrupt_origin_prepare() || !registry_origin_identity_pins()) return 0;
    AcquireSRWLockExclusive(&registry_lock);
    int ok=!registry_origin_closing && !registry_origin_open;
    if(ok) registry_origin_enabled=1;
    ReleaseSRWLockExclusive(&registry_lock);return ok;
}
static void registry_origin_enter(registry_origin_scope *s,unsigned source,uintptr_t caller,void *exec,void *context,
    void *program,uint64_t begin,uint64_t end,unsigned mode,void *tls,uint64_t *rng) {
    memset(s,0,sizeof(*s));s->previous=registry_origin_current;registry_origin_current=s;
    s->source=source;s->caller=caller;s->exec=exec;s->context=context;s->program=program;
    s->begin=begin;s->end=end;s->mode=mode;s->tls=tls;s->rng=rng;
    registry_creation_origin_enter(s);
    s->creation_index=creation_range_enter(s);
    if(!registry_origin_enabled) return;
    AcquireSRWLockExclusive(&registry_lock);
    if(registry_origin_enabled && !registry_origin_closing && stats.active && boot_phase==3 && registry_has_pending()) {
        s->admitted=1;++registry_origin_open;
        if(registry_origin_open>registry_origin_peak) registry_origin_peak=registry_origin_open;
        if(registry_origin_open>REGISTRY_PENDING_CAPACITY) ++registry_origin_failures;
    }
    ReleaseSRWLockExclusive(&registry_lock);
}
/* Called with registry_lock held, exactly when the immutable pending entry is
 * allocated. Failed/unknown first attempts are published and never retried. */
static void registry_origin_capture(registry_pending_origin *n,void *object,uint64_t runtime) {
    if(!registry_origin_enabled) return;
    n->recorded=1;registry_origin_identity_capture(&n->identity,object,runtime);registry_origin_scope *s=registry_origin_current;
    if(!s || !s->admitted) {n->error=ORIGIN_UNKNOWN;++registry_origin_failures;return;}
    n->source=s->source;n->caller=registry_local_rva((void*)s->caller);n->caller_source=n->caller?2:3;
    if(s->source==REGISTRY_ORIGIN_RANGE) {
        interrupt_origin caller=interrupt_origin_read(s->caller);
        n->caller=caller.raw;n->verified_caller=caller.caller;n->caller_source=caller.source;
        if(caller.source!=1) n->error|=ORIGIN_CALLER;
    } else if(s->source==REGISTRY_ORIGIN_AI && n->caller==0x106e3fb && interrupt_origin_ready && interrupt_origin_owned()) {
        n->verified_caller=n->caller;n->caller_source=1;
    }
    if(s->source==REGISTRY_ORIGIN_AI && n->caller_source!=1) n->error|=ORIGIN_CALLER;
    n->context=(uintptr_t)s->context;n->exec=(uintptr_t)s->exec;n->program=(uintptr_t)s->program;
    n->begin=s->begin;n->end=s->end;n->mode=s->mode;n->ui=GetCurrentThreadId()==ui_thread;
    void *type=NULL,*exec=NULL;unsigned char *entity=NULL;unsigned char view=0;
    if(read_local(object,&type,8)) {n->type=registry_local_rva(type);n->reads|=1;}
    if(s->context && read_local((unsigned char*)s->context+0x130,&exec,8) && exec && (!s->exec || exec==s->exec)) {
        n->reads|=512;if(!s->exec) n->exec=(uintptr_t)exec;
    }
    if(n->exec && read_local((unsigned char*)(uintptr_t)n->exec+0x18,&n->pc,8)) n->reads|=2;
    if(s->context && read_local((unsigned char*)s->context+0x140,&n->context_pc,8)) n->reads|=4;
    if(s->context && read_local((unsigned char*)s->context+0x120,&entity,8) && entity && read_local(entity+8,&n->entity,8) && n->entity) {
        for(unsigned depth=0;entity && depth<16;depth++) {
            if(entity==object) {n->depth=depth;n->reads|=8;break;}
            unsigned char *next=NULL;if(!read_local(entity+0x70,&next,8) || next==entity) break;entity=next;
        }
    }
    n->script_hash=s->context?ai_script(s->context,n->script):0;
    if(n->script_hash) n->reads|=16;else strcpy(n->script,"unsupported");
    if(s->tls) {
        if(read_local((unsigned char*)s->tls+0x314,&view,1)) {n->view=view;n->reads|=32;}
        if(read_local((unsigned char*)s->tls+0x300,&n->time,8)) n->reads|=64;
    }
    if(s->rng && read_local(s->rng,&n->rng_before,8)) n->reads|=128;
    if((n->reads&767)!=767) n->error|=ORIGIN_READ;
    if(s->count==16) n->error|=ORIGIN_OVERFLOW;
    else s->notes[s->count++]=n;
    if(n->error) ++registry_origin_failures;
}
static void registry_origin_leave(registry_origin_scope *s,uint64_t result) {
    if(s->closed) {InterlockedIncrement64(&registry_origin_failures);return;}
    creation_range_leave(s->creation_index,result);
    registry_creation_origin_end(s,result,1);
    if(!s->admitted && !s->count) {s->closed=1;registry_origin_current=s->previous;return;}
    AcquireSRWLockExclusive(&registry_lock);
    if(registry_origin_current!=s) ++registry_origin_failures;
    for(unsigned i=0;i<s->count;i++) {
        registry_pending_origin *n=s->notes[i];n->result=result;
        if(s->rng && read_local(s->rng,&n->rng_after,8)) n->reads|=256;
        else {n->error|=ORIGIN_READ;++registry_origin_failures;}
        n->complete=1;
    }
    if(s->admitted) --registry_origin_open;
    s->closed=1;ReleaseSRWLockExclusive(&registry_lock);registry_origin_current=s->previous;
}
/* The scope itself is live in its owning wrapper's finally. Only its resident
 * note pointers and own bookkeeping are used; native context/RNG/event memory
 * may already be dead and must not be sampled on this path. */
static void registry_origin_abort(registry_origin_scope *s) {
    if(s->closed)return;
    DWORD saved_error=GetLastError();creation_range_abort(s->creation_index);
    registry_creation_origin_end(s,0,0);
    AcquireSRWLockExclusive(&registry_lock);
    if(registry_origin_current!=s)++registry_origin_failures;
    if(s->admitted || s->count)++registry_origin_failures;
    for(unsigned i=0;i<s->count;i++)s->notes[i]->error|=ORIGIN_UNWIND;
    if(s->admitted) {
        if(registry_origin_open>0)--registry_origin_open;else ++registry_origin_failures;
    }
    s->closed=1;registry_origin_current=s->previous;
    ReleaseSRWLockExclusive(&registry_lock);SetLastError(saved_error);
}
/* No waiting under native locks. A STOP crossing an open original is incomplete
 * even when that original later publishes its exit. Storage remains resident. */
static void registry_origin_close(void) {
    AcquireSRWLockExclusive(&registry_lock);registry_origin_closing=1;
    if(registry_origin_open>registry_origin_restore_open) registry_origin_restore_open=registry_origin_open;
    ReleaseSRWLockExclusive(&registry_lock);
}
static int registry_origin_format(unsigned index,char *out,size_t capacity) {
    registry_pending_update p;AcquireSRWLockShared(&registry_lock);
    int exists=index<registry_pending_count;if(exists) p=registry_pending[index];ReleaseSRWLockShared(&registry_lock);
    if(!exists) return 0;registry_pending_origin n=p.origin;
    int length=snprintf(out,capacity,"schema=1 runtime=%016llx order=%llu thread=%u recorded=%u source=%u caller=%llx verified=%llx caller_source=%u context=%016llx exec=%016llx program=%016llx begin=%llx end=%llx mode=%u pc=%llx context_pc=%llx type=%llx entity=%016llx depth=%u ui=%u view=%u time=%016llx rng_before=%016llx rng_after=%016llx result=%016llx reads=%x error=%x complete=%u script_hash=%016llx script=%s",
        p.runtime,p.order,p.thread,n.recorded,n.source,n.caller,n.verified_caller,n.caller_source,n.context,n.exec,n.program,n.begin,n.end,n.mode,
        n.pc,n.context_pc,n.type,n.entity,n.depth,n.ui,n.view,n.time,n.rng_before,n.rng_after,n.result,n.reads,n.error,n.complete,n.script_hash,n.script[0]?n.script:"none");
    return length>0 && (size_t)length<capacity?length:0;
}
