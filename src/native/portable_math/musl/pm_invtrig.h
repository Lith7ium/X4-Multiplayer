/* Inverse trigonometric functions.
 * Derived from musl libc src/math (the copy bundled with Zig 0.15.2); see
 * LICENSE-musl.txt. Changes for X4 Multiplayer: internal static linkage, pm_
 * prefixes, the __FP_FAST_FMA and TOINT_INTRINSICS branches resolved to the
 * plain IEEE path, double_t -> double. Included only by ../portable_math_impl.h. */
#ifndef X4PM_MUSL_PM_INVTRIG_H
#define X4PM_MUSL_PM_INVTRIG_H

/* ---- derived from musl src/math/atan.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/s_atan.c */
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
/* atan(x)
 * Method
 *   1. Reduce x to positive by atan(x) = -atan(-x).
 *   2. According to the integer k=4t+0.25 chopped, t=x, the argument
 *      is further reduced to one of the following intervals and the
 *      arctangent of t is evaluated by the corresponding formula:
 *
 *      [0,7/16]      atan(x) = t-t^3*(a1+t^2*(a2+...(a10+t^2*a11)...)
 *      [7/16,11/16]  atan(x) = atan(1/2) + atan( (t-0.5)/(1+t/2) )
 *      [11/16.19/16] atan(x) = atan( 1 ) + atan( (t-1)/(1+t) )
 *      [19/16,39/16] atan(x) = atan(3/2) + atan( (t-1.5)/(1+1.5t) )
 *      [39/16,INF]   atan(x) = atan(INF) + atan( -1/t )
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */



static const double pm_atan_atanhi[] = {
  4.63647609000806093515e-01, /* atan(0.5)hi 0x3FDDAC67, 0x0561BB4F */
  7.85398163397448278999e-01, /* atan(1.0)hi 0x3FE921FB, 0x54442D18 */
  9.82793723247329054082e-01, /* atan(1.5)hi 0x3FEF730B, 0xD281F69B */
  1.57079632679489655800e+00, /* atan(inf)hi 0x3FF921FB, 0x54442D18 */
};

static const double pm_atan_atanlo[] = {
  2.26987774529616870924e-17, /* atan(0.5)lo 0x3C7A2B7F, 0x222F65E2 */
  3.06161699786838301793e-17, /* atan(1.0)lo 0x3C81A626, 0x33145C07 */
  1.39033110312309984516e-17, /* atan(1.5)lo 0x3C700788, 0x7AF0CBBD */
  6.12323399573676603587e-17, /* atan(inf)lo 0x3C91A626, 0x33145C07 */
};

static const double pm_atan_aT[] = {
  3.33333333333329318027e-01, /* 0x3FD55555, 0x5555550D */
 -1.99999999998764832476e-01, /* 0xBFC99999, 0x9998EBC4 */
  1.42857142725034663711e-01, /* 0x3FC24924, 0x920083FF */
 -1.11111104054623557880e-01, /* 0xBFBC71C6, 0xFE231671 */
  9.09088713343650656196e-02, /* 0x3FB745CD, 0xC54C206E */
 -7.69187620504482999495e-02, /* 0xBFB3B0F2, 0xAF749A6D */
  6.66107313738753120669e-02, /* 0x3FB10D66, 0xA0D03D51 */
 -5.83357013379057348645e-02, /* 0xBFADDE2D, 0x52DEFD9A */
  4.97687799461593236017e-02, /* 0x3FA97B4B, 0x24760DEB */
 -3.65315727442169155270e-02, /* 0xBFA2B444, 0x2C6A6C2F */
  1.62858201153657823623e-02, /* 0x3F90AD3A, 0xE322DA11 */
};

static double pm_atan(double x)
{
	double w,s1,s2,z;
	uint32_t ix,sign;
	int id;

	GET_HIGH_WORD(ix, x);
	sign = ix >> 31;
	ix &= 0x7fffffff;
	if (ix >= 0x44100000) {   /* if |x| >= 2^66 */
		if (pm_isnan(x))
			return x;
		z = pm_atan_atanhi[3] + 0x1p-120f;
		return sign ? -z : z;
	}
	if (ix < 0x3fdc0000) {    /* |x| < 0.4375 */
		if (ix < 0x3e400000) {  /* |x| < 2^-27 */
			if (ix < 0x00100000)
				/* raise underflow for subnormal x */
				FORCE_EVAL((float)x);
			return x;
		}
		id = -1;
	} else {
		x = pm_fabs(x);
		if (ix < 0x3ff30000) {  /* |x| < 1.1875 */
			if (ix < 0x3fe60000) {  /*  7/16 <= |x| < 11/16 */
				id = 0;
				x = (2.0*x-1.0)/(2.0+x);
			} else {                /* 11/16 <= |x| < 19/16 */
				id = 1;
				x = (x-1.0)/(x+1.0);
			}
		} else {
			if (ix < 0x40038000) {  /* |x| < 2.4375 */
				id = 2;
				x = (x-1.5)/(1.0+1.5*x);
			} else {                /* 2.4375 <= |x| < 2^66 */
				id = 3;
				x = -1.0/x;
			}
		}
	}
	/* end of argument reduction */
	z = x*x;
	w = z*z;
	/* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
	s1 = z*(pm_atan_aT[0]+w*(pm_atan_aT[2]+w*(pm_atan_aT[4]+w*(pm_atan_aT[6]+w*(pm_atan_aT[8]+w*pm_atan_aT[10])))));
	s2 = w*(pm_atan_aT[1]+w*(pm_atan_aT[3]+w*(pm_atan_aT[5]+w*(pm_atan_aT[7]+w*pm_atan_aT[9]))));
	if (id < 0)
		return x - x*(s1+s2);
	z = pm_atan_atanhi[id] - (x*(s1+s2) - pm_atan_atanlo[id] - x);
	return sign ? -z : z;
}

/* ---- derived from musl src/math/atan2.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/e_atan2.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 */
/* atan2(y,x)
 * Method :
 *      1. Reduce y to positive by atan2(y,x)=-atan2(-y,x).
 *      2. Reduce x to positive by (if x and y are unexceptional):
 *              ARG (x+iy) = arctan(y/x)           ... if x > 0,
 *              ARG (x+iy) = pi - arctan[y/(-x)]   ... if x < 0,
 *
 * Special cases:
 *
 *      ATAN2((anything), NaN ) is NaN;
 *      ATAN2(NAN , (anything) ) is NaN;
 *      ATAN2(+-0, +(anything but NaN)) is +-0  ;
 *      ATAN2(+-0, -(anything but NaN)) is +-pi ;
 *      ATAN2(+-(anything but 0 and NaN), 0) is +-pi/2;
 *      ATAN2(+-(anything but INF and NaN), +INF) is +-0 ;
 *      ATAN2(+-(anything but INF and NaN), -INF) is +-pi;
 *      ATAN2(+-INF,+INF ) is +-pi/4 ;
 *      ATAN2(+-INF,-INF ) is +-3pi/4;
 *      ATAN2(+-INF, (anything but,0,NaN, and INF)) is +-pi/2;
 *
 * Constants:
 * The hexadecimal values are the intended ones for the following
 * constants. The decimal values may be used, provided that the
 * compiler will convert from decimal to binary accurately enough
 * to produce the hexadecimal values shown.
 */


static const double
pm_atan2_pi     = 3.1415926535897931160E+00, /* 0x400921FB, 0x54442D18 */
pm_atan2_pi_lo  = 1.2246467991473531772E-16; /* 0x3CA1A626, 0x33145C07 */

static double pm_atan2(double y, double x)
{
	double z;
	uint32_t m,lx,ly,ix,iy;

	if (pm_isnan(x) || pm_isnan(y))
		return x+y;
	EXTRACT_WORDS(ix, lx, x);
	EXTRACT_WORDS(iy, ly, y);
	if (((ix-0x3ff00000) | lx) == 0)  /* x = 1.0 */
		return pm_atan(y);
	m = ((iy>>31)&1) | ((ix>>30)&2);  /* 2*sign(x)+sign(y) */
	ix = ix & 0x7fffffff;
	iy = iy & 0x7fffffff;

	/* when y = 0 */
	if ((iy|ly) == 0) {
		switch(m) {
		case 0:
		case 1: return y;   /* atan(+-0,+anything)=+-0 */
		case 2: return  pm_atan2_pi; /* atan(+0,-anything) = pi */
		case 3: return -pm_atan2_pi; /* atan(-0,-anything) =-pi */
		}
	}
	/* when x = 0 */
	if ((ix|lx) == 0)
		return m&1 ? -pm_atan2_pi/2 : pm_atan2_pi/2;
	/* when x is INF */
	if (ix == 0x7ff00000) {
		if (iy == 0x7ff00000) {
			switch(m) {
			case 0: return  pm_atan2_pi/4;   /* atan(+INF,+INF) */
			case 1: return -pm_atan2_pi/4;   /* atan(-INF,+INF) */
			case 2: return  3*pm_atan2_pi/4; /* atan(+INF,-INF) */
			case 3: return -3*pm_atan2_pi/4; /* atan(-INF,-INF) */
			}
		} else {
			switch(m) {
			case 0: return  0.0; /* atan(+...,+INF) */
			case 1: return -0.0; /* atan(-...,+INF) */
			case 2: return  pm_atan2_pi;  /* atan(+...,-INF) */
			case 3: return -pm_atan2_pi;  /* atan(-...,-INF) */
			}
		}
	}
	/* |y/x| > 0x1p64 */
	if (ix+(64<<20) < iy || iy == 0x7ff00000)
		return m&1 ? -pm_atan2_pi/2 : pm_atan2_pi/2;

	/* z = atan(|y/x|) without spurious underflow */
	if ((m&2) && iy+(64<<20) < ix)  /* |y/x| < 0x1p-64, x<0 */
		z = 0;
	else
		z = pm_atan(pm_fabs(y/x));
	switch (m) {
	case 0: return z;              /* atan(+,+) */
	case 1: return -z;             /* atan(-,+) */
	case 2: return pm_atan2_pi - (z-pm_atan2_pi_lo); /* atan(+,-) */
	default: /* case 3 */
		return (z-pm_atan2_pi_lo) - pm_atan2_pi; /* atan(-,-) */
	}
}

/* ---- derived from musl src/math/asin.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/e_asin.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/* asin(x)
 * Method :
 *      Since  asin(x) = x + x^3/6 + x^5*3/40 + x^7*15/336 + ...
 *      we approximate asin(x) on [0,0.5] by
 *              asin(x) = x + x*x^2*R(x^2)
 *      where
 *              R(x^2) is a rational approximation of (asin(x)-x)/x^3
 *      and its remez error is bounded by
 *              |(asin(x)-x)/x^3 - R(x^2)| < 2^(-58.75)
 *
 *      For x in [0.5,1]
 *              asin(x) = pi/2-2*asin(sqrt((1-x)/2))
 *      Let y = (1-x), z = y/2, s := sqrt(z), and pio2_hi+pio2_lo=pi/2;
 *      then for x>0.98
 *              asin(x) = pi/2 - 2*(s+s*z*R(z))
 *                      = pio2_hi - (2*(s+s*z*R(z)) - pio2_lo)
 *      For x<=0.98, let pio4_hi = pio2_hi/2, then
 *              f = hi part of s;
 *              c = sqrt(z) - f = (z-f*f)/(s+f)         ...f+c=sqrt(z)
 *      and
 *              asin(x) = pi/2 - 2*(s+s*z*R(z))
 *                      = pio4_hi+(pio4-2s)-(2s*z*R(z)-pio2_lo)
 *                      = pio4_hi+(pio4-2f)-(2s*z*R(z)-(pio2_lo+2c))
 *
 * Special cases:
 *      if x is NaN, return x itself;
 *      if |x|>1, return NaN with invalid signal.
 *
 */


static const double
pm_asin_pio2_hi = 1.57079632679489655800e+00, /* 0x3FF921FB, 0x54442D18 */
pm_asin_pio2_lo = 6.12323399573676603587e-17, /* 0x3C91A626, 0x33145C07 */
/* coefficients for R(x^2) */
pm_asin_pS0 =  1.66666666666666657415e-01, /* 0x3FC55555, 0x55555555 */
pm_asin_pS1 = -3.25565818622400915405e-01, /* 0xBFD4D612, 0x03EB6F7D */
pm_asin_pS2 =  2.01212532134862925881e-01, /* 0x3FC9C155, 0x0E884455 */
pm_asin_pS3 = -4.00555345006794114027e-02, /* 0xBFA48228, 0xB5688F3B */
pm_asin_pS4 =  7.91534994289814532176e-04, /* 0x3F49EFE0, 0x7501B288 */
pm_asin_pS5 =  3.47933107596021167570e-05, /* 0x3F023DE1, 0x0DFDF709 */
pm_asin_qS1 = -2.40339491173441421878e+00, /* 0xC0033A27, 0x1C8A2D4B */
pm_asin_qS2 =  2.02094576023350569471e+00, /* 0x40002AE5, 0x9C598AC8 */
pm_asin_qS3 = -6.88283971605453293030e-01, /* 0xBFE6066C, 0x1B8D0159 */
pm_asin_qS4 =  7.70381505559019352791e-02; /* 0x3FB3B8C5, 0xB12E9282 */

static double pm_asin_R(double z)
{
	double p, q;
	p = z*(pm_asin_pS0+z*(pm_asin_pS1+z*(pm_asin_pS2+z*(pm_asin_pS3+z*(pm_asin_pS4+z*pm_asin_pS5)))));
	q = 1.0+z*(pm_asin_qS1+z*(pm_asin_qS2+z*(pm_asin_qS3+z*pm_asin_qS4)));
	return p/q;
}

static double pm_asin(double x)
{
	double z,r,s;
	uint32_t hx,ix;

	GET_HIGH_WORD(hx, x);
	ix = hx & 0x7fffffff;
	/* |x| >= 1 or nan */
	if (ix >= 0x3ff00000) {
		uint32_t lx;
		GET_LOW_WORD(lx, x);
		if (((ix-0x3ff00000) | lx) == 0)
			/* asin(1) = +-pi/2 with inexact */
			return x*pm_asin_pio2_hi + 0x1p-120f;
		return 0/(x-x);
	}
	/* |x| < 0.5 */
	if (ix < 0x3fe00000) {
		/* if 0x1p-1022 <= |x| < 0x1p-26, avoid raising underflow */
		if (ix < 0x3e500000 && ix >= 0x00100000)
			return x;
		return x + x*pm_asin_R(x*x);
	}
	/* 1 > |x| >= 0.5 */
	z = (1 - pm_fabs(x))*0.5;
	s = pm_sqrt(z);
	r = pm_asin_R(z);
	if (ix >= 0x3fef3333) {  /* if |x| > 0.975 */
		x = pm_asin_pio2_hi-(2*(s+s*r)-pm_asin_pio2_lo);
	} else {
		double f,c;
		/* f+c = sqrt(z) */
		f = s;
		SET_LOW_WORD(f,0);
		c = (z-f*f)/(s+f);
		x = 0.5*pm_asin_pio2_hi - (2*s*r - (pm_asin_pio2_lo-2*c) - (0.5*pm_asin_pio2_hi-2*f));
	}
	if (hx >> 31)
		return -x;
	return x;
}

/* ---- derived from musl src/math/acos.c ---- */
/* origin: FreeBSD /usr/src/lib/msun/src/e_acos.c */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
/* acos(x)
 * Method :
 *      acos(x)  = pi/2 - asin(x)
 *      acos(-x) = pi/2 + asin(x)
 * For |x|<=0.5
 *      acos(x) = pi/2 - (x + x*x^2*R(x^2))     (see asin.c)
 * For x>0.5
 *      acos(x) = pi/2 - (pi/2 - 2asin(sqrt((1-x)/2)))
 *              = 2asin(sqrt((1-x)/2))
 *              = 2s + 2s*z*R(z)        ...z=(1-x)/2, s=sqrt(z)
 *              = 2f + (2c + 2s*z*R(z))
 *     where f=hi part of s, and c = (z-f*f)/(s+f) is the correction term
 *     for f so that f+c ~ sqrt(z).
 * For x<-0.5
 *      acos(x) = pi - 2asin(sqrt((1-|x|)/2))
 *              = pi - 0.5*(s+s*z*R(z)), where z=(1-|x|)/2,s=sqrt(z)
 *
 * Special cases:
 *      if x is NaN, return x itself;
 *      if |x|>1, return NaN with invalid signal.
 *
 * Function needed: sqrt
 */


static const double
pm_acos_pio2_hi = 1.57079632679489655800e+00, /* 0x3FF921FB, 0x54442D18 */
pm_acos_pio2_lo = 6.12323399573676603587e-17, /* 0x3C91A626, 0x33145C07 */
pm_acos_pS0 =  1.66666666666666657415e-01, /* 0x3FC55555, 0x55555555 */
pm_acos_pS1 = -3.25565818622400915405e-01, /* 0xBFD4D612, 0x03EB6F7D */
pm_acos_pS2 =  2.01212532134862925881e-01, /* 0x3FC9C155, 0x0E884455 */
pm_acos_pS3 = -4.00555345006794114027e-02, /* 0xBFA48228, 0xB5688F3B */
pm_acos_pS4 =  7.91534994289814532176e-04, /* 0x3F49EFE0, 0x7501B288 */
pm_acos_pS5 =  3.47933107596021167570e-05, /* 0x3F023DE1, 0x0DFDF709 */
pm_acos_qS1 = -2.40339491173441421878e+00, /* 0xC0033A27, 0x1C8A2D4B */
pm_acos_qS2 =  2.02094576023350569471e+00, /* 0x40002AE5, 0x9C598AC8 */
pm_acos_qS3 = -6.88283971605453293030e-01, /* 0xBFE6066C, 0x1B8D0159 */
pm_acos_qS4 =  7.70381505559019352791e-02; /* 0x3FB3B8C5, 0xB12E9282 */

static double pm_acos_R(double z)
{
	double p, q;
	p = z*(pm_acos_pS0+z*(pm_acos_pS1+z*(pm_acos_pS2+z*(pm_acos_pS3+z*(pm_acos_pS4+z*pm_acos_pS5)))));
	q = 1.0+z*(pm_acos_qS1+z*(pm_acos_qS2+z*(pm_acos_qS3+z*pm_acos_qS4)));
	return p/q;
}

static double pm_acos(double x)
{
	double z,w,s,c,df;
	uint32_t hx,ix;

	GET_HIGH_WORD(hx, x);
	ix = hx & 0x7fffffff;
	/* |x| >= 1 or nan */
	if (ix >= 0x3ff00000) {
		uint32_t lx;

		GET_LOW_WORD(lx,x);
		if (((ix-0x3ff00000) | lx) == 0) {
			/* acos(1)=0, acos(-1)=pi */
			if (hx >> 31)
				return 2*pm_acos_pio2_hi + 0x1p-120f;
			return 0;
		}
		return 0/(x-x);
	}
	/* |x| < 0.5 */
	if (ix < 0x3fe00000) {
		if (ix <= 0x3c600000)  /* |x| < 2**-57 */
			return pm_acos_pio2_hi + 0x1p-120f;
		return pm_acos_pio2_hi - (x - (pm_acos_pio2_lo-x*pm_acos_R(x*x)));
	}
	/* x < -0.5 */
	if (hx >> 31) {
		z = (1.0+x)*0.5;
		s = pm_sqrt(z);
		w = pm_acos_R(z)*s-pm_acos_pio2_lo;
		return 2*(pm_acos_pio2_hi - (s+w));
	}
	/* x > 0.5 */
	z = (1.0-x)*0.5;
	s = pm_sqrt(z);
	df = s;
	SET_LOW_WORD(df,0);
	c = (z-df*df)/(s+df);
	w = pm_acos_R(z)*s+c;
	return 2*(df+w);
}

#endif
