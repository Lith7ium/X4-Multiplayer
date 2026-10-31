"""Capability separation and saved-input checks; no native game evidence."""
import copy
import io
from pathlib import Path
import tempfile
import unittest

import native_portable as portable
import portable_import_capability_v1 as cap
import preflight_portable_rotation_v1 as preflight
from test_native_portable import contract, frame, participants, TOKEN
from native_portable_coordinator import PortableBarrier


ROOT = Path(__file__).resolve().parents[1]


def imported_contract():
    result = contract()
    result['snapshot_sha256'] = cap.SNAPSHOT_SHA256
    result['import_math'] = cap.capability(cap.SNAPSHOT_SHA256)
    return result


def reviewed_euler_contract():
    """Public regression vector for the already pinned twelve-ship fixture."""
    value = imported_contract()
    saved = (0x176a13, 0x176a74, 0x176ad5, 0x176b36, 0x176b97, 0x176bf8,
             0x176c59, 0x176c77, 0x176c95, 0x176cb3, 0x176cd1, 0x176cef)
    for actor, identity in zip(value['actors'], saved):
        actor['saved'] = f'{identity:016x}'
    value['stock_sha256'] = 'f589af25b4170053314d1271e18d6499b5c7a313ccad0bdaacb1aabdaea4a5ac'
    value['fixture_files'] = {
        'md/native_gate_battle.xml': 'b0bfdaef5de2c2856e7e98cf42621d3f9a2db07743f52d2b1de1b2515c7bd4cd',
        'md/x4lan.xml': '4ae0d4f86f3a77a1be85f1f5999aee13ab9a2f1121d5d7bcef468fdce522e3dc',
        'ui/native_portable_loader.lua': 'bb105a63558ad63b99404573c235a168e93147fbb40194959f1820a67e373547',
    }
    return value


def document(read='<rotation yaw="90"/>', write='<rotation yaw="-90"/>'):
    return ('<save><component id="[0xa]"><movement class="navigation"><position>'
            f'<read><offset>{read}</offset></read><write><offset>{write}</offset></write>'
            '</position></movement></component></save>')


class Capability(unittest.TestCase):
    def test_reviewed_contract_header_and_legacy_vector_agree(self):
        new = reviewed_euler_contract()
        self.assertEqual(portable.validate_contract(new), cap.CONTRACT_SHA256)
        old = copy.deepcopy(new); del old['import_math']
        self.assertEqual(portable.validate_contract(old), cap.LEGACY_CONTRACT_SHA256)
        self.assertIn('#define X4ROT_IMPORT_EULER_PER_BANK 10005u', cap.header(new))

    def test_old_contract_and_wire_are_unchanged(self):
        value = contract()
        expected = portable.sha(portable.canonical_json(value))
        self.assertEqual(portable.validate_contract(value), expected)
        self.assertNotIn('import_math', value)
        encoded = portable.encode_frame(frame(), value)
        self.assertEqual(portable.decode_frame(encoded, value), frame())

    def test_new_math_is_part_of_contract_and_frame_identity(self):
        old = contract()
        old['snapshot_sha256'] = cap.SNAPSHOT_SHA256
        new = imported_contract()
        old_sha, new_sha = map(portable.validate_contract, (old, new))
        self.assertNotEqual(old_sha, new_sha)
        a = frame(); a['contract_sha256'] = new_sha
        with self.assertRaisesRegex(ValueError, 'contract mismatch'):
            portable.encode_frame(a, old)
        b = copy.deepcopy(a)
        b['actors'][0]['active']['transform'][3] = '3f7fffff'
        result = portable.compare(a, b, new)
        self.assertFalse(result['equal'])
        self.assertEqual(result['differences'], ['.actors[0].active.transform[3]'])

    def test_legacy_peer_cannot_join_new_capability(self):
        new = imported_contract()
        barrier = PortableBarrier(TOKEN, new, participants(), lambda value: None)
        peer = participants()['1']
        old = copy.deepcopy(new); del old['import_math']
        packet = '|'.join(('NGPHELLO', '1', TOKEN, '1', peer['platform'],
                           peer['executable_sha256'], peer['agent_sha256'],
                           portable.validate_contract(old), portable.SCHEMA_SHA)).encode()
        barrier.receive(packet, ('127.0.0.1', 30101), 1.0)
        self.assertEqual(barrier.summary()['failure'], 'participant_capability_or_artifact_mismatch')

    def test_refuses_unknown_missing_or_changed_capability(self):
        mutations = [lambda v: v.update(snapshot_sha256='0' * 64),
                     lambda v: v['import_math'].update(mode=cap.LEGACY_MODE),
                     lambda v: v['import_math'].update(descriptor_sha256='0' * 64),
                     lambda v: v['import_math'].update(navigation_records=True),
                     lambda v: v['import_math']['banks']['read'].update(euler=10004),
                     lambda v: v['import_math']['banks']['write'].update(absent=0),
                     lambda v: v['import_math'].update(enabled=True),
                     lambda v: v['import_math'].pop('banks'),
                     lambda v: v.update(import_math=None)]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                value = imported_contract(); mutate(value)
                with self.assertRaises(ValueError):
                    portable.validate_contract(value)

    def test_arithmetic_source_and_header_constants(self):
        cap.verify_sources(ROOT / 'src/native/engine')
        sha = portable.validate_contract(imported_contract())
        header = cap.header(imported_contract())
        for value in (sha, cap.MODE, cap.DESCRIPTOR_SHA256, cap.SNAPSHOT_SHA256):
            self.assertIn(value, header)
        self.assertIn('X4ROT_IMPORT_EULER_PER_BANK 10005u', header)
        with self.assertRaises(ValueError):
            cap.header('bad"\n')
        with self.assertRaises(ValueError):
            cap.header(sha)
        with self.assertRaises(ValueError):
            cap.header(contract())
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in cap.SOURCE_SHA256:
                (directory / name).write_bytes((ROOT / 'src/native/engine' / name).read_bytes())
            cap.verify_sources(directory)
            path = directory / 'portable_rotation.c'
            path.write_bytes(path.read_bytes() + b'/* altered */\n')
            with self.assertRaisesRegex(ValueError, 'source changed'):
                cap.verify_sources(directory)


class SavedInput(unittest.TestCase):
    def scan(self, text):
        return preflight.scan(io.BytesIO(text.encode('utf-8')))

    def test_absence_empty_and_missing_attributes_remain_distinct(self):
        values = [self.scan(document(value, value)) for value in ('', '<rotation/>', '<rotation yaw=""/>')]
        self.assertEqual(values[0]['banks']['read'], {'euler': 0, 'absent': 1})
        for value in values[1:]:
            self.assertEqual(value['banks']['read'], {'euler': 1, 'absent': 0})
        self.assertEqual(len({value['inputs_sha256'] for value in values}), 3)

    def test_rejects_shapes_native_helper_cannot_read(self):
        for fragment in ('<quaternion/>', '<rotation/><rotation/>', '<unknown/>',
                         '<position/><position/>', '<rotation x="0"/>',
                         '<rotation><child/></rotation>'):
            with self.subTest(fragment=fragment), self.assertRaises(ValueError):
                self.scan(document(fragment))
        with self.assertRaises(ValueError):
            self.scan(document().replace('<offset>', '<offset extra="1">', 1))

    def test_rejects_unsupported_and_overflowing_decimal(self):
        for text in ('+90', ' 90', '90 ', '1e2', 'nan', '90;', '.', '--1', '9' * 256, '0' * 257):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.scan(document('<rotation yaw="' + text + '"/>'))
        for text in ('-0', '.5', '1.', '-.5', '0' * 256):
            with self.subTest(accepted=text):
                self.assertEqual(self.scan(document('<rotation yaw="' + text + '"/>'))['navigation_records'], 1)

    def test_duplicate_attributes_and_component_identities_refuse(self):
        with self.assertRaises(preflight.ET.ParseError):
            self.scan(document('<rotation yaw="1" yaw="2"/>'))
        xml = document()
        component = xml[len('<save>'):-len('</save>')]
        with self.assertRaisesRegex(ValueError, 'repeated or invalid'):
            self.scan('<save>' + component + component + '</save>')
        for changed in ('', '0', '[0x0]', '[0x10000000000000000]'):
            with self.subTest(identity=changed), self.assertRaises(ValueError):
                self.scan(xml.replace('[0xa]', changed))

    def test_missing_banks_and_offsets_refuse(self):
        for old, new in (('<read><offset>', '<read>'), ('</offset></read>', '</read>'),
                         ('<read>', '<other>'), ('</read>', '</other>')):
            with self.subTest(old=old), self.assertRaises((ValueError, preflight.ET.ParseError)):
                self.scan(document().replace(old, new))


if __name__ == '__main__':
    unittest.main()
