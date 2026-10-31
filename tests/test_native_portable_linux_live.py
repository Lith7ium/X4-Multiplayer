"""Actual LuaJIT with a synthetic read-only bridge; no native game claim."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

SOURCE = (Path(__file__).resolve().parents[1] / 'tools/dev/native_portable_linux_live.lua').read_text()


class LinuxPortableAdapter(unittest.TestCase):
    def runtime(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4MP_PORTABLE_CONFIG={enabled=true,mode='portable-fixture12-zero-v1',profile='45841',
            contract_sha256='516ef8728fad1e9c99da5e3fa864d5d05366525ba7576c5256b2e9926b10bd50',
            snapshot_sha256=string.rep('a',64),schema_sha256='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e'}
          package.loaded.ffi={os='Linux',C=setmetatable({},{__index=function()error('restricted FFI forbidden')end}),
            cdef=function()error('FFI declarations forbidden')end}
          ready=false;safe=false;modules=0
          GetUISafeModeOption=function()return safe end
          package.loadlib=function(path,symbol)
            assert(path=='./extensions/x4_lan_lab/native/x4mp_load_probe.so')
            assert(symbol=='luaopen_x4mp_load_probe');modules=modules+1
            return function()return {ready=function()return ready end}end
          end
        ''')
        return lua

    def test_only_exact_ready_boolean_and_snapshot_authorize_load(self):
        lua = self.runtime(); lua.execute(SOURCE)
        self.assertEqual(lua.globals().modules, 1)
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        lua.execute('ready=1'); self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        lua.execute('ready=true')
        self.assertFalse(lua.eval("X4MP_PORTABLE_BACKEND.before_load(string.rep('b',64))"))
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))
        lua.execute('ready=false'); self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))

    def test_wrong_mode_profile_schema_and_snapshot_refuse_before_module_load(self):
        for change in ("mode='snapshot-import-observer-v1'", "profile='45850'", "schema_sha256='wrong'", "snapshot_sha256=nil"):
            lua = self.runtime(); lua.execute('X4MP_PORTABLE_CONFIG.' + change)
            with self.assertRaises(Exception): lua.execute(SOURCE)
            self.assertEqual(lua.globals().modules, 0)

    def test_missing_bridge_wrong_artifact_and_protected_ui_refuse(self):
        for setup in ('safe=true', 'package.loadlib=function()return nil,"missing bridge"end',
                      'package.loadlib=function()return function()return nil,"agent mismatch"end end'):
            lua = self.runtime(); lua.execute(setup)
            with self.assertRaises(Exception): lua.execute(SOURCE)
            self.assertIsNone(lua.globals().X4MP_PORTABLE_BACKEND)

    def test_disabled_source_remains_inert(self):
        lua = self.runtime(); lua.execute('X4MP_PORTABLE_CONFIG.enabled=false'); lua.execute(SOURCE)
        self.assertEqual(lua.globals().modules, 0); self.assertIsNone(lua.globals().X4MP_PORTABLE_BACKEND)

    def test_new_mode_requires_all_three_pins_and_legacy_rejects_math_field(self):
        setup="""
          X4MP_PORTABLE_CONFIG.mode='portable-fixture12-euler-v2'
          X4MP_PORTABLE_CONFIG.contract_sha256='77b19d6f210426c86c73bafdf3a3f2531747968819a5ef61e93344f902da2790'
          X4MP_PORTABLE_CONFIG.snapshot_sha256='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
          X4MP_PORTABLE_CONFIG.import_math_sha256='27c9bd768900673531c698e59b410fc38940b036ef9563e5e9483e9f3303537b'
        """
        lua=self.runtime();lua.execute(setup);lua.execute(SOURCE);lua.execute('ready=true')
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load(X4MP_PORTABLE_CONFIG.snapshot_sha256)'))
        self.assertFalse(lua.eval("X4MP_PORTABLE_BACKEND.before_load(string.rep('a',64))"))
        # UI reload reattaches the read-only accessor without resetting native
        # math or issuing another Load. The native accessor remains authoritative.
        lua.execute(SOURCE);self.assertEqual(lua.globals().modules,2)
        lua.execute('ready=false');self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        lua=self.runtime();lua.execute(setup);lua.execute("X4MP_PORTABLE_CONFIG.mode='portable-fixture12-euler-v1'")
        with self.assertRaises(Exception):lua.execute(SOURCE)
        self.assertEqual(lua.globals().modules,0)
        for key in ('contract_sha256','snapshot_sha256','import_math_sha256'):
            for value in ('nil',"string.rep('0',64)"):
                lua=self.runtime();lua.execute(setup);lua.execute('X4MP_PORTABLE_CONFIG.'+key+'='+value)
                with self.assertRaises(Exception):lua.execute(SOURCE)
                self.assertEqual(lua.globals().modules,0)
        lua=self.runtime();lua.execute("X4MP_PORTABLE_CONFIG.import_math_sha256=false")
        with self.assertRaises(Exception):lua.execute(SOURCE)
        self.assertEqual(lua.globals().modules,0)


if __name__ == '__main__': unittest.main()
