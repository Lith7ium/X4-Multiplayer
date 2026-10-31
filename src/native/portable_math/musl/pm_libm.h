/* Internal helpers for the musl-derived sources (replaces musl's src/internal/libm.h).
 * The word-access macros, eval/barrier helpers and the __math_* error helpers are
 * derived from musl libc (MIT, see LICENSE-musl.txt); the pm_floor, pm_fabs,
 * pm_sqrt and pm_isnan helpers are original. Included only by ../portable_math_impl.h. */
#ifndef X4PM_MUSL_PM_LIBM_H
#define X4PM_MUSL_PM_LIBM_H

#include <stdint.h>

/* Round-to-nearest is the only mode the library runs in (the public wrappers set
 * MXCSR), but the musl code paths that keep results exact in other modes stay. */
#define WANT_ROUNDING 1
#define issignaling_inline(x) 0

#ifdef __GNUC__
#define predict_true(x) __builtin_expect(!!(x), 1)
#define predict_false(x) __builtin_expect(x, 0)
#else
#define predict_true(x) (x)
#define predict_false(x) (x)
#endif

static inline float eval_as_float(float x) { float y = x; return y; }
static inline double eval_as_double(double x) { double y = x; return y; }
static inline float fp_barrierf(float x) { volatile float y = x; return y; }
static inline double fp_barrier(double x) { volatile double y = x; return y; }
static inline void fp_force_evalf(float x) { volatile float y; y = x; (void)y; }
static inline void fp_force_eval(double x) { volatile double y; y = x; (void)y; }
#define FORCE_EVAL(x) do {                        \
	if (sizeof(x) == sizeof(float)) {         \
		fp_force_evalf((float)(x));       \
	} else {                                  \
		fp_force_eval((double)(x));       \
	}                                         \
} while(0)

#define asuint(f) ((union{float _f; uint32_t _i;}){f})._i
#define asfloat(i) ((union{uint32_t _i; float _f;}){i})._f
#define asuint64(f) ((union{double _f; uint64_t _i;}){f})._i
#define asdouble(i) ((union{uint64_t _i; double _f;}){i})._f

#define EXTRACT_WORDS(hi,lo,d)                    \
do {                                              \
  uint64_t __u = asuint64(d);                     \
  (hi) = __u >> 32;                               \
  (lo) = (uint32_t)__u;                           \
} while (0)

#define GET_HIGH_WORD(hi,d)                       \
do {                                              \
  (hi) = asuint64(d) >> 32;                       \
} while (0)

#define GET_LOW_WORD(lo,d)                        \
do {                                              \
  (lo) = (uint32_t)asuint64(d);                   \
} while (0)

#define INSERT_WORDS(d,hi,lo)                     \
do {                                              \
  (d) = asdouble(((uint64_t)(hi)<<32) | (uint32_t)(lo)); \
} while (0)

#define SET_HIGH_WORD(d,hi)                       \
  INSERT_WORDS(d, hi, (uint32_t)asuint64(d))

#define SET_LOW_WORD(d,lo)                        \
  INSERT_WORDS(d, asuint64(d)>>32, lo)

#define PM_INFINITY asdouble(0x7ff0000000000000ULL)

/* Original helpers: integer bit operations and the IEEE square root only. */
static inline double pm_fabs(double x) { return asdouble(asuint64(x) & 0x7fffffffffffffffULL); }
static inline int pm_isnan(double x) { return (asuint64(x) & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL; }
static inline double pm_sqrt(double x)
{
	/* SQRTSD is correctly rounded IEEE square root on every x86-64 CPU. */
	__asm__ ("sqrtsd %1, %0" : "=x"(x) : "x"(x));
	return x;
}
/* Largest integer value not greater than x, by masking fraction bits. */
static double pm_floor(double x)
{
	uint64_t u = asuint64(x), m;
	int e = (int)(u >> 52 & 0x7ff) - 0x3ff;
	if (e >= 52)
		return x;  /* integral, inf or nan */
	if (e < 0) {
		if ((u << 1) == 0)
			return x;  /* +-0 */
		return u >> 63 ? -1.0 : 0.0;
	}
	m = 0x000fffffffffffffULL >> e;
	if ((u & m) == 0)
		return x;
	if (u >> 63)
		u += m;
	u &= ~m;
	return asdouble(u);
}

/* musl src/math/scalbn.c */
static double pm_scalbn(double x, int n)
{
	union {double f; uint64_t i;} u;
	double y = x;

	if (n > 1023) {
		y *= 0x1p1023;
		n -= 1023;
		if (n > 1023) {
			y *= 0x1p1023;
			n -= 1023;
			if (n > 1023)
				n = 1023;
		}
	} else if (n < -1022) {
		/* make sure final n < -53 to avoid double
		   rounding in the subnormal range */
		y *= 0x1p-1022 * 0x1p53;
		n += 1022 - 53;
		if (n < -1022) {
			y *= 0x1p-1022 * 0x1p53;
			n += 1022 - 53;
			if (n < -1022)
				n = -1022;
		}
	}
	u.i = (uint64_t)(0x3ff+n)<<52;
	x = y * u.f;
	return x;
}

/* musl src/math/__math_{xflow,oflow,uflow,invalid,divzero}.c */
static double pm_math_xflow(uint32_t sign, double y)
{
	return eval_as_double(fp_barrier(sign ? -y : y) * y);
}
static double pm_math_oflow(uint32_t sign)
{
	return pm_math_xflow(sign, 0x1p769);
}
static double pm_math_uflow(uint32_t sign)
{
	return pm_math_xflow(sign, 0x1p-767);
}
static double pm_math_invalid(double x)
{
	return (x - x) / (x - x);
}
static double pm_math_divzero(uint32_t sign)
{
	return fp_barrier(sign ? -1.0 : 1.0) / 0.0;
}

#endif
