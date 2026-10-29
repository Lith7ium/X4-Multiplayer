"""Send a bounded bubble action, retaining its ID across timeouts/retries."""

from pathlib import Path
import argparse, json, secrets, socket, time

ROOT = Path(__file__).resolve().parent


def run(verb):
    cfg = json.loads((ROOT / 'session.json').read_text())
    package = json.loads((ROOT / 'package.json').read_text())
    pending = ROOT / 'pending-action.json'
    if pending.exists():
        action = json.loads(pending.read_text())
        if action['verb'] != verb:
            raise RuntimeError('Another action is pending. Retry that same command first.')
    else:
        action = {'request': secrets.token_hex(16), 'verb': verb}
        pending.write_text(json.dumps(action))
    request = action['request']
    packet = f"ACTION|1|{cfg['token']}|{package['slot']}|{request}|{verb}|energycells".encode()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect((cfg.get('host', '127.0.0.1'), cfg['port']))
        sock.settimeout(0.8)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            sock.send(packet)
            try:
                response_fields = sock.recv(769).decode('ascii').split('|')
            except (socket.timeout, ConnectionRefusedError, ConnectionResetError):
                continue
            if len(response_fields) != 4 or response_fields[:3] != ['RESULT', '1', request]:
                continue
            if response_fields[3] == 'PENDING':
                time.sleep(0.2)
                continue
            pending.unlink()
            messages = {
                'OK': 'Confirmed: cargo action completed in the host game.',
                'REJECTED': 'Rejected: check distance, container, cargo capacity or dock.',
                'BUSY': 'Station busy. Try again later.',
                'CANCELLED': 'Connection interrupted. Check cargo; if uncertain, restart both test games.',
                'NO_WORLD': 'The host test game is not ready yet.',
                'NO_PLAYER': 'Your game is not connected yet.',
                'SESSION_FULL': 'Test limit reached. Restart both games and the server.',
            }
            print(messages.get(response_fields[3], response_fields[3]))
            return 0 if response_fields[3] == 'OK' else 1
    print(
        'No final reply yet. Request saved. Retry the same command; its request ID will be reused.'
    )
    return 2


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('verb', choices=['PICKUP', 'TRADE'])
    a = p.parse_args()
    try:
        raise SystemExit(run(a.verb))
    except (OSError, ValueError, RuntimeError) as e:
        print('ERROR:', e)
        raise SystemExit(1)
