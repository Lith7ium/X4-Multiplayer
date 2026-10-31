"""Bounded read-only early Action::Execute evidence, not target authority."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

PARTS = [set(s.split()) for s in (
    'event part selected errors stage thread ui boot tick enter leave parent depth children unobserved_children id name version slot program_hash type action chance rng_before rng_after pc_before pc_after flags_before flags_after',
    'event part node exec frame current context lifetime entity runtime script program begin end vtable target id name version slot program_hash tag chance phase_status eligible elapsed app_flags vbi_loading view main tls_time game real saved_time table',
    'event part node exec frame current context lifetime entity runtime script program begin end vtable target id name version slot program_hash tag chance phase_status eligible elapsed app_flags vbi_loading view main tls_time game real saved_time table',
)]
STOP = set('schema capacity selected reserved complete rows failures dropped calls other_owner other_script other_context other_phase unknown threads open peak restore_open closed unknown_rng value_coverage chance_coverage exception_cleanup'.split())
HEX = set('id program_hash type action chance rng_before rng_after flags_before flags_after node exec frame current context lifetime entity runtime script program begin end vtable target tag app_flags tls_time game real saved_time table'.split())
GUARD = PARTS[1] - set('event part phase_status eligible elapsed app_flags vbi_loading view main tls_time game real saved_time table'.split())
STAGES = {1: (0x2c0cc88, 0xbe2320), 2: (0x2c0cdd0, 0xbe0ba0), 3: (0x2c0fb30, 0xb77830)}
U32 = set('event part selected errors stage thread ui boot depth children unobserved_children version type action chance tag phase_status eligible app_flags vbi_loading view main schema capacity complete rows closed value_coverage chance_coverage exception_cleanup'.split())
U16 = {'flags_before', 'flags_after'}
IMAGE_SIZE = 0x6fa9000


def selections(entries):
    if not isinstance(entries, list) or not 1 <= len(entries) <= 8:
        raise ValueError('action manifest needs 1..8 script selections')
    seen = set()
    for e in entries:
        if not isinstance(e, dict) or set(e) != {'id', 'version', 'name'}:
            raise ValueError('invalid action manifest fields')
        if not isinstance(e['id'], str) or not re.fullmatch('[0-9a-f]{16}', e['id']) or not int(e['id'], 16):
            raise ValueError('invalid action saved owner')
        if type(e['version']) is not int or not 0 <= e['version'] <= 0x7fffffff:
            raise ValueError('invalid action script version')
        if not isinstance(e['name'], str) or not re.fullmatch('[A-Za-z0-9_.]{1,95}', e['name']):
            raise ValueError('invalid action script name')
        key = (e['id'], e['name'])
        if key in seen:
            raise ValueError('duplicate action selection')
        seen.add(key)
    if len({e['id'] for e in entries}) > 2:
        raise ValueError('action manifest exceeds two-owner bound')
    return entries


def manifest_wire(document, snapshot, snapshot_starts):
    if not isinstance(document, dict) or set(document) != {'schema', 'snapshot_sha256', 'entries'} or type(document['schema']) is not int or document['schema'] != 1:
        raise ValueError('unsupported action manifest schema')
    entries = selections(document['entries']); checksum = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    if document['snapshot_sha256'] != checksum:
        raise ValueError('action manifest snapshot mismatch')
    owners = {e['id']: 0 for e in entries}; versions = {e['name']: [] for e in entries}
    for nodes in snapshot_starts(snapshot):
        path = [n.tag for n in nodes]; node = nodes[-1]
        if path == ['savegame', 'aidirector', 'patch', 'script'] and node.get('name') in versions:
            versions[node.get('name')].append(node.get('version'))
        elif path == ['savegame', 'aidirector', 'entity'] and re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', node.get('id', '')):
            identity = f'{int(node.get("id")[3:-1], 16):016x}'
            if identity in owners:
                owners[identity] += 1
    if any(n != 1 for n in owners.values()) or any(versions[e['name']] != [str(e['version'])] for e in entries):
        raise ValueError('action saved owner or script version missing/duplicated')
    if hashlib.sha256(snapshot.read_bytes()).hexdigest() != checksum:
        raise ValueError('snapshot changed while validating action manifest')
    return ';'.join(f'{e["id"]},{e["version"]},{e["name"]}' for e in entries)


def configured(config):
    matches = re.findall(r'^X4LAN_CONFIG\.native_gate_world_action_manifest\s*=\s*("(?:\\.|[^"\\])*")\s*$', config, re.M)
    if len(matches) != 1:
        raise ValueError('missing or duplicated action manifest configuration')
    wire = json.loads(matches[0])
    if not isinstance(wire, str) or not 0 < len(wire) < 2048:
        raise ValueError('invalid action manifest bound')
    entries = []
    for item in wire.split(';'):
        fields = item.split(',')
        if len(fields) != 3 or not re.fullmatch('[0-9]+', fields[1]):
            raise ValueError('invalid action manifest record')
        entries.append(dict(id=fields[0], version=int(fields[1]), name=fields[2]))
    return selections(entries)


def fields(text):
    result = {}
    for word in text.split():
        key, value = word.split('=', 1)
        if key in result:
            raise ValueError('duplicate action field')
        if key == 'name':
            if not re.fullmatch('[A-Za-z0-9_.-]{1,95}', value):
                raise ValueError('invalid action name')
        else:
            base = 16 if key in HEX else 10
            if not re.fullmatch('[0-9a-f]+' if base == 16 else '[0-9]+', value):
                raise ValueError('invalid action number')
            value = int(value, base)
            width = 16 if key in U16 else 32 if key in U32 else 64
            if value >= 1 << width:
                raise ValueError(f'action {key} exceeds uint{width}')
        result[key] = value
    return result


def evidence(text, expected, errors, label='action'):
    selections(expected); rows = []; stop = None; parse_start = len(errors)
    def fail(condition, message):
        if condition:
            errors.append(f'{label}: {message}')
    for line in text.splitlines():
        if 'NATIVE_GATE WORLD_ACTION_' not in line:
            continue
        kind, _, body = line.split('NATIVE_GATE WORLD_ACTION_', 1)[1].partition(' ')
        try:
            # Native event formatting has a 2048-byte buffer; the tested
            # engine-log budget additionally includes its fixed prefix/newline.
            # STOP's own formatter has a smaller 1024-byte body buffer.
            emitted = 'WORLD_ACTION_'+kind+' '+body
            if not body.isascii() or len('[X4LAN] NATIVE_GATE '+emitted+'\n') >= 2048 or (kind == 'STOP' and len(body) >= 1024):
                raise ValueError('action row exceeds emitted bounds')
            row = fields(body)
            if kind == 'EVENT':
                part = row.get('part', -1)
                if part not in range(3) or set(row) != PARTS[part] or stop is not None or len(rows) >= 2048*3:
                    raise ValueError('invalid action row/schema/order/bound')
                if row['event'] != len(rows)//3+1 or part != len(rows)%3:
                    raise ValueError('missing or reordered action row')
                rows.append(row)
            elif kind == 'STOP':
                if stop is not None or set(row) != STOP:
                    raise ValueError('duplicate or invalid action STOP')
                stop = row
            else:
                raise ValueError('unknown action record')
        except (ValueError, TypeError) as error:
            errors.append(f'{label}: {error}')
    parse_errors = list(errors[parse_start:])
    fail(stop is None, 'missing STOP'); fail(len(rows)%3 != 0, 'incomplete event')
    if stop is not None:
        fail(stop['schema'] != 1 or stop['capacity'] != 2048 or stop['selected'] != len(expected), 'unsupported bounds')
        fail(stop['reserved'] != stop['complete'] or stop['complete']*3 != len(rows) or stop['rows'] != len(rows), 'incomplete publication/export')
        fail(any(stop[k] for k in ('failures', 'dropped', 'unknown', 'threads', 'open', 'restore_open', 'value_coverage', 'chance_coverage', 'exception_cleanup')) or stop['closed'] != 1, 'failed/incomplete observer coverage')
        fail(sum(stop[k] for k in ('reserved', 'other_owner', 'other_script', 'other_context', 'other_phase', 'unknown', 'threads')) != stop['calls'] or not stop['peak'], 'inconsistent admission accounting')
    events = []; hits = set(); boundaries = set(); unknown_rng = 0; thread_ids = set()
    for offset in range(0, len(rows)-2, 3):
        record_start = len(errors)
        e, before, after = rows[offset:offset+3]; ordinal = e['event']; selected = e['selected']
        fail(e['errors'] != 0, f'event {ordinal}: native errors {e["errors"]}')
        fail(e['boot'] != 2 or e['tick'] or e['ui'] != 1 or not e['thread'], f'event {ordinal}: unsupported early thread/phase')
        thread_ids.add(e['thread']); fail(not 1 <= selected <= len(expected), f'event {ordinal}: unknown selection')
        if 1 <= selected <= len(expected):
            s = expected[selected-1]; hits.add(selected)
            fail(e['id'] != int(s['id'], 16) or e['name'] != s['name'] or e['version'] != s['version'], f'event {ordinal}: manifest mismatch')
        fail(not 0 < e['enter'] < e['leave'] or e['enter'] in boundaries or e['leave'] in boundaries, f'event {ordinal}: invalid boundaries')
        boundaries.update((e['enter'], e['leave']))
        fail(not 1 <= e['depth'] <= 64 or e['parent'] >= ordinal, f'event {ordinal}: invalid depth/parent')
        fail(e['pc_before'] != e['slot']+1 or not e['pc_before'], f'event {ordinal}: invalid native program slot')
        fail(e['stage'] not in (0, 1, 2, 3) or (e['stage'] in STAGES and (e['type'], e['action']) != STAGES[e['stage']]) or
             (e['stage'] == 0 and e['type'] in {s[0] for s in STAGES.values()}), f'event {ordinal}: unsupported action class/target')
        fail(e['type'] > IMAGE_SIZE-32 or e['action'] >= IMAGE_SIZE, f'event {ordinal}: action RVA outside native image')
        fail(any(before[k] != after[k] for k in GUARD), f'event {ordinal}: changed raw lifetime/program/action')
        for phase in (before, after):
            fail(any(phase[k] != e[k] for k in ('id', 'name', 'version', 'slot', 'program_hash', 'chance')), f'event {ordinal}: metadata disagreement')
            fail(phase['tag'] != 0x66 or phase['frame'] != phase['exec']+0x38 or phase['current'] != phase['exec'] or
                 not all(phase[k] for k in ('node', 'exec', 'context', 'lifetime', 'entity', 'runtime', 'script', 'program', 'begin', 'table')), f'event {ordinal}: invalid identity')
            fail(phase['end'] <= phase['begin'] or (phase['end']-phase['begin'])%8 or not e['slot'] < (phase['end']-phase['begin'])//8 <= 4096, f'event {ordinal}: invalid vector bound')
            base = phase['vtable']-e['type']
            fail(base != phase['target']-e['action'] or base < 0x10000 or base%0x10000 or base+IMAGE_SIZE > 0xffffffffffffffff,
                 f'event {ordinal}: inconsistent module RVAs')
            fail(any(phase[k] < 0x10000 for k in ('node','exec','frame','current','context','entity','script','program','begin','end','vtable','target','table')),
                 f'event {ordinal}: invalid native pointer range')
            fail(phase['phase_status'] != 1 or phase['eligible'] != 1 or phase['elapsed'] > 120000 or phase['view'] != 1 or phase['main'] or
                 (phase['app_flags'], phase['vbi_loading']) not in ((0x26, 1), (0x20, 0)), f'event {ordinal}: unsupported pre-admission context')
            fail(phase['tls_time'] != phase['game'] or phase['game'] != phase['saved_time'] or
                 any(not math.isfinite(struct.unpack('<d', phase[k].to_bytes(8, 'little'))[0]) for k in ('tls_time', 'game', 'real', 'saved_time')), f'event {ordinal}: invalid clock sample')
        fail(any(before[k] != after[k] for k in ('app_flags', 'vbi_loading', 'tls_time', 'game', 'saved_time')), f'event {ordinal}: changed native phase')
        unknown_rng += e['stage'] == 0 and e['rng_before'] != e['rng_after']
        events.append({'event': e, 'before': before, 'after': after,
                       'validation_errors': parse_errors + errors[record_start:]})
    fail(hits != set(range(1, len(expected)+1)), 'selected script coverage missing')
    fail(len(thread_ids) > 1, 'multiple selected native UI threads')
    for record in events:
        record_start = len(errors)
        e = record['event']; children = [r['event'] for r in events if r['event']['parent'] == e['event']]
        fail(len(children) != e['children'] or any(c['depth'] != e['depth']+1 or not e['enter'] < c['enter'] < c['leave'] < e['leave'] for c in children), f'event {e["event"]}: incomplete nested relation')
        record['validation_errors'].extend(errors[record_start:])
    fail(stop is not None and stop['unknown_rng'] != unknown_rng, 'inconsistent unknown RNG count')
    return events, {'stop': stop, 'selected_hits': sorted(hits), 'rng_changing_action_classes_known': not unknown_rng,
                    'unobserved_nested_calls': sum(r['event']['unobserved_children'] for r in events),
                    'evaluated_values_observed': False, 'pre_dispatch_chance_observed': False}


def semantic_sequences(events):
    result = {}; keys = {}
    for r in events:
        e = r['event']; owner = f'{e["id"]:016x}'; sequence = result.setdefault(owner, [])
        key = (owner, len(sequence)); keys[e['event']] = key
        sample = {k: e[k] for k in ('name', 'version', 'slot', 'stage', 'type', 'action', 'program_hash', 'chance', 'rng_before', 'rng_after', 'pc_before', 'pc_after', 'flags_before', 'flags_after', 'children', 'unobserved_children')}
        sample['parent'] = keys.get(e['parent']) if e['parent'] else None
        for suffix in ('before', 'after'):
            sample[suffix] = {k: r[suffix][k] for k in ('app_flags', 'vbi_loading', 'tls_time', 'game', 'saved_time')}
        sequence.append(sample)
    return result


def paired_diagnostics(captures, claimed_pass, errors):
    if not captures:
        return None
    ordered = sorted(captures, key=lambda c: c['slot']); paired = len(ordered) == 2 and {c['slot'] for c in ordered} == {1, 2}
    same = paired and ordered[0]['expected'] == ordered[1]['expected']
    if not same:
        errors.append('selected early Action pair/manifest missing or different')
    valid = same and not any(c['errors'] for c in ordered); differences = []
    if valid:
        left, right = (semantic_sequences(c['events']) for c in ordered)
        for owner in sorted(left.keys() | right.keys()):
            a, b = left.get(owner, []), right.get(owner, [])
            for index in range(max(len(a), len(b))):
                if index >= len(a) or index >= len(b):
                    differences.append(dict(id=owner, ordinal=index+1, missing='left' if index >= len(a) else 'right'))
                elif a[index] != b[index]:
                    differences.append(dict(id=owner, ordinal=index+1, fields={k: [a[index][k], b[index][k]] for k in a[index] if a[index][k] != b[index][k]}))
    if claimed_pass and (not valid or differences):
        errors.append('selected early Action capture incomplete or input/output traces differ')
    return dict(capture_valid=valid, traces_equal=not differences if valid else None, differences=differences,
                key_scope='per-saved-owner execution sequence; program hashes and action RVAs are Windows-local',
                evaluated_values_observed=False, pre_dispatch_chance_observed=False, exception_cleanup=False,
                clients=[dict(slot=c['slot'], coverage=c['coverage'], errors=c['errors'], sequences=semantic_sequences(c['events'])) for c in ordered])


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('left', type=Path); p.add_argument('right', type=Path)
    p.add_argument('--manifest', type=Path, required=True); p.add_argument('--out', type=Path, required=True); args = p.parse_args()
    expected = selections(json.loads(args.manifest.read_text())['entries']); captures = []; errors = []
    for slot, path in enumerate((args.left, args.right), 1):
        local = []; events, coverage = evidence(path.read_text(encoding='utf-8', errors='replace'), expected, local, str(slot))
        captures.append(dict(slot=slot, expected=expected, events=events, coverage=coverage, errors=local)); errors.extend(local)
    result = paired_diagnostics(captures, False, errors); result['errors'] = errors
    args.out.write_text(json.dumps(result, indent=2)+'\n'); print(json.dumps({'errors': errors, 'differences': len(result['differences'])})); return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
