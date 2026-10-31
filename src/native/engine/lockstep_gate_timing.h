/* Shared frame wall-time breakdown (gate.frame_timing). Needs LARGE_INTEGER and
 * QueryPerformanceFrequency (Windows, or lockstep_platform_linux.h). */
/* Per-frame wall-time breakdown (QPC): waiting for the relay permit, the
 * original FrameTick, and everything outside the gate until the next tick.
 * Of outside: pre = the gate's own work from its entry to the permit wait (checks,
 * created identities, the mass traffic/octree/region joins); created and join are
 * parts of pre. outside - pre is the engine and the UI (probe included). pace is the
 * hold before FrameTick (native_gate_tick_hz); wait and frame do not include it. */
static uint64_t ft_wait,ft_frame,ft_outside,ft_sum_wait,ft_sum_frame,ft_sum_outside,ft_frames,ft_last_end;
static uint64_t ft_pre,ft_created,ft_join,ft_sum_pre;
/* ReadProcessMemory calls (read_local, every thread) between two gate entries: each is a
 * system call (~0.3 us offline). Counted without a lock: approximate under contention. */
static uint64_t read_local_calls,ft_reads,ft_reads_last;
static uint64_t ft_us(LARGE_INTEGER a,LARGE_INTEGER b) {
    static LARGE_INTEGER f;if(!f.QuadPart) QueryPerformanceFrequency(&f);
    return f.QuadPart?(uint64_t)((b.QuadPart-a.QuadPart)*1000000/f.QuadPart):0;
}
static int frame_timing_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"frames=%llu wait_us=%llu frame_us=%llu outside_us=%llu sum_wait_us=%llu sum_frame_us=%llu sum_outside_us=%llu"
        " pre_us=%llu created_us=%llu join_us=%llu sum_pre_us=%llu reads=%llu pace_us=%llu sum_pace_us=%llu sim_us=%llu",
        (unsigned long long)ft_frames,(unsigned long long)ft_wait,(unsigned long long)ft_frame,(unsigned long long)ft_outside,
        (unsigned long long)ft_sum_wait,(unsigned long long)ft_sum_frame,(unsigned long long)ft_sum_outside,
        (unsigned long long)ft_pre,(unsigned long long)ft_created,(unsigned long long)ft_join,(unsigned long long)ft_sum_pre,
        (unsigned long long)ft_reads,(unsigned long long)gate_pace_us,(unsigned long long)gate_pace_sum_us,(unsigned long long)gate_sim_us);
    return n>0 && (size_t)n<capacity?n:0;
}
