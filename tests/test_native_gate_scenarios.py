"""Census scenario fixtures (missions, highway) and other-start snapshot tooling.

LuaJIT runs the real probe and capture script against mocks; MD files are checked
structurally (and against the game schema with X4LAN_MD_SCHEMA=1). No X4 is involved:
this proves schedules, refusals and tooling, not native behaviour.
"""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
import xml.etree.ElementTree as ET

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
DEV = ROOT / 'tools/dev'
PROBE = (DEV / 'native_gate_probe.lua').read_text(encoding='utf-8')
CAPTURE = (DEV / 'native_gate_snapshot.lua').read_text(encoding='utf-8')
MISSIONS = DEV / 'native_gate_missions.xml'
HIGHWAY = DEV / 'native_gate_highway.xml'
spec = importlib.util.spec_from_file_location('native_gate_scenarios', DEV / 'native_gate_scenarios.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
try:
    from lxml import etree
except ImportError:
    etree = None
RE = Path(os.environ.get('X4_RE', ROOT.parent / 'x4-re'))
MD_XSD = RE / 'gamescripts/resolved/base/libraries/md.xsd'

HARNESS = '''
  X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='battle',
    native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32)}
  X4LAN_SHA256={digest=function() return string.rep('b',64) end}
  logs={};loads=0;reports=0;boots=0;is_menu=true;callbacks={};raised={}
  state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
  local gate={boot=function() boots=boots+1;return 0 end,poll=function() return state end,
    load_requested=function() state.boot_phase=1;return 1 end,
    report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
    stop=function() state.active=0;return 1 end}
  package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
  local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
      IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end,
      IsComponentOperational=function() return false end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function(name,fn) callbacks[name]=fn end
  SetScript=function(name,fn) update=fn end
  GetSaveList=function() return {{filename='save_009'}} end
  LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
  ConvertIDTo64Bit=function(object) return object end
  AddUITriggeredEvent=function(screen,control,param)
    local copy={};for k,v in pairs(param or {}) do copy[k]=v end
    raised[#raised+1]={screen=screen,control=control,param=copy}
  end
'''

MISSION_TICKS = {32: ['MissionSetup'], 34: ['MissionOffer'], 300: ['MissionAccept'], 320: ['MissionVisit'],
                 340: ['MissionHunt'], 1500: ['MissionCleanup']}


def lua_with(config=''):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(HARNESS)
    lua.execute(config)
    return lua


def schedule(lua, last):
    events = lua.globals().X4LAN_NATIVE_SCENARIOS.events
    out = {}
    for tick in range(0, last + 1):
        rows = events(tick)
        if len(rows):
            out[tick] = [(row[1], dict(row[2].items())) for row in rows.values()]
    return out


def expected_missions(last):
    out = {}
    for tick in range(0, last + 1):
        controls = list(MISSION_TICKS.get(tick, []))
        if tick >= 120 and tick % 120 == 0:
            controls.append('MissionSample')
        if controls:
            out[tick] = [(c, {'tick': tick}) for c in controls]
    return out


def expected_highway(last, mode='warp', super_=False, target='highway'):
    leave, super_at = (2430, 3030) if mode == 'fly' else (930, 1230)
    out = {}
    for tick in range(0, last + 1):
        rows = []
        if tick == 40:
            rows.append(('HighwayStart', {'tick': tick, 'mode': mode, 'target': target}))
        if tick == 100:
            rows.append(('HighwayEnter', {'tick': tick}))
        if tick == leave:
            rows.append(('HighwayLeave', {'tick': tick}))
        if super_ and tick == super_at:
            rows.append(('HighwaySuper', {'tick': tick}))
        if super_ and tick == super_at + 60:
            rows.append(('HighwaySuperEnter', {'tick': tick}))
        if tick >= 120 and tick % 60 == 0:
            rows.append(('HighwaySample', {'tick': tick}))
        if rows:
            out[tick] = rows
    return out


class ProbeScheduleTests(unittest.TestCase):
    def test_default_configuration_raises_no_scenario_events(self):
        lua = lua_with()
        lua.execute(PROBE)
        self.assertEqual(schedule(lua, 5000), {})
        self.assertIsNone(lua.globals().X4LAN_NATIVE_SCENARIOS.text)

    def test_missions_raise_each_cue_once_at_its_fixed_tick(self):
        lua = lua_with('X4LAN_CONFIG.native_gate_missions=true')
        lua.execute(PROBE)
        self.assertEqual(schedule(lua, 5000), expected_missions(5000))
        self.assertEqual(lua.globals().X4LAN_NATIVE_SCENARIOS.text, 'missions=true highway=false super=false target=none')

    def test_highway_variants_raise_their_fixed_ticks(self):
        for value, mode, super_, target in (('true', 'warp', False, 'highway'), ("'warp'", 'warp', True, 'highway'),
                                            ("'fly'", 'fly', False, 'beacon'), ("'fly'", 'fly', True, 'highway')):
            with self.subTest(value=value, super_=super_, target=target):
                config = f'X4LAN_CONFIG.native_gate_highway={value}'
                if super_:
                    config += ';X4LAN_CONFIG.native_gate_highway_super=true'
                if target != 'highway':
                    config += f";X4LAN_CONFIG.native_gate_highway_target='{target}'"
                lua = lua_with(config)
                lua.execute(PROBE)
                self.assertEqual(schedule(lua, 5000), expected_highway(5000, mode, super_, target))
                self.assertEqual(lua.globals().X4LAN_NATIVE_SCENARIOS.text,
                                 f'missions=false highway={mode} super={str(super_).lower()} target={target}')

    def test_invalid_or_conflicting_configurations_refuse_before_booting(self):
        for config in ("native_gate_missions='yes'", 'native_gate_missions=1', "native_gate_highway='super'",
                       'native_gate_highway_super=true', "native_gate_highway_target='beacon'",
                       "native_gate_highway=true;X4LAN_CONFIG.native_gate_highway_target='gate'",
                       "native_gate_highway=true;X4LAN_CONFIG.native_gate_highway_super='true'",
                       "native_gate_missions=true;X4LAN_CONFIG.native_gate_hash='clock'",
                       'native_gate_missions=true;X4LAN_CONFIG.native_gate_snapshot_sha=nil',
                       'native_gate_missions=true;X4LAN_CONFIG.native_gate_highway=true',
                       'native_gate_missions=true;X4LAN_CONFIG.native_gate_player_tour=true',
                       'native_gate_highway=true;X4LAN_CONFIG.native_gate_player_tour=true',
                       'native_gate_highway=true;X4LAN_CONFIG.native_gate_pilot_inputs=true',
                       "native_gate_highway=true;X4LAN_CONFIG.native_gate_player_probe='observe'",
                       'native_gate_highway=true;X4LAN_CONFIG.native_gate_player_orders=true'):
            with self.subTest(config=config):
                lua = lua_with('X4LAN_CONFIG.' + config)
                with self.assertRaises(LuaError):
                    lua.execute(PROBE)
                self.assertEqual((lua.globals().boots, lua.globals().loads), (0, 0))
        # Explicit false values are the same as absent options.
        lua = lua_with('X4LAN_CONFIG.native_gate_missions=false;X4LAN_CONFIG.native_gate_highway=false;'
                       'X4LAN_CONFIG.native_gate_highway_super=false')
        lua.execute(PROBE)
        self.assertEqual(schedule(lua, 2000), {})

    def run_battle(self, lua, ticks):
        lua.execute(PROBE)
        lua.execute('update();update();update()')
        lua.execute('state.boot_phase=3;is_menu=false')
        lua.execute(PROBE)
        lua.execute('''
          for i=1,12 do callbacks['X4LAN_NativeGate.Actor'..i](nil,100+i) end
          callbacks['X4LAN_NativeGate.Ready'](nil,12)
        ''')
        for tick in ticks:
            lua.execute(f'state.issued={tick};update()')
        logs = list(lua.globals().logs.values())
        self.assertFalse(any('ERROR' in line for line in logs), [line for line in logs if 'ERROR' in line][:3])
        raised = [(r.screen, r.control, dict(r.param.items())) for r in lua.globals().raised.values()]
        return raised, logs

    def test_admitted_battle_frames_raise_the_scenario_events_in_order(self):
        lua = lua_with('X4LAN_CONFIG.native_gate_missions=true')
        raised, logs = self.run_battle(lua, range(1, 401))
        scenario = [(control, param['tick']) for screen, control, param in raised if control.startswith('Mission')]
        expected = [(c, t) for t, rows in sorted(expected_missions(400).items()) for c, _ in rows]
        self.assertEqual(scenario, expected)
        self.assertTrue(all(screen == 'X4LAN_NativeGate' for screen, _, _ in raised))
        self.assertTrue(any('NATIVE_GATE SCENARIO missions=true highway=false' in line for line in logs))
        lua = lua_with("X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_target='beacon'")
        raised, _ = self.run_battle(lua, range(1, 181))
        start = [param for _, control, param in raised if control == 'HighwayStart']
        self.assertEqual(start, [{'tick': 40, 'mode': 'fly', 'target': 'beacon'}])
        self.assertEqual([c for _, c, _ in raised if c.startswith('Highway')],
                         ['HighwayStart', 'HighwayEnter', 'HighwaySample', 'HighwaySample'])


class HighwayTraceTests(unittest.TestCase):
    def test_trace_logs_exact_player_and_convoy_poses_inside_its_window(self):
        lua = lua_with("X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_target='beacon';"
                       'X4LAN_CONFIG.native_gate_highway_trace={100,102}')
        lua.execute('''
          local C=require('ffi').C
          C.IsComponentOperational=function(id) return id==7 or id==201 or id==202 end
          C.GetPlayerOccupiedShipID=function() return 7 end
          C.GetObjectPositionInSector=function(id) return {x=id+0.5,y=-2,z=0.1,yaw=1,pitch=0,roll=-0.25} end
          C.IsAutoPilotActive=function() return true end
          C.IsFlightAssistActive=function() return false end
          C.GetBoostEnergyPercentage=function() return 75.5 end
        ''')
        lua.execute(PROBE)
        lua.execute('update();update();update()')
        lua.execute('state.boot_phase=3;is_menu=false')
        lua.execute(PROBE)
        lua.execute('''
          for i=1,12 do callbacks['X4LAN_NativeGate.Actor'..i](nil,100+i) end
          callbacks['X4LAN_NativeGate.Ready'](nil,12)
          for i=1,3 do callbacks['X4LAN_NativeGate.HighwayShip'..i](nil,200+i) end
        ''')
        for tick in range(98, 105):
            lua.execute(f'state.issued={tick};update()')
        lines = [line.split('NATIVE_GATE ')[1] for line in lua.globals().logs.values() if 'HIGHWAY_TRACE' in line]
        self.assertEqual(len(lines), 15)
        self.assertEqual({int(re.search(r'tick=(\d+)', line)[1]) for line in lines}, {100, 101, 102})
        self.assertEqual(lines[0], 'HIGHWAY_TRACE tick=100 who=player pose=0x1.ep+2,-0x1p+1,0x1.999999999999ap-4/0x1p+0,0x0p+0,-0x1p-2 '
                                   'ap=1 fa=0 boost=0x1.2ep+6')
        self.assertEqual(lines[1:5], ['HIGHWAY_TRACE tick=100 who=c1 pose=0x1.93p+7,-0x1p+1,0x1.999999999999ap-4/0x1p+0,0x0p+0,-0x1p-2',
                                      'HIGHWAY_TRACE tick=100 who=c2 pose=0x1.95p+7,-0x1p+1,0x1.999999999999ap-4/0x1p+0,0x0p+0,-0x1p-2',
                                      'HIGHWAY_TRACE tick=100 who=c3 pose=gone', 'HIGHWAY_TRACE tick=100 who=c4 pose=pending'])
        self.assertFalse(any('ERROR' in line for line in lua.globals().logs.values()))

    def test_trace_configuration_refuses_without_highway_or_with_bad_windows(self):
        for config in ('X4LAN_CONFIG.native_gate_highway_trace={1,10}',
                       "X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_trace={10,1}",
                       "X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_trace={0,10}",
                       "X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_trace={1,4000}",
                       "X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_trace={1.5,10}",
                       "X4LAN_CONFIG.native_gate_highway='fly';X4LAN_CONFIG.native_gate_highway_trace=10"):
            with self.subTest(config=config):
                lua = lua_with(config)
                with self.assertRaises(LuaError):
                    lua.execute(PROBE)
        self.assertEqual(tool.scenario_lines('highway', 'fly', False, 'beacon', (100, 600))[0][-1],
                         'X4LAN_CONFIG.native_gate_highway_trace = {100,600}')
        for args in (('highway', 'fly', False, 'beacon', (600, 100)), ('missions', 'warp', False, 'highway', (1, 2))):
            with self.subTest(args=args), self.assertRaises(ValueError):
                tool.scenario_lines(*args)


def md_root(path):
    return ET.fromstring(path.read_text(encoding='utf-8'))


def ui_controls(root):
    out = set()
    for event in root.iter('event_ui_triggered'):
        assert event.get('screen') == "'X4LAN_NativeGate'"
        out.add(event.get('control').strip("'"))
    return out


class MdScriptTests(unittest.TestCase):
    def test_missions_md_handles_every_scheduled_cue_and_uses_stock_libraries(self):
        root = md_root(MISSIONS)
        self.assertEqual(root.get('name'), 'X4LAN_NativeMissions')
        self.assertEqual(ui_controls(root), {c for _, rows in expected_missions(2000).items() for c, _ in rows})
        refs = {c.get('ref') for c in root.iter('cue') if c.get('ref')}
        self.assertEqual(refs, {'md.GM_Destroy_Objects.Start', 'md.RML_FlyTo.FlyTo'})
        self.assertEqual([s.get('cue') for s in root.iter('signal_cue')], ['md.GenericMissions.EvaluateSectorMissions'])
        accepts = [s for s in root.iter('signal_objects')]
        self.assertEqual({s.get('param') for s in accepts}, {"'accept'"})
        params = {p.get('name'): p.get('value') for c in root.iter('cue') if c.get('ref') == 'md.GM_Destroy_Objects.Start'
                  for p in c.iter('param')}
        self.assertEqual(params['RewardCr'], '25000Cr')
        self.assertEqual(params['RemoveOnSectorChange'], 'false')
        self.assertNotIn('WithoutOffer', params)  # the stock offer and accept path is exercised

    def test_highway_md_handles_every_scheduled_cue_and_drives_the_stock_autopilot(self):
        root = md_root(HIGHWAY)
        self.assertEqual(root.get('name'), 'X4LAN_NativeHighway')
        expected = {c for _, rows in expected_highway(5000, 'warp', True).items() for c, _ in rows}
        self.assertEqual(ui_controls(root), expected)
        autopilot = list(root.iter('start_player_autopilot'))
        self.assertEqual(len(autopilot), 4)
        self.assertTrue(all(a.get('useunknown') == 'true' for a in autopilot))
        text = HIGHWAY.read_text(encoding='utf-8')
        self.assertIn('macro.highway02_cluster_14_sector001_macro', text)
        self.assertIn('macro.cluster_01_sector002_macro', text)
        for order in root.iter('create_order'):
            if order.get('id') == "'MoveWait'":
                self.assertIn(('precise', 'true'), [(p.get('name'), p.get('value')) for p in order.iter('param')])

    def test_fixtures_avoid_unkeyed_placement_and_log_with_their_prefix(self):
        seeds = []
        for path, kind in ((MISSIONS, 'MISSION'), (HIGHWAY, 'HIGHWAY')):
            text = path.read_text(encoding='utf-8')
            root = md_root(path)
            for word in ('get_safe_pos', 'randomoffset', 'find_closest', 'chance='):
                self.assertNotIn(word, text)
            lines = [d.get('text') for d in root.iter('debug_text')]
            self.assertTrue(lines)
            self.assertTrue(all(line.startswith(f"'[X4LAN] NATIVE_GATE {kind} ") for line in lines), lines)
            seeds += [s.get('seed') for s in root.iter('create_ship')]
        # Fixed seeds, distinct from each other and from the 6v6 (4711+) and big battle (9000+) fixtures.
        self.assertTrue(all(s and re.fullmatch(r'9[5-9]\d\d( \+ \$i)?', s) for s in seeds), seeds)
        self.assertEqual(len(seeds), len(set(seeds)))
        bases = (MISSIONS.read_text() + HIGHWAY.read_text())
        self.assertNotIn('seed="9000', bases)

    # Compiling the game schema takes about a minute: opt in with X4LAN_MD_SCHEMA=1 (lxml + x4-re).
    @unittest.skipUnless(os.environ.get('X4LAN_MD_SCHEMA') == '1' and etree is not None and MD_XSD.is_file(),
                         'set X4LAN_MD_SCHEMA=1 with lxml and the read-only x4-re md.xsd')
    def test_md_validates_against_game_schema(self):
        schema = etree.XMLSchema(etree.parse(str(MD_XSD)))
        for path in (MISSIONS, HIGHWAY):
            with self.subTest(path=path.name):
                self.assertTrue(schema.validate(etree.parse(str(path))), schema.error_log)


BASE_CONFIG = '''X4LAN_CONFIG.enabled = false
X4LAN_CONFIG.native_gate_test = true
X4LAN_CONFIG.native_gate_hash = "battle"
X4LAN_CONFIG.native_gate_snapshot_sha = "%s"
X4LAN_CONFIG.native_gate_snapshot_fixture = {'0000000000176a13'}
X4LAN_CONFIG.native_gate_world_hash_stride = 60
X4LAN_CONFIG.native_gate_world_hash_watch = {'0000000000097720','00000000000979c1'}
X4LAN_CONFIG.native_gate_big_war = true
X4LAN_CONFIG.native_gate_missions = false
''' % ('a' * 64)


def base_overlay(directory, config=BASE_CONFIG):
    base = Path(directory) / 'base'
    for sub in ('ui', 'native', 'md'):
        (base / sub).mkdir(parents=True)
    (base / 'ui/config.lua.append').write_text(config)
    (base / 'ui/native_gate_probe.lua').write_text('-- old probe\n')
    (base / 'native/x4lan_gate_64.dll').write_bytes(b'dll')
    (base / 'md/native_gate_battle.xml').write_text('<mdscript/>\n')
    (base / 'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
    (base / 'x4lane-manifest.json').write_text('{}\n')
    return base


def lua_accepts(config_text, extra=''):
    """Execute the generated config lines into the harness and load the real probe."""
    lua = lua_with()
    lua.execute('local X=X4LAN_CONFIG\nX4LAN_CONFIG={}\n' + config_text +
                '\nfor k,v in pairs(X4LAN_CONFIG) do X[k]=v end\nX4LAN_CONFIG=X\n' + extra)
    lua.execute(PROBE)
    return lua


class ToolTests(unittest.TestCase):
    def test_config_lines_are_accepted_by_the_probe_with_matching_tick_counts(self):
        for scenario, mode, super_, target, ticks in (('missions', 'warp', False, 'highway', 1800),
                                                      ('highway', 'warp', False, 'highway', 1500),
                                                      ('highway', 'warp', True, 'beacon', 2700),
                                                      ('highway', 'fly', False, 'highway', 3000),
                                                      ('highway', 'fly', True, 'highway', 4500)):
            with self.subTest(scenario=scenario, mode=mode, super_=super_, target=target):
                lines, count = tool.scenario_lines(scenario, mode, super_, target)
                self.assertEqual(count, ticks)
                self.assertEqual(lines[0], tool.HEADER)
                lua = lua_accepts('\n'.join(line for line in lines if not line.startswith('--')))
                events = schedule(lua, ticks)
                actions = [tick for tick, rows in events.items() if any(not c.endswith('Sample') for c, _ in rows)]
                self.assertTrue(actions)
                self.assertLessEqual(max(actions), ticks - 300)  # at least 5 s to observe the last action
        for args in (('missions', 'fly'), ('missions', 'warp', True), ('highway', 'drive'), ('highway', 'warp', False, 'gate'), ('tour',)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                tool.scenario_lines(*args)

    def test_overlay_adds_md_probe_and_config_and_refreshes_the_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            base = base_overlay(directory)
            lines, _ = tool.scenario_lines('highway', 'warp', True)
            out = tool.build_overlay('highway', base, Path(directory) / 'out', lines)
            config = (out / 'ui/config.lua.append').read_text()
            self.assertNotIn('native_gate_missions', config)
            self.assertTrue(config.endswith('X4LAN_CONFIG.native_gate_highway = "warp"\nX4LAN_CONFIG.native_gate_highway_super = true\n'))
            self.assertIn('native_gate_big_war = true', config)
            self.assertEqual((out / 'md/native_gate_highway.xml').read_bytes(), HIGHWAY.read_bytes())
            self.assertEqual((out / 'ui/native_gate_probe.lua').read_text(encoding='utf-8'), PROBE)
            manifest = json.loads((out / 'x4lane-manifest.json').read_text())
            files = {str(f.relative_to(out)).replace('\\', '/') for f in out.rglob('*') if f.is_file()} - {'x4lane-manifest.json'}
            self.assertEqual(set(manifest), files)
            for name, sha in manifest.items():
                self.assertEqual(hashlib.sha256((out / name).read_bytes()).hexdigest(), sha)
            # Rebuilding on the scenario overlay replaces the block instead of stacking it.
            lines2, _ = tool.scenario_lines('missions')
            again = tool.build_overlay('missions', out, Path(directory) / 'again', lines2)
            config2 = (again / 'ui/config.lua.append').read_text()
            self.assertEqual(config2.count(tool.HEADER), 1)
            self.assertNotIn('native_gate_highway', config2)
            with self.assertRaises(ValueError):
                tool.build_overlay('missions', base, out, lines2)  # existing output

    def test_control_overlay_strips_the_scenario_and_its_md(self):
        with tempfile.TemporaryDirectory() as directory:
            base = base_overlay(directory)
            lines, _ = tool.scenario_lines('highway', 'fly', False, 'beacon')
            scenario = tool.build_overlay('highway', base, Path(directory) / 'hw', lines)
            self.assertTrue((scenario / 'md/native_gate_highway.xml').is_file())
            self.assertEqual(tool.scenario_lines('none'), ([], 1500))
            control = tool.build_overlay('none', scenario, Path(directory) / 'control', [])
            config = (control / 'ui/config.lua.append').read_text()
            self.assertNotIn('native_gate_highway', config)
            self.assertNotIn(tool.HEADER, config)
            self.assertFalse((control / 'md/native_gate_highway.xml').exists())
            self.assertTrue((control / 'md/native_gate_battle.xml').is_file())
            self.assertEqual(set(json.loads((control / 'x4lane-manifest.json').read_text())),
                             {str(f.relative_to(control)).replace('\\', '/') for f in control.rglob('*') if f.is_file()} - {'x4lane-manifest.json'})
            with self.assertRaises(ValueError):
                tool.scenario_lines('none', 'fly')

    def test_overlay_adds_speech_game_time_and_replaces_it(self):
        with tempfile.TemporaryDirectory() as directory:
            base = base_overlay(directory)
            lines, _ = tool.scenario_lines('highway', 'fly', False, 'beacon')
            out = tool.build_overlay('highway', base, Path(directory) / 'out', lines, speech_game_time=True)
            config = (out / 'ui/config.lua.append').read_text()
            self.assertTrue(config.endswith('X4LAN_CONFIG.native_gate_speech_game_time = true\n'))
            lua_accepts('X4LAN_CONFIG.native_gate_speech_game_time = true')
            again = tool.build_overlay('none', out, Path(directory) / 'again', [])
            self.assertNotIn('speech_game_time', (again / 'ui/config.lua.append').read_text())

    def test_overlay_adds_the_gravidar_watch_and_gate_dll(self):
        with tempfile.TemporaryDirectory() as directory:
            base = base_overlay(directory, BASE_CONFIG + 'X4LAN_CONFIG.native_gate_gravidar_key_all = true\n')
            dll = Path(directory) / 'gate.dll'
            dll.write_bytes(b'new gate')
            lines, _ = tool.scenario_lines('missions')
            out = tool.build_overlay('missions', base, Path(directory) / 'out', lines, gate_dll=dll,
                                     watch=('000000000009fb3b', '000000000009f121'))
            config = (out / 'ui/config.lua.append').read_text()
            self.assertTrue(config.endswith("X4LAN_CONFIG.native_gate_gravidar_watch = {'000000000009fb3b','000000000009f121'}\n"))
            self.assertEqual((out / 'native/x4lan_gate_64.dll').read_bytes(), b'new gate')
            again = tool.build_overlay('missions', out, Path(directory) / 'again', lines)
            self.assertNotIn('gravidar_watch', (again / 'ui/config.lua.append').read_text())
            lua_accepts('\n'.join(line for line in config.splitlines() if line.startswith('X4LAN_CONFIG.native_gate_gravidar')),
                        'X4LAN_CONFIG.native_gate_radar_serial=true;X4LAN_CONFIG.native_gate_world_identities=true')
            for watch in (('9fb3b',), ('000000000009fb3b',) * 2, tuple(f'{i:016x}' for i in range(1, 10))):
                with self.subTest(watch=watch), self.assertRaises(ValueError):
                    tool.build_overlay('missions', base, Path(directory) / 'bad', lines, watch=watch)
            with self.assertRaises(ValueError):
                tool.build_overlay('missions', base_overlay(Path(directory) / 'nokey'), Path(directory) / 'bad', lines,
                                   watch=('000000000009fb3b',))
            with self.assertRaises(ValueError):
                tool.build_overlay('missions', base, Path(directory) / 'bad', lines, gate_dll=Path(directory) / 'missing.dll')

    def test_overlay_refuses_bases_the_fixture_excludes(self):
        lines, _ = tool.scenario_lines('highway')
        for change in ('X4LAN_CONFIG.native_gate_player_tour = true', 'X4LAN_CONFIG.native_gate_pilot_inputs = true',
                       'X4LAN_CONFIG.native_gate_player_orders = true'):
            with self.subTest(change=change), self.assertRaises(ValueError):
                tool.merged_config(BASE_CONFIG + change + '\n', 'highway', lines)
        self.assertIn('native_gate_missions = true',
                      tool.merged_config(BASE_CONFIG + 'X4LAN_CONFIG.native_gate_pilot_inputs = true\n', 'missions',
                                         tool.scenario_lines('missions')[0]))
        for text in (BASE_CONFIG.replace('"battle"', '"clock"'), BASE_CONFIG.replace('native_gate_snapshot_sha', 'x')):
            with self.assertRaises(ValueError):
                tool.merged_config(text, 'missions', lines)
        with tempfile.TemporaryDirectory() as directory:
            base = base_overlay(directory)
            (base / 'native/x4lan_gate_64.dll').unlink()
            with self.assertRaises(ValueError):
                tool.build_overlay('missions', base, Path(directory) / 'out', lines)

    def test_compare_strips_client_log_prefixes_and_reports_the_first_divergence(self):
        def log(rows, prefix):
            return '\n'.join(['[General] unrelated line'] + [f'{prefix} [X4LAN] {row}' for row in rows]) + '\n'
        rows = ['NATIVE_GATE SCENARIO missions=true highway=false super=false target=none',
                'NATIVE_GATE MISSION setup tick=32 ready=1 targets=4 hunters=2 client=1 sector=cluster_14_sector001_macro money=10000',
                'NATIVE_GATE MISSION sample tick=120 money=10000 generic=7']
        one = log(rows, '[Scripts] 7.30 *** Context:md.X4LAN_NativeMissions.Missions<inst:14edbb>:')
        two = log(rows, '[Scripts] 7.30 *** Context:md.X4LAN_NativeMissions.Missions<inst:9a01ff>:')
        ok, details = tool.compare([one, two])
        self.assertTrue(ok, details)
        self.assertIn('3 identical lines on 2 clients', details)
        self.assertIn('MISSION setup=1', details)
        ok, details = tool.compare([one, two.replace('generic=7', 'generic=8')])
        self.assertFalse(ok)
        self.assertIn('client 2 differs at line 3', details)
        ok, details = tool.compare([one, log(rows[:2], '[x]')])
        self.assertFalse(ok)
        self.assertIn('client 2 has 2 lines', details)


class SessionCompareTests(unittest.TestCase):
    def test_lines_after_the_gated_stop_are_not_compared(self):
        # sc-highway2/sc-highway-fly: the clients run ungated after STOP; their post-STOP
        # zone/activity lines differed by one tick and were reported as a divergence.
        def log(after):
            return '\n'.join(['[Scripts] 1.0 *** Context:md.X: [X4LAN] NATIVE_GATE HIGHWAY sample tick=1500 pos=1,2,3',
                              '[X4LAN] NATIVE_GATE STOP slot=p1 completed=1500 reason=1 restored=1',
                              f'[Scripts] 2.0 *** Context:md.X: [X4LAN] NATIVE_GATE HIGHWAY zone ms={after} who=player']) + '\n'
        ok, details = tool.compare([log(35308), log(35327)])
        self.assertTrue(ok, details)
        self.assertIn('1 identical lines', details)
        self.assertIn('client 1 stop=1500 after=1, client 2 stop=1500 after=1', details)
        lines, stop, after = tool.session(log(1))
        self.assertEqual((lines, stop, after), (['NATIVE_GATE HIGHWAY sample tick=1500 pos=1,2,3'], 1500, 1))
        ok, details = tool.compare([log(1).replace('pos=1,2,3', 'pos=1,2,4'), log(1)])
        self.assertFalse(ok)
        self.assertIn('client 2 differs at line 1', details)
        # Traces and event-timed lines are separate streams: a speech line that lands at another
        # game time differs in the event stream only, the per-tick traces still match.
        def mixed(speech_ms):
            return '\n'.join(['[X4LAN] NATIVE_GATE HIGHWAY_TRACE tick=180 who=player pose=0x1p+0',
                              f'[Scripts] *** Context:md.X: [X4LAN] NATIVE_GATE HIGHWAY speech ms={speech_ms} actor=Betty page=10002 line=920',
                              '[X4LAN] NATIVE_GATE HIGHWAY_TRACE tick=181 who=player pose=0x1p+0',
                              '[X4LAN] NATIVE_GATE STOP slot=p1 completed=181 reason=1']) + '\n'
        ok, details = tool.compare([mixed(10066), mixed(10116)])
        self.assertFalse(ok)
        self.assertIn("events: client 2 differs at line 1: 'NATIVE_GATE HIGHWAY speech ms=10066", details)
        self.assertNotIn('trace:', details)
        ok, details = tool.compare([mixed(10116), mixed(10116)])
        self.assertTrue(ok, details)
        # Without a STOP line every line counts (a run that never stopped cleanly).
        self.assertEqual(tool.session('[X4LAN] NATIVE_GATE MISSION sample tick=1\n'), (['NATIVE_GATE MISSION sample tick=1'], None, 0))


def synthetic_save(directory, player_ship=0xae79c, start='x4ep1_gamestart_fight'):
    ships = ''.join(f'<component id="[0x{0xa000 + i:x}]" class="ship_s" owner="{"argon" if i <= 6 else "xenon"}" '
                    f'macro="{"ship_arg_s_fighter_01_a_macro" if i <= 6 else "ship_xen_s_fighter_01_a_macro"}">'
                    '<movement class="navigation"/></component>' for i in range(1, 13))
    player = (f'<component class="ship_s" id="[0x{player_ship:x}]" macro="ship_par_s_fighter_01_a_macro">'
              '<component class="cockpit" id="[0xae79f]"><component class="player" id="[0x11e12]"/></component></component>')
    values = ''.join(f'<value type="component" value="[0x{0xa000 + i:x}]"/>' for i in range(1, 13))
    xml = (f'<savegame><info><game id="X4" version="900" build="611726" time="7.1" start="{start}" seed="276944566"/></info>'
           '<universe><component class="cluster" id="[0x5]"><component class="sector" macro="cluster_47_sector001_macro" id="[0xc01]">'
           f'<component class="zone" id="[0xd01]">{ships}{player}</component></component></component></universe>'
           f'<script><refs type="list"><ref id="77">{values}</ref></refs></script>'
           '<md><script name="X4LAN_NativeGate"><cue name="Spawn" state="complete"><vars><value name="$ships" type="list" value="77"/>'
           '</vars></cue></script></md></savegame>')
    path = Path(directory) / 'save_009.xml.gz'
    with gzip.open(path, 'wt') as stream:
        stream.write(xml)
    return path


class StartSnapshotTests(unittest.TestCase):
    def test_capture_md_changes_only_the_gamestart_guard(self):
        original = (DEV / 'native_gate_battle.xml').read_text(encoding='utf-8').splitlines()
        for gamestart in tool.RECOMMENDED:
            variant = tool.capture_battle_md(gamestart).splitlines()
            self.assertEqual(len(variant), len(original))
            changed = [(a, b) for a, b in zip(original, variant) if a != b]
            self.assertEqual(len(changed), 1)
            self.assertEqual(changed[0][1], changed[0][0].replace("'custom_creative'", f"'{gamestart}'"))
        for gamestart in ('x4ep1_gamestart_trade', 'x4ep1_gamestart_boron1', "x' or true or '", ''):
            with self.subTest(gamestart=gamestart), self.assertRaises(ValueError):
                tool.capture_battle_md(gamestart)
        self.assertTrue(set(tool.RECOMMENDED) <= set(tool.STARTS))

    def test_capture_overlay_has_the_snapshot_script_variant_md_and_gamestart_config(self):
        with tempfile.TemporaryDirectory() as directory:
            dll = Path(directory) / 'fixed.dll'
            dll.write_bytes(b'fixed')
            out = tool.build_capture_overlay('x4ep1_gamestart_terran1', dll, Path(directory) / 'cap')
            self.assertEqual((out / 'ui.xml.files').read_text(), 'ui/native_gate_snapshot.lua\n')
            self.assertEqual((out / 'ui/native_gate_snapshot.lua').read_text(encoding='utf-8'), CAPTURE)
            self.assertEqual((out / 'native/x4lan_fixedtick_64.dll').read_bytes(), b'fixed')
            self.assertIn("player.module == 'x4ep1_gamestart_terran1'", (out / 'md/native_gate_battle.xml').read_text(encoding='utf-8'))
            config = (out / 'ui/config.lua.append').read_text()
            self.assertIn('X4LAN_CONFIG.native_gate_snapshot = "capture"', config)
            self.assertIn('X4LAN_CONFIG.native_gate_snapshot_gamestart = "x4ep1_gamestart_terran1"', config)
            self.assertEqual(set(json.loads((out / 'x4lane-manifest.json').read_text())),
                             {'md/native_gate_battle.xml', 'native/x4lan_fixedtick_64.dll', 'ui/native_gate_snapshot.lua',
                              'ui/config.lua.append', 'ui.xml.files'})
            with self.assertRaises(ValueError):
                tool.build_capture_overlay('x4ep1_gamestart_terran1', dll, out)
            with self.assertRaises(ValueError):
                tool.build_capture_overlay('x4ep1_gamestart_terran1', Path(directory) / 'missing.dll', Path(directory) / 'x')

    def test_snapshot_lines_bind_checksum_fixture_and_watch_list(self):
        with tempfile.TemporaryDirectory() as directory:
            path = synthetic_save(directory)
            lines, header = tool.snapshot_lines(path)
            self.assertEqual(header['start'], 'x4ep1_gamestart_fight')
            self.assertEqual(header['player'], {'ship': '00000000000ae79c', 'macro': 'ship_par_s_fighter_01_a_macro',
                                                'sector': 'cluster_47_sector001_macro'})
            sha = hashlib.sha256(path.read_bytes()).hexdigest()
            fixture = [f'{0xa000 + i:016x}' for i in range(1, 13)]
            self.assertEqual(lines[0], f'X4LAN_CONFIG.native_gate_snapshot_sha = "{sha}"')
            self.assertEqual(lines[1], 'X4LAN_CONFIG.native_gate_snapshot_fixture = {' + ','.join(repr(s) for s in fixture) + '}')
            self.assertEqual(lines[2], 'X4LAN_CONFIG.native_gate_world_hash_watch = {' +
                             ','.join(repr(s) for s in fixture + ['00000000000ae79c']) + '}')
            rebased = tool.rebased_config(BASE_CONFIG, lines)
            self.assertIn(lines[0], rebased)
            self.assertIn(lines[1], rebased)
            self.assertIn(lines[2], rebased)
            self.assertNotIn("'0000000000176a13'", rebased)
            self.assertNotIn('00000000000979c1', rebased)
            self.assertIn('native_gate_big_war = true', rebased)
            self.assertEqual(len(tool.parse_config(rebased)), len(tool.parse_config(BASE_CONFIG)))
            # The probe accepts the rebased snapshot identity (lane wrapper matches it textually).
            lua_accepts(rebased, 'X4LAN_CONFIG.native_gate_world_hash_stride=nil')
            without_stride = BASE_CONFIG.replace('X4LAN_CONFIG.native_gate_world_hash_stride = 60\n', '') \
                                        .replace("X4LAN_CONFIG.native_gate_world_hash_watch = {'0000000000097720','00000000000979c1'}\n", '')
            self.assertNotIn('world_hash_watch', tool.rebased_config(without_stride, lines))
            for key in ('native_gate_player_origin_token', 'native_gate_npc_pilot'):
                with self.assertRaises(ValueError):
                    tool.rebased_config(BASE_CONFIG + f'X4LAN_CONFIG.{key} = 1\n', lines)
            out, _ = tool.build_rebased(base_overlay(directory), path, Path(directory) / 'rebased')
            self.assertIn(lines[0], (out / 'ui/config.lua.append').read_text())

    def test_snapshot_without_a_player_ship_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = synthetic_save(directory)
            text = gzip.open(path, 'rt').read().replace('class="ship_s" id="[0xae79c]"', 'class="station" id="[0xae79c]"')
            with gzip.open(path, 'wt') as stream:
                stream.write(text)
            with self.assertRaises(ValueError):
                tool.snapshot_header(path)


CAPTURE_HARNESS = '''
  X4LAN_CONFIG={enabled=false,test_profile='45841',native_gate_snapshot='capture'}
  logs={};raised={};callbacks={};cutscene=true;started='x4ep1_gamestart_fight';now=7
  package.loadlib=function(path) assert(path:find('fixedtick'));return function() return {start=function() return 1 end} end end
  local ffi={cdef=function() end,string=function(s) return s end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,IsPlayerValid=function() return true end,
      IsGamePaused=function() return false end,GetCurrentGameTime=function() return now end,
      IsSaveListLoadingComplete=function() return true end,ReloadSaveList=function() end,
      IsFullscreenCutsceneActive=function() return cutscene end,GetGameStartName=function() return started end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function(name,fn) callbacks[name]=fn end
  SetScript=function(name,fn) update=fn end
  AddUITriggeredEvent=function(screen,control) raised[#raised+1]=control end
'''


class CaptureScriptTests(unittest.TestCase):
    def capture(self, config=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(CAPTURE_HARNESS)
        lua.execute(config)
        lua.execute(CAPTURE)
        return lua

    def test_capture_waits_for_the_intro_and_checks_the_gamestart(self):
        lua = self.capture("X4LAN_CONFIG.native_gate_snapshot_gamestart='x4ep1_gamestart_fight'")
        lua.execute('update();update();update()')
        self.assertEqual(len(lua.globals().raised), 0)  # fullscreen intro still running
        lua.execute('cutscene=false;update()')
        self.assertEqual(list(lua.globals().raised.values()), ['Spawn'])
        logs = list(lua.globals().logs.values())
        self.assertTrue(any('NATIVE_SNAPSHOT GAMESTART id=x4ep1_gamestart_fight time=7' in line for line in logs))
        self.assertFalse(any('ERROR' in line for line in logs))

    def test_capture_refuses_another_gamestart_and_invalid_configuration(self):
        lua = self.capture("X4LAN_CONFIG.native_gate_snapshot_gamestart='x4ep1_gamestart_terran1'")
        lua.execute('cutscene=false;update();update()')
        self.assertEqual(len(lua.globals().raised), 0)
        self.assertTrue(any('ERROR' in line and 'gamestart mismatch x4ep1_gamestart_fight' in line for line in lua.globals().logs.values()))
        # Without the option the custom_creative capture behaves as before.
        lua = self.capture()
        lua.execute('cutscene=false;update()')
        self.assertEqual(list(lua.globals().raised.values()), ['Spawn'])
        for value in ('1', "'bad id'", 'true'):
            with self.subTest(value=value), self.assertRaises(LuaError):
                self.capture('X4LAN_CONFIG.native_gate_snapshot_gamestart=' + value)


if __name__ == '__main__':
    unittest.main()
