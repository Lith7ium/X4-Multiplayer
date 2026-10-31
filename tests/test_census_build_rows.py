import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_build_rows', ROOT/'tools/dev/census_build_rows.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

FIND = 'tick=100 kind=find scope=Job_Helper.Process_Ships n=2 set=aa order=bb rng=cc ids=1,2'


class ReadTests(unittest.TestCase):
    def test_rows_come_back_in_index_order_without_the_index(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder)/'log.txt'
            log.write_text('[X4LAN] NATIVE_GATE BUILD_ROW index=1 tick=5 kind=add task=2 ship=3 rng=0\n'
                           'other line\n[=ERROR=] 1.0 [X4LAN] NATIVE_GATE BUILD_ROW index=0 '+FIND+'\n')
            self.assertEqual(module.read_rows(log), [FIND, 'tick=5 kind=add task=2 ship=3 rng=0'])


class ExplainTests(unittest.TestCase):
    def first(self, a, b):
        index = module.first_difference([FIND, a], [FIND, b])
        self.assertEqual(index, 1)
        return module.explain(a, b)

    def test_the_list_holds_other_ships(self):
        self.assertIn('different ships', self.first(FIND.replace('set=aa', 'set=ab'), FIND))
        self.assertIn('different ships', self.first(FIND.replace('n=2', 'n=3'), FIND))

    def test_the_same_ships_in_another_order(self):
        self.assertIn('another order', self.first(FIND.replace('order=bb', 'order=bc'), FIND))

    def test_the_shuffles_input(self):
        self.assertIn('RNG word', self.first(FIND.replace('rng=cc', 'rng=cd'), FIND))

    def test_another_ship_gets_the_task(self):
        self.assertIn('the choice after the find', self.first('tick=5 kind=add task=2 ship=3 rng=0', 'tick=5 kind=add task=2 ship=4 rng=0'))

    def test_another_task_starts(self):
        self.assertIn('start logic', self.first('tick=5 kind=start task=2 processor=9 ok=1', 'tick=5 kind=start task=3 processor=9 ok=1'))
        self.assertIn('one client only', self.first('tick=5 kind=start task=2 processor=9 ok=1', 'tick=5 kind=start task=2 processor=9 ok=0'))

    def test_steps_out_of_line(self):
        self.assertIn('different steps', self.first('tick=5 kind=add task=2 ship=3 rng=0', 'tick=5 kind=start task=2 processor=9 ok=1'))

    def test_a_prefix_is_no_difference(self):
        self.assertIsNone(module.first_difference([FIND], [FIND, FIND]))


if __name__ == '__main__':
    unittest.main()
