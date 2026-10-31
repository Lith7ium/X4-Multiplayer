/* Selected early action metadata/RNG observer. No engine writes/getters, no
 * sampling correction. Evaluated coordinate Values and pre-dispatch chance
 * draws are explicitly outside this bracket. */
#define WORLD_ACTION_MAX 2048u
#define WORLD_ACTION_SELECTED 8u
#define WORLD_ACTION_MANIFEST_CAP 2048u
#define WORLD_ACTION_PROGRAM_MAX 4096u
enum {WA_READ=1,WA_OWNER=2,WA_VERSION=4,WA_NODE=8,WA_PHASE=16,WA_LIFETIME=32,WA_STOP=64,WA_DEPTH=128};
typedef struct {uint64_t saved;unsigned version;char name[96];} world_action_selection;
typedef struct {
    uintptr_t node,exec,frame,current,context,entity,script,program,begin,end,vtable,action;
    uint64_t lifetime,runtime,saved,index,program_hash;
    unsigned tag,version,chance;char name[96];
} world_action_guard;
typedef struct {
    volatile LONG complete;unsigned errors,selected,stage,thread,ui,boot,depth,children,unobserved_children;
    uint64_t tick,enter,leave,parent,rng_before,rng_after,pc_before,pc_after;
    unsigned flags_before,flags_after;
    world_action_guard before,after;world_queue_phase_sample phase_before,phase_after;
} world_action_event;
static world_action_selection world_action_selected[WORLD_ACTION_SELECTED];
static world_action_event world_action_events[WORLD_ACTION_MAX];
static char world_action_manifest[WORLD_ACTION_MANIFEST_CAP];
static unsigned world_action_selected_count,world_action_exported;
static volatile LONG world_action_started;
static LONG64 world_action_calls,world_action_reserved,world_action_sequence,world_action_failures,world_action_dropped;
static LONG64 world_action_other_owner,world_action_other_script,world_action_other_context,world_action_other_phase;
static LONG64 world_action_unknown,world_action_threads,world_action_open,world_action_peak,world_action_restore_open,world_action_unknown_rng;
static int world_action_enabled,world_action_closing;
static SRWLOCK world_action_lock=SRWLOCK_INIT;
static _Thread_local world_action_event *world_action_current;
static _Thread_local unsigned world_action_depth;

static int world_action_parse(const char *text,size_t length,world_action_selection out[WORLD_ACTION_SELECTED],unsigned *count) {
    if(!text||!length||length>=WORLD_ACTION_MANIFEST_CAP||memchr(text,0,length)) return 0;
    char copy[WORLD_ACTION_MANIFEST_CAP];memcpy(copy,text,length);copy[length]=0;const char *p=copy;unsigned n=0;
    memset(out,0,sizeof(*out)*WORLD_ACTION_SELECTED);
    while(*p) {
        if(n==WORLD_ACTION_SELECTED) return 0;world_action_selection *s=&out[n];uint64_t version;
        if(!world_wait_number(&p,',',16,&s->saved)||!s->saved||!world_wait_number(&p,',',10,&version)||version>INT32_MAX) return 0;
        s->version=(unsigned)version;unsigned size=0;
        while(*p&&*p!=';') {char c=*p++;if(size==95||!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.')) return 0;s->name[size++]=c;}
        if(!size||(*p==';'&&!*++p)) return 0;
        for(unsigned i=0;i<n;i++) if(out[i].saved==s->saved&&!strcmp(out[i].name,s->name)) return 0;
        ++n;
    }
    uint64_t owners[2]={0};unsigned owner_count=0;
    for(unsigned i=0;i<n;i++) {
        int found=0;for(unsigned j=0;j<owner_count;j++) if(out[i].saved==owners[j]) found=1;
        if(!found) {if(owner_count==2) return 0;owners[owner_count++]=out[i].saved;}
    }
    *count=n;return n>0;
}
/* Known other context returns2; unresolved must not silently become foreign. */
static void (*world_action_unknown_callback)(void*,void*,void*,unsigned,const world_action_guard*);
static int world_action_owner_ex(void *exec,world_action_guard *g,unsigned *reason) {
    npc_provenance_guard owner={0};uintptr_t type=0;unsigned why=0;int status=0;
    if(!npc_provenance_guard_read(exec,&owner)) {why=1;goto done;}
    g->tag=owner.tag;g->context=owner.context;g->lifetime=owner.id;
    if(owner.tag==0x5d) {status=2;goto done;}
    if(owner.tag!=0x66||!owner.context) {why=2;goto done;}
    if(!read_local((void*)owner.context,&type,8)||type!=(uintptr_t)(image+0x2c6a270)) {why=3;goto done;}
    if(!read_local((void*)(owner.context+0x120),&g->entity,8)||!g->entity) {why=4;goto done;}
    if(!read_local((void*)(g->entity+8),&g->runtime,8)||!g->runtime) {why=5;goto done;}
    g->saved=world_identity_lookup(g->runtime);if(!g->saved) {why=6;goto done;}status=1;
done:if(reason) *reason=why;return status;
}
static int world_action_owner(void *exec,world_action_guard *g) {
    return world_action_owner_ex(exec,g,NULL);
}
static int world_action_program(const world_action_guard *g,uint64_t *hash_out) {
    if(!g->begin||g->end<g->begin||((g->end-g->begin)&7)||(g->end-g->begin)/8>WORLD_ACTION_PROGRAM_MAX) return 0;
    uint64_t hash=14695981039346656037ULL;
    for(uintptr_t p=g->begin;p<g->end;p+=8) {
        uintptr_t node,type;if(!read_local((void*)p,&node,8)||!node||!read_local((void*)node,&type,8)||
            type<(uintptr_t)image||type>=(uintptr_t)(image+0x6fa9000)) return 0;
        hash=(hash^(type-(uintptr_t)image))*1099511628211ULL;
    }
    *hash_out=hash;return 1;
}
/* index is the immutable node slot selected on entry; output PC may jump. */
static int world_action_guard_read(void *node,void *exec,void *frame,uint64_t index,world_action_guard *g) {
    memset(g,0,sizeof(*g));g->node=(uintptr_t)node;g->exec=(uintptr_t)exec;g->frame=(uintptr_t)frame;g->index=index;
    if(!node||!exec||g->exec>UINTPTR_MAX-0x38||g->frame!=g->exec+0x38||
       !read_local(image+NPC_PROVENANCE_CURRENT,&g->current,8)||g->current!=g->exec||
       !npc_provenance_current_matches(g->current)||world_action_owner(exec,g)!=1||
       !read_local((void*)(g->context+0xd8),&g->script,8)||!g->script||
       !read_local((void*)(g->script+0x58),&g->version,4)||!ai_script((void*)g->context,g->name)||
       !read_local((unsigned char*)exec+0x10,&g->program,8)||!g->program||
       !read_local((void*)g->program,&g->begin,8)||!read_local((void*)(g->program+8),&g->end,8)||
       !world_action_program(g,&g->program_hash)||index>=(g->end-g->begin)/8) return 0;
    uintptr_t again=0;
    return read_local((void*)(g->begin+index*8),&again,8)&&again==g->node&&
        read_local(node,&g->vtable,8)&&g->vtable>=(uintptr_t)image&&g->vtable<=(uintptr_t)(image+0x6fa9000-32)&&
        read_local((void*)(g->vtable+0x18),&g->action,8)&&g->action>=(uintptr_t)image&&g->action<(uintptr_t)(image+0x6fa9000)&&
        read_local((unsigned char*)node+0x10,&g->chance,4);
}
static unsigned world_action_stage(const world_action_guard *g) {
    const unsigned tables[]={0x2c0cc88,0x2c0cdd0,0x2c0fb30},targets[]={0xbe2320,0xbe0ba0,0xb77830};
    for(unsigned i=0;i<3;i++) if(g->vtable==(uintptr_t)(image+tables[i])) return g->action==(uintptr_t)(image+targets[i])?i+1:UINT_MAX;
    return 0; /* Known raw dispatch metadata, uninterpreted action class. */
}
static int world_action_phase(world_queue_phase_sample *s,void *tls) {
    ULONGLONG now=GetTickCount64();int ok=world_queue_phase_read(read_local,tls,s);
    s->main=GetCurrentThreadId()==main_thread;s->elapsed=now>=boot_started?now-boot_started:UINT64_MAX;
    s->eligible=ok&&boot_mode&&boot_phase==2&&!stats.issued&&GetCurrentThreadId()==ui_thread&&!s->main&&s->view==1&&
        s->elapsed<=120000&&s->tls_time==s->game&&s->game==s->saved&&
        ((s->app_flags==0x26&&s->vbi_loading==1)||(s->app_flags==0x20&&s->vbi_loading==0));
    return s->eligible;
}
static int world_action_sample(void *tls,void *(*tls_reader)(void),uint64_t *rng,void *exec,uint64_t *pc,unsigned *flags) {
    uint64_t again;unsigned short raw_flags;
    if(!tls||tls_reader()!=tls||!read_local((unsigned char*)tls+0x318,rng,8)||
       !read_local((unsigned char*)exec+0x18,pc,8)||!read_local((unsigned char*)exec+0x30,&raw_flags,2)||
       !read_local((unsigned char*)tls+0x318,&again,8)||again!=*rng) return 0;
    *flags=raw_flags;return 1;
}
static int world_action_admit(void) {
    AcquireSRWLockExclusive(&world_action_lock);int ok=world_action_enabled&&!world_action_closing&&stats.active;
    if(ok) {++world_action_calls;if(++world_action_open>world_action_peak) world_action_peak=world_action_open;}
    ReleaseSRWLockExclusive(&world_action_lock);return ok;
}
static void world_action_observe_with_tls(nested_action_fn original,void *node,void *exec,void *frame,void *(*tls_reader)(void)) {
    if(!world_action_admit()) {original(node,exec,frame);return;}
    world_action_event *previous=world_action_current,*e=NULL;world_action_current=NULL;++world_action_depth;
    void *tls=tls_reader();world_action_guard owner={0};unsigned selected=0;
    if(boot_phase!=2||stats.issued) InterlockedIncrement64(&world_action_other_phase);
    else if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&world_action_threads);
    else {
        unsigned reason=0;int status=world_action_owner_ex(exec,&owner,&reason);
        if(status==2) InterlockedIncrement64(&world_action_other_context);
        else if(!status) {InterlockedIncrement64(&world_action_unknown);if(world_action_unknown_callback) world_action_unknown_callback(node,exec,frame,reason,&owner);}
        else {
            int watched=0;for(unsigned i=0;i<world_action_selected_count;i++) if(owner.saved==world_action_selected[i].saved) watched=1;
            if(!watched) InterlockedIncrement64(&world_action_other_owner);
            else {
                if(!ai_script((void*)owner.context,owner.name)) {InterlockedIncrement64(&world_action_unknown);if(world_action_unknown_callback) world_action_unknown_callback(node,exec,frame,7,&owner);}
                else {
                    for(unsigned i=0;i<world_action_selected_count;i++) if(owner.saved==world_action_selected[i].saved&&!strcmp(owner.name,world_action_selected[i].name)) selected=i+1;
                    if(!selected) InterlockedIncrement64(&world_action_other_script);
                }
            }
        }
    }
    if(selected) {
        LONG64 index=InterlockedIncrement64(&world_action_reserved)-1;
        if(index>=WORLD_ACTION_MAX) {InterlockedIncrement64(&world_action_dropped);if(previous) previous->errors|=WA_DEPTH;}
        else {
            e=&world_action_events[index];e->selected=selected;e->thread=GetCurrentThreadId();e->ui=e->thread==ui_thread;e->boot=boot_phase;e->tick=stats.issued;
            e->parent=previous?(uint64_t)(previous-world_action_events)+1:0;e->depth=world_action_depth;
            e->enter=InterlockedIncrement64(&world_action_sequence);if(previous) ++previous->children;
            if(e->depth>64) e->errors|=WA_DEPTH;
            if(!world_action_sample(tls,tls_reader,&e->rng_before,exec,&e->pc_before,&e->flags_before)||!e->pc_before) e->errors|=WA_READ;
            if(!world_action_guard_read(node,exec,frame,e->pc_before?e->pc_before-1:UINT64_MAX,&e->before)) e->errors|=WA_OWNER;
            world_action_guard again;
            if(!world_action_guard_read(node,exec,frame,e->before.index,&again)||memcmp(&again,&e->before,sizeof(again))) e->errors|=WA_LIFETIME;
            const world_action_selection *s=&world_action_selected[selected-1];
            if(e->before.saved!=s->saved||e->before.version!=s->version||strcmp(e->before.name,s->name)) e->errors|=WA_VERSION;
            e->stage=world_action_stage(&e->before);if(e->stage==UINT_MAX) e->errors|=WA_NODE;
            if(!world_action_phase(&e->phase_before,tls)) e->errors|=WA_PHASE;
            world_action_current=e;
        }
    }
    if(!e&&previous) ++previous->unobserved_children;
    original(node,exec,frame); /* exactly once, including every unsupported path */
    if(e) {
        if(!world_action_guard_read(node,exec,frame,e->before.index,&e->after)||memcmp(&e->before,&e->after,sizeof(e->before))) e->errors|=WA_LIFETIME;
        world_action_guard again;
        if(!world_action_guard_read(node,exec,frame,e->before.index,&again)||memcmp(&again,&e->after,sizeof(again))) e->errors|=WA_LIFETIME;
        if(!world_action_sample(tls,tls_reader,&e->rng_after,exec,&e->pc_after,&e->flags_after)) e->errors|=WA_READ;
        if(!world_action_phase(&e->phase_after,tls)) e->errors|=WA_PHASE;
        if(e->phase_before.app_flags!=e->phase_after.app_flags||e->phase_before.vbi_loading!=e->phase_after.vbi_loading||
           e->phase_before.game!=e->phase_after.game||e->phase_before.saved!=e->phase_after.saved||e->phase_before.tls_time!=e->phase_after.tls_time) e->errors|=WA_PHASE;
        if(!e->stage&&e->rng_before!=e->rng_after) InterlockedIncrement64(&world_action_unknown_rng);
        e->leave=InterlockedIncrement64(&world_action_sequence);
    }
    if(world_action_current!=e && e) e->errors|=WA_DEPTH;
    world_action_current=previous;if(world_action_depth) --world_action_depth;else if(e) e->errors|=WA_DEPTH;
    AcquireSRWLockExclusive(&world_action_lock);
    if(e) {if(world_action_closing) e->errors|=WA_STOP;if(e->errors) ++world_action_failures;InterlockedExchange(&e->complete,1);}
    if(world_action_open<=0) ++world_action_failures;else --world_action_open;
    ReleaseSRWLockExclusive(&world_action_lock);
}
static void world_action_observe(nested_action_fn original,void *node,void *exec,void *frame) {
    world_action_observe_with_tls(original,node,exec,frame,world_queue_tls);
}
static void world_action_restore(void) {
    AcquireSRWLockExclusive(&world_action_lock);world_action_closing=1;world_action_enabled=0;
    if(world_action_open>world_action_restore_open) world_action_restore_open=world_action_open;
    /* Callback stays resident. Late shared dispatch calls still run original. */
    ReleaseSRWLockExclusive(&world_action_lock);
}
#include "lockstep_world_action_pins.h"
static int world_action_configure(const char *text,size_t length) {
    world_action_selection parsed[WORLD_ACTION_SELECTED];unsigned count;
    if(GetCurrentThreadId()!=ui_thread||!world_action_parse(text,length,parsed,&count)) return 0;
    AcquireSRWLockExclusive(&world_action_lock);
    int ok=stats.active&&!world_action_closing&&world_action_dispatch_owned();
    if(world_action_started) ok=ok&&world_action_enabled&&strlen(world_action_manifest)==length&&!memcmp(text,world_action_manifest,length);
    else {
        ok=ok&&boot_mode&&!boot_phase&&!stats.issued&&world_trace_early&&world_queue_enabled&&
            (boot_options&281474976710656ULL)&&npc_provenance_enabled&&world_action_pins();
        if(ok&&stats.active) {
            memcpy(world_action_selected,parsed,sizeof(parsed));world_action_selected_count=count;
            memcpy(world_action_manifest,text,length);world_action_manifest[length]=0;world_action_enabled=1;
            InterlockedExchange(&world_action_started,1);InterlockedExchangePointer(&world_action_callback,(PVOID)world_action_observe);
        } else ok=0;
    }
    ReleaseSRWLockExclusive(&world_action_lock);return ok;
}
static unsigned world_action_ready(void) {
    unsigned limit=world_action_reserved>WORLD_ACTION_MAX?WORLD_ACTION_MAX:(unsigned)world_action_reserved,ready=0;
    while(ready<limit&&InterlockedCompareExchange(&world_action_events[ready].complete,0,0)) ++ready;return ready;
}
