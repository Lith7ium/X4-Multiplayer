/* Opt-in ordering experiment. Move the native publication from before the
 * MoveWorker admission wait to after admission, then join its next native pass
 * before pathing/AI can advance. No contact or event payload is synthesized. */
#include <tlhelp32.h>
#define GRAVIDAR_WORD 0xef45d8u
#define GRAVIDAR_PUBLISH 0x10b12c0u
#define GRAVIDAR_UNIT_ENTRY 0x2c67648u
#define GRAVIDAR_UNIT_APPLY 0x10acd70u
typedef void (*gravidar_publish_fn)(void *,unsigned char);
typedef void (*gravidar_unit_fn)(void *,void *,void *);
#define GRAVIDAR_CONTACT_LIMIT 256u
typedef struct {unsigned present,count;uint64_t ids[GRAVIDAR_CONTACT_LIMIT];} gravidar_contacts;
/* Read-only copies of old/input/applied primary and secondary sets, followed by
 * the two native added-contact sets. Raw IDs are local diagnostics, not hashes. */
typedef struct {
    uint64_t tick,calls,time,before[3],after[3],rng_before,rng_after;
    gravidar_contacts contacts[8];
} gravidar_sample;
static gravidar_sample gravidar_samples[ACTOR_CAPACITY];
static uint64_t gravidar_original_word,gravidar_patched_word,gravidar_frames,gravidar_waits,gravidar_timeouts,gravidar_calls;
static volatile LONG64 gravidar_failures;
static uint64_t gravidar_contacts_truncated;
static volatile LONG gravidar_in_frame;
static int gravidar_installed,gravidar_unit_installed,gravidar_restored;
static int gravidar_seeded,gravidar_initial;
static void *gravidar_initialized[ACTOR_CAPACITY];
static uint64_t gravidar_initial_before[ACTOR_CAPACITY][3],gravidar_initial_after[ACTOR_CAPACITY][3];
static unsigned gravidar_inits;
static int gravidar_read_contacts(const void *vector,gravidar_contacts *out) {
    uintptr_t bounds[3];memset(out,0,sizeof(*out));
    if(!vector) return 1; /* Optional secondary vector is absent. */
    if(!read_local(vector,bounds,sizeof(bounds)) || bounds[1]<bounds[0] || bounds[2]<bounds[1] ||
       (bounds[0]&7) || ((bounds[1]-bounds[0])&7) || ((bounds[2]-bounds[0])&7) ||
       (!bounds[0] && (bounds[1] || bounds[2]))) return 0;
    /* Rows of a watched unit: a crowded one is shown truncated, it does not stop the gate. */
    uint64_t real=(bounds[1]-bounds[0])/8;
    if(real>GRAVIDAR_CONTACT_LIMIT) ++gravidar_contacts_truncated;
    unsigned count=real>GRAVIDAR_CONTACT_LIMIT?GRAVIDAR_CONTACT_LIMIT:(unsigned)real;
    if(count && !read_local((void*)bounds[0],out->ids,count*8)) return 0;
    for(unsigned i=0;i<count;i++) if(!out->ids[i]) return 0;
    out->present=1;out->count=count;return 1;
}
static int gravidar_snapshot_contacts(gravidar_sample *s,void *unit,void *result,int after) {
    unsigned char *u=unit,*r=result;void *secondary=NULL,*added=NULL;
    /* The native apply function uses this same lock for these vectors. It is
     * released before calling the original; the observer never changes a set. */
    EnterCriticalSection((CRITICAL_SECTION*)(u+0x18));
    int ok=read_local(u+0x98,&secondary,8) &&
        gravidar_read_contacts(u+0x68,&s->contacts[after?4:0]) &&
        gravidar_read_contacts(secondary,&s->contacts[after?5:1]);
    if(after) ok=ok && read_local(u+0xa0,&added,8) &&
        gravidar_read_contacts(u+0x80,&s->contacts[6]) &&
        gravidar_read_contacts(added,&s->contacts[7]);
    LeaveCriticalSection((CRITICAL_SECTION*)(u+0x18));
    if(!after) ok=ok && result && gravidar_read_contacts(r+0x10,&s->contacts[2]) &&
        gravidar_read_contacts(r+0x28,&s->contacts[3]);
    return ok;
}
static int gravidar_initialize_unit(unsigned actor,void *unit,double time) {
    void *vtable;uint64_t id;
    if(!actor_is_ship(actor) || !isfinite(time) || !read_local(unit,&vtable,8) || vtable!=image+0x2c67610 ||
       !read_local((unsigned char*)unit+8,&id,8) || !id || id!=actor_ids[actor-1]) return 0;
    if(gravidar_initialized[actor-1]) return gravidar_initialized[actor-1]==unit;
    EnterCriticalSection((CRITICAL_SECTION*)((unsigned char*)unit+0x18));
    memcpy(gravidar_initial_before[actor-1],(unsigned char*)unit+0xa8,24);
    double times[3]={time-gate_tick_dt(),time-gate_tick_dt(),time};
    memcpy((unsigned char*)unit+0xa8,times,24);memcpy(gravidar_initial_after[actor-1],times,24);
    LeaveCriticalSection((CRITICAL_SECTION*)((unsigned char*)unit+0x18));
    gravidar_initialized[actor-1]=unit;++gravidar_inits;return 1;
}
static int gravidar_register(unsigned actor) {
    if(!gravidar_initial || gravidar_initialized[actor-1]) return 1;
    void *worker=*(void**)(image+0x6cf1650);void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    double time;if(!worker || !tls || !tls[0] || GetCurrentThreadId()!=ui_thread ||
        !read_local((unsigned char*)tls[0]+0x300,&time,8) || !isfinite(time)) return 0;
    unsigned char *w=worker;
    EnterCriticalSection((CRITICAL_SECTION*)(w+0x6960));
    int ready=w[0x6988]==1;
    LeaveCriticalSection((CRITICAL_SECTION*)(w+0x6960));
    if(!ready) return 0;
    uint64_t id=actor_ids[actor-1];CRITICAL_SECTION *bucket=(CRITICAL_SECTION*)(w+0x88+(id&255)*0x28);
    EnterCriticalSection(bucket);
    typedef void *(*lookup_fn)(void *,uint64_t);
    void *unit=((lookup_fn)(image+0x10b2dc0))(worker,id);
    int ok=unit && gravidar_initialize_unit(actor,unit,time);
    LeaveCriticalSection(bucket);return ok;
}
static int gravidar_wait(void *worker,unsigned timeout) {
    unsigned char *w=worker;void *vtable;uint32_t running,enabled;ULONGLONG started=GetTickCount64();int waited=0;
    for(;;) {
        if(!read_local(w,&vtable,8) || vtable!=image+0x2c68860 ||
           !read_local(w+0x40,&running,4) || running!=2 || !read_local(w+0x44,&enabled,4) || enabled!=1) return 0;
        EnterCriticalSection((CRITICAL_SECTION*)(w+0x6960));
        unsigned char ready=*(unsigned char*)(w+0x6988);
        LeaveCriticalSection((CRITICAL_SECTION*)(w+0x6960));
        if(ready==1) return 1;if(ready!=0) return 0;
        if(!waited) {++gravidar_waits;waited=1;}
        if(GetTickCount64()-started>=timeout) {++gravidar_timeouts;return 0;}
        Sleep(1);
    }
}
static volatile LONG gravidar_key_all,gravidar_time_sync;
static uint64_t *volatile gravidar_thread_rng;
static unsigned char *gravidar_thread_slot;
static uint64_t gravidar_time_syncs,gravidar_time_stale;
static volatile LONG64 gravidar_time_failures;
#define GRAVIDAR_TIME_LOCK 0x6b60e10u
#define GRAVIDAR_TIME_GLOBAL 0x6b60e38u
/* R01: identical Gravidar events carried due 7.0997 on one client and 7.083
 * (load time) on the other. Every thread keeps its own game-time copy at TLS
 * slot0+0x300, refreshed by 0x14b8e20 from the global under its lock; the
 * Gravidar thread refreshed at different moments. Find the thread by its X4
 * name once, then refresh its copy the same way before each gated frame. */
typedef struct {LONG status;void *teb;uintptr_t process,thread,affinity;LONG priority,base;} thread_basic_info;
_Static_assert(sizeof(thread_basic_info)==48,"THREAD_BASIC_INFORMATION layout");
static unsigned char *gravidar_find_slot(void) {
    typedef LONG (WINAPI *query_fn)(HANDLE,int,void *,ULONG,ULONG *);typedef LONG (WINAPI *name_fn)(HANDLE,PWSTR *);
    query_fn query=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    name_fn describe=(name_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription");
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(!query || !describe || snap==INVALID_HANDLE_VALUE) return NULL;
    unsigned char *found=NULL;unsigned matches=0;THREADENTRY32 entry;entry.dwSize=sizeof(entry);
    for(BOOL ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)) {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId()) continue;
        HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);if(!thread) continue;
        PWSTR name=NULL;thread_basic_info basic; /* THREAD_BASIC_INFORMATION: exactly 48 bytes */
        if(describe(thread,&name)>=0 && name && !wcscmp(name,L"Gravidar") && query(thread,0,&basic,sizeof(basic),NULL)>=0) {
            void **slots=NULL,*slot=NULL;
            if(read_local((unsigned char*)basic.teb+0x58,&slots,8) && slots && read_local(slots,&slot,8) && slot) {found=slot;++matches;}
        }
        if(name) LocalFree(name);
        CloseHandle(thread);
    }
    CloseHandle(snap);return matches==1?found:NULL;
}
static int gravidar_frame(gravidar_publish_fn original,void *worker,unsigned timeout) {
    if(!gravidar_wait(worker,timeout)) return 0;
    if((gravidar_key_all || gravidar_time_sync) && !gravidar_thread_slot) {
        gravidar_thread_slot=gravidar_find_slot();
        if(!gravidar_thread_slot) {InterlockedIncrement64(&gravidar_time_failures);return 0;}
        if(!gravidar_thread_rng) gravidar_thread_rng=(uint64_t*)(gravidar_thread_slot+0x318);
    }
    /* The worker reported ready: its thread is idle, so its TLS copies can be written. */
    if(gravidar_key_all && gravidar_thread_rng) *gravidar_thread_rng=lockstep_seed(0x4752415646524d45ULL,stats.issued,0,0,0,0);
    if(gravidar_time_sync) {
        CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+GRAVIDAR_TIME_LOCK);double now,before;
        EnterCriticalSection(lock);now=*(double*)(image+GRAVIDAR_TIME_GLOBAL);LeaveCriticalSection(lock);
        memcpy(&before,gravidar_thread_slot+0x300,8);
        if(!isfinite(now) || now<before) {InterlockedIncrement64(&gravidar_time_failures);return 0;}
        if(before!=now) ++gravidar_time_stale;
        memcpy(gravidar_thread_slot+0x328,&before,8);memcpy(gravidar_thread_slot+0x300,&now,8);++gravidar_time_syncs;
    }
    LONG previous=InterlockedExchange(&gravidar_in_frame,1);
    int ok=0;
    __try {
        original(worker,0);++gravidar_frames;
        ok=gravidar_wait(worker,timeout);
    } __finally {
        DWORD error=GetLastError();
        InterlockedExchange(&gravidar_in_frame,previous);
        if(AbnormalTermination()) InterlockedIncrement64(&gravidar_failures);
        SetLastError(error);
    }
    return ok;
}
static uint64_t gravidar_boot_skipped,gravidar_boot_resets,gravidar_boot_drains;
/* T06: a Gravidar pass already in flight when the gate admitted frame 1 still
 * posted load-time scan events partially before or after tick 1. Wait for the
 * worker to go idle once, when boot phase 3 is entered, before frame 1 runs. */
static int gravidar_boot_drain(void) {
    void *worker=*(void**)(image+0x6cf1650);
    if(!worker) return 0;
    int ok=gravidar_wait(worker,5000);if(ok) ++gravidar_boot_drains;
    return ok;
}
static void gravidar_defer(void *worker,unsigned char reset) {
    if(stats.active && boot_phase==3) {
        if(reset) InterlockedIncrement64(&gravidar_failures);
        return;
    }
    /* T03/T04: an ungated Gravidar pass while the load completes raced the first
     * gated frame, so one client posted its scan events at the load time and the
     * other at tick 1 (T07: also from phase 1, after the save time is applied but
     * before the gate sees loading). With time sync armed, the first pass waits. */
    if(gravidar_time_sync && stats.active && boot_mode && (boot_phase==1 || boot_phase==2)) {
        if(!reset) {++gravidar_boot_skipped;return;}
        ++gravidar_boot_resets;
    }
    ((gravidar_publish_fn)(image+GRAVIDAR_PUBLISH))(worker,reset);
}
static unsigned gravidar_actor(void *unit) {
    uint64_t id,actual;
    if(!read_local((unsigned char*)unit+8,&id,8) || !id) return 0;
    for(unsigned i=0;i<ACTOR_CAPACITY;i++) if(id==actor_ids[i] && actor_components[i]) {
        if(!read_local((unsigned char*)actor_components[i]+8,&actual,8) || id!=actual) {
            InterlockedIncrement64(&gravidar_failures);return 0;
        }
        return i+1;
    }
    return 0;
}
static void gravidar_execute(gravidar_unit_fn original,gravidar_sample *s,unsigned actor,uint64_t *rng,void *unit,void *queue,void *result) {
    uint64_t saved=*rng;
    const int restore_rng=gravidar_seeded;
    __try {
        if(restore_rng) *rng=actor_seed(0x5241444152554e49ULL,stats.issued,actor,0,s->calls,0);
        s->rng_before=*rng;original(unit,queue,result);s->rng_after=*rng;
    } __finally {
        DWORD error=GetLastError();
        if(restore_rng) *rng=saved;
        if(AbnormalTermination()) InterlockedIncrement64(&gravidar_failures);
        SetLastError(error);
    }
}
/* Census option, default off: K02 found the Gravidar thread making ~1,000
 * draws per frame from an unseeded per-thread stream (equal counts, different
 * words). Key every unit applied inside a gated Gravidar frame by its portable
 * saved ID and tick; restore the thread's word afterwards. Units without a
 * saved ID keep the natural stream and are counted as unkeyed; with
 * native_gate_portable_created a created unit is keyed by its created ID
 * (counted keyed and created), so its draws no longer depend on the order in
 * which the pass reaches the remaining unkeyed units. */
static volatile LONG64 gravidar_keyed,gravidar_unkeyed,gravidar_key_failures,gravidar_key_created;
/* The unit's portable key: saved ID, created ID (portable keys on) or 0. */
static uint64_t gravidar_unit_key(uint64_t runtime,unsigned *klass) {return world_identity_key(runtime,klass);}
/* Read-only census diagnostic, DEFAULT OFF (native_gate_gravidar_watch).
 * sc-missions (29 Oct): at tick 644 ship 9fb3b got event_object_enemy_found on
 * one client and event_gravidar_has_scanned on the other (decoded from the
 * script RNG event keys), and 9f121's has_scanned interrupt fired at 328 vs
 * 420: the units' Gravidar contact sets differed while every imported and
 * created object still agreed. For up to GRAVIDAR_WATCH_MAX watched saved IDs,
 * each keyed unit apply inside a gated Gravidar frame records the unit's sets
 * before the call (old primary/secondary, scan result primary/secondary) and
 * after it (applied primary/secondary, the two added sets: what raises the
 * found/scanned events). A contact is written as its portable key (saved or
 * created) or, without one, as its runtime ID marked unmapped: such objects
 * (temporary components, mass traffic) cannot be joined across clients, only
 * counted. Sets are sorted. Nothing is written to the game. */
#define GRAVIDAR_WATCH_MAX 8u
#define GRAVIDAR_WATCH_RING 128u
enum {GRAVIDAR_WATCH_SAVED=0,GRAVIDAR_WATCH_CREATED=1,GRAVIDAR_WATCH_UNMAPPED=2};
typedef struct {unsigned present,count;uint64_t ids[GRAVIDAR_CONTACT_LIMIT];uint8_t klass[GRAVIDAR_CONTACT_LIMIT];} gravidar_watch_set_t;
typedef struct {uint64_t tick,unit,ordinal;unsigned ok;gravidar_watch_set_t sets[8];} gravidar_watch_row_t;
static uint64_t gravidar_watch_ids[GRAVIDAR_WATCH_MAX];
static volatile LONG gravidar_watch_count;
static gravidar_watch_row_t gravidar_watch_ring[GRAVIDAR_WATCH_RING];
static gravidar_sample gravidar_watch_scratch;
static volatile LONG gravidar_watch_busy;
static volatile LONG64 gravidar_watch_cursor,gravidar_watch_failures,gravidar_watch_unmapped;
static uint64_t gravidar_watch_last_tick,gravidar_watch_last_unit,gravidar_watch_last_ordinal;
/* slot is the 1-based position in the probe's list. The loading UI reloads the
 * probe, which repeats the identical configuration before frame 1 (sc-missions2
 * refused that second pass): a slot already armed accepts only its own ID, the
 * next free slot appends, anything else refuses. Saved IDs are compared at apply
 * time through the world identity map, so no lookup is needed while arming. */
static int gravidar_watch_add(const char *text,size_t length,unsigned slot) {
    LONG count=gravidar_watch_count;
    if(!text || length!=16 || stats.issued || GetCurrentThreadId()!=ui_thread || !slot || slot>GRAVIDAR_WATCH_MAX || slot>(unsigned)count+1) return 0;
    uint64_t saved=0;
    for(size_t i=0;i<length;i++) {
        unsigned digit=(unsigned)(text[i]>='0' && text[i]<='9'?text[i]-'0':text[i]>='a' && text[i]<='f'?text[i]-'a'+10:16);
        if(digit>15) return 0;saved=(saved<<4)|digit;
    }
    if(!saved) return 0;
    if(slot<=(unsigned)count) return gravidar_watch_ids[slot-1]==saved;
    for(LONG i=0;i<count;i++) if(gravidar_watch_ids[i]==saved) return 0;
    gravidar_watch_ids[count]=saved;InterlockedIncrement(&gravidar_watch_count);return 1;
}
static int gravidar_watched(uint64_t saved) {
    LONG n=gravidar_watch_count;
    for(LONG i=0;i<n;i++) if(gravidar_watch_ids[i]==saved) return 1;
    return 0;
}
static int gravidar_watch_compare(const void *a,const void *b) {
    const uint64_t *x=a,*y=b;
    if(x[0]!=y[0]) return x[0]<y[0]?-1:1;
    return x[1]<y[1]?-1:x[1]>y[1];
}
static void gravidar_watch_map(const gravidar_contacts *in,gravidar_watch_set_t *out) {
    uint64_t pairs[GRAVIDAR_CONTACT_LIMIT][2];
    out->present=in->present;out->count=in->count<=GRAVIDAR_CONTACT_LIMIT?in->count:GRAVIDAR_CONTACT_LIMIT;
    for(unsigned i=0;i<out->count;i++) {
        unsigned klass=WORLD_KEY_NONE;uint64_t key=world_identity_key(in->ids[i],&klass);
        if(key) {pairs[i][0]=klass==WORLD_KEY_CREATED?GRAVIDAR_WATCH_CREATED:GRAVIDAR_WATCH_SAVED;pairs[i][1]=key;}
        else {pairs[i][0]=GRAVIDAR_WATCH_UNMAPPED;pairs[i][1]=in->ids[i];InterlockedIncrement64(&gravidar_watch_unmapped);}
    }
    qsort(pairs,out->count,sizeof(pairs[0]),gravidar_watch_compare);
    for(unsigned i=0;i<out->count;i++) {out->klass[i]=(uint8_t)pairs[i][0];out->ids[i]=pairs[i][1];}
}
/* Called on the Gravidar thread around one keyed apply; ok=0 marks a failed read. */
static void gravidar_watch_record(uint64_t saved,int ok) {
    if(stats.issued!=gravidar_watch_last_tick || saved!=gravidar_watch_last_unit) gravidar_watch_last_ordinal=0;
    gravidar_watch_last_tick=stats.issued;gravidar_watch_last_unit=saved;++gravidar_watch_last_ordinal;
    LONG64 index=InterlockedCompareExchange64(&gravidar_watch_cursor,0,0);
    gravidar_watch_row_t *r=&gravidar_watch_ring[index%GRAVIDAR_WATCH_RING];
    r->tick=stats.issued;r->unit=saved;r->ordinal=gravidar_watch_last_ordinal;r->ok=(unsigned)ok;
    for(unsigned k=0;k<8;k++) {
        if(ok) gravidar_watch_map(&gravidar_watch_scratch.contacts[k],&r->sets[k]);
        else {r->sets[k].present=0;r->sets[k].count=0;}
    }
    if(!ok) InterlockedIncrement64(&gravidar_watch_failures);
    InterlockedIncrement64(&gravidar_watch_cursor);
}
static int gravidar_watch_row_text(uint64_t index,char *out,size_t capacity) {
    LONG64 cursor=InterlockedCompareExchange64(&gravidar_watch_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    if(cursor>(LONG64)GRAVIDAR_WATCH_RING && index<(uint64_t)(cursor-(LONG64)GRAVIDAR_WATCH_RING)) {
        int n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);return n>0 && (size_t)n<capacity?n:0;
    }
    const gravidar_watch_row_t *r=&gravidar_watch_ring[index%GRAVIDAR_WATCH_RING];
    int n=snprintf(out,capacity,"index=%llu tick=%llu unit=%016llx ord=%llu ok=%u counts=%u/%u/%u/%u/%u/%u/%u/%u",
        (unsigned long long)index,(unsigned long long)r->tick,(unsigned long long)r->unit,(unsigned long long)r->ordinal,r->ok,
        r->sets[0].count,r->sets[1].count,r->sets[2].count,r->sets[3].count,r->sets[4].count,r->sets[5].count,r->sets[6].count,r->sets[7].count);
    return n>0 && (size_t)n<capacity?n:0;
}
/* One set as s:/c:/u: keys (u: runtime IDs are local to this client). */
static int gravidar_watch_set_text(uint64_t index,unsigned set,char *out,size_t capacity) {
    static const char kinds[3]={'s','c','u'};
    LONG64 cursor=InterlockedCompareExchange64(&gravidar_watch_cursor,0,0);
    if(set>7 || (LONG64)index>=cursor || (cursor>(LONG64)GRAVIDAR_WATCH_RING && index<(uint64_t)(cursor-(LONG64)GRAVIDAR_WATCH_RING))) return 0;
    const gravidar_watch_set_t *s=&gravidar_watch_ring[index%GRAVIDAR_WATCH_RING].sets[set];
    int n=snprintf(out,capacity,"index=%llu set=%u present=%u count=%u keys=",(unsigned long long)index,set,s->present,s->count);
    if(n<=0 || (size_t)n>=capacity) return 0;
    size_t used=(size_t)n;
    for(unsigned i=0;i<s->count;i++) {
        int m=snprintf(out+used,capacity-used,"%s%c:%016llx",i?",":"",kinds[s->klass[i]<3?s->klass[i]:2],(unsigned long long)s->ids[i]);
        if(m<=0 || (size_t)m>=capacity-used) return 0;
        used+=(size_t)m;
    }
    return (int)used;
}
static int gravidar_watch_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"watched=%ld rows=%lld evicted=%lld failures=%lld unmapped=%lld",gravidar_watch_count,
        (long long)gravidar_watch_cursor,(long long)(gravidar_watch_cursor>(LONG64)GRAVIDAR_WATCH_RING?gravidar_watch_cursor-(LONG64)GRAVIDAR_WATCH_RING:0),
        (long long)gravidar_watch_failures,(long long)gravidar_watch_unmapped);
    return n>0 && (size_t)n<capacity?n:0;
}
static void gravidar_key_unit(gravidar_unit_fn original,void *unit,void *queue,void *result) {
    uint64_t runtime=0,saved=0;unsigned klass=WORLD_KEY_NONE;void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!read_local((unsigned char*)unit+8,&runtime,8) || !(saved=gravidar_unit_key(runtime,&klass)) || !tls || !tls[0]) {
        InterlockedIncrement64(&gravidar_unkeyed);original(unit,queue,result);return;
    }
    if(klass==WORLD_KEY_CREATED) InterlockedIncrement64(&gravidar_key_created);
    uint64_t *rng=(uint64_t*)((unsigned char*)tls[0]+0x318),prior=*rng;
    if(!gravidar_thread_rng) gravidar_thread_rng=rng;else if(gravidar_thread_rng!=rng) InterlockedIncrement64(&gravidar_key_failures);
    /* Watched saved unit: sets before the call (read-only, the unit's own lock). */
    int watch=klass==WORLD_KEY_SAVED && gravidar_watch_count && gravidar_watched(saved) && !InterlockedExchange(&gravidar_watch_busy,1);
    int watch_ok=watch && gravidar_snapshot_contacts(&gravidar_watch_scratch,unit,result,0);
    __try {
        *rng=lockstep_seed(0x4752415649444152ULL,stats.issued,saved,0,0,0);
        original(unit,queue,result);InterlockedIncrement64(&gravidar_keyed);
    } __finally {
        *rng=prior;if(AbnormalTermination()) InterlockedIncrement64(&gravidar_key_failures);
        if(watch && AbnormalTermination()) {InterlockedIncrement64(&gravidar_watch_failures);InterlockedExchange(&gravidar_watch_busy,0);}
    }
    gpost_note(unit,result,saved,klass); /* census-zone: has_scanned contact changes, lockstep_gravidar_posts.h */
    if(watch) {
        watch_ok=watch_ok && gravidar_snapshot_contacts(&gravidar_watch_scratch,unit,result,1);
        gravidar_watch_record(saved,watch_ok);InterlockedExchange(&gravidar_watch_busy,0);
    }
}
static int gravidar_key_arm(void) {
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return gravidar_key_all!=0;
    InterlockedExchange(&gravidar_key_all,1);return 1;
}
static int gravidar_time_arm(void) {
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return gravidar_time_sync!=0;
    if(!hash_ok(image+0x14b8e20,101,"e416f11fb3a8d1f3d6c4e40995a2f57582064bd6e68539f636f96c2f3b6c3020")) return 0;
    InterlockedExchange(&gravidar_time_sync,1);return 1;
}
static int gravidar_key_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld time_sync=%ld installed=%d frames=%llu keyed=%lld unkeyed=%lld time_syncs=%llu time_stale=%llu "
        "time_failures=%lld boot_skipped=%llu boot_resets=%llu boot_drains=%llu failures=%lld created=%lld",gravidar_key_all,gravidar_time_sync,gravidar_unit_installed,(unsigned long long)gravidar_frames,
        (long long)gravidar_keyed,(long long)gravidar_unkeyed,(unsigned long long)gravidar_time_syncs,(unsigned long long)gravidar_time_stale,
        (long long)gravidar_time_failures,(unsigned long long)gravidar_boot_skipped,(unsigned long long)gravidar_boot_resets,(unsigned long long)gravidar_boot_drains,(long long)gravidar_key_failures,
        (long long)gravidar_key_created);
    return n>0 && (size_t)n<capacity?n:0;
}
static void gravidar_unit(void *unit,void *queue,void *result) {
    uint64_t registry_runtime;
    if(registry_is_observing() && read_local((unsigned char*)unit+8,&registry_runtime,8)) registry_observe_runtime(registry_runtime,REGISTRY_GRAVIDAR);
    gravidar_unit_fn original=(gravidar_unit_fn)(image+GRAVIDAR_UNIT_APPLY);
    unsigned actor=gravidar_in_frame?gravidar_actor(unit):0;
    if(!actor) {
        if(gravidar_key_all && gravidar_in_frame && stats.active && boot_phase==3) gravidar_key_unit(original,unit,queue,result);
        else original(unit,queue,result);
        return;
    }
    if(gravidar_initial && gravidar_initialized[actor-1]!=unit) {
        InterlockedIncrement64(&gravidar_failures);original(unit,queue,result);return;
    }
    gravidar_sample *s=&gravidar_samples[actor-1];
    if(s->tick!=stats.issued) {s->tick=stats.issued;s->calls=0;}++s->calls;++gravidar_calls;
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    if(!tls || !tls[0] || !read_local((unsigned char*)tls[0]+0x300,&s->time,8) ||
       !read_local((unsigned char*)tls[0]+0x318,&s->rng_before,8) ||
       !read_local((unsigned char*)unit+0xa8,s->before,24) ||
       !gravidar_snapshot_contacts(s,unit,result,0)) {
        InterlockedIncrement64(&gravidar_failures);original(unit,queue,result);return;
    }
    gravidar_execute(original,s,actor,(uint64_t*)((unsigned char*)tls[0]+0x318),unit,queue,result);
    if(!read_local((unsigned char*)unit+0xa8,s->after,24) ||
       !gravidar_snapshot_contacts(s,unit,result,1))
        InterlockedIncrement64(&gravidar_failures);
}
static int gravidar_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+GRAVIDAR_WORD,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+GRAVIDAR_WORD),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+GRAVIDAR_WORD,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+GRAVIDAR_WORD,8);return actual==expected;
}
static int gravidar_unit_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+GRAVIDAR_UNIT_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((PVOID volatile*)(image+GRAVIDAR_UNIT_ENTRY),value,expected);
    VirtualProtect(image+GRAVIDAR_UNIT_ENTRY,8,old,&ignored);return actual==expected;
}
static void gravidar_restore(void) {
    int owned=gravidar_installed || gravidar_unit_installed;
    if(gravidar_installed && gravidar_swap(gravidar_patched_word,gravidar_original_word)) gravidar_installed=0;
    if(gravidar_unit_installed && gravidar_unit_swap((void*)gravidar_unit,image+GRAVIDAR_UNIT_APPLY)) gravidar_unit_installed=0;
    if(owned) gravidar_restored=!gravidar_installed && !gravidar_unit_installed;
}
static int gravidar_install(int seed,int initial) {
    if(!boot_mode || !(path_options&1) || !radar_installed || (initial && !seed) ||
       (initial && !hash_ok(image+0x10b2dc0,181,"43ced4fa9f55c0b96d47b6f3252a10fe3fcf3c7d6b9d95042f242ff9d9671840")) ||
       !hash_ok(image+0xef45d0,32,"4aa7f759764741aba58f7f43083af1205437074ae9821d0793d99e20c735e1f8") ||
       !hash_ok(image+GRAVIDAR_PUBLISH,317,"89362a2325ff0149e2e41e08628ad92eb2405e592c3dfc6f3e3e148667399daa") ||
       !hash_ok(image+0x10ae0e0,4686,"7fdbcc2998badaec41dea9b363938be23b24be94c881eb74c203cfd899be342c") ||
       !hash_ok(image+GRAVIDAR_UNIT_APPLY,2262,"a1f8ae33a9b8c9494c231878651d7a146767b3a44b5fd94f651afcafc04ab6df")) return 0;
    intptr_t distance=(intptr_t)(thunk+704)-(intptr_t)(image+0xef45de);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    if(!thunk_slot_ready(704)) return 0;
    memcpy(&gravidar_original_word,image+GRAVIDAR_WORD,8);gravidar_patched_word=gravidar_original_word;
    int32_t relative=(int32_t)distance;memcpy((unsigned char*)&gravidar_patched_word+2,&relative,4);
    if(!gravidar_swap(gravidar_original_word,gravidar_patched_word)) return 0;
    gravidar_installed=1;
    if(!gravidar_unit_swap(image+GRAVIDAR_UNIT_APPLY,(void*)gravidar_unit)) {gravidar_restore();return 0;}
    gravidar_seeded=seed;gravidar_initial=initial;gravidar_unit_installed=1;return 1;
}
