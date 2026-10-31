"""Who allocates runtime IDs, and which allocations one client made more often: RUNTIME_IDS_CALLER rows of a run.

  python tools/dev/census_runtime_callers.py RUN [--left 1] [--right 2] [--right-run RUN2] [--tick N] [--phase load|play] [--limit 40]
  python tools/dev/census_runtime_callers.py RUN --first

RUN is a directory with client1, client2, ... Each client logs the table at tick 1 (Linux) and every 300 ticks, or
every native_gate_runtime_ids_caller_stride ticks.
Rows are compared at the last tick both clients logged, or at --tick, by (phase, thread, route, frames); only those
whose counts differ are listed, largest first. Frames are return addresses, innermost first, relative to the image
base (0x400000 on Linux, 0x140000000 on Windows). Reads logs only.

--first walks the ticks in order and stops at the first play-phase difference in the shared list, the one
native_gate_runtime_ids_barrier folds into the digest: the ui and view lists differ between clients by design. A
simulation allocation one client calls local shows here too, because the route is part of the row.
"""
import argparse
import collections
import re
from pathlib import Path

ROW = re.compile(r'RUNTIME_IDS_CALLER tick=(\d+) phase=(\w+) thread=(\w+) route=(\w+) count=(\d+) frames=([0-9a-f,]+)(?: cue=([\w.$]+))?')


def read_table(path):
    """{tick: {(phase, thread, route, frames): count}} from one client's log; an MD cue instance's frames end in
    ' cue=<name>' (the same stack for another cue is another row)."""
    tables = collections.defaultdict(dict)
    with open(path, errors='replace') as log:
        for line in log:
            match = ROW.search(line)
            if match:
                tick, phase, thread, route, count, frames, cue = match.groups()
                tables[int(tick)][(phase, thread, route, frames + (' cue=' + cue if cue else ''))] = int(count)
    return tables


def compare(left, right):
    """Rows whose counts differ, (difference, key, left count, right count), largest difference first."""
    rows = []
    for key in sorted(set(left) | set(right)):
        a, b = left.get(key, 0), right.get(key, 0)
        if a != b:
            rows.append((a - b, key, a, b))
    rows.sort(key=lambda row: (-abs(row[0]), row[1]))
    return rows


def first_difference(left, right, phase='play', route='shared'):
    """(first differing tick or None, last tick that agreed or None, its rows) over the ticks both clients logged."""
    agreed = None
    for tick in sorted(set(left) & set(right)):
        a = {k: v for k, v in left[tick].items() if k[0] == phase and k[2] == route}
        b = {k: v for k, v in right[tick].items() if k[0] == phase and k[2] == route}
        rows = compare(a, b)
        if rows:
            return tick, agreed, rows
        agreed = tick
    return None, agreed, []


def log_of(run, client):
    return Path(run) / f'client{client}' / 'engine-output.log'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run')
    parser.add_argument('--left', type=int, default=1)
    parser.add_argument('--right', type=int, default=2)
    parser.add_argument('--right-run')
    parser.add_argument('--tick', type=int)
    parser.add_argument('--phase', choices=('load', 'play'))
    parser.add_argument('--limit', type=int, default=40)
    parser.add_argument('--first', action='store_true', help='the first tick the shared list differs, and the stacks that did it')
    args = parser.parse_args()
    left = read_table(log_of(args.run, args.left))
    right = read_table(log_of(args.right_run or args.run, args.right))
    common = sorted(set(left) & set(right))
    if not common:
        raise SystemExit(f'no tick with RUNTIME_IDS_CALLER rows on both clients (left {sorted(left)}, right {sorted(right)})')
    if args.first:
        tick, agreed, rows = first_difference(left, right)
        if tick is None:
            print(f'shared list agrees on all {len(common)} ticks logged on both clients, through {agreed}')
            return
        print(f'first difference in the shared list at tick {tick}; last agreed tick {agreed}; {len(rows)} rows differ')
        for difference, (phase, thread, route, frames), count_a, count_b in rows[:args.limit]:
            print(f'{difference:+6d} {count_a:8d} {count_b:8d} {phase} {thread} {route} {frames}')
        return
    tick = args.tick if args.tick is not None else common[-1]
    if tick not in common:
        raise SystemExit(f'tick {tick} is not logged on both clients; ticks {common}')
    a, b = left[tick], right[tick]
    if args.phase:
        a = {k: v for k, v in a.items() if k[0] == args.phase}
        b = {k: v for k, v in b.items() if k[0] == args.phase}
    print(f'tick {tick}: rows {len(a)}/{len(b)}, allocations {sum(a.values())}/{sum(b.values())}, difference {sum(a.values()) - sum(b.values())}; ticks logged {common}')
    rows = compare(a, b)
    print(f'{len(rows)} rows differ')
    for difference, (phase, thread, route, frames), count_a, count_b in rows[:args.limit]:
        print(f'{difference:+6d} {count_a:8d} {count_b:8d} {phase} {thread} {route} {frames}')


if __name__ == '__main__':
    main()
