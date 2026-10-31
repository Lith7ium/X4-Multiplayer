/* Harness for portable_decimal.c (driven by tools/dev/check_portable_decimal.py).
 * stdin: one case per line, "<separator hex byte> <text as hex bytes>"; stdout: one line
 * per case, "<binary64 bits> <end offset or -1> <error> <binary32 bits> <end> <error>".
 * The fallbacks are the signalling-NaN patterns 7ff00000000dead1 and 7f80dea1, so a
 * fallback result is recognisable. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portable_decimal.h"

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

int main(void)
{
	static char line[1 << 16];
	static char text[1 << 15];
	const uint64_t fb64 = 0x7ff00000000dead1ULL;
	const uint32_t fb32 = 0x7f80dea1u;
	double f64; float f32;
	memcpy(&f64, &fb64, 8); memcpy(&f32, &fb32, 4);
	while (fgets(line, sizeof line, stdin)) {
		char *p = line;
		unsigned sep = (unsigned)strtoul(p, &p, 16);
		while (*p == ' ') ++p;
		size_t n = 0;
		while (hexval(p[0]) >= 0 && hexval(p[1]) >= 0 && n + 1 < sizeof text) {
			text[n++] = (char)(hexval(p[0]) << 4 | hexval(p[1]));
			p += 2;
		}
		text[n] = 0;
		const char *end64 = NULL, *end32 = NULL;
		unsigned char err64 = 0, err32 = 0;
		double d = x4pd_parse64(text, (unsigned char)sep, &end64, &err64, f64);
		float f = x4pd_parse32(text, (unsigned char)sep, &end32, &err32, f32);
		uint64_t db; uint32_t fbits;
		memcpy(&db, &d, 8); memcpy(&fbits, &f, 4);
		printf("%016llx %ld %u %08x %ld %u\n", (unsigned long long)db, end64 ? (long)(end64 - text) : -1L, err64,
		       fbits, end32 ? (long)(end32 - text) : -1L, err32);
	}
	return 0;
}
