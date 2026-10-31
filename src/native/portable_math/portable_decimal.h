/* X4 Multiplayer portable decimal reader: the number syntax of X4's saved-value
 * readers (Windows X4.exe 0x14c3090 binary64 / 0x14c2bd0 binary32, Linux X4
 * 0x1a3b1a0 / 0x1a3ac40), with the value rounded correctly once from the decimal
 * text instead of the engines' floating-point accumulation (Windows scale *= 0.1
 * and the CRT pow, Linux scale /= 10 and glibc pow, which give different bits).
 * Integer arithmetic only: the result is the same on every CPU, compiler and MXCSR.
 *
 * Syntax (both engines, reproduced here): a number ends at NUL or at the separator
 * character. Space, tab, CR and LF are skipped anywhere; '/' starts a comment to
 * the end of the line; one '-' may precede the first digit; one '.'; digits
 * '0'-'9'; then optionally 'e' or 'E' and an exponent that is a 32-bit integer
 * (wrapping) of decimal digits or, after '%', binary digits, with one '+' or '-'
 * before its digits, a trailing '.' followed only by zeros allowed; '#' before any
 * digit makes the number (or the exponent) zero up to the separator. Any other
 * character is an error.
 * Results: a number -> the correctly rounded value (ties to even, overflow to
 * infinity, gradual underflow, sign kept on zero); no digit, a leading separator
 * or a leading '#' -> +0 (error flag set, except for '#'); an error -> fallback.
 * end (optional) receives the engines' end position: after the separator, at the
 * NUL, or at the offending character; error (optional) is set to 1 on errors, on
 * a missing number and on a NULL text (end untouched then). */
#ifndef X4_PORTABLE_DECIMAL_H
#define X4_PORTABLE_DECIMAL_H

#ifndef X4PD_API
#define X4PD_API
#endif

X4PD_API double x4pd_parse64(const char *text, unsigned char separator, const char **end,
                             unsigned char *error, double fallback);
X4PD_API float x4pd_parse32(const char *text, unsigned char separator, const char **end,
                            unsigned char *error, float fallback);

#endif
