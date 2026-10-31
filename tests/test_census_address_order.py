import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_address_order', ROOT/'tools/dev/census_address_order.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(index, when, kind, total, part, ids, partial=0):
    return f'[X4LAN] NATIVE_GATE ADDRESS_ORDER_ROW index={index} when={when} kind={kind} total={total} part={part} partial={partial} ids={",".join(ids)}'


class ParseTests(unittest.TestCase):
    def test_parts_join_in_order_and_a_short_list_is_partial(self):
        lines = [row(1, 'arm', 'macro', 3, 1, ['c']), row(0, 'arm', 'macro', 3, 0, ['a', 'b']), 'noise', row(2, 'tick', 'ware', 5, 0, ['x'])]
        parsed = module.parse(lines)
        self.assertEqual(parsed[('arm', 'macro')], {'ids': ['a', 'b', 'c'], 'total': 3, 'partial': False, 'at': {}})
        self.assertTrue(parsed[('tick', 'ware')]['partial'])

    def test_the_walk_breaking_off_is_partial(self):
        parsed = module.parse([row(0, 'arm', 'ware', 1, 0, ['a'], partial=1)])
        self.assertTrue(parsed[('arm', 'ware')]['partial'])

    def test_an_empty_sample(self):
        parsed = module.parse([row(0, 'arm', 'macro', 0, 0, [])])
        self.assertEqual(parsed[('arm', 'macro')], {'ids': [], 'total': 0, 'partial': False, 'at': {}})


class CompareTests(unittest.TestCase):
    def test_same_order(self):
        c = module.compare(['a', 'b', 'c'], ['a', 'b', 'c'])
        self.assertTrue(c['same_order'])
        self.assertEqual((c['common'], c['inverted_pairs'], c['adjacent_inverted'], c['sample']), (3, 0, 0, []))

    def test_one_swap_is_named(self):
        c = module.compare(['cargo', 'repair', 'fighter'], ['repair', 'cargo', 'fighter'])
        self.assertFalse(c['same_order'])
        self.assertEqual((c['inverted_pairs'], c['all_pairs']), (1, 3))
        self.assertEqual(c['sample'], [['cargo', 'repair']])

    def test_full_reversal_counts_every_pair(self):
        c = module.compare(list('abcd'), list('dcba'))
        self.assertEqual((c['inverted_pairs'], c['all_pairs'], c['adjacent_inverted']), (6, 6, 3))

    def test_only_shared_ids_are_ordered(self):
        c = module.compare(['a', 'x', 'b'], ['b', 'a', 'y', 'z'])
        self.assertEqual((c['a'], c['b'], c['common'], c['only_a'], c['only_b']), (3, 4, 2, 1, 2))
        self.assertEqual(c['inverted_pairs'], 1)

    def test_the_sample_is_bounded(self):
        c = module.compare([str(i) for i in range(40)], [str(i) for i in reversed(range(40))], sample=3)
        self.assertEqual(len(c['sample']), 3)


class RebuiltTests(unittest.TestCase):
    def test_a_moved_object_was_rebuilt(self):
        arm = module.parse(['ADDRESS_ORDER_ROW index=0 when=arm kind=macro total=2 part=0 partial=0 at=100 ids=a,b',
                            'ADDRESS_ORDER_ROW index=1 when=arm kind=macro total=1 part=1 partial=0 at=200 ids=c'])
        tick = module.parse(['ADDRESS_ORDER_ROW index=0 when=tick kind=macro total=2 part=0 partial=0 at=900 ids=a,x',
                             'ADDRESS_ORDER_ROW index=1 when=tick kind=macro total=1 part=1 partial=0 at=200 ids=c'])
        self.assertEqual(module.rebuilt(arm[('arm', 'macro')], tick[('tick', 'macro')]), {'compared': 2, 'same_address': 1, 'moved': 1})

    def test_old_rows_without_an_address_compare_nothing(self):
        arm = module.parse([row(0, 'arm', 'macro', 1, 0, ['a'])])
        tick = module.parse([row(0, 'tick', 'macro', 1, 0, ['a'])])
        self.assertEqual(module.rebuilt(arm[('arm', 'macro')], tick[('tick', 'macro')])['compared'], 0)


class CheckLineTests(unittest.TestCase):
    def sample(self, ids):
        return module.parse([row(0, 'tick', 'macro', len(ids), 0, ids)])

    def test_the_arena_outcome(self):
        lines, ok = module.check_lines({'client1': self.sample(['0a', '0b', '10']), 'client2': self.sample(['0a', '0b', '10'])})
        self.assertTrue(ok)
        self.assertEqual(lines, ['ADDRESS_ORDER_CHECK tick/macro pair=client1-client2 same_order=true hash_order=true common=3 only=0/0 inverted=0 of=3 partial=false'])

    def test_the_heap_outcome(self):
        lines, ok = module.check_lines({'client1': self.sample(['0b', '0a']), 'client2': self.sample(['0a', '0b'])})
        self.assertFalse(ok)
        self.assertIn('same_order=false hash_order=false', lines[0])
        self.assertIn('inverted=1 of=1', lines[0])

    def test_same_order_on_both_but_not_hash_order(self):
        lines, ok = module.check_lines({'client1': self.sample(['0b', '0a']), 'client2': self.sample(['0b', '0a'])})
        self.assertTrue(ok)
        self.assertIn('same_order=true hash_order=false', lines[0])

    def test_a_missing_sample_fails(self):
        lines, ok = module.check_lines({'client1': self.sample(['0a']), 'client2': {}})
        self.assertFalse(ok)
        self.assertIn('missing=1', lines[0])


class ReportTests(unittest.TestCase):
    def test_pairs_and_what_a_load_created(self):
        one = module.parse([row(0, 'arm', 'macro', 2, 0, ['a', 'b']), row(1, 'tick', 'macro', 3, 0, ['a', 'c', 'b'])])
        two = module.parse([row(0, 'arm', 'macro', 2, 0, ['b', 'a']), row(1, 'tick', 'macro', 3, 0, ['c', 'b', 'a'])])
        out = module.report({'client1': one, 'client2': two})
        self.assertEqual(out['clients']['client1']['created/macro'], 1)
        pair = out['pairs']['client1-client2']
        self.assertFalse(pair['arm/macro']['same_order'])
        self.assertEqual(pair['tick/macro']['inverted_pairs'], 2)


if __name__ == '__main__':
    unittest.main()
