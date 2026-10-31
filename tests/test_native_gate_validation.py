"""Canonical state logs must remain complete across X4's 8 KiB line limit."""
import importlib.util
from pathlib import Path
import unittest
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gate_validation', ROOT/'tools/dev/validate_native_gate.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class StateLogTests(unittest.TestCase):
    def test_readiness_tracks_each_start_repetition_and_generation(self):
        saved='0000000000000123';start=['0']*24
        for i,value in {0:'22',1:'1',4:saved,13:'100',15:'200',19:'4036000000000000',22:'1',23:'4'}.items():start[i]=value
        row=f'NATIVE_GATE NPC_READY tick=23 ordinal=1 enqueue=1 origin=4 saved={saved} start_event=1 generation=7 page=100 line=200 duration=3ff0000000000000 estimate=4000000000000000 start=4036000000000000 clock=4036000000000000 repeat=1'
        second=row.replace('tick=23 ordinal=1','tick=24 ordinal=2').replace('repeat=1','repeat=2')
        stop='\nNATIVE_GATE NPC_READY_STOP calls=2 restored=1 failures=0\nNATIVE_GATE NPC_READY_COVERAGE seen=2 foreign=0 foreign_thread=0'
        good=row+'\n'+second+stop;errors=[]
        self.assertEqual(len(module.npc_ready_evidence(good,1,100,errors,[start])),2);self.assertFalse(errors)
        for bad in ('',good.replace('start_event=1','start_event=2'),good.replace('restored=1','restored=0'),
                    good.replace('foreign_thread=0','foreign_thread=1'),good.replace('seen=2','seen=3'),
                    row+'\n'+second.replace('generation=7','generation=8')+stop,
                    row+'\n'+second.replace('repeat=2','repeat=1')+stop,
                    row+'\n'+second.replace('duration=3ff0000000000000','duration=4000000000000000')+stop,
                    good.replace('duration=3ff0000000000000','duration=7ff0000000000000'),
                    good+'\nNATIVE_GATE NPC_READY tick=broken'):
            errors=[];module.npc_ready_evidence(bad,1,100,errors,[start]);self.assertTrue(errors,bad)

    def test_deadline_requires_one_finish_actual_duration_and_no_premature_completion(self):
        saved='0000000000000123';start=['0']*24
        for i,value in {0:'22',1:'1',4:saved,13:'100',15:'200',19:'4036000000000000',22:'1',23:'4'}.items():start[i]=value
        ready=[('23','1','1','4',saved,'1','1','100','200','3ff0000000000000','4000000000000000','4036000000000000','4036000000000000','1')]
        finish=('80','1','1','4',saved,'564119','0','1','4','100','200','3ff0000000000000','4036000000000000','4037000000000000','0','0','0','0'*16,'0'*16)
        row=f'NATIVE_GATE NPC_DEADLINE tick=30 ordinal=1 enqueue=1 origin=4 saved={saved} page=100 line=200 duration=3ff0000000000000 start=4036000000000000 clock=4036800000000000 early=1'
        stop='\nNATIVE_GATE NPC_DEADLINE_STOP calls=1 restored=1 failures=0\nNATIVE_GATE NPC_DEADLINE_COVERAGE seen=1 foreign=0 foreign_thread=0'
        good=row+stop;errors=[]
        self.assertEqual(len(module.npc_deadline_evidence(good,1,100,errors,[start],[finish],ready)),1);self.assertFalse(errors)
        for bad in ('',good.replace('early=1','early=0'),good.replace('enqueue=1','enqueue=2'),good.replace('restored=1','restored=0'),
                    good.replace('seen=1','seen=2'),good.replace('foreign_thread=0','foreign_thread=1'),good+'\nNATIVE_GATE NPC_DEADLINE tick=broken'):
            errors=[];module.npc_deadline_evidence(bad,1,100,errors,[start],[finish],ready);self.assertTrue(errors,bad)
        for finishes in ([],[finish,finish],[finish[:11]+('4000000000000000',)+finish[12:]],
                         [finish[:13]+('4036800000000000',)+finish[14:]]):
            errors=[];module.npc_deadline_evidence(good,1,100,errors,[start],finishes,ready);self.assertTrue(errors)
        errors=[];module.npc_deadline_evidence(good,1,100,errors,[start],[finish],[]);self.assertTrue(errors)
        interrupted=finish[:5]+('55ca38','1')+finish[7:13]+('4036800000000000',)+finish[14:]
        errors=[];module.npc_deadline_evidence(good,1,100,errors,[start],[interrupted],ready);self.assertFalse(errors)

    def test_native_finish_requires_active_start_provenance_without_hiding_early_completion(self):
        zero='0'*16;saved='0000000000000123';start=['0']*24
        for i,value in {0:'22',4:saved,13:'100',15:'200',17:'3ff0000000000000',19:'4036000000000000',22:'1',23:'4'}.items():start[i]=value
        row=f'NATIVE_GATE NPC_FINISH tick=30 ordinal=1 enqueue=1 origin=4 saved={saved} caller=563f7b interrupted=0 audio=1 state=5 handle=123 page=100 line=200 duration=3ff0000000000000 start=4036000000000000 clock=4036800000000000 flags=0/1/0 rng={zero}/{zero}'
        good=row+'\nNATIVE_GATE NPC_FINISH_STOP calls=1 restored=1 failures=0\nNATIVE_GATE NPC_FINISH_COVERAGE seen=1 foreign_thread=0 unowned=0'
        errors=[];rows=module.npc_finish_evidence(good,1,30,errors,[start]);self.assertFalse(errors);self.assertEqual(len(rows),1)
        for bad in ('',good.replace('origin=4','origin=5'),good.replace('enqueue=1','enqueue=2'),good.replace('page=100','page=101'),
                    good.replace('restored=1','restored=0'),good.replace('seen=1','seen=2'),good.replace('foreign_thread=0','foreign_thread=1'),
                    good.replace('clock=4036800000000000','clock=7ff0000000000000'),good+'\nNATIVE_GATE NPC_FINISH tick=broken'):
            errors=[];module.npc_finish_evidence(bad,1,30,errors,[start]);self.assertTrue(errors,bad)
        errors=[];same=module.npc_finish_evidence(good.replace('handle=123','handle=456'),2,30,errors,[start]);self.assertFalse(errors);self.assertEqual(rows,same)
        errors=[];different=module.npc_finish_evidence(good.replace('interrupted=0','interrupted=1'),2,30,errors,[start]);self.assertFalse(errors);self.assertNotEqual(rows,different)
        # Native audio readiness replaces the initial duration estimate. The
        # changed final duration is valid evidence but must remain unequal.
        errors=[];actual=module.npc_finish_evidence(good.replace('duration=3ff0000000000000','duration=4000000000000000'),2,30,errors,[start]);self.assertFalse(errors);self.assertNotEqual(rows,actual)

    def test_npc_scope_requires_pre_execution_seed_parent_order_and_restoration(self):
        cue='NPC_UseCases.UseCase';program=0x1234;pc=0
        key,seed=module.npc_exec_identity(cue,program,pc,22,1)
        row=f'NATIVE_GATE NPC_EXEC tick=22 ordinal=1 parent=0 depth=1 key={key:016x} call=1 program={program:016x} pc=0 before=0000000000000001 scoped={seed:016x} after=0000000000000002 restored=0000000000000001 cue={cue}'
        stop='NATIVE_GATE NPC_EXEC_STOP calls=1 restored=1 failures=0 identity=ordered_invocation_v1'
        good=row+'\n'+stop;errors=[]
        rows=module.npc_exec_evidence(good,1,22,errors);self.assertFalse(errors)
        for bad in ('',stop.replace('calls=1','calls=0'),good.replace('call=1','call=2'),
                    good.replace(f'scoped={seed:016x}','scoped=0000000000000001'),
                    good.replace('restored=0000000000000001','restored=0000000000000003'),
                    good.replace('parent=0','parent=1'),good.replace('depth=1','depth=2'),
                    good.replace('identity=ordered_invocation_v1','identity=stable'),
                    good.replace('pc=0','pc=1'),good.replace('restored=1','restored=0'),
                    good+'\nNATIVE_GATE NPC_EXEC tick=truncated'):
            errors=[];module.npc_exec_evidence(bad,1,22,errors);self.assertTrue(errors,bad)
        # A different caller stream is audited but is the intended intervention.
        other=good.replace('before=0000000000000001','before=0000000000000003').replace('restored=0000000000000001','restored=0000000000000003')
        errors=[];other_rows=module.npc_exec_evidence(other,2,22,errors);self.assertFalse(errors)
        self.assertEqual(module.npc_exec_payloads(rows),module.npc_exec_payloads(other_rows))
        changed=module.npc_exec_evidence(good.replace('after=0000000000000002','after=0000000000000003'),2,22,[])
        self.assertNotEqual(module.npc_exec_payloads(rows),module.npc_exec_payloads(changed))
        _,second_seed=module.npc_exec_identity(cue,program,pc,22,2)
        second=row.replace('ordinal=1','ordinal=2').replace('parent=0 depth=1','parent=1 depth=2').replace('call=1','call=2').replace(f'scoped={seed:016x}',f'scoped={second_seed:016x}')
        errors=[];self.assertEqual(len(module.npc_exec_evidence(row+'\n'+second+'\n'+stop.replace('calls=1','calls=2'),1,22,errors)),2);self.assertFalse(errors)

    def test_speech_selection_requires_saved_identity_owner_and_immutable_fields(self):
        zero='0'*16
        row=f'NATIVE_GATE NPC_SPEAK tick=22 ordinal=1 action=4 exec=0 saved=0000000000000123 entity=0000000000000456 caller=561ab0 clock=4036000000000000 state=0/5 handle=0/123 page=0/100 line=0/200 duration={zero}/3ff0000000000000 start={zero}/4036000000000000 rng={zero}/{zero} enqueue=1 origin=4'
        prefix=f'NATIVE_GATE NPC_ACTION tick=22 ordinal=4 \nNATIVE_GATE NPC_ENQUEUE tick=22 ordinal=1 action=4 exec=0 saved=0000000000000123 priority=-7 interrupt=1 recipients={zero}/{zero} flags=6 count=1 result=1 rng={zero}/{zero} lines=00000064/000000c8\nNATIVE_GATE NPC_ENQUEUE_STOP calls=1 restored=1 failures=0'
        stop='NATIVE_GATE NPC_SPEAK_STOP calls=1 restored=1 failures=0\nNATIVE_GATE NPC_SPEAK_COVERAGE seen=1 foreign_thread=0 unowned=0'
        good=prefix+'\n'+row+'\n'+stop;errors=[]
        rows=module.npc_speak_evidence(good,1,22,errors);self.assertFalse(errors)
        for bad in ('',stop.replace('calls=1','calls=0'),good.replace('action=4','action=5'),
                    good.replace('origin=4','origin=5'),good.replace('enqueue=1','enqueue=2'),good.replace('exec=0','exec=4'),
                    good.replace('saved=0000000000000123','saved='+zero),
                    good.replace('caller=561ab0','caller=0'),good.replace('4036000000000000','7ff0000000000000'),
                    good.replace('restored=1','restored=0'),good.replace('calls=1','calls=2'),
                    good.replace('foreign_thread=0','foreign_thread=1'),good.replace('seen=1','seen=2'),
                    good.replace('result=1','result=0'),good.replace('count=1','count=2'),
                    good+'\nNATIVE_GATE NPC_SPEAK tick=truncated'):
            errors=[];module.npc_speak_evidence(bad,1,22,errors);self.assertTrue(errors,bad)
        def parsed(text):
            errors=[];result=module.npc_speak_evidence(text,2,22,errors);self.assertFalse(errors);return result
        relocated=parsed(good.replace('entity=0000000000000456','entity=0000000000000789').replace('handle=0/123','handle=0/456'))
        self.assertEqual(module.npc_speak_payloads(rows),module.npc_speak_payloads(relocated))
        for before,after in [('line=0/200','line=0/201'),('3ff0000000000000','4000000000000000')]:
            self.assertNotEqual(module.npc_speak_payloads(rows,True),module.npc_speak_payloads(parsed(good.replace(before,after)),True))
        later=parsed(good.replace('clock=4036000000000000','clock=4037000000000000'))
        self.assertEqual(module.npc_speak_payloads(rows,True),module.npc_speak_payloads(later,True))
        self.assertNotEqual(module.npc_speak_payloads(rows),module.npc_speak_payloads(later))
        second=row.replace('ordinal=1','ordinal=2').replace('action=4','action=0')
        errors=[];self.assertEqual(len(module.npc_speak_evidence(prefix+'\n'+row+'\n'+second+'\n'+stop.replace('calls=1','calls=2').replace('seen=1','seen=2'),1,22,errors)),2);self.assertFalse(errors)

    def test_named_npc_lifecycle_requires_name_and_explicit_optional_fixture_parent(self):
        word='0'*16
        row=f'NATIVE_GATE NPC_ACTION tick=22 ordinal=1 interrupt=0 step=0 actor=0 parent=0 depth=1 owner=5d program=0000000000000001 pc=0 next=2 type=1000 action=2000 chance=0 flags=0/0 rng={word}/{word} named=1 cue=NPC_UseCases.UseCase'
        good=row+'\nNATIVE_GATE NPC_ACTION_STOP calls=1 restored=1 failures=0'
        errors=[];self.assertEqual(len(module.npc_action_evidence(good,1,22,errors)),1);self.assertFalse(errors)
        for bad in ('','NATIVE_GATE NPC_ACTION_STOP calls=0 restored=1 failures=0',
                    good.replace('named=1','named=0'),good.replace('NPC_UseCases.UseCase','unreadable'),
                    good.replace('owner=5d','owner=66'),good.replace('actor=0','actor=1'),
                    good.replace('step=0','step=4'),good.replace('restored=1','restored=0'),
                    good.replace('depth=1','depth=2'),good.replace('parent=0','parent=1'),
                    good.replace('calls=1','calls=2'),good+'\nNATIVE_GATE NPC_ACTION tick=broken'):
            errors=[];module.npc_action_evidence(bad,1,22,errors);self.assertTrue(errors,bad)
        owned='NATIVE_GATE INTERRUPT_STEP tick=22 ordinal=4 parent=3 actor=1 \n'+good.replace('interrupt=0 step=0 actor=0','interrupt=3 step=4 actor=1')
        errors=[];self.assertEqual(len(module.npc_action_evidence(owned,1,22,errors)),1);self.assertFalse(errors)

    def test_nested_actions_require_explicit_instruction_and_nested_parents(self):
        word='0'*16
        step='NATIVE_GATE INTERRUPT_STEP tick=22 ordinal=4 parent=3 actor=1 '
        row=f'NATIVE_GATE NESTED_ACTION tick=22 ordinal=1 interrupt=3 step=4 actor=1 parent=0 depth=1 owner=5d program=0000000000000001 pc=0 next=2 type=1000 action=2000 chance=0 flags=0/0 rng={word}/{word}'
        good=step+'\n'+row+'\nNATIVE_GATE NESTED_ACTION_STOP calls=1 restored=1 failures=0'
        errors=[];self.assertEqual(len(module.nested_action_evidence(good,1,22,errors)),1);self.assertFalse(errors)
        for bad in ('',good.replace('step=4','step=5'),good.replace('interrupt=3','interrupt=4'),
                    good.replace('depth=1','depth=2'),good.replace('parent=0','parent=1'),
                    good.replace('pc=0','pc=1048576'),good.replace('type=1000','type=6fa9000'),
                    good.replace('program=0000000000000001','program='+word),
                    good.replace('restored=1','restored=0'),good.replace('calls=1','calls=2'),
                    good+'\nNATIVE_GATE NESTED_ACTION tick=broken'):
            errors=[];module.nested_action_evidence(bad,1,22,errors);self.assertTrue(errors,bad)
        second=row.replace('ordinal=1','ordinal=2').replace('parent=0 depth=1','parent=1 depth=2')
        errors=[];self.assertEqual(len(module.nested_action_evidence(good.replace('calls=1','calls=2')+'\n'+second,1,22,errors)),2);self.assertFalse(errors)

    def test_instruction_trace_requires_native_parent_and_complete_restored_records(self):
        word='0'*16
        parent='NATIVE_GATE INTERRUPT tick=22 ordinal=3 actor=1 kind=1 '
        row=f'NATIVE_GATE INTERRUPT_STEP tick=22 ordinal=1 parent=3 actor=1 pc=0 next=2 type=1000 action=2000 chance=0 flags=0/0 rng={word}/{word}'
        good=parent+'\n'+row+'\nNATIVE_GATE INTERRUPT_STEP_STOP calls=1 restored=1 failures=0'
        errors=[];self.assertEqual(len(module.interrupt_step_evidence(good,1,22,errors)),1);self.assertFalse(errors)
        for bad in ('',good.replace('parent=3','parent=4'),good.replace('pc=0','pc=1048576'),
                    good.replace('type=1000','type=6fa9000'),good.replace('action=2000','action=0'),
                    good.replace('restored=1','restored=0'),good.replace('calls=1','calls=2'),
                    good.replace('ordinal=1','ordinal=2'),good+'\nNATIVE_GATE INTERRUPT_STEP tick=broken',
                    good.replace('flags=0/0','flags=0/65536')):
            errors=[];module.interrupt_step_evidence(bad,1,22,errors);self.assertTrue(errors,bad)

    def test_radar_contact_snapshots_require_every_stage_and_untruncated_identity(self):
        scans={(4,10):(1,)}
        lines=[f'NATIVE_GATE GRAVIDAR_CONTACTS id=4 tick=10 calls=1 stage={stage} present=1 count=1 ids=0000000000000123/2' for stage in range(1,9)]
        good='\n'.join(lines);errors=[]
        rows=module.gravidar_contact_trace(good,1,scans,errors)
        self.assertFalse(errors);self.assertEqual(rows[(4,10,3)],(1,((0x123,2),)))
        for bad in ('', '\n'.join(lines[:-1]),good+'\n'+lines[0],good.replace('calls=1','calls=2'),
                    good.replace('count=1','count=2'),good.replace('0123/2','0123/13'),
                    good.replace('0000000000000123/2','123/2'),good.replace('0000000000000123/2','0000000000000000/2'),
                    good.replace('tick=10','tick=11'),good.replace('stage=8','stage=9'),
                    good.replace('present=1','present=0'),good.replace('ids=','ids=broken')):
            errors=[];module.gravidar_contact_trace(bad,1,scans,errors);self.assertTrue(errors,bad)
        empty=good.replace('count=1 ids=0000000000000123/2','count=0 ids=-')
        empty=empty.replace('stage=2 present=1','stage=2 present=0').replace('stage=8 present=1','stage=8 present=0')
        errors=[];rows=module.gravidar_contact_trace(empty,1,scans,errors)
        self.assertFalse(errors);self.assertNotEqual(rows[(4,10,1)],rows[(4,10,2)])

    def test_dock_refresh_requires_complete_cadence_coverage(self):
        good='NATIVE_GATE DOCK_REFRESH_STOP calls=300 forced=300'
        errors=[];module.dock_refresh_evidence(good,1,errors);self.assertFalse(errors)
        for bad in ('',good+'\n'+good,good.replace('forced=300','forced=299'),good.replace('300','0')):
            errors=[];module.dock_refresh_evidence(bad,1,errors);self.assertTrue(errors)

    def test_shape_coverage_includes_extra_bodies_without_double_initializing_shared_holders(self):
        rows=','.join(f'{actor}/{body}/{int(actor<=12 or body==0)}' for actor in range(1,15) for body in (0,1))
        good='NATIVE_GATE SHAPE_STOP calls=100 restored=1 failures=0 initial_refreshes=27\nNATIVE_GATE SHAPE_INIT_TOTALS data='+rows+',13/3/1,14/3/0\nNATIVE_GATE DOCK_SHAPE_STOP registered=2 extras=2 extra_calls=4'
        errors=[];module.shape_initialization_evidence(good,1,True,True,errors);self.assertFalse(errors)
        for before,after in [('extras=2','extras=1'),('extra_calls=4','extra_calls=1'),('14/3/0','14/17/0'),
                             ('14/3/0','13/3/0'),('13/3/1','13/3/0'),('1/0/1','1/0/0'),('restored=1','restored=0'),
                             ('initial_refreshes=27','initial_refreshes=28'),('data=','data=1/0/1,'),('13/3/1','13/3')]:
            with self.subTest(before=before,after=after):
                errors=[];module.shape_initialization_evidence(good.replace(before,after),1,True,True,errors);self.assertTrue(errors)
        errors=[];module.shape_initialization_evidence(good+'\nNATIVE_GATE SHAPE_INIT_TOTALS data=1/0/1',1,True,True,errors);self.assertTrue(errors)

    def test_interrupt_trace_requires_complete_records_and_restoration(self):
        word='0'*16
        good=f'NATIVE_GATE INTERRUPT tick=2254 ordinal=1 actor=5 kind=1 event=506 flags=0/4 script=order.fight.escort pc=368 range=1/20 mode=1 rng={word}/{word} values=a:{word},0:{word} result=0\nNATIVE_GATE INTERRUPT_STOP calls=1 restored=1 failures=0'
        errors=[];self.assertEqual(module.interrupt_evidence(good,1,2254,errors),1);self.assertFalse(errors)
        for log in ('',good.replace('ordinal=1','ordinal=2'),good.replace('calls=1','calls=2'),good.replace('restored=1','restored=0'),good.replace('tick=2254','tick=2255'),good+'\nNATIVE_GATE INTERRUPT tick=broken'):
            errors=[];module.interrupt_evidence(log,1,2254,errors);self.assertTrue(errors)

    def test_interrupt_rng_count_and_semantics_exclude_event_storage_addresses(self):
        row='NATIVE_GATE INTERRUPT tick=22 ordinal=3 actor=1 kind=1 event=527 flags=4/4 script=fight.attack pc=23 range=1/2 mode=1 rng=1111111111111111/2222222222222222 values=a:0000000000000001,0:0000000000000000 result=0'
        log=row+'\nNATIVE_GATE INTERRUPT_SEEDS count=1'
        errors=[];before=module.interrupt_rng_evidence(log,1,errors);self.assertFalse(errors)
        self.assertEqual(before,module.interrupt_rng_evidence(log.replace('ordinal=3','ordinal=4').replace('values=a:0000000000000001','values=a:0000000000000002'),1,errors))
        self.assertNotEqual(before,module.interrupt_rng_evidence(log.replace('rng=1111','rng=3333'),1,errors))
        module.interrupt_rng_evidence(log.replace('count=1','count=0'),1,errors);self.assertTrue(errors)

    def test_native_damage_trace_requires_contiguous_events_and_restoration(self):
        word='0'*16;pair=word+','+word
        good=f'NATIVE_GATE DAMAGE id=12 tick=3571 ordinal=1 time={word} amount={word} absorbed={word} priority=0 before={pair} after={pair} stack=51f958,5209ff,0,0,0,0,0,0\nNATIVE_GATE DAMAGE_STOP calls=1 restored=1 failures=0'
        errors=[];self.assertEqual(module.damage_evidence(good,1,3571,errors),1);self.assertFalse(errors)
        self.assertEqual(module.damage_payloads(good),module.damage_payloads(good.replace('stack=51f958','stack=51f959')))
        self.assertNotEqual(module.damage_payloads(good),module.damage_payloads(good.replace('amount='+word,'amount='+'1'*16)))
        for log in ('',good.replace('calls=1','calls=2'),good.replace('ordinal=1','ordinal=2'),good.replace('restored=1','restored=0'),good.replace('51f958','ffffffff')):
            errors=[];module.damage_evidence(log,1,3571,errors);self.assertTrue(errors)

    def test_radar_scan_trace_is_complete_bit_exact_and_repeated_samples_are_immutable(self):
        zero='0'*16;times=','.join([zero]*3)
        good=f'NATIVE_GATE GRAVIDAR_UNIT id=4 tick=10 calls=1 time={zero} before={times} after={times} rng_before={zero} rng_after={zero}'
        errors=[];rows=module.gravidar_trace(good+'\n'+good,1,10,1,errors)
        self.assertEqual(len(rows),1);self.assertFalse(errors)
        for log,total in (('',1),(good,2),(good.replace('id=4','id=13'),1),(good.replace('tick=10','tick=11'),1),
                          (good+'\n'+good.replace('rng_after='+zero,'rng_after='+'1'*16),1)):
            errors=[];module.gravidar_trace(log,1,10,total,errors);self.assertTrue(errors)
        event='NATIVE_GATE RADAR id=4 tick=10 ordinal=1 source=4 flags=17 script=move.seekenemies'
        self.assertEqual(module.radar_payloads(event),module.radar_payloads(event.replace('flags=17','flags=99')))

    def test_radar_phase_join_requires_every_frame_and_clean_worker_completion(self):
        good='NATIVE_GATE GRAVIDAR_STOP frames=100 waits=103 timeouts=0 calls=20 restored=1 failures=0'
        errors=[];self.assertEqual(module.gravidar_evidence(good,1,100,errors)['frames'],100);self.assertFalse(errors)
        for log in ('',good.replace('frames=100','frames=99'),good.replace('timeouts=0','timeouts=1'),
                    good.replace('restored=1','restored=0'),good.replace('waits=103','waits=201'),good.replace('calls=20','calls=0')):
            errors=[];self.assertIsNone(module.gravidar_evidence(log,1,100,errors));self.assertTrue(errors)

    def test_radar_trace_requires_every_event_in_order_and_clean_restore(self):
        good='NATIVE_GATE RADAR id=3 tick=1273 ordinal=1 source=3 flags=0 script=move.seekenemies\nNATIVE_GATE RADAR_STOP calls=1 restored=1 failures=0'
        errors=[]
        self.assertEqual(module.radar_evidence(good,1,1273,errors),1)
        self.assertEqual(errors,[])
        errors=[]
        self.assertEqual(module.radar_evidence('NATIVE_GATE RADAR_STOP calls=0 restored=1 failures=0',1,1273,errors),0)
        self.assertEqual(errors,[]) # One peer can stop before its first asynchronous event.
        for log in ('',good.replace('calls=1','calls=2'),good.replace('ordinal=1','ordinal=2'),
                    good.replace('source=3','source=13'),good.replace('tick=1273','tick=1274'),
                    good.replace('restored=1','restored=0'),good.replace('failures=0','failures=1'),
                    good.replace('id=3','id=0')):
            errors=[];module.radar_evidence(log,1,1273,errors);self.assertTrue(errors)

    def test_spline_join_requires_exercise_and_no_timeout(self):
        good='NATIVE_GATE SPLINE_JOIN requests=4 waits=2 timeouts=0'
        errors=[]
        self.assertEqual(module.spline_join_evidence(good,1,errors),{'requests':4,'waits':2,'timeouts':0})
        self.assertEqual(errors,[])
        for log in ('',good+'\n'+good,good.replace('requests=4','requests=0'),
                    good.replace('waits=2','waits=5'),good.replace('timeouts=0','timeouts=1')):
            errors=[]
            self.assertIsNone(module.spline_join_evidence(log,1,errors))
            self.assertTrue(errors)

    def fixture(self):
        rows = [str(i)+'/dead/body0:'+('f'*900)+'/body1:none' for i in range(1, 13)]
        lines = ['[X4LAN] NATIVE_GATE STATE_ROW tick=4 id='+str(i)+' data='+row
                 for i, row in enumerate(rows, 1)]
        lines.append('[X4LAN] NATIVE_GATE STATE_END tick=4 entities=12 scope=BATTLE_SUBSET_3')
        return rows, lines

    def test_reassembles_all_canonical_bytes_beyond_single_line_limit(self):
        rows, lines = self.fixture()
        errors = []
        parsed = module.parse_samples('\n'.join(lines), 1, errors)
        expected = 'BATTLE_SUBSET_3|'+'|'.join(rows)
        self.assertGreater(len(expected), 8192)
        self.assertEqual(parsed, {4: expected})
        self.assertEqual(errors, [])

    def test_rejects_missing_duplicate_or_unterminated_rows(self):
        _, lines = self.fixture()
        for bad in (lines[1:], lines+[lines[0]], lines[:-1], lines+[lines[-1]]):
            with self.subTest(lines=len(bad)):
                errors = []
                module.parse_samples('\n'.join(bad), 1, errors)
                self.assertTrue(errors)


    def test_early_hash_mismatch_before_fixture_enrollment_reports_instead_of_crashing(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory)
            (run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':1}))
            states={str(tick):{str(slot):{'game':20+tick/60,'real':20+tick/60,
                'digest':('a' if tick==0 or slot==1 else 'b')*64,'command':'00000000'}
                for slot in (1,2)} for tick in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,
                'verdict':'fail','failure':'battle_state_mismatch_tick_1'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text('')
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n')
            result=module.validate(run,'battle')
        self.assertEqual(result['first_mismatch'],1)
        self.assertFalse(result['session_pass'])
        self.assertFalse(result['evidence_valid'])
        self.assertIn('player 1 has no actual battle samples',result['errors'])
        self.assertIn('three-second hold not proven',result['errors'])


if __name__ == '__main__':
    unittest.main()
