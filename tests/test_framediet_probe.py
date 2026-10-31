"""Opt-in server frame diet loader (src/native/engine/framediet_probe.lua) under real LuaJIT."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime

SOURCE = Path(__file__).resolve().parents[1] / 'src/native/engine/framediet_probe.lua'

STATS = '''
  function stats_table()
    local s={mode=current_mode,sites=sites_mask,period=0,digest_max=16384,frames=frames,loading_frames=0,
      present_frames=0,ui_scenes_calls=30,ui_scenes_skipped=skipped,ui_render_calls=0,ui_render_skipped=0,
      camera_calls=0,camera_skipped=0,draw_owned=1,draw_set=1,draw_reapplied=0,reason=reason,
      stalls=stall_count,stall_sites=stall_sites,stall_frame=stall_frame}
    for _,row in ipairs({'ui_scenes','ui_render','camera','control'}) do
      for _,k in ipairs({'audited','ms_mean','ms_max','rng_calls','rng_steps','rng_unknown','id_calls','ids',
        'event_calls','events_up','time_changed','digests','digest_components','digest_changed','digest_unreadable'}) do
        s[row..'_'..k]=0
      end
      s[row..'_words']='';s[row..'_first']=''
    end
    if current_mode=='audit' then
      s.camera_audited=20;s.camera_rng_calls=20;s.camera_rng_steps=340;s.camera_ids=2;s.camera_id_calls=1
      s.camera_digests=1;s.camera_digest_components=500;s.camera_digest_changed=3;s.camera_words='0x1e8:3'
      s.camera_first='uid=5 class=0x75 off=0x1e8 before=0 after=1'
      s.control_digests=1;s.control_digest_components=500
    end
    return s
  end
'''


class FrameDietProbeTests(unittest.TestCase):
    def runtime(self, settings=''):
        lua = LuaRuntime()
        lua.execute('''
          now=0;logs={};armed=true;frames=10;skipped=30;sites_mask=9;current_mode=nil;starts={};sets={};stops=0
          X4LAN_CONFIG={test_profile='45841'}
          function GetCurRealTime() return now end
          function DebugError(s) logs[#logs+1]=s end
          function SetScript(event,fn) assert(event=='onUpdate');ui=fn end
        ''' + STATS + '''
          handle={active=function(self) return armed,stall_count end,stop=function(self) stops=stops+1 end,
            stats=function(self) return stats_table() end,
            set=function(self,mode,sites) sets[#sets+1]=sites and (mode..':'..sites) or mode;current_mode=mode
              if set_refusal then return nil,set_refusal end
              return true end}
          function package.loadlib(path,symbol)
            assert(symbol=='luaopen_x4lan_framediet');loaded_path=path
            if load_failure then return nil,load_failure end
            return function() if init_nothing then return end
              if init_error then error(init_error) end
              return {start=function(...)
              local args={n=select('#',...),...};starts[#starts+1]=args
              if refusal then local r=refusal;if refusal_once then refusal=nil end;return nil,r end
              current_mode=args[1]
              return handle
            end} end
          end
        ''')
        lua.execute(settings)
        lua.execute(SOURCE.read_text())
        return lua

    def logs(self, lua):
        return list(lua.eval('logs').values())

    def test_inert_without_mode_or_outside_the_test_profile(self):
        lua = self.runtime()
        self.assertIsNone(lua.eval('ui'))
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_CONFIG.test_profile='45850'")
        self.assertIsNone(lua.eval('ui'))
        self.assertIsNone(lua.eval('loaded_path'))

    def test_arms_with_server_default_and_reports(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet'")
        lua.execute('ui()')
        args = lua.eval('starts[1]')
        self.assertEqual((args[1], args[2], args['n']), ('diet', 'server', 5))
        logs = self.logs(lua)
        self.assertIn('FRAMEDIET armed mode=diet sites=ui_scenes,draw period=0 digest=16384', logs[0])
        lua.execute('now=10;ui()')
        logs = self.logs(lua)
        self.assertIn('[X4LAN] FRAMEDIET mode=diet sites=ui_scenes,draw phase=diet t=10.000 frames=10', logs[1])
        self.assertIn('scenesCalls=30 scenesSkipped=30', logs[1])
        self.assertIn('drawOwned=1 drawSet=1 drawReapplied=0', logs[1])
        self.assertFalse(any('FRAMEDIET_AUDIT' in line for line in logs))

    def test_passes_options_and_default_dll_path(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='audit';X4LAN_FRAMEDIET_SITES='all';X4LAN_FRAMEDIET_PERIOD=60;"
                           "X4LAN_FRAMEDIET_AUDIT_EVERY=15;X4LAN_FRAMEDIET_DIGEST=4096")
        lua.execute('ui()')
        args = lua.eval('starts[1]')
        self.assertEqual([args[i] for i in range(1, 6)], ['audit', 'all', 60, 15, 4096])
        self.assertEqual(lua.eval('loaded_path'), './extensions/x4_lan_lab/native/x4lan_framediet_64.dll')

    def test_audit_rows_are_logged(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='audit';X4LAN_FRAMEDIET_SITES='all'")
        lua.execute('ui();now=10;ui()')
        audit = [line for line in self.logs(lua) if 'FRAMEDIET_AUDIT' in line]
        self.assertEqual(len(audit), 2)
        self.assertIn('row=camera audited=20 msMean=0.0000 msMax=0.000 rngCalls=20 rngSteps=340 rngUnknown=0 idCalls=1 ids=2', audit[0])
        self.assertIn('digests=1 components=500 changed=3 unreadable=0 words=0x1e8:3 first=uid=5 class=0x75 off=0x1e8', audit[0])
        self.assertIn('row=control', audit[1])
        self.assertIn('words=- first=-', audit[1])

    def test_loading_retries_then_arms(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';refusal='loading';refusal_once=true")
        lua.execute('ui();ui();now=0.5;ui()')
        self.assertEqual(lua.eval('#starts'), 1)
        lua.execute('now=1;ui()')
        self.assertEqual(lua.eval('#starts'), 2)
        self.assertTrue(any('FRAMEDIET armed' in line for line in self.logs(lua)))

    def test_terminal_refusal_is_loud_once(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';refusal='unsupported executable or modified engine signatures'")
        lua.execute('ui();now=5;ui();now=9;ui()')
        self.assertEqual(lua.eval('#starts'), 1)
        disabled = [line for line in self.logs(lua) if 'FEATURE framediet DISABLED' in line]
        self.assertEqual(disabled, ['[X4LAN] FEATURE framediet DISABLED reason=unsupported executable or modified engine signatures'])

    def test_module_init_failure_is_loud(self):
        for setting, reason in (("init_nothing=true", 'module init failed: nil'),
                                ("init_error='boom'", 'module init failed: [string')):
            lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';" + setting)
            self.assertIsNone(lua.eval('ui'))
            logs = self.logs(lua)
            self.assertEqual(len(logs), 1)
            self.assertTrue(logs[0].startswith('[X4LAN] FEATURE framediet DISABLED reason=' + reason), logs[0])

    def test_main_thread_refusal_is_retried(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';refusal='loading (UI on the main thread)';refusal_once=true")
        lua.execute('ui();now=1;ui()')
        self.assertEqual(lua.eval('#starts'), 2)
        logs = self.logs(lua)
        self.assertFalse(any('DISABLED' in line for line in logs))
        self.assertTrue(any('FRAMEDIET armed' in line for line in logs))

    def test_stall_disables_the_cycle_and_preserves_pass_through_owner(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_FRAMEDIET_AB_S=30;"
                           "X4LAN_FRAMEDIET_AB_CYCLE='pass;ui_render'")
        lua.execute("ui();now=5;stall_count=1;stall_sites=2;stall_frame=500;ui();now=6;ui()")
        stalls = [line for line in self.logs(lua) if 'FRAMEDIET stall' in line]
        self.assertEqual(len(stalls), 1)
        self.assertIn('FRAMEDIET stall sites=ui_render frame=500 phase=pass t=5.000 stalls=1', stalls[0])
        self.assertEqual([line for line in self.logs(lua) if 'FEATURE framediet DISABLED' in line],
                         ['[X4LAN] FEATURE framediet DISABLED reason=UI Lua stalled while skipping'])
        lua.execute('now=31;ui();now=61;ui();now=91;ui()')
        self.assertEqual(list(lua.eval('sets').values()), ['pass'])
        self.assertEqual(lua.eval('stops'), 0)  # retain the RNG split/pass-through owner
        self.assertEqual(lua.eval('#starts'), 1)

    def test_load_failure_is_loud(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';load_failure='The specified module could not be found.'")
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual(self.logs(lua), ['[X4LAN] FEATURE framediet DISABLED reason=load failed: The specified module could not be found.'])

    def test_disarm_is_reported_with_reason(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet'")
        lua.execute("ui();armed=false;reason='vtable entry changed';now=2;ui();now=3;ui()")
        logs = self.logs(lua)
        self.assertEqual(logs[-1], '[X4LAN] FEATURE framediet DISABLED reason=vtable entry changed')
        self.assertIn('[X4LAN] FRAMEDIET mode=diet', logs[-2])
        self.assertEqual(lua.eval('stops'), 1)

    def test_ab_alternates_diet_and_pass(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_FRAMEDIET_AB_S=30;X4LAN_FRAMEDIET_REPORT_S=1000")
        lua.execute('ui();now=30;ui();now=45;ui();now=60;ui();now=90;ui()')
        self.assertEqual(list(lua.eval('sets').values()), ['pass', 'diet', 'pass'])
        phases = [line for line in self.logs(lua) if 'FRAMEDIET phase=' in line]
        self.assertEqual([line.split()[2] for line in phases], ['phase=pass', 'phase=diet', 'phase=pass'])

    def test_ab_cycle_enters_each_phase_in_order(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_FRAMEDIET_SITES='all';X4LAN_FRAMEDIET_AB_S=30;"
                           "X4LAN_FRAMEDIET_AB_CYCLE='pass;server;ui_scenes,camera';X4LAN_FRAMEDIET_REPORT_S=1000")
        lua.execute('ui();now=30;ui();now=60;ui();now=90;ui()')
        self.assertEqual(list(lua.eval('sets').values()),
                         ['pass', 'diet:server', 'diet:ui_scenes,camera', 'pass'])
        phases = [line.split()[2] for line in self.logs(lua) if 'FRAMEDIET phase=' in line]
        self.assertEqual(phases, ['phase=pass', 'phase=server', 'phase=ui_scenes,camera', 'phase=pass'])

    def test_refused_phase_switch_disarms_loudly(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_FRAMEDIET_AB_S=30;set_refusal='site not installed'")
        lua.execute('ui();now=30;ui();now=60;ui()')
        disabled = [line for line in self.logs(lua) if 'FEATURE framediet DISABLED' in line]
        self.assertEqual(disabled, ['[X4LAN] FEATURE framediet DISABLED reason=phase switch refused: site not installed'])
        self.assertEqual(lua.eval('stops'), 1)

    def test_audit_cycle_toggles_sites_with_mode_prefix(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='audit';X4LAN_FRAMEDIET_SITES='camera,rng_split';X4LAN_FRAMEDIET_AB_S=60;"
                           "X4LAN_FRAMEDIET_AB_CYCLE='audit:camera;audit:camera,rng_split';X4LAN_FRAMEDIET_REPORT_S=1000")
        lua.execute('ui();now=60;ui();now=120;ui()')
        self.assertEqual(list(lua.eval('sets').values()),
                         ['audit:camera', 'audit:camera,rng_split', 'audit:camera'])

    def test_ab_is_ignored_outside_diet_mode(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='audit';X4LAN_FRAMEDIET_AB_S=30")
        lua.execute('ui();now=31;ui()')
        self.assertEqual(lua.eval('#sets'), 0)

    def test_delay_postpones_arming(self):
        lua = self.runtime("X4LAN_FRAMEDIET_MODE='diet';X4LAN_FRAMEDIET_DELAY_S=20")
        lua.execute('ui();now=19;ui()')
        self.assertEqual(lua.eval('#starts'), 0)
        lua.execute('now=20;ui()')
        self.assertEqual(lua.eval('#starts'), 1)


if __name__ == '__main__':
    unittest.main()
