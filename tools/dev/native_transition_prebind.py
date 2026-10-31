"""Prospective initialization receipt; no diagnostic-row promotion or lease."""
import hashlib
import re
from native_transition_queue_diag import configured as queue_configured

SUFFIX='|PLAYER_TRANSITION_PREBIND_1'
PREFIX='NATIVE_GATE PLAYER_TRANSITION_PREBIND'
ROW=set('schema ordinal phase thread boot tick clock event ticket parent caller command bytes registry page block index lane generation status s0'.split())
STOP=set('schema capacity count rows source returns pending finalized poison open restore_open closing run image atomic_lease allocation_generation_known queue_exception_cleanup action_tick action_clock action_thread action_ticket action_dispatch'.split())
HEX=set('caller command registry page block image'.split())
U64=set('tick clock event ticket parent command registry page block image open restore_open action_tick action_clock action_ticket action_dispatch'.split())

def configured(config):
    values=re.findall(r'\bnative_gate_player_transition_prebind_receipt\s*=\s*([^\r\n]*)',config)
    if not values:return False
    if values!=['true'] or not queue_configured(config):raise ValueError('exact local prebind receipt with queue evidence required')
    return True

def fields(text,expected):
    from native_transition_evidence import state
    pairs=[p.split('=') for p in text.split(' ')];row=dict(pairs)
    if len(pairs)!=len(row) or set(row)!=expected:raise ValueError('prebind field set')
    for key,value in row.items():
        if key in ('run','bytes'):
            if not re.fullmatch('[0-9a-f]{'+str(64 if key=='run' else 32)+'}',value):raise ValueError('prebind bytes')
        elif key=='s0':
            row[key]=s=state(value)
            canonical=','.join([f'{s[k]:016x}' for k in ('saved','runtime')]+[f'{s[k]:x}' for k in ('object','controller','agent','slots','faction','model','controller_type','model_type')]+[str(s[k]) for k in ('owns','player')]+[s['owner']])
            if value!=canonical:raise ValueError('prebind state encoding')
        else:
            base=16 if key in HEX else 10;bits=64 if key in U64 else 32
            if not re.fullmatch(r'0|[1-9a-f][0-9a-f]{0,15}' if base==16 else r'0|[1-9][0-9]{0,19}',value):raise ValueError('prebind canonical integer')
            row[key]=int(value,base)
            if row[key]>=1<<bits:raise ValueError('prebind integer width')
    return row

def evidence(log,core,config=None):
    rows={};stop=None;errors=[];enabled=0;run=None;records=0
    early=[e for e in core['events'].values() if e['kind']==4 and
           (e['boot']!=3 or (e['parent'] and core['events'].get(e['parent'],{}).get('boot')!=3))]
    selected=None
    if config is not None:
        try:
            selected=configured(config)
            tokens=re.findall(r'\btoken\s*=\s*"([0-9a-f]{32})"',config)
            if selected and len(tokens)!=1:raise ValueError('one prebind token required')
            if selected:run=hashlib.sha256(tokens[0].encode()).hexdigest()
        except ValueError as error:selected=True;errors.append(str(error))
    for line in log.splitlines():
        if PREFIX not in line:continue
        records+=1
        if records>256 or len(line)>=1664:
            if records<=257:errors.append('prebind record/line bound')
            continue
        try:
            tail=line.split(PREFIX,1)[1]
            if selected is False or stop is not None:raise ValueError('unconfigured/closed prebind stream')
            if tail.startswith('_ENABLED '):
                r=fields(tail[9:],set('schema resumed run atomic_lease'.split()))
                if r['schema']!=1 or r['resumed']!=len(rows) or r['atomic_lease'] or (run is not None and r['run']!=run):raise ValueError('prebind enable binding')
                run=r['run'];enabled+=1
            elif tail.startswith('_STOP '):
                if len(tail[6:])>=768:raise ValueError('prebind footer bound')
                stop=fields(tail[6:],STOP)
            elif tail.startswith(' '):
                if len('PLAYER_TRANSITION_PREBIND'+tail)>=1536 or not enabled:raise ValueError('prebind row before configuration')
                r=fields(tail[1:],ROW)
                if r['ordinal']!=len(rows)+1 or r['ordinal']>3:raise ValueError('prebind row ordinal')
                rows[r['ordinal']]=r
            else:raise ValueError('prebind record kind')
        except (KeyError,ValueError,TypeError):errors.append('malformed prebind record')
    if not early and not records and not selected:return None
    try:
        if not enabled or stop is None or set(rows)!={1,2,3}:raise ValueError('missing prebind phases/footer')
        if (any(stop[k]!=v for k,v in dict(schema=1,capacity=3,count=3,rows=3,source=1,returns=1,pending=0,
            finalized=1,poison=0,open=0,restore_open=0,closing=1,atomic_lease=0,allocation_generation_known=0,queue_exception_cleanup=0).items()) or
            stop['run']!=run or stop['image']<0x10000 or stop['image']%0x10000 or stop['image']+0x6fa9000>=1<<64):raise ValueError('prebind footer incomplete')
        a,b,c=(rows[i] for i in (1,2,3));q=core['events'][a['event']];parent=core['events'][a['parent']]
        ticket=core['tickets'][a['ticket']];dispatch=core['events'][ticket['dispatch']];watch=core['watches'][1]
        if len(early)!=1 or early[0]!=q:raise ValueError('one early producer required')
        if (q['boot']!=2 or q['tick'] or q['caller']!=0xd8dd26 or q['arg'] or q['errors'] or q['queue_valid']!=1 or
            q['parent']!=parent['event'] or parent['kind']!=3 or parent['boot']!=2 or parent['tick'] or parent['arg']!=1 or
            parent['extra']!=8 or parent['active0'] or parent['active1']!=1 or parent['errors'] or
            parent['thread']!=q['thread'] or not parent['enter']<q['enter']<q['leave']<parent['leave']):raise ValueError('prebind native producer/activation')
        if sum(e['kind']==3 and e['boot']==2 and not e['tick'] for e in core['events'].values())!=1:raise ValueError('duplicate early activation')
        if (a['schema']!=1 or a['phase']!=1 or a['boot']!=2 or a['tick'] or a['caller']!=0xd8dd26 or a['thread']!=q['thread'] or
            a['clock']!=q['enter'] or a['ticket']!=1 or a['ticket']!=q['queue'] or a['event']!=ticket['enqueue'] or
            a['command']!=ticket['command'] or a['bytes']!=ticket['before'] or ticket['mode'] or ticket['image']!=stop['image'] or
            a['s0']!=q['s0'] or a['s0']!=watch['s0'] or a['s0']['saved']!=0xae79c or a['s0']['owner']!='player' or
            a['s0']['controller_type']!=0x2c39488 or a['s0']['owns']!=1 or a['s0']['player']!=1):raise ValueError('prebind source binding')
        shared=ROW-{'ordinal','phase','boot','thread','clock','caller'}
        if (any(b[k]!=a[k] or c[k]!=a[k] for k in shared) or b['phase']!=2 or c['phase']!=3 or
            b['boot']!=2 or c['boot']!=3 or b['caller'] or c['caller'] or b['thread']!=a['thread'] or not c['thread'] or
            not q['leave']<=b['clock']<parent['leave']<=c['clock']<dispatch['enter'] or dispatch['boot']!=3 or
            dispatch['tick']<1 or dispatch['caller']!=0xeacf66):raise ValueError('prebind return/bind chronology')
        s=a['s0'];index=(s['runtime']&0x1ffffff)-1
        if (index<0 or a['index']!=index or a['lane']!=(index&0xfffff)%3 or a['generation']!=((s['runtime']>>25)&0xffff) or
            not 0<=a['status']<=15 or a['status']&5!=5 or a['registry']<0x10000 or a['page']<0x10000 or
            a['block']!=a['page']+((index&0xfffff)//3)*32):raise ValueError('prebind registry witness')
        if any(t['ticket']!=a['ticket'] and t['command']==a['command'] for t in core['tickets'].values()):raise ValueError('early command address reused')
        if stop['action_tick']:
            if (stop['action_tick']!=30 or not stop['action_thread'] or stop['action_ticket']!=a['ticket'] or
                stop['action_dispatch']!=dispatch['event'] or not dispatch['tick']<30 or
                not dispatch['leave']<=stop['action_clock']<=max(e['leave'] for e in core['events'].values())):
                raise ValueError('prebind action prerequisite chronology')
        elif any(stop[k] for k in ('action_clock','action_thread','action_ticket','action_dispatch')):raise ValueError('partial prebind action prerequisite')
    except (KeyError,TypeError,ValueError) as error:errors.append(str(error))
    return dict(evidence_valid=not errors,errors=errors,rows=rows,stop=stop,atomic_lease=False,
                allocation_generation_known=False,independent_player_context=False,completed_release=False)
