"""Compare per-thread TLS RNG words from THREAD_RNG census rows of two clients.

Threads are paired by start address and creation order. For each thread the
number of generator steps between consecutive ticks (draws per frame) is
recovered by walking X4's generator forward with an external counter, so a
reseed shows up as "unreachable" rather than as a count.
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

ROW = re.compile(r'NATIVE_GATE THREAD_RNG tick=(\d+) threads=(\d+) skipped=(\d+) ui=(\d+) main=(\d+) (?:rows=(\S*)|parts=(\d+))')
PART = re.compile(r'NATIVE_GATE THREAD_RNG_PART tick=(\d+) part=(\d+) rows=(\S*)')


def rows_of(text):
    rows, seen = [], collections.Counter()
    for item in text.split(','):
        start, thread, word = item.split(':')
        seen[start] += 1
        rows.append(dict(label=f'{start}#{seen[start]}', thread=int(thread), word=word))
    return rows


def parse(path):
    ticks, pending = {}, {}
    for line in bounded_lines(path):
        if 'THREAD_RNG' not in line: continue
        m = PART.search(line)
        if m:
            pending.setdefault(int(m[1]), {})[int(m[2])] = m[3];continue
        m = ROW.search(line)
        if not m: continue
        ticks[int(m[1])] = dict(ui=int(m[4]), main=int(m[5]), text=m[6], parts=int(m[7]) if m[7] else 0)
    for tick, row in ticks.items():
        text = row.pop('text')
        if row.pop('parts'): text = ','.join(v for _, v in sorted(pending.get(tick, {}).items()))
        row['rows'] = rows_of(text)
    return ticks


def steps(pairs, counter, limit):
    if not pairs: return []
    text = ''.join(f'{a} {b} {limit}\n' for a, b in pairs)
    out = subprocess.run([str(counter)], input=text, capture_output=True, text=True, check=True).stdout.split()
    return [int(x) for x in out]


def analyse(run, counter, limit):
    slots = client_slots(run)
    parsed = [parse(run/f'client{s}/engine-output.log') for s in slots]
    if len(slots) == 2: return analyse_pair(parsed, counter, limit)
    return against_reference(slots, lambda k: analyse_pair([parsed[0], parsed[k-1]], counter, limit))


def analyse_pair(clients, counter, limit):
    common = sorted(set(clients[0]) & set(clients[1]))
    labels = [{r['label']: r for r in clients[i][common[0]]['rows']} for i in (0, 1)] if common else [{}, {}]
    shared = sorted(set(labels[0]) & set(labels[1]))
    per_thread = {}
    for label in shared:
        words = [[next((r['word'] for r in clients[i][t]['rows'] if r['label'] == label), None) for t in common] for i in (0, 1)]
        role = [('ui' if labels[i][label]['thread'] == clients[i][common[0]]['ui'] else
                 'main' if labels[i][label]['thread'] == clients[i][common[0]]['main'] else '') for i in (0, 1)]
        pairs = [(w[k], w[k+1]) for w in words for k in range(len(common)-1) if w[k] and w[k+1]]
        counts = steps(pairs, counter, limit)
        half = len(counts)//2
        draws = [counts[:half], counts[half:]]
        changing = any(c != 0 for c in counts)
        first_word_diff = next((common[k] for k in range(len(common)) if words[0][k] != words[1][k]), None)
        first_draw_diff = next((common[k+1] for k in range(min(len(draws[0]), len(draws[1]))) if draws[0][k] != draws[1][k]), None)
        per_thread[label] = dict(role=role, changing=changing, first_word_difference=first_word_diff,
                                 first_draw_count_difference=first_draw_diff,
                                 draws_client1=draws[0][:12], draws_client2=draws[1][:12],
                                 total_draws=[sum(x for x in d if x > 0) for d in draws],
                                 unreachable=[sum(1 for x in d if x < 0) for d in draws])
    return dict(ticks=len(common), threads=[len(labels[0]), len(labels[1])], paired=len(shared),
                only_client1=sorted(set(labels[0])-set(labels[1]))[:16], only_client2=sorted(set(labels[1])-set(labels[0]))[:16],
                active={k: v for k, v in per_thread.items() if v['changing']})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    p.add_argument('--counter', type=Path, required=True, help='batch step counter executable (steps_batch)')
    p.add_argument('--limit', type=int, default=5_000_000)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    text = json.dumps(analyse(args.run, args.counter, args.limit), indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
