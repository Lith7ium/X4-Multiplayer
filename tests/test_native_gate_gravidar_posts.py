"""Gravidar has_scanned contact changes: probe arming/refusals/row drain (LuaJIT, mock
gate) and the census join (census_gravidar_posts.py). Native recording is covered by
src/native/engine/test_lockstep_gravidar_posts.h; no X4 is involved here."""
import importlib.util
from pathlib import Path
import sys
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_gravidar_watch as watch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('census_gravidar_posts', ROOT / 'tools/dev/census_gravidar_posts.py')
census = importlib.util.module_from_spec(spec)
spec.loader.exec_module(census)

HARNESS = watch.HARNESS.replace("native_gate_gravidar_watch={'000000000009fb3b','000000000009f121'}}",
                                'native_gate_gravidar_posts=true}')
MOCKS = '''
  armed=0;post_rows={}
  gate.gravidar_posts=function(...) assert(select('#',...)==0);armed=armed+1;return posts_native or 1 end
  gate.gravidar_posts_status=function() return 'armed=1 active=1 posts=3 changes=1 timers=2 rows=1 dropped=0 digest=00000000000000aa' end
  gate.gravidar_post_row=function(i) return post_rows[i] end
'''


def lua(config=''):
    runtime = LuaRuntime(unpack_returned_tuples=True)
    runtime.execute(HARNESS)
    runtime.execute(MOCKS)
    runtime.execute(config)
    return runtime


class ProbeTests(unittest.TestCase):
    def test_posts_arm_after_unit_keying_and_drain_rows_every_admitted_tick(self):
        self.assertIn('native_gate_gravidar_posts=true', HARNESS)
        runtime = lua("post_rows[0]='index=0 tick=1 unit=s:0000000000023a8c sizes=2/3 added=1 removed=0 plus=s:000000000006dd60 minus='")
        runtime.execute(watch.PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().armed, 1)
        runtime.execute('state.boot_phase=3;is_menu=false')
        runtime.execute(watch.PROBE)
        runtime.execute('update();state.issued=1;update();update()')
        logs = list(runtime.globals().logs.values())
        self.assertTrue(any(line.endswith('GRAVIDAR_POSTS_ARMED') for line in logs))
        self.assertTrue(any('GRAVIDAR_POSTS tick=1 armed=1 active=1 posts=3' in line for line in logs))
        self.assertEqual(sum('GRAVIDAR_POST_ROW index=0 tick=1 unit=s:0000000000023a8c sizes=2/3 added=1' in line for line in logs), 1)
        self.assertFalse(any('ERROR' in line for line in logs))

    def test_refusals(self):
        for config in ('X4LAN_CONFIG.native_gate_gravidar_posts=\'all\'', 'posts_native=0'):
            with self.subTest(config=config):
                runtime = lua(config)
                runtime.execute(watch.PROBE)
                runtime.execute('update();update();update()')
                self.assertEqual(runtime.globals().loads, 0)
                self.assertTrue(any('Gravidar posts refused' in line for line in runtime.globals().logs.values()), config)
        runtime = lua('X4LAN_CONFIG.native_gate_gravidar_key_all=nil')
        runtime.execute(watch.PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().loads, 0)


ROW = '[X4LAN] NATIVE_GATE GRAVIDAR_POST_ROW index={i} tick={t} unit={u} sizes={s} added={a} removed={r} plus={p} minus={m}'


class CensusTests(unittest.TestCase):
    def row(self, i, t, u, plus='', minus='', s='1/1'):
        return ROW.format(i=i, t=t, u=u, s=s, a=len([x for x in plus.split(',') if x]), r=len([x for x in minus.split(',') if x]),
                          p=plus, m=minus)

    def test_one_sided_and_differing_rows_are_named_in_tick_order(self):
        a = [self.row(0, 900, 's:00000000000aaaaa', plus='s:0000000000000001'),
             self.row(1, 1474, 's:0000000000023a8c', plus='s:000000000006dd60,u:9999/02b7d738'),
             self.row(2, 1600, 's:00000000000bbbbb', minus='c:8000000000000001')]
        b = [self.row(0, 900, 's:00000000000aaaaa', plus='s:0000000000000001'),
             self.row(1, 1556, 's:0000000000023a8c', plus='s:000000000006dd60,u:7777/02b7d738'),
             self.row(2, 1600, 's:00000000000bbbbb', minus='c:8000000000000002')]
        result = census.compare([census.parse(a), census.parse(b)])
        self.assertEqual(result['rows'], [3, 3])
        self.assertEqual(result['joined'], 2)
        self.assertEqual(result['first_tick'], 1474)
        first = result['examples'][0]
        self.assertEqual((first['tick'], first['unit'], first['present']), (1474, 's:0000000000023a8c', [True, False]))
        self.assertEqual(first['plus'][0], ['s:000000000006dd60', 'u/02b7d738'])
        self.assertEqual([e['tick'] for e in result['examples']], [1474, 1556, 1600])
        self.assertEqual(result['examples'][2]['present'], [True, True])

    def test_rows_with_scan_inputs_keep_them_per_contact_and_compare_by_key(self):
        head = ('[X4LAN] NATIVE_GATE GRAVIDAR_POST_ROW index=0 tick=1722 unit=s:0000000000027938 sizes=80/78 added=0 removed={r} '
                'time=4040000000000000 a={{p=1,2,3 r=471c4000 f=3f800000 s=42c80000 v=00000000 fl=8 z=3/9n}} plus= minus={m} edge={e}')
        near = 's:00000000000ada16{p=47000000,0,0 d=471cd000 b=471cd600 v=00000000 s=42480000 fl=0 z=3n}'
        gone = 's:00000000000add7a{p=47200000,0,0 d=471ce000 b=471cd600 v=00000000 s=42480000 fl=0 z=4n}'
        a = [head.format(r=2, m=near + ',' + gone, e='')]
        b = [head.format(r=1, m=gone, e=near)]
        result = census.compare([census.parse(a), census.parse(b)])
        self.assertEqual((result['joined'], result['differing'], result['first_tick']), (1, 1, 1722))
        example = result['examples'][0]
        self.assertEqual(example['minus'], [['s:00000000000ada16', 's:00000000000add7a'], ['s:00000000000add7a']])
        self.assertIn('d=471cd000 b=471cd600', example['inputs'][0]['s:00000000000ada16'])
        self.assertEqual(example['inputs'][0]['s:00000000000ada16'], example['inputs'][1]['s:00000000000ada16'])
        self.assertIn('r=471c4000', example['scanner'][0][0])
        self.assertEqual((example['late'], result['late_drops']), ([[], []], [0, 0])) # a near scanner is never late

    def test_late_drops_are_gathered_in_bound_in_a_later_near_group_of_a_wide_scanner(self):
        head = ('[X4LAN] NATIVE_GATE GRAVIDAR_POST_ROW index=0 tick=980 unit=s:0000000000029bfd sizes=51/{n} added=0 removed={r} '
                'time=40376a94d242e6b6 a={{p=c6597797,445d247b,4806a8ee r=4851f600 f=3f800000 s=458d3800 v=00000000 fl=8 z={z}/12w}} '
                'plus= minus={m} edge=')
        late = 's:000000000009ef8e{p=c6000000,0,48000000 d=47c35000 b=4851f800 v=00000000 s=42480000 fl=0 z=7n}'
        far = 's:000000000008f0f1{p=c6000000,0,49000000 d=48a00000 b=4851f800 v=00000000 s=42480000 fl=0 z=7n}'
        earlier = 's:000000000008f0dc{p=c6000000,0,48000000 d=47c35000 b=4851f800 v=00000000 s=42480000 fl=0 z=2n}'
        rows = [head.format(n=47, r=4, z=5, m=','.join((late, far, earlier, 's:000000000008f131{z=-}')))]
        result = census.compare([census.parse(rows), census.parse([])])
        self.assertEqual((result['late_drops'], result['examples'][0]['late']), ([1, 0], [['s:000000000009ef8e'], []]))
        rows = [head.format(n=50, r=1, z=7, m=late)] # the same group as the scanner is checked before its apply
        self.assertEqual(census.compare([census.parse(rows), census.parse([])])['late_drops'], [0, 0])

    def test_unmapped_contacts_compare_by_class_only(self):
        a = [self.row(0, 10, 's:0000000000000001', plus='u:1111/02b7d738')]
        b = [self.row(0, 10, 's:0000000000000001', plus='u:2222/02b7d738')]
        result = census.compare([census.parse(a), census.parse(b)])
        self.assertEqual((result['joined'], result['differing'], result['first_tick']), (1, 0, None))


if __name__ == '__main__':
    unittest.main()
