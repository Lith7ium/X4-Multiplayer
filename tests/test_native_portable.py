"""Synthetic contract/transport tests, never native gameplay evidence."""
import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
import native_portable as p
from native_portable_coordinator import PortableBarrier


TOKEN = '1' * 32
ADDRESSES = {1: ('127.0.0.1', 30101), 2: ('127.0.0.1', 30102)}


def bits(value, width=64):
    return struct.pack('>d' if width == 64 else '>f', value).hex()


def contract():
    actors = [{'saved': f'{i:016x}', 'fixture_slot': i,
               'macro': 'ship_arg_s_fighter_01_a_macro' if i <= 6 else 'ship_xen_s_fighter_01_a_macro',
               'owner': 'argon' if i <= 6 else 'xenon'} for i in range(1, 13)]
    return {'schema': p.SCHEMA, 'schema_sha256': p.SCHEMA_SHA,
            'snapshot_sha256': 'a' * 64, 'stock_sha256': 'b' * 64,
            'fixture_files': {'md/native_gate_battle.xml': 'c' * 64}, 'actors': actors,
            'coverage': list(p.COVERAGE), 'unsupported': list(p.UNSUPPORTED)}


def frame(tick=0):
    manifest = contract()
    transform = [bits(v, 32) for v in (0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1)]
    actors = []
    for actor in manifest['actors']:
        row = dict(actor, present=True, **{'class': 'ship_small'}, controller='navigation',
                   coverage=list(p.COVERAGE), parent_saved='0000000000000100', hull_bits=bits(100))
        for bank in ('active', 'other'):
            row[bank] = {'transform': list(transform), 'references': ['0000000000000100', '0000000000000100']}
        actors.append(row)
    return {'contract_sha256': p.validate_contract(manifest), 'tick': tick, 'game_bits': bits(7 + tick / 60),
            'calls': tick, 'returns': tick, 'receipt_tick': tick,
            'commands': ['00000000', '00000000'], 'actors': actors}


def participants():
    return {'1': {'platform': 'windows', 'executable_sha256': 'd' * 64, 'agent_sha256': 'e' * 64},
            '2': {'platform': 'linux', 'executable_sha256': 'f' * 64, 'agent_sha256': '0' * 64}}


def hello(slot):
    row = participants()[str(slot)]
    return '|'.join(('NGPHELLO', '1', TOKEN, str(slot), row['platform'], row['executable_sha256'],
                     row['agent_sha256'], p.validate_contract(contract()), p.SCHEMA_SHA)).encode()


def packet(slot, value):
    return '|'.join(('NGPSTATE', '1', TOKEN, str(slot), bits(100 * slot + value['tick'] / 60),
                     str(slot) * 64, p.encode_frame(value, contract()).hex())).encode()


class PortableSchema(unittest.TestCase):
    def test_round_trip_exact_bits_and_domain_separation(self):
        value = frame()
        encoded = p.encode_frame(value, contract())
        self.assertEqual(encoded[:8], b'X4P1FRM\0')
        self.assertEqual(encoded[48:56], struct.pack('<d', 7))
        self.assertEqual(p.decode_frame(encoded, contract()), value)
        self.assertEqual(p.encode_frame(value, contract()), encoded)
        self.assertNotEqual(p.sha(encoded), p.sha(p.encode_actor(value['actors'][0], contract()['actors'][0])))

    def test_fields_expose_separate_divergences(self):
        left, right = frame(), frame()
        right['actors'][0]['hull_bits'] = bits(99)
        right['actors'][3]['active']['transform'][0] = bits(1, 32)
        result = p.compare(left, right, contract())
        self.assertFalse(result['equal'])
        self.assertEqual(result['differences'], ['.actors[0].hull_bits', '.actors[3].active.transform[0]'])
        self.assertIn('shields', result['unsupported'])

    def test_does_not_normalize_negative_zero_or_bank_roles(self):
        left, right = frame(), frame()
        right['actors'][0]['active']['transform'][0] = '80000000'
        self.assertNotEqual(p.encode_frame(left, contract()), p.encode_frame(right, contract()))
        right['actors'][0]['other']['transform'][0] = bits(2, 32)
        other = copy.deepcopy(right)
        other['actors'][0]['active'], other['actors'][0]['other'] = other['actors'][0]['other'], other['actors'][0]['active']
        self.assertFalse(p.compare(right, other, contract())['equal'])

    def test_refuses_unsupported_unknown_missing_nonfinite_or_changed_identity(self):
        changes = [lambda v: v['actors'][0].update(present=False),
                   lambda v: v['actors'][0].update(coverage=[]),
                   lambda v: v['actors'][0].update(controller='docking'),
                   lambda v: v['actors'][0].update(pointer='1234'),
                   lambda v: v['actors'][0].update(hull_bits='7ff8000000000000'),
                   lambda v: v['actors'][0].update(macro='wrong_macro'),
                   lambda v: v['actors'][0]['active']['transform'].__setitem__(0, '7f800000'),
                   lambda v: v['actors'].reverse(), lambda v: v['actors'].pop(),
                   lambda v: v['commands'].__setitem__(1, '00000001'),
                   lambda v: v.update(tick=1), lambda v: v.update(calls=True)]
        for mutation in changes:
            with self.subTest(change=mutation):
                value = frame(); mutation(value)
                with self.assertRaises(ValueError): p.encode_frame(value, contract())

    def test_binary_rejects_truncation_extra_bytes_and_forged_count(self):
        raw = p.encode_frame(frame(), contract())
        for bad in (raw[:-1], raw + b'\0', raw[:88] + b'\x0b\0' + raw[90:], b'X' * 4097):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError): p.decode_frame(bad, contract())

    def test_contract_rejects_coverage_downgrade_artifact_gap_and_duplicate_slot(self):
        mutations = [lambda v: v.update(coverage=[]), lambda v: v.update(fixture_files={}),
                     lambda v: v['actors'][1].update(fixture_slot=1),
                     lambda v: v['actors'].reverse(), lambda v: v.update(schema_sha256='0' * 64),
                     lambda v: v['fixture_files'].update({'../outside': '1' * 64})]
        for mutation in mutations:
            value = contract(); mutation(value)
            with self.assertRaises(ValueError): p.validate_contract(value)


class PortableCoordinator(unittest.TestCase):
    def test_hold_release_proof_survives_bounded_sample_saturation(self):
        barrier, history = self.barrier()
        self.pair(barrier, 0, 0); self.pair(barrier, 1, 1); self.pair(barrier, 2, 2)
        for index in range(300):
            self.assertEqual(barrier.receive(packet(1, frame(2)), ADDRESSES[1], 2.01 + index / 1000), [])
        self.assertEqual(len(barrier.hold_observations), 256)
        self.assertEqual(barrier.receive(packet(1, frame(2)), ADDRESSES[1], 5.1), [])
        self.assertEqual(barrier.hold_release_observations, {})
        self.assertEqual(len(barrier.receive(packet(2, frame(2)), ADDRESSES[2], 5.2)), 2)
        proof = history[-1]['hold_release_observations']
        self.assertEqual(set(proof), {1, 2})
        for slot in (1, 2):
            self.assertGreaterEqual(proof[slot]['elapsed'], 3)
            self.assertEqual(proof[slot]['frame_sha256'], p.sha(p.encode_frame(frame(2), contract())))
        barrier.receive(packet(1, frame(2)), ADDRESSES[1], 8)
        self.assertEqual(barrier.summary()['hold_release_observations'], proof)

    def test_hello_and_stale_frames_do_not_prove_current_tick_hold(self):
        barrier, history = self.barrier()
        self.pair(barrier, 0, 0); self.pair(barrier, 1, 1)
        self.assertEqual(self.pair(barrier, 2, 2), [])
        self.assertEqual(barrier.receive(hello(2), ADDRESSES[2], 6.1), [])
        self.assertEqual(barrier.receive(packet(1, frame(2)), ADDRESSES[1], 6.2), [])
        stale = barrier.receive(packet(2, frame(1)), ADDRESSES[2], 6.3)
        self.assertEqual(len(stale), 1)
        self.assertEqual(stale[0][1].decode().split('|')[4], '2')
        self.assertEqual(barrier.receive(packet(1, frame(2)), ADDRESSES[1], 6.4), [])
        self.assertEqual(barrier.released, 1)
        current = barrier.receive(packet(2, frame(2)), ADDRESSES[2], 6.5)
        self.assertEqual(len(current), 2)
        self.assertEqual(barrier.released, 2)
        self.assertEqual(history[-1]['released'], 2)

    def barrier(self, **options):
        history = []
        barrier = PortableBarrier(TOKEN, contract(), participants(), lambda s: history.append(copy.deepcopy(s)),
                                  ticks=4, hold_tick=2, hold_seconds=3, **options)
        for slot in (1, 2): self.assertEqual(barrier.receive(hello(slot), ADDRESSES[slot], 0), [])
        return barrier, history

    def pair(self, barrier, tick, now):
        self.assertEqual(barrier.receive(packet(1, frame(tick)), ADDRESSES[1], now), [])
        return barrier.receive(packet(2, frame(tick)), ADDRESSES[2], now)

    def test_zero_receipts_ordered_durable_hold_both_peers_and_terminal_ack(self):
        barrier, history = self.barrier()
        for tick in (0, 1):
            replies = self.pair(barrier, tick, tick)
            self.assertEqual(len(replies), 2)
            self.assertTrue(all(p.startswith(b'NGPGO|1|') for _, p in replies))
            self.assertEqual(history[-1]['released'], tick)  # Durable before caller can send replies.
        self.assertEqual(self.pair(barrier, 2, 2), [])
        self.assertEqual(barrier.receive(packet(1, frame(2)), ADDRESSES[1], 5.1), [])
        self.assertEqual(len(barrier.receive(packet(2, frame(2)), ADDRESSES[2], 5.2)), 2)
        self.assertEqual(len(self.pair(barrier, 3, 6)), 2)
        replies = self.pair(barrier, 4, 7)
        self.assertTrue(all(p.startswith(b'NGPSTOP|1|') for _, p in replies))
        self.assertFalse(barrier.done)
        for slot in (1, 2):
            data = f'NGPDONE|1|{TOKEN}|{slot}|4|5|5|1'.encode()
            replies = barrier.receive(data, ADDRESSES[slot], 8)
            self.assertIn(b'NGPCLOSED', replies[0][1])
        self.assertTrue(barrier.done)
        self.assertEqual(history[-1]['verdict'], 'pass')
        self.assertFalse(history[-1]['native_processes_verified_by_coordinator'])
        self.assertGreaterEqual(history[-1]['hold_seconds'], 3)

    def test_schema_mismatch_and_missing_handshake_never_admit(self):
        barrier, _ = self.barrier()
        bad = hello(2).replace(p.SCHEMA_SHA.encode(), b'0' * 64)
        replies = barrier.receive(bad, ADDRESSES[2], 1)
        self.assertEqual(barrier.failure, 'participant_capability_or_artifact_mismatch')
        self.assertTrue(all(p.startswith(b'NGPSTOP|') for _, p in replies))
        fresh = PortableBarrier(TOKEN, contract(), participants(), lambda _: None)
        self.assertEqual(fresh.receive(packet(1, frame()), ADDRESSES[1], 1), [])
        self.assertEqual(fresh.released, -1)

    def test_distinct_field_mismatches_stop_both_without_permit(self):
        barrier, history = self.barrier()
        left, right = frame(), frame()
        right['actors'][1]['other']['references'][0] = '0000000000000101'
        barrier.receive(packet(1, left), ADDRESSES[1], 0)
        replies = barrier.receive(packet(2, right), ADDRESSES[2], 0)
        self.assertEqual(barrier.failure, 'portable_state_mismatch')
        self.assertEqual(len(replies), 2)
        self.assertEqual(history[-1]['first_difference']['differences'], ['.actors[1].other.references[0]'])
        self.assertEqual(barrier.released, -1)

    def test_changed_duplicate_future_tick_and_wrong_endpoint(self):
        for kind in ('changed', 'future', 'endpoint'):
            barrier, _ = self.barrier()
            barrier.receive(packet(1, frame()), ADDRESSES[1], 0)
            value = frame(1 if kind == 'future' else 0)
            if kind == 'changed': value['actors'][0]['hull_bits'] = bits(99)
            address = ('127.0.0.1', 39999) if kind == 'endpoint' else ADDRESSES[1]
            replies = barrier.receive(packet(1, value), address, 1)
            if kind == 'endpoint': self.assertEqual(replies, []); self.assertIsNone(barrier.failure)
            else: self.assertIsNotNone(barrier.failure)
            self.assertEqual(barrier.released, -1)

    def test_persistence_failure_never_returns_permit(self):
        def fail(_): raise OSError('test disk failure')
        barrier, _ = self.barrier(); barrier.persist = fail
        replies = self.pair(barrier, 0, 0)
        self.assertEqual(barrier.failure, 'evidence_persistence_failed')
        self.assertTrue(all(p.startswith(b'NGPSTOP|') for _, p in replies))

    def test_missing_peer_timeout_and_terminal_mismatch(self):
        barrier, _ = self.barrier(peer_timeout=2)
        self.pair(barrier, 0, 1)
        self.assertTrue(barrier.poll(4)); self.assertEqual(barrier.failure, 'missing_peer_or_terminal_ack')
        barrier, _ = self.barrier(); barrier.stopping = True
        barrier.receive(f'NGPDONE|1|{TOKEN}|1|4|5|4|1'.encode(), ADDRESSES[1], 1)
        self.assertEqual(barrier.failure, 'terminal_count_or_restoration_mismatch')

    def test_first_loaded_peer_remains_within_bounded_startup_budget(self):
        barrier, _ = self.barrier(startup_timeout=20, peer_timeout=2)
        barrier.receive(packet(1, frame()), ADDRESSES[1], 1)
        self.assertEqual(barrier.poll(10), [])
        self.assertEqual(barrier.released, -1)
        self.assertEqual(len(barrier.receive(packet(2, frame()), ADDRESSES[2], 11)), 2)
        self.assertTrue(barrier.poll(14))
        self.assertEqual(barrier.failure, 'missing_peer_or_terminal_ack')
        barrier, _ = self.barrier(startup_timeout=20, peer_timeout=2)
        barrier.receive(packet(1, frame()), ADDRESSES[1], 1)
        barrier.receive(hello(2), ADDRESSES[2], 19)
        self.assertTrue(barrier.poll(21))
        self.assertEqual(barrier.failure, 'startup_timeout')
        self.assertEqual(barrier.released, -1)


if __name__ == '__main__': unittest.main()
