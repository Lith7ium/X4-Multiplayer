import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_ui_lazy', ROOT/'tools/dev/census_ui_lazy.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(index, tick, key='0000000000042df2', state='40812576c8b43ec9', target='cargo', lua=0, chain='0041a910,00407a55,0,0,0,0'):
    return (f'[X4LAN] NATIVE_GATE UI_LAZY_ROW index={index} tick={tick} target={target} class=saved key={key} lua={lua} '
            f'blocked=0 a1=4080e8 a2=0 state={state} chain={chain} other=0\n')


def table(*rows):
    with tempfile.TemporaryDirectory() as folder:
        log = Path(folder)/'log.txt'
        log.write_text(''.join(rows))
        return module.read_rows(log)


class CompareTests(unittest.TestCase):
    def test_a_call_on_one_client_only(self):
        left = table(row(0, 15961), row(1, 15962))
        right = table(row(0, 15961), row(1, 15962), row(2, 15962, key='0000000000000abc'))
        only_left, only_right, state = module.compare(left, right)
        self.assertEqual(only_left, [])
        self.assertEqual([(t, c[2]) for t, c in only_right], [(15962, '0000000000000abc')])
        self.assertEqual(state, [])

    def test_the_same_call_from_another_state(self):
        only_left, only_right, state = module.compare(table(row(0, 16021, state='a')), table(row(0, 16021, state='b')))
        self.assertEqual((only_left, only_right), ([], []))
        self.assertEqual([(t, a, b) for t, _, a, b in state], [(16021, ['a'], ['b'])])

    def test_another_caller_is_another_call(self):
        only_left, only_right, _ = module.compare(table(row(0, 5)), table(row(0, 5, chain='0041a910,00999999,0,0,0,0')))
        self.assertEqual((len(only_left), len(only_right)), (1, 1))

    def test_a_row_drained_twice_counts_once(self):
        self.assertEqual(len(table(row(0, 5), row(0, 5))[5]), 1)


if __name__ == '__main__':
    unittest.main()
