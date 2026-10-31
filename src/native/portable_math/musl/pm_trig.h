/* sin, cos, tan and sincos of double: reduction by pm_rem_pio2 and the kernels.
 * Same structure as musl src/math/{sin,cos,tan,sincos}.c, which derive from FreeBSD
 * /usr/src/lib/msun/src/s_{sin,cos,tan}.c; see LICENSE-musl.txt:
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 * X4 Multiplayer change: pm_sincos takes exactly the branches of pm_sin and pm_cos,
 * so sincos(x) is bit-identical to (sin(x), cos(x)). Callers on one platform use
 * sincos where the other platform's compiler emitted separate sin and cos calls.
 * Included only by ../portable_math.c. */
#ifndef X4PM_MUSL_PM_TRIG_H
#define X4PM_MUSL_PM_TRIG_H

static double pm_sin(double x)
{
	double y[2];
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;
	if (ix <= 0x3fe921fb) {  /* |x| ~< pi/4 */
		if (ix < 0x3e500000)  /* |x| < 2**-26: sin(x) rounds to x */
			return x;
		return pm_k_sin(x, 0.0, 0);
	}
	if (ix >= 0x7ff00000)  /* sin(Inf or NaN) is NaN */
		return x - x;
	n = (unsigned)pm_rem_pio2(x, y);
	switch (n & 3) {
	case 0: return  pm_k_sin(y[0], y[1], 1);
	case 1: return  pm_k_cos(y[0], y[1]);
	case 2: return -pm_k_sin(y[0], y[1], 1);
	default: return -pm_k_cos(y[0], y[1]);
	}
}

static double pm_cos(double x)
{
	double y[2];
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;
	if (ix <= 0x3fe921fb) {  /* |x| ~< pi/4 */
		if (ix < 0x3e46a09e)  /* |x| < 2**-27 * sqrt(2): cos(x) rounds to 1 */
			return 1.0;
		return pm_k_cos(x, 0.0);
	}
	if (ix >= 0x7ff00000)  /* cos(Inf or NaN) is NaN */
		return x - x;
	n = (unsigned)pm_rem_pio2(x, y);
	switch (n & 3) {
	case 0: return  pm_k_cos(y[0], y[1]);
	case 1: return -pm_k_sin(y[0], y[1], 1);
	case 2: return -pm_k_cos(y[0], y[1]);
	default: return  pm_k_sin(y[0], y[1], 1);
	}
}

static void pm_sincos(double x, double *sn, double *cs)
{
	double y[2], s, c;
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;
	if (ix <= 0x3fe921fb) {  /* |x| ~< pi/4: the two small-argument cuts of pm_sin and pm_cos */
		*sn = ix < 0x3e500000 ? x : pm_k_sin(x, 0.0, 0);
		*cs = ix < 0x3e46a09e ? 1.0 : pm_k_cos(x, 0.0);
		return;
	}
	if (ix >= 0x7ff00000) {
		*sn = *cs = x - x;
		return;
	}
	n = (unsigned)pm_rem_pio2(x, y);
	s = pm_k_sin(y[0], y[1], 1);
	c = pm_k_cos(y[0], y[1]);
	switch (n & 3) {
	case 0: *sn = s;  *cs = c;  break;
	case 1: *sn = c;  *cs = -s; break;
	case 2: *sn = -s; *cs = -c; break;
	default: *sn = -c; *cs = s; break;
	}
}

static double pm_tan(double x)
{
	double y[2];
	uint32_t ix;
	unsigned n;

	GET_HIGH_WORD(ix, x);
	ix &= 0x7fffffff;
	if (ix <= 0x3fe921fb) {  /* |x| ~< pi/4 */
		if (ix < 0x3e400000)  /* |x| < 2**-27: tan(x) rounds to x */
			return x;
		return pm_k_tan(x, 0.0, 0);
	}
	if (ix >= 0x7ff00000)  /* tan(Inf or NaN) is NaN */
		return x - x;
	n = (unsigned)pm_rem_pio2(x, y);
	return pm_k_tan(y[0], y[1], (int)(n & 1));
}

#endif
