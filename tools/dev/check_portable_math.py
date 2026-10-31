"""Cross-compiler/cross-platform bit-identity and accuracy proof for src/native/portable_math.

Builds src/native/portable_math/test_portable_math.c with every available toolchain,
runs the same integer-generated input set (edge cases plus millions of points) through
each build, and requires one SHA-256 per function and in total for all of them, under
three caller MXCSR states. Builds:
  Windows host: Zig 0.15.2 for x86_64-windows-gnu at -O0/-O2/-O3 and -O2 for the native
    CPU (FMA available, -ffp-contract=off); with --wsl DISTRO also GCC in that WSL
    distribution at -O0/-O2/-O3/-O2 -march=native and Zig's x86_64-linux-gnu build.
  Linux host: cc/gcc at the same levels, plus Zig for x86_64-linux-gnu when given.
Accuracy: a dumped sample (edge cases plus --sample points per function) is compared
with mpmath at 160+ bits (optional dependency; skipped with a note when missing) and
reported as the maximum error in ulps of the correctly rounded result.
No game binaries are read or run. Outputs go to .local/portable-math/.

  python tools/dev/check_portable_math.py --zig <zig.exe> [--wsl Ubuntu-24.04]
      [--count 2000000] [--sample 20000] [--sweep] [--report path.json]
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/portable_math'
OUT = ROOT / '.local/portable-math'
FLAGS = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off']
FILES = ['portable_math.c', 'test_portable_math.c']
UNARY_FLOAT = ['sinf', 'cosf', 'tanf', 'sincosf', 'asinf', 'acosf', 'atanf', 'expf', 'exp2f', 'expm1f',
               'logf', 'log2f', 'log10f', 'log1pf', 'cbrtf', 'sinhf', 'coshf', 'tanhf']


def wsl_path(path):
    text = str(path.resolve())
    return '/mnt/' + text[0].lower() + text[2:].replace('\\', '/')


class Build:
    def __init__(self, name, run_prefix, exe):
        self.name, self.run_prefix, self.exe = name, run_prefix, exe

    def run(self, *args, timeout=3600):
        result = subprocess.run(self.run_prefix + [self.exe, *map(str, args)], capture_output=True, text=True, timeout=timeout)
        if result.returncode:
            raise RuntimeError(f'{self.name} {args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
        return result.stdout


def zig_version(zig):
    version = subprocess.check_output([str(zig), 'version'], text=True).strip()
    if version != '0.15.2':
        raise SystemExit(f'expected Zig 0.15.2, found {version}')
    return version


def builds(args):
    OUT.mkdir(parents=True, exist_ok=True)
    found, toolchains = [], {}
    sources = [str(SOURCE / f) for f in FILES]
    levels = [('O0', ['-O0']), ('O2', ['-O2']), ('O3', ['-O3'])]
    native_fma = ['-O2', '-DX4PM_ALLOW_FMA_TARGET']
    if os.name == 'nt':
        if not args.zig:
            raise SystemExit('Windows host: --zig <zig 0.15.2> is required')
        toolchains['zig'] = 'zig ' + zig_version(args.zig)
        for tag, opt in levels + [('O2-native', native_fma + ['-mcpu=native'])]:
            exe = OUT / f'tpm-windows-zig-{tag}.exe'
            subprocess.run([str(args.zig), 'cc', '-target', 'x86_64-windows-gnu', *FLAGS, *opt, *sources, '-o', str(exe)], check=True)
            found.append(Build(f'windows-zig-{tag}', [], str(exe)))
        if args.wsl:
            prefix = ['wsl', '-d', args.wsl, '--']
            gcc = subprocess.run(prefix + ['gcc', '--version'], capture_output=True, text=True)
            libc = subprocess.run(prefix + ['ldd', '--version'], capture_output=True, text=True)
            toolchains['wsl_gcc'] = gcc.stdout.splitlines()[0] if gcc.returncode == 0 else 'missing'
            toolchains['wsl_libc'] = libc.stdout.splitlines()[0] if libc.returncode == 0 else 'unknown'
            for tag, opt in levels + [('O2-native', native_fma + ['-march=native'])]:
                exe = OUT / f'tpm-linux-gcc-{tag}'
                subprocess.run(prefix + ['gcc', *FLAGS, *opt, *[wsl_path(Path(s)) for s in sources], '-o', wsl_path(exe)], check=True)
                found.append(Build(f'linux-gcc-{tag}', prefix, wsl_path(exe)))
            exe = OUT / 'tpm-linux-zig-O2'
            subprocess.run([str(args.zig), 'cc', '-target', 'x86_64-linux-gnu', *FLAGS, '-O2', *sources, '-o', str(exe)], check=True)
            found.append(Build('linux-zig-O2', prefix, wsl_path(exe)))
    else:
        cc = shutil.which(args.cc or 'cc')
        if not cc:
            raise SystemExit('no C compiler')
        toolchains['cc'] = subprocess.check_output([cc, '--version'], text=True).splitlines()[0]
        for tag, opt in levels + [('O2-native', native_fma + ['-march=native'])]:
            exe = OUT / f'tpm-linux-cc-{tag}'
            subprocess.run([cc, *FLAGS, *opt, *sources, '-o', str(exe)], check=True)
            found.append(Build(f'linux-cc-{tag}', [], str(exe)))
        if args.zig:
            toolchains['zig'] = 'zig ' + zig_version(args.zig)
            exe = OUT / 'tpm-linux-zig-O2'
            subprocess.run([str(args.zig), 'cc', '-target', 'x86_64-linux-gnu', *FLAGS, '-O2', *sources, '-o', str(exe)], check=True)
            found.append(Build('linux-zig-O2', [], str(exe)))
    return found, toolchains


def parse_hash(text):
    functions, totals, kat = {}, {}, None
    for line in text.splitlines():
        parts = line.split()
        if parts[0] == 'fn':
            functions[parts[1]] = (int(parts[2].split('=')[1]), parts[3].split('=')[1])
        elif parts[0] == 'total':
            totals[parts[1].split('=')[1]] = (int(parts[3].split('=')[1]), parts[4].split('=')[1])
        elif parts[0] == 'kat':
            kat = (parts[1], parts[3])
    return functions, totals, kat


# ---- accuracy (mpmath) ----
def ulp_error(name, kind, a, b, r):
    import mpmath
    from mpmath import mp, mpf
    is_float = kind == 'f'
    unpack = (lambda u: struct.unpack('<f', struct.pack('<I', u & 0xffffffff))[0]) if is_float else \
             (lambda u: struct.unpack('<d', struct.pack('<Q', u))[0])
    x, y = unpack(a), unpack(b)
    results = [unpack(r[0]), unpack(r[1])]
    if not all(map(math.isfinite, (x, y))):
        return None
    mp.prec = 200
    base = name[:-1] if is_float else name
    X, Y = mpf(x), mpf(y)
    try:
        if base == 'sincos':
            exact = [mpmath.sin(X), mpmath.cos(X)]
        else:
            fn = {
                'sin': lambda: mpmath.sin(X), 'cos': lambda: mpmath.cos(X), 'tan': lambda: mpmath.tan(X),
                'asin': lambda: mpmath.asin(X) if abs(X) <= 1 else None, 'acos': lambda: mpmath.acos(X) if abs(X) <= 1 else None,
                'atan': lambda: mpmath.atan(X), 'atan2': lambda: mpmath.atan2(X, Y) if X and Y else None,
                'exp': lambda: mpmath.exp(X), 'exp2': lambda: mpmath.power(2, X), 'expm1': lambda: mpmath.expm1(X),
                'log': lambda: mpmath.log(X) if X > 0 else None, 'log2': lambda: mpmath.log(X, 2) if X > 0 else None,
                'log10': lambda: mpmath.log10(X) if X > 0 else None, 'log1p': lambda: mpmath.log1p(X) if X > -1 else None,
                'pow': lambda: pow_exact(X, Y), 'fmod': lambda: None,
                'hypot': lambda: mpmath.sqrt(X * X + Y * Y), 'cbrt': lambda: mpmath.cbrt(X) if X >= 0 else -mpmath.cbrt(-X),
                'sinh': lambda: mpmath.sinh(X), 'cosh': lambda: mpmath.cosh(X), 'tanh': lambda: mpmath.tanh(X),
            }[base]
            value = fn()
            if value is None:
                return None
            exact = [value]
    except (ValueError, ZeroDivisionError, OverflowError):
        return None
    worst = 0.0
    for got, want in zip(results, exact):
        if isinstance(want, mpmath.mpc) or want is None:
            return None
        err = ulps(got, want, is_float)
        if err is None:
            return None
        worst = max(worst, err)
    return worst


def pow_exact(X, Y):
    import mpmath
    if X == 0:
        return None
    if X < 0:
        if Y != int(Y):
            return None
        value = mpmath.power(-X, Y)
        return -value if int(Y) % 2 else value
    return mpmath.power(X, Y)


def ulps(got, want, is_float):
    import mpmath
    mant, emin, emax = (24, -126, 127) if is_float else (53, -1022, 1023)
    if want == 0:
        return 0.0 if got == 0 else math.inf
    mag = abs(want)
    overflow = mpmath.ldexp(2, emax) - mpmath.ldexp(1, emax - mant)  # largest finite + half an ulp
    if mag >= overflow:
        return 0.0 if math.isinf(got) and (got > 0) == (want > 0) else math.inf
    if math.isinf(got) or math.isnan(got):
        return math.inf
    e = max(int(mpmath.floor(mpmath.log(mag, 2))), emin)
    unit = mpmath.ldexp(1, e - mant + 1)
    return float(abs(mpmath.mpf(got) - want) / unit)


def accuracy_chunk(rows):
    out = {}
    for name, a, b, r0, r1 in rows:
        kind = 'f' if name.endswith('f') else 'd'
        err = ulp_error(name, kind, a, b, (r0, r1))
        if err is None:
            continue
        best = out.get(name, (-1.0, None, 0))
        count = best[2] + 1
        out[name] = (err, (a, b), count) if err > best[0] else (best[0], best[1], count)
    return out


def accuracy(dump_path, workers):
    try:
        import mpmath  # noqa: F401
    except ImportError:
        return {'skipped': 'mpmath not installed'}
    rows = []
    for line in dump_path.read_text().splitlines():
        name, a, b, r0, r1 = line.split()
        if name == 'fmod' or name == 'fmodf':
            continue
        rows.append((name, int(a, 16), int(b, 16), int(r0, 16), int(r1, 16)))
    chunks = [rows[i::workers] for i in range(workers)]
    merged = {}
    with concurrent.futures.ProcessPoolExecutor(workers) as pool:
        for part in pool.map(accuracy_chunk, chunks):
            for name, (err, where, count) in part.items():
                old = merged.get(name, (-1.0, None, 0))
                merged[name] = (max(err, old[0]), where if err > old[0] else old[1], old[2] + count)
    return {name: {'max_ulp': round(err, 4), 'at': [hex(where[0]), hex(where[1])], 'points': count}
            for name, (err, where, count) in sorted(merged.items())}


def fmod_exact(dump_path):
    """fmod is exact: compare with Python's math.fmod (also exact) on finite inputs."""
    bad = checked = 0
    for line in dump_path.read_text().splitlines():
        name, a, b, r0, _ = line.split()
        if name not in ('fmod', 'fmodf'):
            continue
        if name == 'fmod':
            x, y, r = (struct.unpack('<d', struct.pack('<Q', int(v, 16)))[0] for v in (a, b, r0))
        else:
            x, y, r = (struct.unpack('<f', struct.pack('<I', int(v, 16) & 0xffffffff))[0] for v in (a, b, r0))
        if not (math.isfinite(x) and math.isfinite(y)) or y == 0:
            continue
        checked += 1
        want = math.fmod(x, y)
        if r != want or math.copysign(1, r) != math.copysign(1, want):
            bad += 1
    return {'checked': checked, 'mismatches': bad}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--zig', type=Path)
    parser.add_argument('--cc')
    parser.add_argument('--wsl', help='also build and run in this WSL distribution (Windows host)')
    parser.add_argument('--count', type=int, default=2000000, help='generated points per function (edge cases come on top)')
    parser.add_argument('--sample', type=int, default=20000, help='generated points per function for the mpmath check')
    parser.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) - 2))
    parser.add_argument('--sweep', action='store_true', help='also hash every binary32 input of the unary float functions')
    parser.add_argument('--report', type=Path, default=OUT / 'report.json')
    parser.add_argument('--accuracy-only', type=Path, metavar='DUMP', help='only rerun the mpmath check on a dump file')
    args = parser.parse_args()
    if args.accuracy_only:
        print(json.dumps({'accuracy': accuracy(args.accuracy_only, args.workers), 'fmod_exact': fmod_exact(args.accuracy_only)}, indent=1))
        return
    started = time.time()
    found, toolchains = builds(args)
    report = {'scope': 'nongame library harness; no X4 binary read or run', 'host': platform.platform(),
              'toolchains': toolchains, 'count': args.count, 'builds': {}}
    reference = None
    for build in found:
        functions, totals, kat = parse_hash(build.run('hash', args.count))
        check = build.run('check', 200000).strip().splitlines()[-1]
        report['builds'][build.name] = {'total': totals, 'kat': kat, 'check': check}
        if check != 'check failures=0':
            raise SystemExit(f'{build.name}: {check}')
        if len({value for value in totals.values()}) != 1:
            raise SystemExit(f'{build.name}: MXCSR-dependent results {totals}')
        if kat[0] != kat[1]:
            raise SystemExit(f'{build.name}: KAT {kat[0]} != expected {kat[1]}')
        if reference is None:
            reference = (build.name, functions, totals)
        elif functions != reference[1] or totals != reference[2]:
            diff = sorted(k for k in functions if functions[k] != reference[1].get(k))
            raise SystemExit(f'{build.name} differs from {reference[0]} in {diff}')
        print(f'{build.name}: {next(iter(totals.values()))[1]} ({next(iter(totals.values()))[0]} points x 3 MXCSR states)')
    report['functions'] = {name: {'points': n, 'sha256': digest} for name, (n, digest) in reference[1].items()}
    report['total'] = next(iter(reference[2].values()))
    # dump identical across the first and last build, then accuracy
    dumps = []
    for build in (found[0], found[-1]):
        path = OUT / f'dump-{build.name}.txt'
        target = wsl_path(path) if build.run_prefix else str(path)
        build.run('dump', target, args.sample)
        dumps.append(path)
    digests = {hashlib.sha256(p.read_bytes()).hexdigest() for p in dumps}
    if len(digests) != 1:
        raise SystemExit('accuracy sample differs between builds')
    report['sample_sha256'] = digests.pop()
    report['accuracy'] = accuracy(dumps[0], args.workers)
    report['fmod_exact'] = fmod_exact(dumps[0])
    if args.sweep:
        sweeps = {}
        for name in UNARY_FLOAT:
            values = set()
            for build in (found[1], found[-1]):
                line = build.run('sweep', name, timeout=7200).strip()
                values.add(line.split('sha256=')[1])
            if len(values) != 1:
                raise SystemExit(f'sweep {name} differs between builds')
            sweeps[name] = values.pop()
            print(f'sweep {name}: {sweeps[name]}')
        report['sweep_all_binary32'] = {'builds': [found[1].name, found[-1].name], 'sha256': sweeps}
    report['seconds'] = round(time.time() - started, 1)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('total', 'accuracy', 'fmod_exact')}, indent=1))
    print(f'{len(found)} builds identical; report {args.report}')


if __name__ == '__main__':
    main()
