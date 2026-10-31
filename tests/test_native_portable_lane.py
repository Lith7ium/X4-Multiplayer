"""Offline released-wrapper checks; no game/process control or lane mutation."""
import ast
import copy
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from test_native_portable import TOKEN, contract, participants
import native_portable_lane as portable
from test_native_snapshot_slot import LocalCore

RELEASE = Path(__file__).resolve().parents[2] / 'lanes/release/dev/x4lane.py'


def options():
    return SimpleNamespace(clients=1, start='menu', no_repin=False, allow_workshop=False, overwrite=False,
        min_free_gb=0, timeout=1200, client_overlay=[], client_extension=[], client_lua=[], client_config=[],
        extension=[], lua=[], ui_file=[], server_cmd='required', server_ready='ready', server_until='closed')


class PortableLane(unittest.TestCase):
    def test_identity_failure_cleanup_releases_only_before_proven_safe_restore(self):
        for case in ('popen', 'early_start', 'attach_no_handle', 'collect_raises', 'finished_raises', 'live', 'unknown_exit'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); source = root / 'save_009.xml.gz'; source.write_bytes(b'owned snapshot')
                cfg = contract(); cfg['snapshot_sha256'] = portable.digest(source)
                artifacts = {name: 'a' * 64 for name in portable.ARTIFACTS}
                artifacts['ui/settings.lua'] = hashlib.sha256(portable.INERT_SETTINGS).hexdigest()
                peers = participants(); peers['1']['agent_sha256'] = artifacts['native/x4lan_gate_64.dll']
                events = []
                class FakeRun:
                    def prepare(self): pass
                    def start_x4(self):
                        if case == 'popen': raise OSError('Popen refused')
                        self.proc = SimpleNamespace(_handle=3, pid=4, poll=lambda: None if case == 'live' else 0)
                        self.job = SimpleNamespace(handle=5)
                        if case == 'early_start': raise OSError('early start failed')
                        if case == 'unknown_exit': self.proc.poll = mock.Mock(side_effect=OSError('exit query failed'))
                    def poll(self, now): pass
                    def collect(self):
                        events.append('collect')
                        if case == 'collect_raises': raise OSError('collection failed')
                    def restore(self): events.append('restore')
                    def release(self): events.append('release')
                class FakeSession:
                    def validate(self): pass
                    def prepare(self): pass
                    def finish(self, reason, message): return reason
                fake = SimpleNamespace(core=LocalCore, Run=FakeRun, SessionRun=FakeSession,
                    Server=SimpleNamespace(check_network=lambda _: None))
                portable.install(fake, snapshot=source, contract=cfg, participants=peers, artifacts=artifacts,
                                 ssh_host='127.0.0.1', remote_script='/private/native_portable_ssh.py')
                run = FakeRun(); run.proc = None; run.result = {}
                run.lane = SimpleNamespace(exe=root / 'X4.exe', lab=root / 'lab')
                session = FakeSession(); session.clients = [run]; session.result = {}
                with mock.patch.object(portable, 'verify_before_launch'), mock.patch.object(portable, 'NativeIdentity') as constructor:
                    identity = constructor.return_value; identity.close.side_effect = lambda: events.append('close')
                    identity.attach.return_value = {'module_observed': False}
                    identity.finished.return_value = {'module_observed': False, 'owned_process_exited': True}
                    if case == 'attach_no_handle': identity.attach.side_effect = OSError('duplicate handle failed')
                    if case in ('attach_no_handle', 'finished_raises'): identity.finished.side_effect = ValueError('independent exit proof unavailable')
                    if case in ('popen', 'early_start', 'attach_no_handle'):
                        with self.assertRaises(OSError): run.start_x4()
                    else: run.start_x4()
                    if case == 'collect_raises':
                        with self.assertRaisesRegex(OSError, 'collection failed'): run.collect()
                    else: run.collect()
                    if case in ('live', 'unknown_exit'):
                        with self.assertRaisesRegex(ValueError, 'leases are live'): run.restore()
                        self.assertNotIn('close', events); self.assertNotIn('restore', events)
                    else:
                        run.restore()
                        self.assertEqual(identity.close.call_count, 1)
                        self.assertLess(events.index('close'), events.index('restore'))
                        self.assertIsNone(run.portable_identity)
                    self.assertEqual(session.finish('until_matched', 'trigger only'), 'error')
                    self.assertEqual(session.finish('launch_failed', 'original failure'), 'launch_failed')

    def test_identity_lifecycle_retains_original_methods_and_refuses_missing_native_proof(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / 'save_009.xml.gz'; source.write_bytes(b'owned snapshot')
            cfg = contract(); cfg['snapshot_sha256'] = portable.digest(source)
            artifacts = {name: 'a' * 64 for name in portable.ARTIFACTS}
            artifacts['ui/settings.lua'] = hashlib.sha256(portable.INERT_SETTINGS).hexdigest()
            peers = participants(); peers['1']['agent_sha256'] = artifacts['native/x4lan_gate_64.dll']
            class FakeRun:
                def prepare(self): pass
                def start_x4(self):
                    self.calls.append('start'); self.proc = SimpleNamespace(_handle=3, pid=4, poll=lambda: None)
                    self.job = SimpleNamespace(handle=5)
                def poll(self, now): self.calls.append('poll')
                def collect(self): self.calls.append('collect')
                def restore(self): self.calls.append('restore')
                def release(self): self.calls.append('release')
            class FakeSession:
                def validate(self): pass
                def prepare(self): pass
                def finish(self, reason, message): return reason
            fake = SimpleNamespace(core=LocalCore, Run=FakeRun, SessionRun=FakeSession,
                Server=SimpleNamespace(check_network=lambda _: None), win=lambda: SimpleNamespace(endpoints=lambda _: []))
            portable.install(fake, snapshot=source, contract=cfg, participants=peers, artifacts=artifacts,
                             ssh_host='127.0.0.1', remote_script='/private/native_portable_ssh.py')
            run = FakeRun(); run.calls = []; run.result = {}
            run.lane = SimpleNamespace(exe=root / 'X4.exe', lab=root / 'lab')
            session = FakeSession(); session.clients = [run]; session.result = {}
            success = {'module_observed': True, 'owned_job_verified': True, 'owned_process_exited': True}
            with mock.patch.object(portable, 'verify_before_launch'), mock.patch.object(portable, 'NativeIdentity') as constructor:
                identity = constructor.return_value
                identity.attach.return_value = {'module_observed': False}
                identity.observe.return_value = {'module_observed': True}
                identity.finished.return_value = dict(success)
                run.start_x4(); run.poll(1); run.poll(1.5)
                identity.observe.assert_called_once()
                identity.observe_udp_sources.assert_called_once()
                run.proc.poll = lambda: 0
                run.collect(); run.restore(); run.release()
                identity.close.assert_called_once()
                self.assertEqual(run.calls, ['start', 'poll', 'poll', 'collect', 'restore', 'release'])
                self.assertEqual(session.finish('until_matched', 'complete'), 'until_matched')
                self.assertEqual(session.result['portable_native_identity_errors'], [])
                for key in (*success, 'artifact_leases_closed_after_exit'):
                    run.result['portable_native_identity'][key] = False
                    self.assertEqual(session.finish('until_matched', 'complete'), 'error')
                    self.assertEqual(session.finish('fail_pattern', 'native failure'), 'fail_pattern')
                    run.result['portable_native_identity'][key] = True
                identity.attach.side_effect = ValueError('wrong owned identity')
                with self.assertRaisesRegex(ValueError, 'wrong owned identity'): run.start_x4()
                run.proc.poll = lambda: 0; run.proc.wait = lambda timeout: 0
                run.release()
                self.assertIn('wrong owned identity', run.result['portable_native_identity_error'])
                self.assertEqual(session.finish('until_matched', 'complete'), 'error')
                self.assertEqual(identity.close.call_count, 2)

    def test_prepared_bytes_are_rechecked_after_startup_wait_before_original_launch(self):
        from build_native_portable_windows import UI, CONTENT, CONFIG
        mutations = ('none', 'loader', 'dll', 'extra', 'settings', 'snapshot', 'autostart', 'profile', 'exe', 'duplicate_slot', 'profile_link')
        for mutation in mutations:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); extensions = root / 'extensions'; lab = extensions / 'x4_lan_lab'
                shared = {'md/native_gate_battle.xml': b'<mdscript name="Fixture"><cues /></mdscript>',
                          'md/x4lan.xml': b'<mdscript name="X4LAN_PortableInert"><cues /></mdscript>',
                          'ui/native_portable_loader.lua': b'-- common loader\n'}
                files = {'content.xml': CONTENT, 'ui.xml': UI, 'ui/config.lua': CONFIG,
                         'ui/settings.lua': portable.INERT_SETTINGS, 'ui/native_portable_windows.lua': b'-- adapter\n',
                         'native/x4lan_gate_64.dll': b'synthetic native artifact'}
                for name, data in {**shared, **files}.items():
                    path = lab / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
                cfg = contract(); cfg['fixture_files'] = {name: hashlib.sha256(data).hexdigest() for name, data in shared.items()}
                artifacts = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
                source = root / 'owned.xml.gz'; source.write_bytes(b'owned snapshot'); cfg['snapshot_sha256'] = portable.digest(source)
                exe = root / 'X4.exe'; exe.write_bytes(b'synthetic executable')
                peers = participants(); peers['1'].update(agent_sha256=artifacts['native/x4lan_gate_64.dll'], executable_sha256=portable.digest(exe))
                profile = root / '45850'; profile.mkdir()
                for name in ('config.xml', 'content.xml', 'inputmap.xml'): (profile / name).write_text('<root/>')
                autostart = extensions / 'x4lane_autostart/ui/settings.lua'; autostart.parent.mkdir(parents=True)
                autostart.write_text('enabled = true\n')
                class Core(LocalCore):
                    AUTOSTART_ID = 'x4lane_autostart'
                    @staticmethod
                    def safe_write(path, data, bound):
                        LocalCore.assert_inside(path, bound); path.write_text(data)
                class FakeRun:
                    def prepare(self): (self.outdir / 'installed').mkdir()
                    def collect(self): pass
                    def restore(self): pass
                    def start_x4(self):
                        self.launches += 1; self.proc = SimpleNamespace(_handle=3, pid=4)
                        self.job = SimpleNamespace(handle=5)
                    def poll(self, now): pass
                    def release(self): pass
                class FakeSession:
                    def validate(self): pass
                    def prepare(self): pass
                    def finish(self, reason, message): return reason
                fake = SimpleNamespace(core=Core, Run=FakeRun, SessionRun=FakeSession,
                    Server=SimpleNamespace(check_network=lambda _: None))
                portable.install(fake, snapshot=source, contract=cfg, participants=peers, artifacts=artifacts,
                                 ssh_host='127.0.0.1', remote_script='/private/native_portable_ssh.py')
                run = FakeRun(); run.lane = SimpleNamespace(profile='45850', profile_dir=profile, lab=lab,
                    extensions=extensions, dir=root, exe=exe)
                run.outdir = root / 'out'; run.outdir.mkdir(); run.result = {}; run.launches = 0
                run.run_id = 'portable-owned-wait'; run.extra_tokens = {'@X4LANE_TOKEN@': TOKEN, '@X4LANE_PORT@': '31001'}
                run.prepare()  # Then model a blocked coordinator/admission startup.
                changes = {'loader': lab / 'ui/native_portable_loader.lua', 'dll': lab / 'native/x4lan_gate_64.dll',
                    'extra': lab / 'extra.lua', 'settings': lab / 'ui/settings.lua', 'snapshot': profile / 'save/save_009.xml.gz',
                    'autostart': autostart, 'profile': profile / 'config.xml', 'exe': exe,
                    'duplicate_slot': profile / 'save/save_009.xml'}
                if mutation == 'none':
                    with mock.patch.object(portable, 'NativeIdentity') as native:
                        run.start_x4(); self.assertEqual(run.launches, 1)
                        native.return_value.attach.assert_called_once_with(3, 4, 5)
                    self.assertTrue(run.result['portable_launch_recheck']['snapshot_unchanged'])
                elif mutation == 'profile_link':
                    with mock.patch.object(Core, 'is_link', side_effect=lambda path: Path(path) == profile or LocalCore.is_link(path)):
                        with self.assertRaisesRegex(ValueError, 'profile changed or became linked'): run.start_x4()
                    self.assertEqual(run.launches, 0)
                else:
                    changes[mutation].write_bytes(b'changed during startup wait')
                    with self.assertRaises(ValueError): run.start_x4()
                    self.assertEqual(run.launches, 0)

    def test_snapshot_change_between_install_and_prepare_refuses_and_cleans_owned_slot(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / 'source.xml.gz'; source.write_bytes(b'original snapshot')
            cfg = contract(); cfg['snapshot_sha256'] = portable.digest(source)
            artifacts = {name: 'a' * 64 for name in portable.ARTIFACTS}
            artifacts['ui/settings.lua'] = hashlib.sha256(portable.INERT_SETTINGS).hexdigest()
            peers = participants(); peers['1']['agent_sha256'] = artifacts['native/x4lan_gate_64.dll']
            class FakeRun:
                def prepare(self): pass
                def collect(self): pass
                def restore(self): self.released_restore_called = True
                def start_x4(self): raise AssertionError('a refused preparation must never launch')
                def poll(self, now): pass
                def release(self): pass
            class FakeSession:
                def validate(self): pass
                def prepare(self): pass
                def finish(self, reason, message): return reason
            fake = SimpleNamespace(core=LocalCore, Run=FakeRun, SessionRun=FakeSession,
                                   Server=SimpleNamespace(check_network=lambda _: None))
            portable.install(fake, snapshot=source, contract=cfg, participants=peers, artifacts=artifacts,
                             ssh_host='127.0.0.1', remote_script='/private/native_portable_ssh.py')
            profile = root / '45850'; profile.mkdir(); lab = root / 'lab'; lab.mkdir()
            (lab / 'ui').mkdir()
            settings = lab / 'ui/settings.lua'; settings.write_bytes(portable.INERT_SETTINGS)
            run = FakeRun(); run.lane = SimpleNamespace(profile='45850', profile_dir=profile, lab=lab)
            run.result = {}; run.proc = None; run.outdir = root / 'out'; run.outdir.mkdir()
            source.write_bytes(b'changed while waiting in queue')
            with mock.patch.object(portable, 'verify_installed', return_value={}):
                with self.assertRaisesRegex(ValueError, 'snapshot changed while waiting'):
                    run.prepare()
            self.assertEqual(settings.read_bytes(), portable.INERT_SETTINGS)
            self.assertTrue((profile / 'save/save_009.xml.gz').exists())
            run.collect(); run.restore()
            self.assertTrue(run.released_restore_called)
            self.assertFalse((profile / 'save/save_009.xml.gz').exists())
            self.assertEqual((run.outdir / 'native-snapshot/save_009.xml.gz').read_bytes(), source.read_bytes())
            self.assertTrue(run.result['native_snapshot']['restored'])

    def test_wrapper_requires_one_disposable_client_without_a_ram_floor(self):
        for value in (0, 0.25, 13.99, 14, 1000):
            args = options(); args.min_free_gb = value
            portable.validate_options(args)  # Legacy CLI option is ignored.
        for key, value in [('clients', 2), ('clients', 0), ('start', 'module'), ('no_repin', True),
                           ('allow_workshop', True), ('overwrite', True),
                           ('extension', ['unexpected']), ('lua', ['extra native action']), ('server_ready', '')]:
            args = options(); setattr(args, key, value)
            with self.subTest(key=key), self.assertRaises(ValueError): portable.validate_options(args)

    @unittest.skipUnless(RELEASE.exists(), 'exact local released runner required')
    def test_exact_ast_change_preserves_every_other_validator_node(self):
        source = RELEASE.read_bytes(); before = hashlib.sha256(source).hexdigest()
        original = portable.validation_function(source)
        changed = portable.one_client_validation(source)
        target = ast.dump(ast.parse('1 <= a.clients <= 3', mode='eval').body, include_attributes=False)
        matches = [n for n in ast.walk(changed) if isinstance(n, ast.Compare)
                   and ast.dump(n, include_attributes=False) == target]
        self.assertEqual(len(matches), 1)
        matches[0].left.value = 2
        self.assertEqual(ast.dump(changed, include_attributes=False), ast.dump(original, include_attributes=False))
        with self.assertRaises(ValueError): portable.one_client_validation(source.replace(b'not 2 <= a.clients <= 3', b'not 2 <= a.clients <= 4'))
        self.assertEqual(hashlib.sha256(RELEASE.read_bytes()).hexdigest(), before)

    @unittest.skipUnless(RELEASE.exists(), 'exact local released runner required')
    def test_actual_release_import_keeps_process_resource_and_cleanup_methods(self):
        before = hashlib.sha256(RELEASE.read_bytes()).hexdigest()
        lane = portable.load_release(RELEASE)
        names = ('general_checks', 'start_sequence', 'close_x4', 'release')
        for name in names:
            self.assertEqual(Path(getattr(lane.Run, name).__code__.co_filename).resolve(), RELEASE.resolve())
        for name in ('preflight', 'acquire', 'launch', 'shutdown', 'finish'):
            self.assertEqual(Path(getattr(lane.SessionRun, name).__code__.co_filename).resolve(), RELEASE.resolve())
        self.assertEqual(lane.PROTECTED_PROFILES, ('45841', '45842'))
        self.assertEqual(hashlib.sha256(RELEASE.read_bytes()).hexdigest(), before)

    def test_manifest_after_repin_is_exact_and_shared_bytes_cannot_change(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); lab = root / 'lab'; lab.mkdir()
            shared = {'md/x4lan.xml': b'<mdscript name="Inert"><cues /></mdscript>',
                      'ui/native_portable_loader.lua': b'-- reviewed shared loader\n'}
            local = {p: b'inert' for p in portable.ARTIFACTS}
            local['ui.xml'] = (b'<addon><environment><file name="ui/settings.lua" />'
                b'<file name="ui/native_portable_windows.lua" /><file name="ui/native_portable_loader.lua" />'
                b'</environment></addon>')
            local['ui/settings.lua'] = portable.INERT_SETTINGS
            for name, data in {**shared, **local}.items():
                path = lab / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            cfg = contract(); cfg['fixture_files'] = {p: hashlib.sha256(b).hexdigest() for p, b in shared.items()}
            artifacts = {p: hashlib.sha256(b).hexdigest() for p, b in local.items()}
            peers = participants(); peers['1']['agent_sha256'] = artifacts['native/x4lan_gate_64.dll']
            portable.validate_artifacts(artifacts, peers)
            run = SimpleNamespace(lane=SimpleNamespace(lab=lab, profile='45850', profile_dir=root / '45850'))
            report = portable.verify_installed(run, cfg, artifacts)
            self.assertEqual(report['shared_files'], cfg['fixture_files'])
            (lab / 'ui/native_portable_loader.lua').write_bytes(b'changed by repinning')
            with self.assertRaises(ValueError): portable.verify_installed(run, cfg, artifacts)
            (lab / 'ui/native_portable_loader.lua').write_bytes(shared['ui/native_portable_loader.lua'])
            (lab / 'md/extra.xml').write_bytes(b'<mdscript/>')
            with self.assertRaises(ValueError): portable.verify_installed(run, cfg, artifacts)

    def test_generated_settings_bind_exact_owned_profile_path_snapshot_and_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run = SimpleNamespace(lane=SimpleNamespace(profile='45850', profile_dir=root / '45850'),
                run_id='portable-owned-001', extra_tokens={'@X4LANE_TOKEN@': TOKEN, '@X4LANE_PORT@': '31001'})
            text = portable.settings_text(run, contract(), participants(), 12, 120000, 900000)
            self.assertIn('profile="45850"', text)
            self.assertIn('save_path=' + json.dumps((root / '45850/save').resolve().as_posix()), text)
            self.assertIn('slot=1', text); self.assertIn('mode="portable-native-v1"', text)
            for bad in ('45841', '45842', '45852', '70000001'):
                run.lane.profile = bad
                with self.assertRaises(ValueError): portable.settings_text(run, contract(), participants(), 12, 120000, 900000)


if __name__ == '__main__': unittest.main()
