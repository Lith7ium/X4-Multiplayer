"""Locate the first differing imported object/domain in two bounded log views.

An N-client run (client1..clientN, N > 2) compares client1 with every other client."""
import argparse
import json
import re
from pathlib import Path
from native_world_evidence import DOMAINS,frames,decode_detail,configured_watches,configured_schema,identity_origin
from census_clients import client_slots,against_reference


def bounded_lines(path):
    limit=path.stat().st_size
    with path.open('rb') as source:
        while source.tell()<limit:yield source.readline().decode('utf-8','replace')


def compare(run,left=1,right=2):
    errors=[]
    streams=[]
    for slot in (left,right):
        client=run/f'client{slot}';config=client/'installed/x4_lan_lab-config.lua'
        config_text=config.read_text() if config.is_file() else ''
        watches=configured_watches(config_text) if config.is_file() else None
        timing=bool(re.search(r'native_gate_world_timing_fields\s*=\s*true',config_text))
        try:schema=configured_schema(config_text)
        except ValueError as error:errors.append(f'player {slot} world hash configuration: {error}');schema='WORLD_IMPORTED_1'
        streams.append(iter(frames(bounded_lines(client/'engine-output.log'),errors,slot,watches,timing,schema)))
    compared=0
    while True:
        a,b=(next(stream,None) for stream in streams)
        if a is None or b is None:
            if (a is None)!=(b is None):errors.append('world snapshot count differs between clients')
            return {'compared_snapshots':compared,'first_difference':None,'errors':errors}
        compared+=1
        if a['tick']!=b['tick']:return {'compared_snapshots':compared,'first_difference':'snapshot cadence','errors':errors}
        if a['schema']!=b['schema']:return {'compared_snapshots':compared,'first_difference':'world hash schema','errors':errors}
        if a['roots']!=b['roots']:
            differences=[];counts={name:0 for name in DOMAINS};first_by_domain={name:[] for name in DOMAINS};inventory=0
            timing_counts={};timing_examples=[]
            for identity in sorted(a['objects'].keys()|b['objects'].keys()):
                x,y=a['objects'].get(identity),b['objects'].get(identity)
                changed=['inventory'] if x is None or y is None else [name for name,h1,h2 in zip(DOMAINS,x['hashes'],y['hashes']) if h1!=h2]
                for name in changed:
                    if name=='inventory':inventory+=1
                    else:
                        counts[name]+=1
                        if len(first_by_domain[name])<8:first_by_domain[name].append(f'{identity:016x}')
                if changed and len(differences)<32:
                    differences.append({'saved_id':f'{identity:016x}','origin':identity_origin(identity),'domains':changed,
                        'clients':[{k:v for k,v in row.items() if k not in ('hashes','timing_data')} if row else None for row in (x,y)]})
                if 'timing' in changed and x.get('timing_data') is not None and y.get('timing_data') is not None:
                    left,right=(decode_detail({'timing':row['timing_data']},a['schema'])['timing'] for row in (x,y))
                    fields={key:[left.get(key),right.get(key)] for key in left.keys()|right.keys() if left.get(key)!=right.get(key)}
                    for key in fields:timing_counts[key]=timing_counts.get(key,0)+1
                    if len(timing_examples)<16:timing_examples.append({'saved_id':f'{identity:016x}','fields':fields})
            detail_differences=[]
            for identity in sorted(a['details'].keys()|b['details'].keys()):
                if identity not in a['details'] or identity not in b['details']:
                    errors.append(f'world detail watch set differs for {identity:x}');continue
                x,y=decode_detail(a['details'][identity],a['schema']),decode_detail(b['details'][identity],b['schema'])
                changed={domain:{field:[x[domain].get(field),y[domain].get(field)] for field in x[domain].keys()|y[domain].keys() if x[domain].get(field)!=y[domain].get(field)} for domain in DOMAINS}
                changed={domain:fields for domain,fields in changed.items() if fields}
                if changed:detail_differences.append({'saved_id':f'{identity:016x}','fields':changed})
            return {'compared_snapshots':compared,'first_difference':a['tick'],'domain_object_counts':counts,'first_by_domain':first_by_domain,
                    'inventory_difference_count':inventory,'objects':differences,'detail_differences':detail_differences,
                    'timing_field_counts':timing_counts,'timing_examples':timing_examples,'coverage':[a['coverage'],b['coverage']],'errors':errors}


def compare_clients(run):
    """Two clients: compare(run). N > 2: client1 against every other client."""
    slots=client_slots(run)
    if len(slots)==2:return compare(run)
    result=against_reference(slots,lambda k:compare(run,1,k))
    result['first_difference_by_client']={name:pair['first_difference'] for name,pair in result['pairs'].items()}
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('run',type=Path);parser.add_argument('--out',type=Path)
    args=parser.parse_args();result=compare_clients(args.run)
    if args.out:args.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
