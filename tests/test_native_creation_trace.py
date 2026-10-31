from pathlib import Path
import copy
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_creation_evidence as creation
import native_registry_md_evidence as md
import native_registry_origin_evidence as origin_evidence
import native_registry_origin_identity_evidence as identity_evidence
import test_native_gate_registry_md as md_tests
from build_native_management_overlay import compose


def bits(value):return int.from_bytes(struct.pack('<d',value),'little')


def fixture():
    obj,ctrl,agent,tls,owner,base,ref,pilot_ref=0x10000,0x20000,0x30000,0x40000,0x50000,0x140000000,0x60000,0x70000
    runtime=9007199254740995
    status={k:0 for k in creation.FOOTER}
    status.update(schema=1,started=1,closing=1,count=11,parts=17,peak=5,handle=0x500000003,state=4,phase1=6,phase2=29,builder_order=33,
                  builder_runtime=runtime,thread=123,object=obj,runtime=runtime,controller=ctrl,agent=agent,
                  generate=1,initialize=1,pilot=1,navigation=1,agent_calls=1,setter=1)
    # Synthetic sequence retains native call semantics and actual result fields.
    calls=[(1,7,28,0,1),(2,8,27,1,2),(3,9,12,2,3),(10,10,11,3,4),(4,13,24,2,3),(5,14,17,5,4),
           (7,15,16,6,5),(8,18,19,5,4),(7,20,21,5,4),(9,22,23,5,4),(6,25,26,2,3)]
    def snapshot(order):
        s={k:0 for k in creation.SNAP};identity={k:0 for k in creation.IDENTITY}
        s.update(thread=123,ui=1,tls=tls,view=1,time=bits(100.0),rng_pointer=tls+0x318,rng=1001+(order>=16)+(order>=21),read=1,write=0,reads=31,stable=1)
        if order>=8:
            s.update(object=obj,runtime=runtime,identity_recorded=1,identity_stable=1)
            identity.update(object=obj,type=0x2b99a88,actual=runtime,object_state=5,owner_getter=0x352b10,
                            owner=owner if order>8 else 0,owner_state=5 if order>8 else 1,controller_state=1,reads=31,
                            raw_type=base+0x2b99a88,raw_owner_getter=base+0x352b10)
        if order>=13:
            s.update(controller=ctrl,direct_reads=1 if order==13 else 127)
            if order>13:s.update(controller_type=base+0x2c51158,controller_owner=obj,c8=2)
        if order>=17:s.update(agent=agent,agent_owner=obj,timer=bits(100.75),direct_reads=s['direct_reads']|1920)
        if order>17:s.update(controller_agent=agent,slots=agent+0xad0)
        if order>=26:identity.update(controller=ctrl,controller_type=0x2c51158,controller_owner=obj,controller_state=5,
                                     slots=agent+0xad0,agent=agent,agent_owner=obj,agent_state=5,reads=1023,raw_controller_type=base+0x2c51158)
        return s,identity
    records=[]
    for index,(kind,enter,leave,parent,depth) in enumerate(calls,1):
        caller=creation.CALLERS.get(kind,0xe85531 if index==7 else 0xea90e1 if kind==7 else 0x1066dc6)
        meta=dict(kind=kind,enter=enter,leave=leave,handle=status['handle'],phase1=6,parent=parent,depth=depth,caller=caller,
                  argc=creation.ARGC[kind],result=ref if kind==1 else pilot_ref if kind==3 else agent if kind==5 else 0x3e800000 if index==7 else 0x3f000000 if kind==7 else 0,
                  result_kind=creation.RESULT[kind],error=0,foreign=0)
        args={1:[0,ref],2:[0,ref],3:[0,pilot_ref,ref],4:[ctrl,obj,0,0,1,0],5:[agent,obj],6:[obj,ctrl,1],
              7:[tls+0x318,0x3f000000 if index==7 else 0x40000000],8:[obj,0x20f,bits(-1.0),bits(-1.0),0],
              9:[obj,0x80000,bits(108.5),1],10:[0x90000,0xa0000,0xb0000,0,4,1]}[kind]
        args=args+[0]*(10-len(args));before,bi=snapshot(enter);after,ai=snapshot(leave)
        ranged={k:0 for k in creation.RANGE};ranged['script']='none'
        if kind==10:
            hashed=14695981039346656037
            for byte in b'orders.base':hashed=((hashed^byte)*1099511628211)&(2**64-1)
            ranged.update(verified=caller,source=1,reads=15,entity=runtime,script_hash=hashed,script='orders.base')
        parts=([meta,{'args':','.join(f'{a:016x}' for a in args)},before,after,bi,dict(bi),ai,dict(ai)]+
               [{'raw':'00'*416} for _ in range(8)]+[ranged])
        records.append([dict(schema=1,index=index,part=part,complete=1,**row) for part,row in enumerate(parts)])
    return records,status


def line(row):
    part=row.get('part');family='footer' if part is None else 'meta' if part==0 else 'snap' if part in (2,3) else 'identity' if 4<=part<=7 else 'range' if part==16 else ''
    def field(key,value):
        if key in creation.FIXED.get(family,set()):return f'{key}={value:016x}'
        if family=='snap' and key=='c8':return f'{key}={value:08x}'
        return f'{key}={value:x}' if key in creation.HEX else f'{key}={value}'
    return ' '.join(field(k,v) for k,v in row.items())


def render(records,status):
    return '\n'.join('NATIVE_GATE CREATION_TRACE '+line(row) for record in records for row in record)+'\nNATIVE_GATE CREATION_TRACE_STOP '+line(status)+'\n'


def combined_fixture():
    records,status=fixture()
    # The real first-origin observer executes after range entry and before the
    # original startup script. Reserve shared ordinal11 for that first update.
    for record in records:
        for key in ('enter','leave'):
            if record[0][key]>=11:record[0][key]+=1
    status['phase2']+=1;status['builder_order']+=1
    lines=[]
    for source in md_tests.RegistryMDBoundaryTests().capture().splitlines():
        if source.startswith('NATIVE_GATE REGISTRY_MD '):
            fields=source.split(' ',2)[2].split();updated=[]
            for field in fields:
                key,value=field.split('=',1)
                if key in ('order','pair') and int(value)>6:value=str(int(value)+23)
                updated.append(key+'='+value)
            source='NATIVE_GATE REGISTRY_MD '+' '.join(updated)
        elif source.startswith('NATIVE_GATE REGISTRY_MD_STOP '):source=source.replace('order=24','order=46')
        else:source=source.replace('order=23','order=11').replace('order=24','order=46')
        lines.append(source)
    record=records[3];meta,before,after,ranged=record[0],record[2],record[3],record[16]
    args=[int(v,16) for v in record[1]['args'].split(',')]
    origin={k:0 for k in origin_evidence.FIELDS}
    origin.update(schema=1,runtime=status['runtime'],order=11,thread=123,recorded=1,source=2,caller=meta['caller'],verified=meta['caller'],caller_source=1,
                  context=args[1],exec=args[0],program=args[2],begin=args[3],end=args[4],mode=args[5],pc=ranged['pc'],context_pc=ranged['context_pc'],
                  type=0x2b99a88,entity=ranged['entity'],depth=ranged['depth'],ui=before['ui'],view=before['view'],time=before['time'],
                  rng_before=before['rng'],rng_after=after['rng'],result=meta['result'],reads=0x3ff,complete=1,script_hash=ranged['script_hash'],script=ranged['script'])
    identity={k:0 for k in identity_evidence.FIELDS};identity.update({k:v for k,v in record[4].items() if k in identity_evidence.FIELDS})
    identity.update(schema=1,runtime=status['runtime'],order=11,thread=123,recorded=1,rereads=identity['reads'],
                    reobject=identity['object_state'],reowner=identity['owner_state'],recontroller=identity['controller_state'],reagent=identity['agent_state'],stable=1)
    return records,status,lines,origin,identity


def render_combined(records,status,lines,origin,identity):
    def row(values,hexes):return ' '.join(f'{k}={v:x}' if k in hexes else f'{k}={v}' for k,v in values.items())
    return ('\n'.join(lines)+'\n'+render(records,status)+'NATIVE_GATE REGISTRY_UPDATE_ORIGIN '+row(origin,origin_evidence.HEX)+
            '\nNATIVE_GATE REGISTRY_ORIGIN_STOP schema=1 count=1 enabled=1 closed=1 failures=0 open=0 restore_open=0 peak=1\n'+
            'NATIVE_GATE REGISTRY_ORIGIN_IDENTITY '+row(identity,identity_evidence.HEX)+'\n')


class CreationTraceTests(unittest.TestCase):
    def test_complete_local_trace_preserves_raw_selectors_timers_and_exact_runtime(self):
        records,status=fixture();trace=creation.evidence(render(records,status),required=True)
        self.assertTrue(trace['supported']);self.assertFalse(trace['enrollment_supported']);self.assertFalse(trace['sector_pose_supported'])
        self.assertEqual(trace['agent_timer'],bits(100.75));self.assertEqual(trace['posted_due'],bits(108.5))
        self.assertEqual(trace['status']['runtime'],9007199254740995)
        self.assertEqual(len(trace['orders']),22);self.assertEqual(trace['records'][6]['before']['rng'],1001)
        self.assertEqual(trace['records'][6]['after']['rng'],1002)
        self.assertEqual(trace['records'][4]['after']['read'],1);self.assertEqual(trace['records'][4]['after']['c8'],2)

    def test_default_off_and_required_missing_are_distinct(self):
        self.assertFalse(creation.evidence('historical')['supported'])
        with self.assertRaises(ValueError):creation.evidence('',required=True)
        with self.assertRaises(ValueError):creation.evidence(render(*fixture()))

    def test_each_incomplete_footer_field_fails_closed(self):
        for key in ('failures','first_error','dropped','open','restore_open','restore_failures','owned','enabled','enrollment_supported','sector_pose_supported'):
            records,status=fixture();status[key]=1
            with self.subTest(key=key),self.assertRaises(ValueError):creation.evidence(render(records,status),required=True)

    def test_truncation_duplicate_partial_outref_scope_and_lifetime_fail(self):
        changes=[(0,0,'complete',0),(0,0,'leave',29),(1,0,'parent',0),(3,0,'caller',1),(6,0,'error',16),
                 (6,0,'foreign',1),(6,2,'rng_pointer',0),(4,3,'stable',0),(4,3,'write',1),(4,3,'slots',0),
                 (1,4,'actual',3),(1,5,'actual',3),(3,16,'script_hash',0),(3,16,'reads',0),(10,0,'depth',8)]
        for index,part,key,value in changes:
            records,status=fixture();records[index][part][key]=value
            with self.subTest(key=key,index=index),self.assertRaises(ValueError):creation.evidence(render(records,status),required=True)
        records,status=fixture();text=render(records,status)
        for bad in (text.split('\n',1)[1],text+text.splitlines()[0]+'\n',text.replace('raw='+'00'*416,'raw=00',1)):
            with self.assertRaises(ValueError):creation.evidence(bad,required=True)

    def test_actual_post_flag_due_and_conditional_branch_are_required(self):
        records,status=fixture()
        for index,position,value in ((9,3,0),(9,2,0x7ff8000000000000),(8,1,0),(6,0,0)):
            changed=copy.deepcopy(records);args=changed[index][1]['args'].split(',');args[position]=f'{value:016x}';changed[index][1]['args']=','.join(args)
            with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)
        records[7][0]['result']=1
        with self.assertRaises(ValueError):creation.evidence(render(records,status),required=True)

    def test_shared_ordinals_must_join_native_phase_and_builder(self):
        records,status,lines,origin,identity=combined_fixture();joined=render_combined(records,status,lines,origin,identity)
        errors=[];result=md.evidence(joined,1,errors,origin_required=True,identity_required=True,creation_required=True)
        self.assertEqual(errors,[]);self.assertTrue(result['creation']['supported'])
        self.assertEqual(result['creation']['first_origin_record'],4)
        errors=[];md.evidence(joined,1,errors);self.assertTrue(errors)
        status['builder_order']+=1;errors=[];md.evidence(render_combined(records,status,lines,origin,identity),1,errors,origin_required=True,identity_required=True,creation_required=True);self.assertTrue(errors)

    def test_creation_phase_execution_tick_is_one_reserved_tick_window(self):
        for tick in (5,6):
            data=list(combined_fixture());data[2]=[line.replace('tick=6','tick='+str(tick)) for line in data[2]]
            errors=[];md.evidence(render_combined(*data),1,errors,origin_required=True,identity_required=True,creation_required=True)
            self.assertEqual(errors,[])
        for phase in ('kind=1 phase=1 ','kind=1 phase=2 ','kind=2 phase=0 '):
            data=list(combined_fixture());data[2]=[line.replace(' tick=6 ',' tick=5 ') if phase in line else line for line in data[2]]
            errors=[];md.evidence(render_combined(*data),1,errors,origin_required=True,identity_required=True,creation_required=True)
            self.assertTrue(any('creation token/phase/Builder native join' in error for error in errors))

    def test_first_origin_exact_range_clock_rng_and_identity_cannot_be_replaced(self):
        for key in ('exec','context','program','begin','end','mode','pc','context_pc','entity','depth','time','rng_before','rng_after','result'):
            data=list(combined_fixture());data[3][key]+=1;errors=[]
            with self.subTest(key=key):
                md.evidence(render_combined(*data),1,errors,origin_required=True,identity_required=True,creation_required=True)
                self.assertTrue(errors)
        data=list(combined_fixture());data[3]['caller']=data[3]['verified']=0x1066d8d;errors=[]
        md.evidence(render_combined(*data),1,errors,origin_required=True,identity_required=True,creation_required=True);self.assertTrue(errors)
        data=list(combined_fixture());data[4]['owner']+=1;errors=[]
        md.evidence(render_combined(*data),1,errors,origin_required=True,identity_required=True,creation_required=True);self.assertTrue(errors)
        errors=[];md.evidence(render_combined(*combined_fixture()),1,errors,creation_required=True);self.assertTrue(errors)

    def test_every_native_narrow_argument_and_pilot_outref_is_checked(self):
        records,status=fixture()
        for index,record in enumerate(records):
            for position,width in creation.NARROW.get(record[0]['kind'],{}).items():
                changed=copy.deepcopy(records);args=changed[index][1]['args'].split(',');args[position]=f'{2**width:016x}';changed[index][1]['args']=','.join(args)
                with self.subTest(kind=record[0]['kind'],position=position),self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)
        for part,key in ((0,'result'),(1,'args')):
            changed=copy.deepcopy(records)
            if part==0:changed[2][part][key]=0
            else:
                args=changed[2][1]['args'].split(',');args[1]='0'*16;changed[2][1]['args']=','.join(args)
            with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)

    def test_completed_setter_presence_states_and_native_floats_are_checked(self):
        records,status=fixture();changed=copy.deepcopy(records)
        for part in (6,7):
            changed[10][part].update(controller=0,controller_type=0,controller_owner=0,controller_state=1,slots=0,agent=0,agent_owner=0,agent_state=0,reads=31,raw_controller_type=0)
        with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)
        changed=copy.deepcopy(records)
        changed[2][0]['leave']=10;changed[3][0].update(enter=11,leave=12,parent=2,depth=3)
        with self.assertRaisesRegex(ValueError,'Pilot scope'):creation.evidence(render(changed,status),required=True)
        changed=copy.deepcopy(records)
        for part in (4,5):changed[2][part]['owner_state']=99
        with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)
        changed=copy.deepcopy(records);changed[5][3]['timer']=0x7ff8000000000000
        with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)
        changed=copy.deepcopy(records);args=changed[7][1]['args'].split(',');args[2]=args[3]='7ff8000000000000';changed[7][1]['args']=','.join(args)
        with self.assertRaises(ValueError):creation.evidence(render(changed,status),required=True)

    def test_lua_drain_preserves_parts_once_and_reports_partial_stop(self):
        lua=LuaRuntime(unpack_returned_tuples=True);module=lua.execute((ROOT/'tools/dev/native_creation_trace.lua').read_text())
        lua.globals().module=module
        lua.execute('''logs={};count=1;complete=1;stopped=false
          gate={poll=function()return {creation_trace_schema=1}end,creation_trace_config=function(m)assert(m:find('handle=0000000500000003',1,true));return 1 end,
          creation_trace_status=function()return 'schema=1 count='..count..' parts=17 done=0' end,
          creation_trace_row=function(i,p)if complete==0 and not stopped then return nil end;return 'schema=1 index='..i..' part='..p..' complete='..complete..' x=0' end}
          drain=module.new(gate,{native_gate_creation_trace_schema=1,native_gate_construction_test=true,native_gate_registry_md_trace=true},function(s)logs[#logs+1]=s end)
          drain(false);drain(false);count=2;complete=0;drain(false);stopped=true;drain(true);drain(true)
        ''')
        logs=list(lua.globals().logs.values());self.assertEqual(len(logs),35)
        self.assertIn('index=2 part=16 complete=0',logs[-2]);self.assertTrue(logs[-1].startswith('CREATION_TRACE_STOP '))
        lua.execute('count=0;logs={};complete=1;drain=module.new(gate,{native_gate_creation_trace_schema=1,native_gate_construction_test=true,native_gate_registry_md_trace=true},function(s)logs[#logs+1]=s end);count=129')
        with self.assertRaises(Exception):lua.execute('drain(false)')

    def test_composer_requires_explicit_schema_capability_and_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'base';(source/'ui').mkdir(parents=True);(source/'native').mkdir()
            config=''.join('X4LAN_CONFIG.'+name+' = true\n' for name in ('native_gate_command_test','native_gate_order_state','native_gate_world_identities','native_gate_contact_diag','native_gate_registry_md_trace'))
            config+='X4LAN_CONFIG.native_gate_snapshot_sha = "'+'a'*64+'"\n'
            config+='X4LAN_CONFIG.native_gate_registry_origin_schema = 1\nX4LAN_CONFIG.native_gate_registry_origin_identity_schema = 1\n'
            (source/'ui/config.lua.append').write_text(config);(source/'ui/native_gate_probe.lua').write_text((ROOT/'tools/dev/native_gate_probe.lua').read_text())
            (source/'ui.xml.files').write_text('ui/native_gate_probe.lua\n');(source/'native/x4lan_gate_64.dll').write_bytes(b'missing')
            def manifest():
                result={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
                (source/'x4lane-manifest.json').write_text(json.dumps(result));return result
            before=manifest()
            for overlap in (source,source/'nested',root):
                with self.assertRaisesRegex(ValueError,'overlap'):compose(source,overlap,'construction',True)
            self.assertFalse((source/'nested').exists());self.assertEqual(before,manifest())
            with self.assertRaisesRegex(ValueError,'construction family'):compose(source,root/'wrong','management',True)
            with self.assertRaisesRegex(ValueError,'native capability'):compose(source,root/'missing','construction',True)
            self.assertFalse((root/'missing').exists())
            compose(source,root/'default','construction')
            self.assertNotIn('native_gate_creation_trace_schema',(root/'default/ui/config.lua.append').read_text())
            self.assertFalse((root/'default/ui/native_creation_trace.lua').exists());self.assertEqual(before,manifest())
            (source/'native/x4lan_gate_64.dll').write_bytes(b'creation_trace_config creation_trace_status creation_trace_row CREATION_TRACE_2;handle=')
            before=manifest();compose(source,root/'trace','construction',True);self.assertEqual(before,manifest())
            out=root/'trace';self.assertIn('native_gate_creation_trace_schema = 2',(out/'ui/config.lua.append').read_text())
            ui=(out/'ui.xml.files').read_text();self.assertLess(ui.index('ui/native_creation_trace.lua'),ui.index('ui/native_gate_probe.lua'))
            self.assertEqual((out/'md/native_gate_construction.xml').read_bytes(),(ROOT/'tools/dev/native_gate_construction.xml').read_bytes())
            actual={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
            self.assertEqual(actual,json.loads((out/'x4lane-manifest.json').read_text()))
            with self.assertRaisesRegex(ValueError,'frozen'):compose(source,out,'construction',True)

    def test_production_abort_drains_closure_once_and_preserves_original_error(self):
        source=(ROOT/'tools/dev/native_gate_probe.lua').read_text();abort=source[source.index("SetScript('onUpdate',function()") :]
        for failure in ('action','once','permanent'):
            with self.subTest(failure=failure):
                lua=LuaRuntime(unpack_returned_tuples=True);lua.execute((ROOT/'tools/dev/native_creation_trace.lua').read_text())
                lua.globals().failure=failure
                lua.execute('''logs={};stops=0;native_owned=true;armed=true;done=false;stopped=false
                  log=function(s)logs[#logs+1]=s end
                  cfg={native_gate_creation_trace_schema=1,native_gate_construction_test=true,native_gate_registry_md_trace=true,native_gate_player_observer=true}
                  gate={poll=function()return {creation_trace_schema=1,active=stopped and 0 or 1,restored=stopped and 1 or 0,issued=5}end,
                    creation_trace_config=function()return 1 end,
                    stop=function()stopped=true;stops=stops+1;return 1 end,
                    creation_trace_status=function()return 'schema=1 count=1 parts=17 done=0' end,
                    creation_trace_row=function(i,p)
                      if p==7 and (failure=='permanent' or (failure=='once' and not stopped)) then return nil end
                      return 'schema=1 index='..i..' part='..p..' complete=1 x=0'
                    end}
                  creation_trace=X4LAN_NATIVE_CREATION_TRACE.new(gate,cfg,log)
                  X4LAN_NATIVE_PLAYER_TRACE=function()log('OTHER_DRAIN')end
                  frame=function()if done then return end;creation_trace(false);error('original action failed')end
                  SetScript=function(name,handler)assert(name=='onUpdate');callback=handler end
                ''')
                lua.execute(abort);lua.execute('callback();callback()');logs=list(lua.globals().logs.values())
                self.assertEqual(lua.globals().stops,1)
                errors=[row for row in logs if row.startswith('ERROR ')];self.assertEqual(len(errors),1)
                self.assertIn('original action failed' if failure=='action' else 'creation trace changed',errors[0])
                parts=[row for row in logs if row.startswith('CREATION_TRACE schema=')]
                self.assertEqual(len(parts),len(set(parts)))
                if failure=='permanent':
                    drain_error=next(i for i,row in enumerate(logs) if row.startswith('CREATION_TRACE_DRAIN_ERROR '))
                    self.assertLess(drain_error,logs.index('OTHER_DRAIN'));self.assertFalse(any(row.startswith('CREATION_TRACE_STOP ') for row in logs))
                else:
                    self.assertEqual(len(parts),17);self.assertEqual(sum(row.startswith('CREATION_TRACE_STOP ') for row in logs),1)


if __name__=='__main__':unittest.main()
