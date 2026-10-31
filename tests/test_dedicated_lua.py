"""Dedicated lifecycle tests with engine doubles, not native X4 evidence."""
from pathlib import Path
import unittest

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    LuaRuntime = None

SOURCE = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui/dedicated_game.lua'
MOCK = r'''
now,gameTime,paused,quitCount,pauseCount=0,10,false,0,0
logs,frames,controlLines={}, {}, {}
api={
 GetSaveFolderPath=function() return '/isolated/X4/45841/save' end,
 GetCurrentGameTime=function() return gameTime end,
 IsGamePaused=function() return paused end,
 GetPlayerContainerID=function() return 11 end,
 GetPlayerID=function() return 11 end,
 GetContextByClass=function() return 22 end,
}
local realRequire=require
require=function(name)
 if name=='ffi' then return {C=api,cdef=function() end,string=function(v) return v end} end
 return realRequire(name)
end
DebugError=function(v) logs[#logs+1]=v end
GetCurRealTime=function() return now end
GetComponentData=function() return 'cluster_14_sector001_macro' end
ConvertStringToLuaID=function(v) return v end
SetScript=function(name,fn) frames[name]=fn end
Pause=function() paused=not paused;pauseCount=pauseCount+1 end
QuitGame=function() quitCount=quitCount+1 end
io.open=function(path,mode)
 if #controlLines==0 then return nil end
 local line=table.remove(controlLines,1)
 return {read=function() return line end,close=function() end}
end
X4LAN_CONFIG={enabled=true,dedicated=true,slot='p1',test_profile='45841',build='world1-test',
 bubble=string.rep('a',32),dedicated_control_path='/tmp/x4lan-control',dedicated_heartbeat_interval=1}
X4LAN_BUBBLE_API={state={active=true,ready=true}}
'''


@unittest.skipUnless(LuaRuntime, 'lupa.luajit21 not installed')
class DedicatedLuaTests(unittest.TestCase):
    def runtime(self, setup=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute(MOCK)
        lua.execute(setup)
        lua.execute(SOURCE.read_text())
        return lua

    def test_inert_without_flag(self):
        lua = self.runtime('X4LAN_CONFIG.dedicated=false')
        self.assertEqual(lua.eval('#logs'), 0)
        self.assertIsNone(lua.eval('frames.onUpdate'))

    def test_requires_authority_profile_and_local_control(self):
        for setup in ("X4LAN_CONFIG.slot='p2'", "X4LAN_CONFIG.test_profile='70000001'",
                      "X4LAN_CONFIG.dedicated_control_path='relative/path'"):
            lua = self.runtime(setup)
            self.assertIsNone(lua.eval('frames.onUpdate'))
            self.assertIn('DEDICATED STOPPED', lua.eval('logs[#logs]'))

    def test_heartbeat_reports_native_progress(self):
        lua = self.runtime()
        lua.execute('frames.onUpdate();now=6;gameTime=11;frames.onUpdate()')
        text = '\n'.join(lua.eval('logs')[i] for i in range(1, len(lua.eval('logs')) + 1))
        self.assertIn('DEDICATED READY build=world1-test slot=p1', text)
        self.assertIn('game=10.000000', text)
        self.assertIn('game=11.000000', text)
        self.assertIn('sector=cluster_14_sector001_macro', text)

    def test_old_control_is_never_executed_and_new_commands_are_idempotent(self):
        lua = self.runtime("controlLines={'7|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|shutdown'}")
        lua.execute('frames.onUpdate();now=2;frames.onUpdate()')
        self.assertEqual(lua.eval('quitCount'), 0)
        lua.execute("controlLines={'7|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|shutdown','8|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|shutdown','8|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|shutdown'}")
        lua.execute('now=3;frames.onUpdate();now=4;frames.onUpdate();now=5;frames.onUpdate()')
        self.assertEqual(lua.eval('quitCount'), 1)

    def test_pause_resume_and_malformed_control(self):
        lua = self.runtime()
        lua.execute("controlLines={'1|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|pause','2|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|pause','3|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|resume'}")
        lua.execute('frames.onUpdate();now=2;frames.onUpdate();now=3;frames.onUpdate();now=4;frames.onUpdate()')
        self.assertEqual(lua.eval('pauseCount'), 2)
        self.assertFalse(lua.eval('paused'))
        lua.execute("controlLines={'4|wrong|shutdown'};now=5;frames.onUpdate()")
        self.assertTrue(lua.eval('X4LAN_DEDICATED.failed'))
        self.assertIn('invalid control identity', lua.eval('logs[#logs]'))


if __name__ == '__main__':
    unittest.main()
