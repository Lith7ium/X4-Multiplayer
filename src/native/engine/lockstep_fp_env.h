/* Determinism fix, DEFAULT OFF: native_gate_fp_env = 'default' or 'observe'.
 *
 * X4::GameClient::Init 0x1223ce0 calls 0x14dd0e0 (at 0x1225ee7), which sets MXCSR
 * flush-to-zero on the main thread (MainMessageLoop) only; threads started later
 * begin with the default MXCSR and run X4::ThreadStartHook 0x96350 through
 * g_ThreadStartHook 0x38f9d58 (called by the thread procedure 0x14b7740). The Linux
 * build never writes MXCSR. Main-thread arithmetic that underflows therefore flushes
 * on Windows and not on Linux, and any driver or overlay that changes a thread's
 * MXCSR changes its results (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt,
 * class E). Live at arm: values=1f80:42,9f80:1 with the UI thread on 1f80, so the one
 * 9f80 is, by inference, the main thread.
 *
 * 'default': every X4 thread computes under MXCSR control 0x1f80 (round to
 * nearest, FTZ and DAZ off, exceptions masked), the value Linux uses:
 *  - at arm (UI thread, before the first admitted frame): the UI thread directly,
 *    every other thread whose start address lies in X4.exe through
 *    SetThreadContext while all other threads are suspended (flags kept);
 *  - threads started later: g_ThreadStartHook points to a wrapper that runs
 *    X4::ThreadStartHook and then sets the value;
 *  - every gate frame, on the thread that runs the gate hook (the main thread; the
 *    MoveWorker has joined), before the frame: set again if anything changed it
 *    (repairs_ui);
 *  - the first admitted frame and every 600 ticks after: all other X4 threads, the UI
 *    thread among them, read under suspension, deviations set again (repairs_worker).
 * 'observe': nothing written; the MXCSR values of the X4 threads at arm, at the
 * first admitted frame and every 600 ticks are counted.
 * Pins: X4::ThreadStartHook, the thread procedure and the FTZ setter. Stop reason
 * 158 if g_ThreadStartHook changes. Restored at stop: the hook pointer, the UI
 * thread's MXCSR and the recorded values of the threads that still run. */
#define FPENV_STOP_REASON 158u
#define FPENV_HOOK_PTR 0x38f9d58u
#define FPENV_HOOK_FN 0x96350u
#define FPENV_TARGET 0x1f80u
#define FPENV_CONTROL 0xffc0u
#define FPENV_SAMPLE_TICKS 600u
#define FPENV_MAX_RECORDS 1024u
enum {FPENV_OFF,FPENV_DEFAULT,FPENV_OBSERVE};
typedef void (*fpenv_hook_fn)(void);
typedef struct {DWORD id;unsigned before;} fpenv_record;
static int fpenv_mode,fpenv_installed,fpenv_restored;
static unsigned fpenv_ui_before,fpenv_threads,fpenv_set_at_arm,fpenv_mismatch_at_arm,fpenv_record_count;
static fpenv_record fpenv_records[FPENV_MAX_RECORDS];
static uint64_t fpenv_last_sample;
static volatile LONG64 fpenv_repairs_ui,fpenv_repairs_worker,fpenv_started,fpenv_samples,fpenv_failures;
static unsigned fpenv_values[8],fpenv_counts[8],fpenv_value_kinds;
static const char *fpenv_stage="idle";
static unsigned fpenv_getcsr(void) {unsigned c;__asm__ __volatile__("stmxcsr %0" : "=m"(c) : : "memory");return c;}
static void fpenv_setcsr(unsigned c) {__asm__ __volatile__("ldmxcsr %0" : : "m"(c) : "memory");}
/* Control bits to the target, sticky flags kept. */
static unsigned fpenv_fixed(unsigned csr) {return FPENV_TARGET|(csr&0x3fu);}
static void fpenv_count_value(unsigned csr) {
    unsigned v=csr&FPENV_CONTROL;
    for(unsigned i=0;i<fpenv_value_kinds;i++) if(fpenv_values[i]==v) {++fpenv_counts[i];return;}
    if(fpenv_value_kinds<8) {fpenv_values[fpenv_value_kinds]=v;fpenv_counts[fpenv_value_kinds++]=1;}
}
/* g_ThreadStartHook replacement: X4's own hook first, then the MXCSR. */
static void fpenv_thread_start(void) {
    ((fpenv_hook_fn)(void *)(image+FPENV_HOOK_FN))();
    fpenv_setcsr(fpenv_fixed(fpenv_getcsr()));InterlockedIncrement64(&fpenv_started);
}
static int fpenv_pins(void) {
    return hash_ok(image+FPENV_HOOK_FN,0x51,"751bc3b0baf522294d6d0267de55e00ac2f3a5b86471977f4b9ddf4427ed88f8") &&
           hash_ok(image+0x14b7740,0x7d,"cc0621b1d70386cded5da0181270d6ac97940f66e3c6f068b12fe62fc7093ba9") &&
           hash_ok(image+0x14dd0e0,0x2f,"4e854bc748c147ca62476657c7ffa9208b1ee62cf28b89c8729ef2e5592ad72f");
}
/* A thread of X4.exe: its Win32 start address lies in the image. */
static int fpenv_is_x4_thread(HANDLE thread) {
    typedef LONG (WINAPI *query_fn)(HANDLE,int,void *,ULONG,ULONG *);
    static query_fn query;
    if(!query) query=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    void *start=NULL;
    if(!query || query(thread,9 /* ThreadQuerySetWin32StartAddress */,&start,sizeof(start),NULL)<0) return 0;
    return (unsigned char*)start>=image && (unsigned char*)start<image+0x6fa9000;
}
/* With the other threads suspended (pmath_freeze): read, and in 'default' set, each X4
 * thread's MXCSR. record: keep the values found (at arm, for restore). */
static unsigned fpenv_visit(int write,int record) {
    unsigned changed=0;fpenv_threads=0;
    for(unsigned t=0;t<pmath_frozen_count;t++) {
        if(!fpenv_is_x4_thread(pmath_frozen[t])) continue;
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL|CONTEXT_FLOATING_POINT;
        if(!GetThreadContext(pmath_frozen[t],&c)) {InterlockedIncrement64(&fpenv_failures);continue;}
        ++fpenv_threads;fpenv_count_value(c.MxCsr);
        if(record && fpenv_record_count<FPENV_MAX_RECORDS) fpenv_records[fpenv_record_count++]=(fpenv_record){pmath_frozen_ids[t],c.MxCsr};
        if(!write || (c.MxCsr&FPENV_CONTROL)==FPENV_TARGET) continue;
        c.MxCsr=fpenv_fixed(c.MxCsr);c.FltSave.MxCsr=c.MxCsr;c.ContextFlags=CONTEXT_FLOATING_POINT;
        if(SetThreadContext(pmath_frozen[t],&c)) ++changed;else InterlockedIncrement64(&fpenv_failures);
    }
    return changed;
}
static int fpenv_hook_state(void) {
    void *now=NULL;if(!read_local(image+FPENV_HOOK_PTR,&now,8)) return -1;
    if(now==(void*)(image+FPENV_HOOK_FN)) return 0;
    return now==(void*)fpenv_thread_start?1:-1;
}
/* gate.fp_env(mode): UI thread, before the first admitted frame. */
static int fpenv_arm(int mode) {
    if(fpenv_installed) return stats.active && fpenv_mode==mode;
    if(mode!=FPENV_DEFAULT && mode!=FPENV_OBSERVE) return 0;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {fpenv_stage="state";return 0;}
    if(!fpenv_pins() || fpenv_hook_state()!=0) {fpenv_stage="pins";return 0;}
    fpenv_value_kinds=0;fpenv_record_count=0;fpenv_failures=0;fpenv_restored=0;
    fpenv_ui_before=fpenv_getcsr();fpenv_count_value(fpenv_ui_before);
    if(!pmath_freeze()) {fpenv_stage="freeze";return 0;}
    fpenv_set_at_arm=fpenv_visit(mode==FPENV_DEFAULT,1);
    int hooked=mode!=FPENV_DEFAULT ||
        InterlockedCompareExchangePointer((void *volatile*)(image+FPENV_HOOK_PTR),(void*)fpenv_thread_start,(void*)(image+FPENV_HOOK_FN))==(void*)(image+FPENV_HOOK_FN);
    pmath_thaw();
    if(!hooked) {fpenv_stage="hook";return 0;}
    fpenv_mismatch_at_arm=0;for(unsigned i=0;i<fpenv_value_kinds;i++) if(fpenv_values[i]!=FPENV_TARGET) fpenv_mismatch_at_arm+=fpenv_counts[i];
    if(mode==FPENV_DEFAULT) fpenv_setcsr(fpenv_fixed(fpenv_ui_before));
    fpenv_mode=mode;fpenv_installed=1;fpenv_last_sample=0;fpenv_stage="armed";return 1;
}
/* Gate frame, on the hook's thread (main thread; the MoveWorker has joined), before the
 * frame: this thread's value, and every 600 ticks the other X4 threads. 0 stops the gate
 * (the hook pointer changed). */
static int fpenv_tick(uint64_t tick) {
    if(!fpenv_installed) return 1;
    if(fpenv_mode==FPENV_DEFAULT) {
        if(fpenv_hook_state()!=1) {fpenv_stage="hook_changed";InterlockedIncrement64(&fpenv_failures);return 0;}
        unsigned csr=fpenv_getcsr();
        if((csr&FPENV_CONTROL)!=FPENV_TARGET) {fpenv_setcsr(fpenv_fixed(csr));InterlockedIncrement64(&fpenv_repairs_ui);}
    }
    /* The first admitted frame, then every 600 ticks: no suspension during the load, whose
     * asset loader race is timing sensitive (lockstep_shape_order.h). */
    if(tick && (!InterlockedCompareExchange64(&fpenv_samples,0,0) || tick>=fpenv_last_sample+FPENV_SAMPLE_TICKS)) {
        fpenv_last_sample=tick;
        if(pmath_freeze()) {
            unsigned changed=fpenv_visit(fpenv_mode==FPENV_DEFAULT,0);pmath_thaw();
            InterlockedAdd64(&fpenv_repairs_worker,changed);InterlockedIncrement64(&fpenv_samples);
        }
    }
    return 1;
}
static int fpenv_owned(void) {return !fpenv_installed || fpenv_mode!=FPENV_DEFAULT || fpenv_hook_state()==1;}
/* The recorded control bits back on the threads that still run (only those arm changed),
 * the calling thread (the main thread when the gate stops in its hook) directly. */
static unsigned fpenv_restore_threads(void) {
    unsigned restored=0;
    for(unsigned r=0;r<fpenv_record_count;r++) if(fpenv_records[r].id==GetCurrentThreadId() && (fpenv_records[r].before&FPENV_CONTROL)!=FPENV_TARGET) {
        fpenv_setcsr((fpenv_getcsr()&0x3fu)|(fpenv_records[r].before&FPENV_CONTROL));++restored;
    }
    if(!pmath_freeze()) return restored;
    for(unsigned t=0;t<pmath_frozen_count;t++) for(unsigned r=0;r<fpenv_record_count;r++) {
        if(fpenv_records[r].id!=pmath_frozen_ids[t] || (fpenv_records[r].before&FPENV_CONTROL)==FPENV_TARGET) continue;
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_FLOATING_POINT;
        if(GetThreadContext(pmath_frozen[t],&c)) {
            c.MxCsr=(c.MxCsr&0x3fu)|(fpenv_records[r].before&FPENV_CONTROL);c.FltSave.MxCsr=c.MxCsr;
            if(SetThreadContext(pmath_frozen[t],&c)) ++restored;
        }
    }
    pmath_thaw();return restored;
}
static void fpenv_restore(void) {
    if(!fpenv_installed) return;
    if(fpenv_mode==FPENV_DEFAULT) {
        if(InterlockedCompareExchangePointer((void *volatile*)(image+FPENV_HOOK_PTR),(void*)(image+FPENV_HOOK_FN),(void*)fpenv_thread_start)!=(void*)fpenv_thread_start) {
            fpenv_stage="restore_foreign";return;
        }
        if(GetCurrentThreadId()==ui_thread) fpenv_setcsr((fpenv_getcsr()&0x3fu)|(fpenv_ui_before&FPENV_CONTROL));
        fpenv_restore_threads();
    }
    fpenv_installed=0;fpenv_restored=1;fpenv_stage="restored";
}
static int fpenv_status(char *out,size_t capacity) {
    char values[160];int k=0;values[0]=0;
    for(unsigned i=0;i<fpenv_value_kinds && k<(int)sizeof(values)-16;i++)
        k+=snprintf(values+k,sizeof(values)-(size_t)k,"%s%04x:%u",i?",":"",fpenv_values[i],fpenv_counts[i]);
    int n=snprintf(out,capacity,"installed=%d mode=%s ui_before=%04x threads=%u values=%s set_at_arm=%u mismatch_at_arm=%u "
        "started=%lld repairs_ui=%lld repairs_worker=%lld samples=%lld restored=%d stage=%s failures=%lld",
        fpenv_installed,fpenv_mode==FPENV_DEFAULT?"default":fpenv_mode==FPENV_OBSERVE?"observe":"off",fpenv_ui_before,fpenv_threads,
        fpenv_value_kinds?values:"-",fpenv_set_at_arm,fpenv_mismatch_at_arm,(long long)fpenv_started,(long long)fpenv_repairs_ui,
        (long long)fpenv_repairs_worker,(long long)fpenv_samples,fpenv_restored,fpenv_stage,(long long)fpenv_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
