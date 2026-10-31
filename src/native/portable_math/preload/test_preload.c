/* Interposition test for libx4mp_portable_math.so (Linux).
 *   test_preload portable   run with LD_PRELOAD=libx4mp_portable_math.so
 *   test_preload glibc      run without it
 * The binary is linked against libm like X4 (versioned references such as
 * exp@GLIBC_2.29) and also contains the portable library under its x4pm_ names.
 * It resolves each of the 28 symbols after the program (dlsym RTLD_NEXT, dladdr), calls
 * every one directly (PLT), through its address (GLOB_DAT or canonical PLT) and
 * the x4pm_ name, and compares the bits: equal for every
 * input under the preload; with glibc alone it reports how many inputs differ. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../portable_math.h"

/* Direct calls through the PLT (wrappers), the libm address (GLOB_DAT or canonical PLT)
 * and the portable library, side by side. */
#define WD1(n) static double w_##n(double x) { return n(x); }
#define WD2(n) static double w_##n(double x, double y) { return n(x, y); }
#define WF1(n) static float w_##n(float x) { return n(x); }
#define WF2(n) static float w_##n(float x, float y) { return n(x, y); }
WD1(sin) WD1(cos) WD1(tan) WD1(asin) WD1(acos) WD1(atan) WD2(atan2) WD1(exp) WD1(exp2) WD1(log) WD1(log2) WD1(log10) WD2(pow)
WF1(sinf) WF1(cosf) WF1(tanf) WF1(asinf) WF1(acosf) WF1(atanf) WF2(atan2f) WF1(expf) WF1(exp2f) WF1(logf) WF1(log2f) WF1(log10f) WF2(powf)
static void w_sincos(double x, double *s, double *c) { sincos(x, s, c); }
static void w_sincosf(float x, float *s, float *c) { sincosf(x, s, c); }
typedef struct { void *direct, *address, *portable; } triple;
#define D1(n) {#n, 0, {(void *)w_##n, (void *)n, (void *)x4pm_##n}}
#define D2(n) {#n, 1, {(void *)w_##n, (void *)n, (void *)x4pm_##n}}
#define F1(n) {#n, 2, {(void *)w_##n, (void *)n, (void *)x4pm_##n}}
#define F2(n) {#n, 3, {(void *)w_##n, (void *)n, (void *)x4pm_##n}}
typedef struct { const char *name; int kind; triple f; } entry2;
static const entry2 entries[] = {
	D1(sin), D1(cos), D1(tan), {"sincos", 4, {(void *)w_sincos, (void *)sincos, (void *)x4pm_sincos}},
	D1(asin), D1(acos), D1(atan), D2(atan2), D1(exp), D1(exp2), D1(log), D1(log2), D1(log10), D2(pow),
	F1(sinf), F1(cosf), F1(tanf), {"sincosf", 5, {(void *)w_sincosf, (void *)sincosf, (void *)x4pm_sincosf}},
	F1(asinf), F1(acosf), F1(atanf), F2(atan2f), F1(expf), F1(exp2f), F1(logf), F1(log2f), F1(log10f), F2(powf),
};
#define N (sizeof(entries) / sizeof(*entries))

static uint64_t mix(uint64_t *s)
{
	uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}
static double bd(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }
static float bf(uint32_t u) { float x; memcpy(&x, &u, 4); return x; }
static uint64_t db(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static uint32_t fb(float x) { uint32_t u; memcpy(&u, &x, 4); return u; }

int main(int argc, char **argv)
{
	int portable = argc == 2 && !strcmp(argv[1], "portable");
	int failures = 0;
	unsigned long long differ_total = 0, calls = 0;
	if (argc != 2 || (!portable && strcmp(argv[1], "glibc"))) {
		fputs("usage: test_preload portable|glibc\n", stderr);
		return 2;
	}
	for (size_t i = 0; i < N; i++) {
		const entry2 *e = &entries[i];
		void *where = dlsym(RTLD_NEXT, e->name); /* the next definition after this program: no canonical PLT */
		Dl_info info;
		const char *file = where && dladdr(where, &info) && info.dli_fname ? info.dli_fname : "?";
		int ours = strstr(file, "x4mp_portable_math") != NULL;
		if (ours != portable) {
			printf("FAIL %s resolves to %s\n", e->name, file);
			++failures;
		}
		uint64_t st = 0x5052454c4f414431ULL ^ i;
		unsigned long long differ = 0;
		for (unsigned k = 0; k < 20000; k++) {
			uint64_t r = mix(&st), q = mix(&st);
			if (k & 1) {
				r = (r & 0x800fffffffffffffULL) | (uint64_t)(1013 + r % 20) << 52;
				q = (q & 0x800fffffffffffffULL) | (uint64_t)(1013 + q % 20) << 52;
			}
			double x = bd(r), y = bd(q);
			float fx = (k & 1) ? (float)x : bf((uint32_t)(r >> 32)), fy = (k & 1) ? (float)y : bf((uint32_t)q);
			uint64_t out[3][2];
			void *fn[3] = {e->f.direct, e->f.address, e->f.portable};
			for (unsigned w = 0; w < 3; w++) {
				double s = 0, c = 0; float fs = 0, fc = 0;
				out[w][1] = 0;
				switch (e->kind) {
				case 0: out[w][0] = db(((double (*)(double))fn[w])(x)); break;
				case 1: out[w][0] = db(((double (*)(double, double))fn[w])(x, y)); break;
				case 2: out[w][0] = fb(((float (*)(float))fn[w])(fx)); break;
				case 3: out[w][0] = fb(((float (*)(float, float))fn[w])(fx, fy)); break;
				case 4: ((void (*)(double, double *, double *))fn[w])(x, &s, &c); out[w][0] = db(s); out[w][1] = db(c); break;
				default: ((void (*)(float, float *, float *))fn[w])(fx, &fs, &fc); out[w][0] = fb(fs); out[w][1] = fb(fc); break;
				}
			}
			calls += 2;
			if (memcmp(out[0], out[2], 16) || memcmp(out[1], out[2], 16))
				++differ;
			if (memcmp(out[0], out[1], 16)) {
				printf("FAIL %s: direct and address-taken calls disagree\n", e->name);
				++failures;
				break;
			}
		}
		if (portable && differ) {
			printf("FAIL %s differs from the portable library on %llu inputs\n", e->name, differ);
			++failures;
		}
		if (!portable)
			printf("glibc %-8s differs on %llu of 20000 inputs (%s)\n", e->name, differ, file);
		differ_total += differ;
	}
	unsigned (*info)(uint64_t *) = (unsigned (*)(uint64_t *))dlsym(RTLD_DEFAULT, "x4mp_portable_math_info");
	if (portable) {
		uint64_t kat = 0;
		if (!info || info(&kat) != (X4PM_VERSION << 16 | 28u) || kat != X4PM_KAT_EXPECTED) {
			printf("FAIL x4mp_portable_math_info\n");
			++failures;
		}
	} else if (info) {
		printf("FAIL the preload is present without being requested\n");
		++failures;
	}
	printf("%s symbols=%u calls=%llu differing_inputs=%llu failures=%d\n", portable ? "portable" : "glibc",
		(unsigned)N, calls, differ_total, failures);
	return failures ? 1 : 0;
}
