"""Production frame-diet / RNG-split loader (src/extension/x4_lan_lab/ui/framediet.lua) under real LuaJIT."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime

SOURCE = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui/framediet.lua'
WINDOWS = '\\\\\\n;\\n?\\n!\\n-\\n'
LINUX = '/\\n;\\n?\\n!\\n-\\n'


class FrameDietLoaderTests(unittest.TestCase):
    def runtime(self, config, platform=WINDOWS, settings=''):
        lua = LuaRuntime()
        lua.execute('''
          now=0;logs={};armed=true;stalls=0;starts={};stops=0;loads=0;reason=nil
          function GetCurRealTime() return now end
          function DebugError(s) logs[#logs+1]=s end
          function SetScript(event,fn) assert(event=='onUpdate');ui=fn end
          handle={active=function(self) return armed,stalls end,stop=function(self) stops=stops+1 end,
            stats=function(self) return {reason=reason} end}
          package.config="''' + platform + '''"
          function package.loadlib(path,symbol)
            assert(path=='./extensions/x4_lan_lab/native/x4lan_framediet_64.dll')
            assert(symbol=='luaopen_x4lan_framediet');loads=loads+1
            if load_failure then return nil,load_failure end
            return function() return {start=function(mode,sites,...)
              assert(select('#',...)==0)
              starts[#starts+1]=mode..':'..sites
              if refusal then local r=refusal;if refusal_once then refusal=nil end;return nil,r end
              return handle
            end} end
          end
        ''')
        lua.execute('X4LAN_CONFIG=' + config)
        lua.execute(settings)
        lua.execute(SOURCE.read_text())
        return lua

    def logs(self, lua):
        return list(lua.eval('logs').values())

    def starts(self, lua):
        return list(lua.eval('starts').values())

    def disabled(self, lua):
        return [line for line in self.logs(lua) if 'FEATURE ' in line and ' DISABLED ' in line]

    def test_inert_unless_enabled(self):
        lua = self.runtime("{enabled=false,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}")
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual((self.logs(lua), lua.eval('loads')), ([], 0))

    def test_client_arms_only_the_rng_split(self):
        lua = self.runtime("{enabled=true,frame_diet=false,rng_split=true,slot='p2',test_profile='45841'}")
        lua.execute('ui()')
        self.assertEqual(self.starts(lua), ['pass:rng_split'])
        self.assertEqual(self.logs(lua), ['[X4LAN] FEATURES process=x4lan-framediet framediet=off rng_split=on'])

    def test_dedicated_windows_server_arms_server_diet_and_split(self):
        lua = self.runtime("{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}")
        lua.execute('ui();now=5;ui()')
        self.assertEqual(self.starts(lua), ['diet:server,rng_split'])
        self.assertEqual(self.logs(lua), ['[X4LAN] FEATURES process=x4lan-framediet framediet=on rng_split=on'])

    def test_server_diet_without_split_flag(self):
        lua = self.runtime("{enabled=true,frame_diet=true,rng_split=false,dedicated=true,slot='p1',test_profile='45841'}")
        lua.execute('ui()')
        self.assertEqual(self.starts(lua), ['diet:server'])
        self.assertIn('framediet=on rng_split=off', self.logs(lua)[0])

    def test_frame_diet_is_refused_on_a_playing_host_or_client(self):
        for config in ("{enabled=true,frame_diet=true,rng_split=true,dedicated=false,slot='p1',test_profile='45841'}",
                       "{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p2',test_profile='45841'}"):
            lua = self.runtime(config)
            lua.execute('ui()')
            self.assertEqual(self.starts(lua), ['pass:rng_split'])
            self.assertEqual(self.disabled(lua), ['[X4LAN] FEATURE framediet DISABLED reason=not_dedicated_server'])
            self.assertIn('framediet=auto-disabled:not_dedicated_server rng_split=on', self.logs(lua)[-1])

    def test_linux_and_other_profiles_refuse_loudly_without_loading(self):
        for platform, config, reason in (
                (LINUX, "{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}",
                 'windows_only'),
                (WINDOWS, "{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45850'}",
                 'not_test_profile')):
            lua = self.runtime(config, platform)
            self.assertIsNone(lua.eval('ui'))
            self.assertEqual(lua.eval('loads'), 0)
            self.assertEqual(self.disabled(lua), [f'[X4LAN] FEATURE framediet DISABLED reason={reason}',
                                                  f'[X4LAN] FEATURE rng_split DISABLED reason={reason}'])
            self.assertEqual(self.logs(lua)[-1], '[X4LAN] FEATURES process=x4lan-framediet '
                             f'framediet=auto-disabled:{reason} rng_split=auto-disabled:{reason}')

    def test_every_slot_test_profile_is_accepted(self):
        for profile in ('45841', '45843', '45848'):
            lua = self.runtime("{enabled=true,rng_split=true,slot='p3',test_profile='%s'}" % profile)
            lua.execute('ui()')
            self.assertEqual(self.starts(lua), ['pass:rng_split'])
            self.assertEqual(self.disabled(lua), [])
        for profile in ('45840', '45849', 'x'):
            lua = self.runtime("{enabled=true,rng_split=true,slot='p3',test_profile='%s'}" % profile)
            self.assertIsNone(lua.eval('ui'))
            self.assertEqual(self.disabled(lua), ['[X4LAN] FEATURE rng_split DISABLED reason=not_test_profile'])

    def test_nothing_requested_logs_features_off_and_loads_nothing(self):
        lua = self.runtime("{enabled=true,frame_diet=false,rng_split=false,slot='p2',test_profile='45841'}")
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual(lua.eval('loads'), 0)
        self.assertEqual(self.logs(lua), ['[X4LAN] FEATURES process=x4lan-framediet framediet=off rng_split=off'])

    def test_load_failure_is_loud(self):
        lua = self.runtime("{enabled=true,rng_split=true,slot='p2',test_profile='45841'}",
                           settings="load_failure='The specified module could not be found.'")
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual(self.disabled(lua), [
            '[X4LAN] FEATURE rng_split DISABLED reason=load_failed:The_specified_module_could_not_be_found.'])
        self.assertIn('rng_split=auto-disabled:load_failed:', self.logs(lua)[-1])

    def test_loading_refusals_are_retried_every_second(self):
        lua = self.runtime("{enabled=true,rng_split=true,slot='p2',test_profile='45841'}",
                           settings="refusal='loading (UI on the main thread)';refusal_once=true")
        lua.execute('ui();now=0.5;ui()')
        self.assertEqual(len(self.starts(lua)), 1)
        self.assertEqual(self.logs(lua), [])
        lua.execute('now=1;ui()')
        self.assertEqual(len(self.starts(lua)), 2)
        self.assertEqual(self.logs(lua), ['[X4LAN] FEATURES process=x4lan-framediet framediet=off rng_split=on'])

    def test_terminal_refusal_disables_both_once(self):
        lua = self.runtime("{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}",
                           settings="refusal='unsupported executable or modified engine signatures'")
        lua.execute('ui();now=2;ui();now=4;ui()')
        self.assertEqual(len(self.starts(lua)), 1)
        reason = 'unsupported_executable_or_modified_engine_signatures'
        self.assertEqual(self.disabled(lua), [f'[X4LAN] FEATURE framediet DISABLED reason={reason}',
                                              f'[X4LAN] FEATURE rng_split DISABLED reason={reason}'])

    def test_stall_disables_the_diet_but_keeps_the_split(self):
        lua = self.runtime("{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}")
        lua.execute('ui();ui();stalls=1;ui();ui();ui()')
        self.assertEqual(self.disabled(lua), ['[X4LAN] FEATURE framediet DISABLED reason=ui_lua_stalled'])
        self.assertEqual(lua.eval('stops'), 0)
        # a later disarm of the remaining owner disables the split as well
        lua.execute("armed=false;reason='vtable entry changed';ui();ui()")
        self.assertEqual(self.disabled(lua)[-1], '[X4LAN] FEATURE rng_split DISABLED reason=vtable_entry_changed')
        self.assertEqual(len(self.disabled(lua)), 2)
        self.assertEqual(lua.eval('stops'), 1)

    def test_disarm_disables_every_armed_feature_once(self):
        lua = self.runtime("{enabled=true,frame_diet=true,rng_split=true,dedicated=true,slot='p1',test_profile='45841'}")
        lua.execute("ui();armed=false;reason='callback ownership lost';ui();ui()")
        self.assertEqual(self.disabled(lua), ['[X4LAN] FEATURE framediet DISABLED reason=callback_ownership_lost',
                                              '[X4LAN] FEATURE rng_split DISABLED reason=callback_ownership_lost'])
        self.assertEqual(lua.eval('stops'), 1)

    def test_loaded_once(self):
        lua = self.runtime("{enabled=true,rng_split=true,slot='p2',test_profile='45841'}")
        lua.execute('first=ui;ui=nil')
        lua.execute(SOURCE.read_text())
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual(lua.eval('loads'), 1)


if __name__ == '__main__':
    unittest.main()
