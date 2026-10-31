"""Independent read-only evidence for one owned Linux snapshot load.

No game launch, process-memory writes, simulation permits or multiplayer claim.
The owning launcher supplies a fresh PID/start/UID claim and private directory.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import select
import struct
import tempfile
import time

from native_portable import SCHEMA_SHA, validate_contract

EXE_SHA = '113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20'
AGENT_SHA = 'dba2adf1025e0247c0bdffd8802b5d0a3ab85f3c237b778a30b860f14494aebf'
MODE = 'snapshot-import-observer-v1'
REGIONS = {'load': (0xf9a2e0, 16), 'clear': (0x1cddf50, 16), 'success': (0xf9a340, 16),
           'main_gate': (0x72fb10, 16), 'render': (0x2025208, 8),
           'dt_load': (0x2025260, 8), 'dt_min': (0x2025268, 8)}
VBI_GLOBAL = 0x3dbc4d8
REGISTRY_GLOBAL = 0x3daa860


def digest(data): return hashlib.sha256(data).hexdigest()


def elf_read(data, address, length):
    if data[:6] != b'\x7fELF\x02\x01': raise ValueError('not_elf64_le')
    offset = struct.unpack_from('<Q', data, 32)[0]
    width, count = struct.unpack_from('<HH', data, 54)
    for index in range(count):
        kind, _, file_offset, va, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, offset + index * width)
        if kind == 1 and va <= address and address + length <= va + size:
            start = file_offset + address - va
            return data[start:start + length]
    raise ValueError('unmapped_elf_region')


def process_start(pid): return int(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[19])


class OwnedProcess:
    def __init__(self, claim, agent):
        self.mem = self.pidfd = None
        self.executable_identity = None
        try:
            self.pid, self.start, self.uid = (int(claim[k]) for k in ('pid', 'start_ticks', 'uid'))
            if self.pid <= 1 or self.uid <= 0 or process_start(self.pid) != self.start:
                raise ValueError('invalid_native_claim')
            self.pidfd = os.pidfd_open(self.pid)
            self.poll = select.poll(); self.poll.register(self.pidfd, select.POLLIN)
            self.verify()
            status = Path(f'/proc/{self.pid}/status').read_text().splitlines()
            uid = int(next(line for line in status if line.startswith('Uid:')).split()[1])
            executable = Path(f'/proc/{self.pid}/exe'); before = executable.stat()
            image = executable.read_bytes(); after = executable.stat()
            self.executable_identity = before.st_dev, before.st_ino, before.st_size
            if self.executable_identity != (after.st_dev, after.st_ino, after.st_size):
                raise ValueError('native_executable_changed_while_verifying')
            if uid != self.uid or len(image) != 60459280 or digest(image) != EXE_SHA:
                raise ValueError('native_identity_mismatch')
            agent = agent.resolve(strict=True)
            if digest(agent.read_bytes()) != AGENT_SHA: raise ValueError('agent_build_mismatch')
            maps = Path(f'/proc/{self.pid}/maps').read_text()
            if not any(line.split(maxsplit=5)[-1] == str(agent) for line in maps.splitlines()):
                raise ValueError('exact_agent_not_mapped')
            self.expected = {k: elf_read(image, address, size) for k, (address, size) in REGIONS.items()}
            self.mem = os.open(f'/proc/{self.pid}/mem', os.O_RDONLY)
            self.table = None
            self.verify()
        except Exception:
            self.close(); raise

    def verify(self):
        if self.poll.poll(0) or process_start(self.pid) != self.start: raise ValueError('owned_process_exited')
        if self.executable_identity is not None:
            stat = Path(f'/proc/{self.pid}/exe').stat()
            if (stat.st_dev, stat.st_ino, stat.st_size) != self.executable_identity:
                raise ValueError('owned_process_executable_changed')

    def read(self, address, size):
        self.verify()
        data = os.pread(self.mem, size, address)
        if len(data) != size: raise ValueError('short_native_read')
        return data

    def word(self, address): return struct.unpack('<Q', self.read(address, 8))[0]

    def restoration(self, initial=False):
        rows = {k: {'matches_original': (data := self.read(address, size)) == self.expected[k],
                    'sha256': digest(data)} for k, (address, size) in REGIONS.items()}
        table = self.word(VBI_GLOBAL)
        if not table: raise ValueError('native_vbi_not_ready')
        slots = [self.word(table + 13 * 8), self.word(table + 0x88 + 13 * 8)]
        if initial:
            if any(slots): raise ValueError('preload_slot_not_free')
            self.table = table
        rows['same_vbi_table'] = table == self.table
        rows['both_slot13_clear'] = slots == [0, 0]
        return rows

    def close(self):
        for name in ('mem', 'pidfd'):
            value = getattr(self, name, None)
            if value is not None: os.close(value); setattr(self, name, None)


def original_regions(rows):
    return all(rows[k]['matches_original'] for k in REGIONS) and rows['same_vbi_table'] and rows['both_slot13_clear']


def publish_ready(path, process, token):
    process.verify()
    if not re.fullmatch('[0-9a-f]{32}', token): raise ValueError('invalid_private_token')
    if not path.is_absolute() or path.parent.resolve() != path.parent: raise ValueError('ready_path_not_canonical')
    parent = path.parent.stat()
    if parent.st_uid != process.uid or parent.st_mode & 0o077: raise ValueError('ready_parent_not_private_owned')
    baseline = process.restoration(initial=True)
    if not original_regions(baseline): raise ValueError('preload_regions_not_original')
    fd, temporary = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        os.fchown(fd, process.uid, -1); os.fchmod(fd, 0o400)
        with os.fdopen(fd, 'wb') as stream:
            fd = None
            stream.write(f'{MODE}|{token}|{process.pid}|{process.start}\n'.encode())
            stream.flush(); os.fsync(stream.fileno())
        process.verify(); os.link(temporary, path, follow_symlinks=False)
    finally:
        if fd is not None: os.close(fd)
        os.unlink(temporary)
    return {'mode': MODE, 'pid': process.pid, 'start_ticks': process.start, 'uid': process.uid,
            'published_monotonic': time.monotonic(), 'pre_arm_regions': baseline}


class FreshLog:
    def __init__(self, path):
        self.path, self.identity, self.offset, self.tail = path, None, 0, b''
        self.consumed_hash = hashlib.sha256()
        if path.exists(): raise ValueError('stale_log')

    def lines(self):
        try:
            with self.path.open('rb') as stream:
                stat = os.fstat(stream.fileno()); identity = stat.st_dev, stat.st_ino
                if self.identity is not None and self.identity != identity: raise ValueError('log_replaced')
                if stat.st_size < self.offset or stat.st_size > 8 * 1024 * 1024: raise ValueError('log_size_invalid')
                self.identity = identity; stream.seek(self.offset); chunk = stream.read(1024 * 1024)
                self.offset += len(chunk)
                self.consumed_hash.update(chunk)
        except FileNotFoundError: return []
        parts = (self.tail + chunk).split(b'\n'); self.tail = parts.pop()
        return [part.decode('utf-8', errors='strict') for part in parts]

    def require_drained(self):
        """Bind a terminal cutoff; neither a partial row nor unread data is proof."""
        if self.tail: raise ValueError('terminal_log_unterminated_record')
        with self.path.open('rb') as stream:
            before = os.fstat(stream.fileno())
            if self.offset > 8 * 1024 * 1024 or before.st_size > 8 * 1024 * 1024:
                raise ValueError('terminal_log_size_invalid')
            if self.identity is None or (before.st_dev, before.st_ino) != self.identity:
                raise ValueError('terminal_log_identity_changed')
            if before.st_size != self.offset: raise ValueError('terminal_log_unread_bytes')
            actual = hashlib.sha256(); remaining = self.offset
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                if not block: raise ValueError('terminal_log_short_read')
                actual.update(block); remaining -= len(block)
            if stream.read(1): raise ValueError('terminal_log_unread_bytes')
            if actual.digest() != self.consumed_hash.digest(): raise ValueError('terminal_log_consumed_content_changed')
            after = os.fstat(stream.fileno())
            # Compare handle metadata to handle metadata. On Windows Python's
            # path-stat and fstat expose different historical ctime semantics.
            with self.path.open('rb') as current_stream:
                current = os.fstat(current_stream.fileno())
            stamp = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)
            if stamp(before) != stamp(after) or stamp(after) != stamp(current):
                raise ValueError('terminal_log_changed_during_cutoff')
        return {'device': after.st_dev, 'inode': after.st_ino, 'bytes_consumed': self.offset,
                'consumed_sha256': actual.hexdigest(),
                'complete_records': True, 'scope': 'observed terminal cutoff; later appends are not covered'}


def agent_record(line):
    """One exact envelope; a path/prefix or second marker cannot hide a fault."""
    if 'X4MP_AGENT' not in line: return None
    match = re.fullmatch(r'(?:[0-9]+\.[0-9]{3} )?X4MP_AGENT ([\x20-\x7e]+)', line)
    if line.count('X4MP_AGENT') != 1 or not match:
        raise ValueError('malformed_or_interleaved_agent_record')
    return match[1]


def accept_terminal_logs(result, agent, world, **evidence):
    """Shared last acceptance boundary, after independent native evidence reads."""
    if 'verdict' in evidence or 'terminal_log_cutoff' in evidence:
        raise ValueError('reserved_terminal_acceptance_field')
    cutoff = {'agent': agent.require_drained(), 'world': world.require_drained()}
    result.update(evidence, verdict='pass', terminal_log_cutoff=cutoff)


def parse_status(line):
    marker = 'X4MP_AGENT LOAD_OBSERVER '
    if marker not in line: return None
    text = line.split(marker, 1)[1]
    kind = text.split(' ', 1)[0]
    if kind not in ('INSTALLED', 'STATE'): return None
    result = {'kind': kind}
    for key, value in re.findall(r'(\w+)=(\[[^\]]*\]|[^ ]+)', text):
        if key in result: raise ValueError('duplicate_status_field')
        result[key] = json.loads(value) if value.startswith('[') else value
    return result


def loader_script_error(line):
    return 'Error while executing' in line and re.search(r'native_portable_(?:linux|loader)\.lua', line) is not None


class AgentEvidence:
    """Only closed, attributed size refusals may identify inherited helpers."""
    def __init__(self):
        self.verified_pids = set()
        self.preverification_build_refusals = []
        self.attributed_build_refusals = []
        self.claim_identity = None
        self.clean_disarms = set()

    def allow_clean_disarm(self, reason):
        if reason == 'disarm_file_present': self.clean_disarms.add((reason, 'off', 'none'))
        elif reason in ('portable_ended', 'admission_probe_ended'):
            self.clean_disarms.update((reason, 'restored', restore) for restore in ('slot', 'worker'))
        else: raise ValueError('unsupported_clean_disarm_scope')

    def verified(self, pid):
        return (pid is not None and self.verified_pids == {pid}
                and (not self.attributed_build_refusals or self.claim_identity is not None))

    def bind_identity(self, pid, start):
        if type(pid) is not int or pid <= 1 or type(start) is not int or start <= 0:
            raise ValueError('unreadable_owned_process_identity')
        if self.claim_identity not in (None, (pid, start)):
            raise ValueError('changed_owned_process_identity')
        self.claim_identity = (pid, start)
        self._resolve_helpers(pid)

    def _resolve_helpers(self, pid):
        if self.claim_identity is not None:
            if pid is not None and pid != self.claim_identity[0]:
                raise ValueError('changed_owned_process_identity')
            pid = self.claim_identity[0]
        if pid is not None and self.verified_pids and self.verified_pids != {pid}:
            raise ValueError('foreign_verified_native_process_in_owned_log')
        for row in self.attributed_build_refusals:
            if row['pid'] == pid or row['pid'] in self.verified_pids:
                raise ValueError('native_process_build_refused')
            if self.claim_identity is not None:
                if row['start_ticks'] < self.claim_identity[1]:
                    raise ValueError('stale_helper_build_refusal')
                if self.verified_pids == {self.claim_identity[0]}:
                    row['attribution'] = 'attributed_foreign_small_executable'

    def observe(self, line, claimed_pid, claimed_start_ticks=None):
        if claimed_start_ticks is not None:
            self.bind_identity(claimed_pid, claimed_start_ticks)
        self._resolve_helpers(claimed_pid)
        body = agent_record(line)
        if body is None: return None
        verified = re.fullmatch(r'VERIFIED build=23660954 version=9\.00 \(611726\) sites=25 pid=([1-9][0-9]*)', body)
        if 'VERIFIED' in body and not verified: raise ValueError('malformed_native_verification')
        if verified:
            pid = int(verified[1])
            if pid in self.verified_pids: raise ValueError('duplicate_native_verification')
            self.verified_pids.add(pid)
            if len(self.verified_pids) > 1: raise ValueError('multiple_verified_native_processes_in_owned_log')
            self._resolve_helpers(claimed_pid)
        if ('REFUSED' in body or 'FAULT' in body) and not body.startswith('REFUSED '):
            raise ValueError('native_observer_refused')
        if body.startswith('SCHED STALL') or re.match(r'(?:PORTABLE|LOAD_OBSERVER|ADMISSION) FINAL(?: |$)', body):
            raise ValueError('native_runtime_incomplete')
        if body.startswith('DISARMED'):
            disarm = re.fullmatch(r'DISARMED reason=([a-z_]+) sched=([a-z_]+) sched_reason=none restore=([a-z_]+)', body)
            if not disarm or disarm.groups() not in self.clean_disarms:
                raise ValueError('native_disarm_fault_or_unowned_scope')
        if body.startswith('SCHED RESTORED') and not re.fullmatch(r'SCHED RESTORED restore=(?:slot|worker)', body):
            raise ValueError('native_scheduler_restoration_fault')
        if body.startswith('REFUSED '):
            reason = body.removeprefix('REFUSED ')
            if reason.startswith('schema='):
                if claimed_pid is not None and self.claim_identity is None:
                    raise ValueError('unreadable_owned_process_identity')
                match = re.fullmatch(r'schema=x4-build-refusal-v1 pid=([1-9][0-9]*) start_ticks=([1-9][0-9]*) '
                    r'reason=executable_size executable_bytes=([1-9][0-9]*) pinned_bytes=60459280', reason)
                if not match: raise ValueError('unreadable_or_native_build_refusal')
                pid, start, size = map(int, match.groups())
                if pid <= 1 or pid > 0xffffffff or start > 0xffffffffffffffff or size >= 60459280:
                    raise ValueError('invalid_helper_build_refusal')
                if any(row['pid'] == pid for row in self.attributed_build_refusals):
                    raise ValueError('duplicate_or_reused_helper_identity')
                if len(self.attributed_build_refusals) >= 16: raise ValueError('excess_attributed_build_refusals')
                self.attributed_build_refusals.append({'pid': pid, 'start_ticks': start,
                    'reason': 'executable_size', 'executable_bytes': size, 'pinned_bytes': 60459280,
                    'attribution': 'pending_owned_verification'})
                self._resolve_helpers(claimed_pid)
                return None
            size = re.fullmatch(r'executable size ([0-9]+) \(pinned 60459280\)', reason)
            # Exact X4 is independently pinned by PIDFD/full file hash. These
            # earlier small-executable refusals remain unattributed diagnostics;
            # they cannot establish acceptance without native VERIFIED(pid).
            if self.verified_pids or not size or not 0 < int(size[1]) < 60459280:
                raise ValueError('ambiguous_native_build_refusal')
            if len(self.preverification_build_refusals) >= 16: raise ValueError('excess_unattributed_build_refusals')
            self.preverification_build_refusals.append({'reason': reason, 'attribution': 'unattributed_preverification_helper'})
        row = parse_status(line)
        if row and not self.verified(claimed_pid): raise ValueError('observer_lifecycle_before_owned_verification')
        return row


def provisional_status(row):
    expected = {'phase': 'Completed', 'sites': '3', 'ready': '0', 'active': '0', 'failure': 'none',
                'restore_error': 'none', 'output_written': '1', 'publishable': '1'}
    if any(row.get(k) != v for k, v in expected.items()): return False
    if any(row.get(k) != [1, 1, 1] for k in ('calls', 'returns', 'raw_calls', 'raw_returns')): return False
    owner = int(row.get('owner', '0'))
    if not owner or not 0 < int(row.get('entries', '0')) <= 1048576: return False
    if not 0 <= int(row.get('capture_ns', '-1')) <= 5_000_000_000: return False
    marks = []
    for index in range(6):
        parts = row.get(f'mark{index}', '').split(',')
        if len(parts) != 13 or int(parts[0]) != owner: return False
        if any(not re.fullmatch('[0-9a-f]{16}', parts[i]) for i in (2, 3, 4, 9, 10)): return False
        marks.append(int(parts[1]))
    return all(a <= b for a, b in zip([marks[i] for i in (0, 2, 3, 1, 4)], [marks[i] for i in (2, 3, 1, 4, 5)]))


def complete_status(row): return provisional_status(row) and row.get('restored') == '1'


def publish_disarm(path, process):
    """Request native-owned restoration; never write engine memory or signal X4."""
    process.verify()
    if not path.is_absolute() or path.parent.resolve() != path.parent: raise ValueError('disarm_path_not_canonical')
    parent = path.parent.stat()
    if parent.st_uid != process.uid or parent.st_mode & 0o077: raise ValueError('disarm_parent_not_private_owned')
    fd, temporary = tempfile.mkstemp(prefix='.disarm.', dir=path.parent)
    try:
        os.fchown(fd, process.uid, -1); os.fchmod(fd, 0o400)
        with os.fdopen(fd, 'wb') as stream:
            fd = None
            stream.write(f'{MODE}|{process.pid}|{process.start}\n'.encode())
            stream.flush(); os.fsync(stream.fileno())
        process.verify(); os.link(temporary, path, follow_symlinks=False)
    finally:
        if fd is not None: os.close(fd)
        os.unlink(temporary)
    return {'pid': process.pid, 'start_ticks': process.start, 'uid': process.uid,
            'published_monotonic': time.monotonic(), 'reason': 'completed_observer_restore'}


class RestorationDrain:
    def __init__(self): self.request = None

    def observe(self, row, process, path, publish=publish_disarm):
        if not provisional_status(row): return False
        if self.request is None:
            if row.get('restored') != '0': raise ValueError('observer_restored_before_owned_drain')
            self.request = publish(path, process)
            return False
        return complete_status(row)


def import_map(data, status, contract):
    if data[:8] != b'X4IMP1\0\0' or len(data) < 16: raise ValueError('invalid_import_magic')
    count = struct.unpack_from('<Q', data, 8)[0]
    if not 0 < count <= 1048576 or count != int(status['entries']) or len(data) != 16 + count * 16:
        raise ValueError('incomplete_import_output')
    entries = list(struct.iter_unpack('<QQ', data[16:]))
    if entries[0][0] == 0 or any(a[0] >= b[0] for a, b in zip(entries, entries[1:])):
        raise ValueError('import_ids_not_unique_sorted')
    unresolved = sum(runtime == 0 for _, runtime in entries)
    if unresolved != int(status['unresolved']): raise ValueError('unresolved_count_mismatch')
    selected = {int(row['saved'], 16) for row in contract['actors']}
    fixtures = {saved: runtime for saved, runtime in entries if saved in selected}
    if set(fixtures) != selected or not all(fixtures.values()) or len(set(fixtures.values())) != 12:
        raise ValueError('fixture_imports_missing_or_aliased')
    wanted_runtime = set(fixtures.values())
    if sum(runtime in wanted_runtime for _, runtime in entries) != 12: raise ValueError('fixture_runtime_has_alias')
    return fixtures, {'entries': count, 'unresolved': unresolved, 'sha256': digest(data), 'fixture_count': len(fixtures)}


def resolve_component(process, registry, runtime):
    index = (runtime & 0x1ffffff) - 1
    if index < 0 or not registry: raise ValueError('invalid_registry_identity')
    page = process.word(registry + 0x60 + (index >> 20) * 8)
    within = index & 0xfffff; lane = within % 3
    if not page: raise ValueError('missing_registry_page')
    record = process.read(page + (within // 3) * 32, 32)
    flags = struct.unpack_from('<I', record, 28)[0] >> (16 + lane * 4)
    generation = struct.unpack_from('<H', record, 24 + lane * 2)[0]
    obj = struct.unpack_from('<Q', record, lane * 8)[0]
    if flags & 5 != 5 or generation != (runtime >> 25) & 0xffff or not obj or obj & 7:
        raise ValueError('registry_generation_or_flags')
    if process.word(obj + 8) != runtime: raise ValueError('component_full_id_mismatch')
    return obj


def fixture_identity(process, fixtures):
    registry = process.word(REGISTRY_GLOBAL); result = []
    for saved, runtime in sorted(fixtures.items()):
        obj = resolve_component(process, registry, runtime)
        if process.word(obj) != 0x3d21748: raise ValueError('fixture_class_mismatch')
        result.append({'saved': f'{saved:016x}', 'runtime': f'{runtime:016x}', 'class': 'ship_small'})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('claim', 'agent-so', 'agent-log', 'world-log', 'ready-file', 'disarm-file', 'contract', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--startup-timeout', type=int, default=900)
    args = parser.parse_args()
    if not re.fullmatch('[a-z0-9_-]{1,80}', args.run_id) or not 60 <= args.startup_timeout <= 1200:
        parser.error('bounded run identity and startup timeout required')
    if args.disarm_file.parent != args.ready_file.parent or args.disarm_file.name != 'disarm':
        parser.error('exact same-case owned disarm path required')
    if any(path.exists() for path in (args.claim, args.ready_file, args.disarm_file, args.report, Path(str(args.agent_log) + '.imports'))):
        parser.error('fresh claim/request/report/import paths required')
    token = os.environ.get('X4MP_LOAD_TOKEN', '')
    if not re.fullmatch('[0-9a-f]{32}', token): parser.error('private token missing')
    contract = json.loads(args.contract.read_text()); contract_sha = validate_contract(contract)
    world, agent = FreshLog(args.world_log), FreshLog(args.agent_log)
    result = {'mode': MODE, 'verdict': 'incomplete', 'contract_sha256': contract_sha, 'agent_sha256': AGENT_SHA,
              'simulation_admission': False, 'cross_platform_gameplay': False, 'markers': {}}
    process = None; installed = None; final = None; request = None; started = time.monotonic(); drain = RestorationDrain()
    agent_evidence = AgentEvidence()
    print('LOAD_OBSERVER_PROBE READY read_only=1 permits=0', flush=True)
    try:
        while time.monotonic() - started < args.startup_timeout:
            if process is None and args.claim.exists(): process = OwnedProcess(json.loads(args.claim.read_text()), args.agent_so)
            if process:
                process.verify()
                agent_evidence.bind_identity(process.pid, process.start)
            result['owned_native_verified'] = bool(process and agent_evidence.verified(process.pid))
            for line in agent.lines():
                row = agent_evidence.observe(line, process.pid if process else None, process.start if process else None)
                result['unattributed_preverification_build_refusals'] = agent_evidence.preverification_build_refusals
                result['attributed_build_refusals'] = agent_evidence.attributed_build_refusals
                result['owned_native_verified'] = bool(process and agent_evidence.verified(process.pid))
                if row:
                    if row.get('failure') not in (None, 'none') or row.get('restore_error') not in (None, 'none'):
                        raise ValueError('native_observer_fault')
                    result['last_status'] = row
                    if row['kind'] == 'INSTALLED' and installed is None:
                        if request is None: raise ValueError('installed_before_owned_request')
                        installed = row; result['installed'] = dict(status=row, observed_monotonic=time.monotonic())
                    if process and drain.observe(row, process, args.disarm_file): final = row
                    if drain.request is not None:
                        result['disarm_request'] = drain.request
                        agent_evidence.allow_clean_disarm('disarm_file_present')
            for line in world.lines():
                if loader_script_error(line): raise ValueError('portable_loader_module_error')
                marker = '[X4LAN] PORTABLE_BOOT '
                if marker not in line: continue
                text = line.split(marker, 1)[1].strip()
                if not text.endswith(' run=' + args.run_id): raise ValueError('world_run_mismatch')
                if text.startswith('ERROR '): raise ValueError('portable_loader_error')
                if text.startswith('MENU '):
                    expected = f'MENU schema={SCHEMA_SHA} snapshot={contract["snapshot_sha256"]} run={args.run_id}'
                    if text != expected or 'MENU' in result['markers']: raise ValueError('invalid_or_repeated_menu')
                    result['markers']['MENU'] = {'text': text, 'observed_monotonic': time.monotonic()}
                elif text.startswith('LOAD_REQUEST '):
                    expected = f'LOAD_REQUEST snapshot={contract["snapshot_sha256"]} contract={contract_sha} run={args.run_id}'
                    if text != expected or installed is None or 'LOAD_REQUEST' in result['markers']:
                        raise ValueError('load_without_installed_ack_or_duplicate')
                    result['markers']['LOAD_REQUEST'] = {'text': text, 'observed_monotonic': time.monotonic()}
                elif text.startswith('LOADED '):
                    pattern = rf'LOADED game=([^ ]+) paused=(true|false) snapshot={contract["snapshot_sha256"]} run={args.run_id}'
                    matched = re.fullmatch(pattern, text)
                    if not matched or not math.isfinite(float(matched[1])) or 'LOAD_REQUEST' not in result['markers']:
                        raise ValueError('loaded_without_exact_owned_request')
                    result['markers'].setdefault('LOADED', {'text': text, 'observed_monotonic': time.monotonic()})
            if process and agent_evidence.verified(process.pid) and 'MENU' in result['markers'] and request is None:
                request = publish_ready(args.ready_file, process, token); result['ready_request'] = request
            if final and 'LOADED' in result['markers']:
                restored = process.restoration()
                if original_regions(restored):
                    path = Path(str(args.agent_log) + '.imports')
                    if path.stat().st_size > 16 + 1048576 * 16: raise ValueError('oversized_import_output')
                    fixtures, output = import_map(path.read_bytes(), final, contract)
                    identities = fixture_identity(process, fixtures)
                    accept_terminal_logs(result, agent, world, final_status=final, restoration=restored, import_output=output,
                                         fixture_identities=identities, loaded_snapshot_verified=True)
                    break
            time.sleep(.02)
        else: raise ValueError('bounded_load_observer_timeout')
    except Exception as error:
        result.update(verdict='fail', error=str(error))
        if process:
            try: result['final_restoration_diagnostic'] = process.restoration()
            except Exception as final_error: result['final_restoration_error'] = str(final_error)
    finally:
        if process: process.close()
        temporary = args.report.with_suffix('.tmp'); temporary.write_text(json.dumps(result, indent=2) + '\n'); temporary.replace(args.report)
    print('LOAD_OBSERVER_PROBE FINISHED verdict=' + result['verdict'], flush=True)
    raise SystemExit(0 if result['verdict'] == 'pass' else 1)


if __name__ == '__main__': main()
