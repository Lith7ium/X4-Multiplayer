from copy import deepcopy
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_world_position as p
import native_world_unknown_identity as identity
from test_native_world_unknown_identity import fixture as identity_fixture
import test_native_gate_probe as probe
import build_native_gate_overlay as builder
from validate_native_gate import validate as validate_run


def fixture():
    _,objects,_,sha=identity_fixture();base=0x140000000;clock=int.from_bytes(struct.pack('<d',7.083),'little')
    guard=dict.fromkeys(p.GQ+p.GD,0)
    guard.update(current=0x20000,context=0x30000,lifetime=77,entity=0x40000,runtime=0x100,script=0x50000,
                 program=0x60000,begin=0x70000,end=0x70750,pc=216,node=0x80000,vtable=base+0x2c0cdd0,
                 target=base+0xbe0ba0,table=0x90000,tls_time=clock,game=clock,real=clock,saved=clock,rng=123,
                 tag=0x66,version=10,app=0x20,view=1,mxcsr=0x1f80,x87cw=0x37f)
    before=deepcopy(guard);before.update(node=guard['node'],exec=guard['current'],frame=guard['current']+0x38,
        saved_time=clock,app_flags=0x20,vbi_loading=0,slot=215,id=0x9d3a4,name='order.fight.patrol',program_hash=99)
    rng1=p.rng_step(123);rng3=p.rng_step(p.rng_step(rng1))
    event=dict(event=1,errors=0,stage=2,type=0x2c0cdd0,action=0xbe0ba0,thread=7,slot=215,id=0x9d3a4,
               version=10,name='order.fight.patrol',rng_before=123,rng_after=rng3)
    parents=[dict(event=event,before=before,after=deepcopy(before),validation_errors=[])]
    e=dict.fromkeys(p.META+p.IDENTITY,0);e.update(schema=2,event=1,action=1,depth=1,thread=7,samples=1,reads=187,bytes=5668,
        image=base,enter=1,leave=2,result=0xa0000,caller=base+0xbe0bd1,descriptor=guard['node']+0x28,
        output=0xa0000,frame=before['frame'],tls=0xb0000,calls=[1]*4,returns=[1]*4)
    e['outer']=[deepcopy(guard),dict(guard,rng=rng3)];e['transform_guard']=[deepcopy(guard),deepcopy(guard)]
    e['radius_guard']=[deepcopy(guard),dict(guard,rng=rng1)]
    e.update(space=0x30100,reference=0,transform=0xc0000,time=clock,range=e['descriptor']+0x68,radius_frame=e['frame'],radius_optional=0,
             fifth=0,radius=0x42200000,space_present=1,reference_present=0,transforms=[bytes(64),bytes(64)],bases=[bytes(16),bytes(16)],output_bytes=struct.pack('<4f',1,2,3,0))
    e['objects']=[[objects[2]['before'],bytes(376)],[objects[2]['before'],bytes(376)]]
    branch=dict(zip(p.BQ,(0,0,0x5b,0,2)));e['branch']=[deepcopy(branch),deepcopy(branch)];e['radius_branch']=deepcopy(e['branch']);e['radius_witness']=dict.fromkeys(p.WQ,0)
    def sample():return dict.fromkeys(p.SQ+p.SD,0)|dict(difference=[bytes(16),bytes(16)],minimum=[bytes(16),bytes(16)],guards=[dict.fromkeys(p.GQ+p.GD,0),dict.fromkeys(p.GQ+p.GD,0)])
    e['samplers']=[sample() for _ in range(4)];s=e['samplers'][0]
    s.update(slot=0xd0108,rng=e['tls']+0x318,caller=base+0xc59951,parent_return=base+0xc59b2f,descriptor=e['range'],frame=e['frame'],
             stack_limit=0xd0000,stack_base=0xe0000,span=0x42c80000,distribution=1,parameter=1,result=0x42200000,source=1,normal=1,
             difference=[struct.pack('<I4xI4x',5,0x42c80000)]*2,minimum=[struct.pack('<I4xQ',5,0)]*2,guards=[deepcopy(guard),dict(guard,rng=rng1)])
    values={1:dict(event=dict(event=1,action=1,kind=0,errors=0,caller=0xbe0c8b,rng_before=rng3,rng_after=rng3),
                   value=dict(tag=0x63,kind=2,payload=e['output_bytes']),validation_errors=[])}
    stop=dict.fromkeys(p.STOP,0);stop.update(schema=2,capacity=64,sampler_capacity=4,read_limit=256,byte_limit=8192,wire_size=p.WIRE_SIZE,row_bytes=256,
                                          reserved=1,complete=1,exported=21,rows=21,calls=1,peak=1,closed=1)
    return e,parents,values,stop,sha


def encode(e):
    out=bytearray()
    def words(names,obj,width):out.extend(struct.pack('<'+('Q' if width==8 else 'I')*len(names),*(obj[n] for n in names)))
    def guard(g):words(p.GQ,g,8);words(p.GD,g,4)
    words(p.META,e,4);out.extend(struct.pack('<8I',*(e['calls']+e['returns'])));words(p.IDENTITY,e,8)
    for field in ('outer','transform_guard','radius_guard'):
        for g in e[field]:guard(g)
    words('space reference transform time range radius_frame radius_optional'.split(),e,8)
    words('fifth radius space_present reference_present'.split(),e,4)
    for field in ('transforms','bases'):out.extend(b''.join(e[field]))
    out.extend(e['output_bytes']);out.extend(b''.join(raw for phase in e['objects'] for raw in phase))
    for s in e['samplers']:
        words(p.SQ,s,8);words(p.SD,s,4);out.extend(b''.join(s['difference']+s['minimum']))
        for g in s['guards']:guard(g)
    for field in ('branch','radius_branch'):
        for branch in e[field]:words(p.BQ,branch,4)
    words(p.WQ,e['radius_witness'],8)
    assert len(out)==p.WIRE_SIZE
    return bytes(out)


def log(events,stop):
    rows=[]
    for event in events:
        raw=encode(event)
        rows.extend(f'NATIVE_GATE WORLD_POSITION schema=2 event={event["event"]} part={n} data={raw[n*256:(n+1)*256].hex()}' for n in range(21))
    if stop is not None:rows.append('NATIVE_GATE WORLD_POSITION_STOP '+' '.join(f'{k}={v}' for k,v in stop.items()))
    return rows


class PositionTests(unittest.TestCase):
    def evaluate(self,event=None,parents=None,values=None,stop=None,lines=None,enclosing=()):
        e,a,v,s,sha=fixture();errors=[]
        with patch.object(identity,'STUB_SHA',sha):
            result=p.evidence(log([e if event is None else event],s if stop is None else stop) if lines is None else lines,
                              a if parents is None else parents,v if values is None else values,errors,enclosing_errors=enclosing)
        return result,errors

    def test_raw_fields_and_parent_value_joins(self):
        result,errors=self.evaluate();self.assertEqual(errors,[]);self.assertTrue(result['coverage_complete'])
        s=result['events'][0]['semantic'];self.assertEqual(s['minimum']['tag'],5);self.assertEqual(s['direction_endpoint_steps'],2)
        self.assertEqual(s['space'],0x1234);self.assertIsNone(s['reference']);self.assertEqual(s['sampler_steps'],1)
        json.dumps(p.paired_diagnostics([dict(slot=i,evidence=result) for i in (1,2)],True,[]))

    def test_unknown_and_bare_position_family_records_remain_strict(self):
        e,a,v,s,_=fixture();rows=log([e],s)
        malformed=('NATIVE_GATE WORLD_POSITION_BOGUS schema=2 event=1',
                   'NATIVE_GATE WORLD_POSITION','NATIVE_GATE WORLD_POSITION_STOP',
                   'NATIVE_GATE WORLD_POSITION_RNG_BOGUS schema=1',
                   'NATIVE_GATE WORLD_POSITION_RNG schema=1 event=1 part=0 data=00',
                   'NATIVE_GATE WORLD_POSITION_RNG_STOP schema=1')
        for extra in malformed:
            with self.subTest(extra=extra):
                result,errors=self.evaluate(lines=rows+[extra])
                self.assertFalse(result['selected_valid']);self.assertTrue(errors)
                claimed=[];pair=p.paired_diagnostics([dict(slot=i,evidence=result) for i in (1,2)],True,claimed)
                self.assertFalse(pair['selected_valid']);self.assertTrue(claimed)

    def branch_fixture(self,transform=True,radius=True,null=False):
        e,a,v,s,sha=fixture()
        def clear(value):
            if isinstance(value,dict):return {k:clear(x) for k,x in value.items()}
            if isinstance(value,list):return [clear(x) for x in value]
            if isinstance(value,bytes):return bytes(len(value))
            return 0
        branch=e['branch'][0];branch['space_tag']=0x5b if transform or null else 0;branch['range_kind']=2 if radius else 0
        e['branch'][1]=deepcopy(branch);e['radius_branch']=[deepcopy(branch),deepcopy(branch)]
        if not transform:
            e['calls'][1]=e['returns'][1]=0
            for key in ('space','reference','transform','time','fifth','space_present','reference_present','transform_guard','transforms','objects'):e[key]=clear(e[key])
        if not radius:
            e['calls'][2:]=e['returns'][2:]=[0,0];e['samples']=0
            for key in ('range','radius_frame','radius_optional','radius','bases','radius_guard','radius_branch','samplers','radius_witness'):e[key]=clear(e[key])
            e['outer'][1]['rng']=123;a[0]['event']['rng_after']=123
            e['output_bytes']=e['transforms'][1][:16] if transform else bytes(16)
            v[1]['event']['rng_before']=v[1]['event']['rng_after']=123;v[1]['value']['payload']=e['output_bytes']
        if null:
            w=e['radius_witness'];w.update(slot=0xd0308,stack_limit=0xd0000,stack_base=0xe0000,caller=e['image']+0xc6fb6f,
                parent_return=0x7ff900001122,resolver_output=0xd0308+0x138)
        return e,a,v,s

    def test_branch_derived_absence_executed_and_evaluated_null(self):
        for transform,radius,null in ((True,True,False),(False,True,False),(True,False,False),(False,False,False),(False,True,True)):
            e,a,v,s=self.branch_fixture(transform,radius,null)
            result,errors=self.evaluate(e,a,v,s);self.assertEqual(errors,[]);self.assertTrue(result['coverage_complete'])
            semantic=result['events'][0]['semantic']
            self.assertEqual(semantic['paths']['space'],'executed' if transform else 'evaluated_null' if null else 'expression_absent')
            self.assertEqual(semantic['paths']['radius'],'sampled' if radius else 'range_absent')
            if not transform:self.assertIsNone(semantic['space']);self.assertEqual(semantic['paths']['reference'],'not_evaluated')
            if not radius:self.assertIsNone(semantic['radius']);self.assertIsNone(semantic['minimum'])

    def test_branch_inputs_witness_and_inactive_sections_are_independently_checked(self):
        e,a,v,s=self.branch_fixture(False,True,True)
        for key in p.WQ:
            changed=deepcopy(e);changed['radius_witness'][key]^=1
            if key=='parent_return':continue  # Diagnostic continuation, not a guessed native return RVA.
            with self.subTest(witness=key):self.assertTrue(self.evaluate(changed,a,v,s)[1])
        for group in ('branch','radius_branch'):
            for phase in (0,1):
                for key in p.BQ:
                    changed=deepcopy(e);changed[group][phase][key]^=1
                    with self.subTest(group=group,phase=phase,key=key):self.assertTrue(self.evaluate(changed,a,v,s)[1])
        for transform,radius,null in ((False,True,False),(True,False,False),(False,False,False)):
            e,a,v,s=self.branch_fixture(transform,radius,null)
            if not transform:
                for field in ('space','reference','transform','time','fifth','space_present','reference_present'):
                    changed=deepcopy(e);changed[field]=1;self.assertTrue(self.evaluate(changed,a,v,s)[1])
                changed=deepcopy(e);changed['transform_guard'][0]['rng']=1;self.assertTrue(self.evaluate(changed,a,v,s)[1])
            if not radius:
                for field in ('range','radius_frame','radius_optional','radius'):
                    changed=deepcopy(e);changed[field]=1;self.assertTrue(self.evaluate(changed,a,v,s)[1])
                changed=deepcopy(e);changed['samplers'][0]['normal']=1;self.assertTrue(self.evaluate(changed,a,v,s)[1])
                changed=deepcopy(e);changed['radius_branch'][0]['kind']=1;self.assertTrue(self.evaluate(changed,a,v,s)[1])
        e,a,v,s=self.branch_fixture(False,False,True);self.assertTrue(self.evaluate(e,a,v,s)[1])
        for kind in (1,3,4):
            e,a,v,s=self.branch_fixture();e['branch'][0]['range_kind']=e['branch'][1]['range_kind']=kind
            self.assertTrue(self.evaluate(e,a,v,s)[1])

    def test_legacy_rows_are_readable_but_never_promoted(self):
        e,a,v,s,sha=fixture();e['schema']=1;s['schema']=1;s['wire_size']=p.LEGACY_WIRE_SIZE
        raw=encode(e)[:p.LEGACY_WIRE_SIZE]
        rows=[f'NATIVE_GATE WORLD_POSITION schema=1 event=1 part={i} data={raw[i*256:(i+1)*256].hex()}' for i in range(21)]
        rows.append('NATIVE_GATE WORLD_POSITION_STOP '+' '.join(f'{k}={n}' for k,n in s.items()))
        result,errors=self.evaluate(lines=rows);self.assertFalse(result['selected_valid']);self.assertEqual(len(errors),2)
        self.assertEqual(result['events'][0]['decoded']['schema'],1)

    def test_all_identity_scalar_flags_and_bounds_rederived(self):
        e,_,_,_,_=fixture()
        mutations={'schema':1,'errors':1,'abnormal':1,'foreign':1,'samples':0,'reads':257,'bytes':8193,'depth':5,'thread':8,
                   'result':0,'caller':0,'descriptor':0,'frame':0,'optional':1,'fifth':1,'range':0,'radius_frame':0,'radius_optional':1,
                   'time':0,'space_present':0,'reference_present':1,'image':0,'tls':0}
        for key,value in mutations.items():
            with self.subTest(key=key):
                x=deepcopy(e);x[key]=value;self.assertTrue(self.evaluate(x)[1])
        for field in p.GQ+p.GD:
            x=deepcopy(e);x['outer'][1][field]^=1
            with self.subTest(guard=field):self.assertTrue(self.evaluate(x)[1])
        for group in ('calls','returns'):
            for i in range(4):
                x=deepcopy(e);x[group][i]=0;self.assertTrue(self.evaluate(x)[1])

    def test_actual_value_stack_and_rng_scope_mutants(self):
        e,_,_,_,_=fixture()
        for field,value in dict(slot=1,rng=0,caller=0,parent_return=0,descriptor=0,frame=0,stack_limit=0,stack_base=0,
                                span=0x7fc00000,result=0x7f800000,source=0,normal=0,abnormal=1,distribution=7,parameter=4097).items():
            x=deepcopy(e);x['samplers'][0][field]=value
            with self.subTest(field=field):self.assertTrue(self.evaluate(x)[1])
        for name in ('minimum','difference'):
            for phase in (0,1):
                x=deepcopy(e);x['samplers'][0][name][phase]=struct.pack('<I4xQ',10,1);self.assertTrue(self.evaluate(x)[1])
        x=deepcopy(e);x['samplers'][1]['normal']=1;self.assertTrue(self.evaluate(x)[1])
        for field in ('bases','objects'):
            x=deepcopy(e)
            if field=='bases':x[field][1]=bytes([1])*16
            else:x[field][1][0]=bytes(376)
            self.assertTrue(self.evaluate(x)[1])

    def test_partial_identity_never_becomes_typed_null(self):
        e,_,_,_,_=fixture()
        for kind in ('partial','null','saved','generation'):
            x=deepcopy(e);raw=bytearray(x['objects'][0][0])
            if kind=='partial':struct.pack_into('<I',raw,104,0)
            elif kind=='null':x['space']=0;x['space_present']=0
            elif kind=='saved':struct.pack_into('<Q',raw,16,0)
            else:raw[346]^=1
            x['objects'][0][0]=x['objects'][1][0]=bytes(raw);self.assertTrue(self.evaluate(x)[1])

    def test_coherent_valid_class_changes_remain_visible_in_paired_inputs(self):
        for which in ('space','reference'):
            e,_,_,_,_=fixture()
            if which=='reference':
                e['reference']=e['space'];e['reference_present']=1
                for phase in range(2):e['objects'][phase][1]=e['objects'][phase][0]
            baseline,errors=self.evaluate(e);self.assertFalse(errors)
            for change in ('vtable','rtti_name'):
                x=deepcopy(e);index=0 if which=='space' else 1;raw=bytearray(x['objects'][0][index])
                if change=='vtable':struct.pack_into('<Q',raw,64,struct.unpack_from('<Q',raw,64)[0]+8)
                else:raw[130]^=1
                x['objects'][0][index]=x['objects'][1][index]=bytes(raw)
                changed,errors=self.evaluate(x);self.assertFalse(errors);self.assertTrue(changed['selected_valid'])
                errors=[];pair=p.paired_diagnostics([dict(slot=1,evidence=baseline),dict(slot=2,evidence=changed)],True,errors)
                self.assertTrue(errors);self.assertIn(which+'_class',pair['differences'][0]['fields'])
                self.assertEqual(changed['events'][0]['semantic'][which+'_class']['scope'],'windows_image_class_v1')

    def test_incoming_win64_return_slot_and_untrimmed_row_bounds(self):
        e,_,_,s,_=fixture()
        for low_bits in range(16):
            changed=deepcopy(e);changed['samplers'][0]['slot']=(changed['samplers'][0]['slot']&~15)|low_bits
            result,errors=self.evaluate(changed)
            self.assertEqual(bool(errors),low_bits!=8);self.assertEqual(result['selected_valid'],low_bits==8)
        rows=log([e],s)
        for padding in (' '*2100,'\t'*2100,'\r'*2100):
            result,errors=self.evaluate(lines=[rows[0]+padding]+rows[1:]);self.assertFalse(result['selected_valid'])
            self.assertTrue(any('raw row bound' in error for error in errors))
        self.assertFalse(self.evaluate(lines=[line+'\r\n' for line in rows])[1])

    def test_missing_invalid_parent_and_value_keep_global_failures(self):
        _,a,v,_,_=fixture()
        for kind in ('parent','parent_failure','value','value_failure','payload','caller'):
            aa,vv=deepcopy(a),deepcopy(v)
            if kind=='parent':aa=[]
            elif kind=='parent_failure':aa[0]['validation_errors']=['failed parent']
            elif kind=='value':vv={}
            elif kind=='value_failure':vv[1]['validation_errors']=['bad value']
            elif kind=='payload':vv[1]['value']['payload']=bytes(16)
            else:vv[1]['event']['caller']=0
            self.assertTrue(self.evaluate(parents=aa,values=vv)[1])
        result,errors=self.evaluate(enclosing=['reason6 remains unresolved']);self.assertFalse(errors)
        self.assertTrue(result['selected_valid']);self.assertFalse(result['coverage_complete'])
        strict=[];p.paired_diagnostics([dict(slot=i,evidence=result) for i in (1,2)],True,strict);self.assertTrue(strict)

    def test_chunks_chronology_counters_and_terminal_debt(self):
        e,_,_,s,_=fixture();rows=log([e],s)
        for lines in (rows[:-1],rows[1:],rows+[rows[0]],rows+[rows[-1]],[rows[1],rows[0]]+rows[2:]):self.assertTrue(self.evaluate(lines=lines)[1])
        for field in ('failures','dropped','open','restore_open','owned','protection_pending','flush_pending','patch_failures','patch_errors'):
            stop=deepcopy(s);stop[field]=1;self.assertTrue(self.evaluate(stop=stop)[1])
        for key,value in (('children',1),('parent',1),('depth',2),('enter',3),('leave',3)):
            x=deepcopy(e);x[key]=value;self.assertTrue(self.evaluate(x)[1])

    def test_iterable_and_pair_refusals_are_json_safe(self):
        e,a,v,s,sha=fixture();errors=[]
        with patch.object(identity,'STUB_SHA',sha):result=p.evidence(iter(log([e],s)),a,v,errors)
        self.assertFalse(errors)
        for slots in ((1,),(1,1),(1,3)):
            errors=[];paired=p.paired_diagnostics([dict(slot=i,evidence=result) for i in slots],True,errors)
            self.assertFalse(paired['selected_valid']);self.assertTrue(errors);json.dumps(paired)

    def test_explicit_configuration(self):
        self.assertFalse(p.configured(''));self.assertTrue(p.configured('X4LAN_CONFIG.native_gate_world_position = true'))
        for value in ('false','1','true -- comment',"'true'"):
            with self.assertRaises(ValueError):p.configured('X4LAN_CONFIG.native_gate_world_position = '+value)

    def test_builder_and_main_validator_refuse_missing_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);dll=root/'gate.dll';dll.write_bytes(b'owned unit fixture')
            args=['overlay','--gate-dll',str(dll),'--mode','battle','--out',str(root/'overlay'),'--world-position']
            with patch.object(sys,'argv',args),contextlib.redirect_stderr(io.StringIO()) as stderr,self.assertRaises(SystemExit):builder.main()
            self.assertIn('--world-position requires --world-values',stderr.getvalue())
            (root/'result.json').write_text(json.dumps(dict(verdict='fail',exit_code=5,exit_reason='fail_pattern')))
            (root/'native-barrier.json').write_text(json.dumps(dict(states={str(t):{str(s):dict(game=20+t/60,real=20+t/60,digest='a'*64,command='00000000') for s in (1,2)} for t in (0,1)},hold_seconds=0,protocol_version=1,verdict='fail')))
            for slot in (1,2):
                client=root/f'client{slot}';(client/'installed').mkdir(parents=True);(client/'result.json').write_text(json.dumps(dict(lane_restored=True)))
                (client/'installed/x4_lan_lab-config.lua').write_text('X4LAN_CONFIG.native_gate_world_position = true\n')
                (client/'engine-output.log').write_text(f'NATIVE_GATE STOP slot=p{slot} completed=1 reason=1 restored=1 wait_game_delta=0 max_dt_error=0\n')
            result=validate_run(root,'battle');self.assertFalse(result['session_pass'])
            self.assertTrue(any('position capture lacks' in error for error in result['errors']))
            self.assertFalse(result['native_selected_position_inputs']['selected_valid']);json.dumps(result)

    def test_genuine_fresh_luajit_states_retain_native_cursor_and_configuration(self):
        for refusal in (False,True):
            shared=dict(configs=0,loads=0,cursor=0,available=0,healthy=True)
            def configure():
                if not shared['healthy']:return 0
                shared['configs']+=1;return 1
            def load():shared['loads']+=1
            def row(n):
                if n==0:return shared['available']
                self.assertEqual(n,shared['cursor']+1);shared['cursor']=n;return 'WORLD_POSITION fixture='+str(n)
            def new_state(phase):
                h=probe.SnapshotProbeTests();h.setUp();lua=h.lua
                lua.globals().owned_config=configure;lua.globals().owned_load=load;lua.globals().owned_cursor=lambda:shared['cursor'];lua.globals().owned_row=row
                lua.execute('''
                  X4LAN_CONFIG.native_gate_world_trace_early=true;X4LAN_CONFIG.native_gate_world_trace_ticks=12
                  X4LAN_CONFIG.native_gate_world_hash_stride=1
                  X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
                  X4LAN_CONFIG.native_gate_world_action_manifest='9d3a4,10,order.fight.patrol'
                  X4LAN_CONFIG.native_gate_world_values=true;X4LAN_CONFIG.native_gate_world_position=true
                  local gate=package.loadlib('gate')()
                  gate.world_watch=function() return 1 end;gate.world_trace=function() return 1 end
                  gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
                  gate.world_banks=function() return 'test' end;gate.world_page=function() return 0 end;gate.world_detail=function() return 0 end
                  gate.world_trace_config=function() return 1 end;gate.world_action_trace_config=function() return 1 end
                  gate.world_values_config=function() return 1 end;gate.world_position_config=function() return owned_config() end
                  gate.world_position_cursor=function() return owned_cursor() end
                  gate.world_position_row=function(i) return owned_row(i) end
                  gate.world_position_status=function() return 'owned=0' end
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
                self.assertEqual(sum('WORLD_POSITION fixture=2' in x for x in lines),1)
                self.assertEqual(sum('WORLD_POSITION fixture=1' in x for x in lines),0)
                self.assertTrue(any('WORLD_POSITION_STOP owned=0' in x for x in lines))


if __name__=='__main__':unittest.main()
