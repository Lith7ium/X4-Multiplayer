"""Real loopback UDP and child-process persistence; peers remain synthetic."""
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from test_native_portable import TOKEN, contract, frame, hello, packet, participants

ROOT = Path(__file__).resolve().parents[1]


class PortableUDP(unittest.TestCase):
    def start(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for name, value in [('contract', contract()), ('participants', participants())]:
            (root / f'{name}.json').write_text(json.dumps(value))
        reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reservation.bind(('127.0.0.1', 0)); port = reservation.getsockname()[1]; reservation.close()
        proc = subprocess.Popen([sys.executable, str(ROOT / 'tools/dev/native_portable_coordinator.py'),
            '--contract', str(root / 'contract.json'), '--participants', str(root / 'participants.json'),
            '--port', str(port), '--report', str(root / 'report.json'), '--ticks', '4', '--lifetime', '30'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=dict(os.environ, X4MP_PORTABLE_TOKEN=TOKEN),
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))

        def cleanup():
            if proc.poll() is None: proc.terminate()
            proc.communicate(timeout=5)

        self.addCleanup(cleanup)
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(proc.stdout.readline()), daemon=True).start()
        self.assertIn('PORTABLE_COORDINATOR READY', ready.get(timeout=5))
        peers = {}
        for slot in (1, 2):
            peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.addCleanup(peer.close)
            peer.bind(('127.0.0.1', 0)); peer.connect(('127.0.0.1', port)); peer.settimeout(.1)
            peer.send(hello(slot)); peers[slot] = peer
        return root, proc, peers

    def test_durable_hold_and_terminal_receipts_over_actual_udp(self):
        root, proc, peers = self.start()
        for tick in range(5):
            received = set(); deadline = time.monotonic() + 8; started = time.monotonic()
            while len(received) < 2:
                self.assertLess(time.monotonic(), deadline, 'bounded UDP barrier did not reply')
                for slot, peer in peers.items():
                    if slot not in received: peer.send(packet(slot, frame(tick)))
                for slot, peer in peers.items():
                    if slot in received: continue
                    try: data = peer.recv(16384)
                    except socket.timeout: continue
                    fields = data.decode().split('|')
                    if fields[0] == 'NGPGO' and int(fields[4]) < tick + 1: continue
                    self.assertEqual(fields[:4], ['NGPSTOP' if tick == 4 else 'NGPGO', '1', TOKEN, str(slot)])
                    if tick == 4: self.assertEqual(fields[4:], ['4', '0'])
                    else: self.assertEqual(fields[4:], [str(tick + 1), '00000000', '00000000'])
                    report = json.loads((root / 'report.json').read_text())
                    self.assertEqual(set(report['frames'][str(tick)]), {'1', '2'})
                    self.assertFalse(report['native_processes_verified_by_coordinator'])
                    received.add(slot)
            if tick == 3: self.assertGreaterEqual(time.monotonic() - started, 3)
        for slot, peer in peers.items():
            peer.send(f'NGPDONE|1|{TOKEN}|{slot}|4|5|5|1'.encode())
            deadline = time.monotonic() + 3
            while True:
                self.assertLess(time.monotonic(), deadline)
                data = peer.recv(16384)
                if data.startswith(b'NGPCLOSED|'): break
            report = json.loads((root / 'report.json').read_text())
            self.assertEqual(report['terminal'][str(slot)]['returns'], 5)
        self.assertEqual(proc.wait(timeout=5), 0)
        self.assertEqual(json.loads((root / 'report.json').read_text())['verdict'], 'pass')

    def test_changed_waiting_state_is_persisted_and_stops_without_permit(self):
        root, proc, peers = self.start()
        peers[1].send(packet(1, frame()))
        changed = frame(); changed['actors'][0]['active']['transform'][0] = '3f800000'
        peers[1].send(packet(1, changed))
        for slot, peer in peers.items():
            peer.settimeout(3)
            data = peer.recv(16384).decode().split('|')
            self.assertEqual(data[:4], ['NGPSTOP', '1', TOKEN, str(slot)])
            self.assertEqual(data[-1], '1')
        self.assertEqual(proc.wait(timeout=5), 1)
        report = json.loads((root / 'report.json').read_text())
        self.assertEqual(report['failure'], 'changed_frame_while_waiting')
        self.assertEqual(report['released'], -1)
        self.assertEqual(report['verdict'], 'fail')


if __name__ == '__main__': unittest.main()
