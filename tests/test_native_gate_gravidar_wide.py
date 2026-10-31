"""Gravidar wide-range applies (native_gate_gravidar_wide): probe arming, refusals and
status lines (LuaJIT, mock gate). The deferral itself is covered by
src/native/engine/test_lockstep_gravidar_wide.h; no X4 is involved here."""
from pathlib import Path
import sys
import unittest

from lupa.luajit21 import LuaRuntime

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_gravidar_watch as watch

HARNESS = watch.HARNESS.replace("native_gate_gravidar_watch={'000000000009fb3b','000000000009f121'}}",
                                'native_gate_gravidar_wide=true}')
MOCKS = '''
  armed=0
  gate.gravidar_wide=function(...) assert(select('#',...)==1);armed_mode=...;armed=armed+1;return wide_native or 1 end
  gate.gravidar_wide_status=function() return 'armed=1 installed=1 active=1 restored=0 calls=4 kept=1 entries=1 direct=3 flushes=1 late=1' end
'''


def lua(config=''):
    runtime = LuaRuntime(unpack_returned_tuples=True)
    runtime.execute(HARNESS)
    runtime.execute(MOCKS)
    runtime.execute(config)
    return runtime


class ProbeTests(unittest.TestCase):
    def test_arms_once_and_logs_status_every_admitted_tick(self):
        self.assertIn('native_gate_gravidar_wide=true', HARNESS)
        runtime = lua()
        runtime.execute(watch.PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().armed, 1)
        runtime.execute('state.boot_phase=3;is_menu=false')
        runtime.execute(watch.PROBE)
        runtime.execute('update();state.issued=1;update();update()')
        logs = list(runtime.globals().logs.values())
        self.assertTrue(any(line.endswith('GRAVIDAR_WIDE_ARMED mode=wide') for line in logs))
        self.assertEqual(runtime.globals().armed_mode, 'wide')
        self.assertTrue(any('GRAVIDAR_WIDE tick=1 armed=1 installed=1 active=1' in line for line in logs))
        self.assertFalse(any('ERROR' in line for line in logs))

    def test_sort_passes_its_mode(self):
        runtime = lua("X4LAN_CONFIG.native_gate_gravidar_wide='sort'")
        runtime.execute(watch.PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual((runtime.globals().armed, runtime.globals().armed_mode), (1, 'sort'))
        self.assertTrue(any(line.endswith('GRAVIDAR_WIDE_ARMED mode=sort') for line in runtime.globals().logs.values()))

    def test_refusals(self):
        for config in ("X4LAN_CONFIG.native_gate_gravidar_wide='observe'", 'wide_native=0', 'gate.gravidar_wide_status=nil',
                       "X4LAN_CONFIG.native_gate_gravidar_wide='sort';X4LAN_CONFIG.native_gate_radar_serial=nil;X4LAN_CONFIG.native_gate_gravidar_key_all=nil",
                       "X4LAN_CONFIG.native_gate_gravidar_wide='sort';X4LAN_CONFIG.native_gate_world_identities=nil;X4LAN_CONFIG.native_gate_gravidar_key_all=nil"):
            with self.subTest(config=config):
                runtime = lua(config)
                runtime.execute(watch.PROBE)
                runtime.execute('update();update();update()')
                self.assertEqual(runtime.globals().loads, 0)
                self.assertTrue(any('Gravidar wide refused' in line for line in runtime.globals().logs.values()), config)


if __name__ == '__main__':
    unittest.main()
