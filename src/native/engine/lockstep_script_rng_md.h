/* Direct MD cue condition checks on per-cue streams, DEFAULT OFF
 * (native_gate_md_timed_rng; needs native_gate_script_rng with the MD bit).
 *
 * MD::Cue_CheckConditions 0x111c290 (argument: the cue; no result) draws the
 * sim thread's TLS PCG once per condition that carries a chance. The script
 * RNG streams already scope its MD::Cue slot-565 tail jump 0x111f990 (SR_CUE).
 * Its only other callers are three direct E8 rel32 calls:
 *   0x112208b MDScheduler_Update 0x1121f80, due heap entry of kind 1: the
 *             timed check of a non-event cue, also during MD start-up in
 *             GameClass::Load and the post-load universe update   SR_TIMED
 *   0x111d296 Cue_EnterWaiting 0x111d090: an event cue that enters the
 *             waiting state checks itself once (record 0x636)     SR_WAIT
 *   0x111e505 Cue_RaiseCueEvent 0x111e370: one call per listener cue of a cue
 *             event (0x633 activated, 0x637 signalled, ...)        SR_RAISE
 * Each rel32 is redirected through THUNK_JUMP 2016 to script_rng_md_check by
 * one 16-byte CAS over the call's aligned 16-byte block (instruction
 * boundaries and every other byte unchanged). The return address names the
 * call site and therefore the source. The scope is the SR_CUE one of
 * lockstep_script_rng.h: owner = FNV-1a of the static "module.cue" name,
 * detail = the cue's own event record (+0x18 keyword, saved ID of a component
 * parameter; the raising cue's runtime ID, value type 0x12, never enters),
 * per-tick ordinal per (owner, stream, detail). Nesting follows the same
 * rules: the same cue continues its stream, another cue checked inside a keyed
 * scope (a listener of the cue that is running) gets its own context-free key
 * and the parent's word is restored afterwards. Before this, timed checks drew
 * from the shared natural stream and listener checks from their parent's.
 * A return address that is none of the three sites runs unscoped and is a
 * failure. Failures stop the gate with reason 86; stop restores the calls. */
#ifndef SCRIPT_RNG_OWN_PLATFORM
#define MD_TIMED_CHECK 0x111c290u
#define MD_TIMED_THUNK 2016u
#endif
#define MD_TIMED_SITES 3u
#ifndef SCRIPT_RNG_OWN_PLATFORM
static const uint32_t md_timed_site[MD_TIMED_SITES]={0x112208bu,0x111d296u,0x111e505u};
#endif
static const unsigned md_timed_source[MD_TIMED_SITES]={SR_TIMED,SR_WAIT,SR_RAISE};
static int md_timed_armed,md_timed_restored,md_timed_installed[MD_TIMED_SITES];
static unsigned __int128 md_timed_original[MD_TIMED_SITES],md_timed_patched[MD_TIMED_SITES];
static volatile LONG64 md_timed_calls[MD_TIMED_SITES],md_timed_foreign,md_timed_failures;

/* Index of the site whose return address this is, MD_TIMED_SITES if none. */
static unsigned md_timed_site_of(uintptr_t back) {
    for(unsigned i=0;i<MD_TIMED_SITES;i++) if(back==(uintptr_t)(image+md_timed_site[i]+5)) return i;
    return MD_TIMED_SITES;
}
static void md_timed_run(uintptr_t back,script_rng_event_fn original,uint64_t *rng,void *cue) {
    unsigned i=md_timed_site_of(back);
    if(i==MD_TIMED_SITES) {
        InterlockedIncrement64(&md_timed_foreign);InterlockedIncrement64(&md_timed_failures);
        original(cue,NULL,NULL,NULL);return;
    }
    InterlockedIncrement64(&md_timed_calls[i]);
    script_rng_event_run(md_timed_source[i],original,rng,cue,NULL,NULL,NULL);
}
/* noinline: the return address is the native caller's (the thunk jumps). */
static __attribute__((noinline)) void script_rng_md_check(void *cue) {
    md_timed_run((uintptr_t)__builtin_return_address(0),(script_rng_event_fn)(image+MD_TIMED_CHECK),interrupt_rng(),cue);
}
/* The Linux gate defines its own pins, swap and call rewrite (linux/gate_script_rng.h). */
#ifdef SCRIPT_RNG_OWN_PLATFORM
static int md_timed_pinned(void);
static int md_timed_swap(unsigned i,unsigned __int128 expected,unsigned __int128 value);
static int md_timed_prepare(unsigned i);
#else
/* The three callers and the callee, unpatched. */
static int md_timed_pinned(void) {
    return hash_ok(image+0x1121f80,394,"1a73fd8bd775643c09f921a2b0e956b23ff8d934f524067466f34b93d3e2b8d1") &&
        hash_ok(image+0x111d090,710,"99e01e788a0b995d5e99f85a0cbfd835e190fd3199e625636b9445f97a78d3f7") &&
        hash_ok(image+0x111e370,520,"d0662b4ba856fe88f7f78f5a188c19a929606a772b12a940022f841c6f007e5b") &&
        hash_ok(image+MD_TIMED_CHECK,712,"5f9346c838fde113044bdca972ea8c18f13dadf33fb48dde50e69664ac5688d1");
}
static int md_timed_swap(unsigned i,unsigned __int128 expected,unsigned __int128 value) {
    unsigned char *block=image+(md_timed_site[i]&~15u);DWORD old,ignored;
    if(!VirtualProtect(block,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 actual=__sync_val_compare_and_swap((unsigned __int128*)block,expected,value);
    VirtualProtect(block,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),block,16);
    return actual==expected;
}
/* Original and patched block of site i: E8 to 0x111c290 becomes E8 to the thunk. */
static int md_timed_prepare(unsigned i) {
    uint32_t site=md_timed_site[i],at=site&15u;unsigned char bytes[16];int32_t rel;
    if(at>11 || !read_local(image+(site&~15u),bytes,16) || bytes[at]!=0xe8) return 0;
    memcpy(&rel,bytes+at+1,4);
    if((uintptr_t)(image+site+5)+(intptr_t)rel!=(uintptr_t)(image+MD_TIMED_CHECK)) return 0;
    intptr_t distance=(intptr_t)(thunk+MD_TIMED_THUNK)-(intptr_t)(image+site+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    memcpy(&md_timed_original[i],bytes,16);rel=(int32_t)distance;memcpy(bytes+at+1,&rel,4);memcpy(&md_timed_patched[i],bytes,16);
    return 1;
}
#endif
static int md_timed_unswap(void) {
    int ok=1;
    for(unsigned i=0;i<MD_TIMED_SITES;i++) if(md_timed_installed[i]) {
        if(md_timed_swap(i,md_timed_patched[i],md_timed_original[i])) md_timed_installed[i]=0;else ok=0;
    }
    return ok;
}
static int md_timed_owned(void) {
    for(unsigned i=0;i<MD_TIMED_SITES;i++)
        if(md_timed_installed[i] && memcmp(image+(md_timed_site[i]&~15u),&md_timed_patched[i],16)) return 0;
    return 1;
}
static unsigned md_timed_sites_installed(void) {
    unsigned n=0;for(unsigned i=0;i<MD_TIMED_SITES;i++) n+=md_timed_installed[i]!=0;
    return n;
}
/* Armed once at boot, before load, after the script RNG (idempotent on a
 * loading-UI reload). Refuses without the script RNG MD bit, the thunk slot
 * or any pin; a partial install is rolled back. */
static int md_timed_arm(void) {
    if(md_timed_armed) return stats.active && md_timed_sites_installed()==MD_TIMED_SITES;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!script_rng_armed || !(script_rng_mask&SCRIPT_RNG_MD) || !thunk || !md_timed_pinned() || !thunk_slot_ready(MD_TIMED_THUNK)) return 0;
    for(unsigned i=0;i<MD_TIMED_SITES;i++) if(!md_timed_prepare(i)) return 0;
    for(unsigned i=0;i<MD_TIMED_SITES;i++) {
        if(!md_timed_swap(i,md_timed_original[i],md_timed_patched[i])) {md_timed_unswap();return 0;}
        md_timed_installed[i]=1;
    }
    md_timed_armed=1;return 1;
}
/* Scopes already pass through once the script RNG is inactive; this only
 * gives the three calls back. A foreign owner keeps the stats unrestored. */
static void md_timed_restore(void) {
    if(!md_timed_armed) return;
    md_timed_restored=md_timed_unswap();
}
static int md_timed_status(char *out,size_t cap) {
    int n=snprintf(out,cap,"armed=%d sites=%u timed_calls=%lld wait_calls=%lld raise_calls=%lld foreign=%lld restored=%d failures=%lld",
        md_timed_armed,md_timed_sites_installed(),(long long)md_timed_calls[0],(long long)md_timed_calls[1],(long long)md_timed_calls[2],
        (long long)md_timed_foreign,md_timed_restored,(long long)md_timed_failures);
    return n>0 && (size_t)n<cap?n:0;
}
