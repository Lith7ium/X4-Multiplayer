"""Independent read-only request/Agent evidence; never pilot authority approval."""
import re
import struct

from analyze_native_solver import records as solver_records

REQUEST_SIZES = dict(before=48, after=48, dirty0=2, dirty1=2, gains=48,
                     solver=20, model=16, params=0x140, configuration=16)
REQUEST_NUMBERS = ('index','tick','actor','agent_ordinal','ordinal','role','group','axis','valid')
SCOPE_SIZES = dict(banks=0x340, params=0x140, model=16)
SCOPE_NUMBERS = ('index','phase','tick','actor','ordinal','role','valid','requests','other_requests','model_same',
                 'solver_same','read','write','model_present','solver_present','main','main_capacity',
                 'pending','pending_capacity','unknown_refs','resource_refs')
CALLERS = ('00ec91cc','00ec91ee','00ec9210','00ec9230','00ec9252','00ec9274')


def row(raw, numbers, sizes, extra):
    parts = raw.split()
    result = dict(part.split('=',1) for part in parts)
    assert len(parts) == len(result) and set(result) == set(numbers) | set(sizes) | set(extra)
    assert all(re.fullmatch(r'\d+',result[k]) for k in numbers)
    result.update({k:int(result[k]) for k in numbers})
    assert all(re.fullmatch('[0-9a-f]{'+str(size*2)+'}',result[k]) for k,size in sizes.items())
    assert re.fullmatch('[0-9a-f]{16}',result['owner'])
    return result


def evidence(log, slot, last_tick, errors):
    declared = 'tick=1 schema=2 input_changes=0 early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1'
    if re.findall(r'NATIVE_GATE PLAYER_REQUEST_ENABLED ([^\r\n]*)',log) != [declared]:
        errors.append(f'player {slot} request observer declaration missing')
    fixtures = dict(re.findall(r'NATIVE_GATE WORLD_ACTOR id=(\d+) saved=([0-9a-f]{16})',log))
    scopes = {}; events = []; scope_rows = []; owners = {}; ordinals = {}; queue_incomplete = 0
    raw_scopes=[]; pending=None
    for raw in re.findall(r'NATIVE_GATE PLAYER_REQUEST_SCOPE_CHUNK ([^\r\n]*)',log):
        try:
            match=re.fullmatch(r'index=(\d+) phase=([01]) part=(\d+) parts=(\d+) bytes=(\d+) data=(.*)',raw)
            assert match
            index,phase,part,parts,size=map(int,match.groups()[:5]);data=match[6]
            assert 1<=index<=2048 and 0<size<16384 and parts==(size+767)//768 and 1<=part<=parts
            assert len(data)==min(768,size-(part-1)*768)
            if part==1:
                assert pending is None and index==len(raw_scopes)//2+1 and phase==len(raw_scopes)%2
                pending=[index,phase,parts,size,[]]
            assert pending and pending[:4]==[index,phase,parts,size] and part==len(pending[4])+1
            pending[4].append(data)
            if part==parts:
                joined=''.join(pending[4]);assert len(joined)==size
                raw_scopes.append(joined);pending=None
        except (AssertionError,ValueError,IndexError,TypeError):
            errors.append(f'player {slot} request scope chunk invalid')
    if pending or re.search(r'NATIVE_GATE PLAYER_REQUEST_SCOPE ',log):
        errors.append(f'player {slot} request scope chunks incomplete or old schema')
    for raw in raw_scopes:
        try:
            r = row(raw,SCOPE_NUMBERS,SCOPE_SIZES,('owner','model_rva','other_first_caller','main_data','pending_data'))
            actor,owner = r['actor'],r['owner']
            assert 1 <= actor <= 12 and owner == fixtures.get(str(actor)) and owner != '0'*16
            assert owners.get(owner,actor) == actor; owners[owner] = actor
            assert r['valid'] == 1 and r['role'] in (1,2,3) and 1 <= r['tick'] <= last_tick
            assert r['index'] == len(scope_rows)//2+1 and r['phase'] == len(scope_rows)%2
            assert r['read'] in (0,1) and r['write'] == 1-r['read']
            assert all(r[k] in (0,1) for k in ('model_same','solver_same','model_present','solver_present'))
            assert re.fullmatch('[0-9a-f]{8}',r['model_rva'])
            assert re.fullmatch('[0-9a-f]{8}',r['other_first_caller'])
            assert r['other_requests'] or r['other_first_caller']=='00000000'
            key = (owner,r['tick'],r['ordinal'])
            assert r['requests'] in (0,3,6)
            resources=0
            for name in ('main','pending'):
                count=r[name]
                assert count <= 16 and count <= r[name+'_capacity'] <= 65536
                assert re.fullmatch('[0-9a-f]{'+str(count*0x90*2)+'}',r[name+'_data'])
                data=bytes.fromhex(r[name+'_data'])
                for offset in range(0,len(data),0x90):
                    assert struct.unpack_from('<Q',data,offset)[0] < 0x6fa9000
                    resource=struct.unpack_from('<Q',data,offset+0x18)[0]
                    assert resource in (0,1);resources+=resource
            assert r['unknown_refs'] <= r['main']+r['pending'] and r['resource_refs'] == resources
            queue_incomplete += bool(r['unknown_refs'] or r['resource_refs'])
            if r['phase'] == 0:
                assert key not in scopes and r['ordinal'] == ordinals.get(key[:2],0)+1
                ordinals[key[:2]]=r['ordinal'];scopes[key]=[r]
            else:
                assert key in scopes and len(scopes[key]) == 1
                before=scopes[key][0]
                assert all(r[k] == before[k] for k in ('index','role','requests','other_requests','other_first_caller','model_same','solver_same','read','write'))
                scopes[key].append(r)
            scope_rows.append(r)
        except (AssertionError,ValueError,KeyError,IndexError,struct.error):
            errors.append(f'player {slot} invalid request Agent scope {len(scope_rows)+1}')
    grouped = {}
    for raw in re.findall(r'NATIVE_GATE PLAYER_REQUEST_EVENT ([^\r\n]*)',log):
        try:
            r=row(raw,REQUEST_NUMBERS,REQUEST_SIZES,('owner','caller','value','epsilon'))
            assert r['index'] == len(events)+1 and 1<=r['index']<=16384 and r['valid'] == 1 and r['role'] in (1,2,3)
            key=(r['owner'],r['tick'],r['agent_ordinal']);scope=scopes[key]
            assert len(scope)==2 and r['actor']==scope[0]['actor'] and r['role']==scope[0]['role']
            prior=grouped.setdefault(key,[])
            assert r['ordinal']==len(prior)+1 and 1 <= r['ordinal'] <= 6
            assert (r['group'],r['axis']) == ((r['ordinal']-1)//3,(r['ordinal']-1)%3)
            assert r['caller']==CALLERS[r['ordinal']-1] and r['epsilon']=='3f1a36e2eb1c432d'
            assert re.fullmatch('[0-9a-f]{16}',r['value'])
            value=struct.unpack('>d',bytes.fromhex(r['value']))[0]
            assert float('-inf') < value < float('inf')
            prior.append(r);events.append(r)
        except (AssertionError,ValueError,KeyError,IndexError,struct.error):
            errors.append(f'player {slot} invalid native request event {len(events)+1}')
    matches=set()
    try:
        solves=solver_records(log)
        for key,scope in scopes.items():
            assert len(scope)==2
            before,after=scope
            assert len(grouped.get(key,[]))==before['requests']
            if not before['requests']:continue
            owner,tick,ordinal=key
            solved=solves.get((tick,before['actor'],1,ordinal,0,0))
            assert solved and solved['owner']==owner
            offset=after['write']*0x1a0*2
            assert solved['m1'][128:320] == after['banks'][offset:offset+192]
            matches.add(owner)
    except (AssertionError,ValueError,KeyError,IndexError):
        errors.append(f'player {slot} request/Agent/solver provenance incomplete')
    stops=re.findall(r'NATIVE_GATE PLAYER_REQUEST_STOP schema=2 count=(\d+) scopes=(\d+) failures=(\d+) excluded=(\d+) open=(\d+) peak=(\d+) restore_open=(\d+) restored=(\d+) active=(\d+) early_coverage=0 generation_coverage=0 native_faction_coverage=0 raw_bank_runtime_refs=1 emitted=(\d+) emitted_scopes=(\d+)',log)
    summary=tuple(map(int,stops[0])) if len(stops)==1 else None
    if not summary or summary[0]>16384 or summary[1]>2048 or summary[0]!=len(events) or summary[1]*2!=len(scope_rows) or summary[2] or summary[4] or summary[5]<2 or summary[6:9]!=(0,1,0) or summary[9:]!=(len(events),len(scopes)):
        errors.append(f'player {slot} request trace/restore incomplete')
    if len(matches)<2:errors.append(f'player {slot} request proof needs two distinct saved fixtures with Agent publication')
    canonical=lambda rows:sorted(tuple((k,r[k]) for k in sorted(r) if k!='index') for r in rows)
    return (canonical(events),canonical(scope_rows)), {
        'events':len(events),'agent_scopes':len(scopes),'matching_published_owners':sorted(matches),
        'excluded_calls':summary[3] if summary else None,'incomplete_queue_content_snapshots':queue_incomplete,
        'peak_open_calls_and_scopes':summary[5] if summary else None,
        'restore_open_calls_and_scopes':summary[6] if summary else None,
        'bank_view':'raw_native_runtime_references_and_padding',
        'other_setters_inside_observed_agents':sum(scope[0]['other_requests'] for scope in scopes.values()),
        'early_coverage':False,'complete_generation_coverage':False,'native_faction_coverage':False,
        'pilot_ownership_proved':False,'control_writes':False,
    }
