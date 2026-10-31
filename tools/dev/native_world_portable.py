"""Offline imported-world diagnostic stream; never a permit or session verdict.

No process access, native offsets or engine callbacks. Producers must separately
prove capture ownership. Unknown domains remain explicit even when hashes agree.
"""
from collections import Counter
import hashlib
import json
import struct

from native_portable import exact_keys, floatword, hexword, identifier, uint, encode_bank

SCHEMA = 'x4-imported-world-diag-v1'
DOMAINS = ('entity', 'topology', 'motion', 'health', 'timing')
MAX_IDS = 524288
MAX_LINE = 4096
MAX_DIFFERENCES = 128
IDENTITIES = ('live', 'unresolved_import', 'unresolved_runtime', 'missing_import',
              'ambiguous_identity', 'read_failed', 'changed_during_capture', 'not_captured')
STATUSES = ('supported', 'unsupported_class', 'unsupported_layout', 'unresolved_reference') + IDENTITIES[1:]


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True,
                      allow_nan=False).encode('ascii')


class Cohort:
    """Explicit fixed inventory; do not construct by filtering supported classes."""
    def __init__(self, saved_ids):
        values = []
        previous = 0
        digest = hashlib.sha256(b'X4WDIAG1-COHORT\0')
        for identity in saved_ids:
            if len(values) == MAX_IDS:
                raise ValueError('cohort count outside bound')
            value = hexword(identity, 16, 'cohort saved ID')
            if value <= previous:
                raise ValueError('cohort IDs zero, duplicate or unordered')
            previous = value
            digest.update(struct.pack('<Q', value))
            values.append(identity)
        self.ids = tuple(values)
        if not self.ids:
            raise ValueError('cohort count outside bound')
        self.members = frozenset(self.ids)
        self.sha256 = digest.hexdigest()


def validate_header(header, cohort):
    exact_keys(header, ('schema', 'source', 'snapshot_sha256', 'content_sha256',
                       'cohort_sha256', 'cohort_count', 'stage', 'tick', 'game_bits'), 'header')
    if header['schema'] != SCHEMA or header['source'] not in ('windows', 'linux', 'offline'):
        raise ValueError('unsupported diagnostic schema/source')
    if header['stage'] != 'joined_main_pre_tick':
        raise ValueError('state requires independently proved joined main pre-tick boundary')
    for key in ('snapshot_sha256', 'content_sha256', 'cohort_sha256'):
        hexword(header[key], 64, key)
    if (type(header['cohort_count']) is not int or header['cohort_count'] != len(cohort.ids)
            or header['cohort_sha256'] != cohort.sha256):
        raise ValueError('header does not bind complete fixed cohort')
    uint(header['tick'], 64, 'tick')
    floatword(header['game_bits'], 16, 'game time', True)
    return {key: value for key, value in header.items() if key != 'source'}


def reference(value, cohort):
    number = hexword(value, 16, 'saved reference')
    if number and value not in cohort.members:
        raise ValueError('nonzero reference outside cohort; use unresolved_reference')


def validate_value(domain, value, cohort):
    if domain == 'entity':
        exact_keys(value, ('class', 'macro', 'owner'), domain)
        if value['class'] != 'ship_small':
            raise ValueError('unproved semantic class')
        identifier(value['macro'], 'macro'); identifier(value['owner'], 'owner')
    elif domain == 'topology':
        exact_keys(value, ('parent_saved',), domain)
        reference(value['parent_saved'], cohort)
    elif domain == 'motion':
        exact_keys(value, ('controller', 'active', 'other'), domain)
        if value['controller'] != 'navigation':
            raise ValueError('unproved motion controller')
        for key in ('active', 'other'):
            encode_bank(value[key])
            for identity in value[key]['references']:
                reference(identity, cohort)
    elif domain == 'health':
        exact_keys(value, ('stored_hull_bits',), domain)
        floatword(value['stored_hull_bits'], 16, 'stored hull', True)
    else:
        raise ValueError('per-object timing has no proved portable layout')


def validate_row(row, cohort, expected):
    exact_keys(row, ('saved', 'identity', 'domains'), 'row')
    if row['saved'] != expected:
        raise ValueError(f'missing, duplicate or unordered row: expected {expected}, got {row["saved"]!r}')
    if row['identity'] not in IDENTITIES:
        raise ValueError('unknown identity result')
    exact_keys(row['domains'], DOMAINS, 'domains')
    for domain in DOMAINS:
        result = row['domains'][domain]
        exact_keys(result, ('status', 'value'), domain)
        status = result['status']
        if status not in STATUSES:
            raise ValueError('unknown domain status')
        if row['identity'] != 'live' and status != row['identity']:
            raise ValueError('unverified identity cannot publish semantic fields')
        if row['identity'] == 'live' and status in ('unresolved_import', 'unresolved_runtime', 'missing_import', 'ambiguous_identity'):
            raise ValueError('domain identity contradicts verified live identity')
        if status == 'supported':
            if row['domains']['entity']['status'] != 'supported':
                raise ValueError('semantic fields require proved supported class')
            validate_value(domain, result['value'], cohort)
        elif result['value'] is not None:
            raise ValueError('unsupported state must not invent a value or opaque bytes')


class Totals:
    def __init__(self, header, cohort):
        context = canonical(validate_header(header, cohort))
        self.hashes = {d: hashlib.sha256(b'X4WDIAG1-ROOT\0' + d.encode() + b'\0' + context) for d in DOMAINS}
        self.identities = Counter({key: 0 for key in IDENTITIES})
        self.coverage = {d: Counter({key: 0 for key in STATUSES}) for d in DOMAINS}
        self.count = 0

    def add(self, row):
        self.count += 1
        self.identities[row['identity']] += 1
        saved = struct.pack('<Q', int(row['saved'], 16))
        for domain in DOMAINS:
            result = row['domains'][domain]
            self.coverage[domain][result['status']] += 1
            payload = canonical(dict(identity=row['identity'], result=result))
            digest = hashlib.sha256(b'X4WDIAG1-ROW\0' + domain.encode() + b'\0' + saved + payload).digest()
            self.hashes[domain].update(saved + digest)

    def footer(self):
        return dict(rows=self.count, roots={d: self.hashes[d].hexdigest() for d in DOMAINS},
                    identity_census=dict(self.identities),
                    domain_census={d: dict(self.coverage[d]) for d in DOMAINS})


def line(kind, value):
    data = canonical({kind: value}) + b'\n'
    if len(data) > MAX_LINE:
        raise ValueError('diagnostic line exceeds bound')
    return data


def encode(stream, header, rows, cohort):
    """Write to a fresh private file; a failed/incomplete stream has no footer."""
    totals = Totals(header, cohort)
    stream.write(line('header', header))
    for index, row in enumerate(rows):
        if index >= len(cohort.ids):
            raise ValueError('extra row outside fixed cohort')
        validate_row(row, cohort, cohort.ids[index])
        stream.write(line('row', row)); totals.add(row)
    if totals.count != len(cohort.ids):
        raise ValueError(f'missing row: {cohort.ids[totals.count]}')
    footer = totals.footer(); stream.write(line('footer', footer))
    return footer


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def read_line(stream, kind):
    raw = stream.readline(MAX_LINE + 1)
    if not raw or len(raw) > MAX_LINE or not raw.endswith(b'\n'):
        raise ValueError(f'missing, truncated or oversized {kind}')
    try:
        item = json.loads(raw.decode('ascii'), object_pairs_hook=unique_pairs)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError('invalid diagnostic JSON') from error
    exact_keys(item, (kind,), 'record kind')
    return item[kind]


class Reader:
    def __init__(self, stream, cohort):
        self.stream, self.cohort = stream, cohort
        self.header = read_line(stream, 'header')
        self.context = validate_header(self.header, cohort)
        self.totals = Totals(self.header, cohort)
        self.complete = False

    def row(self):
        index = self.totals.count
        if self.complete or index >= len(self.cohort.ids):
            raise ValueError('row after cohort end')
        row = read_line(self.stream, 'row')
        validate_row(row, self.cohort, self.cohort.ids[index])
        self.totals.add(row)
        return row

    def finish(self):
        if self.complete or self.totals.count != len(self.cohort.ids):
            raise ValueError('incomplete or repeated finish')
        footer = read_line(self.stream, 'footer')
        # Exact canonical comparison also rejects bool-as-int and extra keys.
        if canonical(footer) != canonical(self.totals.footer()):
            raise ValueError('footer roots/census disagree with records')
        if self.stream.read(1):
            raise ValueError('trailing diagnostic data')
        self.complete = True
        return footer


def first_difference(left, right, path='value'):
    if type(left) is not type(right):
        return dict(field=path, left=left, right=right)
    if isinstance(left, dict):
        for key in sorted(left.keys() | right.keys()):
            if key not in left or key not in right:
                return dict(field=path+'.'+key, left=left.get(key), right=right.get(key))
            found = first_difference(left[key], right[key], path+'.'+key)
            if found: return found
    elif isinstance(left, list):
        for index, (a, b) in enumerate(zip(left, right)):
            found = first_difference(a, b, f'{path}[{index}]')
            if found: return found
        if len(left) != len(right): return dict(field=path+'.length', left=len(left), right=len(right))
    elif left != right:
        return dict(field=path, left=left, right=right)
    return None


def compare(left_stream, right_stream, cohort, difference_limit=MAX_DIFFERENCES):
    if type(difference_limit) is not int or not 1 <= difference_limit <= MAX_DIFFERENCES:
        raise ValueError('difference output bound')
    left, right = Reader(left_stream, cohort), Reader(right_stream, cohort)
    if left.context != right.context:
        raise ValueError('snapshot/content/cohort/stage/tick/game boundary mismatch')
    counts = {d: dict(comparable=0, value_differences=0, coverage_differences=0) for d in DOMAINS}
    differences = []; total = identity_differences = 0

    def retain(value):
        nonlocal total
        total += 1
        if len(differences) < difference_limit: differences.append(value)

    for _ in cohort.ids:
        a, b = left.row(), right.row()
        if a['identity'] != b['identity']:
            identity_differences += 1
            retain(dict(saved=a['saved'], domain='identity', field='identity', left=a['identity'], right=b['identity']))
        for domain in DOMAINS:
            x, y = a['domains'][domain], b['domains'][domain]
            if x['status'] != y['status']:
                counts[domain]['coverage_differences'] += 1
                retain(dict(saved=a['saved'], domain=domain, field='status', left=x['status'], right=y['status']))
            elif x['status'] == 'supported':
                counts[domain]['comparable'] += 1
                found = first_difference(x['value'], y['value'])
                if found:
                    counts[domain]['value_differences'] += 1
                    retain(dict(saved=a['saved'], domain=domain, status='supported', **found))
    lfooter, rfooter = left.finish(), right.finish()
    result = {}
    for domain, values in counts.items():
        complete = values['comparable'] == len(cohort.ids)
        result[domain] = dict(**values, complete=complete,
            semantic_equal=False if values['value_differences'] else True if complete else None,
            supported_subset_equal=not bool(values['value_differences']) if values['comparable'] else None,
            coverage_equal=not bool(values['coverage_differences']),
            root_equal=lfooter['roots'][domain] == rfooter['roots'][domain],
            left_census=lfooter['domain_census'][domain], right_census=rfooter['domain_census'][domain])
    return dict(schema=SCHEMA, diagnostic_only=True, stream_valid=True, inventory_count=len(cohort.ids),
                identity_differences=identity_differences, domains=result,
                difference_count=total, differences_truncated=total>len(differences), first_differences=differences,
                left=lfooter, right=rfooter)
