/* Determinism fix, DEFAULT OFF: native_gate_exact_reciprocal = true.
 *
 * Simulation code in X4.exe (flight models, bullets, steering, pilots, splines)
 * evaluates RCPSS and RSQRTSS: approximate 1/x and 1/sqrt(x) whose bits only
 * have an architectural error bound (1.5*2^-12), so Intel and AMD CPUs return
 * different results and Windows-to-Windows lockstep across vendors diverges
 * (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt, class A). Each such
 * instruction is rewritten to exactly rounded IEEE arithmetic with fixed
 * semantics that do not depend on the register allocation:
 *   rcpss  d, s   ->  d[0] = 1.0f / s[0]                 (DIVSS)
 *   rsqrtss d, s  ->  d[0] = 1.0f / sqrt(s[0])           (SQRTSS, then DIVSS)
 *   rsqrtss d, s ... rcpss d, d  ->  d[0] = sqrt(s[0])   (SQRTSS; the RCPSS emits nothing)
 * lanes 1-3 of d unchanged, flags untouched. The last line is the approximate
 * length rcp(rsqrt(x)), 475 times in X4.exe, whether the two instructions are
 * adjacent or apart (no instruction in between touches d, no branch enters):
 * sqrt(x) is what the Linux build computes where X4.exe has such a pair, so this
 * also serves Windows/Linux agreement; the preload does the same on Linux.
 * DIVSS and SQRTSS are correctly rounded under the thread's MXCSR on every
 * x86-64 CPU, which makes the rewritten sites vendor independent by
 * construction; the trampolines contain nothing else but moves, AND/OR masks,
 * LEA RSP and the copied original neighbour instructions.
 *
 * Sites: lockstep_exact_reciprocal_sites.h (tools/dev/gen_exact_reciprocal.py):
 * 1,262 of the 1,264 instructions reachable from .pdata roots, in 819 windows of
 * whole instructions (71 RCPSS and 241 RSQRTSS alone, 475 chains). Each window is pinned by an FNV-1a 64 digest of its bytes;
 * its first five bytes become jmp rel32 to a trampoline (rewritten instructions,
 * copied neighbours with RIP displacements adjusted, jmp back), the rest int3.
 * The trampoline keeps the value in a 16-byte stack slot below RSP (lea rsp; no
 * red zone on Windows; no flags). Other modules' pins that cover a window keep
 * passing: while a window is patched, read_local, sample_read and hash_ok see its
 * original bytes. Install and restore suspend every other thread and refuse while
 * one stands inside a window. Armed on the UI thread before the first admitted
 * frame; window bytes checked every gate tick (stop reason 157); restored at stop. */
#include "../portable_math/exact_reciprocal_emit.h"
#include "lockstep_exact_reciprocal_sites.h"
#define XRCP_STOP_REASON 157u
#define XRCP_DROP 16u              /* no red zone on Windows */
#define XRCP_PAGE_ANCHOR 0xd00000u
static unsigned char *xrcp_region;
static size_t xrcp_region_size;
static unsigned char xrcp_original[XRCP_WINDOWS][XRCP_MAX_WINDOW];
static volatile unsigned char xrcp_patched[XRCP_WINDOWS];
static unsigned char xrcp_sizes[XRCP_WINDOWS];
static volatile LONG xrcp_any;
static int xrcp_installed,xrcp_restored;
static unsigned xrcp_busy,xrcp_attempts;
static volatile LONG64 xrcp_failures;
static const char *xrcp_stage="idle";

static void xrcp_patch_bytes(unsigned i,unsigned char out[XRCP_MAX_WINDOW]) {
    const xrcp_window *w=&xrcp_windows[i];
    int32_t rel=(int32_t)((intptr_t)(xrcp_region+XRCP_CONSTANTS+(size_t)i*XRCP_SLOT)-(intptr_t)(image+w->rva+5));
    memset(out,0xcc,XRCP_MAX_WINDOW);out[0]=0xe9;memcpy(out+1,&rel,4);
}
/* 0 original, 1 ours, -1 anything else. */
static int xrcp_state(unsigned i) {
    const xrcp_window *w=&xrcp_windows[i];unsigned char ours[XRCP_MAX_WINDOW];
    const unsigned char *now=image+w->rva; /* the raw bytes (read_local shows the originals); code is always readable */
    if(!memcmp(now,xrcp_original[i],w->length)) return 0;
    if(!xrcp_region) return -1;
    xrcp_patch_bytes(i,ours);return memcmp(now,ours,w->length)?-1:1;
}

/* ---- original-byte view for the gate's code reads (forward declared in lockstep_gate.c) ---- */
static int xrcp_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&xrcp_any || !image || !source || !bytes) return 0;
    const unsigned char *p=source;
    return p<image+0x6fa9000 && p+bytes>image;
}
static void xrcp_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!xrcp_overlaps(source,bytes)) return;
    uintptr_t lo=(uintptr_t)source-(uintptr_t)image,hi=lo+bytes;
    unsigned first=0,last=XRCP_WINDOWS; /* windows are sorted by RVA */
    while(first<last) {unsigned mid=(first+last)/2;if(xrcp_windows[mid].rva+(uintptr_t)xrcp_windows[mid].length<=lo) first=mid+1;else last=mid;}
    for(unsigned i=first;i<XRCP_WINDOWS && xrcp_windows[i].rva<hi;i++) {
        if(!xrcp_patched[i]) continue;
        const xrcp_window *w=&xrcp_windows[i];
        for(unsigned k=0;k<w->length;k++) {
            uintptr_t at=w->rva+k;
            if(at>=lo && at<hi) ((unsigned char*)copy)[at-lo]=xrcp_original[i][k];
        }
    }
}

static int xrcp_pins(void) {
    for(unsigned i=0;i<XRCP_WINDOWS;i++) {
        const xrcp_window *w=&xrcp_windows[i];SIZE_T got=0;
        if(w->length<5 || w->length>XRCP_MAX_WINDOW || (i && w->rva<xrcp_windows[i-1].rva+xrcp_windows[i-1].length)) return 0;
        if(!ReadProcessMemory(GetCurrentProcess(),image+w->rva,xrcp_original[i],w->length,&got) || got!=w->length ||
           xrcp_fnv(xrcp_original[i],w->length)!=w->digest) return 0;
    }
    return 1;
}
static unsigned char *xrcp_alloc_region(size_t size) {
    uintptr_t at=(uintptr_t)(image+XRCP_PAGE_ANCHOR)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t far_low=(intptr_t)p-(intptr_t)(image+xrcp_windows[0].rva);
            intptr_t far_high=(intptr_t)(p+size)-(intptr_t)(image+xrcp_windows[XRCP_WINDOWS-1].rva);
            if(far_low>INT32_MIN/2 && far_low<INT32_MAX/2 && far_high>INT32_MIN/2 && far_high<INT32_MAX/2) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int xrcp_build(void) {
    size_t size=XRCP_CONSTANTS+(size_t)XRCP_WINDOWS*XRCP_SLOT;size=(size+0xffff)&~(size_t)0xffff;
    if(!xrcp_region) {xrcp_region=xrcp_alloc_region(size);xrcp_region_size=size;}
    if(!xrcp_region) return 0;
    DWORD old,ignored;if(!VirtualProtect(xrcp_region,xrcp_region_size,PAGE_READWRITE,&old)) return 0;
    memset(xrcp_region,0xcc,xrcp_region_size);memcpy(xrcp_region,xrcp_constants,sizeof(xrcp_constants));
    for(unsigned i=0;i<XRCP_WINDOWS;i++) {
        unsigned size_used;
        if(!xrcp_emit_window(xrcp_region+XRCP_CONSTANTS+(size_t)i*XRCP_SLOT,&xrcp_windows[i],xrcp_original[i],
                             image+xrcp_windows[i].rva,xrcp_region,XRCP_DROP,&size_used)) {VirtualProtect(xrcp_region,xrcp_region_size,PAGE_EXECUTE_READ,&ignored);return 0;}
        xrcp_sizes[i]=(unsigned char)size_used;
    }
    if(!VirtualProtect(xrcp_region,xrcp_region_size,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),xrcp_region,xrcp_region_size);return 1;
}
/* 1 when no frozen thread stands strictly inside a window (needs pmath_freeze). */
static int xrcp_windows_clear(void) {
    for(unsigned t=0;t<pmath_frozen_count;t++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[t],&c)) return 0;
        if(c.Rip<=(uintptr_t)image || c.Rip>=(uintptr_t)image+0x6fa9000) continue;
        uintptr_t rva=c.Rip-(uintptr_t)image;unsigned first=0,last=XRCP_WINDOWS;
        while(first<last) {unsigned mid=(first+last)/2;if(xrcp_windows[mid].rva+(uintptr_t)xrcp_windows[mid].length<=rva) first=mid+1;else last=mid;}
        if(first<XRCP_WINDOWS && rva>xrcp_windows[first].rva && rva<xrcp_windows[first].rva+(uintptr_t)xrcp_windows[first].length) return 0;
    }
    return 1;
}
/* Every window from one state to the other with the other threads frozen; 1 done, 0 nothing
 * changed, -1 partial and stuck. */
static int xrcp_switch(int install) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        ++xrcp_attempts;
        if(!pmath_freeze()) return 0;
        if(!xrcp_windows_clear()) {pmath_thaw();++xrcp_busy;Sleep(1);continue;}
        unsigned done=0;int ok=1;
        for(;done<XRCP_WINDOWS;done++) {
            unsigned char ours[XRCP_MAX_WINDOW];xrcp_patch_bytes(done,ours);const xrcp_window *w=&xrcp_windows[done];
            if(!view_cas_bytes(w->rva,install?xrcp_original[done]:ours,install?ours:xrcp_original[done],w->length)) {ok=0;break;}
            xrcp_patched[done]=(unsigned char)(install!=0);
            if(install) InterlockedExchange(&xrcp_any,1);
        }
        int stuck=0;
        if(!ok) for(unsigned i=0;i<done;i++) {
            unsigned char ours[XRCP_MAX_WINDOW];xrcp_patch_bytes(i,ours);const xrcp_window *w=&xrcp_windows[i];
            if(view_cas_bytes(w->rva,install?ours:xrcp_original[i],install?xrcp_original[i]:ours,w->length)) xrcp_patched[i]=(unsigned char)!install;
            else stuck=1;
        }
        int any=0;for(unsigned i=0;i<XRCP_WINDOWS;i++) any|=xrcp_patched[i];
        InterlockedExchange(&xrcp_any,any);
        pmath_thaw();
        return ok?1:stuck?-1:0;
    }
    return 0;
}
/* gate.exact_reciprocal(): UI thread, before the first admitted frame. */
static int xrcp_arm(void) {
    if(xrcp_installed) return stats.active!=0;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {xrcp_stage="state";return 0;}
    if(InterlockedCompareExchange(&xrcp_any,0,0)) {xrcp_stage="stuck";return 0;}
    if(!xrcp_pins()) {xrcp_stage="pins";return 0;}
    if(!xrcp_build()) {xrcp_stage="build";return 0;}
    for(unsigned i=0;i<XRCP_WINDOWS;i++) if(xrcp_state(i)!=0) {xrcp_stage="site";return 0;}
    xrcp_failures=0;xrcp_restored=0;
    int r=xrcp_switch(1);
    if(r!=1) {xrcp_stage=r<0?"stuck":"busy";if(r<0) InterlockedIncrement64(&xrcp_failures);return 0;}
    xrcp_installed=1;xrcp_stage="armed";return 1;
}
static int xrcp_owned(void) {
    if(!xrcp_installed) return 1;
    for(unsigned i=0;i<XRCP_WINDOWS;i++) if(xrcp_state(i)!=1) return 0;
    return xrcp_region && !memcmp(xrcp_region,xrcp_constants,sizeof(xrcp_constants));
}
/* Gate tick: any change stops the gate with reason 157. */
static int xrcp_check(void) {
    if(InterlockedCompareExchange64(&xrcp_failures,0,0)) return 0;
    if(!xrcp_owned()) {xrcp_stage="site_changed";InterlockedIncrement64(&xrcp_failures);return 0;}
    return 1;
}
static void xrcp_restore(void) {
    if(!xrcp_installed) return;
    for(unsigned i=0;i<XRCP_WINDOWS;i++) if(xrcp_state(i)!=1) {xrcp_stage="restore_foreign";return;}
    int r=xrcp_switch(0);
    if(r==1) {xrcp_installed=0;xrcp_restored=1;xrcp_stage="restored";}
    else xrcp_stage=r<0?"restore_stuck":"restore_busy";
}
static int xrcp_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d windows=%u rewritten=%u restored=%d stage=%s attempts=%u busy=%u failures=%lld",
        xrcp_installed,(unsigned)XRCP_WINDOWS,(unsigned)XRCP_REWRITTEN,xrcp_restored,xrcp_stage,xrcp_attempts,xrcp_busy,(long long)xrcp_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
