"""Owned temporary files/processes only; never starts X4 or display helpers."""
import copy
from contextlib import ExitStack, redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import linux_portable_prepare as prepare
from test_native_portable import participants


CAP = {'mode': 'portable-fixture12-zero-v1', 'contract_sha256': '1' * 64}
REVIEW = '2' * 64


def request(root):
    return {'schema': 'x4-portable-prepare-v1', 'run_id': 'owned-test', **CAP,
        'review_sha256': REVIEW, 'participants': participants(), 'ticks': 12,
        'startup_timeout_ms': 600000, 'timeout_ms': 120000,
        'remote_script': (root / 'coordinator/native_portable_ssh.py').as_posix()}


def private(path, value):
    path.write_bytes(prepare.canonical_json(value)); path.chmod(0o600)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture(root):
    root.chmod(0o700); game = root / 'game'; game.mkdir(); bundle = root / 'bundle'; bundle.mkdir()
    files = {}
    for i in range(127):
        raw = str(i).encode(); name = f'owned-{i}.bin'; (game / name).write_bytes(raw)
        files[name] = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
    dlc = [f'ego_dlc_test{i}' for i in range(7)]
    for name in dlc: (game / 'extensions' / name).mkdir(parents=True)
    stock = {'files': files, 'dlc': dlc, 'payloads_hashed': True}
    digest = hashlib.sha256(prepare.canonical_json(stock)).hexdigest()
    (bundle / 'stock.json').write_bytes(prepare.canonical_json({**stock, 'manifest_sha256': digest}))
    req = root / 'request.json'; private(req, request(root))
    return game, bundle, {'stock_sha256': digest}, req, root / 'handoff.json'


class RequestContract(unittest.TestCase):
    def test_nested_handoff_input_duplicates_refuse(self):
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            prepare.decode_control(b'{"config":{"token":"redacted","token":"redacted"},"prepared":{}}')

    def test_no_fictitious_coordinator_token_or_extra_fields_in_prepare_request(self):
        value = request(Path('/private'))
        self.assertEqual(prepare.validate_request(value, CAP, participants()['2'], REVIEW), value)
        for mutate in (lambda x: x.update(token='0' * 32), lambda x: x.update(coordinator={}),
                       lambda x: x.update(ticks=True), lambda x: x.update(review_sha256='3' * 64)):
            bad = copy.deepcopy(value); mutate(bad)
            with self.assertRaises(ValueError): prepare.validate_request(bad, CAP, participants()['2'], REVIEW)


@unittest.skipUnless(sys.platform == 'linux', 'Linux descriptor and signal semantics')
class OwnedPreparation(unittest.TestCase):
    @unittest.skipUnless(getattr(os, 'geteuid', lambda: -1)() == 0, 'publisher owns root-only control files')
    def test_publisher_rechecks_both_lifetimes_and_cleans_temporary_files(self):
        import linux_portable_handoff as publisher
        for failure in ('none', 'first_coordinator', 'preparer', 'second_coordinator'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); _, bundle, contract, req, handoff = fixture(root)
                (root / '.x4mp-admission-test-root').touch(); os.chown(root, 1004, -1)
                args = SimpleNamespace(root=root, bundle=bundle, review_sha256=REVIEW, prepare_request=req,
                                       handoff=handoff, config=root / 'final.json')
                config = {k: v for k, v in request(root).items() if k not in ('schema', 'run_id', 'review_sha256')}
                config.update(token='8' * 32, coordinator={})
                value = {'config': config, 'prepared': {'pid': 123, 'start_ticks': 456}}
                readfd, writefd = os.pipe(); handles = []; coordinator_calls = 0
                def coordinator(*_):
                    nonlocal coordinator_calls
                    coordinator_calls += 1
                    if (failure == 'first_coordinator' and coordinator_calls == 1
                            or failure == 'second_coordinator' and coordinator_calls == 2):
                        raise ValueError('owned coordinator refused')
                    fd = os.dup(readfd); handles.append(fd); return fd
                def preparer(*_):
                    if failure == 'preparer': raise ValueError('owned preparer refused')
                    fd = os.dup(readfd); handles.append(fd); return fd
                try:
                    with mock.patch.object(publisher.runner, 'review_bundle', return_value=({'participant': participants()['2']}, contract)), \
                            mock.patch.object(publisher.runner, 'validate_session'), \
                            mock.patch.object(publisher.runner, 'reviewed_capability', return_value=CAP), \
                            mock.patch.object(publisher.runner, 'verify_coordinator', side_effect=coordinator), \
                            mock.patch.object(prepare, 'verify_preparer', side_effect=preparer):
                        if failure == 'none':
                            result = publisher.publish(args, value)
                            self.assertEqual(result['preparing_pid'], 123)
                            self.assertTrue(handoff.exists()); self.assertEqual(coordinator_calls, 2)
                            actual, digest = prepare.read_control(root, args.config)
                            self.assertEqual(actual, config); self.assertEqual(result['config_sha256'], digest)
                        else:
                            with self.assertRaises(ValueError): publisher.publish(args, value)
                            self.assertFalse(handoff.exists())
                    self.assertEqual(list(root.glob('.portable-handoff-*')), [])
                    for fd in handles:
                        with self.assertRaises(OSError): os.fstat(fd)
                    if args.config.exists(): self.assertEqual(args.config.stat().st_mode & 0o777, 0o600)
                finally: os.close(readfd); os.close(writefd)

    def _runner_boundary(self, cancel_stage):
        import linux_native_portable_run as runner
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary).resolve() / 'home'; home.mkdir()
            root = home / 'x4mp-linux-admission-test'; root.mkdir()
            game, bundle, contract, req, handoff = fixture(root)
            (root / '.x4mp-admission-test-root').touch()
            (home / 'x4').mkdir(); (home / 'x4/game').symlink_to(game, target_is_directory=True)
            review = {'participant': participants()['2']}
            args = SimpleNamespace(root=root, bundle=bundle, review_sha256=REVIEW,
                                   config=None, prepare_request=req, handoff=handoff)
            actual = prepare.Preparation; guards = []
            def create(*positional, **keywords):
                guard = actual(*positional, **keywords, uid=os.getuid(), stdin_fd=None)
                guards.append(guard)
                def stop_before_handoff(_validate):
                    self.assertFalse((root / 'cases').exists())
                    self.assertEqual(len(guard.stock.rows), 127)
                    if cancel_stage:
                        guard.config_path = root / 'final.json'
                        return {}, '0' * 64
                    raise RuntimeError('test stops at waiting boundary')
                guard.wait = stop_before_handoff
                return guard
            def stage(case, *_):
                self.assertTrue(cancel_stage)
                (case / 'extensions/x4_lan_lab').mkdir()
                guards[0]._cancel(None, None)
            output = io.StringIO()
            with mock.patch('pwd.getpwnam', return_value=SimpleNamespace(pw_dir=str(home), pw_uid=os.getuid(), pw_gid=os.getgid())), \
                    mock.patch.object(runner.os, 'geteuid', return_value=0), \
                    mock.patch.object(runner, '__file__', str(bundle / 'bin/linux_native_portable_run.py')), \
                    mock.patch.object(runner, 'review_bundle', return_value=(review, contract)), \
                    mock.patch.object(runner, 'reviewed_capability', return_value=CAP), \
                    mock.patch.object(runner.isolation, 'sha', return_value=runner.isolation.EXE_SHA), \
                    mock.patch.object(runner.isolation, 'processes', return_value={}), \
                    mock.patch.object(runner.isolation, 'steam_identity', return_value={'owned': True}), \
                    mock.patch.object(runner.isolation, 'memory_available_bytes', return_value=1), \
                    mock.patch.object(prepare, 'Preparation', side_effect=create), \
                    mock.patch.object(runner, 'stage_fixture', side_effect=stage), \
                    mock.patch.object(runner.subprocess, 'Popen', side_effect=AssertionError('child before handoff')), \
                    mock.patch.object(runner.subprocess, 'run', side_effect=AssertionError('helper before handoff')), \
                    redirect_stdout(output):
                with self.assertRaisesRegex(RuntimeError, 'cancelled' if cancel_stage else 'waiting boundary'), ExitStack() as resources:
                    runner.run(args, resources)
            self.assertIn('PORTABLE_PREPARED ', output.getvalue())
            self.assertEqual(len(guards), 1); self.assertTrue(guards[0].stock.closed)
            for row in guards[0].stock.rows.values():
                with self.assertRaises(OSError): os.fstat(row['fd'])

    def test_actual_runner_reaches_prepared_before_case_display_probe_or_game(self):
        self._runner_boundary(False)

    def test_actual_runner_cancellation_during_staging_refuses_xauth_helper(self):
        self._runner_boundary(True)

    def test_partial_hash_cancel_closes_every_acquired_descriptor(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); game, bundle, contract, req, handoff = fixture(root)
            rows = json.loads((bundle / 'stock.json').read_text())['files']; held = prepare.StockFiles(); calls = 0
            def cancel():
                nonlocal calls
                calls += 1
                if calls == 5: raise RuntimeError('owned cancellation')
            with self.assertRaisesRegex(RuntimeError, 'cancellation'): held.acquire(game, rows, cancel)
            self.assertTrue(held.closed); self.assertGreater(len(held.rows), 0)
            for row in held.rows.values():
                with self.assertRaises(OSError): os.fstat(row['fd'])

    def test_same_size_write_restored_mtime_and_path_replacement_refuse(self):
        for mutation in ('content', 'replacement', 'symlink'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); game, bundle, _, _, _ = fixture(root)
                held = prepare.StockFiles().acquire(game, json.loads((bundle / 'stock.json').read_text())['files'], lambda: None)
                try:
                    target = game / 'owned-0.bin'; before = target.stat()
                    if mutation == 'content':
                        time.sleep(.002); target.write_bytes(b'x'); os.utime(target, ns=(before.st_atime_ns, before.st_mtime_ns))
                    else:
                        other = game / 'replacement'; other.write_bytes(target.read_bytes()); target.unlink()
                        if mutation == 'replacement': other.rename(target)
                        else: target.symlink_to(other)
                    with self.assertRaises(ValueError): held.recheck()
                finally: held.close()

    def test_control_parent_and_target_links_duplicate_json_and_public_mode_refuse(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); root.chmod(0o700)
            path = root / 'request.json'; private(path, {'ok': True})
            link = root / 'linked'; link.symlink_to(path)
            with self.assertRaises(ValueError): prepare.read_control(root, link, os.getuid())
            alias = root / 'alias'; alias.symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError): prepare.read_control(alias, alias / path.name, os.getuid())
            path.write_text('{"a":1,"a":2}')
            with self.assertRaisesRegex(ValueError, 'duplicate'): prepare.read_control(root, path, os.getuid())
            path.chmod(0o644)
            with self.assertRaises(ValueError): prepare.read_control(root, path, os.getuid())
            with self.assertRaises(ValueError): prepare.publish_handoff(root, link, {'ok': True}, os.getuid())

    def test_publication_race_leaves_foreign_target_and_removes_owned_temporary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); root.chmod(0o700); target = root / 'handoff.json'
            original = prepare._rename_noreplace
            def race(directory, source, destination):
                (root / destination).write_bytes(b'foreign')
                original(directory, source, destination)
            with mock.patch.object(prepare, '_rename_noreplace', side_effect=race):
                with self.assertRaises(FileExistsError): prepare.publish_handoff(root, target, {'ours': True}, os.getuid())
            self.assertEqual(target.read_bytes(), b'foreign')
            self.assertEqual(list(root.glob('.portable-handoff-*')), [])

    def test_receiver_can_read_immediately_after_rename_before_publisher_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); root.chmod(0o700); target = root / 'handoff.json'
            original = prepare._rename_noreplace; observations = []
            def interleave(directory, source, destination):
                original(directory, source, destination)
                self.assertFalse((root / source).exists())
                observations.append(prepare.read_control(root, root / destination, os.getuid())[0])
                self.assertEqual((root / destination).stat().st_nlink, 1)
            with mock.patch.object(prepare, '_rename_noreplace', side_effect=interleave):
                prepare.publish_handoff(root, target, {'owned': True}, os.getuid())
            self.assertEqual(observations, [{'owned': True}])
            self.assertEqual(list(root.glob('.portable-handoff-*')), [])

    def test_missing_atomic_rename_refuses_without_fallback_or_temporary_leak(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); root.chmod(0o700); target = root / 'handoff.json'
            with mock.patch.object(prepare.ctypes, 'CDLL', return_value=object()):
                with self.assertRaisesRegex(RuntimeError, 'unavailable'):
                    prepare.publish_handoff(root, target, {'owned': True}, os.getuid())
            self.assertFalse(target.exists())
            self.assertEqual(list(root.glob('.portable-handoff-*')), [])

    def test_one_shot_handoff_binds_process_request_and_final_config(self):
        for mutation in ('none', 'pid', 'start', 'request', 'hash', 'session'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); game, bundle, contract, req, target = fixture(root)
                guard = prepare.Preparation(root, req, target, CAP, participants()['2'], REVIEW, uid=os.getuid(), stdin_fd=None)
                try:
                    with self.assertRaisesRegex(ValueError, 'not ready'): guard.accept(lambda v: None)
                    with self.assertRaisesRegex(ValueError, 'stock verification'): guard.prepared()
                    guard.hash_stock(bundle, contract, game); receipt = guard.prepared()
                    with self.assertRaisesRegex(ValueError, 'one-shot'): guard.prepared()
                    with self.assertRaisesRegex(ValueError, 'one-shot'): guard.hash_stock(bundle, contract, game)
                    config = {k: v for k, v in request(root).items() if k not in ('schema', 'run_id', 'review_sha256')}
                    config.update(token='4' * 32, coordinator={'actual': 'owned-fixture'})
                    if mutation == 'session': config['ticks'] = 11
                    config_path = root / 'config.json'; digest = private(config_path, config)
                    row = {'schema': 'x4-portable-handoff-v1', 'pid': receipt['pid'], 'start_ticks': receipt['start_ticks'],
                        'request_sha256': receipt['request_sha256'], 'config_path': str(config_path), 'config_sha256': digest}
                    if mutation == 'pid': row['pid'] += 1
                    if mutation == 'start': row['start_ticks'] += 1
                    if mutation == 'request': row['request_sha256'] = '5' * 64
                    if mutation == 'hash': row['config_sha256'] = '6' * 64
                    prepare.publish_handoff(root, target, row, os.getuid())
                    self.assertEqual(list(root.glob('.portable-handoff-*')), [])
                    if mutation == 'none':
                        self.assertEqual(guard.accept(lambda v: None)[0], config)
                        with self.assertRaisesRegex(ValueError, 'already consumed'): guard.accept(lambda v: None)
                    else:
                        with self.assertRaises(ValueError): guard.accept(lambda v: None)
                    with self.assertRaises(ValueError): prepare.publish_handoff(root, target, row, os.getuid())
                finally: guard.close()

    def test_deadline_and_controller_eof_are_refusals(self):
        for failure in ('timeout', 'eof', 'signal'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve(); _, _, _, req, target = fixture(root)
                readfd, writefd = os.pipe(); now = [0]
                guard = prepare.Preparation(root, req, target, CAP, participants()['2'], REVIEW,
                    uid=os.getuid(), clock=lambda: now[0], stdin_fd=readfd)
                try:
                    if failure == 'timeout': now[0] = prepare.PREPARE_SECONDS
                    elif failure == 'eof': os.close(writefd); writefd = None
                    else: guard._cancel(None, None)
                    with self.assertRaises((RuntimeError, TimeoutError)): guard.check()
                    self.assertFalse(target.exists())
                finally:
                    guard.close(); os.close(readfd)
                    if writefd is not None: os.close(writefd)

    def test_real_waiting_process_pidfd_and_same_supervisor_handoff(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve(); game, bundle, contract, req, target = fixture(root)
            scripts = bundle / 'bin'; scripts.mkdir(); script = scripts / 'linux_native_portable_run.py'
            helper_dir = str(Path(prepare.__file__).parent)
            script.write_text('''import json,os,sys
from pathlib import Path
sys.path.insert(0, sys.argv[0].rsplit('/',3)[0])
sys.path.insert(0, ''' + repr(helper_dir) + ''')
import linux_portable_prepare as p
r=Path(sys.argv[sys.argv.index('--root')+1]);b=Path(sys.argv[sys.argv.index('--bundle')+1])
q=r/'request.json';h=r/'handoff.json';v,_=p.read_control(r,q,os.getuid())
g=p.Preparation(r,q,h,{'mode':v['mode'],'contract_sha256':v['contract_sha256']},v['participants']['2'],v['review_sha256'],uid=os.getuid())
try:
 s=json.loads((b/'stock.json').read_text());g.hash_stock(b,{'stock_sha256':s['manifest_sha256']},r/'game')
 print(json.dumps(g.prepared()),flush=True)
 g.wait(lambda value:None);g.stock.recheck(g.active_check)
 print(json.dumps({'accepted_pid':os.getpid(),'same_start':g.start}),flush=True)
finally:g.close()
''')
            args = ['python3', '-B', str(script), '--root', str(root), '--bundle', str(bundle),
                '--review-sha256', REVIEW, '--prepare-request', str(req), '--handoff', str(target)]
            child = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertTrue(select.select([child.stdout], [], [], 8)[0])
                receipt = json.loads(child.stdout.readline())
                fd = prepare.verify_preparer(root, bundle, req, target, receipt, REVIEW, contract['stock_sha256'], os.getuid())
                try:
                    bad = dict(receipt, start_ticks=receipt['start_ticks'] + 1)
                    with self.assertRaises(ValueError): prepare.verify_preparer(root, bundle, req, target, bad, REVIEW, contract['stock_sha256'], os.getuid())
                    config = {k: v for k, v in request(root).items() if k not in ('schema', 'run_id', 'review_sha256')}
                    config.update(token='7' * 32, coordinator={})
                    path = root / 'config.json'; digest = private(path, config)
                    prepare.publish_handoff(root, target, {'schema': 'x4-portable-handoff-v1', 'pid': child.pid,
                        'start_ticks': receipt['start_ticks'], 'request_sha256': receipt['request_sha256'],
                        'config_path': str(path), 'config_sha256': digest}, os.getuid())
                    self.assertTrue(select.select([child.stdout], [], [], 8)[0])
                    accepted = json.loads(child.stdout.readline())
                    self.assertEqual(accepted, {'accepted_pid': child.pid, 'same_start': receipt['start_ticks']})
                    self.assertEqual(child.wait(timeout=8), 0, child.stderr.read())
                    poll = select.poll(); poll.register(fd, select.POLLIN); self.assertTrue(poll.poll(0))
                finally: os.close(fd)
            finally:
                if child.poll() is None: child.terminate(); child.wait(timeout=5)
                child.stdin.close(); child.stdout.close(); child.stderr.close()


if __name__ == '__main__': unittest.main()
