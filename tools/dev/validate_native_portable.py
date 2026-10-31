"""Independently validate portable protocol evidence; native identity is separate.

These checks deliberately never promote coordinator or SSH results to a native
two-game pass. Native launch, loaded-module, restoration and cleanup evidence
must also be bound by the platform runners before combined acceptance.
"""
import math
from pathlib import PureWindowsPath

from native_portable import (COVERAGE, SCHEMA, UNSUPPORTED, compare, encode_frame,
                             exact_keys, hexword, sha, validate_contract)
from native_portable_coordinator import real_clock, validate_participants


def positive_int(value, label):
    if type(value) is not int or value <= 0:
        raise ValueError(label + ': positive integer required')
    return value


def finite_number(value, label):
    if type(value) not in (float, int) or not math.isfinite(value) or value < 0:
        raise ValueError(label + ': nonnegative finite number required')
    return value


def validate_endpoint(value, transport):
    exact_keys(value, ('transport', 'address'), 'peer endpoint')
    address = value['address']
    if (value['transport'] != transport or not isinstance(address, list) or len(address) != 2
            or address[0] != '127.0.0.1' or type(address[1]) is not int
            or not (address[1] == 1 if transport == 'ssh_virtual' else 1024 <= address[1] <= 65535)):
        raise ValueError('peer endpoint kind or bounded loopback address differs')
    return address


def coordinator_evidence(report, contract, participants, *, ticks, hold_tick, hold_seconds):
    """Re-encode every frame, compare fields, clocks, receipts and both hold observations."""
    result = {'valid': False, 'errors': [], 'frame_roots': {}, 'scope': list(COVERAGE),
              'unsupported': list(UNSUPPORTED), 'native_processes_verified': False}
    try:
        contract_sha = validate_contract(contract); validate_participants(participants)
        if (type(ticks) is not int or type(hold_tick) is not int
                or not 1 <= hold_tick < ticks <= 120 or not 0 < hold_seconds <= 10):
            raise ValueError('invalid expected bounded run')
        finite_number(hold_seconds, 'expected hold')
        if (report['schema'] != SCHEMA or report['contract_sha256'] != contract_sha
                or report['participants'] != participants
                or report['native_processes_verified_by_coordinator'] is not False):
            raise ValueError('coordinator contract, participants or scope differs')
        if (report['verdict'] != 'pass' or report['failure'] is not None
                or report['first_difference'] is not None):
            raise ValueError('coordinator did not complete without divergence')
        if (type(report['ticks']) is not int or report['ticks'] != ticks
                or type(report['released']) is not int or report['released'] != ticks - 1
                or report['hello_slots'] != [1, 2]):
            raise ValueError('coordinator admission coverage differs')
        exact_keys(report['peer_endpoints'], ('1', '2'), 'peer endpoints')
        first_transport = report['peer_endpoints']['1']['transport']
        if first_transport not in ('ssh_virtual', 'loopback_udp'): raise ValueError('unknown first peer transport')
        validate_endpoint(report['peer_endpoints']['1'], first_transport)
        validate_endpoint(report['peer_endpoints']['2'], 'loopback_udp')
        frames = report['frames']
        exact_keys(frames, (str(tick) for tick in range(ticks + 1)), 'completed frames')
        prior = {}; real_baseline = {}; roots = {}
        for tick in range(ticks + 1):
            rows = frames[str(tick)]; exact_keys(rows, ('1', '2'), 'frame participants')
            roots[str(tick)] = {}
            for slot in ('1', '2'):
                row = rows[slot]; exact_keys(row, ('state', 'real_bits', 'diagnostic_sha256'), 'frame observation')
                hexword(row['diagnostic_sha256'], 64, 'platform diagnostic')
                raw = encode_frame(row['state'], contract)
                if row['state']['tick'] != tick:
                    raise ValueError('frame key and receipt tick differ')
                roots[str(tick)][slot] = sha(raw)
                game = real_clock(row['state']['game_bits']); real = real_clock(row['real_bits'])
                if tick == 0:
                    real_baseline[slot] = real
                elif abs(game - prior[slot] - 1 / 60) > 1e-8:
                    raise ValueError('game clock step differs at tick ' + str(tick) + ' slot ' + slot)
                if abs(real - real_baseline[slot] - tick / 60) > 1e-8:
                    raise ValueError('relative real clock differs at tick ' + str(tick) + ' slot ' + slot)
                prior[slot] = game
            comparison = compare(rows['1']['state'], rows['2']['state'], contract)
            if not comparison['equal']:
                result['first_difference'] = {'tick': tick, **comparison}
                raise ValueError('portable fields differ at tick ' + str(tick))
        result['frame_roots'] = roots
        exact_keys(report['terminal'], ('1', '2'), 'terminal participants')
        expected = {'tick': ticks, 'calls': ticks + 1, 'returns': ticks + 1, 'restored': 1}
        for slot in ('1', '2'):
            row = report['terminal'][slot]
            if row != expected or any(type(value) is not int for value in row.values()):
                raise ValueError('terminal original-once/restoration receipt differs')
        if (type(report['hold_tick']) is not int or report['hold_tick'] != hold_tick
                or finite_number(report['hold_required_seconds'], 'required hold') != hold_seconds):
            raise ValueError('declared hold differs from expected run')
        duration = finite_number(report['hold_seconds'], 'actual hold')
        if duration < hold_seconds:
            raise ValueError('actual hold was too short')
        proof = report['hold_release_observations']
        exact_keys(proof, ('1', '2'), 'hold release participants')
        for slot in ('1', '2'):
            row = proof[slot]; expected_frame = frames[str(hold_tick)][slot]
            exact_keys(row, ('elapsed', 'game_bits', 'real_bits', 'frame_sha256'), 'hold release')
            elapsed = finite_number(row['elapsed'], 'hold observation elapsed')
            if not hold_seconds <= elapsed <= duration:
                raise ValueError('both current-tick observations must follow the required hold')
            if (row['game_bits'] != expected_frame['state']['game_bits']
                    or row['real_bits'] != expected_frame['real_bits']
                    or row['frame_sha256'] != roots[str(hold_tick)][slot]):
                raise ValueError('hold release did not observe the immutable completed frame')
        result['valid'] = True
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError) as error:
        result['errors'].append(str(error))
    return result


def transport_evidence(session, bridge, ready, *, policy, source_hashes, contract=None):
    """Require exact remote source and the released server's owned SSH endpoint audit."""
    result = {'valid': False, 'errors': [], 'native_processes_verified': False}
    try:
        names = ('native_portable.py', 'native_portable_coordinator.py', 'native_portable_ssh.py')
        if contract is not None:
            digest = validate_contract(contract)
            if bridge['coordinator'].get('contract_sha256') != digest:
                raise ValueError('transport contract binding differs')
            if 'import_math' in contract:
                names += ('portable_import_capability.py',)
                # Archived003/004 used the standalone v1 module. Today's
                # version-dispatch module imports v1 and needs both source pins.
                historical = (contract['import_math']['mode'] == 'portable-fixture12-euler-v1'
                    and source_hashes.get('portable_import_capability.py') ==
                    '86d8c39f2ea119b04ceebc8767ed4d2f099127bf92f328ac38a04314ba5683d8'
                    and 'portable_import_capability_v1.py' not in source_hashes)
                if not historical: names += ('portable_import_capability_v1.py',)
        exact_keys(source_hashes, names, 'remote source manifest')
        for name, value in source_hashes.items(): hexword(value, 64, name)
        if (bridge['transport_complete'] is not True or bridge['failure'] is not None
                or type(bridge['ssh_exit_code']) is not int or bridge['ssh_exit_code'] != 0
                or type(bridge['ssh_stderr_bytes']) is not int or bridge['ssh_stderr_bytes'] != 0
                or bridge['native_processes_verified_by_transport'] is not False):
            raise ValueError('SSH transport did not complete in its declared scope')
        positive_int(bridge['sequence'], 'durable checkpoint sequence')
        validate_endpoint(bridge['local_native_endpoint'], 'loopback_udp')
        exact_keys(bridge['coordinator']['peer_endpoints'], ('1', '2'), 'remote peer endpoints')
        validate_endpoint(bridge['coordinator']['peer_endpoints']['1'], 'ssh_virtual')
        validate_endpoint(bridge['coordinator']['peer_endpoints']['2'], 'loopback_udp')
        if bridge['remote'] != ready:
            raise ValueError('remote readiness record differs')
        exact_keys(ready, ('kind', 'platform', 'hashes', 'pid', 'start_ticks', 'port', 'report_path'), 'remote ready')
        if ready['kind'] != 'ready' or ready['platform'] != 'linux' or ready['hashes'] != source_hashes:
            raise ValueError('remote source or platform differs')
        positive_int(ready['pid'], 'remote PID'); positive_int(ready['start_ticks'], 'remote start')
        if type(ready['port']) is not int or not 1024 <= ready['port'] <= 65535:
            raise ValueError('remote loopback port invalid')
        if not isinstance(ready['report_path'], str) or not ready['report_path'].startswith('/'):
            raise ValueError('remote evidence path invalid')
        server = session['server']; observed = server['authorized_transport_exception']
        for key in ('kind', 'remote_ipv4', 'remote_tcp_port', 'command_sha256'):
            if observed[key] != policy[key]: raise ValueError('SSH policy differs: ' + key)
        if policy['kind'] != 'explicit_owned_ssh_only' or policy['remote_tcp_port'] != 22:
            raise ValueError('unexpected SSH policy scope')
        if PureWindowsPath(observed['executable']) != PureWindowsPath(policy['executable']):
            raise ValueError('SSH executable differs')
        identity = observed['identity']
        if not isinstance(identity, list) or len(identity) != 3:
            raise ValueError('SSH identity missing')
        for value in identity: positive_int(value, 'SSH identity')
        if identity[2] != positive_int(server['pid'], 'owned server PID'):
            raise ValueError('SSH is not the recorded server child')
        audit = observed['audit']
        if not isinstance(audit, list) or not audit or len(audit) > 1024:
            raise ValueError('SSH endpoint audit missing or unbounded')
        for sample in audit:
            if not isinstance(sample, list) or not sample:
                raise ValueError('empty SSH endpoint sample')
            for endpoint in sample:
                if (endpoint['proto'] != 'tcp' or endpoint['remote'] != policy['remote_ipv4']
                        or type(endpoint['rport']) is not int or endpoint['rport'] != 22
                        or endpoint['pid'] != identity[0] or endpoint['created'] != identity[1]
                        or endpoint['state'] not in ('SYN_SENT', 'ESTABLISHED', 'CLOSE_WAIT', 'FIN_WAIT1',
                                                   'FIN_WAIT2', 'LAST_ACK', 'CLOSING', 'TIME_WAIT')):
                    raise ValueError('SSH endpoint or process attribution differs')
        if server.get('violations'):
            raise ValueError('released server retained network violations')
        result.update(valid=True, coordinator_identity={'pid': ready['pid'], 'start_ticks': ready['start_ticks']},
                      owned_ssh_identity=identity, audit_samples=len(audit))
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError) as error:
        result['errors'].append(str(error))
    return result
