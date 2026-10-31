"""Join native participant, transport and portable subset evidence after cleanup.

This is a bounded fixture result, never whole-game or nonzero-command parity.
All inputs are private evidence collected by the exact reviewed runners.
"""
import hashlib
from pathlib import PureWindowsPath
import re

from native_portable import canonical_json, validate_contract
from native_portable_lane import PROFILES
from linux_portable_probe import (held_clock_samples, terminal_status, RotationEvidence,
                                  parse_rotation, ROTATION_FIELDS, MODE, EULER_MODE)
from linux_native_portable_run import reviewed_capability
from linux_load_observer_probe import complete_status, original_regions
from native_portable_rotation import rotation_evidence
from validate_native_portable import coordinator_evidence, transport_evidence, validate_endpoint, positive_int


def windows_terminal(log, run, contract, agent, ticks):
    if not isinstance(log, str) or len(log.encode()) > 8 * 1024 * 1024: raise ValueError('Windows log unbounded')
    if re.search(r'PORTABLE_BOOT (?:ERROR|CANCEL)|PORTABLE_NATIVE TERMINAL result=failure', log):
        raise ValueError('Windows portable loader or terminal refused')
    lines = [line.split('[X4LAN] PORTABLE_NATIVE TERMINAL ', 1)[1].strip()
             for line in log.splitlines() if '[X4LAN] PORTABLE_NATIVE TERMINAL ' in line]
    if len(lines) != 1 or len(lines[0]) > 1024: raise ValueError('one fresh Windows terminal receipt required')
    fields = {}
    for word in lines[0].split():
        if not re.fullmatch(r'[a-z_]+=[a-z0-9_-]+', word): raise ValueError('Windows terminal encoding differs')
        key, value = word.split('=')
        if key in fields: raise ValueError('duplicate Windows terminal field')
        fields[key] = value
    expected = dict(result='success', slot='1', run=run, contract=validate_contract(contract),
        snapshot=contract['snapshot_sha256'], agent=agent, schema=contract['schema_sha256'], saved='0'*16,
        stage='complete', wire_stage='closed', configured='1', wire='1', draining='0', closed='1',
        terminal_restored='1', active='0', reason='1', failures='0', last=str(ticks), issued=str(ticks),
        completed=str(ticks), frames=str(ticks+1), calls=str(ticks+1), returns=str(ticks+1))
    if fields != expected: raise ValueError('Windows terminal identity or native counts differ')
    if 'import_math' in contract:
        math = rotation_evidence(log.splitlines(), contract, run, agent)
        if not math['valid']:
            raise ValueError('Windows rotation proof failed: ' + '; '.join(math['errors']))
        fields['rotation'] = math
    elif 'PORTABLE_ROTATION TERMINAL' in log:
        raise ValueError('Windows rotation receipt in legacy contract')
    return fields


def linux_rotation_evidence(probe, contract):
    """Recheck raw import counts, Load ownership and independently read code bytes."""
    capability = reviewed_capability(contract)
    if capability['mode'] != EULER_MODE:
        raise ValueError('Linux rotation evidence requires Euler capability')
    if any(probe.get(key) != value for key, value in capability.items()):
        raise ValueError('Linux rotation probe capability differs')
    if any(probe.get(key) is not True for key in ('rotation_import_verified', 'rotation_receipt_seen')):
        raise ValueError('Linux rotation receipt or import proof missing')
    raw = probe['final_rotation_status']
    if (not isinstance(raw, dict) or set(raw) != ROTATION_FIELDS
            or any(type(value) is not str for value in raw.values())):
        raise ValueError('Linux rotation terminal fields differ')
    row = parse_rotation('X4MP_AGENT ROTATION ' + ' '.join(f'{key}={value}' for key, value in raw.items()))
    if row != raw or probe.get('last_rotation_status') != row:
        raise ValueError('Linux final rotation receipt differs from last observation')
    pointers = []
    for key in ('rotation_table', 'rotation_manager'):
        value = probe[key]
        if (not isinstance(value, str) or not re.fullmatch('[1-9a-f][0-9a-f]{0,11}', value)
                or not 0 < int(value, 16) < 1 << 47):
            raise ValueError('Linux independent rotation pointer invalid: ' + key)
        pointers.append(int(value, 16))
    evidence = RotationEvidence(capability)
    evidence.observe(row)
    if not evidence.terminal(probe['final_native_status'], probe['final_load_status'], *pointers):
        raise ValueError('Linux rotation counts or Load ownership differ')
    if not evidence.drained(row, probe['rotation_restoration']):
        raise ValueError('Linux rotation code restoration differs')
    return {'valid': True, 'status': row, 'native_processes_verified': False,
            'scope': 'Linux saved navigation/player bank Euler import and independently observed restoration only'}


def combined_evidence(session, bridge, linux, log, contract, participants, *, artifacts, policy,
                      source_hashes, review_sha256, ticks=12, hold_tick=3):
    result = {'valid': False, 'native_pair_verified': False, 'errors': [],
        'scope': contract['coverage'], 'unsupported': contract['unsupported'], 'commands': 'zero only',
        'load_boundary': 'joined main pre-FrameTick; native load tail included'}
    try:
        coordinator = bridge['coordinator']
        protocol = coordinator_evidence(coordinator, contract, participants, ticks=ticks, hold_tick=hold_tick, hold_seconds=3)
        transport = transport_evidence(session, bridge, bridge['remote'], policy=policy,
                                       source_hashes=source_hashes, contract=contract)
        if not protocol['valid'] or not transport['valid']: raise ValueError('protocol or transport evidence incomplete')
        if session['verdict'] != 'pass' or session['exit_code'] != 0 or len(session['clients']) != 1:
            raise ValueError('released Windows session did not pass')
        client = session['clients'][0]; identity = client['portable_native_identity']
        if client['profile'] not in PROFILES or client['verdict'] != 'pass' or client.get('portable_native_identity_error'):
            raise ValueError('Windows disposable participant identity failed')
        if any(identity.get(key) is not True for key in ('module_observed', 'owned_job_verified', 'owned_process_exited', 'artifact_leases_closed_after_exit')):
            raise ValueError('Windows process/module/exit proof incomplete')
        if identity['pid'] != client['x4']['pid'] or positive_int(identity['created'], 'Windows process creation') < identity['leases_ready_filetime']:
            raise ValueError('Windows process handle binding differs')
        for name, expected in (('executable', participants['1']['executable_sha256']), ('module', participants['1']['agent_sha256'])):
            row = identity[name]
            if row['sha256'] != expected or row['write_delete_sharing'] is not False: raise ValueError('Windows leased artifact differs')
        if (PureWindowsPath(identity['mapped_module']['path']) != PureWindowsPath(identity['module']['path'])
                or identity['mapped_module']['image_size'] != identity['module']['image_size']):
            raise ValueError('Windows mapped module differs from leased bytes')
        installed = client['portable_installed']
        if (installed['shared_files'] != contract['fixture_files'] or installed['participant_files'] != artifacts
                or installed['snapshot']['sha256'] != contract['snapshot_sha256']
                or client['native_snapshot']['restored'] is not True or client['lane_restored'] is not True):
            raise ValueError('Windows installed bytes or restored profile differ')
        recheck = client['portable_launch_recheck']
        if any(recheck.get(key) is not True for key in ('shared_files_unchanged', 'participant_files_unchanged', 'snapshot_unchanged', 'autostart_and_profile_unchanged')):
            raise ValueError('Windows launch-boundary inventory incomplete')
        terminal = windows_terminal(log, client['run_id'], contract, participants['1']['agent_sha256'], ticks)
        endpoint = validate_endpoint(bridge['local_native_endpoint'], 'loopback_udp')
        ownership = identity['udp_source_ownership']
        if (ownership['pid'], ownership['created']) != (identity['pid'], identity['created']): raise ValueError('Windows UDP process generation differs')
        matches = [row for row in ownership['sources'] if row['address'][0] in ('127.0.0.1', '0.0.0.0') and row['address'][1] == endpoint[1]]
        if (len(matches) != 1 or matches[0]['observations'] < 2
                or not identity['created'] <= matches[0]['first_filetime'] <= matches[0]['last_filetime']):
            raise ValueError('Windows received source lacks repeated owned-process observations')
        if (linux.get('error') or linux.get('cleanup_errors') or linux['probe_exit_code'] != 0
                or linux['review_sha256'] != review_sha256 or linux['native_linux_game'] is not True
                or linux['contract_sha256'] != validate_contract(contract)
                or linux['synthetic_coordinator'] is not False or linux['native_pair_verified'] is not False
                or linux['game_sha256'] != participants['2']['executable_sha256']
                or linux['agent_sha256'] != participants['2']['agent_sha256']):
            raise ValueError('Linux owned native launch or reviewed artifact failed')
        for key in ('native_pidfd_exited', 'xvfb_pidfd_exited', 'stock_stamps_unchanged', 'owned_native_exited',
                    'independent_probe_exited', 'private_xvfb_exited', 'steam_identity_unchanged'):
            if linux.get(key) is not True: raise ValueError('Linux cleanup or preserved state missing: '+key)
        probe = linux['probe']
        if (probe['verdict'] != 'pass' or probe['native_pair_verified'] is not False
                or probe['contract_sha256'] != validate_contract(contract) or probe['agent_sha256'] != participants['2']['agent_sha256']
                or probe['private_config_sha256'] != linux['private_config_sha256']):
            raise ValueError('Linux independent probe binding failed')
        for key in ('owned_native_verified', 'native_frame0_verified', 'native_terminal_verified', 'restoration_verified', 'native_endpoint_pinned_before_request'):
            if probe.get(key) is not True: raise ValueError('Linux native proof missing: '+key)
        if probe['claim'] != {'pid': linux['native_pid'], 'start_ticks': linux['native_start_ticks'], 'uid': linux['native_uid']}:
            raise ValueError('Linux claim and owned launch differ')
        mode = EULER_MODE if 'import_math' in contract else MODE
        if not terminal_status(probe['final_native_status'], ticks, mode) or not complete_status(probe['final_load_status']):
            raise ValueError('Linux native terminal lifecycle differs')
        math = None
        if mode == EULER_MODE:
            capability = reviewed_capability(contract)
            if any(linux.get(key) != value for key, value in capability.items()):
                raise ValueError('Linux launch rotation capability differs')
            math = linux_rotation_evidence(probe, contract)
        if not original_regions(probe['restoration']) or probe['restoration']['loaded_padding']['matches_native_loaded_baseline'] is not True:
            raise ValueError('Linux independent restoration failed')
        held_clock_samples(probe['independent_hold_samples'], coordinator['frames'][str(hold_tick)]['2'])
        lifetime = probe['coordinator_lifetime']; ready = bridge['remote']
        if (not lifetime['terminal_bound_while_alive'] or (lifetime['pid'], lifetime['start_ticks']) != (ready['pid'], ready['start_ticks'])
                or lifetime['terminal_report_sha256'] != hashlib.sha256(canonical_json(coordinator)).hexdigest()):
            raise ValueError('Linux coordinator live terminal differs from SSH evidence')
        source = validate_endpoint(coordinator['peer_endpoints']['2'], 'loopback_udp')
        first, last = probe['native_endpoint_first'], probe['native_endpoint_last']
        if (first != last or first['source'] != source or first['destination'] != ['127.0.0.1', ready['port']]
                or first['pid'] != linux['native_pid'] or first['start_ticks'] != linux['native_start_ticks']
                or positive_int(first['inode'], 'Linux socket inode') <= 0):
            raise ValueError('Linux peer2 socket provenance differs')
        result.update(valid=True, native_pair_verified=True, protocol=protocol, transport=transport,
            windows_terminal=terminal, windows_identity={'pid': identity['pid'], 'created': identity['created']},
            linux_identity=probe['claim'], windows_udp_source=endpoint, windows_udp_binding=matches[0]['address'], linux_udp_source=source)
        if math is not None: result['linux_rotation'] = math
    except (ValueError, KeyError, TypeError, AttributeError, OverflowError) as error:
        result['errors'].append(str(error))
    return result
