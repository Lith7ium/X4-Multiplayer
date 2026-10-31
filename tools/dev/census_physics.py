"""Jolt physics census: name the first physics input that differs between clients.

native_gate_physics_trace (lockstep_physics_trace.h) logs one PHYSICS_STEP row per
JPH::PhysicsSystem::Update call with order-independent digests over every body that
has a portable key (saved or created), and created bodies apart:

  manager   body manager slots, body count and free-list head (the next BodyIDs)
  ids/cids  BodyID and object layer per body (flags without the transient 0x08)
  active    active-list index per body (Jolt's traversal order; reported apart)
  flags     Jolt body flags (InvalidateContactCache is set for a frame by a shape
            change; reported apart)
  shape/cshape  the order of each body's compound children (SubShapeIDs)
  mass/cmass    each body's inverse mass, inverse inertia and inertia rotation
  pre/cpre  motion type, pose and velocities before the step (the inputs X4 set)
  cset      contact callbacks with a portable side as a set (ccset: created side)
  corder    the same callbacks in callback order
  post/cpost pose and velocities after the step

plus counts (bodies, keyed, created, local, missing, contacts...) and foreign callbacks
(cforeign: callbacks off the stepping thread, 0 expected). PHYSICS_BODY rows (created
bodies, pre and post state, BodyID) and PHYSICS_CONTACT rows (callbacks with a created
side) cover ticks 1..N. native_gate_body_order logs BODY_ORDER_ROW per object the
frame's physics creation pass handled: native position (pos, heap-address order) and
portable place (order). native_gate_shape_order logs SHAPE_ORDER_ROW per compound child
it moved back to its stand-in's index (reported per client: where a collision file
finished loading after the ship was built). native_gate_nav_octree logs NAV_OCTREE_ROW
per finished object nav octree build (the tick it became visible, portable key, flag);
per pair the builds visible at different ticks or on one client only.

Per pair of clients (client 1 against each other) the report gives the first tick of
each digest that differs and the verdict: the earliest of
  allocation (ids/cids) < shapes (shape/cshape/mass/cmass) < inputs (pre/cpre)
  < contacts (cset/ccset/counts) < contact_order (corder) < outputs (post/cpost)
at that tick, with the bodies whose BodyID, inputs or outputs differ and the contact
rows only one client has (manager, active and counts with unportable bodies are
reported, not decisive: Jolt sorts each island's contacts, so the active-list order
alone does not change a result, and client-local bodies move the free list). Body order: the first tick whose native order differs (the
pass ran in different heap-address order) and whose portable order differs.

  python tools/dev/census_physics.py <run>                    (client1..N/x4lan-debug.txt)
  python tools/dev/census_physics.py --logs a.txt b.txt [--out census.json]
"""
import argparse
import collections
import json
from pathlib import Path
import re
import sys

STEP = re.compile(r'NATIVE_GATE PHYSICS_STEP index=\d+ (?:(evicted)=1|tick=(\d+) step=(\d+) (.*))$')
BODY = re.compile(r'NATIVE_GATE PHYSICS_BODY index=\d+ (?:(evicted)=1|tick=(\d+) step=(\d+) scene=(\d+) key=([0-9a-f]{16}) klass=(\d) '
                  r'which=(\d) body=([0-9a-f]{8}) active=([0-9a-f]{8}) motion=(\d+) flags=([0-9a-f]{2}) layer=([0-9a-f]{4}) '
                  r'(?:children=(\d+) shape=([0-9a-f]{16}) (?:mass=([0-9a-f]{16}) )?)?pre=([0-9a-f]+) post=([0-9a-f]+))')
SHAPE_ORDER = re.compile(r'NATIVE_GATE SHAPE_ORDER_ROW index=\d+ (?:(evicted)=1|tick=(\d+) from=(\d+) to=(\d+) children=(\d+))')
CONTACT = re.compile(r'NATIVE_GATE PHYSICS_CONTACT index=\d+ (?:(evicted)=1|tick=(\d+) step=(\d+) ord=\d+ (.*))$')
ORDER = re.compile(r'NATIVE_GATE BODY_ORDER_ROW index=\d+ (?:(evicted)=1|tick=(\d+) mode=(\w+) count=\d+ pos=(\d+) order=(\d+) '
                   r'kind=(\w+) klass=(\d) key=([0-9a-f]{16}) drop=(\d) create=(\d))')
STATUS = re.compile(r'NATIVE_GATE PHYSICS_TRACE(?:_STOP| tick=\d+) (.*)$')
SORDER_STATUS = re.compile(r'NATIVE_GATE SHAPE_ORDER(?:_STOP| tick=\d+) (.*)$')
NOCT_ROW = re.compile(r'NATIVE_GATE NAV_OCTREE_ROW index=\d+ (?:(evicted)=1|tick=(\d+) key=([0-9a-f]{16}) klass=(\d) flag=(\d) joined=(\d) us=(\d+))')
# Optional fields after us= in any order (snapshot= comes before content= when the input snapshot is armed).
NOCT_CONTENT = re.compile(r' content=([0-9a-f]{16}) cells=(\d+)')
NOCT_STATUS = re.compile(r'NATIVE_GATE NAV_OCTREE(?:_STOP| tick=\d+) (.*)$')
NOCT_LOOK = re.compile(r'NATIVE_GATE NAV_OCTREE_LOOKUP index=\d+ (?:(evicted)=1|tick=(\d+) site=(\w+) key=([0-9a-f]{16}) klass=(\d) flag=(\d) '
                       r'ask=(\d) result=(\w+) count=(\d+))')
# The last nav_octree status per client (native_gate_nav_octree).
NOCT_FIELDS = ('mode', 'pops', 'builds', 'join_builds', 'free_builds', 'deferrals', 'waited', 'max_build_us', 'joins', 'max_join_us',
               'max_pending', 'held_deferred', 'snapshot', 'snap_captured', 'snap_used', 'snap_live', 'snap_waits', 'snap_gone', 'max_snap_us',
               'builder_view', 'main_view', 'stage', 'failures')
NOCT_SNAPSHOT = re.compile(r' snapshot=(\d)')
# The last shape_order status per client: whether the sync loads, in-place moves and mass requests happened.
SORDER_FIELDS = ('mode', 'total_moves', 'removals', 'appends', 'native', 'boot_moves', 'requests', 'cached', 'loads', 'resident', 'invalid',
                 'unloaded', 'passed', 'alt_keys', 'max_load_us', 'mass_forced', 'boot_forced', 'waits', 'max_wait_us', 'stage', 'failures')
# Decisive stages. manager, active and the counts that include bodies or callbacks
# without a portable key (bodies, local, contacts, clocal) are reported, not decisive:
# client-local bodies move the free list without touching a portable body.
STAGES = (('allocation', ('ids', 'cids')), ('shapes', ('shape', 'cshape', 'mass', 'cmass')), ('inputs', ('pre', 'cpre')),
          ('contacts', ('cset', 'ccset', 'ckeyed', 'ccreated', 'flips', 'sensors')),
          ('contact_order', ('corder',)), ('outputs', ('post', 'cpost')))
REPORTED = ('manager', 'ids', 'cids', 'active', 'flags', 'shape', 'cshape', 'mass', 'cmass', 'pre', 'cpre', 'cset', 'ccset', 'corder', 'post', 'cpost',
            'bodies', 'keyed', 'created', 'local', 'missing', 'contacts', 'clocal', 'result', 'det', 'cforeign')


def fields(text):
    return dict(part.split('=', 1) for part in text.split() if '=' in part)


def parse(lines):
    """One client: steps {(tick, step): fields}, bodies {tick: {body key: row}}, contacts {(tick, step): [rows]},
    body order {tick: [rows]}, evicted counts, last trace status."""
    steps, bodies, contacts, order = {}, collections.defaultdict(dict), collections.defaultdict(list), collections.defaultdict(list)
    moves = collections.defaultdict(list)
    evicted = collections.Counter()
    status, sorder, noct = {}, {}, {}
    octrees, local_octrees, lookups, contents, snapshots = {}, 0, collections.defaultdict(collections.Counter), {}, collections.Counter()
    for line in lines:
        if 'PHYSICS_' not in line and 'BODY_ORDER_ROW' not in line and 'SHAPE_ORDER' not in line and 'NAV_OCTREE' not in line:
            continue
        if (m := STEP.search(line)):
            if m[1]: evicted['step'] += 1
            else: steps[(int(m[2]), int(m[3]))] = fields(m[4])
        elif (m := BODY.search(line)):
            if m[1]: evicted['body'] += 1; continue
            key = (int(m[3]), int(m[4]), int(m[6]), m[5], int(m[7]))
            bodies[int(m[2])][key] = dict(body=m[8], active=m[9], motion=m[10], flags=m[11], layer=m[12], children=m[13], shape=m[14],
                                          mass=m[15], pre=m[16], post=m[17])
        elif (m := CONTACT.search(line)):
            if m[1]: evicted['contact'] += 1
            else: contacts[(int(m[2]), int(m[3]))].append(m[4])
        elif (m := ORDER.search(line)):
            if m[1]: evicted['order'] += 1; continue
            order[int(m[2])].append(dict(mode=m[3], pos=int(m[4]), order=int(m[5]), kind=m[6], klass=int(m[7]), key=m[8],
                                         drop=int(m[9]), create=int(m[10])))
        elif (m := SHAPE_ORDER.search(line)):
            if m[1]: evicted['shape_order'] += 1
            else: moves[int(m[2])].append(dict(source=int(m[3]), to=int(m[4]), children=int(m[5])))
        elif (m := STATUS.search(line)):
            status = fields(m[1])
        elif (m := SORDER_STATUS.search(line)):
            sorder = fields(m[1])
        elif (m := NOCT_ROW.search(line)):
            if m[1]: evicted['nav_octree'] += 1
            elif m[4] == '2': local_octrees += 1  # no portable key: process local
            else:
                octrees.setdefault((m[3], int(m[5])), int(m[2]))
                if (f := NOCT_SNAPSHOT.search(line)): snapshots[f[1]] += 1
                # Only octrees built here carry content; a no-op build (the object's shared octree already existed) or a
                # build without a digest has content=0 cells=0 and is not compared.
                if (f := NOCT_CONTENT.search(line)) and f[1] != '0' * 16 and int(f[2]):
                    contents.setdefault((m[3], int(m[5])), (f[1], int(f[2])))
        elif (m := NOCT_LOOK.search(line)):
            if m[1]: evicted['nav_octree_lookup'] += 1
            else: lookups[int(m[2])][(m[3], m[4], int(m[5]), int(m[6]), int(m[7]), m[8])] += int(m[9])
        elif (m := NOCT_STATUS.search(line)):
            noct = fields(m[1])
    return dict(steps=steps, bodies=dict(bodies), contacts=dict(contacts), order=dict(order), evicted=dict(evicted), status=status,
                moves=dict(moves), sorder=sorder, octrees=octrees, local_octrees=local_octrees, noct=noct, lookups=dict(lookups),
                contents=contents, snapshot_rows=dict(snapshots))


def first_difference(a, b, field):
    for key in sorted(set(a) | set(b)):
        x, y = a.get(key), b.get(key)
        if x is None or y is None:
            return dict(tick=key[0], step=key[1], present=[x is not None, y is not None])
        if x.get(field) != y.get(field):
            return dict(tick=key[0], step=key[1], values=[x.get(field), y.get(field)])
    return None


def verdict(a, b):
    """The earliest (tick, step) with a difference and the first stage that differs there."""
    for key in sorted(set(a) | set(b)):
        x, y = a.get(key), b.get(key)
        if x is None or y is None:
            return dict(tick=key[0], step=key[1], stage='step_missing', fields=[])
        for stage, names in STAGES:
            differing = [n for n in names if x.get(n) != y.get(n)]
            if differing:
                return dict(tick=key[0], step=key[1], stage=stage, fields=differing)
    return None


def body_differences(a, b, tick, limit=8):
    x, y = a.get(tick, {}), b.get(tick, {})
    joined = sorted(set(x) & set(y))
    out = {'only': [[list(k) for k in sorted(set(x) - set(y))][:limit], [list(k) for k in sorted(set(y) - set(x))][:limit]]}
    for name in ('body', 'active', 'flags', 'children', 'shape', 'mass', 'pre', 'post'):
        rows = [dict(step=k[0], scene=k[1], klass=k[2], key=k[3], which=k[4], values=[x[k][name], y[k][name]])
                for k in joined if x[k][name] != y[k][name]]
        out[name] = dict(count=len(rows), examples=rows[:limit])
    return out


def contact_differences(a, b, key, limit=8):
    x, y = collections.Counter(a.get(key, [])), collections.Counter(b.get(key, []))
    return dict(tick=key[0], step=key[1], rows=[sum(x.values()), sum(y.values())], same_multiset=x == y,
                same_order=a.get(key, []) == b.get(key, []),
                client1_only=sorted((x - y).elements())[:limit], other_only=sorted((y - x).elements())[:limit])


def first_contact_difference(a, b, limit=8):
    for key in sorted(set(a) | set(b)):
        if a.get(key, []) != b.get(key, []):
            return contact_differences(a, b, key, limit)
    return None


def order_sequence(rows, field):
    return [(r['klass'], r['key'], r['drop'], r['create']) for r in sorted(rows, key=lambda r: r[field])]


def first_order_difference(a, b, field, limit=8):
    for tick in sorted(set(a) | set(b)):
        x, y = order_sequence(a.get(tick, []), field), order_sequence(b.get(tick, []), field)
        if x != y:
            at = next((i for i, (p, q) in enumerate(zip(x, y)) if p != q), min(len(x), len(y)))
            return dict(tick=tick, entries=[len(x), len(y)], same_set=sorted(x) == sorted(y), first_position=at,
                        client1=[list(e) for e in x[at:at+limit]], other=[list(e) for e in y[at:at+limit]])
    return None


def octree_differences(a, b, limit=8):
    """Octree builds (portable key, flag) by the tick they became visible: those at other ticks or on one client only."""
    joined = sorted(set(a) & set(b))
    differing = [dict(key=k[0], flag=k[1], ticks=[a[k], b[k]]) for k in joined if a[k] != b[k]]
    differing.sort(key=lambda d: (min(d['ticks']), d['key'], d['flag']))
    return dict(count=len(differing), first=differing[:limit],
                only=[[list(k) for k in sorted(set(a) - set(b))][:limit], [list(k) for k in sorted(set(b) - set(a))][:limit]])


def content_differences(a, b, limit=8):
    """Octrees built on both clients whose content digest differs (built from another moment's object state)."""
    rows = [dict(key=k[0], flag=k[1], content=[a[k][0], b[k][0]], cells=[a[k][1], b[k][1]]) for k in sorted(set(a) & set(b)) if a[k] != b[k]]
    return dict(compared=len(set(a) & set(b)), count=len(rows), rows=rows[:limit])


def lookup_differences(a, b, limit=8):
    """The first tick whose octree lookups/invalidations differ: (site, key, class, flag, ask, result) -> count on each side."""
    for tick in sorted(set(a) | set(b)):
        x, y = a.get(tick, collections.Counter()), b.get(tick, collections.Counter())
        if x != y:
            rows = [dict(site=k[0], key=k[1], klass=k[2], flag=k[3], ask=k[4], result=k[5], counts=[x.get(k, 0), y.get(k, 0)])
                    for k in sorted(set(x) | set(y)) if x.get(k, 0) != y.get(k, 0)]
            return dict(tick=tick, count=len(rows), rows=rows[:limit])
    return None


def compare(clients, limit=8):
    reference = clients[0]
    result = {'steps': [len(c['steps']) for c in clients], 'evicted': [c['evicted'] for c in clients],
              'status': [{k: c['status'].get(k) for k in ('foreign', 'foreign_steps', 'stray', 'failures', 'stage')} for c in clients],
              'foreign_callbacks': [sum(int(s.get('cforeign', 0)) for s in c['steps'].values()) for c in clients],
              'step_errors': [sorted({s.get('result') for s in c['steps'].values()} - {'0'}) for c in clients],
              'shape_order_moves': [{str(t): len(v) for t, v in sorted(c['moves'].items())} for c in clients],
              'shape_order': [{k: c['sorder'][k] for k in SORDER_FIELDS if k in c['sorder']} for c in clients],
              'nav_octree': [dict({k: c['noct'][k] for k in NOCT_FIELDS if k in c['noct']}, rows=len(c['octrees']), local=c['local_octrees'],
                                  snapshot_rows=c['snapshot_rows']) for c in clients],
              'pairs': []}
    for number, other in enumerate(clients[1:], 2):
        pair = {'client': number, 'first': {name: first_difference(reference['steps'], other['steps'], name) for name in REPORTED}}
        pair['verdict'] = verdict(reference['steps'], other['steps'])
        if pair['verdict']:
            tick = pair['verdict']['tick']
            pair['bodies'] = body_differences(reference['bodies'], other['bodies'], tick, limit)
        pair['contacts'] = first_contact_difference(reference['contacts'], other['contacts'], limit)
        pair['nav_octree'] = octree_differences(reference['octrees'], other['octrees'], limit)
        pair['nav_octree_lookups'] = lookup_differences(reference['lookups'], other['lookups'], limit)
        pair['nav_octree_content'] = content_differences(reference['contents'], other['contents'], limit)
        pair['first_body_rows'] = next((dict(tick=t, **body_differences(reference['bodies'], other['bodies'], t, limit))
                                        for t in sorted(set(reference['bodies']) | set(other['bodies']))
                                        if reference['bodies'].get(t) != other['bodies'].get(t)), None)
        pair['body_order'] = {'native': first_order_difference(reference['order'], other['order'], 'pos', limit),
                              'portable': first_order_difference(reference['order'], other['order'], 'order', limit)}
        result['pairs'].append(pair)
    return result


def client_logs(run):
    logs = sorted(Path(run).glob('client*/x4lan-debug.txt')) or sorted(Path(run).glob('client*/engine-output.log'))
    if len(logs) < 2:
        raise ValueError(f'{run}: needs client1..N/x4lan-debug.txt')
    return logs


def read(path):
    with open(path, encoding='utf-8', errors='replace') as handle:
        return parse(handle)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path, nargs='?')
    p.add_argument('--logs', type=Path, nargs='+')
    p.add_argument('--limit', type=int, default=8)
    p.add_argument('--out', type=Path)
    args = p.parse_args(argv)
    try:
        logs = args.logs or client_logs(args.run)
        if len(logs) < 2:
            raise ValueError('needs at least two client logs')
        result = compare([read(path) for path in logs], args.limit)
    except (ValueError, OSError) as error:
        p.error(str(error))
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.out:
        args.out.write_text(text + '\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
