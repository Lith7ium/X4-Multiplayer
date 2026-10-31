from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from lupa.luajit21 import LuaRuntime

ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'tools/dev/native_management_admission.lua').read_text()
sys.path.insert(0,str(ROOT/'tools/dev'))
from native_management_evidence import evidence
from build_native_management_overlay import compose


class ManagementAdmissionTests(unittest.TestCase):
    def test_management_composer_refuses_native_player_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)/'base';(base/'ui').mkdir(parents=True)
            (base/'ui/native_gate_probe.lua').write_text((ROOT/'tools/dev/native_gate_probe.lua').read_text())
            (base/'ui.xml.files').write_text('ui/native_gate_probe.lua\n')
            for mode in ('observe','native-local-negative'):
                (base/'ui/config.lua.append').write_text('X4LAN_CONFIG.native_gate_command_test = "coop"\nX4LAN_CONFIG.native_gate_order_state = true\nX4LAN_CONFIG.native_gate_snapshot_sha = "test"\nX4LAN_CONFIG.native_gate_player_probe = "'+mode+'"\n')
                manifest={str(p.relative_to(base)):hashlib.sha256(p.read_bytes()).hexdigest() for p in base.rglob('*') if p.is_file() and p.name!='x4lane-manifest.json'}
                (base/'x4lane-manifest.json').write_text(json.dumps(manifest))
                output=Path(tmp)/mode
                with self.subTest(mode=mode),self.assertRaisesRegex(ValueError,'combined diagnostic not implemented: native_gate_player_probe'):
                    compose(base,output)
                self.assertFalse(output.exists())

    def fixture(self):
        lua=LuaRuntime(unpack_returned_tuples=True)
        module=lua.execute(SOURCE)
        model=module.new()
        mutations=[]
        def commit(action):
            mutations.append((action.slot,action.asset,action.op,action.value))
            return True
        return model,mutations,commit

    def test_same_tick_shared_account_conflict_then_fresh_retry(self):
        model,mutations,commit=self.fixture()
        result=model.batch(1,'40010200','40010300',commit)
        self.assertEqual(mutations,[(1,1,64,2)])
        self.assertIn('2,64,1,3,0,STALE_REVISION,1',result)
        result=model.batch(2,'00000000','40010301',commit)
        self.assertEqual(mutations[-1],(2,1,64,3))
        self.assertEqual(model.revisions[1],2)

    def test_malformed_second_command_has_no_partial_native_effect(self):
        for second in ('bad','ff010000','40000600','43020100'):
            model,mutations,commit=self.fixture()
            with self.subTest(second=second),self.assertRaises(Exception):
                model.batch(1,'40010200',second,commit)
            self.assertEqual(mutations,[])
            self.assertEqual(model.revisions[1],0)

    def test_private_asset_denied_without_consuming_owner_revision(self):
        model,mutations,commit=self.fixture()
        result=model.batch(1,'00000000','43020000',commit)
        self.assertEqual(mutations,[])
        self.assertIn('DENIED_OWNER,0',result)
        model.batch(2,'43020000','43030000',commit)
        self.assertEqual(mutations,[(1,2,67,0),(2,3,67,0)])

    def test_duplicate_gap_failure_and_revision_exhaustion_fail_closed(self):
        model,mutations,commit=self.fixture()
        model.batch(1,'40010200','00000000',commit)
        for tick in (1,3):
            with self.assertRaises(Exception):model.batch(tick,'40010201','00000000',commit)
        self.assertEqual(len(mutations),1)
        model.revisions[1]=255
        self.assertIn('REVISION_EXHAUSTED',model.batch(2,'400102ff','00000000',commit))
        self.assertEqual(len(mutations),1)
        with self.assertRaises(Exception):model.batch(3,'43020000','00000000',lambda _:False)
        self.assertEqual(model.revisions[2],0)

    def test_new_lua_files_parse(self):
        lua=LuaRuntime()
        for name in ('native_management_admission.lua','native_gate_management.lua'):
            lua.execute('assert(loadstring(...))',(ROOT/'tools/dev'/name).read_text())

    def test_denial_preserves_revision_and_partial_failure_poisoning(self):
        model,mutations,commit=self.fixture()
        result=model.batch(1,'46010000','00000000',lambda _: 'DENIED_FUNDS')
        self.assertIn('DENIED_FUNDS,0',result)
        self.assertEqual(model.revisions[1],0)
        partial=[]
        def second_fails(action):
            if action.slot==2:raise RuntimeError('simulated native effect failure')
            return commit(action)
        with self.assertRaises(Exception):
            model.batch(2,'43020000','43030000',second_fails,lambda receipt,*_:partial.append(receipt))
        self.assertEqual(mutations,[(1,2,67,0)])
        self.assertIn('1,67,2,0,0,APPLIED,1;2,67,3,0,0,NATIVE_FAILURE,0',partial[0])
        with self.assertRaises(Exception):model.batch(2,'43020000','00000000',commit)
        with self.assertRaises(Exception):model.batch(3,'43020001','00000000',commit)
        self.assertEqual(len(mutations),1)

    def evidence_fixture(self):
        states={tick:{str(slot):{'command':'40010200' if tick==30 and slot==1 else '00000000'} for slot in (1,2)} for tick in range(33)}
        lines=[]
        for tick in range(1,33):
            a,b=(states[tick-1][str(slot)]['command'] for slot in (1,2))
            lines.append(f'NATIVE_GATE INPUTS tick={tick} mode=management p1={a} p2={b}')
            receipt='1,64,1,2,0,APPLIED,1' if tick==31 else ''
            if tick<20:state=f'MANAGEMENT_1:{tick}:pending'
            else:
                wallet,budget,revision=(80000,20000,1) if tick>=31 else (100000,0,0)
                state=f'MANAGEMENT_1:{tick}:{wallet}:{budget}:default:0:-1:{revision},0,0:ORDERS_1;0;0:ORDERS_1;0;0:{receipt}'
            lines.append(f'NATIVE_GATE MANAGEMENT_STATE tick={tick} state={state}')
            if receipt:
                lines.extend([f'NATIVE_GATE MANAGEMENT_RECEIPTS tick={tick} data={receipt}',
                              f'NATIVE_GATE MANAGEMENT_EFFECT tick={tick} slot=1 op=64 asset=1 before=100000,0 after=80000,20000'])
        return '\n'.join(lines),states

    def test_independent_evidence_checks_actual_effect_and_conservation(self):
        log,states=self.evidence_fixture();errors=[]
        commands,applied,samples=evidence(log,1,states,errors)
        self.assertEqual(errors,[])
        self.assertEqual(applied,{(31,1):'APPLIED'})
        self.assertEqual(len(samples),32)
        self.assertEqual(commands[31],'INPUTS_1:management:40010200:00000000')
        for bad in (log.replace('after=80000,20000','after=80001,20000'),
                    log.replace('before=100000,0','before=99999,1'),
                    log.replace('1,64,1,2,0,APPLIED,1','1,64,1,2,0,STALE_REVISION,1'),
                    log.replace('MANAGEMENT_STATE tick=32','MANAGEMENT_STATE tick=31')):
            errors=[]
            evidence(bad,1,states,errors)
            self.assertTrue(errors)

    def test_three_account_conservation_and_insufficient_funds_refusal(self):
        log,states=self.evidence_fixture()
        states[30]['1']['command']='47010200';states[30]['2']['command']='46010001'
        log=log.replace('MANAGEMENT_1','MANAGEMENT_2').replace('p1=40010200 p2=00000000','p1=47010200 p2=46010001')
        lines=[]
        for line in log.splitlines():
            if 'MANAGEMENT_STATE' in line and not line.endswith('pending'):
                tick=int(line.split('tick=')[1].split()[0]);line += ':20000' if tick>=31 else ':0'
                if tick>=31:line=line.replace(':80000:20000:',':80000:0:')
            line=line.replace('1,64,1,2,0,APPLIED,1','1,71,1,2,0,APPLIED,1;2,70,1,0,1,DENIED_FUNDS,1')
            line=line.replace('op=64 asset=1 before=100000,0 after=80000,20000','op=71 asset=1 before=100000,0,0 after=80000,0,20000')
            lines.append(line)
        lines.append('NATIVE_GATE MANAGEMENT_DENIAL tick=31 slot=2 reason=DENIED_FUNDS before=80000,0,20000 target=1000000000000 after=80000,0,20000')
        source='\n'.join(lines);errors=[]
        evidence(source,1,states,errors)
        self.assertEqual(errors,[])
        for bad in (source.replace('target=1000000000000','target=1000'),source.replace('after=80000,0,20000','after=80000,1,20000')):
            errors=[];evidence(bad,1,states,errors);self.assertTrue(errors)


class NativeManagementGuardTests(unittest.TestCase):
    def setUp(self):
        self.lua=LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
          local ffi=require('ffi');native={};local proxy={C=native}
          for _,k in ipairs({'cdef','typeof','new','string'}) do proxy[k]=ffi[k] end
          require=function(name) assert(name=='ffi');return proxy end
          X4LAN_CONFIG={native_gate_management_test=true,slot='p1'}
          events={};logs={};counts={[201]=0,[202]=0};identities={[201]=9007199254740993ULL,[202]=9007199254740999ULL}
          removals=0;fail_second=false
          RegisterEvent=function(name,fn) events[name]=fn end
          ConvertIDTo64Bit=function(v) return v end
          ConvertStringTo64Bit=function(v) return tonumber(v) end
          AddUITriggeredEvent=function() end
          DebugError=function(s) logs[#logs+1]=s end
          GetPlayerMoney=function() return 100000 end
          GetComponentData=function() return 0 end
          native.GetContainerBuildMethod=function() return '' end
          native.GetContainerTradeRuleID=function() return 0 end
          native.HasContainerOwnTradeRule=function() return false end
          native.IsComponentOperational=function() return true end
          native.GetOrderID=function(object) return identities[object] end
          native.CreateOrder3=function(object) counts[object]=counts[object]+1;return 1 end
          native.EnableOrder=function(object) return not (object==202 and fail_second) end
          native.RemoveOrder2=function(object) counts[object]=counts[object]-1;removals=removals+1;return true end
          X4LAN_NATIVE_ORDERS=function(object)
            return 'ORDERS_1;1;'..counts[object]..(counts[object]>0 and ';1,Wait,ready,8,1,1,0,0,0,0' or '')..';default,none'
          end
        ''')
        self.lua.execute(SOURCE)
        self.lua.execute((ROOT/'tools/dev/native_gate_management.lua').read_text())
        self.lua.execute('''
          events['X4LAN_NativeManagement.Station'](nil,101)
          events['X4LAN_NativeManagement.Account'](nil,102)
          events['X4LAN_NativeManagement.OperatingAccount'](nil,103)
          events['X4LAN_NativeManagement.Ship1'](nil,201)
          events['X4LAN_NativeManagement.Ship2'](nil,202)
          for tick=1,19 do X4LAN_NATIVE_MANAGEMENT.apply(tick,'00000000','00000000') end
        ''')

    def test_reused_queue_index_with_different_uint64_identity_cannot_cancel(self):
        adapter=self.lua.globals().X4LAN_NATIVE_MANAGEMENT
        adapter.apply(20,'43020000','00000000')
        self.lua.execute('identities[201]=9007199254740994ULL')
        adapter.apply(21,'44020001','00000000')
        state=adapter.sample()
        self.assertIn('DENIED_STALE_ORDER',state)
        self.assertEqual(self.lua.globals().removals,0)
        self.assertNotIn('900719925474099',state)

    def test_setup_preflights_both_slots_and_cannot_repeat_creation(self):
        self.lua.execute((ROOT/'tools/dev/native_gate_management.lua').read_text())
        self.lua.execute('setup_calls=0;AddUITriggeredEvent=function() setup_calls=setup_calls+1 end')
        adapter=self.lua.globals().X4LAN_NATIVE_MANAGEMENT
        for tick in range(1,5):adapter.apply(tick,'00000000','00000000')
        for command in ('bad','43030000'):
            with self.assertRaises(Exception):adapter.apply(5,'00000000',command)
            self.assertEqual(self.lua.globals().setup_calls,0)
        adapter.apply(5,'00000000','00000000')
        with self.assertRaises(Exception):adapter.apply(5,'00000000','00000000')
        self.assertEqual(self.lua.globals().setup_calls,1)

    def test_late_native_failure_emits_partial_state_and_cannot_replay(self):
        adapter=self.lua.globals().X4LAN_NATIVE_MANAGEMENT
        self.lua.execute('fail_second=true')
        with self.assertRaises(Exception):adapter.apply(20,'43020000','43030000')
        log='\n'.join(self.lua.globals().logs.values())
        self.assertIn('MANAGEMENT_PARTIAL tick=20 receipts=1,67,2,0,0,APPLIED,1;2,67,3,0,0,NATIVE_FAILURE,0',log)
        self.assertIn('poisoned=1',log)
        with self.assertRaises(Exception):adapter.apply(21,'43020001','00000000')
        self.assertEqual(self.lua.globals().counts[201],1)


if __name__=='__main__':unittest.main()
