"""Independent read-only evidence for the owned Linux native portable participant.

Only a fresh PIDFD claim and exact artifact may publish the owned preload request.
No process launch, engine-memory write, simulation permit or synthetic peer.
"""
import argparse
from collections import deque
import hashlib
import json
import math
import os
from pathlib import Path
import re
import select
import struct
import sys
import tempfile
import time

sys.dont_write_bytecode = True
import linux_load_observer_probe as observer
from linux_native_portable_run import MODE, EULER_MODE, read_private, validate_session, verify_coordinator, review_bundle, reviewed_capability
from native_portable import SCHEMA_SHA, encode_frame, hexword, validate_contract
from validate_native_portable import coordinator_evidence, validate_endpoint

CLOCKS = 0x3db82b0
FRAME = 0x3daaaf0
SAVED_GAME = '401c54fdf3b645a2'
STATUS_FIELDS = set(('mode load_tail_included result failed first_boundary_ack tick calls returns receipt_tick '
    'subset_reads subset_changes clock_changes wait_kind max_wait_ns terminal_ack closed restored restore_error '
    'loaded_frame loaded_padding load_clocks index_ns load_selectors installed loaded_module').split())
ROTATION_REGIONS = {
    'navigation_read_import': (0x13e67f0, 16, '7d83a6f33d57a4ee5a164216c1d528751055bf645099f51338479859299d3922'),
    'navigation_write_import': (0x13e69f0, 16, '617d679204816da8bab0f415efacd2f260579ab3dfeca4945c468807818a42c3'),
}
ROTATION_MANAGER = 0x3e0ced0
ROTATION_FIELDS = set(('phase mode descriptor snapshot contract serial owner table client module manager installed '
    'load_calls load_returns load_true abnormal scope_open active read_calls read_returns read_euler read_absent '
    'read_words write_calls write_returns write_euler write_absent write_words cancelled failure restored '
    'restore_error publishable').split())
ROTATION_CLASS_COUNTERS = tuple(f'role{role}_{kind}_{field}' for role in (0,1) for kind in ('nav','player')
                               for field in ('calls','returns','euler','absent','words'))
ROTATION_FIELDS |= set(ROTATION_CLASS_COUNTERS) | {'lifetimes','paired','inputs'}
ROTATION_PHASES = ('Waiting', 'Installed', 'Loading', 'Completed', 'Failed')


class PortableProcess(observer.OwnedProcess):
    """Use the existing read-only readers with the new explicitly pinned artifact."""
    def __init__(self, claim, agent, agent_sha, mode=MODE):
        self.mem = self.pidfd = None; self.executable_identity = None; self.table = None
        self.rotation_expected = {}
        try:
            if set(claim) != {'pid', 'start_ticks', 'uid'} or any(type(v) is not int or v <= 0 for v in claim.values()):
                raise ValueError('invalid_native_claim')
            self.pid, self.start, self.uid = (claim[key] for key in ('pid', 'start_ticks', 'uid'))
            self.pidfd = os.pidfd_open(self.pid); self.poll = select.poll(); self.poll.register(self.pidfd, select.POLLIN)
            self.verify()
            uid = int(next(line for line in Path(f'/proc/{self.pid}/status').read_text().splitlines() if line.startswith('Uid:')).split()[1])
            image_path = Path(f'/proc/{self.pid}/exe'); before = image_path.stat(); image = image_path.read_bytes(); after = image_path.stat()
            self.executable_identity = before.st_dev, before.st_ino, before.st_size
            if (uid != self.uid or self.executable_identity != (after.st_dev, after.st_ino, after.st_size)
                    or len(image) != 60459280 or observer.digest(image) != observer.EXE_SHA):
                raise ValueError('native_executable_or_uid_mismatch')
            hexword(agent_sha, 64, 'reviewed agent digest'); agent = agent.resolve(strict=True); first = agent.stat()
            if observer.digest(agent.read_bytes()) != agent_sha: raise ValueError('native_agent_digest_mismatch')
            last = agent.stat(); self.agent = agent
            self.agent_identity = (first.st_dev, first.st_ino, first.st_size, first.st_mtime_ns)
            if self.agent_identity != (last.st_dev, last.st_ino, last.st_size, last.st_mtime_ns):
                raise ValueError('native_agent_changed_while_verifying')
            self.verify_agent_mapping()
            self.expected = {name: observer.elf_read(image, address, size) for name, (address, size) in observer.REGIONS.items()}
            if mode not in (MODE, EULER_MODE): raise ValueError('unreviewed_native_process_mode')
            if mode == EULER_MODE:
                for name, (address, size, expected) in ROTATION_REGIONS.items():
                    data = observer.elf_read(image, address, size)
                    if observer.digest(data) != expected: raise ValueError('rotation_original_image_pin_differs')
                    self.rotation_expected[name] = data
            self.mem = os.open(f'/proc/{self.pid}/mem', os.O_RDONLY)
            self.verify()
        except Exception:
            self.close(); raise

    def verify_agent_mapping(self):
        self.verify(); current = self.agent.stat()
        if self.agent_identity != (current.st_dev, current.st_ino, current.st_size, current.st_mtime_ns):
            raise ValueError('native_agent_file_identity_changed')
        matched = []
        for line in Path(f'/proc/{self.pid}/maps').read_text().splitlines():
            fields = line.split(maxsplit=5)
            if len(fields) != 6 or fields[5] != str(self.agent): continue
            major, minor = (int(value, 16) for value in fields[3].split(':'))
            if os.makedev(major, minor) != current.st_dev or int(fields[4]) != current.st_ino:
                raise ValueError('native_agent_mapping_file_identity_differs')
            if fields[1] == 'r-xp': matched.append(fields[0])
        if not matched: raise ValueError('exact_executable_agent_mapping_missing')
        self.verify()
        return {'path': str(self.agent), 'device': current.st_dev, 'inode': current.st_ino,
                'bytes': current.st_size, 'executable_ranges': matched}

    def clocks(self):
        return [f'{value:016x}' for value in struct.unpack('<QQQ', self.read(CLOCKS, 24))]

    def rotation_restoration(self):
        if set(self.rotation_expected) != set(ROTATION_REGIONS):
            raise ValueError('rotation_restoration_not_configured')
        rows = {}
        for name, (address, size, _) in ROTATION_REGIONS.items():
            before = self.read(address, size); after = self.read(address, size)
            rows[name] = dict(matches_original=before == after == self.rotation_expected[name],
                              sha256=observer.digest(after))
        return rows

    def rotation_manager(self):
        before = self.word(ROTATION_MANAGER); after = self.word(ROTATION_MANAGER)
        if not 0 < before < 1 << 47 or before != after:
            raise ValueError('rotation_manager_missing_or_changed')
        return before

    def bridge_mapping(self, bridge, expected):
        self.verify(); before = bridge.stat()
        if bridge.is_symlink() or observer.digest(bridge.read_bytes()) != expected:
            raise ValueError('reviewed_readiness_bridge_bytes_differ')
        matches = []
        for line in Path(f'/proc/{self.pid}/maps').read_text().splitlines():
            fields = line.split(maxsplit=5)
            if len(fields) != 6 or fields[1] != 'r-xp': continue
            major, minor = (int(value, 16) for value in fields[3].split(':'))
            if os.makedev(major, minor) == before.st_dev and int(fields[4]) == before.st_ino:
                matches.append({'path': fields[5], 'range': fields[0]})
        after = bridge.stat(); self.verify()
        if not matches or (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise ValueError('exact_readiness_bridge_mapping_missing_or_changed')
        return {'device': before.st_dev, 'inode': before.st_ino, 'bytes': before.st_size,
                'sha256': expected, 'executable_ranges': matches}

    def endpoint(self, source, destination):
        local = validate_endpoint(source, 'loopback_udp') if source is not None else None
        if type(destination) is not int or not 1024 <= destination <= 65535: raise ValueError('invalid_coordinator_destination')
        def scan():
            self.verify(); sockets = {}
            for entry in Path(f'/proc/{self.pid}/fd').iterdir():
                try: target = os.readlink(entry)
                except OSError: continue
                match = re.fullmatch(r'socket:\[([0-9]+)\]', target)
                if match: sockets.setdefault(match[1], []).append(int(entry.name))
            rows = [line.split() for line in Path(f'/proc/{self.pid}/net/udp').read_text().splitlines()[1:]]
            matches = [row for row in rows if len(row) > 9 and row[1].startswith('0100007F:')
                       and row[2] == f'0100007F:{destination:04X}' and row[9] in sockets
                       and (local is None or row[1] == f'0100007F:{local[1]:04X}')]
            self.verify()
            if len(matches) != 1: raise ValueError('native_pid_does_not_own_exact_connected_udp_socket')
            row = matches[0]
            if int(row[7]) != self.uid: raise ValueError('native_socket_uid_differs')
            actual = validate_endpoint({'transport': 'loopback_udp',
                'address': ['127.0.0.1', int(row[1].split(':')[1], 16)]}, 'loopback_udp')
            return {'pid': self.pid, 'start_ticks': self.start, 'uid': self.uid, 'inode': int(row[9]),
                    'fds': sorted(sockets[row[9]]), 'source': actual, 'destination': ['127.0.0.1', destination]}
        before = scan(); after = scan()
        if before != after: raise ValueError('native_socket_identity_changed_during_observation')
        return before


class ExclusiveAgentEvidence(observer.AgentEvidence):
    """PID-less lifecycle rows are valid only for one verified native process."""
    def bind(self, pid):
        if self.verified_pids and self.verified_pids != {pid}:
            raise ValueError('foreign_verified_native_process_in_owned_log')

    def observe(self, line, claimed_pid, claimed_start_ticks=None):
        row = super().observe(line, claimed_pid, claimed_start_ticks)
        if len(self.verified_pids) > 1:
            raise ValueError('multiple_verified_native_processes_in_owned_log')
        if claimed_pid is not None: self.bind(claimed_pid)
        return row


class CoordinatorLifetime:
    """Retain the same PIDFD/socket through terminal publication, then permit exit.

    Once a complete report was independently accepted while this process was
    alive on its original socket, return only that saved report. A replacement
    process or later report file cannot retroactively supply terminal evidence.
    """
    def __init__(self, fd, config):
        self.fd, self.ready = fd, config['coordinator']
        self.pid = self.ready['pid']; self.poll = select.poll(); self.poll.register(fd, select.POLLIN)
        self.terminal = None
        self.executable = self.file_identity(Path(f'/proc/{self.pid}/exe'))
        self.command = Path(f'/proc/{self.pid}/cmdline').read_bytes()
        if (self.executable != self.file_identity(Path(sys.executable))
                or self.command.split(b'\0')[1:] != [str(Path(config['remote_script']).resolve(strict=True)).encode(), b'--remote', b'']):
            raise ValueError('coordinator_initial_executable_or_command_differs')
        self.socket = self.scan()
        self.evidence = {'pid': self.pid, 'start_ticks': self.ready['start_ticks'],
            'endpoint': self.socket, 'live_observations': 0, 'terminal_bound_while_alive': False,
            'exited_after_terminal': False}
        self.verify()

    @staticmethod
    def file_identity(path):
        info = path.stat(); return info.st_dev, info.st_ino, info.st_size

    def identity(self):
        if self.poll.poll(0) or observer.process_start(self.pid) != self.ready['start_ticks']:
            raise ValueError('coordinator_exited_before_terminal_binding')
        uid = int(next(line for line in Path(f'/proc/{self.pid}/status').read_text().splitlines() if line.startswith('Uid:')).split()[1])
        if (uid != 0 or self.file_identity(Path(f'/proc/{self.pid}/exe')) != self.executable
                or Path(f'/proc/{self.pid}/cmdline').read_bytes() != self.command):
            raise ValueError('coordinator_process_identity_changed')

    def scan(self):
        self.identity(); sockets = {}
        for entry in Path(f'/proc/{self.pid}/fd').iterdir():
            try: target = os.readlink(entry)
            except OSError: continue
            match = re.fullmatch(r'socket:\[([0-9]+)\]', target)
            if match: sockets.setdefault(match[1], []).append(int(entry.name))
        address = f'0100007F:{self.ready["port"]:04X}'
        rows = [line.split() for line in Path(f'/proc/{self.pid}/net/udp').read_text().splitlines()[1:]]
        matches = [row for row in rows if len(row) > 9 and row[1] == address and row[2] == '00000000:0000'
                   and row[9] in sockets and row[7] == '0']
        self.identity()
        if len(matches) != 1: raise ValueError('coordinator_original_socket_missing')
        row = matches[0]
        return {'address': ['127.0.0.1', self.ready['port']], 'inode': int(row[9]), 'fds': sorted(sockets[row[9]])}

    def verify(self):
        if self.poll.poll(0):
            if self.terminal is None: raise ValueError('coordinator_exited_before_terminal_binding')
            self.evidence['exited_after_terminal'] = True
            return
        if self.scan() != self.socket or self.scan() != self.socket:
            raise ValueError('coordinator_socket_identity_changed')
        self.evidence['live_observations'] += 1

    def report(self, read, validate):
        self.verify()
        if self.terminal is not None: return self.terminal
        value = read(); self.verify()
        if value and value.get('verdict') == 'pass':
            validate(value)
            self.verify()
            self.terminal = json.loads(json.dumps(value))
            self.evidence.update(terminal_bound_while_alive=True, terminal_observed_monotonic=time.monotonic(),
                terminal_report_sha256=hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest())
        return value


def publish_request(path, process, token, mode=MODE):
    if mode not in (MODE, EULER_MODE): raise ValueError('unreviewed_request_mode')
    process.verify(); hexword(token, 32, 'private token')
    if not path.is_absolute() or path.parent.resolve() != path.parent: raise ValueError('request_path_not_canonical')
    parent = path.parent.stat()
    if parent.st_uid != process.uid or parent.st_mode & 0o077: raise ValueError('request_parent_not_private_owned')
    baseline = process.restoration(initial=True)
    if not observer.original_regions(baseline): raise ValueError('preload_regions_not_original')
    rotation = process.rotation_restoration() if mode == EULER_MODE else None
    if rotation is not None and not all(row['matches_original'] for row in rotation.values()):
        raise ValueError('rotation_preload_regions_not_original')
    frame = process.word(FRAME)
    padding = process.read(frame + 0x201, 1)[0] if frame else None
    fd, temporary = tempfile.mkstemp(prefix='.portable-ready.', dir=path.parent)
    try:
        os.fchown(fd, process.uid, -1); os.fchmod(fd, 0o400)
        with os.fdopen(fd, 'wb') as stream:
            fd = None; stream.write(f'{mode}|{token}|{process.pid}|{process.start}\n'.encode('ascii'))
            stream.flush(); os.fsync(stream.fileno())
        process.verify(); os.link(temporary, path, follow_symlinks=False)
    finally:
        if fd is not None: os.close(fd)
        os.unlink(temporary)
    result = {'mode': mode, 'pid': process.pid, 'start_ticks': process.start, 'uid': process.uid,
            'published_monotonic': time.monotonic(), 'pre_arm_regions': baseline,
            'preload_frame': f'{frame:x}', 'preload_padding': padding}
    if rotation is not None: result['pre_arm_rotation_regions'] = rotation
    return result


def parse_portable(line, mode=MODE):
    marker = 'X4MP_AGENT PORTABLE STATE '
    if marker not in line: return None
    text = line.split(marker, 1)[1].strip()
    if len(text) > 2048: raise ValueError('oversized_native_portable_status')
    result = {}
    for field in text.split():
        if '=' not in field: raise ValueError('malformed_native_portable_status')
        key, value = field.split('=', 1)
        if key in result: raise ValueError('duplicate_native_portable_status_field')
        result[key] = value
    if mode not in (MODE, EULER_MODE) or set(result) != STATUS_FIELDS or result['mode'] != mode:
        raise ValueError('native_portable_status_schema_differs')
    return result


def terminal_status(row, ticks, mode=MODE):
    if mode not in (MODE, EULER_MODE): return False
    expected = {'mode': mode, 'load_tail_included': '1',
        'result': 'passed_subset_euler_import' if mode == EULER_MODE else 'passed_subset_zero_only', 'failed': '0',
        'first_boundary_ack': '1', 'tick': str(ticks), 'calls': str(ticks + 1), 'returns': str(ticks + 1),
        'receipt_tick': str(ticks), 'subset_changes': '0', 'clock_changes': '0', 'terminal_ack': '1',
        'closed': '1', 'restored': '1', 'restore_error': 'none', 'installed': '1'}
    if any(row.get(key) != value for key, value in expected.items()): return False
    if (not 0 < int(row['loaded_frame'], 16) < 1 << 47 or not 0 < int(row['loaded_module'], 16) < 1 << 47
            or not 0 <= int(row['loaded_padding']) <= 255 or int(row['subset_reads']) < ticks + 1
            or not 0 <= int(row['index_ns']) <= 5_000_000_000 or not 0 <= int(row['wait_kind']) <= 2
            or not 0 <= int(row['max_wait_ns']) <= 600_000_000_000):
        return False
    clocks = row['load_clocks'].split(','); selectors = row['load_selectors'].split(',')
    if len(clocks) != 3 or selectors not in (['0', '1'], ['1', '0']) or clocks[0] != SAVED_GAME: return False
    for value in clocks:
        hexword(value, 16, 'native loaded clock')
        if not math.isfinite(struct.unpack('>d', bytes.fromhex(value))[0]): return False
    return True


def prelaunch_status(row):
    """Joined-main wait evidence only; never a portable terminal predicate."""
    if not isinstance(row, dict) or set(row) != STATUS_FIELDS: return False
    expected = dict(mode=EULER_MODE, load_tail_included='1', result='main_ready', failed='0',
        first_boundary_ack='1', tick='0', calls='0', returns='0', receipt_tick='0', subset_changes='0',
        clock_changes='0', wait_kind='1', terminal_ack='0', closed='0', restored='0', restore_error='none', installed='1')
    if any(row.get(key) != value for key, value in expected.items()): return False
    try:
        if (not 0 < int(row['loaded_frame'], 16) < 1 << 47 or not 0 < int(row['loaded_module'], 16) < 1 << 47
                or not 0 <= int(row['loaded_padding']) <= 255 or not 1 <= int(row['subset_reads']) <= 1_000_000_000
                or not 0 <= int(row['index_ns']) <= 5_000_000_000
                or not 0 <= int(row['max_wait_ns']) <= 600_000_000_000): return False
        clocks = row['load_clocks'].split(','); selectors = row['load_selectors'].split(',')
        if len(clocks) != 3 or selectors not in (['0', '1'], ['1', '0']) or clocks[0] != SAVED_GAME: return False
        for value in clocks:
            hexword(value, 16, 'prelaunch loaded clock')
            if not math.isfinite(struct.unpack('>d', bytes.fromhex(value))[0]): return False
    except (ValueError, TypeError, KeyError): return False
    return True


def parse_rotation(line):
    record = observer.agent_record(line)
    if record is None or not record.startswith('ROTATION'): return None
    if not record.startswith('ROTATION '): raise ValueError('malformed_rotation_status')
    text = record[len('ROTATION '):]
    if len(text) > 4096: raise ValueError('oversized_rotation_status')
    row = {}
    for token in text.split():
        if '=' not in token: raise ValueError('malformed_rotation_status')
        key, value = token.split('=', 1)
        if key in row: raise ValueError('duplicate_rotation_status_field')
        row[key] = value
    if set(row) != ROTATION_FIELDS or row['phase'] not in ROTATION_PHASES:
        raise ValueError('rotation_status_schema_differs')
    for key in ('descriptor', 'snapshot', 'contract','inputs'): hexword(row[key], 64, 'rotation ' + key)
    for key in ('table', 'client', 'module', 'manager'):
        if not re.fullmatch('[0-9a-f]{1,16}', row[key]) or int(row[key], 16) >= 1 << 47:
            raise ValueError('rotation_pointer_not_bounded')
    for key in ROTATION_FIELDS - {'phase', 'mode', 'descriptor', 'snapshot', 'contract','inputs', 'table', 'client',
                                  'module', 'manager', 'failure', 'restore_error'}:
        if not re.fullmatch('0|[1-9][0-9]{0,19}', row[key]) or int(row[key]) >= 1 << 64:
            raise ValueError('rotation_counter_not_canonical')
    for key in ('installed', 'load_true', 'abnormal', 'scope_open', 'cancelled', 'restored', 'publishable'):
        if row[key] not in ('0', '1'): raise ValueError('rotation_boolean_not_canonical')
    for key in ('failure', 'restore_error'):
        if not re.fullmatch('[a-z][a-z0-9_]{0,95}', row[key]): raise ValueError('rotation_reason_not_canonical')
    return row


class RotationEvidence:
    """A separate native receipt; portable frames cannot supply missing math proof."""
    def __init__(self, capability):
        self.capability, self.latest, self.anchors = capability, None, {}
        self.phase = -1
        self.role_expected = {}
        self.class_expected = {}
        self.lifetimes_expected = None
        self.inputs_expected = None
        if capability['mode'] == EULER_MODE:
            import portable_import_capability as cap
            self.role_expected = dict(calls=str(cap.AGENT_COUNT), returns=str(cap.AGENT_COUNT),
                euler=str(cap.EULER_PER_BANK), absent=str(cap.ABSENT_PER_BANK), words=str(9 * cap.EULER_PER_BANK))
            configured=cap.capability(capability['snapshot_sha256'])
            for role,label in enumerate(('read','write')):
                for kind,key in (('nav','navigation'),('player','player')):
                    count=configured['banks'][label][key]
                    euler,absent=count['euler'],count['absent']
                    self.class_expected.update({f'role{role}_{kind}_{field}':str(value) for field,value in
                        dict(calls=euler+absent,returns=euler+absent,euler=euler,absent=absent,words=9*euler).items()})
            self.lifetimes_expected=str(sum(configured['movement_records'].values()))
            self.inputs_expected=configured['inputs_sha256']

    def observe(self, row):
        if self.capability['mode'] != EULER_MODE:
            raise ValueError('rotation_receipt_in_legacy_mode')
        expected = dict(mode=EULER_MODE, descriptor=self.capability['import_math_sha256'],
                        snapshot=self.capability['snapshot_sha256'], contract=self.capability['contract_sha256'],inputs=self.inputs_expected)
        if any(row[key] != value for key, value in expected.items()):
            raise ValueError('rotation_receipt_capability_differs')
        if (row['phase'] == 'Failed' or row['failure'] != 'none' or row['restore_error'] != 'none'
                or row['abnormal'] != '0' or row['cancelled'] != '0'):
            raise ValueError('native_rotation_fault')
        phase = ROTATION_PHASES.index(row['phase'])
        if phase < self.phase: raise ValueError('rotation_phase_regressed')
        monotone = ('load_calls', 'load_returns', 'load_true', 'installed', 'restored', 'publishable') + tuple(
            role + '_' + field for role in ('read', 'write') for field in ('calls', 'returns', 'euler', 'absent', 'words')) + ROTATION_CLASS_COUNTERS + ('lifetimes','paired')
        if self.latest and any(int(row[key]) < int(self.latest[key]) for key in monotone):
            raise ValueError('rotation_counter_regressed')
        for key in ('serial', 'owner', 'table', 'client', 'module', 'manager'):
            value = int(row[key], 16 if key in ('table', 'client', 'module', 'manager') else 10)
            if key in self.anchors and value != self.anchors[key]:
                raise ValueError('rotation_ownership_changed:' + key)
            if value: self.anchors[key] = value
        self.phase, self.latest = phase, dict(row)

    def drained(self, row, regions):
        """Restoration after refusal is diagnostic only, never recovered success."""
        if not isinstance(row, dict) or set(regions) != set(ROTATION_REGIONS): return False
        expected = dict(mode=EULER_MODE, descriptor=self.capability.get('import_math_sha256'),
            snapshot=self.capability['snapshot_sha256'], contract=self.capability['contract_sha256'],
            inputs=self.inputs_expected,scope_open='0', active='0', restored='1', restore_error='none')
        return (all(row.get(key) == value for key, value in expected.items())
            and all(regions[name].get('matches_original') is True and regions[name].get('sha256') == pin
                    for name, (_, _, pin) in ROTATION_REGIONS.items()))

    def _completed_import(self, native, load, table, manager, load_predicate):
        row = self.latest
        if (row is None or not isinstance(native, dict) or not isinstance(load, dict)
                or native.get('mode') != EULER_MODE):
            return False
        expected = dict(phase='Completed', serial='1', installed='1', load_calls='1', load_returns='1', load_true='1',
                        abnormal='0', scope_open='0', active='0', cancelled='0', failure='none',
                        restored='1', restore_error='none', publishable='1')
        for role in ('read', 'write'):
            expected.update({role+'_'+key:value for key,value in self.role_expected.items()})
        expected.update(self.class_expected)
        expected.update(lifetimes=self.lifetimes_expected,paired=self.lifetimes_expected,inputs=self.inputs_expected)
        if any(row[key] != value for key, value in expected.items()): return False
        if len(self.anchors) != 6 or not load_predicate(load): return False
        # Load's install boundary retains the same VBI table/GameClient; its
        # earlier module is StartMenu, so join the loaded module to portable.
        boundary = load.get('install_last', '').split(',')
        if len(boundary) != 12: return False
        try:
            return (int(row['owner']) == int(load['owner']) == int(boundary[0])
                and int(row['table'], 16) == int(boundary[1], 16) == table
                and int(row['client'], 16) == int(boundary[2], 16)
                and int(row['module'], 16) == int(native['loaded_module'], 16)
                and int(row['manager'], 16) == manager)
        except (ValueError, KeyError): return False

    def terminal(self, native, load, table, manager):
        return (isinstance(native, dict) and native.get('result') == 'passed_subset_euler_import'
            and self._completed_import(native, load, table, manager, observer.complete_status))

    def prelaunch(self, native, load, table, manager, regions):
        # Load/scheduler hooks remain owned during frame0 wait. Only the completed
        # rotation hooks must already be independently restored at this phase.
        return (prelaunch_status(native) and isinstance(regions, dict) and set(regions) == set(ROTATION_REGIONS)
            and all(regions[name].get('matches_original') is True and regions[name].get('sha256') == pin
                    for name, (_, _, pin) in ROTATION_REGIONS.items())
            and self._completed_import(native, load, table, manager, observer.provisional_status))


class LoaderEvidence:
    def __init__(self, run, contract): self.run, self.contract, self.markers = run, contract, {}

    def observe(self, line, installed):
        if ('Error while executing' in line and re.search(r'native_portable_(?:linux_live|loader)\.lua', line)):
            raise ValueError('portable_loader_module_error')
        marker = '[X4LAN] PORTABLE_BOOT '
        if marker not in line: return
        text = line.split(marker, 1)[1].strip()
        if not text.endswith(' run=' + self.run): raise ValueError('world_run_mismatch')
        kind = text.split(' ', 1)[0]
        if kind in ('ERROR', 'CANCEL'): raise ValueError('portable_loader_error_or_cancel')
        if kind == 'MENU':
            expected = f'MENU schema={SCHEMA_SHA} snapshot={self.contract["snapshot_sha256"]} run={self.run}'
            if text != expected or kind in self.markers: raise ValueError('invalid_or_repeated_menu')
        elif kind == 'LOAD_REQUEST':
            expected = f'LOAD_REQUEST snapshot={self.contract["snapshot_sha256"]} contract={validate_contract(self.contract)} run={self.run}'
            if text != expected or installed is None or kind in self.markers: raise ValueError('load_without_installed_ack_or_duplicate')
        elif kind == 'LOADED':
            matched = re.fullmatch(rf'LOADED game=([^ ]+) paused=(true|false) snapshot={self.contract["snapshot_sha256"]} run={self.run}', text)
            if not matched or not math.isfinite(float(matched[1])) or 'LOAD_REQUEST' not in self.markers:
                raise ValueError('loaded_without_exact_owned_request')
            # Supplemental only: never a condition for frame0 or permits.
        else: raise ValueError('unknown_loader_marker')
        self.markers.setdefault(kind, {'text': text, 'observed_monotonic': time.monotonic()})


def report_snapshot(path):
    try:
        with path.open('rb') as stream:
            if os.fstat(stream.fileno()).st_size > 16 * 1024 * 1024: raise ValueError('oversized_coordinator_report')
            return json.load(stream)
    except FileNotFoundError: return None


def held_clock_samples(samples, hold):
    pair = [hold['state']['game_bits'], hold['real_bits']]
    matching = [sample for sample in samples if sample['clocks'][:2] == pair]
    if (len(matching) < 10 or matching[-1]['observed_monotonic'] - matching[0]['observed_monotonic'] < 2
            or any(sample['clocks'] != matching[0]['clocks'] for sample in matching)):
        raise ValueError('independent_three_clock_hold_not_proven')
    return matching


def request_disarm(path, process, mode=MODE):
    if mode not in (MODE, EULER_MODE): raise ValueError('unreviewed_disarm_mode')
    process.verify()
    if path.exists(): return
    if path.parent.resolve() != path.parent or path.parent.stat().st_uid != process.uid or path.parent.stat().st_mode & 0o077:
        raise ValueError('disarm_parent_not_private_owned')
    fd, temporary = tempfile.mkstemp(prefix='.portable-disarm.', dir=path.parent)
    try:
        os.fchown(fd, process.uid, -1); os.fchmod(fd, 0o400)
        with os.fdopen(fd, 'wb') as stream:
            fd = None; stream.write(f'{mode}|{process.pid}|{process.start}\n'.encode('ascii'))
            stream.flush(); os.fsync(stream.fileno())
        process.verify(); os.link(temporary, path, follow_symlinks=False)
    finally:
        if fd is not None: os.close(fd)
        os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('claim', 'agent-so', 'agent-log', 'world-log', 'ready-file', 'report', 'contract', 'config', 'disarm-file'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--agent-sha256', required=True); parser.add_argument('--run-id', required=True)
    parser.add_argument('--review-sha256', required=True)
    args = parser.parse_args()
    if not re.fullmatch('portable-native-[0-9a-f]{8}', args.run_id): raise ValueError('invalid_owned_run')
    if args.report.exists() or args.ready_file.exists() or args.disarm_file.exists(): raise ValueError('fresh_owned_paths_required')
    contract = json.loads(args.contract.read_text()); contract_sha = validate_contract(contract)
    config, private_sha = read_private(args.config)
    participant = {'platform': 'linux', 'executable_sha256': observer.EXE_SHA, 'agent_sha256': args.agent_sha256}
    validate_session(config, contract, participant)
    capability = reviewed_capability(contract); mode = capability['mode']
    bundle = args.agent_so.resolve(strict=True).parent.parent
    review, reviewed_contract = review_bundle(bundle, args.review_sha256)
    if review['participant'] != participant or reviewed_contract != contract: raise ValueError('probe_review_binding_differs')
    agent = observer.FreshLog(args.agent_log); world = observer.FreshLog(args.world_log)
    loader = LoaderEvidence(args.run_id, contract); provenance = ExclusiveAgentEvidence()
    provenance.allow_clean_disarm('portable_ended')
    result = {**capability, 'verdict': 'fail', 'native_pair_verified': False,
        'contract_sha256': contract_sha, 'agent_sha256': args.agent_sha256, 'private_config_sha256': private_sha,
        'scope': contract['coverage'], 'unsupported': contract['unsupported'], 'markers': loader.markers,
        'attributed_build_refusals': provenance.attributed_build_refusals}
    if mode == EULER_MODE: result.update(rotation_import_verified=False, rotation_receipt_seen=False)
    process = None; request = installed = native = load = report = None
    rotation = RotationEvidence(capability)
    first_endpoint = last_endpoint = bridge_identity = None
    next_endpoint = next_clock = 0.0; clock_samples = deque(maxlen=2048)
    bridge_path = args.claim.parent / 'extensions/x4_lan_lab/native/x4mp_load_probe.so'
    bridge_sha = review['files']['fixture/native/x4mp_load_probe.so']
    deadline = time.monotonic() + (config['startup_timeout_ms'] + config['timeout_ms']) / 1000 + 20
    coordinatorfd = None
    def validate_report(value):
        checked = coordinator_evidence(value, contract, config['participants'], ticks=config['ticks'],
            hold_tick=min(3, config['ticks'] - 1), hold_seconds=3)
        if not checked['valid']: raise ValueError('independent_coordinator_validation:' + ';'.join(checked['errors']))
        return checked
    print('PORTABLE_NATIVE_PROBE READY', flush=True)
    try:
        coordinatorfd = verify_coordinator(config, bundle, args.claim.resolve().parents[2])
        coordinator = CoordinatorLifetime(coordinatorfd, config)
        result['coordinator_lifetime'] = coordinator.evidence
        while time.monotonic() < deadline:
            now = time.monotonic()
            if process is None and args.claim.exists():
                process = PortableProcess(json.loads(args.claim.read_text()), args.agent_so, args.agent_sha256, mode)
                provenance.bind(process.pid)
                result['claim'] = {'pid': process.pid, 'start_ticks': process.start, 'uid': process.uid}
                result['agent_mapping'] = process.verify_agent_mapping()
            if process:
                process.verify()
                provenance.bind_identity(process.pid, process.start)
            for line in agent.lines():
                row = provenance.observe(line, process.pid if process else None, process.start if process else None)
                if row:
                    load = row; result['last_load_status'] = row
                    if row.get('failure') not in (None, 'none') or row.get('restore_error') not in (None, 'none'):
                        raise ValueError('native_load_observer_fault')
                    if row['kind'] == 'INSTALLED' and installed is None:
                        if request is None: raise ValueError('installed_before_owned_request')
                        installed = row; result['installed'] = {'status': row, 'observed_monotonic': now}
                if 'X4MP_AGENT PORTABLE REFUSED' in line: raise ValueError('native_portable_refused')
                math_row = parse_rotation(line)
                if math_row:
                    result['last_rotation_status'] = math_row
                    result['rotation_receipt_seen'] = True
                    if not process or not provenance.verified(process.pid) or request is None:
                        raise ValueError('rotation_status_before_owned_request')
                    rotation.observe(math_row)
                row = parse_portable(line, mode)
                if row:
                    if not process or not provenance.verified(process.pid): raise ValueError('portable_status_before_owned_verification')
                    native = row; result['last_native_status'] = row
                    if row['failed'] != '0' or row['restore_error'] != 'none': raise ValueError('native_portable_fault:' + row['result'])
                    if row['first_boundary_ack'] == '1':
                        result.setdefault('first_boundary_ack', {'status': row, 'observed_monotonic': now,
                            'origin': 'native latched first joined-main frame0; observation may be later'})
            for line in world.lines(): loader.observe(line, installed)
            if process and provenance.verified(process.pid) and 'MENU' in loader.markers and request is None:
                bridge_identity = process.bridge_mapping(bridge_path, bridge_sha)
                result['readiness_bridge_mapping'] = bridge_identity
                coordinator.verify()
                first_endpoint = process.endpoint(None, config['coordinator']['port'])
                result['native_endpoint_first'] = first_endpoint
                result['native_endpoint_pinned_before_request'] = True
                request = publish_request(args.ready_file, process, config['token'], mode); result['ready_request'] = request
                result['owned_native_verified'] = True
                result['unattributed_preverification_build_refusals'] = provenance.preverification_build_refusals
                result['attributed_build_refusals'] = provenance.attributed_build_refusals
            report = coordinator.report(lambda: report_snapshot(Path(config['coordinator']['report_path'])), validate_report)
            if report:
                if (report.get('contract_sha256') != contract_sha or report.get('participants') != config['participants']
                        or report.get('ticks') != config['ticks']): raise ValueError('coordinator_report_binding_differs')
                if report.get('failure'):
                    result['coordinator_failure'] = report.get('failure'); result['first_difference'] = report.get('first_difference')
                    raise ValueError('coordinator_refused:' + report['failure'])
                endpoints = report.get('peer_endpoints', {})
                if process and '2' in endpoints and now >= next_endpoint:
                    next_endpoint = now + .25
                    current = process.endpoint(endpoints['2'], config['coordinator']['port'])
                    if first_endpoint is None or current != first_endpoint: raise ValueError('native_connected_socket_changed')
                    last_endpoint = current; result['native_endpoint_last'] = current
            if process and native and native['first_boundary_ack'] == '1' and now >= next_clock:
                next_clock = now + .1
                clocks = process.clocks()
                if clocks == process.clocks():
                    # The durable hold frame is published only at release.
                    # Retain bounded independent reads now, then match its exact
                    # game/real tuple after the complete report is available.
                    clock_samples.append({'observed_monotonic': now, 'clocks': clocks})
            if ('prelaunch_request' in config and process and report and not result.get('prelaunch_receipt')
                    and report.get('hello_slots') == [2] and report.get('frames')):
                from linux_portable_prelaunch import capture
                published = capture(args=args, config=config, contract=contract, review=review, process=process,
                    coordinator=coordinator, provenance=provenance, request=request, installed=installed,
                    native=native, load=load, rotation=rotation, loader=loader, first_endpoint=first_endpoint,
                    bridge_identity=bridge_identity, agent_log=agent, world_log=world)
                if published:
                    result['prelaunch_receipt'] = published
                    print('PORTABLE_NATIVE_PRELAUNCH published=' + published['raw_sha256'], flush=True)
            if native and load and terminal_status(native, config['ticks'], mode) and observer.complete_status(load):
                if mode == EULER_MODE:
                    manager = process.rotation_manager()
                    if not rotation.terminal(native, load, process.table, manager):
                        time.sleep(.02); continue
                if not report or report.get('verdict') != 'pass':
                    time.sleep(.02); continue
                checked = validate_report(report)
                if (request is None or installed is None or 'LOAD_REQUEST' not in loader.markers
                        or first_endpoint is None or last_endpoint != first_endpoint):
                    raise ValueError('native_startup_or_endpoint_proof_missing')
                zero = report['frames']['0']['2']['state']; encode_frame(zero, contract)
                if zero['game_bits'] != SAVED_GAME or zero['tick'] != 0: raise ValueError('native_frame0_saved_clock_differs')
                hold = report['frames'][str(min(3, config['ticks'] - 1))]['2']
                hold_samples = held_clock_samples(clock_samples, hold)
                result['independent_hold_samples'] = hold_samples
                restored = process.restoration()
                frame = process.word(FRAME); padding = process.read(frame + 0x201, 1)[0]
                restored['loaded_padding'] = {'origin': 'native-owned loaded pre-arm baseline; final byte read independently',
                    'frame': f'{frame:x}', 'expected': int(native['loaded_padding']), 'observed': padding,
                    'matches_native_loaded_baseline': frame == int(native['loaded_frame'], 16) and padding == int(native['loaded_padding'])}
                if not observer.original_regions(restored) or not restored['loaded_padding']['matches_native_loaded_baseline']:
                    raise ValueError('native_code_slot_or_padding_not_restored')
                if mode == EULER_MODE:
                    math_restored = process.rotation_restoration()
                    math_table = process.word(observer.VBI_GLOBAL)
                    math_manager = process.rotation_manager()
                    if (not all(row['matches_original'] for row in math_restored.values())
                            or not rotation.terminal(native, load, math_table, math_manager)):
                        raise ValueError('native_rotation_restoration_or_ownership_differs')
                    result.update(rotation_import_verified=True, final_rotation_status=rotation.latest,
                                  rotation_restoration=math_restored, rotation_table=f'{math_table:x}',
                                  rotation_manager=f'{math_manager:x}')
                imports = Path(str(args.agent_log) + '.imports')
                if imports.stat().st_size > 16 + 1048576 * 16: raise ValueError('oversized_native_import_output')
                fixtures, import_result = observer.import_map(imports.read_bytes(), load, contract)
                identities = observer.fixture_identity(process, fixtures)
                final_endpoint = process.endpoint(report['peer_endpoints']['2'], config['coordinator']['port'])
                if final_endpoint != first_endpoint: raise ValueError('native_final_socket_identity_differs')
                final_bridge = process.bridge_mapping(bridge_path, bridge_sha)
                if final_bridge != bridge_identity: raise ValueError('native_readiness_bridge_identity_changed')
                coordinator.verify()
                if not coordinator.evidence['terminal_bound_while_alive']: raise ValueError('terminal_coordinator_binding_missing')
                final_mapping = process.verify_agent_mapping()
                observer.accept_terminal_logs(result, agent, world, native_frame0_verified=True, native_terminal_verified=True,
                    restoration_verified=True, final_native_status=native, final_load_status=load,
                    restoration=restored, fixture_identities=identities, import_output=import_result,
                    native_endpoint_last=final_endpoint, coordinator_validation=checked,
                    final_agent_mapping=final_mapping, final_bridge_mapping=final_bridge)
                break
            time.sleep(.02)
        else:
            if mode == EULER_MODE and native and terminal_status(native, config['ticks'], mode) and not result['rotation_import_verified']:
                raise ValueError('native_rotation_evidence_incomplete' if rotation.latest else 'native_rotation_receipt_missing')
            raise ValueError('bounded_native_portable_probe_timeout')
    except Exception as error:
        result.update(verdict='fail', error=str(error))
        result['last_independent_clock_samples'] = list(clock_samples)[-64:]
        if process:
            try:
                request_disarm(args.disarm_file, process, mode); result['failure_disarm_requested'] = True
                until = time.monotonic() + 10
                while time.monotonic() < until:
                    for line in agent.lines():
                        row = parse_portable(line, mode)
                        if row: result['failure_drain_native_status'] = row
                        row = parse_rotation(line)
                        if row: result['failure_drain_rotation_status'] = row
                        row = observer.parse_status(line)
                        if row: result['failure_drain_load_status'] = row
                    restored = process.restoration(); result['failure_restoration_diagnostic'] = restored
                    if mode == EULER_MODE:
                        math_regions = process.rotation_restoration()
                        result['failure_rotation_restoration_diagnostic'] = math_regions
                        result['failure_rotation_drain_complete'] = rotation.drained(
                            result.get('failure_drain_rotation_status', rotation.latest), math_regions)
                    row = result.get('failure_drain_native_status', native or {})
                    if (observer.original_regions(restored) and row.get('terminal_ack') == '1'
                            and (mode != EULER_MODE or result['failure_rotation_drain_complete'])): break
                    time.sleep(.05)
            except Exception as diagnostic:
                result['failure_drain_error'] = str(diagnostic)
    finally:
        if process: process.close()
        if coordinatorfd is not None: os.close(coordinatorfd)
        temporary = args.report.with_suffix('.tmp'); temporary.write_text(json.dumps(result, indent=2) + '\n'); temporary.replace(args.report)
    print('PORTABLE_NATIVE_PROBE FINISHED verdict=' + result['verdict'], flush=True)
    raise SystemExit(0 if result['verdict'] == 'pass' else 1)


if __name__ == '__main__': main()
