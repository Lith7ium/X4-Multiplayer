from copy import deepcopy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_gate_wait_manifest import selected_time_bits,double_bits
from native_world_wait import evidence as wait_evidence,configured_waits,rng_step
from native_world_wait_rng import DOMAIN,HEX,manifest_wire,configured,evidence,paired_diagnostics,policies,seed,fnv
from test_native_gate_world_wait import fixture,log,bits
from test_native_gate_world_wait_rng import SNAPSHOT,SELECTED
from validate_native_gate import validate

PATROL=dict(id='000000000009d3a4',name='order.fight.patrol',version=10,index=1,time_text='6.31667',minimum=.001,maximum=1)
THIRD=dict(id='000000000006d945',name='move.idle',version=4,index=5,time_text='6.98333',minimum=.1,maximum=.5)
OBSERVED=dict(schema=2,snapshot_sha256=SNAPSHOT,entries=[SELECTED,PATROL,THIRD])
EXPERIMENT=dict(schema=2,snapshot_sha256=SNAPSHOT,entries=[
    SELECTED|dict(policy='intervene',slot=70,pc=71,since=0),
    PATROL|dict(policy='intervene',slot=121,pc=122,since=0),THIRD|dict(policy='observe')])


def config():
    wire=';'.join(','.join([e['id'],str(e['version']),str(e['index']),selected_time_bits(e),double_bits(e['minimum']),double_bits(e['maximum']),e['name']]) for e in OBSERVED['entries'])
    return ('X4LAN_CONFIG.native_gate_world_wait_manifest = '+json.dumps(wire)+'\n'+
            'X4LAN_CONFIG.native_gate_world_wait_rng_manifest = '+json.dumps(manifest_wire(EXPERIMENT,OBSERVED))+'\n'+
            'X4LAN_CONFIG.native_gate_snapshot_sha = '+json.dumps(SNAPSHOT)+'\n')


def sample(natural_input=2026):
    rows=[];receipts=[]
    for index,entry in enumerate(EXPERIMENT['entries'],1):
        parts=fixture();natural=entry['policy']=='observe';before=natural_input if natural else index*123
        slot=entry.get('slot',70);pc=entry.get('pc',71);time=int(selected_time_bits(entry),16)
        for part in parts:part['event']=index
        parts[0].update(selected=index,enter=2*index-1,leave=2*index,id=int(entry['id'],16),name=entry['name'],version=entry['version'],
            xml_index=entry['index'],time=time,expected_time=time,slot=slot,pc=pc,minimum=bits(entry['minimum']),maximum=bits(entry['maximum']),
            rng_before=before,rng_after=rng_step(before) if natural else before)
        parts[3].update({k:parts[0][k] for k in ('id','name','version','since','slot','pc','time')})
        for part in parts[1:3]:
            expression=bytearray(part['expression']);struct.pack_into('<d',expression,0x20,entry['minimum']);struct.pack_into('<d',expression,0x30,entry['maximum'])
            part.update(expression=bytes(expression),app_flags=0x26,vbi_loading=1)
        key=0 if natural else seed(entry,SNAPSHOT)
        receipts.append(dict(event=index,selected=index,configured=1,applied=0 if natural else 1,refusal=0,restored=0 if natural else 1,
            domain=0 if natural else DOMAIN,key_schema=0 if natural else 1,script_hash=fnv(entry['name']),time_hash=fnv(entry['time_text']),
            seed=key,caller_before=before,scoped_before=key,scoped_after=0 if natural else rng_step(key),caller_restored=0 if natural else before,
            expected_slot=0 if natural else slot,expected_pc=0 if natural else pc,expected_since=0,policy=2 if natural else 1,
            natural_after=rng_step(before) if natural else 0))
        rows.extend(parts)
    stop=dict(schema=2,selected=3,rows=3,applied=2,refused=0,open=0,restore_open=0,closed=1,global_sync=0,exception_cleanup=0,observed=1)
    return rows,receipts,stop


def combined(rows,receipts,stop):
    encoded=log(rows,selected=3,reserved=3,complete=3,rows=12)
    for receipt in receipts:
        encoded+='\nNATIVE_GATE WORLD_WAIT_RNG '+' '.join(f'{k}={v:016x}' if k in HEX else f'{k}={v}' for k,v in receipt.items())
    return encoded+'\nNATIVE_GATE WORLD_WAIT_RNG_STOP '+' '.join(f'{k}={v}' for k,v in stop.items())


def read(text):
    errors=[];events,coverage=wait_evidence(text,OBSERVED['entries'],errors,rng_configured=True,rng_policies=policies(EXPERIMENT))
    result=evidence(text,EXPERIMENT,events,errors)
    return events,result,errors


class WaitPolicyTests(unittest.TestCase):
    def test_wire_exact_and_keys_unchanged(self):
        self.assertEqual(manifest_wire(EXPERIMENT,OBSERVED),
            'WAIT_POLICY_2;'+SNAPSHOT+';1,intervene,70,71,0,6.91667;2,intervene,121,122,0,6.31667;3,observe,6.98333')
        self.assertEqual(configured(config(),configured_waits(config())),EXPERIMENT)
        self.assertEqual([seed(e,SNAPSHOT) for e in EXPERIMENT['entries'][:2]],[0x41aa3ad655ad9a9c,0x8b0f91e8910d9f7c])
        with self.assertRaises(ValueError):seed(EXPERIMENT['entries'][2],SNAPSHOT)
        reordered=deepcopy(EXPERIMENT);reordered['entries'].reverse();observation=deepcopy(OBSERVED);observation['entries'].reverse()
        self.assertTrue(manifest_wire(reordered,observation));self.assertEqual(seed(reordered['entries'][1],SNAPSHOT),0x8b0f91e8910d9f7c)

    def test_exact_binding_unknown_policy_and_guessed_guard_refuse(self):
        for field,value in (('policy','seed'),('policy',None),('slot',70),('pc',71),('since',0),('time_text','6.983330'),('version',5)):
            changed=deepcopy(EXPERIMENT);changed['entries'][2][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):manifest_wire(changed,OBSERVED)
        for entries in (EXPERIMENT['entries'][:2],EXPERIMENT['entries']+[EXPERIMENT['entries'][2]],list(reversed(EXPERIMENT['entries']))):
            with self.assertRaises(ValueError):manifest_wire(EXPERIMENT|{'entries':entries},OBSERVED)
        for replacement in ('3,observe,70,71,0,6.98333','3,other,6.98333','2,observe,6.98333','3,observe,6.98334'):
            with self.assertRaises(ValueError):configured(config().replace('3,observe,6.98333',replacement),configured_waits(config()))
        with self.assertRaises(ValueError):configured(config().replace(manifest_wire(EXPERIMENT,OBSERVED),'WAIT_POLICY_2'),configured_waits(config()))

    def test_mixed_capture_preserves_natural_raw_interpretation(self):
        events,result,errors=read(combined(*sample()));self.assertEqual(errors,[])
        self.assertEqual([e['input_output']['single_rng_step_matches'] for e in events],[None,None,True])
        self.assertEqual(events[2]['input_output']['rng_interpretation'],'natural')
        self.assertEqual(result['stop']['applied'],2);self.assertEqual(result['stop']['observed'],1)
        self.assertNotIn('seed',result['samples'][2]);self.assertFalse(result['global_rng_synchronized'])
        # Slot/PC are captured as facts; an observe entry never asserts inferred values.
        rows,receipts,stop=sample();rows[8]['slot']=rows[11]['slot']=99;rows[8]['pc']=rows[11]['pc']=100
        self.assertFalse(read(combined(rows,receipts,stop))[2])

    def test_every_natural_receipt_and_new_stop_field_is_checked(self):
        for field in sample()[1][2]:
            rows,receipts,stop=sample();receipts[2][field]^=1
            with self.subTest(field=field):self.assertTrue(read(combined(rows,receipts,stop))[2])
        for field in ('schema','selected','rows','applied','observed','refused','open','restore_open','closed'):
            rows,receipts,stop=sample();stop[field]^=1
            with self.subTest(stop=field):self.assertTrue(read(combined(rows,receipts,stop))[2])
        rows,receipts,stop=sample();rows[8]['rng_after']=receipts[2]['natural_after']=receipts[2]['caller_before']
        self.assertTrue(read(combined(rows,receipts,stop))[2])
        for removed in ('natural_after','policy'):
            rows,receipts,stop=sample();del receipts[2][removed]
            self.assertTrue(read(combined(rows,receipts,stop))[2])
        rows,receipts,stop=sample();receipts.pop();self.assertTrue(read(combined(rows,receipts,stop))[2])

    def test_observation_difference_does_not_hide_intervention_equality_or_strict_failure(self):
        captures=[]
        for slot,incoming in ((1,2026),(2,2027)):
            events,result,errors=read(combined(*sample(incoming)));self.assertFalse(errors)
            captures.append(dict(slot=slot,manifest=EXPERIMENT,evidence=result,errors=errors))
        errors=['whole world mismatch'];result=paired_diagnostics(captures,False,errors)
        self.assertTrue(result['capture_valid']);self.assertTrue(result['intervene_outputs_equal']);self.assertFalse(result['observe_outputs_equal'])
        self.assertFalse(result['selected_outputs_equal']);self.assertEqual(errors,['whole world mismatch'])
        paired_diagnostics(captures,True,errors);self.assertEqual(len(errors),2)
        captures[1]['errors']=['missing natural receipt'];self.assertIsNone(paired_diagnostics(captures,False,errors)['observe_outputs_equal'])

    def test_main_validator_selects_interpretation_before_raw_parse(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);(run/'result.json').write_text(json.dumps(dict(verdict='fail',exit_code=1)))
            states={str(t):{str(s):dict(game=20+t/60,real=20+t/60,digest=('b' if t and s==2 else 'a')*64,command='00000000') for s in (1,2)} for t in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps(dict(states=states,hold_seconds=0,verdict='fail',failure='battle_state_mismatch_tick_1')))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps(dict(lane_restored=True)))
                (client/'installed/x4_lan_lab-config.lua').write_text(config())
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+combined(*sample(2025+slot)))
            result=validate(run,'battle')
        self.assertFalse(result['session_pass']);self.assertEqual(result['first_mismatch'],1)
        self.assertTrue(result['native_wait_rng_experiment']['intervene_outputs_equal']);self.assertFalse(result['native_wait_rng_experiment']['observe_outputs_equal'])
        for client in result['native_wait_restore']['clients']:
            self.assertTrue(client['samples'][2]['single_rng_step_matches'])
            self.assertEqual(client['samples'][2]['rng_interpretation'],'natural')
        self.assertIn('three-second hold not proven',result['errors'])

    def test_observe_source_exits_before_intervention_effects(self):
        source=(Path(__file__).resolve().parents[1]/'src/native/engine/lockstep_world_wait_rng.h').read_text()
        begin=source[source.index('static void world_wait_rng_begin('):source.index('static void world_wait_rng_end(')]
        block=begin[begin.index('if(r && r->policy==WWR_OBSERVE)'):begin.index('if(!r || r->policy!=WWR_INTERVENE')]
        self.assertIn('return;',block)
        for operation in ('TlsSetValue','*address=','used=','world_wait_rng_applied','world_wait_rng_refused','current_tls()'):
            self.assertNotIn(operation,block)
        self.assertLess(begin.index('world_wait_rng_refuse(parent,WWR_NESTED)'),begin.index('if(r && r->policy==WWR_OBSERVE)'))


if __name__=='__main__':unittest.main()
