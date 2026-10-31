"""Python side of the replay protocol (see src/rust/x4lan-relay/src/bin/x4lan-replay.rs).

Drives the unmodified x4lan session classes from JSON lines on stdin. The only
change to the relay is deterministic leases: ``secrets.token_hex`` returns
``f"{n:032x}"`` for n = 1, 2, ... per session, exactly like the Rust driver.
An exception escaping the Python relay is reported as {"exception": ...}; the
real relay would stop on it.
"""
import json
from pathlib import Path
import secrets
import struct
import sys
import traceback

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))

from x4lan import host, bubble, economy, coop, docking, universe  # noqa: E402

COUNTER = [0]


def token_hex(nbytes=16):
    COUNTER[0] += 1
    return f"{COUNTER[0]:0{2 * nbytes}x}"


secrets.token_hex = token_hex

LEVELS = {
    "base": host.Session,
    "bubble": bubble.BubbleSession,
    "economy": economy.EconomySession,
    "coop": coop.CoopSession,
    "dock": docking.DockSession,
    "universe": universe.UniverseSession,
}


def address(text):
    ip, port = text.rsplit(":", 1)
    return ip, int(port)


def handle(session, cmd):
    op = cmd["op"]
    if op == "new":
        COUNTER[0] = 0
        try:
            return LEVELS[cmd["level"]](cmd["token"], cmd["build"], cmd["bubble"]), {"ok": True}
        except ValueError as exc:
            return None, {"error": str(exc)}
    try:
        if op == "recv":
            out = session.receive(bytes.fromhex(cmd["hex"]), address(cmd["addr"]), cmd["t"])
            return session, {"out": [p.hex() for p in out]}
        if op == "snap":
            # The relay loop bundles every tick for XBCAP peers (host.Relay.step).
            out = host.bundle(session.snapshots(cmd["t"]), session.peers)
            return session, {"out": [[f"{a[0]}:{a[1]}", p.hex()] for a, p in out]}
        if op == "expire":
            session.expire(cmd["t"])
            return session, {}
    except Exception as exc:  # the real relay loop would stop here
        return session, {"exception": f"{type(exc).__name__}: {exc}",
                         "where": traceback.extract_tb(exc.__traceback__)[-1][:3]}
    if op == "float":
        try:
            return session, {"v": struct.pack(">d", float(cmd["text"])).hex()}
        except ValueError:
            return session, {"v": None}
    if op == "fmt":
        x = struct.unpack(">d", bytes.fromhex(cmd["bits"]))[0] if "bits" in cmd else cmd["x"]
        return session, {"g9": format(x, ".9g"), "g17": "%.17g" % x, "f3": f"{x:.3f}"}
    raise ValueError(op)


def main():
    session = None
    out = sys.stdout
    for line in sys.stdin:
        if not line.strip():
            continue
        session, reply = handle(session, json.loads(line))
        out.write(json.dumps(reply) + "\n")
    out.flush()


if __name__ == "__main__":
    main()
