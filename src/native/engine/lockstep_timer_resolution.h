/* Performance option, DEFAULT OFF. Gate helpers (Gravidar ready waits, spline
 * result waits) poll with Sleep(1), which sleeps a whole default timer period
 * (15.625 ms) per call; census perf-Pmin frames were quantised to 47/78 ms.
 * While the gate is active, request a 1 ms process timer period through
 * winmm's timeBeginPeriod (loaded dynamically, no link change) and release it
 * when the gate finishes. Wall-clock waits only; simulated time stays gated. */
typedef unsigned (WINAPI *timer_period_fn)(unsigned);
static timer_period_fn timer_end_period;
static unsigned timer_period_ms;
static int timer_period_active;
static int timer_resolution_arm(unsigned ms) {
    if(timer_period_active) return timer_period_ms==ms;
    if(!stats.active || ms<1 || ms>15 || GetCurrentThreadId()!=ui_thread) return 0;
    HMODULE winmm=LoadLibraryW(L"winmm.dll");if(!winmm) return 0;
    timer_period_fn begin=(timer_period_fn)(void *)GetProcAddress(winmm,"timeBeginPeriod");
    timer_end_period=(timer_period_fn)(void *)GetProcAddress(winmm,"timeEndPeriod");
    if(!begin || !timer_end_period || begin(ms)) return 0; /* TIMERR_NOERROR is 0 */
    timer_period_ms=ms;timer_period_active=1;return 1;
}
static void timer_resolution_restore(void) {
    if(timer_period_active && timer_end_period && !timer_end_period(timer_period_ms)) timer_period_active=0;
}
static int timer_resolution_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"active=%d ms=%u",timer_period_active,timer_period_ms);
    return n>0 && (size_t)n<capacity?n:0;
}
