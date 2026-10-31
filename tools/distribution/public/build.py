"""Explicit public-only builder. Existing build_packages.py remains INTERNAL."""
from pathlib import Path
import argparse
import hashlib
import html
import json
import shutil
import tarfile
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2]
VERSION = '6.0.1'


def write(path, text):
    path.write_text(text, encoding='utf-8')


def replace_exact(text, old, new):
    if text.count(old) != 1: raise ValueError('Expected exactly one installer template occurrence: ' + old)
    return text.replace(old, new)


def stage(destination, runtime_cache):
    """Stage fresh unconfigured packages. Never generate or include credentials."""
    runtimes = json.loads((HERE / 'runtimes.json').read_text())
    for platform in ('Linux', 'Windows'):
        record = runtimes[platform]; path = runtime_cache / record['file']
        if hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
            raise ValueError('Runtime checksum mismatch: ' + str(path))
    if not (runtime_cache / 'licenses').is_dir(): raise ValueError('Runtime dependency licenses are required.')
    destination.mkdir(parents=True, exist_ok=False)
    roots = []
    for platform in ('Linux', 'Windows'):
        root = destination / f'X4-Multiplayer-Public-Test-01-{VERSION}-{platform}-x64'
        root.mkdir(); roots.append(root)
        package = dict(channel='public', test_id='PT01', version=VERSION, platform=platform,
                       slot=None, package_id='public-pt01-unconfigured', test_profile='45842',
                       steam_build='23660954', game_version='9.00 (611726)', gameplay_base='6.0.0')
        write(root / 'package.json', json.dumps(package, indent=2) + '\n')
        write(root / 'VERSION.txt', VERSION + '\n')
        shutil.copy2(HERE / 'launcher.py', root / 'launcher.py')
        shutil.copy2(HERE / 'runtimes.json', root / 'RUNTIMES.json')
        for name in ('README.txt', 'TEST-PLAN.txt', 'TEST-REPORT-TEMPLATE.txt', 'KNOWN-LIMITS.txt', 'THIRD-PARTY.txt', 'CHANGELOG.txt'):
            shutil.copy2(HERE / name, root / name)
        readme = html.escape((HERE / 'README.txt').read_text())
        write(root / 'START-HERE.html', '<!doctype html><html lang="en"><meta charset="utf-8">'
              '<meta name="viewport" content="width=device-width,initial-scale=1">'
              '<title>X4 Multiplayer Public Test 01</title><style>'
              'body{max-width:900px;margin:3em auto;padding:0 1em;font:17px/1.6 system-ui}'
              'pre{white-space:pre-wrap;font:inherit}a{color:#065ac3}</style>'
              '<h1>X4 Multiplayer - Public Test 01</h1><p>Version 6.0.1 · Experimental two-player co-op</p>'
              '<p><a href="TEST-PLAN.txt">Test plan</a> · <a href="KNOWN-LIMITS.txt">Known limits</a> · '
              '<a href="TEST-REPORT-TEMPLATE.txt">Report template</a> · <a href="RELEASE-CHECKS.txt">Verification</a></p>'
              '<pre>' + readme + '</pre></html>\n')
        extension = root / 'extension/x4_lan_lab'
        shutil.copytree(SOURCE / 'src/extension/x4_lan_lab', extension)
        for name in ('aiscripts/x4lan.interaction.fire.xml', 'aiscripts/x4lan.interaction.trade.xml', 'md/x4lan_interactions.xml'):
            (extension / name).unlink()
        (extension / 'native' / ('x4lan_net_64.dll' if platform == 'Linux' else 'x4lan_net.so')).unlink()
        content = (extension / 'content.xml').read_text().replace('X4 LAN Lab (experimental)', 'X4 Multiplayer Public Test 01 - 6.0.1')
        content = content.replace('profile 45841', 'profile 45842').replace('version="7"', 'version="8"').replace('2026-10-10','2026-10-11')
        write(extension / 'content.xml', content)
        write(extension / 'ui/config.lua', '-- Run 1-SETUP. Public downloads contain no session credentials.\n'
              'X4LAN_CONFIG = { enabled = false, test_profile = "45842" }\n')
        (root / 'x4lan').mkdir()
        for name in ('__init__.py','host.py','bubble.py','economy.py','coop.py','quests.py','monitor.py','netprobe.py'):
            shutil.copy2(SOURCE / 'src/x4lan' / name, root / 'x4lan' / name)
        shutil.copy2(SOURCE / 'tools/distribution/action.py', root / 'action.py')
        (root / 'native-source').mkdir()
        shutil.copy2(SOURCE / 'src/native/net.c', root / 'native-source/net.c')
        shutil.copytree(runtime_cache / 'licenses', root / 'licenses/python-build-standalone')
        actions = {'1-SETUP':'setup', '2-START-SERVER':'server', '2-TEST-CONNECTION':'test',
                   '3-START-X4-TEST':'start', '4-DIAGNOSTICS':'diagnose', '5-REMOVE-MOD':'remove',
                   '6-COLLECT-CONTAINER':'collect', '7-VERIFY-FILES':'verify'}
        runtime = root / 'runtime'
        if platform == 'Linux':
            text = (SOURCE / 'tools/distribution/linux_setup.py').read_text()
            text = replace_exact(text, "PROFILE = '45841'", "PROFILE = '45842'")
            text = text.replace('1-INSTALL.sh','1-SETUP.sh')
            text = text.replace('Next: 2-START-SERVER.sh, then 3-START-X4-TEST.sh.', 'Installation complete. Follow the role-specific next step below.')
            write(root / 'setup_linux.py', text)
            with tempfile.TemporaryDirectory(prefix='x4-public-runtime-') as temp:
                with tarfile.open(runtime_cache / runtimes[platform]['file']) as archive:
                    archive.extractall(temp, filter='data')
                shutil.copytree(Path(temp) / 'python', runtime, symlinks=True)
            for title, action in actions.items():
                p = root / (title + '.sh')
                write(p, '#!/usr/bin/env bash\nset -u\ncd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1\n'
                      '"./runtime/bin/python3" -I -B launcher.py ' + action + ' "$@"\nresult=$?\n'
                      'if [ -t 0 ]; then read -r -p "Press Enter to close... " answer; fi\nexit "$result"\n')
                p.chmod(0o755)
        else:
            text = (SOURCE / 'tools/distribution/windows_setup.ps1').read_text()
            text = replace_exact(text, "$ProfileId = '45841'", "$ProfileId = '45842'")
            text = text.replace('1-INSTALL.cmd','1-SETUP.cmd').replace('Linux firewall','host firewall')
            text = text.replace('Next: 2-TEST-CONNECTION.cmd, then 3-START-X4-TEST.cmd.', 'Installation complete. Follow the role-specific next step below.')
            write(root / 'setup_windows.ps1', text)
            with zipfile.ZipFile(runtime_cache / runtimes[platform]['file']) as archive:
                archive.extractall(runtime)
            write(runtime / 'python314._pth', 'python314.zip\n.\n..\n')
            for title, action in actions.items():
                data = ('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                        '"%~dp0runtime\\python.exe" -I -B "%~dp0launcher.py" ' + action + ' %*\r\n'
                        'set "X4LAN_RESULT=%ERRORLEVEL%"\r\necho.\r\npause\r\nexit /b %X4LAN_RESULT%\r\n')
                (root / (title + '.cmd')).write_bytes(data.encode('ascii'))
        # Upstream Linux ships a few encoding caches; retain portable source only.
        for cache in runtime.rglob('__pycache__'):
            shutil.rmtree(cache)
    return roots


def finalize(destination, evidence):
    """Freeze only fresh staged trees; attach actual verification before hashing."""
    if not evidence.is_file(): raise ValueError('A verification report is required.')
    roots = sorted(p for p in destination.iterdir() if p.is_dir() and p.name.startswith('X4-Multiplayer-Public-Test-01-'))
    if len(roots) != 2: raise ValueError('Expected both public package trees.')
    if any(destination.glob('*.zip')) or any(destination.glob('*.tar.gz')):
        raise ValueError('Release archives already exist; use a new release directory.')
    for root in roots:
        package = json.loads((root / 'package.json').read_text())
        if package['slot'] is not None: raise ValueError('Never publish a configured package.')
        forbidden = {'session.json','JOIN-SESSION.json','installation.json','pending-action.json','.session.lock','__pycache__','logs','backups'}
        for path in root.rglob('*'):
            if path.name in forbidden or path.suffix == '.pyc': raise ValueError('Private/generated artifact in public package: ' + str(path))
        shutil.copy2(evidence, root / 'RELEASE-CHECKS.txt')
        files = sorted(p for p in root.rglob('*') if p.is_file())
        write(root / 'SHA256SUMS.txt', ''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(root).as_posix() + '\n' for p in files))
        if package['platform'] == 'Linux':
            with tarfile.open(destination / (root.name + '.tar.gz'), 'w:gz') as archive:
                archive.add(root, arcname=root.name)
        else:
            with zipfile.ZipFile(destination / (root.name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(root.rglob('*')):
                    if path.is_file(): archive.write(path, path.relative_to(destination))
    archives = sorted([*destination.glob('*.zip'), *destination.glob('*.tar.gz')])
    write(destination / 'PACKAGES-SHA256.txt', ''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in archives))
    shutil.copy2(evidence, destination / 'RELEASE-CHECKS.txt')
    shutil.copy2(HERE / 'README.txt', destination / 'README.txt')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['stage','finalize'])
    parser.add_argument('destination', type=Path)
    parser.add_argument('--runtime-cache', type=Path)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--public-release', action='store_true', required=True,
                        help='Use only for an explicitly requested public release')
    args = parser.parse_args()
    if args.action == 'stage':
        if args.runtime_cache is None: parser.error('--runtime-cache is required for staging')
        stage(args.destination.resolve(), args.runtime_cache.resolve())
    else:
        if args.evidence is None: parser.error('--evidence is required to finalize')
        finalize(args.destination.resolve(), args.evidence.resolve())
