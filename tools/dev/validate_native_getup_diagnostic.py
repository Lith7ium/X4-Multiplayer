"""Independent one-native GetUp observations. No paired/session success path."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys,importlib.util
if __name__=='__main__':
    _policy_path=Path(__file__).with_name('native_getup_source_only.py')
    _policy_spec=importlib.util.spec_from_file_location('x4lan_getup_source_only',_policy_path)
    _policy=importlib.util.module_from_spec(_policy_spec)
    _policy_bytes=_policy_path.read_bytes();exec(compile(_policy_bytes,str(_policy_path),'exec'),_policy.__dict__)
    sys.modules['x4lan_getup_source_only']=_policy;_policy.remember(_policy_path,_policy_bytes);_policy.install();_policy.record_script(__file__)
from native_getup_diagnostic import CONTRACT,LIMIT,FALSE_CLAIMS,Diagnostic,integer,hex_field
from native_player_getup import evidence as getup_evidence,bootstrap_plan
from native_player_origin import configured_evidence as origin_evidence
from native_transition_evidence import configured_evidence as transition_evidence
from native_world_evidence import evidence as world_evidence,configured_watches,configured_schema
from native_world_trace import evidence as trace_evidence
from native_world_queue import evidence as queue_evidence
from validate_native_gate import parse_samples  # Pure row reconstruction, never validate().
from native_getup_diagnostic_contract import check as check_manifest,file_record,FIXED

OPTIONS=70377334178981
SNAPSHOT='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
PASSIVE=('serial_pathing','world_identities','actor_diag','flight_diag','ai_diag','agent_diag','ship_diag','nav_diag','spline_diag',
         'world_trace_early','world_queue_trace','player_transition_observer','world_timing_fields')
ALLOWED=set(PASSIVE)|set(('test hash start_time snapshot_sha trace_stride trace_window world_hash_stride world_hash_schema world_hash_watch '
    'world_trace_ticks player_probe player_transition_manifest player_transition_last_tick player_origin_token diagnostic_contract player_transition_queue_evidence player_transition_prebind_receipt').split())
STATUS=set(('schema contract run epoch native_clients physical_slot options tick issued calls returns unwinds failures terminal_started '
    'terminal_finished closed restored reason stage session_pass parity_checked independent_native_players completed_release').split())

def configuration(config):
    values={}
    for name,value in re.findall(r'\bnative_gate_([a-z_]+)\s*=\s*([^\r\n]+)',config):
        if name in values or name not in ALLOWED:raise ValueError('duplicate/unsupported diagnostic configuration '+name)
        values[name]=value.strip()
    required={name:'true' for name in PASSIVE};required.update(test='true',world_hash_stride='1',player_transition_last_tick='120')
    if any(values.get(k)!=v for k,v in required.items()):raise ValueError('diagnostic passive configuration missing')
    if values.get('start_time','5')!='5' or values.get('trace_stride','1')!='1' or 'trace_window' in values:raise ValueError('diagnostic capture window differs')
    def quoted(name):
        value=values.get(name,'');m=re.fullmatch(r'([\'"])([^\'"\r\n]+)\1',value)
        if not m:raise ValueError('quoted diagnostic configuration '+name)
        return m[2]
    if quoted('snapshot_sha')!=SNAPSHOT or quoted('hash')!='battle' or quoted('player_probe')!='diagnostic-getup' or quoted('diagnostic_contract')!=CONTRACT:
        raise ValueError('diagnostic fixed identity/mode')
    if quoted('player_transition_manifest')!='2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player':raise ValueError('selected transition manifest')
    window=integer(values.get('world_trace_ticks',''),12)
    if window<2:raise ValueError('raw trace window')
    plan=bootstrap_plan(config,diagnostic=True)
    from native_transition_queue_diag import configured as queue_evidence_configured
    queue_evidence=int(queue_evidence_configured(config))
    from native_transition_prebind import configured as prebind_configured
    prebind_receipt=int(prebind_configured(config))
    tokens=re.findall(r'\btoken\s*=\s*"([0-9a-f]{32})"',config)
    if len(tokens)!=1:raise ValueError('one native token required')
    if re.findall(r'(?m)^X4LAN_CONFIG\.slot\s*=\s*"(p[12])"\s*$',config)!=['p1']:raise ValueError('physical slot1 required')
    if re.findall(r'(?m)^X4LAN_CONFIG\.enabled\s*=\s*(\w+)\s*$',config)!=['false']:raise ValueError('ordinary networking adapter must remain disabled')
    if re.findall(r'(?m)^X4LAN_CONFIG\.host\s*=\s*"([^"]*)"\s*$',config)!=['127.0.0.1']:raise ValueError('one loopback host assignment required')
    ports=re.findall(r'(?m)^X4LAN_CONFIG\.port\s*=\s*([^\r\n]*)',config)
    if len(ports)!=1 or integer(ports[0],65535)<1024:raise ValueError('one bounded coordinator port required')
    watches=configured_watches(config)
    if not watches:raise ValueError('passive world watches missing')
    return dict(token=tokens[0],run=hashlib.sha256(tokens[0].encode()).hexdigest(),plan=plan,
                watches=watches,schema=configured_schema(config),trace_last=window,port=int(ports[0]),native_values=values,queue_evidence=queue_evidence,prebind_receipt=prebind_receipt)

def transport_evidence(document,token,errors):
    try:
        d=Diagnostic(token);transcript=document['transcript']
        if not isinstance(transcript,list) or len(transcript)>4096:raise ValueError('transcript capacity')
        previous=-1.0
        for row in transcript:
            if set(row)!=set(('direction','at','endpoint','packet')):raise ValueError('transcript row shape')
            at=row['at']
            if type(at) not in (float,int) or not math.isfinite(at) or at<previous:raise ValueError('transcript time')
            previous=at
            endpoint=row['endpoint']
            if not isinstance(endpoint,list) or len(endpoint)!=2 or endpoint[0]!='127.0.0.1' or type(endpoint[1]) is not int or not 1024<=endpoint[1]<=65535:raise ValueError('endpoint')
            if not isinstance(row['packet'],str) or len(row['packet'])>=512:raise ValueError('transcript packet')
            if row['direction']=='in':d.receive(row['packet'].encode('ascii'),tuple(endpoint),at)
            elif row['direction']!='out':raise ValueError('transcript direction')
        expected=json.loads(json.dumps(d.summary()))
        if document!=expected:raise ValueError('coordinator result differs from independent transcript replay')
        if not expected['local_transport_valid'] or expected['rejected']:raise ValueError('local transport incomplete/failed')
        return {int(k):v for k,v in document['states'].items()}
    except (KeyError,TypeError,ValueError,UnicodeError) as error:errors.append('diagnostic transport: '+str(error));return {}

def native_status(log,run,errors):
    result=None
    try:
        rows=re.findall(r'NATIVE_GATE GETUP_DIAGNOSTIC_STOP ([^\r\n]+)',log)
        if len(rows)!=1 or len(rows[0])>=640:raise ValueError('one bounded native terminal required')
        fields=[part.split('=') for part in rows[0].split(' ')];result=dict(fields)
        if len(fields)!=len(result) or set(result)!=STATUS:raise ValueError('terminal field set')
        literals=dict(contract=CONTRACT,run=run,stage='closed')
        for key,value in literals.items():
            if result[key]!=value:raise ValueError('terminal '+key)
        for key in STATUS-literals.keys():result[key]=integer(result[key],2**53-1 if key=='options' else 2**32-1)
        expected=dict(schema=1,epoch=1,native_clients=1,physical_slot=1,options=OPTIONS,tick=120,issued=120,calls=121,returns=121,
                      unwinds=0,failures=0,terminal_started=1,terminal_finished=1,closed=1,restored=1,reason=1,
                      session_pass=0,parity_checked=0,independent_native_players=0,completed_release=0)
        if any(result[k]!=v for k,v in expected.items()):raise ValueError('terminal native accounting/restoration')
    except (ValueError,TypeError,KeyError) as error:errors.append('diagnostic native receipt: '+str(error))
    return result

def local_evidence(log,config,document,lane,manifest):
    errors=[];result=dict(schema=1,category='local_action_diagnostic',contract=CONTRACT,native_clients=1,**FALSE_CLAIMS)
    try:cfg=configuration(config)
    except ValueError as error:return dict(result,local_contract_valid=False,errors=[str(error)])
    states=transport_evidence(document,cfg['token'],errors);status=native_status(log,cfg['run'],errors)
    try:
        check_manifest(manifest)
        if cfg['queue_evidence']!=manifest.get('queue_evidence',0):raise ValueError('manifest queue evidence differs')
        if cfg['prebind_receipt']!=manifest.get('prebind_receipt',0):raise ValueError('manifest prebind receipt differs')
        if manifest['run']!=cfg['run'] or manifest['token']!=cfg['token'] or manifest['port']!=cfg['port']:raise ValueError('manifest run/token/endpoint differs')
        expected=dict(FIXED,run=cfg['run'],host=manifest['host'],port=manifest['port'],lane=manifest['lane'],
                      installed_gate_sha256=manifest['inputs']['overlay']['files']['native/x4lan_gate_64.dll']['sha256'])
        if cfg['queue_evidence']:expected['queue_evidence']=1
        if cfg['prebind_receipt']:expected['prebind_receipt']=1
        if lane.get('getup_diagnostic')!=expected:raise ValueError('installed one-native contract differs')
    except (KeyError,TypeError,ValueError) as error:errors.append('immutable diagnostic manifest: '+str(error))
    if lane.get('lane_restored') is not True or lane.get('verdict')!='pass' or lane.get('exit_code')!=0:errors.append('owned lane cleanup/run failed')
    if re.search(r'NATIVE_GATE (?:ERROR|REFUSED|.*DRAIN_ERROR)|Error in MD cue md\.X4LAN_Native|Error while executing.*native_gate_',log):errors.append('raw native/Lua failure retained')
    selected,inputs,action=getup_evidence(log,1,config,states,errors,diagnostic=True)
    origin=origin_evidence(log,config,1,errors);transition=transition_evidence(log,config,1,cfg['watches'],errors)
    world,world_coverage=world_evidence(log,1,1,120,errors,cfg['watches'],True,cfg['schema'])
    trace,trace_coverage=trace_evidence(log,errors,'local raw world trace',cfg['watches'])
    trace_stop=trace_coverage.get('stop',{})
    if trace_stop.get('schema')!=3 or trace_stop.get('early')!=1 or trace_stop.get('last')!=cfg['trace_last']:
        errors.append('configured early raw trace window differs')
    _,queue_coverage=queue_evidence(log,errors,'local raw world queue',trace,cfg['watches'])
    errors.extend(queue_coverage.get('provenance_errors',[]))
    samples=parse_samples(log,1,errors)
    for tick,row in states.items():
        if tick==0:data='SNAPSHOT_BYTES_1|'+SNAPSHOT+'|'+cfg['plan']
        else:data=samples.get(tick,'BATTLE_FIXTURE_PENDING_1')+'|'+inputs.get(tick,'MISSING_INPUT')+'|'+selected.get(tick,'MISSING_PLAYER')+'|'+world.get(tick,'MISSING_WORLD')
        if hashlib.sha256(data.encode()).hexdigest()!=row['digest']:errors.append(f'local canonical hash differs at{tick}')
    stops=re.findall(r'NATIVE_GATE STOP slot=p1 completed=(\d+) reason=(\d+) restored=(\d+) wait_game_delta=([^ ]+) max_dt_error=([^\r\n]+)',log)
    if len(stops)!=1 or stops[0][:3]!=('120','1','1'):errors.append('native gate stop missing/failed')
    else:
        try:
            drift,dt=map(float,stops[0][3:])
            if not math.isfinite(drift) or not math.isfinite(dt) or drift!=0 or not 0<=dt<=1e-8:errors.append('native clock guard failed')
        except ValueError:errors.append('native clock grammar')
    for name in ('FLIGHT','AI','AGENT','SHIP','NAV','SPLINE'):
        rows=re.findall(r'NATIVE_GATE '+name+r'_STOP ([^\r\n]+)',log)
        if len(rows)!=1 or not re.search(r'\brestored=1\b',rows[0]) or not re.search(r'\bfailures=0\b',rows[0]):errors.append(name+' passive restoration/failure')
    if not re.search(r'NATIVE_GATE BOOT phase=3 dt_restored=1(?:\r?\n|$)',log):errors.append('fixed dt restoration absent')
    if not re.search(r'NATIVE_GATE PATH joins=\d+ restored=1 rng_seeds=0(?:\r?\n|$)',log):errors.append('serial path restoration/native RNG contract')
    if not re.search(r'NATIVE_GATE WORLD_HASH_STOP scans=120 failures=0(?:\r?\n|$)',log):errors.append('raw world scan accounting')
    identities=re.findall(r'NATIVE_GATE WORLD_IDENTITY_STOP calls=(\d+) count=(\d+) late=(\d+) restored=(\d+) failures=(\d+)(?=\r?\n|$)',log)
    if (len(identities)!=1 or identities[0][3:]!=('1','0') or not 0<int(identities[0][1])<=524288 or
        int(identities[0][0])<int(identities[0][1]) or any(row['imported']!=int(identities[0][1]) for row in world_coverage)):
        errors.append('raw saved identity import/restoration differs')
    return dict(result,local_contract_valid=not errors,errors=errors,native_status=status,action_evidence=action,
                request_accepted=action['evidence_valid'] and action['request_accepted'],
                delivery_observed=action['evidence_valid'] and action['delivery_observed'],
                selected_publication_observed=transition['evidence_valid'] and any(e['kind']==2 for e in transition['events'].values()),
                model_dispatch_observed=action['evidence_valid'] and action['model_dispatch_observed'],origin=origin,transition=transition,
                raw_world_coverage=world_coverage,raw_trace_coverage=trace_coverage,raw_queue_coverage=queue_coverage)

def _validate(run):
    run=Path(run)
    if any(run.glob('client*')):raise ValueError('diagnostic cannot contain paired client directories')
    native=run/'native'
    contract_path=run/'diagnostic-contract.json';manifest=json.loads(contract_path.read_text())
    check_manifest(manifest,verify_files=True)
    lane=json.loads((native/'result.json').read_text());config_path=native/'installed/x4_lan_lab-config.lua'
    from native_getup_diagnostic_contract import check_installed_config
    check_installed_config(manifest,config_path.read_text())
    result=local_evidence((native/'engine-output.log').read_text(errors='replace'),config_path.read_text(),
        json.loads((run/'diagnostic-transport.json').read_text()),lane,manifest)
    expected_runner=dict(schema=1,contract=CONTRACT,run=manifest['run'],native_clients=1,runner_exit=0,
        coordinator_joined=True,failure=None,manifest_sha256=file_record(contract_path)['sha256'],execution_receipt=manifest['execution_receipt'],**FALSE_CLAIMS)
    if json.loads((run/'diagnostic-runner.json').read_text())!=expected_runner:result['errors'].append('diagnostic runner closure differs')
    if lane.get('install',{}).get('sha256',{}).get('ui/config.lua')!=file_record(config_path)['sha256']:result['errors'].append('installed config hash differs')
    snapshot=lane.get('native_snapshot',{})
    if snapshot.get('mode')!='load' or snapshot.get('slot')!='save_009' or snapshot.get('restored') is not True or snapshot.get('staged')!={k:manifest['inputs']['snapshot'][k] for k in ('sha256','bytes')}:
        result['errors'].append('snapshot ownership/cleanup differs')
    if lane.get('profile') not in ('45850','45851','45853','45854','45855','45856','45857','45858') or lane.get('lane')!=manifest['lane']:
        result['errors'].append('disposable lane identity differs')
    result['local_contract_valid']=not result['errors']
    return result

def validate(run):
    try:return _validate(run)
    except (OSError,ValueError,KeyError,TypeError,OverflowError) as error:
        return dict(schema=1,category='local_action_diagnostic',contract=CONTRACT,native_clients=1,
                    local_contract_valid=False,errors=[str(error)],**FALSE_CLAIMS)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args()
    result=validate(a.run);print(json.dumps(result,indent=2));raise SystemExit(0 if result['local_contract_valid'] else 1)

if __name__=='__main__':main()
