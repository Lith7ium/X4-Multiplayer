"""Strict selected CreatePosition intervention; raw observer bytes stay raw."""
import hashlib
import json
import re
import struct
from native_world_find_rng import configured as configured_find, selection as find_selection
from native_world_position import require, rng_step, numeric, json_safe

SNAPSHOT='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
DIGEST='a582beee51faba17312d1d025a75a6cd8e0eca078e86efcf3efbe4fb3353aff0'
SEED=0x17bafa51eebe82a5
META='schema event errors thread position action occurrence samples prewrite_refused write_attempted applied restore_required restored debt abnormal normal reads bytes composition find_event'.split()
QUADS='caller_before seed scoped_before sampler_after scoped_after caller_after tls address sampler_slot image raw_outer_before raw_sampler_before frame descriptor anchor_wait find_candidate'.split()
IQ='context entity script exec program order backlink registry group lifetime runtime saved order_id'.split()
ID='state version flags generation'.split()
STOP=set('schema composition capacity reserved complete exported rows calls open peak restore_open closed failures dropped debt used seed digest'.split())


def selection(composition=2):
    require(type(composition) is int and composition in (1,2),'explicit Position Find composition required')
    return dict(schema=1,enabled=False,domain='X4LAN/WORLD_CREATE_POSITION/RNG/v1',snapshot_sha256=SNAPSHOT,
        expected_find_composition=composition,selected=dict(saved_pilot='000000000009d3a4',saved_order='0000000000003cdb',
        saved_script_decimal=38361,script='order.fight.patrol',version=10,slot=215,pc=216,phase=2,tick=0,owner_occurrence=1,
        saved_time_text='6.31667',restore_slot=121,restore_pc=122),inputs=dict(branch=[0,0,0,0,2],base='00'*16,
        minimum_tag=0,minimum_payload='0000000000000000',difference_tag=5,difference_payload='0000000047435000',
        span_bits='47435000',distribution=1,count=1,mxcsr_mask='0000ffc0',mxcsr_value='00001f80',x87_control='027f',
        maximum_tag=5,maximum_bits='47435000',scratch_tag=0,scratch_payload='0000000000000000',math_flags=[[0,1],list(range(7))]),
        program_shape_hash='985d66488a43c795',expected_key_sha256=DIGEST,expected_seed_hex=f'{SEED:016x}')


def key_bytes(doc):
    s,i=doc['selected'],doc['inputs'];name=s['script'].encode('ascii');time=s['saved_time_text'].encode('ascii')
    return (doc['domain'].encode('ascii')+b'\0'+bytes.fromhex(doc['snapshot_sha256'])+
        struct.pack('<QQQH',int(s['saved_pilot'],16),int(s['saved_order'],16),s['saved_script_decimal'],len(name))+name+
        struct.pack('<IQQIQQH',s['version'],s['slot'],s['pc'],s['phase'],s['tick'],s['owner_occurrence'],len(time))+time+
        struct.pack('<QQ5I',s['restore_slot'],s['restore_pc'],*i['branch'])+bytes.fromhex(i['base'])+
        struct.pack('<IQIQ6I',i['minimum_tag'],int(i['minimum_payload'],16),i['difference_tag'],int(i['difference_payload'],16),
                    int(i['span_bits'],16),i['distribution'],i['count'],int(i['mxcsr_mask'],16),int(i['mxcsr_value'],16),int(i['x87_control'],16)))


def manifest_wire(doc):
    expected=selection(doc.get('expected_find_composition'))
    require(json.dumps(doc,sort_keys=True)==json.dumps(expected,sort_keys=True),'unsupported Position RNG manifest')
    key=key_bytes(doc);require(len(key)==260 and hashlib.sha256(key).hexdigest()==DIGEST,'Position semantic key mismatch')
    return f'POSITION_RNG_1;{SNAPSHOT};{doc["expected_find_composition"]};985d66488a43c795;{SEED:016x};'


def configured(config):
    rows=re.findall(r'^\s*X4LAN_CONFIG\.native_gate_world_position_rng_manifest\s*=\s*("(?:\\.|[^"\\])*")\s*$',config,re.M)
    if not rows and 'native_gate_world_position_rng_manifest' not in config:return None
    require(len(rows)==1,'invalid Position RNG config')
    wire=json.loads(rows[0]);matches=[selection(c) for c in (1,2) if manifest_wire(selection(c))==wire]
    require(len(matches)==1,'unsupported Position RNG wire')
    doc=matches[0];find=configured_find(config)
    require(find is not None and find['schema']==doc['expected_find_composition'],'Position Find composition mismatch')
    from native_world_position import configured as position_configured
    require(position_configured(config),'Position RNG requires raw Position observer')
    return doc


def decode(raw):
    require(len(raw)==576,'Position RNG extent')
    at=0
    def integers(names,code):
        nonlocal at
        values=struct.unpack_from('<'+code*len(names),raw,at);at+=struct.calcsize('<'+code*len(names));return dict(zip(names,values))
    def data(n):
        nonlocal at
        out=raw[at:at+n];at+=n;return out
    e=integers(META,'I');e.update(integers(QUADS,'Q'));e['identity']=[integers(IQ,'Q')|integers(ID,'I') for _ in range(2)]
    e['maximum']=[data(16),data(16)];e['scratch']=[data(16),data(16)];e['math']=[list(integers(('a','b'),'I').values()) for _ in range(2)]
    e['output']=data(16);e['digest']=data(32).hex();require(at==576,'Position RNG decode extent');return e


def prepare(log,doc,find,waits,errors,label='Position RNG'):
    manifest_wire(doc)
    before=len(errors);rows={};order=[];stop=None;events={}
    if isinstance(log,str):log=log.splitlines()
    for line in log:
        if 'NATIVE_GATE WORLD_POSITION_RNG' not in line:continue
        try:
            raw=line.split('NATIVE_GATE ',1)[1];require(raw.isascii() and len(raw)+30<1024,'Position RNG raw row bound')
            name,body=raw.rstrip('\r\n').split(' ',1);parts=[p.split('=',1) for p in body.split()]
            require(all(len(p)==2 for p in parts) and len({p[0] for p in parts})==len(parts),'Position RNG duplicate keys');p=dict(parts)
            if name=='WORLD_POSITION_RNG_STOP':
                require(stop is None and set(p)==STOP,'Position RNG STOP schema');stop={}
                for k,v in p.items():
                    require(bool(re.fullmatch('[0-9a-f]{64}' if k=='digest' else '[0-9a-f]{16}' if k=='seed' else '[0-9]+',v)),'Position RNG STOP token')
                    stop[k]=v if k=='digest' else int(v,16 if k=='seed' else 10)
                require(all(type(v) is str or v<1<<64 for v in stop.values()),'Position RNG STOP overflow')
            else:
                require(name=='WORLD_POSITION_RNG' and stop is None and set(p)=={'schema','event','part','data'} and p['schema']=='1','Position RNG row schema/order')
                require(re.fullmatch('[0-9]+',p['event']) and re.fullmatch('[0-9]+',p['part']),'Position RNG row integer')
                event,part=int(p['event']),int(p['part']);key=event,part
                require(1<=event<=8 and 0<=part<3 and key not in rows,'Position RNG duplicate/index')
                require(re.fullmatch('[0-9a-f]{'+str(min(256,576-part*256)*2)+'}',p['data']),'Position RNG row bytes')
                rows[key]=bytes.fromhex(p['data']);order.append(key)
        except (ValueError,KeyError,TypeError) as exc:errors.append(f'{label}: {exc}')
    for n in sorted({e for e,_ in rows}):
        try:
            e=decode(b''.join(rows[n,p] for p in range(3)));require(e['event']==n and e['schema']==1,'Position RNG event/schema');events[n]=e
        except (ValueError,KeyError,struct.error) as exc:errors.append(f'{label} event {n}: {exc}')
    expected=[(n,p) for n in range(1,len(events)+1) for p in range(3)]
    if order!=expected:errors.append(f'{label}: missing/reordered rows')
    expected_stop={'schema':1,'composition':doc['expected_find_composition'],'capacity':8,'reserved':1,'complete':1,'exported':3,'rows':3,
        'open':0,'restore_open':0,'closed':1,'failures':0,'dropped':0,'debt':0,'used':1,'seed':SEED,'digest':DIGEST}
    if not stop or any(stop[k]!=v for k,v in expected_stop.items()) or not 1<=stop['peak']<=stop['calls'] or set(events)!={1}:
        errors.append(f'{label}: missing/failed STOP')
    if not find.get('selected_scope_valid') or find.get('composition')!=doc['expected_find_composition']:
        errors.append(f'{label}: invalid completed Find dependency')
    anchors=[w for w in waits if w['input_output']['id']==0x9d3a4 and w['input_output']['name']=='order.fight.patrol']
    anchor_wait=anchors[0]['raw'][0]['event'] if len(anchors)==1 else 0
    if not anchor_wait or anchors[0].get('validation_errors')!=[]:errors.append(f'{label}: invalid import Wait anchor')
    return dict(events=events,stop=stop,find=find,document=doc,anchor_wait=anchor_wait,raw_valid=len(errors)==before,errors=errors[before:])


def bridge(position,parent,values,capture):
    require(capture['raw_valid'],'unverified Position RNG receipt stream')
    matches=[r for r in capture['events'].values() if r['position']==position['event']]
    if not matches:return None
    require(len(matches)==1,'ambiguous Position RNG bridge');r=matches[0];p=parent['event'];before=parent['before'];after=parent['after'];s=position['samplers'][0]
    require(not r['errors'] and not r['abnormal'] and not r['debt'] and not r['prewrite_refused'] and
            all(r[k]==1 for k in ('occurrence','samples','write_attempted','applied','restore_required','restored','normal','find_event')),'failed Position RNG scope')
    require(0<r['reads']<=256 and 0<r['bytes']<=16384 and r['composition']==capture['document']['expected_find_composition'],'Position RNG bounds/composition')
    require(r['action']==position['action']==p['event'] and r['thread']==position['thread']==p['thread'] and p['id']==0x9d3a4 and
            p['name']=='order.fight.patrol' and p['version']==10 and p['slot']==215 and not p['parent'] and p['depth']==1 and
            before['program_hash']==after['program_hash']==0x985d66488a43c795 and p['boot']==2 and p['tick']==0,'Position selected Action join')
    require(r['identity'][0]==r['identity'][1],'Position order lifetime changed');identity=r['identity'][0]
    find=capture['find']['events'][1]
    require(find[0]['thread']==p['thread'] and 0<find[0]['action']<p['event'], 'Position Find must precede it on the same thread')
    require(r['find_candidate']==find[0]['candidate'] and all(identity=={k:find[n][k] for k in IQ+ID} for n in (1,2,3)), 'Position Find/order identity join')
    for k in ('context','entity','script','exec','program','lifetime','runtime','saved','version'):
        name='id' if k=='saved' else k
        require(identity[k]==before[name]==after[name],'Position selected lifetime '+k)
    require(identity['saved']==0x9d3a4 and identity['version']==10 and identity['backlink']==identity['context'] and identity['order_id'] and
            identity['state'] in (1,5,6,7),'Position order guard')
    require(r['anchor_wait']==capture['anchor_wait'] and r['tls']==position['tls'] and r['address']==s['rng']==r['tls']+0x318 and r['sampler_slot']==s['slot'] and
            r['image']==position['image'] and r['frame']==position['frame'] and r['descriptor']==position['descriptor'],'Position actual pointer joins')
    require(s['slot']+0x188<=s['stack_base'],'Position extra evaluated Value stack extent')
    require(r['maximum'][0]==r['maximum'][1] and numeric(r['maximum'][0])=={'tag':5,'bits':0x47435000,'width':4} and
            r['scratch'][0]==r['scratch'][1] and int.from_bytes(r['scratch'][0][:4],'little')==0 and int.from_bytes(r['scratch'][0][8:],'little')==0,'Position max/scratch inputs')
    require(position['branch'][0]==dict(kind=0,coordinates=0,space_tag=0,reference_tag=0,range_kind=2) and position['bases']==[bytes(16),bytes(16)] and
            numeric(s['minimum'][0])=={'tag':0,'bits':0,'width':4} and int.from_bytes(s['minimum'][0][8:],'little')==0 and
            numeric(s['difference'][0])=={'tag':5,'bits':0x47435000,'width':4} and s['span']==0x47435000 and s['distribution']==s['parameter']==1,'Position exact consumed inputs')
    require(r['math'][0]==r['math'][1] and r['math'][0][0] in (0,1) and r['math'][0][1] in range(7),'Position math dispatch inputs')
    require((position['outer'][0]['mxcsr']&0xffc0)==0x1f80 and position['outer'][0]['x87cw']==0x27f,'Position admitted FP controls')
    require(r['seed']==r['scoped_before']==SEED and r['digest']==DIGEST and r['sampler_after']==s['guards'][1]['rng']==rng_step(SEED) and
            r['scoped_after']==position['outer'][1]['rng']==rng_step(rng_step(rng_step(SEED))) and s['result']==position['radius']==0x46c881ce,'Position seeded native output')
    require(r['raw_outer_before']==position['outer'][0]['rng'] and r['raw_sampler_before']==s['guards'][0]['rng']==r['caller_before']==r['caller_after'] and
            r['output']==position['output_bytes'],'Position raw/effective bridge')
    assignments=[v for v in values if v['event']['action']==p['event'] and v['event']['kind']==0]
    require(len(assignments)==1 and assignments[0]['event']['rng_before']==r['caller_after'],'Position restored assignment join')
    return r


def finalize(capture,positions,errors,label='Position RNG'):
    before=len(errors);events=capture['events'];supported={r['event'] for r in positions['events'] if r['supported']}
    if not positions.get('selected_valid'):errors.append(f'{label}: selected raw Position stream did not validate')
    for r in events.values():
        if r['position'] not in supported:errors.append(f'{label}: selected raw Position did not validate')
    valid=capture['raw_valid'] and len(errors)==before and set(events)=={1}
    return dict(configured=True,selected_scope_valid=valid,composition=capture['document']['expected_find_composition'],
                events=events,stop=capture['stop'],errors=capture['errors']+errors[before:])


def paired_diagnostics(captures,claimed_pass,errors):
    if not captures:return dict(configured=False)
    valid=len(captures)==2 and {c['slot'] for c in captures}=={1,2} and all(c['evidence']['selected_scope_valid'] and not c['errors'] for c in captures)
    valid=valid and captures[0]['evidence']['composition']==captures[1]['evidence']['composition']
    outputs=[]
    if valid:
        for c in captures:
            r=c['evidence']['events'][1];outputs.append((c['evidence']['composition'],r['seed'],r['sampler_after'],r['scoped_after'],r['math'],r['output']))
    equal=valid and outputs[0]==outputs[1]
    if claimed_pass and not equal:errors.append('claimed pass lacks equal verified selected Position RNG scope')
    return json_safe(dict(configured=True,selected_scope_valid=valid,outputs_equal=equal,clients=captures,cross_platform_math_equivalence=False))
