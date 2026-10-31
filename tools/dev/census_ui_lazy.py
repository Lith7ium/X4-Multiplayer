"""Lazy container updates two clients made differently: UI_LAZY_ROW rows of a run (native_gate_ui_lazy_rows).

  python tools/dev/census_ui_lazy.py RUN [--left 1] [--right 2] [--limit 20]

RUN is a directory with client1, client2, ... In the row window every call of the three lazy updates in an admitted
frame is a row (target cargo, trade or units; the container's portable key; lua=1 for a UI query; a1/a2 the arguments,
state what the container was brought to before; chain the six image return addresses). Rows are compared tick by tick
as multisets of (target, class, key, lua, a1, a2, chain):
  only_left / only_right: calls one client made and the other did not, the first ones in tick order;
  state: the same call on both, but the container had been brought to another time before it.
Reads logs only.
"""
import argparse
import collections
from pathlib import Path
import re

ROW = re.compile(r'UI_LAZY_ROW index=(\d+) tick=(\d+) target=(\w+) class=(\w+) key=(\w+) lua=(\d+) blocked=(\d+) '
                 r'a1=(\w+) a2=(\w+) state=(\w+) chain=([0-9a-f,]+) other=(\d+)')


def read_rows(path):
    """{tick: [(call, state)]} from one client's log, each row once."""
    rows = {}
    with open(path, errors='replace') as log:
        for line in log:
            match = ROW.search(line)
            if match:
                index, tick, target, klass, key, lua, _blocked, a1, a2, state, chain, other = match.groups()
                rows[int(index)] = (int(tick), (target, klass, key, lua, a1, a2, chain, other), state)
    by_tick = collections.defaultdict(list)
    for index in sorted(rows):
        tick, call, state = rows[index]
        by_tick[tick].append((call, state))
    return by_tick


def compare(left, right):
    """(only_left, only_right, state) lists of (tick, call[, states]) over the ticks both clients have rows for."""
    only_left, only_right, state = [], [], []
    for tick in sorted(set(left) | set(right)):
        a = collections.Counter(call for call, _ in left.get(tick, []))
        b = collections.Counter(call for call, _ in right.get(tick, []))
        only_left += [(tick, call) for call in (a - b).elements()]
        only_right += [(tick, call) for call in (b - a).elements()]
        sa = collections.defaultdict(list)
        sb = collections.defaultdict(list)
        for call, s in left.get(tick, []):
            sa[call].append(s)
        for call, s in right.get(tick, []):
            sb[call].append(s)
        for call in sa.keys() & sb.keys():
            if sa[call] != sb[call]:
                state.append((tick, call, sa[call], sb[call]))
    return only_left, only_right, state


def show(call):
    target, klass, key, lua, a1, a2, chain, other = call
    return f'{target} {klass} key={key} lua={lua} a1={a1} a2={a2} chain={chain}' + (' other=1' if other != '0' else '')


def log_of(run, client):
    return Path(run) / f'client{client}' / 'engine-output.log'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run')
    parser.add_argument('--left', type=int, default=1)
    parser.add_argument('--right', type=int, default=2)
    parser.add_argument('--limit', type=int, default=20)
    args = parser.parse_args()
    left, right = read_rows(log_of(args.run, args.left)), read_rows(log_of(args.run, args.right))
    print(f'ticks with rows {len(left)}/{len(right)}, rows {sum(map(len, left.values()))}/{sum(map(len, right.values()))}')
    only_left, only_right, state = compare(left, right)
    for name, rows in ((f'only client{args.left}', only_left), (f'only client{args.right}', only_right)):
        print(f'{name}: {len(rows)}')
        for tick, call in rows[:args.limit]:
            print(f'  tick={tick} {show(call)}')
    print(f'same call, other state: {len(state)}')
    for tick, call, a, b in state[:args.limit]:
        print(f'  tick={tick} {show(call)} state {",".join(a)} | {",".join(b)}')


if __name__ == '__main__':
    main()
