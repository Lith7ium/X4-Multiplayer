"""Original, offline integer arithmetic proposal; never imported by a game adapter.

The core accepts IEEE binary32 radians. A separate bounded plain-decimal parser
and degree conversion propose common input semantics; they do not emulate either
native parser. No XML ownership, native ABI, or safe engine patch site is defined.
No host float arithmetic, libm, epsilon, state copying, or special cardinal-angle
substitution is used. Finite inputs have deterministic outputs; correct rounding
of every possible trigonometric input is NOT established by the sampled tests.
"""
from functools import lru_cache
import re

FRACTION_BITS = 384
SCALE = 1 << FRACTION_BITS
DEGREES_TO_RADIANS = 0x3c8efa35
DECIMAL = re.compile(r'(-?)([0-9]+(?:\.[0-9]*)?|\.[0-9]+)', re.ASCII)


def round_ratio(numerator: int, denominator: int) -> int:
    """Round an exact signed rational to nearest integer, ties to even."""
    if denominator <= 0:
        raise ValueError('positive denominator required')
    sign = -1 if numerator < 0 else 1
    quotient, remainder = divmod(abs(numerator), denominator)
    if remainder * 2 > denominator or (remainder * 2 == denominator and quotient & 1):
        quotient += 1
    return sign * quotient


def unpack(bits: int) -> tuple[int, int]:
    """Return exact signed significand and power-of-two exponent."""
    if type(bits) is not int or not 0 <= bits <= 0xffffffff:
        raise ValueError('binary32 word required')
    exponent, mantissa = (bits >> 23) & 255, bits & 0x7fffff
    if exponent == 255:
        raise ValueError('nonfinite angle or arithmetic operand')
    if exponent:
        mantissa |= 1 << 23
    return (-mantissa if bits >> 31 else mantissa), (exponent - 150 if exponent else -149)


def pack(significand: int, exponent: int, zero_sign: int = 0) -> int:
    """Round an exact dyadic value to binary32, including gradual underflow."""
    if not significand:
        return (zero_sign & 1) << 31
    sign, magnitude = int(significand < 0), abs(significand)
    top = magnitude.bit_length() - 1 + exponent
    quantum = max(top - 23, -149)
    shift = quantum - exponent
    rounded = round_ratio(magnitude, 1 << shift) if shift > 0 else magnitude << -shift
    if not rounded:
        return sign << 31
    if rounded >= 1 << 24:
        rounded >>= 1
        quantum += 1
    if quantum > 104:
        raise ValueError('binary32 arithmetic overflow')
    if rounded < 1 << 23:
        assert quantum == -149
        return sign << 31 | rounded
    return sign << 31 | (quantum + 150) << 23 | (rounded - (1 << 23))


def add(left: int, right: int) -> int:
    a, ae = unpack(left)
    b, be = unpack(right)
    exponent = min(ae, be)
    return pack((a << (ae - exponent)) + (b << (be - exponent)), exponent,
                int(left == right == 0x80000000))


def multiply(left: int, right: int) -> int:
    a, ae = unpack(left)
    b, be = unpack(right)
    return pack(a * b, ae + be, (left ^ right) >> 31)


def negate(bits: int) -> int:
    unpack(bits)
    return bits ^ 0x80000000


def decimal_to_binary32(text: str) -> int:
    """Proposed common finite decimal rule, distinct from either native parser.

    At most 256 ASCII characters; optional minus and decimal point, at least one
    digit. This intentionally restricts the initial proposal to plain decimal.
    No partial parse, whitespace, plus, exponent, suffix, NaN/Inf, Unicode digit,
    locale, or native-parser fallback is accepted.
    Underflow rounds gradually, preserving a negative zero; overflow refuses.
    """
    if type(text) is not str or len(text) > 256:
        raise ValueError('bounded decimal text required')
    match = DECIMAL.fullmatch(text)
    if match is None:
        raise ValueError('unsupported decimal grammar')
    sign, digits = match.groups()
    integer, point, fractional = digits.partition('.')
    coefficient = int(integer + fractional)
    power = -len(fractional) if point else 0
    numerator, denominator = coefficient * 10 ** max(power, 0), 10 ** max(-power, 0)
    if not numerator:
        return 0x80000000 if sign == '-' else 0
    top = numerator.bit_length() - denominator.bit_length()
    below_power = numerator < denominator << top if top >= 0 else numerator << -top < denominator
    if below_power:
        top -= 1
    quantum = max(top - 23, -149)
    rounded = round_ratio(numerator << max(-quantum, 0), denominator << max(quantum, 0))
    return pack(-rounded if sign == '-' else rounded, quantum, int(sign == '-'))


def degrees_to_radians(text: str | None) -> int:
    """Proposed missing/empty attribute +0, exact parse, one f32 multiply."""
    degrees = 0 if text is None or text == '' else decimal_to_binary32(text)
    return multiply(degrees, DEGREES_TO_RADIANS)


def text_basis(pitch: str | None = None, yaw: str | None = None,
               roll: str | None = None) -> tuple[tuple[int, int, int], ...]:
    return basis(*(degrees_to_radians(value) for value in (pitch, yaw, roll)))


@lru_cache(maxsize=1)
def half_pi() -> int:
    """Generate pi/2 by Machin's identity; no engine or libm coefficient table."""
    def atan_inverse(denominator: int) -> int:
        total, power, index = 0, denominator, 0
        while True:
            term = round_ratio(SCALE, power * (2 * index + 1))
            if not term:
                return total
            total += -term if index & 1 else term
            power *= denominator * denominator
            index += 1
    return 8 * atan_inverse(5) - 2 * atan_inverse(239)


def sincos(bits: int) -> tuple[int, int]:
    """Binary32 radian bits -> (sin bits, cos bits), using integer fixed point."""
    value, exponent = unpack(bits)
    if not value:
        return bits, 0x3f800000
    angle = value << (exponent + FRACTION_BITS)
    divisor = half_pi()
    quadrant = round_ratio(angle, divisor)
    residual = angle - quadrant * divisor
    assert abs(residual) * 2 <= divisor
    square = residual * residual

    def series(first: int, sine: bool) -> int:
        total, term = first, first
        # |residual| <= pi/4. The bound is a refusal guard, not a convergence guess.
        for index in range(1, 128):
            divisor = (2 * index) * (2 * index + (1 if sine else -1))
            term = -round_ratio(term * square, SCALE * SCALE * divisor)
            if not term:
                return total
            total += term
        raise ArithmeticError('bounded trigonometric series did not converge')

    sine, cosine = series(residual, True), series(SCALE, False)
    pair = ((sine, cosine), (cosine, -sine), (-sine, -cosine), (-cosine, sine))[quadrant & 3]
    return tuple(pack(value, -FRACTION_BITS) for value in pair)


def basis(pitch: int, yaw: int, roll: int) -> tuple[tuple[int, int, int], ...]:
    """Proposed common grouping for three stored basis vectors, not world pose.

    Each named operation below rounds to binary32. This grouping deliberately
    defines a common arithmetic contract; arbitrary native grouping equivalence
    is not claimed. Inputs use the audited pitch/yaw/roll mathematical convention.
    """
    sp, cp = sincos(pitch)
    sy, cy = sincos(yaw)
    sr, cr = sincos(roll)
    srsp, crsp = multiply(sr, sp), multiply(cr, sp)
    return (
        (add(multiply(cr, cy), multiply(srsp, sy)),
         negate(multiply(sr, cp)),
         add(negate(multiply(cr, sy)), multiply(srsp, cy))),
        (add(multiply(sr, cy), negate(multiply(crsp, sy))),
         multiply(cr, cp),
         add(negate(multiply(sr, sy)), negate(multiply(crsp, cy)))),
        (multiply(cp, sy), sp, multiply(cp, cy)),
    )
