/* One explicit, guarded patrol Position RNG scope. Existing engine originals
 * compute every output. Raw Position observations are never rewritten. */
#define WPR_CAP 8u
#define WPR_READS 256u
#define WPR_BYTES 16384u
#define WPR_SEED 0x17bafa51eebe82a5ULL
enum {WPR_INPUT=1,WPR_IDENTITY=2,WPR_DEPENDENCY=4,WPR_TLS=8,WPR_DUPLICATE=16,
      WPR_NESTED=32,WPR_STOP=64,WPR_POST=128,WPR_RESTORE=256,WPR_UNWIND=512,
      WPR_PINS=1024,WPR_READ=2048,WPR_OVERFLOW=4096};
typedef struct {
    volatile LONG complete;
    unsigned errors,thread,position,action,occurrence,samples,prewrite_refused,write_attempted,applied;
    unsigned restore_required,restored,debt,abnormal,normal,reads,bytes,composition,find_event;
    uint64_t caller_before,seed,scoped_before,sampler_after,scoped_after,caller_after,tls,address;
    uint64_t sampler_slot,image,raw_outer_before,raw_sampler_before,frame,descriptor,anchor_wait,find_candidate;
    world_find_identity identity[2];unsigned char maximum[2][16],scratch[2][16];
    unsigned math[2][2];unsigned char output[16],digest[32];
} wpr_event;
static SRWLOCK wpr_lock=SRWLOCK_INIT;
static unsigned wpr_started,wpr_enabled,wpr_closed,wpr_composition,wpr_reserved,wpr_exported,wpr_used;
static LONG64 wpr_calls,wpr_open,wpr_peak,wpr_restore_open,wpr_failures,wpr_dropped,wpr_debt;
static wpr_event wpr_events[WPR_CAP];
static char wpr_manifest[256];static unsigned char wpr_digest[32];static uint64_t wpr_seed;
static _Thread_local wp_scope *wpr_current;
static int wpr_pins(void);
#ifndef WPR_RUNTIME_PINS
#define WPR_RUNTIME_PINS wpr_pins
#endif
static int wpr_bound(void);
static void wpr_begin(wp_scope*);
static void wpr_sample(wp_scope*,wp_sample*,unsigned);
static void wpr_finish(wp_scope*,unsigned);

static int wpr_key(unsigned char digest[32],uint64_t *seed) {
    static const char domain[]="X4LAN/WORLD_CREATE_POSITION/RNG/v1",name[]="order.fight.patrol",time[]="6.31667";
    static const unsigned char snapshot[]={0x78,0x16,0x35,0x10,0x56,0xe6,0x04,0x0d,0x92,0xae,0x9a,0x46,0x19,0xae,0x2e,0x15,
        0xa5,0x77,0xfa,0x26,0xa1,0x91,0x34,0xf1,0x1e,0x20,0x74,0x63,0x17,0xe6,0x1a,0xa8};
    unsigned char bytes[260],*p=bytes;
    memcpy(p,domain,sizeof(domain));p+=sizeof(domain);memcpy(p,snapshot,32);p+=32;
    world_find_le(&p,0x9d3a4,8);world_find_le(&p,0x3cdb,8);world_find_le(&p,38361,8);
    world_find_le(&p,sizeof(name)-1,2);memcpy(p,name,sizeof(name)-1);p+=sizeof(name)-1;
    world_find_le(&p,10,4);world_find_le(&p,215,8);world_find_le(&p,216,8);world_find_le(&p,2,4);
    world_find_le(&p,0,8);world_find_le(&p,1,8);world_find_le(&p,sizeof(time)-1,2);memcpy(p,time,sizeof(time)-1);p+=sizeof(time)-1;
    world_find_le(&p,121,8);world_find_le(&p,122,8);
    for(unsigned i=0;i<4;i++)world_find_le(&p,0,4);world_find_le(&p,2,4);memset(p,0,16);p+=16;
    world_find_le(&p,0,4);world_find_le(&p,0,8);world_find_le(&p,5,4);world_find_le(&p,0x47435000,8);
    world_find_le(&p,0x47435000,4);world_find_le(&p,1,4);world_find_le(&p,1,4);
    world_find_le(&p,0xffc0,4);world_find_le(&p,0x1f80,4);world_find_le(&p,0x27f,4);
    if(p-bytes!=260||!world_find_sha(bytes,260,digest))return 0;
    *seed=0;for(unsigned i=0;i<8;i++)*seed|=(uint64_t)digest[i]<<(8*i);return *seed==WPR_SEED;
}
static int wpr_stopped(void) {return !stats.active||InterlockedCompareExchange((volatile LONG*)&wpr_closed,0,0);}
static wpr_event *wpr_of(wp_scope *s) {return s?(wpr_event*)s->rng_event:NULL;}
static int wpr_read(const void *address,void *out,size_t size) {
    wp_scope *s=wpr_current;wpr_event *e=wpr_of(s);if(!e)return 0;
    if(wpr_stopped()||!address||!size||size-1>UINTPTR_MAX-(uintptr_t)address||e->reads>=WPR_READS||size>WPR_BYTES-e->bytes){e->errors|=WPR_READ;return 0;}
    ++e->reads;e->bytes+=(unsigned)size;if(!s->reader(address,out,size)){e->errors|=WPR_READ;return 0;}return 1;
}
static int wpr_selected(wp_scope *s) {
    world_action_event *p=s?s->parent:NULL;
    return p&&p->before.saved==WORLD_FIND_SAVED&&!strcmp(p->before.name,"order.fight.patrol")&&p->before.index==215;
}
static int wpr_conflict(void) {wpr_event *e=wpr_of(wpr_current);if(!e)return 0;e->errors|=WPR_NESTED;return 1;}
/* Never nest this lock with wpr_lock or wp_lock. The copied anchor and receipt
 * are immutable after complete; later checks also reject changed identities. */
static int wpr_dependency(wpr_event *e) {
    int ok;AcquireSRWLockShared(&world_find_lock);
    ok=world_find_started&&world_find_enabled&&!world_find_closed&&world_find_composition==wpr_composition&&
       world_find_dependencies_for(wpr_composition)&&!world_find_open&&!world_find_restore_open&&!world_find_failures&&!world_find_dropped&&
       world_find_anchor_ready&&!world_find_anchor_pending&&!world_find_anchor_errors&&world_find_reserved==1&&world_find_used==1&&
       world_find_events[0].complete&&!world_find_events[0].errors&&world_find_events[0].applied&&world_find_events[0].restored&&
       !world_find_events[0].abnormal&&world_find_events[0].assigned==0x29&&world_find_anchor_event&&
       world_find_anchor_event->complete&&!world_find_anchor_event->errors&&world_find_shape==WORLD_FIND_SHAPE&&
       world_wait_anchor_observer==world_find_wait_anchor&&world_wait_rng_foreign_active==world_find_conflict&&world_values_candidate_call==world_find_call;
    if(ok&&e){e->identity[0]=world_find_anchor;e->find_event=1;e->find_candidate=world_find_events[0].candidate;
        e->anchor_wait=(uint64_t)(world_find_anchor_event-world_wait_events)+1;}
    ReleaseSRWLockShared(&world_find_lock);return ok;
}
static int wpr_input(wp_scope *s,wp_sample *p,unsigned mode) {
    unsigned after=mode!=0,numeric_after=mode==1;
    wpr_event *e=wpr_of(s);wp_event *w=s?s->event:NULL;world_action_event *a=s?s->parent:NULL;
    static const wp_branch branch={{0,0,0,0,2}};static const unsigned char zero[16]={0};
    if(!e||!w||!a||wpr_current!=s||s->depth!=1||s->rng_previous||w->errors||a->errors||a->complete||world_action_current!=a||
       a->parent||a->depth!=1||a->stage!=2||a->thread!=e->thread||a->before.version!=10||a->before.program_hash!=WORLD_FIND_SHAPE||
       (a->before.end-a->before.begin)/8!=234||a->before.chance||a->before.vtable!=(uintptr_t)(image+0x2c0cdd0)||
       a->before.action!=(uintptr_t)(image+0xbe0ba0)||!wpr_selected(s)||
       memcmp(&w->branch[0],&branch,sizeof(branch))||memcmp(&w->radius_branch[0],&branch,sizeof(branch))||
       w->calls[0]!=1||w->calls[1]||w->calls[2]!=1||w->calls[3]!=1||w->samples!=1||
       w->arguments[3]||w->radius_optional||memcmp(w->base[0],zero,16)||!p||p!=&w->sample[0]||p->source!=1||
       p->span!=0x47435000||p->distribution!=1||p->parameter!=1||p->rng!=(uintptr_t)s->tls+0x318||
       (wp_control()&0xffc0)!=0x1f80||wp_x87()!=0x27f)return 0;
    if(p->slot>UINTPTR_MAX-0x188||(p->slot&15)!=8||p->slot<p->stack_limit||p->slot+0x188>p->stack_base)return 0;
    if(!wpr_read((void*)(p->slot+0x78),e->maximum[after],16)||!wpr_read((void*)(p->slot+0x178),e->scratch[after],16)||
       !wpr_read(image+0x38edec0,&e->math[after][0],4)||!wpr_read(image+0x2f3f220,&e->math[after][1],4))return 0;
    const unsigned char *maximum=e->maximum[after],*scratch=e->scratch[after],*minimum=p->minimum[numeric_after],*difference=p->difference[numeric_after];
    if(wp_d(maximum)!=5||wp_d(maximum+8)!=0x47435000||wp_d(scratch)||wp_q(scratch+8)||
       wp_d(minimum)||wp_q(minimum+8)||wp_d(difference)!=5||wp_d(difference+8)!=0x47435000||
       e->math[after][0]>1||e->math[after][1]>6)return 0;
    if(after&&(memcmp(e->maximum[0],maximum,16)||memcmp(e->scratch[0],scratch,16)||memcmp(e->math[0],e->math[1],sizeof(e->math[0]))))return 0;
    world_find_identity identity;if(!world_find_identity_stable(wpr_read,a->before.context,a->before.exec,&identity)||memcmp(&identity,&e->identity[0],sizeof(identity)))return 0;
    if(after)e->identity[1]=identity;
    return !wpr_stopped()&&s->tls_reader&&s->tls_reader()==s->tls&&GetCurrentThreadId()==e->thread&&
        boot_phase==2&&!stats.issued&&wpr_bound()&&wp_rng_begin==wpr_begin&&wp_rng_sample==wpr_sample&&wp_rng_finish==wpr_finish&&world_find_extra_conflict==wpr_conflict&&!world_find_current&&
        !(world_wait_rng_tls!=TLS_OUT_OF_INDEXES&&TlsGetValue(world_wait_rng_tls));
}
static void wpr_begin(wp_scope *s) {
    s->rng_entered=1;s->rng_previous=wpr_current;if(wpr_current)wpr_conflict();wpr_current=s;
    AcquireSRWLockExclusive(&wpr_lock);
    s->rng_admitted=wpr_enabled&&!wpr_stopped();
    if(s->rng_admitted){++wpr_calls;if(++wpr_open>wpr_peak)wpr_peak=wpr_open;}
    if(s->rng_admitted&&wpr_selected(s)){
        unsigned n=wpr_reserved++;
        if(n>=WPR_CAP){++wpr_dropped;++wpr_failures;}
        else{wpr_event *e=&wpr_events[n];s->rng_event=e;e->thread=GetCurrentThreadId();e->occurrence=++wpr_used;
            e->position=s->event?(unsigned)(s->event-wp_events)+1:0;e->action=s->event?s->event->parent_action:0;
            e->composition=wpr_composition;e->seed=wpr_seed;memcpy(e->digest,wpr_digest,32);e->image=(uintptr_t)image;
            e->tls=(uintptr_t)s->tls;e->address=e->tls?e->tls+0x318:0;
            if(s->event){e->raw_outer_before=s->event->outer[0].q[18];e->frame=s->event->arguments[2];e->descriptor=s->event->arguments[0];}
            if(e->occurrence!=1)e->errors|=WPR_DUPLICATE;if(s->rng_previous)e->errors|=WPR_NESTED;
        }
    }ReleaseSRWLockExclusive(&wpr_lock);
    wpr_event *e=wpr_of(s);if(e&&!wpr_dependency(e))e->errors|=WPR_DEPENDENCY;
}
/* Both store and readback are guarded. Caller marks the attempt and restoration
 * debt FIRST. A partial or uncertain write therefore cannot escape finally. */
static int wpr_native_store(uint64_t *address,uint64_t value) {
    int ok=0;__try {*(volatile uint64_t*)address=value;ok=*(volatile uint64_t*)address==value;}__except(EXCEPTION_EXECUTE_HANDLER){ok=0;}return ok;
}
#ifndef WPR_NATIVE_STORE
#define WPR_NATIVE_STORE wpr_native_store
#endif
static int wpr_store(wp_scope *s,uint64_t value) {
    wpr_event *e=wpr_of(s);int ok=0;
    __try {if(e&&GetCurrentThreadId()==e->thread&&s->tls_reader&&s->tls_reader()==s->tls&&
       e->tls==(uintptr_t)s->tls&&e->address==e->tls+0x318&&!(e->address&7)&&world_wait_rng_writable((uint64_t*)(uintptr_t)e->address))
        ok=WPR_NATIVE_STORE((uint64_t*)(uintptr_t)e->address,value);
    }__except(EXCEPTION_EXECUTE_HANDLER){ok=0;}return ok;
}
static int wpr_cleanup_read(wp_scope *s,uint64_t *value){
    wpr_event *e=wpr_of(s);int ok=0;
    __try{if(e&&GetCurrentThreadId()==e->thread&&s->tls_reader&&s->tls_reader()==s->tls&&e->tls==(uintptr_t)s->tls&&e->address==e->tls+0x318)
        ok=read_local((void*)(uintptr_t)e->address,value,8);
    }__except(EXCEPTION_EXECUTE_HANDLER){ok=0;}return ok;
}
static void wpr_sample(wp_scope *s,wp_sample *p,unsigned after) {
    wpr_event *e=wpr_of(s);
    if(!e){if(wpr_current)wpr_conflict();return;}
    if(after){
        if(!e->write_attempted)return;
        if(!wpr_input(s,p,1)||!p->normal||p->abnormal||p->result!=0x46c881ce||
           p->guard[1].q[18]!=world_find_step(e->seed))e->errors|=WPR_POST;
        if(p)e->sampler_after=p->guard[1].q[18];return;
    }
    ++e->samples;if(e->samples!=1)e->errors|=WPR_DUPLICATE;
    if(!wpr_input(s,p,0)||!wpr_input(s,p,2))e->errors|=WPR_INPUT;
    if(!wpr_dependency(NULL))e->errors|=WPR_DEPENDENCY;
    if(!WPR_RUNTIME_PINS())e->errors|=WPR_PINS;
    if(p){e->sampler_slot=p->slot;e->raw_sampler_before=p->guard[0].q[18];}
    uint64_t before=0,again=0;
    if(!s->tls_reader||s->tls_reader()!=s->tls||!wpr_read((void*)(uintptr_t)e->address,&before,8)||
       !wpr_read((void*)(uintptr_t)e->address,&again,8)||before!=again||before!=e->raw_sampler_before||
       !world_wait_rng_writable((uint64_t*)(uintptr_t)e->address))e->errors|=WPR_TLS;
    AcquireSRWLockExclusive(&wpr_lock);
    if(wpr_stopped())e->errors|=WPR_STOP;
    if(e->errors){e->prewrite_refused=1;ReleaseSRWLockExclusive(&wpr_lock);return;}
    e->caller_before=before;e->write_attempted=e->restore_required=e->debt=1;++wpr_debt;
    if(wpr_store(s,e->seed)){e->applied=1;e->scoped_before=e->seed;}else e->errors|=WPR_TLS;
    ReleaseSRWLockExclusive(&wpr_lock);
}
static void wpr_finish(wp_scope *s,unsigned abnormal) {
    if(!s->rng_entered)return;wpr_event *e=wpr_of(s);wp_event *w=s->event;
    if(e){
        if(abnormal){e->abnormal=1;e->errors|=WPR_UNWIND;
            if(e->restore_required&&!wpr_cleanup_read(s,&e->scoped_after))e->errors|=WPR_TLS;
        }
        else if(e->write_attempted){
            e->normal=1;if(!w||w->errors||w->returns[0]!=1||w->calls[0]!=1||w->samples!=1||w->radius!=0x46c881ce||
                memcmp(w->branch,w->branch+1,sizeof(wp_branch))||memcmp(w->radius_branch,w->radius_branch+1,sizeof(wp_branch)))e->errors|=WPR_POST;
            if(w){e->scoped_after=w->outer[1].q[18];memcpy(e->output,w->output,16);}
            unsigned math[2]={0};world_find_identity identity;
            if(!wpr_read(image+0x38edec0,math,4)||!wpr_read(image+0x2f3f220,math+1,4)||memcmp(math,e->math[0],sizeof(math))||
               !s->parent||!world_find_identity_stable(wpr_read,s->parent->before.context,s->parent->before.exec,&identity)||
               memcmp(&identity,&e->identity[0],sizeof(identity))||!wpr_dependency(NULL)||
               boot_phase!=2||stats.issued||!wpr_bound()||wp_rng_begin!=wpr_begin||wp_rng_sample!=wpr_sample||wp_rng_finish!=wpr_finish||world_find_extra_conflict!=wpr_conflict||
               e->scoped_after!=world_find_step(world_find_step(world_find_step(e->seed))))e->errors|=WPR_POST;
            memcpy(e->math[1],math,sizeof(math));
        }
        if(e->restore_required){
            /* No native object or unwound frame reads in abnormal cleanup. */
            if(wpr_store(s,e->caller_before)){e->caller_after=e->caller_before;e->restored=1;e->debt=0;}
            else e->errors|=WPR_RESTORE;
        }else if(!e->prewrite_refused){e->prewrite_refused=1;e->errors|=WPR_INPUT;}
        if(wpr_current!=s)e->errors|=WPR_NESTED;
    }
    wpr_current=s->rng_previous;
    AcquireSRWLockExclusive(&wpr_lock);
    if(e){if(e->restore_required&&e->restored)--wpr_debt;if(wpr_stopped())e->errors|=WPR_STOP;
        if(e->errors)++wpr_failures;InterlockedExchange(&e->complete,1);}
    if(s->rng_admitted){if(!wpr_open)++wpr_failures;else --wpr_open;}
    ReleaseSRWLockExclusive(&wpr_lock);
}
static void world_position_rng_restore(void) {
    AcquireSRWLockExclusive(&wpr_lock);wpr_enabled=0;wpr_closed=1;if(wpr_open>wpr_restore_open)wpr_restore_open=wpr_open;
    ReleaseSRWLockExclusive(&wpr_lock);
}
