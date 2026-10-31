"""Record the relay traffic of the existing Python test suite as replay scenarios.

Runs tests/test_*.py with the x4lan session classes instrumented. Every session a test
creates becomes one scenario: its constructor arguments and each outermost
receive/snapshots/expire call with the arguments and the Python result. Leases are made
deterministic per session (same scheme as py_driver.py) so the scenario can be replayed.

Tests also reach into session state directly (assigning ``s.world``, editing peers,
calling helpers). Such a trace cannot be replayed from packets alone; relay_parity.py
detects that by replaying it into a fresh Python session first and only compares Rust
on traces that Python itself reproduces.

  python src/rust/parity/record_tests.py --out src/rust/target/parity/test-traces.jsonl
"""
import argparse
import json
from pathlib import Path
import secrets
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "src"), str(ROOT / "tools")]

from x4lan import host, bubble, economy, coop, docking, universe, cargo, identity, npc_combat, missions  # noqa: E402

LEVEL = {host.Session: "base", bubble.BubbleSession: "bubble", economy.EconomySession: "economy",
         coop.CoopSession: "coop", docking.DockSession: "dock", universe.UniverseSession: "universe"}
SESSIONS = list(LEVEL)
TRACES = []           # every recorded session, in creation order
STATE = {"current": None, "test": None}


class Trace:
    def __init__(self, session, args):
        self.level = LEVEL.get(type(session))
        self.commands = [{"op": "new", "level": self.level, "token": args[0], "build": args[1], "bubble": args[2]}]
        self.results = [{"ok": True}]
        self.counter = 0
        self.depth = 0
        self.test = STATE["test"]
        self.broken = None


def token_hex(nbytes=16):
    trace = STATE["current"]
    if trace is None:
        return original_token_hex(nbytes)
    trace.counter += 1
    return f"{trace.counter:0{2 * nbytes}x}"


original_token_hex = secrets.token_hex
secrets.token_hex = token_hex


def addr(a):
    return f"{a[0]}:{a[1]}"


def wrap_init(cls):
    original = cls.__init__

    def __init__(self, *args, **kwargs):
        outer = "_x4trace" not in self.__dict__
        if outer:
            self._x4trace = Trace(self, args)
        original(self, *args, **kwargs)
        if outer and self._x4trace.level and type(self) in LEVEL:
            TRACES.append(self._x4trace)
    cls.__init__ = __init__


def record(trace, command, call):
    """Run call; record it if it is the outermost relay entry of this session."""
    if trace is None or trace.depth:
        if trace is not None:
            trace.depth += 1
        try:
            return call()
        finally:
            if trace is not None:
                trace.depth -= 1
    previous, STATE["current"] = STATE["current"], trace
    trace.depth += 1
    try:
        result = call()
    except Exception as exc:
        trace.commands.append(command)
        trace.results.append({"exception": f"{type(exc).__name__}: {exc}"})
        raise
    finally:
        trace.depth -= 1
        STATE["current"] = previous
    trace.commands.append(command)
    if command["op"] == "recv":
        trace.results.append({"out": [p.hex() for p in result]})
    elif command["op"] == "snap":
        trace.results.append({"out": [[addr(a), p.hex()] for a, p in result]})
    else:
        trace.results.append({})
    return result


def wrap_session(cls):
    d = cls.__dict__
    if "receive" in d:
        original_receive = d["receive"]

        def receive(self, raw, address, now):
            cmd = {"op": "recv", "t": now, "addr": addr(address), "hex": bytes(raw).hex()}
            return record(self.__dict__.get("_x4trace"), cmd, lambda: original_receive(self, raw, address, now))
        cls.receive = receive
    if "snapshots" in d:
        original_snapshots = d["snapshots"]

        def snapshots(self, now):
            return record(self.__dict__.get("_x4trace"), {"op": "snap", "t": now}, lambda: original_snapshots(self, now))
        cls.snapshots = snapshots
    if "expire" in d:
        original_expire = d["expire"]

        def expire(self, now):
            return record(self.__dict__.get("_x4trace"), {"op": "expire", "t": now}, lambda: original_expire(self, now))
        cls.expire = expire


def wrap_component(cls):
    """Components dispatched by the session (tests sometimes call them directly)."""
    original_receive = cls.receive

    def receive(self, raw, address, now):
        cmd = {"op": "recv", "t": now, "addr": addr(address), "hex": bytes(raw).hex()}
        return record(self.session.__dict__.get("_x4trace"), cmd, lambda: original_receive(self, raw, address, now))
    cls.receive = receive


for cls in SESSIONS:
    wrap_init(cls)
    wrap_session(cls)
for cls in (cargo.CargoTransactions, identity.Identities, npc_combat.NpcCombat, missions.MissionJournal):
    wrap_component(cls)


class Result(unittest.TextTestResult):
    def startTest(self, test):
        STATE["test"] = test.id()
        super().startTest(test)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--pattern", default="test_*.py")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"), pattern=args.pattern)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.with_suffix(".log").open("w") as log:
        result = unittest.TextTestRunner(resultclass=Result, verbosity=2, stream=log).run(suite)
    with args.out.open("w") as stream:
        for i, trace in enumerate(TRACES):
            if len(trace.commands) < 2:
                continue  # constructed but never exercised
            stream.write(json.dumps({"name": f"{trace.test}#{i}", "level": trace.level,
                                     "commands": trace.commands, "results": trace.results}) + "\n")
    print(f"tests run: {result.testsRun}, failures: {len(result.failures)}, errors: {len(result.errors)}, "
          f"skipped: {len(result.skipped)}; recorded sessions: {sum(len(t.commands) > 1 for t in TRACES)}")


if __name__ == "__main__":
    main()
