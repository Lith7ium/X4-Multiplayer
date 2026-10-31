"""Docking acceptance requires native attachment, occupancy and departure."""
from pathlib import Path
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import validate_native_gate as validator


class NativeDockingTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          local ffi=require('ffi');native={};local proxy={C=native}
          for _,k in ipairs({'cdef','typeof','new','string'}) do proxy[k]=ffi[k] end
          require=function(name) assert(name=='ffi');return proxy end
          X4LAN_CONFIG={native_gate_docking_state=true};events={}
          actors={101,102,103,104,105,106,107,108,109,110,111,112}
          RegisterEvent=function(name,fn) events[name]=fn end
          ConvertIDTo64Bit=function(value) return value end
          attached=true;parent=201;members={101};truncated=false;owner='argon'
          native.IsComponentOperational=function() return true end
          native.GetContextByClass=function(object) return object==101 and parent or 0 end
          native.GetNumDockedShips=function(object) return object==201 and #members or 0 end
          native.GetDockedShips=function(buffer,n)
            for i=1,n do buffer[i-1]=members[i] end
            return truncated and 0 or n
          end
          native.GetObjectPositionInSector=function() return {x=-5000,y=100000,z=2500,yaw=0,pitch=0,roll=0} end
          GetComponentData=function(object,key)
            if key=='isdocked' then return object==101 and attached,false,false end
            return 'station_gen_factory_base_01_macro',owner,100,0
          end
        ''')
        self.lua.execute((ROOT/'tools/dev/native_gate_docking.lua').read_text())
        self.lua.execute("events['X4LAN_NativeGate.DockAnchor1'](nil,201);events['X4LAN_NativeGate.DockAnchor2'](nil,202)")

    def sample(self):
        return self.lua.globals().X4LAN_NATIVE_DOCKING(self.lua.globals().actors,20)

    def test_native_docked_population_uses_canonical_actor_identity(self):
        ships,stations=self.sample()
        self.assertEqual(ships[1],'DOCK_1;1;0;0;1')
        self.assertTrue(stations[1].endswith(';1;1'))
        self.lua.execute('members={103,101};actors[1]=101')
        _,stations=self.sample();self.assertTrue(stations[1].endswith(';2;1,3'))
        self.lua.execute('attached=false;parent=0;members={}')
        ships,stations=self.sample();self.assertEqual(ships[1],'DOCK_1;0;0;0;0')
        self.assertTrue(stations[1].endswith(';0;none'))

    def test_foreign_or_incomplete_native_docking_data_fails_closed(self):
        for change in ('parent=999','members={999}','members={101,101}','truncated=true',"owner='bad;field'",'attached=nil'):
            with self.subTest(change=change):
                self.setUp();self.lua.execute(change)
                with self.assertRaises(Exception):self.sample()

    def test_dock_colliders_must_register_to_their_native_anchor(self):
        self.lua.execute('''
          X4LAN_CONFIG.native_gate_docking_shape_rng=true;bound={}
          gate={register_dock_collider=function(i,anchor,module)
            assert(anchor==200+i and module==300+i);bound[#bound+1]=i;return 1
          end}
        ''')
        with self.assertRaises(Exception):self.sample()
        self.lua.execute("events['X4LAN_NativeGate.DockCollider1'](nil,301);events['X4LAN_NativeGate.DockCollider2'](nil,302)")
        self.lua.execute('X4LAN_NATIVE_DOCKING(actors,20,gate);X4LAN_NATIVE_DOCKING(actors,21,gate)')
        self.assertEqual(list(self.lua.globals().bound.values()),[1,2])

    def fixture(self):
        states={t:{'1':{'command':'00000000'},'2':{'command':'00000000'}} for t in range(8)}
        states[1]['1']['command']='09010100';states[4]['1']['command']='03010100'
        samples={}
        for tick in range(2,8):
            docked=3<=tick<6;rows=[]
            for actor in range(1,13):
                here=docked and actor==1
                row=f'{actor}/{500 if tick>=6 else 0}/0/0/0/0/0/100/0/dock:DOCK_1;{int(here)};0;0;{int(here)}'
                if actor<=2:
                    occupied=docked and actor==1
                    row+=f'/anchor:ANCHOR_1;{actor};station_gen_factory_base_01_macro;argon;100;0;0,0,0,0,0,0;'+('1;1' if occupied else '0;none')
                rows.append(row)
            samples[tick]='BATTLE_SUBSET_8|'+'|'.join(rows)
        return samples,states

    def test_validator_requires_actual_docking_then_departure_and_movement(self):
        samples,states=self.fixture();errors=[]
        self.assertEqual(validator.docking_evidence(samples,states,1,errors),{'completed_dockings':1,'completed_departures':1})
        self.assertEqual(errors,[])
        for before,after in (('DOCK_1;1;0;0;1','DOCK_1;0;0;0;0'),(';1;1',';0;none'),('/500/','/0/'),('/dock:','/missing:')):
            with self.subTest(change=before):
                errors=[];validator.docking_evidence({t:s.replace(before,after) for t,s in samples.items()},states,1,errors)
                self.assertTrue(errors)
        errors=[];validator.docking_evidence({t:s.replace('DOCK_1;1;0;0;1','DOCK_1;0;0;0;0') for t,s in samples.items()},states,1,errors,False)
        self.assertEqual(errors,[])
        # A later Move cannot claim an undocking that happened on its own first.
        samples[4]=samples[6];errors=[]
        result=validator.docking_evidence(samples,states,1,errors)
        self.assertEqual(result['completed_departures'],0)
        self.assertTrue(any('no longer docked at departure' in error for error in errors))
