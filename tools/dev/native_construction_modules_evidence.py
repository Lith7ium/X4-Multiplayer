"""Inactive single-module observations; no native plan linkage or completion proof."""
import re

MODULE_MACRO = 'struct_arg_base_02_macro'
SCOPE = ['native_plan_link_supported=0', 'sector_pose_supported=0',
         'atomic_snapshot=0', 'generation_lease=0']


def _uint(text, maximum, minimum=0):
    if not re.fullmatch('0|[1-9][0-9]*', text):
        raise ValueError('noncanonical module census integer')
    value = int(text)
    if not minimum <= value <= maximum:
        raise ValueError('module census integer outside bound')
    return value


def sample(raw, private, *, expected_handle):
    f, local = raw.split(':'), private.split(':')
    if (len(f) != 15 or f[0] != 'CONSTRUCTION_MODULES_1'
            or not re.fullmatch('[0-9a-f]{16}', expected_handle)
            or f[2] != expected_handle or f[3:7] != SCOPE):
        raise ValueError('invalid module census schema, scope or handle')
    tick = _uint(f[1], 0xffffffff, 1)
    sop, scon, present = (_uint(v, 1) for v in f[7:10])
    first_seen = _uint(f[10], tick)
    mop, mcon, unknown = (_uint(v, 1) for v in f[12:15])
    if sop + scon != 1 or mop + mcon > 1:
        raise ValueError('inconsistent module census lifecycle flags')
    if present:
        if not first_seen or f[11] != MODULE_MACRO or unknown != int(mop + mcon == 0):
            raise ValueError('invalid observed module identity or state')
    elif first_seen or f[11] != '-' or mop or mcon or unknown:
        raise ValueError('empty module census carries invented module state')
    if (len(local) != 5 or local[0] != 'CONSTRUCTION_MODULES_LOCAL_1'
            or local[1:3] != f[1:3]):
        raise ValueError('missing or mismatched private module identity evidence')
    station_id = _uint(local[3], 0xffffffffffffffff, 1)
    module_id = _uint(local[4], 0xffffffffffffffff, 1) if present else None
    if not present and local[4] != '-':
        raise ValueError('empty module census carries private module identity')
    return {'tick': tick, 'handle': expected_handle, 'station_operational': bool(sop),
            'station_construction': bool(scon), 'present': bool(present),
            'first_seen': first_seen, 'macro': f[11], 'operational': bool(mop),
            'construction': bool(mcon), 'unknown_state': bool(unknown),
            'station_id': station_id, 'module_id': module_id,
            'native_plan_link_supported': False, 'sector_pose_supported': False,
            'atomic_snapshot': False, 'generation_lease': False}


def sequence(records, *, expected_handle, required_ticks):
    """Private IDs verify continuity locally; they must never enter peer hashes."""
    rows = [sample(raw, private, expected_handle=expected_handle) for raw, private in records]
    ticks, required = [r['tick'] for r in rows], list(required_ticks)
    if not required or ticks != required or ticks != sorted(set(ticks)):
        raise ValueError('module census coverage, order or uniqueness failed')
    if rows[0]['present']:
        raise ValueError('module census requires an observed empty baseline')
    retained, birth = None, 0
    for row in rows:
        if row['station_id'] != rows[0]['station_id']:
            raise ValueError('station native lifetime changed')
        if retained is not None and row['module_id'] != retained:
            raise ValueError('observed module disappeared, was replaced or reused')
        if row['present'] and retained is None:
            retained, birth = row['module_id'], row['tick']
        if row['first_seen'] != birth:
            raise ValueError('module first-seen evidence changed or was invented')
    return {'samples': rows, 'observed_birth': retained is not None,
            'construction_observed': any(r['construction'] for r in rows),
            'operational_observed': any(r['operational'] for r in rows),
            'unknown_state': any(r['unknown_state'] for r in rows),
            'native_plan_link_supported': False, 'sector_pose_supported': False,
            'atomic_snapshot': False, 'generation_lease': False,
            'construction_completion_proven': False}
