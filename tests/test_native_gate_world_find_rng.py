from copy import deepcopy
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_world_find_rng as find
import build_native_gate_overlay as builder
import test_native_gate_probe as probe_tests
import test_native_gate_world_action as action_tests
import test_native_gate_world_values as value_tests
import test_native_gate_world_wait as wait_tests
from validate_native_gate import validate


def fixture():
    doc = find.selection(); e = doc['entries'][0]; seed = int(e['expected_seed_hex'], 16); after = find.rng_step(seed)
    identity = dict(event=1,part=1,context=0x10000,lifetime=77,entity=0x20000,runtime=0x8111,saved=0x9d3a4,
                    script=0x30000,version=10,exec=0x40000,program=0x50000,order=0x60000,order_id=0x40001,state=1,
                    backlink=0x10000,registry=0x70000,group=0x80000,flags=0x10000,generation=1)
    event = dict(event=1,part=0,errors=0,applied=1,restored=1,abnormal=0,thread=7,action=4,candidate=2,ordinal=0,
                 caller_before=50,seed=seed,scoped_before=seed,scoped_after=after,caller_after=50,result=0xfedcba9876543210,assigned=0x29)
    stop = dict.fromkeys(find.KEYS['STOP'], 0)
    stop.update(schema=1,capacity=8,reserved=1,complete=1,rows=4,calls=1,closed=1,used=1,anchor_calls=1,anchor_ready=1,
                seed=seed,digest=e['expected_key_sha256'],saved_pilot=0x9d3a4,saved_order=0x3cdb,saved_script_decimal=38361,scope_exception_cleanup=1)
    rows = [('EVENT',event)] + [('IDENTITY',dict(identity,part=p)) for p in (1,2,3)] + [('STOP',stop)]
    a = dict(event=4,errors=0,id=0x9d3a4,name='order.fight.patrol',version=10,slot=129,pc_before=130,pc_after=130,chance=0,
             boot=2,tick=0,thread=7,program_hash=int(e['guards']['program_shape_hash'],16),type=0x2c0f3c8,action=0xb87a30,
             parent=0,depth=1,children=0,unobserved_children=0)
    local = {k:identity[k] for k in ('context','lifetime','entity','runtime','script','exec','program')}
    local.update(node=0x90000,frame=identity['exec']+0x38,game=0x401c54fdf3b645a2)
    actions = [dict(event=a,before=local,after=deepcopy(local),validation_errors=[])]
    array = {i:dict(saved=int(c['saved'],16),type=int(c['type_rva'],16),valid=1) for i,c in enumerate(e['guards']['candidates'])}
    declaration = bytearray(34);struct.pack_into('<I',declaration,0,0x5b);struct.pack_into('<I',declaration,16,0x54)
    values = {2:dict(event=dict(event=2,errors=0,kind=1,caller=0xb87ae8,action=4,children=1,rng_before=50,rng_after=50,result=event['result'],candidate=0),
                     arrays={0:deepcopy(array),1:deepcopy(array)},value=dict(optional_before=bytes(32),optional_after=bytes(32),declaration=bytes(declaration))),
              3:dict(event=dict(event=3,errors=0,caller=0xd2ad67,rng_before=after,rng_after=after,candidate=2),value=dict(saved=0x29))}
    for index,record in values.items():
        record['validation_errors']=[]
        record['event'].update(thread=7,pc_before=130,pc_after=130,flags_before=0,flags_after=0,a=0x900f0,
                              enter=1 if index==2 else 2,leave=4 if index==2 else 3)
        record['phase']={k+'_'+phase:v for phase in ('before','after') for k,v in
            dict(status=1,eligible=1,app=0x20,vbi=0,view=1,main=0,elapsed=10,tls=0x401c54fdf3b645a2,
                 game=0x401c54fdf3b645a2,saved=0x401c54fdf3b645a2).items()}
    values[2]['event'].update(c=local['frame'],d=0)
    values[3]['event'].update(kind=0,action=4,children=0,b=local['frame'],c=0x90100,d=0xa0000,result=1)
    values[3]['value'].update(kind=3,tag=0xa,runtime=0x9001,raw=struct.pack('<I4xQ',0xa,0x9001),payload=bytes(16))
    waits = [dict(input_output=dict(id=0x9d3a4,name='order.fight.patrol'),raw=[dict(errors=0,slot=121,pc=122,version=10,xml_index=1),local],validation_errors=[])]
    return rows,actions,values,waits


def log(rows):
    return '\n'.join('NATIVE_GATE WORLD_FIND_'+kind+' '+' '.join(k+'='+(f'{v:x}' if k in find.HEX else str(v)) for k,v in row.items()) for kind,row in rows)


class FindRngTests(unittest.TestCase):
    def read(self, data):
        rows,actions,values,waits=data;errors=[]
        result=find.evidence(log(rows),find.selection(),actions,values,waits,errors,'test Find')
        return result,errors

    def test_independent_key_vector_and_lossless_configuration(self):
        d=find.selection();wire=find.key_bytes(d)
        self.assertEqual(len(wire),179)
        self.assertEqual(hashlib.sha256(wire).hexdigest(),'080994d92a20d8658e7e391ea47ffdef7afe3ca8175ecabcb6fe34b7ba4020bc')
        seed=int.from_bytes(hashlib.sha256(wire).digest()[:8],'little')
        self.assertEqual(seed,0x65d8202ad9940908);self.assertEqual(find.rng_step(seed),0x22403ede5ea8c4dd)
        config='X4LAN_CONFIG.native_gate_world_find_rng_manifest = '+json.dumps(find.manifest_wire(d))+'\n'
        self.assertEqual(find.configured(config),d);self.assertIsNone(find.configured(''))
        for invalid in (config+config,config.replace('65d8202ad9940908','65d8202ad9940909'),config.replace('"','')):
            with self.subTest(invalid=invalid),self.assertRaises(ValueError):find.configured(invalid)
        hashes=set()
        for field in ('saved_order','saved_pilot','ordinal','tick'):
            changed=deepcopy(d);value=changed['entries'][0][field]
            changed['entries'][0][field]=f'{int(value,16)+1:016x}' if isinstance(value,str) else value+1
            hashes.add(hashlib.sha256(find.key_bytes(changed)).digest())
        self.assertEqual(len(hashes),4);self.assertNotIn(hashlib.sha256(wire).digest(),hashes)

    def test_exact_manifest_refuses_seed_or_selection_changes(self):
        for mutation in (lambda d:d.update(enabled=True),lambda d:d.update(schema=True),lambda d:d['entries'][0].update(ordinal=1),
                         lambda d:d['entries'][0].update(expected_seed_hex=7338652879700363528),lambda d:d['entries'][0]['wait_anchor'].update(saved_script_id=0x38361),
                         lambda d:d['entries'][0]['guards']['candidates'].reverse(),lambda d:d['entries'][0].update(extra=0)):
            d=find.selection();mutation(d)
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):find.manifest_wire(d)

    def test_verified_local_output_is_not_global_rng_claim(self):
        result,errors=self.read(fixture());self.assertFalse(errors);self.assertTrue(result['selected_scope_valid'])
        second=fixture();second[0][0][1].update(caller_before=99,caller_after=99);second[2][2]['event'].update(rng_before=99,rng_after=99)
        other,err=self.read(second);self.assertFalse(err)
        captures=[dict(slot=1,evidence=result,errors=[]),dict(slot=2,evidence=other,errors=[])]
        paired=find.paired_diagnostics(captures,True,errors)
        self.assertTrue(paired['selected_outputs_equal']);self.assertFalse(paired['global_capture_valid']);self.assertFalse(paired['global_rng_parity']);self.assertFalse(errors)
        duplicates=[dict(slot=1,evidence=result,errors=[])]*2
        rejected=[];self.assertFalse(find.paired_diagnostics(duplicates,True,rejected)['selected_scope_valid']);self.assertTrue(rejected)

    def test_selected_raw_joins_refuse_review_counterexamples(self):
        for mutate in (lambda d:d[2][2]['event'].update(thread=8),lambda d:d[2][2]['event'].update(pc_before=999,pc_after=999),
                       lambda d:d[2][3]['event'].update(action=99),lambda d:d[2][3]['value'].update(kind=1,tag=1),
                       lambda d:d[2][3]['event'].update(thread=8),lambda d:d[2][3]['event'].update(pc_after=131),
                       lambda d:d[2][2]['phase'].update(app_before=0x26),lambda d:d[2][3]['phase'].update(game_after=1),
                       lambda d:d[2][3]['event'].update(enter=5),lambda d:d[2][3]['value'].update(runtime=0),
                       lambda d:d[1][0].update(validation_errors=['selected Action failed']),
                       lambda d:d[3][0].update(validation_errors=['selected Wait failed']),
                       lambda d:d[2][2].update(validation_errors=['selected candidate failed']),
                       lambda d:d[2][3].update(validation_errors=['selected assignment failed'])):
            data=fixture();mutate(data);result,errors=self.read(data);self.assertFalse(result['selected_scope_valid']);self.assertTrue(errors)

    def test_raw_record_validation_is_separate_from_aggregate_coverage(self):
        rows=action_tests.fixture();errors=[]
        records,_=action_tests.evidence(action_tests.log(rows,unknown=1,calls=2),[action_tests.ENTRY],errors)
        self.assertTrue(errors);self.assertEqual(records[0]['validation_errors'],[])
        rows[2]['program']+=8;errors=[]
        records,_=action_tests.evidence(action_tests.log(rows),[action_tests.ENTRY],errors)
        self.assertTrue(records[0]['validation_errors'])
        for field in ('time','expected_time','caller','boot','result'):
            rows=wait_tests.fixture();rows[0][field]+=1;errors=[]
            records,_=wait_tests.evidence(wait_tests.log(rows),[wait_tests.ENTRY],errors)
            self.assertTrue(records[0]['validation_errors'],field)
        rows=wait_tests.fixture();rows[2]['lifetime']+=1;errors=[]
        records,_=wait_tests.evidence(wait_tests.log(rows),[wait_tests.ENTRY],errors)
        self.assertTrue(records[0]['validation_errors'])
        rows=wait_tests.fixture();errors=[]
        records,_=wait_tests.evidence(wait_tests.log(rows,unknown=1),[wait_tests.ENTRY],errors)
        self.assertTrue(errors);self.assertEqual(records[0]['validation_errors'],[])
        for kind,field,value in [('EVENT','thread',8),('PHASE','game_after',1),('VALUE','kind',1)]:
            rows,parents=value_tests.fixture(True)
            next(row for name,row in rows if name==kind and row['event']==2)[field]=value;errors=[]
            records,_=value_tests.evidence(value_tests.log(rows),parents,errors)
            self.assertTrue(records[2]['validation_errors'],field)
        rows,parents=value_tests.fixture(True);rows[-1][1]['failures']=1;errors=[]
        records,_=value_tests.evidence(value_tests.log(rows),parents,errors)
        self.assertTrue(errors);self.assertTrue(all(r['validation_errors']==[] for r in records.values()))

    def test_all_status_failures_refuse_claimed_pass(self):
        fields=('open','restore_open','failures','dropped','anchor_pending','anchor_errors','used','anchor_calls','anchor_ready','closed','rows','reserved','complete','scope_exception_cleanup','enclosing_exception_cleanup')
        for field in fields:
            with self.subTest(field=field):
                data=fixture();data[0][-1][1][field]+=1;result,errors=self.read(data);self.assertTrue(errors)
                pair_errors=[];p=find.paired_diagnostics([dict(slot=1,evidence=result,errors=errors)]*2,True,pair_errors)
                self.assertFalse(p['selected_scope_valid']);self.assertTrue(pair_errors)

    def test_original_once_seed_and_identity_mutants(self):
        for kind,field in [('EVENT',k) for k in ('errors','applied','restored','abnormal','ordinal','seed','scoped_before','scoped_after','caller_after','assigned','action','candidate')]+[('IDENTITY',k) for k in ('context','lifetime','entity','runtime','script','exec','program','order','order_id','state','backlink','flags','generation')]:
            with self.subTest(kind=kind,field=field):
                data=fixture();row=data[0][0 if kind=='EVENT' else 2][1];row[field]+=1
                self.assertTrue(self.read(data)[1])
        for mutate in (lambda d:d[2][2]['arrays'][0][0].update(saved=0x29),lambda d:d[2][2]['arrays'][1][1].update(type=0x2b66081),
                       lambda d:d[2][3]['event'].update(rng_after=1),lambda d:d[2][3]['value'].update(saved=0x28),
                       lambda d:d[1][0]['event'].update(program_hash=1),lambda d:d[3][0]['raw'][0].update(slot=122),
                       lambda d:d[3].clear(),lambda d:d[2][2]['value'].update(declaration=bytes(34))):
            data=fixture();mutate(data);self.assertTrue(self.read(data)[1])

    def test_native_widths_unknown_rows_truncation_and_export_order(self):
        data=fixture()
        for name,key,value in [('EVENT','thread',1<<32),('EVENT','seed',1<<64),('IDENTITY','flags',1<<32),('STOP','reserved',1<<32)]:
            rows=deepcopy(data[0]);next(row for kind,row in rows if kind==name)[key]=value
            self.assertTrue(self.read((rows,*data[1:]))[1])
        for text in (log(data[0])+'\nNATIVE_GATE WORLD_FIND_MISSING x=1',log(data[0][1:]),log(data[0]+[data[0][0]]),
                     log(data[0]).replace('thread=7','thread=7 '+' '*1500),log(data[0]).replace('digest=',' '*1024+'digest='),
                     log(data[0]).replace('errors=0','errors=0 errors=0')):
            errors=[];find.evidence(text,find.selection(),*data[1:],errors,'bad');self.assertTrue(errors)

    def test_builder_requires_explicit_complete_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate.dll';dll.write_bytes(b'test only');manifest=root/'find.json';manifest.write_text(json.dumps(find.selection()))
            argv=['overlay','--gate-dll',str(dll),'--mode','battle','--out',str(root/'out'),'--world-find-rng-manifest',str(manifest)]
            with mock.patch.object(sys,'argv',argv),contextlib.redirect_stderr(io.StringIO()) as err,self.assertRaises(SystemExit):builder.main()
            self.assertIn('requires typed Values',err.getvalue());self.assertFalse((root/'out').exists())

    def test_main_validator_retains_old_failures_and_missing_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);(run/'result.json').write_text(json.dumps(dict(verdict='fail',exit_code=5)))
            states={str(t):{str(s):dict(game=20+t/60,real=20+t/60,digest=('a' if s==1 else 'b')*64,command='00000000') for s in (1,2)} for t in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps(dict(states=states,hold_seconds=0,protocol_version=1,verdict='pass')))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True);(client/'result.json').write_text(json.dumps(dict(lane_restored=True)))
                (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_find_rng_manifest = '+json.dumps(find.manifest_wire(find.selection()))+'\n')
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n')
            result=validate(run,'battle')
        self.assertFalse(result['session_pass']);self.assertFalse(result['evidence_valid']);self.assertIn('three-second hold not proven',result['errors'])
        self.assertFalse(result['native_selected_find_rng']['selected_scope_valid'])

    def test_luajit_config_order_and_append_only_export(self):
        harness=probe_tests.SnapshotProbeTests();harness.setUp();lua=harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_trace_early=true;X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          X4LAN_CONFIG.native_gate_world_action_manifest='99,4,move.idle';X4LAN_CONFIG.native_gate_world_values=true
          X4LAN_CONFIG.native_gate_world_wait_manifest='wait';X4LAN_CONFIG.native_gate_world_wait_rng_manifest='rng'
          X4LAN_CONFIG.native_gate_world_find_rng_manifest='find'
          local gate=package.loadlib('gate')();find_configs=0;find_reads=0;available=0;order={}
          gate.world_trace_config=function() return 1 end
          gate.world_wait_trace_config=function() order[#order+1]='wait';return 1 end
          gate.world_wait_rng_config=function() order[#order+1]='rng';return 1 end
          gate.world_action_trace_config=function() order[#order+1]='action';return 1 end
          gate.world_values_config=function() order[#order+1]='values';return 1 end
          gate.world_find_rng_config=function(text) assert(text=='find' and order[#order]=='values');find_configs=find_configs+1;return 1 end
          gate.world_trace_row=function() return 0 end;gate.world_action_trace_row=gate.world_trace_row
          gate.world_wait_trace_row=gate.world_trace_row;gate.world_wait_rng_row=gate.world_trace_row
          gate.world_values_row=gate.world_trace_row;gate.world_values_unknown_row=gate.world_trace_row
          gate.world_values_cursors=function() return 0,0,0 end
          gate.world_values_unknown_identity_row=gate.world_trace_row
          gate.world_find_rng_row=function(row) if row==0 then return available end assert(row==find_reads+1);find_reads=row;return 'WORLD_FIND_EVENT test='..row end
          gate.world_trace_status=function() return 'test=1' end;gate.world_action_trace_status=gate.world_trace_status
          gate.world_wait_trace_status=gate.world_trace_status;gate.world_wait_rng_status=gate.world_trace_status
          gate.world_values_status=gate.world_trace_status;gate.world_find_rng_status=gate.world_trace_status
          gate.world_values_unknown_identity_status=gate.world_trace_status
          local load=LoadGame;LoadGame=function(slot) assert(find_configs==1);load(slot) end
          setmetatable(state,{__index=function() return 0 end})
        ''')
        lua.execute(probe_tests.SOURCE);lua.execute('update();update();update()');self.assertEqual(lua.globals().find_configs,1)
        lua.execute("state.boot_phase=3;state.issued=1;state.input1='00000000';state.input2='00000000';is_menu=false")
        lua.execute(probe_tests.SOURCE);lua.execute('update()');self.assertEqual(lua.globals().find_configs,2)
        lua.execute('available=1;state.active=0;state.completed=1;update()')
        lines=list(lua.globals().logs.values());self.assertFalse(any('ERROR' in line for line in lines),lines)
        row=next(i for i,line in enumerate(lines) if 'WORLD_FIND_EVENT test=1' in line);stop=next(i for i,line in enumerate(lines) if 'WORLD_FIND_STOP test=1' in line)
        self.assertLess(row,stop);self.assertEqual(lua.globals().find_reads,1)


if __name__=='__main__':unittest.main()
