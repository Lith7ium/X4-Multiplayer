"""Independent evidence for the inactive cancellation-only diagnostic protocol.

Local uint64 order IDs are checked per client and never returned as portable state.
`commands` contains the two admitted commands at each applied (not proposal) tick.
Activation also requires the caller to verify the preceding actual build lifecycle.
"""
import re


def identities(raw):
    if raw == 'none':
        return []
    values = raw.split(',')
    if any(not re.fullmatch(r'[1-9]\d{0,19}', value) or int(value) >= 2**64 for value in values):
        raise ValueError('invalid exact local order identity')
    result = list(map(int, values))
    if len(result) > 16 or len(set(result)) != len(result):
        raise ValueError('invalid local order identity set')
    return result


def local_snapshot(raw, retained):
    match = re.fullmatch(r'CANCEL_NATIVE_LOCAL_1;retained=(\d+);queue=([^;]+);operational=([01])', raw)
    if not match or identities(match[1]) != [retained]:
        raise ValueError('native cancellation lifetime changed')
    if match[3] == '0':
        if match[2] != 'unavailable':
            raise ValueError('lost builder has fabricated order census')
        return None
    return identities(match[2])


def evidence(log, commands, errors, require_completion=True, build_verified=False):
    portable_states = {}
    completed = False
    portable_capture = None
    try:
        captures = re.findall(r'NATIVE_GATE CONSTRUCTION_CANCEL_CAPTURE tick=(\d+) portable=(\S+) local=(\S+)', log)
        if len(captures) != 1:
            raise ValueError('missing or duplicate deployment capture')
        capture_tick, portable_capture, private = captures[0]
        capture_tick = int(capture_tick)
        cap = re.fullmatch(r'ORDER_CAPTURE_1;ticket=(\d+-2-2-1);queue_before=(\d+);queue_after=(\d+);index=(\d+);kind=DeployToStation;new=1', portable_capture)
        local = re.fullmatch(r'ORDER_CAPTURE_LOCAL_1;ticket=(\d+-2-2-1);identity=(\d+);before=([^;]+);after=([^;]+)', private)
        if not cap or not local or cap[1] != local[1]:
            raise ValueError('deployment capture ticket/shape mismatch')
        deploy_tick = int(cap[1].split('-')[0])
        if not 0 < deploy_tick <= capture_tick <= deploy_tick+60:
            raise ValueError('deployment capture is outside acknowledgement window')
        retained = identities(local[2])[0]
        before, after = identities(local[3]), identities(local[4])
        index = int(cap[4])
        if int(cap[2]) != len(before) or int(cap[3]) != len(after) or len(after) != len(before)+1 or not 1 <= index <= len(after):
            raise ValueError('deployment capture counts/index mismatch')
        if retained in before or after[index-1] != retained or after[:index-1]+after[index:] != before:
            raise ValueError('deployment capture reused identity or changed prior orders')
        native_acks = re.findall(r'NATIVE_GATE CONSTRUCTION_ACK tick=(\d+) ticket=([\d-]+) kind=deploy result=1\|DeployToStation(?:\r?$)', log, re.M)
        if native_acks != [(str(capture_tick), cap[1])]:
            raise ValueError('capture lacks the actual admitted deployment ACK')
        states = {}
        for at, value in re.findall(r'NATIVE_GATE CONSTRUCTION_CANCEL_STATE tick=(\d+) state=([^\r\n]+)', log):
            at = int(at)
            if at in states:
                raise ValueError('duplicate cancellation state')
            states[at] = value
        if not states or set(states) != set(commands) or sorted(states) != list(range(capture_tick+1, max(states)+1)):
            raise ValueError('cancellation frame coverage incomplete')
        local_rows = {}
        for at, phase, value in re.findall(r'NATIVE_GATE CONSTRUCTION_CANCEL_LOCAL tick=(\d+) phase=(before|after|ack) state=(\S+)', log):
            key = int(at), phase
            if key in local_rows:
                raise ValueError('duplicate local cancellation census')
            local_rows[key] = local_snapshot(value, retained)
        md_rows = {}
        for source, observed, value in re.findall(r'NATIVE_GATE CONSTRUCTION_MD tick=(\d+) observed=(\d+) state=([^\r\n]+)', log):
            observed = int(observed)
            parts = value.split('|')
            if observed in md_rows or len(parts) != 9 or parts[0] != source or parts[3] not in ('0','1') or parts[4] not in ('0','1'):
                raise ValueError('invalid native construction MD sample')
            md_rows[observed] = (int(source), int(parts[3]), int(parts[4]))
        revision, status, ticket, request_tick = 1, 'IDLE', 'none', 0
        earlier = [at for at in md_rows if at <= capture_tick]
        latest = md_rows[max(earlier)] if earlier else None
        consumed_local = set()
        for tick in sorted(states):
            if len(commands[tick]) != 2:
                raise ValueError('cancellation needs both admitted command slots')
            fields = states[tick].split(':')
            if len(fields) != 6 or fields[:2] != ['CANCEL_BATCH_1', str(tick)]:
                raise ValueError('invalid cancellation state schema/tick')
            if tick in md_rows:
                source = md_rows[tick]
                if source[0] >= tick or (latest and source[0] <= latest[0]):
                    raise ValueError('native cancellation MD sample stale/future')
                latest = source
            expected_md = 'pending' if latest is None else ','.join(map(str, latest))
            if fields[4] != expected_md:
                raise ValueError('cancellation state fabricated its native MD sample')
            ack = 'none'
            if status == 'PENDING' and tick-request_tick > 120:
                raise ValueError('cancellation ACK exceeded deadline')
            if status == 'PENDING' and latest and latest[0] > request_tick and latest[1:] == (0,0):
                observed_queue = local_rows.get((tick,'ack'))
                if observed_queue is None or retained in observed_queue:
                    raise ValueError('ACK lacks native order absence/live builder')
                consumed_local.add((tick,'ack'))
                status, ack, completed = 'ACK', ticket, True
            rows = fields[2].split(';') if fields[2] else []
            wanted = []
            for slot, command in enumerate(commands[tick], 1):
                if not re.fullmatch(r'(?:00000000|4b0200[0-9a-f]{2})', command):
                    raise ValueError('malformed admitted cancellation command')
                if command == '00000000':
                    continue
                expected_revision = int(command[6:8],16)
                outcome = ('DENIED_OWNER' if slot != 2 else 'DENIED_BUSY' if status == 'PENDING' else
                           'DENIED_COMPLETE' if status == 'ACK' else 'STALE_REVISION' if expected_revision != revision else 'PENDING')
                result_ticket = 'none'
                if outcome == 'PENDING':
                    old = local_rows.get((tick,'before'))
                    new = local_rows.get((tick,'after'))
                    if old is None or new is None or len(old)<index or old[index-1]!=retained or old.count(retained) != 1 or new != [v for v in old if v != retained]:
                        raise ValueError('native cancellation removal/preservation evidence invalid')
                    consumed_local.update(((tick,'before'),(tick,'after')))
                    revision += 1
                    request_tick, ticket, status = tick, f'{tick}-{slot}-2-{revision}', 'PENDING'
                    result_ticket = ticket
                wanted.append(','.join(map(str,(slot,75,2,expected_revision,outcome,revision,result_ticket))))
            if rows != wanted or fields[3] != ack:
                raise ValueError('cancellation ordered receipt/ACK differs')
            expected_guard = f'CANCEL_GUARD_1;deploy={cap[1]};ticket={ticket};tick={request_tick};revision={revision};status={status}'
            if fields[5] != expected_guard:
                raise ValueError('native cancellation guard state differs')
            portable_states[tick] = states[tick]
        if set(local_rows) != consumed_local:
            raise ValueError('unsolicited local cancellation census')
        if require_completion and (not completed or not build_verified):
            raise ValueError('cancellation completion or preceding actual build unverified')
    except (ValueError, IndexError, KeyError, TypeError) as exc:
        errors.append('construction cancellation '+str(exc))
    return {'portable_capture':portable_capture, 'states':portable_states, 'completed':completed and not errors}
