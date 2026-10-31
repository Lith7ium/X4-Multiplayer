from copy import deepcopy
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import unittest
import tempfile
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
from native_world_values import BYTE_FIELDS,HEX,KEYS,evidence,paired_diagnostics,configured
import test_native_gate_world_action as action_tests
import test_native_gate_probe as probe_tests
from test_native_gate_world_action import fixture as action_fixture,log as action_log,ENTRY
import build_native_gate_overlay as builder
from validate_native_gate import validate

def bits(n):return int.from_bytes(struct.pack('<d',n),'little')

def fixture(candidate=False):
    parent=dict(event=dict(event=1,errors=0,type=0x2c0f3c8 if candidate else 0x2c0cc88,
                          action=0xb87a30 if candidate else 0xbe2320,stage=0 if candidate else 1,
                          thread=7,id=0x99,name='move.idle',version=4,slot=0),
                before=dict(node=0x10000,frame=0x20038,game=bits(7.083)),
                after=dict(node=0x10000,frame=0x20038,game=bits(7.083)))
    def rows(index,kind):
        e=dict.fromkeys(KEYS['EVENT'],0);e.update(event=index,kind=kind,thread=7,action=1,enter=1,leave=2,
            caller=0xb87ae8 if kind else 0xbe23af,result=0xaabbccdd11223344 if kind else 0xfedcba9876543201,
            rng_before=50,rng_after=57,pc_before=1,pc_after=1,a=0x100f0 if candidate else 0x101b8,
            b=0x60000 if kind else 0x20038,c=0x20038 if kind else (0x10100 if candidate else 0x101c8),d=0 if kind else 0x30000)
        v={k:(bytes(BYTE_FIELDS[k]) if k in BYTE_FIELDS else 0) for k in KEYS['VALUE']};v.update(event=index)
        if not kind:v.update(tag=0x63,kind=2,raw=struct.pack('<I4xQ',0x63,0x40000),payload=struct.pack('<4f',12.5,-4,1,0))
        p=dict.fromkeys(KEYS['PHASE'],0);p['event']=index
        for phase in ('before','after'):
            p.update({k+'_'+phase:n for k,n in dict(status=1,eligible=1,app=0x20,view=1,elapsed=100,
                tls=bits(7.083),game=bits(7.083),real=bits(4),saved=bits(7.083)).items()})
        return [('EVENT',e),('VALUE',v),('PHASE',p)]
    records=rows(1,1 if candidate else 0)
    if candidate:
        records[0][1].update(leave=4,children=1,rng_after=68)
        records[1][1].update(begin_before=0x70000,end_before=0x70010,capacity_before=0x70010,count_before=2,
                              begin_after=0x70000,end_after=0x70010,capacity_after=0x70010,count_after=2)
        for phase in (0,1):
            for index in (0,1):records.append(('ARRAY',dict(event=1,phase=phase,index=index,object=0x50000+index*0x100,
                runtime=0x11+index,saved=0x101+index,type=0x2b80000,valid=1)))
        child=rows(2,0);child[0][1].update(candidate=1,enter=2,leave=3,caller=0xd2ad67,rng_before=61,rng_after=68)
        child[1][1].update(tag=0xa,kind=3,raw=struct.pack('<I4xQ',0xa,0x11),payload=bytes(16),runtime=0x11,saved=0x101)
        records.extend(child)
    stop=dict.fromkeys(KEYS['STOP'],0);stop.update(schema=1,capacity=256,candidate_capacity=128,
        reserved=2 if candidate else 1,complete=2 if candidate else 1,rows=len(records),calls=2 if candidate else 1,
        peak=2 if candidate else 1,closed=1)
    records.append(('STOP',stop));return records,[parent]

def log(records):
    def value(key,v):return v.hex() if isinstance(v,bytes) else f'{v:x}' if key in HEX else str(v)
    return '\n'.join('NATIVE_GATE WORLD_VALUES_'+name+' '+' '.join(k+'='+value(k,v) for k,v in row.items()) for name,row in records)

class WorldValuesTests(unittest.TestCase):
    def read(self,records,parents):
        errors=[];events,coverage=evidence(log(records),parents,errors);return events,coverage,errors

    def test_typed_outputs_and_candidate_join(self):
        for candidate in (False,True):
            rows,parents=fixture(candidate);events,coverage,errors=self.read(rows,parents)
            self.assertEqual(errors,[]);self.assertEqual(len(events),2 if candidate else 1)
            self.assertFalse(coverage['exception_cleanup'])

    def test_iterable_input_preserves_separate_identity_stream(self):
        rows,parents=fixture()
        lines=log(rows).splitlines()+['NATIVE_GATE WORLD_UNKNOWN_IDENTITY_STOP schema=1 samples=0 complete=0 rows=0 exported=0 open=0 closed=1 read_limit=512 byte_limit=16384 hop_limit=8']
        errors=[];_,coverage=evidence(iter(lines),parents,errors)
        self.assertEqual(errors,[]);self.assertTrue(coverage['unknown_identity']['available'])
        self.assertTrue(coverage['unknown_identity']['coverage_complete'])

    def test_argument_identity_type_phase_and_lifetime_failures(self):
        changes=[(0,'caller',0),(0,'thread',1<<32),(0,'flags_before',1<<16),(0,'a',0x12345),
                 (0,'errors',1),(0,'action',2),(0,'result',2),(0,'d',0),(1,'tag',4),(1,'kind',3),
                 (1,'payload',struct.pack('<4f',float('nan'),0,0,0)),(2,'game_after',bits(9)),
                 (2,'app_after',0x26),(2,'eligible_before',0)]
        for index,key,value in changes:
            with self.subTest(key=key,value=value):
                rows,parents=fixture();rows[index][1][key]=value;self.assertTrue(self.read(rows,parents)[2])

    def test_candidate_mutations_and_missing_assignment(self):
        changes=[(0,'children',0),(0,'caller',0xbe23af),(0,'b',0),(0,'d',123),(1,'count_before',129),
                 (1,'end_after',0x70018),(3,'runtime',0),(3,'saved',0),(3,'object',1),
                 (3,'valid',256),(3,'type',0x7000000),(7,'candidate',0),(8,'saved',999)]
        for index,key,value in changes:
            with self.subTest(index=index,key=key):
                rows,parents=fixture(True);rows[index][1][key]=value;self.assertTrue(self.read(rows,parents)[2])

    def test_missing_duplicate_order_caps_and_stop(self):
        rows,parents=fixture(True)
        variants=[rows[:-1],rows[:2]+rows[3:],rows+[rows[0]],[rows[1],rows[0]]+rows[2:],rows+[rows[-1]]]
        for variant in variants:self.assertTrue(self.read(variant,parents)[2])
        errors=[];evidence(log(rows).replace('WORLD_VALUES_EVENT ','WORLD_VALUES_EVENT '+' '*2100,1),parents,errors);self.assertTrue(errors)
        for field in ('failures','dropped','open','restore_open','sites','patch_failures','patch_errors','protection_pending','flush_pending','exception_cleanup'):
            variant=deepcopy(rows);variant[-1][1][field]=1;self.assertTrue(self.read(variant,parents)[2])

    def test_parent_global_failure_never_promotes_capture(self):
        rows,parents=fixture();events,coverage,errors=self.read(rows,parents);self.assertFalse(errors)
        captures=[dict(slot=i,actions=parents,events=events,coverage=coverage,errors=[],expected=['same'],
            parent_errors=['action: failed/incomplete observer coverage']) for i in (1,2)]
        strict=[];result=paired_diagnostics(captures,True,strict)
        self.assertFalse(result['capture_valid']);self.assertFalse(result['traces_equal']);self.assertTrue(strict)
        self.assertTrue(result['selected_receipts_valid']);self.assertTrue(result['selected_traces_equal'])
        captures[0]['parent_errors']=['action: event 1: invalid identity']
        self.assertFalse(paired_diagnostics(captures,False,[])['selected_receipts_valid'])

    def test_value_difference_refuses_claimed_pass(self):
        captures=[]
        for slot in (1,2):
            rows,parents=fixture();rows[1][1]['payload']=struct.pack('<4f',slot,0,0,0)
            events,coverage,errors=self.read(rows,parents);self.assertEqual(errors,[])
            captures.append(dict(slot=slot,actions=parents,events=events,coverage=coverage,errors=errors,parent_errors=[],expected=['same']))
        strict=[];result=paired_diagnostics(captures,True,strict)
        self.assertTrue(result['capture_valid']);self.assertFalse(result['traces_equal']);self.assertEqual(len(result['differences']),1);self.assertTrue(strict)

    def test_optional_component_and_candidate_types_are_compared(self):
        for changed_field in ('optional_saved','optional_type','optional_valid','candidate_type'):
            captures=[]
            for slot in (1,2):
                rows,parents=fixture(True);rows[0][1]['d']=0x80000
                saved=0x501+(slot==2 and changed_field=='optional_saved')
                kind=0x2b80000+(slot==2 and changed_field=='optional_type')
                valid=1+(slot==2 and changed_field=='optional_valid')
                raw=struct.pack('<QQQIB3x',0x80000,0x777,saved,kind,valid)
                rows[1][1].update(optional_before=raw,optional_after=raw)
                if slot==2 and changed_field=='candidate_type':
                    for name,row in rows:
                        if name=='ARRAY' and row['index']==0:row['type']+=1
                events,coverage,errors=self.read(rows,parents);self.assertEqual(errors,[])
                captures.append(dict(slot=slot,actions=parents,events=events,coverage=coverage,errors=[],parent_errors=[],expected=['same']))
            strict=[];result=paired_diagnostics(captures,True,strict)
            self.assertTrue(result['capture_valid']);self.assertFalse(result['traces_equal']);self.assertTrue(result['differences']);self.assertTrue(strict)
            self.assertEqual(result['component_type_scope'],'windows_image_rva')

    def test_unresolved_samples_accounting_and_parent_count_remain_strict(self):
        rows,parents=fixture();stop=rows[-1][1];stop.update(calls=7,unknown_total=6,unknown_samples=4,unknown_rows=4,unknown_omitted=2,reason6=6)
        sample=dict.fromkeys(KEYS['UNKNOWN'],0);sample.update(reason=6,reads=31,owned=1,thread=7,ui=1,boot=2,
            exec=0x20000,current=0x20000,raw_tag=0x66,raw_value=0x300a8,tag=0x66,context=0x30000,lifetime=1,
            entity=0x40000,runtime=99,phase_status=1,eligible=1,app=0x20,view=1,elapsed=100,
            tls=bits(7.083),game=bits(7.083),real=bits(4),saved_time=bits(7.083))
        rows[-1:-1]=[('UNKNOWN',dict(sample,index=i)) for i in range(1,5)]
        events,coverage,errors=self.read(rows,parents);self.assertEqual(errors,[])
        captures=[dict(slot=i,actions=parents,events=events,coverage=coverage,errors=[],parent_errors=[],expected=['same']) for i in (1,2)]
        strict=[];result=paired_diagnostics(captures,True,strict)
        self.assertTrue(result['selected_receipts_valid']);self.assertFalse(result['capture_valid']);self.assertTrue(strict)
        for key,value in [('index',2),('reason',0),('reads',32),('owned',2),('thread',1<<32),('current',0),('saved',5),('game',bits(9))]:
            with self.subTest(key=key):
                changed=deepcopy(rows);changed[3][1][key]=value;self.assertTrue(self.read(changed,parents)[2])
        for key in ('unknown_total','unknown_samples','unknown_rows','unknown_omitted','reason6','calls'):
            changed=deepcopy(rows);changed[-1][1][key]+=1;self.assertTrue(self.read(changed,parents)[2])
        errors=[];evidence(log(rows),parents,errors,parent_coverage={'stop':{'unknown':5}})
        self.assertTrue(any('differs from parent' in error for error in errors))

    def test_per_owner_sequences_ignore_cross_owner_interleaving_and_thread_ids(self):
        captures=[]
        for slot in (1,2):
            rows,parents=fixture();other_rows,other_parents=fixture();other_parents[0]['event']['id']=0xaa
            groups=[(rows,parents),(other_rows,other_parents)]
            if slot==2:groups.reverse()
            combined=[];all_parents=[]
            for ordinal,(items,parent) in enumerate(groups,1):
                parent[0]['event'].update(event=ordinal,thread=7*slot)
                items[0][1].update(event=ordinal,action=ordinal,thread=7*slot,enter=ordinal*2-1,leave=ordinal*2)
                for _,part in items[1:3]:part['event']=ordinal
                combined+=items[:-1];all_parents+=parent
            stop=deepcopy(rows[-1][1]);stop.update(reserved=2,complete=2,rows=6,calls=2)
            combined.append(('STOP',stop));events,coverage,errors=self.read(combined,all_parents);self.assertEqual(errors,[])
            captures.append(dict(slot=slot,actions=all_parents,events=events,coverage=coverage,errors=[],parent_errors=[],expected=['same']))
        self.assertTrue(paired_diagnostics(captures,True,[])['traces_equal'])

    def test_configuration_is_explicit_bounded_and_builder_requires_action_manifest(self):
        self.assertFalse(configured(''))
        config='X4LAN_CONFIG.native_gate_world_values = true\n';self.assertTrue(configured(config))
        for invalid in (config+config,config.replace('true','1'),config.replace('true','false'),config.replace('true','"true"')):
            with self.assertRaises(ValueError):configured(invalid)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);snapshot,document=action_tests.ActionManifestTests().create(root)
            dll=root/'gate.dll';dll.write_bytes(b'test only');manifest=root/'action.json';manifest.write_text(json.dumps(document))
            args=['overlay','--gate-dll',str(dll),'--mode','battle','--snapshot',str(snapshot),'--out',str(root/'out'),'--world-values']
            with mock.patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()) as err,self.assertRaises(SystemExit):builder.main()
            self.assertIn('--world-values requires --world-action-manifest',err.getvalue())
            args+=['--world-action-manifest',str(manifest),'--world-identities','--world-hash-stride','1','--world-hash-watch','0000000000000001','--world-trace-ticks','12','--world-trace-early','--world-queue-trace','--serial-pathing','--npc-diag']
            with mock.patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()):builder.main()
            self.assertTrue(configured(next((root/'out').rglob('config.lua.append')).read_text()))

    def test_main_validator_retains_strict_failure_and_typed_diagnostics(self):
        wire=f'{ENTRY["id"]},4,move.idle'
        with tempfile.TemporaryDirectory() as directory:
            run=Path(directory);(run/'result.json').write_text(json.dumps({'verdict':'fail','exit_code':5,'exit_reason':'fail_pattern'}))
            states={str(t):{str(s):{'game':20+t/60,'real':20+t/60,'digest':('a' if t==0 or s==1 else 'b')*64,'command':'00000000'} for s in (1,2)} for t in (0,1)}
            (run/'native-barrier.json').write_text(json.dumps({'states':states,'hold_seconds':0,'protocol_version':1,'verdict':'fail','failure':'battle_state_mismatch_tick_1'}))
            for slot in (1,2):
                client=run/f'client{slot}';(client/'installed').mkdir(parents=True);(client/'result.json').write_text(json.dumps({'lane_restored':True}))
                (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_action_manifest = '+json.dumps(wire)+'\nX4LAN_CONFIG.native_gate_world_values = true\n')
                rows,_=fixture();rows[1][1]['payload']=struct.pack('<4f',slot,0,0,0)
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n'+action_log(action_fixture())+'\n'+log(rows))
            result=validate(run,'battle')
        self.assertFalse(result['session_pass']);self.assertIn('three-second hold not proven',result['errors'])
        self.assertTrue(result['native_early_action_values']['capture_valid']);self.assertFalse(result['native_early_action_values']['traces_equal'])

    def test_luajit_config_order_retry_and_append_only_drain_before_stop(self):
        harness=probe_tests.SnapshotProbeTests();harness.setUp();lua=harness.lua
        lua.execute('''
          X4LAN_CONFIG.native_gate_world_trace_early=true
          X4LAN_CONFIG.native_gate_world_trace_ticks=12
          X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
          X4LAN_CONFIG.native_gate_world_action_manifest='99,4,move.idle'
          X4LAN_CONFIG.native_gate_world_values=true
          X4LAN_CONFIG.native_gate_world_hash_stride=1
          local gate=package.loadlib('gate')();action_configs=0;value_configs=0;value_reads=0;unknown_reads=0;available=0
          gate.world_trace_config=function() return 1 end
          gate.world_watch=function() return 1 end
          gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
          gate.world_banks=function() return 'test' end
          gate.world_page=function() return 0 end;gate.world_detail=function() return 0 end
          gate.world_trace=function() return 1 end
          gate.world_action_trace_config=function() action_configs=action_configs+1;return 1 end
          gate.world_values_config=function() assert(action_configs==value_configs+1);value_configs=value_configs+1;return 1 end
          gate.world_trace_row=function() return 0 end;gate.world_action_trace_row=function() return 0 end
          gate.world_values_row=function(row) if row==0 then return available end assert(row==value_reads+1);value_reads=row;return 'WORLD_VALUES_EVENT test='..row end
          gate.world_values_unknown_row=function(row) if row==0 then return available end assert(row==unknown_reads+1);unknown_reads=row;return 'WORLD_VALUES_UNKNOWN test='..row end
          gate.world_trace_status=function() return 'test=1' end;gate.world_action_trace_status=function() return 'test=1' end
          gate.world_values_status=function() return 'test=1' end
          gate.world_values_cursors=function() return value_reads,unknown_reads,0 end
          gate.world_values_unknown_identity_row=function(row) if row==0 then return 0 end error('unexpected detail') end
          gate.world_values_unknown_identity_status=function() return 'test=1' end
          local load=LoadGame;LoadGame=function(slot) assert(value_configs==1);load(slot) end
          setmetatable(state,{__index=function() return 0 end})
        ''')
        lua.execute(probe_tests.SOURCE);lua.execute('update();update();update()')
        self.assertEqual(lua.globals().value_configs,1);self.assertEqual(lua.globals().loads,1)
        lua.execute("state.boot_phase=3;state.issued=1;state.input1='00000000';state.input2='00000000';is_menu=false")
        lua.execute('available=1;update()')
        self.assertEqual(lua.globals().value_reads,1);self.assertEqual(lua.globals().unknown_reads,1)
        lua.execute(probe_tests.SOURCE);lua.execute('update()')
        self.assertEqual(lua.globals().value_configs,2)
        lua.execute('available=2;state.active=0;state.completed=1;update()')
        lines=list(lua.globals().logs.values());self.assertFalse(any('ERROR' in line for line in lines),lines)
        row=next(i for i,line in enumerate(lines) if 'WORLD_VALUES_EVENT test=1' in line)
        unknown=next(i for i,line in enumerate(lines) if 'WORLD_VALUES_UNKNOWN test=1' in line)
        stop=next(i for i,line in enumerate(lines) if 'WORLD_VALUES_STOP test=1' in line)
        self.assertLess(row,unknown);self.assertLess(unknown,stop);self.assertEqual(lua.globals().value_reads,2)
        self.assertEqual(sum('WORLD_VALUES_EVENT test=1' in line for line in lines),1)
        self.assertEqual(sum('WORLD_VALUES_UNKNOWN test=1' in line for line in lines),1)

if __name__=='__main__':unittest.main()
