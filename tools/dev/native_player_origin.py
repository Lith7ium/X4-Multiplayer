"""Independent local GetUp origin receipts, not release or pilot authority proof.

Schema 1 exports producer-scoped transition associations, but no delivery
entry/leave ordinals or after-capture tick. Keep those limitations explicit.
Pointers and opaque native returns join one process only, never two replicas.
"""
import math
import re
import struct

from native_transition_evidence import evidence as transition_evidence
from native_transition_manifest import configured as configured_manifest

TICKET = set('schema ticket event due post_tick delivery_tick post_clock delivery_clock after_clock controller0 controller1 post_returned delivery_state errors post_thread delivery_thread post_rax delivery_rax'.split())
TICKET3 = TICKET | set('request post_enter post_leave delivery_enter delivery_leave after_tick transition_before transition_after'.split())
EDGE = set('schema edge ticket transition'.split())
STATUS = set('schema first last imports bound sites closing calls returns open peak restore_open prebind native_failures tickets edges post_calls post_returns delivery_calls delivery_returns core_open core_peak core_stop_open failures other unknown excluded ticket_rows edge_rows player_saved ship_saved player_runtime ship_runtime player ship agent owner raw_world_coverage action_authority atomic_lease early_coverage raw_pointers due_is_current capture_complete'.split())
STATUS2 = STATUS | set('native_unwinds post_unwinds delivery_unwinds'.split())
FIXED = set('due post_clock delivery_clock after_clock post_rax delivery_rax player_saved ship_saved player_runtime ship_runtime'.split())
POINTERS = set('event controller0 controller1 player ship agent owner'.split())
DEC64 = set('post_tick delivery_tick transition calls returns open peak restore_open prebind native_failures post_calls post_returns delivery_calls delivery_returns core_open core_peak core_stop_open failures other unknown excluded'.split())
DEC64 |= set('native_unwinds post_unwinds delivery_unwinds'.split())
DEC64 |= TICKET3-TICKET
BOOL = set('bound closing post_returned raw_world_coverage action_authority atomic_lease early_coverage raw_pointers due_is_current capture_complete'.split())
PREFIX = 'NATIVE_GATE PLAYER_ORIGIN_'
CAPS = {'TICKET': 1024, 'EDGE': 192, 'STOP': 1536}


def number(key, text):
    if key in FIXED:
        pattern, base, bits = r'[0-9a-f]{16}', 16, 64
    elif key in POINTERS:
        pattern, base, bits = r'(?:0|[1-9a-f][0-9a-f]{0,15})', 16, 64
    else:
        bits = 64 if key in DEC64 else 32
        pattern, base = r'(?:0|[1-9][0-9]{0,' + str(19 if bits == 64 else 9) + r'})', 10
    if not re.fullmatch(pattern, text):
        raise ValueError('numeric syntax/width')
    value = int(text, base)
    if value >= 1 << bits or (key in BOOL and value not in (0, 1)):
        raise ValueError('numeric bounds')
    return value


def finite_clock(bits):
    value = struct.unpack('<d', struct.pack('<Q', bits))[0]
    return math.isfinite(value) and value >= 0


def selected_lifetime(state, stop):
    return (state['owner'] == 'player' and
            all(state[a] == stop[b] for a, b in (('saved', 'ship_saved'), ('runtime', 'ship_runtime'),
                ('object', 'ship'), ('agent', 'agent'), ('faction', 'owner'))))


def global_activation(event, events, stop):
    """Global callback has no selected-ship state, even inside publication."""
    if (event['arg'] not in (0, 1) or event['extra'] != 8 or
            any(event[k] for k in ('queue', 'enqueue', 'queue_valid', 'transfer', 'publication', 'old', 'new', 'result', 'old_owns')) or
            any(state['owner'] != '-' or any(value != 0 for key, value in state.items() if key != 'owner')
                for state in (event['s0'], event['s1']))):
        return False
    if event['parent']:
        parent = events.get(event['parent'])
        return bool(parent and parent['kind'] == 2 and not parent['errors'] and not parent['queue_errors'] and
                    parent['thread'] == event['thread'] and parent['boot'] == 3 and
                    parent['tick'] <= event['tick'] and parent['old'] >= 0x10000 and parent['new'] >= 0x10000 and
                    parent['s0']['controller'] == parent['old'] and parent['s1']['controller'] == parent['new'] and
                    parent['enter'] < event['enter'] < event['leave'] < parent['leave'] and
                    all(selected_lifetime(parent[phase], stop) for phase in ('s0', 's1')))
    return True


def configured(config):
    tokens = re.findall(r'\bnative_gate_player_origin_token\s*=\s*"([^"\r\n]*)"', config)
    if len(tokens) != 1:
        raise ValueError('one explicit player origin token required')
    match = re.fullmatch(r'1\|([0-9a-f]{16})\|([0-9a-f]{16})\|([1-9][0-9]{0,2})\|([1-9][0-9]{0,2})', tokens[0])
    if not match:
        raise ValueError('invalid player origin token')
    player, ship = (int(match[i], 16) for i in (1, 2))
    first, last = (int(match[i]) for i in (3, 4))
    if not player or not ship or player == ship or not 1 <= first <= last <= 120:
        raise ValueError('player origin token bounds')
    return dict(schema=1, player_saved=player, ship_saved=ship, first=first, last=last)


def evidence(log):
    tickets, edges, stop, errors = {}, {}, None, []
    records = 0
    for line in log.splitlines():
        if PREFIX not in line:
            continue
        records += 1
        if records > 32 + 128 + 1:
            if records == 32 + 128 + 2:
                errors.append('origin record capacity exceeded')
            continue
        try:
            if line.count(PREFIX) != 1 or len(line) >= 2048:
                raise ValueError('line capacity/marker')
            kind, raw = line.split(PREFIX, 1)[1].split(' ', 1)
            if kind not in CAPS or len(('PLAYER_ORIGIN_' + kind + ' ' if kind != 'STOP' else '') + raw) >= CAPS[kind]:
                raise ValueError('row capacity/type')
            pairs = [p.split('=', 1) for p in raw.split(' ')]
            row = dict(pairs)
            if stop is not None or len(row) != len(pairs) or set(row) != {'TICKET': TICKET3 if row.get('schema') == '3' else TICKET, 'EDGE': EDGE, 'STOP': STATUS2 if row.get('schema') in ('2','3') else STATUS}[kind]:
                raise ValueError('row shape/order')
            row = {key: number(key, value) for key, value in row.items()}
            if row['schema'] not in (1, 2, 3):
                raise ValueError('schema')
            if kind == 'STOP':
                stop = row
            else:
                target, field, cap = (tickets, 'ticket', 32) if kind == 'TICKET' else (edges, 'edge', 128)
                if row[field] != len(target) + 1 or row[field] > cap or (kind == 'TICKET' and edges):
                    raise ValueError('row index/order')
                target[row[field]] = row
        except (TypeError, ValueError):
            errors.append('malformed origin record')

    if stop is None:
        errors.append('origin stop missing')
    else:
        zeros = 'sites open restore_open native_failures core_open core_stop_open failures raw_world_coverage action_authority atomic_lease early_coverage due_is_current'.split()
        if stop['schema'] >= 2: zeros += ['native_unwinds', 'post_unwinds', 'delivery_unwinds']
        if (any(stop[k] for k in zeros) or any(stop[k] != 1 for k in ('bound', 'imports', 'closing', 'raw_pointers')) or
                not 1 <= stop['first'] <= stop['last'] <= 120 or
                any(stop[k] != len(tickets) for k in ('tickets', 'ticket_rows')) or
                any(stop[k] != len(edges) for k in ('edges', 'edge_rows')) or
                stop['tickets'] > 32 or stop['edges'] > 128):
            errors.append('incomplete/failed origin coverage')
        core_calls = stop['post_calls'] + stop['delivery_calls']
        if (stop['calls'] != stop['returns'] or stop['post_calls'] != stop['post_returns'] or
                stop['delivery_calls'] != stop['delivery_returns'] or stop['post_calls'] != len(tickets) or
                stop['delivery_calls'] != len(tickets) + stop['other'] + stop['unknown'] or
                not core_calls <= stop['calls'] <= core_calls + stop['prebind'] + stop['excluded'] or
                stop['prebind'] > stop['calls'] or
                not bool(stop['calls']) <= stop['peak'] <= stop['calls'] or
                not bool(core_calls) <= stop['core_peak'] <= core_calls or stop['core_peak'] > stop['peak']):
            errors.append('inconsistent origin call accounting')
        if (not stop['player_saved'] or not stop['ship_saved'] or stop['player_saved'] == stop['ship_saved'] or
                not stop['player_runtime'] & 0x1ffffff or not stop['ship_runtime'] & 0x1ffffff or
                stop['player_runtime'] == stop['ship_runtime'] or
                any(stop[k] < 0x10000 for k in ('player', 'ship', 'agent', 'owner')) or
                len({stop[k] for k in ('player', 'ship', 'agent')}) != 3):
            errors.append('invalid origin lifetime binding')
        if any(row['schema'] != stop['schema'] for row in list(tickets.values()) + list(edges.values())):
            errors.append('mixed origin receipt schema')

    event_addresses = set()
    for ticket in tickets.values():
        if (ticket['event'] in event_addresses or any(ticket[k] < 0x10000 for k in ('event', 'controller0', 'controller1')) or
                ticket['errors'] or ticket['post_returned'] != 1 or ticket['delivery_state'] != 2 or
                not ticket['post_thread'] or not ticket['delivery_thread'] or
                any(not finite_clock(ticket[k]) for k in ('due', 'post_clock', 'delivery_clock', 'after_clock')) or
                not stop or not stop['first'] <= ticket['post_tick'] <= ticket['delivery_tick'] <= stop['last']):
            errors.append('invalid/incomplete origin ticket')
        event_addresses.add(ticket['event'])
        if ticket['schema']==3 and (ticket['request']!=1 or ticket['post_tick']!=30 or
                not 0<ticket['post_enter']<ticket['post_leave'] or
                not ticket['post_enter']<ticket['delivery_enter']<ticket['delivery_leave'] or
                not (ticket['post_leave']<ticket['delivery_enter'] or ticket['delivery_leave']<ticket['post_leave']) or
                not ticket['delivery_tick']<=ticket['after_tick']<=120 or
                ticket['transition_before']>ticket['transition_after']):
            errors.append('invalid request/delivery ordinal receipt')
    if stop and stop['schema']==3:
        ordinals=sorted(t[k] for t in tickets.values() for k in ('post_enter','post_leave','delivery_enter','delivery_leave'))
        if ordinals!=list(range(1,len(tickets)*4+1)) or any(t['transition_after']>1024 for t in tickets.values()):
            errors.append('origin ordinal capacity/coverage mismatch')
    local_valid = not errors
    if stop and stop['capture_complete'] != int(local_valid and bool(tickets)):
        errors.append('origin claimed completeness disagrees with receipts')
    local_valid = not errors

    transition = transition_evidence(log)
    join_errors, joined = [], []
    if not transition['evidence_valid']:
        join_errors.append('transition evidence invalid (raw failures retained)')
    watches = list(transition['watches'].values())
    if not stop or not transition['stop'] or transition['stop'].get('schema') != 2 or len(watches) != 1:
        join_errors.append('one schema2 controlled ship watch required')
    elif stop:
        watch = watches[0]
        initial = watch['s0']
        if (watch['type'] != 0x2c39488 or watch['owner'] != 'player' or initial['player'] != 1 or initial['owns'] != 1 or
                any(initial[a] != stop[b] for a, b in (('saved', 'ship_saved'), ('runtime', 'ship_runtime'), ('object', 'ship'), ('agent', 'agent'), ('faction', 'owner'))) or
                stop['last'] > transition['stop'].get('last', 0)):
            join_errors.append('origin/initial transition lifetime mismatch')
    seen = set()
    for edge in edges.values():
        ticket = tickets.get(edge['ticket'])
        event = transition['events'].get(edge['transition'])
        if (edge['transition'] in seen or not ticket or not event or not stop or
                event['errors'] or event['queue_errors'] or event['boot'] != 3 or
                event['thread'] != ticket['delivery_thread'] or
                not ticket['delivery_tick'] <= event['tick'] <= stop['last'] or
                (ticket['schema']==3 and (event['tick']>ticket['after_tick'] or
                 not ticket['transition_before']<event['enter']<event['leave']<=ticket['transition_after']))):
            join_errors.append('invalid origin transition association')
        else:
            association = dict(ticket=edge['ticket'], transition=edge['transition'])
            if event['kind'] == 3:
                if not global_activation(event, transition['events'], stop):
                    join_errors.append('invalid global activation association or publication parent')
                association.update(association='producer_scoped_global_activation',
                                   publication_parent=event['parent'] or None, selected_ship_activation_proven=False)
            else:
                if any(not selected_lifetime(event[phase], stop) for phase in ('s0', 's1')):
                    join_errors.append('origin edge left selected lifetime')
            joined.append(association)
        seen.add(edge['transition'])
    errors.extend(join_errors)
    return dict(evidence_valid=not errors, errors=errors, origin_receipts_valid=local_valid,
                tickets=tickets, edges=edges, stop=stop, transition_errors=transition['errors'],
                origin_exercised=bool(tickets), associated_transition_events=joined if not errors else [],
                comparison_scope='local_producer_scoped_receipts_only', scope_containment_serialized=bool(stop and stop['schema']==3),
                exception_cleanup_scope='origin_only' if stop and stop['schema'] >= 2 else 'ordinary_return_only',
                queue_exception_cleanup=False,
                after_capture_tick_serialized=bool(stop and stop['schema']==3), public_request_acceptance_observed=False,
                completed_releases=[], independent_player_context=False, atomic_lease=False)


def configured_evidence(log, config, slot, errors):
    result = evidence(log)
    expected = None
    try:
        expected = configured(config)
        manifest = configured_manifest(config)
        if (not manifest or len(manifest[0]) != 1 or manifest[0][0]['controller'] != 'player' or
                manifest[0][0]['owner'] != 'player' or int(manifest[0][0]['saved'], 16) != expected['ship_saved'] or
                expected['last'] > manifest[1] or not result['stop'] or
                any(result['stop'][key] != value for key, value in expected.items() if key != 'schema')):
            raise ValueError('origin configuration/receipt/watch mismatch')
        if (not re.search(r'native_gate_player_probe\s*=\s*"(?:observe|agreed-getup|diagnostic-getup)"', config) or
                any(not re.search(key + r'\s*=\s*true', config) for key in
                    ('native_gate_world_trace_early', 'native_gate_world_queue_trace', 'native_gate_player_transition_observer'))):
            raise ValueError('origin configuration prerequisites missing')
        if result['stop'] and (result['stop']['schema']==3)!=bool(re.search(r'native_gate_player_probe\s*=\s*"(?:agreed-getup|diagnostic-getup)"',config)):
            raise ValueError('origin public-request schema/mode mismatch')
    except ValueError as error:
        result['errors'].append(str(error))
    result['configuration'] = expected
    result['slot'] = slot
    result['evidence_valid'] = not result['errors']
    if result['errors']:
        result['associated_transition_events'] = []
    errors.extend(f'player {slot} origin: {error}' for error in result['errors'])
    return result
