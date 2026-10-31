"""Event census diff: multiset comparison per pass/tick of synthetic EVENT_ROW logs."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import census_events as c

ONE = '3ff0000000000000'   # 1.0
HALF = '3fe0000000000000'  # 0.5


def row(index, pass_, slot, source, etype, due=ONE, tick=0, phase=2, stype='2a91750', now='4000000000000000'):
    return (f'[X4LAN] NATIVE_GATE EVENT_ROW index={index} pass={pass_} phase={phase} tick={tick} slot={slot} '
            f'source={source} stype={stype} etype={etype} due={due} now={now}\n')


def client(extra=False, runtime='000000e0e0000104', reorder=False):
    first = [row(0, 1, 0, 'saved:0000000000000030', '2b00010', HALF), row(1, 1, 1, 'saved:0000000000000020', '2b00020'),
             row(2, 1, 2, f'runtime:{runtime}', '2b00020')]
    if reorder: first = [first[1], first[0], first[2]]
    second = [row(3, 4, 0, 'saved:0000000000000050', '2b00030', tick=1, phase=3),
              row(4, 4, 1, 'saved:0000000000000040', '2b00030', tick=1, phase=3)]
    if extra: second.append(row(5, 4, 2, 'saved:0000000000000041', '2b00040', tick=1, phase=3))
    return ['unrelated line\n']+first+second


class EventCensusTests(unittest.TestCase):
    def test_first_differing_pass_reports_only_the_extra_row(self):
        left, ls = c.parse(client(reorder=True))
        right, rs = c.parse(client(extra=True, runtime='000000e0e00008fc'))
        self.assertEqual((ls['rows'], rs['rows'], ls['gaps'], rs['gaps']), (5, 6, 0, 0))
        result = c.compare(left, right)
        self.assertEqual((result['groups'], result['differing_groups']), ([2, 2], 1))
        first = result['first_difference']
        self.assertEqual((first['position'], first['client1']['label'], first['client2']['ticks']), (1, 4, [1]))
        self.assertEqual((first['only_client1'], first['only_client1_count'], first['only_client2_count']), ([], 0, 1))
        extra = first['only_client2'][0]
        self.assertEqual((extra['source'], extra['etype'], extra['slot'], extra['due_value']), ('saved:0000000000000041', '2b00040', 2, 1.0))

    def test_unmapped_sources_key_by_class_unless_runtime_is_requested(self):
        left, _ = c.parse(client())
        right, _ = c.parse(client(runtime='000000e0e00008fc'))
        self.assertIsNone(c.compare(left, right)['first_difference'])
        runtime = c.compare(left, right, runtime=True)['first_difference']
        self.assertEqual((runtime['position'], runtime['only_client1'][0]['source'], runtime['only_client2'][0]['source']),
                         (0, 'runtime:000000e0e0000104', 'runtime:000000e0e00008fc'))

    def test_created_sources_are_portable_keys(self):
        def created(identity):
            return client()+[row(6, 5, 0, f'created:{identity}', '2b00050', tick=2, phase=3)]
        left, ls = c.parse(created('800000f222222222'))
        same, _ = c.parse(created('800000f222222222'))
        other, _ = c.parse(created('800000f111111111'))
        self.assertEqual((ls['rows'], ls.get('malformed', 0), left[-1]['kind']), (6, 0, 'created'))
        self.assertIsNone(c.compare(left, same)['first_difference'])
        first = c.compare(left, other)['first_difference']
        self.assertEqual((first['only_client1'][0]['source'], first['only_client2'][0]['source']),
                         ('created:800000f222222222', 'created:800000f111111111'))

    def test_tick_grouping_evictions_duplicates_and_class_names(self):
        stream = client(extra=True)+[client()[1], '[X4LAN] NATIVE_GATE EVENT_ROW index=9 evicted=1\n',
                                     '[X4LAN] NATIVE_GATE EVENT_ROW index=10 broken\n']
        rows, stats = c.parse(stream)
        self.assertEqual((stats['duplicate'], stats['evicted'], stats['malformed'], stats['gaps']), (1, 1, 1, 3))
        right, _ = c.parse(client())
        result = c.compare(rows, right, by='tick', names={0x2b00040: 'U::ExampleEvent'})
        first = result['first_difference']
        self.assertEqual((result['groups'], first['client1']['label'], first['only_client1'][0]['etype_class']),
                         ([2, 2], 1, 'U::ExampleEvent'))

    def test_command_line_reads_a_run_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            run = Path(temp)
            for slot, text in ((1, client()), (2, client(extra=True))):
                (run/f'client{slot}').mkdir()
                (run/f'client{slot}'/'engine-output.log').write_text(''.join(text))
            classes = run/'classes.csv'
            classes.write_text('class,mangled,vtable_rvas\nU::ExampleEvent,x,0x2b00040;0x2b00100@+0x18\n')
            out = io.StringIO()
            with patch.object(sys, 'argv', ['census_events.py', str(run), '--classes', str(classes)]), redirect_stdout(out):
                c.main()
            result = json.loads(out.getvalue())
            self.assertEqual(result['first_difference']['only_client2'][0]['etype_class'], 'U::ExampleEvent')
            self.assertEqual([s['rows'] for s in result['clients']], [5, 6])


def rng_row(index, tick, key, draws, entry='0000000000001111', etype='2ac17b8', due='404c000000000000', klass='saved'):
    return (f'[X4LAN] NATIVE_GATE EVENT_RNG_ROW index={index} tick={tick} class={klass} key={key} stype=2b9bff8 etype={etype} '
            f'due={due} entry={entry} draws={draws} keyed=0\n')


def rng_status(tick, keyset, drawset):
    return f'[X4LAN] NATIVE_GATE EVENT_RNG tick={tick} armed=1 mode=observe frame={tick} calls=9 keyset={keyset} drawset={drawset} stage=armed\n'


class EventRngCensusTests(unittest.TestCase):
    def test_sums_rows_draws_and_entry_words(self):
        left = c.parse_rng([rng_status(1, 'aa', 'bb'), rng_row(0, 1, '0000000000043cc2', 3), rng_status(2, 'ab', 'bc'),
                            rng_row(1, 2, '00000000000a1df9', 2, entry='0000000000002222'), rng_row(2, 2, '0000000000075278', 1),
                            '[X4LAN] NATIVE_GATE EVENT_RNG_ROW index=3 evicted=1\n', 'unrelated\n'])
        right = c.parse_rng([rng_status(1, 'aa', 'bb'), rng_row(0, 1, '0000000000043cc2', 3), rng_status(2, 'ab', 'bd'),
                             rng_row(1, 2, '00000000000a1df9', 4, entry='0000000000003333')])
        self.assertEqual((left['stats']['rows'], left['stats']['evicted'], left['stats']['status']), (3, 1, 2))
        result = c.compare_rng(left, right)
        self.assertEqual((result['first_sums_difference'], result['only'], result['draw_mismatch'], result['entry_mismatch']), (2, [1, 0], 1, 1))
        self.assertEqual(result['first_draw_mismatch']['draws'], [2, 4])
        self.assertEqual(result['first_entry_mismatch']['entry'], ['0000000000002222', '0000000000003333'])
        self.assertEqual((result['only_client1'][0]['key'], result['trade_offer_rows_one_side']), ('0000000000075278', 1))
        same = c.compare_rng(left, left)
        self.assertEqual((same['first_sums_difference'], same['draw_mismatch'], same['only']), (None, 0, [0, 0]))

    def test_command_line_adds_event_rng_when_rows_exist(self):
        with tempfile.TemporaryDirectory() as temp:
            run = Path(temp)
            for slot, draws in ((1, 2), (2, 3)):
                (run/f'client{slot}').mkdir()
                (run/f'client{slot}'/'engine-output.log').write_text(''.join(client()+[rng_status(1, 'aa', 'bb'), rng_row(0, 1, '0000000000043cc2', draws)]))
            out = io.StringIO()
            with patch.object(sys, 'argv', ['census_events.py', str(run)]), redirect_stdout(out):
                c.main()
            result = json.loads(out.getvalue())
            self.assertEqual(result['event_rng']['first_draw_mismatch']['draws'], [2, 3])
            self.assertIsNone(result['first_difference'])


if __name__ == '__main__': unittest.main()
