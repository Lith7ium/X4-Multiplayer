/* X4 Multiplayer portable decimal reader; see portable_decimal.h. Original code:
 * the syntax follows the engines' readers, the value comes from exact big-integer
 * arithmetic and one round-to-nearest-even step. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "portable_decimal.h"

#define PD_MAXD 800             /* significant digits kept; the rest only matter as sticky */
#define PD_LIMBS 132            /* 4224 bits: 10^1130 << 60 with headroom */

enum {PD_VALUE, PD_ZERO, PD_FALLBACK};
typedef struct {
	int negative, sticky;
	unsigned ndigits;
	int64_t e10;                /* value = digits * 10^e10 */
	unsigned char digit[PD_MAXD];
} pd_number;

/* ---- the engines' syntax ---- */
static int pd_space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int pd_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* The exponent after 'e': 32-bit, wrapping like the engines. Returns 1 and the value,
 * or 0 on an error; *end as the engines leave it. */
static int pd_exponent(const unsigned char *pos, unsigned char sep, const unsigned char **end, int32_t *value)
{
	uint32_t v = 0;
	int negative = 0, sign = 0, digits = 0;
	unsigned base = 10;
	unsigned char c = *pos;
	if (c == sep) {
		*end = pos;
		return 0;
	}
	for (;;) {
		if (c == 0) {
			*end = pos;
			if (!digits)
				return 0;
			*value = (int32_t)(negative ? 0u - v : v);
			return 1;
		}
		if (pd_space(c)) {
			c = *++pos;
		} else if (c == '/') {
			while (*pos != '\n' && *pos != 0)
				++pos;
			c = *pos;
		} else if (c == '.') {
			do
				c = *++pos;
			while (c == '0');
			if (c != sep && c != 0) {
				*end = pos;
				return 0;
			}
		} else if (c == '-' || c == '+') {
			if (digits || sign) {
				*end = pos;
				return 0;
			}
			negative = c == '-';
			sign = 1;
			c = *++pos;
		} else if (c == '%') {
			if (digits) {
				*end = pos;
				return 0;
			}
			base = 2;
			c = *++pos;
		} else if (c == '#') {
			if (digits) {
				*end = pos;
				return 0;
			}
			do
				c = *++pos;
			while (c != sep && c != 0);
			*end = c ? pos + 1 : pos;
			*value = 0;
			return 1;
		} else if (base == 2) {
			if (sign || (c != '0' && c != '1')) {
				*end = pos;
				return 0;
			}
			v = v * 2 + (uint32_t)(c - '0');
			digits = 1;
			c = *++pos;
		} else {
			if (!pd_digit(c)) {
				*end = pos;
				return 0;
			}
			v = v * 10 + (uint32_t)(c - '0');
			digits = 1;
			c = *++pos;
		}
		if (c == sep) {
			if (!digits) {
				*end = pos;
				return 0;
			}
			*end = c ? pos + 1 : pos;
			*value = (int32_t)(negative ? 0u - v : v);
			return 1;
		}
	}
}

static void pd_keep(pd_number *n, unsigned char d, int fraction)
{
	if (n->ndigits == 0 && d == 0) {        /* leading zero: only moves fraction digits */
		if (fraction)
			--n->e10;
		return;
	}
	if (n->ndigits < PD_MAXD) {
		n->digit[n->ndigits++] = d;
		if (fraction)
			--n->e10;
	} else {
		n->sticky |= d != 0;
		if (!fraction)
			++n->e10;
	}
}

static int pd_scan(const unsigned char *s, unsigned char sep, const unsigned char **end_out,
                   unsigned char *error, pd_number *n)
{
	const unsigned char *pos = s, *end = s;
	int moved = 0, digits = 0, point = 0;
	unsigned char c;
	memset(n, 0, offsetof(pd_number, digit));
	if (!s) {
		if (error) *error = 1;
		return PD_FALLBACK;
	}
	c = *pos;
	if (c == sep)
		goto missing;
	for (;;) {
		if (c == 0) {
			if (moved) end = pos;
			if (!digits) goto missing;
			goto number;
		}
		if (c == 'e' || c == 'E') {
			if (moved) end = pos;
			if (!digits) goto missing;
			goto exponent;
		}
		if (pd_space(c)) {
			++pos;
		} else if (c == '#') {
			if (moved) end = pos;
			if (digits) goto failed;
			do
				c = *++end;
			while (c != sep && c != 0);
			if (c) ++end;
			if (end_out) *end_out = end;
			return PD_ZERO;
		} else if (c == '/') {
			while (*pos != '\n' && *pos != 0)
				++pos;
		} else if (c == '.') {
			if (point) goto failed;
			point = 1;
			++pos;
		} else if (c == '-') {
			if (digits || n->negative) goto failed;
			n->negative = 1;
			++pos;
		} else if (pd_digit(c)) {
			pd_keep(n, (unsigned char)(c - '0'), point);
			digits = 1;
			++pos;
		} else {
			goto failed;
		}
		moved = 1;
		c = *pos;
		if (c == sep) {
			end = pos;
			if (!digits) goto missing;
			if ((sep & 0xdf) == 'E') goto exponent;
			if (sep) end = ++pos;
			goto number;
		}
	}
exponent: {
		int32_t exponent = 0;
		if (!pd_exponent(pos + 1, sep, &end, &exponent)) {
			if (end_out) *end_out = end;
			if (error) *error = 1;
			return PD_FALLBACK;
		}
		n->e10 += exponent;
	}
number:
	if (end_out) *end_out = end;
	return PD_VALUE;
missing:
	if (end_out) *end_out = pos;
	if (error) *error = 1;
	return PD_ZERO;
failed:
	if (end_out) *end_out = pos;
	if (error) *error = 1;
	return PD_FALLBACK;
}

/* ---- unsigned big integers, little-endian 32-bit limbs ---- */
typedef struct { unsigned n; uint32_t w[PD_LIMBS]; } pd_big;

static void pd_trim(pd_big *a) { while (a->n && !a->w[a->n - 1]) --a->n; }
static void pd_small(pd_big *a, uint32_t v) { a->n = v != 0; a->w[0] = v; }
static unsigned pd_bits(const pd_big *a)
{
	unsigned b = 0;
	if (!a->n) return 0;
	for (uint32_t top = a->w[a->n - 1]; top; top >>= 1) ++b;
	return (a->n - 1) * 32 + b;
}
static int pd_bit(const pd_big *a, unsigned i) { return i / 32 < a->n && (a->w[i / 32] >> (i % 32) & 1); }
/* a = a * m + add */
static void pd_mul_add(pd_big *a, uint32_t m, uint32_t add)
{
	uint64_t carry = add;
	for (unsigned i = 0; i < a->n; i++) {
		uint64_t t = (uint64_t)a->w[i] * m + carry;
		a->w[i] = (uint32_t)t;
		carry = t >> 32;
	}
	if (carry && a->n < PD_LIMBS) a->w[a->n++] = (uint32_t)carry;
}
static void pd_shl(pd_big *a, unsigned s)
{
	unsigned limbs = s / 32, bits = s % 32;
	if (!a->n) return;
	if (a->n + limbs + 1 > PD_LIMBS) limbs = PD_LIMBS - a->n - 1; /* not reached within the bounds used */
	a->w[a->n + limbs] = 0;
	for (unsigned i = a->n; i-- > 0;) {
		a->w[i + limbs + 1] |= bits ? a->w[i] >> (32 - bits) : 0;
		a->w[i + limbs] = a->w[i] << bits;
	}
	for (unsigned i = 0; i < limbs; i++) a->w[i] = 0;
	a->n += limbs + 1;
	pd_trim(a);
}
static int pd_cmp(const pd_big *a, const pd_big *b)
{
	if (a->n != b->n) return a->n < b->n ? -1 : 1;
	for (unsigned i = a->n; i-- > 0;)
		if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
	return 0;
}
static void pd_sub(pd_big *a, const pd_big *b) /* a -= b, a >= b */
{
	uint64_t borrow = 0;
	for (unsigned i = 0; i < a->n; i++) {
		uint64_t t = (uint64_t)a->w[i] - (i < b->n ? b->w[i] : 0) - borrow;
		a->w[i] = (uint32_t)t;
		borrow = t >> 63;
	}
	pd_trim(a);
}
static void pd_shr1(pd_big *a)
{
	for (unsigned i = 0; i < a->n; i++)
		a->w[i] = a->w[i] >> 1 | (i + 1 < a->n ? a->w[i + 1] << 31 : 0);
	pd_trim(a);
}
static void pd_pow10(pd_big *a, unsigned e)
{
	pd_small(a, 1);
	for (; e >= 9; e -= 9) pd_mul_add(a, 1000000000u, 0);
	static const uint32_t p10[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
	pd_mul_add(a, p10[e], 0);
}

/* Round M * 2^X (plus a sticky tail below M's last bit) to precision p with minimum
 * exponent emin and maximum exponent emax; returns the IEEE bits without the sign. */
static uint64_t pd_round(const pd_big *m, int64_t x, int sticky, unsigned p, int emin, int emax)
{
	int64_t len = pd_bits(m), e = len - 1 + x;      /* e: weight of the leading bit */
	int64_t lsb = (e < emin ? emin : e) - (int64_t)p + 1, shift = lsb - x;
	uint64_t q = 0, one = (uint64_t)1 << (p - 1);
	if (shift <= 0) {                               /* exact; callers keep a sticky tail only with shift > 0 */
		for (int64_t i = len - 1; i >= 0; i--) q = q << 1 | (uint64_t)pd_bit(m, (unsigned)i);
		q <<= -shift;
	} else {
		int half = shift - 1 < len ? pd_bit(m, (unsigned)(shift - 1)) : 0, below = sticky;
		for (int64_t i = 0; i < shift - 1 && i < len && !below; i++) below = pd_bit(m, (unsigned)i);
		for (int64_t i = len - 1; i >= shift; i--) q = q << 1 | (uint64_t)pd_bit(m, (unsigned)i);
		if (half && (below || (q & 1))) ++q;
		if (q >> p) { q >>= 1; ++lsb; }              /* rounding carried into a new leading bit */
	}
	if (q >= one) {                                 /* normal */
		int64_t exponent = lsb + p - 1;
		if (exponent > emax) return (uint64_t)(2 * emax + 1) << (p - 1);  /* infinity */
		return (uint64_t)(exponent + emax) << (p - 1) | (q - one);
	}
	return q;                                       /* subnormal or zero: lsb == emin - p + 1 */
}

/* Correctly rounded magnitude bits of digits * 10^e10 for (p, emin, emax); values of
 * 10^max10 and more overflow, values below 10^min10 round to zero. About 2 KB of stack. */
static uint64_t pd_value(const pd_number *n, unsigned p, int emin, int emax, int max10, int min10)
{
	pd_big a, b, r;
	int64_t magnitude = (int64_t)n->ndigits + n->e10;   /* 10^(magnitude-1) <= value < 10^magnitude */
	if (!n->ndigits) return 0;
	if (magnitude - 1 >= max10) return (uint64_t)(2 * emax + 1) << (p - 1);
	if (magnitude < min10) return 0;
	a.n = 0;
	for (unsigned i = 0; i < n->ndigits; i++) pd_mul_add(&a, 10, n->digit[i]);
	if (n->e10 >= 0) {
		pd_pow10(&b, (unsigned)n->e10);
		memset(r.w, 0, sizeof(r.w));
		r.n = a.n + b.n;
		for (unsigned i = 0; i < a.n; i++) {
			uint64_t carry = 0;
			for (unsigned j = 0; j < b.n; j++) {
				uint64_t t = (uint64_t)a.w[i] * b.w[j] + r.w[i + j] + carry;
				r.w[i + j] = (uint32_t)t;
				carry = t >> 32;
			}
			r.w[i + b.n] = (uint32_t)carry;
		}
		pd_trim(&r);
		return pd_round(&r, 0, n->sticky, p, emin, emax);
	}
	/* a / 10^-e10, shifted for at least p + 3 quotient bits; the remainder is sticky */
	pd_pow10(&b, (unsigned)(-n->e10));
	int64_t k = (int64_t)pd_bits(&b) - (int64_t)pd_bits(&a) + p + 3;
	if (k < 0) k = 0;
	pd_shl(&a, (unsigned)k);
	unsigned top = pd_bits(&a) > pd_bits(&b) ? pd_bits(&a) - pd_bits(&b) : 0;
	memset(r.w, 0, sizeof(r.w));
	r.n = top / 32 + 1;
	pd_shl(&b, top);
	for (unsigned i = top + 1; i-- > 0;) {
		if (pd_cmp(&a, &b) >= 0) {
			pd_sub(&a, &b);
			r.w[i / 32] |= 1u << (i % 32);
		}
		pd_shr1(&b);
	}
	pd_trim(&r);
	return pd_round(&r, -k, n->sticky || a.n != 0, p, emin, emax);
}

X4PD_API double x4pd_parse64(const char *text, unsigned char separator, const char **end,
                             unsigned char *error, double fallback)
{
	pd_number n;
	int kind = pd_scan((const unsigned char *)text, separator, (const unsigned char **)end, error, &n);
	if (kind == PD_FALLBACK)
		return fallback;
	uint64_t bits = kind == PD_VALUE ? pd_value(&n, 53, -1022, 1023, 309, -324) | (uint64_t)n.negative << 63 : 0;
	double v;
	memcpy(&v, &bits, 8);
	return v;
}

X4PD_API float x4pd_parse32(const char *text, unsigned char separator, const char **end,
                            unsigned char *error, float fallback)
{
	pd_number n;
	int kind = pd_scan((const unsigned char *)text, separator, (const unsigned char **)end, error, &n);
	if (kind == PD_FALLBACK)
		return fallback;
	uint32_t bits = kind == PD_VALUE ? (uint32_t)pd_value(&n, 24, -126, 127, 39, -46) | (uint32_t)n.negative << 31 : 0;
	float v;
	memcpy(&v, &bits, 4);
	return v;
}
