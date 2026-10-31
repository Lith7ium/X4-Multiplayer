"""Bullet cadence census: per-tick BULLET_CADENCE counters, camera samples, BULLET and STATE rows."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import census_cadence as c


def cadence(tick, fixture='4/4/4/0', other='9/9/9/0', camera='0:ae79c:0:1:0:ae79c/2:0/-1', mode='observe', first=0):
    return (f'[X4LAN] NATIVE_GATE BULLET_CADENCE tick={tick} mode={mode} armed=1 installed=1 site=3375 frame={tick} '
            f'fixture={fixture} other={other} total_fixture=1/1/1/0 total_other=1/1/1/0 first_later={first} '
            f'unreadable=0 camera={camera} restored=0 stage=armed failures=0')


def client(switch=3, camera_tick=None, bullet='rng_after=01', shield='100'):
    out = ['unrelated']
    for tick in range(1, 6):
        later = tick >= switch
        out.append(cadence(tick, fixture='4/4/0/4' if later else '4/4/4/0', other='9/9/0/9' if later else '9/9/9/0',
                           camera='0:1234:0:1:0:1234/1:0/-1' if camera_tick and tick >= camera_tick else '0:ae79c:0:1:0:ae79c/2:0/-1',
                           first=switch if later else 0))
        out.append(f'[X4LAN] NATIVE_GATE BULLET id=7 slot=1 tick={tick} spawn=1 call=1 created=0 '+(bullet if tick == 4 else 'rng_after=00'))
        out.append(f'[X4LAN] NATIVE_GATE STATE_ROW tick={tick} id=1 data=1/0/100/'+(shield if tick == 5 else '100'))
    out.append('[X4LAN] NATIVE_GATE BULLET_CADENCE_STOP mode=observe armed=0')
    return out


def nav(tick, fixture='3/1/0/0/1/1/0/0/1', other='9/2/0/0/2/2/0/0/2', mode='observe'):
    return (f'[X4LAN] NATIVE_GATE NAV_CADENCE tick={tick} mode={mode} armed=1 installed=1 sites=0573:1473:4272:1773 frame={tick} '
            f'fixture={fixture} other={other} total_fixture=1/1/0/0/1/1/0/0/1 total_other=1/1/0/0/1/1/0/0/1 '
            f'first_now=0 first_mid=0 unreadable=0 restored=0 stage=armed failures=0')


def nav_client(near_tick=None):
    """Five ticks; from near_tick on, one fixture probe refresh is a camera-near (now) choice."""
    return ['unrelated']+[nav(t, fixture='3/1/0/0/1/1/1/0/0' if near_tick and t >= near_tick else '3/1/0/0/1/1/0/0/1') for t in range(1, 6)]


def view(tick, levels='0/0/0/0/0/3/2/1/4/0/0', calls=10, differ=0, mode='players'):
    return (f'[X4LAN] NATIVE_GATE VIEW_POINTS tick={tick} mode={mode} armed=1 installed=1 frame={tick} source=player viewpoints=1 ids=ae79c '
            f'calls={calls} evaluations={calls} levels={levels} differ={differ} total_calls=1 total_evaluations=1 total_differ=0 '
            f'first_differ=0 refreshes=0 latches=1 unreadable=0 foreign=0 restored=0 stage=armed failures=0')


def nav_row(index, tick, ident, saved='0000000000000000', probe_on=1, attention=7):
    return (f'[X4LAN] NATIVE_GATE NAV_CADENCE_ROW index={index} tick={tick} id={ident} saved={saved} attention={attention} params_on=1 '
            f'params_due=4000000000000000 probe=1 probe_on={probe_on} probe_due=4000000000000000 body=1 zone_scene=1')


def mass(tick, skips=0, word='0000000000000001', draws=4, first_skip=0, wait=10, frame=None, camera='0:00000000000000aa'):
    return (f'[X4LAN] NATIVE_GATE MASS_TRAFFIC tick={tick} mode=sync armed=1 installed=1 frame={tick if frame is None else frame} '
            f'tries=12 skips={skips} total_tries=99 total_skips={skips} first_skip={first_skip} joins={tick} busy=1 unknown=0 '
            f'absent=0 wait_us={wait} max_wait_us={wait} total_wait_us={wait} word={word} draws={draws} total_draws={draws} '
            f'draws_unknown=0 seeds={tick} worker={tick}/{tick-1} camera={camera} restored=0 stage=armed failures=0')


def region(tick, word='0000000000000001', draws=2, busy=0):
    return (f'[X4LAN] NATIVE_GATE REGION_PASS tick={tick} mode=sync armed=1 installed=1 joins={tick} busy={busy} unknown=0 absent=0 '
            f'wait_us=5 max_wait_us=9 total_wait_us=9 word={word} draws={draws} total_draws={draws} draws_unknown=0 seeds={tick} '
            f'worker={tick}/{tick-1} camera=- restored=0 stage=armed failures=0')


class RegionPassCensusTests(unittest.TestCase):
    def test_words_and_draws_compare_per_tick(self):
        a = c.parse([region(1), region(2), region(3, draws=5)]);b = c.parse([region(1), region(2, busy=1), region(3, word='0000000000000002')])
        r = c.compare(a, b)
        self.assertEqual(r['first_region_word']['tick'], 3);self.assertEqual(r['first_region_draws']['tick'], 3)
        self.assertEqual(r['clients'][1]['region_busy'], 1);self.assertEqual(r['clients'][0]['region_draws'], 9)
        self.assertEqual(r['clients'][0]['region_modes'], ['sync'])


def view_row(index, tick, key, vtable='02b66080', level=6, trigger='007cddc0'):
    return (f'[X4LAN] NATIVE_GATE VIEW_POINTS_ROW index={index} tick={tick} key={key} klass=0 vtable={vtable} level={level} camera=-1 '
            f'trigger={trigger}')


class ViewRowCensusTests(unittest.TestCase):
    def test_rows_compare_per_object_and_group_by_vtable(self):
        k1, k2, k3 = '0000000000000072', '0000000000000073', '0000000000000074'
        a = c.parse([view_row(0, 2, k1), view_row(1, 2, k2, '02b7feb8', 5), view_row(2, 3, k3), view_row(3, 4, '0000000000000000')])
        b = c.parse([view_row(0, 2, k1, level=7), view_row(1, 2, k2, '02b7feb8', 5), view_row(2, 5, k3)])
        r = c.compare(a, b)
        self.assertEqual([d['controller'] for d in r['first_view_rows']], [k1+'/02b66080', k3+'/02b66080'])
        self.assertEqual(r['view_row_vtables'], {'02b66080': dict(objects=2, first_tick=2, triggers={'007cddc0': 4})})


def gdig(tick, factors='00000000000000f1', times='00000000000000a1', active='00000000000000c1'):
    return (f'[X4LAN] NATIVE_GATE GRAVIDAR_DIGEST tick={tick} tick={tick} units=10 unmapped=2 factors={factors} times={times} '
            f'classes=0000000000000011 active=3/4 active_digest={active}/00000000000000d1 active_unmapped=0/0 stage=ok')


class GravidarDigestCensusTests(unittest.TestCase):
    def test_each_digest_compares_per_tick(self):
        a = c.parse([gdig(1), gdig(2), gdig(3, factors='00000000000000f2'), gdig(4, active='00000000000000c2')])
        b = c.parse([gdig(1), gdig(2, times='00000000000000a2'), gdig(3), gdig(4)])
        r = c.compare(a, b)
        self.assertEqual(r['first_gravidar_times']['tick'], 2);self.assertEqual(r['first_gravidar_factors']['tick'], 3)
        self.assertEqual(r['first_gravidar_active']['tick'], 4);self.assertIsNone(r['first_gravidar_units'])
        self.assertIsNone(r['first_gravidar_classes']);self.assertIsNone(r['first_gravidar_sectors'])
        self.assertIsNone(r['first_gravidar_rows']);self.assertIsNone(r['clients'][0]['gravidar_deadlines'])

    def test_sector_digests_rows_and_deadline_passes_compare_per_tick(self):
        def sectors(tick, deadlines='00000000000000e1', attention='00000000000000e2'):
            return gdig(tick)+(f' passive=4 sectors=2 sector_unmapped=0 deadlines={deadlines} attention={attention} rows=3 row_ticks=8 '
                               'table=12 table_full=0 row_drop=0')

        def row(index, tick, kind, key, rest):
            return f'[X4LAN] NATIVE_GATE GRAVIDAR_DIGEST_ROW index={index} tick={tick} kind={kind} key={key} {rest}'

        def deadlines(tick, cur_tick, passes, dueset, rebased=2, units=0):
            return (f'[X4LAN] NATIVE_GATE GRAVIDAR_DEADLINES tick={tick} armed=1 installed=1 active=1 restored=0 calls=9 passed=3 threads=0 '
                    f'nested=0 fields=4 first=2 rebased={rebased} due=3 keyed=2 unmapped=0 native=1 unscoped=0 passes=3 low=2 high=1 sectors=2 '
                    f'cur_tick={cur_tick} cur_calls=2 cur_passes={passes} cur_keyed={passes} cur_dueset={dueset} last_tick=0 last_calls=0 '
                    f'last_passes=0 last_keyed=0 last_dueset=0000000000000000 admitted=1 units_active=7 units_rebased={units} failures=0 stage=admitted')
        unit = 'first=0 factor=3f800000 class=02c67610 scan=401c660f04c756b3 due=401e6c9d29bf56b3 event=401c660f04c756b3'
        a = c.parse([sectors(1), row(0, 1, 's', 's:00000000000007a5', 'first=1 attention=2 due=4029000000000000'), deadlines(1, 1, 2, 'a'*16),
                     sectors(2), row(1, 2, 'a', 's:0000000000029bfd', unit), deadlines(2, 1, 2, 'a'*16),
                     sectors(3, attention='00000000000000e3'), row(2, 3, 'p', 'c:8000000000000001', 'first=0 factor=3e800000 class=02c67660'),
                     row(3, 3, 'a', 's:0000000000000010', unit), deadlines(3, 3, 1, 'b'*16),
                     sectors(4, deadlines='00000000000000e4'), '[X4LAN] NATIVE_GATE GRAVIDAR_DIGEST_ROW index=0 evicted=1'])
        b = c.parse([sectors(1), row(0, 1, 's', 's:00000000000007a5', 'first=1 attention=2 due=4029000000000000'), deadlines(1, 1, 2, 'a'*16),
                     sectors(2), row(1, 2, 'a', 's:0000000000029bfd', unit), deadlines(2, 1, 2, 'a'*16),
                     sectors(3), row(2, 3, 'a', 's:0000000000000010', unit.replace('due=401e', 'due=401f')), deadlines(3, 3, 1, 'c'*16, units=1),
                     sectors(4)])
        r = c.compare(a, b)
        self.assertEqual(r['first_gravidar_sector_attention']['tick'], 3);self.assertEqual(r['first_gravidar_sectors']['tick'], 4)
        self.assertEqual(r['first_gravidar_sectors']['client1'], (2, 0, '00000000000000e4'))
        rows = r['first_gravidar_rows']
        self.assertEqual((rows['tick'], rows['differing']), (3, 2))
        self.assertEqual([d['key'] for d in rows['rows']], ['a/s:0000000000000010', 'p/c:8000000000000001'])
        self.assertIn('due=401e6c9d29bf56b3', rows['rows'][0]['client1']);self.assertIn('due=401f6c9d29bf56b3', rows['rows'][0]['client2'])
        self.assertIsNone(rows['rows'][1]['client2'])
        # Tick 2's tally is tick 1's (no field Move at 2): no passes, not compared as a difference.
        self.assertEqual(r['first_gravidar_deadline_passes'], dict(tick=3, client1=dict(passes=1, dueset='b'*16), client2=dict(passes=1, dueset='c'*16)))
        self.assertEqual(r['clients'][0]['gravidar_deadlines']['rebased'], 2);self.assertEqual(r['clients'][1]['gravidar_deadlines']['units_rebased'], 1)
        self.assertEqual(r['clients'][0]['gravidar_deadlines']['stage'], 'admitted');self.assertEqual(r['clients'][0]['gravidar_deadlines']['native'], 1)
        self.assertEqual((r['clients'][0]['gravidar_rows'], r['clients'][0]['gravidar_rows_evicted']), (4, 1))


def attall(tick, calls=3, inits=0, ids=40, unmapped=5):
    return (f'[X4LAN] NATIVE_GATE ATTENTION_ALL tick={tick} armed=1 installed=1 frame={tick} calls={calls} inits={inits} '
            f'total_calls=99 total_inits=40 ids={ids} unmapped={unmapped} first=1 stage=armed failures=0')


class AttentionAllCensusTests(unittest.TestCase):
    def test_calls_inits_and_ids_compare_per_tick(self):
        a = c.parse([attall(1, inits=40), attall(2), attall(3, calls=4), attall(4, ids=41)])
        b = c.parse([attall(1, inits=40), attall(2), attall(3), attall(4, unmapped=7)])
        r = c.compare(a, b)
        self.assertEqual(r['first_attention_all']['tick'], 3);self.assertEqual(r['first_attention_all']['client1'], (4, 0))
        self.assertEqual(r['first_attention_all_ids']['tick'], 4)
        self.assertEqual([s['attention_all_unmapped'] for s in r['clients']], [5, 7])
        self.assertEqual([s['attention_all_ticks'] for s in r['clients']], [4, 4])


def region_camera(tick, key='0000000000176a13', native=0):
    return (f'[X4LAN] NATIVE_GATE REGION_CAMERA tick={tick} mode=view installed=1 frame={tick} viewpoint=5ca73 key={key} '
            f'updates={tick} native_frames={native} restored=0 stage=armed failures=0')


class RegionCameraCensusTests(unittest.TestCase):
    def test_viewpoint_keys_compare_per_tick(self):
        a = c.parse([region_camera(1), region_camera(2), region_camera(3)])
        b = c.parse([region_camera(1), region_camera(2), region_camera(3, key='0000000000000000', native=1)])
        r = c.compare(a, b)
        self.assertEqual(r['first_region_camera'], dict(tick=3, client1='0000000000176a13', client2='0000000000000000'))
        self.assertEqual([s['region_camera_native_frames'] for s in r['clients']], [0, 1])
        self.assertEqual([s['region_camera_ticks'] for s in r['clients']], [3, 3])


def effect_row(index, tick, time='3fe0000000000000', deferred=0, key='0000000000176a13'):
    return (f'[X4LAN] NATIVE_GATE EFFECT_ROW index={index} tick={tick} key={key} klass=0 delay=3e800000 time={time} '
            f'now=3ff0000000000000 deferred={deferred} unreadable=0')


class EffectRowCensusTests(unittest.TestCase):
    def test_rows_compare_as_multisets_per_tick(self):
        a = c.parse([effect_row(0, 20), effect_row(1, 20, deferred=1), effect_row(2, 21)])
        b = c.parse([effect_row(0, 20, deferred=1), effect_row(1, 20), effect_row(2, 21, time='3fe0000000000001')])
        r = c.compare(a, b)
        self.assertEqual([d['tick'] for d in r['first_effect_rows']], [21])
        self.assertIn('time=3fe0000000000000', r['first_effect_rows'][0]['client1_only'][0])
        self.assertIn('time=3fe0000000000001', r['first_effect_rows'][0]['client2_only'][0])


def motion(tick, saved='00000000000a214c', slot0='00', target='11'):
    return (f'[X4LAN] NATIVE_GATE MOTION_WATCH index={tick} tick={tick} saved={saved} attention=5 ctrl=02c51158 model=02c4e9c8 '
            f'flags=15 buffer={tick % 2} slot0={slot0} slot1=00 cache=00 target={target}')


class MotionWatchCensusTests(unittest.TestCase):
    def test_first_differing_tick_and_fields_per_ship(self):
        a = c.parse([motion(1), motion(2), motion(3, slot0='01'), motion(4, slot0='02', target='12')])
        b = c.parse([motion(1), motion(2), motion(3), motion(4)])
        r = c.compare(a, b)
        self.assertEqual(r['first_motion_watch'], {'00000000000a214c': dict(tick=3, fields=['slot0'])})

    def test_order_and_target_events_compare_as_sequences_per_ship(self):
        def target(index, tick, transform, saved='00000000000a214c'):
            return (f'[X4LAN] NATIVE_GATE MOTION_TARGET index={index} tick={tick} saved={saved} model=02c4f610 flag=1 '
                    f'chain=00e8cf55,00e9005f,00ef0000,00ef1111 ref=0000000000123456/0/2/2b6e910 transform={transform}')
        def order(index, tick, point):
            return (f'[X4LAN] NATIVE_GATE MOTION_ORDER index={index} tick={tick} saved=00000000000a214c site=006e0c72 '
                    f'chain=010a1500,010a1600 ref=00000000000a2000/0/0/2b82440/0 point={point}')
        a = c.parse([order(0, 190, 'aa'), target(0, 193, '00'), target(1, 194, '98'), target(2, 3, '00', saved='0000000000078855')])
        b = c.parse([order(0, 190, 'aa'), target(0, 193, '00'), target(1, 194, 'a0')])
        r = c.compare(a, b)
        self.assertEqual(r['first_motion_orders'], {})
        self.assertEqual(r['first_motion_events'], {})
        ev = lambda tick: (f'[X4LAN] NATIVE_GATE MOTION_EVENT index=0 tick={tick} saved=0000000000078855 event=start_travel '
                           f'time=4030a6508dfea2de now=4030a6508dfea2de data=0 ui=1 chain=00e9195e,00000000,00000000,00000000')
        r2 = c.compare(c.parse([ev(574)]), c.parse([ev(598)]))
        self.assertEqual(r2['first_motion_events'], {'0000000000078855': dict(event=0, ticks=[574, 598], fields=['tick'])})
        self.assertEqual(r['first_motion_targets'], {'00000000000a214c': dict(event=1, ticks=[194, 194], fields=['transform']),
                                                     '0000000000078855': dict(event=0, ticks=[3, None],
                                                                              fields=['chain', 'flag', 'model', 'ref', 'tick', 'transform'])})


class MassTrafficCensusTests(unittest.TestCase):
    def test_skips_words_and_draws_compare_per_tick(self):
        a = c.parse([mass(1), mass(2), mass(3, word='00000000000000aa'), mass(4, skips=1, first_skip=4)])
        b = c.parse([mass(1), mass(2, wait=900), mass(3, word='00000000000000bb', draws=5), mass(4)])
        r = c.compare(a, b)
        self.assertEqual(r['first_mass_word']['tick'], 3)
        self.assertEqual(r['first_mass_draws']['tick'], 3)
        self.assertEqual(r['first_mass_skips'], dict(tick=4, client1=1, client2=0))
        self.assertEqual(r['clients'][0]['mass_first_skip'], 4);self.assertIsNone(r['clients'][1]['mass_first_skip'])
        self.assertEqual(r['clients'][0]['mass_skips'], 1);self.assertEqual(r['clients'][1]['mass_max_wait_us'], 900)
        self.assertEqual(r['clients'][0]['mass_modes'], ['sync'])
        self.assertIsNone(r['first_mass_camera'])
        b = c.parse([mass(1), mass(2, camera='0:00000000000000bb')])
        self.assertEqual(c.compare(c.parse([mass(1), mass(2)]), b)['first_mass_camera'],
                         dict(tick=2, client1='0:00000000000000aa', client2='0:00000000000000bb'))

    def test_frame_counters_of_another_tick_are_not_compared(self):
        a = c.parse([mass(5, skips=2, frame=4)]);b = c.parse([mass(5, frame=4)])
        r = c.compare(a, b)
        self.assertIsNone(r['first_mass_skips']);self.assertEqual(r['clients'][0]['mass_skips'], 0)


class NavRowCensusTests(unittest.TestCase):
    def test_rows_compare_per_controller_and_report_the_earliest(self):
        a = c.parse([nav_row(0, 5, 3), nav_row(1, 32, 3, probe_on=0), nav_row(2, 40, 0, '0000000000074d83')])
        b = c.parse([nav_row(0, 5, 3), nav_row(1, 33, 3, probe_on=0), nav_row(2, 38, 0, '0000000000074d83', attention=8)])
        r = c.compare(a, b)['first_nav_rows']
        self.assertEqual([d['controller'] for d in r], ['3/0000000000000000', '0/0000000000074d83'])
        self.assertEqual(r[0]['tick'], 32);self.assertEqual(r[0]['row'], 1);self.assertEqual(r[1]['tick'], 38)
        self.assertEqual(c.compare(a, a)['first_nav_rows'], [])
        r = c.compare(a, c.parse([nav_row(0, 5, 3)]))['first_nav_rows']
        self.assertEqual(r[0]['client2'], None);self.assertEqual(r[0]['tick'], 32)


class ViewCensusTests(unittest.TestCase):
    def test_view_counters_compare_per_tick(self):
        a = c.parse([view(t) for t in range(1, 5)])
        b = c.parse([view(t, calls=11 if t == 3 else 10) for t in range(1, 5)])
        r = c.compare(a, b)
        self.assertEqual(r['clients'][0]['view_ticks'], 4);self.assertEqual(r['clients'][0]['view_sources'], ['player'])
        self.assertEqual(r['first_view_counts']['tick'], 3);self.assertEqual(r['first_view_counts']['client2'][0], 11)
        self.assertIsNone(c.compare(a, c.parse([view(t) for t in range(1, 5)]))['first_view_counts'])
        o = c.parse([view(t, mode='observe', differ=2 if t >= 2 else 0) for t in range(1, 5)])
        s = c.compare(o, o)['clients'][0]
        self.assertEqual(s['first_view_differ'], 2);self.assertEqual(s['view_differ'], 6);self.assertEqual(s['view_modes'], ['observe'])


class CadenceCensusTests(unittest.TestCase):
    def test_nav_counters_report_camera_near_choices_and_the_first_difference(self):
        a = c.parse(nav_client());b = c.parse(nav_client(near_tick=4))
        r = c.compare(a, b)
        self.assertEqual(r['clients'][0]['nav_ticks'], 5);self.assertEqual(r['clients'][0]['nav_modes'], ['observe'])
        self.assertIsNone(r['clients'][0]['first_nav_near_fixture']);self.assertEqual(r['clients'][1]['first_nav_near_fixture'], 4)
        self.assertIsNone(r['clients'][1]['first_nav_near_other'])
        self.assertEqual(r['first_nav_fixture_counts']['tick'], 4);self.assertIsNone(r['first_nav_other_counts'])
        self.assertEqual(r['first_nav_fixture_counts']['client2'], [3, 1, 0, 0, 1, 1, 1, 0, 0])
        self.assertIsNone(c.compare(a, c.parse(nav_client()))['first_nav_fixture_counts'])

    def test_identical_clients_report_the_switch_and_no_difference(self):
        a = c.parse(client());b = c.parse(client())
        r = c.compare(a, b)
        self.assertEqual(r['clients'][0]['first_later_fixture'], 3);self.assertEqual(r['clients'][0]['ticks'], 5)
        self.assertEqual(r['clients'][0]['modes'], ['observe'])
        for key in ('first_fixture_counts', 'first_other_counts', 'first_camera', 'first_bullet_rows', 'first_state_rows'):
            self.assertIsNone(r[key], key)

    def test_a_later_switch_on_one_client_is_the_first_difference(self):
        r = c.compare(c.parse(client()), c.parse(client(switch=4, camera_tick=4, bullet='rng_after=02', shield='98')))
        self.assertEqual(r['clients'][1]['first_later_fixture'], 4)
        self.assertEqual(r['first_fixture_counts']['tick'], 3);self.assertEqual(r['first_other_counts']['tick'], 3)
        self.assertEqual(r['first_fixture_counts']['client1'], [4, 4, 0, 4])
        self.assertEqual(r['first_camera']['tick'], 4);self.assertEqual(r['first_bullet_rows']['tick'], 4)
        self.assertEqual(r['first_state_rows']['tick'], 5)

    def test_camera_samples_compare_without_process_local_ids(self):
        self.assertEqual(c.portable_camera('0:5ca55:0:1:0:5ca55/8:5ca55/8'), c.portable_camera('0:5ca67:0:1:0:5ca67/8:5ca67/8'))
        self.assertEqual(c.portable_camera('0:5ca55:0:1:0:5ca55/8:1234/2'), '0:anchor:0:1:0:anchor/8:other/2')
        self.assertEqual(c.portable_camera('0:0:1:0:1:0/-1:0/-1'), '0:none:1:0:1:none/-1:none/-1')
        self.assertEqual(c.portable_camera('none'), 'none')

    def test_run_directory_and_explicit_logs(self):
        with tempfile.TemporaryDirectory() as root:
            for slot, lines in ((1, client()), (2, client(switch=4))):
                path = Path(root)/f'client{slot}';path.mkdir();(path/'engine-output.log').write_text('\n'.join(lines)+'\n')
            for argv in (['census_cadence.py', root], ['census_cadence.py', '--logs', f'{root}/client1/engine-output.log', f'{root}/client2/engine-output.log']):
                out = io.StringIO()
                with patch.object(sys, 'argv', argv), redirect_stdout(out): c.main()
                self.assertEqual(json.loads(out.getvalue())['first_fixture_counts']['tick'], 3)


if __name__ == '__main__':
    unittest.main()
