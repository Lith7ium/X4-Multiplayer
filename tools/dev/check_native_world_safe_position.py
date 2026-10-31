"""Check passive production formatter output from owned memory, never a game."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import struct
import native_world_action as action
import native_world_values as values
import native_world_safe_position as passive


def check(path):
    text=path.read_text(encoding='ascii');errors=[]
    actions,coverage=action.evidence(text,[dict(id='0000000000080d54',version=4,name='move.idle')],errors)
    typed,_=values.evidence(text,actions,errors,parent_coverage=coverage)
    receipt=passive.evidence(text,actions,typed,errors)
    if errors or not receipt['coverage_complete']:raise ValueError(errors)
    result=passive.paired_diagnostics([dict(slot=i,evidence=receipt) for i in (1,2)],True,errors);json.dumps(result)
    if errors or not result['selected_valid']:raise ValueError(errors)
    semantic=receipt['events'][0]['semantic']
    if semantic['candidate_payload_equal_final_value']:raise ValueError('owned avoidance inequality was lost')
    e=receipt['events'][0]['decoded'];parents={a['event']['event']:a for a in actions};checked=0
    mutants=[]
    for group in ('outer','radius_guard'):
        for phase in (0,1):
            for field in passive.GQ+passive.GD:
                if field=='rng':continue
                changed=deepcopy(e);changed[group][phase][field]^=1;mutants.append(changed)
    for phase in (0,1):
        for field in passive.WITNESS:
            changed=deepcopy(e);changed['caller_witness'][phase][field]^=1;mutants.append(changed)
        for offset in (0,8,16,56,104,108,112,116,252,258,350,352,364,372):
            changed=deepcopy(e);raw=bytearray(changed['passed_reference'][phase]);raw[offset]^=1;changed['passed_reference'][phase]=bytes(raw);mutants.append(changed)
    for key in ('caller','descriptor','output','frame','optional','result','errors','abnormal','foreign','children','parent'):
        changed=deepcopy(e);changed[key]^=1;mutants.append(changed)
    for lower,upper in ((-4096,0),(0,4096),(-4096,4096)):
        changed=deepcopy(e);sample=changed['samplers'][0]
        sample['stack_limit']+=lower;sample['stack_base']+=upper;mutants.append(changed)
    for changed in mutants:
        try:passive.validate(changed,parents,typed)
        except (ValueError,KeyError,TypeError,struct.error):checked+=1
        else:raise ValueError('accepted mutated native passive guard')
    class_changes=0
    for field in ('vtable','rtti_name'):
        changed=deepcopy(e);raw=bytearray(changed['passed_reference'][0])
        if field=='vtable':struct.pack_into('<Q',raw,64,struct.unpack_from('<Q',raw,64)[0]+8)
        else:raw[130]^=1
        changed['passed_reference']=[bytes(raw)]*2
        changed_receipt=deepcopy(receipt);changed_receipt['events'][0]['semantic']=passive.validate(changed,parents,typed)
        refused=[];pair=passive.paired_diagnostics([dict(slot=1,evidence=receipt),dict(slot=2,evidence=changed_receipt)],True,refused)
        if not refused or not any(d['field']=='reference_class' for d in pair['differences']):raise ValueError('class metadata erased')
        class_changes+=1
    lines=text.splitlines();index=next(i for i,line in enumerate(lines) if 'NATIVE_GATE WORLD_SAFE_POSITION schema' in line);lines[index]+=' '*2100
    refused=[];bad=passive.evidence(lines,actions,typed,refused)
    if not refused or bad['selected_valid']:raise ValueError('raw padding bypass')
    return dict(owned_fixture_only=True,raw_bytes=passive.WIRE_SIZE,mutants_rejected=checked,coherent_class_differences=class_changes,
                candidate_final_inequality_accepted=True,avoidance_inputs_covered=False,native_execution=False,parser_pass=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('export',type=Path);args=p.parse_args();print(json.dumps(check(args.export)))
