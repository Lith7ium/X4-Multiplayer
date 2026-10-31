"""Build the Windows frame-diet / render RNG split module (x4lan_framediet_64.dll) for the internal packages.

Frame-diet subset of contrib/ferrite-rw-frame-diet's native/engine/build.py: same compiler pin, flags and
generated signature header, so this branch reproduces the natively validated module byte for byte.
  python src/native/engine/build.py --zig <zig 0.15.2> [--test]   (--test runs the synthetic lifecycle tests)
"""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'src'))
from native.engine.signatures import c_header


def compiler_path(zig: Path | None) -> str:
    compiler = zig or os.environ.get('X4LAN_ZIG') or shutil.which('zig')
    if not compiler:
        raise RuntimeError('Packaging requires Zig 0.15.2; supply --zig or set X4LAN_ZIG.')
    compiler = str(Path(compiler).resolve())
    if subprocess.check_output([compiler, 'version'], text=True).strip() != '0.15.2':
        raise RuntimeError('The engine module requires Zig 0.15.2.')
    return compiler


def output_directory(root: Path) -> Path:
    out = root.resolve() / '.local' / 'engine'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'fixedtick_signatures.h').write_text(c_header())
    return out


def build_framediet_module(root: Path, zig: Path | None = None) -> Path:
    """Server frame diet / render RNG split (reports/REWRITE-FRAME-DIET.md); Windows packages bundle it."""
    root = root.resolve()
    compiler = compiler_path(zig)
    out = output_directory(root)
    module = out / 'x4lan_framediet_64.dll'
    subprocess.run([
        compiler, 'cc', '-target', 'x86_64-windows-gnu',
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared',
        '-I', str(out), str(root / 'src/native/engine/framediet.c'), '-lbcrypt', '-o', str(module),
    ], check=True)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True, type=Path)
    parser.add_argument('--test', action='store_true', help='Build/run the synthetic Windows lifecycle tests')
    args = parser.parse_args()
    module = build_framediet_module(ROOT, args.zig)
    print(module)
    if args.test:
        if os.name != 'nt':
            parser.error('--test executes a Windows binary and requires Windows')
        out = module.parent
        test = out / 'test_framediet.exe'
        subprocess.run([
            str(args.zig.resolve()), 'cc', '-target', 'x86_64-windows-gnu',
            '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(out),
            str(ROOT / 'src/native/engine/test_framediet.c'), '-lbcrypt', '-o', str(test),
        ], check=True)
        subprocess.run([str(test)], check=True)


if __name__ == '__main__':
    main()
