/* The simulation tick rate, shared by the Windows and the Linux gate.
 *
 * native_gate_tick_hz (gate.tick_rate before the start; default 60, also 40, 30, 20):
 * every admitted frame advances game and real time by exactly dt = 1/hz, the bits of the
 * double division, the same on every client. The world runs at real time when a tick
 * costs at most 1/hz, and the display rate equals the tick rate (one render per tick).
 * What depends on the rate takes it from here: the patched dt, the gate's clock checks,
 * the modules' "one frame ago" deadlines, the pilot throttle ramp and the prediction step.
 * The relay checks the same step (native_gate_relay.py --tick-hz). Everything counted in
 * ticks stays counted in ticks: see reports/LOCKSTEP-TICK-RATE-20261030.txt.
 *
 * Pacing (with an explicit rate, unless native_gate_tick_pace = false): a tick cheaper than
 * 1/hz would run the world faster than real time, so each client holds FrameTick until its
 * own schedule origin + n/hz, after the permit and after its report left, so no other
 * client waits for the sleep. A late client runs at once and catches up to
 * GATE_PACE_CATCHUP_MS; further behind it re-bases and does not burst. Pacing changes no
 * simulated value: it only moves when a frame starts, as the wait for a permit does. */
#ifndef LOCKSTEP_TICK_H
#define LOCKSTEP_TICK_H
#include <math.h>
#include <stdint.h>
#define GATE_TICK_HZ_DEFAULT 60u
#define GATE_PACE_CATCHUP_MS 250
#define GATE_PACE_SPIN_US 1000
static unsigned gate_tick_hz=GATE_TICK_HZ_DEFAULT;
static int gate_tick_pace;
static int gate_tick_hz_valid(unsigned hz) {return hz==60u || hz==40u || hz==30u || hz==20u;}
static double gate_tick_dt(void) {return 1.0/(double)gate_tick_hz;}
/* Before the start only; afterwards (a reloaded UI state) only the same values pass. */
static int gate_tick_configure(double hz,double pace,int started) {
    if(!isfinite(hz) || floor(hz)!=hz || hz<1 || hz>1000 || !gate_tick_hz_valid((unsigned)hz) || (pace!=0 && pace!=1)) return 0;
    if(started) return gate_tick_hz==(unsigned)hz && gate_tick_pace==(int)pace;
    gate_tick_hz=(unsigned)hz;gate_tick_pace=(int)pace;return 1;
}
/* The simulation thread's own frame: from its pathing kick to its next wait for FrameTick
 * (needs the mass traffic module's wait hook). Against outside_us it tells whether the
 * tick is limited by the simulation or by the main thread and the renderer. */
static LARGE_INTEGER gate_sim_started;
static uint64_t gate_sim_us;
static __attribute__((unused)) void gate_sim_begin(void) {QueryPerformanceCounter(&gate_sim_started);}
static __attribute__((unused)) void gate_sim_end(void) {
    LARGE_INTEGER now,f;
    if(!gate_sim_started.QuadPart) return;
    QueryPerformanceCounter(&now);QueryPerformanceFrequency(&f);
    gate_sim_us=f.QuadPart?(uint64_t)((now.QuadPart-gate_sim_started.QuadPart)*1000000/f.QuadPart):0;gate_sim_started.QuadPart=0;
}
typedef struct {int64_t origin;uint64_t count,rebases;} gate_pace_state;
static gate_pace_state gate_pace;
static uint64_t gate_pace_us,gate_pace_sum_us;
/* The time to hold until, or 0: the first frame sets the origin, and a frame more than
 * the catch-up limit late sets it again. Pure, in the performance counter's unit. */
static int64_t gate_pace_deadline(gate_pace_state *p,int64_t now,int64_t freq,unsigned hz) {
    if(!p->origin) {p->origin=now;p->count=0;return 0;}
    ++p->count;
    int64_t deadline=p->origin+(int64_t)p->count*freq/(int64_t)hz;
    if(now-deadline>freq*GATE_PACE_CATCHUP_MS/1000) {p->origin=now;p->count=0;++p->rebases;return 0;}
    return now<deadline?deadline:0;
}
#ifdef _WIN32
/* A high-resolution timer when the system has one (Windows 10 1803+), else a short
 * Sleep only when it cannot overshoot much; the last stretch spins. */
static void gate_pace_nap(int64_t left_us) {
    static HANDLE timer;static int tried;
    if(!tried) {tried=1;timer=CreateWaitableTimerExW(NULL,NULL,0x00000002u /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,0x1F0003u /* TIMER_ALL_ACCESS */);}
    if(left_us<=GATE_PACE_SPIN_US) {YieldProcessor();return;}
    if(timer) {
        LARGE_INTEGER due;due.QuadPart=-(LONGLONG)(left_us-GATE_PACE_SPIN_US)*10;
        if(SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) {WaitForSingleObject(timer,100);return;}
    }
    if(left_us>20000) Sleep(1);else SwitchToThread();
}
#else
static void gate_pace_nap(int64_t left_us) {
    if(left_us<=GATE_PACE_SPIN_US) {__builtin_ia32_pause();return;}
    struct timespec pause={0,(long)(left_us-GATE_PACE_SPIN_US)*1000};nanosleep(&pause,NULL);
}
#endif
/* On the gate's thread, after the permit, before FrameTick. */
static void gate_pace_wait(void) {
    gate_pace_us=0;
    if(!gate_tick_pace) return;
    LARGE_INTEGER now,start,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&now);start=now;
    int64_t deadline=gate_pace_deadline(&gate_pace,now.QuadPart,f.QuadPart,gate_tick_hz);
    if(!deadline) return;
    while(now.QuadPart<deadline) {
        gate_pace_nap((deadline-now.QuadPart)*1000000/f.QuadPart);
        QueryPerformanceCounter(&now);
    }
    gate_pace_us=(uint64_t)((now.QuadPart-start.QuadPart)*1000000/f.QuadPart);gate_pace_sum_us+=gate_pace_us;
}
#endif
