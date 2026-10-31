"""Physics trace and body order: probe arming, row drains, stop, refusals and offline option checks."""
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
BASE = 'X4LAN_CONFIG.native_gate_snapshot_sha = "%s"\n' % ('a'*64)
IDENTITIES = ('X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_created_identities = true\n'
              'X4LAN_CONFIG.native_gate_portable_created = true\n')
MOCKS = '''
  local gate=package.loadlib('gate')();order={};finals={}
  gate.created_identities=function() return 1 end
  gate.created_identities_status=function() return 'armed=1' end
  gate.created_row=function() end
  gate.portable_created=function() order[#order+1]='portable';return 1 end
  gate.physics_trace=function(rows) order[#order+1]='trace:'..rows;return trace_native or 1 end
  gate.physics_trace_status=function() return 'installed=1 rows=40 foreign=0 stray=0 stage=armed' end
  gate.physics_trace_row=function(kind,i,final)
    if final==1 then finals[#finals+1]=kind end
    if i<2 then return 'index='..i..' tick='..state.issued..' kind='..kind end
  end
  gate.body_order=function(mode) order[#order+1]='order:'..mode;return order_native or 1 end
  gate.body_order_status=function() return 'mode=sort installed=1 stage=armed' end
  gate.body_order_row=function(i,final) if i<1 then return 'index='..i..' tick=1 mode=sort pos=0 order=0' end end
  gate.shape_order=function(mode) order[#order+1]='shape:'..tostring(mode);return shape_native or 1 end
  gate.shape_order_status=function() return 'installed=1 moves=0 stage=armed' end
  gate.shape_order_row=function(i) if i<1 then return 'index='..i..' tick=1 from=8 to=0 children=9' end end
  gate.nav_octree=function(mode,delay,snap) order[#order+1]='octree:'..tostring(mode)..':'..tostring(delay)..':'..tostring(snap);return octree_native or 1 end
  gate.nav_octree_status=function() return 'mode=sync installed=1 builds=0 stage=armed' end
  gate.nav_octree_row=function(i) if i<2 then return 'index='..i..' tick=1 key=80000f09105ebb8a klass=1 flag='..i..' joined=1 us=5' end end
'''


class PhysicsProbeTests(unittest.TestCase):
    setUp = probe.SnapshotProbeTests.setUp

    def configure(self, lines):
        self.lua.execute(lines+MOCKS)

    def test_arms_after_created_keys_drains_rows_every_tick_and_at_stop(self):
        self.configure('''
          X4LAN_CONFIG.native_gate_world_identities=true
          X4LAN_CONFIG.native_gate_created_identities=true
          X4LAN_CONFIG.native_gate_portable_created=true
          X4LAN_CONFIG.native_gate_physics_trace=40
          X4LAN_CONFIG.native_gate_body_order='sort'
        ''')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1)
        self.assertEqual(list(self.lua.globals().order.values()), ['portable', 'trace:40', 'order:sort'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('PHYSICS_TRACE_ARMED rows=40' in line for line in lines))
        self.assertTrue(any('BODY_ORDER_ARMED mode=sort' in line for line in lines))
        for name in ('PHYSICS_STEP', 'PHYSICS_BODY', 'PHYSICS_CONTACT'):
            self.assertEqual(sum(f'NATIVE_GATE {name} index=' in line for line in lines), 2, name)  # drained once, cursor kept
        self.assertEqual(sum('NATIVE_GATE BODY_ORDER_ROW index=0' in line for line in lines), 1)
        self.assertTrue(any('PHYSICS_TRACE tick=1 installed=1' in line for line in lines))
        self.assertTrue(any('BODY_ORDER tick=2 mode=sort' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))
        self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})")
        self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('PHYSICS_TRACE_STOP installed=1' in line for line in lines))
        self.assertTrue(any('BODY_ORDER_STOP mode=sort' in line for line in lines))
        self.assertEqual(sorted(set(self.lua.globals().finals.values())), [0, 1, 2])  # the stop drain takes the last tick's rows

    def test_shape_order_arms_logs_rows_and_stops(self):
        self.configure('X4LAN_CONFIG.native_gate_shape_order=true')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1);self.assertEqual(list(self.lua.globals().order.values()), ['shape:place'])
        self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
        self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
        self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})");self.lua.execute('update()')
        lines = list(self.lua.globals().logs.values())
        self.assertTrue(any('SHAPE_ORDER_ARMED mode=place' in line for line in lines))
        self.assertTrue(any('SHAPE_ORDER tick=1 installed=1' in line for line in lines))
        self.assertEqual(sum('NATIVE_GATE SHAPE_ORDER_ROW index=0 tick=1 from=8 to=0 children=9' in line for line in lines), 1)
        self.assertTrue(any('SHAPE_ORDER_STOP installed=1' in line for line in lines))
        self.assertFalse(any('ERROR' in line for line in lines))

    def test_nav_octree_arms_logs_rows_and_stops(self):
        cases = (("'sync'", 'sync', 'octree:sync:0:0', 'mode=sync'), ("'observe'", 'observe', 'octree:observe:0:0', 'mode=observe'),
                 ("'deadline'", 'deadline', 'octree:deadline:30:0', 'mode=deadline delay=30'),
                 ("'deadline';X4LAN_CONFIG.native_gate_nav_octree_delay=12", 'deadline', 'octree:deadline:12:0', 'mode=deadline delay=12'),
                 ("'deadline';X4LAN_CONFIG.native_gate_nav_octree_snapshot=true", 'deadline', 'octree:deadline:30:1',
                  'mode=deadline delay=30 snapshot=1'),
                 ("nil;X4LAN_CONFIG.native_gate_nav_octree_snapshot=true", 'observe', 'octree:observe:0:1', 'mode=observe snapshot=1'))
        for config, mode, call, armed in cases:
            with self.subTest(config=config):
                self.setUp()
                self.configure("X4LAN_CONFIG.native_gate_nav_octree=%s" % config)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 1);self.assertEqual(list(self.lua.globals().order.values()), [call])
                self.lua.execute('state.boot_phase=3;state.issued=1;is_menu=false')
                self.lua.execute(probe.SOURCE);self.lua.execute('update();state.issued=2;update()')
                self.lua.execute("state.active=0;state.completed=2;setmetatable(state,{__index=function() return 0 end})");self.lua.execute('update()')
                lines = list(self.lua.globals().logs.values())
                self.assertTrue(any(line.endswith('NAV_OCTREE_ARMED '+armed) for line in lines), armed)
                self.assertTrue(any('NAV_OCTREE tick=1 mode=sync installed=1' in line for line in lines))
                self.assertEqual(sum('NATIVE_GATE NAV_OCTREE_ROW index=' in line for line in lines), 2)  # drained once, cursor kept
                self.assertTrue(any('NAV_OCTREE_ROW index=1 tick=1 key=80000f09105ebb8a klass=1 flag=1 joined=1' in line for line in lines))
                self.assertTrue(any('NAV_OCTREE_STOP mode=sync' in line for line in lines))
                self.assertFalse(any('ERROR' in line for line in lines))

    def test_shape_order_wait_mode_is_passed_through(self):
        self.configure("X4LAN_CONFIG.native_gate_shape_order='wait'")
        self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
        self.assertEqual(self.lua.globals().loads, 1);self.assertEqual(list(self.lua.globals().order.values()), ['shape:wait'])
        self.assertTrue(any('SHAPE_ORDER_ARMED mode=wait' in line for line in self.lua.globals().logs.values()))

    def test_refusals(self):
        cases = (
            ('X4LAN_CONFIG.native_gate_physics_trace=40', 'physics trace refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=true', 'physics trace refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=-1', 'physics trace refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=1.5', 'physics trace refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_physics_trace=40;trace_native=0', 'physics trace refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_created_identities=true;'
             "X4LAN_CONFIG.native_gate_body_order='sort'", 'body order refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_created_identities=true;'
             "X4LAN_CONFIG.native_gate_portable_created=true;X4LAN_CONFIG.native_gate_body_order='yes'", 'body order refused'),
            ('X4LAN_CONFIG.native_gate_world_identities=true;X4LAN_CONFIG.native_gate_created_identities=true;'
             "X4LAN_CONFIG.native_gate_portable_created=true;X4LAN_CONFIG.native_gate_body_order='sort';order_native=0", 'body order refused'),
            ("X4LAN_CONFIG.native_gate_shape_order='yes'", 'shape order refused'),
            ('X4LAN_CONFIG.native_gate_shape_order=true;shape_native=0', 'shape order refused'),
            ("X4LAN_CONFIG.native_gate_shape_order='WAIT'", 'shape order refused'),
            ("X4LAN_CONFIG.native_gate_nav_octree=true", 'nav octree refused'),
            ("X4LAN_CONFIG.native_gate_nav_octree='SYNC'", 'nav octree refused'),
            ("X4LAN_CONFIG.native_gate_nav_octree='sync';octree_native=0", 'nav octree refused'),
            ("X4LAN_CONFIG.native_gate_nav_octree='deadline';octree_native=0", 'nav octree refused'),
        )
        for config, why in cases:
            with self.subTest(config=config):
                self.setUp()
                self.configure(config)
                self.lua.execute(probe.SOURCE);self.lua.execute('update();update();update()')
                self.assertEqual(self.lua.globals().loads, 0)
                self.assertTrue(any(why in line for line in self.lua.globals().logs.values()), why)


class PhysicsOptionTests(unittest.TestCase):
    def failures(self, text, name):
        return [f['rule'] for f in options.check(text)[1] if name in f['rule']]

    def test_shape_order_is_true_with_the_snapshot(self):
        for value in ('true', "'wait'", '"sync"', 'false', 'nil'):
            self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_shape_order = %s\n' % value, 'shape_order'), [], value)
        self.assertEqual(len(self.failures(BASE+"X4LAN_CONFIG.native_gate_shape_order = 'on'\n", 'shape_order')), 1)
        self.assertEqual(len(self.failures('X4LAN_CONFIG.native_gate_shape_order = true\n', 'shape_order')), 1)

    def test_nav_octree_modes_need_world_identities_and_the_snapshot(self):
        on = BASE+'X4LAN_CONFIG.native_gate_world_identities = true\n'
        for value in ("'observe'", '"sync"', "'deadline'", 'false', 'nil'):
            self.assertEqual(self.failures(on+'X4LAN_CONFIG.native_gate_nav_octree = %s\n' % value, 'nav_octree'), [], value)
        for value in ('true', 'nil', 'false'):
            self.assertEqual(self.failures(on+'X4LAN_CONFIG.native_gate_nav_octree_snapshot = %s\n' % value, 'nav_octree_snapshot'), [], value)
        self.assertEqual(len(self.failures(on+"X4LAN_CONFIG.native_gate_nav_octree_snapshot = 'yes'\n", 'nav_octree_snapshot')), 1)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_nav_octree_snapshot = true\n', 'nav_octree_snapshot'),
                         ['native_gate_nav_octree_snapshot needs native_gate_world_identities'])
        for clients in ('3', '@X4LANE_CLIENTS@'):
            self.assertEqual(self.failures(on+"X4LAN_CONFIG.native_gate_clients = %s\nX4LAN_CONFIG.native_gate_nav_octree = 'agreed'\n" % clients,
                                           'nav_octree'), [], clients)
        self.assertEqual(self.failures(on+"X4LAN_CONFIG.native_gate_nav_octree = 'agreed'\n", 'nav_octree'),
                         ["native_gate_nav_octree = 'agreed' needs the protocol 3 input wire (native_gate_clients)"])
        deadline = on+"X4LAN_CONFIG.native_gate_nav_octree = 'deadline'\n"
        for value in ('1', '30', '3600', 'nil'):
            self.assertEqual(self.failures(deadline+'X4LAN_CONFIG.native_gate_nav_octree_delay = %s\n' % value, 'nav_octree'), [], value)
        for value in ('0', '3601', '1.5', "'30'", 'true', '030'):
            self.assertEqual(self.failures(deadline+'X4LAN_CONFIG.native_gate_nav_octree_delay = %s\n' % value, 'nav_octree'),
                             ['native_gate_nav_octree_delay must be a tick count 1..3600'], value)
        self.assertEqual(self.failures(on+"X4LAN_CONFIG.native_gate_nav_octree = 'sync'\nX4LAN_CONFIG.native_gate_nav_octree_delay = 30\n",
                                       'nav_octree'), ["native_gate_nav_octree_delay needs native_gate_nav_octree = 'deadline'"])
        for value in ('true', "'on'", "'SYNC'"):
            self.assertEqual(len(self.failures(on+'X4LAN_CONFIG.native_gate_nav_octree = %s\n' % value, 'nav_octree')), 1, value)
        self.assertEqual(self.failures(BASE+"X4LAN_CONFIG.native_gate_nav_octree = 'sync'\n", 'nav_octree'),
                         ['native_gate_nav_octree needs native_gate_world_identities'])
        self.assertEqual(self.failures("X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_nav_octree = 'sync'\n",
                                       'nav_octree'), ['native_gate_nav_octree needs the snapshot boot'])

    def test_physics_trace_is_a_tick_count_with_world_identities(self):
        on = BASE+'X4LAN_CONFIG.native_gate_world_identities = true\n'
        for value in ('0', '40', '1000000', 'nil'):
            self.assertEqual(self.failures(on+'X4LAN_CONFIG.native_gate_physics_trace = %s\n' % value, 'physics_trace'), [], value)
        for value in ('-1', '1.5', '1000001', 'true', "'8'", '08'):
            self.assertEqual(len(self.failures(on+'X4LAN_CONFIG.native_gate_physics_trace = %s\n' % value, 'physics_trace')), 1, value)
        self.assertEqual(self.failures(BASE+'X4LAN_CONFIG.native_gate_physics_trace = 40\n', 'physics_trace'),
                         ['native_gate_physics_trace needs native_gate_world_identities'])
        self.assertEqual(self.failures('X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_physics_trace = 40\n',
                                       'physics_trace'), ['native_gate_physics_trace needs the snapshot boot'])

    def test_body_order_modes_and_identities(self):
        for value in ("'sort'", '"observe"', 'false', 'nil'):
            self.assertEqual(self.failures(BASE+IDENTITIES+'X4LAN_CONFIG.native_gate_body_order = %s\n' % value, 'body_order'), [], value)
        self.assertEqual(len(self.failures(BASE+IDENTITIES+"X4LAN_CONFIG.native_gate_body_order = 'on'\n", 'body_order')), 1)
        self.assertEqual(len(self.failures(BASE+IDENTITIES+'X4LAN_CONFIG.native_gate_body_order = true\n', 'body_order')), 1)
        partial = BASE+'X4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_created_identities = true\n'
        self.assertEqual(len(self.failures(partial+"X4LAN_CONFIG.native_gate_body_order = 'sort'\n", 'body_order')), 1)
        self.assertEqual(len(self.failures(IDENTITIES+"X4LAN_CONFIG.native_gate_body_order = 'sort'\n", 'body_order')), 1)


if __name__ == '__main__':
    unittest.main()
