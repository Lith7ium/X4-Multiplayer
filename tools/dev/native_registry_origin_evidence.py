"""Bounded local provenance for the immutable first covered AI-family entry.

This does not classify an entry as harmless or prove complete native coverage.
"""
import re

FIELDS = ('schema runtime order thread recorded source caller verified caller_source context exec program '
          'begin end mode pc context_pc type entity depth ui view time rng_before rng_after result '
          'reads error complete script_hash script').split()
HEX = set(('runtime caller verified context exec program begin end pc context_pc type entity time '
           'rng_before rng_after result reads error script_hash').split())
IRQ_CALLERS = {0x10662ef, 0x1066d8d, 0x1066dc6, 0x106b082, 0x106d8db,
               0x106e2f4, 0x106e34f, 0x106fef1, 0x10a7996}


def evidence(log, updates, required=False):
    """Return absent for historical captures; reject incomplete new captures."""
    def bounded(pattern, limit):
        found = []
        for match in re.finditer(pattern, log):
            if len(found) == limit:
                raise ValueError('origin global capacity or footer count')
            found.append(match[1])
        return found
    lines = bounded(r'NATIVE_GATE REGISTRY_UPDATE_ORIGIN ([^\r\n]*)', 256)
    footers = bounded(r'NATIVE_GATE REGISTRY_ORIGIN_STOP ([^\r\n]*)', 1)
    result = {'supported': False, 'rows': [], 'scope': 'first_covered_ai_family_entry_only'}
    if not lines and not footers:
        if required:
            raise ValueError('required origin capture missing')
        return result
    if len(lines) > 256 or len(footers) != 1 or len(footers[0]) > 256:
        raise ValueError('origin global capacity or footer count')
    footer = re.fullmatch(r'schema=1 count=(\d+) enabled=1 closed=1 failures=0 open=0 restore_open=0 peak=(\d+)', footers[0])
    if not footer:
        raise ValueError('origin footer completeness')
    count, peak = map(int, footer.groups())
    if count != len(lines) or count != len(updates) or not 0 <= peak <= 256:
        raise ValueError('origin receipt coverage')
    for line, update in zip(lines, updates):
        if len(line) > 1535:
            raise ValueError('origin row length')
        pairs = [field.split('=', 1) for field in line.split(' ')]
        if any(len(p) != 2 for p in pairs) or [p[0] for p in pairs] != FIELDS:
            raise ValueError('origin row schema')
        row = {}
        for field, value in pairs:
            if field == 'script':
                if not re.fullmatch(r'[!-~]{1,95}', value) or '|' in value:
                    raise ValueError('origin script boundary')
                row[field] = value
            else:
                if not re.fullmatch(r'[0-9a-f]{1,16}' if field in HEX else r'[0-9]{1,20}', value):
                    raise ValueError('origin numeric encoding')
                row[field] = int(value, 16 if field in HEX else 10)
                if row[field] >= 2**64:
                    raise ValueError('origin numeric bounds')
        if row['schema'] != 1 or any(row[k] != update[k] for k in ('runtime', 'order', 'thread')):
            raise ValueError('origin immutable lifetime/order/thread join')
        if update['kind'] != 6:
            if row['script'] != 'none' or any(row[k] for k in FIELDS if k not in ('schema', 'runtime', 'order', 'thread', 'script')):
                raise ValueError('origin attached to a different first family')
        else:
            if row['recorded'] != 1 or row['complete'] != 1 or row['reads'] != 0x3ff or row['error'] or not 1 <= row['source'] <= 4 or \
               row['type'] not in (0x2b7feb8, 0x2b99a88) or not row['context'] or not row['exec'] or not row['entity'] or \
               not 0 <= row['depth'] < 16 or row['ui'] not in (0, 1) or row['view'] not in (0, 1) or row['mode'] > 2**32-1 or not peak:
                raise ValueError('origin first attempt incomplete or unsupported')
            if not row['script_hash'] or row['script'] in ('none', 'unsupported'):
                raise ValueError('origin script unsupported')
            hashed = 14695981039346656037
            for byte in row['script'].encode('ascii'):
                hashed = ((hashed ^ byte) * 1099511628211) & (2**64-1)
            if hashed != row['script_hash']:
                raise ValueError('origin script preimage')
            if row['source'] == 2:
                if row['caller_source'] != 1 or row['caller'] != row['verified'] or row['verified'] not in IRQ_CALLERS or row['mode'] > 255:
                    raise ValueError('origin range caller not verified')
            elif row['source'] == 1:
                if row['caller_source'] != 1 or row['caller'] != 0x106e3fb or row['verified'] != row['caller']:
                    raise ValueError('origin AI caller not verified')
            elif row['caller_source'] not in (2, 3) or row['verified'] or (row['caller_source'] == 2) != bool(row['caller']):
                raise ValueError('origin vtable caller classification')
        result['rows'].append(row)
    result['supported'] = True
    return result
