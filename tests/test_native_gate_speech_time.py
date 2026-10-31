"""native_gate_speech_game_time: probe arming, refusal and row drain (LuaJIT, mock gate).
The routing itself is covered natively in src/native/engine/test_lockstep_speech_time.h."""
from pathlib import Path
import unittest

from lupa.luajit21 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
PROBE = (ROOT / 'tools/dev/native_gate_probe.lua').read_text(encoding='utf-8')
STOP_FIELDS = ('state.completed=state.issued;state.restored=1;state.wait_game_delta=0;state.max_dt_error=0;state.path_joins=0;'
               'state.path_restored=1;state.rng_seeds=0;state.dt_restored=1;')

HARNESS = '''
  X4LAN_CONFIG={native_gate_test=true,test_profile='45841',slot='p1',native_gate_hash='clock',
    native_gate_snapshot_sha=string.rep('a',64),port=12345,token=string.rep('c',32),native_gate_speech_game_time=true}
  X4LAN_SHA256={digest=function() return string.rep('b',64) end}
  logs={};loads=0;reports=0;boots=0;is_menu=true;arms=0;rows={}
  state={active=1,boot_phase=0,issued=0,report_tick=0,rng='0'}
  gate={boot=function() boots=boots+1;return 0 end,poll=function() return state end,
    load_requested=function() state.boot_phase=1;return 1 end,
    report=function(tick) reports=reports+1;state.report_tick=tick;return 1 end,
    stop=function() state.active=0;return 1 end,
    speech_time=function() arms=arms+1;return refuse and 0 or 1 end,
    speech_time_row=function(i) return rows[i] end,
    speech_time_status=function() return 'armed=1 installed=1 routed='..(rows[0] and 1 or 0) end}
  package.loadlib=function(path) assert(not path:find('fixedtick'));return function() return gate end end
  local ffi={cdef=function() end,typeof=function() return true end,string=function(s) return s end,
    C={GetSaveFolderPath=function() return 'fake/45841/save' end,GetCurrentGameTime=function() return 7 end,
      IsStartmenu=function() return is_menu end,ReloadSaveList=function() end,IsSaveListLoadingComplete=function() return true end}}
  require=function(name) assert(name=='ffi');return ffi end
  DebugError=function(s) logs[#logs+1]=s end
  RegisterEvent=function() end
  SetScript=function(name,fn) update=fn end
  GetSaveList=function() return {{filename='save_009'}} end
  LoadGame=function(slot) assert(slot=='save_009');loads=loads+1 end
'''


def lua(config=''):
    runtime = LuaRuntime(unpack_returned_tuples=True)
    runtime.execute(HARNESS)
    runtime.execute(config)
    return runtime


def watched(runtime, word):
    return [line.split('NATIVE_GATE ')[1] for line in runtime.globals().logs.values() if word in line]


class SpeechTimeProbeTests(unittest.TestCase):
    def test_arms_before_the_load_re_arms_after_the_reload_and_drains_rows(self):
        runtime = lua()
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual((runtime.globals().arms, runtime.globals().loads), (1, 1))
        runtime.execute('state.boot_phase=3;is_menu=false')
        runtime.execute(PROBE)
        runtime.execute("update();rows[0]='index=0 tick=185 key=0000000000011e13 kind=saved page=10002 line=920 early=1';state.issued=1;update()")
        runtime.execute('state.issued=2;update()')  # nothing new: no second status line
        self.assertEqual(runtime.globals().arms, 2)
        self.assertEqual(len(watched(runtime, 'SPEECH_TIME_ARMED')), 2)
        self.assertEqual(watched(runtime, 'SPEECH_TIME_ROW'), ['SPEECH_TIME_ROW index=0 tick=185 key=0000000000011e13 kind=saved page=10002 line=920 early=1'])
        self.assertEqual(watched(runtime, 'SPEECH_TIME armed'), ['SPEECH_TIME armed=1 installed=1 routed=1'])
        runtime.execute(STOP_FIELDS + 'state.active=0;state.reason=1;update()')
        self.assertEqual(watched(runtime, 'SPEECH_TIME_STOP'), ['SPEECH_TIME_STOP armed=1 installed=1 routed=1'])
        self.assertFalse(any('ERROR' in line for line in runtime.globals().logs.values()))

    def test_refusals(self):
        for config in ("X4LAN_CONFIG.native_gate_speech_game_time='yes'", 'X4LAN_CONFIG.native_gate_speech_game_time=1'):
            with self.subTest(config=config):
                runtime = lua(config)
                with self.assertRaises(LuaError):
                    runtime.execute(PROBE)
                self.assertEqual(runtime.globals().boots, 0)
        runtime = lua('refuse=true')
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().loads, 0)
        self.assertTrue(any('speech game time refused' in line for line in runtime.globals().logs.values()))
        runtime = lua('X4LAN_CONFIG.native_gate_speech_game_time=false')
        runtime.execute(PROBE)
        runtime.execute('update();update();update()')
        self.assertEqual(runtime.globals().arms, 0)


if __name__ == '__main__':
    unittest.main()
