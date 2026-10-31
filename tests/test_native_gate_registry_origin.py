import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_registry_origin_evidence as origin


class RegistryOriginTests(unittest.TestCase):
    def sample(self, kind=6):
        update = dict(runtime=999, order=12, thread=123, kind=kind)
        row = dict.fromkeys(origin.FIELDS, 0)
        row.update(schema=1, runtime=999, order=12, thread=123, script='none')
        if kind == 6:
            hashed = 14695981039346656037
            for byte in b'orders.base':
                hashed = ((hashed ^ byte) * 1099511628211) & (2**64-1)
            row.update(recorded=1, source=2, caller=0x1066dc6, verified=0x1066dc6, caller_source=1,
                       context=100, exec=200, program=300, end=2, mode=1, type=0x2b99a88,
                       entity=998, depth=2, ui=1, view=1, reads=0x3ff, complete=1,
                       script_hash=hashed, script='orders.base', rng_before=55, rng_after=66)
        return update, row

    def render(self, row):
        return ('NATIVE_GATE REGISTRY_UPDATE_ORIGIN ' + ' '.join(
            f'{key}={value:x}' if key in origin.HEX else f'{key}={value}' for key, value in row.items()) +
            '\nNATIVE_GATE REGISTRY_ORIGIN_STOP schema=1 count=1 enabled=1 closed=1 failures=0 open=0 restore_open=0 peak=1\n')

    def test_exact_lifetime_origin_and_rng_change_are_retained(self):
        update, row = self.sample()
        result = origin.evidence(self.render(row), [update])
        self.assertTrue(result['supported'])
        self.assertEqual(result['rows'][0], row)
        self.assertEqual(result['scope'], 'first_covered_ai_family_entry_only')

    def test_historical_capture_and_non_ai_first_entry(self):
        update, row = self.sample(2)
        self.assertFalse(origin.evidence('', [update])['supported'])
        with self.assertRaisesRegex(ValueError, 'required origin capture missing'):
            origin.evidence('', [update], required=True)
        self.assertTrue(origin.evidence(self.render(row), [update])['supported'])
        row['recorded'] = 1
        with self.assertRaisesRegex(ValueError, 'different first family'):
            origin.evidence(self.render(row), [update])

    def test_unknown_failed_or_replaced_first_attempt_refuses(self):
        update, good = self.sample()
        changes = {'runtime':1000, 'order':13, 'thread':124, 'recorded':0, 'source':0,
                   'complete':0, 'reads':0x2ff, 'error':2, 'caller_source':4, 'verified':0,
                   'caller':0x1066dc7, 'type':0x2b9bff8, 'depth':16, 'context':0,
                   'exec':0, 'script':'unsupported', 'script_hash':1, 'mode':256}
        for key, value in changes.items():
            with self.subTest(key=key), self.assertRaises(ValueError):
                origin.evidence(self.render(dict(good, **{key:value})), [update])

    def test_inflight_at_stop_remains_incomplete(self):
        update, row = self.sample()
        for before, after in [('closed=1','closed=0'), ('open=0','open=1'), ('restore_open=0','restore_open=1'),
                              ('failures=0','failures=1'), ('enabled=1','enabled=0'), ('peak=1','peak=257')]:
            with self.subTest(after=after), self.assertRaises(ValueError):
                origin.evidence(self.render(row).replace(before, after), [update])

    def test_global_caps_duplicates_and_join_count(self):
        update, row = self.sample();log = self.render(row)
        line, footer = log.strip().split('\n')
        for bad in [line+'\n', footer+'\n', log+footer+'\n', line+'\n'+log,
                    ('x'*1536)+log, (line+'\n')*257+footer+'\n', log.replace('count=1','count=2'),
                    log.replace('source=2','source=2 source=2'), log.replace('mode=1','mode=18446744073709551616')]:
            if bad.startswith('x'):
                bad=log.replace('script=orders.base','script='+'x'*1536)
            with self.assertRaises(ValueError):origin.evidence(bad, [update])

    def test_ai_and_vtable_source_classification(self):
        update, row = self.sample()
        ai=dict(row, source=1, caller=0x106e3fb, verified=0x106e3fb)
        self.assertTrue(origin.evidence(self.render(ai), [update])['supported'])
        for source in (3,4):
            event=dict(row, source=source, caller=0x1234, verified=0, caller_source=2)
            self.assertTrue(origin.evidence(self.render(event), [update])['supported'])
            event.update(caller=0, caller_source=3)
            self.assertTrue(origin.evidence(self.render(event), [update])['supported'])


if __name__ == '__main__':
    unittest.main()
