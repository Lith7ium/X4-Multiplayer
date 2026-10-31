"""Actual LuaJIT, synthetic backend: snapshot loading requires native ACK."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'tools/dev/native_portable_loader.lua').read_text()
LINUX = (ROOT / 'tools/dev/native_portable_linux.lua').read_text()


class PortableLoader(unittest.TestCase):
    def linux_runtime(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4MP_PORTABLE_CONFIG={enabled=true,mode='snapshot-import-observer-v1'}
          package.loaded.ffi={os='Linux',C=setmetatable({},{__index=function()error('restricted ffi.C')end}),
            cdef=function()error('no FFI declarations needed')end}
          safe=false; ready=false; moduleloads=0
          GetUISafeModeOption=function()return safe end
          package.loadlib=function(path,symbol)
            assert(path=='./extensions/x4_lan_lab/native/x4mp_load_probe.so')
            assert(symbol=='luaopen_x4mp_load_probe');moduleloads=moduleloads+1
            return function() return {ready=function()return ready end} end
          end
        ''')
        return lua

    def test_linux_uses_only_supported_read_only_lua_module(self):
        lua=self.linux_runtime();lua.execute(LINUX)
        self.assertEqual(lua.globals().moduleloads,1)
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        lua.execute('ready=true');self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load()'))
        lua.execute('ready=false');self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.before_load()'))
        lua.execute('ready=1');self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))

    def test_linux_module_failure_and_protected_ui_refuse(self):
        for setup in ('safe=true', 'package.loadlib=function()return nil,"missing module"end',
                      'package.loadlib=function()return function()return nil,"agent mismatch"end end'):
            lua=self.linux_runtime();lua.execute(setup)
            with self.assertRaises(Exception):lua.execute(LINUX)
            self.assertIsNone(lua.globals().X4MP_PORTABLE_BACKEND)

    def runtime(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          menu=true;valid=false;ready=false;loads=0;refreshes=0;requests=0;logs={}
          savepath='/private/case/home/45841/save';listed=true
          local C={GetSaveFolderPath=function()return savepath end,IsStartmenu=function()return menu end,
            IsSaveListLoadingComplete=function()return true end,ReloadSaveList=function()refreshes=refreshes+1 end,
            IsPlayerValid=function()return valid end,IsGamePaused=function()return true end,
            GetCurrentGameTime=function()return 7 end}
          package.loaded.ffi={C=C,cdef=function()end,string=function(v)return v end,os='Linux'}
          X4MP_PORTABLE_CONFIG={enabled=true,profile='45841',run='test-case',save_path=savepath,
            snapshot_sha256=string.rep('a',64),contract_sha256=string.rep('b',64),
            schema_sha256='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e'}
          X4MP_PORTABLE_BACKEND={ready=function()return ready end,
            before_load=function()requests=requests+1;return ready end}
          GetSaveList=function()return listed and {{filename='save_009'}} or {} end
          LoadGame=function(slot)assert(slot=='save_009' and ready and requests==1);loads=loads+1 end
          SetScript=function(_,f)update=f end
          DebugError=function(s)table.insert(logs,s)end
        ''')
        return lua

    def test_no_timing_substitute_for_ack_and_exactly_one_load(self):
        lua = self.runtime(); lua.execute(SOURCE)
        lua.execute('for i=1,1000 do update() end')
        self.assertEqual(lua.globals().loads, 0)
        self.assertEqual(lua.globals().refreshes, 1)
        lua.execute('ready=true;update();update();update()')
        self.assertEqual(lua.globals().loads, 1)
        self.assertEqual(lua.globals().requests, 1)
        lua.execute('menu=false;valid=true'); lua.execute(SOURCE); lua.execute('update();update()')
        self.assertEqual(lua.globals().loads, 1)
        self.assertTrue(any('LOADED' in row for row in lua.globals().logs.values()))

    def test_missing_slot_or_profile_change_refuses_before_request(self):
        for mutation in ('listed=false', "savepath='/normal/70000001/save'"):
            lua = self.runtime(); lua.execute(SOURCE); lua.execute('update();ready=true;' + mutation + ';update();update()')
            self.assertEqual(lua.globals().loads, 0)
            self.assertEqual(lua.globals().requests, 0)
            self.assertTrue(any('ERROR' in row for row in lua.globals().logs.values()))

    def test_disabled_is_inert_and_wrong_profile_fails(self):
        lua = LuaRuntime(); lua.execute('X4MP_PORTABLE_CONFIG={enabled=false}')
        lua.execute(SOURCE)
        lua = self.runtime(); lua.execute("X4MP_PORTABLE_CONFIG.profile='70000001'")
        with self.assertRaises(Exception): lua.execute(SOURCE)

    def test_backend_rechecks_ack_immediately_before_load(self):
        lua = self.runtime(); lua.execute(SOURCE)
        lua.execute('update();ready=true;X4MP_PORTABLE_BACKEND.before_load=function()return false end;update()')
        self.assertEqual(lua.globals().loads, 0)
        self.assertTrue(any('native load request refused' in row for row in lua.globals().logs.values()))

    def test_platform_profile_allowlist_and_exact_owned_path(self):
        for platform,profiles in [('Linux',('45841',)),('Windows',('45850','45851','45853','45854','45855','45856','45857','45858'))]:
            for profile in profiles:
                lua=self.runtime()
                lua.execute(f"package.loaded.ffi.os='{platform}';X4MP_PORTABLE_CONFIG.profile='{profile}';savepath='/private/{profile}/save';X4MP_PORTABLE_CONFIG.save_path=savepath")
                lua.execute(SOURCE);lua.execute('update();ready=true;update()')
                self.assertEqual(lua.globals().loads,1)
        for platform,profile in [('Windows','45841'),('Windows','45842'),('Windows','45852'),('Linux','45850'),('Linux','70000001'),('OSX','45841')]:
            lua=self.runtime()
            lua.execute(f"package.loaded.ffi.os='{platform}';X4MP_PORTABLE_CONFIG.profile='{profile}';savepath='/private/{profile}/save';X4MP_PORTABLE_CONFIG.save_path=savepath")
            with self.assertRaises(Exception):lua.execute(SOURCE)
            self.assertEqual(lua.globals().loads,0)
        lua=self.runtime();lua.execute("package.loaded.ffi.os='Windows';X4MP_PORTABLE_CONFIG.profile='45850'")
        with self.assertRaises(Exception):lua.execute(SOURCE)
        self.assertEqual(lua.globals().loads,0)

    def test_load_error_cancels_armed_backend_once_and_retains_failure(self):
        for restoration in ('true','false'):
            lua=self.runtime();lua.execute('cancels=0;X4MP_PORTABLE_BACKEND.cancel=function()cancels=cancels+1;return '+restoration+' end')
            lua.execute(SOURCE)
            lua.execute('update();ready=true;LoadGame=function()error("native load refused")end;update();update()')
            self.assertEqual(lua.globals().cancels,1)
            self.assertEqual(lua.globals().requests,1)
            self.assertEqual(lua.globals().loads,0)
            self.assertTrue(any('CANCEL acknowledged='+restoration in row for row in lua.globals().logs.values()))
            self.assertTrue(any('ERROR' in row for row in lua.globals().logs.values()))

    def test_optional_observer_is_not_a_readiness_or_game_call_dependency(self):
        lua=self.runtime();lua.execute('observations=0;X4MP_PORTABLE_BACKEND.observe=function()observations=observations+1 end')
        lua.execute(SOURCE);lua.execute('update();ready=true;update();menu=false;valid=true;update();update()')
        self.assertEqual(lua.globals().loads,1)
        self.assertEqual(lua.globals().observations,4)
        lua.execute('X4MP_PORTABLE_BACKEND.observe=function()error("observation invalid")end;update();update()')
        self.assertEqual(sum('ERROR observation=' in row for row in lua.globals().logs.values()),1)
        self.assertEqual(lua.globals().loads,1)


if __name__ == '__main__': unittest.main()
