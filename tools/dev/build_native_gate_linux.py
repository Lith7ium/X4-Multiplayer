"""Build the opt-in Linux gate module x4lan_gate.so, separately from playable packages.

  python tools/dev/build_native_gate_linux.py --zig PATH/TO/zig-0.15.2 [--out DIR]

Cross-compiles src/native/engine/linux/gate_linux.c with Zig 0.15.2 for
x86_64-linux-gnu.2.17 (as x4lan_net.so), so it loads in the Steam Runtime and on
newer glibc hosts; runs on Windows or Linux. Tests: tools/dev/check_lockstep_gate_linux.py
(on Linux: add --zig to test this same compiler). The module is inert until the probe
calls gate.boot/gate.start, and refuses any executable other than the pinned X4 9.00 build.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src'))
from native.engine.build import compiler_path  # Zig 0.15.2 pin shared with the Windows modules

ENGINE = ROOT / 'src/native/engine'
FLAGS = ['-target', 'x86_64-linux-gnu.2.17', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
         '-Wno-misleading-indentation', '-mcx16', '-ffp-contract=off', '-fPIC', '-shared', '-fvisibility=hidden',
         '-fasynchronous-unwind-tables', '-I', str(ENGINE), '-I', str(ENGINE / 'linux')]


def build(zig, out):
    out.mkdir(parents=True, exist_ok=True)
    module = out / 'x4lan_gate.so'
    subprocess.run([compiler_path(zig), 'cc', *FLAGS, str(ENGINE / 'linux/gate_linux.c'),
                    '-ldl', '-lpthread', '-o', str(module)], check=True)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--zig', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=ROOT / '.local/native-lockstep/linux')
    args = parser.parse_args()
    module = build(args.zig, args.out)
    print(module, hashlib.sha256(module.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
