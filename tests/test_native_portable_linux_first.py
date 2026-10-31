"""Opt-in first Linux frame checkpoint; all peers are synthetic."""
import hashlib
import json
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

import native_portable as portable
import native_portable_ssh as ssh
import portable_import_capability as cap
from test_native_portable import TOKEN, frame
from test_native_portable_ssh import config


def first_config():
    value = config()
    value['contract']['snapshot_sha256'] = cap.SNAPSHOT_SHA256
    value['contract']['import_math'] = cap.capability(cap.SNAPSHOT_SHA256)
    value['linux_first'] = True
    return value


def hello(cfg, slot):
    row = cfg['participants'][str(slot)]
    return '|'.join(('NGPHELLO', '1', TOKEN, str(slot), row['platform'], row['executable_sha256'],
        row['agent_sha256'], portable.validate_contract(cfg['contract']), portable.SCHEMA_SHA)).encode()


def state(cfg, slot, tick=0):
    value = frame(tick); value['contract_sha256'] = portable.validate_contract(cfg['contract'])
    raw = portable.encode_frame(value, cfg['contract'])
    return '|'.join(('NGPSTATE', '1', TOKEN, str(slot), '4059000000000000',
                     hashlib.sha256(raw).hexdigest(), raw.hex())).encode()


class FirstLinuxCheckpoint(unittest.TestCase):
    def session(self, cfg=None):
        directory = tempfile.TemporaryDirectory(); self.addCleanup(directory.cleanup)
        cfg = first_config() if cfg is None else cfg
        events, udp = [], []
        coordinator = ssh.RemoteCoordinator(cfg, Path(directory.name) / 'report.json', events.append,
            lambda data, address: udp.append((address, data)), 0)
        return coordinator, cfg, events, udp

    def test_requires_explicit_true_and_euler_contract(self):
        for value in (False, 1, 'true', None):
            cfg = first_config(); cfg['linux_first'] = value
            with self.subTest(value=value), self.assertRaises(ValueError): ssh.validate_config(cfg)
        cfg = config(); cfg['linux_first'] = True
        with self.assertRaises(ValueError): ssh.validate_config(cfg)
        self.assertIs(ssh.validate_config(first_config())['linux_first'], True)
        self.assertNotIn('linux_first', ssh.validate_config(config()))

    def test_first_accepted_linux_frame_is_durable_once_with_no_grant(self):
        session, cfg, events, udp = self.session(); address = ('127.0.0.1', 31002)
        session.receive(hello(cfg, 2), address, 0, 'udp')
        self.assertFalse(session.report.exists())
        data = state(cfg, 2)
        session.receive(data, address, .1, 'udp')
        self.assertEqual([row['kind'] for row in events], ['checkpoint'])
        self.assertEqual(udp, [])
        report = json.loads(session.report.read_text())
        self.assertEqual(report['verdict'], 'incomplete'); self.assertIsNone(report['failure'])
        self.assertEqual(report['released'], -1); self.assertEqual(report['terminal'], {})
        self.assertEqual(report['hello_slots'], [2]); self.assertEqual(set(report['frames']), {'0'})
        self.assertEqual(set(report['frames']['0']), {'2'})
        self.assertEqual(report['peer_endpoints']['2']['address'], list(address))
        for now in (.2, .3, .4): session.receive(data, address, now, 'udp')
        self.assertEqual(len(events), 1); self.assertEqual(udp, [])
        session.receive(hello(cfg, 1), ssh.WINDOWS, .5, 'ssh')
        session.receive(state(cfg, 1), ssh.WINDOWS, .5, 'ssh')
        self.assertEqual([row['kind'] for row in events], ['checkpoint', 'checkpoint', 'packet'])
        self.assertTrue(udp[-1][1].startswith(b'NGPGO'))

    def test_omitted_option_preserves_unpaired_default_behavior(self):
        cfg = first_config(); del cfg['linux_first']
        session, cfg, events, udp = self.session(cfg); address = ('127.0.0.1', 31002)
        session.receive(hello(cfg, 2), address, 0, 'udp')
        session.receive(state(cfg, 2), address, .1, 'udp')
        self.assertFalse(session.report.exists()); self.assertEqual(events, []); self.assertEqual(udp, [])

    def test_missing_untrusted_future_or_slot1_frame_cannot_be_readiness(self):
        for scenario in ('no_hello', 'token', 'future', 'slot1_present', 'timeout'):
            with self.subTest(scenario=scenario):
                session, cfg, events, udp = self.session(); address = ('127.0.0.1', 31002)
                if scenario != 'no_hello': session.receive(hello(cfg, 2), address, 0, 'udp')
                if scenario == 'slot1_present': session.receive(hello(cfg, 1), ssh.WINDOWS, 0, 'ssh')
                if scenario == 'timeout': session.dispatch(session.barrier.poll(4))
                else:
                    data = state(cfg, 2, 1 if scenario == 'future' else 0)
                    if scenario == 'token': data = data.replace(TOKEN.encode(), b'f' * 32)
                    session.receive(data, address, .1, 'udp')
                self.assertFalse(session.first_linux_checkpoint)
                self.assertTrue(all(data.startswith(b'NGPSTOP') for _, data in udp))
                self.assertTrue(all(row['report']['failure'] for row in events if row['kind'] == 'checkpoint'))

    def test_disk_and_checkpoint_errors_at_first_linux_frame_stop_without_go(self):
        for failure in ('disk', 'stdout'):
            with self.subTest(failure=failure):
                session, cfg, events, udp = self.session(); address = ('127.0.0.1', 31002)
                session.receive(hello(cfg, 2), address, 0, 'udp')
                if failure == 'disk':
                    with mock.patch.object(ssh, 'atomic_report', side_effect=OSError('owned disk fault')):
                        session.receive(state(cfg, 2), address, .1, 'udp')
                    self.assertEqual(session.barrier.failure, 'evidence_persistence_failed')
                else:
                    session.output = mock.Mock(side_effect=BrokenPipeError('owned pipe closed'))
                    with self.assertRaises(ssh.CheckpointOutputError):
                        session.receive(state(cfg, 2), address, .1, 'udp')
                    session.abort('ssh_input_eof')
                    report = json.loads(session.report.read_text())
                    self.assertEqual(report['failure'], 'ssh_checkpoint_output_failed')
                    self.assertTrue(report['ssh_checkpoint_output_failed'])
                self.assertFalse(session.first_linux_checkpoint)
                self.assertTrue(session.barrier.stopping)
                self.assertTrue(udp)
                self.assertTrue(all(data.startswith(b'NGPSTOP') for _, data in udp))

    @unittest.skipUnless(sys.platform == 'linux', 'actual Linux owned subprocess')
    def test_actual_linux_first_checkpoint_and_eof_without_windows_peer(self):
        cfg = first_config()
        with tempfile.TemporaryDirectory() as temporary, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            root = Path(temporary); source = Path(ssh.__file__).resolve().parent
            for name in ssh.provenance(cfg['contract']): shutil.copyfile(source / name, root / name)
            with subprocess.Popen([sys.executable, '-B', str(root / 'native_portable_ssh.py'), '--remote'],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
                try:
                    proc.stdin.write(portable.canonical_json(cfg) + b'\n'); proc.stdin.flush()
                    events = queue.Queue()
                    reader = threading.Thread(target=ssh.read_events, args=(proc.stdout, events), daemon=True)
                    reader.start(); ready = events.get(timeout=5)
                    self.assertEqual(ready['kind'], 'ready')
                    self.assertEqual(ready['hashes'], ssh.provenance(cfg['contract']))
                    udp.bind(('127.0.0.1', 0)); udp.connect(('127.0.0.1', ready['port'])); udp.settimeout(.1)
                    udp.send(hello(cfg, 2)); udp.send(state(cfg, 2))
                    checkpoint = events.get(timeout=5)
                    self.assertEqual(checkpoint['kind'], 'checkpoint')
                    self.assertEqual(checkpoint['report']['hello_slots'], [2])
                    self.assertEqual(checkpoint['report']['released'], -1)
                    self.assertEqual(checkpoint['report']['verdict'], 'incomplete')
                    self.assertEqual(json.loads(Path(ready['report_path']).read_text()), checkpoint['report'])
                    self.assertEqual(checkpoint['sha256'], hashlib.sha256(portable.canonical_json(checkpoint['report'])).hexdigest())
                    udp.send(state(cfg, 2))
                    with self.assertRaises(socket.timeout): udp.recv(ssh.MAX_PACKET)
                    self.assertTrue(events.empty())
                    proc.stdin.close(); proc.stdin = None; udp.settimeout(3)
                    self.assertTrue(udp.recv(ssh.MAX_PACKET).startswith(b'NGPSTOP'))
                    self.assertEqual(proc.wait(timeout=5), 1); reader.join(timeout=1)
                    report = json.loads(Path(ready['report_path']).read_text())
                    self.assertEqual(report['failure'], 'ssh_input_eof'); self.assertEqual(report['released'], -1)
                finally:
                    if proc.poll() is None: proc.terminate()
                    proc.communicate(timeout=5)


if __name__ == '__main__': unittest.main()
