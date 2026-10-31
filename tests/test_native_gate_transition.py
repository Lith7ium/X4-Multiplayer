from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_transition_evidence import FIELDS,HEX,STATE,STATUS,evidence


def fixture():
    s=dict(saved=99,runtime=123,object=0x10000,controller=0x20000,agent=0x30000,slots=0x30ad0,
           faction=0x40000,model=0x50000,controller_type=0x2c51158,model_type=0x2c4e9c8,owns=1,player=0,owner='argon')
    after=deepcopy(s);after.update(controller=0x60000,player=1)
    rows=[]
    for index,kind,start,end in ((1,1,1,2),(2,2,3,4),(3,3,5,8),(4,4,6,7),(5,5,9,10)):
        e={k:0 for k in FIELDS};e.update(event=index,kind=kind,enter=start,leave=end,thread=7,boot=3,caller=1,s0=deepcopy(s),s1=deepcopy(after));rows.append(e)
    rows[0].update(arg=1,old=0x20000,new=0x60000,result=0x60000)
    rows[1].update(old=0x20000,new=0x60000);rows[1]['s0']['owns']=0;rows[1]['s0']['player']=1;rows[1]['s1']['controller_type']=0x2c39488
    rows[2].update(arg=1,extra=8,active1=1)
    for e in rows[3:]:
        e.update(transfer=1,publication=2,enqueue=1,queue=1 if e['kind']==4 else 2,queue_valid=1,s0=deepcopy(rows[1]['s1']))
    rows[3]['parent']=3;rows[4]['s1']=deepcopy(rows[4]['s0']);rows[4]['s1'].update(model=0,model_type=0)
    return rows


def log(records,**stop_changes):
    def value(k,v):
        if k in ('s0','s1'):return ','.join(f'{v[x]:x}' if i<10 else str(v[x]) for i,x in enumerate(STATE))
        return f'{v:x}' if k in HEX else str(v)
    lines=['NATIVE_GATE PLAYER_TRANSITION '+' '.join(f'{k}={value(k,e[k])}' for k in sorted(FIELDS)) for e in records]
    stop={k:0 for k in STATUS};stop.update(schema=1,capacity=512,count=len(records),complete=len(records),rows=len(records),peak=1,raw_pointers=1);stop.update(stop_changes)
    lines.append('NATIVE_GATE PLAYER_TRANSITION_STOP '+' '.join(f'{k}={v}' for k,v in stop.items()))
    return '\n'.join(lines)


class TransitionEvidenceTests(unittest.TestCase):
    def test_separate_causal_completion_and_authority(self):
        result=evidence(log(fixture()));self.assertTrue(result['evidence_valid'],result['errors'])
        self.assertEqual(result['observed_model_completions'],[5]);self.assertFalse(result['atomic_lease']);self.assertFalse(result['independent_player_context'])

    def test_activation_and_request_do_not_prove_dispatch(self):
        self.assertEqual(evidence(log(fixture()[:4]))['observed_model_completions'],[])
        rows=fixture();rows[4]['s1']['model']=0x50000
        self.assertEqual(evidence(log(rows))['observed_model_completions'],[])

    def test_owner_bind_can_activate_inside_publication_but_needs_final_receipt(self):
        rows=fixture();rows[1].update(leave=8);rows[2].update(enter=4,leave=7,parent=2);rows[3].update(enter=5,leave=6)
        result=evidence(log(rows));self.assertTrue(result['evidence_valid'],result['errors']);self.assertEqual(result['observed_model_completions'],[5])
        rows[1]['s1']['controller']=0x77777
        self.assertEqual(evidence(log(rows))['observed_model_completions'],[])

    def test_each_causal_or_actual_result_tamper_removes_completion(self):
        for index,key,value in ((0,'result',1),(0,'old_owns',1),(0,'arg',0),(1,'new',1),(3,'parent',2),
                                (3,'enqueue',2),(3,'transfer',2),(4,'transfer',2),(4,'publication',1),(4,'enqueue',99)):
            rows=fixture();rows[index][key]=value
            self.assertEqual(evidence(log(rows))['observed_model_completions'],[],(index,key))
        for phase,key,value in (('s0','runtime',124),('s1','owner','xenon'),('s1','agent',0x77777),('s1','controller',0x99999),
                                ('s1','player',0),('s1','owns',0),('s1','controller_type',0x2c51158)):
            rows=fixture();rows[4][phase][key]=value
            self.assertEqual(evidence(log(rows))['observed_model_completions'],[],(phase,key))

    def test_raw_world_failure_is_preserved_beside_interpreted_observation(self):
        rows=fixture();rows[4]['queue_errors']=256
        result=evidence(log(rows));self.assertFalse(result['evidence_valid']);self.assertEqual(result['observed_model_completions'],[5])

    def test_typed_command_or_lifetime_failure_never_counts_as_completion(self):
        for index in (3,4):
            for error in (1,2,4,8,16,32,64,512):
                rows=fixture();rows[index]['queue_errors']=error
                result=evidence(log(rows));self.assertFalse(result['evidence_valid']);self.assertEqual(result['observed_model_completions'],[],(index,error))
            rows=fixture();rows[index]['queue_valid']=0
            self.assertEqual(evidence(log(rows))['observed_model_completions'],[])

    def test_partial_stop_capacity_and_claimed_exclusions_refuse(self):
        for key,value in [('restore_open',1),('open',1),('sites',4),('failures',1),('dropped',1),('count',513),('rows',4),('atomic_lease',1),('generation_exclusion',1)]:
            self.assertFalse(evidence(log(fixture(),**{key:value}))['evidence_valid'],key)
        self.assertFalse(evidence(log(fixture()).replace('event=5','event=513'))['evidence_valid'])
        self.assertFalse(evidence(log(fixture()).replace('kind=5','kind=5 kind=5'))['evidence_valid'])


if __name__=='__main__':unittest.main()
