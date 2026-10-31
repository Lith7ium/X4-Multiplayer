"""One disposable Windows participant using the unchanged released lane runner.

The sole admission syntax adjustment permits one client in this wrapper process.
Exact released source hashes and the original validation AST are pinned. Resource,
profile, process, network, queue and cleanup implementations remain released code.
No file in the released runner is modified.
"""
import argparse
import ast
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

from native_gate_lane import PROFILES, SnapshotSlot, digest
from native_portable import exact_keys, hexword, validate_contract
from native_portable_coordinator import validate_participants
from native_portable_ssh import validate_config
from native_portable_windows_identity import NativeIdentity

# Tested lane release 5d5a680: RAM telemetry never gates admission.
RELEASE_SHA = '324dcf72cc7471b017e69b04c2dea41da61afbd93533f1944bad35f51014d3bb'
CORE_SHA = '2a452ede53fcc51503e2dc0d49099dafdd3f7fb25ca954bbe46b36237c06d5ff'
VALIDATE_AST_SHA = 'bc13f2990ddb45b959c3d9903915dc5df898cd47c53aff1554a9e8620701166d'
INERT_SETTINGS = b'-- Owned portable settings are generated after released lane admission.\nX4MP_PORTABLE_CONFIG={enabled=false}\n'
ARTIFACTS = {'content.xml', 'ui.xml', 'ui/config.lua', 'ui/settings.lua',
             'ui/native_portable_windows.lua', 'native/x4lan_gate_64.dll'}


def server_command(ssh_host, remote_script):
    return subprocess.list2cmdline(['{python}', str(Path(__file__).with_name('native_portable_ssh.py')),
        '--ssh-host', ssh_host, '--remote-script', remote_script,
        '--config', '{outdir}/portable-session.json', '--report', '{outdir}/portable-barrier.json',
        '--port', '{port}', '--runner-owned'])


def validation_function(source):
    module = ast.parse(source)
    classes = [n for n in module.body if isinstance(n, ast.ClassDef) and n.name == 'SessionRun']
    if len(classes) != 1: raise ValueError('released SessionRun changed')
    functions = [n for n in classes[0].body if isinstance(n, ast.FunctionDef) and n.name == 'validate']
    if len(functions) != 1: raise ValueError('released validate changed')
    function = functions[0]
    if hashlib.sha256(ast.dump(function, include_attributes=False).encode()).hexdigest() != VALIDATE_AST_SHA:
        raise ValueError('released validation AST changed')
    return function


def one_client_validation(source):
    """Change exactly the first lower-bound constant, preserving every other AST node."""
    original = validation_function(source); changed = copy.deepcopy(original)
    target = ast.dump(ast.parse('2 <= a.clients <= 3', mode='eval').body, include_attributes=False)
    matches = [n for n in ast.walk(changed) if isinstance(n, ast.Compare)
               and ast.dump(n, include_attributes=False) == target]
    if len(matches) != 1: raise ValueError('released participant predicate changed')
    matches[0].left.value = 1
    return changed


def load_release(path):
    path = path.resolve(); core_path = path.parent / 'lane/lanecore.py'
    if path.name != 'x4lane.py' or digest(path) != RELEASE_SHA or digest(core_path) != CORE_SHA:
        raise ValueError('unreviewed released runner/core bytes')
    function = one_client_validation(path.read_bytes())
    existing = sys.modules.get('lanecore')
    if existing and Path(existing.__file__).resolve() != core_path:
        raise ValueError('another lanecore module is already loaded')
    spec = importlib.util.spec_from_file_location('portable_released_x4lane', path)
    lane = importlib.util.module_from_spec(spec); spec.loader.exec_module(lane)
    namespace = {}
    exec(compile(ast.Module(body=[function], type_ignores=[]), str(path), 'exec'), lane.__dict__, namespace)
    lane.SessionRun.validate = namespace['validate']
    return lane


def validate_options(args):
    if (type(args.clients) is not int or args.clients != 1 or args.start != 'menu'
            or args.no_repin or args.allow_workshop or args.overwrite
            or not 30 <= args.timeout <= 1800
            or args.client_overlay or args.client_extension or args.client_lua or args.client_config
            or args.extension or args.lua or args.ui_file):
        raise ValueError('portable session requires exactly one fresh, repinned disposable menu lane')
    if not args.server_cmd or not args.server_ready or not args.server_until:
        raise ValueError('owned SSH server readiness and closed-success triggers required')


def validate_artifacts(artifacts, participants):
    exact_keys(artifacts, ARTIFACTS, 'Windows participant files')
    for path, value in artifacts.items(): hexword(value, 64, path)
    if artifacts['native/x4lan_gate_64.dll'] != participants['1']['agent_sha256']:
        raise ValueError('Windows native artifact differs from participant identity')
    if artifacts['ui/settings.lua'] != hashlib.sha256(INERT_SETTINGS).hexdigest():
        raise ValueError('base portable settings are not the reviewed inert placeholder')


def verify_installed(run, contract, artifacts):
    lane = run.lane; expected = dict(contract['fixture_files'])
    if set(expected) & ARTIFACTS: raise ValueError('shared and participant files overlap')
    expected.update(artifacts)
    actual = {p.relative_to(lane.lab).as_posix(): digest(p) for p in lane.lab.rglob('*') if p.is_file()}
    if actual != expected: raise ValueError('installed portable bytes differ from complete manifest')
    if lane.profile not in PROFILES or lane.profile_dir.name != lane.profile:
        raise ValueError('portable snapshot outside a released disposable profile')
    ui = ET.parse(lane.lab / 'ui.xml').getroot()
    if [n.get('name') for n in ui.findall('./environment/file')] != [
            'ui/settings.lua', 'ui/native_portable_windows.lua', 'ui/native_portable_loader.lua']:
        raise ValueError('portable UI loads unexpected modules')
    if not {'md/x4lan.xml', 'ui/native_portable_loader.lua'} <= set(contract['fixture_files']):
        raise ValueError('shared contract lacks inert MD or common loader')
    return {'shared_files': {p: actual[p] for p in contract['fixture_files']},
            'participant_files': dict(artifacts), 'profile': lane.profile,
            'native_process_verified': False}


def launch_files(run, core):
    autostart = run.lane.extensions / core.AUTOSTART_ID
    core.check_tree_unlinked(autostart)
    return {'autostart': {p.relative_to(autostart).as_posix(): digest(p)
                         for p in autostart.rglob('*') if p.is_file()},
            'profile': {name: digest(run.lane.profile_dir / name)
                        for name in ('config.xml', 'content.xml', 'inputmap.xml')}}


def verify_before_launch(run, core, contract, participants, artifacts):
    evidence = run.result['portable_installed']
    if (run.lane.profile != evidence['profile'] or run.lane.profile_dir.name != run.lane.profile
            or core.is_link(run.lane.profile_dir)):
        raise ValueError('prepared lane profile changed or became linked')
    current_artifacts = dict(artifacts)
    current_artifacts['ui/settings.lua'] = evidence['owned_settings_sha256']
    core.check_tree_unlinked(run.lane.lab)
    verify_installed(run, contract, current_artifacts)
    if launch_files(run, core) != evidence['launch_files']:
        raise ValueError('prepared autostart or profile bytes changed')
    slot = run.native_snapshot.directory
    if slot != run.lane.profile_dir / 'save' or core.is_link(slot):
        raise ValueError('prepared snapshot directory changed')
    core.check_tree_unlinked(slot)
    if sorted(p.name for p in slot.glob('save_009*')) != ['save_009.xml.gz']:
        raise ValueError('prepared snapshot slot changed')
    if digest(slot / 'save_009.xml.gz') != contract['snapshot_sha256']:
        raise ValueError('prepared snapshot bytes changed')
    if digest(run.lane.exe) != participants['1']['executable_sha256']:
        raise ValueError('Windows executable differs from declared participant')
    run.result['portable_launch_recheck'] = {'shared_files_unchanged': True,
        'participant_files_unchanged': True, 'snapshot_unchanged': True,
        'autostart_and_profile_unchanged': True, 'executable_sha256': participants['1']['executable_sha256']}


def settings_text(run, contract, participants, ticks, timeout_ms, startup_ms, rotation_rejection_diagnostics=False):
    if type(rotation_rejection_diagnostics) is not bool:
        raise ValueError('rotation rejection diagnostic option must be boolean')
    if rotation_rejection_diagnostics and 'import_math' not in contract:
        raise ValueError('rotation rejection diagnostic requires Euler contract')
    tokens = run.extra_tokens
    token = tokens.get('@X4LANE_TOKEN@', ''); hexword(token, 32, 'owned session token')
    port = int(tokens.get('@X4LANE_PORT@', '0'))
    if not 1024 <= port <= 65535: raise ValueError('invalid owned port')
    if run.lane.profile not in PROFILES or not re.fullmatch('[a-z0-9_-]{1,80}', run.run_id):
        raise ValueError('invalid owned profile/run')
    path = (run.lane.profile_dir / 'save').resolve().as_posix()
    if not path.endswith('/' + run.lane.profile + '/save'): raise ValueError('owned save suffix differs')
    values = dict(enabled=True, mode='portable-native-v1', slot=1, profile=run.lane.profile,
        run=run.run_id, save_path=path, token=token, port=port,
        snapshot_sha256=contract['snapshot_sha256'], contract_sha256=validate_contract(contract),
        schema_sha256=contract['schema_sha256'], agent_sha256=participants['1']['agent_sha256'],
        fixture_manifest=','.join(actor['saved'] for actor in sorted(contract['actors'], key=lambda a: a['fixture_slot'])),
        ticks=ticks, timeout_ms=timeout_ms, startup_timeout_ms=startup_ms)
    if 'import_math' in contract:
        import portable_import_capability as cap
        if values['contract_sha256'] != cap.CONTRACT_SHA256:
            raise ValueError('unreviewed Windows Euler import contract')
        cap.validate(contract['import_math'], contract['snapshot_sha256'])
        values.update(mode=cap.MODE, import_math_sha256=cap.DESCRIPTOR_SHA256)
        if rotation_rejection_diagnostics: values['rotation_rejection_diagnostics'] = True
    def literal(value):
        if value is True: return 'true'
        if type(value) is int: return str(value)
        if not value.isascii() or any(ord(c) < 32 for c in value): raise ValueError('unsupported config string')
        return json.dumps(value)
    return 'X4MP_PORTABLE_CONFIG={' + ','.join(key + '=' + literal(value) for key, value in values.items()) + '}\n'


def install(lane, *, snapshot, contract, participants, artifacts, ssh_host, remote_script,
            ticks=12, timeout_ms=120000, startup_ms=900000, rotation_rejection_diagnostics=False,
            linux_first=False, linux_prepared=None, linux_review=None):
    validate_contract(contract); validate_participants(participants); validate_artifacts(artifacts, participants)
    if digest(snapshot) != contract['snapshot_sha256']: raise ValueError('shared snapshot bytes differ')
    if linux_first:
        import native_portable_prelaunch as prelaunch
        if (startup_ms > 600000 or linux_prepared is None or linux_review is None
                or validate_contract(contract) != prelaunch.cap.CONTRACT_SHA256):
            raise ValueError('Linux-first launch requires exact Euler-v2 preparation and bounded startup')
    elif linux_prepared is not None or linux_review is not None:
        raise ValueError('Linux preparation supplied without explicit Linux-first mode')
    if (type(ticks) is not int or not 2 <= ticks <= 120 or type(timeout_ms) is not int
            or not 1000 <= timeout_ms <= 120000 or type(startup_ms) is not int
            or not timeout_ms <= startup_ms <= 900000):
        raise ValueError('invalid portable runtime bounds')
    from native_gate_ssh_policy import install as install_ssh_policy
    install_ssh_policy(lane, ssh_host, remote_script)
    original_validate = lane.SessionRun.validate
    original_session_prepare = lane.SessionRun.prepare
    original_session_finish = lane.SessionRun.finish
    original_prepare, original_collect, original_restore = lane.Run.prepare, lane.Run.collect, lane.Run.restore
    original_start_x4 = lane.Run.start_x4
    original_poll, original_release = lane.Run.poll, lane.Run.release

    def validate(session):
        validate_options(session.args)
        if (session.args.server_cmd != server_command(ssh_host, remote_script)
                or session.args.server_ready != 'PORTABLE_SSH READY'
                or session.args.server_until != 'PORTABLE_SSH failure=none done=True transport_closed=True'
                or timeout_ms < 4000 or session.args.timeout <= (startup_ms + timeout_ms) / 1000 + 10):
            raise ValueError('portable session server command or lifetime differs from reviewed bounds')
        original_validate(session)
        if len(session.clients) != 1: raise ValueError('unexpected released client count')
        session.result['portable_runner'] = {'released_sha256': RELEASE_SHA, 'core_sha256': CORE_SHA,
            'validation_ast_sha256': VALIDATE_AST_SHA, 'admission_change': 'session client lower bound 2 to 1',
            'windows_clients': 1, 'native_linux_game_required_separately': True}

    def prepare(run):
        original_prepare(run)  # Full released isolation, repinning, overlays and profile checks.
        lane.core.check_tree_unlinked(run.lane.lab)
        evidence = verify_installed(run, contract, artifacts)
        run.native_snapshot = SnapshotSlot(run.lane.profile, run.lane.profile_dir, lane.core)
        evidence['snapshot'] = run.native_snapshot.stage(snapshot)
        if evidence['snapshot']['sha256'] != contract['snapshot_sha256']:
            # Admission can queue for minutes. Keep the owned slot registered
            # for archive/cleanup, but never enable a changed snapshot.
            raise ValueError('snapshot changed while waiting for lane admission')
        settings = run.lane.extensions / lane.core.AUTOSTART_ID / 'ui/settings.lua'
        old = settings.read_text(encoding='utf-8')
        if old.count('enabled = true') != 1: raise ValueError('unexpected released autostart settings')
        lane.core.safe_write(settings, old.replace('enabled = true', 'enabled = false', 1), run.lane.dir)
        shutil.copy2(settings, run.outdir / 'installed/autostart-settings.lua')
        native_settings = settings_text(run, contract, participants, ticks, timeout_ms, startup_ms, rotation_rejection_diagnostics)
        target = run.lane.lab / 'ui/settings.lua'
        lane.core.safe_write(target, native_settings, run.lane.dir)
        evidence['owned_settings_sha256'] = digest(target)
        evidence['save_path'] = (run.lane.profile_dir / 'save').resolve().as_posix()
        evidence['launch_files'] = launch_files(run, lane.core)
        run.result['portable_installed'] = evidence
        # Private collected settings contain the ephemeral token and remain local.
        shutil.copy2(target, run.outdir / 'installed/portable-settings.lua')

    def session_prepare(session):
        original_session_prepare(session)
        values = dict(token=session.values['token'], contract=contract, participants=participants, ticks=ticks,
            hold_tick=min(3, ticks - 1), hold_seconds=3, startup_timeout=startup_ms // 1000,
            peer_timeout=timeout_ms // 1000, lifetime=min(1800, int(session.args.timeout) + 30))
        if linux_first: values['linux_first'] = True
        validate_config(values)
        path = session.outdir / 'portable-session.json'
        with path.open('x', encoding='ascii') as stream: json.dump(values, stream); stream.write('\n')

    def close_identity_after_owned_exit(run):
        """Preserve refusal, but release eligible leases before released restore."""
        identity = getattr(run, 'portable_identity', None)
        if identity is None: return True
        process = getattr(run, 'proc', None)
        try:
            exited = process is not None and process.poll() is not None
            if process is not None and not exited:
                return False
            evidence = run.result.get('portable_native_identity', {})
            if process is None:
                run.result.setdefault('portable_native_identity_error', 'released launch created no native process')
                evidence['artifact_leases_closed_without_process'] = True
            else:
                try: evidence = identity.finished()
                except Exception as error:
                    # Popen's original owned handle proves exit even when the
                    # independent duplicate could not be attached or queried.
                    run.result.setdefault('portable_native_identity_error', str(error))
                evidence['runner_owned_exit_observed'] = True
                evidence['artifact_leases_closed_after_exit'] = True
            identity.close(); run.portable_identity = None
            run.result['portable_native_identity'] = evidence
            return True
        except Exception as error:
            run.result.setdefault('portable_native_identity_error', str(error))
            return False

    def collect(run):
        try:
            original_collect(run)
        except Exception as error:
            run.result.setdefault('portable_native_identity_error', 'released collection failed: ' + str(error))
            raise
        finally:
            # The released shutdown still calls restore if collection raises.
            # Do not strand a deny-delete lease after an owned exit.
            close_identity_after_owned_exit(run)
        slot = getattr(run, 'native_snapshot', None)
        if slot:
            if run.proc is not None and run.proc.poll() is None: raise ValueError('cannot collect live snapshot writer')
            run.result.setdefault('native_snapshot', {})['files'] = slot.collect(run.outdir)

    def restore(run):
        if not close_identity_after_owned_exit(run):
            run.result.setdefault('portable_native_identity_error', 'owned native process still live or exit identity unavailable before restore')
            raise ValueError('cannot restore portable artifacts while native identity leases are live')
        slot = getattr(run, 'native_snapshot', None)
        if slot:
            if run.proc is not None and run.proc.poll() is None: raise ValueError('cannot restore live snapshot writer')
            slot.restore()
            run.result.setdefault('native_snapshot', {})['restored'] = True
        original_restore(run)

    def start_x4(run):
        # Released start_sequence already holds its launch lock and repeated
        # admission/instance checks. This is its last call before native launch.
        verify_before_launch(run, lane.core, contract, participants, artifacts)
        identity = NativeIdentity(run.lane.exe, run.lane.lab / 'native/x4lan_gate_64.dll',
                                  participants['1']['executable_sha256'], participants['1']['agent_sha256'])
        run.portable_identity = identity
        try:
            if linux_first:
                from native_portable_start_gate import final_check
                final_check(run)
            value = original_start_x4(run)
            run.result['portable_native_identity'] = identity.attach(int(run.proc._handle), run.proc.pid, run.job.handle)
            run.portable_identity_next = 0
            return value
        except Exception as error:
            run.result['portable_native_identity_error'] = str(error)
            raise

    def poll(run, now):
        identity = getattr(run, 'portable_identity', None)
        if identity is not None and run.proc.poll() is None and now >= run.portable_identity_next:
            run.portable_identity_next = now + 1
            try:
                identity.observe()
                run.result['portable_native_identity'] = identity.observe_udp_sources(lane.win().endpoints)
            except Exception as error:
                run.result['portable_native_identity_error'] = str(error)
                raise
        return original_poll(run, now)

    def release(run):
        try: original_release(run)
        finally:
            identity = getattr(run, 'portable_identity', None)
            if identity is not None:
                # Original cleanup has already closed the owned job. A missing
                # normal collect/exit proof remains a failure, never a fallback.
                run.result.setdefault('portable_native_identity_error', 'identity leases reached final cleanup without collected exit proof')
                process = getattr(run, 'proc', None)
                if process is not None:
                    try: process.wait(timeout=5)
                    except Exception as error:
                        run.result.setdefault('portable_identity_cleanup_error', str(error))
                close_identity_after_owned_exit(run)

    def session_finish(session, reason, message):
        failures = []
        for run in session.clients:
            identity = run.result.get('portable_native_identity', {})
            if (run.result.get('portable_native_identity_error') or any(identity.get(key) is not True for key in
                    ('module_observed', 'owned_job_verified', 'owned_process_exited', 'artifact_leases_closed_after_exit'))):
                failures.append('Windows native process, module or owned exit evidence incomplete')
        session.result['portable_native_identity_errors'] = failures
        if failures and reason in ('until_matched', 'duration_elapsed'):
            reason, message = 'error', '; '.join(failures)
        return original_session_finish(session, reason, message)

    lane.SessionRun.validate, lane.SessionRun.prepare = validate, session_prepare
    lane.SessionRun.finish = session_finish
    lane.Run.prepare, lane.Run.collect, lane.Run.restore = prepare, collect, restore
    lane.Run.start_x4, lane.Run.poll, lane.Run.release = start_x4, poll, release
    if linux_first:
        from native_portable_start_gate import install as install_start_gate
        install_start_gate(lane, contract=contract, participants=participants, remote_script=remote_script,
            prepared_path=linux_prepared, review_path=linux_review)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('lane-tool', 'snapshot', 'contract', 'participants', 'artifacts'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--ssh-host', required=True); parser.add_argument('--remote-script', required=True)
    parser.add_argument('--ticks', type=int, default=12)
    parser.add_argument('--timeout-ms', type=int, default=120000)
    parser.add_argument('--startup-ms', type=int, default=900000)
    parser.add_argument('--rotation-rejection-diagnostics', action='store_true')
    parser.add_argument('--linux-first', action='store_true')
    parser.add_argument('--linux-prepared', type=Path); parser.add_argument('--linux-review', type=Path)
    parser.add_argument('lane_args', nargs=argparse.REMAINDER)
    args = parser.parse_args(); remaining = args.lane_args
    if remaining[:1] == ['--']: remaining = remaining[1:]
    if not remaining or remaining[0] != 'session': parser.error('exact one-client released session required')
    lane = load_release(args.lane_tool)
    install(lane, snapshot=args.snapshot, contract=json.loads(args.contract.read_text()),
        participants=json.loads(args.participants.read_text()), artifacts=json.loads(args.artifacts.read_text()),
        ssh_host=args.ssh_host, remote_script=args.remote_script, ticks=args.ticks,
        timeout_ms=args.timeout_ms, startup_ms=args.startup_ms, rotation_rejection_diagnostics=args.rotation_rejection_diagnostics,
        linux_first=args.linux_first, linux_prepared=args.linux_prepared, linux_review=args.linux_review)
    return lane.main(remaining)


if __name__ == '__main__': raise SystemExit(main())
