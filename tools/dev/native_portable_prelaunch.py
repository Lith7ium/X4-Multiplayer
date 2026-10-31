"""Closed startup bindings for an optional Linux-first native session.

These objects authorize only a Windows launch attempt. They never constitute a
portable terminal result or grant a simulation tick. No process or native memory
access is performed here.
"""
import hashlib
import json
import math
import re
import struct

from native_portable import canonical_json, exact_keys, hexword, validate_contract
from native_portable_coordinator import validate_participants
import portable_import_capability as cap

WAIT_SCHEMA = 'x4-portable-windows-wait-v1'
RECEIPT_SCHEMA = 'x4-linux-prelaunch-v1'
FETCH_SCHEMA = 'x4-linux-prelaunch-fetch-v1'
MAX_RECEIPT_BYTES = 65536
MAX_MESSAGE_BYTES = 262144
MAX_AGE_NS = 10_000_000_000
WAIT_KEYS = ('schema nonce session_id client_run runner session_config_raw_sha256 '
    'session_config_canonical_sha256 coordinator_ready_raw_sha256 coordinator_ready_canonical_sha256 '
    'contract_sha256 participants linux_prepared linux_prepared_raw_sha256 linux_prepared_canonical_sha256 '
    'created_monotonic_ns deadline_monotonic_ns startup_timeout_ms timeout_ms ticks ssh_lifetime').split()


def sha(raw): return hashlib.sha256(raw).hexdigest()
def object_sha(value): return sha(canonical_json(value))


def decode(raw, maximum=MAX_MESSAGE_BYTES):
    if not isinstance(raw, bytes) or not 0 < len(raw) <= maximum: raise ValueError('prelaunch JSON size differs')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise ValueError('duplicate prelaunch JSON field')
            result[key] = value
        return result
    def invalid(_): raise ValueError('nonfinite prelaunch JSON')
    return json.loads(raw, object_pairs_hook=unique, parse_constant=invalid)


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high: raise ValueError('prelaunch ' + name + ' differs')
    return value


def run_id(value):
    if not isinstance(value, str) or re.fullmatch('[a-z0-9_-]{1,100}', value) is None:
        raise ValueError('prelaunch run identity differs')
    return value


def validate_prepared(value, contract):
    exact_keys(value, ('schema pid start_ticks request_sha256 review_sha256 stock_files stock_sha256 '
        'preparation_timeout_seconds handoff_timeout_seconds').split(), 'prelaunch prepared receipt')
    if (value['schema'] != 'x4-portable-prepared-v1' or value['stock_files'] != 127
            or value['stock_sha256'] != contract['stock_sha256']
            or value['preparation_timeout_seconds'] != 1800 or value['handoff_timeout_seconds'] != 1200):
        raise ValueError('prelaunch preparation scope differs')
    integer(value['pid'], 2, 1 << 31, 'preparer PID')
    integer(value['start_ticks'], 1, (1 << 64) - 1, 'preparer start')
    for key in ('request_sha256', 'review_sha256', 'stock_sha256'): hexword(value[key], 64, key)
    return value


def validate_ready(value, contract):
    from native_portable_ssh import provenance
    exact_keys(value, ('kind', 'platform', 'hashes', 'pid', 'start_ticks', 'port', 'report_path'), 'prelaunch coordinator ready')
    if value['kind'] != 'ready' or value['platform'] != 'linux' or value['hashes'] != provenance(contract):
        raise ValueError('prelaunch coordinator source identity differs')
    integer(value['pid'], 2, 1 << 31, 'coordinator PID')
    integer(value['start_ticks'], 1, (1 << 64) - 1, 'coordinator start')
    integer(value['port'], 1024, 65535, 'coordinator port')
    if not isinstance(value['report_path'], str) or re.fullmatch('/[A-Za-z0-9_./-]+', value['report_path']) is None:
        raise ValueError('prelaunch coordinator report path differs')
    return value


def validate_wait(value, contract, participants, *, session_config=None, ready=None):
    exact_keys(value, WAIT_KEYS, 'prelaunch wait request')
    if value['schema'] != WAIT_SCHEMA or validate_contract(contract) != cap.CONTRACT_SHA256:
        raise ValueError('prelaunch requires the reviewed Euler-v2 contract')
    if value['contract_sha256'] != cap.CONTRACT_SHA256 or value['participants'] != participants:
        raise ValueError('prelaunch contract or participant binding differs')
    validate_participants(participants)
    hexword(value['nonce'], 32, 'prelaunch challenge')
    run_id(value['session_id']); run_id(value['client_run'])
    exact_keys(value['runner'], ('pid', 'created'), 'Windows runner identity')
    integer(value['runner']['pid'], 2, 1 << 31, 'Windows runner PID')
    integer(value['runner']['created'], 1, (1 << 64) - 1, 'Windows runner creation')
    for key in WAIT_KEYS:
        if key.endswith('_sha256'): hexword(value[key], 64, key)
    validate_prepared(value['linux_prepared'], contract)
    if object_sha(value['linux_prepared']) != value['linux_prepared_canonical_sha256']:
        raise ValueError('prelaunch preparation canonical digest differs')
    integer(value['ticks'], 2, 120, 'ticks')
    integer(value['startup_timeout_ms'], 1000, 600000, 'startup timeout')
    integer(value['timeout_ms'], 1000, min(120000, value['startup_timeout_ms']), 'runtime timeout')
    integer(value['ssh_lifetime'], value['startup_timeout_ms'] // 1000 + 1, 1800, 'SSH lifetime')
    created = integer(value['created_monotonic_ns'], 1, (1 << 64) - 1, 'Windows monotonic start')
    if value['deadline_monotonic_ns'] != created + value['startup_timeout_ms'] * 1_000_000:
        raise ValueError('prelaunch absolute deadline differs')
    if session_config is not None:
        from native_portable_ssh import validate_config
        validate_config(session_config)
        if (session_config.get('linux_first') is not True or session_config['contract'] != contract
                or session_config['participants'] != participants or session_config['ticks'] != value['ticks']
                or session_config['startup_timeout'] * 1000 != value['startup_timeout_ms']
                or session_config['peer_timeout'] * 1000 != value['timeout_ms']
                or session_config['lifetime'] != value['ssh_lifetime']
                or object_sha(session_config) != value['session_config_canonical_sha256']):
            raise ValueError('prelaunch SSH session binding differs')
    if ready is not None:
        validate_ready(ready, contract)
        if object_sha(ready) != value['coordinator_ready_canonical_sha256']:
            raise ValueError('prelaunch coordinator readiness binding differs')
    return value


def make_wait(contract, participants, session_raw, ready_raw, prepared_raw, *, session_id, client_run,
              runner, nonce, started_ns):
    session, ready, prepared = (decode(raw, MAX_RECEIPT_BYTES) for raw in (session_raw, ready_raw, prepared_raw))
    value = dict(schema=WAIT_SCHEMA, nonce=nonce, session_id=session_id, client_run=client_run, runner=runner,
        session_config_raw_sha256=sha(session_raw), session_config_canonical_sha256=object_sha(session),
        coordinator_ready_raw_sha256=sha(ready_raw), coordinator_ready_canonical_sha256=object_sha(ready),
        contract_sha256=validate_contract(contract), participants=participants, linux_prepared=prepared,
        linux_prepared_raw_sha256=sha(prepared_raw), linux_prepared_canonical_sha256=object_sha(prepared),
        created_monotonic_ns=started_ns, deadline_monotonic_ns=started_ns + session['startup_timeout'] * 1_000_000_000,
        startup_timeout_ms=session['startup_timeout'] * 1000, timeout_ms=session['peer_timeout'] * 1000,
        ticks=session['ticks'], ssh_lifetime=session['lifetime'])
    return validate_wait(value, contract, participants, session_config=session, ready=ready)


def linux_config(session, ready, remote_script, request):
    validate_wait(request, session['contract'], session['participants'], session_config=session, ready=ready)
    return dict(mode=cap.MODE, token=session['token'], participants=session['participants'],
        contract_sha256=cap.CONTRACT_SHA256, import_math_sha256=cap.DESCRIPTOR_SHA256,
        ticks=request['ticks'], startup_timeout_ms=request['startup_timeout_ms'], timeout_ms=request['timeout_ms'],
        coordinator=ready, remote_script=remote_script, prelaunch_request=request)


def validate_linux_config(config, contract):
    request = config['prelaunch_request']
    session = dict(token=config['token'], contract=contract, participants=config['participants'], ticks=config['ticks'],
        hold_tick=min(3, config['ticks'] - 1), hold_seconds=3,
        startup_timeout=config['startup_timeout_ms'] // 1000, peer_timeout=config['timeout_ms'] // 1000,
        lifetime=request['ssh_lifetime'], linux_first=True)
    expected = linux_config(session, config['coordinator'], config['remote_script'], request)
    if config != expected: raise ValueError('prelaunch Linux configuration differs')
    return request


def first_linux_report(report, contract, participants, *, ticks):
    if (not isinstance(report, dict) or report.get('contract_sha256') != validate_contract(contract)
            or report.get('participants') != participants or report.get('failure') is not None
            or report.get('verdict') != 'incomplete' or report.get('released') != -1
            or report.get('schema') != contract['schema'] or report.get('ticks') != ticks
            or report.get('first_difference') is not None or report.get('hold_tick') != min(3, ticks - 1)
            or report.get('hold_required_seconds') != 3 or report.get('hold_seconds') != 0
            or report.get('hold_observations') != [] or report.get('hold_release_observations') != {}
            or report.get('hello_slots') != [2] or report.get('terminal') != {}
            or report.get('native_processes_verified_by_coordinator') is not False
            or set(report.get('frames', {})) != {'0'} or set(report['frames']['0']) != {'2'}
            or set(report.get('peer_endpoints', {})) != {'2'}):
        raise ValueError('prelaunch requires one unpermitted Linux frame0')
    from native_portable import encode_frame
    from native_portable_coordinator import real_clock
    from validate_native_portable import validate_endpoint
    row = report['frames']['0']['2']
    exact_keys(row, ('state', 'real_bits', 'diagnostic_sha256'), 'prelaunch coordinator state')
    encoded = encode_frame(row['state'], contract)
    if (row['state']['tick'] != 0 or row['state']['calls'] != 0 or row['state']['returns'] != 0
            or row['state']['receipt_tick'] != 0 or row['state']['game_bits'] != '401c54fdf3b645a2'):
        raise ValueError('prelaunch frame clocks or original receipts differ')
    hexword(row['real_bits'], 16, 'prelaunch real clock')
    real_clock(row['real_bits'])
    hexword(row['diagnostic_sha256'], 64, 'prelaunch native diagnostic digest')
    return sha(encoded), validate_endpoint(report['peer_endpoints']['2'], 'loopback_udp')


def receipt_age(observed_ns, now_ns):
    integer(observed_ns, 1, (1 << 64) - 1, 'Linux receipt clock')
    integer(now_ns, 1, (1 << 64) - 1, 'Linux fetch clock')
    age = now_ns - observed_ns
    if not 0 <= age <= MAX_AGE_NS: raise ValueError('prelaunch receipt is stale or from a future clock')
    return age


RECEIPT_KEYS = ('schema scope native_pair_verified request request_raw_sha256 request_canonical_sha256 '
    'linux_config_raw_sha256 linux_config_canonical_sha256 review_sha256 contract_sha256 participants '
    'run case bundle claim producer native_executable_identity agent_mapping bridge_mapping '
    'python_sources coordinator_lifetime coordinator_report coordinator_report_raw_sha256 '
    'coordinator_report_canonical_sha256 frame0_sha256 native_endpoint_first native_endpoint_current '
    'native_status load_status rotation_status rotation_regions rotation_table rotation_manager '
    'clocks_before clocks_after ready_request installed markers log_cutoff observed_monotonic_ns').split()


def validate_receipt(value, config, contract, review):
    """Pure joins only; the owning probe/fetcher supply the independent observations."""
    import linux_portable_probe as probe
    exact_keys(value, RECEIPT_KEYS, 'Linux prelaunch receipt')
    request = validate_linux_config(config, contract)
    if (value['schema'] != RECEIPT_SCHEMA or value['scope'] != 'Linux joined-main startup boundary only'
            or value['native_pair_verified'] is not False or value['request'] != request
            or value['request_raw_sha256'] != object_sha(request)
            or value['request_canonical_sha256'] != object_sha(request)
            or value['linux_config_raw_sha256'] != object_sha(config)
            or value['linux_config_canonical_sha256'] != object_sha(config)
            or value['review_sha256'] != request['linux_prepared']['review_sha256']
            or value['contract_sha256'] != config['contract_sha256'] or value['participants'] != config['participants']):
        raise ValueError('Linux prelaunch session bindings differ')
    if (review.get('contract_sha256') != config['contract_sha256'] or review.get('mode') != cap.MODE
            or review.get('participant') != config['participants']['2']):
        raise ValueError('Linux prelaunch review binding differs')
    sources = {key: digest for key, digest in review['files'].items() if key.startswith('bin/') and key.endswith('.py')}
    if value['python_sources'] != sources or not sources:
        raise ValueError('Linux prelaunch producer closure differs')
    for digest in sources.values(): hexword(digest, 64, 'prelaunch source digest')
    run_id(value['run'])
    if (not value['run'].startswith('portable-native-') or not isinstance(value['case'], str)
            or not value['case'].endswith('/cases/' + value['run'])
            or not isinstance(value['bundle'], str) or not value['bundle'].startswith('/')):
        raise ValueError('Linux prelaunch owned paths differ')
    exact_keys(value['claim'], ('pid', 'start_ticks', 'uid'), 'Linux prelaunch native claim')
    claim = value['claim']; integer(claim['pid'], 2, 1 << 31, 'native PID')
    integer(claim['start_ticks'], 1, (1 << 64) - 1, 'native start')
    if claim['uid'] != 1004: raise ValueError('Linux prelaunch native UID differs')
    exact_keys(value['producer'], ('pid', 'start_ticks', 'uid', 'parent_pid', 'parent_start_ticks',
        'argv_sha256', 'source_sha256', 'executable_identity'), 'Linux prelaunch producer')
    producer = value['producer']; prepared = request['linux_prepared']
    integer(producer['pid'], 2, 1 << 31, 'probe PID'); integer(producer['start_ticks'], 1, (1 << 64) - 1, 'probe start')
    if (producer['uid'] != 0 or producer['parent_pid'] != prepared['pid']
            or producer['parent_start_ticks'] != prepared['start_ticks']
            or producer['source_sha256'] != sources.get('bin/linux_portable_probe.py')):
        raise ValueError('Linux prelaunch producer ownership differs')
    hexword(producer['argv_sha256'], 64, 'producer argv digest')
    for row in (producer['executable_identity'], value['native_executable_identity']):
        if not isinstance(row, list) or len(row) != 3: raise ValueError('prelaunch executable identity differs')
        for item in row: integer(item, 1, (1 << 64) - 1, 'executable file identity')
    if value['native_executable_identity'][2] != 60459280: raise ValueError('prelaunch native size differs')
    ready = config['coordinator']; lifetime = value['coordinator_lifetime']
    if (lifetime.get('pid') != ready['pid'] or lifetime.get('start_ticks') != ready['start_ticks']
            or lifetime.get('terminal_bound_while_alive') is not False or lifetime.get('exited_after_terminal') is not False
            or type(lifetime.get('live_observations')) is not int or lifetime['live_observations'] < 1):
        raise ValueError('prelaunch live coordinator binding differs')
    coordinator_socket = lifetime.get('endpoint', {})
    if coordinator_socket.get('address') != ['127.0.0.1', ready['port']]:
        raise ValueError('prelaunch coordinator socket differs')
    integer(coordinator_socket.get('inode'), 1, (1 << 64) - 1, 'coordinator socket inode')
    endpoint = value['native_endpoint_current']
    if endpoint != value['native_endpoint_first']:
        raise ValueError('prelaunch native socket changed since before request')
    if any(endpoint.get(key) != claim[key] for key in ('pid', 'start_ticks', 'uid')):
        raise ValueError('prelaunch native socket generation differs')
    if endpoint.get('destination') != ['127.0.0.1', ready['port']]:
        raise ValueError('prelaunch native socket destination differs')
    integer(endpoint.get('inode'), 1, (1 << 64) - 1, 'native socket inode')
    for row in (endpoint, coordinator_socket):
        fds = row.get('fds')
        if not isinstance(fds, list) or not fds or fds != sorted(set(fds)):
            raise ValueError('prelaunch socket file descriptors differ')
        for fd in fds: integer(fd, 0, 1 << 20, 'socket file descriptor')
    report = value['coordinator_report']
    frame_sha, source = first_linux_report(report, contract, config['participants'], ticks=config['ticks'])
    if (value['frame0_sha256'] != frame_sha or endpoint.get('source') != source
            or value['coordinator_report_canonical_sha256'] != object_sha(report)):
        raise ValueError('prelaunch frame or report digest differs')
    hexword(value['coordinator_report_raw_sha256'], 64, 'prelaunch raw report digest')
    clocks = value['clocks_before']
    if not isinstance(clocks, list) or len(clocks) != 3 or value['clocks_after'] != clocks:
        raise ValueError('prelaunch independently read clocks changed')
    for clock in clocks:
        hexword(clock, 16, 'prelaunch clock')
        if not math.isfinite(struct.unpack('>d',bytes.fromhex(clock))[0]): raise ValueError('prelaunch nonfinite clock')
    if clocks[:2] != [report['frames']['0']['2']['state']['game_bits'], report['frames']['0']['2']['real_bits']]:
        raise ValueError('prelaunch frame differs from independently read clocks')
    if clocks != value['native_status'].get('load_clocks', '').split(','):
        raise ValueError('prelaunch clocks differ from native loaded baseline')
    evidence = probe.RotationEvidence(probe.reviewed_capability(contract))
    row = value['rotation_status']
    if set(row) != probe.ROTATION_FIELDS: raise ValueError('prelaunch rotation schema differs')
    probe.parse_rotation('X4MP_AGENT ROTATION ' + ' '.join(f'{key}={item}' for key, item in row.items()))
    evidence.observe(row)
    table = integer(value['rotation_table'], 1, (1 << 47) - 1, 'rotation table')
    manager = integer(value['rotation_manager'], 1, (1 << 47) - 1, 'rotation manager')
    if not evidence.prelaunch(value['native_status'], value['load_status'], table, manager, value['rotation_regions']):
        raise ValueError('prelaunch native boundary or completed rotation evidence differs')
    arm = value['ready_request']
    if (any(arm.get(key) != claim[key] for key in ('pid', 'start_ticks', 'uid')) or arm.get('mode') != cap.MODE
            or not probe.observer.original_regions(arm.get('pre_arm_regions', {}))):
        raise ValueError('prelaunch owned request evidence differs')
    installed = value['installed']
    if (installed.get('status', {}).get('kind') != 'INSTALLED'
            or installed['status'].get('owner') != value['load_status'].get('owner')):
        raise ValueError('prelaunch installation acknowledgment differs')
    markers = value['markers']; loader = probe.LoaderEvidence(value['run'], contract)
    if not {'MENU', 'LOAD_REQUEST'} <= set(markers) or set(markers) - {'MENU', 'LOAD_REQUEST', 'LOADED'}:
        raise ValueError('prelaunch loader markers missing or unknown')
    for name in ('MENU', 'LOAD_REQUEST', 'LOADED'):
        if name in markers: loader.observe('[X4LAN] PORTABLE_BOOT ' + markers[name]['text'], installed['status'])
    integer(value['observed_monotonic_ns'], 1, (1 << 64) - 1, 'Linux observation clock')
    times = [markers['MENU']['observed_monotonic'], arm['published_monotonic'], installed['observed_monotonic'],
             markers['LOAD_REQUEST']['observed_monotonic'], value['observed_monotonic_ns'] / 1_000_000_000]
    if any(type(item) not in (int, float) or not 0 < item < 1e15 for item in times) or times != sorted(times):
        raise ValueError('prelaunch Linux lifecycle order differs')
    exact_keys(value['log_cutoff'], ('agent', 'world'), 'prelaunch clean log cutoff')
    for cutoff in value['log_cutoff'].values():
        if cutoff.get('complete_records') is not True: raise ValueError('prelaunch partial log evidence')
        integer(cutoff.get('bytes_consumed'), 1, 8 * 1024 * 1024, 'prelaunch consumed log bytes')
        hexword(cutoff.get('consumed_sha256'), 64, 'prelaunch consumed log digest')
    if (value['agent_mapping'].get('path') != value['bundle'] + '/bin/libx4mp_portable.so'
            or not value['agent_mapping'].get('executable_ranges')
            or value['bridge_mapping'].get('sha256') != review['files'].get('fixture/native/x4mp_load_probe.so')
            or not value['bridge_mapping'].get('executable_ranges')):
        raise ValueError('prelaunch mapped native artifacts differ')
    return value


def validate_grant(value, request, config, contract, review, now_ns):
    """Windows same-host elapsed time plus Linux same-host receipt age."""
    exact_keys(value, ('schema', 'request_sha256', 'fetch_started_monotonic_ns', 'fetch'), 'prelaunch owner grant')
    if value['schema'] != 'x4-portable-launch-grant-v1' or value['request_sha256'] != object_sha(request):
        raise ValueError('prelaunch owner grant binding differs')
    started = integer(value['fetch_started_monotonic_ns'], request['created_monotonic_ns'],
                      now_ns, 'Windows fetch start')
    if now_ns >= request['deadline_monotonic_ns']: raise ValueError('prelaunch absolute startup deadline expired')
    fetched = value['fetch']
    exact_keys(fetched, ('schema', 'ready', 'receipt', 'receipt_raw_sha256', 'receipt_canonical_sha256',
        'report_raw_hex', 'fetched_monotonic_ns', 'remote_age_ns', 'generations_verified', 'sockets_verified'),
        'prelaunch fetch envelope')
    if (fetched['schema'] != FETCH_SCHEMA or fetched['ready'] is not True
            or fetched['generations_verified'] is not True or fetched['sockets_verified'] is not True):
        raise ValueError('prelaunch live fetch evidence missing')
    receipt = validate_receipt(fetched['receipt'], config, contract, review)
    if receipt['request'] != request: raise ValueError('prelaunch foreign wait request')
    if (fetched['receipt_raw_sha256'] != object_sha(receipt)
            or fetched['receipt_canonical_sha256'] != object_sha(receipt)):
        raise ValueError('prelaunch fetched receipt digest differs')
    text = fetched['report_raw_hex']
    if not isinstance(text, str) or not 0 < len(text) <= MAX_RECEIPT_BYTES * 2 or re.fullmatch('[0-9a-f]+', text) is None:
        raise ValueError('prelaunch raw report encoding differs')
    raw = bytes.fromhex(text)
    if sha(raw) != receipt['coordinator_report_raw_sha256'] or decode(raw) != receipt['coordinator_report']:
        raise ValueError('prelaunch raw coordinator report differs')
    remote_age = receipt_age(receipt['observed_monotonic_ns'], fetched['fetched_monotonic_ns'])
    if fetched['remote_age_ns'] != remote_age or now_ns - started + remote_age > MAX_AGE_NS:
        raise ValueError('prelaunch proof expired before native launch')
    return receipt
