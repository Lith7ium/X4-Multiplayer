"""Read-only exact-snapshot input preflight for the opt-in Euler import mode.

Counts and shape checks are preparation evidence, never live native receipts.
Private saved IDs/angles are digested rather than printed or written by default.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import portable_import_capability_v1 as cap
from portable_rotation_reference import degrees_to_radians


def one(parent, tag):
    nodes = parent.findall(tag)
    if len(nodes) != 1:
        raise ValueError('Euler preflight: missing or repeated ' + tag)
    return nodes[0]


def offset_input(offset):
    if offset.attrib or len(offset) > 32:
        raise ValueError('Euler preflight: offset shape')
    rotations = offset.findall('rotation')
    if (len(rotations) > 1 or len(offset.findall('position')) > 1
            or any(child.tag not in ('position', 'rotation') for child in offset)):
        raise ValueError('Euler preflight: unsupported orientation shape')
    if not rotations:
        return None
    rotation = rotations[0]
    if len(rotation) or any(axis not in cap.AXES for axis in rotation.attrib):
        raise ValueError('Euler preflight: rotation shape')
    values = [rotation.get(axis) for axis in cap.AXES]
    for value in values:
        # This uses the common parser, not either platform's native fallback.
        degrees_to_radians(value)
    return values


def scan(stream):
    components, active, records = [], None, []
    seen = set()
    counts = {role: {'euler': 0, 'absent': 0} for role in cap.ROLES}
    for event, node in ET.iterparse(stream, events=('start', 'end')):
        if event == 'start':
            if node.tag == 'component':
                components.append(node.get('id'))
            if node.tag == 'movement' and node.get('class') == 'navigation':
                if active is not None:
                    raise ValueError('Euler preflight: nested navigation')
                active = node
            continue
        if node is active:
            identity = components[-1] if components else None
            if not isinstance(identity, str) or not re.fullmatch(r'\[0x[0-9a-f]+\]', identity):
                raise ValueError('Euler preflight: missing component identity')
            saved = int(identity[3:-1], 16)
            if not 0 < saved < 1 << 64 or saved in seen:
                raise ValueError('Euler preflight: repeated or invalid navigation identity')
            seen.add(saved)
            position = one(node, 'position')
            row = {'saved': f'{saved:016x}', 'banks': {}}
            for role in cap.ROLES:
                values = offset_input(one(one(position, role), 'offset'))
                row['banks'][role] = values
                counts[role]['absent' if values is None else 'euler'] += 1
            records.append(row)
            active = None
        if active is None:
            node.clear()
        if node.tag == 'component':
            components.pop()
    if active is not None or components:
        raise ValueError('Euler preflight: incomplete XML')
    return {'navigation_records': len(records), 'banks': counts,
            'inputs_sha256': hashlib.sha256(cap.canonical(sorted(records, key=lambda r: r['saved']))).hexdigest()}


def preflight(snapshot):
    with snapshot.open('rb') as raw:
        before = hashlib.file_digest(raw, 'sha256').hexdigest()
    expected = cap.capability(before)
    with gzip.open(snapshot, 'rb') as stream:
        result = scan(stream)
    with snapshot.open('rb') as raw:
        after = hashlib.file_digest(raw, 'sha256').hexdigest()
    if after != before:
        raise ValueError('Euler preflight: snapshot changed while reading')
    if any(result[key] != expected[key] for key in ('navigation_records', 'banks')):
        raise ValueError('Euler preflight: snapshot navigation counts differ')
    return dict(result, snapshot_sha256=before, descriptor_sha256=cap.DESCRIPTOR_SHA256,
                scope='offline_saved_xml_only', native_load=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = preflight(args.snapshot)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        with args.report.open('x', encoding='utf-8', newline='\n') as output:
            output.write(text)
    print(text, end='')


if __name__ == '__main__':
    main()
