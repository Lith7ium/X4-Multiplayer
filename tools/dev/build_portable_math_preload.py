"""Build libx4mp_portable_math.so (Linux LD_PRELOAD) and run its interposition test.

The library interposes the 28 transcendental libm symbols the Linux X4 imports
with src/native/portable_math, the same code the Windows gate redirects the CRT
to (native_gate_portable_math). Off unless preloaded; see
src/native/portable_math/preload/x4mp_portable_math.c.

  python tools/dev/build_portable_math_preload.py [--zig <zig 0.15.2>] [--cc gcc]
         [--test] [--wsl Ubuntu-24.04]

Without --cc the library is built with Zig for x86_64-linux-gnu.2.17 (oldest glibc
symbol versions, so it also loads inside older Steam runtimes). --test builds the
test program three ways (PIE, non-PIE like X4's ET_EXEC, and -z now) against the
host libm and runs it with and without the preload, for the Zig build and for a
host-compiler build, and runs the exact-reciprocal recipes synthetically; it needs a
Linux host or --wsl on Windows. --elf <Linux X4> adds the decimal-reader patch test and
the exact-reciprocal install over the mapped ELF with a decode of every trampoline
(tools/dev/check_exact_reciprocal.py --linux, needs capstone).
Outputs go to .local/portable-math/preload/.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'src/native/portable_math'
OUT = ROOT / '.local/portable-math/preload'
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-fno-builtin']
LIBRARY = [SOURCE / 'preload/x4mp_portable_math.c', SOURCE / 'portable_math.c', SOURCE / 'portable_decimal.c']
SERVICE = {'x4mp_portable_math_info', 'x4mp_portable_decimal_install', 'x4mp_exact_reciprocal_install',
           'x4mp_exact_reciprocal_region', 'x4mp_portable_math_phdrs_are_x4'}


def exported_names():
    text = (SOURCE / 'preload/x4mp_portable_math.c').read_text()
    return sorted(set(re.findall(r'^EXPORT \w+ (\w+)\(', text, re.M)) - SERVICE)


def target_names():
    targets = json.loads((ROOT / 'reports/LOCKSTEP-CROSSPLATFORM-PATCH-TARGETS-20261029.json').read_text())
    return sorted(e['symbol'] for e in targets['math']['linux'] if e['action'].startswith('interpose'))


def unix_path(path, wsl):
    if not wsl:
        return str(path)
    text = str(Path(path).resolve())
    return '/mnt/' + text[0].lower() + text[2:].replace('\\', '/')


def run(command, wsl=None, env=None, check=True):
    if wsl:
        prefix = ['wsl', '-d', wsl, '--']
        if env:
            prefix += ['env'] + [f'{k}={v}' for k, v in env.items()]
        command = prefix + command
        env = None
    else:
        env = dict(os.environ, **env) if env else None
    return subprocess.run(command, check=check, capture_output=True, text=True, env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--zig', type=Path, help='Zig 0.15.2 (default: X4LAN_ZIG when set)')
    parser.add_argument('--cc', help='host C compiler for the test program and a second library build')
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--wsl', help='Windows host: build and run the Linux test in this WSL distribution')
    parser.add_argument('--elf', type=Path, help='with --test: the pinned Linux X4, for the decimal-reader patch test (read only)')
    args = parser.parse_args()
    if args.zig is None and os.environ.get('X4LAN_ZIG'):
        args.zig = Path(os.environ['X4LAN_ZIG'])
    if exported_names() != target_names():
        raise SystemExit(f'exports {exported_names()} != patch targets {target_names()}')
    OUT.mkdir(parents=True, exist_ok=True)
    libraries = []
    if args.zig:
        version = subprocess.check_output([str(args.zig), 'version'], text=True).strip()
        if version != '0.15.2':
            raise SystemExit(f'expected Zig 0.15.2, found {version}')
        lib = OUT / 'libx4mp_portable_math.so'
        subprocess.run([str(args.zig), 'cc', '-target', 'x86_64-linux-gnu.2.17', '-shared', '-fPIC', '-fvisibility=hidden',
                        *FLAGS, *map(str, LIBRARY), '-o', str(lib)], check=True)
        libraries.append(('zig', lib))
        print(lib)
    if not args.test:
        if not libraries:
            raise SystemExit('nothing built: give --zig, or --test with a host compiler')
        return
    if os.name == 'nt' and not args.wsl:
        raise SystemExit('--test on Windows needs --wsl DISTRO')
    wsl = args.wsl if os.name == 'nt' else None
    cc = args.cc or 'cc'
    host_lib = OUT / 'libx4mp_portable_math-host.so'
    run([cc, '-shared', '-fPIC', '-fvisibility=hidden', *FLAGS, *[unix_path(p, wsl) for p in LIBRARY],
         '-o', unix_path(host_lib, wsl)], wsl)
    libraries.append(('host', host_lib))
    sources = [unix_path(SOURCE / 'preload/test_preload.c', wsl), unix_path(SOURCE / 'portable_math.c', wsl)]
    variants = {'pie': ['-pie', '-fPIE'], 'nopie': ['-no-pie', '-fno-pie'], 'bindnow': ['-Wl,-z,now']}
    for name, extra in variants.items():
        exe = OUT / f'test_preload-{name}'
        run([cc, *FLAGS, *extra, *sources, '-o', unix_path(exe, wsl), '-lm', '-ldl'], wsl)
        glibc = run([unix_path(exe, wsl), 'glibc'], wsl, check=False)
        print(f'{name} glibc: {glibc.stdout.strip().splitlines()[-1]}')
        if glibc.returncode:
            raise SystemExit(glibc.stdout + glibc.stderr)
        for tag, lib in libraries:
            log = OUT / f'preload-{name}-{tag}.log'
            if log.exists():
                log.unlink()
            result = run([unix_path(exe, wsl), 'portable'], wsl,
                         env={'LD_PRELOAD': unix_path(lib, wsl), 'X4MP_PORTABLE_MATH_LOG': unix_path(log, wsl)}, check=False)
            line = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else result.stderr
            print(f'{name} preload {tag}: {line}')
            if result.returncode or 'failures=0' not in line:
                raise SystemExit(result.stdout + result.stderr)
            active = log.read_text()
            if 'X4MP_PORTABLE_MATH ACTIVE version=1 ' not in active or 'symbols=28' not in active:
                raise SystemExit(f'activation log: {active!r}')
    print(f'{len(variants)} test programs x {len(libraries)} libraries: every symbol interposed, bits equal to the portable library')
    for flag in ('X4MP_PORTABLE_DECIMAL', 'X4MP_EXACT_RECIPROCAL'):
        for tag, lib in libraries:
            log = OUT / f'preload-refused-{tag}.log'
            if log.exists():
                log.unlink()
            result = run([unix_path(OUT / 'test_preload-nopie', wsl), 'portable'], wsl, check=False,
                         env={'LD_PRELOAD': unix_path(lib, wsl), 'X4MP_PORTABLE_MATH_LOG': unix_path(log, wsl), flag: '1'})
            refused = log.read_text() if log.exists() else ''
            if result.returncode != 97 or 'REFUSED engine patches requested but the executable is not' not in refused:
                raise SystemExit(f'{flag} outside X4: exit {result.returncode}, log {refused!r}')
    print('engine patches requested outside the pinned X4: refused before any read (exit 97)')
    reciprocal = OUT / 'test_preload_reciprocal'
    run([cc, *FLAGS, '-pie', '-fPIE', unix_path(SOURCE / 'preload/test_preload_reciprocal.c', wsl), '-o', unix_path(reciprocal, wsl),
         '-ldl', '-lm'], wsl)
    result = run([unix_path(reciprocal, wsl)], wsl, check=False)
    print(result.stdout.strip())
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if args.elf:
        for tag, lib in libraries:
            export = OUT / f'xrcp-linux-{tag}.bin'
            result = run([unix_path(reciprocal, wsl), unix_path(args.elf, wsl), unix_path(lib, wsl), unix_path(export, wsl)], wsl, check=False)
            lines = result.stdout.strip().splitlines()
            print('\n'.join(f'{tag}: {line}' for line in lines[1:-1]) if len(lines) > 2 else f'{tag}: {result.stderr}')
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            try:
                import capstone  # noqa: F401
            except ImportError:
                print('capstone missing: trampoline decode check skipped')
                continue
            check = subprocess.run([sys.executable, str(ROOT / 'tools/dev/check_exact_reciprocal.py'), '--linux', str(args.elf),
                                    '--export', str(export)], capture_output=True, text=True)
            print(f'{tag}: {check.stdout.strip() or check.stderr.strip()}')
            if check.returncode:
                raise SystemExit(check.stdout + check.stderr)
        exe = OUT / 'test_preload_decimal'
        run([cc, *FLAGS, '-pie', '-fPIE', unix_path(SOURCE / 'preload/test_preload_decimal.c', wsl),
             unix_path(SOURCE / 'portable_decimal.c', wsl), '-o', unix_path(exe, wsl), '-ldl'], wsl)
        for tag, lib in libraries:
            result = run([unix_path(exe, wsl), unix_path(args.elf, wsl), unix_path(lib, wsl)], wsl, check=False)
            print(f'{tag}: {result.stdout.strip()}')
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)


if __name__ == '__main__':
    main()
