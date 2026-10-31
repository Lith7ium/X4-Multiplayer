/* Census diagnostic, DEFAULT OFF, read-only: native_gate_transition_rows = true
 * (needs native_gate_world_identities).
 * long-L21: 8bddc warped into an unkeyed space at 1990 on client 2 and 1991
 * on client 1, ac854 the other way round, with every warp row before them
 * equal. Taken first for a highway entry; long-L22 (no transition rows for
 * them, warp time 0) and the image showed new tempzones made by the pathing
 * jobs instead (lockstep_tempzone_order.h). A highway entry runs through three
 * queued events:
 * - the ship's pathing transition names the entry gate; RefreshFromPathingState
 *   0xea2a20 (at path_det's stamping after the pathing join, at the synced time
 *   and in portable order, or earlier on a lazy transform query at the
 *   querying thread's time) calls PostTransitionEvents 0xea35e0, which posts a
 *   TransitionEvent (vtable 0x2ba9f90: +0x18 the ship's ID, +0x20 a flag) to
 *   the gate with the TLS game time, not immediate: it waits in the gate's own
 *   time-ordered queue (FIFO for equal times);
 * - ProcessDueEvents 0x99bb50 delivers it through TransitionEvent::Dispatch
 *   0x9abe30 to the gate's slot 29 (HighwayEntryGate 0xd9fb50, Gate 0x5cdbb0);
 *   the HighwayEntryGate handler posts the ship's WarpEvent into the highway's
 *   zone, again queued with the TLS time;
 * - the next pass delivers the WarpEvent (WARP_RNG_ROW).
 * An event carries the time it was posted, and a pass delivers every queued
 * event whose time is not after its own. This module points TransitionEvent's
 * dispatch entry (0x2ba9fa0, the image's only reference) at a wrapper that
 * logs one TRANSITION_ROW per delivery before running the original: tick,
 * ordinal in the tick, the gate's key and class (vtable RVA), the ship's key,
 * the flag, the event's time, the delivery time (TLS+0x300) and whether it ran
 * on the MoveWorker. Joined across clients with the WARP_RNG_ROWs (which carry
 * the WarpEvent's time too) they name the link where the two clients part:
 * the transition's post time (the stamping's synced time or a lazy query's),
 * its delivery tick and order at the gate, or the warp. Nothing is changed.
 * Pins: the dispatch. Stop reason 144 (entry or pins changed). */
#define TRR_ENTRY 0x2ba9fa0u    /* U::TransitionEvent vtable 0x2ba9f90 slot 2 */
#define TRR_DISPATCH 0x9abe30u  /* U::TransitionEvent::Dispatch */
#define TRR_DISPATCH_SIZE 0xa7u
#define TRR_DISPATCH_SHA "9de5c8c6b5a65123dd159dc08921e2b9ffa6bc6324b86921d67a4164a06906d3"
#define TRR_RING 8192u
#define TRR_STOP_REASON 144u
typedef void (*trr_fn)(void *,void *,void *,double);
typedef struct {uint64_t tick,gate,object,time,now;uint32_t ordinal,gate_class;uint8_t gate_klass,object_klass,flag,ui;} trr_row_t;
static int trr_installed,trr_restored;
static volatile LONG trr_active;
static volatile LONG64 trr_calls,trr_rows,trr_failures;
static uint64_t trr_tick=UINT64_MAX;
static volatile LONG trr_ordinal;
static trr_row_t trr_ring[TRR_RING];
static trr_fn trr_original_override; /* tests only; NULL runs TransitionEvent::Dispatch */
static const char *trr_stage="idle";
static int trr_pins(void) {return hash_ok(image+TRR_DISPATCH,TRR_DISPATCH_SIZE,TRR_DISPATCH_SHA);}
static void trr_dispatch(void *event,void *listener,void *arg,double time);
static int trr_owned(void) {return !trr_installed || *(void*volatile*)(image+TRR_ENTRY)==(void*)trr_dispatch;}
/* tls: the calling thread's TLS block (slot 0), NULL when unset. */
static void trr_run(trr_fn original,const unsigned char *tls,void *event,void *listener,void *arg,double time) {
    InterlockedIncrement64(&trr_calls);
    if(InterlockedCompareExchange(&trr_active,0,0) && stats.active && boot_phase==3 && stats.issued) {
        trr_row_t r;memset(&r,0,sizeof(r));r.tick=stats.issued;r.ui=GetCurrentThreadId()==ui_thread;
        if(r.ui && trr_tick!=stats.issued) {trr_tick=stats.issued;InterlockedExchange(&trr_ordinal,0);}
        r.ordinal=(uint32_t)InterlockedIncrement(&trr_ordinal);
        uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;void *vtable=NULL;
        if(listener && read_local((unsigned char*)listener+8,&runtime,8) && runtime) r.gate=world_identity_key(runtime,&klass);
        r.gate_klass=(uint8_t)klass;
        if(listener && read_local(listener,&vtable,8)) r.gate_class=(unsigned char*)vtable>=image && (unsigned char*)vtable<image+0x6fa9000?
            (uint32_t)((unsigned char*)vtable-image):0xffffffffu;
        runtime=0;klass=WORLD_KEY_NONE;
        if(event && read_local((unsigned char*)event+0x18,&runtime,8) && runtime) r.object=world_identity_key(runtime,&klass);
        r.object_klass=(uint8_t)klass;
        if(event) read_local((unsigned char*)event+0x20,&r.flag,1);
        memcpy(&r.time,&time,8);
        if(tls) read_local(tls+0x300,&r.now,8);
        LONG64 index=InterlockedIncrement64(&trr_rows)-1;trr_ring[(uint64_t)index%TRR_RING]=r;
    }
    original(event,listener,arg,time);
}
/* noinline keeps the TEB read out of the caller. */
static __attribute__((noinline)) void trr_dispatch(void *event,void *listener,void *arg,double time) {
    void **tls=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    trr_run(trr_original_override?trr_original_override:(trr_fn)(image+TRR_DISPATCH),tls?(const unsigned char*)tls[0]:NULL,event,listener,arg,time);
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int trr_arm(void) {
    if(trr_installed) return stats.active!=0; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {trr_stage="needs";return 0;}
    if(!trr_pins()) {trr_stage="pins";return 0;}
    trr_calls=trr_rows=trr_failures=0;trr_tick=UINT64_MAX;trr_ordinal=0;
    InterlockedExchange(&trr_active,1);
    if(!script_rng_slot_swap(TRR_ENTRY,image+TRR_DISPATCH,(void*)trr_dispatch)) {InterlockedExchange(&trr_active,0);trr_stage="entry";return 0;}
    trr_installed=1;trr_restored=0;trr_stage="armed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 144. */
static int trr_check(void) {
    if(InterlockedCompareExchange64(&trr_failures,0,0)) return 0;
    const char *why=!trr_owned()?"entry_changed":!trr_pins()?"block_changed":NULL;
    if(why) {trr_stage=why;InterlockedIncrement64(&trr_failures);return 0;}
    return 1;
}
static void trr_restore(void) {
    InterlockedExchange(&trr_active,0);
    if(!trr_installed) return;
    if(!script_rng_slot_swap(TRR_ENTRY,(void*)trr_dispatch,image+TRR_DISPATCH)) return; /* a foreign value stays */
    trr_installed=0;trr_restored=1;
}
static char trr_klass_char(uint8_t klass) {return klass==WORLD_KEY_CREATED?'c':klass==WORLD_KEY_SAVED?'s':'-';}
static int trr_row(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&trr_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)TRR_RING && index<(uint64_t)(count-(LONG64)TRR_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const trr_row_t *r=&trr_ring[index%TRR_RING];
    n=snprintf(out,cap,"index=%llu tick=%llu ordinal=%u gate=%c:%016llx class=%08x object=%c:%016llx flag=%u time=%016llx now=%016llx ui=%u",
        (unsigned long long)index,(unsigned long long)r->tick,r->ordinal,trr_klass_char(r->gate_klass),(unsigned long long)r->gate,r->gate_class,
        trr_klass_char(r->object_klass),(unsigned long long)r->object,r->flag,(unsigned long long)r->time,(unsigned long long)r->now,r->ui);
    return n>0 && (size_t)n<cap?n:0;
}
static int trr_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d calls=%lld rows=%lld restored=%d stage=%s failures=%lld",trr_installed,(long long)trr_calls,
        (long long)trr_rows,trr_restored,trr_stage,(long long)trr_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
