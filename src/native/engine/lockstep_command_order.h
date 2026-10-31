/* Determinism diagnostic and fix, DEFAULT OFF: the order of the event commands
 * the pathing batch jobs append in one pass (native_gate_command_order =
 * 'observe' | 'sort'; needs native_gate_serial_pathing, and the travel snapshot
 * join for 'sort').
 * Events posted with U::EventSource_PostEventDeferred 0x99a720 become commands
 * {+0 kind (3 = insert event), +8 source runtime ID, +0x10 event, +0x18 time}
 * in the payload (+0x10) of 0x40-byte nodes of the std::list
 * g_EventCommandQueue 0x39943a8 (sentinel +0, size +8; append 0x998430), under
 * CRITICAL_SECTION 0x398a558. U::Path::PathingBatchJob 0xeaeb60 collects its
 * batch's commands privately and splices them at the tail of that list under the
 * same lock (0xeaebb2..0xeaec3e), so the pass's segment is in job completion
 * order: thread timing. FlushCommandQueue 0x99abe0 inserts each event into its
 * source's time multimap, FIFO on equal times, and native_gate_event_order keeps
 * native order inside a source run: two events of one source at the same time
 * from different batches are delivered in completion order. long-L12: the only
 * keyed attention differences in 3000 ticks are three pairs of low-attention
 * small ships whose ConnectTo swaps one tick (92d05/9404e at 1789/1790,
 * 6dc62/ac854 at 2023/2024, 909aa/a79fa at 2358/2360), each pair in unrelated
 * sectors, after an EVENT_ORDER hash difference with equal counts at 1788.
 * Each admitted frame, the queue's tail is noted before the pass is released and
 * the segment appended up to the join and path determinism's stamping (whose
 * RefreshFromPathingState calls post transition/waypoint events: long-L13's
 * 92d05 connects from a WarpEvent, chain WarpEvent::Dispatch 0x9aa290 ->
 * 0x6cfa64 -> Space::AddChild 0x83a8e0 -> ConnectTo) is read under the lock: per source
 * (grouped by runtime ID) the commands in queue order. Status: segment size,
 * sources with two or more commands (multi), pairs out of (time, event type)
 * order (unsorted) and equal in both (ties: only the payload could order them),
 * commands of sources without a portable identity (local), an order-free set
 * hash and a hash of every source's sequence (portable keys; local sources count
 * with key 0), and the same hash with every source in canonical order (canon:
 * equal on all clients unless ties remain). One COMMAND_ORDER_ROW per multi source: its key and the event
 * types and time bits in queue order. 'sort' also gives each multi source whose
 * segment commands are all inserts its own commands in (time, event type, queue)
 * order by exchanging payloads among that source's nodes; no node moves, other
 * sources keep their places, groups with other kinds are left (mixed).
 * Trace rows, DEFAULT OFF (native_gate_command_order_rows): a row for every
 * source of the segment, not only multi ones, with a detail= list per command:
 * kind, queue position and up to two portable keys found among the event's
 * qwords +0x8..+0x40 (the event's targets: a ship, a gate, a zone). long-L13:
 * set, seq and canon part at 1788 with four commands on both clients and no
 * multi source, so the differing command has no row without this. Pins: the
 * post routine, the append helper and the splice block. Failures stop the gate
 * with reason 131. */
#define CO_QUEUE 0x39943a8u
#define CO_LOCK 0x398a558u
#define CO_POST 0x99a720u
#define CO_APPEND 0x998430u
#define CO_SPLICE 0xeaebb2u
#define CO_PAYLOAD 0x10u
#define CO_PAYLOAD_SIZE 0x30u
#define CO_CAP 16384u
#define CO_ROW_RING 4096u
#define CO_ROWS_PER_TICK 64u
#define CO_ROW_TYPES 6u
#define CO_REFS 2u
#define CO_REF_SCAN 8u /* event qwords +0x8..+0x40 */
#define CO_STOP_REASON 131u
enum {CO_OFF,CO_OBSERVE,CO_SORT};
typedef struct {unsigned char *node;uint64_t runtime,key,time,ref[CO_REFS];uint32_t etype,kind,klass,position;} co_cmd;
typedef struct {uint64_t tick,key;uint32_t klass,n,reordered,traced,etype[CO_ROW_TYPES],kind[CO_ROW_TYPES],position[CO_ROW_TYPES];
    uint64_t time[CO_ROW_TYPES],ref[CO_ROW_TYPES][CO_REFS];} co_row_t;
static co_cmd co_cmds[CO_CAP],co_scratch[CO_CAP];
static unsigned char co_payloads[CO_CAP][CO_PAYLOAD_SIZE];
static co_row_t co_ring[CO_ROW_RING];
static volatile LONG64 co_rows,co_failures;
static int co_mode,co_armed,co_noted,co_trace;
static unsigned char *co_sentinel,*co_tail;
static uint64_t co_size_before,co_tick=UINT64_MAX;
static struct {uint64_t pre,appended,multi,unsorted,ties,local,other,mixed,sorted,set,seq,canon,dropped,rows;} co_frame;
static uint64_t co_frames,co_total_multi,co_total_unsorted,co_total_ties,co_total_sorted,co_total_mixed,co_swapped,co_unjoined,co_overflow;
static const char *co_stage="idle";
static uint64_t co_mix(uint64_t x) {x^=x>>33;x*=0xff51afd7ed558ccdULL;x^=x>>33;x*=0xc4ceb9fe1a85ec53ULL;x^=x>>33;return x;}
static int co_pins(void) {
    return hash_ok(image+CO_POST,0xed,"58b80a15cd07746aeb8e15c6b91230adeb30b3355fe80f58dc1b93b5c72925c3") &&
        hash_ok(image+CO_APPEND,0x7b,"449d0bb0bffbf44f1a11f34be34d77731e26d70da48918b9e7a12e3474befbf3") &&
        hash_ok(image+CO_SPLICE,0x8c,"cc3bbb56020941742707e330b83ef07c7baee3d180bf8bb0ec39a42f0cacbd25");
}
static int co_arm(int mode,unsigned options) {
    if(co_armed) return stats.active && co_mode==mode;
    if((mode!=CO_OBSERVE && mode!=CO_SORT) || !stats.active || (boot_mode && boot_phase) || stats.issued ||
       GetCurrentThreadId()!=ui_thread || !(options&1)) return 0;
    if(!co_pins()) {co_stage="pins";return 0;}
    co_rows=co_failures=0;co_frames=co_total_multi=co_total_unsorted=co_total_ties=co_total_sorted=co_total_mixed=0;
    co_swapped=co_unjoined=co_overflow=0;co_noted=0;co_tick=UINT64_MAX;memset(&co_frame,0,sizeof(co_frame));
    co_mode=mode;co_armed=1;co_stage="armed";return 1;
}
static void co_fail(const char *why) {co_stage=why;InterlockedIncrement64(&co_failures);}
/* Trace rows: after the arming, before the first admitted frame. */
static int co_trace_arm(void) {
    if(!co_armed || stats.issued || GetCurrentThreadId()!=ui_thread) return co_armed && co_trace;
    co_trace=1;return 1;
}
/* Before the pass is released (MoveWorker): note the tail and size. */
static void co_before_release(void) {
    if(!co_armed || !stats.active) return;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+CO_LOCK);
    EnterCriticalSection(lock);
    co_sentinel=*(unsigned char**)(image+CO_QUEUE);co_size_before=*(uint64_t*)(image+CO_QUEUE+8);
    co_tail=co_sentinel?*(unsigned char**)(co_sentinel+8):NULL;
    LeaveCriticalSection(lock);
    co_noted=co_sentinel && co_tail;if(!co_noted) co_fail("queue");
}
static int co_group_order(const void *a,const void *b) {
    const co_cmd *p=a,*q=b;
    if(p->runtime!=q->runtime) return p->runtime<q->runtime?-1:1;
    return p->position<q->position?-1:p->position>q->position;
}
/* Canonical order inside one source: time, event type, then queue position. */
static int co_canonical_order(const void *a,const void *b) {
    const co_cmd *p=a,*q=b;double tp,tq;memcpy(&tp,&p->time,8);memcpy(&tq,&q->time,8);
    if(tp!=tq) return tp<tq?-1:1;
    if(p->etype!=q->etype) return p->etype<q->etype?-1:1;
    return p->position<q->position?-1:p->position>q->position;
}
static void co_row(const co_cmd *g,unsigned n,unsigned reordered) {
    if(++co_frame.rows>CO_ROWS_PER_TICK) {++co_frame.dropped;return;}
    co_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.key=g[0].key;r.klass=g[0].klass;r.n=n;r.reordered=reordered;r.traced=co_trace!=0;
    for(unsigned i=0;i<n && i<CO_ROW_TYPES;i++) {
        r.etype[i]=g[i].etype;r.time[i]=g[i].time;r.kind[i]=g[i].kind;r.position[i]=g[i].position;memcpy(r.ref[i],g[i].ref,sizeof(r.ref[i]));
    }
    LONG64 index=InterlockedIncrement64(&co_rows)-1;co_ring[(uint64_t)index%CO_ROW_RING]=r;
}
/* One source group, in queue order: counts, hashes, row and (sort) payload exchange. */
static uint64_t co_group(co_cmd *g,unsigned n,int sort) {
    uint64_t seq=0xcbf29ce484222325ULL;unsigned all_inserts=1,in_order=1;
    for(unsigned i=0;i<n;i++) {
        uint64_t parts[3]={g[i].kind,g[i].etype,g[i].time};
        for(unsigned j=0;j<3;j++) {seq^=parts[j];seq*=0x100000001b3ULL;}
        if(g[i].kind!=3) all_inserts=0;
    }
    uint64_t key=co_mix(g[0].key^((uint64_t)g[0].klass<<62));
    if(n<2) {co_frame.canon+=co_mix(seq^key);if(co_trace) co_row(g,n,0);return co_mix(seq^key);}
    ++co_frame.multi;
    memcpy(co_scratch,g,sizeof(*g)*n);qsort(co_scratch,n,sizeof(*g),co_canonical_order);
    uint64_t canon=0xcbf29ce484222325ULL;
    for(unsigned i=0;i<n;i++) {
        uint64_t parts[3]={co_scratch[i].kind,co_scratch[i].etype,co_scratch[i].time};
        for(unsigned j=0;j<3;j++) {canon^=parts[j];canon*=0x100000001b3ULL;}
    }
    co_frame.canon+=co_mix(canon^key);
    for(unsigned i=0;i<n;i++) if(co_scratch[i].position!=g[i].position) in_order=0;
    for(unsigned i=1;i<n;i++) {
        if(co_canonical_order(&g[i-1],&g[i])>0) ++co_frame.unsorted;
        if(!memcmp(&co_scratch[i-1].time,&co_scratch[i].time,8) && co_scratch[i-1].etype==co_scratch[i].etype) ++co_frame.ties;
    }
    unsigned reordered=0;
    if(sort && !in_order) {
        if(!all_inserts) ++co_frame.mixed;
        else {
            /* Payloads in canonical order, written back into the group's nodes in queue order. */
            for(unsigned i=0;i<n;i++) memcpy(co_payloads[i],co_scratch[i].node+CO_PAYLOAD,CO_PAYLOAD_SIZE);
            for(unsigned i=0;i<n;i++) memcpy(g[i].node+CO_PAYLOAD,co_payloads[i],CO_PAYLOAD_SIZE);
            ++co_frame.sorted;reordered=1;
        }
    }
    co_row(g,n,reordered);
    return co_mix(seq^key);
}
static void co_read_command(co_cmd *c,unsigned char *node,uint32_t position) {
    uint32_t kind=0;uint64_t runtime=0,time=0;unsigned char *event=NULL,*vt=NULL;unsigned klass=WORLD_KEY_NONE;
    read_local(node+CO_PAYLOAD,&kind,4);read_local(node+CO_PAYLOAD+8,&runtime,8);
    read_local(node+CO_PAYLOAD+0x10,&event,8);read_local(node+CO_PAYLOAD+0x18,&time,8);
    c->node=node;c->kind=kind;c->runtime=runtime;c->time=time;c->position=position;c->etype=0xffffffffu;
    if(event && read_local(event,&vt,8) && vt>=image && vt<image+0x6fa9000) c->etype=(uint32_t)(vt-image);
    c->key=runtime?world_identity_key(runtime,&klass):0;c->klass=klass;
    if(c->klass==WORLD_KEY_NONE) c->key=0;
    c->ref[0]=c->ref[1]=0;
    if(co_trace && event) for(unsigned i=0,found=0;i<CO_REF_SCAN && found<CO_REFS;i++) {
        uint64_t value=0;unsigned k;
        if(!read_local(event+8+8*i,&value,8) || !value || value==runtime) continue;
        uint64_t ref=world_identity_key(value,&k);
        if(ref) c->ref[found++]=ref;
    }
}
/* After the join (MoveWorker; tsnap has waited for the late splices). */
static void co_after_join(int joined) {
    if(!co_armed || !stats.active || !co_noted) return;
    co_noted=0;
    if(co_tick!=stats.issued) {co_tick=stats.issued;memset(&co_frame,0,sizeof(co_frame));}
    if(!joined) ++co_unjoined;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+CO_LOCK);
    EnterCriticalSection(lock);
    unsigned char *sentinel=*(unsigned char**)(image+CO_QUEUE),*node;
    uint64_t size=*(uint64_t*)(image+CO_QUEUE+8);unsigned n=0,full=0;
    if(sentinel!=co_sentinel) {++co_swapped;node=sentinel?*(unsigned char**)sentinel:NULL;co_frame.pre=0;}
    else {node=*(unsigned char**)co_tail;co_frame.pre=co_size_before;}
    for(;node && node!=sentinel;node=*(unsigned char**)node) {
        if(n==CO_CAP) {++co_overflow;full=1;break;}
        co_read_command(&co_cmds[n],node,n);++n;
    }
    co_frame.appended=n;
    if(sentinel==co_sentinel && !full && size!=co_size_before+n) co_fail("size");
    for(unsigned i=0;i<n;i++) {
        const co_cmd *c=&co_cmds[i];
        if(c->kind!=3) ++co_frame.other;
        if(c->klass==WORLD_KEY_NONE) ++co_frame.local;
        co_frame.set+=co_mix(co_mix(c->key^((uint64_t)c->klass<<62))^co_mix(((uint64_t)c->kind<<32)|c->etype)^co_mix(c->time));
    }
    /* Group by source (runtime ID only groups; it is not part of any hash). */
    qsort(co_cmds,n,sizeof(*co_cmds),co_group_order);
    /* 'sort' only once the late splices are proven done (else the segment may still grow). */
    int sort=co_mode==CO_SORT && joined && !full;
    for(unsigned i=0;i<n;) {
        unsigned j=i+1;while(j<n && co_cmds[j].runtime==co_cmds[i].runtime) ++j;
        co_frame.seq+=co_group(&co_cmds[i],j-i,sort);i=j;
    }
    LeaveCriticalSection(lock);
    ++co_frames;co_total_multi+=co_frame.multi;co_total_unsorted+=co_frame.unsorted;co_total_ties+=co_frame.ties;
    co_total_sorted+=co_frame.sorted;co_total_mixed+=co_frame.mixed;
    if(!InterlockedCompareExchange64(&co_failures,0,0)) co_stage=co_mode==CO_SORT && !sort?"unjoined":"frame"; /* a failure keeps its reason */
}
static int co_check(void) {
    if(InterlockedCompareExchange64(&co_failures,0,0)) return 0;
    if(!co_pins()) {co_fail("block_changed");return 0;}
    return 1;
}
static int co_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&co_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)CO_ROW_RING && index<(uint64_t)(count-(LONG64)CO_ROW_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    static const char *const kinds[3]={"saved","created","local"};
    const co_row_t *r=&co_ring[index%CO_ROW_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu source=%s:%016llx n=%u reordered=%u seq=",(unsigned long long)index,(unsigned long long)r->tick,
        kinds[r->klass<3?r->klass:2],(unsigned long long)r->key,r->n,r->reordered);
    for(unsigned i=0;n>0 && (size_t)n<cap && i<r->n && i<CO_ROW_TYPES;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%08x@%016llx",i?",":"",r->etype[i],(unsigned long long)r->time[i]);
    if(r->traced && n>0 && (size_t)n<cap) {
        n+=snprintf(out+n,cap-(size_t)n," detail=");
        for(unsigned i=0;n>0 && (size_t)n<cap && i<r->n && i<CO_ROW_TYPES;i++)
            n+=snprintf(out+n,cap-(size_t)n,"%sk%u#%u:%016llx/%016llx",i?",":"",r->kind[i],r->position[i],
                (unsigned long long)r->ref[i][0],(unsigned long long)r->ref[i][1]);
    }
    return n>0 && (size_t)n<cap?n:0;
}
static int co_status(char *out,size_t cap) {
    int fresh=co_tick==stats.issued;
    int n=snprintf(out,cap,"mode=%s armed=%d frame=%llu pre=%llu appended=%llu multi=%llu unsorted=%llu ties=%llu local=%llu other=%llu "
        "sorted=%llu mixed=%llu set=%016llx seq=%016llx canon=%016llx rows=%lld dropped=%llu frames=%llu total_multi=%llu total_unsorted=%llu "
        "total_ties=%llu total_sorted=%llu total_mixed=%llu swapped=%llu unjoined=%llu overflow=%llu stage=%s failures=%lld",
        co_mode==CO_SORT?"sort":co_mode==CO_OBSERVE?"observe":"off",co_armed,(unsigned long long)(co_tick==UINT64_MAX?0:co_tick),
        (unsigned long long)(fresh?co_frame.pre:0),(unsigned long long)(fresh?co_frame.appended:0),(unsigned long long)(fresh?co_frame.multi:0),
        (unsigned long long)(fresh?co_frame.unsorted:0),(unsigned long long)(fresh?co_frame.ties:0),(unsigned long long)(fresh?co_frame.local:0),
        (unsigned long long)(fresh?co_frame.other:0),(unsigned long long)(fresh?co_frame.sorted:0),(unsigned long long)(fresh?co_frame.mixed:0),
        (unsigned long long)(fresh?co_frame.set:0),(unsigned long long)(fresh?co_frame.seq:0),(unsigned long long)(fresh?co_frame.canon:0),(long long)co_rows,
        (unsigned long long)(fresh?co_frame.dropped:0),(unsigned long long)co_frames,(unsigned long long)co_total_multi,
        (unsigned long long)co_total_unsorted,(unsigned long long)co_total_ties,(unsigned long long)co_total_sorted,
        (unsigned long long)co_total_mixed,(unsigned long long)co_swapped,(unsigned long long)co_unjoined,(unsigned long long)co_overflow,
        co_stage,(long long)co_failures);
    return n>0 && (size_t)n<cap?n:0;
}
