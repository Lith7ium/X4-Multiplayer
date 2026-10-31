"""Independent GetUp request evidence. A boolean return never proves release."""
import hashlib
import re
from native_player_origin import evidence as origin_evidence
from native_transition_evidence import evidence as transition_evidence

FIELDS=set('schema run epoch tick last processed claimed accepted calls returns unwinds open stop_open failures closing player ship command1 command2 completed_release independent_player atomic_lease queue_exception_cleanup boundary'.split())

def bootstrap_plan(config,*,diagnostic=False):
    """Exact fixed plan hashed by the real Lua probe; never infer a mode."""
    mode='diagnostic-getup' if diagnostic else 'agreed-getup'
    if re.findall(r'\bnative_gate_player_probe\s*=\s*"([\w-]+)"',config)!=[mode]:raise ValueError('GetUp bootstrap mode')
    tokens=re.findall(r'\btoken\s*=\s*"([0-9a-f]{32})"',config)
    if len(tokens)!=1:raise ValueError('GetUp bootstrap token')
    origins=re.findall(r'\bnative_gate_player_origin_token\s*=\s*"([^"\r\n]*)"',config)
    if origins!=['1|0000000000011e12|00000000000ae79c|1|120']:raise ValueError('GetUp bootstrap identity/window')
    contracts=re.findall(r'\bnative_gate_diagnostic_contract\s*=\s*"([^"\r\n]*)"',config)
    if contracts!=(['diagnostic-getup-1'] if diagnostic else []):raise ValueError('GetUp bootstrap contract')
    run=hashlib.sha256(tokens[0].encode('ascii')).hexdigest()
    prefix='PLAYER_GETUP_LOCAL_PLAN_1:diagnostic-getup-1:1:' if diagnostic else 'PLAYER_GETUP_PLAN_1:'
    from native_transition_queue_diag import configured,SUFFIX
    extra=SUFFIX if configured(config) else ''
    from native_transition_prebind import configured as prebind_configured,SUFFIX as PREBIND_SUFFIX
    if prebind_configured(config):extra+=PREBIND_SUFFIX
    return prefix+run+':1:30:120:0000000000011e12:00000000000ae79c'+extra
def evidence(log,slot,config,states,errors,*,diagnostic=False):
    local=[];samples={};inputs={};stop=None;records=0
    mode='diagnostic-getup' if diagnostic else 'agreed-getup'
    fields=FIELDS|({'contract','native_clients','parity_checked'} if diagnostic else set())
    token=re.findall(r'\btoken\s*=\s*"([0-9a-f]{32})"',config)
    run=hashlib.sha256(token[0].encode()).hexdigest() if len(token)==1 else None
    if not run:local.append('one run token required')
    if re.findall(r'native_gate_player_probe\s*=\s*"([\w-]+)"',config)!=[mode]:local.append('GetUp mode missing/ambiguous')
    ticks=set(states)-{0}
    for line in log.splitlines():
        relevant='NATIVE_GATE PLAYER_GETUP_' in line or 'NATIVE_GATE PLAYER_INPUTS ' in line
        if relevant:
            records+=1
            if records>241 or len(line)>=1024:
                if records<=242:local.append('GetUp record/line capacity exceeded')
                continue
            if stop is not None:local.append('GetUp record after STOP');continue
        if 'NATIVE_GATE PLAYER_GETUP_STOP ' in line:
            try:
                raw=line.split('NATIVE_GATE PLAYER_GETUP_STOP ',1)[1]
                if len(raw)>=768 or stop is not None:raise ValueError()
                parts=[part.split('=') for part in raw.split(' ')];row=dict(parts)
                if len(parts)!=len(row) or set(row)!=fields:raise ValueError()
                literals=dict(run=run,player='0000000000011e12',ship='00000000000ae79c',command1='87010000',command2='00000000',boundary='admitted_ui')
                if diagnostic:literals['contract']='diagnostic-getup-1'
                if any(row[k]!=v for k,v in literals.items()):raise ValueError()
                for key in fields-literals.keys():
                    if not re.fullmatch(r'0|[1-9][0-9]{0,19}',row[key]) or int(row[key])>=1<<64:raise ValueError()
                    row[key]=int(row[key])
                stop=row
            except (ValueError,TypeError):local.append('malformed/duplicate GetUp footer')
        elif 'NATIVE_GATE PLAYER_GETUP_STATE ' in line:
            try:
                match=re.fullmatch(r'([1-9][0-9]{0,2}) data=(.+)',line.split('NATIVE_GATE PLAYER_GETUP_STATE tick=',1)[1]);tick=int(match[1]);data=match[2]
                a,b=data.split('|');f=a.split(':');u=b.split(':')
                if (tick not in ticks or tick!=len(samples)+1 or len(f)!=13 or len(u)!=5 or f[:3]!=['PLAYER_GETUP_LOCAL_1' if diagnostic else 'PLAYER_GETUP_STATE_1',run,'1'] or
                    f[3]!=str(tick) or f[4:6]!=(['1','1'] if tick>=30 else ['0','0']) or
                    f[6] not in (('2','3','4','5') if tick>=30 else ('0',)) or
                    f[7:9]!=['0000000000011e12','00000000000ae79c'] or
                    not re.fullmatch(r'0|[1-9a-f][0-9a-f]{0,7}',f[10]) or int(f[10],16)>=0x6fa9000 or
                    u[0]!='PLAYER_GETUP_UI_1' or u[1]!='0000000000011e12' or any(not re.fullmatch(r'[0-9a-f]{16}',v) for v in u[1:])):raise ValueError()
                released=int(f[6])>=4
                if f[9]!=('2c51158' if released else '2c39488') or f[11:]!=(['0','1'] if released else ['1','1']):raise ValueError()
                if u[4]!=('0000000000000000' if released else '00000000000ae79c'):raise ValueError()
                if (u[2] not in (('00000000000ae79f','00000000000ae79c') if released else ('00000000000ae79f',)) or
                    u[3] not in (('00000000000ae79c','0000000000000000') if released else ('00000000000ae79c',))):raise ValueError()
                samples[tick]=data
            except (ValueError,TypeError,IndexError,AttributeError):local.append('invalid GetUp state')
        elif 'NATIVE_GATE PLAYER_INPUTS ' in line:
            match=re.fullmatch(r'tick=([1-9][0-9]{0,2}) data=(PLAYER_INPUTS_1:[0-9a-f]{8}:[0-9a-f]{8})',line.split('NATIVE_GATE PLAYER_INPUTS ',1)[1])
            if not match:local.append('malformed GetUp batch');continue
            tick=int(match[1]);expected=('87010000' if tick==30 else '00000000','00000000')
            # In local mode input2 is an internal constant, never a peer row.
            admitted=(states.get(tick-1,{}).get('proposal'),'00000000') if diagnostic else tuple(states.get(tick-1,{}).get(str(s),{}).get('command') for s in (1,2))
            if tick!=len(inputs)+1 or tick not in ticks or tuple(match[2].split(':')[1:])!=expected or admitted!=expected:local.append('GetUp batch not admitted')
            inputs[tick]=match[2]
        elif relevant:local.append('unknown GetUp record')
    if set(samples)!=ticks or set(inputs)!=ticks:local.append('GetUp state/batch coverage incomplete')
    if not stop:local.append('GetUp footer missing')
    elif (any(stop[k] for k in ('unwinds','open','stop_open','failures','completed_release','independent_player','atomic_lease','queue_exception_cleanup')) or
          any(stop[k]!=v for k,v in dict(schema=2 if diagnostic else 1,epoch=1,tick=30,last=120,closing=1,processed=max(ticks,default=0),
             claimed=int(30 in ticks),accepted=int(30 in ticks),calls=int(30 in ticks),returns=int(30 in ticks)).items())):
        local.append('GetUp native claim/return/stop mismatch')
    if diagnostic and (slot!=1 or not stop or stop.get('native_clients')!=1 or stop.get('parity_checked')!=0):local.append('local diagnostic declaration invalid')
    origin=origin_evidence(log)
    if not origin['evidence_valid'] or not origin['stop'] or origin['stop']['schema']!=3:local.append('GetUp origin/transition evidence invalid')
    if origin['stop'] and any(origin['stop'].get(k,0) for k in ('other','unknown','excluded')):local.append('GetUp origin has unsupported coverage')
    tickets=list(origin['tickets'].values())
    if len(tickets)!=int(30 in ticks):local.append('GetUp request/origin count mismatch')
    origin_transition=transition_evidence(log);transitions=origin_transition['events']
    from native_transition_prebind import configured as prebind_configured
    if prebind_configured(config):
        prerequisite=origin_transition.get('prebind_receipt') or {};footer=prerequisite.get('stop') or {}
        if 30 in ticks:
            if (not prerequisite.get('evidence_valid') or len(tickets)!=1 or footer.get('action_tick')!=30 or
                tickets[0]['request']!=1 or tickets[0]['post_tick']!=30 or footer.get('action_thread')!=tickets[0]['post_thread'] or
                footer.get('action_clock',1<<64)>tickets[0]['transition_before']):
                local.append('GetUp lacks completed initialization prerequisite before request')
        elif any(footer.get(k,0) for k in ('action_tick','action_clock','action_thread','action_ticket','action_dispatch')):
            local.append('GetUp initialization prerequisite has no actual request')
    linked={e['transition'] for e in origin['edges'].values()}
    initial=next(iter(origin_transition['watches'].values()),{}).get('s0',{})
    selected_publications=[e for e in transitions.values() if e['kind']==2]
    eligible=[e for e in selected_publications if e['event'] in linked and
              e['s0']['controller']==initial.get('controller') and e['old']==initial.get('controller') and
              e['s0']['controller_type']==0x2c39488 and e['s1']['controller_type']==0x2c51158 and
              e['new']==e['s1']['controller'] and e['s1']['player']==0 and e['s1']['owns']==1]
    if selected_publications and (len(eligible)!=1 or len(selected_publications)!=1):local.append('GetUp publication identity changed or repeated')
    first_publication=eligible[0] if len(eligible)==1 and len(selected_publications)==1 else None
    previous_phase=0
    for tick,data in samples.items():
        f=data.split('|')[0].split(':');phase=int(f[6])
        if phase<previous_phase:local.append('GetUp phase regressed')
        previous_phase=phase
        if first_publication and tick>=first_publication['tick'] and phase<4:local.append('GetUp returned to pre-publication context')
        if phase>=3 and not any(t['delivery_state']==2 and t['after_tick']<=tick for t in tickets):local.append('GetUp delivery phase lacks returned native delivery')
        publications=[first_publication] if first_publication and first_publication['tick']<=tick else []
        if phase>=4 and not publications:local.append('GetUp public context change lacks selected publication')
        if phase==5 and not any(e['kind']==5 and e['queue_valid']==1 and not e['errors'] and not e['queue_errors'] and
                               e['tick']<=tick and e['publication'] in [p['event'] for p in publications] for e in transitions.values()):
            local.append('GetUp model phase lacks valid dispatch')
    if ticks!=set(range(1,121)):local.append('GetUp bounded fixture incomplete; action acceptance not inferred')
    if 120 in samples and samples[120].split('|')[0].split(':')[6]!='5':local.append('GetUp model dispatch observation incomplete; release completion remains unproved')
    if not re.search(r'native_gate_world_hash_stride\s*=\s*1\b',config):local.append('GetUp strict world contract missing')
    errors.extend(f'player {slot} GetUp: {message}' for message in local)
    return samples,inputs,dict(mode=mode,context=None,errors=local,evidence_valid=not local,
        stop=stop,request_accepted=bool(stop and stop['accepted']),delivery_observed=bool(tickets and tickets[0]['delivery_state']==2),
        model_dispatch_observed=bool(120 in samples and samples[120].split('|')[0].split(':')[6]=='5'),
        completed_releases=[],independent_player_context=False,atomic_lease=False,queue_exception_cleanup=False)
