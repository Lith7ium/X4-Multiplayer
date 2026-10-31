/* X4 Multiplayer portable math: one libm whose results are the same bits on every
 * x86-64 compiler and C library, so a Windows (MSVC CRT) and a Linux (glibc libm)
 * X4 compute the same transcendental results once both are redirected here.
 *
 * Guarantees, for every input including NaN, infinities, subnormals and huge
 * arguments (full Payne-Hanek reduction for the trigonometric functions):
 * - Only IEEE-754 basic operations (+ - * /, sqrt via SQRTSD, int<->double
 *   conversions) and integer bit operations. No FMA, no x87, no long double, no
 *   lookup of the host libm, no CPU-dependent dispatch. portable_math.c refuses to
 *   compile under -ffast-math, with excess precision, or for an FMA target unless
 *   the builder asserts -ffp-contract=off with -DX4PM_ALLOW_FMA_TARGET.
 * - Independent of the caller's MXCSR: when it differs from the default, each
 *   entry point evaluates under round-to-nearest, flush-to-zero and
 *   denormals-are-zero cleared and all exceptions masked (MXCSR 0x1f80), then
 *   puts the caller's MXCSR back (flags raised inside are then discarded).
 * - A NaN result is canonical: the first NaN argument, quieted, else the x86
 *   default NaN (0xfff8000000000000 / 0xffc00000). sincos(x) and sincosf(x) are
 *   bit-identical to (sin(x), cos(x)) and (sinf(x), cosf(x)).
 * - pow(x, y) and powf(x, y) return x*x, x and 1/x exactly for y = 2, 1 and -1,
 *   the forms GCC and MSVC fold such calls into.
 * - Accuracy: double functions are the musl/fdlibm/Arm algorithms (below 1 ulp,
 *   exp/log/pow about 0.52 ulp); float functions evaluate the double function and
 *   round once, so they are within 0.5 ulp plus a double ulp. Measured maxima are
 *   in reports/PORTABLE-MATH-20261029.txt.
 *
 * Sources are derived from musl libc (MIT; fdlibm parts under the Sun notice, exp,
 * log and pow under the Arm MIT notice); see musl/LICENSE-musl.txt and NOTICE.txt. */
#ifndef X4_PORTABLE_MATH_H
#define X4_PORTABLE_MATH_H
#include <stdint.h>

#define X4PM_VERSION 1u
/* x4pm_kat() over the fixed known-answer inputs of this version; consumers refuse
 * to install when a build returns anything else (miscompiled or changed code). */
#define X4PM_KAT_EXPECTED UINT64_C(0x06e026f4169223c1)

#ifndef X4PM_API
#define X4PM_API
#endif

X4PM_API double x4pm_sin(double x);
X4PM_API double x4pm_cos(double x);
X4PM_API double x4pm_tan(double x);
X4PM_API void x4pm_sincos(double x, double *sin_out, double *cos_out);
X4PM_API double x4pm_asin(double x);
X4PM_API double x4pm_acos(double x);
X4PM_API double x4pm_atan(double x);
X4PM_API double x4pm_atan2(double y, double x);
X4PM_API double x4pm_exp(double x);
X4PM_API double x4pm_exp2(double x);
X4PM_API double x4pm_expm1(double x);
X4PM_API double x4pm_log(double x);
X4PM_API double x4pm_log2(double x);
X4PM_API double x4pm_log10(double x);
X4PM_API double x4pm_log1p(double x);
X4PM_API double x4pm_pow(double x, double y);
X4PM_API double x4pm_fmod(double x, double y);
X4PM_API double x4pm_hypot(double x, double y);
X4PM_API double x4pm_cbrt(double x);
X4PM_API double x4pm_sinh(double x);
X4PM_API double x4pm_cosh(double x);
X4PM_API double x4pm_tanh(double x);

X4PM_API float x4pm_sinf(float x);
X4PM_API float x4pm_cosf(float x);
X4PM_API float x4pm_tanf(float x);
X4PM_API void x4pm_sincosf(float x, float *sin_out, float *cos_out);
X4PM_API float x4pm_asinf(float x);
X4PM_API float x4pm_acosf(float x);
X4PM_API float x4pm_atanf(float x);
X4PM_API float x4pm_atan2f(float y, float x);
X4PM_API float x4pm_expf(float x);
X4PM_API float x4pm_exp2f(float x);
X4PM_API float x4pm_expm1f(float x);
X4PM_API float x4pm_logf(float x);
X4PM_API float x4pm_log2f(float x);
X4PM_API float x4pm_log10f(float x);
X4PM_API float x4pm_log1pf(float x);
X4PM_API float x4pm_powf(float x, float y);
X4PM_API float x4pm_fmodf(float x, float y);
X4PM_API float x4pm_hypotf(float x, float y);
X4PM_API float x4pm_cbrtf(float x);
X4PM_API float x4pm_sinhf(float x);
X4PM_API float x4pm_coshf(float x);
X4PM_API float x4pm_tanhf(float x);

/* FNV-1a 64 over the result bits of every function above for a fixed input set
 * (edge cases plus a deterministic pseudo-random sweep). */
X4PM_API uint64_t x4pm_kat(void);

#endif
