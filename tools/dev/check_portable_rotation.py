"""Offline accuracy and cross-host digest checks for the proposed rotation oracle.

The independent reference uses Decimal Gauss-Legendre pi and Decimal series,
then exact Fraction operations and a binary-search binary32 rounder for every
basis coefficient. The candidate uses Machin pi and integer fixed point.
No game or engine bytes.
The tested corpus is bounded and does not prove correctly rounded trig for all
finite binary32 values. Numeric error metrics here never relax a network hash.
"""
import argparse
from decimal import Decimal, localcontext
from fractions import Fraction
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import platform
import random
import struct
import time

import portable_rotation_reference as candidate


def decimal_word(bits):
    sign = -1 if bits >> 31 else 1
    exponent, mantissa = (bits >> 23) & 255, bits & 0x7fffff
    if exponent:
        mantissa |= 1 << 23
    return sign * Decimal(mantissa) * Decimal(2) ** (exponent - 150 if exponent else -149)


def decimal_pi():
    a, b, t, power = Decimal(1), Decimal(1) / Decimal(2).sqrt(), Decimal(1) / 4, Decimal(1)
    for _ in range(10):
        next_a = (a + b) / 2
        b = (a * b).sqrt()
        t -= power * (a - next_a) ** 2
        a, power = next_a, power * 2
    return (a + b) ** 2 / (4 * t)


def decimal_sincos(angle, pi):
    quadrant = (angle / (pi / 2)).to_integral_value(rounding='ROUND_HALF_EVEN')
    residual = angle - quadrant * (pi / 2)
    squared = residual * residual
    sine, cosine, sterm, cterm = residual, Decimal(1), residual, Decimal(1)
    for index in range(1, 256):
        sterm *= -squared / ((2 * index) * (2 * index + 1))
        cterm *= -squared / ((2 * index) * (2 * index - 1))
        next_sine, next_cosine = sine + sterm, cosine + cterm
        if (next_sine, next_cosine) == (sine, cosine):
            return ((sine, cosine), (cosine, -sine), (-sine, -cosine), (-cosine, sine))[int(quadrant) & 3]
        sine, cosine = next_sine, next_cosine
    raise ArithmeticError('Decimal reference convergence guard')


def nearest_word(value):
    # Binary64 is used only to find nearby candidates, never to choose rounding.
    # Compare exact Decimal distances to all five candidates, including both sides
    # of a possible binary64 double-rounding tie.
    sign = 0x80000000 if value.is_signed() else 0
    seed = struct.unpack('<I', struct.pack('<f', float(abs(value))))[0]
    options = range(max(0, seed - 2), min(0x3f800000, seed + 2) + 1)
    selected = min(options, key=lambda word: (abs(abs(value) - decimal_word(word)), word & 1))
    return sign | selected


@lru_cache(maxsize=8192)
def rational_word(bits):
    """Independent exact value decoder; no candidate arithmetic is used."""
    exponent = (bits >> 23) & 255
    if exponent == 255:
        raise ValueError('nonfinite oracle operand')
    if exponent == 0:
        magnitude = Fraction(bits & 0x7fffff, 1 << 149)
    else:
        magnitude = Fraction((bits & 0x7fffff) + 0x800000) * Fraction(2) ** (exponent - 150)
    return -magnitude if bits & 0x80000000 else magnitude


def rational_round(value, negative_zero=False):
    """Find adjacent binary32 values by order, compare exact Fraction distances.

    This deliberately does not reuse the candidate's significand packing,
    bit-length/exponent selection, shift rounding, or host float conversion.
    Basis operands are bounded; overflow is an oracle refusal.
    """
    sign = 0x80000000 if value < 0 or (value == 0 and negative_zero) else 0
    magnitude = abs(value)
    if magnitude > rational_word(0x7f7fffff):
        raise ArithmeticError('oracle basis overflow')
    low, high = 0, 0x7f7fffff
    while low < high:
        middle = (low + high + 1) // 2
        if rational_word(middle) <= magnitude:
            low = middle
        else:
            high = middle - 1
    if rational_word(low) == magnitude:
        return sign | low
    upper = low + 1
    below, above = magnitude - rational_word(low), rational_word(upper) - magnitude
    selected = low if below < above or (below == above and low % 2 == 0) else upper
    return sign | selected


def oracle_operation(operator, left, right=None):
    if operator == 'neg':
        rational_word(left)
        return left ^ 0x80000000
    a, b = rational_word(left), rational_word(right)
    if operator == 'mul':
        return rational_round(a * b, bool((left ^ right) & 0x80000000))
    if operator == 'add':
        return rational_round(a + b, left == right == 0x80000000)
    raise ValueError('unknown reference expression')


# Auditable expression trees define the proposed grouping, including each
# intermediate binary32 rounding. Every leaf comes from independent Decimal
# sin/cos, and every node goes through the separate Fraction/binary-search oracle.
BASIS_EXPRESSIONS = (
    (('add', ('mul', 'cr', 'cy'), ('mul', ('mul', 'sr', 'sp'), 'sy')),
     ('neg', ('mul', 'sr', 'cp')),
     ('add', ('neg', ('mul', 'cr', 'sy')), ('mul', ('mul', 'sr', 'sp'), 'cy'))),
    (('add', ('mul', 'sr', 'cy'), ('neg', ('mul', ('mul', 'cr', 'sp'), 'sy'))),
     ('mul', 'cr', 'cp'),
     ('add', ('neg', ('mul', 'sr', 'sy')), ('neg', ('mul', ('mul', 'cr', 'sp'), 'cy')))),
    (('mul', 'cp', 'sy'), 'sp', ('mul', 'cp', 'cy')),
)


def reference_basis(pitch, yaw, roll, pi):
    operands = {}
    for axis, bits in (('p', pitch), ('y', yaw), ('r', roll)):
        sine, cosine = tuple(nearest_word(value) for value in decimal_sincos(decimal_word(bits), pi))
        if bits in (0, 0x80000000):
            sine, cosine = bits, 0x3f800000
        operands['s' + axis], operands['c' + axis] = sine, cosine

    def evaluate(expression):
        if isinstance(expression, str):
            return operands[expression]
        return oracle_operation(expression[0], *(evaluate(child) for child in expression[1:]))
    return tuple(tuple(evaluate(coefficient) for coefficient in vector) for vector in BASIS_EXPRESSIONS)


def corpus(random_count):
    result = {0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x807fffff, 0x7f7fffff, 0xff7fffff}
    for exponent in range(255):
        for mantissa in (0, 1, 2, 0x3fffff, 0x400000, 0x7ffffd, 0x7ffffe, 0x7fffff):
            for sign in (0, 0x80000000):
                result.add(sign | exponent << 23 | mantissa)
    for center in (0x3f490fdb, 0x3fc90fdb, 0x40490fdb, 0x40c90fdb):
        for delta in range(-16, 17):
            result.add(center + delta)
            result.add((center + delta) | 0x80000000)
    rng = random.Random(0x5844524f54415445)
    for _ in range(random_count):
        while True:
            word = rng.getrandbits(32)
            if word & 0x7f800000 != 0x7f800000:
                result.add(word)
                break
    return sorted(result)


def run(random_count=4096):
    started = time.monotonic()
    rows, errors, vectors, decimal_rows = [], [], [], []
    # Exact midpoint decimal cases have known nearest-even answers independent
    # of the candidate's parser or either platform's native string conversion.
    with localcontext() as context:
        context.prec = 256
        rng = random.Random(0x444543494d414c)
        lower_words = {rng.randrange(0x7f7fffff) for _ in range(1024)}
        lower_words.update(exponent << 23 for exponent in range(255))
        epsilon = Decimal(1).scaleb(-200)
        for low in sorted(lower_words):
            midpoint = (decimal_word(low) + decimal_word(low + 1)) / 2
            for delta, target in ((-epsilon,low),(Decimal(0),low+(low&1)),(epsilon,low+1)):
                text = format(midpoint + delta, 'f')
                for negative in (False,True):
                    signed = '-' + text if negative else text
                    expected = target | (0x80000000 if negative else 0)
                    actual = candidate.decimal_to_binary32(signed)
                    if actual != expected:
                        raise AssertionError(f'exact decimal midpoint rounding: {signed}')
                    decimal_rows.append([signed,f'{actual:08x}'])
    with localcontext() as context:
        context.prec = 180
        pi = decimal_pi()
        for word in corpus(random_count):
            actual = candidate.sincos(word)
            expected_decimal = decimal_sincos(decimal_word(word), pi)
            expected = tuple(nearest_word(value) for value in expected_decimal)
            if word in (0, 0x80000000):
                expected = (word, 0x3f800000)
            if actual != expected:
                errors.append({'domain': 'sincos', 'input': f'{word:08x}', 'actual': list(map(hex, actual)), 'expected': list(map(hex, expected))})
            rows.append([f'{word:08x}', *[f'{value:08x}' for value in actual]])
        # An explicit common grouping is tested independently of native grouping.
        selected = (0, 0x80000000, 0x3f000000, 0x3f490fdb, 0x3fc90fdb, 0xbfc90fdb, 0x7f7fffff)
        max_norm = Decimal(0)
        max_dot = Decimal(0)
        for pitch in selected:
            for yaw in selected:
                for roll in selected:
                    matrix = candidate.basis(pitch, yaw, roll)
                    expected = reference_basis(pitch, yaw, roll, pi)
                    for vector in range(3):
                        for coefficient in range(3):
                            if matrix[vector][coefficient] != expected[vector][coefficient]:
                                errors.append({'domain': 'basis', 'inputs': [f'{value:08x}' for value in (pitch,yaw,roll)],
                                               'vector': vector, 'coefficient': coefficient,
                                               'actual': f'{matrix[vector][coefficient]:08x}',
                                               'expected': f'{expected[vector][coefficient]:08x}'})
                    values = [tuple(decimal_word(word) for word in vector) for vector in matrix]
                    max_norm = max(max_norm, *(abs(sum(x*x for x in vector)-1) for vector in values))
                    max_dot = max(max_dot, *(abs(sum(a*b for a,b in zip(values[i],values[j]))) for i,j in ((0,1),(0,2),(1,2))))
                    vectors.append([*[f'{value:08x}' for value in (pitch,yaw,roll)],
                                    *[[f'{value:08x}' for value in vector] for vector in matrix]])
        # Quality bound only for these offline samples; no tolerance in lockstep.
        if max_norm > Decimal('0.000001') or max_dot > Decimal('0.000001'):
            raise AssertionError('sampled basis quality bound')
    evidence = dict(sincos=rows, basis=vectors, decimal=decimal_rows)
    digest = hashlib.sha256(json.dumps(evidence,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return dict(scope='offline original arithmetic proposal; no native execution or runtime acceptance',
                platform=platform.platform(), inputs=len(rows), basis_cases=len(vectors), decimal_cases=len(decimal_rows), mismatch_count=len(errors),
                sincos_mismatch_count=sum(row['domain']=='sincos' for row in errors),
                basis_mismatch_count=sum(row['domain']=='basis' for row in errors),
                mismatches=errors[:20], max_norm_error=str(max_norm), max_dot_error=str(max_dot),
                corpus_sha256=digest, seconds=time.monotonic()-started,
                sources={path.name:hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
                         for path in (Path(__file__),Path(candidate.__file__))})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--random-count', type=int, default=4096)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    if not 0 <= args.random_count <= 100000:
        parser.error('random-count outside bounded range')
    report = run(args.random_count)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    if report['mismatch_count']:
        raise SystemExit('candidate differs from independent arithmetic reference')
    if args.compare:
        other = json.loads(args.compare.read_text())
        for key in ('inputs','basis_cases','decimal_cases','mismatch_count','sincos_mismatch_count','basis_mismatch_count','corpus_sha256','sources'):
            if report[key] != other[key]:
                raise SystemExit(f'cross-host {key} differs')


if __name__ == '__main__':
    main()
