"""Closed local manifest and read-only inputs for the one-native runner."""
import hashlib,json,re
from pathlib import Path
from native_getup_diagnostic import CONTRACT,FALSE_CLAIMS,hex_field
from native_transition_queue_diag import configured as queue_evidence_configured
from native_transition_prebind import configured as prebind_configured

OPTIONS=70377334178981
SNAPSHOT='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
FIXED=dict(schema=1,contract=CONTRACT,epoch=1,native_clients=1,physical_slot=1,options=OPTIONS,snapshot_sha=SNAPSHOT,
           first=1,action_tick=30,last=120,**FALSE_CLAIMS)
KEYS=set(FIXED)|{'token','run','host','port','lane','inputs','execution_receipt'}

def file_record(path):
    path=Path(path)
    if path.is_symlink() or not path.is_file():raise ValueError('regular immutable input required')
    with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path.resolve()),sha256=digest,bytes=path.stat().st_size)

def tree_record(path):
    path=Path(path)
    if path.is_symlink() or not path.is_dir():raise ValueError('unlinked input tree required')
    files={}
    for item in sorted(path.rglob('*')):
        if item.is_symlink() or getattr(item,'is_junction',lambda:False)():raise ValueError('linked input tree')
        if item.is_file():
            record=file_record(item);files[item.relative_to(path).as_posix()]={k:record[k] for k in ('sha256','bytes')}
    if not files or len(files)>4096:raise ValueError('bounded nonempty input tree required')
    return dict(path=str(path.resolve()),files=files)

def manifest(token,port,lane,lane_tool,base,overlay,snapshot,execution_receipt):
    value=dict(FIXED,token=token,run=hashlib.sha256(hex_field(token,32).encode()).hexdigest(),host='127.0.0.1',port=port,lane=lane,
               inputs=dict(lane_tool=file_record(lane_tool),base=tree_record(base),overlay=tree_record(overlay),snapshot=file_record(snapshot)),
               execution_receipt=file_record(execution_receipt))
    if queue_evidence_configured((Path(overlay)/'ui/config.lua.append').read_text()):value['queue_evidence']=1
    if prebind_configured((Path(overlay)/'ui/config.lua.append').read_text()):value['prebind_receipt']=1
    check(value,verify_files=True);return value

def check(value,verify_files=False):
    if not isinstance(value,dict) or set(value) not in (KEYS,KEYS|{'queue_evidence'},KEYS|{'queue_evidence','prebind_receipt'}):raise ValueError('closed diagnostic manifest required')
    if 'queue_evidence' in value and (type(value['queue_evidence']) is not int or value['queue_evidence']!=1):raise ValueError('diagnostic queue evidence schema')
    if 'prebind_receipt' in value and (type(value['prebind_receipt']) is not int or value['prebind_receipt']!=1):raise ValueError('diagnostic prebind receipt schema')
    if any(type(value[k]) is not type(v) or value[k]!=v for k,v in FIXED.items()):raise ValueError('diagnostic manifest contract')
    token=hex_field(value['token'],32)
    if value['run']!=hashlib.sha256(token.encode()).hexdigest() or value['host']!='127.0.0.1':raise ValueError('diagnostic run/endpoint')
    if type(value['port']) is not int or not 1024<=value['port']<=65535 or value['lane'] not in ('b1','b2','b3','b4'):raise ValueError('diagnostic lane/port')
    inputs=value['inputs']
    if not isinstance(inputs,dict) or set(inputs)!={'lane_tool','base','overlay','snapshot'}:raise ValueError('diagnostic input set')
    for name,record in inputs.items():
        if not isinstance(record,dict) or not isinstance(record.get('path'),str) or not Path(record['path']).is_absolute():raise ValueError('absolute input path')
        if name in ('base','overlay'):
            if set(record)!={'path','files'} or not isinstance(record['files'],dict) or not 1<=len(record['files'])<=4096:raise ValueError('tree record')
            for path,entry in record['files'].items():
                if not isinstance(path,str) or not re.fullmatch(r'[A-Za-z0-9_. /-]+',path) or any(p in ('','..','.') for p in path.split('/')) or path.startswith('/'):raise ValueError('tree relative path')
                if set(entry)!={'sha256','bytes'} or type(entry['bytes']) is not int or not 0<=entry['bytes']<=2**32:raise ValueError('tree file width')
                hex_field(entry['sha256'],64)
            actual=tree_record(record['path']) if verify_files else record
        else:
            if set(record)!={'path','sha256','bytes'} or type(record['bytes']) is not int or not 0<record['bytes']<=2**32:raise ValueError('file record')
            hex_field(record['sha256'],64);actual=file_record(record['path']) if verify_files else record
        if actual!=record:raise ValueError('immutable input changed: '+name)
    if inputs['snapshot']['sha256']!=SNAPSHOT:raise ValueError('exact snapshot required')
    if 'native/x4lan_gate_64.dll' not in inputs['overlay']['files']:raise ValueError('diagnostic gate DLL absent')
    receipt=value['execution_receipt']
    if (not isinstance(receipt,dict) or set(receipt)!={'path','sha256','bytes'} or not isinstance(receipt['path'],str) or
        not Path(receipt['path']).is_absolute() or type(receipt['bytes']) is not int or not 0<receipt['bytes']<=8*1024*1024):raise ValueError('execution receipt record required')
    hex_field(receipt['sha256'],64)
    if verify_files:
        from native_getup_diagnostic_execution import load
        load(receipt,lane_tool=inputs['lane_tool']['path'])
    return value

def configured_text(value):
    check(value)
    data=(Path(value['inputs']['overlay']['path'])/'ui/config.lua.append').read_bytes()
    expected=value['inputs']['overlay']['files'].get('ui/config.lua.append')
    if expected!={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}:raise ValueError('manifest-bound overlay configuration differs')
    text=data.decode('utf-8')
    for old,new in {'@X4LANE_PORT@':str(value['port']),'@X4LANE_TOKEN@':value['token'],'@X4LANE_SLOT@':'p1'}.items():text=text.replace(old,new)
    from validate_native_getup_diagnostic import configuration
    cfg=configuration(text)
    if cfg['queue_evidence']!=value.get('queue_evidence',0):raise ValueError('manifest queue evidence differs from overlay')
    if cfg['prebind_receipt']!=value.get('prebind_receipt',0):raise ValueError('manifest prebind receipt differs from overlay')
    if cfg['run']!=value['run']:raise ValueError('overlay run mismatch')
    if re.findall(r'\bhost\s*=\s*"([^"]*)"',text)!=['127.0.0.1'] or re.findall(r'\bport\s*=\s*(\d+)',text)!=[str(value['port'])]:raise ValueError('one configured loopback endpoint required')
    return text

def check_installed_config(value,text):
    """Bind every closed native setting to the immutable overlay's plan.

    Profile repinning and inert base defaults are outside these fixed native
    assignments. Watch ordering and explicitly present defaults stay bound too.
    The lane's own installed SHA is additional evidence, not the authority here.
    """
    from validate_native_getup_diagnostic import configuration
    intended=configuration(configured_text(value));installed=configuration(text)
    if installed!=intended:raise ValueError('installed diagnostic configuration differs from immutable overlay')
    return installed

def install_lane(lane,value,lane_args,snapshot):
    """Only the explicit manifest opts one released Run into loopback policy.

    Admission, process ownership, profile protection and cleanup stay released.
    No session/client object, config fallback or policy replacement is created.
    """
    check(value,verify_files=True);configured_text(value)
    if not lane_args or lane_args[0]!='run' or file_record(snapshot)!=value['inputs']['snapshot']:raise ValueError('diagnostic requires one snapshot run')
    allowed={'--agent','--label','--lane','--collect','--base','--overlay','--start','--seed','--timeout','--wait','--settle','--until','--expect','--fail-on','--repin-profile'}
    cursor=1
    while cursor<len(lane_args):
        key=lane_args[cursor]
        if key not in allowed:raise ValueError('unsupported diagnostic lane argument')
        cursor+=1 if key=='--repin-profile' else 2
    if cursor!=len(lane_args):raise ValueError('incomplete lane argument')
    def option(name):
        at=[i for i,s in enumerate(lane_args) if s==name]
        if len(at)!=1 or at[0]+1>=len(lane_args):raise ValueError('one '+name+' required')
        return lane_args[at[0]+1]
    if any(s in lane_args for s in ('--lua','--extension','--ui-file','--overwrite')):raise ValueError('diagnostic extra payload refused')
    if option('--lane')!=value['lane']:raise ValueError('manifest lane differs')
    for key,expected in (('--start','menu'),('--seed','276944566'),('--settle','3')):
        if option(key)!=expected:raise ValueError('diagnostic launch setting differs')
    for argument,key in (('--base','base'),('--overlay','overlay')):
        if Path(option(argument)).resolve()!=Path(value['inputs'][key]['path']):raise ValueError('manifest input differs')
    from native_getup_diagnostic_execution import load,verify_runtime
    execution=load(value['execution_receipt'],lane_tool=value['inputs']['lane_tool']['path'])
    verify_runtime(execution,lane,lane.core.Paths.from_env(lane.ROOT))
    original_x4lock=lane.ensure_x4lock
    def pinned_x4lock(paths,build=True):
        verify_runtime(execution,lane,paths)
        result=original_x4lock(paths,build=False)
        if file_record(result)!=execution['x4lock']:raise ValueError('unpinned x4lock result')
        return result
    lane.ensure_x4lock=pinned_x4lock
    original=lane.Run.__init__
    def init(run,*args,**kwargs):
        original(run,*args,**kwargs)
        verify_runtime(execution,lane,run.paths)
        run.extra_tokens.update({'@X4LANE_PORT@':str(value['port']),'@X4LANE_TOKEN@':value['token'],'@X4LANE_SLOT@':'p1'})
        run.network='loopback'
        run.result['getup_diagnostic']=dict(FIXED,run=value['run'],host=value['host'],port=value['port'],lane=value['lane'])
        if 'queue_evidence' in value:run.result['getup_diagnostic']['queue_evidence']=1
        if 'prebind_receipt' in value:run.result['getup_diagnostic']['prebind_receipt']=1
    lane.Run.__init__=init
    prepare=lane.Run.prepare
    def prepare_checked(run):
        check(value,verify_files=True);verify_runtime(execution,lane,run.paths);configured_text(value);prepare(run)
        check_installed_config(value,(run.lane.lab/'ui/config.lua').read_text())
        # Record the actual installed DLL in this isolated profile. No writes.
        actual=file_record(run.lane.lab/'native/x4lan_gate_64.dll')
        expected=value['inputs']['overlay']['files']['native/x4lan_gate_64.dll']
        if any(actual[k]!=expected[k] for k in expected):raise ValueError('installed diagnostic DLL differs')
        run.result['getup_diagnostic']['installed_gate_sha256']=actual['sha256']
    lane.Run.prepare=prepare_checked
