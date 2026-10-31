"""Log-only NPC targeting diagnostic: adapter gating, start retry and log format."""
from pathlib import Path
import unittest
import xml.etree.ElementTree as E

from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
LUA = ROOT / 'src/extension/x4_lan_lab/ui/npc_target_diag.lua'
MD = ROOT / 'src/extension/x4_lan_lab/md/x4lan_npc_target_diag.xml'


class NpcTargetDiagTests(unittest.TestCase):
    def runtime(self, enabled=True):
        lua = LuaRuntime()
        lua.execute('''
          now=0;logs={};events={};handlers={}
          function GetCurRealTime() return now end
          function DebugError(s) logs[#logs+1]=s end
          function RegisterEvent(name,fn) handlers[name]=fn end
          function AddUITriggeredEvent(screen,control) events[#events+1]=screen..'/'..control end
          function SetScript(event,fn) assert(event=='onUpdate');ui=fn end
        ''')
        lua.execute("X4LAN_CONFIG={slot='p1',npc_target_diag=%s}" % ('true' if enabled else 'false'))
        lua.execute(LUA.read_text())
        return lua

    def test_disabled_registers_nothing(self):
        lua = self.runtime(enabled=False)
        self.assertIsNone(lua.eval('ui'))
        self.assertEqual(lua.eval('next(handlers)'), None)

    def test_start_is_resent_until_ready_then_stops(self):
        lua = self.runtime()
        lua.execute('ui();ui();now=5;ui()')
        self.assertEqual(lua.eval('#events'), 2)
        lua.execute("handlers['X4LAN_NpcTargetDiag.Ready']();now=20;ui()")
        self.assertEqual(lua.eval('#events'), 2)
        self.assertIn('NPCDIAG READY slot=p1', lua.eval('logs[1]'))

    def test_rows_are_logged_with_kind_and_slot(self):
        lua = self.runtime()
        lua.execute("handlers['X4LAN_NpcTargetDiag.Npc'](nil,'ship=2 npc=ABC-123 target=this_player radar=1')")
        self.assertEqual(lua.eval('logs[1]'), '[X4LAN] NPCDIAG NPC slot=p1 ship=2 npc=ABC-123 target=this_player radar=1')

    def test_md_is_log_only(self):
        tags = {e.tag for e in E.parse(MD).iter()}
        writes = {'set_owner', 'create_ship', 'destroy_object', 'create_order', 'cancel_order', 'set_faction_relation',
                  'add_faction_relation', 'set_object_radar_visible', 'warp', 'set_known'}
        self.assertFalse(tags & writes)
        self.assertIn('raise_lua_event', tags)


if __name__ == '__main__':
    unittest.main()
