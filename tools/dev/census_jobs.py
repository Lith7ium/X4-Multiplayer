"""Per-job RNG draw census: join JOB_RNG rows of two clients by job key.

Each row carries the TLS word at job entry (seed: natural word in observe
mode, keyed seed in key mode) and at exit. Draws per job are generator steps
from seed to exit, recovered with the rng_steps counter. Jobs are joined by
(tick, callback, body, ordinal); a job present on one client only, or with a
different draw count, is a divergence lead attributed to its body RVA.
An N-client run (client1..clientN, N > 2) compares client1 with every other client.
"""
import argparse
import collections
import json
import re
import subprocess
from pathlib import Path

from compare_native_world import bounded_lines
from census_clients import client_slots, against_reference

ROW = re.compile(r'NATIVE_GATE JOB_RNG index=(\d+) tick=(\d+) key=([0-9a-f]{16}) cb=([0-9a-f]+) body=([0-9a-f]+) ord=(\d+) cat=(\d+) '
                 r'tid=(\d+) seeded=(\d) seed=([0-9a-f]{16}) exit=([0-9a-f]{16})')
STATUS = re.compile(r'NATIVE_GATE JOB_RNG_(?:STOP|STATUS tick=\d+) (installed=.*)$')


def parse(path):
    rows, status, evicted = {}, None, 0
    for line in bounded_lines(path):
        if 'NATIVE_GATE JOB_RNG' not in line: continue
        m = ROW.search(line)
        if m:
            key = (int(m[2]), m[4], m[5], int(m[6]))
            rows[key] = dict(key=m[3], cat=int(m[7]), tid=int(m[8]), seeded=int(m[9]), seed=m[10], exit=m[11])
        elif 'evicted=1' in line: evicted += 1
        else:
            s = STATUS.search(line.rstrip())
            if s: status = s[1]
    return rows, status, evicted


def count_draws(rows, counter, limit):
    items = list(rows.values())
    text = ''.join(f"{r['seed']} {r['exit']} {limit}\n" for r in items)
    out = subprocess.run([str(counter)], input=text, capture_output=True, text=True, check=True).stdout.split()
    for r, n in zip(items, out): r['draws'] = int(n)


def analyse(run, counter, limit):
    slots = client_slots(run)
    parsed = [parse(run/f'client{s}/engine-output.log') for s in slots]
    for rows, _, _ in parsed: count_draws(rows, counter, limit)
    if len(slots) == 2: return analyse_pair(parsed)
    return against_reference(slots, lambda k: analyse_pair([parsed[0], parsed[k-1]]))


def analyse_pair(clients):
    a, b = clients[0][0], clients[1][0]
    bodies = collections.defaultdict(lambda: dict(jobs=[0, 0], joined=0, draw_mismatch=0, only=[0, 0], draws=[0, 0],
                                                  first_mismatch_tick=None, examples=[]))
    for key in sorted(a.keys() | b.keys()):
        tick, cb, body, ordinal = key
        x, y = a.get(key), b.get(key)
        g = bodies[f'{cb}/{body}']
        for i, r in enumerate((x, y)):
            if r: g['jobs'][i] += 1; g['draws'][i] += max(r['draws'], 0)
        if x and y:
            g['joined'] += 1
            if x['draws'] != y['draws']:
                g['draw_mismatch'] += 1
                if g['first_mismatch_tick'] is None: g['first_mismatch_tick'] = tick
                if len(g['examples']) < 4: g['examples'].append(dict(tick=tick, ord=ordinal, draws=[x['draws'], y['draws']],
                                                                    seeds_equal=x['seed'] == y['seed']))
        else:
            g['only'][0 if x else 1] += 1
            if g['first_mismatch_tick'] is None: g['first_mismatch_tick'] = tick
    ranked = sorted(bodies.items(), key=lambda kv: (kv[1]['first_mismatch_tick'] is None, kv[1]['first_mismatch_tick'] or 0,
                                                    -kv[1]['draw_mismatch']))
    unreachable = [sum(1 for r in rows.values() if r['draws'] < 0) for rows, _, _ in clients]
    return dict(rows=[len(a), len(b)], evicted=[c[2] for c in clients], unreachable=unreachable,
                status=[c[1] for c in clients], bodies=dict(ranked))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    p.add_argument('--counter', type=Path, required=True)
    p.add_argument('--limit', type=int, default=2_000_000)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    text = json.dumps(analyse(args.run, args.counter, args.limit), indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
