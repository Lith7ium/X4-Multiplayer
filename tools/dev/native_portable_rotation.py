"""Independent Windows Euler-import receipts; no native process identity proof."""
import re

from native_portable import validate_contract
import portable_import_capability as cap
import native_portable_rotation_v1 as v1

TEXT = {'schema': 'x4-rotation-status-v2', 'mode': cap.MODE,
        'descriptor': cap.DESCRIPTOR_SHA256, 'contract': cap.CONTRACT_SHA256,
        'snapshot': cap.SNAPSHOT_SHA256, 'inputs': cap.INPUTS_SHA256}
NUMBERS = ('configured', 'phase', 'stop', 'failure', 'load_calls', 'load_returns',
           'load_true', 'load_false', 'load_abnormal', 'load_open',
           'role0_calls', 'role0_returns', 'role0_euler',
           'role1_calls', 'role1_returns', 'role1_euler',
           'foreign', 'open', 'owned', 'restore_error', 'restored', 'complete')
NUMBERS += tuple(f'role{role}_{cls}_{field}' for role in (0, 1) for cls in ('nav', 'player')
                 for field in ('calls', 'returns', 'euler')) + ('lifetimes', 'paired')
MAX_INTEGER = (1 << 53) - 1


def parse_rotation(text):
    if isinstance(text, str) and text.startswith('schema=x4-rotation-status-v1 '):
        return v1.parse_rotation(text)
    if not isinstance(text, str) or not 0 < len(text) <= 2047:
        raise ValueError('rotation status bounds')
    fields = {}
    for part in text.split(' '):
        if not re.fullmatch('[a-z0-9_]+=[a-z0-9_-]+', part):
            raise ValueError('rotation status encoding')
        key, value = part.split('=')
        if key in fields or key not in (*TEXT, *NUMBERS):
            raise ValueError('rotation status unknown or repeated field')
        if key in NUMBERS:
            if not re.fullmatch('0|[1-9][0-9]{0,15}', value) or int(value) > MAX_INTEGER:
                raise ValueError('rotation status noncanonical count')
            value = int(value)
        fields[key] = value
    if set(fields) != set(TEXT) | set(NUMBERS) or any(fields[k] != v for k, v in TEXT.items()):
        raise ValueError('rotation status incomplete or wrong capability')
    if any(fields[k] > 1 for k in ('configured', 'stop', 'restored', 'complete')):
        raise ValueError('rotation status flag')
    if fields['phase'] > 5 or fields['owned'] > 3:
        raise ValueError('rotation status phase or patch mask')
    return fields


def terminal_complete(row):
    if isinstance(row, dict) and row.get('schema') == v1.TEXT['schema']:
        return v1.terminal_complete(row)
    one = ('configured', 'stop', 'load_calls', 'load_returns', 'load_true', 'restored', 'complete')
    zero = ('failure', 'load_false', 'load_abnormal', 'load_open', 'open', 'owned', 'restore_error')
    return (isinstance(row, dict) and set(row) == set(TEXT) | set(NUMBERS)
            and all(row.get(k) == v for k, v in TEXT.items())
            and all(type(row[k]) is int and 0 <= row[k] <= MAX_INTEGER for k in NUMBERS)
            and all(type(row.get(k)) is int and row[k] == 1 for k in one)
            and all(type(row.get(k)) is int and row[k] == 0 for k in zero)
            and row.get('phase') == 4
            and all(type(row.get(f'role{r}_{name}')) is int and
                    row[f'role{r}_{name}'] == cap.EULER_PER_BANK
                    for r in (0, 1) for name in ('calls', 'returns', 'euler'))
            and row['lifetimes'] == row['paired'] == cap.EULER_PER_BANK
            and all(row[f'role{r}_{cls}_{name}'] == expected
                    for r in (0, 1) for cls, expected in (('nav', cap.NAVIGATION_EULER), ('player', cap.PLAYER_EULER))
                    for name in ('calls', 'returns', 'euler')))


def rotation_evidence(lines, contract, run, agent_sha256):
    result = {'valid': False, 'errors': [], 'native_processes_verified': False,
              'scope': 'Windows navigation/player saved-bank Euler import and owned restoration only'}
    try:
        imported = contract.get('import_math') if isinstance(contract, dict) else None
        if isinstance(imported, dict) and imported.get('mode') == cap.v1.MODE:
            return v1.rotation_evidence(lines, contract, run, agent_sha256)
        if validate_contract(contract) != cap.CONTRACT_SHA256 or 'import_math' not in contract:
            raise ValueError('unreviewed rotation evidence contract')
        if not re.fullmatch('[a-z0-9_-]{1,80}', run) or not re.fullmatch('[0-9a-f]{64}', agent_sha256):
            raise ValueError('invalid expected run or agent digest')
        marker = '[X4LAN] PORTABLE_ROTATION TERMINAL '
        records = []
        for line in lines:
            if 'PORTABLE_ROTATION TERMINAL' not in line:
                continue
            at = line.find(marker)
            if at < 0:
                raise ValueError('malformed rotation terminal marker')
            if records:
                raise ValueError('repeated rotation terminal receipt')
            value = line[at + len(marker):].rstrip('\r\n')
            if len(value) > 2048:
                raise ValueError('rotation terminal receipt bounds')
            match = re.fullmatch(r'result=(complete|failure) slot=1 run=([a-z0-9_-]+) agent=([0-9a-f]{64}) (.+)', value)
            if not match or match[2] != run or match[3] != agent_sha256:
                raise ValueError('rotation receipt run or artifact differs')
            row = parse_rotation(match[4])
            if row['schema'] != TEXT['schema']:
                raise ValueError('rotation receipt version differs from contract')
            if (match[1] == 'complete') != terminal_complete(row):
                raise ValueError('rotation receipt contradicts counters')
            records.append(row)
        if len(records) != 1:
            raise ValueError('missing or repeated rotation terminal receipt')
        result['status'] = records[0]
        if not terminal_complete(records[0]):
            raise ValueError('rotation import or restoration incomplete')
        result['valid'] = True
    except (ValueError, TypeError, KeyError) as error:
        result['errors'].append(str(error))
    return result
