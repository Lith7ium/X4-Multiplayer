import importlib.util
from pathlib import Path
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('live_relay',ROOT/'tools/dev/native_gate_relay.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
TOKEN='a'*32
validation_spec=importlib.util.spec_from_file_location('live_validation',ROOT/'tools/dev/validate_native_gate.py')
validation=importlib.util.module_from_spec(validation_spec);validation_spec.loader.exec_module(validation)


class LiveInputTests(unittest.TestCase):
    def setUp(self):
        self.inbox=module.LiveInputs(TOKEN)

    def packet(self,kind,slot,seq,value,port=10001):
        return self.inbox.receive(f'{kind}|1|{TOKEN}|{slot}|{seq}|{value}'.encode(),('127.0.0.1',port))

    def test_duplicate_submission_and_lost_offer_do_not_duplicate_capture(self):
        self.assertIn(b'NGQUEUED',self.packet('NGCMD',1,1,'03010100')[0][1])
        self.assertEqual(self.packet('NGCMD',1,1,'02010000'),[])
        self.assertEqual(self.packet('NGCMD',1,3,'02010000'),[])
        offered=self.packet('NGPOLL',1,0,0)
        self.assertIn(b'|1|1|03010100',offered[0][1])
        self.assertEqual(self.packet('NGPOLL',1,0,0),offered)
        self.assertEqual(self.packet('NGPOLL',1,1,50),[])
        self.assertEqual(self.packet('NGPOLL',1,1,51),[])
        self.assertEqual(self.inbox.rows[1][1]['tick'],50)
        self.assertIn(b'NGCAPTURED',self.packet('NGCMD',1,1,'03010100')[0][1])

    def test_independent_players_and_pinned_game_endpoint(self):
        self.packet('NGCMD',1,1,'03010100')
        self.packet('NGCMD',2,1,'03070200')
        one=self.packet('NGPOLL',1,0,0,20001)
        two=self.packet('NGPOLL',2,0,0,20002)
        self.assertNotEqual(one,two)
        self.assertEqual(self.packet('NGPOLL',1,1,40,20002),[])
        self.assertEqual(self.inbox.captured[1],0)
        self.assertEqual(self.inbox.receive(f'NGPOLL|1|{TOKEN}|1|0|0'.encode(),('192.0.2.1',20001)),[])

    def test_live_capture_is_immutable_and_retransmissions_do_not_reapply(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4LAN_CONFIG={native_gate_command_plan='live',host='127.0.0.1',port=12345,slot='p1',token=string.rep('a',32)}
          packets={};sent={};logs={}
          connection={send=function(self,p) sent[#sent+1]=p;return true end,
            receive=function() return table.remove(packets,1) end}
          package.loadlib=function() return function() return {open=function() return connection end} end end
          DebugError=function(s) logs[#logs+1]=s end
        ''')
        lua.execute((ROOT/'tools/dev/native_gate_live.lua').read_text())
        capture=lua.globals().X4LAN_NATIVE_LIVE_INPUT
        self.assertEqual(capture(1),'00000000')
        lua.execute("packets[1]='NGNEXT|1|'..X4LAN_CONFIG.token..'|1|1|03010100'")
        self.assertEqual(capture(1),'00000000')  # Never change an already sealed proposal.
        self.assertEqual(capture(2),'03010100')
        self.assertEqual(capture(2),'03010100')
        lua.execute("packets[1]='NGNEXT|1|'..X4LAN_CONFIG.token..'|1|1|03010100'")
        self.assertEqual(capture(3),'00000000')
        self.assertEqual(len(lua.globals().logs),1)
        self.assertTrue(lua.globals().sent[3].endswith('|1|1|2'))
        lua.execute("packets[1]='NGNEXT|1|'..X4LAN_CONFIG.token..'|1|3|02010000'")
        with self.assertRaises(Exception): capture(4)

    def test_validation_links_external_submission_client_capture_and_admission(self):
        log='NATIVE_GATE INPUT_CAPTURE slot=1 seq=1 tick=2 command=03010100'
        queued={'1':{'tick':2,'command':'03010100'}}
        states={2:{'1':{'command':'03010100'}},3:{'1':{'command':'00000000'}}}
        errors=[]
        self.assertEqual(validation.live_input_evidence(log,1,queued,states,errors),1)
        self.assertEqual(errors,[])
        for bad_log,bad_queue,bad_states in ((log+log,queued,states),(log,{},states),
                (log,queued,{2:states[2]}),(log.replace('03010100','02010000'),queued,states)):
            errors=[]
            validation.live_input_evidence(bad_log,1,bad_queue,bad_states,errors)
            self.assertTrue(errors)


if __name__ == '__main__': unittest.main()
