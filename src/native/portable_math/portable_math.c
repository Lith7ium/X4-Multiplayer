/* X4 Multiplayer portable math library; see portable_math.h for the contract.
 * Build as its own translation unit with -ffp-contract=off and without fast-math. */
#include <float.h>
#include <stdint.h>

#if !defined(__x86_64__) && !defined(_M_X64)
#error "portable math: x86-64 (SSE2 scalar double) only"
#endif
#if defined(__FAST_MATH__)
#error "portable math must not be compiled with -ffast-math"
#endif
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "portable math needs FLT_EVAL_METHOD 0 (SSE scalar arithmetic, no excess precision)"
#endif
#if defined(__FMA__) && !defined(X4PM_ALLOW_FMA_TARGET)
#error "FMA target: build with -ffp-contract=off and define X4PM_ALLOW_FMA_TARGET to confirm"
#endif
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

#include "portable_math.h"
#include "musl/pm_libm.h"
#include "musl/pm_kernels.h"
#include "musl/pm_trig.h"
#include "musl/pm_invtrig.h"
#include "musl/pm_exp.h"
#include "musl/pm_log.h"
#include "musl/pm_pow.h"
#include "musl/pm_misc.h"

/* ---- MXCSR: evaluate in the default environment whatever the caller set ---- */
#define PM_CSR_DEFAULT 0x1f80u  /* round to nearest, FTZ/DAZ off, all exceptions masked */
#define PM_CSR_CONTROL 0xffc0u  /* everything but the six sticky exception flags */
static inline unsigned pm_getcsr(void)
{
	unsigned c;
	__asm__ __volatile__("stmxcsr %0" : "=m"(c) : : "memory");
	return c;
}
static inline void pm_setcsr(unsigned c)
{
	__asm__ __volatile__("ldmxcsr %0" : : "m"(c) : "memory");
}
/* Orders a value after the preceding MXCSR write and before the following one. */
#define PM_FENCE(v) __asm__ __volatile__("" : "+x"(v) : : "memory")
#define PM_EVAL1(r, a, expr) do {                                          \
	unsigned pm_csr_ = pm_getcsr();                                     \
	if (predict_false((pm_csr_ & PM_CSR_CONTROL) != PM_CSR_DEFAULT)) {  \
		pm_setcsr(PM_CSR_DEFAULT | (pm_csr_ & 0x3fu));              \
		PM_FENCE(a);                                                \
		(r) = (expr);                                               \
		PM_FENCE(r);                                                \
		pm_setcsr(pm_csr_);                                         \
	} else                                                              \
		(r) = (expr);                                               \
} while (0)
#define PM_EVAL2(r, a, b, expr) do {                                       \
	unsigned pm_csr_ = pm_getcsr();                                     \
	if (predict_false((pm_csr_ & PM_CSR_CONTROL) != PM_CSR_DEFAULT)) {  \
		pm_setcsr(PM_CSR_DEFAULT | (pm_csr_ & 0x3fu));              \
		PM_FENCE(a);                                                \
		PM_FENCE(b);                                                \
		(r) = (expr);                                               \
		PM_FENCE(r);                                                \
		pm_setcsr(pm_csr_);                                         \
	} else                                                              \
		(r) = (expr);                                               \
} while (0)

/* ---- NaN results: the first NaN argument quieted, else the default NaN. Applied inside
 * the evaluation window: a compiler may turn these bit tests into UCOMISD, which must not
 * run (raise flags or trap) under the caller's MXCSR. ---- */
#define PM_QNAN_BIT 0x0008000000000000ULL
#define PM_DEFAULT_NAN 0xfff8000000000000ULL
#define PM_QNANF_BIT 0x00400000u
#define PM_DEFAULT_NANF 0xffc00000u
static inline int pm_isnanf(float x) { return (asuint(x) & 0x7fffffffu) > 0x7f800000u; }
static inline double pm_nan1(double r, double x)
{
	if (predict_false(pm_isnan(r)))
		return asdouble(pm_isnan(x) ? asuint64(x) | PM_QNAN_BIT : PM_DEFAULT_NAN);
	return r;
}
static inline double pm_nan2(double r, double x, double y)
{
	if (predict_false(pm_isnan(r)))
		return asdouble(pm_isnan(x) ? asuint64(x) | PM_QNAN_BIT :
		                pm_isnan(y) ? asuint64(y) | PM_QNAN_BIT : PM_DEFAULT_NAN);
	return r;
}
static inline float pm_nan1f(float r, float x)
{
	if (predict_false(pm_isnanf(r)))
		return asfloat(pm_isnanf(x) ? asuint(x) | PM_QNANF_BIT : PM_DEFAULT_NANF);
	return r;
}
static inline float pm_nan2f(float r, float x, float y)
{
	if (predict_false(pm_isnanf(r)))
		return asfloat(pm_isnanf(x) ? asuint(x) | PM_QNANF_BIT :
		               pm_isnanf(y) ? asuint(y) | PM_QNANF_BIT : PM_DEFAULT_NANF);
	return r;
}

/* pow with the exact forms compilers fold pow(x, 2), pow(x, 1) and pow(x, -1) into. */
static double pm_pow_folded(double x, double y)
{
	uint64_t iy = asuint64(y);
	if (iy == 0x4000000000000000ULL)
		return x * x;
	if (iy == 0x3ff0000000000000ULL)
		return x;
	if (iy == 0xbff0000000000000ULL)
		return 1.0 / x;
	return pm_pow(x, y);
}
static float pm_powf_folded(float x, float y)
{
	uint32_t iy = asuint(y);
	if (iy == 0x40000000u)
		return x * x;
	if (iy == 0x3f800000u)
		return x;
	if (iy == 0xbf800000u)
		return 1.0f / x;
	return (float)pm_pow((double)x, (double)y);
}

/* ---- double entry points ---- */
#define PM_DOUBLE1(name, core) \
X4PM_API double x4pm_##name(double x) \
{ \
	double r; \
	PM_EVAL1(r, x, pm_nan1(core(x), x)); \
	return r; \
}
#define PM_DOUBLE2(name, core) \
X4PM_API double x4pm_##name(double x, double y) \
{ \
	double r; \
	PM_EVAL2(r, x, y, pm_nan2(core(x, y), x, y)); \
	return r; \
}
PM_DOUBLE1(sin, pm_sin)
PM_DOUBLE1(cos, pm_cos)
PM_DOUBLE1(tan, pm_tan)
PM_DOUBLE1(asin, pm_asin)
PM_DOUBLE1(acos, pm_acos)
PM_DOUBLE1(atan, pm_atan)
PM_DOUBLE2(atan2, pm_atan2)
PM_DOUBLE1(exp, pm_exp)
PM_DOUBLE1(exp2, pm_exp2)
PM_DOUBLE1(expm1, pm_expm1)
PM_DOUBLE1(log, pm_log)
PM_DOUBLE1(log2, pm_log2)
PM_DOUBLE1(log10, pm_log10)
PM_DOUBLE1(log1p, pm_log1p)
PM_DOUBLE2(pow, pm_pow_folded)
PM_DOUBLE2(fmod, pm_fmod)
PM_DOUBLE2(hypot, pm_hypot)
PM_DOUBLE1(cbrt, pm_cbrt)
PM_DOUBLE1(sinh, pm_sinh)
PM_DOUBLE1(cosh, pm_cosh)
PM_DOUBLE1(tanh, pm_tanh)

X4PM_API void x4pm_sincos(double x, double *sin_out, double *cos_out)
{
	double s, c;
	unsigned csr = pm_getcsr();
	if (predict_false((csr & PM_CSR_CONTROL) != PM_CSR_DEFAULT)) {
		pm_setcsr(PM_CSR_DEFAULT | (csr & 0x3fu));
		PM_FENCE(x);
		pm_sincos(x, &s, &c);
		s = pm_nan1(s, x);
		c = pm_nan1(c, x);
		PM_FENCE(s);
		PM_FENCE(c);
		pm_setcsr(csr);
	} else {
		pm_sincos(x, &s, &c);
		s = pm_nan1(s, x);
		c = pm_nan1(c, x);
	}
	*sin_out = s;
	*cos_out = c;
}

/* ---- float entry points: the double function, rounded once ---- */
#define PM_FLOAT1(name, core) \
X4PM_API float x4pm_##name##f(float x) \
{ \
	float r; \
	PM_EVAL1(r, x, pm_nan1f((float)core((double)x), x)); \
	return r; \
}
#define PM_FLOAT2(name, core) \
X4PM_API float x4pm_##name##f(float x, float y) \
{ \
	float r; \
	PM_EVAL2(r, x, y, pm_nan2f((float)core((double)x, (double)y), x, y)); \
	return r; \
}
PM_FLOAT1(sin, pm_sin)
PM_FLOAT1(cos, pm_cos)
PM_FLOAT1(tan, pm_tan)
PM_FLOAT1(asin, pm_asin)
PM_FLOAT1(acos, pm_acos)
PM_FLOAT1(atan, pm_atan)
PM_FLOAT2(atan2, pm_atan2)
PM_FLOAT1(exp, pm_exp)
PM_FLOAT1(exp2, pm_exp2)
PM_FLOAT1(expm1, pm_expm1)
PM_FLOAT1(log, pm_log)
PM_FLOAT1(log2, pm_log2)
PM_FLOAT1(log10, pm_log10)
PM_FLOAT1(log1p, pm_log1p)
PM_FLOAT2(fmod, pm_fmod)
PM_FLOAT2(hypot, pm_hypot)
PM_FLOAT1(cbrt, pm_cbrt)
PM_FLOAT1(sinh, pm_sinh)
PM_FLOAT1(cosh, pm_cosh)
PM_FLOAT1(tanh, pm_tanh)

X4PM_API float x4pm_powf(float x, float y)
{
	float r;
	PM_EVAL2(r, x, y, pm_nan2f(pm_powf_folded(x, y), x, y));
	return r;
}

X4PM_API void x4pm_sincosf(float x, float *sin_out, float *cos_out)
{
	double s, c;
	float fs, fc;
	unsigned csr = pm_getcsr();
	if (predict_false((csr & PM_CSR_CONTROL) != PM_CSR_DEFAULT)) {
		pm_setcsr(PM_CSR_DEFAULT | (csr & 0x3fu));
		PM_FENCE(x);
		pm_sincos((double)x, &s, &c);
		fs = pm_nan1f((float)s, x);
		fc = pm_nan1f((float)c, x);
		PM_FENCE(fs);
		PM_FENCE(fc);
		pm_setcsr(csr);
	} else {
		pm_sincos((double)x, &s, &c);
		fs = pm_nan1f((float)s, x);
		fc = pm_nan1f((float)c, x);
	}
	*sin_out = fs;
	*cos_out = fc;
}

/* ---- known-answer digest ---- */
static void pm_kat_mix(uint64_t *hash, uint64_t bits, unsigned width)
{
	for (unsigned i = 0; i < width; i += 8) {
		*hash ^= (bits >> i) & 0xff;
		*hash *= 0x100000001b3ULL;
	}
}
static void pm_kat_double(uint64_t *hash, double x, double y)
{
	double s, c;
	double (*const unary[])(double) = {x4pm_sin, x4pm_cos, x4pm_tan, x4pm_asin, x4pm_acos, x4pm_atan,
		x4pm_exp, x4pm_exp2, x4pm_expm1, x4pm_log, x4pm_log2, x4pm_log10, x4pm_log1p, x4pm_cbrt,
		x4pm_sinh, x4pm_cosh, x4pm_tanh};
	double (*const binary[])(double, double) = {x4pm_atan2, x4pm_pow, x4pm_fmod, x4pm_hypot};
	for (unsigned i = 0; i < sizeof(unary) / sizeof(*unary); i++)
		pm_kat_mix(hash, asuint64(unary[i](x)), 64);
	for (unsigned i = 0; i < sizeof(binary) / sizeof(*binary); i++)
		pm_kat_mix(hash, asuint64(binary[i](x, y)), 64);
	x4pm_sincos(x, &s, &c);
	pm_kat_mix(hash, asuint64(s), 64);
	pm_kat_mix(hash, asuint64(c), 64);
}
static void pm_kat_float(uint64_t *hash, float x, float y)
{
	float s, c;
	float (*const unary[])(float) = {x4pm_sinf, x4pm_cosf, x4pm_tanf, x4pm_asinf, x4pm_acosf, x4pm_atanf,
		x4pm_expf, x4pm_exp2f, x4pm_expm1f, x4pm_logf, x4pm_log2f, x4pm_log10f, x4pm_log1pf, x4pm_cbrtf,
		x4pm_sinhf, x4pm_coshf, x4pm_tanhf};
	float (*const binary[])(float, float) = {x4pm_atan2f, x4pm_powf, x4pm_fmodf, x4pm_hypotf};
	for (unsigned i = 0; i < sizeof(unary) / sizeof(*unary); i++)
		pm_kat_mix(hash, asuint(unary[i](x)), 32);
	for (unsigned i = 0; i < sizeof(binary) / sizeof(*binary); i++)
		pm_kat_mix(hash, asuint(binary[i](x, y)), 32);
	x4pm_sincosf(x, &s, &c);
	pm_kat_mix(hash, asuint(s), 32);
	pm_kat_mix(hash, asuint(c), 32);
}
X4PM_API uint64_t x4pm_kat(void)
{
	static const uint64_t edges[] = {
		0x0000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000001ULL, 0x000fffffffffffffULL,
		0x0010000000000000ULL, 0x3ff0000000000000ULL, 0xbff0000000000000ULL, 0x4000000000000000ULL,
		0x3fe0000000000000ULL, 0x3ff921fb54442d18ULL, 0x400921fb54442d18ULL, 0x4012d97c7f3321d2ULL,
		0x7fefffffffffffffULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000000ULL,
		0x7ff4000000000001ULL, 0x7e37e43c8800759cULL, 0x4086232bdd7abcd2ULL, 0xc0874910d52d3051ULL,
		0x3e45798ee2308c3aULL, 0x41d0000000000000ULL, 0x4330000000000001ULL, 0xc330000000000000ULL};
	uint64_t state = 0x58344d504b415431ULL;  /* "X4MPKAT1" */
	uint64_t hash = 0xcbf29ce484222325ULL;
	for (unsigned i = 0; i < sizeof(edges) / sizeof(*edges); i++)
		for (unsigned j = 0; j < sizeof(edges) / sizeof(*edges); j += 5) {
			pm_kat_double(&hash, asdouble(edges[i]), asdouble(edges[j]));
			pm_kat_float(&hash, (float)asdouble(edges[i]), (float)asdouble(edges[j]));
		}
	for (unsigned i = 0; i < 2048; i++) {
		uint64_t a, b;
		state += 0x9e3779b97f4a7c15ULL; a = state;
		a = (a ^ (a >> 30)) * 0xbf58476d1ce4e5b9ULL; a = (a ^ (a >> 27)) * 0x94d049bb133111ebULL; a ^= a >> 31;
		state += 0x9e3779b97f4a7c15ULL; b = state;
		b = (b ^ (b >> 30)) * 0xbf58476d1ce4e5b9ULL; b = (b ^ (b >> 27)) * 0x94d049bb133111ebULL; b ^= b >> 31;
		if (i & 1) {
			/* moderate magnitudes: |x| < 2^(e-1023) with e in [1013, 1033) */
			a = (a & 0x800fffffffffffffULL) | ((uint64_t)(1013 + (a >> 52 & 15) + (a >> 56 & 3)) << 52);
			b = (b & 0x800fffffffffffffULL) | ((uint64_t)(1013 + (b >> 52 & 15) + (b >> 56 & 3)) << 52);
		}
		pm_kat_double(&hash, asdouble(a), asdouble(b));
		pm_kat_float(&hash, asfloat((uint32_t)(a >> 32)), asfloat((uint32_t)b));
	}
	return hash;
}
