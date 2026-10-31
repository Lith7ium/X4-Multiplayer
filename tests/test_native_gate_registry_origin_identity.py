import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_registry_origin_identity_evidence as identity


class OriginIdentityTests(unittest.TestCase):
    def sample(self, kind=6):
        update = dict(runtime=999, order=12, thread=123, kind=kind)
        row = dict.fromkeys(identity.FIELDS, 0)
        row.update(schema=1, runtime=999, order=12, thread=123)
        if kind == 6:
            row.update(recorded=1, object=10000, type=0x2b99a88, actual=999, object_state=5,
                       owner_getter=0x352b10, owner=20000, owner_state=5, controller=30000,
                       controller_type=0x2c51158, controller_owner=10000, controller_state=5,
                       slots=40000+0xad0, agent=40000, agent_owner=10000, agent_state=5,
                       reads=1023, rereads=1023, reobject=5, reowner=5, recontroller=5, reagent=5, stable=1)
        return update, row

    def render(self, row):
        return 'NATIVE_GATE REGISTRY_ORIGIN_IDENTITY '+' '.join(
            f'{key}={value:x}' if key in identity.HEX else f'{key}={value}' for key,value in row.items())+'\n'

    def check(self, row, update=None):
        update = update or self.sample()[0]
        result = identity.evidence(self.render(row), [update], required=True)
        self.assertTrue(result['supported'])
        self.assertFalse(result['lifetime_exclusion_proven'])
        self.assertEqual(result['rows'], [row])

    def test_complete_presence_is_not_enrollment_or_lifetime_exclusion(self):
        update, row = self.sample()
        self.check(row, update)
        self.assertLess(len(self.render(row)), 900)

    def test_absent_controller_retains_owner_and_no_agent_inference(self):
        _, row = self.sample()
        row.update(controller=0, controller_type=0, controller_owner=0, controller_state=1,
                   slots=0, agent=0, agent_owner=0, agent_state=0, reads=31, rereads=31, recontroller=1, reagent=0)
        self.check(row)
        row['agent_state'] = 5
        with self.assertRaisesRegex(ValueError, 'read branches'):
            self.check(row)

    def test_unsupported_controller_has_no_nav_layout_reads(self):
        _, row = self.sample()
        row.update(controller_type=0x12345, controller_owner=0, controller_state=3, slots=0,
                   agent=0, agent_owner=0, agent_state=0, reads=63, rereads=63, recontroller=3, reagent=0)
        self.check(row)
        row.update(reads=1023, rereads=1023)
        with self.assertRaisesRegex(ValueError, 'read branches'):
            self.check(row)

    def test_partial_relationship_and_unreadable_attempt_are_preserved(self):
        _, row = self.sample()
        partial = dict(row, agent_owner=0, agent_state=4, reagent=4)
        self.check(partial)
        unreadable = dict(row, agent_owner=0, agent_state=2, reagent=2, reads=511, rereads=511, stable=3)
        self.check(unreadable)
        with self.assertRaisesRegex(ValueError, 'unreadable reread'):
            self.check(dict(unreadable, stable=1))
        with self.assertRaisesRegex(ValueError, 'unread field'):
            self.check(dict(unreadable, agent_owner=10000))

    def test_changed_second_snapshot_never_replaces_first(self):
        _, row = self.sample()
        row.update(stable=2, rereads=3, reobject=4, reowner=0, recontroller=0, reagent=0)
        self.check(row)
        self.assertEqual(row['actual'], 999)
        with self.assertRaisesRegex(ValueError, 'false stable'):
            self.check(dict(row, stable=1))
        with self.assertRaisesRegex(ValueError, 'reread branches'):
            self.check(dict(row, reowner=5))

    def test_unknown_class_and_missing_object_are_explicit(self):
        update, row = self.sample(2)
        row.update(recorded=1, object=10000, type=0x12345, actual=999, object_state=3,
                   reads=3, rereads=3, reobject=3, stable=1)
        self.check(row)
        absent = dict(row, object=0, type=0, actual=0, object_state=1, reads=0, rereads=0, reobject=1)
        self.check(absent)
        unknown = dict(absent, object=1, object_state=2, reobject=2, stable=3)
        self.check(unknown)
        with self.assertRaisesRegex(ValueError, 'different first family'):
            self.check(row, update)

    def test_historical_and_non_ai_zero_rows(self):
        update, row = self.sample(2)
        self.assertFalse(identity.evidence('', [update])['supported'])
        with self.assertRaisesRegex(ValueError, 'required origin identity'):
            identity.evidence('', [update], required=True)
        self.check(row, update)

    def test_bounds_missing_duplicates_and_false_provenance(self):
        update, row = self.sample();log = self.render(row)
        for key,value in [('runtime',1000), ('order',13), ('thread',124), ('recorded',0), ('stable',0),
                          ('reads',1024), ('rereads',1024), ('object_state',6), ('reagent',6)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                identity.evidence(self.render(dict(row, **{key:value})), [update])
        for bad in [log+log, log*257, log.replace('stable=1','stable=1 stable=1'),
                    log.replace('stable=1','stable='+'1'*1024), log.replace('order=12','order=18446744073709551616')]:
            with self.assertRaises(ValueError):
                identity.evidence(bad, [update])
        with self.assertRaisesRegex(ValueError, 'lifetime/order/thread'):
            identity.evidence(log+log, [update,update])


if __name__ == '__main__':
    unittest.main()
