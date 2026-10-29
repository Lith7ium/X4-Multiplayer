"""Offline internal launcher with Windows/Linux hosting. No downloads, game assets or global installation."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
VERSION = (
    (ROOT / 'VERSION.txt' if (ROOT / 'VERSION.txt').is_file() else ROOT.parents[1] / 'VERSION.txt')
    .read_text()
    .strip()
)
TEST_ID = 'INTERNAL'
PROFILE = '45841'
PORT = 45841
# N-player protocol tag: every new session carries it, so an older build never passes
# HELLO. Dedicated sessions also name their player count (-np1-p<N>-), which the Rust
# supervisor checks against session.json max_players.
PROTOCOL = 'np1'
BUILD = 'world1-' + VERSION + '-' + PROTOCOL + '-9.00-steam23660954'
MAX_PLAYERS = 8
# Isolated test profiles: the server and player 2 use 45841, player K uses 45839+K, so
# several clients can share one test PC (player 3 -> 45842, player 4 -> 45843, ...).
TEST_PROFILES = tuple(str(45841 + i) for i in range(MAX_PLAYERS))


def build_label(max_players=None):
    if max_players is None:
        return BUILD
    return 'world1-' + VERSION + '-' + PROTOCOL + '-p' + str(max_players) + '-9.00-steam23660954'


def role_slot(role):
    """Package role -> player slot: host p1, guest p2, guestK p(K+1) (legacy role names)."""
    if role == 'host':
        return 'p1'
    if role == 'guest':
        return 'p2'
    match = re.fullmatch(r'guest([2-7])', str(role))
    if not match:
        raise RuntimeError('Unknown dedicated package role.')
    return 'p' + str(int(match[1]) + 1)


def dedicated_setup_role(package):
    """A dedicated package fixes its setup role: the server package hosts, every client joins."""
    if not package.get('dedicated'):
        return None
    return 'host' if role_slot(package.get('role')) == 'p1' else 'guest'


def slot_profile(slot):
    number = int(slot[1:])
    return '45841' if number <= 2 else str(45839 + number)


def local_ship(slot):
    """Default shared ship: p1->1, p2->2, p3->1 (legacy), pK->K-1."""
    number = int(slot[1:])
    return {1: 1, 2: 2, 3: 1}.get(number, number - 1)


def package_profile(package):
    profile = str(package.get('test_profile', PROFILE))
    if profile not in TEST_PROFILES:
        raise RuntimeError('Unsupported isolated test profile in package.json.')
    return profile


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def internal_ip(value):
    ip = ipaddress.IPv4Address(value)
    if ip.is_loopback or ip.is_unspecified or ip.is_multicast or str(ip) == '255.255.255.255':
        raise ValueError('Use the host LAN or ZeroTier IPv4, not localhost or a broadcast address.')
    return str(ip)


def validate_invite(value):
    keys = {
        'format',
        'channel',
        'version',
        'test_id',
        'steam_build',
        'build',
        'bubble',
        'port',
        'coop',
        'token',
        'server',
        'fire2',
        'world_sync',
        'world_mode',
    }
    if not isinstance(value, dict) or set(value) not in (
        keys,
        keys | {'dedicated'},
        keys | {'dedicated', 'max_players'},
    ):
        raise ValueError('Not an internal session file.')
    if 'dedicated' in value and value['dedicated'] is not True:
        raise ValueError('Invalid dedicated-session marker.')
    players = value.get('max_players')
    if players is not None and (type(players) is not int or not 2 <= players <= MAX_PLAYERS):
        raise ValueError('Invalid player count.')
    expected = dict(
        format=1,
        channel='internal',
        version=VERSION,
        test_id=TEST_ID,
        steam_build='23660954',
        build=build_label(players),
        port=PORT,
        coop=True,
        fire2=True,
        world_sync=True,
        world_mode='replica',
    )
    if any(type(value[k]) is not type(v) or value[k] != v for k, v in expected.items()):
        raise ValueError('Session version/channel does not match Internal Test.')
    if not isinstance(value['token'], str) or not re.fullmatch('[0-9a-f]{32}', value['token']):
        raise ValueError('Invalid session key.')
    if not isinstance(value['bubble'], str) or not re.fullmatch('[0-9a-f]{32}', value['bubble']):
        raise ValueError('Invalid session identity.')
    internal_ip(value['server'])
    return value


@contextmanager
def session_lock(filename=".session.lock"):
    # Shared by configure and server. A stale file after a crash holds no OS lock.
    with (ROOT / filename).open('a+b') as stream:
        if stream.tell() == 0:
            stream.write(b'0')
            stream.flush()
        stream.seek(0)
        try:
            if sys.platform == 'win32':
                import msvcrt

                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise RuntimeError(
                'The server or another setup action is running in this folder. Close it first.'
            ) from exc
        try:
            yield
        finally:
            stream.seek(0)
            if sys.platform == 'win32':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def backend_module(name):
    spec = importlib.util.spec_from_file_location('internal_' + name, ROOT / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if name == 'profile_settings' and (
        getattr(module, 'SETTINGS_API', None) != 1
        or not callable(getattr(module, 'import_settings', None))
        or not callable(getattr(module, 'atomic_write', None))
    ):
        raise RuntimeError(
            'Mixed launcher modules: extract the complete matching package into a new folder. '
            'Keep the existing session folder for recovery; do not overlay individual files.'
        )
    return module


def ensure_closed():
    if sys.platform == 'win32':
        result = subprocess.run(
            ['tasklist.exe', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'],
            capture_output=True,
            check=True,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        # The ASCII executable name needs no decoding of localized console bytes.
        if b'"x4.exe"' in result.stdout.lower():
            raise RuntimeError('Close X4 before changing the session or installation.')
    else:
        backend_module('setup_linux').ensure_closed()


def configured():
    package = read_json(ROOT / 'package.json')
    if package.get('channel') != 'internal' or package.get('version') != VERSION:
        raise RuntimeError('This launcher requires a Internal Test package.')
    slot = package.get('slot')
    if (
        not isinstance(slot, str)
        or not re.fullmatch(r'p[1-8]', slot)
        or not (ROOT / 'session.json').is_file()
    ):
        raise RuntimeError('Run 01-INSTALL first to create or join a session.')
    if slot not in ('p1', 'p2') and not package.get('dedicated'):
        raise RuntimeError('Player ' + slot[1:] + ' requires a dedicated-server package.')
    session = read_json(ROOT / 'session.json')
    validate_invite({k: v for k, v in session.items() if k != 'host'})
    if session.get('dedicated', False) != package.get('dedicated', False):
        raise RuntimeError('Session and package dedicated modes differ.')
    players = session.get('max_players', 3 if session.get('dedicated') else 2)
    if int(package['slot'][1:]) > players:
        raise RuntimeError(
            'This package is player '
            + package['slot'][1:]
            + ', but the session has '
            + str(players)
            + ' players.'
        )
    expected_host = '127.0.0.1' if package['slot'] == 'p1' else session['server']
    if session.get('host') != expected_host:
        raise RuntimeError('Session role/address mismatch. Remove the mod and run setup again.')
    if package.get('package_id') != session_package_id(session, package['slot']):
        raise RuntimeError(
            'Package ownership does not match the saved session. Preserve this folder for recovery.'
        )
    return package, session


def session_package_id(session, slot):
    return 'lan-' + hashlib.sha256(session['token'].encode()).hexdigest()[:16] + '-' + slot


def session_config(session, slot, host, dedicated=False, control_path=None, profile=PROFILE):
    cfg = dict(
        enabled=True,
        interaction_test=False,
        bubble_actions=True,
        trading=True,
        walking_avatars=True,  # Internal: on-foot avatars (walk_game.lua); fail closed with WALK_* diagnostics.
        test_profile=profile,
        host=host,
        port=PORT,
        token=session['token'],
        build=session['build'],
        bubble=session['bubble'],
        coop=True,
        fire2=True,
        world_sync=True,
        world_mode='replica',
        ship_macro='ship_ter_s_xperimental_01_a_macro',
        ship_loadout='x4lan_coop_xperimental',
        ship_weapons=6,
        seed_id=session['bubble'],
        seed_verified=False,
        coop_docks=True,
        pilot_health=True,
        shot_claims=True,
        shot_visual=True,
        shot_capture=True,
        npc_combat=True,
        faction_missions=True,
        npc_target_diag=True,
        npc_motion_diagnostics=1,
        motion_diagnostics=1,
        projectile_visuals=True,
        shot_undo=False,
        frame_profile=True,
        net_probe=True,  # On by default in internal packages (dev rule); set false only if they break.
        player_factions=True,  # Own faction per human (pool x4lan_playerN); see docs/PLAYER-FACTIONS.txt.
        slot=slot,
        mode='proxy',
        proxy_macro='ship_arg_s_scout_01_a_macro',
    )
    if dedicated:
        cfg.update(dedicated_session=True, local_ship=local_ship(slot), dedicated=slot == 'p1')
        if 'max_players' in session:
            cfg['max_players'] = session['max_players']
        if slot == 'p1':
            if control_path is None:
                raise ValueError('Dedicated authority needs a local control path.')
            cfg['dedicated_control_path'] = str(control_path)
            cfg['dedicated_heartbeat_interval'] = 5
    # Engine instrumentation, on by default in internal packages (dev rule, like frame_profile above), each with
    # its own flag. Windows only (x4lan_framediet_64.dll): the render RNG split on every role for RNG parity, the
    # server frame diet only on a dedicated server (p1), never on a playing host or a client.
    windows = sys.platform == 'win32'
    cfg.update(rng_split=windows, frame_diet=windows and dedicated and slot == 'p1')
    return (
        '-- Internal Test. Private session configuration; do not publish.\nX4LAN_CONFIG = {\n'
        + ''.join('    ' + k + ' = ' + json.dumps(v) + ',\n' for k, v in cfg.items())
        + '}\n'
    )


def recover_session_metadata():
    """Repair an overlaid package from a complete same-version session, never a game/save.

    Refuse partial/conflicting ownership. Keep exact prior bytes and roll back a
    failed write; the existing invitation, token and world directory stay intact.
    """
    with session_lock():
        package = read_json(ROOT / 'package.json')
        if package.get('slot') is not None or package.get('package_id') is not None:
            return  # configured() validates explicit ownership; never guess it away.
        if package.get('channel') != 'internal' or package.get('version') != VERSION:
            raise RuntimeError('Cannot recover a session into a different package version/channel.')
        saved = read_json(ROOT / 'session.json')
        if not isinstance(saved, dict) or 'host' not in saved:
            raise RuntimeError('Incomplete session.json; no files were changed.')
        invitation = validate_invite({k: v for k, v in saved.items() if k != 'host'})
        if package.get('dedicated'):
            slot = role_slot(package.get('role'))
        elif saved['host'] == '127.0.0.1':
            slot = 'p1'
        elif saved['host'] == saved['server']:
            slot = 'p2'
        else:
            raise RuntimeError('Conflicting saved session address; no files were changed.')
        package.update(slot=slot, package_id=session_package_id(saved, slot))
        state_path = ROOT / 'installation.json'
        if state_path.exists():
            backend_module('world_launcher').owned_installation(ROOT, package)
        invite_path = ROOT / 'JOIN-SESSION.json'
        if invite_path.exists() and read_json(invite_path) != invitation:
            raise RuntimeError(
                'Existing invitation conflicts with session.json; no files were changed.'
            )
        config_path = ROOT / 'extension/x4_lan_lab/ui/config.lua'
        updates = {
            config_path: session_config(
                saved,
                slot,
                saved['host'],
                package.get('dedicated', False),
                ROOT / '.local/dedicated/control.txt',
                package_profile(package),
            ).encode('utf-8')
        }
        if slot == 'p1' and not invite_path.exists():
            updates[invite_path] = (json.dumps(invitation, indent=2) + '\n').encode('utf-8')
        updates[ROOT / 'package.json'] = (json.dumps(package, indent=2) + '\n').encode('utf-8')
        for path in (*updates, ROOT / 'session.json', state_path, invite_path):
            if path.is_symlink() or any(
                p.is_symlink() for p in path.parents if p != ROOT and ROOT in p.parents
            ):
                raise RuntimeError('Linked session metadata is unsupported; no files were changed.')
        settings = backend_module('profile_settings')
        before = {p: p.read_bytes() if p.exists() else None for p in updates}
        backup = (
            ROOT
            / 'backups'
            / ('session-recovery-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S-%fZ'))
        )
        backup.mkdir(parents=True)
        for path, data in before.items():
            if data is not None:
                target = backup / path.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        write_json(
            backup / 'before.json',
            {p.relative_to(ROOT).as_posix(): b is not None for p, b in before.items()},
        )
        changed = []
        try:
            for path, data in updates.items():
                if (
                    path.is_symlink()
                    or (path.read_bytes() if path.exists() else None) != before[path]
                ):
                    raise RuntimeError('Session metadata changed during recovery; retry.')
                settings.atomic_write(path, data)
                changed.append(path)
        except Exception as error:
            failures = []
            for path in reversed(changed):
                try:
                    if before[path] is None:
                        path.unlink()
                    else:
                        settings.atomic_write(path, before[path])
                except OSError as exc:
                    failures.append(str(exc))
            if failures:
                raise RuntimeError(
                    'Recovery rollback incomplete. Preserve backup: ' + str(backup)
                ) from error
            raise
    print('Recovered existing session metadata; session key and installed world are unchanged.')


def configure(role, server=None, invite=None):
    # Only uninstalled package-local templates change here. Server-only setup is
    # safe while an unrelated X4 instance runs; installation still checks X4.
    with session_lock():
        if (ROOT / 'installation.json').exists():
            raise RuntimeError(
                'Remove the installed mod with 09-REMOVE-MOD before changing roles or sessions.'
            )
        if (ROOT / 'session.json').exists():
            raise RuntimeError(
                'This folder already has a session. Reuse it, or use a fresh extracted package for a new session.'
            )
        package = read_json(ROOT / 'package.json')
        fixed = dedicated_setup_role(package)
        if fixed and role != fixed:
            raise RuntimeError(
                'This dedicated '
                + ('server' if fixed == 'host' else 'client')
                + ' package cannot be configured as '
                + role
                + '. Use the matching package.'
            )
        if role == 'host':
            players = package.get('max_players') if package.get('dedicated') else None
            if players is not None and (
                type(players) is not int or not 2 <= players <= MAX_PLAYERS
            ):
                raise RuntimeError('Invalid max_players in package.json.')
            session = dict(
                format=1,
                channel='internal',
                version=VERSION,
                test_id=TEST_ID,
                steam_build='23660954',
                build=build_label(players),
                bubble=secrets.token_hex(16),
                port=PORT,
                coop=True,
                fire2=True,
                world_sync=True,
                world_mode='replica',
                token=secrets.token_hex(16),
                server=internal_ip(server),
            )
            if players is not None:
                session['max_players'] = players
        elif role == 'guest':
            path = Path(invite).expanduser()
            if path.stat().st_size > 8192:
                raise ValueError('Session file is too large.')
            session = validate_invite(read_json(path))
        else:
            raise ValueError('Choose host or guest.')
        if package.get('channel') != 'internal' or package.get('version') != VERSION:
            raise RuntimeError('Not a matching internal package.')
        dedicated = package.get('dedicated', False)
        if dedicated:
            package['slot'] = role_slot(package.get('role'))
            session['dedicated'] = True
            players = session.get('max_players', 3)
            if int(package['slot'][1:]) > players:
                raise RuntimeError(
                    'This package is player '
                    + package['slot'][1:]
                    + ', but the session has '
                    + str(players)
                    + ' players. Use a package for a lower player number.'
                )
        else:
            package['slot'] = 'p1' if role == 'host' else 'p2'
        package['package_id'] = session_package_id(session, package['slot'])
        host = '127.0.0.1' if role == 'host' else session['server']
        (ROOT / 'extension/x4_lan_lab/ui/config.lua').write_text(
            session_config(
                session,
                package['slot'],
                host,
                dedicated,
                ROOT / '.local/dedicated/control.txt',
                package_profile(package),
            ),
            encoding='utf-8',
        )
        write_json(ROOT / 'package.json', package)
        if role == 'host':
            write_json(ROOT / 'JOIN-SESSION.json', session)
        write_json(ROOT / 'session.json', {**session, 'host': host})
    print('Session configured as ' + role.upper() + ' (' + package['slot'] + ').')
    if role == 'host':
        print(
            'Send JOIN-SESSION.json privately to your other player. Keep it out of public downloads.'
        )


def platform_action(action, game=None):
    package, session = configured()
    if action in ('install', 'start', 'uninstall'):
        ensure_closed()
    if action != 'install':
        state = read_json(ROOT / 'installation.json')
        if state['package_id'] != package['package_id'] or Path(
            state['profile']
        ).name != package_profile(package):
            raise RuntimeError('Installation does not belong to this internal session/profile.')
        target = Path(state['game']) / 'extensions/x4_lan_lab'
        marker = target / '.x4lan-package-id'
        if action in ('start', 'uninstall') and (
            not marker.is_file() or marker.read_text(encoding='utf-8-sig') != package['package_id']
        ):
            raise RuntimeError(
                'The installed mod belongs to another package. Use its original launcher.'
            )
    if sys.platform == 'win32':
        names = dict(install='Install', start='Start', diagnose='Diagnose', uninstall='Remove')
        cmd = [
            'powershell.exe',
            '-NoLogo',
            '-NoProfile',
            '-ExecutionPolicy',
            'Bypass',
            '-File',
            str(ROOT / 'setup_windows.ps1'),
            '-Action',
            names[action],
        ]
        if action == 'install':
            cmd += ['-ServerIP', session['host']]
            if game:
                cmd += ['-GamePath', game]
        subprocess.run(cmd, check=True)
    else:
        module = backend_module('setup_linux')
        if action == 'install':
            module.install(argparse.Namespace(game=game, steam_kind=None))
        else:
            getattr(module, action)()
    if action == 'uninstall':
        # The backend has archived the owned mod. Retain its state as evidence.
        destination = (
            ROOT
            / 'backups'
            / ('installation-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.json')
        )
        (ROOT / 'installation.json').replace(destination)


def test_connection():
    _, cfg = configured()
    nonce = secrets.token_hex(16)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(2)
        sock.connect((cfg['host'], cfg['port']))
        for _ in range(3):
            try:
                sock.send(f'PING|1|{cfg["token"]}|{nonce}'.encode())
                if sock.recv(769) == f'PONG|1|{nonce}|{cfg["build"]}|{cfg["bubble"]}'.encode():
                    print('CONNECTION OK. Matching Internal Test session; no player slot occupied.')
                    return
            except OSError:
                continue
    raise RuntimeError(
        'No matching server reply. Check the host address, session file, server and UDP 45841 firewall rule.'
    )


def server(console=False):
    # File managers launch scripts without a TTY. Re-enter through the wrapper
    # in a real terminal, which also keeps errors visible until Enter is pressed.
    if sys.platform == 'linux' and not console and not sys.stdout.isatty():
        terminals = [
            ('konsole', ['--separate', '-e']),
            ('gnome-terminal', ['--title=X4 LAN Server', '--']),
            ('xfce4-terminal', ['--title=X4 LAN Server', '-x']),
            ('xterm', ['-T', 'X4 LAN Server', '-e']),
        ]
        for terminal, options in terminals:
            executable = shutil.which(terminal)
            if executable:
                subprocess.Popen(
                    [
                        executable,
                        *options,
                        'bash',
                        str(ROOT / '03-START-SERVER-HOST.sh'),
                        '--console',
                    ],
                    cwd=ROOT,
                    start_new_session=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                print('Opened the X4 LAN Server window.')
                return
        raise RuntimeError(
            'No supported terminal found. Open a terminal in this folder and run: bash 03-START-SERVER-HOST.sh --console'
        )
    package, cfg = configured()
    if package['slot'] != 'p1':
        raise RuntimeError('Only the HOST starts the server. The GUEST uses 04-TEST-CONNECTION.')
    if package.get('dedicated'):
        backend_module('world_launcher').prepare_play(ROOT, package, cfg, ensure_closed)
        state = read_json(ROOT / 'installation.json')
        work = ROOT / '.local/dedicated'
        work.mkdir(parents=True, exist_ok=True)
        write_json(
            work / 'config.json',
            dict(
                game=str(Path(state['game']) / 'X4'),
                profile=state['profile'],
                package=str(ROOT),
                session=str(ROOT / 'session.json'),
                work=str(work),
                display=os.environ.get('DISPLAY', ':0'),
            ),
        )
        with session_lock(), backend_module('world_launcher').WorldServer(ROOT, cfg):
            subprocess.run(
                [str(ROOT / 'x4lan-dedicated'), '--config', str(work / 'config.json'), 'run'],
                check=True,
            )
        return
    from x4lan.monitor import attach, probe, run

    if probe(cfg):
        # A read-only viewer must not acquire the running server's setup lock.
        status_path = ROOT / 'logs/server-status.json'
        if not status_path.is_file():
            raise RuntimeError(
                'A matching server is running, but its status is unavailable in this folder.'
            )
        status = read_json(status_path)
        if not status.get('running') or any(
            status.get(k) != cfg[k] for k in ('build', 'bubble', 'port')
        ):
            raise RuntimeError(
                'A matching server is running, but this folder has no matching live status.'
            )
        print('Opening status viewer. Closing it leaves the server running.', flush=True)
        return attach(status_path)
    with session_lock():
        print('Internal Test server. Keep this window open. Ctrl+C stops it.', flush=True)
        with backend_module('world_launcher').WorldServer(ROOT, cfg):
            run(cfg, ROOT / 'logs', version=VERSION + ' Internal')


def diagnostics():
    package, cfg = configured()
    if (ROOT / 'installation.json').exists():
        platform_action('diagnose')
    logs = ROOT / 'logs'
    logs.mkdir(exist_ok=True)
    write_json(
        logs / 'internal-test-info.json',
        dict(
            version=VERSION,
            test_id=TEST_ID,
            channel='internal',
            platform=package['platform'],
            role=package['slot'],
            python=sys.version.split()[0],
            steam_build=package['steam_build'],
            collected_at=datetime.now(timezone.utc).isoformat(),
        ),
    )
    # Engine logs can include diagnostic payloads; scrub this session's key.
    for path in logs.iterdir():
        if path.is_file() and path.suffix in ('.txt', '.log', '.json', '.jsonl'):
            text = path.read_text(encoding='utf-8', errors='replace')
            if cfg['token'] in text:
                path.write_text(
                    text.replace(cfg['token'], '[SESSION KEY REDACTED]'), encoding='utf-8'
                )
    print('Diagnostics:', logs)
    print('Follow NEXT-TEST.txt. Logs contain local paths/IPs; review before sharing.')


def verify():
    configured_files = (
        {'package.json', 'extension/x4_lan_lab/ui/config.lua'}
        if (ROOT / 'session.json').exists()
        else set()
    )
    count = 0
    for row in (ROOT / 'SHA256SUMS.txt').read_text().splitlines():
        expected, name = row.split('  ', 1)
        path = ROOT / name
        if not path.resolve().is_relative_to(ROOT):
            raise ValueError('Invalid checksum path.')
        if name in configured_files:
            continue
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError('Missing or changed package file: ' + name)
        count += 1
    if configured_files:
        configured()
    print(
        f'PASS: {count} package files verified.'
        + (' Session-specific configuration excluded.' if configured_files else '')
    )


def import_settings(source=None, keep=False, force_select=False):
    configured()
    ensure_closed()
    validator = None
    if (ROOT / 'world/slot.json').exists():
        world = backend_module('world_launcher')
        world.owned_installation(ROOT, configured()[0])
        manifest, _ = world.seed.read_bundle(ROOT / 'world/bundle')
        validator = lambda profile: world.validate_slot(ROOT, profile, manifest)
    return backend_module('profile_settings').import_settings(
        ROOT, source=source, keep=keep, force_select=force_select, slot_validator=validator
    )


def choose_invitation():
    """Use an OS file dialog when available; never guess a session or expose its key."""
    print('Select the JOIN-SESSION.json sent privately by your host.')
    command = None
    if sys.platform == 'win32':
        # The path is returned as UTF-8 data, never interpolated into a command.
        script = r"""[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Windows.Forms
$dialog = New-Object System.Windows.Forms.OpenFileDialog
$dialog.Title = 'Select your host JOIN-SESSION.json'
$dialog.Filter = 'Session invitation (JOIN-SESSION.json)|JOIN-SESSION.json|JSON files (*.json)|*.json'
$dialog.CheckFileExists = $true
try {
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        [Console]::WriteLine($dialog.FileName)
    } else { exit 2 }
} finally { $dialog.Dispose() }
"""
        command = ['powershell.exe', '-NoProfile', '-STA', '-Command', script]
    elif os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY'):
        if shutil.which('kdialog'):
            command = [
                'kdialog',
                '--getopenfilename',
                str(ROOT),
                '*.json',
                '--title',
                'Select your host JOIN-SESSION.json',
            ]
        elif shutil.which('zenity'):
            command = [
                'zenity',
                '--file-selection',
                '--title=Select your host JOIN-SESSION.json',
                '--file-filter=JSON invitations | *.json',
            ]
    if command:
        try:
            result = subprocess.run(command, capture_output=True, encoding='utf-8', check=False)
        except OSError:
            result = None
        if result is not None and result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
        if result is not None and result.returncode in (1, 2) and not result.stderr.strip():
            raise RuntimeError('Invitation selection cancelled. Run 01-INSTALL to try again.')
        print('File selection is unavailable; paste the invitation path below.')
    value = input('Path to the host JOIN-SESSION.json: ').strip().strip('"')
    if not value:
        raise ValueError('No invitation selected. Ask the host for JOIN-SESSION.json.')
    return value


def choose_game_executable():
    """Return a selected installation path as data, before creating a session."""
    print('Select X4.exe (Steam > X4 > Properties > Installed Files > Browse).', flush=True)
    script = r'''[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Windows.Forms
$dialog = New-Object System.Windows.Forms.OpenFileDialog
$dialog.Title = 'X4 LAN - Select X4.exe'
$dialog.Filter = 'X4 executable (X4.exe)|X4.exe'
$dialog.CheckFileExists = $true
$dialog.RestoreDirectory = $true
try {
    if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        [Console]::WriteLine($dialog.FileName)
    } else { exit 2 }
} finally { $dialog.Dispose() }
'''
    try:
        result = subprocess.run(
            ['powershell.exe', '-NoLogo', '-NoProfile', '-STA', '-Command', script],
            capture_output=True,
            check=False,
        )
    except OSError:
        result = None
    if result is not None:
        if result.returncode == 0:
            try:
                return normalize_windows_game(result.stdout.decode('utf-8-sig').strip())
            except UnicodeError:
                pass  # Never substitute characters inside a filesystem path.
        elif result.returncode == 2 and not result.stderr.strip():
            raise RuntimeError('X4.exe selection cancelled; no session or installation changed.')
    print('File selection is unavailable; paste X4.exe or its installation folder.')
    return normalize_windows_game(input('X4 game path: '))


def normalize_windows_game(value):
    value = str(value).strip().strip('"')
    if not value:
        raise ValueError('Select X4.exe or the folder containing it.')
    path = Path(value).expanduser()
    if path.name.lower() == 'x4.exe':
        path = path.parent
    if not path.is_dir() or not (path / 'X4.exe').is_file():
        raise ValueError('X4.exe was not found in the selected installation folder.')
    return str(path.resolve())


def setup(args):
    game = args.game
    if not args.server_only:
        ensure_closed()
        backend_module(
            'profile_settings'
        )  # Detect an incomplete module update before setup writes.
        if sys.platform == 'win32':
            if game:
                game = normalize_windows_game(game)
            elif not (ROOT / 'installation.json').exists():
                game = choose_game_executable()
    if (ROOT / 'session.json').exists():
        recover_session_metadata()
    if not (ROOT / 'session.json').exists():
        package_path = ROOT / 'package.json'
        fixed = dedicated_setup_role(read_json(package_path)) if package_path.is_file() else None
        if fixed and args.role is None:
            print('Dedicated ' + ('server' if fixed == 'host' else 'client') + ' package.')
        role = args.role or fixed or input('Role: [H]ost or [G]uest? ').strip().lower()
        role = {'h': 'host', 'g': 'guest'}.get(role, role)
        if role == 'host':
            from x4lan.monitor import addresses

            for row in addresses():
                print(row['kind'] + ': ' + row['ip'] + ' (' + row['interface'] + ')')
            configure(role, server=args.server or input('Your host LAN/ZeroTier IPv4: ').strip())
        elif role == 'guest':
            invite = args.invite or (
                str(ROOT / 'JOIN-SESSION.json') if (ROOT / 'JOIN-SESSION.json').is_file() else None
            )
            configure(role, invite=invite or choose_invitation())
        else:
            raise ValueError('Choose H for host or G for guest.')
    package, _ = configured()
    if not args.server_only:
        if not (ROOT / 'installation.json').exists():
            platform_action('install', game)
        else:
            state, _ = backend_module('world_launcher').owned_installation(ROOT, package)
            if game and Path(game).expanduser().resolve() != Path(state['game']).resolve():
                raise RuntimeError(
                    'Already installed in another game folder. Use 09-REMOVE-MOD before relocating.'
                )
            print('Existing owned installation:', state['game'])
        source, keep = (
            getattr(args, 'settings_profile', None),
            getattr(args, 'keep_test_settings', False),
        )
        record_path = ROOT / 'settings-import.json'
        if source or keep or not record_path.exists():
            import_settings(source, keep)
        else:
            record = read_json(record_path)
            print('Current settings source:', record.get('source') or '(kept test settings)')
            if sys.stdin.isatty():
                answer = input('Choose the settings source again? [y/N]: ').strip().lower()
                if answer in ('y', 'yes', 'j', 'ja'):
                    import_settings(force_select=True)
                elif answer not in ('', 'n', 'no', 'nein'):
                    raise ValueError('Choose Y or N.')
            else:
                print('Use 08-IMPORT-SETTINGS to select a different profile.')
    print(
        'Next: '
        + (
            '02-PREPARE-WORLD-HOST, then 03-START-SERVER-HOST, then '
            if package['slot'] == 'p1'
            else '04-TEST-CONNECTION, then '
        )
        + '05-START-X4.'
    )


def main():
    parser = argparse.ArgumentParser(
        description='X4 Multiplayer Internal Test - Windows/Linux host or guest'
    )
    parser.add_argument(
        'action',
        choices=[
            'setup',
            'server',
            'test',
            'start',
            'diagnose',
            'remove',
            'collect',
            'verify',
            'version',
            'settings',
            'prepare-world',
        ],
    )
    parser.add_argument('--role', choices=['host', 'guest'])
    parser.add_argument('--server', help='Host LAN/ZeroTier IPv4 when creating a session')
    parser.add_argument('--invite', help='Path to JOIN-SESSION.json when joining')
    parser.add_argument('--game', help='Override detected game installation')
    parser.add_argument(
        '--server-only',
        action='store_true',
        help='Configure files without installing or launching X4',
    )
    parser.add_argument(
        '--console',
        action='store_true',
        help='Run the server in this console without opening a terminal',
    )
    parser.add_argument(
        '--settings-profile',
        help='Normal X4 profile folder to copy display and control preferences from',
    )
    parser.add_argument(
        '--keep-test-settings',
        action='store_true',
        help='Keep current test settings instead of importing preferences',
    )
    args = parser.parse_args()
    try:
        print('X4 Multiplayer ' + VERSION + ' - INTERNAL TEST', flush=True)
        if args.action == 'setup':
            setup(args)
        elif args.action == 'server':
            server(console=args.console)
        elif args.action == 'settings':
            import_settings(args.settings_profile, args.keep_test_settings, force_select=True)
        elif args.action == 'test':
            test_connection()
        elif args.action in ('start', 'prepare-world'):
            package, session = configured()
            with session_lock(
                '.world-operation.lock' if args.action == 'start' else '.session.lock'
            ):
                world = backend_module('world_launcher')
                action = world.start if args.action == 'start' else world.prepare
                action(ROOT, package, session, ensure_closed)
        elif args.action == 'remove':
            configured()
            ensure_closed()
            # Validate the backend ownership before removing the reserved snapshot.
            state = read_json(ROOT / 'installation.json')
            backend_module('world_launcher').owned_installation(ROOT, configured()[0])
            backend_module('world_launcher').remove_slot(ROOT, state)
            platform_action('uninstall')
        elif args.action == 'diagnose':
            diagnostics()
        elif args.action == 'collect':
            configured()
            return backend_module('action').run('PICKUP')
        elif args.action == 'verify':
            verify()
        return 0
    except KeyboardInterrupt:
        print('\nStopped.')
        return 130
    except EOFError:
        print(
            'ERROR: Interactive input is unavailable. Open a terminal in this package folder and run '
            'bash 01-INSTALL.sh (Linux) or 01-INSTALL.cmd (Windows).',
            file=sys.stderr,
        )
        return 1
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.SubprocessError) as exc:
        print('ERROR: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
