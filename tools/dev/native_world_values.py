"""Read-only selected Action output receipts; raw pointer/RNG evidence retained."""
import math
import re
import struct
from native_world_unknown_identity import evidence as unknown_identity_evidence

KEYS = {name: set(fields.split()) for name, fields in {
    'EVENT': 'event kind errors thread action candidate children enter leave caller result rng_before rng_after pc_before pc_after flags_before flags_after a b c d',
    'VALUE': 'event tag kind raw payload runtime saved declaration optional_before optional_after begin_before end_before capacity_before count_before begin_after end_after capacity_after count_after',
    'PHASE': 'event status_before eligible_before app_before vbi_before view_before main_before elapsed_before tls_before game_before real_before saved_before status_after eligible_after app_after vbi_after view_after main_after elapsed_after tls_after game_after real_after saved_after',
    'ARRAY': 'event phase index object runtime saved type valid',
    'UNKNOWN': 'index reason reads owned errors thread ui boot tick exec current raw_tag raw_value tag context lifetime entity runtime saved phase_status eligible app vbi view main elapsed tls game real saved_time',
    'STOP': 'schema capacity candidate_capacity reserved complete rows failures dropped calls other_scope other_type open peak restore_open sites closed patch_failures patch_errors protection_pending flush_pending unknown_total unknown_samples unknown_rows unknown_omitted reason1 reason2 reason3 reason4 reason5 reason6 reason7 exception_cleanup',
}.items()}
BYTE_FIELDS = {'raw': 16, 'payload': 16, 'declaration': 34, 'optional_before': 32, 'optional_after': 32}
HEX = set('caller result rng_before rng_after flags_before flags_after a b c d tag runtime saved begin_before end_before capacity_before begin_after end_after capacity_after app_before app_after tls_before tls_after game_before game_after real_before real_after saved_before saved_after object type exec current raw_tag raw_value context lifetime entity app tls game real saved_time'.split())
U32 = set('event kind errors thread action candidate children tag phase index type valid schema capacity candidate_capacity complete rows sites closed patch_errors protection_pending flush_pending exception_cleanup status_before status_after eligible_before eligible_after app_before app_after vbi_before vbi_after view_before view_after main_before main_after reason reads owned ui boot raw_tag phase_status eligible app vbi view main unknown_samples unknown_rows'.split())
IMAGE_SIZE = 0x6fa9000
CALLERS = {1: {0xbe23af}, 2: {0xbe0c8b}, 3: {0xc5b2ac, 0xc5b2c5, 0xc5ba03}, 4: {0xd2ad67}}
OFFSETS = {1: 0x1b8, 2: 0xf8, 3: 0x28, 4: 0xf0}


def configured(config):
    matches = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_world_values\s*=\s*(.*?)\s*$', config, re.M)
    if not matches:
        if 'native_gate_world_values' in config:
            raise ValueError('unsupported typed Action output configuration')
        return False
    if matches != ['true']:
        raise ValueError('typed Action outputs require one explicit true configuration')
    return True


def fields(body, name):
    if not body.isascii() or len(body) + len('[X4LAN] NATIVE_GATE WORLD_VALUES_') + len(name) + 2 >= 2048 or (name == 'STOP' and len(body) >= 1024):
        raise ValueError('row exceeds native formatter bound')
    values = {}
    for item in body.split():
        key, value = item.split('=', 1)
        if key in values:
            raise ValueError('duplicate field')
        if key in BYTE_FIELDS:
            if not re.fullmatch('[0-9a-f]{%d}' % (BYTE_FIELDS[key] * 2), value):
                raise ValueError('invalid byte field')
            values[key] = bytes.fromhex(value)
            continue
        base = 16 if key in HEX else 10
        if not re.fullmatch('[0-9a-f]+' if base == 16 else '[0-9]+', value):
            raise ValueError('invalid number')
        number = int(value, base)
        width = 16 if key in ('flags_before', 'flags_after') else 32 if key in U32 else 64
        if number >= 1 << width:
            raise ValueError('native integer overflow')
        values[key] = number
    if set(values) != KEYS[name]:
        raise ValueError('unknown/missing fields')
    return values


def component(item):
    if not item['object']:
        return not any(item[k] for k in ('runtime', 'saved', 'type', 'valid'))
    return 0x10000 <= item['object'] <= (1 << 64)-1-0xd1 and item['runtime'] > 0 and item['saved'] > 0 and 0 < item['type'] < IMAGE_SIZE and item['valid'] <= 255


def optional(raw):
    pointer, runtime, saved, kind, valid = struct.unpack('<QQQIB3x', raw)
    if raw[-3:] != b'\0' * 3:
        raise ValueError('impossible component padding')
    result = dict(object=pointer, runtime=runtime, saved=saved, type=kind, valid=valid)
    if not component(result):
        raise ValueError('unsupported component lifetime/reference')
    return result


def value_semantic(v):
    tag = int.from_bytes(v['raw'][:4], 'little')
    if tag != v['tag']:
        raise ValueError('copied Value tag mismatch')
    if v['kind'] == 1 and tag <= 9:
        if v['runtime'] or v['saved'] or any(v['payload']):
            raise ValueError('impossible scalar metadata')
        size, code = (4, '<i') if tag <= 1 else (8, '<q') if tag <= 3 else (4, '<f') if tag <= 6 else (8, '<d')
        payload = v['raw'][8:8 + size]
        if not math.isfinite(struct.unpack(code, payload)[0]):
            raise ValueError('nonfinite scalar')
        return ('scalar', tag, payload.hex())
    if v['kind'] == 2 and tag == 0x63:
        if v['runtime'] or v['saved'] or int.from_bytes(v['raw'][8:], 'little') < 0x10000 or not all(math.isfinite(x) for x in struct.unpack('<3f', v['payload'][:12])):
            raise ValueError('unsupported borrowed position')
        return ('position', v['payload'][:12].hex())
    if v['kind'] == 3 and tag == 0xa:
        if not v['runtime'] or not v['saved'] or int.from_bytes(v['raw'][8:], 'little') != v['runtime'] or any(v['payload']):
            raise ValueError('unresolved component Value')
        return ('component', v['saved'])
    raise ValueError('unsupported typed output')


def evidence(lines, actions, errors, label='world values', parent_coverage=None):
    events = {}; stop = None; row_count = 0; row_order = []; unknown = []; parse_start = len(errors)
    if isinstance(lines, str):
        lines = lines.splitlines()
    else:
        lines = list(lines)  # Both independent decoders consume the same input.
    for line in lines:
        if 'NATIVE_GATE WORLD_VALUES_' not in line:
            continue
        try:
            text = line.rstrip('\r\n').split('NATIVE_GATE WORLD_VALUES_', 1)[1]
            name, body = text.split(' ', 1)
            if name not in KEYS:
                raise ValueError('unknown record')
            row = fields(body, name)
            if name == 'STOP':
                if stop is not None:
                    raise ValueError('duplicate STOP')
                stop = row; continue
            if name == 'UNKNOWN':
                if stop is not None or row['index'] != len(unknown)+1 or len(unknown)>=28 or not 1<=row['reason']<=7 or row['reads']>31 or row['owned'] not in (0,1) or row['errors'] or row['ui']!=1 or row['boot']!=2 or row['tick'] or not row['thread'] or row['exec']<0x10000:
                    raise ValueError('invalid unresolved-owner sample')
                if row['owned'] and (row['reads']&25 !=25 or row['current']!=row['exec'] or row['raw_tag']!=0x66 or row['raw_value']<0xa8 or row['context']!=row['raw_value']-0xa8 or not row['lifetime']):
                    raise ValueError('unproven native owner bracket')
                if row['reads']&4 and (not row['eligible'] or row['phase_status']!=1 or row['main'] or row['view']!=1 or row['elapsed']>120000 or (row['app'],row['vbi']) not in ((0x20,0),(0x26,1))):
                    raise ValueError('inconsistent unresolved phase sample')
                if row['reads']&4 and (row['tls']!=row['game'] or row['game']!=row['saved_time'] or not all(math.isfinite(struct.unpack('<d',row[key].to_bytes(8,'little'))[0]) for key in ('tls','game','real','saved_time'))):
                    raise ValueError('inconsistent unresolved native clocks')
                if row['reads']&2 and row['reason']>=3 and (row['tag']!=0x66 or row['context']<0x10000 or not row['lifetime'] or (row['reason']>=5 and row['entity']<0x10000) or (row['reason']>=6 and not row['runtime']) or (row['reason']==6 and row['saved']) or (row['reason']==7 and not row['saved'])):
                    raise ValueError('inconsistent unresolved-owner reason')
                unknown.append(row);continue
            row_count += 1; index = row['event']
            if stop is not None:
                raise ValueError('event after STOP')
            if not 1 <= index <= 256:
                raise ValueError('event bound')
            event = events.setdefault(index, {'arrays': {0: {}, 1: {}}})
            if name == 'ARRAY':
                if row['phase'] not in (0, 1) or row['index'] >= 128 or row['index'] in event['arrays'][row['phase']]:
                    raise ValueError('duplicate/invalid array row')
                event['arrays'][row['phase']][row['index']] = row
                row_order.append((index, name, row['phase'], row['index']))
            else:
                if name.lower() in event:
                    raise ValueError('duplicate event part')
                event[name.lower()] = row
                row_order.append((index, name))
        except (ValueError, KeyError) as exc:
            errors.append(f'{label}: {exc}')
    parse_errors = list(errors[parse_start:])
    parent_map = {x['event']['event']: x for x in actions}
    for index, record in events.items():
        record_start = len(errors)
        try:
            if set(record) != {'arrays', 'event', 'value', 'phase'}:
                raise ValueError('missing event part')
            e, v, p = (record[x] for x in ('event', 'value', 'phase'))
            parent = parent_map[e['action']]; pe = parent['event']; pg = parent['before']
            stage = 4 if (pe['type'], pe['action']) == (0x2c0f3c8, 0xb87a30) else pe['stage']
            if e['errors'] or pe['errors'] or e['kind'] not in (0, 1) or stage not in OFFSETS or e['thread'] != pe['thread'] or not 0 < e['enter'] < e['leave'] or e['pc_before'] >= 1048576 or e['pc_after'] >= 1048576:
                raise ValueError('failed/incomplete selected scope')
            if e['a'] != pg['node'] + OFFSETS[stage] or e['a'] >= 1 << 64 or e['caller'] >= IMAGE_SIZE or e['b' if e['kind'] == 0 else 'c'] != pg['frame']:
                raise ValueError('parent Action argument join')
            for phase in ('before', 'after'):
                q = parent[phase]
                if p['status_' + phase] != 1 or p['eligible_' + phase] != 1 or p['main_' + phase] or p['view_' + phase] != 1 or p['elapsed_' + phase] > 120000:
                    raise ValueError('ineligible phase')
                if (p['app_' + phase], p['vbi_' + phase]) not in ((0x20, 0), (0x26, 1)):
                    raise ValueError('unsupported loading flags')
                for key in ('tls', 'game', 'real', 'saved'):
                    if not math.isfinite(struct.unpack('<d', p[key + '_' + phase].to_bytes(8, 'little'))[0]):
                        raise ValueError('nonfinite native clock')
                if p['tls_' + phase] != p['game_' + phase] or p['game_' + phase] != p['saved_' + phase] or p['game_' + phase] != q['game']:
                    raise ValueError('parent clock join')
            if any(p[x + '_before'] != p[x + '_after'] for x in ('app', 'vbi', 'tls', 'game', 'saved')):
                raise ValueError('phase changed during original')
            if e['kind'] == 0:
                if e['caller'] not in CALLERS[stage] or e['c'] != e['a'] + 16 or not 0x10000 <= e['d'] <= (1 << 64)-16 or e['result'] & 255 not in (0, 1) or e['children']:
                    raise ValueError('unowned assignment caller/result')
                record['semantic'] = value_semantic(v)
                if any(v[k] for k in ('begin_before', 'end_before', 'capacity_before', 'count_before', 'begin_after', 'end_after', 'capacity_after', 'count_after')) or any(v['declaration']) or any(v['optional_before']) or any(v['optional_after']) or any(record['arrays'].values()):
                    raise ValueError('assignment has candidate storage')
                if stage != 4 and e['candidate']:
                    raise ValueError('foreign candidate parent')
                if stage == 4:
                    candidate = events[e['candidate']]; ce = candidate['event']; cv = candidate['value']
                    if ce['errors'] or ce['kind'] != 1 or ce['action'] != e['action'] or ce['a'] != e['a'] or ce['c'] != e['b'] or not ce['enter'] < e['enter'] < e['leave'] < ce['leave'] or v['kind'] != 3:
                        raise ValueError('candidate/assignment scope mismatch')
                    if not any(x['runtime'] == v['runtime'] and x['saved'] == v['saved'] for x in candidate['arrays'][0].values()):
                        raise ValueError('assigned component absent from candidate list')
            else:
                if stage != 4 or e['caller'] != 0xb87ae8 or not 0x10000 <= e['b'] <= (1 << 64)-24 or e['candidate'] or v['tag'] or v['kind'] or any(v['raw']) or any(v['payload']) or v['runtime'] or v['saved']:
                    raise ValueError('unowned candidate call')
                for phase_id, phase in enumerate(('before', 'after')):
                    begin, end, capacity, count = (v[x + '_' + phase] for x in ('begin', 'end', 'capacity', 'count'))
                    if not 0 <= count <= 128 or not begin <= end <= capacity or (capacity - begin) % 8 or (capacity - begin) // 8 > 128 or end - begin != count * 8 or (not begin and (end or capacity)) or (begin and (begin < 0x10000 or begin % 8)):
                        raise ValueError('candidate header bounds')
                    array = record['arrays'][phase_id]
                    if set(array) != set(range(count)) or not all(component(x) for x in array.values()):
                        raise ValueError('incomplete/unresolved candidate array')
                    if optional(v['optional_' + phase])['object'] != e['d']:
                        raise ValueError('optional component argument mismatch')
                for field in ('begin', 'end', 'capacity', 'count', 'optional'):
                    if v[field + '_before'] != v[field + '_after']:
                        raise ValueError('candidate lifetime/header changed')
                if [{k: record['arrays'][0][i][k] for k in ('object', 'runtime', 'saved', 'type', 'valid')} for i in range(v['count_before'])] != [{k: record['arrays'][1][i][k] for k in ('object', 'runtime', 'saved', 'type', 'valid')} for i in range(v['count_after'])]:
                    raise ValueError('candidate object changed during original')
                children = [x['event'] for x in events.values() if 'event' in x and x['event']['candidate'] == index]
                if e['children'] != len(children):
                    raise ValueError('missing candidate output receipt')
                record['semantic'] = ('candidates', tuple((record['arrays'][0][i]['saved'], record['arrays'][0][i]['type'], record['arrays'][0][i]['valid']) for i in range(v['count_before'])), tuple(optional(v['optional_before'])[key] for key in ('saved','type','valid')), v['declaration'][32:].hex())
        except (ValueError, KeyError, OverflowError) as exc:
            errors.append(f'{label} event {index}: {exc}')
        record['validation_errors'] = parse_errors + errors[record_start:]
    expected_order=[]; boundaries=[]
    for index, record in sorted(events.items()):
        expected_order.extend((index, name) for name in ('EVENT','VALUE','PHASE'))
        for phase in (0,1):
            expected_order.extend((index,'ARRAY',phase,i) for i in sorted(record['arrays'][phase]))
        if 'event' in record:boundaries.extend((record['event']['enter'],record['event']['leave']))
    if row_order!=expected_order or sorted(boundaries)!=list(range(1,2*len(events)+1)):
        errors.append(f'{label}: missing/reordered publication or causal boundaries')
    if stop is None:
        errors.append(f'{label}: missing STOP')
    elif stop['schema'] != 1 or stop['capacity'] != 256 or stop['candidate_capacity'] != 128 or stop['complete'] != stop['reserved'] or stop['complete'] != len(events) or set(events) != set(range(1, len(events) + 1)) or stop['rows'] != row_count or not events or stop['closed'] != 1 or stop['exception_cleanup'] or any(stop[k] for k in ('failures', 'dropped', 'open', 'restore_open', 'sites', 'patch_failures', 'patch_errors', 'protection_pending', 'flush_pending')) or stop['calls'] < stop['reserved'] or not 1 <= stop['peak'] <= stop['calls']:
        errors.append(f'{label}: failed/incomplete observer coverage')
    if stop is not None:
        if stop['calls']!=stop['reserved']+stop['other_scope']+stop['other_type']+stop['unknown_total'] or stop['unknown_samples']!=len(unknown) or stop['unknown_rows']!=len(unknown) or stop['unknown_total']!=stop['unknown_samples']+stop['unknown_omitted'] or sum(stop['reason'+str(i)] for i in range(1,8))!=stop['unknown_total'] or any(sum(row['reason']==i for row in unknown)!=min(4,stop['reason'+str(i)]) for i in range(1,8)):
            errors.append(f'{label}: unresolved-owner/admission accounting mismatch')
        if parent_coverage is not None and (parent_coverage.get('stop') is None or parent_coverage['stop']['unknown'] != stop['unknown_total']):
            errors.append(f'{label}: unresolved-owner count differs from parent Action observer')
    details = unknown_identity_evidence(lines, unknown, errors, label+' identity')
    return events, {'stop': stop, 'unknown': unknown, 'unknown_identity': details, 'exception_cleanup': False}


def paired_diagnostics(captures, claimed_pass, errors):
    if not captures:
        return {'configured': False}
    captures=sorted(captures,key=lambda c:c['slot'])
    selected_valid = len(captures) == 2 and {c['slot'] for c in captures}=={1,2} and captures[0].get('expected')==captures[1].get('expected') and all(not c['errors'] and all(error.endswith(': failed/incomplete observer coverage') for error in c.get('parent_errors',[])) for c in captures)
    complete = selected_valid and all(not c.get('parent_errors') and not c['coverage']['stop']['unknown_total'] for c in captures)
    differences = []
    if selected_valid:
        def semantic(c):
            parents = {x['event']['event']: x['event'] for x in c['actions']}; rows = {}; occurrences = {}; action_keys = {}; candidate_keys = {}
            # Local thread IDs and interleaving between owners are diagnostics.
            # Compare each saved owner's causal sequence and explicit parent links.
            for parent in sorted(parents.values(), key=lambda p: p['event']):
                owner = parent['id']; ordinal = occurrences.get(owner, 0)
                occurrences[owner] = ordinal + 1; action_keys[parent['event']] = ordinal
            for record in sorted(c['events'].values(), key=lambda r: r['event']['enter']):
                e = record['event']; p = parents[e['action']]
                sequence = rows.setdefault(p['id'], [])
                if e['kind'] == 1:
                    candidate_keys[e['event']] = len(sequence)
                sequence.append(dict(action_ordinal=action_keys[e['action']], script=p['name'], version=p['version'], slot=p['slot'], kind=e['kind'], candidate=candidate_keys.get(e['candidate']) if e['candidate'] else None, value=record['semantic'], rng_before=e['rng_before'], rng_after=e['rng_after'], assigned=(e['result'] & 255) if e['kind'] == 0 else None))
            return rows
        left, right = map(semantic, captures)
        for owner in sorted(set(left) | set(right)):
            a_rows, b_rows = left.get(owner, []), right.get(owner, [])
            for i in range(max(len(a_rows), len(b_rows))):
                a = a_rows[i] if i < len(a_rows) else None; b = b_rows[i] if i < len(b_rows) else None
                if a != b:
                    differences.append(dict(owner=owner, index=i, left=a, right=b))
    equal = selected_valid and not differences
    if claimed_pass and (not complete or not equal):
        errors.append('claimed native pass lacks equal complete typed Action receipts')
    return dict(configured=True, capture_valid=complete, selected_receipts_valid=selected_valid, selected_traces_equal=equal, traces_equal=complete and equal, differences=differences, exception_cleanup=False, component_type_scope='windows_image_rva')
