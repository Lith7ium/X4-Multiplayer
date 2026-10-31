"""Differential test: src/native/net.c (shipped x4lan_net_64.dll) vs the Rust x4lan-net cdylib.

Both modules are loaded the way X4 loads them: a LuaJIT 2.1 runtime registered in the
process as ``lua51_64.dll`` runs the unmodified ``src/extension/x4_lan_lab/ui/transport.lua``,
which calls ``package.loadlib(path, "luaopen_x4lan_net")``. Every scenario runs once per
module in a fresh Lua state; the observations (return values, Lua error messages,
datagrams seen by a Python UDP peer) must be identical.

Windows x64 only (the Linux .so is covered by tests/test_lua.py with lupa). Requires a
LuaJIT DLL; build_luajit.py produces one from the luajit-src crate's vendored sources.
No X4 installation is touched.

  python src/rust/parity/native_parity.py --lua src/rust/target/luajit/lua51_64.dll \
      --module c=src/extension/x4_lan_lab/native/x4lan_net_64.dll \
      --module rust=src/rust/target/release/x4lan_net.dll
"""
from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
TRANSPORT = ROOT / "src/extension/x4_lan_lab/ui/transport.lua"


class Lua:
    """A tiny ctypes driver for the LuaJIT C API (only what the harness needs)."""

    def __init__(self, dll):
        self.dll = dll
        dll.luaL_newstate.restype = ctypes.c_void_p
        dll.luaL_openlibs.argtypes = [ctypes.c_void_p]
        dll.luaL_loadbuffer.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
        dll.lua_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        dll.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
        dll.lua_tolstring.restype = ctypes.c_void_p
        dll.lua_settop.argtypes = [ctypes.c_void_p, ctypes.c_int]
        dll.lua_close.argtypes = [ctypes.c_void_p]
        self.state = dll.luaL_newstate()
        dll.luaL_openlibs(self.state)

    def run(self, code: str) -> str:
        """Run a chunk; return its string result, or 'ERROR:<message>'."""
        data = code.encode()
        status = self.dll.luaL_loadbuffer(self.state, data, len(data), b"=harness")
        if status == 0:
            status = self.dll.lua_pcall(self.state, 0, 1, 0)
        size = ctypes.c_size_t()
        pointer = self.dll.lua_tolstring(self.state, -1, ctypes.byref(size))
        text = ctypes.string_at(pointer, size.value).decode("latin-1") if pointer else "<non-string>"
        self.dll.lua_settop(self.state, 0)
        return ("ERROR:" if status else "") + text

    def close(self):
        self.dll.lua_close(self.state)


PRELUDE = r"""
local function show(...)
  local out = {}
  for i = 1, select('#', ...) do
    local v = select(i, ...)
    if type(v) == 'string' then out[#out+1] = string.format('%q', v)
    else out[#out+1] = type(v) == 'userdata' and 'userdata' or tostring(v) end
  end
  return table.concat(out, ',')
end
H = H or {}
H.show = show
H.try = function(f, ...) return show(pcall(f, ...)) end
"""

# Pure-Lua scenarios: (name, chunk). Each chunk returns a string observation.
LUA_CASES = [
    ("module shape", """
        local n = X4LAN_TRANSPORT and true
        local c = X4LAN_TRANSPORT.open('127.0.0.1', 9)
        local mt = getmetatable(c)
        local keys = {}
        for k in pairs(mt) do keys[#keys+1] = k end
        table.sort(keys)
        local reg = debug.getregistry()['x4lan.udp.v1'] == mt
        c:close()
        return H.show(type(c), table.concat(keys, ' '), mt.__index == mt, reg)"""),
    ("port errors", """
        local r = {}
        for _, p in ipairs({0, -1, 65536, 1.5, 0/0, 1/0, 70000}) do
          r[#r+1] = H.try(X4LAN_TRANSPORT.open, '127.0.0.1', p)
        end
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, '127.0.0.1', 'x')
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, '127.0.0.1')
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, '127.0.0.1', '45841')
        return table.concat(r, '\\n')"""),
    ("host errors", """
        local r = {}
        for _, h in ipairs({'', '1.2.3', '01.2.3.4', '1.2.3.04', '256.0.0.1', '255.255.255.2555',
                            '1.2.3.4 ', ' 1.2.3.4', 'localhost', 'a\\0b', '127.0.0.1\\0', '1..2.3',
                            '0x7f.0.0.1', '127.1', '::1', '1.2.3.4.5', '000.0.0.0', '0.0.0.0', '255.255.255.255.255.2', '255.255.255.255.255.25'}) do
          local ok, c = pcall(X4LAN_TRANSPORT.open, h, 9)
          r[#r+1] = H.show(ok, ok and 'conn' or c)
          if ok then c:close() end
        end
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, nil, 9)
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, 127, 9)
        r[#r+1] = H.try(X4LAN_TRANSPORT.open, {}, 9)
        return table.concat(r, '\\n')"""),
    ("bad self and payload", """
        local c = X4LAN_TRANSPORT.open('127.0.0.1', 9)
        local r = {H.try(c.send, nil, 'x'), H.try(c.send, {}, 'x'), H.try(c.receive, 5),
                   H.try(c.close, 'str'), H.try(c.send, c), H.try(c.send, c, {}),
                   H.try(c.send, c, 12345), H.try(c.send, c, string.rep('a', 769)),
                   H.try(c.send, c, string.rep('a', 768))}
        c:close()
        return table.concat(r, '\\n')"""),
    ("closed connection", """
        local c = X4LAN_TRANSPORT.open('127.0.0.1', 9)
        c:close(); c:close()
        return H.show(c:send('x'), c:receive(), c.__gc ~= nil)"""),
    ("garbage collection", """
        for i = 1, 200 do X4LAN_TRANSPORT.open('127.0.0.1', 9) end
        collectgarbage(); collectgarbage()
        local c = X4LAN_TRANSPORT.open('127.0.0.1', 9)
        local mt = getmetatable(c)
        mt.__gc(c); mt.__gc(c)
        return H.show(c:send('x'), collectgarbage('count') > 0)"""),
]


def udp_cases(lua: Lua) -> list[tuple[str, str]]:
    """Exchange real datagrams with a Python peer on 127.0.0.1."""
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(("127.0.0.1", 0))
    peer.settimeout(1)
    port = peer.getsockname()[1]
    results = []
    try:
        lua.run(f"C = X4LAN_TRANSPORT.open('127.0.0.1', {port}); return ''")
        sent = lua.run("return H.show(C:send(''), C:send('a'), C:send(string.rep('b', 768)),"
                       " C:send(string.rep('c', 769)), C:send('x|y\\0z\\255'))")
        seen = []
        source = None
        for _ in range(4):
            data, source = peer.recvfrom(4096)
            seen.append((len(data), data[:12].decode("latin-1")))
        results.append(("send results + datagrams", sent + " " + json.dumps(seen)))
        results.append(("receive empty queue", lua.run("return H.show(C:receive())")))
        for payload in (b"PING", b"z" * 768, b"y" * 769, b"", b"\x00\xff|ok", b"q" * 2000):
            peer.sendto(payload, source)
            observed = lua.run("local t = {} for i = 1, 3 do t[i] = H.show(C:receive()) end return table.concat(t, ';')")
            results.append((f"receive {len(payload)} bytes", observed.replace("z" * 768, "<z*768>")))
        # A datagram from a third party never reaches a connected UDP socket.
        other = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        other.sendto(b"foreign", source)
        other.close()
        results.append(("foreign sender filtered", lua.run("return H.show(C:receive())")))
        # Windows: a closed peer's ICMP port-unreachable surfaces on recv.
        peer.close()
        lua.run("C:send('to a closed port'); return ''")
        results.append(("after peer closed", lua.run("return H.show(C:receive(), C:receive(), C:send('again'))")))
        lua.run("C:close(); return ''")
    finally:
        peer.close()
    return results


def run_module(lua_dll: Path, module: Path) -> list[tuple[str, str]]:
    lua = Lua(ctypes.CDLL(str(lua_dll)))
    try:
        setup = lua.run(PRELUDE + f"X4LAN_NATIVE_PATH = {json.dumps(str(module))}\n" +
                        TRANSPORT.read_text(encoding="utf-8") + "\nreturn 'ok'")
        if setup != "ok":
            raise RuntimeError(setup)
        results = [(name, lua.run(code)) for name, code in LUA_CASES]
        results += udp_cases(lua)
        return results
    finally:
        lua.close()


def unresolved(lua_dll: Path, module: Path) -> str:
    """In a child process, load LuaJIT under a different module name: the module must
    fail to resolve lua51_64.dll and transport.lua must report it."""
    code = (
        "import ctypes, shutil, sys, tempfile, os\n"
        "from pathlib import Path\n"
        "d = Path(tempfile.mkdtemp())\n"
        "shutil.copy(sys.argv[1], d / 'lua51_other.dll')\n"
        "sys.path.insert(0, sys.argv[3])\n"
        "import native_parity as n\n"
        "lua = n.Lua(ctypes.CDLL(str(d / 'lua51_other.dll')))\n"
        "print(lua.run(n.PRELUDE + 'X4LAN_NATIVE_PATH=' + repr(sys.argv[2]) + '\\n' +"
        " n.TRANSPORT.read_text() + '\\nreturn H.try(X4LAN_TRANSPORT.open, \"127.0.0.1\", 9)'))\n")
    out = subprocess.run([sys.executable, "-c", code, str(lua_dll), str(module), str(Path(__file__).parent)],
                         capture_output=True, text=True, timeout=60)
    return (out.stdout + out.stderr).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lua", type=Path, required=True, help="LuaJIT DLL named lua51_64.dll")
    parser.add_argument("--module", action="append", required=True, help="label=path (first is the reference)")
    parser.add_argument("--json", type=Path, help="write all observations here")
    args = parser.parse_args()
    if args.lua.name.lower() != "lua51_64.dll":
        parser.error("the LuaJIT DLL must be named lua51_64.dll (net.c looks it up by that name)")
    modules = [tuple(m.split("=", 1)) for m in args.module]
    observed = {}
    for label, path in modules:
        rows = run_module(args.lua, Path(path).resolve())
        rows.append(("unresolved Lua ABI", unresolved(args.lua, Path(path).resolve())))
        observed[label] = rows
    reference_label = modules[0][0]
    reference = observed[reference_label]
    failures = 0
    for label, rows in observed.items():
        if label == reference_label:
            continue
        for (name, expected), (_, actual) in zip(reference, rows):
            same = expected == actual
            failures += not same
            print(f"[{'SAME' if same else 'DIFF'}] {name}")
            if not same:
                print(f"    {reference_label}: {expected!r}\n    {label}: {actual!r}")
    total = len(reference) * (len(modules) - 1)
    print(f"native parity: {total - failures}/{total} scenarios identical")
    if args.json:
        args.json.write_text(json.dumps(observed, indent=1), encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
