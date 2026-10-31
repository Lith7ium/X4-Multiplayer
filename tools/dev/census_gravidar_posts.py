"""Gravidar has_scanned contact changes: join GRAVIDAR_POST_ROW lines of two (or more) clients.

native_gate_gravidar_posts (lockstep_gravidar_posts.h) logs one row per
has_scanned post of a keyed Gravidar unit that added or removed contacts: tick,
unit key, set sizes and the added (plus) and removed (minus) contacts as
portable keys (s:saved, c:created) or u:runtime/class for objects with neither;
u: runtime IDs are local to a client and compare by class only. Rows join by
(tick, unit). A row present on one client only, or with other contacts, is a
one-sided Gravidar input: the scanned/found events behind the station split of
sc-missions59/63 (fight.attack.object.station's 'Station Attacked' handler).
Rows carry the scan's own inputs (a={...} for the scanning unit, {...} per
contact, z=- for a contact the scan did not gather). A removed contact that the
scan gathered within its bound in a near list of a later zone group than a
wide-list scanner (stations) is a 'late' drop: the scanner was applied before
that group's pair checks (lockstep_gravidar_wide.h); the group order follows
zone addresses, so it differs between clients.

  python tools/dev/census_gravidar_posts.py <run>            (client1..N/x4lan-debug.txt)
  python tools/dev/census_gravidar_posts.py --logs a.log b.log [--out census.json]
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

ROW = re.compile(r'NATIVE_GATE GRAVIDAR_POST_ROW index=(\d+) (?:evicted=1|tick=(\d+) unit=(\S+) sizes=(\d+)/(\d+) '
                 r'added=(\d+) removed=(\d+)(.*))$')
STATUS = re.compile(r'NATIVE_GATE GRAVIDAR_POSTS_STOP (.*)$')
SCANNER_ZONE = re.compile(r'\bz=(\d+)/(\d+)([nw])')
ITEM_INPUTS = re.compile(r'\bd=([0-9a-f]{8}) b=([0-9a-f]{8}).*\bz=(\d+)([nw])')


def items(text):
    """Top-level comma-separated items; an item may carry {scan inputs} with spaces and commas."""
    out, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
        elif ch == ',' and depth == 0:
            out.append(text[start:i])
            start = i + 1
    if text[start:]:
        out.append(text[start:])
    return [x for x in out if x]


def split(rest):
    """' ... plus=A minus=B [edge=C]' -> {'plus': [...], 'minus': [...], 'edge': [...]}, items as (key, inputs)."""
    marks = [(rest.find(' ' + name + '='), name) for name in ('plus', 'minus', 'edge')]
    marks = sorted((i, n) for i, n in marks if i >= 0)
    lists = {'plus': [], 'minus': [], 'edge': []}
    for k, (i, name) in enumerate(marks):
        end = marks[k + 1][0] if k + 1 < len(marks) else len(rest.rstrip())
        for item in items(rest[i + len(name) + 2:end].strip()):
            key, _, inputs = item.partition('{')
            lists[name].append((key, inputs.rstrip('}')))
    head = rest[:marks[0][0]].strip() if marks else rest.strip()
    return lists, head


def contact(text):
    """A portable key as is; an unmapped one as its class only (runtime IDs differ by client)."""
    if text.startswith('u:'):
        return 'u/' + text.split('/', 1)[1] if '/' in text else 'u'
    return text


def bits(text):
    return struct.unpack('>f', bytes.fromhex(text))[0]


def late(head, removed):
    """Removed contacts gathered within their bound in a near list of a group after a wide scanner's group."""
    m = SCANNER_ZONE.search(head)
    if not m or m.group(3) != 'w':
        return []
    out = []
    for key, inputs in removed:
        i = ITEM_INPUTS.search(inputs)
        if i and i.group(4) == 'n' and int(i.group(3)) > int(m.group(1)) and bits(i.group(1)) <= bits(i.group(2)):
            out.append(contact(key))
    return sorted(out)


def parse(lines):
    """Rows of one client: {(tick, unit): [{'sizes', 'added', 'removed', 'plus', 'minus'}...]}."""
    rows, evicted, status, seen = {}, 0, None, set()
    for line in lines:
        m = ROW.search(line)
        if m:
            if m.group(1) in seen:
                continue
            seen.add(m.group(1))
            if m.group(2) is None:
                evicted += 1
                continue
            key = (int(m.group(2)), m.group(3))
            lists, head = split(m.group(8))
            rows.setdefault(key, []).append({
                'sizes': [int(m.group(4)), int(m.group(5))], 'added': int(m.group(6)), 'removed': int(m.group(7)),
                'plus': sorted(contact(k) for k, _ in lists['plus']), 'minus': sorted(contact(k) for k, _ in lists['minus']),
                'scanner': head, 'inputs': {k: v for name in ('plus', 'minus', 'edge') for k, v in lists[name] if v},
                'late': late(head, lists['minus'])})
            continue
        m = STATUS.search(line)
        if m:
            status = m.group(1)
    return {'rows': rows, 'evicted': evicted, 'status': status}


def compare(clients, limit=20):
    keys = sorted(set().union(*(c['rows'] for c in clients)))
    examples, joined, differing = [], 0, 0
    for key in keys:
        present = [key in c['rows'] for c in clients]
        if all(present):
            joined += 1
            views = [[(r['added'], r['removed'], r['plus'], r['minus']) for r in c['rows'][key]] for c in clients]
            if all(v == views[0] for v in views):
                continue
            differing += 1
        if len(examples) < limit:
            examples.append({'tick': key[0], 'unit': key[1], 'present': present,
                             'plus': [[x for r in c['rows'].get(key, []) for x in r['plus']] for c in clients],
                             'minus': [[x for r in c['rows'].get(key, []) for x in r['minus']] for c in clients],
                             'sizes': [[r['sizes'] for r in c['rows'].get(key, [])] for c in clients],
                             'scanner': [[r['scanner'] for r in c['rows'].get(key, [])] for c in clients],
                             'inputs': [{k: v for r in c['rows'].get(key, []) for k, v in r['inputs'].items()} for c in clients],
                             'late': [[x for r in c['rows'].get(key, []) for x in r['late']] for c in clients]})
    return {'rows': [sum(len(v) for v in c['rows'].values()) for c in clients], 'evicted': [c['evicted'] for c in clients],
            'joined': joined, 'differing': differing, 'one_sided': sum(1 for e in examples if not all(e['present'])),
            'first_tick': examples[0]['tick'] if examples else None,
            'late_drops': [sum(len(r['late']) for rows in c['rows'].values() for r in rows) for c in clients], 'examples': examples,
            'status': [c['status'] for c in clients]}


def client_logs(run):
    logs = sorted(Path(run).glob('client*/x4lan-debug.txt'))
    if len(logs) < 2:
        raise SystemExit('need client*/x4lan-debug.txt of at least two clients under ' + str(run))
    return logs


def read(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as handle:
        return parse(handle)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', nargs='?')
    p.add_argument('--logs', nargs='+')
    p.add_argument('--out')
    p.add_argument('--limit', type=int, default=20)
    args = p.parse_args(argv)
    logs = args.logs or (client_logs(args.run) if args.run else None)
    if not logs:
        p.error('give a run directory or --logs')
    result = compare([read(path) for path in logs], args.limit)
    text = json.dumps(result, indent=2)
    if args.out:
        Path(args.out).write_text(text + '\n', encoding='utf-8')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
