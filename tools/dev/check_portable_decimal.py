"""Check src/native/portable_math/portable_decimal.c: correct rounding, identical builds,
and the same syntax as the four engine readers it replaces.

1. A deterministic corpus (saved-value shapes, exponents, comments, '#', '%', separators,
   junk, huge and tiny magnitudes, long digit strings) goes through the C harness built
   with Zig for Windows and, with --wsl/--cc, with GCC for Linux: the outputs must be
   identical, and every value must equal the correctly rounded binary64/binary32 of an
   independent Python model of the syntax (exact Fraction arithmetic).
2. With --windows <X4.exe> and/or --linux <X4>, the original readers (Windows 0x14c3090,
   0x14c2bd0 for ';'; Linux 0x1a3b1a0, 0x1a3ac40 with separator, end and error) run in
   unicorn on the same corpus. Every case must agree on the outcome (number, zero or the
   fallback), and on Linux also on the end position and the error flag; numbers must agree
   within 1e-9 relative (the originals are not correctly rounded, which is the point).
   The executables are only read. Needs unicorn and capstone for part 2.

  python tools/dev/check_portable_decimal.py --zig <zig> [--wsl Ubuntu-24.04]
         [--windows X4.exe] [--linux X4] [--count 20000]
"""
import argparse
from fractions import Fraction
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/portable_math'
OUT = ROOT / '.local/portable-math/decimal'
FB64, FB32 = 0x7ff00000000dead1, 0x7f80dea1
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']


# ---- independent model of the syntax ----
def model(text, sep):
    """(kind, negative, digits, e10, end, error): kind 'value', 'zero' or 'fallback'."""
    if text is None:
        return 'fallback', 0, '', 0, None, 1
    s = text + b'\0'
    pos, end, moved, digits, point, neg = 0, 0, False, False, False, False
    mant, frac = [], 0

    def at(i):
        return s[i] if i < len(s) else 0
    c = at(pos)
    if c == sep:
        return 'zero', 0, '', 0, pos, 1
    while True:
        if c == 0:
            if moved: end = pos
            if not digits: return 'zero', 0, '', 0, pos, 1
            return 'value', neg, ''.join(mant), -frac, end, 0
        if c in b'eE':
            if moved: end = pos
            if not digits: return 'zero', 0, '', 0, pos, 1
            break
        if c in b' \t\n\r':
            pos += 1
        elif c == ord('#'):
            if moved: end = pos
            if digits: return 'fallback', 0, '', 0, pos, 1
            q = end + 1
            while at(q) != sep and at(q) != 0: q += 1
            return 'zero', 0, '', 0, q + 1 if at(q) else q, 0
        elif c == ord('/'):
            while at(pos) not in (0, 10): pos += 1
        elif c == ord('.'):
            if point: return 'fallback', 0, '', 0, pos, 1
            point = True; pos += 1
        elif c == ord('-'):
            if digits or neg: return 'fallback', 0, '', 0, pos, 1
            neg = True; pos += 1
        elif 48 <= c <= 57:
            mant.append(chr(c)); frac += point; digits = True; pos += 1
        else:
            return 'fallback', 0, '', 0, pos, 1
        moved = True
        c = at(pos)
        if c == sep:
            end = pos
            if not digits: return 'zero', 0, '', 0, pos, 1
            if (sep & 0xdf) == ord('E'): break
            if sep: end = pos + 1
            return 'value', neg, ''.join(mant), -frac, end, 0
    # exponent at pos (the 'e' or an 'e' separator)
    pos += 1
    v, eneg, sign, edig, base = 0, False, False, False, 10
    c = at(pos)
    if c == sep:
        return 'fallback', 0, '', 0, pos, 1
    while True:
        if c == 0:
            if not edig: return 'fallback', 0, '', 0, pos, 1
            end = pos; break
        if c in b' \t\n\r':
            pos += 1; c = at(pos)
        elif c == ord('/'):
            while at(pos) not in (0, 10): pos += 1
            c = at(pos)
        elif c == ord('.'):
            pos += 1
            while at(pos) == ord('0'): pos += 1
            c = at(pos)
            if c != sep and c != 0: return 'fallback', 0, '', 0, pos, 1
        elif c in b'+-':
            if edig or sign: return 'fallback', 0, '', 0, pos, 1
            eneg = c == ord('-'); sign = True; pos += 1; c = at(pos)
        elif c == ord('%'):
            if edig: return 'fallback', 0, '', 0, pos, 1
            base = 2; pos += 1; c = at(pos)
        elif c == ord('#'):
            if edig: return 'fallback', 0, '', 0, pos, 1
            pos += 1
            while at(pos) != sep and at(pos) != 0: pos += 1
            return 'value', neg, ''.join(mant), -frac, pos + 1 if at(pos) else pos, 0
        elif base == 2:
            if sign or c not in b'01': return 'fallback', 0, '', 0, pos, 1
            v = (v * 2 + c - 48) & 0xffffffff; edig = True; pos += 1; c = at(pos)
        else:
            if not 48 <= c <= 57: return 'fallback', 0, '', 0, pos, 1
            v = (v * 10 + c - 48) & 0xffffffff; edig = True; pos += 1; c = at(pos)
        if c == sep:
            if not edig: return 'fallback', 0, '', 0, pos, 1
            end = pos + 1 if c else pos
            break
    if eneg: v = (-v) & 0xffffffff
    if v >= 1 << 31: v -= 1 << 32
    return 'value', neg, ''.join(mant), v - frac, end, 0


def round_bits(neg, digits, e10, p, emin, emax):
    digits = digits.lstrip('0')
    sign = (1 << (p + (8 if p == 24 else 11) - 1)) if neg else 0
    inf = ((1 << (8 if p == 24 else 11)) - 1) << (p - 1)
    if not digits:
        return sign
    mag = len(digits) + e10
    if mag - 1 > (39 if p == 24 else 309):
        return sign | inf
    if mag < (-46 if p == 24 else -325):
        return sign
    v = Fraction(int(digits)) * (Fraction(10) ** e10)
    e = v.numerator.bit_length() - v.denominator.bit_length()
    if Fraction(2) ** e > v: e -= 1
    lsb = max(e, emin) - p + 1
    q = v / Fraction(2) ** lsb
    m = q.numerator // q.denominator
    rem = q - m
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and m & 1):
        m += 1
    if m >> p:
        m >>= 1; lsb += 1
    if m >= 1 << (p - 1):
        exponent = lsb + p - 1
        if exponent > emax:
            return sign | inf
        return sign | (exponent + emax) << (p - 1) | (m - (1 << (p - 1)))
    return sign | m


# ---- corpus ----
def corpus(count, seed=0x58344443):
    rng = random.Random(seed)
    fixed = [b'', b'0', b'-0', b'1', b'-1', b'0.1', b'.5', b'5.', b'-.5', b'123.456', b'1e5', b'1E-5', b'1e+5',
             b'1e400', b'-1e400', b'1e-400', b'2.4703282292062327e-324', b'2.4703282292062328e-324',
             b'4.9406564584124654e-324', b'1.7976931348623157e308', b'1.7976931348623158e308',
             b'1.401298464324817e-45', b'7.006492321624085e-46', b'3.4028235e38', b'3.4028236e38',
             b'0.30000000000000004', b'9007199254740993', b'#', b'#5', b'5#', b'1e#', b'1e#;2', b'-', b'.', b'..5',
             b'1..5', b'--1', b'1-', b'+1', b'1e', b'1e;', b'e5', b'1 2 3', b'1/x\n2', b'/c', b'1e%101', b'1e-%101',
             b'1e%2', b'1e5.000', b'1e5.01', b'1e 5', b'1e+-5', b'1e5;7', b'12;34', b';1', b'0x10', b'1,5',
             b'\x80', b'1\x80', b'1e2147483647', b'1e2147483648', b'1e-2147483648', b'1e4294967296',
             b'0.' + b'0' * 400 + b'1', b'1' + b'0' * 400, b'9' * 900, b'0.' + b'9' * 900 + b'e-300',
             b'1.000000000000000000000000000000000000001', b'7.0e-46', b'7.00649232162408535461864791644958065640e-46']
    for t in fixed:
        yield ord(';'), t
    alphabet = b'0123456789' * 6 + b'.-+eE ;#/%\n\t\r,x1' + b'0123456789'
    for i in range(count):
        kind = rng.random()
        if kind < 0.45:   # saved-value shapes
            ip = str(rng.randrange(0, 10 ** rng.randrange(1, 12)))
            fp = ''.join(rng.choice('0123456789') for _ in range(rng.randrange(0, 18)))
            t = ('-' if rng.random() < 0.3 else '') + ip + ('.' + fp if fp or rng.random() < 0.2 else '')
            if rng.random() < 0.25:
                t += rng.choice('eE') + rng.choice(['', '+', '-']) + str(rng.randrange(0, 330))
            if rng.random() < 0.15:
                t += ';' + str(rng.randrange(100))
            text = t.encode()
        elif kind < 0.6:  # extreme magnitudes
            text = ('%d.%de%d' % (rng.randrange(1, 10), rng.randrange(10 ** 16), rng.randrange(-360, 360))).encode()
        else:             # grammar fuzz
            text = bytes(rng.choice(alphabet) for _ in range(rng.randrange(0, 14)))
        sep = ord(';') if rng.random() < 0.85 else rng.choice([0, ord(','), ord('e'), ord(' '), ord('1')])
        yield sep, text


# ---- C harness ----
def build(args):
    OUT.mkdir(parents=True, exist_ok=True)
    sources = [str(SOURCE / 'portable_decimal.c'), str(SOURCE / 'test_portable_decimal.c')]
    runs = []
    if args.zig:
        target = 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-gnu'
        exe = OUT / ('tpd-zig' + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([str(args.zig), 'cc', '-target', target, *FLAGS, *sources, '-o', str(exe)], check=True)
        runs.append(('zig-' + target, [str(exe)]))
    if args.wsl:
        def wsl_path(p):
            text = str(Path(p).resolve())
            return '/mnt/' + text[0].lower() + text[2:].replace('\\', '/')
        exe = OUT / 'tpd-gcc'
        subprocess.run(['wsl', '-d', args.wsl, '--', 'gcc', *FLAGS, *map(wsl_path, sources), '-o', wsl_path(exe)], check=True)
        runs.append(('wsl-gcc', ['wsl', '-d', args.wsl, '--', wsl_path(exe)]))
    elif args.cc:
        exe = OUT / 'tpd-cc'
        subprocess.run([args.cc, *FLAGS, *sources, '-o', str(exe)], check=True)
        runs.append(('cc', [str(exe)]))
    return runs


def run_harness(command, cases):
    stdin = ''.join('%02x %s\n' % (sep, text.hex()) for sep, text in cases)
    out = subprocess.run(command, input=stdin, capture_output=True, text=True, check=True).stdout.splitlines()
    rows = []
    for line in out:
        a, b, c, d, e, f = line.split()
        rows.append((int(a, 16), int(b), int(c), int(d, 16), int(e), int(f)))
    return rows


# ---- the original readers in unicorn ----
class Original:
    STACK, TEXT, SENTINEL = 0x7f0000000000, 0x7e0000000000, 0x7d0000000000

    def __init__(self):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
        self.mu = Uc(UC_ARCH_X86, UC_MODE_64)
        self.mu.mem_map(self.STACK - 0x100000, 0x200000)
        self.mu.mem_map(self.TEXT, 0x100000)
        self.mu.mem_map(self.SENTINEL, 0x1000)
        self.mu.mem_write(self.SENTINEL, b'\xf4')

    def ret_with(self, rax=None, xmm0=None):
        from unicorn.x86_const import UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_RAX, UC_X86_REG_XMM0
        mu = self.mu
        rsp = mu.reg_read(UC_X86_REG_RSP)
        mu.reg_write(UC_X86_REG_RIP, struct.unpack('<Q', mu.mem_read(rsp, 8))[0])
        mu.reg_write(UC_X86_REG_RSP, rsp + 8)
        if rax is not None: mu.reg_write(UC_X86_REG_RAX, rax)
        if xmm0 is not None: mu.reg_write(UC_X86_REG_XMM0, xmm0)

    def pow_hook(self, mu, address, size, user):
        """pow(10, e) of both engines, modelled (their overflow paths need the C runtime)."""
        from unicorn.x86_const import UC_X86_REG_XMM0, UC_X86_REG_XMM1
        x = struct.unpack('<d', struct.pack('<Q', mu.reg_read(UC_X86_REG_XMM0) & 0xffffffffffffffff))[0]
        y = struct.unpack('<d', struct.pack('<Q', mu.reg_read(UC_X86_REG_XMM1) & 0xffffffffffffffff))[0]
        try:
            r = math.pow(x, y)
        except OverflowError:
            r = math.inf
        self.ret_with(xmm0=struct.unpack('<Q', struct.pack('<d', r))[0])

    def run(self, entry, regs, xmm, stack_args=b''):
        from unicorn.x86_const import UC_X86_REG_RSP, UC_X86_REG_XMM0, UC_X86_REG_RIP
        mu = self.mu
        rsp = self.STACK - 0x2000
        frame = struct.pack('<Q', self.SENTINEL) + b'\0' * 0x20 + stack_args
        mu.mem_write(rsp, frame)
        mu.reg_write(UC_X86_REG_RSP, rsp)
        for r, v in regs.items(): mu.reg_write(r, v)
        mu.reg_write(UC_X86_REG_XMM0, xmm)
        mu.emu_start(entry, self.SENTINEL, count=2000000)
        if mu.reg_read(UC_X86_REG_RIP) != self.SENTINEL:
            raise RuntimeError('emulation did not return')
        return mu.reg_read(UC_X86_REG_XMM0)


class WindowsOriginal(Original):
    BASE = 0x140000000

    def __init__(self, path):
        super().__init__()
        from unicorn import UC_HOOK_CODE
        import lockstep_linux_map as lm
        data = lm.verify_build(path, lm.WINDOWS_BUILD)
        pe = lm.PeImage(data)
        self.mu.mem_map(self.BASE, (pe.image_size + 0xfff) & ~0xfff)
        self.mu.mem_write(self.BASE, data[:0x1000])
        for name, va, size, raw, raw_size, flags in pe.sections:
            self.mu.mem_write(self.BASE + va, data[raw:raw + min(raw_size, size)])
        from unicorn.x86_const import UC_X86_REG_RCX
        hooks = {self.BASE + 0x1a05280: lambda c: 4 if 48 <= c <= 57 else 0,          # isdigit (C locale)
                 self.BASE + 0x1a05540: lambda c: 0x80 if chr(c & 0xff) in '0123456789abcdefABCDEF' and c >= 0 else 0}

        def hook(mu, address, size, user):
            f = hooks.get(address)
            if f:
                c = mu.reg_read(UC_X86_REG_RCX) & 0xffffffff
                if c >= 1 << 31: c -= 1 << 32
                self.ret_with(rax=f(c))
        for a in hooks:
            self.mu.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
        self.mu.hook_add(UC_HOOK_CODE, self.pow_hook, begin=self.BASE + 0x1a1e840, end=self.BASE + 0x1a1e840)

    def parse(self, text, single):
        from unicorn.x86_const import UC_X86_REG_RCX
        self.mu.mem_write(self.TEXT, text + b'\0')
        fallback = struct.pack('<I', FB32) + b'\0' * 4 if single else struct.pack('<Q', FB64)
        out = self.run(self.BASE + (0x14c2bd0 if single else 0x14c3090), {UC_X86_REG_RCX: self.TEXT}, 0, fallback)
        return out & 0xffffffff if single else out & 0xffffffffffffffff


class LinuxOriginal(Original):
    def __init__(self, path):
        super().__init__()
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_XMM0, UC_X86_REG_XMM1
        import lockstep_linux_map as lm
        data = lm.verify_build(path, lm.LINUX_BUILD)
        from elftools.elf.elffile import ELFFile
        import io
        elf = ELFFile(io.BytesIO(data))
        for seg in elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD': continue
            lo = seg['p_vaddr'] & ~0xfff
            hi = (seg['p_vaddr'] + seg['p_memsz'] + 0xfff) & ~0xfff
            self.mu.mem_map(lo, hi - lo)
            self.mu.mem_write(seg['p_vaddr'], data[seg['p_offset']:seg['p_offset'] + seg['p_filesz']])

        def isxdigit_hook(mu, address, size, user):
            c = mu.reg_read(UC_X86_REG_RDI) & 0xff
            self.ret_with(rax=1 if chr(c) in '0123456789abcdefABCDEF' else 0)
        self.mu.hook_add(UC_HOOK_CODE, self.pow_hook, begin=0x659318, end=0x659318)
        self.mu.hook_add(UC_HOOK_CODE, isxdigit_hook, begin=0x657580, end=0x657580)

    def parse(self, text, sep, single):
        from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX
        mu = self.mu
        end_slot, err_slot = self.TEXT + 0x80000, self.TEXT + 0x80010
        mu.mem_write(self.TEXT, text + b'\0')
        mu.mem_write(end_slot, struct.pack('<Q', 0xffffffffffffffff) + b'\0' * 16)
        fallback = FB32 if single else FB64
        out = self.run(0x1a3ac40 if single else 0x1a3b1a0,
                       {UC_X86_REG_RDI: self.TEXT, UC_X86_REG_RSI: sep, UC_X86_REG_RDX: end_slot, UC_X86_REG_RCX: err_slot},
                       fallback)
        end = struct.unpack('<Q', mu.mem_read(end_slot, 8))[0]
        err = mu.mem_read(err_slot, 1)[0]
        bits = out & (0xffffffff if single else 0xffffffffffffffff)
        return bits, (end - self.TEXT if end != 0xffffffffffffffff else -1), err


def agrees(kind, theirs, ours, single):
    """The original's result against the portable one for the modelled outcome."""
    fallback = FB32 if single else FB64
    if kind == 'fallback':
        return theirs == fallback == ours
    if kind == 'zero':
        return theirs == 0 == ours
    return theirs != fallback and close(theirs, ours, single)


def as_float(bits, single):
    return struct.unpack('<f', struct.pack('<I', bits))[0] if single else struct.unpack('<d', struct.pack('<Q', bits))[0]


def close(a, b, single):
    """Same value up to the originals' accumulation error: 1e-9 relative (binary32: one ulp
    of the narrowed result), or both below the normal range."""
    x, y = as_float(a, single), as_float(b, single)
    if math.isnan(x) and y == 0:   # stock: 0 * pow(10, e) with e > 308 is 0 * inf = NaN; the value is zero
        return True
    tiny = 1.2e-38 if single else 2.3e-308
    if math.isinf(x) or math.isinf(y):   # the originals' scaled accumulation overflows early
        return x == y or (math.copysign(1, x) == math.copysign(1, y) and min(abs(x), abs(y)) >= (3e37 if single else 1e307))
    if abs(x) < tiny and abs(y) < tiny:
        return math.copysign(1, x) == math.copysign(1, y) or x == y == 0
    if single:
        return abs(a - b) <= 1 and (a ^ b) >> 31 == 0
    # pow(10, e) is itself subnormal below 1e-308: the originals lose digits there
    return abs(x - y) <= (1e-9 if min(abs(x), abs(y)) > 1e-290 else 1e-3) * max(abs(x), abs(y))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--cc')
    parser.add_argument('--wsl')
    parser.add_argument('--windows', type=Path)
    parser.add_argument('--linux', type=Path)
    parser.add_argument('--count', type=int, default=20000)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    cases = list(corpus(args.count))
    runs = build(args)
    if not runs:
        raise SystemExit('no build: give --zig, --wsl or --cc')
    results = {name: run_harness(cmd, cases) for name, cmd in runs}
    first = next(iter(results.values()))
    for name, rows in results.items():
        if rows != first:
            raise SystemExit(f'{name} differs from {runs[0][0]}')
    stats = {'cases': len(cases), 'value': 0, 'zero': 0, 'fallback': 0}
    for (sep, text), row in zip(cases, first):
        kind, neg, digits, e10, end, err = model(text, sep)
        b64, end64, err64, b32, end32, err32 = row
        stats[kind] += 1
        if kind == 'fallback':
            want64, want32 = FB64, FB32
        elif kind == 'zero':
            want64 = want32 = 0
        else:
            want64 = round_bits(neg, digits, e10, 53, -1022, 1023)
            want32 = round_bits(neg, digits, e10, 24, -126, 127)
        if (b64, b32) != (want64, want32) or (end64, err64) != (end if end is not None else -1, err) or (end32, err32) != (end64, err64):
            raise SystemExit(f'mismatch {sep:#x} {text!r}: got {row}, model {kind} {want64:#x} {want32:#x} end={end} err={err}')
    print(f'{len(runs)} builds identical on {len(cases)} cases; values correctly rounded, syntax as modelled: {stats}')
    if args.windows:
        win, n = WindowsOriginal(args.windows), 0
        for (sep, text), row in zip(cases, first):
            if sep != ord(';') or len(text) > 200: continue
            kind = model(text, sep)[0]
            for single, ours in ((False, row[0]), (True, row[3])):
                theirs = win.parse(text, single)
                if not agrees(kind, theirs, ours, single):
                    raise SystemExit(f'Windows {"0x14c2bd0" if single else "0x14c3090"} {text!r}: original {theirs:#x}, portable {ours:#x}')
            n += 1
        print(f'Windows readers 0x14c3090/0x14c2bd0: {n} cases agree on the outcome (number, zero, fallback) and value')
    if args.linux:
        lin, n = LinuxOriginal(args.linux), 0
        for (sep, text), row in zip(cases, first):
            if len(text) > 200: continue
            kind = model(text, sep)[0]
            for single, ours, end, err in ((False, row[0], row[1], row[2]), (True, row[3], row[4], row[5])):
                theirs, tend, terr = lin.parse(text, sep, single)
                if not agrees(kind, theirs, ours, single) or (tend, terr) != (end, err):
                    raise SystemExit(f'Linux {"0x1a3ac40" if single else "0x1a3b1a0"} sep={sep:#x} {text!r}: original '
                                     f'{theirs:#x} end={tend} err={terr}, portable {ours:#x} end={end} err={err}')
            n += 1
        print(f'Linux readers 0x1a3b1a0/0x1a3ac40: {n} cases agree on the outcome, value, end position and error flag')


if __name__ == '__main__':
    main()
