"""Run the existing Lua/native contract against the Rust Linux transport."""
import ctypes
from pathlib import Path
import unittest

from test_lua import LuaTests, LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'src/rust/target/release/libx4lan_net.so'


@unittest.skipUnless(LuaRuntime and MODULE.is_file(), 'LuaJIT or Rust release transport unavailable')
class RustNativeTests(LuaTests):
    def runtime(self):
        import lupa.luajit21
        ctypes.CDLL(lupa.luajit21.__file__, mode=ctypes.RTLD_GLOBAL)
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().X4LAN_NATIVE_PATH = str(MODULE)
        for name in ('transport.lua', 'client.lua'):
            lua.execute((ROOT / 'src/extension/x4_lan_lab/ui' / name).read_text())
        return lua


if __name__ == '__main__':
    unittest.main()
