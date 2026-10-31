"""Inactive logical unit census verifier; never establishes physical drone coverage."""
import re

BUILD_MACRO = 'ship_gen_xs_buildingdrone_01_a_macro'


def sample(raw, *, expected_handle, expected_stock=30):
    fields = raw.split(':')
    if (len(fields) != 17 or fields[0] != 'CONSTRUCTION_UNITS_1'
            or not re.fullmatch('[0-9a-f]{16}', expected_handle)
            or fields[2] != expected_handle
            or fields[3:6] != ['logical_only=1', 'physical_ids=0', 'only_drones=1']):
        raise ValueError('invalid logical unit schema or identity')
    numeric = fields[1:2] + fields[6:16]
    if any(not re.fullmatch('0|[1-9][0-9]*', value) for value in numeric):
        raise ValueError('noncanonical logical unit count')
    tick, stock, total, build, stored, unavailable, stock_match, stored_match, bounded, unknown, uncertain = map(int, numeric)
    if not 1 <= tick <= 0xffffffff or not 0 <= expected_stock <= 100 or stock != expected_stock:
        raise ValueError('invalid logical unit tick or expected stock')
    if any(value > 100 for value in (total, build, stored, unavailable)):
        raise ValueError('logical unit count exceeds fixture capacity')
    rows = []
    for row in fields[16].split(';') if fields[16] else []:
        match = re.fullmatch(r'([a-z0-9_]{1,96}),([a-z0-9_]{1,24}),(0|[1-9][0-9]*)', row)
        if not match or int(match[3]) > 100:
            raise ValueError('malformed logical unit row')
        rows.append((match[1], match[2], int(match[3])))
    if (len(rows) > 16 or len({row[:2] for row in rows}) != len(rows)
            or fields[16] != ';'.join(sorted(fields[16].split(';')))):
        raise ValueError('duplicate, unsorted or excessive logical unit rows')
    calculated_build = sum(amount for _, category, amount in rows if category == 'build')
    calculated_unknown = sum(macro != BUILD_MACRO or category != 'build' for macro, category, _ in rows)
    if total != sum(row[2] for row in rows) or build != calculated_build or unknown != calculated_unknown:
        raise ValueError('logical unit row accounting mismatch')
    expected_flags = (int(build == stock), int(stored == build), int(unavailable <= stored))
    if (stock_match, stored_match, bounded) != expected_flags:
        raise ValueError('incorrect logical unit relationship flags')
    if uncertain != int(not all(expected_flags) or unknown > 0):
        raise ValueError('incorrect logical unit accounting uncertainty')
    return {'tick': tick, 'handle': expected_handle, 'stock': stock, 'total': total,
            'build': build, 'stored': stored, 'unavailable': unavailable, 'unknown_rows': unknown,
            'accounting_unknown': bool(uncertain), 'stock_delta': build - stock,
            'rows': rows, 'physical_identity_coverage': False}


def sequence(raw_samples, *, expected_handle, required_ticks, expected_stock=30):
    """Require caller-supplied coverage; report logical use separately from completion."""
    decoded = [sample(raw, expected_handle=expected_handle, expected_stock=expected_stock) for raw in raw_samples]
    ticks = [row['tick'] for row in decoded]
    required = list(required_ticks)
    if (not required or ticks != required or ticks != sorted(set(ticks))):
        raise ValueError('logical unit sample coverage, order or uniqueness failed')
    return {'samples': decoded, 'logical_stock_constant': all(row['stock_delta'] == 0 for row in decoded),
            'logical_unavailable_observed': any(row['unavailable'] > 0 for row in decoded),
            'accounting_unknown': any(row['accounting_unknown'] for row in decoded),
            'physical_identity_coverage': False, 'construction_completion_proven': False}
