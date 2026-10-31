from copy import deepcopy
import hashlib
import json
import struct
import unittest
from unittest.mock import patch
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/dev'))
import native_world_position_rng as r
import native_world_position as p
import test_native_world_position as position_tests
import test_native_gate_probe as probe
import native_world_find_rng as find_reader
import test_native_gate_world_find_rng as find_tests
position_log=position_tests.log


def encode(e):
    out=bytearray(struct.pack('<20I16Q',*(e[k] for k in r.META+r.QUADS)))
    for i in e['identity']:out.extend(struct.pack('<13Q4I',*(i[k] for k in r.IQ+r.ID)))
    out.extend(b''.join(e['maximum']+e['scratch']));out.extend(struct.pack('<4I',*sum(e['math'],[])))
    out.extend(e['output']);out.extend(bytes.fromhex(e['digest']));assert len(out)==576;return bytes(out)


def receipt_log(e,stop):
    raw=encode(e);rows=[f'NATIVE_GATE WORLD_POSITION_RNG schema=1 event={e["event"]} part={i} data={raw[i*256:(i+1)*256].hex()}' for i in range(3)]
    rows.append('NATIVE_GATE WORLD_POSITION_RNG_STOP '+' '.join(f'{k}={v:016x}' if k=='seed' else f'{k}={v}' for k,v in stop.items()));return rows


def fixture(composition=2):
    e,a,v,stop=position_tests.PositionTests().branch_fixture(False,True,False);s=e['samplers'][0];seed1=r.rng_step(r.SEED);seed3=r.rng_step(r.rng_step(seed1))
    for group in (e['outer'],e['radius_guard'],s['guards']):
        for g in group:g['x87cw']=0x27f
    e['outer'][1]['rng']=seed3;e['radius_guard'][1]['rng']=s['guards'][1]['rng']=seed1
    e['radius']=s['result']=0x46c881ce;s['span']=0x47435000;s['minimum']=[bytes(16)]*2;s['difference']=[struct.pack('<I4xI4x',5,0x47435000)]*2
    e['action']=2
    a[0]['event'].update(event=2,parent=0,depth=1,boot=2,tick=0,rng_after=123)
    for b in ('before','after'):a[0][b].update(program_hash=0x985d66488a43c795,x87cw=0x27f)
    v[1]['event'].update(action=2,rng_before=123,rng_after=123)
    identity={k:a[0]['before'].get('id' if k=='saved' else k,0) for k in r.IQ+r.ID}
    identity.update(order=0x100000,order_id=0x40001,backlink=identity['context'],registry=0x110000,group=0x120000,state=1,flags=0x10000,generation=1)
    record=dict.fromkeys(r.META+r.QUADS,0);record.update(schema=1,event=1,thread=7,position=1,action=2,occurrence=1,samples=1,
        write_attempted=1,applied=1,restore_required=1,restored=1,normal=1,reads=152,bytes=1104,composition=composition,find_event=1,
        caller_before=123,seed=r.SEED,scoped_before=r.SEED,sampler_after=seed1,scoped_after=seed3,caller_after=123,tls=e['tls'],address=s['rng'],
        sampler_slot=s['slot'],image=e['image'],raw_outer_before=123,raw_sampler_before=123,frame=e['frame'],descriptor=e['descriptor'],anchor_wait=2,find_candidate=4,
        identity=[deepcopy(identity),deepcopy(identity)],maximum=[struct.pack('<I4xI4x',5,0x47435000)]*2,scratch=[bytes(16)]*2,math=[[1,2],[1,2]],output=e['output_bytes'],digest=r.DIGEST)
    rng_stop=dict(schema=1,composition=composition,capacity=8,reserved=1,complete=1,exported=3,rows=3,calls=3,open=0,peak=1,restore_open=0,
                  closed=1,failures=0,dropped=0,debt=0,used=1,seed=r.SEED,digest=r.DIGEST)
    find=dict(selected_scope_valid=True,composition=composition,events={1:{0:dict(candidate=4,thread=7,action=1),**{i:dict(identity,event=1,part=i) for i in (1,2,3)}}})
    waits=[dict(input_output=dict(id=0x9d3a4,name='order.fight.patrol'),raw=[dict(event=2)],validation_errors=[])]
    return e,a,v,stop,record,rng_stop,find,waits


class PositionRngTests(unittest.TestCase):
    def evaluate(self,items=None,lines=None,extras=(),enclosing_errors=()):
        e,a,v,stop,record,rstop,find,waits=deepcopy(items or fixture());errors=[];position_errors=[]
        combined=position_log([e],stop)+(receipt_log(record,rstop) if lines is None else lines)+list(extras)
        prepared=r.prepare(combined,r.selection(find['composition']),find,waits,errors)
        positions=p.evidence(combined,a,v,position_errors,enclosing_errors=enclosing_errors,intervention=prepared)
        result=r.finalize(prepared,positions,errors)
        return result,errors,positions,position_errors

    def test_exact_key_and_both_compositions(self):
        for c in (1,2):
            doc=r.selection(c);key=r.key_bytes(doc);self.assertEqual(len(key),260);self.assertEqual(hashlib.sha256(key).hexdigest(),r.DIGEST)
            self.assertEqual(int.from_bytes(hashlib.sha256(key).digest()[:8],'little'),r.SEED);self.assertIn(f';{c};',r.manifest_wire(doc))
            result,errors,positions,perrors=self.evaluate(fixture(c));self.assertEqual(errors+perrors,[]);self.assertTrue(result['selected_scope_valid']);self.assertTrue(positions['selected_valid'])
        for k in ('expected_seed_hex','expected_key_sha256','program_shape_hash'):
            doc=r.selection();doc[k]='0'*len(doc[k]);self.assertRaises(ValueError,r.manifest_wire,doc)
        doc=r.selection();doc['selected']['saved_script_decimal']=0x38361;self.assertRaises(ValueError,r.manifest_wire,doc)
        doc=r.selection();doc['expected_find_composition']=True;self.assertRaises(ValueError,r.manifest_wire,doc)

    def test_native_widths_receipts_and_join_mutants(self):
        good=fixture();self.assertEqual(r.decode(encode(good[4])),good[4])
        for key in r.META+r.QUADS:
            with self.subTest(field=key):
                items=deepcopy(good);items[4][key]^=1
                if key in ('reads','bytes'):items[4][key]=0
                if key=='event':continue
                result,errors,positions,perrors=self.evaluate(items)
                self.assertFalse(result['selected_scope_valid'],(key,errors,perrors))
        for phase in (0,1):
            for key in r.IQ+r.ID:
                with self.subTest(identity=key,phase=phase):
                    items=deepcopy(good);items[4]['identity'][phase][key]^=1
                    self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])
        for key in ('maximum','scratch'):
            for offset in (0,8):
                items=deepcopy(good);raw=bytearray(items[4][key][0]);raw[offset]^=1;items[4][key][0]=bytes(raw)
                self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])
        for side in (0,1):
            items=deepcopy(good);items[4]['math'][side][0]=2;self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])
        for key in r.STOP-{'calls','peak'}:
            items=deepcopy(good);value=items[5][key];items[5][key]=('0'*len(value) if isinstance(value,str) else value^1)
            self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'],key)

    def test_no_natural_reinterpretation_and_strict_pair(self):
        e,a,v,stop,*_=fixture();errors=[];natural=p.evidence(position_log([e],stop),a,v,errors)
        self.assertFalse(natural['selected_valid']);self.assertTrue(errors)
        result,errors,_,_=self.evaluate();caps=[dict(slot=i,evidence=deepcopy(result),errors=[]) for i in (1,2)]
        paired=r.paired_diagnostics(caps,True,errors);self.assertTrue(paired['outputs_equal']);self.assertEqual(errors,[])
        caps[1]['slot']=1;self.assertFalse(r.paired_diagnostics(caps,True,errors)['selected_scope_valid']);self.assertTrue(errors)
        caps=[dict(slot=i,evidence=deepcopy(result),errors=[]) for i in (1,2)];caps[1]['evidence']['composition']=1
        self.assertFalse(r.paired_diagnostics(caps,True,[])['selected_scope_valid'])
        for field in ('math','output'):
            caps=[dict(slot=i,evidence=deepcopy(result),errors=[]) for i in (1,2)]
            if field=='math':caps[1]['evidence']['events'][1][field]=[[0,2],[0,2]]
            else:caps[1]['evidence']['events'][1][field]=bytes(16)
            self.assertFalse(r.paired_diagnostics(caps,True,[])['outputs_equal'])

    def test_rows_cannot_be_omitted_padded_or_reordered(self):
        items=fixture();rows=receipt_log(items[4],items[5])
        for bad in (rows[:-1],rows[1:],rows+[rows[0]],rows[::-1], [rows[0]+' '*1024]+rows[1:]):
            self.assertFalse(self.evaluate(lines=bad)[0]['selected_scope_valid'])
        items=fixture();items[6]['selected_scope_valid']=False;self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])
        items=fixture();items[7][0]['validation_errors']=['bad wait'];self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])

    def test_find_must_be_prior_and_on_the_same_thread(self):
        for composition in (1,2):
            for key,value in (('action',0),('action',2),('action',3),('thread',8),('action',None),('thread',None)):
                with self.subTest(composition=composition,key=key,value=value):
                    items=fixture(composition);prior=items[6]['events'][1][0]
                    if value is None:del prior[key]
                    else:prior[key]=value
                    result,errors,positions,perrors=self.evaluate(items)
                    self.assertFalse(result['selected_scope_valid']);self.assertFalse(positions['selected_valid']);self.assertTrue(errors+perrors)
                    claimed=[];r.paired_diagnostics([dict(slot=i,evidence=result,errors=errors) for i in (1,2)],True,claimed);self.assertTrue(claimed)

    def test_actual_find_reader_cannot_authorize_a_later_action(self):
        items=list(fixture(1));identity=items[4]['identity'][0]
        rows,actions,values,waits=find_tests.fixture()
        fields=('context','lifetime','entity','runtime','script','exec','program')
        for kind,row in rows:
            if kind=='IDENTITY':row.update({k:identity[k] for k in r.IQ+r.ID})
        for side in ('before','after'):
            actions[0][side].update({k:identity[k] for k in fields});actions[0][side]['frame']=identity['exec']+0x38
        values[2]['event']['c']=identity['exec']+0x38;values[3]['event']['b']=identity['exec']+0x38
        waits[0]['raw'][1].update({k:identity[k] for k in fields});waits[0]['raw'][0]['event']=2
        errors=[];found=find_reader.evidence(find_tests.log(rows),find_reader.selection(),actions,values,waits,errors,'prior Find')
        self.assertTrue(found['selected_scope_valid']);self.assertEqual(errors,[]);self.assertEqual(found['events'][1][0]['action'],4)
        items[4]['find_candidate']=found['events'][1][0]['candidate'];items[6]=found;items[7]=waits
        self.assertFalse(self.evaluate(items)[0]['selected_scope_valid'])
        # The same independently parsed Find4 is a valid prior dependency for
        # Position5 with unchanged identity and completed top-level parents.
        items[0]['action']=items[1][0]['event']['event']=items[2][1]['event']['action']=items[4]['action']=5
        result,errors,positions,perrors=self.evaluate(items)
        self.assertTrue(result['selected_scope_valid']);self.assertTrue(positions['selected_valid']);self.assertEqual(errors+perrors,[])

    def test_configured_stream_delegates_only_exact_rng_record_names(self):
        result,errors,positions,perrors=self.evaluate()
        self.assertTrue(result['selected_scope_valid']);self.assertTrue(positions['selected_valid']);self.assertEqual(errors+perrors,[])
        for extra in ('NATIVE_GATE WORLD_POSITION_BOGUS schema=2 event=1',
                      'NATIVE_GATE WORLD_POSITION','NATIVE_GATE WORLD_POSITION_STOP',
                      'NATIVE_GATE WORLD_POSITION_RNG_BOGUS schema=1'):
            with self.subTest(extra=extra):
                result,errors,positions,perrors=self.evaluate(extras=[extra])
                self.assertFalse(positions['selected_valid']);self.assertTrue(perrors)
                claimed=[];p.paired_diagnostics([dict(slot=i,evidence=positions) for i in (1,2)],True,claimed);self.assertTrue(claimed)
                self.assertFalse(result['selected_scope_valid']);self.assertTrue(errors)
                claimed=[];pair=r.paired_diagnostics([dict(slot=i,evidence=result,errors=[]) for i in (1,2)],True,claimed)
                self.assertFalse(pair['selected_scope_valid']);self.assertFalse(pair['outputs_equal']);self.assertTrue(claimed)
        # Bare known RNG tokens are delegated, then refused by their dedicated
        # parser. Their malformed form cannot authorize the bridge.
        for extra in ('NATIVE_GATE WORLD_POSITION_RNG','NATIVE_GATE WORLD_POSITION_RNG_STOP'):
            result,errors,positions,_=self.evaluate(extras=[extra])
            self.assertTrue(errors);self.assertFalse(result['selected_scope_valid']);self.assertFalse(positions['selected_valid'])

    def test_scoped_result_requires_complete_valid_raw_position_stream(self):
        for composition in (1,2):
            for key,value in (('open',1),('failures',1),('patch_failures',1),('exported',0),('missing',None)):
                with self.subTest(composition=composition,key=key):
                    items=list(fixture(composition))
                    if key=='missing':items[3]=None
                    else:items[3][key]=value
                    result,errors,positions,perrors=self.evaluate(items)
                    self.assertTrue(positions['events'][0]['supported'])
                    self.assertFalse(positions['selected_valid']);self.assertTrue(perrors)
                    self.assertFalse(result['selected_scope_valid']);self.assertTrue(errors)
                    # The receipt's own validity must refuse a pair even if a
                    # caller forgets to forward the separate raw error list.
                    claimed=[];pair=r.paired_diagnostics([dict(slot=i,evidence=result,errors=[]) for i in (1,2)],True,claimed)
                    self.assertFalse(pair['selected_scope_valid']);self.assertFalse(pair['outputs_equal']);self.assertTrue(claimed)

    def test_unrelated_enclosing_coverage_is_kept_separate(self):
        result,errors,positions,perrors=self.evaluate(enclosing_errors=['unrelated Action/Values coverage failure'])
        self.assertTrue(positions['selected_valid']);self.assertFalse(positions['coverage_complete'])
        self.assertTrue(result['selected_scope_valid']);self.assertEqual(errors+perrors,[])
        self.assertEqual(positions['enclosing_errors'],['unrelated Action/Values coverage failure'])
        claimed=[];p.paired_diagnostics([dict(slot=i,evidence=positions) for i in (1,2)],True,claimed);self.assertTrue(claimed)

    def test_actual_lua_configuration_order_and_retained_drain(self):
        for refuse in (False,True):
            shared=dict(configs=0,loads=0,cursor=0,available=0,healthy=True)
            def config(text):
                self.assertEqual(text,'position-wire')
                if not shared['healthy']:return 0
                shared['configs']+=1;return 1
            def row(n):
                if n==0:return shared['available']
                self.assertEqual(n,shared['cursor']+1);shared['cursor']=n;return 'WORLD_POSITION_RNG fixture='+str(n)
            def new_state(phase):
                h=probe.SnapshotProbeTests();h.setUp();lua=h.lua
                lua.globals().owned_config=config;lua.globals().owned_row=row;lua.globals().owned_cursor=lambda:shared['cursor']
                lua.globals().owned_load=lambda:shared.__setitem__('loads',shared['loads']+1)
                lua.execute('''
                  X4LAN_CONFIG.native_gate_world_trace_early=true;X4LAN_CONFIG.native_gate_world_trace_ticks=12
                  X4LAN_CONFIG.native_gate_world_hash_stride=1;X4LAN_CONFIG.native_gate_world_hash_watch={'0000000000000063'}
                  X4LAN_CONFIG.native_gate_world_action_manifest='action';X4LAN_CONFIG.native_gate_world_values=true
                  X4LAN_CONFIG.native_gate_world_position=true;X4LAN_CONFIG.native_gate_world_wait_manifest='wait'
                  X4LAN_CONFIG.native_gate_world_wait_rng_manifest='wait-rng';X4LAN_CONFIG.native_gate_world_find_rng_manifest='find'
                  X4LAN_CONFIG.native_gate_world_position_rng_manifest='position-wire'
                  local gate=package.loadlib('gate')();local position_ready=false;local find_ready=false
                  gate.world_trace_config=function() return 1 end;gate.world_action_trace_config=function() return 1 end
                  gate.world_wait_trace_config=function() return 1 end;gate.world_wait_rng_config=function() return 1 end
                  gate.world_values_config=function() return 1 end
                  gate.world_position_config=function() position_ready=true;return 1 end
                  gate.world_find_rng_config=function() find_ready=true;return 1 end
                  gate.world_position_rng_config=function(text) assert(position_ready and find_ready);return owned_config(text) end
                  gate.world_position_rng_cursor=function() return owned_cursor() end
                  gate.world_position_rng_row=function(i) return owned_row(i) end
                  gate.world_position_rng_status=function() return 'owned=0' end
                  gate.world_position_cursor=function() return 0 end;gate.world_position_row=function() return 0 end
                  gate.world_position_status=function() return 'test=1' end
                  gate.world_watch=function() return 1 end;gate.world_trace=function() return 1 end
                  gate.world_capture=function(tick) return 'tick='..tick..' state=WORLD_IMPORTED_1:'..tick end
                  gate.world_banks=function() return 'test' end;gate.world_page=function() return 0 end;gate.world_detail=function() return 0 end
                  gate.world_values_cursors=function() return 0,0,0 end
                  for _,name in ipairs({'world_trace_row','world_action_trace_row','world_wait_trace_row','world_wait_rng_row','world_find_rng_row',
                    'world_values_row','world_values_unknown_row','world_values_unknown_identity_row'}) do gate[name]=function() return 0 end end
                  for _,name in ipairs({'world_trace_status','world_action_trace_status','world_wait_trace_status','world_wait_rng_status','world_find_rng_status',
                    'world_values_status','world_values_unknown_identity_status'}) do gate[name]=function() return 'test=1' end end
                  LoadGame=function(slot) assert(slot=='save_009');owned_load() end
                  setmetatable(state,{__index=function() return 0 end})
                ''')
                lua.globals().state.boot_phase=phase
                if phase==3:lua.execute("state.issued=1;is_menu=false;state.input1='00000000';state.input2='00000000'")
                lua.execute(probe.SOURCE);return lua
            first=new_state(0);first.execute('update();update();update()');self.assertEqual(shared['loads'],1)
            second=new_state(2);second.execute('update()');self.assertEqual(shared['configs'],2);self.assertEqual(shared['loads'],1)
            shared['available']=3;second.execute("state.boot_phase=3;state.issued=1;is_menu=false;state.input1='00000000';state.input2='00000000';update()")
            self.assertEqual(shared['cursor'],3,list(second.globals().logs.values()))
            shared['healthy']=not refuse;third=new_state(3);third.execute('update()')
            self.assertEqual(shared['loads'],1);self.assertEqual(shared['cursor'],3)
            if refuse:
                self.assertEqual(third.globals().state.active,0);self.assertEqual(shared['configs'],2)
                self.assertTrue(any('configuration refused' in line for line in third.globals().logs.values()))
            else:
                third.execute('state.active=0;state.completed=1;update()');lines=list(third.globals().logs.values())
                self.assertFalse(any('ERROR' in line for line in lines),lines)
                self.assertFalse(any('WORLD_POSITION_RNG fixture=' in line for line in lines))
                self.assertTrue(any('WORLD_POSITION_RNG_STOP owned=0' in line for line in lines))


if __name__=='__main__':unittest.main()
