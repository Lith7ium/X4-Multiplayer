"""Closed portable fixture schema; no process access and no gameplay emulation.

Only the declared twelve saved ShipSmall/Navigation components are compared.
Raw IEEE bits are retained; pointers, platform RVAs and wall-clock epochs are not
portable state. A matching subset must never be labelled whole-game lockstep.
"""
import hashlib
import json
import math
import re
import struct

SCHEMA = 'x4-portable-fixture-v1'
COVERAGE = ('identity', 'topology', 'stored_hull', 'navigation_transform', 'navigation_references')
UNSUPPORTED = ('shields', 'velocities', 'navigation_flags', 'ai', 'rng', 'weapons',
               'cargo', 'economy', 'missions', 'new_or_destroyed_objects', 'other_components')
SCHEMA_DESCRIPTION = {
    'id': SCHEMA, 'endian': 'little', 'actors': 12, 'coverage_mask': 31,
    'row_prefix': 'X4P1ROW\\0',
    'row_fields': ['saved:u64', 'fixture_slot:u16', 'present:u8=1', 'class:u8=1',
                   'controller:u8=1', 'coverage:u32=31', 'macro:u8_length+ascii',
                   'owner:u8_length+ascii', 'parent_saved:u64', 'hull_bits:u64',
                   'active:12xf32_bits+ref_b0_saved:u64+ref_b8_saved:u64',
                   'other:12xf32_bits+ref_b0_saved:u64+ref_b8_saved:u64'],
    'frame_prefix': 'X4P1FRM\\0',
    'frame_fields': ['contract:32bytes', 'tick:u64', 'game_bits:u64', 'calls:u64',
                     'returns:u64', 'receipt_tick:u64', 'slot1_command:u32=0',
                     'slot2_command:u32=0', 'actor_count:u16=12',
                     'rows:savedID_sorted(u16_length+row_bytes)'],
    'bank_roles': ['active_read_selector', 'other_selector'],
    'capture': 'main_pre_FrameTick_after_join_before_permit',
    'transform_domain': 'native_local_transform_position_then_three_basis_vectors',
    'step': [1, 60],
    'coverage': COVERAGE, 'unsupported': UNSUPPORTED,
}


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')


def sha(data):
    return hashlib.sha256(data).hexdigest()


SCHEMA_SHA = sha(canonical_json(SCHEMA_DESCRIPTION))


def exact_keys(value, keys, label):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ValueError(label + ': missing or unknown fields')


def uint(value, bits, label):
    if type(value) is not int or not 0 <= value < 1 << bits:
        raise ValueError(label + ': invalid unsigned integer')
    return value


def hexword(value, width, label):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{' + str(width) + '}', value):
        raise ValueError(label + ': noncanonical hexadecimal')
    return int(value, 16)


def floatword(value, width, label, nonnegative=False):
    number = hexword(value, width, label)
    decoded = struct.unpack('>f' if width == 8 else '>d', bytes.fromhex(value))[0]
    if not math.isfinite(decoded) or (nonnegative and decoded < 0):
        raise ValueError(label + ': invalid floating point state')
    return number


def identifier(value, label):
    if not isinstance(value, str) or not re.fullmatch('[a-z][a-z0-9_]{0,62}', value):
        raise ValueError(label + ': invalid semantic identifier')
    data = value.encode('ascii')
    return bytes([len(data)]) + data


def validate_contract(value):
    fields = ('schema', 'schema_sha256', 'snapshot_sha256', 'stock_sha256',
              'fixture_files', 'actors', 'coverage', 'unsupported')
    has_import = isinstance(value, dict) and 'import_math' in value
    exact_keys(value, fields + (('import_math',) if has_import else ()), 'contract')
    if value['schema'] != SCHEMA or value['schema_sha256'] != SCHEMA_SHA:
        raise ValueError('contract: unknown schema')
    for key in ('snapshot_sha256', 'stock_sha256'):
        hexword(value[key], 64, key)
    if has_import:
        from portable_import_capability import validate
        validate(value['import_math'], value['snapshot_sha256'])
    if value['coverage'] != list(COVERAGE) or value['unsupported'] != list(UNSUPPORTED):
        raise ValueError('contract: coverage differs')
    files = value['fixture_files']
    if not isinstance(files, dict) or not files or len(files) > 32:
        raise ValueError('contract: missing or oversized fixture manifest')
    for path, digest in files.items():
        if not re.fullmatch('[a-z0-9_/.-]{1,160}', path) or path.startswith('/') or '..' in path.split('/'):
            raise ValueError('contract: invalid fixture path')
        hexword(digest, 64, 'fixture digest')
    actors = value['actors']
    if not isinstance(actors, list) or len(actors) != 12:
        raise ValueError('contract: requires twelve actors')
    saved, slots = [], []
    for actor in actors:
        exact_keys(actor, ('saved', 'fixture_slot', 'macro', 'owner'), 'contract actor')
        identity = hexword(actor['saved'], 16, 'saved')
        if not identity:
            raise ValueError('contract: null saved identity')
        slot = uint(actor['fixture_slot'], 16, 'fixture slot')
        identifier(actor['macro'], 'macro'); identifier(actor['owner'], 'owner')
        saved.append(identity); slots.append(slot)
    if saved != sorted(set(saved)) or sorted(slots) != list(range(1, 13)):
        raise ValueError('contract: duplicate or unordered identity/slot')
    return sha(canonical_json(value))


def encode_bank(value):
    exact_keys(value, ('transform', 'references'), 'bank')
    vectors, references = value['transform'], value['references']
    if not isinstance(vectors, list) or len(vectors) != 12 or not isinstance(references, list) or len(references) != 2:
        raise ValueError('bank: incomplete transform or references')
    return struct.pack('<12I2Q', *(floatword(v, 8, 'transform') for v in vectors),
                       *(hexword(v, 16, 'reference') for v in references))


def encode_actor(value, expected):
    exact_keys(value, ('saved', 'fixture_slot', 'present', 'class', 'controller', 'coverage',
                       'macro', 'owner', 'parent_saved', 'hull_bits', 'active', 'other'), 'actor')
    if any(value[key] != expected[key] for key in ('saved', 'fixture_slot', 'macro', 'owner')):
        raise ValueError('actor: fixture identity changed')
    if (type(value['present']) is not bool or not value['present'] or value['class'] != 'ship_small'
            or value['controller'] != 'navigation' or value['coverage'] != list(COVERAGE)):
        raise ValueError('actor: absent, unsupported class/controller or incomplete coverage')
    data = bytearray(b'X4P1ROW\0')
    data.extend(struct.pack('<QHBBBI', hexword(value['saved'], 16, 'saved'),
                            uint(value['fixture_slot'], 16, 'fixture slot'), 1, 1, 1, 31))
    data.extend(identifier(value['macro'], 'macro')); data.extend(identifier(value['owner'], 'owner'))
    data.extend(struct.pack('<QQ', hexword(value['parent_saved'], 16, 'parent'),
                            floatword(value['hull_bits'], 16, 'hull', True)))
    data.extend(encode_bank(value['active'])); data.extend(encode_bank(value['other']))
    return bytes(data)


def encode_frame(value, contract):
    contract_sha = validate_contract(contract)
    exact_keys(value, ('contract_sha256', 'tick', 'game_bits', 'calls', 'returns',
                       'receipt_tick', 'commands', 'actors'), 'frame')
    if value['contract_sha256'] != contract_sha:
        raise ValueError('frame: contract mismatch')
    tick = uint(value['tick'], 64, 'tick')
    if tick > 120 or any(uint(value[key], 64, key) != tick for key in ('calls', 'returns', 'receipt_tick')):
        raise ValueError('frame: command receipt or original return count differs')
    if value['commands'] != ['00000000', '00000000']:
        raise ValueError('frame: zero-command baseline only')
    if not isinstance(value['actors'], list) or len(value['actors']) != 12:
        raise ValueError('frame: incomplete actor coverage')
    data = bytearray(b'X4P1FRM\0' + bytes.fromhex(contract_sha))
    data.extend(struct.pack('<5Q2IH', tick, floatword(value['game_bits'], 16, 'game clock', True),
                            value['calls'], value['returns'], value['receipt_tick'], 0, 0, 12))
    for actor, expected in zip(value['actors'], contract['actors']):
        encoded = encode_actor(actor, expected)
        data.extend(struct.pack('<H', len(encoded))); data.extend(encoded)
    return bytes(data)


def differences(left, right, prefix=''):
    """Field paths preserve independent divergence causes instead of one root."""
    if type(left) is not type(right):
        return [prefix or '$']
    if isinstance(left, dict):
        if set(left) != set(right):
            return [prefix or '$']
        return [path for key in sorted(left) for path in differences(left[key], right[key], prefix + '.' + key)]
    if isinstance(left, list):
        if len(left) != len(right):
            return [prefix]
        return [path for i, (a, b) in enumerate(zip(left, right)) for path in differences(a, b, f'{prefix}[{i}]')]
    return [] if left == right else [prefix]


def compare(left, right, contract):
    a, b = encode_frame(left, contract), encode_frame(right, contract)
    return {'schema': SCHEMA, 'scope': list(COVERAGE), 'unsupported': list(UNSUPPORTED),
            'left_sha256': sha(a), 'right_sha256': sha(b), 'equal': a == b,
            'differences': differences(left, right)}


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def take(self, length):
        if self.offset + length > len(self.data):
            raise ValueError('truncated portable record')
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def unpack(self, format):
        return struct.unpack(format, self.take(struct.calcsize(format)))

    def text(self):
        length, = self.unpack('<B')
        if not 1 <= length <= 63:
            raise ValueError('invalid portable identifier length')
        return self.take(length).decode('ascii')


def decode_actor(data):
    reader = Reader(data)
    if reader.take(8) != b'X4P1ROW\0':
        raise ValueError('unknown actor schema')
    saved, slot, present, kind, controller, coverage = reader.unpack('<QHBBBI')
    if (present, kind, controller, coverage) != (1, 1, 1, 31):
        raise ValueError('unsupported actor coverage')
    macro, owner = reader.text(), reader.text()
    parent, hull = reader.unpack('<QQ')
    result = dict(saved=f'{saved:016x}', fixture_slot=slot, present=True, **{'class': 'ship_small'},
                  controller='navigation', coverage=list(COVERAGE), macro=macro, owner=owner,
                  parent_saved=f'{parent:016x}', hull_bits=f'{hull:016x}')
    for key in ('active', 'other'):
        words = reader.unpack('<12I2Q')
        result[key] = {'transform': [f'{v:08x}' for v in words[:12]],
                       'references': [f'{v:016x}' for v in words[12:]]}
    if reader.offset != len(data):
        raise ValueError('trailing actor bytes')
    return result


def decode_frame(data, contract):
    if not isinstance(data, bytes) or len(data) > 4096:
        raise ValueError('oversized or nonbinary portable frame')
    reader = Reader(data)
    if reader.take(8) != b'X4P1FRM\0':
        raise ValueError('unknown frame schema')
    digest = reader.take(32).hex()
    tick, game, calls, returns, receipt, first, second, count = reader.unpack('<5Q2IH')
    if count != 12:
        raise ValueError('incomplete portable frame')
    actors = []
    for _ in range(count):
        size, = reader.unpack('<H')
        if not 173 <= size <= 320:
            raise ValueError('invalid bounded actor size')
        actors.append(decode_actor(reader.take(size)))
    if reader.offset != len(data):
        raise ValueError('trailing frame bytes')
    result = dict(contract_sha256=digest, tick=tick, game_bits=f'{game:016x}', calls=calls,
                  returns=returns, receipt_tick=receipt,
                  commands=[f'{first:08x}', f'{second:08x}'], actors=actors)
    if encode_frame(result, contract) != data:
        raise ValueError('noncanonical portable frame')
    return result
