"""Synthetic flying peer for an explicitly labelled ONE-game integration test.

This is a trajectory generator, not a second X4 client. It cannot prove multiplayer.
"""

import argparse
import json
import math
import secrets
import socket
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--slot", choices=("p1", "p2"), default="p2")
    parser.add_argument("--sector", required=True, help="sector macro from [X4LAN] telemetry log")
    args = parser.parse_args()
    with open(args.session, encoding="utf-8") as stream:
        cfg = json.load(stream)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect((args.host, cfg["port"]))
    sock.setblocking(False)
    started = last_host = time.monotonic()
    last_hello = last_send = last_print = -math.inf
    nonce, lease, seq = secrets.token_hex(16), None, 0
    print("SYNTHETIC TEST PEER. Movement below is generated, not read from X4.", flush=True)
    try:
        while True:
            now = time.monotonic()
            if lease and now - last_host > 3:
                nonce, lease, seq = secrets.token_hex(16), None, 0
            if now - last_hello >= 1:
                msg = f"HELLO|1|{cfg['token']}|{cfg['build']}|{args.sector}|{cfg['bubble']}|{args.slot}|{nonce}"
                try:
                    sock.send(msg.encode("ascii"))
                except OSError:
                    pass
                last_hello = now
            for _ in range(32):
                try:
                    raw = sock.recv(769)
                except (BlockingIOError, ConnectionRefusedError, ConnectionResetError):
                    break
                fields = raw.decode("ascii", errors="replace").split("|")
                if len(fields) == 5 and fields[:3] == ["WELCOME", "1", nonce]:
                    if lease is None:
                        lease = fields[3]
                        print("Connected to relay", flush=True)
                    if lease == fields[3]:
                        last_host = now
                if (
                    lease
                    and len(fields) in (4, 12)
                    and fields[0] in ("EMPTY", "PEER")
                    and fields[1:3] == ["1", lease]
                ):
                    last_host = now
                    if fields[0] == "PEER" and now - last_print >= 2:
                        print("X4 pose received: " + ", ".join(fields[6:9]), flush=True)
                        last_print = now
            if lease and now - last_send >= 0.05:
                seq += 1
                t = (now - started) * 0.15
                slot_x = -1000 if args.slot == "p1" else 1000
                pose = (
                    slot_x + 300 * math.cos(t),
                    100 * math.sin(t * 0.7),
                    300 * math.sin(t),
                    ((-t + math.pi) % math.tau) - math.pi,
                    0.2 * math.sin(t * 0.8),
                    0.3 * math.sin(t * 0.6),
                )
                msg = f"STATE|1|{lease}|{seq}|" + "|".join(f"{x:.9g}" for x in pose)
                try:
                    sock.send(msg.encode("ascii"))
                except OSError:
                    pass
                last_send = now
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
