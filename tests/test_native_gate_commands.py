import importlib.util
from pathlib import Path
import unittest
import tempfile
import json
from lupa.luajit21 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'tools/dev/native_gate_commands.lua').read_text()
spec = importlib.util.spec_from_file_location('native_gate_validator', ROOT/'tools/dev/validate_native_gate.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)
builder_spec=importlib.util.spec_from_file_location('native_gate_builder',ROOT/'tools/dev/build_native_gate_overlay.py')
builder=importlib.util.module_from_spec(builder_spec);builder_spec.loader.exec_module(builder)


class NativeCommandTests(unittest.TestCase):
    def test_replay_preserves_independent_capture_frames_and_rejects_bad_encoding(self):
        report={'protocol_version':2,'states':{
            '48':{'1':{'command':'03010100'},'2':{'command':'03070200'}},
            '324':{'1':{'command':'02010000'},'2':{'command':'00000000'}},
            '325':{'1':{'command':'00000000'},'2':{'command':'02070000'}}}}
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'barrier.json';path.write_text(json.dumps(report))
            plan=builder.replay_plan(path)
            lua=LuaRuntime(unpack_returned_tuples=True)
            plans=lua.eval(plan)
            self.assertEqual(plans[1][324],'02010000')
            self.assertIsNone(plans[2][324])
            self.assertEqual(plans[2][325],'02070000')
            report['states']['48']['1']['command']='bad"lua!'
            path.write_text(json.dumps(report))
            with self.assertRaises(ValueError):builder.replay_plan(path)

    def test_extra_steering_generations_require_complete_per_actor_evidence(self):
        self.assertIsNotNone(validator.steering_initializations('NATIVE_GATE STEERING_INIT count=12'))
        self.assertIsNone(validator.steering_initializations('NATIVE_GATE STEERING_INIT count=20'))
        rows = ','.join(f'{i}/{3 if i in (1, 3, 7, 9) else 1}' for i in range(1, 13))
        log = 'NATIVE_GATE STEERING_INIT count=20\nNATIVE_GATE STEERING_INIT_TOTALS data='+rows
        self.assertIsNotNone(validator.steering_initializations(log))
        self.assertIsNone(validator.steering_initializations(log.replace('count=20', 'count=19')))
        self.assertIsNone(validator.steering_initializations(log.replace(',12/1', '')))
        self.assertIsNone(validator.steering_initializations(log.replace('12/1', '11/1')))

    def adapter(self, mode, slot, plan='combat', **config):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('events={};logs={};AddUITriggeredEvent=function(s,c,a) events[#events+1]=a end;DebugError=function(s) logs[#logs+1]=s end')
        lua.globals().X4LAN_CONFIG = lua.table_from({'native_gate_command_test': mode, 'slot': slot, 'native_gate_command_plan': plan, **config})
        lua.execute(SOURCE)
        return lua

    def test_two_distinct_players_propose_and_apply_same_ordered_batch(self):
        a, b = self.adapter('pvp', 'p1'), self.adapter('pvp', 'p2')
        first = a.globals().X4LAN_NATIVE_COMMANDS.proposal(60)
        second = b.globals().X4LAN_NATIVE_COMMANDS.proposal(90)
        self.assertNotEqual(first, second)
        for lua in (a, b):
            suffix = lua.globals().X4LAN_NATIVE_COMMANDS.apply(91, first, second)
            self.assertEqual(suffix, 'INPUTS_1:pvp:02010000:02070000')
            events = lua.globals().events
            self.assertEqual((events[1].slot, events[1].actor, events[2].slot, events[2].actor), (1, 1, 2, 7))

    def test_ownership_and_complete_batch_validation_precede_mutation(self):
        lua = self.adapter('pvp', 'p1')
        apply = lua.globals().X4LAN_NATIVE_COMMANDS.apply
        for a, b in [('02010000', '02020000'), ('02070000', '00000000'),
                     ('01010200', '00000000'), ('02010001', '00000000')]:
            with self.assertRaises(Exception):
                apply(61, a, b)
            self.assertEqual(len(lua.globals().events), 0)

    def test_coop_uses_separate_ships_on_same_team(self):
        lua = self.adapter('coop', 'p2')
        adapter = lua.globals().X4LAN_NATIVE_COMMANDS
        self.assertEqual(adapter.proposal(150), '01020800')
        adapter.apply(151, '01010700', '01020800')
        self.assertEqual([e.actor for e in lua.globals().events.values()], [1, 2])
        with self.assertRaises(Exception):
            adapter.apply(152, '00000000', '01070100')

    def test_maneuvers_route_owned_movement_and_friendly_escort(self):
        lua = self.adapter('coop', 'p2', 'maneuvers')
        adapter=lua.globals().X4LAN_NATIVE_COMMANDS
        self.assertEqual(adapter.proposal(90),'03020200')
        self.assertEqual(adapter.proposal(150),'04040200')
        adapter.apply(91,'03010100','03020200')
        adapter.apply(151,'04030100','04040200')
        self.assertEqual([e.op for e in lua.globals().events.values()],[3,3,4,4])
        for command in ('03020300','04040700','04040400'):
            with self.assertRaises(Exception): adapter.apply(152,'00000000',command)

    def test_travel_plan_preserves_owner_and_exposes_longer_native_flight(self):
        lua=self.adapter('coop','p2','travel');adapter=lua.globals().X4LAN_NATIVE_COMMANDS
        self.assertEqual(adapter.proposal(90),'05020200')
        self.assertEqual(adapter.proposal(1830),'05020100')
        adapter.apply(91,'05010100','05020200')
        self.assertEqual([e.op for e in lua.globals().events.values()],[5,5])
        with self.assertRaises(Exception):adapter.apply(92,'05010300','00000000')

    def test_travel_evidence_requires_actual_active_flag_and_complete_samples(self):
        lines=['NATIVE_GATE TRAVEL tick=30 id='+str(i)+' alive=1 charging=0 active='+('1' if i in (1,2) else '0')+
               ' speed=1500 maxspeed=250 charge=1 attention=nearby time=7.6' for i in range(1,13)]
        errors=[];rows,active=validator.travel_evidence('\n'.join(lines),1,60,errors)
        self.assertEqual(errors,[]);self.assertEqual(active,{1,2});self.assertEqual(len(rows),12)
        for bad in (lines[1:],lines+[lines[0]],[s.replace('speed=1500','speed=nan') for s in lines],
                    [s.replace('attention=nearby','attention=invalid') for s in lines]):
            errors=[];validator.travel_evidence('\n'.join(bad),1,60,errors);self.assertTrue(errors)

    def test_withdraw_requires_boost_telemetry_and_owned_actor(self):
        lua=self.adapter('coop','p2','withdraw');adapter=lua.globals().X4LAN_NATIVE_COMMANDS
        with self.assertRaises(Exception):adapter.proposal(45)
        lua.execute('X4LAN_CONFIG.native_gate_boost_diag=true')
        self.assertEqual(adapter.proposal(45),'08020000')
        adapter.apply(46,'08010000','08020000')
        self.assertEqual([e.actor for e in lua.globals().events.values()],[1,2])
        for a,b in (('08010100','00000000'),('00000000','08070000')):
            with self.assertRaises(Exception):adapter.apply(47,a,b)
        self.assertEqual(len(lua.globals().events),2)

    def test_boost_evidence_uses_precommand_baseline_not_dynamic_boost_ceiling(self):
        initial=['NATIVE_GATE BOOST tick=30 id='+str(i)+' alive=1 active=0 available=1 remaining=1 speed=50 maxspeed=250 time=7.6' for i in range(1,13)]
        active=['NATIVE_GATE BOOST tick=90 id='+str(i)+' alive=1 active='+('1' if i in (1,2) else '0')+
                ' available=0 remaining=0.75 speed=1000 maxspeed=2500 time=8.6' for i in range(1,13)]
        lines=initial+active
        errors=[];rows,boosted=validator.boost_evidence('\n'.join(lines),1,120,errors)
        self.assertEqual(errors,[]);self.assertEqual(boosted,{1,2});self.assertEqual(len(rows),24)
        for bad in (lines[1:],lines+[lines[0]],[s.replace('speed=1000','speed=nan') for s in lines],[s.replace('remaining=0.75','remaining=1.1') for s in lines]):
            errors=[];validator.boost_evidence('\n'.join(bad),1,120,errors);self.assertTrue(errors)
        for before,after in (('active=1','active=0'),('speed=1000','speed=200'),('remaining=0.75','remaining=1'),('tick=30','tick=31')):
            errors=[];_,boosted=validator.boost_evidence('\n'.join(lines).replace(before,after),1,120,errors);self.assertFalse(boosted)

    def test_dock_order_requires_capture_and_own_anchor(self):
        for mode,actor in (('coop',2),('pvp',7)):
            lua=self.adapter(mode,'p2','docking');adapter=lua.globals().X4LAN_NATIVE_COMMANDS
            with self.assertRaises(Exception):adapter.proposal(45)
            lua.execute('X4LAN_CONFIG.native_gate_docking_state=true')
            self.assertEqual(adapter.proposal(45),f'09{actor:02x}0200')
            adapter.apply(46,'09010100',adapter.proposal(45))
            self.assertEqual([e.target for e in lua.globals().events.values()],[1,2])
            for command in (f'09{actor:02x}0100',f'09{actor:02x}0300'):
                with self.assertRaises(Exception):adapter.apply(47,'00000000',command)
            self.assertEqual(len(lua.globals().events),2)

    def test_docking_exit_variant_is_explicit_and_requires_docking_fixture(self):
        for exit_route in ('diagonal','above'):
            for slot in ('p1','p2'):
                lua=self.adapter('coop',slot,'docking',native_gate_docking_state=True,native_gate_docking_exit=exit_route)
                lua.globals().X4LAN_NATIVE_COMMANDS.apply(6001,'03010100','03020200')
                self.assertEqual([e.dockexit for e in lua.globals().events.values()],[exit_route,exit_route])
        for plan,capture,exit_route in [('combat',True,'above'),('docking',False,'above'),('docking',True,'unknown')]:
            with self.assertRaises(Exception):
                self.adapter('coop','p1',plan,native_gate_docking_state=capture,native_gate_docking_exit=exit_route)

    def test_fleet_orders_require_state_capture_and_preserve_team_ownership(self):
        lua=self.adapter('coop','p2','fleet');adapter=lua.globals().X4LAN_NATIVE_COMMANDS
        with self.assertRaises(Exception):adapter.proposal(90)
        lua.execute('X4LAN_CONFIG.native_gate_fleet_state=true')
        self.assertEqual(adapter.proposal(90),'06040200')
        self.assertEqual(adapter.proposal(1230),'07040000')
        adapter.apply(91,'06030100','06040200')
        self.assertEqual([e.actor for e in lua.globals().events.values()],[3,4])
        for a,b in (('06030700','00000000'),('06030300','00000000'),('00000000','07040100')):
            with self.assertRaises(Exception):adapter.apply(92,a,b)
        self.assertEqual(len(lua.globals().events),2)

    def test_validator_requires_native_application_and_identical_admitted_inputs(self):
        states = {0: {'1': {'command': '02010000'}, '2': {'command': '01070100'}}, 1: {}}
        log = ('NATIVE_GATE INPUTS tick=1 mode=pvp p1=02010000 p2=01070100\n'
               'NATIVE_GATE COMMAND_APPLIED tick=1 slot=1 op=2 actor=1 target=0 time=7.1\n'
               'NATIVE_GATE COMMAND_APPLIED tick=1 slot=2 op=1 actor=7 target=1 time=7.1\n')
        errors = []
        suffix, applied = validator.command_evidence(log, 1, 'pvp', states, errors)
        self.assertEqual(errors, [])
        self.assertEqual(len(applied), 2)
        self.assertEqual(suffix[1], 'INPUTS_1:pvp:02010000:01070100')
        for bad in (log.split('NATIVE_GATE COMMAND_APPLIED')[0], log.replace('p2=01070100', 'p2=00000000')):
            errors = []
            validator.command_evidence(bad, 1, 'pvp', states, errors)
            self.assertTrue(errors)


if __name__ == '__main__':
    unittest.main()
