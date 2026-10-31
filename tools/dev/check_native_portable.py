"""Compile/run native C versus Python byte-contract vectors; synthetic only."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'src'))
from test_native_portable import contract, frame
from native_portable import SCHEMA_DESCRIPTION, SCHEMA_SHA, canonical_json, encode_frame, sha, validate_contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--cc')
    choice.add_argument('--zig', type=Path)
    args = parser.parse_args()
    if args.zig:
        from native.engine.build import compiler_path
        command = [compiler_path(args.zig), 'cc', '-target', 'x86_64-windows-gnu']
    else:
        command = [args.cc]
    out = ROOT / '.local/portable-contract'
    out.mkdir(parents=True, exist_ok=True)
    executable = out / ('test_state.exe' if os.name == 'nt' else 'test_state')
    source = ROOT / 'src/native/engine/test_lockstep_portable_state.c'
    subprocess.run(command + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
    result = subprocess.check_output([str(executable), validate_contract(contract())], text=True).strip()
    expected = encode_frame(frame(), contract())
    if bytes.fromhex(result) != expected:
        raise RuntimeError('C/Python canonical bytes differ')
    specification = json.loads((ROOT / 'src/native/engine/portable_fixture_v1.json').read_text())
    if canonical_json(specification) != canonical_json(SCHEMA_DESCRIPTION):
        raise RuntimeError('published schema differs from encoder')
    print(json.dumps({'synthetic_only': True, 'schema_sha256': SCHEMA_SHA,
                      'frame_bytes': len(expected), 'frame_sha256': sha(expected)}))


if __name__ == '__main__': main()
