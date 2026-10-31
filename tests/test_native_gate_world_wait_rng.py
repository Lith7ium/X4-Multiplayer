from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_gate_wait_manifest import windows_saved_time_bits
from native_world_wait import evidence as wait_evidence,configured_waits,compare,rng_step
from native_world_wait_rng import DOMAIN,FIELDS,HEX,seed,fnv,manifest_wire,configured,evidence,paired_diagnostics
from test_native_gate_world_wait import ENTRY,fixture,log,bits
from validate_native_gate import validate

SNAPSHOT='7816351056e6040d92ae9a4619ae2e15a577fa26a19134f11e20746317e61aa8'
SELECTED={k:v for k,v in ENTRY.items() if k!='time'}|{'time_text':'6.91667'}
OBSERVED={'schema':2,'snapshot_sha256':SNAPSHOT,'entries':[SELECTED]}
EXPERIMENT={'schema':1,'snapshot_sha256':SNAPSHOT,'entries':[SELECTED|{'slot':70,'pc':71,'since':0}]}


def config():
    wire=f"{ENTRY['id']},4,5,{windows_saved_time_bits('6.91667')},{bits(.1):016x},{bits(.5):016x},move.idle"
    return ('X4LAN_CONFIG.native_gate_world_wait_manifest = '+json.dumps(wire)+'\n'+
            'X4LAN_CONFIG.native_gate_world_wait_rng_manifest = '+json.dumps(manifest_wire(EXPERIMENT,OBSERVED))+'\n'+
            'X4LAN_CONFIG.native_gate_snapshot_sha = '+json.dumps(SNAPSHOT)+'\n')


def sample():
    rows=fixture();entry=EXPERIMENT['entries'][0];key=seed(entry,SNAPSHOT)
    for row in (rows[0],rows[3]):row.update(time=int(windows_saved_time_bits('6.91667'),16),slot=70,pc=71)
    rows[0].update(expected_time=rows[0]['time'],rng_after=rows[0]['rng_before'])
    for row in rows[1:3]:row.update(app_flags=0x26,vbi_loading=1)
    receipt=dict(event=1,selected=1,configured=1,applied=1,refusal=0,restored=1,domain=DOMAIN,key_schema=1,
                 script_hash=fnv(entry['name']),time_hash=fnv(entry['time_text']),seed=key,
                 caller_before=123,scoped_before=key,scoped_after=rng_step(key),caller_restored=123,
                 expected_slot=70,expected_pc=71,expected_since=0)
    stop=dict(schema=1,selected=1,rows=1,applied=1,refused=0,open=0,restore_open=0,closed=1,global_sync=0,exception_cleanup=0)
    return rows,receipt,stop


def combined(rows,receipt,stop):
    encoded=' '.join(f'{k}={v:016x}' if k in HEX else f'{k}={v}' for k,v in receipt.items())
    return log(rows)+'\nNATIVE_GATE WORLD_WAIT_RNG '+encoded+'\nNATIVE_GATE WORLD_WAIT_RNG_STOP '+' '.join(f'{k}={v}' for k,v in stop.items())


def read(text):
    errors=[];events,_=wait_evidence(text,[SELECTED],errors,rng_configured=True)
    result=evidence(text,EXPERIMENT,events,errors)
    return result,events,errors


class SavedWaitRngTests(unittest.TestCase):
    def test_independent_key_goldens_and_each_sequential_field(self):
        entry=EXPERIMENT['entries'][0]
        self.assertEqual(seed(entry,SNAPSHOT),0x41aa3ad655ad9a9c)
        self.assertEqual(seed(entry|{'id':'99'},SNAPSHOT),0x6afa3d95d687908f)
        patrol=dict(id='9d3a4',name='order.fight.patrol',version=10,index=1,time_text='6.31667',minimum=.001,maximum=1)
        self.assertEqual(seed(patrol,SNAPSHOT),0x8b0f91e8910d9f7c)
        key=seed(entry,SNAPSHOT)
        for field,value in (('id','80d55'),('name','move.idlx'),('version',5),('index',4),('time_text','6.916670'),('minimum',.2),('maximum',.6)):
            with self.subTest(field=field):self.assertNotEqual(seed(entry|{field:value},SNAPSHOT),key)
        self.assertNotEqual(seed(entry|{'version':5,'index':4},SNAPSHOT),key)
        self.assertEqual(seed(entry|{'slot':99,'pc':100,'since':1,'runtime':999,'thread':17},SNAPSHOT),key)
        for i in range(4):
            words=[SNAPSHOT[j:j+16] for j in range(0,64,16)];words[i]=f'{int(words[i],16)^1:016x}'
            self.assertNotEqual(seed(entry,''.join(words)),key)

    def test_exact_manifest_roundtrip_and_refusals(self):
        self.assertEqual(configured(config(),configured_waits(config())),EXPERIMENT)
        for kind in ('schema','snapshot','time','index','slot','pc','since','extra','missing','order'):
            changed=deepcopy(EXPERIMENT)
            if kind=='schema':changed['schema']=2
            elif kind=='snapshot':changed['snapshot_sha256']='0'*64
            elif kind=='time':changed['entries'][0]['time_text']='6.916670'
            elif kind=='index':changed['entries'][0]['index']=4
            elif kind in ('slot','pc','since'):changed['entries'][0][kind]=1<<30
            elif kind=='extra':changed['entries'][0]['extra']=1
            elif kind=='missing':del changed['entries'][0]['since']
            elif kind=='order':changed['entries']*=2
            with self.subTest(kind=kind),self.assertRaises(ValueError):manifest_wire(changed,OBSERVED)
        with self.assertRaises(ValueError):manifest_wire(EXPERIMENT,OBSERVED|{'schema':1})
        for text in (config()+config(),config().replace(SNAPSHOT,'0'*64,1),config().replace('1,70,71,0','1,4096,71,0')):
            with self.assertRaises(ValueError):configured(text,configured_waits(config()))

    def test_success_is_separate_intervention_not_natural_global_stream(self):
        result,events,errors=read(combined(*sample()));self.assertEqual(errors,[])
        self.assertFalse(result['global_rng_synchronized']);self.assertFalse(result['exception_cleanup'])
        self.assertIsNone(events[0]['input_output']['single_rng_step_matches'])
        self.assertEqual(events[0]['input_output']['rng_interpretation'],'scoped_experiment_separate_evidence')
        errors=[];wait_evidence(combined(*sample()),[SELECTED],errors)
        self.assertIn('wait: unknown wait record',errors)

    def test_every_receipt_field_and_stop_failure_rejected(self):
        for field in FIELDS:
            rows,receipt,stop=sample();receipt[field]^=1
            with self.subTest(field=field):self.assertTrue(read(combined(rows,receipt,stop))[2])
        for field in sample()[2]:
            rows,receipt,stop=sample();stop[field]^=1
            with self.subTest(stop=field):self.assertTrue(read(combined(rows,receipt,stop))[2])

    def test_raw_phase_lifetime_expression_and_result_stay_strict(self):
        for part,field,value in ((0,'errors',256),(0,'result',0),(1,'main',1),(1,'view',0),(1,'app_flags',0x24),
                                (2,'vbi_loading',0),(2,'game',bits(8)),(2,'lifetime',999),(2,'expression',bytes(104)),
                                (2,'eligible',0),(2,'elapsed',120001),(0,'rng_after',999)):
            rows,receipt,stop=sample();rows[part][field]=value
            with self.subTest(part=part,field=field):self.assertTrue(read(combined(rows,receipt,stop))[2])

    def test_missing_duplicate_extra_malformed_and_truncated_fail(self):
        text=combined(*sample())
        for altered in (text.replace('applied=1','applied=+1'),text.replace('configured=1','configured=18446744073709551616'),
                        text.replace(' key_schema=1',''),text+'\n'+text.splitlines()[-2],
                        '\n'.join(text.splitlines()[:-1]),text.replace(' domain=',' domain=z'),text.replace('WORLD_WAIT_RNG ','WORLD_WAIT_RNG_UNKNOWN ')):
            with self.subTest(text=altered[-150:]):self.assertTrue(read(altered)[2])

    def test_pair_keeps_raw_caller_differences_and_old_failure(self):
        left,left_raw,errors=read(combined(*sample()));self.assertFalse(errors)
        rows,receipt,stop=sample();rows[0]['rng_before']=rows[0]['rng_after']=456;receipt['caller_before']=receipt['caller_restored']=456
        right,right_raw,errors=read(combined(rows,receipt,stop));self.assertFalse(errors)
        self.assertTrue(compare(left_raw,right_raw))
        captures=[dict(slot=i,manifest=EXPERIMENT,evidence=e,errors=[]) for i,e in ((1,left),(2,right))]
        errors=['whole world mismatch'];result=paired_diagnostics(captures,False,errors)
        self.assertTrue(result['capture_valid']);self.assertTrue(result['selected_outputs_equal']);self.assertEqual(errors,['whole world mismatch'])
        captures[1]['evidence']['samples'][0]['due_after']+=1
        result=paired_diagnostics(captures,True,errors);self.assertFalse(result['selected_outputs_equal']);self.assertEqual(len(errors),2)
        captures[1]['errors']=['STOP overlap'];self.assertIsNone(paired_diagnostics(captures,False,errors)['selected_outputs_equal'])

    def test_main_validator_preserves_prior_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);(run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':1}))
            states={str(t):{str(s):{'game':20+t/60,'real':20+t/60,'digest':('b' if t and s==2 else 'a')*64,'command':'00000000'} for s in (1,2)} for t in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,'verdict':'fail','failure':'battle_state_mismatch_tick_1'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True)
                (client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text(config())
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+combined(*sample()))
            result=validate(run,'battle')
        self.assertFalse(result['session_pass']);self.assertEqual(result['first_mismatch'],1)
        self.assertTrue(result['native_wait_rng_experiment']['selected_outputs_equal'])
        self.assertIn('three-second hold not proven',result['errors'])
        self.assertIn('player 1 has no actual battle samples',result['errors'])


if __name__=='__main__':unittest.main()
