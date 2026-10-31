"""Jolt physics census (census_physics.py) on synthetic PHYSICS_* and BODY_ORDER_ROW lines."""
import io
import json
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import census_physics as c

CREATED = '80000f0ff3922735'
DIGESTS = ('manager', 'ids', 'active', 'flags', 'shape', 'mass', 'pre', 'post', 'cids', 'cshape', 'cmass', 'cpre', 'cpost', 'cset', 'corder', 'ccset')


def step(index, tick, n=1, **changed):
    values = {name: f'{tick:08x}{n:08x}' for name in DIGESTS}
    values.update(changed)
    counts = 'result=0 det=1 bodies=90 keyed=85 created=80 local=5 missing=0 size=2048 num=1500'
    digests = ' '.join(f'{name}={values[name]}' for name in ('manager', 'ids', 'active', 'flags', 'shape', 'mass', 'pre', 'post', 'cids', 'cshape', 'cmass',
                                                             'cpre', 'cpost'))
    contacts = f"contacts={changed.get('contacts', 12)} ckeyed=10 ccreated=8 clocal={changed.get('clocal', 2)} cforeign=0 flips=3 sensors=0"
    return (f'[X4LAN] NATIVE_GATE PHYSICS_STEP index={index} tick={tick} step={n} scene=0 {counts} {digests} {contacts} '
            f'cset={values["cset"]} corder={values["corder"]} ccset={values["ccset"]}')


def body(index, tick, key=CREATED, body_id='01000003', pre='aa'*52, post='bb'*52):
    return (f'[X4LAN] NATIVE_GATE PHYSICS_BODY index={index} tick={tick} step=1 scene=0 key={key} klass=1 which=0 body={body_id} '
            f'active=00000003 motion=2 flags=04 layer=0001 pre={pre} post={post}')


def contact(index, tick, ord_, a='0:0000000000000030/0/01000001/2', depth='bc23d70a'):
    return (f'[X4LAN] NATIVE_GATE PHYSICS_CONTACT index={index} tick={tick} step=1 ord={ord_} added=1 a={a} b=1:{CREATED}/0/01000003/2 '
            f'sub=00000011/00000022 flip=0 base=40a0000040c0000040e00000 normal=000000003f80000000000000 depth={depth} count=2 '
            f'points=0123456789abcdef settings=000000000000000000000000000000000000000000000000000000 sensor=0')


def order(index, tick, pos, order_, key, kind='frame', mode='observe'):
    return (f'[X4LAN] NATIVE_GATE BODY_ORDER_ROW index={index} tick={tick} mode={mode} count=2 pos={pos} order={order_} kind={kind} '
            f'klass=1 key={key} drop=0 create=1')


class PhysicsCensusTests(unittest.TestCase):
    def test_identical_clients_have_no_verdict(self):
        lines = [step(0, 1), step(1, 1, 2), body(0, 1), contact(0, 1, 1)]
        r = c.compare([c.parse(lines), c.parse(lines)])
        self.assertIsNone(r['pairs'][0]['verdict'])
        self.assertTrue(all(v is None for v in r['pairs'][0]['first'].values()))
        self.assertIsNone(r['pairs'][0]['contacts']);self.assertIsNone(r['pairs'][0]['first_body_rows'])

    def test_allocation_is_named_before_inputs_with_the_bodies(self):
        a = c.parse([step(0, 1), step(1, 2), body(0, 2)])
        b = c.parse([step(0, 1), step(1, 2, cids='ff', ids='fe', pre='fd', post='fc'), body(0, 2, body_id='02000007', post='cc'*52)])
        p = c.compare([a, b])['pairs'][0]
        self.assertEqual(p['verdict'], dict(tick=2, step=1, stage='allocation', fields=['ids', 'cids']))
        self.assertEqual(p['bodies']['body']['count'], 1);self.assertEqual(p['bodies']['body']['examples'][0]['values'], ['01000003', '02000007'])
        self.assertEqual(p['bodies']['pre']['count'], 0);self.assertEqual(p['bodies']['post']['count'], 1)
        self.assertEqual(p['first']['pre']['tick'], 2)

    def test_local_bodies_and_active_order_are_reported_not_decisive(self):
        a = c.parse([step(0, 1), step(1, 2)])
        b = c.parse([step(0, 1, manager='01', active='02', clocal=5, contacts=15), step(1, 2, cpost='ff')])
        p = c.compare([a, b])['pairs'][0]
        self.assertEqual(p['verdict'], dict(tick=2, step=1, stage='outputs', fields=['cpost']))
        self.assertEqual(p['first']['manager']['tick'], 1);self.assertEqual(p['first']['active']['tick'], 1)
        self.assertEqual(p['first']['clocal']['values'], ['2', '5'])

    def test_contact_order_only_and_contact_rows(self):
        a = c.parse([step(0, 3), contact(0, 3, 1), contact(1, 3, 2, depth='bca3d70a')])
        b = c.parse([step(0, 3, corder='77'), contact(0, 3, 1, depth='bca3d70a'), contact(1, 3, 2)])
        p = c.compare([a, b])['pairs'][0]
        self.assertEqual(p['verdict']['stage'], 'contact_order')
        self.assertTrue(p['contacts']['same_multiset']);self.assertFalse(p['contacts']['same_order'])
        b = c.parse([step(0, 3, cset='76', ccset='75'), contact(0, 3, 1), contact(1, 3, 2, depth='bcf5c28f')])
        p = c.compare([a, b])['pairs'][0]
        self.assertEqual(p['verdict'], dict(tick=3, step=1, stage='contacts', fields=['cset', 'ccset']))
        self.assertFalse(p['contacts']['same_multiset'])
        self.assertIn('depth=bca3d70a', p['contacts']['client1_only'][0]);self.assertIn('depth=bcf5c28f', p['contacts']['other_only'][0])

    def test_missing_steps_evictions_status_and_foreign_callbacks(self):
        a = c.parse([step(0, 1), step(1, 2),
                     '[X4LAN] NATIVE_GATE PHYSICS_BODY index=4 evicted=1',
                     '[X4LAN] NATIVE_GATE PHYSICS_TRACE tick=2 installed=1 rows=40 frame=2 steps=2 native=0 foreign_steps=0 contacts=9 '
                     'foreign=0 stray=0 step_rows=2 body_rows=0 contact_rows=0 restored=0 stage=armed failures=0'])
        b = c.parse([step(0, 1).replace('cforeign=0', 'cforeign=3'),
                     '[X4LAN] NATIVE_GATE PHYSICS_TRACE_STOP installed=0 rows=40 frame=2 steps=1 native=0 foreign_steps=1 contacts=9 '
                     'foreign=3 stray=1 step_rows=1 body_rows=0 contact_rows=0 restored=1 stage=armed failures=0'])
        r = c.compare([a, b])
        self.assertEqual(r['pairs'][0]['verdict'], dict(tick=2, step=1, stage='step_missing', fields=[]))
        self.assertEqual(r['evicted'], [{'body': 1}, {}]);self.assertEqual(r['foreign_callbacks'], [0, 3])
        self.assertEqual(r['status'][1], dict(foreign='3', foreign_steps='1', stray='1', failures='0', stage='armed'))

    def test_body_order_native_versus_portable(self):
        a = c.parse([order(0, 15, 0, 0, 'aa'.rjust(16, '0')), order(1, 15, 1, 1, 'bb'.rjust(16, '0'))])
        b = c.parse([order(0, 15, 0, 1, 'bb'.rjust(16, '0')), order(1, 15, 1, 0, 'aa'.rjust(16, '0'))])
        p = c.compare([a, b])['pairs'][0]['body_order']
        self.assertEqual(p['native']['tick'], 15);self.assertTrue(p['native']['same_set']);self.assertEqual(p['native']['first_position'], 0)
        self.assertIsNone(p['portable'])

    def test_compound_child_order_is_named_before_inputs(self):
        new = lambda i, t, shape: (f'[X4LAN] NATIVE_GATE PHYSICS_BODY index={i} tick={t} step=1 scene=0 key=80000ff8a4da86ef klass=1 which=0 '
                                   f'body=00800089 active=00000003 motion=2 flags=14 layer=0001 children=9 shape={shape} mass={"0"*16} '
                                   f'pre={"aa"*52} post={"bb"*52}')
        a = c.parse([step(0, 16), new(0, 16, '1'*16)])
        b = c.parse([step(0, 16, cshape='ff', shape='fe', post='fd', cset='fc'), new(0, 16, '2'*16),
                     '[X4LAN] NATIVE_GATE SHAPE_ORDER_ROW index=0 tick=15 from=8 to=0 children=9',
                     '[X4LAN] NATIVE_GATE SHAPE_ORDER_ROW index=1 evicted=1',
                     '[X4LAN] NATIVE_GATE SHAPE_ORDER tick=15 mode=sync moves=1 total_moves=1 loads=3 resident=0 stage=armed failures=0',
                     '[X4LAN] NATIVE_GATE SHAPE_ORDER_STOP mode=sync moves=0 total_moves=1 loads=576 resident=0 invalid=2 boot_moves=4 '
                     'mass_forced=9 stage=armed failures=0'])
        r = c.compare([a, b])
        p = r['pairs'][0]
        self.assertEqual(p['verdict'], dict(tick=16, step=1, stage='shapes', fields=['shape', 'cshape']))
        self.assertEqual(p['bodies']['shape']['examples'][0]['values'], ['1'*16, '2'*16])
        self.assertEqual(p['bodies']['children']['count'], 0)
        self.assertEqual(r['shape_order_moves'], [{}, {'15': 1}]);self.assertEqual(r['evicted'][1], {'shape_order': 1})
        self.assertEqual(r['shape_order'], [{}, dict(mode='sync', total_moves='1', boot_moves='4', loads='576', resident='0', invalid='2',
                                                     mass_forced='9', stage='armed', failures='0')])
        self.assertEqual(c.parse([body(0, 2)])['bodies'][2][(1, 0, 1, CREATED, 0)]['shape'], None)  # rows without the shape fields
        b = c.parse([step(0, 16, cmass='ee', mass='ef'), new(0, 16, '1'*16)])
        self.assertEqual(c.compare([a, b])['pairs'][0]['verdict'], dict(tick=16, step=1, stage='shapes', fields=['mass', 'cmass']))

    def test_nav_octree_builds_are_compared_by_portable_key(self):
        row = lambda i, t, key, klass=1, flag=0: (f'[X4LAN] NATIVE_GATE NAV_OCTREE_ROW index={i} tick={t} key={key} klass={klass} flag={flag} '
                                                  f'joined=1 us=40')
        a = c.parse([row(0, 16, '80000f09105ebb8a'), row(1, 16, '80000f09105ebb8a', flag=1), row(2, 15, '0'*16, klass=2),
                     row(3, 17, '80000ff25cdfd254'), '[X4LAN] NATIVE_GATE NAV_OCTREE tick=16 mode=sync builds=3 joins=2 stage=armed failures=0'])
        b = c.parse([row(0, 16, '80000f09105ebb8a'), row(1, 18, '80000f09105ebb8a', flag=1), row(2, 19, '80000fa5d6f1a777'),
                     '[X4LAN] NATIVE_GATE NAV_OCTREE_ROW index=3 evicted=1',
                     '[X4LAN] NATIVE_GATE NAV_OCTREE_STOP mode=observe builds=3 joins=0 stage=armed failures=0'])
        r = c.compare([a, b])
        self.assertEqual(r['nav_octree'], [dict(mode='sync', builds='3', joins='2', stage='armed', failures='0', rows=3, local=1, snapshot_rows={}),
                                           dict(mode='observe', builds='3', joins='0', stage='armed', failures='0', rows=3, local=0, snapshot_rows={})])
        snap = c.parse([row(0, 16, '80000f09105ebb8a') + ' snapshot=1', row(1, 17, '80000ff25cdfd254') + ' snapshot=0',
                        '[X4LAN] NATIVE_GATE NAV_OCTREE_STOP mode=agreed snapshot=1 snap_captured=2 snap_used=1 snap_live=1 builder_view=0 main_view=0'])
        self.assertEqual(c.compare([snap, snap])['nav_octree'][0], dict(mode='agreed', snapshot='1', snap_captured='2', snap_used='1', snap_live='1',
                                                                       builder_view='0', main_view='0', rows=2, local=0,
                                                                       snapshot_rows={'1': 1, '0': 1}))
        o = r['pairs'][0]['nav_octree']
        self.assertEqual(o['count'], 1);self.assertEqual(o['first'], [dict(key='80000f09105ebb8a', flag=1, ticks=[16, 18])])
        self.assertEqual(o['only'], [[['80000ff25cdfd254', 0]], [['80000fa5d6f1a777', 0]]])
        self.assertEqual(r['evicted'][1], {'nav_octree': 1})
        look = lambda i, t, site, key, result, count=1: (f'[X4LAN] NATIVE_GATE NAV_OCTREE_LOOKUP index={i} tick={t} site={site} key={key} '
                                                         f'klass=0 flag=0 ask=0 result={result} count={count}')
        a = c.parse([look(0, 103, '53fcfa', '0' * 13 + '6e2', 'miss', 3), look(1, 104, '446a8b', '0' * 13 + '6e2', 'miss')])
        b = c.parse([look(0, 103, '53fcfa', '0' * 13 + '6e2', 'hidden', 1), look(1, 103, '53fcfa', '0' * 13 + '6e2', 'miss', 2),
                     look(2, 104, '446a8b', '0' * 13 + '6e2', 'miss')])
        d = c.compare([a, b])['pairs'][0]['nav_octree_lookups']
        self.assertEqual(d['tick'], 103);self.assertEqual(d['count'], 2)
        self.assertEqual([(x['result'], x['counts']) for x in d['rows']], [('hidden', [0, 1]), ('miss', [3, 2])])
        self.assertIsNone(c.compare([a, a])['pairs'][0]['nav_octree_lookups'])
        built = lambda content: (f'[X4LAN] NATIVE_GATE NAV_OCTREE_ROW index=0 tick=40 key={"0" * 13}6e2 klass=0 flag=1 joined=0 us=5 '
                                 f'content={content} cells=12 index=3 assigned=22')
        e = c.compare([c.parse([built('1' * 16)]), c.parse([built('2' * 16)])])['pairs'][0]
        self.assertEqual(e['nav_octree'], dict(count=0, first=[], only=[[], []]))
        self.assertEqual(e['nav_octree_content'], dict(compared=1, count=1, rows=[dict(key='0' * 13 + '6e2', flag=1, content=['1' * 16, '2' * 16],
                                                                                      cells=[12, 12])]))
        self.assertEqual(c.compare([c.parse([built('0' * 16)]), c.parse([built('2' * 16)])])['pairs'][0]['nav_octree_content']['compared'], 0)

    def test_nav_octree_content_with_the_input_snapshot_row_layout(self):
        # st3-F63sa ('agreed' + snapshot): snapshot= precedes content=; a no-op build (shared octree already made) has
        # snapshot=0 content=0 cells=0 and is not compared.
        def row(i, key, flag, content, cells, snap):
            return (f'[X4LAN] NATIVE_GATE NAV_OCTREE_ROW index={i} tick=29 key={key} klass=0 flag={flag} joined=0 us=318323 '
                    f'snapshot={snap} content={content} cells={cells} index={i} assigned=22')
        same = [row(0, '000000000002578f', 0, '5efa957a25341025', 102417, 1), row(1, '000000000002578f', 1, '0d1660763bc6de03', 102553, 1),
                row(2, '0000000000027818', 0, '5efa957a25341025', 102417, 1), row(3, '00000000000004d5', 0, '0' * 16, 0, 0)]
        a, b = c.parse(same), c.parse(same)
        self.assertEqual(c.compare([a, b])['pairs'][0]['nav_octree_content'], dict(compared=3, count=0, rows=[]))
        self.assertEqual(c.compare([a, b])['nav_octree'][0]['snapshot_rows'], {'1': 3, '0': 1})
        other = c.parse(same[:1] + [row(1, '000000000002578f', 1, '69e2d2975967f67f', 102689, 1)] + same[2:])
        d = c.compare([a, other])['pairs'][0]['nav_octree_content']
        self.assertEqual((d['compared'], d['count']), (3, 1))
        self.assertEqual(d['rows'], [dict(key='000000000002578f', flag=1, content=['0d1660763bc6de03', '69e2d2975967f67f'], cells=[102553, 102689])])
        # A digest on one client only (the other a no-op or no digest) is not compared.
        lone = c.parse(same[:3] + [row(3, '00000000000004d5', 0, '1' * 16, 5, 1)])
        self.assertEqual(c.compare([a, lone])['pairs'][0]['nav_octree_content']['compared'], 3)

    def test_cli_reads_client_logs_and_writes_json(self):
        with tempfile.TemporaryDirectory() as d:
            for n, extra in ((1, {}), (2, {'cpre': '99'})):
                (Path(d)/f'client{n}').mkdir()
                (Path(d)/f'client{n}'/'x4lan-debug.txt').write_text('\n'.join(['noise', step(0, 5, **extra)])+'\n')
            out = Path(d)/'physics.json'
            with redirect_stdout(io.StringIO()):
                self.assertEqual(c.main([d, '--out', str(out)]), 0)
            self.assertEqual(json.loads(out.read_text())['pairs'][0]['verdict']['stage'], 'inputs')
        with self.assertRaises(SystemExit), redirect_stderr(io.StringIO()):
            c.main(['--logs', 'only-one.txt'])


if __name__ == '__main__':
    unittest.main()
