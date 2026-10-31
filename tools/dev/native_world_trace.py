"""Read-only native motion evidence: local pointers are never comparison keys."""
from collections import Counter, defaultdict
import io

HOOKS={'baseline','ship','nav','agent','steering','spline','loaded','path_before','path_after','ai','pending','order'}
BLOCKS={'banks':0x340,'params':0x4e0,'agent':0x5f0,'nav':21,'timer':9,'manager':48}
EVENT_KEYS=set('event tick id hook parent enter leave thread ui errors dt time cheap player'.split())
PRODUCER_KEYS=set('boot caller script pc_before pc_after'.split())
STATE_KEYS=set('event phase rng tls_time view read write read_end write_end model model_bytes stage'.split())
QUEUE_STATE_KEYS=set('main_count main_capacity pending_count pending_capacity unknown_refs'.split())
QUEUE_BLOCKS={'main_queue','pending_queue','main_refs','pending_refs'}
STOP_KEYS=set('schema watches first last capacity reserved complete rows failures dropped unmatched'.split())
EARLY_STOP_KEYS=set('early early_bound early_imports early_failures early_stage'.split())
HEX_KEYS={'id','dt','time','rng','tls_time','model','caller','pc_before','pc_after'}


def evidence(lines, errors, label='trace', expected=None):
    """Parse streaming lines, validate complete publication and nesting evidence."""
    if isinstance(lines,str):lines=io.StringIO(lines)
    events={};stop=None;rows=0
    def fail(message):errors.append(f'{label}: {message}')
    for line in lines:
        marker='NATIVE_GATE WORLD_TRACE_'
        if marker not in line:continue
        text=line.split(marker,1)[1].strip()
        try:
            kind,fields=text.split(' ',1)
            tokens=fields.split(' ');pairs=[token.split('=',1) for token in tokens]
            if any(len(pair)!=2 for pair in pairs):raise ValueError('invalid field')
            raw=dict(pairs)
            if len(raw)!=len(pairs):raise ValueError('duplicate field')
            if kind=='STOP':
                if set(raw) not in (STOP_KEYS,STOP_KEYS|EARLY_STOP_KEYS) or stop is not None:raise ValueError('duplicate/invalid stop')
                stop={key:(value if key=='early_stage' else int(value)) for key,value in raw.items()};continue
            if stop is not None:raise ValueError('row after stop')
            if kind not in {'EVENT','STATE','DATA'}:raise ValueError('unknown row')
            rows+=1
            index=int(raw['event'])
            if not 1<=index<=2048:raise ValueError('event outside cap')
            if kind=='EVENT':
                if set(raw) not in (EVENT_KEYS,EVENT_KEYS|PRODUCER_KEYS) or index in events or raw['hook'] not in HOOKS:raise ValueError('duplicate/invalid event')
                event={key:(value if key in {'hook','script'} else int(value,16 if key in HEX_KEYS else 10)) for key,value in raw.items()}
                event['states']={};events[index]=event
            else:
                if index not in events:raise ValueError('data before event')
                phase=raw['phase']
                if phase not in {'before','after'}:raise ValueError('invalid phase')
                if kind=='STATE':
                    if set(raw) not in (STATE_KEYS,STATE_KEYS|QUEUE_STATE_KEYS) or phase in events[index]['states']:raise ValueError('duplicate/invalid state')
                    state={key:int(value,16 if key in HEX_KEYS else 10) for key,value in raw.items() if key not in {'event','phase'}}
                    state['blocks']={};events[index]['states'][phase]=state
                else:
                    if set(raw)!={'event','phase','block','bytes'}:raise ValueError('invalid data')
                    block=raw['block'];state=events[index]['states'][phase]
                    allowed=set(BLOCKS)|{'model','slot'}|(QUEUE_BLOCKS if 'main_count' in state else set())
                    if block not in allowed or block in state['blocks']:raise ValueError('duplicate/unknown block')
                    data=b'' if raw['bytes']=='-' else bytes.fromhex(raw['bytes'])
                    if block=='model':size=state['model_bytes']
                    elif block=='slot':size=0x1a0 if events[index]['hook'] in {'steering','spline'} else 0
                    elif block in QUEUE_BLOCKS:
                        count=state[block.split('_')[0]+'_count']
                        if not 0<=count<=16:raise ValueError('queue exceeds bound')
                        size=count*(0x90 if block.endswith('_queue') else 12)
                    else:size=BLOCKS[block]
                    if len(data)!=size:raise ValueError('wrong block size')
                    state['blocks'][block]=data
        except (ValueError,KeyError,OverflowError) as error:fail(f'malformed {text[:80]} ({error})')
    if stop is None:fail('missing stop summary');stop={}
    count=len(events)
    if set(events)!=set(range(1,count+1)):fail('noncontiguous event IDs')
    early=stop.get('schema')==3 and stop.get('early')==1
    if (stop.get('schema') not in (1,2,3) or stop.get('capacity')!=2048 or stop.get('first')!=(0 if early else 2) or
        not 2<=stop.get('last',0)<=12 or stop.get('complete')!=count or stop.get('reserved')!=count or
        stop.get('rows')!=rows or rows!=count*(27 if stop.get('schema') in (2,3) else 19) or any(stop.get(key)!=0 for key in ('failures','dropped','unmatched'))):
        fail('incomplete/failed trace coverage')
    if stop.get('schema')==3 and (stop.get('early') not in (0,1) or stop.get('early_failures')!=0 or
        (early and (stop.get('early_bound')!=stop.get('watches') or stop.get('early_imports')!=stop.get('watches') or stop.get('early_stage')!='complete')) or
        (not early and (stop.get('early_bound')!=0 or stop.get('early_imports')!=0 or stop.get('early_stage')!='off'))):
        fail('missing/failed early lifecycle coverage')
    baseline=Counter();orders=[];coverage=defaultdict(Counter)
    for index,event in events.items():
        coverage[event['id']][event['hook']]+=1
        if event['hook']=='baseline':baseline[event['id']]+=1
        valid_tick=(event['tick']==1 if event['hook']=='baseline' else
                    early and event['tick']==0 if event['hook']=='loaded' else
                    (1 if early else 2)<=event['tick']<=stop.get('last',0))
        if (event['errors'] or not event['id'] or not valid_tick or event['ui'] not in (0,1) or
            event['cheap'] not in (0,1) or event['player'] not in (0,1) or not event['enter']<event['leave']):
            fail(f'event {index} failed or invalid context')
        if stop.get('schema')==3:
            if not PRODUCER_KEYS<=event.keys() or event.get('boot')!=3:fail(f'event {index} missing producer context')
            if event['hook']=='ai':
                if not early or event['ui']!=1 or event.get('caller')!=0x106e3fb or not event.get('script') or event.get('script')=='-':fail(f'event {index} invalid AI provenance')
            elif any(event.get(key) not in (0,'-') for key in ('caller','script','pc_before','pc_after')):fail(f'event {index} unexpected AI provenance')
            if event['hook'] in {'path_before','path_after'} and event['ui']!=1:fail(f'event {index} wrong pathing boundary thread')
        orders.extend((event['enter'],event['leave']))
        if event['parent']:
            parent=events.get(event['parent'])
            if not parent or not (parent['thread']==event['thread'] and parent['tick']==event['tick'] and
                parent['enter']<event['enter']<event['leave']<parent['leave']):fail(f'event {index} invalid parent')
        if set(event['states'])!={'before','after'}:fail(f'event {index} missing phases')
        for phase,state in event['states'].items():
            expected_blocks=set(BLOCKS)|{'model','slot'}|(QUEUE_BLOCKS if stop.get('schema') in (2,3) else set())
            if (state['stage']!=9 or set(state['blocks'])!=expected_blocks or state['read'] not in (0,1) or
                state['write']!=1-state['read'] or state['read_end']!=state['read'] or state['write_end']!=state['write'] or
                (state['model'],state['model_bytes']) not in {(0,0),(0x2c4e9c8,0x3a0),(0x2c4f480,0x4b0),(0x2c4f610,0x270)}):
                fail(f'event {index} {phase} incomplete/unsupported state')
            if stop.get('schema') in (2,3):
                if (state.get('unknown_refs')!=0 or any(not 0<=state.get(name+'_count',-1)<=16 or
                    not state.get(name+'_count',-1)<=state.get(name+'_capacity',-1)<=65536 for name in ('main','pending'))):
                    fail(f'event {index} {phase} invalid queue/reference coverage')
                for name in ('main','pending'):
                    data=state['blocks'].get(name+'_queue',b'');refs=state['blocks'].get(name+'_refs',b'')
                    for i in range(min(state.get(name+'_count',0),16)):
                        runtime=int.from_bytes(data[i*0x90+0x10:i*0x90+0x18],'little')
                        saved=int.from_bytes(refs[i*12+4:i*12+12],'little')
                        vtable=int.from_bytes(refs[i*12:i*12+4],'little')
                        if bool(runtime)!=bool(saved) or not 0<vtable<0x6fa9000:fail(f'event {index} {phase} invalid canonical queue reference')
    if sorted(orders)!=list(range(1,count*2+1)):fail('missing/duplicate causal order token')
    if not baseline or any(n!=1 for n in baseline.values()) or len(baseline)!=stop.get('watches') or set(coverage)!=set(baseline):
        fail('missing/duplicate watched baseline')
    if expected is not None and set(baseline)!=set(expected):fail('configured watch coverage differs')
    if early:
        phases=Counter((event['id'],event['hook'],event['tick']) for event in events.values())
        for identity in baseline:
            if phases[identity,'loaded',0]!=1:fail(f'missing loaded baseline {identity:016x}')
            for tick in range(1,max((event['tick'] for event in events.values()),default=0)+1):
                for phase in ('path_before','path_after'):
                    if phases[identity,phase,tick]!=1:fail(f'missing {phase} {identity:016x} tick {tick}')
    # A single object updated concurrently without nesting cannot be aligned by
    # thread ID or an arbitrary global order. Preserve it as unsupported evidence.
    grouped=defaultdict(list)
    for event in events.values():grouped[event['id'],event['tick']].append(event)
    for (identity,tick),group in grouped.items():
        group.sort(key=lambda event:event['enter'])
        for pos,event in enumerate(group):
            for other in group[pos+1:]:
                if other['enter']>=event['leave']:break
                if other['thread']!=event['thread']:fail(f'unsupported concurrent owner {identity:016x} tick {tick}')
    return events,{'stop':stop,'hooks':{f'{key:016x}':dict(value) for key,value in coverage.items()}}


def causal_keys(events):
    """Local parent relationships + saved identity; exclude raw thread IDs."""
    result={};ordinals=Counter()
    for event in sorted(events.values(),key=lambda item:item['enter']):
        parent=result.get(event['parent'],())
        stem=(event['tick'],event['id'],event['hook'],parent)
        ordinals[stem]+=1
        result[event['event']]=(event['tick'],f"{event['id']:016x}",event['hook'],ordinals[stem],parent)
    return result


def semantic_state(state):
    """Only pinned, interpreted scalar/vector fields. Raw padding stays separate."""
    result={key:state[key] for key in ('rng','tls_time','view','read','write','model')}
    def take(name,data,offset,size):result[name]=data[offset:offset+size].hex()
    blocks=state['blocks']
    for bank in range(2):
        data=blocks['banks'][bank*0x1a0:(bank+1)*0x1a0]
        for vector in range(6):take(f'bank{bank}.vector{vector}',data,vector*16,12)
        # Runtime UniverseIDs at +b0/+b8 are retained only in raw diagnostics.
        for name,offset,size in (('mode',0xcc,4),('eta',0x150,8),('arrived',0x159,1),('notified',0x15a,1),('layer',0x15b,1)):
            take(f'bank{bank}.{name}',data,offset,size)
    for bank in range(3):
        data=blocks['params'][bank*0x1a0:(bank+1)*0x1a0]
        take(f'params{bank}.limits',data,0,0x60);take(f'params{bank}.stamp',data,0x178,8)
    for name,offset,size in (('refresh_time',0,8),('refresh_cursor',8,4),('parameters_due',12,8),('attention',20,1)):
        take('nav.'+name,blocks['nav'],offset,size)
    take('agent.constructor_timer',blocks['timer'],0,8);take('agent.setup_flag',blocks['timer'],8,1)
    if state['model']==0x2c4e9c8:
        take('steering.constructor_angles',blocks['model'],0x33c,8)
        take('steering.avoidance_time',blocks['model'],0x88,8)
    elif state['model']==0x2c4f480:take('spline.avoidance_time',blocks['model'],0x108,8)
    elif state['model']==0x2c4f610:
        # Constructor and Update prove these initialized, typed fields. Offset
        # names deliberately avoid guessing the purpose of each scalar/flag.
        for offset in (0x20,0x30,0x40,0x50,0x70,0xb0,0xc0,0xd0,0x220,0x230,0x240,0x250):
            take(f'linear.vector{offset:x}',blocks['model'],offset,12)
        for offset in (0x80,0x88):take(f'linear.time{offset:x}',blocks['model'],offset,8)
        take('linear.flags90',blocks['model'],0x90,8)
        for offset in (0x98,0x9c,0xa0,0xa4,0xa8,0x260):take(f'linear.scalar{offset:x}',blocks['model'],offset,4)
    if 'main_count' in state:
        for name in ('main','pending'):
            result[name+'.count']=state[name+'_count']
            for index in range(state[name+'_count']):
                data=blocks[name+'_queue'][index*0x90:(index+1)*0x90];prefix=f'{name}.{index}'
                refs=blocks[name+'_refs'][index*12:(index+1)*12]
                take(prefix+'.type',refs,0,4);take(prefix+'.reference10',refs,4,8)
                result[prefix+'.resource18_present']=any(data[0x18:0x20])
                for vector in range(4):take(f'{prefix}.vector{vector}',data,0x20+vector*16,12)
                take(prefix+'.op64',data,0x64,4)
                for offset in (0x74,0x79,0x7b,0x7d):take(f'{prefix}.flag{offset:x}',data,offset,1)
                take(prefix+'.deadline80',data,0x80,8)
    return result


def differences(left,right):
    return {key:{'left':left.get(key),'right':right.get(key)} for key in left.keys()|right.keys() if left.get(key)!=right.get(key)}


def compare(left,right):
    keys=[causal_keys(events) for events in (left,right)]
    maps=[{mapping[index]:event for index,event in events.items()} for mapping,events in zip(keys,(left,right))]
    output=[]
    for key in sorted(maps[0].keys()|maps[1].keys()):
        a,b=maps[0].get(key),maps[1].get(key)
        if a is None or b is None:
            output.append({'key':key,'missing':'left' if a is None else 'right'});continue
        item={'key':key,'local_events':[a['event'],b['event']]}
        args=differences({k:a.get(k) for k in ('dt','time','cheap','player','ui','boot','caller','script','pc_before','pc_after')},
                         {k:b.get(k) for k in ('dt','time','cheap','player','ui','boot','caller','script','pc_before','pc_after')})
        if args:item['arguments']=args
        for phase in ('before','after'):
            sa,sb=a['states'][phase],b['states'][phase]
            delta=differences(semantic_state(sa),semantic_state(sb))
            if delta:item[phase]=delta
            raw={}
            for name in sa['blocks']:
                x,y=sa['blocks'][name],sb['blocks'][name]
                offsets=[i for i in range(max(len(x),len(y))) if x[i:i+1]!=y[i:i+1]]
                if offsets:raw[name]={'different_bytes':len(offsets),'first_offsets':offsets[:32]}
            if raw:item[phase+'_raw_local_differences']=raw
        if len(item)>2:output.append(item)
    return output
