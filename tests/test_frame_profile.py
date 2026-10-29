"""Opt-in FRAMECOST profiler with engine doubles; native frame costs still need an X4 run."""
from pathlib import Path
import ast
import re
import unittest
from test_game_adapter import MOCK, LuaRuntime
from test_bubble_adapter import EXTRA
ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'src/extension/x4_lan_lab/ui'
EARLY = ('measure_clock.lua', 'net_rtt.lua', 'frame_profile.lua')
ADAPTERS = ('bubble_game.lua', 'motion.lua', 'coop_game.lua', 'life_game.lua')
CLOCK = '''
clockNow=0;clockStep=0.0001
X4LAN_MEASURE_CLOCK=function() clockNow=clockNow+clockStep;return clockNow end
'''


def framecost(logs):
    return [line for line in logs if ' FRAMECOST v=1 ' in line]


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class FrameProfileTests(unittest.TestCase):
    def runtime(self, setup=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(MOCK + EXTRA + setup)
        return lua

    def load(self, lua, files):
        for name in files:
            lua.execute((UI / name).read_text())

    def coop(self, flags):
        lua = self.runtime(CLOCK + flags + '''
X4LAN_CONFIG.coop=true
sectors={[11]=22,[42]=22};ConvertIDTo64Bit=function(v) return tonumber(v) end
api.GetContextByClass=function(object) return sectors[object] or 22 end
originalAdd,originalRegister,originalNew,originalOpen=AddUITriggeredEvent,RegisterEvent,X4LAN_CLIENT.new,X4LAN_TRANSPORT.open
''')
        self.load(lua, EARLY + ADAPTERS)
        lua.execute('rootBefore=X4LAN_BUBBLE_API.frame')
        self.load(lua, ('frame_profile_install.lua',))
        lua.execute("events['X4LAN.Loaded']();now=10;frames.onUpdate();events['X4LAN_Bubble.Ready']();"
                    "frames.onUpdate();events['X4LAN_Coop.Ready']()")
        return lua

    def test_disabled_installs_nothing(self):
        for flags in ('', 'X4LAN_CONFIG.frame_profile=false;X4LAN_CONFIG.net_probe=false'):
            with self.subTest(flags=flags):
                lua = self.coop(flags)
                lua.execute('''
assert(X4LAN_MEASURE==nil and X4LAN_FRAME_PROFILE==nil and X4LAN_NET_PROBE==nil)
assert(AddUITriggeredEvent==originalAdd and RegisterEvent==originalRegister)
assert(X4LAN_CLIENT.new==originalNew and X4LAN_TRANSPORT.open==originalOpen)
assert(X4LAN_BUBBLE_API.frame==rootBefore)
for _,v in ipairs(logs) do assert(not v:find('FRAMECOST') and not v:find('MEASURE_CLOCK')) end
''')

    def test_source_config_is_inert(self):
        text = (UI / 'config.lua').read_text()
        self.assertIn('frame_profile = false', text)
        self.assertIn('net_probe = false', text)
        launcher = ast.parse((ROOT / 'tools/distribution/session_launcher.py').read_text())
        config_function = next(
            node for node in launcher.body
            if isinstance(node, ast.FunctionDef) and node.name == 'session_config'
        )
        config_assignment = next(
            node for node in config_function.body
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == 'cfg' for target in node.targets)
        )
        self.assertIsInstance(config_assignment.value, ast.Call)
        self.assertIsInstance(config_assignment.value.func, ast.Name)
        self.assertEqual(config_assignment.value.func.id, 'dict')
        defaults = {keyword.arg: keyword.value for keyword in config_assignment.value.keywords}
        for flag in ('frame_profile', 'net_probe'):
            self.assertIsInstance(defaults[flag], ast.Constant)
            self.assertIs(defaults[flag].value, True)  # Enabled in internal packages.

        ui = (ROOT / 'src/extension/x4_lan_lab/ui.xml').read_text()
        order = [ui.index('ui/' + n) for n in ('client.lua', 'measure_clock.lua', 'net_rtt.lua',
                                                  'frame_profile.lua', 'game.lua', 'bubble_game.lua',
                                                  'identity_game.lua', 'frame_profile_install.lua')]
        self.assertEqual(order, sorted(order))

    def test_exclusive_time_nested_errors_and_returns(self):
        lua = self.runtime('X4LAN_CONFIG.frame_profile=true;manual=0;X4LAN_MEASURE_CLOCK=function() return manual end')
        self.load(lua, EARLY)
        lua.execute('''
local P=X4LAN_FRAME_PROFILE
local inner=P.wrap('inner',function(x) manual=manual+0.002;return x,x+1 end)
local failing=P.wrap('failing',function() manual=manual+0.001;error('boom',0) end)
local outer=P.wrap('outer',function()
 manual=manual+0.003
 local a,b=inner(4);assert(a==4 and b==5)
 local ok,e=pcall(failing);assert(not ok and e=='boom')
 return 'done'
end)
assert(outer()=='done')
local o,i,f=P.sections.outer,P.sections.inner,P.sections.failing
assert(math.abs(o.total-0.003)<1e-12 and math.abs(i.total-0.002)<1e-12 and math.abs(f.total-0.001)<1e-12)
assert(o.calls==1 and i.calls==1 and f.calls==1 and math.abs(o.max-0.003)<1e-12)
''')

    def test_framecost_line_counts_sections_and_md_round_trips(self):
        lua = self.coop('X4LAN_CONFIG.frame_profile=true')
        lua.execute('''
local P=X4LAN_FRAME_PROFILE;assert(P and P.ready)
assert(X4LAN_BUBBLE_API.frame~=rootBefore)
-- Slow the coop chain so it dominates, then let one second of frames pass.
local coopFrame=P.sections.coop;assert(coopFrame)
clockStep=0.001
for i=1,400 do now=10+i*0.01;frames.onUpdate() end
events['X4LAN_Coop.Docks'](nil,'')
for i=401,1200 do now=10+i*0.01;frames.onUpdate() end
''')
        logs = list(lua.globals().logs.values())
        lines = framecost(logs)
        self.assertTrue(lines, logs[-5:])
        line = lines[0]
        fields = dict(re.findall(r'(\w+)=(\S+)', line))
        self.assertEqual(fields['v'], '1')
        self.assertGreater(int(fields['frames']), 10)
        for key in ('mod_ms_mean', 'mod_ms_p95', 'mod_ms_max', 'frame_interval_ms_p95'):
            self.assertGreaterEqual(float(fields[key]), 0)
        self.assertLessEqual(float(fields['mod_ms_mean']), float(fields['mod_ms_max']))
        self.assertEqual(fields['clock'], 'injected')
        sections = {s.split(':')[0]: s.split(':')[1:] for s in fields['sections'].split(',')}
        for name in ('bubble', 'coop', 'net', 'life', 'coop.prepare'):
            self.assertIn(name, sections)
            calls, total, peak = sections[name]
            self.assertGreater(int(calls), 0)
            self.assertGreaterEqual(float(total), float(peak))
        md = [line for line in logs if ' FRAMECOST_MD v=1 ' in line]
        self.assertEqual(len(md), len(lines))
        joined = ' '.join(md)
        self.assertIn('X4LAN_Coop.Setup:1', joined)  # Lua->MD event counted by screen/control
        self.assertIn('X4LAN_Coop.Docks:1', joined)  # MD->Lua callback counted by event name
        self.assertTrue(any(l.startswith('[X4LAN] FRAMECOST installed root=bubble') for l in logs))

    def test_errors_still_stop_the_adapter_and_are_not_swallowed(self):
        lua = self.coop('X4LAN_CONFIG.frame_profile=true')
        lua.execute('''
X4LAN_BUBBLE_API.coop.receive=nil -- the next packet dispatch raises inside a wrapped frame
X4LAN_BUBBLE_API.life.frame=function() error('life failure') end
now=20;frames.onUpdate()
assert(X4LAN_BUBBLE_API.state.failed)
''')
        self.assertTrue(any('BUBBLE STOPPED' in l and 'life failure' in l for l in lua.globals().logs.values()))

    def test_real_luajit_ffi_clock_is_high_resolution(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
DebugError=function() end
GetCurRealTime=function() return 0 end
X4LAN_CONFIG={enabled=true,frame_profile=true}
''')
        self.load(lua, ('measure_clock.lua',))
        source = lua.eval('X4LAN_MEASURE.source')
        self.assertIn(source, ('qpc', 'clock_gettime'))
        self.assertLess(lua.eval('X4LAN_MEASURE.resolutionSeconds'), 1e-5)
        self.assertTrue(lua.eval('X4LAN_MEASURE.timed'))

    def test_frozen_fallback_clock_reports_untimed(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
logs={};DebugError=function(v) logs[#logs+1]=v end
GetCurRealTime=function() return 5 end
local realRequire=require
require=function(name) if name=='ffi' then error('no ffi') end return realRequire(name) end
os.clock=function() return 1 end  -- frozen as well
X4LAN_CONFIG={enabled=true,frame_profile=true}
''')
        self.load(lua, ('measure_clock.lua',))
        self.assertEqual(lua.eval('X4LAN_MEASURE.source'), 'GetCurRealTime')
        self.assertFalse(lua.eval('X4LAN_MEASURE.timed'))
        self.assertIn('resolution_us=none', lua.eval('logs[1]'))

    def test_failed_ffi_clock_falls_back_to_os_clock_and_logs_why(self):
        # Live Windows clients logged source=GetCurRealTime resolution_us=none, so every
        # FRAMECOST section read 0 ms. A QPC failure is now logged and os.clock is used.
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
logs={};DebugError=function(v) logs[#logs+1]=v end
GetCurRealTime=function() return 5 end
local realRequire=require
local fake={os='Windows',cdef=function() end,new=function() return {[0]=0} end,
 C=setmetatable({},{__index=function(_,name) error('cannot resolve symbol '..name) end}),
 load=function(name) error('cannot load module '..name) end}
require=function(name) if name=='ffi' then return fake end return realRequire(name) end
X4LAN_CONFIG={enabled=true,frame_profile=true}
''')
        self.load(lua, ('measure_clock.lua',))
        self.assertEqual(lua.eval('X4LAN_MEASURE.source'), 'os.clock')
        self.assertTrue(lua.eval('X4LAN_MEASURE.timed'))
        row = lua.eval('logs[1]')
        self.assertIn('source=os.clock', row)
        self.assertIn('failures=qpc:', row)
        self.assertIn('kernel32:', row)

    def test_percentile_is_nearest_rank(self):
        lua = self.runtime('X4LAN_CONFIG.frame_profile=true;' + CLOCK)
        self.load(lua, ('measure_clock.lua',))
        lua.execute('''
local p=X4LAN_MEASURE.percentile
local v={5,1,4,2,3}
assert(p(v,0)==1 and p(v,.5)==3 and p(v,.95)==5 and p(v,1)==5 and p({},.5)==nil)
assert(v[1]==5) -- input not reordered
''')


if __name__ == '__main__':
    unittest.main()
