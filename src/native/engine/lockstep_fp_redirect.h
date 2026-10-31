/* native_gate_fp_redirect = 'shadow' | 'redirect': Option B slice 1, the flight parameter refresh recomputed by our
 * own strict code next to the native one, on Windows and Linux alike. Default off; diagnostic in 'shadow' (nothing is
 * written to the game); 'redirect' is shadow plus two writes: the writer's +0x0..+0xc (fr_writer_run) become ours, and
 * on Windows the spline segment length of the two composite builders that inline BezierSpline::Set (fr_len_seen,
 * FR10/FR11), the Bezier handle length s = d/3 there (fr_handle_seen, FR13) and the PointGuard formation angle
 * (fr_pg_seen, FR16); 'writer' redirects only the writer and shadows the length, the handles and the angle. Fail
 * closed: a hooked site that no longer
 * leads here stops the session (FR_STOP_REASON).
 *
 * The method ("recompute after native on both platforms", reports/LOCKSTEP-LINUX-PORT-PLAN-20261030.txt, Option B):
 * the native refresh (Windows 0xea1e80, Linux 0x14378c0; args owner, slot, flag) runs unchanged with all its
 * traversal and side effects. This module captures the inputs of three of its float outputs on the way, and after
 * it returns recomputes them from those inputs in one strict C source (no fast-math, no contraction, the order
 * written below) and compares them with what native wrote:
 *   slot+0x24 = 1 / comp[A]                       comp[A] = Linux comp+0x5d4, Windows comp+0x5f4
 *   slot+0x4  = G4                                 the engine getter's result (Linux 0x15d1770, Windows 0x5f91b0)
 *   slot+0x0  = G0 * L(x0, x1, 100, 1, x, 1e-5)    G0 from Linux 0x15d18a0 / Windows 0x5f9340; x0 = 1/comp[B],
 *               x1 = 1/comp[A], x = max(x0, min(x1, w)), w = lane 2 of the owner's query vector at time t
 *   L(x0,x1,a,b,x,eps) = |x0-x1| < eps ? (b+a)/2 : x0 >= x ? a : x >= x1 ? b : ((x-x0)*(b-a))/(x1-x0) + a
 * On one branch native then multiplies +0x4 and +0x0 by M(flag, a, b, c) (Linux 0x1429e80, Windows 0xea3dc0):
 *   d = b-a; |d| < 1e-4 ? 1 : r = ((c-a)+(c-a))/d, r = r < 0 ? 0 : r, flag ? (1 < r ? 1 : r) : r
 * with a, b = slot+0x2c, slot+0x24 for +0x4 and the reverse for +0x0. That branch reads slot+0x24, a value this
 * method recomputes, before any overwrite: it is counted (m_branch), and its M is compared on its own. So is the
 * +0x8/+0xc branch, which also reads +0x24 (v8_branch: +0x8 or +0xc is no longer 400 after native).
 *
 * Hooks: the getter and M calls inside the refresh (rel32 call sites), and its two ends: Linux its prologue,
 * Windows its G4 call (the first thing on the path with the getters; the slot from r15) and a detour on the
 * common tail every exit passes (no caller is patched: other modules pin the callers' code). The getter hooks see
 * comp, the ship (o20) and the buffer the refresh passes; w and its 16-byte vector lie at a fixed distance below
 * that buffer in the refresh's frame (FR_W_FROM_BUFFER). The M hooks read the slot's +0x4 / +0x0 before native
 * multiplies them (the "pre" values). A per-thread context, keyed by GetCurrentThreadId() in a fixed table,
 * carries all of it from the hooks to the compare. Compared from arming on, the snapshot load included: in the
 * census fixture the full refreshes run in the load and the admitted ticks take only the flag path.
 *
 * Fields compared, each with the calls compared, the calls whose bits differ and the largest ulp distance:
 *   f0, f4   ours vs native before the M branch;   f24   ours vs native;   m   our M vs native M (per M call);
 *   f0_out, f4_out   ours (times our M when the branch ran) vs what native left in the slot.
 * FP_REDIRECT_ROW lines (watched saved IDs; the load as tick 0, then ticks 1..N): every input's bits and both
 * results, so the two platforms' rows show which input differs between them. The load's calls are kept unkeyed
 * and keyed after admission. Row and status code is shared by both platforms.
 *
 * Platform part (lockstep_gate.c / linux/gate_fp_redirect.h) defines FR_COMP_A, FR_COMP_B, FR_W_FROM_BUFFER,
 * FR_M_PARAMS / FR_M_ARGS (M's native argument order), installs the hooks and sets the fr_native_* pointers. */
#include <float.h>
#include "lockstep_spline_length.h"
#if defined(__FAST_MATH__) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD!=0
#error "lockstep_fp_redirect.h needs strict IEEE single precision (no fast-math, FLT_EVAL_METHOD 0)"
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#define FR_STOP_REASON 179u /* 178 cutscene ready */
#define FR_IDS 4u
#define FR_RECORDS 32768u /* the load's compared calls (FR1: under 20,130) and the ticks' watched ones */
#define FR_THREADS 128u
enum {FR_OFF=0,FR_SHADOW=1,FR_REDIRECT=2,FR_WRITER=3}; /* redirect: shadow, the writer's +0x0..+0xc and (Windows) the
                                                     * spline length overwritten with ours; writer: only the writer's */
enum {FR_F0=0,FR_F4,FR_F24,FR_M,FR_F0_OUT,FR_F4_OUT,FR_P0,FR_P4,FR_P8,FR_PC,FR_W0,FR_W4,FR_W8,FR_WC,FR_LEN,FR_HANDLE,FR_PG,FR_FIELDS};
enum {FR_SEEN_G4=1u,FR_SEEN_G0=2u,FR_SEEN_M4=4u,FR_SEEN_M0=8u,FR_SEEN_COMP=16u};
typedef void (*fr_refresh_fn)(void *,void *,uint64_t);
typedef float (*fr_getter_fn)(void *,void *,void *);
typedef float (*fr_m_fn)(FR_M_PARAMS);
typedef void (*fr_params_fn)(FR_P_PARAMS);
static fr_refresh_fn fr_native_refresh;
static fr_getter_fn fr_native_g4,fr_native_g0;
static fr_m_fn fr_native_m;
static fr_params_fn fr_native_params; /* the refresh's first call (Linux 0x148cb50, Windows 0x6dc610), on every path */
enum {FR_HIT_G4=0,FR_HIT_G0,FR_HIT_M4,FR_HIT_M0,FR_HIT_PARAMS,FR_HIT_PARAMS_FLAG,FR_HIT_V,FR_HIT_MULPS,FR_HIT_WRITER,FR_HIT_LEN0,FR_HIT_LEN1,FR_HIT_HANDLE0,FR_HIT_HANDLE3=FR_HIT_HANDLE0+3,FR_HIT_PG,FR_HITS};
enum {FR_PSEEN_V=1u,FR_PSEEN_MULPS=2u};
static uint32_t fr_k_read(void); /* the platform's table float K (below) */
typedef uint64_t (*fr_writer_fn)(void *,void *,void *,void *);
static fr_writer_fn fr_native_writer; /* Linux 0x14811c0, Windows 0x6db200 (o20, D, A, slot) */
enum {FR_KIND_COMPARE=0,FR_KIND_PARAMS=1};
#define FR_WINDOW 0x30u /* params rows: the slot's +0x0..+0x30 and +0x100..+0x130, before and after the call */
/* ---- The strict recomputation: one source for both platforms. Constants as their bits. ---- */
static float fr_f32(uint32_t bits) {float f;memcpy(&f,&bits,4);return f;}
static uint32_t fr_bits(float f) {uint32_t bits;memcpy(&bits,&f,4);return bits;}
static float fr_abs(float f) {return fr_f32(fr_bits(f)&0x7fffffffu);}
#define FR_ONE 0x3f800000u      /* 1.0 (Linux 0x2e24600, Windows 0x142cbe5d4) */
#define FR_LERP_A 0x42c80000u   /* 100.0 (Linux 0x2d5f3b4; Windows folds a = 100, a-b = 99) */
#define FR_LERP_EPS 0x3727c5acu /* 1e-5 (Linux 0x2e2716c, Windows 0x142cbe088) */
#define FR_HALF 0x3f000000u     /* 0.5 (Linux 0x2def394; Windows folds (a+b)/2 = 50.5) */
#define FR_M_EPS 0x38d1b717u    /* 1e-4 (Linux 0x2d1c600, Windows 0x142cbe0ac) */
static float fr_lerp(float x0,float x1,float a,float b,float x,float eps) {
    if(eps>fr_abs(x0-x1)) {float s=b+a;return s*fr_f32(FR_HALF);}
    if(x0>=x) return a;
    if(x>=x1) return b;
    float t=x-x0;t=t*(b-a);t=t/(x1-x0);return t+a;
}
static float fr_m(unsigned flag,float a,float b,float c) {
    float d=b-a;
    if(fr_f32(FR_M_EPS)>fr_abs(d)) return fr_f32(FR_ONE);
    float r=c-a;r=r+r;r=r/d;
    r=r<0.0f?0.0f:r;
    if(flag&0xffu) r=fr_f32(FR_ONE)<r?fr_f32(FR_ONE):r;
    return r;
}
/* +0x24, +0x4, +0x0 from comp[A], comp[B], G4, G0 and w (bits in, bits out). */
static void fr_recompute(uint32_t ca,uint32_t cb,uint32_t g4,uint32_t g0,uint32_t w,uint32_t out[3]) {
    const float one=fr_f32(FR_ONE),x1=one/fr_f32(ca),x0=one/fr_f32(cb),wf=fr_f32(w);
    const float m=x1<wf?x1:wf,x=x0>m?x0:m;
    const float l=fr_lerp(x0,x1,fr_f32(FR_LERP_A),one,x,fr_f32(FR_LERP_EPS));
    out[0]=fr_bits(fr_f32(g0)*l);out[1]=g4;out[2]=fr_bits(x1);
}
/* The first call's +0x0..+0xc (Linux 0x148cb50 at 0x148d81a..0x148d8cb, Windows 0x6dc610 at 0x6dcceb..0x6dcd94), on
 * its path with d > 1e-5, after Linux 0x18a15f0 / Windows 0x3dc040 has filled v (8 floats):
 *   new = max(K, |v_lane|) / d;  slot+i = new > slot+i ? new : slot+i;   lanes: +0x0 v2, +0x4 v2, +0x8 v0, +0xc v1.
 * d: a virtual of the ship (Linux +0x1eb8, Windows +0x1e28), times a table float on the branch that first calls
 * Linux 0x14811c0 / Windows 0x6db200 (the 'mulps' branch, which also scales +0x0..+0xc). K: Linux [[0x3db83c8]+0x318],
 * Windows [[0x146cf1538]+0x300]. Windows computes max(K,|v|) * (1/d) (divss 1.0/d at 0x6dcd14, then mulss): a
 * reciprocal Linux does not take, the source of FR3's one-ulp split. Ours divides, as Linux. */
static uint32_t fr_p_new(uint32_t k,uint32_t lane,uint32_t d,uint32_t cur) {
    const float kf=fr_f32(k),a=fr_abs(fr_f32(lane)),m=kf>a?kf:a,n=m/fr_f32(d),c=fr_f32(cur);
    return fr_bits(n>c?n:c);
}
static void fr_p_recompute(const uint32_t v[8],uint32_t k,uint32_t d,const uint32_t cur[4],uint32_t out[4]) {
    out[0]=fr_p_new(k,v[2],d,cur[0]);out[1]=fr_p_new(k,v[2],d,cur[1]);out[2]=fr_p_new(k,v[0],d,cur[2]);out[3]=fr_p_new(k,v[1],d,cur[3]);
}
/* The writer (Linux 0x14811c0, Windows 0x6db200; o20, D, A, slot). The first call calls it on the watched ships'
 * path (Linux 0x148cd5e, Windows 0x6dcaaf, right after the ship's defaults struct is copied into the slot) and on
 * its 'mulps' branch (Linux 0x148d94e, Windows 0x6dcbda). Its +0x0..+0xc as the slot holds them when it returns, in
 * Linux offsets (Windows: D and the data multipliers 0x10 lower, the virtuals 0x90 lower, the table's fields below):
 *   all four |A+0x0..+0xc| < 1e-4:  q_i = K
 *   else  s = D914*T4fd0;  den = ((D914*r78) + (s < r80 ? s : r80)) + (s < r88 ? s : r88)
 *         q0 = (a0/den)*D918;  q1 = (a1/den)*D91c;  q2 = ((a2/den)*D920)*p;  q3 = ((a3/den)*D924)*p
 *         p = [[o20+0x398]+0x50]; without that pointer q2 takes no multiply and q3 one by 1.0
 *   then  (Linux 0x1481050 / Windows 0x6db600) q_i *= D998+4i;
 *         (Linux 0x1481150 / Windows inline) x = q0*T330 > K ? q0*T330 : K;  q2 = x < q2 ? x : q2;  q3 likewise.
 * r78..r98: the ship's virtuals +0x2078, +0x2080, +0x2088, +0x2090, +0x2098 (no arguments; Windows +0x1fe8..+0x2008).
 * T: Linux [0x3db83c8] (+0x4fd0, K +0x318, +0x330); Windows [0x146cf1538] (+0x5050, K +0x300, +0x318).
 * Windows computes inv = 1.0/den once (0x6db3e3) and q0 = (inv*a0)*D918, q1 = (inv*a1)*D91c, q2 = (p*D920)*(inv*a2),
 * q3 = (p*D924)*(inv*a3): the reciprocal rewrite (and, for q2/q3, a reassociation) behind FR3's one-ulp split of all
 * four. Every other output of the writer is computed in the same order on both. Ours is Linux's order. */
typedef struct {uint32_t a[4],d[5],dm[4],r[5],t_scale,k,t_clamp,p,has_p;} fr_w_in;
static void fr_w_recompute(const fr_w_in *in,uint32_t out[4]) {
    const float eps=fr_f32(FR_M_EPS),k=fr_f32(in->k);float q[4];
    if(eps>fr_abs(fr_f32(in->a[0])) && eps>fr_abs(fr_f32(in->a[1])) && eps>fr_abs(fr_f32(in->a[2])) && eps>fr_abs(fr_f32(in->a[3]))) {
        q[0]=q[1]=q[2]=q[3]=k;
    } else {
        const float d0=fr_f32(in->d[0]),s=d0*fr_f32(in->t_scale),r80=fr_f32(in->r[1]),r88=fr_f32(in->r[2]);
        float den=d0*fr_f32(in->r[0]);den=den+(s<r80?s:r80);den=den+(s<r88?s:r88);
        for(unsigned i=0;i<4;i++) {float t=fr_f32(in->a[i])/den;q[i]=t*fr_f32(in->d[1+i]);}
        if(in->has_p) q[2]=q[2]*fr_f32(in->p);
        q[3]=q[3]*fr_f32(in->has_p?in->p:FR_ONE);
    }
    for(unsigned i=0;i<4;i++) q[i]=q[i]*fr_f32(in->dm[i]);
    float x=q[0]*fr_f32(in->t_clamp);x=x>k?x:k;
    q[2]=x<q[2]?x:q[2];q[3]=x<q[3]?x:q[3];
    for(unsigned i=0;i<4;i++) out[i]=fr_bits(q[i]);
}
/* The writer's inputs as the platform lays them out: the table pointer's address, the table's three fields
 * (scale, K, clamp), D's first field and first multiplier, the first virtual's vtable offset. The five virtuals
 * are called again here, after native returned: in the large ship vtables they are getters (Linux 0x10e7ba0
 * returns 1.0, 0x1089fa0 0.0, 0x1168e90 a field, 0x1173210 a field it computes once and caches), so the call
 * changes nothing and native has already run them. 0 when anything cannot be read. */
typedef float (*fr_vfloat_fn)(void *);
static int fr_w_gather(void *o20,const unsigned char *data,const unsigned char *a,const void *table_at,unsigned t_scale,unsigned t_k,
        unsigned t_clamp,unsigned d0,unsigned dm0,unsigned vt0,fr_w_in *in) {
    const unsigned char *table=NULL,*vt=NULL,*pp=NULL;
    if(!read_local(table_at,&table,8) || !table || !read_local(a,in->a,16) || !read_local(data+d0,in->d,20) || !read_local(data+dm0,in->dm,16)) return 0;
    if(!read_local(table+t_scale,&in->t_scale,4) || !read_local(table+t_k,&in->k,4) || !read_local(table+t_clamp,&in->t_clamp,4)) return 0;
    if(!read_local((const unsigned char*)o20+0x398,&pp,8)) return 0;
    in->has_p=pp!=NULL;in->p=FR_ONE;
    if(pp && !read_local(pp+0x50,&in->p,4)) return 0;
    if(!read_local(o20,&vt,8) || !vt) return 0;
    for(unsigned i=0;i<5;i++) {
        fr_vfloat_fn fn=NULL;
        if(!read_local(vt+vt0+8u*i,&fn,8) || !fn) return 0;
        in->r[i]=fr_bits(fn(o20));
    }
    return 1;
}
static int fr_w_read(void *o20,const unsigned char *data,const unsigned char *a,fr_w_in *in); /* the platform's layout (below) */
/* ---- State ---- */
typedef struct {
    unsigned depth,seen;unsigned char *slot;const unsigned char *comp;const unsigned char *o20;
    uint32_t ca,cb,g4,g0,w;unsigned char vec[16],buf[32];
    uint32_t m_a[2],m_b[2],m_c[2],m_flag[2],m_native[2],m_ours[2],m_pre[2];
    unsigned char *pslot;unsigned pdepth,pseen;uint32_t pd,pk,pv[8],pcur[4]; /* the first call's */
    unsigned wn,wsame;fr_w_in w_in;uint32_t w_native[4],w_ours[4]; /* its last writer call */
} fr_ctx;
/* A compared call kept for rows: the ship's runtime ID, its key resolved at once in admitted ticks, after
 * admission (fr_row) for the load's calls, whose identities are not sealed yet. */
typedef struct {
    volatile LONG64 ready;uint64_t tick,key,runtime;uint32_t thread,seen,kind,flag;unsigned char pre[2*FR_WINDOW],post[2*FR_WINDOW];
    uint32_t pseen,pd,pk,pv[8],pcur[4],pours[4];
    uint32_t wn,wsame;fr_w_in w_in;uint32_t w_native[4],w_ours[4];
    uint32_t ca,cb,g4,g0,w,native[3],ours[3],v8,v12;unsigned char vec[16],buf[32];
    uint32_t m_a[2],m_b[2],m_c[2],m_flag[2],m_native[2],m_ours[2],m_pre[2];
} fr_record_t;
static volatile LONG64 fr_thread_keys[FR_THREADS];
static fr_ctx fr_ctxs[FR_THREADS];
static fr_record_t fr_ring[FR_RECORDS];
static uint64_t fr_ids[FR_IDS];
static unsigned fr_id_count,fr_installed,fr_restored;
static int fr_mode;
static uint64_t fr_ticks=3;
static const char *fr_stage="idle";
static volatile LONG64 fr_calls,fr_compared,fr_load_compared,fr_skipped,fr_nested,fr_unfinished,fr_no_context,fr_orphans;
static volatile LONG64 fr_m_branch,fr_v8_branch,fr_comp_changed,fr_records,fr_dropped,fr_failures,fr_w_unread,fr_w_written,fr_len_written,fr_handle_written,fr_pg_written;
static volatile LONG64 fr_n[FR_FIELDS],fr_diff[FR_FIELDS],fr_max_ulp[FR_FIELDS];
/* Every stub's entries, counted before anything else: a hook that never fires shows as 0 here. */
static volatile LONG64 fr_hits[FR_HITS];
/* The row scan (fr_row, UI thread only): next record, rows given, records scanned and those with a saved key. */
static uint64_t fr_scan,fr_emitted,fr_scanned,fr_keyed,fr_ordinals[FR_IDS],fr_ordinal_tick[FR_IDS];
/* This thread's context; claimed on first use (X4's threads are a fixed pool; a full table only skips). */
static fr_ctx *fr_context(int claim) {
    const LONG64 key=(LONG64)GetCurrentThreadId()+1;
    for(unsigned i=0;i<FR_THREADS;i++) if(fr_thread_keys[i]==key) return &fr_ctxs[i];
    if(!claim) return NULL;
    for(unsigned i=0;i<FR_THREADS;i++)
        if(!fr_thread_keys[i] && InterlockedCompareExchange64(&fr_thread_keys[i],key,0)==0) return &fr_ctxs[i];
    return NULL;
}
/* ulp distance of two floats' bits; 0xffffffff when only one is a NaN or the NaNs differ. */
static uint32_t fr_ulp(uint32_t a,uint32_t b) {
    if(a==b) return 0;
    if((a&0x7fffffffu)>0x7f800000u || (b&0x7fffffffu)>0x7f800000u) return 0xffffffffu;
    const int64_t x=(a>>31)?-(int64_t)(a&0x7fffffffu):(int64_t)a,y=(b>>31)?-(int64_t)(b&0x7fffffffu):(int64_t)b;
    const int64_t d=x>y?x-y:y-x;return d>=0xffffffffLL?0xfffffffeu:(uint32_t)d;
}
static void fr_field(unsigned field,uint32_t ours,uint32_t native) {
    InterlockedIncrement64(&fr_n[field]);
    if(ours==native) return;
    InterlockedIncrement64(&fr_diff[field]);
    const LONG64 d=(LONG64)fr_ulp(ours,native);LONG64 now=fr_max_ulp[field];
    while(d>now) {const LONG64 seen=InterlockedCompareExchange64(&fr_max_ulp[field],d,now);if(seen==now) break;now=seen;}
}
/* From arming on, the load included: the full refreshes (with the getters) run in the load, the admitted ticks of
 * the census fixture take only the flag path (FR1: 20,130 calls, 98 of them in ticks 1-5, none with getters). */
static int fr_comparing(void) {return (fr_mode==FR_SHADOW || fr_mode==FR_REDIRECT || fr_mode==FR_WRITER) && stats.active;}
static int fr_writes_writer(void) {return fr_mode==FR_REDIRECT || fr_mode==FR_WRITER;}
/* A refresh begins (Linux: its entry; Windows: its G4 call, the first thing on the path with the getters). */
static fr_ctx *fr_begin(void *slot) {
    if(!fr_comparing()) return NULL;
    fr_ctx *c=fr_context(1);
    if(!c) {InterlockedIncrement64(&fr_no_context);return NULL;}
    if(c->depth) {InterlockedIncrement64(&fr_unfinished);} /* the previous one never reached its end; restart */
    c->depth=1;c->seen=0;c->slot=slot;c->comp=NULL;c->o20=NULL;
    return c;
}
/* A row record for the ship o20 (its runtime ID at +8): in admitted ticks 1..N only a watched ship's, keyed at once;
 * in the load (tick 0) every one, keyed after admission by fr_row. NULL when none is kept. */
static fr_record_t *fr_record_take(const unsigned char *o20,uint32_t kind) {
    const uint64_t tick=stats.issued;
    if(!fr_id_count || tick>fr_ticks || !o20) return NULL;
    uint64_t runtime=0,key=0;
    if(!read_local(o20+8,&runtime,8) || !runtime) return NULL;
    if(tick) {
        unsigned klass=WORLD_KEY_NONE;int watched=0;
        if(!world_identity_installed || !(key=world_identity_key(runtime,&klass)) || klass!=WORLD_KEY_SAVED) return NULL;
        for(unsigned i=0;i<fr_id_count;i++) if(fr_ids[i]==key) watched=1;
        if(!watched) return NULL;
    }
    const LONG64 index=InterlockedIncrement64(&fr_records)-1;
    if(index>=(LONG64)FR_RECORDS) {InterlockedIncrement64(&fr_dropped);return NULL;}
    fr_record_t *r=&fr_ring[index];
    r->tick=tick;r->key=key;r->runtime=runtime;r->thread=GetCurrentThreadId()==ui_thread?0:1;r->kind=kind;r->flag=0;r->seen=0;
    return r;
}
static void fr_record_give(fr_record_t *r) {InterlockedExchange64(&r->ready,(LONG64)(r-fr_ring)+1);}
/* The refresh has written everything: compare, and keep a row candidate. */
static void fr_end(fr_ctx *c) {
    c->depth=0;
    if((c->seen&(FR_SEEN_G4|FR_SEEN_G0))!=(FR_SEEN_G4|FR_SEEN_G0)) {InterlockedIncrement64(&fr_skipped);return;} /* the flag or early path */
    uint32_t ours[3],native_out[3],v8,v12;
    fr_recompute(c->ca,c->cb,c->g4,c->g0,c->w,ours);
    memcpy(&native_out[0],c->slot,4);memcpy(&native_out[1],c->slot+4,4);memcpy(&native_out[2],c->slot+0x24,4);
    memcpy(&v8,c->slot+8,4);memcpy(&v12,c->slot+0xc,4);
    {uint32_t ca,cb;memcpy(&ca,c->comp+FR_COMP_A,4);memcpy(&cb,c->comp+FR_COMP_B,4);if(ca!=c->ca || cb!=c->cb) InterlockedIncrement64(&fr_comp_changed);}
    const uint64_t tick=stats.issued;
    InterlockedIncrement64(&fr_compared);
    if(!tick) InterlockedIncrement64(&fr_load_compared);
    if(c->seen&FR_SEEN_COMP) InterlockedIncrement64(&fr_comp_changed);
    if(v8!=0x43c80000u || v12!=0x43c80000u) InterlockedIncrement64(&fr_v8_branch);
    const int m4=(c->seen&FR_SEEN_M4)!=0,m0=(c->seen&FR_SEEN_M0)!=0;
    if(m4 || m0) InterlockedIncrement64(&fr_m_branch);
    fr_field(FR_F0,ours[0],m0?c->m_pre[1]:native_out[0]);
    fr_field(FR_F4,ours[1],m4?c->m_pre[0]:native_out[1]);
    fr_field(FR_F24,ours[2],native_out[2]);
    fr_field(FR_F0_OUT,m0?fr_bits(fr_f32(c->m_ours[1])*fr_f32(ours[0])):ours[0],native_out[0]);
    fr_field(FR_F4_OUT,m4?fr_bits(fr_f32(c->m_ours[0])*fr_f32(ours[1])):ours[1],native_out[1]);
    fr_record_t *r=fr_record_take(c->o20,FR_KIND_COMPARE);
    if(!r) return;
    r->seen=c->seen;
    r->ca=c->ca;r->cb=c->cb;r->g4=c->g4;r->g0=c->g0;r->w=c->w;memcpy(r->vec,c->vec,16);memcpy(r->buf,c->buf,32);
    memcpy(r->native,native_out,12);memcpy(r->ours,ours,12);r->v8=v8;r->v12=v12;
    memcpy(r->m_a,c->m_a,8);memcpy(r->m_b,c->m_b,8);memcpy(r->m_c,c->m_c,8);memcpy(r->m_flag,c->m_flag,8);
    memcpy(r->m_native,c->m_native,8);memcpy(r->m_ours,c->m_ours,8);memcpy(r->m_pre,c->m_pre,8);
    fr_record_give(r);
}
/* ---- Hooks (shared; the platform part routes the sites here and sets fr_native_*) ---- */
/* which 0: G4 (first, +0x4), 1: G0 (+0x0). Called with what the native getter returned. */
static void fr_getter_seen(unsigned which,void *comp,void *o20,const unsigned char *buf,float value) {
    fr_ctx *c=fr_context(0);
    if(!c || !c->depth) {if(fr_comparing()) InterlockedIncrement64(&fr_orphans);return;} /* a refresh not entered through our hook */
    if(c->comp && c->comp!=comp) c->seen|=FR_SEEN_COMP;
    c->comp=comp;c->o20=o20;
    if(which==0) {
        c->g4=fr_bits(value);c->seen|=FR_SEEN_G4;
        memcpy(c->vec,buf+FR_W_FROM_BUFFER-8,16);memcpy(&c->w,buf+FR_W_FROM_BUFFER,4);memcpy(c->buf,buf,32);
    } else {
        c->g0=fr_bits(value);c->seen|=FR_SEEN_G0;
        memcpy(&c->ca,c->comp+FR_COMP_A,4);memcpy(&c->cb,c->comp+FR_COMP_B,4);
    }
}
/* which 0: the M for +0x4 (first), 1: for +0x0. Reads the slot value native is about to multiply. */
static void fr_m_seen(unsigned which,uint64_t flag,float a,float b,float c_lane,float native) {
    fr_ctx *c=fr_context(0);
    if(!c || !c->depth) {if(fr_comparing()) InterlockedIncrement64(&fr_orphans);return;} /* a refresh not entered through our hook */
    c->seen|=which?FR_SEEN_M0:FR_SEEN_M4;
    c->m_a[which]=fr_bits(a);c->m_b[which]=fr_bits(b);c->m_c[which]=fr_bits(c_lane);c->m_flag[which]=(uint32_t)(flag&0xffu);
    c->m_native[which]=fr_bits(native);c->m_ours[which]=fr_bits(fr_m((unsigned)(flag&0xffu),a,b,c_lane));
    memcpy(&c->m_pre[which],c->slot+(which?0:4),4);
    fr_field(FR_M,c->m_ours[which],c->m_native[which]);
}
static float fr_g4_hook(void *comp,void *o20,void *buf) {
    InterlockedIncrement64(&fr_hits[FR_HIT_G4]);
    const float v=fr_native_g4(comp,o20,buf);fr_getter_seen(0,comp,o20,buf,v);return v;
}
static float fr_g0_hook(void *comp,void *o20,void *buf) {
    InterlockedIncrement64(&fr_hits[FR_HIT_G0]);
    const float v=fr_native_g0(comp,o20,buf);fr_getter_seen(1,comp,o20,buf,v);return v;
}
static float fr_m4_hook(FR_M_PARAMS) {InterlockedIncrement64(&fr_hits[FR_HIT_M4]);const float v=fr_native_m(FR_M_ARGS);fr_m_seen(0,flag,a,b,c,v);return v;}
static float fr_m0_hook(FR_M_PARAMS) {InterlockedIncrement64(&fr_hits[FR_HIT_M0]);const float v=fr_native_m(FR_M_ARGS);fr_m_seen(1,flag,a,b,c,v);return v;}
/* The refresh's first call, on every path (the flag path included): counted by flag, and for watched ships the
 * slot's two windows before and after it (FR2: the getter path never ran in the fixture, so +0x0/+0x4/+0x24 are
 * written elsewhere; this shows whether here). */
static void fr_params_hook(FR_P_PARAMS) {
    InterlockedIncrement64(&fr_hits[FR_HIT_PARAMS]);
    if(flag&0xffu) InterlockedIncrement64(&fr_hits[FR_HIT_PARAMS_FLAG]);
    fr_ctx *c=fr_comparing() && slot?fr_context(1):NULL;
    if(c) {c->pslot=slot;c->pdepth=1;c->pseen=0;c->wn=0;c->wsame=0;}
    fr_record_t *r=c?fr_record_take((const unsigned char*)o20,FR_KIND_PARAMS):NULL;
    if(r) {r->flag=(uint32_t)(flag&0xffu);memcpy(r->pre,slot,FR_WINDOW);memcpy(r->pre+FR_WINDOW,slot+0x100,FR_WINDOW);}
    fr_native_params(FR_P_ARGS);
    if(!c) return;
    c->pdepth=0;uint32_t ours[4]={0,0,0,0};
    if(c->pseen&FR_PSEEN_V) {
        uint32_t native[4];fr_p_recompute(c->pv,c->pk,c->pd,c->pcur,ours);memcpy(native,slot,16);
        for(unsigned i=0;i<4;i++) fr_field(FR_P0+i,ours[i],native[i]);
    }
    if(r) {
        r->pseen=c->pseen;r->pd=c->pd;r->pk=c->pk;memcpy(r->pv,c->pv,32);memcpy(r->pcur,c->pcur,16);memcpy(r->pours,ours,16);
        r->wn=c->wn;r->wsame=c->wsame;r->w_in=c->w_in;memcpy(r->w_native,c->w_native,16);memcpy(r->w_ours,c->w_ours,16);
        memcpy(r->post,slot,FR_WINDOW);memcpy(r->post+FR_WINDOW,slot+0x100,FR_WINDOW);fr_record_give(r);
    }
}
/* After v is filled, right before the max block: v (32 bytes at buf), d, K and the slot's +0x0..+0xc as they are. */
static void fr_v_seen(const unsigned char *buf,float d) {
    InterlockedIncrement64(&fr_hits[FR_HIT_V]);
    fr_ctx *c=fr_context(0);
    if(!c || !c->pdepth) {if(fr_comparing()) InterlockedIncrement64(&fr_orphans);return;}
    c->pseen|=FR_PSEEN_V;c->pd=fr_bits(d);c->pk=fr_k_read();memcpy(c->pv,buf,32);memcpy(c->pcur,c->pslot,16);
}
/* The branch through Linux 0x14811c0 / Windows 0x6db200 began. */
static void fr_mulps_seen(void) {
    InterlockedIncrement64(&fr_hits[FR_HIT_MULPS]);
    fr_ctx *c=fr_context(0);if(c && c->pdepth) c->pseen|=FR_PSEEN_MULPS;
}
/* Either call of the writer: native first, unchanged; then (comparing) its inputs, our +0x0..+0xc and native's as
 * the slot holds them now, before the caller reads them. Inside a first call the last one is kept for its row.
 * 'redirect': ours replaces native's +0x0..+0xc here, before the first call copies them (+0x10c, +0x11c) or scales
 * them; on Linux ours equals native (its own order), on Windows it is Linux's value. Inputs that cannot be read in
 * redirect mode are a failure (the session stops, FR_STOP_REASON): the platforms would part there. */
static uint64_t fr_writer_run(void *o20,void *data,void *a,void *slot) {
    InterlockedIncrement64(&fr_hits[FR_HIT_WRITER]);
    const uint64_t result=fr_native_writer(o20,data,a,slot);
    if(!fr_comparing() || !o20 || !data || !a || !slot) return result;
    fr_w_in in;uint32_t ours[4],native[4];
    if(!fr_w_read(o20,(const unsigned char*)data,(const unsigned char*)a,&in)) {
        InterlockedIncrement64(&fr_w_unread);if(fr_writes_writer()) InterlockedIncrement64(&fr_failures);
        return result;
    }
    fr_w_recompute(&in,ours);memcpy(native,slot,16);
    for(unsigned i=0;i<4;i++) fr_field(FR_W0+i,ours[i],native[i]);
    if(fr_writes_writer() && memcmp(ours,native,16)) {memcpy(slot,ours,16);InterlockedIncrement64(&fr_w_written);}
    fr_ctx *c=fr_context(0);
    if(c && c->pdepth) {++c->wn;c->wsame=(unsigned char*)slot==c->pslot;c->w_in=in;memcpy(c->w_native,native,16);memcpy(c->w_ours,ours,16);}
    return result;
}
/* FR10/FR11: BezierSpline::Set's length where Windows inlines it (the composite builders 0x857960 and 0x31c640;
 * lockstep_spline_length.h). The platform hooks the block's multiply by 0.5 and hands over the segment record
 * (control points at +0x60..+0x90, stored by then; length at +0x18) and the block's value; this owns the store.
 * Off or not comparing: the block's own (0 or NaN -> 1.0). 'shadow': the same, ours compared (len). 'redirect': the
 * canonical length with Linux's rule (0 -> 1.0), counted in len_written where the stored bits change; 'writer' (the
 * writer redirected, this shadowed) as 'shadow'. Returns what
 * the block's register holds afterwards (native, or ours in redirect). */
static inline uint32_t fr_len_seen(unsigned which,unsigned char *element,uint32_t native) {
    InterlockedIncrement64(&fr_hits[FR_HIT_LEN0+which]);
    uint32_t value=native,stored=sl_stored_windows(native);
    if(fr_comparing()) {
        const uint32_t ours=sl_length(element+0x60);
        fr_field(FR_LEN,ours,native);
        if(fr_mode==FR_REDIRECT) {
            value=ours;
            if(sl_stored_linux(ours)!=stored) InterlockedIncrement64(&fr_len_written);
            stored=sl_stored_linux(ours);
        }
    }
    memcpy(element+0x18,&stored,4);
    return value;
}
/* FR13: the Bezier handle length the same builders form where the caller left it 0, a third of the segment's chord d
 * (lockstep_spline_length.h sl_handle). The platform hooks the multiply by k = 0.333333343f (the whole instruction)
 * and hands over d and k from their registers; this returns what the register holds afterwards. Off or not comparing:
 * native, d*k. 'shadow' / 'writer': native, ours compared (handle). 'redirect': ours, d/3.0f, Linux's division,
 * counted in handle_written where the bits change. */
static inline uint32_t fr_handle_seen(unsigned which,uint32_t d,uint32_t k) {
    InterlockedIncrement64(&fr_hits[FR_HIT_HANDLE0+(which&3u)]);
    const uint32_t native=sl_handle_windows(d,k);
    if(!fr_comparing()) return native;
    const uint32_t ours=sl_handle(d);
    fr_field(FR_HANDLE,ours,native);
    if(fr_mode!=FR_REDIRECT) return native;
    if(ours!=native) InterlockedIncrement64(&fr_handle_written);
    return ours;
}
/* FR16: the angle of a PointGuard formation slot (PointGuardFormationShape vtable slot 6: Windows 0xe9c380, Linux
 * 0x13ccca0), the wingman's place on a half circle around the leader, a = rem * pi / (m+1) with rem = member index %
 * (m+1), m = min(wingmen, the shape's per-ring count). Its sine and cosine scale the shape's radius into the offset
 * that Formation::GetTargetPositionForWingMan (Windows 0xe99d70, Linux 0x1e592d0) turns into the flight model target.
 * Linux divides first and doubles: t = rem / ((m+1) + (m+1)), then the circle point 0x1a47f30 takes t * 6.28318548f.
 * Windows inlines it as rem * (3.14159274f / (m+1)): another rounding, one ulp of the angle in about a quarter of the
 * (m+1, rem) pairs up to 32, which moves the target by a fraction of an ulp and now and then flips the last bit of a
 * world coordinate (FR15: 726de x, 83761 z). rem and m+1 arrive as the floats both platforms convert identically. */
static inline uint32_t fr_pg_angle(uint32_t rem_bits,uint32_t m1_bits) {
    float rem,m1,t,a;memcpy(&rem,&rem_bits,4);memcpy(&m1,&m1_bits,4);
    t=rem/(m1+m1);a=t*6.28318548f;
    uint32_t bits;memcpy(&bits,&a,4);return bits;
}
static inline uint32_t fr_pg_angle_windows(uint32_t rem_bits,uint32_t m1_bits) {
    float rem,m1,k,a;memcpy(&rem,&rem_bits,4);memcpy(&m1,&m1_bits,4);
    k=3.14159274f/m1;a=rem*k;
    uint32_t bits;memcpy(&bits,&a,4);return bits;
}
/* The platform hooks the three instructions that form the angle and hands over rem and m+1 from their registers; this
 * returns the angle. Off or not comparing: native. 'shadow' / 'writer': native, ours compared (pg). 'redirect': ours,
 * counted in pg_written where the bits change. */
static inline uint32_t fr_pg_seen(unsigned which,uint32_t rem,uint32_t m1) {
    (void)which;InterlockedIncrement64(&fr_hits[FR_HIT_PG]);
    const uint32_t native=fr_pg_angle_windows(rem,m1);
    if(!fr_comparing()) return native;
    const uint32_t ours=fr_pg_angle(rem,m1);
    fr_field(FR_PG,ours,native);
    if(fr_mode!=FR_REDIRECT) return native;
    if(ours!=native) InterlockedIncrement64(&fr_pg_written);
    return ours;
}
/* ---- Configuration, rows, status ---- */
/* A saved ID to row; before arming. A loading-UI rerun: an ID already watched is accepted, a new one refused. */
static int fr_watch(uint64_t id) {
    if(!id) return 0;
    for(unsigned i=0;i<fr_id_count;i++) if(fr_ids[i]==id) return 1;
    if(fr_installed || fr_id_count>=FR_IDS) return 0;
    fr_ids[fr_id_count++]=id;return 1;
}
/* Checks shared by both arms; the platform installs afterwards. */
static int fr_arm_ready(int mode,uint64_t ticks) {
    if(mode!=FR_SHADOW && mode!=FR_REDIRECT && mode!=FR_WRITER) {fr_stage="mode";return 0;}
    if(!ticks || ticks>600) {fr_stage="ticks";return 0;}
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {fr_stage="refused";return 0;}
    if(fr_id_count && !world_identity_installed) {fr_stage="needs";return 0;}
    fr_ticks=ticks;fr_calls=fr_compared=fr_load_compared=fr_skipped=fr_nested=fr_unfinished=fr_no_context=fr_orphans=0;
    fr_m_branch=fr_v8_branch=fr_comp_changed=fr_records=fr_dropped=fr_failures=fr_w_unread=fr_w_written=fr_len_written=fr_handle_written=fr_pg_written=0;
    fr_scan=fr_emitted=fr_scanned=fr_keyed=0;memset(fr_ordinal_tick,0xff,sizeof(fr_ordinal_tick));
    for(unsigned f=0;f<FR_FIELDS;f++) fr_n[f]=fr_diff[f]=fr_max_ulp[f]=0;
    for(unsigned h=0;h<FR_HITS;h++) fr_hits[h]=0;
    return 1;
}
static int fr_status(char *out,size_t cap) {
    static const char *const names[FR_LEN]={"f0","f4","f24","m","f0_out","f4_out","p0","p4","p8","pc","w0","w4","w8","wc"};
    int n=snprintf(out,cap,"installed=%u restored=%u mode=%s stage=%s calls=%lld compared=%lld load_compared=%lld skipped=%lld unfinished=%lld no_context=%lld orphans=%lld m_branch=%lld v8_branch=%lld comp_changed=%lld",
        fr_installed,fr_restored,fr_mode==FR_SHADOW?"shadow":fr_mode==FR_REDIRECT?"redirect":fr_mode==FR_WRITER?"writer":"off",fr_stage,(long long)fr_calls,(long long)fr_compared,(long long)fr_load_compared,
        (long long)fr_skipped,(long long)fr_unfinished,(long long)fr_no_context,(long long)fr_orphans,(long long)fr_m_branch,(long long)fr_v8_branch,(long long)fr_comp_changed);
    if(n<=0 || (size_t)n>=cap) return 0;
    for(unsigned f=0;f<FR_LEN;f++) { /* field=max_ulp:calls_differing:calls_compared; len at the end */
        int m=snprintf(out+n,cap-(size_t)n," %s=%lld:%lld:%lld",names[f],(long long)fr_max_ulp[f],(long long)fr_diff[f],(long long)fr_n[f]);
        if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
        n+=m;
    }
    int m=snprintf(out+n,cap-(size_t)n," hits=g4:%lld,g0:%lld,m4:%lld,m0:%lld params=%lld params_flag=%lld v=%lld mulps=%lld writer=%lld w_unread=%lld w_written=%lld",(long long)fr_hits[FR_HIT_G4],(long long)fr_hits[FR_HIT_G0],
        (long long)fr_hits[FR_HIT_M4],(long long)fr_hits[FR_HIT_M0],(long long)fr_hits[FR_HIT_PARAMS],(long long)fr_hits[FR_HIT_PARAMS_FLAG],(long long)fr_hits[FR_HIT_V],(long long)fr_hits[FR_HIT_MULPS],
        (long long)fr_hits[FR_HIT_WRITER],(long long)fr_w_unread,(long long)fr_w_written);
    if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
    n+=m;
    m=snprintf(out+n,cap-(size_t)n," ids=%u ticks=%llu records=%lld dropped=%lld scanned=%llu keyed=%llu rows=%llu failures=%lld",fr_id_count,
        (unsigned long long)fr_ticks,(long long)(fr_records<(LONG64)FR_RECORDS?fr_records:(LONG64)FR_RECORDS),(long long)fr_dropped,
        (unsigned long long)fr_scanned,(unsigned long long)fr_keyed,(unsigned long long)fr_emitted,(long long)fr_failures);
    if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
    n+=m;
    /* FR10/FR11 (Windows only; zeros on Linux, whose Set needs no hook): len=max_ulp:differing:compared, per builder. */
    m=snprintf(out+n,cap-(size_t)n," len=%lld:%lld:%lld len_hits=%lld,%lld len_written=%lld",(long long)fr_max_ulp[FR_LEN],(long long)fr_diff[FR_LEN],
        (long long)fr_n[FR_LEN],(long long)fr_hits[FR_HIT_LEN0],(long long)fr_hits[FR_HIT_LEN1],(long long)fr_len_written);
    if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
    n+=m;
    /* FR13 (Windows only; zeros on Linux, which divides by 3.0 natively): handle=max_ulp:differing:compared, per site. */
    m=snprintf(out+n,cap-(size_t)n," handle=%lld:%lld:%lld handle_hits=%lld,%lld,%lld,%lld handle_written=%lld",(long long)fr_max_ulp[FR_HANDLE],
        (long long)fr_diff[FR_HANDLE],(long long)fr_n[FR_HANDLE],(long long)fr_hits[FR_HIT_HANDLE0],(long long)fr_hits[FR_HIT_HANDLE0+1],
        (long long)fr_hits[FR_HIT_HANDLE0+2],(long long)fr_hits[FR_HIT_HANDLE0+3],(long long)fr_handle_written);
    if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
    n+=m;
    /* FR16 (Windows only; zeros on Linux, which divides first natively): pg=max_ulp:differing:compared. */
    m=snprintf(out+n,cap-(size_t)n," pg=%lld:%lld:%lld pg_hits=%lld pg_written=%lld",(long long)fr_max_ulp[FR_PG],(long long)fr_diff[FR_PG],
        (long long)fr_n[FR_PG],(long long)fr_hits[FR_HIT_PG],(long long)fr_pg_written);
    return m>0 && (size_t)m<cap-(size_t)n?n+m:0;
}
static void fr_hex(char *out,const unsigned char *bytes,size_t n) {
    static const char digits[]="0123456789abcdef";
    for(size_t i=0;i<n;i++) {out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}
    out[n*2]=0;
}
/* The index-th row, in record order, of a watched ship: inputs, native and our results (float bits), M per call
 * (a,b,c,flag,native,ours,pre; zeros when that M did not run). Asked in order from 0 (the probe's cursor); only
 * after admission, when the saved identities are sealed and the load's records can be keyed. */
static int fr_row(uint64_t index,char *out,size_t cap) {
    if(stats.issued<1 || index!=fr_emitted) return 0;
    const LONG64 count=InterlockedCompareExchange64(&fr_records,0,0);
    while(fr_scan<FR_RECORDS && (LONG64)fr_scan<count) {
        const fr_record_t *r=&fr_ring[fr_scan];
        if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)fr_scan+1) return 0; /* still being written */
        const uint64_t at=fr_scan++;++fr_scanned;
        uint64_t key=r->key;unsigned klass=WORLD_KEY_SAVED;int watched=-1;
        if(!key) key=world_identity_key(r->runtime,&klass);
        if(!key || klass!=WORLD_KEY_SAVED) continue;
        ++fr_keyed;
        for(unsigned i=0;i<fr_id_count;i++) if(fr_ids[i]==key) watched=(int)i;
        if(watched<0) continue;
        if(fr_ordinal_tick[watched]!=r->tick) {fr_ordinal_tick[watched]=r->tick;fr_ordinals[watched]=0;}
        const uint64_t ordinal=fr_ordinals[watched]++;
        if(r->kind==FR_KIND_PARAMS) {
            char pre0[2*FR_WINDOW+1],post0[2*FR_WINDOW+1],pre1[2*FR_WINDOW+1],post1[2*FR_WINDOW+1];
            fr_hex(pre0,r->pre,FR_WINDOW);fr_hex(post0,r->post,FR_WINDOW);fr_hex(pre1,r->pre+FR_WINDOW,FR_WINDOW);fr_hex(post1,r->post+FR_WINDOW,FR_WINDOW);
            int n=snprintf(out,cap,"index=%llu record=%llu kind=params tick=%llu key=%016llx ordinal=%llu thread=%s flag=%u v_path=%u mulps=%u d=%08x k=%08x "
                "v=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x cur=%08x,%08x,%08x,%08x ours=%08x,%08x,%08x,%08x pre0=%s post0=%s pre100=%s post100=%s",
                (unsigned long long)index,(unsigned long long)at,(unsigned long long)r->tick,(unsigned long long)key,(unsigned long long)ordinal,
                r->thread?"other":"ui",r->flag,(r->pseen&FR_PSEEN_V)!=0,(r->pseen&FR_PSEEN_MULPS)!=0,r->pd,r->pk,r->pv[0],r->pv[1],r->pv[2],r->pv[3],
                r->pv[4],r->pv[5],r->pv[6],r->pv[7],r->pcur[0],r->pcur[1],r->pcur[2],r->pcur[3],r->pours[0],r->pours[1],r->pours[2],r->pours[3],pre0,post0,pre1,post1);
            if(n<=0 || (size_t)n>=cap) return 0;
            /* The first call's last writer call (writer=calls:same slot): A, D, multipliers, table (scale,K,clamp),
             * p (present:bits), the five virtuals, native's +0x0..+0xc right after it and ours. */
            const fr_w_in *w=&r->w_in;
            const int m=snprintf(out+n,cap-(size_t)n," writer=%u:%u wa=%08x,%08x,%08x,%08x wd=%08x,%08x,%08x,%08x,%08x wm=%08x,%08x,%08x,%08x "
                "wt=%08x,%08x,%08x wp=%u:%08x wr=%08x,%08x,%08x,%08x,%08x wnative=%08x,%08x,%08x,%08x wours=%08x,%08x,%08x,%08x",
                r->wn,r->wsame,w->a[0],w->a[1],w->a[2],w->a[3],w->d[0],w->d[1],w->d[2],w->d[3],w->d[4],w->dm[0],w->dm[1],w->dm[2],w->dm[3],
                w->t_scale,w->k,w->t_clamp,w->has_p,w->p,w->r[0],w->r[1],w->r[2],w->r[3],w->r[4],
                r->w_native[0],r->w_native[1],r->w_native[2],r->w_native[3],r->w_ours[0],r->w_ours[1],r->w_ours[2],r->w_ours[3]);
            if(m<=0 || (size_t)m>=cap-(size_t)n) return 0;
            ++fr_emitted;return n+m;
        }
        char vec[33],buf[65];fr_hex(vec,r->vec,16);fr_hex(buf,r->buf,32);
        int n=snprintf(out,cap,"index=%llu record=%llu kind=compare tick=%llu key=%016llx ordinal=%llu thread=%s seen=%x ca=%08x cb=%08x g4=%08x g0=%08x w=%08x vec=%s buf=%s "
            "native=%08x,%08x,%08x ours=%08x,%08x,%08x v8=%08x v12=%08x m4=%08x,%08x,%08x,%u,%08x,%08x,%08x m0=%08x,%08x,%08x,%u,%08x,%08x,%08x",
            (unsigned long long)index,(unsigned long long)at,(unsigned long long)r->tick,(unsigned long long)key,(unsigned long long)ordinal,r->thread?"other":"ui",r->seen,
            r->ca,r->cb,r->g4,r->g0,r->w,vec,buf,r->native[0],r->native[1],r->native[2],r->ours[0],r->ours[1],r->ours[2],r->v8,r->v12,
            r->m_a[0],r->m_b[0],r->m_c[0],r->m_flag[0],r->m_native[0],r->m_ours[0],r->m_pre[0],
            r->m_a[1],r->m_b[1],r->m_c[1],r->m_flag[1],r->m_native[1],r->m_ours[1],r->m_pre[1]);
        if(n<=0 || (size_t)n>=cap) return 0;
        ++fr_emitted;return n;
    }
    return 0;
}
static int fr_lua_mode(const char *text,size_t length) {
    if(text && length==6 && !memcmp(text,"shadow",6)) return FR_SHADOW;
    if(text && length==6 && !memcmp(text,"writer",6)) return FR_WRITER;
    return text && length==8 && !memcmp(text,"redirect",8)?FR_REDIRECT:-1;
}
