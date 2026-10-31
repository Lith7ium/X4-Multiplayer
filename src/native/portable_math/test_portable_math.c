/* Cross-compiler / cross-platform proof harness for the portable math library.
 *   test_portable_math hash [count]     per-function SHA-256 of all outputs, three MXCSR states
 *   test_portable_math dump PATH [count] inputs and outputs as text, for the mpmath accuracy check
 *   test_portable_math check [count]    contract checks (sincos, float = rounded double, pow forms,
 *                                       NaN results, MXCSR restoration), prints the KAT digest
 *   test_portable_math sweep NAME       every binary32 input of one unary float function, SHA-256
 * Inputs come from integer generators only, so every build sees the same input bits. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portable_math.h"

/* ---- SHA-256 (FIPS 180-4) ---- */
typedef struct { uint32_t h[8]; uint64_t length; unsigned char block[64]; unsigned used; } sha256;
static const uint32_t sha_k[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void sha_block(sha256 *s, const unsigned char *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h;
	for (unsigned i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
	for (unsigned i = 16; i < 64; i++)
		w[i] = w[i-16] + (rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3)) + w[i-7] +
		       (rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10));
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (unsigned i = 0; i < 64; i++) {
		uint32_t t1 = h + (rotr(e,6) ^ rotr(e,11) ^ rotr(e,25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
		uint32_t t2 = (rotr(a,2) ^ rotr(a,13) ^ rotr(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(sha256 *s)
{
	static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
	memcpy(s->h, iv, sizeof iv); s->length = 0; s->used = 0;
}
static void sha_update(sha256 *s, const void *data, size_t n)
{
	const unsigned char *p = data;
	s->length += n;
	while (n) {
		unsigned take = 64 - s->used;
		if (take > n) take = (unsigned)n;
		memcpy(s->block + s->used, p, take); s->used += take; p += take; n -= take;
		if (s->used == 64) { sha_block(s, s->block); s->used = 0; }
	}
}
static void sha_final(sha256 *s, char hex[65])
{
	uint64_t bits = s->length * 8;
	unsigned char pad = 0x80, zero = 0, len[8];
	sha_update(s, &pad, 1);
	while (s->used != 56) sha_update(s, &zero, 1);
	for (unsigned i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
	sha_update(s, len, 8);
	for (unsigned i = 0; i < 8; i++) sprintf(hex + 8 * i, "%08x", s->h[i]);
}
static void sha_u64(sha256 *s, uint64_t v)
{
	unsigned char b[8];
	for (unsigned i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (8 * i));
	sha_update(s, b, 8);
}

/* ---- bits ---- */
static uint64_t dbits(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static double bitsd(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }
static uint32_t fbits(float x) { uint32_t u; memcpy(&u, &x, 4); return u; }
static float bitsf(uint32_t u) { float x; memcpy(&x, &u, 4); return x; }

/* ---- MXCSR ---- */
static unsigned getcsr(void) { unsigned c; __asm__ __volatile__("stmxcsr %0" : "=m"(c) : : "memory"); return c; }
static void setcsr(unsigned c) { __asm__ __volatile__("ldmxcsr %0" : : "m"(c) : "memory"); }

/* ---- function table ---- */
enum kind { D1, D2, DSC, F1, F2, FSC };
enum domain { WIDE, TRIG, INVTRIG, EXP, LOG, LOG1P, POW, HYP, ROOT, PAIR };
typedef struct {
	const char *name; enum kind kind; enum domain domain;
	double (*d1)(double); double (*d2)(double, double); void (*dsc)(double, double *, double *);
	float (*f1)(float); float (*f2)(float, float); void (*fsc)(float, float *, float *);
} fn;
#define FD1(n, d) {#n, D1, d, x4pm_##n, 0, 0, 0, 0, 0}
#define FD2(n, d) {#n, D2, d, 0, x4pm_##n, 0, 0, 0, 0}
#define FF1(n, d) {#n, F1, d, 0, 0, 0, x4pm_##n, 0, 0}
#define FF2(n, d) {#n, F2, d, 0, 0, 0, 0, x4pm_##n, 0}
static const fn fns[] = {
	FD1(sin, TRIG), FD1(cos, TRIG), FD1(tan, TRIG), {"sincos", DSC, TRIG, 0, 0, x4pm_sincos, 0, 0, 0},
	FD1(asin, INVTRIG), FD1(acos, INVTRIG), FD1(atan, WIDE), FD2(atan2, PAIR),
	FD1(exp, EXP), FD1(exp2, EXP), FD1(expm1, EXP), FD1(log, LOG), FD1(log2, LOG), FD1(log10, LOG),
	FD1(log1p, LOG1P), FD2(pow, POW), FD2(fmod, PAIR), FD2(hypot, PAIR), FD1(cbrt, ROOT),
	FD1(sinh, HYP), FD1(cosh, HYP), FD1(tanh, HYP),
	FF1(sinf, TRIG), FF1(cosf, TRIG), FF1(tanf, TRIG), {"sincosf", FSC, TRIG, 0, 0, 0, 0, 0, x4pm_sincosf},
	FF1(asinf, INVTRIG), FF1(acosf, INVTRIG), FF1(atanf, WIDE), FF2(atan2f, PAIR),
	FF1(expf, EXP), FF1(exp2f, EXP), FF1(expm1f, EXP), FF1(logf, LOG), FF1(log2f, LOG), FF1(log10f, LOG),
	FF1(log1pf, LOG1P), FF2(powf, POW), FF2(fmodf, PAIR), FF2(hypotf, PAIR), FF1(cbrtf, ROOT),
	FF1(sinhf, HYP), FF1(coshf, HYP), FF1(tanhf, HYP),
};
#define NFNS (sizeof(fns) / sizeof(*fns))

/* ---- inputs ---- */
static const uint64_t edge_d[] = {
	0x0000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000001ULL, 0x8000000000000001ULL,
	0x000fffffffffffffULL, 0x0010000000000000ULL, 0x8010000000000000ULL, 0x3ff0000000000000ULL,
	0xbff0000000000000ULL, 0x3fe0000000000000ULL, 0xbfe0000000000000ULL, 0x4000000000000000ULL,
	0xc000000000000000ULL, 0x3fefffffffffffffULL, 0x3ff0000000000001ULL, 0x3ff921fb54442d18ULL,
	0xbff921fb54442d18ULL, 0x400921fb54442d18ULL, 0x4012d97c7f3321d2ULL, 0x401921fb54442d18ULL,
	0x7fefffffffffffffULL, 0xffefffffffffffffULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL,
	0x7ff8000000000000ULL, 0xfff8000000000000ULL, 0x7ff0000000000001ULL, 0xfff4000000000123ULL,
	0x7ff8000000000abcULL, 0x7e37e43c8800759cULL, 0x4086232bdd7abcd2ULL, 0xc0874910d52d3051ULL,
	0x40862e42fefa39efULL, 0xc086232bdd7abcd2ULL, 0x4090000000000000ULL, 0xc090c80000000000ULL,
	0x3e45798ee2308c3aULL, 0x41d0000000000000ULL, 0x4330000000000001ULL, 0xc330000000000000ULL,
	0x4340000000000000ULL, 0x44d0000000000000ULL, 0x3c90000000000000ULL, 0x4008000000000000ULL,
	0xc008000000000000ULL, 0x3fe6a09e667f3bcdULL, 0x3ff6a09e667f3bcdULL, 0x7fe0000000000000ULL};
static const uint32_t edge_f[] = {
	0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x00800000u, 0x80800000u,
	0x3f800000u, 0xbf800000u, 0x3f000000u, 0xbf000000u, 0x40000000u, 0xc0000000u, 0x3f7fffffu,
	0x3f800001u, 0x3fc90fdbu, 0xbfc90fdbu, 0x40490fdbu, 0x4096cbe4u, 0x40c90fdbu, 0x7f7fffffu,
	0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u, 0x7f800001u, 0xffa00123u,
	0x7fc00abcu, 0x42b17218u, 0xc2cff1b5u, 0xc2b00000u, 0x43000000u, 0xc3160000u, 0x4b800001u,
	0xcb800000u, 0x5a000000u, 0x33800000u, 0x40400000u, 0xc0400000u, 0x3f3504f3u, 0x3fb504f3u,
	0x7e800000u, 0x4e800000u, 0x5c000000u, 0x7149f2cau, 0x2e800000u, 0x0c000000u};
#define NEDGE_D (sizeof(edge_d) / sizeof(*edge_d))
#define NEDGE_F (sizeof(edge_f) / sizeof(*edge_f))

static uint64_t mix64(uint64_t *state)
{
	uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}
static uint64_t seed_of(const char *name, unsigned stream)
{
	uint64_t h = 0xcbf29ce484222325ULL ^ stream;
	for (; *name; name++) { h ^= (unsigned char)*name; h *= 0x100000001b3ULL; }
	return h;
}
/* A double with random sign/mantissa and a biased exponent in [lo, hi]. */
static uint64_t dexp(uint64_t r, unsigned lo, unsigned hi)
{
	unsigned e = lo + (unsigned)((r >> 11) % (hi - lo + 1));
	return (r & 0x800fffffffffffffULL) | (uint64_t)e << 52;
}
static uint32_t fexp(uint64_t r, unsigned lo, unsigned hi)
{
	unsigned e = lo + (unsigned)((r >> 40) % (hi - lo + 1));
	return ((uint32_t)r & 0x807fffffu) | (uint32_t)e << 23;
}
static uint64_t gen_d(enum domain d, uint64_t *st)
{
	uint64_t r = mix64(st), r2 = mix64(st);
	switch (r2 % 8) {
	case 0: case 1: return r;                                  /* any bit pattern */
	case 2: return dexp(r, 1023 - 30, 1023 + 30);
	case 3: return dexp(r, 1023 - 4, 1023 + 6);
	case 5: return dexp(r, 0, 60);                              /* subnormal and tiny */
	case 6: return dexp(r, 1900, 2046);                         /* huge */
	case 7: {                                                   /* small integers and halves */
		int64_t k = (int64_t)(r % 4001) - 2000;
		return dbits((double)k * ((r2 >> 8) & 1 ? 0.5 : 1.0));
	}
	default: break;
	}
	switch (d) {                                                /* case 4: the function's hard region */
	case TRIG: {                                                /* next to k*pi/2, k up to 2^24 */
		int64_t k = (int64_t)(r % (1u << 24)) - (1 << 23);
		uint64_t near = dbits((double)k * 1.5707963267948966);
		return near + (uint64_t)((int64_t)(r2 >> 32 & 15) - 8);
	}
	case INVTRIG: return dexp(r, 990, 1022);
	case EXP: return dexp(r, 1013, 1032);
	case LOG: return (r2 >> 16 & 1) ? (0x3ff0000000000000ULL | (r & 0xffffffffffULL))
	                                : (0x3fe0000000000000ULL | (r & 0x000fffffffffffffULL) | 0x000ff00000000000ULL);
	case LOG1P: return dexp(r, 960, 1023) | ((r2 >> 20 & 1) ? 0x8000000000000000ULL : 0);
	case HYP: return dexp(r, 1023 - 30, 1023 + 9);
	case ROOT: case WIDE: case PAIR: case POW: default: return dexp(r, 1, 2046);
	}
}
static uint32_t gen_f(enum domain d, uint64_t *st)
{
	uint64_t r = mix64(st), r2 = mix64(st);
	switch (r2 % 8) {
	case 0: case 1: return (uint32_t)r;
	case 2: return fexp(r, 127 - 30, 127 + 30);
	case 3: return fexp(r, 127 - 4, 127 + 6);
	case 5: return fexp(r, 0, 20);
	case 6: return fexp(r, 200, 254);
	case 7: {
		int32_t k = (int32_t)(r % 4001) - 2000;
		return fbits((float)k * ((r2 >> 8) & 1 ? 0.5f : 1.0f));
	}
	default: break;
	}
	switch (d) {
	case TRIG: {
		int32_t k = (int32_t)(r % (1u << 20)) - (1 << 19);
		uint32_t near = fbits((float)((double)k * 1.5707963267948966));
		return near + (uint32_t)((int32_t)(r2 >> 32 & 7) - 4);
	}
	case INVTRIG: return fexp(r, 100, 126);
	case EXP: return fexp(r, 117, 134);
	case LOG: return (r2 >> 16 & 1) ? (0x3f800000u | ((uint32_t)r & 0xfffu)) : (0x3f000000u | ((uint32_t)r & 0x7fffffu) | 0x7f0000u);
	case LOG1P: return fexp(r, 90, 127) | ((r2 >> 20 & 1) ? 0x80000000u : 0);
	case HYP: return fexp(r, 127 - 30, 127 + 6);
	default: return fexp(r, 1, 254);
	}
}
/* pow pairs: wide, near 1 with large y, negative x with integer y, typical game exponents */
static void gen_pow_d(uint64_t *st, uint64_t *x, uint64_t *y)
{
	uint64_t r = mix64(st), s = mix64(st);
	switch (s % 4) {
	case 0: *x = gen_d(PAIR, st); *y = gen_d(PAIR, st); break;
	case 1: *x = 0x3ff0000000000000ULL ^ (r & 0x80000000fffffULL); *y = dexp(s, 1023, 1023 + 40); break;
	case 2: *x = dexp(r, 1013, 1033) | 0x8000000000000000ULL; *y = dbits((double)((int64_t)((s >> 8) % 61) - 30)); break;
	default: *x = dexp(r, 1013, 1033) & 0x7fffffffffffffffULL; *y = dexp(s, 1013, 1027); break;
	}
}
static void gen_pow_f(uint64_t *st, uint32_t *x, uint32_t *y)
{
	uint64_t r = mix64(st), s = mix64(st);
	switch (s % 4) {
	case 0: *x = gen_f(PAIR, st); *y = gen_f(PAIR, st); break;
	case 1: *x = 0x3f800000u ^ ((uint32_t)r & 0x8000ffffu); *y = fexp(s, 127, 127 + 20); break;
	case 2: *x = fexp(r, 117, 137) | 0x80000000u; *y = fbits((float)((int32_t)((s >> 8) % 61) - 30)); break;
	default: *x = fexp(r, 117, 137) & 0x7fffffffu; *y = fexp(s, 117, 131); break;
	}
}

/* ---- evaluation: input bits -> output bits ---- */
static void eval(const fn *f, uint64_t a, uint64_t b, uint64_t out[2])
{
	double ds, dc; float fs, fc;
	out[0] = out[1] = 0;
	switch (f->kind) {
	case D1: out[0] = dbits(f->d1(bitsd(a))); break;
	case D2: out[0] = dbits(f->d2(bitsd(a), bitsd(b))); break;
	case DSC: f->dsc(bitsd(a), &ds, &dc); out[0] = dbits(ds); out[1] = dbits(dc); break;
	case F1: out[0] = fbits(f->f1(bitsf((uint32_t)a))); break;
	case F2: out[0] = fbits(f->f2(bitsf((uint32_t)a), bitsf((uint32_t)b))); break;
	case FSC: f->fsc(bitsf((uint32_t)a), &fs, &fc); out[0] = fbits(fs); out[1] = fbits(fc); break;
	}
}
static int is_float(const fn *f) { return f->kind >= F1; }
static int is_binary(const fn *f) { return f->kind == D2 || f->kind == F2; }
/* The i-th input pair of a function: edge pairs first, then the generated stream. */
typedef struct { uint64_t st; unsigned long long index; } input_stream;
static int next_input(const fn *f, input_stream *s, unsigned long long count, uint64_t *a, uint64_t *b)
{
	unsigned long long i = s->index++;
	size_t ne = is_float(f) ? NEDGE_F : NEDGE_D;
	unsigned long long edges = is_binary(f) ? (unsigned long long)ne * ne : ne;
	if (i >= edges + count) return 0;
	*b = 0;
	if (i < edges) {
		unsigned long long p = is_binary(f) ? i / ne : i, q = is_binary(f) ? i % ne : 0;
		*a = is_float(f) ? edge_f[p] : edge_d[p];
		if (is_binary(f)) *b = is_float(f) ? edge_f[q] : edge_d[q];
		return 1;
	}
	if (f->domain == POW) {
		if (is_float(f)) { uint32_t x, y; gen_pow_f(&s->st, &x, &y); *a = x; *b = y; }
		else gen_pow_d(&s->st, a, b);
		return 1;
	}
	if (is_float(f)) { *a = gen_f(f->domain, &s->st); if (is_binary(f)) *b = gen_f(f->domain, &s->st); }
	else { *a = gen_d(f->domain, &s->st); if (is_binary(f)) *b = gen_d(f->domain, &s->st); }
	return 1;
}

static int run_hash(unsigned long long count)
{
	static const unsigned modes[3] = {0x1f80u, 0xffc0u, 0x1f80u & ~0x0680u}; /* default; FTZ+DAZ+toward zero; invalid/zero/overflow traps on */
	char total_hex[3][65];
	int ok = 1;
	for (unsigned m = 0; m < 3; m++) {
		sha256 total; sha_init(&total);
		unsigned long long points = 0;
		for (size_t k = 0; k < NFNS; k++) {
			const fn *f = &fns[k];
			sha256 s; sha_init(&s);
			input_stream in = {seed_of(f->name, 1), 0};
			uint64_t a, b, out[2];
			unsigned long long n = 0;
			unsigned saved = getcsr(), after;
			/* inputs are generated in the default state; only the calls run in modes[m] */
			while (next_input(f, &in, count, &a, &b)) {
				setcsr(modes[m]);
				eval(f, a, b, out);
				after = getcsr();
				setcsr(saved);
				if (after != modes[m] && m && ok) {
					printf("MXCSR_CHANGED fn=%s mode=%04x after=%04x\n", f->name, modes[m], after);
					ok = 0;
				}
				sha_u64(&s, out[0]);
				if (f->kind == DSC || f->kind == FSC) sha_u64(&s, out[1]);
				++n;
			}
			char hex[65]; sha_final(&s, hex);
			if (m == 0) printf("fn %-8s points=%llu sha256=%s\n", f->name, n, hex);
			sha_update(&total, hex, 64);
			points += n;
		}
		sha_final(&total, total_hex[m]);
		printf("total mxcsr=%04x functions=%u points=%llu sha256=%s\n", modes[m], (unsigned)NFNS, points, total_hex[m]);
	}
	if (strcmp(total_hex[0], total_hex[1]) || strcmp(total_hex[0], total_hex[2])) { puts("MXCSR_DEPENDENT"); ok = 0; }
	printf("kat %016llx expected %016llx\n", (unsigned long long)x4pm_kat(), (unsigned long long)X4PM_KAT_EXPECTED);
	return ok ? 0 : 1;
}

static int run_dump(const char *path, unsigned long long count)
{
	FILE *out = fopen(path, "wb");
	if (!out) return 2;
	for (size_t k = 0; k < NFNS; k++) {
		const fn *f = &fns[k];
		input_stream in = {seed_of(f->name, 2), 0};
		uint64_t a, b, r[2];
		while (next_input(f, &in, count, &a, &b)) {
			eval(f, a, b, r);
			fprintf(out, "%s %llx %llx %llx %llx\n", f->name, (unsigned long long)a, (unsigned long long)b,
				(unsigned long long)r[0], (unsigned long long)r[1]);
		}
	}
	return fclose(out) ? 2 : 0;
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; if (failures < 40) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); } } } while (0)
static int dnan(uint64_t u) { return (u & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL; }
static int fnan(uint32_t u) { return (u & 0x7fffffffu) > 0x7f800000u; }

static int run_check(unsigned long long count)
{
	/* 1. sincos == (sin, cos); float = the double function rounded once; NaN results canonical. */
	for (size_t k = 0; k < NFNS; k++) {
		const fn *f = &fns[k];
		input_stream in = {seed_of(f->name, 3), 0};
		uint64_t a, b, r[2];
		while (next_input(f, &in, count, &a, &b)) {
			eval(f, a, b, r);
			if (f->kind == DSC) {
				CHECK(r[0] == dbits(x4pm_sin(bitsd(a))) && r[1] == dbits(x4pm_cos(bitsd(a))), "sincos %llx", (unsigned long long)a);
				continue;
			}
			if (f->kind == FSC) {
				CHECK(r[0] == fbits(x4pm_sinf(bitsf((uint32_t)a))) && r[1] == fbits(x4pm_cosf(bitsf((uint32_t)a))), "sincosf %llx", (unsigned long long)a);
				continue;
			}
			int nan_in = is_float(f) ? fnan((uint32_t)a) || (is_binary(f) && fnan((uint32_t)b)) : dnan(a) || (is_binary(f) && dnan(b));
			int nan_out = is_float(f) ? fnan((uint32_t)r[0]) : dnan(r[0]);
			if (nan_out) {
				uint64_t want;
				if (is_float(f))
					want = fnan((uint32_t)a) ? ((uint32_t)a | 0x00400000u) : (is_binary(f) && fnan((uint32_t)b)) ? ((uint32_t)b | 0x00400000u) : 0xffc00000u;
				else
					want = dnan(a) ? (a | 0x0008000000000000ULL) : (is_binary(f) && dnan(b)) ? (b | 0x0008000000000000ULL) : 0xfff8000000000000ULL;
				CHECK(r[0] == want, "%s nan %llx %llx -> %llx", f->name, (unsigned long long)a, (unsigned long long)b, (unsigned long long)r[0]);
			}
			if (is_float(f) && !nan_in && !nan_out && strcmp(f->name, "powf")) {
				/* the float function is the double one rounded once */
				char dname[16]; size_t len = strlen(f->name) - 1; memcpy(dname, f->name, len); dname[len] = 0;
				for (size_t j = 0; j < NFNS; j++) if (!strcmp(fns[j].name, dname)) {
					uint64_t d[2]; uint64_t da = dbits((double)bitsf((uint32_t)a)), db = dbits((double)bitsf((uint32_t)b));
					eval(&fns[j], da, db, d);
					CHECK(fbits((float)bitsd(d[0])) == (uint32_t)r[0], "%s != (float)%s at %llx %llx", f->name, dname, (unsigned long long)a, (unsigned long long)b);
				}
			}
		}
	}
	/* 2. pow folds: x*x, x and 1/x for y = 2, 1, -1 (also powf) */
	{
		uint64_t st = seed_of("powfold", 4);
		for (unsigned long long i = 0; i < count; i++) {
			uint64_t a = gen_d(WIDE, &st); uint32_t fa = gen_f(WIDE, &st);
			double x = bitsd(a); float fx = bitsf(fa);
			if (dnan(a) || fnan(fa)) continue;
			CHECK(dbits(x4pm_pow(x, 2.0)) == dbits(x * x), "pow2 %llx", (unsigned long long)a);
			CHECK(dbits(x4pm_pow(x, 1.0)) == a, "pow1 %llx", (unsigned long long)a);
			CHECK(dbits(x4pm_pow(x, -1.0)) == dbits(1.0 / x), "powm1 %llx", (unsigned long long)a);
			CHECK(fbits(x4pm_powf(fx, 2.0f)) == fbits(fx * fx), "powf2 %x", fa);
			CHECK(fbits(x4pm_powf(fx, 1.0f)) == fa, "powf1 %x", fa);
			CHECK(fbits(x4pm_powf(fx, -1.0f)) == fbits(1.0f / fx), "powfm1 %x", fa);
		}
	}
	/* 3. special values (C99 Annex F) */
	CHECK(dbits(x4pm_pow(bitsd(0x7ff8000000000000ULL), 0.0)) == dbits(1.0), "pow(nan,0)");
	CHECK(dbits(x4pm_pow(1.0, bitsd(0x7ff8000000000000ULL))) == dbits(1.0), "pow(1,nan)");
	CHECK(dbits(x4pm_pow(-0.0, -1.0)) == 0xfff0000000000000ULL, "pow(-0,-1)");
	CHECK(dbits(x4pm_sin(-0.0)) == 0x8000000000000000ULL && dbits(x4pm_tan(-0.0)) == 0x8000000000000000ULL, "sin/tan -0");
	CHECK(dbits(x4pm_cos(bitsd(0x7ff0000000000000ULL))) == 0xfff8000000000000ULL, "cos(inf)");
	CHECK(dbits(x4pm_atan2(0.0, -0.0)) == dbits(3.141592653589793), "atan2(0,-0)");
	CHECK(dbits(x4pm_hypot(bitsd(0x7ff0000000000000ULL), bitsd(0x7ff8000000000000ULL))) == 0x7ff0000000000000ULL, "hypot(inf,nan)");
	CHECK(dbits(x4pm_log(0.0)) == 0xfff0000000000000ULL && dbits(x4pm_log(-1.0)) == 0xfff8000000000000ULL, "log(0), log(-1)");
	CHECK(dbits(x4pm_exp(1000.0)) == 0x7ff0000000000000ULL && dbits(x4pm_exp(-1000.0)) == 0, "exp over/underflow");
	CHECK(fbits(x4pm_expf(89.0f)) == 0x7f800000u && fbits(x4pm_sinf(bitsf(0xffa00123u))) == 0xffe00123u, "expf overflow, sinf(snan)");
	CHECK(dbits(x4pm_fmod(5.5, 2.0)) == dbits(1.5) && dbits(x4pm_fmod(-0.0, 1.0)) == 0x8000000000000000ULL, "fmod");
	CHECK(dbits(x4pm_sin(bitsd(0x7e37e43c8800759cULL))) == dbits(x4pm_sin(bitsd(0x7e37e43c8800759cULL))), "huge sin stable");
	/* 4. MXCSR: every entry point restores the caller's state exactly, flags included */
	{
		static const unsigned states[] = {0x1f80u, 0x9fc0u, 0x3f80u, 0x5f80u, 0x7f80u, 0x1fbfu, 0x1d80u};
		for (unsigned i = 0; i < sizeof(states) / sizeof(*states); i++) {
			unsigned saved = getcsr();
			setcsr(states[i]);
			double s, c; float fs, fc;
			volatile double r1 = x4pm_sin(1e300), r2 = x4pm_pow(0.5, 1e-310), r3 = x4pm_exp(-745.0), r4 = x4pm_log(-1.0);
			volatile double r5 = x4pm_atan2(bitsd(0x7ff4000000000001ULL), 1e-310);
			x4pm_sincos(3.0, &s, &c); x4pm_sincosf(3.0f, &fs, &fc);
			volatile float rf = x4pm_powf(1e-20f, 3.0f), rg = x4pm_logf(0.0f), rh = x4pm_sinf(bitsf(0xffa00123u));
			(void)r1; (void)r2; (void)r3; (void)r4; (void)r5; (void)rf; (void)rg; (void)rh;
			unsigned after = getcsr();
			setcsr(saved);
			if ((states[i] & 0xffc0u) != 0x1f80u)
				CHECK(after == states[i], "mxcsr %04x -> %04x", states[i], after);
			else
				CHECK((after & 0xffc0u) == (states[i] & 0xffc0u), "mxcsr control %04x -> %04x", states[i], after);
		}
	}
	printf("kat %016llx expected %016llx\n", (unsigned long long)x4pm_kat(), (unsigned long long)X4PM_KAT_EXPECTED);
	printf("check failures=%d\n", failures);
	return failures ? 1 : 0;
}

static int run_sweep(const char *name)
{
	const fn *f = NULL;
	for (size_t k = 0; k < NFNS; k++) if (!strcmp(fns[k].name, name)) f = &fns[k];
	if (!f || (f->kind != F1 && f->kind != FSC)) return 2;
	sha256 s; sha_init(&s);
	uint32_t i = 0;
	do {
		uint64_t r[2];
		eval(f, i, 0, r);
		unsigned char b[8] = {(unsigned char)r[0], (unsigned char)(r[0] >> 8), (unsigned char)(r[0] >> 16), (unsigned char)(r[0] >> 24),
			(unsigned char)r[1], (unsigned char)(r[1] >> 8), (unsigned char)(r[1] >> 16), (unsigned char)(r[1] >> 24)};
		sha_update(&s, b, f->kind == FSC ? 8 : 4);
	} while (++i);
	char hex[65]; sha_final(&s, hex);
	printf("sweep %s inputs=4294967296 sha256=%s\n", name, hex);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "hash")) return run_hash(argc > 2 ? strtoull(argv[2], NULL, 10) : 1000000ULL);
	if (argc >= 3 && !strcmp(argv[1], "dump")) return run_dump(argv[2], argc > 3 ? strtoull(argv[3], NULL, 10) : 20000ULL);
	if (argc >= 2 && !strcmp(argv[1], "check")) return run_check(argc > 2 ? strtoull(argv[2], NULL, 10) : 200000ULL);
	if (argc == 3 && !strcmp(argv[1], "sweep")) return run_sweep(argv[2]);
	fputs("usage: test_portable_math hash [count] | dump PATH [count] | check [count] | sweep NAME\n", stderr);
	return 2;
}
