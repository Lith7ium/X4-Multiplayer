"""Cross-format native publication evidence must preserve byte order and failures."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/dev'))
from analyze_native_pilot_authority import analyze, word_bytes


def fixture():
    pose_velocity = bytes(range(96))
    output = bytes(reversed(range(96)))
    def words(data):
        return ''.join(f'{value:08x}' for value in struct.unpack(f'<{len(data)//4}I', data))
    flight = {'tick': 2, 'id': 1, 'calls': 1, 'behaviour': '2c4db70',
              'input': words(pose_velocity + bytes(0x1a0-96)),
              'after': words(output), 'model': '00'*0x3a0}
    solver = {'tick': 2, 'actor': 1, 'kind': 1, 'ordinal': 1, 'solve_tick': 0,
              'solve_ordinal': 0, 'owner': '0000000000000123', 'c0': '00'*504,
              'm0': (bytes(64)+pose_velocity).hex(), 'm1': (bytes(64)+output).hex()}
    return '\n'.join('NATIVE_GATE '+tag+' '+' '.join(f'{k}={v}' for k, v in row.items())
                     for tag, row in (('FLIGHT', flight), ('PLAYER_SOLVER_EVENT', solver)))


class PilotAuthorityEvidenceTests(unittest.TestCase):
    def test_native_word_and_byte_captures_compare_without_claiming_authority(self):
        self.assertEqual(word_bytes('0403020108070605', 8), bytes(range(1, 9)))
        report = analyze(fixture())
        self.assertTrue(report['observed_motion_provenance_matches'])
        self.assertFalse(report['pilot_ownership_proved'])
        self.assertFalse(report['ai_order_suppression_proved'])

    def test_publication_change_and_missing_or_repeated_original_are_failures(self):
        original = fixture()
        changed = original.replace('after=5c5d5e5f', 'after=5c5d5e00')
        self.assertNotEqual(changed, original)
        for text in (changed, original.replace('calls=1', 'calls=0'),
                     original.replace('calls=1', 'calls=2'), ''):
            with self.subTest(text=text[:70]):
                self.assertFalse(analyze(text)['observed_motion_provenance_matches'])


if __name__ == '__main__':
    unittest.main()
