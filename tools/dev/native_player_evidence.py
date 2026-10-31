"""Validate declared singleton context, admitted actions, and native state traces.

This is evidence for a singleton control diagnostic, never evidence that two
independent native player contexts exist or that all cockpit inputs are captured.
"""
import math
import re
import struct
import hashlib


def fire_evidence(log,slot,context,last_tick,errors):
    """Physical player projectile attribution; absence of damage is not success."""
    seeded='PLAYER_LAUNCH_ENABLED' in log
    if re.findall(r'NATIVE_GATE PLAYER_FIRE_ENABLED tick=(\d+) native_trigger=(\d+) rng_changes=(\d+)',log)!=[('1','1',str(int(seeded)))]:
        errors.append(f'player {slot} native firing declaration missing')
    pattern=(r'NATIVE_GATE PLAYER_FIRE_EVENT index=(\d+) tick=(\d+) kind=(\d+) ship=([0-9a-f]{16}) gun=([0-9a-f]{16}) '
             r'spawn=(\d+) target=([0-9a-f]{16}) call=(\d+) amount=([0-9a-f]{16}) absorbed=([0-9a-f]{16}) '
             r'before=([0-9a-f]{16}) after=([0-9a-f]{16}) hits_before=(-?\d+) hits_after=(-?\d+)')
    complete_moves=bool(re.search(r'native_trigger=1 rng_changes=[01] trace=2',log))
    records=[];created={};moved=set();damage=0;damage_effects=0;guns=set();ordinals={};moves={}
    for row in re.findall(pattern,log):
        index,tick,kind=map(int,row[:3]);ship,gun=row[3:5];spawn=int(row[5]);target=row[6];call=int(row[7])
        values=[struct.unpack('>d',bytes.fromhex(raw))[0] for raw in row[8:12]]
        hits0,hits1=map(int,row[12:]);key=(gun,spawn)
        valid=(index==len(records)+1 and 1<=tick<=last_tick and context and ship==context[3] and
               gun!='0'*16 and spawn>0 and kind in (1,2,3,4) and all(map(math.isfinite,values)))
        if kind==1:
            valid=valid and key not in created and spawn==ordinals.get(gun,0)+1 and call==0
            created[key]=tick;guns.add(gun);ordinals[gun]=spawn
        else:
            valid=valid and key in created and tick>=created.get(key,tick+1) and call>0
            if kind==2:
                valid=valid and call==moves.get(key,0)+1;moved.add(key);moves[key]=call
            elif kind==3:valid=valid and key in moved and hits0!=hits1
            elif kind==4:
                valid=valid and key in moved and target!='0'*16 and values[0]>=0 and values[1]>=0
                damage+=1;damage_effects+=int(values[1]>0 and values[3]<values[2])
        if kind!=4:valid=valid and target=='0'*16 and (values[:2]==[0]*2 if kind==2 and complete_moves else values==[0]*4)
        if kind not in ((2,3) if complete_moves else (3,)):valid=valid and hits0==hits1==0
        if not valid:errors.append(f'player {slot} invalid player projectile attribution {index}')
        records.append(row)
    if log.count('NATIVE_GATE PLAYER_FIRE_EVENT ')!=len(records):errors.append(f'player {slot} malformed player projectile event')
    stops=re.findall(r'NATIVE_GATE PLAYER_FIRE_STOP guns=(\d+) created=(\d+) moves=(\d+) damage=(\d+) events=(\d+) failures=(\d+) held=(\d+) cleanup=(\d+) rng_changes=(\d+) emitted=(\d+)',log)
    summary=tuple(map(int,stops[0])) if len(stops)==1 else None
    if (not summary or summary[0]<len(guns) or summary[1]!=len(created) or summary[2]<len(moved) or summary[3]!=damage or
        summary[4]!=len(records) or summary[5:7]!=(0,0) or summary[8]!=int(seeded) or summary[9]!=len(records)):
        errors.append(f'player {slot} player firing trace incomplete or trigger not released')
    if complete_moves and summary and summary[2]!=sum(moves.values()):errors.append(f'player {slot} player Move trace incomplete')
    evaluations=re.findall(r'NATIVE_GATE PLAYER_OBSERVATION index=\d+ tick=\d+ kind=14 role=\d+ thread=\d+ actor=([0-9a-f]{16}) owner=([0-9a-f]{16}) time=[0-9a-f]{16} result=1 valid=1',log)
    positive_guns={gun for gun,owner in evaluations if context and owner==context[3]}
    if not guns<=positive_guns:errors.append(f'player {slot} created player projectile lacks positive native shoot evaluation')
    return (records,summary),{'created':len(created),'moved':len(moved),'damage_calls':damage,
        'shield_damage_effects':damage_effects,'positive_shoot_evaluations':len(evaluations),
        'firing_with_damage_observed':bool(created and moved and damage_effects and positive_guns),
        'guns':sorted(guns),'damage_scope':'registered fixture main shields','player_rng_reseeded':seeded}


def observer_evidence(log,slot,context,last_tick,errors):
    """Read-only trace is independently compared; it is not in the wire hash."""
    enabled=re.findall(r'NATIVE_GATE PLAYER_OBSERVER_ENABLED tick=(\d+) early_coverage=(\d+) input_changes=(\d+)',log)
    if enabled!=[('1','0','0')]:errors.append(f'player {slot} observer coverage declaration missing')
    pattern=(r'NATIVE_GATE PLAYER_OBSERVATION index=(\d+) tick=(\d+) kind=(\d+) role=(\d+) thread=(\d+) '
             r'actor=([0-9a-f]{16}) owner=([0-9a-f]{16}) time=([0-9a-f]{16}) result=(\d+) valid=(\d+) '
             r'g0=([0-9a-f]{158}) c0=([0-9a-f]{98}) g1=([0-9a-f]{158}) c1=([0-9a-f]{98})')
    records=[];kinds={};roles={};changed={}
    for row in re.findall(pattern,log):
        index,tick,kind,role,thread=map(int,row[:5])
        actor,owner,time,result,valid,g0,c0,g1,c1=row[5:]
        if (index!=len(records)+1 or not 1<=tick<=last_tick or kind not in (14,33) or role not in (1,2,3)
                or not thread or actor=='0'*16 or valid!='1' or result not in ('0','1')):
            errors.append(f'player {slot} invalid native player observation {index}')
        if kind==33 and (not context or actor!=context[3] or owner!=context[3] or result!='0'):
            errors.append(f'player {slot} native update context differs at {index}')
        for raw in (g0,g1):
            decoded=struct.unpack('<11f3dI7B',bytes.fromhex(raw))
            if not all(math.isfinite(v) for v in decoded[:14]):errors.append(f'player {slot} nonfinite native control state {index}')
        kinds[kind]=kinds.get(kind,0)+1;roles[role]=roles.get(role,0)+1
        changed[kind]=changed.get(kind,0)+int(g0!=g1 or c0!=c1)
        # OS thread IDs diagnose coverage locally; the role is portable.
        records.append((index,tick,kind,role,actor,owner,time,result,valid,g0,c0,g1,c1))
    stops=re.findall(r'NATIVE_GATE PLAYER_OBSERVER_STOP enabled=(\d+) count=(\d+) failures=(\d+) restored=(\d+) active=(\d+) early_coverage=(\d+) emitted=(\d+)',log)
    if stops!=[('1',str(len(records)),'0','1','0','0',str(len(records)))]:
        errors.append(f'player {slot} native observer incomplete or not restored')
    if log.count('NATIVE_GATE PLAYER_OBSERVATION ')!=len(records):errors.append(f'player {slot} malformed native observation')
    if not kinds.get(33):errors.append(f'player {slot} native controller update observer unexercised')
    return records,{'calls_by_kind':kinds,'thread_roles':roles,'changed_calls_by_kind':changed,
                    'shoot_evaluation_exercised':bool(kinds.get(14)),'early_updates_observed':False,
                    'input_mutations':False,'wire_hash_includes_trace':False}


def evidence(log, slot, mode, states, errors):
    ticks=set(states)-{0}
    samples={}
    contexts=re.findall(r'NATIVE_GATE PLAYER_CONTEXT slot=p([12]) player=([0-9a-f]{16}) container=([0-9a-f]{16}) occupied=([0-9a-f]{16}) controlled=([0-9a-f]{16}) native_contexts=1 input_capture=none',log)
    context=contexts[0][1:] if len(contexts)==1 and int(contexts[0][0])==slot else None
    if not context:errors.append(f'player {slot} singleton context declaration missing')
    for tick,data in re.findall(r'NATIVE_GATE PLAYER_STATE tick=(\d+) data=([^\r\n]+)',log):
        tick=int(tick)
        try:
            fields=data.split(':')
            if tick in samples or len(fields)!=9 or fields[0]!='PLAYER_SINGLETON_1':raise ValueError()
            if tuple(fields[1:5])!=context or fields[5] not in ('0','1') or fields[6] not in ('0','1'):raise ValueError()
            if not re.fullmatch(r'[\w.\-]+',fields[7]):raise ValueError()
            speeds=fields[8].split(',')
            if len(speeds)!=8 or any(not math.isfinite(float(v)) for v in speeds):raise ValueError()
            if any(speeds[i] not in ('0','1') for i in (3,4,5)):raise ValueError()
        except (ValueError,TypeError):errors.append(f'player {slot} invalid native player state {tick}')
        samples[tick]=data
    if set(samples)!=ticks:errors.append(f'player {slot} native player state sequence incomplete')
    inputs={}
    actions=[]
    for tick,owner,actor,action,source,local in re.findall(r'NATIVE_GATE PLAYER_ACTION tick=(\d+) slot=p([12]) actor=([0-9a-f]{16}) action=(\w+) source=(\w+) local_only=([01])',log):
        row=(int(tick),int(owner),actor,action,source,local)
        if row in actions:errors.append(f'player {slot} duplicate native player action')
        actions.append(row)
    expected=[]
    if mode=='native-local-negative' and 30 in ticks and context:
        expected=[(30,slot,context[3],'flight_assist_toggle' if slot==1 else 'travel_start','native_export','1')]
    if mode in ('agreed-singleton','agreed-throttle','authority-negative','agreed-fire'):
        for tick,data in re.findall(r'NATIVE_GATE PLAYER_INPUTS tick=(\d+) data=(PLAYER_INPUTS_1:[0-9a-f]{8}:[0-9a-f]{8})',log):
            tick=int(tick)
            if tick in inputs:errors.append(f'player {slot} duplicate native player batch')
            inputs[tick]=data
            commands=tuple(states.get(tick-1,{}).get(str(s),{}).get('command') for s in (1,2))
            if tuple(data.split(':')[1:])!=commands:errors.append(f'player {slot} native player batch was not admitted at {tick}')
            if commands[1]!='00000000':errors.append(f'player {slot} unauthorized second native player context')
            command=commands[0]
            if command!='00000000':
                action={'81010000':'flight_assist_toggle','82010000':'travel_start','83010000':'travel_stop'}.get(command)
                if mode=='agreed-throttle' and command and re.fullmatch(r'8401[0-9a-f]{2}00',command) and int(command[4:6],16)<=200:action='throttle_'+command[4:6]
                if mode=='agreed-fire' and command:
                    if command in ('85010000','85010100'):action='trigger_'+str(int(command[4:6],16))
                    elif re.fullmatch(r'8601[0-9a-f]{2}00',command) and 1<=int(command[4:6],16)<=12:action='target_'+command[4:6]
                if not action or not context:errors.append(f'player {slot} invalid native player action envelope')
                else:expected.append((tick,1,context[3],action,'admitted_native_export','0'))
        if set(inputs)!=ticks:errors.append(f'player {slot} native admitted batch trace incomplete')
    if actions!=expected:errors.append(f'player {slot} native action application differs from admitted plan')
    rejections=re.findall(r'NATIVE_GATE PLAYER_BATCH_REJECTED tick=(\d+) p1=([0-9a-f]{8}) p2=([0-9a-f]{8}) effects=(\d+)',log)
    if mode=='authority-negative':
        admitted=tuple(states.get(29,{}).get(str(s),{}).get('command') for s in (1,2))
        if rejections!=[('30','81010000','81010000','0')] or admitted!=('81010000','81010000'):
            errors.append(f'player {slot} authority rejection differs from admitted conflict')
        if ticks!=set(range(1,30)) or actions:
            errors.append(f'player {slot} authority rejection advanced or applied an action')
        if re.findall(r'NATIVE_GATE ABORT stopped=(\d+) active=(\d+) restored=(\d+) tick=(\d+)',log)!=[('1','0','1','30')]:
            errors.append(f'player {slot} authority rejection did not stop and restore')
        if not re.search(r'NATIVE_GATE ERROR [^\r\n]*native player context ownership refused',log):
            errors.append(f'player {slot} authority rejection reason missing')
    elif rejections:
        errors.append(f'player {slot} unexpected native player batch rejection')
    activities={tick:data.split(':')[7] for tick,data in samples.items() if len(data.split(':'))==9}
    active_travel=[tick for tick,data in samples.items() if len(data.split(':'))==9 and data.split(':')[8].split(',')[4]=='1']
    controls={}
    if mode in ('agreed-throttle','agreed-fire'):
        for tick,data in re.findall(r'NATIVE_GATE PLAYER_CONTROLS tick=(\d+) data=([^\r\n]+)',log):
            tick=int(tick)
            try:
                match=re.fullmatch(r'PLAYER_AXES_1:([0-9a-f]{16}):([0-9a-f]{56})\|PLAYER_POSE_1:(.+)',data)
                if tick in controls or not match or not context or match[1]!=context[3]:raise ValueError()
                values=[struct.unpack('>f',bytes.fromhex(match[2][i:i+8]))[0] for i in range(0,56,8)]
                pose=list(map(float,match[3].split(',')))
                if len(pose)!=6 or not all(math.isfinite(v) for v in values+pose):raise ValueError()
            except (ValueError,TypeError,struct.error):errors.append(f'player {slot} invalid native axes/pose at {tick}')
            controls[tick]=data
        if set(controls)!=ticks:errors.append(f'player {slot} native axes/pose trace incomplete')
        for tick,data in controls.items():
            if tick in samples:samples[tick]+='|'+data
    if mode=='agreed-fire':
        fire={};held=0;last_time=-math.inf
        complete_moves=bool(re.search(r'native_trigger=1 rng_changes=[01] trace=2',log))
        seeded='PLAYER_LAUNCH_ENABLED' in log
        digest='0'*64;count=created=moves=damage=0;digests={}
        for kind,row in re.findall(r'NATIVE_GATE PLAYER_FIRE_(EVENT|STATE) ([^\r\n]+)',log):
            if kind=='EVENT':
                digest=hashlib.sha256((digest+'\n'+row).encode()).hexdigest();count+=1
                event=re.search(r' kind=(\d+) ',row)
                if event:
                    created+=event[1]=='1';moves+=event[1]=='2';damage+=event[1]=='4'
            else:
                tick=re.match(r'tick=(\d+) ',row)
                if tick:digests[int(tick[1])]=(digest,created,moves,damage,count)
        actions_by_tick={row[0]:row[3] for row in actions}
        for tick,data in re.findall(r'NATIVE_GATE PLAYER_FIRE_STATE tick=(\d+) data=([^\r\n]+)',log):
            tick=int(tick)
            try:
                match=re.fullmatch(r'PLAYER_FIRE_1:([0-9a-f]{16}):([0-9a-f]{14}):([0-9a-f]{2}):([0-9a-f]{16})\|PLAYER_AIM_1:([0-9a-f]{16}):([^|]+)(?:\|PLAYER_PROJECTILES_1:((?:\d+:){9}[0-9a-f]{64}))?(?:\|PLAYER_LAUNCH_1:1:504c5957504e3031:(?:\d+:){5}[0-9a-f]{64})?',data)
                if tick in fire or not match or not context or match[1]!=context[3]:raise ValueError()
                action=actions_by_tick.get(tick,'')
                if action.startswith('trigger_'):held=int(action[-1])
                flags=bytes.fromhex(match[2]);history=int(match[3],16)
                time=struct.unpack('>d',bytes.fromhex(match[4]))[0]
                aim=list(map(float,match[6].split(',')))
                if (flags[0]!=held or history&1!=held or any(flags[1:4]) or
                    not math.isfinite(time) or time<last_time or len(aim)!=6 or not all(map(math.isfinite,aim))):raise ValueError()
                if complete_moves:
                    if not match[7]:raise ValueError()
                    parts=match[7].split(':');summary=list(map(int,parts[:9]));actual=digests[tick]
                    if not (0<=summary[0]<=32 and tuple(summary[1:5])==actual[1:] and
                            summary[5:]==[0,held,0,int(seeded)] and parts[9]==actual[0]):raise ValueError()
                last_time=time
            except (ValueError,TypeError,struct.error):errors.append(f'player {slot} invalid native trigger/aim state at {tick}')
            fire[tick]=data
        if set(fire)!=ticks:errors.append(f'player {slot} native trigger/aim trace incomplete')
        for tick,data in fire.items():
            if tick in samples:samples[tick]+='|'+data
    return samples,inputs,{'context':context,'actions':actions,'rejections':rejections,'activity_transitions':[(tick,value) for tick,value in sorted(activities.items()) if activities.get(tick-1)!=value],
                           'travel_active_ticks':active_travel,'independent_native_players':False,'input_capture_complete':False}
