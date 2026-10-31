"""Nav octree invalidation sites: probe arming after the octree module, rows at the first tick and the stop, refusals."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_native_gate_probe as probe

MOCKS = '''
  local gate=package.loadlib('gate')();order={}
  gate.nav_octree=function(mode,delay) order[#order+1]='octree:'..tostring(mode);return 1 end
  gate.nav_octree_status=function() return 'mode=deadline installed=1 stage=armed' end
  gate.nav_octree_row=function() return nil end
  gate.nav_octree_sites=function(...) assert(select('#',...)==0);order[#order+1]='sites';return sites_native or 1 end
  gate.nav_octree_sites_status=function() return 'armed=1 active=1 entries=2 overflow=0 load=7450d3:0,745255:0,753510:0,784691:0,tail:5,other:0' end
  site_rows={[0]='index=0 phase=load site=tail from=75356e class=02b7feb8 thread=main count=5 noid=0 last=s:00000000000a7351',
             [1]='index=1 free',[2]='index=2 phase=run site=745255 from=74525a class=02b66080 thread=moveworker count=1 noid=0 last=-:0'}
  gate.nav_octree_site_row=function(i) return site_rows[i] end
'''


class NavOctreeSitesProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def test_arms_after_the_octrees_logs_rows_at_the_first_tick_and_at_stop(self):
        self.lua.execute("X4LAN_CONFIG.native_gate_nav_octree='deadline';X4LAN_CONFIG.native_gate_nav_octree_sites=true"+MOCKS)
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(list(self.lua.globals().order.values()), ['octree:deadline', 'sites'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any(line.endswith('NAV_OCTREE_SITES_ARMED') for line in lines))
        self.assertTrue(any('NAV_OCTREE_SITES tick=1 armed=1 active=1 entries=2' in line for line in lines))
        self.assertTrue(any('NAV_OCTREE_SITES tick=2 armed=1' in line for line in lines))
        self.assertEqual(sum('NAV_OCTREE_SITE at=first index=0 phase=load site=tail from=75356e' in line for line in lines), 1)
        self.assertEqual(sum('NAV_OCTREE_SITE at=first index=2 phase=run' in line for line in lines), 1)
        self.assertFalse(any('index=1 free' in line for line in lines))
        self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})");self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('NAV_OCTREE_SITES_STOP armed=1' in line for line in lines))
        self.assertEqual(sum('NAV_OCTREE_SITE at=stop index=0 phase=load' in line for line in lines), 1)
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_refusals(self):
        for config in ("X4LAN_CONFIG.native_gate_nav_octree='sync';X4LAN_CONFIG.native_gate_nav_octree_sites=true",
                       "X4LAN_CONFIG.native_gate_nav_octree='deadline';X4LAN_CONFIG.native_gate_nav_octree_sites='all'",
                       "X4LAN_CONFIG.native_gate_nav_octree_sites=true",
                       "X4LAN_CONFIG.native_gate_nav_octree='deadline';X4LAN_CONFIG.native_gate_nav_octree_sites=true;sites_native=0"):
            with self.subTest(config=config):
                self.setUp()
                self.lua.execute(config+MOCKS)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any('nav octree sites refused' in line for line in self.lua.globals().logs.values()), config)


if __name__ == '__main__':
    unittest.main()
