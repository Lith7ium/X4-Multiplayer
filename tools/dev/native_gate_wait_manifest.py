"""Validate a selected Wait observer manifest against its immutable snapshot."""
import hashlib
import math
import re
import struct


def double_bits(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError('wait manifest needs finite numeric seconds')
    try:
        value = float(value)
    except OverflowError as error:
        raise ValueError('wait manifest seconds exceed double range') from error
    if not math.isfinite(value):
        raise ValueError('wait manifest needs finite numeric seconds')
    return f'{struct.unpack("<Q", struct.pack("<d", value))[0]:016x}'


def windows_saved_time_bits(text):
    """Pinned 14c3090 plain-decimal path, with each SSE2 operation rounded.

    This bounded subset deliberately excludes signs, exponents and whitespace.
    It is for persisted saved time only, not compiled expression constants.
    Linux's analogous parser divides the scale by ten; do not assume parity.
    """
    if not isinstance(text, str) or not re.fullmatch(r'(?:0|[1-9][0-9]{0,14})(?:\.[0-9]{1,15})?', text):
        raise ValueError('unsupported saved time text; requires bounded plain decimal')
    total = 0.0
    scale = 0.0
    for char in text:
        if char == '.':
            scale = 1.0
        elif scale == 0.0:
            # Native integer path adds ASCII before subtracting 48. Preserve
            # these separate binary64 operations rather than use int(text).
            total = total * 10.0
            total = total + float(ord(char))
            total = total - 48.0
        else:
            scale = scale * 0.1
            digit = float(ord(char) - 48) * scale
            total = total + digit
    return double_bits(total)


def selected_time_bits(entry):
    """Legacy numeric selectors remain unchanged, including historical failures."""
    if ('time' in entry) == ('time_text' in entry):
        raise ValueError('wait selection requires exactly one saved time representation')
    return windows_saved_time_bits(entry['time_text']) if 'time_text' in entry else double_bits(entry['time'])


def wait_manifest(manifest, snapshot, snapshot_starts):
    if set(manifest) != {'schema', 'snapshot_sha256', 'entries'} or type(manifest['schema']) is not int or manifest['schema'] not in (1, 2):
        raise ValueError('unsupported wait manifest schema')
    time_key = 'time' if manifest['schema'] == 1 else 'time_text'
    checksum = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    if manifest['snapshot_sha256'] != checksum:
        raise ValueError('wait manifest snapshot mismatch')
    entries = manifest['entries']
    if not isinstance(entries, list) or not 1 <= len(entries) <= 8:
        raise ValueError('wait manifest requires 1..8 selected actions')
    expected = {}
    for entry in entries:
        if set(entry) != {'id', 'name', 'version', 'index', time_key, 'minimum', 'maximum'}:
            raise ValueError('unexpected wait manifest fields')
        if not isinstance(entry['id'], str) or not re.fullmatch('[0-9a-f]{16}', entry['id']) or not int(entry['id'], 16):
            raise ValueError('invalid saved pilot identity')
        if not isinstance(entry['name'], str) or not re.fullmatch('[A-Za-z0-9_.]{1,95}', entry['name']):
            raise ValueError('invalid script name')
        for key, limit in (('version', 0x7fffffff), ('index', 65535)):
            if type(entry[key]) is not int or not 0 <= entry[key] <= limit:
                raise ValueError('invalid saved version or XML blocking index')
        selected_time_bits(entry)
        for key in ('minimum', 'maximum'):
            double_bits(entry[key])
        if (time_key == 'time' and entry['time'] < 0) or entry['minimum'] < 0 or entry['maximum'] <= entry['minimum']:
            raise ValueError('invalid saved time or constant wait range')
        key = (entry['id'], entry['name'])
        if key in expected:
            raise ValueError('duplicate selected saved action')
        expected[key] = entry
    versions = {entry['name']: [] for entry in entries}
    found = {key: [] for key in expected}
    for nodes in snapshot_starts(snapshot):
        path = [node.tag for node in nodes]
        node = nodes[-1]
        if path == ['savegame', 'aidirector', 'patch', 'script'] and node.get('name') in versions:
            versions[node.get('name')].append(node.get('version'))
        elif path == ['savegame', 'aidirector', 'entity', 'script']:
            identity = nodes[2].get('id', '')
            if not re.fullmatch(r'\[0x[0-9a-f]{1,16}\]', identity):
                continue
            key = (f'{int(identity[3:-1],16):016x}', node.get('name'))
            if key in found:
                found[key].append(dict(node.attrib))
    wire = []
    for key, entry in expected.items():
        if len(found[key]) != 1 or versions[entry['name']] != [str(entry['version'])]:
            raise ValueError('selected saved script/version missing or duplicated')
        saved = found[key][0]
        same_time = (saved.get('time') == entry['time_text'] if time_key == 'time_text' else
                     double_bits(float(saved.get('time', 'nan'))) == double_bits(entry['time']))
        if (saved.get('index') != str(entry['index']) or
                not same_time or
                ('version' in saved and saved['version'] != str(entry['version']))):
            raise ValueError('selected saved blocking action does not match manifest')
        wire.append(','.join([entry['id'], str(entry['version']), str(entry['index']),
                             selected_time_bits(entry), double_bits(entry['minimum']),
                             double_bits(entry['maximum']), entry['name']]))
    if hashlib.sha256(snapshot.read_bytes()).hexdigest() != checksum:
        raise ValueError('snapshot changed while validating wait manifest')
    result = ';'.join(wire)
    if len(result) >= 2048:
        raise ValueError('wait manifest exceeds native bound')
    return result
