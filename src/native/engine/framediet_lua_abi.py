"""Check x4lan_framediet_64.dll in a standalone Lua process; never attach to or start X4.

python src/native/engine/framediet_lua_abi.py --lua-runtime <copy of lua51_64.dll> --module .local/engine/x4lan_framediet_64.dll
"""
import argparse
import ctypes
from pathlib import Path


def run(lua_runtime: Path, module: Path) -> None:
    lua = ctypes.WinDLL(str(lua_runtime.resolve()))
    lua.luaL_newstate.restype = ctypes.c_void_p
    lua.luaL_openlibs.argtypes = [ctypes.c_void_p]
    lua.luaL_loadstring.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lua.lua_pcall.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lua.lua_tolstring.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    lua.lua_tolstring.restype = ctypes.c_char_p
    lua.lua_close.argtypes = [ctypes.c_void_p]
    state = lua.luaL_newstate()
    if not state:
        raise RuntimeError('luaL_newstate failed')
    try:
        lua.luaL_openlibs(state)
        # Decimal byte escapes avoid treating the path as Lua text.
        path = ''.join('\\%03d' % b for b in str(module.resolve()).encode('utf-8'))
        source = f'''
          local module=assert(package.loadlib("{path}","luaopen_x4lan_framediet"))()
          assert(type(module.start)=='function')
          local function refused(expected, ...)
            local h,why=module.start(...)
            assert(h==nil and why==expected,tostring(why))
          end
          refused('usage: start(mode, sites[, period[, audit_every[, digest_max]]])')
          refused('usage: start(mode, sites[, period[, audit_every[, digest_max]]])','diet')
          refused('usage: start(mode, sites[, period[, audit_every[, digest_max]]])','diet',1)
          refused('usage: start(mode, sites[, period[, audit_every[, digest_max]]])','diet','server',0,0,0,0)
          refused('unsupported mode','skip','server')
          refused('unsupported sites','diet','')
          refused('unsupported sites','diet','ui_scenes,bogus')
          refused('invalid period','diet','server',-1)
          refused('invalid period','diet','server',1.5)
          refused('invalid period','diet','server','4')
          refused('invalid audit interval','audit','all',0,-2)
          refused('invalid digest size','audit','all',0,30,2^21)
          refused('unsupported executable or modified engine signatures','diet','server')
          refused('unsupported executable or modified engine signatures','audit','all',0,30,1024)
          refused('unsupported executable or modified engine signatures','pass','ui_scenes,ui_render,camera,draw',nil,nil,nil)
          local thread=coroutine.create(function()
            local h,why=module.start('diet','server')
            assert(h==nil and why=='main Lua thread required',why)
          end)
          assert(coroutine.resume(thread))
        '''.encode('utf-8')
        result = lua.luaL_loadstring(state, source)
        if not result:
            result = lua.lua_pcall(state, 0, 0, 0)
        if result:
            raise RuntimeError(lua.lua_tolstring(state, -1, None).decode('utf-8', errors='replace'))
    finally:
        lua.lua_close(state)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lua-runtime', required=True, type=Path)
    parser.add_argument('--module', required=True, type=Path)
    args = parser.parse_args()
    run(args.lua_runtime, args.module)
    print('PASS: actual Lua ABI, argument validation, coroutine rejection, non-X4 process rejection')


if __name__ == '__main__':
    main()
