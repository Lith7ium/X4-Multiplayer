"""Read-only evaluated CreatePosition receipts. No inferred canonical owners."""
import math
import re
import struct
from native_world_unknown_identity import obj, valid_object, text as object_text

WIRE_SIZE, ROW_BYTES, ROWS = 5320, 256, 21
LEGACY_WIRE_SIZE=5176
BQ='kind coordinates space_tag reference_tag range_kind'.split()
WQ='slot stack_limit stack_base caller parent_return resolver_output resolved_before resolved_after'.split()
META = 'schema event action depth thread errors children foreign samples reads bytes abnormal'.split()
IDENTITY = 'image enter leave parent result caller descriptor output frame optional tls'.split()
GQ = 'current context lifetime entity runtime script program begin end pc node vtable target table tls_time game real saved rng'.split()
GD = 'tag version flags chance app loading view mxcsr x87cw'.split()
SQ = 'slot rng caller parent_return descriptor frame stack_limit stack_base'.split()
SD = 'span distribution parameter result source normal abnormal'.split()
STOP = set('schema capacity sampler_capacity read_limit byte_limit wire_size row_bytes reserved complete exported rows calls open peak restore_open closed failures dropped owned protection_pending flush_pending patch_failures patch_errors'.split())


def configured(config):
    rows = re.findall(r'^\s*X4LAN_CONFIG\.native_gate_world_position\s*=\s*(.*?)\s*$', config, re.M)
    if not rows and 'native_gate_world_position' not in config:
        return False
    if rows != ['true']:
        raise ValueError('position flag must be one explicit true')
    return True


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, n):
        if n > len(self.data)-self.at:
            raise ValueError('truncated position bytes')
        out = self.data[self.at:self.at+n]
        self.at += n
        return out

    def words(self, names, width):
        return dict(zip(names, struct.unpack('<'+('Q' if width == 8 else 'I')*len(names), self.take(len(names)*width))))

    def guard(self):
        return self.words(GQ, 8) | self.words(GD, 4)


def decode(raw):
    if len(raw) not in (WIRE_SIZE,LEGACY_WIRE_SIZE):
        raise ValueError('position byte count')
    r = Reader(raw)
    e = r.words(META, 4)
    e['calls'] = list(struct.unpack('<4I', r.take(16)))
    e['returns'] = list(struct.unpack('<4I', r.take(16)))
    e.update(r.words(IDENTITY, 8))
    for name in ('outer', 'transform_guard', 'radius_guard'):
        e[name] = [r.guard(), r.guard()]
    e.update(r.words('space reference transform time range radius_frame radius_optional'.split(), 8))
    e.update(r.words('fifth radius space_present reference_present'.split(), 4))
    e['transforms'] = [r.take(64), r.take(64)]
    e['bases'] = [r.take(16), r.take(16)]
    e['output_bytes'] = r.take(16)
    e['objects'] = [[r.take(376) for _ in range(2)] for _ in range(2)]
    e['samplers'] = []
    for _ in range(4):
        s = r.words(SQ, 8) | r.words(SD, 4)
        s['difference'] = [r.take(16), r.take(16)]
        s['minimum'] = [r.take(16), r.take(16)]
        s['guards'] = [r.guard(), r.guard()]
        e['samplers'].append(s)
    if len(raw)==WIRE_SIZE:
        e['branch']=[r.words(BQ,4),r.words(BQ,4)]
        e['radius_branch']=[r.words(BQ,4),r.words(BQ,4)]
        e['radius_witness']=r.words(WQ,8)
    if r.at != len(raw):
        raise ValueError('position decoder extent')
    return e


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def finite(bits, width=8):
    return math.isfinite(struct.unpack('<d' if width == 8 else '<f', bits.to_bytes(width, 'little'))[0])


def numeric(raw):
    tag = int.from_bytes(raw[:4], 'little')
    require(tag <= 9, 'non-immediate numeric Value')
    width = 4 if tag <= 1 or 4 <= tag <= 6 else 8
    bits = int.from_bytes(raw[8:8+width], 'little')
    require(tag < 4 or finite(bits, width), 'nonfinite numeric Value')
    return {'tag': tag, 'bits': bits, 'width': width}


def class_signature(o, image):
    """Retain validated class inputs for this exact Windows image only."""
    fields=('vtable','col_address','type_descriptor','class_function','bitmap_address')
    return {'scope':'windows_image_class_v1',**{k+'_rva':o[k]-image for k in fields},
            'rtti_name':object_text(o['name_raw'],o['name_length']),
            'bitmap':o['bitmap'].hex(),'col':o['col'].hex()}


def validate(e, actions, values, intervention=None):
    if isinstance(values, dict): values=values.values()
    parent = actions[e['action']]
    pe, before, after = (parent[k] for k in ('event', 'before', 'after'))
    require(parent.get('validation_errors') == [] and not pe['errors'], 'invalid completed parent Action')
    scoped=None
    if intervention is not None:
        from native_world_position_rng import bridge
        scoped=bridge(e,parent,values,intervention)
    require(e['schema'] == 2 and not e['errors'] and not e['abnormal'] and not e['foreign'], 'failed native position receipt')
    branch=e['branch'][0]
    require(branch==e['branch'][1] and branch['kind']<=3 and branch['coordinates']<=1 and branch['range_kind'] in (0,2), 'changed/unsupported branch inputs')
    transformed=e['calls'][1]==1; sampled=branch['range_kind']==2
    require(e['calls']==e['returns'] and e['calls'][0]==1 and e['calls'][1] in (0,1) and
            e['calls'][2:]==([1,1] if sampled else [0,0]) and e['samples']==int(sampled), 'missing/extra original calls or returns')
    require(branch['space_tag'] or not transformed, 'forbidden transform for absent space expression')
    require(0 < e['reads'] <= 256 and 0 < e['bytes'] <= 8192 and 1 <= e['depth'] <= 4 and 0 < e['enter'] < e['leave'], 'position bounds')
    require(pe['stage'] == 2 and pe['type'] == 0x2c0cdd0 and pe['action'] == 0xbe0ba0 and e['thread'] == pe['thread'], 'parent Action class/thread')
    base = before['vtable']-0x2c0cdd0
    require(e['image'] == base and base >= 0x10000 and base % 0x10000 == 0, 'position module base')
    require(e['caller'] == base+0xbe0bd1 and e['descriptor'] == before['node']+0x28 and e['frame'] == before['frame'] and
            e['output'] >= 0x10000 and e['result'] == e['output'] and not e['optional'], 'outer argument/result join')
    def zero(*items):return all(v==0 for item in items for v in flatten_numbers(item))
    if transformed:
        require(e['space'] and e['transform'] and not e['fifth'], 'actual transform arguments')
    else:
        require(zero(*(e[k] for k in ('space','reference','transform','time','fifth','space_present','reference_present','transform_guard','transforms','objects'))), 'inactive transform section is not zero')
    if sampled:
        require(e['range']==e['descriptor']+0x68 and e['radius_frame']==e['frame'] and not e['radius_optional'], 'actual radius arguments')
        require(e['radius_branch']==[branch,branch], 'changed radius branch inputs')
        require(e['bases'][0]==e['bases'][1], 'consumed base changed during radius')
        if transformed:require(e['transforms'][1][:16]==e['bases'][0], 'transformed base was not consumed')
    else:
        require(zero(*(e[k] for k in ('range','radius_frame','radius_optional','radius','bases','radius_guard','radius_branch','samplers','radius_witness'))), 'inactive radius section is not zero')
        if transformed:require(e['transforms'][1][:16]==e['output_bytes'], 'transform-only output join')
    null_witness=bool(branch['space_tag'] and not transformed)
    if null_witness:
        require(sampled,'unwitnessed null space without radius')
        w=e['radius_witness'];t=w['slot']
        require(t%16==8 and 0<w['stack_limit']<=t<=(1<<64)-0x140 and t+0x140<=w['stack_base'], 'radius witness stack bounds/alignment')
        require(w['caller']==base+0xc6fb6f and w['resolver_output']==t+0x138 and w['parent_return'] and
                w['resolved_before']==w['resolved_after']==0, 'native evaluated-null space witness')
        require(all(w[k]==e['samplers'][0][k] for k in ('stack_limit','stack_base')), 'radius/sampler TIB identity mismatch')
    else:require(zero(e['radius_witness']), 'unused radius witness is not zero')
    guards = e['outer'] + (e['transform_guard'] if transformed else []) + (e['radius_guard']+e['samplers'][0]['guards'] if sampled else [])
    mapping = {'saved': 'saved_time', 'app': 'app_flags', 'loading': 'vbi_loading', 'pc': None, 'rng': None,
               'flags': None, 'mxcsr': None, 'x87cw': None}
    for g in guards:
        for key in GQ+GD:
            if key in mapping and mapping[key] is None:
                continue
            name = mapping.get(key, key)
            if name in before:
                require(g[key] == before[name] == after[name], 'changed partial guard '+key)
        require(g['pc'] == pe['slot']+1 and g['flags'] == 0 and g['view'] == 1 and g['tag'] == 0x66, 'guard PC/tag/flags')
        require(g['tls_time'] == g['game'] == g['saved'] and all(finite(g[k]) for k in ('tls_time', 'game', 'real', 'saved')), 'guard clocks')
        require(g['mxcsr'] <= 0xffff and g['x87cw'] <= 0xffff, 'FP control bounds')
        require({k:v for k,v in g.items() if k != 'rng'} == {k:v for k,v in guards[0].items() if k != 'rng'}, 'guard lifetime/phase/FP controls changed')
    if transformed:require(e['time'] == guards[0]['tls_time'], 'evaluated transform time')
    classes={'space':None,'reference':None}
    for j, name in enumerate(('space', 'reference')) if transformed else ():
        require(e[name+'_present'] == bool(e[name]), 'typed null presence')
        require(e['objects'][0][j] == e['objects'][1][j], 'object registry/class lifetime changed')
        raw = e['objects'][0][j]
        if not e[name]:
            require(raw == bytes(376), 'failed object capture masquerades as null')
            classes[name]=None
        else:
            o = obj(raw)
            require(valid_object(o, base) and o['object'] == e[name] and o['saved'], 'unproven saved object identity')
            classes[name]=class_signature(o,base)
    s = e['samplers'][0];sampler_steps=direction_steps=0
    if sampled:
        require(s['source'] == 1 and e['tls'] >= 0x10000 and s['rng'] == e['tls']+0x318 and s['normal'] == 1 and not s['abnormal'], 'sampler TLS/return')
        require(s['caller'] == base+0xc59951 and s['parent_return'] == base+0xc59b2f and s['descriptor'] == e['range'] and s['frame'] == e['frame'], 'sampler scope join')
        require(s['slot']%16==8 and 0 < s['stack_limit'] <= s['slot'] <= (1<<64)-0x158 and s['slot']+0x158 <= s['stack_base'], 'sampler stack bounds/alignment')
        require(s['minimum'][0] == s['minimum'][1] and s['difference'][0] == s['difference'][1], 'evaluated range Value changed')
        numeric(s['minimum'][0]); numeric(s['difference'][0])
        require(finite(s['span'],4) and finite(s['result'],4) and finite(e['radius'],4), 'nonfinite sampler/radius')
        require(all(v == 0 for unused in e['samplers'][1:] for v in flatten_numbers(unused)), 'extra sampler bytes')
    assigned = [v for v in values if v['event']['action'] == e['action'] and v['event']['kind'] == 0]
    require(len(assigned) == 1, 'missing/ambiguous downstream Value')
    v = assigned[0]
    require(v.get('validation_errors') == [] and not v['event']['errors'] and v['event']['caller']==0xbe0c8b and v['value']['tag'] == 0x63 and v['value']['kind'] == 2, 'unsupported downstream Value')
    require(v['value']['payload'] == e['output_bytes'] and v['event']['rng_before'] == (scoped['caller_after'] if scoped else e['outer'][1]['rng']), 'downstream output/RNG join')
    require(e['outer'][0]['rng']==pe['rng_before'] and v['event']['rng_after']==pe['rng_after'], 'enclosing Action RNG endpoint join')
    if sampled:
        if transformed:require(e['transform_guard'][1]['rng']==e['radius_guard'][0]['rng'], 'unobserved RNG change after transform')
        require(s['guards'][1]['rng']==e['radius_guard'][1]['rng'], 'unobserved RNG change after sampler')
        require(1<=s['distribution']<=5 and s['parameter']<=4096, 'unsupported sampler distribution/count')
        sampler_steps=1 if s['distribution']==1 or s['parameter']<2 else s['parameter']
        rng=scoped['scoped_before'] if scoped else s['guards'][0]['rng']
        for _ in range(sampler_steps): rng=rng_step(rng)
        require(rng==s['guards'][1]['rng'], 'sampler RNG endpoint mismatch')
        direction_steps=2 if struct.unpack('<f',e['radius'].to_bytes(4,'little'))[0]>0 else 0
        rng=e['radius_guard'][1]['rng']
        for _ in range(direction_steps): rng=rng_step(rng)
        require(rng==e['outer'][1]['rng'], 'direction RNG endpoint mismatch')
    elif transformed:require(e['transform_guard'][1]['rng']==e['outer'][1]['rng'], 'unobserved RNG change after transform-only path')
    return {'saved_owner': f'{pe["id"]:016x}', 'name': pe['name'], 'version': pe['version'], 'slot': pe['slot'],
            'paths':{'space':'executed' if transformed else 'evaluated_null' if null_witness else 'expression_absent',
                     'radius':'sampled' if sampled else 'range_absent','reference':'executed' if transformed and e['reference'] else 'null' if transformed else 'not_evaluated'},
            'branch':branch,
            'space': obj(e['objects'][0][0])['saved'] if transformed else None, 'reference': obj(e['objects'][0][1])['saved'] if e['reference'] else None,
            'space_class':classes['space'],'reference_class':classes['reference'],
            'base': (e['bases'][0] if sampled else e['output_bytes']).hex(),
            'transform_before': e['transforms'][0].hex() if transformed else None, 'transform_after': e['transforms'][1].hex() if transformed else None,
            'time': e['time'] if transformed else None, 'minimum': numeric(s['minimum'][0]) if sampled else None, 'difference': numeric(s['difference'][0]) if sampled else None,
            'span': s['span'] if sampled else None, 'distribution': s['distribution'] if sampled else None, 'parameter': s['parameter'] if sampled else None, 'sampler_result': s['result'] if sampled else None,
            'radius': e['radius'] if sampled else None, 'rng': [g['rng'] for g in guards], 'sampler_steps':sampler_steps,'direction_endpoint_steps':direction_steps,'output': e['output_bytes'].hex(),
            'mxcsr': guards[0]['mxcsr'], 'x87cw': guards[0]['x87cw']}


def flatten_numbers(value):
    if isinstance(value, dict):
        for item in value.values(): yield from flatten_numbers(item)
    elif isinstance(value, (list, tuple, bytes)):
        for item in value: yield from flatten_numbers(item)
    else:
        yield value


def rng_step(value):
    value=(value*0x5851f42d4c957f2d+0x14057b7ef767814f)&((1<<64)-1)
    return (value>>30 | value<<34)&((1<<64)-1)


def json_safe(value):
    if isinstance(value,bytes):return value.hex()
    if isinstance(value,dict):return {k:json_safe(v) for k,v in value.items()}
    if isinstance(value,(list,tuple)):return [json_safe(v) for v in value]
    return value


def evidence(lines, actions, values, errors, label='world position', enclosing_errors=(),intervention=None):
    if isinstance(lines, str): lines = lines.splitlines()
    rows, order, stop, local = {}, [], None, []; matched=0;stream_schema=None
    for line in lines:
        if 'NATIVE_GATE WORLD_POSITION' not in line: continue
        # Preserve legacy fail-closed family screening. Only the two exact
        # configured intervention tokens belong to its dedicated strict reader;
        # unknown suffixes and bare raw Position records must not disappear.
        raw_record=line.split('NATIVE_GATE ',1)[1]
        family=raw_record.rstrip('\r\n').partition(' ')[0]
        if intervention is not None and family in ('WORLD_POSITION_RNG','WORLD_POSITION_RNG_STOP'):continue
        matched+=1
        if matched>64*ROWS+1:
            local.append(f'{label}: position stream exceeds bounded publication capacity');break
        try:
            require(raw_record.isascii() and len(raw_record)<2048, 'position raw row bound')
            name, body = raw_record.rstrip('\r\n').split(' ',1)
            require(body.isascii() and len(body)+55 < 2048, 'position row bound')
            pairs = [part.split('=',1) for part in body.split()]
            require(all(len(p)==2 for p in pairs) and len({p[0] for p in pairs}) == len(pairs), 'duplicate position keys')
            raw = dict(pairs)
            if name == 'WORLD_POSITION_STOP':
                require(stop is None and set(raw) == STOP and all(re.fullmatch('[0-9]+',v) for v in raw.values()), 'position STOP schema')
                stop = {k:int(v) for k,v in raw.items()}
                require(all(v < 1<<64 for v in stop.values()), 'position STOP overflow')
            else:
                require(name == 'WORLD_POSITION' and stop is None and set(raw)=={'schema','event','part','data'}, 'position row schema/order')
                require(raw['schema'] in ('1','2') and re.fullmatch('[0-9]+',raw['event']) and re.fullmatch('[0-9]+',raw['part']), 'position row integers')
                if stream_schema is None:stream_schema=int(raw['schema'])
                require(stream_schema==int(raw['schema']),'mixed position schemas')
                event, part = int(raw['event']), int(raw['part']); key = event,part
                require(1 <= event <= 64 and 0 <= part < ROWS and key not in rows, 'position duplicate/bound')
                length = min(ROW_BYTES, (WIRE_SIZE if stream_schema==2 else LEGACY_WIRE_SIZE)-part*ROW_BYTES)
                require(re.fullmatch('[0-9a-f]{'+str(length*2)+'}',raw['data']), 'position byte row')
                rows[key] = bytes.fromhex(raw['data']); order.append(key)
        except (ValueError,KeyError) as exc:
            local.append(f'{label}: {exc}')
    events=[]; parents={x['event']['event']:x for x in actions}; expected=[]
    for number in sorted({key[0] for key in rows}):
        record_errors=[]; event={'event':number,'supported':False,'validation_errors':record_errors}
        try:
            expected.extend((number,p) for p in range(ROWS))
            raw=b''.join(rows[number,p] for p in range(ROWS)); event['decoded']=decode(raw)
            require(event['decoded']['event']==number, 'position index mismatch')
            require(event['decoded']['schema']==stream_schema, 'position schema mismatch')
            event['semantic']=validate(event['decoded'],parents,values,intervention);event['supported']=True
        except (ValueError,KeyError,IndexError,TypeError,struct.error) as exc:
            record_errors.append(f'{label} event {number}: {exc}')
        local.extend(record_errors);events.append(event)
    if order != expected: local.append(f'{label}: missing/reordered position parts')
    if not stop or any(stop[k]!=v for k,v in {'schema':2,'capacity':64,'sampler_capacity':4,'read_limit':256,'byte_limit':8192,'wire_size':WIRE_SIZE,'row_bytes':ROW_BYTES,'closed':1}.items()) or any(stop[k] for k in ('failures','dropped','open','restore_open','owned','protection_pending','flush_pending','patch_failures','patch_errors')) or stop['reserved']!=len(events) or stop['complete']!=len(events) or stop['rows']!=len(rows) or stop['exported']!=len(rows) or stop['calls']<len(events) or not 1<=stop['peak']<=stop['calls']:
        local.append(f'{label}: incomplete/failed position STOP')
    if [e['event'] for e in events] != list(range(1,len(events)+1)) or not events:
        local.append(f'{label}: missing position events')
    used=set(); bounds=set(); prior_enter=0
    for record in events:
        e=record.get('decoded',{})
        if not e: continue
        if e['action'] in used or not e['parent'] < e['event'] or e['enter'] in bounds or e['leave'] in bounds or e['enter']<=prior_enter:
            local.append(f'{label}: ambiguous parent/boundary sequence')
        prior_enter=e['enter'];used.add(e['action']);bounds.update((e['enter'],e['leave']))
    if sorted(bounds)!=list(range(1,len(events)*2+1)):local.append(f'{label}: missing causal boundary')
    decoded={r['event']:r['decoded'] for r in events if 'decoded' in r}
    for number,e in decoded.items():
        children=[c for c in decoded.values() if c['parent']==number]
        ancestors=[p for p in decoded.values() if p['enter']<e['enter']<e['leave']<p['leave']]
        parent=max(ancestors,key=lambda p:p['enter']) if ancestors else None
        if e['children']!=len(children) or e['parent']!=(parent['event'] if parent else 0) or e['depth']!=(parent['depth']+1 if parent else 1) or any(e['enter']<p['enter']<e['leave']<p['leave'] for p in decoded.values()):
            local.append(f'{label} event {number}: inconsistent nested chronology')
    if used != {x['event']['event'] for x in actions if x['event']['stage']==2}:
        local.append(f'{label}: CreatePosition Action coverage gap')
    errors.extend(local)
    return {'events':events,'stop':stop,'errors':local,'enclosing_errors':list(enclosing_errors),
            'selected_valid':not local,'coverage_complete':not local and not enclosing_errors,'canonical_unknown_identity':False,
            'guard_scope':'partial boundary rereads joined to complete parent Action and downstream Value; no lifetime lease/ABA exclusion'}


def paired_diagnostics(captures, claimed_pass, errors):
    if not captures: return None
    ordered=sorted(captures,key=lambda x:x['slot']); valid=len(ordered)==2 and {c['slot'] for c in ordered}=={1,2} and all(c['evidence']['selected_valid'] for c in ordered)
    differences=[]
    if valid:
        left,right=([e['semantic'] for e in c['evidence']['events']] for c in ordered)
        for i in range(max(len(left),len(right))):
            if i>=len(left) or i>=len(right): differences.append({'ordinal':i+1,'missing':'left' if i>=len(left) else 'right'})
            elif left[i]!=right[i]: differences.append({'ordinal':i+1,'fields':{k:[left[i][k],right[i][k]] for k in left[i] if left[i][k]!=right[i][k]}})
    if claimed_pass and (not valid or differences or any(not c['evidence']['coverage_complete'] for c in ordered)):
        errors.append('evaluated CreatePosition capture incomplete or differs')
    return json_safe({'selected_valid':valid,'differences':differences,'clients':ordered,'cross_platform_math_equivalence':False,
                     'direction_draw_coverage':'pinned two-step endpoint relation only; no per-draw samples'})
