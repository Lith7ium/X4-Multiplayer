"""Copied prebind queue observations; never accepted tickets or authority."""
import hashlib
import re

PREFIX='NATIVE_GATE PLAYER_TRANSITION_QUEUE_DIAG'
SUFFIX='|PLAYER_TRANSITION_QUEUE_DIAG_1'
ROW=set(('schema ordinal kind tick boot thread bound selection watch imports id_generation runtime saved command agent object '
         'imported_object bound_agent manager recipient_reads manager_reads manager_match caller reads decode type first second '
         'parent parent_kind order candidates source byte_match').split())
STOP=set(('schema capacity count rows calls unknown unreadable ambiguous dropped open restore_open closing run image '
          'allocation_generation_known ticket_authority atomic_lease').split())
HEX=set('command agent object imported_object bound_agent manager caller type image'.split())
U64=set('tick runtime saved parent order source calls unknown unreadable ambiguous dropped open restore_open'.split())|HEX
FALSE=dict(allocation_generation_known=False,ticket_authority=False,atomic_lease=False)

def configured(config):
    values=re.findall(r'\bnative_gate_player_transition_queue_evidence\s*=\s*([^\r\n]*)',config)
    if not values:return False
    if values!=['true']:raise ValueError('exact queue evidence flag required')
    if (re.findall(r'\bnative_gate_player_probe\s*=\s*"([^"\r\n]*)"',config)!=['diagnostic-getup'] or
        re.findall(r'\bnative_gate_diagnostic_contract\s*=\s*"([^"\r\n]*)"',config)!=['diagnostic-getup-1'] or
        re.findall(r'\bnative_gate_player_transition_observer\s*=\s*([^\r\n]*)',config)!=['true'] or
        re.findall(r'\bnative_gate_player_transition_manifest\s*=\s*"([^"\r\n]*)"',config)!=['2|00000000000ae79c/p/ship_arg_s_scout_01_a_macro/player'] or
        re.findall(r'\bnative_gate_player_transition_last_tick\s*=\s*([^\r\n]*)',config)!=['120']):
        raise ValueError('queue evidence requires exact local GetUp watch')
    return True

def fields(raw,expected):
    parts=raw.split(' ');pairs=[p.split('=') for p in parts];r=dict(pairs)
    if len(r)!=len(parts) or set(r)!=expected:raise ValueError('field set')
    for key,value in r.items():
        if key in ('run','first','second'):
            if not re.fullmatch('[0-9a-f]{'+str(64 if key=='run' else 32)+'}',value):raise ValueError('hex bytes')
        elif key in ('runtime','saved'):
            if not re.fullmatch('[0-9a-f]{16}',value):raise ValueError('fixed identity')
            r[key]=int(value,16)
        else:
            base=16 if key in HEX else 10;bits=64 if key in U64 else 32
            if not re.fullmatch(r'0|[1-9a-f][0-9a-f]{0,15}' if base==16 else r'0|[1-9][0-9]{0,19}',value):raise ValueError('canonical number')
            r[key]=int(value,base)
            if r[key]>=1<<bits:raise ValueError('numeric width')
    return r

def decode_valid(r,image):
    first=bytes.fromhex(r['first']);second=bytes.fromhex(r['second']);zero=bytes(16)
    if (not r['reads']&1 and r['type']) or (not r['reads']&2 and first!=zero) or (not r['reads']&4 and second!=zero):return False
    d=r['decode'];expected=image+0x2c51c38
    if r['selection']!=1:return d==0 and r['reads']==0 and r['type']==0 and first==second==zero
    masks={1:7,2:1,3:0,4:1,5:1,6:3,7:7,8:7,9:7}
    if masks.get(d)!=r['reads']:return False
    if d==3:return True
    inside=image<=r['type']<image+0x6fa9000
    if d==4:return not inside
    if d==2:return inside and r['type']!=expected
    if r['type']!=expected:return False
    if d in (5,6):return True
    if d==7:return first!=second
    if first!=second:return False
    copied=int.from_bytes(first[:8],'little')
    if d==8:return copied!=expected
    return copied==expected and ((int.from_bytes(first[8:12],'little')<=6)==(d==1))

def evidence(log,config,transitions):
    rows={};stop=None;errors=[];enabled=0;records=0;run=None
    try:
        selected=configured(config)
        tokens=re.findall(r'\btoken\s*=\s*"([0-9a-f]{32})"',config)
        if selected and len(tokens)!=1:raise ValueError('one run token')
        if selected:run=hashlib.sha256(tokens[0].encode()).hexdigest()
    except ValueError as error:selected=True;errors.append(str(error))
    for line in log.splitlines():
        if PREFIX not in line:continue
        records+=1
        if records>512 or len(line)>=1152:
            if records<=513:errors.append('queue evidence record/line bound')
            continue
        if not selected:errors.append('unconfigured queue evidence');continue
        try:
            tail=line.split(PREFIX,1)[1]
            if stop is not None:raise ValueError('record after STOP')
            if tail.startswith('_ENABLED '):
                r=fields(tail[9:],set('schema resumed run allocation_generation_known ticket_authority'.split()))
                if r!=dict(schema=1,resumed=len(rows),run=run,allocation_generation_known=0,ticket_authority=0):raise ValueError('enable/cursor')
                enabled+=1
            elif tail.startswith('_STOP '):
                if len(tail[6:])>=768:raise ValueError('status capacity')
                stop=fields(tail[6:],STOP)
            elif tail.startswith(' '):
                if len('PLAYER_TRANSITION_QUEUE_DIAG'+tail)>=1024 or not enabled:raise ValueError('row capacity/order')
                r=fields(tail[1:],ROW)
                if r['ordinal']!=len(rows)+1 or r['ordinal']>256:raise ValueError('ordinal')
                rows[r['ordinal']]=r
            else:raise ValueError('unknown record')
        except (ValueError,TypeError):errors.append('malformed queue evidence record')
    if not selected and not records:return None
    if not enabled or not stop:errors.append('queue evidence configuration/STOP missing')
    if stop:
        if (stop['schema']!=1 or stop['capacity']!=256 or stop['count']!=stop['rows'] or stop['count']!=len(rows) or
            stop['calls']!=len(rows)+stop['unknown']+stop['dropped'] or stop['ambiguous']>len(rows)+stop['dropped'] or
            stop['unreadable']>stop['calls'] or stop['run']!=run or stop['closing']!=1 or
            any(stop[k] for k in ('dropped','open','restore_open','allocation_generation_known','ticket_authority','atomic_lease')) or
            stop['image']<0x10000 or stop['image']%0x10000 or stop['image']+0x6fa9000>=1<<64):errors.append('queue evidence coverage/closure')
        core=transitions.get('stop') or {}
        if (core.get('schema')!=2 or stop['calls']!=core.get('queue_calls') or
            stop['open']!=core.get('open') or stop['restore_open']!=core.get('restore_open')):errors.append('queue evidence admission join')
        image=stop['image'];events=transitions.get('events',{});watches=transitions.get('watches',{})
        # Capture samples the global transition clock before publishing a row.
        # Concurrent rows need not be monotonic. At closed export, however,
        # their samples must fit the complete recorded enter/leave timeline.
        ordinals=[e[k] for e in events.values() for k in ('enter','leave')]
        clock_complete=(all(core.get(k)==len(events) for k in ('count','complete','rows')) and
                        sorted(ordinals)==list(range(1,len(events)*2+1)))
        if not clock_complete:errors.append('queue evidence transition clock incomplete')
        last_order=max(ordinals,default=0)
        for ordinal,r in rows.items():
            try:
                if (r['schema']!=1 or r['kind'] not in (1,2) or r['tick']>120 or r['boot']>3 or not r['thread'] or
                    (r['tick']>0 and r['boot']!=3) or r['order']>last_order or
                    r['bound'] not in (0,1) or r['selection'] not in (1,2,3) or r['watch'] not in (1,2) or
                    r['id_generation']>65535 or r['recipient_reads']>2 or r['manager_reads']>1 or r['manager_match']>1 or
                    r['byte_match']>1 or not image<=r['caller']<image+0x6fa9000 or not decode_valid(r,image)):raise ValueError('row fields/decode')
                if r['selection']!=1:raise ValueError('ambiguous imported association')
                w=watches[r['watch']]['s0']
                if (r['watch']!=1 or r['saved']!=0xae79c or r['imports']!=1 or r['saved']!=w['saved'] or r['runtime']!=w['runtime'] or r['imported_object']!=w['object'] or
                    r['id_generation']!=((r['runtime']>>25)&0xffff) or r['command']<0x10000 or
                    r['bound_agent']!=(w['agent'] if r['bound'] else 0)):raise ValueError('imported identity snapshot')
                if r['kind']==1:
                    if r['agent'] or r['object'] or r['recipient_reads'] or not r['manager'] or r['manager_reads']!=1 or r['manager_match']!=1:raise ValueError('enqueue receiver')
                elif (r['caller']!=image+0xeacf66 or r['manager'] or r['manager_reads'] or r['manager_match'] or
                      r['recipient_reads']!=2 or r['object']!=r['imported_object'] or r['agent']!=w['agent']):raise ValueError('dispatch receiver')
                if r['parent']:
                    parent=events[r['parent']]
                    if (parent['kind']!=r['parent_kind'] or parent['thread']!=r['thread'] or parent['tick']!=r['tick'] or
                        parent['boot']!=r['boot'] or not parent['enter']<=r['order']<parent['leave']):raise ValueError('scope parent')
                elif r['parent_kind']:raise ValueError('unknown parent')
                candidates=[q for i,q in rows.items() if i<ordinal and q['kind']==1 and q['command']==r['command'] and q['runtime']==r['runtime']] if r['kind']==2 else []
                source=candidates[0]['ordinal'] if len(candidates)==1 else 0
                match=int(bool(source and candidates[0]['selection']==r['selection']==1 and candidates[0]['decode']==r['decode']==1 and candidates[0]['first']==r['first']))
                if r['candidates']!=len(candidates) or r['source']!=source or r['byte_match']!=match:raise ValueError('copied correlation')
                if r['decode'] not in (1,2):raise ValueError('failed native command read/decode')
            except (ValueError,KeyError,TypeError):errors.append('invalid/incomplete queue observation '+str(ordinal))
        if stop['unreadable'] or stop['ambiguous']:errors.append('unreadable/ambiguous queue association retained')
    return dict(schema=1,rows=rows,stop=stop,errors=errors,evidence_valid=not errors,**FALSE)
