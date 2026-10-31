"""Build our own Jolt Physics 5.5.0 for Windows (MSVC ABI) and Linux (Itanium ABI) and
run one physics scenario through each build, offline (no X4).

  python tools/dev/jolt_portable_build.py fetch
  python tools/dev/jolt_portable_build.py build  [--variant deterministic|control] [--threads N]
  python tools/dev/jolt_portable_build.py run    [--variant ...] [--frames 600] [--jobs 4] --out FILE
  python tools/dev/jolt_portable_build.py compare A B
  python3 tools/dev/jolt_portable_build.py x4-harness --x4 X4 [--redirect]          (Linux)
  python3 tools/dev/jolt_portable_build.py x4-run --x4 X4 [--redirect all] --out F  (Linux)

fetch clones the official tag v5.5.0 (MIT, Jorrit Rouwe) into .local/jolt and checks
the commit; the sources are never committed (third_party/JoltPhysics-5.5.0 holds the
licence and the pin). build compiles every Jolt/*.cpp with the host's toolchain:
on Windows MSVC (cl.exe from Visual Studio Build Tools, found with vswhere), on Linux
g++ (or --cxx). Configuration of both: X4's Jolt feature set (single precision,
ObjectStream on, 16-bit object layers, no asserts, profiler or debug renderer),
SSE2 baseline (no SSE4/AVX/FMA paths), NDEBUG, static runtime on Windows.
  deterministic: JPH_CROSS_PLATFORM_DETERMINISTIC, /fp:precise or -ffp-contract=off
  control:       no determinism define; MSVC /fp:fast like X4.exe's Jolt, g++ default
  mapping:       Windows: control with link-time code generation like X4.exe, linked
                 whole with a map (jolt_all.exe): the reference for jolt_portable_winmap.py
run executes src/native/jolt/jolt_determinism_scenario.cpp and writes its output;
compare reports whether two outputs are identical (bit-identical simulation).
x4-harness/x4-run (Linux, owned game required, offline): the same scenario driven by
the Jolt inside the owned X4 Linux executable (src/native/jolt/x4_linux_jolt_loader.cpp
maps it into the test process; size and SHA-256 pinned), optionally with its Jolt
functions redirected to our deterministic build: the offline model of the redirect.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
JOLT_TAG, JOLT_COMMIT = 'v5.5.0', '23dadd0e603f1b321142d4c74df07fce85064989'
JOLT_URL = 'https://github.com/jrouwe/JoltPhysics.git'
JOLT = ROOT / '.local/jolt/JoltPhysics-5.5.0'
SCENARIO = ROOT / 'src/native/jolt/jolt_determinism_scenario.cpp'
DEFINES = ['JPH_OBJECT_STREAM', 'NDEBUG']


def fetch():
    if not (JOLT / '.git').is_dir():
        JOLT.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', '-c', 'core.autocrlf=false', 'clone', '--depth', '1', '--branch', JOLT_TAG, JOLT_URL, str(JOLT)], check=True)
    head = subprocess.check_output(['git', '-c', 'safe.directory=*', '-C', str(JOLT), 'rev-parse', 'HEAD'], text=True).strip()
    if head != JOLT_COMMIT:
        raise SystemExit(f'{JOLT} is at {head}, expected {JOLT_TAG} {JOLT_COMMIT}')
    if subprocess.run(['git', '-c', 'safe.directory=*', '-c', 'core.filemode=false', '-C', str(JOLT), 'diff', '--quiet', 'HEAD'], check=False).returncode:
        raise SystemExit(f'{JOLT} has local changes')
    return JOLT


def sources():
    return sorted((JOLT / 'Jolt').rglob('*.cpp'))


def msvc_environment():
    """The x64 Developer environment of the newest Visual Studio (Build Tools) with C++."""
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    path = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    vcvars = Path(path) / 'VC/Auxiliary/Build/vcvars64.bat'
    env = dict(os.environ, PATH=f'{vswhere.parent};{os.environ.get("PATH", "")}')  # vcvars calls vswhere itself
    out = subprocess.check_output(f'"{vcvars}" >nul && set', shell=True, text=True, env=env)
    env = dict(line.split('=', 1) for line in out.splitlines() if '=' in line)
    env['PATH'] = env.pop('Path', env.get('PATH', ''))
    return env


def compile_flags(variant, windows, compiler):
    """Compiler command prefix for one variant (without source and output)."""
    defines = DEFINES + (['JPH_CROSS_PLATFORM_DETERMINISTIC'] if variant == 'deterministic' else [])
    if windows:
        return [compiler, '/nologo', '/c', '/std:c++17', '/permissive-', '/Zc:__cplusplus', '/Zc:inline', '/EHsc', '/GR', '/MT',
                '/O2', '/Oi', '/Ot', '/GS-', '/Gy', '/fp:except-', '/fp:precise' if variant == 'deterministic' else '/fp:fast',
                '/W3', f'/I{JOLT}'] + (['/GL'] if variant == 'mapping' else []) + [f'/D{d}' for d in defines]
    return [compiler, '-c', '-std=c++17', '-O2', '-fPIC', '-msse2', '-mfpmath=sse', '-pthread', '-fexceptions', '-frtti',
            '-Wno-stringop-overflow', '-Wno-psabi', f'-I{JOLT}'] + \
        (['-ffp-contract=off'] if variant == 'deterministic' else []) + [f'-D{d}' for d in defines]


class Toolchain:
    def __init__(self, variant, cxx=None):
        self.variant, self.windows = variant, os.name == 'nt'
        self.out = ROOT / '.local/jolt-portable' / ('windows' if self.windows else 'linux') / variant
        if self.windows:
            self.env = msvc_environment()
            self.cl, self.lib, self.link = (shutil.which(t, path=self.env['PATH']) for t in ('cl', 'lib', 'link'))
            self.version = subprocess.run([self.cl], env=self.env, capture_output=True, text=True).stderr.splitlines()[0]
            self.flags = compile_flags(variant, True, self.cl)
        else:
            self.env = None
            cxx = cxx or 'g++'
            self.version = subprocess.check_output([cxx, '--version'], text=True).splitlines()[0]
            self.flags = compile_flags(variant, False, cxx)

    def compile(self, source, obj):
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = self.flags + ([str(source), f'/Fo{obj}'] if self.windows else [str(source), '-o', str(obj)])
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f'{source}: {result.stdout}{result.stderr}')
        return obj

    def build(self, threads):
        files = sources()
        objects = [self.out / 'obj' / (f.relative_to(JOLT).with_suffix('.obj' if self.windows else '.o')) for f in files]
        stale = [(f, o) for f, o in zip(files, objects) if not o.exists() or o.stat().st_mtime < f.stat().st_mtime]
        with ThreadPoolExecutor(threads) as pool:
            list(pool.map(lambda pair: self.compile(*pair), stale))
        library = self.out / ('jolt.lib' if self.windows else 'libjolt.a')
        library.unlink(missing_ok=True)
        if self.windows:
            listing = self.out / 'objects.txt'
            listing.write_text('\n'.join(f'"{o}"' for o in objects))
            ltcg = ['/LTCG'] if self.variant == 'mapping' else []
            subprocess.run([self.lib, '/nologo', *ltcg, f'/OUT:{library}', f'@{listing}'], env=self.env, check=True)
        else:
            subprocess.run(['ar', 'rcs', str(library)] + [str(o) for o in objects], check=True)
        scenario_obj = self.compile(SCENARIO, self.out / ('scenario.obj' if self.windows else 'scenario.o'))
        exe = self.out / ('jolt_determinism_scenario.exe' if self.windows else 'jolt_determinism_scenario')
        if self.windows:
            subprocess.run([self.link, '/nologo', *ltcg, f'/OUT:{exe}', str(scenario_obj), str(library)], env=self.env, check=True)
            # Every Jolt function kept, with a map: the reference side of jolt_portable_winmap.py.
            whole = self.out / 'jolt_all.exe'
            subprocess.run([self.link, '/nologo', *ltcg, f'/OUT:{whole}', f'/MAP:{whole.with_suffix(".map")}', '/OPT:NOREF',
                            '/OPT:NOICF', '/INCREMENTAL:NO', str(scenario_obj), f'/WHOLEARCHIVE:{library}'],
                           env=self.env, check=True, capture_output=True)
        else:
            subprocess.run([self.flags[0], '-pthread', '-o', str(exe), str(scenario_obj), str(library)], check=True)
        (self.out / 'build.json').write_text(json.dumps({'variant': self.variant, 'compiler': self.version, 'flags': self.flags[1:],
                                                          'sources': len(files), 'jolt_commit': JOLT_COMMIT}, indent=2) + '\n')
        return exe


# Jolt translation units X4 does not link (it brings its own job system); the harness
# compiles them from the same source. Everything else comes from the executable.
HARNESS_SUPPORT = ['Jolt/Core/JobSystemThreadPool', 'Jolt/Core/JobSystemWithBarrier', 'Jolt/Core/Semaphore', 'Jolt/Physics/StateRecorderImpl']


def elf_symbols(path):
    """Defined dynamic symbols of an ELF file: name -> value (nm -D)."""
    out = subprocess.check_output(['nm', '-D', '--defined-only', str(path)], text=True)
    return {parts[2]: int(parts[0], 16) for parts in (line.split() for line in out.splitlines()) if len(parts) == 3}


def symbols(obj, *flags):
    return [line.split() for line in subprocess.check_output(['nm', *flags, str(obj)], text=True).splitlines() if line.split()]


def x4_object_symbols(x4):
    """Names of the executable's exported Jolt data objects (nm -D types B/D/R/V/u)."""
    return {p[2] for p in symbols(x4, '-D', '--defined-only') if len(p) == 3 and p[1] in 'BDRVu' and '3JPH' in p[2]}


def shared_names(names):
    """Jolt static data a redirected function must share with the executable: the allocator
    hooks, Trace, Factory::sInstance, PhysicsMaterial::sDefault, Body::sFixedToWorld, the
    collision dispatch tables, the shape registry and constant tables. Vtables, typeinfo and
    function-local statics are not shared: each copy keeps its own."""
    return {n for n in names if '3JPH' in n and not n.startswith(('_ZTV', '_ZTI', '_ZTS', '_ZZ', '_ZGV', '_ZTh', '_ZTv'))}


def shared_data(x4):
    return shared_names(x4_object_symbols(x4))


def qph_name(name):
    """Our build's twin of a Jolt symbol: namespace JPH renamed to QPH (same length)."""
    return name.replace('3JPH', '3QPH')


def qph_objects(det, x4, out):
    """Our deterministic Jolt with every Jolt symbol except the shared data renamed from
    namespace JPH to QPH (same length, valid mangling), so it links next to the executable's
    Jolt. Shared data definitions are weakened (the executable's copy is bound instead) and
    their dynamic initializers dropped (the executable initialised its own)."""
    shared = shared_data(x4)
    objects = sorted((det.out / 'obj').rglob('*.o'))
    names = set()
    for o in objects:
        names |= {p[-1] for p in symbols(o) if '3JPH' in p[-1]}
    mapping = out / 'qph.map'
    mapping.write_text(''.join(f'{n} {qph_name(n)}\n' for n in sorted(names - shared)))
    renamed, dropped = [], []
    for o in objects:
        target = out / 'qph' / o.relative_to(det.out / 'obj')
        target.parent.mkdir(parents=True, exist_ok=True)
        defined_shared = sorted(p[-1] for p in symbols(o, '--defined-only') if p[-1] in shared)
        command = ['objcopy', f'--redefine-syms={mapping}'] + [f'--weaken-symbol={n}' for n in defined_shared]
        if defined_shared and subprocess.run(['objdump', '-h', str(o)], capture_output=True, text=True).stdout.count('.init_array'):
            command.append('--remove-section=.init_array')
            dropped.append(o.name)
        subprocess.run(command + [str(o), str(target)], check=True)
        renamed.append(target)
    return renamed, shared, dropped


def x4_harness(x4, cxx=None, redirect=False):
    """Linux only: link the scenario against the Jolt inside the owned X4 executable.
    X4's RegisterTypesInternal refuses another feature word, so the harness side uses the
    control configuration (feature word 0x400 like X4; same layouts and, under g++, the
    same code as the deterministic one). With redirect, our deterministic Jolt is linked
    in as namespace QPH and the loader can point the executable's Jolt functions at it
    (X4_JOLT_REDIRECT=all or name fragments): the offline model of the in-game redirect."""
    tool = Toolchain('control', cxx)
    tool.build(os.cpu_count() or 4)
    out = tool.out.parent / ('x4-harness-redirect' if redirect else 'x4-harness')
    out.mkdir(parents=True, exist_ok=True)
    scenario = out / 'scenario_x4.o'
    subprocess.run(tool.flags + ['-DJOLT_SCENARIO_X4', str(SCENARIO), '-o', str(scenario)], check=True)
    loader = out / 'loader.o'
    subprocess.run(tool.flags + ['-I', str(ROOT / 'src/native/engine'), str(ROOT / 'src/native/jolt/x4_linux_jolt_loader.cpp'), '-o', str(loader)], check=True)
    support = [tool.out / 'obj' / (name + '.o') for name in HARNESS_SUPPORT]
    objects = [scenario, loader] + support
    qph, shared, dropped = [], set(), []
    if redirect:
        det = Toolchain('deterministic', cxx)
        det.build(os.cpu_count() or 4)
        qph, shared, dropped = qph_objects(det, x4, out)
    undefined = set()
    for o in objects + qph:
        undefined |= {p[-1] for p in symbols(o, '-u')}
    defined = set()
    for o in support:
        defined |= {p[-1] for p in symbols(o, '--defined-only')}
    weak_shared = set()
    for o in qph:
        weak_shared |= {p[-1] for p in symbols(o, '--defined-only') if p[-1] in shared}
    x4symbols = elf_symbols(x4)
    wanted = (undefined - defined) | weak_shared
    bound = sorted(n for n in wanted if '3JPH' in n and n in x4symbols)
    missing = sorted(n for n in wanted if '3JPH' in n and n not in x4symbols)
    if missing:
        raise SystemExit('Jolt symbols neither in the executable nor in the harness support units:\n  ' + '\n  '.join(missing))
    script = out / 'x4-jolt-symbols.ld'  # generated from the owned executable; stays under .local
    script.write_text(''.join(f'{n} = 0x{x4symbols[n]:x};\n' for n in bound))
    exe = out / 'jolt_determinism_scenario_x4'
    subprocess.run([tool.flags[0], '-no-pie', '-Wl,-Ttext-segment=0x10000000', '-pthread', '-rdynamic', '-o', str(exe)] +
                   [str(o) for o in objects + qph] + [str(script), '-ldl'], check=True)
    print(f'{exe}: {len(bound)} Jolt symbols bound to the executable' +
          (f'; {len(qph)} renamed objects, {len(shared)} shared data symbols, initializers dropped in {dropped}' if redirect else ''))
    return exe


def x4_run(x4, redirect, frames, jobs, out):
    """Linux only: run the harness (x4-harness) against the owned executable; redirect is
    None (the executable's own Jolt), 'all' or comma-separated name fragments."""
    folder = ROOT / '.local/jolt-portable/linux' / ('x4-harness-redirect' if redirect else 'x4-harness')
    exe = folder / 'jolt_determinism_scenario_x4'
    if not exe.exists():
        raise SystemExit(f'{exe} missing: run x4-harness{" --redirect" if redirect else ""} first')
    env = dict(os.environ, X4_LINUX_EXE=str(x4))
    env.pop('X4_JOLT_REDIRECT', None)
    if redirect:
        env['X4_JOLT_REDIRECT'] = redirect
    result = subprocess.run([str(exe), str(frames), str(jobs)], env=env, capture_output=True, text=True, timeout=900)
    Path(out).write_text(result.stdout)
    for line in result.stderr.splitlines():
        print(line)
    if result.returncode:
        raise SystemExit(f'harness exited with {result.returncode}')
    final = [line for line in result.stdout.splitlines() if line.startswith('final ')]
    print(f'X4 Linux Jolt redirect={redirect or "none"} jobs={jobs}: {final[0] if final else "no final line"}')


def run(variant, frames, jobs, out):
    tool = Toolchain(variant)
    exe = tool.out / ('jolt_determinism_scenario.exe' if tool.windows else 'jolt_determinism_scenario')
    if not exe.exists():
        raise SystemExit(f'{exe} missing: run build first')
    text = subprocess.run([str(exe), str(frames), str(jobs)], check=True, capture_output=True, text=True).stdout
    Path(out).write_text(text)
    final = [line for line in text.splitlines() if line.startswith('final ')]
    print(f'{platform.system()} {variant} jobs={jobs}: {final[0] if final else "no final line"}')


def compare(a, b):
    left, right = Path(a).read_text().splitlines(), Path(b).read_text().splitlines()
    for i, (x, y) in enumerate(zip(left, right)):
        if x != y:
            print(f'FIRST DIFFERENCE line {i + 1}:\n  {a}: {x}\n  {b}: {y}')
            return 1
    if len(left) != len(right):
        print(f'length differs: {len(left)} versus {len(right)} lines')
        return 1
    print(f'IDENTICAL {len(left)} lines, {left[-1]}')
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    sub.add_parser('fetch')
    b = sub.add_parser('build')
    b.add_argument('--variant', choices=('deterministic', 'control', 'mapping'), default='deterministic')
    b.add_argument('--threads', type=int, default=os.cpu_count() or 4)
    b.add_argument('--cxx')
    r = sub.add_parser('run')
    r.add_argument('--variant', choices=('deterministic', 'control'), default='deterministic')
    r.add_argument('--frames', type=int, default=600)
    r.add_argument('--jobs', type=int, default=4)
    r.add_argument('--out', required=True)
    h = sub.add_parser('x4-harness', help="Linux: build the scenario against the owned X4 executable's own Jolt")
    h.add_argument('--x4', required=True, type=Path)
    h.add_argument('--cxx')
    h.add_argument('--redirect', action='store_true', help='also link our deterministic Jolt (namespace QPH) for X4_JOLT_REDIRECT')
    x = sub.add_parser('x4-run', help='Linux: run the x4-harness scenario against the owned X4 executable')
    x.add_argument('--x4', required=True, type=Path)
    x.add_argument('--redirect', help="'all' or comma-separated Jolt name fragments (needs x4-harness --redirect)")
    x.add_argument('--frames', type=int, default=600)
    x.add_argument('--jobs', type=int, default=4)
    x.add_argument('--out', required=True)
    c = sub.add_parser('compare')
    c.add_argument('a')
    c.add_argument('b')
    args = p.parse_args()
    if args.command == 'fetch':
        print(fetch())
    elif args.command == 'build':
        fetch()
        print(Toolchain(args.variant, args.cxx).build(args.threads))
    elif args.command == 'x4-harness':
        fetch()
        print(x4_harness(args.x4, args.cxx, args.redirect))
    elif args.command == 'x4-run':
        x4_run(args.x4, args.redirect, args.frames, args.jobs, args.out)
    elif args.command == 'run':
        run(args.variant, args.frames, args.jobs, args.out)
    else:
        sys.exit(compare(args.a, args.b))


if __name__ == '__main__':
    main()
