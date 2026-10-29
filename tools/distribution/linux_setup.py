"""Bazzite/native-Linux package installer. Only modifies its own mod and test profile."""

from __future__ import annotations
import argparse
from datetime import datetime
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
# Default test profile. package.json test_profile selects a client's isolated profile
# (45841 for the server and player 2, 45839+K for player K), so clients can share a PC.
PROFILE = '45841'
TEST_PROFILES = tuple(str(45841 + i) for i in range(8))
# Preserve resolution/graphics/input; exclusive fullscreen stalls unfocused X4.
TEST_SETTINGS = {
    'autosave': 'false',
    'uisafemode': 'false',
    'fullscreen': 'false',
    'borderless': 'true',
}

GAME_ARGS = [
    '-applaunch',
    '392160',
    '-personalfolderid',
    PROFILE,
    '-module',
    'custom_creative',
    '-skipintro',
    '-debug',
    'scripts',
    '-logfile',
    'x4lan-debug.txt',
    '-nocputhrottle',
    '-noworkshopsync',
]


def package_profile():
    path = ROOT / 'package.json'
    package = json.loads(path.read_text(encoding='utf-8-sig')) if path.is_file() else {}
    profile = str(package.get('test_profile', PROFILE))
    if profile not in TEST_PROFILES:
        raise RuntimeError('Unsupported isolated test profile in package.json.')
    return profile


def game_args():
    args = list(GAME_ARGS)
    args[args.index('-personalfolderid') + 1] = package_profile()
    return args


def package_version():
    path = ROOT / 'VERSION.txt'
    return path.read_text().strip() if path.is_file() else 'legacy (no VERSION.txt)'


def read_state():
    path = ROOT / 'installation.json'
    if not path.exists():
        raise RuntimeError('Run 01-INSTALL.sh first.')
    return json.loads(path.read_text())


def steam_roots(home: Path):
    candidates = [
        ('native', home / '.local/share/Steam'),
        ('native', home / '.steam/steam'),
        ('flatpak', home / '.var/app/com.valvesoftware.Steam/.local/share/Steam'),
    ]
    result = []
    for kind, path in candidates:
        if not path.is_dir():
            continue
        path = path.resolve()
        if any(p == path for _, p in result):
            continue
        result.append((kind, path))
    return result


def libraries(root: Path):
    found = [root]
    path = root / 'steamapps/libraryfolders.vdf'
    if path.exists():
        for value in re.findall(r'"path"\s+"([^"]+)"', path.read_text(errors='replace')):
            p = Path(value.replace('\\\\', '\\'))
            if p not in found:
                found.append(p)
    return found


def discover(home: Path):
    found = []
    for kind, root in steam_roots(home):
        for lib in libraries(root):
            manifest = lib / 'steamapps/appmanifest_392160.acf'
            if not manifest.is_file():
                continue
            text = manifest.read_text(errors='replace')
            name = re.search(r'"installdir"\s+"([^"]+)"', text)
            if name:
                game = lib / 'steamapps/common' / name[1]
                if (game / 'X4').is_file():
                    found.append((kind, root, game.resolve()))
    return list(dict.fromkeys(found))


def check_game(game: Path, expected_build: str):
    if not (game / 'X4').is_file():
        raise RuntimeError(
            'Native Linux X4 was not found. Disable forced Proton compatibility for X4 in Steam.'
        )
    with (game / 'X4').open('rb') as stream:
        magic = stream.read(4)
    if magic != b'\x7fELF':
        raise RuntimeError('X4 is not a native Linux executable.')
    version = (game / 'version.dat').read_text().strip()
    if version != '900':
        raise RuntimeError('This package requires X4 9.00 (version.dat=900). Found: ' + version)
    manifest = game.parent.parent / 'appmanifest_392160.acf'
    match = re.search(r'"buildid"\s+"(\d+)"', manifest.read_text())
    if not match or match[1] != expected_build:
        raise RuntimeError(
            'Steam build mismatch. Both computers and this package must use build '
            + expected_build
            + '.'
        )


def ensure_closed():
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            args = (proc / 'cmdline').read_bytes().split(b'\0')
            if args and Path(os.fsdecode(args[0])).name in ('X4', 'X4.exe'):
                raise RuntimeError('X4 is still running. Close the game first.')
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            pass


def assert_test_profile(profile: Path):
    if profile.name != package_profile() or profile.is_symlink():
        raise RuntimeError('Unexpected test profile path.')
    for item in (profile / 'save', profile / 'config.xml', profile / 'config.x4lan.tmp'):
        if item.is_symlink():
            raise RuntimeError('Refusing to modify a linked test profile item: ' + str(item))
    if any((profile / 'save').glob('*.xml*')):
        raise RuntimeError(
            'The test profile contains saves. Back them up separately and use an empty test profile. They will not be changed.'
        )


def configure_profile(profile: Path, backup: Path):
    assert_test_profile(profile)
    config = profile / 'config.xml'
    if config.is_symlink():
        raise RuntimeError('Refusing to edit a linked profile configuration.')
    tree = ET.parse(config) if config.exists() else ET.ElementTree(ET.Element('root'))
    if tree.getroot().tag != 'root':
        raise RuntimeError('Unknown config.xml format.')
    if all(tree.getroot().findtext(name) == value for name, value in TEST_SETTINGS.items()):
        return
    if config.exists():
        backup.mkdir(parents=True, exist_ok=True)
        shutil.copy2(config, backup / 'config.xml')
    for name, value in TEST_SETTINGS.items():
        node = tree.getroot().find(name)
        if node is None:
            node = ET.SubElement(tree.getroot(), name)
        node.text = value
    profile.mkdir(parents=True, exist_ok=True)
    temp = profile / 'config.x4lan.tmp'
    tree.write(temp, encoding='utf-8', xml_declaration=True)
    temp.replace(config)


def install(args):
    ensure_closed()
    package = json.loads((ROOT / 'package.json').read_text())
    package_profile()  # Refuse an unsupported profile before any discovery or copy.
    found = discover(Path.home())
    if args.game:
        kind = args.steam_kind or 'native'
        game = Path(args.game).expanduser().resolve()
        candidates = [
            (k, r, g)
            for k, r, g in found
            if g == game and (not args.steam_kind or k == args.steam_kind)
        ]
        if candidates:
            kind, steamroot, game = candidates[0]
        else:
            steamroot = None
    else:
        if not found:
            raise RuntimeError(
                'X4 was not found automatically. Use: python3 setup.py install --game "/path/steamapps/common/X4 Foundations" --steam-kind native (or flatpak).'
            )
        if len(found) > 1:
            for i, (k, _, g) in enumerate(found, 1):
                print(f'{i}: {k}: {g}')
            choice = int(input('Which installation should be used? ')) - 1
            if choice not in range(len(found)):
                raise RuntimeError('Invalid selection.')
            kind, steamroot, game = found[choice]
        else:
            kind, steamroot, game = found[0]
    check_game(game, package['steam_build'])
    if kind == 'flatpak':
        profile = (
            Path.home() / '.var/app/com.valvesoftware.Steam/.config/EgoSoft/X4' / package_profile()
        )
        steam = ['flatpak', 'run', 'com.valvesoftware.Steam']
    else:
        profile = (
            Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config')))
            / 'EgoSoft/X4'
            / package_profile()
        )
        steam = ['steam']
    if not shutil.which(steam[0]):
        raise RuntimeError('Steam launcher not found: ' + steam[0])
    target = game / 'extensions/x4_lan_lab'
    if target.exists() or target.is_symlink():
        raise RuntimeError(
            'The mod folder already exists. Remove it using its original package first; existing files will not be overwritten: '
            + str(target)
        )
    backup = ROOT / 'backups' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    # Validate the profile before copying any files into the installation.
    if any((profile / 'save').glob('*.xml*')):
        raise RuntimeError('The test profile contains saves; installation cancelled.')
    (game / 'extensions').mkdir(exist_ok=True)
    shutil.copytree(ROOT / 'extension/x4_lan_lab', target)
    try:
        configure_profile(profile, backup)
        (target / '.x4lan-package-id').write_text(package['package_id'])
        state = {
            'game': str(game),
            'profile': str(profile),
            'steam': steam,
            'package_id': package['package_id'],
            'version': package_version(),
        }
        (ROOT / 'installation.json').write_text(json.dumps(state, indent=2) + '\n')
    except Exception:
        if target.exists():
            shutil.rmtree(target)  # Only the directory just created above.
        raise
    print('Installed. Existing player profiles were left untouched.')
    print('Test profile:', profile)
    print('Installation complete. Follow the role-specific next step shown by the launcher.')


def start():
    ensure_closed()
    state = read_state()
    check_game(Path(state['game']), json.loads((ROOT / 'package.json').read_text())['steam_build'])
    assert_test_profile(Path(state['profile']))
    config = ET.parse(Path(state['profile']) / 'config.xml').getroot()
    if config.findtext('autosave') != 'false' or config.findtext('uisafemode') != 'false':
        raise RuntimeError(
            'The test profile must keep autosave=false and uisafemode=false. See INSTALL.html.'
        )
    if any((Path(state['profile']) / 'save').glob('*.xml*')):
        raise RuntimeError(
            'The test profile contains saves. See INSTALL.html; do not load an existing game.'
        )
    print('Using your test profile display, graphics and control settings.')
    log = Path(state['profile']) / 'x4lan-debug.txt'
    if log.exists():
        (ROOT / 'logs').mkdir(exist_ok=True)
        shutil.copy2(
            log,
            ROOT / 'logs' / ('x4-previous-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '.txt'),
        )
    subprocess.run(state['steam'] + game_args(), check=True)
    print('Steam is starting a new creative sandbox. Do not load an existing game.')


def server(console=False):
    # A desktop-launched .sh has no terminal. Give the server a visible window.
    if not console and not sys.stdout.isatty():
        command = [sys.executable, str(ROOT / 'setup.py'), 'server', '--console']
        terminals = [
            ('konsole', ['--separate', '-e']),
            ('gnome-terminal', ['--title=X4 LAN Server', '--']),
            ('xfce4-terminal', ['--title=X4 LAN Server', '-x']),
            ('xterm', ['-T', 'X4 LAN Server', '-e']),
        ]
        for terminal, options in terminals:
            executable = shutil.which(terminal)
            if executable:
                subprocess.Popen([executable] + options + command, start_new_session=True)
                print('Opened the X4 LAN Server window.')
                return
        raise RuntimeError(
            'No supported terminal found. Open a terminal in this folder and run: bash 03-START-SERVER-HOST.sh'
        )
    sys.path.insert(0, str(ROOT))
    from x4lan.monitor import run

    cfg = json.loads((ROOT / 'session.json').read_text())
    run(cfg, ROOT / 'logs', version=package_version())


def diagnose():
    state = read_state()
    out = ROOT / 'logs'
    out.mkdir(exist_ok=True)
    log = Path(state['profile']) / 'x4lan-debug.txt'
    if log.exists():
        # Keep a full engine log as well: filtering hid the Windows loading errors.
        shutil.copy2(log, out / 'x4-debug.txt')
        lines = [l for l in log.read_text(errors='replace').splitlines() if '[X4LAN]' in l]
        (out / 'x4-mod-diagnostics.txt').write_text('\n'.join(lines) + '\n')
        print('\n'.join(lines[-12:]) or 'No X4LAN messages in the game log yet.')
    else:
        print('No game log in the test profile yet.')
    print('Diagnostics folder:', out)


def uninstall():
    ensure_closed()
    state = read_state()
    target = Path(state['game']) / 'extensions/x4_lan_lab'
    marker = target / '.x4lan-package-id'
    if not marker.is_file() or marker.read_text() != state['package_id']:
        raise RuntimeError(
            'The installed mod cannot be matched to this package; no files were removed.'
        )
    destination = ROOT / 'backups' / ('removed-mod-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    destination.parent.mkdir(exist_ok=True)
    shutil.move(str(target), destination)
    print(
        'Test mod removed from X4 and archived in the package folder. The test profile was retained; no saves were deleted.'
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', action='version', version='X4 LAN Lab ' + package_version())
    parser.add_argument('action', choices=['install', 'start', 'server', 'diagnose', 'uninstall'])
    parser.add_argument('--game')
    parser.add_argument('--steam-kind', choices=['native', 'flatpak'])
    parser.add_argument('--console', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        print('X4 LAN Lab ' + package_version(), flush=True)
        if args.action == 'install':
            install(args)
        elif args.action == 'server':
            server(args.console)
        else:
            globals()[args.action]()
    except (OSError, ValueError, RuntimeError, ET.ParseError, subprocess.SubprocessError) as exc:
        print('ERROR:', exc, file=sys.stderr)
        return 1
    finally:
        if args.console and sys.stdin.isatty():
            try:
                input('Press Enter to close this window... ')
            except (EOFError, KeyboardInterrupt):
                pass
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
