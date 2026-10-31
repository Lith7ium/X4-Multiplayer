/* Separate imported watch schema2. Never aliases/rebinds world_trace_watches.
 * Native pointers are private observations, not retained native references. */
#define TRANSITION_WATCH_MAX 2u
typedef struct {
    uint64_t saved,runtime;uintptr_t object,agent,initial_controller;
    unsigned imports,initial_type;
    char macro[64],owner[64];
    transition_state initial;
} transition_saved_watch;
static transition_saved_watch transition_saved_watches[TRANSITION_WATCH_MAX];
static unsigned transition_saved_count,transition_saved_bound,transition_saved_last;
static unsigned transition_saved_watch_rows,transition_saved_ticket_rows;
static LONG64 transition_saved_failures,transition_saved_prebind,transition_saved_import_calls;
static char transition_saved_manifest[320];
static int transition_read_native(void *,void *,uint64_t,uint64_t,transition_state *);
static int transition_prebind_bind_locked(const transition_saved_watch *,unsigned,const transition_registry_identity *);
static int transition_prebind_healthy_locked(void);
static void transition_prebind_poison_locked(unsigned);
static int transition_saved_pins(void) {
    return transition_registry_pins() && hash_ok(image+0xefb10,5,"a8adead0a89e8bcc0f6079669194e35b31c483df478a1515e46c55a9cf74fb51") &&
        hash_ok(image+0x352b10,8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53");
}
static int transition_saved_parse(const char *text,size_t length,unsigned last,transition_saved_watch out[2],unsigned *count) {
    if(!text || length<2 || length>=sizeof(transition_saved_manifest) || memchr(text,0,length) || text[0]!='2' || text[1]!='|' || last<1 || last>120) return 0;
    char local[320];memcpy(local,text,length);local[length]=0;char *cursor=local+2;unsigned n=0;
    while(*cursor) {
        if(n>=2) return 0;char id[17]={0},type=0;int used=0;transition_saved_watch *w=&out[n];
        if(sscanf(cursor,"%16[0-9a-f]/%c/%63[a-z0-9_]/%63[a-z0-9_]%n",id,&type,w->macro,w->owner,&used)!=4 ||
           strlen(id)!=16 || (type!='p' && type!='n') || (cursor[used] && cursor[used]!=';')) return 0;
        for(unsigned i=0;i<16;i++) w->saved=(w->saved<<4)|(uint64_t)(id[i]<='9'?id[i]-'0':id[i]-'a'+10);
        if(!w->saved || (n && w->saved<=out[n-1].saved)) return 0;
        w->initial_type=type=='p'?0x2c39488:0x2c51158;++n;cursor+=used;
        if(*cursor && !*++cursor) return 0;
    }
    *count=n;return n>0;
}
static int transition_saved_config(const char *text,size_t length,unsigned last) {
    transition_saved_watch parsed[2]={0};unsigned count=0;
    if(!transition_saved_parse(text,length,last,parsed,&count)) return 0;
    int ok=0;AcquireSRWLockExclusive(&transition_lock);
    if(transition_closing || !stats.active || GetCurrentThreadId()!=ui_thread) goto done;
    if(transition_saved_count) {
        ok=last==transition_saved_last && strlen(transition_saved_manifest)==length && !memcmp(text,transition_saved_manifest,length);goto done;
    }
    if(transition_started || !boot_mode || boot_phase || stats.issued || !world_identity_installed || !transition_saved_pins()) goto done;
    memcpy(transition_saved_watches,parsed,sizeof(parsed));memcpy(transition_saved_manifest,text,length);
    transition_saved_manifest[length]=0;transition_saved_last=last;transition_saved_count=count;ok=1;
done:ReleaseSRWLockExclusive(&transition_lock);return ok;
}
static void transition_import_capture(void *object,uint64_t runtime,uint64_t saved) {
    AcquireSRWLockExclusive(&transition_lock);
    if(!transition_saved_count || transition_closing || !transition_enabled || !stats.active) {ReleaseSRWLockExclusive(&transition_lock);return;}
    ++transition_saved_import_calls;
    for(unsigned i=0;i<transition_saved_count;i++) if(transition_saved_watches[i].saved==saved) {
        transition_saved_watch *w=&transition_saved_watches[i];
        if(w->imports || transition_saved_bound || !object || !runtime || !boot_mode || boot_phase<1 || boot_phase>=3) ++transition_saved_failures;
        else {
            for(unsigned j=0;j<transition_saved_count;j++) if(j!=i && (transition_saved_watches[j].runtime==runtime || transition_saved_watches[j].object==(uintptr_t)object)) ++transition_saved_failures;
            w->runtime=runtime;w->object=(uintptr_t)object;w->imports=1;
        }
        break;
    }
    ReleaseSRWLockExclusive(&transition_lock);
}
static int transition_saved_object(void *object) {
    int found=-1;AcquireSRWLockExclusive(&transition_lock);
    for(unsigned i=0;i<transition_saved_count;i++) if(object && transition_saved_watches[i].object==(uintptr_t)object) {
        if(found!=-1 || transition_saved_watches[i].imports!=1 || transition_saved_failures) {found=-2;break;}
        if(!transition_saved_bound) {++transition_saved_prebind;break;}found=(int)i;
    }
    ReleaseSRWLockExclusive(&transition_lock);return found;
}
static int transition_saved_copy(unsigned index,transition_saved_watch *out) {
    AcquireSRWLockShared(&transition_lock);int ok=index<transition_saved_count && transition_saved_bound && !transition_saved_failures;
    if(ok) *out=transition_saved_watches[index];ReleaseSRWLockShared(&transition_lock);return ok;
}
static int transition_saved_guard(const transition_saved_watch *w,transition_state *s) {
    transition_registry_identity registry;
    return w->imports==1 && world_identity_lookup(w->runtime)==w->saved &&
        transition_registry_read(read_local,image+0x6cf1500,w->runtime,w->object,&registry) &&
        registry_macro_detail((void*)w->object,w->macro,NULL) && registry_owner_detail((void*)w->object,w->owner,NULL) &&
        s->saved==w->saved && s->runtime==w->runtime && s->object==w->object && (!w->agent || s->agent==w->agent) &&
        !strcmp(s->owner,w->owner);
}
static int transition_saved_distinct(const transition_saved_watch *w,unsigned count) {
    if(!count || count>TRANSITION_WATCH_MAX) return 0;
    for(unsigned i=0;i<count;i++) for(unsigned j=0;j<i;j++)
        if(w[i].saved==w[j].saved || w[i].runtime==w[j].runtime || w[i].object==w[j].object ||
           w[i].agent==w[j].agent || w[i].initial_controller==w[j].initial_controller) return 0;
    return 1;
}
static int transition_saved_bind(void) {
    if(!transition_saved_count) return 1;
    if(!transition_admit()) return 0;
    transition_saved_watch local[2];transition_registry_identity registry[2]={0};unsigned count;int ok=1;
    AcquireSRWLockShared(&transition_lock);count=transition_saved_count;memcpy(local,transition_saved_watches,sizeof(local));
    if(transition_saved_bound) {ok=!transition_saved_failures && transition_prebind_healthy_locked();ReleaseSRWLockShared(&transition_lock);transition_leave();return ok;}
    ok=!transition_saved_failures && boot_phase==3 && !stats.issued && GetCurrentThreadId()==main_thread;
    ReleaseSRWLockShared(&transition_lock);
    for(unsigned i=0;ok && i<count;i++) {
        transition_state a,b;transition_saved_watch *w=&local[i];
        ok=w->imports==1 && transition_read_native((void*)w->object,NULL,w->saved,w->runtime,&a) &&
            a.controller_type==w->initial_type && a.player==(w->initial_type==0x2c39488) && a.owns_agent==1 && transition_saved_guard(w,&a) &&
            transition_read_native((void*)w->object,NULL,w->saved,w->runtime,&b) && !memcmp(&a,&b,sizeof(a)) && transition_saved_guard(w,&b);
        if(ok && transition_prebind_enabled) ok=transition_registry_read(read_local,image+0x6cf1500,w->runtime,w->object,&registry[i]);
        if(ok) {w->agent=a.agent;w->initial_controller=a.controller;w->initial=a;}
    }
    if(ok && !transition_saved_distinct(local,count)) ok=0;
    AcquireSRWLockExclusive(&transition_lock);
    if(transition_closing || !stats.active || transition_saved_failures) ok=0;
    if(ok) ok=transition_prebind_bind_locked(local,count,registry);
    if(!ok && transition_prebind_enabled) transition_prebind_poison_locked(32);
    if(ok) {memcpy(transition_saved_watches,local,sizeof(local));transition_saved_bound=count;}
    else ++transition_saved_failures;
    ReleaseSRWLockExclusive(&transition_lock);transition_leave();return ok;
}
