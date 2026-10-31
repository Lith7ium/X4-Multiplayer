"""Who gets built, and the first decision two clients disagree on: BUILD_ROW rows of a run (native_gate_build_rows).

  python tools/dev/census_build_rows.py RUN [--left 1] [--right 2] [--limit 8] [--context 2]

RUN is a directory with client1, client2, ... Rows are find (the list of waiting ships as native built it), add (a
build task for a ship) and start (a processor takes a task). They come in the order decided, so the first one that
differs says which step parted first:
  find: the list differs (n, set) or only its order (order), or the shuffle's input word (rng);
  add:  same list, another ship gets the next task: the shuffle or the script's choice after it;
  start: same tasks in the same order, another one starts: the start logic, so a shipyard's resources.
Reads logs only.
"""
import argparse
from pathlib import Path
import re

ROW = re.compile(r'BUILD_ROW index=(\d+) (tick=\d+ kind=\w+ .*?)\s*$')


def read_rows(path):
    """The rows of one client's log in index order, without their index."""
    rows = {}
    with open(path, errors='replace') as log:
        for line in log:
            match = ROW.search(line)
            if match:
                rows[int(match.group(1))] = match.group(2)
    return [rows[index] for index in sorted(rows)]


def fields(row):
    return dict(pair.split('=', 1) for pair in row.split() if '=' in pair)


def first_difference(left, right):
    """Index of the first row that differs, or None when one list is a prefix of the other or they are equal."""
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return index
    return None


def explain(a, b):
    """What the two rows at the first difference say."""
    x, y = fields(a), fields(b)
    if x.get('kind') != y.get('kind') or x.get('tick') != y.get('tick'):
        return f"different steps: {x.get('kind')} at tick {x.get('tick')} against {y.get('kind')} at tick {y.get('tick')}"
    kind = x['kind']
    if kind == 'find':
        if x.get('set') != y.get('set') or x.get('n') != y.get('n'):
            return 'find: the lists hold different ships'
        if x.get('order') != y.get('order'):
            return 'find: the same ships in another order'
        if x.get('rng') != y.get('rng'):
            return "find: the same list, another RNG word: the shuffle's input"
    if kind == 'add':
        if x.get('ship') != y.get('ship'):
            return 'add: another ship gets the task, so the choice after the find'
        if x.get('task') != y.get('task'):
            return 'add: the same ship under another task ID'
    if kind == 'start':
        if x.get('task') != y.get('task'):
            return 'start: another task starts, so the start logic or the resources behind it'
        if x.get('ok') != y.get('ok'):
            return 'start: the same task, started on one client only'
        if x.get('processor') != y.get('processor'):
            return 'start: the same task on another processor'
    differing = sorted(k for k in set(x) | set(y) if x.get(k) != y.get(k))
    return f'{kind}: {",".join(differing)}'


def log_of(run, client):
    return Path(run) / f'client{client}' / 'engine-output.log'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run')
    parser.add_argument('--left', type=int, default=1)
    parser.add_argument('--right', type=int, default=2)
    parser.add_argument('--context', type=int, default=2)
    parser.add_argument('--limit', type=int, default=8)
    args = parser.parse_args()
    left, right = read_rows(log_of(args.run, args.left)), read_rows(log_of(args.run, args.right))
    print(f'rows {len(left)}/{len(right)}')
    index = first_difference(left, right)
    if index is None:
        print(f'no row differs in the first {min(len(left), len(right))}')
        return
    print(f'first difference at row {index}: {explain(left[index], right[index])}')
    for i in range(max(0, index - args.context), min(index + args.limit, max(len(left), len(right)))):
        a = left[i] if i < len(left) else '<none>'
        b = right[i] if i < len(right) else '<none>'
        if a == b:
            print(f'  = {a[:150]}')
        else:
            print(f'  1 {a[:190]}')
            print(f'  2 {b[:190]}')


if __name__ == '__main__':
    main()
