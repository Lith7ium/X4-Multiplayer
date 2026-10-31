"""Shared co-op ship spec: static contract checks plus LuaJIT engine doubles.

These tests do not prove engine behaviour; the SHIP_SPEC debug line is the native evidence.
"""
from pathlib import Path
import importlib.util
import re
import shutil
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
from test_game_adapter import MOCK, LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
EXT = ROOT / 'src/extension/x4_lan_lab'
spec = importlib.util.spec_from_file_location('build_packages', ROOT / 'tools/distribution/build_packages.py')
build_packages = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_packages)

SHARED = '''
X4LAN_CONFIG.bubble_actions=true
X4LAN_CONFIG.ship_macro='ship_ter_s_xperimental_01_a_macro'
X4LAN_CONFIG.ship_loadout='x4lan_coop_xperimental'
X4LAN_CONFIG.ship_weapons=6
GetComponentData=function() return 'cluster_14_sector001_macro' end
requests,spawned={},{}
ConvertIDTo64Bit=tonumber
AddUITriggeredEvent=function(screen,control,value) requests[#requests+1]={screen,control,value} end
api.SpawnObjectAtPos2=function(macro,sector,p,owner)
 assert(owner=='player');spawned[#spawned+1]=macro
 if unavailable or (failSecond and #spawned==2) then return 0 end
 local n=60+#spawned;positions[n]=p
 if inertSecond and #spawned==2 then positions[n]=nil end
 return n
end
X4LAN_TRANSPORT={open=function() return {close=function() closeCount=closeCount+1 end,send=function() end} end}
X4LAN_CLIENT={new=function()
 client={lease=string.rep('a',32),update=function() return true end,render=function() return nil end}
 return client
end}
function count(name) local n=0;for _,r in ipairs(requests) do if r[2]==name then n=n+1 end end;return n end
function last(name) for i=#requests,1,-1 do if requests[i][2]==name then return requests[i][3] end end end
function logged(text) for _,v in ipairs(logs) do if v:find(text,1,true) then return true end end;return false end
'''


class ShipSpecContractTests(unittest.TestCase):
    def loadout(self):
        root = ET.parse(EXT / 'libraries/loadouts.xml').getroot()
        self.assertEqual(root.tag, 'diff')
        add = root.find('add')
        self.assertEqual(add.get('sel'), '/loadouts')
        return add.find('loadout')

    def test_loadout_is_full_and_includes_docking_computer(self):
        loadout = self.loadout()
        self.assertEqual(loadout.get('id'), build_packages.SHIP_LOADOUT)
        self.assertEqual(loadout.get('macro'), build_packages.SHIP_MACRO)
        macros = loadout.find('macros')
        weapons = sorted(e.get('path') for e in macros.findall('weapon'))
        self.assertEqual(weapons, ['../con_weapon_0%d' % i for i in range(1, 7)])
        self.assertEqual(sorted(e.get('path') for e in macros.findall('engine')), ['../con_engine_01', '../con_engine_02'])
        self.assertEqual([e.get('path') for e in macros.findall('shield')], ['../con_shield_01'])
        software = [e.get('ware') for e in loadout.find('software')]
        self.assertIn('software_dockmk2', software)
        self.assertEqual(len(software), len(set(software)))
        self.assertEqual(len(loadout.find('virtualmacros').findall('thruster')), 1)
        for element in macros:
            self.assertNotIn('optional', element.attrib, 'equipment must not silently drop')

    def test_inert_source_config_and_builder_name_the_same_spec(self):
        config = (EXT / 'ui/config.lua').read_text()
        self.assertIn('enabled = false', config)
        self.assertIn('token = ""', config)
        self.assertIn('ship_macro = "%s"' % build_packages.SHIP_MACRO, config)
        self.assertIn('ship_loadout = "%s"' % build_packages.SHIP_LOADOUT, config)
        self.assertIn('ship_weapons = %d,' % build_packages.SHIP_WEAPONS, config)
        weapons = len(self.loadout().find('macros').findall('weapon'))
        self.assertEqual(weapons, build_packages.SHIP_WEAPONS, 'expected count must equal the loadout')

    def test_built_pair_configures_identical_ship_for_both_slots(self):
        with tempfile.TemporaryDirectory() as temp:
            # Exercise current assembly before release reports exist. These
            # fixture reports cannot become production validation records.
            source = Path(temp) / 'source'
            source.mkdir()
            for name in ('VERSION.txt', 'docs/VERSIONING.txt', 'src/extension', 'src/x4lan'):
                if (ROOT / name).is_dir():
                    shutil.copytree(ROOT / name, source / name,
                                    ignore=shutil.ignore_patterns('__pycache__'))
                else:
                    (source / name).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / name, source / name)
            (source / 'reports').mkdir()
            release = build_packages.version()
            for name, suffix in (('automated', 'tests'), ('release', 'checks')):
                (source / 'reports' / f'{name}-{release}-{suffix}.txt').write_text(
                    'Unit-test fixture only; no release acceptance.\n')
            diet = Path(temp) / 'framediet-fixture.dll'
            diet.write_bytes(b'frame diet assembly fixture, not a game module')
            with patch.object(build_packages, 'SOURCE', source), patch.object(
                    build_packages, 'build_framediet', return_value=diet) as compile_diet:
                folders = build_packages.build(Path(temp) / 'pair')
            compile_diet.assert_called_once_with(None)
            configs = [(f / 'extension/x4_lan_lab/ui/config.lua').read_text() for f in folders]
            for config in configs:
                self.assertIn('ship_macro = "%s",' % build_packages.SHIP_MACRO, config)
                self.assertIn('ship_loadout = "%s",' % build_packages.SHIP_LOADOUT, config)
                self.assertIn('ship_weapons = %d,' % build_packages.SHIP_WEAPONS, config)
            for folder in folders:
                self.assertTrue((folder / 'extension/x4_lan_lab/libraries/loadouts.xml').is_file())
                diet_module = folder / 'extension/x4_lan_lab/native/x4lan_framediet_64.dll'
                if folder.name.endswith('Windows11'):
                    self.assertEqual(diet_module.read_bytes(), diet.read_bytes())
                else:
                    self.assertFalse(diet_module.exists())
            with patch.object(build_packages, 'SOURCE', source), patch.object(
                    build_packages, 'build_framediet', side_effect=RuntimeError('frame diet compiler failed')):
                with self.assertRaisesRegex(RuntimeError, 'frame diet compiler failed'):
                    build_packages.build(Path(temp) / 'incomplete-diet')
            self.assertFalse((Path(temp) / 'incomplete-diet').exists())

    def test_md_prepares_both_ships_and_refuses_without_substitute(self):
        root = ET.parse(EXT / 'md/x4lan_bubble.xml').getroot()
        arena = root.find("./cues/cue[@name='Arena']/actions")
        local = arena.find("set_value[@name='$local']")
        self.assertIn('md.X4LAN_Bubble.PrepareShips.$own', local.get('exact'))
        legacy = [n for n in arena.findall('do_if') if n.find('apply_loadout') is not None]
        self.assertEqual(len(legacy), 1)
        self.assertTrue(legacy[0].get('value').startswith('not md.X4LAN_Bubble.PrepareShips.$own'))
        prepare = root.find("./cues/cue[@name='PrepareShips']")
        self.assertIsNotNone(prepare)
        text = ET.tostring(prepare, encoding='unicode')
        self.assertEqual(text.count('<create_ship'), 3)              # own, peer, extras (N-player)
        self.assertEqual(text.count('<loadout loadout="$loadout"'), 3)
        self.assertIn('ware.software_dockmk2', text)
        # Exact configured count; an empty slot is accepted only without missile storage.
        self.assertIn('$candidate.weapons.operational.count != $args.$weapons', text)
        self.assertIn('$candidate.weapons.operational.count lt $candidate.weapons.numslots and $candidate.ammostorage.missile.capacity != 0', text)
        self.assertNotIn('!= $candidate.weapons.numslots or', text)
        self.assertIn('X4LAN_Bubble.ShipsRefused', text)
        self.assertIn('X4LAN_Bubble.ShipsReady', text)
        self.assertIn('X4LAN_Bubble.PreparedOwn', text)
        self.assertIn('X4LAN_Bubble.PreparedProxy', text)
        self.assertIn('SHIP_SPEC role=', text)
        self.assertNotIn('player.ship', text, 'the shared ship must never fall back to the current ship')
        refused = next(n for n in prepare.find('actions').findall('do_if') if n.get('value') == "$refusal != ''")
        self.assertEqual(len(refused.findall('do_if/destroy_object')), 2)
        self.assertIsNone(refused.find('teleport_player'))

    def test_generator_source_carries_the_shared_spec(self):
        source = (ROOT / 'tools/dev/build_bubble_md.py').read_text()
        self.assertIn("name=\"PrepareShips\"", source)
        self.assertIn('not md.X4LAN_Bubble.PrepareShips.$own?', source)


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class ShipSpecAdapterTests(unittest.TestCase):
    def make(self, setup=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(MOCK + SHARED + setup)
        lua.execute((EXT / 'ui/bubble_game.lua').read_text())
        lua.execute("events['X4LAN.Loaded']();now=10;frames.onUpdate()")
        return lua

    def test_setup_waits_for_both_shared_ships(self):
        for slot, setup in (('p1', 'SetupHost'), ('p2', 'SetupGuest')):
            with self.subTest(slot=slot):
                l = self.make("X4LAN_CONFIG.slot='%s'" % slot)
                l.execute('''
assert(count('PrepareShips')==1 and count('%(s)s')==0)
local p=last('PrepareShips')
assert(p.macro=='ship_ter_s_xperimental_01_a_macro' and p.loadout=='x4lan_coop_xperimental' and p.weapons==6)
assert(#spawned==0 and p.own==nil and p.proxy==nil)
assert(tostring(p.sector)=='22')
now=11;frames.onUpdate();assert(count('PrepareShips')==1 and count('%(s)s')==0)
positions[61]=shipPos;positions[62]=shipPos
events['X4LAN_Bubble.PreparedOwn'](nil,61)
frames.onUpdate();assert(count('%(s)s')==0, 'one callback is not readiness')
events['X4LAN_Bubble.PreparedProxy'](nil,62)
events['X4LAN_Bubble.ShipsReady']();frames.onUpdate()
assert(count('%(s)s')==1 and tostring(last('%(s)s'))=='62')
local s=X4LAN_BUBBLE_API.state
assert(s.ship==61 and s.proxy==62 and not s.failed)
events['X4LAN_Bubble.PreparedOwn'](nil,11);events['X4LAN_Bubble.ShipsReady']()
frames.onUpdate();assert(count('%(s)s')==1 and #spawned==0 and s.ship==61)
''' % {'s': setup})

    def test_unavailable_macro_stops_without_substitute(self):
        l = self.make()
        l.execute('''
events['X4LAN_Bubble.ShipsRefused'](nil,'macro/sector (X4: Timelines required)')
frames.onUpdate()
local s=X4LAN_BUBBLE_API.state
assert(s.failed and count('PrepareShips')==1 and count('SetupHost')==0)
assert(#spawned==0 and positions[11]~=nil)
assert(logged('Timelines required'))
''')

    def test_incomplete_duplicate_or_original_object_callbacks_never_start_gameplay(self):
        for own, proxy in [(61, None), (61, 0), (61, 61), (11, 62), (61, 11), (61, 99)]:
            with self.subTest(own=own, proxy=proxy):
                l = self.make()
                l.execute("positions[61]=shipPos;positions[62]=shipPos")
                l.globals().events['X4LAN_Bubble.PreparedOwn'](None, own)
                if proxy is not None:
                    l.globals().events['X4LAN_Bubble.PreparedProxy'](None, proxy)
                l.execute('''
events['X4LAN_Bubble.ShipsReady']();frames.onUpdate()
local s=X4LAN_BUBBLE_API.state
assert(s.failed and count('SetupHost')==0 and #spawned==0)
assert(positions[11]~=nil, 'original ship must stay')
assert(#removed==0)
''')

    def test_md_refusal_stops_without_setup(self):
        l = self.make()
        l.execute('''
events['X4LAN_Bubble.ShipsRefused'](nil,'equipment');frames.onUpdate()
local s=X4LAN_BUBBLE_API.state
assert(s.failed and count('SetupHost')==0 and logged('no substitute ship'))
events['X4LAN_Bubble.ShipsReady']();frames.onUpdate();assert(count('SetupHost')==0)
''')

    def test_legacy_config_keeps_proxy_macro_and_immediate_setup(self):
        l = self.make('X4LAN_CONFIG.ship_macro=nil;X4LAN_CONFIG.proxy_macro="ship_arg_s_scout_01_a_macro"')
        l.execute("assert(count('PrepareShips')==0 and count('SetupHost')==1 and spawned[1]=='ship_arg_s_scout_01_a_macro' and #spawned==1)")


if __name__ == '__main__':
    unittest.main()
