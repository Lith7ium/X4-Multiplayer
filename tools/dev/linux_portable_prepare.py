"""Same-process, one-shot stock verification before coordinator startup.

No reusable hash cache or native operation. Retained descriptors are read-only;
they do not prevent external writes. Every transition therefore rechecks both
descriptor and pathname identity, including ctime, against the hashed state.
"""
import hashlib
import ctypes
import json
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import select
import signal
import stat
import sys
import time

from native_portable import canonical_json, exact_keys, hexword
from native_portable_coordinator import validate_participants

PREPARE_SECONDS = 1800
WAIT_SECONDS = 1200
BLOCK = 1024 * 1024


def identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns,
            info.st_mode, info.st_uid, info.st_gid, info.st_nlink)


def control_path(root, path, *, absent=False):
    root, path = Path(root), Path(path)
    if (not root.is_absolute() or root.resolve(strict=True) != root
            or root.stat().st_mode & 0o077 or not path.is_absolute() or path.parent != root):
        raise ValueError('preparation control path outside canonical private root')
    if absent:
        if os.path.lexists(path): raise ValueError('preparation control target already exists')
    elif path.resolve(strict=True) != path:
        raise ValueError('linked preparation control path')
    return path


def read_control(root, path, uid=0):
    path = control_path(root, path)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        before = os.fstat(stream.fileno())
        if (not stat.S_ISREG(before.st_mode) or before.st_uid != uid or before.st_nlink != 1
                or before.st_mode & 0o077 or not 0 < before.st_size <= 65536):
            raise ValueError('preparation control ownership/mode/size differs')
        raw = stream.read(65537)
        if identity(before) != identity(os.fstat(stream.fileno())) or identity(before) != identity(path.lstat()):
            raise ValueError('preparation control changed during read')
    return decode_control(raw), hashlib.sha256(raw).hexdigest()


def decode_control(raw):
    def unique(items):
        value = {}
        for key, item in items:
            if key in value: raise ValueError('duplicate preparation control key')
            value[key] = item
        return value
    return json.loads(raw, object_pairs_hook=unique)


def validate_request(value, capability, participant, review_sha):
    extra = ('import_math_sha256',) if 'import_math_sha256' in capability else ()
    exact_keys(value, ('schema', 'run_id', 'mode', 'contract_sha256', 'participants',
        'review_sha256', 'ticks', 'startup_timeout_ms', 'timeout_ms', 'remote_script') + extra,
        'preparation request')
    if value['schema'] != 'x4-portable-prepare-v1' or not re.fullmatch('[a-z0-9_-]{1,80}', value['run_id']):
        raise ValueError('preparation request schema/run differs')
    if value['review_sha256'] != review_sha or any(value[key] != capability[key]
            for key in ('mode', 'contract_sha256') + extra):
        raise ValueError('preparation artifact or capability differs')
    validate_participants(value['participants'])
    if value['participants']['2'] != participant: raise ValueError('preparation participant differs')
    for name, low, high in (('ticks', 2, 120), ('startup_timeout_ms', 1000, 600000), ('timeout_ms', 1000, 120000)):
        if type(value[name]) is not int or not low <= value[name] <= high:
            raise ValueError('preparation timing differs')
    if value['timeout_ms'] > value['startup_timeout_ms']: raise ValueError('preparation timing order differs')
    if (not isinstance(value['remote_script'], str) or not value['remote_script'].startswith('/')
            or '\0' in value['remote_script']):
        raise ValueError('preparation coordinator path differs')
    return value


class StockFiles:
    """Descriptors live for exactly this supervisor's preparation/native attempt."""
    def __init__(self):
        self.rows = {}
        self.closed = False

    def acquire(self, game, files, check):
        game = Path(game).resolve(strict=True)
        try:
            for name, expected in files.items():
                check()
                relative = PurePosixPath(name)
                if relative.is_absolute() or '..' in relative.parts or not relative.parts:
                    raise ValueError('invalid stock relative path')
                path = game / name
                resolved = path.resolve(strict=True)
                if not resolved.is_relative_to(game): raise ValueError('stock outside native game root')
                link_identity = identity(path.lstat())
                fd = os.open(resolved, os.O_RDONLY | os.O_NOFOLLOW)
                row = {'fd': fd, 'path': path, 'resolved': resolved, 'link': link_identity}
                self.rows[name] = row  # Any subsequent exception closes this fd too.
                before = os.fstat(fd); row['identity'] = identity(before)
                if not stat.S_ISREG(before.st_mode) or before.st_size != expected['bytes']:
                    raise ValueError('stock type/size differs')
                digest = hashlib.sha256()
                while True:
                    check()
                    data = os.read(fd, BLOCK)
                    if not data: break
                    digest.update(data)
                if digest.hexdigest() != expected['sha256']: raise ValueError('stock payload differs')
                self._check_row(row)
            self.recheck(check)
        except BaseException:
            self.close()
            raise
        return self

    @staticmethod
    def _check_row(row):
        path, resolved = row['path'], row['resolved']
        if (path.resolve(strict=True) != resolved or identity(path.lstat()) != row['link']
                or identity(resolved.stat()) != row['identity']
                or identity(os.fstat(row['fd'])) != row['identity']):
            raise ValueError('retained stock pathname/descriptor changed')

    def recheck(self, check=lambda: None):
        if self.closed: raise ValueError('stock descriptors already closed')
        for row in self.rows.values():
            check(); self._check_row(row)

    def old_stamps(self):
        return {name: (row['identity'][1], row['identity'][2], row['identity'][3]) for name, row in self.rows.items()}

    def close(self):
        if self.closed: return
        self.closed = True
        for row in self.rows.values(): os.close(row['fd'])


class Preparation:
    def __init__(self, root, request, handoff, capability, participant, review_sha, *, uid=0,
                 clock=time.monotonic, stdin_fd=0):
        self.root = Path(root); self.uid = uid; self.clock = clock; self.stdin_fd = stdin_fd
        self.handoff = control_path(root, handoff, absent=True)
        self.request_path = control_path(root, request)
        self.request, self.request_sha = read_control(root, request, uid)
        validate_request(self.request, capability, participant, review_sha)
        self.review_sha = review_sha
        self.pid = os.getpid()
        self.start = int(Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19])
        self.stock = StockFiles(); self.cancelled = False; self.accepted = False; self.ready = False
        self.deadline = clock() + PREPARE_SECONDS; self.handlers = {}
        for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            self.handlers[number] = signal.signal(number, self._cancel)

    def _cancel(self, _number, _frame): self.cancelled = True

    def check(self):
        if self.cancelled: raise RuntimeError('owned preparation cancelled')
        if self.clock() >= self.deadline: raise TimeoutError('bounded preparation expired')
        if self.stdin_fd is not None:
            poll = select.poll(); poll.register(self.stdin_fd, select.POLLIN | select.POLLHUP | select.POLLERR)
            if poll.poll(0):
                # The controller holds this channel open without data. It is
                # cancellation/lifetime evidence, never a configuration channel.
                raise RuntimeError('preparation controller input closed or changed')

    def hash_stock(self, bundle, contract, game):
        if self.ready or self.stock.rows or self.stock.closed:
            raise ValueError('preparation stock verification is one-shot')
        value = json.loads((Path(bundle) / 'stock.json').read_text()); claimed = value.pop('manifest_sha256')
        if (hashlib.sha256(canonical_json(value)).hexdigest() != claimed or claimed != contract['stock_sha256']
                or value.get('payloads_hashed') is not True or len(value['files']) != 127 or len(value['dlc']) != 7):
            raise ValueError('incomplete common stock manifest')
        self.stock.acquire(game, value['files'], self.check)
        self.stock_sha = claimed
        return self.stock.old_stamps()

    def prepared(self):
        if self.ready or len(self.stock.rows) != 127:
            raise ValueError('complete one-shot stock verification required')
        self.check(); self.stock.recheck(self.check)
        self.ready = True
        self.deadline = self.clock() + WAIT_SECONDS
        self.receipt = {'schema': 'x4-portable-prepared-v1', 'pid': self.pid, 'start_ticks': self.start,
            'request_sha256': self.request_sha, 'review_sha256': self.review_sha,
            'stock_files': len(self.stock.rows), 'stock_sha256': self.stock_sha,
            'preparation_timeout_seconds': PREPARE_SECONDS, 'handoff_timeout_seconds': WAIT_SECONDS}
        return self.receipt

    def accept(self, validate_session):
        if not self.ready: raise ValueError('stock preparation is not ready')
        if self.accepted: raise ValueError('preparation handoff already consumed')
        self.check(); self.stock.recheck(self.check)
        if read_control(self.root, self.request_path, self.uid)[1] != self.request_sha:
            raise ValueError('preparation request changed')
        value, digest = read_control(self.root, self.handoff, self.uid)
        exact_keys(value, ('schema', 'pid', 'start_ticks', 'request_sha256', 'config_path', 'config_sha256'), 'preparation handoff')
        if (value['schema'] != 'x4-portable-handoff-v1' or (value['pid'], value['start_ticks']) != (self.pid, self.start)
                or type(value['pid']) is not int or type(value['start_ticks']) is not int
                or value['request_sha256'] != self.request_sha):
            raise ValueError('preparation handoff process/request differs')
        hexword(value['config_sha256'], 64, 'private config digest')
        config, config_sha = read_control(self.root, value['config_path'], self.uid)
        if config_sha != value['config_sha256']: raise ValueError('preparation final config changed')
        expected = {key: item for key, item in self.request.items() if key not in ('schema', 'run_id', 'review_sha256')}
        if any(config.get(key) != item for key, item in expected.items()):
            raise ValueError('preparation final session differs')
        validate_session(config)
        self.accepted = True; self.handoff_sha = digest; self.config_sha = config_sha
        self.config_path = Path(value['config_path'])
        return config, config_sha

    def wait(self, validate_session):
        while not os.path.lexists(self.handoff):
            self.check(); time.sleep(.1)
        return self.accept(validate_session)

    def active_check(self):
        # Native startup/runtime keep their existing separate deadlines. The
        # controller channel and cancellation remain live after handoff.
        prior = self.deadline; self.deadline = float('inf')
        try: self.check()
        finally: self.deadline = prior

    def close(self):
        self.stock.close()
        for number, previous in self.handlers.items(): signal.signal(number, previous)
        self.handlers.clear()


def publish_handoff(root, target, value, uid=0):
    """One-shot same-directory publication; caller first proves coordinator/PIDFD."""
    target = control_path(root, target, absent=True)
    raw = canonical_json(value)
    if len(raw) > 65536: raise ValueError('handoff too large')
    directory = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    temporary = '.portable-handoff-' + secrets.token_hex(16)
    created = False
    try:
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=directory)
        created = True
        with os.fdopen(fd, 'wb') as stream:
            os.fchmod(stream.fileno(), 0o600); os.fchown(stream.fileno(), uid, -1)
            stream.write(raw); stream.flush(); os.fsync(stream.fileno())
        before = os.fstat(directory); current = Path(root).lstat()
        if (before.st_dev, before.st_ino, before.st_mode, before.st_uid) != (current.st_dev, current.st_ino, current.st_mode, current.st_uid):
            raise ValueError('private publication directory changed')
        # A hardlink would transiently expose nlink2, which the receiver rightly
        # rejects. NOREPLACE rename exposes exactly the final nlink1 file.
        _rename_noreplace(directory, temporary, target.name)
    finally:
        try:
            if created:
                try: os.unlink(temporary, dir_fd=directory)
                except FileNotFoundError: pass
        finally:
            os.close(directory)


def _rename_noreplace(directory, source, target):
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, 'renameat2', None)
    if rename is None: raise RuntimeError('atomic no-replace rename unavailable')
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(directory, os.fsencode(source), directory, os.fsencode(target), 1) != 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number))


def verify_preparer(root, bundle, request, handoff, receipt, review_sha, stock_sha, uid=0):
    """Pin the waiting supervisor before publishing its one allowed handoff."""
    root, bundle = Path(root), Path(bundle)
    request = control_path(root, request)
    handoff = control_path(root, handoff, absent=True)
    exact_keys(receipt, ('schema', 'pid', 'start_ticks', 'request_sha256', 'review_sha256',
        'stock_files', 'stock_sha256', 'preparation_timeout_seconds', 'handoff_timeout_seconds'), 'prepared receipt')
    if (receipt['schema'] != 'x4-portable-prepared-v1' or receipt['review_sha256'] != review_sha
            or receipt['stock_sha256'] != stock_sha or receipt['stock_files'] != 127
            or receipt['preparation_timeout_seconds'] != PREPARE_SECONDS or receipt['handoff_timeout_seconds'] != WAIT_SECONDS
            or read_control(root, request, uid)[1] != receipt['request_sha256']):
        raise ValueError('prepared receipt differs from owned request/artifacts')
    pid, start = receipt['pid'], receipt['start_ticks']
    if type(pid) is not int or pid <= 1 or type(start) is not int or start <= 0:
        raise ValueError('invalid preparing supervisor identity')
    script = bundle / 'bin/linux_native_portable_run.py'
    expected = ['python3', '-B', str(script), '--root', str(root), '--bundle', str(bundle),
        '--review-sha256', review_sha, '--prepare-request', str(request), '--handoff', str(handoff)]
    fd = os.pidfd_open(pid)
    try:
        proc = Path('/proc') / str(pid)
        watch = select.poll(); watch.register(fd, select.POLLIN)
        command = proc.joinpath('cmdline').read_bytes().rstrip(b'\0').split(b'\0')
        if (watch.poll(0) or proc.stat().st_uid != uid
                or int(proc.joinpath('stat').read_text().rsplit(')', 1)[1].split()[19]) != start
                or proc.joinpath('exe').resolve() != Path(sys.executable).resolve()
                or command != [word.encode() for word in expected]):
            raise ValueError('preparing supervisor process/command differs')
        if watch.poll(0): raise ValueError('preparing supervisor exited')
        return fd
    except BaseException:
        os.close(fd)
        raise
