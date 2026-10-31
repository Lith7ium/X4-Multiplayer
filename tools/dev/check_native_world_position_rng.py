"""Check real C Position/RNG exports against declared prerequisite fixtures.

This does not execute the game or claim an end-to-end Find/import/Action run.
Those prerequsites have separate production tests; their copied completed
metadata are modeled here to test the new raw wire and exact bridge joins.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import struct
import native_world_position as p
import native_world_position_rng as r


def check(path):
    log=path.read_text(encoding='ascii');wire={}
    for line in log.splitlines():
        if line.startswith('NATIVE_GATE WORLD_POSITION schema=2 '):
            fields=dict(w.split('=',1) for w in line.split()[2:]);wire[int(fields['part'])]=bytes.fromhex(fields['data'])
    e=p.decode(b''.join(wire[i] for i in range(21)));g=e['outer'][0];seed1=r.rng_step(r.SEED)
    require=p.require
    require(e['samplers'][0]['result']==0x46c881ce and e['samplers'][0]['guards'][1]['rng']==seed1,'C sampler result')
    require(e['outer'][1]['rng']==r.rng_step(r.rng_step(seed1)),'C direction endpoint')
    q={k:g[k] for k in ('current','context','lifetime','entity','runtime','script','program','begin','end','node','vtable','target','table','tls_time','game','real','tag','version','chance')}
    q.update(exec=g['current'],frame=e['frame'],id=0x9d3a4,name='order.fight.patrol',slot=215,program_hash=0x985d66488a43c795,
             saved_time=g['saved'],app_flags=g['app'],vbi_loading=g['loading'])
    parent=dict(event=dict(event=e['action'],errors=0,stage=2,type=0x2c0cdd0,action=0xbe0ba0,thread=e['thread'],slot=215,id=0x9d3a4,
                          name='order.fight.patrol',version=10,parent=0,depth=1,boot=2,tick=0,rng_before=g['rng'],rng_after=123),
                before=q,after=deepcopy(q),validation_errors=[])
    values=[dict(event=dict(event=1,action=e['action'],kind=0,errors=0,caller=0xbe0c8b,rng_before=123,rng_after=123),
                 value=dict(tag=0x63,kind=2,payload=e['output_bytes']),validation_errors=[])]
    rows={}
    for line in log.splitlines():
        if line.startswith('NATIVE_GATE WORLD_POSITION_RNG schema=1 '):
            fields=dict(w.split('=',1) for w in line.split()[2:]);rows[int(fields['part'])]=bytes.fromhex(fields['data'])
    receipt=r.decode(b''.join(rows[i] for i in range(3)));identity=receipt['identity'][0]
    require(e['action']==2,'owned C fixture must leave a real prior Action ordinal')
    find=dict(selected_scope_valid=True,composition=receipt['composition'],events={1:{0:dict(candidate=receipt['find_candidate'],thread=e['thread'],action=1),
        **{i:dict(identity,event=1,part=i) for i in (1,2,3)}}})
    waits=[dict(input_output=dict(id=0x9d3a4,name='order.fight.patrol'),raw=[dict(event=receipt['anchor_wait'])],validation_errors=[])]
    errors=[];prepared=r.prepare(log,r.selection(receipt['composition']),find,waits,errors)
    positions=p.evidence(log,[parent],values,errors,intervention=prepared);result=r.finalize(prepared,positions,errors)
    require(not errors and result['selected_scope_valid'],str(errors));mutants=0
    for key,value in (('action',0),('action',2),('action',3),('thread',e['thread']+1),('action',None),('thread',None)):
        changed=deepcopy(prepared)
        if value is None:del changed['find']['events'][1][0][key]
        else:changed['find']['events'][1][0][key]=value
        try:p.validate(e,{e['action']:parent},values,changed)
        except (ValueError,KeyError,TypeError):mutants+=1
        else:raise ValueError('accepted wrong prior C fixture Find '+key)
    for key in r.META+r.QUADS:
        if key in ('schema','event','reads','bytes'):continue
        changed=deepcopy(prepared);changed['events'][1][key]^=1
        try:p.validate(e,{e['action']:parent},values,changed)
        except (ValueError,KeyError,TypeError):mutants+=1
        else:raise ValueError('accepted changed C receipt '+key)
    for phase in (0,1):
        for key in r.IQ+r.ID:
            changed=deepcopy(prepared);changed['events'][1]['identity'][phase][key]^=1
            try:p.validate(e,{e['action']:parent},values,changed)
            except (ValueError,KeyError,TypeError):mutants+=1
            else:raise ValueError('accepted changed C identity '+key)
    for phase in (0,1):
        for key in p.GQ+p.GD:
            changed=deepcopy(e);changed['outer'][phase][key]^=1
            try:p.validate(changed,{e['action']:parent},values,prepared)
            except (ValueError,KeyError,TypeError):mutants+=1
            else:raise ValueError('accepted changed C outer guard '+key)
    raw_stop=next(line for line in log.splitlines() if line.startswith('NATIVE_GATE WORLD_POSITION_STOP '))
    changed_logs=[log.replace(raw_stop,'')]
    for key,value in (('open',1),('failures',1),('patch_failures',1),('exported',0)):
        fields=dict(word.split('=',1) for word in raw_stop.split()[2:]);fields[key]=str(value)
        changed_logs.append(log.replace(raw_stop,'NATIVE_GATE WORLD_POSITION_STOP '+' '.join(f'{k}={v}' for k,v in fields.items())))
    changed_logs.extend(log+'\n'+extra+'\n' for extra in ('NATIVE_GATE WORLD_POSITION_BOGUS schema=2 event=1',
                         'NATIVE_GATE WORLD_POSITION','NATIVE_GATE WORLD_POSITION_STOP'))
    for changed_log in changed_logs:
        rng_errors=[];position_errors=[]
        bad_capture=r.prepare(changed_log,r.selection(receipt['composition']),find,waits,rng_errors)
        bad_positions=p.evidence(changed_log,[parent],values,position_errors,intervention=bad_capture)
        bad=r.finalize(bad_capture,bad_positions,rng_errors)
        require(bad_positions['events'][0]['supported'] and not bad_positions['selected_valid'] and position_errors,'C selected stream mutant')
        require(not bad['selected_scope_valid'] and rng_errors,'C failed stream promoted to scoped success')
        claimed=[];pair=r.paired_diagnostics([dict(slot=i,evidence=bad,errors=[]) for i in (1,2)],True,claimed)
        require(not pair['selected_scope_valid'] and not pair['outputs_equal'] and claimed,'C failed stream promoted to paired success')
        mutants+=1
    return dict(owned_fixture_only=True,native_engine_execution=False,modeled_completed_parent_value_find_wait_dependencies=True,
                actual_position_rng_wrapper_export=True,wire_bytes=576,position_bytes=5320,mutants_rejected=mutants,parser_pass=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('export',type=Path);args=parser.parse_args();print(json.dumps(check(args.export)))
