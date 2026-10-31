"""Independent bounded A/B intervention evidence, separate from build acceptance."""
import re
import hashlib
import native_registry_creation
import native_registry_md_evidence
from native_creation_rng import MASK, bootstrap_plan, parse_manifest, assignment, bootstrap_preimage

COMMON = 'schema scope part creation epoch handle tick thread phase1 enter leave error admitted original normal closed restored stop_overlap'.split()
PARTS = ['key scoped_after'.split(), 'tls address time caller restored_word allocation region region_size protection type child_before child_after'.split()]
FOOTER = 'schema configured closing count active attempted failures restore_failures restore_open epoch options seed_a seed_b'.split()
HEX = set('handle error key scoped_after tls address time caller restored_word allocation region protection type options seed_a seed_b'.split())
ACK = 'schema ordinal boot digest slot phase issued state records scopes attempted'.split()
ACK_STOP = 'schema acks boot digest slot'.split()
ACK2 = 'schema ordinal boot digest slot phase registry_phase reattach registry_acks issued state records scopes attempted'.split()
ACK2_STOP = 'schema acks registry_acks boot digest slot phase registry_phase failed'.split()


def fields(raw, expected):
    pairs = [word.split('=', 1) for word in raw.split(' ')]
    if any(len(pair) != 2 for pair in pairs) or [pair[0] for pair in pairs] != expected:
        raise ValueError('field names/order/duplicates')
    result = {}
    for name, value in pairs:
        if name in ('boot','digest'):
            if not re.fullmatch('[0-9a-f]{'+('32' if name=='boot' else '64')+'}',value):
                raise ValueError('acknowledgement identity grammar')
            result[name]=value
            continue
        if name in ('child_before','child_after'):
            if not re.fullmatch('[0-9a-f]{16}(?:,[0-9a-f]{16}){11}',value):
                raise ValueError('child writer vector grammar')
            result[name]=[int(v,16) for v in value.split(',')]
            continue
        pattern = '[0-9a-f]{1,16}' if name in HEX else '(?:0|[1-9][0-9]{0,19})'
        if not re.fullmatch(pattern, value):
            raise ValueError('field grammar '+name)
        result[name] = int(value, 16 if name in HEX else 10)
        if result[name] > (1 << 64)-1:
            raise ValueError('field width '+name)
    return result


def load_acknowledgements(log,config,slot,receipt):
    events=list(re.finditer(r'NATIVE_GATE (CREATION_RNG_CONFIG|CREATION_RNG_ACK) ([^\r\n]*)',log))
    if len(events)<2 or len(events)>16 or len(events)%2:raise ValueError('ACK2 pair cardinality')
    count=len(events)//2;registry=bool(native_registry_creation.plan(config,assignment))
    digest=hashlib.sha256(bootstrap_preimage(config).encode('ascii')).hexdigest()
    boot=None;prior_phase=prior_registry_phase=0;rows=[]
    for i in range(count):
        header,event=events[2*i:2*i+2]
        if header[1]!='CREATION_RNG_CONFIG' or header[2]!=receipt or event[1]!='CREATION_RNG_ACK':
            raise ValueError('ACK2 pair order/manifest/key')
        row=fields(event[2],ACK2)
        if boot is None:boot=row['boot']
        expected=dict(schema=2,ordinal=i+1,boot=boot,digest=digest,slot=slot,phase=row['phase'],registry_phase=row['registry_phase'],
            reattach=int(i>0),registry_acks=i+1 if registry else 0,issued=0,state=0,records=0,scopes=0,attempted=0)
        if row!=expected:raise ValueError('ACK2 boot/digest/ordinal/activity/pair')
        if (i==0 and (row['phase'] or row['registry_phase'])) or (i>0 and not 1<=row['phase']<=3) or row['phase']<prior_phase:
            raise ValueError('ACK2 phase lifecycle/regression')
        if row['registry_phase']>row['phase'] or row['registry_phase']<prior_registry_phase or \
           (registry and i>0 and (not 1<=row['registry_phase']<=3 or row['registry_phase']<prior_phase)) or \
           (not registry and row['registry_phase']):
            raise ValueError('ACK2 registry phase/pair')
        prior_phase,prior_registry_phase=row['phase'],row['registry_phase'];rows.append(row)
    loads=list(re.finditer(r'NATIVE_GATE LOAD_REQUEST snapshot=([^\r\n ]+)',log))
    if len(loads)!=1 or loads[0][1]!=parse_manifest(bootstrap_plan(config))['snapshot'] or loads[0].start()<events[1].end() or \
       (count>1 and loads[0].end()>events[2].start()):raise ValueError('ACK2 Load ordering/identity')
    # After the initial ACK, only ARMED can precede the actual Load request.
    # This also covers the no-reload case and the registry half of a reattach.
    if any(name!='ARMED' for name in re.findall(r'NATIVE_GATE ([A-Z0-9_]+)',log[events[1].end():loads[0].start()])):
        raise ValueError('ACK2 activity/reattachment before Load')
    allowed=('CREATION_RNG_CONFIG','CREATION_RNG_ACK','ARMED','LOAD_REQUEST')+ (('REGISTRY_CREATION_CONFIG',) if registry else ())
    if any(name not in allowed for name in re.findall(r'NATIVE_GATE ([A-Z0-9_]+)',log[:events[-1].end()])):
        raise ValueError('ACK2 attachment after observed activity')
    footers=list(re.finditer(r'NATIVE_GATE CREATION_RNG_ACK_STOP ([^\r\n]*)',log));stops=list(re.finditer(r'NATIVE_GATE CREATION_RNG_STOP ',log))
    if len(footers)!=1 or len(stops)!=1 or footers[0].start()<stops[0].start() or stops[0].start()<events[-1].end():
        raise ValueError('ACK2 footer order/cardinality')
    expected=dict(schema=2,acks=count,registry_acks=count if registry else 0,boot=boot,digest=digest,slot=slot,
        phase=prior_phase,registry_phase=prior_registry_phase,failed=0)
    if fields(footers[0][1],ACK2_STOP)!=expected:raise ValueError('ACK2 footer mismatch/partial pair')
    return {'schema':2,'count':count,'boot':boot,'digest':digest,'phases':[r['phase'] for r in rows],
        'registry_phases':[r['registry_phase'] for r in rows],'registry_acks':count if registry else 0}


def acknowledgements(log,config,slot,receipt):
    mode=assignment(config,'native_gate_creation_rng_ack_schema',False)
    if mode is None:
        if 'NATIVE_GATE CREATION_RNG_ACK' in log or re.findall(r'NATIVE_GATE CREATION_RNG_CONFIG ([^\r\n]*)',log)!=[receipt]:
            raise ValueError('missing/duplicate/different manifest and key receipt')
        return None
    if mode=='2':return load_acknowledgements(log,config,slot,receipt)
    if mode!='1':raise ValueError('acknowledgement mode')
    events=list(re.finditer(r'NATIVE_GATE (CREATION_RNG_CONFIG|CREATION_RNG_ACK) ([^\r\n]*)',log))
    if len(events)<2 or len(events)>16 or len(events)%2:
        raise ValueError('acknowledgement pair cardinality')
    expected_digest=hashlib.sha256(bootstrap_preimage(config).encode('ascii')).hexdigest()
    boot=None
    for i in range(len(events)//2):
        header,event=events[2*i:2*i+2]
        if header[1]!='CREATION_RNG_CONFIG' or header[2]!=receipt or event[1]!='CREATION_RNG_ACK':
            raise ValueError('acknowledgement pair order/manifest/key')
        row=fields(event[2],ACK)
        if boot is None:boot=row['boot']
        expected=dict(schema=1,ordinal=i+1,boot=boot,digest=expected_digest,slot=slot,
                      phase=0,issued=0,state=0,records=0,scopes=0,attempted=0)
        if row!=expected:raise ValueError('acknowledgement boot/digest/ordinal/activity')
    # Native zero-activity receipts are primary evidence. Printed ordering also
    # refuses a reload after activity already visible in this retained log.
    allowed=('CREATION_RNG_CONFIG','CREATION_RNG_ACK','ARMED','LOAD_REQUEST')
    if native_registry_creation.plan(config,assignment):allowed+=('REGISTRY_CREATION_CONFIG',)
    for name in re.findall(r'NATIVE_GATE ([A-Z0-9_]+)',log[:events[-1].end()]):
        if name not in allowed:
            raise ValueError('acknowledgement after observed activity')
    footers=list(re.finditer(r'NATIVE_GATE CREATION_RNG_ACK_STOP ([^\r\n]*)',log))
    stops=list(re.finditer(r'NATIVE_GATE CREATION_RNG_STOP ',log))
    if len(footers)!=1 or len(stops)!=1 or footers[0].start()<stops[0].start() or stops[0].start()<events[-1].end():
        raise ValueError('acknowledgement footer order/cardinality')
    if fields(footers[0][1],ACK_STOP)!=dict(schema=1,acks=len(events)//2,boot=boot,digest=expected_digest,slot=slot):
        raise ValueError('acknowledgement footer mismatch')
    return {'schema':1,'count':len(events)//2,'boot':boot,'digest':expected_digest}


def evidence(log, config, slot, errors):
    result = {'supported': False, 'valid': False, 'scopes': [], 'canonical': None,
              'whole_rng_supported': False, 'enrollment_supported': False}
    try:
        manifest = bootstrap_plan(config)
        if manifest is None:
            if 'NATIVE_GATE CREATION_RNG' in log:
                raise ValueError('unconfigured intervention evidence')
            return result
        result['supported'] = True
        parsed = parse_manifest(manifest)
        receipt = manifest+f';seed_a={parsed["key_a"]:016x};seed_b={parsed["key_b"]:016x}'
        result['acknowledgements']=acknowledgements(log,config,slot,receipt)
        raw_footer = re.findall(r'NATIVE_GATE CREATION_RNG_STOP ([^\r\n]*)', log)
        if len(raw_footer) != 1:
            raise ValueError('missing unique footer')
        status = fields(raw_footer[0], FOOTER)
        result['status'] = status
        if status != dict(schema=1, configured=1, closing=1, count=2, active=0, attempted=3,
                          failures=0, restore_failures=0, restore_open=0, epoch=parsed['epoch'], options=MASK,
                          seed_a=parsed['key_a'], seed_b=parsed['key_b']):
            raise ValueError('incomplete/failed/abnormal/STOP-overlapped scopes')
        rows = re.findall(r'NATIVE_GATE CREATION_RNG ([^\r\n]*)', log)
        if len(rows) != 4:
            raise ValueError('scope part cardinality')
        dependencies=[]
        provenance=native_registry_md_evidence.evidence(log,slot,dependencies,True,True,2)
        if dependencies:
            raise ValueError('mandatory MD/first-origin/identity join: '+'; '.join(dependencies))
        trace=provenance['creation']
        if not trace.get('supported') or not trace.get('a_valid') or not trace.get('b_valid'):
            raise ValueError('complete schema2 constructor/native-readiness evidence required')
        selected = {r['kind']: r for r in trace['records'] if r['kind'] in (1, 4)}
        for scope, kind in ((1, 1), (2, 4)):
            parts = [fields(rows[(scope-1)*2+part], COMMON+PARTS[part]) for part in range(2)]
            a, local = parts
            if any(a[name] != local[name] for name in COMMON if name != 'part') or \
               a['schema'] != 1 or a['scope'] != scope or a['part'] != 0 or local['part'] != 1:
                raise ValueError('scope parts changed or reordered')
            r = selected[kind]
            expected = {'creation': r['index'], 'epoch': parsed['epoch'], 'handle': parsed['handle'],
                        'tick': trace['status']['execution_tick'], 'thread': r['before']['thread'],
                        'phase1': r['phase1'], 'enter': r['enter'], 'leave': r['leave'],
                        'error': 0, 'admitted': 1, 'original': 1, 'normal': 1, 'closed': 1,
                        'restored': 1, 'stop_overlap': 0, 'key': parsed['key_a' if scope == 1 else 'key_b']}
            if any(a[name] != value for name, value in expected.items()) or r['parent'] or \
               r['before']['thread'] != r['after']['thread']:
                raise ValueError('scope/native original/command join')
            if local['tls'] != r['before']['tls'] or local['tls'] != r['after']['tls'] or \
               local['address'] != local['tls']+0x318 or local['address'] != r['before']['rng_pointer'] or \
               local['address'] != r['after']['rng_pointer'] or local['address'] & 7 or \
               local['time'] != r['before']['time'] or local['time'] != r['after']['time'] or \
               local['caller'] != r['before']['rng'] or local['restored_word'] != local['caller'] or \
               a['scoped_after'] != r['after']['rng']:
                raise ValueError('natural/scoped/restored TLS/RNG/clock join')
            if not local['allocation'] or not local['region'] or local['region_size'] < 4096 or \
               (local['allocation'] | local['region'] | local['region_size']) & 4095 or \
               not local['allocation'] <= local['region'] <= local['address'] <= local['region']+local['region_size']-8 or \
               local['protection'] not in (4, 8) or local['type'] not in (0x20000, 0x40000, 0x1000000):
                raise ValueError('writable owned TLS region evidence')
            if local['child_before']!=[0]*12 or local['child_after']!=[0]*12:
                raise ValueError('enabled actor writer failure or incomplete child')
            result['scopes'].append({'portable': a, 'local': local})
        draws = [{'caller': r['caller'], 'subject': r['aux']['subject'], 'bound': r['args'][1],
                  'before': r['aux']['rng_before'], 'after': r['aux']['rng_after'], 'result': r['result']}
                 for r in trace['records'] if r['kind'] == 7]
        # The fully pinned selected B path has the Agent timer draw, followed
        # by the observed query and its conditional Navigation timer draw.
        # Preserve an unexpected intervening draw as a coverage failure.
        agent=next(r for r in trace['records'] if r['kind']==5)
        query=next(r for r in trace['records'] if r['kind']==8)
        b_draws=sorted((r for r in trace['records'] if r['kind']==7 and r['aux']['stage']==2),key=lambda r:r['enter'])
        cursor=parsed['key_b']
        if agent['before']['rng']!=cursor:
            raise ValueError('B seeded entry/Agent boundary changed')
        for r in b_draws:
            if r['aux']['rng_before']!=cursor:
                raise ValueError('B observed draw chain differs from admitted key')
            cursor=r['aux']['rng_after']
        first=b_draws[0]['aux']['rng_after']
        if agent['after']['rng']!=first or query['before']['rng']!=first or query['after']['rng']!=first or \
           selected[4]['after']['rng']!=cursor:
            raise ValueError('B Agent/query/return RNG coverage gap')
        result['canonical'] = {'manifest': manifest, 'scopes': [
            {key: r['portable'][key] for key in ('scope', 'epoch', 'handle', 'tick', 'key', 'scoped_after')}
            for r in result['scopes']], 'draws': draws,
            'agent_timer': trace['agent_timer'], 'posted_due': trace['posted_due'],
            'initial_raw': [{'kind':r['kind'],'banks':r['after_banks'],
                             'fields':{k:r['after'][k] for k in ('read','write','c0','c8','d0')}}
                            for r in trace['records'] if r['kind'] in (4,5)]}
        result['raw_comparison_scope']='exact physical bytes and selectors; no coordinate/reference normalization'
        result['valid'] = True
    except (ValueError, KeyError, TypeError) as exc:
        errors.append(f'player {slot} construction RNG evidence: {exc}')
    return result
