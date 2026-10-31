"""Compose a fresh disposable management overlay; never edit frozen inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET
from native_creation_rng import manifest as rng_manifest, bootstrap_preimage as rng_preimage


def construction_semantic(plan_path: Path):
    root=ET.parse(plan_path).getroot()
    expected=ET.fromstring('''<diff><add sel="/plans"><plan id="x4lan_native_construction_base"><entry index="1" macro="struct_arg_base_02_macro"><offset><position x="0" y="0" z="0"/></offset></entry></plan></add></diff>''')
    def shape(element):
        if (element.text or '').strip() or (element.tail or '').strip():
            raise ValueError('unsupported construction plan content')
        return element.tag,tuple(sorted(element.attrib.items())),tuple(shape(child) for child in element)
    if shape(root)!=shape(expected):
        raise ValueError('construction plan differs from bounded semantic payload')
    return 'CONSTRUCTION_PLAN_1|struct_arg_base_02_macro|0,0,0|0,0,0|no_connections|no_equipment|default|unpaid_plot_2000,2000,2000'


def compose(source: Path, output: Path, family='management', creation_trace=False, creation_rng_epoch=None, registry_creation=False, creation_ack_schema=1):
    if family not in ('management','construction'):
        raise ValueError('unsupported native diagnostic family')
    if creation_trace and family!='construction':
        raise ValueError('creation trace requires construction family')
    if creation_rng_epoch is not None and (not creation_trace or family!='construction'):
        raise ValueError('construction RNG requires explicit constructor trace')
    rng_text=rng_manifest(creation_rng_epoch) if creation_rng_epoch is not None else None
    if type(creation_ack_schema) is not int or creation_ack_schema not in (1,2) or (creation_ack_schema==2 and rng_text is None):
        raise ValueError('ACK2 requires explicit construction RNG and a supported acknowledgement schema')
    if registry_creation and rng_text is None:
        raise ValueError('provisional registry evidence requires explicit construction RNG')
    source=source.resolve();output=output.resolve()
    if source.is_relative_to(output) or output.is_relative_to(source):
        raise ValueError('source and output paths overlap; frozen input must remain untouched')
    flag='native_gate_'+family+'_test'
    adapter='X4LAN_NATIVE_'+family.upper()
    config = (source/'ui/config.lua.append').read_text()
    for name in ('native_gate_command_test', 'native_gate_order_state', 'native_gate_snapshot_sha'):
        if not re.search(r'X4LAN_CONFIG\.'+name+r'\s*=', config):
            raise ValueError(f'management requires {name}')
    for name in ('native_gate_world_hash_stride', 'native_gate_docking_state', 'native_gate_player_probe', 'native_gate_management_test', 'native_gate_construction_test', 'native_gate_creation_trace_schema', 'native_gate_creation_rng_schema', 'native_gate_creation_rng_manifest', 'native_gate_creation_rng_ack_schema', 'native_gate_registry_creation_schema'):
        if re.search(r'X4LAN_CONFIG\.'+name+r'\s*=', config):
            raise ValueError(f'combined diagnostic not implemented: {name}')
    if family=='construction':
        for name in ('native_gate_world_identities','native_gate_contact_diag','native_gate_registry_md_trace'):
            if not re.search(r'X4LAN_CONFIG\.'+name+r'\s*=\s*true\b',config):
                raise ValueError(f'construction enrollment requires enabled {name}')
    if output.exists():
        raise ValueError('output must be new; frozen overlays cannot be changed')
    root = Path(__file__).resolve().parent
    semantic=construction_semantic(root/'native_gate_construction_plan.xml') if family=='construction' else None
    manifest = json.loads((source/'x4lane-manifest.json').read_text())
    actual = {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in source.rglob('*') if p.is_file() and p.name != 'x4lane-manifest.json'}
    if actual != manifest:
        raise ValueError('source overlay manifest mismatch')
    probe = (source/'ui/native_gate_probe.lua').read_text()
    if family=='construction' and not re.search(r'X4LAN_NATIVE_CONSTRUCTION[^\r\n]*\.bind_gate[^\r\n]*\(gate(?:,|\))',probe):
        raise ValueError('construction enrollment requires probe bind_gate integration')
    if family=='construction' and ('native_gate_registry_md_trace' not in probe or 'registry_md_installed' not in probe):
        raise ValueError('marked construction requires native MD boundary probe capability')
    if family=='construction' and 'ship_raw_motion' not in probe:
        raise ValueError('construction requires passive raw motion probe capability')
    if creation_trace:
        for marker in ('native_gate_registry_origin_schema','native_gate_registry_origin_identity_schema'):
            if not re.search(r'X4LAN_CONFIG\.'+marker+r'\s*=\s*1\b',config):
                raise ValueError('creation trace requires '+marker)
        if 'native_gate_creation_trace_schema' not in probe or 'X4LAN_NATIVE_CREATION_TRACE' not in probe:
            raise ValueError('creation trace requires probe capability')
        dll=(source/'native/x4lan_gate_64.dll').read_bytes()
        if any(capability not in dll for capability in (b'creation_trace_config',b'creation_trace_status',b'creation_trace_row',b'CREATION_TRACE_2;handle=')):
            raise ValueError('creation trace requires native capability')
    if rng_text is not None:
        if any(capability not in dll for capability in (b'creation_rng_config',b'creation_rng_status',b'creation_rng_row',b'creation_rng_ack_status',b'CREATION_RNG_1;epoch=')) or \
           'X4LAN_NATIVE_CREATION_RNG' not in probe:
            raise ValueError('construction RNG requires native/probe capability')
        if creation_ack_schema==2 and b'creation_rng_ack2_schema' not in dll:
            raise ValueError('construction ACK2 requires native capability')
        if hashlib.sha256((root/'native_gate_construction.xml').read_text().encode()).hexdigest() != '58790b0bcdeba653487e948f6cc6ce86c5ea4eb4c42d4268093d45fa39d600b4':
            raise ValueError('construction RNG MD fixture differs from reviewed source')
        preview=config+'\nX4LAN_CONFIG.native_gate_construction_test = true\nX4LAN_CONFIG.native_gate_creation_trace_schema = 2\n'
        preview+='X4LAN_CONFIG.native_gate_construction_plan_sha = "'+hashlib.sha256(semantic.encode()).hexdigest()+'"\n'
        preview+='X4LAN_CONFIG.native_gate_creation_rng_schema = 1\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = '+str(creation_ack_schema)+'\nX4LAN_CONFIG.native_gate_creation_rng_manifest = "'+rng_text+'"\n'
        if registry_creation:
            if any(capability not in dll for capability in (b'registry_creation_config',b'registry_creation_status',b'registry_creation_identity',b'registry_creation_operation',b'REGISTRY_CREATION_1;handle=')) or 'X4LAN_NATIVE_REGISTRY_CREATION' not in probe:
                raise ValueError('provisional registry requires native/probe capability')
            preview+='X4LAN_CONFIG.native_gate_registry_creation_schema = 1\n'
        rng_preimage(preview)  # Reject unsupported input before any output write.
    edits = {
        " if cfg.native_gate_command_test then\n  assert(s.wire_version==2":
        f" if cfg.{flag} then\n  assert(s.wire_version==2,'{family} requires admitted inputs')\n  input_state=assert({adapter}).apply(tick,s.input1,s.input2)\n elseif cfg.native_gate_command_test then\n  assert(s.wire_version==2",
        " if input_state then data=data..'|'..input_state end":
        f" if input_state then data=data..'|'..input_state end\n if cfg.{flag} then data=data..'|'..{adapter}.sample() end",
        " local command=cfg.native_gate_command_test and X4LAN_NATIVE_COMMANDS.proposal(tick) or '00000000'":
        f" local command=cfg.{flag} and {adapter}.proposal(tick) or (cfg.native_gate_command_test and X4LAN_NATIVE_COMMANDS.proposal(tick) or '00000000')",
    }
    for before, after in edits.items():
        if probe.count(before) != 1:
            raise ValueError('native probe integration point changed')
        probe = probe.replace(before, after)
    shutil.copytree(source, output)
    (output/'ui/native_gate_probe.lua').write_text(probe)
    support='native_management_admission.lua' if family=='management' else 'native_construction_transactions.lua'
    for name, target in (
        (support, 'ui/'+support),
        (f'native_gate_{family}.lua', f'ui/native_gate_{family}.lua'),
        (f'native_gate_{family}.xml', f'md/native_gate_{family}.xml'),
        (f'native_gate_{family}_plan.xml', 'libraries/constructionplans.xml'),
    ):
        (output/target).parent.mkdir(exist_ok=True)
        shutil.copy2(root/name, output/target)
    if family=='construction':
        shutil.copy2(root/'native_construction_raw.lua',output/'ui/native_construction_raw.lua')
    if creation_trace:
        shutil.copy2(root/'native_creation_trace.lua',output/'ui/native_creation_trace.lua')
    if rng_text is not None:
        shutil.copy2(root/'native_creation_rng.lua',output/'ui/native_creation_rng.lua')
    if registry_creation:
        shutil.copy2(root/'native_registry_creation.lua',output/'ui/native_registry_creation.lua')
    ui = (output/'ui.xml.files').read_text()
    raw_support='ui/native_construction_raw.lua\n' if family=='construction' else ''
    trace_support=('ui/native_creation_trace.lua\n' if creation_trace else '')+('ui/native_creation_rng.lua\n' if rng_text is not None else '')
    if registry_creation:trace_support+='ui/native_registry_creation.lua\n'
    (output/'ui.xml.files').write_text(f'ui/{support}\n'+raw_support+trace_support+f'ui/native_gate_{family}.lua\n'+ui)
    config+=f'\nX4LAN_CONFIG.{flag} = true\n'
    if family=='construction':
        config+='X4LAN_CONFIG.native_gate_construction_plan_sha = "'+hashlib.sha256(semantic.encode()).hexdigest()+'"\n'
    if creation_trace:
        config+='X4LAN_CONFIG.native_gate_creation_trace_schema = 2\n'
    if rng_text is not None:
        config+='X4LAN_CONFIG.native_gate_creation_rng_schema = 1\nX4LAN_CONFIG.native_gate_creation_rng_ack_schema = '+str(creation_ack_schema)+'\nX4LAN_CONFIG.native_gate_creation_rng_manifest = "'+rng_text+'"\n'
    if registry_creation:config+='X4LAN_CONFIG.native_gate_registry_creation_schema = 1\n'
    (output/'ui/config.lua.append').write_text(config)
    manifest = {str(p.relative_to(output)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in output.rglob('*') if p.is_file() and p.name != 'x4lane-manifest.json'}
    (output/'x4lane-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlay', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--family',choices=('management','construction'),default='management')
    parser.add_argument('--creation-trace',action='store_true')
    parser.add_argument('--creation-rng-epoch',type=int,help='explicit fixture A/B RNG experiment; natural observation remains default')
    parser.add_argument('--registry-creation',action='store_true',help='provisional identity/operation evidence only; no enrollment')
    parser.add_argument('--creation-ack-schema',type=int,choices=(1,2),default=1,help='explicit ACK2 permits only owned configured post-Load reattachment')
    args = parser.parse_args()
    compose(args.overlay, args.out,args.family,args.creation_trace,args.creation_rng_epoch,args.registry_creation,args.creation_ack_schema)
