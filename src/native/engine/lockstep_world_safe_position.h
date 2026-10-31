/* Passive GetSafePos candidate receipt. No getter, evaluator or RNG write. */
#define GPS_CAP 4u
typedef struct {
    wp_event e;
    wp_scope shadow;
    uint64_t witness[2][7]; /* T,limit,base,return,parent return,saved frame,arg5 */
    wui_object reference[2];
    uintptr_t entry;
    unsigned radius_open,sample_open;
} gps_record;
static gps_record gps_records[GPS_CAP];
static SRWLOCK gps_lock=SRWLOCK_INIT;
static LONG gps_started,gps_enabled,gps_closing;
static LONG64 gps_calls,gps_open,gps_peak,gps_restore_open,gps_failures,gps_dropped,gps_sequence;
static unsigned gps_reserved,gps_exported;
static unsigned char gps_binding[sizeof(wp_binding)];
static _Thread_local gps_record *gps_current;
static const wp_passive_callbacks gps_callbacks;

static int gps_stopped(void){return InterlockedCompareExchange(&gps_closing,0,0)||wp_stopped();}
static int gps_selected(const world_action_event *p){
    return p&&p->stage==1&&p->before.saved==0x80d54&&p->before.version==4&&
        p->before.index==51&&!strcmp(p->before.name,"move.idle");
}
static gps_record *gps_record_for(wp_scope *s){
    gps_record *r=s?(gps_record*)s->passive_record:NULL;
    if(!r)return NULL;
    if(gps_current!=r||world_action_current!=r->shadow.parent||GetCurrentThreadId()!=r->e.thread){r->e.errors|=WP_SCOPE;return NULL;}
    if(gps_stopped()){r->e.errors|=WP_STOP;return NULL;}
    return r;
}
static int gps_frame(gps_record *r,unsigned after){
    wp_scope *s=&r->shadow;uint64_t *q=r->witness[after];NT_TIB *tib=(NT_TIB*)NtCurrentTeb();uintptr_t t=r->entry;
    q[0]=t;q[1]=(uintptr_t)tib->StackLimit;q[2]=(uintptr_t)tib->StackBase;
    if((t&15)!=8||t<q[1]||t>UINTPTR_MAX-0x290||t+0x290>q[2]||r->e.arguments[1]!=t+0xc8||
       !wp_u64(s,t,q+3)||!wp_at(s,t,0x260,q+4,8)||!wp_at(s,t,0x278,q+5,8)||!wp_at(s,t,0x288,q+6,8)||
       q[3]!=(uintptr_t)(image+0xc72905)||q[4]!=(uintptr_t)(image+0xbe2382)||q[5]!=r->e.arguments[2]||
       q[6]!=r->e.arguments[3]||!q[6]){r->e.errors|=WP_SCOPE;return 0;}
    if(after&&memcmp(r->witness[0],r->witness[1],sizeof(r->witness[0]))){r->e.errors|=WP_CHANGED;return 0;}return 1;
}
static void gps_reference(gps_record *r,unsigned after){
    if(!wp_object_read(&r->shadow,r->e.arguments[3],&r->reference[after]))r->e.errors|=WP_TYPE;
    if(after&&memcmp(r->reference,r->reference+1,sizeof(r->reference[0])))r->e.errors|=WP_CHANGED;
}
static void gps_begin(wp_scope *scope,void*a,void*b,void*c,void*d,uintptr_t caller,uintptr_t t){
    scope->passive_previous=gps_current;scope->passive_record=NULL;gps_current=NULL;
    if(scope->passive_previous){gps_record *old=scope->passive_previous;++old->e.foreign;old->e.errors|=WP_SCOPE;}
    AcquireSRWLockExclusive(&gps_lock);
    int on=gps_enabled&&!gps_stopped();if(on)++gps_calls;
    if(on&&gps_selected(scope->parent)){
        unsigned index=gps_reserved++;
        if(index>=GPS_CAP){++gps_dropped;++gps_failures;}
        else {gps_record *r=&gps_records[index];scope->passive_record=r;gps_current=r;r->shadow=*scope;r->shadow.event=&r->e;
            r->entry=t;r->e.thread=GetCurrentThreadId();r->e.depth=scope->depth;r->e.parent_action=(unsigned)(scope->parent-world_action_events)+1;
            r->e.calls[0]=1;r->e.caller=caller;r->e.image_base=(uintptr_t)image;r->e.tls_address=(uintptr_t)scope->tls;
            r->e.arguments[0]=(uintptr_t)a;r->e.arguments[1]=(uintptr_t)b;r->e.arguments[2]=(uintptr_t)c;r->e.arguments[3]=(uintptr_t)d;
            r->e.enter=(uint64_t)InterlockedIncrement64(&gps_sequence);if(++gps_open>gps_peak)gps_peak=gps_open;
            if(index)r->e.errors|=WP_COUNTS;if(scope->depth!=1)r->e.errors|=WP_SCOPE;
        }
    }
    ReleaseSRWLockExclusive(&gps_lock);
    gps_record *r=gps_record_for(scope);if(!r)return;
    world_action_guard *p=&scope->parent->before;
    if(caller!=(uintptr_t)(image+0xc72905)||a!=(void*)(p->node+0x28)||c!=(void*)p->frame||!b||!d||
       p->vtable!=(uintptr_t)(image+0x2c0cc88)||p->action!=(uintptr_t)(image+0xbe2320))r->e.errors|=WP_SCOPE;
    wp_guard_read(&r->shadow,&r->e.outer[0]);gps_frame(r,0);gps_reference(r,0);wp_branch_read(&r->shadow,&r->e.branch[0]);
}
static void gps_normal(wp_scope *scope,uintptr_t result){
    gps_record *r=scope?scope->passive_record:NULL;if(!r)return;
    ++r->e.returns[0];r->e.result=result;
    if(!gps_record_for(scope))return;
    if(result!=r->e.arguments[1])r->e.errors|=WP_SCOPE;
    wp_read(&r->shadow,r->e.arguments[1],r->e.output,16);gps_frame(r,1);gps_reference(r,1);
    wp_guard_finish(&r->shadow,&r->e.outer[0],&r->e.outer[1]);
    wp_branch_read(&r->shadow,&r->e.branch[1]);wp_branch_same(&r->shadow,&r->e.branch[0],&r->e.branch[1]);
    if(r->e.calls[0]!=1||r->e.calls[1]||r->e.calls[2]!=1||r->e.calls[3]!=1||r->e.samples!=1||
       memcmp(r->e.calls,r->e.returns,sizeof(r->e.calls))||r->e.branch[0].d[4]!=2)r->e.errors|=WP_COUNTS;
}
static void gps_finish(wp_scope *scope,unsigned abnormal){
    gps_record *r=scope?scope->passive_record:NULL;
    if(r){if(abnormal){r->e.errors|=WP_UNWIND;r->e.abnormal|=1;}if(gps_current!=r||r->radius_open||r->sample_open)r->e.errors|=WP_SCOPE;
        r->e.leave=(uint64_t)InterlockedIncrement64(&gps_sequence);
        AcquireSRWLockExclusive(&gps_lock);if(gps_stopped())r->e.errors|=WP_STOP;if(r->e.errors)++gps_failures;
        if(gps_open<=0)++gps_failures;else --gps_open;InterlockedExchange(&r->e.complete,1);ReleaseSRWLockExclusive(&gps_lock);}
    if(scope)gps_current=scope->passive_previous;
}
static void gps_radius_begin(wp_scope *scope,void*range,void*frame,void*optional,uintptr_t caller,uintptr_t t){
    (void)t;gps_record *r=gps_record_for(scope);if(!r)return;++r->e.calls[2];++r->radius_open;
    if(r->e.calls[2]!=1||caller!=(uintptr_t)(image+0xc6fb6f)||(uintptr_t)range!=r->e.arguments[0]+0x68||
       (uintptr_t)frame!=r->e.arguments[2]||optional)r->e.errors|=WP_SCOPE;
    r->e.range=(uintptr_t)range;r->e.radius_frame=(uintptr_t)frame;r->e.radius_optional=(uintptr_t)optional;
    wp_guard_read(&r->shadow,&r->e.radius_guard[0]);wp_read(&r->shadow,r->e.arguments[1],r->e.base[0],16);
    wp_branch_read(&r->shadow,&r->e.radius_branch[0]);wp_branch_same(&r->shadow,&r->e.branch[0],&r->e.radius_branch[0]);
}
static void gps_radius_normal(wp_scope *scope,float value){
    gps_record *r=scope?scope->passive_record:NULL;if(!r||!r->radius_open)return;++r->e.returns[2];r->e.radius=wp_fbits(value);
    if(!gps_record_for(scope))return;
    wp_read(&r->shadow,r->e.arguments[1],r->e.base[1],16);if(memcmp(r->e.base[0],r->e.base[1],16))r->e.errors|=WP_CHANGED;
    wp_guard_finish(&r->shadow,&r->e.radius_guard[0],&r->e.radius_guard[1]);wp_branch_read(&r->shadow,&r->e.radius_branch[1]);
    wp_branch_same(&r->shadow,&r->e.branch[0],&r->e.radius_branch[1]);
}
static void gps_radius_finish(wp_scope *scope,unsigned abnormal){
    gps_record *r=scope?scope->passive_record:NULL;if(!r)return;if(abnormal){r->e.errors|=WP_UNWIND;r->e.abnormal|=4;}
    if(r->radius_open)--r->radius_open;
}
/* Both frames belong to the same admitted thread/stack interval. No stack
 * switch or interval growth is supported between these copied boundaries. */
static void gps_sample_stack_join(gps_record *r,const wp_sample *s){
    if(s->stack_limit!=r->witness[0][1]||s->stack_base!=r->witness[0][2])r->e.errors|=WP_CHANGED;
}
static void gps_sample_begin(wp_scope *scope,void*rng,float span,unsigned distribution,unsigned parameter,uintptr_t caller,uintptr_t t){
    gps_record *r=gps_record_for(scope);if(!r)return;++r->e.calls[3];unsigned index=r->e.samples++;++r->sample_open;
    if(index||!r->radius_open||caller!=(uintptr_t)(image+0xc59951)){r->e.errors|=WP_SCOPE;return;}
    wp_sample *s=&r->e.sample[0];s->slot=t;s->rng=(uintptr_t)rng;s->span=wp_fbits(span);s->distribution=distribution;s->parameter=parameter;
    s->source=(uintptr_t)rng==(uintptr_t)r->shadow.tls+0x318?1:0;if(!s->source)r->e.errors|=WP_TYPE;
    wp_guard_read(&r->shadow,&s->guard[0]);wp_stack_read(&r->shadow,s,0);gps_sample_stack_join(r,s);
}
static void gps_sample_normal(wp_scope *scope,float value){
    gps_record *r=scope?scope->passive_record:NULL;if(!r||!r->sample_open)return;++r->e.returns[3];
    if(r->e.samples!=1)return;wp_sample *s=&r->e.sample[0];s->result=wp_fbits(value);s->normal=1;
    if(!gps_record_for(scope))return;wp_stack_read(&r->shadow,s,1);gps_sample_stack_join(r,s);
    if(memcmp(s->minimum[0],s->minimum[1],16)||memcmp(s->difference[0],s->difference[1],16))r->e.errors|=WP_CHANGED;
    wp_guard_finish(&r->shadow,&s->guard[0],&s->guard[1]);
}
static void gps_sample_finish(wp_scope *scope,unsigned abnormal){
    gps_record *r=scope?scope->passive_record:NULL;if(!r)return;if(abnormal){r->e.errors|=WP_UNWIND;r->e.abnormal|=8;r->e.sample[0].abnormal=1;}
    if(r->sample_open)--r->sample_open;
}
static const wp_passive_callbacks gps_callbacks={gps_begin,gps_normal,gps_finish,gps_radius_begin,gps_radius_normal,gps_radius_finish,gps_sample_begin,gps_sample_normal,gps_sample_finish};
static void world_safe_position_stop(void){AcquireSRWLockExclusive(&gps_lock);gps_enabled=0;gps_closing=1;if(gps_open>gps_restore_open)gps_restore_open=gps_open;ReleaseSRWLockExclusive(&gps_lock);}
static int gps_pins(void){
    static const world_action_pin pins[]={
      {0xbe2320,226,"a1633b66afd1a2d3b9eebf5dedb92fb020a5e91f9b772921531f9184ad0f80ff"},
      {0xc72380,3552,"e5a9077b31bf996cbea45c65aa59f755062fb4062ea7deb301f4dfae0cbf1419"}};
    unsigned char data[3552];for(unsigned i=0;i<2;i++)if(!read_local(image+pins[i].rva,data,pins[i].size)||!hash_ok(data,pins[i].size,pins[i].sha))return 0;
    uintptr_t target=0;return read_local(image+0x2c0cc88+0x18,&target,8)&&target==(uintptr_t)(image+0xbe2320)&&wp_pins_ok();
}
static int gps_configure_using(int(*pins)(void)){
    if(GetCurrentThreadId()!=ui_thread)return 0;
    AcquireSRWLockExclusive(&wp_lock);AcquireSRWLockExclusive(&gps_lock);
    unsigned selected=0;for(unsigned i=0;i<world_action_selected_count;i++)if(world_action_selected[i].saved==0x80d54&&world_action_selected[i].version==4&&!strcmp(world_action_selected[i].name,"move.idle"))++selected;
    int ok=stats.active&&boot_mode&&!gps_stopped()&&selected==1&&wp_started&&wp_enabled&&wp_bound()&&wp_dependencies()&&
        !wp_failures&&!wp_dropped&&!wp_patch_failures&&wp_patch_mask(0)==15&&!wp_patch_mask(1)&&!wp_patch_mask(2);
    for(unsigned i=0;ok&&i<4;i++){wp_patch *p=&wp_patches[i];unsigned __int128 actual=0;ok=read_local(image+p->rva,&actual,p->size)&&actual==p->patched&&thunk_slot_ready(p->offset);}
    if(gps_started)ok=ok&&gps_enabled&&!gps_failures&&!gps_dropped&&wp_passive==&gps_callbacks&&!memcmp(gps_binding,&wp_binding,sizeof(gps_binding));
    else ok=ok&&!boot_phase&&!stats.issued&&!wp_passive;
    if(ok)ok=pins();
    if(ok&&!gps_started){memcpy(gps_binding,&wp_binding,sizeof(gps_binding));gps_started=1;gps_enabled=1;wp_passive=&gps_callbacks;}
    ReleaseSRWLockExclusive(&gps_lock);ReleaseSRWLockExclusive(&wp_lock);return ok;
}
static int world_safe_position_configure(void){return gps_configure_using(gps_pins);}
