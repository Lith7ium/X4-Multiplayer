"""Provenance is complete observation, never a semantic identity certificate."""
import copy
import unittest
from tools.dev import validate_native_gate as gate


class NPCProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.trigger={'tick':652,'ordinal':58,'depth':1,'cue':'NPC_UseCases.UseCase'}
        self.row=dict(tick='652',trigger='58',flags='0',depth='2',cue='1:NPC_UseCases.UseCase',state='2/2',block='0',
                      time='4028000000000000',time_role='action_block_time',event='580',event_name='object_signalled',
                      parent='2:NPC_UseCases.UseCaseManager',template='3:NPC_UseCases.UseCase',root='1:NPC_UseCases.UseCase',
                      namespace='1:NPC_UseCases.UseCase',ancestry='2:NPC_UseCases.UseCaseManager',
                      values='a:1:0000000000176b3b,a:0:0000000000000000,54:0:0000000000000000,0:0:0000000000000000',
                      owner_kind='2',owner_status='1',owner='0:ambient.script',owner_saved='0000000000176b3b',
                      owner_pc='39',owner_type='2c0de40',owner_action='bc39c0',owner_caller='106e3fb')

    def log(self,row=None,*,unresolved=3,unowned=0):
        row=self.row if row is None else row
        return '\n'.join(('NATIVE_GATE NPC_PROVENANCE '+' '.join(f'{k}={v}' for k,v in row.items()),
                          'NATIVE_GATE NPC_PROVENANCE_STOP calls=1 restored=1 failures=0 overflows=0 scope=windows_local_observation_v1',
                          f'NATIVE_GATE NPC_PROVENANCE_COVERAGE unresolved={unresolved} unowned={unowned}'))

    def validate(self,log=None,triggers=None):
        errors=[];result=gate.npc_provenance_evidence(self.log() if log is None else log,1,5000,errors,
                                                    [self.trigger] if triggers is None else triggers)
        return result,errors

    def test_local_tokens_do_not_claim_same_instance_or_semantic_identity(self):
        result,errors=self.validate();self.assertEqual(errors,[])
        self.assertEqual(result['unresolved'],3);self.assertFalse(result['semantic_identity_proven'])
        changed=copy.deepcopy(self.row)
        for key in ('cue','parent','template','root','namespace','ancestry'):
            token,name=changed[key].split(':',1);changed[key]=f'{int(token)+100}:{name}'
        other,errors=self.validate(self.log(changed));self.assertEqual(errors,[])
        self.assertEqual(result['rows'],other['rows'])
        changed['owner_saved']='0000000000176bfd'
        other,errors=self.validate(self.log(changed));self.assertEqual(errors,[])
        self.assertNotEqual(result['rows'],other['rows'])

    def test_only_proven_event_and_slot_can_decode_object(self):
        for changed in (dict(self.row,event='581',event_name='unresolved'),
                        dict(self.row,values=self.row['values'].replace('a:0:0000000000000000','a:1:0000000000176b3b')),
                        dict(self.row,values=self.row['values'].replace('54:0:0000000000000000','54:0:00007ffdeadbeef0')),
                        dict(self.row,values=self.row['values'].replace('a:1:0000000000176b3b','a:0:0000000000000000'))):
            with self.subTest(changed=changed):self.assertTrue(self.validate(self.log(changed))[1])
        changed=dict(self.row,values=self.row['values'].replace('a:1:0000000000176b3b','a:2:0000000000000000'))
        result,errors=self.validate(self.log(changed,unresolved=4));self.assertEqual(errors,[]);self.assertEqual(result['unresolved'],4)

    def test_missing_tampered_or_partial_capture_fails(self):
        for log in ('',self.log().replace('calls=1','calls=0'),self.log().replace('restored=1','restored=0'),
                    self.log().replace('overflows=0','overflows=1'),self.log().replace('flags=0','flags=16'),
                    self.log().replace('trigger=58','trigger=59'),self.log().replace('depth=2','depth=0'),
                    self.log().replace('unresolved=3','unresolved=0'),self.log()+'\nNATIVE_GATE NPC_PROVENANCE broken',
                    self.log().replace('time_role=action_block_time','time_role=waiting_time')):
            with self.subTest(log=log):self.assertTrue(self.validate(log)[1])
        self.assertTrue(self.validate(triggers=[])[1])
        self.assertTrue(self.validate(triggers=[self.trigger,self.trigger])[1])

    def test_unbracketed_owner_and_cue_refs_remain_limited(self):
        changed=dict(self.row,owner_status='2',owner_caller='0',owner_saved='0000000000000000')
        result,errors=self.validate(self.log(changed,unowned=1));self.assertEqual(errors,[]);self.assertEqual(result['unowned'],1)
        changed=dict(self.row,event='637',event_name='cue_signalled',values='12:4:0000000000000001,0:0:0000000000000000,0:0:0000000000000000,0:0:0000000000000000')
        result,errors=self.validate(self.log(changed));self.assertEqual(errors,[])
        self.assertEqual(result['rows'][0]['values'][0],(0x12,4,'NPC_UseCases.UseCase'))
        self.assertFalse(result['semantic_identity_proven'])
        changed['values']=changed['values'].replace('12:4:0000000000000001','12:4:0000000000000009')
        self.assertTrue(self.validate(self.log(changed))[1])

    def test_ancestry_cycle_and_owner_mislabel_fail(self):
        for changed in (dict(self.row,ancestry='1:NPC_UseCases.UseCase'),
                        dict(self.row,owner_status='1',owner_caller='0'),
                        dict(self.row,owner_kind='1',owner='0:ambient.script'),
                        dict(self.row,parent='0:NPC_UseCases.UseCaseManager'),
                        dict(self.row,root='1:NPC_UseCases.Different'),
                        dict(self.row,ancestry='none')):
            with self.subTest(changed=changed):self.assertTrue(self.validate(self.log(changed))[1])

    def test_owned_origin_requires_explicit_single_native_mapping(self):
        row=dict(self.row,owner_caller='106e3fb',owner_caller_raw='0',owner_caller_source='2')
        def check(row):
            log=self.log(row).replace('windows_local_observation_v1','windows_local_observation_v2')
            errors=[];gate.npc_provenance_evidence(log,1,5000,errors,[self.trigger],True);return errors
        self.assertEqual(check(row),[])
        self.assertEqual(check(dict(row,owner_caller_raw='106e3fb',owner_caller_source='1')),[])
        for changed in (dict(row,owner_caller='106e3fc'),dict(row,owner_caller_raw='106e3fb'),
                        dict(row,owner_caller_source='3'),dict(row,owner_caller_source='1'),
                        dict(row,owner_kind='1',owner='1:NPC_UseCases.UseCase',owner_saved='0'),
                        dict(row,owner_status='2')):
            with self.subTest(changed=changed):self.assertTrue(check(changed))
        # The previous009 zero-caller failure is not retroactively accepted.
        self.assertTrue(self.validate(self.log(dict(self.row,owner_caller='0')))[1])


if __name__=='__main__':unittest.main()
