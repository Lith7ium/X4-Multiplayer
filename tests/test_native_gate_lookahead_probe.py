"""LuaJIT probe checks for the protocol 4 lookahead window (native_gate_lookahead)."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime, LuaError

SOURCE = (Path(__file__).resolve().parents[1]/'tools/dev/native_gate_probe.lua').read_text()


class LookaheadProbeTests(unittest.TestCase):
    def setUp(self, config="slot='p3',native_gate_clients=3,native_gate_lookahead=2"):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_test=true,test_profile='45841',%s,
            native_gate_hash='clock',native_gate_snapshot_sha=string.rep('a',64),
            port=12345,token=string.rep('c',32)}
          X4LAN_SHA256={digest=function(data) hashed=data;return string.rep('b',64) end}
          logs={};loads=0;boots=0;is_menu=true;calls={};reports={}
          state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0',wire_version=3,clients=3,inputs={'','',''},lookahead=2}
          local gate={boot=function(slot) boots=boots+1;calls[#calls+1]='boot';return 0 end,
            poll=function() return state end,
            load_requested=function() state.boot_phase=1;return 1 end,
            report=function(tick,hash,blob) reports[#reports+1]=tick..':'..tostring(blob);state.report_tick=tick;return 1 end,
            propose=function(tick,blob) return 1 end,
            lookahead_status=function() return 'lookahead=2 ready=1 ahead_max=3 idle=0 resends=0 stale=0 duplicates=0' end,
            stop=function() state.active=0;return 1 end}
          gate.wire=function(version,clients) calls[#calls+1]='wire '..version..'/'..clients;return 1 end
          gate.lookahead=function(k) calls[#calls+1]='lookahead '..k;return lookahead_result or 1 end
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

    def admitted(self, tick=1, inputs="{'','',''}", extra=''):
        """Boot, load, reload the UI and run one admitted tick."""
        self.lua.execute(SOURCE)
        self.lua.execute('update();update();update()')
        self.lua.execute(f'state.boot_phase=3;state.issued={tick};is_menu=false;state.inputs={inputs};{extra}')
        self.lua.execute(SOURCE)
        self.lua.execute('update()')

    def test_window_is_configured_after_the_wire_before_boot_and_after_reload(self):
        self.admitted()
        self.assertEqual(self.values('calls'), ['wire 3/3', 'lookahead 2', 'boot', 'wire 3/3', 'lookahead 2', 'boot'])
        self.assertEqual(self.values('reports'), ['1:nil'])
        self.assertTrue(any(line.endswith('ARMED slot=p3 hash_scope=clock wire=3 clients=3 lookahead=2') for line in self.logs()))
        self.assertFalse(any('ERROR' in line for line in self.logs()), self.logs())

    def test_inputs_name_the_proposal_tick_and_must_match_the_window(self):
        self.lua.execute('''X4LAN_NATIVE_INPUTS={
          apply=function(tick,inputs) applied=tick..'|'..table.concat(inputs,',');return 'ORDERS' end,
          proposal=function(tick) return nil end}''')
        self.admitted(tick=7, inputs="{'01','','0203'}", extra='state.inputs_tick=4')
        self.assertEqual(self.lua.globals().applied, '7|01,,0203')
        self.assertTrue(any('INPUTS tick=7 proposed=4 blobs=01,,0203' in line for line in self.logs()), self.logs())
        self.assertFalse(any('ERROR' in line for line in self.logs()))
        cases = (('state.inputs_tick=5', 7), ('state.inputs_tick=nil', 7), ('state.lookahead=nil', 1),
                 ('state.lookahead=1', 1), ('state.inputs_tick=0', 2))
        for extra, tick in cases:
            self.setUp()
            self.admitted(tick=tick, extra=extra)
            self.assertEqual(self.values('reports'), [], extra)
            self.assertTrue(any('ERROR' in line and 'lookahead mismatch' in line for line in self.logs()), (extra, self.logs()))

    def test_dll_without_window_or_refusing_it_never_boots(self):
        self.lua.execute('lookahead_result=0')
        self.lua.execute(SOURCE);self.lua.execute('update();update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED lookahead=2' in line for line in self.logs()))
        self.setUp()   # an older gate DLL: protocol 3 only
        self.lua.execute('''
          local load=package.loadlib;package.loadlib=function(path) local gate=load(path)();gate.lookahead=nil;return function() return gate end end''')
        self.lua.execute(SOURCE);self.lua.execute('update()')
        self.assertEqual(self.lua.globals().boots, 0)
        self.assertTrue(any('REFUSED lookahead=2' in line for line in self.logs()))

    def test_window_values_and_input_delays_are_checked_before_loading(self):
        bad = {"slot='p1',native_gate_lookahead=2": 'needs the protocol 3 input wire',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=9": 'must be 0..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=-1": 'must be 0..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=1.5": 'must be 0..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead='2'": 'must be 0..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=0/0": 'must be 0..8',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_pilot_inputs=true,native_gate_pilot_source='wire'":
                   'pilot delay must be at least',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=3,native_gate_pilot_inputs=true,native_gate_pilot_source='wire',"
               "native_gate_pilot_delay=4": 'pilot delay must be at least',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=3,native_gate_player_orders=true": 'player order delay must be at least',
               "slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_player_orders=true,native_gate_player_orders_delay=2":
                   'player order delay must be at least'}
        for config, message in bad.items():
            self.setUp(config)
            with self.assertRaisesRegex(LuaError, message, msg=config): self.lua.execute(SOURCE)
            self.assertEqual((self.lua.globals().boots, self.lua.globals().loads), (0, 0))
        # Delays that cover the window pass the check (the adapters are then loaded as usual).
        for config in ("slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_pilot_inputs=true,"
                       "native_gate_pilot_source='wire',native_gate_pilot_delay=4",
                       "slot='p1',native_gate_clients=2,native_gate_lookahead=1,native_gate_pilot_inputs=true,native_gate_pilot_source='wire'",
                       "slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_pilot_inputs=true,native_gate_pilot_source='script'",
                       "slot='p1',native_gate_clients=2,native_gate_lookahead=0"):
            self.setUp(config);self.lua.execute(SOURCE)
        self.setUp("slot='p1',native_gate_clients=2,native_gate_lookahead=2,native_gate_player_orders=true")
        with self.assertRaisesRegex(LuaError, 'player order adapter missing'): self.lua.execute(SOURCE)

    def test_zero_window_keeps_protocol_3_unchanged(self):
        self.setUp("slot='p3',native_gate_clients=3,native_gate_lookahead=0")
        self.lua.execute('state.lookahead=nil')
        self.admitted()
        self.assertEqual(self.values('calls'), ['wire 3/3', 'boot', 'wire 3/3', 'boot'])
        self.assertTrue(any(line.endswith('ARMED slot=p3 hash_scope=clock wire=3 clients=3') for line in self.logs()))
        self.assertFalse(any('ERROR' in line or 'LOOKAHEAD' in line for line in self.logs()))

    def test_stop_logs_the_window_status(self):
        self.admitted()
        self.lua.execute('state.active=0;update()')
        self.assertTrue(any('LOOKAHEAD_STOP lookahead=2 ready=1 ahead_max=3' in line for line in self.logs()), self.logs())


if __name__ == '__main__': unittest.main()
