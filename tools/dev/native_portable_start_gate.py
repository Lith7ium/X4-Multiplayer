"""Opt-in owner-pipe gate before the released Windows native launch sequence."""
import os
from pathlib import Path
import secrets
import sys
import tempfile
import threading
import time

import native_portable_prelaunch as binding
from native_portable import canonical_json


def publish_request(path, value):
    """Complete bytes appear once; the controller cannot see a partial JSON."""
    fd, temporary = tempfile.mkstemp(prefix='.prelaunch-request-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(canonical_json(value)); stream.flush(); os.fsync(stream.fileno())
        if os.name == 'nt':
            os.rename(temporary, path)  # Windows rename refuses an existing target.
        else:
            from linux_portable_prepare import _rename_noreplace
            directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try: _rename_noreplace(directory, Path(temporary).name, path.name)
            finally: os.close(directory)
    finally:
        try: os.unlink(temporary)
        except FileNotFoundError: pass


class OwnerChannel:
    """One bounded newline record; EOF/any extra byte invalidates it immediately."""
    def __init__(self, stream):
        self.lock = threading.Lock(); self.message = None; self.error = None
        self.thread = threading.Thread(target=self._read, args=(stream,), daemon=True)
        self.thread.start()

    def _read(self, stream):
        try:
            raw = bytearray()
            while True:
                block = stream.read(1)
                with self.lock:
                    if not block:
                        self.error = 'prelaunch owner channel closed'; return
                    if self.message is not None:
                        self.error = 'prelaunch owner sent extra data'; return
                    raw.extend(block)
                    if len(raw) > binding.MAX_MESSAGE_BYTES:
                        self.error = 'prelaunch owner message too large'; return
                    if block == b'\n': self.message = binding.decode(bytes(raw))
        except Exception as error:
            with self.lock: self.error = 'prelaunch owner channel error: ' + type(error).__name__

    def snapshot(self):
        with self.lock:
            if self.error: raise ValueError(self.error)
            return self.message


class StartGate:
    def __init__(self, request, config, contract, review, stream, live_check, *, now=time.monotonic_ns):
        self.request, self.config, self.contract, self.review = request, config, contract, review
        self.live_check, self.now = live_check, now
        self.channel = OwnerChannel(stream); self.grant = None

    def check(self, *, require_grant=True):
        now = self.now()
        if now >= self.request['deadline_monotonic_ns']: raise ValueError('prelaunch absolute startup deadline expired')
        self.live_check(require_grant)
        now = self.now()
        message = self.channel.snapshot()
        if message is None:
            if require_grant: raise ValueError('prelaunch owner grant missing')
            return False
        binding.validate_grant(message, self.request, self.config, self.contract, self.review, now)
        if self.grant is not None and self.grant != message: raise ValueError('prelaunch owner grant changed')
        self.grant = message
        # Recheck channel after all potentially slow validations/reads. No fresh
        # proof or new deadline may replace a proof that expired during a lock wait.
        self.channel.snapshot()
        binding.validate_grant(message, self.request, self.config, self.contract, self.review, self.now())
        return True

    def wait(self):
        while not self.check(require_grant=False): time.sleep(.02)


def install(lane, *, contract, participants, remote_script, prepared_path, review_path):
    """Add seams only; all original reservation, admission and cleanup remain."""
    prepared_raw = Path(prepared_path).read_bytes(); review_raw = Path(review_path).read_bytes()
    prepared = binding.validate_prepared(binding.decode(prepared_raw), contract)
    review = binding.decode(review_raw)
    if binding.sha(review_raw) != prepared['review_sha256']: raise ValueError('prelaunch review differs from prepared supervisor')
    original_server = lane.SessionRun.start_server
    original_launch = lane.SessionRun.launch
    original_sequence = lane.Run.start_sequence
    original_x4 = lane.Run.start_x4

    def alive(session, force=False):
        # SessionRun.refresh_reserved can discard a prepared reservation after
        # RESERVE_MAX_S. Refresh the already-owned individual locks instead.
        if session.server is None: raise ValueError('prelaunch owned server missing')
        failure = session.server.poll()
        if failure: raise ValueError('prelaunch owned server failed: ' + str(failure[0]))
        now = time.monotonic()
        schedule = session.portable_gate_poll
        if force or now >= schedule['reservation']:
            for client in session.clients:
                lock = client.lock
                if lock is None or not lock.held or lane.core.read_owner(lock.path).get('run') != lock.info.get('run'):
                    raise ValueError('prelaunch prepared lane reservation lost')
                client.refresh_reserved()
            quiet = session.quiet
            if quiet is not None:
                if not quiet.held or lane.core.read_owner(quiet.path).get('run') != quiet.info.get('run'):
                    raise ValueError('prelaunch owned quiet reservation lost')
                quiet.refresh()
            schedule['reservation'] = now + 1
        if force or now >= schedule['network']:
            # Match released monitoring cadence. SSH policy records each valid
            # exception, so polling it at the 20ms pipe cadence exhausts its
            # unchanged bounded audit long before the startup deadline.
            session.server.check_network()
            schedule['network'] = now + 5

    def start_server(session):
        started = time.monotonic_ns()
        original_server(session)
        if len(session.clients) != 1: raise ValueError('prelaunch requires one owned Windows participant')
        session_raw = (session.outdir / 'portable-session.json').read_bytes()
        ready_raw = (session.outdir / 'portable-barrier-remote.json').read_bytes()
        config, ready = binding.decode(session_raw), binding.decode(ready_raw)
        request = binding.make_wait(contract, participants, session_raw, ready_raw, prepared_raw,
            session_id=session.session_id, client_run=session.clients[0].run_id,
            runner=dict(pid=os.getpid(), created=lane.win().process_created(os.getpid())),
            nonce=secrets.token_hex(16), started_ns=started)
        linux_config = binding.linux_config(config, ready, remote_script, request)
        session.portable_gate_poll = dict(reservation=0.0, network=0.0)
        gate = StartGate(request, linux_config, contract, review, sys.stdin.buffer, lambda force: alive(session, force))
        session.portable_start_gate = gate
        for client in session.clients: client.portable_start_gate = gate
        path = session.outdir / 'portable-prelaunch-request.json'
        publish_request(path, request)
        session.result['portable_prelaunch'] = dict(request_sha256=binding.object_sha(request),
            deadline_monotonic_ns=request['deadline_monotonic_ns'], native_pair_verified=False)
        print('PORTABLE_PRELAUNCH WAIT request=' + binding.object_sha(request), flush=True)
        gate.wait()
        session.result['portable_prelaunch']['grant'] = gate.grant
        print('PORTABLE_PRELAUNCH VERIFIED linux_frame0=' + gate.grant['fetch']['receipt']['frame0_sha256'], flush=True)

    def launch(session):
        session.portable_start_gate.check()
        return original_launch(session)

    def start_sequence(run):
        run.portable_start_gate.check()
        return original_sequence(run)

    def start_x4(run):
        # This wrapper sits outside the existing final artifact/identity wrapper.
        # That wrapper invokes final_check again after hashing, immediately before
        # NativeIdentity creation and original start_x4.
        run.portable_start_gate.check()
        return original_x4(run)

    lane.SessionRun.start_server, lane.SessionRun.launch = start_server, launch
    lane.Run.start_sequence, lane.Run.start_x4 = start_sequence, start_x4


def final_check(run):
    gate = getattr(run, 'portable_start_gate', None)
    if gate is None: raise ValueError('prelaunch gate missing at final native launch')
    gate.check()
