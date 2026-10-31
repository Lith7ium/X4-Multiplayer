"""Compare the address order of macros and wares between clients (native_gate_address_order)."""
import argparse
import json
from pathlib import Path
import re
import sys

ROW = re.compile(r'ADDRESS_ORDER_ROW index=(\d+) when=(arm|tick) kind=(macro|ware) total=(\d+) part=(\d+) partial=(\d)(?: at=([0-9a-f]+))? ids=([0-9a-f,]*)')


def parse(lines):
    """{(when, kind): {'ids': [...], 'total': n, 'partial': bool}} from one client's log lines."""
    parts = {}
    for line in lines:
        m = ROW.search(line)
        if not m:
            continue
        key = (m.group(2), m.group(3))
        entry = parts.setdefault(key, {'rows': {}, 'at': {}, 'total': int(m.group(4)), 'partial': False})
        entry['partial'] = entry['partial'] or m.group(6) == '1'
        ids = [i for i in m.group(8).split(',') if i]
        entry['rows'][int(m.group(5))] = ids
        if m.group(7) and ids:
            entry['at'][ids[0]] = m.group(7)
    out = {}
    for key, entry in parts.items():
        ids = []
        for part in sorted(entry['rows']):
            ids += entry['rows'][part]
        out[key] = {'ids': ids, 'total': entry['total'], 'partial': entry['partial'] or len(ids) != entry['total'], 'at': entry['at']}
    return out


def inversions(order):
    """Pairs out of order in a permutation of ranks, by merge count."""
    def count(items):
        if len(items) < 2:
            return items, 0
        mid = len(items)//2
        left, a = count(items[:mid])
        right, b = count(items[mid:])
        merged, i, j, c = [], 0, 0, a+b
        while i < len(left) and j < len(right):
            if left[i] <= right[j]:
                merged.append(left[i])
                i += 1
            else:
                merged.append(right[j])
                j += 1
                c += len(left)-i
        merged += left[i:]+right[j:]
        return merged, c
    return count(list(order))[1]


def compare(a, b, sample=8):
    """Two lists of ids in address order: what they share and how differently they order it."""
    only_a = [i for i in a if i not in set(b)]
    only_b = [i for i in b if i not in set(a)]
    common_b = [i for i in b if i in set(a)]
    rank = {i: k for k, i in enumerate(common_b)}
    common_a = [i for i in a if i in rank]
    order = [rank[i] for i in common_a]
    pairs = []
    for k in range(len(order)-1):
        if order[k] > order[k+1] and len(pairs) < sample:
            pairs.append([common_a[k], common_a[k+1]])
    n = len(order)
    return {'a': len(a), 'b': len(b), 'common': n, 'only_a': len(only_a), 'only_b': len(only_b),
            'inverted_pairs': inversions(order), 'all_pairs': n*(n-1)//2,
            'adjacent_inverted': sum(1 for k in range(n-1) if order[k] > order[k+1]),
            'same_order': order == sorted(order), 'sample': pairs}


def rebuilt(arm, tick):
    """Objects listed at arm and at the tick: did they stay where they were? Only rows' first ids carry an address."""
    tick_at = dict(tick.get('at', {}))
    arm_at = arm.get('at', {})
    shared = [i for i in arm_at if i in tick_at]
    same = sum(1 for i in shared if arm_at[i] == tick_at[i])
    return {'compared': len(shared), 'same_address': same, 'moved': len(shared)-same}


def report(clients):
    """clients: {name: parse(...)}. Every sample of every pair of clients."""
    names = sorted(clients)
    out = {'clients': {}, 'pairs': {}}
    for name in names:
        out['clients'][name] = {f'{when}/{kind}': {'total': v['total'], 'partial': v['partial']} for (when, kind), v in sorted(clients[name].items())}
    for x in range(len(names)):
        for y in range(x+1, len(names)):
            a, b = clients[names[x]], clients[names[y]]
            pair = {}
            for key in sorted(set(a) & set(b)):
                pair[f'{key[0]}/{key[1]}'] = compare(a[key]['ids'], b[key]['ids'])
            out['pairs'][f'{names[x]}-{names[y]}'] = pair
    # Created between the arm and the tick, per client: the set a load adds.
    for name in names:
        for kind in ('macro', 'ware'):
            arm, tick = clients[name].get(('arm', kind)), clients[name].get(('tick', kind))
            if arm and tick:
                out['clients'][name][f'created/{kind}'] = len(set(tick['ids'])-set(arm['ids']))
                out['clients'][name][f'kept/{kind}'] = rebuilt(arm, tick)
    return out


def ascending(ids):
    """Address order is id hash order: what the macro arena promises for every client."""
    return all(int(ids[k], 16) < int(ids[k+1], 16) for k in range(len(ids)-1))


def check_lines(clients):
    """One line per pair for the tick's macros; (lines, whether all of them hold)."""
    lines, ok = [], True
    names = sorted(clients)
    for x in range(len(names)):
        for y in range(x+1, len(names)):
            a, b = clients[names[x]].get(('tick', 'macro')), clients[names[y]].get(('tick', 'macro'))
            if not a or not b:
                lines.append(f'ADDRESS_ORDER_CHECK tick/macro pair={names[x]}-{names[y]} missing=1')
                ok = False
                continue
            c = compare(a['ids'], b['ids'])
            hash_order = ascending(a['ids']) and ascending(b['ids'])
            lines.append(f"ADDRESS_ORDER_CHECK tick/macro pair={names[x]}-{names[y]} same_order={str(c['same_order']).lower()} "
                         f"hash_order={str(hash_order).lower()} common={c['common']} only={c['only_a']}/{c['only_b']} "
                         f"inverted={c['inverted_pairs']} of={c['all_pairs']} partial={str(a['partial'] or b['partial']).lower()}")
            ok = ok and c['same_order'] and not (a['partial'] or b['partial'])
    return lines, ok


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path, help='a census run directory (client*/engine-output.log)')
    p.add_argument('--require-macro-order', action='store_true', help='exit 1 unless tick/macro same_order=true on every pair')
    args = p.parse_args()
    clients = {}
    for log in sorted(args.run.glob('client*/engine-output.log')):
        with open(log, encoding='utf-8', errors='replace') as f:
            clients[log.parent.name] = parse(line for line in f if 'ADDRESS_ORDER_ROW' in line)
    if not clients:
        sys.exit(f'no client logs under {args.run}')
    lines, ok = check_lines(clients)
    print(json.dumps(report(clients), indent=1))
    for line in lines:
        print(line)
    if args.require_macro_order and not ok:
        sys.exit(1)


if __name__ == '__main__':
    main()
