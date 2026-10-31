"""Build a fresh private shared contract and inert fixture; never install/launch."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from build_native_gate_overlay import snapshot_fixture
from native_portable import COVERAGE, SCHEMA, SCHEMA_SHA, UNSUPPORTED, canonical_json, hexword, sha, validate_contract

ROOT = Path(__file__).resolve().parents[2]


def file_sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_stock(path):
    data = json.loads(path.read_text())
    claimed = data.pop('manifest_sha256')
    if sha(canonical_json(data)) != claimed or data.get('payloads_hashed') is not True:
        raise ValueError('stock manifest lacks complete verified payload hashes')
    if len(data.get('files', {})) != 127 or len(data.get('dlc', {})) != 7:
        raise ValueError('stock manifest does not match supported seven-DLC baseline')
    for value in data['files'].values():
        if set(value) != {'bytes', 'sha256'} or type(value['bytes']) is not int or value['bytes'] < 0:
            raise ValueError('incomplete stock file')
        hexword(value['sha256'], 64, 'stock file digest')
    return claimed


def build(snapshot, stock, output, loader=None, import_math=False, shared_fixture=None):
    if output.exists():
        raise ValueError('output must be fresh to preserve prior evidence')
    snapshot_sha = file_sha(snapshot)
    saved = snapshot_fixture(snapshot)
    if file_sha(snapshot) != snapshot_sha:
        raise ValueError('snapshot changed while validating identities')
    import_preflight = None
    if type(import_math) is not bool:
        raise ValueError('import_math must be an explicit boolean')
    if import_math:
        import portable_import_capability as cap
        from preflight_portable_rotation import preflight
        cap.verify_sources(ROOT / 'src/native/engine')
        import_preflight = preflight(snapshot)
        if import_preflight['snapshot_sha256'] != snapshot_sha:
            raise ValueError('snapshot changed during Euler preflight')
    files = {'md/native_gate_battle.xml': ROOT / 'tools/dev/native_gate_battle.xml',
             'md/x4lan.xml': ROOT / 'tools/dev/native_portable_inert.xml',
             'ui/native_portable_loader.lua': loader or ROOT / 'tools/dev/native_portable_loader.lua'}
    if shared_fixture is not None:
        if loader is not None:
            raise ValueError('choose a shared fixture or a loader override, not both')
        # Select exactly these three shared inputs. Platform adapters in a frozen
        # bundle are not shared inputs and are never copied by this builder.
        files = {name: shared_fixture / name for name in files}
    actors = [{'saved': identity, 'fixture_slot': index + 1,
               'macro': 'ship_arg_s_fighter_01_a_macro' if index < 6 else 'ship_xen_s_fighter_01_a_macro',
               'owner': 'argon' if index < 6 else 'xenon'} for index, identity in enumerate(saved)]
    contract = {'schema': SCHEMA, 'schema_sha256': SCHEMA_SHA, 'snapshot_sha256': snapshot_sha,
                'stock_sha256': validate_stock(stock), 'fixture_files': {k: file_sha(v) for k, v in files.items()},
                'actors': sorted(actors, key=lambda row: row['saved']),
                'coverage': list(COVERAGE), 'unsupported': list(UNSUPPORTED)}
    if import_math:
        contract['import_math'] = cap.capability(snapshot_sha)
    contract_sha = validate_contract(contract)
    output.mkdir(parents=True, exist_ok=False)
    for name, source in files.items():
        target = output / 'fixture' / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if file_sha(target) != contract['fixture_files'][name]:
            raise ValueError('fixture changed during copy')
    (output / 'contract.json').write_text(json.dumps(contract, indent=2) + '\n')
    if import_preflight is not None:
        (output / 'import-preflight.json').write_text(json.dumps(import_preflight, indent=2) + '\n')
    (output / 'manifest.json').write_text(json.dumps({'contract_sha256': contract_sha,
        'schema_sha256': SCHEMA_SHA, 'snapshot_sha256': snapshot_sha,
        'status': 'inert fixture; participant adapters and native review required'}, indent=2) + '\n')
    return contract_sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot', type=Path, required=True)
    p.add_argument('--stock-manifest', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--loader', type=Path, help='explicit reviewed common-loader bytes; hash remains in contract')
    p.add_argument('--euler-import', action='store_true', help='explicit new math capability; requires exact saved-input preflight')
    p.add_argument('--shared-fixture', type=Path, help='retain the three exact common files from an existing reviewed fixture')
    args = p.parse_args()
    print(json.dumps({'contract_sha256': build(args.snapshot, args.stock_manifest, args.out, args.loader, args.euler_import, args.shared_fixture),
                      'native_launch': False}))


if __name__ == '__main__': main()
