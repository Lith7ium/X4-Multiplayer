"""Internal shared starting world: native capture, authenticated transfer and load.

Owns only its new package-local world bundle and a journalled save_010 copy in
profile 45841. Never edits content.xml, Workshop settings or normal save profiles.
A pristine starting snapshot is not a persistent multiplayer campaign.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

from x4lan import seed

TRANSFER_PORT = 45844
GAME_ARGS = [
    '-applaunch',
    '392160',
    '-personalfolderid',
    '45841',
    '-module',
    'x4lan_internal',
    '-skipintro',
    '-debug',
    'scripts',
    '-logfile',
    'x4lan-debug.txt',
    '-nocputhrottle',
    '-noworkshopsync',
]
# Isolated test profiles: 45841 for the server and player 2, 45839+K for player K.
TEST_PROFILES = tuple(str(45841 + i) for i in range(8))


def package_profile(package):
    profile = str(package.get('test_profile', '45841'))
    if profile not in TEST_PROFILES:
        raise RuntimeError('Unsupported isolated test profile in package.json.')
    return profile


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, value):
    path = Path(path)
    temp = path.with_suffix('.tmp')
    with temp.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temp.replace(path)


def replace_bytes(path, data):
    path = Path(path)
    temp = path.with_name(path.name + '.world-tmp')
    # Exclusive creation refuses a leftover temporary file from an earlier attempt.
    with temp.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    # Replace only after the new bytes have been flushed to the filesystem.
    temp.replace(path)


def digest(path):
    with Path(path).open('rb') as stream:
        return seed._digest(stream)


def content_digest(extension):
    # Platform transport binaries have their own package hashes. Shared script
    # identity must remain equal after the two packages prune the other binary.
    extension = Path(extension)
    digest = hashlib.sha256()
    for path in sorted(extension.rglob('*')):
        name = path.relative_to(extension).as_posix()
        if (
            not path.is_file()
            or name == 'ui/config.lua'
            or name.startswith('native/')
            or name.startswith('.')
        ):
            continue
        digest.update(name.encode() + b'\0' + path.read_bytes() + b'\0')
    return digest.hexdigest()


def xml_root(path):
    try:
        return ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise RuntimeError('Invalid XML in ' + str(path) + '; no settings were changed.') from exc


def local_content(game, profile):
    """Read-only preflight; the game independently checks its actual enabled list."""
    selection = Path(profile) / 'content.xml'
    enabled = {}
    if selection.exists():
        for row in xml_root(selection).findall('extension'):
            name, value = row.get('id'), row.get('enabled')
            if name in enabled or value not in ('true', 'false'):
                raise RuntimeError(
                    'Ambiguous test-profile extension selection; no selection was changed.'
                )
            enabled[name] = value == 'true'
    rows = []
    for path in sorted((Path(game) / 'extensions').glob('*/content.xml')):
        item = xml_root(path)
        name = item.get('id')
        if not name or not seed.ATOM.fullmatch(name):
            raise RuntimeError('Unresolved installed extension identity.')
        if not enabled.get(name, item.get('enabled', 'true') != 'false'):
            continue
        if name != 'x4_lan_lab' and not name.startswith('ego_dlc_'):
            raise RuntimeError(
                'Enabled extra mod: '
                + name
                + '. Installed manifest: '
                + str(path)
                + '. Multiplayer profile selection: '
                + str(selection)
                + '. Normal-profile preferences do not copy mod selections. '
                'No Workshop or profile selection was changed.'
            )
        version = item.get('version', '')
        if not re.fullmatch(r'[0-9]{1,9}', version):
            raise RuntimeError('Unresolved extension version: ' + name)
        rows.append(dict(id=name, version=str(int(version))))
    rows.sort(key=lambda row: row['id'])
    if len({row['id'] for row in rows}) != len(rows) or not any(
        row['id'] == 'x4_lan_lab' for row in rows
    ):
        raise RuntimeError('Multiplayer extension is disabled or extension IDs are duplicated.')
    return rows


def content_string(extensions):
    return ','.join(row['id'] + ':' + row['version'] for row in extensions)


def owned_installation(root, package):
    root = Path(root)
    state = read(root / 'installation.json')
    profile = Path(state['profile'])
    game = Path(state['game'])
    target = game / 'extensions/x4_lan_lab'
    if state.get('package_id') != package['package_id'] or profile.name != package_profile(package):
        raise RuntimeError('Installation belongs to a different package/profile.')
    for path in (
        profile,
        profile / 'save',
        profile / 'config.xml',
        target,
        target / 'ui/config.lua',
    ):
        if path.is_symlink():
            raise RuntimeError('Linked installation/profile paths are unsupported: ' + str(path))
    marker = target / '.x4lan-package-id'
    if not marker.is_file() or marker.read_text(encoding='utf-8-sig') != package['package_id']:
        raise RuntimeError(
            'Installed mod belongs to a different package. Use its original launcher.'
        )
    if sys.platform == 'win32':
        subprocess.run(
            [
                'powershell.exe',
                '-NoLogo',
                '-NoProfile',
                '-ExecutionPolicy',
                'Bypass',
                '-File',
                str(root / 'setup_windows.ps1'),
                '-Action',
                'CheckWorldProfile',
            ],
            check=True,
        )
    return state, target


def context(root, package, session):
    root = Path(root)
    state, target = owned_installation(root, package)
    game, profile = Path(state['game']), Path(state['profile'])
    if (game / 'version.dat').read_text().strip() != '900':
        raise RuntimeError('This package requires X4 9.00.')
    match = re.search(
        r'"buildid"\s+"(\d+)"', (game.parent.parent / 'appmanifest_392160.acf').read_text()
    )
    if not match or match[1] != package['steam_build']:
        raise RuntimeError('Installed game build differs from this package.')
    config = xml_root(profile / 'config.xml')
    if config.tag != 'root' or any(
        config.findtext(setting) != 'false' for setting in ('autosave', 'uisafemode')
    ):
        raise RuntimeError('Test profile must keep autosave=false and uisafemode=false.')
    if any(config.find(setting) is not None for setting in ('load', 'loadgame', 'module', 'mpar')):
        raise RuntimeError('Test profile has a conflicting load/start override.')
    expected_digest = content_digest(root / 'extension/x4_lan_lab')
    if content_digest(target) != expected_digest:
        raise RuntimeError('Installed multiplayer scripts differ from the package.')
    extensions = local_content(game, profile)
    if not any(row['id'] == 'ego_dlc_timelines' for row in extensions):
        raise RuntimeError(
            'Enable/install the Timelines DLC for the shared Xperimental Shuttle. No content settings were changed.'
        )
    identity = dict(
        seed_id=session['bubble'],
        game_build=session['build'],
        mod_sha256=expected_digest,
        extensions=extensions,
    )
    seed.validate_manifest(dict(protocol=1, sha256='0' * 64, size=1, format='x4-xml', **identity))
    return state, target, identity


def save_files(profile):
    folder = Path(profile) / 'save'
    if folder.is_symlink():
        raise RuntimeError('Linked test save directory is unsupported.')
    return (
        sorted(
            save_path for save_path in folder.iterdir() if save_path.name != 'steam_autocloud.vdf'
        )
        if folder.exists()
        else []
    )


def validate_slot(root, profile, manifest):
    files = save_files(profile)
    record = Path(root) / 'world/slot.json'
    if not record.is_file():
        raise RuntimeError(
            'Reserved test save has no ownership record; nothing overwritten or removed.'
        )
    owned = read(record)
    suffix = '.xml.gz' if manifest['format'] == 'x4-xml-gzip' else '.xml'
    expected = Path(profile) / 'save' / ('save_010' + suffix)
    if (
        owned != dict(path=str(expected), sha256=manifest['sha256'], seed_id=manifest['seed_id'])
        or files != [expected]
        or expected.is_symlink()
        or not expected.is_file()
        or digest(expected) != manifest['sha256']
    ):
        raise RuntimeError(
            'Test save differs from the owned starting snapshot. Existing saves were preserved.'
        )
    return expected


def stage(root, profile, bundle):
    manifest, _ = seed.read_bundle(bundle)
    record = Path(root) / 'world/slot.json'
    suffix = '.xml.gz' if manifest['format'] == 'x4-xml-gzip' else '.xml'
    expected = Path(profile) / 'save' / ('save_010' + suffix)
    if save_files(profile):
        return validate_slot(root, profile, manifest)
    # Intent is durable before writing the reserved file. An interrupted copy
    # cannot turn a preexisting file into a later removal target.
    value = dict(path=str(expected), sha256=manifest['sha256'], seed_id=manifest['seed_id'])
    if record.exists() and read(record) != value:
        raise RuntimeError('Previous reserved-slot ownership differs; preserve it for review.')
    write(record, value)
    seed.stage_test_save(bundle, profile)
    return validate_slot(root, profile, manifest)


def verify_prepared(folder, session):
    folder = Path(folder)
    if not (folder / 'prepared.json').is_file() or not (folder / 'bundle/manifest.json').is_file():
        raise RuntimeError(
            'Run 02-PREPARE-WORLD-HOST on the host before starting its server or game.'
        )
    manifest, _ = seed.read_bundle(folder / 'bundle')
    expected = dict(seed_id=session['bubble'], sha256=manifest['sha256'], native_completed=True)
    if not (folder / 'prepared.json').is_file() or read(folder / 'prepared.json') != expected:
        raise RuntimeError(
            'Run 02-PREPARE-WORLD-HOST; no matching completed native host world is available.'
        )
    if manifest['seed_id'] != session['bubble'] or manifest['game_build'] != session['build']:
        raise RuntimeError('Prepared host world belongs to another session.')
    return manifest


def verify_bundle(bundle, identity):
    manifest, _ = seed.read_bundle(bundle)
    if any(manifest[field] != expected_value for field, expected_value in identity.items()):
        raise RuntimeError('Shared world differs from this session/build/DLC/mod set.')
    return manifest


def launch(state):
    steam = state['steam']
    command = steam if isinstance(steam, list) else [steam]
    args = list(GAME_ARGS)
    profile = Path(state['profile']).name if state.get('profile') else '45841'
    if profile not in TEST_PROFILES:
        raise RuntimeError('Unsupported isolated test profile: ' + profile)
    args[args.index('-personalfolderid') + 1] = profile
    subprocess.run(command + args, check=True)


def game_closed(ensure_closed):
    try:
        ensure_closed()
        return True
    except RuntimeError:
        return False


def capture_config(identity):
    return (
        '-- Temporary shared-world preparation; normal gameplay is disabled.\nX4LAN_CONFIG = {\n'
        ' enabled=false, test_profile="45841", seed_capture=true, coop_docks=true,\n seed_id='
        + json.dumps(identity['seed_id'])
        + ',\n seed_content='
        + json.dumps(content_string(identity['extensions']))
        + ',\n}\n'
    ).encode()


def finish_capture(root, state, target, identity):
    folder = Path(root) / 'world'
    run = folder / 'capture'
    receipt = read(run / 'ownership.json')
    if receipt.get('identity') != identity:
        raise RuntimeError(
            'Capture content changed; use its original DLC/mod configuration before recovery.'
        )
    config = target / 'ui/config.lua'
    if digest(config) != receipt['capture_config_sha256'] or config.read_bytes() != capture_config(
        identity
    ):
        raise RuntimeError('Capture configuration changed; retain it for review.')
    logfile = Path(state['profile']) / 'x4lan-debug.txt'
    if not logfile.exists() or logfile.stat().st_size > 32 * 1024 * 1024:
        raise RuntimeError('Native capture log missing or exceeds limit.')
    text = logfile.read_text(errors='replace')
    shutil.copy2(logfile, run / 'native.log')
    docks = 'WORLD_CAPTURE DOCKS_MARKED id=' + identity['seed_id']
    requested = 'WORLD_CAPTURE SAVE_REQUESTED id=' + identity['seed_id']
    saved = 'WORLD_CAPTURE SAVED id=' + identity['seed_id'] + ' slot=save_010'
    exited = 'WORLD_CAPTURE EXIT id=' + identity['seed_id']
    if (
        text.count(docks) != 1
        or text.count(requested) != 1
        or text.count(saved) != 1
        or text.count(exited) != 1
        or not text.index(docks) < text.index(requested) < text.index(saved) < text.index(exited)
        or 'WORLD_CAPTURE STOPPED' in text
    ):
        raise RuntimeError(
            'Native world preparation did not complete. Capture log and saves were preserved.'
        )
    files = save_files(state['profile'])
    if (
        len(files) != 1
        or files[0].name not in ('save_010.xml', 'save_010.xml.gz')
        or files[0].is_symlink()
    ):
        raise RuntimeError('Unexpected capture save inventory; nothing removed.')
    bundle = folder / 'bundle'
    if bundle.exists():
        manifest = verify_bundle(bundle, identity)
        if digest(files[0]) != manifest['sha256']:
            raise RuntimeError('Interrupted capture source differs from its sealed world.')
    else:
        manifest = seed.create_bundle(files[0], bundle, **identity)
    write(
        folder / 'slot.json',
        dict(path=str(files[0]), sha256=manifest['sha256'], seed_id=manifest['seed_id']),
    )
    previous = run / 'config-before.lua'
    if digest(previous) != receipt['before_config_sha256']:
        raise RuntimeError('Capture backup differs; retain for review.')
    replace_bytes(config, previous.read_bytes())
    write(
        folder / 'prepared.json',
        dict(seed_id=manifest['seed_id'], sha256=manifest['sha256'], native_completed=True),
    )
    return manifest


def prepare(root, package, session, ensure_closed, timeout=600):
    if package['slot'] != 'p1':
        raise RuntimeError('Only the HOST prepares a new starting world.')
    ensure_closed()
    state, target, identity = context(root, package, session)
    folder = Path(root) / 'world'
    bundle = folder / 'bundle'
    run = folder / 'capture'
    if (folder / 'prepared.json').exists():
        verify_prepared(folder, session)
        verify_bundle(bundle, identity)
        print('Shared starting world already prepared. It will not be regenerated.')
        return
    if (run / 'ownership.json').exists():
        if save_files(state['profile']):
            finish_capture(root, state, target, identity)
            print('Recovered completed native world preparation.')
            return
        receipt = read(run / 'ownership.json')
        config = target / 'ui/config.lua'
        previous = run / 'config-before.lua'
        if (
            digest(config) != receipt['capture_config_sha256']
            or digest(previous) != receipt['before_config_sha256']
        ):
            raise RuntimeError('Interrupted capture ownership changed; preserve for review.')
        logfile = Path(state['profile']) / 'x4lan-debug.txt'
        if logfile.exists():
            shutil.copy2(logfile, run / 'failed-native.log')
        replace_bytes(config, previous.read_bytes())
        run.rename(folder / ('failed-capture-' + str(time.time_ns())))
        print('Previous capture produced no save; evidence retained. Retrying preparation.')
    if save_files(state['profile']):
        raise RuntimeError(
            'World preparation requires an empty test save directory. Existing saves were preserved.'
        )
    folder.mkdir(mode=0o700, exist_ok=True)
    run.mkdir(mode=0o700)
    config = target / 'ui/config.lua'
    old = config.read_bytes()
    new = capture_config(identity)
    (run / 'config-before.lua').write_bytes(old)
    write(
        run / 'ownership.json',
        dict(
            seed_id=identity['seed_id'],
            identity=identity,
            before_config_sha256=hashlib.sha256(old).hexdigest(),
            capture_config_sha256=hashlib.sha256(new).hexdigest(),
        ),
    )
    logfile = Path(state['profile']) / 'x4lan-debug.txt'
    if logfile.exists():
        shutil.copy2(logfile, run / 'previous.log')
        logfile.unlink()
    replace_bytes(config, new)
    print(
        'Creating the shared starting world. X4 will save it and close automatically. Do not play or load another save.',
        flush=True,
    )
    launch(state)
    deadline = time.monotonic() + timeout
    observed = False
    while time.monotonic() < deadline:
        closed = game_closed(ensure_closed)
        if not closed:
            observed = True
        if (
            observed
            and not closed
            and logfile.exists()
            and logfile.stat().st_size <= 32 * 1024 * 1024
        ):
            text = logfile.read_text(errors='replace')
            begin = 'WORLD_CAPTURE BEGIN id=' + identity['seed_id']
            if begin in text and 'WORLD_CAPTURE STOPPED' in text[text.index(begin) :]:
                shutil.copy2(logfile, run / 'failed-native.log')
                failure = next(
                    line
                    for line in text[text.index(begin) :].splitlines()
                    if 'WORLD_CAPTURE STOPPED' in line
                )
                raise RuntimeError(
                    failure
                    + '. Close X4, correct the reported content/settings difference and rerun 02-PREPARE-WORLD-HOST. Evidence was retained.'
                )
        if observed and closed:
            finish_capture(root, state, target, identity)
            print(
                'Shared world ready. Start 03-START-SERVER-HOST and send JOIN-SESSION.json privately to your guest.'
            )
            return
        time.sleep(1)
    raise RuntimeError(
        'World preparation timed out. Close X4; capture evidence and any new save are preserved. Do not use the play launcher yet.'
    )


def install_play_config(root, target, identity):
    template = (Path(root) / 'extension/x4_lan_lab/ui/config.lua').read_text()
    # The inert/generated template is package-local, never a network-provided path or script.
    if 'seed_verified = false,' not in template or template.count('X4LAN_CONFIG = {') != 1:
        raise RuntimeError('Shared-world configuration template is not valid.')
    ready = template.replace(
        'seed_verified = false,',
        'seed_verified = true,\n    seed_content = '
        + json.dumps(content_string(identity['extensions']))
        + ',',
    )
    replace_bytes(target / 'ui/config.lua', ready.encode('utf-8'))


def prepare_play(root, package, session, ensure_closed):
    ensure_closed()
    state, target, identity = context(root, package, session)
    folder = Path(root) / 'world'
    folder.mkdir(mode=0o700, exist_ok=True)
    bundle = folder / 'bundle'
    if package['slot'] == 'p1':
        verify_prepared(folder, session)
    elif not (bundle / 'manifest.json').is_file():
        incoming = folder / ('incoming-' + str(time.time_ns()))
        print('Receiving the shared host world over TCP 45844...', flush=True)
        with socket.create_connection((session['server'], TRANSFER_PORT), timeout=20) as connection:
            connection.settimeout(60)
            seed.receive_bundle(connection, incoming, session['token'], **identity)
        if bundle.exists():
            raise RuntimeError('An incomplete world bundle exists; preserved for review.')
        incoming.rename(bundle)
    verify_bundle(bundle, identity)
    stage(root, state['profile'], bundle)
    install_play_config(root, target, identity)
    return state


def start(root, package, session, ensure_closed):
    state = prepare_play(root, package, session, ensure_closed)
    logfile = Path(state['profile']) / 'x4lan-debug.txt'
    if logfile.exists():
        logs = Path(root) / 'logs'
        logs.mkdir(exist_ok=True)
        shutil.copy2(logfile, logs / ('before-world-start-' + str(time.time_ns()) + '.txt'))
    print(
        'Loading the verified shared starting world. The temporary loading world is replaced automatically.',
        flush=True,
    )
    launch(state)


def remove_slot(root, state):
    folder = Path(root) / 'world'
    record = folder / 'slot.json'
    if not record.exists():
        return
    manifest, _ = seed.read_bundle(folder / 'bundle')
    owned = validate_slot(root, state['profile'], manifest)
    owned.unlink()
    record.rename(folder / ('slot-removed-' + str(time.time_ns()) + '.json'))


class WorldServer:
    """Bounded sequential private-session transfer alongside the existing UDP relay."""

    def __init__(self, root, session, bind='0.0.0.0', port=TRANSFER_PORT):
        self.bundle = Path(root) / 'world/bundle'
        self.token = session['token']
        verify_prepared(Path(root) / 'world', session)
        self.stop = threading.Event()
        self.connection = None
        self.guard = threading.Lock()
        self.listener = socket.socket()
        try:
            self.listener.bind((bind, port))
            self.listener.listen(2)
            self.listener.settimeout(0.5)
        except BaseException:
            self.listener.close()
            raise
        self.thread = threading.Thread(target=self.run, name='X4-world-transfer', daemon=True)

    def run(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with self.guard:
                if self.stop.is_set():
                    connection.close()
                    break
                self.connection = connection
            with connection:
                connection.settimeout(15)
                try:
                    seed.send_bundle(connection, self.bundle, self.token)
                    print(
                        'Shared starting world sent; guest performs its own checksum/content verification.',
                        flush=True,
                    )
                except (OSError, ValueError):
                    print('World transfer interrupted or rejected; guest can retry.', flush=True)
            with self.guard:
                self.connection = None

    def __enter__(self):
        self.thread.start()
        print('Shared world transfer ready on TCP 45844.', flush=True)
        return self

    def __exit__(self, *_):
        with self.guard:
            self.stop.set()
            self.listener.close()
            if self.connection:
                try:
                    self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        self.thread.join(3)
