"""Validate local constructor observations; never accept enrollment or a build.

Pointers and physical bank order stay local. The shared ordinal join describes
observed native call boundaries, not scheduler exclusion or all engine writes.
"""
import math
import re
import struct
from collections import Counter
from native_registry_origin_evidence import IRQ_CALLERS
from native_registry_origin_identity_evidence import check_first

COMMON='schema index part complete'.split()
META='kind enter leave handle phase1 parent depth caller argc result result_kind error foreign'.split()
SNAP=('thread ui main tls view time rng_pointer rng read write reads stable object runtime controller controller_type '
      'controller_owner slots controller_agent agent agent_owner timer c0 c8 d0 direct_reads identity_recorded identity_stable').split()
IDENTITY=('object type actual object_state owner_getter owner owner_state controller controller_type controller_owner '
          'controller_state slots agent agent_owner agent_state reads raw_type raw_owner_getter raw_controller_type').split()
RANGE='verified source pc context_pc entity depth reads script_hash script'.split()
FOOTER=('schema started enabled closing count parts failures first_error dropped open peak restore_open restore_failures owned handle '
        'state phase1 phase2 builder_order builder_runtime thread object runtime controller agent generate initialize pilot navigation '
        'agent_calls setter sector_pose_supported enrollment_supported').split()
HEX=set(('handle caller result error tls time rng_pointer rng object runtime controller controller_type controller_owner slots '
         'controller_agent agent agent_owner timer c0 c8 d0 reads direct_reads type actual owner_getter owner raw_type raw_owner_getter '
         'raw_controller_type verified pc context_pc entity script_hash first_error owned builder_runtime').split())
CALLERS={1:0xba1b8b,2:0xc84531,3:0xc8224f,4:0xea4b5e,5:0xeac5ff,6:0x6cd8be,8:0xea906e,9:0xea910a}
ARGC={1:10,2:9,3:7,4:6,5:4,6:3,7:2,8:5,9:4,10:6}
RESULT={1:1,2:0,3:1,4:0,5:1,6:0,7:2,8:3,9:0,10:1}
NARROW={1:{4:8,7:32,8:8,9:8},2:{5:8,7:32,8:8},4:{4:8,5:8},6:{2:8},7:{1:32},8:{1:32,4:8},9:{3:8},10:{5:8}}
FIXED={
    'meta':set('handle result'.split()),
    'snap':set('tls time rng_pointer rng object runtime controller controller_type controller_owner slots controller_agent agent agent_owner timer c0 d0'.split()),
    'identity':set('object actual owner controller controller_owner slots agent agent_owner raw_type raw_owner_getter raw_controller_type'.split()),
    'range':{'entity','script_hash'},
    'footer':set('handle builder_runtime object runtime controller agent'.split()),
}


def parse(line, fields):
    if len(line)>1535:raise ValueError('creation row exceeds bound')
    pairs=[piece.split('=',1) for piece in line.split(' ')]
    if any(len(p)!=2 for p in pairs) or [p[0] for p in pairs]!=fields:
        raise ValueError('creation exact row schema')
    result={}
    payload=fields[len(COMMON):] if fields[:len(COMMON)]==COMMON else fields
    family='meta' if payload==META else 'snap' if payload==SNAP else 'identity' if payload==IDENTITY else 'range' if payload==RANGE else 'footer' if payload==FOOTER else ''
    for key,value in pairs:
        if key in ('raw','args','script'):
            result[key]=value;continue
        if not re.fullmatch('[0-9a-f]{1,16}' if key in HEX else '[0-9]{1,20}',value):
            raise ValueError('creation number syntax')
        if key in FIXED.get(family,set()) and len(value)!=16 or family=='snap' and key=='c8' and len(value)!=8:
            raise ValueError('creation exact native integer width')
        result[key]=int(value,16 if key in HEX else 10)
        if result[key]>=2**64:raise ValueError('creation number overflow')
    return result


def floating(bits, width=8):
    if not 0<=bits<2**(width*8):raise ValueError('creation float width')
    value=struct.unpack('<d' if width==8 else '<f',bits.to_bytes(width,'little'))[0]
    if not math.isfinite(value):raise ValueError('creation nonfinite clock or result')
    return value


def evidence(log, required=False):
    # Schema-1 frozen diagnostics retain their original validator and errors.
    # A fresh schema marker must match the actual native producer; no upgrade
    # by reinterpreting earlier records under the newer ownership rules.
    if required==2:
        import native_creation_evidence_v2
        return native_creation_evidence_v2.evidence(log,required=True)
    if required not in (False,True,1):raise ValueError('creation unsupported configured schema')
    return _evidence_v1(log,required)


def _evidence_v1(log, required=False):
    rows=[]
    for match in re.finditer(r'NATIVE_GATE CREATION_TRACE ([^\r\n]*)',log):
        if len(rows)>=128*17:raise ValueError('creation global record bound')
        rows.append(match[1])
    footers=re.findall(r'NATIVE_GATE CREATION_TRACE_STOP ([^\r\n]*)',log)
    absent={'supported':False,'records':[],'orders':[], 'sector_pose_supported':False,'enrollment_supported':False}
    if not rows and not footers:
        if required:raise ValueError('required creation trace missing')
        return absent
    if not required:raise ValueError('creation trace without explicit schema marker')
    if len(footers)!=1:raise ValueError('creation unique footer missing')
    status=parse(footers[0],FOOTER)
    expected={'schema':1,'started':1,'enabled':0,'closing':1,'parts':17,'failures':0,'first_error':0,'dropped':0,
              'open':0,'restore_open':0,'restore_failures':0,'owned':0,'handle':0x500000003,'state':4,
              'generate':1,'initialize':1,'pilot':1,'navigation':1,'agent_calls':1,'setter':1,
              'sector_pose_supported':0,'enrollment_supported':0}
    if any(status[k]!=v for k,v in expected.items()) or not 1<=status['count']<=128 or not 1<=status['peak']<=8 or \
       not 0<status['phase1']<status['phase2']<status['builder_order'] or not 0<status['thread']<2**32 or \
       not status['object'] or not status['controller'] or not status['agent'] or not status['runtime'] or status['runtime']!=status['builder_runtime']:
        raise ValueError('creation footer incomplete, failed or unsupported')
    if len(rows)!=status['count']*17:raise ValueError('creation part coverage')
    records=[];orders=set();max_depth=0
    for index in range(1,status['count']+1):
        parts=[]
        for part in range(17):
            fields=META if part==0 else ['args'] if part==1 else SNAP if part in (2,3) else IDENTITY if part<8 else ['raw'] if part<16 else RANGE
            row=parse(rows[(index-1)*17+part],COMMON+fields)
            if (row['schema'],row['index'],row['part'],row['complete'])!=(1,index,part,1):
                raise ValueError('creation duplicate, missing, reordered or incomplete part')
            parts.append(row)
        meta=parts[0];kind=meta['kind'];args=parts[1]['args']
        if kind not in ARGC or meta['argc']!=ARGC[kind] or meta['result_kind']!=RESULT[kind] or meta['error'] or meta['foreign'] or \
           meta['handle']!=status['handle'] or meta['phase1']!=status['phase1'] or not 1<=meta['depth']<=8 or not 0<=meta['parent']<index or \
           not status['phase1']<meta['enter']<meta['leave']<status['phase2'] or meta['enter'] in orders or meta['leave'] in orders:
            raise ValueError('creation call identity, scope, ordering or failure')
        if kind in CALLERS and meta['caller']!=CALLERS[kind]:raise ValueError('creation native caller')
        if kind==7 and meta['caller'] not in (0xe85531,0xea90e1):raise ValueError('creation RNG caller')
        if kind==10 and meta['caller'] not in IRQ_CALLERS:raise ValueError('creation range caller')
        if not re.fullmatch('[0-9a-f]{16}(?:,[0-9a-f]{16}){9}',args):raise ValueError('creation argument encoding')
        args=[int(v,16) for v in args.split(',')]
        if any(args[ARGC[kind]:]) or (not RESULT[kind] and meta['result']):raise ValueError('creation unused arguments or void result')
        if any(args[position]>=2**width for position,width in NARROW.get(kind,{}).items()):
            raise ValueError('creation native narrow argument width')
        if records and records[-1]['enter']>=meta['enter']:raise ValueError('creation admission order')
        if meta['parent']:
            parent=records[meta['parent']-1]
            if not parent['enter']<meta['enter']<meta['leave']<parent['leave'] or meta['depth']!=parent['depth']+1:
                raise ValueError('creation parent interval')
        elif meta['depth']!=1 or kind not in (1,4,6):raise ValueError('creation root scope')
        orders.update((meta['enter'],meta['leave']));max_depth=max(max_depth,meta['depth'])
        for edge,snap in enumerate(parts[2:4]):
            if snap['thread']!=status['thread'] or snap['ui']!=1 or snap['main'] not in (0,1) or snap['view'] not in (0,1) or \
               snap['stable']!=1 or snap['reads']!=31 or snap['read'] not in (0,1) or snap['write']!=1-snap['read'] or \
               not snap['tls'] or snap['rng_pointer']!=snap['tls']+0x318:
                raise ValueError('creation unstable clock/TLS/selector sample')
            floating(snap['time'])
            if snap['object'] not in (0,status['object']) or snap['runtime']!=(status['runtime'] if snap['object'] else 0) or \
               snap['controller'] not in (0,status['controller']) or snap['agent'] not in (0,status['agent']):
                raise ValueError('creation candidate lifetime changed')
            samples=parts[4+edge*2:6+edge*2]
            normalized=[{k:v for k,v in s.items() if k not in COMMON} for s in samples]
            if normalized[0]!=normalized[1]:raise ValueError('creation identity reread changed')
            identity=samples[0]
            if snap['object']:
                if snap['identity_recorded']!=1 or snap['identity_stable']!=1 or identity['object']!=status['object'] or \
                   identity['actual']!=status['runtime'] or identity['type']!=0x2b99a88 or identity['object_state']!=5:
                    raise ValueError('creation identity presence missing')
                for native_identity in samples:check_first(dict(native_identity,runtime=status['runtime']))
            elif snap['identity_recorded'] or snap['identity_stable'] or any(normalized[0].values()):
                raise ValueError('creation pre-object identity invented')
            if snap['direct_reads']&~2047:raise ValueError('creation unknown raw field coverage')
            if snap['controller'] and not snap['direct_reads']&1:raise ValueError('creation controller read missing')
            if snap['agent'] and (snap['direct_reads']&1920!=1920 or snap['agent_owner']!=status['object']):
                raise ValueError('creation Agent fields missing')
            if snap['agent']:floating(snap['timer'])
            for bank,raw in enumerate(parts[8+edge*4:12+edge*4]):
                if not re.fullmatch('[0-9a-f]{832}',raw['raw']):raise ValueError('creation physical-bank encoding')
                if not snap['direct_reads']&(512 if bank<2 else 1024) and int(raw['raw'],16):
                    raise ValueError('creation raw bank without read coverage')
        ranged=parts[16]
        if kind==10:
            if ranged['verified']!=meta['caller'] or ranged['source']!=1 or ranged['reads']!=15 or not 0<=ranged['depth']<16 or \
               not ranged['entity'] or (not ranged['depth'] and ranged['entity']!=status['runtime']) or not re.fullmatch('[!-~]{1,95}',ranged['script']) or '|' in ranged['script'] or \
               ranged['script'] in ('none','unsupported') or not args[0] or not args[1] or not args[2] or args[5]>255:
                raise ValueError('creation range provenance')
            hashed=14695981039346656037
            for byte in ranged['script'].encode('ascii'):hashed=((hashed^byte)*1099511628211)&(2**64-1)
            if ranged['script_hash']!=hashed:raise ValueError('creation range script preimage')
        elif ranged['script']!='none' or any(ranged[k] for k in RANGE if k!='script'):
            raise ValueError('creation range fields attached to other call')
        records.append(dict(meta,args=args,before=parts[2],after=parts[3],range=ranged,
                            before_identity=parts[4],after_identity=parts[6],
                            before_banks=[p['raw'] for p in parts[8:12]],after_banks=[p['raw'] for p in parts[12:16]]))
    if max_depth!=status['peak']:raise ValueError('creation peak scope coverage')
    stack=[]
    boundaries=sorted([(r['enter'],True,r) for r in records]+[(r['leave'],False,r) for r in records],key=lambda b:b[0])
    for _,enter,record in boundaries:
        if enter:
            if record['parent']!=(stack[-1] if stack else 0):raise ValueError('creation cross-pair nesting')
            stack.append(record['index'])
        elif not stack or stack.pop()!=record['index']:raise ValueError('creation crossing original calls')
    if stack:raise ValueError('creation unmatched originals')
    counts=Counter(r['kind'] for r in records)
    if any(counts[k]!=1 for k in (1,2,3,4,5,6,8)) or not counts[10]:raise ValueError('creation constructor/query/range coverage')
    one={k:next(r for r in records if r['kind']==k) for k in (1,2,3,4,5,6,8)}
    if one[1]['args'][3] or one[2]['args'][2] or not one[1]['args'][1] or one[1]['result']!=one[1]['args'][1] or one[2]['args'][1]!=one[1]['args'][1] or \
       not one[3]['args'][1] or one[3]['result']!=one[3]['args'][1] or \
       one[3]['args'][2]!=one[1]['args'][1] or one[4]['args'][:3]!=[status['controller'],status['object'],0] or \
       one[5]['args'][:2]!=[status['agent'],status['object']] or one[5]['result']!=status['agent'] or \
       one[6]['args'][:2]!=[status['object'],status['controller']] or one[8]['args'][0]!=status['object'] or one[8]['args'][1]!=0x20f or \
       one[8]['args'][4] or one[8]['result'] not in (0,1):raise ValueError('creation constructor/outref/event argument join')
    for record in (one[4],one[6]):
        after=record['after']
        if after['direct_reads']&127!=127 or after['controller_owner']!=status['object'] or after['controller_agent']!=status['agent'] or after['slots']!=status['agent']+0xad0:
            raise ValueError('creation completed navigation relationship')
    if one[6]['after_identity']['controller']!=status['controller']:
        raise ValueError('creation setter did not publish captured controller')
    if one[8]['args'][2]!=one[8]['args'][3]:raise ValueError('creation query limits differ')
    floating(one[8]['args'][2]);floating(one[8]['args'][3])
    rngs=[r for r in records if r['kind']==7];posted=[r for r in records if r['kind']==9]
    if len(rngs)!=(1 if one[8]['result'] else 2) or len(posted)!=(0 if one[8]['result'] else 1):
        raise ValueError('creation conditional RNG/post coverage')
    for rng in rngs:
        expected_parent=one[5] if rng['caller']==0xe85531 else one[4]
        bound=0x3f000000 if rng['caller']==0xe85531 else 0x40000000
        if rng['parent']!=expected_parent['index'] or rng['args'][:2]!=[rng['before']['rng_pointer'],bound] or rng['before']['rng_pointer']!=rng['after']['rng_pointer']:
            raise ValueError('creation RNG scope/state pointer/bound')
        floating(rng['result'],4)
    if sorted(r['caller'] for r in rngs)!=sorted([0xe85531]+([] if one[8]['result'] else [0xea90e1])):
        raise ValueError('creation RNG site multiplicity')
    if posted:
        post=posted[0]
        if post['parent']!=one[4]['index'] or post['args'][0]!=status['object'] or not post['args'][1] or post['args'][3]!=1:
            raise ValueError('creation actual post arguments')
        floating(post['args'][2])
    startup=[r for r in records if r['kind']==10 and r['caller']==0x1066dc6 and r['args'][3:6]==[0,4,1] and r['range']['script']=='orders.base']
    if len(startup)!=1 or startup[0]['parent']!=one[3]['index']:
        raise ValueError('creation exact startup range missing from Pilot scope')
    return dict(absent,supported=True,records=records,orders=sorted(orders),status=status,
                agent_timer=one[5]['after']['timer'],posted_due=posted[0]['args'][2] if posted else None,
                scope='reserved_constructor_call_boundaries_only')


def join_first_origin(trace, origins, identities, update):
    """The same interrupt scope produced both captures; require the exact join.

    No later range may replace an earlier unknown or unrelated native entry.
    Other pending lifetimes remain independently validated by the MD parser.
    """
    if not origins['supported'] or not identities['supported'] or update['kind']!=6:
        raise ValueError('creation first-origin dependency or unsupported family')
    origin=next((r for r in origins['rows'] if r['runtime']==update['runtime']),None)
    identity=next((r for r in identities['rows'] if r['runtime']==update['runtime']),None)
    if not origin or not identity or origin['source']!=2:
        raise ValueError('creation first origin is not the captured range')
    candidates=[r for r in trace['records'] if r['kind']==10 and r['enter']<origin['order']<r['leave']]
    matches=[]
    for record in candidates:
        before,after,ranged=record['before'],record['after'],record['range']
        expected={
            'runtime':before['runtime'],'thread':before['thread'],'caller':record['caller'],
            'verified':ranged['verified'],'caller_source':ranged['source'],
            'exec':record['args'][0],'context':record['args'][1],'program':record['args'][2],
            'begin':record['args'][3],'end':record['args'][4],'mode':record['args'][5],
            'pc':ranged['pc'],'context_pc':ranged['context_pc'],'entity':ranged['entity'],
            'depth':ranged['depth'],'script':ranged['script'],'script_hash':ranged['script_hash'],
            'ui':before['ui'],'view':before['view'],'time':before['time'],
            'rng_before':before['rng'],'rng_after':after['rng'],'result':record['result'],
        }
        if any(origin[k]!=v for k,v in expected.items()):continue
        native_identity=record['before_identity']
        fields='object type actual object_state owner_getter owner owner_state controller controller_type controller_owner controller_state slots agent agent_owner agent_state reads'.split()
        if any(identity[k]!=native_identity[k] for k in fields) or identity['stable']!=before['identity_stable'] or \
           identity['recorded']!=before['identity_recorded']:continue
        matches.append(record)
    if len(matches)!=1:raise ValueError('creation first-origin range/identity/clock/RNG/result join')
    trace['first_origin_record']=matches[0]['index']
