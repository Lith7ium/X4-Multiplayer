"""Bounded dynamic-ship receipts; native IDs never define portable actor roles."""
from functools import lru_cache
import re

@lru_cache(maxsize=2)
def footer(log):
    matches=re.findall(r'NATIVE_GATE SHIP_REGISTRY_STOP count=(\d+) failures=(\d+) data=([^\r\n]*)',log)
    if not matches:return ()
    if len(matches)!=1:raise ValueError('duplicate registry footer')
    count,failures,raw=matches[0]
    if not 1<=int(count)<=16 or failures!='0':raise ValueError('registry count/failure')
    result=[];previous=0
    for index,item in enumerate(raw.split(',')):
        m=re.fullmatch(r'(\d+)/([0-9a-f]{16})/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)',item)
        if not m:raise ValueError('registry footer row')
        actor,handle,reserved,bound,prepared,first,kind,late=(int(v,16 if i==1 else 10) for i,v in enumerate(m.groups()))
        if actor!=15+index or not previous<handle<2**63 or handle>>32!=reserved or (handle>>24)&255>2 or not handle&0xffffff:
            raise ValueError('registry creation identity')
        if not reserved<=bound<=reserved+1 or not bound==prepared or not first>=prepared or not 1<=kind<=17 or late:
            raise ValueError('registry boundary')
        result.append((actor,handle,reserved,bound,prepared,first,kind,late));previous=handle
    if len(result)!=int(count):raise ValueError('registry footer coverage')
    return tuple(result)

def ship_ids(log):
    try:return frozenset(range(1,13))|frozenset(row[0] for row in footer(log))
    except ValueError:return frozenset(range(1,13))

STATUS=(r'SHIP_REGISTRY_1;handle=([0-9a-f]{16});slot=(\d+);reserved_tick=(\d+);bound_tick=(\d+);'
        r'prepared_tick=(\d+);first_update_tick=(\d+);first_update_kind=(\d+);late_updates=(\d+);identity_ok=([01])')

def evidence(log,slot,last_tick,errors):
    lines=re.findall(r'NATIVE_GATE SHIP_REGISTRY ([^\r\n]*)',log)
    if not lines and 'NATIVE_GATE SHIP_REGISTRY_STOP ' not in log:return None
    records={};names={};first_updates={}
    try:
        declared={row[0]:row for row in footer(log)}
        if not declared:raise ValueError('missing registry footer')
        for line in lines:
            m=re.fullmatch(r'tick=(\d+) macro=([a-z0-9_]+) owner=([a-z0-9_]+) status='+STATUS,line)
            if not m:raise ValueError('malformed registry receipt')
            tick=int(m[1]);handle=int(m[4],16);actor,reserved,bound,prepared,first,kind,late,identity=map(int,m.groups()[4:])
            row=declared.get(actor)
            if row is None or (handle,reserved,bound,prepared)!=(row[1],row[2],row[3],row[4]) or \
               not reserved<tick<=last_tick or not identity or late or \
               not ((first==kind==0) or (prepared<=first<=tick and 1<=kind<=17)) or (actor,tick) in records:
                raise ValueError('registry identity/boundary receipt')
            if actor in names and names[actor]!=(m[2],m[3]):raise ValueError('registry identity changed')
            if actor in first_updates and first_updates[actor]!=(first,kind):raise ValueError('first update changed')
            if first:first_updates[actor]=(first,kind)
            names[actor]=(m[2],m[3]);records[(actor,tick)]=(m[2],m[3],m[4],reserved,bound,prepared,first,kind,late,identity)
        expected={(actor,tick) for actor,row in declared.items() for tick in range(row[2]+1,last_tick+1)}
        if set(records)!=expected:raise ValueError('registry receipt cadence')
        for actor,row in declared.items():
            final=records[(actor,last_tick)]
            if final[6:9]!=(row[5],row[6],0):raise ValueError('registry footer/receipt disagreement')
        scopes=re.findall(r'NATIVE_GATE SHIP_WEAPON_SCOPE handle=([0-9a-f]{16}) slot=(\d+) weapon_slots=(\d+) registered=(\d+) turret_slots=(\d+) occupied_turrets=(\d+)',log)
        if len(scopes)!=len(declared):raise ValueError('dynamic weapon coverage missing')
        seen=set()
        for handle,actor,count,registered,turrets,occupied in scopes:
            actor=int(actor)
            if actor in seen or actor not in declared or int(handle,16)!=declared[actor][1] or not 0<=int(registered)<=int(count)<=32 or not 0<=int(turrets)<=128 or occupied!='0':
                raise ValueError('dynamic weapon scope')
            seen.add(actor)
            registrations=re.findall(rf'NATIVE_GATE WEAPON_REGISTER id={actor} slot=(\d+) macro=([^\s]+)',log)
            if len(registrations)!=int(registered) or len({r[0] for r in registrations})!=len(registrations) or \
               any(not 1<=int(r[0])<=int(count) or r[1]=='nil' for r in registrations):raise ValueError('dynamic weapon registration count/slot')
    except (ValueError,IndexError,KeyError) as exc:
        errors.append(f'player {slot} dynamic ship evidence invalid: {exc}')
    return records

def seed(domain,tick,handle,stream=0,ordinal=1,detail=0):
    mask=2**64-1
    def mix(x):
        x=((x^(x>>30))*0xbf58476d1ce4e5b9)&mask
        x=((x^(x>>27))*0x94d049bb133111eb)&mask
        return x^(x>>31)
    value=mix(0x58344c5345454432^domain)
    for item in (tick,handle|2**63,stream,ordinal,detail):value=mix(value^item)
    return value

def navigation(log,slot,errors):
    rows=re.findall(r'NATIVE_GATE SHIP_NAV_FIRST data=([^\r\n]*)',log)
    if not rows and 'NATIVE_GATE SHIP_REGISTRY_STOP ' not in log:return None
    result={}
    try:
        declared={row[0]:row for row in footer(log)}
        for row in rows:
            m=re.fullmatch(r'(\d+)/([0-9a-f]{16})/(\d+)/(\d+)/([01])/'+r'/'.join(['([0-9a-f]{16})']*5),row)
            if not m:raise ValueError('malformed first navigation row')
            actor,handle,tick,calls,initialized=(int(value,16 if i==1 else 10) for i,value in enumerate(m.groups()[:5]))
            if actor in result or actor not in declared or handle!=declared[actor][1] or tick<declared[actor][4] or calls!=1 or initialized!=1:
                raise ValueError('first navigation identity/boundary')
            if int(m[9],16)!=seed(0x4e41565f55504454,tick,handle):raise ValueError('first navigation stream')
            result[actor]=dict(zip(('handle','tick','calls','initialized','time','deadline_before','deadline_after','rng_before','rng_after'),m.groups()[1:]))
        if set(result)!=set(declared):raise ValueError('first navigation coverage')
    except (ValueError,IndexError) as exc:
        errors.append(f'player {slot} dynamic first navigation invalid: {exc}')
    return result

def navigation_results(trace):
    if trace is None:return None
    return {actor:{key:value for key,value in row.items() if key!='deadline_before'} for actor,row in trace.items()}
