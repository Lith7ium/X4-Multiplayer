"""Check the adopted legacy formatting scope; use --fix to format those files."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).with_name('readability-files.txt')


def command_succeeded(command):
    """Let the formatter explain its own failures without hiding its exit status."""
    return subprocess.run(command, cwd=ROOT, check=False).returncode == 0


def check_xml(paths, formatter, fix):
    """Use libxml2 formatting without permitting external network access."""
    valid = True
    environment = dict(os.environ, XMLLINT_INDENT='    ')
    for path in paths:
        result = subprocess.run(
            [formatter, '--nonet', '--format', str(path)],
            capture_output=True,
            env=environment,
            check=False,
        )
        if result.returncode:
            print(result.stderr.decode('utf-8', errors='replace'), file=sys.stderr)
            valid = False
        elif path.read_bytes() != result.stdout:
            if fix:
                path.write_bytes(result.stdout)
                print('Formatted:', path.relative_to(ROOT))
            else:
                print('Needs formatting:', path.relative_to(ROOT))
                valid = False
    return valid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fix', action='store_true', help='Rewrite files instead of checking.')
    parser.add_argument('--stylua', default='stylua', help='StyLua executable (tested with 2.6.0).')
    parser.add_argument('--xmllint', default='xmllint', help='libxml2 xmllint executable.')
    parser.add_argument('files', nargs='*', help='Optional repository-relative manifest entries.')
    args = parser.parse_args()

    adopted = MANIFEST.read_text(encoding='utf-8').splitlines()
    selected = args.files or adopted
    unknown = sorted(set(selected) - set(adopted))
    if unknown:
        parser.error('Files outside the adopted formatting scope: ' + ', '.join(unknown))
    paths = [ROOT / name for name in selected]
    python_paths = [str(path) for path in paths if path.suffix == '.py']
    lua_paths = [str(path) for path in paths if path.suffix == '.lua']
    xml_paths = [path for path in paths if path.suffix == '.xml']

    # Check tool availability before --fix can partially reformat the selection.
    if lua_paths and shutil.which(args.stylua) is None:
        parser.error('StyLua is missing; install 2.6.0 or pass --stylua PATH.')
    if xml_paths and shutil.which(args.xmllint) is None:
        parser.error('xmllint is missing; install libxml2 tools or select Python/Lua files only.')
    # Resolve relative executable paths before commands switch to the repo root.
    if lua_paths:
        args.stylua = str(Path(shutil.which(args.stylua)).resolve())
    if xml_paths:
        args.xmllint = str(Path(shutil.which(args.xmllint)).resolve())
    if python_paths:
        try:
            import ruff  # noqa: F401 -- availability check, execution uses python -m below
        except ImportError:
            parser.error(
                'Install the optional formatter: python -m pip install -r requirements-style.txt'
            )

    valid = True
    if python_paths:
        command = [sys.executable, '-m', 'ruff', 'format', '--config', str(ROOT / 'ruff.toml')]
        if not args.fix:
            command.append('--check')
        valid = command_succeeded(command + python_paths) and valid
    if lua_paths:
        command = [args.stylua, '--verify', '--config-path', str(ROOT / 'stylua.toml')]
        if not args.fix:
            command.append('--check')
        valid = command_succeeded(command + lua_paths) and valid
    valid = check_xml(xml_paths, args.xmllint, args.fix) and valid
    return 0 if valid else 1


if __name__ == '__main__':
    raise SystemExit(main())
