#ifndef X4_PORTABLE_ROTATION_H
#define X4_PORTABLE_ROTATION_H
/* Standalone original arithmetic candidate. No engine integration or writes.
 * All numbers are IEEE binary32 bit patterns, not C float operands. */
#include <stddef.h>
#include <stdint.h>

enum x4rot_result {
    X4ROT_OK = 0,
    X4ROT_KEEP_NATIVE = 1,
    X4ROT_UNSUPPORTED = -1,
    X4ROT_OVERFLOW = -2,
    X4ROT_CAPACITY = -3
};
enum x4rot_orientation { X4ROT_ABSENT = 0, X4ROT_EULER = 1 };
struct x4rot_attribute {
    const char *text;
    size_t length;
    unsigned present;
};

/* Outputs are unchanged on any result other than OK. The caller owns all input
 * and output memory. Nonempty text must point to length readable bytes; no NUL
 * scan occurs. Missing attributes require {NULL,0,0}; explicit empty is distinct
 * and also defaults to +0. Invalid/nonfinite/overflow input never falls back. */
int x4rot_decimal(const char *text, size_t length, uint32_t *out);
int x4rot_add(uint32_t left, uint32_t right, uint32_t *out);
int x4rot_multiply(uint32_t left, uint32_t right, uint32_t *out);
int x4rot_sincos(uint32_t radians, uint32_t out[2]);
int x4rot_basis(const uint32_t radians[3], uint32_t out[9]);
int x4rot_offset(unsigned orientation, const struct x4rot_attribute angles[3], uint32_t out[9]);
/* Standalone harness verifies this generated constant against the Python oracle. */
void x4rot_half_pi_words(uint32_t out[13]);
#endif
