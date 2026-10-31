"""Independently check native gate logs, state hashes, clocks and cleanup evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import math
import struct
from collections import Counter
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from native_world_evidence import evidence as world_hash_evidence,configured_watches,configured_schema
from native_world_trace import evidence as world_trace_evidence
from native_world_queue import evidence as world_queue_evidence
from native_world_wait import evidence as world_wait_evidence,configured_waits,paired_diagnostics as world_wait_diagnostics
from native_transition_evidence import configured_evidence as transition_evidence
from native_world_wait_rng import configured as wait_rng_configured,evidence as wait_rng_evidence,paired_diagnostics as wait_rng_diagnostics,policies as wait_rng_policies
from native_world_action import configured as action_configured,evidence as action_evidence,paired_diagnostics as action_diagnostics
from native_world_values import evidence as value_evidence,paired_diagnostics as value_diagnostics,configured as values_configured
from native_world_position import evidence as position_evidence,paired_diagnostics as position_diagnostics,configured as position_configured
from native_world_safe_position import evidence as safe_position_evidence,paired_diagnostics as safe_position_diagnostics,configured as safe_position_configured
from native_world_position_rng import configured as position_rng_configured,prepare as position_rng_prepare,finalize as position_rng_finalize,paired_diagnostics as position_rng_diagnostics
from native_player_origin import configured_evidence as player_origin_evidence
from native_world_find_rng import evidence as find_evidence,paired_diagnostics as find_diagnostics,configured as find_configured
from native_player_getup import evidence as player_getup_evidence,bootstrap_plan as getup_bootstrap_plan
from native_player_evidence import evidence as player_evidence, observer_evidence as player_observer_evidence, fire_evidence as player_fire_evidence
from native_management_evidence import evidence as management_evidence
from native_player_launch_evidence import evidence as player_launch_evidence
from native_registry_evidence import evidence as registry_evidence,ship_ids,navigation as registry_navigation,navigation_results as registry_navigation_results
from native_solver_evidence import evidence as solver_evidence
from native_construction_evidence import evidence as construction_evidence
from native_creation_rng import bootstrap_agreement as creation_rng_bootstrap
from native_creation_rng_evidence import evidence as creation_rng_evidence
from native_registry_creation_evidence import evidence as registry_creation_evidence
from native_registry_md_evidence import evidence as registry_md_evidence
from native_request_evidence import evidence as request_evidence


def parse_samples(log, slot, errors):
    samples = {int(t): data for t, data in re.findall(r'NATIVE_GATE STATE tick=(\d+) entities=12 data=([^\r\n]+)', log)}
    rows, ends = {}, set()
    for match in re.finditer(r'NATIVE_GATE STATE_ROW tick=(\d+) id=(\d+) data=([^\r\n]+)', log):
        tick, actor = int(match[1]), int(match[2])
        frame = rows.setdefault(tick, {})
        if actor not in range(1, 13) or actor in frame or not match[3].startswith(str(actor)+'/'):
            errors.append(f'player {slot} invalid/duplicate state row at {tick}/{actor}')
        frame[actor] = match[3]
    for match in re.finditer(r'NATIVE_GATE STATE_END tick=(\d+) entities=(\d+) scope=([^\s]+)', log):
        tick = int(match[1])
        if tick in ends or tick in samples or match[2] != '12' or match[3] not in ('BATTLE_SUBSET_3', 'BATTLE_SUBSET_4', 'BATTLE_SUBSET_5', 'BATTLE_SUBSET_6', 'BATTLE_SUBSET_7', 'BATTLE_SUBSET_8') or set(rows.get(tick, {})) != set(range(1, 13)):
            errors.append(f'player {slot} incomplete/duplicate state frame at {tick}')
        else:
            samples[tick] = match[3]+'|'+'|'.join(rows[tick][actor] for actor in range(1, 13))
        ends.add(tick)
    if rows.keys()-ends:
        errors.append(f'player {slot} state rows missing frame terminator')
    return samples


def command_evidence(log, slot, mode, states, errors):
    inputs, applied = {}, {}
    for t, m, a, b in re.findall(r'NATIVE_GATE INPUTS tick=(\d+) mode=(\w+) p1=([0-9a-f]{8}) p2=([0-9a-f]{8})', log):
        tick = int(t)
        if tick in inputs or m != mode:
            errors.append(f'player {slot} duplicate/wrong command batch at {tick}')
        inputs[tick] = (a, b)
    expected = {}
    for tick in sorted(states):
        if tick == 0:
            continue
        commands = tuple(states[tick-1].get(str(s), {}).get('command') for s in (1, 2))
        if inputs.get(tick) != commands or None in commands:
            errors.append(f'player {slot} admitted command batch differs at {tick}')
        for player, command in enumerate(commands, 1):
            if command and command != '00000000':
                expected[tick, player] = tuple(int(command[i:i+2], 16) for i in (0, 2, 4))
    for tick, player, op, actor, target, age in re.findall(r'NATIVE_GATE COMMAND_APPLIED tick=(\d+) slot=(\d+) op=(\d+) actor=(\d+) target=(\d+) time=([\deE.+-]+)', log):
        key = int(tick), int(player)
        if key in applied or expected.get(key) != (int(op), int(actor), int(target)):
            errors.append(f'player {slot} unexpected/duplicate native command {key}')
        applied[key] = float(age)
    if set(applied) != set(expected) or not all(any(s == p for _, s in applied) for p in (1, 2)):
        errors.append(f'player {slot} native command application is incomplete')
    if 'COMMAND_REFUSED' in log:
        errors.append(f'player {slot} refused a native command')
    if set(inputs) != set(states)-{0}:
        errors.append(f'player {slot} command batch log is incomplete')
    return {t: 'INPUTS_1:'+mode+':'+a+':'+b for t, (a, b) in inputs.items()}, applied


def live_input_evidence(log, slot, queued, states, errors):
    captured = {}
    for player, sequence, tick, command in re.findall(r'NATIVE_GATE INPUT_CAPTURE slot=([12]) seq=(\d+) tick=(\d+) command=([0-9a-f]{8})', log):
        sequence, tick = int(sequence), int(tick)
        if int(player) != slot or sequence in captured:
            errors.append(f'player {slot} duplicate/foreign live capture')
        captured[sequence] = {'tick': tick, 'command': command}
    expected = {int(sequence): row for sequence, row in (queued or {}).items()}
    if not expected or set(expected) != set(range(1, len(expected)+1)) or captured != expected:
        errors.append(f'player {slot} live input capture differs from submitted queue')
    proposals = {tick: row[str(slot)]['command'] for tick, row in states.items()
                 if str(slot) in row and row[str(slot)].get('command', '00000000') != '00000000'}
    seen = {}
    for sequence, row in captured.items():
        tick = row['tick']
        if tick in seen or tick+1 not in states:
            errors.append(f'player {slot} live input duplicate tick or never admitted')
        seen[tick] = row['command']
    if seen != proposals:
        errors.append(f'player {slot} live capture differs from native proposals')
    return len(captured)


def travel_evidence(log, slot, last_tick, errors):
    rows, active = {}, set()
    for tick, actor, payload in re.findall(r'NATIVE_GATE TRAVEL tick=(\d+) id=(\d+) ([^\r\n]+)', log):
        key=int(tick),int(actor)
        if key in rows:
            errors.append(f'player {slot} duplicate native travel sample {key}')
        fields=dict(re.findall(r'(\w+)=([^ ]+)',payload))
        try:
            if fields.get('alive') == '0':
                if fields != {'alive':'0'}: raise ValueError('dead sample fields')
            elif fields.get('alive') == '1':
                if set(fields) != {'alive','charging','active','speed','maxspeed','charge','attention','time'}:
                    raise ValueError('incomplete travel fields')
                if fields['charging'] not in ('0','1') or fields['active'] not in ('0','1'):
                    raise ValueError('invalid travel flags')
                # Native MD formats attention using common.xsd's named enum.
                if fields['attention'] not in ('unknown','ingalaxy','incluster','sectorvisible','insector',
                                               'adjacentzone','visible','inzone','nearby','adjacentroom','inroom'):
                    raise ValueError('invalid native attention enum')
                values={name:float(fields[name]) for name in ('speed','maxspeed','charge','time')}
                if not all(math.isfinite(v) for v in values.values()) or values['speed']<0 or values['maxspeed']<=0 or not 0<=values['charge']<=1:
                    raise ValueError('invalid travel values')
                if fields['active']=='1': active.add(key[1])
            else: raise ValueError('invalid operational flag')
        except ValueError:
            errors.append(f'player {slot} invalid native travel sample {key}')
        rows[key]=fields
    expected={(tick,actor) for tick in range(30,last_tick,60) for actor in range(1,13)}
    # The final UI request cannot require another simulation frame after STOP.
    rows={key:value for key,value in rows.items() if key[0]!=last_tick}
    if set(rows)!=expected:
        errors.append(f'player {slot} native travel sample sequence incomplete')
    return rows, active


def boost_evidence(log, slot, last_tick, errors):
    rows, boosted, valid_live = {}, set(), []
    for tick,actor,payload in re.findall(r'NATIVE_GATE BOOST tick=(\d+) id=(\d+) ([^\r\n]+)',log):
        key=int(tick),int(actor);fields=dict(re.findall(r'(\w+)=([^ ]+)',payload))
        if key in rows:errors.append(f'player {slot} duplicate native boost sample {key}')
        try:
            if fields.get('alive')=='0':
                if fields!={'alive':'0'}:raise ValueError('dead fields')
            elif fields.get('alive')=='1':
                if set(fields)!={'alive','active','available','remaining','speed','maxspeed','time'}:raise ValueError('fields')
                if fields['active'] not in ('0','1') or fields['available'] not in ('0','1'):raise ValueError('flags')
                values={name:float(fields[name]) for name in ('remaining','speed','maxspeed','time')}
                if not all(math.isfinite(v) for v in values.values()) or not 0<=values['remaining']<=1 or values['speed']<0 or values['maxspeed']<=0:raise ValueError('values')
                valid_live.append((key,fields,values))
            else:raise ValueError('alive')
        except ValueError:errors.append(f'player {slot} invalid native boost sample {key}')
        rows[key]=fields
    # maxspeed is mode-dependent: it rises while boost is active. The fixture
    # samples frame 30 before either withdrawal command applies (31/46), so use
    # that native inactive baseline, not the later boosted speed ceiling.
    baselines={key[1]:values['maxspeed'] for key,fields,values in valid_live if key[0]==30 and fields['active']=='0'}
    for key,fields,values in valid_live:
        if key[1] in baselines and fields['active']=='1' and values['speed']>baselines[key[1]] and values['remaining']<1:boosted.add(key[1])
    rows={key:value for key,value in rows.items() if key[0]!=last_tick}
    expected={(tick,actor) for tick in range(30,last_tick,60) for actor in range(1,13)}
    if set(rows)!=expected:errors.append(f'player {slot} native boost sample sequence incomplete')
    return rows,boosted


def steering_initializations(log):
    counts = re.findall(r'NATIVE_GATE STEERING_INIT count=(\d+)', log)
    totals = re.findall(r'NATIVE_GATE STEERING_INIT_TOTALS data=([0-9/,]+)', log)
    if len(counts) != 1:
        return None
    if not totals:
        # Preserve the original twelve-model evidence; never infer additional
        # model generations merely from an aggregate count.
        return tuple((i, 1) for i in range(1, 13)) if counts == ['12'] else None
    if len(totals) != 1:
        return None
    rows = [tuple(map(int, row.split('/'))) for row in totals[0].split(',')]
    if len(rows) != len(ship_ids(log)) or any(len(row) != 2 for row in rows) or {row[0] for row in rows} != ship_ids(log):
        return None
    if any(row[1] < (1 if row[0]<=12 else 0) for row in rows) or sum(row[1] for row in rows) != int(counts[0]):
        return None
    return tuple(sorted(rows))


def spline_join_evidence(log, slot, errors):
    rows=re.findall(r'NATIVE_GATE SPLINE_JOIN requests=(\d+) waits=(\d+) timeouts=(\d+)',log)
    if len(rows)!=1 or int(rows[0][0])==0 or int(rows[0][1])>int(rows[0][0]) or rows[0][2]!='0':
        errors.append(f'player {slot} native spline result join failed or was not exercised')
        return None
    return dict(zip(('requests','waits','timeouts'),map(int,rows[0])))


def world_identity_evidence(log, slot, errors):
    rows=re.findall(r'NATIVE_GATE WORLD_ACTOR id=(\d+) saved=([0-9a-f]{16})(?=\r?\n|$)',log)
    stop=re.findall(r'NATIVE_GATE WORLD_IDENTITY_STOP calls=(\d+) count=(\d+) late=(\d+) restored=(\d+) failures=(\d+)(?=\r?\n|$)',log)
    valid=len(stop)==1 and stop[0][3:]==('1','0') and 12<=int(stop[0][1])<=524288 and int(stop[0][0])>=int(stop[0][1])
    valid=valid and len(rows)==12 and log.count('NATIVE_GATE WORLD_ACTOR ')==12 and \
        {int(r[0]) for r in rows}==set(range(1,13)) and len({r[1] for r in rows})==12 and all(int(r[1],16)>0 for r in rows)
    if not valid:errors.append(f'player {slot} saved world identity capture incomplete or not restored')
    return sorted(rows,key=lambda r:int(r[0]))


def interrupt_evidence(log, slot, last_tick, errors):
    stop=re.findall(r'NATIVE_GATE INTERRUPT_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE INTERRUPT tick=(\d+) ordinal=(\d+) actor=(\d+) kind=([01]) event=[0-9a-f]+ flags=[0-9a-f]+/[0-9a-f]+ script=[^ ]+ pc=\d+ range=\d+/\d+ mode=\d+ rng={word}/{word} values=[0-9a-f]+:{word},[0-9a-f]+:{word} result=[0-9a-f]+',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows)
    valid=valid and log.count('NATIVE_GATE INTERRUPT tick=')==len(rows)
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence,actor,kind=map(int,row)
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and actor in ship_ids(log)
        previous=tick
    if not valid:errors.append(f'player {slot} native interrupt trace incomplete or not restored')
    return len(rows)


def interrupt_step_evidence(log, slot, last_tick, errors):
    parents={int(m[2]):(int(m[1]),int(m[3])) for m in re.finditer(
        r'NATIVE_GATE INTERRUPT tick=(\d+) ordinal=(\d+) actor=(\d+) kind=1 ',log)}
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE INTERRUPT_STEP tick=(\d+) ordinal=(\d+) parent=(\d+) actor=(\d+) pc=(\d+) next=(\d+) type=([0-9a-f]+) action=([0-9a-f]+) chance=(\d+) flags=(\d+)/(\d+) rng=({word})/({word})',log)
    stop=re.findall(r'NATIVE_GATE INTERRUPT_STEP_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows) and len(rows)>0
    valid=valid and log.count('NATIVE_GATE INTERRUPT_STEP tick=')==len(rows)
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence,parent,actor,pc,next_pc=map(int,row[:6])
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and parents.get(parent)==(tick,actor) and \
            0<=pc<1048576 and 0<=next_pc<=0xffffffffffffffff and \
            0<int(row[6],16)<0x6fa9000 and 0<int(row[7],16)<0x6fa9000 and \
            int(row[8])<=0xffffffff and int(row[9])<=0xffff and int(row[10])<=0xffff
        previous=tick
    if not valid:errors.append(f'player {slot} native interrupt instruction trace incomplete or not restored')
    return rows


def nested_action_evidence(log, slot, last_tick, errors):
    steps={int(m[2]):(int(m[1]),int(m[3]),int(m[4])) for m in re.finditer(
        r'NATIVE_GATE INTERRUPT_STEP tick=(\d+) ordinal=(\d+) parent=(\d+) actor=(\d+) ',log)}
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NESTED_ACTION tick=(\d+) ordinal=(\d+) interrupt=(\d+) step=(\d+) actor=(\d+) parent=(\d+) depth=(\d+) owner=([0-9a-f]+) program=({word}) pc=(\d+) next=(\d+) type=([0-9a-f]+) action=([0-9a-f]+) chance=(\d+) flags=(\d+)/(\d+) rng=({word})/({word})',log)
    stop=re.findall(r'NATIVE_GATE NESTED_ACTION_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows)
    valid=valid and log.count('NATIVE_GATE NESTED_ACTION tick=')==len(rows)
    previous=0;parents={}
    for ordinal,row in enumerate(rows,1):
        tick,sequence,interrupt,step,actor,parent,depth=map(int,row[:7])
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and steps.get(step)==(tick,interrupt,actor) and \
            1<=depth<=64 and ((not parent and depth==1) or parents.get(parent)==(tick,depth-1)) and \
            int(row[7],16)<=0xffffffff and int(row[8],16)!=0 and 0<=int(row[9])<1048576 and \
            0<=int(row[10])<=0xffffffffffffffff and 0<int(row[11],16)<0x6fa9000 and \
            0<int(row[12],16)<0x6fa9000 and int(row[13])<=0xffffffff and int(row[14])<=0xffff and int(row[15])<=0xffff
        parents[sequence]=(tick,depth);previous=tick
    if not valid:errors.append(f'player {slot} nested native action trace incomplete or not restored')
    return rows


def npc_action_evidence(log, slot, last_tick, errors):
    steps={int(m[2]):(int(m[1]),int(m[3]),int(m[4])) for m in re.finditer(
        r'NATIVE_GATE INTERRUPT_STEP tick=(\d+) ordinal=(\d+) parent=(\d+) actor=(\d+) ',log)}
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_ACTION tick=(\d+) ordinal=(\d+) interrupt=(\d+) step=(\d+) actor=(\d+) parent=(\d+) depth=(\d+) owner=([0-9a-f]+) program=({word}) pc=(\d+) next=(\d+) type=([0-9a-f]+) action=([0-9a-f]+) chance=(\d+) flags=(\d+)/(\d+) rng=({word})/({word}) named=(\d+) cue=([^\s]+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_ACTION_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows) and len(rows)>0
    valid=valid and log.count('NATIVE_GATE NPC_ACTION tick=')==len(rows)
    previous=0;parents={}
    for ordinal,row in enumerate(rows,1):
        tick,sequence,interrupt,step,actor,parent,depth=map(int,row[:7])
        owned=(not actor and not interrupt and not step) or (1<=actor<=12 and steps.get(step)==(tick,interrupt,actor))
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and owned and \
            1<=depth<=64 and ((not parent and depth==1) or parents.get(parent)==(tick,depth-1)) and \
            row[7]=='5d' and int(row[8],16)!=0 and 0<=int(row[9])<1048576 and \
            0<=int(row[10])<=0xffffffffffffffff and 0<int(row[11],16)<0x6fa9000 and \
            0<int(row[12],16)<0x6fa9000 and int(row[13])<=0xffffffff and int(row[14])<=0xffff and int(row[15])<=0xffff and \
            row[18]=='1' and bool(re.fullmatch(r'NPC_UseCases\.[A-Za-z0-9_.%\-]+',row[19]))
        parents[sequence]=(tick,depth);previous=tick
    if not valid:errors.append(f'player {slot} named NPC lifecycle trace incomplete or not restored')
    return rows


def npc_mix(value):
    mask=(1<<64)-1
    value=((value^(value>>30))*0xbf58476d1ce4e5b9)&mask
    value=((value^(value>>27))*0x94d049bb133111eb)&mask
    return value^(value>>31)


def npc_exec_identity(cue, program, pc, tick, call):
    key=14695981039346656037
    for byte in cue.encode('ascii'):
        key=((key^byte)*1099511628211)&((1<<64)-1)
    key=npc_mix(npc_mix(key^program)^pc)
    seed=npc_mix(0x58344c5345454432^0x4e50435f45584543)
    for field in (tick,key,program,call,pc):seed=npc_mix(seed^field)
    return key,seed


def npc_trigger_text(text):
    h=14695981039346656037
    for b in text.encode('ascii'):h=((h^b)*1099511628211)&((1<<64)-1)
    return h


def npc_trigger_cause_key(tick,actor,script,pc,begin,end,event,mode):
    h=0x4649585f43415531
    for v in (tick,actor,script,pc,begin,end,event,mode):h=npc_mix(h^v)
    return h


def npc_trigger_path_key(row):
    h=0x4649585f50415431
    for name in ('cause','parent_key','visit','child','action_program','action_pc','type','action','cue_hash','program','pc','caller'):
        h=npc_mix(h^row[name])
    return h


def npc_trigger_evidence(log, slot, last_tick, errors, legacy=False):
    def records(kind):
        result=[]
        for m in re.finditer(r'NATIVE_GATE '+kind+r' ([^\r\n]+)',log):
            try:
                items=[v.split('=',1) for v in m[1].split()]
                row=dict(items)
                if len(row)!=len(items):raise ValueError('duplicate field')
                result.append(row)
            except (ValueError,IndexError):result.append({})
        return result
    causes=records('NPC_CAUSE');rows=records('NPC_TRIGGER')
    stop=re.findall(r'NATIVE_GATE NPC_TRIGGER_STOP calls=(\d+) causes=(\d+) restored=(\d+) failures=(\d+) identity=([^\s]+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_TRIGGER_COVERAGE seen=(\d+) foreign_thread=(\d+) unsupported=(\d+)',log)
    valid=len(stop)==1 and stop[0][2:]==('1','0','fixture_trigger_v1') and int(stop[0][0])==len(rows) and int(stop[0][1])==len(causes)
    valid=valid and len(coverage)==1 and int(coverage[0][0])==len(rows) and coverage[0][1]=='0'
    irqs={int(r['ordinal']):r for r in records('INTERRUPT') if r.get('kind')=='1'}
    steps={};visits={}
    for r in records('INTERRUPT_STEP'):
        try:
            parent=int(r['parent']);visits[parent]=visits.get(parent,0)+1
            steps[int(r['ordinal'])]=(r,visits[parent])
        except (ValueError,KeyError):valid=False
    cause_by_irq={};cause_counts={};previous=0
    try:
        for raw in causes:
            if set(raw)!=set('tick irq actor key occurrence script pc range event mode status'.split()):raise ValueError('cause schema')
            c={k:int(v,16 if k in ('key','script','event') else 10) for k,v in raw.items() if k!='range'}
            c['begin'],c['end']=map(int,raw['range'].split('/'));irq=irqs.get(c['irq'])
            valid=valid and previous<=c['tick']<=last_tick and irq is not None and c['irq'] not in cause_by_irq
            if irq:
                expected=(int(irq['tick']),int(irq['actor']),npc_trigger_text(irq['script']),int(irq['pc']),irq['range'],int(irq['event'],16),int(irq['mode']))
                valid=valid and (c['tick'],c['actor'],c['script'],c['pc'],raw['range'],c['event'],c['mode'])==expected
                zero=irq['values']=='0:0000000000000000,0:0000000000000000'
                if c['status'] in (1,6):
                    wanted=npc_trigger_cause_key(c['tick'],c['actor'],c['script'],c['pc'],c['begin'],c['end'],c['event'],c['mode'])
                    group=(c['tick'],wanted);cause_counts[group]=cause_counts.get(group,0)+1
                    valid=valid and zero and 1<=c['actor']<=12 and c['key']==wanted and c['occurrence']==cause_counts[group] and c['status']==(1 if c['occurrence']==1 else 6)
                elif c['status']==3:valid=valid and not zero and c['key']==0 and c['occurrence']==0
                else:valid=False
            cause_by_irq[c['irq']]=c;previous=c['tick']
        valid=valid and len(cause_by_irq)==len(irqs)
    except (ValueError,KeyError,TypeError):valid=False
    normalized=[];parents={};children={};unsupported=0;previous=0;ambiguous=0
    hex_fields={'key','cause','parent_key','action_program','type','action','program','caller'}
    expected_fields=set('tick ordinal parent depth status legacy key cause irq step actor parent_key visit child action_program action_pc type action program pc caller rng cue'.split())
    try:
        for ordinal,raw in enumerate(rows,1):
            if set(raw)!=expected_fields:raise ValueError('trigger schema')
            r={k:int(v,16 if k in hex_fields else 10) for k,v in raw.items() if k not in ('cue','rng')}
            r['cue']=raw['cue'];r['cue_hash']=npc_trigger_text(r['cue']);r['before'],r['after']=(int(v,16) for v in raw['rng'].split('/'))
            valid=valid and previous<=r['tick']<=last_tick and r['ordinal']==ordinal and r['legacy']==int(legacy) and 1<=r['depth']<=64 and \
                r['status'] in range(1,9) and ((not r['parent'] and r['depth']==1) or (r['parent'] in parents and parents[r['parent']]['depth']+1==r['depth']))
            if r['status']==1:
                c=cause_by_irq.get(r['irq']);step,visit=steps.get(r['step'],({},0));parent=parents.get(r['parent'])
                valid=valid and c is not None and c['status']==1 and c['key']==r['cause'] and c['actor']==r['actor'] and \
                    c['tick']==r['tick'] and int(step.get('parent',-1))==r['irq'] and r['key']==npc_trigger_path_key(r)
                if r['parent_key']:
                    valid=valid and parent is not None and parent['status']==1 and parent['key']==r['parent_key'] and parent['irq']==r['irq'] and \
                        r['action_program']==parent['program'] and r['visit']>0
                    child_group=('action',r['parent'],r['visit'])
                else:
                    valid=valid and r['visit']==visit and r['action_program']==0 and r['action_pc']==int(step.get('pc',-1)) and \
                        r['type']==int(step.get('type','0'),16) and r['action']==int(step.get('action','0'),16)
                    child_group=('step',r['step'])
                children[child_group]=children.get(child_group,0)+1;valid=valid and r['child']==children[child_group]
                # A later identical cause occurrence invalidates the first one's
                # uniqueness too; preserve its trace but never claim identity.
                if c and cause_counts.get((c['tick'],c['key']),0)>1:ambiguous+=1
            else:
                unsupported+=1;valid=valid and r['key']==0
            r['usable']=r['status']==1 and cause_counts.get((r['tick'],r['cause']),0)==1
            parents[r['ordinal']]=r;previous=r['tick'];normalized.append(r)
        valid=valid and len(coverage)==1 and int(coverage[0][2])==unsupported
    except (ValueError,KeyError,TypeError):valid=False
    if not valid:errors.append(f'player {slot} native NPC trigger observation incomplete, inconsistent or not restored')
    return {'rows':normalized,'unsupported':unsupported,'ambiguous_candidates':ambiguous,
            'candidate_coverage_complete':bool(normalized) and not unsupported and not ambiguous}


def npc_provenance_evidence(log, slot, last_tick, errors, triggers, origin=False):
    """Validate diagnostic joins; local cue tokens never become semantic IDs."""
    rows=re.findall(r'NATIVE_GATE NPC_PROVENANCE ([^\r\n]+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_PROVENANCE_STOP calls=(\d+) restored=(\d+) failures=(\d+) overflows=(\d+) scope=([^\s]+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_PROVENANCE_COVERAGE unresolved=(\d+) unowned=(\d+)',log)
    scope='windows_local_observation_v2' if origin else 'windows_local_observation_v1'
    valid=bool(rows) and len(rows)==len(triggers) and len(stop)==1 and stop[0][1:]==('1','0','0',scope) and int(stop[0][0])==len(rows)
    valid=valid and len(coverage)==1 and len(rows)==log.count('NATIVE_GATE NPC_PROVENANCE ')
    expected=set('tick trigger flags depth cue state block time time_role event event_name parent template root namespace ancestry values owner_kind owner_status owner owner_saved owner_pc owner_type owner_action owner_caller'.split())
    if origin:expected.update(('owner_caller_raw','owner_caller_source'))
    names={};normalized=[];unresolved=unowned=0
    def link(text):
        token,name=text.split(':',1);token=int(token)
        if not 0<=token<=4096 or not re.fullmatch(r'[A-Za-z0-9_.%\-]+',name):raise ValueError('link')
        if token:
            if name in ('none','unreadable','unsupported') or names.get(token,name)!=name:raise ValueError('token identity')
            names[token]=name
        return token,name
    try:
        for index,raw in enumerate(rows):
            items=[x.split('=',1) for x in raw.split()];r=dict(items)
            if set(r)!=expected or len(r)!=len(items):raise ValueError('schema')
            integer={k:int(r[k],16 if k in ('time','event','owner_saved','owner_type','owner_action','owner_caller') else 10)
                     for k in ('tick','trigger','flags','depth','block','time','event','owner_kind','owner_status','owner_saved','owner_pc','owner_type','owner_action','owner_caller')}
            t=triggers[index] if index<len(triggers) else {}
            valid=valid and 0<integer['tick']<=last_tick and integer['trigger']==t.get('ordinal') and integer['tick']==t.get('tick') and \
                integer['flags']==0 and t.get('depth',65)<=integer['depth']<=64 and 0<=integer['block']<1048576 and \
                0<=integer['time']<1<<64 and 0<=integer['owner_saved']<1<<64
            cue=link(r['cue']);links=[link(r[k]) for k in ('parent','template','root','namespace')]
            ancestry=[] if r['ancestry']=='none' else [link(x) for x in r['ancestry'].split(',')]
            owner=link(r['owner']);state,pending=map(int,r['state'].split('/'))
            valid=valid and cue[0]>0 and cue[1]==t.get('cue') and 0<=state<=4 and 0<=pending<=4 and len(ancestry)<=8
            valid=valid and len({x[0] for x in ancestry})==len(ancestry) and all(x[0]>0 and x[0]!=cue[0] for x in ancestry)
            valid=valid and ((not links[0][0] and not ancestry) or (bool(ancestry) and ancestry[0]==links[0]))
            valid=valid and all(token or name=='none' for token,name in links)
            time_role='action_block_time' if state==pending==2 else 'waiting_time' if state==pending==1 else 'state_time'
            event_name={0x580:'object_signalled',0x637:'cue_signalled'}.get(integer['event'],'unresolved')
            valid=valid and r['time_role']==time_role and r['event_name']==event_name
            values=[]
            for slot_index,text in enumerate(r['values'].split(',')):
                tag,kind,payload=text.split(':');tag=int(tag,16);kind=int(kind);payload=int(payload,16)
                if not 0<=tag<=0xffffffff or kind not in range(6) or not 0<=payload<1<<64:raise ValueError('value')
                valid=valid and (kind not in (0,2,3,5) or payload==0)
                if kind in (1,2):valid=valid and slot_index==0 and integer['event']==0x580 and tag==0xa and (kind!=1 or payload>0)
                elif kind==3:valid=valid and slot_index==0 and integer['event'] in (0x580,0x637) and tag==0
                elif kind in (4,5):valid=valid and slot_index==0 and integer['event']==0x637 and tag==0x12 and (kind!=4 or payload in names)
                else:valid=valid and not (slot_index==0 and ((integer['event']==0x580 and tag in (0,0xa)) or (integer['event']==0x637 and tag in (0,0x12))))
                if kind in (0,2,5):unresolved+=1
                values.append((tag,kind,names.get(payload) if kind==4 else payload))
            valid=valid and len(values)==4
            kind,status=integer['owner_kind'],integer['owner_status']
            valid=valid and kind in range(4) and status in range(3) and 0<=integer['owner_pc']<1048576 and \
                all(0<=integer[k]<0x6fa9000 for k in ('owner_type','owner_action','owner_caller'))
            if kind==0:valid=valid and status==0 and owner==(0,'none') and all(integer[k]==0 for k in ('owner_saved','owner_pc','owner_type','owner_action','owner_caller'))
            else:
                valid=valid and status in (1,2) and integer['owner_type']>0 and integer['owner_action']>0 and (status!=1 or integer['owner_caller']>0)
                if kind==1:valid=valid and owner[0]>0 and integer['owner_saved']==0
                elif kind==2:valid=valid and owner[0]==0 and owner[1] not in ('none','unreadable','unsupported')
                else:valid=valid and owner==(0,'unsupported') and integer['owner_saved']==0
            if status==2 or kind==3 or (kind==2 and not integer['owner_saved']):unowned+=1
            if origin:
                raw=int(r['owner_caller_raw'],16);source=int(r['owner_caller_source'])
                valid=valid and 0<=raw<0x6fa9000 and source in range(4)
                if status!=1:valid=valid and source==0 and raw==0 and integer['owner_caller']==0
                elif source==1:valid=valid and raw==integer['owner_caller'] and raw>0
                elif source==2:valid=valid and kind==2 and raw==0 and integer['owner_caller']==0x106e3fb
                else:valid=False # Unknown foreign callers remain unsupported.
                integer.update(owner_caller_raw=raw,owner_caller_source=source)
            # Local tokens are auxiliary. Name equivalence does not distinguish
            # duplicate instances and is never reported as semantic identity.
            normalized.append({**integer,'cue':cue[1],'state':(state,pending),'links':[x[1] for x in links],
                               'ancestry':[x[1] for x in ancestry],'values':values,'owner':owner[1]})
        valid=valid and len(coverage)==1 and tuple(map(int,coverage[0]))==(unresolved,unowned)
    except (ValueError,KeyError,TypeError):valid=False
    if not valid:errors.append(f'player {slot} NPC provenance missing, inconsistent, unstable or not restored')
    return {'rows':normalized,'unresolved':unresolved,'unowned':unowned,'semantic_identity_proven':False}


def npc_pilot_evidence(log, slot, last_tick, errors, origin=False):
    """A selected-owner timeline; local lifetimes and observed parents are not semantic causes."""
    lines=re.findall(r'NATIVE_GATE NPC_PILOT ([^\r\n]+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_PILOT_STOP calls=(\d+) restored=(\d+) failures=(\d+) overflows=(\d+) scope=([^\s]+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_PILOT_COVERAGE foreign=(\d+) unmapped=(\d+) unreadable=(\d+) retired=(\d+) after_unresolved=(\d+)',log)
    version='selected_saved_pilot_v2' if origin else 'selected_saved_pilot_v1'
    valid=bool(lines) and len(stop)==1 and stop[0][1:]==('1','0','0',version) and int(stop[0][0])==len(lines)
    valid=valid and len(coverage)==1 and log.count('NATIVE_GATE NPC_PILOT ')==len(lines)
    expected=set('tick ordinal end_tick kind nest depth enter exit context saved script after_script pc pc_role flags event event_role types payloads mode range rng returned after_status errors parent_kind parent_status parent_self parent_saved parent_name parent_pc parent_caller parent_caller_source'.split())
    if origin:expected.update(('irq_caller_raw','irq_caller','irq_caller_source'))
    numeric=expected-set('script after_script pc pc_role flags event_role types payloads range rng parent_name'.split())
    rows=[];edges=[];contexts={};kinds={1:0,2:0,3:0};retired=after_unresolved=unowned=irq_unresolved=0
    irq_sites={0x10662ef,0x1066d8d,0x1066dc6,0x106b082,0x106d8db,0x106e2f4,0x106e34f,0x106fef1,0x10a7996}
    totals=(0,0,0,0,0)
    try:
        for ordinal,line in enumerate(lines,1):
            items=[x.split('=',1) for x in line.split()];raw=dict(items)
            if set(raw)!=expected or len(raw)!=len(items):raise ValueError('schema')
            r={k:int(raw[k],16 if k in ('saved','event','parent_saved','parent_caller','irq_caller_raw','irq_caller') else 10) for k in numeric}
            if any(x<0 or x>=1<<64 for x in r.values()):raise ValueError('integer')
            for key,base in (('pc',10),('flags',16),('types',16),('range',10),('rng',16)):
                r[key]=tuple(int(x,base) for x in raw[key].split('/'))
                if len(r[key])!=2 or any(x<0 or x>=1<<64 for x in r[key]):raise ValueError('pair')
            for key,limit in (('script',287),('after_script',287),('parent_name',255)):
                if not 0<len(raw[key])<=limit or not re.fullmatch(r'[A-Za-z0-9_.%\-]+',raw[key]):raise ValueError('name')
                r[key]=raw[key]
            valid=valid and r['ordinal']==ordinal and 0<r['tick']<=last_tick and (not rows or r['tick']>=rows[-1]['tick']) and r['end_tick']==r['tick'] and r['saved']==0x976ab and \
                r['kind'] in kinds and 1<=r['depth']<=64 and 0<=r['nest']<ordinal and 0<r['enter']<r['exit']<=2*len(lines) and \
                1<=r['context']<=1024 and r['returned']==1 and r['errors']==0 and r['after_status'] in range(1,6) and \
                raw['payloads']=='opaque' and raw['event_role']=='stored_context' and raw['pc_role']==('stored_context' if r['kind']==3 else 'execution') and \
                r['mode']<=255 and r['event']<1<<32 and \
                all(x<1<<32 for x in r['flags']+r['types']) and r['script'] not in ('unreadable','unresolved')
            if r['kind'] in kinds:kinds[r['kind']]+=1
            if r['kind']!=2:valid=valid and r['mode']==0 and r['range']==(0,0)
            if origin:
                source,caller,raw_caller=(r[k] for k in ('irq_caller_source','irq_caller','irq_caller_raw'))
                if r['kind']!=2:valid=valid and (source,caller,raw_caller)==(0,0,0)
                else:
                    known=source==1 and caller==raw_caller and caller in irq_sites
                    irq_unresolved+=not known;valid=valid and known
            if r['after_status']==1:valid=valid and r['after_script'] not in ('unreadable','unresolved')
            else:valid=valid and r['pc'][1]==0 and r['flags'][1]==0 and r['after_script']=='unresolved'
            retired+=r['after_status'] in (2,3);after_unresolved+=r['after_status'] in (4,5)
            pk,ps,source=r['parent_kind'],r['parent_status'],r['parent_caller_source']
            valid=valid and pk in range(4) and ps in range(3) and r['parent_self'] in (0,1) and source in range(4) and r['parent_caller']<0x6fa9000
            if pk==0:valid=valid and ps==0 and r['parent_name']=='none' and all(r[k]==0 for k in ('parent_self','parent_saved','parent_pc','parent_caller','parent_caller_source'))
            else:
                valid=valid and ps in (1,2) and (pk!=1 or r['parent_saved']==0)
                valid=valid and ((r['parent_name']=='unsupported' and r['parent_saved']==0) if pk==3 else r['parent_name'] not in ('none','unsupported','unreadable'))
                if ps==2:valid=valid and source==0 and r['parent_caller']==0
                elif source==1:valid=valid and r['parent_caller']>0
                elif source==2:valid=valid and pk==2 and r['parent_caller']==0x106e3fb
                else:valid=False
            unowned+=ps==2 or pk==3 or (pk==2 and r['parent_saved']==0)
            edges.extend(((r['enter'],True,ordinal),(r['exit'],False,ordinal)))
            r['context']=contexts.setdefault(r['context'],len(contexts)+1);rows.append(r)
        stack=[]
        for sequence,(actual,enter,ordinal) in enumerate(sorted(edges),1):
            r=rows[ordinal-1];valid=valid and actual==sequence
            if enter:
                valid=valid and r['nest']==(stack[-1] if stack else 0) and r['depth']==len(stack)+1
                if stack:valid=valid and rows[stack[-1]-1]['tick']==r['tick']
                stack.append(ordinal)
            else:
                valid=valid and bool(stack) and stack[-1]==ordinal
                if stack:stack.pop()
        if coverage:totals=tuple(map(int,coverage[0]))
        valid=valid and not stack and kinds[1]>0 and len(coverage)==1 and totals[0]==0 and totals[3:]==(retired,after_unresolved)
    except (ValueError,KeyError,TypeError,IndexError):valid=False
    if not valid:errors.append(f'player {slot} selected NPC pilot trace missing, inconsistent, unsupported or not restored')
    return {'rows':rows,'kinds':kinds,'foreign':totals[0],'unmapped':totals[1],'unreadable':totals[2],
            'retired':retired,'after_unresolved':after_unresolved,'unowned':unowned,
            'irq_origin_schema':2 if origin else 1,'irq_origin_unresolved':irq_unresolved,
            'irq_origin_complete':origin and valid and kinds[2]>0 and not irq_unresolved,
            'coverage_complete':valid and all(kinds.values()) and not any(totals) and not unowned,
            'semantic_identity_proven':False}


def npc_exec_evidence(log, slot, last_tick, errors):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_EXEC tick=(\d+) ordinal=(\d+) parent=(\d+) depth=(\d+) key=({word}) call=(\d+) program=({word}) pc=(\d+) before=({word}) scoped=({word}) after=({word}) restored=({word}) cue=([^\s]+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_EXEC_STOP calls=(\d+) restored=(\d+) failures=(\d+) identity=([^\s]+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0','ordered_invocation_v1') and int(stop[0][0])==len(rows) and bool(rows)
    valid=valid and log.count('NATIVE_GATE NPC_EXEC tick=')==len(rows)
    previous=0;parents={};calls={};keys={}
    for ordinal,row in enumerate(rows,1):
        tick,sequence,parent,depth=map(int,row[:4]);key=int(row[4],16);call=int(row[5]);program=int(row[6],16);pc=int(row[7]);cue=row[12]
        named=bool(re.fullmatch(r'NPC_UseCases\.[A-Za-z0-9_.%\-]+',cue))
        expected=npc_exec_identity(cue,program,pc,tick,call) if named else None
        identity=(cue,program,pc);count=calls.get((tick,key),0)+1
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and 1<=depth<=64 and \
            ((not parent and depth==1) or parents.get(parent)==(tick,depth-1)) and \
            0<program and pc<1048576 and expected==(key,int(row[9],16)) and row[8]==row[11] and \
            call==count and keys.get(key,identity)==identity
        parents[sequence]=(tick,depth);calls[tick,key]=count;keys[key]=identity;previous=tick
    if not valid:errors.append(f'player {slot} experimental NPC execution scope incomplete, incorrectly seeded or not restored')
    return rows


def npc_exec_payloads(rows):
    # Caller RNG remains audited/restored above. The experiment intentionally
    # replaces that input before execution; compare the scoped input AND output.
    return [row[:8]+row[9:11]+row[12:] for row in rows]


def npc_enqueue_evidence(log, slot, last_tick, errors, scoped=False):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_ENQUEUE tick=(\d+) ordinal=(\d+) action=(\d+) exec=(\d+) saved=({word}) priority=(-?\d+) interrupt=(\d+) recipients=({word})/({word}) flags=([0-9a-f]+) count=(\d+) result=(\d+) rng=({word})/({word}) lines=([^\s]+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_ENQUEUE_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows) and bool(rows)
    valid=valid and log.count('NATIVE_GATE NPC_ENQUEUE tick=')==len(rows)
    actions={int(m[2]):int(m[1]) for m in re.finditer(r'NATIVE_GATE NPC_ACTION tick=(\d+) ordinal=(\d+) ',log)}
    executions={int(m[2]):int(m[1]) for m in re.finditer(r'NATIVE_GATE NPC_EXEC tick=(\d+) ordinal=(\d+) ',log)}
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence,action,execution=map(int,row[:4]);count=int(row[10]);lines=row[14].split(',')
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and action>0 and actions.get(action)==tick and \
            ((scoped and execution and executions.get(execution)==tick) or not execution) and int(row[4],16)>0 and \
            -(1<<31)<=int(row[5])<(1<<31) and row[6] in ('0','1') and row[11] in ('0','1') and int(row[9],16)<=0xffffffff and \
            0<=count<=64 and ((not count and row[14]=='-') or (count==len(lines) and all(re.fullmatch(r'[0-9a-f]{8}/[0-9a-f]{8}',line) for line in lines)))
        previous=tick
    if not valid:errors.append(f'player {slot} native NPC speech enqueue trace incomplete, unowned or not restored')
    return rows


def npc_speak_evidence(log, slot, last_tick, errors, scoped=False, enqueues=None):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_SPEAK tick=(\d+) ordinal=(\d+) action=(\d+) exec=(\d+) saved=({word}) entity=({word}) caller=([0-9a-f]+) clock=({word}) state=(\d+)/(\d+) handle=([0-9a-f]+)/([0-9a-f]+) page=(\d+)/(\d+) line=(\d+)/(\d+) duration=({word})/({word}) start=({word})/({word}) rng=({word})/({word}) enqueue=(\d+) origin=(\d+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_SPEAK_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows) and bool(rows)
    valid=valid and log.count('NATIVE_GATE NPC_SPEAK tick=')==len(rows)
    coverage=re.findall(r'NATIVE_GATE NPC_SPEAK_COVERAGE seen=(\d+) foreign_thread=(\d+) unowned=(\d+)',log)
    valid=valid and len(coverage)==1 and coverage[0][1]=='0' and int(coverage[0][0])==len(rows)+int(coverage[0][2])
    if enqueues is None:enqueues=npc_enqueue_evidence(log,slot,last_tick,errors,scoped)
    requests={int(row[1]):row for row in enqueues}
    actions={int(m[2]):int(m[1]) for m in re.finditer(r'NATIVE_GATE NPC_ACTION tick=(\d+) ordinal=(\d+) ',log)}
    executions={int(m[2]):int(m[1]) for m in re.finditer(r'NATIVE_GATE NPC_EXEC tick=(\d+) ordinal=(\d+) ',log)}
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence,action,execution=map(int,row[:4]);saved=int(row[4],16)
        request=requests.get(int(row[22]));origin=int(row[23])
        times=[struct.unpack('<d',int(row[i],16).to_bytes(8,'little'))[0] for i in (7,16,17,18,19)]
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and saved>0 and int(row[5],16)>0 and \
            0<int(row[6],16)<0x6fa9000 and all(math.isfinite(value) for value in times) and \
            all(int(row[i])<=0xffffffff for i in (8,9,12,13,14,15)) and \
            all(int(row[i],16)<=0xffffffff for i in (10,11)) and \
            (not action or actions.get(action)==tick) and \
            ((scoped and execution and executions.get(execution)==tick) or not execution) and \
            request is not None and int(request[0])<=tick and request[2]==str(origin) and request[4]==row[4] and request[11]=='1'
        previous=tick
    if not valid:errors.append(f'player {slot} native NPC speech selection trace incomplete, unowned or not restored')
    return rows


def npc_speak_payloads(rows, selection_only=False):
    # Runtime entity IDs and audio handles are process-local auxiliary evidence.
    # Preserve every timing/RNG field in the strict comparison. The narrower
    # projection helps diagnose selected page/line/duration independently.
    indices=(0,1,2,3,4,6,8,9,12,13,14,15,16,17,22,23) if selection_only else tuple(i for i in range(24) if i not in (5,10,11))
    return [tuple(row[i] for i in indices) for row in rows]


def npc_finish_evidence(log, slot, last_tick, errors, starts):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_FINISH tick=(\d+) ordinal=(\d+) enqueue=(\d+) origin=(\d+) saved=({word}) caller=([0-9a-f]+) interrupted=(\d+) audio=(\d+) state=(\d+) handle=([0-9a-f]+) page=(\d+) line=(\d+) duration=({word}) start=({word}) clock=({word}) flags=(\d+)/(\d+)/(\d+) rng=({word})/({word})',log)
    stop=re.findall(r'NATIVE_GATE NPC_FINISH_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_FINISH_COVERAGE seen=(\d+) foreign_thread=(\d+) unowned=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows) and bool(rows)
    valid=valid and len(coverage)==1 and coverage[0][1]=='0' and int(coverage[0][0])==len(rows)+int(coverage[0][2])
    valid=valid and log.count('NATIVE_GATE NPC_FINISH tick=')==len(rows)
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence,enqueue,origin=map(int,row[:4])
        # Pinned tick state3 writes audio-object duration to SpeakData+18 at
        # 563fb7. It may legitimately replace the initial estimate recorded by
        # SpeakNextLine. Preserve/compare finish duration; do not require it to
        # equal that estimate when checking the request's ownership.
        matching=any(int(start[0])<=tick and start[22]==str(enqueue) and start[23]==str(origin) and start[4]==row[4] and
                     (start[13],start[15],start[19])==(row[10],row[11],row[13]) for start in starts)
        times=[struct.unpack('<d',int(row[i],16).to_bytes(8,'little'))[0] for i in (12,13,14)]
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and matching and int(row[4],16)>0 and \
            0<int(row[5],16)<0x6fa9000 and row[6] in ('0','1') and int(row[7])<=255 and \
            all(int(row[i])<=0xffffffff for i in (8,10,11)) and int(row[9],16)<=0xffffffff and \
            all(int(row[i])<=255 for i in (15,16,17)) and all(math.isfinite(value) for value in times)
        previous=tick
    if not valid:errors.append(f'player {slot} native NPC speech finish trace incomplete, unowned or not restored')
    # The audio handle itself is local; provenance and all semantic state remain.
    return [row[:9]+row[10:] for row in rows]


def npc_ready_evidence(log, slot, last_tick, errors, starts):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_READY tick=(\d+) ordinal=(\d+) enqueue=(\d+) origin=(\d+) saved=({word}) start_event=(\d+) generation=(\d+) page=(\d+) line=(\d+) duration=({word}) estimate=({word}) start=({word}) clock=({word}) repeat=(\d+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_READY_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_READY_COVERAGE seen=(\d+) foreign=(\d+) foreign_thread=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows)
    valid=valid and len(coverage)==1 and coverage[0][2]=='0' and int(coverage[0][0])==len(rows)+int(coverage[0][1])
    valid=valid and log.count('NATIVE_GATE NPC_READY tick=')==len(rows)
    counts={};generations={};durations={};previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence=int(row[0]),int(row[1]);event=row[5]
        owner=[a for a in starts if a[1]==event and (a[22],a[23],a[4],a[13],a[15],a[19])==
               (row[2],row[3],row[4],row[7],row[8],row[11]) and int(a[0])<=tick]
        counts[event]=counts.get(event,0)+1;generations.setdefault(event,row[6]);durations.setdefault(event,row[9])
        times=[struct.unpack('<d',int(row[i],16).to_bytes(8,'little'))[0] for i in (9,10,11,12)]
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and len(owner)==1 and int(row[6])>0 and \
            int(row[13])==counts[event] and generations[event]==row[6] and durations[event]==row[9] and \
            all(math.isfinite(v) for v in times) and times[0]>0 and times[3]>=times[2]
        previous=tick
    if not valid:errors.append(f'player {slot} native NPC readiness trace incomplete, changed, unowned or not restored')
    return rows


def npc_deadline_evidence(log, slot, last_tick, errors, starts, finishes, readiness):
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE NPC_DEADLINE tick=(\d+) ordinal=(\d+) enqueue=(\d+) origin=(\d+) saved=({word}) page=(\d+) line=(\d+) duration=({word}) start=({word}) clock=({word}) early=(\d+)',log)
    stop=re.findall(r'NATIVE_GATE NPC_DEADLINE_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    coverage=re.findall(r'NATIVE_GATE NPC_DEADLINE_COVERAGE seen=(\d+) foreign=(\d+) foreign_thread=(\d+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows)
    valid=valid and len(coverage)==1 and coverage[0][2]=='0' and int(coverage[0][0])==len(rows)+int(coverage[0][1])
    valid=valid and log.count('NATIVE_GATE NPC_DEADLINE tick=')==len(rows)
    def number(value):return struct.unpack('<d',int(value,16).to_bytes(8,'little'))[0]
    def finished(start):
        return [f for f in finishes if (f[2],f[3],f[4],f[9],f[10],f[12])==
                (start[22],start[23],start[4],start[13],start[15],start[19]) and int(f[0])>=int(start[0])]
    # A bounded experiment ending with an active line is inconclusive, not a
    # complete lifecycle proof. Explicit interruptions remain valid native exits.
    for start in starts:
        matches=finished(start)
        valid=valid and len(matches)==1
        if len(matches)==1 and matches[0][6]=='0':
            f=matches[0];duration,begin,end=map(number,(f[11],f[12],f[13]))
            ready=[r for r in readiness if r[5]==start[1] and int(r[0])<=int(f[0]) and r[9]==f[11]]
            valid=valid and bool(ready) and duration>0 and end>=begin+duration
    previous=0
    for ordinal,row in enumerate(rows,1):
        tick,sequence=map(int,row[:2]);duration,begin,now=map(number,row[7:10])
        owner=[a for a in starts if (a[22],a[23],a[4],a[13],a[15],a[19])==
               (row[2],row[3],row[4],row[5],row[6],row[8]) and int(a[0])<=tick]
        valid=valid and previous<=tick<=last_tick and sequence==ordinal and len(owner)==1 and \
            all(math.isfinite(v) for v in (duration,begin,now)) and duration>0 and now>=begin and \
            row[10] in ('0','1') and (row[10]=='1')==(now<begin+duration)
        if len(owner)==1:
            ready=[r for r in readiness if r[5]==owner[0][1] and int(r[0])<=tick and r[9]==row[7]]
            valid=valid and bool(ready)
            matches=finished(owner[0])
            valid=valid and len(matches)==1
            if len(matches)==1:
                f=matches[0]
                valid=valid and int(f[0])>=tick and f[11]==row[7] and \
                    (f[6]=='1' or (f[5] in ('563f7b','564119') and number(f[13])>=begin+duration))
        previous=tick
    if not valid:errors.append(f'player {slot} native NPC deadline experiment incomplete, premature, unowned or not restored')
    return rows


def interrupt_rng_evidence(log, slot, errors):
    rows=re.findall(r'NATIVE_GATE INTERRUPT (tick=[^\r\n]+)',log)
    rows=[re.sub(r' ordinal=\d+| values=[^ ]+', '', row) for row in rows if ' kind=1 ' in row]
    count=re.findall(r'NATIVE_GATE INTERRUPT_SEEDS count=(\d+)',log)
    if len(count)!=1 or int(count[0])!=len(rows):
        errors.append(f'player {slot} native interrupt RNG scope count incomplete')
    return rows


def damage_evidence(log, slot, last_tick, errors):
    stop=re.findall(r'NATIVE_GATE DAMAGE_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    word=r'[0-9a-f]{16}'
    rows=re.findall(rf'NATIVE_GATE DAMAGE id=(\d+) tick=(\d+) ordinal=(\d+) time=({word}) amount=({word}) absorbed=({word}) priority=(-?\d+) before=({word},{word}) after=({word},{word}) stack=([0-9a-f,]+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(rows);previous=0
    for ordinal,row in enumerate(rows,1):
        actor,tick,sequence=map(int,row[:3]);stack=row[9].split(',')
        valid=valid and actor in ship_ids(log) and previous<=tick<=last_tick and sequence==ordinal and len(stack)==8 and all(int(v,16)<0x6fa9000 for v in stack)
        previous=tick
    if not valid:errors.append(f'player {slot} native damage trace incomplete or not restored')
    return len(rows)


def damage_payloads(log):
    # Caller RVAs aid diagnosis; gameplay inputs/results are the equality gate.
    return re.findall(r'NATIVE_GATE DAMAGE (id=\d+ tick=\d+ ordinal=\d+ time=[0-9a-f]{16} amount=[0-9a-f]{16} absorbed=[0-9a-f]{16} priority=-?\d+ before=[0-9a-f]{16},[0-9a-f]{16} after=[0-9a-f]{16},[0-9a-f]{16}) stack=',log)


def shape_initialization_evidence(log, slot, dock_shapes, extra_shapes, errors):
    stop=re.findall(r'NATIVE_GATE SHAPE_STOP calls=(\d+) restored=(\d+) failures=(\d+) initial_refreshes=(\d+)',log)
    totals=re.findall(r'NATIVE_GATE SHAPE_INIT_TOTALS data=([^\r\n]+)',log)
    try:
        if len(stop)!=1 or len(totals)!=1 or stop[0][1:3]!=('1','0'):raise ValueError()
        if not re.fullmatch(r'\d+/\d+/\d+(,\d+/\d+/\d+)*',totals[0]):raise ValueError()
        rows=[tuple(map(int,row.split('/'))) for row in totals[0].split(',')]
        expected={(actor,body) for actor in ship_ids(log)|({13,14} if dock_shapes else set()) for body in (0,1)}
        if extra_shapes:
            extra=[r for r in rows if r[0] in (13,14) and 2<=r[1]<=16]
            footer=re.findall(r'NATIVE_GATE DOCK_SHAPE_STOP registered=2 extras=(\d+) extra_calls=(\d+)',log)
            if not dock_shapes or len(footer)!=1 or {r[0] for r in extra}!={13,14}:raise ValueError()
            if int(footer[0][0])!=len(extra) or int(footer[0][1])<len(extra):raise ValueError()
            expected.update((r[0],r[1]) for r in extra)
        if len(rows)!=len(expected) or {(r[0],r[1]) for r in rows}!=expected:raise ValueError()
        if any(r[2]<1 for r in rows if r[0]<=12):raise ValueError()
        if sum(r[2] for r in rows)!=int(stop[0][3]) or int(stop[0][3])>int(stop[0][0]):raise ValueError()
        if dock_shapes and (re.findall(r'NATIVE_GATE DOCK_SHAPE_STOP registered=(\d+)',log)!=['2'] or
                            any(sum(r[2] for r in rows if r[0]==actor)<1 for actor in (13,14))):raise ValueError()
    except (ValueError,IndexError):
        errors.append(f'player {slot} shape initialization/coverage trace is incomplete')


def dock_refresh_evidence(log, slot, errors):
    rows=re.findall(r'NATIVE_GATE DOCK_REFRESH_STOP calls=(\d+) forced=(\d+)',log)
    if len(rows)!=1 or int(rows[0][0])<1 or rows[0][0]!=rows[0][1]:
        errors.append(f'player {slot} dock refresh cadence did not cover every registered body call')


def snapshot_fixture_evidence(log, config, slot, last_tick, errors):
    manifest=re.search(r'native_gate_snapshot_fixture\s*=\s*\{([^}\r\n]+)\}',config)
    ids=re.findall(r"['\"]([0-9a-f]{16})['\"]",manifest[1]) if manifest else []
    rows=re.findall(r'NATIVE_GATE SNAPSHOT_ACTOR ([^\r\n]+)',log)
    stop=re.findall(r'NATIVE_GATE SNAPSHOT_BIND_STOP bound=(\d+) prepared=(\d+) enrolled=(\d+) failures=(\d+)',log)
    enroll=re.findall(r'NATIVE_GATE SNAPSHOT_ENROLL id=(\d+) saved=([0-9a-f]{16})',log)
    valid=len(ids)==12 and len(set(ids))==12 and len(rows)==12 and len(stop)==1 and stop[0][0:2]==('12','12') and stop[0][3]=='0'
    valid=valid and int(stop[0][2])==len(enroll) and enroll==[(str(i+1),saved) for i,saved in enumerate(ids[:len(enroll)])]
    # A differential failure on frame 1 can stop before the queued MD event.
    # Any run reaching frame 4 must prove all later authoritative enrollments.
    if last_tick>=4:valid=valid and stop[0][2]=='12' and enroll==[(str(i+1),saved) for i,saved in enumerate(ids)]
    payloads=[];runtimes=set()
    mask=(1<<64)-1
    def mix(x):
        x=((x^(x>>30))*0xbf58476d1ce4e5b9)&mask;x=((x^(x>>27))*0x94d049bb133111eb)&mask
        return x^(x>>31)
    for ordinal,line in enumerate(rows,1):
        fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
        try:
            required={'id','saved','runtime','flight','bound_tick','prepared_tick','nav_tick','nav_calls','initialized','time','deadline_before','deadline_after','rng_before','rng_after'}
            if set(fields)!=required or fields['id']!=str(ordinal) or fields['saved']!=ids[ordinal-1] or fields['bound_tick']!='0' or fields['prepared_tick']!='1':raise ValueError()
            for key in ('saved','runtime','time','deadline_before','deadline_after','rng_before','rng_after'):
                if not re.fullmatch('[0-9a-f]{16}',fields[key]):raise ValueError()
            if int(fields['runtime'],16)==0 or fields['runtime'] in runtimes:raise ValueError()
            runtimes.add(fields['runtime'])
            if fields['flight'] not in ('none','spline','steering','disabled'):raise ValueError()
            if re.search(r'native_gate_nav_diag\s*=\s*true',config) and (fields['nav_tick']!='1' or fields['nav_calls']!='1'):raise ValueError()
            if re.search(r'native_gate_nav_initial_refresh\s*=\s*true',config) and fields['initialized']!='1':raise ValueError()
            if re.search(r'native_gate_nav_rng\s*=\s*true',config):
                seed=mix(0x58344c5345454432^0x4e41565f55504454)
                for value in (1,ordinal,0,1,0):seed=mix(seed^value)
                if fields['rng_before']!=f'{seed:016x}':raise ValueError()
            payloads.append({k:v for k,v in fields.items() if k!='runtime'})
        except (ValueError,KeyError,IndexError):valid=False
    if not valid:errors.append(f'player {slot} snapshot prebinding/first navigation/enrollment evidence invalid')
    return payloads


def snapshot_navigation_results(trace):
    # The explicitly enabled first-refresh policy replaces the loaded native
    # deadline by running its normal refresh. Retain that unequal preimage in
    # the input diagnostic; validate the seed/time and actual result separately.
    # The strict world hash still includes the resulting native deadline bytes.
    return [{key:value for key,value in row.items() if key!='deadline_before'} for row in trace]


def shape_lifetime_evidence(log, slot, last_tick, errors, require_holder_id=False):
    stop=re.findall(r'NATIVE_GATE SHAPE_LIFETIME_STOP calls=(\d+) failures=(\d+)',log)
    lines=re.findall(r'NATIVE_GATE SHAPE_LIFETIME ([^\r\n]+)',log)
    valid=len(stop)==1 and stop[0][1]=='0' and int(stop[0][0])==len(lines) and bool(lines)
    previous=0;identities={};identity_width=None
    for ordinal,line in enumerate(lines,1):
        fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
        try:
            if set(fields)!={'tick','ordinal','id','which','seen','initialized','due_before','before','after'}:raise ValueError()
            tick=int(fields['tick']);actor=int(fields['id']);which=int(fields['which'])
            if not(previous<=tick<=last_tick and fields['ordinal']==str(ordinal) and actor in ship_ids(log)|{13,14} and 0<=which<=(16 if actor in (13,14) else 1)):raise ValueError()
            if fields['seen'] not in ('0','1') or fields['initialized'] not in ('0','1') or not re.fullmatch('[0-9a-f]{16}',fields['due_before']):raise ValueError()
            for key in ('before','after'):
                if not re.fullmatch(r'[01]:[0-9a-f]{1,16}:[0-9a-f]{1,16}:[0-9a-f]{8}(?::[0-9a-f]{16})?',fields[key]):raise ValueError()
                parts=fields[key].split(':')
                if identity_width is None:identity_width=len(parts)
                if len(parts)!=identity_width or (require_holder_id and len(parts)!=5):raise ValueError()
                if len(parts)==5 and bool(int(parts[2],16))!=bool(int(parts[4],16)):raise ValueError()
            key=(actor,which)
            empty='0:0:0:00000000'+(':0000000000000000' if identity_width==5 else '')
            if fields['before']!=identities.get(key,empty) or not fields['after'].startswith('1:') or fields['before']==fields['after']:raise ValueError()
            identities[key]=fields['after'];previous=tick
        except (ValueError,KeyError):valid=False
    if not valid:errors.append(f'player {slot} shape lifetime trace incomplete or inconsistent')
    return len(lines)


def contact_evidence(log, slot, last_tick, errors):
    stop=re.findall(r'NATIVE_GATE CONTACT_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    lines=re.findall(r'NATIVE_GATE CONTACT ([^\r\n]+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(lines)
    payloads=[];previous=0
    for ordinal,line in enumerate(lines,1):
        fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
        try:
            required={'tick','ordinal','added','count','subshape','base','normal','depth','body0','body1','points0','points1','settings0','settings1'}
            optional=set(fields)-required
            if not required<=set(fields) or optional not in (set(),{'shape_due0','shape_due1'},{'shape_due0','shape_due1','record0','record1'}):raise ValueError()
            for key in ('shape_due0','shape_due1'):
                if key in fields and not re.fullmatch('[0-9a-f]{16}',fields[key]):raise ValueError()
            for key in ('record0','record1'):
                if key in fields and fields[key] not in ('0','1','2'):raise ValueError()
            tick=int(fields['tick']);count=int(fields['count'])
            if not(previous<=tick<=last_tick and int(fields['ordinal'])==ordinal and fields['added'] in ('0','1') and 0<=count<=64):raise ValueError()
            if not re.fullmatch('[0-9a-f]{8},[0-9a-f]{8}',fields['subshape']):raise ValueError()
            for key,width in (('base',24),('normal',24),('depth',8)):
                if not re.fullmatch('[0-9a-f]{'+str(width)+'}',fields[key]):raise ValueError()
            actors=[]
            for i in range(2):
                body=fields[f'body{i}'].split(':')
                if len(body)!=7 or not body[0].isdigit() or not int(body[0]) in ship_ids(log)|{0}:raise ValueError()
                if not re.fullmatch('[0-9a-f]{8}',body[1]) or not all(re.fullmatch('[0-9a-f]+',v) for v in body[2:5]):raise ValueError()
                if not re.fullmatch('[0-9a-f]{56}',body[5]) or not re.fullmatch('[0-9a-f]{48}',body[6]):raise ValueError()
                if not re.fullmatch('[0-9a-f]{'+str(count*24)+'}',fields[f'points{i}']):raise ValueError()
                if not re.fullmatch('[0-9a-f]{48}:0[01]:[0-9a-f]{48}',fields[f'settings{i}']):raise ValueError()
                actors.append(int(body[0]))
                # Body/component IDs are local identities. Compare actor, native
                # types, exact pose/velocity and all actual contact inputs.
                fields[f'body{i}']=':'.join([body[0],*body[3:]])
            if not any(actors):raise ValueError()
            previous=tick;payloads.append(fields)
        except (ValueError,KeyError):valid=False
    if not valid:errors.append(f'player {slot} native contact trace incomplete or not restored')
    return payloads


def contact_multiset(trace):
    # Diagnostic only. Keep tick and multiplicity; omit callback sequence to
    # distinguish reordered callbacks from changed physical inputs. Ordered
    # equality remains the acceptance gate until ordering effects are resolved.
    return Counter(tuple(sorted((k,v) for k,v in row.items() if k!='ordinal')) for row in trace)


def contact_order_evidence(log, slot, contacts, errors):
    """Validate a parallel, read-only traversal trace without changing contact equality."""
    lines=re.findall(r'NATIVE_GATE CONTACT_ORDER ([^\r\n]+)',log)
    valid=len(lines)==len(contacts);result=[]
    for ordinal,line in enumerate(lines,1):
        fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
        try:
            if set(fields)!={'tick','ordinal','body0','body1'}:raise ValueError()
            if fields['ordinal']!=str(ordinal) or fields['tick']!=contacts[ordinal-1]['tick']:raise ValueError()
            for key in ('body0','body1'):
                if not re.fullmatch(r'[0-9a-f]{8}:[0-9a-f]{8}:[012]:[0-9a-f]{4}:0[01]:[0-9a-f]{2}:0[012]:[0-9a-f]{2}',fields[key]):raise ValueError()
            result.append(fields)
        except (ValueError,KeyError,IndexError):valid=False
    if not valid:errors.append(f'player {slot} native contact traversal trace incomplete or malformed')
    return result


def contact_properties_evidence(log, slot, last_tick, errors):
    """Body lifecycle diagnostics are complete but are not a portable state hash."""
    stop=re.findall(r'NATIVE_GATE CONTACT_PROPERTIES_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    lines=re.findall(r'NATIVE_GATE CONTACT_PROPERTIES ([^\r\n]+)',log)
    valid=len(stop)==1 and stop[0][1:]==('1','0') and int(stop[0][0])==len(lines)
    previous=0
    for ordinal,line in enumerate(lines,1):
        fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
        try:
            if set(fields)!={'tick','ordinal','id','before0','before1','after0','after1','stack'}:raise ValueError()
            tick=int(fields['tick'])
            if not(previous<=tick<=last_tick and fields['ordinal']==str(ordinal) and int(fields['id']) in ship_ids(log)):raise ValueError()
            for key in ('before0','before1','after0','after1'):
                if fields[key]!='none' and not re.fullmatch(r'[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{8}:[012]:[0-9a-f]{4}:0[012]:0[01]:[0-9a-f]{8}:[0-9a-f]{8}:[0-9a-f]{2}',fields[key]):raise ValueError()
            if not re.fullmatch(r'[0-9a-f]{1,7}(?:,[0-9a-f]{1,7}){7}',fields['stack']):raise ValueError()
            previous=tick
        except (ValueError,KeyError):valid=False
    if not valid:errors.append(f'player {slot} native contact property trace incomplete or not restored')
    return len(lines)


def gravidar_evidence(log, slot, last_tick, errors):
    rows=re.findall(r'NATIVE_GATE GRAVIDAR_STOP frames=(\d+) waits=(\d+) timeouts=(\d+) calls=(\d+) restored=(\d+) failures=(\d+)',log)
    if len(rows)!=1 or int(rows[0][0])!=last_tick or int(rows[0][1])>2*last_tick or rows[0][2]!='0' or int(rows[0][3])==0 or rows[0][4:]!=('1','0'):
        errors.append(f'player {slot} native radar phase join failed, was incomplete or not restored')
        return None
    return dict(zip(('frames','waits','timeouts','calls','restored','failures'),map(int,rows[0])))


def gravidar_trace(log, slot, last_tick, total, errors):
    word=r'[0-9a-f]{16}'
    times=word+','+word+','+word
    pattern=rf'NATIVE_GATE GRAVIDAR_UNIT id=(\d+)(?: final=1)? tick=(\d+) calls=(\d+) time=({word}) before=({times}) after=({times}) rng_before=({word}) rng_after=({word})'
    records={};valid=True
    for row in re.findall(pattern,log):
        actor,tick,calls=map(int,row[:3])
        if not calls:continue
        key=(actor,tick);value=(calls,)+row[3:]
        valid=valid and actor in ship_ids(log) and 0<tick<=last_tick and (key not in records or records[key]==value)
        records[key]=value
    if not valid or total is None or sum(row[0] for row in records.values())!=total:
        errors.append(f'player {slot} native radar scan trace incomplete or inconsistent')
    return records


def gravidar_contact_trace(log, slot, scans, errors):
    """Validate complete read-only snapshots; local IDs are not portable hashes.

    Stages 1/2 are old primary/secondary, 3/4 input, 5/6 applied, and 7/8
    native added-contact sets. Absent optional vectors differ from empty ones.
    More than one apply per actor/frame would overwrite a sample and refuses.
    """
    records={};valid=True
    lines=re.findall(r'NATIVE_GATE GRAVIDAR_CONTACTS ([^\r\n]*)',log)
    pattern=r'id=(\d+) tick=(\d+) calls=(\d+) stage=(\d+) present=([01]) count=(\d+) ids=([-0-9a-f/,]+)'
    for line in lines:
        match=re.fullmatch(pattern,line)
        if not match:valid=False;continue
        actor,tick,calls,stage,present,count=map(int,match.groups()[:6])
        raw=match[7];ids=[]
        if raw!='-':
            for token in raw.split(','):
                item=re.fullmatch(r'([0-9a-f]{16})/(\d+)',token)
                if not item:valid=False;continue
                identity,mapped=int(item[1],16),int(item[2]);ids.append((identity,mapped))
                valid=valid and identity>0 and mapped in ship_ids(log)|{0}
        key=(actor,tick,stage);scan=scans.get((actor,tick))
        valid=valid and scan is not None and calls==1 and scan[0]==calls and 1<=stage<=8 and \
            0<=count<=256 and count==len(ids) and (present==1 or count==0) and \
            (stage not in (1,3,4,5,7) or present==1) and key not in records
        records[key]=(present,tuple(ids))
    valid=valid and len(records)==8*len(scans) and all(
        (actor,tick,stage) in records for actor,tick in scans for stage in range(1,9))
    if not valid:errors.append(f'player {slot} native radar contact snapshots incomplete or inconsistent')
    return records


def radar_payloads(log):
    # The dispatcher's third register is unused by this pinned native handler;
    # its diagnostic flags value is not semantic event data.
    return re.findall(r'NATIVE_GATE RADAR id=(\d+) tick=(\d+) ordinal=(\d+) source=(\d+) flags=\d+ script=([!-~]+)',log)


def radar_evidence(log, slot, last_tick, errors):
    stop=re.findall(r'NATIVE_GATE RADAR_STOP calls=(\d+) restored=(\d+) failures=(\d+)',log)
    rows=re.findall(r'NATIVE_GATE RADAR id=(\d+) tick=(\d+) ordinal=(\d+) source=(\d+) flags=(\d+) script=([!-~]+)',log)
    valid=len(stop)==1 and stop[0][1:] == ('1','0') and int(stop[0][0])==len(rows)
    previous=0
    for ordinal,row in enumerate(rows,1):
        actor,tick,sequence,source,flags=map(int,row[:5])
        valid=valid and actor in ship_ids(log) and previous<=tick<=last_tick and sequence==ordinal and source in ship_ids(log)|{0} and flags<=0xffffffff and len(row[5])<96
        previous=tick
    if not valid:errors.append(f'player {slot} radar event trace missing, malformed or not restored')
    return len(rows)


def fleet_evidence(samples,states,slot,errors):
    histories={actor:[] for actor in range(1,13)}
    for tick,data in sorted(samples.items()):
        for row in data.split('|')[1:]:
            m=re.search(r'^(\d+)/.*?/fleet:FLEET_1;(\d+);([\w.\-]+);(\d+)(?:/|$)',row)
            if m and int(m[1]) in histories:
                histories[int(m[1])].append((tick,int(m[2]),m[3],int(m[4])))
    commands=[]
    for tick,players in sorted(states.items()):
        if tick+1 not in states:continue
        for player in ('1','2'):
            value=players[player].get('command','00000000')
            if value!='00000000':commands.append((tick+1,int(value[:2],16),int(value[2:4],16),int(value[4:6],16)))
    changes=0
    for index,(tick,op,actor,target) in enumerate(commands):
        if op not in (6,7):continue
        changes+=1;wanted=target if op==6 else 0
        end=next((t for t,_,a,_ in commands[index+1:] if a==actor),max(states)+1)
        if not any(tick<=t<end and commander==wanted and assignment==('defence' if wanted else 'none')
                   for t,commander,assignment,_ in histories[actor]):
            errors.append(f'player {slot} native fleet change never observed at {tick}/{actor}')
    return changes


def docking_evidence(samples, states, slot, errors, require_completion=True):
    histories={actor:[] for actor in range(1,13)}
    for tick,data in sorted(samples.items()):
        rows=data.split('|')[1:]
        if len(rows)!=12:
            errors.append(f'player {slot} incomplete docking frame {tick}');continue
        occupancy={}
        for anchor,row in enumerate(rows[:2],1):
            match=re.search(r'/anchor:([^/|]+)',row)
            try:
                if not match:raise ValueError('missing anchor')
                if match[1]=='pending' and tick<=7:continue
                fields=match[1].split(';')
                if len(fields)!=9 or fields[:2]!=['ANCHOR_1',str(anchor)]:raise ValueError('anchor fields')
                if fields[2]!='station_gen_factory_base_01_macro' or fields[3] not in ('argon','xenon'):raise ValueError('anchor identity')
                health=list(map(float,fields[4:6]));pose=list(map(float,fields[6].split(',')))
                if len(pose)!=6 or not all(math.isfinite(v) for v in health+pose) or not all(0<=v<=100 for v in health):raise ValueError('anchor values')
                count=int(fields[7]);members=[] if fields[8]=='none' else list(map(int,fields[8].split(',')))
                if count!=len(members) or members!=sorted(set(members)) or any(i not in range(1,13) for i in members):raise ValueError('anchor occupancy')
                occupancy[anchor]=set(members)
            except ValueError:errors.append(f'player {slot} invalid docking anchor {tick}/{anchor}')
        for actor,row in enumerate(rows,1):
            match=re.search(r'/dock:([^/|]+)',row)
            if '/dead/' in row:
                if not match or match[1]!='dead':errors.append(f'player {slot} invalid dead docking state {tick}/{actor}')
                continue
            if not match or not re.fullmatch(r'DOCK_1;[01];[01];[01];[012]',match[1]):
                errors.append(f'player {slot} incomplete native docking state {tick}/{actor}');continue
            docked,docking,stored,target=map(int,match[1].split(';')[1:])
            try:
                position=tuple(map(float,row.split('/')[1:4]))
                if len(position)!=3 or not all(math.isfinite(v) for v in position):raise ValueError('position')
            except ValueError:
                errors.append(f'player {slot} invalid docking position {tick}/{actor}');continue
            histories[actor].append((tick,docked,docking,stored,target,position,occupancy))
    commands=[]
    for tick,peers in sorted(states.items()):
        if tick+1 not in states:continue
        for player in (1,2):
            command=peers.get(str(player),{}).get('command','00000000')
            if command!='00000000':commands.append((tick+1,player,*[int(command[i:i+2],16) for i in (0,2,4)]))
    cycles=departures=0
    for index,(tick,player,op,actor,target) in enumerate(commands):
        if op!=9:continue
        following=[c for c in commands[index+1:] if c[3]==actor]
        end=following[0][0] if following else max(states)+1
        docked=[r for r in histories[actor] if tick<=r[0]<end and r[1]==1 and r[4]==target and actor in r[6].get(target,set())]
        if target!=player:errors.append(f'player {slot} foreign docking destination {tick}/{actor}')
        if not docked:
            if require_completion:errors.append(f'player {slot} commanded docking never completed {tick}/{actor}')
            continue
        cycles+=1
        if following and following[0][2] in (3,5):
            move=following[0][0];end=following[1][0] if len(following)>1 else max(states)+1
            before=next((r for r in histories[actor] if r[0]==move-1),None)
            if before is None or before[1]!=1 or before[4]!=target or actor not in before[6].get(target,set()):
                if require_completion:errors.append(f'player {slot} ship no longer docked at departure command {move}/{actor}')
                continue
            departure=[r for r in histories[actor] if move<=r[0]<end and r[1:5]==(0,0,0,0) and
                       actor not in r[6].get(target,set()) and math.dist(r[5],docked[-1][5])>=100]
            if departure:departures+=1
            elif require_completion:errors.append(f'player {slot} commanded departure never completed {move}/{actor}')
    if require_completion and not cycles:errors.append(f'player {slot} native docking was not exercised')
    return {'completed_dockings':cycles,'completed_departures':departures}


def validate(run, scope):
    # Valid negative evidence is useful too. session_pass additionally requires
    # checked gameplay equality and a clean runner outcome.
    session_result = json.loads((run/'result.json').read_text())
    barrier = json.loads((run/'native-barrier.json').read_text())
    if barrier.get('clients', 2) != 2 or barrier.get('protocol_version') in (3, 4):
        # The fixture checks below bind two players and protocol 2 commands.
        raise ValueError('validate_native_gate covers two-client protocol 1/2 runs only; use the census tools for wire 3/4 or N clients')
    states = {int(t): rows for t, rows in barrier['states'].items()}
    ticks = sorted(states)
    errors, stops, logs, samples, rng = [], [], [], [], []
    command_results = []
    travel_results, travel_active = [], []
    boost_results, boosted_actors = [], []
    spline_joins = []
    fleet_changes = []
    docking_results = []
    radar_events = []
    gravidar_results = []
    gravidar_traces = []
    damage_calls = []
    contact_traces = []
    contact_order_traces = []
    contact_property_calls = []
    shape_lifetime_calls = []
    interrupt_calls = []
    interrupt_rng_traces = []
    interrupt_step_traces = []
    nested_action_traces = []
    npc_action_traces = []
    npc_exec_traces = []
    npc_trigger_traces = []
    npc_provenance_traces = []
    npc_pilot_traces = []
    npc_speak_traces = []
    npc_enqueue_traces = []
    npc_finish_traces = []
    npc_deadline_traces = []
    npc_ready_traces = []
    world_identity_traces = []
    world_hash_traces = []
    world_hash_coverage = []
    world_wait_captures = []
    world_wait_rng_captures = []
    world_action_captures = []
    world_value_captures = []
    world_find_captures = []
    world_position_captures = []
    world_safe_position_captures = []
    world_position_rng_captures = []
    native_player_results = []
    native_transition_captures = []
    native_origin_captures = []
    native_player_observer_traces = []
    snapshot_fixture_traces = []
    native_player_fire_traces = []
    native_player_launch_traces = []
    registry_traces = []
    registry_navigation_traces = []
    native_solver_traces = []
    registry_md_traces = []
    creation_rng_traces = []
    registry_creation_traces = []
    native_request_traces = []
    if ticks != list(range(ticks[-1]+1)):
        errors.append('missing ticks')
    for slot in (1, 2):
        client = run/f'client{slot}'
        log = (client/'engine-output.log').read_text(errors='replace')
        logs.append(log)
        if re.search(r'Error in MD cue md\.X4LAN_Native(?:Gate|Commands)', log):
            errors.append(f'player {slot} native fixture MD script error')
        result = json.loads((client/'result.json').read_text())
        if not result.get('lane_restored'):
            errors.append(f'player {slot} lane not restored')
        found = re.findall(r'NATIVE_GATE STOP slot=p(\d) completed=(\d+) reason=(\d+) restored=(\d+) wait_game_delta=([\deE.+-]+) max_dt_error=([\deE.+-]+)', log)
        if len(found) != 1:
            errors.append(f'player {slot} missing unique native stop')
            continue
        s, completed, reason, restored, drift, dt_error = found[0]
        if (int(s), int(completed), int(reason), int(restored)) != (slot, ticks[-1], 1, 1) or float(drift) != 0 or float(dt_error) > 1e-8:
            errors.append(f'player {slot} native stop/clock check failed')
        stops.append({'player': slot, 'completed': int(completed), 'wait_clock_drift': float(drift), 'max_dt_error': float(dt_error)})
        config = (client/'installed/x4_lan_lab-config.lua').read_text()
        rate = re.search(r'native_gate_tick_hz\s*=\s*(\d+)', config)
        tick_hz = int(rate[1]) if rate else 60
        creation_rng_suffix=None
        try:creation_rng_suffix=creation_rng_bootstrap(config)
        except ValueError as exc:errors.append(f'player {slot} construction RNG bootstrap: {exc}')
        if re.search(r'native_gate_creation_trace_schema\s*=',config) and not re.search(r'native_gate_registry_md_trace\s*=\s*true',config):
            errors.append(f'player {slot} creation trace lacks native MD evidence dependency')
        if re.search(r'native_gate_registry_md_trace\s*=\s*true',config):
            origin_required=bool(re.search(r'native_gate_registry_origin_schema\s*=',config))
            if origin_required and not re.search(r'native_gate_registry_origin_schema\s*=\s*1\s*(?:\n|$)',config):
                errors.append(f'player {slot} unsupported registry origin schema')
            identity_required=bool(re.search(r'native_gate_registry_origin_identity_schema\s*=',config))
            if identity_required and not re.search(r'native_gate_registry_origin_identity_schema\s*=\s*1\s*(?:\n|$)',config):
                errors.append(f'player {slot} unsupported registry origin identity schema')
            if identity_required and not origin_required:
                errors.append(f'player {slot} registry origin identity requires origin capture')
            creation_required=bool(re.search(r'native_gate_creation_trace_schema\s*=',config))
            creation_schema=re.search(r'native_gate_creation_trace_schema\s*=\s*([12])\s*(?:\n|$)',config)
            if creation_required and (not creation_schema or not identity_required):
                errors.append(f'player {slot} creation trace schema/dependency invalid')
            if creation_schema:creation_required=int(creation_schema[1])
            registry_md_traces.append(registry_md_evidence(log,slot,errors,origin_required=origin_required,identity_required=identity_required,creation_required=creation_required))
        registry_traces.append(registry_evidence(log,slot,ticks[-1],errors))
        creation_rng_traces.append(creation_rng_evidence(log,config,slot,errors))
        registry_creation_traces.append(registry_creation_evidence(log,config,slot,errors,
            registry_md_traces[-1] if registry_md_traces else None))
        registry_navigation_traces.append(registry_navigation(log,slot,errors))
        if re.search(r'native_gate_snapshot_fixture\s*=',config):
            snapshot_fixture_traces.append(snapshot_fixture_evidence(log,config,slot,ticks[-1],errors))
        if re.search(r'native_gate_shape_lifetimes\s*=\s*true',config):
            shape_lifetime_calls.append(shape_lifetime_evidence(log,slot,ticks[-1],errors,
                bool(re.search(r'native_gate_shape_lifetime_ids\s*=\s*true',config))))
        if re.search(r'native_gate_interrupt_rng\s*=\s*true',config):
            interrupt_rng_traces.append(interrupt_rng_evidence(log,slot,errors))
        if re.search(r'native_gate_interrupt_diag\s*=\s*true',config):
            interrupt_calls.append(interrupt_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_interrupt_steps\s*=\s*true',config):
            interrupt_step_traces.append(interrupt_step_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_nested_actions\s*=\s*true',config):
            nested_action_traces.append(nested_action_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_npc_lifecycle\s*=\s*true',config):
            npc_action_traces.append(npc_action_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_npc_trigger\s*=\s*true',config):
            npc_trigger_traces.append(npc_trigger_evidence(log,slot,ticks[-1],errors,bool(re.search(r'native_gate_npc_rng\s*=\s*true',config))))
        if re.search(r'native_gate_npc_provenance\s*=\s*true',config):
            trigger_rows=npc_trigger_traces[-1]['rows'] if len(npc_trigger_traces)==slot else []
            npc_provenance_traces.append(npc_provenance_evidence(log,slot,ticks[-1],errors,trigger_rows,
                bool(re.search(r'native_gate_npc_provenance_origin\s*=\s*true',config))))
        pilot_config=re.search(r'native_gate_npc_pilot\s*=\s*([^\s;]+)',config)
        if pilot_config and pilot_config[1]!='false':
            if pilot_config[1]!='620203':errors.append(f'player {slot} selected NPC pilot configuration unsupported')
            npc_pilot_traces.append(npc_pilot_evidence(log,slot,ticks[-1],errors,
                bool(re.search(r'native_gate_npc_pilot_origin\s*=\s*true',config))))
        npc_scoped=bool(re.search(r'native_gate_npc_rng\s*=\s*true',config))
        if npc_scoped:
            npc_exec_traces.append(npc_exec_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_npc_diag\s*=\s*true',config):
            npc_enqueue_traces.append(npc_enqueue_evidence(log,slot,ticks[-1],errors,npc_scoped))
            npc_speak_traces.append(npc_speak_evidence(log,slot,ticks[-1],errors,npc_scoped,npc_enqueue_traces[-1]))
            if re.search(r'native_gate_npc_finish\s*=\s*true',config):
                npc_finish_traces.append(npc_finish_evidence(log,slot,ticks[-1],errors,npc_speak_traces[-1]))
            if re.search(r'native_gate_npc_deadline\s*=\s*true',config):
                npc_ready_traces.append(npc_ready_evidence(log,slot,ticks[-1],errors,npc_speak_traces[-1]))
                npc_deadline_traces.append(npc_deadline_evidence(log,slot,ticks[-1],errors,npc_speak_traces[-1],npc_finish_traces[-1] if npc_finish_traces else [],npc_ready_traces[-1]))
        if re.search(r'native_gate_world_identities\s*=\s*true',config):
            world_identity_traces.append(world_identity_evidence(log,slot,errors))
        if re.search(r'native_gate_damage_diag\s*=\s*true',config):
            damage_calls.append(damage_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_contact_diag\s*=\s*true',config):
            contact_traces.append(contact_evidence(log,slot,ticks[-1],errors))
            if re.search(r'native_gate_contact_order\s*=\s*true',config):
                contact_order_traces.append(contact_order_evidence(log,slot,contact_traces[-1],errors))
            if re.search(r'native_gate_contact_properties\s*=\s*true',config):
                contact_property_calls.append(contact_properties_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_radar_diag\s*=\s*true', config):
            radar_events.append(radar_evidence(log,slot,ticks[-1],errors))
        if re.search(r'native_gate_radar_serial\s*=\s*true', config):
            gravidar_results.append(gravidar_evidence(log,slot,ticks[-1],errors))
            gravidar_traces.append(gravidar_trace(log,slot,ticks[-1],gravidar_results[-1]['calls'] if gravidar_results[-1] else None,errors))
            if re.search(r'native_gate_radar_contacts\s*=\s*true',config):
                gravidar_contact_trace(log,slot,gravidar_traces[-1],errors)
        if re.search(r'native_gate_radar_initial_refresh\s*=\s*true',config) and not re.search(rf'NATIVE_GATE GRAVIDAR_INIT count={len(ship_ids(log))}(?:\r?\n|$)',log):
            errors.append(f'player {slot} radar deadline initialization count differs')
        schema = re.search(r'native_gate_rng_schema\s*=\s*(\d+)', config)
        if schema and re.findall(r'NATIVE_GATE RNG_SCHEMA version=(\d+)', log) != [schema[1]]:
            errors.append(f'player {slot} RNG schema mismatch or missing')
        snapshot = re.search(r'native_gate_snapshot_sha\s*=\s*"([0-9a-f]{64})"', config)
        if snapshot:
            info = result.get('native_snapshot', {})
            if not info.get('restored') or info.get('staged', {}).get('sha256') != snapshot[1]:
                errors.append(f'player {slot} snapshot staging/restoration failed')
            if 'NATIVE_GATE BOOT phase=3 dt_restored=1' not in log:
                errors.append(f'player {slot} boot fixed-dt restoration failed')
        serial = bool(re.search(r'native_gate_serial_pathing\s*=\s*true', config))
        seeded = bool(re.search(r'native_gate_seed_rng\s*=\s*true', config))
        flight = bool(re.search(r'native_gate_flight_diag\s*=\s*true', config))
        if flight:
            f = re.search(r'NATIVE_GATE FLIGHT_STOP calls=(\d+) restored=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1:
                errors.append(f'player {slot} flight observer did not run or restore')
        if re.search(r'native_gate_avoidance_refresh\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE FLIGHT_STOP calls=(\d+) restored=(\d+) failures=(\d+) refreshes=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0 or f[1] != f[4]:
                errors.append(f'player {slot} native avoidance refresh count/failures/restoration invalid')
        if re.search(r'native_gate_steering_init\s*=\s*true', config):
            initializations = steering_initializations(log)
            if initializations is None:
                errors.append(f'player {slot} steering initialization evidence is incomplete')
            elif slot == 2 and initializations != steering_initializations(logs[0]):
                errors.append('steering model generations differ between players')
            if re.search(r'native_gate_steering_schema\s*=\s*2', config):
                constructors = re.findall(r'NATIVE_GATE STEERING_CONSTRUCT calls=(\d+) restored=(\d+) registrations=(\d+)', log)
                if initializations is None or len(constructors) != 1 or constructors[0][1] != '1' or int(constructors[0][0])+int(constructors[0][2]) != sum(count for _, count in initializations):
                    errors.append(f'player {slot} steering constructor accounting or restoration failed')
        if re.search(r'native_gate_ai_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE AI_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} AI observer did not run or restore')
        if re.search(r'native_gate_agent_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE AGENT_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} pathing-agent observer did not run or restore cleanly')
        if re.search(r'native_gate_spline_diag\s*=\s*true', config):
            if re.search(r'native_gate_spline_join\s*=\s*true', config):
                spline_joins.append(spline_join_evidence(log,slot,errors))
            f = re.search(r'NATIVE_GATE SPLINE_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or f.groups()[1:] != ('1', '0'):
                errors.append(f'player {slot} spline observer did not run or restore cleanly')
            if re.search(r'native_gate_spline_refresh\s*=\s*true', config):
                f = re.search(r'NATIVE_GATE SPLINE_STOP calls=(\d+) restored=1 failures=0 refreshes=(\d+)', log)
                if not f or f[1] != f[2]:
                    errors.append(f'player {slot} native spline refresh count differs')
        if re.search(r'native_gate_ship_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE SHIP_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} ship observer did not run or restore cleanly')
        if re.search(r'native_gate_params_init\s*=\s*true', config):
            if not re.search(rf'NATIVE_GATE PARAMS_STOP calls={len(ship_ids(log))}(?:\r?\n|$)', log):
                errors.append(f'player {slot} movement parameter initialization count differs')
        if re.search(r'native_gate_weapon_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE WEAPON_STOP calls=(\d+) restored=(\d+) failures=(\d+) registered=(\d+)', log)
            registrations = re.findall(r'NATIVE_GATE WEAPON_REGISTER id=(\d+) slot=(\d+) macro=([^\s]+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0 or int(f[4]) != len(registrations):
                errors.append(f'player {slot} weapon observer did not register, run or restore cleanly')
            if not set(range(1,13)).issubset({int(row[0]) for row in registrations}) or not {int(row[0]) for row in registrations}.issubset(ship_ids(log)) or any(row[2] == 'nil' for row in registrations):
                errors.append(f'player {slot} weapon identities are incomplete')
            if slot == 2 and registrations != re.findall(r'NATIVE_GATE WEAPON_REGISTER id=(\d+) slot=(\d+) macro=([^\s]+)', logs[0]):
                errors.append('weapon physical slots or macros differ between players')
        if re.search(r'native_gate_params_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE PARAMS_TRACE_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} movement parameter observer did not run or restore cleanly')
        if re.search(r'native_gate_nav_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE NAV_STOP calls=(\d+) restored=(\d+) failures=(\d+) initial_refreshes=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} navigation observer did not run or restore cleanly')
            if re.search(r'native_gate_nav_initial_refresh\s*=\s*true', config) and (not f or int(f[4]) != len(ship_ids(log))):
                errors.append(f'player {slot} navigation initial refresh count differs')
        if re.search(r'native_gate_velocity_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE VELOCITY_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} velocity observer did not run or restore cleanly')
        if re.search(r'native_gate_shape_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE SHAPE_STOP calls=(\d+) restored=(\d+) failures=(\d+) initial_refreshes=(\d+)', log)
            if not f or int(f[1]) == 0 or int(f[2]) != 1 or int(f[3]) != 0:
                errors.append(f'player {slot} shape observer did not run or restore cleanly')
            if re.search(r'native_gate_shape_initial_refresh\s*=\s*true', config):
                shape_initialization_evidence(log,slot,
                    bool(re.search(r'native_gate_docking_shape_rng\s*=\s*true',config)),
                    bool(re.search(r'native_gate_docking_extra_shapes\s*=\s*true',config)),errors)
            if re.search(r'native_gate_docking_shape_refresh\s*=\s*true',config):dock_refresh_evidence(log,slot,errors)
        if re.search(r'native_gate_bullet_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE BULLET_STOP calls=(\d+) created=(\d+) restored=(\d+) failures=(\d+) unknown=(\d+) outside=(\d+) unsupported=(\d+)', log)
            rows = re.findall(r'NATIVE_GATE BULLET id=(\d+) slot=(\d+) tick=(\d+) spawn=(\d+) call=(\d+) created=([01]) ', log)
            if not f or min(int(f[1]), int(f[2])) == 0 or (f[3], f[4]) != ('1', '0') or int(f[7]) != 0:
                errors.append(f'player {slot} bullet observer did not run or restore cleanly')
            elif sum(r[-1] == '0' for r in rows) != int(f[1]) or sum(r[-1] == '1' for r in rows) != int(f[2]) or len(set(rows)) != len(rows):
                errors.append(f'player {slot} bullet event trace is incomplete or duplicated')
        if re.search(r'native_gate_death_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE DEATH_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            rows = re.findall(r'NATIVE_GATE DEATH id=(\d+) tick=(\d+) call=(\d+) ', log)
            if not f or (f[2], f[3]) != ('1', '0') or len(rows) != int(f[1]) or len(set(rows)) != len(rows):
                errors.append(f'player {slot} death observer evidence is incomplete or did not restore')
        if re.search(r'native_gate_mass_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE MASS_STOP calls=(\d+) restored=(\d+) failures=(\d+)', log)
            count = len(re.findall(r'NATIVE_GATE MASS id=', log))
            if not f or (f[2], f[3]) != ('1', '0') or count != int(f[1]) or count == 0:
                errors.append(f'player {slot} mass observer evidence is incomplete or did not restore')
        if re.search(r'native_gate_attention_diag\s*=\s*true', config):
            f = re.search(r'NATIVE_GATE ATTENTION_STOP calls=(\d+) restored=(\d+) failures=(\d+) initial_refreshes=(\d+)', log)
            if not f or int(f[1]) == 0 or (f[2], f[3]) != ('1', '0'):
                errors.append(f'player {slot} attention observer did not run or restore')
            if re.search(r'native_gate_attention_initial_refresh\s*=\s*true', config) and (not f or int(f[4]) != len(ship_ids(log))):
                errors.append(f'player {slot} attention initialization count differs')
        if serial or seeded:
            path = re.search(r'NATIVE_GATE PATH joins=(\d+) restored=(\d+)(?: rng_seeds=(\d+))?', log)
            if not path or int(path[2]) != 1 or (serial and int(path[1]) != int(completed)) or (seeded and int(path[3] or 0) != int(completed)):
                errors.append(f'player {slot} ordering/RNG experiment count or restoration failed')
        by_tick = parse_samples(log, slot, errors)
        if re.search(r'native_gate_shield_refresh\s*=\s*true', config):
            refreshes = re.findall(r'NATIVE_GATE SHIELD_REFRESH calls=(\d+) failures=(\d+)', log)
            alive = sum(sum('/dead/' not in row and not row.endswith('/dead') for row in data.split('|')[1:]) for data in by_tick.values())
            if len(refreshes) != 1 or int(refreshes[0][0]) != alive or refreshes[0][1] != '0':
                errors.append(f'player {slot} native shield refresh count/failures differ')
        command_mode = re.search(r'native_gate_command_test\s*=\s*"(pvp|coop)"', config)
        if re.search(r'native_gate_boost_diag\s*=\s*true',config):
            telemetry, boosted = boost_evidence(log,slot,ticks[-1],errors)
            boost_results.append(telemetry);boosted_actors.append(sorted(boosted))
            if re.search(r'native_gate_command_plan\s*=\s*"withdraw"',config) and barrier.get('verdict')=='pass':
                leaders={1,7 if command_mode and command_mode[1]=='pvp' else 2}
                if not leaders<=boosted:errors.append(f'player {slot} both controlled leaders did not exercise native boost')
        if re.search(r'native_gate_travel_diag\s*=\s*true', config):
            telemetry, active = travel_evidence(log,slot,ticks[-1],errors)
            travel_results.append(telemetry);travel_active.append(sorted(active))
            if re.search(r'native_gate_command_plan\s*=\s*"travel"',config):
                leaders={1,7 if command_mode and command_mode[1]=='pvp' else 2}
                if not leaders<=active:
                    errors.append(f'player {slot} both controlled leaders did not enter native travel mode')
        command_states = {}
        player_states={}
        player_mode=re.search(r'native_gate_player_probe\s*=\s*"([\w-]+)"',config)
        if 'NATIVE_GATE PLAYER_GETUP_' in log and (not player_mode or player_mode[1]!='agreed-getup'):
            errors.append(f'player {slot} unexpected GetUp action receipts without exclusive mode')
        if player_mode:
            if player_mode[1]=='agreed-getup':player_states,command_states,player_result=player_getup_evidence(log,slot,config,states,errors)
            else:player_states,command_states,player_result=player_evidence(log,slot,player_mode[1],states,errors)
            if re.search(r'native_gate_player_observer\s*=\s*true',config):
                trace,observed=player_observer_evidence(log,slot,player_result['context'],ticks[-1],errors)
                native_player_observer_traces.append(trace);player_result['observer']=observed
            if re.search(r'native_gate_player_solver_observer\s*=\s*true',config):
                trace,observed=solver_evidence(log,slot,player_result['context'],ticks[-1],errors)
                native_solver_traces.append(trace);player_result['solver']=observed
            if re.search(r'native_gate_player_request_observer\s*=\s*true',config):
                trace,observed=request_evidence(log,slot,ticks[-1],errors)
                native_request_traces.append(trace);player_result['requests']=observed
            if player_mode[1]=='agreed-fire':
                trace,observed=player_fire_evidence(log,slot,player_result['context'],ticks[-1],errors)
                native_player_fire_traces.append(trace);player_result['firing']=observed
            if re.search(r'native_gate_player_weapon_rng\s*=\s*true',config):
                trace,observed=player_launch_evidence(log,slot,player_result['context'],ticks[-1],errors)
                native_player_launch_traces.append(trace);player_result['launch_rng']=observed
            native_player_results.append(player_result)
        management_states = {}
        if re.search(r'\bnative_gate_player_transition_queue_evidence\s*=',config):
            errors.append('single-native queue evidence cannot form a paired fixture')
        if re.search(r'native_gate_player_transition_observer\s*=\s*true',config):
            native_transition_captures.append(transition_evidence(log,config,slot,configured_watches(config),errors))
        if 'native_gate_player_origin_token' in config or 'NATIVE_GATE PLAYER_ORIGIN_' in log:
            native_origin_captures.append(player_origin_evidence(log,config,slot,errors))
        world_states={}
        world_stride=re.search(r'native_gate_world_hash_stride\s*=\s*(\d+)',config)
        try:world_schema=configured_schema(config)
        except ValueError as error:errors.append(f'player {slot} world hash configuration: {error}');world_schema='WORLD_IMPORTED_1'
        if world_stride:
            world_states,coverage=world_hash_evidence(log,slot,int(world_stride[1]),ticks[-1],errors,configured_watches(config),
                bool(re.search(r'native_gate_world_timing_fields\s*=\s*true',config)),world_schema)
            world_hash_traces.append(world_states);world_hash_coverage.append(coverage)
        values_enabled=False
        action_errors=[];action_events=[];value_errors=[];value_events=[]
        try:values_enabled=values_configured(config)
        except ValueError as error:errors.append(f'player {slot} typed outputs configuration: {error}')
        if 'native_gate_world_action_manifest' in config:
            action_errors=[];action_expected=[];action_events=[];action_coverage={}
            try:
                action_expected=action_configured(config)
                action_events,action_coverage=action_evidence(log,action_expected,action_errors,f'player {slot} world action')
            except ValueError as error:action_errors.append(f'player {slot} world action configuration: {error}')
            errors.extend(action_errors)
            world_action_captures.append(dict(slot=slot,expected=action_expected,events=action_events,coverage=action_coverage,errors=action_errors))
            if values_enabled:
                value_errors=[];value_events,value_coverage=value_evidence(log,action_events,value_errors,f'player {slot} world values',parent_coverage=action_coverage)
                errors.extend(value_errors)
                world_value_captures.append(dict(slot=slot,actions=action_events,events=value_events,coverage=value_coverage,errors=value_errors,parent_errors=action_errors,expected=action_expected))
        elif values_enabled:
            errors.append(f'player {slot} typed outputs lack selected Action configuration')
        if ('native_gate_world_position' in config or 'NATIVE_GATE WORLD_POSITION' in log) and 'native_gate_world_position_rng_manifest' not in config:
            position_errors=[];position_capture={'selected_valid':False,'coverage_complete':False,'events':[]}
            try:
                if not position_configured(config) or not values_enabled or 'native_gate_world_action_manifest' not in config:
                    raise ValueError('post-evaluation position capture lacks explicit flag or Action/Values configuration')
                position_capture=position_evidence(log,action_events,value_events,position_errors,f'player {slot} world position',action_errors+value_errors)
            except (ValueError,KeyError,TypeError) as error:position_errors.append(f'player {slot} world position configuration/evidence: {error}')
            errors.extend(position_errors);world_position_captures.append(dict(slot=slot,evidence=position_capture,errors=position_errors))
        if 'native_gate_world_safe_position' in config or 'NATIVE_GATE WORLD_SAFE_POSITION' in log:
            safe_errors=[];safe_capture={'selected_valid':False,'coverage_complete':False,'events':[]}
            try:
                if not safe_position_configured(config) or not position_configured(config) or not values_enabled or 'native_gate_world_action_manifest' not in config:
                    raise ValueError('safe position capture lacks explicit flag or Position/Action/Values configuration')
                safe_capture=safe_position_evidence(log,action_events,value_events,safe_errors,f'player {slot} world safe position',action_errors+value_errors)
            except (ValueError,KeyError,TypeError) as error:safe_errors.append(f'player {slot} world safe position configuration/evidence: {error}')
            errors.extend(safe_errors);world_safe_position_captures.append(dict(slot=slot,evidence=safe_capture,errors=safe_errors))
        if 'native_gate_world_wait_manifest' in config:
            wait_errors=[];wait_expected=[];wait_events=[];wait_coverage={}
            rng_configured='native_gate_world_wait_rng_manifest' in config;rng_manifest={}
            try:
                wait_expected=configured_waits(config)
                if rng_configured:rng_manifest=wait_rng_configured(config,wait_expected)
                wait_events,wait_coverage=world_wait_evidence(log,wait_expected,wait_errors,f'player {slot} world wait',rng_configured=rng_configured,
                    rng_policies=wait_rng_policies(rng_manifest) if rng_manifest else None)
            except ValueError as error:wait_errors.append(f'player {slot} world wait configuration: {error}')
            errors.extend(wait_errors)
            world_wait_captures.append(dict(slot=slot,expected=wait_expected,events=wait_events,coverage=wait_coverage,errors=wait_errors))
            if rng_configured:
                rng_errors=list(wait_errors);rng_evidence={}
                try:
                    if not rng_manifest:raise ValueError('policy manifest did not validate before raw interpretation')
                    rng_evidence=wait_rng_evidence(log,rng_manifest,wait_events,rng_errors,f'player {slot} world Wait RNG')
                except ValueError as error:rng_errors.append(f'player {slot} world Wait RNG configuration: {error}')
                errors.extend(e for e in rng_errors if e not in wait_errors)
                world_wait_rng_captures.append(dict(slot=slot,manifest=rng_manifest,evidence=rng_evidence,errors=rng_errors))
        elif 'native_gate_world_wait_rng_manifest' in config:
            errors.append(f'player {slot} Wait RNG experiment lacks read-only observation configuration')
        if 'native_gate_world_find_rng_manifest' in config:
            find_errors=[];find_capture={'selected_scope_valid':False,'events':{},'stop':None}
            try:
                find_expected=find_configured(config)
                if not values_enabled or not all(k in config for k in ('native_gate_world_action_manifest','native_gate_world_wait_manifest','native_gate_world_wait_rng_manifest')):
                    raise ValueError('FindComponent experiment lacks required raw observers')
                find_capture=find_evidence(log,find_expected,action_events,value_events,wait_events,find_errors,f'player {slot} FindComponent RNG')
            except (ValueError,KeyError,TypeError) as error:find_errors.append(f'player {slot} FindComponent RNG configuration/evidence: {error}')
            errors.extend(find_errors);world_find_captures.append(dict(slot=slot,evidence=find_capture,errors=find_errors))
        if 'native_gate_world_position_rng_manifest' in config or 'NATIVE_GATE WORLD_POSITION_RNG' in log:
            intervention_errors=[];position_errors=[];intervention_capture={'configured':True,'selected_scope_valid':False,'events':{},'stop':None}
            position_capture={'selected_valid':False,'coverage_complete':False,'events':[]}
            try:
                document=position_rng_configured(config)
                if document is None or not values_enabled or not all(k in config for k in ('native_gate_world_find_rng_manifest','native_gate_world_wait_manifest','native_gate_world_action_manifest')):
                    raise ValueError('Position RNG lacks explicit configuration/dependencies')
                prepared=position_rng_prepare(log,document,find_capture,wait_events,intervention_errors,f'player {slot} Position RNG')
                position_capture=position_evidence(log,action_events,value_events,position_errors,f'player {slot} world position',action_errors+value_errors,prepared)
                intervention_capture=position_rng_finalize(prepared,position_capture,intervention_errors,f'player {slot} Position RNG')
            except (ValueError,KeyError,TypeError) as error:intervention_errors.append(f'player {slot} Position RNG configuration/evidence: {error}')
            errors.extend(position_errors);errors.extend(intervention_errors)
            world_position_captures.append(dict(slot=slot,evidence=position_capture,errors=position_errors))
            world_position_rng_captures.append(dict(slot=slot,evidence=intervention_capture,errors=intervention_errors))
        if re.search(r'native_gate_world_trace_ticks\s*=\s*\d+',config):
            motion_trace,_=world_trace_evidence(log,errors,f'player {slot} world motion',configured_watches(config))
            if 'native_gate_world_queue_trace = true' in config:
                _,queue_coverage=world_queue_evidence(log,errors,f'player {slot} world queue',motion_trace,configured_watches(config))
                errors.extend(queue_coverage['provenance_errors'])
        if command_mode:
            if barrier.get('protocol_version') != 2:
                errors.append(f'player {slot} command transport version missing')
            if re.search(r'native_gate_construction_test\s*=\s*true',config):
                command_states,applied,management_states,_=construction_evidence(log,slot,states,errors,barrier.get('verdict')=='pass')
            elif re.search(r'native_gate_management_test\s*=\s*true',config):
                command_states,applied,management_states=management_evidence(log,slot,states,errors)
            else:
                command_states, applied = command_evidence(log, slot, command_mode[1], states, errors)
            command_results.append(applied)
            if re.search(r'native_gate_command_plan\s*=\s*"live"', config):
                live_input_evidence(log, slot, (barrier.get('live_inputs') or {}).get(str(slot)), states, errors)
        if re.search(r'native_gate_strict_physics\s*=\s*true', config):
            order_state = bool(re.search(r'native_gate_order_state\s*=\s*true', config))
            spline_state = bool(re.search(r'native_gate_spline_state\s*=\s*true', config))
            fleet_state = bool(re.search(r'native_gate_fleet_state\s*=\s*true', config))
            docking_state = bool(re.search(r'native_gate_docking_state\s*=\s*true', config))
            prefix = 'BATTLE_SUBSET_8|' if docking_state else 'BATTLE_SUBSET_7|' if fleet_state else 'BATTLE_SUBSET_6|' if spline_state else 'BATTLE_SUBSET_5|' if order_state else 'BATTLE_SUBSET_4|' if re.search(r'native_gate_steering_schema\s*=\s*2', config) else 'BATTLE_SUBSET_3|'
            if not by_tick or any(not data.startswith(prefix) or
                                  data.count('/body0:') != 12 or data.count('/body1:') != 12
                                  for data in by_tick.values()):
                errors.append(f'player {slot} strict physics scope missing from actual state')
            if prefix in ('BATTLE_SUBSET_4|', 'BATTLE_SUBSET_5|', 'BATTLE_SUBSET_6|') and any(any('/dead/' not in row and ':steering=' not in row for row in data.split('|')[1:]) for data in by_tick.values()):
                errors.append(f'player {slot} live steering state missing from canonical rows')
            if spline_state and any(any('/dead/' not in row and not (docking_state and re.search(r':dockctl=(?:DOCKCTL_1:[0-9a-f]{16}:[0-9a-f]{8}:(?:00|01){2}:[0-9a-f]{336}|UNDOCKCTL_1:[0-9a-f]{16}:[0-9a-f]{8}:(?:00|01):[0-9a-f]{192}):attention=\d+/',row)) and not re.search(r':spline=(?:none|[0-9a-f]{32})/',row) for row in data.split('|')[1:]) for data in by_tick.values()):
                errors.append(f'player {slot} live spline state missing from canonical rows')
            if fleet_state:
                if any(any('/dead/' not in row and not re.search(r'/fleet:FLEET_1;(?:[0-9]|1[0-2]);[\w.\-]+;(?:[0-9]|10)/',row) for row in data.split('|')[1:]) for data in by_tick.values()):
                    errors.append(f'player {slot} live fleet state missing from canonical rows')
                fleet_changes.append(fleet_evidence(by_tick,states,slot,errors))
                if re.search(r'native_gate_command_plan\s*=\s*"fleet"',config) and not fleet_changes[-1]:
                    errors.append(f'player {slot} fleet plan did not exercise native assignment')
            if order_state and any(any('/dead/' not in row and '/orders:ORDERS_1;' not in row for row in data.split('|')[1:]) for data in by_tick.values()):
                errors.append(f'player {slot} live order queue missing from canonical rows')
            if docking_state:
                docking_results.append(docking_evidence(by_tick,states,slot,errors,barrier.get('verdict')=='pass'))
        samples.append(by_tick)
        rng.append({int(t): state for t, state in re.findall(r'NATIVE_GATE RNG tick=(\d+) state=([0-9a-f]{16})', log)})
        if scope == 'battle' and not by_tick:
            errors.append(f'player {slot} has no actual battle samples')
        if scope == 'battle' and 'NATIVE_GATE FIXTURE engaged' not in log:
            errors.append(f'player {slot} battle never engaged')
        last_world_state=None
        for tick in ticks:
            row = states[tick].get(str(slot))
            if row is None:
                errors.append(f'player {slot} missing state at {tick}')
                continue
            if tick and abs(row['game'] - states[tick-1][str(slot)]['game'] - 1/tick_hz) > 1e-8:
                errors.append(f'player {slot} game step at {tick}')
            data = 'CLOCK_ONLY_1' if scope == 'clock' else by_tick.get(tick, 'BATTLE_FIXTURE_PENDING_1')
            if snapshot and tick == 0:
                data = 'SNAPSHOT_BYTES_1|'+snapshot[1]
                manifest=re.search(r'native_gate_snapshot_fixture\s*=\s*\{([^}\r\n]+)\}',config)
                if manifest:data+='|SNAPSHOT_FIXTURE_1|'+','.join(re.findall(r"['\"]([0-9a-f]{16})['\"]",manifest[1]))
                if player_mode and player_mode[1]=='agreed-getup':
                    try:data+='|'+getup_bootstrap_plan(config)
                    except ValueError as error:errors.append(f'player {slot} GetUp bootstrap: {error}')
                if creation_rng_suffix:data+='|'+creation_rng_suffix
            if tick in command_states:
                data += '|'+command_states[tick]
            if tick in player_states:
                data += '|'+player_states[tick]
            if tick in management_states:
                data += '|'+management_states[tick]
            if world_stride and tick:
                last_world_state=world_states.get(tick,last_world_state)
                if not last_world_state:errors.append(f'player {slot} world baseline unavailable at {tick}')
                else:data+='|'+last_world_state
            if hashlib.sha256(data.encode()).hexdigest() != row['digest']:
                errors.append(f'player {slot} independently calculated hash differs at {tick}')
    mismatches = []
    for tick in ticks:
        a, b = states[tick].get('1'), states[tick].get('2')
        if a is None or b is None:
            continue
        real_equal = a['real'] == b['real']
        if barrier.get('real_time_domain') == 'elapsed from snapshot admission':
            elapsed = [states[tick][s]['real']-states[0][s]['real'] for s in ('1', '2')]
            real_equal = abs(elapsed[0]-elapsed[1]) <= 1e-8 and all(abs(t-tick/60) <= 1e-8 for t in elapsed)
        if a['game'] != b['game'] or not real_equal:
            errors.append(f'peer clocks differ at {tick}')
        if a['digest'] != b['digest']:
            mismatches.append(tick)
    if barrier['hold_seconds'] < 3:
        errors.append('three-second hold not proven')
    if barrier['verdict'] == 'pass' and mismatches:
        errors.append('relay passed mismatched state')
    if mismatches and (mismatches != [ticks[-1]] or barrier['failure'] != f'battle_state_mismatch_tick_{ticks[-1]}'):
        errors.append('did not stop on first mismatch')
    differences = []
    # World/player extensions can reject the first frame before the MD fixture
    # has enrolled. The missing battle coverage remains an evidence error above;
    # it must not prevent reporting the actual barrier mismatch.
    if mismatches and len(samples) == 2 and all(mismatches[0] in sample for sample in samples):
        a, b = (sample[mismatches[0]].split('|')[1:] for sample in samples)
        for left, right in zip(a, b):
            if left != right:
                differences.append({'player1': left, 'player2': right})
    common_rng = sorted(set(rng[0]) & set(rng[1])) if len(rng) == 2 else []
    if command_results and (len(command_results) != 2 or command_results[0] != command_results[1]):
        errors.append('native commands applied at different game times across players')
    if travel_results and (len(travel_results)!=2 or travel_results[0]!=travel_results[1]):
        errors.append('native travel telemetry differs between players')
    if spline_joins and (len(spline_joins)!=2 or any(row is None for row in spline_joins) or
                         spline_joins[0]['requests']!=spline_joins[1]['requests']):
        errors.append('native spline requests joined differ between players')
    # Direct attack/escort scenarios need not subscribe to radar notifications.
    # Fleet-change acceptance still exercises the event path that exposed its
    # original desync; other scenarios report this coverage separately.
    if radar_events and barrier.get('verdict')=='pass' and any(fleet_changes) and not sum(radar_events):
        errors.append('native radar event observer was not exercised by fleet changes')
    if damage_calls and barrier.get('verdict')=='pass' and not sum(damage_calls):
        errors.append('native damage observer was not exercised')
    interrupt_equal=interrupt_rng_traces[0]==interrupt_rng_traces[1] if len(interrupt_rng_traces)==2 else None
    interrupt_steps_equal=interrupt_step_traces[0]==interrupt_step_traces[1] if len(interrupt_step_traces)==2 else None
    nested_actions_equal=nested_action_traces[0]==nested_action_traces[1] if len(nested_action_traces)==2 else None
    npc_actions_equal=npc_action_traces[0]==npc_action_traces[1] if len(npc_action_traces)==2 else None
    npc_trigger_keys_equal=None
    if npc_trigger_traces:
        if len(npc_trigger_traces)!=2:errors.append('native NPC trigger observation missing from one player')
        else:
            candidates=[[r['key'] for r in trace['rows'] if r['usable']] for trace in npc_trigger_traces]
            npc_trigger_keys_equal=candidates[0]==candidates[1]
            if not npc_trigger_keys_equal:errors.append('native NPC fixture trigger candidate paths differ between players')
    npc_exec_equal=npc_exec_payloads(npc_exec_traces[0])==npc_exec_payloads(npc_exec_traces[1]) if len(npc_exec_traces)==2 else None
    npc_provenance_equal=None
    if npc_provenance_traces:
        if len(npc_provenance_traces)!=2:errors.append('native NPC provenance missing from one player')
        else:
            npc_provenance_equal=npc_provenance_traces[0]['rows']==npc_provenance_traces[1]['rows']
            if not npc_provenance_equal:errors.append('native NPC provenance observations differ between players')
    npc_speak_equal=npc_speak_payloads(npc_speak_traces[0])==npc_speak_payloads(npc_speak_traces[1]) if len(npc_speak_traces)==2 and all(npc_speak_traces) else None
    npc_pilot_equal=None
    if npc_pilot_traces:
        if len(npc_pilot_traces)!=2:errors.append('selected NPC pilot trace missing from one player')
        else:
            npc_pilot_equal=npc_pilot_traces[0]['rows']==npc_pilot_traces[1]['rows']
            if not npc_pilot_equal:errors.append('selected NPC pilot execution provenance differs between players')
    npc_selection_equal=npc_speak_payloads(npc_speak_traces[0],True)==npc_speak_payloads(npc_speak_traces[1],True) if len(npc_speak_traces)==2 and all(npc_speak_traces) else None
    npc_enqueue_equal=npc_enqueue_traces[0]==npc_enqueue_traces[1] if len(npc_enqueue_traces)==2 and all(npc_enqueue_traces) else None
    if npc_deadline_traces and (len(npc_deadline_traces)!=2 or not any(row[10]=='1' for trace in npc_deadline_traces for row in trace)):
        errors.append('native NPC deadline experiment lacks paired early-completion intervention coverage')
    npc_finish_equal=npc_finish_traces[0]==npc_finish_traces[1] if len(npc_finish_traces)==2 and all(npc_finish_traces) else None
    if npc_finish_traces and len(npc_finish_traces)!=2:errors.append('native NPC speech finish capture missing from one player')
    if npc_exec_traces and len(npc_exec_traces)!=2:errors.append('experimental NPC execution scope missing from one player')
    if npc_speak_traces and len(npc_speak_traces)!=2:errors.append('native NPC speech selection missing from one player')
    world_identities_equal=world_identity_traces[0]==world_identity_traces[1] if len(world_identity_traces)==2 else None
    world_hashes_equal=world_hash_traces[0]==world_hash_traces[1] if len(world_hash_traces)==2 else None
    registry_equal=registry_traces[0]==registry_traces[1] if len(registry_traces)==2 else None
    registry_nav_equal=registry_navigation_traces[0]==registry_navigation_traces[1] if len(registry_navigation_traces)==2 else None
    creation_rng_equal=None
    if any(t['supported'] for t in creation_rng_traces):
        creation_rng_equal=len(creation_rng_traces)==2 and all(t['valid'] for t in creation_rng_traces) and creation_rng_traces[0]['canonical']==creation_rng_traces[1]['canonical']
        if not creation_rng_equal:errors.append('peer construction RNG scopes/draws/native timers differ or are incomplete')
    registry_nav_results_equal=registry_navigation_results(registry_navigation_traces[0])==registry_navigation_results(registry_navigation_traces[1]) if len(registry_navigation_traces)==2 else None
    snapshot_fixture_equal=snapshot_fixture_traces[0]==snapshot_fixture_traces[1] if len(snapshot_fixture_traces)==2 else None
    snapshot_results_equal=snapshot_navigation_results(snapshot_fixture_traces[0])==snapshot_navigation_results(snapshot_fixture_traces[1]) if len(snapshot_fixture_traces)==2 else None
    if world_identity_traces and (len(world_identity_traces)!=2 or not world_identities_equal):
        errors.append('saved fixture identities differ between players or capture missing')
    radar_equal=radar_payloads(logs[0])==radar_payloads(logs[1]) if radar_events and len(logs)==2 else None
    gravidar_equal=gravidar_traces[0]==gravidar_traces[1] if len(gravidar_traces)==2 else None
    damage_equal=damage_payloads(logs[0])==damage_payloads(logs[1]) if damage_calls and len(logs)==2 else None
    contact_equal=contact_traces[0]==contact_traces[1] if len(contact_traces)==2 else None
    contact_sets_equal=contact_multiset(contact_traces[0])==contact_multiset(contact_traces[1]) if len(contact_traces)==2 else None
    if barrier.get('verdict')=='pass':
        if snapshot_results_equal is False:errors.append('snapshot first navigation results differ between players')
        if registry_equal is False:errors.append('dynamic native ship receipts differ between players')
        if registry_nav_results_equal is False:errors.append('dynamic first navigation results differ between players')
        if contact_equal is False:errors.append('native contact inputs/settings differ between players')
        if boost_results and (len(boost_results)!=2 or boost_results[0]!=boost_results[1]):errors.append('native boost telemetry differs between players')
        if interrupt_equal is False:errors.append('native interrupt execution/RNG differs between players')
        if interrupt_steps_equal is False:errors.append('native interrupt instruction decisions differ between players')
        if nested_actions_equal is False:errors.append('nested native action decisions differ between players')
        if npc_actions_equal is False:errors.append('named NPC lifecycle decisions differ between players')
        if npc_exec_equal is False:errors.append('experimental NPC scoped execution inputs/results differ between players')
        if npc_speak_equal is False:errors.append('native NPC speech selection/timing/RNG differs between players')
        if npc_enqueue_equal is False:errors.append('native NPC speech enqueue arguments/results differ between players')
        if npc_finish_equal is False:errors.append('native NPC speech finish provenance/state differs between players')
        if interrupt_equal is True and not any(interrupt_rng_traces):errors.append('native interrupt RNG scope was not exercised')
        if radar_equal is False:errors.append('native radar event delivery differs between players')
        if gravidar_equal is False:errors.append('native radar scan timing/state differs between players')
        if damage_equal is False:errors.append('native damage inputs/results differ between players')
    checked_equal = barrier['verdict'] == 'pass' and not mismatches
    if native_transition_captures and len(native_transition_captures)!=2:
        errors.append('native transition observer capture/configuration missing from one player')
    if len(native_transition_captures)==2 and native_transition_captures[0]['configuration']!=native_transition_captures[1]['configuration']:
        errors.append('native transition observer configurations differ between players')
    if native_origin_captures and len(native_origin_captures)!=2:
        errors.append('native player origin capture/configuration missing from one player')
    if len(native_origin_captures)==2 and native_origin_captures[0]['configuration']!=native_origin_captures[1]['configuration']:
        errors.append('native player origin configurations differ between players')
    if len(native_origin_captures)==2 and (native_origin_captures[0]['stop'] or {}).get('schema')!=(native_origin_captures[1]['stop'] or {}).get('schema'):
        errors.append('native player origin receipt schemas differ between players')
    player_observers_equal=native_player_observer_traces[0]==native_player_observer_traces[1] if len(native_player_observer_traces)==2 else None
    if native_player_observer_traces and (len(native_player_observer_traces)!=2 or not player_observers_equal):
        errors.append('native player observer context/history differs between players or capture missing')
    player_fire_equal=native_player_fire_traces[0]==native_player_fire_traces[1] if len(native_player_fire_traces)==2 else None
    if native_player_fire_traces and (len(native_player_fire_traces)!=2 or not player_fire_equal):
        errors.append('native player projectile/damage trace differs between players or capture missing')
    player_launch_equal=native_player_launch_traces[0]==native_player_launch_traces[1] if len(native_player_launch_traces)==2 else None
    if native_player_launch_traces and (len(native_player_launch_traces)!=2 or not player_launch_equal):
        errors.append('native player launch RNG/timing differs between players or capture missing')
    if native_request_traces and (len(native_request_traces)!=2 or native_request_traces[0]!=native_request_traces[1]):
        errors.append('native raw request/Agent diagnostics differ or capture missing (bank references/padding retained)')
    if native_solver_traces and (len(native_solver_traces)!=2 or native_solver_traces[0]!=native_solver_traces[1]):
        errors.append('native player/fixture solver inputs/results/publication differ or capture missing')
    wait_diagnostics=world_wait_diagnostics(world_wait_captures,barrier.get('verdict')=='pass',errors)
    wait_rng_result=wait_rng_diagnostics(world_wait_rng_captures,barrier.get('verdict')=='pass',errors)
    action_result=action_diagnostics(world_action_captures,barrier.get('verdict')=='pass',errors)
    value_result=value_diagnostics(world_value_captures,barrier.get('verdict')=='pass',errors)
    find_result=find_diagnostics(world_find_captures,barrier.get('verdict')=='pass',errors)
    position_result=position_diagnostics(world_position_captures,barrier.get('verdict')=='pass',errors)
    safe_position_result=safe_position_diagnostics(world_safe_position_captures,barrier.get('verdict')=='pass',errors)
    position_rng_result=position_rng_diagnostics(world_position_rng_captures,barrier.get('verdict')=='pass',errors)
    session_pass = not errors and checked_equal and session_result.get('verdict') == 'pass' and session_result.get('exit_code') == 0
    return {'evidence_valid': not errors, 'errors': errors, 'scope': scope,
            'registry_md_boundary_traces': registry_md_traces,
            'creation_rng_scopes': creation_rng_traces,
            'provisional_registry': registry_creation_traces,
            'creation_rng_equal': creation_rng_equal,
            'session_pass': session_pass, 'runner_verdict': session_result.get('verdict'),
            'runner_exit_reason': session_result.get('exit_reason'),
            'native_commands_applied_per_client': len(command_results[0]) if command_results else 0,
            'travel_active_actors': travel_active,
            'native_boosted_actors': boosted_actors,
            'native_docking_per_client': docking_results,
            'spline_result_joins': spline_joins,
            'radar_events_per_client': radar_events,
            'radar_event_delivery_exercised': bool(sum(radar_events)),
            'radar_phase_joins': gravidar_results,
            'radar_event_timing_equal': radar_equal,
            'radar_scan_timing_equal': gravidar_equal,
            'native_interrupt_calls_per_client': interrupt_calls,
            'native_interrupt_rng_equal': interrupt_equal,
            'native_interrupt_steps_equal': interrupt_steps_equal,
            'native_nested_actions_equal': nested_actions_equal,
            'native_nested_actions_per_client': [len(trace) for trace in nested_action_traces],
            'native_npc_lifecycle_equal': npc_actions_equal,
            'native_npc_actions_per_client': [len(trace) for trace in npc_action_traces],
            'native_npc_fixture_trigger_keys_equal': npc_trigger_keys_equal,
            'native_npc_trigger_per_client': [len(trace['rows']) for trace in npc_trigger_traces],
            'native_npc_trigger_unsupported_per_client': [trace['unsupported'] for trace in npc_trigger_traces],
            'native_npc_trigger_ambiguous_per_client': [trace['ambiguous_candidates'] for trace in npc_trigger_traces],
            'native_npc_trigger_candidate_coverage_complete': len(npc_trigger_traces)==2 and all(trace['candidate_coverage_complete'] for trace in npc_trigger_traces),
            'native_npc_trigger_semantic_identity_proven': False,
            'native_npc_provenance_equal': npc_provenance_equal,
            'native_npc_provenance_per_client': [len(trace['rows']) for trace in npc_provenance_traces],
            'native_npc_provenance_unresolved_per_client': [trace['unresolved'] for trace in npc_provenance_traces],
            'native_npc_provenance_unowned_per_client': [trace['unowned'] for trace in npc_provenance_traces],
            'native_npc_provenance_semantic_identity_proven': False,
            'native_npc_pilot_equal': npc_pilot_equal,
            'native_npc_pilot_per_client': [len(t['rows']) for t in npc_pilot_traces],
            'native_npc_pilot_coverage_per_client': [{k:v for k,v in t.items() if k!='rows'} for t in npc_pilot_traces],
            'native_npc_pilot_coverage_complete':len(npc_pilot_traces)==2 and all(t['coverage_complete'] for t in npc_pilot_traces),
            'native_npc_pilot_semantic_identity_proven':False,
            'native_npc_exec_per_client': [len(trace) for trace in npc_exec_traces],
            'native_npc_exec_equal': npc_exec_equal,
            'native_npc_exec_identity': 'ordered_invocation_v1' if npc_exec_traces else None,
            'native_npc_exec_identity_limitation': 'duplicate cue instance semantic identity and scheduling unresolved' if npc_exec_traces else None,
            'native_npc_speech_per_client': [len(trace) for trace in npc_speak_traces],
            'native_npc_speech_equal': npc_speak_equal,
            'native_npc_speech_selection_equal': npc_selection_equal,
            'native_npc_enqueue_per_client': [len(trace) for trace in npc_enqueue_traces],
            'native_npc_enqueue_equal': npc_enqueue_equal,
            'native_npc_ready_per_client': [len(trace) for trace in npc_ready_traces],
            'native_npc_deadline_per_client': [len(trace) for trace in npc_deadline_traces],
            'native_npc_finish_per_client': [len(trace) for trace in npc_finish_traces],
            'native_npc_finish_equal': npc_finish_equal,
            'saved_fixture_identities_equal': world_identities_equal,
            'snapshot_first_navigation_equal': snapshot_fixture_equal,
            'dynamic_ship_registry_equal': registry_equal,
            'dynamic_first_navigation_equal': registry_nav_equal,
            'dynamic_first_navigation_results_equal': registry_nav_results_equal,
            'snapshot_first_navigation_results_equal': snapshot_results_equal,
            'imported_world_hashes_equal': world_hashes_equal,
            'imported_world_hash_coverage': world_hash_coverage,
            'native_wait_restore': wait_diagnostics,
            'native_wait_rng_experiment': wait_rng_result,
            'native_early_actions': action_result,
            'native_early_action_values': value_result,
            'native_selected_find_rng': find_result,
            'native_selected_position_inputs': position_result,
            'native_selected_safe_position_inputs': safe_position_result,
            'native_selected_position_rng': position_rng_result,
            'native_player_diagnostics': native_player_results,
            'native_player_transitions': native_transition_captures,
            'native_player_origins': native_origin_captures,
            'native_player_observers_equal': player_observers_equal,
            'native_player_fire_equal': player_fire_equal,
            'native_player_launch_equal': player_launch_equal,
            'independent_native_players': False,
            'native_damage_calls_per_client': damage_calls,
            'native_damage_equal': damage_equal,
            'native_contact_calls_per_client': [len(trace) for trace in contact_traces],
            'native_contact_inputs_equal': contact_equal,
            'native_contact_multisets_equal': contact_sets_equal,
            'native_contact_traversal_equal': contact_order_traces[0]==contact_order_traces[1] if len(contact_order_traces)==2 else None,
            'native_contact_property_updates_per_client': contact_property_calls,
            'native_shape_lifetime_transitions_per_client': shape_lifetime_calls,
            'native_contacts_exercised': bool(any(contact_traces)),
            'native_fleet_changes_per_client': fleet_changes,
            'checked_state_equal': checked_equal,
            'ticks_observed': len(ticks), 'first_mismatch': mismatches[0] if mismatches else None,
            'hold_seconds': barrier['hold_seconds'], 'native_stops': stops,
            'first_rng_difference': next((t for t in common_rng if rng[0][t] != rng[1][t]), None),
            'first_state_differences': differences}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('run', type=Path)
    p.add_argument('--scope', choices=('clock', 'battle'), required=True)
    args = p.parse_args()
    result = validate(args.run, args.scope)
    text = json.dumps(result, indent=2)+'\n'
    (args.run/'validation.json').write_text(text)
    print(text)
    raise SystemExit(0 if result['evidence_valid'] else 1)


if __name__ == '__main__':
    main()
