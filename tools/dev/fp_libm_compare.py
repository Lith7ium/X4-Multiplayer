"""Bitwise comparison of X4's Windows static-CRT math with a host glibc libm (offline).

Generates fixed pseudo-random inputs, executes the Windows X4.exe CRT math entries (see
fp_inventory.WINDOWS_CRT_MATH) in a unicorn x86-64 emulator, evaluates the same inputs
with the C library of a Linux host through a small nongame C harness, and reports for
every function how many results differ and the largest difference in ULPs.

Limits: unicorn has no AVX, so the Windows entries run their SSE2 path
(__use_fma3_lib = 0). Machines with FMA3+AVX run the separate FMA3 path, which this tool
does not measure. No X4 process is started; X4.exe is only read and its leaf math code
emulated. The glibc result is the host's libm, not a traced game call.

  python tools/dev/fp_libm_compare.py --windows X4.exe --workdir .local/fp-inventory/libm \
      --linux-cmd "wsl -d Ubuntu-24.04 --" [--no-fma] --json out.json
"""
import argparse
import json
import shlex
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lockstep_linux_map as lm  # noqa: E402

BASE = 0x140000000
USE_FMA3_LIB, ISA_AVAILABLE = 0x38edec0, 0x2f3f220
# name: (Windows entry RVA, binary64?, input generator, lanes)
CASES = {
    'sinf': (0x1a19480, False, ('angle',), 1), 'cosf': (0x1a19b80, False, ('angle',), 1),
    'tanf': (0x1a240d0, False, ('uniform', -1.5, 1.5), 1), 'asinf': (0x1b43ce0, False, ('uniform', -1, 1), 1),
    'acosf': (0x1a19250, False, ('uniform', -1, 1), 1), 'atanf': (0x1a23e64, False, ('uniform', -10, 10), 1),
    'atan2f': (0x1a1a398, False, ('pair', -1000, 1000), 1), 'expf': (0x1b43ed0, False, ('uniform', -20, 20), 1),
    'logf': (0x1b44940, False, ('log', 1e-6, 1e6), 1), 'log10f': (0x1b44460, False, ('log', 1e-6, 1e6), 1),
    'powf': (0x1a1a790, False, ('pow',), 1), 'fmodf': (0x1a1a080, False, ('fmod',), 1),
    'sincosf': (0x1750210, False, ('angle',), 2),
    'pow': (0x1a1e840, True, ('pow',), 1), 'sin': (0x1a26f80, True, ('uniform', -3.141592653589793, 3.141592653589793), 1),
    'cos': (0x1a260d0, True, ('uniform', -3.141592653589793, 3.141592653589793), 1),
    'exp': (0x1a26650, True, ('uniform', -20, 20), 1), 'log': (0x1a26a50, True, ('log', 1e-6, 1e6), 1),
    'atan2': (0x1a258fc, True, ('pair', -1000, 1000), 1), 'acos': (0x1a251c0, True, ('uniform', -1, 1), 1),
    'sincos': (0x1b42f20, True, ('uniform', -3.141592653589793, 3.141592653589793), 2),
}
HARNESS = r'''/* nongame: evaluate the host C library on binary input pairs */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    const char *f = argv[1]; FILE *in = fopen(argv[2], "rb"), *out = fopen(argv[3], "wb");
    int dbl = argv[4][0] == 'd';
    if (!in || !out) return 2;
    for (;;) {
        if (dbl) {
            double a[2], r[2] = {0, 0};
            if (fread(a, 8, 2, in) != 2) break;
            if (!strcmp(f, "pow")) r[0] = pow(a[0], a[1]); else if (!strcmp(f, "atan2")) r[0] = atan2(a[0], a[1]);
            else if (!strcmp(f, "sin")) r[0] = sin(a[0]); else if (!strcmp(f, "cos")) r[0] = cos(a[0]);
            else if (!strcmp(f, "exp")) r[0] = exp(a[0]); else if (!strcmp(f, "log")) r[0] = log(a[0]);
            else if (!strcmp(f, "acos")) r[0] = acos(a[0]); else if (!strcmp(f, "sincos")) sincos(a[0], &r[0], &r[1]);
            else return 3;
            fwrite(r, 8, 2, out);
        } else {
            float a[2], r[2] = {0, 0};
            if (fread(a, 4, 2, in) != 2) break;
            if (!strcmp(f, "sinf")) r[0] = sinf(a[0]); else if (!strcmp(f, "cosf")) r[0] = cosf(a[0]);
            else if (!strcmp(f, "tanf")) r[0] = tanf(a[0]); else if (!strcmp(f, "asinf")) r[0] = asinf(a[0]);
            else if (!strcmp(f, "acosf")) r[0] = acosf(a[0]); else if (!strcmp(f, "atanf")) r[0] = atanf(a[0]);
            else if (!strcmp(f, "atan2f")) r[0] = atan2f(a[0], a[1]); else if (!strcmp(f, "expf")) r[0] = expf(a[0]);
            else if (!strcmp(f, "logf")) r[0] = logf(a[0]); else if (!strcmp(f, "log10f")) r[0] = log10f(a[0]);
            else if (!strcmp(f, "powf")) r[0] = powf(a[0], a[1]); else if (!strcmp(f, "fmodf")) r[0] = fmodf(a[0], a[1]);
            else if (!strcmp(f, "sincosf")) sincosf(a[0], &r[0], &r[1]);
            else return 3;
            fwrite(r, 4, 2, out);
        }
    }
    return fclose(out) ? 4 : 0;
}
'''


def ulp_distance(a, b, binary64):
    """Distance in units in the last place between two same-format bit patterns (ints)."""
    bits = 64 if binary64 else 32
    def ordered(x):
        x &= (1 << bits) - 1
        return x if not x >> (bits - 1) else -(x & ((1 << (bits - 1)) - 1))
    return abs(ordered(a) - ordered(b))


def inputs(spec, n, binary64, rng):
    import numpy as np
    dt = np.float64 if binary64 else np.float32
    kind = spec[0]
    if kind == 'angle':
        x = np.concatenate([rng.uniform(-np.pi, np.pi, n), rng.uniform(-100, 100, n // 10)]); y = np.zeros_like(x)
    elif kind == 'uniform': x = rng.uniform(spec[1], spec[2], n); y = np.zeros(n)
    elif kind == 'log': x = np.exp(rng.uniform(np.log(spec[1]), np.log(spec[2]), n)); y = np.zeros(n)
    elif kind == 'pair': x = rng.uniform(spec[1], spec[2], n); y = rng.uniform(spec[1], spec[2], n)
    elif kind == 'pow': x = np.exp(rng.uniform(np.log(1e-3), np.log(10), n)); y = rng.uniform(-4, 4, n)
    elif kind == 'fmod': x = rng.uniform(-1000, 1000, n); y = rng.uniform(0.1, 50, n)
    else: raise ValueError(kind)
    out = np.empty(2 * len(x), dt); out[0::2] = x; out[1::2] = y
    return out


class Emulator:
    def __init__(self, data, isa):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
        from unicorn import x86_const as x
        self.x = x
        pe = lm.PeImage(data)
        mu = self.mu = Uc(UC_ARCH_X86, UC_MODE_64)
        mu.mem_map(BASE, (pe.image_size + 0xfff) & ~0xfff)
        for _, va, vsize, raw, raw_size, _ in pe.sections:
            if raw_size: mu.mem_write(BASE + va, data[raw:raw + min(raw_size, vsize)])
        self.stack, self.ret, teb = 0x7ff000000000, 0x7ff200000000, 0x7ff100000000
        mu.mem_map(self.stack - 0x100000, 0x200000)
        mu.mem_map(teb, 0x10000); mu.mem_write(teb + 0x30, struct.pack('<Q', teb)); mu.msr_write(0xC0000101, teb)
        mu.mem_map(self.ret, 0x1000); mu.mem_write(self.ret, b'\xf4')
        mu.mem_write(BASE + USE_FMA3_LIB, struct.pack('<I', 0))
        mu.mem_write(BASE + ISA_AVAILABLE, struct.pack('<I', isa))

    def call(self, rva, a, b):
        x, mu = self.x, self.mu
        sp = self.stack - 0x1008
        mu.mem_write(sp, struct.pack('<Q', self.ret)); mu.reg_write(x.UC_X86_REG_RSP, sp)
        mu.reg_write(x.UC_X86_REG_XMM0, a); mu.reg_write(x.UC_X86_REG_XMM1, b)
        mu.reg_write(x.UC_X86_REG_MXCSR, 0x1f80)
        mu.emu_start(BASE + rva, self.ret, count=100000)
        if mu.reg_read(x.UC_X86_REG_RIP) != self.ret: raise RuntimeError(f'{rva:#x} did not return')
        return mu.reg_read(x.UC_X86_REG_XMM0)


def wsl_path(path):
    p = str(Path(path).resolve())
    return '/mnt/' + p[0].lower() + p[2:].replace('\\', '/') if len(p) > 1 and p[1] == ':' else p


def main(argv=None):
    import numpy as np
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--windows', type=Path, required=True)
    ap.add_argument('--workdir', type=Path, required=True)
    ap.add_argument('--linux-cmd', default='', help='command prefix that runs a Linux shell command, e.g. "wsl -d Ubuntu-24.04 --"')
    ap.add_argument('--no-fma', action='store_true', help='also evaluate glibc with its FMA/AVX2 variants disabled (tunable)')
    ap.add_argument('-n', type=int, default=20000)
    ap.add_argument('--json', type=Path)
    args = ap.parse_args(argv)
    data = lm.verify_build(args.windows, lm.WINDOWS_BUILD)
    args.workdir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(611726)
    emus = {isa: Emulator(data, isa) for isa in (5, 1)}
    for name, (rva, binary64, spec, lanes) in CASES.items():
        inp = inputs(spec, args.n, binary64, rng)
        inp.tofile(args.workdir / f'in_{name}.bin')
        u = np.uint64 if binary64 else np.uint32
        words = inp.view(u)
        for isa, emu in emus.items():
            if isa == 1 and lanes == 1: continue  # only the packed kernels dispatch on __isa_available
            out = np.zeros(len(inp), u)
            for i in range(0, len(inp), 2):
                r = emu.call(rva, int(words[i]), int(words[i + 1]))
                if binary64: out[i], out[i + 1] = r & (2**64 - 1), (r >> 64) & (2**64 - 1) if lanes == 2 else 0
                else: out[i], out[i + 1] = r & 0xffffffff, (r >> 32) & 0xffffffff if lanes == 2 else 0
            out.tofile(args.workdir / f'win_isa{isa}_{name}.bin')
    (args.workdir / 'glibc_eval.c').write_text(HARNESS, encoding='utf-8')
    variants = ['default'] + (['nofma'] if args.no_fma else [])
    if args.linux_cmd:
        wd = wsl_path(args.workdir) if args.linux_cmd.startswith('wsl') else str(args.workdir)
        script = [f'cd {shlex.quote(wd)}', 'gcc -O0 -fno-builtin -o glibc_eval glibc_eval.c -lm',
                  "ldd --version | head -1 > glibc_version.txt", 'sha256sum "$(gcc -print-file-name=libm.so.6)" >> glibc_version.txt || true']
        for name, (_, binary64, _, _) in CASES.items():
            kind = 'd' if binary64 else 'f'
            script.append(f'./glibc_eval {name} in_{name}.bin lin_default_{name}.bin {kind}')
            if args.no_fma:
                script.append(f'GLIBC_TUNABLES=glibc.cpu.hwcaps=-AVX2,-FMA,-AVX2_Usable,-FMA_Usable '
                              f'./glibc_eval {name} in_{name}.bin lin_nofma_{name}.bin {kind}')
        subprocess.run(shlex.split(args.linux_cmd) + ['bash', '-c', ' && '.join(script)], check=True)
    result = dict(schema='x4-crt-vs-glibc-v1', n=args.n, windows_path='SSE2 (__use_fma3_lib=0)',
                  glibc=(args.workdir / 'glibc_version.txt').read_text().strip() if (args.workdir / 'glibc_version.txt').exists() else None,
                  functions={})
    for name, (rva, binary64, _, lanes) in CASES.items():
        u = np.uint64 if binary64 else np.uint32
        w = np.fromfile(args.workdir / f'win_isa5_{name}.bin', u)
        row = dict(windows_entry=f'{rva:#x}')
        for v in variants:
            path = args.workdir / f'lin_{v}_{name}.bin'
            if not path.exists(): continue
            g = np.fromfile(path, u)
            for lane in range(lanes):
                a, b = w[lane::2], g[lane::2]
                diff = a != b
                key = v + ('' if lanes == 1 else ('.sin' if lane == 0 else '.cos'))
                row[key] = dict(n=int(len(a)), differ=int(diff.sum()), percent=round(100 * float(diff.mean()), 3),
                                max_ulp=max((ulp_distance(int(x), int(y), binary64) for x, y in zip(a[diff], b[diff])), default=0))
        if lanes == 2:
            w1 = np.fromfile(args.workdir / f'win_isa1_{name}.bin', u)
            row['windows_isa1_vs_isa5_differ'] = int((w1 != w).sum())
        if 'nofma' in variants and (args.workdir / f'lin_nofma_{name}.bin').exists():
            row['glibc_fma_vs_nofma_differ'] = int((np.fromfile(args.workdir / f'lin_default_{name}.bin', u) !=
                                                    np.fromfile(args.workdir / f'lin_nofma_{name}.bin', u)).sum())
        result['functions'][name] = row
    text = json.dumps(result, indent=1)
    if args.json: args.json.write_text(text + '\n', encoding='utf-8')
    print(text)


if __name__ == '__main__':
    raise SystemExit(main())
