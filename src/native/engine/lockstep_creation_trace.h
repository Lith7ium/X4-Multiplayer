/* Bounded observer-owned creation records. Native objects/RNG/queues are read
 * only. A reservation is joined to borrowed arguments and later native returns;
 * none of these states enroll an actor or prove worker/lifetime exclusion. */
#include "lockstep_creation_trace_contract.h"
#define CREATION_MANIFEST "CREATION_TRACE_2;handle=0000000500000003;macro=ship_arg_xl_builder_01_a_macro;owner=player;tick=5"
enum creation_error {CREATION_E_TOKEN=1,CREATION_E_THREAD=2,CREATION_E_CALLER=4,
    CREATION_E_SCOPE=8,CREATION_E_READ=16,CREATION_E_IDENTITY=32,CREATION_E_OVERFLOW=64,
    CREATION_E_OWNERSHIP=128,CREATION_E_RETURN=256,CREATION_E_BRANCH=512,CREATION_E_UNWIND=1024};
typedef struct {
    uint64_t tls,time,rng,rng_pointer,object,runtime,controller,agent,timer;
    uint64_t controller_type,controller_owner,slots,controller_agent,agent_owner;
    uint64_t cache_time,cache_index,other_time;
    unsigned thread,ui,main,view,read,write,reads,direct_reads,stable;
    registry_origin_identity identity;
    unsigned char navigation[2][416],parameters[2][416];
} creation_snapshot;
typedef struct {
    uint64_t enter,leave,handle,phase_order,caller,args[10],result;
    uint64_t range_verified,range_pc,range_context_pc,range_entity,range_script_hash;
    unsigned kind,count,parent,depth,error,result_kind,complete,foreign,unwound;
    unsigned range_source,range_reads,range_depth;
    unsigned stage,subject,aux_reads;
    uint64_t proposed_type,published_before,published_after,rng_before,rng_after;
    char range_script[96];
    creation_snapshot before,after;
} creation_record;
typedef struct {
    uint64_t handle,phase_order,phase2_order,builder_order,builder_runtime;
    uint64_t reserved_tick,execution_tick;
    uint64_t object,runtime,controller,agent;
    uint64_t prior_static,callback_order,callback_tick,true_owner;
    unsigned a_error,b_error,a_closed,b_closed,static_count,excluded_count,unresolved_count,ancillary_rng,foreign_callbacks;
    unsigned state,thread,generate_count,initialize_count,pilot_count,navigation_count,agent_count,setter_count;
} creation_token;
static creation_record creation_records[CREATION_CAPACITY];
static creation_token creation_selected;
static SRWLOCK creation_lock=SRWLOCK_INIT;
static unsigned creation_count;
static volatile LONG creation_started,creation_enabled,creation_closing,creation_window;
static uint64_t creation_failures,creation_dropped,creation_open,creation_peak,creation_restore_open,creation_restore_failures;
static unsigned creation_first_error;
static _Thread_local unsigned creation_current;
static unsigned creation_owned;
static uint64_t creation_original_words[CREATION_HOOK_COUNT][2],creation_patched_words[CREATION_HOOK_COUNT][2];
static int creation_hooks_owned(void);
/* Separate opt-in intervention; natural schema2 remains unchanged. */
static void creation_rng_begin(unsigned,unsigned,uintptr_t,const uint64_t *,unsigned *);
static void creation_rng_end(unsigned,int);
static void creation_rng_stop_locked(void);

/* Caller holds only our own short-lived lock. */
static void creation_error_locked(unsigned error) {
    if(!error)return;if(!creation_first_error)creation_first_error=error;++creation_failures;
    if(creation_selected.state<4)creation_selected.a_error|=error;else creation_selected.b_error|=error;
}
static int creation_read(uint64_t base,unsigned offset,void *out,size_t size) {
    return base && base<=UINTPTR_MAX-offset && size<=UINTPTR_MAX-(base+offset) &&
        read_local((void*)(uintptr_t)(base+offset),out,size);
}
static void creation_snapshot_once(creation_snapshot *s,const creation_token *token,void *tls) {
    memset(s,0,sizeof(*s));s->thread=GetCurrentThreadId();s->ui=s->thread==ui_thread;s->main=s->thread==main_thread;
    s->object=token->object;s->runtime=token->runtime;s->controller=token->controller;s->agent=token->agent;
    s->tls=(uintptr_t)tls;s->rng_pointer=(uintptr_t)registry_origin_rng(tls);
    unsigned char view=0;
    if(creation_read(s->tls,0x300,&s->time,8))s->reads|=1;
    if(creation_read(s->rng_pointer,0,&s->rng,8))s->reads|=2;
    if(creation_read(s->tls,0x314,&view,1)){s->view=view;s->reads|=4;}
    if(read_local(image+0x38f4ee4,&s->read,4))s->reads|=8;
    if(read_local(image+0x2f4b6f4,&s->write,4))s->reads|=16;
    if(s->object)registry_origin_identity_capture(&s->identity,(void*)(uintptr_t)s->object,s->runtime);
    if(s->controller) {
        if(creation_read(s->controller,0,&s->controller_type,8))s->direct_reads|=1;
        if(s->controller_type==(uintptr_t)(image+0x2c51158)) {
            if(creation_read(s->controller,0x28,&s->controller_owner,8))s->direct_reads|=2;
            if(creation_read(s->controller,0x30,&s->slots,8))s->direct_reads|=4;
            if(creation_read(s->controller,0x40,&s->controller_agent,8))s->direct_reads|=8;
            if(creation_read(s->controller,0xc0,&s->cache_time,8))s->direct_reads|=16;
            if(creation_read(s->controller,0xc8,&s->cache_index,4))s->direct_reads|=32;
            if(creation_read(s->controller,0xd0,&s->other_time,8))s->direct_reads|=64;
        }
    }
    /* The token gains an Agent only after its actual constructor returned. */
    if(s->agent) {
        if(creation_read(s->agent,0xef0,&s->agent_owner,8))s->direct_reads|=128;
        if(s->agent_owner==s->object && s->object) {
            if(creation_read(s->agent,0xac0,&s->timer,8))s->direct_reads|=256;
            if(creation_read(s->agent,0xad0,s->navigation,sizeof(s->navigation)))s->direct_reads|=512;
            if(creation_read(s->agent,0,s->parameters,sizeof(s->parameters)))s->direct_reads|=1024;
        }
    }
}
static unsigned creation_snapshot_read(creation_snapshot *s,const creation_token *token,void *tls) {
    creation_snapshot second;creation_snapshot_once(s,token,tls);creation_snapshot_once(&second,token,tls);
    s->stable=!memcmp(s,&second,sizeof(*s));
    unsigned error=s->reads!=31 || s->view>1 || s->read>1 || s->write>1 || s->read+s->write!=1 || !s->stable?CREATION_E_READ:0;
    if(s->object && (s->identity.stable!=1 || s->identity.first.object_state!=PRESENCE_PRESENT))error|=CREATION_E_IDENTITY;
    /* A freshly allocated controller has not yet installed its vtable. Keep
     * those partial reads explicit; only completed return boundaries demand
     * a fully matching relationship. */
    if(s->controller && !(s->direct_reads&1))error|=CREATION_E_READ;
    if(s->agent && ((s->direct_reads&1920)!=1920 || s->agent_owner!=s->object))error|=CREATION_E_IDENTITY;
    return error;
}
static unsigned creation_raw_object(uint64_t object,uint64_t *runtime) {
    uint64_t id=0,again=0,type=0;const char *why=NULL;
    if(!object || !creation_read(object,0,&type,8) || !creation_read(object,8,&id,8) || !id ||
       type!=(uintptr_t)(image+0x2b99a88) || world_identity_lookup(id) ||
       !registry_macro_detail((void*)(uintptr_t)object,"ship_arg_xl_builder_01_a_macro",&why) ||
       !creation_read(object,8,&again,8) || again!=id)return CREATION_E_IDENTITY;
    *runtime=id;return 0;
}
static unsigned creation_ref_object(uint64_t ref,uint64_t *object,uint64_t *runtime) {
    uint64_t first=0,second=0;
    if(!creation_read(ref,0,&first,8) || creation_raw_object(first,runtime) ||
       !creation_read(ref,0,&second,8) || first!=second)return CREATION_E_IDENTITY;
    *object=first;return 0;
}
/* The Static controller has no audited owner member. Only its exact type and
 * the selected object's published pointer are observed; never read Nav fields. */
static int creation_type(uint64_t pointer,unsigned rva) {
    uint64_t a=0,b=0;return creation_read(pointer,0,&a,8) && creation_read(pointer,0,&b,8) &&
        a==b && a==(uintptr_t)(image+rva);
}
static unsigned creation_selected_identity(const creation_token *t) {
    uint64_t runtime=0,owner=0;const char *why=NULL;
    return creation_raw_object(t->object,&runtime) || runtime!=t->runtime ||
        !t->true_owner || !creation_read(t->object,0x348,&owner,8) || owner!=t->true_owner ||
        !registry_owner_detail((void*)(uintptr_t)t->object,"player",&why)?CREATION_E_IDENTITY:0;
}
static int creation_ancillary_caller(unsigned kind,uint64_t caller) {
    int allowed=kind==CREATION_SETTER ? caller==0x7458c7 || caller==0x51b71e || caller==0x6c6e5a || caller==0x746bb8 || caller==0x6cd8be :
        kind==CREATION_RANDOM && (caller==0x6bd8fd || caller==0x14ddd3a || caller==0x5061ef || caller==0x501440 ||
            caller==0x4fe58b || caller==0x50e56e || caller==0x50e6d9 || caller==0x50ee6c || caller==0x50f2f2);
    if(!allowed)return 0;
    for(unsigned i=0;i<sizeof(creation_ancillary_callers)/sizeof(*creation_ancillary_callers);i++) {
        const creation_caller_pin *p=&creation_ancillary_callers[i];
        if(caller==p->caller)return !memcmp(image+p->site,p->bytes,p->width);
    }
    return 0;
}
static unsigned creation_aux_read(creation_record *r,unsigned after) {
    uint64_t a=0,b=0;unsigned error=0;
    if(r->kind==CREATION_RANDOM) {
        if(creation_read(r->args[0],0,&a,8) && creation_read(r->args[0],0,&b,8) && a==b) {
            if(after)r->rng_after=a;else r->rng_before=a;r->aux_reads|=after?8:4;
        } else error|=CREATION_E_READ;
    }
    if(r->kind==CREATION_SETTER && r->subject==1) {
        if(creation_read(r->args[0],0x200,&a,8) && creation_read(r->args[0],0x200,&b,8) && a==b) {
            if(after)r->published_after=a;else r->published_before=a;r->aux_reads|=after?2:1;
            /* Two stable reads per source do not make conflicting sources
             * coherent. Join this publication sample to the corresponding
             * identity sample before interpreting it as a Static/Nav change. */
            const registry_presence_sample *identity=after?&r->after.identity.first:&r->before.identity.first;
            uint64_t expected_type=after?r->proposed_type:r->stage==1?0:0x2a34b38;
            if(identity->controller!=a || identity->controller_type!=expected_type)error|=CREATION_E_IDENTITY;
        } else error|=CREATION_E_READ;
        if(!after && creation_read(r->args[1],0,&a,8) && creation_read(r->args[1],0,&b,8) && a==b)r->proposed_type=registry_local_rva((void*)(uintptr_t)a);
        else if(!after)error|=CREATION_E_READ;
    }
    return error;
}
static unsigned creation_entry_identity(unsigned kind,const uint64_t *args,creation_token *t) {
    unsigned error=0;uint64_t object=0,runtime=0;
    if(kind==CREATION_INITIALIZE || kind==CREATION_PILOT) {
        error=creation_ref_object(args[kind==CREATION_INITIALIZE?1:2],&object,&runtime);
        if(!error && t->object && (t->object!=object || t->runtime!=runtime))error=CREATION_E_IDENTITY;
        if(!error && kind==CREATION_INITIALIZE && !t->object){t->object=object;t->runtime=runtime;}
        if(kind==CREATION_INITIALIZE && args[2])error|=CREATION_E_BRANCH; /* Exact null-explicit-owner MD path only. */
        if(kind==CREATION_PILOT && !error) {
            const char *why=NULL;uint64_t owner=0;
            if(!registry_owner_detail((void*)(uintptr_t)object,"player",&why) || !creation_read(object,0x348,&owner,8) || !owner ||
               (t->true_owner && t->true_owner!=owner))error|=CREATION_E_IDENTITY;
            else t->true_owner=owner;
        }
    } else if(kind==CREATION_NAVIGATION) {
        uint64_t published=0;
        if(t->state!=4 || !t->prior_static || !creation_type(t->prior_static,0x2a34b38) ||
           !creation_read(t->object,0x200,&published,8) || published!=t->prior_static ||
           args[1]!=t->object || args[2]!=t->prior_static || !args[0] || args[0]==t->prior_static || t->controller || creation_selected_identity(t))error|=CREATION_E_IDENTITY;
        else t->controller=args[0];
    } else if(kind==CREATION_AGENT) {
        if(t->state!=4 || !t->controller || args[1]!=t->object || !args[0] || t->agent || creation_selected_identity(t))error|=CREATION_E_IDENTITY;
    } else if(kind==CREATION_SETTER) {
        error|=creation_selected_identity(t);
        if(t->state<4) {
            if(t->prior_static || !creation_type(args[1],0x2a34b38))error|=CREATION_E_IDENTITY;
        } else if(!t->controller || args[1]!=t->controller || !creation_type(args[1],0x2c51158))error|=CREATION_E_IDENTITY;
    } else if(kind==CREATION_EVENT_QUERY || kind==CREATION_POST) {
        if(!t->object || args[0]!=t->object)error|=CREATION_E_IDENTITY;
    } else if(kind==CREATION_BUILDER) {
        if(t->state!=4 || !t->builder_order || args[0]!=t->handle || args[1]!=t->runtime ||
           t->builder_runtime!=t->runtime || creation_current || stats.issued!=t->execution_tick)error|=CREATION_E_TOKEN;
    }
    return error;
}
static unsigned creation_enter_context(unsigned kind,uintptr_t caller,const uint64_t *args,unsigned count,void *tls) {
    if(!InterlockedCompareExchange(&creation_window,0,0) || !InterlockedCompareExchange(&creation_enabled,0,0))return 0;
    creation_record record={0};creation_token token;unsigned index=0;
    AcquireSRWLockExclusive(&creation_lock);
    if(!creation_enabled || creation_closing || !stats.active || boot_phase!=3 ||
       creation_selected.state<1 || creation_selected.state>4)goto ignored;
    /* Only the reserved UI creation window is observed. A covered foreign
     * thread with the exact candidate owner is an explicit unsupported access. */
    if(GetCurrentThreadId()!=creation_selected.thread) {
        if(!creation_selected.object || !((kind==CREATION_NAVIGATION || kind==CREATION_AGENT) ? args[1]==creation_selected.object :
           kind==CREATION_SETTER && args[0]==creation_selected.object))goto ignored;
        record.foreign=1;record.error|=CREATION_E_THREAD;
    }
    if(!creation_current && !record.foreign && kind!=CREATION_GENERATE && kind!=CREATION_NAVIGATION && kind!=CREATION_SETTER && kind!=CREATION_BUILDER)goto ignored;
    if(!creation_current && kind==CREATION_NAVIGATION && args[1]!=creation_selected.object)goto ignored;
    if(!creation_current && kind==CREATION_SETTER && args[0]!=creation_selected.object)goto ignored;
    if(count>10 || !kind || (kind>CREATION_RANGE && kind!=CREATION_BUILDER)){creation_error_locked(CREATION_E_SCOPE);goto ignored;}
    if(creation_count==CREATION_CAPACITY){++creation_dropped;creation_error_locked(CREATION_E_OVERFLOW);goto ignored;}
    record.kind=kind;record.count=count;record.parent=creation_current;
    record.stage=creation_selected.state<4?1:2;record.subject=1;
    if(stats.issued!=creation_selected.execution_tick)record.error|=CREATION_E_TOKEN;
    if(kind==CREATION_SETTER && !record.foreign) {
        if(!creation_selected.object){record.subject=3;++creation_selected.unresolved_count;}
        else if(args[0]!=creation_selected.object){record.subject=2;++creation_selected.excluded_count;}
        if(!args[0])record.error|=CREATION_E_IDENTITY;
    }
    if((record.stage==1 && (kind==CREATION_NAVIGATION || kind==CREATION_AGENT || kind==CREATION_EVENT_QUERY || kind==CREATION_POST || kind==CREATION_BUILDER)) ||
       (record.stage==2 && (kind==CREATION_GENERATE || kind==CREATION_INITIALIZE || kind==CREATION_PILOT)))record.error|=CREATION_E_SCOPE;
    record.depth=creation_current?creation_records[creation_current-1].depth+1:1;
    if(record.depth>CREATION_DEPTH){++creation_dropped;creation_error_locked(CREATION_E_OVERFLOW);goto ignored;}
    record.caller=registry_local_rva((void*)caller);memcpy(record.args,args,count*8);
    if(kind<=CREATION_HOOK_COUNT && kind!=CREATION_RANDOM && kind!=CREATION_SETTER && record.caller!=creation_patch_contracts[kind-1].expected_caller)record.error|=CREATION_E_CALLER;
    if(kind==CREATION_SETTER && !creation_ancillary_caller(kind,record.caller))record.error|=CREATION_E_CALLER;
    if(kind==CREATION_SETTER && record.subject==1 && record.caller!=0x6cd8be)record.error|=CREATION_E_CALLER;
    if(kind==CREATION_RANDOM && record.caller!=0xe85531 && record.caller!=0xea90e1) {
        record.subject=4;++creation_selected.ancillary_rng;
        if(record.stage!=1 || !creation_ancillary_caller(kind,record.caller))record.error|=CREATION_E_CALLER;
        unsigned parent_kind=creation_current?creation_records[creation_current-1].kind:0;
        if(parent_kind!=((record.caller==0x6bd8fd || record.caller==0x14ddd3a)?CREATION_PILOT:CREATION_INITIALIZE))record.error|=CREATION_E_SCOPE;
    }
    if(kind==CREATION_RANDOM && (!creation_current ||
       (record.caller==0xe85531 && creation_records[creation_current-1].kind!=CREATION_AGENT) ||
       (record.caller==0xea90e1 && creation_records[creation_current-1].kind!=CREATION_NAVIGATION)))record.error|=CREATION_E_SCOPE;
    if(!record.foreign && kind==CREATION_GENERATE) {
        if(creation_selected.state!=1 || creation_selected.generate_count || args[3])record.error|=CREATION_E_TOKEN;
        if(creation_selected.state==1)creation_selected.state=2;++creation_selected.generate_count;
    } else if(creation_selected.state<2)record.error|=CREATION_E_TOKEN;
    if(!record.foreign && record.subject==1) {
        if(kind==CREATION_INITIALIZE)++creation_selected.initialize_count;
        if(kind==CREATION_PILOT)++creation_selected.pilot_count;
        if(kind==CREATION_NAVIGATION)++creation_selected.navigation_count;
        if(kind==CREATION_AGENT)++creation_selected.agent_count;
        if(kind==CREATION_SETTER && record.stage==2)++creation_selected.setter_count;
        record.error|=creation_entry_identity(kind,args,&creation_selected);
    }
    token=creation_selected;record.handle=token.handle;record.phase_order=token.phase_order;
    record.enter=registry_md_next_order();index=++creation_count;creation_current=index;
    creation_records[index-1]=record;++creation_open;if(creation_open>creation_peak)creation_peak=creation_open;
ignored:
    ReleaseSRWLockExclusive(&creation_lock);
    if(!index)return 0;
    record.error|=creation_snapshot_read(&record.before,&token,tls);
    if(kind==CREATION_RANDOM && record.subject==1 && args[0]!=record.before.rng_pointer)record.error|=CREATION_E_IDENTITY;
    if(kind==CREATION_RANDOM && record.subject==4 && args[0]!=record.before.rng_pointer && record.caller!=0x14ddd3a)record.error|=CREATION_E_IDENTITY;
    record.error|=creation_aux_read(&record,0);
    if(!creation_hooks_owned())record.error|=CREATION_E_OWNERSHIP;
    AcquireSRWLockExclusive(&creation_lock);creation_records[index-1].before=record.before;creation_records[index-1].error|=record.error;
    creation_records[index-1].aux_reads=record.aux_reads;creation_records[index-1].proposed_type=record.proposed_type;
    creation_records[index-1].published_before=record.published_before;creation_records[index-1].rng_before=record.rng_before;
    creation_error_locked(record.error);ReleaseSRWLockExclusive(&creation_lock);return index;
}
static void creation_leave_context(unsigned index,uint64_t result,unsigned result_kind,void *tls) {
    if(!index)return;creation_record record;creation_token token;unsigned error=0;
    AcquireSRWLockExclusive(&creation_lock);
    if(index>creation_count || creation_records[index-1].complete){creation_error_locked(CREATION_E_SCOPE);ReleaseSRWLockExclusive(&creation_lock);return;}
    record=creation_records[index-1];if(creation_current!=index)error|=CREATION_E_SCOPE;
    if(!record.foreign && record.kind==CREATION_GENERATE) {
        uint64_t object=0,runtime=0;error|=creation_ref_object(record.args[1],&object,&runtime);
        if(result!=record.args[1] || object!=creation_selected.object || runtime!=creation_selected.runtime)error|=CREATION_E_RETURN;
        if(record.stage==1 && creation_selected.state==2)creation_selected.state=3;else error|=CREATION_E_TOKEN;
    } else if(!record.foreign && record.kind==CREATION_AGENT) {
        if(result!=record.args[0] || !result || creation_selected.agent)error|=CREATION_E_RETURN;
        else creation_selected.agent=result;
    }
    token=creation_selected;ReleaseSRWLockExclusive(&creation_lock);
    error|=creation_snapshot_read(&record.after,&token,tls);
    error|=creation_aux_read(&record,1);
    if(token.true_owner && record.after.identity.first.owner!=token.true_owner)error|=CREATION_E_IDENTITY;
    if(record.kind==CREATION_NAVIGATION && ((record.after.direct_reads&127)!=127 ||
       record.after.controller_owner!=token.object || record.after.controller_agent!=token.agent ||
       record.after.slots!=token.agent+0xad0))error|=CREATION_E_IDENTITY;
    if(record.kind==CREATION_SETTER && record.subject==1 && record.stage==1 &&
       (record.proposed_type!=0x2a34b38 || record.published_before || record.published_after!=record.args[1] ||
        record.after.identity.first.controller!=record.args[1] || !creation_type(record.args[1],0x2a34b38)))error|=CREATION_E_IDENTITY;
    if(record.kind==CREATION_SETTER && record.subject==1 && record.stage==2 && (record.published_before!=token.prior_static ||
       record.published_after!=token.controller || record.after.identity.first.controller!=token.controller ||
       record.after.controller_owner!=token.object || record.after.controller_agent!=token.agent ||
       record.after.slots!=token.agent+0xad0 || record.after.agent_owner!=token.object))error|=CREATION_E_IDENTITY;
    if(record.kind==CREATION_BUILDER && (creation_selected_identity(&token) ||
       record.after.identity.first.controller!=token.controller || !token.controller || !token.agent ||
       record.after.controller_owner!=token.object || record.after.controller_agent!=token.agent ||
       record.after.slots!=token.agent+0xad0 || record.after.agent_owner!=token.object))error|=CREATION_E_IDENTITY;
    AcquireSRWLockExclusive(&creation_lock);
    creation_record *stored=&creation_records[index-1];stored->after=record.after;stored->result=result;stored->result_kind=result_kind;
    stored->aux_reads=record.aux_reads;stored->published_after=record.published_after;stored->rng_after=record.rng_after;
    if(record.kind==CREATION_SETTER && record.subject==1 && record.stage==1 && !record.foreign) {
        ++creation_selected.static_count;
        if(!stored->error && !error)creation_selected.prior_static=record.args[1];
    }
    stored->error|=error;stored->leave=registry_md_next_order();stored->complete=1;
    if(creation_open)--creation_open;else {error|=CREATION_E_SCOPE;stored->error|=CREATION_E_SCOPE;}
    creation_current=record.parent;creation_error_locked(error);
    if(record.kind==CREATION_BUILDER) {
        creation_selected.callback_order=stored->leave;creation_selected.callback_tick=stats.issued;
        creation_selected.b_closed=1;creation_selected.state=5;InterlockedExchange(&creation_window,0);
        if(creation_closing || !stats.active || creation_open || stats.issued!=creation_selected.execution_tick ||
           creation_selected.navigation_count!=1 || creation_selected.agent_count!=1 || creation_selected.setter_count!=1)
            creation_error_locked(CREATION_E_SCOPE);
    }
    ReleaseSRWLockExclusive(&creation_lock);
    registry_creation_trace(index,1);
}
typedef void *(*creation_tls_reader)(void);
static unsigned creation_enter_with_tls_reader(unsigned kind,uintptr_t caller,const uint64_t *args,unsigned count,creation_tls_reader read_tls) {
    if(!InterlockedCompareExchange(&creation_window,0,0) || !InterlockedCompareExchange(&creation_enabled,0,0) || !stats.active || boot_phase!=3)return 0;
    return creation_enter_context(kind,caller,args,count,read_tls());
}
static void creation_leave_with_tls_reader(unsigned index,uint64_t result,unsigned result_kind,creation_tls_reader read_tls) {
    if(!index)return;
    creation_leave_context(index,result,result_kind,read_tls());
}
static unsigned creation_enter(unsigned kind,uintptr_t caller,const uint64_t *args,unsigned count) {
    return creation_enter_with_tls_reader(kind,caller,args,count,registry_origin_tls);
}
static void creation_leave(unsigned index,uint64_t result,unsigned result_kind) {creation_leave_with_tls_reader(index,result,result_kind,registry_origin_tls);}

/* Called only by the live wrapper's finally. Never inspect native arguments,
 * TLS, out-references or a context whose lifetime the exception may have ended.
 * A failed original is incomplete, not a normal return with a fabricated zero. */
static void creation_abort(unsigned index) {
    if(!index)return;
    AcquireSRWLockExclusive(&creation_lock);
    InterlockedExchange(&creation_window,0);
    if(index>creation_count)creation_error_locked(CREATION_E_SCOPE|CREATION_E_UNWIND);
    else {
        creation_record *r=&creation_records[index-1];
        unsigned error=CREATION_E_UNWIND;
        if(!r->unwound && !r->complete) {
            if(creation_current!=index)error|=CREATION_E_SCOPE;
            if(creation_open)--creation_open;else error|=CREATION_E_SCOPE;
            creation_current=r->parent;r->unwound=1;
        }
        r->error|=error;creation_error_locked(error);
    }
    ReleaseSRWLockExclusive(&creation_lock);
    registry_creation_trace(index,2);
}

/* A matched deferred callback is a diagnostic boundary, not an engine call or
 * permission to bind. The caller invokes the unchanged registry_bind next.
 * Foreign/stale callbacks never close the selected window or inherit its TLS
 * nesting. STOP can win either admission or completion; both remain failures. */
static void creation_bind_note_using(uint64_t handle,uint64_t runtime,creation_tls_reader read_tls) {
    if(!InterlockedCompareExchange(&creation_enabled,0,0))return;
    AcquireSRWLockExclusive(&creation_lock);
    int eligible=creation_enabled && !creation_closing && stats.active && boot_phase==3 && creation_selected.state==4;
    if(eligible && (handle!=creation_selected.handle || runtime!=creation_selected.runtime)) {
        ++creation_selected.foreign_callbacks;eligible=0;
    }
    if(eligible) {
        unsigned error=(GetCurrentThreadId()!=creation_selected.thread?CREATION_E_THREAD:0)|
            (stats.issued!=creation_selected.execution_tick?CREATION_E_TOKEN:0)|(creation_current?CREATION_E_SCOPE:0);
        if(error){creation_error_locked(error);eligible=0;}
    }
    ReleaseSRWLockExclusive(&creation_lock);if(!eligible)return;
    uint64_t args[]={handle,runtime};DWORD saved=GetLastError();
    unsigned index=creation_enter_with_tls_reader(CREATION_BUILDER,0,args,2,read_tls);
    creation_leave_with_tls_reader(index,0,0,read_tls);SetLastError(saved);
}
static void creation_bind_note(uint64_t handle,uint64_t runtime) {creation_bind_note_using(handle,runtime,registry_origin_tls);}
static int creation_bind_arguments(const char *handle_text,size_t handle_length,const char *runtime_text,size_t runtime_length,
        uint64_t *handle,uint64_t *runtime) {
    /* Reuse the exact native registry parsers once. A malformed callback has
     * no selected identity and must never become an observation deadline. */
    if(!registry_handle(handle_text,handle_length,handle) || !registry_runtime_text(runtime_text,runtime_length,runtime))return 0;
    creation_bind_note(*handle,*runtime);return 1;
}

/* The command reserves at tick 5; MD evaluates the queued Setup action either
 * in that tick or the immediately following native tick. Join that immutable
 * event tick to the exact still-pending reservation, rather than relabeling the
 * reservation tick as execution. This is diagnostic admission, never binding. */
static int creation_reservation_tick(uint64_t handle,uint64_t event_tick,uint64_t *reserved_tick) {
    int valid=0;unsigned pending=0,actor=0;dynamic_ship reservation={0};
    AcquireSRWLockShared(&registry_lock);
    if(dynamic_ship_count<=ACTOR_DYNAMIC_COUNT) {
        for(unsigned i=0;i<dynamic_ship_count;i++) if(!dynamic_ships[i].bound_tick) {
            ++pending;
            if(dynamic_ships[i].handle==handle) {reservation=dynamic_ships[i];actor=i+ACTOR_DYNAMIC_FIRST;}
        }
        valid=pending==1 && actor && !registry_failures && !actor_components[actor-1] && !actor_ids[actor-1];
    }
    ReleaseSRWLockShared(&registry_lock);
    if(!valid || handle!=0x0000000500000003ULL || reservation.reserved_tick!=5 ||
       reservation.reserved_tick!=handle>>32 || reservation.bound_tick || reservation.prepared_tick ||
       memcmp(reservation.macro,"ship_arg_xl_builder_01_a_macro",sizeof("ship_arg_xl_builder_01_a_macro")) ||
       memcmp(reservation.owner,"player",sizeof("player")) ||
       event_tick<reservation.reserved_tick || event_tick>reservation.reserved_tick+1) return 0;
    *reserved_tick=reservation.reserved_tick;return 1;
}
static void creation_md_note(unsigned kind,unsigned phase,unsigned edge,uint64_t order,uint64_t event_tick,uint64_t handle,
        uint64_t payload,unsigned pending,unsigned error,unsigned thread,unsigned reads) {
    if(!InterlockedCompareExchange(&creation_enabled,0,0))return;
    uint64_t reserved_tick=0;int reservation_valid=0;
    if(kind==1 && phase==1 && edge==1)reservation_valid=creation_reservation_tick(handle,event_tick,&reserved_tick);
    AcquireSRWLockExclusive(&creation_lock);
    if(!creation_enabled || creation_closing || !stats.active || boot_phase!=3)goto done;
    if(kind==1 && phase==1 && edge==1) {
        if(creation_selected.state || creation_failures || !reservation_valid || pending!=1 || event_tick!=stats.issued ||
           thread!=ui_thread || error || reads!=31) {InterlockedExchange(&creation_window,0);creation_error_locked(CREATION_E_TOKEN);goto done;}
        creation_selected.handle=handle;creation_selected.phase_order=order;creation_selected.thread=thread;creation_selected.state=1;
        creation_selected.reserved_tick=reserved_tick;creation_selected.execution_tick=event_tick;
        InterlockedExchange(&creation_window,1);
    } else if(kind==1 && phase==2 && edge==0) {
        creation_selected.phase2_order=order;
        if(event_tick!=creation_selected.execution_tick || event_tick!=stats.issued ||
           handle!=creation_selected.handle || thread!=creation_selected.thread || error || reads!=31 || pending!=1 ||
           creation_selected.state!=3 || creation_open || creation_selected.generate_count!=1 ||
           creation_selected.initialize_count!=1 || creation_selected.pilot_count!=1 ||
           creation_selected.navigation_count || creation_selected.agent_count || creation_selected.setter_count ||
           creation_selected.static_count!=1 || !creation_selected.prior_static)
            creation_error_locked(CREATION_E_SCOPE);
        /* Unresolved admission is retained, not retroactively rewritten. It
         * must prove a different address once the selected identity exists. */
        for(unsigned i=0;i<creation_count;i++)if(creation_records[i].subject==3 &&
            (!creation_records[i].args[0] || creation_records[i].args[0]==creation_selected.object))creation_error_locked(CREATION_E_IDENTITY);
        creation_selected.a_closed=1;creation_selected.state=4;
    } else if(kind==2 && edge==0) {
        creation_selected.builder_order=order;creation_selected.builder_runtime=payload;
        if(event_tick!=creation_selected.execution_tick || event_tick!=stats.issued ||
           creation_selected.state!=4 || !payload || payload!=creation_selected.runtime ||
           handle!=creation_selected.handle || thread!=creation_selected.thread || error)creation_error_locked(CREATION_E_IDENTITY);
    }
done:
    ReleaseSRWLockExclusive(&creation_lock);
}
static unsigned creation_range_enter(const registry_origin_scope *scope) {
    if(!InterlockedCompareExchange(&creation_window,0,0) || !InterlockedCompareExchange(&creation_enabled,0,0) || scope->source!=REGISTRY_ORIGIN_RANGE || !creation_current)return 0;
    DWORD saved_error=GetLastError();
    uint64_t args[]={(uintptr_t)scope->exec,(uintptr_t)scope->context,(uintptr_t)scope->program,scope->begin,scope->end,scope->mode};
    unsigned index=creation_enter(CREATION_RANGE,scope->caller,args,6);
    if(index) {
        interrupt_origin origin=interrupt_origin_read(scope->caller);creation_record copy={0};uint64_t object=0;
        AcquireSRWLockShared(&creation_lock);object=creation_selected.object;ReleaseSRWLockShared(&creation_lock);
        copy.range_verified=origin.caller;copy.range_source=origin.source;
        if(scope->exec && creation_read((uintptr_t)scope->exec,0x18,&copy.range_pc,8))copy.range_reads|=1;
        if(scope->context && creation_read((uintptr_t)scope->context,0x140,&copy.range_context_pc,8))copy.range_reads|=2;
        uint64_t entity=0;
        if(scope->context && creation_read((uintptr_t)scope->context,0x120,&entity,8) && entity &&
           creation_read(entity,8,&copy.range_entity,8) && copy.range_entity) {
            for(unsigned depth=0;entity && depth<16;depth++) {
                if(entity==object){copy.range_depth=depth;copy.range_reads|=4;break;}
                uint64_t next=0;if(!creation_read(entity,0x70,&next,8) || next==entity)break;entity=next;
            }
        }
        copy.range_script_hash=scope->context?ai_script(scope->context,copy.range_script):0;
        if(copy.range_script_hash)copy.range_reads|=8;else strcpy(copy.range_script,"unsupported");
        unsigned error=(origin.source!=1?CREATION_E_CALLER:0)|(copy.range_reads!=15?CREATION_E_READ:0);
        AcquireSRWLockExclusive(&creation_lock);creation_record *r=&creation_records[index-1];
        r->range_verified=copy.range_verified;r->range_source=copy.range_source;r->range_pc=copy.range_pc;
        r->range_context_pc=copy.range_context_pc;r->range_entity=copy.range_entity;r->range_depth=copy.range_depth;
        r->range_script_hash=copy.range_script_hash;r->range_reads=copy.range_reads;memcpy(r->range_script,copy.range_script,sizeof(r->range_script));
        r->error|=error;creation_error_locked(error);ReleaseSRWLockExclusive(&creation_lock);
    }
    SetLastError(saved_error);return index;
}
static void creation_range_leave(unsigned index,uint64_t result) {
    DWORD saved_error=GetLastError();creation_leave(index,result,1);SetLastError(saved_error);
}
static void creation_range_abort(unsigned index) {
    DWORD saved_error=GetLastError();creation_abort(index);SetLastError(saved_error);
}

#include "lockstep_creation_trace_forward.h"

static int creation_hooks_owned(void) {
    int ok=1;AcquireSRWLockShared(&creation_lock);
    if(creation_owned!=(1u<<CREATION_HOOK_COUNT)-1)ok=0;
    for(unsigned i=0;ok && i<CREATION_HOOK_COUNT;i++) {
        unsigned char actual[16];const creation_patch_contract *p=&creation_patch_contracts[i];
        if(!read_local(image+p->word,actual,p->width) || memcmp(actual,creation_patched_words[i],p->width))ok=0;
    }
    ReleaseSRWLockShared(&creation_lock);return ok;
}
static int creation_pins_valid(void) {
    for(unsigned i=0;i<sizeof(creation_pins)/sizeof(*creation_pins);i++) {
        const creation_pin *p=&creation_pins[i];if(!hash_ok(image+p->rva,p->length,p->sha256))return 0;
    }
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++) {
        const creation_patch_contract *p=&creation_patch_contracts[i];unsigned size=p->prefix>p->width?p->prefix:p->width;
        if(memcmp(image+p->word,p->expected,size) || !thunk_slot_ready(p->thunk))return 0;
    }
    return 1;
}
/* Lock held. A failed protection restore/flush is distinct from whether CAS
 * acquired the native word; rollback must retain ownership of a changed word. */
typedef BOOL (WINAPI *creation_protect_fn)(LPVOID,SIZE_T,DWORD,PDWORD);
typedef BOOL (WINAPI *creation_flush_fn)(HANDLE,LPCVOID,SIZE_T);
static int creation_swap_using(unsigned index,const uint64_t *expected,const uint64_t *desired,int *changed,
        creation_protect_fn protect,creation_flush_fn flush) {
    const creation_patch_contract *p=&creation_patch_contracts[index];DWORD old,ignored;*changed=0;
    if(!protect(image+p->word,p->width,PAGE_EXECUTE_READWRITE,&old))return 0;
    if(p->width==8) {
        uint64_t found=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+p->word),(LONG64)desired[0],(LONG64)expected[0]);
        *changed=found==expected[0];
    } else {
        unsigned __int128 a,b;memcpy(&a,expected,16);memcpy(&b,desired,16);
        unsigned __int128 found=__sync_val_compare_and_swap((unsigned __int128*)(image+p->word),a,b);*changed=found==a;
    }
    int protected_ok=protect(image+p->word,p->width,old,&ignored)!=0;
    int flushed=flush(GetCurrentProcess(),image+p->word,p->width)!=0;
    return *changed && protected_ok && flushed;
}
static int creation_swap(unsigned index,const uint64_t *expected,const uint64_t *desired,int *changed) {
    return creation_swap_using(index,expected,desired,changed,VirtualProtect,FlushInstructionCache);
}
typedef int (*creation_swap_fn)(unsigned,const uint64_t *,const uint64_t *,int *);
static void creation_restore_using(creation_swap_fn change) {
    InterlockedExchange(&creation_window,0);
    AcquireSRWLockExclusive(&creation_lock);creation_closing=1;InterlockedExchange(&creation_enabled,0);
    creation_rng_stop_locked();
    if(creation_selected.state && (!creation_selected.a_closed || !creation_selected.b_closed))creation_error_locked(CREATION_E_SCOPE);
    if(creation_open>creation_restore_open)creation_restore_open=creation_open;
    for(unsigned i=CREATION_HOOK_COUNT;i>0;i--) {
        unsigned index=i-1;if(!(creation_owned&(1u<<index)))continue;int changed=0;
        int ok=change(index,creation_patched_words[index],creation_original_words[index],&changed);
        if(changed)creation_owned&=~(1u<<index);
        if(!ok){++creation_restore_failures;creation_error_locked(CREATION_E_OWNERSHIP);}
    }
    ReleaseSRWLockExclusive(&creation_lock);
}
static void creation_restore(void) {creation_restore_using(creation_swap);}
static int creation_configure_using(const char *manifest,size_t length,creation_swap_fn change) {
    if(!manifest || length!=sizeof(CREATION_MANIFEST)-1 || memcmp(manifest,CREATION_MANIFEST,length))return 0;
    /* Optional NPC execution/pilot/deadline stacks have no complete abnormal
     * closure contract here. They are off in the accepted construction fixture. */
    if(!change || (boot_options&((1ULL<<47)|(1ULL<<48)|(1ULL<<49))))return 0;
    if(spawn_rng_installed)return 0; /* native_gate_spawn_rng owns the Generate entry word */
    if(InterlockedCompareExchange(&creation_started,0,0))return creation_enabled && !creation_closing && creation_hooks_owned();
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !registry_md_installed || !registry_origin_enabled || !world_identity_installed ||
       !IsProcessorFeaturePresent(PF_COMPARE_EXCHANGE128) || !creation_pins_valid() || !thunk_unwind_prepare(3456))return 0;
    for(unsigned i=0;i<CREATION_HOOK_COUNT;i++) {
        const creation_patch_contract *p=&creation_patch_contracts[i];unsigned at=p->call_offset?p->call_offset:0;
        unsigned target=p->thunk+(p->call_offset?0:32);
        intptr_t distance=(intptr_t)(thunk+target)-(intptr_t)(image+p->word+at+5);
        if(distance<INT32_MIN || distance>INT32_MAX)return 0;
        memcpy(creation_original_words[i],image+p->word,p->width);memcpy(creation_patched_words[i],creation_original_words[i],16);
        unsigned char *word=(unsigned char*)creation_patched_words[i];if(!p->call_offset)word[0]=0xe9;
        int32_t rel=(int32_t)distance;memcpy(word+at+1,&rel,4);
    }
    AcquireSRWLockExclusive(&creation_lock);creation_started=1;int ok=stats.active && !creation_closing;
    for(unsigned i=0;ok && i<CREATION_HOOK_COUNT;i++) {
        int changed=0;ok=change(i,creation_original_words[i],creation_patched_words[i],&changed);
        if(changed)creation_owned|=1u<<i;
    }
    if(ok)InterlockedExchange(&creation_enabled,1);else creation_error_locked(CREATION_E_OWNERSHIP);
    ReleaseSRWLockExclusive(&creation_lock);if(!ok)creation_restore();return ok;
}
static int creation_configure(const char *manifest,size_t length) {return creation_configure_using(manifest,length,creation_swap);}

/* Local diagnostic only: pointer addresses and physical bank order are never
 * portable identities or lockstep hashes. Eighteen bounded parts per record;
 * an unfinished record is readable only after closing and remains invalid. */
#define CREATION_PARTS 18u
static int creation_status(char *out,size_t capacity) {
    AcquireSRWLockShared(&creation_lock);creation_token t=creation_selected;
    int n=snprintf(out,capacity,"schema=2 started=%ld enabled=%ld closing=%ld count=%u parts=%u failures=%llu first_error=%x dropped=%llu open=%llu peak=%llu restore_open=%llu restore_failures=%llu owned=%x handle=%016llx state=%u phase1=%llu phase2=%llu builder_order=%llu builder_runtime=%016llx thread=%u object=%016llx runtime=%016llx controller=%016llx agent=%016llx generate=%u initialize=%u pilot=%u navigation=%u agent_calls=%u setter=%u sector_pose_supported=0 enrollment_supported=0 reserved_tick=%llu execution_tick=%llu prior_static=%016llx callback_order=%llu callback_tick=%llu a_closed=%u b_closed=%u a_error=%x b_error=%x static_calls=%u excluded_setters=%u unresolved_setters=%u ancillary_rng=%u foreign_callbacks=%u whole_rng_supported=0 true_owner=%016llx",
        creation_started,creation_enabled,creation_closing,creation_count,CREATION_PARTS,creation_failures,creation_first_error,creation_dropped,
        creation_open,creation_peak,creation_restore_open,creation_restore_failures,creation_owned,t.handle,t.state,t.phase_order,t.phase2_order,t.builder_order,
        t.builder_runtime,t.thread,t.object,t.runtime,t.controller,t.agent,t.generate_count,t.initialize_count,t.pilot_count,t.navigation_count,t.agent_count,t.setter_count,
        t.reserved_tick,t.execution_tick,t.prior_static,t.callback_order,t.callback_tick,t.a_closed,t.b_closed,t.a_error,t.b_error,
        t.static_count,t.excluded_count,t.unresolved_count,t.ancillary_rng,t.foreign_callbacks,t.true_owner);
    ReleaseSRWLockShared(&creation_lock);return n>0 && (size_t)n<capacity?n:0;
}
static int creation_row(unsigned index,unsigned part,char *out,size_t capacity) {
    if(part>=CREATION_PARTS)return 0;creation_record r;
    AcquireSRWLockShared(&creation_lock);
    int exists=index<creation_count && (creation_records[index].complete || creation_closing);
    if(exists)r=creation_records[index];ReleaseSRWLockShared(&creation_lock);if(!exists)return 0;
    int n=snprintf(out,capacity,"schema=2 index=%u part=%u complete=%u ",index+1,part,r.complete);
    if(n<=0 || (size_t)n>=capacity)return 0;char *tail=out+n;size_t remaining=capacity-(size_t)n;int added=0;
    if(part==0)added=snprintf(tail,remaining,"kind=%u enter=%llu leave=%llu handle=%016llx phase1=%llu parent=%u depth=%u caller=%llx argc=%u result=%016llx result_kind=%u error=%x foreign=%u",
        r.kind,r.enter,r.leave,r.handle,r.phase_order,r.parent,r.depth,r.caller,r.count,r.result,r.result_kind,r.error,r.foreign);
    else if(part==1)added=snprintf(tail,remaining,"args=%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx",
        r.args[0],r.args[1],r.args[2],r.args[3],r.args[4],r.args[5],r.args[6],r.args[7],r.args[8],r.args[9]);
    else if(part==2 || part==3) {
        const creation_snapshot *s=part==2?&r.before:&r.after;
        added=snprintf(tail,remaining,"thread=%u ui=%u main=%u tls=%016llx view=%u time=%016llx rng_pointer=%016llx rng=%016llx read=%u write=%u reads=%x stable=%u object=%016llx runtime=%016llx controller=%016llx controller_type=%016llx controller_owner=%016llx slots=%016llx controller_agent=%016llx agent=%016llx agent_owner=%016llx timer=%016llx c0=%016llx c8=%08llx d0=%016llx direct_reads=%x identity_recorded=%u identity_stable=%u",
            s->thread,s->ui,s->main,s->tls,s->view,s->time,s->rng_pointer,s->rng,s->read,s->write,s->reads,s->stable,s->object,s->runtime,
            s->controller,s->controller_type,s->controller_owner,s->slots,s->controller_agent,s->agent,s->agent_owner,s->timer,s->cache_time,s->cache_index,s->other_time,s->direct_reads,s->identity.recorded,s->identity.stable);
    } else if(part>=4 && part<=7) {
        const creation_snapshot *s=part<6?&r.before:&r.after;
        const registry_presence_sample *a=(part&1)?&s->identity.second:&s->identity.first;
        added=snprintf(tail,remaining,"object=%016llx type=%llx actual=%016llx object_state=%u owner_getter=%llx owner=%016llx owner_state=%u controller=%016llx controller_type=%llx controller_owner=%016llx controller_state=%u slots=%016llx agent=%016llx agent_owner=%016llx agent_state=%u reads=%x raw_type=%016llx raw_owner_getter=%016llx raw_controller_type=%016llx",
            a->object,a->type,a->actual,a->object_state,a->owner_getter,a->owner,a->owner_state,a->controller,a->controller_type,a->controller_owner,a->controller_state,a->slots,a->agent,a->agent_owner,a->agent_state,a->reads,a->raw_type,a->raw_owner_getter,a->raw_controller_type);
    } else if(part>=8 && part<=15) {
        const creation_snapshot *s=part<12?&r.before:&r.after;unsigned piece=(part-8)%4;
        const unsigned char *raw=piece<2?s->navigation[piece]:s->parameters[piece-2];
        const char *hex="0123456789abcdef";if(remaining<837)return 0;memcpy(tail,"raw=",4);
        for(unsigned i=0;i<416;i++){tail[4+2*i]=hex[raw[i]>>4];tail[5+2*i]=hex[raw[i]&15];}tail[836]=0;added=836;
    } else if(part==16)added=snprintf(tail,remaining,"verified=%llx source=%u pc=%llx context_pc=%llx entity=%016llx depth=%u reads=%x script_hash=%016llx script=%s",
        r.range_verified,r.range_source,r.range_pc,r.range_context_pc,r.range_entity,r.range_depth,r.range_reads,r.range_script_hash,r.range_script[0]?r.range_script:"none");
    else added=snprintf(tail,remaining,"stage=%u subject=%u proposed_type=%llx published_before=%016llx published_after=%016llx rng_before=%016llx rng_after=%016llx reads=%x",
        r.stage,r.subject,r.proposed_type,r.published_before,r.published_after,r.rng_before,r.rng_after,r.aux_reads);
    return added>0 && (size_t)added<remaining?n+added:0;
}
