"""Strict evidence for the single selected native FindComponent RNG scope.

The inert JSON describes a reviewed selection. Only the explicit overlay switch
activates it; the JSON's enabled:false never activates a game by itself.
"""
import hashlib
import json
import re
import struct
from pathlib import Path
from native_world_wait import rng_step
from native_world_values import value_semantic

CONTRACT_SHA = '1a0272594d0989d56476d130f1842e4f3f5e62de7041122c7e3a379559995a07'
THIRD_CONTRACT_SHA = '44ed014534fb05209d4717179c6858e0d982d05a9e2d2d5a70c946bfa9f0c101'
PREFIX = 'FIND_COMPONENT_RNG_1;'
THIRD_PREFIX = 'FIND_COMPONENT_RNG_2;'
KEYS = {name: set(keys.split()) for name, keys in {
    'EVENT': 'event part errors applied restored abnormal thread action candidate ordinal caller_before seed scoped_before scoped_after caller_after result assigned',
    'IDENTITY': 'event part context lifetime entity runtime saved script version exec program order order_id state backlink registry group flags generation',
    'STOP': 'schema capacity reserved complete rows calls open restore_open failures dropped closed used anchor_calls anchor_ready anchor_pending anchor_errors seed digest saved_pilot saved_order saved_script_decimal scope_exception_cleanup enclosing_exception_cleanup',
}.items()}
HEX = set('caller_before seed scoped_before scoped_after caller_after result assigned context lifetime entity runtime saved script exec program order order_id backlink registry group flags generation saved_pilot saved_order'.split())
U32 = set('event part errors applied restored abnormal thread action candidate ordinal version state flags generation schema capacity reserved complete rows closed used anchor_ready anchor_pending anchor_errors saved_script_decimal scope_exception_cleanup enclosing_exception_cleanup'.split())


def selection(schema=1):
    if type(schema) is not int or schema not in (1, 2):
        raise ValueError('unsupported FindComponent composition')
    filename = 'native_world_find_selection.json' if schema == 1 else 'native_world_find_third_wait_selection.json'
    document = json.loads(Path(__file__).with_name(filename).read_text())
    raw = json.dumps(document, sort_keys=True, separators=(',', ':')).encode('ascii')
    if hashlib.sha256(raw).hexdigest() != (CONTRACT_SHA if schema == 1 else THIRD_CONTRACT_SHA):
        raise ValueError('reviewed FindComponent selection contract changed')
    return document


def key_bytes(document):
    e = document['entries'][0]
    a = e['wait_anchor']
    name, time = e['script'].encode('ascii'), a['time_text'].encode('ascii')
    return (document['domain'].encode('ascii') + b'\0' + bytes.fromhex(document['snapshot_sha256']) +
            struct.pack('<QQH', int(e['saved_pilot'], 16), int(e['saved_order'], 16), len(name)) + name +
            struct.pack('<IQQIQQQI', e['version'], e['action_slot'], e['action_pc'], e['phase'], e['tick'], e['ordinal'],
                        a['saved_script_id'], a['blocking_index']) + struct.pack('<H', len(time)) + time +
            struct.pack('<QQ', a['native_slot'], a['native_pc']))


def manifest_wire(document):
    expected = selection(document.get('schema'))
    # Equality alone accepts bool as int; exact canonical types are also required.
    if json.dumps(document, sort_keys=True) != json.dumps(expected, sort_keys=True):
        raise ValueError('unsupported FindComponent selection, policy or guards')
    digest = hashlib.sha256(key_bytes(document)).digest()
    entry = document['entries'][0]
    if digest.hex() != entry['expected_key_sha256'] or f'{int.from_bytes(digest[:8], "little"):016x}' != entry['expected_seed_hex']:
        raise ValueError('FindComponent semantic seed mismatch')
    wire = (PREFIX if document['schema'] == 1 else THIRD_PREFIX) + document['snapshot_sha256'] + ';' + entry['guards']['program_shape_hash'] + ';' + entry['expected_seed_hex'] + ';'
    return wire if document['schema'] == 1 else wire + THIRD_CONTRACT_SHA + ';'


def composition_waits(document):
    """The exact new composition supplies its unchanged observation fields too."""
    policy = document['composed_wait_policy']
    observed = dict(schema=2, snapshot_sha256=policy['snapshot_sha256'], entries=[
        {k: v for k, v in e.items() if k not in ('policy', 'slot', 'pc', 'since')} for e in policy['entries']])
    return policy, observed


def composition_config(config, document):
    from native_world_wait_rng import manifest_wire as wait_wire
    from native_gate_wait_manifest import double_bits, selected_time_bits
    policy, observed = composition_waits(document)
    wanted = {
        'native_gate_snapshot_sha': document['snapshot_sha256'],
        'native_gate_world_wait_rng_manifest': wait_wire(policy, observed),
        'native_gate_world_wait_manifest': ';'.join(','.join([
            e['id'], str(e['version']), str(e['index']), selected_time_bits(e),
            double_bits(e['minimum']), double_bits(e['maximum']), e['name']]) for e in observed['entries'])}
    for key, value in wanted.items():
        lines = re.findall(r'^\s*X4LAN_CONFIG\.' + key + r'\s*=\s*("(?:\\.|[^"\\])*")\s*$', config, re.M)
        if len(lines) != 1 or json.loads(lines[0]) != value:
            raise ValueError('FindComponent composition requires exact ' + key)


def verify_input_files(document, snapshot, observed, policy, actions):
    """Builder admission pins file bytes separately from semantic seed identity."""
    manifest_wire(document)
    if hashlib.sha256(snapshot.read_bytes()).hexdigest() != document['snapshot_sha256']:
        raise ValueError('FindComponent snapshot mismatch')
    for path,key in ((observed,'existing_wait_manifest_sha256'),(policy,'existing_wait_policy_sha256'),
                     (actions,'existing_action_manifest_sha256')):
        if hashlib.sha256(path.read_bytes()).hexdigest() != document[key]:
            raise ValueError('FindComponent requires exact reviewed composition input manifests')


def configured(config):
    lines = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_world_find_rng_manifest\s*=\s*("(?:\\.|[^"\\])*")\s*$', config, re.M)
    if not lines and 'native_gate_world_find_rng_manifest' not in config:
        return None
    if len(lines) != 1:
        raise ValueError('invalid or duplicated FindComponent RNG configuration')
    wire = json.loads(lines[0])
    expected = selection(2 if wire.startswith(THIRD_PREFIX) else 1)
    if wire != manifest_wire(expected):
        raise ValueError('invalid or duplicated FindComponent RNG configuration')
    if expected['schema'] == 2:
        composition_config(config, expected)
    return expected


def fields(body, name):
    bound = 1024 if name == 'STOP' else 1536
    if not body.isascii() or len(body) + len('[X4LAN] NATIVE_GATE WORLD_FIND_') + len(name) + 2 >= bound:
        raise ValueError('FindComponent row exceeds emitted bound')
    result = {}
    for item in body.split():
        k, v = item.split('=', 1)
        if k in result:
            raise ValueError('duplicate field')
        if k in ('digest', 'composition'):
            if not re.fullmatch('[0-9a-f]{64}', v):
                raise ValueError('invalid digest')
            result[k] = v
            continue
        if not re.fullmatch('[0-9a-f]+' if k in HEX else '[0-9]+', v):
            raise ValueError('invalid native integer')
        value = int(v, 16 if k in HEX else 10)
        if value >= 1 << (32 if k in U32 else 64):
            raise ValueError('native field overflow')
        result[k] = value
    expected = KEYS[name] | ({'composition'} if name == 'STOP' and result.get('schema') == 2 else set())
    if set(result) != expected:
        raise ValueError('unknown/missing fields')
    return result


def selected_value_join(record, action, candidate=None):
    """Check selected joins without treating unrelated aggregate coverage as valid.

    Raw readers return records even when they also report validation errors.
    Their per-record receipt is required in addition to the exact native joins.
    """
    if record.get('validation_errors') != []:
        raise ValueError('selected raw Value/candidate record did not validate')
    e, v, p = (record[k] for k in ('event', 'value', 'phase'))
    a, g = action['event'], action['before']
    if (e['errors'] or e['thread'] != a['thread'] or e['action'] != a['event'] or
            (e['pc_before'], e['pc_after'], e['flags_before'], e['flags_after']) != (130, 130, 0, 0) or
            not 0 < e['enter'] < e['leave'] or e['a'] != g['node'] + 0xf0):
        raise ValueError('selected Value/candidate thread/PC/Action join')
    for suffix in ('before', 'after'):
        expected = {'status': 1, 'eligible': 1, 'app': 0x20, 'vbi': 0, 'view': 1, 'main': 0,
                    'tls': 0x401c54fdf3b645a2, 'game': 0x401c54fdf3b645a2, 'saved': 0x401c54fdf3b645a2}
        if any(p[k+'_'+suffix] != value for k, value in expected.items()) or p['elapsed_'+suffix] > 120000:
            raise ValueError('selected Value/candidate phase join')
        if p['game_'+suffix] != action[suffix]['game']:
            raise ValueError('selected Value/candidate parent clock join')
    if candidate is None:
        if e['kind'] != 1 or e['candidate'] or e['c'] != g['frame'] or e['d']:
            raise ValueError('selected candidate argument/parent join')
    else:
        ce = candidate['event']
        if (e['kind'] != 0 or e['candidate'] != ce['event'] or e['children'] or
                e['b'] != g['frame'] or e['c'] != e['a']+16 or
                not 0x10000 <= e['d'] <= (1 << 64)-16 or e['result'] & 255 not in (0, 1) or
                not ce['enter'] < e['enter'] < e['leave'] < ce['leave'] or
                value_semantic(v) != ('component', 0x29)):
            raise ValueError('selected component assignment/lifetime join')


def evidence(log, document, actions, values, waits, errors, label):
    before = len(errors)
    events, stop, rows = {}, None, []
    composition_receipt = None
    if document['schema'] == 2:
        from native_world_wait_rng import evidence as wait_rng_evidence
        from native_gate_wait_manifest import selected_time_bits, double_bits
        try:
            # Independent selected-record validation is required before joining
            # RNG receipts; unrelated global unknown coverage remains separate.
            policy, observed = composition_waits(document)
            if len(waits) != 3 or any(w.get('validation_errors') != [] for w in waits):
                raise ValueError('missing/invalid composed Wait records')
            by_owner = {(w['raw'][0]['id'], w['raw'][0]['name']): w['raw'][0] for w in waits}
            for entry in observed['entries']:
                w = by_owner[(int(entry['id'], 16), entry['name'])]
                if any(w[k] != v for k, v in dict(version=entry['version'], xml_index=entry['index'],
                       time=int(selected_time_bits(entry), 16), minimum=int(double_bits(entry['minimum']), 16),
                       maximum=int(double_bits(entry['maximum']), 16)).items()):
                    raise ValueError('changed composed Wait selection')
            composition_receipt = wait_rng_evidence(log, policy, waits, errors, label + ' composed Wait')
        except (KeyError, ValueError, TypeError) as error:
            errors.append(f'{label}: composition proof: {error}')

    def fail(test, message):
        if test:
            errors.append(f'{label}: {message}')

    for line in log.splitlines():
        match = re.search(r'NATIVE_GATE WORLD_FIND_(\w+) (.*)$', line)
        if not match:
            continue
        try:
            name, body = match.groups()
            if name not in KEYS:
                raise ValueError('unknown row kind')
            row = fields(body, name)
            if name == 'STOP':
                if stop is not None:
                    raise ValueError('duplicate STOP')
                stop = row
            else:
                if stop is not None:
                    raise ValueError('row after STOP')
                if not 1 <= row['event'] <= 8 or row['part'] not in ({0} if name == 'EVENT' else {1, 2, 3}):
                    raise ValueError('invalid event/part')
                parts = events.setdefault(row['event'], {})
                if row['part'] in parts:
                    raise ValueError('duplicate event part')
                parts[row['part']] = row
                rows.append((row['event'], row['part']))
        except (ValueError, KeyError) as error:
            errors.append(f'{label}: {error}')
    e = document['entries'][0]
    expected_seed = int(e['expected_seed_hex'], 16)
    fail(stop is None, 'missing STOP')
    if stop:
        fail(any(stop[k] for k in ('open', 'restore_open', 'failures', 'dropped', 'anchor_pending', 'anchor_errors')), 'failed/open/unsupported scope')
        fail(any(stop[k] != v for k, v in dict(schema=document['schema'], capacity=8, reserved=1, complete=1, rows=4, closed=1, used=1, anchor_calls=1,
                                             anchor_ready=1, scope_exception_cleanup=1, enclosing_exception_cleanup=0, saved_script_decimal=38361).items()), 'unexercised/incomplete scope accounting')
        fail(stop['calls'] < 1 or stop['seed'] != expected_seed or stop['digest'] != e['expected_key_sha256'] or
             stop['saved_pilot'] != 0x9d3a4 or stop['saved_order'] != 0x3cdb, 'wrong canonical key/binding')
        fail(document['schema'] == 2 and stop.get('composition') != THIRD_CONTRACT_SHA, 'wrong composition receipt')
    fail(rows != [(1, p) for p in range(4)], 'missing, duplicate or unordered receipt')
    event = events.get(1, {})
    if set(event) == {0, 1, 2, 3}:
        r, anchor, pre, post = [event[p] for p in range(4)]
        fail(r['errors'] != 0 or (r['applied'], r['restored'], r['abnormal'], r['ordinal']) != (1, 1, 0, 0), 'refused/abnormal/duplicate intervention')
        fail(not r['thread'] or not r['action'] or not r['candidate'], 'missing original-call receipt join')
        fail(r['seed'] != expected_seed or r['scoped_before'] != expected_seed or r['scoped_after'] != rng_step(expected_seed) or
             r['caller_before'] != r['caller_after'] or r['assigned'] != 0x29, 'scoped RNG/result/restoration mismatch')
        identity_keys = KEYS['IDENTITY'] - {'event', 'part'}
        fail(any(anchor[k] != pre[k] or pre[k] != post[k] for k in identity_keys), 'changed anchored lifetime')
        for i in (anchor, pre, post):
            fail(i['saved'] != 0x9d3a4 or i['version'] != 10 or i['state'] not in (1, 5, 6, 7) or i['backlink'] != i['context'] or
                 any(i[k] < 0x10000 or i[k] > (1 << 64)-4096 for k in ('context', 'entity', 'script', 'exec', 'program', 'order', 'registry', 'group')) or
                 not all(i[k] for k in ('lifetime', 'runtime', 'order_id')), 'unsupported local order identity')
            slot = (i['order_id'] & 0x3ffff)-1
            fail(slot < 0 or not (i['flags'] >> (16+4*((slot & 0x7fff) % 3)) & 1) or
                 (i['generation'] >> (16*((slot & 0x7fff) % 3 & 1)) & 0xffff) != (i['order_id'] >> 18 & 0xffff), 'order registry generation/occupancy mismatch')
        action = next((a for a in actions if a['event']['event'] == r['action']), None)
        candidate = values.get(r['candidate'])
        fail(action is None or candidate is None, 'missing parent Action/candidate')
        if candidate and candidate['event'].get('kind') != 1:
            fail(True, 'receipt points to an assignment, not candidate call')
            candidate = None
        if action:
            fail(action.get('validation_errors') != [], 'selected raw Action record did not validate')
            a = action['event']; g = action['before']
            fail(a['errors'] or a['id'] != 0x9d3a4 or a['name'] != 'order.fight.patrol' or a['version'] != 10 or a['slot'] != 129 or
                 a['pc_before'] != 130 or a['pc_after'] != 130 or a['chance'] or a['boot'] != 2 or a['tick'] or a['thread'] != r['thread'] or
                 a['program_hash'] != int(e['guards']['program_shape_hash'], 16) or a['type'] != 0x2c0f3c8 or a['action'] != 0xb87a30 or
                 a['parent'] or a['depth'] != 1 or a['children'] or a['unobserved_children'], 'wrong parent Action')
            fail(any(g[k] != pre[v] for k, v in [('context', 'context'), ('lifetime', 'lifetime'), ('entity', 'entity'), ('runtime', 'runtime'),
                                               ('script', 'script'), ('exec', 'exec'), ('program', 'program')]), 'Action/order lifetime mismatch')
        if candidate:
            if action:
                try:
                    selected_value_join(candidate, action)
                except (KeyError, ValueError, TypeError) as error:
                    fail(True, str(error))
            v = candidate['event']
            fail(v['errors'] or v['kind'] != 1 or v['caller'] != 0xb87ae8 or v['action'] != r['action'] or v['children'] != 1 or
                 v['rng_before'] != r['caller_before'] or v['rng_after'] != r['caller_after'] or v['result'] != r['result'], 'raw candidate/intervention mismatch')
            expected = [(int(c['saved'], 16), int(c['type_rva'], 16), 1) for c in e['guards']['candidates']]
            for phase in (0, 1):
                actual = [(x['saved'], x['type'], x['valid']) for _, x in sorted(candidate['arrays'][phase].items())]
                fail(actual != expected, 'candidate sequence changed')
            fail(any(candidate['value'][k] != bytes(32) for k in ('optional_before', 'optional_after')), 'unexpected optional component')
            declaration = candidate['value']['declaration']
            fail(len(declaration) != 34 or int.from_bytes(declaration[:4], 'little') != 0x5b or
                 int.from_bytes(declaration[16:20], 'little') != 0x54 or declaration[32:] != bytes(2), 'unsupported declaration mode')
            children = [c for c in values.values() if c['event']['candidate'] == r['candidate']]
            fail(len(children) != 1, 'missing/duplicate assignment')
            if len(children) == 1:
                child = children[0]
                if action:
                    try:
                        selected_value_join(child, action, candidate)
                    except (KeyError, ValueError, TypeError) as error:
                        fail(True, str(error))
                fail(child['event']['errors'] or child['event']['caller'] != 0xd2ad67 or child['event']['rng_before'] != r['scoped_after'] or
                     child['event']['rng_after'] != r['scoped_after'] or child['value']['saved'] != r['assigned'], 'wrong scoped assignment')
        anchored = [w for w in waits if w['input_output']['id'] == 0x9d3a4 and w['input_output']['name'] == 'order.fight.patrol']
        fail(len(anchored) != 1, 'missing/duplicate original Wait anchor')
        if len(anchored) == 1:
            w = anchored[0]; raw, identity = w['raw'][:2]
            fail(w.get('validation_errors') != [], 'selected raw Wait record did not validate')
            fail(raw['errors'] or raw['slot'] != 121 or raw['pc'] != 122 or raw['version'] != 10 or raw['xml_index'] != 1, 'wrong Wait anchor')
            fail(any(identity[k] != anchor[v] for k, v in [('context', 'context'), ('lifetime', 'lifetime'), ('entity', 'entity'), ('runtime', 'runtime'),
                                                         ('script', 'script'), ('exec', 'exec'), ('program', 'program')]), 'Wait/order lifetime mismatch')
    return dict(events=events, stop=stop, selected_scope_valid=len(errors) == before,
                composition=document['schema'], composed_wait=composition_receipt)


def paired_diagnostics(captures, claimed_pass, errors):
    if not captures:
        return {'configured': False}
    valid = (len(captures) == 2 and {c['slot'] for c in captures} == {1, 2} and
             all(c['evidence']['selected_scope_valid'] and not c['errors'] for c in captures))
    valid = valid and captures[0]['evidence'].get('composition') == captures[1]['evidence'].get('composition')
    outputs = []
    if valid:
        for c in captures:
            r = c['evidence']['events'][1][0]
            outputs.append((r['seed'], r['scoped_before'], r['scoped_after'], r['assigned'], r['ordinal']))
    equal = valid and outputs[0] == outputs[1]
    if claimed_pass and not equal:
        errors.append('claimed pass lacks equal verified selected FindComponent intervention')
    return dict(configured=True, selected_scope_valid=valid, selected_outputs_equal=equal,
                global_capture_valid=False, global_rng_parity=False, enclosing_exception_cleanup=False,
                clients=[dict(slot=c['slot'], errors=c['errors'], evidence=c['evidence']) for c in captures])
