"""Build INTERNAL Windows/Linux host-or-guest packages without session secrets."""

from pathlib import Path
import argparse, hashlib, json, re, secrets, shutil, subprocess, tarfile, tempfile, zipfile
import importlib.util

SOURCE = Path(__file__).resolve().parents[2]
DIST = SOURCE / 'tools/distribution'
BUILD = '23660954'
SHIP_MACRO = 'ship_ter_s_xperimental_01_a_macro'
SHIP_LOADOUT = 'x4lan_coop_xperimental'
SHIP_WEAPONS = 6


def build_framediet(zig=None):
    """Server frame diet / render RNG split module for Windows packages (ui/framediet.lua loads it)."""
    spec = importlib.util.spec_from_file_location(
        'x4lan_engine_builder', SOURCE / 'src/native/engine/build.py'
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.build_framediet_module(SOURCE, zig)


def version():
    value = (SOURCE / 'VERSION.txt').read_text().strip()
    if not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', value):
        raise ValueError('VERSION.txt must contain major.minor.patch.')
    return value


def write(path, text):
    path.write_text(text, encoding='utf-8')


def document(source, destination, release):
    write(destination, source.read_text().replace('{{VERSION}}', release))


def linux_starter(action, bundled):
    command = '"./runtime/bin/python3"' if bundled else 'python3'
    return (
        (DIST / 'linux_action.sh')
        .read_text()
        .replace('{{PYTHON}}', command)
        .replace('{{ACTION}}', action)
    )


def copy_server_runtime(root):
    """Copy the same platform-neutral server implementation into either package."""
    (root / 'x4lan').mkdir()
    for name in (
        '__init__',
        'host',
        'bubble',
        'economy',
        'coop',
        'quests',
        'missions',
        'monitor',
        'universe',
        'cargo',
        'identity',
        'npc_combat',
        'seed',
        'docking',
        'projectile_protocol',
        'projectile_relay',
        'netprobe',
        'players',
    ):
        shutil.copy2(SOURCE / 'src/x4lan' / (name + '.py'), root / 'x4lan' / (name + '.py'))


def dedicated_roles(max_players):
    """Linux server (role host, player 1) plus a Windows and a Linux client package per further player.

    Role names keep the legacy spelling (guest = player 2, guestK = player K+1). Player K
    uses isolated profile 45841 for K <= 2, otherwise 45839+K, so clients can share a PC.
    Both client packages of one slot are interchangeable: same role, profile and pair.
    """
    if not 2 <= max_players <= 8:
        raise ValueError('--max-players must be from 2 to 8.')
    roles = [('Linux', 'host', '45841')]
    for guest_index in range(1, max_players):
        role, profile = (
            ('guest', '45841')
            if guest_index == 1
            else ('guest%d' % guest_index, str(45840 + guest_index))
        )
        roles += [('Windows11', role, profile), ('Linux', role, profile)]
    return roles


def package_name(release, platform, role):
    """Windows11[-GuestK] (legacy) for Windows clients; Linux is the server, Linux-Client[K] a client."""
    if role == 'host':
        return f'X4-LAN-{release}-{platform}'
    if platform == 'Linux':
        return f'X4-LAN-{release}-Linux-Client' + role[5:]
    return f'X4-LAN-{release}-{platform}' + ('-Guest' + role[5:] if role != 'guest' else '')


def build(
    destination: Path,
    runtime_cache: Path | None = None,
    dedicated=False,
    max_players=3,
    zig: Path | None = None,
):
    release = version()
    reports = [
        SOURCE / f'reports/{name}-{release}-{suffix}.txt'
        for name, suffix in [('automated', 'tests'), ('release', 'checks')]
    ]
    for report in reports:
        if not report.is_file():
            raise RuntimeError('Record exact release checks before packaging: ' + str(report))
    runtimes = json.loads((DIST / 'public/runtimes.json').read_text())
    if runtime_cache:
        for platform in ('Linux', 'Windows'):
            record = runtimes[platform]
            if (
                hashlib.sha256((runtime_cache / record['file']).read_bytes()).hexdigest()
                != record['sha256']
            ):
                raise ValueError('Runtime checksum mismatch: ' + platform)
        if not (runtime_cache / 'licenses').is_dir():
            raise ValueError('Runtime licenses are required.')
    framediet_module = build_framediet(zig)
    destination.mkdir(parents=True, exist_ok=False)
    pair_id = 'internal-dist-' + secrets.token_hex(8)
    folders, archives = [], []
    roles = (
        dedicated_roles(max_players)
        if dedicated
        else [('Linux', 'host', '45841'), ('Windows11', 'guest', '45841')]
    )
    if dedicated:
        # One Rust build per set: the server binary plus the x4lan-net module that every
        # Linux package of this set (server and clients) loads.
        subprocess.run(
            ['cargo', 'build', '--release', '--bin', 'x4lan-dedicated'],
            cwd=SOURCE / 'src/rust',
            check=True,
        )
        subprocess.run(
            ['cargo', 'build', '--release', '-p', 'x4lan-net'], cwd=SOURCE / 'src/rust', check=True
        )
    for platform, role, profile in roles:
        root = destination / (
            package_name(release, platform, role) if dedicated else f'X4-LAN-{release}-{platform}'
        )
        root.mkdir()
        folders.append(root)
        package = dict(
            package_id=None,
            pair_id=pair_id,
            slot=None,
            platform=platform,
            role=role,
            dedicated=dedicated,
            release=7,
            version=release,
            channel='internal',
            steam_build=BUILD,
            game_version='9.00 (611726)',
            test_profile=profile,
            runtime_bundled=bool(runtime_cache),
            roles=list(dict.fromkeys(role_entry[1] for role_entry in roles)),
        )
        if dedicated:
            package['max_players'] = max_players
        write(root / 'package.json', json.dumps(package, indent=2) + '\n')
        write(root / 'VERSION.txt', release + '\n')
        extension = root / 'extension/x4_lan_lab'
        shutil.copytree(SOURCE / 'src/extension/x4_lan_lab', extension)
        content = extension / 'content.xml'
        write(
            content,
            content.read_text().replace(
                'name="X4 LAN Lab (experimental)"', f'name="X4 LAN Lab {release} (experimental)"'
            ),
        )
        for name in (
            'aiscripts/x4lan.interaction.fire.xml',
            'aiscripts/x4lan.interaction.trade.xml',
            'md/x4lan_interactions.xml',
        ):
            (extension / name).unlink()
        (
            extension / 'native' / ('x4lan_net_64.dll' if platform == 'Linux' else 'x4lan_net.so')
        ).unlink()
        if platform == 'Windows11':
            shutil.copy2(framediet_module, extension / 'native/x4lan_framediet_64.dll')
        else:
            (extension / 'native/x4lan_framediet_64.dll').unlink(missing_ok=True)
        write(
            extension / 'ui/config.lua',
            '-- Run 01-INSTALL before using this package.\nX4LAN_CONFIG = {\n'
            '    enabled = false,\n'
            + f'    ship_macro = "{SHIP_MACRO}",\n'
            + f'    ship_loadout = "{SHIP_LOADOUT}",\n    ship_weapons = {SHIP_WEAPONS},\n'
            + '}\n',
        )
        for name in ('INSTALL.html', 'README.txt', 'CHANGELOG.txt', 'NEXT-TEST.txt'):
            document(
                DIST / name,
                root / ('00-START-HERE.html' if name == 'INSTALL.html' else name),
                release,
            )
        shutil.copy2(SOURCE / 'docs/VERSIONING.txt', root / 'VERSIONING.txt')
        if dedicated:
            shutil.copy2(SOURCE / 'docs/DEDICATED-SERVER.md', root / 'DEDICATED-SERVER.md')
        shutil.copy2(DIST / 'WORLD-SYNC-TEST.txt', root / 'WORLD-SYNC-TEST.txt')
        for name in (
            'HUMAN-DAMAGE-TEST.txt',
            'HUMAN-DOCK-TEST.txt',
            'HUMAN-NPC-MISSION-TEST.txt',
            'HUMAN-TEST-RESULT.txt',
            'HUMAN-WALK-TEST.txt',
        ):
            document(DIST / name, root / name, release)
        (root / 'reports').mkdir()
        shutil.copy2(reports[1], root / 'reports/RELEASE-CHECKS.txt')
        shutil.copy2(reports[0], root / 'reports/AUTOMATED-TESTS.txt')
        shutil.copy2(DIST / 'session_launcher.py', root / 'launcher.py')
        shutil.copy2(DIST / 'profile_settings.py', root / 'profile_settings.py')
        shutil.copy2(DIST / 'world_launcher.py', root / 'world_launcher.py')
        shutil.copy2(DIST / 'action.py', root / 'action.py')
        copy_server_runtime(root)
        if dedicated and role == 'host':
            shutil.copy2(
                SOURCE / 'src/rust/target/release/x4lan-dedicated', root / 'x4lan-dedicated'
            )
        if dedicated and platform == 'Linux':
            shutil.copy2(
                SOURCE / 'src/rust/target/release/libx4lan_net.so',
                extension / 'native/x4lan_net.so',
            )
        if platform == 'Linux':
            shutil.copy2(DIST / 'linux_setup.py', root / 'setup_linux.py')
        else:
            shutil.copy2(DIST / 'windows_setup.ps1', root / 'setup_windows.ps1')
        if runtime_cache:
            runtime = root / 'runtime'
            if platform == 'Linux':
                with tempfile.TemporaryDirectory(prefix='x4-internal-runtime-') as temp:
                    with tarfile.open(runtime_cache / runtimes['Linux']['file']) as archive:
                        archive.extractall(temp, filter='data')
                    shutil.copytree(Path(temp) / 'python', runtime, symlinks=True)
            else:
                with zipfile.ZipFile(runtime_cache / runtimes['Windows']['file']) as archive:
                    archive.extractall(runtime)
                write(runtime / 'python314._pth', 'python314.zip\n.\n..\n')
            shutil.copytree(runtime_cache / 'licenses', root / 'licenses/python')
            write(root / 'RUNTIMES.json', json.dumps(runtimes, indent=2) + '\n')
            shutil.copy2(DIST / 'THIRD-PARTY.txt', root / 'THIRD-PARTY.txt')
        actions = {
            '01-INSTALL': 'setup',
            '02-PREPARE-WORLD-HOST': 'prepare-world',
            '03-START-SERVER-HOST': 'server',
            '04-TEST-CONNECTION': 'test',
            '05-START-X4': 'start',
            '06-DIAGNOSTICS': 'diagnose',
            '07-VERIFY-PACKAGE': 'verify',
            '08-IMPORT-SETTINGS': 'settings',
            '09-REMOVE-MOD': 'remove',
            '10-COLLECT-TEST-CONTAINER': 'collect',
        }
        for title, action in actions.items():
            if platform == 'Linux':
                path = root / (title + '.sh')
                write(path, linux_starter(action, bool(runtime_cache)))
                path.chmod(0o755)
            else:
                command = '"%~dp0runtime\\python.exe"' if runtime_cache else 'python'
                (root / (title + '.cmd')).write_bytes(
                    (
                        '@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                        + command
                        + ' -I -B "%~dp0launcher.py" '
                        + action
                        + ' %*\r\n'
                        'set "X4LAN_RESULT=%ERRORLEVEL%"\r\necho.\r\npause\r\nexit /b %X4LAN_RESULT%\r\n'
                    ).encode('ascii')
                )
        for cache in root.rglob('__pycache__'):
            shutil.rmtree(cache)
        files = sorted(file_path for file_path in root.rglob('*') if file_path.is_file())
        write(
            root / 'SHA256SUMS.txt',
            ''.join(
                hashlib.sha256(file_path.read_bytes()).hexdigest()
                + '  '
                + file_path.relative_to(root).as_posix()
                + '\n'
                for file_path in files
            ),
        )
        if platform == 'Linux':
            # Preserve runtime aliases and executable modes. ZIP follows these
            # links and duplicates the large interpreter/shared-library files.
            archive = destination / (root.name + '.tar.xz')
            with tarfile.open(archive, 'w:xz', preset=9) as tar:
                tar.add(root, arcname=root.name)
        else:
            archive = destination / (root.name + '.zip')
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
                for path in sorted(root.rglob('*')):
                    if path.is_file():
                        z.write(path, path.relative_to(destination))
        archives.append(archive)
    write(
        destination / 'PACKAGES-SHA256.txt',
        ''.join(
            hashlib.sha256(file_path.read_bytes()).hexdigest() + '  ' + file_path.name + '\n'
            for file_path in sorted(archives)
        ),
    )
    for name in (
        'INSTALL.html',
        'README.txt',
        'NEXT-TEST.txt',
        'HUMAN-NPC-MISSION-TEST.txt',
        'HUMAN-DAMAGE-TEST.txt',
        'HUMAN-DOCK-TEST.txt',
        'HUMAN-TEST-RESULT.txt',
        'HUMAN-WALK-TEST.txt',
    ):
        document(
            DIST / name,
            destination / ('00-START-HERE.html' if name == 'INSTALL.html' else name),
            release,
        )
    shutil.copy2(DIST / 'WORLD-SYNC-TEST.txt', destination / 'WORLD-SYNC-TEST.txt')
    shutil.copy2(SOURCE / 'docs/VERSIONING.txt', destination / 'VERSIONING.txt')
    (destination / 'reports').mkdir()
    shutil.copy2(reports[1], destination / 'reports/RELEASE-CHECKS.txt')
    write(destination / 'VERSION.txt', release + '\n')
    print(
        'Internal dedicated-server packages created:'
        if dedicated
        else 'Internal host-or-guest packages created:',
        destination,
    )
    return folders


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    parser.add_argument('--runtime-cache', type=Path, required=True)
    parser.add_argument('--dedicated', action='store_true')
    parser.add_argument(
        '--zig',
        type=Path,
        help='Zig 0.15.2 for the Windows frame-diet module (else X4LAN_ZIG or PATH)',
    )
    parser.add_argument(
        '--max-players',
        type=int,
        default=3,
        help='dedicated only: players including the server slot p1 (2-8, default 3)',
    )
    args = parser.parse_args()
    if args.max_players != 3 and not args.dedicated:
        parser.error('--max-players requires --dedicated')
    build(
        args.destination,
        args.runtime_cache,
        dedicated=args.dedicated,
        max_players=args.max_players,
        zig=args.zig,
    )
