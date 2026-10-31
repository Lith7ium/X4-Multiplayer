"""Compile/run the owned-memory XML adapter checks; no game access or hooks."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cc")
    parser.add_argument("--compiler-arg", action="append", default=[])
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / "src/native/engine/test_rotation_xml_input.c"
    with tempfile.TemporaryDirectory(prefix="x4mp-rotation-xml-") as temp:
        output = Path(temp) / ("test.exe" if os.name == "nt" else "test")
        command = [args.compiler, *args.compiler_arg, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2"]
        if args.sanitize:
            command.append("-fsanitize=address,undefined")
        subprocess.run([*command, str(source), "-o", str(output)], cwd=root, check=True)
        subprocess.run([str(output)], cwd=root, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
