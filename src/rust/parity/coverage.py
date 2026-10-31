"""Which relay output packet kinds the generated scenarios reach (Python reference).

  python src/rust/parity/coverage.py --generated 600
"""
import argparse
import collections
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import scenarios  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=int, default=600)
    parser.add_argument("--seed", type=int, default=7541)
    args = parser.parse_args()
    items = scenarios.handwritten() + scenarios.randomized(args.generated, args.seed)
    lines = [json.dumps(c) for s in items for c in s["commands"]]
    out = subprocess.run([sys.executable, str(HERE / "py_driver.py")], input="\n".join(lines) + "\n",
                         capture_output=True, text=True, check=True).stdout.splitlines()
    kinds = collections.Counter()
    for line in out:
        for x in json.loads(line).get("out", []):
            packet = bytes.fromhex(x[1] if isinstance(x, list) else x)
            kinds[packet.split(b"|")[0].decode("latin-1")] += 1
    for kind, count in sorted(kinds.items(), key=lambda kv: -kv[1]):
        print(f"{kind:14} {count}")


if __name__ == "__main__":
    main()
