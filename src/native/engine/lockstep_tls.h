/* Per-thread engine words used for RNG keying, by platform. Every X4 thread owns
 * a thread RNG word (the state Random::U64State steps: Windows 0x14ddb30, Linux
 * 0x1a48590; seeded by 0x14dd110 / 0x1a47fe0), CurTime (the game time this thread
 * sees, synced at the start of every MoveWorker iteration) and a component view
 * selector (MoveWorker and the Pathing Manager set it to 1 at thread start).
 *   Windows: TEB+0x58 -> TLS slot 0; RNG +0x318, CurTime +0x300, view byte +0x314.
 *   Linux:   static TLS of the ET_EXEC image (local-exec, fixed thread-pointer
 *            offsets); RNG fs:-0x2720, CurTime fs:-0x2728, view dword fs:-0x2620.
 * The Linux offsets are pinned through the code that uses them (linux/gate_sites.h:
 * thread_rng_seed, move_worker_run_entry, move_worker_frame_start) and the image's
 * PT_TLS segment. A base is the calling thread's own; another engine thread's
 * words may be touched through its recorded base only while that thread is
 * parked at a frame boundary. Module ports use these accessors instead of
 * reading the TEB or %fs inline. */
#define GATE_TLS_LINUX_RNG (-0x2720)
#define GATE_TLS_LINUX_TIME (-0x2728)
#define GATE_TLS_LINUX_PREVIOUS_TIME (-0x2628)
#define GATE_TLS_LINUX_VIEW (-0x2620)
#define GATE_TLS_WINDOWS_RNG 0x318
#define GATE_TLS_WINDOWS_TIME 0x300
#define GATE_TLS_WINDOWS_VIEW 0x314
#if defined(_WIN32)
static inline unsigned char *gate_tls_self(void) {
    void **slots=*(void***)((unsigned char*)NtCurrentTeb()+0x58);
    return slots?(unsigned char*)slots[0]:NULL;
}
static inline uint64_t *gate_tls_rng(unsigned char *base) {return base?(uint64_t*)(base+GATE_TLS_WINDOWS_RNG):NULL;}
static inline double *gate_tls_time(unsigned char *base) {return base?(double*)(base+GATE_TLS_WINDOWS_TIME):NULL;}
static inline int gate_tls_worker(unsigned char *base) {return base && base[GATE_TLS_WINDOWS_VIEW]==1;}
#else
/* The base is the thread pointer: glibc keeps the TCB self pointer at fs:0. */
#ifdef GATE_TLS_TEST
static unsigned char *gate_tls_test_base(void);
static inline unsigned char *gate_tls_self(void) {return gate_tls_test_base();}
#else
static inline unsigned char *gate_tls_self(void) {
    unsigned char *tp;__asm__("mov %%fs:0,%0":"=r"(tp));return tp;
}
#endif
static inline uint64_t *gate_tls_rng(unsigned char *base) {return base?(uint64_t*)(base+GATE_TLS_LINUX_RNG):NULL;}
static inline double *gate_tls_time(unsigned char *base) {return base?(double*)(base+GATE_TLS_LINUX_TIME):NULL;}
static inline int gate_tls_worker(unsigned char *base) {
    int32_t view;if(!base) return 0;memcpy(&view,base+GATE_TLS_LINUX_VIEW,4);return view==1;
}
#endif
