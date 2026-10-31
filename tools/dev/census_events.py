"""Event census: which due events differ between the two clients (native_gate_event_rows).

Each EVENT_ROW is one collected due-event batch entry: pass, phase, tick, slot,
source (saved ID, a created ID with native_gate_portable_created, or an
unportable runtime ID), source/event vtable RVAs and the
due time. Rows are grouped by delivery pass in recording order (or by tick with
--by tick) and every group pair is compared as a multiset keyed by
(source, event type, due time). Unmapped sources are keyed by their class vtable
RVA because runtime IDs differ between peers (--runtime keys them by runtime ID).
The first differing group is printed with the rows present on only one client.
An N-client run (client1..clientN, N > 2) compares client1 with every other client.

native_gate_event_rng adds EVENT_RNG status lines (cumulative counters and
order-free keyset/drawset sums of the deliveries so far) and EVENT_RNG_ROW lines
(deliveries whose listener drew: tick, class, key, event type, due time, the
thread word at entry, draws). event_rng reports the first tick whose sums differ,
rows present on one client only, the first joined delivery with other draws, and
the first joined delivery that drew from a word that already differed (entry; in
'observe' that is the natural frame word the listener inherited).
"""
import argparse
import collections
import csv
import json
import re
import struct
from pathlib import Path

from census_clients import client_slots, against_reference

ROW = re.compile(r'NATIVE_GATE EVENT_ROW index=(\d+) (?:(evicted)=1|pass=(\d+) phase=(\d+) tick=(\d+) slot=(\d+) '
                 r'source=(saved|created|runtime|none|\?):([0-9a-f]{16}) stype=([0-9a-f]+) etype=([0-9a-f]+) '
                 r'due=([0-9a-f]{16}) now=([0-9a-f]{16}))')


RNG_STATUS = re.compile(r'NATIVE_GATE EVENT_RNG tick=(\d+) (.*)$')
RNG_ROW = re.compile(r'NATIVE_GATE EVENT_RNG_ROW index=(\d+) (?:(evicted)=1|tick=(\d+) class=(\w+) key=([0-9a-f]{16}) stype=([0-9a-f]+) '
                     r'etype=([0-9a-f]+) due=([0-9a-f]{16}) entry=([0-9a-f]{16}) draws=(-?\d+) keyed=([01]))')


def lines(path):
    with Path(path).open('rb') as source:
        for raw in source: yield raw.decode('utf-8', 'replace')


def parse(stream):
    """Rows in index order; a re-drained index keeps its first copy."""
    rows, seen, top = [], set(), -1
    stats = collections.Counter()
    for line in stream:
        if 'EVENT_ROW ' not in line: continue
        m = ROW.search(line)
        if not m: stats['malformed'] += 1;continue
        index = int(m[1])
        if index in seen: stats['duplicate'] += 1;continue
        seen.add(index);top = max(top, index)
        if m[2]: stats['evicted'] += 1;continue
        rows.append(dict(index=index, pass_=int(m[3]), phase=int(m[4]), tick=int(m[5]), slot=int(m[6]), kind=m[7],
                         source=int(m[8], 16), stype=int(m[9], 16), etype=int(m[10], 16), due=m[11], now=m[12]))
    rows.sort(key=lambda r: r['index'])
    stats['gaps'] = top+1-len(seen)  # never drained, e.g. a run stopped before STOP
    stats['rows'] = len(rows)
    return rows, dict(stats)


def source_key(row, runtime=False):
    if row['kind'] in ('saved', 'created'): return f"{row['kind']}:{row['source']:x}"
    if row['kind'] == 'runtime': return f"runtime:{row['source']:x}" if runtime else f"unmapped@{row['stype']:x}"
    return row['kind']


def key(row, runtime=False):
    return (source_key(row, runtime), row['etype'], row['due'])


def groups(rows, by):
    """Ordered (label, meta, rows): passes by first appearance, or ticks ascending."""
    out = collections.OrderedDict()
    for row in rows:
        label = row['pass_'] if by == 'pass' else row['tick']
        group = out.setdefault(label, dict(label=label, phases=set(), ticks=set(), now=set(), rows=[]))
        group['phases'].add(row['phase']);group['ticks'].add(row['tick']);group['now'].add(row['now']);group['rows'].append(row)
    result = list(out.values())
    if by == 'tick': result.sort(key=lambda g: g['label'])
    return result


def load_names(path):
    """Primary vtable RVA -> class name from x4-re classes.csv (optional)."""
    names = {}
    if not path: return names
    with Path(path).open(encoding='utf-8', errors='replace', newline='') as source:
        for row in csv.DictReader(source):
            for part in (row.get('vtable_rvas') or '').split(';'):
                if part.startswith('0x') and '@' not in part:
                    try: names.setdefault(int(part, 16), row['class'].strip())
                    except ValueError: pass
    return names


def due_value(bits):
    return struct.unpack('<d', bytes.fromhex(bits)[::-1])[0]


def describe(row, names):
    out = dict(index=row['index'], pass_=row['pass_'], phase=row['phase'], tick=row['tick'], slot=row['slot'],
               source=f"{row['kind']}:{row['source']:016x}", stype=f"{row['stype']:x}", etype=f"{row['etype']:x}",
               due=row['due'], due_value=due_value(row['due']))
    if row['stype'] in names: out['stype_class'] = names[row['stype']]
    if row['etype'] in names: out['etype_class'] = names[row['etype']]
    return out


def meta(group):
    return dict(label=group['label'], rows=len(group['rows']), phases=sorted(group['phases']), ticks=sorted(group['ticks']),
                now=sorted(group['now']))


def compare(left, right, by='pass', runtime=False, limit=50, names=None):
    names = names or {}
    a, b = groups(left, by), groups(right, by)
    if by == 'tick':
        labels = sorted({g['label'] for g in a} | {g['label'] for g in b})
        index_a, index_b = {g['label']: g for g in a}, {g['label']: g for g in b}
        pairs = [(index_a.get(label), index_b.get(label)) for label in labels]
    else:
        pairs = [(a[i] if i < len(a) else None, b[i] if i < len(b) else None) for i in range(max(len(a), len(b)))]
    first, differing = None, []
    empty = dict(label=None, phases=set(), ticks=set(), now=set(), rows=[])
    for position, (x, y) in enumerate(pairs):
        x, y = x or empty, y or empty
        cx = collections.Counter(key(r, runtime) for r in x['rows'])
        cy = collections.Counter(key(r, runtime) for r in y['rows'])
        if cx == cy: continue
        only_x, only_y = cx-cy, cy-cx
        differing.append(dict(position=position, client1=meta(x), client2=meta(y),
                              only_client1=sum(only_x.values()), only_client2=sum(only_y.values())))
        if first is None:
            def pick(rows, extra):
                left_over, picked = collections.Counter(extra), []
                for row in rows:
                    k = key(row, runtime)
                    if left_over[k] > 0: left_over[k] -= 1;picked.append(describe(row, names))
                return picked
            first = dict(position=position, client1=meta(x), client2=meta(y),
                         only_client1=pick(x['rows'], only_x)[:limit], only_client2=pick(y['rows'], only_y)[:limit],
                         only_client1_count=sum(only_x.values()), only_client2_count=sum(only_y.values()))
    return dict(by=by, groups=[len(a), len(b)], compared=len(pairs), differing_groups=len(differing),
                differing=differing[:limit], first_difference=first)


def parse_rng(stream):
    """EVENT_RNG status per tick (fields as text) and EVENT_RNG_ROW rows in index order."""
    status, rows, seen = {}, [], set()
    stats = collections.Counter()
    for line in stream:
        if 'EVENT_RNG' not in line: continue
        if 'EVENT_RNG_ROW ' in line:
            m = RNG_ROW.search(line)
            if not m: stats['malformed'] += 1;continue
            index = int(m[1])
            if index in seen: stats['duplicate'] += 1;continue
            seen.add(index)
            if m[2]: stats['evicted'] += 1;continue
            rows.append(dict(index=index, tick=int(m[3]), klass=m[4], key=m[5], stype=m[6], etype=m[7], due=m[8], entry=m[9],
                             draws=int(m[10]), keyed=m[11] == '1'))
            continue
        m = RNG_STATUS.search(line.rstrip())
        if m: status[int(m[1])] = dict(re.findall(r'(\w+)=(\S+)', m[2]))
    rows.sort(key=lambda r: r['index'])
    stats['rows'] = len(rows);stats['status'] = len(status)
    return dict(status=status, rows=rows, stats=dict(stats))


def rng_key(row):
    return (row['tick'], row['klass'], row['key'], row['etype'], row['due'])


def compare_rng(left, right, limit=20):
    """First tick whose cumulative sums differ; rows joined by (tick, class, key, event type, due)."""
    ticks = sorted(set(left['status']) & set(right['status']))
    first_sums = next((t for t in ticks if any(left['status'][t].get(f) != right['status'][t].get(f) for f in ('keyset', 'drawset'))), None)
    by_a, by_b = collections.defaultdict(list), collections.defaultdict(list)
    for row in left['rows']: by_a[rng_key(row)].append(row)
    for row in right['rows']: by_b[rng_key(row)].append(row)
    only_a = sorted((r for k in by_a.keys()-by_b.keys() for r in by_a[k]), key=lambda r: r['index'])
    only_b = sorted((r for k in by_b.keys()-by_a.keys() for r in by_b[k]), key=lambda r: r['index'])
    draws, entries = [], []
    for k in sorted(by_a.keys() & by_b.keys()):
        for x, y in zip(by_a[k], by_b[k]):
            pair = dict(tick=k[0], klass=k[1], key=k[2], etype=k[3], due=k[4], draws=[x['draws'], y['draws']], entry=[x['entry'], y['entry']])
            if x['draws'] != y['draws']: draws.append(pair)
            if x['entry'] != y['entry']: entries.append(pair)
    trade = [r for r in only_a+only_b if r['etype'] == '2ac17b8']
    return dict(status_ticks=len(ticks), first_sums_difference=first_sums, rows=[len(left['rows']), len(right['rows'])],
                only=[len(only_a), len(only_b)], only_client1=only_a[:limit], only_client2=only_b[:limit],
                draw_mismatch=len(draws), first_draw_mismatch=draws[0] if draws else None,
                entry_mismatch=len(entries), first_entry_mismatch=entries[0] if entries else None,
                trade_offer_rows_one_side=len(trade))


def client_logs(run):
    return [Path(run)/f'client{slot}'/'engine-output.log' for slot in client_slots(run)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path, nargs='?', help='run directory with client1/..clientN/engine-output.log')
    p.add_argument('--logs', type=Path, nargs='+', metavar='LOG', help='explicit engine-output.log of client1, client2[, ...]')
    p.add_argument('--by', choices=('pass', 'tick'), default='pass')
    p.add_argument('--runtime', action='store_true', help='key unmapped sources by their (unportable) runtime ID')
    p.add_argument('--classes', type=Path, help='optional x4-re classes.csv to name vtable RVAs')
    p.add_argument('--limit', type=int, default=50)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    if bool(args.run) == bool(args.logs): p.error('give a run directory or --logs CLIENT1 CLIENT2 [...]')
    if args.logs and len(args.logs) < 2: p.error('--logs needs at least two clients')
    paths = args.logs or client_logs(args.run)
    kept = [[line for line in lines(path) if 'EVENT_R' in line] for path in paths] # EVENT_ROW and EVENT_RNG lines, read once
    parsed = [parse(k) for k in kept]
    rng = [parse_rng(k) for k in kept]
    names = load_names(args.classes)
    pair = lambda k: dict(clients=[parsed[0][1], parsed[k-1][1]], **compare(parsed[0][0], parsed[k-1][0], args.by, args.runtime, args.limit, names))
    result = pair(2) if len(paths) == 2 else against_reference(list(range(1, len(paths)+1)), pair)
    if any(r['rows'] or r['status'] for r in rng):
        result['event_rng'] = dict(clients=[r['stats'] for r in rng], **compare_rng(rng[0], rng[1], args.limit))
    text = json.dumps(result, indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
