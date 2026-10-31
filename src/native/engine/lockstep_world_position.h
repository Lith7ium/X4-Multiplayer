#include <intrin.h>
/* Explicit read-only CreatePosition receipts. No evaluation, RNG write, native
 * reference acquisition or canonical identity is performed by this module. */
#define WP_OUTER 0xc6f7b0u
#define WP_SAMPLER 0x14ddd10u
#define WP_CAP 64u
#define WP_SAMPLES 4u
#define WP_READS 256u
#define WP_BYTES 8192u
enum {WP_SCOPE=1,WP_READ=2,WP_BUDGET=4,WP_STOP=8,WP_OWNER=16,WP_PHASE=32,
      WP_CHANGED=64,WP_TYPE=128,WP_COUNTS=256,WP_UNWIND=512,WP_PATCH=1024};
typedef uintptr_t (*wp_outer_fn)(void*,void*,void*,void*);
typedef void (*wp_transform_fn)(void*,void*,double,void*,unsigned char);
typedef float (*wp_radius_fn)(void*,void*,void*);
typedef float (*wp_sampler_fn)(void*,float,unsigned,unsigned);
/* Copied boundary q: current,context,lifetime,entity,runtime,script,program,
 * begin,end,pc,actual_node,vtable,execute,table,tls_time,game,real,saved,rng.
 * d: tag,version,flags,chance,app,loading,view,mxcsr,x87cw. */
typedef struct {uint64_t q[19];uint32_t d[9];} wp_guard;
/* Exact native branch inputs, not evaluated Values: kind, coordinate flag,
 * space expression tag, reference expression tag, radius range kind. */
typedef struct {uint32_t d[5];} wp_branch;
typedef struct {
    uintptr_t slot,rng,caller,parent_return,descriptor,frame,stack_limit,stack_base;
    unsigned span,distribution,parameter,result,source,normal,abnormal;
    unsigned char difference[2][16],minimum[2][16];wp_guard guard[2];
} wp_sample;
typedef struct {
    volatile LONG complete;unsigned errors,thread,parent_action,depth,children,foreign;
    unsigned calls[4],returns[4],abnormal,reads,bytes,samples;
    uint64_t enter,leave,parent,result;uintptr_t arguments[4],caller,image_base,tls_address;
    wp_guard outer[2],transform_guard[2],radius_guard[2];
    uintptr_t space,reference,transform,range,radius_frame,radius_optional;
    uint64_t time;unsigned fifth,radius;
    unsigned char transform_bytes[2][64],base[2][16],output[16];
    wui_object objects[2][2]; /* before/after, space/reference; null is explicit */
    unsigned object_present[2];wp_sample sample[WP_SAMPLES];
    wp_branch branch[2],radius_branch[2];
    /* Direct radius-wrapper T, TIB bounds, caller, native outer continuation
     * (diagnostic), forwarded resolver output pointer, before/after contents. */
    uint64_t radius_witness[8];
} wp_event;
#include "lockstep_world_position_passive.h"
typedef struct wp_scope {
    struct wp_scope *previous;wp_event *event;world_action_event *parent;
    npc_provenance_frame *provenance;
    void *tls;void *(*tls_reader)(void);world_values_reader reader;
    unsigned depth,admitted;
    const wp_passive_callbacks *passive_callbacks;
    void *passive_record,*passive_previous;
    /* Optional experiment storage. The read-only receipt remains unchanged. */
    struct wp_scope *rng_previous;void *rng_event;unsigned rng_admitted,rng_entered;
    void (*rng_sample_callback)(struct wp_scope*,wp_sample*,unsigned);
    void (*rng_finish_callback)(struct wp_scope*,unsigned);
} wp_scope;
static void (*wp_rng_begin)(wp_scope*);
static void (*wp_rng_sample)(wp_scope*,wp_sample*,unsigned);
static void (*wp_rng_finish)(wp_scope*,unsigned);
static SRWLOCK wp_lock=SRWLOCK_INIT;
static volatile LONG wp_started,wp_enabled,wp_closing;
static LONG64 wp_calls,wp_open,wp_peak,wp_restore_open,wp_failures,wp_dropped,wp_sequence;
static unsigned wp_reserved,wp_exported;
static wp_event wp_events[WP_CAP];
static _Thread_local wp_scope *wp_current;
static _Thread_local wp_scope *wp_radius_current;

static int wp_stopped(void) {return InterlockedCompareExchange(&wp_closing,0,0)||!stats.active;}
static int wp_read(wp_scope *s,uintptr_t address,void *out,size_t size) {
    wp_event *e=s?s->event:NULL;if(!e) return 0;
    if(wp_stopped()) {e->errors|=WP_STOP;return 0;}
    if(!address||!size||size-1>UINTPTR_MAX-address) {e->errors|=WP_READ;return 0;}
    if(e->reads>=WP_READS||size>WP_BYTES-e->bytes) {e->errors|=WP_BUDGET;return 0;}
    ++e->reads;e->bytes+=(unsigned)size;
    if(!s->reader((void*)address,out,size)) {e->errors|=WP_READ;return 0;}return 1;
}
static int wp_at(wp_scope *s,uintptr_t base,size_t offset,void *out,size_t size) {
    if(!base||offset>UINTPTR_MAX-base) {if(s&&s->event)s->event->errors|=WP_READ;return 0;}
    return wp_read(s,base+offset,out,size);
}
static int wp_u64(wp_scope *s,uintptr_t address,uint64_t *out) {return wp_read(s,address,out,8);}
static uint64_t wp_q(const void *p) {uint64_t x;memcpy(&x,p,8);return x;}
static unsigned wp_d(const void *p) {unsigned x;memcpy(&x,p,4);return x;}
static unsigned wp_fbits(float x) {unsigned bits;memcpy(&bits,&x,4);return bits;}
static int wp_finite64(uint64_t x) {return (x&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL;}
static unsigned wp_control(void) {unsigned x;__asm__ volatile("stmxcsr %0":"=m"(x));return x;}
static unsigned wp_x87(void) {unsigned short x;__asm__ volatile("fnstcw %0":"=m"(x));return x;}

static int wp_branch_read(wp_scope *s,wp_branch *out) {
    unsigned char head[5],tags[32],range[12];uintptr_t a=s->event->arguments[0];memset(out,0,sizeof(*out));
    if(!wp_read(s,a,head,sizeof(head))||!wp_at(s,a,0x48,tags,sizeof(tags))||!wp_at(s,a,0x68,range,sizeof(range)))return 0;
    out->d[0]=wp_d(head);out->d[1]=head[4];out->d[2]=wp_d(tags);out->d[3]=wp_d(tags+16);out->d[4]=wp_d(range+8);
    if(out->d[0]>3||out->d[1]>1){s->event->errors|=WP_TYPE;return 0;}return 1;
}
static int wp_branch_same(wp_scope *s,const wp_branch *a,const wp_branch *b) {
    if(memcmp(a,b,sizeof(*a))){s->event->errors|=WP_CHANGED;return 0;}return 1;
}
static int wp_radius_witness(wp_scope *s,uintptr_t t,uintptr_t resolver_output,unsigned after) {
    wp_event *e=s->event;uint64_t *q=e->radius_witness;NT_TIB *tib=(NT_TIB*)NtCurrentTeb();uint64_t caller=0,parent[2]={0};
    if((t&15)!=8||t<(uintptr_t)tib->StackLimit||t>UINTPTR_MAX-0x140||t+0x140>(uintptr_t)tib->StackBase||
       resolver_output!=t+0x138||!wp_u64(s,t,&caller)||caller!=(uintptr_t)(image+0xc6fb6f)||
       !wp_at(s,t,0x130,parent,sizeof(parent))||!parent[0]||parent[1]){e->errors|=WP_SCOPE;return 0;}
    if(!after){q[0]=t;q[1]=(uintptr_t)tib->StackLimit;q[2]=(uintptr_t)tib->StackBase;q[3]=caller;q[4]=parent[0];q[5]=resolver_output;q[6]=parent[1];}
    else if(q[0]!=t||q[1]!=(uintptr_t)tib->StackLimit||q[2]!=(uintptr_t)tib->StackBase||q[3]!=caller||q[4]!=parent[0]||q[5]!=resolver_output){e->errors|=WP_CHANGED;return 0;}
    else q[7]=parent[1];return 1;
}
static void wp_branch_counts(wp_scope *s) {
    wp_event *e=s->event;unsigned space=e->branch[0].d[2],range=e->branch[0].d[4];
    if(e->calls[0]!=1||e->returns[0]!=1||memcmp(e->calls,e->returns,sizeof(e->calls))||e->calls[1]>1)e->errors|=WP_COUNTS;
    if(!space&&e->calls[1])e->errors|=WP_COUNTS;
    if(space&&!e->calls[1]&&(!e->radius_witness[0]||range!=2))e->errors|=WP_TYPE;
    if(!range){if(e->calls[2]||e->calls[3]||e->samples)e->errors|=WP_COUNTS;}
    else if(range==2){if(e->calls[2]!=1||e->calls[3]!=1||e->samples!=1)e->errors|=WP_COUNTS;}
    else e->errors|=WP_TYPE; /* Exact/list/failed-range paths without one sampled range are unsupported. */
}

/* This is deliberately a bounded partial reread, NOT a whole-program hash.
 * The independent parser must also join the completed parent Action receipt. */
static int wp_guard_read(wp_scope *s,wp_guard *out) {
    memset(out,0,sizeof(*out));wp_event *e=s->event;world_action_event *parent=s->parent;
    if(!parent||world_action_current!=parent||parent->complete||parent->errors||
       !s->provenance||npc_provenance_current!=s->provenance||npc_provenance_overflow_depth||npc_provenance_denied_depth||
       !s->provenance->readable||s->provenance->scope_phase!=2||
       GetCurrentThreadId()!=e->thread||s->tls_reader()!=s->tls||!s->tls) {e->errors|=WP_SCOPE;return 0;}
    const world_action_guard *p=&parent->before;
    unsigned char exec[0x38],context[0x128],descriptor[0x4c],entity[16],node[0x14],tls[32],name[96]={0};
    uint64_t *q=out->q;uint32_t *d=out->d;
    if(!wp_u64(s,(uintptr_t)(image+NPC_PROVENANCE_CURRENT),q)||q[0]!=p->exec||
       !wp_at(s,p->exec,0x10,exec,sizeof(exec))) goto owner;
    d[0]=wp_d(exec+0x28);d[2]=(unsigned short)wp_d(exec+0x20);
    if(d[0]!=0x66||wp_q(exec+0x30)<0xa8) goto owner;
    q[1]=wp_q(exec+0x30)-0xa8;q[6]=wp_q(exec);q[9]=wp_q(exec+8);
    if(q[1]!=p->context||q[6]!=p->program||q[9]!=p->index+1||
       s->provenance->self.exec!=p->exec||s->provenance->self.tag!=d[0]||s->provenance->self.value!=wp_q(exec+0x30)||
       s->provenance->self.context!=p->context||s->provenance->self.id!=p->lifetime||
       !wp_read(s,(uintptr_t)q[1],context,sizeof(context))) goto owner;
    q[2]=wp_q(context+8);q[3]=wp_q(context+0x120);q[5]=wp_q(context+0xd8);
    if(wp_q(context)!=(uintptr_t)(image+0x2c6a270)||q[2]!=p->lifetime||q[3]!=p->entity||q[5]!=p->script||
       !wp_read(s,(uintptr_t)q[3],entity,sizeof(entity))||
       !wp_at(s,(uintptr_t)q[5],0x10,descriptor,sizeof(descriptor))) goto owner;
    q[4]=wp_q(entity+8);d[1]=wp_d(descriptor+0x48);
    uint64_t length=wp_q(descriptor+0x10),capacity=wp_q(descriptor+0x18);
    if(q[4]!=p->runtime||world_identity_lookup(q[4])!=p->saved||d[1]!=p->version||
       length!=strlen(p->name)||!length||length>95||capacity<length||capacity>4096) goto owner;
    uintptr_t text=capacity<16?(uintptr_t)q[5]+0x10:(uintptr_t)wp_q(descriptor);
    if(!wp_read(s,text,name,(size_t)length+1)||memcmp(name,p->name,(size_t)length+1)||
       !wp_read(s,(uintptr_t)q[6],q+7,16)||q[7]!=p->begin||q[8]!=p->end||q[8]<q[7]||
       ((q[8]-q[7])&7)||q[9]>(q[8]-q[7])/8||
       !wp_at(s,(uintptr_t)q[7],p->index*8,q+10,8)||q[10]!=p->node||
       !wp_read(s,p->node,node,sizeof(node))) goto owner;
    q[11]=wp_q(node);d[3]=wp_d(node+0x10);
    if(q[11]!=p->vtable||d[3]!=p->chance||!wp_at(s,(uintptr_t)q[11],0x18,q+12,8)||q[12]!=p->action) goto owner;
    if(!wp_at(s,(uintptr_t)s->tls,0x300,tls,sizeof(tls))||
       !wp_u64(s,(uintptr_t)(image+TABLE_RVA),q+13)||!q[13]||
       !wp_at(s,(uintptr_t)q[13],0x130,d+5,4)||!wp_read(s,(uintptr_t)(image+0x6adf9fc),d+4,4)||
       !wp_u64(s,(uintptr_t)(image+GAME_RVA),q+15)||!wp_u64(s,(uintptr_t)(image+REAL_RVA),q+16)||
       !wp_u64(s,(uintptr_t)(image+0x3d19790),q+17)) goto phase;
    q[14]=wp_q(tls);q[18]=wp_q(tls+24);d[6]=wp_d(tls+20);d[7]=wp_control();d[8]=wp_x87();
    ULONGLONG now=GetTickCount64();
    if(!boot_mode||boot_phase!=2||stats.issued||now<boot_started||now-boot_started>120000||
       !(boot_options&281474976710656ULL)||!npc_provenance_enabled||GetCurrentThreadId()!=ui_thread||GetCurrentThreadId()==main_thread||
       d[6]!=1||q[14]!=q[15]||q[15]!=q[17]||!wp_finite64(q[14])||!wp_finite64(q[16])||
       !((d[4]==0x20&&!d[5])||(d[4]==0x26&&d[5]==1))||d[2]||
       q[13]!=parent->phase_before.table||q[14]!=parent->phase_before.game||
       d[4]!=parent->phase_before.app_flags||d[5]!=parent->phase_before.vbi_loading) goto phase;
    return 1;
owner:e->errors|=WP_OWNER;return 0;
phase:e->errors|=WP_PHASE;return 0;
}
static int wp_guard_same(const wp_guard *a,const wp_guard *b) {
    /* RNG alone may advance. Native real time must stay equal in this bracket. */
    return !memcmp(a->q,b->q,18*8)&&!memcmp(a->d,b->d,sizeof(a->d));
}
static void wp_guard_finish(wp_scope *s,const wp_guard *before,wp_guard *after) {
    if(!wp_guard_read(s,after)||!wp_guard_same(before,after)) s->event->errors|=WP_CHANGED;
}
static int wp_object_read_api(void *context,uint64_t address,void *out,size_t size) {return wp_read(context,(uintptr_t)address,out,size);}
static int wp_object_stopped(void *unused) {(void)unused;return wp_stopped();}
static int wp_object_read(wp_scope *s,uintptr_t object,wui_object *out) {
    memset(out,0,sizeof(*out));if(!object) return 1;
    wui_result unused={0};const wui_api api={s,wp_object_read_api,world_unknown_identity_saved,wp_object_stopped,world_unknown_identity_stub};
    wui_reader r={&api,&unused,(uintptr_t)image};
    int ok=wui_object_read(&r,out,object,0)&&out->q[2];if(!ok)s->event->errors|=WP_TYPE;return ok;
}
static int wp_numeric(const unsigned char raw[16]) {
    unsigned tag=wp_d(raw);if(tag>9) return 0;
    if(tag>=4&&tag<=6) return (wp_d(raw+8)&0x7f800000u)!=0x7f800000u;
    if(tag>=7) return wp_finite64(wp_q(raw+8));return 1;
}
/* T is supplied by the wrapper, never guessed from this helper's own frame. */
static int wp_stack_read(wp_scope *s,wp_sample *p,unsigned after) {
    NT_TIB *tib=(NT_TIB*)NtCurrentTeb();uintptr_t t=p->slot;uint64_t ret=0,frame[3]={0};
    if(!after){p->stack_limit=(uintptr_t)tib->StackLimit;p->stack_base=(uintptr_t)tib->StackBase;}
    else if(p->stack_limit!=(uintptr_t)tib->StackLimit||p->stack_base!=(uintptr_t)tib->StackBase){s->event->errors|=WP_CHANGED;return 0;}
    if((t&15)!=8||t<(uintptr_t)tib->StackLimit||t>UINTPTR_MAX-0x158||t+0x158>(uintptr_t)tib->StackBase||
       !wp_u64(s,t,&ret)||ret!=(uintptr_t)(image+0xc59951)||
       !wp_at(s,t,0x140,frame,sizeof(frame))||frame[0]!=(uintptr_t)(image+0xc59b2f)||
       frame[1]!=s->event->arguments[0]+0x68||frame[2]!=s->event->arguments[2]||
       !wp_at(s,t,0x38,p->difference[after],16)||!wp_at(s,t,0x58,p->minimum[after],16)) {s->event->errors|=WP_SCOPE;return 0;}
    if(!after) {p->caller=ret;p->parent_return=frame[0];p->descriptor=frame[1];p->frame=frame[2];}
    if(!wp_numeric(p->difference[after])||!wp_numeric(p->minimum[after])) {s->event->errors|=WP_TYPE;return 0;}return 1;
}
static wp_scope *wp_inner(unsigned kind,uintptr_t caller) {
    wp_scope *s=wp_current;if(!s||!s->event) return NULL;
    if(wp_stopped()||world_action_current!=s->parent||GetCurrentThreadId()!=s->event->thread||
       caller!=(uintptr_t)(image+(kind==1?0xc6fb4d:0xc6fb6f))) {
        s->event->errors|=wp_stopped()?WP_STOP:WP_SCOPE;++s->event->foreign;return NULL;
    }return s;
}
static void wp_outer_finish(wp_scope *s,unsigned abnormal) {
    wp_event *e=s->event;
    if(e) {if(abnormal) {e->abnormal|=1;e->errors|=WP_UNWIND;}
        if(wp_current!=s)e->errors|=WP_SCOPE;
        e->leave=(uint64_t)InterlockedIncrement64(&wp_sequence);
    }
    wp_current=s->previous;
    AcquireSRWLockExclusive(&wp_lock);
    if(e) {if(wp_stopped())e->errors|=WP_STOP;if(e->errors)++wp_failures;InterlockedExchange(&e->complete,1);}
    if(s->admitted) {if(wp_open<=0)++wp_failures;else --wp_open;}
    ReleaseSRWLockExclusive(&wp_lock);
}
static uintptr_t wp_outer_observe_at(wp_outer_fn original,void *a,void *b,void *c,void *d,uintptr_t caller,
                                  world_values_reader reader,void *(*tls_reader)(void),uintptr_t entry_slot) {
    wp_scope scope={0};scope.previous=wp_current;scope.depth=scope.previous?scope.previous->depth+1:1;
    scope.passive_callbacks=wp_passive;
    scope.reader=reader;scope.tls_reader=tls_reader;scope.tls=tls_reader();scope.parent=world_action_current;scope.provenance=npc_provenance_current;
    scope.rng_sample_callback=wp_rng_sample;scope.rng_finish_callback=wp_rng_finish;
    AcquireSRWLockExclusive(&wp_lock);
    scope.admitted=wp_enabled&&!wp_stopped();
    if(scope.admitted) {++wp_calls;if(++wp_open>wp_peak)wp_peak=wp_open;}
    world_action_event *parent=scope.parent;
    if(scope.admitted&&parent&&parent->stage==2) {
        unsigned index=wp_reserved++;if(index>=WP_CAP) {++wp_dropped;++wp_failures;}
        else {scope.event=&wp_events[index];wp_event *e=scope.event;e->thread=GetCurrentThreadId();e->depth=scope.depth;
            e->parent_action=(unsigned)(parent-world_action_events)+1;e->caller=caller;e->image_base=(uintptr_t)image;e->tls_address=(uintptr_t)scope.tls;e->calls[0]=1;
            e->arguments[0]=(uintptr_t)a;e->arguments[1]=(uintptr_t)b;e->arguments[2]=(uintptr_t)c;e->arguments[3]=(uintptr_t)d;
            e->enter=(uint64_t)InterlockedIncrement64(&wp_sequence);
            if(scope.previous&&scope.previous->event) {e->parent=(unsigned)(scope.previous->event-wp_events)+1;++scope.previous->event->children;}
        }
    }
    ReleaseSRWLockExclusive(&wp_lock);wp_current=&scope;uintptr_t result=0;
    __try {
        wp_event *e=scope.event;
        if(e) {if(scope.depth>4||caller!=(uintptr_t)(image+0xbe0bd1)||a!=(void*)(parent->before.node+0x28)||
                  c!=(void*)parent->before.frame||d||!b||parent->before.vtable!=(uintptr_t)(image+0x2c0cdd0)||
                  parent->before.action!=(uintptr_t)(image+0xbe0ba0)) e->errors|=WP_SCOPE;
            if(!wp_guard_read(&scope,&e->outer[0])) e->errors|=WP_OWNER;
            wp_branch_read(&scope,&e->branch[0]);
        }
        if(wp_rng_begin)wp_rng_begin(&scope);
        if(scope.passive_callbacks)scope.passive_callbacks->begin(&scope,a,b,c,d,caller,entry_slot);
        result=original(a,b,c,d); /* exactly once, including denied scopes */
        if(e) {e->result=result;++e->returns[0];
            if(result!=(uintptr_t)b)e->errors|=WP_SCOPE;
            if(!wp_read(&scope,(uintptr_t)b,e->output,16))e->errors|=WP_READ;
            wp_guard_finish(&scope,&e->outer[0],&e->outer[1]);
            wp_branch_read(&scope,&e->branch[1]);wp_branch_same(&scope,&e->branch[0],&e->branch[1]);wp_branch_counts(&scope);
        }
        if(scope.passive_callbacks)scope.passive_callbacks->normal(&scope,result);
    } __finally {
        /* Cleanup must precede raw complete/open publication, even after a
         * failed post-read or propagated native exception. No wp_lock here. */
        if(scope.rng_finish_callback)scope.rng_finish_callback(&scope,AbnormalTermination());
        if(scope.passive_callbacks)scope.passive_callbacks->finish(&scope,AbnormalTermination());
        wp_outer_finish(&scope,AbnormalTermination());
    }
    return result;
}
/* Existing owned direct-receipt tests do not claim a native incoming T. */
static __attribute__((unused)) uintptr_t wp_outer_observe(wp_outer_fn original,void *a,void *b,void *c,void *d,uintptr_t caller,
                                  world_values_reader reader,void *(*tls_reader)(void)) {
    return wp_outer_observe_at(original,a,b,c,d,caller,reader,tls_reader,0);
}
static void wp_transform_observe(wp_transform_fn original,void *space,void *buffer,double time,void *reference,unsigned char fifth,uintptr_t caller) {
    wp_scope *s=wp_inner(1,caller),*saved=wp_current;wp_event *e=s?s->event:NULL;
    if(!s)wp_current=NULL;
    __try {
        if(e) {++e->calls[1];if(e->calls[1]!=1)e->errors|=WP_COUNTS;
            e->space=(uintptr_t)space;e->reference=(uintptr_t)reference;e->transform=(uintptr_t)buffer;e->fifth=fifth;memcpy(&e->time,&time,8);
            e->object_present[0]=space!=NULL;e->object_present[1]=reference!=NULL;
            if(!space||!buffer||fifth)e->errors|=WP_SCOPE;
            wp_guard_read(s,&e->transform_guard[0]);if(e->time!=e->transform_guard[0].q[14])e->errors|=WP_PHASE;
            wp_read(s,(uintptr_t)buffer,e->transform_bytes[0],64);wp_object_read(s,(uintptr_t)space,&e->objects[0][0]);wp_object_read(s,(uintptr_t)reference,&e->objects[0][1]);
        }
        original(space,buffer,time,reference,fifth);
        if(e) {++e->returns[1];wp_read(s,(uintptr_t)buffer,e->transform_bytes[1],64);
            wp_object_read(s,(uintptr_t)space,&e->objects[1][0]);wp_object_read(s,(uintptr_t)reference,&e->objects[1][1]);
            if(memcmp(e->objects[0],e->objects[1],sizeof(e->objects[0])))e->errors|=WP_CHANGED;
            wp_guard_finish(s,&e->transform_guard[0],&e->transform_guard[1]);}
    } __finally {if(e&&AbnormalTermination()){e->abnormal|=2;e->errors|=WP_UNWIND;}wp_current=saved;}
}
static float wp_radius_observe(wp_radius_fn original,void *range,void *frame,void *optional,uintptr_t caller,uintptr_t entry_slot,uintptr_t resolver_output) {
    wp_scope *passive=wp_current;
    wp_scope *s=wp_inner(2,caller),*saved=wp_radius_current;wp_event *e=s?s->event:NULL;wp_radius_current=s;float result=0;
    __try {
        if(e) {++e->calls[2];if(e->calls[2]!=1)e->errors|=WP_COUNTS;
            e->range=(uintptr_t)range;e->radius_frame=(uintptr_t)frame;e->radius_optional=(uintptr_t)optional;
            if((uintptr_t)range!=e->arguments[0]+0x68||(uintptr_t)frame!=e->arguments[2]||optional)e->errors|=WP_SCOPE;
            wp_guard_read(s,&e->radius_guard[0]);wp_read(s,e->arguments[1],e->base[0],16);
            wp_branch_read(s,&e->radius_branch[0]);wp_branch_same(s,&e->branch[0],&e->radius_branch[0]);
            if(e->branch[0].d[2]&&!e->calls[1])wp_radius_witness(s,entry_slot,resolver_output,0);
        }
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->radius_begin(passive,range,frame,optional,caller,entry_slot);
        result=original(range,frame,optional);
        if(e) {++e->returns[2];e->radius=wp_fbits(result);wp_read(s,e->arguments[1],e->base[1],16);
            if(memcmp(e->base[0],e->base[1],16))e->errors|=WP_CHANGED;
            wp_guard_finish(s,&e->radius_guard[0],&e->radius_guard[1]);
            wp_branch_read(s,&e->radius_branch[1]);wp_branch_same(s,&e->radius_branch[0],&e->radius_branch[1]);
            if(e->branch[0].d[2]&&!e->calls[1])wp_radius_witness(s,entry_slot,resolver_output,1);}
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->radius_normal(passive,result);
    } __finally {if(e&&AbnormalTermination()){e->abnormal|=4;e->errors|=WP_UNWIND;}
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->radius_finish(passive,AbnormalTermination());
        wp_radius_current=saved;}
    return result;
}
static float wp_sampler_observe(wp_sampler_fn original,void *rng,float span,unsigned distribution,unsigned parameter,uintptr_t entry_slot,uintptr_t caller) {
    wp_scope *passive=wp_current;
    wp_scope *s=wp_radius_current;wp_event *e=NULL;wp_sample *p=NULL;
    if(s&&s==wp_current&&s->event&&world_action_current==s->parent&&caller==(uintptr_t)(image+0xc59951)) {
        e=s->event;++e->calls[3];unsigned index=e->samples++;
        if(index<WP_SAMPLES) {p=&e->sample[index];p->slot=entry_slot;p->rng=(uintptr_t)rng;p->span=wp_fbits(span);p->distribution=distribution;p->parameter=parameter;}
        else e->errors|=WP_COUNTS;
    } else if(s&&s->event) {++s->event->foreign;s->event->errors|=WP_SCOPE;}
    float result=0;
    __try {
        if(p) {p->source=(uintptr_t)rng==(uintptr_t)s->tls+0x318?1:0;if(!p->source)e->errors|=WP_TYPE;
            wp_guard_read(s,&p->guard[0]);wp_stack_read(s,p,0);}
        if(s&&s->rng_sample_callback)s->rng_sample_callback(s,p,0);
        else if(wp_rng_sample)wp_rng_sample(s,p,0);
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->sample_begin(passive,rng,span,distribution,parameter,caller,entry_slot);
        result=original(rng,span,distribution,parameter);
        if(e)++e->returns[3];
        if(p) {p->result=wp_fbits(result);p->normal=1;wp_stack_read(s,p,1);
            if(memcmp(p->difference[0],p->difference[1],16)||memcmp(p->minimum[0],p->minimum[1],16))e->errors|=WP_CHANGED;
            wp_guard_finish(s,&p->guard[0],&p->guard[1]);}
        if(s&&s->rng_sample_callback)s->rng_sample_callback(s,p,1);
        else if(wp_rng_sample)wp_rng_sample(s,p,1);
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->sample_normal(passive,result);
    } __finally {if(e&&AbnormalTermination()){e->abnormal|=8;e->errors|=WP_UNWIND;if(p)p->abnormal=1;}
        if(passive&&passive->passive_callbacks)passive->passive_callbacks->sample_finish(passive,AbnormalTermination());}
    return result;
}
#ifndef WP_NATIVE_READER
#define WP_NATIVE_READER read_local
#endif
#ifndef WP_NATIVE_TLS_READER
#define WP_NATIVE_TLS_READER world_queue_tls
#endif
static __declspec(noinline) uintptr_t world_position_outer(void *a,void *b,void *c,void *d) {
    uintptr_t t=(uintptr_t)_AddressOfReturnAddress();
    return wp_outer_observe_at((wp_outer_fn)(thunk+3776),a,b,c,d,(uintptr_t)__builtin_return_address(0),WP_NATIVE_READER,WP_NATIVE_TLS_READER,t);
}
static __declspec(noinline) void world_position_transform(void *a,void *b,double time,void *d,unsigned char fifth) {
    wp_transform_observe((wp_transform_fn)(image+0x3db8a0),a,b,time,d,fifth,(uintptr_t)__builtin_return_address(0));
}
static __declspec(noinline) float world_position_radius(void *a,void *b,void *c,uintptr_t resolver_output) {
    uintptr_t t=(uintptr_t)_AddressOfReturnAddress();
    return wp_radius_observe((wp_radius_fn)(image+0xc59b00),a,b,c,(uintptr_t)__builtin_return_address(0),t,resolver_output);
}
static __declspec(noinline) float world_position_sampler(void *rng,float span,unsigned distribution,unsigned parameter) {
    uintptr_t t=(uintptr_t)_AddressOfReturnAddress();
    return wp_sampler_observe((wp_sampler_fn)(thunk+3904),rng,span,distribution,parameter,t,(uintptr_t)__builtin_return_address(0));
}
