"""Independent evidence checks for the bounded native management diagnostic."""
import re


def _rows(log, pattern, errors, label):
    result={}
    for match in re.finditer(pattern,log):
        tick=int(match[1])
        if tick in result:errors.append(f'duplicate management {label} at {tick}')
        result[tick]=match.groups()[1:]
    return result


def evidence(log, slot, states, errors):
    prefix=f'player {slot} management '
    inputs=_rows(log,r'NATIVE_GATE INPUTS tick=(\d+) mode=management p1=([0-9a-f]{8}) p2=([0-9a-f]{8})',errors,'inputs')
    samples=_rows(log,r'NATIVE_GATE MANAGEMENT_STATE tick=(\d+) state=([^\r\n]+)',errors,'state')
    receipts=_rows(log,r'NATIVE_GATE MANAGEMENT_RECEIPTS tick=(\d+) data=([^\r\n]+)',errors,'receipts')
    effects={}
    denials={}
    for t,p,op,asset,before,after in re.findall(r'NATIVE_GATE MANAGEMENT_EFFECT tick=(\d+) slot=([12]) op=(\d+) asset=(\d+) before=([^\s]*) after=([^\s]*)',log):
        key=int(t),int(p)
        if key in effects:errors.append(prefix+f'duplicate effect {key}')
        effects[key]=(int(op),int(asset),before,after)
    for t,p,reason,before,target,after in re.findall(r'NATIVE_GATE MANAGEMENT_DENIAL tick=(\d+) slot=([12]) reason=(DENIED_FUNDS|DENIED_STALE_ORDER) before=([^\s]+) target=([^\s]+) after=([^\s]+)',log):
        key=int(t),int(p)
        if key in denials:errors.append(prefix+f'duplicate denial {key}')
        denials[key]=reason,before,target,after
    if re.search(r'Error in MD cue md\.X4LAN_NativeManagement|NATIVE_GATE ERROR',log):
        errors.append(prefix+'native error')
    expected_ticks=set(states)-{0}
    if set(inputs)!=expected_ticks or set(samples)!=expected_ticks:errors.append(prefix+'incomplete tick evidence')
    revision={1:0,2:0,3:0};applied={};seen_receipts={};snapshot={};command_states={};management_states={};previous_money=None;schema=None;seen_denials=set()
    for tick in sorted(expected_ticks):
        try:
            commands=tuple(states[tick-1][str(p)]['command'] for p in (1,2))
            if inputs[tick]!=commands:raise ValueError('admitted input differs')
            state=samples[tick][0]
            management_states[tick]=state
            command_states[tick]='INPUTS_1:management:'+':'.join(commands)
            fields=state.split(':')
            if fields[0] not in ('MANAGEMENT_1','MANAGEMENT_2') or fields[1]!=str(tick):raise ValueError('wrong state schema/tick')
            if schema is not None and schema!=fields[0]:raise ValueError('management schema changed')
            schema=fields[0];extended=schema=='MANAGEMENT_2'
            wanted=[]
            for player,command in enumerate(commands,1):
                if command=='00000000':continue
                op,asset,value,oldrev=(int(command[i:i+2],16) for i in (0,2,4,6))
                valid=(op in ((64,71) if extended else (64,)) and asset==1 and value<=5) or (op==66 and asset==1 and value in (1,2)) or (op in (67,68) and asset in (2,3) and value==0) or (op==69 and asset==1 and value in (0,1)) or (extended and op==70 and asset==1 and value==0)
                if not valid:raise ValueError('invalid submitted command')
                status='DENIED_OWNER' if asset>1 and player!=asset-1 else 'STALE_REVISION' if oldrev!=revision[asset] else 'REVISION_EXHAUSTED' if oldrev==255 else 'APPLIED'
                key=tick,player
                if status=='APPLIED' and key in denials:
                    reason,before,target,after=denials[key]
                    if before!=after:raise ValueError('refused action changed native state')
                    if reason=='DENIED_FUNDS':
                        money=tuple(map(int,before.split(',')))
                        target_amount=1000000000000 if op==70 else value*10000
                        if not extended or op not in (64,70,71) or len(money)!=3 or int(target)!=target_amount or target_amount-money[2 if op==71 else 1]<=money[0]:raise ValueError('invalid insufficient funds refusal')
                        if previous_money is not None and money!=previous_money:raise ValueError('refusal native balances differ')
                    elif op!=68:raise ValueError('stale order refusal for wrong action')
                    status=reason;seen_denials.add(key)
                if status=='APPLIED':
                    effect=effects.get(key)
                    if not effect or effect[:2]!=(op,asset):raise ValueError('missing/mismatched native effect')
                    before,after=effect[2:]
                    if op in (64,70,71):
                        oldmoney=tuple(map(int,before.split(',')));newmoney=tuple(map(int,after.split(',')))
                        if len(oldmoney)!=(3 if extended else 2) or len(newmoney)!=len(oldmoney):raise ValueError('native account census incomplete')
                        if previous_money is not None and previous_money!=oldmoney:raise ValueError('budget precondition differs from last native state')
                        changed=2 if op==71 else 1;target=1000000000000 if op==70 else value*10000
                        if min(*oldmoney,*newmoney)<0 or newmoney[changed]!=target or sum(oldmoney)!=sum(newmoney) or (extended and oldmoney[3-changed]!=newmoney[3-changed]):raise ValueError('money not conserved')
                        snapshot['money']=newmoney;previous_money=newmoney
                    elif op==66:
                        if after!=('default','closedloop')[value-1]:raise ValueError('wrong native build method')
                        snapshot['method']=after
                    elif op==69:
                        ownrule,rule=map(int,after.split(','))
                        if ownrule!=value:raise ValueError('wrong native trade rule override')
                        snapshot['rule']=(ownrule,rule)
                    else:
                        b=before.split(';');a=after.split(';')
                        if b[0]!='ORDERS_1' or a[0]!='ORDERS_1' or int(a[2])-int(b[2])!=(1 if op==67 else -1):raise ValueError('native order count did not change correctly')
                        if op==67 and ',Wait,' not in after:raise ValueError('native Wait order absent')
                        snapshot['order'+str(asset)]=after
                    revision[asset]+=1
                    applied[key]=status
                elif key in effects:raise ValueError('rejected command had native effect')
                wanted.append(','.join(map(str,(player,op,asset,value,oldrev,status,revision[asset]))))
            payload=';'.join(wanted)
            if payload:
                if receipts.get(tick)!=(payload,):raise ValueError('receipt differs from command admission')
                seen_receipts[tick]=payload
            elif tick in receipts:raise ValueError('unexpected receipt')
            if tick<20:
                if fields[2:]!=['pending'] or payload:raise ValueError('early management state/command')
            else:
                if len(fields)!=(12 if extended else 11):raise ValueError('malformed native management state')
                wallet,budget=int(fields[2]),int(fields[3]);rule=(int(fields[5]),int(fields[6]))
                money=(wallet,budget,int(fields[11])) if extended else (wallet,budget)
                if min(wallet,budget)<0 or fields[7]!=','.join(str(revision[a]) for a in (1,2,3)):raise ValueError('bad balance/revision state')
                if fields[10]!=payload:raise ValueError('state receipt differs')
                if 'money' in snapshot and snapshot['money']!=money:raise ValueError('unaccounted shared funds change')
                if 'method' in snapshot and snapshot['method']!=fields[4]:raise ValueError('build method state differs')
                if 'rule' in snapshot and snapshot['rule']!=rule:raise ValueError('trade rule state differs')
                for asset,index in ((2,8),(3,9)):
                    # In the effect frame, compare complete independently sampled native queues.
                    if any(k[0]==tick and effects[k][1]==asset for k in effects) and snapshot.get('order'+str(asset))!=fields[index]:raise ValueError('order state differs from effect')
                previous_money=money
        except (ValueError,KeyError,IndexError) as exc:
            errors.append(prefix+f'{tick}: {exc}')
    if set(effects)!=set(applied):errors.append(prefix+'unexpected native effect')
    if set(denials)!=seen_denials:errors.append(prefix+'unexpected native denial')
    if max(expected_ticks,default=0)>=300:
        expected_applied=12 if schema=='MANAGEMENT_2' else 10
        if len(applied)!=expected_applied or sum(row.count('STALE_REVISION') for row in seen_receipts.values())!=1 or sum(row.count('DENIED_OWNER') for row in seen_receipts.values())!=1 or (schema=='MANAGEMENT_2' and sum(row.count('DENIED_FUNDS') for row in seen_receipts.values())!=1):
            errors.append(prefix+'planned coverage incomplete')
    return command_states,applied,management_states
