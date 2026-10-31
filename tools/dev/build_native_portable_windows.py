"""Build a fresh inert Windows participant base; never install or launch X4."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from native_portable import hexword, validate_contract
from native_portable_lane import ARTIFACTS, INERT_SETTINGS

EXE_SHA = '19750a6563889a970f434b5566eb396c6b2dc29ff814bd3e336f838176ad6891'
UI = (b'<?xml version="1.0"?><addon name="x4_lan_lab"><environment type="menus">'
      b'<dependency name="ego_detailmonitor" /><file name="ui/settings.lua" />'
      b'<file name="ui/native_portable_windows.lua" /><file name="ui/native_portable_loader.lua" />'
      b'</environment></addon>\n')
CONTENT = (b'<?xml version="1.0"?><content id="x4_lan_lab" name="Owned portable snapshot probe" '
           b'version="11" save="false"><dependency version="900" /></content>\n')
CONFIG = b'X4LAN_CONFIG={enabled=false,host="127.0.0.1",token=""}\n'


def sha(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def build(contract_path, fixture, dll, dll_sha, adapter, adapter_sha, output):
    contract = json.loads(contract_path.read_text()); contract_sha = validate_contract(contract)
    if output.exists(): raise ValueError('output must be fresh')
    hexword(dll_sha, 64, 'reviewed native digest'); hexword(adapter_sha, 64, 'reviewed adapter digest')
    if sha(dll) != dll_sha or sha(adapter) != adapter_sha: raise ValueError('participant artifact bytes differ')
    expected = contract['fixture_files']
    if set(expected) != {'md/native_gate_battle.xml', 'md/x4lan.xml', 'ui/native_portable_loader.lua'}:
        raise ValueError('unexpected shared fixture files')
    if any(path.is_symlink() for path in fixture.rglob('*')): raise ValueError('linked shared fixture')
    actual = {p.relative_to(fixture).as_posix(): sha(p) for p in fixture.rglob('*') if p.is_file()}
    if actual != expected: raise ValueError('shared fixture differs from complete contract')
    inert = ET.parse(fixture / 'md/x4lan.xml').getroot()
    if (inert.tag != 'mdscript' or inert.attrib != {'name': 'X4LAN_PortableInert'}
            or len(inert) != 1 or inert[0].tag != 'cues' or inert[0].attrib or len(inert[0])):
        raise ValueError('required lane MD file is not inert')
    output.mkdir(parents=True); base = output / 'base'; shutil.copytree(fixture, base)
    files = {'content.xml': CONTENT, 'ui.xml': UI, 'ui/config.lua': CONFIG, 'ui/settings.lua': INERT_SETTINGS,
             'ui/native_portable_windows.lua': adapter.read_bytes(), 'native/x4lan_gate_64.dll': dll.read_bytes()}
    for name, data in files.items():
        target = base / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    artifacts = {name: sha(base / name) for name in ARTIFACTS}
    if artifacts['native/x4lan_gate_64.dll'] != dll_sha or artifacts['ui/native_portable_windows.lua'] != adapter_sha:
        raise ValueError('participant source changed during copy')
    copied = {name: sha(base / name) for name in expected}
    if copied != expected: raise ValueError('shared source changed during copy')
    complete = {p.relative_to(base).as_posix(): sha(p) for p in base.rglob('*') if p.is_file()}
    if any(p.is_symlink() for p in base.rglob('*')) or complete != {**expected, **artifacts}:
        raise ValueError('copied base includes changed or unmanifested files')
    (output / 'artifacts.json').write_text(json.dumps(artifacts, indent=2) + '\n')
    shutil.copyfile(contract_path, output / 'contract.json')
    if validate_contract(json.loads((output / 'contract.json').read_text())) != contract_sha:
        raise ValueError('contract changed during copy')
    result = {'contract_sha256': contract_sha, 'platform': 'windows', 'executable_sha256': EXE_SHA,
              'agent_sha256': dll_sha, 'adapter_sha256': adapter_sha, 'shared_files': copied,
              'native_launch': False, 'status': 'inert base; exact participant and runner review required'}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('contract', 'fixture', 'dll', 'adapter', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--dll-sha256', required=True); parser.add_argument('--adapter-sha256', required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.contract, args.fixture, args.dll, args.dll_sha256,
                           args.adapter, args.adapter_sha256, args.out)))


if __name__ == '__main__': main()
