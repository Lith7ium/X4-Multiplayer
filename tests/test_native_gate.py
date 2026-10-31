import contextlib
import io
import json
import tempfile
from unittest import mock
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_relay', ROOT/'tools/dev/native_gate_relay.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
TOKEN = 'a'*32


class NativeBarrierTests(unittest.TestCase):
    def test_report_that_cannot_be_replaced_does_not_end_the_relay(self):
        with tempfile.TemporaryDirectory() as root:
            report = Path(root)/'native-barrier.json'
            report.mkdir()
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertFalse(module.atomic_write(report, '{"first":1}\n'))
            self.assertIn('checkpoint not written', output.getvalue())
            self.assertNotRegex(output.getvalue(), r'Traceback|failure=(?!none)[a-z]')
            report.rmdir()
            self.assertTrue(module.atomic_write(report, '{"second":2}\n'))
            self.assertEqual(json.loads(report.read_text()), {'second': 2})

    def test_udp_reset_during_receive_or_stop_send_keeps_other_peer_served(self):
        def packet(slot,tick):
            bits=struct.pack('>d',20+tick/60).hex()
            return (f"NGSTATE|1|{TOKEN}|{slot}|{tick}|{bits}|{bits}|{'b'*64}".encode(),('127.0.0.1',10000+slot))
        class FakeSocket:
            def __init__(self):
                self.inputs=iter([ConnectionResetError(10054,'closed peer'),packet(1,0),packet(2,0),packet(1,1),packet(2,1),ConnectionResetError(10054,'closed peer')])
                self.sent=[];self.closed=False
            def bind(self, address):pass
            def settimeout(self, value):pass
            def recvfrom(self, size):
                value=next(self.inputs)
                if isinstance(value,Exception):raise value
                return value
            def sendto(self, packet, address):
                self.sent.append((packet,address))
                if packet.startswith(b'NGSTOP') and address[1]==10001:raise ConnectionResetError(10054,'closed peer')
            def close(self):self.closed=True
        sock=FakeSocket()
        with tempfile.TemporaryDirectory() as d:
            report=Path(d)/'result.json'
            argv=['relay','--port','12345','--token',TOKEN,'--ticks','1','--report',str(report)]
            with mock.patch('sys.argv',argv),mock.patch.object(module.socket,'socket',return_value=sock),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(StopIteration):module.main()
            result=json.loads(report.read_text())
        self.assertTrue(sock.closed);self.assertEqual(result['verdict'],'pass');self.assertIsNone(result['failure'])
        self.assertEqual(result['common_ticks'],[0,1])
        self.assertEqual([address[1] for packet,address in sock.sent if packet.startswith(b'NGSTOP')],[10001,10002])

    def test_unrelated_socket_errors_are_not_ignored(self):
        sock=mock.Mock();sock.recvfrom.side_effect=PermissionError('denied')
        with tempfile.TemporaryDirectory() as d:
            argv=['relay','--port','12345','--token',TOKEN,'--report',str(Path(d)/'result.json')]
            with mock.patch('sys.argv',argv),mock.patch.object(module.socket,'socket',return_value=sock),contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(PermissionError):module.main()
        sock.close.assert_called_once()

    def send_command(self, b, slot, tick, command, now=0):
        bits = struct.pack('>d', 20+tick/60).hex()
        return b.receive(f"NGSTATE|2|{TOKEN}|{slot}|{tick}|{bits}|{bits}|{'b'*64}|{command}".encode(), ('127.0.0.1', 10000+slot), now)

    def test_commands_are_immutable_ordered_and_replayed_with_permit(self):
        b = module.Barrier(TOKEN)
        self.assertEqual(self.send_command(b, 2, 0, '01070100'), [])
        out = self.send_command(b, 1, 0, '02010000')
        self.assertEqual(out[0][1], f'NGGO|2|{TOKEN}|1|1|02010000|01070100'.encode())
        self.assertEqual(self.send_command(b, 1, 0, '02010000')[0][1], out[0][1])
        self.assertEqual(self.send(b, 1, 0, 1), [])  # No protocol downgrade.
        self.send_command(b, 1, 0, '00000000')
        self.assertEqual(b.failed, 'changed_state_while_waiting_tick_0')

    def test_missing_or_malformed_commands_cannot_release_native_tick(self):
        b = module.Barrier(TOKEN)
        self.send_command(b, 1, 0, '02010000')
        for command in ('', '0000000', '000000000', '0000000G'):
            self.assertEqual(self.send_command(b, 2, 0, command), [])
        self.assertEqual(b.released, -1)
        self.assertEqual(len(self.send_command(b, 2, 0, '00000000')), 2)

    def send(self, b, slot, tick, now, digest='b'*64, delta=0):
        game = 20 + tick/60 + delta
        bits = struct.pack('>d', game).hex()
        return b.receive(f'NGSTATE|1|{TOKEN}|{slot}|{tick}|{bits}|{bits}|{digest}'.encode(), ('127.0.0.1', 10000+slot), now)

    def test_native_frames_wait_for_both_players_and_hold(self):
        b = module.Barrier(TOKEN, ticks=12, hold_tick=10, hold_seconds=3)
        for tick in range(10):
            self.assertEqual(self.send(b, 1, tick, tick), [])
            self.assertEqual(len(self.send(b, 2, tick, tick)), 2)
        self.send(b, 1, 10, 10)
        self.assertEqual(self.send(b, 2, 10, 10), [])
        self.assertEqual(self.send(b, 2, 10, 12.9), [])
        self.assertEqual(b.released, 9)
        self.assertEqual(len(self.send(b, 1, 10, 13)), 2)
        self.assertEqual(b.summary()['hold_seconds'], 3)

    def test_lost_permit_retransmits_without_advancing(self):
        b = module.Barrier(TOKEN)
        self.send(b, 1, 0, 0);self.send(b, 2, 0, 0)
        retry = self.send(b, 1, 0, 1)
        self.assertEqual(retry[0][1], f'NGGO|1|{TOKEN}|1|1'.encode())
        self.assertEqual(b.released, 0)

    def test_changed_state_while_waiting_is_a_failure(self):
        b = module.Barrier(TOKEN)
        self.send(b, 1, 0, 0)
        self.send(b, 1, 0, 1, delta=.001)
        self.assertEqual(b.failed, 'changed_state_while_waiting_tick_0')

    def test_wrong_native_dt_stops_both(self):
        b = module.Barrier(TOKEN)
        self.send(b, 1, 0, 0);self.send(b, 2, 0, 0)
        self.send(b, 1, 1, 1);out = self.send(b, 2, 1, 1, delta=.01)
        self.assertEqual(b.failed, 'native_dt_mismatch_player_2_tick_1')
        self.assertTrue(all(p.startswith(b'NGSTOP|') for _, p in out))

    def test_tick_rate_sets_the_timestep_and_the_elapsed_real_clock(self):
        for hz in module.TICK_RATES:
            b = module.Barrier(TOKEN, relative_real=True, tick_hz=hz)
            def send(slot, tick, step=1/hz):
                game = struct.pack('>d', 7+tick*step).hex()
                real = struct.pack('>d', slot*10+tick*step).hex()
                return b.receive(f"NGSTATE|1|{TOKEN}|{slot}|{tick}|{game}|{real}|{'b'*64}".encode(), ('127.0.0.1', 10000+slot), tick)
            for tick in range(3):
                send(1, tick);send(2, tick)
            self.assertIsNone(b.failed, hz)
            self.assertEqual(b.summary()['tick_hz'], hz)
        # A client at another rate stops the session on the first step, whichever way round.
        for relay_hz, client_hz in ((30, 60), (60, 30)):
            b = module.Barrier(TOKEN, tick_hz=relay_hz)
            self.send(b, 1, 0, 0);self.send(b, 2, 0, 0)
            self.send(b, 1, 1, 1, delta=1/client_hz-1/60);self.send(b, 2, 1, 1, delta=1/client_hz-1/60)
            self.assertEqual(b.failed, 'native_dt_mismatch_player_2_tick_1')
        for bad in (0, 25, 120, '30', None):
            with self.assertRaises(ValueError):
                module.Barrier(TOKEN, tick_hz=bad)

    def test_actual_state_mismatch_stops_both(self):
        b = module.Barrier(TOKEN)
        self.send(b, 1, 0, 0)
        out = self.send(b, 2, 0, 0, digest='c'*64)
        self.assertEqual(b.failed, 'battle_state_mismatch_tick_0')
        self.assertEqual(len(out), 2)

    def test_observe_records_mismatches_and_keeps_releasing(self):
        b = module.Barrier(TOKEN, ticks=3, hold_tick=99, observe=True)
        for tick in range(4):
            self.send(b, 1, tick, tick)
            out = self.send(b, 2, tick, tick, digest='c'*64 if tick in (1, 3) else 'b'*64)
            self.assertIsNone(b.failed)
            self.assertEqual(len(out), 2)
        self.assertTrue(b.done)
        self.assertTrue(all(p.startswith(b'NGSTOP|') for _, p in out))
        summary = b.summary()
        self.assertEqual(summary['verdict'], 'diverged')
        self.assertEqual(summary['first_digest_mismatch'], 1)
        self.assertEqual(summary['digest_mismatch_ticks'], [1, 3])

    def test_observe_still_stops_on_clock_mismatch(self):
        b = module.Barrier(TOKEN, observe=True)
        self.send(b, 1, 0, 0)
        self.send(b, 2, 0, 0, delta=1/60)
        self.assertEqual(b.failed, 'native_clock_mismatch_tick_0')

    def test_same_steps_at_different_game_times_are_refused(self):
        b = module.Barrier(TOKEN)
        self.send(b, 1, 0, 0)
        out = self.send(b, 2, 0, 0, delta=1/60)
        self.assertEqual(b.failed, 'native_clock_mismatch_tick_0')
        self.assertEqual(len(out), 2)

    def test_foreign_identity_cannot_grant_a_tick(self):
        b = module.Barrier(TOKEN)
        raw=f"NGSTATE|1|{'0'*32}|1|0|{'0'*16}|{'0'*16}|{'b'*64}".encode()
        self.assertEqual(b.receive(raw, ('127.0.0.1', 10001), 0), [])
        self.assertEqual(b.released, -1)

    def test_snapshot_real_clock_uses_explicit_admission_origin(self):
        b = module.Barrier(TOKEN, relative_real=True)
        def send(slot, tick, extra=0):
            game = struct.pack('>d', 7+tick/60).hex()
            real = struct.pack('>d', slot*10+tick/60+extra).hex()
            return b.receive(f"NGSTATE|1|{TOKEN}|{slot}|{tick}|{game}|{real}|{'b'*64}".encode(), ('127.0.0.1', 10000+slot), tick)
        send(1, 0);self.assertEqual(len(send(2, 0)), 2)
        send(1, 1);self.assertEqual(len(send(2, 1)), 2)
        self.assertIsNone(b.failed)
        send(1, 2);send(2, 2, .001)
        self.assertEqual(b.failed, 'native_clock_mismatch_tick_2')


if __name__ == '__main__': unittest.main()
