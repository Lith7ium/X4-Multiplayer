"""Read-only request/publication provenance from existing native solver captures.

This does not establish player authority or alter session validation. FLIGHT
blocks contain printed uint32 words; PLAYER_SOLVER_EVENT blocks contain bytes.
Converting that distinction explicitly is necessary before comparing them.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct

from analyze_native_solver import records


def word_bytes(value, size):
    if len(value) != size * 2 or len(value) % 8:
        raise ValueError('invalid FLIGHT word block length')
    return b''.join(struct.pack('<I', int(value[i:i+8], 16))
                    for i in range(0, len(value), 8))


def analyze(log):
    flights = {}
    for text in re.findall(r'NATIVE_GATE FLIGHT ([^\r\n]*)', log):
        row = dict(part.split('=', 1) for part in text.split())
        if int(row['calls']) == 0:
            continue
        key = int(row['tick']), int(row['id'])
        if key in flights:
            raise ValueError('duplicate observed FLIGHT tick/actor')
        flights[key] = row

    counts = Counter()
    modes = Counter()
    behaviors = Counter()
    owners = {}
    failures = []
    for key, row in sorted(records(log).items()):
        tick, actor, kind, ordinal, _, _ = key
        if actor == 0 or kind != 1:
            continue
        counts['fixture_solves'] += 1
        flight = flights.get((tick, actor))
        if not flight or int(flight['calls']) != 1 or ordinal != 1:
            failures.append({'tick': tick, 'actor': actor,
                             'reason': 'missing or non-unique steering invocation'})
            continue
        before = word_bytes(flight['input'], 0x1a0)
        after = word_bytes(flight['after'], 96)
        model = word_bytes(flight['model'], 0x3a0)
        c0, m0, m1 = (bytes.fromhex(row[field]) for field in ('c0', 'm0', 'm1'))
        if (len(c0), len(m0), len(m1)) != (504, 160, 160):
            raise ValueError('invalid solver block length')
        if actor in owners and owners[actor] != row['owner']:
            raise ValueError('saved owner changed within capture')
        owners[actor] = row['owner']
        checks = {
            'solver_input_pose_matches_steering_entry': m0[64:128] == before[:64],
            'solver_input_velocity_matches_steering_entry': m0[128:160] == before[64:96],
            'solver_output_matches_steering_exit': m1[64:160] == after,
        }
        for name, matches in checks.items():
            counts[name] += matches
            if not matches:
                failures.append({'tick': tick, 'actor': actor, 'reason': name})
        modes[(c0[0x1c], c0[0x22], c0[0x23], c0[0x24], c0[0x31],
               struct.unpack_from('<I', c0, 0x48)[0],
               struct.unpack_from('<I', c0, 0x50)[0])] += 1
        behaviors[(actor, flight['behaviour'],
                   struct.unpack_from('<I', model, 0x68)[0],
                   model[0xa8], struct.unpack_from('<I', model, 0xac)[0],
                   struct.unpack_from('<I', model, 0xb0)[0])] += 1
    return {
        'schema': 1, 'diagnostic_only': True, 'counts': dict(counts),
        'saved_owners': owners, 'failures': failures,
        'observed_motion_provenance_matches': bool(counts['fixture_solves']) and not failures,
        'solver_mode_fields': ['initial_1c', 'boost_22', 'travel_23', 'reverse_24',
                               'linear_skip_31', 'angular_mode_48', 'linear_mode_50'],
        'solver_modes': [{'values': key, 'count': count} for key, count in sorted(modes.items())],
        'model_fields': ['actor', 'behavior_rva', 'order_mode_68', 'solver_branch_a8',
                         'angular_request_mode_ac', 'linear_request_mode_b0'],
        'models': [{'values': key, 'count': count} for key, count in sorted(behaviors.items())],
        'pilot_ownership_proved': False, 'ai_order_suppression_proved': False,
        'strict_validation_unchanged': True,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    args = p.parse_args()
    clients = []
    for client in (1, 2):
        path = args.run / f'client{client}/x4lan-debug.txt'
        size = path.stat().st_size
        with path.open('rb') as stream:
            clients.append(analyze(stream.read(size).decode(errors='replace')))
    result = {'clients': clients, 'diagnostic_only': True,
              'summaries_equal': clients[0] == clients[1]}
    output = args.run / 'pilot-authority-analysis.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
