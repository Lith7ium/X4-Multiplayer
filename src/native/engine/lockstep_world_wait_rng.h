/* Selected saved-Wait RNG isolation experiment, DEFAULT OFF. Reuses the owned
 * Wait hook. Only normal-return cleanup is supported; no engine exception
 * cleanup or global RNG synchronization is claimed. */
#define WORLD_WAIT_RNG_DOMAIN 0x574149545f535631ULL /* WAIT_SV1 */
#define WORLD_WAIT_RNG_CAP 4096u
#define WORLD_WAIT_POLICY_PREFIX "WAIT_POLICY_2;"
enum {WWR_INTERVENE=1,WWR_OBSERVE=2};
enum {WWR_RAW=1,WWR_BIND=2,WWR_PHASE=4,WWR_TLS=8,WWR_DUPLICATE=16,
      WWR_NESTED=32,WWR_CLOSED=64,WWR_RESTORE=128};
typedef struct {uint64_t slot,pc,script_hash,time_hash,seed;unsigned since,used,policy;char text[32];} world_wait_rng_selection;
static world_wait_rng_selection world_wait_rng_selected[WORLD_WAIT_SELECTED];
static uint64_t world_wait_rng_snapshot[4];
static char world_wait_rng_manifest[WORLD_WAIT_RNG_CAP];
static volatile LONG world_wait_rng_started;
static unsigned world_wait_rng_exported,world_wait_rng_applied,world_wait_rng_refused,world_wait_rng_observed,world_wait_rng_schema;
static LONG64 world_wait_rng_open,world_wait_rng_restore_open;
static DWORD world_wait_rng_tls=TLS_OUT_OF_INDEXES;
static int (*world_wait_rng_foreign_active)(void);
typedef struct world_wait_rng_scope {
    world_wait_event *event;uint64_t *address,caller;void *tls;
    void *(*current_tls)(void);int active;
} world_wait_rng_scope;
#include "lockstep_text_hash.h"
static uint64_t world_wait_rng_seed(const world_wait_selection *s,const world_wait_rng_selection *r,const uint64_t snapshot[4]) {
    uint64_t value=lockstep_seed(WORLD_WAIT_RNG_DOMAIN,s->version,s->saved,r->script_hash,s->index,r->time_hash);
    value=lockstep_mix(value^s->minimum);value=lockstep_mix(value^s->maximum);
    /* SHA words: displayed hex left-to-right, each parsed as a big-endian u64. */
    for(unsigned i=0;i<4;i++) value=lockstep_mix(value^snapshot[i]);
    return value;
}
static int world_wait_rng_time(const char *text,uint64_t *bits) {
    unsigned whole=0,fraction=0;int decimal=0;double sum=0,scale=0;
    if(!text || !*text || (text[0]=='0' && text[1] && text[1]!='.')) return 0;
    for(const char *p=text;*p;p++) {
        if(*p=='.' && !decimal && whole) {decimal=1;scale=1;continue;}
        if(*p<'0'||*p>'9') return 0;
        if(!decimal) {if(++whole>15) return 0;sum=sum*10.0;sum=sum+(double)(unsigned char)*p;sum=sum-48.0;}
        else {if(++fraction>15) return 0;scale=scale*0.1;double digit=(double)(*p-'0')*scale;sum=sum+digit;}
    }
    if(!whole || (decimal&&!fraction) || !isfinite(sum)) return 0;memcpy(bits,&sum,8);return 1;
}
static int world_wait_rng_parse(const char *text,size_t length,world_wait_rng_selection out[WORLD_WAIT_SELECTED],uint64_t snapshot[4],unsigned *schema_out) {
    if(!text||!length||length>=WORLD_WAIT_RNG_CAP||memchr(text,0,length)||!world_wait_selected_count) return 0;
    char copy[WORLD_WAIT_RNG_CAP];memcpy(copy,text,length);copy[length]=0;const char *p=copy;unsigned schema=1;
    if(!strncmp(p,WORLD_WAIT_POLICY_PREFIX,sizeof(WORLD_WAIT_POLICY_PREFIX)-1)) {schema=2;p+=sizeof(WORLD_WAIT_POLICY_PREFIX)-1;}
    for(unsigned word=0;word<4;word++) {
        uint64_t value=0;
        for(unsigned i=0;i<16;i++) {unsigned digit=*p>='0'&&*p<='9'?*p-'0':*p>='a'&&*p<='f'?*p-'a'+10:16;
            if(digit>15) return 0;value=(value<<4)|digit;++p;}
        snapshot[word]=value;
    }
    if(*p++!=';') return 0;unsigned count=0;memset(out,0,sizeof(*out)*WORLD_WAIT_SELECTED);
    while(*p) {
        if(count>=world_wait_selected_count) return 0;uint64_t selection,since;
        world_wait_rng_selection *r=&out[count];
        if(!world_wait_number(&p,',',10,&selection)||selection!=count+1) return 0;
        r->policy=WWR_INTERVENE;since=0;
        if(schema==2) {
            if(!strncmp(p,"intervene,",10)) p+=10;
            else if(!strncmp(p,"observe,",8)) {r->policy=WWR_OBSERVE;p+=8;}
            else return 0;
        }
        if(r->policy==WWR_INTERVENE &&
           (!world_wait_number(&p,',',10,&r->slot)||r->slot>=4096||
            !world_wait_number(&p,',',10,&r->pc)||r->pc>=1048576||
            !world_wait_number(&p,',',10,&since)||since>INT32_MAX)) return 0;
        r->since=(unsigned)since;unsigned n=0;
        while(*p&&*p!=';') {if(n==31) return 0;r->text[n++]=*p++;}
        if(!n||(*p==';'&&!*++p)) return 0;uint64_t time;
        if(!world_wait_rng_time(r->text,&time)||time!=world_wait_selected[count].time||since>world_wait_selected[count].version) return 0;
        r->script_hash=world_wait_rng_hash(world_wait_selected[count].name);r->time_hash=world_wait_rng_hash(r->text);
        if(r->policy==WWR_INTERVENE) r->seed=world_wait_rng_seed(&world_wait_selected[count],r,snapshot);++count;
    }
    if(count!=world_wait_selected_count) return 0;*schema_out=schema;return 1;
}
static int world_wait_rng_writable(uint64_t *address) {
    MEMORY_BASIC_INFORMATION info;
    return address && !((uintptr_t)address&7) && VirtualQuery(address,&info,sizeof(info)) && info.State==MEM_COMMIT &&
        !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && (info.Protect==PAGE_READWRITE||info.Protect==PAGE_WRITECOPY) &&
        (uintptr_t)address>=(uintptr_t)info.BaseAddress &&
        (uintptr_t)address+8<=(uintptr_t)info.BaseAddress+info.RegionSize;
}
static void world_wait_rng_refuse(world_wait_event *e,unsigned why) {
    if(e) {e->rng_refusal|=why;e->errors|=256;}
}
static void world_wait_rng_begin(world_wait_rng_scope *scope,world_wait_event *e,void *tls,void *(*current_tls)(void)) {
    if(!InterlockedCompareExchange(&world_wait_rng_started,0,0)) return;
    /* TLS retains a resident event, never a stack address. Unsupported native
     * exception unwind therefore cannot leave a dangling observer scope. */
    world_wait_event *parent=world_wait_rng_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(world_wait_rng_tls);
    if(parent) world_wait_rng_refuse(parent,WWR_NESTED);
    if(!e) return;e->rng_configured=1;
    unsigned why=e->errors?WWR_RAW:0;
    if(parent || (world_wait_rng_foreign_active && world_wait_rng_foreign_active())) why|=WWR_NESTED;
    if(!e->selected||e->selected>world_wait_selected_count) why|=WWR_BIND;
    const world_wait_rng_selection *r=why&WWR_BIND?NULL:&world_wait_rng_selected[e->selected-1];
    if(r) e->rng_policy=r->policy;
    /* Explicit natural observation bypasses every intervention guard, TLS write,
     * restoration and used/applied/refusal counter. Raw Wait errors stay intact.
     * A seeded parent above still records unsupported nesting. */
    if(r && r->policy==WWR_OBSERVE) {
        AcquireSRWLockExclusive(&world_wait_lock);++world_wait_rng_observed;ReleaseSRWLockExclusive(&world_wait_lock);return;
    }
    if(!r || r->policy!=WWR_INTERVENE || e->before.slot!=r->slot || e->before.pc!=r->pc || e->before.since!=r->since) why|=WWR_BIND;
    if(e->boot!=2||e->tick||!e->ui||e->phase_before.main||e->phase_before.view!=1||e->phase_before.app_flags!=0x26||
       e->phase_before.vbi_loading!=1||e->phase_before.game!=e->phase_before.saved||
       e->phase_before.tls_time!=e->phase_before.game||!e->phase_before.eligible) why|=WWR_PHASE;
    uint64_t *address=tls?(uint64_t*)((unsigned char*)tls+0x318):NULL,again=0;
    if(!current_tls||current_tls()!=tls||!world_wait_rng_writable(address)||
       !read_local(address,&again,8)||again!=e->rng_before) why|=WWR_TLS;
    AcquireSRWLockExclusive(&world_wait_lock);
    if(!stats.active||!world_wait_enabled||world_wait_restoring) why|=WWR_CLOSED;
    if(r && r->used) why|=WWR_DUPLICATE;
    if(!why) {
        world_wait_guard guard;world_queue_phase_sample phase;
        if(!world_wait_guard_read((void*)e->before.self,(void*)e->before.exec,(void*)e->before.value,&guard)||
           memcmp(&guard,&e->before,sizeof(guard))) why|=WWR_BIND;
        if(!world_wait_phase(&phase,tls)||phase.app_flags!=0x26||phase.vbi_loading!=1||phase.view!=1||phase.main||
           phase.game!=e->phase_before.game||phase.saved!=e->phase_before.saved||phase.tls_time!=e->phase_before.tls_time||
           GetCurrentThreadId()!=ui_thread||boot_phase!=2||stats.issued) why|=WWR_PHASE;
        if(current_tls()!=tls||!read_local(address,&again,8)||again!=e->rng_before) why|=WWR_TLS;
    }
    if(!why && !TlsSetValue(world_wait_rng_tls,e)) why|=WWR_TLS;
    if(why) {++world_wait_rng_refused;world_wait_rng_refuse(e,why);ReleaseSRWLockExclusive(&world_wait_lock);return;}
    /* No lock crosses original. STOP cannot close admission between the final
     * check and this TLS write; it records open overlap instead of draining. */
    scope->event=e;scope->address=address;scope->caller=again;scope->tls=tls;scope->current_tls=current_tls;scope->active=1;
    world_wait_rng_selected[e->selected-1].used=1;++world_wait_rng_open;++world_wait_rng_applied;
    e->rng_applied=1;e->rng_seed=r->seed;*address=r->seed;e->rng_scoped_before=*address;
    if(e->rng_scoped_before!=r->seed) world_wait_rng_refuse(e,WWR_TLS);
    ReleaseSRWLockExclusive(&world_wait_lock);
}
static void world_wait_rng_end(world_wait_rng_scope *scope) {
    if(!scope->active) return;world_wait_event *e=scope->event;
    int same_tls=scope->current_tls()==scope->tls;
    if(!same_tls) world_wait_rng_refuse(e,WWR_TLS|WWR_RESTORE);
    /* Original Wait has no TLS lifetime transition. Retain failures if that
     * contract changes; never write an unreadable/unwritable captured address. */
    if(!same_tls||!read_local(scope->address,&e->rng_scoped_after,8)||!world_wait_rng_writable(scope->address)) world_wait_rng_refuse(e,WWR_RESTORE);
    else {*scope->address=scope->caller;e->rng_caller_restored=*scope->address;e->rng_restored=e->rng_caller_restored==scope->caller;
        if(!e->rng_restored) world_wait_rng_refuse(e,WWR_RESTORE);}
    if(!TlsSetValue(world_wait_rng_tls,NULL)) world_wait_rng_refuse(e,WWR_TLS);
    AcquireSRWLockExclusive(&world_wait_lock);if(world_wait_rng_open<=0) world_wait_rng_refuse(e,WWR_RESTORE);else --world_wait_rng_open;
    ReleaseSRWLockExclusive(&world_wait_lock);scope->active=0;
}
static void world_wait_rng_post(world_wait_event *e) {
    if(!e||!e->rng_applied) return;
    const world_queue_phase_sample *a=&e->phase_before,*b=&e->phase_after;
    if(a->app_flags!=b->app_flags||a->vbi_loading!=b->vbi_loading||a->view!=b->view||a->main!=b->main||
       a->game!=b->game||a->saved!=b->saved||a->tls_time!=b->tls_time) world_wait_rng_refuse(e,WWR_PHASE);
    if(e->rng_after!=e->rng_caller_restored) world_wait_rng_refuse(e,WWR_RESTORE);
}
