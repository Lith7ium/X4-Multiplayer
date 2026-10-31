"""Snapshot-bound, read-only Small-ship transition watches (schema2)."""
import hashlib
import re


def decode_wire(wire):
    if not isinstance(wire,str) or not wire.startswith('2|') or len(wire)>=320:
        raise ValueError('invalid transition manifest schema/length')
    result=[]
    for part in wire[2:].split(';'):
        match=re.fullmatch(r'([0-9a-f]{16})/([pn])/([a-z0-9_]{1,63})/([a-z0-9_]{1,63})',part)
        if not match:raise ValueError('invalid transition watch fields')
        saved,kind,macro,owner=match.groups()
        if not int(saved,16) or (result and saved<=result[-1]['saved']):raise ValueError('transition watches must be unique and sorted')
        result.append(dict(saved=saved,controller='player' if kind=='p' else 'navigation',macro=macro,owner=owner))
    if not 1<=len(result)<=2:raise ValueError('transition watch capacity exceeded')
    return result


def manifest(data,snapshot,starts):
    if not isinstance(data,dict) or set(data)!={'schema','snapshot_sha256','last_tick','entries'} or type(data['schema']) is not int or data['schema']!=2:
        raise ValueError('invalid transition manifest')
    last=data['last_tick']
    if type(last) is not int or not 1<=last<=120:raise ValueError('transition last_tick must be1..120')
    with snapshot.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    if data['snapshot_sha256']!=digest:raise ValueError('transition snapshot checksum mismatch')
    entries=data['entries']
    if not isinstance(entries,list) or not 1<=len(entries)<=2:raise ValueError('transition requires1..2 entries')
    if any(not isinstance(row,dict) or set(row)!={'saved','controller','macro','owner'} for row in entries):
        raise ValueError('invalid transition watch entry')
    try:
        wire='2|'+';'.join('/'.join((row['saved'],{'player':'p','navigation':'n'}[row['controller']],row['macro'],row['owner'])) for row in entries)
    except (KeyError,TypeError):raise ValueError('invalid transition watch value') from None
    expected=decode_wire(wire)
    counts={row['saved']:[0,0] for row in expected};by_id={int(row['saved'],16):row for row in expected}
    def selected(element):
        value=element.get('id','')
        return by_id.get(int(value[3:-1],16)) if element.tag=='component' and re.fullmatch(r'\[0x[0-9a-f]{1,16}\]',value) else None
    for nodes in starts(snapshot):
        element=nodes[-1];row=selected(element)
        if row:
            if element.get('class')!='ship_s' or element.get('macro')!=row['macro'] or element.get('owner')!=row['owner']:
                raise ValueError('transition saved ship identity mismatch')
            counts[row['saved']][0]+=1
        if len(nodes)>1 and element.tag=='movement':
            row=selected(nodes[-2])
            if row:
                if element.get('class')!=row['controller'] or (row['controller']=='player' and element.get('player')!='1'):
                    raise ValueError('transition saved controller mismatch')
                counts[row['saved']][1]+=1
    if any(value!=[1,1] for value in counts.values()):raise ValueError('transition saved identities/controllers missing or duplicated')
    with snapshot.open('rb') as stream:after=hashlib.file_digest(stream,'sha256').hexdigest()
    if after!=digest:raise ValueError('transition snapshot changed during validation')
    return wire,last


def configured(config):
    wires=re.findall(r'native_gate_player_transition_manifest\s*=\s*"([^"\r\n]*)"',config)
    limits=re.findall(r'native_gate_player_transition_last_tick\s*=\s*(\d+)\b',config)
    if not wires and not limits:return None
    if len(wires)!=1 or len(limits)!=1 or not 1<=int(limits[0])<=120:raise ValueError('invalid configured transition watch manifest')
    return decode_wire(wires[0]),int(limits[0])
