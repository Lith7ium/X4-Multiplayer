"""Native fleet identities are canonical and orders must actually change them."""
from pathlib import Path
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import validate_native_gate as validator


class NativeFleetTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          X4LAN_CONFIG={native_gate_fleet_state=true}
          actors={101,102,103,104,105,106,107,108,109,110,111,112}
          commander=101;assignment='defence';group=1
          GetCommander=function() return commander end
          ConvertIDTo64Bit=function(value) return tonumber(value) end
          GetComponentData=function(object,a,b)
            assert(a=='assignment' and b=='subordinategroup')
            return assignment,group
          end
        ''')
        self.lua.execute((ROOT/'tools/dev/native_gate_fleet.lua').read_text())

    def sample(self):
        return self.lua.globals().X4LAN_NATIVE_FLEET(103,self.lua.globals().actors)

    def test_identity_uses_fixture_index_and_keeps_assignment_and_group(self):
        self.assertEqual(self.sample(),'FLEET_1;1;defence;1')
        self.lua.execute("commander=202;actors[1]=202")
        self.assertEqual(self.sample(),'FLEET_1;1;defence;1')
        self.lua.execute("commander=102;assignment='attack';group=2")
        self.assertEqual(self.sample(),'FLEET_1;2;attack;2')
        self.lua.execute('commander=nil')
        self.assertEqual(self.sample(),'FLEET_1;0;none;0')

    def test_unknown_self_or_malformed_native_relationship_fails_closed(self):
        for change in ('commander=999','commander=103','group=11',"assignment='bad;field'",'assignment=nil'):
            with self.subTest(change=change):
                self.setUp();self.lua.execute(change)
                with self.assertRaises(Exception):self.sample()

    def test_validator_requires_actual_join_and_remove_before_next_command(self):
        states={t:{'1':{'command':'00000000'},'2':{'command':'00000000'}} for t in range(7)}
        states[1]['1']['command']='06030100';states[4]['1']['command']='07030000'
        samples={t:'BATTLE_SUBSET_7|3/live/fleet:FLEET_1;'+('1;defence;1' if 3<=t<6 else '0;none;0')+'/body0:none' for t in range(7)}
        errors=[]
        self.assertEqual(validator.fleet_evidence(samples,states,1,errors),2)
        self.assertEqual(errors,[])
        samples[6]=samples[5]
        errors=[];validator.fleet_evidence(samples,states,1,errors)
        self.assertTrue(errors)
