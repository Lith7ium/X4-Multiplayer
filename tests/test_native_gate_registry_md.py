from pathlib import Path
import sys
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/dev'))
import native_registry_md_evidence as md

class RegistryMDBoundaryTests(unittest.TestCase):
    def capture(self,early_nav=False,unrelated=False):
        rows=[];order=0;first=0;other=0
        sequence=[(3,0),(4,0)]+[(1,p) for p in (1,2,3)]+[(2,0)]+[(1,p) for p in (4,5,6,7,8)]
        for kind,phase in sequence:
            if unrelated and phase==1:order+=1;other=order
            if early_nav and phase==2:order+=1;first=order
            pair=order+1
            for edge in (0,1):
                order+=1
                row=dict(schema=2,order=order,pair=pair,tick=6,handle=0x500000003,edge=edge,kind=kind,phase=phase,
                         caller=0xc2095a if kind==1 else 0xc208cd,payload=phase if kind==1 else 9007199254740995 if kind==2 else kind,
                         ui=1,main=0,thread=123,view=1,time=0x401cbb645a1cac08,ready=1,sink=0xabcdef,reads=31,error=0,result=edge,pending=1,
                         name='X4LAN_NativeConstruction.'+{1:'Boundary',2:'Builder',3:'Station',4:'Storage'}[kind])
                rows.append('NATIVE_GATE REGISTRY_MD '+' '.join(f'{k}={v:016x}' if k in {'handle','payload','time'} else f'{k}={v:x}' if k in md.HEX else f'{k}={v}' for k,v in row.items()))
        if not first:order+=1;first=order
        order+=1;ai=order;kind=2 if early_nav else 6
        if unrelated:rows.append(f'NATIVE_GATE REGISTRY_UPDATE_FIRST schema=1 runtime=000000000000006f tick=6 kind=3 order={other} thread=456 object=0 ai_order=0 ai_thread=0')
        rows.append(f'NATIVE_GATE REGISTRY_UPDATE_FIRST schema=1 runtime=0020000000000003 tick=6 kind={kind} order={first} thread=123 object=1 ai_order={ai} ai_thread=123')
        return '\n'.join(rows)+f'\nNATIVE_GATE REGISTRY_MD_STOP schema=2 count=22 order={order} updates={2 if unrelated else 1} failures=0 restored=1 open=0 restore_open=0 peak=1\nCONSTRUCTION_BIND_BEGIN tick=6 native_id=9007199254740995\npending_tick=6 pending_kind={kind} pending_ai=1 pending_order={first} pending_thread=123 ai_script=orders.base ai_order={ai} ai_thread=123\n'

    def test_complete_trace_preserves_exact_identity_and_first_ai_location(self):
        errors=[];trace=md.evidence(self.capture(),1,errors)
        self.assertEqual(errors,[]);self.assertEqual(trace['first_ai_order'],24)
        self.assertEqual(trace['builder_runtime'],9007199254740995)
        self.assertEqual(len(trace['rows']),22);self.assertEqual(trace['first_update_order'],23)
        self.assertTrue(trace['builder_entry_precedes_first_update'])
        self.assertEqual(trace['ordering_scope'],'same_native_thread')
        self.assertTrue(trace['ui_view_observed'])

    def test_missing_unknown_late_restore_and_identity_evidence_refuse(self):
        for before,after in [('count=22','count=21'),('failures=0','failures=1'),('restored=1','restored=0'),
                             ('reads=1f','reads=f'),('error=0','error=80'),('ui=1','ui=2'),('kind=1 phase=1','kind=1 phase=2'),
                             ('caller=c2095a','caller=c208cd'),('ai_order=24','ai_order=23'),('native_id=9007199254740995','native_id=9007199254740996'),
                             ('X4LAN_NativeConstruction.Boundary','unknown'),('pair=1','pair=2'),('open=0','open=1'),
                             ('restore_open=0','restore_open=1'),('ai_thread=123','ai_thread=0'),('updates=1','updates=2'),
                             ('pending_order=23','pending_order=22'),('runtime=0020000000000003','runtime=0020000000000004'),
                             ('X4LAN_NativeConstruction.Station','X4LAN_NativeConstruction.Other')]:
            with self.subTest(before=before):
                errors=[];md.evidence(self.capture().replace(before,after,1),1,errors);self.assertTrue(errors)
        errors=[];md.evidence('\n'.join(self.capture().splitlines()[1:]),1,errors);self.assertTrue(errors)

    def test_earlier_navigation_cannot_be_hidden_by_later_ai(self):
        errors=[];trace=md.evidence(self.capture(early_nav=True),1,errors)
        self.assertEqual(errors,[]);self.assertEqual(trace['first_update_kind'],2)
        self.assertFalse(trace['builder_entry_precedes_first_update'])
        self.assertGreater(trace['first_ai_order'],trace['builder_return_order'])

    def test_passive_targets_correlate_exact_ids_without_transform_getters(self):
        callbacks='CONSTRUCTION_TARGET_BOUND tick=6 kind=station native_id=3 sector_pose_supported=0\nCONSTRUCTION_TARGET_BOUND tick=6 kind=storage native_id=4 sector_pose_supported=0\n'
        errors=[];trace=md.evidence(self.capture()+callbacks,1,errors)
        self.assertEqual(errors,[]);self.assertTrue(trace['ancillary_callback_correlation'])
        self.assertEqual(trace['update_coverage'],'registered_wrapper_entries_only')
        for changed in (callbacks.replace('native_id=3','native_id=4'),callbacks.splitlines()[0],callbacks+callbacks,
                        callbacks.replace('sector_pose_supported=0','sector_pose_supported=1'),callbacks.replace('tick=6','tick=7')):
            errors=[];md.evidence(self.capture()+changed,1,errors);self.assertTrue(errors)

    def test_other_native_lifetime_does_not_replace_builder_first_update(self):
        errors=[];trace=md.evidence(self.capture(unrelated=True),1,errors)
        self.assertEqual(errors,[]);self.assertEqual(len(trace['updates']),2)
        self.assertTrue(trace['builder_entry_precedes_first_update']);self.assertEqual(trace['first_update_order'],24)

    def test_distinct_native_threads_are_valid_observation_not_same_thread_proof(self):
        errors=[];trace=md.evidence(self.capture().replace('ui=1 main=0 thread=123 view=1','ui=0 main=1 thread=456 view=0'),1,errors)
        self.assertEqual(errors,[]);self.assertFalse(trace['first_ai_same_thread'])
        self.assertEqual(trace['ordering_scope'],'atomic_observation_only');self.assertFalse(trace['ui_view_observed'])

    def test_probe_drain_is_immutable_and_after_stop_safe(self):
        source=(ROOT/'tools/dev/native_gate_probe.lua').read_text()
        helper=source[source.index('local registry_md_seen=0'):source.index('local function outside_session()')]
        lua=LuaRuntime(unpack_returned_tuples=True)
        lua.execute("cfg={native_gate_registry_md_trace=true};logs={};log=function(s) logs[#logs+1]=s end;gate={registry_md=function(i) return 'order='..i end}")
        lua.execute(helper+'\ndrain=drain_registry_md')
        lua.execute('drain({active=1,registry_md_count=2});drain({active=0,registry_md_count=3});drain({active=0,registry_md_count=3})')
        self.assertEqual(list(lua.globals().logs.values()),['REGISTRY_MD order=1','REGISTRY_MD order=2','REGISTRY_MD order=3'])
        with self.assertRaises(Exception):lua.execute('drain({registry_md_count=2})')
        lua.execute('gate.registry_md=function() end')
        with self.assertRaises(Exception):lua.execute('drain({registry_md_count=4})')

    def test_overlay_default_off_dependency_validation(self):
        import subprocess
        base=[sys.executable,str(ROOT/'tools/dev/build_native_gate_overlay.py'),'--registry-md-trace','--out','unused','--gate-dll','missing','--mode','battle']
        result=subprocess.run(base,capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('--registry-md-trace requires',result.stderr)
