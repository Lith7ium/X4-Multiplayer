/* One explicitly configured FindComponent draw. No new native patch site.
 * Saved script 38361 is DECIMAL manifest metadata, not a native context ID. */
#define WORLD_FIND_CAP 8u
#define WORLD_FIND_SAVED 0x9d3a4ULL
#define WORLD_FIND_ORDER 0x3cdbULL
#define WORLD_FIND_SNAPSHOT "7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8"
#define WORLD_FIND_SHAPE 0x985d66488a43c795ULL
#define WORLD_FIND_THIRD_CONTRACT "44ed014534fb05209d4717179c6858e0d982d05a9e2d2d5a70c946bfa9f0c101"
#define WORLD_FIND_THIRD_POLICY "WAIT_POLICY_2;" WORLD_FIND_SNAPSHOT ";1,intervene,70,71,0,6.91667;2,intervene,121,122,0,6.31667;3,intervene,70,71,0,6.98333"
enum {WF_ANCHOR=1,WF_IDENTITY=2,WF_INPUT=4,WF_PHASE=8,WF_TLS=16,WF_DUPLICATE=32,
      WF_NESTED=64,WF_STOP=128,WF_POST=256,WF_RESTORE=512,WF_UNWIND=1024,WF_OVERFLOW=2048};
typedef struct {
    uintptr_t context,entity,script,exec,program,order,backlink,registry,group;
    uint64_t context_id,runtime,saved,order_id;unsigned state,version,flags,generation;
} world_find_identity;
typedef struct {
    volatile LONG complete;unsigned errors,applied,restored,abnormal,thread,action,candidate,ordinal;
    uint64_t caller_before,seed,scoped_before,scoped_after,caller_after,result,assigned;
    world_find_identity before,after;
} world_find_event;
typedef struct world_find_scope {
    struct world_find_scope *previous;world_find_event *event;void *tls;uint64_t *address;
    void *(*tls_reader)(void);uint64_t caller;unsigned admitted,active;
} world_find_scope;
static SRWLOCK world_find_lock=SRWLOCK_INIT;
static int world_find_enabled,world_find_started,world_find_closed;
static unsigned world_find_composition;
static unsigned world_find_reserved,world_find_exported,world_find_used,world_find_anchor_ready,world_find_anchor_pending;
static LONG64 world_find_calls,world_find_open,world_find_restore_open,world_find_failures,world_find_dropped,world_find_anchor_calls;
static world_find_event world_find_events[WORLD_FIND_CAP];
static world_find_identity world_find_anchor;
static const world_wait_event *world_find_anchor_event;
static unsigned world_find_anchor_errors;
static uint64_t world_find_seed,world_find_shape;
static unsigned char world_find_digest[32];
static char world_find_manifest[256];
static _Thread_local world_find_scope *world_find_current;
static int (*world_find_extra_conflict)(void);

static int world_find_sha(const unsigned char *bytes,unsigned size,unsigned char digest[32]) {
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;int ok=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0||
       BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0||BCryptHashData(hash,(PUCHAR)bytes,size,0)<0||
       BCryptFinishHash(hash,digest,32,0)<0) goto done;
    ok=1;
done:if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
static void world_find_le(unsigned char **p,uint64_t v,unsigned bytes) {for(unsigned i=0;i<bytes;i++) {*(*p)++=(unsigned char)v;v>>=8;}}
static int world_find_key(const unsigned char snapshot[32],uint64_t pilot,uint64_t order,uint64_t tick,uint64_t ordinal,
                          unsigned char digest[32],uint64_t *seed) {
    static const char domain[]="X4LAN/WORLD_FIND_COMPONENT/RNG/v1",name[]="order.fight.patrol",time[]="6.31667";
    unsigned char bytes[192],*p=bytes;memcpy(p,domain,sizeof(domain));p+=sizeof(domain);memcpy(p,snapshot,32);p+=32;
    world_find_le(&p,pilot,8);world_find_le(&p,order,8);world_find_le(&p,sizeof(name)-1,2);memcpy(p,name,sizeof(name)-1);p+=sizeof(name)-1;
    world_find_le(&p,10,4);world_find_le(&p,129,8);world_find_le(&p,130,8);world_find_le(&p,2,4);world_find_le(&p,tick,8);world_find_le(&p,ordinal,8);
    world_find_le(&p,38361,8);world_find_le(&p,1,4);world_find_le(&p,sizeof(time)-1,2);memcpy(p,time,sizeof(time)-1);p+=sizeof(time)-1;
    world_find_le(&p,121,8);world_find_le(&p,122,8);if(p-bytes!=179||!world_find_sha(bytes,(unsigned)(p-bytes),digest)) return 0;
    *seed=0;for(unsigned i=0;i<8;i++) *seed|=(uint64_t)digest[i]<<(i*8);return 1;
}
static uint64_t world_find_step(uint64_t value) {value=value*0x5851f42d4c957f2dULL+0x14057b7ef767814fULL;return(value>>30)|(value<<34);}
/* Read-only equivalent of pinned33d420. Return selected slot metadata too, so a
 * second read rejects generation/occupied/pointer changes, not just pointer reuse. */
static int world_find_registry(world_values_reader read,uint64_t id,uintptr_t expected,world_find_identity *out) {
    uintptr_t map=0,bank=0,group=0,object=0;unsigned flags=0,generation=0;
    uint64_t low=id&0x3ffffULL;if(!low) return 0;uint64_t slot=low-1,bank_index=slot>>15;slot&=0x7fff;
    if(bank_index>=8||!read(image+0x6cf1750,&map,8)||!map||map>UINTPTR_MAX-0x98||
       !read((void*)(map+0x58+bank_index*8),&bank,8)||!bank||bank>UINTPTR_MAX-((slot/3)*32+32)) return 0;
    group=bank+(slot/3)*32;unsigned part=(unsigned)(slot%3);
    if(!read((void*)(group+part*8),&object,8)||object!=expected||
       !read((void*)(group+0x1c),&flags,4)||!((flags>>(16+part*4))&1)||
       !read((void*)(group+0x18+(part>>1)*4),&generation,4)||
       (unsigned short)(generation>>((part&1)*16))!=(unsigned short)(id>>18)) return 0;
    out->registry=map;out->group=group;out->flags=flags;out->generation=generation;return 1;
}
static int world_find_identity_read(world_values_reader read,uintptr_t context,uintptr_t exec,world_find_identity *out) {
    memset(out,0,sizeof(*out));out->context=context;out->exec=exec;uintptr_t type=0;
    if(context<0x10000||context>UINTPTR_MAX-0x150||exec<0x10000||exec>UINTPTR_MAX-0x20||
       !read((void*)context,&type,8)||type!=(uintptr_t)(image+0x2c6a270)||
       !read((void*)(context+8),&out->context_id,8)||!out->context_id||
       !read((void*)(context+0x120),&out->entity,8)||out->entity<0x10000||out->entity>UINTPTR_MAX-16||
       !read((void*)(out->entity+8),&out->runtime,8)||!out->runtime||
       (out->saved=world_identity_lookup(out->runtime))!=WORLD_FIND_SAVED||
       !read((void*)(context+0xd8),&out->script,8)||!out->script||out->script>UINTPTR_MAX-0x5c||
       !read((void*)(out->script+0x58),&out->version,4)||out->version!=10||
       !read((void*)(exec+0x10),&out->program,8)||!out->program||
       !read((void*)(context+0x148),&out->order,8)||out->order<0x10000||out->order>UINTPTR_MAX-0x78||
       !read((void*)out->order,&type,8)||type!=(uintptr_t)(image+0x2c69060)||
       !read((void*)(out->order+8),&out->order_id,8)||!out->order_id||
       !read((void*)(out->order+0x20),&out->state,4)||!(out->state==1||(out->state>=5&&out->state<=7))||
       !read((void*)(out->order+0x50),&out->backlink,8)||out->backlink!=context) return 0;
    return world_find_registry(read,out->order_id,out->order,out);
}
static int world_find_identity_stable(world_values_reader read,uintptr_t context,uintptr_t exec,world_find_identity *out) {
    world_find_identity again;return world_find_identity_read(read,context,exec,out)&&world_find_identity_read(read,context,exec,&again)&&!memcmp(out,&again,sizeof(again));
}
/* Import map key is a saved ORDER ID, not a Universe ID. No engine lookup call. */
static int world_find_import_order(world_values_reader read,uint64_t *id) {
    uintptr_t root=0,again=0,node=0,visited[64];unsigned count=0;*id=0;
    if(!read(image+0x31c2de8,&root,8)||!root) return 0;node=root;
    while(node&&count<64) {
        if(node<0x10000||node>UINTPTR_MAX-0x30) return 0;
        for(unsigned i=0;i<count;i++) if(visited[i]==node) return 0;visited[count++]=node;
        uint64_t key=0;uintptr_t next=0;
        if(!read((void*)(node+0x20),&key,8)) return 0;
        if(key==WORLD_FIND_ORDER) {
            uint64_t pair[2],check[2];return read((void*)(node+0x20),pair,16)&&pair[0]==key&&pair[1]&&
                read((void*)(node+0x20),check,16)&&!memcmp(pair,check,16)&&
                read(image+0x31c2de8,&again,8)&&again==root&&(*id=pair[1])!=0;
        }
        if(!read((void*)(node+(key>WORLD_FIND_ORDER?8:16)),&next,8)) return 0;node=next;
    }return 0;
}
static void world_find_fail(world_find_event *e,unsigned why) {if(e) e->errors|=why;}
static int world_find_conflict(void) {
    int extra=world_find_extra_conflict?world_find_extra_conflict():0;
    if(!world_find_current)return extra;world_find_fail(world_find_current->event,WF_NESTED);return 1;
}
static void world_find_wait_anchor(const world_wait_event *e,unsigned after) {
    if(world_find_current) world_find_fail(world_find_current->event,WF_NESTED);
    if(!e||e->before.saved!=WORLD_FIND_SAVED||strcmp(e->before.name,"order.fight.patrol")) return;
    AcquireSRWLockExclusive(&world_find_lock);
    if(!world_find_enabled||world_find_closed) {ReleaseSRWLockExclusive(&world_find_lock);return;}
    if(!after) {
        ++world_find_anchor_calls;
        if(world_find_anchor_calls!=1||world_find_anchor_pending||world_find_anchor_ready) {
            world_find_anchor_errors|=WF_DUPLICATE;ReleaseSRWLockExclusive(&world_find_lock);return;
        }
        world_find_anchor_pending=1;world_find_anchor_event=e;
        uint64_t order_id=0,time=0;world_wait_rng_time("6.31667",&time);
        if(e->errors||e->caller!=(uintptr_t)(image+WORLD_WAIT_CALLER)||e->before.version!=10||e->before.time!=time||
           e->before.slot!=121||e->before.pc!=122||e->boot!=2||e->tick||!e->ui||!e->selected||
           e->phase_before.main||e->phase_before.view!=1||e->phase_before.app_flags!=0x26||e->phase_before.vbi_loading!=1||
           !e->phase_before.eligible||e->phase_before.game!=0x401c54fdf3b645a2ULL||e->phase_before.game!=e->phase_before.saved||e->phase_before.tls_time!=e->phase_before.game||
           !world_find_identity_stable(read_local,e->before.context,e->before.exec,&world_find_anchor)||
           !world_find_import_order(read_local,&order_id)||order_id!=world_find_anchor.order_id) world_find_anchor_errors|=WF_ANCHOR;
    } else {
        if(!world_find_anchor_pending||world_find_anchor_event!=e) {
            world_find_anchor_errors|=WF_DUPLICATE;ReleaseSRWLockExclusive(&world_find_lock);return;
        }
        world_find_identity current;uint64_t order_id=0;
        if(!e->complete||e->errors||
           !world_find_identity_stable(read_local,e->after.context,e->after.exec,&current)||memcmp(&current,&world_find_anchor,sizeof(current))||
           !world_find_import_order(read_local,&order_id)||order_id!=world_find_anchor.order_id) world_find_anchor_errors|=WF_ANCHOR;
        world_find_anchor_pending=0;world_find_anchor_ready=!world_find_anchor_errors;
    }
    ReleaseSRWLockExclusive(&world_find_lock);
}
static int world_find_selected(const world_action_event *p) {
    return p&&p->before.saved==WORLD_FIND_SAVED&&!strcmp(p->before.name,"order.fight.patrol")&&p->before.index==129;
}
static unsigned world_find_guards(world_values_event *v,world_action_event *p,void *tls,void *(*tls_reader)(void),world_find_identity *identity) {
    unsigned why=0;static const uint64_t saved[]={0x28,0x29,0x2b,0x428af};world_values_candidates candidates;
    unsigned char declaration[0x22];world_queue_phase_sample phase;uint64_t pc=0,rng=0;unsigned flags=0;
    if(!v||!p||v->errors||p->errors||!world_values_parent_same(p)||world_values_candidate_current!=v||
       p->before.version!=10||p->before.vtable!=(uintptr_t)(image+0x2c0f3c8)||p->before.action!=(uintptr_t)(image+0xb87a30)||
       p->before.program_hash!=world_find_shape||(p->before.end-p->before.begin)/8!=234||p->before.chance||p->parent||p->depth!=1||
       v->caller!=0xb87ae8||v->kind!=1||v->arguments[0]!=p->before.node+0xf0||v->arguments[2]!=p->before.frame||v->arguments[3]) why|=WF_INPUT;
    if(!v||!read_local((void*)v->arguments[0],declaration,sizeof(declaration))||memcmp(declaration,v->declaration,sizeof(declaration))||
       *(uint32_t*)declaration!=0x5b||*(uint32_t*)(declaration+16)!=0x54||declaration[32]||declaration[33]||
       !world_values_vector_stable(read_local,(void*)v->arguments[1],&candidates)||memcmp(&candidates,&v->before,sizeof(candidates))||candidates.count!=4) why|=WF_INPUT;
    if(!(why&WF_INPUT)) for(unsigned i=0;i<4;i++) if(candidates.items[i].saved!=saved[i]||candidates.items[i].type!=0x2b66080||candidates.items[i].valid!=1) why|=WF_INPUT;
    if(!world_find_anchor_ready||world_find_anchor_pending||world_find_anchor_errors) why|=WF_ANCHOR;
    if(!p||!world_find_identity_stable(read_local,p->before.context,p->before.exec,identity)||memcmp(identity,&world_find_anchor,sizeof(*identity))) why|=WF_IDENTITY;
    if(!world_action_phase(&phase,tls)||phase.app_flags!=0x20||phase.vbi_loading||phase.game!=0x401c54fdf3b645a2ULL) why|=WF_PHASE;
    if(!tls||!tls_reader||tls_reader()!=tls||!world_action_sample(tls,tls_reader,&rng,(void*)(p?p->before.exec:0),&pc,&flags)||pc!=130||flags||
       !world_wait_rng_writable((uint64_t*)((unsigned char*)tls+0x318))) why|=WF_TLS;
    return why;
}
static void world_find_restore(void) {
    AcquireSRWLockExclusive(&world_find_lock);world_find_enabled=0;world_find_closed=1;
    if(world_find_open>world_find_restore_open) world_find_restore_open=world_find_open;
    ReleaseSRWLockExclusive(&world_find_lock);
}
/* Only the observer-owned TLS location may be accessed in abnormal cleanup.
 * A write fault is contained here; the original exception keeps propagating. */
static int world_find_tls_store(world_find_scope *s,uint64_t value,unsigned *written) {
    if(written) *written=0;
    if(!s->address||((uintptr_t)s->address&7)||!s->tls_reader||s->tls_reader()!=s->tls||!world_wait_rng_writable(s->address)) return 0;
    int ok=0;__try {*s->address=value;if(written) *written=1;ok=*s->address==value;} __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}return ok;
}
static void world_find_scope_finish(world_find_scope *s,unsigned abnormal) {
    world_find_event *e=s->event;
    if(e&&abnormal) {e->abnormal=1;e->errors|=WF_UNWIND;}
    if(s->active) {
        uint64_t out=0;
        if(s->tls_reader&&s->tls_reader()==s->tls&&read_local(s->address,&out,8)) {if(e) e->scoped_after=out;}
        else world_find_fail(e,WF_TLS);
        if(world_find_tls_store(s,s->caller,NULL)) {e->caller_after=s->caller;e->restored=1;}else world_find_fail(e,WF_RESTORE);
    }
    if(world_find_current!=s) world_find_fail(e,WF_NESTED);world_find_current=s->previous;
    AcquireSRWLockExclusive(&world_find_lock);
    if(e) {if(world_find_closed) e->errors|=WF_STOP;if(e->errors) ++world_find_failures;InterlockedExchange(&e->complete,1);}
    if(s->admitted) {if(world_find_open<=0) ++world_find_failures;else --world_find_open;}
    ReleaseSRWLockExclusive(&world_find_lock);
}
static uintptr_t world_find_call(world_values_fn original,void *a,void *b,void *c,void *d,
                                 world_values_event *v,world_action_event *p,void *tls,void *(*tls_reader)(void)) {
    world_find_scope scope={0};scope.previous=world_find_current;scope.tls=tls;scope.tls_reader=tls_reader;
    int extra=world_find_extra_conflict?world_find_extra_conflict():0;
    if(scope.previous) world_find_fail(scope.previous->event,WF_NESTED);
    AcquireSRWLockExclusive(&world_find_lock);
    scope.admitted=world_find_enabled&&!world_find_closed&&stats.active;
    if(scope.admitted) {++world_find_calls;++world_find_open;}
    if(scope.admitted&&world_find_selected(p)) {
        unsigned index=world_find_reserved++;if(index>=WORLD_FIND_CAP) {++world_find_dropped;++world_find_failures;}
        else {
            world_find_event *e=&world_find_events[index];scope.event=e;e->ordinal=index;e->thread=GetCurrentThreadId();
            e->action=v?v->parent_action:0;e->candidate=v?(unsigned)(v-world_values_events)+1:0;e->seed=world_find_seed;
            e->errors=world_find_guards(v,p,tls,tls_reader,&e->before);
            if(extra)e->errors|=WF_NESTED;
            if(scope.previous||(world_wait_rng_tls!=TLS_OUT_OF_INDEXES&&TlsGetValue(world_wait_rng_tls))) e->errors|=WF_NESTED;
            if(world_find_used||index) e->errors|=WF_DUPLICATE;
            scope.address=tls?(uint64_t*)((unsigned char*)tls+0x318):NULL;
            uint64_t again=0;
            if(!scope.address||!read_local(scope.address,&e->caller_before,8)||!read_local(scope.address,&again,8)||again!=e->caller_before) e->errors|=WF_TLS;
            if(!stats.active||world_find_closed) e->errors|=WF_STOP;
            if(!e->errors) {
                scope.caller=e->caller_before;world_find_used=1;
                /* A failed pre-write check/store never authorizes restoration.
                 * If the aligned store completed but readback faults, finally
                 * still owns restoration of this exact saved caller location. */
                unsigned written=0;int stored=world_find_tls_store(&scope,e->seed,&written);
                scope.active=e->applied=written;
                if(stored) e->scoped_before=e->seed;else e->errors|=WF_TLS;
            }
        }
    }
    ReleaseSRWLockExclusive(&world_find_lock);
    world_find_current=&scope;uintptr_t result=0;
    __try {
        result=original(a,b,c,d); /* exactly once; complete RAX retained */
        if(scope.event) {
            world_find_event *e=scope.event;e->result=result;
            if(scope.active) {
                e->errors|=world_find_guards(v,p,tls,tls_reader,&e->after);
                uint64_t current=0;if(!tls_reader||tls_reader()!=tls||!read_local(scope.address,&current,8)||current!=world_find_step(e->seed)) e->errors|=WF_POST;
                if(!v||v->children!=1) e->errors|=WF_POST;
                unsigned found=0,limit=world_values_reserved>WORLD_VALUES_MAX?WORLD_VALUES_MAX:(unsigned)world_values_reserved;
                for(unsigned i=0;i<limit;i++) {world_values_event *child=&world_values_events[i];
                    if(child->parent_candidate==e->candidate) {++found;if(!child->complete||child->errors||child->caller!=0xd2ad67||child->value.kind!=3||child->value.tag!=0xa) e->errors|=WF_POST;else e->assigned=child->value.saved;}}
                static const uint64_t saved[]={0x28,0x29,0x2b,0x428af};if(found!=1||e->assigned!=saved[world_find_step(e->seed)%4]) e->errors|=WF_POST;
            }
        }
    } __finally {world_find_scope_finish(&scope,AbnormalTermination());}
    return result;
}
#include "lockstep_world_find_rng_config.h"
