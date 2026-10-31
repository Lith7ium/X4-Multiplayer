"""Live UDP comparison of the real relay processes: `python -m x4lan.host` vs `x4lan-relay`.

Both are started with the same command-line arguments (drop-in check), then two
synthetic UDP clients run identical scripted traffic against each. Leases are random
in both, so they are normalised to L1/L2 by order of first appearance and snapshot
tick counters to T. Direct replies must match exactly and in order; the periodic
snapshot packets are compared as the set of distinct normalised packets each client
receives in a quiet window after the script settles (which intermediate states a
20 Hz tick happens to sample depends on wall-clock timing).

Also measures startup latency (process start to first PONG) and checks that both
survive an oversize datagram.

  python src/rust/parity/live_parity.py --rust src/rust/target/release/x4lan-relay.exe
"""
import argparse
import json
from pathlib import Path
import socket
import statistics
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[3]
TOKEN = "a" * 32
REPLY_KINDS = {"PONG", "WELCOME", "RESET", "RESULT", "ORDER_RESULT", "PILOT_DENIED", "CDOCK_DENIED", "CARGOREPLY", "UACK"}
SECTOR = "cluster_01_sector001_macro"
BUILD = "world1-test"


def nonce(n):
    return f"{n:x}" * 32


class Client:
    def __init__(self, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.setblocking(False)
        self.target = ("127.0.0.1", port)

    def drain(self):
        out = []
        while True:
            try:
                out.append(self.sock.recv(4096))
            except BlockingIOError:
                return out
            except ConnectionResetError:
                continue  # Windows reports an earlier ICMP port-unreachable here


class Normaliser:
    def __init__(self):
        self.names = {}

    def learn(self, packet):
        f = packet.split("|")
        if f[0] == "WELCOME" and len(f) > 3 and f[3] not in self.names:
            self.names[f[3]] = f"L{len(self.names) + 1}"

    def __call__(self, packet):
        text = packet.decode("latin-1")
        self.learn(text)
        for lease, name in self.names.items():
            text = text.replace(lease, name)
        f = text.split("|")
        if f[0] in ("EMPTY", "PEER") and len(f) > 3:
            f[3] = "T"
        return "|".join(f)


def scripts(build):
    """(mode name, extra CLI args, session.json or None, steps). A step is (slot, text)
    with {L1}/{L2} lease placeholders, or ('wait', seconds)."""
    hello = lambda slot, n, addr_slot=None: (addr_slot or slot, f"HELLO|1|{TOKEN}|{build}|{SECTOR}|bubble|{slot}|{nonce(n)}")
    common = [
        ("p1", f"PING|1|{TOKEN}|{nonce(5)}"), ("p1", f"PING|1|{'b' * 32}|{nonce(5)}"),
        ("p1", f"RESUME|1|{TOKEN}|{build}|{SECTOR}|bubble|p1|{nonce(1)}|{'c' * 32}|{nonce(2)}"),
        hello("p1", 1), hello("p2", 2), hello("p2", 3, "p1"), hello("p1", 1),
        ("p1", "STATE|1|{L1}|1|10|20|30|0.1|0.2|0.3"), ("p2", "STATE|1|{L2}|1|-1|-2|-3|0|0|0"),
        ("p1", "STATE|1|{L1}|0|99|0|0|0|0|0"), ("p2", "STATE|1|{L1}|2|1|1|1|0|0|0"),
        ("wait", .2),
    ]
    world = "|".join(map(str, [100, 50, 3, 100, 50, 4, 80, 80, 90, 90, 7, 1, 0, 1, 0, 12, 13]))
    bubble = [
        ("p1", "WORLD|1|{L1}|1|" + world), ("p2", f"ACTION|1|{TOKEN}|p2|{'1' * 32}|TRADE|energycells"),
        ("p2", f"ACTION|1|{TOKEN}|p2|{'1' * 32}|TRADE|energycells"), ("p2", "BODY|1|{L2}|1|suit|0|3|1|2|3|0|0|0"),
        ("p2", "WALK|1|{L2}|1|2|1|2|3|0|0|0"), ("p1", "FIRE|1|{L1}|4"), ("wait", .15),
        ("p1", f"DONE|1|{{L1}}|{'1' * 32}|OK"), ("p2", f"ACTION|1|{TOKEN}|p2|{'1' * 32}|TRADE|energycells"), ("wait", .15),
    ]
    payload = "M,200000,10,20,30;W,energycells,10,5,6,100,200,10,12,500,11,13,600"
    economy = [
        ("p1", f"ECON|1|{{L1}}|1|2|2|{payload[40:]}"), ("p1", f"ECON|1|{{L1}}|1|1|2|{payload[:40]}"),
        ("p2", f"ORDER|1|{{L2}}|{'2' * 32}|{{L1}}|m1|energycells,-5,10"),
        ("p2", f"ORDER|1|{{L2}}|{'2' * 32}|{{L1}}|m1|energycells,-5,10"), ("p2", "DOCKSTATE|1|{L2}|1|m2"), ("wait", .6),
        ("p1", f"ORDER_DONE|1|{{L1}}|{'2' * 32}|OK"), ("wait", .1),
    ]
    coop = [
        ("p1", "CREW|1|{L1}|1|1|1"), ("p2", "CREW|1|{L2}|1|1|1"), ("p2", "CREW|1|{L2}|2|2|2"),
        ("p2", "NAV2|1|{L2}|2|1|1|" + SECTOR + "|1|2|3|0|0|0|0|1|50|1|2|3|0|0|0"),
        ("p2", "FIRE2|1|{L2}|2|1|1|50|1|2|3|3"), ("wait", .1), ("p2", "HACK|1|{L2}|2|2"),
        ("p2", "HEALTH|1|{L2}|2|2|1|75|20"), ("p1", "QUEST|1|{L1}|1|1|5|0|0"),
        ("p2", f"COOPORDER|1|{{L2}}|{'3' * 32}|{{L1}}|2|1|m1|energycells,-5,10"), ("wait", .3),
    ]
    rows = [f"{i},ship,ship_arg_s_fighter_01_a_macro,argon,1,2,3,0,0,0,none,0" for i in (3, 7)]
    digest = zlib.adler32("\n".join(rows).encode())
    universe = [
        ("p2", "UINTEREST|1|{L2}|1|" + SECTOR), ("p1", f"UBEGIN|1|{{L1}}|{{L1}}|1|{SECTOR}|2|{digest}"),
        ("p1", "UOBJECT|1|{L1}|{L1}|1|1|" + rows[0]), ("p1", "UOBJECT|1|{L1}|{L1}|1|2|" + rows[1]),
        ("p1", "UCOMMIT|1|{L1}|{L1}|1"), ("wait", .1), ("p2", "UACK|1|{L2}|{L1}|1|0"), ("wait", .1),
        ("p2", "UACK|1|{L2}|{L1}|1|1"), ("p2", "UACK|1|{L2}|{L1}|1|2"), ("wait", .1), ("p2", "UACK|1|{L2}|{L1}|1|3"),
        ("p2", f"CARGOREQ|1|{{L2}}|{'4' * 32}|{{L1}}|2|1|DROP|0|energycells|5"), ("wait", .3),
    ]
    yield "base", [], None, common
    yield "bubble (--bubble-actions)", ["--bubble-actions"], None, common + bubble
    yield "economy (--trading)", ["--trading"], None, common + bubble + economy
    yield "coop (session.json coop)", [], {"coop": True}, common + bubble + economy + coop
    yield "universe (session.json world_sync)", ["--trading"], {"coop": True, "world_sync": True}, common + bubble + coop + universe


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start(command, port, args, session, scratch):
    argv = list(command) + ["--port", str(port)] + args
    if session is not None:
        path = Path(scratch) / f"session-{port}.json"
        path.write_text(json.dumps(session))
        argv += ["--session", str(path)]
    else:
        argv += ["--token", TOKEN, "--build", BUILD, "--bubble", "bubble"]
    return subprocess.Popen(argv, cwd=ROOT / "src", stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def wait_ready(port, timeout=10.0):
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.settimeout(0.002)
    started = time.perf_counter()
    try:
        while time.perf_counter() - started < timeout:
            try:
                probe.sendto(f"PING|1|{TOKEN}|{nonce(9)}".encode(), ("127.0.0.1", port))
                if probe.recv(4096).startswith(b"PONG|"):
                    return time.perf_counter() - started
            except OSError:
                pass
        raise TimeoutError("relay did not answer PING")
    finally:
        probe.close()


def run_script(command, args, session, steps, build, scratch):
    port = free_port()
    if session is not None:
        session = {"token": TOKEN, "build": build, "bubble": "bubble", "port": port, **session}
    process = start(command, port, args, session, scratch)
    try:
        wait_ready(port)
        clients = {"p1": Client(port), "p2": Client(port)}
        norm = Normaliser()
        replies, snapshots = [], {"p1": set(), "p2": set()}

        def collect(window):
            end = time.perf_counter() + window
            step_replies = []
            while time.perf_counter() < end:
                for slot, client in clients.items():
                    for packet in client.drain():
                        text = norm(packet)
                        if text.split("|")[0] in REPLY_KINDS:
                            step_replies.append(f"{slot}<-{text}")
                        else:
                            snapshots[slot].add(text)
                time.sleep(0.002)
            return step_replies

        for slot, text in steps:
            if slot == "wait":
                replies.extend(collect(text))
                continue
            for lease, name in norm.names.items():
                text = text.replace("{" + name + "}", lease)
            clients[slot].sock.sendto(text.encode("latin-1"), clients[slot].target)
            replies.append(f"{slot}->{text.split('|')[0]}: " + " ; ".join(collect(0.03)))
        # Settled state: let in-flight ticks pass, then record one quiet window.
        replies.extend(collect(0.3))
        for seen in snapshots.values():
            seen.clear()
        replies.extend(collect(0.6))  # longer than the slowest periodic packet (0.5 s)
        # Oversize datagram, then a readiness probe.
        clients["p1"].sock.sendto(b"x" * 1000, clients["p1"].target)
        time.sleep(0.2)
        alive = process.poll() is None
        try:
            wait_ready(port, timeout=1.0)
            answered = True
        except TimeoutError:
            answered = False
        return replies, snapshots, {"alive_after_oversize": alive and answered,
                                    "stderr_tail": "" if alive else process.communicate()[1][-300:]}
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()


def startup(command, runs, scratch):
    times = []
    for _ in range(runs):
        port = free_port()
        started = time.perf_counter()
        process = start(command, port, [], None, scratch)
        try:
            wait_ready(port)
            times.append(time.perf_counter() - started)
        finally:
            process.kill()
            process.wait()
    return times


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rust", required=True)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--startup-runs", type=int, default=10)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    relays = {"python": [args.python, "-m", "x4lan.host"], "rust": [str(Path(args.rust).resolve())]}
    report = {"scripts": {}, "startup_seconds": {}}
    failures = 0
    with tempfile.TemporaryDirectory(prefix="x4lan-live-") as scratch:
        for name, extra, session, steps in scripts(BUILD):
            results = {k: run_script(cmd, extra, session, steps, BUILD, scratch) for k, cmd in relays.items()}
            (py_r, py_s, py_x), (rs_r, rs_s, rs_x) = results["python"], results["rust"]
            same_replies = py_r == rs_r
            same_snaps = py_s == rs_s
            failures += not (same_replies and same_snaps)
            report["scripts"][name] = {"steps": len(steps), "reply_lines": len(py_r), "distinct_snapshots":
                                       sum(len(v) for v in py_s.values()), "replies_identical": same_replies,
                                       "snapshot_sets_identical": same_snaps, "python": py_x, "rust": rs_x}
            print(f"{name}: replies {'identical' if same_replies else 'DIFFERENT'} ({len(py_r)} lines), "
                  f"snapshot sets {'identical' if same_snaps else 'DIFFERENT'} "
                  f"({sum(len(v) for v in py_s.values())} distinct); survives 1000-byte datagram: "
                  f"python={py_x['alive_after_oversize']} rust={rs_x['alive_after_oversize']}")
            if not same_replies:
                for a, b in zip(py_r, rs_r):
                    if a != b:
                        print("   python:", a[:300], "\n   rust:  ", b[:300])
                        break
            if not same_snaps:
                for slot in ("p1", "p2"):
                    print(f"   {slot} only python:", sorted(py_s[slot] - rs_s[slot])[:3])
                    print(f"   {slot} only rust:  ", sorted(rs_s[slot] - py_s[slot])[:3])
        for k, cmd in relays.items():
            times = startup(cmd, args.startup_runs, scratch)
            report["startup_seconds"][k] = {"median": statistics.median(times), "min": min(times), "max": max(times)}
            print(f"startup to first PONG, {k}: median {statistics.median(times) * 1000:.1f} ms "
                  f"(min {min(times) * 1000:.1f}, max {max(times) * 1000:.1f}, n={len(times)})")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
