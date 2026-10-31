"""Portable Linux launcher preflight only; native X4 is never started."""
import copy
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_native_portable_run as runner
import portable_import_capability as cap
from test_native_portable import contract, participants, TOKEN


def session(cfg):
    result = {'mode': runner.EULER_MODE if 'import_math' in cfg else runner.MODE, 'token': TOKEN, 'participants': participants(),
        'contract_sha256': runner.validate_contract(cfg), 'ticks': 12, 'startup_timeout_ms': 600000, 'timeout_ms': 120000,
        'remote_script': '/private/coordinator/native_portable_ssh.py',
        'coordinator': {'kind': 'ready', 'platform': 'linux', 'hashes': {}, 'pid': 123, 'start_ticks': 456,
                        'port': 31001, 'report_path': '/private/coordinator/runs/owned/report.json'}}
    if 'import_math' in cfg: result['import_math_sha256'] = cap.DESCRIPTOR_SHA256
    return result


def bundle_fixture(root, euler=False):
    cfg = contract(); cfg['fixture_files'] = {name: hashlib.sha256(name.encode()).hexdigest() for name in runner.SHARED}
    cfg['snapshot_sha256'] = hashlib.sha256(b'snapshot').hexdigest()
    if euler: cfg['import_math'] = cap.capability(cfg['snapshot_sha256'])
    mode = runner.EULER_MODE if euler else runner.MODE
    payloads = runner.payloads(mode)
    for name in payloads:
        path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(name.encode())
    for name in runner.SHARED: (root / 'fixture' / name).write_bytes(name.encode())
    (root / 'contract.json').write_text(json.dumps(cfg)); (root / 'save_009.xml.gz').write_bytes(b'snapshot')
    participant = {'platform': 'linux', 'executable_sha256': runner.isolation.EXE_SHA,
                   'agent_sha256': runner.isolation.sha(root / 'bin/libx4mp_portable.so')}
    review = {'mode': mode, 'contract_sha256': runner.validate_contract(cfg), 'schema_sha256': runner.SCHEMA_SHA,
              'participant': participant, 'files': {name: runner.isolation.sha(root / name) for name in payloads}}
    if euler: review['import_math_sha256'] = cap.DESCRIPTOR_SHA256
    (root / 'review.json').write_text(json.dumps(review))
    return cfg, participant, runner.isolation.sha(root / 'review.json')


class PortableLinuxPreflight(unittest.TestCase):
    def test_closed_session_binds_actual_participant_and_common_bounds(self):
        cfg = contract(); values = session(cfg)
        with mock.patch.object(runner, 'CONTRACT_SHA', runner.validate_contract(cfg)):
            self.assertEqual(runner.validate_session(values, cfg, values['participants']['2']), values)
            changes = [lambda v: v.update(mode='snapshot-import-observer-v1'),
                       lambda v: v.update(startup_timeout_ms=900000), lambda v: v.update(ticks=True),
                       lambda v: v.update(timeout_ms=120001), lambda v: v.update(extra='fallback'),
                       lambda v: v['coordinator'].update(port=22), lambda v: v['participants']['2'].update(agent_sha256='9' * 64)]
            for mutate in changes:
                value = copy.deepcopy(values); mutate(value)
                with self.assertRaises(ValueError): runner.validate_session(value, cfg, values['participants']['2'])

    def test_mode_environment_excludes_legacy_and_secrets_from_loader_settings(self):
        cfg = contract(); values = session(cfg)
        with mock.patch.object(runner, 'CONTRACT_SHA', runner.validate_contract(cfg)):
            result = runner.portable_environment({'HOME': '/private/home'}, values, '/private/agent.so', '/private/log', '/private/ready', '/private/disarm')
        self.assertEqual(result['X4MP_PORTABLE_PORT'], '31001')
        self.assertEqual(result['X4MP_PORTABLE'], runner.MODE)
        self.assertFalse(any(k.startswith(('X4MP_SCHED', 'X4MP_ADMISSION', 'X4MP_LOAD')) for k in result))
        for key in ('X4MP_SCHED', 'X4MP_AGENT', 'LD_PRELOAD'):
            with self.assertRaises(ValueError): runner.portable_environment({key: 'unexpected'}, values, 'a', 'b', 'c', 'd')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); bundle = root / 'bundle'; bundle.mkdir()
            cfg, _, _ = bundle_fixture(bundle); case = root / 'portable-owned'; profile = case / 'home/45841'
            (profile / 'save').mkdir(parents=True)
            with mock.patch.object(runner, 'CONTRACT_SHA', runner.validate_contract(cfg)):
                boot = runner.stage_fixture(case, profile, bundle, cfg, json.loads((bundle / 'review.json').read_text()))
            for name in runner.SHARED:
                self.assertEqual((boot / name).read_bytes(), (bundle / 'fixture' / name).read_bytes())
            settings = (boot / 'ui/settings.lua').read_text()
            self.assertIn(runner.MODE, settings); self.assertNotIn(TOKEN, settings); self.assertNotIn('token', settings)
            self.assertNotIn('autostart', (boot / 'ui.xml').read_text())
            self.assertEqual((profile / 'save/save_009.xml.gz').read_bytes(), b'snapshot')

    def test_review_requires_complete_unchanged_payload_and_matching_snapshot(self):
        for mutation in ('none', 'extra', 'changed', 'missing', 'wrong_manifest'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); cfg, _, digest = bundle_fixture(root)
                if mutation == 'extra': (root / 'unreviewed.lua').write_text('extra')
                if mutation == 'changed': (root / 'fixture/ui/native_portable_linux_live.lua').write_text('changed')
                if mutation == 'missing': (root / 'bin/libx4mp_portable.so').unlink()
                if mutation == 'wrong_manifest': digest = '0' * 64
                with mock.patch.object(runner, 'CONTRACT_SHA', runner.validate_contract(cfg)):
                    if mutation == 'none':
                        self.assertEqual(runner.review_bundle(root, digest)[1], cfg)
                    else:
                        with self.assertRaises(ValueError): runner.review_bundle(root, digest)

    def test_euler_session_environment_require_exact_capability_and_reject_mixed_modes(self):
        cfg = contract(); cfg['snapshot_sha256'] = cap.SNAPSHOT_SHA256
        cfg['import_math'] = cap.capability(cfg['snapshot_sha256']); values = session(cfg)
        # This fixture has synthetic actors/stock. Only the artifact digest is
        # substituted; production still requires the exact reviewed77b19... pin.
        with self.assertRaises(ValueError): runner.reviewed_capability(cfg)
        with mock.patch.object(cap, 'CONTRACT_SHA256', runner.validate_contract(cfg)):
            self.assertEqual(runner.validate_session(values,cfg,values['participants']['2']),values)
            environment = runner.portable_environment({},values,'a','b','c','d',cfg)
            self.assertEqual(environment['X4MP_PORTABLE'],cap.MODE)
            self.assertEqual(environment['X4MP_PORTABLE_IMPORT_SHA256'],cap.DESCRIPTOR_SHA256)
            self.assertEqual(environment['X4MP_PORTABLE_SNAPSHOT_SHA256'],cap.SNAPSHOT_SHA256)
            for mutate in (lambda x:x.pop('import_math_sha256'),lambda x:x.update(import_math_sha256='0'*64),
                           lambda x:x.update(mode=runner.MODE),lambda x:x.update(contract_sha256=runner.CONTRACT_SHA),
                           lambda x:x.update(snapshot_sha256=cap.SNAPSHOT_SHA256),lambda x:x.update(mode='portable-fixture12-euler-v1')):
                bad=copy.deepcopy(values);mutate(bad)
                with self.assertRaises(ValueError):runner.validate_session(bad,cfg,values['participants']['2'])
            with self.assertRaises(ValueError):runner.portable_environment({},values,'a','b','c','d')
            for key in ('X4MP_PORTABLE_IMPORT_SHA256','X4MP_PORTABLE_SNAPSHOT_SHA256'):
                with self.assertRaises(ValueError):runner.portable_environment({key:'inherited'},values,'a','b','c','d',cfg)
        old=contract();legacy=session(old);legacy['import_math_sha256']=cap.DESCRIPTOR_SHA256
        with mock.patch.object(runner,'CONTRACT_SHA',runner.validate_contract(old)):
            with self.assertRaises(ValueError):runner.validate_session(legacy,old,legacy['participants']['2'])
        with self.assertRaises(ValueError):runner.portable_environment({},legacy,'a','b','c','d')
        import portable_import_capability_v1 as historical
        prior=contract();prior['snapshot_sha256']=historical.SNAPSHOT_SHA256
        prior['import_math']=historical.capability(historical.SNAPSHOT_SHA256)
        runner.validate_contract(prior)  # Read-only historical schema remains valid.
        with self.assertRaises(ValueError):runner.reviewed_capability(prior)
        with self.assertRaises(ValueError):runner.payloads(historical.MODE)

    def test_euler_review_closure_and_settings_cannot_drop_or_change_capability_source(self):
        for mutation in ('none','missing','changed','missing_v1','changed_v1','wrong_descriptor','extra'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);bundle=root/'bundle';bundle.mkdir()
                # Owned synthetic snapshot and artifact pins only; never stage a game.
                with mock.patch.object(cap,'SNAPSHOT_SHA256',hashlib.sha256(b'snapshot').hexdigest()):
                    cfg,_,digest=bundle_fixture(bundle,euler=True)
                    with mock.patch.object(cap,'CONTRACT_SHA256',runner.validate_contract(cfg)):
                        source=bundle/'bin/portable_import_capability.py'
                        if mutation=='missing':source.unlink()
                        if mutation=='changed':source.write_bytes(b'changed')
                        if mutation=='missing_v1':(bundle/'bin/portable_import_capability_v1.py').unlink()
                        if mutation=='changed_v1':(bundle/'bin/portable_import_capability_v1.py').write_bytes(b'changed')
                        if mutation=='extra':(bundle/'foreign').write_text('extra')
                        if mutation=='wrong_descriptor':
                            row=json.loads((bundle/'review.json').read_text());row['import_math_sha256']='0'*64
                            (bundle/'review.json').write_text(json.dumps(row));digest=runner.isolation.sha(bundle/'review.json')
                        if mutation!='none':
                            with self.assertRaises(ValueError):runner.review_bundle(bundle,digest)
                            continue
                        review,actual=runner.review_bundle(bundle,digest);self.assertEqual(actual,cfg)
                        case=root/'case';profile=case/'profile';(profile/'save').mkdir(parents=True)
                        boot=runner.stage_fixture(case,profile,bundle,cfg,review)
                        settings=(boot/'ui/settings.lua').read_text()
                        for value in (cap.MODE,cap.DESCRIPTOR_SHA256,cap.SNAPSHOT_SHA256,cap.CONTRACT_SHA256):self.assertIn(value,settings)
                        self.assertNotIn(TOKEN,settings);self.assertNotIn('token',settings)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);cfg,_,digest=bundle_fixture(root)
            (root/'bin/portable_import_capability.py').write_text('extra legacy file')
            with mock.patch.object(runner,'CONTRACT_SHA',runner.validate_contract(cfg)):
                with self.assertRaises(ValueError):runner.review_bundle(root,digest)


@unittest.skipUnless(sys.platform == 'linux' and hasattr(os, 'pidfd_open'), 'Linux PIDFD/socket ownership required')
class ActualCoordinatorIdentity(unittest.TestCase):
    def test_owned_udp_process_provenance_and_private_config_refusals(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); remote = root / 'coordinator'; remote.mkdir()
            bundled = root / 'bundle/bin'; bundled.mkdir(parents=True)
            code = '''import os,socket,sys,json
from pathlib import Path
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
 sock.bind(('127.0.0.1',0))
 print(json.dumps({'pid':os.getpid(),'start_ticks':int(Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()[19]),'port':sock.getsockname()[1]}),flush=True)
 sys.stdin.readline()
'''
            for name in ('native_portable.py', 'native_portable_coordinator.py', 'native_portable_ssh.py'):
                data = code.encode() if name == 'native_portable_ssh.py' else b'# synthetic preflight fixture\n'
                (remote / name).write_bytes(data); (bundled / name).write_bytes(data)
            script = remote / 'native_portable_ssh.py'
            child = subprocess.Popen([sys.executable, str(script), '--remote'], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                identity = json.loads(child.stdout.readline())
                ready = {**identity, 'kind': 'ready', 'platform': 'linux',
                         'report_path': str(remote / 'runs/owned/report.json'),
                         'hashes': {name: runner.isolation.sha(bundled / name) for name in
                                    ('native_portable.py', 'native_portable_coordinator.py', 'native_portable_ssh.py')}}
                config = {'coordinator': ready, 'remote_script': str(script)}
                fd = runner.verify_coordinator(config, root / 'bundle', root)
                watch = select.poll(); watch.register(fd, select.POLLIN)
                try: self.assertFalse(watch.poll(0))
                finally: os.close(fd)
                config['mode']=runner.EULER_MODE
                data=b'# owned new-mode provenance fixture\n'
                for name in ('portable_import_capability.py','portable_import_capability_v1.py'):
                    (remote/name).write_bytes(data);(bundled/name).write_bytes(data)
                    ready['hashes'][name]=runner.isolation.sha(bundled/name)
                fd=runner.verify_coordinator(config,root/'bundle',root);os.close(fd)
                (remote/name).write_bytes(data+b'changed')
                with self.assertRaisesRegex(ValueError,'source changed'):runner.verify_coordinator(config,root/'bundle',root)
                (remote/name).write_bytes(data)
                for key in ('port', 'start_ticks'):
                    changed = copy.deepcopy(config); changed['coordinator'][key] += 1
                    with self.assertRaises(ValueError): runner.verify_coordinator(changed, root / 'bundle', root)
                (remote / 'native_portable.py').write_text('# changed')
                with self.assertRaisesRegex(ValueError, 'source changed'): runner.verify_coordinator(config, root / 'bundle', root)
                private = root / 'private.json'; private.write_text('{"owned":true}'); private.chmod(0o600)
                self.assertTrue(runner.read_private(private, os.getuid())[0]['owned'])
                private.chmod(0o644)
                with self.assertRaisesRegex(ValueError, 'ownership, mode'): runner.read_private(private, os.getuid())
                child.stdin.write('\n'); child.stdin.flush(); self.assertEqual(child.wait(timeout=5), 0)
            finally:
                if child.poll() is None: child.kill(); child.wait(timeout=5)
                child.stdin.close(); child.stdout.close(); child.stderr.close()


if __name__ == '__main__': unittest.main()
