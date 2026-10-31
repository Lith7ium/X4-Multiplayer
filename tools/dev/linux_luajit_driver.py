#!/usr/bin/python3
"""Bounded nongame LuaJIT library driver for the isolated readiness ABI checks.

Acts only as `luajit - module.so` for the existing five-case test helper. Uses an
existing full-hash-pinned LuaJIT library; no package install, X4 or engine hooks.
"""
import ctypes
import hashlib
import json
import mmap
import os
from pathlib import Path
import sys
import time


def mapping_prefix(address):
    with open('/proc/self/maps', 'rb') as stream: data = stream.read(32 * 1024 * 1024 + 1)
    if len(data) > 32 * 1024 * 1024: raise ValueError('test map-size bound exceeded')
    prefix = 0
    for line in data.splitlines(keepends=True):
        start, end = (int(value, 16) for value in line.split(None, 1)[0].split(b'-'))
        if start <= address < end: return prefix, len(data)
        prefix += len(line)
    raise ValueError('test accessor mapping not found')


def large_map_prefix():
    """Optional nongame pressure test; reserve only private-process mappings."""
    reservations = []
    if os.environ.get('X4LAN_TEST_LUAJIT_LARGE_MAPS') != '1': return reservations
    try: accessor = ctypes.CDLL(None).x4mp_load_observer_ready
    except AttributeError: return reservations  # Missing-agent negative test.
    address = ctypes.cast(accessor, ctypes.c_void_p).value
    deadline = time.monotonic() + 5
    try:
        while True:
            prefix, total = mapping_prefix(address)
            if prefix > 1_048_576: break
            if len(reservations) >= 32768 or time.monotonic() > deadline: raise ValueError('bounded map-prefix setup refused')
            for _ in range(256):
                reservations.append(mmap.mmap(-1, mmap.PAGESIZE, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ | mmap.PROT_WRITE))
        directory = Path(os.environ['X4LAN_TEST_LUAJIT_EVIDENCE']).resolve(strict=True)
        stat = directory.stat()
        if stat.st_uid != os.getuid() or stat.st_mode & 0o077: raise ValueError('private test evidence directory required')
        with (directory / f'map-prefix-{os.getpid()}.json').open('x') as stream:
            json.dump({'prefix_bytes': prefix, 'total_bytes': total, 'mapping_count': len(reservations),
                       'reserved_bytes': len(reservations) * mmap.PAGESIZE, 'native_x4': False}, stream)
        return reservations
    except Exception:
        for value in reservations: value.close()
        raise


def main():
    if len(sys.argv) != 3 or sys.argv[1] != '-': raise ValueError('expected stdin and exact module argument')
    module = Path(sys.argv[2]).resolve(strict=True)
    if not module.is_file() or module.suffix != '.so': raise ValueError('module file required')
    source = sys.stdin.buffer.read(16385)
    if len(source) > 16384: raise ValueError('bounded Lua input exceeded')
    library = Path(os.environ['X4LAN_TEST_LUAJIT_LIBRARY']).resolve(strict=True)
    expected = os.environ['X4LAN_TEST_LUAJIT_SHA256']
    if len(expected) != 64 or hashlib.sha256(library.read_bytes()).hexdigest() != expected:
        raise ValueError('existing LuaJIT library hash differs')
    reservations = large_map_prefix()
    lua = ctypes.CDLL(str(library), mode=ctypes.RTLD_GLOBAL)
    pointer = ctypes.c_void_p
    lua.luaL_newstate.argtypes = []; lua.luaL_newstate.restype = pointer
    lua.luaL_openlibs.argtypes = [pointer]; lua.luaL_openlibs.restype = None
    lua.luaL_loadbuffer.argtypes = [pointer, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    lua.luaL_loadbuffer.restype = ctypes.c_int
    lua.lua_pcall.argtypes = [pointer, ctypes.c_int, ctypes.c_int, ctypes.c_int]; lua.lua_pcall.restype = ctypes.c_int
    lua.lua_tolstring.argtypes = [pointer, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]; lua.lua_tolstring.restype = pointer
    lua.lua_close.argtypes = [pointer]; lua.lua_close.restype = None
    state = lua.luaL_newstate()
    if not state: raise RuntimeError('Lua state allocation failed')
    try:
        lua.luaL_openlibs(state)
        script = ('arg={[1]=' + json.dumps(str(module)) + '}\n').encode() + source
        code = lua.luaL_loadbuffer(state, script, len(script), b'owned-load-probe-abi')
        if not code: code = lua.lua_pcall(state, 0, -1, 0)
        if code:
            length = ctypes.c_size_t(); message = lua.lua_tolstring(state, -1, ctypes.byref(length))
            reason = ctypes.string_at(message, min(length.value, 1024)).decode(errors='replace') if message else 'non-string Lua error'
            raise RuntimeError(reason)
    finally:
        lua.lua_close(state)
        for value in reservations: value.close()


if __name__ == '__main__': main()
