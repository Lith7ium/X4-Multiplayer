"""Carry loopback native gate datagrams to a separate Linux coordinator over SSH.

This is a bounded development transport, not native Linux X4 admission. The
original Barrier alone decides permits; the Windows bridge never grants a tick.
No public UDP listener, deployed service, native executable or game profile changes.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import queue
import re
import secrets
import select
import shlex
import shutil
import socket
import subprocess
import sys
import threading
import time

from native_gate_relay import Barrier


MAX_LINE = 32 * 1024 * 1024


def provenance():
    root = Path(__file__).resolve().parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in ('native_gate_ssh.py', 'native_gate_relay.py')}


def emit(value):
    sys.stdout.write(json.dumps(value, separators=(',', ':')) + '\n')
    sys.stdout.flush()


def remote_config(value):
    # tick_hz is optional: a config without it means 60 (native_gate_tick_hz).
    if not isinstance(value, dict) or set(value)-{'tick_hz'} != {'token', 'ticks', 'hold_tick', 'hold_seconds', 'relative_real', 'lifetime'}:
        raise ValueError('invalid coordinator configuration')
    if any(type(value[name]) is not int for name in ('ticks', 'hold_tick', 'lifetime')) or not 1 <= value['ticks'] <= 18000 or not 1 <= value['lifetime'] <= 3600:
        raise ValueError('invalid coordinator bounds')
    if type(value['relative_real']) is not bool or not isinstance(value['hold_seconds'], (int, float)) or not math.isfinite(value['hold_seconds']) or not 0 <= value['hold_seconds'] <= 60:
        raise ValueError('invalid coordinator timing')
    return Barrier(value['token'], value['ticks'], value['hold_tick'], value['hold_seconds'], relative_real=value['relative_real'],
                   tick_hz=value.get('tick_hz', 60))


def remote_packet(message):
    if not isinstance(message, dict) or set(message) != {'kind', 'peer', 'hex'} or message['kind'] != 'packet':
        raise ValueError('invalid transport envelope')
    peer, data = message['peer'], message['hex']
    if type(peer) is not int or not 1 <= peer <= 32 or not isinstance(data, str) or len(data) > 1024 or len(data) % 2 or not re.fullmatch('[0-9a-f]*', data):
        raise ValueError('invalid transport packet')
    return peer, bytes.fromhex(data)


def remote_main():
    if platform.system() != 'Linux':
        raise RuntimeError('remote coordinator requires Linux')
    raw = sys.stdin.buffer.raw.readline(4096)
    config = json.loads(raw)
    barrier = remote_config(config)
    started = time.monotonic()
    output = Path(__file__).resolve().parent/'runs'/secrets.token_hex(12)
    output.mkdir(parents=True, mode=0o700)
    emit({'kind': 'ready', 'platform': platform.platform(), 'hashes': provenance(), 'pid':os.getpid(),'report_path':str(output/'native-barrier.json')})
    status = None
    terminal_until = None
    checkpointed = False
    pending = bytearray()
    def checkpoint():
        summary=barrier.summary()
        atomic_report(output/'native-barrier.json',summary)
        emit({'kind':'checkpoint','summary':summary})
    try:
        while time.monotonic() - started < config['lifetime']:
            if terminal_until is not None and time.monotonic() >= terminal_until:break
            if b'\n' not in pending:
                ready, _, _ = select.select([sys.stdin], [], [], .25)
                if not ready:continue
                raw=os.read(sys.stdin.fileno(),4096)
                if not raw:break
                pending.extend(raw)
                if len(pending)>8192:raise ValueError('oversized transport input')
                if b'\n' not in pending:continue
            raw, _, rest=pending.partition(b'\n');pending=bytearray(rest)
            message = json.loads(raw)
            if message == {'kind': 'stop'}:
                break
            peer, data = remote_packet(message)
            responses = barrier.receive(data, ('127.0.0.1', 20000 + peer), time.monotonic())
            command_release=any(packet.startswith(b'NGGO|2|') for _,packet in responses) and any(
                row.get('command','00000000')!='00000000' for row in barrier.states.get(barrier.released,{}).values())
            if command_release or ((barrier.failed or barrier.done) and not checkpointed):
                # Persist on Linux and send the checkpoint before any permit;
                # Windows persists it before forwarding that permit to X4.
                checkpoint()
                if barrier.failed or barrier.done:checkpointed=True
            if responses:
                emit({'kind': 'datagrams', 'rows': [[address[1] - 20000, packet.hex()] for address, packet in responses]})
            current = (barrier.released, barrier.failed, barrier.done)
            if (barrier.failed or barrier.done) and terminal_until is None:
                # Briefly retain the coordinator for final STOP retransmission,
                # then close SSH before the runner is allowed to claim success.
                terminal_until = time.monotonic()+2
            if current != status:
                emit({'kind': 'status', 'released': current[0], 'failed': current[1], 'done': current[2]})
                status = current
    finally:
        if not checkpointed:
            checkpoint()


def lines(pipe, events):
    try:
        while True:
            raw = pipe.readline(MAX_LINE + 1)
            if not raw:
                break
            if len(raw) > MAX_LINE:
                raise ValueError('oversized SSH coordinator response')
            events.put(json.loads(raw))
    except Exception as error:
        events.put({'kind': 'transport_error', 'error': type(error).__name__})
    finally:
        events.put({'kind': 'eof'})


def atomic_report(path, value):
    path = Path(path)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def ssh_command(host,script):
    if host.startswith('-') or not re.fullmatch(r'[A-Za-z0-9_.@-]+',host) or not script.startswith('/') or not re.fullmatch(r'[A-Za-z0-9_./-]+',script):
        raise ValueError('invalid SSH destination or remote script path')
    exe=shutil.which('ssh')
    if not exe:raise ValueError('OpenSSH executable unavailable')
    remote='python3 '+shlex.quote(script)+' --remote'
    options=('BatchMode=yes','ConnectTimeout=12','ServerAliveInterval=5','ServerAliveCountMax=3',
             'ControlMaster=no','ControlPath=none','ControlPersist=no','ProxyCommand=none','ProxyJump=none',
             'PermitLocalCommand=no','ClearAllForwardings=yes','ForwardAgent=no','ForwardX11=no',
             'ForwardX11Trusted=no','Tunnel=no','ExitOnForwardFailure=yes','RemoteCommand=none')
    command=[str(Path(exe).resolve()),'-T','-p','22']
    for option in options:command+=['-o',option]
    return command+[host,remote]


def bridge(args):
    command=ssh_command(args.ssh_host,args.remote_script)
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    events = queue.Queue()
    threading.Thread(target=lines, args=(process.stdout, events), daemon=True).start()
    errors = bytearray()
    def drain_errors():
        for block in iter(lambda: process.stderr.read(4096), b''):
            if len(errors) < 32768:
                errors.extend(block[:32768-len(errors)])
    threading.Thread(target=drain_errors, daemon=True).start()
    def send(message):
        process.stdin.write((json.dumps(message, separators=(',', ':')) + '\n').encode())
        process.stdin.flush()
    started = time.monotonic()
    config = dict(token=args.token, ticks=args.ticks, hold_tick=args.hold_tick, hold_seconds=args.hold_seconds,
                  relative_real=args.relative_real, lifetime=args.lifetime, **({'tick_hz': args.tick_hz} if args.tick_hz != 60 else {}))
    remote_config(config)  # Same bounds before transmitting credentials.
    send(config)
    ready = False
    peers, addresses, last_packets = {}, {}, {}
    report = None
    provenance_record = None
    traffic = {'input_datagrams': 0, 'output_datagrams': 0, 'bytes_to_linux': 0, 'bytes_from_linux': 0}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', args.port));sock.setblocking(False)
    try:
        while time.monotonic() - started < args.lifetime:
            while True:
                try:
                    message = events.get_nowait()
                except queue.Empty:
                    break
                kind = message.get('kind')
                if kind == 'ready':
                    if ready or message.get('hashes') != provenance() or not message.get('platform', '').startswith('Linux'):
                        raise ValueError('SSH coordinator provenance mismatch')
                    ready = True;provenance_record = message
                    atomic_report(args.report.with_name('ssh-coordinator.json'),message)
                    print('NATIVE_GATE RELAY READY transport=ssh coordinator=Linux', flush=True)
                elif not ready:
                    raise RuntimeError('SSH coordinator failed before verified readiness')
                elif kind == 'datagrams':
                    for peer, encoded in message['rows']:
                        if peer not in addresses or not isinstance(encoded, str) or len(encoded) > 1024:
                            raise ValueError('invalid coordinator response destination')
                        packet = bytes.fromhex(encoded)
                        try:
                            sock.sendto(packet, addresses[peer])
                        except ConnectionResetError:
                            continue
                        traffic['output_datagrams'] += 1;traffic['bytes_from_linux'] += len(packet)
                elif kind == 'status':
                    # done=True is the runner's stop trigger. Publish it only
                    # after finally has persisted a successful SSH exit.
                    print(f'NATIVE_GATE tick={message["released"]} failure={message["failed"] or "none"} done=False transport_draining={message["done"]}', flush=True)
                elif kind == 'checkpoint':
                    report = message['summary']
                    report['transport'] = {'kind': 'ssh_byte_preserving', 'coordinator': provenance_record, 'traffic': dict(traffic), 'native_linux_simulation': False}
                    atomic_report(args.report, report)
                elif kind == 'eof':
                    if report is None or report.get('verdict')=='incomplete':
                        raise RuntimeError('SSH coordinator disconnected before evidence checkpoint')
                    return report
                else:
                    raise RuntimeError('invalid SSH coordinator response')
            if not ready:
                if time.monotonic() - started > 20:
                    raise TimeoutError('SSH coordinator readiness timeout')
                time.sleep(.02);continue
            available, _, _ = select.select([sock], [], [], .01)
            if available:
                try:
                    packet, address = sock.recvfrom(1024)
                except ConnectionResetError:
                    continue
                if address[0] != '127.0.0.1' or len(packet) > 512:
                    continue
                if address not in peers:
                    if len(peers) >= 32:
                        continue
                    peers[address] = len(peers)+1;addresses[peers[address]] = address
                send({'kind': 'packet', 'peer': peers[address], 'hex': packet.hex()})
                fields=packet.decode('ascii','replace').split('|')
                if len(fields)==9 and fields[:3]==['NGSTATE','2',args.token] and fields[3] in ('1','2') and fields[4].isdecimal():
                    last_packets[address]=(fields[3],int(fields[4]))
                traffic['input_datagrams'] += 1;traffic['bytes_to_linux'] += len(packet)
        raise TimeoutError('bounded SSH coordinator lifetime expired')
    finally:
        if report is None or report.get('verdict')=='incomplete':
            # Abort can stop but never grant a tick. Cover the latest report and
            # its one possible in-flight permit; X4 accepts only its exact tick.
            for address,(slot,tick) in last_packets.items():
                for stop_tick in (tick,tick+1):
                    try:sock.sendto(f'NGSTOP|2|{args.token}|{slot}|{stop_tick}'.encode(),address)
                    except OSError:pass
        sock.close()
        try:
            send({'kind': 'stop'})
        except (BrokenPipeError, OSError):
            pass
        try:
            process.stdin.close()
        except OSError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill();process.wait()
        while not events.empty():
            message=events.get_nowait()
            if message.get('kind')=='checkpoint':
                report=message['summary']
                report['transport']={'kind':'ssh_byte_preserving','coordinator':provenance_record,'native_linux_simulation':False}
        if report is not None:
            report['transport']['traffic'] = dict(traffic)
            report['transport']['ssh_exit_code'] = process.returncode
            if report.get('verdict')=='incomplete':report['transport']['aborted']=True
            atomic_report(args.report, report)
        if errors:
            args.report.with_suffix('.ssh-stderr.txt').write_bytes(errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--remote', action='store_true')
    parser.add_argument('--ssh-host')
    parser.add_argument('--remote-script')
    parser.add_argument('--port', type=int)
    parser.add_argument('--token')
    parser.add_argument('--ticks', type=int, default=120)
    parser.add_argument('--hold-tick', type=int, default=10)
    parser.add_argument('--hold-seconds', type=float, default=3)
    parser.add_argument('--relative-real', action='store_true')
    parser.add_argument('--lifetime', type=int, default=1200)
    parser.add_argument('--tick-hz', type=int, default=60, choices=(60, 40, 30, 20))
    parser.add_argument('--report', type=Path)
    parser.add_argument('--runner-owned', action='store_true', help='keep completed bridge alive briefly for the owning lane job cleanup')
    args = parser.parse_args()
    if args.remote:
        remote_main()
    else:
        if not args.ssh_host or not args.remote_script or not args.report or not args.port or not args.token:
            parser.error('bridge requires SSH destination/script, loopback port, token and report')
        result=bridge(args)
        if result.get('verdict')=='pass':
            if result['transport'].get('ssh_exit_code')!=0:
                raise RuntimeError('SSH coordinator did not exit cleanly')
            print('NATIVE_GATE failure=none done=True transport_closed=True',flush=True)
            if args.runner_owned:
                # The released runner treats any server-process exit during a
                # session as failure, even code0. SSH is already closed and its
                # result durable; only this inert bridge awaits owned cleanup.
                time.sleep(30)


if __name__ == '__main__':
    main()
