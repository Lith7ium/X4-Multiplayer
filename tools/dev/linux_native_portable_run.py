"""Isolated Linux participant for the reviewed two-native portable baseline.

The caller supplies a frozen review manifest and a private coordinator config.
No fallback peer, service deployment, Steam launch or normal profile mutation.
"""
import argparse
import ctypes
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import select
import shutil
import signal
import stat
import subprocess
import sys
import time

sys.dont_write_bytecode = True  # The reviewed source tree remains byte-for-byte inert.
import linux_native_admission_run as isolation
import linux_portable_prepare as preparation
from native_portable import SCHEMA_SHA, canonical_json, exact_keys, hexword, validate_contract
from native_portable_coordinator import validate_participants

MODE = 'portable-fixture12-zero-v1'
CONTRACT_SHA = '516ef8728fad1e9c99da5e3fa864d5d05366525ba7576c5256b2e9926b10bd50'
EULER_MODE = 'portable-fixture12-euler-v2'
SHARED = {'md/native_gate_battle.xml', 'md/x4lan.xml', 'ui/native_portable_loader.lua'}
SCRIPTS = {'linux_native_portable_run.py', 'linux_native_admission_run.py', 'linux_portable_probe.py',
           'linux_load_observer_probe.py', 'native_portable.py', 'native_portable_coordinator.py',
           'native_portable_ssh.py', 'validate_native_portable.py', 'linux_portable_prepare.py',
           'linux_portable_handoff.py', 'native_portable_prelaunch.py', 'linux_portable_prelaunch.py'}
PAYLOADS = {'contract.json', 'stock.json', 'save_009.xml.gz', 'bin/libx4mp_portable.so',
            'fixture/ui/native_portable_linux_live.lua', 'fixture/native/x4mp_load_probe.so'}
PAYLOADS |= {'fixture/' + path for path in SHARED} | {'bin/' + path for path in SCRIPTS}


def reviewed_capability(contract):
    digest = validate_contract(contract)
    if 'import_math' in contract:
        import portable_import_capability as cap
        if digest != cap.CONTRACT_SHA256 or contract['import_math']['mode'] != EULER_MODE:
            raise ValueError('unreviewed Euler common contract')
        return dict(mode=cap.MODE, contract_sha256=digest, snapshot_sha256=cap.SNAPSHOT_SHA256,
                    import_math_sha256=cap.DESCRIPTOR_SHA256)
    if digest != CONTRACT_SHA:
        raise ValueError('unreviewed legacy common contract')
    return dict(mode=MODE, contract_sha256=digest, snapshot_sha256=contract['snapshot_sha256'])


def payloads(mode):
    if mode not in (MODE, EULER_MODE): raise ValueError('unreviewed portable mode')
    return PAYLOADS | ({'bin/portable_import_capability.py','bin/portable_import_capability_v1.py'} if mode == EULER_MODE else set())


def read_private(path, uid=0):
    path = Path(path)
    if not path.is_absolute() or path.resolve(strict=True) != path: raise ValueError('private path is not canonical')
    before = path.lstat()
    if (not stat.S_ISREG(before.st_mode) or before.st_uid != uid or before.st_nlink != 1
            or before.st_mode & 0o077 or not 0 < before.st_size <= 65536):
        raise ValueError('private file ownership, mode or size differs')
    data = path.read_bytes(); after = path.lstat()
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError('private file changed during read')
    return json.loads(data), hashlib.sha256(data).hexdigest()


def validate_session(value, contract, participant):
    capability = reviewed_capability(contract)
    extra = ('import_math_sha256',) if capability['mode'] == EULER_MODE else ()
    optional = ('prelaunch_request',) if 'prelaunch_request' in value else ()
    exact_keys(value, ('mode', 'token', 'participants', 'contract_sha256', 'ticks',
                       'startup_timeout_ms', 'timeout_ms', 'coordinator', 'remote_script') + extra + optional, 'native session')
    if any(value[key] != capability[key] for key in ('mode', 'contract_sha256') + extra):
        raise ValueError('unreviewed portable mode or common contract')
    hexword(value['token'], 32, 'private token'); validate_participants(value['participants'])
    if value['participants']['2'] != participant: raise ValueError('Linux artifact differs from declared participant')
    for key, low, high in (('ticks', 2, 120), ('startup_timeout_ms', 1000, 600000), ('timeout_ms', 1000, 120000)):
        if type(value[key]) is not int or not low <= value[key] <= high: raise ValueError('native bound differs: ' + key)
    if value['timeout_ms'] > value['startup_timeout_ms']: raise ValueError('runtime exceeds startup bound')
    ready = value['coordinator']
    exact_keys(ready, ('kind', 'platform', 'hashes', 'pid', 'start_ticks', 'port', 'report_path'), 'coordinator identity')
    if ready['kind'] != 'ready' or ready['platform'] != 'linux': raise ValueError('coordinator is not verified Linux readiness')
    for key in ('pid', 'start_ticks'):
        if type(ready[key]) is not int or ready[key] <= 1: raise ValueError('coordinator identity missing')
    if type(ready['port']) is not int or not 1024 <= ready['port'] <= 65535: raise ValueError('coordinator port invalid')
    if not all(isinstance(path, str) and path.startswith('/') and '\0' not in path
               for path in (ready['report_path'], value['remote_script'])):
        raise ValueError('coordinator paths are not absolute')
    if optional:
        if capability['mode'] != EULER_MODE: raise ValueError('Linux-first requires reviewed Euler mode')
        from native_portable_prelaunch import validate_linux_config
        validate_linux_config(value, contract)
    return value


def review_bundle(bundle, expected_sha):
    hexword(expected_sha, 64, 'frozen review digest')
    bundle = bundle.resolve(strict=True); manifest = bundle / 'review.json'
    if isolation.sha(manifest) != expected_sha: raise ValueError('unreviewed bundle manifest')
    review = json.loads(manifest.read_text())
    extra = ('import_math_sha256',) if review.get('mode') == EULER_MODE else ()
    exact_keys(review, ('mode', 'contract_sha256', 'schema_sha256', 'participant', 'files') + extra, 'review manifest')
    inventory_expected = payloads(review['mode'])
    if review['schema_sha256'] != SCHEMA_SHA:
        raise ValueError('unreviewed bundle capability')
    exact_keys(review['files'], inventory_expected, 'complete review payload')
    inventory = set()
    for path in bundle.rglob('*'):
        if path.is_symlink(): raise ValueError('linked review payload')
        if path.is_file(): inventory.add(path.relative_to(bundle).as_posix())
        elif not path.is_dir(): raise ValueError('nonregular review payload')
    if inventory != inventory_expected | {'review.json'}: raise ValueError('missing or extra review payload')
    for name, expected in review['files'].items():
        hexword(expected, 64, name)
        if isolation.sha(bundle / name) != expected: raise ValueError('changed review payload: ' + name)
    participant = review['participant']
    exact_keys(participant, ('platform', 'executable_sha256', 'agent_sha256'), 'Linux participant')
    if (participant['platform'] != 'linux' or participant['executable_sha256'] != isolation.EXE_SHA
            or participant['agent_sha256'] != review['files']['bin/libx4mp_portable.so']):
        raise ValueError('unsupported native participant')
    contract = json.loads((bundle / 'contract.json').read_text())
    capability = reviewed_capability(contract)
    if (any(review[key] != capability[key] for key in ('mode', 'contract_sha256') + extra)
            or set(contract['fixture_files']) != SHARED):
        raise ValueError('common contract differs')
    if any(review['files']['fixture/' + name] != expected for name, expected in contract['fixture_files'].items()):
        raise ValueError('shared fixture differs')
    if review['files']['save_009.xml.gz'] != contract['snapshot_sha256']:
        raise ValueError('snapshot differs from contract')
    return review, contract


def verify_stock(bundle, contract, game):
    stock = json.loads((bundle / 'stock.json').read_text()); claimed = stock.pop('manifest_sha256')
    if (hashlib.sha256(canonical_json(stock)).hexdigest() != claimed or claimed != contract['stock_sha256']
            or stock.get('payloads_hashed') is not True or len(stock['files']) != 127 or len(stock['dlc']) != 7):
        raise ValueError('incomplete common stock manifest')
    stamps = {}
    for name, row in stock['files'].items():
        path = game / name
        if not path.resolve(strict=True).is_relative_to(game): raise ValueError('stock outside native game root')
        before = path.stat()
        if before.st_size != row['bytes'] or isolation.sha(path) != row['sha256']: raise ValueError('stock payload differs')
        after = path.stat(); stamp = (before.st_ino, before.st_size, before.st_mtime_ns)
        if stamp != (after.st_ino, after.st_size, after.st_mtime_ns): raise ValueError('stock changed during verification')
        stamps[name] = stamp
    return stamps


def verify_coordinator(config, bundle, root):
    ready = config['coordinator']; script = Path(config['remote_script']).resolve(strict=True)
    report = Path(ready['report_path'])
    if (report.resolve() != report or not script.is_relative_to(root)
            or not report.is_relative_to(script.parent / 'runs')):
        raise ValueError('coordinator outside isolated test namespace')
    mode = config.get('mode', MODE)
    payloads(mode)  # Refuse unknown modes even for isolated identity checks.
    names = ('native_portable.py', 'native_portable_coordinator.py', 'native_portable_ssh.py')
    if mode == EULER_MODE: names += ('portable_import_capability.py','portable_import_capability_v1.py')
    expected = {name: isolation.sha(bundle / 'bin' / name) for name in names}
    if ready['hashes'] != expected or script.name != 'native_portable_ssh.py':
        raise ValueError('coordinator source provenance differs')
    if any(isolation.sha(script.parent / name) != digest for name, digest in expected.items()):
        raise ValueError('remote coordinator source changed')
    pid = ready['pid']; fd = os.pidfd_open(pid)
    try:
        poll = select.poll(); poll.register(fd, select.POLLIN)
        row = isolation.processes().get(pid)
        command = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
        if (poll.poll(0) or not row or row['start'] != ready['start_ticks'] or row['uid'] != 0
                or Path(f'/proc/{pid}/exe').resolve() != Path(sys.executable).resolve()
                or command[1:] != [str(script).encode(), b'--remote', b'']):
            raise ValueError('coordinator process identity or command differs')
        sockets = set()
        for entry in Path(f'/proc/{pid}/fd').iterdir():
            try: target = os.readlink(entry)
            except OSError: continue
            match = re.fullmatch(r'socket:\[([0-9]+)\]', target)
            if match: sockets.add(match[1])
        address = f'0100007F:{ready["port"]:04X}'
        matches = [line.split() for line in Path(f'/proc/{pid}/net/udp').read_text().splitlines()[1:]
                   if len(line.split()) > 9 and line.split()[1] == address and line.split()[9] in sockets]
        if (len(matches) != 1 or poll.poll(0) or isolation.start_ticks(pid) != ready['start_ticks']
                or Path(f'/proc/{pid}/exe').resolve() != Path(sys.executable).resolve()):
            raise ValueError('coordinator does not own exact loopback endpoint')
        return fd
    except Exception:
        os.close(fd); raise


def portable_environment(base, config, agent, agent_log, ready_file, disarm, contract=None):
    if any(key.startswith(('X4MP_', 'LD_PRELOAD')) for key in base):
        raise ValueError('base environment already contains native mode')
    if contract is None:
        # Compatibility for legacy callers only; new mode always needs the
        # validated contract, never an environment-only mode switch.
        if (config['mode'] != MODE or config.get('contract_sha256') != CONTRACT_SHA
                or 'import_math_sha256' in config):
            raise ValueError('Euler environment requires validated contract')
        capability = dict(mode=MODE)
    else:
        validate_session(config, contract, config['participants']['2'])
        capability = reviewed_capability(contract)
    ready = config['coordinator']
    result = {**base, 'LD_PRELOAD': str(agent), 'X4MP_AGENT': '1', 'X4MP_AGENT_LOG': str(agent_log),
        'X4MP_AGENT_CADENCE_MS': '100', 'X4MP_AGENT_DISARM_FILE': str(disarm), 'X4MP_PORTABLE': capability['mode'],
        'X4MP_PORTABLE_TOKEN': config['token'], 'X4MP_PORTABLE_PORT': str(ready['port']),
        'X4MP_PORTABLE_READY_FILE': str(ready_file), 'X4MP_PORTABLE_AGENT_SHA256': config['participants']['2']['agent_sha256'],
        'X4MP_PORTABLE_CONTRACT_SHA256': config['contract_sha256'], 'X4MP_PORTABLE_TICKS': str(config['ticks']),
        'X4MP_PORTABLE_STARTUP_TIMEOUT_MS': str(config['startup_timeout_ms']),
        'X4MP_PORTABLE_TIMEOUT_MS': str(config['timeout_ms'])}
    if capability['mode'] == EULER_MODE:
        result.update(X4MP_PORTABLE_IMPORT_SHA256=capability['import_math_sha256'],
                      X4MP_PORTABLE_SNAPSHOT_SHA256=capability['snapshot_sha256'])
    return result


def stage_fixture(case, profile, bundle, contract, review):
    capability = reviewed_capability(contract)
    boot = case / 'extensions/x4_lan_lab'; shutil.copytree(bundle / 'fixture', boot)
    expected = {name.removeprefix('fixture/'): digest for name, digest in review['files'].items() if name.startswith('fixture/')}
    actual = {path.relative_to(boot).as_posix(): isolation.sha(path) for path in boot.rglob('*') if path.is_file()}
    if actual != expected: raise ValueError('staged fixture or native readiness bridge differs')
    shutil.copyfile(bundle / 'save_009.xml.gz', profile / 'save/save_009.xml.gz')
    if isolation.sha(profile / 'save/save_009.xml.gz') != contract['snapshot_sha256']: raise ValueError('staged snapshot differs')
    (boot / 'content.xml').write_text('<?xml version="1.0"?><content id="x4_lan_lab" name="Owned portable native baseline" version="11" save="false"><dependency version="900" /></content>\n')
    (boot / 'ui.xml').write_text('<?xml version="1.0"?><addon name="x4_lan_lab"><environment type="menus"><dependency name="ego_detailmonitor" /><file name="ui/settings.lua" /><file name="ui/native_portable_linux_live.lua" /><file name="ui/native_portable_loader.lua" /></environment></addon>\n')
    settings = {'enabled': True, **capability, 'profile': '45841', 'run': case.name,
                'save_path': str(profile / 'save'), 'schema_sha256': SCHEMA_SHA}
    (boot / 'ui/settings.lua').write_text('X4MP_PORTABLE_CONFIG={' + ','.join(
        key + '=' + ('true' if value is True else json.dumps(value)) for key, value in settings.items()) + '}\n')
    return boot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--review-sha256', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--config', type=Path)
    mode.add_argument('--prepare-request', type=Path)
    parser.add_argument('--handoff', type=Path)
    args = parser.parse_args()
    if bool(args.prepare_request) != bool(args.handoff):
        parser.error('--prepare-request and --handoff are required together')
    with ExitStack() as resources:
        run(args, resources)


def run(args, resources):
    import pwd
    if os.geteuid() != 0: raise RuntimeError('root supervisor required for owned PIDFD evidence')
    user = pwd.getpwnam('x4server'); home = Path(user.pw_dir); root = args.root.resolve(strict=True)
    if (root.parent != home or not root.name.startswith('x4mp-linux-admission-')
            or not (root / '.x4mp-admission-test-root').is_file()):
        raise ValueError('not an isolated owned native test root')
    bundle = args.bundle.resolve(strict=True)
    if not bundle.is_relative_to(root): raise ValueError('bundle outside isolated test root')
    if args.config and not args.config.resolve(strict=True).is_relative_to(root):
        raise ValueError('private config outside isolated test root')
    review, contract = review_bundle(bundle, args.review_sha256)
    if Path(__file__).resolve() != bundle / 'bin/linux_native_portable_run.py':
        raise ValueError('active runner is not the reviewed bundled source')
    capability = reviewed_capability(contract)
    prepared = None
    if args.prepare_request:
        prepared = preparation.Preparation(root, args.prepare_request, args.handoff, capability,
                                           review['participant'], args.review_sha256)
        resources.callback(prepared.close)
    else:
        config, config_sha = read_private(args.config)
        validate_session(config, contract, review['participant'])
    game = (home / 'x4/game').resolve(strict=True)
    if isolation.sha(game / 'X4') != isolation.EXE_SHA: raise ValueError('Linux executable changed')
    if any(row['name'] == 'X4' for row in isolation.processes().values()): raise ValueError('another Linux X4 is running')
    available = isolation.memory_available_bytes()
    steam = isolation.steam_identity(home, user.pw_uid)
    if prepared:
        stock_stamps = prepared.hash_stock(bundle, contract, game)
        review_bundle(bundle, args.review_sha256)
        if any(row['name'] == 'X4' for row in isolation.processes().values()) or isolation.steam_identity(home, user.pw_uid) != steam:
            raise ValueError('native admission or Steam changed during preparation')
        print('PORTABLE_PREPARED ' + json.dumps(prepared.prepared()), flush=True)
        config, config_sha = prepared.wait(lambda value: validate_session(value, contract, review['participant']))
        args.config = prepared.config_path
        prepared.stock.recheck(prepared.active_check)
        review_bundle(bundle, args.review_sha256)
        if any(row['name'] == 'X4' for row in isolation.processes().values()) or isolation.steam_identity(home, user.pw_uid) != steam:
            raise ValueError('native admission or Steam changed before handoff')
    else:
        stock_stamps = verify_stock(bundle, contract, game)
    if prepared: prepared.active_check()
    case = root / 'cases' / ('portable-native-' + secrets.token_hex(4)); case.mkdir(parents=True, exist_ok=False)
    profile = case / 'home/.config/EgoSoft/X4/45841'
    for path in (profile / 'save', case / 'tmp', case / 'cache', case / 'data', case / 'runtime',
                 case / 'game', case / 'extensions', case / 'home/.steam'):
        path.mkdir(parents=True, exist_ok=True)
    (case / 'runtime').chmod(0o700)
    (profile / 'config.xml').write_text('<?xml version="1.0"?><root><autosave>false</autosave><uisafemode>false</uisafemode><fullscreen>false</fullscreen><borderless>false</borderless><res_width>800</res_width><res_height>600</res_height><antialiasing>0</antialiasing><vsync>false</vsync></root>\n')
    for name in ('root', 'steam.pid', 'sdk32', 'sdk64'):
        source = home / '.steam' / name
        if source.exists(): (case / 'home/.steam' / name).symlink_to(source)
    (case / 'home/.steam/steam').symlink_to('root')
    dlc_binds = []
    for extension in (game / 'extensions').glob('ego_dlc_*'):
        if extension.is_dir():
            view = case / 'dlc-source' / extension.name; view.mkdir(parents=True)
            dlc_binds.append((extension.resolve(strict=True), view))
            (case / 'extensions' / extension.name).symlink_to(view)
    stage_fixture(case, profile, bundle, contract, review)
    extension_ids = sorted(path.name for path in (case / 'extensions').iterdir())
    expected_extensions = sorted([name for name in json.loads((bundle / 'stock.json').read_text())['dlc']] + ['x4_lan_lab'])
    if extension_ids != expected_extensions: raise ValueError('private extension view differs from stock manifest')
    (profile / 'content.xml').write_text('<?xml version="1.0"?><content>' + ''.join(
        '<extension id="' + name + '" enabled="true" />' for name in extension_ids) + '</content>\n')
    lock = case / 'x4.lock'; lock.touch()
    for path in [case, *case.rglob('*')]:
        if not path.is_symlink(): os.chown(path, user.pw_uid, user.pw_gid)
    case.chmod(0o700)
    display = next((n for n in range(101, 120) if not Path(f'/tmp/.X{n}-lock').exists() and not Path(f'/tmp/.X11-unix/X{n}').exists()), None)
    if display is None: raise ValueError('no unused private Xvfb display')
    auth = case / 'xauthority'; auth.touch(mode=0o600); os.chown(auth, user.pw_uid, user.pw_gid)
    if prepared: prepared.active_check()
    subprocess.run(['runuser', '-u', 'x4server', '--', 'xauth', '-f', str(auth), 'add', f':{display}', '.', secrets.token_hex(16)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=10)
    claim = case / 'claim.json'; agentlog = case / 'agent.log'; disarm = case / 'disarm'; ready_file = case / 'ready-request'
    agent = bundle / 'bin/libx4mp_portable.so'; probe = bundle / 'bin/linux_portable_probe.py'
    base_env = {'PATH': '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin', 'HOME': str(case / 'home'),
        'USER': 'x4server', 'LOGNAME': 'x4server', 'DISPLAY': f':{display}', 'XAUTHORITY': str(auth),
        'XDG_CONFIG_HOME': str(case / 'home/.config'), 'XDG_CACHE_HOME': str(case / 'cache'), 'XDG_DATA_HOME': str(case / 'data'),
        'XDG_RUNTIME_DIR': str(case / 'runtime'), 'TMPDIR': str(case / 'tmp'), 'SteamAppId': '392160', 'SteamGameId': '392160',
        'LD_LIBRARY_PATH': str(case / 'game/lib'), 'VK_ICD_FILENAMES': '/usr/share/vulkan/icd.d/lvp_icd.json',
        'VK_DRIVER_FILES': '/usr/share/vulkan/icd.d/lvp_icd.json', 'SDL_AUDIODRIVER': 'dummy', 'ALSOFT_DRIVERS': 'null'}
    environment = portable_environment(base_env, config, agent, agentlog, ready_file, disarm, contract)
    command = isolation.namespace_command(case, game, dlc_binds, lock, environment, [str(case / 'game/X4'),
        '-personalfolderid', '45841', '-skipintro', '-debug', 'scripts', '-logfile', 'native-probe.log',
        '-nocputhrottle', '-noworkshopsync', '-seed:276944566'])
    result = {**capability, 'case': str(case), 'native_linux_game': True, 'synthetic_coordinator': False,
        'native_pair_verified': False, 'review_sha256': args.review_sha256, 'private_config_sha256': config_sha,
        'schema_sha256': SCHEMA_SHA, 'game_sha256': isolation.EXE_SHA,
        'agent_sha256': review['participant']['agent_sha256'], 'stock_files_verified': len(stock_stamps),
        'steam_before': steam, 'load_tail_included': True,
        'memory_available_bytes': {'preflight': available},
        'scheduler_install_phase': 'first successful-load slot13; Windows installs fixed dt before Load'}
    if prepared:
        result['preparation'] = {**prepared.receipt, 'handoff_sha256': prepared.handoff_sha,
                                 'private_config_sha256': config_sha, 'same_supervisor': True}
    xvfb = gameproc = probeproc = None; gamepidfd = xvfbpidfd = coordinatorfd = None
    starts = {}; logs = []; gamepid = None; cancelled = False
    def cancel(_number, _frame):
        nonlocal cancelled
        cancelled = True
    if not prepared:
        for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP): signal.signal(number, cancel)
    def check_cancel():
        if prepared: prepared.active_check()
        if cancelled: raise RuntimeError('owned native participant cancelled')
    def launch(label, arguments, process_env=None, target_user=False):
        check_cancel()
        output = (case / (label + '.log')).open('wb'); logs.append(output)
        parent = os.getpid()
        def parent_death():
            if ctypes.CDLL(None).prctl(1, signal.SIGKILL) != 0 or os.getppid() != parent: os._exit(125)
        options = {'user': user.pw_uid, 'group': user.pw_gid, 'extra_groups': []} if target_user else {}
        process = subprocess.Popen(arguments, stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            start_new_session=True, env=process_env, preexec_fn=parent_death, **options)
        starts[process.pid] = isolation.start_ticks(process.pid)
        return process
    try:
        coordinatorfd = verify_coordinator(config, bundle, root)
        result['coordinator_identity'] = {key: config['coordinator'][key] for key in ('pid', 'start_ticks', 'port')}
        if any(row['name'] == 'X4' for row in isolation.processes().values()) or isolation.steam_identity(home, user.pw_uid) != steam:
            raise ValueError('native admission or Steam changed during preflight')
        if read_private(args.config)[1] != config_sha: raise ValueError('private session config changed during preflight')
        review_bundle(bundle, args.review_sha256)
        if prepared: prepared.stock.recheck(check_cancel)
        result['memory_available_bytes']['before_helpers'] = isolation.memory_available_bytes()
        xvfb = launch('xvfb', ['/usr/bin/Xvfb', f':{display}', '-screen', '0', '800x600x24', '-nolisten', 'tcp', '-noreset', '-auth', str(auth)], target_user=True)
        xvfbpidfd = os.pidfd_open(xvfb.pid)
        if Path(f'/proc/{xvfb.pid}/exe').resolve() != Path('/usr/bin/Xvfb').resolve() or isolation.processes()[xvfb.pid]['uid'] != user.pw_uid:
            raise ValueError('private Xvfb identity differs')
        result['private_xvfb_identity'] = {'pid': xvfb.pid, 'start_ticks': starts[xvfb.pid], 'uid': user.pw_uid}
        deadline = time.monotonic() + 10
        while not Path(f'/tmp/.X11-unix/X{display}').exists():
            check_cancel()
            if xvfb.poll() is not None or time.monotonic() > deadline: raise RuntimeError('private Xvfb failed')
            time.sleep(.1)
        probe_argv = ['python3', '-B', str(probe), '--claim', str(claim), '--agent-so', str(agent),
            '--agent-sha256', review['participant']['agent_sha256'], '--agent-log', str(agentlog),
            '--review-sha256', args.review_sha256,
            '--world-log', str(profile / 'native-probe.log'), '--ready-file', str(ready_file), '--run-id', case.name,
            '--report', str(case / 'probe.json'), '--contract', str(bundle / 'contract.json'),
            '--config', str(args.config), '--disarm-file', str(disarm)]
        if 'prelaunch_request' in config:
            from linux_portable_prelaunch import probe_arguments
            if probe_argv != probe_arguments(case, bundle, args.review_sha256,
                    review['participant']['agent_sha256'], args.config): raise ValueError('owned probe command differs')
        probeproc = launch('probe', probe_argv)
        deadline = time.monotonic() + 10
        while 'PORTABLE_NATIVE_PROBE READY' not in isolation.bounded_text(case / 'probe.log'):
            check_cancel()
            if probeproc.poll() is not None or time.monotonic() > deadline: raise RuntimeError('independent probe failed before readiness')
            time.sleep(.05)
        result['memory_available_bytes']['before_game'] = isolation.memory_available_bytes()
        if any(row['name'] == 'X4' for row in isolation.processes().values()):
            raise ValueError('native admission changed immediately before launch')
        if isolation.steam_identity(home, user.pw_uid) != steam: raise ValueError('existing Steam changed before launch')
        if prepared: prepared.stock.recheck(check_cancel)
        gameproc = launch('game', command)
        deadline = time.monotonic() + 45
        while gamepid is None:
            check_cancel(); table = isolation.processes(); owned = isolation.descendants(gameproc.pid, table)
            matches = [pid for pid in owned if table.get(pid, {}).get('name') == 'X4' and table[pid]['uid'] == user.pw_uid]
            if len(matches) > 1: raise ValueError('ambiguous owned native process')
            if matches and str(agent) in Path(f'/proc/{matches[0]}/maps').read_text():
                pid = matches[0]; gamepidfd = os.pidfd_open(pid)
                if isolation.start_ticks(pid) != table[pid]['start']: raise ValueError('native identity changed before PIDFD claim')
                gamepid = pid; result.update(native_pid=pid, native_start_ticks=table[pid]['start'], native_uid=user.pw_uid)
                result['extension_views'] = {}
                for view in (case / 'game/extensions', game / 'extensions'):
                    names = sorted(path.name for path in (Path(f'/proc/{pid}/root') / str(view).lstrip('/')).iterdir())
                    if names != extension_ids: raise ValueError('native extension view differs')
                    result['extension_views'][str(view)] = names
                result['native_executable_path'] = os.readlink(f'/proc/{pid}/exe')
                isolation.atomic_json(claim, {'pid': pid, 'start_ticks': table[pid]['start'], 'uid': user.pw_uid})
                break
            if gameproc.poll() is not None or time.monotonic() > deadline: raise RuntimeError('exact agent did not map in owned native process')
            time.sleep(.02)
        print('OWNED_NATIVE_READY ' + json.dumps({'case': str(case), 'pid': gamepid, 'start_ticks': result['native_start_ticks'], 'mode': capability['mode']}), flush=True)
        deadline = time.monotonic() + (config['startup_timeout_ms'] + config['timeout_ms']) / 1000 + 30
        while probeproc.poll() is None:
            check_cancel()
            if time.monotonic() > deadline: raise RuntimeError('bounded native participant timeout')
            if gameproc.poll() is not None: raise RuntimeError('native exited before evidence completed')
            if isolation.steam_identity(home, user.pw_uid) != steam: raise ValueError('existing Steam identity changed')
            time.sleep(.25)
        result['probe_exit_code'] = probeproc.returncode
        result['probe'] = json.loads((case / 'probe.json').read_text())
        required = ('owned_native_verified', 'native_frame0_verified', 'native_terminal_verified', 'restoration_verified')
        if capability['mode'] == EULER_MODE: required += ('rotation_import_verified',)
        if result['probe'].get('verdict') != 'pass' or any(result['probe'].get(key) is not True for key in required):
            raise ValueError(result['probe'].get('error') or 'native portable proof incomplete')
        if prepared:
            prepared.stock.recheck(check_cancel)
            result['preparation']['retained_stock_identity_unchanged'] = True
        result['stock_stamps_unchanged'] = all((s.st_ino, s.st_size, s.st_mtime_ns) == stamp
            for name, stamp in stock_stamps.items() for s in [(game / name).stat()])
        if not result['stock_stamps_unchanged']: raise ValueError('stock changed during native participant')
    except Exception as error:
        result['error'] = str(error)
    finally:
        disarm.touch(); time.sleep(1)
        for fd, label in ((gamepidfd, 'native'), (xvfbpidfd, 'xvfb')):
            if fd is not None:
                try: isolation.stop_pidfd(fd); result[label + '_pidfd_exited'] = True
                except Exception as error: result.setdefault('cleanup_errors', []).append(str(error))
                finally: os.close(fd)
        for process in (gameproc, probeproc, xvfb):
            if process is not None:
                try: isolation.stop_group(process, starts[process.pid])
                except Exception as error: result.setdefault('cleanup_errors', []).append(str(error))
        if coordinatorfd is not None:
            watch = select.poll(); watch.register(coordinatorfd, select.POLLIN)
            result['coordinator_pidfd_exited'] = bool(watch.poll(0)); os.close(coordinatorfd)
        for output in logs: output.close()
        try: result['steam_after'] = isolation.steam_identity(home, user.pw_uid)
        except Exception as error: result['steam_after'] = {'error': str(error)}
        result['steam_identity_unchanged'] = result['steam_after'] == steam
        table = isolation.processes()
        result['owned_native_exited'] = gamepid is None or gamepid not in table or table[gamepid]['start'] != result['native_start_ticks']
        result['independent_probe_exited'] = probeproc is None or probeproc.poll() is not None
        result['private_xvfb_exited'] = xvfb is None or result.get('xvfb_pidfd_exited') is True
        if prepared:
            try:
                prepared.stock.recheck()
                result['preparation']['retained_stock_identity_after_cleanup'] = True
            except Exception as error:
                result['preparation']['retained_stock_identity_after_cleanup'] = False
                result.setdefault('cleanup_errors', []).append(str(error))
        isolation.atomic_json(case / 'launch-result.json', result)
        print('NATIVE_PORTABLE_FINISHED ' + json.dumps({'case': str(case), 'probe_exit_code': result.get('probe_exit_code'),
            'error': result.get('error'), 'owned_native_exited': result['owned_native_exited'],
            'steam_identity_unchanged': result['steam_identity_unchanged']}), flush=True)
    success = (result.get('probe_exit_code') == 0 and not result.get('error') and not result.get('cleanup_errors')
        and all(result.get(key) is True for key in ('native_pidfd_exited', 'xvfb_pidfd_exited', 'stock_stamps_unchanged',
            'owned_native_exited', 'independent_probe_exited', 'private_xvfb_exited', 'steam_identity_unchanged')))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__': main()
