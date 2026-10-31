/* Cost cut, DEFAULT OFF: native_gate_path_budget = N (full agent updates per batch and pass).
 *
 * UpdateBatch 0xeaecc0 walks a batch's agents from its cursor. Stock: once the wall clock
 * since the pass began (manager+0x210, QueryPerformanceCounter) reaches 1 ms (manager+0x218)
 * the remaining agents get cheap=1 and the next pass starts at the first of them, so the
 * full updates go round the batch. That split depends on the clock, so
 * native_gate_path_determinism bit 2 lifts the budget and every agent gets a full update
 * every tick (about 9,800 in the play stack; the pass is 3.3 ms on this PC and much
 * more on a loaded machine, where the join stalls the MoveWorker). This keeps stock's
 * round-robin and replaces the clock with a count: after N full updates in a batch the
 * remaining agents of that pass are cheap. The decision then follows the batch list and its
 * cursor, which must agree between clients (PATH_DETERMINISM walk= shows them per tick). A
 * flagged batch keeps stock's guarantee of its first 101 agents.
 *
 * The clock block 0xeaeda6..0xeaedfb becomes: compare the loop index with N-1, then the
 * block's own two exits (stay full at 0xeaee09, turn cheap at 0xeaedfe). Fixture agents
 * keep native_gate_agent_full. The block is pinned whole, written with every other thread
 * suspended and none inside it, checked every gate tick (stop reason 176) and restored
 * at stop; other modules' pins over UpdateBatch read the original bytes
 * (lockstep_gate.c fp_patch_view). */
#define PBUD_BLOCK 0xeaeda6u
#define PBUD_SIZE 0x56u
#define PBUD_NEXT 0xeaee09u
#define PBUD_CHEAP 0xeaedfeu
#define PBUD_SHA "3ae2bb137978a0e5817c2fd42abb8c2911841298ce23338f302e625c72042dc6"
#define PBUD_STOP_REASON 176u /* 175 macro arena */
#define PBUD_MAX 100000u
static unsigned char pbud_original[PBUD_SIZE],pbud_ours[PBUD_SIZE];
static unsigned pbud_n,pbud_busy;
static int pbud_installed,pbud_restored;
static volatile LONG pbud_any;
static volatile LONG64 pbud_failures;
static const char *pbud_stage="idle";
/* cmp rsi,N-1; jb next; jmp cheap; int3 fill. All in one function: rel32 always reaches. */
static void pbud_emit(unsigned char out[PBUD_SIZE],unsigned n,int32_t to_next,int32_t to_cheap) {
    static const unsigned char compare[3]={0x48,0x81,0xfe},below[2]={0x0f,0x82};
    int32_t v=(int32_t)(n-1u),a=to_next-13,b=to_cheap-18;memset(out,0xcc,PBUD_SIZE);
    memcpy(out,compare,3);memcpy(out+3,&v,4);memcpy(out+7,below,2);memcpy(out+9,&a,4);out[13]=0xe9;memcpy(out+14,&b,4);
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int pbud_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&pbud_any || !image || !source || !bytes) return 0;
    const unsigned char *p=source,*at=image+PBUD_BLOCK;
    return p<at+PBUD_SIZE && p+bytes>at;
}
static void pbud_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!pbud_overlaps(source,bytes)) return;
    const unsigned char *p=source,*at=image+PBUD_BLOCK;
    size_t lo=at<p?(size_t)(p-at):0,hi=at+PBUD_SIZE>p+bytes?(size_t)(p+bytes-at):PBUD_SIZE;
    unsigned char *c=(unsigned char*)copy+(at+lo-p);
    if(!memcmp(c,pbud_ours+lo,hi-lo)) memcpy(c,pbud_original+lo,hi-lo);
}
static int pbud_block_clear(void) {
    uintptr_t lo=(uintptr_t)(image+PBUD_BLOCK),hi=lo+PBUD_SIZE;
    for(unsigned t=0;t<pmath_frozen_count;t++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[t],&c)) return 0;
        if(c.Rip>=lo && c.Rip<hi) return 0;
    }
    return 1;
}
/* The block from one state to the other, every other thread suspended and none inside. */
static int pbud_switch(const unsigned char *from,const unsigned char *to) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) return 0;
        if(!pbud_block_clear()) {pmath_thaw();++pbud_busy;Sleep(1);continue;}
        unsigned char *at=image+PBUD_BLOCK;DWORD old,ignored;int ok=0;
        if(!memcmp(at,from,PBUD_SIZE) && VirtualProtect(at,PBUD_SIZE,PAGE_EXECUTE_READWRITE,&old)) {
            memcpy(at,to,PBUD_SIZE);
            ok=VirtualProtect(at,PBUD_SIZE,old,&ignored) && FlushInstructionCache(GetCurrentProcess(),at,PBUD_SIZE);
        }
        pmath_thaw();return ok;
    }
    return 0;
}
/* gate.path_budget(N): the UI thread, before the first admitted frame, with the serial
 * pathing join (the cursor must not move while the MoveWorker reads). */
static int pbud_arm(unsigned n) {
    if(pbud_installed) return stats.active && n==pbud_n;
    if(!n || n>PBUD_MAX || !stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !path_installed || !(path_options&1)) {pbud_stage="state";return 0;}
    if(!hash_ok(image+PBUD_BLOCK,PBUD_SIZE,PBUD_SHA)) {pbud_stage="pins";return 0;}
    memcpy(pbud_original,image+PBUD_BLOCK,PBUD_SIZE);
    pbud_emit(pbud_ours,n,(int32_t)(PBUD_NEXT-PBUD_BLOCK),(int32_t)(PBUD_CHEAP-PBUD_BLOCK));
    InterlockedExchange(&pbud_any,1);
    if(!pbud_switch(pbud_original,pbud_ours)) {InterlockedExchange(&pbud_any,0);pbud_stage="busy";return 0;}
    pbud_n=n;pbud_failures=0;pbud_restored=0;pbud_installed=1;pbud_stage="armed";return 1;
}
static int pbud_owned(void) {return !pbud_installed || !memcmp(image+PBUD_BLOCK,pbud_ours,PBUD_SIZE);}
static int pbud_check(void) {
    if(InterlockedCompareExchange64(&pbud_failures,0,0)) return 0;
    if(!pbud_owned()) {pbud_stage="site_changed";InterlockedIncrement64(&pbud_failures);return 0;}
    return 1;
}
static void pbud_restore(void) {
    if(!pbud_installed) return;
    if(!pbud_owned()) {pbud_stage="restore_foreign";return;}
    if(!pbud_switch(pbud_ours,pbud_original)) {pbud_stage="stuck";InterlockedIncrement64(&pbud_failures);return;}
    InterlockedExchange(&pbud_any,0);pbud_installed=0;pbud_restored=1;pbud_stage="restored";
}
static int pbud_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d n=%u restored=%d stage=%s busy=%u failures=%lld",pbud_installed,pbud_n,pbud_restored,pbud_stage,pbud_busy,(long long)pbud_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
