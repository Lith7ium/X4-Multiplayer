"""Copy display/control preferences into an owned internal profile, never saves/mod selection."""

from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import tempfile
import xml.etree.ElementTree as ET

# Isolated multiplayer test profiles (one per client on a shared PC); never a settings source.
TEST_PROFILES = {str(45841 + i) for i in range(8)}
SETTINGS_API = 1
INPUT_FILE = re.compile(r'inputmap(?:_[A-Za-z0-9_-]+)?\.xml\Z')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def xml_root(data, name):
    try:
        return ET.fromstring(data)
    except ET.ParseError as exc:
        raise ValueError('Invalid XML in ' + name + ': ' + str(exc)) from exc


def owned_profile(package_root, slot_validator=None):
    package = read_json(package_root / 'package.json')
    state = read_json(package_root / 'installation.json')
    if (
        package.get('channel') != 'internal'
        or not package.get('package_id')
        or state.get('package_id') != package['package_id']
    ):
        raise RuntimeError('Settings import requires this package\'s installed internal mod.')
    profile = Path(state['profile'])
    expected = str(package.get('test_profile', '45841'))
    if (
        expected not in TEST_PROFILES
        or not profile.is_absolute()
        or profile.name != expected
        or profile.resolve().name != expected
    ):
        raise RuntimeError('Unexpected test profile path.')
    marker = Path(state['game']) / 'extensions/x4_lan_lab/.x4lan-package-id'
    if marker.read_text(encoding='utf-8-sig') != package['package_id']:
        raise RuntimeError('The installed mod belongs to another package.')
    if (profile / 'save').is_symlink():
        raise RuntimeError('The test profile contains saves or a linked save folder.')
    if any((profile / 'save').glob('*.xml*')):
        if slot_validator is None:
            raise RuntimeError(
                'The test profile contains saves; a verified shared-world slot is required.'
            )
        slot_validator(profile)
    return profile


def candidates(profile):
    return sorted(
        candidate
        for candidate in profile.parent.iterdir()
        if candidate.is_dir()
        and candidate.name.isdigit()
        and candidate.name not in TEST_PROFILES
        and not candidate.is_symlink()
        and (candidate / 'config.xml').is_file()
    )


def prepare(profile, source):
    source = source.expanduser().resolve()
    if source == profile.resolve() or source.name in TEST_PROFILES:
        raise RuntimeError('Choose a normal game profile as the settings source.')
    config = source / 'config.xml'
    if config.is_symlink():
        raise RuntimeError('Refusing linked source settings.')
    tree = xml_root(config.read_bytes(), str(config))
    if tree.tag != 'root':
        raise RuntimeError('Unknown source config.xml format.')
    # Keep resolution, quality and controls; use borderless for background ticks.
    for name, value in {
        'autosave': 'false',
        'uisafemode': 'false',
        'fullscreen': 'false',
        'borderless': 'true',
    }.items():
        nodes = tree.findall(name)
        if not nodes:
            nodes = [ET.SubElement(tree, name)]
        for node in nodes:
            node.text = value
    files = {'config.xml': ET.tostring(tree, encoding='utf-8', xml_declaration=True)}
    for path in sorted(source.iterdir()):
        if not INPUT_FILE.fullmatch(path.name):
            continue
        if path.is_symlink() or not path.is_file():
            raise RuntimeError('Refusing linked or invalid input settings: ' + path.name)
        data = path.read_bytes()
        xml_root(data, str(path))
        files[path.name] = data
    before = {}
    for name in files:
        target = profile / name
        if target.is_symlink():
            raise RuntimeError('Refusing linked test settings: ' + name)
        before[name] = target.read_bytes() if target.exists() else None
    return source, files, before


def atomic_write(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=path.parent, prefix='.x4lan-settings-', delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def import_settings(
    package_root, source=None, keep=False, ask=input, slot_validator=None, force_select=False
):
    profile = owned_profile(package_root, slot_validator)
    if source and keep:
        raise ValueError('Choose a source profile or keep the test settings, not both.')
    if source is None and not keep:
        choices = candidates(profile)
        if len(choices) == 1 and not force_select:
            source = choices[0]
        elif choices:
            print('Choose the normal game profile whose display and controls you use:')
            for i, candidate in enumerate(choices, 1):
                print(f'{i}: {candidate}')
            answer = ask('Profile number (0 keeps the current test settings): ').strip()
            if not answer.isdigit() or int(answer) > len(choices):
                raise ValueError(
                    'Invalid settings profile selection. Run 01-INSTALL again to retry.'
                )
            if int(answer):
                source = choices[int(answer) - 1]
    record = {'profile': str(profile), 'source': None, 'files': []}
    record_path = package_root / 'settings-import.json'
    if source is None:
        atomic_write(record_path, (json.dumps(record, indent=2) + '\n').encode())
        print('Keeping current test settings / game defaults; no normal settings profile selected.')
        return record
    source, files, before = prepare(profile, Path(source))
    backup = (
        package_root
        / 'backups'
        / ('settings-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S-%fZ'))
    )
    backup.mkdir(parents=True)
    for name, data in before.items():
        if data is not None:
            (backup / name).write_bytes(data)
    record.update(source=str(source), files=sorted(files), backup=str(backup))
    (backup / 'before.json').write_text(
        json.dumps(
            {filename: contents is not None for filename, contents in before.items()}, indent=2
        )
        + '\n'
    )
    changed = []
    try:
        for name, data in files.items():
            target = profile / name
            current = target.read_bytes() if target.exists() else None
            if target.is_symlink() or current != before[name]:
                raise RuntimeError('Test settings changed during import: ' + name)
            atomic_write(target, data)
            changed.append(name)
        atomic_write(record_path, (json.dumps(record, indent=2) + '\n').encode())
    except Exception as error:
        failures = []
        for name in reversed(changed):
            try:
                if before[name] is None:
                    (profile / name).unlink()
                else:
                    atomic_write(profile / name, before[name])
            except OSError as exc:
                failures.append(str(exc))
        if failures:
            raise RuntimeError(
                'Settings import failed; rollback incomplete. Backup: '
                + str(backup)
                + '; '
                + '; '.join(failures)
            ) from error
        raise
    print('Display, graphics and controls copied from:', source)
    print('Previous test settings backed up to:', backup)
    print(
        'Test profile uses borderless mode for Alt+Tab, with autosave/protected UI mode disabled. Resolution and controls are preserved.'
    )
    return record
