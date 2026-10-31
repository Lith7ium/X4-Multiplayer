/* fmod, hypot, cbrt, expm1, log1p and the hyperbolic functions.
 * Derived from musl libc src/math (the copy bundled with Zig 0.15.2); see
 * LICENSE-musl.txt. Changes for X4 Multiplayer: internal static linkage, pm_
 * prefixes, the __FP_FAST_FMA and TOINT_INTRINSICS branches resolved to the
 * plain IEEE path, double_t -> double. Included only by ../portable_math_impl.h. */
#ifndef X4PM_MUSL_PM_MISC_H
#define X4PM_MUSL_PM_MISC_H

/* ---- derived from musl src/math/fmod.c ---- */

static double pm_fmod(double x, double y)
{
	union {double f; uint64_t i;} ux = {x}, uy = {y};
	int ex = ux.i>>52 & 0x7ff;
	int ey = uy.i>>52 & 0x7ff;
	int sx = ux.i>>63;
	uint64_t i;

	/* in the followings uxi should be ux.i, but then gcc wrongly adds */
	/* float load/store to inner loops ruining performance and code size */
	uint64_t uxi = ux.i;

	if (uy.i<<1 == 0 || pm_isnan(y) || ex == 0x7ff)
		return (x*y)/(x*y);
	if (uxi<<1 <= uy.i<<1) {
		if (uxi<<1 == uy.i<<1)
			return 0*x;
		return x;
	}

	/* normalize x and y */
	if (!ex) {
		for (i = uxi<<12; i>>63 == 0; ex--, i <<= 1);
		uxi <<= -ex + 1;
	} else {
		uxi &= -1ULL >> 12;
		uxi |= 1ULL << 52;
	}
	if (!ey) {
		for (i = uy.i<<12; i>>63 == 0; ey--, i <<= 1);
		uy.i <<= -ey + 1;
	} else {
		uy.i &= -1ULL >> 12;
		uy.i |= 1ULL << 52;
	}

	/* x mod y */
	for (; ex > ey; ex--) {
		i = uxi - uy.i;
		if (i >> 63 == 0) {
			if (i == 0)
				return 0*x;
			uxi = i;
		}
		uxi <<= 1;
	}
	i = uxi - uy.i;
	if (i >> 63 == 0) {
		if (i == 0)
			return 0*x;
		uxi = i;
	}
	for (; uxi>>52 == 0; uxi <<= 1, ex--);

	/* scale result */
	if (ex > 0) {
		uxi -= 1ULL << 52;
		uxi |= (uint64_t)ex << 52;
	} else {
		uxi >>= -ex + 1;
	}
	uxi |= (uint64_t)sx << 63;
	ux.i = uxi;
	return ux.f;
}

/* ---- derived from musl src/math/hypot.c ---- */

#define pm_hypot_SPLIT (0x1p27 + 1) /* FLT_EVAL_METHOD 0 */

static void pm_hypot_sq(double *hi, double *lo, double x)
{
	double xh, xl, xc;

	xc = (double)x*pm_hypot_SPLIT;
	xh = x - xc + xc;
	xl = x - xh;
	*hi = (double)x*x;
	*lo = xh*xh - *hi + 2*xh*xl + xl*xl;
}

static double pm_hypot(double x, double y)
{
	union {double f; uint64_t i;} ux = {x}, uy = {y}, ut;
	int ex, ey;
	double hx, lx, hy, ly, z;

	/* arrange |x| >= |y| */
	ux.i &= -1ULL>>1;
	uy.i &= -1ULL>>1;
	if (ux.i < uy.i) {
		ut = ux;
		ux = uy;
		uy = ut;
	}

	/* special cases */
	ex = ux.i>>52;
	ey = uy.i>>52;
	x = ux.f;
	y = uy.f;
	/* note: hypot(inf,nan) == inf */
	if (ey == 0x7ff)
		return y;
	if (ex == 0x7ff || uy.i == 0)
		return x;
	/* note: hypot(x,y) ~= x + y*y/x/2 with inexact for small y/x */
	/* 64 difference is enough for ld80 double_t */
	if (ex - ey > 64)
		return x + y;

	/* precise sqrt argument in nearest rounding mode without overflow */
	/* xh*xh must not overflow and xl*xl must not underflow in sq */
	z = 1;
	if (ex > 0x3ff+510) {
		z = 0x1p700;
		x *= 0x1p-700;
		y *= 0x1p-700;
	} else if (ey < 0x3ff-450) {
		z = 0x1p-700;
		x *= 0x1p700;
		y *= 0x1p700;
	}
	pm_hypot_sq(&hx, &lx, x);
	pm_hypot_sq(&hy, &ly, y);
	return z*pm_sqrt(ly+lx+hy+hx);
}
#undef pm_hypot_SPLIT

/* ---- derived from musl src/math/cbrt.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/s_cbrt.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * Optimized by Bruce D. Evans.
 */
/* cbrt(x)
 * Return cube root of x
 */


static const uint32_t
pm_cbrt_B1 = 715094163, /* B1 = (1023-1023/3-0.03306235651)*2**20 */
pm_cbrt_B2 = 696219795; /* B2 = (1023-1023/3-54/3-0.03306235651)*2**20 */

/* |1/cbrt(x) - p(x)| < 2**-23.5 (~[-7.93e-8, 7.929e-8]). */
static const double
pm_cbrt_P0 =  1.87595182427177009643,  /* 0x3ffe03e6, 0x0f61e692 */
pm_cbrt_P1 = -1.88497979543377169875,  /* 0xbffe28e0, 0x92f02420 */
pm_cbrt_P2 =  1.621429720105354466140, /* 0x3ff9f160, 0x4a49d6c2 */
pm_cbrt_P3 = -0.758397934778766047437, /* 0xbfe844cb, 0xbee751d9 */
pm_cbrt_P4 =  0.145996192886612446982; /* 0x3fc2b000, 0xd4e4edd7 */

static double pm_cbrt(double x)
{
	union {double f; uint64_t i;} u = {x};
	double r,s,t,w;
	uint32_t hx = u.i>>32 & 0x7fffffff;

	if (hx >= 0x7ff00000)  /* cbrt(NaN,INF) is itself */
		return x+x;

	/*
	 * Rough cbrt to 5 bits:
	 *    cbrt(2**e*(1+m) ~= 2**(e/3)*(1+(e%3+m)/3)
	 * where e is integral and >= 0, m is real and in [0, 1), and "/" and
	 * "%" are integer division and modulus with rounding towards minus
	 * infinity.  The RHS is always >= the LHS and has a maximum relative
	 * error of about 1 in 16.  Adding a bias of -0.03306235651 to the
	 * (e%3+m)/3 term reduces the error to about 1 in 32. With the IEEE
	 * floating point representation, for finite positive normal values,
	 * ordinary integer divison of the value in bits magically gives
	 * almost exactly the RHS of the above provided we first subtract the
	 * exponent bias (1023 for doubles) and later add it back.  We do the
	 * subtraction virtually to keep e >= 0 so that ordinary integer
	 * division rounds towards minus infinity; this is also efficient.
	 */
	if (hx < 0x00100000) { /* zero or subnormal? */
		u.f = x*0x1p54;
		hx = u.i>>32 & 0x7fffffff;
		if (hx == 0)
			return x;  /* cbrt(0) is itself */
		hx = hx/3 + pm_cbrt_B2;
	} else
		hx = hx/3 + pm_cbrt_B1;
	u.i &= 1ULL<<63;
	u.i |= (uint64_t)hx << 32;
	t = u.f;

	/*
	 * New cbrt to 23 bits:
	 *    cbrt(x) = t*cbrt(x/t**3) ~= t*P(t**3/x)
	 * where P(r) is a polynomial of degree 4 that approximates 1/cbrt(r)
	 * to within 2**-23.5 when |r - 1| < 1/10.  The rough approximation
	 * has produced t such than |t/cbrt(x) - 1| ~< 1/32, and cubing this
	 * gives us bounds for r = t**3/x.
	 *
	 * Try to optimize for parallel evaluation as in __tanf.c.
	 */
	r = (t*t)*(t/x);
	t = t*((pm_cbrt_P0+r*(pm_cbrt_P1+r*pm_cbrt_P2))+((r*r)*r)*(pm_cbrt_P3+r*pm_cbrt_P4));

	/*
	 * Round t away from zero to 23 bits (sloppily except for ensuring that
	 * the result is larger in magnitude than cbrt(x) but not much more than
	 * 2 23-bit ulps larger).  With rounding towards zero, the error bound
	 * would be ~5/6 instead of ~4/6.  With a maximum error of 2 23-bit ulps
	 * in the rounded t, the infinite-precision error in the Newton
	 * approximation barely affects third digit in the final error
	 * 0.667; the error in the rounded t can be up to about 3 23-bit ulps
	 * before the final error is larger than 0.667 ulps.
	 */
	u.f = t;
	u.i = (u.i + 0x80000000) & 0xffffffffc0000000ULL;
	t = u.f;

	/* one step Newton iteration to 53 bits with error < 0.667 ulps */
	s = t*t;         /* t*t is exact */
	r = x/s;         /* error <= 0.5 ulps; |r| < |t| */
	w = t+t;         /* t+t is exact */
	r = (r-t)/(w+r); /* r-t is exact; w+r ~= 3*t */
	t = t+t*r;       /* error <= 0.5 + 0.5/3 + epsilon */
	return t;
}

/* ---- derived from musl src/math/expm1.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/s_expm1.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/* expm1(x)
 * Returns exp(x)-1, the exponential of x minus 1.
 *
 * Method
 *   1. Argument reduction:
 *      Given x, find r and integer k such that
 *
 *               x = k*ln2 + r,  |r| <= 0.5*ln2 ~ 0.34658
 *
 *      Here a correction term c will be computed to compensate
 *      the error in r when rounded to a floating-point number.
 *
 *   2. Approximating expm1(r) by a special rational function on
 *      the interval [0,0.34658]:
 *      Since
 *          r*(exp(r)+1)/(exp(r)-1) = 2+ r^2/6 - r^4/360 + ...
 *      we define R1(r*r) by
 *          r*(exp(r)+1)/(exp(r)-1) = 2+ r^2/6 * R1(r*r)
 *      That is,
 *          R1(r**2) = 6/r *((exp(r)+1)/(exp(r)-1) - 2/r)
 *                   = 6/r * ( 1 + 2.0*(1/(exp(r)-1) - 1/r))
 *                   = 1 - r^2/60 + r^4/2520 - r^6/100800 + ...
 *      We use a special Remez algorithm on [0,0.347] to generate
 *      a polynomial of degree 5 in r*r to approximate R1. The
 *      maximum error of this polynomial approximation is bounded
 *      by 2**-61. In other words,
 *          R1(z) ~ 1.0 + Q1*z + Q2*z**2 + Q3*z**3 + Q4*z**4 + Q5*z**5
 *      where   Q1  =  -1.6666666666666567384E-2,
 *              Q2  =   3.9682539681370365873E-4,
 *              Q3  =  -9.9206344733435987357E-6,
 *              Q4  =   2.5051361420808517002E-7,
 *              Q5  =  -6.2843505682382617102E-9;
 *              z   =  r*r,
 *      with error bounded by
 *          |                  5           |     -61
 *          | 1.0+Q1*z+...+Q5*z   -  R1(z) | <= 2
 *          |                              |
 *
 *      expm1(r) = exp(r)-1 is then computed by the following
 *      specific way which minimize the accumulation rounding error:
 *                             2     3
 *                            r     r    [ 3 - (R1 + R1*r/2)  ]
 *            expm1(r) = r + --- + --- * [--------------------]
 *                            2     2    [ 6 - r*(3 - R1*r/2) ]
 *
 *      To compensate the error in the argument reduction, we use
 *              expm1(r+c) = expm1(r) + c + expm1(r)*c
 *                         ~ expm1(r) + c + r*c
 *      Thus c+r*c will be added in as the correction terms for
 *      expm1(r+c). Now rearrange the term to avoid optimization
 *      screw up:
 *                      (      2                                    2 )
 *                      ({  ( r    [ R1 -  (3 - R1*r/2) ]  )  }    r  )
 *       expm1(r+c)~r - ({r*(--- * [--------------------]-c)-c} - --- )
 *                      ({  ( 2    [ 6 - r*(3 - R1*r/2) ]  )  }    2  )
 *                      (                                             )
 *
 *                 = r - E
 *   3. Scale back to obtain expm1(x):
 *      From step 1, we have
 *         expm1(x) = either 2^k*[expm1(r)+1] - 1
 *                  = or     2^k*[expm1(r) + (1-2^-k)]
 *   4. Implementation notes:
 *      (A). To save one multiplication, we scale the coefficient Qi
 *           to Qi*2^i, and replace z by (x^2)/2.
 *      (B). To achieve maximum accuracy, we compute expm1(x) by
 *        (i)   if x < -56*ln2, return -1.0, (raise inexact if x!=inf)
 *        (ii)  if k=0, return r-E
 *        (iii) if k=-1, return 0.5*(r-E)-0.5
 *        (iv)  if k=1 if r < -0.25, return 2*((r+0.5)- E)
 *                     else          return  1.0+2.0*(r-E);
 *        (v)   if (k<-2||k>56) return 2^k(1-(E-r)) - 1 (or exp(x)-1)
 *        (vi)  if k <= 20, return 2^k((1-2^-k)-(E-r)), else
 *        (vii) return 2^k(1-((E+2^-k)-r))
 *
 * Special cases:
 *      expm1(INF) is INF, expm1(NaN) is NaN;
 *      expm1(-INF) is -1, and
 *      for finite argument, only expm1(0)=0 is exact.
 *
 * Accuracy:
 *      according to an error analysis, the error is always less than
 *      1 ulp (unit in the last place).
 *
 * Misc. info.
 *      For IEEE double
 *          if x >  7.09782712893383973096e+02 then expm1(x) overflow
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */


static const double
pm_expm1_o_threshold = 7.09782712893383973096e+02, /* 0x40862E42, 0xFEFA39EF */
pm_expm1_ln2_hi      = 6.93147180369123816490e-01, /* 0x3fe62e42, 0xfee00000 */
pm_expm1_ln2_lo      = 1.90821492927058770002e-10, /* 0x3dea39ef, 0x35793c76 */
pm_expm1_invln2      = 1.44269504088896338700e+00, /* 0x3ff71547, 0x652b82fe */
/* Scaled Q's: Qn_here = 2**n * Qn_above, for R(2*z) where z = hxs = x*x/2: */
pm_expm1_Q1 = -3.33333333333331316428e-02, /* BFA11111 111110F4 */
pm_expm1_Q2 =  1.58730158725481460165e-03, /* 3F5A01A0 19FE5585 */
pm_expm1_Q3 = -7.93650757867487942473e-05, /* BF14CE19 9EAADBB7 */
pm_expm1_Q4 =  4.00821782732936239552e-06, /* 3ED0CFCA 86E65239 */
pm_expm1_Q5 = -2.01099218183624371326e-07; /* BE8AFDB7 6E09C32D */

static double pm_expm1(double x)
{
	double y,hi,lo,c,t,e,hxs,hfx,r1,twopk;
	union {double f; uint64_t i;} u = {x};
	uint32_t hx = u.i>>32 & 0x7fffffff;
	int k, sign = u.i>>63;

	/* filter out huge and non-finite argument */
	if (hx >= 0x4043687A) {  /* if |x|>=56*ln2 */
		if (pm_isnan(x))
			return x;
		if (sign)
			return -1;
		if (x > pm_expm1_o_threshold) {
			x *= 0x1p1023;
			return x;
		}
	}

	/* argument reduction */
	if (hx > 0x3fd62e42) {  /* if  |x| > 0.5 ln2 */
		if (hx < 0x3FF0A2B2) {  /* and |x| < 1.5 ln2 */
			if (!sign) {
				hi = x - pm_expm1_ln2_hi;
				lo = pm_expm1_ln2_lo;
				k =  1;
			} else {
				hi = x + pm_expm1_ln2_hi;
				lo = -pm_expm1_ln2_lo;
				k = -1;
			}
		} else {
			k  = pm_expm1_invln2*x + (sign ? -0.5 : 0.5);
			t  = k;
			hi = x - t*pm_expm1_ln2_hi;  /* t*ln2_hi is exact here */
			lo = t*pm_expm1_ln2_lo;
		}
		x = hi-lo;
		c = (hi-x)-lo;
	} else if (hx < 0x3c900000) {  /* |x| < 2**-54, return x */
		if (hx < 0x00100000)
			FORCE_EVAL((float)x);
		return x;
	} else
		k = 0;

	/* x is now in primary range */
	hfx = 0.5*x;
	hxs = x*hfx;
	r1 = 1.0+hxs*(pm_expm1_Q1+hxs*(pm_expm1_Q2+hxs*(pm_expm1_Q3+hxs*(pm_expm1_Q4+hxs*pm_expm1_Q5))));
	t  = 3.0-r1*hfx;
	e  = hxs*((r1-t)/(6.0 - x*t));
	if (k == 0)   /* c is 0 */
		return x - (x*e-hxs);
	e  = x*(e-c) - c;
	e -= hxs;
	/* exp(x) ~ 2^k (x_reduced - e + 1) */
	if (k == -1)
		return 0.5*(x-e) - 0.5;
	if (k == 1) {
		if (x < -0.25)
			return -2.0*(e-(x+0.5));
		return 1.0+2.0*(x-e);
	}
	u.i = (uint64_t)(0x3ff + k)<<52;  /* 2^k */
	twopk = u.f;
	if (k < 0 || k > 56) {  /* suffice to return exp(x)-1 */
		y = x - e + 1.0;
		if (k == 1024)
			y = y*2.0*0x1p1023;
		else
			y = y*twopk;
		return y - 1.0;
	}
	u.i = (uint64_t)(0x3ff - k)<<52;  /* 2^-k */
	if (k < 20)
		y = (x-e+(1-u.f))*twopk;
	else
		y = (x-(e+u.f)+1)*twopk;
	return y;
}

/* ---- derived from musl src/math/log1p.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/s_log1p.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/* double log1p(double x)
 * Return the natural logarithm of 1+x.
 *
 * Method :
 *   1. Argument Reduction: find k and f such that
 *                      1+x = 2^k * (1+f),
 *         where  sqrt(2)/2 < 1+f < sqrt(2) .
 *
 *      Note. If k=0, then f=x is exact. However, if k!=0, then f
 *      may not be representable exactly. In that case, a correction
 *      term is need. Let u=1+x rounded. Let c = (1+x)-u, then
 *      log(1+x) - log(u) ~ c/u. Thus, we proceed to compute log(u),
 *      and add back the correction term c/u.
 *      (Note: when x > 2**53, one can simply return log(x))
 *
 *   2. Approximation of log(1+f): See log.c
 *
 *   3. Finally, log1p(x) = k*ln2 + log(1+f) + c/u. See log.c
 *
 * Special cases:
 *      log1p(x) is NaN with signal if x < -1 (including -INF) ;
 *      log1p(+INF) is +INF; log1p(-1) is -INF with signal;
 *      log1p(NaN) is that NaN with no signal.
 *
 * Accuracy:
 *      according to an error analysis, the error is always less than
 *      1 ulp (unit in the last place).
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 *
 * Note: Assuming log() return accurate answer, the following
 *       algorithm can be used to compute log1p(x) to within a few ULP:
 *
 *              u = 1+x;
 *              if(u==1.0) return x ; else
 *                         return log(u)*(x/(u-1.0));
 *
 *       See HP-15C Advanced Functions Handbook, p.193.
 */


static const double
pm_log1p_ln2_hi = 6.93147180369123816490e-01,  /* 3fe62e42 fee00000 */
pm_log1p_ln2_lo = 1.90821492927058770002e-10,  /* 3dea39ef 35793c76 */
pm_log1p_Lg1 = 6.666666666666735130e-01,  /* 3FE55555 55555593 */
pm_log1p_Lg2 = 3.999999999940941908e-01,  /* 3FD99999 9997FA04 */
pm_log1p_Lg3 = 2.857142874366239149e-01,  /* 3FD24924 94229359 */
pm_log1p_Lg4 = 2.222219843214978396e-01,  /* 3FCC71C5 1D8E78AF */
pm_log1p_Lg5 = 1.818357216161805012e-01,  /* 3FC74664 96CB03DE */
pm_log1p_Lg6 = 1.531383769920937332e-01,  /* 3FC39A09 D078C69F */
pm_log1p_Lg7 = 1.479819860511658591e-01;  /* 3FC2F112 DF3E5244 */

static double pm_log1p(double x)
{
	union {double f; uint64_t i;} u = {x};
	double hfsq,f,c,s,z,R,w,t1,t2,dk;
	uint32_t hx,hu;
	int k;

	hx = u.i>>32;
	k = 1;
	if (hx < 0x3fda827a || hx>>31) {  /* 1+x < sqrt(2)+ */
		if (hx >= 0xbff00000) {  /* x <= -1.0 */
			if (x == -1)
				return x/0.0; /* log1p(-1) = -inf */
			return (x-x)/0.0;     /* log1p(x<-1) = NaN */
		}
		if (hx<<1 < 0x3ca00000<<1) {  /* |x| < 2**-53 */
			/* underflow if subnormal */
			if ((hx&0x7ff00000) == 0)
				FORCE_EVAL((float)x);
			return x;
		}
		if (hx <= 0xbfd2bec4) {  /* sqrt(2)/2- <= 1+x < sqrt(2)+ */
			k = 0;
			c = 0;
			f = x;
		}
	} else if (hx >= 0x7ff00000)
		return x;
	if (k) {
		u.f = 1 + x;
		hu = u.i>>32;
		hu += 0x3ff00000 - 0x3fe6a09e;
		k = (int)(hu>>20) - 0x3ff;
		/* correction term ~ log(1+x)-log(u), avoid underflow in c/u */
		if (k < 54) {
			c = k >= 2 ? 1-(u.f-x) : x-(u.f-1);
			c /= u.f;
		} else
			c = 0;
		/* reduce u into [sqrt(2)/2, sqrt(2)] */
		hu = (hu&0x000fffff) + 0x3fe6a09e;
		u.i = (uint64_t)hu<<32 | (u.i&0xffffffff);
		f = u.f - 1;
	}
	hfsq = 0.5*f*f;
	s = f/(2.0+f);
	z = s*s;
	w = z*z;
	t1 = w*(pm_log1p_Lg2+w*(pm_log1p_Lg4+w*pm_log1p_Lg6));
	t2 = z*(pm_log1p_Lg1+w*(pm_log1p_Lg3+w*(pm_log1p_Lg5+w*pm_log1p_Lg7)));
	R = t2 + t1;
	dk = k;
	return s*(hfsq+R) + (dk*pm_log1p_ln2_lo+c) - hfsq + f + dk*pm_log1p_ln2_hi;
}

/* ---- derived from musl src/math/__expo2.c ---- */

/* k is such that k*ln2 has minimal relative error and x - kln2 > log(DBL_MIN) */
static const int pm_expo2_k = 2043;
static const double pm_expo2_kln2 = 0x1.62066151add8bp+10;

/* exp(x)/2 for x >= log(DBL_MAX), slightly better than 0.5*exp(x/2)*exp(x/2) */
static double pm_expo2(double x, double sign)
{
	double scale;

	/* note that k is odd and scale*scale overflows */
	INSERT_WORDS(scale, (uint32_t)(0x3ff + pm_expo2_k/2) << 20, 0);
	/* exp(x - k ln2) * 2**(k-1) */
	/* in directed rounding correct sign before rounding or overflow is important */
	return pm_exp(x - pm_expo2_kln2) * (sign * scale) * scale;
}

/* ---- derived from musl src/math/sinh.c ---- */

/* sinh(x) = (exp(x) - 1/exp(x))/2
 *         = (exp(x)-1 + (exp(x)-1)/exp(x))/2
 *         = x + x^3/6 + o(x^5)
 */
static double pm_sinh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	double t, h, absx;

	h = 0.5;
	if (u.i >> 63)
		h = -h;
	/* |x| */
	u.i &= (uint64_t)-1/2;
	absx = u.f;
	w = u.i >> 32;

	/* |x| < log(DBL_MAX) */
	if (w < 0x40862e42) {
		t = pm_expm1(absx);
		if (w < 0x3ff00000) {
			if (w < 0x3ff00000 - (26<<20))
				/* note: inexact and underflow are raised by expm1 */
				/* note: this branch avoids spurious underflow */
				return x;
			return h*(2*t - t*t/(t+1));
		}
		/* note: |x|>log(0x1p26)+eps could be just h*exp(x) */
		return h*(t + t/(t+1));
	}

	/* |x| > log(DBL_MAX) or nan */
	/* note: the result is stored to handle overflow */
	t = pm_expo2(absx, 2*h);
	return t;
}

/* ---- derived from musl src/math/cosh.c ---- */

/* cosh(x) = (exp(x) + 1/exp(x))/2
 *         = 1 + 0.5*(exp(x)-1)*(exp(x)-1)/exp(x)
 *         = 1 + x*x/2 + o(x^4)
 */
static double pm_cosh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	double t;

	/* |x| */
	u.i &= (uint64_t)-1/2;
	x = u.f;
	w = u.i >> 32;

	/* |x| < log(2) */
	if (w < 0x3fe62e42) {
		if (w < 0x3ff00000 - (26<<20)) {
			/* raise inexact if x!=0 */
			FORCE_EVAL(x + 0x1p120f);
			return 1;
		}
		t = pm_expm1(x);
		return 1 + t*t/(2*(1+t));
	}

	/* |x| < log(DBL_MAX) */
	if (w < 0x40862e42) {
		t = pm_exp(x);
		/* note: if x>log(0x1p26) then the 1/t is not needed */
		return 0.5*(t + 1/t);
	}

	/* |x| > log(DBL_MAX) or nan */
	/* note: the result is stored to handle overflow */
	t = pm_expo2(x, 1.0);
	return t;
}

/* ---- derived from musl src/math/tanh.c ---- */

/* tanh(x) = (exp(x) - exp(-x))/(exp(x) + exp(-x))
 *         = (exp(2*x) - 1)/(exp(2*x) - 1 + 2)
 *         = (1 - exp(-2*x))/(exp(-2*x) - 1 + 2)
 */
static double pm_tanh(double x)
{
	union {double f; uint64_t i;} u = {.f = x};
	uint32_t w;
	int sign;
	double t;

	/* x = |x| */
	sign = u.i >> 63;
	u.i &= (uint64_t)-1/2;
	x = u.f;
	w = u.i >> 32;

	if (w > 0x3fe193ea) {
		/* |x| > log(3)/2 ~= 0.5493 or nan */
		if (w > 0x40340000) {
			/* |x| > 20 or nan */
			/* note: this branch avoids raising overflow */
			t = 1 - 0/x;
		} else {
			t = pm_expm1(2*x);
			t = 1 - 2/(t+2);
		}
	} else if (w > 0x3fd058ae) {
		/* |x| > log(5/3)/2 ~= 0.2554 */
		t = pm_expm1(2*x);
		t = t/(t+2);
	} else if (w >= 0x00100000) {
		/* |x| >= 0x1p-1022, up to 2ulp error in [0.1,0.2554] */
		t = pm_expm1(-2*x);
		t = -t/(t+2);
	} else {
		/* |x| is subnormal */
		/* note: the branch above would not raise underflow in [0x1p-1023,0x1p-1022) */
		FORCE_EVAL((float)x);
		t = x;
	}
	return sign ? -t : t;
}

#endif
