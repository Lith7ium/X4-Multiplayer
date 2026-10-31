"""Selected-pilot capture must retain missing and unresolved causal coverage."""
import unittest
from tools.dev import validate_native_gate as gate


class NPCPilotTests(unittest.TestCase):
    def setUp(self):
        self.row=dict(tick='229',ordinal='1',end_tick='229',kind='1',nest='0',depth='1',enter='1',exit='2',context='1',
                      saved='00000000000976ab',script='move.flee',after_script='move.flee',pc='0/48',pc_role='execution',flags='1/1',event='555',event_role='stored_context',
                      types='a/58',payloads='opaque',mode='0',range='0/0',rng='123456789abcdef0/23456789abcdef01',
                      returned='1',after_status='1',errors='0',parent_kind='0',parent_status='0',parent_self='0',
                      parent_saved='0000000000000000',parent_name='none',parent_pc='0',parent_caller='0',parent_caller_source='0')

    def log(self,rows=None,*,foreign=0,unmapped=0,unreadable=0,retired=0,after_unresolved=0):
        rows=[self.row] if rows is None else rows
        return '\n'.join(['NATIVE_GATE NPC_PILOT '+' '.join(k+'='+v for k,v in r.items()) for r in rows]+[
            f'NATIVE_GATE NPC_PILOT_STOP calls={len(rows)} restored=1 failures=0 overflows=0 scope=selected_saved_pilot_v1',
            f'NATIVE_GATE NPC_PILOT_COVERAGE foreign={foreign} unmapped={unmapped} unreadable={unreadable} retired={retired} after_unresolved={after_unresolved}'])

    def check(self,log=None,origin=False):
        errors=[];result=gate.npc_pilot_evidence(self.log() if log is None else log,1,5000,errors,origin);return result,errors

    def test_local_tokens_and_partial_kinds_do_not_claim_cause_identity(self):
        result,errors=self.check();self.assertEqual(errors,[])
        self.assertFalse(result['coverage_complete']);self.assertFalse(result['semantic_identity_proven'])
        other,errors=self.check(self.log([dict(self.row,context='99')]))
        self.assertEqual(errors,[]);self.assertEqual(result['rows'],other['rows'])

    def test_nested_events_require_balanced_original_return_intervals(self):
        rows=[dict(self.row,exit='6'),dict(self.row,ordinal='2',kind='3',pc_role='stored_context',nest='1',depth='2',enter='2',exit='5'),
              dict(self.row,ordinal='3',kind='2',nest='2',depth='3',enter='3',exit='4',range='7/9',mode='1')]
        result,errors=self.check(self.log(rows));self.assertEqual(errors,[]);self.assertTrue(result['coverage_complete'])
        for changes in ({'nest':'1'},{'exit':'6'},{'enter':'2'},{'returned':'0'}):
            bad=[*rows[:2],dict(rows[2],**changes)]
            with self.subTest(changes=changes):self.assertTrue(self.check(self.log(bad))[1])

    def test_retired_or_unreadable_after_state_remains_explicit(self):
        for status in (2,3,4,5):
            row=dict(self.row,after_status=str(status),after_script='unresolved',pc='0/0',flags='1/0')
            result,errors=self.check(self.log([row],retired=int(status in (2,3)),after_unresolved=int(status in (4,5))))
            self.assertEqual(errors,[]);self.assertFalse(result['coverage_complete'])
            self.assertTrue(self.check(self.log([row]))[1])
            self.assertTrue(self.check(self.log([dict(row,after_script='move.flee')],retired=int(status in (2,3)),after_unresolved=int(status in (4,5))))[1])

    def test_top_level_execution_ticks_must_not_go_backwards(self):
        rows=[dict(self.row,ordinal=str(i+1),kind=str(kind),enter=str(2*i+1),exit=str(2*i+2),
                   tick=str(227+i),end_tick=str(227+i),pc_role='stored_context' if kind==3 else 'execution')
              for i,kind in enumerate((1,3,2))]
        result,errors=self.check(self.log(rows));self.assertEqual(errors,[]);self.assertTrue(result['coverage_complete'])
        for i,row in enumerate(rows):row.update(tick=str(229-i),end_tick=str(229-i))
        result,errors=self.check(self.log(rows));self.assertTrue(errors);self.assertFalse(result['coverage_complete'])

    def test_versioned_irq_origin_requires_exact_known_native_callsite(self):
        normal=dict(self.row,irq_caller_raw='0',irq_caller='0',irq_caller_source='0')
        irq=dict(normal,ordinal='2',kind='2',enter='3',exit='4',irq_caller_raw='106d8db',irq_caller='106d8db',irq_caller_source='1')
        def log(row=irq):return self.log([normal,row]).replace('selected_saved_pilot_v1','selected_saved_pilot_v2')
        result,errors=self.check(log(),True);self.assertEqual(errors,[]);self.assertTrue(result['irq_origin_complete'])
        self.assertTrue(self.check(log())[1]);self.assertTrue(self.check(self.log(),True)[1])
        for changed in ({'irq_caller_source':'2'},{'irq_caller_source':'3','irq_caller':'0','irq_caller_raw':'0'},
                        {'irq_caller_source':'4','irq_caller':'0'},{'irq_caller':'106d8dc'},
                        {'irq_caller':'106d8dc','irq_caller_raw':'106d8dc'}, {'kind':'1'}):
            result,errors=self.check(log(dict(irq,**changed)),True)
            with self.subTest(changed=changed):self.assertTrue(errors);self.assertFalse(result['irq_origin_complete'])

    def test_missing_tampered_or_foreign_capture_is_not_complete(self):
        for log in ('',self.log([]),self.log().replace('restored=1','restored=0'),self.log().replace('overflows=0','overflows=1'),
                    self.log().replace('errors=0','errors=4'),self.log().replace('saved=00000000000976ab','saved=00000000000976ac',1),
                    self.log().replace('pc_role=execution','pc_role=stored_context'),self.log().replace('event_role=stored_context','event_role=trigger'),
                    self.log().replace('payloads=opaque','payloads=7ff1234'),self.log(foreign=1),self.log()+'\nNATIVE_GATE NPC_PILOT broken'):
            with self.subTest(log=log):self.assertTrue(self.check(log)[1])
        result,errors=self.check(self.log(unmapped=7,unreadable=2));self.assertEqual(errors,[]);self.assertFalse(result['coverage_complete'])

    def test_owned_parent_origin_is_narrow_and_unbracketed_is_explicit(self):
        row=dict(self.row,parent_kind='2',parent_status='1',parent_saved='00000000000976ab',parent_name='order.trade.routine',
                 parent_caller='106e3fb',parent_caller_source='2',parent_pc='5')
        self.assertEqual(self.check(self.log([row]))[1],[])
        for changed in (dict(row,parent_caller='106e3fc'),dict(row,parent_caller_source='3'),dict(row,parent_kind='1')):
            self.assertTrue(self.check(self.log([changed]))[1])
        row.update(parent_status='2',parent_caller='0',parent_caller_source='0')
        result,errors=self.check(self.log([row]));self.assertEqual(errors,[]);self.assertEqual(result['unowned'],1)


if __name__=='__main__':unittest.main()
