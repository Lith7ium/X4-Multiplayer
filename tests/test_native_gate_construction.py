from pathlib import Path
import sys
import tempfile
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_construction_evidence import PLAN_SHA,SEMANTIC,BUILDER_HANDLE,evidence,registry_evidence
from build_native_management_overlay import construction_semantic,compose


class ConstructionTransactions(unittest.TestCase):
    def fixture(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        module=lua.execute((ROOT/'tools/dev/native_construction_transactions.lua').read_text())
        model=module.new();enqueued=[]
        def enqueue(action):enqueued.append((action.ticket,action.op,action.asset,action.slot))
        return model,enqueued,enqueue

    def test_pending_does_not_mean_native_success_and_locks_shared_plan(self):
        model,queued,enqueue=self.fixture()
        result=model.batch(1,'49010000','49010000',enqueue)
        self.assertIn('1,73,1,0,PENDING,1,1-1-1-1',result)
        self.assertIn('2,73,1,0,DENIED_BUSY,1,none',result)
        self.assertIsNone(model.completed[1]);self.assertEqual(len(queued),1)
        model.ack('1-1-1-1','plan','1|struct_arg_base_02_macro')
        self.assertEqual(model.completed[1],'1-1-1-1')
        result=model.batch(2,'00000000','4a020000',enqueue)
        self.assertIn('PENDING',result)
        self.assertEqual(len(queued),2)
        model.ack('2-2-2-1','deploy','1|DeployToStation')
        with self.assertRaises(Exception):model.ack('2-2-2-1','deploy','1|DeployToStation')

    def test_malformed_second_command_has_no_enqueue_and_builder_waits_for_plan(self):
        model,queued,enqueue=self.fixture()
        with self.assertRaises(Exception):model.batch(1,'49010000','4a020001bad',enqueue)
        self.assertEqual(queued,[])
        self.assertIn('DENIED_DEPENDENCY',model.batch(1,'00000000','4a020000',enqueue))
        self.assertEqual(queued,[])
        self.assertIn('DENIED_OWNER',model.batch(2,'4a020000','00000000',enqueue))

    def test_wrong_ack_and_timeout_cannot_convert_request_to_completion(self):
        model,queued,enqueue=self.fixture();model.batch(1,'49010000','00000000',enqueue)
        for ticket,kind,result in [('9-1-1-1','plan','1|struct_arg_base_02_macro'),('1-1-1-1','plan','1|different_macro')]:
            with self.assertRaises(Exception):model.ack(ticket,kind,result)
        self.assertIsNone(model.completed[1])
        for tick in range(2,62):model.batch(tick,'00000000','00000000',enqueue)
        with self.assertRaises(Exception):model.batch(62,'00000000','00000000',enqueue)
        with self.assertRaises(Exception):model.ack('1-1-1-1','plan','1|struct_arg_base_02_macro')
        with self.assertRaises(Exception):model.batch(62,'00000000','00000000',enqueue)
        self.assertEqual(len(queued),1)

    def test_lua_parses_and_plan_semantics_digest(self):
        import hashlib
        lua=LuaRuntime()
        for name in ('native_gate_construction.lua','native_construction_transactions.lua','native_construction_raw.lua'):
            lua.execute('assert(loadstring(...))',(ROOT/'tools/dev'/name).read_text())
        self.assertEqual(PLAN_SHA,hashlib.sha256(SEMANTIC.encode()).hexdigest())
        path=ROOT/'tools/dev/native_gate_construction_plan.xml'
        self.assertEqual(construction_semantic(path),SEMANTIC)
        with tempfile.TemporaryDirectory() as directory:
            modified=Path(directory)/'plan.xml'
            for text in (path.read_text().replace('x="0"','x="1"'),path.read_text().replace('index="1"','index="2"'),path.read_text().replace('struct_arg_base_02_macro','other_macro')):
                modified.write_text(text)
                with self.assertRaises(ValueError):construction_semantic(modified)

    def test_construction_composer_rejects_base_without_import_and_contact_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'base';(source/'ui').mkdir(parents=True)
            required='\n'.join(f'X4LAN_CONFIG.{k} = true' for k in ('native_gate_command_test','native_gate_order_state','native_gate_snapshot_sha'))
            for enabled,missing in (('', 'world_identities'),('\nX4LAN_CONFIG.native_gate_world_identities = true','contact_diag'),('\nX4LAN_CONFIG.native_gate_world_identities = true\nX4LAN_CONFIG.native_gate_contact_diag = true','registry_md_trace')):
                (source/'ui/config.lua.append').write_text(required+enabled)
                with self.assertRaisesRegex(ValueError,missing):compose(source,Path(directory)/'out','construction')

    def test_composer_refuses_old_probe_before_copying_passive_fixture(self):
        import hashlib,json
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'base';(source/'ui').mkdir(parents=True)
            names=('native_gate_command_test','native_gate_order_state','native_gate_snapshot_sha','native_gate_world_identities','native_gate_contact_diag','native_gate_registry_md_trace')
            (source/'ui/config.lua.append').write_text('\n'.join(f'X4LAN_CONFIG.{name}=true' for name in names))
            (source/'ui/native_gate_probe.lua').write_text('X4LAN_NATIVE_CONSTRUCTION.bind_gate(gate)\n-- native_gate_registry_md_trace registry_md_installed')
            manifest={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
            (source/'x4lane-manifest.json').write_text(json.dumps(manifest))
            output=Path(directory)/'out'
            with self.assertRaisesRegex(ValueError,'passive raw motion'):compose(source,output,'construction')
            self.assertFalse(output.exists())

    def test_adapter_can_load_before_base_sha_module_but_checks_before_effects(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('RegisterEvent=function() end;DebugError=function() end')
        lua.globals().X4LAN_CONFIG=lua.table_from({'native_gate_construction_test':True,'slot':'p1','native_gate_construction_plan_sha':PLAN_SHA})
        lua.execute((ROOT/'tools/dev/native_construction_transactions.lua').read_text())
        lua.execute((ROOT/'tools/dev/native_construction_raw.lua').read_text())
        lua.execute((ROOT/'tools/dev/native_gate_construction.lua').read_text())
        adapter=lua.globals().X4LAN_NATIVE_CONSTRUCTION
        with self.assertRaises(Exception):adapter.apply(1,'00000000','00000000')
        lua.globals().X4LAN_SHA256=lua.table_from({'digest':lambda text: PLAN_SHA if text==SEMANTIC else 'wrong'})
        lua.execute('gate={ship_reserve=function()end,ship_bind=function()end,ship_status=function()end,ship_raw_motion=function()end,poll=function()end};weapon_helper=function()return 1 end')
        adapter.bind_gate(lua.globals().gate,lua.globals().weapon_helper)
        self.assertEqual(adapter.apply(1,'00000000','00000000'),'INPUTS_1:construction:00000000:00000000')

    def adapter_fixture(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
          local ffi=require('ffi');local native={};local proxy={C=native}
          for _,k in ipairs({'cdef','typeof','new','string','cast'}) do proxy[k]=ffi[k] end
          require=function() return proxy end
          native.GetCurrentBuildProgress=function() return 12.5 end
          position_z=5000
          native.GetObjectPositionInSector=function() error('transform getter must never run') end
          native.SetContainerBuildMethod=function() end
          native.GetContainerBuildMethod=function() return 'default' end
          native.GetNumUpgradeSlots=function(_,_,kind) return kind=='weapon' and 2 or 8 end
          native.GetUpgradeSlotCurrentComponent=function(_,kind,slot) return (occupied_armament==kind and slot==1) and 55 or 0 end
          events={};RegisterEvent=function(name,fn) events[name]=fn end
          diagnostics={};DebugError=function(line) diagnostics[#diagnostics+1]=line end;setup_count=0
          GetComponentData=function() return 'ship_arg_xl_builder_01_a_macro','player' end
          AddUITriggeredEvent=function(_,control) if control=='Setup' then setup_count=setup_count+1 end end
          ConvertIDTo64Bit=function(v) return v end
          X4LAN_NATIVE_ORDERS=function() return 'ORDERS_1;1;0;default,Wait' end
          clock=0;bound_tick=0;late_updates=0;reserved_tick=0;gate={}
          registry_md_installed=1
          gate.poll=function() return {issued=clock,registry_md_installed=registry_md_installed} end
          gate.ship_reserve=function(handle,macro,owner,tick)
           assert(handle=='0000000500000003' and macro=='ship_arg_xl_builder_01_a_macro' and owner=='player' and tick==5)
           if refuse_reserve then return nil,'registry=reservation_guard stage=reservation' end
           reserved_tick=tick;return 15
          end
          gate.ship_bind=function(handle,native_id)
           if refuse_bind then return nil,'registry=bind stage=identity' end
           assert(handle=='0000000500000003');bound_tick=clock;bound_native_id=native_id
           if corrupt_pose then position_z=5001 end
           return 1,bind_identity
          end
          gate.ship_status=function(handle)
           return string.format('SHIP_REGISTRY_1;handle=%s;slot=15;reserved_tick=%d;bound_tick=%d;prepared_tick=%d;first_update_tick=%d;first_update_kind=%d;late_updates=%d;identity_ok=%d',handle,reserved_tick,bound_tick,bound_tick,clock>=7 and 7 or 0,clock>=7 and 1 or 0,late_updates,bound_tick>0 and 1 or 0)
          end
          gate.ship_raw_motion=function(handle,native_id)
           assert(handle=='0000000500000003' and native_id=='18446744073709551614')
           if refuse_raw then return nil,'raw=unstable' end
           local pose=string.rep('0',272);local motion='5811c502'..string.rep('0',372)
           if position_z~=5000 then
            pose=pose:sub(1,16)..'01'..pose:sub(19)
            motion=motion:sub(1,8)..'01'..motion:sub(11)
           end
           local raw='SHIP_RAW_MOTION_1;handle='..handle..';slot=15;tick='..clock..';sector_pose_supported=0;identity_ok=1;stable=1;read_before=0;write_before=1;read_after=0;write_after=1;unknown_references=0;reference_flags=00000000;parent=0000000000000000;raw_pose='..pose..';motion='..motion..';timing='..string.rep('0',90)
           local private='SHIP_RAW_MOTION_LOCAL_1;runtime=fffffffffffffffe;refs='..string.rep('0000000000000000,',4)..'0000000000000000'
           return raw,private
          end
          weapon_helper=function(object,slot,handle)
           assert(tostring(object)=='18446744073709551614ULL' and slot==15 and handle=='0000000500000003')
           if refuse_weapons then return nil,'unsupported' end
           weapon_enrolled=true;return 1
          end
          create_fixture=function()
           events['X4LAN_NativeConstruction.Station'](nil,101)
           events['X4LAN_NativeConstruction.Storage'](nil,102)
           events['X4LAN_NativeConstruction.Builder'](nil,18446744073709551614ULL)
          end
        ''')
        lua.globals().X4LAN_CONFIG=lua.table_from({'native_gate_construction_test':True,'native_gate_registry_md_trace':True,'slot':'p1','native_gate_construction_plan_sha':PLAN_SHA})
        lua.globals().X4LAN_SHA256=lua.table_from({'digest':lambda _:PLAN_SHA})
        lua.execute((ROOT/'tools/dev/native_construction_transactions.lua').read_text())
        lua.execute((ROOT/'tools/dev/native_construction_raw.lua').read_text())
        lua.execute((ROOT/'tools/dev/native_gate_construction.lua').read_text())
        lua.globals().X4LAN_NATIVE_CONSTRUCTION.bind_gate(lua.globals().gate,lua.globals().weapon_helper)
        return lua,lua.globals().X4LAN_NATIVE_CONSTRUCTION

    def test_adapter_hashes_native_float_progress_and_exact_creation_identity(self):
        lua,adapter=self.adapter_fixture()
        lua.execute('''
          for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end
          clock=6;create_fixture()
          for tick=6,20 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end
        ''')
        self.assertIn(':12.5:',adapter.sample())
        self.assertIn(':SHIP_RAW_MOTION_1;handle='+BUILDER_HANDLE,adapter.sample())
        self.assertIn(';first_update_tick=7;first_update_kind=1;late_updates=0;identity_ok=1',adapter.sample())
        self.assertEqual(lua.globals().bound_native_id,'18446744073709551614')
        self.assertIn(' native_id=18446744073709551614 sector_pose_supported=0 raw_phase=before','\n'.join(lua.globals().diagnostics.values()))
        self.assertNotIn('18446744073709551614',adapter.sample())
        self.assertTrue(lua.globals().weapon_enrolled)
        self.assertTrue(adapter.sample().startswith('CONSTRUCTION_6:'))
        self.assertIn(':CONSTRUCTION_TARGETS_2;station_ready=1;storage_ready=1;sector_pose_supported=0',adapter.sample())
        self.assertLess(max(map(len,lua.globals().diagnostics.values())),900)
        self.assertNotIn('GetObjectPositionInSector',(ROOT/'tools/dev/native_gate_construction.lua').read_text())

    def test_target_identity_is_available_only_after_its_own_native_callback(self):
        lua,adapter=self.adapter_fixture()
        lua.execute("for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end")
        self.assertTrue(adapter.sample().endswith('CONSTRUCTION_TARGETS_2;station_ready=0;storage_ready=0;sector_pose_supported=0'))
        lua.execute("clock=6;events['X4LAN_NativeConstruction.Station'](nil,101);X4LAN_NATIVE_CONSTRUCTION.apply(6,'00000000','00000000')")
        self.assertTrue(adapter.sample().endswith('CONSTRUCTION_TARGETS_2;station_ready=1;storage_ready=0;sector_pose_supported=0'))
        self.assertIn('CONSTRUCTION_TARGET_BOUND tick=6 kind=station', '\n'.join(lua.globals().diagnostics.values()))
        self.assertNotIn('kind=storage','\n'.join(lua.globals().diagnostics.values()))

    def test_marked_setup_requires_config_and_actual_native_observer_readiness(self):
        for fault in ('registry_md_installed=0','registry_md_installed=nil','X4LAN_CONFIG.native_gate_registry_md_trace=false'):
            lua,adapter=self.adapter_fixture()
            lua.execute("for tick=1,4 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;clock=5;"+fault)
            with self.assertRaisesRegex(Exception,'ready native MD boundary observer'):adapter.apply(5,'00000000','00000000')
            self.assertEqual(lua.globals().setup_count,0);self.assertEqual(lua.globals().reserved_tick,0)

    def test_md_markers_bracket_actions_without_changing_original_fixture(self):
        import hashlib,json,xml.etree.ElementTree as ET
        root=ET.parse(ROOT/'tools/dev/native_gate_construction.xml').getroot()
        actions=root.find('./cues/cue[@name="Setup"]/actions/do_if')
        children=list(actions)
        markers=[e for e in children if e.tag=='raise_lua_event' and e.get('name')=="'X4LAN_NativeConstruction.Boundary'"]
        self.assertEqual([e.get('param') for e in markers],list(map(str,range(1,9))))
        expected={1:(1,'create_ship'),2:(-1,'create_ship'),3:(1,'raise_lua_event'),4:(-1,'raise_lua_event'),5:(1,'add_units'),6:(-1,'add_units'),7:(1,'create_order'),8:(-1,'create_order')}
        for marker in markers:
            offset,tag=expected[int(marker.get('param'))]
            neighbor=children[children.index(marker)+offset]
            self.assertEqual(neighbor.tag,tag)
            if tag=='raise_lua_event':self.assertEqual(neighbor.get('name'),"'X4LAN_NativeConstruction.Builder'")
            actions.remove(marker)
        def shape(e):return [e.tag,sorted(e.attrib.items()),(e.text or '').strip(),[shape(x) for x in e]]
        digest=hashlib.sha256(json.dumps(shape(root),separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,'365ac759ea97e45a30d8cc66b6d032b918de37ab32daa9399bab1878dac06093')

    def test_deferred_boundary_log_cannot_mask_primary_native_refusal(self):
        lua,adapter=self.adapter_fixture()
        lua.execute("for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;clock=6;gate.poll=function()error('stopped')end;events['X4LAN_NativeConstruction.Boundary'](nil,8)")
        self.assertIn('CONSTRUCTION_BOUNDARY_UI phase=8 tick=0 previous_admitted=5','\n'.join(lua.globals().diagnostics.values()))

    def test_creation_refuses_before_setup_without_reservation_and_rejects_late_binding(self):
        lua,adapter=self.adapter_fixture()
        lua.execute("for tick=1,4 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;refuse_reserve=true;clock=5")
        with self.assertRaisesRegex(Exception,'registry=reservation_guard stage=reservation'):adapter.apply(5,'00000000','00000000')
        self.assertEqual(lua.globals().setup_count,0)
        for fault in ('clock=7','clock=6;late_updates=1','clock=6;corrupt_pose=true','clock=6;refuse_weapons=true','clock=6;refuse_raw=true',"clock=6;occupied_armament='weapon'","clock=6;occupied_armament='turret'"):
            lua,adapter=self.adapter_fixture()
            lua.execute("for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end")
            lua.execute(fault)
            with self.assertRaises(Exception):lua.globals().create_fixture()

    def test_creation_frame_preflights_both_slots_and_cannot_repeat_setup(self):
        lua,adapter=self.adapter_fixture()
        lua.execute("for tick=1,4 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;clock=5")
        with self.assertRaises(Exception):adapter.apply(5,'00000000','bad')
        self.assertEqual(lua.globals().reserved_tick,0);self.assertEqual(lua.globals().setup_count,0)
        adapter.apply(5,'00000000','00000000')
        self.assertEqual(lua.globals().setup_count,1)
        with self.assertRaises(Exception):adapter.apply(5,'00000000','00000000')
        self.assertEqual(lua.globals().setup_count,1)

    def test_prebind_diagnostics_survive_identity_refusal_and_optional_ui_failure(self):
        for unavailable in (False,True):
            lua,adapter=self.adapter_fixture()
            lua.execute("for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;clock=6;refuse_bind=true")
            if unavailable:lua.execute("GetComponentData=function() error('UI unavailable') end")
            with self.assertRaisesRegex(Exception,'registry=bind stage=identity'):lua.globals().create_fixture()
            lines='\n'.join(lua.globals().diagnostics.values())
            self.assertIn('CONSTRUCTION_ARMAMENT tick=6 state=UNARMED_1;weapon_slots=2;turret_slots=8;weapons=0;turrets=0',lines)
            expected='available=0 macro=unavailable owner=unavailable' if unavailable else 'available=1 macro=ship_arg_xl_builder_01_a_macro owner=player'
            self.assertIn('CONSTRUCTION_BUILDER_UI tick=6 '+expected,lines)
            self.assertIn('CONSTRUCTION_NATIVE_REFUSED operation=ship_bind reason=registry=bind stage=identity',lines)
            self.assertNotIn('CONSTRUCTION_BIND_END',lines)
            self.assertEqual(lua.globals().bound_tick,0)
            self.assertFalse(lua.globals().weapon_enrolled)

    def test_successful_bind_retains_secondary_identity_diagnostic(self):
        lua,adapter=self.adapter_fixture()
        lua.execute("bind_identity='SHIP_BIND_IDENTITY_1 object_type=2b99a88';for tick=1,5 do clock=tick;X4LAN_NATIVE_CONSTRUCTION.apply(tick,'00000000','00000000') end;clock=6;create_fixture()")
        self.assertIn('CONSTRUCTION_NATIVE_IDENTITY SHIP_BIND_IDENTITY_1 object_type=2b99a88','\n'.join(lua.globals().diagnostics.values()))
        self.assertTrue(lua.globals().weapon_enrolled)
class ConstructionEvidence(unittest.TestCase):
    def test_runner_fails_on_management_and_construction_md_and_lua_errors(self):
        import ast,re
        tree=ast.parse((ROOT/'tools/dev/run_native_gate.py').read_text())
        pattern=next(node.value for node in ast.walk(tree) if isinstance(node,ast.Constant) and isinstance(node.value,str) and node.value.startswith('Error while executing'))
        for text in ('Error in MD cue md.X4LAN_NativeConstruction.Plan: Property lookup failed',
                     'Error in MD cue md.X4LAN_NativeManagement.Setup: invalid container',
                     "Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_gate_construction.lua'",
                     "Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_gate_management.lua'",
                     "Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_construction_raw.lua'",
                     "Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_construction_transactions.lua'",
                     "Error while executing the Lua script 'extensions/x4_lan_lab/ui/native_management_admission.lua'",
                     'NATIVE_GATE STOP slot=p1 completed=5 reason=75 restored=1',
                     'NATIVE_GATE REFUSED unsupported build'):
            self.assertIsNotNone(re.search(pattern,text))
        self.assertIsNone(re.search(pattern,'NATIVE_GATE STOP slot=p1 completed=120 reason=1 restored=1'))

    def fixture(self):
        import struct
        states={tick:{str(s):{'command':'49010000' if tick==30 and s==1 else '4a020000' if tick==90 and s==2 else '00000000'} for s in (1,2)} for tick in range(181)}
        lines=[];latest='pending';pending={};complete={};revisions={1:0,2:0}
        seeds={'claytronics':17,'energycells':34,'hullparts':62}
        for tick in range(1,181):
            commands=[states[tick-1][str(s)]['command'] for s in (1,2)]
            lines.append(f'NATIVE_GATE INPUTS tick={tick} mode=construction p1={commands[0]} p2={commands[1]}')
            ack=''
            if tick in (32,92):
                asset=1 if tick==32 else 2;kind='plan' if asset==1 else 'deploy';result='1|struct_arg_base_02_macro' if asset==1 else '1|DeployToStation'
                ticket=pending.pop(asset);complete[asset]=ticket;ack=ticket+'|'+kind+'|'+result
                lines.append(f'NATIVE_GATE CONSTRUCTION_ACK tick={tick} ticket={ticket} kind={kind} result={result}')
            if tick>20 and (tick-1)%30==20:
                at=tick-1
                flags='0|0|0|0|0|0|0' if at<31 else '1|0|0|0|0|0|0' if at<100 else '1|1|1|1|1|0|0' if at<170 else '0|0|1|1|1|1|1'
                resources='' if at<31 else ';'.join(f'{w},{n},{n if at<100 else 0}' for w,n in seeds.items())
                latest=f'{at}|{flags}|{resources}'
                lines.append(f'NATIVE_GATE CONSTRUCTION_MD tick={at} observed={tick} state={latest}')
            receipt=''
            if tick in (31,91):
                asset=1 if tick==31 else 2;player=asset;op=73 if asset==1 else 74
                revisions[asset]=1;ticket=f'{tick}-{player}-{asset}-1';pending[asset]=ticket
                receipt=f'{player},{op},{asset},0,PENDING,1,{ticket}'
                lines.append(f'NATIVE_GATE CONSTRUCTION_RECEIPTS tick={tick} data={receipt}')
                if asset==1:
                    for w,n in seeds.items():lines.append(f'NATIVE_GATE CONSTRUCTION_RESOURCE_SEED ticket={ticket} ware={w} amount={n}')
            if tick<20:state=f'CONSTRUCTION_1:{tick}:pending:{PLAN_SHA}'
            else:
                progress=50.0 if 100<=tick<170 else -1.0
                orders='ORDERS_1;1;1;1,DeployToStation,started,3,1' if tick>=92 else 'ORDERS_1;1;0;default,Wait'
                state=':'.join(['CONSTRUCTION_1',str(tick),PLAN_SHA,','.join(str(revisions[a]) for a in (1,2)),','.join(pending.get(a,'none') for a in (1,2)),','.join(complete.get(a,'none') for a in (1,2)),latest,struct.pack('<f',progress).hex(),orders,receipt,ack])
            lines.append(f'NATIVE_GATE CONSTRUCTION_STATE tick={tick} state={state}')
        return '\n'.join(lines),states

    def test_native_ack_completion_and_resource_evidence_required(self):
        source,states=self.fixture();errors=[]
        _,applied,_,coverage=evidence(source,1,states,errors,True)
        self.assertEqual(errors,[]);self.assertEqual(len(applied),2)
        self.assertTrue(coverage['module_completed'])
        for bad in (source.replace('claytronics,17,0','claytronics,17,18'),
                    source.replace('1|struct_arg_base_02_macro','1|wrong_macro'),
                    source.replace('00004842','000080bf'),
                    source.replace('0|0|1|1|1|1|1','0|0|1|1|1|0|1')):
            errors=[];evidence(bad,1,states,errors,True);self.assertTrue(errors)

    def test_round_trip_progress_text_is_checked_and_empty_payload_cannot_pass(self):
        source,states=self.fixture()
        source=source.replace('CONSTRUCTION_1','CONSTRUCTION_2').replace(':00004842:',':50:').replace(':000080bf:',':-1:')
        source='\n'.join(line+':BUILDER_POSE_1,20000,100000,5000,0,0,0' if 'CONSTRUCTION_STATE' in line and line.split(' state=')[1].split(':')[2]!='pending' else line for line in source.splitlines())
        errors=[];evidence(source,1,states,errors,True);self.assertEqual(errors,[])
        for bad in (source.replace(':50:',':nan:'),source.replace(':50:',':101:'),source.replace(':50:','::'),source.replace('BUILDER_POSE_1,20000','BUILDER_POSE_1,nan'),source.replace(',5000,0,0,0',',5000,0,0')):
            errors=[];evidence(bad,1,states,errors,True);self.assertTrue(errors)

    def test_builder_pose_is_checked_from_creation_and_cannot_disappear(self):
        source,states=self.fixture()
        source=source.replace('CONSTRUCTION_1','CONSTRUCTION_3').replace(':00004842:',':50:').replace(':000080bf:',':-1:')
        pose='BUILDER_POSE_1,20000,100000,5000,0,0,0'
        lines=[]
        for line in source.splitlines():
            if 'CONSTRUCTION_STATE' in line:
                tick=int(line.split('tick=')[1].split()[0])
                line+=':'+('builder_pending' if tick<6 else pose)
            lines.append(line)
        source='\n'.join(lines)
        errors=[];_,_,_,coverage=evidence(source,1,states,errors,True)
        self.assertEqual(errors,[]);self.assertEqual(coverage['builder_pose_first_tick'],6)
        initial=f'CONSTRUCTION_3:7:pending:{PLAN_SHA}:'
        for bad in (source.replace(initial+pose,initial+'builder_pending'),source.replace(initial+pose,initial+'BUILDER_POSE_1,nan,100000,5000,0,0,0')):
            errors=[];evidence(bad,1,states,errors,True);self.assertTrue(errors)

    def registry_fixture(self):
        source,states=self.fixture()
        source=source.replace('CONSTRUCTION_1','CONSTRUCTION_4').replace(':00004842:',':50:').replace(':000080bf:',':-1:')
        pose='BUILDER_POSE_1,20000,100000,5000,0,0,0'
        def registry(tick):
            if tick<5:return 'registry_pending'
            bound=6 if tick>=6 else 0;first=7 if tick>=7 else 0
            return f'SHIP_REGISTRY_1;handle={BUILDER_HANDLE};slot=15;reserved_tick=5;bound_tick={bound};prepared_tick={bound};first_update_tick={first};first_update_kind={int(first>0)};late_updates=0;identity_ok={int(bound>0)}'
        lines=[]
        for line in source.splitlines():
            if 'CONSTRUCTION_STATE' in line:
                tick=int(line.split('tick=')[1].split()[0])
                if tick==6:
                    lines.append(f'NATIVE_GATE CONSTRUCTION_BIND_BEGIN tick=6 previous_admitted=5 handle={BUILDER_HANDLE} pose={pose}')
                    lines.append(f'NATIVE_GATE CONSTRUCTION_BIND_END tick=6 registry={registry(6)} pose={pose}')
                line+=':'+('builder_pending' if tick<6 else pose)+':'+registry(tick)+':'+('armament_pending' if tick<6 else 'UNARMED_1;weapon_slots=2;turret_slots=8;weapons=0;turrets=0')
            lines.append(line)
        source='\n'.join(lines)
        return source,states,registry,pose

    def test_native_registry_receipts_require_creation_boundary_and_first_update(self):
        source,states,registry,pose=self.registry_fixture()
        errors=[];_,_,_,coverage=evidence(source,1,states,errors,True)
        self.assertEqual(errors,[]);self.assertEqual(coverage['builder_bound_tick'],6)
        self.assertEqual(coverage['builder_first_update'],(7,1))
        bad_values=[source.replace(old,new) for old,new in (
            (';slot=15;',';slot=13;'),(';reserved_tick=5;',';reserved_tick=4;'),
            (';prepared_tick=6;',';prepared_tick=7;'),(';first_update_tick=7;',';first_update_tick=5;'),
            (';late_updates=0;',';late_updates=1;'),(';identity_ok=1',';identity_ok=0'),
            (';weapons=0;',';weapons=1;'),(';turret_slots=8;',';turret_slots=129;'),
            ('CONSTRUCTION_BIND_END','MISSING_BIND_END'))]
        bad_values.append(source.replace(f'CONSTRUCTION_BIND_END tick=6 registry={registry(6)} pose={pose}',f'CONSTRUCTION_BIND_END tick=6 registry={registry(6)} pose={pose.replace("5000","5001")}'))
        for bad in bad_values:
            errors=[];evidence(bad,1,states,errors,True);self.assertTrue(errors)

    def test_native_target_pose_hashes_need_callbacks_and_remain_complete(self):
        source,states,_,_=self.registry_fixture()
        source=source.replace('CONSTRUCTION_4','CONSTRUCTION_5')
        station='STATION_POSE_1,20000,100000,0,0,0,0'
        storage='STORAGE_POSE_1,20100,100000,-400,0,0,0'
        lines=[]
        for line in source.splitlines():
            if 'CONSTRUCTION_STATE' in line:
                tick=int(line.split('tick=')[1].split()[0])
                if tick==6:
                    lines.append('NATIVE_GATE CONSTRUCTION_TARGET_BOUND tick=6 kind=station pose='+station)
                    lines.append('NATIVE_GATE CONSTRUCTION_TARGET_BOUND tick=6 kind=storage pose='+storage)
                line+=':CONSTRUCTION_TARGETS_1;'+('station_pending;storage_pending' if tick<6 else station+';'+storage)
            lines.append(line)
        source='\n'.join(lines)
        errors=[];_,_,_,coverage=evidence(source,1,states,errors,True)
        self.assertEqual(errors,[])
        self.assertEqual(coverage['target_pose_first_ticks'],{'station':6,'storage':6})
        bad_values=[source.replace(old,new) for old,new in (
            ('kind=station','kind=missing'),('kind=storage','kind=missing'),
            ('TARGET_BOUND tick=6','TARGET_BOUND tick=7'),
            ('STORAGE_POSE_1,20100','STORAGE_POSE_1,nan'),
            ('CONSTRUCTION_TARGETS_1;'+station,'CONSTRUCTION_TARGETS_1;station_pending'),
            ('CONSTRUCTION_TARGETS_1;station_pending;storage_pending','CONSTRUCTION_TARGETS_1;'+station+';'+storage),
            ('CONSTRUCTION_5:7:','CONSTRUCTION_4:7:'))]
        bad_values.append(source+'\nNATIVE_GATE CONSTRUCTION_TARGET_BOUND tick=6 kind=storage pose='+storage)
        for bad in bad_values:
            errors=[];evidence(bad,1,states,errors,True);self.assertTrue(errors)

    def passive_fixture(self):
        source,states,registry,pose=self.registry_fixture()
        lua,_=ConstructionTransactions().adapter_fixture()
        helper=lua.globals().X4LAN_NATIVE_CONSTRUCTION_RAW
        result=[]
        def emit(kind,tick,phase,data):helper.emit(lambda value:result.append('NATIVE_GATE '+value),kind,tick,phase,data)
        for line in source.splitlines():
            if 'CONSTRUCTION_BIND_BEGIN' in line:
                lua.globals().clock=6
                raw,private=lua.globals().gate.ship_raw_motion(BUILDER_HANDLE,'18446744073709551614')
                for kind,native_id in (('station',101),('storage',102)):
                    result.append(f'NATIVE_GATE CONSTRUCTION_TARGET_BOUND tick=6 kind={kind} native_id={native_id} sector_pose_supported=0')
                for phase in ('before','after'):
                    emit('RAW',6,phase,raw)
                    result.append(f'NATIVE_GATE CONSTRUCTION_RAW_LOCAL tick=6 phase={phase} data={private}')
                result.append(f'NATIVE_GATE CONSTRUCTION_BIND_BEGIN tick=6 previous_admitted=5 handle={BUILDER_HANDLE} native_id=18446744073709551614 sector_pose_supported=0 raw_phase=before')
            elif 'CONSTRUCTION_BIND_END' in line:
                result.append(f'NATIVE_GATE CONSTRUCTION_BIND_END tick=6 registry={registry(6)} sector_pose_supported=0 raw_phase=after')
            elif 'CONSTRUCTION_STATE' in line:
                tick=int(line.split('tick=')[1].split()[0]);state=line.split(' state=')[1]
                lua.globals().clock=tick
                raw=lua.globals().gate.ship_raw_motion(BUILDER_HANDLE,'18446744073709551614')[0]
                state=state.replace('CONSTRUCTION_4','CONSTRUCTION_6').replace(pose,raw)
                state+=f':CONSTRUCTION_TARGETS_2;station_ready={int(tick>=6)};storage_ready={int(tick>=6)};sector_pose_supported=0'
                emit('STATE',tick,'sample',state)
            else:result.append(line)
        return '\n'.join(result),states

    def test_passive_schema_can_decode_but_cannot_pass_strict_sector_pose_acceptance(self):
        source,states=self.passive_fixture();errors=[]
        _,_,decoded,coverage=evidence(source,1,states,errors,False)
        self.assertEqual(errors,[]);self.assertEqual(len(decoded),180)
        self.assertIsNone(coverage['builder_pose_first_tick']);self.assertEqual(coverage['builder_raw_first_tick'],6)
        self.assertEqual(coverage['target_pose_first_ticks'],{})
        self.assertEqual(coverage['target_callback_first_ticks'],{'station':6,'storage':6})
        self.assertFalse(coverage['sector_pose_supported']);self.assertFalse(coverage['strict_completion_supported'])
        errors=[];evidence(source,1,states,errors,True)
        self.assertEqual(errors,['player 1 construction sector poses unsupported; strict construction acceptance unavailable'])

    def test_passive_schema_requires_complete_chunks_and_exact_callback_lifetimes(self):
        source,states=self.passive_fixture()
        for before,after in (('STATE_PART tick=7 phase=sample part=1','STATE_PART tick=7 phase=sample part=2'),
                             ('RAW_END tick=6 phase=before','RAW_END tick=7 phase=before'),
                             ('kind=storage native_id=102','kind=storage native_id=101'),
                             ('native_id=18446744073709551614','native_id=18446744073709551613'),
                             ('sector_pose_supported=0','sector_pose_supported=1')):
            errors=[];evidence(source.replace(before,after),1,states,errors,False);self.assertTrue(errors)


if __name__=='__main__':unittest.main()
