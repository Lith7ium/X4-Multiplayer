"""N-client lockstep barrier and input wire 3: relay, CLI and overlay (no native game)."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_relay_nslot', ROOT/'tools/dev/native_gate_relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
TOKEN = 'a'*32


def state(slot, tick, version=3, blob='', clients=3, digest='b'*64, delta=0, real=None, token=TOKEN):
    game = 20+(tick if isinstance(tick, int) else 0)/60+delta
    fields = ['NGSTATE', str(version), token, str(slot), str(tick), struct.pack('>d', game).hex(),
              struct.pack('>d', game if real is None else real).hex(), digest]
    if version == 2: fields.append(blob or '00000000')
    if version == 3: fields += [str(clients), blob]
    return '|'.join(fields).encode()


def send(b, slot, tick, now=0, **kw):
    return b.receive(state(slot, tick, **kw), ('127.0.0.1', 10000+(slot if isinstance(slot, int) else 0)), now)


class BarrierNTests(unittest.TestCase):
    def test_three_clients_wait_for_every_client_and_release_together(self):
        for version, tail in ((1, ''), (3, '|||')):
            b = relay.Barrier(TOKEN, clients=3, hold_tick=99)
            self.assertEqual(send(b, 1, 0, version=version), [])
            self.assertEqual(send(b, 3, 0, version=version), [])
            self.assertEqual(b.released, -1)
            out = send(b, 2, 0, version=version)
            self.assertEqual([(address[1], packet) for address, packet in out],
                             [(10000+s, f'NGGO|{version}|{TOKEN}|{s}|1{tail}'.encode()) for s in (1, 2, 3)])
            self.assertEqual(b.released, 0)
            self.assertEqual(b.summary()['common_ticks'], [0])
            self.assertEqual(b.summary()['clients'], 3)

    def test_eight_clients_receive_every_sealed_blob_in_slot_order(self):
        b = relay.Barrier(TOKEN, clients=8, hold_tick=99)
        blobs = {s: format(s, '02x')*s for s in range(1, 8)}
        blobs[8] = 'ab'*240                                   # the 480-character cap
        for s in (8, 3, 1, 7, 2, 6, 4):
            self.assertEqual(send(b, s, 0, clients=8, blob=blobs[s]), [])
        out = send(b, 5, 0, clients=8, blob=blobs[5])
        joined = '|'.join(blobs[s] for s in range(1, 9))
        self.assertEqual([packet for _, packet in out], [f'NGGO|3|{TOKEN}|{s}|1|{joined}'.encode() for s in range(1, 9)])
        self.assertLessEqual(max(len(packet) for _, packet in out), 3910)
        # A lost permit is retransmitted with the same tick-0 blobs.
        self.assertEqual(send(b, 4, 0, clients=8, blob=blobs[4]), [((('127.0.0.1', 10004)), out[3][1])])
        # Tick 1 proposals travel with the permit for tick 2, never earlier.
        later = {s: 'ff'*(s % 3) for s in range(1, 9)}
        for s in range(1, 9):
            out = send(b, s, 1, clients=8, blob=later[s])
        self.assertEqual(out[0][1], f"NGGO|3|{TOKEN}|1|2|{'|'.join(later[s] for s in range(1, 9))}".encode())
        summary = b.summary()
        self.assertEqual(summary['common_ticks'], [0, 1])
        self.assertEqual(summary['states'][0][8]['blob'], blobs[8])
        self.assertEqual(summary['protocol_version'], 3)

    def test_wire3_refuses_malformed_or_foreign_reports(self):
        bad = [state(1, 0, clients=2), state(1, 0, clients=4), state(1, 0, clients='03'),
               state(1, 0, blob='0A'), state(1, 0, blob='0'), state(1, 0, blob='zz'), state(1, 0, blob='0|'),
               state(1, 0, blob='ab'*241), state(4, 0), state(0, 0), state('01', 0), state(1, '00'), state(1, '+0'),
               state(1, 0, token='0'*32), state(1, 0, version=2), state(1, 0)+b'|', state(1, 0).replace(b'NGSTATE|3', b'NGSTATE|4'),
               b'NGSTATE|3|'+b'0'*1015, state(1, '0'*600, version=1)]
        self.assertGreater(len(bad[-2]), 1024)
        self.assertGreater(len(bad[-1]), 512)
        for packet in bad:
            b = relay.Barrier(TOKEN, clients=3)
            self.assertEqual(b.receive(packet, ('127.0.0.1', 10001), 0), [], packet[:80])
            self.assertEqual((b.states, b.version), ({}, None), packet[:80])
        b = relay.Barrier(TOKEN, clients=3)
        self.assertEqual(b.receive(state(1, 0), ('192.0.2.1', 10001), 0), [])  # loopback only
        maximal = state(1, 0, blob='ab'*240)
        self.assertGreater(len(maximal), 512)
        self.assertLessEqual(len(maximal), 1024)
        self.assertEqual(b.receive(maximal, ('127.0.0.1', 10001), 0), [])
        self.assertEqual(b.states[0][1]['blob'], 'ab'*240)
        # No protocol change after the first admitted report.
        for version in (1, 2):
            self.assertEqual(send(b, 2, 0, version=version), [])
        self.assertNotIn(2, b.states[0])

    def test_protocol2_stays_two_players_and_protocol3_runs_with_two(self):
        b = relay.Barrier(TOKEN, clients=3)
        self.assertEqual(send(b, 1, 0, version=2, blob='02010000'), [])
        self.assertEqual(b.states, {})
        b = relay.Barrier(TOKEN)
        send(b, 2, 0, clients=2)
        out = send(b, 1, 0, clients=2, blob='aa')
        self.assertEqual(out[1][1], f'NGGO|3|{TOKEN}|2|1|aa|'.encode())

    def test_changed_blob_while_waiting_is_a_failure(self):
        b = relay.Barrier(TOKEN, clients=3)
        send(b, 1, 0, blob='01')
        send(b, 1, 0, blob='02')
        self.assertEqual(b.failed, 'changed_state_while_waiting_tick_0')

    def test_observe_records_which_slots_differ_per_tick(self):
        b = relay.Barrier(TOKEN, ticks=3, hold_tick=99, observe=True, clients=3)
        digests = {1: {3: 'c'*64}, 2: {2: 'c'*64, 3: 'd'*64}}
        for tick in range(4):
            for slot in (1, 2, 3):
                out = send(b, slot, tick, digest=digests.get(tick, {}).get(slot, 'b'*64))
            self.assertIsNone(b.failed)
            self.assertEqual(len(out), 3)
        self.assertTrue(b.done and all(p.startswith(b'NGSTOP|3|') for _, p in out))
        summary = b.summary()
        self.assertEqual((summary['verdict'], summary['first_digest_mismatch']), ('diverged', 1))
        self.assertEqual(summary['digest_mismatch_ticks'], [1, 2])
        self.assertEqual(summary['digest_mismatch_slots'], {1: [3], 2: [2, 3]})
        self.assertEqual(summary['common_ticks'], [0, 1, 2, 3])
        json.dumps(summary)

    def test_strict_mismatch_on_any_client_stops_every_client(self):
        b = relay.Barrier(TOKEN, clients=4)
        for slot in (1, 2, 3):
            send(b, slot, 0, clients=4)
        out = send(b, 4, 0, clients=4, digest='c'*64)
        self.assertEqual(b.failed, 'battle_state_mismatch_tick_0')
        self.assertEqual(sorted(a[1] for a, p in out if p.startswith(b'NGSTOP|3|')), [10001, 10002, 10003, 10004])
        self.assertEqual(b.summary()['digest_mismatch_slots'], {0: [4]})

    def test_clock_and_timestep_checks_cover_every_client(self):
        b = relay.Barrier(TOKEN, clients=3)
        send(b, 1, 0);send(b, 2, 0);send(b, 3, 0, delta=1/60)
        self.assertEqual(b.failed, 'native_clock_mismatch_tick_0')
        b = relay.Barrier(TOKEN, clients=3)
        for slot in (1, 2, 3): send(b, slot, 0)
        send(b, 1, 1);send(b, 2, 1);out = send(b, 3, 1, delta=.01)
        self.assertEqual(b.failed, 'native_dt_mismatch_player_3_tick_1')
        self.assertTrue(len(out) == 3 and all(p.startswith(b'NGSTOP|') for _, p in out))
        b = relay.Barrier(TOKEN, clients=3, relative_real=True)
        for tick in (0, 1):
            for slot in (1, 2, 3): send(b, slot, tick, real=slot*10+tick/60)
        self.assertIsNone(b.failed)
        for slot in (1, 2, 3): send(b, slot, 2, real=slot*10+2/60+(.001 if slot == 3 else 0))
        self.assertEqual(b.failed, 'native_clock_mismatch_tick_2')

    def test_unsupported_client_counts_are_refused(self):
        for clients in (1, 9, 0, '3', True, 2.0):
            with self.assertRaises(ValueError):
                relay.Barrier(TOKEN, clients=clients)
        with tempfile.TemporaryDirectory() as d:
            argv = ['relay', '--port', '12345', '--token', TOKEN, '--clients', '9', '--report', str(Path(d)/'r.json')]
            with mock.patch('sys.argv', argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    relay.main()

    def test_live_inputs_admit_every_configured_slot(self):
        inbox = relay.Barrier(TOKEN, clients=3, live_inputs=True).live_inputs
        self.assertEqual(sorted(inbox.rows), [1, 2, 3])
        reply = inbox.receive(f'NGCMD|1|{TOKEN}|3|1|03010100'.encode(), ('127.0.0.1', 20003))
        self.assertIn(b'NGQUEUED', reply[0][1])
        self.assertEqual(inbox.receive(f'NGCMD|1|{TOKEN}|4|1|03010100'.encode(), ('127.0.0.1', 20004)), [])
        self.assertEqual(sorted(relay.LiveInputs(TOKEN).rows), [1, 2])

    def test_main_serves_three_clients_and_survives_oversize_datagrams(self):
        def packet(slot, tick):
            return state(slot, tick, blob='0102' if slot == 2 and tick == 0 else ''), ('127.0.0.1', 10000+slot)
        class FakeSocket:
            def __init__(self):
                self.inputs = iter([(b'X'*9000, ('127.0.0.1', 10001))]+[packet(s, t) for t in (0, 1) for s in (3, 1, 2)])
                self.sent, self.sizes, self.closed = [], set(), False
            def bind(self, address): pass
            def settimeout(self, value): pass
            def recvfrom(self, size):
                self.sizes.add(size)
                return next(self.inputs)
            def sendto(self, packet, address): self.sent.append((packet, address))
            def close(self): self.closed = True
        sock = FakeSocket()
        with tempfile.TemporaryDirectory() as d:
            report = Path(d)/'result.json'
            argv = ['relay', '--port', '12345', '--token', TOKEN, '--ticks', '1', '--clients', '3', '--report', str(report)]
            with mock.patch('sys.argv', argv), mock.patch.object(relay.socket, 'socket', return_value=sock), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(StopIteration): relay.main()
            result = json.loads(report.read_text())
        self.assertEqual(sock.sizes, {65536})
        self.assertEqual((result['verdict'], result['clients'], result['common_ticks']), ('pass', 3, [0, 1]))
        self.assertEqual(result['states']['0']['2']['blob'], '0102')
        self.assertIn((f'NGGO|3|{TOKEN}|3|1||0102|'.encode(), ('127.0.0.1', 10003)), sock.sent)
        self.assertEqual([a[1] for p, a in sock.sent if p.startswith(b'NGSTOP')], [10001, 10002, 10003])


class RelayProcessTests(unittest.TestCase):
    def test_real_udp_three_clients_blob_permit_and_checkpoint(self):
        token = TOKEN
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            reservation.close()
            report = Path(directory)/'report.json'
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(port), '--token', token,
                                        '--hold-tick', '99', '--clients', '3', '--report', str(report)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            peers = []
            try:
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                for _ in range(3):
                    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1', 0));peer.settimeout(5);peers.append(peer)
                peers[0].sendto(b'NGSTATE|3|'+b'0'*9000, ('127.0.0.1', port))   # refused, relay keeps serving
                blobs = {1: '', 2: 'ab'*240, 3: '0102'}
                for slot, peer in enumerate(peers, 1):
                    peer.sendto(state(slot, 0, blob=blobs[slot]), ('127.0.0.1', port))
                for slot, peer in enumerate(peers, 1):
                    self.assertEqual(peer.recv(8192).decode(), f"NGGO|3|{token}|{slot}|1||{'ab'*240}|0102")
                saved = json.loads(report.read_text())   # non-empty blobs are checkpointed before release
                self.assertEqual((saved['clients'], saved['common_ticks'], saved['verdict']), (3, [0], 'incomplete'))
                self.assertEqual(saved['states']['0']['2']['blob'], 'ab'*240)
                self.assertIsNone(process.poll())
            finally:
                for peer in peers: peer.close()
                process.terminate();process.communicate(timeout=5)


class ValidatorTests(unittest.TestCase):
    def test_two_client_validator_refuses_wire3_and_n_client_reports(self):
        vspec = importlib.util.spec_from_file_location('validate_native_gate_nslot', ROOT/'tools/dev/validate_native_gate.py')
        validator = importlib.util.module_from_spec(vspec)
        vspec.loader.exec_module(validator)
        for barrier in ({'clients': 3, 'protocol_version': 1, 'states': {'0': {}}}, {'clients': 2, 'protocol_version': 3, 'states': {'0': {}}}):
            with tempfile.TemporaryDirectory() as d:
                run = Path(d)
                (run/'result.json').write_text('{}')
                (run/'native-barrier.json').write_text(json.dumps(barrier))
                with self.assertRaisesRegex(ValueError, 'two-client protocol 1/2'):
                    validator.validate(run, 'clock')


class OverlayTests(unittest.TestCase):
    def build(self, *extra):
        with tempfile.TemporaryDirectory() as d:
            dll = Path(d)/'gate.dll';dll.write_bytes(b'gate')
            fixed = Path(d)/'fixed.dll';fixed.write_bytes(b'fixed')
            out = Path(d)/'overlay'
            result = subprocess.run([sys.executable, str(ROOT/'tools/dev/build_native_gate_overlay.py'), '--gate-dll', str(dll),
                                     '--fixedtick-dll', str(fixed), '--mode', 'clock', '--out', str(out), *extra],
                                    capture_output=True, text=True)
            config = (out/'ui/config.lua.append').read_text() if result.returncode == 0 else None
        return result, config

    def test_session_clients_takes_n_from_the_lane_session(self):
        result, config = self.build('--session-clients')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('X4LAN_CONFIG.native_gate_clients = @X4LANE_CLIENTS@\n', config)
        self.assertIn('X4LAN_CONFIG.slot = "@X4LANE_SLOT@"', config)
        result, config = self.build()
        self.assertNotIn('native_gate_clients', config)
        result, _ = self.build('--session-clients', '--command-test', 'pvp')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--session-clients excludes', result.stderr)


if __name__ == '__main__': unittest.main()
