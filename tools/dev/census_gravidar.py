"""Gravidar contact watch census: join GRAVIDAR_WATCH rows of two (or more) clients.

native_gate_gravidar_watch (lockstep_gravidar.h) records, for each keyed apply of
a watched saved unit inside a gated Gravidar frame, the unit's contact sets:

  0 old primary   1 old secondary   2 scan primary    3 scan secondary  (before)
  4 applied prim. 5 applied second. 6 added primary   7 added secondary (after)

Contacts are portable keys (s:saved, c:created) or u:runtime for objects with
neither (temporary components, mass traffic); u: IDs are local to a client and
are compared by count only. Rows join by (tick, unit, ordinal). The first row
whose sets differ names the contact one client saw and the other did not: the
input of the found/scanned events behind the class-B divergence (sc-missions,
tick 644, unit 9fb3b) and the move.seekenemies interrupt (unit 9f121, 328/420).

  python tools/dev/census_gravidar.py <run>                 (client1..N/engine-output.log)
  python tools/dev/census_gravidar.py --logs a.log b.log [--out census.json]
"""
import argparse
import json
from pathlib import Path
import re
import sys

SETS = ('old_primary', 'old_secondary', 'scan_primary', 'scan_secondary',
        'applied_primary', 'applied_secondary', 'added_primary', 'added_secondary')
ROW = re.compile(r'NATIVE_GATE GRAVIDAR_WATCH_ROW index=(\d+) (?:evicted=1|tick=(\d+) unit=([0-9a-f]{16}) ord=(\d+) ok=(\d) counts=([\d/]+))')
SET = re.compile(r'NATIVE_GATE GRAVIDAR_WATCH_SET index=(\d+) set=(\d) present=(\d) count=(\d+) keys=(\S*)')
STATUS = re.compile(r'NATIVE_GATE GRAVIDAR_WATCH_STOP (.*)$')


def parse(lines):
    """Rows of one client: {(tick, unit, ord): {'ok', 'counts', 'sets': {set: (keys, unmapped)}}}."""
    by_index, rows, evicted, status = {}, {}, 0, None
    for line in lines:
        m = ROW.search(line)
        if m:
            if m.group(2) is None:
                evicted += 1
                continue
            key = (int(m.group(2)), m.group(3), int(m.group(4)))
            row = {'ok': int(m.group(5)), 'counts': [int(c) for c in m.group(6).split('/')], 'sets': {}}
            if len(row['counts']) != 8:
                raise ValueError(f'malformed Gravidar watch row: {line.strip()}')
            by_index[int(m.group(1))] = row
            rows[key] = row
            continue
        m = SET.search(line)
        if m:
            row = by_index.get(int(m.group(1)))
            if row is None:
                raise ValueError(f'Gravidar watch set without its row: {line.strip()}')
            keys = [k for k in m.group(5).split(',') if k]
            if len(keys) != int(m.group(4)) or any(not re.fullmatch(r'[scu]:[0-9a-f]{16}', k) for k in keys):
                raise ValueError(f'malformed Gravidar watch set: {line.strip()}')
            portable = tuple(sorted(k for k in keys if not k.startswith('u:')))
            row['sets'][int(m.group(2))] = (portable, len(keys) - len(portable))
            continue
        m = STATUS.search(line)
        if m:
            status = m.group(1)
    for row in rows.values():
        for set_index, count in enumerate(row['counts']):
            portable, unmapped = row['sets'].setdefault(set_index, ((), 0))
            if len(portable) + unmapped != count:
                raise ValueError('Gravidar watch row and set counts disagree')
    return {'rows': rows, 'evicted': evicted, 'status': status}


def set_difference(a, b):
    out = {}
    for index, name in enumerate(SETS):
        (pa, ua), (pb, ub) = a['sets'][index], b['sets'][index]
        if pa != pb or ua != ub:
            out[name] = {'only_reference': sorted(set(pa) - set(pb)), 'only_other': sorted(set(pb) - set(pa)), 'unmapped': [ua, ub]}
    return out


def compare(clients, limit=20):
    reference = clients[0]['rows']
    result = {'rows': [len(c['rows']) for c in clients], 'evicted': [c['evicted'] for c in clients],
              'status': [c['status'] for c in clients], 'pairs': []}
    for number, other in enumerate(clients[1:], 2):
        rows = other['rows']
        joined = sorted(set(reference) & set(rows))
        only = [sorted(set(reference) - set(rows)), sorted(set(rows) - set(reference))]
        mismatches = []
        for key in joined:
            diff = set_difference(reference[key], rows[key])
            if diff or reference[key]['ok'] != rows[key]['ok']:
                mismatches.append({'tick': key[0], 'unit': key[1], 'ord': key[2], 'ok': [reference[key]['ok'], rows[key]['ok']], 'sets': diff})
        first_only = min((k for side in only for k in side), default=None)
        result['pairs'].append({
            'client': number, 'joined': len(joined), 'mismatches': len(mismatches),
            'only': [len(only[0]), len(only[1])],
            'first_mismatch': mismatches[0] if mismatches else None,
            'first_only': list(first_only) if first_only else None,
            'mismatch_examples': mismatches[:limit],
            'only_examples': [[list(k) for k in side[:limit]] for side in only],
        })
    return result


def client_logs(run):
    logs = sorted(Path(run).glob('client*/engine-output.log'))
    if len(logs) < 2:
        raise ValueError(f'{run}: needs client1..N/engine-output.log')
    return logs


def read(path):
    with open(path, encoding='utf-8', errors='replace') as handle:
        return parse(line for line in handle if 'GRAVIDAR_WATCH' in line)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path, nargs='?')
    p.add_argument('--logs', type=Path, nargs='+')
    p.add_argument('--limit', type=int, default=20)
    p.add_argument('--out', type=Path)
    args = p.parse_args(argv)
    try:
        logs = args.logs or client_logs(args.run)
        if len(logs) < 2:
            raise ValueError('needs at least two client logs')
        result = compare([read(path) for path in logs], args.limit)
    except (ValueError, OSError) as error:
        p.error(str(error))
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
