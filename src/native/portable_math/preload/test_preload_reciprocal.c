/* The preload's exact-reciprocal rewrite (X4MP_EXACT_RECIPROCAL=1), Linux, PIE build.
 *   test_preload_reciprocal                                   synthetic recipes only
 *   test_preload_reciprocal <X4 ELF> <libx4mp_portable_math.so> <export file>
 *
 * Synthetic: for d,s in xmm0-15 and seven window shapes (RCPSS/RSQRTSS d,s before a NOP
 * or a copied RIP-relative LEA; RSQRTSS d,s + RCPSS d,d one by one; the same as an
 * rcp(rsqrt) chain, adjacent and around the LEA; a lone chain end; the chain through a
 * register copy, RSQRTSS d,s + MOVAPS c,d + RCPSS c,d: c[0] = sqrt(x), d[0] = 1/sqrt(x)) a generated function
 * loads all sixteen XMM registers, fills the 128-byte red zone below RSP, sets flags,
 * runs the window and stores everything. The window is first run as is (approximate,
 * within the architectural 1.5*2^-12), then patched to jmp rel32 into the trampoline
 * that exact_reciprocal_emit.h emits with the Linux drop of 144 bytes: d[0] must then
 * hold the exactly rounded 1/x, 1/sqrt(x), 1/(1/sqrt(x)) or, for a chain, sqrt(x) bit for
 * bit (a lone chain end leaves d alone), every other register lane, the red zone, the
 * flags and RSP must be unchanged and the copied LEA must yield the original address.
 * Mapped: maps the ELF's PT_LOAD segments at their fixed addresses (the file is only
 * read), checks that one changed byte makes x4mp_exact_reciprocal_install refuse without
 * writing, installs, checks every window's jmp and int3 fill, and writes the trampoline
 * region for tools/dev/check_exact_reciprocal.py --linux; then runs every copied chain's
 * installed windows on the mapped X4 code (copied_run) and checks the copy register
 * against sqrtss bit for bit. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../exact_reciprocal_emit.h"
#include "exact_reciprocal_linux_sites.h"
#include "../../engine/lockstep_spline_length.h"

#define DROP 144u
#define LEA_DISP 0x12345
enum { OUT_XMM = 0, OUT_RED = 256, OUT_FLAGS0 = 384, OUT_FLAGS1 = 392, OUT_R9 = 400, OUT_SIZE = 408 };

static unsigned failures;
#define CHECK(c, ...) do { if (!(c)) { if (failures++ < 20) { printf(__VA_ARGS__); printf("\n"); } } } while (0)

static unsigned movups(unsigned char *p, int store, unsigned reg, unsigned base, int32_t disp)
{
	unsigned n = 0;
	if (reg >= 8) p[n++] = 0x44;
	p[n++] = 0x0f; p[n++] = store ? 0x11 : 0x10;
	p[n++] = (unsigned char)(0x80 | (reg & 7) << 3 | base);
	memcpy(p + n, &disp, 4);
	return n + 4;
}
static unsigned mov_rsi(unsigned char *p, unsigned reg, int32_t disp) /* mov [rsi+disp], r64 */
{
	p[0] = reg >= 8 ? 0x4c : 0x48; p[1] = 0x89; p[2] = (unsigned char)(0x86 | (reg & 7) << 3);
	memcpy(p + 3, &disp, 4);
	return 7;
}
static unsigned approx(unsigned char *p, unsigned kind, unsigned d, unsigned s)
{
	unsigned n = 0;
	p[n++] = 0xf3;
	if (d >= 8 || s >= 8) p[n++] = (unsigned char)(0x40 | (d >= 8) << 2 | (s >= 8));
	p[n++] = 0x0f; p[n++] = kind == XRCP_RCPSS || kind == XRCP_CHAIN_END || kind == XRCP_COPY_END || kind == XRCP_MOVE_END ? 0x53 : 0x52;
	p[n++] = (unsigned char)(0xc0 | (d & 7) << 3 | (s & 7));
	return n;
}
/* The copied chain's copy register for d (any other register). */
static unsigned copy_of(unsigned d) { return (d + 5) & 15; }

#define VARIANTS 8
static void put_approx(unsigned char *code, unsigned *n, unsigned at, xrcp_window *w, unsigned kind, unsigned d, unsigned s)
{
	unsigned len = approx(code + *n, kind, d, s);
	w->insn[w->count++] = (xrcp_insn){(unsigned char)(*n - at), (unsigned char)len, (unsigned char)kind, 0};
	*n += len;
}
static void put_copy(unsigned char *code, unsigned *n, unsigned at, xrcp_window *w, int lea, unsigned *lea_end)
{
	unsigned o = *n;
	if (lea) {
		int32_t disp = LEA_DISP;
		code[o] = 0x4c; code[o + 1] = 0x8d; code[o + 2] = 0x0d; memcpy(code + o + 3, &disp, 4); /* lea r9, [rip+disp] */
		*n += 7;
		*lea_end = *n;
	} else {
		code[(*n)++] = 0x90;
	}
	w->insn[w->count++] = (xrcp_insn){(unsigned char)(o - at), (unsigned char)(*n - o), XRCP_COPY, (unsigned char)(lea ? 3 : 0)};
}
static void put_movaps(unsigned char *code, unsigned *n, unsigned at, xrcp_window *w, unsigned c, unsigned r) /* movaps c, r */
{
	unsigned o = *n;
	if (c >= 8 || r >= 8) code[(*n)++] = (unsigned char)(0x40 | (c >= 8) << 2 | (r >= 8));
	code[(*n)++] = 0x0f; code[(*n)++] = 0x28; code[(*n)++] = (unsigned char)(0xc0 | (c & 7) << 3 | (r & 7));
	w->insn[w->count++] = (xrcp_insn){(unsigned char)(o - at), (unsigned char)(*n - o), XRCP_COPY, 0};
}

/* Builds function + window at code; returns the window offset and fills w. Shapes:
 * 0 [K d,s][nop]  1 [K d,s][lea]  2 [rsqrtss d,s][rcpss d,d]  3 the same as a chain
 * 4 the chain around the lea  5 [rcpss d,d as a chain end][nop]
 * 6 the copied chain [rsqrtss d,s][movaps c,d][rcpss c,d], c = copy_of(d)  7 the chain into c [rsqrtss d,s][rcpss c,d]. */
static unsigned build(unsigned char *code, unsigned variant, unsigned kind, unsigned d, unsigned s, xrcp_window *w, unsigned *lea_end)
{
	unsigned n = 0, at;
	for (unsigned r = 0; r < 16; r++) n += movups(code + n, 0, r, 7, (int32_t)(16 * r));
	code[n++] = 0x31; code[n++] = 0xc0;                         /* xor eax, eax */
	code[n++] = 0x83; code[n++] = 0xe8; code[n++] = 0x01;       /* sub eax, 1: CF SF PF AF set */
	code[n++] = 0x9f;                                           /* lahf */
	code[n++] = 0x0f; code[n++] = 0x90; code[n++] = 0xc0;       /* seto al */
	code[n++] = 0x89; code[n++] = 0xc2;                         /* mov edx, eax */
	for (unsigned k = 1; k <= 16; k++) {
		uint64_t v = 0x5a5a000000000000ULL ^ (uint64_t)k * 0x0101010101ULL;
		code[n++] = 0x48; code[n++] = 0xb8; memcpy(code + n, &v, 8); n += 8;              /* mov rax, imm64 */
		code[n++] = 0x48; code[n++] = 0x89; code[n++] = 0x44; code[n++] = 0x24;
		code[n++] = (unsigned char)(int8_t)(-8 * (int)k);                                 /* mov [rsp-8k], rax */
	}
	at = n;
	memset(w, 0, sizeof(*w));
	switch (variant) {
	case 0: put_approx(code, &n, at, w, kind, d, s); put_copy(code, &n, at, w, 0, lea_end); break;
	case 1: put_approx(code, &n, at, w, kind, d, s); put_copy(code, &n, at, w, 1, lea_end); break;
	case 2: put_approx(code, &n, at, w, XRCP_RSQRTSS, d, s); put_approx(code, &n, at, w, XRCP_RCPSS, d, d); break;
	case 3: put_approx(code, &n, at, w, XRCP_SQRT, d, s); put_approx(code, &n, at, w, XRCP_CHAIN_END, d, d); break;
	case 4:
		put_approx(code, &n, at, w, XRCP_SQRT, d, s);
		put_copy(code, &n, at, w, 1, lea_end);
		put_approx(code, &n, at, w, XRCP_CHAIN_END, d, d);
		break;
	case 6:
		put_approx(code, &n, at, w, XRCP_SQRT, d, s);
		put_movaps(code, &n, at, w, copy_of(d), d);
		put_approx(code, &n, at, w, XRCP_COPY_END, copy_of(d), d);
		break;
	case 7: put_approx(code, &n, at, w, XRCP_SQRT, d, s); put_approx(code, &n, at, w, XRCP_MOVE_END, copy_of(d), d); break;
	default: put_approx(code, &n, at, w, XRCP_CHAIN_END, d, d); put_copy(code, &n, at, w, 0, lea_end); break;
	}
	w->length = (unsigned char)(n - at);
	code[n++] = 0x9f; code[n++] = 0x0f; code[n++] = 0x90; code[n++] = 0xc0;               /* lahf; seto al */
	code[n++] = 0x49; code[n++] = 0x89; code[n++] = 0xc0;                                 /* mov r8, rax */
	for (unsigned k = 1; k <= 16; k++) {
		code[n++] = 0x48; code[n++] = 0x8b; code[n++] = 0x44; code[n++] = 0x24;
		code[n++] = (unsigned char)(int8_t)(-8 * (int)k);                                 /* mov rax, [rsp-8k] */
		n += mov_rsi(code + n, 0, (int32_t)(OUT_RED + 8 * (k - 1)));
	}
	n += mov_rsi(code + n, 2, OUT_FLAGS0);
	n += mov_rsi(code + n, 8, OUT_FLAGS1);
	n += mov_rsi(code + n, 9, OUT_R9);
	for (unsigned r = 0; r < 16; r++) n += movups(code + n, 1, r, 6, (int32_t)(OUT_XMM + 16 * r));
	code[n++] = 0xc3;
	return at;
}

static float exact_rsqrt(float x)
{
	float r;
	__asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
	volatile float one = 1.0f;
	return one / r;
}
static float exact_sqrt(float x)
{
	float r;
	__asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
	return r;
}
static float exact_rcp(float x)
{
	volatile float one = 1.0f;
	return one / x;
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float from(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static uint64_t rng = 0x9e3779b97f4a7c15ULL;
static uint32_t next32(void)
{
	rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
	return (uint32_t)(rng >> 16);
}

static unsigned synthetic(void)
{
	static const uint32_t special[] = {0x3f800000, 0x40490fdb, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff,
		0xff7fffff, 0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001, 0x7f800001, 0xbf800000,
		0x3e800000, 0x1f800000, 0x5f000000, 0x3effffff};
	const unsigned nspecial = sizeof(special) / sizeof(*special);
	unsigned char *page = mmap(NULL, 2 * 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED) { printf("no executable memory\n"); return 0; }
	unsigned char *code = page, *region = page + 65536, *tramp = region + XRCP_CONSTANTS;
	unsigned cases = 0, calls = 0;
	for (unsigned variant = 0; variant < VARIANTS; variant++)
	for (unsigned kind = XRCP_RCPSS; kind <= (variant <= 1 ? XRCP_RSQRTSS : XRCP_RCPSS); kind++)
	for (unsigned d = 0; d < 16; d++)
	for (unsigned s = 0; s < 16; s++) {
		xrcp_window w;
		unsigned lea_end = 0, used;
		memset(code, 0xcc, 4096);
		unsigned at = build(code, variant, kind, d, s, &w, &lea_end);
		unsigned char original[XRCP_MAX_WINDOW];
		memcpy(original, code + at, w.length);
		memset(region, 0xcc, 4096);
		memcpy(region, xrcp_constants, sizeof(xrcp_constants));
		if (!xrcp_emit_window(tramp, &w, original, code + at, region, DROP, &used)) { CHECK(0, "emit refused v%u k%u d%u s%u", variant, kind, d, s); continue; }
		void (*fn)(const void *, void *) = (void (*)(const void *, void *))(uintptr_t)code;
		for (unsigned v = 0; v < nspecial + 12; v++) {
			uint32_t in[64], out[OUT_SIZE / 4], approx_out[OUT_SIZE / 4];
			for (unsigned i = 0; i < 64; i++) in[i] = next32();
			uint32_t x = v < nspecial ? special[v] : next32();
			if (v >= nspecial && (v & 1)) x &= 0x7fffffff;
			in[4 * s] = x;
			/* as compiled: approximate */
			memcpy(code + at, original, w.length);
			memset(approx_out, 0, sizeof(approx_out));
			fn(in, approx_out);
			/* patched */
			memset(code + at, 0xcc, w.length);
			int32_t rel = (int32_t)((intptr_t)tramp - (intptr_t)(code + at + 5));
			code[at] = 0xe9; memcpy(code + at + 1, &rel, 4);
			memset(out, 0, sizeof(out));
			fn(in, out);
			calls += 2;
			float fx = from(x), want;
			if (variant == 2) want = exact_rcp(exact_rsqrt(fx));
			else if (variant == 3 || variant == 4) want = exact_sqrt(fx);
			else if (variant == 5) want = from(in[4 * d]);
			else if (variant == 6 || variant == 7) want = exact_rsqrt(fx); /* d keeps the one-by-one value; c gets sqrt */
			else want = kind == XRCP_RCPSS ? exact_rcp(fx) : exact_rsqrt(fx);
			const unsigned c = variant == 6 || variant == 7 ? copy_of(d) : 16;
			for (unsigned r = 0; r < 16; r++)
				for (unsigned lane = 0; lane < 4; lane++) {
					uint32_t expect = r == c && variant == 6 ? in[4 * d + lane] : in[4 * r + lane]; /* 7: c keeps its lanes 1-3 */
					if (r == d && lane == 0) expect = variant == 5 ? in[4 * d] : bits(want);
					if (r == c && lane == 0) expect = bits(exact_sqrt(fx));
					CHECK(out[4 * r + lane] == expect, "v%u k%u d%u s%u x=%08x: xmm%u[%u] %08x want %08x", variant, kind, d, s, x, r, lane,
					      out[4 * r + lane], expect);
					if (!((r == d || r == c) && lane == 0))
						CHECK(approx_out[4 * r + lane] == expect, "unpatched v%u d%u s%u: xmm%u[%u] changed", variant, d, s, r, lane);
				}
			float a = from(approx_out[4 * d]);
			/* the bound holds for normal inputs; RCPSS/RSQRTSS read subnormal inputs as zero */
			if (isfinite(want) && fabsf(fx) >= 0x1p-126f && fabsf(want) > 1e-30f && fabsf(want) < 1e30f && variant <= 1)
				CHECK(fabsf(a - want) <= fabsf(want) * 0x1.8p-12f, "unpatched v%u k%u x=%08x: %a vs %a beyond the bound", variant, kind, x, a, want);
			for (unsigned k = 0; k < 16; k++) {
				uint64_t red, expect = 0x5a5a000000000000ULL ^ (uint64_t)(k + 1) * 0x0101010101ULL;
				memcpy(&red, (unsigned char *)out + OUT_RED + 8 * k, 8);
				CHECK(red == expect, "v%u k%u d%u s%u: red zone slot %u clobbered", variant, kind, d, s, k + 1);
			}
			uint64_t f0, f1, r9;
			memcpy(&f0, (unsigned char *)out + OUT_FLAGS0, 8);
			memcpy(&f1, (unsigned char *)out + OUT_FLAGS1, 8);
			memcpy(&r9, (unsigned char *)out + OUT_R9, 8);
			CHECK((f0 & 0xffff) == (f1 & 0xffff) && (f1 & 0xd500) == 0x9500, "v%u: flags %04llx -> %04llx", variant,
			      (unsigned long long)(f0 & 0xffff), (unsigned long long)(f1 & 0xffff));
			if (lea_end)
				CHECK(r9 == (uint64_t)(uintptr_t)(code + lea_end) + LEA_DISP, "v%u lea r9 %llx", variant, (unsigned long long)r9);
		}
		cases++;
	}
	munmap(page, 2 * 65536);
	if (failures)
		return 0;
	printf("synthetic recipes: %u windows (RCPSS/RSQRTSS alone and with a RIP-relative copy, the pair one by one, the "
	       "rcp(rsqrt) chain adjacent and apart, a lone chain end, the chain through a register copy and into another register; xmm0-15 x xmm0-15), "
	       "%u calls, drop %u: exact bits, "
	       "lanes, red zone, flags and RSP kept\n", cases, calls, DROP);
	return cases;
}

static int map_elf(const char *path)
{
	int fd = open(path, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st)) return 0;
	unsigned char *file = malloc((size_t)st.st_size);
	if (!file || read(fd, file, (size_t)st.st_size) != st.st_size) return 0;
	close(fd);
	Elf64_Ehdr *eh = (Elf64_Ehdr *)file;
	Elf64_Phdr *ph = (Elf64_Phdr *)(file + eh->e_phoff);
	for (unsigned i = 0; i < eh->e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD) continue;
		uintptr_t lo = ph[i].p_vaddr & ~0xfffULL, hi = (ph[i].p_vaddr + ph[i].p_memsz + 0xfff) & ~0xfffULL;
		void *m = mmap((void *)lo, hi - lo, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (m != (void *)lo) { fprintf(stderr, "cannot map %#lx\n", (unsigned long)lo); return 0; }
		memcpy((void *)ph[i].p_vaddr, file + ph[i].p_offset, ph[i].p_filesz);
		if (ph[i].p_flags & PF_X) mprotect(m, hi - lo, PROT_READ | PROT_EXEC);
	}
	free(file);
	return 1;
}

/* The copied chains of the table (kind XRCP_COPY_END), found on the original bytes before the install:
 * the window of the rsqrtss r,s that opens each, the end of the window of its rcpss c,r. */
typedef struct { uintptr_t start, end; unsigned r, s, c; } copied_chain;
#define COPIED_MAX 32
static copied_chain copied[COPIED_MAX];
static unsigned ncopied;
static int copied_find(void)
{
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		const xrcp_window *b = &xrcp_linux_windows[i];
		for (unsigned k = 0; k < b->count; k++) {
			if (b->insn[k].kind != XRCP_COPY_END) continue;
			unsigned c, r, d, s, found = 0;
			if (!xrcp_decode((const unsigned char *)(uintptr_t)b->rva + b->insn[k].offset, b->insn[k].length, XRCP_COPY_END, &c, &r) ||
			    ncopied == COPIED_MAX)
				return 0;
			for (unsigned j = i + 1; j-- > 0 && !found;) {
				const xrcp_window *a = &xrcp_linux_windows[j];
				for (unsigned m = a->count; m-- > 0 && !found;) {
					const uintptr_t at = a->rva + a->insn[m].offset;
					if (a->insn[m].kind != XRCP_SQRT || at >= b->rva + b->insn[k].offset) continue;
					if (xrcp_decode((const unsigned char *)at, a->insn[m].length, XRCP_SQRT, &d, &s) && d == r) {
						copied[ncopied++] = (copied_chain){a->rva, b->rva + b->length, r, s, c};
						found = 1;
					}
				}
			}
			if (!found) return 0;
		}
	}
	return 1;
}
static uint64_t copied_rsp, copied_out;
/* Runs each installed copied chain on the mapped X4: an entry stub loads xmm0-15 (the source lane 0 = x), points
 * rdi, rsi and r8 at an aligned scratch buffer (the windows' copied loads), jumps to the rsqrtss window; a
 * mov r11, exit; jmp r11 written at the end of the rcpss window returns to a stub that stores xmm0-15. The copy
 * register must hold sqrtss(x) bit for bit. */
static unsigned copied_run(unsigned *inputs)
{
	static const uint32_t special[] = {0x3f800000, 0x40800000, 0x00000000, 0x80000000, 0x00000001, 0x007fffff, 0x00800000,
		0x7f7fffff, 0x7f800000, 0x7fc00000, 0xbf800000, 0x3e800000, 0x4b800001, 0x3effffff, 0x5f000000, 0x1f800000};
	const unsigned nspecial = sizeof(special) / sizeof(*special), random_inputs = 20000;
	unsigned char *page = mmap(NULL, 8192, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED) return 0;
	unsigned char *entry = page, *exitp = page + 1024, *scratch = page + 4096;
	unsigned n = 0;
	static const unsigned char pushes[] = {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
	memcpy(entry, pushes, sizeof(pushes)); n += sizeof(pushes);
	uint64_t a = (uint64_t)(uintptr_t)&copied_rsp;
	entry[n++] = 0x48; entry[n++] = 0xb8; memcpy(entry + n, &a, 8); n += 8;               /* mov rax, &copied_rsp */
	entry[n++] = 0x48; entry[n++] = 0x89; entry[n++] = 0x20;                              /* mov [rax], rsp */
	a = (uint64_t)(uintptr_t)&copied_out;
	entry[n++] = 0x48; entry[n++] = 0xb8; memcpy(entry + n, &a, 8); n += 8;               /* mov rax, &copied_out */
	entry[n++] = 0x48; entry[n++] = 0x89; entry[n++] = 0x30;                              /* mov [rax], rsi */
	static const unsigned char frame[] = {0x48, 0x83, 0xe4, 0xf0, 0x48, 0x81, 0xec, 0x00, 0x02, 0x00, 0x00}; /* and rsp,-16; sub rsp,0x200 */
	memcpy(entry + n, frame, sizeof(frame)); n += sizeof(frame);
	for (unsigned r = 0; r < 16; r++) n += movups(entry + n, 0, r, 7, (int32_t)(16 * r));
	static const unsigned char go[] = {0x49, 0x89, 0xcb, 0x48, 0x89, 0xd7, 0x48, 0x89, 0xd6, 0x49, 0x89, 0xd0, 0x41, 0xff, 0xe3};
	memcpy(entry + n, go, sizeof(go)); n += sizeof(go);                                  /* r11=rcx; rdi=rsi=r8=rdx; jmp r11 */
	n = 0;
	a = (uint64_t)(uintptr_t)&copied_out;
	exitp[n++] = 0x48; exitp[n++] = 0xb8; memcpy(exitp + n, &a, 8); n += 8;
	exitp[n++] = 0x48; exitp[n++] = 0x8b; exitp[n++] = 0x00;                              /* mov rax, [rax] */
	for (unsigned r = 0; r < 16; r++) n += movups(exitp + n, 1, r, 0, (int32_t)(16 * r));
	a = (uint64_t)(uintptr_t)&copied_rsp;
	exitp[n++] = 0x48; exitp[n++] = 0xb8; memcpy(exitp + n, &a, 8); n += 8;
	exitp[n++] = 0x48; exitp[n++] = 0x8b; exitp[n++] = 0x20;                              /* mov rsp, [rax] */
	static const unsigned char pops[] = {0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3};
	memcpy(exitp + n, pops, sizeof(pops));
	void (*fn)(const void *, void *, void *, uintptr_t) = (void (*)(const void *, void *, void *, uintptr_t))(uintptr_t)entry;
	unsigned runs = 0;
	for (unsigned i = 0; i < ncopied; i++) {
		const copied_chain *cc = &copied[i];
		unsigned char saved[13], back[13] = {0x49, 0xbb};
		uint64_t e = (uint64_t)(uintptr_t)exitp;
		memcpy(back + 2, &e, 8); back[10] = 0x41; back[11] = 0xff; back[12] = 0xe3;     /* mov r11, exit; jmp r11 */
		uintptr_t lo = cc->end & ~0xfffULL, hi = (cc->end + 13 + 0xfff) & ~0xfffULL;
		mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC);
		memcpy(saved, (void *)cc->end, 13); memcpy((void *)cc->end, back, 13);
		mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC);
		unsigned good = 0, total = 0;
		for (unsigned v = 0; v < nspecial + random_inputs; v++) {
			uint32_t in[64], out[64];
			for (unsigned k = 0; k < 64; k++) in[k] = next32();
			for (unsigned k = 0; k < 1024; k++) ((uint32_t *)scratch)[k] = next32() & 0x3fffffff;
			uint32_t x = v < nspecial ? special[v] : next32();
			if (v >= nspecial && (v & 1)) x &= 0x7fffffff;
			in[4 * cc->s] = x;
			memset(out, 0, sizeof(out));
			fn(in, out, scratch, cc->start);
			total++;
			good += out[4 * cc->c] == bits(exact_sqrt(from(x)));
			CHECK(out[4 * cc->c] == bits(exact_sqrt(from(x))), "copied chain %#lx: xmm%u[0] %08x, sqrtss(%08x) = %08x",
			      (unsigned long)cc->start, cc->c, out[4 * cc->c], x, bits(exact_sqrt(from(x))));
		}
		mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC);
		memcpy((void *)cc->end, saved, 13);
		mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC);
		printf("copied chain %#lx..%#lx (rsqrtss xmm%u,xmm%u; copy xmm%u): %u/%u inputs give sqrtss(x) bit for bit\n",
		       (unsigned long)cc->start, (unsigned long)cc->end, cc->r, cc->s, cc->c, good, total);
		runs += good == total;
		*inputs += total;
	}
	munmap(page, 8192);
	return runs;
}

/* BezierSpline::Set (0x1a45a80, to its ret; rdi the segment, rsi/rdx/rcx/r8 the 16-byte aligned control points, the
 * length at +8), whose first two distances are copied chains: on its own bytes with the install, its length must be
 * the canonical one of lockstep_spline_length.h (Linux's zero rule), the value the Windows gate redirects to. */
#define SET_VA 0x1a45a80u
#define SET_SIZE 0x14bu
#define SET_FNV 0x905d3c7d1c452fcaULL
static unsigned set_run(unsigned *zeros)
{
	typedef void (*set_fn)(void *, const float *, const float *, const float *, const float *);
	const set_fn set = (set_fn)(uintptr_t)SET_VA;
	static float seg[0x30] __attribute__((aligned(16)));
	static float p[4][4] __attribute__((aligned(16)));
	unsigned equal = 0;
	for (unsigned k = 0; k < 20000; k++) {
		for (unsigned i = 0; i < 4; i++)
			for (unsigned c = 0; c < 4; c++) p[i][c] = (float)((int32_t)(next32() % 8000001u) - 4000000) * 0.001f;
		if (k % 97 == 0) for (unsigned i = 1; i < 4; i++) memcpy(p[i], p[0], 16);
		memset(seg, 0x5a, sizeof(seg));
		set(seg, p[0], p[1], p[2], p[3]);
		uint32_t got, want = sl_stored_linux(sl_length((const unsigned char *)p));
		memcpy(&got, (unsigned char *)seg + 8, 4);
		CHECK(got == want, "Set %u: %08x, canonical %08x", k, got, want);
		equal += got == want;
		*zeros += got == SL_ONE;
	}
	return equal;
}

static uint64_t windows_digest(void)
{
	uint64_t h = 0;
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++)
		h = h * 31 + xrcp_fnv((const unsigned char *)(uintptr_t)xrcp_linux_windows[i].rva, xrcp_linux_windows[i].length);
	return h;
}

static int mapped(const char *elf, const char *lib, const char *export)
{
	if (!map_elf(elf)) return 0;
	void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
	if (!h) { printf("dlopen: %s\n", dlerror()); return 0; }
	unsigned (*install)(void) = (unsigned (*)(void))dlsym(h, "x4mp_exact_reciprocal_install");
	unsigned (*info)(const unsigned char **, size_t *, const unsigned char **) =
		(unsigned (*)(const unsigned char **, size_t *, const unsigned char **))dlsym(h, "x4mp_exact_reciprocal_region");
	int (*identity)(const Elf64_Phdr *, unsigned) = (int (*)(const Elf64_Phdr *, unsigned))dlsym(h, "x4mp_portable_math_phdrs_are_x4");
	if (!install || !info || !identity) { printf("missing exports\n"); return 0; }
	/* the constructor's executable check, on the mapped ELF's own program headers */
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)(uintptr_t)0x400000u;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)(uintptr_t)(0x400000u + eh->e_phoff);
	CHECK(identity(ph, eh->e_phnum) == 1, "the mapped X4's program headers are not recognised");
	Elf64_Phdr changed[16];
	if (eh->e_phnum > 16) { printf("too many program headers\n"); return 0; }
	memcpy(changed, ph, eh->e_phnum * sizeof(*ph));
	for (unsigned i = 0; i < eh->e_phnum; i++) if (changed[i].p_type == PT_LOAD && (changed[i].p_flags & PF_X)) changed[i].p_memsz += 16;
	CHECK(identity(changed, eh->e_phnum) == 0, "a changed text segment is recognised as X4");
	unsigned char *note = (unsigned char *)(uintptr_t)0x40030cu; /* the build ID's first byte (readelf) */
	mprotect((void *)0x400000u, 4096, PROT_READ | PROT_WRITE);
	*note ^= 1;
	CHECK(identity(ph, eh->e_phnum) == 0, "a changed build ID is recognised as X4");
	*note ^= 1;
	const unsigned char *region, *used;
	size_t size;
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++)
		CHECK(xrcp_fnv((const unsigned char *)(uintptr_t)xrcp_linux_windows[i].rva, xrcp_linux_windows[i].length) ==
		      xrcp_linux_windows[i].digest, "window %#x does not match its pin in the mapped ELF", xrcp_linux_windows[i].rva);
	CHECK(copied_find(), "a copied chain end without its rsqrtss window");
	CHECK(xrcp_fnv((const unsigned char *)(uintptr_t)SET_VA, SET_SIZE) == SET_FNV && ((const unsigned char *)(uintptr_t)SET_VA)[SET_SIZE - 1] == 0xc3,
	      "Set 0x1a45a80 does not match its pin");
	/* one changed byte: refused, nothing written */
	const xrcp_window *t = &xrcp_linux_windows[XRCP_LINUX_WINDOWS / 2];
	unsigned char *b = (unsigned char *)(uintptr_t)t->rva + 1;
	uintptr_t page = (uintptr_t)b & ~0xfffULL;
	mprotect((void *)page, 8192, PROT_READ | PROT_WRITE | PROT_EXEC);
	*b ^= 0x40;
	uint64_t before = windows_digest();
	CHECK(install() == 0, "install accepted a changed window");
	CHECK(windows_digest() == before, "refused install wrote X4 code");
	CHECK(info(&region, &size, &used) == XRCP_LINUX_WINDOWS && !region && !used, "refused install left a region");
	*b ^= 0x40;
	mprotect((void *)page, 8192, PROT_READ | PROT_EXEC);
	/* install */
	CHECK(install() == XRCP_LINUX_WINDOWS, "install refused");
	CHECK(install() == XRCP_LINUX_WINDOWS, "second install refused");
	info(&region, &size, &used);
	if (!region || !used) { printf("no region\n"); return 0; }
	CHECK((uintptr_t)region + size <= 0x400000u, "region %p not below the image", (const void *)region);
	unsigned rewritten = 0;
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		const xrcp_window *w = &xrcp_linux_windows[i];
		const unsigned char *site = (const unsigned char *)(uintptr_t)w->rva;
		int32_t rel;
		memcpy(&rel, site + 1, 4);
		CHECK(site[0] == 0xe9 && site + 5 + rel == region + XRCP_CONSTANTS + (size_t)i * XRCP_SLOT, "window %#x: jmp", w->rva);
		for (unsigned k = 5; k < w->length; k++) CHECK(site[k] == 0xcc, "window %#x: fill", w->rva);
		for (unsigned k = 0; k < w->count; k++) rewritten += w->insn[k].kind != XRCP_COPY;
	}
	CHECK(rewritten == XRCP_LINUX_REWRITTEN, "rewritten %u", rewritten);
	FILE *f = fopen(export, "wb");
	if (!f) { printf("cannot write %s\n", export); return 0; }
	uint64_t bases[2] = {0, (uint64_t)(uintptr_t)region};
	uint32_t head[2] = {XRCP_LINUX_WINDOWS, (uint32_t)size};
	fwrite("XRCP1", 1, 5, f); fwrite(bases, 8, 2, f); fwrite(head, 4, 2, f);
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		uint32_t row[3] = {xrcp_linux_windows[i].rva, (uint32_t)(XRCP_CONSTANTS + i * XRCP_SLOT), used[i]};
		fwrite(row, 4, 3, f);
	}
	fwrite(region, 1, size, f);
	if (fclose(f)) return 0;
	unsigned inputs = 0, runs = copied_run(&inputs);
	CHECK(runs == ncopied, "copied chains: %u of %u give sqrtss(x)", runs, ncopied);
	printf("installed copied chains run on the mapped X4: %u of %u give sqrtss(x) bit for bit (%u inputs)\n", runs, ncopied, inputs);
	unsigned zeros = 0, set_equal = set_run(&zeros);
	CHECK(set_equal == 20000 && zeros >= 200, "Set: %u of 20000 canonical, %u zero lengths", set_equal, zeros);
	printf("BezierSpline::Set 0x1a45a80 on its own bytes with the install: %u/20000 lengths = the canonical one "
	       "(lockstep_spline_length.h, Linux's zero rule; %u zero lengths stored 1.0)\n", set_equal, zeros);
	printf("mapped X4: executable check accepts its headers and refuses a changed text segment or build ID; "
	       "changed window refused without writes; %u windows (%u instructions) installed, jmp and fill checked, "
	       "region at %p exported\n", (unsigned)XRCP_LINUX_WINDOWS, rewritten, (const void *)region);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc != 1 && argc != 4) { fprintf(stderr, "usage: %s [<X4 ELF> <preload .so> <export>]\n", argv[0]); return 2; }
	if (!synthetic()) { printf("failures=%u\n", failures); return 1; }
	if (argc == 4 && !mapped(argv[1], argv[2], argv[3])) return 1;
	printf("failures=%u\n", failures);
	return failures ? 1 : 0;
}
