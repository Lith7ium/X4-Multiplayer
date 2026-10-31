"""Provisional constructor evidence. Valid capture never means operational READY.

Local sequence orders only this observer. MD anchors reference the existing
shared ordinal; they do not exclude intervening native work or worker access.
"""
import re
from native_registry_creation import MANIFEST, plan
from native_creation_rng import assignment, bootstrap_plan, parse_manifest
from native_creation_rng_evidence import acknowledgements
from native_registry_origin_identity_evidence import check_first, STATES

COMMON='schema index part sequence md_anchor creation edge complete closed error'.split()
SUMMARY=('creation_enter creation_leave tick thread handle object runtime owner macro lookup lookup_first lookup_second '
         'map_before map_after controller agent slots admitted relationship open_scopes creation_error '
         'presence_recorded presence_stable pending_order pending_kind').split()
PRESENCE=('object type actual owner_getter owner controller controller_type controller_owner slots agent agent_owner '
          'reads object_state owner_state controller_state agent_state').split()
OPERATION=('schema index enter leave md_enter md_leave tick thread handle object runtime owner kind source caller verified '
           'context exec program begin end mode script_hash script creation classification paired captured closed normal '
           'unwound stop_overlap error identity_at_entry result').split()
FOOTER=('schema configured closing sequence identities operations failures dropped open peak stop_open identity_open '
        'identity_stop_open unpaired workers kinds handle runtime tick thread identity_admitted publication ready '
        'preparation_supported worker_exclusion_supported lifetime_lease_supported').split()
HEX=set(('handle object runtime owner macro lookup_first lookup_second map_before map_after controller agent slots '
         'error creation_error type actual owner_getter controller_type controller_owner agent_owner reads kinds '
         'caller verified context exec program begin end script_hash result').split())
FIXED=set(('handle object runtime owner macro lookup_first lookup_second map_before map_after controller agent slots '
           'actual controller_owner agent_owner context exec program script_hash result').split())
BOOL=set(('configured closing admitted relationship presence_recorded paired captured closed complete normal unwound '
          'stop_overlap identity_at_entry identity_admitted publication ready preparation_supported '
          'worker_exclusion_supported lifetime_lease_supported').split())
U32=set(('schema index part edge thread lookup open_scopes presence_stable pending_kind object_state owner_state '
         'controller_state agent_state kind source mode classification identities operations open peak stop_open '
         'identity_open identity_stop_open unpaired workers').split())


def fields(raw, expected):
    if len(raw)>1535:raise ValueError('registry creation row length')
    pairs=[v.split('=',1) for v in raw.split(' ')]
    if any(len(p)!=2 for p in pairs) or [p[0] for p in pairs]!=expected:
        raise ValueError('registry creation exact fields/order')
    row={}
    for key,value in pairs:
        if key=='script':
            if not re.fullmatch('[!-~]{1,95}',value) or '|' in value:raise ValueError('registry creation script')
            row[key]=value;continue
        pattern='[0-9a-f]{16}' if key in FIXED else '[0-9a-f]{1,16}' if key in HEX else '(?:0|[1-9][0-9]{0,19})'
        if not re.fullmatch(pattern,value):raise ValueError('registry creation field syntax '+key)
        row[key]=int(value,16 if key in HEX else 10)
        if row[key]>=2**(32 if key in U32 else 64) or key in BOOL and row[key]>1:
            raise ValueError('registry creation field width '+key)
    if row['schema']!=1:raise ValueError('registry creation schema')
    return row


def bounded(log,name,maximum):
    rows=[]
    for m in re.finditer('NATIVE_GATE '+name+r' ([^\r\n]*)',log):
        if len(rows)==maximum:raise ValueError('registry creation bounded capacity '+name)
        rows.append(m[1])
    return rows


def decode(log):
    """Check retained local rows including refusals; do not infer enrollment."""
    events=re.findall(r'NATIVE_GATE (REGISTRY_CREATION_[A-Z_]+) ',log)
    allowed={'REGISTRY_CREATION_CONFIG','REGISTRY_CREATION_IDENTITY','REGISTRY_CREATION_OPERATION','REGISTRY_CREATION_STOP'}
    if any(name not in allowed for name in events) or not events or events[-1]!='REGISTRY_CREATION_STOP':
        raise ValueError('registry creation unknown/drain-error/after-footer evidence')
    footers=bounded(log,'REGISTRY_CREATION_STOP',1)
    if len(footers)!=1:raise ValueError('registry creation unique footer')
    status=fields(footers[0],FOOTER)
    if len(footers[0])>1023 or status['configured']!=1 or status['closing']!=1 or \
       any(status[k] for k in ('publication','ready','preparation_supported','worker_exclusion_supported','lifetime_lease_supported')):
        raise ValueError('registry creation cannot publish or claim readiness')
    if not 0<=status['identities']<=4 or not 0<=status['operations']<=256 or status['kinds']>0x1ffff:
        raise ValueError('registry creation footer bounds')
    raw=bounded(log,'REGISTRY_CREATION_IDENTITY',12)
    if len(raw)!=3*status['identities']:raise ValueError('registry creation identity part cardinality')
    identities=[];sequence=set();intervals=[]
    for index in range(1,status['identities']+1):
        parts=[fields(raw[(index-1)*3+p],COMMON+(SUMMARY if p==0 else PRESENCE)) for p in range(3)]
        row=parts[0];samples=[{k:p[k] for k in PRESENCE} for p in parts[1:]]
        if row['index']!=index or row['part']!=0 or row['edge']!=index-1 or not row['sequence'] or row['sequence'] in sequence or \
           any(p['part']!=n or any(p[k]!=row[k] for k in COMMON if k!='part') for n,p in enumerate(parts)):
            raise ValueError('registry creation identity part join/order')
        sequence.add(row['sequence'])
        if identities and row['sequence']<=identities[-1]['sequence']:raise ValueError('registry creation identity admission order')
        if row['error']&~0x7ff or row['creation_error']>0xffffffff or row['lookup'] not in range(6) or \
           row['presence_stable'] not in range(4) or row['pending_kind']>17 or row['open_scopes']>256 or \
           bool(row['pending_kind'])!=bool(row['pending_order']) or not row['handle'] or not row['runtime'] or not row['object']:
            raise ValueError('registry creation identity state encoding')
        if row['complete'] and not row['closed'] or not row['complete'] and not row['error']:
            raise ValueError('registry creation incomplete identity without refusal')
        if (row['tick']!=status['tick'] or row['thread']!=status['thread'] or row['handle']!=status['handle']) and not row['error']&16:
            raise ValueError('registry creation identity command mismatch without scope refusal')
        if row['runtime']!=status['runtime'] and not row['error']&512:
            raise ValueError('registry creation identity runtime mismatch without lifetime refusal')
        if identities and any(row[k]!=identities[0][k] for k in ('object','runtime','owner','macro')) and not row['error']&512:
            raise ValueError('registry creation immutable identity changed without refusal')
        if row['presence_recorded']:
            for sample in samples:
                if any(sample[k]>5 for k in STATES):raise ValueError('registry creation presence state')
                check_first(dict(sample,runtime=row['runtime']))
            stable=3 if any(s[k]==2 for s in samples for k in STATES) else 1 if samples[0]==samples[1] else 2
            if row['presence_stable']!=stable:raise ValueError('registry creation presence reread')
            if any(row[k]!=samples[0][k] for k in ('controller','agent','slots')):
                raise ValueError('registry creation presence summary join')
            if (samples[0]['object']!=row['object'] or samples[0]['actual']!=row['runtime'] or
                samples[0]['owner']!=row['owner']) and not row['error']&(2|512):
                raise ValueError('registry creation identity/presence disagreement without refusal')
        elif row['presence_stable'] or any(v for s in samples for v in s.values()):
            raise ValueError('registry creation unrecorded presence populated')
        if row['admitted']:
            if row['edge']==3 or row['error'] or row['creation_error'] or not row['complete'] or row['lookup']!=2 or \
               not row['map_before'] or row['map_before']!=row['map_after'] or \
               row['lookup_first']!=row['object'] or row['lookup_second']!=row['object'] or \
               row['presence_recorded']!=1 or row['presence_stable']!=1 or not row['macro'] or not row['owner'] or \
               samples[0]['object']!=row['object'] or samples[0]['actual']!=row['runtime'] or \
               samples[0]['type']!=0x2b99a88 or samples[0]['owner']!=row['owner'] or samples[0]['owner_state']!=5 or \
               row['edge']==0 and (row['pending_order'] or row['open_scopes'] or row['sequence']!=1):
                raise ValueError('registry creation admitted identity lacks lookup/identity proof')
        if row['relationship'] and (row['edge']!=3 or row['presence_recorded']!=1 or row['presence_stable']!=1 or \
           samples[0]['controller_state']!=5 or samples[0]['agent_state']!=5 or not row['controller'] or not row['agent'] or row['slots']!=row['agent']+0xad0):
            raise ValueError('registry creation relationship candidate unsupported')
        if not status['failures'] and row['complete'] and row['admitted']!=int(row['edge']<3):
            raise ValueError('registry creation healthy identity admission contradicts producer')
        row['samples']=samples;identities.append(row)
    operations=[];raw=bounded(log,'REGISTRY_CREATION_OPERATION',256);kindmask=0;workers=0
    if len(raw)!=status['operations']:raise ValueError('registry creation operation cardinality')
    for index,line in enumerate(raw,1):
        r=fields(line,OPERATION)
        if r['index']!=index or not r['enter'] or r['enter'] in sequence or r['error']&~0x7ff or \
           not 1<=r['kind']<=17 or r['source']>4 or r['classification']>2 or not r['handle']:
            raise ValueError('registry creation operation identity/order')
        if operations and r['enter']<=operations[-1]['enter']:raise ValueError('registry creation operation admission order')
        sequence.add(r['enter']);kindmask|=1<<(r['kind']-1)
        workers+=r['thread']!=status['thread']
        if r['paired']:
            if r['kind']!=6 or not 1<=r['source']<=4 or not r['context']:
                raise ValueError('registry creation paired scope family')
            if r['closed']:
                if not r['enter']<r['leave'] or r['leave'] in sequence or r['normal']+r['unwound']!=1 or \
                   not r['normal'] and r['result'] or r['unwound'] and not r['error']&128:
                    raise ValueError('registry creation paired exit')
                sequence.add(r['leave'])
            elif r['leave'] or r['normal'] or r['unwound'] or r['result'] or not r['error']:
                raise ValueError('registry creation unclosed operation claims result')
            intervals.append((r['enter'],r['leave']))
        elif any(r[k] for k in ('source','leave','md_leave','closed','normal','unwound','result','classification')) or not r['error']&256:
            raise ValueError('registry creation invented unpaired exit')
        if not r['captured'] and not r['error']:raise ValueError('registry creation missing sample without refusal')
        if r['stop_overlap'] and not r['error']&64:raise ValueError('registry creation STOP overlap erased')
        if r['thread']!=status['thread'] and not r['error']&8:raise ValueError('registry creation worker unreported')
        if r['tick']!=status['tick'] and not r['error']&16:raise ValueError('registry creation operation tick refusal erased')
        if (r['runtime']!=status['runtime'] or r['handle']!=status['handle']) and not r['error']&512:
            raise ValueError('registry creation lifetime refusal erased')
        if r['classification']==1 and (r['source']!=2 or r['caller']!=0x1066dc6 or r['verified']!=r['caller'] or \
           r['begin']!=0 or r['end']!=4 or r['mode']!=1 or r['script']!='orders.base'):
            raise ValueError('registry creation startup classification')
        if r['classification']==2 and (r['source']!=1 or r['caller']!=0x106e3fb or r['verified']!=r['caller']):
            raise ValueError('registry creation AI classification')
        if r['paired'] and r['captured'] and not r['classification'] and not r['error']&256:
            raise ValueError('registry creation unsupported scope hidden')
        if r['script']!='none':
            hashed=14695981039346656037
            for b in r['script'].encode('ascii'):hashed=((hashed^b)*1099511628211)&(2**64-1)
            if hashed!=r['script_hash']:raise ValueError('registry creation script preimage')
        elif r['script_hash']:raise ValueError('registry creation missing script')
        operations.append(r)
    if kindmask!=status['kinds'] or workers!=status['workers'] or sum(not r['paired'] for r in operations)!=status['unpaired'] or \
       sum(r['paired'] and not r['closed'] for r in operations)!=status['open'] or \
       sum(not r['closed'] for r in identities)!=status['identity_open']:
        raise ValueError('registry creation footer coverage')
    if status['sequence']!=len(sequence)+status['dropped'] or not status['dropped'] and \
       any(value!=index for index,value in enumerate(sorted(sequence),1)) or sequence and max(sequence)>status['sequence']:
        raise ValueError('registry creation separate sequence gap')
    anchors=[(r['sequence'],r['md_anchor']) for r in identities]
    anchors += [(r['enter'],r['md_enter']) for r in operations]
    anchors += [(r['leave'],r['md_leave']) for r in operations if r['closed']]
    ordered=[anchor for _,anchor in sorted(anchors)]
    if ordered!=sorted(ordered):raise ValueError('registry creation local order reverses atomic MD anchors')
    peak=max((sum(a<=n and (not b or n<b) for a,b in intervals) for n in sequence),default=0)
    if peak!=status['peak'] or status['stop_open']>peak or status['identity_stop_open']>1:
        raise ValueError('registry creation direct scope count')
    for r in identities:
        if r['open_scopes']!=sum(a<r['sequence'] and (not b or r['sequence']<b) for a,b in intervals):
            raise ValueError('registry creation candidate open scopes')
    if (any(r['error'] for r in identities+operations) or status['dropped']) and not status['failures'] or \
       status['identity_admitted'] and (status['failures'] or not any(r['admitted'] for r in identities)):
        raise ValueError('registry creation sticky refusal missing')
    return dict(status=status,identities=identities,operations=operations,
                capture_complete=status['identities']==4 and not(status['dropped'] or status['open'] or status['identity_open']) and
                    all(r['complete'] for r in identities) and all(r['captured'] for r in operations),
                identity_admitted=bool(status['identity_admitted']),ready=False,publication=False,
                lifetime_lease_supported=False,worker_exclusion_supported=False,preparation_supported=False)


def join(result,provenance):
    trace=provenance.get('creation',{});status=trace.get('status',{})
    if not trace.get('a_valid') or not trace.get('b_valid'):raise ValueError('registry creation needs complete native constructor/MD provenance')
    records={r['index']:r for r in trace['records']};s=result['status']
    if (s['handle'],s['runtime'],s['thread'],s['tick'])!=(status['handle'],status['runtime'],status['thread'],status['execution_tick']):
        raise ValueError('registry creation command/Builder lifetime join')
    for row in result['identities']:
        r=records.get(row['creation']);edge=row['edge']
        if not r or r['kind']!=(3 if edge<2 else 1 if edge==2 else 6) or \
           row['creation_enter']!=r['enter'] or row['creation_leave']!=(0 if edge==0 else r['leave']) or \
           row['creation_error']!=r['error'] or row['handle']!=r['handle'] or \
           row['tick']!=status['execution_tick'] or row['thread']!=status['thread']:
            raise ValueError('registry creation native record join')
        anchor=r['enter'] if edge==0 else r['leave']
        if not anchor<=row['md_anchor']<=status['callback_order']:raise ValueError('registry creation immutable MD anchor')
        if edge<2:
            parent=records.get(r['parent']);grand=records.get(parent['parent']) if parent else None
            if r['caller']!=0xc8224f or not parent or parent['kind']!=2 or not grand or grand['kind']!=1:
                raise ValueError('registry creation Pilot ancestry')
        if edge==3 and (r['aux']['stage']!=2 or r['aux']['subject']!=1 or r['caller']!=0x6cd8be or \
           row['controller']!=status['controller'] or row['agent']!=status['agent'] or r['leave']>=status['callback_order']):
            raise ValueError('registry creation selected setter candidate join')
        if row['admitted'] or row['relationship']:
            native=r['before_identity'] if edge==0 else r['after_identity']
            if any(row['samples'][0][k]!=native[k] for k in PRESENCE):raise ValueError('registry creation native presence join')
    for row in result['operations']:
        if (row['object']!=status['object'] or row['owner']!=status['true_owner'] or row['runtime']!=status['runtime']) and not row['error']&512:
            raise ValueError('registry creation operation selected identity mismatch')
        if row['md_enter']<status['phase1'] or row['md_enter']>status['callback_order'] or \
           row['closed'] and (row['md_leave']<row['md_enter'] or row['md_leave']>status['callback_order']):
            raise ValueError('registry creation operation anchor outside selected trace')
        if row['source']==2 and row['paired']:
            r=records.get(row['creation'])
            if not r or r['kind']!=10 or row['md_enter']<r['enter'] or row['closed'] and row['md_leave']<r['leave'] or \
               row['caller']!=r['caller'] or row['context']!=r['args'][1] or row['exec']!=r['args'][0] or \
               [row[k] for k in ('program','begin','end','mode')]!=r['args'][2:6] or row['normal'] and row['result']!=r['result']:
                raise ValueError('registry creation exact range scope join')
    result['provenance_joined']=True


def evidence(log,config,slot,errors,provenance=None):
    result={'supported':False,'valid':False,'ready':False,'publication':False}
    try:
        marker=plan(config,assignment)
        if marker is None:
            if 'NATIVE_GATE REGISTRY_CREATION_' in log:raise ValueError('unconfigured registry creation evidence')
            return result
        result['supported']=True
        manifest=bootstrap_plan(config);parsed=parse_manifest(manifest)
        receipt=manifest+f';seed_a={parsed["key_a"]:016x};seed_b={parsed["key_b"]:016x}'
        ack=acknowledgements(log,config,slot,receipt)
        headers=bounded(log,'REGISTRY_CREATION_CONFIG',8)
        if headers!=[MANIFEST]*ack['count']:raise ValueError('registry creation attachment cardinality/manifest')
        names=re.findall(r'NATIVE_GATE (REGISTRY_CREATION_CONFIG|CREATION_RNG_CONFIG|CREATION_RNG_ACK) ',log)
        if names!=['REGISTRY_CREATION_CONFIG','CREATION_RNG_CONFIG','CREATION_RNG_ACK']*ack['count']:
            raise ValueError('registry creation attachment order')
        result.update(decode(log))
        if provenance is None:
            import native_registry_md_evidence
            deps=[];provenance=native_registry_md_evidence.evidence(log,slot,deps,True,True,2)
            if deps:raise ValueError('registry creation missing provenance: '+'; '.join(deps))
        join(result,provenance);result['valid']=True
    except (ValueError,KeyError,IndexError,TypeError) as exc:
        errors.append(f'player {slot} provisional registry evidence invalid: {exc}')
    return result
