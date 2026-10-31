/* Default-off, selected saved Wait restoration observations. Original engine
 * calls execute once on every path. Default read-only; the separately configured
 * saved-Wait RNG experiment is confined to the original call's TLS stream. */
#define WORLD_WAIT_ENTRY 0x10a50d0u
#define WORLD_WAIT_CALLER 0x1069bfcu
#define WORLD_WAIT_VTABLE 0x2c68d78u
#define WORLD_WAIT_MAX 128u
#define WORLD_WAIT_SELECTED 8u
#define WORLD_WAIT_MANIFEST_CAP 2048u
typedef unsigned char (*world_wait_fn)(void *,void *,void *);
typedef struct {uint64_t saved,time,minimum,maximum;unsigned version,index;char name[96];} world_wait_selection;
typedef struct {
    uintptr_t self,exec,value,context,context_vtable,entity,script,program,begin,end,vtable;
    uint64_t lifetime,runtime,saved,pc,slot,time;
    unsigned version,since,tag;unsigned char blocking;
    uintptr_t listeners;unsigned char timeout[16],expression[0x68];char name[96];
} world_wait_guard;
typedef struct {
    volatile LONG complete;
    unsigned errors,selected,thread,ui,boot,result,index_status;
    uint64_t tick,enter,leave,rng_before,rng_after,due_before,due_after;
    unsigned flags_before,flags_after,reason_before,reason_after;
    uintptr_t caller;world_wait_guard before,after;world_queue_phase_sample phase_before,phase_after;
    unsigned rng_configured,rng_applied,rng_refusal,rng_restored,rng_policy;
    uint64_t rng_seed,rng_scoped_before,rng_scoped_after,rng_caller_restored;
} world_wait_event;
/* Optional copied import anchor; observer never invokes the original. */
static void (*world_wait_anchor_observer)(const world_wait_event*,unsigned);
/* Error bits: read1, identity2, phase4, manifest8, expression16,
 * lifetime/input-change32, original-result64, stop-overlap128, RNG experiment256. */
static world_wait_selection world_wait_selected[WORLD_WAIT_SELECTED];
static world_wait_event world_wait_events[WORLD_WAIT_MAX];
static char world_wait_manifest[WORLD_WAIT_MANIFEST_CAP];
static unsigned world_wait_selected_count,world_wait_started,world_wait_installed,world_wait_exported;
static volatile LONG64 world_wait_count,world_wait_calls,world_wait_import_calls,world_wait_foreign_calls;
static volatile LONG64 world_wait_unknown,world_wait_failures,world_wait_dropped,world_wait_sequence;
static LONG64 world_wait_open,world_wait_peak,world_wait_restore_open;
static int world_wait_enabled,world_wait_restoring;
static SRWLOCK world_wait_lock=SRWLOCK_INIT;
static uint64_t world_wait_original,world_wait_patched;

static int world_wait_number(const char **cursor,char delimiter,unsigned base,uint64_t *value) {
    const char *start=*cursor,*p=start;uint64_t n=0;
    while(*p && *p!=delimiter) {
        unsigned d=*p>='0'&&*p<='9'?(unsigned)(*p-'0'):*p>='a'&&*p<='f'?(unsigned)(*p-'a'+10):16;
        if(d>=base || n>(UINT64_MAX-d)/base) return 0;n=n*base+d;++p;
    }
    if(p==start || *p!=delimiter || (base==16 && p-start!=16)) return 0;
    *value=n;*cursor=p+1;return 1;
}
static int world_wait_parse(const char *text,size_t size,world_wait_selection out[WORLD_WAIT_SELECTED],unsigned *count) {
    if(!text || !size || size>=WORLD_WAIT_MANIFEST_CAP || memchr(text,0,size)) return 0;
    char copy[WORLD_WAIT_MANIFEST_CAP];memcpy(copy,text,size);copy[size]=0;const char *p=copy;unsigned n=0;
    memset(out,0,sizeof(*out)*WORLD_WAIT_SELECTED);
    while(*p) {
        if(n==WORLD_WAIT_SELECTED) return 0;world_wait_selection *s=&out[n];uint64_t version,index;
        if(!world_wait_number(&p,',',16,&s->saved) || !s->saved ||
           !world_wait_number(&p,',',10,&version) || version>INT32_MAX ||
           !world_wait_number(&p,',',10,&index) || index>65535 ||
           !world_wait_number(&p,',',16,&s->time) || !world_wait_number(&p,',',16,&s->minimum) ||
           !world_wait_number(&p,',',16,&s->maximum)) return 0;
        s->version=(unsigned)version;s->index=(unsigned)index;unsigned length=0;
        while(*p && *p!=';') {
            char c=*p++;if(length==95 || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.')) return 0;
            s->name[length++]=c;
        }
        if(!length || (*p==';' && !*++p)) return 0;
        double time,minimum,maximum;memcpy(&time,&s->time,8);memcpy(&minimum,&s->minimum,8);memcpy(&maximum,&s->maximum,8);
        if(!isfinite(time)||time<0||!isfinite(minimum)||minimum<0||!isfinite(maximum)||maximum<=minimum) return 0;
        for(unsigned i=0;i<n;i++) if(out[i].saved==s->saved && !strcmp(out[i].name,s->name)) return 0;
        ++n;
    }
    *count=n;return n>0;
}
static int world_wait_admit(void) {
    AcquireSRWLockExclusive(&world_wait_lock);
    int yes=world_wait_enabled && !world_wait_restoring && stats.active;
    if(yes) {InterlockedIncrement64(&world_wait_calls);if(++world_wait_open>world_wait_peak) world_wait_peak=world_wait_open;}
    ReleaseSRWLockExclusive(&world_wait_lock);return yes;
}
static void world_wait_leave(world_wait_event *e) {
    AcquireSRWLockExclusive(&world_wait_lock);
    if(e && world_wait_restoring) e->errors|=128;
    if(e) {if(e->errors) InterlockedIncrement64(&world_wait_failures);InterlockedExchange(&e->complete,1);}
    if(world_wait_open<=0) InterlockedIncrement64(&world_wait_failures);else --world_wait_open;
    ReleaseSRWLockExclusive(&world_wait_lock);
}
/* Shallow canonical owner lookup before selection; no script/Agent getters. */
static int world_wait_owner(void *self,void *exec,void *value,world_wait_guard *g) {
    memset(g,0,sizeof(*g));g->self=(uintptr_t)self;g->exec=(uintptr_t)exec;g->value=(uintptr_t)value;
    uintptr_t payload=0;
    if(!self || !exec || g->exec>UINTPTR_MAX-0x40 || g->value!=g->exec+0x38 ||
       !read_local(value,&g->tag,4) || g->tag!=0x66 || !read_local((unsigned char*)value+8,&payload,8) || payload<0xa8) return 0;
    g->context=payload-0xa8;
    if(!read_local((void*)g->context,&g->context_vtable,8) || g->context_vtable!=(uintptr_t)(image+0x2c6a270) ||
       !read_local((void*)(g->context+8),&g->lifetime,8) || !g->lifetime ||
       !read_local((void*)(g->context+0x120),&g->entity,8) || !g->entity ||
       !read_local((void*)(g->entity+8),&g->runtime,8) || !g->runtime) return 0;
    g->saved=world_identity_lookup(g->runtime);return 1;
}
static int world_wait_guard_read(void *self,void *exec,void *value,world_wait_guard *g) {
    if(!world_wait_owner(self,exec,value,g)) return 0;
    if(!read_local(self,&g->vtable,8) || !read_local((unsigned char*)self+0x70,&g->since,4) ||
       !read_local((unsigned char*)self+0x30,&g->listeners,8) || !read_local((unsigned char*)self+0x60,g->timeout,16) ||
       !read_local((unsigned char*)self+0x78,g->expression,sizeof(g->expression)) ||
       !read_local((void*)(g->context+0xd8),&g->script,8) || !g->script ||
       !read_local((void*)(g->script+0x58),&g->version,4) || !ai_script((void*)g->context,g->name) ||
       !read_local((unsigned char*)exec+0x10,&g->program,8) || !g->program ||
       !read_local((void*)g->program,&g->begin,8) || !read_local((void*)(g->program+8),&g->end,8) ||
       !read_local((unsigned char*)exec+0x18,&g->pc,8) || !read_local((unsigned char*)exec+0x20,&g->slot,8) ||
       !read_local((unsigned char*)exec+0x28,&g->time,8) || !read_local((unsigned char*)exec+0x30,&g->blocking,1)) return 0;
    if(!g->begin || g->end<g->begin || (g->end-g->begin)%8 || g->end-g->begin>65536*8 || g->slot>=(g->end-g->begin)/8) return 0;
    uintptr_t node=0;return read_local((void*)(g->begin+g->slot*8),&node,8) && node==g->self;
}
static int world_wait_numeric(const unsigned char *value,uint64_t *bits) {
    unsigned tag;uint64_t payload;memcpy(&tag,value,4);memcpy(&payload,value+8,8);double n;
    if(tag<=1) {int32_t v;memcpy(&v,&payload,4);n=v;}
    else if(tag<=3) {int64_t v;memcpy(&v,&payload,8);n=(double)v;}
    else if(tag<=6) {float v;memcpy(&v,&payload,4);n=v;}
    else if(tag<=9) memcpy(&n,&payload,8);else return 0;
    if(!isfinite(n)) return 0;memcpy(bits,&n,8);return 1;
}
static int world_wait_expression(const world_wait_guard *g,const world_wait_selection *s) {
    unsigned mode,distribution,draws,seed,basis,timeout;
    memcpy(&mode,g->expression+8,4);memcpy(&distribution,g->expression+12,4);memcpy(&draws,g->expression+16,4);
    memcpy(&seed,g->expression+0x38,4);memcpy(&basis,g->expression+0x48,4);memcpy(&timeout,g->timeout,4);
    uint64_t minimum,maximum;
    return g->vtable==(uintptr_t)(image+WORLD_WAIT_VTABLE) && g->blocking==1 && g->since<=s->version &&
        mode==2 && distribution==1 && draws==1 && !seed && !basis && !timeout && !g->listeners &&
        world_wait_numeric(g->expression+0x18,&minimum) && world_wait_numeric(g->expression+0x28,&maximum) &&
        minimum==s->minimum && maximum==s->maximum;
}
static int world_wait_outputs(world_wait_event *e,void *tls,int after) {
    uint64_t *rng=after?&e->rng_after:&e->rng_before,*due=after?&e->due_after:&e->due_before;
    unsigned *flags=after?&e->flags_after:&e->flags_before,*reason=after?&e->reason_after:&e->reason_before;
    uintptr_t context=after?e->after.context:e->before.context;
    if(!(tls && context && read_local((unsigned char*)tls+0x318,rng,8) &&
        read_local((void*)(context+0x188),flags,4) && read_local((void*)(context+0x18c),reason,4) &&
        read_local((void*)(context+0x190),due,8))) return 0;
    uint64_t rng_again,due_again;unsigned flags_again,reason_again;
    return read_local((unsigned char*)tls+0x318,&rng_again,8) && rng_again==*rng &&
        read_local((void*)(context+0x188),&flags_again,4) && flags_again==*flags &&
        read_local((void*)(context+0x18c),&reason_again,4) && reason_again==*reason &&
        read_local((void*)(context+0x190),&due_again,8) && due_again==*due;
}
static int world_wait_phase(world_queue_phase_sample *s,void *tls) {
    ULONGLONG now=GetTickCount64();int ok=world_queue_phase_read(read_local,tls,s);
    s->main=GetCurrentThreadId()==main_thread;s->elapsed=now>=boot_started?now-boot_started:UINT64_MAX;
    /* No UI-thread assumption: exact importer caller is the observed scope.
     * Loading indicators are retained, not interpreted as an in-Load proof. */
    s->eligible=ok && boot_mode && boot_phase==2 && !stats.issued && s->elapsed<=120000;return s->eligible;
}
#include "lockstep_world_wait_rng.h"
#include "lockstep_ai_wait_rng.h"
static unsigned char world_wait_observe_with_tls(world_wait_fn original,void *self,void *exec,void *value,uintptr_t caller,void *tls,void *(*current_tls)(void)) {
    int admitted=world_wait_admit();world_wait_event *event=NULL;world_wait_guard shallow;
    if(admitted) {
        if(caller!=(uintptr_t)(image+WORLD_WAIT_CALLER)) InterlockedIncrement64(&world_wait_foreign_calls);
        else {
            InterlockedIncrement64(&world_wait_import_calls);unsigned selected=0;
            int readable=world_wait_owner(self,exec,value,&shallow);world_wait_guard owner_again;
            if(!world_wait_owner(self,exec,value,&owner_again) || memcmp(&shallow,&owner_again,sizeof(shallow))) readable=0;
            if(readable && shallow.saved) {
                int watched=0;for(unsigned i=0;i<world_wait_selected_count;i++) if(world_wait_selected[i].saved==shallow.saved) watched=1;
                if(watched) {
                    char name[96]={0};if(!ai_script((void*)shallow.context,name)) readable=0;
                    else for(unsigned i=0;i<world_wait_selected_count;i++) if(world_wait_selected[i].saved==shallow.saved && !strcmp(world_wait_selected[i].name,name)) {selected=i+1;break;}
                }
            } else InterlockedIncrement64(&world_wait_unknown);
            if(selected || !readable || !shallow.saved) {
                LONG64 ordinal=InterlockedIncrement64(&world_wait_count)-1;
                if(ordinal>=WORLD_WAIT_MAX) {InterlockedIncrement64(&world_wait_dropped);InterlockedIncrement64(&world_wait_failures);}
                else {
                    event=&world_wait_events[ordinal];event->selected=selected;event->caller=caller;
                    event->thread=GetCurrentThreadId();event->ui=event->thread==ui_thread;event->boot=(unsigned)boot_phase;event->tick=stats.issued;
                    event->enter=(uint64_t)InterlockedIncrement64(&world_wait_sequence);event->before=shallow;
                    if(!readable) event->errors|=1;if(!shallow.saved) event->errors|=2;
                    if(!world_wait_guard_read(self,exec,value,&event->before)) event->errors|=1;
                    world_wait_guard again;
                    if(!world_wait_guard_read(self,exec,value,&again) || memcmp(&event->before,&again,sizeof(again))) event->errors|=32;
                    if(selected) {
                        const world_wait_selection *s=&world_wait_selected[selected-1];
                        if(event->before.saved!=s->saved || event->before.version!=s->version || event->before.time!=s->time || strcmp(event->before.name,s->name)) event->errors|=8;
                        if(!world_wait_expression(&event->before,s)) event->errors|=16;
                    }
                    /* Expected XML ordinal is manifest metadata. We do not call
                     * engine predicates to count preceding blocking actions. */
                    event->index_status=0;
                    if(!world_wait_phase(&event->phase_before,tls)) event->errors|=4;
                    if(!world_wait_outputs(event,tls,0)) event->errors|=1;
                    if(!world_wait_guard_read(self,exec,value,&again) || memcmp(&event->before,&again,sizeof(again))) event->errors|=32;
                }
            }
        }
    }
    if(world_wait_anchor_observer) world_wait_anchor_observer(event,0);
    world_wait_rng_scope rng_scope={0};
    world_wait_rng_begin(&rng_scope,event,tls,current_tls);
    /* native_gate_ai_wait_rng keys this one call (exclusive with the selected experiment). */
    unsigned char result=ai_wait_call(original,self,exec,value,caller);
    world_wait_rng_end(&rng_scope);
    if(event) {
        event->result=result;
        if(!world_wait_guard_read(self,exec,value,&event->after) || memcmp(&event->before,&event->after,sizeof(event->before))) event->errors|=32;
        if(!world_wait_phase(&event->phase_after,tls)) event->errors|=4;
        if(!world_wait_outputs(event,tls,1)) event->errors|=1;
        world_wait_guard again;
        if(!world_wait_guard_read(self,exec,value,&again) || memcmp(&event->after,&again,sizeof(again))) event->errors|=32;
        if(result!=1) event->errors|=64;world_wait_rng_post(event);event->leave=(uint64_t)InterlockedIncrement64(&world_wait_sequence);
    }
    if(admitted) world_wait_leave(event);
    if(world_wait_anchor_observer) world_wait_anchor_observer(event,1);
    return result;
}
static unsigned char world_wait_observe(world_wait_fn original,void *self,void *exec,void *value,uintptr_t caller,void *tls) {
    return world_wait_observe_with_tls(original,self,exec,value,caller,tls,world_queue_tls);
}
static unsigned char world_wait_restore_call(void *self,void *exec,void *value) {
    return world_wait_observe((world_wait_fn)(thunk+2752),self,exec,value,(uintptr_t)__builtin_return_address(0),world_queue_tls());
}
static int world_wait_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+WORLD_WAIT_ENTRY,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+WORLD_WAIT_ENTRY),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+WORLD_WAIT_ENTRY,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+WORLD_WAIT_ENTRY,8);return actual==expected;
}
static void world_wait_restore(void) {
    AcquireSRWLockExclusive(&world_wait_lock);world_wait_restoring=1;world_wait_enabled=0;
    if(world_wait_rng_open>world_wait_rng_restore_open) world_wait_rng_restore_open=world_wait_rng_open;
    if(world_wait_open>world_wait_restore_open) world_wait_restore_open=world_wait_open;
    if(world_wait_installed && world_wait_swap(world_wait_patched,world_wait_original)) world_wait_installed=0;
    ReleaseSRWLockExclusive(&world_wait_lock);
}
static void world_wait_build_thunk(unsigned char stub[64],unsigned char *entry,void *target) {
    memset(stub,0,64);memcpy(stub,"\x48\x89\x5c\x24\x08",5);
    stub[5]=0xff;stub[6]=0x25;void *next=entry+5;memcpy(stub+11,&next,8);
    stub[32]=0xff;stub[33]=0x25;memcpy(stub+38,&target,8);
}
/* make_thunk owns an unpublished RW allocation here. No engine entry points
 * to this page yet. Disabled Wait observation does not depend on its pins. */
static void world_wait_prepare_thunk(unsigned char *page) {
    world_wait_build_thunk(page+2752,image+WORLD_WAIT_ENTRY,(void*)world_wait_restore_call);
}
static int world_wait_thunk_matches(void) {
    unsigned char expected[64],actual[64];MEMORY_BASIC_INFORMATION info;
    world_wait_build_thunk(expected,image+WORLD_WAIT_ENTRY,(void*)world_wait_restore_call);
    if(!thunk || !VirtualQuery(thunk+2752,&info,sizeof(info)) || info.State!=MEM_COMMIT || info.Protect!=PAGE_EXECUTE_READ ||
       (uintptr_t)info.BaseAddress>(uintptr_t)(thunk+2752) ||
       (uintptr_t)(thunk+2816)-(uintptr_t)info.BaseAddress>info.RegionSize) return 0;
    return read_local(thunk+2752,actual,sizeof(actual)) && !memcmp(expected,actual,sizeof(actual));
}
/* Full contiguous image pins, including the descriptor+58 saved-version store
 * and exact importer call. This never invokes the pinned engine functions. */
#include "lockstep_world_wait_pins.h"
/* lockstep_decimal.h: X4's saved-value reader replaced by the correctly rounded one. The
 * manifest's saved times are the stock reader's bits (native_gate_wait_manifest.py), so a
 * selected-Wait manifest and native_gate_decimal refuse each other in either order. */
static int pdec_active(void);
static int world_wait_configure(const char *text,size_t length) {
    world_wait_selection parsed[WORLD_WAIT_SELECTED];unsigned count;
    if(!world_wait_parse(text,length,parsed,&count)) return 0;
    if(world_wait_started) {
        AcquireSRWLockShared(&world_wait_lock);
        uint64_t entry=0;int same=world_wait_installed && world_wait_enabled && strlen(world_wait_manifest)==length && !memcmp(text,world_wait_manifest,length) &&
            world_wait_thunk_matches() && read_local(image+WORLD_WAIT_ENTRY,&entry,8) && entry==world_wait_patched;
        ReleaseSRWLockShared(&world_wait_lock);return same;
    }
    /* An entry already installed (native_gate_ai_wait_rng armed first) is never re-read as original. */
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || world_wait_installed ||
       !world_identity_installed || !world_trace_early || pdec_active() || !world_wait_pins() || !world_wait_thunk_matches()) return 0;
    intptr_t distance=(intptr_t)(thunk+2784)-(intptr_t)(image+WORLD_WAIT_ENTRY+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&world_wait_original,image+WORLD_WAIT_ENTRY,8);world_wait_patched=world_wait_original;
    int32_t relative=(int32_t)distance;((unsigned char*)&world_wait_patched)[0]=0xe9;memcpy((unsigned char*)&world_wait_patched+1,&relative,4);
    /* Publish immutable manifest/admission and the hook under the restoration
     * lock. A concurrent STOP cannot miss a not-yet-published installed bit. */
    memcpy(world_wait_selected,parsed,sizeof(parsed));world_wait_selected_count=count;
    memcpy(world_wait_manifest,text,length);world_wait_manifest[length]=0;world_wait_started=1;
    AcquireSRWLockExclusive(&world_wait_lock);
    int ok=stats.active && !world_wait_restoring;
    if(ok) {world_wait_enabled=1;ok=world_wait_swap(world_wait_original,world_wait_patched);}
    if(ok) world_wait_installed=1;else {world_wait_enabled=0;world_wait_restoring=1;}
    ReleaseSRWLockExclusive(&world_wait_lock);return ok;
}
static int world_wait_rng_configure(const char *text,size_t length) {
    if(GetCurrentThreadId()!=ui_thread||!world_wait_started) return 0;
    world_wait_rng_selection parsed[WORLD_WAIT_SELECTED];uint64_t snapshot[4];unsigned schema;
    if(!world_wait_rng_parse(text,length,parsed,snapshot,&schema)) return 0;
    AcquireSRWLockExclusive(&world_wait_lock);uint64_t entry=0;
    int ok=stats.active&&!ai_wait_armed&&world_wait_enabled&&!world_wait_restoring&&world_wait_installed&&world_wait_thunk_matches()&&
        read_local(image+WORLD_WAIT_ENTRY,&entry,8)&&entry==world_wait_patched;
    if(world_wait_rng_started) {
        ok=ok&&strlen(world_wait_rng_manifest)==length&&!memcmp(world_wait_rng_manifest,text,length);
    } else {
        ok=ok&&boot_mode&&!boot_phase&&!stats.issued;
        if(ok&&world_wait_rng_tls==TLS_OUT_OF_INDEXES) world_wait_rng_tls=TlsAlloc();
        ok=ok&&world_wait_rng_tls!=TLS_OUT_OF_INDEXES;
        if(ok) {
            memcpy(world_wait_rng_selected,parsed,sizeof(parsed));memcpy(world_wait_rng_snapshot,snapshot,sizeof(snapshot));world_wait_rng_schema=schema;
            memcpy(world_wait_rng_manifest,text,length);world_wait_rng_manifest[length]=0;InterlockedExchange(&world_wait_rng_started,1);
        }
    }
    ReleaseSRWLockExclusive(&world_wait_lock);return ok;
}
static unsigned world_wait_ready(void) {
    unsigned limit=(unsigned)(world_wait_count>WORLD_WAIT_MAX?WORLD_WAIT_MAX:world_wait_count),ready=0;
    while(ready<limit && InterlockedCompareExchange(&world_wait_events[ready].complete,0,0)) ++ready;return ready;
}
