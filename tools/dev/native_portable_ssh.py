"""One portable coordinator for Windows over owned SSH and Linux over loopback.

This transports declared state only. The owning runners must independently prove
the two native processes, exact artifacts, load boundary and cleanup. There is no
local coordinator fallback. Default SSH host verification remains enabled.
"""
import argparse
import hashlib
import hmac
import json
import os
from pathlib import Path
import queue
import re
import select
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

from native_portable import canonical_json, exact_keys, validate_contract
from native_portable_coordinator import (MAX_PACKET, PortableBarrier, atomic_report,
                                         number, validate_participants)

MAX_LINE = 16 * 1024 * 1024
WINDOWS = ('127.0.0.1', 1)  # Virtual address; never a real network destination.


def provenance(contract=None):
    # Legacy closures remain exactly three files. The optional capability module
    # is executable contract-validation code and belongs in the new closure.
    names = ('native_portable_ssh.py', 'native_portable.py', 'native_portable_coordinator.py')
    if contract is not None:
        validate_contract(contract)
        if 'import_math' in contract:
            names += ('portable_import_capability.py', 'portable_import_capability_v1.py')
    root = Path(__file__).resolve().parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in names}


def validate_config(value):
    extra = ('linux_first',) if isinstance(value, dict) and 'linux_first' in value else ()
    exact_keys(value, ('token', 'contract', 'participants', 'ticks', 'hold_tick',
                      'hold_seconds', 'startup_timeout', 'peer_timeout', 'lifetime') + extra, 'SSH config')
    validate_contract(value['contract']); validate_participants(value['participants'])
    if extra:
        import portable_import_capability as cap
        if value['linux_first'] is not True or value['contract'].get('import_math', {}).get('mode') != cap.MODE:
            raise ValueError('Linux-first startup requires the explicit Euler capability')
    for key, lower, upper in (('ticks', 2, 120), ('hold_tick', 1, 119),
                              ('startup_timeout', 1, 1200), ('peer_timeout', 1, 120),
                              ('lifetime', 5, 1800)):
        if type(value[key]) is not int or not lower <= value[key] <= upper:
            raise ValueError('invalid bounded ' + key)
    if (type(value['hold_seconds']) not in (int, float) or not 0 <= value['hold_seconds'] <= 10
            or value['hold_tick'] >= value['ticks'] or value['peer_timeout'] <= value['hold_seconds']
            or value['lifetime'] <= value['startup_timeout']):
        raise ValueError('invalid coordinator timing')
    if not isinstance(value['token'], str) or not re.fullmatch('[0-9a-f]{32}', value['token']):
        raise ValueError('invalid private token')
    return value


def auth_slot(data, token):
    if not isinstance(data, bytes) or len(data) > MAX_PACKET: return None
    try:
        fields = data.decode('ascii').split('|')
        if len(fields) < 4 or fields[1] != '1' or not hmac.compare_digest(fields[2], token): return None
        slot = number(fields[3], 2)
        return slot if slot in (1, 2) else None
    except (ValueError, UnicodeError): return None


def unpack_packet(message):
    exact_keys(message, ('kind', 'hex'), 'transport packet')
    encoded = message['hex']
    if (message['kind'] != 'packet' or not isinstance(encoded, str) or not encoded
            or len(encoded) > 2 * MAX_PACKET or len(encoded) % 2
            or not re.fullmatch('[0-9a-f]+', encoded)):
        raise ValueError('invalid bounded transport packet')
    return bytes.fromhex(encoded)


def emit(value):
    data = canonical_json(value)
    if len(data) > MAX_LINE: raise ValueError('oversized response')
    sys.stdout.buffer.write(data + b'\n'); sys.stdout.buffer.flush()


class CheckpointOutputError(Exception):
    """SSH output failed after disk persistence; not a disk-error subtype."""


class RemoteCoordinator:
    """All permits come from this one barrier, with durable evidence first."""
    def __init__(self, config, report, output, udp_send, started):
        self.config, self.report, self.output, self.udp_send = validate_config(config), report, output, udp_send
        self.sequence = 0
        self.checkpoint_output_failed = False
        self.first_linux_checkpoint = False
        self.barrier = PortableBarrier(config['token'], config['contract'], config['participants'],
            self.persist, ticks=config['ticks'], hold_tick=config['hold_tick'], hold_seconds=config['hold_seconds'],
            started=started, startup_timeout=config['startup_timeout'], peer_timeout=config['peer_timeout'])

    def persist_local(self, report):
        # Tick keys originate as Python integers. Normalize the exact JSON
        # representation before hashing: at tick10, integer sort order and
        # the receiver's decoded string-key sort order are otherwise different.
        # This same immutable value is written durably and sent over SSH.
        report = json.loads(canonical_json(report))
        if self.checkpoint_output_failed:
            report['ssh_checkpoint_output_failed'] = True
        atomic_report(self.report, report)
        if self.config.get('linux_first') is True:
            # The optional read-only startup fetch requires a private regular
            # report, not only a private containing directory. Before checkpoint.
            self.report.chmod(0o600)
        return report

    def persist(self, report):
        report = self.persist_local(report)
        self.sequence += 1
        try:
            self.output({'kind': 'checkpoint', 'sequence': self.sequence, 'report': report,
                         'sha256': hashlib.sha256(canonical_json(report)).hexdigest()})
        except (OSError, RuntimeError, ValueError) as error:
            # A failed pipe must abort before returning any grant, but it must
            # not overwrite an earlier cause with a fictitious disk failure.
            self.checkpoint_output_failed = True
            self.barrier.failure = self.barrier.failure or 'ssh_checkpoint_output_failed'
            self.barrier.stopping = True
            # Preserve both the first cause and this secondary output failure
            # without attempting the broken transport again. Real disk errors
            # here still reach PortableBarrier's strict persistence-failure path.
            self.persist_local(self.barrier.summary())
            raise CheckpointOutputError('ssh_checkpoint_output_failed') from error

    def dispatch(self, replies):
        # Remote persistence ran before PortableBarrier returned these replies.
        # Emit Windows before UDP: a broken SSH pipe cannot admit Linux alone.
        for address, data in replies:
            if address == WINDOWS:
                self.output({'kind': 'packet', 'sequence': self.sequence, 'hex': data.hex()})
        for address, data in replies:
            if address != WINDOWS:
                try: self.udp_send(data, address)
                except ConnectionResetError: pass

    def receive(self, data, address, now, channel):
        slot = auth_slot(data, self.config['token'])
        if slot is None: return
        if slot != (1 if channel == 'ssh' else 2) or (channel != 'ssh' and address == WINDOWS):
            self.dispatch(self.barrier.fail('transport_slot_route_mismatch')); return
        replies = self.barrier.receive(data, WINDOWS if channel == 'ssh' else address, now)
        barrier = self.barrier
        if (self.config.get('linux_first') is True and not self.first_linux_checkpoint
                and slot == 2 and set(barrier.hellos) == {2}
                and set(barrier.frames) == {0} and set(barrier.frames[0]) == {2}
                and barrier.released == -1 and not barrier.stopping and not barrier.failure):
            # Optional diagnostic evidence only: the independent native probe
            # must still prove process/module/socket/load/math ownership. No
            # grant exists until the ordinary two-peer barrier admits it.
            if not barrier.durable():
                replies = barrier.stop_all()
            else:
                self.first_linux_checkpoint = True
        self.dispatch(replies)

    def abort(self, reason):
        # Even when SSH output fails, persist locally and stop the Linux peer.
        try: replies = self.barrier.fail(reason)
        except (CheckpointOutputError, OSError): replies = self.barrier.stop_all()
        for address, data in replies:
            if address != WINDOWS:
                try: self.udp_send(data, address)
                except OSError: pass
        try: self.dispatch([(a, d) for a, d in replies if a == WINDOWS])
        except (BrokenPipeError, OSError): pass


def remote_main():
    if sys.platform != 'linux': raise RuntimeError('remote coordinator requires Linux')
    raw = sys.stdin.buffer.raw.readline(32769)
    if len(raw) > 32768 or not raw.endswith(b'\n'): raise ValueError('invalid initial config')
    config = validate_config(json.loads(raw))
    root = Path(__file__).resolve().parent / 'runs'
    root.mkdir(mode=0o700, exist_ok=True)
    if root.is_symlink() or root.stat().st_uid != os.getuid() or root.stat().st_mode & 0o077:
        raise ValueError('remote report directory is not private and owned')
    case = Path(tempfile.mkdtemp(prefix='portable-', dir=root))
    started = time.monotonic(); pending = bytearray(); terminal_at = None; cancelled = []
    for sig in (signal.SIGTERM, signal.SIGHUP): signal.signal(sig, lambda signum, _: cancelled.append(signum))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.bind(('127.0.0.1', 0)); udp.setblocking(False)
        session = RemoteCoordinator(config, case / 'portable-barrier.json', emit, udp.sendto, started)
        emit({'kind': 'ready', 'platform': 'linux', 'hashes': provenance(config['contract']), 'pid': os.getpid(),
              'start_ticks': int(Path('/proc/self/stat').read_text().rsplit(')', 1)[1].split()[19]),
              'port': udp.getsockname()[1], 'report_path': str(session.report)})
        try:
            while time.monotonic() - started <= config['lifetime']:
                now = time.monotonic()
                if cancelled: session.abort('remote_signal'); break
                readable, _, _ = select.select([sys.stdin.fileno(), udp], [], [], .05)
                if sys.stdin.fileno() in readable:
                    block = os.read(sys.stdin.fileno(), 65536)
                    if not block:
                        if not session.barrier.done: session.abort('ssh_input_eof')
                        break
                    pending.extend(block)
                    if len(pending) > MAX_PACKET * 4: raise ValueError('oversized transport input')
                    while b'\n' in pending:
                        line, _, remaining = pending.partition(b'\n'); pending = bytearray(remaining)
                        message = json.loads(line)
                        if message == {'kind': 'stop'}:
                            session.abort('bridge_requested_stop'); return 1
                        session.receive(unpack_packet(message), WINDOWS, now, 'ssh')
                if udp in readable:
                    try: data, address = udp.recvfrom(MAX_PACKET + 1)
                    except ConnectionResetError: continue
                    session.receive(data, address, now, 'udp')
                session.dispatch(session.barrier.poll(now))
                if session.barrier.done or session.barrier.failure:
                    if terminal_at is None: terminal_at = now
                    if now - terminal_at >= 2: break
            if not session.barrier.done and not session.barrier.failure:
                session.abort('remote_lifetime_expired')
            session.persist(session.barrier.summary())
        except CheckpointOutputError:
            session.abort('ssh_checkpoint_output_failed')
            return 1
        except (ValueError, KeyError, TypeError, OSError, RuntimeError):
            session.abort('remote_transport_failure')
            return 1
        return 0 if session.barrier.done and not session.barrier.failure else 1


def read_events(pipe, events):
    try:
        while True:
            data = pipe.readline(MAX_LINE + 1)
            if not data: break
            if len(data) > MAX_LINE or not data.endswith(b'\n'): raise ValueError('oversized remote line')
            message = json.loads(data)
            if not isinstance(message, dict): raise ValueError('invalid remote message')
            events.put(message, timeout=1)
    except (ValueError, OSError, queue.Full):
        try: events.put({'kind': 'transport_error'}, timeout=1)
        except queue.Full: pass
    finally:
        try: events.put({'kind': 'eof'}, timeout=1)
        except queue.Full: pass


class BridgeEvidence:
    def __init__(self, config, report_path):
        self.config, self.path = validate_config(config), report_path
        self.ready, self.report, self.sequence = None, None, 0

    def checkpoint(self, message):
        exact_keys(message, ('kind', 'sequence', 'report', 'sha256'), 'checkpoint')
        report = message['report']
        if (type(message['sequence']) is not int or message['sequence'] != self.sequence + 1
                or hashlib.sha256(canonical_json(report)).hexdigest() != message['sha256']
                or report.get('contract_sha256') != validate_contract(self.config['contract'])
                or report.get('participants') != self.config['participants']
                or report.get('ticks') != self.config['ticks']
                or report.get('native_processes_verified_by_coordinator') is not False):
            raise ValueError('remote checkpoint provenance mismatch')
        atomic_report(self.path, {'coordinator': report, 'remote': self.ready,
                                 'sequence': message['sequence'], 'coordinator_sha256': message['sha256'],
                                 'transport_complete': False})
        self.report, self.sequence = report, message['sequence']

    def packet(self, message):
        exact_keys(message, ('kind', 'sequence', 'hex'), 'coordinator reply')
        if not self.sequence or type(message['sequence']) is not int or message['sequence'] != self.sequence:
            raise ValueError('reply without durable checkpoint')
        data = unpack_packet({'kind': 'packet', 'hex': message['hex']})
        if auth_slot(data, self.config['token']) != 1: raise ValueError('invalid reply slot')
        fields = data.decode().split('|')
        if len(fields) < 5: raise ValueError('truncated coordinator reply')
        tick = number(fields[4], self.config['ticks'])
        if fields[0] == 'NGPGO':
            if fields[5:] != ['00000000', '00000000'] or tick < 1 or tick - 1 > self.report['released'] or self.report['failure']:
                raise ValueError('unrecorded or unsupported permit')
        elif fields[0] == 'NGPSTOP':
            if fields[5:] != ['1' if self.report['failure'] else '0']:
                raise ValueError('invalid terminal response')
        elif fields[0] == 'NGPCLOSED':
            if len(fields) != 5 or str(1) not in self.report['terminal'] or self.report['failure']:
                raise ValueError('unrecorded terminal receipt')
        else: raise ValueError('invalid coordinator reply')
        return data


def bridge(args, *, command=None):
    # Test callers may supply an owned subprocess; no CLI fallback exists.
    from native_gate_ssh import ssh_command
    config = validate_config(json.loads(args.config.read_text()))
    if args.report.exists() or not 1024 <= args.port <= 65535: raise ValueError('fresh report and bounded port required')
    process = subprocess.Popen(command or ssh_command(args.ssh_host, args.remote_script),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    events = queue.Queue(maxsize=32); errors = bytearray()
    reader = threading.Thread(target=read_events, args=(process.stdout, events), daemon=True); reader.start()
    def stderr():
        for block in iter(lambda: process.stderr.read(4096), b''):
            if len(errors) < 32768: errors.extend(block[:32768-len(errors)])
    error_reader = threading.Thread(target=stderr, daemon=True); error_reader.start()
    def send(value):
        process.stdin.write(canonical_json(value) + b'\n'); process.stdin.flush()
    evidence = BridgeEvidence(config, args.report)
    address = None; latest = 0; success = False; failure = None; started = time.monotonic()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(('127.0.0.1', args.port)); sock.setblocking(False); send(config)
        while time.monotonic() - started <= config['lifetime'] + 15:
            if errors: raise RuntimeError('SSH stderr is not empty')
            try: message = events.get(timeout=.01)
            except queue.Empty: message = None
            if message:
                kind = message.get('kind')
                if kind == 'ready':
                    exact_keys(message, ('kind', 'platform', 'hashes', 'pid', 'start_ticks', 'port', 'report_path'), 'ready')
                    if (evidence.ready or message['platform'] != 'linux' or message['hashes'] != provenance(config['contract'])
                            or type(message['pid']) is not int or message['pid'] <= 1
                            or type(message['start_ticks']) is not int or message['start_ticks'] <= 0
                            or type(message['port']) is not int or not 1024 <= message['port'] <= 65535
                            or not isinstance(message['report_path'], str) or not message['report_path'].startswith('/')):
                        raise ValueError('remote readiness provenance mismatch')
                    evidence.ready = message
                    atomic_report(args.report.with_name(args.report.stem + '-remote.json'), message)
                    print('PORTABLE_SSH READY native_identity_requires_runner=1', flush=True)
                elif not evidence.ready: raise RuntimeError('remote failed before verified readiness')
                elif kind == 'checkpoint': evidence.checkpoint(message)
                elif kind == 'packet':
                    if address is None: raise ValueError('reply before owned endpoint')
                    data = evidence.packet(message)
                    try: sock.sendto(data, address)
                    except ConnectionResetError: pass
                elif kind == 'eof':
                    process.wait(timeout=5); error_reader.join(timeout=2)
                    if (process.returncode or errors or evidence.report is None or evidence.report.get('verdict') != 'pass'
                            or set(evidence.report.get('terminal', {})) != {'1', '2'}):
                        raise RuntimeError('remote closed without complete success')
                    success = True; break
                else: raise RuntimeError('invalid remote transport event')
            if not evidence.ready:
                if time.monotonic() - started > 20: raise TimeoutError('remote readiness timeout')
                continue
            readable, _, _ = select.select([sock], [], [], 0)
            if readable:
                try: data, peer = sock.recvfrom(MAX_PACKET + 1)
                except ConnectionResetError: continue
                if peer[0] != '127.0.0.1' or auth_slot(data, config['token']) != 1: continue
                if address is None:
                    if not data.startswith(b'NGPHELLO|'): continue
                    address = peer
                if peer != address: continue
                if data.startswith(b'NGPSTATE|'):
                    fields = data.decode().split('|')
                    if len(fields) == 7 and len(fields[6]) >= 96:
                        try:
                            tick = int.from_bytes(bytes.fromhex(fields[6][80:96]), 'little')
                            if 0 <= tick <= config['ticks']: latest = max(latest, tick)
                        except ValueError: pass
                send({'kind': 'packet', 'hex': data.hex()})
        if not success: raise TimeoutError('bounded bridge lifetime expired')
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.TimeoutExpired) as error:
        failure = type(error).__name__ + ': ' + str(error)
    finally:
        if not success and address:
            for tick in {latest, min(latest + 1, config['ticks'])}:
                try: sock.sendto(f'NGPSTOP|1|{config["token"]}|1|{tick}|1'.encode(), address)
                except OSError: pass
        sock.close()
        if not success:
            try: send({'kind': 'stop'})
            except OSError: pass
        try: process.stdin.close()
        except OSError: pass
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=3)
        error_reader.join(timeout=1)
        if errors:
            success = False; failure = 'ssh_stderr_present'
            args.report.with_suffix('.ssh-stderr.txt').write_bytes(errors)
        if process.returncode != 0: success = False
        reader.join(timeout=1)
        process.stdout.close(); process.stderr.close()
        atomic_report(args.report, {'coordinator': evidence.report, 'remote': evidence.ready,
            'sequence': evidence.sequence, 'transport_complete': success, 'failure': failure,
            'ssh_exit_code': process.returncode, 'ssh_stderr_bytes': len(errors),
            'local_native_endpoint': {'transport': 'loopback_udp', 'address': list(address)} if address else None,
            'native_processes_verified_by_transport': False})
    if not success: return 1
    print('PORTABLE_SSH failure=none done=True transport_closed=True', flush=True)
    if args.runner_owned: time.sleep(30)  # Inert owned bridge; socket/SSH already closed.
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--remote', action='store_true')
    parser.add_argument('--ssh-host'); parser.add_argument('--remote-script')
    parser.add_argument('--config', type=Path); parser.add_argument('--report', type=Path)
    parser.add_argument('--port', type=int); parser.add_argument('--runner-owned', action='store_true')
    args = parser.parse_args()
    if args.remote: return remote_main()
    if not all((args.ssh_host, args.remote_script, args.config, args.report, args.port)):
        parser.error('bridge requires SSH destination/script, private config, report and loopback port')
    return bridge(args)


if __name__ == '__main__': raise SystemExit(main())
