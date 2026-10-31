"""Bounded queue input evidence; raw addresses/lifetime IDs stay process-local."""
from collections import Counter,defaultdict
import io
import math
import struct

KEYS=set('event kind tick id related reference enter leave enqueue trace_event errors thread ui boot view caller order_type payload_type rng time source_status source_tag name_status source_saved source_pc name command record exec context lifetime payload'.split())
HEX=set('id related reference caller order_type payload_type rng time source_tag source_saved source_pc command record exec context lifetime'.split())
PHASE=set('phase_status early_eligible app_flags vbi_loading main elapsed phase_view phase_tls game real saved_time table'.split())
HEX.update('app_flags phase_tls game real saved_time table'.split())
TYPED=set('order_size order_value raw_caller caller_source dispatch_parent command_header flags_before flags_after'.split())
HEX.add('raw_caller')
BYTES={'payload','command_header','flags_before','flags_after'}
MODEL=0x2c51c38
SMALL={0x2c513b8,0x2c51538,0x2c51518,0x2c514f8,MODEL}
STOP=set('schema capacity reserved complete rows failures dropped installed restored outstanding preimport_calls open peak restore_open'.split())

def evidence(lines,errors,label='queue',trace=None,expected=None):
    if isinstance(lines,str):lines=io.StringIO(lines)
    events={};stop=None;provenance=[]
    def fail(message):errors.append(f'{label}: {message}')
    for line in lines:
        if 'NATIVE_GATE WORLD_QUEUE_' not in line:continue
        text=line.split('NATIVE_GATE WORLD_QUEUE_',1)[1].strip()
        try:
            kind,text=text.split(' ',1);pairs=[x.split('=',1) for x in text.split()];raw=dict(pairs)
            if len(raw)!=len(pairs):raise ValueError('duplicate field')
            if kind=='STOP':
                if stop is not None or set(raw)!=STOP:raise ValueError('duplicate/invalid stop')
                stop={key:int(value) for key,value in raw.items()};continue
            if kind!='EVENT' or stop is not None or set(raw) not in (KEYS,KEYS|PHASE,KEYS|PHASE|TYPED):raise ValueError('invalid event')
            e={key:(value if key=='name' else bytes.fromhex(value) if key in BYTES else int(value,16 if key in HEX else 10)) for key,value in raw.items()}
            e['_schema']=3 if TYPED<=set(raw) else 2 if PHASE<=set(raw) else 1
            if e['event'] in events or not 1<=e['event']<=512 or len(e['payload'])!=0x90:raise ValueError('duplicate/out-of-bound event')
            events[e['event']]=e
        except (ValueError,KeyError,TypeError) as error:fail(f'malformed row ({error})')
    count=len(events)
    if not count:fail('no selected queue activity; hooks unexercised')
    if stop is None:fail('missing stop');stop={}
    if (stop.get('schema') not in (1,2,3) or stop.get('capacity')!=512 or stop.get('reserved')!=count or
        stop.get('complete')!=count or stop.get('rows')!=count or stop.get('failures')!=0 or
        stop.get('dropped')!=0 or stop.get('installed')!=0 or stop.get('restored')!=1 or
        stop.get('open')!=0 or stop.get('restore_open')!=0 or stop.get('peak',0)<1 or
        set(events)!=set(range(1,count+1))):fail('incomplete/failed queue coverage')
    orders=[];claims=Counter();groups=defaultdict(list)
    for index,e in events.items():
        orders.extend((e['enter'],e['leave']));groups[e['id']].append(e)
        if e['_schema']!=stop.get('schema'):fail(f'event {index} inconsistent schema')
        early=False
        typed=e['_schema']==3
        if e['_schema']>=2:
            finite=all(0<=e[k]<2**64 and math.isfinite(struct.unpack('<d',e[k].to_bytes(8,'little'))[0])
                       for k in ('phase_tls','game','real','saved_time'))
            if (e['phase_status']!=1 or e['early_eligible'] not in (0,1) or e['vbi_loading'] not in (0,1) or
                e['main'] not in (0,1) or e['phase_view'] not in (0,1) or e['phase_view']!=e['view'] or
                e['phase_tls']!=e['time'] or e['table']<0x10000 or not finite):fail(f'event {index} unsupported/unstable phase sample')
            early=(e['phase_status']==1 and e['boot']==2 and e['tick']==0 and e['ui']==1 and
                   e['phase_view']==1 and 0<=e['elapsed']<=120000 and finite)
            if e['early_eligible'] and not early:fail(f'event {index} invalid pre-admission eligibility')
        target=e['order_type']==0x2c513d8
        if (e['errors'] or e['kind'] not in ((0,1,2) if typed else (0,1)) or not e['id'] or not 0<=e['tick']<=12 or
            e['boot'] not in (1,2,3) or e['ui'] not in (0,1) or not e['enter']<e['leave'] or
            not 0<e['caller']<0x6fa9000 or not(target or (typed and e['order_type'] in SMALL)) or
            (target and (e['payload_type']!=0x2c4f578 or int.from_bytes(e['payload'][:8],'little')<0x10000 or
                         bool(int.from_bytes(e['payload'][0x10:0x18],'little'))!=bool(e['reference']))) or
            (not target and (e['payload_type'] or e['reference'] or e['payload']!=bytes(0x90)))):fail(f'event {index} unsupported/failed input')
        if typed:
            if (len(e['command_header'])!=16 or len(e['flags_before'])!=13 or len(e['flags_after'])!=13 or
                int.from_bytes(e['command_header'][:8],'little')<0x10000 or e['order_size']!=(0xb0 if target else 0x10) or
                e['record']!=(e['command']+0x10 if target else 0) or e['raw_caller']<0x10000 or
                e['caller_source']!=(1 if e['kind']==1 else 0)):
                fail(f'event {index} invalid typed command/caller')
            if e['order_type']==MODEL:
                if not 0<=e['order_value']<=6 or len(e['command_header'])!=16 or int.from_bytes(e['command_header'][8:12],'little')!=e['order_value'] or e['related']:
                    fail(f'event {index} invalid flight model command')
            elif e['order_type'] in SMALL-{0x2c513b8,MODEL}:
                if e['order_value'] not in (0,1) or len(e['command_header'])!=16 or e['command_header'][8]!=e['order_value'] or e['related']:
                    fail(f'event {index} invalid boolean command')
            elif e['order_value']:fail(f'event {index} unexpected typed value')
            if e['order_type']==0x2c513b8 and bool(int.from_bytes(e['command_header'][8:16],'little'))!=bool(e['related']):
                fail(f'event {index} unmapped abort related owner')
        if expected is not None and e['id'] not in expected:fail(f'event {index} outside watches')
        if e['source_status'] not in range(6) or e['name_status'] not in range(4):fail(f'event {index} invalid provenance status')
        if e['source_status']==2 and (e['ui']!=1 or not(e['boot']==3 or (early and e['early_eligible'])) or not e['exec'] or not e['context'] or not e['lifetime']):
            fail(f'event {index} inconsistent claimed execution ownership')
        # Dispatch normally has no script running. Only enqueue is a producer.
        if e['kind']==0:
            if e['enqueue']!=index or e['trace_event'] or e['command']<0x10000 or (not typed and e['record']!=e['command']+0x10) or (typed and e['dispatch_parent']):fail(f'event {index} invalid transfer token')
            if e['source_status']!=2 or e['source_tag']!=0x66 or e['name_status']!=1 or e['name']=='-' or not e['source_saved']:
                provenance.append(f'{label}: enqueue {index} lacks proven named enclosing AI (status={e["source_status"]}, tag={e["source_tag"]:x}, name_status={e["name_status"]})')
        else:
            source=events.get(e['enqueue'])
            if not typed or e['kind']==2:claims[e['enqueue']]+=1
            if (not source or source['kind']!=0 or source['id']!=e['id'] or source['enter']>=e['enter'] or
                source['record']!=e['record'] or source['command']!=e['command'] or source['payload']!=e['payload'] or
                source['related']!=e['related'] or source['reference']!=e['reference']):fail(f'event {index} unmatched/changed dispatch input')
            if typed:
                if not source or any(source.get(k)!=e[k] for k in ('order_size','order_type','order_value','command_header')):
                    fail(f'event {index} changed typed input')
                if e['kind']==2:
                    if e['caller']!=0xeacf66 or e['dispatch_parent']:fail(f'event {index} foreign manager dispatch')
                else:
                    parent=events.get(e['dispatch_parent'])
                    if (not target or e['caller']!=0xeacf66 or not parent or parent['kind']!=2 or
                        any(parent[k]!=e[k] for k in ('id','tick','thread','enqueue','command','record')) or
                        not(parent['enter']<e['enter']<e['leave']<parent['leave'])):fail(f'event {index} missing exact enclosing dispatch')
            if trace is not None:
                t=trace.get(e['trace_event'])
                hook='order' if typed and e['kind']==2 else 'pending'
                if not t or t['hook']!=hook or t['id']!=e['id'] or t['tick']!=e['tick'] or t['errors']:fail(f'event {index} missing Agent bracket')
                if typed and e['kind']==1 and t and parent and t.get('parent')!=parent['trace_event']:fail(f'event {index} wrong Agent bracket parent')
    if sorted(orders)!=list(range(1,count*2+1)):fail('invalid queue causal order')
    if any(n!=1 for n in claims.values()):fail('duplicate dispatch generation')
    generations={};dispatches={e['enqueue']:e for e in events.values() if e['kind']==(2 if e['_schema']==3 else 1)}
    for e in sorted((e for e in events.values() if e['kind']==0),key=lambda e:e['enter']):
        previous=generations.get(e['command'])
        if previous:
            retired=dispatches.get(previous['event'])
            if not retired or retired['leave']>=e['enter']:fail('ambiguous reused command lifetime')
        generations[e['command']]=e
    if stop.get('outstanding')!=sum(e['kind']==0 and e['event'] not in claims for e in events.values()):fail('invalid outstanding generation count')
    if stop.get('schema')==3 and stop.get('outstanding'):fail('undispatched command generations remain; completeness not proven')
    appends=Counter(e['dispatch_parent'] for e in events.values() if e['_schema']==3 and e['kind']==1)
    if any(n!=1 for n in appends.values()):fail('duplicate nested append')
    for group in groups.values():
        enqueues=sorted((e for e in group if e['kind']==0),key=lambda e:e['enter'])
        for i,e in enumerate(enqueues):
            for other in enqueues[i+1:]:
                if other['enter']>=e['leave']:break
                if e['thread']!=other['thread']:fail('unsupported concurrent enqueue owner')
    return events,{'stop':stop,'provenance_errors':provenance,'hooks':dict(Counter({0:'enqueue',1:'append',2:'dispatch'}.get(e['kind'],'unknown') for e in events.values()))}

def semantic(e):
    data=e['payload'];out={k:e[k] for k in ('tick','boot','caller','order_type','payload_type','related','reference','source_status','source_tag','name_status','source_saved','source_pc','name','view','rng','time')}
    if e.get('_schema',1)>=2:
        out.update({k:e[k] for k in ('phase_status','early_eligible','vbi_loading','phase_tls','game','saved_time')})
        out['synchronous_loading']=bool(e['app_flags']&2)
        # Process elapsed/global real time and raw table address remain local
        # diagnostics. They are not synchronized simulation-clock equality.
    if e.get('_schema')==3:
        out.update({k:e[k] for k in ('order_size','order_value','caller_source')})
        out.update({k:e[k].hex() for k in ('flags_before','flags_after')})
    for i in range(4):out[f'vector{i}']=data[0x20+i*16:0x2c+i*16].hex()
    for offset,size in ((0x64,4),(0x74,1),(0x79,1),(0x7b,1),(0x7d,1),(0x80,8)):out[f'field{offset:x}']=data[offset:offset+size].hex()
    return out

def compare(left,right):
    def keys(events):
        ordinals=Counter();result={};enqueue_keys={}
        for e in sorted(events.values(),key=lambda e:e['enter']):
            if e['kind']==0:
                ordinals[e['id']]+=1;key=(f'{e["id"]:016x}',ordinals[e['id']],0);enqueue_keys[e['event']]=key
            else:key=(*enqueue_keys.get(e['enqueue'],(f'{e["id"]:016x}',0,0))[:2],e['kind'])
            result[key]=e
        return result
    a,b=keys(left),keys(right);out=[]
    for key in sorted(a.keys()|b.keys()):
        if key not in a or key not in b:out.append({'key':key,'missing':'left' if key not in a else 'right'});continue
        x,y=semantic(a[key]),semantic(b[key]);fields={k:[x.get(k),y.get(k)] for k in x.keys()|y.keys() if x.get(k)!=y.get(k)}
        if fields:out.append({'key':key,'fields':fields})
    return out
