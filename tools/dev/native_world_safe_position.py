"""Passive selected GetSafePos candidate evidence. No avoidance/RNG correction."""
import re
import struct
from native_world_position import (decode as decode_position, Reader, GQ, GD, require, finite,
    numeric, obj, valid_object, class_signature, flatten_numbers, json_safe, rng_step)

WIRE_SIZE, ROW_BYTES, ROWS = 6184, 256, 25
WITNESS = 'slot stack_limit stack_base caller parent_return frame reference'.split()
STOP = set('schema capacity read_limit byte_limit wire_size row_bytes reserved complete exported rows calls open peak restore_open closed failures dropped owned protection_pending flush_pending patch_failures patch_errors'.split())


def configured(config):
    rows=re.findall(r'^\s*X4LAN_CONFIG\.native_gate_world_safe_position\s*=\s*(.*?)\s*$',config,re.M)
    if not rows and 'native_gate_world_safe_position' not in config:return False
    require(rows==['true'],'safe position flag must be one explicit true')
    return True


def selected(parent):
    p=parent['event']
    return (p['id'],p['name'],p['version'],p['slot'],p['stage'])==(0x80d54,'move.idle',4,51,1)


def decode(raw):
    require(len(raw)==WIRE_SIZE,'safe position byte count')
    e=decode_position(raw[:5320]);r=Reader(raw[5320:])
    e['caller_witness']=[r.words(WITNESS,8),r.words(WITNESS,8)]
    e['passed_reference']=[r.take(376),r.take(376)]
    require(r.at==WIRE_SIZE-5320,'safe position decoder extent')
    return e


def validate(e,actions,values):
    parent=actions[e['action']];pe,before,after=(parent[k] for k in ('event','before','after'))
    require(selected(parent) and parent.get('validation_errors')==[] and not pe['errors'],'invalid selected completed GetSafePos Action')
    require((pe['type'],pe['action'],pe['pc_before'],pe['pc_after'])==(0x2c0cc88,0xbe2320,52,52),'GetSafePos type/PC')
    require(e['schema']==2 and not any(e[k] for k in ('errors','abnormal','foreign','children','parent')),'failed native safe position receipt')
    require(e['calls']==e['returns']==[1,0,1,1] and e['samples']==1,'missing/extra candidate/radius/sampler original')
    require(e['depth']==1 and 0<e['enter']<e['leave'] and e['thread']==pe['thread'] and 0<e['reads']<=256 and 0<e['bytes']<=8192,'safe position bounds/thread')
    base=before['vtable']-0x2c0cc88
    require(base>=0x10000 and base%0x10000==0 and e['image']==base,'safe position module base')
    require(e['caller']==base+0xc72905 and e['descriptor']==before['node']+0x28 and e['frame']==before['frame'] and
        e['optional']>=0x10000 and e['result']==e['output'],'safe position native arguments')
    require(e['caller_witness'][0]==e['caller_witness'][1],'changed native caller frame')
    w=e['caller_witness'][0];t=w['slot']
    require(t%16==8 and 0<w['stack_limit']<=t<=(1<<64)-0x290 and t+0x290<=w['stack_base'],'caller frame bounds/alignment')
    require(w['caller']==base+0xc72905 and w['parent_return']==base+0xbe2382 and w['frame']==e['frame'] and
        w['reference']==e['optional'] and e['output']==t+0xc8,'caller frame provenance')
    require(e['passed_reference'][0]==e['passed_reference'][1],'changed passed-reference identity')
    reference=obj(e['passed_reference'][0])
    require(valid_object(reference,base) and reference['object']==e['optional'] and reference['saved'],'unsupported passed-reference identity')
    # These reserved fields have no transform-interception coverage in this schema.
    unused=('space','reference','transform','time','fifth','space_present','reference_present','transform_guard','transforms','objects','radius_witness')
    require(all(v==0 for key in unused for v in flatten_numbers(e[key])),'nonzero reserved safe position fields')
    branch=e['branch'][0]
    require(e['branch']==[branch,branch] and e['radius_branch']==[branch,branch] and
        branch['kind']<=3 and branch['coordinates']<=1 and branch['range_kind']==2,'changed/unsupported sampled branch')
    require(e['range']==e['descriptor']+0x68 and e['radius_frame']==e['frame'] and not e['radius_optional'],'radius actual arguments')
    require(e['bases'][0]==e['bases'][1],'evaluated candidate base changed during radius')
    require(all(finite(int.from_bytes(v[i:i+4],'little'),4) for v in (e['bases'][0],e['output_bytes']) for i in (0,4,8)),'nonfinite candidate position')
    s=e['samplers'][0]
    require(all(v==0 for unused_sample in e['samplers'][1:] for v in flatten_numbers(unused_sample)),'extra sampler bytes')
    require(s['source']==1 and e['tls']>=0x10000 and s['rng']==e['tls']+0x318 and s['normal']==1 and not s['abnormal'],'sampler original/TLS source')
    require(s['caller']==base+0xc59951 and s['parent_return']==base+0xc59b2f and s['descriptor']==e['range'] and s['frame']==e['frame'],'sampler scope')
    require(s['slot']%16==8 and 0<s['stack_limit']<=s['slot']<=(1<<64)-0x158 and s['slot']+0x158<=s['stack_base'],'sampler stack bounds')
    require((s['stack_limit'],s['stack_base'])==(w['stack_limit'],w['stack_base']),'sampler/outer TIB interval differs')
    require(s['minimum'][0]==s['minimum'][1] and s['difference'][0]==s['difference'][1],'evaluated sampler Values changed')
    minimum,difference=numeric(s['minimum'][0]),numeric(s['difference'][0])
    require(all(finite(v,4) for v in (s['span'],s['result'],e['radius'])),'nonfinite sampled radius')
    guards=e['outer']+e['radius_guard']+s['guards']
    mapping={'saved':'saved_time','app':'app_flags','loading':'vbi_loading','pc':None,'rng':None,'flags':None,'mxcsr':None,'x87cw':None}
    for g in guards:
        for key in GQ+GD:
            name=mapping.get(key,key)
            if name in before:require(g[key]==before[name]==after[name],'changed partial guard '+key)
        require(g['pc']==52 and g['flags']==0 and g['view']==1 and g['tag']==0x66,'partial PC/tag/flags')
        require(g['tls_time']==g['game']==g['saved'] and all(finite(g[k]) for k in ('tls_time','game','real','saved')),'partial clocks')
        require(g['mxcsr']<=0xffff and g['x87cw']<=0xffff,'FP controls')
        require({k:v for k,v in g.items() if k!='rng'}=={k:v for k,v in guards[0].items() if k!='rng'},'partial lifetime/phase/FP changed')
    require(1<=s['distribution']<=5 and s['parameter']<=4096,'unsupported sampler distribution')
    steps=1 if s['distribution']==1 or s['parameter']<2 else s['parameter'];rng=s['guards'][0]['rng']
    for _ in range(steps):rng=rng_step(rng)
    require(rng==s['guards'][1]['rng']==e['radius_guard'][1]['rng'],'sampler endpoint relation')
    # Candidate continuation endpoints are measured, not attributed to a
    # fixed direction path for every copied descriptor kind.
    if isinstance(values,dict):values=values.values()
    assigned=[v for v in values if v['event']['action']==e['action'] and v['event']['kind']==0]
    require(len(assigned)==1,'missing/ambiguous final GetSafePos assignment')
    v=assigned[0];ve=v['event']
    require(v.get('validation_errors')==[] and not ve['errors'] and ve['caller']==0xbe23af and
        v['value']['tag']==0x63 and v['value']['kind']==2,'invalid final GetSafePos Value')
    require(ve['a']==before['node']+0x1b8 and ve['b']==e['frame'] and ve['c']==before['node']+0x1c8 and
        ve['thread']==e['thread'] and ve['pc_before']==ve['pc_after']==52 and not ve['flags_before'] and not ve['flags_after'],
        'final GetSafePos assignment invocation')
    payload=v['value']['payload'];require(isinstance(payload,bytes) and len(payload)==16,'final GetSafePos payload width')
    require(ve['rng_before']==ve['rng_after']==pe['rng_after'],'final assignment/Action RNG join')
    return {'saved_owner':f'{pe["id"]:016x}','name':pe['name'],'version':pe['version'],'slot':pe['slot'],'pc':52,
        'branch':branch,'passed_reference':reference['saved'],'reference_class':class_signature(reference,base),
        'base':e['bases'][0].hex(),'minimum':minimum,'difference':difference,'span':s['span'],'distribution':s['distribution'],
        'parameter':s['parameter'],'sampler_result':s['result'],'radius':e['radius'],'candidate':e['output_bytes'].hex(),
        'final_value':payload.hex(),'candidate_payload_equal_final_value':e['output_bytes']==payload,
        'rng':[g['rng'] for g in guards],'action_rng':[pe['rng_before'],pe['rng_after']],
        'sampler_steps':steps,'candidate_continuation_rng':[rng,e['outer'][1]['rng']],
        'mxcsr':guards[0]['mxcsr'],'x87cw':guards[0]['x87cw'],
        'avoidance_inputs_covered':False,'same_coordinate_semantics_proven':False}


def evidence(lines,actions,values,errors,label='world safe position',enclosing_errors=()):
    if isinstance(lines,str):lines=lines.splitlines()
    rows={};order=[];stop=None;local=[];matched=0
    for line in lines:
        if 'NATIVE_GATE WORLD_SAFE_POSITION' not in line:continue
        matched+=1
        if matched>4*ROWS+1:local.append(label+': stream capacity');break
        try:
            raw=line.split('NATIVE_GATE ',1)[1];require(raw.isascii() and len(raw)<2048,'raw row bound')
            name,body=raw.rstrip('\r\n').split(' ',1);pairs=[p.split('=',1) for p in body.split()]
            require(all(len(p)==2 for p in pairs) and len({p[0] for p in pairs})==len(pairs),'duplicate/malformed keys');fields=dict(pairs)
            if name=='WORLD_SAFE_POSITION_STOP':
                require(stop is None and set(fields)==STOP and all(re.fullmatch('[0-9]+',v) for v in fields.values()),'STOP schema')
                stop={k:int(v) for k,v in fields.items()};require(all(v<1<<64 for v in stop.values()),'STOP overflow')
            else:
                require(name=='WORLD_SAFE_POSITION' and stop is None and set(fields)=={'schema','event','part','data'} and fields['schema']=='1','row schema/order')
                require(re.fullmatch('[0-9]+',fields['event']) and re.fullmatch('[0-9]+',fields['part']),'row integers')
                event,part=int(fields['event']),int(fields['part']);key=event,part
                require(1<=event<=4 and 0<=part<ROWS and key not in rows,'row capacity/duplicate')
                size=min(ROW_BYTES,WIRE_SIZE-part*ROW_BYTES);require(re.fullmatch('[0-9a-f]{'+str(size*2)+'}',fields['data']),'row bytes')
                rows[key]=bytes.fromhex(fields['data']);order.append(key)
        except (ValueError,KeyError,TypeError) as exc:local.append(f'{label}: {exc}')
    actions=list(actions);parents={p['event']['event']:p for p in actions};values=list(values.values()) if isinstance(values,dict) else list(values)
    events=[];expected=[]
    for n in sorted({k[0] for k in rows}):
        errs=[];record={'event':n,'supported':False,'validation_errors':errs};expected.extend((n,p) for p in range(ROWS))
        try:
            e=decode(b''.join(rows[n,p] for p in range(ROWS)));record['decoded']=e;require(e['event']==n,'event identity')
            record['semantic']=validate(e,parents,values);record['supported']=True
        except (ValueError,KeyError,TypeError,IndexError,struct.error) as exc:errs.append(f'{label} event {n}: {exc}')
        local.extend(errs);events.append(record)
    if order!=expected:local.append(label+': missing/reordered rows')
    fixed={'schema':1,'capacity':4,'read_limit':256,'byte_limit':8192,'wire_size':WIRE_SIZE,'row_bytes':ROW_BYTES,'reserved':1,'complete':1,'rows':ROWS,'exported':ROWS,'peak':1,'closed':1}
    if not stop or any(stop.get(k)!=v for k,v in fixed.items()) or any(stop.get(k)!=0 for k in ('open','restore_open','failures','dropped','owned','protection_pending','flush_pending','patch_failures','patch_errors')) or stop.get('calls',0)<1:local.append(label+': incomplete/failed STOP')
    chosen=[p['event']['event'] for p in actions if selected(p)]
    if len(events)!=1 or len(chosen)!=1 or events[0].get('decoded',{}).get('action')!=chosen[0]:local.append(label+': selected Action coverage gap')
    if events and (events[0].get('decoded',{}).get('enter'),events[0].get('decoded',{}).get('leave'))!=(1,2):local.append(label+': boundary chronology')
    errors.extend(local)
    return {'events':events,'stop':stop,'errors':local,'enclosing_errors':list(enclosing_errors),'selected_valid':not local,
        'coverage_complete':not local and not enclosing_errors,'avoidance_inputs_covered':False,'canonical_unknown_identity':False}


def paired_diagnostics(captures,claimed_pass,errors):
    if not captures:return None
    ordered=sorted(captures,key=lambda c:c['slot']);valid=len(ordered)==2 and [c['slot'] for c in ordered]==[1,2] and all(c['evidence']['selected_valid'] for c in ordered)
    differences=[]
    if valid:
        a,b=(c['evidence']['events'][0]['semantic'] for c in ordered)
        differences=[{'field':k,'left':a.get(k),'right':b.get(k)} for k in sorted(a.keys()|b.keys()) if a.get(k)!=b.get(k)]
    else:errors.append('world safe position: invalid paired selected receipts')
    if claimed_pass and (not valid or differences):errors.append('world safe position: claimed pass contradicts selected evidence')
    return json_safe({'selected_valid':valid,'differences':differences,'clients':ordered,'avoidance_inputs_covered':False,'cross_platform_math_equivalence':False})
