"""Validate/reconstruct sparse imported-world SHA-256 snapshots, streaming logs."""
import hashlib
import io
import re
import struct

DOMAINS=('topology','health','motion','timing')
WORD=r'[0-9a-f]{64}'
DETAIL=re.compile(r'NATIVE_GATE WORLD_DETAIL tick=(\d+) id=([0-9a-f]{16}) domain=(topology|health|motion|timing) bytes=([0-9a-f]{32,2048})(?=\r?\n|$)')
OBJECT=re.compile(r'NATIVE_GATE WORLD_OBJECT tick=(\d+) id=([0-9a-f]{16}) type=([0-9a-f]+) flags=([0-9a-f]+) parent=([0-9a-f]{16}) '+
                  ' '.join(name+'=('+WORD+')' for name in DOMAINS)+r'(?: timing_fields=(-|[0-9a-f]{2,192}))?(?: nav_raw=(-|[0-9a-f]{198}))?(?=\r?\n|$)')
SUMMARY=re.compile(r'NATIVE_GATE WORLD_HASH tick=(\d+) objects=(\d+) present=(\d+) changed=(\d+) unknown_class=(\d+) unknown_motion=(\d+) unknown_references=(\d+)(?: created=\d+)? state=(WORLD_IMPORTED_[12]:(\d+):(\d+):'+':'.join('('+WORD+')' for _ in DOMAINS)+r')(?=\r?\n|$)')
BANKS=re.compile(r'NATIVE_GATE WORLD_BANKS tick=(\d+) read_before=(\d+) write_before=(\d+) read_after=(\d+) write_after=(\d+)(?=\r?\n|$)')
SCHEMAS=('WORLD_IMPORTED_1','WORLD_IMPORTED_2')
# Created identities (native_gate_created_identities): objects created after
# the snapshot load carry bit 63 and their creation tick in bits 40..62.
CREATED=re.compile(r' created=(\d+) state=')
CREATED_BIT=1<<63
IMPORTED_MAX=524288
CREATED_MAX=131072


def is_created(identity):
    return bool(identity&CREATED_BIT)


def identity_origin(identity):
    return 'created' if is_created(identity) else 'saved'


def created_tick(identity):
    return (identity>>40)&0x7fffff if is_created(identity) else None


def roots(objects):
    hashes=[hashlib.sha256() for _ in DOMAINS]
    for identity,row in sorted(objects.items()):
        key=struct.pack('<Q',identity)
        for digest,value in zip(hashes,row['hashes']):digest.update(key+value)
    return [digest.hexdigest() for digest in hashes]


def configured_watches(config):
    match=re.search(r'native_gate_world_hash_watch\s*=\s*\{([^}]*)\}',config)
    if not match:return set()
    tokens=[part.strip() for part in match[1].split(',') if part.strip()]
    if len(tokens)>64 or any(not re.fullmatch(r"(['\"])[0-9a-f]{16}\1",token) for token in tokens):
        raise ValueError('invalid configured world watches')
    result={int(token[1:-1],16) for token in tokens}
    if 0 in result or len(result)!=len(tokens):raise ValueError('duplicate or zero world watch')
    return result


def configured_schema(config):
    entries=re.findall(r'\bnative_gate_world_hash_schema\s*=([^\r\n]*)',config)
    if not entries:return SCHEMAS[0]
    if len(entries)!=1:raise ValueError('duplicate configured world hash schema')
    match=re.fullmatch(r"\s*(['\"])(WORLD_IMPORTED_[12])\1\s*(?:--[^\r\n]*)?",entries[0])
    if not match:raise ValueError('invalid configured world hash schema')
    schema=match[2]
    if schema==SCHEMAS[1]:
        strides=re.findall(r'\bnative_gate_world_hash_stride\s*=([^\r\n]*)',config)
        if len(strides)!=1 or not re.fullmatch(r'\s*\d+\s*',strides[0]) or not 1<=int(strides[0])<=18000:
            raise ValueError('enriched world hash requires one valid census stride')
    return schema


def nav_timing(raw,attention,phase):
    """Rebuild V2 timing from same-row physical diagnostics, without float conversion.

    This validates receipt consistency. It is not a proof of process identity,
    native ownership or atomicity; those are checked by the native capture.
    """
    if len(raw)!=99:raise ValueError('incomplete enriched Nav raw tuple')
    read,write=struct.unpack_from('<II',raw)
    cursor=struct.unpack_from('<I',raw,16)[0]
    if (read,write) not in ((0,1),(1,0)) or (read,write)!=phase:
        raise ValueError('enriched Nav selector/capture mismatch')
    if cursor>2:raise ValueError('invalid enriched Nav cursor')
    pending=[]
    for physical in range(2):
        runtime,counter=struct.unpack_from('<QQ',raw,29+physical*16)
        saved=struct.unpack_from('<Q',raw,61+physical*8)[0];state=raw[77+physical]
        if not ((state==0 and runtime==0 and saved==0) or (state==1 and runtime!=0 and saved!=0)):
            raise ValueError('unknown or invalid enriched Nav pending reference')
        pending.append(struct.pack('<BQQ',state,saved,counter))
    relation=2 if cursor==2 else 0 if cursor==read else 1
    return (attention+raw[8:16]+struct.pack('<I',relation)+raw[20:29]+
            b''.join(raw[79+p*10:89+p*10] for p in (read,write))+
            b''.join(pending[p] for p in (read,write)))


def frames(lines, errors, slot, expected_watches=None, require_timing_fields=False, expected_schema=SCHEMAS[0]):
    """Yield a borrowed live record map; consume before advancing the iterator."""
    if expected_schema not in SCHEMAS:raise ValueError('unsupported world hash schema')
    enriched=expected_schema==SCHEMAS[1]
    objects={};pending={};details={};banks=None;pending_tick=None;previous=0
    for line in lines:
        if enriched and 'NATIVE_GATE WORLD_BANKS ' in line:
            m=BANKS.search(line)
            if not m:errors.append(f'player {slot} malformed world banks');continue
            tick,read,write,read_end,write_end=map(int,m.groups())
            if banks is not None or tick<=previous or (pending_tick is not None and pending_tick!=tick) or (read,write) not in ((0,1),(1,0)) or (read,write)!=(read_end,write_end):
                errors.append(f'player {slot} invalid world banks at {tick}')
            banks=(read,write);pending_tick=tick
        elif 'NATIVE_GATE WORLD_DETAIL ' in line:
            m=DETAIL.search(line)
            if not m or len(m[4])%2:
                errors.append(f'player {slot} malformed world detail');continue
            tick=int(m[1]);identity=int(m[2],16);domain=m[3]
            if tick<=previous or (pending_tick is not None and pending_tick!=tick) or domain in details.get(identity,{}) or (identity not in details and len(details)>=64):
                errors.append(f'player {slot} invalid world detail at {tick}/{identity:x}');continue
            pending_tick=tick;details.setdefault(identity,{})[domain]=bytes.fromhex(m[4])
        elif 'NATIVE_GATE WORLD_OBJECT ' in line:
            m=OBJECT.search(line)
            if not m:
                errors.append(f'player {slot} malformed world object');continue
            tick=int(m[1]);identity=int(m[2],16);kind=int(m[3],16);flags=int(m[4],16)
            if kind>0xffffffff or flags>0xffffffff:
                errors.append(f'player {slot} world object integer out of range at {tick}/{identity:x}');continue
            if (pending_tick is not None and pending_tick!=tick) or tick<=previous or identity in pending or not identity or flags>63 or bool(flags&1)!=bool(kind):
                errors.append(f'player {slot} invalid world object at {tick}/{identity:x}')
            pending_tick=tick
            pending[identity]={'type':kind,'flags':flags,'parent':int(m[5],16),'hashes':tuple(bytes.fromhex(m[i]) for i in range(6,10))}
            raw=m[10]
            if (require_timing_fields or enriched) and raw is None:errors.append(f'player {slot} missing world timing fields at {tick}/{identity:x}')
            if raw is not None:
                if raw!='-' and len(raw)%2:
                    errors.append(f'player {slot} malformed world timing fields at {tick}/{identity:x}')
                else:
                    data=(b'' if raw=='-' else bytes.fromhex(raw))+struct.pack('<IIQ',kind,flags,int(m[5],16))
                    pending[identity]['timing_data']=data
                    if hashlib.sha256(data).digest()!=pending[identity]['hashes'][3]:
                        errors.append(f'player {slot} world timing fields do not match hash at {tick}/{identity:x}')
                    if not enriched and len(data)>80:errors.append(f'player {slot} legacy timing schema exceeded at {tick}/{identity:x}')
            if enriched:
                if m[11] is None:errors.append(f'player {slot} missing enriched Nav raw fields at {tick}/{identity:x}')
                else:pending[identity]['nav_raw']=m[11]
            elif m[11] is not None:errors.append(f'player {slot} enriched Nav fields in legacy capture at {tick}/{identity:x}')
        elif 'NATIVE_GATE WORLD_HASH ' in line:
            m=SUMMARY.search(line)
            if not m:
                errors.append(f'player {slot} malformed world summary');continue
            tick,count,present,changed,unknown_class,unknown_motion,unknown_references=map(int,m.groups()[:7])
            created_match=CREATED.search(line);created=int(created_match[1]) if created_match else None
            schema=m[8].split(':',1)[0]
            if schema!=expected_schema:errors.append(f'player {slot} world hash schema/configuration mismatch at {tick}')
            if tick<=previous or (pending_tick is not None and pending_tick!=tick) or changed!=len(pending) or int(m[9])!=tick or int(m[10])!=count:
                errors.append(f'player {slot} incomplete world delta at {tick}')
            objects.update(pending)
            imported=count-(created or 0)
            if count!=len(objects) or not 0<imported<=IMPORTED_MAX or not 0<=(created or 0)<=CREATED_MAX or not 0<=present<=count or any(n>present for n in (unknown_class,unknown_motion,unknown_references)):
                errors.append(f'player {slot} invalid world coverage at {tick}')
            if created is not None and created!=sum(1 for identity in objects if is_created(identity)):
                errors.append(f'player {slot} created identity count mismatch at {tick}')
            if created is not None and any(is_created(identity) and created_tick(identity)>tick for identity in pending):
                errors.append(f'player {slot} created identity tick out of range at {tick}')
            expected=list(m.groups()[10:14])
            actual=roots(objects)
            if actual!=expected:errors.append(f'player {slot} independently calculated world roots differ at {tick}')
            if enriched:
                if banks is None:errors.append(f'player {slot} enriched world capture missing bank phase at {tick}')
                for identity,row in objects.items():
                    data=row.get('timing_data',b'');body=data[:-16];raw=row.get('nav_raw')
                    try:
                        if len(data)<16 or raw is None:raise ValueError('incomplete enriched timing receipt')
                        if len(body)==79 and row['flags']&5==5:
                            if raw=='-' or nav_timing(bytes.fromhex(raw),body[:4],banks)!=body:
                                raise ValueError('enriched Nav raw fields do not match timing preimage')
                        elif len(body)!=(4 if row['flags']&1 else 0) or raw!='-':raise ValueError('invalid enriched timing schema')
                    except ValueError as error:errors.append(f'player {slot} {error} at {tick}/{identity:x}')
            if expected_watches is not None and set(details)!=expected_watches:
                errors.append(f'player {slot} configured world detail set incomplete at {tick}')
            for identity,domains in details.items():
                row=objects.get(identity)
                if row is None or set(domains)!=set(DOMAINS):
                    errors.append(f'player {slot} incomplete world detail at {tick}/{identity:x}');continue
                for domain,data in domains.items():
                    if hashlib.sha256(data).digest()!=row['hashes'][DOMAINS.index(domain)] or struct.unpack('<IIQ',data[-16:])!=(row['type'],row['flags'],row['parent']):
                        errors.append(f'player {slot} world detail does not match hashed object at {tick}/{identity:x}/{domain}')
            yield {'tick':tick,'schema':schema,'state':m[8],'roots':expected,'objects':objects,'details':details,'banks':banks,
                   'coverage':{'imported':imported,'created':created or 0,'present':present,'unknown_class':unknown_class,'unknown_motion':unknown_motion,'unknown_references':unknown_references}}
            pending={};details={};banks=None;pending_tick=None;previous=tick
    if pending or details or banks is not None:errors.append(f'player {slot} world delta missing terminator')


def decode_detail(domains,schema=SCHEMAS[0]):
    """Decode only the pinned canonical schema, retaining exact bytes for floats."""
    result={}
    for domain,data in domains.items():
        kind,flags,parent=struct.unpack('<IIQ',data[-16:]);body=data[:-16]
        fields={'type':kind,'flags':flags,'parent':f'{parent:016x}'}
        def value(name,offset,fmt):
            size=struct.calcsize('<'+fmt);raw=body[offset:offset+size]
            if len(raw)!=size:raise ValueError(f'truncated {domain}/{name}')
            values=struct.unpack('<'+fmt,raw)
            fields[name]={'hex':raw.hex(),'value':values[0] if len(values)==1 else values}
        if domain=='topology' and flags&1 and len(body)==5:
            value('mode',0,'I');value('lifecycle',4,'B')
        elif domain=='health' and flags&2 and len(body)>=8:
            value('stored_hull',0,'d');fields['stored_shield_encoding']=body[8:].decode('ascii','replace')
        elif domain=='motion' and flags&4 and len(body)>=4:
            value('controller',0,'I');controller=struct.unpack('<I',body[:4])[0]
            if controller==0x2c51158 and len(body)==190:
                for bank in range(2):
                    base=4+bank*93
                    for vector in range(6):value(f'bank{bank}.vector{vector}',base+12*vector,'3f')
                    value(f'bank{bank}.reference0',base+72,'Q');value(f'bank{bank}.reference1',base+80,'Q')
                    value(f'bank{bank}.mode',base+88,'I');value(f'bank{bank}.layer',base+92,'B')
            elif len(body)==52:
                for vector in range(4):value(f'transform.vector{vector}',4+vector*12,'3f')
            else:fields['controller_state_hex']=body[4:].hex()
        elif domain=='timing' and flags&1 and len(body)>=4:
            value('attention',0,'I')
            if len(body)==(79 if schema==SCHEMAS[1] else 45) and flags&4 and ('motion' not in domains or domains['motion'][:4]==struct.pack('<I',0x2c51158)):
                value('nav.refresh_time',4,'d');value('nav.refresh_relation' if schema==SCHEMAS[1] else 'nav.refresh_cursor',12,'I')
                value('nav.parameters_due',16,'d');value('nav.parameters_gate',24,'B')
                for bank in range(2):
                    value(f'bank{bank}.eta',25+bank*10,'d')
                    value(f'bank{bank}.arrived',33+bank*10,'B');value(f'bank{bank}.notified',34+bank*10,'B')
                    if schema==SCHEMAS[1]:
                        value(f'bank{bank}.pending_reference_state',45+bank*17,'B')
                        value(f'bank{bank}.pending_reference',46+bank*17,'Q')
                        value(f'bank{bank}.reference_transition_count',54+bank*17,'Q')
            elif len(body)>4:fields['state_hex']=body[4:].hex()
        elif body:fields['state_hex']=body.hex()
        result[domain]=fields
    return result


def evidence(log,slot,stride,last_tick,errors,expected_watches=None,require_timing_fields=False,expected_schema=SCHEMAS[0]):
    if not isinstance(stride,int) or not 1<=stride<=18000:
        errors.append(f'player {slot} invalid world census stride');return {},[]
    result={};coverage=[]
    for frame in frames(io.StringIO(log),errors,slot,expected_watches,require_timing_fields,expected_schema):
        result[frame['tick']]=frame['state'];coverage.append(frame['coverage'])
    expected={1}|set(range(stride,last_tick+1,stride)) if last_tick else set()
    if set(result)!=expected:errors.append(f'player {slot} world snapshot cadence incomplete')
    stop=re.findall(r'NATIVE_GATE WORLD_HASH_STOP scans=(\d+) failures=(\d+)',log)
    if len(stop)!=1 or stop[0]!=(str(len(result)),'0'):errors.append(f'player {slot} world capture stop/count invalid')
    return result,coverage
