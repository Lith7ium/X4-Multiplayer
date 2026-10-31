"""Validate local constructor observations; never accept enrollment or a build.

Pointers and physical bank order stay local. The shared ordinal join describes
observed native call boundaries, not scheduler exclusion or all engine writes.
Schema 2 separates constructor A from same-lifetime lazy-navigation B.
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
AUX='stage subject proposed_type published_before published_after rng_before rng_after reads'.split()
FOOTER+='reserved_tick execution_tick prior_static callback_order callback_tick a_closed b_closed a_error b_error static_calls excluded_setters unresolved_setters ancillary_rng foreign_callbacks whole_rng_supported true_owner'.split()
HEX=set(('handle caller result error tls time rng_pointer rng object runtime controller controller_type controller_owner slots '
         'controller_agent agent agent_owner timer c0 c8 d0 reads direct_reads type actual owner_getter owner raw_type raw_owner_getter '
         'raw_controller_type verified pc context_pc entity script_hash first_error owned builder_runtime').split())
HEX.update('proposed_type published_before published_after rng_before rng_after prior_static a_error b_error true_owner'.split())
CALLERS={1:0xba1b8b,2:0xc84531,3:0xc8224f,4:0xea4b5e,5:0xeac5ff,6:0x6cd8be,8:0xea906e,9:0xea910a}
SETTER_CALLERS={0x7458c7,0x51b71e,0x6c6e5a,0x746bb8,0x6cd8be}
ANCILLARY_RNG={0x6bd8fd,0x14ddd3a,0x5061ef,0x501440,0x4fe58b,0x50e56e,0x50e6d9,0x50ee6c,0x50f2f2}
RNG_CALLERS=ANCILLARY_RNG|{0xe85531,0xea90e1}
ARGC={1:10,2:9,3:7,4:6,5:4,6:3,7:2,8:5,9:4,10:6,12:2}
RESULT={1:1,2:0,3:1,4:0,5:1,6:0,7:2,8:3,9:0,10:1,12:0}
NARROW={1:{4:8,7:32,8:8,9:8},2:{5:8,7:32,8:8},4:{4:8,5:8},6:{2:8},7:{1:32},8:{1:32,4:8},9:{3:8},10:{5:8}}
FIXED={
    'meta':set('handle result'.split()),
    'snap':set('tls time rng_pointer rng object runtime controller controller_type controller_owner slots controller_agent agent agent_owner timer c0 d0'.split()),
    'identity':set('object actual owner controller controller_owner slots agent agent_owner raw_type raw_owner_getter raw_controller_type'.split()),
    'range':{'entity','script_hash'},
    'footer':set('handle builder_runtime object runtime controller agent prior_static true_owner'.split()),
    'aux':set('published_before published_after rng_before rng_after'.split()),
}


def parse(line, fields):
    if len(line)>1535:raise ValueError('creation row exceeds bound')
    pairs=[piece.split('=',1) for piece in line.split(' ')]
    if any(len(p)!=2 for p in pairs) or [p[0] for p in pairs]!=fields:
        raise ValueError('creation exact row schema')
    result={}
    payload=fields[len(COMMON):] if fields[:len(COMMON)]==COMMON else fields
    family='meta' if payload==META else 'snap' if payload==SNAP else 'identity' if payload==IDENTITY else 'range' if payload==RANGE else 'footer' if payload==FOOTER else 'aux' if payload==AUX else ''
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


def native_float_state(state,bound_bits):
    """Exact nonnegative FloatState scalar operations; no state write/replay."""
    bound=floating(bound_bits,4)
    if bound<0:raise ValueError('creation negative FloatState branch unsupported')
    mask=2**64-1;value=(state*0x5851f42d4c957f2d+0x14057b7ef767814f)&mask
    next_state=((value>>30)|(value<<34))&mask
    def f32(value):return struct.unpack('<f',struct.pack('<f',value))[0]
    result=f32(f32(f32(next_state&0xffffffff)*2**-32)*bound)
    return next_state,int.from_bytes(struct.pack('<f',result),'little')


def evidence(log, required=False):
    rows=[]
    for match in re.finditer(r'NATIVE_GATE CREATION_TRACE ([^\r\n]*)',log):
        if len(rows)>=128*18:raise ValueError('creation global record bound')
        rows.append(match[1])
    footers=re.findall(r'NATIVE_GATE CREATION_TRACE_STOP ([^\r\n]*)',log)
    absent={'supported':False,'records':[],'orders':[], 'sector_pose_supported':False,'enrollment_supported':False}
    if not rows and not footers:
        if required:raise ValueError('required creation trace missing')
        return absent
    if not required:raise ValueError('creation trace without explicit schema marker')
    if len(footers)!=1:raise ValueError('creation unique footer missing')
    status=parse(footers[0],FOOTER)
    expected={'schema':2,'started':1,'enabled':0,'closing':1,'parts':18,'failures':0,'first_error':0,'dropped':0,
              'open':0,'restore_open':0,'restore_failures':0,'owned':0,'handle':0x500000003,'state':5,
              'generate':1,'initialize':1,'pilot':1,'navigation':1,'agent_calls':1,'setter':1,
              'sector_pose_supported':0,'enrollment_supported':0,'whole_rng_supported':0,
              'reserved_tick':5,'a_closed':1,'b_closed':1,'a_error':0,'b_error':0,'static_calls':1}
    if any(status[k]!=v for k,v in expected.items()) or not 1<=status['count']<=128 or not 1<=status['peak']<=8 or \
       not 0<status['phase1']<status['phase2']<status['builder_order']<status['callback_order'] or \
       status['execution_tick'] not in (5,6) or status['callback_tick']!=status['execution_tick'] or not status['prior_static'] or status['prior_static']==status['controller'] or not 0<status['thread']<2**32 or \
       not status['object'] or not status['controller'] or not status['agent'] or not status['runtime'] or status['runtime']!=status['builder_runtime'] or not status['true_owner']:
        raise ValueError('creation footer incomplete, failed or unsupported')
    if len(rows)!=status['count']*18:raise ValueError('creation part coverage')
    records=[];orders=set();max_depth=0;image_bases=set()
    for index in range(1,status['count']+1):
        parts=[]
        for part in range(18):
            fields=META if part==0 else ['args'] if part==1 else SNAP if part in (2,3) else IDENTITY if part<8 else ['raw'] if part<16 else RANGE if part==16 else AUX
            row=parse(rows[(index-1)*18+part],COMMON+fields)
            if (row['schema'],row['index'],row['part'],row['complete'])!=(2,index,part,1):
                raise ValueError('creation duplicate, missing, reordered or incomplete part')
            parts.append(row)
        meta=parts[0];kind=meta['kind'];args=parts[1]['args'];aux=parts[17]
        if kind not in ARGC or meta['argc']!=ARGC[kind] or meta['result_kind']!=RESULT[kind] or meta['error'] or meta['foreign'] or \
           meta['handle']!=status['handle'] or meta['phase1']!=status['phase1'] or not 1<=meta['depth']<=8 or not 0<=meta['parent']<index or \
           not status['phase1']<meta['enter']<meta['leave']<=status['callback_order'] or meta['enter'] in orders or meta['leave'] in orders:
            raise ValueError('creation call identity, scope, ordering or failure')
        if kind in CALLERS and kind!=6 and meta['caller']!=CALLERS[kind]:raise ValueError('creation native caller')
        if kind==7 and meta['caller'] not in RNG_CALLERS:raise ValueError('creation RNG caller')
        if kind==6 and meta['caller'] not in SETTER_CALLERS:raise ValueError('creation setter caller')
        if kind==12 and meta['caller']:raise ValueError('creation callback has no native caller')
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
        elif meta['depth']!=1 or kind not in (1,4,6,12):raise ValueError('creation root scope')
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
                image_base=identity['raw_type']-identity['type'];image_bases.add(image_base)
                if image_base<=0 or identity['raw_owner_getter']!=image_base+identity['owner_getter'] or \
                   (identity['controller'] and identity['raw_controller_type']!=image_base+identity['controller_type']):
                    raise ValueError('creation raw/native class RVA disagreement')
                if snap['direct_reads']&126 and snap['controller_type']!=image_base+0x2c51158:
                    raise ValueError('creation Nav fields attached to another controller type')
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
                            before_banks=[p['raw'] for p in parts[8:12]],after_banks=[p['raw'] for p in parts[12:16]],aux=aux))
    if max_depth!=status['peak']:raise ValueError('creation peak scope coverage')
    if len(image_bases)!=1:raise ValueError('creation native image base changed')
    stack=[]
    boundaries=sorted([(r['enter'],True,r) for r in records]+[(r['leave'],False,r) for r in records],key=lambda b:b[0])
    for _,enter,record in boundaries:
        if enter:
            if record['parent']!=(stack[-1] if stack else 0):raise ValueError('creation cross-pair nesting')
            stack.append(record['index'])
        elif not stack or stack.pop()!=record['index']:raise ValueError('creation crossing original calls')
    if stack:raise ValueError('creation unmatched originals')
    return check_stages(records,status,orders,absent)


def check_stages(records,status,orders,absent):
    """Require two independent observed intervals; never infer enrollment."""
    selected=[];excluded=unresolved=ancillary=0
    for record in records:
        aux=record['aux'];kind=record['kind'];args=record['args'];stage=aux['stage'];subject=aux['subject']
        before,after=record['before'],record['after']
        if stage not in (1,2) or subject not in (1,2,3,4):raise ValueError('creation stage/subject enum')
        if stage==1 and not record['leave']<status['phase2'] or stage==2 and not record['enter']>status['phase2']:
            raise ValueError('creation A/B interval crossing')
        parent=records[record['parent']-1] if record['parent'] else None
        if parent and parent['aux']['stage']!=stage:raise ValueError('creation parent crosses A/B boundary')
        if subject in (2,3):
            if stage!=1 or kind!=6 or not parent or parent['kind'] not in (1,2,3) or not args[0] or args[0]==status['object']:
                raise ValueError('creation excluded/unresolved setter ownership')
            if (subject==3)!=(before['object']==0):raise ValueError('creation setter admission identity rewritten')
            excluded+=subject==2;unresolved+=subject==3
        elif subject==4:
            if stage!=1 or kind!=7 or record['caller'] not in ANCILLARY_RNG or not parent or \
               parent['kind']!=(3 if record['caller'] in (0x6bd8fd,0x14ddd3a) else 2):
                raise ValueError('creation ancillary RNG call scope')
            ancillary+=1
        else:selected.append(record)
        expected_reads=12 if kind==7 else 3 if kind==6 and subject==1 else 0
        if aux['reads']!=expected_reads:raise ValueError('creation auxiliary read coverage')
        if kind!=7 and (aux['rng_before'] or aux['rng_after']):raise ValueError('creation invented argument RNG')
        if kind!=6 or subject!=1:
            if any(aux[k] for k in ('proposed_type','published_before','published_after')):raise ValueError('creation invented controller publication')
        if kind==7:
            bound=floating(args[1],4);floating(record['result'],4)
            if not args[0] or bound<0:raise ValueError('creation RNG argument')
            if args[0]==before['rng_pointer']:
                if args[0]!=after['rng_pointer'] or aux['rng_before']!=before['rng'] or aux['rng_after']!=after['rng']:
                    raise ValueError('creation argument/TLS RNG join')
            elif record['caller']!=0x14ddd3a or subject!=4:raise ValueError('creation unsupported non-TLS RNG argument')
            if native_float_state(aux['rng_before'],args[1])!=(aux['rng_after'],record['result']):
                raise ValueError('creation observed FloatState state/result mismatch')
        if subject==1 and kind==6 and record['caller']!=0x6cd8be:raise ValueError('creation selected setter caller')
    if (excluded,unresolved,ancillary)!=(status['excluded_setters'],status['unresolved_setters'],status['ancillary_rng']):
        raise ValueError('creation ancillary coverage counts')
    counts=Counter(r['kind'] for r in selected)
    if any(counts[k]!=1 for k in (1,2,3,4,5,8,12)) or counts[6]!=2 or not counts[10]:
        raise ValueError('creation A/B native call coverage')
    one={k:next(r for r in selected if r['kind']==k) for k in (1,2,3,4,5,8,12)}
    # Selection is published to the observer before Initialize's entry sample.
    # Every later sample must retain it, including nested ancillary calls.
    for record in records:
        for edge,order in (('before',record['enter']),('after',record['leave'])):
            expected=status['object'] if order>=one[2]['enter'] else 0
            if record[edge]['object']!=expected:
                raise ValueError('creation selected identity missing or premature')
    static=[r for r in selected if r['kind']==6 and r['aux']['stage']==1]
    published=[r for r in selected if r['kind']==6 and r['aux']['stage']==2]
    if len(static)!=1 or len(published)!=1:raise ValueError('creation typed publication coverage')
    static,published=static[0],published[0]
    for kind in (1,2,3,10):
        if any(r['aux']['stage']!=1 for r in selected if r['kind']==kind):raise ValueError('creation constructor outside A')
    for kind in (4,5,7,8,9,12):
        if any(r['aux']['stage']!=2 for r in selected if r['kind']==kind):raise ValueError('creation navigation outside B')
    if one[1]['parent'] or one[2]['parent']!=one[1]['index'] or one[3]['parent']!=one[2]['index'] or \
       one[4]['parent'] or one[5]['parent']!=one[4]['index'] or published['parent'] or static['parent'] or one[12]['parent']:
        raise ValueError('creation exact constructor/readiness parent')
    if not one[1]['leave']<static['enter'] or not one[4]['leave']<published['enter']<published['leave']<one[12]['enter'] or \
       one[12]['leave']!=status['callback_order'] or not status['builder_order']<one[12]['enter']:
        raise ValueError('creation static/Nav/callback publication order')
    if one[1]['args'][3] or one[2]['args'][2] or not one[1]['args'][1] or one[1]['result']!=one[1]['args'][1] or \
       not one[2]['args'][1] or not one[3]['args'][1] or one[3]['result']!=one[3]['args'][1] or \
       one[3]['args'][2]!=one[2]['args'][1] or one[4]['args'][:3]!=[status['controller'],status['object'],status['prior_static']] or \
       one[5]['args'][:2]!=[status['agent'],status['object']] or one[5]['result']!=status['agent'] or \
       static['args'][:2]!=[status['object'],status['prior_static']] or published['args'][:2]!=[status['object'],status['controller']] or \
       one[12]['args'][:2]!=[status['handle'],status['runtime']]:
        raise ValueError('creation constructor/outref/prior-Static/callback join')
    sa=static['aux'];pa=published['aux']
    # Generate owns an outer result reference; Initialize/Pilot borrow the
    # generator's distinct local reference. The common selected object/runtime
    # in their samples joins the objects, not those reference-slot addresses.
    for record in (static,published):
        for edge in ('before','after'):
            native=record[edge+'_identity'];aux=record['aux']
            expected_type=aux['proposed_type'] if edge=='after' else 0 if record is static else 0x2a34b38
            if native['controller']!=aux['published_'+edge] or native['controller_type']!=expected_type:
                raise ValueError('creation publication/identity sample disagreement')
    if sa['proposed_type']!=0x2a34b38 or sa['published_before'] or sa['published_after']!=status['prior_static'] or \
       static['after_identity']['controller']!=status['prior_static'] or static['after_identity']['controller_type']!=0x2a34b38 or \
       static['after_identity']['controller_state']!=3 or static['after_identity']['controller_owner'] or \
       pa['proposed_type']!=0x2c51158 or pa['published_before']!=status['prior_static'] or pa['published_after']!=status['controller']:
        raise ValueError('creation typed Static/Nav publication')
    if one[4]['before_identity']['controller']!=status['prior_static'] or one[4]['before_identity']['controller_type']!=0x2a34b38:
        raise ValueError('creation prior controller no longer published')
    for record in (one[4],published,one[12]):
        after=record['after']
        if after['direct_reads']&127!=127 or after['controller_owner']!=status['object'] or \
           after['controller_agent']!=status['agent'] or after['slots']!=status['agent']+0xad0 or after['agent_owner']!=status['object']:
            raise ValueError('creation completed navigation relationship')
    for record in (published,one[12]):
        if record['after_identity']['controller']!=status['controller'] or record['after_identity']['controller_state']!=5 or record['after_identity']['agent_state']!=5:
            raise ValueError('creation selected controller not published at callback')
    for record in (one[3],one[4],one[12]):
        if any(record[edge]['owner_state']!=5 or record[edge]['owner']!=status['true_owner'] for edge in ('before_identity','after_identity')):
            raise ValueError('creation true owner missing at identity boundary')
    for record in records:
        if record['leave']>=one[3]['enter'] and record['after_identity']['owner']!=status['true_owner']:
            raise ValueError('creation true owner changed after Pilot admission')
    query=one[8]
    if query['parent']!=one[4]['index'] or query['args'][:2]!=[status['object'],0x20f] or query['args'][4] or query['result'] not in (0,1) or query['args'][2]!=query['args'][3]:
        raise ValueError('creation native query arguments')
    floating(query['args'][2]);floating(query['args'][3])
    rngs=[r for r in selected if r['kind']==7];posted=[r for r in selected if r['kind']==9]
    if sorted(r['caller'] for r in rngs)!=sorted([0xe85531]+([] if query['result'] else [0xea90e1])) or len(posted)!=(0 if query['result'] else 1):
        raise ValueError('creation conditional RNG/post coverage')
    for rng in rngs:
        parent=one[5] if rng['caller']==0xe85531 else one[4]
        bound=0x3f000000 if rng['caller']==0xe85531 else 0x40000000
        if rng['parent']!=parent['index'] or rng['args'][:2]!=[rng['before']['rng_pointer'],bound]:
            raise ValueError('creation native timer RNG parent/pointer/bound')
    if posted:
        post=posted[0]
        if post['parent']!=one[4]['index'] or post['args'][0]!=status['object'] or not post['args'][1] or post['args'][3]!=1:
            raise ValueError('creation actual post arguments')
        floating(post['args'][2])
    startup=[r for r in selected if r['kind']==10 and r['caller']==0x1066dc6 and r['args'][3:6]==[0,4,1] and r['range']['script']=='orders.base']
    if len(startup)!=1 or startup[0]['parent']!=one[2]['index'] or startup[0]['enter']<=one[3]['leave']:
        raise ValueError('creation Initialize-tail startup range missing')
    return dict(absent,supported=True,records=records,orders=sorted(orders),status=status,a_valid=True,b_valid=True,
                whole_rng_supported=False,agent_timer=one[5]['after']['timer'],posted_due=posted[0]['args'][2] if posted else None,
                scope='constructor_A_and_same_lifetime_navigation_B_observation_only')
