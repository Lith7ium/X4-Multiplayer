"""Independent selected Wait import evidence reader. No engine calls or correction."""
import argparse
import json
import math
from pathlib import Path
import re
import struct
from native_gate_wait_manifest import selected_time_bits

PARTS = [set(s.split()) for s in (
    'event part selected errors enter leave result tick boot thread ui caller id name version since xml_index index_status slot pc time expected_time minimum maximum rng_before rng_after due_before due_after flags_before flags_after reason_before reason_after',
    'event part self exec value context context_vtable lifetime entity runtime script program begin end vtable blocking listeners timeout expression phase_status eligible elapsed app_flags vbi_loading view main tls_time game real saved_time table',
    'event part self exec value context context_vtable lifetime entity runtime script program begin end vtable blocking listeners timeout expression phase_status eligible elapsed app_flags vbi_loading view main tls_time game real saved_time table',
    'event part id name version since slot pc time tag',
)]
STOP = set('schema capacity selected reserved complete rows failures dropped calls import_calls foreign_calls unknown installed restored open peak restore_open index_coverage'.split())
HEX = set('caller id time expected_time minimum maximum rng_before rng_after due_before due_after flags_before flags_after reason_before reason_after self exec value context context_vtable lifetime entity runtime script program begin end vtable listeners app_flags tls_time game real saved_time table tag'.split())
RAW = {'timeout': 16, 'expression': 104}


def rng_step(value):
    value = (value * 0x5851f42d4c957f2d + 0x14057b7ef767814f) & ((1 << 64) - 1)
    return ((value >> 30) | (value << 34)) & ((1 << 64) - 1)


def number(bits):
    return struct.unpack('<d', int(bits).to_bytes(8, 'little'))[0]


def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def constant_value(raw, offset):
    tag = int.from_bytes(raw[offset:offset+4], 'little')
    if tag > 9:
        raise ValueError('nonconstant expression value')
    fmt = '<i' if tag <= 1 else '<q' if tag <= 3 else '<f' if tag <= 6 else '<d'
    return float(struct.unpack_from(fmt, raw, offset+8)[0])


def configured_waits(config):
    values = re.findall(r'^X4LAN_CONFIG\.native_gate_world_wait_manifest\s*=\s*("(?:\\.|[^"\\])*")\s*$', config, re.M)
    if len(values) != 1:
        raise ValueError('missing or duplicated wait manifest configuration')
    wire = json.loads(values[0])
    if not 0 < len(wire) < 2048:
        raise ValueError('invalid wait manifest bound')
    result = []; seen = set()
    for item in wire.split(';'):
        fields = item.split(',')
        if len(fields) != 7:
            raise ValueError('invalid wait manifest record')
        identity, version, index, time, minimum, maximum, name = fields
        if not all(re.fullmatch('[0-9a-f]{16}', s) for s in (identity, time, minimum, maximum)) or not int(identity, 16):
            raise ValueError('invalid wait manifest identity/bits')
        if not re.fullmatch('[0-9]+', version) or int(version)>0x7fffffff or not re.fullmatch('[0-9]+', index) or int(index)>65535 or not re.fullmatch('[A-Za-z0-9_.]{1,95}', name):
            raise ValueError('invalid wait manifest selector')
        t, low, high = (number(int(s, 16)) for s in (time, minimum, maximum))
        if not all(math.isfinite(n) for n in (t, low, high)) or t<0 or low<0 or high<=low or (identity, name) in seen:
            raise ValueError('invalid wait manifest values')
        seen.add((identity, name));result.append(dict(id=identity, name=name, version=int(version), index=int(index), time=t, minimum=low, maximum=high))
    if not 1<=len(result)<=8:
        raise ValueError('invalid wait manifest count')
    return result


def fields(text):
    result = {}
    for word in text.split():
        key, value = word.split('=', 1)
        if key in result:
            raise ValueError('duplicate field')
        if key in RAW:
            if not re.fullmatch('[0-9a-f]{'+str(RAW[key]*2)+'}', value):
                raise ValueError('invalid raw bytes')
            value = bytes.fromhex(value)
        elif key == 'name':
            if not re.fullmatch(r'[A-Za-z0-9_.-]{1,95}', value):
                raise ValueError('invalid script name')
        else:
            base = 16 if key in HEX else 10
            if not re.fullmatch('[0-9a-f]+' if base == 16 else '[0-9]+', value):
                raise ValueError('invalid integer')
            value = int(value, base)
            if value > (1 << 64) - 1:
                raise ValueError('integer exceeds uint64')
        result[key] = value
    return result


def evidence(text, expected, errors, label='wait', rng_configured=False, rng_policies=None):
    if rng_policies is not None and (not rng_configured or len(rng_policies)!=len(expected) or
        any(policy not in ('intervene','observe') for policy in rng_policies)):
        raise ValueError('Wait evidence policy does not match selected manifest')
    rows = []; stop = None; parse_start = len(errors)
    for line in text.splitlines():
        if 'NATIVE_GATE WORLD_WAIT_' not in line:
            continue
        kind, _, body = line.split('NATIVE_GATE WORLD_WAIT_', 1)[1].partition(' ')
        if rng_configured and kind in ('RNG','RNG_STOP'):
            continue  # Independent opt-in RNG reader must validate these rows.
        try:
            row = fields(body)
            if kind == 'EVENT':
                part = row.get('part', -1)
                if part not in range(4) or set(row) != PARTS[part]:
                    raise ValueError('invalid event schema')
                if stop is not None:
                    raise ValueError('row after STOP')
                if row['event'] != len(rows)//4+1 or part != len(rows)%4 or len(rows) >= 128*4:
                    raise ValueError('missing, reordered or excessive event row')
                rows.append(row)
            elif kind == 'STOP':
                if stop is not None or set(row) != STOP:
                    raise ValueError('duplicate or invalid STOP')
                stop = row
            else:
                raise ValueError('unknown wait record')
        except (ValueError, TypeError) as error:
            errors.append(f'{label}: {error}')
    parse_errors = list(errors[parse_start:])
    def fail(condition, message):
        if condition:
            errors.append(f'{label}: {message}')
    fail(stop is None, 'missing STOP')
    fail(len(rows)%4 != 0, 'incomplete event')
    if stop is not None:
        fail(stop['schema'] != 1 or stop['capacity'] != 128 or stop['selected'] != len(expected) or stop['index_coverage'] != 0, 'unsupported STOP bounds')
        fail(stop['reserved'] != stop['complete'] or stop['complete']*4 != len(rows) or stop['rows'] != len(rows), 'incomplete publication/export')
        fail(any(stop[k] for k in ('failures', 'dropped', 'unknown', 'installed', 'open', 'restore_open')) or stop['restored'] != 1, 'failed or unrestored observer coverage')
        fail(stop['import_calls']+stop['foreign_calls'] != stop['calls'] or stop['complete'] > stop['import_calls'] or not stop['peak'], 'inconsistent call accounting')
    events = []; seen = {}; boundaries = set()
    for offset in range(0, len(rows)-3, 4):
        record_start = len(errors)
        event, before, after, final = rows[offset:offset+4]
        selected = event['selected']; ordinal = event['event']
        fail(event['errors'] != 0, f'event {ordinal}: native errors {event["errors"]}')
        fail(event['caller'] != 0x1069bfc or event['boot'] != 2 or event['tick'] != 0 or event['result'] != 1, f'event {ordinal}: unverified importer/result')
        fail(not 1 <= selected <= len(expected), f'event {ordinal}: unknown selection')
        fail(not 0 < event['enter'] < event['leave'] or event['enter'] in boundaries or event['leave'] in boundaries, f'event {ordinal}: invalid causal boundaries')
        boundaries.update((event['enter'], event['leave']))
        if 1 <= selected <= len(expected):
            saved = expected[selected-1];seen[selected] = seen.get(selected, 0)+1
            try:
                expected_time = int(selected_time_bits(saved), 16)
            except (ValueError, TypeError, KeyError) as error:
                errors.append(f'{label}: invalid manifest saved time: {error}')
                expected_time = None
            fail(event['id'] != int(saved['id'], 16) or event['name'] != saved['name'] or event['version'] != saved['version'] or
                 event['xml_index'] != saved['index'] or event['time'] != expected_time or event['expected_time'] != expected_time or
                 event['minimum'] != bits(saved['minimum']) or event['maximum'] != bits(saved['maximum']), f'event {ordinal}: manifest mismatch')
        fail(event['index_status'] != 0, f'event {ordinal}: unsupported ordinal claim')
        for key in ('id', 'name', 'version', 'since', 'slot', 'pc', 'time'):
            fail(event[key] != final[key], f'event {ordinal}: changed {key}')
        fail(final['tag'] != 0x66, f'event {ordinal}: changed Value tag')
        expression = before['expression']
        fail(struct.unpack_from('<III', expression, 8) != (2, 1, 1) or
             struct.unpack_from('<I', expression, 0x38)[0] or struct.unpack_from('<I', expression, 0x48)[0] or
             struct.unpack_from('<I', before['timeout'])[0] or before['listeners'], f'event {ordinal}: unsupported expression or auxiliary action')
        try:
            fail(bits(constant_value(expression, 0x18)) != event['minimum'] or bits(constant_value(expression, 0x28)) != event['maximum'], f'event {ordinal}: raw range mismatch')
        except ValueError as error:
            errors.append(f'{label}: event {ordinal}: {error}')
        fail(event['since'] > event['version'], f'event {ordinal}: unsupported script version')
        local = set('self exec value context context_vtable lifetime entity runtime script program begin end vtable blocking listeners timeout expression'.split())
        fail(any(before[k] != after[k] for k in local), f'event {ordinal}: changed raw identity/input')
        for phase in (before, after):
            fail(phase['phase_status'] != 1 or phase['eligible'] != 1 or phase['elapsed'] > 120000 or phase['vbi_loading'] not in (0, 1) or phase['view'] not in (0, 1), f'event {ordinal}: unsupported pre-admission context')
            fail(any(not math.isfinite(number(phase[k])) for k in ('tls_time', 'game', 'real', 'saved_time')), f'event {ordinal}: nonfinite clock')
            fail(phase['value'] != phase['exec']+0x38 or not all(phase[k] for k in ('self', 'context', 'lifetime', 'entity', 'runtime', 'script', 'program', 'begin')), f'event {ordinal}: invalid local identity')
            fail(phase['blocking'] != 1 or phase['end'] <= phase['begin'] or (phase['end']-phase['begin'])%8 or event['slot'] >= (phase['end']-phase['begin'])//8, f'event {ordinal}: invalid blocked program slot')
        fail(any(not math.isfinite(number(event[k])) for k in ('due_before', 'due_after')), f'event {ordinal}: nonfinite deadline')
        # This is a bounded input/output observation, not proof that no nested
        # RNG consumer exists. It identifies consistency with exactly one step.
        interpreted = {k: event[k] for k in ('id', 'name', 'version', 'xml_index', 'slot', 'pc', 'time', 'rng_before', 'rng_after', 'due_before', 'due_after', 'flags_before', 'flags_after', 'reason_before', 'reason_after')}
        scoped = rng_configured and (rng_policies is None or
            (1 <= selected <= len(rng_policies) and rng_policies[selected-1]=='intervene'))
        interpreted['rng_interpretation'] = 'scoped_experiment_separate_evidence' if scoped else 'natural'
        interpreted['single_rng_step_matches'] = None if scoped else rng_step(event['rng_before']) == event['rng_after']
        interpreted['deadline_minus_saved_start'] = number(event['due_after']) - number(event['time'])
        interpreted['deadline_changed'] = event['due_before'] != event['due_after']
        for suffix, phase in (('before', before), ('after', after)):
            for field in ('tls_time', 'game', 'saved_time', 'app_flags', 'vbi_loading', 'view'):
                interpreted[field+'_'+suffix] = phase[field]
        events.append({'input_output': interpreted, 'raw': [event, before, after, final],
                       'validation_errors': parse_errors + errors[record_start:]})
    fail(seen != {i+1: 1 for i in range(len(expected))}, 'selected saved wait coverage missing or duplicated')
    return events, {'stop': stop, 'selected_hits': seen, 'xml_ordinal_directly_observed': False}


def compare(left, right):
    a = {(e['input_output']['id'], e['input_output']['name']): e['input_output'] for e in left}
    b = {(e['input_output']['id'], e['input_output']['name']): e['input_output'] for e in right}
    result = []
    for key in sorted(a.keys() | b.keys()):
        if key not in a or key not in b:
            result.append({'id': f'{key[0]:016x}', 'name': key[1], 'missing': 'left' if key not in a else 'right'})
        else:
            for field in a[key]:
                if a[key][field] != b[key][field]:
                    result.append({'id': f'{key[0]:016x}', 'name': key[1], 'field': field, 'left': a[key][field], 'right': b[key][field]})
    return result


def paired_diagnostics(captures, claimed_pass, errors):
    """Keep capture validity separate from observed cross-client divergence."""
    if not captures:
        return None
    paired = len(captures) == 2 and {c['slot'] for c in captures} == {1, 2}
    if not paired:
        errors.append('selected Wait capture/configuration missing for one player')
    ordered = sorted(captures, key=lambda c: c['slot'])
    same_manifest = paired and ordered[0]['expected'] == ordered[1]['expected']
    if paired and not same_manifest:
        errors.append('selected Wait manifests differ between players')
    valid = same_manifest and not any(c['errors'] for c in ordered)
    differences = compare(ordered[0]['events'], ordered[1]['events']) if valid else []
    equal = not differences if valid else None
    if claimed_pass and equal is False:
        errors.append('selected saved Wait restoration inputs/deadlines differ between players')
    return {'capture_valid': valid, 'semantics_equal': equal, 'differences': differences,
            'xml_ordinal_directly_observed': False,
            'clients': [{'slot': c['slot'], 'coverage': c['coverage'], 'errors': c['errors'],
                         'samples': [e['input_output'] for e in c['events']]} for c in ordered]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path); parser.add_argument('right', type=Path)
    parser.add_argument('--manifest', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--rng-manifest',type=Path,help='separate opt-in scoped RNG experiment manifest')
    args = parser.parse_args(); document=json.loads(args.manifest.read_text());expected = document['entries']; errors = []; samples = []; coverage = [];rng_captures=[]
    rng_manifest=None
    if args.rng_manifest:
        from native_world_wait_rng import manifest_wire,evidence as rng_evidence,paired_diagnostics,policies
        rng_manifest=json.loads(args.rng_manifest.read_text());manifest_wire(rng_manifest,document)
    for slot,path in enumerate((args.left, args.right),1):
        text=path.read_text(encoding='utf-8', errors='replace');local_errors=[]
        events, status = evidence(text, expected, local_errors, path.parent.name,rng_configured=rng_manifest is not None,rng_policies=policies(rng_manifest) if rng_manifest else None)
        if rng_manifest:
            result=rng_evidence(text,rng_manifest,events,local_errors,path.parent.name+' Wait RNG')
            rng_captures.append(dict(slot=slot,manifest=rng_manifest,evidence=result,errors=local_errors))
        errors.extend(local_errors)
        samples.append(events); coverage.append(status)
    rng_result=paired_diagnostics(rng_captures,False,errors) if rng_manifest else None
    result = {'scope': 'selected exact saved Wait importer calls; no deadline writes; RNG is natural unless the separate scoped experiment is configured; XML ordinal is manifest metadata, native program slot is observed; native flags do not prove inside Load',
              'errors': errors, 'coverage': coverage, 'samples': samples, 'differences': compare(*samples) if not errors else [],'rng_experiment':rng_result}
    args.out.write_text(json.dumps(result, indent=2, default=lambda value: value.hex() if isinstance(value, bytes) else str(value))+'\n')
    print(json.dumps({'errors': errors, 'differences': len(result['differences']), 'out': str(args.out)}))
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
