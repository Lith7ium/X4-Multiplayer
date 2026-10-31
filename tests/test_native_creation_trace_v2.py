from pathlib import Path
import copy
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_creation_evidence as legacy
import native_creation_evidence_v2 as creation
import native_registry_md_evidence as md
import native_registry_origin_evidence as origin
import native_registry_origin_identity_evidence as identity
import test_native_creation_trace as old
import test_native_gate_registry_md as md_tests


def fixture():
    obj,ctrl,agent,tls,owner,base,ref,local_ref,pilot_ref,static=0x10000,0x20000,0x30000,0x40000,0x50000,0x140000000,0x60000,0x61000,0x70000,0x80000
    runtime=9007199254740995
    rng1,result1=creation.native_float_state(1001,0x3f000000)
    rng2,result2=creation.native_float_state(rng1,0x40000000)
    status={k:0 for k in creation.FOOTER}
    status.update(schema=2,started=1,closing=1,count=13,parts=18,peak=3,handle=0x500000003,state=5,
                  phase1=6,phase2=18,builder_order=22,builder_runtime=runtime,thread=123,object=obj,runtime=runtime,
                  controller=ctrl,agent=agent,generate=1,initialize=1,pilot=1,navigation=1,agent_calls=1,setter=1,
                  reserved_tick=5,execution_tick=6,prior_static=static,callback_order=50,callback_tick=6,a_closed=1,b_closed=1,static_calls=1,true_owner=owner)
    calls=[(1,7,15,0,1),(2,8,14,1,2),(3,9,10,2,3),(10,11,13,2,3),(6,16,17,0,1),
           (4,32,43,0,1),(5,33,36,6,2),(7,34,35,7,3),(8,37,38,6,2),(7,39,40,6,2),(9,41,42,6,2),(6,44,45,0,1),(12,49,50,0,1)]
    def sample(order):
        s={k:0 for k in creation.SNAP};i={k:0 for k in creation.IDENTITY}
        s.update(thread=123,ui=1,tls=tls,view=1,time=old.bits(100),rng_pointer=tls+0x318,rng=rng2 if order>=40 else rng1 if order>=35 else 1001,read=1,reads=31,stable=1)
        if order>=8:
            s.update(object=obj,runtime=runtime,identity_recorded=1,identity_stable=1)
            i.update(object=obj,type=0x2b99a88,actual=runtime,object_state=5,owner_getter=0x352b10,
                     owner=owner if order>8 else 0,owner_state=5 if order>8 else 1,controller_state=1,reads=31,
                     raw_type=base+0x2b99a88,raw_owner_getter=base+0x352b10)
        if order>=17:
            i.update(controller=static,controller_type=0x2a34b38,controller_state=3,reads=63,raw_controller_type=base+0x2a34b38)
        if order>=32:
            s.update(controller=ctrl,direct_reads=1 if order==32 else 127)
            if order>32:s.update(controller_type=base+0x2c51158,controller_owner=obj,c8=2)
        if order>=36:s.update(agent=agent,agent_owner=obj,timer=old.bits(100.75),direct_reads=s['direct_reads']|1920)
        if order>36:s.update(controller_agent=agent,slots=agent+0xad0)
        if order>=45:i.update(controller=ctrl,controller_type=0x2c51158,controller_owner=obj,controller_state=5,
                              slots=agent+0xad0,agent=agent,agent_owner=obj,agent_state=5,reads=1023,raw_controller_type=base+0x2c51158)
        return s,i
    records=[]
    for index,(kind,enter,leave,parent,depth) in enumerate(calls,1):
        caller=0 if kind==12 else creation.CALLERS.get(kind,0xe85531 if index==8 else 0xea90e1 if kind==7 else 0x1066dc6)
        meta=dict(kind=kind,enter=enter,leave=leave,handle=status['handle'],phase1=6,parent=parent,depth=depth,caller=caller,argc=creation.ARGC[kind],
                  result=ref if kind==1 else pilot_ref if kind==3 else agent if kind==5 else result1 if index==8 else result2 if kind==7 else 0,
                  result_kind=creation.RESULT[kind],error=0,foreign=0)
        args={1:[0,ref],2:[0,local_ref],3:[0,pilot_ref,local_ref],4:[ctrl,obj,static,0,1,0],5:[agent,obj],6:[obj,static if index==5 else ctrl,1],
              7:[tls+0x318,0x3f000000 if index==8 else 0x40000000],8:[obj,0x20f,old.bits(-1),old.bits(-1),0],9:[obj,0x90000,old.bits(108.5),1],
              10:[0xa0000,0xb0000,0xc0000,0,4,1],12:[status['handle'],runtime]}[kind]
        args+=[0]*(10-len(args));before,bi=sample(enter);after,ai=sample(leave)
        ranged={k:0 for k in creation.RANGE};ranged['script']='none'
        if kind==10:
            h=14695981039346656037
            for byte in b'orders.base':h=((h^byte)*1099511628211)&(2**64-1)
            ranged.update(verified=caller,source=1,reads=15,entity=runtime,script_hash=h,script='orders.base')
        aux={k:0 for k in creation.AUX};aux.update(stage=1 if index<=5 else 2,subject=1)
        if kind==6:aux.update(proposed_type=0x2a34b38 if index==5 else 0x2c51158,published_before=0 if index==5 else static,published_after=args[1],reads=3)
        if kind==7:aux.update(rng_before=before['rng'],rng_after=after['rng'],reads=12)
        parts=[meta,{'args':','.join(f'{a:016x}' for a in args)},before,after,bi,dict(bi),ai,dict(ai)]+[{'raw':'00'*416} for _ in range(8)]+[ranged,aux]
        records.append([dict(schema=2,index=index,part=part,complete=1,**row) for part,row in enumerate(parts)])
    return records,status


def line(row):
    part=row.get('part');family='footer' if part is None else 'meta' if part==0 else 'snap' if part in (2,3) else 'identity' if 4<=part<=7 else 'range' if part==16 else 'aux' if part==17 else ''
    def field(k,v):
        if k in creation.FIXED.get(family,set()):return f'{k}={v:016x}'
        if family=='snap' and k=='c8':return f'{k}={v:08x}'
        return f'{k}={v:x}' if k in creation.HEX else f'{k}={v}'
    return ' '.join(field(k,v) for k,v in row.items())


def render(records,status):
    return '\n'.join('NATIVE_GATE CREATION_TRACE '+line(row) for record in records for row in record)+'\nNATIVE_GATE CREATION_TRACE_STOP '+line(status)+'\n'


def insert_ancillary(records,status,parent_index,kind,caller,args,subject):
    parent=records[parent_index-1];enter=parent[0]['enter']+1
    sample=copy.deepcopy(parent[2]);ident=copy.deepcopy(parent[4])
    for record in records:
        for key in ('enter','leave'):
            if record[0][key]>=enter:record[0][key]+=2
        if record[0]['parent']>parent_index:record[0]['parent']+=1
    for key in ('phase2','builder_order','callback_order'):
        if status[key]>=enter:status[key]+=2
    new=copy.deepcopy(parent)
    new[0].update(kind=kind,enter=enter,leave=enter+1,parent=parent_index,depth=parent[0]['depth']+1,caller=caller,argc=creation.ARGC[kind],result=0,result_kind=creation.RESULT[kind])
    new[1]['args']=','.join(f'{x:016x}' for x in args+[0]*(10-len(args)))
    new[2]=dict(sample);new[3]=dict(sample)
    for i in range(4,8):new[i]=dict(ident)
    for k in creation.RANGE:new[16][k]='none' if k=='script' else 0
    for k in creation.AUX:new[17][k]=0
    new[17].update(stage=1,subject=subject)
    if kind==7:
        state,result=creation.native_float_state(55,args[1]);new[0]['result']=result
        new[17].update(rng_before=55,rng_after=state,reads=12);status['ancillary_rng']+=1
    else:status['excluded_setters' if subject==2 else 'unresolved_setters']+=1
    records.insert(parent_index,new);status['count']+=1;status['peak']=max(status['peak'],new[0]['depth'])
    for index,record in enumerate(records,1):
        for part,row in enumerate(record):row.update(schema=2,index=index,part=part,complete=1)
    return new


def combined_fixture():
    records,status=fixture();rows=[];order_map=dict(zip(range(1,23),list(range(1,7))+list(range(18,32))+[46,47]))
    for source in md_tests.RegistryMDBoundaryTests().capture().splitlines():
        if source.startswith('NATIVE_GATE REGISTRY_MD '):
            fields=dict(piece.split('=',1) for piece in source.split(' ',2)[2].split())
            fields['order']=str(order_map[int(fields['order'])]);fields['pair']=str(order_map[int(fields['pair'])]);fields['thread']='123';fields['tick']='6'
            if fields['kind']=='2':fields['payload']=f"{status['runtime']:016x}"
            source='NATIVE_GATE REGISTRY_MD '+' '.join(k+'='+v for k,v in fields.items())
        elif source.startswith('NATIVE_GATE REGISTRY_MD_STOP '):source=source.replace('order=24','order=50')
        elif source.startswith('NATIVE_GATE REGISTRY_UPDATE_FIRST '):
            source=f"NATIVE_GATE REGISTRY_UPDATE_FIRST schema=1 runtime={status['runtime']:016x} tick=6 kind=6 order=12 thread=123 object=1 ai_order=48 ai_thread=123"
        elif 'CONSTRUCTION_BIND_BEGIN' in source:source=re.sub(r'native_id=\d+',f"native_id={status['runtime']}",source)
        else:
            source=re.sub(r'pending_tick=\d+ pending_kind=\d+ pending_ai=\d+ pending_order=\d+ pending_thread=\d+',
                          'pending_tick=6 pending_kind=6 pending_ai=1 pending_order=12 pending_thread=123',source)
            source=re.sub(r'ai_order=\d+ ai_thread=\d+','ai_order=48 ai_thread=123',source)
        rows.append(source)
    record=records[3];meta,before,after,ranged=record[0],record[2],record[3],record[16]
    args=[int(v,16) for v in record[1]['args'].split(',')]
    o={k:0 for k in origin.FIELDS}
    o.update(schema=1,runtime=status['runtime'],order=12,thread=123,recorded=1,source=2,caller=meta['caller'],verified=meta['caller'],caller_source=1,
             context=args[1],exec=args[0],program=args[2],begin=args[3],end=args[4],mode=args[5],pc=ranged['pc'],context_pc=ranged['context_pc'],
             type=0x2b99a88,entity=ranged['entity'],depth=ranged['depth'],ui=before['ui'],view=before['view'],time=before['time'],
             rng_before=before['rng'],rng_after=after['rng'],result=meta['result'],reads=0x3ff,complete=1,script_hash=ranged['script_hash'],script=ranged['script'])
    rows.append('NATIVE_GATE REGISTRY_UPDATE_ORIGIN '+' '.join(k+'='+(f'{v:x}' if k in origin.HEX else str(v)) for k,v in o.items()))
    rows.append('NATIVE_GATE REGISTRY_ORIGIN_STOP schema=1 count=1 enabled=1 closed=1 failures=0 open=0 restore_open=0 peak=1')
    i={k:0 for k in identity.FIELDS};i.update(schema=1,runtime=status['runtime'],order=12,thread=123,recorded=1,stable=1)
    fields=record[4]
    for k in i:
        if k in fields and k not in ('schema',):i[k]=fields[k]
    i.update(rereads=fields['reads'],reobject=fields['object_state'],reowner=fields['owner_state'],recontroller=fields['controller_state'],reagent=fields['agent_state'])
    rows.append('NATIVE_GATE REGISTRY_ORIGIN_IDENTITY '+' '.join(k+'='+(f'{v:x}' if k in identity.HEX else str(v)) for k,v in i.items()))
    return '\n'.join(rows)+'\n'+render(records,status)


class CreationV2Tests(unittest.TestCase):
    def test_two_distinct_stages_and_static_layout(self):
        records,status=fixture();result=legacy.evidence(render(records,status),required=2)
        self.assertTrue(result['a_valid'] and result['b_valid']);self.assertFalse(result['whole_rng_supported'])
        self.assertEqual(result['status']['prior_static'],records[5][4]['controller'])

    def test_generator_outer_reference_and_borrowed_local_reference_are_distinct(self):
        records,status=fixture();trace=legacy.evidence(render(records,status),required=2)
        generate,initialize,pilot=trace['records'][:3]
        self.assertNotEqual(generate['args'][1],initialize['args'][1])
        self.assertEqual(pilot['args'][2],initialize['args'][1])
        self.assertEqual(generate['result'],generate['args'][1])
        self.assertEqual(generate['after']['object'],initialize['before']['object'])
        for changed in ('pilot_local','generate_return','generate_object'):
            with self.subTest(changed=changed):
                mutated=copy.deepcopy(records)
                if changed=='pilot_local':
                    args=mutated[2][1]['args'].split(',');args[2]=f"{generate['args'][1]:016x}";mutated[2][1]['args']=','.join(args)
                elif changed=='generate_return':mutated[0][0]['result']=initialize['args'][1]
                else:mutated[0][3]['object']+=1
                with self.assertRaises(ValueError):legacy.evidence(render(mutated,status),required=2)

    def test_selected_identity_cannot_be_erased_after_initialize_selection(self):
        for index in (1,6):
            with self.subTest(record_index=index+1):
                records,status=fixture()
                records[index][2].update(object=0,runtime=0,identity_recorded=0,identity_stable=0)
                for part in (4,5):
                    for key in creation.IDENTITY:records[index][part][key]=0
                with self.assertRaisesRegex(ValueError,'selected identity missing'):
                    legacy.evidence(render(records,status),required=2)

    def test_setter_aux_and_identity_samples_must_agree(self):
        for index in (4,11):
            records,status=fixture()
            for target,source in ((4,6),(5,7)):
                for key in creation.IDENTITY:records[index][target][key]=records[index][source][key]
            with self.subTest(index=index),self.assertRaisesRegex(ValueError,'publication/identity sample disagreement'):
                legacy.evidence(render(records,status),required=2)

    def test_schema_cannot_reinterpret_old_trace(self):
        records,status=old.fixture()
        with self.assertRaises(ValueError):legacy.evidence(old.render(records,status),required=2)
        self.assertTrue(legacy.evidence(old.render(records,status),required=1)['supported'])
        records,status=fixture()
        with self.assertRaises(ValueError):legacy.evidence(render(records,status),required=1)

    def test_excluded_and_unresolved_setters_are_retained(self):
        for subject,parent in ((3,1),(2,3)):
            records,status=fixture()
            added=insert_ancillary(records,status,parent,6,0x7458c7,[0x123000,0,1],subject)
            self.assertTrue(legacy.evidence(render(records,status),required=2)['supported'])
            added[1]['args']=f"{status['object']:016x}"+added[1]['args'][16:]
            with self.assertRaisesRegex(ValueError,'ownership'):legacy.evidence(render(records,status),required=2)

    def test_non_tls_rng_argument_is_observed_without_whole_rng_claim(self):
        records,status=fixture()
        added=insert_ancillary(records,status,3,7,0x14ddd3a,[0x123000,0x3f800000],4)
        trace=legacy.evidence(render(records,status),required=2)
        self.assertFalse(trace['whole_rng_supported']);self.assertEqual(trace['status']['ancillary_rng'],1)
        added[17]['rng_after']^=1
        with self.assertRaisesRegex(ValueError,'FloatState'):legacy.evidence(render(records,status),required=2)

    def test_schema2_lua_resumes_final_part_without_duplicate_records(self):
        lua=old.LuaRuntime(unpack_returned_tuples=True)
        lua.execute((ROOT/'tools/dev/native_creation_trace.lua').read_text())
        lua.execute('''
          logs={};fail=true
          gate={poll=function()return {creation_trace_schema=2}end,
            creation_trace_config=function(m)assert(m:find('CREATION_TRACE_2;',1,true));return 1 end,
            creation_trace_status=function()return 'schema=2 count=1 parts=18 done=0' end,
            creation_trace_row=function(i,p)if p==17 and fail then error('part interrupted') end;return 'schema=2 index=1 part='..p..' complete=1 x=0' end}
          drain=X4LAN_NATIVE_CREATION_TRACE.new(gate,{native_gate_creation_trace_schema=2,native_gate_construction_test=true,native_gate_registry_md_trace=true},function(x)logs[#logs+1]=x end)
          assert(not pcall(drain,false));assert(#logs==17);fail=false;drain(true);drain(true)
        ''')
        logs=list(lua.globals().logs.values());self.assertEqual(len(logs),19)
        self.assertIn('part=17',logs[-2]);self.assertTrue(logs[-1].startswith('CREATION_TRACE_STOP schema=2'))

    def test_independent_mutations_refuse(self):
        mutations=[('prior_static',lambda r,s:s.update(prior_static=s['prior_static']+1)),
                   ('callback_tick',lambda r,s:s.update(callback_tick=7)),('missing_callback',lambda r,s:s.update(b_closed=0)),
                   ('A failure',lambda r,s:s.update(a_error=1)),('B failure',lambda r,s:s.update(b_error=1)),
                   ('stop overlap',lambda r,s:s.update(restore_open=1)),('RNG overclaim',lambda r,s:s.update(whole_rng_supported=1)),
                   ('legacy parent',lambda r,s:r[3][0].update(parent=3,depth=4)),
                   ('unknown stage',lambda r,s:r[5][17].update(stage=3)),('unknown ownership',lambda r,s:r[5][17].update(subject=5)),
                   ('invented Static owner',lambda r,s:r[4][6].update(controller_owner=s['object'])),
                   ('wrong published controller',lambda r,s:r[11][17].update(published_before=0)),
                   ('callback native caller',lambda r,s:r[12][0].update(caller=0x6cd8be)),
                   ('callback scope',lambda r,s:r[12][17].update(stage=1)),
                   ('wrong RNG sample',lambda r,s:r[7][17].update(rng_before=1)),
                   ('ancillary count omitted',lambda r,s:s.update(ancillary_rng=1))]
        for name,mutate in mutations:
            with self.subTest(name=name):
                records,status=fixture();mutate(records,status)
                with self.assertRaises(ValueError):legacy.evidence(render(records,status),required=2)

    def test_full_md_first_origin_identity_join(self):
        errors=[];result=md.evidence(combined_fixture(),1,errors,True,True,2)
        self.assertEqual(errors,[]);self.assertEqual(result['creation']['first_origin_record'],4)


if __name__=='__main__':unittest.main()
