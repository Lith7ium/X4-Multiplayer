/* libx4mp_portable_math.so: LD_PRELOAD interposition of the 28 transcendental libm
 * symbols the Linux X4 imports, so the Linux build computes with the same library
 * as the Windows gate's native_gate_portable_math (src/native/portable_math).
 *
 * Opt-in: nothing changes unless this object is preloaded (the dedicated server's
 * "portable_math" key, or LD_PRELOAD by hand). The X4 ELF (9.00, build 611726)
 * reaches libm only through its PLT (2,276 calls) and 13 GLOB_DAT slots, all bound
 * by the dynamic linker to the first definition in the global scope, which is
 * this object; there is no IRELATIVE or inlined transcendental in the ELF and
 * glibc's ifunc variants are never consulted. Versioned references such as
 * exp@GLIBC_2.29 bind to these unversioned definitions.
 *
 * Opt-in as well, X4MP_PORTABLE_DECIMAL=1: X4's saved-value readers go to
 * portable_decimal.c, as the Windows gate's native_gate_decimal does (below).
 * And X4MP_EXACT_RECIPROCAL=1: X4's RCPSS/RSQRTSS become exactly rounded DIVSS/SQRTSS
 * through trampolines, as the Windows gate's native_gate_exact_reciprocal does (below).
 *
 * Fail closed: the constructor checks the library's known-answer digest (and, when an
 * engine patch is asked for, the executable's GNU build ID and text segment, then the
 * patch's pins) and stops the process (exit 97) on a mismatch, so a
 * miscompiled build or another X4 build cannot run a session with different
 * arithmetic. X4MP_PORTABLE_MATH_LOG=<file> appends one line with the version,
 * digest, reader and reciprocal state and executable.
 * Build: tools/dev/build_portable_math_preload.py (zig cc x86_64-linux-gnu or cc). */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../portable_math.h"

#define EXPORT __attribute__((visibility("default")))

EXPORT double sin(double x) { return x4pm_sin(x); }
EXPORT double cos(double x) { return x4pm_cos(x); }
EXPORT double tan(double x) { return x4pm_tan(x); }
EXPORT void sincos(double x, double *s, double *c) { x4pm_sincos(x, s, c); }
EXPORT double asin(double x) { return x4pm_asin(x); }
EXPORT double acos(double x) { return x4pm_acos(x); }
EXPORT double atan(double x) { return x4pm_atan(x); }
EXPORT double atan2(double y, double x) { return x4pm_atan2(y, x); }
EXPORT double exp(double x) { return x4pm_exp(x); }
EXPORT double exp2(double x) { return x4pm_exp2(x); }
EXPORT double log(double x) { return x4pm_log(x); }
EXPORT double log2(double x) { return x4pm_log2(x); }
EXPORT double log10(double x) { return x4pm_log10(x); }
EXPORT double pow(double x, double y) { return x4pm_pow(x, y); }
EXPORT float sinf(float x) { return x4pm_sinf(x); }
EXPORT float cosf(float x) { return x4pm_cosf(x); }
EXPORT float tanf(float x) { return x4pm_tanf(x); }
EXPORT void sincosf(float x, float *s, float *c) { x4pm_sincosf(x, s, c); }
EXPORT float asinf(float x) { return x4pm_asinf(x); }
EXPORT float acosf(float x) { return x4pm_acosf(x); }
EXPORT float atanf(float x) { return x4pm_atanf(x); }
EXPORT float atan2f(float y, float x) { return x4pm_atan2f(y, x); }
EXPORT float expf(float x) { return x4pm_expf(x); }
EXPORT float exp2f(float x) { return x4pm_exp2f(x); }
EXPORT float logf(float x) { return x4pm_logf(x); }
EXPORT float log2f(float x) { return x4pm_log2f(x); }
EXPORT float log10f(float x) { return x4pm_log10f(x); }
EXPORT float powf(float x, float y) { return x4pm_powf(x, y); }

/* ---- the saved-value readers (opt-in: X4MP_PORTABLE_DECIMAL=1) ----
 * Linux X4 0x1a3b1a0 (binary64) and 0x1a3ac40 (binary32) read the numbers of a save
 * with digit accumulation, scale /= 10 and glibc pow; the Windows gate's
 * native_gate_decimal sends X4.exe's pair to the same src/native/portable_math/
 * portable_decimal.c, so both builds read the same correctly rounded bits. SysV ABI:
 * rdi text, esi separator, rdx end (char **, may be NULL), rcx error flag (bool *, may
 * be NULL), xmm0 fallback. Each entry is pinned by an FNV-1a 64 digest of its whole
 * body and overwritten with jmp [rip]; target (14 bytes; no branch in X4 enters those
 * bytes). Installed from the constructor, before main and any X4 thread. */
#include <stdint.h>
#include <sys/mman.h>
#include "../portable_decimal.h"
static double x4mp_read64(const char *text, int separator, const char **end, unsigned char *error, double fallback)
{
	return x4pd_parse64(text, (unsigned char)separator, end, error, fallback);
}
static float x4mp_read32(const char *text, int separator, const char **end, unsigned char *error, float fallback)
{
	return x4pd_parse32(text, (unsigned char)separator, end, error, fallback);
}
static const struct { uintptr_t at; size_t size; uint64_t digest; void *target; } x4mp_readers[2] = {
	{0x1a3b1a0u, 0x39fu, 0x352c9de754c3579dULL, (void *)x4mp_read64},
	{0x1a3ac40u, 0x3bfu, 0x2f7e4e35aa7fc74fULL, (void *)x4mp_read32},
};
static uint64_t x4mp_fnv(const unsigned char *p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}
/* 1 installed (or already), 0 refused (a pin differs or memory is not writable). */
EXPORT int x4mp_portable_decimal_install(void)
{
	long page = sysconf(_SC_PAGESIZE);
	for (unsigned i = 0; i < 2; i++) {
		const unsigned char *at = (const unsigned char *)x4mp_readers[i].at;
		unsigned char jump[14] = {0xff, 0x25, 0, 0, 0, 0};
		memcpy(jump + 6, &x4mp_readers[i].target, 8);
		if (!memcmp(at, jump, 14))
			continue;
		if (x4mp_fnv(at, x4mp_readers[i].size) != x4mp_readers[i].digest)
			return 0;
	}
	for (unsigned i = 0; i < 2; i++) {
		unsigned char *at = (unsigned char *)x4mp_readers[i].at;
		unsigned char jump[14] = {0xff, 0x25, 0, 0, 0, 0};
		memcpy(jump + 6, &x4mp_readers[i].target, 8);
		if (!memcmp(at, jump, 14))
			continue;
		uintptr_t lo = (uintptr_t)at & ~(uintptr_t)(page - 1), hi = ((uintptr_t)at + 14 + page - 1) & ~(uintptr_t)(page - 1);
		if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC))
			return 0;
		memcpy(at, jump, 14);
		__builtin___clear_cache((char *)at, (char *)at + 14);
		if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC))
			return 0;
	}
	return 1;
}

/* ---- RCPSS/RSQRTSS to exact arithmetic (opt-in: X4MP_EXACT_RECIPROCAL=1) ----
 * RCPSS and RSQRTSS only have an architectural error bound, so Intel and AMD CPUs give
 * different bits (LOCKSTEP-CROSSPLATFORM-FP-INVENTORY-20261029.txt, class A). All 1,185
 * of them in Linux X4 9.00 (exact_reciprocal_linux_sites.h, tools/dev/
 * gen_exact_reciprocal.py --linux) are rewritten as on Windows:
 *   rcpss d, s -> d[0] = 1.0f / s[0];  rsqrtss d, s -> d[0] = 1.0f / sqrt(s[0])
 *   rsqrtss d, s ... rcpss d, d -> d[0] = sqrt(s[0])   (462 such chains)
 * lanes 1-3 of d kept, flags untouched (../exact_reciprocal_emit.h). Each window of whole
 * instructions is pinned by an FNV-1a 64 digest and becomes jmp rel32 (rest int3) to a
 * trampoline in a region mapped below the image (within 2 GiB); the trampoline keeps the
 * value 144 bytes below RSP, beyond the System V red zone. Windows do not cover the
 * x4mp-agent's pinned sites or the decimal readers. Installed from the constructor,
 * before main and any X4 thread; there is no restore (the process ends with it). */
#include "../exact_reciprocal_emit.h"
#include "exact_reciprocal_linux_sites.h"
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define XRCP_LINUX_DROP 144u
static unsigned char *xrcp_linux_region;
static size_t xrcp_linux_region_size;
static unsigned char xrcp_linux_used[XRCP_LINUX_WINDOWS];
static int xrcp_linux_done;

static unsigned char *xrcp_linux_try(uintptr_t at, size_t size)
{
	uintptr_t first = xrcp_linux_windows[0].rva, last = xrcp_linux_windows[XRCP_LINUX_WINDOWS - 1].rva;
	void *p = mmap((void *)at, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (p == MAP_FAILED)
		return NULL;
	intptr_t low = (intptr_t)p - (intptr_t)first, high = (intptr_t)p + (intptr_t)size - (intptr_t)last;
	if (p != (void *)at || low < INT32_MIN / 2 || low > INT32_MAX / 2 || high < INT32_MIN / 2 || high > INT32_MAX / 2) {
		munmap(p, size);
		return NULL;
	}
	return p;
}
/* Below the image first (an ET_EXEC at 0x400000 leaves 0x10000-0x400000 unused), else
 * 1-1.75 GiB above the first window. */
static unsigned char *xrcp_linux_alloc(size_t size)
{
	uintptr_t first = xrcp_linux_windows[0].rva, last = xrcp_linux_windows[XRCP_LINUX_WINDOWS - 1].rva;
	unsigned char *p;
	for (uintptr_t at = (0x400000u - size) & ~(uintptr_t)0xffff; at >= 0x10000 && at + size <= 0x400000u; at -= 0x10000)
		if ((p = xrcp_linux_try(at, size)))
			return p;
	for (uintptr_t at = (last + 0x40000000u) & ~(uintptr_t)0xffff; at < first + 0x70000000u; at += 0x10000)
		if ((p = xrcp_linux_try(at, size)))
			return p;
	return NULL;
}
/* mprotect the pages of every window (windows are sorted). */
static int xrcp_linux_protect(uintptr_t page, int prot)
{
	uintptr_t done = 0;
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		uintptr_t lo = xrcp_linux_windows[i].rva & ~(page - 1);
		uintptr_t hi = (xrcp_linux_windows[i].rva + xrcp_linux_windows[i].length + page - 1) & ~(page - 1);
		if (lo < done)
			lo = done;
		if (lo < hi && mprotect((void *)lo, hi - lo, prot))
			return 0;
		if (hi > done)
			done = hi;
	}
	return 1;
}
/* XRCP_LINUX_WINDOWS when installed (also when it already was), 0 when refused: a window
 * differs from its pin, no region near the image, or no write access; nothing in X4 is
 * written then. */
EXPORT unsigned x4mp_exact_reciprocal_install(void)
{
	if (xrcp_linux_done)
		return XRCP_LINUX_WINDOWS;
	uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		const xrcp_window *w = &xrcp_linux_windows[i];
		if (w->length < 5 || w->length > XRCP_MAX_WINDOW || (i && w->rva < xrcp_linux_windows[i - 1].rva + xrcp_linux_windows[i - 1].length))
			return 0;
		if (xrcp_fnv((const unsigned char *)(uintptr_t)w->rva, w->length) != w->digest)
			return 0;
	}
	size_t size = (XRCP_CONSTANTS + (size_t)XRCP_LINUX_WINDOWS * XRCP_SLOT + 0xffff) & ~(size_t)0xffff;
	unsigned char *region = xrcp_linux_alloc(size);
	if (!region)
		return 0;
	memset(region, 0xcc, size);
	memcpy(region, xrcp_constants, sizeof(xrcp_constants));
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		const xrcp_window *w = &xrcp_linux_windows[i];
		const unsigned char *site = (const unsigned char *)(uintptr_t)w->rva;
		unsigned used;
		if (!xrcp_emit_window(region + XRCP_CONSTANTS + (size_t)i * XRCP_SLOT, w, site, site, region, XRCP_LINUX_DROP, &used)) {
			munmap(region, size);
			return 0;
		}
		xrcp_linux_used[i] = (unsigned char)used;
	}
	if (mprotect(region, size, PROT_READ | PROT_EXEC)) {
		munmap(region, size);
		return 0;
	}
	if (!xrcp_linux_protect(page, PROT_READ | PROT_WRITE | PROT_EXEC)) {
		xrcp_linux_protect(page, PROT_READ | PROT_EXEC);
		munmap(region, size);
		return 0;
	}
	for (unsigned i = 0; i < XRCP_LINUX_WINDOWS; i++) {
		const xrcp_window *w = &xrcp_linux_windows[i];
		unsigned char *site = (unsigned char *)(uintptr_t)w->rva;
		int32_t rel = (int32_t)((intptr_t)(region + XRCP_CONSTANTS + (size_t)i * XRCP_SLOT) - (intptr_t)(site + 5));
		memset(site + 5, 0xcc, w->length - 5u);
		memcpy(site + 1, &rel, 4);
		site[0] = 0xe9;
		__builtin___clear_cache((char *)site, (char *)site + w->length);
	}
	xrcp_linux_protect(page, PROT_READ | PROT_EXEC);
	xrcp_linux_region = region;
	xrcp_linux_region_size = size;
	xrcp_linux_done = 1;
	return XRCP_LINUX_WINDOWS;
}
/* For tests: the trampoline region, its size and the bytes used per window (NULL before
 * the install); returns the number of windows. */
EXPORT unsigned x4mp_exact_reciprocal_region(const unsigned char **region, size_t *size, const unsigned char **used)
{
	*region = xrcp_linux_region;
	*size = xrcp_linux_region_size;
	*used = xrcp_linux_done ? xrcp_linux_used : NULL;
	return XRCP_LINUX_WINDOWS;
}

/* For tests and diagnostics: version, digest, and the number of interposed symbols. */
EXPORT unsigned x4mp_portable_math_info(uint64_t *kat)
{
	if (kat)
		*kat = x4pm_kat();
	return X4PM_VERSION << 16 | 28u;
}

static void x4mp_portable_math_log(const char *text)
{
	const char *path = getenv("X4MP_PORTABLE_MATH_LOG");
	if (!path || !*path)
		return;
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0)
		return;
	ssize_t ignored = write(fd, text, strlen(text));
	(void)ignored;
	close(fd);
}

/* The engine patches only run in the pinned executable: the first object of the
 * process is an ET_EXEC (load bias 0) with X4 9.00's text segment and GNU build ID,
 * checked before any pinned address is read. */
#include <link.h>
static const unsigned char x4mp_build_id[20] = {0x77, 0xcf, 0x7a, 0xc0, 0xff, 0x32, 0xd8, 0x8f, 0xfd, 0x3d,
	0xb7, 0xeb, 0x6e, 0xa2, 0xe0, 0x25, 0xae, 0xcb, 0x71, 0x1d};
/* 1 when these program headers (of an object loaded at bias 0) are X4 9.00's. */
EXPORT int x4mp_portable_math_phdrs_are_x4(const ElfW(Phdr) *phdr, unsigned count)
{
	int text = 0, id = 0;
	for (unsigned i = 0; i < count; i++) {
		const ElfW(Phdr) *ph = &phdr[i];
		if (ph->p_type == PT_LOAD && (ph->p_flags & PF_X) && ph->p_vaddr == 0x656000u && ph->p_memsz == 0x261d0a9u)
			text = 1;
		if (ph->p_type != PT_NOTE)
			continue;
		const unsigned char *n = (const unsigned char *)(uintptr_t)ph->p_vaddr, *end = n + ph->p_memsz;
		while (n + 12 <= end) {
			uint32_t namesz, descsz, type;
			memcpy(&namesz, n, 4);
			memcpy(&descsz, n + 4, 4);
			memcpy(&type, n + 8, 4);
			const unsigned char *name = n + 12, *desc = name + ((namesz + 3u) & ~3u);
			if (desc + descsz > end)
				break;
			if (type == 3 && namesz == 4 && !memcmp(name, "GNU", 4) && descsz == 20 && !memcmp(desc, x4mp_build_id, 20))
				id = 1;
			n = desc + ((descsz + 3u) & ~3u);
		}
	}
	return text && id;
}
static int x4mp_main_phdr(struct dl_phdr_info *info, size_t size, void *data)
{
	(void)size;
	*(int *)data = info->dlpi_addr == 0 && x4mp_portable_math_phdrs_are_x4(info->dlpi_phdr, info->dlpi_phnum);
	return 1; /* the first object is the executable */
}
static int x4mp_main_is_x4(void)
{
	int ok = 0;
	dl_iterate_phdr(x4mp_main_phdr, &ok);
	return ok;
}

static void x4mp_refuse(const char *why, const char *exe)
{
	char line[4352];
	snprintf(line, sizeof(line), "X4MP_PORTABLE_MATH REFUSED %s exe=%s\n", why, exe);
	x4mp_portable_math_log(line);
	ssize_t ignored = write(2, line, strlen(line));
	(void)ignored;
	_exit(97);
}

static int x4mp_flag(const char *name)
{
	const char *value = getenv(name);
	return value && !strcmp(value, "1");
}

__attribute__((constructor)) static void x4mp_portable_math_init(void)
{
	char line[4352], exe[4096], why[96];
	uint64_t kat = x4pm_kat();
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	exe[n > 0 ? n : 0] = 0;
	if (kat != X4PM_KAT_EXPECTED) {
		snprintf(why, sizeof(why), "kat=%016llx expected=%016llx", (unsigned long long)kat, (unsigned long long)X4PM_KAT_EXPECTED);
		x4mp_refuse(why, exe);
	}
	int readers = x4mp_flag("X4MP_PORTABLE_DECIMAL"), reciprocal = x4mp_flag("X4MP_EXACT_RECIPROCAL");
	if ((readers || reciprocal) && !x4mp_main_is_x4())
		x4mp_refuse("engine patches requested but the executable is not the pinned Linux X4 9.00 (build ID 77cf7ac0...)", exe);
	if (readers && !x4mp_portable_decimal_install())
		x4mp_refuse("decimal readers differ from the pinned X4 9.00 build", exe);
	unsigned windows = 0;
	if (reciprocal && !(windows = x4mp_exact_reciprocal_install()))
		x4mp_refuse("exact reciprocal windows differ from the pinned X4 9.00 build or cannot be patched", exe);
	/* The Linux X4 never writes MXCSR and its threads inherit their creator's, so the
	 * value here (0x1f80 expected) is what the simulation threads run with unless a
	 * library changes it; the Windows gate's native_gate_fp_env 'default' sets 0x1f80. */
	unsigned mxcsr = __builtin_ia32_stmxcsr() & 0xffc0u; /* control bits; the status flags are history */
	snprintf(line, sizeof(line), "X4MP_PORTABLE_MATH ACTIVE version=%u kat=%016llx symbols=28 decimal=%d "
		"exact_reciprocal=%u/%u mxcsr_control=%04x pid=%ld exe=%s\n", X4PM_VERSION, (unsigned long long)kat, readers,
		windows ? (unsigned)XRCP_LINUX_REWRITTEN : 0u, (unsigned)XRCP_LINUX_REWRITTEN, mxcsr, (long)getpid(), exe);
	x4mp_portable_math_log(line);
}
