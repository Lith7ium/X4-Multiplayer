"""Where two clients' due events first differ: pass by pass, from the EVENT_ROW lines of one run.

  python tools/dev/census_event_rows.py RUN [--left 1] [--right 2] [--right-run RUN2] [--any-type] [--x4 PATH/TO/X4]
         [--passes 3] [--limit 12]

A run needs native_gate_event_order with native_gate_event_rows = N (rows for the boot passes and the first
N gated ticks). A row is one due event as the pass collected it: source class and ID, the source's and the
event's vtable, due time and the pass's game time. The sets are compared by those fields, then their order
is, because a pass can hold the same events in another order. --x4 names the vtables, from the Linux
executable's typeinfo; a Windows run's vtables stay as RVAs. --right-run takes the right client from another
run, and --any-type leaves the vtables out of the comparison, since a Windows and a Linux run's RVAs never match.
A local source's ID is compared too: it is only portable with runtime IDs set on both sides. Read only.
"""
import argparse
import collections
import re
import struct
import sys
from pathlib import Path

ROW = re.compile(r'EVENT_ROW index=(\d+) pass=(\d+) phase=(\d+) tick=(\d+) slot=(\d+) source=(\w+):([0-9a-f]+) '
                 r'stype=([0-9a-f]+) etype=([0-9a-f]+) due=([0-9a-f]+) now=([0-9a-f]+)')
BASE = 0x400000


def read_rows(path):
    passes, evicted = collections.defaultdict(list), 0
    with open(path, errors='replace') as log:
        for line in log:
            if 'EVENT_ROW' not in line:
                continue
            if 'evicted=1' in line:
                evicted += 1
                continue
            m = ROW.search(line)
            if m:
                passes[int(m[2])].append(dict(phase=int(m[3]), tick=int(m[4]), slot=int(m[5]), klass=m[6], id=m[7], stype=m[8], etype=m[9], due=m[10], now=m[11]))
    return passes, evicted


class Names:
    def __init__(self, path):
        self.data = Path(path).read_bytes() if path else None
        self.cache = {}
        if self.data:
            phoff, = struct.unpack_from('<Q', self.data, 32)
            phnum, = struct.unpack_from('<H', self.data, 56)
            self.loads = [s for s in (struct.unpack_from('<IIQQQQQQ', self.data, phoff + i * 56) for i in range(phnum)) if s[0] == 1]

    def _read(self, va, size):
        for s in self.loads:
            if s[3] <= va and va + size <= s[3] + s[5]:
                return self.data[s[2] + va - s[3]:s[2] + va - s[3] + size]
        return None

    def name(self, rva):
        value = int(rva, 16)
        if not self.data or value == 0xffffffff:
            return rva
        if rva not in self.cache:
            text = rva
            typeinfo = self._read(BASE + value - 8, 8)
            if typeinfo:
                pointer = self._read(struct.unpack('<Q', typeinfo)[0] + 8, 8)
                raw = self._read(struct.unpack('<Q', pointer)[0], 96) if pointer else None
                if raw:
                    text = raw.split(b'\0')[0].decode('ascii', 'replace')
            self.cache[rva] = text
        return self.cache[rva]


def key(row):
    return (row['klass'], row['id'], row['stype'], row['etype'], row['due'], row['now'])


def key_any_type(row):
    return (row['klass'], row['id'], row['due'], row['now'])


def as_double(bits):
    return struct.unpack('>d', bytes.fromhex(bits))[0]


def describe(row, names):
    return (f"tick {row['tick']} phase {row['phase']} {row['klass']}:{row['id']} source {names.name(row['stype'])} event {names.name(row['etype'])} "
            f"due {as_double(row['due']):.6f} now {as_double(row['now']):.6f}")


def compare(left, right, names, passes, limit, out=sys.stdout, key=key):
    shown = 0
    common = sorted(set(left) & set(right))
    print(f'passes: left {len(left)}, right {len(right)}, common {len(common)}', file=out)
    for number in common:
        a, b = left[number], right[number]
        ca, cb = collections.Counter(map(key, a)), collections.Counter(map(key, b))
        same_set, same_order = ca == cb, [key(r) for r in a] == [key(r) for r in b]
        if same_set and same_order:
            continue
        shown += 1
        print(f"pass {number} (phase {a[0]['phase']}, tick {a[0]['tick']}): {len(a)} vs {len(b)} rows, "
              f"{'same set, other order' if same_set else 'different sets'}", file=out)
        by_key = {key(r): r for r in a + b}
        only_left, only_right = list((ca - cb).elements()), list((cb - ca).elements())
        for label, items in (('only left', only_left), ('only right', only_right)):
            print(f'  {label}: {len(items)}', file=out)
            for item in items[:limit]:
                print('    ' + describe(by_key[item], names), file=out)
        if same_set:
            at = next(i for i, (x, y) in enumerate(zip(a, b)) if key(x) != key(y))
            print(f'  first order difference at slot {a[at]["slot"]}:', file=out)
            print('    left  ' + describe(a[at], names), file=out)
            print('    right ' + describe(b[at], names), file=out)
        if shown >= passes:
            break
    if not shown:
        print('every common pass is identical', file=out)
    return shown


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('--left', type=int, default=1)
    parser.add_argument('--right', type=int, default=2)
    parser.add_argument('--right-run', type=Path, help='take the right client from this run')
    parser.add_argument('--any-type', action='store_true', help='leave the vtables out of the comparison (Windows against Linux)')
    parser.add_argument('--x4', help='Linux executable, to name the vtables')
    parser.add_argument('--passes', type=int, default=3, help='differing passes to show')
    parser.add_argument('--limit', type=int, default=12, help='rows per side to show')
    args = parser.parse_args()
    left, evicted_left = read_rows(args.run / f'client{args.left}/engine-output.log')
    right, evicted_right = read_rows((args.right_run or args.run) / f'client{args.right}/engine-output.log')
    if evicted_left or evicted_right:
        print(f'evicted rows: left {evicted_left}, right {evicted_right} (the ring holds 65536; lower the row ticks)')
    if not left or not right:
        raise SystemExit('no EVENT_ROW lines: the run needs native_gate_event_rows')
    compare(left, right, Names(args.x4), args.passes, args.limit, key=key_any_type if args.any_type else key)


if __name__ == '__main__':
    main()
