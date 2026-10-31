from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest
import contextlib
import io
import tempfile
import json
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import build_native_gate_overlay as builder
from validate_native_gate import validate as validate_run
import test_native_gate_probe as probe
import native_world_safe_position as g
import native_world_position as p
import native_world_unknown_identity as identity
from test_native_world_position import fixture as position_fixture,encode as encode_position


def fixture():
    e,parents,values,_,_=position_fixture();parent=parents[0];event=parent['event']
    event.update(stage=1,id=0x80d54,name='move.idle',version=4,slot=51,type=0x2c0cc88,action=0xbe2320,pc_before=52,pc_after=52)
    ref=e['objects'][0][0];o=p.obj(ref);base=e['image'];t=0xd3108
    for phase in ('before','after'):
        parent[phase].update(id=0x80d54,name='move.idle',version=4,slot=51,pc=52,vtable=base+0x2c0cc88,target=base+0xbe2320,action=base+0xbe2320)
    for guard in e['outer']+e['radius_guard']+e['samplers'][0]['guards']:
        guard.update(version=4,pc=52,vtable=base+0x2c0cc88,target=base+0xbe2320)
    e.update(caller=base+0xc72905,optional=o['object'],output=t+0xc8,result=t+0xc8,calls=[1,0,1,1],returns=[1,0,1,1],space=0,reference=0,transform=0,time=0,space_present=0,reference_present=0)
    e['transform_guard']=[dict.fromkeys(p.GQ+p.GD,0)]*2;e['transforms']=[bytes(64)]*2;e['objects']=[[bytes(376)]*2 for _ in range(2)]
    e['passed_reference']=[ref,ref];w=dict(zip(g.WITNESS,(t,0xd0000,0xe0000,base+0xc72905,base+0xbe2382,e['frame'],e['optional'])))
    e['caller_witness']=[w,dict(w)]
    branch=dict(zip(p.BQ,(1,0,0x5b,0x5b,2)));e['branch']=[dict(branch)]*2;e['radius_branch']=[dict(branch)]*2
    value=values[1];ve=value['event'];ve.update(caller=0xbe23af,a=parent['before']['node']+0x1b8,b=e['frame'],c=parent['before']['node']+0x1c8,thread=7,pc_before=52,pc_after=52,flags_before=0,flags_after=0)
    final_rng=p.rng_step(p.rng_step(event['rng_after']));event['rng_before']=77;event['rng_after']=final_rng;ve['rng_before']=ve['rng_after']=final_rng
    value['value']['payload']=struct.pack('<4f',7,8,9,0)
    stop=dict.fromkeys(g.STOP,0);stop.update(schema=1,capacity=4,read_limit=256,byte_limit=8192,wire_size=g.WIRE_SIZE,row_bytes=256,reserved=1,complete=1,exported=25,rows=25,calls=9,peak=1,closed=1)
    return e,parents,values,stop


def encode(e):
    out=encode_position(e)
    out+=b''.join(struct.pack('<7Q',*(w[k] for k in g.WITNESS)) for w in e['caller_witness'])
    out+=b''.join(e['passed_reference']);assert len(out)==g.WIRE_SIZE
    return out


def log(e,stop):
    raw=encode(e)
    return '\n'.join([f'NATIVE_GATE WORLD_SAFE_POSITION schema=1 event=1 part={i} data={raw[i*256:(i+1)*256].hex()}' for i in range(g.ROWS)]+['NATIVE_GATE WORLD_SAFE_POSITION_STOP '+' '.join(f'{k}={v}' for k,v in stop.items())])


class SafePositionTests(unittest.TestCase):
    def setUp(self):
        pin=patch.object(identity,'STUB_SHA',position_fixture()[4]);pin.start();self.addCleanup(pin.stop)

    def read(self,e=None,parents=None,values=None,stop=None,text=None):
        defaults=fixture();e=e if e is not None else defaults[0];parents=parents if parents is not None else defaults[1];values=values if values is not None else defaults[2];stop=stop if stop is not None else defaults[3]
        errors=[];result=g.evidence(log(e,stop) if text is None else text,parents,values,errors)
        return result,errors

    def test_unequal_candidate_final_is_valid_diagnostic(self):
        r,errors=self.read();self.assertEqual(errors,[]);self.assertTrue(r['selected_valid'])
        s=r['events'][0]['semantic'];self.assertFalse(s['candidate_payload_equal_final_value']);self.assertFalse(s['avoidance_inputs_covered'])
        e,a,v,stop=fixture();v[1]['value']['payload']=e['output_bytes'];r,errors=self.read(e,a,v,stop);self.assertEqual(errors,[]);self.assertTrue(r['events'][0]['semantic']['candidate_payload_equal_final_value'])

    def test_actual_frame_and_guard_mutants_refuse(self):
        e,a,v,stop=fixture();mutants=[]
        for key in ('caller','descriptor','frame','optional','output','result'):
            m=deepcopy(e);m[key]+=8;mutants.append(m)
        for key in g.WITNESS:
            m=deepcopy(e);m['caller_witness'][0][key]+=8;mutants.append(m)
        for key in ('caller','parent_return'):
            m=deepcopy(e)
            for w in m['caller_witness']:w[key]+=8
            mutants.append(m)
        for slot in range(4):
            m=deepcopy(e);m['calls'][slot]+=1;mutants.append(m)
        for key in ('errors','abnormal','foreign','children','parent'):
            m=deepcopy(e);m[key]=1;mutants.append(m)
        for m in mutants:
            with self.subTest(m=m):self.assertTrue(self.read(m,a,v,stop)[1])

    def test_sampler_and_outer_share_same_tib_interval(self):
        e,a,v,stop=fixture()
        self.assertEqual(self.read(e,a,v,stop)[1],[])
        for lower,upper in ((-4096,0),(0,4096),(-4096,4096)):
            changed=deepcopy(e);sample=changed['samplers'][0]
            sample['stack_limit']+=lower;sample['stack_base']+=upper
            result,errors=self.read(changed,a,v,stop)
            self.assertFalse(result['selected_valid']);self.assertTrue(any('sampler/outer TIB interval differs' in error for error in errors))
        # The copied bounds are checked together; no compiler-specific distance
        # between the wrapper frames is invented by this correction.
        changed=deepcopy(e)
        for witness in changed['caller_witness']:
            witness['stack_limit']-=4096;witness['stack_base']+=4096
        self.assertTrue(self.read(changed,a,v,stop)[1])

    def test_parent_value_omission_failure_and_foreign_invocation(self):
        e,a,v,stop=fixture()
        self.assertTrue(self.read(e,[],v,stop)[1]);self.assertTrue(self.read(e,a,{},stop)[1])
        for key in ('slot','pc_before','pc_after','stage','type','action','id','version'):
            bad=deepcopy(a);bad[0]['event'][key]+=1;self.assertTrue(self.read(e,bad,v,stop)[1])
        for key in ('a','b','c','thread','caller','pc_before','pc_after'):
            bad=deepcopy(v);bad[1]['event'][key]+=1;self.assertTrue(self.read(e,a,bad,stop)[1])
        bad=deepcopy(v);bad[1]['validation_errors']=['parent invalid'];self.assertTrue(self.read(e,a,bad,stop)[1])
        bad=deepcopy(a);bad[0]['validation_errors']=['parent invalid'];self.assertTrue(self.read(e,bad,v,stop)[1])

    def test_strict_stream_bounds_and_terminal(self):
        e,a,v,stop=fixture();text=log(e,stop)
        for bad in (text.replace('part=0 ','part=1 ',1),text.split('\n',1)[1],text.replace('schema=1','schema=2',1),text.replace('\n',' '*2100+'\n',1)):
            self.assertTrue(self.read(e,a,v,stop,bad)[1])
        for k in ('open','restore_open','failures','dropped','owned','protection_pending','flush_pending','patch_failures','patch_errors'):
            bad=dict(stop);bad[k]=1;self.assertTrue(self.read(e,a,v,bad)[1])
        for k in ('reserved','complete','exported','rows','closed'):
            bad=dict(stop);bad[k]=0;self.assertTrue(self.read(e,a,v,bad)[1])

    def test_generator_enclosing_errors_and_pair_differences(self):
        e,a,v,stop=fixture();errors=[]
        r=g.evidence(iter(log(e,stop).splitlines()),iter(a),iter(v.values()),errors,enclosing_errors=['unknown reason6'])
        self.assertEqual(errors,[]);self.assertTrue(r['selected_valid']);self.assertFalse(r['coverage_complete'])
        other=deepcopy(r);other['events'][0]['semantic']['candidate']='00'*16
        errors=[];result=g.paired_diagnostics([dict(slot=1,evidence=r),dict(slot=2,evidence=other)],True,errors)
        self.assertTrue(result['differences']);self.assertTrue(errors)

    def test_explicit_default_off_flag(self):
        self.assertFalse(g.configured(''))
        self.assertTrue(g.configured('X4LAN_CONFIG.native_gate_world_safe_position = true'))
        for bad in ('false','1','nil'):
            with self.assertRaises(ValueError):g.configured('X4LAN_CONFIG.native_gate_world_safe_position = '+bad)

    def test_builder_and_validator_missing_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate.dll';dll.write_bytes(b'owned fixture')
            args=['overlay','--gate-dll',str(dll),'--mode','battle','--out',str(root/'overlay'),'--world-safe-position']
            with patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()) as stderr,self.assertRaises(SystemExit):builder.main()
            self.assertIn('--world-safe-position requires --world-position',stderr.getvalue())
            (root/'result.json').write_text(json.dumps(dict(verdict='fail',exit_code=5,exit_reason='fail_pattern')))
            (root/'native-barrier.json').write_text(json.dumps(dict(states={str(t):{str(s):dict(game=20+t/60,real=20+t/60,digest='a'*64,command='00000000') for s in (1,2)} for t in (0,1)},hold_seconds=0,protocol_version=1,verdict='fail')))
            for slot in (1,2):
                client=root/f'client{slot}';(client/'installed').mkdir(parents=True);(client/'result.json').write_text(json.dumps(dict(lane_restored=True)))
                (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_safe_position = true\n')
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n')
            result=validate_run(root,'battle');self.assertFalse(result['session_pass'])
            self.assertTrue(any('safe position capture lacks' in error for error in result['errors']))
            self.assertFalse(result['native_selected_safe_position_inputs']['selected_valid']);json.dumps(result)

    def test_genuine_fresh_luajit_states_retain_passive_cursor_and_configuration(self):
        for refusal in (False,True):
            shared=dict(configs=0,loads=0,cursor=0,available=0,healthy=True)
            def configure():
                if not shared['healthy']:return 0
                shared['configs']+=1;return 1
            def load():shared['loads']+=1
            def row(n):
                if n==0:return shared['available']
                self.assertEqual(n,shared['cursor']+1);shared['cursor']=n;return 'WORLD_SAFE_POSITION fixture='+str(n)
            def new_state(phase):
                h=probe.SnapshotProbeTests();h.setUp();lua=h.lua
                lua.globals().owned_config=configure;lua.globals().owned_load=load;lua.globals().owned_cursor=lambda:shared['cursor'];lua.globals().owned_row=row
                lua.execute('''
                  X4LAN_CONFIG.native_gate_world_trace_early=true;X4LAN_CONFIG.native_gate_world_trace_ticks=12
                  X4LAN_CONFIG.native_gate_world_hash_stride=1
                  X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
                  X4LAN_CONFIG.native_gate_world_action_manifest='80d54,4,move.idle'
                  X4LAN_CONFIG.native_gate_world_values=true;X4LAN_CONFIG.native_gate_world_position=true;X4LAN_CONFIG.native_gate_world_safe_position=true
                  local gate=package.loadlib('gate')()
                  gate.world_watch=function() return 1 end;gate.world_trace=function() return 1 end
                  gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
                  gate.world_banks=function() return 'test' end;gate.world_page=function() return 0 end;gate.world_detail=function() return 0 end
                  gate.world_trace_config=function() return 1 end;gate.world_action_trace_config=function() return 1 end
                  gate.world_values_config=function() return 1 end;gate.world_position_config=function() return 1 end;gate.world_position_cursor=function() return 0 end;gate.world_position_row=function() return 0 end;gate.world_position_status=function() return "owned=0" end;gate.world_safe_position_config=function() return owned_config() end
                  gate.world_safe_position_cursor=function() return owned_cursor() end
                  gate.world_safe_position_row=function(i) return owned_row(i) end
                  gate.world_safe_position_status=function() return 'owned=0' end
                  gate.world_values_cursors=function() return 0,0,0 end
                  gate.world_trace_row=function() return 0 end;gate.world_action_trace_row=function() return 0 end
                  gate.world_values_row=function() return 0 end;gate.world_values_unknown_row=function() return 0 end
                  gate.world_values_unknown_identity_row=function() return 0 end
                  gate.world_trace_status=function() return 'test=1' end;gate.world_action_trace_status=function() return 'test=1' end
                  gate.world_values_status=function() return 'test=1' end;gate.world_values_unknown_identity_status=function() return 'test=1' end
                  LoadGame=function(slot) assert(slot=='save_009');owned_load() end
                  setmetatable(state,{__index=function() return 0 end})
                ''')
                lua.globals().state.boot_phase=phase
                if phase==3:lua.execute("state.issued=1;is_menu=false;state.input1='00000000';state.input2='00000000'")
                lua.execute(probe.SOURCE);return lua
            first=new_state(0);first.execute('update();update();update()');self.assertEqual(shared['loads'],1)
            second=new_state(2);second.execute('update()');self.assertEqual(shared['configs'],2);self.assertEqual(shared['loads'],1)
            shared['available']=1;second.execute("state.boot_phase=3;state.issued=1;is_menu=false;state.input1='00000000';state.input2='00000000';update()")
            self.assertEqual(shared['cursor'],1,list(second.globals().logs.values()))
            shared['healthy']=not refusal;shared['available']=2
            third=new_state(3);third.execute('update()')
            self.assertEqual(shared['loads'],1);self.assertEqual(shared['cursor'],2)
            if refusal:
                self.assertEqual(third.globals().state.active,0);self.assertEqual(shared['configs'],2)
                self.assertTrue(any('configuration refused' in x for x in third.globals().logs.values()))
            else:
                third.execute('state.active=0;state.completed=1;update()')
                lines=list(third.globals().logs.values());self.assertFalse(any('ERROR' in x for x in lines),lines)
                self.assertEqual(sum('WORLD_SAFE_POSITION fixture=2' in x for x in lines),1)
                self.assertEqual(sum('WORLD_SAFE_POSITION fixture=1' in x for x in lines),0)
                self.assertTrue(any('WORLD_SAFE_POSITION_STOP owned=0' in x for x in lines))


if __name__=='__main__':unittest.main()

