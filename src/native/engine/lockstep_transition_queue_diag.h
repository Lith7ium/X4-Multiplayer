/* Optional copied-only queue evidence. These are observations, never accepted
 * tickets, allocation generations, native references or authority receipts. */
#define TRANSITION_DIAG_MAX 256u
#define TRANSITION_DIAG_SUFFIX "|PLAYER_TRANSITION_QUEUE_DIAG_1"
typedef struct {
    unsigned kind,boot,thread,bound,selection,watch,imports,id_generation,reads,decode;
    unsigned parent_kind,candidates,byte_match,recipient_reads,manager_reads,manager_match;
    uint64_t tick,runtime,saved,parent,order,source;
    uintptr_t command,agent,object,imported_object,bound_agent,caller,type,manager;
    unsigned char first[16],second[16];
} transition_diag_row;
static transition_diag_row transition_diag_rows[TRANSITION_DIAG_MAX];
static unsigned transition_diag_enabled,transition_diag_count,transition_diag_exported;
static uint64_t transition_diag_calls,transition_diag_unknown,transition_diag_unreadable,transition_diag_ambiguous,transition_diag_dropped;
static char transition_diag_digest[65],transition_diag_run[65];
static uintptr_t transition_diag_image,transition_diag_thunk;
static uint64_t transition_diag_started;
static unsigned transition_diag_port,transition_diag_timeout;

static int transition_diag_config(const char *run) {
    char preimage[384];uint64_t main_word=0;
    if(!run || strlen(run)!=64 || !hash_ok(secret,32,run)) return 0;
    int length=snprintf(preimage,sizeof(preimage),
        "SNAPSHOT_BYTES_1|7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8"
        "|PLAYER_GETUP_LOCAL_PLAN_1:diagnostic-getup-1:1:%s:1:30:120:0000000000011e12:00000000000ae79c" TRANSITION_DIAG_SUFFIX "%s",run,
        transition_prebind_enabled?TRANSITION_PREBIND_SUFFIX:"");
    if(length<0 || (size_t)length>=sizeof(preimage) || !hash_ok(preimage,(ULONG)length,initial_state_digest)) return 0;
    int ok=0;AcquireSRWLockExclusive(&transition_lock);
    if(!stats.active || !used || !image || !thunk || !read_local(image+WORD_RVA,&main_word,8) || main_word!=patched_word ||
       !boot_mode || GetCurrentThreadId()!=ui_thread || transition_closing ||
       boot_phase<0 || boot_phase>3 || stats.issued>120 || stats.slot!=1 || boot_options!=70377334178981ULL ||
       transition_saved_count!=1 || transition_saved_last!=120 ||
       strcmp(transition_saved_manifest,"2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player") ||
       transition_failures || transition_saved_failures || transition_saved_queue_failures || transition_dropped || transition_diag_dropped) goto done;
    if(transition_diag_enabled) {
        ok=transition_started && transition_enabled && transition_sites==7 && !transition_open &&
            transition_diag_image==(uintptr_t)image && transition_diag_thunk==(uintptr_t)thunk &&
            transition_diag_started==boot_started && transition_diag_port==remote_port && transition_diag_timeout==timeout_ms &&
            !strcmp(transition_diag_digest,initial_state_digest) && !strcmp(transition_diag_run,run);
        for(unsigned i=0;ok && i<3;i++) {uint64_t word=0;ok=read_local(image+transition_entries[i],&word,8) && word==transition_patched[i];}
        goto done;
    }
    if(boot_phase || stats.issued || transition_started || transition_saved_bound) goto done;
    memcpy(transition_diag_digest,initial_state_digest,65);memcpy(transition_diag_run,run,65);
    transition_diag_image=(uintptr_t)image;transition_diag_thunk=(uintptr_t)thunk;
    transition_diag_started=boot_started;transition_diag_port=remote_port;transition_diag_timeout=timeout_ms;transition_diag_enabled=1;ok=1;
done:ReleaseSRWLockExclusive(&transition_lock);return ok;
}
/* Read bits:1 type,2 first full copy,4 second full copy. Failed/partial reads
 * publish no bytes. Decode:1 model,2 other,3 type unreadable,4 outside image,
 * 5 first unreadable,6 second unreadable,7 changed,8 copied type changed,
 * 9 mode out of range. No after-original read belongs to this diagnostic. */
static void transition_diag_decode(transition_reader read,const void *command,transition_diag_row *r) {
    uintptr_t type=0,copied=0;unsigned mode=0;unsigned char bytes[16];
    if(!read(command,&type,8)) {r->decode=3;return;}
    r->type=type;r->reads=1;
    if(type<(uintptr_t)image || type>=(uintptr_t)(image+0x6fa9000)) {r->decode=4;return;}
    if(type!=(uintptr_t)(image+WORLD_QUEUE_MODEL_VTABLE)) {r->decode=2;return;}
    if(!read(command,bytes,16)) {r->decode=5;return;}
    memcpy(r->first,bytes,16);r->reads|=2;
    if(!read(command,bytes,16)) {r->decode=6;return;}
    memcpy(r->second,bytes,16);r->reads|=4;
    if(memcmp(r->first,r->second,16)) {r->decode=7;return;}
    memcpy(&copied,r->first,8);if(copied!=type) {r->decode=8;return;}
    memcpy(&mode,r->first+8,4);r->decode=mode<=6?1:9;
}
/* Caller holds transition_lock. These count completed capture outcomes, not
 * admission. Publish them atomically with row/unknown/dropped so active UI
 * snapshots never mistake a borrowed read in progress for lost evidence.
 * Existing transition_open covers that entire interval, including STOP. */
static void transition_diag_outcome(const transition_diag_row *r) {
    ++transition_diag_calls;
    if(r->kind==2 && r->recipient_reads!=2) ++transition_diag_unreadable;
    if(r->selection==2 || r->selection==3) ++transition_diag_ambiguous;
}
/* Separate selection never calls transition_saved_runtime/object, and hence
 * cannot alter their legacy prebind/unknown/failure counters or admission. */
static void transition_diag_capture(transition_reader read,unsigned kind,void *command,void *receiver,uint64_t runtime,uintptr_t caller) {
    if(!transition_diag_enabled) return;
    transition_diag_row r={0};r.kind=kind;r.command=(uintptr_t)command;r.runtime=runtime;r.caller=caller;
    r.tick=stats.issued;r.boot=(unsigned)boot_phase;r.thread=GetCurrentThreadId();
    unsigned recipient_reads=0;
    if(kind==2) {
        r.agent=(uintptr_t)receiver;
        uintptr_t object=0;uint64_t id=0;
        if(transition_read_offset(read,r.agent,0xef0,&object,8)) {
            recipient_reads=1;r.object=object;
            if(transition_read_offset(read,object,8,&id,8)) {recipient_reads=2;r.runtime=id;}
        }
        r.recipient_reads=recipient_reads;
    } else {
        uintptr_t expected=0;r.manager=(uintptr_t)receiver;
        if(read(image+0x6cf21c8,&expected,8)) {r.manager_reads=1;r.manager_match=expected==r.manager && expected!=0;}
    }
    AcquireSRWLockExclusive(&transition_lock);
    r.bound=transition_saved_bound!=0;
    unsigned matches=0;
    for(unsigned i=0;i<transition_saved_count;i++) if(r.runtime && transition_saved_watches[i].runtime==r.runtime) {
        transition_saved_watch *w=&transition_saved_watches[i];++matches;
        r.watch=i+1;r.imports=w->imports;r.saved=w->saved;r.imported_object=w->object;r.bound_agent=w->agent;
    }
    if(matches==1 && r.imports==1 && r.imported_object && !transition_saved_failures) {
        r.selection=1;r.id_generation=(unsigned)((r.runtime>>25)&0xffff);
        for(unsigned i=0;i<transition_saved_count;i++) if(i+1!=r.watch &&
            (transition_saved_watches[i].object==r.imported_object || transition_saved_watches[i].saved==r.saved)) r.selection=2;
        if(kind==2 && (recipient_reads!=2 || r.object!=r.imported_object)) r.selection=3;
    } else if(matches) r.selection=2;
    /* Unselected recipients have counters only. A prior command address is
     * never permission to dereference or associate an unknown recipient. */
    if(!matches) {transition_diag_outcome(&r);++transition_diag_unknown;ReleaseSRWLockExclusive(&transition_lock);return;}
    ReleaseSRWLockExclusive(&transition_lock);
    transition_scope *scope=transition_tls==TLS_OUT_OF_INDEXES?NULL:TlsGetValue(transition_tls);
    if(scope && scope->event && scope->event->thread==r.thread) {r.parent=transition_index(scope->event);r.parent_kind=scope->event->kind;}
    r.order=(uint64_t)InterlockedCompareExchange64(&transition_order,0,0);
    /* Ambiguous association does not authorize additional command reads. */
    if(r.selection==1) transition_diag_decode(read,command,&r);
    AcquireSRWLockExclusive(&transition_lock);
    transition_diag_outcome(&r);
    if(transition_diag_count>=TRANSITION_DIAG_MAX) ++transition_diag_dropped;
    else {
        if(kind==2) for(unsigned i=0;i<transition_diag_count;i++) {
            const transition_diag_row *q=&transition_diag_rows[i];
            if(q->kind==1 && q->command==r.command && q->runtime==r.runtime) {++r.candidates;r.source=i+1;}
        }
        if(r.candidates==1) {
            const transition_diag_row *q=&transition_diag_rows[r.source-1];
            r.byte_match=q->selection==1 && r.selection==1 && q->decode==1 && r.decode==1 && !memcmp(q->first,r.first,16);
        } else r.source=0;
        transition_diag_rows[transition_diag_count++]=r;
    }
    ReleaseSRWLockExclusive(&transition_lock);
}
