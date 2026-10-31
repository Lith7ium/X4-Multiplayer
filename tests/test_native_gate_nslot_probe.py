"""LuaJIT probe checks for native input wire 3 (N clients, opaque input blobs)."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime, LuaError

SOURCE = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_probe.lua').read_text()


class WireThreeProbeTests(unittest.TestCase):
    def setUp(self, config="slot='p3',native_gate_clients=3"):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',%s,
            native_gate_hash='clock',native_gate_snapshot_sha=string.rep('a',64),
            port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
          logs={};loads=0;boots=0;is_menu=true;wires={};proposals={};reports={}
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'','',''}}
          local gate={boot=function(slot) boots=boots+1;boot_slot=slot;return 0 end,
            poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick,hash,blob) reports[#reports+1]=tick..':'..tostring(blob);state.report_tick=tick;return 1 end,
            stop=function() state.active=0;return 1 end}
          gate.wire=function(version,clients) wires[#wires+1]=version..'/'..clients;return wire_result or 1 end
          gate.propose=function(tick,blob) proposals[#proposals+1]=tick..':'..blob;return propose_result or 1 end
          package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
          local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
            C={GetSaveFolderPath=function() return 'fake/45841/save' end,
              GetCurrentGameTime=function() return 7 end,IsStartmenu=function() return is_menu end,
              ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
          require=function(name) assert(name=='ffi');return ffi end
          DebugError=function(s) logs[#logs+1]=s end
          RegisterEvent=function() end
          SetScript=function(name,fn) update=fn end
          GetSaveList=function() return {{filename='save_009'}} end
          LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
        ''' % config)

    def values(self, name):
        return list(self.lua.globals()[name].values())

    def logs(self):
        return self.values('logs')

    def admitted(self, inputs="{'','',''}"):
        """Boot, load, reload the UI and run the first admitted tick."""
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false;state.inputs='+inputs)
        self.lua.execute(SOURCE)
        self.lua.execute('update()')

    def test_wire_is_configured_before_boot_and_repeated_after_reload(self):
        self.admitted()
        g = self.lua.globals()
        self.assertEqual(self.values('wires'), ['3/3', '3/3'])
        self.assertEqual((g.boots, g.boot_slot, g.loads), (2, 3, 1))
        self.assertEqual(self.values('reports'), ['1:nil'])  # the report seals the staged (here empty) blob
        self.assertEqual(self.values('proposals'), [])
        self.assertTrue(any('ARMED slot=p3 hash_scope=clock wire=3 clients=3' in line for line in self.logs()))
        self.assertFalse(any('ERROR' in line or 'INPUTS' in line for line in self.logs()))

    def test_refused_wire_never_boots(self):
        self.lua.execute('wire_result=0')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED wire=3 clients=3' in line for line in self.logs()))
        self.setUp()  # a gate DLL without the wire 3 export
        self.lua.execute('''
          local load=package.loadlib;package.loadlib=function(path) local gate=load(path)();gate.wire=nil;return function() return gate end end''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED wire=3 clients=3' in line for line in self.logs()))

    def test_slot_and_client_count_are_checked_before_loading(self):
        for config in ("slot='p4',native_gate_clients=3", "slot='p3',native_gate_clients=9", "slot='p1',native_gate_clients=1",
                       "slot='p1',native_gate_clients=2.5", "slot='p1',native_gate_clients='3'", "slot='p3'", "slot='p9',native_gate_clients=8",
                       "slot='p03',native_gate_clients=3", "slot=3,native_gate_clients=3"):
            self.setUp(config)
            with self.assertRaises(LuaError, msg=config): self.lua.execute(SOURCE)
            self.assertEqual((self.lua.globals().boots, self.lua.globals().loads), (0, 0))
        for config in ("slot='p8',native_gate_clients=8", "slot='p2',native_gate_clients=2", "slot='p2'"):
            self.setUp(config);self.lua.execute(SOURCE)

    def test_two_player_fixtures_refuse_wire_3(self):
        for extra in ("native_gate_command_test='pvp'", "native_gate_player_probe='observe'", 'native_gate_docking_state=true',
                      'native_gate_management_test=true', 'native_gate_construction_test=true', 'native_gate_creation_rng_schema=1',
                      'native_gate_registry_creation_schema=1', 'native_gate_docking_shape_rng=true'):
            self.setUp("slot='p1',native_gate_clients=2,"+extra)
            with self.assertRaisesRegex(LuaError, 'native gate wire 2', msg=extra): self.lua.execute(SOURCE)

    def test_admitted_inputs_without_adapter_must_be_empty(self):
        self.admitted("{'','0a',''}")
        self.assertEqual(self.values('reports'), [])
        self.assertTrue(any('INPUTS tick=1 blobs=,0a,' in line for line in self.logs()))
        self.assertTrue(any('ERROR' in line and 'unexpected native inputs' in line for line in self.logs()))

    def test_adapter_applies_inputs_into_the_digest_and_proposes_through_the_gate(self):
        self.lua.execute('''X4LAN_NATIVE_INPUTS={
          apply=function(tick,inputs,clients,slot) applied=tick..'|'..table.concat(inputs,',')..'|'..clients..'|'..slot;return 'ORDERS:'..#inputs end,
          proposal=function(tick) return string.format('ff%02x',tick) end}''')
        self.admitted("{'01','','0203'}")
        g = self.lua.globals()
        self.assertEqual(g.applied, '1|01,,0203|3|3')
        self.assertIn('ORDERS:3', g.hashed)
        self.assertEqual(self.values('proposals'), ['1:ff01'])
        self.assertEqual(self.values('reports'), ['1:nil'])
        self.assertTrue(any('INPUTS tick=1 blobs=01,,0203' in line for line in self.logs()))
        self.assertFalse(any('ERROR' in line for line in self.logs()))

    def test_refused_proposal_or_malformed_admission_aborts(self):
        cases = (("propose_result=0;X4LAN_NATIVE_INPUTS={proposal=function() return '01' end}", "{'','',''}", 'proposal refused'),
                 ("X4LAN_NATIVE_INPUTS={proposal=function() return 1 end}", "{'','',''}", 'proposal refused'),
                 ('', "{'','0A',''}", 'invalid native input blob'),
                 ('', "{'','0',''}", 'invalid native input blob'),
                 ('', "{'',''}", 'wire 3 mismatch'),
                 ('state.wire_version=2', "{'','',''}", 'wire 3 mismatch'),
                 ('state.clients=4', "{'','',''}", 'wire 3 mismatch'),
                 ('', "{'','',string.rep('ab',241)}", 'invalid native input blob'))
        for setup, inputs, message in cases:
            self.setUp();self.lua.execute(setup)
            self.admitted(inputs)
            self.assertEqual(self.values('reports'), [], setup+inputs)
            self.assertTrue(any('ERROR' in line and message in line for line in self.logs()), (setup, inputs, self.logs()))

    def test_protocol_2_default_never_configures_wire_3(self):
        self.setUp("slot='p2'")
        self.lua.execute("state.wire_version=2;state.clients=2;state.inputs=nil;state.input1='00000000';state.input2='00000000'")
        self.admitted("nil")
        self.assertEqual(self.values('wires'), [])
        self.assertEqual(self.values('reports'), ['1:00000000'])
        self.assertTrue(any(line.endswith('ARMED slot=p2 hash_scope=clock') for line in self.logs()))


if __name__ == '__main__': unittest.main()
