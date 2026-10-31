"""Actual LuaJIT diagnostic boot/reload/partial-init and terminal raw retention."""
import hashlib,re,unittest
from pathlib import Path
from lupa.luajit21 import LuaRuntime
import test_native_gate_getup as getup
from test_native_gate_getup_diagnostic_evidence import fixture
from native_player_getup import bootstrap_plan

SOURCE=(getup.ROOT/'tools/dev/native_gate_getup_diagnostic.lua').read_text()

class DiagnosticLuaTests(unittest.TestCase):
    def test_production_probe_local_contract_preimage_and_reload(self):
        harness=getup.GetUpTests();backend=harness.probe_backend();lua=harness.probe_runtime(backend,True);preimages=[]
        def digest(value):preimages.append(value);return hashlib.sha256(value.encode()).hexdigest()
        lua.globals().actual_digest=digest
        lua.execute('X4LAN_SHA256.digest=function(v) return actual_digest(v) end;update();update();update()')
        config=fixture()[1].replace('token = "'+'a'*32+'"','token = "'+'c'*32+'"')
        expected='SNAPSHOT_BYTES_1|'+'a'*64+'|'+bootstrap_plan(config,diagnostic=True)
        self.assertIn(expected,preimages);self.assertEqual(backend['boot_args'][4],hashlib.sha256(expected.encode()).hexdigest())
        self.assertEqual(backend['boot_args'][5],70377334178981);self.assertEqual(backend['loads'],1)
        backend['state']['boot_phase']=2
        second=harness.probe_runtime(backend,True)
        second.globals().actual_digest=digest;second.execute('X4LAN_SHA256.digest=function(v) return actual_digest(v) end;update();update();update()')
        self.assertEqual((backend['boots'],backend['getups'],backend['loads'],backend['stops']),(2,2,1,0))
        for vm in (lua,second):self.assertFalse(any('ERROR' in line for line in vm.globals().logs.values()))

    def test_local_partial_configuration_uses_production_probe_cleanup(self):
        harness=getup.GetUpTests();backend=harness.probe_backend();backend['refuse_getup']=True
        lua=harness.probe_runtime(backend,True);lua.execute('update();update();update()')
        self.assertEqual((backend['stops'],backend['loads']),(1,0));self.assertGreater(backend['status_reads'],0)
        self.assertEqual(backend['claim']['calls'],0)
        self.assertTrue(any('PLAYER_ORIGIN_STOP ' in line and 'schema=2 ' in line for line in lua.globals().logs.values()))

    def runtime(self,raw):
        lua=LuaRuntime(unpack_returned_tuples=True);lua.globals().raw=raw
        lua.execute("""
          logs={};DebugError=function(s) logs[#logs+1]=s end
          X4LAN_CONFIG={slot='p1',native_gate_player_probe='diagnostic-getup',native_gate_diagnostic_contract='diagnostic-getup-1',token=string.rep('a',32)}
          gate={player_getup_diagnostic_status=function() return raw end}
        """)
        lua.globals().run=hashlib.sha256(b'a'*32).hexdigest();lua.execute('X4LAN_SHA256={digest=function() return run end}')
        lua.execute(SOURCE);return lua

    def test_wait_until_native_terminal_then_preserve_failed_raw_footer(self):
        raw=re.search(r'GETUP_DIAGNOSTIC_STOP ([^\n]+)',fixture()[0])[1]
        lua=self.runtime(raw.replace('terminal_finished=1','terminal_finished=0'))
        self.assertFalse(lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,false)'));self.assertEqual(len(lua.globals().logs),0)
        failed=raw.replace('failures=0','failures=1').replace('restored=1','restored=0')
        lua.globals().raw=failed
        self.assertTrue(lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,false)'))
        self.assertEqual(list(lua.globals().logs.values()),['[X4LAN] NATIVE_GATE GETUP_DIAGNOSTIC_STOP '+failed])
        self.assertTrue(lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,false)'));self.assertEqual(len(lua.globals().logs),1)

    def test_closed_native_footer_fields_and_claims(self):
        raw=re.search(r'GETUP_DIAGNOSTIC_STOP ([^\n]+)',fixture()[0])[1]
        for before,after in [('session_pass=0','session_pass=1'),('native_clients=1','native_clients=2'),('epoch=1','epoch=01'),
                             ('physical_slot=1','physical_slot=2'),('options=70377334178981','options=1'),('failures=0','unknown=0'),
                             ('reason=1','reason=4294967296'),('closed=1','closed=1 closed=1')]:
            with self.subTest(before=before):
                lua=self.runtime(raw.replace(before,after))
                with self.assertRaises(Exception):lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,false)')
                self.assertEqual(len(lua.globals().logs),0)
                self.assertTrue(lua.eval('X4LAN_NATIVE_DIAGNOSTIC.drain(gate,true)'))
                self.assertIn('GETUP_DIAGNOSTIC_DRAIN_ERROR',lua.globals().logs[1])

if __name__=='__main__':unittest.main()
