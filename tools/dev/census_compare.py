"""Compare one client of any census run with one client of any other, at one capture tick.

census_world.py compares the clients of one run, capture by capture. This compares across
runs, for example a Windows client with a Linux client of the same snapshot, and lists the
differing objects by class. Object and controller types are Windows vtable RVAs in both
logs, so class names come from the committed Linux class table (gate_world_classes.h).

  python tools/dev/census_compare.py LEFT_RUN RIGHT_RUN --tick 1
         [--left-client 1] [--right-client 1] [--samples 6] [--watch-list 32]
         [--snapshot save_009.xml.gz] [--out result.json]

--snapshot adds the saved component's class and macro from the owned snapshot to each sample
and groups the differing objects by macro. Read only. The timing domain has its preimage in
the log, so its differing fields are named; motion and health only have a hash there, unless
the object is a world hash watch in both runs (native_gate_world_hash_watch), when its
decoded fields are compared too. --watch-list prints a watch list for the next pair of runs.
"""
import argparse
import collections
import gzip
import json
import re
from pathlib import Path

from census_clients import client_slots
from compare_native_world import bounded_lines
from native_world_evidence import DOMAINS, configured_schema, configured_watches, decode_detail, frames, is_created

ROOT = Path(__file__).resolve().parents[2]
CLASS_TABLE = ROOT / 'src/native/engine/linux/gate_world_classes.h'
TABLE_ROW = re.compile(r'\{0x[0-9a-f]+,0x([0-9a-f]+),0x[0-9a-f]+,\d+,-?\d+\}, /\* \.\?A[VU](\S+?)@@ \*/')
COMPONENT = re.compile(rb'<component class="([^"]*)" macro="([^"]*)"[^>]*? id="\[0x([0-9a-f]+)\]"')
# The WORLD_IMPORTED_2 timing preimage of a nav object (native_world_evidence.nav_timing).
NAV_TIMING = (('attention', 0, 4), ('nav.refresh_time', 4, 12), ('nav.refresh_relation', 12, 16), ('nav.parameters_due', 16, 24),
              ('nav.parameters_gate', 24, 25), ('bank0.eta', 25, 33), ('bank0.arrived', 33, 34), ('bank0.notified', 34, 35),
              ('bank1.eta', 35, 43), ('bank1.arrived', 43, 44), ('bank1.notified', 44, 45), ('bank0.pending', 45, 62), ('bank1.pending', 62, 79))


def class_names():
    return {int(rva, 16): name.replace('@', '::') for rva, name in TABLE_ROW.findall(CLASS_TABLE.read_text())}


def snapshot_components(path):
    """saved ID -> (class, macro) of the snapshot's components."""
    with gzip.open(path, 'rb') as stream:
        data = stream.read()
    return {int(m[3], 16): (m[1].decode('ascii', 'replace'), m[2].decode('ascii', 'replace')) for m in COMPONENT.finditer(data)}


def capture(run, slot, tick):
    """The object map of one client at the capture of that tick (later captures are not read)."""
    client = run / f'client{slot}'
    config = (client / 'installed/x4_lan_lab-config.lua').read_text()
    errors = []
    seen = []
    for frame in frames(bounded_lines(client / 'engine-output.log'), errors, slot, configured_watches(config), True, configured_schema(config)):
        seen.append(frame['tick'])
        if frame['tick'] == tick:
            return {'run': run.name, 'client': slot, 'tick': tick, 'objects': frame['objects'], 'coverage': frame['coverage'],
                    'details': frame['details'], 'schema': frame['schema'], 'errors': errors}
        if frame['tick'] > tick:
            break
    raise SystemExit(f'{run.name} client {slot} has no capture at tick {tick} (captures {seen[:8]})')


def timing_difference(x, y):
    """Names of the differing timing fields, from the preimages in the two rows."""
    left, right = x.get('timing_data', b'')[:-16], y.get('timing_data', b'')[:-16]
    if len(left) != len(right):
        return [f'length {len(left)}/{len(right)}']
    if len(left) == 4:
        return ['attention'] if left != right else []
    if len(left) == 79:
        return [name for name, begin, end in NAV_TIMING if left[begin:end] != right[begin:end]]
    return ['other'] if left != right else []


def detail_difference(left, right, schema):
    """Decoded fields of an object watched in both runs that differ, with both values."""
    a, b = decode_detail(left, schema), decode_detail(right, schema)
    out = {}
    for domain in DOMAINS:
        fields_a, fields_b = a.get(domain, {}), b.get(domain, {})
        for name in fields_a.keys() | fields_b.keys():
            if fields_a.get(name) != fields_b.get(name):
                out[f'{domain}.{name}'] = [fields_a.get(name), fields_b.get(name)]
    return out


def compare(left, right, names, components=None, samples=6):
    a, b = left['objects'], right['objects']
    common = a.keys() & b.keys()
    imported = {i for i in common if not is_created(i)}
    only_left, only_right = a.keys() - b.keys(), b.keys() - a.keys()
    domain_counts, combos, timing_fields = collections.Counter(), collections.Counter(), collections.Counter()
    by_class = collections.defaultdict(lambda: dict(total=0, present=0, differing=0, **{d: 0 for d in DOMAINS}, samples=[]))
    by_macro = collections.Counter()
    meta_differs = collections.Counter()
    differing = []
    for identity in sorted(common):
        x, y = a[identity], b[identity]
        name = names.get(x['type'], f"{x['type']:#x}" if x['type'] else 'absent')
        row = by_class[name]
        row['total'] += 1
        row['present'] += bool(x['flags'])
        changed = tuple(d for d, h1, h2 in zip(DOMAINS, x['hashes'], y['hashes']) if h1 != h2)
        for key in ('type', 'flags', 'parent'):
            if x[key] != y[key]:
                meta_differs[key] += 1
        if not changed:
            continue
        row['differing'] += 1
        combos['+'.join(changed)] += 1
        for domain in changed:
            domain_counts[domain] += 1
            row[domain] += 1
        fields = timing_difference(x, y) if 'timing' in changed else []
        for field in fields:
            timing_fields[field] += 1
        entry = dict(saved=f'{identity:016x}', domains=list(changed), flags=[x['flags'], y['flags']], parent=f"{x['parent']:016x}")
        if fields:
            entry['timing_fields'] = fields
        if components is not None:
            component = components.get(identity)
            parent = components.get(x['parent'])
            entry['component'] = component
            entry['parent_component'] = parent
            by_macro[(component or ('?', '?'))[1]] += 1
        if len(row['samples']) < samples:
            row['samples'].append(entry)
        differing.append((identity, changed))
    details = {}
    for identity in left['details'].keys() & right['details'].keys():
        difference = detail_difference(left['details'][identity], right['details'][identity], left['schema'])
        if difference:
            details[f'{identity:016x}'] = difference
    ordered = sorted(by_class.items(), key=lambda kv: (-kv[1]['differing'], kv[0]))
    return dict(
        left=dict(run=left['run'], client=left['client'], coverage=left['coverage'], errors=left['errors'][:8]),
        right=dict(run=right['run'], client=right['client'], coverage=right['coverage'], errors=right['errors'][:8]),
        tick=left['tick'], common=len(common), common_saved=len(imported), only_left=len(only_left), only_right=len(only_right),
        all_equal=len(common) - len(differing), differing=len(differing), by_domain=dict(domain_counts), combinations=dict(combos),
        metadata_differs=dict(meta_differs), timing_fields=dict(timing_fields.most_common()),
        by_class=[dict(name=name, **row) for name, row in ordered if row['differing']],
        classes_equal=sum(1 for _, row in ordered if not row['differing']),
        by_macro=dict(by_macro.most_common(40)), watched_details=details), differing


def text_report(result):
    lines = [f"{result['left']['run']}/client{result['left']['client']} vs {result['right']['run']}/client{result['right']['client']} at tick {result['tick']}",
             f"common {result['common']} (saved {result['common_saved']}), only left {result['only_left']}, only right {result['only_right']}",
             f"all equal {result['all_equal']}, differing {result['differing']}: {result['by_domain']}",
             f"combinations {result['combinations']}", f"metadata differs {result['metadata_differs']}"]
    if result['timing_fields']:
        lines.append(f"timing fields {result['timing_fields']}")
    lines.append(f"{'class':44} {'present':>8} {'differ':>7} {'topo':>6} {'health':>7} {'motion':>7} {'timing':>7}")
    for row in result['by_class']:
        lines.append(f"{row['name'][:44]:44} {row['present']:8} {row['differing']:7} {row['topology']:6} {row['health']:7} {row['motion']:7} {row['timing']:7}")
    if result['by_macro']:
        lines.append(f"by macro {result['by_macro']}")
    return '\n'.join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('left', type=Path)
    p.add_argument('right', type=Path)
    p.add_argument('--tick', type=int, default=1)
    p.add_argument('--left-client', type=int)
    p.add_argument('--right-client', type=int)
    p.add_argument('--samples', type=int, default=6)
    p.add_argument('--watch-list', type=int, help='print a native_gate_world_hash_watch list of this many differing objects, spread over classes')
    p.add_argument('--snapshot', type=Path)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    left_slot = args.left_client or client_slots(args.left)[0]
    right_slot = args.right_client or client_slots(args.right)[0]
    components = snapshot_components(args.snapshot) if args.snapshot else None
    left, right = capture(args.left, left_slot, args.tick), capture(args.right, right_slot, args.tick)
    result, differing = compare(left, right, class_names(), components, args.samples)
    print(text_report(result))
    if args.watch_list:
        picked, classes = [], collections.Counter()
        for identity, _ in sorted(differing, key=lambda item: classes.update([left['objects'][item[0]]['type']]) or classes[left['objects'][item[0]]['type']]):
            picked.append(f'{identity:016x}')
            if len(picked) == args.watch_list:
                break
        print('watch list: {' + ','.join(f"'{saved}'" for saved in sorted(picked)) + '}')
    if args.out:
        args.out.write_text(json.dumps(result, indent=1, default=str) + '\n')


if __name__ == '__main__':
    main()
