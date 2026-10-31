"""Closed source/dependency receipt for the explicit one-native diagnostic.

The final receipt is generated from a reviewed frozen export, never inferred
from the mutable author's checkout. This is not OS/interpreter attestation.
"""
import argparse,hashlib,json,re,sys
from pathlib import Path
from native_getup_diagnostic_contract import file_record

CURRENT_ROOT=Path(__file__).resolve().parents[2]
CONTRACT='diagnostic-getup-execution-1'
KEYS={'schema','contract','import_policy','source_root','tools_dev','lane_root','lane_tree','autostart','x4lock'}
REQUIRED_SOURCE={'run_native_getup_diagnostic.py','native_gate_lane.py','native_getup_diagnostic.py',
                 'native_getup_diagnostic_contract.py','native_getup_diagnostic_execution.py','validate_native_getup_diagnostic.py','native_getup_source_only.py'}
REQUIRED_LANE={'dev/x4lane.py','dev/lane/lanecore.py','dev/lane/lanewin.py','native/engine/signatures.py','RELEASE.txt'}

def record_shape(row,path=False):
    if (not isinstance(row,dict) or set(row)!=({'path','sha256','bytes'} if path else {'sha256','bytes'}) or
        type(row['bytes']) is not int or not 0<=row['bytes']<=2**32 or not isinstance(row['sha256'],str) or
        re.fullmatch(r'[0-9a-f]{64}',row['sha256']) is None):raise ValueError('execution record grammar')
    if path and (not isinstance(row['path'],str) or not Path(row['path']).is_absolute()):raise ValueError('execution record path')

def inventory(root):
    root=Path(root)
    if not root.is_dir() or root.is_symlink() or getattr(root,'is_junction',lambda:False)():raise ValueError('physical execution tree required')
    files={}
    for item in sorted(root.rglob('*')):
        relative=item.relative_to(root)
        if '__pycache__' in relative.parts or item.suffix in ('.pyc','.pyo'):continue
        if item.is_symlink() or getattr(item,'is_junction',lambda:False)():raise ValueError('linked execution dependency')
        if item.is_file():
            row=file_record(item);files[relative.as_posix()]={k:row[k] for k in ('sha256','bytes')}
    if not 1<=len(files)<=8192:raise ValueError('bounded execution inventory required')
    return dict(path=str(root.resolve()),files=files)

def capture(source_root,lane_root,x4lock):
    source_root=Path(source_root).resolve();lane_root=Path(lane_root).resolve()
    value=dict(schema=1,contract=CONTRACT,import_policy='source-only-1',source_root=str(source_root),tools_dev=inventory(source_root/'tools/dev'),
        lane_root=str(lane_root),lane_tree=inventory(lane_root),autostart=inventory(lane_root/'dev/lane/x4lane_autostart'),x4lock=file_record(x4lock))
    verify(value,current_root=source_root,lane_tool=lane_root/'dev/x4lane.py');return value

def verify(value,*,current_root=None,lane_tool=None):
    if (not isinstance(value,dict) or set(value)!=KEYS or type(value['schema']) is not int or value['schema']!=1 or
        value['contract']!=CONTRACT or value['import_policy']!='source-only-1'):raise ValueError('closed execution receipt required')
    current_root=CURRENT_ROOT if current_root is None else Path(current_root)
    for key in ('source_root','lane_root'):
        if not isinstance(value[key],str) or not Path(value[key]).is_absolute() or str(Path(value[key]).resolve())!=value[key]:raise ValueError('canonical execution root required')
    source=Path(value['source_root']);lane=Path(value['lane_root'])
    if source!=current_root.resolve():raise ValueError('current source ROOT differs from execution receipt')
    expected={'tools_dev':source/'tools/dev','lane_tree':lane,'autostart':lane/'dev/lane/x4lane_autostart'}
    for key,path in expected.items():
        row=value[key]
        if not isinstance(row,dict) or set(row)!={'path','files'} or not isinstance(row['files'],dict) or row['path']!=str(path):raise ValueError('execution inventory shape: '+key)
        for entry in row['files'].values():record_shape(entry)
        if row!=inventory(path):raise ValueError('execution inventory differs: '+key)
    if not REQUIRED_SOURCE<=value['tools_dev']['files'].keys() or not REQUIRED_LANE<=value['lane_tree']['files'].keys():raise ValueError('execution dependency missing')
    row=value['x4lock']
    record_shape(row,path=True)
    if row!=file_record(row['path']):raise ValueError('execution x4lock differs')
    if lane_tool is not None and Path(lane_tool).resolve()!=lane/'dev/x4lane.py':raise ValueError('lane tool outside execution receipt')
    return value

def load(reference,*,current_root=None,lane_tool=None):
    record_shape(reference,path=True)
    path=reference['path']
    if not isinstance(path,str) or not Path(path).is_absolute() or not 0<reference.get('bytes',0)<=8*1024*1024:raise ValueError('bounded absolute execution receipt required')
    data=Path(path).read_bytes()
    if reference!=dict(path=str(Path(path).resolve()),sha256=hashlib.sha256(data).hexdigest(),bytes=len(data)):raise ValueError('execution receipt path/digest differs')
    value=verify(json.loads(data),current_root=current_root,lane_tool=lane_tool)
    policy=sys.modules.get('x4lan_getup_source_only')
    if policy is None:raise ValueError('diagnostic source-only policy absent')
    if not hasattr(policy,'verify') or not hasattr(policy,'bind'):raise ValueError('diagnostic source-only policy invalid')
    policy.bind((value['tools_dev'],value['lane_tree']))
    policy.verify((value['tools_dev'],value['lane_tree']))
    return value

def verify_runtime(value,lane,paths):
    """Check module-cache origins and actual resolved helper locations, read-only."""
    root=Path(value['lane_root'])
    if Path(lane.ROOT).resolve()!=root or Path(lane.core.__file__).resolve()!=root/'dev/lane/lanecore.py':raise ValueError('loaded lane/core origin differs')
    for name,expected in (('lanecore',root/'dev/lane/lanecore.py'),('lanewin',root/'dev/lane/lanewin.py')):
        module=sys.modules.get(name)
        if module is not None and Path(getattr(module,'__file__','')).resolve()!=expected:raise ValueError('cached '+name+' origin differs')
    if Path(paths.repo).resolve()!=root or Path(paths.autostart_source).resolve()!=Path(value['autostart']['path']):raise ValueError('runtime autostart location differs')
    if file_record(paths.x4lock_exe)!=value['x4lock']:raise ValueError('runtime x4lock location/bytes differ')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source-root','lane-root','x4lock','out'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();value=capture(args.source_root,args.lane_root,args.x4lock)
    with args.out.open('x') as stream:json.dump(value,stream,indent=2);stream.write('\n')

if __name__=='__main__':main()
