"""Interpret an observed solver mismatch without changing strict validation.

The constructor at d47ac0 leaves four alignment ranges unwritten. The inspected
preparation/core path uses adjacent typed fields. We report these bytes separately
as constructor-unwritten, not as proved irrelevant in every possible native path.
Raw differences remain failures in native_solver_evidence and the session report.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

UNWRITTEN_HEADER_RANGES = ((0x25,0x28),(0x35,0x38),(0x3c,0x40),(0x5e,0x60))
CONTEXT_BLOCKS = {'header':(0,0x78),'linear_velocity_requests':(0x78,0xa8),
                  'angular_velocity_requests':(0xa8,0xd8),'limits':(0xd8,504)}
MOTION_BLOCKS = {'acceleration':(0,32),'local_velocity_buffer':(32,64),
                 'pose':(64,128),'velocity':(128,160)}


def records(log):
    result={}
    for text in re.findall(r'NATIVE_GATE PLAYER_SOLVER_EVENT ([^\r\n]*)',log):
        row=dict(part.split('=',1) for part in text.split())
        key=tuple(int(row[k]) for k in ('tick','actor','kind','ordinal','solve_tick','solve_ordinal'))
        if key in result:raise ValueError('duplicate solver identity')
        result[key]=row
    return result


def compare(a,b):
    counts=Counter();raw=[];outside=[];gap_values=defaultdict(set)
    named={f'{phase}.{name}':0 for phase,blocks in (('c0',CONTEXT_BLOCKS),('c1',CONTEXT_BLOCKS),('m0',MOTION_BLOCKS),('m1',MOTION_BLOCKS)) for name in blocks}
    for key in sorted(a.keys()|b.keys()):
        if key not in a or key not in b:
            raw.append({'key':key,'field':'record','missing_client':1 if key not in a else 2});outside.append(raw[-1]);continue
        for field in sorted(a[key].keys()|b[key].keys()):
            if field=='index' or a[key].get(field)==b[key].get(field):continue
            difference={'key':key,'field':field};counts[field]+=1
            if field in ('c0','c1','m0','m1','source','configuration','published'):
                left=bytes.fromhex(a[key][field]);right=bytes.fromhex(b[key][field])
                if len(left)!=len(right):raise ValueError('different native block lengths')
                offsets=[i for i,(x,y) in enumerate(zip(left,right)) if x!=y]
                blocks=CONTEXT_BLOCKS if field in ('c0','c1') else MOTION_BLOCKS if field in ('m0','m1') else {}
                for name,(start,end) in blocks.items():
                    if any(start<=i<end for i in offsets):named[f'{field}.{name}']+=1
                difference['bytes']=[{'offset':hex(i),'client1':f'{left[i]:02x}','client2':f'{right[i]:02x}'} for i in offsets]
                unwritten=(field in ('c0','c1') and all(any(first<=i<last for first,last in UNWRITTEN_HEADER_RANGES) for i in offsets))
                difference['only_constructor_unwritten_header_bytes']=unwritten
                if unwritten:
                    for i in offsets:gap_values[key[1],i].add((left[i],right[i]))
                else:outside.append(difference)
            else:
                difference['client1']=a[key].get(field);difference['client2']=b[key].get(field);outside.append(difference)
            raw.append(difference)
    return {'raw_equal':not raw,'different_records_or_blocks':len(raw),'different_blocks':dict(counts),
            'different_named_blocks':named,'local_velocity_before_is_output_buffer_preimage':True,
            'first_raw_difference':raw[0] if raw else None,
            'differences_outside_constructor_unwritten_ranges':outside,
            'observed_constructor_unwritten_values':[{'actor':actor,'offset':hex(offset),'values':sorted(values)}
                for (actor,offset),values in sorted(gap_values.items())],
            'unwritten_ranges_are_not_a_session_acceptance_rule':True,
            'raw_differences':raw}


def client_summary(rows):
    owners={};solves={};publication_matches=Counter();delays=Counter();counts=Counter();contexts={};prepost=Counter();gap_changes=0
    for key,row in sorted(rows.items()):
        tick,actor,kind,ordinal,solve_tick,solve_ordinal=key;counts[kind]+=1
        owner=owners.setdefault(actor,{'saved':row['owner'],'first_tick':tick,'last_tick':tick,'solves':0,'publications':0})
        owner['last_tick']=tick
        if kind==1:
            owner['solves']+=1;solves[actor,tick,ordinal]=row
            for field,(start,end) in CONTEXT_BLOCKS.items():
                if row['c0'][start*2:end*2]!=row['c1'][start*2:end*2]:prepost[field]+=1
            for first,last in UNWRITTEN_HEADER_RANGES:
                contexts.setdefault((actor,first,last),set()).add(row['c0'][first*2:last*2])
                gap_changes+=row['c0'][first*2:last*2]!=row['c1'][first*2:last*2]
        else:
            owner['publications']+=1;delays[tick-solve_tick]+=1
            solved=solves.get((actor,solve_tick,solve_ordinal))
            if solved and row['published']==solved['m1'][128:320]:publication_matches[actor]+=1
    return {'records':len(rows),'solves':counts[1],'publications':counts[2],
            'owners':owners,'matching_publications_by_actor':dict(publication_matches),
            'publication_tick_delays':dict(delays),'context_changes_during_original':dict(prepost),
            'unwritten_ranges_constant_within_owner':all(len(values)==1 for values in contexts.values()),
            'unwritten_range_changes_during_original':gap_changes,
            'all_native_players_independent':False}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);args=p.parse_args()
    clients=[]
    for i in (1,2):
        path=args.run/f'client{i}/x4lan-debug.txt';size=path.stat().st_size
        with path.open('rb') as stream:log=stream.read(size).decode(errors='replace')
        clients.append(records(log))
    result={'schema':1,'diagnostic_only':True,'strict_validation_unchanged':True,
            'clients':[client_summary(c) for c in clients],'comparison':compare(*clients)}
    output=args.run/'solver-analysis.json';output.write_text(json.dumps(result,indent=2)+'\n')
    brief={**result,'comparison':{k:v for k,v in result['comparison'].items() if k!='raw_differences'}}
    print(json.dumps(brief,indent=2))


if __name__=='__main__':main()
