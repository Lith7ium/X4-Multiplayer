/* Determinism fix, DEFAULT OFF: native_gate_decimal = true.
 *
 * X4 reads the numbers of a save (and other ';'-separated value lists) with one
 * reader in two forms: X4.exe 0x14c3090 (binary64) and 0x14c2bd0 (binary32, the
 * binary64 result narrowed), Linux X4 0x1a3b1a0 / 0x1a3ac40. Both accumulate the
 * digits in binary64, but Windows scales fraction digits with *= 0.1 and the CRT
 * pow, Linux with /= 10 and glibc pow, so a saved value can load with different
 * bits (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt, class P; 2,262 of 38,448
 * saved AI times differed by one ulp). Both entries go to src/native/portable_math/
 * portable_decimal.c, which reads the same syntax and rounds the decimal value
 * correctly once, with integer arithmetic; the Linux preload replaces the Linux
 * pair the same way (X4MP_PORTABLE_DECIMAL=1), so both builds read the same bits.
 * The Windows forms are the engine's general reader specialised for separator ';'
 * without end and error outputs; their fallback value is the fifth (stack)
 * argument and rdx, r8 and r9 are unused. All 319 call sites were scanned: none
 * keeps a volatile register live across the call.
 * Each entry's first five bytes become jmp rel32 to a stub (jmp [rip]; target) on
 * a private page, with every other thread suspended and none inside the window.
 * Pins: SHA-256 of both readers; entry bytes and stubs every gate tick (stop reason
 * 159). Armed before the snapshot load, restored at stop. While an entry holds our
 * jump, other modules reading it through read_local, sample_read or hash_ok see the
 * original bytes (pdec_unpatch_copy): native_gate_world_wait and native_gate_ai_wait_rng
 * pin 0x14c3090 whole and arm after this module in the probe. This module reads raw.
 * native_gate_ai_wait_rng keys on the time the engine loaded, whichever reader read it,
 * so both arm together in either order. A selected-Wait manifest (native_gate_world_wait,
 * world_wait_rng, world_find_rng) carries the stock reader's bits for its saved times;
 * that experiment and this module refuse each other in either order (stage
 * "world_wait_manifest" here, world_wait_configure there). */
#include "../portable_math/portable_decimal.h"
#define PDEC_STOP_REASON 159u
#define PDEC_COUNT 2u
static double pdec_read64(const char *text,void *unused1,void *unused2,void *unused3,double fallback) {
    (void)unused1;(void)unused2;(void)unused3;return x4pd_parse64(text,';',NULL,NULL,fallback);
}
static float pdec_read32(const char *text,void *unused1,void *unused2,void *unused3,float fallback) {
    (void)unused1;(void)unused2;(void)unused3;return x4pd_parse32(text,';',NULL,NULL,fallback);
}
typedef struct {unsigned rva,size;const char *sha;void *target;} pdec_site;
static const pdec_site pdec_sites[PDEC_COUNT]={
    {0x14c3090u,0x4b4u,"2cdc7e8eb8fe0a5de5594a3ef478e1cf3381777a7d3ac0adf7beba54e12ec944",(void*)pdec_read64},
    {0x14c2bd0u,0x4b8u,"dad25ab672216a403a8d765e4fb2f5875121939000480425c7dafa402f732af6",(void*)pdec_read32},
};
static int pdec_installed,pdec_restored;
static unsigned char *pdec_page;
static unsigned char pdec_original[PDEC_COUNT][5];
static volatile LONG pdec_any;           /* an entry may hold our bytes: the view is on */
static unsigned pdec_busy;
static volatile LONG64 pdec_failures;
static const char *pdec_stage="idle";
static void pdec_patch_bytes(unsigned i,unsigned char out[5]) {
    int32_t rel=(int32_t)((intptr_t)(pdec_page+16*i)-(intptr_t)(image+pdec_sites[i].rva+5));out[0]=0xe9;memcpy(out+1,&rel,4);
}
static void pdec_stub_bytes(unsigned i,unsigned char out[16]) {
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};void *target=pdec_sites[i].target;
    memcpy(out,jump,6);memcpy(out+6,&target,8);out[14]=out[15]=0xcc;
}
/* ---- original-byte view (lockstep_gate.c fp_patch_view) ---- */
static int pdec_overlaps(const void *source,size_t bytes) {
    if(!*(volatile LONG*)&pdec_any || !image || !source || !bytes || !pdec_page) return 0;
    const unsigned char *p=source;
    for(unsigned i=0;i<PDEC_COUNT;i++) {const unsigned char *at=image+pdec_sites[i].rva;if(p<at+5 && p+bytes>at) return 1;}
    return 0;
}
static void pdec_unpatch_copy(const void *source,void *copy,size_t bytes) {
    if(!pdec_overlaps(source,bytes)) return;
    const unsigned char *p=source;
    for(unsigned i=0;i<PDEC_COUNT;i++) {
        const unsigned char *at=image+pdec_sites[i].rva;
        if(at>=p+bytes || at+5<=p) continue;
        size_t lo=at<p?(size_t)(p-at):0,hi=at+5>p+bytes?(size_t)(p+bytes-at):5;
        unsigned char ours[5],*c=(unsigned char*)copy+(at+lo-p);pdec_patch_bytes(i,ours);
        if(!memcmp(c,ours+lo,hi-lo)) memcpy(c,pdec_original[i]+lo,hi-lo);
    }
}
static int pdec_state(unsigned i) {
    unsigned char ours[5];const unsigned char *now=image+pdec_sites[i].rva;
    if(!memcmp(now,pdec_original[i],5)) return 0;
    if(!pdec_page) return -1;
    pdec_patch_bytes(i,ours);return memcmp(now,ours,5)?-1:1;
}
static int pdec_page_ok(void) {
    if(!pdec_page) return 0;
    for(unsigned i=0;i<PDEC_COUNT;i++) {unsigned char want[16];pdec_stub_bytes(i,want);if(memcmp(pdec_page+16*i,want,16)) return 0;}
    return 1;
}
static unsigned char *pdec_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+pdec_sites[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+pdec_sites[0].rva);
            if(d>INT32_MIN/2 && d<INT32_MAX/2) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int pdec_windows_clear(void) {
    for(unsigned t=0;t<pmath_frozen_count;t++) {
        CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_CONTROL;
        if(!GetThreadContext(pmath_frozen[t],&c)) return 0;
        for(unsigned i=0;i<PDEC_COUNT;i++) {
            uintptr_t entry=(uintptr_t)(image+pdec_sites[i].rva);
            if(c.Rip>entry && c.Rip<entry+5) return 0;
        }
    }
    return 1;
}
/* 1 switched, 0 nothing changed, -1 partial and stuck. */
static int pdec_switch_once(int install);
static int pdec_switch(int install) {
    if(install) InterlockedExchange(&pdec_any,1);
    int r=pdec_switch_once(install);
    if(install?r==0:r==1) InterlockedExchange(&pdec_any,0); /* both entries original again */
    return r;
}
static int pdec_switch_once(int install) {
    for(unsigned attempt=0;attempt<200;attempt++) {
        if(!pmath_freeze()) return 0;
        if(!pdec_windows_clear()) {pmath_thaw();++pdec_busy;Sleep(1);continue;}
        unsigned done=0;int ok=1,stuck=0;
        for(;done<PDEC_COUNT;done++) {
            unsigned char ours[5];pdec_patch_bytes(done,ours);
            if(!view_cas_bytes(pdec_sites[done].rva,install?pdec_original[done]:ours,install?ours:pdec_original[done],5)) {ok=0;break;}
        }
        if(!ok) for(unsigned i=0;i<done;i++) {
            unsigned char ours[5];pdec_patch_bytes(i,ours);
            if(!view_cas_bytes(pdec_sites[i].rva,install?ours:pdec_original[i],install?pdec_original[i]:ours,5)) stuck=1;
        }
        pmath_thaw();
        return ok?1:stuck?-1:0;
    }
    return 0;
}
static int pdec_pins(void) {
    for(unsigned i=0;i<PDEC_COUNT;i++) {
        static unsigned char copy[0x500];const pdec_site *s=&pdec_sites[i];
        if(s->size>sizeof(copy)) return 0;
        memcpy(copy,image+s->rva,s->size);  /* raw */
        if(pdec_installed) {
            unsigned char ours[5];pdec_patch_bytes(i,ours);
            if(memcmp(copy,ours,5)) return 0;
            memcpy(copy,pdec_original[i],5);
        }
        if(!hash_ok(copy,s->size,s->sha)) return 0;
    }
    return 1;
}
/* gate.decimal(): UI thread, before the first admitted frame. */
static int pdec_active(void) {return pdec_installed || InterlockedCompareExchange(&pdec_any,0,0);}
static int pdec_arm(void) {
    if(pdec_installed) return stats.active!=0;
    if(!stats.active || stats.issued || (boot_mode && boot_phase) || GetCurrentThreadId()!=ui_thread) {pdec_stage="state";return 0;}
    if(world_wait_started) {pdec_stage="world_wait_manifest";return 0;}
    if(!pdec_pins()) {pdec_stage="pins";return 0;}
    for(unsigned i=0;i<PDEC_COUNT;i++) memcpy(pdec_original[i],image+pdec_sites[i].rva,5); /* raw, just pinned */
    if(!pdec_page) pdec_page=pdec_alloc_page();
    if(!pdec_page) {pdec_stage="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(pdec_page,0x1000,PAGE_READWRITE,&old)) {pdec_stage="page";return 0;}
    memset(pdec_page,0xcc,0x1000);for(unsigned i=0;i<PDEC_COUNT;i++) pdec_stub_bytes(i,pdec_page+16*i);
    if(!VirtualProtect(pdec_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {pdec_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pdec_page,0x1000);
    pdec_failures=0;pdec_restored=0;
    int r=pdec_switch(1);
    if(r!=1) {pdec_stage=r<0?"stuck":"busy";if(r<0) InterlockedIncrement64(&pdec_failures);return 0;}
    pdec_installed=1;pdec_stage="armed";return 1;
}
static int pdec_owned(void) {
    if(!pdec_installed) return 1;
    for(unsigned i=0;i<PDEC_COUNT;i++) if(pdec_state(i)!=1) return 0;
    return pdec_page_ok();
}
/* Gate tick: any change stops the gate with reason 159. */
static int pdec_check(void) {
    if(InterlockedCompareExchange64(&pdec_failures,0,0)) return 0;
    if(!pdec_owned()) {pdec_stage="site_changed";InterlockedIncrement64(&pdec_failures);return 0;}
    return 1;
}
static void pdec_restore(void) {
    if(!pdec_installed) return;
    for(unsigned i=0;i<PDEC_COUNT;i++) if(pdec_state(i)!=1) {pdec_stage="restore_foreign";return;}
    int r=pdec_switch(0);
    if(r==1) {pdec_installed=0;pdec_restored=1;pdec_stage="restored";}
    else pdec_stage=r<0?"restore_stuck":"restore_busy";
}
static int pdec_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d readers=%u restored=%d stage=%s busy=%u failures=%lld",
        pdec_installed,(unsigned)PDEC_COUNT,pdec_restored,pdec_stage,pdec_busy,(long long)pdec_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
