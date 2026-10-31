"""Actual LuaJIT adapter checks; native engine behavior has separate C coverage."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'tools/dev/native_portable_windows.lua').read_text()

class WindowsPortableAdapter(unittest.TestCase):
    def runtime(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          savepath='C:/private/45850/save';safe=false;loads=0;boots=0;cancels=0;requests=0;phase=0
          active=true;fixture_ok=true;transport_ok=true;restore_ok=true;logs={};status_reads=0
          package.loaded.ffi={os='Windows',cdef=function()end,string=function(s)return s end,
            C={GetSaveFolderPath=function()return savepath end}}
          GetUISafeModeOption=function()return safe end
          DebugError=function(s)table.insert(logs,s)end
          X4MP_PORTABLE_CONFIG={enabled=true,mode='portable-native-v1',slot=1,profile='45850',run='test',
            save_path=savepath,token=string.rep('a',32),snapshot_sha256=string.rep('b',64),
            contract_sha256=string.rep('c',64),agent_sha256=string.rep('d',64),
            schema_sha256='57edd192fe98d5daa46df95e7d84d17a76976a51776cf06fcf9e4e5e8e5dde3e',
            port=40000,timeout_ms=5000,ticks=12,fixture_manifest=string.rep('0',203)}
          native={
            boot=function(slot,port,token,timeout,snapshot,options)
              assert(slot==1 and port==40000 and token==string.rep('a',32) and timeout==5000)
              assert(snapshot==string.rep('b',64) and options==70368744177664)
              boots=boots+1;return active and 0 or 10
            end,
            portable_config=function(ids,contract,ticks)
              assert(ids==string.rep('0',203) and contract==string.rep('c',64) and ticks==12)
              return fixture_ok and 1 or 0
            end,
            portable_transport=function(agent,startup) assert(agent==string.rep('d',64) and startup==600000);return transport_ok and active and 1 or 0 end,
            portable_ready=function()return active and phase==0 and 1 or 0 end,
            load_requested=function()if active and phase==0 then phase=1;requests=requests+1;return 1 end return 0 end,
            portable_cancel=function()cancels=cancels+1;active=false;return restore_ok and 1 or 0 end,
            portable_status=function()
              status_reads=status_reads+1
              if status_missing then return nil end
              if status_override then return status_override end
              local fields={}
              for _,key in ipairs(status_keys) do fields[#fields+1]=key..'='..tostring(status_fields[key]) end
              return table.concat(fields,' ')
            end,
          }
          package.loadlib=function(path,name)
            assert(path=='./extensions/x4_lan_lab/native/x4lan_gate_64.dll' and name=='luaopen_x4lan_gate')
            loads=loads+1;return function()return native end
          end
          status_keys={'schema','configured','frames','last','calls','returns','failures','saved','stage',
            'wire','draining','closed','terminal_restored','wire_stage','active','reason','issued','completed'}
          status_fields={schema=X4MP_PORTABLE_CONFIG.schema_sha256,configured=1,frames=1,last=12,calls=0,
            returns=0,failures=0,saved='0000000000000000',stage='complete',wire=1,draining=0,closed=0,
            terminal_restored=0,wire_stage='waiting',active=1,reason=0,issued=0,completed=0}
          function close_status()
            local f=status_fields
            f.frames=13;f.calls=13;f.returns=13;f.closed=1;f.terminal_restored=1;f.wire_stage='closed'
            f.active=0;f.reason=1;f.issued=12;f.completed=12
          end
        ''')
        return lua

    def test_explicit_native_peer_and_once_only_load_admission(self):
        lua=self.runtime();lua.execute(SOURCE)
        self.assertEqual(lua.globals().boots,1)
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.before_load(string.rep("e",64))'))
        self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.before_load(string.rep("b",64))'))
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.before_load(string.rep("b",64))'))
        self.assertEqual(lua.globals().requests,1)
        lua.execute(SOURCE)  # Existing native session survives UI reload, but cannot request a second load.
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        self.assertEqual(lua.globals().requests,1)

    def test_owner_and_bounds_refuse_before_loading_module(self):
        for change in ("savepath='C:/normal/70000001/save'",'safe=true',
                       'X4MP_PORTABLE_CONFIG.slot=2','X4MP_PORTABLE_CONFIG.ticks=121',
                       'X4MP_PORTABLE_CONFIG.startup_timeout_ms=100',
                       'X4MP_PORTABLE_CONFIG.port=40000.5',"X4MP_PORTABLE_CONFIG.token='bad'"):
            lua=self.runtime();lua.execute(change)
            with self.assertRaises(Exception):lua.execute(SOURCE)
            self.assertEqual(lua.globals().loads,0)
            self.assertEqual(lua.globals().boots,0)

    def test_partial_configuration_failure_cancels_once(self):
        for change in ('fixture_ok=false','transport_ok=false'):
            lua=self.runtime();lua.execute(change)
            with self.assertRaises(Exception):lua.execute(SOURCE)
            self.assertEqual(lua.globals().cancels,1)
            self.assertFalse(lua.globals().active)
            self.assertIsNone(lua.globals().X4MP_PORTABLE_BACKEND)

    def test_released_profiles_only_and_exact_suffix(self):
        for profile in ('45850','45851','45853','45854','45855','45856','45857','45858'):
            lua=self.runtime()
            lua.execute(f"X4MP_PORTABLE_CONFIG.profile='{profile}';savepath='C:/private/{profile}/save';X4MP_PORTABLE_CONFIG.save_path=savepath")
            lua.execute(SOURCE)
            self.assertTrue(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        for profile in ('45841','45842','45852','70000001'):
            lua=self.runtime()
            lua.execute(f"X4MP_PORTABLE_CONFIG.profile='{profile}';savepath='C:/private/{profile}/save';X4MP_PORTABLE_CONFIG.save_path=savepath")
            with self.assertRaises(Exception):lua.execute(SOURCE)
            self.assertEqual(lua.globals().loads,0)
        lua=self.runtime();lua.execute("X4MP_PORTABLE_CONFIG.profile='45851'")
        with self.assertRaises(Exception):lua.execute(SOURCE)
        self.assertEqual(lua.globals().loads,0)

    def test_cancel_restoration_failure_and_stale_readiness(self):
        lua=self.runtime();lua.execute(SOURCE);lua.execute('restore_ok=false')
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.cancel()'))
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.cancel()'))
        self.assertEqual(lua.globals().cancels,1)
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.ready()'))
        self.assertFalse(lua.eval('X4MP_PORTABLE_BACKEND.before_load(string.rep("b",64))'))
        with self.assertRaises(Exception):lua.execute(SOURCE)
        self.assertEqual(lua.globals().cancels,1)

    def test_profile_change_raises_for_loader_cancellation(self):
        lua=self.runtime();lua.execute(SOURCE);lua.execute("savepath='C:/normal/70000001/save'")
        with self.assertRaises(Exception):lua.eval('X4MP_PORTABLE_BACKEND.ready()')
        self.assertEqual(lua.globals().requests,0)

    def test_disabled_is_inert(self):
        lua=LuaRuntime();lua.execute('X4MP_PORTABLE_CONFIG={enabled=false}');lua.execute(SOURCE)

    def test_terminal_requires_fresh_closed_status_and_logs_exactly_once(self):
        lua=self.runtime();lua.execute(SOURCE)
        lua.execute('status_missing=true;X4MP_PORTABLE_BACKEND.observe();status_missing=false;X4MP_PORTABLE_BACKEND.observe()')
        self.assertEqual(len(lua.globals().logs),0)
        lua.execute('close_status();status_fields.draining=1;X4MP_PORTABLE_BACKEND.observe()')
        self.assertEqual(len(lua.globals().logs),0)
        lua.execute('status_fields.draining=0;X4MP_PORTABLE_BACKEND.observe();X4MP_PORTABLE_BACKEND.observe()')
        self.assertEqual(lua.globals().status_reads,4)
        self.assertEqual(len(lua.globals().logs),1)
        row=lua.globals().logs[1]
        self.assertLessEqual(len(row),1024)
        for field in ('PORTABLE_NATIVE TERMINAL result=success slot=1 run=test',
                      'contract='+'c'*64,'snapshot='+'b'*64,'agent='+'d'*64,
                      'frames=13 last=12 calls=13 returns=13 failures=0',
                      'active=0 reason=1 issued=12 completed=12'):
            self.assertIn(field,row)
        self.assertNotIn('a'*32,row)  # Session secret is never logged.
        self.assertEqual(lua.globals().requests,0)
        self.assertEqual(lua.globals().cancels,0)

    def test_frame_zero_failure_and_each_incomplete_success_receipt(self):
        lua=self.runtime();lua.execute(SOURCE)
        lua.execute('status_fields.active=0;status_fields.reason=74;status_fields.failures=1;X4MP_PORTABLE_BACKEND.observe()')
        self.assertIn('result=failure',lua.globals().logs[1])
        self.assertIn('issued=0 completed=0',lua.globals().logs[1])
        for key,value in {'frames':12,'last':11,'calls':12,'returns':12,'failures':1,'completed':11,
                          'issued':11,'configured':0,'wire':0,'closed':0,'terminal_restored':0,
                          'reason':76,'saved':'0000000000000001','stage':'identity','wire_stage':'terminal_not_acknowledged'}.items():
            lua=self.runtime();lua.execute(SOURCE);lua.execute('close_status()')
            lua.globals().status_fields[key]=value
            lua.execute('X4MP_PORTABLE_BACKEND.observe()')
            self.assertIn('result=failure',lua.globals().logs[1],key)

    def test_invalid_status_never_fabricates_terminal_and_cancel_remains_fatal(self):
        for status in ('closed=1', 'x'*640, ' schema=x', 'schema=x\nclosed=1'):
            lua=self.runtime();lua.execute(SOURCE);lua.globals().status_override=status
            with self.assertRaises(Exception):lua.eval('X4MP_PORTABLE_BACKEND.observe()')
            self.assertEqual(len(lua.globals().logs),0)
        for change in ('status_fields.calls="013"','status_fields.calls="9007199254740992"',
                       'status_fields.active=2','status_fields.stage=string.rep("x",65)',
                       'table.insert(status_keys,"closed")','status_fields.schema=string.rep("f",64)'):
            lua=self.runtime();lua.execute(SOURCE);lua.execute(change)
            with self.assertRaises(Exception):lua.eval('X4MP_PORTABLE_BACKEND.observe()')
            self.assertEqual(len(lua.globals().logs),0)
        lua=self.runtime();lua.execute(SOURCE)
        lua.execute('X4MP_PORTABLE_BACKEND.cancel();close_status();X4MP_PORTABLE_BACKEND.observe()')
        self.assertEqual(lua.globals().cancels,1)
        self.assertTrue(any('CANCEL' in row for row in lua.globals().logs.values()))
        self.assertFalse(any('TERMINAL' in row for row in lua.globals().logs.values()))

    def test_common_callback_observes_after_closure_and_load_error(self):
        loader=(ROOT/'tools/dev/native_portable_loader.lua').read_text()
        for failed in (False,True):
            lua=self.runtime();lua.execute(SOURCE)
            lua.execute('''
              menu=true;valid=false
              local C=package.loaded.ffi.C
              C.IsStartmenu=function()return menu end;C.IsPlayerValid=function()return valid end
              C.IsSaveListLoadingComplete=function()return true end;C.ReloadSaveList=function()end
              C.IsGamePaused=function()return false end;C.GetCurrentGameTime=function()return 7 end
              GetSaveList=function()return {{filename='save_009'}} end
              LoadGame=function()end;SetScript=function(_,f)update=f end
            ''')
            lua.execute(loader);lua.execute('update()')
            if failed:lua.execute('LoadGame=function()error("load failed")end')
            lua.execute('update()')
            if failed:
                lua.execute('status_fields.active=0;status_fields.reason=78;status_fields.wire_stage="configured"')
            else:lua.execute('menu=false;valid=true;close_status()')
            lua.execute('update();update()')
            rows=list(lua.globals().logs.values())
            self.assertEqual(sum('PORTABLE_NATIVE TERMINAL' in row for row in rows),1)
            self.assertTrue(any('result='+('failure' if failed else 'success') in row for row in rows))
            self.assertEqual(any('ERROR' in row for row in rows),failed)
            self.assertEqual(lua.globals().cancels,1 if failed else 0)

if __name__=='__main__':unittest.main()
