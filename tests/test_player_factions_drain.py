"""Real LuaJIT FIFO regressions for the client-only drain fix from b9f03b4."""
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

UI = Path(__file__).resolve().parents[1] / 'src/extension/x4_lan_lab/ui'


class ClientDrain(unittest.TestCase):
    def setup_lua(self, count):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute((UI / 'client.lua').read_text(encoding='utf-8'))
        lua.globals().count = count
        lua.execute('''
         queue={}
         for i=1,count do queue[i]='EMPTY|1|'..string.rep('b',32)..'|'..i end
         transport={send=function() return true end,receive=function() return table.remove(queue,1) end}
         local cfg={token=string.rep('a',32),build='test',bubble='bubble',sector='s',slot='p2'}
         client=X4LAN_CLIENT.new(cfg,transport,string.rep('1',32))
         client:receive('WELCOME|1|'..string.rep('1',32)..'|'..string.rep('b',32)..'|20',0)
        ''')
        return lua

    def test_original_200_packet_regression(self):
        lua = self.setup_lua(200)
        lua.execute('''assert(client:update(.1,nil));assert(#queue==0 and client.drained==200)
                       assert(client.drainedMax==200 and client.tick==200)''')

    def test_bound_and_max_metrics(self):
        lua = self.setup_lua(300)
        lua.execute('''assert(client:update(.1,nil));assert(#queue==44 and client.drained==256)
                       assert(client:update(.2,nil));assert(#queue==0 and client.drained==44)
                       assert(client.drainedMax==256 and client.tick==300)''')

    def test_faction_relation_after_bulk_packets_reaches_core(self):
        lua = self.setup_lua(199)
        lua.execute((UI / 'player_factions_core.lua').read_text(encoding='utf-8'))
        lua.execute('''
         calls={}
         local core=X4LAN_PLAYERS_CORE.new({slot='p2'},
          {send=function()end,log=function()end,md=function(action,args) calls[#calls+1]={action,args} end})
         client.receiveExtension=function(self,f,now) return core.receive(self.lease,f,now) end
         queue[200]='PFREL|1|'..string.rep('b',32)..'|2|1|2|-0.5000'
         assert(client:update(.1,nil));assert(#queue==0)
         assert(#calls==1 and calls[1][1]=='Relation' and calls[1][2].value==-.5)
         assert(calls[1][2].fa=='x4lan_player1' and calls[1][2].fb=='player')
        ''')


if __name__ == '__main__':
    unittest.main()
