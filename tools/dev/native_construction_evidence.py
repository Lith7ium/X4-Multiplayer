"""Independent pending/ack, resource and actual-completion construction evidence."""
import hashlib
import math
import re
import struct
from native_construction_raw_evidence import sample as raw_sample, same_frame as raw_same_frame, reassemble as raw_reassemble

SEMANTIC='CONSTRUCTION_PLAN_1|struct_arg_base_02_macro|0,0,0|0,0,0|no_connections|no_equipment|default|unpaid_plot_2000,2000,2000'
PLAN_SHA=hashlib.sha256(SEMANTIC.encode()).hexdigest()
BUILDER_HANDLE='0000000500000003'
REGISTRY_PATTERN=(r'SHIP_REGISTRY_1;handle=([0-9a-f]{16});slot=(\d+);reserved_tick=(\d+);bound_tick=(\d+);prepared_tick=(\d+);first_update_tick=(\d+);first_update_kind=(\d+);late_updates=(\d+);identity_ok=([01])')


def registry_evidence(raw,tick,builder_present):
    if tick<5:
        if raw!='registry_pending' or builder_present:raise ValueError('builder exists before reservation')
        return None
    m=re.fullmatch(REGISTRY_PATTERN,raw)
    if not m or m[1]!=BUILDER_HANDLE:raise ValueError('invalid native ship registry shape/handle')
    slot,reserved,bound,prepared,first,kind,late,identity=map(int,m.groups()[1:])
    if not 15<=slot<=30 or reserved!=5 or late:raise ValueError('invalid native ship reservation or late update')
    if builder_present:
        if identity!=1 or not 5<=bound<=6 or prepared!=bound or prepared>tick:raise ValueError('builder not prepared at creation boundary')
        if not ((first==0 and kind==0) or (prepared<=first<=tick and kind>0)):raise ValueError('builder updated outside enrollment')
        if tick>=20 and not first:raise ValueError('builder native update coverage missing')
    elif tick>6 or any((bound,prepared,first,kind,identity)):
        raise ValueError('builder callback missing or premature identity')
    return {'slot':slot,'bound':bound,'prepared':prepared,'first':first,'kind':kind}


def evidence(log,slot,states,errors,require_completion):
    prefix=f'player {slot} construction '
    def rows(pattern):
        result={}
        for match in re.finditer(pattern,log):
            tick=int(match[1])
            if tick in result:errors.append(prefix+f'duplicate row at {tick}')
            result[tick]=match.groups()[1:]
        return result
    inputs=rows(r'NATIVE_GATE INPUTS tick=(\d+) mode=construction p1=([0-9a-f]{8}) p2=([0-9a-f]{8})')
    samples=rows(r'NATIVE_GATE CONSTRUCTION_STATE tick=(\d+) state=([^\r\n]+)')
    raw_chunks={}
    try:
        raw_chunks=raw_reassemble(log,'RAW')
        for (tick,phase),data in raw_reassemble(log,'STATE').items():
            if tick in samples:raise ValueError('duplicate mixed construction state encodings')
            samples[tick]=(data,)
    except ValueError as exc:errors.append(prefix+str(exc))
    receipts=rows(r'NATIVE_GATE CONSTRUCTION_RECEIPTS tick=(\d+) data=([^\r\n]+)')
    acks={}
    for t,ticket,kind,result in re.findall(r'NATIVE_GATE CONSTRUCTION_ACK tick=(\d+) ticket=([\d-]+) kind=(plan|deploy) result=([^\r\n]+)',log):
        acks.setdefault(int(t),[]).append((ticket,kind,result))
    md={}
    for original,observed,state in re.findall(r'NATIVE_GATE CONSTRUCTION_MD tick=(\d+) observed=(\d+) state=([^\r\n]+)',log):
        key=int(observed)
        if key in md:errors.append(prefix+'multiple native samples observed in one frame')
        md[key]=(int(original),state)
    seeds={}
    for ticket,ware,amount in re.findall(r'NATIVE_GATE CONSTRUCTION_RESOURCE_SEED ticket=([\d-]+) ware=(\w+) amount=(\d+)',log):
        if ware in seeds:errors.append(prefix+'duplicate seeded resource')
        seeds[ware]=(ticket,int(amount))
    if re.search(r'Error in MD cue md\.X4LAN_NativeConstruction|NATIVE_GATE ERROR',log):errors.append(prefix+'native error')
    expected=set(states)-{0}
    if set(inputs)!=expected or set(samples)!=expected:errors.append(prefix+'incomplete native evidence')
    pending={};complete={};revisions={1:0,2:0};command_states={};native_states={};applied={}
    latest_md='pending';latest_md_tick=0;had_empty=False;had_progress=False;had_deployed=False;had_complete=False;resources_seen=False;had_order=False;builder_first_tick=None
    registry_slot=None;registry_bound=None;registry_update=None;registry_schema=False;unarmed_scope=None
    target_first={};target_callbacks={};raw_target_callbacks={};state_schema=None
    for at,kind,pose in re.findall(r'CONSTRUCTION_TARGET_BOUND tick=(\d+) kind=(station|storage) pose=([^\r\n]+)',log):
        if kind in target_callbacks:errors.append(prefix+'duplicate target callback '+kind)
        target_callbacks[kind]=(int(at),pose)
    for at,kind,native_id in re.findall(r'CONSTRUCTION_TARGET_BOUND tick=(\d+) kind=(station|storage) native_id=(\d+) sector_pose_supported=0',log):
        if kind in raw_target_callbacks:errors.append(prefix+'duplicate raw target callback '+kind)
        raw_target_callbacks[kind]=(int(at),int(native_id))
    def valid_pose(raw,label='BUILDER'):
        parts=raw.split(',')
        return len(parts)==7 and parts[0]==label+'_POSE_1' and all(re.fullmatch(r'-?\d+(?:\.\d+)?(?:e[+-]?\d+)?',v) and math.isfinite(float(v)) for v in parts[1:])
    for tick in sorted(expected):
        try:
            commands=tuple(states[tick-1][str(p)]['command'] for p in (1,2))
            if inputs[tick]!=commands:raise ValueError('admitted input mismatch')
            command_states[tick]='INPUTS_1:construction:'+':'.join(commands)
            state=samples[tick][0];native_states[tick]=state;fields=state.split(':')
            if fields[0] not in ('CONSTRUCTION_1','CONSTRUCTION_2','CONSTRUCTION_3','CONSTRUCTION_4','CONSTRUCTION_5','CONSTRUCTION_6') or fields[1]!=str(tick):raise ValueError('wrong state schema/tick')
            if state_schema is not None and state_schema!=fields[0]:raise ValueError('construction schema changed during run')
            state_schema=fields[0]
            actual_ack=[]
            for ticket,kind,result in acks.get(tick,[]):
                asset=1 if kind=='plan' else 2
                wanted='1|struct_arg_base_02_macro' if asset==1 else '1|DeployToStation'
                if pending.get(asset)!=ticket or result!=wanted:raise ValueError('unsolicited/invalid native acknowledgement')
                del pending[asset];complete[asset]=ticket
                actual_ack.append(ticket+'|'+kind+'|'+result)
                applied[tick,asset]=kind
            if tick in md:
                at,value=md[tick]
                if at<=latest_md_tick or at>=tick or at%30!=20 or not value.startswith(str(at)+'|'):raise ValueError('native sample timing invalid')
                latest_md_tick=at;latest_md=value
                parts=value.split('|')
                if len(parts)!=9:raise ValueError('malformed native MD state')
                flags=list(map(int,parts[1:8]))
                if any(v not in (0,1) for v in flags):raise ValueError('unbounded native construction state')
                exists,processing,assigned,deployed,total,finished,operational=flags
                if total==0 and at<31:had_empty=True
                if deployed and assigned:had_deployed=True
                current={}
                for row in parts[8].split(';') if parts[8] else []:
                    ware,seeded,remaining=row.split(',');seeded=int(seeded);remaining=int(remaining)
                    if ware in current or ware not in seeds or seeds[ware][1]!=seeded or not 0<=remaining<=seeded:raise ValueError('resource ledger mismatch')
                    current[ware]=remaining
                if current:
                    resources_seen=True
                    if set(current)!=set(seeds):raise ValueError('incomplete resource census')
                if finished:
                    if total!=1 or not current or any(current.values()):raise ValueError('completion lacks actual module/resource consumption')
                    had_complete=True
            wanted=[]
            for player,command in enumerate(commands,1):
                if command=='00000000':continue
                op,asset,arg,revision=(int(command[i:i+2],16) for i in (0,2,4,6))
                if not ((op==73 and asset==1) or (op==74 and asset==2)) or arg:raise ValueError('invalid construction command')
                status='DENIED_OWNER' if asset==2 and player!=2 else 'DENIED_BUSY' if asset in pending else 'DENIED_COMPLETE' if asset in complete else 'STALE_REVISION' if revision!=revisions[asset] else 'DENIED_DEPENDENCY' if asset==2 and 1 not in complete else 'PENDING'
                ticket='none'
                if status=='PENDING':
                    revisions[asset]+=1;ticket=f'{tick}-{player}-{asset}-{revisions[asset]}';pending[asset]=ticket
                wanted.append(','.join(map(str,(player,op,asset,revision,status,revisions[asset],ticket))))
            receipt=';'.join(wanted)
            if receipts.get(tick,("",))!=(receipt,):raise ValueError('pending receipt mismatch')
            if tick<20:
                expected_fields=8 if fields[0] in ('CONSTRUCTION_5','CONSTRUCTION_6') else 7 if fields[0]=='CONSTRUCTION_4' else 5 if fields[0]=='CONSTRUCTION_3' else 4
                if fields[2:4]!=['pending',PLAN_SHA] or receipt or len(fields)!=expected_fields:raise ValueError('early construction state')
                if fields[0] in ('CONSTRUCTION_3','CONSTRUCTION_4','CONSTRUCTION_5','CONSTRUCTION_6'):
                    if fields[4]=='builder_pending':
                        if builder_first_tick is not None:raise ValueError('native builder disappeared')
                    else:
                        if fields[0]=='CONSTRUCTION_6':raw_sample(fields[4],handle=BUILDER_HANDLE,tick=tick)
                        elif not valid_pose(fields[4]):raise ValueError('malformed early native builder pose')
                        if builder_first_tick is None:builder_first_tick=tick
            else:
                if len(fields)!=(11 if fields[0]=='CONSTRUCTION_1' else 15 if fields[0] in ('CONSTRUCTION_5','CONSTRUCTION_6') else 14 if fields[0]=='CONSTRUCTION_4' else 12) or fields[2]!=PLAN_SHA:raise ValueError('plan digest/state shape mismatch')
                if fields[3]!=','.join(str(revisions[a]) for a in (1,2)) or fields[4]!=','.join(pending.get(a,'none') for a in (1,2)) or fields[5]!=','.join(complete.get(a,'none') for a in (1,2)):raise ValueError('transaction state mismatch')
                if fields[6]!=latest_md or fields[9]!=receipt or fields[10]!=';'.join(actual_ack):raise ValueError('native/receipt state mismatch')
                if not fields[8].startswith('ORDERS_1;'):raise ValueError('native builder order state missing')
                if ',DeployToStation,' in fields[8]:had_order=True
                if fields[0]=='CONSTRUCTION_1':progress=struct.unpack('<f',bytes.fromhex(fields[7]))[0]
                else:
                    if not re.fullmatch(r'-?\d+(?:\.\d+)?(?:e[+-]?\d+)?',fields[7]):raise ValueError('malformed native progress')
                    progress=float(fields[7])
                    if fields[0]=='CONSTRUCTION_6':raw_sample(fields[11],handle=BUILDER_HANDLE,tick=tick)
                    elif not valid_pose(fields[11]):raise ValueError('malformed native builder pose')
                    if builder_first_tick is None:builder_first_tick=tick
                if not -1<=progress<=100:raise ValueError('invalid native progress')
                if 0<progress<100:had_progress=True
            if fields[0] in ('CONSTRUCTION_4','CONSTRUCTION_5','CONSTRUCTION_6'):
                registry_schema=True
                registry_at=5 if tick<20 else 12
                armament=fields[registry_at+1]
                r=registry_evidence(fields[registry_at],tick,builder_first_tick is not None)
                if builder_first_tick is None:
                    if armament!='armament_pending':raise ValueError('armament census before builder creation')
                else:
                    arm=re.fullmatch(r'UNARMED_1;weapon_slots=(\d+);turret_slots=(\d+);weapons=0;turrets=0',armament)
                    if not arm or int(arm[1])>32 or int(arm[2])>128:raise ValueError('explicitly unarmed builder native census invalid')
                    if unarmed_scope is not None and armament!=unarmed_scope:raise ValueError('builder physical armament layout changed')
                    unarmed_scope=armament
                if r:
                    if registry_slot is not None and r['slot']!=registry_slot:raise ValueError('native ship registry slot changed')
                    registry_slot=r['slot']
                    if registry_bound is not None and r['bound']!=registry_bound:raise ValueError('native builder binding changed')
                    if r['bound']:registry_bound=r['bound']
                    if registry_update is not None and (r['first'],r['kind'])!=registry_update:raise ValueError('native first-update identity changed')
                    if r['first']:registry_update=(r['first'],r['kind'])
                if fields[0]=='CONSTRUCTION_6' and builder_first_tick is not None:
                    raw_sample(fields[4 if tick<20 else 11],handle=BUILDER_HANDLE,tick=tick,slot=registry_slot)
            if fields[0]=='CONSTRUCTION_5':
                targets=fields[-1].split(';')
                if len(targets)!=3 or targets[0]!='CONSTRUCTION_TARGETS_1':raise ValueError('invalid construction target census')
                for kind,pose in zip(('station','storage'),targets[1:]):
                    callback=target_callbacks.get(kind)
                    if pose==kind+'_pending':
                        if kind in target_first or builder_first_tick is not None or (callback and tick>callback[0]):raise ValueError('native target disappeared or callback unobserved')
                    else:
                        if not valid_pose(pose,kind.upper()):raise ValueError('malformed native target pose')
                        if not callback or not 5<=callback[0]<=6 or callback[0]>tick or not valid_pose(callback[1],kind.upper()):raise ValueError('native target pose lacks valid creation callback')
                        if kind not in target_first:
                            if tick>callback[0]+1:raise ValueError('native target first pose delayed')
                            target_first[kind]=tick
            if fields[0]=='CONSTRUCTION_6':
                targets=re.fullmatch(r'CONSTRUCTION_TARGETS_2;station_ready=([01]);storage_ready=([01]);sector_pose_supported=0',fields[-1])
                if not targets:raise ValueError('invalid passive target coverage')
                for kind,ready in zip(('station','storage'),map(int,targets.groups())):
                    callback=raw_target_callbacks.get(kind)
                    if not ready:
                        if kind in target_first or builder_first_tick is not None or (callback and tick>callback[0]):raise ValueError('raw target disappeared or callback unobserved')
                    else:
                        if not callback or not 5<=callback[0]<=6 or callback[0]>tick or not 0<callback[1]<2**64:raise ValueError('raw target lacks exact creation callback')
                        if kind not in target_first:
                            if tick>callback[0]+1:raise ValueError('raw target callback delayed')
                            target_first[kind]=tick
        except (ValueError,KeyError,IndexError,struct.error) as exc:errors.append(prefix+f'{tick}: {exc}')
    if require_completion and (set(complete)!={1,2} or pending or not all((had_empty,had_progress,had_deployed,had_complete,resources_seen,had_order))):
        errors.append(prefix+'actual build completion/deployment/resource coverage incomplete')
    if require_completion and (not seeds or any(ticket!=complete.get(1) for ticket,_ in seeds.values())):errors.append(prefix+'resource seed ticket differs from admitted plan')
    if state_schema=='CONSTRUCTION_6':
        if require_completion:errors.append(prefix+'sector poses unsupported; strict construction acceptance unavailable')
        if len({native_id for _,native_id in raw_target_callbacks.values()})!=len(raw_target_callbacks):errors.append(prefix+'raw target lifetimes overlap')
        begins=re.findall(r'CONSTRUCTION_BIND_BEGIN tick=(\d+) previous_admitted=(\d+) handle=([0-9a-f]{16}) native_id=(\d+) sector_pose_supported=0 raw_phase=before',log)
        ends=re.findall(r'CONSTRUCTION_BIND_END tick=(\d+) registry=(\S+) sector_pose_supported=0 raw_phase=after',log)
        private={}
        for at,phase,data in re.findall(r'CONSTRUCTION_RAW_LOCAL tick=(\d+) phase=(before|after) data=([^\r\n]+)',log):
            if phase in private:errors.append(prefix+'duplicate private raw phase')
            private[phase]=(int(at),data)
        if builder_first_tick is not None:
            try:
                if len(begins)!=1 or len(ends)!=1 or set(private)!={'before','after'}:raise ValueError('passive enrollment receipt missing/duplicate')
                at,previous,handle,native_id=begins[0];end_at,registry=ends[0];at=int(at)
                if at!=int(end_at) or at!=registry_bound or int(previous) not in (at-1,at) or handle!=BUILDER_HANDLE or any(t!=at for t,_ in private.values()):raise ValueError('passive enrollment receipt timing/identity')
                if int(native_id) in {value for _,value in raw_target_callbacks.values()}:raise ValueError('builder and target lifetime overlap')
                if set(raw_chunks)!={(at,'before'),(at,'after')}:raise ValueError('passive raw phase coverage')
                before=raw_sample(raw_chunks[at,'before'],handle=handle,tick=at,slot=registry_slot)
                after=raw_sample(raw_chunks[at,'after'],handle=handle,tick=at,slot=registry_slot)
                raw_same_frame(before,after,private['before'][1],private['after'][1],native_id=native_id)
                registry_evidence(registry,at,True)
            except ValueError as exc:errors.append(prefix+str(exc))
    elif registry_schema and builder_first_tick is not None:
        begins=re.findall(r'CONSTRUCTION_BIND_BEGIN tick=(\d+) previous_admitted=(\d+) handle=([0-9a-f]{16})(?: native_id=\d+)? pose=([^\r\n]+)',log)
        ends=re.findall(r'CONSTRUCTION_BIND_END tick=(\d+) registry=(\S+) pose=([^\r\n]+)',log)
        try:
            if len(begins)!=1 or len(ends)!=1:raise ValueError('native builder binding receipt missing/duplicate')
            at,previous,handle,before=begins[0];end_at,raw,after=ends[0];at=int(at)
            if at!=int(end_at) or at!=registry_bound or int(previous) not in (at-1,at) or handle!=BUILDER_HANDLE or before!=after or not valid_pose(before):raise ValueError('native builder enrollment timing/pose changed')
            registry_evidence(raw,at,True)
        except ValueError as exc:errors.append(prefix+str(exc))
    passive=state_schema=='CONSTRUCTION_6'
    return command_states,applied,native_states,{'empty_observed':had_empty,'progress_observed':had_progress,'builder_order_observed':had_order,'builder_deployed':had_deployed,'module_completed':had_complete,'builder_pose_first_tick':None if passive else builder_first_tick,'builder_raw_first_tick':builder_first_tick if passive else None,'builder_bound_tick':registry_bound,'builder_first_update':registry_update,'builder_unarmed_scope':unarmed_scope,'target_pose_first_ticks':{} if passive else target_first,'target_callback_first_ticks':target_first,'sector_pose_supported':not passive,'strict_completion_supported':not passive,'seeded_resources':{w:a for w,(_,a) in seeds.items()}}
