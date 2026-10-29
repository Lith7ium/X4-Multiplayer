"""Create two configured client folders without modifying an X4 installation."""

import argparse
import ipaddress
import json
from pathlib import Path
import secrets
import shutil

from x4lan.host import ATOM


def prepare(
    output: Path, host: str, build: str, mode: str = "telemetry", port: int = 45841
) -> dict:
    ipaddress.IPv4Address(host)
    if not ATOM.fullmatch(build):
        raise ValueError("build must be an identifier such as 9.00-611726")
    if mode not in ("telemetry", "proxy") or not 1 <= port <= 65535:
        raise ValueError("invalid mode or port")
    output.mkdir(parents=True, exist_ok=False)
    session = dict(token=secrets.token_hex(16), build=build, bubble="bubble-01", port=port)
    (output / "session.json").write_text(json.dumps(session, indent=2) + "\n", encoding="utf-8")
    source = Path(__file__).parent / "src" / "extension" / "x4_lan_lab"
    for slot in ("p1", "p2"):
        target = output / slot / "extensions" / "x4_lan_lab"
        shutil.copytree(source, target)
        values = {
            "enabled": True,
            "test_profile": "45841",
            "host": host,
            **session,
            "slot": slot,
            "mode": mode,
            "proxy_macro": "ship_arg_s_scout_01_a_macro",
        }
        lines = ["-- Generated X4 LAN Lab configuration", "X4LAN_CONFIG = {"]
        for key, value in values.items():
            literal = "true" if value is True else json.dumps(value)
            lines.append(f"    {key} = {literal},")
        lines.append("}\n")
        (target / "ui" / "config.lua").write_text("\n".join(lines), encoding="utf-8")
    return session


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="LAN IPv4 address of relay computer")
    parser.add_argument("--build", required=True, help="exact shared X4 build label")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("telemetry", "proxy"), default="telemetry")
    parser.add_argument("--port", type=int, default=45841)
    args = parser.parse_args()
    prepare(args.output, args.host, args.build, args.mode, args.port)
    print(f"Prepared {args.output / 'p1'} and {args.output / 'p2'} ({args.mode}).")
    print("No game files were modified. See README.md for the live test.")
