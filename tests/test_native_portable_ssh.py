"""Bounded process/UDP transport tests; all protocol peers are synthetic."""
import copy
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest import mock

from test_native_portable import TOKEN, contract, frame, hello, packet, participants
import native_portable_ssh as ssh
import portable_import_capability as cap
from native_portable import canonical_json

ROOT = Path(__file__).resolve().parents[1]


def config():
    return dict(token=TOKEN, contract=contract(), participants=participants(), ticks=4,
                hold_tick=2, hold_seconds=.15, startup_timeout=3, peer_timeout=2, lifetime=9)


class PortableTransport(unittest.TestCase):
    def test_new_contract_adds_capability_module_to_exact_provenance_only(self):
        old=contract();new=copy.deepcopy(old);new['snapshot_sha256']=cap.SNAPSHOT_SHA256
        new['import_math']=cap.capability(new['snapshot_sha256'])
        self.assertEqual(ssh.provenance(),ssh.provenance(old))
        hashes=ssh.provenance(new)
        self.assertEqual(set(hashes),set(ssh.provenance())|{'portable_import_capability.py','portable_import_capability_v1.py'})
        self.assertEqual(hashes['portable_import_capability.py'],hashlib.sha256((ROOT/'tools/dev/portable_import_capability.py').read_bytes()).hexdigest())
        self.assertEqual(hashes['portable_import_capability_v1.py'],hashlib.sha256((ROOT/'tools/dev/portable_import_capability_v1.py').read_bytes()).hexdigest())
        bad=copy.deepcopy(new);bad['import_math']['descriptor_sha256']='0'*64
        with self.assertRaises(ValueError):ssh.provenance(bad)
    def test_config_and_channel_bounds(self):
        for key, value in [('ticks', True), ('lifetime', 3), ('peer_timeout', 0),
                           ('startup_timeout', 1801), ('hold_seconds', float('nan')),
                           ('token', 'not-private-token')]:
            value_config = config(); value_config[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): ssh.validate_config(value_config)
        self.assertIsNone(ssh.auth_slot(hello(1).replace(TOKEN.encode(), b'2' * 32), TOKEN))
        for bad in ('GG', '0', '00' * (ssh.MAX_PACKET + 1)):
            with self.assertRaises(ValueError): ssh.unpack_packet(dict(kind='packet', hex=bad))

    def session(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        root = Path(temporary.name); events = []; udp = []
        session = ssh.RemoteCoordinator(config(), root / 'report.json', events.append,
                                        lambda data, address: udp.append((address, data)), 0)
        session.receive(hello(1), ssh.WINDOWS, 0, 'ssh')
        session.receive(hello(2), ('127.0.0.1', 31002), 0, 'udp')
        return session, events, udp, root

    def test_checkpoint_precedes_permits_and_cross_channel_spoof_stops(self):
        session, events, udp, root = self.session()
        session.receive(packet(1, frame()), ssh.WINDOWS, 0, 'ssh')
        session.receive(packet(2, frame()), ('127.0.0.1', 31002), 0, 'udp')
        self.assertEqual([e['kind'] for e in events], ['checkpoint', 'packet'])
        self.assertEqual(events[1]['sequence'], events[0]['sequence'])
        self.assertIn(b'NGPGO', udp[-1][1])
        self.assertEqual(json.loads((root / 'report.json').read_text())['released'], 0)
        session.receive(hello(1), ('127.0.0.1', 31002), 1, 'udp')
        self.assertEqual(session.barrier.failure, 'transport_slot_route_mismatch')
        self.assertTrue(udp[-1][1].startswith(b'NGPSTOP'))

    def test_checkpoint_hash_is_json_roundtrip_stable_past_single_digit_ticks(self):
        session, events, _, root = self.session()
        report = session.barrier.summary()
        report['frames'] = {tick: {1: {'tick': tick}, 2: {'tick': tick}} for tick in range(13)}
        session.persist(report)
        checkpoint = json.loads(canonical_json(events[-1]))
        normalized = checkpoint['report']
        self.assertEqual(set(normalized['frames']), {str(tick) for tick in range(13)})
        self.assertEqual(set(normalized['frames']['10']), {'1', '2'})
        self.assertEqual(checkpoint['sha256'], hashlib.sha256(canonical_json(normalized)).hexdigest())
        self.assertEqual(json.loads((root / 'report.json').read_text()), normalized)
        self.assertEqual(checkpoint, json.loads(canonical_json(checkpoint)))

    def test_broken_ssh_output_still_persists_failure_and_stops_linux(self):
        session, _, udp, root = self.session()
        def broken(_): raise BrokenPipeError('closed owned test pipe')
        session.output = broken
        session.abort('ssh_input_eof')
        self.assertEqual(session.barrier.failure, 'ssh_input_eof')
        self.assertTrue(session.barrier.stopping)
        report = json.loads((root / 'report.json').read_text())
        self.assertEqual(report['failure'], 'ssh_input_eof')
        self.assertTrue(report['ssh_checkpoint_output_failed'])
        self.assertTrue(udp[-1][1].startswith(b'NGPSTOP'))
        self.assertTrue(udp[-1][1].endswith(b'|1'))
        # The real remote loop persists again at exit and may abort again when
        # that output fails. Neither path may replace the first cause.
        for _ in range(2):
            with self.assertRaises(ssh.CheckpointOutputError): session.persist(session.barrier.summary())
            session.abort('remote_transport_failure')
            self.assertEqual(session.barrier.failure, 'ssh_input_eof')
            self.assertEqual(json.loads(session.report.read_text()), report)

    def test_checkpoint_output_failure_prevents_grants_and_stays_distinct_from_disk(self):
        for error_type in (BrokenPipeError, RuntimeError, ValueError):
            with self.subTest(error=error_type):
                session, events, udp, _ = self.session()
                session.output = mock.Mock(side_effect=error_type('owned test output failure'))
                session.receive(packet(1, frame()), ssh.WINDOWS, 0, 'ssh')
                with self.assertRaises(ssh.CheckpointOutputError):
                    session.receive(packet(2, frame()), ('127.0.0.1', 31002), 0, 'udp')
                self.assertEqual(udp, []); self.assertEqual(events, [])
                self.assertEqual(session.barrier.failure, 'ssh_checkpoint_output_failed')
                self.assertTrue(session.barrier.stopping)
                report = json.loads(session.report.read_text())
                self.assertEqual(report['failure'], 'ssh_checkpoint_output_failed')
                self.assertTrue(report['ssh_checkpoint_output_failed'])
                self.assertEqual(set(report['frames']['0']), {'1', '2'})
                # Even if a caller observes the exception and a later output
                # succeeds, the latched failure cannot grant a retransmission.
                session.output = events.append
                session.receive(packet(2, frame()), ('127.0.0.1', 31002), .1, 'udp')
                session.abort('remote_transport_failure')
                self.assertTrue(udp)
                self.assertTrue(all(data.startswith(b'NGPSTOP') for _, data in udp))
                self.assertEqual(json.loads(session.report.read_text())['failure'], 'ssh_checkpoint_output_failed')

    def test_actual_disk_failure_keeps_strict_persistence_failure(self):
        session, events, udp, _ = self.session()
        session.receive(packet(1, frame()), ssh.WINDOWS, 0, 'ssh')
        with mock.patch.object(ssh, 'atomic_report', side_effect=OSError('owned disk failure')):
            session.receive(packet(2, frame()), ('127.0.0.1', 31002), 0, 'udp')
        self.assertEqual(session.barrier.failure, 'evidence_persistence_failed')
        self.assertTrue(session.barrier.stopping)
        self.assertFalse(session.checkpoint_output_failed)
        self.assertTrue(all(data.startswith(b'NGPSTOP') for _, data in udp))
        self.assertTrue(all(bytes.fromhex(row['hex']).startswith(b'NGPSTOP') for row in events))

    def test_disk_failure_recording_secondary_output_error_is_not_hidden(self):
        session, _, udp, _ = self.session()
        original = ssh.atomic_report
        writes = []
        def fail_second(path, report):
            writes.append(report)
            if len(writes) == 2: raise OSError('owned second disk write failed')
            return original(path, report)
        session.output = mock.Mock(side_effect=BrokenPipeError('closed owned test pipe'))
        with mock.patch.object(ssh, 'atomic_report', side_effect=fail_second):
            session.abort('ssh_input_eof')
        self.assertEqual(session.barrier.failure, 'evidence_persistence_failed')
        self.assertTrue(session.checkpoint_output_failed)
        self.assertTrue(udp[-1][1].startswith(b'NGPSTOP'))
        # The unsuccessful second write must not be claimed as durable.
        self.assertEqual(json.loads(session.report.read_text())['failure'], 'ssh_input_eof')
        self.assertNotIn('ssh_checkpoint_output_failed', json.loads(session.report.read_text()))

    def test_bridge_requires_matching_durable_checkpoint_before_permit(self):
        session, events, _, root = self.session()
        session.receive(packet(1, frame()), ssh.WINDOWS, 0, 'ssh')
        session.receive(packet(2, frame()), ('127.0.0.1', 31002), 0, 'udp')
        # JSON conversion reproduces integer-key serialization across SSH.
        checkpoint, response = json.loads(json.dumps(events))
        evidence = ssh.BridgeEvidence(config(), root / 'bridge.json')
        with self.assertRaises(ValueError): evidence.packet(response)
        bad = copy.deepcopy(checkpoint); bad['sha256'] = '0' * 64
        with self.assertRaises(ValueError): evidence.checkpoint(bad)
        evidence.checkpoint(checkpoint)
        self.assertIn(b'NGPGO', evidence.packet(response))
        for changed in (dict(response, sequence=0), dict(response, sequence=2),
                        dict(response, hex=bytes.fromhex(response['hex']).replace(b'00000000', b'00000001').hex())):
            with self.assertRaises(ValueError): evidence.packet(changed)
        with self.assertRaises(ValueError): evidence.checkpoint(checkpoint)


@unittest.skipUnless(sys.platform == 'linux', 'actual Linux owned subprocess and loopback sockets')
class PortableRemoteProcess(unittest.TestCase):
    def prepare(self, contract=None):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for name in ssh.provenance(contract): shutil.copyfile(ROOT / 'tools/dev' / name, root / name)
        return root

    def test_actual_new_mode_closure_requires_capability_module_before_readiness(self):
        cfg=config();cfg['contract']['snapshot_sha256']=cap.SNAPSHOT_SHA256
        cfg['contract']['import_math']=cap.capability(cap.SNAPSHOT_SHA256)
        for missing in (None,'portable_import_capability.py','portable_import_capability_v1.py'):
            with self.subTest(missing=missing):
                root=self.prepare(cfg['contract'])
                if missing:(root/missing).unlink()
                process=subprocess.run([sys.executable,'-B',str(root/'native_portable_ssh.py'),'--remote'],
                    input=canonical_json(cfg)+b'\n',stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=8)
                self.assertEqual(process.returncode,1)  # Owned stdin EOF is a refusal, never a session pass.
                if missing:
                    self.assertEqual(process.stdout,b'');self.assertIn(b'portable_import_capability',process.stderr)
                else:
                    self.assertEqual(process.stderr,b'')
                    ready=json.loads(process.stdout.splitlines()[0])
                    self.assertEqual(ready['hashes'],ssh.provenance(cfg['contract']))
                    self.assertEqual(len(ready['hashes']),5)
                    report=json.loads(Path(ready['report_path']).read_text())
                    self.assertEqual(report['failure'],'ssh_input_eof');self.assertEqual(report['released'],-1)

    def remote(self):
        root = self.prepare()
        proc = subprocess.Popen([sys.executable, str(root / 'native_portable_ssh.py'), '--remote'],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def cleanup():
            if proc.poll() is None: proc.terminate()
            proc.communicate(timeout=5)
        self.addCleanup(cleanup)
        proc.stdin.write(canonical_json(config()) + b'\n'); proc.stdin.flush()
        events = queue.Queue(); threading.Thread(target=ssh.read_events, args=(proc.stdout, events), daemon=True).start()
        ready = events.get(timeout=5)
        self.assertEqual(ready['kind'], 'ready'); self.assertEqual(ready['hashes'], ssh.provenance())
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.addCleanup(udp.close)
        udp.bind(('127.0.0.1', 0)); udp.connect(('127.0.0.1', ready['port'])); udp.settimeout(3)
        udp.send(hello(2))
        proc.stdin.write(canonical_json(dict(kind='packet', hex=hello(1).hex())) + b'\n'); proc.stdin.flush()
        time.sleep(.05)
        return root, proc, events, udp, ready

    def test_remote_stdin_eof_stops_actual_udp_peer_and_persists_failure(self):
        _, proc, events, udp, ready = self.remote()
        proc.stdin.close(); proc.stdin = None
        self.assertTrue(udp.recv(ssh.MAX_PACKET).endswith(b'|0|1'))
        self.assertEqual(proc.wait(timeout=5), 1)
        report = json.loads(Path(ready['report_path']).read_text())
        self.assertEqual(report['failure'], 'ssh_input_eof'); self.assertEqual(report['released'], -1)

    def test_actual_closed_stdout_and_stdin_eof_preserve_primary_and_secondary_failures(self):
        root = self.prepare()
        with subprocess.Popen([sys.executable, '-B', str(root / 'native_portable_ssh.py'), '--remote'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
            try:
                proc.stdin.write(canonical_json(config()) + b'\n'); proc.stdin.flush()
                ready = json.loads(proc.stdout.readline())
                self.assertEqual(ready['kind'], 'ready')
                self.assertEqual(ready['pid'], proc.pid)
                self.assertEqual(ready['hashes'], ssh.provenance())
                # No background reader keeps the pipe alive. This is a real
                # kernel EPIPE during the abort checkpoint and final persist.
                proc.stdout.close(); proc.stdout = None
                proc.stdin.close(); proc.stdin = None
                proc.communicate(timeout=5)
                self.assertNotEqual(proc.returncode, 0)
                report = json.loads(Path(ready['report_path']).read_text())
                self.assertEqual(report['failure'], 'ssh_input_eof')
                self.assertTrue(report['ssh_checkpoint_output_failed'])
                self.assertEqual(report['released'], -1)
                self.assertEqual(report['frames'], {})
                self.assertEqual(report['hello_slots'], [])
                self.assertEqual(report['verdict'], 'fail')
            finally:
                if proc.poll() is None: proc.terminate()
                proc.communicate(timeout=5)

    def test_actual_closed_stdout_at_first_checkpoint_sends_udp_stop_never_go(self):
        root = self.prepare()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp, subprocess.Popen(
                [sys.executable, '-B', str(root / 'native_portable_ssh.py'), '--remote'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
            try:
                proc.stdin.write(canonical_json(config()) + b'\n'); proc.stdin.flush()
                ready = json.loads(proc.stdout.readline())
                proc.stdout.close(); proc.stdout = None
                udp.bind(('127.0.0.1', 0)); udp.connect(('127.0.0.1', ready['port'])); udp.settimeout(.1)
                for data in (hello(1), packet(1, frame())):
                    proc.stdin.write(canonical_json(dict(kind='packet', hex=data.hex())) + b'\n')
                proc.stdin.flush()
                deadline = time.monotonic() + 5
                while True:
                    self.assertLess(time.monotonic(), deadline)
                    udp.send(hello(2)); udp.send(packet(2, frame()))
                    try: reply = udp.recv(ssh.MAX_PACKET)
                    except socket.timeout: continue
                    self.assertTrue(reply.startswith(b'NGPSTOP'))
                    self.assertTrue(reply.endswith(b'|0|1'))
                    break
                proc.stdin.close(); proc.stdin = None
                proc.communicate(timeout=5)
                self.assertNotEqual(proc.returncode, 0)
                report = json.loads(Path(ready['report_path']).read_text())
                self.assertEqual(report['failure'], 'ssh_checkpoint_output_failed')
                self.assertTrue(report['ssh_checkpoint_output_failed'])
                self.assertEqual(set(report['frames']['0']), {'1', '2'})
                self.assertEqual(report['verdict'], 'fail')
            finally:
                if proc.poll() is None: proc.terminate()
                proc.communicate(timeout=5)

    def test_remote_authentic_changed_frame_stops_both_channels(self):
        _, proc, events, udp, ready = self.remote()
        changed = frame(); changed['actors'][0]['hull_bits'] = '4058c00000000000'
        for value in (frame(), changed):
            proc.stdin.write(canonical_json(dict(kind='packet', hex=packet(1, value).hex())) + b'\n')
            proc.stdin.flush()
        self.assertTrue(udp.recv(ssh.MAX_PACKET).startswith(b'NGPSTOP'))
        checkpoint = events.get(timeout=3); response = events.get(timeout=3)
        self.assertEqual(checkpoint['report']['failure'], 'changed_frame_while_waiting')
        self.assertTrue(bytes.fromhex(response['hex']).startswith(b'NGPSTOP'))
        self.assertEqual(proc.wait(timeout=5), 1)

    def test_complete_bridge_and_direct_linux_udp_share_one_coordinator(self):
        root = self.prepare(); private_config = root / 'private.json'
        run_config = config(); run_config['ticks'] = 12
        private_config.write_text(json.dumps(run_config))
        reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reserve.bind(('127.0.0.1', 0)); port = reserve.getsockname()[1]; reserve.close()
        args = SimpleNamespace(config=private_config, report=root / 'bridge.json', port=port,
                               ssh_host='unused', remote_script='unused', runner_owned=False)
        result = queue.Queue()
        worker = threading.Thread(target=lambda: result.put(ssh.bridge(args,
            command=[sys.executable, str(root / 'native_portable_ssh.py'), '--remote'])), daemon=True)
        worker.start(); deadline = time.monotonic() + 5
        ready_file = root / 'bridge-remote.json'
        while not ready_file.exists():
            self.assertLess(time.monotonic(), deadline); time.sleep(.01)
        remote = json.loads(ready_file.read_text()); peers = {}
        for slot, target in ((1, port), (2, remote['port'])):
            peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.addCleanup(peer.close)
            peer.bind(('127.0.0.1', 0)); peer.connect(('127.0.0.1', target)); peer.settimeout(.05)
            peer.send(hello(slot)); peers[slot] = peer
        for tick in range(13):
            accepted = set(); deadline = time.monotonic() + 2
            dropped_first_windows_go = False
            while len(accepted) < 2:
                self.assertLess(time.monotonic(), deadline)
                for slot, peer in peers.items():
                    if slot not in accepted: peer.send(packet(slot, frame(tick)))
                for slot, peer in peers.items():
                    if slot in accepted: continue
                    try: data = peer.recv(ssh.MAX_PACKET)
                    except socket.timeout: continue
                    fields = data.decode().split('|')
                    if fields[0] == 'NGPGO' and int(fields[4]) < tick + 1: continue
                    self.assertEqual(fields[0], 'NGPSTOP' if tick == 12 else 'NGPGO', f'tick {tick}')
                    if tick == 0 and slot == 1 and not dropped_first_windows_go:
                        dropped_first_windows_go = True
                        continue  # Retransmit unchanged frame0 through the actual bridge.
                    report = json.loads((root / 'bridge.json').read_text())
                    if slot == 1: self.assertEqual(set(report['coordinator']['frames'][str(tick)]), {'1', '2'})
                    accepted.add(slot)
        for slot, peer in peers.items():
            peer.send(f'NGPDONE|1|{TOKEN}|{slot}|12|13|13|1'.encode()); deadline = time.monotonic() + 2
            while True:
                self.assertLess(time.monotonic(), deadline)
                try: data = peer.recv(ssh.MAX_PACKET)
                except socket.timeout: continue
                if data.startswith(b'NGPCLOSED'): break
        self.assertEqual(result.get(timeout=5), 0); worker.join(timeout=1)
        final = json.loads((root / 'bridge.json').read_text())
        self.assertTrue(final['transport_complete']); self.assertEqual(final['ssh_exit_code'], 0)
        self.assertEqual(final['coordinator']['verdict'], 'pass')
        self.assertEqual(set(final['coordinator']['frames']), {str(tick) for tick in range(13)})
        self.assertEqual(final['coordinator']['terminal']['1']['returns'], 13)
        self.assertEqual(final['coordinator']['terminal']['2']['returns'], 13)
        self.assertFalse(final['native_processes_verified_by_transport'])
        self.assertEqual(final['local_native_endpoint'], {'transport': 'loopback_udp', 'address': list(peers[1].getsockname())})
        self.assertEqual(final['coordinator']['peer_endpoints'], {
            '1': {'transport': 'ssh_virtual', 'address': ['127.0.0.1', 1]},
            '2': {'transport': 'loopback_udp', 'address': list(peers[2].getsockname())}})

    def test_bad_provenance_stderr_and_early_eof_cannot_complete_bridge(self):
        for mode in ('provenance', 'stderr', 'eof'):
            with self.subTest(mode=mode):
                root = self.prepare(); (root / 'private.json').write_text(json.dumps(config()))
                reserve = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                reserve.bind(('127.0.0.1', 0)); port = reserve.getsockname()[1]; reserve.close()
                hashes = ssh.provenance()
                if mode == 'provenance': hashes['native_portable_ssh.py'] = '0' * 64
                ready = dict(kind='ready', platform='linux', hashes=hashes, pid=12345,
                             start_ticks=1, port=32123, report_path='/private/test/report.json')
                script = root / 'fake_owned_transport.py'
                script.write_text('import json,sys\nsys.stdin.readline()\nprint(' + repr(json.dumps(ready))
                                  + ',flush=True)\n' + ('print("test transport stderr",file=sys.stderr,flush=True)\n'
                                                       if mode == 'stderr' else ''))
                args = SimpleNamespace(config=root / 'private.json', report=root / 'bridge.json', port=port,
                                       ssh_host='unused', remote_script='unused', runner_owned=False)
                self.assertEqual(ssh.bridge(args, command=[sys.executable, str(script)]), 1)
                final = json.loads((root / 'bridge.json').read_text())
                self.assertFalse(final['transport_complete'])
                self.assertIsNone(final['coordinator'])
                if mode == 'stderr': self.assertGreater(final['ssh_stderr_bytes'], 0)


if __name__ == '__main__': unittest.main()
