"""Protocol 4 lookahead window: relay rules, simulated clients over latency/loss, CLI (no native game)."""
import contextlib
import heapq
import importlib.util
import io
import json
from pathlib import Path
import random
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_gate_relay_lookahead', ROOT/'tools/dev/native_gate_relay.py')
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
TOKEN = 'a'*32


def state(slot, tick, blob='', clients=3, lookahead=2, version=4, digest='b'*64, delta=0, token=TOKEN):
    game = 20+(tick if isinstance(tick, int) else 0)/60+delta
    fields = ['NGSTATE', str(version), token, str(slot), str(tick), struct.pack('>d', game).hex(), struct.pack('>d', game).hex(), digest]
    if version == 2: fields.append(blob or '00000000')
    if version == 3: fields += [str(clients), blob]
    if version == 4: fields += [str(clients), str(lookahead), blob]
    return '|'.join(fields).encode()


def send(b, slot, tick, now=0, **kw):
    kw.setdefault('lookahead', b.lookahead or 2)
    kw.setdefault('clients', b.clients)
    return b.receive(state(slot, tick, **kw), ('127.0.0.1', 10000+slot), now)


def permits(out):
    """{(slot, T): packet} for the NGGO packets of a reply list."""
    found = {}
    for address, packet in out:
        f = packet.decode().split('|')
        if f[0] == 'NGGO':
            found[(address[1]-10000, int(f[4]))] = packet.decode()
    return found


class LookaheadBarrierTests(unittest.TestCase):
    def test_tick0_releases_k_plus_1_permits_and_later_ticks_one_each(self):
        b = relay.Barrier(TOKEN, clients=3, hold_tick=99, lookahead=2)
        blobs = {1: '01', 2: '', 3: 'ff00'}
        self.assertEqual(send(b, 1, 1), [])                       # nothing permitted before tick 0
        self.assertNotIn(1, b.states)
        for slot in (1, 3):
            self.assertEqual(send(b, slot, 0, blob=blobs[slot]), [])
        out = send(b, 2, 0, blob=blobs[2])
        got = permits(out)
        self.assertEqual(sorted(got), sorted((s, t) for t in (1, 2, 3) for s in (1, 2, 3)))
        self.assertEqual(got[(1, 1)], f'NGGO|4|{TOKEN}|1|1|2|||')         # no proposals before tick 0
        self.assertEqual(got[(2, 2)], f'NGGO|4|{TOKEN}|2|2|2|||')
        self.assertEqual(got[(3, 3)], f'NGGO|4|{TOKEN}|3|3|2|01||ff00')   # tick 0 blobs with permit k+1
        self.assertEqual((b.released, b.permitted()), (0, 3))
        # Reports up to the highest permit are admitted, later ones dropped.
        self.assertEqual(send(b, 1, 3), [])
        self.assertEqual(send(b, 1, 4), [])
        self.assertIn(1, b.states[3])
        self.assertNotIn(4, b.states)
        # A retransmitted tick 0 report gets its permits again, sender only.
        again = send(b, 2, 0, blob=blobs[2])
        self.assertEqual([a[1] for a, _ in again], [10002]*3)
        self.assertEqual(sorted(permits(again)), [(2, 1), (2, 2), (2, 3)])
        # Tick 1 complete: exactly permit 1+1+2 = 4, carrying the tick 1 blobs.
        send(b, 1, 1, blob='aa');send(b, 2, 1)
        out = send(b, 3, 1, blob='bb')
        self.assertEqual(permits(out), {(s, 4): f'NGGO|4|{TOKEN}|{s}|4|2|aa||bb' for s in (1, 2, 3)})
        self.assertEqual(b.summary()['lookahead'], 2)
        self.assertEqual(b.summary()['protocol_version'], 4)
        self.assertEqual(b.status(0)['permitted'], 4)

    def test_out_of_order_reports_release_several_ticks_in_order(self):
        b = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=3)
        send(b, 1, 0)
        out = send(b, 2, 0)
        self.assertEqual([int(p.split(b'|')[4]) for _, p in out], [1, 1, 2, 2, 3, 3, 4, 4])
        for tick in (1, 2, 3):
            send(b, 1, tick, blob=f'{tick:02x}')
        send(b, 2, 3);send(b, 2, 2)                                 # tick 1 still missing
        self.assertEqual(b.released, 0)
        out = send(b, 2, 1)
        self.assertEqual([int(p.split(b'|')[4]) for _, p in out], [5, 5, 6, 6, 7, 7])
        self.assertEqual(b.released, 3)
        self.assertEqual(permits(out)[(1, 7)], f'NGGO|4|{TOKEN}|1|7|3|03|')

    def test_limit_stops_without_permits_beyond_ticks(self):
        b = relay.Barrier(TOKEN, ticks=4, clients=2, hold_tick=99, lookahead=2)
        issued = set()
        for tick in range(5):
            for slot in (1, 2):
                out = send(b, slot, tick)
                issued |= {t for (_, t) in permits(out)}
        self.assertEqual(issued, {1, 2, 3, 4})
        self.assertTrue(b.done)
        self.assertEqual([p for _, p in out], [f'NGSTOP|4|{TOKEN}|{s}|4'.encode() for s in (1, 2)])
        self.assertEqual(send(b, 1, 2), [(('127.0.0.1', 10001), f'NGSTOP|4|{TOKEN}|1|2'.encode())])
        self.assertEqual(b.summary()['verdict'], 'pass')
        self.assertEqual(b.summary()['common_ticks'], [0, 1, 2, 3, 4])

    def test_strict_digest_mismatch_is_detected_at_most_k_ticks_late_and_stops_everyone(self):
        b = relay.Barrier(TOKEN, clients=3, hold_tick=99, lookahead=2)
        issued = set()
        for tick in range(6):
            for slot in (1, 2, 3):
                out = send(b, slot, tick, digest='c'*64 if (tick, slot) == (5, 3) else 'b'*64)
                issued |= {t for (_, t) in permits(out)}
        self.assertEqual(b.failed, 'battle_state_mismatch_tick_5')
        self.assertEqual(max(issued), 5+2)                          # clients ran at most k ticks past it
        self.assertTrue(all(p.startswith(b'NGSTOP|4|') and p.endswith(b'|5') for _, p in out))
        self.assertEqual(len(out), 3)
        self.assertEqual(b.summary()['digest_mismatch_slots'], {5: [3]})
        # Any later report gets the stop for its own tick.
        self.assertEqual(send(b, 1, 6), [(('127.0.0.1', 10001), f'NGSTOP|4|{TOKEN}|1|6'.encode())])

    def test_observe_keeps_comparing_digests_with_the_window(self):
        b = relay.Barrier(TOKEN, ticks=8, clients=2, hold_tick=99, observe=True, lookahead=2)
        for tick in range(9):
            for slot in (1, 2):
                send(b, slot, tick, digest='c'*64 if slot == 2 and tick in (3, 4) else 'b'*64)
        summary = b.summary()
        self.assertEqual((summary['verdict'], summary['digest_mismatch_ticks']), ('diverged', [3, 4]))
        self.assertEqual(summary['common_ticks'], list(range(9)))
        self.assertEqual(b.take_notices(), ['NATIVE_GATE DESYNC tick=3 slots=2 first=3 mismatches=1',
                                            'NATIVE_GATE RESYNC tick=5 mismatches=2'])

    def test_timestep_clock_and_changed_report_checks_still_fail(self):
        b = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=2)
        send(b, 1, 0);send(b, 2, 0);send(b, 1, 1)
        send(b, 2, 1, delta=.01)
        self.assertEqual(b.failed, 'native_dt_mismatch_player_2_tick_1')
        b = relay.Barrier(TOKEN, clients=2, hold_tick=99, lookahead=2)
        send(b, 1, 0);send(b, 2, 0);send(b, 1, 2, blob='01')
        out = send(b, 1, 2, blob='02')                             # a changed retransmission inside the window
        self.assertEqual(b.failed, 'changed_state_while_waiting_tick_2')
        self.assertEqual(out, [(('127.0.0.1', 10001), f'NGSTOP|4|{TOKEN}|1|2'.encode())])

    def test_hold_tick_delays_the_permit_it_releases(self):
        b = relay.Barrier(TOKEN, clients=2, hold_tick=3, hold_seconds=1, lookahead=2)
        for tick in range(4):
            send(b, 1, tick, now=0)
            out = send(b, 2, tick, now=0)
        self.assertEqual((b.released, permits(out)), (2, {}))     # permit 6 held
        self.assertEqual(send(b, 1, 3, now=.5), [])
        out = send(b, 2, 4, now=1.2)                                 # any report re-checks the hold
        self.assertEqual(sorted(permits(out)), [(1, 6), (2, 6)])
        self.assertAlmostEqual(b.summary()['hold_seconds'], 1.2)

    def test_mixed_transports_fail_closed_with_a_named_reason(self):
        # Old client (protocol 3) at a lookahead relay.
        b = relay.Barrier(TOKEN, clients=2, lookahead=2)
        out = b.receive(state(1, 0, version=3, clients=2), ('127.0.0.1', 10001), 0)
        self.assertEqual((b.failed, out), ('lookahead_mismatch_slot_1', [(('127.0.0.1', 10001), f'NGSTOP|3|{TOKEN}|1|0'.encode())]))
        self.assertEqual((b.states, b.version), ({}, None))
        # New client at a relay without --lookahead (protocol 3 relay).
        b = relay.Barrier(TOKEN, clients=2)
        out = b.receive(state(2, 0, clients=2, lookahead=2), ('127.0.0.1', 10002), 0)
        self.assertEqual((b.failed, out), ('lookahead_mismatch_slot_2', [(('127.0.0.1', 10002), f'NGSTOP|4|{TOKEN}|2|0'.encode())]))
        # Another window, also after the relay admitted protocol 4.
        b = relay.Barrier(TOKEN, clients=2, lookahead=2)
        send(b, 1, 0)
        out = send(b, 2, 0, lookahead=1)
        self.assertEqual(b.failed, 'lookahead_mismatch_slot_2')
        for version in (1, 2):
            b = relay.Barrier(TOKEN, clients=2, lookahead=1)
            b.receive(state(1, 0, version=version, clients=2), ('127.0.0.1', 10001), 0)
            self.assertEqual(b.failed, 'lookahead_mismatch_slot_1')
        # Unauthenticated or malformed reports never fail the session.
        bad = [state(1, 0, token='0'*32), state(1, 0, lookahead='02'), state(1, 0, lookahead='0'), state(1, 0, lookahead='9'),
               state(1, 0, lookahead='+2'), state(1, 0, blob='0A'), state(1, 0, clients=4), state(1, '01'), state(9, 0),
               state(1, 0)+b'|', state(1, 0).replace(b'|2|', b'||', 1), b'NGSTATE|4|'+b'0'*1015]
        for packet in bad:
            for k in (0, 2):
                b = relay.Barrier(TOKEN, clients=3, lookahead=k)
                self.assertEqual(b.receive(packet, ('127.0.0.1', 10001), 0), [], packet[:90])
                self.assertEqual((b.failed, b.states, b.version), (None, {}, None), packet[:90])
        # A spoofed slot address is dropped before the transport check.
        b = relay.Barrier(TOKEN, clients=2, lookahead=2)
        send(b, 1, 0)
        self.assertEqual(b.receive(state(1, 0, version=3, clients=2), ('127.0.0.1', 20001), 0), [])
        self.assertIsNone(b.failed)

    def test_constructor_and_cli_refuse_invalid_windows(self):
        for k in (-1, 9, '2', True, 2.0, None):
            with self.assertRaises(ValueError):
                relay.Barrier(TOKEN, lookahead=k)
        with self.assertRaises(ValueError):
            relay.Barrier(TOKEN, lookahead=2, live_inputs=True)
        for extra in (['--lookahead', '9'], ['--lookahead', '-1'], ['--lookahead', '2', '--live-inputs']):
            with tempfile.TemporaryDirectory() as d:
                argv = ['relay', '--port', '12345', '--token', TOKEN, '--report', str(Path(d)/'r.json'), *extra]
                with mock.patch('sys.argv', argv), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        relay.main()

    def test_permit_with_inputs_is_checkpointed_before_release(self):
        self.assertTrue(relay.permit_carries_inputs(f'NGGO|4|{TOKEN}|1|7|2||0a'.encode()))
        self.assertFalse(relay.permit_carries_inputs(f'NGGO|4|{TOKEN}|1|7|2||'.encode()))
        self.assertFalse(relay.permit_carries_inputs(f'NGGO|3|{TOKEN}|1|7||0a'.encode()))

        def packet(slot, tick):
            return state(slot, tick, clients=2, lookahead=1, blob='0a' if (slot, tick) == (2, 1) else ''), ('127.0.0.1', 10000+slot)
        writes = []

        class FakeSocket:
            def __init__(self):
                self.inputs = iter([packet(s, t) for t in (0, 1, 2, 3) for s in (1, 2)])
                self.sent = []
            def bind(self, address): pass
            def settimeout(self, value): pass
            def recvfrom(self, size): return next(self.inputs)
            def sendto(self, data, address):
                self.sent.append(data)
                if data.endswith(b'|0a') and not writes:
                    writes.append(json.loads(report.read_text()))
            def close(self): pass
        sock = FakeSocket()
        with tempfile.TemporaryDirectory() as d:
            report = Path(d)/'result.json'
            argv = ['relay', '--port', '12345', '--token', TOKEN, '--ticks', '3', '--clients', '2', '--lookahead', '1',
                    '--hold-tick', '99', '--report', str(report)]
            with mock.patch('sys.argv', argv), mock.patch.object(relay.socket, 'socket', return_value=sock), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(StopIteration): relay.main()
            result = json.loads(report.read_text())
        self.assertEqual(writes[0]['states']['1']['2']['blob'], '0a')      # saved before the permit left
        self.assertIn(f'NGGO|4|{TOKEN}|1|3|1||0a'.encode(), sock.sent)
        self.assertEqual((result['verdict'], result['lookahead'], result['protocol_version']), ('pass', 1, 4))


class SimulatedSession:
    """Discrete-event model of N native clients (lockstep_lookahead.h) and the relay.

    Each client completes tick c at time E, sends report c (seal), and starts tick
    c+1 once it holds the permit for c+1; a frame takes frame(slot, tick) seconds.
    While waiting it resends every window report not sent for 100 ms. Datagrams
    take up/down seconds per client and may be lost. The relay is the real Barrier.
    """

    def __init__(self, k, clients=3, ticks=120, frame=lambda s, t: .031, up=lambda s: .0055, down=lambda s: .0055,
                 loss=0.0, seed=1, digest=lambda s, t: 'b'*64, crash=None, delay=None, observe=False):
        self.k, self.n, self.ticks, self.frame, self.up, self.down = k, clients, ticks, frame, up, down
        self.rng, self.loss, self.digest, self.crash, self.delay = random.Random(seed), loss, digest, crash or {}, delay
        self.barrier = relay.Barrier(TOKEN, ticks=ticks, hold_tick=10**9, clients=clients, lookahead=k, observe=observe)
        self.events, self.sequence = [], 0
        self.completed = {s: 0 for s in range(1, clients+1)}
        self.held = {s: {} for s in range(1, clients+1)}
        self.sent = {s: {} for s in range(1, clients+1)}      # tick -> [packet, last send]
        self.waiting = {s: None for s in range(1, clients+1)}  # wait start or None
        self.stopped = {s: None for s in range(1, clients+1)}
        self.starts = {s: {} for s in range(1, clients+1)}
        self.applied = {s: {} for s in range(1, clients+1)}
        self.late = []
        self.issued = {}                                      # T -> first relay send time

    def proposal(self, slot, tick):
        if self.delay is not None:                            # orders-like: the blob names its apply tick
            return f'{(tick+self.delay) & 0xffff:04x}' if (tick+slot) % 2 else ''
        return f'{slot:02x}{tick & 0xff:02x}' if (tick+slot) % 3 else ''

    def push(self, at, kind, *data):
        self.sequence += 1
        heapq.heappush(self.events, (at, self.sequence, kind, data))

    def lost(self):
        return self.loss and self.rng.random() < self.loss

    def to_relay(self, slot, packet, now):
        if not self.lost():
            self.push(now+self.up(slot), 'relay', slot, packet)

    def report(self, slot, now):
        c = self.completed[slot]
        if self.stopped[slot]:
            return
        if c in self.crash.get(slot, ()):                     # this client stops reporting
            self.stopped[slot] = 'crashed'
            return
        # k=0 is today's protocol 3; k>0 protocol 4.
        packet = state(slot, c, blob=self.proposal(slot, c), clients=self.n, lookahead=self.k, digest=self.digest(slot, c),
                       version=4 if self.k else 3)
        self.sent[slot][c] = [packet, now]
        for old in [t for t in self.sent[slot] if t < c-self.k]:
            del self.sent[slot][old]
        self.to_relay(slot, packet, now)
        self.try_run(slot, now)

    def try_run(self, slot, now):
        want = self.completed[slot]+1
        if want in self.held[slot]:
            self.waiting[slot] = None
            blobs = self.held[slot].pop(want)
            self.starts[slot][want] = now
            self.applied[slot][want] = blobs
            if self.delay is not None:
                for blob in blobs:
                    if blob and int(blob, 16) < want:
                        self.late.append((slot, want, blob))
            self.push(now+self.frame(slot, want), 'done', slot, want)
        elif self.waiting[slot] is None:
            self.waiting[slot] = now
            self.push(now, 'poll', slot)

    def poll(self, slot, now):
        if self.waiting[slot] is None or self.stopped[slot]:
            return
        if now-self.waiting[slot] > 2.0:
            self.stopped[slot] = 7                                # the gate's wait timeout
            return
        for tick, entry in sorted(self.sent[slot].items()):
            if now-entry[1] >= .1-1e-9:
                entry[1] = now
                self.to_relay(slot, entry[0], now)
        self.push(now+.025, 'poll', slot)

    def deliver(self, slot, packet, now):
        f = packet.decode().split('|')
        if self.stopped[slot]:
            return
        if f[0] == 'NGSTOP':
            self.stopped[slot] = 1
            return
        tick, c = int(f[4]), self.completed[slot]
        if self.k:
            assert f[5] == str(self.k) and len(f) == 6+self.n
        assert len(f) == (6 if self.k else 5)+self.n and tick <= c+1+self.k, ('permit beyond the window', slot, tick, c)
        if tick <= c:
            return
        blobs = f[6:] if self.k else f[5:]
        source = tick-1-self.k
        assert blobs[slot-1] == (self.proposal(slot, source) if source >= 0 else ''), 'own proposal changed'
        if tick in self.held[slot]:
            assert self.held[slot][tick] == blobs, 'conflicting permit'
        self.held[slot][tick] = blobs
        if self.waiting[slot] is not None:
            self.try_run(slot, now)

    def at_relay(self, slot, packet, now):
        out = self.barrier.receive(packet, ('127.0.0.1', 10000+slot), now)
        for address, reply in out:
            f = reply.decode().split('|')
            if f[0] == 'NGGO':
                tick, source = int(f[4]), int(f[4])-1-self.k
                # Never a permit before every input it carries exists: all reports of
                # its source tick (tick 0 for the first k+1) are in and checked.
                needed = max(source, 0)
                assert self.barrier.released >= needed and len(self.barrier.states[needed]) == self.n
                expected = [self.barrier.states[source][s]['blob'] if source >= 0 else '' for s in range(1, self.n+1)]
                assert f[6 if self.k else 5:] == expected
                self.issued.setdefault(tick, now)
            target = address[1]-10000
            if not self.lost():
                self.push(now+self.down(target), 'client', target, reply)

    def run(self, horizon=120.0):
        for slot in self.completed:
            self.report(slot, 0.0)
        while self.events:
            now, _, kind, data = heapq.heappop(self.events)
            if now > horizon:
                break
            if kind == 'relay':
                self.at_relay(*data, now)
            elif kind == 'client':
                self.deliver(*data, now)
            elif kind == 'poll':
                self.poll(*data, now)
            elif kind == 'done':
                slot, tick = data
                self.completed[slot] = tick
                self.report(slot, now)
            if all(self.stopped.values()):
                break
        return self

    def period(self, slot=1, first=50, last=110):   # 60 ticks: whole bursts for k+1 = 1..5
        return (self.starts[slot][last]-self.starts[slot][first])/(last-first)


class SimulatedClientTests(unittest.TestCase):
    def assert_identical_inputs(self, sim):
        for tick in sim.applied[1]:
            rows = [sim.applied[s].get(tick) for s in sim.applied]
            self.assertTrue(all(row == rows[0] for row in rows if row is not None), tick)
            source = tick-1-sim.k
            self.assertEqual(rows[0], [sim.proposal(s, source) if source >= 0 else '' for s in range(1, sim.n+1)])

    def test_frame_time_approaches_max_of_frame_and_window_share_of_round_trip(self):
        # Steady state: (k+1)*P = max(k*P, r) + f, so P = max(f, (r+f)/(k+1)).
        for r, f in ((.011, .031), (.06, .031), (.1, .031), (.06, .016)):
            for k in range(0, 5):
                sim = SimulatedSession(k, clients=2, ticks=130, frame=lambda s, t, f=f: f,
                                       up=lambda s, r=r: r/2, down=lambda s, r=r: r/2).run()
                expected = max(f, (r+f)/(k+1))
                self.assertAlmostEqual(sim.period(), expected, delta=expected*.01, msg=(r, f, k))
                self.assertEqual(sim.barrier.summary()['verdict'], 'pass')
                self.assert_identical_inputs(sim)
        # Jitter: one-way 5.5..25.5 ms (round trip 11..51 ms); k=2 absorbs it.
        for k, low, high in ((0, .060, .070), (1, .033, .036), (2, .0305, .0315)):
            rng = random.Random(5)
            sim = SimulatedSession(k, clients=2, ticks=400, up=lambda s: .0055+rng.uniform(0, .02),
                                   down=lambda s: .0055+rng.uniform(0, .02)).run()
            self.assertTrue(low <= sim.period(first=100, last=400) <= high, (k, sim.period(first=100, last=400)))
        # The vps-V2 numbers: 31 ms frames and an 11 ms round trip gave 42 ms frames.
        self.assertAlmostEqual(SimulatedSession(0, clients=2).run().period(), .042, delta=.0005)
        self.assertAlmostEqual(SimulatedSession(2, clients=2).run().period(), .031, delta=.0005)

    def test_mixed_speeds_and_latencies_keep_inputs_identical_and_never_early(self):
        for k in (1, 2, 4):
            for seed in range(4):
                rng = random.Random(seed)
                frames = {s: rng.uniform(.008, .05) for s in range(1, 5)}
                ups = {s: rng.uniform(.001, .06) for s in range(1, 5)}
                downs = {s: rng.uniform(.001, .06) for s in range(1, 5)}
                sim = SimulatedSession(k, clients=4, ticks=90, seed=seed,
                                       frame=lambda s, t: frames[s]*(1.5 if t % 7 == 0 else 1),
                                       up=lambda s: ups[s], down=lambda s: downs[s]).run()
                self.assertEqual(sim.barrier.summary()['verdict'], 'pass', (k, seed))
                self.assertEqual(sim.barrier.summary()['common_ticks'], list(range(91)))
                self.assertEqual(set(sim.stopped.values()), {1})
                self.assert_identical_inputs(sim)
                # No client ever ran more than k ticks past the slowest report.
                for slot in sim.starts:
                    for tick, at in sim.starts[slot].items():
                        self.assertGreaterEqual(at, sim.issued[tick])

    def test_lost_datagrams_are_recovered_by_resends(self):
        for k in (1, 3):
            sim = SimulatedSession(k, clients=3, ticks=150, loss=.05, seed=k).run()
            self.assertEqual(sim.barrier.summary()['verdict'], 'pass')
            self.assertEqual(set(sim.stopped.values()), {1})
            self.assert_identical_inputs(sim)

    def test_inputs_with_delay_k_plus_1_are_never_late(self):
        # Orders-like blobs name their apply tick (proposal tick + delay).
        k = 2
        sim = SimulatedSession(k, clients=3, ticks=60, delay=k+1).run()
        self.assertEqual((sim.late, sim.barrier.summary()['verdict']), ([], 'pass'))
        sim = SimulatedSession(k, clients=3, ticks=60, delay=k).run()
        self.assertTrue(sim.late)                               # why the probe refuses delay < k+1

    def test_a_missing_report_stops_every_client_inside_the_window(self):
        k = 2
        sim = SimulatedSession(k, clients=3, ticks=100, crash={2: range(20, 10**6)}).run()
        self.assertEqual(max(sim.issued), 19+1+k)               # nothing released past the last report
        self.assertEqual(max(max(starts) for starts in sim.starts.values()), 19+1+k)
        self.assertEqual(sim.stopped, {1: 7, 2: 'crashed', 3: 7})  # the others time out, none guesses
        self.assertEqual(sim.barrier.summary()['verdict'], 'incomplete')

    def test_digest_mismatch_stops_every_client_within_k_ticks(self):
        k = 3
        sim = SimulatedSession(k, clients=2, ticks=100, digest=lambda s, t: 'c'*64 if (s, t) == (2, 40) else 'b'*64).run()
        self.assertEqual(sim.barrier.failed, 'battle_state_mismatch_tick_40')
        self.assertLessEqual(max(max(starts) for starts in sim.starts.values()), 40+k)
        self.assertEqual(set(sim.stopped.values()), {1})


class RelayProcessLookaheadTests(unittest.TestCase):
    def test_real_udp_two_clients_with_window_2(self):
        with tempfile.TemporaryDirectory() as directory, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            reservation.close()
            report = Path(directory)/'report.json'
            process = subprocess.Popen([sys.executable, str(ROOT/'tools/dev/native_gate_relay.py'), '--port', str(port), '--token', TOKEN,
                                        '--hold-tick', '99', '--clients', '2', '--ticks', '4', '--lookahead', '2', '--report', str(report)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            peers = []
            try:
                self.assertEqual(process.stdout.readline().strip(), 'NATIVE_GATE RELAY READY')
                for _ in range(2):
                    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    peer.bind(('127.0.0.1', 0));peer.settimeout(5);peers.append(peer)
                for slot, peer in enumerate(peers, 1):
                    peer.sendto(state(slot, 0, clients=2, blob='0a' if slot == 1 else ''), ('127.0.0.1', port))
                for slot, peer in enumerate(peers, 1):
                    got = [peer.recv(8192).decode() for _ in range(3)]
                    self.assertEqual(got, [f'NGGO|4|{TOKEN}|{slot}|{t}|2|{"0a" if t == 3 else ""}|' for t in (1, 2, 3)])
                for tick in (1, 2, 3, 4):
                    for slot, peer in enumerate(peers, 1):
                        peer.sendto(state(slot, tick, clients=2), ('127.0.0.1', port))
                for slot, peer in enumerate(peers, 1):
                    got = [peer.recv(8192).decode() for _ in range(2)]
                    self.assertEqual(got, [f'NGGO|4|{TOKEN}|{slot}|4|2||', f'NGSTOP|4|{TOKEN}|{slot}|4'])
                # The relay prints its status line after the final report is written.
                for line in iter(process.stdout.readline, ''):
                    if 'done=True' in line: break
                saved = json.loads(report.read_text())
                self.assertEqual((saved['verdict'], saved['lookahead'], saved['common_ticks']), ('pass', 2, [0, 1, 2, 3, 4]))
            finally:
                for peer in peers: peer.close()
                process.terminate();process.communicate(timeout=5)


if __name__ == '__main__': unittest.main()
