from pathlib import Path
import struct
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
from native_construction_raw_evidence import sample, local_detail, same_frame, reassemble

HANDLE = '0000000500000003'
RUNTIME = 18446744073709551614
PRIVATE = 'SHIP_RAW_MOTION_LOCAL_1;runtime=fffffffffffffffe;refs=' + ','.join(['0'*16]*5)


def fixture(tick=6):
    vectors = struct.pack('<18f', 1, 2, 3, 1, 0, 0, 0, 1, 0, 0, 0, 1, 4, 5, 6, 0, 0, 0)
    bank = vectors + bytes(16) + bytes(5)
    motion = struct.pack('<I', 0x2c51158) + bank*2
    pose = bytes(8) + (bank[:48]+bank[72:88])*2
    return f'SHIP_RAW_MOTION_1;handle={HANDLE};slot=15;tick={tick};sector_pose_supported=0;identity_ok=1;stable=1;read_before=0;write_before=1;read_after=0;write_after=1;unknown_references=0;reference_flags=00000000;parent=0000000000000000;raw_pose={pose.hex()};motion={motion.hex()};timing={bytes(45).hex()}'


class PassiveConstructionRaw(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.module = self.lua.execute((ROOT/'tools/dev/native_construction_raw.lua').read_text())

    def decode(self, raw):
        return sample(raw, handle=HANDLE, tick=6, slot=15)

    def test_passive_layout_decodes_without_deriving_sector_pose(self):
        raw = fixture();native = self.module.decode(raw, HANDLE, 15, 6);decoded = self.decode(raw)
        self.assertEqual(native.pose, decoded['raw_pose'])
        self.assertFalse(decoded['sector_pose_supported'])
        self.assertEqual(local_detail(PRIVATE, native_id=str(RUNTIME), canonical=decoded), [0]*5)
        self.assertEqual(self.module.local_detail(PRIVATE), PRIVATE)

    def test_identity_selectors_unknown_flags_and_truncated_fields_are_refused(self):
        raw = fixture()
        for before, after in (('slot=15', 'slot=13'), ('tick=6;', 'tick=5;'),
                              ('sector_pose_supported=0', 'sector_pose_supported=1'),
                              ('stable=1', 'stable=0'), ('read_after=0', 'read_after=1'),
                              ('write_before=1', 'write_before=0'), ('identity_ok=1', 'identity_ok=0'),
                              ('unknown_references=0', 'unknown_references=1'),
                              ('reference_flags=00000000', 'reference_flags=00000020')):
            bad = raw.replace(before, after)
            with self.subTest(before=before):
                with self.assertRaises(ValueError): self.decode(bad)
                with self.assertRaises(Exception): self.module.decode(bad, HANDLE, 15, 6)
        with self.assertRaises(ValueError): self.decode(raw[:-1])
        with self.assertRaises(Exception): self.module.decode(raw[:-1], HANDLE, 15, 6)

    def test_pose_subset_must_exactly_match_normalized_raw_motion(self):
        raw = fixture();pose = self.decode(raw)['raw_pose']
        bad = raw.replace('raw_pose='+pose, 'raw_pose=01'+pose[2:])
        with self.assertRaises(ValueError): self.decode(bad)
        with self.assertRaises(Exception): self.module.decode(bad, HANDLE, 15, 6)

    def test_preparation_can_change_velocity_and_timing_but_not_raw_frame(self):
        raw = fixture();before = self.decode(raw)
        motion = bytearray.fromhex(before['motion']);motion[4+48:4+52] = struct.pack('<f', 99)
        changed = raw.replace('motion='+before['motion'], 'motion='+motion.hex()).replace('timing='+before['timing'], 'timing=01'+before['timing'][2:])
        after = self.decode(changed)
        same_frame(before, after, PRIVATE, PRIVATE, native_id=str(RUNTIME))
        self.module.same_frame(self.module.decode(raw, HANDLE, 15, 6), self.module.decode(changed, HANDLE, 15, 6), PRIVATE, PRIVATE)

    def test_unresolved_frames_remain_unknown_and_private_ids_cannot_change(self):
        raw = fixture().replace('unknown_references=0', 'unknown_references=1').replace('reference_flags=00000000', 'reference_flags=00000001')
        data = self.decode(raw)
        private = PRIVATE.replace('refs='+'0'*16, 'refs=0000000000000001')
        self.assertEqual(local_detail(private, native_id=str(RUNTIME), canonical=data)[0], 1)
        self.assertEqual(data['unknown_references'], 1)
        changed = private.replace('refs=0000000000000001', 'refs=0000000000000002')
        with self.assertRaises(ValueError): same_frame(data, data, private, changed, native_id=str(RUNTIME))
        with self.assertRaises(Exception): self.module.same_frame(self.module.decode(raw, HANDLE, 15, 6), self.module.decode(raw, HANDLE, 15, 6), private, changed)
        with self.assertRaises(ValueError): local_detail(PRIVATE, native_id=str(RUNTIME-1), canonical=data)

    def chunks(self):
        lines = []
        self.module.emit(lambda value: lines.append('NATIVE_GATE '+value), 'RAW', 6, 'before', fixture())
        return lines

    def test_bounded_chunk_round_trip_and_private_separation(self):
        lines = self.chunks()
        self.assertLess(max(map(len, lines)), 900)
        self.assertEqual(reassemble('\n'.join(lines), 'RAW'), {(6, 'before'): fixture()})
        self.assertNotIn('fffffffffffffffe', '\n'.join(lines))

    def test_chunk_gaps_reordering_duplication_truncation_and_false_lengths_fail(self):
        lines = self.chunks()
        faults = [lines[:-1], lines[1:], list(reversed(lines)), lines+[lines[-1]],
                  [lines[0], lines[0]]+lines[1:], [lines[0][:-1]]+lines[1:],
                  lines[:-1]+[lines[-1].replace('bytes=', 'bytes=9')],
                  [lines[0].replace('phase=before', 'phase=sample')]+lines[1:]]
        for bad in faults:
            with self.subTest(bad=bad[-1][:50]):
                with self.assertRaises(ValueError): reassemble('\n'.join(bad), 'RAW')

    def test_chunk_writer_rejects_unbounded_or_injected_payloads(self):
        for data in ('', 'x'*8193, 'x\nforged'):
            with self.assertRaises(Exception): self.module.emit(lambda _: None, 'RAW', 6, 'before', data)


if __name__ == '__main__':
    unittest.main()
