"""Census tools on N-client runs: client1 against every other client (synthetic logs only)."""
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import census_clients
import census_events
import census_jobs
import census_perf
import census_scripts
import census_threads
import census_world
import compare_native_world
from test_census_events import client as event_log
from test_native_gate_census_scripts import row as script_row, tick as script_tick
from test_native_gate_world_nav import log as world_log, row as world_row

CONFIG = "X4LAN_CONFIG.native_gate_world_hash_schema = 'WORLD_IMPORTED_2'\nX4LAN_CONFIG.native_gate_world_hash_stride = 1\n"


def make_run(root, logs, config=CONFIG):
    for slot, text in enumerate(logs, 1):
        client = Path(root)/f'client{slot}'
        (client/'installed').mkdir(parents=True)
        (client/'installed/x4_lan_lab-config.lua').write_text(config)
        (client/'engine-output.log').write_text(text)
    return Path(root)


def run_main(module, argv):
    out = io.StringIO()
    with mock.patch('sys.argv', ['tool', *map(str, argv)]), redirect_stdout(out):
        module.main()
    return json.loads(out.getvalue())


class ClientDiscoveryTests(unittest.TestCase):
    def test_contiguous_clients_are_required(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for name in ('client1', 'client3', 'clientx', 'client01'):
                (root/name).mkdir()
            (root/'client2.txt').write_text('')
            with self.assertRaises(ValueError): census_clients.client_slots(root)
            self.assertEqual(census_clients.client_slots(root, strict=False), [1, 3])
            (root/'client2').mkdir()
            self.assertEqual(census_clients.client_slots(root), [1, 2, 3])
        with tempfile.TemporaryDirectory() as d:
            (Path(d)/'client1').mkdir()
            with self.assertRaises(ValueError): census_clients.client_slots(d)


class WorldCensusTests(unittest.TestCase):
    def world_logs(self):
        changed = world_row(1)
        changed['hashes'] = (hashlib.sha256(b'changed topology').digest(), *changed['hashes'][1:])
        return [world_log(world_row()), world_log(world_row(1)), world_log(changed)]

    def test_compare_reports_each_client_against_client1(self):
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, self.world_logs())
            result = compare_native_world.compare_clients(run)
            self.assertEqual((result['clients'], result['reference']), (3, 'client1'))
            self.assertEqual(result['first_difference_by_client'], {'client2': None, 'client3': 1})
            self.assertEqual(result['pairs']['client3']['domain_object_counts']['topology'], 1)
            self.assertEqual(result['pairs']['client2']['errors'], [])
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, self.world_logs()[:2])  # two clients keep the original report
            self.assertEqual(compare_native_world.compare_clients(run), compare_native_world.compare(run))

    def test_world_census_scans_all_clients_once_and_keeps_two_client_format(self):
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, self.world_logs())
            (run/'native-barrier.json').write_text(json.dumps({'verdict': 'diverged', 'clients': 3, 'states': {'0': {}, '1': {}},
                                                               'digest_mismatch_ticks': [1], 'digest_mismatch_slots': {'1': [3]}}))
            result = run_main(census_world, [run])
            world = result['world']
            self.assertEqual((world['clients'], world['captures'], world['error_count']), (3, 1, 0))
            self.assertEqual(world['pairs']['client2']['diverged_objects'], 0)
            self.assertEqual(world['pairs']['client3']['diverged_objects'], 1)
            self.assertEqual(world['pairs']['client3']['roots'][0]['seed_domains'], {'topology': 1})
            self.assertEqual(sorted(result['side']), ['client2', 'client3'])
            self.assertEqual(result['barrier']['digest_mismatch_slots'], {'1': [3]})
            self.assertEqual(result['barrier']['clients'], 3)
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, self.world_logs()[::2])
            world = census_world.census(run, 64, [1, 2])
            self.assertNotIn('pairs', world)
            self.assertEqual((world['captures'], world['diverged_objects']), (1, 1))

    def test_world_census_reports_cadence_differences_across_all_clients(self):
        logs = self.world_logs()
        logs[2] = world_log(world_row(1), tick=2)
        with tempfile.TemporaryDirectory() as d:
            world = census_world.census(make_run(d, logs), 64, [1, 2, 3])
            self.assertIn('capture cadence differs: 1 versus 1 versus 2', world['errors'])


class StreamCensusTests(unittest.TestCase):
    def test_events_per_client_from_run_or_logs(self):
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, [''.join(event_log()), ''.join(event_log(reorder=True)), ''.join(event_log(extra=True))])
            result = run_main(census_events, [run])
            self.assertEqual((result['clients'], result['reference']), (3, 'client1'))
            self.assertEqual(result['pairs']['client2']['differing_groups'], 0)
            self.assertEqual(result['pairs']['client3']['differing_groups'], 1)
            self.assertEqual(result['pairs']['client3']['first_difference']['only_client2_count'], 1)
            logs = [run/f'client{k}/engine-output.log' for k in (1, 2, 3)]
            self.assertEqual(run_main(census_events, ['--logs', *logs]), result)
            two = run_main(census_events, ['--logs', *logs[:2]])
            self.assertEqual((two['differing_groups'], len(two['clients'])), (0, 2))
            with self.assertRaises(SystemExit), mock.patch('sys.stderr', io.StringIO()):
                run_main(census_events, ['--logs', logs[0]])

    def test_scripts_per_client(self):
        a = [script_row(0, 1, 'a'*16, 3, 'move.idle'), script_tick(2, 1, '1'*16, '2'*16)]
        b = [script_row(0, 1, 'a'*16, 4, 'move.idle'), script_tick(2, 1, '1'*16, '5'*16)]
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, ['\n'.join(a)+'\n', '\n'.join(a)+'\n', '\n'.join(b)+'\n'])
            result = run_main(census_scripts, [run])
            self.assertIsNone(result['pairs']['client2']['first_drawset_tick'])
            self.assertEqual(result['pairs']['client3']['first_drawset_tick'], 1)
            self.assertEqual(result['pairs']['client3']['scripts']['exec/move.idle']['draw_mismatch'], 1)

    def test_threads_and_jobs_per_client(self):
        def threads(word):
            return ''.join(f'NATIVE_GATE THREAD_RNG tick={t} threads=1 skipped=0 ui=7 main=8 rows=abc:7:{word*(t+1):016x}\n' for t in (1, 2))
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, [threads(1), threads(1), threads(2)])
            with mock.patch.object(census_threads, 'steps', lambda pairs, counter, limit: [1]*len(pairs)):
                result = census_threads.analyse(run, Path('unused'), 10)
            self.assertEqual(sorted(result['pairs']), ['client2', 'client3'])
            self.assertIsNone(result['pairs']['client2']['active']['abc#1']['first_word_difference'])
            self.assertEqual(result['pairs']['client3']['active']['abc#1']['first_word_difference'], 1)

        def jobs(exit):
            return (f'NATIVE_GATE JOB_RNG index=0 tick=1 key={"1"*16} cb=10 body=20 ord=0 cat=1 tid=5 seeded=1 '
                    f'seed={"2"*16} exit={exit:016x}\n')
        def count(rows, counter, limit):
            for r in rows.values(): r['draws'] = int(r['exit'], 16)
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, [jobs(3), jobs(3), jobs(4)])
            with mock.patch.object(census_jobs, 'count_draws', count):
                result = census_jobs.analyse(run, Path('unused'), 10)
            self.assertEqual(result['pairs']['client2']['bodies']['10/20']['draw_mismatch'], 0)
            self.assertEqual(result['pairs']['client3']['bodies']['10/20']['draw_mismatch'], 1)

    def test_perf_native_breakdown_covers_every_client(self):
        def timing(frame):
            return ''.join(f'[X4LAN] NATIVE_GATE FRAME_TIMING tick={t} frames={t} wait_us=1000 frame_us={frame} outside_us=500 probe_us=100\n'
                           for t in (3, 4, 5))
        with tempfile.TemporaryDirectory() as d:
            run = make_run(d, [timing(16000), timing(17000), timing(30000)], config='')
            result = census_perf.native(run)
            self.assertEqual(sorted(result), ['client1', 'client2', 'client3'])
            self.assertEqual(result['client3']['frametick_ms'], 30.0)


if __name__ == '__main__': unittest.main()
