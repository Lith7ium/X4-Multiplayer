"""Read-only transition receipts. Observed completion is not a pilot authority lease.

Raw runtime/pointer fields only join records within one process. A separate
portable view must use saved identity; raw queue/world failures are retained.
"""
import re
from native_transition_manifest import configured as configured_manifest

FIELDS=set('event kind tick enter leave parent thread boot caller errors arg extra queue enqueue queue_errors queue_valid transfer publication old new result old_owns active0 active1 s0 s1'.split())
HEX={'caller','old','new','result'}
STATE=('saved','runtime','object','controller','agent','slots','faction','model','controller_type','model_type','owns','player','owner')
STATUS=set('schema capacity count complete rows failures excluded dropped open peak restore_open sites enabled atomic_lease generation_exclusion faction_exclusion early_import_coverage raw_pointers'.split())
STATUS2=STATUS|set('watches bound watch_failures import_calls prebind last queue_calls queue_returns queue_other queue_unknown queue_failures tickets outstanding raw_world_coverage watch_rows ticket_rows'.split())
WATCH=set('schema index type macro owner generation raw_world_coverage s0'.split())
TICKET=set('schema ticket state saved runtime command agent enqueue dispatch mode before after image raw_world_coverage'.split())
EVENT64=set('tick enter leave parent queue enqueue transfer publication old new result'.split())
STATUS32=set('schema capacity complete rows sites enabled atomic_lease generation_exclusion faction_exclusion early_import_coverage raw_pointers watches bound last tickets outstanding raw_world_coverage watch_rows ticket_rows'.split())
TICKET64=set('saved runtime command agent enqueue dispatch image'.split())
BOOL=set('queue_valid old_owns active0 active1'.split())
EXCEPTION_STATUS=set('exception_schema direct_calls direct_returns direct_unwinds queue_exception_cleanup'.split())
STATUS32|={'exception_schema','queue_exception_cleanup'}


def number(value,base,bits):
    if not re.fullmatch(r'[0-9a-f]{1,'+str(bits//4)+'}' if base==16 else r'[0-9]{1,'+str(10 if bits==32 else 20)+'}',value):
        raise ValueError('invalid numeric syntax/width')
    result=int(value,base)
    if result>=1<<bits:raise ValueError('numeric bounds')
    return result


def state(text):
    values=text.split(',')
    if len(values)!=len(STATE):raise ValueError('state length')
    result={key:(number(value,16,64 if i<8 else 32) if i<10 else number(value,10,32) if i<12 else value) for i,(key,value) in enumerate(zip(STATE,values))}
    if any(not 0<=result[key]<2**64 for key in STATE[:10]) or any(result[key] not in (0,1) for key in ('owns','player')):
        raise ValueError('state bounds')
    if not re.fullmatch(r'[a-z0-9_]{1,63}|-',result['owner']):raise ValueError('owner string')
    return result


def same_ship(a,b):
    return bool(a['saved']) and all(a[k]==b[k] for k in ('saved','runtime','object','agent','slots','faction','owner'))


def observed_completion(e,events):
    """A causal mode0 completion diagnostic, deliberately not session_pass."""
    try:
        before,after=e['s0'],e['s1']
        if (e['kind']!=5 or e['errors'] or e['queue_valid']!=1 or e['queue_errors']&~(128|256) or e['arg']!=0 or after['model'] or after['model_type'] or
            after['player']!=1 or after['owns']!=1 or after['controller_type']!=0x2c39488 or
            not same_ship(before,after) or before['controller']!=after['controller']):return False
        requests=[q for q in events.values() if q['kind']==4 and q['queue']==e['enqueue']]
        if len(requests)!=1:return False
        q=requests[0];activation=events[q['parent']];transfer=events[e['transfer']];publication=events[e['publication']]
        if any(p['errors'] for p in (q,activation,transfer,publication)):return False
        if (q['arg'] or q['queue_valid']!=1 or q['queue_errors'] or q['enqueue']!=q['queue'] or q['transfer']!=e['transfer'] or q['publication']!=e['publication'] or
            not same_ship(q['s0'],before) or q['s0']['controller']!=before['controller'] or
            activation['kind']!=3 or activation['arg']!=1 or activation['extra']!=8 or activation['active1']!=1 or
            activation['thread']!=q['thread'] or not activation['enter']<q['enter']<q['leave']<activation['leave'] or
            transfer['kind']!=1 or transfer['arg']!=1 or transfer['extra']!=0 or transfer['old_owns']!=0 or
            transfer['s0']['owns']!=1 or transfer['s1']['owns']!=1 or transfer['s1']['player']!=1 or
            transfer['result']!=transfer['new'] or transfer['old']==transfer['new'] or
            transfer['s0']['controller']!=transfer['old'] or transfer['s1']['controller']!=transfer['new'] or
            publication['kind']!=2 or publication['new']!=before['controller'] or transfer['new']!=publication['new'] or
            publication['s0']['controller']!=publication['old'] or publication['s1']['owns']!=1 or publication['s1']['player']!=1 or
            transfer['old']!=publication['old'] or not same_ship(transfer['s0'],transfer['s1']) or
            not same_ship(transfer['s1'],publication['s0']) or not same_ship(publication['s0'],publication['s1']) or
            publication['s1']['controller']!=publication['new'] or not same_ship(publication['s1'],before) or
            not transfer['leave']<publication['enter'] or publication['leave']>=e['enter'] or q['leave']>=e['enter'] or
            not(publication['leave']<activation['enter'] or
                publication['enter']<activation['enter']<activation['leave']<publication['leave'])):return False
        return True
    except (KeyError,TypeError,ValueError):return False


def evidence(log):
    events={};watches={};tickets={};stop=None;errors=[];records=0
    for line in log.splitlines():
        if 'NATIVE_GATE PLAYER_TRANSITION ' in line:kind='event';raw=line.split('NATIVE_GATE PLAYER_TRANSITION ',1)[1]
        elif 'NATIVE_GATE PLAYER_TRANSITION_STOP ' in line:kind='stop';raw=line.split('NATIVE_GATE PLAYER_TRANSITION_STOP ',1)[1]
        elif 'NATIVE_GATE PLAYER_TRANSITION_WATCH ' in line:kind='watch';raw=line.split('NATIVE_GATE PLAYER_TRANSITION_WATCH ',1)[1]
        elif 'NATIVE_GATE PLAYER_TRANSITION_TICKET ' in line:kind='ticket';raw=line.split('NATIVE_GATE PLAYER_TRANSITION_TICKET ',1)[1]
        else:continue
        records+=1
        if records>512+2+256+1:
            if records==512+2+256+2:errors.append('transition record capacity exceeded')
            continue
        try:
            label={'event':'PLAYER_TRANSITION ','watch':'PLAYER_TRANSITION_WATCH ','ticket':'PLAYER_TRANSITION_TICKET ','stop':''}[kind]
            if len(label+raw)>={'event':2048,'watch':1024,'ticket':768,'stop':1536}[kind]:raise ValueError('transition row capacity exceeded')
            pairs=[p.split('=',1) for p in raw.split()];r=dict(pairs)
            if len(r)!=len(pairs):raise ValueError('duplicate key')
            if kind=='stop':
                expected=STATUS2 if r.get('schema')=='2' else STATUS
                if 'exception_schema' in r:expected=expected|EXCEPTION_STATUS
                if stop is not None or set(r)!=expected:raise ValueError('invalid stop')
                stop={k:number(v,10,32 if k in STATUS32 else 64) for k,v in r.items()};continue
            if kind in ('watch','ticket'):
                if stop is not None or set(r)!=(WATCH if kind=='watch' else TICKET):raise ValueError('invalid separate record')
                row={}
                for key,value in r.items():
                    if key=='s0':row[key]=state(value)
                    elif key in ('macro','owner'):
                        if not re.fullmatch(r'[a-z0-9_]{1,63}',value):raise ValueError('invalid watch text')
                        row[key]=value
                    elif key in ('before','after'):
                        if not re.fullmatch(r'[0-9a-f]{32}',value):raise ValueError('invalid ticket bytes')
                        row[key]=value
                    else:row[key]=number(value,16 if key in ('type','saved','runtime','command','agent','image') else 10,64 if kind=='ticket' and key in TICKET64 else 32)
                index=row['index' if kind=='watch' else 'ticket'];target=watches if kind=='watch' else tickets
                if row['schema']!=2 or not 1<=index<=(2 if kind=='watch' else 256) or index in target:raise ValueError('invalid separate record index')
                target[index]=row;continue
            if stop is not None or set(r)!=FIELDS:raise ValueError('invalid row')
            e={k:(state(v) if k in ('s0','s1') else number(v,16 if k in HEX else 10,64 if k in EVENT64 else 32)) for k,v in r.items()}
            if not 1<=e['event']<=512 or e['event'] in events or any(v<0 for k,v in e.items() if k not in ('s0','s1')):raise ValueError('event bounds')
            if any(e[k] not in (0,1) for k in BOOL):raise ValueError('event boolean bounds')
            events[e['event']]=e
        except (TypeError,ValueError):errors.append('malformed transition record')
    count=len(events)
    if (stop is None or stop.get('schema') not in (1,2) or stop.get('capacity')!=512 or
        any(stop.get(k)!=count for k in ('count','complete','rows')) or
        any(stop.get(k)!=0 for k in ('failures','dropped','open','restore_open','sites','enabled','atomic_lease','generation_exclusion','faction_exclusion','early_import_coverage')) or
        stop.get('raw_pointers')!=1 or stop.get('peak',-1)<(1 if count else 0) or stop.get('excluded',-1)<0 or set(events)!=set(range(1,count+1))):
        errors.append('incomplete/failed transition coverage')
    ordinals=[]
    if stop and 'exception_schema' in stop:
        direct_events=sum(e['kind'] in (1,2,3) for e in events.values())
        if (stop['exception_schema']!=1 or stop['queue_exception_cleanup'] or stop['direct_unwinds'] or
            stop['direct_calls']!=stop['direct_returns'] or not direct_events<=stop['direct_calls']<=direct_events+stop['excluded']):
            errors.append('direct transition exception/accounting failure')
    for e in events.values():
        ordinals.extend((e['enter'],e['leave']))
        if (e['kind'] not in range(1,6) or not e['enter']<e['leave'] or not 0<e['caller']<0x6fa9000 or
            e['errors'] or e['queue_errors'] or e['queue_valid'] not in (0,1) or
            (e['kind'] in (4,5) and e['queue_valid']!=1) or e['boot'] not in range(4) or not e['thread']):errors.append('failed transition or raw queue record')
        if e['parent']:
            parent=events.get(e['parent'])
            if not parent or parent['thread']!=e['thread'] or not parent['enter']<e['enter']<e['leave']<parent['leave']:
                errors.append('invalid nested transition parent')
    if sorted(ordinals)!=list(range(1,count*2+1)):errors.append('invalid transition causal order')
    valid_dispatches=None
    if stop and stop.get('schema')==2:valid_dispatches=separate_receipts(events,watches,tickets,stop,errors)
    elif watches or tickets:errors.append('separate receipts without schema2 footer')
    completions=[e['event'] for e in events.values() if observed_completion(e,events) and (valid_dispatches is None or e['event'] in valid_dispatches)]
    result={'evidence_valid':not errors,'errors':errors,'events':events,'stop':stop,'watches':watches,'tickets':tickets,
            'observed_model_completions':completions,'transition_exercised':bool(count),
            'exception_cleanup_scope':'direct_wrappers_only' if stop and 'exception_schema' in stop else 'ordinary_return_only',
            'queue_exception_cleanup':False,
            'atomic_lease':False,'independent_player_context':False}
    from native_transition_prebind import evidence as prebind_evidence
    early=prebind_evidence(log,result)
    if early is not None:
        result['prebind_receipt']=early;errors.extend('prebind: '+message for message in early['errors'])
        result['evidence_valid']=not errors
    return result


def separate_receipts(events,watches,tickets,stop,errors):
    valid=set();owners={};ticket_events=[]
    model_events=sum(e['kind'] in (4,5) for e in events.values())
    coverage_ok=not (not 1<=len(watches)<=2 or any(stop.get(k)!=len(watches) for k in ('watches','bound','watch_rows')) or
        any(stop.get(k)!=len(tickets) for k in ('tickets','ticket_rows')) or
        any(stop.get(k)!=0 for k in ('watch_failures','queue_failures','outstanding','raw_world_coverage')) or
        stop.get('queue_calls')!=stop.get('queue_returns') or stop.get('queue_calls',-1)<len(tickets)*2 or
        stop.get('queue_calls')!=model_events+sum(stop.get(k,0) for k in ('queue_other','queue_unknown','queue_failures')) or
        stop.get('import_calls',-1)<len(watches) or not 1<=stop.get('last',0)<=120 or
        any(stop.get(k,-1)<0 for k in ('prebind','queue_other','queue_unknown')) or
        set(watches)!=set(range(1,len(watches)+1)) or set(tickets)!=set(range(1,len(tickets)+1)))
    if not coverage_ok:
        errors.append('incomplete separate watch/queue coverage')
    ambiguous={a['s0']['saved'] for i,a in watches.items() for j,b in watches.items() if i!=j and
               any(a['s0'][k]==b['s0'][k] for k in ('saved','runtime','object','agent','controller'))}
    for watch in watches.values():
        s=watch['s0']
        if (watch['raw_world_coverage'] or watch['type'] not in (0x2c51158,0x2c39488) or
            s['controller_type']!=watch['type'] or s['player']!=(watch['type']==0x2c39488) or s['owner']!=watch['owner'] or s['owns']!=1 or
            not all(s[k] for k in ('saved','runtime','object','controller','agent','faction')) or s['slots']!=s['agent']+0xad0 or
            watch['generation']!=((s['runtime']>>25)&0xffff) or not s['runtime']&0x1ffffff or s['saved'] in owners or s['saved'] in ambiguous):
            errors.append('invalid initial transition watch');owners.pop(s['saved'],None)
        else:owners[s['saved']]=watch
    for e in events.values():
        if e['kind'] in (4,5) and (e['queue_errors'] or e['tick']>stop['last']):errors.append('invalid separate queue event coverage')
        for key in ('s0','s1'):
            s=e[key]
            if s['saved'] and (s['saved'] not in owners or not same_ship(s,owners[s['saved']]['s0'])):
                errors.append('transition left initial saved object/Agent/owner lifetime')
    for ticket in tickets.values():
        try:
            q=events[ticket['enqueue']];e=events[ticket['dispatch']];raw=bytes.fromhex(ticket['before'])
            good=(ticket['state']==3 and not ticket['raw_world_coverage'] and 0<=ticket['mode']<=6 and
                  ticket['before']==ticket['after'] and int.from_bytes(raw[:8],'little')==ticket['image']+0x2c51c38 and
                  ticket['image']>=0x10000 and ticket['image']%0x10000==0 and
                  int.from_bytes(raw[8:12],'little')==ticket['mode'] and ticket['command']>=0x10000 and
                  q['kind']==4 and e['kind']==5 and e['caller']==0xeacf66 and q['leave']<e['enter'] and
                  all(x['queue']==x['enqueue']==ticket['ticket'] and x['queue_valid']==1 and x['arg']==ticket['mode'] and
                      not x['errors'] and not x['queue_errors'] and x['s0']['saved']==ticket['saved'] and
                      x['s0']['runtime']==ticket['runtime'] and x['s0']['agent']==ticket['agent'] for x in (q,e)) and
                  same_ship(q['s0'],e['s0']) and same_ship(e['s0'],e['s1']) and
                  q['s0']['controller']==e['s0']['controller']==e['s1']['controller'] and
                  ticket['saved'] in owners and same_ship(q['s0'],owners[ticket['saved']]['s0']))
            if good:valid.add(e['event']);ticket_events.extend((q['event'],e['event']))
            else:errors.append('invalid separate model ticket/result')
        except (KeyError,TypeError,ValueError):errors.append('missing separate model ticket/result')
    expected=[e['event'] for e in events.values() if e['kind'] in (4,5)]
    if sorted(ticket_events)!=sorted(expected):errors.append('separate queue events/tickets incomplete or duplicated')
    return valid if coverage_ok else set()


def configured_evidence(log, config, slot, watches, errors):
    """Keep local pointer joins separate from replica equality and authority."""
    result=evidence(log)
    local_errors=result['errors']
    if (not re.search(r'native_gate_player_probe\s*=\s*"(?:observe|agreed-getup|diagnostic-getup)"',config) or
        any(not re.search(key+r'\s*=\s*true',config) for key in
            ('native_gate_world_trace_early','native_gate_world_queue_trace','native_gate_player_transition_observer'))):
        local_errors.append('transition configuration prerequisites missing')
    enabled=0;seen=0;stopped=False
    watch_seen=ticket_seen=0
    manifest=None
    try:manifest=configured_manifest(config)
    except ValueError as error:local_errors.append(str(error))
    schema=2 if manifest else 1
    for line in log.splitlines():
        if 'NATIVE_GATE PLAYER_TRANSITION_ENABLED ' in line:
            raw=line.split('NATIVE_GATE PLAYER_TRANSITION_ENABLED ',1)[1]
            pattern=r'schema='+str(schema)+r' resumed=(\d+) actions=0 singleton=1 early_import_coverage=0'
            if schema==2:pattern+=r' watches=(\d+) tickets=(\d+) raw_world_coverage=0'
            match=re.fullmatch(pattern,raw)
            if stopped or not match or int(match[1])!=seen:local_errors.append('transition configuration/export cursor mismatch')
            if match and schema==2 and (int(match[2])!=watch_seen or int(match[3])!=ticket_seen):local_errors.append('transition watch/ticket export cursor mismatch')
            enabled+=1
        elif 'NATIVE_GATE PLAYER_TRANSITION ' in line:
            if not enabled:local_errors.append('transition record precedes configuration')
            seen+=1
        elif 'NATIVE_GATE PLAYER_TRANSITION_STOP ' in line:stopped=True
        elif 'NATIVE_GATE PLAYER_TRANSITION_WATCH ' in line:
            if not enabled:local_errors.append('transition watch precedes configuration')
            watch_seen+=1
        elif 'NATIVE_GATE PLAYER_TRANSITION_TICKET ' in line:
            if not enabled:local_errors.append('transition ticket precedes configuration')
            ticket_seen+=1
        elif 'NATIVE_GATE PLAYER_TRANSITION_DRAIN_ERROR ' in line:local_errors.append('transition drain failed')
    if not enabled:local_errors.append('transition enable receipt missing')
    expected={int(row['saved'],16) for row in manifest[0]} if manifest else set(watches)
    if result['stop'] and result['stop']['schema']!=schema:local_errors.append('transition configured schema mismatch')
    if manifest:
        if not result['stop'] or result['stop'].get('last')!=manifest[1]:local_errors.append('transition configured window mismatch')
        actual={row['s0']['saved']:row for row in result['watches'].values()}
        if set(actual)!=expected:local_errors.append('transition initial watch set differs from manifest')
        for entry in manifest[0]:
            row=actual.get(int(entry['saved'],16))
            if not row or row['macro']!=entry['macro'] or row['owner']!=entry['owner'] or row['type']!=({'player':0x2c39488,'navigation':0x2c51158}[entry['controller']]):
                local_errors.append('transition initial watch differs from manifest')
    if not expected:local_errors.append('transition watched saved identities missing')
    for event in result['events'].values():
        for phase in ('s0','s1'):
            saved=event[phase]['saved']
            if saved and saved not in expected:local_errors.append('transition owner outside watched saved identities')
    from native_transition_queue_diag import evidence as queue_evidence
    diagnostic=queue_evidence(log,config,result)
    if diagnostic is not None:
        result['queue_evidence']=diagnostic
        local_errors.extend('copied queue: '+message for message in diagnostic['errors'])
    from native_transition_prebind import evidence as prebind_evidence
    early=prebind_evidence(log,result,config)
    if early is not None:
        result['prebind_receipt']=early
        for message in early['errors']:
            if 'prebind: '+message not in local_errors:local_errors.append('prebind: '+message)
    result['evidence_valid']=not local_errors
    result['slot']=slot
    result['comparison_scope']='local_causal_receipts_only'
    result['configuration']=({'schema':2,'entries':manifest[0],'last_tick':manifest[1]} if manifest else
                             {'schema':1,'saved':sorted(watches)})
    if diagnostic is not None:result['configuration']['queue_evidence']=1
    if early is not None:result['configuration']['prebind_receipt']=1
    result['action_origin_proven']=False
    errors.extend(f'player {slot} transition: {message}' for message in local_errors)
    return result
