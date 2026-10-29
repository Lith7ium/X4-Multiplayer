"""Build the small Lua module with Zig 0.15.2's C compiler, no Lua library link."""

import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--zig", required=True, type=Path)
parser.add_argument("--platform", choices=("linux", "windows", "both"), default="both")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
output = root / "src/extension/x4_lan_lab/native"
output.mkdir(exist_ok=True)
common = [
    str(args.zig.resolve()),
    "cc",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-shared",
    str(root / "src/native/net.c"),
]
if args.platform in ("linux", "both"):
    subprocess.run(
        common
        + ["-target", "x86_64-linux-gnu.2.17", "-fPIC", "-ldl", "-o", str(output / "x4lan_net.so")],
        check=True,
    )
if args.platform in ("windows", "both"):
    # Import libraries/debug artefacts do not belong in the deployable mod.
    with tempfile.TemporaryDirectory(prefix="x4lan-build-") as scratch:
        dll = Path(scratch) / "x4lan_net_64.dll"
        subprocess.run(
            common + ["-target", "x86_64-windows-gnu", "-lws2_32", "-o", str(dll)], check=True
        )
        (output / dll.name).write_bytes(dll.read_bytes())
print("Built", args.platform, "module(s)")
