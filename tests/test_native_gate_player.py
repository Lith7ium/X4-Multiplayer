"""Player context/action diagnostics must not imply two native players."""
from pathlib import Path
import unittest
import sys
import hashlib
from lupa.luajit21 import LuaRuntime

SOURCE=(Path(__file__).resolve().parents[1]/'tools/dev/native_gate_player.lua').read_text()
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_player_evidence import evidence,observer_evidence,fire_evidence
from native_player_launch_evidence import evidence as launch_evidence,seed as launch_seed


class PlayerProbeTests(unittest.TestCase):
    def test_observer_evidence_preserves_history_and_explicit_coverage(self):
        actor='0000000000000004'
        log=('NATIVE_GATE PLAYER_OBSERVER_ENABLED tick=1 early_coverage=0 input_changes=0\n'
             'NATIVE_GATE PLAYER_OBSERVATION index=1 tick=2 kind=33 role=1 thread=77 actor='+actor+' owner='+actor+
             ' time=0000000000000000 result=0 valid=1 g0='+'0'*158+' c0='+'0'*98+' g1='+'0'*158+' c1='+'0'*98+
             '\nNATIVE_GATE PLAYER_OBSERVER_STOP enabled=1 count=1 failures=0 restored=1 active=0 early_coverage=0 emitted=1')
        errors=[];trace,coverage=observer_evidence(log,1,(actor,actor,actor,actor),2,errors)
        self.assertEqual(errors,[]);self.assertFalse(coverage['shoot_evaluation_exercised'])
        self.assertFalse(coverage['early_updates_observed'])
        other,_=observer_evidence(log.replace('thread=77','thread=88'),2,(actor,actor,actor,actor),2,[])
        self.assertEqual(trace,other)
        other,_=observer_evidence(log.replace(' c1='+'0'*98,' c1=01'+'0'*96),2,(actor,actor,actor,actor),2,[])
        self.assertNotEqual(trace,other)
        for before,after in [('valid=1','valid=0'),('restored=1','restored=0'),('index=1','index=2')]:
            errors=[];observer_evidence(log.replace(before,after),1,(actor,actor,actor,actor),2,errors)
            self.assertTrue(errors)

    def runtime(self, slot='p1'):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          X4LAN_CONFIG={native_gate_player_probe='native-local-negative',slot='''+repr(slot)+'''}
          calls={};logs={};assist=true;activity='none';controlled=4
          local api={GetPlayerID=function() return 1 end,GetPlayerContainerID=function() return 4 end,
            GetPlayerOccupiedShipID=function() return 4 end,GetPlayerControlledShipID=function() return controlled end,
            IsFlightAssistActive=function() return assist end,IsPlayerOccupiedShipDocked=function() return false end,
            CanStartTravelMode=function() return true end,
            ToggleFlightAssist=function() calls[#calls+1]='assist';assist=not assist end,
            StartPlayerActivity=function(value) calls[#calls+1]=value;activity=value end}
          api.StopPlayerActivity=function(value) calls[#calls+1]='stop_'..value;activity='none' end
          api.GetObjectPositionInSector=function() return {x=0,y=0,z=0,yaw=0,pitch=0,roll=0} end
          api.GetSofttarget2=function() return {softtargetID=target or 0} end
          api.GetRelativeAimOffset=api.GetObjectPositionInSector
          require=function(name) assert(name=='ffi');return {typeof=function() return true end,cdef=function() end,C=api} end
          DebugError=function(text) logs[#logs+1]=text end
          GetPlayerSpeed=function() return 0,0,0,false,false,false,0,0 end
          GetPlayerActivity=function() return activity end
          gate={saved_identity=function(value) return string.format('%016x',tonumber(value)) end}
          gate.player_controls=function() return 'PLAYER_AXES_1:0000000000000004:'..string.rep('0',56) end
          gate.player_throttle=function(tick,owner,actor,value) assert(owner==1 and actor=='0000000000000004');calls[#calls+1]=value;return 1 end
        ''')
        lua.execute(SOURCE)
        return lua

    def fire_runtime(self,seeded=False):
        lua=self.runtime()
        lua.execute('''
          X4LAN_CONFIG.native_gate_player_probe='agreed-fire';X4LAN_CONFIG.native_gate_player_observer=true
          events={};RegisterEvent=function(name,fn) events[name]=fn end;ConvertIDTo64Bit=function(v) return v end
          gate.player_observe_enable=function() return 1 end
          gate.player_observe=function() return 'count=0 failures=0 restored=1 active=0' end
          gate.player_fire_enable=function() return 1 end
          gate.player_fire_event=function() return 'guns=0 created=0 moves=0 damage=0 events=0 failures=0 held='..(held and '1' or '0')..' cleanup=0 rng_changes='..(X4LAN_CONFIG.native_gate_player_weapon_rng and '1' or '0') end
          gate.player_fire_state=function() return 'PLAYER_FIRE_1:0000000000000004:'..(held and '01' or '00')..string.rep('0',12)..':'..(held and '01' or '00')..':0000000000000000' end
          gate.player_trigger=function(tick,owner,actor,value) assert(owner==1 and actor=='0000000000000004');held=value==1;calls[#calls+1]='trigger'..value;return 1 end
          gate.player_target=function(tick,owner,actor,fixture) assert(owner==1 and actor=='0000000000000004');target=fixture;calls[#calls+1]='target'..fixture;return 1 end
          gate.player_trigger_cleanup=function() held=false;calls[#calls+1]='cleanup';return 1 end
          gate.player_launch_enable=function() return 1 end
          gate.player_launch_event=function() return 'schema=1 domain=504c5957504e3031 count=0 failures=0 scopes=0 restored=0 bullet_rng=0' end
        ''')
        if seeded:lua.execute('X4LAN_CONFIG.native_gate_player_weapon_rng=true')
        lua.execute(SOURCE)
        lua.execute("events['X4LAN_NativeGate.Actor12'](nil,12);X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');X4LAN_NATIVE_PLAYER(1,gate)")
        return lua

    def test_seeded_lua_receipts_match_independent_validator(self):
        lua=self.fire_runtime(seeded=True)
        lua.globals().X4LAN_SHA256=lua.table_from({'digest':lambda value:hashlib.sha256(value.encode()).hexdigest()})
        seed=launch_seed(2,4,5,1)
        row=(f'index=1 tick=2 kind=1 ship=0000000000000004 gun=0000000000000005 call=1 spawn=0 caller=0000000000000007 seed={seed:016x} after={seed+3:016x} restored=0000000000000007 original=1 valid=1 pre='+'0'*48+' post='+'0'*48)
        lua.globals().launch_row=row
        lua.execute('''
          gate.player_launch_event=function(i) if i==0 then return 'schema=1 domain=504c5957504e3031 count=1 failures=0 scopes=1 restored=1 bullet_rng=0' end return launch_row end
          X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'00000000','00000000');X4LAN_NATIVE_PLAYER(2,gate)
          X4LAN_NATIVE_PLAYER_FIRE_TRACE(gate,true)
        ''')
        log='\n'.join(lua.globals().logs.values());states={tick:{str(slot):{'command':'00000000'} for slot in (1,2)} for tick in range(3)}
        errors=[];samples,_,result=evidence(log,1,'agreed-fire',states,errors)
        _,launch=launch_evidence(log,1,result['context'],2,errors)
        self.assertEqual(errors,[]);self.assertIn('|PLAYER_LAUNCH_1:',samples[2]);self.assertEqual(launch['scopes'],1)

    def test_projectile_receipt_digest_is_recomputed_from_actual_events(self):
        lua=self.fire_runtime()
        lua.globals().X4LAN_SHA256=lua.table_from({'digest':lambda value:hashlib.sha256(value.encode()).hexdigest()})
        lua.execute('''
          gate.player_fire_event=function(i)
            if i==0 then return 'guns=1 created=1 moves=0 damage=0 events=1 failures=0 held=0 cleanup=0 rng_changes=0' end
            return 'index=1 tick=2 kind=1 ship=0000000000000004 gun=0000000000000005 spawn=1 target=0000000000000000 call=0 amount=0000000000000000 absorbed=0000000000000000 before=0000000000000000 after=0000000000000000 hits_before=0 hits_after=0'
          end
          X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'00000000','00000000');X4LAN_NATIVE_PLAYER(2,gate)
        ''')
        log='\n'.join(lua.globals().logs.values())
        states={tick:{str(slot):{'command':'00000000'} for slot in (1,2)} for tick in range(3)}
        errors=[];evidence(log,1,'agreed-fire',states,errors);self.assertEqual(errors,[])
        errors=[];evidence(log.replace('target=0000000000000000 call=0','target=0000000000000000 call=1'),1,'agreed-fire',states,errors)
        self.assertTrue(any('trigger/aim' in e for e in errors))

    def test_fire_batch_ownership_and_native_effects_are_separate(self):
        lua=self.fire_runtime()
        with self.assertRaisesRegex(Exception,'ownership refused'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'85010100','85010100')")
        self.assertEqual(len(lua.globals().calls),0)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'86010c00','00000000');X4LAN_NATIVE_PLAYER(2,gate);X4LAN_NATIVE_PLAYER_COMMANDS.apply(3,'85010100','00000000');state=X4LAN_NATIVE_PLAYER(3,gate)")
        self.assertEqual(list(lua.globals().calls.values()),['target12','trigger1'])
        self.assertIn('PLAYER_FIRE_1:',lua.globals().state)
        self.assertIn('PLAYER_AIM_1:000000000000000c:',lua.globals().state)
        with self.assertRaisesRegex(Exception,'repeated or out of order'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(3,'85010100','00000000')")
        lua.execute('X4LAN_NATIVE_PLAYER_FIRE_TRACE(gate,true)')
        self.assertFalse(lua.globals().held)

    def test_fire_state_is_reconstructed_from_admitted_commands(self):
        lua=self.fire_runtime()
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'85010100','00000000');X4LAN_NATIVE_PLAYER(2,gate)")
        log='\n'.join(lua.globals().logs.values())
        states={0:{'1':{'command':'00000000'},'2':{'command':'00000000'}},1:{'1':{'command':'85010100'},'2':{'command':'00000000'}},2:{}}
        errors=[];samples,_,_=evidence(log,1,'agreed-fire',states,errors)
        self.assertEqual(errors,[]);self.assertIn('|PLAYER_FIRE_1:',samples[2])
        errors=[];evidence(log.replace('01000000000000:01:','00000000000000:00:'),1,'agreed-fire',states,errors)
        self.assertTrue(any('trigger/aim' in e for e in errors))

    def test_fire_evidence_requires_creation_move_and_real_damage(self):
        actor='0000000000000004';gun='0000000000000005'
        prefix='NATIVE_GATE PLAYER_FIRE_ENABLED tick=1 native_trigger=1 rng_changes=0\n'
        base=' ship='+actor+' gun='+gun+' spawn=1 target='
        empty=' amount=0000000000000000 absorbed=0000000000000000 before=0000000000000000 after=0000000000000000 hits_before=0 hits_after=0\n'
        log=prefix+'NATIVE_GATE PLAYER_FIRE_EVENT index=1 tick=2 kind=1'+base+'0000000000000000 call=0'+empty
        log+='NATIVE_GATE PLAYER_FIRE_EVENT index=2 tick=2 kind=2'+base+'0000000000000000 call=1'+empty
        log+='NATIVE_GATE PLAYER_FIRE_EVENT index=3 tick=3 kind=4'+base+'000000000000000c call=2 amount=4000000000000000 absorbed=4000000000000000 before=4059000000000000 after=4058800000000000 hits_before=0 hits_after=0\n'
        log+='NATIVE_GATE PLAYER_OBSERVATION index=1 tick=2 kind=14 role=1 thread=99 actor='+gun+' owner='+actor+' time=0000000000000000 result=1 valid=1\n'
        log+='NATIVE_GATE PLAYER_FIRE_STOP guns=1 created=1 moves=2 damage=1 events=3 failures=0 held=0 cleanup=0 rng_changes=0 emitted=3'
        errors=[];_,result=fire_evidence(log,1,(actor,actor,actor,actor),3,errors)
        self.assertEqual(errors,[]);self.assertTrue(result['firing_with_damage_observed'])
        for before,after in [('spawn=1','spawn=2'),('held=0','held=1'),('result=1','result=0'),('kind=2','kind=3')]:
            errors=[];fire_evidence(log.replace(before,after),1,(actor,actor,actor,actor),3,errors);self.assertTrue(errors)
        _,result=fire_evidence(log.replace('after=4058800000000000','after=4059000000000000'),1,(actor,actor,actor,actor),3,[])
        self.assertFalse(result['firing_with_damage_observed'])

    def test_native_actions_both_clients_are_explicitly_local_and_once(self):
        for slot,action in (('p1','assist'),('p2','travel')):
            with self.subTest(slot=slot):
                lua=self.runtime(slot)
                lua.execute('for tick=1,30 do state=X4LAN_NATIVE_PLAYER(tick,gate) end')
                self.assertEqual(list(lua.globals().calls.values()),[action])
                self.assertIn('native_contexts=1 input_capture=none',lua.globals().logs[1])
                self.assertTrue(any('local_only=1' in row for row in lua.globals().logs.values()))
                with self.assertRaisesRegex(Exception,'tick discontinuity'):
                    lua.execute('X4LAN_NATIVE_PLAYER(30,gate)')
                self.assertEqual(len(lua.globals().calls),1)

    def test_uncontrolled_ship_refuses_native_actions(self):
        lua=self.runtime()
        lua.execute('controlled=0')
        with self.assertRaisesRegex(Exception,'actually controlled ship'):
            lua.execute('for tick=1,30 do X4LAN_NATIVE_PLAYER(tick,gate) end')
        self.assertEqual(len(lua.globals().calls),0)

    def test_unknown_saved_identity_fails_closed(self):
        lua=self.runtime()
        lua.execute("gate.saved_identity=function() return '0000000000000000' end")
        with self.assertRaisesRegex(Exception,'unmapped native player identity'):
            lua.execute('X4LAN_NATIVE_PLAYER(1,gate)')

    def test_both_inputs_cannot_be_called_two_independent_player_contexts(self):
        a,b=self.runtime('p1'),self.runtime('p2')
        for lua in (a,b):lua.execute('for tick=1,30 do state=X4LAN_NATIVE_PLAYER(tick,gate) end')
        self.assertNotEqual(a.globals().state,b.globals().state)
        # Identity prefix still proves both actions target the same saved singleton.
        self.assertEqual(a.globals().state.split(':')[:5],b.globals().state.split(':')[:5])

    def test_admitted_authority_and_whole_batch_validation(self):
        lua=self.runtime()
        lua.execute("X4LAN_CONFIG.native_gate_player_probe='agreed-singleton'")
        lua.execute(SOURCE)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');X4LAN_NATIVE_PLAYER(1,gate)")
        with self.assertRaisesRegex(Exception,'ownership refused'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','81010000')")
        self.assertEqual(len(lua.globals().calls),0)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','00000000')")
        self.assertEqual(list(lua.globals().calls.values()),['assist'])
        with self.assertRaisesRegex(Exception,'repeated or out of order'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','00000000')")
        self.assertEqual(len(lua.globals().calls),1)

    def test_changed_controller_refuses_admitted_actions(self):
        lua=self.runtime()
        lua.execute("X4LAN_CONFIG.native_gate_player_probe='agreed-singleton'")
        lua.execute(SOURCE)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');X4LAN_NATIVE_PLAYER(1,gate);controlled=5")
        with self.assertRaisesRegex(Exception,'context changed'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','00000000')")
        self.assertEqual(len(lua.globals().calls),0)

    def test_native_throttle_uses_admitted_bounded_value(self):
        lua=self.runtime()
        lua.execute("X4LAN_CONFIG.native_gate_player_probe='agreed-throttle'")
        lua.execute(SOURCE)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');state=X4LAN_NATIVE_PLAYER(1,gate)")
        self.assertIn('|PLAYER_AXES_1:',lua.globals().state)
        with self.assertRaisesRegex(Exception,'unsupported native player action'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'8401c900','00000000')")
        self.assertEqual(len(lua.globals().calls),0)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'8401c800','00000000')")
        self.assertEqual(list(lua.globals().calls.values()),[1])

    def test_conflicting_native_authority_proposals_refuse_before_first_effect(self):
        for slot in ('p1','p2'):
            lua=self.runtime(slot)
            lua.execute("X4LAN_CONFIG.native_gate_player_probe='authority-negative'")
            lua.execute(SOURCE)
            self.assertEqual(lua.eval('X4LAN_NATIVE_PLAYER_COMMANDS.proposal(29)'),'81010000')
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');X4LAN_NATIVE_PLAYER(1,gate)")
            with self.assertRaisesRegex(Exception,'ownership refused'):
                lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','81010000')")
            self.assertEqual(len(lua.globals().calls),0)
            self.assertTrue(any('PLAYER_BATCH_REJECTED tick=2 p1=81010000 p2=81010000 effects=0' in line for line in lua.globals().logs.values()))

    def test_independent_parser_checks_proposals_and_does_not_accept_aliases_as_players(self):
        lua=self.runtime()
        lua.execute("X4LAN_CONFIG.native_gate_player_probe='agreed-singleton'")
        lua.execute(SOURCE)
        lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(1,'00000000','00000000');X4LAN_NATIVE_PLAYER(1,gate);X4LAN_NATIVE_PLAYER_COMMANDS.apply(2,'81010000','00000000');X4LAN_NATIVE_PLAYER(2,gate)")
        log='\n'.join(lua.globals().logs.values())
        states={0:{'1':{'command':'00000000'},'2':{'command':'00000000'}},
                1:{'1':{'command':'81010000'},'2':{'command':'00000000'}},2:{}}
        errors=[]
        samples,inputs,result=evidence(log,1,'agreed-singleton',states,errors)
        self.assertEqual(errors,[])
        self.assertEqual(set(samples),{1,2})
        self.assertFalse(result['independent_native_players'])
        self.assertFalse(result['input_capture_complete'])
        states[1]['1']['command']='82010000'
        errors=[]
        evidence(log,1,'agreed-singleton',states,errors)
        self.assertTrue(any('not admitted' in error for error in errors))

    def test_authority_evidence_requires_admitted_conflict_zero_effects_and_restoration(self):
        lua=self.runtime()
        lua.execute("X4LAN_CONFIG.native_gate_player_probe='authority-negative'")
        lua.execute(SOURCE)
        lua.execute("for tick=1,29 do X4LAN_NATIVE_PLAYER_COMMANDS.apply(tick,'00000000','00000000');X4LAN_NATIVE_PLAYER(tick,gate) end")
        with self.assertRaisesRegex(Exception,'ownership refused'):
            lua.execute("X4LAN_NATIVE_PLAYER_COMMANDS.apply(30,'81010000','81010000')")
        log='\n'.join(lua.globals().logs.values())+'\n[X4LAN] NATIVE_GATE ERROR native player context ownership refused\n[X4LAN] NATIVE_GATE ABORT stopped=1 active=0 restored=1 tick=30'
        states={tick:{str(slot):{'command':'81010000' if tick==29 else '00000000'} for slot in (1,2)} for tick in range(30)}
        errors=[]
        _,_,result=evidence(log,1,'authority-negative',states,errors)
        self.assertEqual(errors,[])
        self.assertEqual(result['actions'],[])
        for before,after in [('effects=0','effects=1'),('restored=1','restored=0'),('p2=81010000','p2=00000000')]:
            errors=[]
            evidence(log.replace(before,after),1,'authority-negative',states,errors)
            self.assertTrue(errors)
        states[29]['2']['command']='00000000'
        errors=[]
        evidence(log,1,'authority-negative',states,errors)
        self.assertTrue(any('admitted conflict' in error for error in errors))


if __name__=='__main__':unittest.main()
