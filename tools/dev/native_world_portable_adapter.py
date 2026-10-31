"""Offline adapters for retained imports and already validated portable frames.

X4IMP1 proves import-map contents only. Unselected objects are not_captured,
never presumed live. This cannot establish native lifecycle or authorize GO.
"""
import argparse
import json
from pathlib import Path
import struct

import native_portable
from native_world_portable import Cohort, DOMAINS, MAX_IDS, SCHEMA, encode


def read_imports(stream):
    header = stream.read(16)
    if len(header) != 16 or header[:8] != b'X4IMP1\0\0':
        raise ValueError('invalid import-map header')
    count, = struct.unpack('<Q', header[8:])
    if not 0 < count <= MAX_IDS:
        raise ValueError('import inventory exceeds common diagnostic bound')
    result = {}; runtimes = set(); previous = 0
    for _ in range(count):
        raw = stream.read(16)
        if len(raw) != 16:
            raise ValueError('truncated import-map record')
        saved, runtime = struct.unpack('<QQ', raw)
        if not saved or saved <= previous:
            raise ValueError('zero, duplicate or unordered imported saved ID')
        if runtime and runtime in runtimes:
            raise ValueError('ambiguous runtime alias in imported inventory')
        previous = saved
        if runtime: runtimes.add(runtime)
        result[f'{saved:016x}'] = runtime
    if stream.read(1):
        raise ValueError('trailing import-map bytes')
    return result


def unknown_row(saved, status):
    return dict(saved=saved, identity=status,
                domains={d: dict(status=status, value=None) for d in DOMAINS})


def actor_row(actor):
    """Caller must validate the original strict frame/contract first."""
    return dict(saved=actor['saved'], identity='live', domains={
        'entity': dict(status='supported', value={key: actor[key] for key in ('class', 'macro', 'owner')}),
        'topology': dict(status='supported', value=dict(parent_saved=actor['parent_saved'])),
        'motion': dict(status='supported', value={key: actor[key] for key in ('controller', 'active', 'other')}),
        'health': dict(status='supported', value=dict(stored_hull_bits=actor['hull_bits'])),
        'timing': dict(status='unsupported_layout', value=None),
    })


def from_portable(frame, contract, cohort, imports, source):
    # Revalidate original strict semantics; no frame field or hash is changed.
    native_portable.encode_frame(frame, contract)
    if set(imports) - cohort.members:
        raise ValueError('cohort drops imported IDs')
    # Also validate supplied in-memory maps, not only read_imports callers.
    previous = 0; runtimes = set()
    for saved, runtime in imports.items():
        value = native_portable.hexword(saved, 16, 'imported saved ID')
        native_portable.uint(runtime, 64, 'runtime ID')
        if value <= previous or (runtime and runtime in runtimes):
            raise ValueError('unordered or ambiguous supplied import map')
        previous = value
        if runtime: runtimes.add(runtime)
    actors = {actor['saved']: actor for actor in frame['actors']}
    if any(not imports.get(saved) or saved not in cohort.members for saved in actors):
        raise ValueError('portable live actor lacks unique imported identity')
    header = dict(schema=SCHEMA, source=source, snapshot_sha256=contract['snapshot_sha256'],
                  content_sha256=frame['contract_sha256'], cohort_sha256=cohort.sha256,
                  cohort_count=len(cohort.ids), stage='joined_main_pre_tick',
                  tick=frame['tick'], game_bits=frame['game_bits'])

    def rows():
        for saved in cohort.ids:
            if saved in actors:
                yield actor_row(actors[saved])
            elif saved not in imports:
                yield unknown_row(saved, 'missing_import')
            else:
                yield unknown_row(saved, 'unresolved_import' if imports[saved] == 0 else 'not_captured')
    return header, rows()


def bounded_json(path, maximum):
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError('JSON input exceeds bound')
    from native_world_portable import unique_pairs
    return json.loads(data, object_pairs_hook=unique_pairs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--imports', type=Path, required=True)
    parser.add_argument('--frame', type=Path, required=True, help='decoded strict portable frame JSON')
    parser.add_argument('--contract', type=Path, required=True)
    parser.add_argument('--cohort', type=Path, help='fixed ordered saved-ID list; default is the complete import inventory')
    parser.add_argument('--source', choices=('windows', 'linux', 'offline'), required=True)
    parser.add_argument('--out', type=Path, required=True, help='fresh private diagnostic output, never a runtime config')
    args = parser.parse_args()
    with args.imports.open('rb') as stream: imports = read_imports(stream)
    cohort = Cohort(bounded_json(args.cohort, 32*1024*1024) if args.cohort else imports)
    header, rows = from_portable(bounded_json(args.frame, 65536), bounded_json(args.contract, 65536), cohort, imports, args.source)
    # A failed write is an incomplete file that the reader rejects; no footer is
    # synthesized on exception. Never replace an existing capture.
    with args.out.open('xb') as stream: footer = encode(stream, header, rows, cohort)
    print(json.dumps(dict(diagnostic_only=True, cohort_count=len(cohort.ids), **footer), sort_keys=True))


if __name__ == '__main__':
    main()
