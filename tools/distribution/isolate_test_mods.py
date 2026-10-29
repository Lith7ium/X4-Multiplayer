"""Explicit, package-local recovery helper for the internal 45841 test profile."""

from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def ensure_closed():
    if sys.platform != 'win32':
        raise RuntimeError('Run this recovery helper on the Windows test PC.')
    result = subprocess.run(
        ['tasklist.exe', '/FI', 'IMAGENAME eq X4.exe', '/FO', 'CSV', '/NH'],
        capture_output=True,
        text=True,
        check=True,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if '"x4.exe"' in result.stdout.lower():
        raise RuntimeError('Close X4 completely, then run this helper again.')


def plan(package_root):
    """Read only: validate ownership and discover actual extension IDs."""
    package = read_json(package_root / 'package.json')
    state = read_json(package_root / 'installation.json')
    if (
        package.get('channel') != 'internal'
        or package.get('slot') not in ('p1', 'p2')
        or not package.get('package_id')
        or state.get('package_id') != package['package_id']
        or state.get('version') != package.get('version')
    ):
        raise RuntimeError('A matching installed internal package is required.')
    profile = Path(state['profile'])
    game = Path(state['game'])
    if (
        not profile.is_absolute()
        or profile.name != '45841'
        or profile.resolve().name != '45841'
        or not (profile / 'config.xml').is_file()
        or not game.is_absolute()
    ):
        raise RuntimeError('Refusing an unexpected or missing test profile.')
    marker = game / 'extensions/x4_lan_lab/.x4lan-package-id'
    if marker.read_text(encoding='utf-8-sig') != package['package_id']:
        raise RuntimeError('The installed mod belongs to another package.')
    target = profile / 'content.xml'
    if target.is_symlink():
        raise RuntimeError('Refusing a linked content.xml.')
    before = target.read_bytes() if target.exists() else None
    tree = ET.fromstring(before) if before is not None else ET.Element('content')
    if tree.tag != 'content':
        raise RuntimeError('Unexpected profile content.xml format.')
    ids = set()
    for entry in tree.findall('extension'):
        if not entry.get('id'):
            raise RuntimeError('A profile extension entry has no ID.')
        ids.add(entry.get('id'))
    # These are extension locations only, not other player profiles or saves.
    roots = (game / 'extensions', profile / 'extensions', profile.parent / 'extensions')
    for root in roots:
        if not root.exists():
            continue
        for folder in sorted(root.iterdir()):
            if not folder.is_dir() or folder.name.startswith('.'):
                continue
            manifest = folder / 'content.xml'
            compressed = folder / 'content.xml.gz'
            if manifest.is_file():
                raw = manifest.read_bytes()
            elif compressed.is_file():
                with gzip.open(compressed, 'rb') as stream:
                    raw = stream.read(2_000_001)
            else:
                continue
            if len(raw) > 2_000_000:
                raise RuntimeError('Extension manifest too large: ' + str(folder))
            extension = ET.fromstring(raw)
            extension_id = extension.get('id')
            if extension.tag != 'content' or not extension_id:
                raise RuntimeError('Invalid extension manifest: ' + str(folder))
            ids.add(extension_id)
    own_manifest = ET.parse(game / 'extensions/x4_lan_lab/content.xml').getroot()
    if own_manifest.get('id') != 'x4_lan_lab':
        raise RuntimeError('Unexpected installed multiplayer extension ID.')
    ids.update(('x4_lan_lab', 'ego_dlc_ventures'))
    disabled = []
    for extension_id in sorted(ids):
        entries = [
            extension_entry
            for extension_entry in tree.findall('extension')
            if extension_entry.get('id') == extension_id
        ]
        if not entries:
            entries = [ET.SubElement(tree, 'extension', id=extension_id)]
        if extension_id == 'x4_lan_lab':
            enabled = 'true'
        elif extension_id.startswith('ego_dlc_') and extension_id != 'ego_dlc_ventures':
            # Preserve DLC that the tester deliberately disabled.
            enabled = (
                'false'
                if any(
                    extension_entry.get('enabled', '').lower() in ('false', '0')
                    for extension_entry in entries
                )
                else 'true'
            )
        else:
            enabled = 'false'
            disabled.append(extension_id)
        for entry in entries:
            entry.set('enabled', enabled)
    ET.indent(tree)
    after = ET.tostring(tree, encoding='utf-8', xml_declaration=True) + b'\n'
    return target, before, after, disabled


def apply(package_root, proposed):
    # Withdrawal is unconditional: a local selection cannot prove Steam subscription state.
    raise RuntimeError(
        'WITHDRAWN: Workshop subscriptions were reported lost. '
        'Do not isolate mods with this helper. Use 10-RECOVER-WORKSHOP.cmd.'
    )


def main():
    parser = argparse.ArgumentParser(
        description='Isolate mods for the installed internal test profile only.'
    )
    parser.add_argument(
        '--check', action='store_true', help='Print the proposed selection without writing files'
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    try:
        if not args.check:
            apply(root, None)
        ensure_closed()
        proposed = plan(root)
        print('Test profile:', proposed[0].parent)
        print(
            'Multiplayer enabled; existing official DLC choices retained; Ventures and other mods disabled.'
        )
        print('Disabled extension IDs:', ', '.join(proposed[3]))
        print('WITHDRAWN HELPER - CHECK ONLY: no files changed. Do not apply this selection.')
        return 0
    except (
        OSError,
        ValueError,
        RuntimeError,
        KeyError,
        ET.ParseError,
        subprocess.SubprocessError,
    ) as exc:
        print('ERROR:', exc, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
