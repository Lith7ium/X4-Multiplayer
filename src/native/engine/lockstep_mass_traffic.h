/* Determinism fix, DEFAULT OFF: joins the two asynchronous passes MoveWorker
 * kicks at the end of every frame, mass traffic (native_gate_mass_traffic =
 * 'sync' | 'observe') and the region manager (native_gate_region_pass = 'sync'
 * | 'observe'). Both stay on.
 * Mass traffic: the network update runs on its own thread,
 * U::MT::MassTrafficUpdateTask::Run 0x8c06d0 (named "MassTrafficUpdateTask",
 * object *0x6cf23e0): MoveWorker::Run kicks one pass after releasing
 * g_semSimDone (0xef4777: under the task lock +0x60 it clears the ready byte
 * +0x88 and sets the wake event +0xa8 when the thread is parked, +0xa0). The
 * pass walks clusters, sectors and zones; per zone it holds g_MassTrafficLock
 * 0x3949b28 and updates the zone's network (zone+0x4f0: 0x82ed50/0x82f110 by
 * the camera test 0x83ef20, then 0x831440). When done it sets +0x88 again under
 * the task lock and parks. U::Zone::Move 0x834950 only TryEnters
 * g_MassTrafficLock (call at 0x834c6e) and skips its traffic sub-update when
 * the pass holds it: the temp-component update 0x8be450 (materialisation
 * 0x8bfb40 -> component creation, RemoveTempComponentEvent by expiry and
 * camera distance) and 0x8acd40.
 * Region manager: U::Regions::RegionManager::Run 0xef30b0 (named
 * "RegionManager", object *0x6cf21e0, the runnable at +0x20) follows the same
 * protocol: the kick right after the mass traffic one (0xef47d4, when the task
 * has work, +0xf8 or +0xfc) clears the ready byte +0x140 under the task lock
 * +0x118 and sets +0x160 when parked (+0x158); the pass (0xef2e90, 0xeea8a0:
 * region objects such as asteroids around the camera, with draws on the
 * thread's word) sets +0x140 again under the lock and parks. T9: a burst of
 * about 104 attention evaluations (objects added in the new sector, 98
 * asteroids created) came at tick 179 on one client and 180 on the other.
 * Neither pass is joined natively: each runs while the main thread publishes
 * the next frame's view caches (FrameTick) and while the next MoveWorker frame
 * runs, so the frame in which its results are used depends on thread timing.
 * 'sync': the MoveWorker's frame-start wait on g_semSimStart (call at
 * 0xef45ea) is counted, so the gate tick (main thread, before FrameTick) knows
 * the MoveWorker has passed the kicks; it then waits for each synced pass
 * (ready byte set, read under its task lock), reads its thread's RNG word
 * (draws of the pass from our seed) and seeds it for the next pass with a
 * lockstep key of the completed tick. The passes then run between the end of
 * frame N and FrameTick N+1, seeing one view-cache bank and one game time, and
 * the mass traffic pass never holds g_MassTrafficLock while Zone::Move runs; a
 * failed TryEnter is counted and fails closed. 'observe' keeps the timing: it
 * counts TryEnter failures and, without waiting, how often a pass is still
 * running at the gate tick.
 * A region pass can take long (T10: 0.86 s at tick 94 after the warp at 90, then
 * more than the 5 s limit at the next one, on both clients): the region join
 * waits up to 120 s (mass traffic 5 s), backing off to Sleep(1) after 2 ms, and
 * a join longer than 1 s samples the pass thread once (suspend, RIP and the
 * image return addresses on its stack, resume; nothing else runs meanwhile) and
 * again on a timeout, so a slow pass and a stuck one can be told apart (stall=).
 * Not covered here (reported): the camera inputs of 0x83ef20, 0x8ab1e0,
 * 0x8be450 and 0xeea8a0 (cluster, position and distance of the local camera).
 * Stride (native_gate_pass_stride = N, default 1): the synced passes run at the end of every
 * Nth frame only. The two passes overlap, so the join costs the longer of them: about
 * 2.7 ms per tick, whichever is joined alone (traffic takes over from region as the world
 * grows), and a stride on one of them alone would save nothing. Traffic takes its time from
 * the thread's game time sync at its start (0x8c083c), so a longer gap is one longer step.
 * On the other ticks the gate holds both ready bytes at 0 while the MoveWorker is parked,
 * so the stock kicks (nothing to do unless ready) pass, and gives them back at the next
 * join, the same ticks on every client.
 * Patches: the two call displacements (ff 15 d32) point at pointer slots on a
 * private page near the image; CAS of the aligned 16 bytes, pinned functions
 * (SHA-256 with our displacements read as the originals), checked every gate
 * tick (stop reason 100), restored by CAS on stop. No thunk. */
#define MT_TASK 0x6cf23e0u        /* U::MT::MassTrafficUpdateTask* */
#define MT_REGION_TASK 0x6cf21e0u /* U::Regions::RegionManager* */
#define MT_LOCK 0x3949b28u        /* g_MassTrafficLock */
#define MT_SEM_START 0x3d79150u   /* g_semSimStart */
#define MT_TRY_SITE 0x834c6eu     /* U::Zone::Move: call [TryEnterCriticalSection] */
#define MT_WAIT_SITE 0xef45eau    /* MoveWorker::Run frame start: call [WaitForSingleObject] */
#define MT_TRY_IAT 0x1bc14c8u
#define MT_WAIT_IAT 0x1bc14b0u
#define MT_SITES 2u
#define MT_PINS 7u
#define MT_PAGE_SIZE 0x1000u
#define MT_DOMAIN 0x58344c4d41535354ULL        /* "X4LMASST" */
#define MT_REGION_DOMAIN 0x58344c5245475053ULL /* "X4LREGPS" */
#define MT_STOP_REASON 100u
#define MT_STRIDE_MAX 16u
#define MT_TIMEOUT_MS 5000u
#define MT_REGION_TIMEOUT_MS 120000u
#define MT_STALL_MS 1000u
#define MT_STALL_FRAMES 6u
#define MT_READY 0x88u            /* traffic task: pass done, ready for the next kick */
#define MT_TASK_LOCK 0x60u
#define MT_REGION_READY 0x140u
#define MT_REGION_LOCK 0x118u
enum {MT_OFF,MT_SYNC,MT_OBSERVE};
static const struct {unsigned rva,iat;} mt_sites[MT_SITES]={{MT_TRY_SITE,MT_TRY_IAT},{MT_WAIT_SITE,MT_WAIT_IAT}};
static const struct {unsigned rva,size;const char *sha;} mt_pins[MT_PINS]={
    {0x834c59u,0x1d0u,"c96e35677de50f3d166ce64bd2053c256ada2f13115a0bb5db0f4a06ce6152b7"}, /* Zone::Move traffic part */
    {0xef45deu,0x17u,"3c4f01866599bfb9315b502f298a242a79a97d7a7c7d572146c6ad58f3ab5a26"},  /* MoveWorker frame-start wait */
    {0xef4777u,0x5du,"5fb0b7d0c79256dd31d4b13481a15bf2cdcdd05cd8bef0b26eb87358339e2a8c"},  /* MoveWorker mass traffic kick */
    {0x8c0756u,0xebu,"109b6019fba8b6358e477ced8c563438123c01ebb13db3bd6a6b459a6662e2d2"},  /* traffic Run: ready, park, wake */
    {0x8c0abau,0xb4u,"ccbc04a190bf269c239e665e4801d798b0600c76e00644005dd2bc706e74c528"},  /* traffic Run: per-zone lock and update */
    {0xef47d4u,0x6au,"38dc863d6a3c5aa5c455742b3c47d6cf783ab8bf619076d9ff8637598bd24e5b"},  /* MoveWorker region kick */
    {0xef3133u,0x152u,"0eb22cbcf5ac376de8482a40ffbb211977ccecb4f020acfbcec7022b488a07e4"}}; /* region Run: ready, park, pass */
/* One joined pass: its task object, lock and ready byte, its thread and RNG point. */
typedef struct {
    const char *name;const wchar_t *thread;unsigned global,lock,ready,timeout_ms;uint64_t domain;
    int mode,seeded,sampled,held;unsigned char *slot;DWORD thread_id;
    uint64_t joins,busy,unknown,absent,wait_us,max_wait_us,total_wait_us,seeds,draws,total_draws,draws_unknown,word,seed;
    uint64_t stalls,stall_ms,stall_tick;uint32_t stall_rip,stall_stack[MT_STALL_FRAMES];unsigned stall_depth;
} mt_pass;
static mt_pass mt_traffic={.name="traffic",.thread=L"MassTrafficUpdateTask",.global=MT_TASK,.lock=MT_TASK_LOCK,.ready=MT_READY,
                           .timeout_ms=MT_TIMEOUT_MS,.domain=MT_DOMAIN};
static mt_pass mt_region={.name="region",.thread=L"RegionManager",.global=MT_REGION_TASK,.lock=MT_REGION_LOCK,.ready=MT_REGION_READY,
                          .timeout_ms=MT_REGION_TIMEOUT_MS,.domain=MT_REGION_DOMAIN};
static int mt_installed,mt_restored,mt_patched;
static volatile LONG mt_active;
static unsigned char *mt_page;
static unsigned char mt_site_done[MT_SITES];
static volatile LONG64 mt_failures,mt_worker_waits,mt_worker_wakes,mt_try_total,mt_try_failed;
static volatile LONG64 mt_frame_try,mt_frame_failed;
static uint64_t mt_frame=UINT64_MAX,mt_first_skip;
static const char *mt_stage="idle";
static unsigned mt_stride=1;
static uint64_t mt_stride_skips;
static int mt_fail(const char *why) {mt_stage=why;InterlockedIncrement64(&mt_failures);return 0;}
static void mt_pass_reset(mt_pass *p) {
    p->mode=MT_OFF;p->seeded=p->sampled=p->held=0;
    p->joins=p->busy=p->unknown=p->absent=p->wait_us=p->max_wait_us=p->total_wait_us=0;
    p->seeds=p->draws=p->total_draws=p->draws_unknown=p->word=p->seed=0;
    p->stalls=p->stall_ms=p->stall_tick=0;p->stall_rip=0;p->stall_depth=0;memset(p->stall_stack,0,sizeof(p->stall_stack));
}
static int32_t mt_disp_to(unsigned site,const void *target) {
    return (int32_t)((intptr_t)target-(intptr_t)(image+site+6));
}
static int mt_disp_fits(unsigned site,const void *target) {
    intptr_t d=(intptr_t)target-(intptr_t)(image+site+6);return d>=INT32_MIN && d<=INT32_MAX;
}
/* 0 original (the IAT entry), 1 ours (our page slot), -1 anything else. */
static int mt_site_state(unsigned i) {
    unsigned char now[6];if(!read_local(image+mt_sites[i].rva,now,6) || now[0]!=0xff || now[1]!=0x15) return -1;
    int32_t d;memcpy(&d,now+2,4);
    if(d==mt_disp_to(mt_sites[i].rva,image+mt_sites[i].iat)) return 0;
    if(mt_page && d==mt_disp_to(mt_sites[i].rva,mt_page+8*i)) return 1;
    return -1;
}
static int rc_read_original(unsigned rva,unsigned char *copy,unsigned size); /* lockstep_region_camera.h: traffic_rows patches Zone::Move */
static int mt_pins_ok(void) {
    static unsigned char copy[0x200];
    for(unsigned p=0;p<MT_PINS;p++) {
        unsigned rva=mt_pins[p].rva,size=mt_pins[p].size;
        if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
        for(unsigned i=0;i<MT_SITES;i++) {
            unsigned at=mt_sites[i].rva;if(at<rva || at+6>rva+size) continue;
            if(mt_site_state(i)<0) return 0;
            int32_t d=mt_disp_to(at,image+mt_sites[i].iat);memcpy(copy+(at-rva)+2,&d,4);
        }
        if(!rc_read_original(rva,copy,size) || !hash_ok(copy,size,mt_pins[p].sha)) return 0;
    }
    return 1;
}
/* One displacement (4 bytes inside one aligned 16-byte block) by CAS. */
static int mt_cas_disp(unsigned site,int32_t expected,int32_t value) {
    unsigned at=site+2,block=at&~15u;if((at&15u)>12u) return 0;
    unsigned char *p=image+block;DWORD old,ignored;
    if(!VirtualProtect(p,16,PAGE_EXECUTE_READWRITE,&old)) return 0;
    unsigned __int128 current,desired;memcpy(&current,(const void*)p,16);desired=current;
    int ok=!memcmp((unsigned char*)&current+(at-block),&expected,4);
    memcpy((unsigned char*)&desired+(at-block),&value,4);
    if(ok) ok=__sync_val_compare_and_swap((unsigned __int128*)p,current,desired)==current;
    VirtualProtect(p,16,old,&ignored);FlushInstructionCache(GetCurrentProcess(),p,16);
    return ok;
}
static unsigned char *mt_page_near(void) {
    uintptr_t at=(uintptr_t)(image+MT_TRY_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,MT_PAGE_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int fits=1;for(unsigned i=0;fits && i<MT_SITES;i++) fits=mt_disp_fits(mt_sites[i].rva,p+8*i);
            if(fits) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void mt_roll(void) {
    if(mt_frame==stats.issued) return;
    mt_frame=stats.issued;InterlockedExchange64(&mt_frame_try,0);InterlockedExchange64(&mt_frame_failed,0);
}
/* Zone::Move's TryEnterCriticalSection(&g_MassTrafficLock). */
static BOOL WINAPI mt_try_enter(CRITICAL_SECTION *lock) {
    BOOL ok=TryEnterCriticalSection(lock);
    if(lock==(CRITICAL_SECTION*)(image+MT_LOCK) && InterlockedCompareExchange(&mt_active,0,0) && mt_traffic.mode!=MT_OFF &&
       (!boot_mode || boot_phase==3)) {
        mt_roll();InterlockedIncrement64(&mt_try_total);InterlockedIncrement64(&mt_frame_try);
        if(!ok) {
            InterlockedIncrement64(&mt_try_failed);InterlockedIncrement64(&mt_frame_failed);
            if(!mt_first_skip) mt_first_skip=stats.issued?stats.issued:1;
            if(mt_traffic.mode==MT_SYNC && mt_traffic.joins) mt_fail("skipped"); /* every frame after the first join is joined */
        }
    }
    return ok;
}
/* The MoveWorker's frame-start WaitForSingleObject(g_semSimStart, INFINITE). */
static DWORD WINAPI mt_wait(HANDLE handle,DWORD milliseconds) {
    int counted=handle && handle==*(HANDLE volatile*)(image+MT_SEM_START);
    if(counted) {gate_sim_end();InterlockedIncrement64(&mt_worker_waits);}
    DWORD result=WaitForSingleObject(handle,milliseconds);
    if(counted) InterlockedIncrement64(&mt_worker_wakes);
    return result;
}
/* A pass thread's TLS slot 0, found once by its X4 thread name. */
typedef struct {LONG status;void *teb;uintptr_t process,thread,affinity;LONG priority,base;} mt_thread_basic;
_Static_assert(sizeof(mt_thread_basic)==48,"THREAD_BASIC_INFORMATION layout");
static unsigned char *mt_find_slot(const wchar_t *wanted,DWORD *id) {
    typedef LONG (WINAPI *query_fn)(HANDLE,int,void *,ULONG,ULONG *);typedef LONG (WINAPI *name_fn)(HANDLE,PWSTR *);
    query_fn query=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    name_fn describe=(name_fn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription");
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(!query || !describe || snap==INVALID_HANDLE_VALUE) return NULL;
    unsigned char *found=NULL;unsigned matches=0;THREADENTRY32 entry;entry.dwSize=sizeof(entry);
    for(BOOL ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)) {
        if(entry.th32OwnerProcessID!=GetCurrentProcessId()) continue;
        HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);if(!thread) continue;
        PWSTR name=NULL;mt_thread_basic basic;
        if(describe(thread,&name)>=0 && name && !wcscmp(name,wanted) && query(thread,0,&basic,sizeof(basic),NULL)>=0) {
            void **slots=NULL,*slot=NULL;
            if(read_local((unsigned char*)basic.teb+0x58,&slots,8) && slots && read_local(slots,&slot,8) && slot) {
                found=slot;++matches;if(id) *id=entry.th32ThreadID;
            }
        }
        if(name) LocalFree(name);
        CloseHandle(thread);
    }
    CloseHandle(snap);return matches==1?found:NULL;
}
/* A pass is done once its task's ready byte is set; read it under the task lock. */
static int mt_ready(const mt_pass *p,unsigned char *task) {
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(task+p->lock);
    EnterCriticalSection(lock);unsigned char ready=task[p->ready];LeaveCriticalSection(lock);
    return ready!=0;
}
static int mt_worker_parked(void) {
    return InterlockedCompareExchange64(&mt_worker_waits,0,0)>InterlockedCompareExchange64(&mt_worker_wakes,0,0);
}
/* The pass thread is idle: the pass's draws and, in sync, the next pass's seed. */
static int mt_rng_point(mt_pass *p,uint64_t tick) {
    int sync=p->mode==MT_SYNC;
    if(!p->slot) p->slot=mt_find_slot(p->thread,&p->thread_id);
    if(!p->slot) return sync?mt_fail(p==&mt_region?"region_thread":"thread"):1;
    uint64_t word;if(!read_local(p->slot+0x318,&word,8)) return sync?mt_fail(p==&mt_region?"region_thread_word":"thread_word"):1;
    uint64_t from=sync?p->seed:p->word;
    if(sync?p->seeded:p->sampled) {
        uint64_t d=render_rng_distance(from,word);
        if(d==UINT64_MAX) ++p->draws_unknown;else {p->draws=d;p->total_draws+=d;}
    }
    p->word=word;p->sampled=1;
    if(sync) {p->seed=lockstep_seed(p->domain,tick,0,0,0,0);memcpy(p->slot+0x318,&p->seed,8);p->seeded=1;++p->seeds;}
    return 1;
}
static unsigned char *mt_task(const mt_pass *p) {return *(unsigned char*volatile*)(image+p->global);}
static uint32_t mt_rva(uint64_t address) {
    uintptr_t base=(uintptr_t)image;return address>=base && address-base<0x6fa9000?(uint32_t)(address-base):0xffffffffu;
}
/* Where a slow pass thread is: RIP and the first image code addresses on its stack.
 * Between suspend and resume only GetThreadContext and ReadProcessMemory run. */
static void mt_sample(mt_pass *p,uint64_t elapsed_ms) {
    if(!p->thread_id && !p->slot) p->slot=mt_find_slot(p->thread,&p->thread_id);
    if(!p->thread_id) return;
    HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,p->thread_id);if(!t) return;
    if(SuspendThread(t)==(DWORD)-1) {CloseHandle(t);return;}
    CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
    uint32_t stack[MT_STALL_FRAMES];unsigned depth=0;uint32_t rip=0xffffffffu;
    if(GetThreadContext(t,&c)) {
        rip=mt_rva(c.Rip);uint64_t words[64];
        for(unsigned chunk=0;chunk<8 && depth<MT_STALL_FRAMES;chunk++) {
            if(!read_local((const void*)(uintptr_t)(c.Rsp+chunk*sizeof(words)),words,sizeof(words))) break;
            for(unsigned i=0;i<64 && depth<MT_STALL_FRAMES;i++) {
                uint32_t r=mt_rva(words[i]);if(r>=0x1000u && r<0x1bc0000u) stack[depth++]=r; /* .text only */
            }
        }
    }
    ResumeThread(t);CloseHandle(t);
    ++p->stalls;if(elapsed_ms>=p->stall_ms) {
        p->stall_ms=elapsed_ms;p->stall_tick=stats.issued;p->stall_rip=rip;p->stall_depth=depth;memcpy(p->stall_stack,stack,sizeof(stack));
    }
}
/* One pass at the gate tick; the MoveWorker is parked when parked != 0. */
static int mt_join_pass(mt_pass *p,uint64_t tick,unsigned timeout_ms,int parked) {
    if(p->mode==MT_OFF) return 1;
    unsigned char *task=mt_task(p);
    if(!task) {++p->absent;return 1;}
    if(p->mode==MT_OBSERVE) {
        if(!parked) {++p->unknown;return 1;}
        if(!mt_ready(p,task)) {++p->busy;return 1;}
        return mt_rng_point(p,tick);
    }
    LARGE_INTEGER t0,t1,frequency;QueryPerformanceCounter(&t0);QueryPerformanceFrequency(&frequency);
    ULONGLONG started=GetTickCount64();int busy=0,sampled=0;
    if(timeout_ms>p->timeout_ms) timeout_ms=p->timeout_ms;
    while(!mt_ready(p,task)) {
        ULONGLONG elapsed=GetTickCount64()-started;busy=1;
        if(elapsed>=timeout_ms) {mt_sample(p,elapsed);return mt_fail(p==&mt_region?"region_pass":"pass");}
        if(!sampled && elapsed>=MT_STALL_MS) {mt_sample(p,elapsed);sampled=1;}
        if(elapsed>=2) Sleep(1);else SwitchToThread();
    }
    QueryPerformanceCounter(&t1);
    p->wait_us=frequency.QuadPart?(uint64_t)((t1.QuadPart-t0.QuadPart)*1000000/frequency.QuadPart):0;
    p->total_wait_us+=p->wait_us;if(p->wait_us>p->max_wait_us) p->max_wait_us=p->wait_us;
    p->busy+=busy;++p->joins;
    return mt_rng_point(p,tick);
}
/* A pass's ready byte, under its lock. */
static void mt_ready_set(mt_pass *p,unsigned char value) {
    unsigned char *task=mt_task(p);
    if(!task) return;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(task+p->lock);
    EnterCriticalSection(lock);task[p->ready]=value;LeaveCriticalSection(lock);
}
static void mt_hold(mt_pass *p) {if(p->mode==MT_SYNC && mt_task(p)) {mt_ready_set(p,0);p->held=1;}}
static int mt_unhold(mt_pass *p) {
    if(!p->held) return 0;
    mt_ready_set(p,1);p->held=0;return 1;
}
/* Gate tick, main thread (MoveWorker joined, before FrameTick). 0 fails closed. */
static int mt_join(uint64_t tick,unsigned timeout_ms) {
    if(!InterlockedCompareExchange(&mt_active,0,0)) return 1;
    int need=(mt_traffic.mode==MT_SYNC && mt_task(&mt_traffic)) || (mt_region.mode==MT_SYNC && mt_task(&mt_region));
    ULONGLONG started=GetTickCount64();
    /* A synced pass is joined only after the MoveWorker has passed its kick, back in its frame-start wait. */
    while(need && !mt_worker_parked()) {
        if(GetTickCount64()-started>=timeout_ms) return mt_fail("worker");
        SwitchToThread();
    }
    int parked=mt_worker_parked();
    /* A held frame had no pass: the bytes back, and the joins below find them idle. */
    if(mt_unhold(&mt_traffic)+mt_unhold(&mt_region)) ++mt_stride_skips;
    if(!mt_join_pass(&mt_traffic,tick,timeout_ms,parked) || !mt_join_pass(&mt_region,tick,mt_region.timeout_ms,parked)) return 0;
    /* The next frame ends at tick+1: hold its kicks unless that tick is on the stride. */
    if(mt_stride>1 && parked && (tick+1)%mt_stride) {mt_hold(&mt_traffic);mt_hold(&mt_region);}
    return 1;
}
/* Before the first admitted frame, after the passes' modes. */
static int mt_stride_arm(unsigned n) {
    int synced=mt_traffic.mode==MT_SYNC || mt_region.mode==MT_SYNC;
    if(mt_stride!=1 || stats.issued) return n==mt_stride && synced;
    if(!n || n>MT_STRIDE_MAX || !synced || !stats.active || GetCurrentThreadId()!=ui_thread) return 0;
    mt_stride=n;return 1;
}
static int mt_install_patches(void) {
    for(unsigned i=0;i<MT_SITES;i++) {
        if(!mt_cas_disp(mt_sites[i].rva,mt_disp_to(mt_sites[i].rva,image+mt_sites[i].iat),mt_disp_to(mt_sites[i].rva,mt_page+8*i))) return 0;
        mt_site_done[i]=1;
    }
    return 1;
}
/* Restore every site still holding our displacement; 1 when none is left patched. */
static int mt_remove_patches(void) {
    int all=1;
    for(unsigned i=0;i<MT_SITES;i++) if(mt_site_done[i]) {
        int state=mt_site_state(i);
        if(state==1 && mt_cas_disp(mt_sites[i].rva,mt_disp_to(mt_sites[i].rva,mt_page+8*i),mt_disp_to(mt_sites[i].rva,image+mt_sites[i].iat))) mt_site_done[i]=0;
        else if(state==0) mt_site_done[i]=0;
        else all=0;
    }
    return all;
}
/* The shared patches, once, for whichever pass is armed first. */
static int mt_install(void) {
    if(mt_installed) return 1;
    if(!mt_pins_ok()) {mt_stage="pins";return 0;}
    for(unsigned i=0;i<MT_SITES;i++) if(mt_site_state(i)!=0) {mt_stage="site";return 0;}
    if(!mt_page) mt_page=mt_page_near();
    if(!mt_page) {mt_stage="page";return 0;}
    void *entries[MT_SITES]={(void*)mt_try_enter,(void*)mt_wait};memcpy(mt_page,entries,sizeof(entries));
    mt_frame=UINT64_MAX;mt_first_skip=0;mt_restored=0;mt_failures=0;
    mt_try_total=mt_try_failed=mt_frame_try=mt_frame_failed=0;
    if(!mt_install_patches()) {mt_remove_patches();mt_stage="cas";return 0;}
    InterlockedExchange(&mt_active,1);
    mt_patched=1;mt_installed=1;mt_stage="armed";return 1;
}
static int mt_arm_pass(mt_pass *p,int mode) {
    if(p->mode!=MT_OFF) return stats.active && mode==p->mode; /* Identical rerun after a loading-UI reload. */
    if((mode!=MT_SYNC && mode!=MT_OBSERVE) || !stats.active || !boot_mode || boot_phase || stats.issued ||
       GetCurrentThreadId()!=ui_thread) return 0;
    if(!mt_install()) return 0;
    unsigned char *slot=p->slot;mt_pass_reset(p);p->slot=slot;p->mode=mode;return 1;
}
static int mt_arm(int mode) {return mt_arm_pass(&mt_traffic,mode);}
static int mt_region_arm(int mode) {return mt_arm_pass(&mt_region,mode);}
static int mt_owned(void) {
    if(!mt_patched) return 1;
    for(unsigned i=0;i<MT_SITES;i++) if(mt_site_state(i)!=1) return 0;
    void *entries[MT_SITES];memcpy(entries,mt_page,sizeof(entries));
    return entries[0]==(void*)mt_try_enter && entries[1]==(void*)mt_wait;
}
/* Main-thread tick check; any failure stops the gate with reason 100. */
static int mt_check(void) {
    if(InterlockedCompareExchange64(&mt_failures,0,0)) return 0;
    const char *why=!mt_owned()?"site_changed":!mt_pins_ok()?"block_changed":NULL;
    if(why) return mt_fail(why);
    return 1;
}
static void mt_restore(void) {
    InterlockedExchange(&mt_active,0);
    mt_unhold(&mt_traffic);mt_unhold(&mt_region);
    if(!mt_installed) return;
    if(mt_patched) {
        if(!mt_remove_patches()) return;
        mt_patched=0;
    }
    mt_restored=1;mt_installed=0;
}
/* The player camera's control state as mass traffic and the region pass read it
 * next (mode and a hash of the local PosRot +0x590..+0x5d0, which the control
 * update rewrites every frame from game and real time); "-" when there is no camera. */
static uint64_t mt_camera_hash(uint32_t *mode) {
    unsigned char *universe=NULL,*camera=NULL,local[0x40];*mode=UINT32_MAX;
    if(!read_local(image+0x3d198f8,&universe,8) || !universe || !read_local(universe+0x3d0,&camera,8) || !camera ||
       !read_local(camera+0x590,local,sizeof(local)) || !read_local(camera+0x868,mode,4)) return 0;
    uint64_t h=0xcbf29ce484222325ULL;for(unsigned i=0;i<sizeof(local);i++) {h^=local[i];h*=0x100000001b3ULL;}
    return h;
}
static const char *mt_mode_name(int mode) {return mode==MT_SYNC?"sync":mode==MT_OBSERVE?"observe":"off";}
/* The pass part of a status line: joins, waits, the thread's word and draws, the longest stall. */
static int mt_pass_text(const mt_pass *p,char *out,size_t capacity) {
    char stall[160];int k=snprintf(stall,sizeof(stall),"%llu:%llu:%08x",(unsigned long long)p->stall_tick,(unsigned long long)p->stall_ms,p->stall_rip);
    for(unsigned i=0;i<p->stall_depth && i<MT_STALL_FRAMES && k>0 && (size_t)k<sizeof(stall);i++)
        k+=snprintf(stall+k,sizeof(stall)-(size_t)k,"%c%x",i?',':':',p->stall_stack[i]);
    int n=snprintf(out,capacity,"joins=%llu busy=%llu unknown=%llu absent=%llu wait_us=%llu max_wait_us=%llu total_wait_us=%llu word=%016llx "
        "draws=%llu total_draws=%llu draws_unknown=%llu seeds=%llu stalls=%llu stall=%s",
        (unsigned long long)p->joins,(unsigned long long)p->busy,(unsigned long long)p->unknown,(unsigned long long)p->absent,
        (unsigned long long)p->wait_us,(unsigned long long)p->max_wait_us,(unsigned long long)p->total_wait_us,(unsigned long long)p->word,
        (unsigned long long)p->draws,(unsigned long long)p->total_draws,(unsigned long long)p->draws_unknown,(unsigned long long)p->seeds,
        (unsigned long long)p->stalls,p->stalls?stall:"-");
    return n>0 && (size_t)n<capacity?n:0;
}
static int mt_status(char *out,size_t capacity) {
    uint32_t camera_mode;uint64_t camera=mt_camera_hash(&camera_mode);char camera_text[40],pass[512];
    if(camera) snprintf(camera_text,sizeof(camera_text),"%u:%016llx",camera_mode,(unsigned long long)camera);else strcpy(camera_text,"-");
    if(!mt_pass_text(&mt_traffic,pass,sizeof(pass))) return 0;
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d frame=%llu tries=%lld skips=%lld total_tries=%lld total_skips=%lld first_skip=%llu %s "
        "worker=%lld/%lld camera=%s restored=%d stage=%s failures=%lld stride=%u stride_skips=%llu",
        mt_mode_name(mt_traffic.mode),(long)mt_active,mt_installed,
        (unsigned long long)(mt_frame==UINT64_MAX?0:mt_frame),(long long)mt_frame_try,(long long)mt_frame_failed,
        (long long)mt_try_total,(long long)mt_try_failed,(unsigned long long)mt_first_skip,pass,
        (long long)mt_worker_waits,(long long)mt_worker_wakes,camera_text,mt_restored,mt_stage,(long long)mt_failures,mt_stride,(unsigned long long)mt_stride_skips);
    return n>0 && (size_t)n<capacity?n:0;
}
static int mt_region_status(char *out,size_t capacity) {
    uint32_t camera_mode;uint64_t camera=mt_camera_hash(&camera_mode);char camera_text[40],pass[512];
    if(camera) snprintf(camera_text,sizeof(camera_text),"%u:%016llx",camera_mode,(unsigned long long)camera);else strcpy(camera_text,"-");
    if(!mt_pass_text(&mt_region,pass,sizeof(pass))) return 0;
    int n=snprintf(out,capacity,"mode=%s armed=%ld installed=%d %s worker=%lld/%lld camera=%s restored=%d stage=%s failures=%lld",
        mt_mode_name(mt_region.mode),(long)mt_active,mt_installed,pass,(long long)mt_worker_waits,(long long)mt_worker_wakes,
        camera_text,mt_restored,mt_stage,(long long)mt_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
