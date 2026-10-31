"""Validate the owned C fixture's real formatter output, never a game log."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import struct
import native_world_action as action
import native_world_values as values
import native_world_position as position


def check(path):
    text=path.read_text(encoding='ascii');errors=[]
    actions,coverage=action.evidence(text,[dict(id='000000000009d3a4',version=10,name='order.fight.patrol')],errors)
    typed,_=values.evidence(text,actions,errors,parent_coverage=coverage)
    receipt=position.evidence(text,actions,typed,errors)
    if errors or not receipt['coverage_complete']:raise ValueError(errors)
    pairs=[dict(slot=i,evidence=receipt) for i in (1,2)]
    result=position.paired_diagnostics(pairs,True,errors);json.dumps(result)
    if errors or not result['selected_valid']:raise ValueError(errors)
    e=receipt['events'][0]['decoded'];parents={a['event']['event']:a for a in actions};checked=0
    for group in ('outer','transform_guard','radius_guard'):
        for phase in (0,1):
            for field in position.GQ+position.GD:
                # Space/reference expressions may consume RNG before transform;
                # that independently observed endpoint is not inferred equal.
                if group=='transform_guard' and phase==0 and field=='rng':continue
                changed=deepcopy(e);changed[group][phase][field]^=1
                try:position.validate(changed,parents,typed)
                except (ValueError,KeyError,TypeError):checked+=1
                else:raise ValueError('accepted mutated native guard '+group+str(phase)+field)
    for phase in (0,1):
        for part in (0,1):
            if not e['objects'][phase][part].strip(b'\0'):continue
            for offset in (0,8,16,56,104,108,112,116,252,258,350,352,364,372):
                changed=deepcopy(e);raw=bytearray(changed['objects'][phase][part]);raw[offset]^=1;changed['objects'][phase][part]=bytes(raw)
                try:position.validate(changed,parents,typed)
                except (ValueError,KeyError,TypeError,struct.error):checked+=1
                else:raise ValueError('accepted mutated native object')
    class_differences=0
    for part,name in ((0,'space'),(1,'reference')):
        if not e[name]:continue
        for field in ('vtable','rtti_name'):
            changed=deepcopy(e);raw=bytearray(changed['objects'][0][part])
            if field=='vtable':struct.pack_into('<Q',raw,64,struct.unpack_from('<Q',raw,64)[0]+8)
            else:raw[130]^=1
            changed['objects'][0][part]=changed['objects'][1][part]=bytes(raw)
            semantic=position.validate(changed,parents,typed)
            mutated=deepcopy(receipt);mutated['events'][0]['semantic']=semantic
            refused=[];pair=position.paired_diagnostics([dict(slot=1,evidence=receipt),dict(slot=2,evidence=mutated)],True,refused)
            if not refused or not pair['differences'] or name+'_class' not in pair['differences'][0]['fields']:
                raise ValueError('coherent class change was erased from paired semantics')
            class_differences+=1
    for delta in (-8,1):
        changed=deepcopy(e);changed['samplers'][0]['slot']+=delta
        try:position.validate(changed,parents,typed)
        except ValueError:checked+=1
        else:raise ValueError('accepted misaligned incoming return slot')
    lines=text.splitlines();index=next(i for i,line in enumerate(lines) if 'NATIVE_GATE WORLD_POSITION schema' in line);lines[index]+=' '*2100
    refused=[];padded=position.evidence(lines,actions,typed,refused)
    if not refused or padded['selected_valid']:raise ValueError('accepted overlong untrimmed row')
    return dict(owned_fixture_only=True,raw_bytes=position.WIRE_SIZE,mutants_rejected=checked,
                coherent_class_differences=class_differences,untrimmed_row_refused=True,
                native_execution=False,parser_pass=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('export',type=Path);args=p.parse_args()
    print(json.dumps(check(args.export)))
