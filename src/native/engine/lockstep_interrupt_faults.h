/* What the AI observers could not record. They only watch: a failed read, a full table or an
 * exception passing through changes nothing in the game, so none of it may end a session.
 * play26-long stopped both clients at tick 15547 (reason 68, 518 s of game time) because
 * npc_actions, which nothing drained without native_gate_npc_lifecycle, reached its 16384 rows.
 *
 * A full table stops recording and counts (*_dropped), until the row drain empties it.
 * Anything else is a fault: counted per site, the first rows of each site name it (site, tick,
 * actor, pc, depth, script), as INTERRUPT_FAULT_ROW and INTERRUPT_FAULTS_STOP.
 * Only native_gate_interrupt_rng keys from these samples, so only with it does a fault stop the
 * gate (reason 68); it fails alike on every client because the same read fails on every client. */
enum {
    IFAULT_THREAD,IFAULT_READ,IFAULT_END,IFAULT_UNWIND,IFAULT_EVENT_UNWIND,
    IFAULT_STEP_RNG,IFAULT_STEP_READ,IFAULT_STEP_AFTER,IFAULT_STEP_UNWIND,
    IFAULT_ACTION_RNG,IFAULT_ACTION_DEPTH,IFAULT_ACTION_FRAME,IFAULT_ACTION_READ,IFAULT_ACTION_AFTER,IFAULT_ACTION_UNWIND,
    IFAULT_NPC_RNG,IFAULT_NPC_NAME,IFAULT_NPC_DEPTH,IFAULT_NPC_FRAME,IFAULT_NPC_READ,IFAULT_NPC_AFTER,IFAULT_NPC_UNWIND,
    IFAULT_SITES
};
static const char *const ifault_names[IFAULT_SITES]={
    "thread","read","end","unwind","event_unwind",
    "step_rng","step_read","step_after","step_unwind",
    "action_rng","action_depth","action_frame","action_read","action_after","action_unwind",
    "npc_rng","npc_name","npc_depth","npc_frame","npc_read","npc_after","npc_unwind",
};
#define IFAULT_ROWS_PER_SITE 4u
#define IFAULT_RING (IFAULT_SITES*IFAULT_ROWS_PER_SITE)
typedef struct {volatile LONG64 ready;uint64_t tick,pc;unsigned site,actor,depth;char script[96];} ifault_row_t;
static ifault_row_t ifault_ring[IFAULT_RING];
static volatile LONG64 ifault_counts[IFAULT_SITES],ifault_rows,ifault_total;
static uint64_t interrupt_dropped,interrupt_step_dropped,nested_action_dropped,npc_action_dropped;
static void interrupt_fault(unsigned site,unsigned actor,uint64_t pc,unsigned depth,const char *script) {
    LONG64 seen=InterlockedIncrement64(&ifault_counts[site]);InterlockedIncrement64(&ifault_total);
    if(interrupt_seeded) InterlockedIncrement64(&interrupt_failures);
    if(seen>(LONG64)IFAULT_ROWS_PER_SITE) return;
    LONG64 index=InterlockedIncrement64(&ifault_rows)-1;
    if(index>=(LONG64)IFAULT_RING) return;
    ifault_row_t *r=&ifault_ring[index];
    r->tick=stats.issued;r->pc=pc;r->site=site;r->actor=actor;r->depth=depth;
    size_t n=0;if(script) while(n<sizeof(r->script)-1 && script[n]) {r->script[n]=script[n];++n;}
    r->script[n]=0;
    InterlockedExchange64(&r->ready,index+1);
}
static __attribute__((unused)) void ifault_reset(void) { /* tests */
    memset((void*)ifault_counts,0,sizeof(ifault_counts));memset(ifault_ring,0,sizeof(ifault_ring));
    ifault_rows=0;ifault_total=0;
    interrupt_dropped=interrupt_step_dropped=nested_action_dropped=npc_action_dropped=0;
}
static int ifault_row(uint64_t index,char *out,size_t cap) {
    if(index>=IFAULT_RING) return 0;
    const ifault_row_t *r=&ifault_ring[index];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index+1) return 0;
    int n=snprintf(out,cap,"index=%llu tick=%llu site=%s actor=%u pc=%llu depth=%u script=%s",(unsigned long long)index,(unsigned long long)r->tick,
        ifault_names[r->site<IFAULT_SITES?r->site:0],r->actor,(unsigned long long)r->pc,r->depth,r->script[0]?r->script:"-");
    return n>0 && (size_t)n<cap?n:0;
}
static int ifault_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"faults=%lld dropped=%llu/%llu/%llu/%llu rows=%lld play_mode=%d sites=",
        (long long)InterlockedCompareExchange64(&ifault_total,0,0),(unsigned long long)interrupt_dropped,(unsigned long long)interrupt_step_dropped,
        (unsigned long long)nested_action_dropped,(unsigned long long)npc_action_dropped,(long long)ifault_rows,gate_play_mode);
    if(n<0 || (size_t)n>=cap) return 0;
    int any=0;
    for(unsigned i=0;i<IFAULT_SITES;i++) {
        LONG64 count=InterlockedCompareExchange64(&ifault_counts[i],0,0);if(!count) continue;
        int m=snprintf(out+n,cap-(size_t)n,"%s%s:%lld",any?",":"",ifault_names[i],(long long)count);
        if(m<0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;any=1;
    }
    if(!any) {if(cap-(size_t)n<2) return 0;out[n++]='-';out[n]=0;}
    return n;
}
