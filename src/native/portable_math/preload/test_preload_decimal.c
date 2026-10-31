/* The preload's decimal-reader patch against the real Linux X4 code (Linux, PIE build).
 *   test_preload_decimal <X4 ELF> <libx4mp_portable_math.so>
 * Maps the ELF's PT_LOAD segments at their fixed addresses in this process (the file is
 * only read), runs the original readers 0x1a3b1a0/0x1a3ac40 on texts without an exponent
 * (that path calls nothing outside the reader), checks that a changed byte makes
 * x4mp_portable_decimal_install refuse without writing, installs, and checks that the
 * same entries then return portable_decimal.c's bits with the originals' end and error. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../portable_decimal.h"

typedef double (*reader64)(const char *, int, const char **, unsigned char *, double);
typedef float (*reader32)(const char *, int, const char **, unsigned char *, float);

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

int main(int argc, char **argv)
{
	static const char *texts[] = {"0.1", "123.456", "-7.25", "1 2.5", "-.5;9", "1/note\n5", "#", "5#", "", "x", "1..2",
		"9007199254740993", "0.000001234567890123456789", "3.4028235", "1.7976931348623157", "17976931348623157" "0000",
		"-0", ";", "  ;7", "12;", "0.30000000000000004;0.1"};
	const unsigned n = sizeof(texts) / sizeof(*texts);
	if (argc != 3 || !map_elf(argv[1])) return 2;
	reader64 r64 = (reader64)(uintptr_t)0x1a3b1a0;
	reader32 r32 = (reader32)(uintptr_t)0x1a3ac40;
	const char *end64[32], *end32[32];
	unsigned char err64[32], err32[32];
	double v64[32];
	float v32[32];
	for (unsigned i = 0; i < n; i++) {
		err64[i] = err32[i] = 0;
		v64[i] = r64(texts[i], ';', &end64[i], &err64[i], -99.0);
		v32[i] = r32(texts[i], ';', &end32[i], &err32[i], -99.0f);
	}
	void *lib = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
	int (*install)(void) = lib ? (int (*)(void))dlsym(lib, "x4mp_portable_decimal_install") : NULL;
	if (!install) { fprintf(stderr, "no install entry\n"); return 2; }
	unsigned char *probe = (unsigned char *)0x1a3ac40 + 0x40, keep = *probe, before[16];
	memcpy(before, (void *)0x1a3b1a0, 16);
	mprotect((void *)((uintptr_t)probe & ~0xfffULL), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
	*probe ^= 1;
	if (install() != 0 || memcmp(before, (void *)0x1a3b1a0, 16)) { puts("FAIL tampered build not refused"); return 1; }
	*probe = keep;
	if (install() != 1 || install() != 1) { puts("FAIL install"); return 1; }
	int failures = 0, differ = 0;
	for (unsigned i = 0; i < n; i++) {
		const char *e64, *e32, *w64, *w32;
		unsigned char f64 = 0, f32 = 0, g64 = 0, g32 = 0;
		double a = r64(texts[i], ';', &e64, &f64, -99.0), want = x4pd_parse64(texts[i], ';', &w64, &g64, -99.0);
		float b = r32(texts[i], ';', &e32, &f32, -99.0f), want32 = x4pd_parse32(texts[i], ';', &w32, &g32, -99.0f);
		if (memcmp(&a, &want, 8) || memcmp(&b, &want32, 4) || e64 != w64 || e32 != w32 || f64 != g64 || f32 != g32) {
			printf("FAIL redirected %u\n", i); ++failures;
		}
		if (e64 != end64[i] || f64 != err64[i] || e32 != end32[i] || f32 != err32[i] ||
		    (v64[i] == -99.0) != (a == -99.0) || (v32[i] == -99.0f) != (b == -99.0f)) {
			printf("FAIL %u end/error/outcome differ from the original\n", i); ++failures;
		}
		differ += memcmp(&a, &v64[i], 8) != 0 || memcmp(&b, &v32[i], 4) != 0;
	}
	printf("decimal readers patched in the mapped X4: %u texts, end/error/outcome as the originals, %d values "
	       "changed to the correctly rounded bits, failures=%d\n", n, differ, failures);
	return failures ? 1 : 0;
}
