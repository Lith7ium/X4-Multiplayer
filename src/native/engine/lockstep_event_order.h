/* Census experiment, DEFAULT OFF: portable delivery order of due events.
 * The schedule g_EventSourceSchedule 0x2f6a9c0 is a unique red-black set of
 * (earliest pending time, EventSource pointer) nodes (+0x20 double, +0x28
 * source; header/end 0x2f6a9c0, root +8, free list +0x10, begin +0x20, size
 * +0x28). Insert 0xa6e6f0, erase-by-key 0xa46f90 and the three inline copies
 * in ProcessDueEvents 0x99bb50 (erase 0x99c0b0/0x99c1a4, insert 0x99c2b4) all
 * compare time with comisd and break ties on the unsigned source address.
 * Per-source events live in a time multimap at source+0x20 (FIFO on equal
 * times). ProcessDueEvents walks the set from begin, copies every due event of
 * each visited source into the static batch 0x39943e0 (0x38-byte slots: +0x18
 * source, +0x28 event, +0x30 time; count 0x38f4bc8, cap 0x10000), repairs the
 * set, then delivers the batch in slot order through DeliverEvent 0x99c470.
 * Equal-time sources are therefore delivered in heap-address order.
 * The set and its comparator are never changed: only the delivery call at
 * 0x99c3d1 is redirected (rel32 inside one aligned qword CAS, instruction
 * boundaries unchanged) through THUNK_JUMP 1984. The first call of a pass
 * reorders the undelivered suffix by source run on (first time, saved ID, else
 * runtime ID after all saved ones, address, native slot); events inside a run
 * keep native order.
 * Each entry is still delivered exactly once, from the slot the native loop is
 * about to release. Sources without a saved identity are ordered by runtime ID
 * and counted as local: their order is portable only if runtime IDs are.
 * With native_gate_portable_created a source with a created identity is class
 * 'created' and ranks after every saved source and before every local one, by
 * created ID (lockstep_world_identity.h); saved and local runs keep their class
 * values, so their order and the order hash are unchanged when it is off.
 * View-local sources: U::UIElement components (vtable 0x2b6cc38; class 105,
 * which the created identities also exclude) are made and killed by each
 * client's UI (the HUD target elements, UIElementList), so their events differ
 * per client by design (cam-C1c: client 2's camera, anchored elsewhere, killed
 * six of them at tick 15 at once, client 1 one by one at 22, 23 and 37). Their
 * runs are still delivered in place, but they are left out of the order hash
 * and of local=, ties= and local_ties= (ties are taken between the remaining
 * runs) and counted as view_runs= / view_entries= instead. */
#define EVENT_ORDER_STRIDE 0x38u
#define EVENT_ORDER_CAPACITY 65536u
/* The Linux gate (linux/gate_event_order.h) defines these for its executable, and its own patch. */
#ifndef EVENT_ORDER_OWN_PLATFORM
#define EVENT_ORDER_SITE 0x99c3d0u
#define EVENT_ORDER_RETURN 0x99c3d6u
#define EVENT_ORDER_DELIVER 0x99c470u
#define EVENT_ORDER_BATCH 0x39943e0u
#define EVENT_ORDER_COUNT 0x38f4bc8u
#define EVENT_ORDER_THUNK 1984u
/* 10 | e8 rel32(DeliverEvent) | 48 8d: the call and its neighbours' bytes. */
#define EVENT_ORDER_NATIVE_WORD 0x8d480000009ae810ULL
#define EVENT_ORDER_VIEW_LOCAL 0x2b6cc38u /* U::UIElement::vftable */
#endif
typedef void (*event_order_deliver_fn)(void *,void *,double);
typedef struct {uint64_t time,id;unsigned char *source;unsigned first,length,klass;uint32_t type;} event_order_run;
typedef struct {unsigned char *source,*event;uint64_t time;} event_order_entry;
typedef struct {void *event;uint64_t time,end;} event_order_mark;
static event_order_run event_order_runs[EVENT_ORDER_CAPACITY];
static event_order_entry event_order_entries[EVENT_ORDER_CAPACITY];
static event_order_mark event_order_marks[EVENT_ORDER_CAPACITY];
static volatile LONG event_order_armed;
static int event_order_installed,event_order_restored;
static uint64_t event_order_original_word,event_order_patched_word,event_order_next,event_order_marked;
static uint64_t event_order_calls,event_order_passes,event_order_boot_passes,event_order_total,event_order_run_total;
static uint64_t event_order_local,event_order_ties,event_order_local_ties,event_order_collisions,event_order_moved,event_order_created;
static uint64_t event_order_saturated,event_order_hash=0xcbf29ce484222325ULL,event_order_view_runs,event_order_view_entries;
static unsigned event_order_last_count,event_order_last_runs;
static volatile LONG64 event_order_failures;
static const char *event_order_stage="idle";
/* Census rows (native_gate_event_rows): one per collected batch entry, written
 * when its pass is ordered, during boot passes and gated ticks <= the limit.
 * Same population as entries=; slots a dying source clears later are kept. */
#define EVENT_ORDER_ROWS 65536u
typedef struct {uint64_t id,due,now;uint32_t pass,slot,tick,source_type,event_type;uint8_t phase,klass;} event_order_row_t;
static event_order_row_t event_order_ring[EVENT_ORDER_ROWS];
static volatile LONG64 event_order_row_cursor;
static volatile LONG event_order_rows_armed;
static unsigned event_order_rows_ticks;
static uint64_t event_order_row_faults;
/* Vtable pointer as an image RVA; no virtual call is made. Foreign: ffffffff. */
static uint32_t event_order_rva(uint64_t pointer) {
    uintptr_t base=(uintptr_t)image;
    return base && pointer>=base && pointer<base+0x6fa9000u?(uint32_t)(pointer-base):0xffffffffu;
}
static uint64_t event_order_fnv(uint64_t hash,const void *data,size_t size) {
    const unsigned char *p=data;
    for(size_t i=0;i<size;i++) {hash^=p[i];hash*=0x100000001b3ULL;}
    return hash;
}
/* Monotone over finite doubles; NaN still gets a fixed place (strict order). */
static uint64_t event_order_time_key(uint64_t bits) {return bits&0x8000000000000000ULL?~bits:bits|0x8000000000000000ULL;}
/* Run classes: saved 0, runtime 1, cleared 2 (as before), created 3. */
enum {EVENT_ORDER_SAVED=0,EVENT_ORDER_RUNTIME=1,EVENT_ORDER_NONE=2,EVENT_ORDER_CREATED=3};
/* Order rank of a class: saved, created, runtime, cleared. Without created
 * runs the ranks of 0/1/2 keep the order of the class values. */
static unsigned event_order_rank(unsigned klass) {return klass==EVENT_ORDER_CREATED?1:klass==EVENT_ORDER_SAVED?0:klass+1;}
static int event_order_is_local(unsigned klass) {return klass==EVENT_ORDER_RUNTIME || klass==EVENT_ORDER_NONE;}
static int event_order_compare(const void *a,const void *b) {
    const event_order_run *x=a,*y=b;
    if(x->time!=y->time) return x->time<y->time?-1:1;
    if(x->klass!=y->klass) return event_order_rank(x->klass)<event_order_rank(y->klass)?-1:1;
    if(x->id!=y->id) return x->id<y->id?-1:1;
    if(x->source!=y->source) return (uintptr_t)x->source<(uintptr_t)y->source?-1:1;
    return x->first<y->first?-1:x->first>y->first;
}
static event_order_entry event_order_read(const unsigned char *batch,uint64_t i) {
    event_order_entry e;const unsigned char *slot=batch+i*EVENT_ORDER_STRIDE;
    memcpy(&e.source,slot+0x18,8);memcpy(&e.event,slot+0x28,8);memcpy(&e.time,slot+0x30,8);return e;
}
/* Saved ID first (class 0), else runtime ID (class 1); a cleared slot is class 2;
 * a created ID (portable keys on) is class 3. */
static int event_order_identify(unsigned runs) {
    int ok=1;
#ifdef EVENT_ORDER_OWN_PLATFORM
    for(unsigned i=0;ok && i<runs;i++) {
        event_order_run *r=&event_order_runs[i];uint64_t vtable=0;
        r->klass=r->source?1:2;r->id=0;
        ok=!r->source || (gate_read_local(r->source+8,&r->id,8) && gate_read_local(r->source,&vtable,8));
        r->type=r->source?event_order_rva(vtable):0xffffffffu;
    }
#else
    __try {
        for(unsigned i=0;i<runs;i++) {
            event_order_run *r=&event_order_runs[i];
            r->klass=r->source?1:2;r->id=r->source?*(volatile uint64_t*)(r->source+8):0;
            r->type=r->source?event_order_rva(*(volatile uint64_t*)r->source):0xffffffffu;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {ok=0;}
#endif
    for(unsigned i=0;ok && i<runs;i++) {
        event_order_run *r=&event_order_runs[i];unsigned klass=WORLD_KEY_NONE;
        uint64_t saved=r->klass==EVENT_ORDER_RUNTIME?world_identity_key(r->id,&klass):0;
        if(saved) {r->klass=klass==WORLD_KEY_CREATED?EVENT_ORDER_CREATED:EVENT_ORDER_SAVED;r->id=saved;}
    }
    return ok;
}
#ifdef EVENT_ORDER_OWN_PLATFORM
static uint64_t event_order_now(void) {
    uint64_t now=0;unsigned char *tls=gate_tls_self();
    if(tls && !gate_read_local(tls+EVENT_ORDER_TLS_TIME,&now,8)) now=0;
    return now;
}
#else
/* Delivering thread's TLS game time; noinline keeps MinGW's NtCurrentTeb
 * inline out of a Clang SEH funclet (as ai_current_rng). */
static __attribute__((noinline)) void *event_order_tls(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots?slots[0]:NULL;
}
static uint64_t event_order_now(void) {
    uint64_t now=0;unsigned char *tls=event_order_tls();
    __try {if(tls) memcpy(&now,tls+0x300,8);} __except(EXCEPTION_EXECUTE_HANDLER) {now=0;}
    return now;
}
#endif
/* Single writer (the delivering thread); a row is published by the cursor. */
static void event_order_record(uint64_t begin,unsigned runs,uint64_t now) {
    uint64_t cursor=(uint64_t)event_order_row_cursor;unsigned at=0;
    for(unsigned k=0;k<runs;k++) {
        const event_order_run *r=&event_order_runs[k];
        for(unsigned i=0;i<r->length;i++,at++) {
            const event_order_entry *e=&event_order_entries[r->first+i];
            event_order_row_t *row=&event_order_ring[cursor%EVENT_ORDER_ROWS];uint64_t vtable=0;
#ifdef EVENT_ORDER_OWN_PLATFORM
            if(e->event && !gate_read_local(e->event,&vtable,8)) {vtable=0;++event_order_row_faults;}
#else
            __try {if(e->event) vtable=*(volatile uint64_t*)e->event;}
            __except(EXCEPTION_EXECUTE_HANDLER) {vtable=0;++event_order_row_faults;}
#endif
            row->id=r->id;row->klass=(uint8_t)r->klass;row->source_type=r->type;row->event_type=event_order_rva(vtable);
            row->due=e->time;row->now=now;row->pass=(uint32_t)event_order_passes;row->slot=(uint32_t)(begin+at);
            row->tick=(uint32_t)stats.issued;row->phase=(uint8_t)boot_phase;
            InterlockedExchange64(&event_order_row_cursor,(LONG64)++cursor);
        }
    }
}
/* Reorders [begin,end) of the batch; the prefix and every slot field other
 * than source/event/time stay in place. Pure stores follow all reads. */
static int event_order_sort(unsigned char *batch,uint64_t begin,uint64_t end,int boot) {
    if(begin>=end || end>EVENT_ORDER_CAPACITY) {event_order_stage="range";return 0;}
    unsigned n=(unsigned)(end-begin),runs=0;
    event_order_stage="collect";
    for(unsigned i=0;i<n;i++) {
        event_order_entries[i]=event_order_read(batch,begin+i);
        if(runs && event_order_runs[runs-1].source==event_order_entries[i].source) {++event_order_runs[runs-1].length;continue;}
        event_order_run *r=&event_order_runs[runs++];
        r->source=event_order_entries[i].source;r->first=i;r->length=1;r->time=event_order_time_key(event_order_entries[i].time);
    }
    event_order_stage="identity";if(!event_order_identify(runs)) return 0;
    qsort(event_order_runs,runs,sizeof(*event_order_runs),event_order_compare);
    uint64_t ties=0,local_ties=0,collisions=0,moved=0,local=0,created=0,view_runs=0,view_entries=0,hash=event_order_hash;unsigned at=0;
    const event_order_run *p=NULL; /* the previous run that is not view-local */
    for(unsigned k=0;k<runs;k++) {
        const event_order_run *r=&event_order_runs[k];
        if(r->first!=at) ++moved;
        if(r->type==EVENT_ORDER_VIEW_LOCAL) {++view_runs;view_entries+=r->length;at+=r->length;continue;}
        if(event_order_is_local(r->klass)) ++local;else if(r->klass==EVENT_ORDER_CREATED) ++created;
        if(p && p->time==r->time) {
            ++ties;if(event_order_is_local(p->klass) || event_order_is_local(r->klass)) ++local_ties;
            if(p->klass==r->klass && p->id==r->id && p->source!=r->source) ++collisions;
        }
        uint64_t portable=event_order_is_local(r->klass)?0:r->id,length=r->length,klass=r->klass;
        hash=event_order_fnv(event_order_fnv(event_order_fnv(event_order_fnv(hash,&klass,8),&portable,8),
            &event_order_entries[r->first].time,8),&length,8);
        at+=r->length;p=r;
    }
    if(at!=n) {event_order_stage="partition";return 0;}
    event_order_stage="write";at=0;
    for(unsigned k=0;k<runs;k++) for(unsigned i=0;i<event_order_runs[k].length;i++,at++) {
        const event_order_entry *e=&event_order_entries[event_order_runs[k].first+i];
        unsigned char *slot=batch+(begin+at)*EVENT_ORDER_STRIDE;
        memcpy(slot+0x18,&e->source,8);memcpy(slot+0x28,&e->event,8);memcpy(slot+0x30,&e->time,8);
        event_order_mark *m=&event_order_marks[begin+at];m->event=e->event;m->time=e->time;m->end=end;
    }
    /* Marks past this pass belong to no live pass: an enclosing pass that is
     * still delivering owns only slots below begin. */
    for(uint64_t i=end;i<event_order_marked;i++) event_order_marks[i].event=NULL;
    event_order_marked=end;
    ++event_order_passes;event_order_boot_passes+=boot!=0;event_order_total+=n;event_order_run_total+=runs;
    event_order_local+=local;event_order_created+=created;event_order_ties+=ties;event_order_local_ties+=local_ties;
    event_order_collisions+=collisions;event_order_moved+=moved;event_order_saturated+=end==EVENT_ORDER_CAPACITY;
    event_order_view_runs+=view_runs;event_order_view_entries+=view_entries;
    event_order_hash=hash;event_order_last_count=n;event_order_last_runs=runs;event_order_stage="done";
    if(event_order_rows_armed && (boot || stats.issued<=event_order_rows_ticks)) event_order_record(begin,runs,event_order_now());
    return 1;
}
static uint64_t event_order_find(const unsigned char *batch,uint64_t count,const void *source,const void *event,uint64_t time) {
    for(uint64_t pass=0;pass<2;pass++) {
        uint64_t from=pass?0:event_order_next,to=pass?(event_order_next<count?event_order_next:count):count;
        for(uint64_t i=from;i<to;i++) {
            event_order_entry e=event_order_read(batch,i);
            if(e.source==source && e.event==event && e.time==time) return i;
        }
    }
    return count;
}
/* The native loop already read slot j into (source,event,time) and releases
 * slot j after this returns, so the slot's current contents are delivered. */
static void event_order_deliver_with(event_order_deliver_fn native,unsigned char *batch,const volatile uint64_t *count_slot,
                                     void *source,void *event,double time) {
    /* After any failure the remaining calls stay native until the gate stops (80). */
    if(!event_order_armed || !stats.active || event_order_failures) {native(source,event,time);return;}
    uint64_t bits,count=*count_slot,j;memcpy(&bits,&time,8);++event_order_calls;
    if(count>EVENT_ORDER_CAPACITY || (j=event_order_find(batch,count,source,event,bits))>=count) {
        event_order_stage="find";InterlockedIncrement64(&event_order_failures);native(source,event,time);return;
    }
    const event_order_mark *m=&event_order_marks[j];
    if(!(m->event==event && m->time==bits && m->end==count) && !event_order_sort(batch,j,count,boot_mode && boot_phase<3)) {
        InterlockedIncrement64(&event_order_failures);native(source,event,time);return;
    }
    event_order_entry e=event_order_read(batch,j);
    event_order_marks[j].event=NULL;event_order_next=j+1;
    double due;memcpy(&due,&e.time,8);
    if(e.source) event_rng_deliver(native,e.source,e.event,due); /* native unless native_gate_event_rng is armed */
}
static void event_order_deliver(void *source,void *event,double time) {
    event_order_deliver_with((event_order_deliver_fn)(image+EVENT_ORDER_DELIVER),image+EVENT_ORDER_BATCH,
        (const volatile uint64_t*)(image+EVENT_ORDER_COUNT),source,event,time);
}
#ifndef EVENT_ORDER_OWN_PLATFORM
static int event_order_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+EVENT_ORDER_SITE,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)(image+EVENT_ORDER_SITE),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+EVENT_ORDER_SITE,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+EVENT_ORDER_SITE,8);
    return actual==expected;
}
static void event_order_restore(void) {
    if(!event_order_installed) return;
    event_order_restored=event_order_swap(event_order_patched_word,event_order_original_word);
    if(event_order_restored) event_order_installed=0;
}
/* ProcessDueEvents around the rel32, DeliverEvent, slot release 0x99ba70 and
 * the EventSource destructor's batch scan (clears only +0x18 of its slots). */
static int event_order_pins(void) {
    return hash_ok(image+0x99bb50,0x882,"0ca7057e3b157a79c5479532d13ac6d04f9be534b9435f72b7abc281c458e475") &&
        hash_ok(image+EVENT_ORDER_RETURN,0x95,"e352aaa4dad9fe5916d82aa7f1849a70aaa78286e6b43e8b5e191ee981530d18") &&
        hash_ok(image+EVENT_ORDER_DELIVER,0x1d3,"b605dc6a10b27c1d26b2ecd6f30d8e7c93641c56162a89c9c995eee83d3447f5") &&
        hash_ok(image+0x99ba70,0xab,"59552f2255c6310a705813c8826d465b6db645356ed77771f8606a8f8e0a561d") &&
        hash_ok(image+0x998f83,0x37,"6a3f4d8e6edf314e86efc269ee3745a5549ae4ad5b506ab86b1c69f20bb79c75");
}
static int event_order_arm(void) {
    /* A loading-UI reload reruns configuration: accept the installed owner. */
    if(event_order_armed) return stats.active && event_order_installed;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !thunk || !event_order_pins() || !thunk_slot_ready(EVENT_ORDER_THUNK)) return 0;
    intptr_t distance=(intptr_t)(thunk+EVENT_ORDER_THUNK)-(intptr_t)(image+EVENT_ORDER_RETURN);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&event_order_original_word,image+EVENT_ORDER_SITE,8);
    if(event_order_original_word!=EVENT_ORDER_NATIVE_WORD) return 0;
    int32_t rel=(int32_t)distance;event_order_patched_word=event_order_original_word;
    memcpy((unsigned char*)&event_order_patched_word+2,&rel,4);
    if(!event_order_swap(event_order_original_word,event_order_patched_word)) return 0;
    event_order_installed=1;InterlockedExchange(&event_order_armed,1);return 1;
}
#endif
#ifndef EVENT_ORDER_ROW_TICKS_MAX
#define EVENT_ORDER_ROW_TICKS_MAX 600
#endif
static int event_order_rows_arm(unsigned ticks) {
    if(event_order_rows_armed) return stats.active && event_order_rows_ticks==ticks;
    if(!event_order_armed || !event_order_installed || !stats.active || !boot_mode || boot_phase || stats.issued ||
       GetCurrentThreadId()!=ui_thread || ticks>EVENT_ORDER_ROW_TICKS_MAX) return 0;
    event_order_rows_ticks=ticks;InterlockedExchange(&event_order_rows_armed,1);return 1;
}
/* One row per call; the probe walks absolute indices until nil. Rows
 * overwritten before they were drained report evicted=1. */
static int event_order_row(uint64_t index,char *out,size_t capacity) {
    static const char *const kinds[5]={"saved","runtime","none","created","?"};
    LONG64 cursor=InterlockedCompareExchange64(&event_order_row_cursor,0,0);
    if((LONG64)index>=cursor) return 0;
    int n;
    if(cursor>(LONG64)EVENT_ORDER_ROWS && index<(uint64_t)(cursor-(LONG64)EVENT_ORDER_ROWS))
        n=snprintf(out,capacity,"index=%llu evicted=1",(unsigned long long)index);
    else {
        const event_order_row_t *r=&event_order_ring[index%EVENT_ORDER_ROWS];
        n=snprintf(out,capacity,"index=%llu pass=%u phase=%u tick=%u slot=%u source=%s:%016llx stype=%x etype=%x due=%016llx now=%016llx",
            (unsigned long long)index,r->pass,r->phase,r->tick,r->slot,kinds[r->klass<4?r->klass:4],(unsigned long long)r->id,
            r->source_type,r->event_type,(unsigned long long)r->due,(unsigned long long)r->now);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int event_order_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld installed=%d calls=%llu passes=%llu boot_passes=%llu entries=%llu runs=%llu local=%llu "
        "ties=%llu local_ties=%llu collisions=%llu moved=%llu saturated=%llu last=%u/%u order=%016llx stage=%s restored=%d failures=%lld "
        "rows_armed=%ld row_ticks=%u rows=%lld row_overwritten=%lld row_faults=%llu created=%llu view_runs=%llu view_entries=%llu",
        event_order_armed,event_order_installed,(unsigned long long)event_order_calls,(unsigned long long)event_order_passes,
        (unsigned long long)event_order_boot_passes,(unsigned long long)event_order_total,(unsigned long long)event_order_run_total,
        (unsigned long long)event_order_local,(unsigned long long)event_order_ties,(unsigned long long)event_order_local_ties,
        (unsigned long long)event_order_collisions,(unsigned long long)event_order_moved,(unsigned long long)event_order_saturated,
        event_order_last_count,event_order_last_runs,(unsigned long long)event_order_hash,event_order_stage,event_order_restored,
        (long long)event_order_failures,event_order_rows_armed,event_order_rows_ticks,(long long)event_order_row_cursor,
        (long long)(event_order_row_cursor>(LONG64)EVENT_ORDER_ROWS?event_order_row_cursor-(LONG64)EVENT_ORDER_ROWS:0),
        (unsigned long long)event_order_row_faults,(unsigned long long)event_order_created,
        (unsigned long long)event_order_view_runs,(unsigned long long)event_order_view_entries);
    return n>0 && (size_t)n<capacity?n:0;
}
