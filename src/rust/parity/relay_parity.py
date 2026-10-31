"""Differential parity: the Python relay vs the Rust relay on identical traffic.

Each scenario is a list of replay commands (new session, receive datagram at time t
from an address, take a snapshot tick, expire). Both implementations run every
scenario through the same JSON-lines protocol with deterministic leases; every reply
and every snapshot packet must be byte-identical, in order.

Scenario sources:
  * --traces: sessions recorded from the existing Python test suite (record_tests.py).
    A trace is compared only if a fresh Python session reproduces the recorded test
    results (tests that poke internal state cannot be replayed from packets).
  * --generated: hand-written protocol scenarios and seeded randomized/mutated traffic
    (scenarios.py).

  python src/rust/parity/relay_parity.py --rust src/rust/target/release/x4lan-replay.exe \
      --traces src/rust/target/parity/test-traces.jsonl --generated 2000 --report src/rust/target/parity/report.json
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import scenarios  # noqa: E402


def run(command, scenario_list):
    """Feed all scenarios to one driver process; return per-scenario result lists."""
    lines = [json.dumps(c) for s in scenario_list for c in s["commands"]]
    started = time.perf_counter()
    done = subprocess.run(command, input="\n".join(lines) + "\n", capture_output=True, text=True, check=False)
    elapsed = time.perf_counter() - started
    if done.returncode:
        raise RuntimeError(f"{command[0]} failed: {done.stderr[-2000:]}")
    replies = [json.loads(line) for line in done.stdout.splitlines()]
    if len(replies) != len(lines):
        raise RuntimeError(f"{command[0]}: {len(replies)} replies for {len(lines)} commands")
    out, i = [], 0
    for s in scenario_list:
        n = len(s["commands"])
        out.append(replies[i:i + n])
        i += n
    return out, elapsed


def first_difference(commands, expected, actual):
    for index, (c, e, a) in enumerate(zip(commands, expected, actual)):
        if e != a:
            return {"index": index, "command": c, "python": e, "rust": a}
    return None


def decode(value):
    """Readable form of hex packets in a diff."""
    if isinstance(value, dict) and "out" in value:
        return {"out": [[x[0], bytes.fromhex(x[1]).decode("latin-1")] if isinstance(x, list)
                        else bytes.fromhex(x).decode("latin-1") for x in value["out"]]}
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rust", required=True, help="path to x4lan-replay")
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--traces", type=Path, action="append", default=[])
    parser.add_argument("--generated", type=int, default=0, help="number of randomized scenarios")
    parser.add_argument("--seed", type=int, default=7541)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--show", type=int, default=5, help="differences to print")
    args = parser.parse_args()

    groups = {}
    for path in args.traces:
        traces = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        groups.setdefault("recorded test traces", []).extend(traces)
    if args.generated:
        groups["hand-written scenarios"] = scenarios.handwritten()
        groups["randomized scenarios"] = scenarios.randomized(args.generated, args.seed)
        groups["number parsing/formatting"] = scenarios.numbers(args.seed)

    report = {"groups": {}, "differences": []}
    failures = 0
    python_driver = [args.python, str(HERE / "py_driver.py")]
    for group, items in groups.items():
        python, py_time = run(python_driver, items)
        rust, rs_time = run([str(Path(args.rust).resolve())], items)
        stats = {"scenarios": len(items), "commands": sum(len(s["commands"]) for s in items),
                 "identical": 0, "different": 0, "not_replayable": 0,
                 "python_seconds": round(py_time, 3), "rust_seconds": round(rs_time, 3),
                 "python_exceptions": 0, "packets_compared": 0}
        for s, p, r in zip(items, python, rust):
            if "results" in s and p != s["results"]:
                stats["not_replayable"] += 1
                continue
            stats["python_exceptions"] += sum("exception" in x for x in p)
            stats["packets_compared"] += sum(len(x.get("out", [])) for x in p)
            diff = first_difference(s["commands"], p, r)
            if diff is None:
                stats["identical"] += 1
            else:
                stats["different"] += 1
                failures += 1
                report["differences"].append({"group": group, "scenario": s["name"], "level": s.get("level"), **diff})
        report["groups"][group] = stats
        print(f"{group}: {stats['identical']}/{stats['scenarios'] - stats['not_replayable']} identical, "
              f"{stats['different']} different, {stats['not_replayable']} not replayable "
              f"({stats['commands']} commands, {stats['packets_compared']} packets compared; "
              f"python {stats['python_seconds']}s, rust {stats['rust_seconds']}s)")
    for d in report["differences"][:args.show]:
        print(f"\nDIFF {d['group']} :: {d['scenario']} at command {d['index']}: {json.dumps(d['command'])[:300]}")
        print("  python:", json.dumps(decode(d["python"]))[:1500])
        print("  rust:  ", json.dumps(decode(d["rust"]))[:1500])
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
