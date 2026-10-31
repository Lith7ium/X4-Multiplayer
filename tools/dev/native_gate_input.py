"""Submit a tactical command to one client in an owned live native-gate run."""
import argparse
from pathlib import Path
import re
import socket
import time


def submit(run, slot, sequence, command, timeout=15):
    if slot not in (1, 2) or not 1 <= sequence <= 4096 or not re.fullmatch('[0-9a-f]{8}', command) or command == '00000000':
        raise ValueError('invalid slot, sequence or nonzero command')
    config = (run/f'client{slot}/installed/x4_lan_lab-config.lua').read_text()
    if 'native_gate_command_plan = "live"' not in config:
        raise ValueError('run is not using live input')
    tokens = re.findall(r'X4LAN_CONFIG\.token\s*=\s*"([0-9a-f]{32})"', config)
    ports = re.findall(r'X4LAN_CONFIG\.port\s*=\s*(\d+)', config)
    if len(tokens) != 1 or len(ports) != 1:
        raise ValueError('unique native run endpoint missing')
    token, port = tokens[0], int(ports[0])
    packet = f'NGCMD|1|{token}|{slot}|{sequence}|{command}'.encode()
    prefix = f'NGCAPTURED|1|{token}|{slot}|{sequence}|'
    deadline = time.monotonic()+timeout
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(('127.0.0.1', port))
        sock.settimeout(.1)
        while time.monotonic() < deadline:
            sock.send(packet)
            try:
                reply = sock.recv(256).decode('ascii')
            except socket.timeout:
                continue
            if reply.startswith(prefix) and reply[len(prefix):].isdecimal():
                return int(reply[len(prefix):])
            # A queued acknowledgement is not evidence of client capture.
            time.sleep(.05)
    raise TimeoutError('command not captured; queue may be full, sequence conflicted, or game stopped')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run', type=Path, required=True)
    p.add_argument('--slot', type=int, choices=(1,2), required=True)
    p.add_argument('--sequence', type=int, required=True)
    p.add_argument('--command', required=True, help='8 hex digits: opcode/actor/argument/00; 1 Attack,2 Wait,3 MoveWait,4 ProtectShip,5 travel-enabled distant MoveWait,6 AssignCommander,7 detach,8 withdraw using native boost,9 DockAndWait at own disposable station')
    args=p.parse_args()
    tick=submit(args.run,args.slot,args.sequence,args.command)
    print(f'Client {args.slot} captured input {args.sequence} at native frame {tick}; barrier admission and native application require run validation.')


if __name__ == '__main__':
    main()
