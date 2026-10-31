"""Describe captured native requests without granting a pilot lease or a pass.

Raw differences, including runtime references and unclassified bytes, are retained.
Empty queues and idle model fields describe two captured boundaries only. They do
not exclude detached commands, native producers or nonzero native control input.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct

from native_request_evidence import evidence, REQUEST_SIZES, SCOPE_SIZES

IDLE_MODEL = bytes.fromhex('00000000010000000100000001000000')
VELOCITY_SOLVER = bytes.fromhex('0001000000000000010100000100000001000000')
UNIT_GAINS = struct.pack('<6d', *([1.0] * 6))
BYTE_FIELDS = set(REQUEST_SIZES) | set(SCOPE_SIZES) | {'main_data', 'pending_data'}
CHANNELS = ('angular0', 'angular1', 'angular2', 'linear0', 'linear1', 'linear2')


def indexed(trace):
    result = []
    for kind, rows in enumerate(trace):
        indexed_rows = {}
        for pairs in rows:
            row = dict(pairs)
            key = (row['tick'], row['owner'],
                   row['agent_ordinal'] if kind == 0 else row['ordinal'],
                   row['ordinal'] if kind == 0 else row['phase'])
            if key in indexed_rows:
                raise ValueError('duplicate request diagnostic identity')
            indexed_rows[key] = row
        result.append(indexed_rows)
    return result


def compare(left, right):
    differences = []
    fields = Counter()
    offsets = defaultdict(Counter)
    for kind, (a, b) in enumerate(zip(left, right)):
        name = ('request', 'scope')[kind]
        for key in sorted(a.keys() | b.keys()):
            if key not in a or key not in b:
                differences.append(dict(kind=name, key=key, field='record',
                                        missing_client=1 if key not in a else 2))
                fields[name + '.record'] += 1
                continue
            for field in sorted(a[key].keys() | b[key].keys()):
                if field == 'index' or a[key].get(field) == b[key].get(field):
                    continue
                label = name + '.' + field
                fields[label] += 1
                diff = dict(kind=name, key=key, field=field)
                if field in BYTE_FIELDS and field in a[key] and field in b[key]:
                    x, y = bytes.fromhex(a[key][field]), bytes.fromhex(b[key][field])
                    diff['sizes'] = [len(x), len(y)]
                    changed = [i for i in range(max(len(x), len(y)))
                               if x[i:i+1] != y[i:i+1]]
                    offsets[label].update(changed)
                    diff['bytes'] = [dict(offset=hex(i), client1=x[i:i+1].hex(),
                                          client2=y[i:i+1].hex()) for i in changed]
                else:
                    diff.update(client1=a[key].get(field), client2=b[key].get(field))
                differences.append(diff)
    differences.sort(key=lambda d: (d['key'], d['kind'], d['field']))
    return dict(raw_equal=not differences, differing_fields=dict(fields),
                byte_offsets={name: {hex(i): n for i, n in sorted(counts.items())}
                              for name, counts in sorted(offsets.items())},
                first_difference=differences[0] if differences else None,
                raw_differences=differences)


def tick_ranges(ticks):
    result = []
    for tick in sorted(set(ticks)):
        if result and tick == result[-1][1] + 1:
            result[-1][1] = tick
        else:
            result.append([tick, tick])
    return result


def idle_empty_boundaries(rows):
    events, scopes = rows
    owners = defaultdict(lambda: dict(ticks=[], neutral_scopes=0, axes=[[] for _ in range(6)]))
    for key, before in sorted(scopes.items()):
        tick, owner, ordinal, phase = key
        if phase != 0:
            continue
        after = scopes.get((tick, owner, ordinal, 1))
        requests = [events.get((tick, owner, ordinal, i)) for i in range(1, 7)]
        if not after or not all(requests):
            continue
        if not all(s['requests'] == 6 and s['other_requests'] == 0 and
                   s['main'] == s['pending'] == 0 and
                   all(s[k] == 1 for k in ('model_present', 'solver_present',
                                           'model_same', 'solver_same')) and
                   s['model_rva'] == '02c4e9c8' and
                   bytes.fromhex(s['model']) == IDLE_MODEL for s in (before, after)):
            continue
        if not all(bytes.fromhex(r['model']) == IDLE_MODEL and
                   bytes.fromhex(r['solver']) == VELOCITY_SOLVER and
                   bytes.fromhex(r['gains']) == UNIT_GAINS for r in requests):
            continue
        observed = owners[owner]
        observed['actor'] = before['actor']
        observed['ticks'].append(tick)
        request_values = [struct.unpack('>d', bytes.fromhex(r['value']))[0] for r in requests]
        observed['neutral_scopes'] += all(v == 0 for v in request_values)
        for values, value in zip(observed['axes'], request_values):
            values.append(value)
    result = []
    for owner, observed in sorted(owners.items()):
        result.append(dict(owner=owner, actor=observed['actor'],
                           ticks=sorted(set(observed['ticks'])),
                           tick_ranges=tick_ranges(observed['ticks']),
                           scopes=len(observed['ticks']),
                           all_six_requests_neutral_scopes=observed['neutral_scopes'],
                           channels={name: dict(nonzero=sum(v != 0 for v in values),
                                                minimum=min(values), maximum=max(values))
                                     for name, values in zip(CHANNELS, observed['axes'])}))
    return result


def analyze(log, slot):
    stops = re.findall(r'NATIVE_GATE STOP .*?completed=(\d+)', log)
    if len(stops) != 1:
        raise ValueError('requires one completed native stop')
    errors = []
    trace, summary = evidence(log, slot, int(stops[0]), errors)
    rows = indexed(trace)
    return rows, dict(structural_errors=errors, evidence=summary,
                     idle_empty_boundary_observations=[] if errors else idle_empty_boundaries(rows),
                     changed_target_history_requests=sum(r['before'] != r['after'] for r in rows[0].values()),
                     changed_dirty_requests=sum(r['dirty0'] != r['dirty1'] for r in rows[0].values()),
                     pilot_ownership_proved=False, producer_quiescence_proved=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    rows, summaries = [], []
    for slot in (1, 2):
        path = args.run / f'client{slot}/x4lan-debug.txt'
        size = path.stat().st_size
        with path.open('rb') as stream:
            indexed_rows, summary = analyze(stream.read(size).decode(errors='replace'), slot)
        rows.append(indexed_rows)
        summaries.append(summary)
    result = dict(schema=1, diagnostic_only=True, strict_validation_unchanged=True,
                  clients=summaries, comparison=compare(*rows))
    result['boundary_observations_equal'] = (not any(c['structural_errors'] for c in summaries) and
        summaries[0]['idle_empty_boundary_observations'] == summaries[1]['idle_empty_boundary_observations'])
    output = args.run / 'request-analysis.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({**result, 'comparison': {k: v for k, v in result['comparison'].items()
                                             if k != 'raw_differences'}}, indent=2))


if __name__ == '__main__':
    main()
