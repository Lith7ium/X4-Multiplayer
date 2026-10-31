"""Compile/run the shared C wire/RNG contract on this host, without launching X4.

Linux: python3 tools/dev/check_lockstep_portable.py --cc gcc
Windows: python tools/dev/check_lockstep_portable.py --zig PATH/zig.exe
Equal contract hashes check these integer/wire vectors, not engine determinism.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    compiler = parser.add_mutually_exclusive_group()
    compiler.add_argument('--cc', default=None)
    compiler.add_argument('--zig', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--compare', type=Path, help='require the same contract and source hashes as another host report')
    args = parser.parse_args()
    if args.zig:
        exe = str(args.zig.resolve())
        version = subprocess.check_output([exe, 'version'], text=True).strip()
        if version != '0.15.2':
            parser.error('the project Zig compiler must be version 0.15.2')
        target = 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-gnu'
        command = [exe, 'cc', '-target', target]
    else:
        exe = shutil.which(args.cc or 'cc')
        if not exe:
            parser.error('C compiler missing; provide --cc or --zig')
        version = subprocess.check_output([exe, '--version'], text=True).splitlines()[0]
        command = [exe]
    directory = ROOT / '.local/portable-lockstep' / platform.system().lower()
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / ('contract.exe' if os.name == 'nt' else 'contract')
    source = ROOT / 'src/native/engine/test_lockstep_portable.c'
    subprocess.run(command + ['-std=c11', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                             str(source), '-o', str(binary)], check=True)
    contract = json.loads(subprocess.check_output([str(binary)], text=True))
    canonical = json.dumps(contract, sort_keys=True, separators=(',', ':')).encode('ascii')
    report = {
        'scope': 'C integer RNG schema and native gate wire parsing; no native simulation proof',
        'platform': platform.platform(), 'compiler': version,
        'contract': contract, 'contract_sha256': hashlib.sha256(canonical).hexdigest(),
        'source_hash_normalization': 'CRLF-to-LF',
        'sources': {p.name: hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
                    for p in (source, source.with_name('lockstep_wire.h'), source.with_name('lockstep_seed_core.h'))},
    }
    path = args.report or directory / 'result.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2))
    if args.compare:
        other = json.loads(args.compare.read_text(encoding='utf8'))
        for key in ('contract', 'contract_sha256', 'source_hash_normalization', 'sources'):
            if other.get(key) != report[key]:
                raise SystemExit(f'FAIL cross-host {key} mismatch: {args.compare}')
        print('PASS same contract and source hashes as comparison host')


if __name__ == '__main__':
    main()
