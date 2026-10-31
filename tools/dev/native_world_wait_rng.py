"""Bounded saved-Wait RNG experiment manifest and independent evidence checks.

Intervention restores the caller's pre-call stream; explicit schema2 observation
retains natural advancement. Neither synchronizes ambient RNG nor proves
exception cleanup.
"""
import json
import re
from native_gate_wait_manifest import selected_time_bits

DOMAIN=0x574149545f535631
MASK=(1<<64)-1
FIELDS=set('event selected configured applied refusal restored domain key_schema script_hash time_hash seed caller_before scoped_before scoped_after caller_restored expected_slot expected_pc expected_since'.split())
HEX=set('domain script_hash time_hash seed caller_before scoped_before scoped_after caller_restored natural_after'.split())
STOP=set('schema selected rows applied refused open restore_open closed global_sync exception_cleanup'.split())
PREFIX='WAIT_POLICY_2'


def policies(manifest):
    return ['intervene' if manifest['schema']==1 else entry['policy'] for entry in manifest['entries']]


def fnv(text):
    value=14695981039346656037
    for byte in text.encode('ascii'):
        value=((value^byte)*1099511628211)&MASK
    return value


def mix(value):
    value=((value^(value>>30))*0xbf58476d1ce4e5b9)&MASK
    value=((value^(value>>27))*0x94d049bb133111eb)&MASK
    return value^(value>>31)


def seed(entry,snapshot):
    from native_gate_wait_manifest import double_bits
    if entry.get('policy')=='observe':raise ValueError('observe policy has no RNG seed')
    # Each field is a distinct step. Native slot/PC/runtime/thread/order absent.
    fields=[entry['version'],int(entry['id'],16),fnv(entry['name']),entry['index'],fnv(entry['time_text']),
            int(double_bits(entry['minimum']),16),int(double_bits(entry['maximum']),16)]
    fields.extend(int(snapshot[i:i+16],16) for i in range(0,64,16))
    value=mix(0x58344c5345454432^DOMAIN)
    for field in fields:value=mix(value^field)
    return value


def manifest_wire(manifest,observed):
    if (set(manifest)!={'schema','snapshot_sha256','entries'} or type(manifest['schema']) is not int or manifest['schema'] not in (1,2) or
        observed.get('schema')!=2 or manifest['snapshot_sha256']!=observed.get('snapshot_sha256') or
        not re.fullmatch('[0-9a-f]{64}',manifest['snapshot_sha256'])):
        raise ValueError('Wait RNG experiment requires the same schema2 snapshot-bound observation')
    entries=manifest['entries'];expected=observed['entries'];schema=manifest['schema']
    if not isinstance(entries,list) or not 1<=len(entries)<=8 or len(entries)!=len(expected):raise ValueError('Wait RNG selection count mismatch')
    parts=([PREFIX] if schema==2 else [])+[manifest['snapshot_sha256']]
    for index,(entry,base) in enumerate(zip(entries,expected),1):
        policy='intervene' if schema==1 else entry.get('policy')
        if policy not in ('intervene','observe'):raise ValueError('unknown Wait policy')
        extra=({'policy'} if schema==2 else set())|({'slot','pc','since'} if policy=='intervene' else set())
        if set(entry)!=set(base)|extra or any(entry[k]!=v for k,v in base.items()):
            raise ValueError('Wait RNG selection differs from exact observed manifest')
        selected_time_bits(entry)
        fields=[str(index)]+([policy] if schema==2 else [])
        if policy=='intervene':
            for field,limit in (('slot',4095),('pc',1048575),('since',entry['version'])):
                if type(entry[field]) is not int or not 0<=entry[field]<=limit:raise ValueError('invalid Wait RNG native guard')
                fields.append(str(entry[field]))
        parts.append(','.join(fields+[entry['time_text']]))
    wire=';'.join(parts)
    if len(wire)>=4096:raise ValueError('Wait RNG manifest bound')
    return wire


def configured(config,observed):
    from native_gate_wait_manifest import windows_saved_time_bits,double_bits
    matches=re.findall(r'^X4LAN_CONFIG\.native_gate_world_wait_rng_manifest\s*=\s*("(?:\\.|[^"\\])*")\s*$',config,re.M)
    if len(matches)!=1:raise ValueError('missing or duplicated Wait RNG config')
    wire=json.loads(matches[0]);parts=wire.split(';');schema=2 if parts[0]==PREFIX else 1
    if schema==2:parts=parts[1:]
    if not 0<len(wire)<4096 or not parts or not re.fullmatch('[0-9a-f]{64}',parts[0]) or len(parts)!=len(observed)+1:raise ValueError('Wait RNG wire schema')
    entries=[]
    for index,(part,base) in enumerate(zip(parts[1:],observed),1):
        fields=part.split(',');policy='intervene'
        if schema==2:
            if len(fields)<3 or fields[1] not in ('intervene','observe'):raise ValueError('unknown Wait policy')
            policy=fields.pop(1)
        if len(fields)!=(5 if policy=='intervene' else 2) or any(not re.fullmatch('[0-9]+',v) for v in fields[:-1]) or int(fields[0])!=index:
            raise ValueError('Wait RNG guard encoding')
        text=fields[-1]
        if windows_saved_time_bits(text)!=double_bits(base['time']):raise ValueError('Wait RNG observation binding')
        entry=dict(base);del entry['time'];entry['time_text']=text
        if schema==2:entry['policy']=policy
        if policy=='intervene':
            slot,pc,since=map(int,fields[1:4])
            if slot>=4096 or pc>=1048576 or since>base['version']:raise ValueError('Wait RNG observation binding')
            entry.update(slot=slot,pc=pc,since=since)
        entries.append(entry)
    snapshots=re.findall(r'^X4LAN_CONFIG\.native_gate_snapshot_sha\s*=\s*"([0-9a-f]{64})"\s*$',config,re.M)
    if snapshots!=[parts[0]]:raise ValueError('Wait RNG configured snapshot mismatch')
    return {'schema':schema,'snapshot_sha256':parts[0],'entries':entries}


def evidence(log,manifest,wait_events,errors,label='Wait RNG'):
    from native_world_wait import rng_step
    schema=manifest['schema'];selected_policies=policies(manifest)
    event_fields=FIELDS|({'policy','natural_after'} if schema==2 else set())
    stop_fields=STOP|({'observed'} if schema==2 else set())
    rows={};stop=None
    for line in log.splitlines():
        kind=None
        if 'NATIVE_GATE WORLD_WAIT_RNG ' in line:kind='event';raw=line.split('NATIVE_GATE WORLD_WAIT_RNG ',1)[1]
        elif 'NATIVE_GATE WORLD_WAIT_RNG_STOP ' in line:kind='stop';raw=line.split('NATIVE_GATE WORLD_WAIT_RNG_STOP ',1)[1]
        if not kind:continue
        try:
            pairs=[part.split('=',1) for part in raw.split()];values=dict(pairs)
            if len(values)!=len(pairs) or set(values)!=(event_fields if kind=='event' else stop_fields):raise ValueError('row schema')
            if any(not re.fullmatch('[0-9a-f]{16}' if key in HEX else '[0-9]+',value) for key,value in values.items()):raise ValueError('integer syntax')
            record={key:int(value,16 if key in HEX else 10) for key,value in values.items()}
            if any(not 0<=v<=MASK for v in record.values()):raise ValueError('integer bounds')
            if kind=='stop':
                if stop is not None:raise ValueError('duplicate STOP')
                stop=record
            else:
                if stop is not None or record['event']!=len(rows)+1 or len(rows)>=128:raise ValueError('row order')
                rows[record['event']]=record
        except (TypeError,ValueError):errors.append(f'{label}: malformed record')
    expected=manifest['entries'];count=len(expected);samples=[]
    applied=selected_policies.count('intervene');observed=count-applied
    if (stop is None or stop.get('schema')!=schema or any(stop.get(k)!=count for k in ('selected','rows')) or stop.get('applied')!=applied or
        (schema==2 and stop.get('observed')!=observed) or
        any(stop.get(k)!=0 for k in ('refused','open','restore_open','global_sync','exception_cleanup')) or stop.get('closed')!=1 or
        len(rows)!=count or len(wait_events)!=count):errors.append(f'{label}: incomplete or failed coverage')
    seen=set()
    for raw in wait_events:
        event,before,after,final=raw['raw'];row=rows.get(event['event']);selected=event['selected']
        if not row or not 1<=selected<=count or selected in seen:errors.append(f'{label}: missing or duplicate selection');continue
        seen.add(selected);entry=expected[selected-1];policy=selected_policies[selected-1]
        bad=(event['errors'] or row['selected']!=selected or row['configured']!=1 or row['refusal'] or
             row['script_hash']!=fnv(entry['name']) or row['time_hash']!=fnv(entry['time_text']) or row['caller_before']!=event['rng_before'])
        if schema==2:bad=bad or row['policy']!=(1 if policy=='intervene' else 2)
        sample={'id':entry['id'],'name':entry['name'],'due_before':event['due_before'],
                'due_after':event['due_after'],'reason_after':event['reason_after']}
        if policy=='intervene':
            key=seed(entry,manifest['snapshot_sha256'])
            bad=bad or (row['applied']!=1 or row['restored']!=1 or row['domain']!=DOMAIN or row['key_schema']!=1 or
                row['seed']!=key or row['scoped_before']!=key or row['scoped_after']!=rng_step(key) or
                row['caller_restored']!=row['caller_before'] or row['caller_restored']!=event['rng_after'] or
                any(row['expected_'+k]!=entry[k] or event[k]!=entry[k] for k in ('slot','pc','since')) or
                event['boot']!=2 or event['tick'] or event['ui']!=1 or before['main'] or before['view']!=1 or before['app_flags']!=0x26 or before['vbi_loading']!=1 or
                not before['tls_time']==before['game']==before['saved_time'] or
                any(before[k]!=after[k] for k in ('main','view','app_flags','vbi_loading','tls_time','game','saved_time')) or
                any(p['phase_status']!=1 or p['eligible']!=1 or p['elapsed']>120000 for p in (before,after)) or
                (schema==2 and row['natural_after']!=0))
            sample.update(seed=key,scoped_after=row['scoped_after'])
        else:
            # No synthetic slot/PC/since expectation or seed is invented for an
            # unobserved owner. Its actual native fields remain in the raw trace.
            bad=bad or (any(row[k] for k in ('applied','restored','domain','key_schema','seed','scoped_before','scoped_after',
                                            'caller_restored','expected_slot','expected_pc','expected_since')) or
                row['natural_after']!=event['rng_after'] or rng_step(event['rng_before'])!=event['rng_after'] or
                raw['input_output']['rng_interpretation']!='natural' or raw['input_output']['single_rng_step_matches'] is not True)
            sample.update(caller_before=row['caller_before'],natural_after=row['natural_after'])
        if schema==2:sample['policy']=policy
        if bad:errors.append(f'{label}: event {event["event"]} failed {policy} proof')
        samples.append(sample)
    return {'stop':stop,'samples':samples,'global_rng_synchronized':False,'exception_cleanup':False}


def paired_diagnostics(captures,claimed_pass,errors):
    if not captures:return None
    ordered=sorted(captures,key=lambda c:c['slot'])
    paired=len(ordered)==2 and {c['slot'] for c in ordered}=={1,2}
    same=paired and ordered[0]['manifest']==ordered[1]['manifest']
    if not same:errors.append('saved Wait RNG experiment configuration missing or different between players')
    valid=same and not any(c['errors'] for c in ordered)
    differences=[]
    if valid:
        left={(s['id'],s['name']):s for s in ordered[0]['evidence']['samples']}
        right={(s['id'],s['name']):s for s in ordered[1]['evidence']['samples']}
        for key in sorted(left.keys()|right.keys()):
            if key not in left or key not in right:differences.append({'id':key[0],'name':key[1],'field':'coverage'})
            else:
                for field in left[key]:
                    if left[key][field]!=right[key][field]:differences.append({'id':key[0],'name':key[1],'field':field,'left':left[key][field],'right':right[key][field]})
    equal=not differences if valid else None
    per_policy={}
    if same and ordered[0]['manifest'].get('schema')==2:
        entries=ordered[0]['manifest']['entries']
        for policy in ('intervene','observe'):
            keys={(e['id'],e['name']) for e in entries if e['policy']==policy}
            per_policy[policy+'_outputs_equal']=(not any((d['id'],d['name']) in keys for d in differences)) if valid and keys else None
    if claimed_pass and equal is False:errors.append('selected saved Wait RNG experiment outputs differ between players')
    return {'capture_valid':valid,'selected_outputs_equal':equal,'differences':differences,
            'global_rng_synchronized':False,'exception_cleanup':False,'clients':ordered,**per_policy}
