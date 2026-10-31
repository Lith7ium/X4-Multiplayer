"""Virtual players step 1 ('observe'): probe arming before loading, per-tick status,
the one-time input handler census, the stop lines, refusals and the option rule; the
step 3 spike ('construct'): arming with its tick, one VPLAYER_SPIKE row per stage change,
refusals and option rules."""
from pathlib import Path
import importlib.util
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_probe as probe

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('check_gate_options', ROOT/'tools/dev/check_gate_options.py')
options = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(options)

MOCKS = '''
  local gate=package.loadlib('gate')();arm_calls={};map_calls=0
  gate.virtual_players=function(mode,due) arm_calls[#arm_calls+1]=mode..(mode=='construct' and ':'..tostring(due) or '');return native_result or 1 end
  spike=nil
  gate.virtual_players_status=function()
    return 'mode=observe stage=observing passes='..state.issued..' sim=00000000000000aa'..(spike and spike() or '')
  end
  gate.virtual_players_map=function()
    map_calls=map_calls+1
    if state.issued<2 then return nil end
    return 'census=1 nodes=5 action.flight=1 state.flight=2 list=state:unknown:0x99:123456'
  end
  gate.virtual_players_regions=function() return 'bank_rot_throttle:sim:3:2:9' end
  gate.virtual_players_undeclared=function() return '2f4b9d3+2' end
  gate.virtual_players_neighbours=function() return 'zone_move:3,pathing_buffer_index:3' end
  gate.virtual_players_hashes=function() return 'bank_device_rot:00000000000000a1' end
  gate.virtual_players_raw=function(cls) return cls=='config' and 'c0' or 'f0' end
'''


class VirtualPlayersProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def config(self, extra=''):
        self.lua.execute("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_virtual_players='observe'"+extra+MOCKS)

    def test_arms_before_loading_logs_status_census_once_and_stop_lines(self):
        self.config()
        self.lua.execute('local load=LoadGame;LoadGame=function(slot) assert(#arm_calls==1);load(slot) end')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(list(self.lua.globals().arm_calls.values()), ['observe'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE)  # loading-UI reload: the identical arm runs again
        self.lua.execute('update();state.issued=2;update();state.issued=3;update()')
        self.assertEqual(list(self.lua.globals().arm_calls.values()), ['observe', 'observe'])
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any(line.endswith('VPLAYER_ARMED mode=observe') for line in lines))
        for tick in (1, 2, 3):
            self.assertTrue(any(f'VPLAYER tick={tick} mode=observe stage=observing passes={tick}' in line for line in lines), tick)
        maps = [line for line in lines if 'VPLAYER_MAP' in line]
        self.assertEqual(len(maps), 1)
        self.assertIn('VPLAYER_MAP tick=2 census=1 nodes=5 action.flight=1 state.flight=2 list=state:unknown:0x99:123456', maps[0])
        self.assertEqual(self.lua.globals().map_calls, 2)  # nil at tick 1, logged at tick 2, never asked again
        self.lua.execute('state.active=0;state.completed=3;setmetatable(state,{__index=function() return 0 end})');self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('VPLAYER_STOP mode=observe stage=observing' in line for line in lines))
        self.assertTrue(any('VPLAYER_REGIONS bank_rot_throttle:sim:3:2:9' in line for line in lines))
        self.assertTrue(any('VPLAYER_UNDECLARED 2f4b9d3+2' in line for line in lines))
        self.assertTrue(any('VPLAYER_NEIGHBOURS zone_move:3,pathing_buffer_index:3' in line for line in lines))
        # Per-region hashes at tick 1 and at stop; raw config/option bytes at tick 1 and at stop.
        self.assertEqual(sum('VPLAYER_HASHES' in line and 'bank_device_rot:00000000000000a1' in line for line in lines), 2)
        self.assertEqual(sum('VPLAYER_RAW' in line and 'class=config c0 option=f0' in line for line in lines), 2)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_off_by_default(self):
        self.lua.execute(MOCKS)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update()')
        self.assertEqual(len(self.lua.globals().arm_calls), 0)
        self.assertFalse(any('VPLAYER' in line for line in self.lua.globals().logs.values()))

    def test_refusals_before_loading(self):
        cases = ["X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_virtual_players=true",
                 "X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_virtual_players='scope'",
                 "X4LAN_CONFIG.native_gate_virtual_players='observe'",
                 "X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_virtual_players='observe';native_result=0"]
        for case in cases:
            with self.subTest(case=case):
                self.setUp()
                self.lua.execute(case+MOCKS)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any('virtual players refused' in line for line in self.lua.globals().logs.values()), case)
        self.setUp()
        self.lua.execute("X4LAN_CONFIG.native_gate_serial_pathing=true;X4LAN_CONFIG.native_gate_virtual_players='observe'"+MOCKS+
                         "package.loadlib('gate')().virtual_players=nil")
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 0)


    def test_construct_arms_with_its_tick_and_logs_each_spike_stage_once(self):
        self.config(";X4LAN_CONFIG.native_gate_virtual_players='construct';X4LAN_CONFIG.native_gate_virtual_players_construct_tick=3")
        self.lua.execute('''spike=function()
          local stage=state.issued<3 and 'waiting' or state.issued<5 and 'constructed' or 'failed'
          return ' spike=construct spike_stage='..stage..' why='..(stage=='failed' and 'singleton_changed' or '-')..' at=3 due=3'
        end''')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE)
        for tick in range(1, 7):
            self.lua.execute(f'state.issued={tick};update()')
        self.assertEqual(list(self.lua.globals().arm_calls.values()), ['construct:3', 'construct:3'])
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any(line.endswith('VPLAYER_ARMED mode=construct tick=3') for line in lines))
        spikes = [line[line.index('VPLAYER_SPIKE'):] for line in lines if 'VPLAYER_SPIKE' in line]
        self.assertEqual(spikes, ['VPLAYER_SPIKE tick=1 spike=construct spike_stage=waiting why=- at=3 due=3',
                                  'VPLAYER_SPIKE tick=3 spike=construct spike_stage=constructed why=- at=3 due=3',
                                  'VPLAYER_SPIKE tick=5 spike=construct spike_stage=failed why=singleton_changed at=3 due=3'])
        self.assertFalse(any('ERROR' in line for line in lines))
        # The default tick is 20.
        self.setUp();self.config(";X4LAN_CONFIG.native_gate_virtual_players='construct'")
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(list(self.lua.globals().arm_calls.values()), ['construct:20'])

    def test_construct_refusals_before_loading(self):
        for extra in ("X4LAN_CONFIG.native_gate_virtual_players='construct';X4LAN_CONFIG.native_gate_virtual_players_construct_tick=1",
                      "X4LAN_CONFIG.native_gate_virtual_players='construct';X4LAN_CONFIG.native_gate_virtual_players_construct_tick=18001",
                      "X4LAN_CONFIG.native_gate_virtual_players='construct';X4LAN_CONFIG.native_gate_virtual_players_construct_tick=2.5",
                      "X4LAN_CONFIG.native_gate_virtual_players='observe';X4LAN_CONFIG.native_gate_virtual_players_construct_tick=20",
                      "X4LAN_CONFIG.native_gate_virtual_players='construct';native_result=0"):
            with self.subTest(extra=extra):
                self.setUp()
                self.lua.execute("X4LAN_CONFIG.native_gate_serial_pathing=true;"+extra+MOCKS)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any('virtual players refused' in line for line in self.lua.globals().logs.values()), extra)


class VirtualPlayersOptionTests(unittest.TestCase):
    BASE = 'X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64)

    def failures(self, text):
        return [f['rule'] for f in options.check(text)[1] if 'virtual_players' in f['rule']]

    def test_observe_needs_serial_pathing_and_the_snapshot_boot(self):
        serial = 'X4LAN_CONFIG.native_gate_serial_pathing = true\n'
        self.assertEqual(self.failures(self.BASE+serial+"X4LAN_CONFIG.native_gate_virtual_players = 'observe'\n"), [])
        self.assertEqual(len(self.failures(self.BASE+"X4LAN_CONFIG.native_gate_virtual_players = 'observe'\n")), 1)
        self.assertEqual(len(self.failures(serial+"X4LAN_CONFIG.native_gate_virtual_players = 'observe'\n")), 1)
        for value in ('true', "'scope'", "'OBSERVE'"):
            with self.subTest(value=value):
                self.assertEqual(len(self.failures(self.BASE+serial+'X4LAN_CONFIG.native_gate_virtual_players = %s\n' % value)), 1)
        self.assertEqual(self.failures(self.BASE+'X4LAN_CONFIG.native_gate_virtual_players = false\n'), [])

    def test_construct_and_its_tick(self):
        serial = 'X4LAN_CONFIG.native_gate_serial_pathing = true\n'
        construct = "X4LAN_CONFIG.native_gate_virtual_players = 'construct'\n"
        self.assertEqual(self.failures(self.BASE+serial+construct), [])
        self.assertEqual(self.failures(self.BASE+serial+construct+'X4LAN_CONFIG.native_gate_virtual_players_construct_tick = 600\n'), [])
        for tick in ('1', '18001', '2.5', "'20'"):
            with self.subTest(tick=tick):
                self.assertEqual(self.failures(self.BASE+serial+construct+'X4LAN_CONFIG.native_gate_virtual_players_construct_tick = %s\n' % tick),
                                 ['native_gate_virtual_players_construct_tick must be an admitted tick 2..18000'])
        self.assertEqual(self.failures(self.BASE+serial+"X4LAN_CONFIG.native_gate_virtual_players = 'observe'\n"
                                       'X4LAN_CONFIG.native_gate_virtual_players_construct_tick = 20\n'),
                         ["native_gate_virtual_players_construct_tick needs native_gate_virtual_players = 'construct'"])


if __name__ == '__main__':
    unittest.main()
