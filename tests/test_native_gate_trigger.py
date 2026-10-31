"""Read-only cause/path evidence must not promote ambient or ambiguous identity."""
from pathlib import Path
import importlib.util
import unittest
spec=importlib.util.spec_from_file_location('gate',Path(__file__).resolve().parents[1]/'tools/dev/validate_native_gate.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)

class TriggerEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.script=gate.npc_trigger_text('fight.attack.object.fighter')
        self.cause=gate.npc_trigger_cause_key(444,5,self.script,23,0,3,0,0)
        self.irq='NATIVE_GATE INTERRUPT tick=444 ordinal=172 actor=5 kind=1 event=0 script=fight.attack.object.fighter pc=23 range=0/3 mode=0 values=0:0000000000000000,0:0000000000000000'
        self.step='NATIVE_GATE INTERRUPT_STEP tick=444 ordinal=908 parent=172 actor=5 pc=2 type=2c0de40 action=bc39c0'
        self.cause_row=f'NATIVE_GATE NPC_CAUSE tick=444 irq=172 actor=5 key={self.cause:016x} occurrence=1 script={self.script:016x} pc=23 range=0/3 event=0 mode=0 status=1'
        self.row=dict(tick=444,ordinal=1,parent=0,depth=1,status=1,legacy=0,key=0,cause=self.cause,irq=172,step=908,actor=5,parent_key=0,visit=1,child=1,
                      action_program=0,action_pc=2,type=0x2c0de40,action=0xbc39c0,program=0x1234,pc=0,caller=0x111c821,cue='NPC_UseCases.UseCase',before=10,after=21)
        self.row['cue_hash']=gate.npc_trigger_text(self.row['cue']);self.row['key']=gate.npc_trigger_path_key(self.row)
    def format(self,row):
        hex_fields={'key','cause','parent_key','action_program','type','action','program','caller'}
        parts=[f'{k}={v:016x}' if k in hex_fields else f'{k}={v}' for k,v in row.items() if k not in ('before','after','cue_hash')]
        parts.append(f"rng={row['before']:016x}/{row['after']:016x}")
        return 'NATIVE_GATE NPC_TRIGGER '+' '.join(parts)
    def log(self,rows=None,causes=None,irqs=None,unsupported=0):
        rows=[self.row] if rows is None else rows;causes=[self.cause_row] if causes is None else causes;irqs=[self.irq] if irqs is None else irqs
        return '\n'.join(irqs+[self.step]+causes+[self.format(r) for r in rows]+[
            f'NATIVE_GATE NPC_TRIGGER_STOP calls={len(rows)} causes={len(causes)} restored=1 failures=0 identity=fixture_trigger_v1',
            f'NATIVE_GATE NPC_TRIGGER_COVERAGE seen={len(rows)} foreign_thread=0 unsupported={unsupported}'])
    def parse(self,log):
        errors=[];result=gate.npc_trigger_evidence(log,1,500,errors);return result,errors
    def test_ambient_permutation_never_changes_fixture_key(self):
        result,errors=self.parse(self.log());self.assertFalse(errors);self.assertTrue(result['candidate_coverage_complete'])
        ambient=dict(self.row,status=2,key=0,cause=0,irq=0,step=0,actor=0,visit=0,child=0)
        fixture=dict(self.row,ordinal=2)
        result,errors=self.parse(self.log([ambient,fixture],unsupported=1));self.assertFalse(errors)
        self.assertFalse(result['candidate_coverage_complete']);self.assertEqual(result['rows'][1]['key'],self.row['key'])
        self.assertEqual([r['key'] for r in result['rows'] if r['usable']],[self.row['key']])
    def test_later_identical_cause_invalidates_first_candidate_uniqueness(self):
        second=self.cause_row.replace('irq=172','irq=173').replace('occurrence=1','occurrence=2').replace('status=1','status=6')
        result,errors=self.parse(self.log(causes=[self.cause_row,second],irqs=[self.irq,self.irq.replace('ordinal=172','ordinal=173')]))
        self.assertFalse(errors);self.assertFalse(result['candidate_coverage_complete']);self.assertEqual(result['ambiguous_candidates'],1)
        self.assertFalse(result['rows'][0]['usable'])
    def test_nonzero_parameter_is_explicitly_unsupported(self):
        cause=self.cause_row.replace(f'key={self.cause:016x}','key=0000000000000000').replace('occurrence=1','occurrence=0').replace('status=1','status=3')
        irq=self.irq.replace('values=0:0000000000000000','values=a:000000000005ca00')
        row=dict(self.row,status=3,key=0,cause=0,visit=0,child=0)
        result,errors=self.parse(self.log([row],[cause],[irq],unsupported=1));self.assertFalse(errors);self.assertFalse(result['candidate_coverage_complete'])
    def test_hierarchical_parent_call_path_and_tampering(self):
        child=dict(self.row,ordinal=2,parent=1,depth=2,parent_key=self.row['key'],action_program=self.row['program'],action_pc=68,action=0x1123610)
        child['key']=gate.npc_trigger_path_key(child)
        result,errors=self.parse(self.log([self.row,child]));self.assertFalse(errors);self.assertNotEqual(result['rows'][0]['key'],result['rows'][1]['key'])
        for log in (self.log().replace('visit=1','visit=2'),self.log().replace('restored=1','restored=0'),
                    self.log().replace('foreign_thread=0','foreign_thread=1'),self.log().replace('legacy=0','legacy=1'),
                    self.log().replace('child=1','child=2'),self.log().replace('status=1 legacy=0','status=2 legacy=0'),
                    self.log([self.row,dict(child,parent=8)]),self.log()+'\nNATIVE_GATE NPC_TRIGGER broken'):
            _,errors=self.parse(log);self.assertTrue(errors,log)

if __name__=='__main__':unittest.main()
