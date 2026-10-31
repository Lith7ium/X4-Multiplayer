"""Independent player launch domain/identity, RNG restoration and timing audit."""
import hashlib
import math
import re
import struct

DOMAIN=0x504c5957504e3031
MASK=(1<<64)-1


def seed(tick,ship,gun,call):
    def mix(value):
        value=((value^(value>>30))*0xbf58476d1ce4e5b9)&MASK
        value=((value^(value>>27))*0x94d049bb133111eb)&MASK
        return value^(value>>31)
    value=mix(0x58344c5345454432^DOMAIN)
    for field in (tick,ship,gun,call,1):value=mix(value^field)
    return value


def canonical(row):
    return re.sub(r' (?:caller|restored)=[0-9a-f]{16}', '', row)


def evidence(log,slot,context,last_tick,errors):
    if re.findall(r'PLAYER_LAUNCH_ENABLED tick=(\d+) schema=(\d+) domain=([0-9a-f]{16}) bullet_rng=(\d+)',log)!=[('1','1','504c5957504e3031','0')]:
        errors.append(f'player {slot} launch RNG declaration missing')
    pattern=(r'index=(\d+) tick=(\d+) kind=([12]) ship=([0-9a-f]{16}) gun=([0-9a-f]{16}) call=(\d+) spawn=(\d+) '
             r'caller=([0-9a-f]{16}) seed=([0-9a-f]{16}) after=([0-9a-f]{16}) restored=([0-9a-f]{16}) '
             r'original=([01]) valid=([01]) pre=([0-9a-f]{48}) post=([0-9a-f]{48})')
    created={(int(t),g,int(s)) for t,g,s in re.findall(r'PLAYER_FIRE_EVENT index=\d+ tick=(\d+) kind=1 ship=[0-9a-f]{16} gun=([0-9a-f]{16}) spawn=(\d+)',log)}
    first_moves={(g,int(s)):before for g,s,before in re.findall(r'PLAYER_FIRE_EVENT index=\d+ tick=\d+ kind=2 ship=[0-9a-f]{16} gun=([0-9a-f]{16}) spawn=(\d+) target=[0-9a-f]{16} call=1 amount=[0-9a-f]{16} absorbed=[0-9a-f]{16} before=([0-9a-f]{16})',log)}
    records=[];digest='0'*64;scopes=0;calls={};initializations=set();phases=set();previous_tick=0;restoration_ok=True
    for kind,row in re.findall(r'NATIVE_GATE (PLAYER_LAUNCH_EVENT|PLAYER_FIRE_STATE) ([^\r\n]+)',log):
        if kind=='PLAYER_FIRE_STATE':
            match=re.fullmatch(r'tick=(\d+) data=.+\|PLAYER_LAUNCH_1:1:504c5957504e3031:(\d+):(\d+):(\d+):(\d+):0:([0-9a-f]{64})',row)
            if not match or int(match[1]) in phases or (int(match[2]),int(match[3]),int(match[4]),int(match[5]),match[6])!=(len(records),0,scopes,scopes,digest):
                errors.append(f'player {slot} launch receipt wire digest/counters differ')
            if match:
                tick=int(match[1]);phases.add(tick)
                if previous_tick>tick:errors.append(f'player {slot} launch event appears before its shared tick')
            continue
        match=re.fullmatch(pattern,row)
        if not match:
            errors.append(f'player {slot} malformed launch receipt');continue
        index,tick,kind=map(int,match.group(1,2,3));ship,gun=match.group(4,5);call,spawn=map(int,match.group(6,7))
        caller,value,after,restored=map(lambda x:int(x,16),match.group(8,9,10,11))
        original,valid=map(int,match.group(12,13));pre,post=match.group(14,15)
        ok=(index==len(records)+1 and previous_tick<=tick<=last_tick and tick>0 and context and ship==context[3] and gun!='0'*16 and call>0 and valid==1)
        previous_tick=tick;key=(tick,gun)
        if kind==1:
            restoration_ok=restoration_ok and caller==restored and original==valid==1
            ok=ok and original==1 and spawn==0 and pre==post=='0'*48 and caller==restored and value==seed(tick,int(ship,16),int(gun,16),call) and call==calls.get(key,0)+1
            calls[key]=call;scopes+=1
        else:
            identity=(tick,gun,spawn)
            times=[struct.unpack('>d',bytes.fromhex(raw[i:i+16]))[0] for raw in (pre,post) for i in range(0,48,16)]
            ok=ok and original==0 and caller==value==after==restored==0 and identity in created and identity not in initializations and call<=calls.get(key,0) and all(map(math.isfinite,times))
            if (gun,spawn) in first_moves:ok=ok and first_moves[gun,spawn]==post[:16]
            initializations.add(identity)
        if not ok:errors.append(f'player {slot} invalid native launch receipt {index}')
        portable=canonical(row);records.append(portable);digest=hashlib.sha256((digest+'\n'+portable).encode()).hexdigest()
    if phases!=set(range(1,last_tick+1)):errors.append(f'player {slot} native launch state sequence incomplete')
    if created!=initializations:errors.append(f'player {slot} native projectile initialization capture incomplete')
    stop=re.findall(r'PLAYER_LAUNCH_STOP schema=1 domain=504c5957504e3031 count=(\d+) failures=(\d+) scopes=(\d+) restored=(\d+) bullet_rng=0 emitted=(\d+)',log)
    if stop!=[(str(len(records)),'0',str(scopes),str(scopes),str(len(records)))]:errors.append(f'player {slot} launch scope restoration/trace incomplete')
    return records,{'schema':1,'domain':'504c5957504e3031','scopes':scopes,'initializations':len(initializations),'caller_rng_restored':bool(restoration_ok and stop==[(str(len(records)),'0',str(scopes),str(scopes),str(len(records)))]),
                    'bullet_move_rng_reseeded':False,'wire_hash_includes_receipts':True}
