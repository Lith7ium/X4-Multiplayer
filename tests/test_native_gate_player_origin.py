from copy import deepcopy
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/dev'))
from native_player_origin import TICKET, EDGE, STATUS, FIXED, POINTERS, configured, configured_evidence, evidence
from test_native_gate_transition import fixture
from test_native_gate_transition_saved import text as transition_text, ENABLE
from validate_native_gate import validate


TOKEN = '1|0000000000011e12|00000000000ae79c|1|120'
CONFIG = '''native_gate_player_probe = "observe"
native_gate_world_trace_early = true
native_gate_world_queue_trace = true
native_gate_player_transition_observer = true
native_gate_player_transition_manifest = "2|00000000000ae79c/p/ship_test_macro/player"
native_gate_player_transition_last_tick = 120
native_gate_player_origin_token = "''' + TOKEN + '"\n'


def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def context(empty=False):
    before = fixture()[0]['s0']
    before.update(saved=0xae79c, owner='player', player=1, controller_type=0x2c39488, model=0, model_type=0)
    after = deepcopy(before)
    after.update(controller=0x60000, player=0, controller_type=0x2c51158, model=0x50000, model_type=0x2c4e9c8)
    rows = fixture()[:2]
    for row in rows:
        row.update(tick=11, arg=0, extra=1 if row['kind'] == 1 else 0, s0=deepcopy(before), s1=deepcopy(after))
    watch = dict(schema=2, index=1, type=0x2c39488, macro='ship_test_macro', owner='player', generation=0, raw_world_coverage=0, s0=before)
    return ([] if empty else rows), [watch], []


def receipts(empty=False):
    ticket = dict(schema=1, ticket=1, event=0x70000, due=bits(30.01), post_tick=10, delivery_tick=11,
                  post_clock=bits(30), delivery_clock=bits(31.5), after_clock=bits(31.5),
                  controller0=0x20000, controller1=0x60000, post_returned=1, delivery_state=2,
                  errors=0, post_thread=7, delivery_thread=7, post_rax=0x123456789abcdef0, delivery_rax=0xfedcba9876543210)
    edges = [dict(schema=1, edge=i, ticket=1, transition=i) for i in (1, 2)]
    stop = {key: 0 for key in STATUS}
    stop.update(schema=1, first=1, last=120, imports=1, bound=1, closing=1,
                calls=2, returns=2, peak=1, tickets=1, edges=2, post_calls=1, post_returns=1,
                delivery_calls=1, delivery_returns=1, core_peak=1, ticket_rows=1, edge_rows=2,
                player_saved=0x11e12, ship_saved=0xae79c, player_runtime=124, ship_runtime=123,
                player=0x80000, ship=0x10000, agent=0x30000, owner=0x40000, raw_pointers=1, capture_complete=1)
    if empty:
        for key in ('calls', 'returns', 'peak', 'tickets', 'edges', 'post_calls', 'post_returns', 'delivery_calls', 'delivery_returns', 'core_peak', 'ticket_rows', 'edge_rows', 'capture_complete'):
            stop[key] = 0
    return ([] if empty else [ticket]), ([] if empty else edges), stop


def render(tickets, edges, stop):
    def value(key, item):
        return f'{item:016x}' if key in FIXED else f'{item:x}' if key in POINTERS else str(item)
    rows = [('TICKET', row) for row in tickets] + [('EDGE', row) for row in edges] + [('STOP', stop)]
    return '\n'.join('NATIVE_GATE PLAYER_ORIGIN_' + kind + ' ' + ' '.join(f'{key}={value(key, row[key])}' for key in sorted(row)) for kind, row in rows)


def capture(empty=False):
    return ENABLE + transition_text(*context(empty)) + '\n' + render(*receipts(empty))


def activation_capture(nested=False):
    rows, watches, model_tickets = context()
    activation = fixture()[2]
    activation.update(event=3, enter=4 if nested else 5, leave=5 if nested else 6,
                      tick=11, thread=7, parent=2 if nested else 0, arg=0, extra=8, active0=1, active1=0)
    for phase in ('s0', 's1'):
        activation[phase] = {key: '-' if key == 'owner' else 0 for key in activation[phase]}
    if nested: rows[1]['leave'] = 6
    rows.append(activation)
    tickets, edges, stop = receipts()
    # Links are emitted on return, so a nested activation precedes publication.
    edges = [dict(schema=1, edge=i, ticket=1, transition=e) for i, e in enumerate((1, 3, 2) if nested else (1, 2, 3), 1)]
    stop.update(edges=3, edge_rows=3)
    return rows, watches, model_tickets, tickets, edges, stop


def activation_text(parts):
    return transition_text(*parts[:3]) + '\n' + render(*parts[3:])


class PlayerOriginEvidenceTests(unittest.TestCase):
    def test_global_activation_is_not_selected_ship_identity_or_release(self):
        for nested in (False, True):
            result = evidence(activation_text(activation_capture(nested)))
            self.assertTrue(result['evidence_valid'], result['errors'])
            association = next(row for row in result['associated_transition_events'] if row['transition'] == 3)
            self.assertEqual(association['association'], 'producer_scoped_global_activation')
            self.assertEqual(association['publication_parent'], 2 if nested else None)
            self.assertFalse(association['selected_ship_activation_proven'])
            self.assertEqual(result['completed_releases'], [])
            self.assertFalse(result['atomic_lease'])

    def test_global_activation_requires_canonical_zero_states_and_publication_lineage(self):
        for phase in ('s0', 's1'):
            for key in fixture()[0]['s0']:
                parts = activation_capture(); parts[0][2][phase][key] = 'player' if key == 'owner' else 1
                self.assertFalse(evidence(activation_text(parts))['evidence_valid'], (phase, key))
        for key, value in [('arg', 2), ('extra', 7), ('queue', 1), ('old', 0x20000), ('new', 0x60000), ('result', 1)]:
            parts = activation_capture(); parts[0][2][key] = value
            self.assertFalse(evidence(activation_text(parts))['evidence_valid'], key)
        for key, value in [('kind', 1), ('thread', 8), ('tick', 12), ('leave', 4), ('old', 0x70000), ('new', 0x70000), ('errors', 1), ('queue_errors', 256)]:
            parts = activation_capture(True); parts[0][1][key] = value
            result = evidence(activation_text(parts))
            self.assertFalse(result['evidence_valid'], key)
            self.assertEqual(result['associated_transition_events'], [])
        parts = activation_capture(True); parts[0][2]['parent'] = 99
        self.assertFalse(evidence(activation_text(parts))['evidence_valid'])
        for key in ('saved', 'runtime', 'object', 'agent', 'faction'):
            parts = activation_capture(True); parts[0][1]['s1'][key] += 1
            self.assertFalse(evidence(activation_text(parts))['evidence_valid'], key)

    def test_local_delivery_and_edges_do_not_claim_release_or_authority(self):
        result = evidence(capture())
        self.assertTrue(result['evidence_valid'], result['errors'])
        self.assertTrue(result['origin_exercised'])
        self.assertEqual(result['associated_transition_events'], [dict(ticket=1, transition=1), dict(ticket=1, transition=2)])
        self.assertFalse(result['scope_containment_serialized'])
        self.assertFalse(result['after_capture_tick_serialized'])
        self.assertFalse(result['public_request_acceptance_observed'])
        self.assertEqual(result['completed_releases'], [])
        self.assertFalse(result['atomic_lease'])
        self.assertFalse(result['independent_player_context'])
        self.assertNotEqual(result['tickets'][1]['due'], result['tickets'][1]['delivery_clock'])

    def test_unexercised_capture_valid_without_fake_success(self):
        result = evidence(capture(True))
        self.assertTrue(result['evidence_valid'], result['errors'])
        self.assertFalse(result['origin_exercised'])
        self.assertEqual(result['associated_transition_events'], [])

    def test_configuration_exact_native_token_window_and_watch(self):
        errors = []
        result = configured_evidence(capture(), CONFIG, 1, errors)
        self.assertEqual(errors, [])
        self.assertEqual(result['configuration'], configured(CONFIG))
        for token in (TOKEN.replace('|1|', '|0|'), TOKEN.replace('|1|', '|01|'), TOKEN.replace('|120', '|121'), TOKEN.replace('11e12', '11E12'), TOKEN.replace('11e12', 'ae79c'), TOKEN + '|1'):
            with self.subTest(token=token), self.assertRaises(ValueError):
                configured(CONFIG.replace(TOKEN, token))
        for config in (CONFIG + '\nnative_gate_player_origin_token = "' + TOKEN + '"', CONFIG.replace('last_tick = 120', 'last_tick = 119'), CONFIG.replace('p/ship_test', 'n/ship_test'), CONFIG.replace('observer = true', 'observer = false'), CONFIG.replace('11e12', '11e13')):
            errors = []
            result = configured_evidence(capture(), config, 1, errors)
            self.assertFalse(result['evidence_valid'])
            self.assertTrue(errors)

    def test_record_syntax_bounds_and_duplicates(self):
        original = capture()
        for before, after in [('ticket=1', 'ticket=+1'), ('ticket=1', 'ticket=01'), ('event=70000', 'event=070000'), ('event=70000', 'event=10000000000000000'), ('post_thread=7', 'post_thread=4294967296'), ('post_tick=10', 'post_tick=18446744073709551616'), ('post_returned=1', 'post_returned=2'), ('due=403e028f5c28f5c3', 'due=403E028F5C28F5C3'), ('schema=1', 'schema=1 schema=1'), ('schema=1', 'schema=1 unexpected=0')]:
            changed = original.replace(before, after, 1)
            self.assertNotEqual(changed, original)
            self.assertFalse(evidence(changed)['evidence_valid'], (before, after))
        for key in ('calls', 'excluded', 'native_failures'):
            tickets, edges, stop = receipts(); stop[key] = 1 << 64
            self.assertFalse(evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))['evidence_valid'], key)

    def test_missing_reordered_duplicate_and_over_capacity_records(self):
        lines = capture().splitlines()
        start = next(i for i, line in enumerate(lines) if 'PLAYER_ORIGIN_TICKET' in line)
        for changed in (lines[:start] + lines[start+1:], lines[:-1], lines + [lines[-1]], lines + [lines[start]], lines[:start] + [lines[start+1], lines[start]] + lines[start+2:], lines[:start] + [lines[start]] * 162):
            self.assertFalse(evidence('\n'.join(changed))['evidence_valid'])
        for kind, cap in (('TICKET', 1024), ('EDGE', 192), ('STOP', 1536)):
            self.assertFalse(evidence(capture().replace('PLAYER_ORIGIN_' + kind + ' ', 'PLAYER_ORIGIN_' + kind + ' ' + ' ' * cap, 1))['evidence_valid'])

    def test_event_reuse_and_invalid_receipt_never_join(self):
        for field, value in [('event', 0), ('controller0', 0), ('delivery_state', 1), ('post_returned', 0), ('errors', 8), ('delivery_thread', 0), ('post_tick', 0), ('delivery_tick', 9), ('delivery_tick', 121), ('due', bits(float('nan'))), ('after_clock', bits(-1.0)), ('post_clock', bits(float('inf')))]:
            tickets, edges, stop = receipts(); tickets[0][field] = value
            result = evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))
            self.assertFalse(result['evidence_valid'], field)
            self.assertEqual(result['associated_transition_events'], [])
        tickets, edges, stop = receipts(); second = deepcopy(tickets[0]); second['ticket'] = 2; tickets.append(second)
        for field in ('tickets', 'post_calls', 'post_returns', 'delivery_calls', 'delivery_returns', 'ticket_rows'): stop[field] = 2
        stop['calls'] = stop['returns'] = 4
        self.assertFalse(evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))['evidence_valid'])

    def test_stop_retains_open_restore_failure_and_counter_disagreements(self):
        for field, value in [('open', 1), ('restore_open', 1), ('core_stop_open', 1), ('sites', 1), ('native_failures', 1), ('failures', 1), ('bound', 0), ('imports', 2), ('closing', 0), ('returns', 1), ('post_calls', 2), ('delivery_returns', 0), ('ticket_rows', 0), ('edges', 129), ('tickets', 33), ('raw_pointers', 0), ('action_authority', 1), ('atomic_lease', 1), ('capture_complete', 0), ('peak', 0), ('core_peak', 3), ('prebind', 3), ('due_is_current', 1)]:
            tickets, edges, stop = receipts(); stop[field] = value
            self.assertFalse(evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))['evidence_valid'], field)
        tickets, edges, stop = receipts(); stop['calls'] = stop['returns'] = 100
        self.assertFalse(evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))['evidence_valid'])

    def test_explicit_excluded_and_other_delivery_counts_remain_diagnostic(self):
        tickets, edges, stop = receipts()
        stop.update(calls=7, returns=7, delivery_calls=3, delivery_returns=3, other=1, unknown=1, prebind=2, excluded=1)
        result = evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))
        self.assertTrue(result['evidence_valid'], result['errors'])
        self.assertEqual(result['stop']['unknown'], 1)

    def test_runtime_object_agent_owner_replacement_refuses_join(self):
        for key, value in [('player_runtime', 123), ('ship_runtime', 125), ('player', 0x10000), ('ship', 0x90000), ('agent', 0x90000), ('owner', 0x90000), ('ship_saved', 1)]:
            tickets, edges, stop = receipts(); stop[key] = value
            result = evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))
            self.assertFalse(result['evidence_valid'], key)
            self.assertEqual(result['associated_transition_events'], [])

    def test_edge_thread_window_missing_duplicate_and_wrong_lifetime(self):
        for field, value in [('thread', 9), ('tick', 10), ('tick', 121), ('boot', 2), ('errors', 1), ('queue_errors', 256)]:
            rows, watches, tickets = context(); rows[0][field] = value
            self.assertFalse(evidence(transition_text(rows, watches, tickets) + '\n' + render(*receipts()))['evidence_valid'], field)
        for field, value in [('transition', 99), ('transition', 2), ('ticket', 2)]:
            tickets, edges, stop = receipts(); edges[0][field] = value
            self.assertFalse(evidence(transition_text(*context()) + '\n' + render(tickets, edges, stop))['evidence_valid'], field)
        rows, watches, tickets = context(); rows[0]['s1']['agent'] += 1
        self.assertFalse(evidence(transition_text(rows, watches, tickets) + '\n' + render(*receipts()))['evidence_valid'])

    def test_raw_transition_failure_stays_failure_despite_valid_origin(self):
        result = evidence(transition_text(*context(), failures=1) + '\n' + render(*receipts()))
        self.assertTrue(result['origin_receipts_valid'])
        self.assertFalse(result['evidence_valid'])
        self.assertTrue(result['transition_errors'])
        self.assertEqual(result['associated_transition_events'], [])

    def test_versioned_exception_receipts_require_all_counters_and_no_unwinds(self):
        tickets, edges, stop = receipts()
        for row in tickets + edges + [stop]: row['schema'] = 2
        stop.update(native_unwinds=0, post_unwinds=0, delivery_unwinds=0)
        transition = transition_text(*context(), exception_schema=1, direct_calls=2, direct_returns=2, direct_unwinds=0, queue_exception_cleanup=0)
        original = transition + '\n' + render(tickets, edges, stop)
        result = evidence(original)
        self.assertTrue(result['evidence_valid'], result['errors'])
        self.assertEqual(result['exception_cleanup_scope'], 'origin_only')
        self.assertFalse(result['queue_exception_cleanup'])
        self.assertEqual(evidence(capture())['exception_cleanup_scope'], 'ordinary_return_only')
        errors = []
        self.assertTrue(configured_evidence(original, CONFIG, 1, errors)['evidence_valid'], errors)
        for field in ('native_unwinds', 'post_unwinds', 'delivery_unwinds'):
            for value in (1, 1 << 64):
                changed = original.replace(field + '=0', field + '=' + str(value))
                self.assertFalse(evidence(changed)['evidence_valid'], (field, value))
            self.assertFalse(evidence(original.replace(field + '=0 ', '', 1))['evidence_valid'])
        tickets[0]['schema'] = 1
        self.assertFalse(evidence(transition + '\n' + render(tickets, edges, stop))['evidence_valid'])
        for field, value in [('exception_schema', 2), ('direct_calls', 3), ('direct_returns', 1), ('direct_unwinds', 1), ('queue_exception_cleanup', 1)]:
            value0 = dict(exception_schema=1, direct_calls=2, direct_returns=2, direct_unwinds=0, queue_exception_cleanup=0)[field]
            changed = original.replace(f'{field}={value0}', f'{field}={value}')
            self.assertFalse(evidence(changed)['evidence_valid'], field)

    def test_unknown_marker_and_huge_logging_prefix_refuse(self):
        self.assertFalse(evidence(capture() + '\nNATIVE_GATE PLAYER_ORIGIN_DRAIN_ERROR missing') ['evidence_valid'])
        self.assertFalse(evidence(capture().replace('NATIVE_GATE PLAYER_ORIGIN_TICKET', 'x' * 2048 + 'NATIVE_GATE PLAYER_ORIGIN_TICKET'))['evidence_valid'])

    def test_main_validator_requires_paired_config_and_keeps_session_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'result.json').write_text(json.dumps(dict(verdict='fail', exit_code=1)))
            states = {str(t): {str(slot): dict(game=20+t/60, real=20+t/60, digest='a'*64, command='00000000')
                      for slot in (1, 2)} for t in (0, 1)}
            (run / 'native-barrier.json').write_text(json.dumps(dict(states=states, hold_seconds=0, verdict='fail', failure='fixture')))
            for slot in (1, 2):
                client = run / f'client{slot}'
                (client / 'installed').mkdir(parents=True)
                (client / 'result.json').write_text(json.dumps(dict(lane_restored=True)))
                (client / 'installed/x4_lan_lab-config.lua').write_text(CONFIG)
                rows, watches, model_tickets = context()
                tickets, edges, stop = receipts()
                if slot == 2:
                    for state in [row[k] for row in rows for k in ('s0', 's1')] + [watches[0]['s0']]:
                        for key in ('object', 'controller', 'agent', 'slots', 'faction', 'model'):
                            if state[key]: state[key] += 0x100000
                    for row in rows:
                        for key in ('old', 'new', 'result'):
                            if row[key]: row[key] += 0x100000
                    for key in ('event', 'controller0', 'controller1'): tickets[0][key] += 0x100000
                    for key in ('player', 'ship', 'agent', 'owner'): stop[key] += 0x100000
                log = ENABLE + transition_text(rows, watches, model_tickets) + '\n' + render(tickets, edges, stop)
                (client / 'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n' + log)
            result = validate(run, 'battle')
            self.assertEqual([c['evidence_valid'] for c in result['native_player_origins']], [True, True])
            self.assertFalse(any('origin configurations differ' in e for e in result['errors']))
            self.assertFalse(result['session_pass'])
            engine = run / 'client2/engine-output.log'
            original = engine.read_text()
            updated = []
            for line in original.splitlines():
                if 'PLAYER_ORIGIN_' in line: line = line.replace('schema=1', 'schema=2')
                if 'PLAYER_ORIGIN_STOP ' in line: line += ' native_unwinds=0 post_unwinds=0 delivery_unwinds=0'
                updated.append(line)
            engine.write_text('\n'.join(updated))
            result = validate(run, 'battle')
            self.assertEqual([c['evidence_valid'] for c in result['native_player_origins']], [True, True])
            self.assertIn('native player origin receipt schemas differ between players', result['errors'])
            engine.write_text(original)
            path = run / 'client2/installed/x4_lan_lab-config.lua'
            path.write_text(CONFIG.replace(TOKEN, TOKEN.replace('11e12', '11e13')))
            result = validate(run, 'battle')
            self.assertIn('native player origin configurations differ between players', result['errors'])
            path.write_text(CONFIG.replace('native_gate_player_origin_token', 'unused_origin_token'))
            result = validate(run, 'battle')
            self.assertEqual(len(result['native_player_origins']), 2)  # Unexpected rows cannot be silently ignored.
            self.assertFalse(result['native_player_origins'][1]['evidence_valid'])


if __name__ == '__main__':
    unittest.main()
