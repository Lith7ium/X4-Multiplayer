"""Decode passive bank-local construction evidence; sector poses are unsupported."""
import re

FIELDS = 'handle slot tick sector_pose_supported identity_ok stable read_before write_before read_after write_after unknown_references reference_flags parent raw_pose motion timing'.split()
HEX_WIDTHS = {'handle': 16, 'reference_flags': 8, 'parent': 16, 'raw_pose': 272, 'motion': 380, 'timing': 90}


def reassemble(log, kind):
    """Reject truncated, duplicated or reordered chunks before interpreting state."""
    if kind not in ('RAW', 'STATE'):
        raise ValueError('unknown construction chunk family')
    pending, complete = {}, {}
    prefix = 'NATIVE_GATE CONSTRUCTION_' + kind + '_'
    for line in log.splitlines():
        if prefix not in line:
            continue
        tail = line.split(prefix, 1)[1]
        if kind == 'RAW' and tail.startswith('LOCAL '):
            continue  # A separate exact-identity receipt.
        if not tail.startswith(('PART ', 'END ')):
            raise ValueError('unknown construction chunk record')
        part = re.fullmatch(r'PART tick=([1-9][0-9]*) phase=(before|after|sample) part=([1-9][0-9]*) count=([1-9][0-9]*) data=([^\r\n]+)', tail)
        end = re.fullmatch(r'END tick=([1-9][0-9]*) phase=(before|after|sample) count=([1-9][0-9]*) bytes=([1-9][0-9]*)', tail)
        match = part or end
        if not match:
            raise ValueError('malformed construction chunk')
        tick, phase = int(match[1]), match[2]
        key = tick, phase
        if tick > 0xffffffff or phase not in (('sample',) if kind == 'STATE' else ('before', 'after')) or key in complete:
            raise ValueError('duplicate or invalid construction chunk identity')
        if part:
            index, count, data = int(part[3]), int(part[4]), part[5]
            if not 1 <= index <= count <= 12 or not 1 <= len(data) <= 700 or (index < count and len(data) != 700) or not data.isascii():
                raise ValueError('construction chunk bounds')
            expected_count, chunks = pending.setdefault(key, (count, []))
            if count != expected_count or index != len(chunks)+1:
                raise ValueError('construction chunk gap or reorder')
            chunks.append(data)
        else:
            count, size = int(end[3]), int(end[4])
            expected_count, chunks = pending.pop(key, (0, []))
            data = ''.join(chunks)
            if count != expected_count or len(chunks) != count or len(data) != size or not 1 <= size <= 8192:
                raise ValueError('construction chunk terminal coverage')
            complete[key] = data
    if pending:
        raise ValueError('unterminated construction chunk sequence')
    return complete


def sample(raw, *, handle, tick, slot=None):
    parts = raw.split(';')
    pairs = [value.split('=', 1) for value in parts[1:]]
    if parts[0] != 'SHIP_RAW_MOTION_1' or any(len(p) != 2 for p in pairs) or [p[0] for p in pairs] != FIELDS:
        raise ValueError('passive motion schema')
    fields = dict(pairs)
    for name, value in fields.items():
        if name in HEX_WIDTHS:
            if not re.fullmatch('[0-9a-f]{' + str(HEX_WIDTHS[name]) + '}', value):
                raise ValueError('passive motion hex width or syntax')
        elif not re.fullmatch('0|[1-9][0-9]*', value):
            raise ValueError('passive motion number syntax')
    values = {k: int(v) for k, v in fields.items() if k not in HEX_WIDTHS}
    if fields['handle'] != handle or values['tick'] != tick or not 1 <= tick <= 0xffffffff or not 15 <= values['slot'] <= 30 or (slot is not None and slot != values['slot']):
        raise ValueError('passive motion identity or tick')
    if values['sector_pose_supported'] != 0 or values['identity_ok'] != 1 or values['stable'] != 1:
        raise ValueError('passive motion coverage or stability')
    rb, wb, ra, wa = (values[name] for name in ('read_before', 'write_before', 'read_after', 'write_after'))
    if rb not in (0, 1) or wb != 1-rb or (rb, wb) != (ra, wa):
        raise ValueError('passive motion selector instability')
    mask = int(fields['reference_flags'], 16)
    if mask > 31 or values['unknown_references'] != mask.bit_count():
        raise ValueError('passive motion reference coverage')
    motion = bytes.fromhex(fields['motion'])
    if int.from_bytes(motion[:4], 'little') != 0x2c51158:
        raise ValueError('unsupported passive motion controller')
    parent = int(fields['parent'], 16)
    pose = parent.to_bytes(8, 'little')
    references = [parent]
    for offset in (4, 97):
        pose += motion[offset:offset+48] + motion[offset+72:offset+88]
        references.extend(int.from_bytes(motion[index:index+8], 'little') for index in (offset+72, offset+80))
    if pose.hex() != fields['raw_pose']:
        raise ValueError('passive position/basis/reference subset differs from motion')
    if any(mask & (1 << index) and value for index, value in enumerate(references)):
        raise ValueError('unresolved passive reference has an invented normalized ID')
    return {**fields, **values, 'references': references, 'sector_pose_supported': False}


def local_detail(raw, *, native_id, canonical):
    match = re.fullmatch(r'SHIP_RAW_MOTION_LOCAL_1;runtime=([0-9a-f]{16});refs=([0-9a-f]{16}(?:,[0-9a-f]{16}){4})', raw)
    if not match or int(match[1], 16) != int(native_id) or not 0 < int(native_id) < 2**64:
        raise ValueError('passive private lifetime mismatch')
    references = [int(value, 16) for value in match[2].split(',')]
    mask = int(canonical['reference_flags'], 16)
    for index, (runtime, normalized) in enumerate(zip(references, canonical['references'])):
        unknown = bool(mask & (1 << index))
        if (not runtime and (normalized or unknown)) or (runtime and not unknown and not normalized):
            raise ValueError('passive private reference coverage mismatch')
    return references


def same_frame(before, after, private_before, private_after, *, native_id):
    refs_before = local_detail(private_before, native_id=native_id, canonical=before)
    refs_after = local_detail(private_after, native_id=native_id, canonical=after)
    keys = ('handle', 'slot', 'tick', 'read_before', 'write_before', 'raw_pose', 'reference_flags', 'parent')
    if any(before[key] != after[key] for key in keys) or refs_before != refs_after:
        raise ValueError('enrollment changed passive position/basis/reference fields')
