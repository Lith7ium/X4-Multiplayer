"""End-to-end on Windows: the extension's own transport.lua + client.lua, running in a
real LuaJIT 2.1 (lua51_64.dll), exchange poses through each native module (net.c DLL,
Rust DLL) and each relay process (python -m x4lan.host, x4lan-relay). This is the
Windows counterpart of tests/test_lua.py's Linux-only real-UDP test. Synthetic peers:
two clients in one Lua state; X4 itself is not involved.

  python src/rust/parity/lua_e2e.py --lua src/rust/target/luajit/lua51_64.dll \
      --module c=src/extension/x4_lan_lab/native/x4lan_net_64.dll --module rust=src/rust/target/release/x4lan_net.dll \
      --relay python --relay rust=src/rust/target/release/x4lan-relay.exe
"""
import argparse
import ctypes
import json
from pathlib import Path
import socket
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
from native_parity import Lua, PRELUDE  # noqa: E402

TOKEN = "a" * 32
UI = ROOT / "src/extension/x4_lan_lab/ui"


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def relay_command(spec):
    if spec == "python":
        return "python", [sys.executable, "-m", "x4lan.host"]
    label, path = spec.split("=", 1)
    return label, [str(Path(path).resolve())]


def exchange(lua, module, command):
    port = free_port()
    process = subprocess.Popen(command + ["--port", str(port), "--token", TOKEN, "--build", "test", "--bubble", "bubble"],
                               cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        time.sleep(0.5)
        setup = lua.run(PRELUDE + f"X4LAN_NATIVE_PATH = {json.dumps(str(module))}\n"
                        + (UI / "transport.lua").read_text(encoding="utf-8") + "\n"
                        + (UI / "client.lua").read_text(encoding="utf-8") + f"""
            E = {{t = {{}}, c = {{}}}}
            for i, slot in ipairs({{'p1', 'p2'}}) do
              E.t[i] = X4LAN_TRANSPORT.open('127.0.0.1', {port})
              local cfg = {{token = string.rep('a', 32), build = 'test', bubble = 'bubble', sector = 'sector_macro', slot = slot}}
              E.c[i] = X4LAN_CLIENT.new(cfg, E.t[i], string.rep(tostring(i), 32))
            end
            return 'ok'""")
        if setup != "ok":
            raise RuntimeError(setup)
        started = time.perf_counter()
        for tick in range(60):
            now = time.perf_counter() - started
            lua.run(f"for i = 1, 2 do E.c[i]:update({now!r}, {{(i - 1) * 100 + {tick}, 0, 0, 0, 0, 0}}) end return ''")
            time.sleep(0.05)
        now = time.perf_counter() - started
        result = lua.run(f"""
            local a, b = E.c[1]:render({now!r}), E.c[2]:render({now!r})
            local out = H.show(a and a[1], b and b[1], E.c[1].lease ~= nil, E.c[2].lease ~= nil)
            for i = 1, 2 do E.t[i]:close() end
            return out""")
        values = result.split(",")
        ok = (values[0] != "nil" and float(values[0]) > 100 and values[1] != "nil" and float(values[1]) < 100
              and values[2:] == ["true", "true"])
        return ok, result
    finally:
        process.kill()
        process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lua", type=Path, required=True)
    parser.add_argument("--module", action="append", required=True, help="label=path")
    parser.add_argument("--relay", action="append", required=True, help="'python' or label=path")
    args = parser.parse_args()
    dll = ctypes.CDLL(str(args.lua))
    failures = 0
    for module in args.module:
        label, path = module.split("=", 1)
        for spec in args.relay:
            relay_label, command = relay_command(spec)
            lua = Lua(dll)
            try:
                ok, detail = exchange(lua, Path(path).resolve(), command)
            finally:
                lua.close()
            failures += not ok
            print(f"[{'PASS' if ok else 'FAIL'}] module={label} relay={relay_label}: "
                  f"p1 sees p2.x, p2 sees p1.x, p1 lease, p2 lease = {detail}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
