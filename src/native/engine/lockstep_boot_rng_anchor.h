/* Census experiment, DEFAULT OFF. World017 showed that the UI-thread TLS RNG
 * word already differs between clients when the first post-load AI script runs
 * (boot2/tick0: dfe99df10abdcbeb versus 3187bfc8c9b3c08e), so every natural
 * script draw after it diverges. Instead of scoping individual draws, anchor
 * that one word exactly once, at the first AI script entry on the UI thread
 * during boot phase 2. Later draws run naturally from the anchored state; there
 * is no restoration. The prior word, entry kind and timing are kept as a
 * receipt. This does not touch other threads' TLS words or object RNG state. */
enum {BOOT_ANCHOR_RUN=1,BOOT_ANCHOR_EVENT=2,BOOT_ANCHOR_ACTION=3};
static volatile LONG boot_rng_anchor_armed;
static int boot_rng_anchor_applied;
static unsigned boot_rng_anchor_kind,boot_rng_anchor_mask=7;
static DWORD boot_rng_anchor_thread;
static uint64_t boot_rng_anchor_seed,boot_rng_anchor_before,boot_rng_anchor_elapsed;
static volatile LONG64 boot_rng_anchor_failures,boot_rng_anchor_late;
static void boot_rng_anchor_apply(unsigned kind) {
    if(!boot_rng_anchor_armed || boot_rng_anchor_applied || !(boot_rng_anchor_mask&(1u<<(kind-1)))) return;
    if(!stats.active || !boot_mode || GetCurrentThreadId()!=ui_thread) return;
    if(boot_phase!=2 || stats.issued) {
        /* Entries after load without a prior anchor mean the anchor point was missed. */
        if(boot_phase==3) InterlockedIncrement64(&boot_rng_anchor_late);
        return;
    }
    uint64_t *rng=interrupt_rng();
    if(!rng) {InterlockedIncrement64(&boot_rng_anchor_failures);return;}
    ULONGLONG now=GetTickCount64();
    boot_rng_anchor_before=*rng;*rng=boot_rng_anchor_seed;
    if(*rng!=boot_rng_anchor_seed) {InterlockedIncrement64(&boot_rng_anchor_failures);return;}
    boot_rng_anchor_kind=kind;boot_rng_anchor_thread=GetCurrentThreadId();
    boot_rng_anchor_elapsed=now>=boot_started?now-boot_started:UINT64_MAX;boot_rng_anchor_applied=1;
}
/* mask bit0 script run, bit1 event, bit2 action. c002 anchored at the first
 * event: clients then made 2448 versus 2452 draws before the first action. */
static int boot_rng_anchor_arm(uint64_t seed,unsigned mask) {
    /* The probe's configuration block reruns after a loading-UI reload; accept
     * only that exact repeat once armed, never a first arm after load began. */
    if(boot_rng_anchor_armed) return stats.active && boot_rng_anchor_seed==seed && boot_rng_anchor_mask==mask;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread || !seed || !mask || mask>7) return 0;
    boot_rng_anchor_seed=seed;boot_rng_anchor_mask=mask;InterlockedExchange(&boot_rng_anchor_armed,1);return 1;
}
static int boot_rng_anchor_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%ld mask=%u applied=%d kind=%u seed=%016llx before=%016llx thread=%lu elapsed=%llu failures=%lld late=%lld",
        boot_rng_anchor_armed,boot_rng_anchor_mask,boot_rng_anchor_applied,boot_rng_anchor_kind,(unsigned long long)boot_rng_anchor_seed,
        (unsigned long long)boot_rng_anchor_before,(unsigned long)boot_rng_anchor_thread,(unsigned long long)boot_rng_anchor_elapsed,
        (long long)boot_rng_anchor_failures,(long long)boot_rng_anchor_late);
    return n>0 && (size_t)n<capacity?n:0;
}
