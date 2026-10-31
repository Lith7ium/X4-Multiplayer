"""Divergence census: every imported object's first differing capture, grouped into root candidates.

A strict run stops at the first differing tick and therefore shows one cause at a
time. A census run (relay --observe) keeps both clients running, so every world
capture can be compared. Divergence spreads: an object that differs after a
neighbour in the same parent (zone/station) already differed is probably fallout.
An object that is the earliest to differ within its parent group is a root
candidate. Groups that first differ at the same capture are independent leads
that can be investigated in parallel. This is a triage aid, not causality proof.
An N-client run (client1..clientN, N > 2) compares client1 with every other
client in one pass over the logs and reports each pair separately.
With native_gate_created_identities, objects created after the load take part
too (ID bit 63, creation tick in bits 40..62): every seed says whether it is
saved or created, a creation that happened on one client only is an inventory
divergence, and CREATED_IDENTITIES/CREATED_ROW lines give the first tick and
row at which the clients' creation streams differ.
"""
import argparse
import collections
import json
import re
from pathlib import Path

from native_world_evidence import DOMAINS, frames, configured_watches, configured_schema, identity_origin, created_tick, is_created
from compare_native_world import bounded_lines
from census_clients import client_slots, against_reference

ACTION = re.compile(r'NATIVE_GATE WORLD_ACTION_EVENT event=(\d+) part=0 .*? boot=(\d) tick=(\d+) .*?id=([0-9a-f]{16}) name=(\S+) .*?slot=(\d+) .*?rng_before=([0-9a-f]{16}) rng_after=([0-9a-f]{16})')
RNG = re.compile(r'NATIVE_GATE RNG tick=(\d+) state=([0-9a-f]{16}|nil)')
ANCHOR = re.compile(r'NATIVE_GATE BOOT_RNG_ANCHOR(?:_STOP)? (armed=.*)$')
CREATED_STATUS = re.compile(r'NATIVE_GATE CREATED_IDENTITIES tick=(\d+) .*? assigned=(\d+) .*? digest=([0-9a-f]{16}) ')
CREATED_ROW = re.compile(r'NATIVE_GATE CREATED_ROW index=\d+ kind=assign tick=(\d+) id=([0-9a-f]{16}) parent=([0-9a-f]{16}) '
                         r'type=([0-9a-f]+) ordinal=(\d+) macro=(\S+)')


def client_streams(run, slots=(1, 2)):
    errors, streams = [], []
    for slot in slots:
        client = run/f'client{slot}'
        config = (client/'installed/x4_lan_lab-config.lua').read_text()
        streams.append(frames(bounded_lines(client/'engine-output.log'), errors, slot, configured_watches(config),
                              True, configured_schema(config)))
    return streams, errors


def side_logs(run, slots=(1, 2)):
    """Per-tick UI RNG words, selected action RNG states and the anchor receipt."""
    result = []
    for slot in slots:
        rng, actions, anchor, created, rows = {}, [], [], {}, []
        for line in bounded_lines(run/f'client{slot}/engine-output.log'):
            if 'NATIVE_GATE CREATED_' in line:
                m = CREATED_STATUS.search(line)
                if m: created[int(m[1])] = (int(m[2]), m[3])
                m = CREATED_ROW.search(line)
                if m: rows.append(dict(tick=int(m[1]), id=m[2], parent=m[3], type=m[4], ordinal=int(m[5]), macro=m[6]))
            elif 'NATIVE_GATE RNG ' in line:
                m = RNG.search(line)
                if m: rng[int(m[1])] = m[2]
            elif 'WORLD_ACTION_EVENT' in line and ' part=0 ' in line:
                m = ACTION.search(line)
                if m: actions.append(dict(event=int(m[1]), boot=int(m[2]), tick=int(m[3]), owner=m[4], script=m[5],
                                          slot=int(m[6]), rng_before=m[7], rng_after=m[8]))
            elif 'BOOT_RNG_ANCHOR' in line:
                m = ANCHOR.search(line.rstrip())
                if m: anchor.append(m[1])
        result.append(dict(rng=rng, actions=actions, anchor=anchor, created=created, created_rows=rows))
    return result


def compare_created(a, b):
    """First tick whose cumulative creation digest differs, and the first
    differing assignment row (both streams are in resolution order)."""
    ticks = sorted(set(a.get('created', {})) & set(b.get('created', {})))
    first = next((t for t in ticks if a['created'][t] != b['created'][t]), None)
    rows_a, rows_b = a.get('created_rows', []), b.get('created_rows', [])
    first_row = next(({'index': i, 'left': x, 'right': y} for i, (x, y) in enumerate(zip(rows_a, rows_b)) if x != y), None)
    if first_row is None and len(rows_a) != len(rows_b):
        i = min(len(rows_a), len(rows_b))
        first_row = {'index': i, 'left': rows_a[i] if i < len(rows_a) else None, 'right': rows_b[i] if i < len(rows_b) else None}
    return dict(status_ticks=len(ticks), first_digest_difference=first,
                assigned=[a['created'][ticks[-1]][0], b['created'][ticks[-1]][0]] if ticks else None,
                rows=[len(rows_a), len(rows_b)], first_row_difference=first_row)


def created_names(sides):
    """Created ID -> macro, from any client's CREATED_ROW lines."""
    names = {}
    for side in sides:
        for row in side.get('created_rows', []): names.setdefault(int(row['id'], 16), row['macro'])
    return names


def compare_side(sides):
    a, b = sides
    rng_ticks = sorted(set(a['rng']) & set(b['rng']))
    first_rng = next((t for t in rng_ticks if a['rng'][t] != b['rng'][t]), None)
    first_action = None
    for x, y in zip(a['actions'], b['actions']):
        if (x['owner'], x['script'], x['slot']) != (y['owner'], y['script'], y['slot']):
            first_action = dict(kind='order', left=x, right=y);break
        if x['rng_before'] != y['rng_before'] or x['rng_after'] != y['rng_after']:
            first_action = dict(kind='rng', left=x, right=y);break
    result = dict(ui_rng_ticks=len(rng_ticks), first_ui_rng_difference=first_rng,
                  actions=[len(a['actions']), len(b['actions'])], first_action_difference=first_action,
                  anchor=[a['anchor'], b['anchor']])
    if a.get('created') or b.get('created') or a.get('created_rows') or b.get('created_rows'):
        result['created'] = compare_created(a, b)
    return result


def census(run, limit=64, slots=(1, 2), names=None):
    """Two slots: the original report. More: client1 against every other slot."""
    streams, errors = client_streams(run, slots)
    others = range(1, len(streams))
    firsts = {k: {} for k in others}       # identity -> (tick, domains, type, parent)
    timelines = {k: [] for k in others}
    captures = 0
    while True:
        captured = [next(stream, None) for stream in streams]
        if any(frame is None for frame in captured):
            if any(frame is not None for frame in captured): errors.append('world capture count differs between clients')
            break
        captures += 1
        a = captured[0]
        if any(frame['tick'] != a['tick'] for frame in captured):
            errors.append('capture cadence differs: '+' versus '.join(str(frame['tick']) for frame in captured));break
        for k in others:
            b, first = captured[k], firsts[k]
            tick, new, diverged, created = a['tick'], 0, collections.Counter(), collections.Counter()
            if a['roots'] != b['roots']:
                x_objects, y_objects = a['objects'], b['objects']
                for identity in x_objects.keys() | y_objects.keys():
                    x, y = x_objects.get(identity), y_objects.get(identity)
                    changed = ('inventory',) if x is None or y is None else tuple(
                        name for name, h1, h2 in zip(DOMAINS, x['hashes'], y['hashes']) if h1 != h2)
                    if not changed: continue
                    for name in changed: diverged[name] += 1
                    if is_created(identity):
                        created['objects'] += 1
                        if changed == ('inventory',): created['inventory'] += 1
                    if identity not in first:
                        row = x or y
                        first[identity] = (tick, changed, row['type'], row['parent']);new += 1
            entry = dict(tick=tick, diverged_by_domain=dict(diverged), newly_diverged=new, coverage=[a['coverage'], b['coverage']])
            if created: entry['diverged_created'] = dict(created)
            timelines[k].append(entry)
    results = [census_pair(firsts[k], timelines[k], captures, errors, limit, names) for k in others]
    if len(slots) == 2: return results[0]
    return dict(against_reference(slots, lambda k: results[slots.index(k)-1]), captures=captures,
                errors=errors[:64], error_count=len(errors))


def describe(identity, domains, kind, names=None):
    """A seed: saved IDs as before; created IDs also say so, with creation tick and macro."""
    seed = dict(saved_id=f'{identity:016x}', origin=identity_origin(identity), domains=list(domains), type=f'{kind:x}')
    if is_created(identity):
        seed['created_tick'] = created_tick(identity)
        if names and identity in names: seed['macro'] = names[identity]
    return seed


def census_pair(first, timeline, captures, errors, limit, names=None):
    # Root candidates: earliest divergence inside each parent group.
    groups = collections.defaultdict(list)
    for identity, (tick, domains, kind, parent) in first.items():
        groups[parent].append((tick, identity, domains, kind))
    roots = []
    for parent, members in groups.items():
        members.sort()
        earliest = members[0][0]
        seeds = [m for m in members if m[0] == earliest]
        roots.append(dict(parent=f'{parent:016x}', parent_origin=identity_origin(parent), first_tick=earliest, members=len(members),
                          seeds=[describe(i, d, k, names) for _, i, d, k in seeds[:8]],
                          seed_count=len(seeds),
                          seed_domains=dict(collections.Counter(name for _, _, d, _ in seeds for name in d))))
    roots.sort(key=lambda r: (r['first_tick'], -r['members']))
    by_tick = collections.Counter(r['first_tick'] for r in roots)
    created = [identity for identity in first if is_created(identity)]
    return dict(captures=captures, diverged_objects=len(first), timeline=timeline,
                diverged_created=len(created),
                created_inventory=sum(1 for identity in created if first[identity][1] == ('inventory',)),
                root_groups=len(roots), root_groups_by_first_tick=dict(sorted(by_tick.items())),
                roots=roots[:limit], errors=errors[:64], error_count=len(errors))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    p.add_argument('--out', type=Path)
    p.add_argument('--limit', type=int, default=64)
    args = p.parse_args()
    slots = client_slots(args.run)
    sides = side_logs(args.run, slots)
    side = compare_side(sides) if len(slots) == 2 else {f'client{k}': compare_side([sides[0], sides[i]]) for i, k in enumerate(slots[1:], 1)}
    result = dict(world=census(args.run, args.limit, slots, created_names(sides)), side=side)
    barrier = args.run/'native-barrier.json'
    if barrier.is_file():
        b = json.loads(barrier.read_text())
        result['barrier'] = {k: b.get(k) for k in ('verdict', 'failure', 'observe', 'first_digest_mismatch', 'digest_mismatch_ticks')}
        if len(slots) > 2 or 'digest_mismatch_slots' in b:
            result['barrier'].update(clients=b.get('clients'), digest_mismatch_slots=b.get('digest_mismatch_slots'))
        result['barrier']['ticks'] = max((int(t) for t in b.get('states', {})), default=None)
    text = json.dumps(result, indent=2)
    if args.out: args.out.write_text(text+'\n')
    print(text)


if __name__ == '__main__': main()
