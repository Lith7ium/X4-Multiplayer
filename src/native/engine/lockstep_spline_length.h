/* BezierSpline::Set's segment length estimate in Option B's canonical form; original code, shared by the Windows
 * gate (native_gate_fp_redirect, lockstep_fp_redirect_win.h) and the Linux preload's own-bytes test.
 *
 * Set (Linux 0x1a45a80, Windows 0x14e4890) stores the control points P0..P3 and estimates the segment's length as
 *   len = (((|P1-P0| + |P2-P1|) + |P3-P2|) + |P3-P0|) * 0.5,   |v| = sqrt(v0*v0 + (v1*v1 + v2*v2)),
 * v = Pj - Pi lane by lane in single precision (lane 3 unused), each sqrt written as rcp(rsqrt(x)), which the
 * exact-reciprocal rewrite makes sqrt(x) on both platforms (Linux's copied chains included). Windows' composite-spline
 * builders 0x857960 and 0x31c640 inline Set and sum pairwise, ((|P2-P1| + |P1-P0|) + (|P3-P0| + |P3-P2|)) * 0.5: the
 * same terms, another rounding. This is the source's left-to-right order with exact sqrt.
 * The stored length is then 1.0 where len compares equal to 0: Linux (ucomiss, jp over a cmove) keeps a NaN,
 * Windows' inlined tail (ucomiss, jne over a store of 1.0) also turns a NaN into 1.0. */
#ifndef X4_LOCKSTEP_SPLINE_LENGTH_H
#define X4_LOCKSTEP_SPLINE_LENGTH_H
#include <float.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#if defined(__FAST_MATH__) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD!=0
#error "lockstep_spline_length.h needs strict IEEE single precision (no fast-math, FLT_EVAL_METHOD 0)"
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#define SL_ONE 0x3f800000u
static inline float sl_sqrt(float x) {return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));} /* SQRTSS: correctly rounded */
/* |b - a| as Set computes it: lane products, then lane 0 + (lane 1 + lane 2). */
static inline float sl_distance(const float *a,const float *b) {
    const float x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2];
    const float xx=x*x,yy=y*y,zz=z*z;
    return sl_sqrt(xx+(yy+zz));
}
/* The canonical length (before the zero rule) from the four control points, 16 bytes each (lanes 0-2 used). */
static inline uint32_t sl_length(const unsigned char points[64]) {
    float p[4][4];memcpy(p,points,64);
    const float d10=sl_distance(p[0],p[1]),d21=sl_distance(p[1],p[2]),d32=sl_distance(p[2],p[3]),d30=sl_distance(p[0],p[3]);
    const float len=(((d10+d21)+d32)+d30)*0.5f;
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* Windows' inlined association, for the tests: ((d21 + d10) + (d30 + d32)) * 0.5. */
static inline uint32_t sl_length_pairwise(const unsigned char points[64]) {
    float p[4][4];memcpy(p,points,64);
    const float d10=sl_distance(p[0],p[1]),d21=sl_distance(p[1],p[2]),d32=sl_distance(p[2],p[3]),d30=sl_distance(p[0],p[3]);
    const float len=((d21+d10)+(d30+d32))*0.5f;
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* FR13: the same builders' Bezier handle length where the caller left it 0, a third of the segment's chord d. Linux
 * (0x1151760 and every Set caller forming handles) divides, d / 3.0f; Windows' builders (0x857d19, 0x857df0,
 * 0x31c9a9, 0x31ca80) multiply by the rounded reciprocal, d * 0.333333343f (k, from the register): another rounding,
 * which then moves P1 = P0 + s*T0 (or P2 = P3 - s*T1) by one ulp of the absolute coordinate now and then. */
static inline uint32_t sl_handle(uint32_t d_bits) {
    float d,s;memcpy(&d,&d_bits,4);s=d/3.0f;
    uint32_t bits;memcpy(&bits,&s,4);return bits;
}
static inline uint32_t sl_handle_windows(uint32_t d_bits,uint32_t k_bits) {
    float d,k,s;memcpy(&d,&d_bits,4);memcpy(&k,&k_bits,4);s=d*k;
    uint32_t bits;memcpy(&bits,&s,4);return bits;
}
static inline int sl_is_zero(uint32_t bits) {return (bits&0x7fffffffu)==0;}
static inline int sl_is_nan(uint32_t bits) {return (bits&0x7fffffffu)>0x7f800000u;}
static inline uint32_t sl_stored_linux(uint32_t len) {return sl_is_zero(len)?SL_ONE:len;}
static inline uint32_t sl_stored_windows(uint32_t len) {return sl_is_zero(len) || sl_is_nan(len)?SL_ONE:len;}
#endif
